/*
 * Copyright (c) 1997 - 2006 Kungliga Tekniska Högskolan
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

#ifndef GSSAPI_KRB5_H_
#define GSSAPI_KRB5_H_

#include <gssapi/gssapi.h>

GSSAPI_CPP_START

#if !defined(__GNUC__) && !defined(__attribute__)
#define __attribute__(x)
#endif

#ifndef GSSKRB5_FUNCTION_DEPRECATED
#define GSSKRB5_FUNCTION_DEPRECATED __attribute__((deprecated))
#endif


/*
 * This is for kerberos5 names.
 */

extern gss_OID_desc GSSAPI_LIB_VARIABLE __gss_krb5_nt_principal_name_oid_desc;
#define GSS_KRB5_NT_PRINCIPAL_NAME (&__gss_krb5_nt_principal_name_oid_desc)

#define GSS_KRB5_NT_USER_NAME (&__gss_c_nt_user_name_oid_desc)
#define GSS_KRB5_NT_MACHINE_UID_NAME (&__gss_c_nt_machine_uid_name_oid_desc)
#define GSS_KRB5_NT_STRING_UID_NAME (&__gss_c_nt_string_uid_name_oid_desc)

extern gss_OID_desc GSSAPI_LIB_VARIABLE __gss_krb5_mechanism_oid_desc;
#define GSS_KRB5_MECHANISM (&__gss_krb5_mechanism_oid_desc)

/* for compatibility with MIT api */

#define gss_mech_krb5 GSS_KRB5_MECHANISM
#define gss_krb5_nt_general_name GSS_KRB5_NT_PRINCIPAL_NAME

/*
 * kerberos mechanism specific functions
 */

struct krb5_keytab_data;
struct krb5_ccache_data;
struct Principal;

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_krb5_ccache_name(OM_uint32 * /*minor_status*/,
		     const char * /*name */,
		     const char ** /*out_name */);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gsskrb5_register_acceptor_identity
        (const char * /*identity*/);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL krb5_gss_register_acceptor_identity
	(const char * /*identity*/);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_krb5_copy_ccache
	(OM_uint32 * /*minor*/,
	 gss_cred_id_t /*cred*/,
	 struct krb5_ccache_data * /*out*/);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_krb5_import_cred(OM_uint32 * /*minor*/,
		     struct krb5_ccache_data * /*in*/,
		     struct Principal * /*keytab_principal*/,
		     struct krb5_keytab_data * /*keytab*/,
		     gss_cred_id_t * /*out*/);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL gss_krb5_get_tkt_flags
	(OM_uint32 * /*minor*/,
	 gss_ctx_id_t /*context_handle*/,
	 OM_uint32 * /*tkt_flags*/);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_extract_authz_data_from_sec_context
	(OM_uint32 * /*minor_status*/,
	 gss_ctx_id_t /*context_handle*/,
	 int /*ad_type*/,
	 gss_buffer_t /*ad_data*/);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_set_dns_canonicalize(int);

struct gsskrb5_send_to_kdc {
    void *func;
    void *ptr;
};

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_set_send_to_kdc(struct gsskrb5_send_to_kdc *)
    GSSKRB5_FUNCTION_DEPRECATED;

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_set_default_realm(const char *);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_extract_authtime_from_sec_context(OM_uint32 *, gss_ctx_id_t, time_t *);

struct EncryptionKey;

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_extract_service_keyblock(OM_uint32 *minor_status,
				 gss_ctx_id_t context_handle,
				 struct EncryptionKey **out);
GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_get_initiator_subkey(OM_uint32 *minor_status,
				 gss_ctx_id_t context_handle,
				 struct EncryptionKey **out);
GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_get_subkey(OM_uint32 *minor_status,
		   gss_ctx_id_t context_handle,
		   struct EncryptionKey **out);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_set_time_offset(int);

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_get_time_offset(int *);

struct gsskrb5_krb5_plugin {
    int type;
    char *name;
    void *symbol;
};

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gsskrb5_plugin_register(struct gsskrb5_krb5_plugin *);


/*
 * Lucid - NFSv4 interface to GSS-API KRB5 to expose key material to
 * do GSS content token handling in-kernel.
 */

typedef struct gss_krb5_lucid_key {
	OM_uint32	type;
	OM_uint32	length;
	void *		data;
} gss_krb5_lucid_key_t;

typedef struct gss_krb5_rfc1964_keydata {
	OM_uint32		sign_alg;
	OM_uint32		seal_alg;
	gss_krb5_lucid_key_t	ctx_key;
} gss_krb5_rfc1964_keydata_t;

typedef struct gss_krb5_cfx_keydata {
	OM_uint32		have_acceptor_subkey;
	gss_krb5_lucid_key_t	ctx_key;
	gss_krb5_lucid_key_t	acceptor_subkey;
} gss_krb5_cfx_keydata_t;

typedef struct gss_krb5_lucid_context_v1 {
	OM_uint32	version;
	OM_uint32	initiate;
	OM_uint32	endtime;
	OM_uint64	send_seq;
	OM_uint64	recv_seq;
	OM_uint32	protocol;
	gss_krb5_rfc1964_keydata_t rfc1964_kd;
	gss_krb5_cfx_keydata_t	   cfx_kd;
} gss_krb5_lucid_context_v1_t;

typedef struct gss_krb5_lucid_context_version {
	OM_uint32	version;	/* Structure version number */
} gss_krb5_lucid_context_version_t;

/*
 * Function declarations
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_krb5_export_lucid_sec_context(OM_uint32 *minor_status,
				  gss_ctx_id_t *context_handle,
				  OM_uint32 version,
				  void **kctx);


GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_krb5_free_lucid_sec_context(OM_uint32 *minor_status,
				void *kctx);


GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_krb5_set_allowable_enctypes(OM_uint32 *minor_status,
				gss_cred_id_t cred,
				OM_uint32 num_enctypes,
				int32_t *enctypes);

GSSAPI_CPP_END

#endif /* GSSAPI_SPNEGO_H_ */
