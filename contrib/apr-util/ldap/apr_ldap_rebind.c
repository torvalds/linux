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

/*  apr_ldap_rebind.c -- LDAP rebind callbacks for referrals
 *
 *  The LDAP SDK allows a callback to be set to enable rebinding
 *  for referral processing.
 *
 */

#include "apr.h"
#include "apu.h"
#include "apu_config.h"

#if APU_DSO_BUILD
#define APU_DSO_LDAP_BUILD
#endif

#include "apr_ldap.h"
#include "apr_errno.h"
#include "apr_strings.h"
#include "apr_ldap_rebind.h"

#include "stdio.h"

#if APR_HAS_LDAP

/* Used to store information about connections for use in the referral rebind callback. */
struct apr_ldap_rebind_entry {
    apr_pool_t *pool;
    LDAP *index;
    const char *bindDN;
    const char *bindPW;
    struct apr_ldap_rebind_entry *next;
};
typedef struct apr_ldap_rebind_entry apr_ldap_rebind_entry_t;


#ifdef NETWARE
#include "apr_private.h"
#define get_apd                 APP_DATA* apd = (APP_DATA*)get_app_data(gLibId);
#define apr_ldap_xref_lock      ((apr_thread_mutex_t *)(apd->gs_ldap_xref_lock))
#define xref_head               ((apr_ldap_rebind_entry_t *)(apd->gs_xref_head))
#else
#if APR_HAS_THREADS
static apr_thread_mutex_t *apr_ldap_xref_lock = NULL;
#endif
static apr_ldap_rebind_entry_t *xref_head = NULL;
#endif

static int apr_ldap_rebind_set_callback(LDAP *ld);
static apr_status_t apr_ldap_rebind_remove_helper(void *data);

static apr_status_t apr_ldap_pool_cleanup_set_null(void *data_)
{
    void **ptr = (void **)data_;
    *ptr = NULL;
    return APR_SUCCESS;
}


/* APR utility routine used to create the xref_lock. */
APU_DECLARE_LDAP(apr_status_t) apr_ldap_rebind_init(apr_pool_t *pool)
{
    apr_status_t retcode = APR_SUCCESS;

#ifdef NETWARE
    get_apd
#endif

#if APR_HAS_THREADS
    /* run after apr_thread_mutex_create cleanup */
    apr_pool_cleanup_register(pool, &apr_ldap_xref_lock, apr_ldap_pool_cleanup_set_null,
                              apr_pool_cleanup_null);

    if (apr_ldap_xref_lock == NULL) {
        retcode = apr_thread_mutex_create(&apr_ldap_xref_lock, APR_THREAD_MUTEX_DEFAULT, pool);
    }
#endif

    return(retcode);
}


APU_DECLARE_LDAP(apr_status_t) apr_ldap_rebind_add(apr_pool_t *pool,
                                                   LDAP *ld, 
                                                   const char *bindDN, 
                                                   const char *bindPW)
{
    apr_status_t retcode = APR_SUCCESS;
    apr_ldap_rebind_entry_t *new_xref;

#ifdef NETWARE
    get_apd
#endif

    new_xref = (apr_ldap_rebind_entry_t *)apr_pcalloc(pool, sizeof(apr_ldap_rebind_entry_t));
    if (new_xref) {
        new_xref->pool = pool;
        new_xref->index = ld;
        if (bindDN) {
            new_xref->bindDN = apr_pstrdup(pool, bindDN);
        }
        if (bindPW) {
            new_xref->bindPW = apr_pstrdup(pool, bindPW);
        }
    
#if APR_HAS_THREADS
       retcode = apr_thread_mutex_lock(apr_ldap_xref_lock);
       if (retcode != APR_SUCCESS) { 
           return retcode;
       }
#endif
    
        new_xref->next = xref_head;
        xref_head = new_xref;
    
#if APR_HAS_THREADS
        retcode = apr_thread_mutex_unlock(apr_ldap_xref_lock);
        if (retcode != APR_SUCCESS) { 
           return retcode;
        }
#endif
    }
    else {
        return(APR_ENOMEM);
    }

    retcode = apr_ldap_rebind_set_callback(ld);
    if (APR_SUCCESS != retcode) {
        apr_ldap_rebind_remove(ld);
        return retcode;
    }

    apr_pool_cleanup_register(pool, ld,
                              apr_ldap_rebind_remove_helper,
                              apr_pool_cleanup_null);

    return(APR_SUCCESS);
}


APU_DECLARE_LDAP(apr_status_t) apr_ldap_rebind_remove(LDAP *ld)
{
    apr_ldap_rebind_entry_t *tmp_xref, *prev = NULL;
    apr_status_t retcode = 0;

#ifdef NETWARE
    get_apd
#endif

#if APR_HAS_THREADS
    retcode = apr_thread_mutex_lock(apr_ldap_xref_lock);
    if (retcode != APR_SUCCESS) { 
        return retcode;
    }
#endif
    tmp_xref = xref_head;

    while ((tmp_xref) && (tmp_xref->index != ld)) {
        prev = tmp_xref;
        tmp_xref = tmp_xref->next;
    }

    if (tmp_xref) {
        if (tmp_xref == xref_head) {
            xref_head = xref_head->next;
        }
        else {
            prev->next = tmp_xref->next;
        }

        /* tmp_xref and its contents were pool allocated so they don't need to be freed here. */

        /* remove the cleanup, just in case this was done manually */
        apr_pool_cleanup_kill(tmp_xref->pool, tmp_xref->index,
                              apr_ldap_rebind_remove_helper);
    }

#if APR_HAS_THREADS
    retcode = apr_thread_mutex_unlock(apr_ldap_xref_lock);
    if (retcode != APR_SUCCESS) { 
       return retcode;
    }
#endif
    return APR_SUCCESS;
}


