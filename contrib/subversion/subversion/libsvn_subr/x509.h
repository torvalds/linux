/**
 * \file x509.h
 *
 *  Based on XySSL: Copyright (C) 2006-2008  Christophe Devine
 *
 *  Copyright (C) 2009  Paul Bakker <polarssl_maintainer at polarssl dot org>
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the names of PolarSSL or XySSL nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef SVN_LIBSVN_SUBR_X509_H
#define SVN_LIBSVN_SUBR_X509_H

#include <stddef.h>
#include <apr_time.h>

#include "svn_x509.h"

/*
 * DER constants
 */
#define ASN1_BOOLEAN                 0x01
#define ASN1_INTEGER                 0x02
#define ASN1_BIT_STRING              0x03
#define ASN1_OCTET_STRING            0x04
#define ASN1_NULL                    0x05
#define ASN1_OID                     0x06
#define ASN1_UTF8_STRING             0x0C
#define ASN1_SEQUENCE                0x10
#define ASN1_SET                     0x11
#define ASN1_PRINTABLE_STRING        0x13
#define ASN1_T61_STRING              0x14
#define ASN1_IA5_STRING              0x16
#define ASN1_UTC_TIME                0x17
#define ASN1_GENERALIZED_TIME        0x18
#define ASN1_UNIVERSAL_STRING        0x1C
#define ASN1_BMP_STRING              0x1E
#define ASN1_PRIMITIVE               0x00
#define ASN1_CONSTRUCTED             0x20
#define ASN1_CONTEXT_SPECIFIC        0x80

/*
 * various object identifiers
 */
#define OID_SUBJECT_ALT_NAME    "\x55\x1D\x11"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Structures for parsing X.509 certificates
 */
typedef struct _x509_buf {
  int tag;
  ptrdiff_t len;
  const unsigned char *p;
} x509_buf;

typedef struct _x509_name {
  x509_buf oid;
  x509_buf val;
  struct _x509_name *next;
} x509_name;

typedef struct _x509_cert {
  int version;
  x509_buf serial;
  x509_buf sig_oid1;

  x509_name issuer;
  x509_name subject;

  apr_time_t valid_from;
  apr_time_t valid_to;

  x509_buf issuer_id;
  x509_buf subject_id;
  apr_array_header_t *dnsnames;

  x509_buf sig_oid2;
  x509_buf sig;

} x509_cert;


struct svn_x509_name_attr_t {
  unsigned char *oid;
  apr_size_t oid_len;
  const char *utf8_value;
};

/*
 * Certificate info, returned from the parser
 */
struct svn_x509_certinfo_t
{
  apr_array_header_t *issuer;
  apr_array_header_t *subject;
  apr_time_t valid_from;
  apr_time_t valid_to;
  svn_checksum_t *digest;
  apr_array_header_t *hostnames;
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif        /* SVN_LIBSVN_SUBR_X509_H */
