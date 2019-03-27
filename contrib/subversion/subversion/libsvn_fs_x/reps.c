/* reps.c --- FSX representation container
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include "reps.h"

#include "svn_sorts.h"
#include "private/svn_string_private.h"
#include "private/svn_packed_data.h"
#include "private/svn_temp_serializer.h"

#include "svn_private_config.h"

#include "cached_data.h"

/* Length of the text chunks we hash and match.  The algorithm will find
 * most matches with a length of 2 * MATCH_BLOCKSIZE and only specific
 * ones that are shorter than MATCH_BLOCKSIZE.
 *
 * This should be a power of two and must be a multiple of 8.
 * Good choices are 32, 64 and 128.
 */
#define MATCH_BLOCKSIZE 64

/* Limit the total text body within a container to 16MB.  Larger values
 * of up to 2GB are possible but become increasingly impractical as the
 * container has to be loaded in its entirety before any of it can be read.
 */
#define MAX_TEXT_BODY 0x1000000

/* Limit the size of the instructions stream.  This should not exceed the
 * text body size limit. */
#define MAX_INSTRUCTIONS (MAX_TEXT_BODY / 8)

/* value of unused hash buckets */
#define NO_OFFSET ((apr_uint32_t)(-1))

/* Byte strings are described by a series of copy instructions that each
 * do one of the following
 *
 * - copy a given number of bytes from the text corpus starting at a
 *   given offset
 * - reference other instruction and specify how many of instructions of
 *   that sequence shall be executed (i.e. a sub-sequence)
 * - copy a number of bytes from the base representation buffer starting
 *   at a given offset
 */

/* The contents of a fulltext / representation is defined by its first
 * instruction and the number of instructions to execute.
 */
typedef struct rep_t
{
  apr_uint32_t first_instruction;
  apr_uint32_t instruction_count;
} rep_t;

/* A single instruction.  The instruction type is being encoded in OFFSET.
 */
typedef struct instruction_t
{
  /* Instruction type and offset.
   * - offset < 0
   *   reference to instruction sub-sequence starting with
   *   container->instructions[-offset].
   * - 0 <= offset < container->base_text_len
   *   reference to the base text corpus;
   *   start copy at offset
   * - offset >= container->base_text_len
   *   reference to the text corpus;
   *   start copy at offset-container->base_text_len
   */
  apr_int32_t offset;

  /* Number of bytes to copy / instructions to execute
   */
  apr_uint32_t count;
} instruction_t;

/* Describe a base fulltext.
 */
typedef struct base_t
{
  /* Revision */
  svn_revnum_t revision;

  /* Item within that revision */
  apr_uint64_t item_index;

  /* Priority with which to use this base over others */
  int priority;

  /* Index into builder->representations that identifies the copy
   * instructions for this base. */
  apr_uint32_t rep;
} base_t;

/* Yet another hash data structure.  This one tries to be more cache
 * friendly by putting the first byte of each hashed sequence in a
 * common array.  This array will often fit into L1 or L2 at least and
 * give a 99% accurate test for a match without giving false negatives.
 */
typedef struct hash_t
{
  /* for used entries i, prefixes[i] == text[offsets[i]]; 0 otherwise.
   * This allows for a quick check without resolving the double
   * indirection. */
  char *prefixes;

  /* for used entries i, offsets[i] is start offset in the text corpus;
   * NO_OFFSET otherwise.
   */
  apr_uint32_t *offsets;

  /* to be used later for optimizations. */
  apr_uint32_t *last_matches;

  /* number of buckets in this hash, i.e. elements in each array above.
   * Must be 1 << (8 * sizeof(hash_key_t) - shift) */
  apr_size_t size;

  /* number of buckets actually in use. Must be <= size. */
  apr_size_t used;

  /* number of bits to shift right to map a hash_key_t to a bucket index */
  apr_size_t shift;

  /* pool to use when growing the hash */
  apr_pool_t *pool;
} hash_t;

/* Hash key type. 32 bits for pseudo-Adler32 hash sums.
 */
typedef apr_uint32_t hash_key_t;

/* Constructor data structure.
 */
struct svn_fs_x__reps_builder_t
{
  /* file system to read base representations from */
  svn_fs_t *fs;

  /* text corpus */
  svn_stringbuf_t *text;

