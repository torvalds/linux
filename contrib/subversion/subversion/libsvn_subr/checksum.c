/*
 * checksum.c:   checksum routines
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

#include <ctype.h>

#include <apr_md5.h>
#include <apr_sha1.h>

#include "svn_checksum.h"
#include "svn_error.h"
#include "svn_ctype.h"
#include "svn_sorts.h"

#include "checksum.h"
#include "fnv1a.h"

#include "private/svn_subr_private.h"

#include "svn_private_config.h"



/* The MD5 digest for the empty string. */
static const unsigned char md5_empty_string_digest_array[] = {
  0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
  0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e
};

/* The SHA1 digest for the empty string. */
static const unsigned char sha1_empty_string_digest_array[] = {
  0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
  0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09
};

/* The FNV-1a digest for the empty string. */
static const unsigned char fnv1a_32_empty_string_digest_array[] = {
  0x81, 0x1c, 0x9d, 0xc5
};

/* The FNV-1a digest for the empty string. */
static const unsigned char fnv1a_32x4_empty_string_digest_array[] = {
  0xcd, 0x6d, 0x9a, 0x85
};

/* Digests for an empty string, indexed by checksum type */
static const unsigned char * empty_string_digests[] = {
  md5_empty_string_digest_array,
  sha1_empty_string_digest_array,
  fnv1a_32_empty_string_digest_array,
  fnv1a_32x4_empty_string_digest_array
};

/* Digest sizes in bytes, indexed by checksum type */
static const apr_size_t digest_sizes[] = {
  APR_MD5_DIGESTSIZE,
  APR_SHA1_DIGESTSIZE,
  sizeof(apr_uint32_t),
  sizeof(apr_uint32_t)
};

/* Checksum type prefixes used in serialized checksums. */
static const char *ckind_str[] = {
  "$md5 $",
  "$sha1$",
  "$fnv1$",
  "$fnvm$",
  /* ### svn_checksum_deserialize() assumes all these have the same strlen() */
};

/* Returns the digest size of it's argument. */
#define DIGESTSIZE(k) \
  (((k) < svn_checksum_md5 || (k) > svn_checksum_fnv1a_32x4) ? 0 : digest_sizes[k])

/* Largest supported digest size */
#define MAX_DIGESTSIZE (MAX(APR_MD5_DIGESTSIZE,APR_SHA1_DIGESTSIZE))

const unsigned char *
svn__empty_string_digest(svn_checksum_kind_t kind)
{
  return empty_string_digests[kind];
}

const char *
svn__digest_to_cstring_display(const unsigned char digest[],
                               apr_size_t digest_size,
                               apr_pool_t *pool)
{
  static const char *hex = "0123456789abcdef";
  char *str = apr_palloc(pool, (digest_size * 2) + 1);
  apr_size_t i;

  for (i = 0; i < digest_size; i++)
    {
      str[i*2]   = hex[digest[i] >> 4];
      str[i*2+1] = hex[digest[i] & 0x0f];
    }
  str[i*2] = '\0';

  return str;
}


const char *
svn__digest_to_cstring(const unsigned char digest[],
                       apr_size_t digest_size,
                       apr_pool_t *pool)
{
  static const unsigned char zeros_digest[MAX_DIGESTSIZE] = { 0 };

  if (memcmp(digest, zeros_digest, digest_size) != 0)
    return svn__digest_to_cstring_display(digest, digest_size, pool);
  else
    return NULL;
}


svn_boolean_t
svn__digests_match(const unsigned char d1[],
                   const unsigned char d2[],
                   apr_size_t digest_size)
{
  static const unsigned char zeros[MAX_DIGESTSIZE] = { 0 };

  return ((memcmp(d1, d2, digest_size) == 0)
          || (memcmp(d2, zeros, digest_size) == 0)
          || (memcmp(d1, zeros, digest_size) == 0));
}

/* Check to see if KIND is something we recognize.  If not, return
 * SVN_ERR_BAD_CHECKSUM_KIND */
