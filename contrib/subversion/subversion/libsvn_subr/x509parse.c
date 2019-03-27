/*
 *  X.509 certificate and private key decoding
 *
 *  Based on XySSL: Copyright (C) 2006-2008   Christophe Devine
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
 *    notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *    * Neither the names of PolarSSL or XySSL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
/*
 *  The ITU-T X.509 standard defines a certificate format for PKI.
 *
 *  http://www.ietf.org/rfc/rfc5280.txt
 *  http://www.ietf.org/rfc/rfc3279.txt
 *  http://www.ietf.org/rfc/rfc6818.txt
 *
 *  ftp://ftp.rsasecurity.com/pub/pkcs/ascii/pkcs-1v2.asc
 *
 *  http://www.itu.int/ITU-T/studygroups/com17/languages/X.680-0207.pdf
 *  http://www.itu.int/ITU-T/studygroups/com17/languages/X.690-0207.pdf
 */

#include <apr_pools.h>
#include <apr_tables.h>
#include "svn_hash.h"
#include "svn_string.h"
#include "svn_time.h"
#include "svn_checksum.h"
#include "svn_utf.h"
#include "svn_ctype.h"
#include "private/svn_utf_private.h"
#include "private/svn_string_private.h"

#include "x509.h"

#include <string.h>
#include <stdio.h>

/*
 * ASN.1 DER decoding routines
 */
static svn_error_t *
asn1_get_len(const unsigned char **p, const unsigned char *end,
             ptrdiff_t *len)
{
  if ((end - *p) < 1)
    return svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, NULL, NULL);

  if ((**p & 0x80) == 0)
    *len = *(*p)++;
  else
    switch (**p & 0x7F)
      {
      case 1:
        if ((end - *p) < 2)
          return svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, NULL, NULL);

        *len = (*p)[1];
        (*p) += 2;
        break;

      case 2:
        if ((end - *p) < 3)
          return svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, NULL, NULL);

        *len = ((*p)[1] << 8) | (*p)[2];
        (*p) += 3;
        break;

      default:
        return svn_error_create(SVN_ERR_ASN1_INVALID_LENGTH, NULL, NULL);
        break;
      }

  if (*len > (end - *p))
    return svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, NULL, NULL);

  return SVN_NO_ERROR;
}

static svn_error_t *
asn1_get_tag(const unsigned char **p,
             const unsigned char *end, ptrdiff_t *len, int tag)
{
  if ((end - *p) < 1)
    return svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, NULL, NULL);

  if (**p != tag)
    return svn_error_create(SVN_ERR_ASN1_UNEXPECTED_TAG, NULL, NULL);

  (*p)++;

  return svn_error_trace(asn1_get_len(p, end, len));
}

static svn_error_t *
asn1_get_int(const unsigned char **p, const unsigned char *end, int *val)
{
  ptrdiff_t len;

  SVN_ERR(asn1_get_tag(p, end, &len, ASN1_INTEGER));

  /* Reject bit patterns that would overflow the output and those that
     represent negative values. */
  if (len > (int)sizeof(int) || (**p & 0x80) != 0)
    return svn_error_create(SVN_ERR_ASN1_INVALID_LENGTH, NULL, NULL);

  *val = 0;

  while (len-- > 0) {
    /* This would be undefined for bit-patterns of negative values. */
    *val = (*val << 8) | **p;
    (*p)++;
  }

  return SVN_NO_ERROR;
}

static svn_boolean_t
equal(const void *left, apr_size_t left_len,
      const void *right, apr_size_t right_len)
{
  if (left_len != right_len)
    return FALSE;

  return memcmp(left, right, right_len) == 0;
}

static svn_boolean_t
oids_equal(x509_buf *left, x509_buf *right)
{
  return equal(left->p, left->len,
               right->p, right->len);
}

/*
 *  Version   ::=  INTEGER  {  v1(0), v2(1), v3(2)  }
 */
static svn_error_t *
x509_get_version(const unsigned char **p, const unsigned char *end, int *ver)
{
  svn_error_t *err;
  ptrdiff_t len;

  /*
   * As defined in the Basic Certificate fields:
   *   version         [0]  EXPLICIT Version DEFAULT v1,
   * the version is the context specific tag 0.
   */
  err = asn1_get_tag(p, end, &len,
                     ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTED | 0);
  if (err)
    {
      if (err->apr_err == SVN_ERR_ASN1_UNEXPECTED_TAG)
        {
          svn_error_clear(err);
          *ver = 0;
          return SVN_NO_ERROR;
        }

      return svn_error_trace(err);
    }

  end = *p + len;

  err = asn1_get_int(p, end, ver);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_VERSION, err, NULL);

  if (*p != end)
    {
      err = svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, NULL, NULL);
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_VERSION, err, NULL);
    }

  return SVN_NO_ERROR;
}

