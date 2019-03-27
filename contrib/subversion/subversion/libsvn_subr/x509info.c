/*
 * x509info.c:  Accessors for svn_x509_certinfo_t
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



#include <string.h>

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_string.h"
#include "svn_hash.h"
#include "x509.h"



svn_x509_name_attr_t *
svn_x509_name_attr_dup(const svn_x509_name_attr_t *attr,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_x509_name_attr_t *result = apr_palloc(result_pool, sizeof(*result));
  result->oid_len = attr->oid_len;
  result->oid = apr_pmemdup(result_pool, attr->oid, attr->oid_len);
  result->utf8_value = apr_pstrdup(result_pool, attr->utf8_value);

  return result;
}

const unsigned char *
svn_x509_name_attr_get_oid(const svn_x509_name_attr_t *attr, apr_size_t *len)
{
  *len = attr->oid_len;
  return attr->oid;
}

const char *
svn_x509_name_attr_get_value(const svn_x509_name_attr_t *attr)
{
  return attr->utf8_value;
}

/* Array elements are assumed to be nul-terminated C strings. */
static apr_array_header_t *
deep_copy_array(apr_array_header_t *s, apr_pool_t *result_pool)
{
  int i;
  apr_array_header_t *d;

  if (!s)
    return NULL;

  d = apr_array_copy(result_pool, s);

  /* Make a deep copy of the strings in the array. */
  for (i = 0; i < s->nelts; ++i)
    {
      APR_ARRAY_IDX(d, i, const char *) =
        apr_pstrdup(result_pool, APR_ARRAY_IDX(s, i, const char *));
    }

  return d;
}

/* Copy an array with elements that are svn_x509_name_attr_t's */
static apr_array_header_t *
deep_copy_name_attrs(apr_array_header_t *s, apr_pool_t *result_pool)
{
  int i;
  apr_array_header_t *d;

  if (!s)
    return NULL;

  d = apr_array_copy(result_pool, s);

  /* Make a deep copy of the svn_x509_name_attr_t's in the array. */
  for (i = 0; i < s->nelts; ++i)
    {
      APR_ARRAY_IDX(d, i, const svn_x509_name_attr_t *) =
        svn_x509_name_attr_dup(APR_ARRAY_IDX(s, i, svn_x509_name_attr_t *),
                               result_pool, result_pool);
    }

  return d;
}

svn_x509_certinfo_t *
svn_x509_certinfo_dup(const svn_x509_certinfo_t *certinfo,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_x509_certinfo_t *result = apr_palloc(result_pool, sizeof(*result));
  result->subject = deep_copy_name_attrs(certinfo->subject, result_pool);
  result->issuer = deep_copy_name_attrs(certinfo->issuer, result_pool);
  result->valid_from = certinfo->valid_from;
  result->valid_to = certinfo->valid_to;
  result->digest = svn_checksum_dup(certinfo->digest, result_pool);
  result->hostnames = deep_copy_array(certinfo->hostnames, result_pool);

  return result;
}

typedef struct asn1_oid {
  const unsigned char *oid;
  const apr_size_t oid_len;
  const char *short_label;
  const char *long_label;
} asn1_oid;

#define CONSTANT_PAIR(c) (unsigned char *)(c), sizeof((c)) - 1

static const asn1_oid asn1_oids[] = {
  { CONSTANT_PAIR(SVN_X509_OID_COMMON_NAME),  "CN", "commonName" },
  { CONSTANT_PAIR(SVN_X509_OID_COUNTRY),      "C",  "countryName" },
  { CONSTANT_PAIR(SVN_X509_OID_LOCALITY),     "L",  "localityName" },
  { CONSTANT_PAIR(SVN_X509_OID_STATE),        "ST", "stateOrProvinceName" },
  { CONSTANT_PAIR(SVN_X509_OID_ORGANIZATION), "O",  "organizationName" },
  { CONSTANT_PAIR(SVN_X509_OID_ORG_UNIT),     "OU", "organizationalUnitName"},
  { CONSTANT_PAIR(SVN_X509_OID_EMAIL),        NULL, "emailAddress" },
  { NULL },
};

/* Given an OID return a null-terminated C string representation.
 * For example an OID with the bytes "\x2A\x86\x48\x86\xF7\x0D\x01\x09\x01"
 * would be converted to the string "1.2.840.113549.1.9.1". */
