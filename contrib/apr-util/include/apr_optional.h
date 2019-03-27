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

#ifndef APR_OPTIONAL_H
#define APR_OPTIONAL_H

#include "apu.h"
/** 
 * @file apr_optional.h
 * @brief APR-UTIL registration of functions exported by modules
 */
#ifdef __cplusplus
extern "C" {
#endif

/** 
 * @defgroup APR_Util_Opt Optional Functions
 * @ingroup APR_Util
 *
 * Typesafe registration and retrieval of functions that may not be present
 * (i.e. functions exported by optional modules)
 * @{
 */

/**
 * The type of an optional function.
 * @param name The name of the function
 */
#define APR_OPTIONAL_FN_TYPE(name) apr_OFN_##name##_t

/**
 * Declare an optional function.
 * @param ret The return type of the function
 * @param name The name of the function
 * @param args The function arguments (including brackets)
 */
#define APR_DECLARE_OPTIONAL_FN(ret,name,args) \
typedef ret (APR_OPTIONAL_FN_TYPE(name)) args

/**
 * XXX: This doesn't belong here, then!
 * Private function! DO NOT USE! 
 * @internal
 */

typedef void (apr_opt_fn_t)(void);
/** @internal */
APU_DECLARE_NONSTD(void) apr_dynamic_fn_register(const char *szName, 
                                                  apr_opt_fn_t *pfn);

/**
 * Register an optional function. This can be later retrieved, type-safely, by
 * name. Like all global functions, the name must be unique. Note that,
 * confusingly but correctly, the function itself can be static!
 * @param name The name of the function
 */
#define APR_REGISTER_OPTIONAL_FN(name) do { \
  APR_OPTIONAL_FN_TYPE(name) *apu__opt = name; \
  apr_dynamic_fn_register(#name,(apr_opt_fn_t *)apu__opt); \
} while (0)

/** @internal
 * Private function! DO NOT USE! 
 */
APU_DECLARE(apr_opt_fn_t *) apr_dynamic_fn_retrieve(const char *szName);

/**
 * Retrieve an optional function. Returns NULL if the function is not present.
 * @param name The name of the function
 */
#define APR_RETRIEVE_OPTIONAL_FN(name) \
	(APR_OPTIONAL_FN_TYPE(name) *)apr_dynamic_fn_retrieve(#name)

/** @} */
#ifdef __cplusplus
}
#endif

#endif /* APR_OPTIONAL_H */
