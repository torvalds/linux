/*
 * bit_array.c :  implement a simple packed bit array
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


#include "svn_sorts.h"
#include "private/svn_subr_private.h"

/* We allocate our data buffer in blocks of this size (in bytes).
 * For performance reasons, this shall be a power of two.
 * It should also not exceed 80kB (to prevent APR pool fragmentation) and
 * not be too small (to keep the number of OS-side memory allocations low -
 * avoiding hitting system-specific limits).
 */
#define BLOCK_SIZE          0x10000

/* Number of bits in each block.
 */
#define BLOCK_SIZE_BITS     (8 * BLOCK_SIZE)

/* Initial array size (covers INITIAL_BLOCK_COUNT * BLOCK_SIZE_BITS bits).
 * For performance reasons, this shall be a power of two.
 */
#define INITIAL_BLOCK_COUNT 16

/* We store the bits in a lazily allocated two-dimensional array.
 * For every BLOCK_SIZE_BITS range of indexes, there is one entry in the
 * BLOCKS array.  If index / BLOCK_SIZE_BITS exceeds BLOCK_COUNT-1, the
 * blocks are implicitly empty.  Only if a bit will be set to 1, will the
 * BLOCKS array be auto-expanded.
 *
 * As long as no bit got set in a particular block, the respective entry in
 * BLOCKS entry will be NULL, implying that all block contents is 0.
 */
struct svn_bit_array__t
{
  /* Data buffer of BLOCK_COUNT blocks, BLOCK_SIZE_BITS each.  Never NULL.
   * Every block may be NULL, though. */
  unsigned char **blocks;

  /* Number of bytes allocated to DATA.  Never shrinks. */
  apr_size_t block_count;

  /* Reallocate DATA form this POOL when growing. */
  apr_pool_t *pool;
};

/* Given that MAX shall be an actual bit index in a packed bit array,
 * return the number of blocks entries to allocate for the data buffer. */
static apr_size_t
select_data_size(apr_size_t max)
{
  /* We allocate a power of two of bytes but at least 16 blocks. */
  apr_size_t size = INITIAL_BLOCK_COUNT;

  /* Caution:
   * MAX / BLOCK_SIZE_BITS == SIZE still means that MAX is out of bounds.
   * OTOH, 2 * (MAX/BLOCK_SIZE_BITS) is always within the value range of
   * APR_SIZE_T. */
  while (size <= max / BLOCK_SIZE_BITS)
    size *= 2;

  return size;
}

svn_bit_array__t *
svn_bit_array__create(apr_size_t max,
                      apr_pool_t *pool)
{
  svn_bit_array__t *array = apr_pcalloc(pool, sizeof(*array));

  array->block_count = select_data_size(max);
  array->pool = pool;
  array->blocks = apr_pcalloc(pool,
                              array->block_count * sizeof(*array->blocks));

  return array;
}

void
svn_bit_array__set(svn_bit_array__t *array,
                   apr_size_t idx,
                   svn_boolean_t value)
{
  unsigned char *block;

  /* Index within ARRAY->BLOCKS for the block containing bit IDX. */
  apr_size_t block_idx = idx / BLOCK_SIZE_BITS;

  /* Within that block, index of the byte containing IDX. */
  apr_size_t byte_idx = (idx % BLOCK_SIZE_BITS) / 8;

  /* Within that byte, index of the bit corresponding to IDX. */
  apr_size_t bit_idx = (idx % BLOCK_SIZE_BITS) % 8;

  /* If IDX is outside the allocated range, we _may_ have to grow it.
   *
   * Be sure to use division instead of multiplication as we need to cover
   * the full value range of APR_SIZE_T for the bit indexes.
   */
  if (block_idx >= array->block_count)
    {
      apr_size_t new_count;
      unsigned char **new_blocks;

      /* Unallocated indexes are implicitly 0, so no actual allocation
       * required in that case.
       */
      if (!value)
        return;

      /* Grow block list to cover IDX.
       * Clear the new entries to guarantee our array[idx]==0 default.
       */
      new_count = select_data_size(idx);
      new_blocks = apr_pcalloc(array->pool, new_count * sizeof(*new_blocks));
      memcpy(new_blocks, array->blocks,
             array->block_count * sizeof(*new_blocks));
      array->blocks = new_blocks;
      array->block_count = new_count;
    }

  /* IDX is covered by ARRAY->BLOCKS now. */

  /* Get the block that contains IDX.  Auto-allocate it if missing. */
  block = array->blocks[block_idx];
  if (block == NULL)
    {
      /* Unallocated indexes are implicitly 0, so no actual allocation
       * required in that case.
       */
      if (!value)
        return;

      /* Allocate the previously missing block and clear it for our
       * array[idx] == 0 default. */
      block = apr_pcalloc(array->pool, BLOCK_SIZE);
      array->blocks[block_idx] = block;
    }

  /* Set / reset one bit.  Be sure to use unsigned shifts. */
  if (value)
    block[byte_idx] |=  (unsigned char)(1u << bit_idx);
  else
    block[byte_idx] &= ~(unsigned char)(1u << bit_idx);
}

svn_boolean_t
svn_bit_array__get(svn_bit_array__t *array,
                   apr_size_t idx)
{
  unsigned char *block;

  /* Index within ARRAY->BLOCKS for the block containing bit IDX. */
  apr_size_t block_idx = idx / BLOCK_SIZE_BITS;

  /* Within that block, index of the byte containing IDX. */
  apr_size_t byte_idx = (idx % BLOCK_SIZE_BITS) / 8;

  /* Within that byte, index of the bit corresponding to IDX. */
  apr_size_t bit_idx = (idx % BLOCK_SIZE_BITS) % 8;

  /* Indexes outside the allocated range are implicitly 0. */
  if (block_idx >= array->block_count)
    return 0;

  /* Same if the respective block has not been allocated. */
  block = array->blocks[block_idx];
  if (block == NULL)
    return 0;

  /* Extract one bit (get the byte, shift bit to LSB, extract it). */
  return (block[byte_idx] >> bit_idx) & 1;
}

