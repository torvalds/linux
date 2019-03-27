/* svn_token.h : value/string-token functions
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

#ifndef SVN_TOKEN_H
#define SVN_TOKEN_H


#include "svn_error.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** A mapping between a string STR and an enumeration value VAL.
 *
 * Maps are an array of these, terminated with a struct where STR == NULL.
 */
typedef struct svn_token_map_t
{
  const char *str;
  int val;
} svn_token_map_t;


/* A value used by some token functions to indicate an unrecognized token.  */
#define SVN_TOKEN_UNKNOWN (-9999)


/* Return the string form of the given VALUE as found in MAP. If the value
   is not recognized, then a MALFUNCTION will occur.  */
const char *
svn_token__to_word(const svn_token_map_t *map,
                   int value);


/* NOTE: in the following functions, if WORD is NULL, then SVN_TOKEN_UNKNOWN
   will be returned, or will cause the appropriate MALFUNCTION or ERROR.  */

/* Return the integer value of the given token WORD, as found in MAP. If the
   string is not recognized, then a MALFUNCTION will occur.

   Note: this function is for persisted string values. Because this function
   will throw a MALFUNCTION, it should not be used for network input or
   user input.  */
int
svn_token__from_word_strict(const svn_token_map_t *map,
                            const char *word);


/* Store the integer value of WORD into *VALUE. If the string is not
   recognized, then SVN_ERR_BAD_TOKEN is returned.  */
svn_error_t *
svn_token__from_word_err(int *value,
                         const svn_token_map_t *map,
                         const char *word);


/* Return the integer value of the given token WORD as found in MAP. If the
   string is not recognized, then SVN_TOKEN_UNKNOWN will be returned.  */
int
svn_token__from_word(const svn_token_map_t *map,
                     const char *word);


/* Return the integer value of the given token WORD/LEN as found in MAP. If
   the string is not recognized, then SVN_TOKEN_UNKNOWN will be returned.  */
int
svn_token__from_mem(const svn_token_map_t *map,
                    const char *word,
                    apr_size_t len);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_TOKEN_H */
