/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * The apr_vsnprintf/apr_snprintf functions are based on, and used with the
 * permission of, the  SIO stdio-replacement strx_* functions by Panos
 * Tsirigotis <panos@alumni.cs.colorado.edu> for xinetd.
 */

/**
 * @file apr_base64.h
 * @brief APR-UTIL Base64 Encoding
 */
#ifndef APR_BASE64_H
#define APR_BASE64_H

#include "apu.h"
#include "apr_general.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup APR_Util_Base64 Base64 Encoding
 * @ingroup APR_Util
 * @{
 */

/* Simple BASE64 encode/decode functions.
 * 
 * As we might encode binary strings, hence we require the length of
 * the incoming plain source. And return the length of what we decoded.
 *
 * The decoding function takes any non valid char (i.e. whitespace, \0
 * or anything non A-Z,0-9 etc as terminal.
 * 
 * plain strings/binary sequences are not assumed '\0' terminated. Encoded
 * strings are neither. But probably should.
 *
 */

/**
 * Given the length of an un-encoded string, get the length of the
 * encoded string.
 * @param len the length of an unencoded string.
 * @return the length of the string after it is encoded, including the
 * trailing \0
 */ 
APU_DECLARE(int) apr_base64_encode_len(int len);

/**
 * Encode a text string using base64encoding.
 * @param coded_dst The destination string for the encoded string.
 * @param plain_src The original string in plain text
 * @param len_plain_src The length of the plain text string
 * @return the length of the encoded string
 */ 
APU_DECLARE(int) apr_base64_encode(char * coded_dst, const char *plain_src, 
                                 int len_plain_src);

/**
 * Encode an EBCDIC string using base64encoding.
 * @param coded_dst The destination string for the encoded string.
 * @param plain_src The original string in plain text
 * @param len_plain_src The length of the plain text string
 * @return the length of the encoded string
 */ 
APU_DECLARE(int) apr_base64_encode_binary(char * coded_dst, 
                                        const unsigned char *plain_src,
                                        int len_plain_src);

/**
 * Determine the maximum buffer length required to decode the plain text
 * string given the encoded string.
 * @param coded_src The encoded string
 * @return the maximum required buffer length for the plain text string
 */ 
APU_DECLARE(int) apr_base64_decode_len(const char * coded_src);

/**
 * Decode a string to plain text
 * @param plain_dst The destination string for the plain text
 * @param coded_src The encoded string 
 * @return the length of the plain text string
 */ 
APU_DECLARE(int) apr_base64_decode(char * plain_dst, const char *coded_src);

/**
 * Decode an EBCDIC string to plain text
 * @param plain_dst The destination string for the plain text
 * @param coded_src The encoded string 
 * @return the length of the plain text string
 */ 
APU_DECLARE(int) apr_base64_decode_binary(unsigned char * plain_dst, 
                                        const char *coded_src);

/** @} */
#ifdef __cplusplus
}
#endif

#endif	/* !APR_BASE64_H */