/*
 *  CertificateSerialNumber   ::=  INTEGER
 */
static svn_error_t *
x509_get_serial(const unsigned char **p,
                const unsigned char *end, x509_buf * serial)
{
  svn_error_t *err;

  if ((end - *p) < 1)
    {
      err = svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, NULL, NULL);
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_SERIAL, err, NULL);
    }

  if (**p != (ASN1_CONTEXT_SPECIFIC | ASN1_PRIMITIVE | 2) &&
      **p != ASN1_INTEGER)
    {
      err = svn_error_create(SVN_ERR_ASN1_UNEXPECTED_TAG, NULL, NULL);
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_SERIAL, err, NULL);
    }

  serial->tag = *(*p)++;

  err = asn1_get_len(p, end, &serial->len);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_SERIAL, err, NULL);

  serial->p = *p;
  *p += serial->len;

  return SVN_NO_ERROR;
}

/*
 *  AlgorithmIdentifier   ::=  SEQUENCE  {
 *     algorithm         OBJECT IDENTIFIER,
 *     parameters        ANY DEFINED BY algorithm OPTIONAL  }
 */
static svn_error_t *
x509_get_alg(const unsigned char **p, const unsigned char *end, x509_buf * alg)
{
  svn_error_t *err;
  ptrdiff_t len;

  err = asn1_get_tag(p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_ALG, err, NULL);

  end = *p + len;
  alg->tag = **p;

  err = asn1_get_tag(p, end, &alg->len, ASN1_OID);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_ALG, err, NULL);

  alg->p = *p;
  *p += alg->len;

  if (*p == end)
    return SVN_NO_ERROR;
  
  /* The OID encoding of 1.2.840.113549.1.1.10 (id-RSASSA-PSS) */
#define OID_RSASSA_PSS "\x2a\x86\x48\x86\xf7\x0d\x01\x01\x0a"

  if (equal(alg->p, alg->len, OID_RSASSA_PSS, sizeof(OID_RSASSA_PSS) - 1))
    {
      /* Skip over algorithm parameters for id-RSASSA-PSS (RFC 8017)
       *
       * RSASSA-PSS-params ::= SEQUENCE {
       *  hashAlgorithm      [0] HashAlgorithm    DEFAULT sha1,
       *  maskGenAlgorithm   [1] MaskGenAlgorithm DEFAULT mgf1SHA1,
       *  saltLength         [2] INTEGER          DEFAULT 20,
       *  trailerField       [3] TrailerField     DEFAULT trailerFieldBC
       * }
       */
      err = asn1_get_tag(p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
      if (err)
        return svn_error_create(SVN_ERR_X509_CERT_INVALID_ALG, err, NULL);

      *p += len;
    }
  else
    {
      /* Algorithm parameters must be NULL for other algorithms */
      err = asn1_get_tag(p, end, &len, ASN1_NULL);
      if (err)
        return svn_error_create(SVN_ERR_X509_CERT_INVALID_ALG, err, NULL);
    }

  if (*p != end)
    {
      err = svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, NULL, NULL);
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_ALG, err, NULL);
    }

  return SVN_NO_ERROR;
}

/*
 *  AttributeTypeAndValue ::= SEQUENCE {
 *    type     AttributeType,
 *    value     AttributeValue }
 *
 *  AttributeType ::= OBJECT IDENTIFIER
 *
 *  AttributeValue ::= ANY DEFINED BY AttributeType
 */
static svn_error_t *
x509_get_attribute(const unsigned char **p, const unsigned char *end,
                   x509_name *cur, apr_pool_t *result_pool)
{
  svn_error_t *err;
  ptrdiff_t len;
  x509_buf *oid;
  x509_buf *val;

  err = asn1_get_tag(p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, err, NULL);

  end = *p + len;

  oid = &cur->oid;

  err = asn1_get_tag(p, end, &oid->len, ASN1_OID);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, err, NULL);

  oid->tag = ASN1_OID;
  oid->p = *p;
  *p += oid->len;

  if ((end - *p) < 1)
    {
      err = svn_error_create(SVN_ERR_ASN1_OUT_OF_DATA, NULL, NULL);
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, err, NULL);
    }

  if (**p != ASN1_BMP_STRING && **p != ASN1_UTF8_STRING &&
      **p != ASN1_T61_STRING && **p != ASN1_PRINTABLE_STRING &&
      **p != ASN1_IA5_STRING && **p != ASN1_UNIVERSAL_STRING)
    {
      err = svn_error_create(SVN_ERR_ASN1_UNEXPECTED_TAG, NULL, NULL);
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, err, NULL);
    }

  val = &cur->val;
  val->tag = *(*p)++;

  err = asn1_get_len(p, end, &val->len);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, err, NULL);

  val->p = *p;
  *p += val->len;

  cur->next = NULL;

  if (*p != end)
    {
      err = svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, NULL, NULL);
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, err, NULL);
    }

  return SVN_NO_ERROR;
}

