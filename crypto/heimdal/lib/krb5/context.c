/*
 * Copyright (c) 1997 - 2010 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
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
#include <assert.h>
#include <com_err.h>

#define INIT_FIELD(C, T, E, D, F)					\
    (C)->E = krb5_config_get_ ## T ## _default ((C), NULL, (D), 	\
						"libdefaults", F, NULL)

#define INIT_FLAG(C, O, V, D, F)					\
    do {								\
	if (krb5_config_get_bool_default((C), NULL, (D),"libdefaults", F, NULL)) { \
	    (C)->O |= V;						\
        }								\
    } while(0)

/*
 * Set the list of etypes `ret_etypes' from the configuration variable
 * `name'
 */

static krb5_error_code
set_etypes (krb5_context context,
	    const char *name,
	    krb5_enctype **ret_enctypes)
{
    char **etypes_str;
    krb5_enctype *etypes = NULL;

    etypes_str = krb5_config_get_strings(context, NULL, "libdefaults",
					 name, NULL);
    if(etypes_str){
	int i, j, k;
	for(i = 0; etypes_str[i]; i++);
	etypes = malloc((i+1) * sizeof(*etypes));
	if (etypes == NULL) {
	    krb5_config_free_strings (etypes_str);
	    krb5_set_error_message (context, ENOMEM, N_("malloc: out of memory", ""));
	    return ENOMEM;
	}
	for(j = 0, k = 0; j < i; j++) {
	    krb5_enctype e;
	    if(krb5_string_to_enctype(context, etypes_str[j], &e) != 0)
		continue;
	    if (krb5_enctype_valid(context, e) != 0)
		continue;
	    etypes[k++] = e;
	}
	etypes[k] = ETYPE_NULL;
	krb5_config_free_strings(etypes_str);
    }
    *ret_enctypes = etypes;
    return 0;
}

/*
 * read variables from the configuration file and set in `context'
 */

static krb5_error_code
init_context_from_config_file(krb5_context context)
{
    krb5_error_code ret;
    const char * tmp;
    char **s;
    krb5_enctype *tmptypes;

    INIT_FIELD(context, time, max_skew, 5 * 60, "clockskew");
    INIT_FIELD(context, time, kdc_timeout, 3, "kdc_timeout");
    INIT_FIELD(context, int, max_retries, 3, "max_retries");

    INIT_FIELD(context, string, http_proxy, NULL, "http_proxy");

    ret = krb5_config_get_bool_default(context, NULL, FALSE,
				       "libdefaults",
				       "allow_weak_crypto", NULL);
    if (ret) {
	krb5_enctype_enable(context, ETYPE_DES_CBC_CRC);
	krb5_enctype_enable(context, ETYPE_DES_CBC_MD4);
	krb5_enctype_enable(context, ETYPE_DES_CBC_MD5);
	krb5_enctype_enable(context, ETYPE_DES_CBC_NONE);
	krb5_enctype_enable(context, ETYPE_DES_CFB64_NONE);
	krb5_enctype_enable(context, ETYPE_DES_PCBC_NONE);
    }

    ret = set_etypes (context, "default_etypes", &tmptypes);
    if(ret)
	return ret;
    free(context->etypes);
    context->etypes = tmptypes;

    ret = set_etypes (context, "default_etypes_des", &tmptypes);
    if(ret)
	return ret;
    free(context->etypes_des);
    context->etypes_des = tmptypes;

    ret = set_etypes (context, "default_as_etypes", &tmptypes);
    if(ret)
	return ret;
    free(context->as_etypes);
    context->as_etypes = tmptypes;

    ret = set_etypes (context, "default_tgs_etypes", &tmptypes);
    if(ret)
	return ret;
    free(context->tgs_etypes);
    context->tgs_etypes = tmptypes;

    ret = set_etypes (context, "permitted_enctypes", &tmptypes);
    if(ret)
	return ret;
    free(context->permitted_enctypes);
    context->permitted_enctypes = tmptypes;

    /* default keytab name */
    tmp = NULL;
    if(!issuid())
	tmp = getenv("KRB5_KTNAME");
    if(tmp != NULL)
	context->default_keytab = tmp;
    else
	INIT_FIELD(context, string, default_keytab,
		   KEYTAB_DEFAULT, "default_keytab_name");

    INIT_FIELD(context, string, default_keytab_modify,
	       NULL, "default_keytab_modify_name");

    INIT_FIELD(context, string, time_fmt,
	       "%Y-%m-%dT%H:%M:%S", "time_format");

    INIT_FIELD(context, string, date_fmt,
	       "%Y-%m-%d", "date_format");

    INIT_FIELD(context, bool, log_utc,
	       FALSE, "log_utc");



    /* init dns-proxy slime */
    tmp = krb5_config_get_string(context, NULL, "libdefaults",
				 "dns_proxy", NULL);
    if(tmp)
	roken_gethostby_setup(context->http_proxy, tmp);
    krb5_free_host_realm (context, context->default_realms);
    context->default_realms = NULL;

    {
	krb5_addresses addresses;
	char **adr, **a;

	krb5_set_extra_addresses(context, NULL);
	adr = krb5_config_get_strings(context, NULL,
				      "libdefaults",
				      "extra_addresses",
				      NULL);
	memset(&addresses, 0, sizeof(addresses));
	for(a = adr; a && *a; a++) {
	    ret = krb5_parse_address(context, *a, &addresses);
	    if (ret == 0) {
		krb5_add_extra_addresses(context, &addresses);
		krb5_free_addresses(context, &addresses);
	    }
	}
	krb5_config_free_strings(adr);

	krb5_set_ignore_addresses(context, NULL);
	adr = krb5_config_get_strings(context, NULL,
				      "libdefaults",
				      "ignore_addresses",
				      NULL);
	memset(&addresses, 0, sizeof(addresses));
	for(a = adr; a && *a; a++) {
	    ret = krb5_parse_address(context, *a, &addresses);
	    if (ret == 0) {
		krb5_add_ignore_addresses(context, &addresses);
		krb5_free_addresses(context, &addresses);
	    }
	}
	krb5_config_free_strings(adr);
    }

    INIT_FIELD(context, bool, scan_interfaces, TRUE, "scan_interfaces");
    INIT_FIELD(context, int, fcache_vno, 0, "fcache_version");
    /* prefer dns_lookup_kdc over srv_lookup. */
    INIT_FIELD(context, bool, srv_lookup, TRUE, "srv_lookup");
    INIT_FIELD(context, bool, srv_lookup, context->srv_lookup, "dns_lookup_kdc");
    INIT_FIELD(context, int, large_msg_size, 1400, "large_message_size");
    INIT_FLAG(context, flags, KRB5_CTX_F_DNS_CANONICALIZE_HOSTNAME, TRUE, "dns_canonicalize_hostname");
    INIT_FLAG(context, flags, KRB5_CTX_F_CHECK_PAC, TRUE, "check_pac");
    context->default_cc_name = NULL;
    context->default_cc_name_set = 0;

    s = krb5_config_get_strings(context, NULL, "logging", "krb5", NULL);
    if(s) {
	char **p;
	krb5_initlog(context, "libkrb5", &context->debug_dest);
	for(p = s; *p; p++)
	    krb5_addlog_dest(context, context->debug_dest, *p);
	krb5_config_free_strings(s);
    }

    tmp = krb5_config_get_string(context, NULL, "libdefaults",
				 "check-rd-req-server", NULL);
    if (tmp == NULL && !issuid())
	tmp = getenv("KRB5_CHECK_RD_REQ_SERVER");
    if(tmp) {
	if (strcasecmp(tmp, "ignore") == 0)
	    context->flags |= KRB5_CTX_F_RD_REQ_IGNORE;
    }

    return 0;
}

