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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "apr_pools.h"
#include "apr_tables.h"
#include "apr.h"
#include "apr_hooks.h"
#include "apr_hash.h"
#include "apr_optional_hooks.h"
#include "apr_optional.h"
#define APR_WANT_MEMFUNC
#define APR_WANT_STRFUNC
#include "apr_want.h"

#if 0
#define apr_palloc(pool,size)   malloc(size)
#endif

APU_DECLARE_DATA apr_pool_t *apr_hook_global_pool = NULL;
APU_DECLARE_DATA int apr_hook_debug_enabled = 0;
APU_DECLARE_DATA const char *apr_hook_debug_current = NULL;

/** @deprecated @see apr_hook_global_pool */
APU_DECLARE_DATA apr_pool_t *apr_global_hook_pool = NULL;

/** @deprecated @see apr_hook_debug_enabled */
APU_DECLARE_DATA int apr_debug_module_hooks = 0;

/** @deprecated @see apr_hook_debug_current */
APU_DECLARE_DATA const char *apr_current_hooking_module = NULL;

/* NB: This must echo the LINK_##name structure */
typedef struct
{
    void (*dummy)(void *);
    const char *szName;
    const char * const *aszPredecessors;
    const char * const *aszSuccessors;
    int nOrder;
} TSortData;

typedef struct tsort_
{
    void *pData;
    int nPredecessors;
    struct tsort_ **ppPredecessors;
    struct tsort_ *pNext;
} TSort;

#ifdef NETWARE
#include "apr_private.h"
#define get_apd                 APP_DATA* apd = (APP_DATA*)get_app_data(gLibId);
#define s_aHooksToSort          ((apr_array_header_t *)(apd->gs_aHooksToSort))
#define s_phOptionalHooks       ((apr_hash_t *)(apd->gs_phOptionalHooks))
#define s_phOptionalFunctions   ((apr_hash_t *)(apd->gs_phOptionalFunctions))
#endif

static int crude_order(const void *a_,const void *b_)
{
    const TSortData *a=a_;
    const TSortData *b=b_;

    return a->nOrder-b->nOrder;
}

static TSort *prepare(apr_pool_t *p,TSortData *pItems,int nItems)
{
    TSort *pData=apr_palloc(p,nItems*sizeof *pData);
    int n;

    qsort(pItems,nItems,sizeof *pItems,crude_order);
    for(n=0 ; n < nItems ; ++n) {
        pData[n].nPredecessors=0;
        pData[n].ppPredecessors=apr_pcalloc(p,nItems*sizeof *pData[n].ppPredecessors);
        pData[n].pNext=NULL;
        pData[n].pData=&pItems[n];
    }

    for(n=0 ; n < nItems ; ++n) {
        int i,k;

        for(i=0 ; pItems[n].aszPredecessors && pItems[n].aszPredecessors[i] ; ++i)
            for(k=0 ; k < nItems ; ++k)
                if(!strcmp(pItems[k].szName,pItems[n].aszPredecessors[i])) {
                    int l;

                    for(l=0 ; l < pData[n].nPredecessors ; ++l)
                        if(pData[n].ppPredecessors[l] == &pData[k])
                            goto got_it;
                    pData[n].ppPredecessors[pData[n].nPredecessors]=&pData[k];
                    ++pData[n].nPredecessors;
                got_it:
                    break;
                }
        for(i=0 ; pItems[n].aszSuccessors && pItems[n].aszSuccessors[i] ; ++i)
            for(k=0 ; k < nItems ; ++k)
                if(!strcmp(pItems[k].szName,pItems[n].aszSuccessors[i])) {
                    int l;

                    for(l=0 ; l < pData[k].nPredecessors ; ++l)
                        if(pData[k].ppPredecessors[l] == &pData[n])
                            goto got_it2;
                    pData[k].ppPredecessors[pData[k].nPredecessors]=&pData[n];
                    ++pData[k].nPredecessors;
                got_it2:
                    break;
                }
    }

    return pData;
}