static svn_error_t *
validate_kind(svn_checksum_kind_t kind)
{
  if (kind >= svn_checksum_md5 && kind <= svn_checksum_fnv1a_32x4)
    return SVN_NO_ERROR;
  else
    return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);
}

/* Create a svn_checksum_t with everything but the contents of the
   digest populated. */
static svn_checksum_t *
checksum_create_without_digest(svn_checksum_kind_t kind,
                               apr_size_t digest_size,
                               apr_pool_t *pool)
{
  /* Use apr_palloc() instead of apr_pcalloc() so that the digest
   * contents are only set once by the caller. */
  svn_checksum_t *checksum = apr_palloc(pool, sizeof(*checksum) + digest_size);
  checksum->digest = (unsigned char *)checksum + sizeof(*checksum);
  checksum->kind = kind;
  return checksum;
}

/* Return a checksum object, allocated in POOL.  The checksum will be of
 * type KIND and contain the given DIGEST.
 */
static svn_checksum_t *
checksum_create(svn_checksum_kind_t kind,
                const unsigned char *digest,
                apr_pool_t *pool)
{
  apr_size_t digest_size = DIGESTSIZE(kind);
  svn_checksum_t *checksum = checksum_create_without_digest(kind, digest_size,
                                                            pool);
  memcpy((unsigned char *)checksum->digest, digest, digest_size);
  return checksum;
}

svn_checksum_t *
svn_checksum_create(svn_checksum_kind_t kind,
                    apr_pool_t *pool)
{
  svn_checksum_t *checksum;
  apr_size_t digest_size;

  switch (kind)
    {
      case svn_checksum_md5:
      case svn_checksum_sha1:
      case svn_checksum_fnv1a_32:
      case svn_checksum_fnv1a_32x4:
        digest_size = digest_sizes[kind];
        break;

      default:
        return NULL;
    }

  checksum = checksum_create_without_digest(kind, digest_size, pool);
  memset((unsigned char *) checksum->digest, 0, digest_size);
  return checksum;
}

svn_checksum_t *
svn_checksum__from_digest_md5(const unsigned char *digest,
                              apr_pool_t *result_pool)
{
  return checksum_create(svn_checksum_md5, digest, result_pool);
}

svn_checksum_t *
svn_checksum__from_digest_sha1(const unsigned char *digest,
                               apr_pool_t *result_pool)
{
  return checksum_create(svn_checksum_sha1, digest, result_pool);
}

svn_checksum_t *
svn_checksum__from_digest_fnv1a_32(const unsigned char *digest,
                                   apr_pool_t *result_pool)
{
  return checksum_create(svn_checksum_fnv1a_32, digest, result_pool);
}

svn_checksum_t *
svn_checksum__from_digest_fnv1a_32x4(const unsigned char *digest,
                                     apr_pool_t *result_pool)
{
  return checksum_create(svn_checksum_fnv1a_32x4, digest, result_pool);
}

svn_error_t *
svn_checksum_clear(svn_checksum_t *checksum)
{
  SVN_ERR(validate_kind(checksum->kind));

  memset((unsigned char *) checksum->digest, 0, DIGESTSIZE(checksum->kind));
  return SVN_NO_ERROR;
}

svn_boolean_t
svn_checksum_match(const svn_checksum_t *checksum1,
                   const svn_checksum_t *checksum2)
{
  if (checksum1 == NULL || checksum2 == NULL)
    return TRUE;

  if (checksum1->kind != checksum2->kind)
    return FALSE;

  switch (checksum1->kind)
    {
      case svn_checksum_md5:
      case svn_checksum_sha1:
      case svn_checksum_fnv1a_32:
      case svn_checksum_fnv1a_32x4:
        return svn__digests_match(checksum1->digest,
                                  checksum2->digest,
                                  digest_sizes[checksum1->kind]);

      default:
        /* We really shouldn't get here, but if we do... */
        return FALSE;
    }
}

const char *
svn_checksum_to_cstring_display(const svn_checksum_t *checksum,
                                apr_pool_t *pool)
{
  switch (checksum->kind)
    {
      case svn_checksum_md5:
      case svn_checksum_sha1:
      case svn_checksum_fnv1a_32:
      case svn_checksum_fnv1a_32x4:
        return svn__digest_to_cstring_display(checksum->digest,
                                              digest_sizes[checksum->kind],
                                              pool);

      default:
        /* We really shouldn't get here, but if we do... */
        return NULL;
    }
}