static krb5_error_code
cc_ops_register(krb5_context context)
{
    context->cc_ops = NULL;
    context->num_cc_ops = 0;

#ifndef KCM_IS_API_CACHE
    krb5_cc_register(context, &krb5_acc_ops, TRUE);
#endif
    krb5_cc_register(context, &krb5_fcc_ops, TRUE);
    krb5_cc_register(context, &krb5_mcc_ops, TRUE);
#ifdef HAVE_SCC
    krb5_cc_register(context, &krb5_scc_ops, TRUE);
#endif
#ifdef HAVE_KCM
#ifdef KCM_IS_API_CACHE
    krb5_cc_register(context, &krb5_akcm_ops, TRUE);
#endif
    krb5_cc_register(context, &krb5_kcm_ops, TRUE);
#endif
    _krb5_load_ccache_plugins(context);
    return 0;
}

static krb5_error_code
cc_ops_copy(krb5_context context, const krb5_context src_context)
{
    const krb5_cc_ops **cc_ops;

    context->cc_ops = NULL;
    context->num_cc_ops = 0;

    if (src_context->num_cc_ops == 0)
	return 0;

    cc_ops = malloc(sizeof(cc_ops[0]) * src_context->num_cc_ops);
    if (cc_ops == NULL) {
	krb5_set_error_message(context, KRB5_CC_NOMEM,
			       N_("malloc: out of memory", ""));
	return KRB5_CC_NOMEM;
    }

    memcpy(rk_UNCONST(cc_ops), src_context->cc_ops,
	   sizeof(cc_ops[0]) * src_context->num_cc_ops);
    context->cc_ops = cc_ops;
    context->num_cc_ops = src_context->num_cc_ops;

    return 0;
}

