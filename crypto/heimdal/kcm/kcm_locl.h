/*
 * Copyright (c) 2005, PADL Software Pty Ltd.
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of PADL Software nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PADL SOFTWARE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL PADL SOFTWARE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * $Id$
 */

#ifndef __KCM_LOCL_H__
#define __KCM_LOCL_H__

#include "headers.h"

#include <kcm.h>

#define KCM_LOG_REQUEST(_context, _client, _opcode)	do { \
    kcm_log(1, "%s request by process %d/uid %d", \
	    kcm_op2string(_opcode), (_client)->pid, (_client)->uid); \
    } while (0)

#define KCM_LOG_REQUEST_NAME(_context, _client, _opcode, _name)	do { \
    kcm_log(1, "%s request for cache %s by process %d/uid %d", \
	    kcm_op2string(_opcode), (_name), (_client)->pid, (_client)->uid); \
    } while (0)

/* Cache management */

#define KCM_FLAGS_VALID			0x0001
#define KCM_FLAGS_USE_KEYTAB		0x0002
#define KCM_FLAGS_RENEWABLE		0x0004
#define KCM_FLAGS_OWNER_IS_SYSTEM	0x0008
#define KCM_FLAGS_USE_CACHED_KEY	0x0010

#define KCM_MASK_KEY_PRESENT		( KCM_FLAGS_USE_KEYTAB | \
					  KCM_FLAGS_USE_CACHED_KEY )

struct kcm_ccache_data;
struct kcm_creds;

struct kcm_default_cache {
    uid_t uid;
    pid_t session; /* really au_asid_t */
    char *name;
    struct kcm_default_cache *next;
};

extern struct kcm_default_cache *default_caches;

struct kcm_creds {
    kcmuuid_t uuid;
    krb5_creds cred;
    struct kcm_creds *next;
};

typedef struct kcm_ccache_data {
    char *name;
    kcmuuid_t uuid;
    unsigned refcnt;
    uint16_t flags;
    uint16_t mode;
    uid_t uid;
    gid_t gid;
    pid_t session; /* really au_asid_t */
    krb5_principal client; /* primary client principal */
    krb5_principal server; /* primary server principal (TGS if NULL) */
    struct kcm_creds *creds;
    krb5_deltat tkt_life;
    krb5_deltat renew_life;
    int32_t kdc_offset;
    union {
	krb5_keytab keytab;
	krb5_keyblock keyblock;
    } key;
    HEIMDAL_MUTEX mutex;
    struct kcm_ccache_data *next;
} kcm_ccache_data;

#define KCM_ASSERT_VALID(_ccache)		do { \
    if (((_ccache)->flags & KCM_FLAGS_VALID) == 0) \
	krb5_abortx(context, "kcm_free_ccache_data: ccache invalid"); \
    else if ((_ccache)->refcnt == 0) \
	krb5_abortx(context, "kcm_free_ccache_data: ccache refcnt == 0"); \
    } while (0)

typedef kcm_ccache_data *kcm_ccache;

/* Event management */

typedef struct kcm_event {
    int valid;
    time_t fire_time;
    unsigned fire_count;
    time_t expire_time;
    time_t backoff_time;
    enum {
	KCM_EVENT_NONE = 0,
	KCM_EVENT_ACQUIRE_CREDS,
	KCM_EVENT_RENEW_CREDS,
	KCM_EVENT_DESTROY_CREDS,
	KCM_EVENT_DESTROY_EMPTY_CACHE
    } action;
    kcm_ccache ccache;
    struct kcm_event *next;
} kcm_event;

/* wakeup interval for event queue */
#define KCM_EVENT_QUEUE_INTERVAL		60
#define KCM_EVENT_DEFAULT_BACKOFF_TIME		5
#define KCM_EVENT_MAX_BACKOFF_TIME		(12 * 60 * 60)


/* Request format is  LENGTH | MAJOR | MINOR | OPERATION | request */
/* Response format is LENGTH | STATUS | response */

typedef struct kcm_client {
    pid_t pid;
    uid_t uid;
    gid_t gid;
    pid_t session;
} kcm_client;

#define CLIENT_IS_ROOT(client) ((client)->uid == 0)

/* Dispatch table */
/* passed in OPERATION | ... ; returns STATUS | ... */
typedef krb5_error_code (*kcm_method)(krb5_context, kcm_client *, kcm_operation, krb5_storage *, krb5_storage *);

struct kcm_op {
    const char *name;
    kcm_method method;
};

#define DEFAULT_LOG_DEST    "0/FILE:" LOCALSTATEDIR "/log/kcmd.log"
#define _PATH_KCM_CONF	    SYSCONFDIR "/kcm.conf"

extern krb5_context kcm_context;
extern char *socket_path;
extern char *door_path;
extern size_t max_request;
extern sig_atomic_t exit_flag;
extern int name_constraints;
#ifdef SUPPORT_DETACH
extern int detach_from_console;
#endif
extern int launchd_flag;
extern int disallow_getting_krbtgt;

#if 0
extern const krb5_cc_ops krb5_kcmss_ops;
#endif

void	kcm_service(void *, const heim_idata *, const heim_icred,
		    heim_ipc_complete, heim_sipc_call);

#include <kcm-protos.h>

#endif /* __KCM_LOCL_H__ */