const char *
svn_checksum_to_cstring(const svn_checksum_t *checksum,
                        apr_pool_t *pool)
{
  if (checksum == NULL)
    return NULL;

  switch (checksum->kind)
    {
      case svn_checksum_md5:
      case svn_checksum_sha1:
      case svn_checksum_fnv1a_32:
      case svn_checksum_fnv1a_32x4:
        return svn__digest_to_cstring(checksum->digest,
                                      digest_sizes[checksum->kind],
                                      pool);

      default:
        /* We really shouldn't get here, but if we do... */
        return NULL;
    }
}


const char *
svn_checksum_serialize(const svn_checksum_t *checksum,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT_NO_RETURN(checksum->kind >= svn_checksum_md5
                           || checksum->kind <= svn_checksum_fnv1a_32x4);
  return apr_pstrcat(result_pool,
                     ckind_str[checksum->kind],
                     svn_checksum_to_cstring(checksum, scratch_pool),
                     SVN_VA_NULL);
}


svn_error_t *
svn_checksum_deserialize(const svn_checksum_t **checksum,
                         const char *data,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_checksum_kind_t kind;
  svn_checksum_t *parsed_checksum;

  /* All prefixes have the same length. */
  apr_size_t prefix_len = strlen(ckind_str[0]);

  /* "$md5 $...", "$sha1$..." or ... */
  if (strlen(data) <= prefix_len)
    return svn_error_createf(SVN_ERR_BAD_CHECKSUM_PARSE, NULL,
                             _("Invalid prefix in checksum '%s'"),
                             data);

  for (kind = svn_checksum_md5; kind <= svn_checksum_fnv1a_32x4; ++kind)
    if (strncmp(ckind_str[kind], data, prefix_len) == 0)
      {
        SVN_ERR(svn_checksum_parse_hex(&parsed_checksum, kind,
                                       data + prefix_len, result_pool));
        *checksum = parsed_checksum;
        return SVN_NO_ERROR;
      }

  return svn_error_createf(SVN_ERR_BAD_CHECKSUM_KIND, NULL,
                           "Unknown checksum kind in '%s'", data);
}