const char *
svn_x509_oid_to_string(const unsigned char *oid, apr_size_t oid_len,
                       apr_pool_t *scratch_pool, apr_pool_t *result_pool)
{
  svn_stringbuf_t *out = svn_stringbuf_create_empty(result_pool);
  const unsigned char *p = oid;
  const unsigned char *end = p + oid_len;
  const char *temp;

  while (p != end) {
    if (p == oid)
      {
        /* Handle decoding the first two values of the OID.  These values
         * are encoded by taking the first value and adding 40 to it and
         * adding the result to the second value, then placing this single
         * value in the first byte of the output.  This is unambiguous since
         * the first value is apparently limited to 0, 1 or 2 and the second
         * is limited to 0 to 39. */
        temp = apr_psprintf(scratch_pool, "%d.%d", *p / 40, *p % 40);
        p++;
      }
    else if (*p < 128)
      {
        /* The remaining values if they're less than 128 are just
         * the number one to one encoded */
        temp = apr_psprintf(scratch_pool, ".%d", *p);
        p++;
      }
    else
      {
        /* Values greater than 128 are encoded as a series of 7 bit values
         * with the left most bit set to indicate this encoding with the
         * last octet missing the left most bit to finish out the series.. */
        unsigned int collector = 0;
        svn_boolean_t dot = FALSE;

        do {
          if (collector == 0 && *p == 0x80)
            {
              /* include leading zeros in the string representation
                 technically not legal, but this seems nicer than just
                 returning NULL */
              if (!dot)
                {
                  svn_stringbuf_appendbyte(out, '.');
                  dot = TRUE;
                }
              svn_stringbuf_appendbyte(out, '0');
            }
          else if (collector > UINT_MAX >> 7)
            {
              /* overflow */
              return NULL;
            }
          collector = collector << 7 | (*(p++) & 0x7f);
        } while (p != end && *p > 127);
        if (collector > UINT_MAX >> 7)
          return NULL; /* overflow */
        collector = collector << 7 | *(p++);
        temp = apr_psprintf(scratch_pool, "%s%d", dot ? "" : ".", collector);
      }
    svn_stringbuf_appendcstr(out, temp);
  }

  if (svn_stringbuf_isempty(out))
    return NULL;

  return out->data;
}

static const asn1_oid *oid_to_asn1_oid(unsigned char *oid, apr_size_t oid_len)
{
  const asn1_oid *entry;

  for (entry = asn1_oids; entry->oid; entry++)
    {
      if (oid_len == entry->oid_len &&
          memcmp(oid, entry->oid, oid_len) == 0)
        return entry;
    }

  return NULL;
}

static const char *oid_to_best_label(unsigned char *oid, apr_size_t oid_len,
                                     apr_pool_t *result_pool)
{
  const asn1_oid *entry = oid_to_asn1_oid(oid, oid_len);

  if (entry)
    {
      if (entry->short_label)
        return entry->short_label;

      if (entry->long_label)
        return entry->long_label;
    }
  else
    {
      const char *oid_string = svn_x509_oid_to_string(oid, oid_len,
                                                      result_pool, result_pool);
      if (oid_string)
        return oid_string;
    }

  return "??";
}

/*
 * Store the name from dn in printable form into buf,
 * using scratch_pool for any temporary allocations.
 * If CN is not NULL, return any common name in CN
 */
static const char *
get_dn(apr_array_header_t *name,
       apr_pool_t *result_pool)
{
  svn_stringbuf_t *buf = svn_stringbuf_create_empty(result_pool);
  int n;

  for (n = 0; n < name->nelts; n++)
    {
      const svn_x509_name_attr_t *attr = APR_ARRAY_IDX(name, n, svn_x509_name_attr_t *);

      if (n > 0)
        svn_stringbuf_appendcstr(buf, ", ");

      svn_stringbuf_appendcstr(buf, oid_to_best_label(attr->oid, attr->oid_len, result_pool));
      svn_stringbuf_appendbyte(buf, '=');
      svn_stringbuf_appendcstr(buf, attr->utf8_value);
    }

  return buf->data;
}

const char *
svn_x509_certinfo_get_subject(const svn_x509_certinfo_t *certinfo,
                              apr_pool_t *result_pool)
{
  return get_dn(certinfo->subject, result_pool);
}

const apr_array_header_t *
svn_x509_certinfo_get_subject_attrs(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->subject;
}

const char *
svn_x509_certinfo_get_issuer(const svn_x509_certinfo_t *certinfo,
                             apr_pool_t *result_pool)
{
  return get_dn(certinfo->issuer, result_pool);
}

const apr_array_header_t *
svn_x509_certinfo_get_issuer_attrs(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->issuer;
}

apr_time_t
svn_x509_certinfo_get_valid_from(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->valid_from;
}

apr_time_t
svn_x509_certinfo_get_valid_to(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->valid_to;
}

const svn_checksum_t *
svn_x509_certinfo_get_digest(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->digest;
}

const apr_array_header_t *
svn_x509_certinfo_get_hostnames(const svn_x509_certinfo_t *certinfo)
{
  return certinfo->hostnames;
}