/*
 *   RelativeDistinguishedName ::=
 *   SET SIZE (1..MAX) OF AttributeTypeAndValue
 */
static svn_error_t *
x509_get_name(const unsigned char **p, const unsigned char *name_end,
              x509_name *name, apr_pool_t *result_pool)
{
  svn_error_t *err;
  ptrdiff_t len;
  const unsigned char *set_end;
  x509_name *cur = NULL;

  err = asn1_get_tag(p, name_end, &len, ASN1_CONSTRUCTED | ASN1_SET);
  if (err || len < 1)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_NAME, err, NULL);

  set_end = *p + len;

  /*
   * iterate until the end of the SET is reached
   */
  while (*p < set_end)
    {
      if (!cur)
        {
          cur = name;
        }
      else
        {
          cur->next = apr_palloc(result_pool, sizeof(x509_name));
          cur = cur->next;
        }
      SVN_ERR(x509_get_attribute(p, set_end, cur, result_pool));
    }

  /*
   * recurse until end of SEQUENCE (name) is reached
   */
  if (*p == name_end)
    return SVN_NO_ERROR;

  cur->next = apr_palloc(result_pool, sizeof(x509_name));

  return svn_error_trace(x509_get_name(p, name_end, cur->next, result_pool));
}

/* Retrieve the date from the X.509 cert data between *P and END in either
 * UTCTime or GeneralizedTime format (as defined in RFC 5280 s. 4.1.2.5.1 and
 * 4.1.2.5.2 respectively) and place the result in WHEN using  SCRATCH_POOL
 * for temporary allocations. */
static svn_error_t *
x509_get_date(apr_time_t *when,
              const unsigned char **p,
              const unsigned char *end,
              apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  apr_status_t ret;
  int tag;
  ptrdiff_t len;
  char *date;
  apr_time_exp_t xt = { 0 };
  char tz;

  err = asn1_get_tag(p, end, &len, ASN1_UTC_TIME);
  if (err && err->apr_err == SVN_ERR_ASN1_UNEXPECTED_TAG)
    {
      svn_error_clear(err);
      err = asn1_get_tag(p, end, &len, ASN1_GENERALIZED_TIME);
      tag = ASN1_GENERALIZED_TIME;
    }
  else
    {
      tag = ASN1_UTC_TIME;
    }
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, err, NULL);

  date = apr_pstrndup(scratch_pool, (const char *) *p, len);
  switch (tag)
    {
    case ASN1_UTC_TIME:
      if (sscanf(date, "%2d%2d%2d%2d%2d%2d%c",
                 &xt.tm_year, &xt.tm_mon, &xt.tm_mday,
                 &xt.tm_hour, &xt.tm_min, &xt.tm_sec, &tz) < 6)
        return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, NULL, NULL);

      /* UTCTime only provides a 2 digit year.  X.509 specifies that years
       * greater than or equal to 50 must be interpreted as 19YY and years
       * less than 50 be interpreted as 20YY.  This format is not used for
       * years greater than 2049. apr_time_exp_t wants years as the number
       * of years since 1900, so don't convert to 4 digits here. */
      xt.tm_year += 100 * (xt.tm_year < 50);
      break;

    case ASN1_GENERALIZED_TIME:
      if (sscanf(date, "%4d%2d%2d%2d%2d%2d%c",
                 &xt.tm_year, &xt.tm_mon, &xt.tm_mday,
                 &xt.tm_hour, &xt.tm_min, &xt.tm_sec, &tz) < 6)
        return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, NULL, NULL);

      /* GeneralizedTime has the full 4 digit year.  But apr_time_exp_t
       * wants years as the number of years since 1900. */
      xt.tm_year -= 1900;
      break;

    default:
      /* shouldn't ever get here because we should error out above in the
       * asn1_get_tag() bits but doesn't hurt to be extra paranoid. */
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, NULL, NULL);
      break;
    }

  /* check that the timezone is GMT
   * ASN.1 allows for the timezone to be specified but X.509 says it must
   * always be GMT.  A little bit of extra paranoia here seems like a good
   * idea. */
  if (tz != 'Z')
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, NULL, NULL);

  /* apr_time_exp_t expects months to be zero indexed, 0=Jan, 11=Dec. */
  xt.tm_mon -= 1;

  /* range checks (as per definition of apr_time_exp_t in apr_time.h) */
  if (xt.tm_usec < 0 ||
      xt.tm_sec < 0 || xt.tm_sec > 61 ||
      xt.tm_min < 0 || xt.tm_min > 59 ||
      xt.tm_hour < 0 || xt.tm_hour > 23 ||
      xt.tm_mday < 1 || xt.tm_mday > 31 ||
      xt.tm_mon < 0 || xt.tm_mon > 11 ||
      xt.tm_year < 0 ||
      xt.tm_wday < 0 || xt.tm_wday > 6 ||
      xt.tm_yday < 0 || xt.tm_yday > 365)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, NULL, NULL);

  ret = apr_time_exp_gmt_get(when, &xt);
  if (ret)
    return svn_error_wrap_apr(ret, NULL);

  *p += len;

  return SVN_NO_ERROR;
}

