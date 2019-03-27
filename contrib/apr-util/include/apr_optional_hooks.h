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
/**
 * @file apr_optional_hooks.h
 * @brief Apache optional hook functions
 */


#ifndef APR_OPTIONAL_HOOK_H
#define APR_OPTIONAL_HOOK_H

#include "apr_tables.h"

#ifdef __cplusplus
extern "C" {
#endif
/** 
 * @defgroup APR_Util_OPT_HOOK Optional Hook Functions
 * @ingroup APR_Util_Hook
 * @{
 */
/**
 * Function to implement the APR_OPTIONAL_HOOK Macro
 * @internal
 * @see APR_OPTIONAL_HOOK
 *
 * @param szName The name of the hook
 * @param pfn A pointer to a function that will be called
 * @param aszPre a NULL-terminated array of strings that name modules whose hooks should precede this one
 * @param aszSucc a NULL-terminated array of strings that name modules whose hooks should succeed this one
 * @param nOrder an integer determining order before honouring aszPre and aszSucc (for example HOOK_MIDDLE)
 */


APU_DECLARE(void) apr_optional_hook_add(const char *szName,void (*pfn)(void),
					const char * const *aszPre,
					const char * const *aszSucc,
					int nOrder);

/**
 * Hook to an optional hook.
 *
 * @param ns The namespace prefix of the hook functions
 * @param name The name of the hook
 * @param pfn A pointer to a function that will be called
 * @param aszPre a NULL-terminated array of strings that name modules whose hooks should precede this one
 * @param aszSucc a NULL-terminated array of strings that name modules whose hooks should succeed this one
 * @param nOrder an integer determining order before honouring aszPre and aszSucc (for example HOOK_MIDDLE)
 */

#define APR_OPTIONAL_HOOK(ns,name,pfn,aszPre,aszSucc,nOrder) do { \
  ns##_HOOK_##name##_t *apu__hook = pfn; \
  apr_optional_hook_add(#name,(void (*)(void))apu__hook,aszPre, aszSucc, nOrder); \
} while (0)

/**
 * @internal
 * @param szName - the name of the function
 * @return the hook structure for a given hook
 */
APU_DECLARE(apr_array_header_t *) apr_optional_hook_get(const char *szName);

/**
 * Implement an optional hook that runs until one of the functions
 * returns something other than OK or DECLINE.
 *
 * @param ns The namespace prefix of the hook functions
 * @param link The linkage declaration prefix of the hook
 * @param ret The type of the return value of the hook
 * @param ret The type of the return value of the hook
 * @param name The name of the hook
 * @param args_decl The declaration of the arguments for the hook
 * @param args_use The names for the arguments for the hook
 * @param ok Success value
 * @param decline Decline value
 */
#define APR_IMPLEMENT_OPTIONAL_HOOK_RUN_ALL(ns,link,ret,name,args_decl,args_use,ok,decline) \
link##_DECLARE(ret) ns##_run_##name args_decl \
    { \
    ns##_LINK_##name##_t *pHook; \
    int n; \
    ret rv; \
    apr_array_header_t *pHookArray=apr_optional_hook_get(#name); \
\
    if(!pHookArray) \
	return ok; \
\
    pHook=(ns##_LINK_##name##_t *)pHookArray->elts; \
    for(n=0 ; n < pHookArray->nelts ; ++n) \
	{ \
	rv=(pHook[n].pFunc)args_use; \
\
	if(rv != ok && rv != decline) \
	    return rv; \
	} \
    return ok; \
    }

/** @} */
#ifdef __cplusplus
}
#endif

#endif /* APR_OPTIONAL_HOOK_H */