  /* text block hash */
  hash_t hash;

  /* array of base_t objects describing all bases defined so far */
  apr_array_header_t *bases;

  /* array of rep_t objects describing all fulltexts (including bases)
   * added so far */
  apr_array_header_t *reps;

  /* array of instruction_t objects describing all instructions */
  apr_array_header_t *instructions;

  /* number of bytes in the text corpus that belongs to bases */
  apr_size_t base_text_len;
};

/* R/o container.
 */
struct svn_fs_x__reps_t
{
  /* text corpus */
  const char *text;

  /* length of the text corpus in bytes */
  apr_size_t text_len;

  /* bases used */
  const base_t *bases;

  /* number of bases used */
  apr_size_t base_count;

  /* fulltext i can be reconstructed by executing instructions
   * first_instructions[i] .. first_instructions[i+1]-1
   * (this array has one extra element at the end)
   */
  const apr_uint32_t *first_instructions;

  /* number of fulltexts (no bases) */
  apr_size_t rep_count;

  /* instructions */
  const instruction_t *instructions;

  /* total number of instructions */
  apr_size_t instruction_count;

  /* offsets > 0 but smaller that this are considered base references */
  apr_size_t base_text_len;
};

/* describe a section in the extractor's result string that is not filled
 * yet (but already exists).
 */
typedef struct missing_t
{
  /* start offset within the result string */
  apr_uint32_t start;

  /* number of bytes to write */
  apr_uint32_t count;

  /* index into extractor->bases selecting the base representation to
   * copy from */
  apr_uint32_t base;

  /* copy source offset within that base representation */
  apr_uint32_t offset;
} missing_t;

/* Fulltext extractor data structure.
 */
struct svn_fs_x__rep_extractor_t
{
  /* filesystem to read the bases from */
  svn_fs_t *fs;

  /* fulltext being constructed */
  svn_stringbuf_t *result;

  /* bases (base_t) yet to process (not used ATM) */
  apr_array_header_t *bases;

  /* missing sections (missing_t) in result->data that need to be filled,
   * yet */
  apr_array_header_t *missing;

  /* pool to use for allocating the above arrays */
  apr_pool_t *pool;
};

/* Given the ADLER32 checksum for a certain range of MATCH_BLOCKSIZE
 * bytes, return the checksum for the range excluding the first byte
 * C_OUT and appending C_IN.
 */
static hash_key_t
hash_key_replace(hash_key_t adler32, const char c_out, const char c_in)
{
  adler32 -= (MATCH_BLOCKSIZE * 0x10000u * ((unsigned char) c_out));

  adler32 -= (unsigned char)c_out;
  adler32 += (unsigned char)c_in;

  return adler32 + adler32 * 0x10000;
}

/* Calculate an pseudo-adler32 checksum for MATCH_BLOCKSIZE bytes starting
   at DATA.  Return the checksum value.  */
static hash_key_t
hash_key(const char *data)
{
  const unsigned char *input = (const unsigned char *)data;
  const unsigned char *last = input + MATCH_BLOCKSIZE;

  hash_key_t s1 = 0;
  hash_key_t s2 = 0;

  for (; input < last; input += 8)
    {
      s1 += input[0]; s2 += s1;
      s1 += input[1]; s2 += s1;
      s1 += input[2]; s2 += s1;
      s1 += input[3]; s2 += s1;
      s1 += input[4]; s2 += s1;
      s1 += input[5]; s2 += s1;
      s1 += input[6]; s2 += s1;
      s1 += input[7]; s2 += s1;
    }

  return s2 * 0x10000 + s1;
}

/* Map the ADLER32 key to a bucket index in HASH and return that index.
 */
static apr_size_t
hash_to_index(hash_t *hash, hash_key_t adler32)
{
  return (adler32 * 0xd1f3da69) >> hash->shift;
}

/* Allocate and initialized SIZE buckets in RESULT_POOL.
 * Assign them to HASH.
 */
static void
allocate_hash_members(hash_t *hash,
                      apr_size_t size,
                      apr_pool_t *result_pool)
{
  apr_size_t i;

  hash->pool = result_pool;
  hash->size = size;

  hash->prefixes = apr_pcalloc(result_pool, size);
  hash->last_matches = apr_pcalloc(result_pool,
                                   sizeof(*hash->last_matches) * size);
  hash->offsets = apr_palloc(result_pool, sizeof(*hash->offsets) * size);

  for (i = 0; i < size; ++i)
    hash->offsets[i] = NO_OFFSET;
}