/*
 *  Validity ::= SEQUENCE {
 *     notBefore    Time,
 *     notAfter    Time }
 *
 *  Time ::= CHOICE {
 *     utcTime    UTCTime,
 *     generalTime  GeneralizedTime }
 */
static svn_error_t *
x509_get_dates(apr_time_t *from,
               apr_time_t *to,
               const unsigned char **p,
               const unsigned char *end,
               apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  ptrdiff_t len;

  err = asn1_get_tag(p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, err, NULL);

  end = *p + len;

  SVN_ERR(x509_get_date(from, p, end, scratch_pool));

  SVN_ERR(x509_get_date(to, p, end, scratch_pool));

  if (*p != end)
    {
      err = svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, NULL, NULL);
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_DATE, err, NULL);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
x509_get_sig(const unsigned char **p, const unsigned char *end, x509_buf * sig)
{
  svn_error_t *err;
  ptrdiff_t len;

  err = asn1_get_tag(p, end, &len, ASN1_BIT_STRING);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_SIGNATURE, err, NULL);

  sig->tag = ASN1_BIT_STRING;

  if (--len < 1 || *(*p)++ != 0)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_SIGNATURE, NULL, NULL);

  sig->len = len;
  sig->p = *p;

  *p += len;

  return SVN_NO_ERROR;
}

/*
 * X.509 v2/v3 unique identifier (not parsed)
 */
static svn_error_t *
x509_get_uid(const unsigned char **p,
             const unsigned char *end, x509_buf * uid, int n)
{
  svn_error_t *err;

  if (*p == end)
    return SVN_NO_ERROR;

  err = asn1_get_tag(p, end, &uid->len,
                     ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTED | n);
  if (err)
    {
      if (err->apr_err == SVN_ERR_ASN1_UNEXPECTED_TAG)
        {
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }

      return svn_error_trace(err);
    }

  uid->tag = ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTED | n;
  uid->p = *p;
  *p += uid->len;

  return SVN_NO_ERROR;
}

/*
 * X.509 v3 extensions (not parsed)
 */
