/*
 * Copyright (c) 1997-2001 Kungliga Tekniska Högskolan
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
#include <vis.h>

struct krb5_rcache_data {
    char *name;
};

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_resolve(krb5_context context,
		krb5_rcache id,
		const char *name)
{
    id->name = strdup(name);
    if(id->name == NULL) {
	krb5_set_error_message(context, KRB5_RC_MALLOC,
			       N_("malloc: out of memory", ""));
	return KRB5_RC_MALLOC;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_resolve_type(krb5_context context,
		     krb5_rcache *id,
		     const char *type)
{
    *id = NULL;
    if(strcmp(type, "FILE")) {
	krb5_set_error_message (context, KRB5_RC_TYPE_NOTFOUND,
				N_("replay cache type %s not supported", ""),
				type);
	return KRB5_RC_TYPE_NOTFOUND;
    }
    *id = calloc(1, sizeof(**id));
    if(*id == NULL) {
	krb5_set_error_message(context, KRB5_RC_MALLOC,
			       N_("malloc: out of memory", ""));
	return KRB5_RC_MALLOC;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_resolve_full(krb5_context context,
		     krb5_rcache *id,
		     const char *string_name)
{
    krb5_error_code ret;

    *id = NULL;

    if(strncmp(string_name, "FILE:", 5)) {
	krb5_set_error_message(context, KRB5_RC_TYPE_NOTFOUND,
			       N_("replay cache type %s not supported", ""),
			       string_name);
	return KRB5_RC_TYPE_NOTFOUND;
    }
    ret = krb5_rc_resolve_type(context, id, "FILE");
    if(ret)
	return ret;
    ret = krb5_rc_resolve(context, *id, string_name + 5);
    if (ret) {
	krb5_rc_close(context, *id);
	*id = NULL;
    }
    return ret;
}

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_rc_default_name(krb5_context context)
{
    return "FILE:/var/run/default_rcache";
}

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_rc_default_type(krb5_context context)
{
    return "FILE";
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_default(krb5_context context,
		krb5_rcache *id)
{
    return krb5_rc_resolve_full(context, id, krb5_rc_default_name(context));
}

struct rc_entry{
    time_t stamp;
    unsigned char data[16];
};

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_initialize(krb5_context context,
		   krb5_rcache id,
		   krb5_deltat auth_lifespan)
{
    FILE *f = fopen(id->name, "w");
    struct rc_entry tmp;
    int ret;

    if(f == NULL) {
	char buf[128];
	ret = errno;
	rk_strerror_r(ret, buf, sizeof(buf));
	krb5_set_error_message(context, ret, "open(%s): %s", id->name, buf);
	return ret;
    }
    tmp.stamp = auth_lifespan;
    fwrite(&tmp, 1, sizeof(tmp), f);
    fclose(f);
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_recover(krb5_context context,
		krb5_rcache id)
{
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_destroy(krb5_context context,
		krb5_rcache id)
{
    int ret;

    if(remove(id->name) < 0) {
	char buf[128];
	ret = errno;
	rk_strerror_r(ret, buf, sizeof(buf));
	krb5_set_error_message(context, ret, "remove(%s): %s", id->name, buf);
	return ret;
    }
    return krb5_rc_close(context, id);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_close(krb5_context context,
	      krb5_rcache id)
{
    free(id->name);
    free(id);
    return 0;
}

static void
checksum_authenticator(Authenticator *auth, void *data)
{
    EVP_MD_CTX *m = EVP_MD_CTX_create();
    unsigned i;

    EVP_DigestInit_ex(m, EVP_md5(), NULL);

    EVP_DigestUpdate(m, auth->crealm, strlen(auth->crealm));
    for(i = 0; i < auth->cname.name_string.len; i++)
	EVP_DigestUpdate(m, auth->cname.name_string.val[i],
		   strlen(auth->cname.name_string.val[i]));
    EVP_DigestUpdate(m, &auth->ctime, sizeof(auth->ctime));
    EVP_DigestUpdate(m, &auth->cusec, sizeof(auth->cusec));

    EVP_DigestFinal_ex(m, data, NULL);
    EVP_MD_CTX_destroy(m);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_store(krb5_context context,
	      krb5_rcache id,
	      krb5_donot_replay *rep)
{
    struct rc_entry ent, tmp;
    time_t t;
    FILE *f;
    int ret;

    ent.stamp = time(NULL);
    checksum_authenticator(rep, ent.data);
    f = fopen(id->name, "r");
    if(f == NULL) {
	char buf[128];
	ret = errno;
	rk_strerror_r(ret, buf, sizeof(buf));
	krb5_set_error_message(context, ret, "open(%s): %s", id->name, buf);
	return ret;
    }
    rk_cloexec_file(f);
    fread(&tmp, sizeof(ent), 1, f);
    t = ent.stamp - tmp.stamp;
    while(fread(&tmp, sizeof(ent), 1, f)){
	if(tmp.stamp < t)
	    continue;
	if(memcmp(tmp.data, ent.data, sizeof(ent.data)) == 0){
	    fclose(f);
	    krb5_clear_error_message (context);
	    return KRB5_RC_REPLAY;
	}
    }
    if(ferror(f)){
	char buf[128];
	ret = errno;
	fclose(f);
	rk_strerror_r(ret, buf, sizeof(buf));
	krb5_set_error_message(context, ret, "%s: %s",
			       id->name, buf);
	return ret;
    }
    fclose(f);
    f = fopen(id->name, "a");
    if(f == NULL) {
	char buf[128];
	rk_strerror_r(errno, buf, sizeof(buf));
	krb5_set_error_message(context, KRB5_RC_IO_UNKNOWN,
			       "open(%s): %s", id->name, buf);
	return KRB5_RC_IO_UNKNOWN;
    }
    fwrite(&ent, 1, sizeof(ent), f);
    fclose(f);
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_expunge(krb5_context context,
		krb5_rcache id)
{
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rc_get_lifespan(krb5_context context,
		     krb5_rcache id,
		     krb5_deltat *auth_lifespan)
{
    FILE *f = fopen(id->name, "r");
    int r;
    struct rc_entry ent;
    r = fread(&ent, sizeof(ent), 1, f);
    fclose(f);
    if(r){
	*auth_lifespan = ent.stamp;
	return 0;
    }
    krb5_clear_error_message (context);
    return KRB5_RC_IO_UNKNOWN;
}

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_rc_get_name(krb5_context context,
		 krb5_rcache id)
{
    return id->name;
}

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_rc_get_type(krb5_context context,
		 krb5_rcache id)
{
    return "FILE";
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_server_rcache(krb5_context context,
		       const krb5_data *piece,
		       krb5_rcache *id)
{
    krb5_rcache rcache;
    krb5_error_code ret;

    char *tmp = malloc(4 * piece->length + 1);
    char *name;

    if(tmp == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    strvisx(tmp, piece->data, piece->length, VIS_WHITE | VIS_OCTAL);
#ifdef HAVE_GETEUID
    ret = asprintf(&name, "FILE:rc_%s_%u", tmp, (unsigned)geteuid());
#else
    ret = asprintf(&name, "FILE:rc_%s", tmp);
#endif
    free(tmp);
    if(ret < 0 || name == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    ret = krb5_rc_resolve_full(context, &rcache, name);
    free(name);
    if(ret)
	return ret;
    *id = rcache;
    return ret;
}
