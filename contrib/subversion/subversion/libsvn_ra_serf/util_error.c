/*
 * util_error.c : serf utility routines for wrapping serf status codes
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
#include <serf.h>

#include "svn_utf.h"
#include "private/svn_error_private.h"

#include "ra_serf.h"

/*
 * Undefine the helpers for creating errors.
 *
 * *NOTE*: Any use of these functions in any other function may need
 * to call svn_error__locate() because the macro that would otherwise
 * do this is being undefined and the filename and line number will
 * not be properly set in the static error_file and error_line
 * variables.
 */
#undef svn_error_create
#undef svn_error_createf
#undef svn_error_quick_wrap
#undef svn_error_wrap_apr
#undef svn_ra_serf__wrap_err

svn_error_t *
svn_ra_serf__wrap_err(apr_status_t status,
                      const char *fmt,
                      ...)
{
  const char *serf_err_msg = serf_error_string(status);
  svn_error_t *err;
  va_list ap;

  err = svn_error_create(status, NULL, NULL);

  if (serf_err_msg || fmt)
    {
      const char *msg;
      const char *err_msg;
      char errbuf[255]; /* Buffer for APR error message. */

      if (serf_err_msg)
        {
          err_msg = serf_err_msg;
        }
      else
        {
          svn_error_t *utf8_err;

          /* Grab the APR error message. */
          apr_strerror(status, errbuf, sizeof(errbuf));
          utf8_err = svn_utf_cstring_to_utf8(&err_msg, errbuf, err->pool);
          if (utf8_err)
            err_msg = NULL;
          svn_error_clear(utf8_err);
        }

      /* Append it to the formatted message. */
      if (fmt)
        {
          va_start(ap, fmt);
          msg = apr_pvsprintf(err->pool, fmt, ap);
          va_end(ap);
        }
      else
        {
          msg = "ra_serf";
        }
      if (err_msg)
        {
          err->message = apr_pstrcat(err->pool, msg, ": ", err_msg,
                                     SVN_VA_NULL);
        }
      else
        {
          err->message = msg;
        }
    }

  return err;
}
