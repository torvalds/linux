/*
 * Copyright (c) 1997 - 2009 Kungliga Tekniska Högskolan
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

#include "krb5_locl.h"

#undef __attribute__
#define __attribute__(x)

#ifndef HEIMDAL_SMALLER

/**
 * Same as krb5_data_free(). MIT compat.
 *
 * Deprecated: use krb5_data_free().
 *
 * @param context Kerberos 5 context.
 * @param data krb5_data to free.
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_data_contents(krb5_context context, krb5_data *data)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    krb5_data_free(data);
}

/**
 * Deprecated: keytypes doesn't exists, they are really enctypes.
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_keytype_to_enctypes_default (krb5_context context,
				  krb5_keytype keytype,
				  unsigned *len,
				  krb5_enctype **val)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    unsigned int i, n;
    krb5_enctype *ret;

    if (keytype != KEYTYPE_DES || context->etypes_des == NULL)
	return krb5_keytype_to_enctypes (context, keytype, len, val);

    for (n = 0; context->etypes_des[n]; ++n)
	;
    ret = malloc (n * sizeof(*ret));
    if (ret == NULL && n != 0) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    for (i = 0; i < n; ++i)
	ret[i] = context->etypes_des[i];
    *len = n;
    *val = ret;
    return 0;
}


static struct {
    const char *name;
    krb5_keytype type;
} keys[] = {
    { "null", ENCTYPE_NULL },
    { "des", ETYPE_DES_CBC_CRC },
    { "des3", ETYPE_OLD_DES3_CBC_SHA1 },
    { "aes-128", ETYPE_AES128_CTS_HMAC_SHA1_96 },
    { "aes-256", ETYPE_AES256_CTS_HMAC_SHA1_96 },
    { "arcfour", ETYPE_ARCFOUR_HMAC_MD5 },
    { "arcfour-56", ETYPE_ARCFOUR_HMAC_MD5_56 }
};

static int num_keys = sizeof(keys) / sizeof(keys[0]);

/**
 * Deprecated: keytypes doesn't exists, they are really enctypes in
 * most cases, use krb5_enctype_to_string().
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_keytype_to_string(krb5_context context,
		       krb5_keytype keytype,
		       char **string)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    const char *name = NULL;
    int i;

    for(i = 0; i < num_keys; i++) {
	if(keys[i].type == keytype) {
	    name = keys[i].name;
	    break;
	}
    }

    if(i >= num_keys) {
	krb5_set_error_message(context, KRB5_PROG_KEYTYPE_NOSUPP,
			       "key type %d not supported", keytype);
	return KRB5_PROG_KEYTYPE_NOSUPP;
    }
    *string = strdup(name);
    if(*string == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    return 0;
}

/**
 * Deprecated: keytypes doesn't exists, they are really enctypes in
 * most cases, use krb5_string_to_enctype().
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_string_to_keytype(krb5_context context,
		       const char *string,
		       krb5_keytype *keytype)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    char *end;
    int i;

    for(i = 0; i < num_keys; i++)
	if(strcasecmp(keys[i].name, string) == 0){
	    *keytype = keys[i].type;
	    return 0;
	}

    /* check if the enctype is a number */
    *keytype = strtol(string, &end, 0);
    if(*end == '\0' && *keytype != 0) {
	if (krb5_enctype_valid(context, *keytype) == 0)
	    return 0;
    }

    krb5_set_error_message(context, KRB5_PROG_KEYTYPE_NOSUPP,
			   "key type %s not supported", string);
    return KRB5_PROG_KEYTYPE_NOSUPP;
}

