/*
 * logger.h : Public definitions for the Repository Cache
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

#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "server.h"



/* Opaque svnserve log file writer data structure.  Access to the log
 * file will be serialized among threads within the same process.
 */
typedef struct logger_t logger_t;

/* In POOL, create a writer object that will write log messages to stderr
 * and return it in *LOGGER.  The log file will not add any buffering
 * on top of stderr.
 */
svn_error_t *
logger__create_for_stderr(logger_t **logger,
                          apr_pool_t *pool);

/* In POOL, create a writer object for log file FILENAME and return it
 * in *LOGGER.  The log file will be flushed & closed when POOL gets
 * cleared or destroyed.
 */
svn_error_t *
logger__create(logger_t **logger,
               const char *filename,
               apr_pool_t *pool);

/* Write the first LEN bytes from ERRSTR to the log file managed by LOGGER.
 */
svn_error_t *
logger__write(logger_t *logger,
              const char *errstr,
              apr_size_t len);

/* Write a description of ERR with additional information from REPOSITORY
 * and CLIENT_INFO to the log file managed by LOGGER.  REPOSITORY as well
 * as CLIENT_INFO may be NULL.  If either ERR or LOGGER are NULL, this
 * becomes a no-op.
 */
void
logger__log_error(logger_t *logger,
                  svn_error_t *err,
                  repository_t *repository,
                  client_info_t *client_info);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LOGGER_H */