static krb5_error_code
kt_ops_register(krb5_context context)
{
    context->num_kt_types = 0;
    context->kt_types     = NULL;

    krb5_kt_register (context, &krb5_fkt_ops);
    krb5_kt_register (context, &krb5_wrfkt_ops);
    krb5_kt_register (context, &krb5_javakt_ops);
    krb5_kt_register (context, &krb5_mkt_ops);
#ifndef HEIMDAL_SMALLER
    krb5_kt_register (context, &krb5_akf_ops);
#endif
    krb5_kt_register (context, &krb5_any_ops);
    return 0;
}

static krb5_error_code
kt_ops_copy(krb5_context context, const krb5_context src_context)
{
    context->num_kt_types = 0;
    context->kt_types     = NULL;

    if (src_context->num_kt_types == 0)
	return 0;

    context->kt_types = malloc(sizeof(context->kt_types[0]) * src_context->num_kt_types);
    if (context->kt_types == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    context->num_kt_types = src_context->num_kt_types;
    memcpy(context->kt_types, src_context->kt_types,
	   sizeof(context->kt_types[0]) * src_context->num_kt_types);

    return 0;
}

static const char *sysplugin_dirs[] =  {
    LIBDIR "/plugin/krb5",
#ifdef __APPLE__
    "/Library/KerberosPlugins/KerberosFrameworkPlugins",
    "/System/Library/KerberosPlugins/KerberosFrameworkPlugins",
#endif
    NULL
};

static void
init_context_once(void *ctx)
{
    krb5_context context = ctx;

    _krb5_load_plugins(context, "krb5", sysplugin_dirs);

    bindtextdomain(HEIMDAL_TEXTDOMAIN, HEIMDAL_LOCALEDIR);
}


/**
 * Initializes the context structure and reads the configuration file
 * /etc/krb5.conf. The structure should be freed by calling
 * krb5_free_context() when it is no longer being used.
 *
 * @param context pointer to returned context
 *
 * @return Returns 0 to indicate success.  Otherwise an errno code is
 * returned.  Failure means either that something bad happened during
 * initialization (typically ENOMEM) or that Kerberos should not be
 * used ENXIO.
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_init_context(krb5_context *context)
{
    static heim_base_once_t init_context = HEIM_BASE_ONCE_INIT;
    krb5_context p;
    krb5_error_code ret;
    char **files;

    *context = NULL;

    p = calloc(1, sizeof(*p));
    if(!p)
	return ENOMEM;

    p->mutex = malloc(sizeof(HEIMDAL_MUTEX));
    if (p->mutex == NULL) {
	free(p);
	return ENOMEM;
    }
    HEIMDAL_MUTEX_init(p->mutex);

    p->flags |= KRB5_CTX_F_HOMEDIR_ACCESS;

    ret = krb5_get_default_config_files(&files);
    if(ret)
	goto out;
    ret = krb5_set_config_files(p, files);
    krb5_free_config_files(files);
    if(ret)
	goto out;

    /* init error tables */
    krb5_init_ets(p);
    cc_ops_register(p);
    kt_ops_register(p);

#ifdef PKINIT
    ret = hx509_context_init(&p->hx509ctx);
    if (ret)
	goto out;
#endif
    if (rk_SOCK_INIT())
	p->flags |= KRB5_CTX_F_SOCKETS_INITIALIZED;

out:
    if(ret) {
	krb5_free_context(p);
	p = NULL;
    } else {
	heim_base_once_f(&init_context, p, init_context_once);
    }
    *context = p;
    return ret;
}

#ifndef HEIMDAL_SMALLER

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_permitted_enctypes(krb5_context context,
			    krb5_enctype **etypes)
{
    return krb5_get_default_in_tkt_etypes(context, KRB5_PDU_NONE, etypes);
}

/*
 *
 */

