/*
 * token.c :  value/string-token functions
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

#include "svn_types.h"
#include "svn_error.h"

#include "private/svn_token.h"
#include "svn_private_config.h"


const char *
svn_token__to_word(const svn_token_map_t *map,
                   int value)
{
  for (; map->str != NULL; ++map)
    if (map->val == value)
      return map->str;

  /* Internal, numeric values should always be found.  */
  SVN_ERR_MALFUNCTION_NO_RETURN();
}


int
svn_token__from_word_strict(const svn_token_map_t *map,
                            const char *word)
{
  int value = svn_token__from_word(map, word);

  if (value == SVN_TOKEN_UNKNOWN)
    SVN_ERR_MALFUNCTION_NO_RETURN();

  return value;
}


svn_error_t *
svn_token__from_word_err(int *value,
                         const svn_token_map_t *map,
                         const char *word)
{
  *value = svn_token__from_word(map, word);

  if (*value == SVN_TOKEN_UNKNOWN)
    return svn_error_createf(SVN_ERR_BAD_TOKEN, NULL,
                             _("Token '%s' is unrecognized"),
                             word);

  return SVN_NO_ERROR;
}


int
svn_token__from_word(const svn_token_map_t *map,
                     const char *word)
{
  if (word == NULL)
    return SVN_TOKEN_UNKNOWN;

  for (; map->str != NULL; ++map)
    if (strcmp(map->str, word) == 0)
      return map->val;

  return SVN_TOKEN_UNKNOWN;
}


int
svn_token__from_mem(const svn_token_map_t *map,
                    const char *word,
                    apr_size_t len)
{
  for (; map->str != NULL; ++map)
    if (strncmp(map->str, word, len) == 0 && map->str[len] == '\0')
      return map->val;

  return SVN_TOKEN_UNKNOWN;
}
