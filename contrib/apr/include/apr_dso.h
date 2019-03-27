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
 */

#ifndef APR_DSO_DOT_H
#define APR_DSO_DOT_H

/**
 * @file apr_dso.h
 * @brief APR Dynamic Object Handling Routines
 */

#include "apr.h"
#include "apr_pools.h"
#include "apr_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup apr_dso Dynamic Object Handling
 * @ingroup APR 
 * @{
 */

#if APR_HAS_DSO || defined(DOXYGEN)

/**
 * Structure for referencing dynamic objects
 */
typedef struct apr_dso_handle_t       apr_dso_handle_t;

/**
 * Structure for referencing symbols from dynamic objects
 */
typedef void *                        apr_dso_handle_sym_t;

/**
 * Load a DSO library.
 * @param res_handle Location to store new handle for the DSO.
 * @param path Path to the DSO library
 * @param ctx Pool to use.
 * @bug We aught to provide an alternative to RTLD_GLOBAL, which
 * is the only supported method of loading DSOs today.
 */
APR_DECLARE(apr_status_t) apr_dso_load(apr_dso_handle_t **res_handle, 
                                       const char *path, apr_pool_t *ctx);

/**
 * Close a DSO library.
 * @param handle handle to close.
 */
APR_DECLARE(apr_status_t) apr_dso_unload(apr_dso_handle_t *handle);

/**
 * Load a symbol from a DSO handle.
 * @param ressym Location to store the loaded symbol
 * @param handle handle to load the symbol from.
 * @param symname Name of the symbol to load.
 */
APR_DECLARE(apr_status_t) apr_dso_sym(apr_dso_handle_sym_t *ressym, 
                                      apr_dso_handle_t *handle,
                                      const char *symname);

/**
 * Report more information when a DSO function fails.
 * @param dso The dso handle that has been opened
 * @param buf Location to store the dso error
 * @param bufsize The size of the provided buffer
 */
APR_DECLARE(const char *) apr_dso_error(apr_dso_handle_t *dso, char *buf, apr_size_t bufsize);

#endif /* APR_HAS_DSO */

/** @} */

#ifdef __cplusplus
}
#endif

#endif