static krb5_error_code
copy_etypes (krb5_context context,
	     krb5_enctype *enctypes,
	     krb5_enctype **ret_enctypes)
{
    unsigned int i;

    for (i = 0; enctypes[i]; i++)
	;
    i++;

    *ret_enctypes = malloc(sizeof(ret_enctypes[0]) * i);
    if (*ret_enctypes == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    memcpy(*ret_enctypes, enctypes, sizeof(ret_enctypes[0]) * i);
    return 0;
}

/**
 * Make a copy for the Kerberos 5 context, the new krb5_context shoud
 * be freed with krb5_free_context().
 *
 * @param context the Kerberos context to copy
 * @param out the copy of the Kerberos, set to NULL error.
 *
 * @return Returns 0 to indicate success.  Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_context(krb5_context context, krb5_context *out)
{
    krb5_error_code ret;
    krb5_context p;

    *out = NULL;

    p = calloc(1, sizeof(*p));
    if (p == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    p->mutex = malloc(sizeof(HEIMDAL_MUTEX));
    if (p->mutex == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	free(p);
	return ENOMEM;
    }
    HEIMDAL_MUTEX_init(p->mutex);


    if (context->default_cc_name)
	p->default_cc_name = strdup(context->default_cc_name);
    if (context->default_cc_name_env)
	p->default_cc_name_env = strdup(context->default_cc_name_env);

    if (context->etypes) {
	ret = copy_etypes(context, context->etypes, &p->etypes);
	if (ret)
	    goto out;
    }
    if (context->etypes_des) {
	ret = copy_etypes(context, context->etypes_des, &p->etypes_des);
	if (ret)
	    goto out;
    }

    if (context->default_realms) {
	ret = krb5_copy_host_realm(context,
				   context->default_realms, &p->default_realms);
	if (ret)
	    goto out;
    }

    ret = _krb5_config_copy(context, context->cf, &p->cf);
    if (ret)
	goto out;

    /* XXX should copy */
    krb5_init_ets(p);

    cc_ops_copy(p, context);
    kt_ops_copy(p, context);

#if 0 /* XXX */
    if(context->warn_dest != NULL)
	;
    if(context->debug_dest != NULL)
	;
#endif

    ret = krb5_set_extra_addresses(p, context->extra_addresses);
    if (ret)
	goto out;
    ret = krb5_set_extra_addresses(p, context->ignore_addresses);
    if (ret)
	goto out;

    ret = _krb5_copy_send_to_kdc_func(p, context);
    if (ret)
	goto out;

    *out = p;

    return 0;

 out:
    krb5_free_context(p);
    return ret;
}

#endif

/**
 * Frees the krb5_context allocated by krb5_init_context().
 *
 * @param context context to be freed.
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_context(krb5_context context)
{
    if (context->default_cc_name)
	free(context->default_cc_name);
    if (context->default_cc_name_env)
	free(context->default_cc_name_env);
    free(context->etypes);
    free(context->etypes_des);
    krb5_free_host_realm (context, context->default_realms);
    krb5_config_file_free (context, context->cf);
    free_error_table (context->et_list);
    free(rk_UNCONST(context->cc_ops));
    free(context->kt_types);
    krb5_clear_error_message(context);
    if(context->warn_dest != NULL)
	krb5_closelog(context, context->warn_dest);
    if(context->debug_dest != NULL)
	krb5_closelog(context, context->debug_dest);
    krb5_set_extra_addresses(context, NULL);
    krb5_set_ignore_addresses(context, NULL);
    krb5_set_send_to_kdc_func(context, NULL, NULL);

#ifdef PKINIT
    if (context->hx509ctx)
	hx509_context_free(&context->hx509ctx);
#endif

    HEIMDAL_MUTEX_destroy(context->mutex);
    free(context->mutex);
    if (context->flags & KRB5_CTX_F_SOCKETS_INITIALIZED) {
 	rk_SOCK_EXIT();
    }

    memset(context, 0, sizeof(*context));
    free(context);
}

/**
 * Reinit the context from a new set of filenames.
 *
 * @param context context to add configuration too.
 * @param filenames array of filenames, end of list is indicated with a NULL filename.
 *
 * @return Returns 0 to indicate success.  Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_config_files(krb5_context context, char **filenames)
{
    krb5_error_code ret;
    krb5_config_binding *tmp = NULL;
    while(filenames != NULL && *filenames != NULL && **filenames != '\0') {
	ret = krb5_config_parse_file_multi(context, *filenames, &tmp);
	if(ret != 0 && ret != ENOENT && ret != EACCES && ret != EPERM) {
	    krb5_config_file_free(context, tmp);
	    return ret;
	}
	filenames++;
    }
#if 0
    /* with this enabled and if there are no config files, Kerberos is
       considererd disabled */
    if(tmp == NULL)
	return ENXIO;
#endif

#ifdef _WIN32
    _krb5_load_config_from_registry(context, &tmp);
#endif

    krb5_config_file_free(context, context->cf);
    context->cf = tmp;
    ret = init_context_from_config_file(context);
    return ret;
}

static krb5_error_code
add_file(char ***pfilenames, int *len, char *file)
{
    char **pp = *pfilenames;
    int i;

    for(i = 0; i < *len; i++) {
	if(strcmp(pp[i], file) == 0) {
	    free(file);
	    return 0;
	}
    }

    pp = realloc(*pfilenames, (*len + 2) * sizeof(*pp));
    if (pp == NULL) {
	free(file);
	return ENOMEM;
    }

    pp[*len] = file;
    pp[*len + 1] = NULL;
    *pfilenames = pp;
    *len += 1;
    return 0;
}