static svn_error_t *
x509_get_ext(apr_array_header_t *dnsnames,
             const unsigned char **p,
             const unsigned char *end)
{
  svn_error_t *err;
  ptrdiff_t len;

  if (*p == end)
    return SVN_NO_ERROR;

  err = asn1_get_tag(p, end, &len,
                     ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTED | 3);
  if (err)
    {
      /* If there aren't extensions that's ok they aren't required */
      if (err->apr_err == SVN_ERR_ASN1_UNEXPECTED_TAG)
        {
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }

      return svn_error_trace(err);
    }

  end = *p + len;

  SVN_ERR(asn1_get_tag(p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE));

  if (end != *p + len)
    {
      err = svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, NULL, NULL);
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_EXTENSIONS, err, NULL);
    }

  while (*p < end)
    {
      ptrdiff_t ext_len;
      const unsigned char *ext_start, *sna_end;
      err = asn1_get_tag(p, end, &ext_len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
      if (err)
        return svn_error_create(SVN_ERR_X509_CERT_INVALID_EXTENSIONS, err,
                                NULL);
      ext_start = *p;

      err = asn1_get_tag(p, end, &len, ASN1_OID);
      if (err)
        return svn_error_create(SVN_ERR_X509_CERT_INVALID_EXTENSIONS, err,
                                NULL);

      /* skip all extensions except SubjectAltName */
      if (!equal(*p, len,
                 OID_SUBJECT_ALT_NAME, sizeof(OID_SUBJECT_ALT_NAME) - 1))
        {
          *p += ext_len - (*p - ext_start);
          continue;
        }
      *p += len;

      err = asn1_get_tag(p, end, &len, ASN1_OCTET_STRING);
      if (err)
        return svn_error_create(SVN_ERR_X509_CERT_INVALID_EXTENSIONS, err,
                                NULL);

      /*   SubjectAltName ::= GeneralNames

           GeneralNames ::= SEQUENCE SIZE (1..MAX) OF GeneralName

           GeneralName ::= CHOICE {
                other Name                      [0]     OtherName,
                rfc822Name                      [1]     IA5String,
                dNSName                         [2]     IA5String,
                x400Address                     [3]     ORAddress,
                directoryName                   [4]     Name,
                ediPartyName                    [5]     EDIPartyName,
                uniformResourceIdentifier       [6]     IA5String,
                iPAddress                       [7]     OCTET STRING,
                registeredID                    [8]     OBJECT IDENTIFIER } */
      sna_end = *p + len;

      err = asn1_get_tag(p, sna_end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
      if (err)
        return svn_error_create(SVN_ERR_X509_CERT_INVALID_EXTENSIONS, err,
                                NULL);

      if (sna_end != *p + len)
        {
          err = svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, NULL, NULL);
          return svn_error_create(SVN_ERR_X509_CERT_INVALID_EXTENSIONS, err, NULL);
        }

      while (*p < sna_end)
        {
          err = asn1_get_tag(p, sna_end, &len, ASN1_CONTEXT_SPECIFIC |
                             ASN1_PRIMITIVE | 2);
          if (err)
            {
              /* not not a dNSName */
              if (err->apr_err == SVN_ERR_ASN1_UNEXPECTED_TAG)
                {
                  svn_error_clear(err);
                  /* need to skip the tag and then find the length to
                   * skip to ignore this SNA entry. */
                  (*p)++;
                  SVN_ERR(asn1_get_len(p, sna_end, &len));
                  *p += len;
                  continue;
                }

              return svn_error_trace(err);
            }
          else
            {
              /* We found a dNSName entry */
              x509_buf *dnsname = apr_palloc(dnsnames->pool, sizeof(*dnsname));
              dnsname->tag = ASN1_IA5_STRING; /* implicit based on dNSName */
              dnsname->len = len;
              dnsname->p = *p;
              APR_ARRAY_PUSH(dnsnames, x509_buf *) = dnsname;
            }

          *p += len;
        }

    }

  return SVN_NO_ERROR;
}

/* Escape all non-ascii or control characters similar to
 * svn_xml_fuzzy_escape() and svn_utf_cstring_from_utf8_fuzzy().
 * All of the encoding formats somewhat overlap with ascii (BMPString
 * and UniversalString are actually always wider so you'll end up
 * with a bunch of escaped nul bytes, but ideally we don't get here
 * for those).  The result is always a nul-terminated C string. */
static const char *
fuzzy_escape(const svn_string_t *src, apr_pool_t *result_pool)
{
  const char *end = src->data + src->len;
  const char *p = src->data, *q;
  svn_stringbuf_t *outstr;
  char escaped_char[6]; /* ? \ u u u \0 */

  for (q = p; q < end; q++)
    {
      if (!svn_ctype_isascii(*q) || svn_ctype_iscntrl(*q))
        break;
    }

  if (q == end)
    return src->data;

  outstr = svn_stringbuf_create_empty(result_pool);
  while (1)
    {
      q = p;

      /* Traverse till either unsafe character or eos. */
      while (q < end && svn_ctype_isascii(*q) && !svn_ctype_iscntrl(*q))
        q++;

      /* copy chunk before marker */
      svn_stringbuf_appendbytes(outstr, p, q - p);

      if (q == end)
        break;

      apr_snprintf(escaped_char, sizeof(escaped_char), "?\\%03u",
                   (unsigned char) *q);
      svn_stringbuf_appendcstr(outstr, escaped_char);

      p = q + 1;
    }

  return outstr->data;
}

/* Escape only NUL characters from a string that is presumed to
 * be UTF-8 encoded and return a nul-terminated C string. */
static const char *
nul_escape(const svn_string_t *src, apr_pool_t *result_pool)
{
  const char *end = src->data + src->len;
  const char *p = src->data, *q;
  svn_stringbuf_t *outstr;

  for (q = p; q < end; q++)
    {
      if (*q == '\0')
        break;
    }

  if (q == end)
    return src->data;

  outstr = svn_stringbuf_create_empty(result_pool);
  while (1)
    {
      q = p;

      /* Traverse till either unsafe character or eos. */
      while (q < end && *q != '\0')
        q++;

      /* copy chunk before marker */
      svn_stringbuf_appendbytes(outstr, p, q - p);

      if (q == end)
        break;

      svn_stringbuf_appendcstr(outstr, "?\\000");

      p = q + 1;
    }

  return outstr->data;
}


