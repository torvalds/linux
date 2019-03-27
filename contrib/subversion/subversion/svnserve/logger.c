/*
 * logger.c : Implementation of the SvnServe logger API
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



#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_error.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_time.h"

#include "private/svn_mutex.h"

#include "svn_private_config.h"
#include "logger.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>   /* For getpid() */
#endif

struct logger_t
{
  /* actual log file / stream object */
  svn_stream_t *stream;

  /* mutex used to serialize access to this structure */
  svn_mutex__t *mutex;

  /* private pool used for temporary allocations */
  apr_pool_t *pool;
};

svn_error_t *
logger__create_for_stderr(logger_t **logger,
                          apr_pool_t *pool)
{
  logger_t *result = apr_pcalloc(pool, sizeof(*result));
  result->pool = svn_pool_create(pool);

  SVN_ERR(svn_stream_for_stderr(&result->stream, pool));
  SVN_ERR(svn_mutex__init(&result->mutex, TRUE, pool));

  *logger = result;

  return SVN_NO_ERROR;
}

svn_error_t *
logger__create(logger_t **logger,
               const char *filename,
               apr_pool_t *pool)
{
  logger_t *result = apr_pcalloc(pool, sizeof(*result));
  apr_file_t *file;

  SVN_ERR(svn_io_file_open(&file, filename,
                           APR_WRITE | APR_CREATE | APR_APPEND,
                           APR_OS_DEFAULT, pool));
  SVN_ERR(svn_mutex__init(&result->mutex, TRUE, pool));

  result->stream = svn_stream_from_aprfile2(file, FALSE,  pool);
  result->pool = svn_pool_create(pool);

  *logger = result;

  return SVN_NO_ERROR;
}

void
logger__log_error(logger_t *logger,
                  svn_error_t *err,
                  repository_t *repository,
                  client_info_t *client_info)
{
  if (logger && err)
    {
      const char *timestr, *continuation;
      const char *user, *repos, *remote_host;
      char errbuf[256];
      /* 8192 from MAX_STRING_LEN in from httpd-2.2.4/include/httpd.h */
      char errstr[8192];

      svn_error_clear(svn_mutex__lock(logger->mutex));

      timestr = svn_time_to_cstring(apr_time_now(), logger->pool);
      remote_host = client_info && client_info->remote_host
                  ? client_info->remote_host
                  : "-";
      user = client_info && client_info->user
           ? client_info->user
           : "-";
      repos = repository && repository->repos_name
            ? repository->repos_name
             : "-";

      continuation = "";
      while (err)
        {
          const char *message = svn_err_best_message(err, errbuf, sizeof(errbuf));
          /* based on httpd-2.2.4/server/log.c:log_error_core */
          apr_size_t len = apr_snprintf(errstr, sizeof(errstr),
                                        "%" APR_PID_T_FMT
                                        " %s %s %s %s ERR%s %s %ld %d ",
                                        getpid(), timestr, remote_host, user,
                                        repos, continuation,
                                        err->file ? err->file : "-", err->line,
                                        err->apr_err);

          len += escape_errorlog_item(errstr + len, message,
                                      sizeof(errstr) - len);
          /* Truncate for the terminator (as apr_snprintf does) */
          if (len > sizeof(errstr) - sizeof(APR_EOL_STR)) {
            len = sizeof(errstr) - sizeof(APR_EOL_STR);
          }

          memcpy(errstr + len, APR_EOL_STR, sizeof(APR_EOL_STR));
          len += sizeof(APR_EOL_STR) -1;  /* add NL, ex terminating NUL */

          svn_error_clear(svn_stream_write(logger->stream, errstr, &len));

          continuation = "-";
          err = err->child;
        }

      svn_pool_clear(logger->pool);

      svn_error_clear(svn_mutex__unlock(logger->mutex, SVN_NO_ERROR));
    }
}

svn_error_t *
logger__write(logger_t *logger,
              const char *errstr,
              apr_size_t len)
{
  SVN_MUTEX__WITH_LOCK(logger->mutex,
                       svn_stream_write(logger->stream, errstr, &len));
  return SVN_NO_ERROR;
}
