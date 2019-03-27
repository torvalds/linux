/* key-gen.c --- manufacturing sequential keys for some db tables
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

#ifndef SVN_LIBSVN_FS_KEY_GEN_H
#define SVN_LIBSVN_FS_KEY_GEN_H

#include <apr.h>

#include "svn_types.h"
#include "private/svn_skel.h"  /* ### for svn_fs_base__{get,put}size() */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* The alphanumeric keys passed in and out of svn_fs_base__next_key
   are guaranteed never to be longer than this many bytes,
   *including* the trailing null byte.  It is therefore safe
   to declare a key as "char key[MAX_KEY_SIZE]".

   Note that this limit will be a problem if the number of
   keys in a table ever exceeds

       18217977168218728251394687124089371267338971528174
       76066745969754933395997209053270030282678007662838
       67331479599455916367452421574456059646801054954062
       15017704234999886990788594743994796171248406730973
       80736524850563115569208508785942830080999927310762
       50733948404739350551934565743979678824151197232629
       947748581376,

   but that's a risk we'll live with for now. */
#define MAX_KEY_SIZE 200

/* In many of the databases, the value at this key is the key to use
   when storing a new record. */
#define NEXT_KEY_KEY "next-key"


/* Generate the next key after a given alphanumeric key.
 *
 * The first *LEN bytes of THIS are an ascii representation of a
 * number in base 36: digits 0-9 have their usual values, and a-z have
 * values 10-35.
 *
 * The new key is stored in NEXT, null-terminated.  NEXT must be at
 * least *LEN + 2 bytes long -- one extra byte to hold a possible
 * overflow column, and one for null termination.  On return, *LEN
 * will be set to the length of the new key, not counting the null
 * terminator.  In other words, the outgoing *LEN will be either equal
 * to the incoming, or to the incoming + 1.
 *
 * If THIS contains anything other than digits and lower-case
 * alphabetic characters, or if it starts with `0' but is not the
 * string "0", then *LEN is set to zero and the effect on NEXT
 * is undefined.
 */
void svn_fs_base__next_key(const char *this, apr_size_t *len, char *next);


/* Compare two strings A and B as base-36 alphanumber keys.
 *
 * Return TRUE iff both keys are NULL or both keys have the same
 * contents.
 */
svn_boolean_t svn_fs_base__same_keys(const char *a, const char *b);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_KEY_GEN_H */