/* Convert an ISO-8859-1 (Latin-1) string to UTF-8.
   ISO-8859-1 is a strict subset of Unicode. */
static svn_error_t *
latin1_to_utf8(const svn_string_t **result, const svn_string_t *src,
               apr_pool_t *result_pool)
{
  apr_int32_t *ucs4buf;
  svn_membuf_t resultbuf;
  apr_size_t length;
  apr_size_t i;
  svn_string_t *res;

  ucs4buf = apr_palloc(result_pool, src->len * sizeof(*ucs4buf));
  for (i = 0; i < src->len; ++i)
    ucs4buf[i] = (unsigned char)(src->data[i]);

  svn_membuf__create(&resultbuf, 2 * src->len, result_pool);
  SVN_ERR(svn_utf__encode_ucs4_string(
              &resultbuf, ucs4buf, src->len, &length));

  res = apr_palloc(result_pool, sizeof(*res));
  res->data = resultbuf.data;
  res->len = length;
  *result = res;
  return SVN_NO_ERROR;
}

/* Make a best effort to convert a X.509 name to a UTF-8 encoded
 * string and return it.  If we can't properly convert just do a
 * fuzzy conversion so we have something to display. */
static const char *
x509name_to_utf8_string(const x509_name *name, apr_pool_t *result_pool)
{
  const svn_string_t *src_string;
  const svn_string_t *utf8_string;
  svn_error_t *err;

  src_string = svn_string_ncreate((const char *)name->val.p,
                                  name->val.len,
                                  result_pool);
  switch (name->val.tag)
    {
    case ASN1_UTF8_STRING:
      if (svn_utf__is_valid(src_string->data, src_string->len))
        return nul_escape(src_string, result_pool);
      else
        /* not a valid UTF-8 string, who knows what it is,
         * so run it through the fuzzy_escape code.  */
        return fuzzy_escape(src_string, result_pool);
      break;

      /* Both BMP and UNIVERSAL should always be in Big Endian (aka
       * network byte order).  But rumor has it that there are certs
       * out there with other endianess and even Byte Order Marks.
       * If we actually run into these, we might need to do something
       * about it. */

    case ASN1_BMP_STRING:
      if (0 != src_string->len % sizeof(apr_uint16_t))
          return fuzzy_escape(src_string, result_pool);
      err = svn_utf__utf16_to_utf8(&utf8_string,
                                   (const void*)(src_string->data),
                                   src_string->len / sizeof(apr_uint16_t),
                                   TRUE, result_pool, result_pool);
      break;

    case ASN1_UNIVERSAL_STRING:
      if (0 != src_string->len % sizeof(apr_int32_t))
          return fuzzy_escape(src_string, result_pool);
      err = svn_utf__utf32_to_utf8(&utf8_string,
                                   (const void*)(src_string->data),
                                   src_string->len / sizeof(apr_int32_t),
                                   TRUE, result_pool, result_pool);
      break;

      /* Despite what all the IETF, ISO, ITU bits say everything out
       * on the Internet that I can find treats this as ISO-8859-1.
       * Even the name is misleading, it's not actually T.61.  All the
       * gory details can be found in the Character Sets section of:
       * https://www.cs.auckland.ac.nz/~pgut001/pubs/x509guide.txt
       */
    case ASN1_T61_STRING:
      err = latin1_to_utf8(&utf8_string, src_string, result_pool);
      break;

      /* This leaves two types out there in the wild.  PrintableString,
       * which is just a subset of ASCII and IA5 which is ASCII (though
       * 0x24 '$' and 0x23 '#' may be defined with differnet symbols
       * depending on the location, in practice it seems everyone just
       * treats it as ASCII).  Since these are just ASCII run through
       * the fuzzy_escape code to deal with anything that isn't actually
       * ASCII.  There shouldn't be any other types here but if we find
       * a cert with some other encoding, the best we can do is the
       * fuzzy_escape().  Note: Technically IA5 isn't valid in this
       * context, however in the real world it may pop up. */
    default:
      return fuzzy_escape(src_string, result_pool);
    }

  if (err)
    {
      svn_error_clear(err);
      return fuzzy_escape(src_string, result_pool);
    }

  return nul_escape(utf8_string, result_pool);
}

