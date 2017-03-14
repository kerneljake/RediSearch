#ifndef __REDISEARCH_H__
#define __REDISEARCH_H__

#include <stdint.h>
#include <stdlib.h>

typedef u_int32_t t_docId;
typedef u_int32_t t_offset;

#define REDISEARCH_ERR 1
#define REDISEARCH_OK 0

/* A payload object is set either by a query expander or by the user, and can be used to process
 * scores. For examples, it can be a feature vector that is then compared to a feature vector
 * extracted from each result or document */
typedef struct {
  char *data;
  size_t len;
} RSPayload;

/* Internally used document flags */
typedef enum {
  Document_DefaultFlags = 0x00,
  Document_Deleted = 0x01,
  Document_HasPayload = 0x02
} RSDocumentFlags;

/* RSDocumentMetadata describes metadata stored about a document in the index (not the document
* itself).
*
* The key is the actual user defined key of the document, not the incremental id. It is used to
* convert incremental internal ids to external string keys.
*
* Score is the original user score as inserted to the index
*
* Flags is not currently used, but should be used in the future to mark documents as deleted, etc.
*/
typedef struct {
  /* The actual key of the document, not the internal incremental id */
  char *key;

  /* The a-priory document score as given by the user on insertion */
  float score;

  /* The maximum frequency of any term in the index, used to normalize frequencies */
  uint32_t maxFreq : 24;

  /* The total number of tokens in the document */
  uint32_t len : 24;

  /* Document flags  */
  RSDocumentFlags flags : 8;

  /* Optional user payload */
  RSPayload *payload;
} RSDocumentMetadata;

/* Forward declaration of the opaque query object */
struct RSQuery;

/* Forward declaration of the opaque query node object */
struct RSQueryNode;

/* We support up to 30 user given flags for each token, flags 1 and 2 are taken by the engine */
typedef uint32_t RSTokenFlags;

/* A token in the query. The expanders receive query tokens and can expand the query with more query
 * tokens */
typedef struct {
  /* The token string - which may or may not be NULL terminated */
  char *str;
  /* The token length */
  size_t len;

  /* Is this token an expansion? */
  uint8_t expanded : 1;

  /* Extension set token flags - up to 31 bits */
  RSTokenFlags flags : 31;
} RSToken;

/* RSQueryExpanderCtx is a context given to query expanders, containing callback methods and useful
 * data */
typedef struct RSQueryExpanderCtx {

  /* Opaque query object used internally by the engine, and should not be accessed */
  struct RSQuery *query;

  /* Opaque query node object used internally by the engine, and should not be accessed */
  struct RSQueryNode **currentNode;

  /* Private data of the extension, set on extension initialization or during expansion. If a Free
   * calbackk is provided, it will be used automatically to free this data */
  void *privdata;

  /* The language of the query. Defaults to "english" */
  const char *language;

  /* ExpandToken allows the user to add an expansion of the token in the query, that will be
   * union-merged with the given token in query time. str is the expanded string, len is its
  length, and flags is a 32 bit flag mask that can be used by the extension to set private
  information on the token */
  void (*ExpandToken)(struct RSQueryExpanderCtx *ctx, const char *str, size_t len,
                      RSTokenFlags flags);

  /* SetPayload allows the query expander to set GLOBAL payload on the query (not unique per token)
   */
  void (*SetPayload)(struct RSQueryExpanderCtx *ctx, RSPayload payload);

} RSQueryExpanderCtx;

/* The signature for a query expander instance */
typedef void (*RSQueryTokenExpander)(RSQueryExpanderCtx *ctx, RSToken *token);
/* A free function called after the query expansion phase is over, to release per-query data */
typedef void (*RSFreeFunction)(void *);

/**************************************
 * Scoring Function API
 **************************************/

/* RS_OFFSETVECTOR_EOF is returned from an RSOffsetIterator when calling next and reaching the end.
 * When calling the iterator you should check for this return value */
#define RS_OFFSETVECTOR_EOF (uint32_t) - 1