svn_error_t *
svn_checksum_parse_hex(svn_checksum_t **checksum,
                       svn_checksum_kind_t kind,
                       const char *hex,
                       apr_pool_t *pool)
{
  apr_size_t i, len;
  unsigned char is_nonzero = 0;
  unsigned char *digest;
  static const unsigned char xdigitval[256] =
    {
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,   /* 0-7 */
      0x08,0x09,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,   /* 8-9 */
      0xFF,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0xFF,   /* A-F */
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0xFF,   /* a-f */
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
      0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    };

  if (hex == NULL)
    {
      *checksum = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(validate_kind(kind));

  *checksum = svn_checksum_create(kind, pool);
  digest = (unsigned char *)(*checksum)->digest;
  len = DIGESTSIZE(kind);

  for (i = 0; i < len; i++)
    {
      unsigned char x1 = xdigitval[(unsigned char)hex[i * 2]];
      unsigned char x2 = xdigitval[(unsigned char)hex[i * 2 + 1]];
      if (x1 == 0xFF || x2 == 0xFF)
        return svn_error_create(SVN_ERR_BAD_CHECKSUM_PARSE, NULL, NULL);

      digest[i] = (x1 << 4) | x2;
      is_nonzero |= digest[i];
    }

  if (!is_nonzero)
    *checksum = NULL;

  return SVN_NO_ERROR;
}

svn_checksum_t *
svn_checksum_dup(const svn_checksum_t *checksum,
                 apr_pool_t *pool)
{
  /* The duplicate of a NULL checksum is a NULL... */
  if (checksum == NULL)
    return NULL;

  /* Without this check on valid checksum kind a NULL svn_checksum_t
   * pointer is returned which could cause a core dump at an
   * indeterminate time in the future because callers are not
   * expecting a NULL pointer.  This commit forces an early abort() so
   * it's easier to track down where the issue arose. */
  switch (checksum->kind)
    {
      case svn_checksum_md5:
      case svn_checksum_sha1:
      case svn_checksum_fnv1a_32:
      case svn_checksum_fnv1a_32x4:
        return checksum_create(checksum->kind, checksum->digest, pool);

      default:
        SVN_ERR_MALFUNCTION_NO_RETURN();
        break;
    }
}

svn_error_t *
svn_checksum(svn_checksum_t **checksum,
             svn_checksum_kind_t kind,
             const void *data,
             apr_size_t len,
             apr_pool_t *pool)
{
  apr_sha1_ctx_t sha1_ctx;

  SVN_ERR(validate_kind(kind));
  *checksum = svn_checksum_create(kind, pool);

  switch (kind)
    {
      case svn_checksum_md5:
        apr_md5((unsigned char *)(*checksum)->digest, data, len);
        break;

      case svn_checksum_sha1:
        apr_sha1_init(&sha1_ctx);
        apr_sha1_update(&sha1_ctx, data, (unsigned int)len);
        apr_sha1_final((unsigned char *)(*checksum)->digest, &sha1_ctx);
        break;

      case svn_checksum_fnv1a_32:
        *(apr_uint32_t *)(*checksum)->digest
          = htonl(svn__fnv1a_32(data, len));
        break;

      case svn_checksum_fnv1a_32x4:
        *(apr_uint32_t *)(*checksum)->digest
          = htonl(svn__fnv1a_32x4(data, len));
        break;

      default:
        /* We really shouldn't get here, but if we do... */
        return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);
    }

  return SVN_NO_ERROR;
}


svn_checksum_t *
svn_checksum_empty_checksum(svn_checksum_kind_t kind,
                            apr_pool_t *pool)
{
  switch (kind)
    {
      case svn_checksum_md5:
      case svn_checksum_sha1:
      case svn_checksum_fnv1a_32:
      case svn_checksum_fnv1a_32x4:
        return checksum_create(kind, empty_string_digests[kind], pool);

      default:
        /* We really shouldn't get here, but if we do... */
        SVN_ERR_MALFUNCTION_NO_RETURN();
    }
}

struct svn_checksum_ctx_t
{
  void *apr_ctx;
  svn_checksum_kind_t kind;
};

svn_checksum_ctx_t *
svn_checksum_ctx_create(svn_checksum_kind_t kind,
                        apr_pool_t *pool)
{
  svn_checksum_ctx_t *ctx = apr_palloc(pool, sizeof(*ctx));

  ctx->kind = kind;
  switch (kind)
    {
      case svn_checksum_md5:
        ctx->apr_ctx = apr_palloc(pool, sizeof(apr_md5_ctx_t));
        apr_md5_init(ctx->apr_ctx);
        break;

      case svn_checksum_sha1:
        ctx->apr_ctx = apr_palloc(pool, sizeof(apr_sha1_ctx_t));
        apr_sha1_init(ctx->apr_ctx);
        break;

      case svn_checksum_fnv1a_32:
        ctx->apr_ctx = svn_fnv1a_32__context_create(pool);
        break;

      case svn_checksum_fnv1a_32x4:
        ctx->apr_ctx = svn_fnv1a_32x4__context_create(pool);
        break;

      default:
        SVN_ERR_MALFUNCTION_NO_RETURN();
    }

  return ctx;
}