/* Topologically sort, dragging out-of-order items to the front. Note that
   this tends to preserve things that want to be near the front better, and
   changing that behaviour might compromise some of Apache's behaviour (in
   particular, mod_log_forensic might otherwise get pushed to the end, and
   core.c's log open function used to end up at the end when pushing items
   to the back was the methedology). Also note that the algorithm could
   go back to its original simplicity by sorting from the back instead of
   the front.
*/
static TSort *tsort(TSort *pData,int nItems)
{
    int nTotal;
    TSort *pHead=NULL;
    TSort *pTail=NULL;

    for(nTotal=0 ; nTotal < nItems ; ++nTotal) {
        int n,i,k;

        for(n=0 ; ; ++n) {
            if(n == nItems)
                assert(0);      /* we have a loop... */
            if(!pData[n].pNext) {
                if(pData[n].nPredecessors) {
                    for(k=0 ; ; ++k) {
                        assert(k < nItems);
                        if(pData[n].ppPredecessors[k])
                            break;
                    }
                    for(i=0 ; ; ++i) {
                        assert(i < nItems);
                        if(&pData[i] == pData[n].ppPredecessors[k]) {
                            n=i-1;
                            break;
                        }
                    }
                } else
                    break;
            }
        }
        if(pTail)
            pTail->pNext=&pData[n];
        else
            pHead=&pData[n];
        pTail=&pData[n];
        pTail->pNext=pTail;     /* fudge it so it looks linked */
        for(i=0 ; i < nItems ; ++i)
            for(k=0 ; k < nItems ; ++k)
                if(pData[i].ppPredecessors[k] == &pData[n]) {
                    --pData[i].nPredecessors;
                    pData[i].ppPredecessors[k]=NULL;
                    break;
                }
    }
    pTail->pNext=NULL;  /* unfudge the tail */
    return pHead;
}

static apr_array_header_t *sort_hook(apr_array_header_t *pHooks,
                                     const char *szName)
{
    apr_pool_t *p;
    TSort *pSort;
    apr_array_header_t *pNew;
    int n;

    apr_pool_create(&p, apr_hook_global_pool);
    pSort=prepare(p,(TSortData *)pHooks->elts,pHooks->nelts);
    pSort=tsort(pSort,pHooks->nelts);
    pNew=apr_array_make(apr_hook_global_pool,pHooks->nelts,sizeof(TSortData));
    if(apr_hook_debug_enabled)
        printf("Sorting %s:",szName);
    for(n=0 ; pSort ; pSort=pSort->pNext,++n) {
        TSortData *pHook;
        assert(n < pHooks->nelts);
        pHook=apr_array_push(pNew);
        memcpy(pHook,pSort->pData,sizeof *pHook);
        if(apr_hook_debug_enabled)
            printf(" %s",pHook->szName);
    }
    if(apr_hook_debug_enabled)
        fputc('\n',stdout);

    /* destroy the pool - the sorted hooks were already copied */
    apr_pool_destroy(p);

    return pNew;
}

#ifndef NETWARE
static apr_array_header_t *s_aHooksToSort;
#endif

typedef struct
{
    const char *szHookName;
    apr_array_header_t **paHooks;
} HookSortEntry;

APU_DECLARE(void) apr_hook_sort_register(const char *szHookName,
                                        apr_array_header_t **paHooks)
{
#ifdef NETWARE
    get_apd
#endif
    HookSortEntry *pEntry;

    if(!s_aHooksToSort)
        s_aHooksToSort=apr_array_make(apr_hook_global_pool,1,sizeof(HookSortEntry));
    pEntry=apr_array_push(s_aHooksToSort);
    pEntry->szHookName=szHookName;
    pEntry->paHooks=paHooks;
}

APU_DECLARE(void) apr_hook_sort_all(void)
{
#ifdef NETWARE
    get_apd
#endif
    int n;

    if (!s_aHooksToSort) {
        s_aHooksToSort = apr_array_make(apr_hook_global_pool, 1, sizeof(HookSortEntry));
    }

    for(n=0 ; n < s_aHooksToSort->nelts ; ++n) {
        HookSortEntry *pEntry=&((HookSortEntry *)s_aHooksToSort->elts)[n];
        *pEntry->paHooks=sort_hook(*pEntry->paHooks,pEntry->szHookName);
    }
}

#ifndef NETWARE
static apr_hash_t *s_phOptionalHooks;
static apr_hash_t *s_phOptionalFunctions;
#endif

APU_DECLARE(void) apr_hook_deregister_all(void)
{
#ifdef NETWARE
    get_apd
#endif
    int n;

    if (!s_aHooksToSort) {
        return;
    }

    for(n=0 ; n < s_aHooksToSort->nelts ; ++n) {
        HookSortEntry *pEntry=&((HookSortEntry *)s_aHooksToSort->elts)[n];
        *pEntry->paHooks=NULL;
    }
    s_aHooksToSort=NULL;
    s_phOptionalHooks=NULL;
    s_phOptionalFunctions=NULL;
}