/* Initialize the HASH data structure with 2**TWOPOWER buckets allocated
 * in RESULT_POOL.
 */
static void
init_hash(hash_t *hash,
          apr_size_t twoPower,
          apr_pool_t *result_pool)
{
  hash->used = 0;
  hash->shift = sizeof(hash_key_t) * 8 - twoPower;

  allocate_hash_members(hash, 1 << twoPower, result_pool);
}

/* Make HASH have at least MIN_SIZE buckets but at least double the number
 * of buckets in HASH by rehashing it based TEXT.
 */
static void
grow_hash(hash_t *hash,
          svn_stringbuf_t *text,
          apr_size_t min_size)
{
  hash_t copy;
  apr_size_t i;

  /* determine the new hash size */
  apr_size_t new_size = hash->size * 2;
  apr_size_t new_shift = hash->shift - 1;
  while (new_size < min_size)
    {
      new_size *= 2;
      --new_shift;
    }

  /* allocate new hash */
  allocate_hash_members(&copy, new_size, hash->pool);
  copy.used = 0;
  copy.shift = new_shift;

  /* copy / translate data */
  for (i = 0; i < hash->size; ++i)
    {
      apr_uint32_t offset = hash->offsets[i];
      if (offset != NO_OFFSET)
        {
          hash_key_t key = hash_key(text->data + offset);
          size_t idx = hash_to_index(&copy, key);

          if (copy.offsets[idx] == NO_OFFSET)
            copy.used++;

          copy.prefixes[idx] = hash->prefixes[i];
          copy.offsets[idx] = offset;
          copy.last_matches[idx] = hash->last_matches[i];
        }
    }

  *hash = copy;
}

svn_fs_x__reps_builder_t *
svn_fs_x__reps_builder_create(svn_fs_t *fs,
                              apr_pool_t *result_pool)
{
  svn_fs_x__reps_builder_t *result = apr_pcalloc(result_pool,
                                                 sizeof(*result));

  result->fs = fs;
  result->text = svn_stringbuf_create_empty(result_pool);
  init_hash(&result->hash, 4, result_pool);

  result->bases = apr_array_make(result_pool, 0, sizeof(base_t));
  result->reps = apr_array_make(result_pool, 0, sizeof(rep_t));
  result->instructions = apr_array_make(result_pool, 0,
                                        sizeof(instruction_t));

  return result;
}

svn_error_t *
svn_fs_x__reps_add_base(svn_fs_x__reps_builder_t *builder,
                        svn_fs_x__representation_t *rep,
                        int priority,
                        apr_pool_t *scratch_pool)
{
  base_t base;
  apr_size_t text_start_offset = builder->text->len;

  svn_stream_t *stream;
  svn_string_t *contents;
  apr_size_t idx;
  SVN_ERR(svn_fs_x__get_contents(&stream, builder->fs, rep, FALSE,
                                 scratch_pool));
  SVN_ERR(svn_string_from_stream2(&contents, stream, SVN__STREAM_CHUNK_SIZE,
                                  scratch_pool));
  SVN_ERR(svn_fs_x__reps_add(&idx, builder, contents));

  base.revision = svn_fs_x__get_revnum(rep->id.change_set);
  base.item_index = rep->id.number;
  base.priority = priority;
  base.rep = (apr_uint32_t)idx;

  APR_ARRAY_PUSH(builder->bases, base_t) = base;
  builder->base_text_len += builder->text->len - text_start_offset;

  return SVN_NO_ERROR;
}

/* Add LEN bytes from DATA to BUILDER's text corpus. Also, add a copy
 * operation for that text fragment.
 */