/**
 * Deprecated: use krb5_get_init_creds() and friends.
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_CALLCONV
krb5_password_key_proc (krb5_context context,
			krb5_enctype type,
			krb5_salt salt,
			krb5_const_pointer keyseed,
			krb5_keyblock **key)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    krb5_error_code ret;
    const char *password = (const char *)keyseed;
    char buf[BUFSIZ];

    *key = malloc (sizeof (**key));
    if (*key == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }
    if (password == NULL) {
	if(UI_UTIL_read_pw_string (buf, sizeof(buf), "Password: ", 0)) {
	    free (*key);
	    krb5_clear_error_message(context);
	    return KRB5_LIBOS_PWDINTR;
	}
	password = buf;
    }
    ret = krb5_string_to_key_salt (context, type, password, salt, *key);
    memset (buf, 0, sizeof(buf));
    return ret;
}

/**
 * Deprecated: use krb5_get_init_creds() and friends.
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_in_tkt_with_password (krb5_context context,
			       krb5_flags options,
			       krb5_addresses *addrs,
			       const krb5_enctype *etypes,
			       const krb5_preauthtype *pre_auth_types,
			       const char *password,
			       krb5_ccache ccache,
			       krb5_creds *creds,
			       krb5_kdc_rep *ret_as_reply)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
     return krb5_get_in_tkt (context,
			     options,
			     addrs,
			     etypes,
			     pre_auth_types,
			     krb5_password_key_proc,
			     password,
			     NULL,
			     NULL,
			     creds,
			     ccache,
			     ret_as_reply);
}

static krb5_error_code KRB5_CALLCONV
krb5_skey_key_proc (krb5_context context,
		    krb5_enctype type,
		    krb5_salt salt,
		    krb5_const_pointer keyseed,
		    krb5_keyblock **key)
{
    return krb5_copy_keyblock (context, keyseed, key);
}

/**
 * Deprecated: use krb5_get_init_creds() and friends.
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_in_tkt_with_skey (krb5_context context,
			   krb5_flags options,
			   krb5_addresses *addrs,
			   const krb5_enctype *etypes,
			   const krb5_preauthtype *pre_auth_types,
			   const krb5_keyblock *key,
			   krb5_ccache ccache,
			   krb5_creds *creds,
			   krb5_kdc_rep *ret_as_reply)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    if(key == NULL)
	return krb5_get_in_tkt_with_keytab (context,
					    options,
					    addrs,
					    etypes,
					    pre_auth_types,
					    NULL,
					    ccache,
					    creds,
					    ret_as_reply);
    else
	return krb5_get_in_tkt (context,
				options,
				addrs,
				etypes,
				pre_auth_types,
				krb5_skey_key_proc,
				key,
				NULL,
				NULL,
				creds,
				ccache,
				ret_as_reply);
}

/**
 * Deprecated: use krb5_get_init_creds() and friends.
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_CALLCONV
krb5_keytab_key_proc (krb5_context context,
		      krb5_enctype enctype,
		      krb5_salt salt,
		      krb5_const_pointer keyseed,
		      krb5_keyblock **key)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    krb5_keytab_key_proc_args *args  = rk_UNCONST(keyseed);
    krb5_keytab keytab = args->keytab;
    krb5_principal principal  = args->principal;
    krb5_error_code ret;
    krb5_keytab real_keytab;
    krb5_keytab_entry entry;

    if(keytab == NULL)
	krb5_kt_default(context, &real_keytab);
    else
	real_keytab = keytab;

    ret = krb5_kt_get_entry (context, real_keytab, principal,
			     0, enctype, &entry);

    if (keytab == NULL)
	krb5_kt_close (context, real_keytab);

    if (ret)
	return ret;

    ret = krb5_copy_keyblock (context, &entry.keyblock, key);
    krb5_kt_free_entry(context, &entry);
    return ret;
}

/**
 * Deprecated: use krb5_get_init_creds() and friends.
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_in_tkt_with_keytab (krb5_context context,
			     krb5_flags options,
			     krb5_addresses *addrs,
			     const krb5_enctype *etypes,
			     const krb5_preauthtype *pre_auth_types,
			     krb5_keytab keytab,
			     krb5_ccache ccache,
			     krb5_creds *creds,
			     krb5_kdc_rep *ret_as_reply)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    krb5_keytab_key_proc_args a;

    a.principal = creds->client;
    a.keytab    = keytab;

    return krb5_get_in_tkt (context,
			    options,
			    addrs,
			    etypes,
			    pre_auth_types,
			    krb5_keytab_key_proc,
			    &a,
			    NULL,
			    NULL,
			    creds,
			    ccache,
			    ret_as_reply);
}

/**
 * Generate a new ccache of type `ops' in `id'.
 *
 * Deprecated: use krb5_cc_new_unique() instead.
 *
 * @return Return an error code or 0, see krb5_get_error_message().
 *
 * @ingroup krb5_ccache
 */


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_gen_new(krb5_context context,
		const krb5_cc_ops *ops,
		krb5_ccache *id)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return krb5_cc_new_unique(context, ops->prefix, NULL, id);
}