/*
 *  `pq' isn't free, it's up the the caller
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_prepend_config_files(const char *filelist, char **pq, char ***ret_pp)
{
    krb5_error_code ret;
    const char *p, *q;
    char **pp;
    int len;
    char *fn;

    pp = NULL;

    len = 0;
    p = filelist;
    while(1) {
	ssize_t l;
	q = p;
	l = strsep_copy(&q, PATH_SEP, NULL, 0);
	if(l == -1)
	    break;
	fn = malloc(l + 1);
	if(fn == NULL) {
	    krb5_free_config_files(pp);
	    return ENOMEM;
	}
	(void)strsep_copy(&p, PATH_SEP, fn, l + 1);
	ret = add_file(&pp, &len, fn);
	if (ret) {
	    krb5_free_config_files(pp);
	    return ret;
	}
    }

    if (pq != NULL) {
	int i;

	for (i = 0; pq[i] != NULL; i++) {
	    fn = strdup(pq[i]);
	    if (fn == NULL) {
		krb5_free_config_files(pp);
		return ENOMEM;
	    }
	    ret = add_file(&pp, &len, fn);
	    if (ret) {
		krb5_free_config_files(pp);
		return ret;
	    }
	}
    }

    *ret_pp = pp;
    return 0;
}

/**
 * Prepend the filename to the global configuration list.
 *
 * @param filelist a filename to add to the default list of filename
 * @param pfilenames return array of filenames, should be freed with krb5_free_config_files().
 *
 * @return Returns 0 to indicate success.  Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_prepend_config_files_default(const char *filelist, char ***pfilenames)
{
    krb5_error_code ret;
    char **defpp, **pp = NULL;

    ret = krb5_get_default_config_files(&defpp);
    if (ret)
	return ret;

    ret = krb5_prepend_config_files(filelist, defpp, &pp);
    krb5_free_config_files(defpp);
    if (ret) {
	return ret;
    }
    *pfilenames = pp;
    return 0;
}

#ifdef _WIN32

/**
 * Checks the registry for configuration file location
 *
 * Kerberos for Windows and other legacy Kerberos applications expect
 * to find the configuration file location in the
 * SOFTWARE\MIT\Kerberos registry key under the value "config".
 */
char *
_krb5_get_default_config_config_files_from_registry()
{
    static const char * KeyName = "Software\\MIT\\Kerberos";
    char *config_file = NULL;
    LONG rcode;
    HKEY key;

    rcode = RegOpenKeyEx(HKEY_CURRENT_USER, KeyName, 0, KEY_READ, &key);
    if (rcode == ERROR_SUCCESS) {
        config_file = _krb5_parse_reg_value_as_multi_string(NULL, key, "config",
                                                            REG_NONE, 0, PATH_SEP);
        RegCloseKey(key);
    }

    if (config_file)
        return config_file;

    rcode = RegOpenKeyEx(HKEY_LOCAL_MACHINE, KeyName, 0, KEY_READ, &key);
    if (rcode == ERROR_SUCCESS) {
        config_file = _krb5_parse_reg_value_as_multi_string(NULL, key, "config",
                                                            REG_NONE, 0, PATH_SEP);
        RegCloseKey(key);
    }

    return config_file;
}

#endif

/**
 * Get the global configuration list.
 *
 * @param pfilenames return array of filenames, should be freed with krb5_free_config_files().
 *
 * @return Returns 0 to indicate success.  Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_default_config_files(char ***pfilenames)
{
    const char *files = NULL;

    if (pfilenames == NULL)
        return EINVAL;
    if(!issuid())
	files = getenv("KRB5_CONFIG");

#ifdef _WIN32
    if (files == NULL) {
        char * reg_files;
        reg_files = _krb5_get_default_config_config_files_from_registry();
        if (reg_files != NULL) {
            krb5_error_code code;

            code = krb5_prepend_config_files(reg_files, NULL, pfilenames);
            free(reg_files);

            return code;
        }
    }
#endif

    if (files == NULL)
	files = krb5_config_file;

    return krb5_prepend_config_files(files, NULL, pfilenames);
}

/**
 * Free a list of configuration files.
 *
 * @param filenames list, terminated with a NULL pointer, to be
 * freed. NULL is an valid argument.
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_config_files(char **filenames)
{
    char **p;
    for(p = filenames; p && *p != NULL; p++)
	free(*p);
    free(filenames);
}

/**
 * Returns the list of Kerberos encryption types sorted in order of
 * most preferred to least preferred encryption type.  Note that some
 * encryption types might be disabled, so you need to check with
 * krb5_enctype_valid() before using the encryption type.
 *
 * @return list of enctypes, terminated with ETYPE_NULL. Its a static
 * array completed into the Kerberos library so the content doesn't
 * need to be freed.
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION const krb5_enctype * KRB5_LIB_CALL
krb5_kerberos_enctypes(krb5_context context)
{
    static const krb5_enctype p[] = {
	ETYPE_AES256_CTS_HMAC_SHA1_96,
	ETYPE_AES128_CTS_HMAC_SHA1_96,
	ETYPE_DES3_CBC_SHA1,
	ETYPE_DES3_CBC_MD5,
	ETYPE_ARCFOUR_HMAC_MD5,
	ETYPE_DES_CBC_MD5,
	ETYPE_DES_CBC_MD4,
	ETYPE_DES_CBC_CRC,
	ETYPE_NULL
    };
    return p;
}

/*
 *
 */