static void
add_new_text(svn_fs_x__reps_builder_t *builder,
             const char *data,
             apr_size_t len)
{
  instruction_t instruction;
  apr_size_t offset;
  apr_size_t buckets_required;

  if (len == 0)
    return;

  /* new instruction */
  instruction.offset = (apr_int32_t)builder->text->len;
  instruction.count = (apr_uint32_t)len;
  APR_ARRAY_PUSH(builder->instructions, instruction_t) = instruction;

  /* add to text corpus */
  svn_stringbuf_appendbytes(builder->text, data, len);

  /* expand the hash upfront to minimize the chances of collisions */
  buckets_required = builder->hash.used + len / MATCH_BLOCKSIZE;
  if (buckets_required * 3 >= builder->hash.size * 2)
    grow_hash(&builder->hash, builder->text, 2 * buckets_required);

  /* add hash entries for the new sequence */
  for (offset = instruction.offset;
       offset + MATCH_BLOCKSIZE <= builder->text->len;
       offset += MATCH_BLOCKSIZE)
    {
      hash_key_t key = hash_key(builder->text->data + offset);
      size_t idx = hash_to_index(&builder->hash, key);

      /* Don't replace hash entries that stem from the current text.
       * This makes early matches more likely. */
      if (builder->hash.offsets[idx] == NO_OFFSET)
        ++builder->hash.used;
      else if (builder->hash.offsets[idx] >= instruction.offset)
        continue;

      builder->hash.offsets[idx] = (apr_uint32_t)offset;
      builder->hash.prefixes[idx] = builder->text->data[offset];
    }
}

svn_error_t *
svn_fs_x__reps_add(apr_size_t *rep_idx,
                   svn_fs_x__reps_builder_t *builder,
                   const svn_string_t *contents)
{
  rep_t rep;
  const char *current = contents->data;
  const char *processed = current;
  const char *end = current + contents->len;
  const char *last_to_test = end - MATCH_BLOCKSIZE - 1;

  if (builder->text->len + contents->len > MAX_TEXT_BODY)
    return svn_error_create(SVN_ERR_FS_CONTAINER_SIZE, NULL,
                      _("Text body exceeds star delta container capacity"));

  if (  builder->instructions->nelts + 2 * contents->len / MATCH_BLOCKSIZE
      > MAX_INSTRUCTIONS)
    return svn_error_create(SVN_ERR_FS_CONTAINER_SIZE, NULL,
              _("Instruction count exceeds star delta container capacity"));

  rep.first_instruction = (apr_uint32_t)builder->instructions->nelts;
  while (current < last_to_test)
    {
      hash_key_t key = hash_key(current);
      size_t offset;
      size_t idx;

      /* search for the next matching sequence */

      for (; current < last_to_test; ++current)
        {
          idx = hash_to_index(&builder->hash, key);
          if (builder->hash.prefixes[idx] == current[0])
            {
              offset = builder->hash.offsets[idx];
              if (   (offset != NO_OFFSET)
                  && (memcmp(&builder->text->data[offset], current,
                             MATCH_BLOCKSIZE) == 0))
                break;
            }
          key = hash_key_replace(key, current[0], current[MATCH_BLOCKSIZE]);
        }

      /* found it? */

      if (current < last_to_test)
        {
          instruction_t instruction;

          /* extend the match */

          size_t prefix_match
            = svn_cstring__reverse_match_length(current,
                                                builder->text->data + offset,
                                                MIN(offset, current - processed));
          size_t postfix_match
            = svn_cstring__match_length(current + MATCH_BLOCKSIZE,
                           builder->text->data + offset + MATCH_BLOCKSIZE,
                           MIN(builder->text->len - offset - MATCH_BLOCKSIZE,
                               end - current - MATCH_BLOCKSIZE));

          /* non-matched section */

          size_t new_copy = (current - processed) - prefix_match;
          if (new_copy)
            add_new_text(builder, processed, new_copy);

          /* add instruction for matching section */

          instruction.offset = (apr_int32_t)(offset - prefix_match);
          instruction.count = (apr_uint32_t)(prefix_match + postfix_match +
                                             MATCH_BLOCKSIZE);
          APR_ARRAY_PUSH(builder->instructions, instruction_t) = instruction;

          processed = current + MATCH_BLOCKSIZE + postfix_match;
          current = processed;
        }
    }

  add_new_text(builder, processed, end - processed);
  rep.instruction_count = (apr_uint32_t)builder->instructions->nelts
                        - rep.first_instruction;
  APR_ARRAY_PUSH(builder->reps, rep_t) = rep;

  *rep_idx = (apr_size_t)(builder->reps->nelts - 1);
  return SVN_NO_ERROR;
}