static svn_error_t *
x509_name_to_certinfo(apr_array_header_t **result,
                      const x509_name *dn,
                      apr_pool_t *scratch_pool,
                      apr_pool_t *result_pool)
{
  const x509_name *name = dn;

  *result = apr_array_make(result_pool, 6, sizeof(svn_x509_name_attr_t *));

  while (name != NULL) {
    svn_x509_name_attr_t *attr = apr_palloc(result_pool, sizeof(svn_x509_name_attr_t));

    attr->oid_len = name->oid.len;
    attr->oid = apr_pmemdup(result_pool, name->oid.p, attr->oid_len);
    attr->utf8_value = x509name_to_utf8_string(name, result_pool);
    if (!attr->utf8_value)
      /* this should never happen */
      attr->utf8_value = apr_pstrdup(result_pool, "??");
    APR_ARRAY_PUSH(*result, const svn_x509_name_attr_t *) = attr;

    name = name->next;
  }

  return SVN_NO_ERROR;
}

static svn_boolean_t
is_hostname(const char *str)
{
  apr_size_t i, len = strlen(str);

  for (i = 0; i < len; i++)
    {
      char c = str[i];

      /* '-' is only legal when not at the start or end of a label */
      if (c == '-')
        {
          if (i + 1 != len)
            {
              if (str[i + 1] == '.')
                return FALSE; /* '-' preceeds a '.' */
            }
          else
            return FALSE; /* '-' is at end of string */

          /* determine the previous character. */
          if (i == 0)
            return FALSE; /* '-' is at start of string */
          else
            if (str[i - 1] == '.')
              return FALSE; /* '-' follows a '.' */
        }
      else if (c != '*' && c != '.' && !svn_ctype_isalnum(c))
        return FALSE; /* some character not allowed */
    }

  return TRUE;
}

static const char *
x509parse_get_cn(apr_array_header_t *subject)
{
  int i;

  for (i = 0; i < subject->nelts; ++i)
    {
      const svn_x509_name_attr_t *attr = APR_ARRAY_IDX(subject, i, const svn_x509_name_attr_t *);
      if (equal(attr->oid, attr->oid_len,
                SVN_X509_OID_COMMON_NAME, sizeof(SVN_X509_OID_COMMON_NAME) - 1))
        return attr->utf8_value;
    }

  return NULL;
}


static void
x509parse_get_hostnames(svn_x509_certinfo_t *ci, x509_cert *crt,
                        apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  ci->hostnames = NULL;

  if (crt->dnsnames->nelts > 0)
    {
      int i;

      ci->hostnames = apr_array_make(result_pool, crt->dnsnames->nelts,
                                     sizeof(const char*));

      /* Subject Alt Names take priority */
      for (i = 0; i < crt->dnsnames->nelts; i++)
        {
          x509_buf *dnsname = APR_ARRAY_IDX(crt->dnsnames, i, x509_buf *);
          const svn_string_t *temp = svn_string_ncreate((const char *)dnsname->p,
                                                        dnsname->len,
                                                        scratch_pool);

          APR_ARRAY_PUSH(ci->hostnames, const char*)
            = fuzzy_escape(temp, result_pool);
        }
    }
  else
    {
      /* no SAN then get the hostname from the CommonName on the cert */
      const char *utf8_value;

      utf8_value = x509parse_get_cn(ci->subject);

      if (utf8_value && is_hostname(utf8_value))
        {
          ci->hostnames = apr_array_make(result_pool, 1, sizeof(const char*));
          APR_ARRAY_PUSH(ci->hostnames, const char*) = utf8_value;
        }
    }
}

/*
 * Parse one certificate.
 */