static krb5_error_code
copy_enctypes(krb5_context context,
	      const krb5_enctype *in,
	      krb5_enctype **out)
{
    krb5_enctype *p = NULL;
    size_t m, n;

    for (n = 0; in[n]; n++)
	;
    n++;
    ALLOC(p, n);
    if(p == NULL)
	return krb5_enomem(context);
    for (n = 0, m = 0; in[n]; n++) {
	if (krb5_enctype_valid(context, in[n]) != 0)
	    continue;
	p[m++] = in[n];
    }
    p[m] = KRB5_ENCTYPE_NULL;
    if (m == 0) {
	free(p);
	krb5_set_error_message (context, KRB5_PROG_ETYPE_NOSUPP,
				N_("no valid enctype set", ""));
	return KRB5_PROG_ETYPE_NOSUPP;
    }
    *out = p;
    return 0;
}


/*
 * set `etype' to a malloced list of the default enctypes
 */

static krb5_error_code
default_etypes(krb5_context context, krb5_enctype **etype)
{
    const krb5_enctype *p = krb5_kerberos_enctypes(context);
    return copy_enctypes(context, p, etype);
}

/**
 * Set the default encryption types that will be use in communcation
 * with the KDC, clients and servers.
 *
 * @param context Kerberos 5 context.
 * @param etypes Encryption types, array terminated with ETYPE_NULL (0).
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_default_in_tkt_etypes(krb5_context context,
			       const krb5_enctype *etypes)
{
    krb5_error_code ret;
    krb5_enctype *p = NULL;

    if(etypes) {
	ret = copy_enctypes(context, etypes, &p);
	if (ret)
	    return ret;
    }
    if(context->etypes)
	free(context->etypes);
    context->etypes = p;
    return 0;
}

/**
 * Get the default encryption types that will be use in communcation
 * with the KDC, clients and servers.
 *
 * @param context Kerberos 5 context.
 * @param etypes Encryption types, array terminated with
 * ETYPE_NULL(0), caller should free array with krb5_xfree():
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_default_in_tkt_etypes(krb5_context context,
			       krb5_pdu pdu_type,
			       krb5_enctype **etypes)
{
    krb5_enctype *enctypes = NULL;
    krb5_error_code ret;
    krb5_enctype *p;

    heim_assert(pdu_type == KRB5_PDU_AS_REQUEST || 
		pdu_type == KRB5_PDU_TGS_REQUEST ||
		pdu_type == KRB5_PDU_NONE, "pdu contant not as expected");

    if (pdu_type == KRB5_PDU_AS_REQUEST && context->as_etypes != NULL)
	enctypes = context->as_etypes;
    else if (pdu_type == KRB5_PDU_TGS_REQUEST && context->tgs_etypes != NULL)
	enctypes = context->tgs_etypes;
    else if (context->etypes != NULL)
	enctypes = context->etypes;

    if (enctypes != NULL) {
	ret = copy_enctypes(context, enctypes, &p);
	if (ret)
	    return ret;
    } else {
	ret = default_etypes(context, &p);
	if (ret)
	    return ret;
    }
    *etypes = p;
    return 0;
}

/**
 * Init the built-in ets in the Kerberos library.
 *
 * @param context kerberos context to add the ets too
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_init_ets(krb5_context context)
{
    if(context->et_list == NULL){
	krb5_add_et_list(context, initialize_krb5_error_table_r);
	krb5_add_et_list(context, initialize_asn1_error_table_r);
	krb5_add_et_list(context, initialize_heim_error_table_r);

	krb5_add_et_list(context, initialize_k524_error_table_r);

#ifdef COM_ERR_BINDDOMAIN_krb5
	bindtextdomain(COM_ERR_BINDDOMAIN_krb5, HEIMDAL_LOCALEDIR);
	bindtextdomain(COM_ERR_BINDDOMAIN_asn1, HEIMDAL_LOCALEDIR);
	bindtextdomain(COM_ERR_BINDDOMAIN_heim, HEIMDAL_LOCALEDIR);
	bindtextdomain(COM_ERR_BINDDOMAIN_k524, HEIMDAL_LOCALEDIR);
#endif

#ifdef PKINIT
	krb5_add_et_list(context, initialize_hx_error_table_r);
#ifdef COM_ERR_BINDDOMAIN_hx
	bindtextdomain(COM_ERR_BINDDOMAIN_hx, HEIMDAL_LOCALEDIR);
#endif
#endif
    }
}

/**
 * Make the kerberos library default to the admin KDC.
 *
 * @param context Kerberos 5 context.
 * @param flag boolean flag to select if the use the admin KDC or not.
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_set_use_admin_kdc (krb5_context context, krb5_boolean flag)
{
    context->use_admin_kdc = flag;
}

/**
 * Make the kerberos library default to the admin KDC.
 *
 * @param context Kerberos 5 context.
 *
 * @return boolean flag to telling the context will use admin KDC as the default KDC.
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_get_use_admin_kdc (krb5_context context)
{
    return context->use_admin_kdc;
}

/**
 * Add extra address to the address list that the library will add to
 * the client's address list when communicating with the KDC.
 *
 * @param context Kerberos 5 context.
 * @param addresses addreses to add
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_add_extra_addresses(krb5_context context, krb5_addresses *addresses)
{

    if(context->extra_addresses)
	return krb5_append_addresses(context,
				     context->extra_addresses, addresses);
    else
	return krb5_set_extra_addresses(context, addresses);
}

/**
 * Set extra address to the address list that the library will add to
 * the client's address list when communicating with the KDC.
 *
 * @param context Kerberos 5 context.
 * @param addresses addreses to set
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_extra_addresses(krb5_context context, const krb5_addresses *addresses)
{
    if(context->extra_addresses)
	krb5_free_addresses(context, context->extra_addresses);

    if(addresses == NULL) {
	if(context->extra_addresses != NULL) {
	    free(context->extra_addresses);
	    context->extra_addresses = NULL;
	}
	return 0;
    }
    if(context->extra_addresses == NULL) {
	context->extra_addresses = malloc(sizeof(*context->extra_addresses));
	if(context->extra_addresses == NULL) {
	    krb5_set_error_message (context, ENOMEM, N_("malloc: out of memory", ""));
	    return ENOMEM;
	}
    }
    return krb5_copy_addresses(context, addresses, context->extra_addresses);
}

/**
 * Get extra address to the address list that the library will add to
 * the client's address list when communicating with the KDC.
 *
 * @param context Kerberos 5 context.
 * @param addresses addreses to set
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_extra_addresses(krb5_context context, krb5_addresses *addresses)
{
    if(context->extra_addresses == NULL) {
	memset(addresses, 0, sizeof(*addresses));
	return 0;
    }
    return krb5_copy_addresses(context,context->extra_addresses, addresses);
}

/**
 * Add extra addresses to ignore when fetching addresses from the
 * underlaying operating system.
 *
 * @param context Kerberos 5 context.
 * @param addresses addreses to ignore
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_add_ignore_addresses(krb5_context context, krb5_addresses *addresses)
{

    if(context->ignore_addresses)
	return krb5_append_addresses(context,
				     context->ignore_addresses, addresses);
    else
	return krb5_set_ignore_addresses(context, addresses);
}

/**
 * Set extra addresses to ignore when fetching addresses from the
 * underlaying operating system.
 *
 * @param context Kerberos 5 context.
 * @param addresses addreses to ignore
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_ignore_addresses(krb5_context context, const krb5_addresses *addresses)
{
    if(context->ignore_addresses)
	krb5_free_addresses(context, context->ignore_addresses);
    if(addresses == NULL) {
	if(context->ignore_addresses != NULL) {
	    free(context->ignore_addresses);
	    context->ignore_addresses = NULL;
	}
	return 0;
    }
    if(context->ignore_addresses == NULL) {
	context->ignore_addresses = malloc(sizeof(*context->ignore_addresses));
	if(context->ignore_addresses == NULL) {
	    krb5_set_error_message (context, ENOMEM, N_("malloc: out of memory", ""));
	    return ENOMEM;
	}
    }
    return krb5_copy_addresses(context, addresses, context->ignore_addresses);
}

/**
 * Get extra addresses to ignore when fetching addresses from the
 * underlaying operating system.
 *
 * @param context Kerberos 5 context.
 * @param addresses list addreses ignored
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_ignore_addresses(krb5_context context, krb5_addresses *addresses)
{
    if(context->ignore_addresses == NULL) {
	memset(addresses, 0, sizeof(*addresses));
	return 0;
    }
    return krb5_copy_addresses(context, context->ignore_addresses, addresses);
}

/**
 * Set version of fcache that the library should use.
 *
 * @param context Kerberos 5 context.
 * @param version version number.
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_fcache_version(krb5_context context, int version)
{
    context->fcache_vno = version;
    return 0;
}

/**
 * Get version of fcache that the library should use.
 *
 * @param context Kerberos 5 context.
 * @param version version number.
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_fcache_version(krb5_context context, int *version)
{
    *version = context->fcache_vno;
    return 0;
}

/**
 * Runtime check if the Kerberos library was complied with thread support.
 *
 * @return TRUE if the library was compiled with thread support, FALSE if not.
 *
 * @ingroup krb5
 */


KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_is_thread_safe(void)
{
#ifdef ENABLE_PTHREAD_SUPPORT
    return TRUE;
#else
    return FALSE;
#endif
}

/**
 * Set if the library should use DNS to canonicalize hostnames.
 *
 * @param context Kerberos 5 context.
 * @param flag if its dns canonicalizion is used or not.
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_set_dns_canonicalize_hostname (krb5_context context, krb5_boolean flag)
{
    if (flag)
	context->flags |= KRB5_CTX_F_DNS_CANONICALIZE_HOSTNAME;
    else
	context->flags &= ~KRB5_CTX_F_DNS_CANONICALIZE_HOSTNAME;
}

/**
 * Get if the library uses DNS to canonicalize hostnames.
 *
 * @param context Kerberos 5 context.
 *
 * @return return non zero if the library uses DNS to canonicalize hostnames.
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_get_dns_canonicalize_hostname (krb5_context context)
{
    return (context->flags & KRB5_CTX_F_DNS_CANONICALIZE_HOSTNAME) ? 1 : 0;
}

/**
 * Get current offset in time to the KDC.
 *
 * @param context Kerberos 5 context.
 * @param sec seconds part of offset.
 * @param usec micro seconds part of offset.
 *
 * @return returns zero
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_kdc_sec_offset (krb5_context context, int32_t *sec, int32_t *usec)
{
    if (sec)
	*sec = context->kdc_sec_offset;
    if (usec)
	*usec = context->kdc_usec_offset;
    return 0;
}

/**
 * Set current offset in time to the KDC.
 *
 * @param context Kerberos 5 context.
 * @param sec seconds part of offset.
 * @param usec micro seconds part of offset.
 *
 * @return returns zero
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_kdc_sec_offset (krb5_context context, int32_t sec, int32_t usec)
{
    context->kdc_sec_offset = sec;
    if (usec >= 0)
	context->kdc_usec_offset = usec;
    return 0;
}

/**
 * Get max time skew allowed.
 *
 * @param context Kerberos 5 context.
 *
 * @return timeskew in seconds.
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION time_t KRB5_LIB_CALL
krb5_get_max_time_skew (krb5_context context)
{
    return context->max_skew;
}

/**
 * Set max time skew allowed.
 *
 * @param context Kerberos 5 context.
 * @param t timeskew in seconds.
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_set_max_time_skew (krb5_context context, time_t t)
{
    context->max_skew = t;
}

/*
 * Init encryption types in len, val with etypes.
 *
 * @param context Kerberos 5 context.
 * @param pdu_type type of pdu
 * @param len output length of val.
 * @param val output array of enctypes.
 * @param etypes etypes to set val and len to, if NULL, use default enctypes.

 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
_krb5_init_etype(krb5_context context,
		 krb5_pdu pdu_type,
		 unsigned *len,
		 krb5_enctype **val,
		 const krb5_enctype *etypes)
{
    krb5_error_code ret;

    if (etypes == NULL)
	ret = krb5_get_default_in_tkt_etypes(context, pdu_type, val);
    else
	ret = copy_enctypes(context, etypes, val);
    if (ret)
	return ret;

    if (len) {
	*len = 0;
	while ((*val)[*len] != KRB5_ENCTYPE_NULL)
	    (*len)++;
    }
    return 0;
}

/*
 * Allow homedir accces
 */