svn_error_t *
svn_checksum_ctx_reset(svn_checksum_ctx_t *ctx)
{
  switch (ctx->kind)
    {
      case svn_checksum_md5:
        memset(ctx->apr_ctx, 0, sizeof(apr_md5_ctx_t));
        apr_md5_init(ctx->apr_ctx);
        break;

      case svn_checksum_sha1:
        memset(ctx->apr_ctx, 0, sizeof(apr_sha1_ctx_t));
        apr_sha1_init(ctx->apr_ctx);
        break;

      case svn_checksum_fnv1a_32:
        svn_fnv1a_32__context_reset(ctx->apr_ctx);
        break;

      case svn_checksum_fnv1a_32x4:
        svn_fnv1a_32x4__context_reset(ctx->apr_ctx);
        break;

      default:
        SVN_ERR_MALFUNCTION();
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum_update(svn_checksum_ctx_t *ctx,
                    const void *data,
                    apr_size_t len)
{
  switch (ctx->kind)
    {
      case svn_checksum_md5:
        apr_md5_update(ctx->apr_ctx, data, len);
        break;

      case svn_checksum_sha1:
        apr_sha1_update(ctx->apr_ctx, data, (unsigned int)len);
        break;

      case svn_checksum_fnv1a_32:
        svn_fnv1a_32__update(ctx->apr_ctx, data, len);
        break;

      case svn_checksum_fnv1a_32x4:
        svn_fnv1a_32x4__update(ctx->apr_ctx, data, len);
        break;

      default:
        /* We really shouldn't get here, but if we do... */
        return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_checksum_final(svn_checksum_t **checksum,
                   const svn_checksum_ctx_t *ctx,
                   apr_pool_t *pool)
{
  *checksum = svn_checksum_create(ctx->kind, pool);

  switch (ctx->kind)
    {
      case svn_checksum_md5:
        apr_md5_final((unsigned char *)(*checksum)->digest, ctx->apr_ctx);
        break;

      case svn_checksum_sha1:
        apr_sha1_final((unsigned char *)(*checksum)->digest, ctx->apr_ctx);
        break;

      case svn_checksum_fnv1a_32:
        *(apr_uint32_t *)(*checksum)->digest
          = htonl(svn_fnv1a_32__finalize(ctx->apr_ctx));
        break;

      case svn_checksum_fnv1a_32x4:
        *(apr_uint32_t *)(*checksum)->digest
          = htonl(svn_fnv1a_32x4__finalize(ctx->apr_ctx));
        break;

      default:
        /* We really shouldn't get here, but if we do... */
        return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL, NULL);
    }

  return SVN_NO_ERROR;
}

apr_size_t
svn_checksum_size(const svn_checksum_t *checksum)
{
  return DIGESTSIZE(checksum->kind);
}

svn_error_t *
svn_checksum_mismatch_err(const svn_checksum_t *expected,
                          const svn_checksum_t *actual,
                          apr_pool_t *scratch_pool,
                          const char *fmt,
                          ...)
{
  va_list ap;
  const char *desc;

  va_start(ap, fmt);
  desc = apr_pvsprintf(scratch_pool, fmt, ap);
  va_end(ap);

  return svn_error_createf(SVN_ERR_CHECKSUM_MISMATCH, NULL,
                           _("%s:\n"
                             "   expected:  %s\n"
                             "     actual:  %s\n"),
                desc,
                svn_checksum_to_cstring_display(expected, scratch_pool),
                svn_checksum_to_cstring_display(actual, scratch_pool));
}

svn_boolean_t
svn_checksum_is_empty_checksum(svn_checksum_t *checksum)
{
  /* By definition, the NULL checksum matches all others, including the
     empty one. */
  if (!checksum)
    return TRUE;

  switch (checksum->kind)
    {
      case svn_checksum_md5:
      case svn_checksum_sha1:
      case svn_checksum_fnv1a_32:
      case svn_checksum_fnv1a_32x4:
        return svn__digests_match(checksum->digest,
                                  svn__empty_string_digest(checksum->kind),
                                  digest_sizes[checksum->kind]);

      default:
        /* We really shouldn't get here, but if we do... */
        SVN_ERR_MALFUNCTION_NO_RETURN();
    }
}

/* Checksum calculating stream wrappers.
 */

/* Baton used by write_handler and close_handler to calculate the checksum
 * and return the result to the stream creator.  It accommodates the data
 * needed by svn_checksum__wrap_write_stream_fnv1a_32x4 as well as
 * svn_checksum__wrap_write_stream.
 */
typedef struct stream_baton_t
{
  /* Stream we are wrapping. Forward write() and close() operations to it. */
  svn_stream_t *inner_stream;

  /* Build the checksum data in here. */
  svn_checksum_ctx_t *context;

  /* Write the final checksum here. May be NULL. */
  svn_checksum_t **checksum;

  /* Copy the digest of the final checksum. May be NULL. */
  unsigned char *digest;

  /* Allocate the resulting checksum here. */
  apr_pool_t *pool;
} stream_baton_t;

/* Implement svn_write_fn_t.
 * Update checksum and pass data on to inner stream.
 */
static svn_error_t *
write_handler(void *baton,
              const char *data,
              apr_size_t *len)
{
  stream_baton_t *b = baton;

  SVN_ERR(svn_checksum_update(b->context, data, *len));
  SVN_ERR(svn_stream_write(b->inner_stream, data, len));

  return SVN_NO_ERROR;
}

/* Implement svn_close_fn_t.
 * Finalize checksum calculation and write results. Close inner stream.
 */
static svn_error_t *
close_handler(void *baton)
{
  stream_baton_t *b = baton;
  svn_checksum_t *local_checksum;

  /* Ensure we can always write to *B->CHECKSUM. */
  if (!b->checksum)
    b->checksum = &local_checksum;

  /* Get the final checksum. */
  SVN_ERR(svn_checksum_final(b->checksum, b->context, b->pool));

  /* Extract digest, if wanted. */
  if (b->digest)
    {
      apr_size_t digest_size = DIGESTSIZE((*b->checksum)->kind);
      memcpy(b->digest, (*b->checksum)->digest, digest_size);
    }

  /* Done here.  Now, close the underlying stream as well. */
  return svn_error_trace(svn_stream_close(b->inner_stream));
}

/* Common constructor function for svn_checksum__wrap_write_stream and
 * svn_checksum__wrap_write_stream_fnv1a_32x4, taking the superset of their
 * respecting parameters.
 *
 * In the current usage, either CHECKSUM or DIGEST will be NULL but this
 * function does not enforce any such restriction.  Also, the caller must
 * make sure that DIGEST refers to a buffer of sufficient length.
 */
static svn_stream_t *
wrap_write_stream(svn_checksum_t **checksum,
                  unsigned char *digest,
                  svn_stream_t *inner_stream,
                  svn_checksum_kind_t kind,
                  apr_pool_t *pool)
{
  svn_stream_t *outer_stream;

  stream_baton_t *baton = apr_pcalloc(pool, sizeof(*baton));
  baton->inner_stream = inner_stream;
  baton->context = svn_checksum_ctx_create(kind, pool);
  baton->checksum = checksum;
  baton->digest = digest;
  baton->pool = pool;

  outer_stream = svn_stream_create(baton, pool);
  svn_stream_set_write(outer_stream, write_handler);
  svn_stream_set_close(outer_stream, close_handler);

  return outer_stream;
}

svn_stream_t *
svn_checksum__wrap_write_stream(svn_checksum_t **checksum,
                                svn_stream_t *inner_stream,
                                svn_checksum_kind_t kind,
                                apr_pool_t *pool)
{
  return wrap_write_stream(checksum, NULL, inner_stream, kind, pool);
}

/* Implement svn_close_fn_t.
 * For FNV-1a-like checksums, we want the checksum as 32 bit integer instead
 * of a big endian 4 byte sequence.  This simply wraps close_handler adding
 * the digest conversion.
 */
static svn_error_t *
close_handler_fnv1a_32x4(void *baton)
{
  stream_baton_t *b = baton;
  SVN_ERR(close_handler(baton));

  *(apr_uint32_t *)b->digest = ntohl(*(apr_uint32_t *)b->digest);
  return SVN_NO_ERROR;
}

svn_stream_t *
svn_checksum__wrap_write_stream_fnv1a_32x4(apr_uint32_t *digest,
                                           svn_stream_t *inner_stream,
                                           apr_pool_t *pool)
{
  svn_stream_t *result
    = wrap_write_stream(NULL, (unsigned char *)digest, inner_stream,
                        svn_checksum_fnv1a_32x4, pool);
  svn_stream_set_close(result, close_handler_fnv1a_32x4);

  return result;
}
