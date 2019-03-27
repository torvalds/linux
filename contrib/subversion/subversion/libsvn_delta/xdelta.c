/*
 * xdelta.c:  xdelta generator.
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


#include <assert.h>

#include <apr_general.h>        /* for APR_INLINE */
#include <apr_hash.h>

#include "svn_hash.h"
#include "svn_delta.h"
#include "private/svn_string_private.h"
#include "delta.h"

/* This is pseudo-adler32. It is adler32 without the prime modulus.
   The idea is borrowed from monotone, and is a translation of the C++
   code.  Graydon Hoare, the author of the original code, gave his
   explicit permission to use it under these terms at 8:02pm on
   Friday, February 11th, 2005.  */

/* Size of the blocks we compute checksums for. This was chosen out of
   thin air.  Monotone used 64, xdelta1 used 64, rsync uses 128.
   However, later optimizations assume it to be 256 or less.
 */
#define MATCH_BLOCKSIZE 64

/* Size of the checksum presence FLAGS array in BLOCKS_T.  With standard
   MATCH_BLOCKSIZE and SVN_DELTA_WINDOW_SIZE, 32k entries is about 20x
   the number of checksums that actually occur, i.e. we expect a >95%
   probability that non-matching checksums get already detected by checking
   against the FLAGS array.
   Must be a power of 2.
 */
#define FLAGS_COUNT (32 * 1024)

/* "no" / "invalid" / "unused" value for positions within the delta windows
 */
#define NO_POSITION ((apr_uint32_t)-1)

/* Feed C_IN into the adler32 checksum and remove C_OUT at the same time.
 * This function may (and will) only be called for characters that are
 * MATCH_BLOCKSIZE positions apart.
 *
 * Please note that the lower 16 bits cannot overflow in neither direction.
 * Therefore, we don't need to split the value into separate values for
 * sum(char) and sum(sum(char)).
 */
static APR_INLINE apr_uint32_t
adler32_replace(apr_uint32_t adler32, const char c_out, const char c_in)
{
  adler32 -= (MATCH_BLOCKSIZE * 0x10000u * ((unsigned char) c_out));

  adler32 -= (unsigned char)c_out;
  adler32 += (unsigned char)c_in;

  return adler32 + adler32 * 0x10000;
}

/* Calculate an pseudo-adler32 checksum for MATCH_BLOCKSIZE bytes starting
   at DATA.  Return the checksum value.  */