static HEIMDAL_MUTEX homedir_mutex = HEIMDAL_MUTEX_INITIALIZER;
static krb5_boolean allow_homedir = TRUE;

krb5_boolean
_krb5_homedir_access(krb5_context context)
{
    krb5_boolean allow;

#ifdef HAVE_GETEUID
    /* is never allowed for root */
    if (geteuid() == 0)
	return FALSE;
#endif

    if (context && (context->flags & KRB5_CTX_F_HOMEDIR_ACCESS) == 0)
	return FALSE;

    HEIMDAL_MUTEX_lock(&homedir_mutex);
    allow = allow_homedir;
    HEIMDAL_MUTEX_unlock(&homedir_mutex);
    return allow;
}

/**
 * Enable and disable home directory access on either the global state
 * or the krb5_context state. By calling krb5_set_home_dir_access()
 * with context set to NULL, the global state is configured otherwise
 * the state for the krb5_context is modified.
 *
 * For home directory access to be allowed, both the global state and
 * the krb5_context state have to be allowed.
 *
 * Administrator (root user), never uses the home directory.
 *
 * @param context a Kerberos 5 context or NULL
 * @param allow allow if TRUE home directory
 * @return the old value
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_set_home_dir_access(krb5_context context, krb5_boolean allow)
{
    krb5_boolean old;
    if (context) {
	old = (context->flags & KRB5_CTX_F_HOMEDIR_ACCESS) ? TRUE : FALSE;
	if (allow)
	    context->flags |= KRB5_CTX_F_HOMEDIR_ACCESS;
	else
	    context->flags &= ~KRB5_CTX_F_HOMEDIR_ACCESS;
    } else {
	HEIMDAL_MUTEX_lock(&homedir_mutex);
	old = allow_homedir;
	allow_homedir = allow;
	HEIMDAL_MUTEX_unlock(&homedir_mutex);
    }

    return old;
}