static apr_status_t apr_ldap_rebind_remove_helper(void *data)
{
    LDAP *ld = (LDAP *)data;
    apr_ldap_rebind_remove(ld);
    return APR_SUCCESS;
}

#if APR_HAS_TIVOLI_LDAPSDK || APR_HAS_OPENLDAP_LDAPSDK || APR_HAS_NOVELL_LDAPSDK
static apr_ldap_rebind_entry_t *apr_ldap_rebind_lookup(LDAP *ld)
{
    apr_ldap_rebind_entry_t *tmp_xref, *match = NULL;

#ifdef NETWARE
    get_apd
#endif

#if APR_HAS_THREADS
    apr_thread_mutex_lock(apr_ldap_xref_lock);
#endif
    tmp_xref = xref_head;

    while (tmp_xref) {
        if (tmp_xref->index == ld) {
            match = tmp_xref;
            tmp_xref = NULL;
        }
        else {
            tmp_xref = tmp_xref->next;
        }
    }

#if APR_HAS_THREADS
    apr_thread_mutex_unlock(apr_ldap_xref_lock);
#endif

    return (match);
}
#endif

#if APR_HAS_TIVOLI_LDAPSDK

/* LDAP_rebindproc() Tivoli LDAP style
 *     Rebind callback function. Called when chasing referrals. See API docs.
 * ON ENTRY:
 *     ld       Pointer to an LDAP control structure. (input only)
 *     binddnp  Pointer to an Application DName used for binding (in *or* out)
 *     passwdp  Pointer to the password associated with the DName (in *or* out)
 *     methodp  Pointer to the Auth method (output only)
 *     freeit   Flag to indicate if this is a lookup or a free request (input only)
 */
static int LDAP_rebindproc(LDAP *ld, char **binddnp, char **passwdp, int *methodp, int freeit)
{
    if (!freeit) {
        apr_ldap_rebind_entry_t *my_conn;

        *methodp = LDAP_AUTH_SIMPLE;
        my_conn = apr_ldap_rebind_lookup(ld);

        if ((my_conn) && (my_conn->bindDN != NULL)) {
            *binddnp = strdup(my_conn->bindDN);
            *passwdp = strdup(my_conn->bindPW);
        } else {
            *binddnp = NULL;
            *passwdp = NULL;
        }
    } else {
        if (*binddnp) {
            free(*binddnp);
        }
        if (*passwdp) {
            free(*passwdp);
        }
    }

    return LDAP_SUCCESS;
}

static int apr_ldap_rebind_set_callback(LDAP *ld)
{
    ldap_set_rebind_proc(ld, (LDAPRebindProc)LDAP_rebindproc);
    return APR_SUCCESS;
}

#elif APR_HAS_OPENLDAP_LDAPSDK

/* LDAP_rebindproc() openLDAP V3 style
 * ON ENTRY:
 *     ld       Pointer to an LDAP control structure. (input only)
 *     url      Unused in this routine
 *     request  Unused in this routine
 *     msgid    Unused in this routine
 *     params   Unused in this routine
 *
 *     or
 *
 *     ld       Pointer to an LDAP control structure. (input only)
 *     url      Unused in this routine
 *     request  Unused in this routine
 *     msgid    Unused in this routine
 */
#if defined(LDAP_SET_REBIND_PROC_THREE)
static int LDAP_rebindproc(LDAP *ld, LDAP_CONST char *url, ber_tag_t request,
                           ber_int_t msgid, void *params)
#else
static int LDAP_rebindproc(LDAP *ld, LDAP_CONST char *url, int request,
                           ber_int_t msgid)
#endif
{
    apr_ldap_rebind_entry_t *my_conn;
    const char *bindDN = NULL;
    const char *bindPW = NULL;

    my_conn = apr_ldap_rebind_lookup(ld);

    if ((my_conn) && (my_conn->bindDN != NULL)) {
        bindDN = my_conn->bindDN;
        bindPW = my_conn->bindPW;
    }

    return (ldap_bind_s(ld, bindDN, bindPW, LDAP_AUTH_SIMPLE));
}

static int apr_ldap_rebind_set_callback(LDAP *ld)
{
#if defined(LDAP_SET_REBIND_PROC_THREE)
    ldap_set_rebind_proc(ld, LDAP_rebindproc, NULL);
#else
    ldap_set_rebind_proc(ld, LDAP_rebindproc);
#endif
    return APR_SUCCESS;
}

#elif APR_HAS_NOVELL_LDAPSDK

/* LDAP_rebindproc() openLDAP V3 style
 * ON ENTRY:
 *     ld       Pointer to an LDAP control structure. (input only)
 *     url      Unused in this routine
 *     request  Unused in this routine
 *     msgid    Unused in this routine
 */
static int LDAP_rebindproc(LDAP *ld, LDAP_CONST char *url, int request, ber_int_t msgid)
{

    apr_ldap_rebind_entry_t *my_conn;
    const char *bindDN = NULL;
    const char *bindPW = NULL;

    my_conn = apr_ldap_rebind_lookup(ld);

    if ((my_conn) && (my_conn->bindDN != NULL)) {
        bindDN = my_conn->bindDN;
        bindPW = my_conn->bindPW;
    }

    return (ldap_bind_s(ld, bindDN, bindPW, LDAP_AUTH_SIMPLE));
}

static int apr_ldap_rebind_set_callback(LDAP *ld)
{
    ldap_set_rebind_proc(ld, LDAP_rebindproc);
    return APR_SUCCESS;
}

#else         /* Implementation not recognised */

static int apr_ldap_rebind_set_callback(LDAP *ld)
{
    return APR_ENOTIMPL;
}

#endif


#endif       /* APR_HAS_LDAP */
