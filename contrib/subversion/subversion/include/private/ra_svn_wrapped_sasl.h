/*
 * svn_wrapped_sasl.h :  wrapped SASL API
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

#ifndef RA_SVN_WRAPPED_SASL_H
#define RA_SVN_WRAPPED_SASL_H

#include <stddef.h>

#ifdef WIN32
#  define APR_WANT_IOVEC
#  include <apr_want.h>
  /* This prevents sasl.h from redefining iovec,
     which is always defined by APR on win32. */
#  define STRUCT_IOVEC_DEFINED
#  include <sasl.h>
#else
#  include <sasl/sasl.h>
#endif

/* Apple deprecated the SASL API on Mac OS X 10.11, causing a
   moderately huge number of deprecation warnings to be emitted during
   compilation. Consequently, we wrap the parts of the SASL API that
   we use in a set of private functions and disable the deprecation
   warnings for this header and the implementation file. */
#ifdef __APPLE__
#  if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 2)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  endif
#endif /* __APPLE__ */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void
svn_sasl__set_mutex(sasl_mutex_alloc_t *, sasl_mutex_lock_t *,
                    sasl_mutex_unlock_t *, sasl_mutex_free_t *);

void
svn_sasl__done(void);

void
svn_sasl__dispose(sasl_conn_t **);

const char *
svn_sasl__errstring(int, const char *, const char **);

const char *
svn_sasl__errdetail(sasl_conn_t *);

int
svn_sasl__getprop(sasl_conn_t *, int, const void **);

int
svn_sasl__setprop(sasl_conn_t *, int, const void *);

int
svn_sasl__client_init(const sasl_callback_t *);

int
svn_sasl__client_new(const char *, const char *, const char *, const char *,
                     const sasl_callback_t *, unsigned, sasl_conn_t **);

int
svn_sasl__client_start(sasl_conn_t *, const char *, sasl_interact_t **,
                       const char **, unsigned *, const char **);

int
svn_sasl__client_step(sasl_conn_t *, const char *, unsigned,
                      sasl_interact_t **, const char **, unsigned *);

int
svn_sasl__server_init(const sasl_callback_t *, const char *);

int
svn_sasl__server_new(const char *, const char *, const char *,
                     const char *, const char *, const sasl_callback_t *,
                     unsigned, sasl_conn_t **);

int
svn_sasl__listmech(sasl_conn_t *, const char *, const char *, const char *,
                   const char *, const char **, unsigned *, int *);

int
svn_sasl__server_start(sasl_conn_t *, const char *, const char *, unsigned,
                       const char **, unsigned *);

int
svn_sasl__server_step(sasl_conn_t *, const char *, unsigned,
                      const char **, unsigned *);

int
svn_sasl__encode(sasl_conn_t *, const char *, unsigned,
                 const char **, unsigned *);

int
svn_sasl__decode(sasl_conn_t *, const char *, unsigned,
                 const char **, unsigned *);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#ifdef __APPLE__
#  if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 2)
#    pragma GCC diagnostic pop
#  endif
#endif /* __APPLE__ */

#endif /* RA_SVN_WRAPPED_SASL_H */