svn_error_t *
svn_x509_parse_cert(svn_x509_certinfo_t **certinfo,
                    const char *buf,
                    apr_size_t buflen,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  ptrdiff_t len;
  const unsigned char *p;
  const unsigned char *end;
  x509_cert *crt;
  svn_x509_certinfo_t *ci;

  crt = apr_pcalloc(scratch_pool, sizeof(*crt));
  p = (const unsigned char *)buf;
  len = buflen;
  end = p + len;

  /*
   * Certificate  ::=      SEQUENCE  {
   *              tbsCertificate           TBSCertificate,
   *              signatureAlgorithm       AlgorithmIdentifier,
   *              signatureValue           BIT STRING      }
   */
  err = asn1_get_tag(&p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, err, NULL);

  if (len != (end - p))
    {
      err = svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, NULL, NULL);
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, err, NULL);
    }

  /*
   * TBSCertificate  ::=  SEQUENCE  {
   */
  err = asn1_get_tag(&p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, err, NULL);

  end = p + len;

  /*
   * Version      ::=      INTEGER  {      v1(0), v2(1), v3(2)  }
   *
   * CertificateSerialNumber      ::=      INTEGER
   *
   * signature                    AlgorithmIdentifier
   */
  SVN_ERR(x509_get_version(&p, end, &crt->version));
  SVN_ERR(x509_get_serial(&p, end, &crt->serial));
  SVN_ERR(x509_get_alg(&p, end, &crt->sig_oid1));

  crt->version++;

  if (crt->version > 3)
    return svn_error_create(SVN_ERR_X509_CERT_UNKNOWN_VERSION, NULL, NULL);

  /*
   * issuer                               Name
   */
  err = asn1_get_tag(&p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, err, NULL);

  SVN_ERR(x509_get_name(&p, p + len, &crt->issuer, scratch_pool));

  /*
   * Validity ::= SEQUENCE {
   *              notBefore          Time,
   *              notAfter           Time }
   *
   */
  SVN_ERR(x509_get_dates(&crt->valid_from, &crt->valid_to, &p, end,
                         scratch_pool));

  /*
   * subject                              Name
   */
  err = asn1_get_tag(&p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, err, NULL);

  SVN_ERR(x509_get_name(&p, p + len, &crt->subject, scratch_pool));

  /*
   * SubjectPublicKeyInfo  ::=  SEQUENCE
   *              algorithm                        AlgorithmIdentifier,
   *              subjectPublicKey         BIT STRING      }
   */
  err = asn1_get_tag(&p, end, &len, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
  if (err)
    return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, err, NULL);

  /* Skip pubkey. */
  p += len;

  /*
   *      issuerUniqueID  [1]      IMPLICIT UniqueIdentifier OPTIONAL,
   *                                               -- If present, version shall be v2 or v3
   *      subjectUniqueID [2]      IMPLICIT UniqueIdentifier OPTIONAL,
   *                                               -- If present, version shall be v2 or v3
   *      extensions              [3]      EXPLICIT Extensions OPTIONAL
   *                                               -- If present, version shall be v3
   */
  crt->dnsnames = apr_array_make(scratch_pool, 3, sizeof(x509_buf *));

  /* Try to parse issuerUniqueID, subjectUniqueID and extensions for *every*
   * version (X.509 v1, v2 and v3), not just v2 or v3.  If they aren't present,
   * we are fine, but we don't want to throw an error if they are.  v1 and v2
   * certificates with the corresponding extra fields are ill-formed per RFC
   * 5280 s. 4.1, but we suspect they could exist in the real world.  Other
   * X.509 parsers (e.g., within OpenSSL or Microsoft CryptoAPI) aren't picky
   * about these certificates, and we also allow them. */
  SVN_ERR(x509_get_uid(&p, end, &crt->issuer_id, 1));
  SVN_ERR(x509_get_uid(&p, end, &crt->subject_id, 2));
  SVN_ERR(x509_get_ext(crt->dnsnames, &p, end));

  if (p != end)
    {
      err = svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, NULL, NULL);
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, err, NULL);
    }

  end = (const unsigned char*) buf + buflen;

  /*
   *      signatureAlgorithm       AlgorithmIdentifier,
   *      signatureValue           BIT STRING
   */
  SVN_ERR(x509_get_alg(&p, end, &crt->sig_oid2));

  if (!oids_equal(&crt->sig_oid1, &crt->sig_oid2))
    return svn_error_create(SVN_ERR_X509_CERT_SIG_MISMATCH, NULL, NULL);

  SVN_ERR(x509_get_sig(&p, end, &crt->sig));

  if (p != end)
    {
      err = svn_error_create(SVN_ERR_ASN1_LENGTH_MISMATCH, NULL, NULL);
      return svn_error_create(SVN_ERR_X509_CERT_INVALID_FORMAT, err, NULL);
    }

  ci = apr_pcalloc(result_pool, sizeof(*ci));

  /* Get the subject name */
  SVN_ERR(x509_name_to_certinfo(&ci->subject, &crt->subject,
                                scratch_pool, result_pool));

  /* Get the issuer name */
  SVN_ERR(x509_name_to_certinfo(&ci->issuer, &crt->issuer,
                                scratch_pool, result_pool));

  /* Copy the validity range */
  ci->valid_from = crt->valid_from;
  ci->valid_to = crt->valid_to;

  /* Calculate the SHA1 digest of the certificate, otherwise known as
    the fingerprint */
  SVN_ERR(svn_checksum(&ci->digest, svn_checksum_sha1, buf, buflen,
                       result_pool));

  /* Construct the array of host names */
  x509parse_get_hostnames(ci, crt, result_pool, scratch_pool);

  *certinfo = ci;
  return SVN_NO_ERROR;
}

