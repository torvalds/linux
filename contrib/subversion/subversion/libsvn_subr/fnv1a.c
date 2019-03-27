/*
 * fnv1a.c :  routines to create checksums derived from FNV-1a
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

#define APR_WANT_BYTEFUNC

#include <assert.h>
#include <apr.h>

#include "private/svn_subr_private.h"
#include "fnv1a.h"

/**
 * See http://www.isthe.com/chongo/tech/comp/fnv/ for more info on FNV-1
 */

/* FNV-1 32 bit constants taken from
 * http://www.isthe.com/chongo/tech/comp/fnv/
 */
#define FNV1_PRIME_32 0x01000193
#define FNV1_BASE_32 2166136261U

/* FNV-1a core implementation returning a 32 bit checksum over the first
 * LEN bytes in INPUT.  HASH is the checksum over preceding data (if any).
 */
static apr_uint32_t
fnv1a_32(apr_uint32_t hash, const void *input, apr_size_t len)
{
  const unsigned char *data = input;
  const unsigned char *end = data + len;

  for (; data != end; ++data)
    {
      hash ^= *data;
      hash *= FNV1_PRIME_32;
    }

  return hash;
}

/* Number of interleaved FVN-1a checksums we calculate for the modified
 * checksum algorithm.
 */
enum { SCALING = 4 };

/* FNV-1a core implementation updating 4 interleaved checksums in HASHES
 * over the first LEN bytes in INPUT.  This will only process multiples
 * of 4 and return the number of bytes processed.  LEN - ReturnValue < 4.
 */
static apr_size_t
fnv1a_32x4(apr_uint32_t hashes[SCALING], const void *input, apr_size_t len)
{
  /* calculate SCALING interleaved FNV-1a hashes while the input
     is large enough */
  const unsigned char *data = input;
  const unsigned char *end = data + len;
  for (; data + SCALING <= end; data += SCALING)
    {
      hashes[0] ^= data[0];
      hashes[0] *= FNV1_PRIME_32;
      hashes[1] ^= data[1];
      hashes[1] *= FNV1_PRIME_32;
      hashes[2] ^= data[2];
      hashes[2] *= FNV1_PRIME_32;
      hashes[3] ^= data[3];
      hashes[3] *= FNV1_PRIME_32;
    }

  return data - (const unsigned char *)input;
}

/* Combine interleaved HASHES plus LEN bytes from INPUT into a single
 * 32 bit hash value and return that.  LEN must be < 4.
 */
static apr_uint32_t
finalize_fnv1a_32x4(apr_uint32_t hashes[SCALING],
                    const void *input,
                    apr_size_t len)
{
  char final_data[sizeof(apr_uint32_t) * SCALING + SCALING - 1];
  apr_size_t i;
  assert(len < SCALING);

  for (i = 0; i < SCALING; ++i)
    hashes[i] = htonl(hashes[i]);

  /* run FNV-1a over the interleaved checksums plus the remaining
     (odd-lotted) input data */
  memcpy(final_data, hashes, sizeof(apr_uint32_t) * SCALING);
  if (len)
    memcpy(final_data + sizeof(apr_uint32_t) * SCALING, input, len);

  return fnv1a_32(FNV1_BASE_32,
                  final_data,
                  sizeof(apr_uint32_t) * SCALING + len);
}

apr_uint32_t
svn__fnv1a_32(const void *input, apr_size_t len)
{
  return fnv1a_32(FNV1_BASE_32, input, len);
}

apr_uint32_t
svn__fnv1a_32x4(const void *input, apr_size_t len)
{
  apr_uint32_t hashes[SCALING]
    = { FNV1_BASE_32, FNV1_BASE_32, FNV1_BASE_32, FNV1_BASE_32 };
  apr_size_t processed = fnv1a_32x4(hashes, input, len);

  return finalize_fnv1a_32x4(hashes,
                             (const char *)input + processed,
                             len - processed);
}

void
svn__fnv1a_32x4_raw(apr_uint32_t hashes[4],
                    const void *input,
                    apr_size_t len)
{
  apr_size_t processed;

  apr_size_t i;
  for (i = 0; i < SCALING; ++i)
    hashes[i] = FNV1_BASE_32;

  /* Process full 16 byte chunks. */
  processed = fnv1a_32x4(hashes, input, len);

  /* Fold the remainder (if any) into the first hash. */
  hashes[0] = fnv1a_32(hashes[0], (const char *)input + processed,
                       len - processed);
}

struct svn_fnv1a_32__context_t
{
  apr_uint32_t hash;
};

svn_fnv1a_32__context_t *
svn_fnv1a_32__context_create(apr_pool_t *pool)
{
  svn_fnv1a_32__context_t *context = apr_palloc(pool, sizeof(*context));
  context->hash = FNV1_BASE_32;

  return context;
}

void
svn_fnv1a_32__context_reset(svn_fnv1a_32__context_t *context)
{
  context->hash = FNV1_BASE_32;
}

void
svn_fnv1a_32__update(svn_fnv1a_32__context_t *context,
                     const void *data,
                     apr_size_t len)
{
  context->hash = fnv1a_32(context->hash, data, len);
}

apr_uint32_t
svn_fnv1a_32__finalize(svn_fnv1a_32__context_t *context)
{
  return context->hash;
}


struct svn_fnv1a_32x4__context_t
{
  apr_uint32_t hashes[SCALING];
  apr_size_t buffered;
  char buffer[SCALING];
};

svn_fnv1a_32x4__context_t *
svn_fnv1a_32x4__context_create(apr_pool_t *pool)
{
  svn_fnv1a_32x4__context_t *context = apr_palloc(pool, sizeof(*context));

  context->hashes[0] = FNV1_BASE_32;
  context->hashes[1] = FNV1_BASE_32;
  context->hashes[2] = FNV1_BASE_32;
  context->hashes[3] = FNV1_BASE_32;

  context->buffered = 0;

  return context;
}

void
svn_fnv1a_32x4__context_reset(svn_fnv1a_32x4__context_t *context)
{
  context->hashes[0] = FNV1_BASE_32;
  context->hashes[1] = FNV1_BASE_32;
  context->hashes[2] = FNV1_BASE_32;
  context->hashes[3] = FNV1_BASE_32;

  context->buffered = 0;
}

void
svn_fnv1a_32x4__update(svn_fnv1a_32x4__context_t *context,
                       const void *data,
                       apr_size_t len)
{
  apr_size_t processed;

  if (context->buffered)
    {
      apr_size_t to_copy = SCALING - context->buffered;
      if (to_copy > len)
        {
          memcpy(context->buffer + context->buffered, data, len);
          context->buffered += len;
          return;
        }

      memcpy(context->buffer + context->buffered, data, to_copy);
      data = (const char *)data + to_copy;
      len -= to_copy;

      fnv1a_32x4(context->hashes, context->buffer, SCALING);
      context->buffered = 0;
    }

  processed = fnv1a_32x4(context->hashes, data, len);
  if (processed != len)
    {
      context->buffered = len - processed;
      memcpy(context->buffer,
             (const char*)data + processed,
             len - processed);
    }
}

apr_uint32_t
svn_fnv1a_32x4__finalize(svn_fnv1a_32x4__context_t *context)
{
  return finalize_fnv1a_32x4(context->hashes,
                             context->buffer,
                             context->buffered);
}
