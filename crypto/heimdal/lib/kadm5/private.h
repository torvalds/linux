/*
 * Copyright (c) 1997-2000 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
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
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $Id$ */

#ifndef __kadm5_privatex_h__
#define __kadm5_privatex_h__

struct kadm_func {
    kadm5_ret_t (*chpass_principal) (void *, krb5_principal, const char*);
    kadm5_ret_t (*create_principal) (void*, kadm5_principal_ent_t,
				     uint32_t, const char*);
    kadm5_ret_t (*delete_principal) (void*, krb5_principal);
    kadm5_ret_t (*destroy) (void*);
    kadm5_ret_t (*flush) (void*);
    kadm5_ret_t (*get_principal) (void*, krb5_principal,
				  kadm5_principal_ent_t, uint32_t);
    kadm5_ret_t (*get_principals) (void*, const char*, char***, int*);
    kadm5_ret_t (*get_privs) (void*, uint32_t*);
    kadm5_ret_t (*modify_principal) (void*, kadm5_principal_ent_t, uint32_t);
    kadm5_ret_t (*randkey_principal) (void*, krb5_principal,
				      krb5_keyblock**, int*);
    kadm5_ret_t (*rename_principal) (void*, krb5_principal, krb5_principal);
    kadm5_ret_t (*chpass_principal_with_key) (void *, krb5_principal,
					      int, krb5_key_data *);
};

/* XXX should be integrated */
typedef struct kadm5_common_context {
    krb5_context context;
    krb5_boolean my_context;
    struct kadm_func funcs;
    void *data;
}kadm5_common_context;

typedef struct kadm5_log_peer {
    int fd;
    char *name;
    krb5_auth_context ac;
    struct kadm5_log_peer *next;
} kadm5_log_peer;

typedef struct kadm5_log_context {
    char *log_file;
    int log_fd;
    uint32_t version;
#ifndef NO_UNIX_SOCKETS
    struct sockaddr_un socket_name;
#else
    struct addrinfo *socket_info;
#endif
    krb5_socket_t socket_fd;
} kadm5_log_context;

typedef struct kadm5_server_context {
    krb5_context context;
    krb5_boolean my_context;
    struct kadm_func funcs;
    /* */
    kadm5_config_params config;
    HDB *db;
    krb5_principal caller;
    unsigned acl_flags;
    kadm5_log_context log_context;
} kadm5_server_context;

typedef struct kadm5_client_context {
    krb5_context context;
    krb5_boolean my_context;
    struct kadm_func funcs;
    /* */
    krb5_auth_context ac;
    char *realm;
    char *admin_server;
    int kadmind_port;
    int sock;
    char *client_name;
    char *service_name;
    krb5_prompter_fct prompter;
    const char *keytab;
    krb5_ccache ccache;
    kadm5_config_params *realm_params;
}kadm5_client_context;

typedef struct kadm5_ad_context {
    krb5_context context;
    krb5_boolean my_context;
    struct kadm_func funcs;
    /* */
    kadm5_config_params config;
    krb5_principal caller;
    krb5_ccache ccache;
    char *client_name;
    char *realm;
    void *ldap_conn;
    char *base_dn;
} kadm5_ad_context;

enum kadm_ops {
    kadm_get,
    kadm_delete,
    kadm_create,
    kadm_rename,
    kadm_chpass,
    kadm_modify,
    kadm_randkey,
    kadm_get_privs,
    kadm_get_princs,
    kadm_chpass_with_key,
    kadm_nop
};

#define KADMIN_APPL_VERSION "KADM0.1"
#define KADMIN_OLD_APPL_VERSION "KADM0.0"

#include "kadm5-private.h"

#endif /* __kadm5_privatex_h__ */