apr_size_t
svn_fs_x__reps_estimate_size(const svn_fs_x__reps_builder_t *builder)
{
  /* approx: size of the text exclusive to us @ 50% compression rate
   *       + 2 bytes per instruction
   *       + 2 bytes per representation
   *       + 8 bytes per base representation
   *       + 1:8 inefficiency in using the base representations
   *       + 100 bytes static overhead
   */
  return (builder->text->len - builder->base_text_len) / 2
       + builder->instructions->nelts * 2
       + builder->reps->nelts * 2
       + builder->bases->nelts * 8
       + builder->base_text_len / 8
       + 100;
}

/* Execute COUNT instructions starting at INSTRUCTION_IDX in CONTAINER
 * and fill the parts of EXTRACTOR->RESULT that we can from this container.
 * Record the remainder in EXTRACTOR->MISSING.
 *
 * This function will recurse for instructions that reference other
 * instruction sequences. COUNT refers to the top-level instructions only.
 */
static void
get_text(svn_fs_x__rep_extractor_t *extractor,
         const svn_fs_x__reps_t *container,
         apr_size_t instruction_idx,
         apr_size_t count)
{
  const instruction_t *instruction;
  const char *offset_0 = container->text - container->base_text_len;

  for (instruction = container->instructions + instruction_idx;
       instruction < container->instructions + instruction_idx + count;
       instruction++)
    if (instruction->offset < 0)
      {
        /* instruction sub-sequence */
        get_text(extractor, container, -instruction->offset,
                 instruction->count);
      }
    else if (instruction->offset >= container->base_text_len)
      {
        /* direct copy instruction */
        svn_stringbuf_appendbytes(extractor->result,
                                  offset_0 + instruction->offset,
                                  instruction->count);
      }
    else
      {
        /* a section that we need to fill from some external base rep. */
        missing_t missing;
        missing.base = 0;
        missing.start = (apr_uint32_t)extractor->result->len;
        missing.count = instruction->count;
        missing.offset = instruction->offset;
        svn_stringbuf_appendfill(extractor->result, 0, instruction->count);

        if (extractor->missing == NULL)
          extractor->missing = apr_array_make(extractor->pool, 1,
                                              sizeof(missing));

        APR_ARRAY_PUSH(extractor->missing, missing_t) = missing;
      }
}