/* RSOffsetVector represents the encoded offsets of a term in a document. You can read the offsets
 * by iterating over it with RSOffsetVector_Iterate */
typedef struct {
  char *data;
  size_t len;
} RSOffsetVector;

#ifndef __RS_OFFSET_VECTOR_H__
/* Forward declaration of the offset vector iterator, implemented internally */
typedef struct RSOffsetIterator RSOffsetIterator;
#endif

/* Iterate an offset vector. The iterator object is allocated on the heap and needs to be freed */
RSOffsetIterator *RSOffsetVector_Iterate(RSOffsetVector *v);

/* Get the next value in an offset vector and advance the iterator. If we've reached the end of the
 * vector, RS_OFFSETVECTOR_EOF is returned */
uint32_t RSOffsetIterator_Next(RSOffsetIterator *vi);

/* Free an iterator object after we are done reading it */
void RSOffsetIterator_Free(RSOffsetIterator *it);

/* Rewind an offset vector iterator and start reading it from the beginning. */
void RSOffsetIterator_Rewind(RSOffsetIterator *it);

/* A single term being evaluated in query time */
typedef struct {
  /* The term string, not necessarily NULL terminated, hence the length is given as well */
  char *str;
  /* The term length */
  size_t len;
  /* Inverse document frequency of the term in the index. See
   * https://en.wikipedia.org/wiki/Tf%E2%80%93idf */
  double idf;
  /* Flags given by the engine or by the query expander */
  RSTokenFlags flags;
} RSQueryTerm;

/* RSIndexRecord represents a single record of a document inside a term in the inverted index */
typedef struct {

  /* The internal document id, not the user given key. We use incremental ids internally */
  t_docId docId;
  /* The term that brought up this record */
  RSQueryTerm *term;
  /* The frequency of the term in the document, un-normalized */
  uint32_t freq;
  /* Field mask - each text field is applied an id which is a power of 2. Thus we can filter out
   * results per field using a mask */
  uint32_t fieldMask;
  /* The encoded offsets in which the term appeared in the document */
  RSOffsetVector offsets;

} RSIndexRecord;

/* RSIndexResult rerpresents the aggregate result of a few RSIndexRecord objects. In a single term
 * query it will have just one term, but in phrase and union queries it can have many records.
 * All members here are read-only and should NOT be changed by extensions */
typedef struct {
  /* The docuId of the result */
  t_docId docId;
  /* The final score as processed by the scoring function */
  double finalScore;
  /* the total frequency of all the records in this result */
  uint32_t totalTF;
  /* The aggregate field mask of all the records in this result */
  uint32_t fieldMask;
  /* The number of records */
  int numRecords;
  /* The capacity of the records array. Has no use for extensions */
  int recordsCap;
  /* An array of recods */
  RSIndexRecord *records;
} RSIndexResult;

/* The context given to a scoring function. It includes the payload set by the user or expander, the
 * private data set by the extensionm and callback functions */
typedef struct {
  /* Private data set by the extension on initialization time, or during scoring */
  void *privdata;
  /* Payload set by the client or by the query expander */
  RSPayload payload;
  /* The GetSlop() calback. Returns the cumulative "slop" or distance between the query terms, that
   * can be used to factor the result score */
  int (*GetSlop)(RSIndexResult *res);
} RSScoringFunctionCtx;

/* RSScoringFunction is a callback type for query custom scoring function modules */
typedef double (*RSScoringFunction)(RSScoringFunctionCtx *ctx, RSIndexResult *res,
                                    RSDocumentMetadata *dmd, double minScore);

/* The extension registeration context, containing the callbacks avaliable to the extension for
 * registering query expanders and scorers. */
typedef struct RSExtensionCtx {
  int (*RegisterScoringFunction)(const char *alias, RSScoringFunction func, RSFreeFunction ff,
                                 void *privdata);
  int (*RegisterQueryExpander)(const char *alias, RSQueryTokenExpander exp, RSFreeFunction ff,
                               void *privdata);
} RSExtensionCtx;

/* An extension initialization function  */
typedef int (*RSExtensionInitFunc)(RSExtensionCtx *ctx);
#endif