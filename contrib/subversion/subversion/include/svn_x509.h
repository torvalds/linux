/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_x509.h
 * @brief Subversion's X509 parser
 */

#ifndef SVN_X509_H
#define SVN_X509_H

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_time.h>

#include "svn_error.h"
#include "svn_checksum.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVN_X509_OID_COMMON_NAME  "\x55\x04\x03"
#define SVN_X509_OID_COUNTRY      "\x55\x04\x06"
#define SVN_X509_OID_LOCALITY     "\x55\x04\x07"
#define SVN_X509_OID_STATE        "\x55\x04\x08"
#define SVN_X509_OID_ORGANIZATION "\x55\x04\x0A"
#define SVN_X509_OID_ORG_UNIT     "\x55\x04\x0B"
#define SVN_X509_OID_EMAIL        "\x2A\x86\x48\x86\xF7\x0D\x01\x09\x01"

/**
 * Representation of parsed certificate info.
 *
 * @since New in 1.9.
 */
typedef struct svn_x509_certinfo_t svn_x509_certinfo_t;

/**
 * Representation of an atttribute in an X.509 name (e.g. Subject or Issuer)
 *
 * @since New in 1.9.
 */
typedef struct svn_x509_name_attr_t svn_x509_name_attr_t;

/**
 * Parse x509 @a der certificate data from @a buf with length @a
 * buflen and return certificate information in @a *certinfo,
 * allocated in @a result_pool.
 *
 * @note This function has been written with the intent of display data in a
 *       certificate for a user to see.  As a result, it does not do much
 *       validation on the data it parses from the certificate.  It does not
 *       for instance verify that the certificate is signed by the issuer.  It
 *       does not verify a trust chain.  It does not error on critical
 *       extensions it does not know how to parse.  So while it can be used as
 *       part of a certificate validation scheme, it can't be used alone for
 *       that purpose.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_x509_parse_cert(svn_x509_certinfo_t **certinfo,
                    const char *buf,
                    apr_size_t buflen,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);

/**
 * Returns a deep copy of the @a attr, allocated in @a result_pool.
 * May use @a scratch_pool for temporary allocations.
 * @since New in 1.9.
 */
svn_x509_name_attr_t *
svn_x509_name_attr_dup(const svn_x509_name_attr_t *attr,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

/**
 * Returns the OID of @a attr as encoded in the certificate.  The
 * length of the OID will be set in @a len.
 * @since New in 1.9.
 */
const unsigned char *
svn_x509_name_attr_get_oid(const svn_x509_name_attr_t *attr, apr_size_t *len);

/**
 * Returns the value of @a attr as a UTF-8 C string.
 * @since New in 1.9.
 */
const char *
svn_x509_name_attr_get_value(const svn_x509_name_attr_t *attr);


/**
 * Returns a deep copy of @a certinfo, allocated in @a result_pool.
 * May use @a scratch_pool for temporary allocations.
 * @since New in 1.9.
 */
svn_x509_certinfo_t *
svn_x509_certinfo_dup(const svn_x509_certinfo_t *certinfo,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);

/**
 * Returns the subject DN from @a certinfo.
 * @since New in 1.9.
 */
const char *
svn_x509_certinfo_get_subject(const svn_x509_certinfo_t *certinfo,
                              apr_pool_t *result_pool);

/**
 * Returns a list of the attributes for the subject in the @a certinfo.
 * Each member of the list is of type svn_x509_name_attr_t.
 *
 * @since New in 1.9.
 */
const apr_array_header_t *
svn_x509_certinfo_get_subject_attrs(const svn_x509_certinfo_t *certinfo);

/**
 * Returns the cerficiate issuer DN from @a certinfo.
 * @since New in 1.9.
 */
const char *
svn_x509_certinfo_get_issuer(const svn_x509_certinfo_t *certinfo,
                             apr_pool_t *result_pool);

/**
 * Returns a list of the attributes for the issuer in the @a certinfo.
 * Each member of the list is of type svn_x509_name_attr_t.
 *
 * @since New in 1.9.
 */
const apr_array_header_t *
svn_x509_certinfo_get_issuer_attrs(const svn_x509_certinfo_t *certinfo);

/**
 * Returns the start of the certificate validity period from @a certinfo.
 *
 * @since New in 1.9.
 */
apr_time_t
svn_x509_certinfo_get_valid_from(const svn_x509_certinfo_t *certinfo);

/**
 * Returns the end of the certificate validity period from @a certinfo.
 *
 * @since New in 1.9.
 */
apr_time_t
svn_x509_certinfo_get_valid_to(const svn_x509_certinfo_t *certinfo);

/**
 * Returns the digest (fingerprint) from @a certinfo
 * @since New in 1.9.
 */
const svn_checksum_t *
svn_x509_certinfo_get_digest(const svn_x509_certinfo_t *certinfo);

/**
 * Returns an array of (const char*) host names from @a certinfo.
 *
 * @since New in 1.9.
 */
const apr_array_header_t *
svn_x509_certinfo_get_hostnames(const svn_x509_certinfo_t *certinfo);

/**
 * Given an @a oid return a null-terminated C string representation.
 * For example an OID with the bytes "\x2A\x86\x48\x86\xF7\x0D\x01\x09\x01"
 * would be converted to the string "1.2.840.113549.1.9.1".  Returns
 * NULL if the @a oid can't be represented as a string.
 *
 * @since New in 1.9. */
const char *
svn_x509_oid_to_string(const unsigned char *oid, apr_size_t oid_len,
                       apr_pool_t *scratch_pool, apr_pool_t *result_pool);

#ifdef __cplusplus
}
#endif
#endif        /* SVN_X509_H */