svn_error_t *
svn_fs_x__reps_get(svn_fs_x__rep_extractor_t **extractor,
                   svn_fs_t *fs,
                   const svn_fs_x__reps_t *container,
                   apr_size_t idx,
                   apr_pool_t *result_pool)
{
  apr_uint32_t first = container->first_instructions[idx];
  apr_uint32_t last = container->first_instructions[idx + 1];

  /* create the extractor object */
  svn_fs_x__rep_extractor_t *result = apr_pcalloc(result_pool,
                                                  sizeof(*result));
  result->fs = fs;
  result->result = svn_stringbuf_create_empty(result_pool);
  result->pool = result_pool;

  /* fill all the bits of the result that we can, i.e. all but bits coming
   * from base representations */
  get_text(result, container, first, last - first);
  *extractor = result;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__extractor_drive(svn_stringbuf_t **contents,
                          svn_fs_x__rep_extractor_t *extractor,
                          apr_size_t start_offset,
                          apr_size_t size,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  /* we don't support base reps right now */
  SVN_ERR_ASSERT(extractor->missing == NULL);

  if (size == 0)
    {
      *contents = svn_stringbuf_dup(extractor->result, result_pool);
    }
  else
    {
      /* clip the selected range */
      if (start_offset > extractor->result->len)
        start_offset = extractor->result->len;

      if (size > extractor->result->len - start_offset)
        size = extractor->result->len - start_offset;

      *contents = svn_stringbuf_ncreate(extractor->result->data + start_offset,
                                        size, result_pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__write_reps_container(svn_stream_t *stream,
                               const svn_fs_x__reps_builder_t *builder,
                               apr_pool_t *scratch_pool)
{
  int i;
  svn_packed__data_root_t *root = svn_packed__data_create_root(scratch_pool);

  /* one top-level stream for each array */
  svn_packed__int_stream_t *bases_stream
    = svn_packed__create_int_stream(root, FALSE, FALSE);
  svn_packed__int_stream_t *reps_stream
    = svn_packed__create_int_stream(root, TRUE, FALSE);
  svn_packed__int_stream_t *instructions_stream
    = svn_packed__create_int_stream(root, FALSE, FALSE);

  /* for misc stuff */
  svn_packed__int_stream_t *misc_stream
    = svn_packed__create_int_stream(root, FALSE, FALSE);

  /* TEXT will be just a single string */
  svn_packed__byte_stream_t *text_stream
    = svn_packed__create_bytes_stream(root);

  /* structure the struct streams such we can extract much of the redundancy
   */
  svn_packed__create_int_substream(bases_stream, TRUE, TRUE);
  svn_packed__create_int_substream(bases_stream, TRUE, FALSE);
  svn_packed__create_int_substream(bases_stream, TRUE, FALSE);
  svn_packed__create_int_substream(bases_stream, TRUE, FALSE);

  svn_packed__create_int_substream(instructions_stream, TRUE, TRUE);
  svn_packed__create_int_substream(instructions_stream, FALSE, FALSE);

  /* text */
  svn_packed__add_bytes(text_stream, builder->text->data, builder->text->len);

  /* serialize bases */
  for (i = 0; i < builder->bases->nelts; ++i)
    {
      const base_t *base = &APR_ARRAY_IDX(builder->bases, i, base_t);
      svn_packed__add_int(bases_stream, base->revision);
      svn_packed__add_uint(bases_stream, base->item_index);
      svn_packed__add_uint(bases_stream, base->priority);
      svn_packed__add_uint(bases_stream, base->rep);
    }

  /* serialize reps */
  for (i = 0; i < builder->reps->nelts; ++i)
    {
      const rep_t *rep = &APR_ARRAY_IDX(builder->reps, i, rep_t);
      svn_packed__add_uint(reps_stream, rep->first_instruction);
    }

  svn_packed__add_uint(reps_stream, builder->instructions->nelts);

  /* serialize instructions */
  for (i = 0; i < builder->instructions->nelts; ++i)
    {
      const instruction_t *instruction
        = &APR_ARRAY_IDX(builder->instructions, i, instruction_t);
      svn_packed__add_int(instructions_stream, instruction->offset);
      svn_packed__add_uint(instructions_stream, instruction->count);
    }

  /* other elements */
  svn_packed__add_uint(misc_stream, 0);

  /* write to stream */
  SVN_ERR(svn_packed__data_write(stream, root, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__read_reps_container(svn_fs_x__reps_t **container,
                              svn_stream_t *stream,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  apr_size_t i;

  base_t *bases;
  apr_uint32_t *first_instructions;
  instruction_t *instructions;

  svn_fs_x__reps_t *reps = apr_pcalloc(result_pool, sizeof(*reps));

  svn_packed__data_root_t *root;
  svn_packed__int_stream_t *bases_stream;
  svn_packed__int_stream_t *reps_stream;
  svn_packed__int_stream_t *instructions_stream;
  svn_packed__int_stream_t *misc_stream;
  svn_packed__byte_stream_t *text_stream;

  /* read from disk */
  SVN_ERR(svn_packed__data_read(&root, stream, result_pool, scratch_pool));

  bases_stream = svn_packed__first_int_stream(root);
  reps_stream = svn_packed__next_int_stream(bases_stream);
  instructions_stream = svn_packed__next_int_stream(reps_stream);
  misc_stream = svn_packed__next_int_stream(instructions_stream);
  text_stream = svn_packed__first_byte_stream(root);

  /* text */
  reps->text = svn_packed__get_bytes(text_stream, &reps->text_len);
  reps->text = apr_pmemdup(result_pool, reps->text, reps->text_len);

  /* de-serialize  bases */
  reps->base_count
    = svn_packed__int_count(svn_packed__first_int_substream(bases_stream));
  bases = apr_palloc(result_pool, reps->base_count * sizeof(*bases));
  reps->bases = bases;

  for (i = 0; i < reps->base_count; ++i)
    {
      base_t *base = bases + i;
      base->revision = (svn_revnum_t)svn_packed__get_int(bases_stream);
      base->item_index = svn_packed__get_uint(bases_stream);
      base->priority = (int)svn_packed__get_uint(bases_stream);
      base->rep = (apr_uint32_t)svn_packed__get_uint(bases_stream);
    }

  /* de-serialize instructions */
  reps->instruction_count
    = svn_packed__int_count
         (svn_packed__first_int_substream(instructions_stream));
  instructions
    = apr_palloc(result_pool,
                 reps->instruction_count * sizeof(*instructions));
  reps->instructions = instructions;

  for (i = 0; i < reps->instruction_count; ++i)
    {
      instruction_t *instruction = instructions + i;
      instruction->offset
        = (apr_int32_t)svn_packed__get_int(instructions_stream);
      instruction->count
        = (apr_uint32_t)svn_packed__get_uint(instructions_stream);
    }

  /* de-serialize reps */
  reps->rep_count = svn_packed__int_count(reps_stream);
  first_instructions
    = apr_palloc(result_pool,
                 (reps->rep_count + 1) * sizeof(*first_instructions));
  reps->first_instructions = first_instructions;

  for (i = 0; i < reps->rep_count; ++i)
    first_instructions[i]
      = (apr_uint32_t)svn_packed__get_uint(reps_stream);
  first_instructions[reps->rep_count] = (apr_uint32_t)reps->instruction_count;

  /* other elements */
  reps->base_text_len = (apr_size_t)svn_packed__get_uint(misc_stream);

  /* return result */
  *container = reps;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__serialize_reps_container(void **data,
                                   apr_size_t *data_len,
                                   void *in,
                                   apr_pool_t *pool)
{
  svn_fs_x__reps_t *reps = in;
  svn_stringbuf_t *serialized;

  /* make a guesstimate on the size of the serialized data.  Erring on the
   * low side will cause the serializer to re-alloc its buffer. */
  apr_size_t size
    = reps->text_len
    + reps->base_count * sizeof(*reps->bases)
    + reps->rep_count * sizeof(*reps->first_instructions)
    + reps->instruction_count * sizeof(*reps->instructions)
    + 100;

  /* serialize array header and all its elements */
  svn_temp_serializer__context_t *context
    = svn_temp_serializer__init(reps, sizeof(*reps), size, pool);

  /* serialize sub-structures */
  svn_temp_serializer__add_leaf(context, (const void **)&reps->text,
                                reps->text_len);
  svn_temp_serializer__add_leaf(context, (const void **)&reps->bases,
                                reps->base_count * sizeof(*reps->bases));
  svn_temp_serializer__add_leaf(context,
                                (const void **)&reps->first_instructions,
                                reps->rep_count *
                                    sizeof(*reps->first_instructions));
  svn_temp_serializer__add_leaf(context, (const void **)&reps->instructions,
                                reps->instruction_count *
                                    sizeof(*reps->instructions));

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *data = serialized->data;
  *data_len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__deserialize_reps_container(void **out,
                                     void *data,
                                     apr_size_t data_len,
                                     apr_pool_t *result_pool)
{
  svn_fs_x__reps_t *reps = (svn_fs_x__reps_t *)data;

  /* de-serialize sub-structures */
  svn_temp_deserializer__resolve(reps, (void **)&reps->text);
  svn_temp_deserializer__resolve(reps, (void **)&reps->bases);
  svn_temp_deserializer__resolve(reps, (void **)&reps->first_instructions);
  svn_temp_deserializer__resolve(reps, (void **)&reps->instructions);

  /* done */
  *out = reps;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__reps_get_func(void **out,
                        const void *data,
                        apr_size_t data_len,
                        void *baton,
                        apr_pool_t *pool)
{
  svn_fs_x__reps_baton_t *reps_baton = baton;

  /* get a usable reps structure  */
  const svn_fs_x__reps_t *cached = data;
  svn_fs_x__reps_t *reps = apr_pmemdup(pool, cached, sizeof(*reps));

  reps->text
    = svn_temp_deserializer__ptr(cached, (const void **)&cached->text);
  reps->bases
    = svn_temp_deserializer__ptr(cached, (const void **)&cached->bases);
  reps->first_instructions
    = svn_temp_deserializer__ptr(cached,
                                 (const void **)&cached->first_instructions);
  reps->instructions
    = svn_temp_deserializer__ptr(cached,
                                 (const void **)&cached->instructions);

  /* return an extractor for the selected item */
  SVN_ERR(svn_fs_x__reps_get((svn_fs_x__rep_extractor_t **)out,
                             reps_baton->fs, reps, reps_baton->idx, pool));

  return SVN_NO_ERROR;
}