/**
 * Deprecated: use krb5_principal_get_realm()
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_realm * KRB5_LIB_CALL
krb5_princ_realm(krb5_context context,
		 krb5_principal principal)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return &principal->realm;
}


/**
 * Deprecated: use krb5_principal_set_realm()
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_princ_set_realm(krb5_context context,
		     krb5_principal principal,
		     krb5_realm *realm)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    principal->realm = *realm;
}

/**
 * Deprecated: use krb5_free_cred_contents()
 *
 * @ingroup krb5_deprecated
 */

/* keep this for compatibility with older code */
KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_creds_contents (krb5_context context, krb5_creds *c)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return krb5_free_cred_contents (context, c);
}

/**
 * Free the error message returned by krb5_get_error_string().
 *
 * Deprecated: use krb5_free_error_message()
 *
 * @param context Kerberos context
 * @param str error message to free
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_error_string(krb5_context context, char *str)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    krb5_free_error_message(context, str);
}

/**
 * Set the error message returned by krb5_get_error_string().
 *
 * Deprecated: use krb5_get_error_message()
 *
 * @param context Kerberos context
 * @param fmt error message to free
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_error_string(krb5_context context, const char *fmt, ...)
    __attribute__((format (printf, 2, 3)))
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    va_list ap;

    va_start(ap, fmt);
    krb5_vset_error_message (context, 0, fmt, ap);
    va_end(ap);
    return 0;
}

/**
 * Set the error message returned by krb5_get_error_string(),
 * deprecated, use krb5_set_error_message().
 *
 * Deprecated: use krb5_vset_error_message()
 *
 * @param context Kerberos context
 * @param msg error message to free
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_vset_error_string(krb5_context context, const char *fmt, va_list args)
    __attribute__ ((format (printf, 2, 0)))
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    krb5_vset_error_message(context, 0, fmt, args);
    return 0;
}

/**
 * Clear the error message returned by krb5_get_error_string().
 *
 * Deprecated: use krb5_clear_error_message()
 *
 * @param context Kerberos context
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_clear_error_string(krb5_context context)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    krb5_clear_error_message(context);
}

/**
 * Deprecated: use krb5_get_credentials_with_flags().
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_cred_from_kdc_opt(krb5_context context,
			   krb5_ccache ccache,
			   krb5_creds *in_creds,
			   krb5_creds **out_creds,
			   krb5_creds ***ret_tgts,
			   krb5_flags flags)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    krb5_kdc_flags f;
    f.i = flags;
    return _krb5_get_cred_kdc_any(context, f, ccache,
				  in_creds, NULL, NULL,
				  out_creds, ret_tgts);
}

/**
 * Deprecated: use krb5_get_credentials_with_flags().
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_cred_from_kdc(krb5_context context,
		       krb5_ccache ccache,
		       krb5_creds *in_creds,
		       krb5_creds **out_creds,
		       krb5_creds ***ret_tgts)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return krb5_get_cred_from_kdc_opt(context, ccache,
				      in_creds, out_creds, ret_tgts, 0);
}

/**
 * Deprecated: use krb5_xfree().
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_unparsed_name(krb5_context context, char *str)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    krb5_xfree(str);
}

/**
 * Deprecated: use krb5_generate_subkey_extended()
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_generate_subkey(krb5_context context,
		     const krb5_keyblock *key,
		     krb5_keyblock **subkey)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return krb5_generate_subkey_extended(context, key, ETYPE_NULL, subkey);
}

/**
 * Deprecated: use krb5_auth_con_getremoteseqnumber()
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_getremoteseqnumber(krb5_context context,
			     krb5_auth_context auth_context,
			     int32_t *seqnumber)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
  *seqnumber = auth_context->remote_seqnumber;
  return 0;
}

#endif /* HEIMDAL_SMALLER */