static APR_INLINE apr_uint32_t
init_adler32(const char *data)
{
  const unsigned char *input = (const unsigned char *)data;
  const unsigned char *last = input + MATCH_BLOCKSIZE;

  apr_uint32_t s1 = 0;
  apr_uint32_t s2 = 0;

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

/* Information for a block of the delta source.  The length of the
   block is the smaller of MATCH_BLOCKSIZE and the difference between
   the size of the source data and the position of this block. */
struct block
{
  apr_uint32_t adlersum;

/* Even in 64 bit systems, store only 32 bit offsets in our hash table
   (our delta window size much much smaller than 4GB).
   That reduces the hash table size by 50% from 32to 16KB
   and makes it easier to fit into the CPU's L1 cache. */
  apr_uint32_t pos;    /* NO_POSITION -> block is not used */
};

/* A hash table, using open addressing, of the blocks of the source. */
struct blocks
{
  /* The largest valid index of slots.
     This value has an upper bound proportionate to the text delta
     window size, so unless we dramatically increase the window size,
     it's safe to make this a 32-bit value.  In any case, it has to be
     hte same width as the block position index, (struct
     block).pos. */
  apr_uint32_t max;

  /* Source buffer that the positions in SLOTS refer to. */
  const char* data;

  /* Bit array indicating whether there may be a matching slot for a given
     adler32 checksum.  Since FLAGS has much more entries than SLOTS, this
     will indicate most cases of non-matching checksums with a "0" bit, i.e.
     as "known not to have a match".
     The mapping of adler32 checksum bits is [0..2][16..27] (LSB -> MSB),
     i.e. address the byte by the multiplicative part of adler32 and address
     the bits in that byte by the additive part of adler32. */
  char flags[FLAGS_COUNT / 8];

  /* The vector of blocks.  A pos value of NO_POSITION represents an unused
     slot. */
  struct block *slots;
};


/* Return a hash value calculated from the adler32 SUM, suitable for use with
   our hash table. */
static apr_uint32_t hash_func(apr_uint32_t sum)
{
  /* Since the adl32 checksum have a bad distribution for the 11th to 16th
     bits when used for our small block size, we add some bits from the
     other half of the checksum. */
  return sum ^ (sum >> 12);
}

/* Return the offset in BLOCKS.FLAGS for the adler32 SUM. */
static apr_uint32_t hash_flags(apr_uint32_t sum)
{
  /* The upper half of SUM has a wider value range than the lower 16 bit.
     Also, we want to a different folding than HASH_FUNC to minimize
     correlation between different hash levels. */
  return (sum >> 16) & ((FLAGS_COUNT / 8) - 1);
}

/* Insert a block with the checksum ADLERSUM at position POS in the source
   data into the table BLOCKS.  Ignore true duplicates, i.e. blocks with
   actually the same content. */
static void
add_block(struct blocks *blocks, apr_uint32_t adlersum, apr_uint32_t pos)
{
  apr_uint32_t h = hash_func(adlersum) & blocks->max;

  /* This will terminate, since we know that we will not fill the table. */
  for (; blocks->slots[h].pos != NO_POSITION; h = (h + 1) & blocks->max)
    if (blocks->slots[h].adlersum == adlersum)
      if (memcmp(blocks->data + blocks->slots[h].pos, blocks->data + pos,
                 MATCH_BLOCKSIZE) == 0)
        return;

  blocks->slots[h].adlersum = adlersum;
  blocks->slots[h].pos = pos;
  blocks->flags[hash_flags(adlersum)] |= 1 << (adlersum & 7);
}

/* Find a block in BLOCKS with the checksum ADLERSUM and matching the content
   at DATA, returning its position in the source data.  If there is no such
   block, return NO_POSITION. */
static apr_uint32_t
find_block(const struct blocks *blocks,
           apr_uint32_t adlersum,
           const char* data)
{
  apr_uint32_t h = hash_func(adlersum) & blocks->max;

  for (; blocks->slots[h].pos != NO_POSITION; h = (h + 1) & blocks->max)
    if (blocks->slots[h].adlersum == adlersum)
      if (memcmp(blocks->data + blocks->slots[h].pos, data,
                 MATCH_BLOCKSIZE) == 0)
        return blocks->slots[h].pos;

  return NO_POSITION;
}

/* Initialize the matches table from DATA of size DATALEN.  This goes
   through every block of MATCH_BLOCKSIZE bytes in the source and
   checksums it, inserting the result into the BLOCKS table.  */
static void
init_blocks_table(const char *data,
                  apr_size_t datalen,
                  struct blocks *blocks,
                  apr_pool_t *pool)
{
  apr_size_t nblocks;
  apr_size_t wnslots = 1;
  apr_uint32_t nslots;
  apr_uint32_t i;

  /* Be pessimistic about the block count. */
  nblocks = datalen / MATCH_BLOCKSIZE + 1;
  /* Find nearest larger power of two. */
  while (wnslots <= nblocks)
    wnslots *= 2;
  /* Double the number of slots to avoid a too high load. */
  wnslots *= 2;
  /* Narrow the number of slots to 32 bits, which is the size of the
     block position index in the hash table.
     Sanity check: On 64-bit platforms, apr_size_t is likely to be
     larger than apr_uint32_t. Make sure that the number of slots
     actually fits into blocks->max.  It's safe to use a hard assert
     here, because the largest possible value for nslots is
     proportional to the text delta window size and is therefore much
     smaller than the range of an apr_uint32_t.  If we ever happen to
     increase the window size too much, this assertion will get
     triggered by the test suite. */
  nslots = (apr_uint32_t) wnslots;
  SVN_ERR_ASSERT_NO_RETURN(wnslots == nslots);
  blocks->max = nslots - 1;
  blocks->data = data;
  blocks->slots = apr_palloc(pool, nslots * sizeof(*(blocks->slots)));
  for (i = 0; i < nslots; ++i)
    {
      /* Avoid using an indeterminate value in the lookup. */
      blocks->slots[i].adlersum = 0;
      blocks->slots[i].pos = NO_POSITION;
    }

  /* No checksum entries in SLOTS, yet => reset all checksum flags. */
  memset(blocks->flags, 0, sizeof(blocks->flags));

  /* If there is an odd block at the end of the buffer, we will
     not use that shorter block for deltification (only indirectly
     as an extension of some previous block). */
  for (i = 0; i + MATCH_BLOCKSIZE <= datalen; i += MATCH_BLOCKSIZE)
    add_block(blocks, init_adler32(data + i), i);
}

/* Try to find a match for the target data B in BLOCKS, and then
   extend the match as long as data in A and B at the match position
   continues to match.  We set the position in A we ended up in (in
   case we extended it backwards) in APOSP and update the corresponding
   position within B given in BPOSP. PENDING_INSERT_START sets the
   lower limit to BPOSP.
   Return number of matching bytes starting at ASOP.  Return 0 if
   no match has been found.
 */
static apr_size_t
find_match(const struct blocks *blocks,
           const apr_uint32_t rolling,
           const char *a,
           apr_size_t asize,
           const char *b,
           apr_size_t bsize,
           apr_size_t *bposp,
           apr_size_t *aposp,
           apr_size_t pending_insert_start)
{
  apr_size_t apos, bpos = *bposp;
  apr_size_t delta, max_delta;

  apos = find_block(blocks, rolling, b + bpos);

  /* See if we have a match.  */
  if (apos == NO_POSITION)
    return 0;

  /* Extend the match forward as far as possible */
  max_delta = asize - apos - MATCH_BLOCKSIZE < bsize - bpos - MATCH_BLOCKSIZE
            ? asize - apos - MATCH_BLOCKSIZE
            : bsize - bpos - MATCH_BLOCKSIZE;
  delta = svn_cstring__match_length(a + apos + MATCH_BLOCKSIZE,
                                    b + bpos + MATCH_BLOCKSIZE,
                                    max_delta);

  /* See if we can extend backwards (max MATCH_BLOCKSIZE-1 steps because A's
     content has been sampled only every MATCH_BLOCKSIZE positions).  */
  while (apos > 0 && bpos > pending_insert_start && a[apos-1] == b[bpos-1])
    {
      --apos;
      --bpos;
      ++delta;
    }

  *aposp = apos;
  *bposp = bpos;

  return MATCH_BLOCKSIZE + delta;
}

/* Utility for compute_delta() that compares the range B[START,BSIZE) with
 * the range of similar size before A[ASIZE]. Create corresponding copy and
 * insert operations.
 *
 * BUILD_BATON and POOL will be passed through from compute_delta().
 */
static void
store_delta_trailer(svn_txdelta__ops_baton_t *build_baton,
                    const char *a,
                    apr_size_t asize,
                    const char *b,
                    apr_size_t bsize,
                    apr_size_t start,
                    apr_pool_t *pool)
{
  apr_size_t end_match;
  apr_size_t max_len = asize > (bsize - start) ? bsize - start : asize;
  if (max_len == 0)
    return;

  end_match = svn_cstring__reverse_match_length(a + asize, b + bsize,
                                                max_len);
  if (end_match <= 4)
    end_match = 0;

  if (bsize - start > end_match)
    svn_txdelta__insert_op(build_baton, svn_txdelta_new,
                           start, bsize - start - end_match, b + start, pool);
  if (end_match)
    svn_txdelta__insert_op(build_baton, svn_txdelta_source,
                           asize - end_match, end_match, NULL, pool);
}


/* Compute a delta from A to B using xdelta.

   The basic xdelta algorithm is as follows:

   1. Go through the source data, checksumming every MATCH_BLOCKSIZE
      block of bytes using adler32, and inserting the checksum into a
      match table with the position of the match.
   2. Go through the target byte by byte, seeing if that byte starts a
      match that we have in the match table.
      2a. If so, try to extend the match as far as possible both
          forwards and backwards, and then insert a source copy
          operation into the delta ops builder for the match.
      2b. If not, insert the byte as new data using an insert delta op.

   Our implementation doesn't immediately insert "insert" operations,
   it waits until we have another copy, or we are done.  The reasoning
   is twofold:

   1. Otherwise, we would just be building a ton of 1 byte insert
      operations
   2. So that we can extend a source match backwards into a pending
     insert operation, and possibly remove the need for the insert
     entirely.  This can happen due to stream alignment.
*/
static void
compute_delta(svn_txdelta__ops_baton_t *build_baton,
              const char *a,
              apr_size_t asize,
              const char *b,
              apr_size_t bsize,
              apr_pool_t *pool)
{
  struct blocks blocks;
  apr_uint32_t rolling;
  apr_size_t lo = 0, pending_insert_start = 0, upper;

  /* Optimization: directly compare window starts. If more than 4
   * bytes match, we can immediately create a matching windows.
   * Shorter sequences result in a net data increase. */
  lo = svn_cstring__match_length(a, b, asize > bsize ? bsize : asize);
  if ((lo > 4) || (lo == bsize))
    {
      svn_txdelta__insert_op(build_baton, svn_txdelta_source,
                             0, lo, NULL, pool);
      pending_insert_start = lo;
    }
  else
    lo = 0;

  /* If the size of the target is smaller than the match blocksize, just
     insert the entire target.  */
  if ((bsize - lo < MATCH_BLOCKSIZE) || (asize < MATCH_BLOCKSIZE))
    {
      store_delta_trailer(build_baton, a, asize, b, bsize, lo, pool);
      return;
    }

  upper = bsize - MATCH_BLOCKSIZE; /* this is now known to be >= LO */

  /* Initialize the matches table.  */
  init_blocks_table(a, asize, &blocks, pool);

  /* Initialize our rolling checksum.  */
  rolling = init_adler32(b + lo);
  while (lo < upper)
    {
      apr_size_t matchlen;
      apr_size_t apos;

      /* Quickly skip positions whose respective ROLLING checksums
         definitely do not match any SLOT in BLOCKS. */
      while (!(blocks.flags[hash_flags(rolling)] & (1 << (rolling & 7)))
             && lo < upper)
        {
          rolling = adler32_replace(rolling, b[lo], b[lo+MATCH_BLOCKSIZE]);
          lo++;
        }

      /* LO is still <= UPPER, i.e. the following lookup is legal:
         Closely check whether we've got a match for the current location.
         Due to the above pre-filter, chances are that we find one. */
      matchlen = find_match(&blocks, rolling, a, asize, b, bsize,
                            &lo, &apos, pending_insert_start);

      /* If we didn't find a real match, insert the byte at the target
         position into the pending insert.  */
      if (matchlen == 0)
        {
          /* move block one position forward. Short blocks at the end of
             the buffer cannot be used as the beginning of a new match */
          if (lo + MATCH_BLOCKSIZE < bsize)
            rolling = adler32_replace(rolling, b[lo], b[lo+MATCH_BLOCKSIZE]);

          lo++;
        }
      else
        {
          /* store the sequence of B that is between the matches */
          if (lo - pending_insert_start > 0)
            svn_txdelta__insert_op(build_baton, svn_txdelta_new,
                                   0, lo - pending_insert_start,
                                   b + pending_insert_start, pool);
          else
            {
              /* the match borders on the previous op. Maybe, we found a
               * match that is better than / overlapping the previous one. */
              apr_size_t len = svn_cstring__reverse_match_length
                                 (a + apos, b + lo, apos < lo ? apos : lo);
              if (len > 0)
                {
                  len = svn_txdelta__remove_copy(build_baton, len);
                  apos -= len;
                  matchlen += len;
                  lo -= len;
                }
            }

          /* Reset the pending insert start to immediately after the
             match. */
          lo += matchlen;
          pending_insert_start = lo;
          svn_txdelta__insert_op(build_baton, svn_txdelta_source,
                                 apos, matchlen, NULL, pool);

          /* Calculate the Adler32 sum for the first block behind the match.
           * Ignore short buffers at the end of B.
           */
          if (lo + MATCH_BLOCKSIZE <= bsize)
            rolling = init_adler32(b + lo);
        }
    }

  /* If we still have an insert pending at the end, throw it in.  */
  store_delta_trailer(build_baton, a, asize, b, bsize, pending_insert_start, pool);
}

void
svn_txdelta__xdelta(svn_txdelta__ops_baton_t *build_baton,
                    const char *data,
                    apr_size_t source_len,
                    apr_size_t target_len,
                    apr_pool_t *pool)
{
  /*  We should never be asked to compute something when the source_len is 0;
      we just use a single insert op there (and rely on zlib for
      compression). */
  assert(source_len != 0);
  compute_delta(build_baton, data, source_len,
                data + source_len, target_len,
                pool);
}