APU_DECLARE(void) apr_hook_debug_show(const char *szName,
                                      const char * const *aszPre,
                                      const char * const *aszSucc)
{
    int nFirst;

    printf("  Hooked %s",szName);
    if(aszPre) {
        fputs(" pre(",stdout);
        nFirst=1;
        while(*aszPre) {
            if(!nFirst)
                fputc(',',stdout);
            nFirst=0;
            fputs(*aszPre,stdout);
            ++aszPre;
        }
        fputc(')',stdout);
    }
    if(aszSucc) {
        fputs(" succ(",stdout);
        nFirst=1;
        while(*aszSucc) {
            if(!nFirst)
                fputc(',',stdout);
            nFirst=0;
            fputs(*aszSucc,stdout);
            ++aszSucc;
        }
        fputc(')',stdout);
    }
    fputc('\n',stdout);
}

/* Optional hook support */

APR_DECLARE_EXTERNAL_HOOK(apr,APU,void,_optional,(void))

APU_DECLARE(apr_array_header_t *) apr_optional_hook_get(const char *szName)
{
#ifdef NETWARE
    get_apd
#endif
    apr_array_header_t **ppArray;

    if(!s_phOptionalHooks)
        return NULL;
    ppArray=apr_hash_get(s_phOptionalHooks,szName,strlen(szName));
    if(!ppArray)
        return NULL;
    return *ppArray;
}

APU_DECLARE(void) apr_optional_hook_add(const char *szName,void (*pfn)(void),
                                        const char * const *aszPre,
                                        const char * const *aszSucc,int nOrder)
{
#ifdef NETWARE
    get_apd
#endif
    apr_array_header_t *pArray=apr_optional_hook_get(szName);
    apr_LINK__optional_t *pHook;

    if(!pArray) {
        apr_array_header_t **ppArray;

        pArray=apr_array_make(apr_hook_global_pool,1,
                              sizeof(apr_LINK__optional_t));
        if(!s_phOptionalHooks)
            s_phOptionalHooks=apr_hash_make(apr_hook_global_pool);
        ppArray=apr_palloc(apr_hook_global_pool,sizeof *ppArray);
        *ppArray=pArray;
        apr_hash_set(s_phOptionalHooks,szName,strlen(szName),ppArray);
        apr_hook_sort_register(szName,ppArray);
    }
    pHook=apr_array_push(pArray);
    pHook->pFunc=pfn;
    pHook->aszPredecessors=aszPre;
    pHook->aszSuccessors=aszSucc;
    pHook->nOrder=nOrder;
    pHook->szName=apr_hook_debug_current;
    if(apr_hook_debug_enabled)
        apr_hook_debug_show(szName,aszPre,aszSucc);
}

/* optional function support */

APU_DECLARE(apr_opt_fn_t *) apr_dynamic_fn_retrieve(const char *szName)
{
#ifdef NETWARE
    get_apd
#endif
    if(!s_phOptionalFunctions)
        return NULL;
    return (void(*)(void))apr_hash_get(s_phOptionalFunctions,szName,strlen(szName));
}

/* Deprecated */
APU_DECLARE_NONSTD(void) apr_dynamic_fn_register(const char *szName,
                                                  apr_opt_fn_t *pfn)
{
#ifdef NETWARE
    get_apd
#endif
    if(!s_phOptionalFunctions)
        s_phOptionalFunctions=apr_hash_make(apr_hook_global_pool);
    apr_hash_set(s_phOptionalFunctions,szName,strlen(szName),(void *)pfn);
}

#if 0
void main()
{
    const char *aszAPre[]={"b","c",NULL};
    const char *aszBPost[]={"a",NULL};
    const char *aszCPost[]={"b",NULL};
    TSortData t1[]=
    {
        { "a",aszAPre,NULL },
        { "b",NULL,aszBPost },
        { "c",NULL,aszCPost }
    };
    TSort *pResult;

    pResult=prepare(t1,3);
    pResult=tsort(pResult,3);

    for( ; pResult ; pResult=pResult->pNext)
        printf("%s\n",pResult->pData->szName);
}
#endif
