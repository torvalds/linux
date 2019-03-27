/*
 * md5.c:   checksum routines
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


#include <apr_md5.h>

#include "svn_checksum.h"
#include "svn_md5.h"
#include "checksum.h"



/* These are all deprecated, and just wrap the internal functions defined
   above. */
const unsigned char *
svn_md5_empty_string_digest(void)
{
  return svn__empty_string_digest(svn_checksum_md5);
}

const char *
svn_md5_digest_to_cstring_display(const unsigned char digest[],
                                  apr_pool_t *pool)
{
  return svn__digest_to_cstring_display(digest, APR_MD5_DIGESTSIZE, pool);
}

const char *
svn_md5_digest_to_cstring(const unsigned char digest[], apr_pool_t *pool)
{
  return svn__digest_to_cstring(digest, APR_MD5_DIGESTSIZE, pool);
}

svn_boolean_t
svn_md5_digests_match(const unsigned char d1[], const unsigned char d2[])
{
  return svn__digests_match(d1, d2, APR_MD5_DIGESTSIZE);
}
