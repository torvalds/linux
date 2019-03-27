/*
 * validate.c:  validation routines
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

/* ==================================================================== */



/*** Includes. ***/

#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_error.h"
#include "svn_ctype.h"
#include "svn_private_config.h"



/*** Code. ***/

svn_error_t *
svn_mime_type_validate(const char *mime_type, apr_pool_t *pool)
{
  /* Since svn:mime-type can actually contain a full content type
     specification, e.g., "text/html; charset=UTF-8", make sure we're
     only looking at the media type here. */
  const apr_size_t len = strcspn(mime_type, "; ");
  const apr_size_t len2 = strlen(mime_type);
  const char *const slash_pos = strchr(mime_type, '/');
  apr_size_t i;
  const char *tspecials = "()<>@,;:\\\"/[]?=";

  if (len == 0)
    return svn_error_createf
      (SVN_ERR_BAD_MIME_TYPE, NULL,
       _("MIME type '%s' has empty media type"), mime_type);

  if (slash_pos == NULL || slash_pos >= &mime_type[len])
    return svn_error_createf
      (SVN_ERR_BAD_MIME_TYPE, NULL,
       _("MIME type '%s' does not contain '/'"), mime_type);

  /* Check the mime type for illegal characters. See RFC 1521. */
  for (i = 0; i < len; i++)
    {
      if (&mime_type[i] != slash_pos
         && (! svn_ctype_isascii(mime_type[i])
            || svn_ctype_iscntrl(mime_type[i])
            || svn_ctype_isspace(mime_type[i])
            || (strchr(tspecials, mime_type[i]) != NULL)))
        return svn_error_createf
          (SVN_ERR_BAD_MIME_TYPE, NULL,
           _("MIME type '%s' contains invalid character '%c' "
             "in media type"),
           mime_type, mime_type[i]);
    }

  /* Check the whole string for unsafe characters. (issue #2872) */
  for (i = 0; i < len2; i++)
    {
      if (svn_ctype_iscntrl(mime_type[i]) && mime_type[i] != '\t')
        return svn_error_createf(
           SVN_ERR_BAD_MIME_TYPE, NULL,
           _("MIME type '%s' contains invalid character '0x%02x' "
             "in postfix"),
           mime_type, mime_type[i]);
    }

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_mime_type_is_binary(const char *mime_type)
{
  /* See comment in svn_mime_type_validate() above. */
  const apr_size_t len = strcspn(mime_type, "; ");
  return ((strncmp(mime_type, "text/", 5) != 0)
          && (len != 15 || strncmp(mime_type, "image/x-xbitmap", len) != 0)
          && (len != 15 || strncmp(mime_type, "image/x-xpixmap", len) != 0)
          );
}
