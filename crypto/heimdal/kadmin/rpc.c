/*
 * Copyright (c) 2008 Kungliga Tekniska Högskolan
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

#include "kadmin_locl.h"

#include <gssapi/gssapi.h>
//#include <gssapi_krb5.h>
//#include <gssapi_spnego.h>

static gss_OID_desc krb5_mechanism =
{9, (void *)(uintptr_t) "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02"};
#define GSS_KRB5_MECHANISM (&krb5_mechanism)

#define CHECK(x)							\
	do {								\
		int __r;						\
		if ((__r = (x))) {					\
			krb5_errx(dcontext, 1, "Failed (%d) on %s:%d",	\
			    __r, __FILE__, __LINE__);			\
		}							\
	} while(0)

static krb5_context dcontext;

#define INSIST(x) CHECK(!(x))

#define VERSION2 0x12345702

#define LAST_FRAGMENT 0x80000000

#define RPC_VERSION 2
#define KADM_SERVER 2112
#define VVERSION 2
#define FLAVOR_GSS 6
#define FLAVOR_GSS_VERSION 1

struct opaque_auth {
    uint32_t flavor;
    krb5_data data;
};

struct call_header {
    uint32_t xid;
    uint32_t rpcvers;
    uint32_t prog;
    uint32_t vers;
    uint32_t proc;
    struct opaque_auth cred;
    struct opaque_auth verf;
};

enum {
    RPG_DATA = 0,
    RPG_INIT = 1,
    RPG_CONTINUE_INIT = 2,
    RPG_DESTROY = 3
};

enum {
    rpg_privacy = 3
};

/*
struct chrand_ret {
	krb5_ui_4 api_version;
	kadm5_ret_t ret;
	int n_keys;
	krb5_keyblock *keys;
};
*/


struct gcred {
    uint32_t version;
    uint32_t proc;
    uint32_t seq_num;
    uint32_t service;
    krb5_data handle;
};

static int
parse_name(const unsigned char *p, size_t len,
	   const gss_OID oid, char **name)
{
    size_t l;

    if (len < 4)
	return 1;

    /* TOK_ID */
    if (memcmp(p, "\x04\x01", 2) != 0)
	return 1;
    len -= 2;
    p += 2;

    /* MECH_LEN */
    l = (p[0] << 8) | p[1];
    len -= 2;
    p += 2;
    if (l < 2 || len < l)
	return 1;

    /* oid wrapping */
    if (p[0] != 6 || p[1] != l - 2)
	return 1;
    p += 2;
    l -= 2;
    len -= 2;

    /* MECH */
    if (l != oid->length || memcmp(p, oid->elements, oid->length) != 0)
	return 1;
    len -= l;
    p += l;

    /* MECHNAME_LEN */
    if (len < 4)
	return 1;
    l = p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
    len -= 4;
    p += 4;

    /* MECH NAME */
    if (len != l)
	return 1;

    *name = malloc(l + 1);
    INSIST(*name != NULL);
    memcpy(*name, p, l);
    (*name)[l] = '\0';

    return 0;
}



static void
gss_error(krb5_context contextp,
	  gss_OID mech, OM_uint32 type, OM_uint32 error)
{
    OM_uint32 new_stat;
    OM_uint32 msg_ctx = 0;
    gss_buffer_desc status_string;
    OM_uint32 ret;

    do {
	ret = gss_display_status (&new_stat,
				  error,
				  type,
				  mech,
				  &msg_ctx,
				  &status_string);
	krb5_warnx(contextp, "%.*s",
		   (int)status_string.length,
		   (char *)status_string.value);
	gss_release_buffer (&new_stat, &status_string);
    } while (!GSS_ERROR(ret) && msg_ctx != 0);
}

static void
gss_print_errors (krb5_context contextp,
		  OM_uint32 maj_stat, OM_uint32 min_stat)
{
    gss_error(contextp, GSS_C_NO_OID, GSS_C_GSS_CODE, maj_stat);
    gss_error(contextp, GSS_C_NO_OID, GSS_C_MECH_CODE, min_stat);
}

static int
read_data(krb5_storage *sp, krb5_storage *msg, size_t len)
{
    char buf[1024];

    while (len) {
	size_t tlen = len;
	ssize_t slen;

	if (tlen > sizeof(buf))
	    tlen = sizeof(buf);

	slen = krb5_storage_read(sp, buf, tlen);
	INSIST((size_t)slen == tlen);

	slen = krb5_storage_write(msg, buf, tlen);
	INSIST((size_t)slen == tlen);

	len -= tlen;
    }
    return 0;
}

static int
collect_framents(krb5_storage *sp, krb5_storage *msg)
{
    krb5_error_code ret;
    uint32_t len;
    int last_fragment;
    size_t total_len = 0;

    do {
	ret = krb5_ret_uint32(sp, &len);
	if (ret)
	    return ret;

	last_fragment = (len & LAST_FRAGMENT);
	len &= ~LAST_FRAGMENT;

	CHECK(read_data(sp, msg, len));
	total_len += len;

    } while(!last_fragment || total_len == 0);

    return 0;
}

static krb5_error_code
store_data_xdr(krb5_storage *sp, krb5_data data)
{
    krb5_error_code ret;
    size_t res;

    ret = krb5_store_data(sp, data);
    if (ret)
	return ret;
    res = 4 - (data.length % 4);
    if (res != 4) {
	static const char zero[4] = { 0, 0, 0, 0 };

	ret = krb5_storage_write(sp, zero, res);
	if((size_t)ret != res)
	    return (ret < 0)? errno : krb5_storage_get_eof_code(sp);
    }
    return 0;
}

static krb5_error_code
ret_data_xdr(krb5_storage *sp, krb5_data *data)
{
    krb5_error_code ret;
    ret = krb5_ret_data(sp, data);
    if (ret)
	return ret;

    if ((data->length % 4) != 0) {
	char buf[4];
	size_t res;

	res = 4 - (data->length % 4);
	if (res != 4) {
	    ret = krb5_storage_read(sp, buf, res);
	    if((size_t)ret != res)
		return (ret < 0)? errno : krb5_storage_get_eof_code(sp);
	}
    }
    return 0;
}

static krb5_error_code
ret_auth_opaque(krb5_storage *msg, struct opaque_auth *ao)
{
    krb5_error_code ret;
    ret = krb5_ret_uint32(msg, &ao->flavor);
    if (ret) return ret;
    ret = ret_data_xdr(msg, &ao->data);
    return ret;
}

static int
ret_gcred(krb5_data *data, struct gcred *gcred)
{
    krb5_storage *sp;

    memset(gcred, 0, sizeof(*gcred));

    sp = krb5_storage_from_data(data);
    INSIST(sp != NULL);

    CHECK(krb5_ret_uint32(sp, &gcred->version));
    CHECK(krb5_ret_uint32(sp, &gcred->proc));
    CHECK(krb5_ret_uint32(sp, &gcred->seq_num));
    CHECK(krb5_ret_uint32(sp, &gcred->service));
    CHECK(ret_data_xdr(sp, &gcred->handle));

    krb5_storage_free(sp);

    return 0;
}

static krb5_error_code
store_gss_init_res(krb5_storage *sp, krb5_data handle,
		   OM_uint32 maj_stat, OM_uint32 min_stat,
		   uint32_t seq_window, gss_buffer_t gout)
{
    krb5_error_code ret;
    krb5_data out;

    out.data = gout->value;
    out.length = gout->length;

    ret = store_data_xdr(sp, handle);
    if (ret) return ret;
    ret = krb5_store_uint32(sp, maj_stat);
    if (ret) return ret;
    ret = krb5_store_uint32(sp, min_stat);
    if (ret) return ret;
    ret = store_data_xdr(sp, out);
    return ret;
}

static int
store_string_xdr(krb5_storage *sp, const char *str)
{
    krb5_data c;
    if (str) {
	c.data = rk_UNCONST(str);
	c.length = strlen(str) + 1;
    } else
	krb5_data_zero(&c);

    return store_data_xdr(sp, c);
}

static int
ret_string_xdr(krb5_storage *sp, char **str)
{
    krb5_data c;
    *str = NULL;
    CHECK(ret_data_xdr(sp, &c));
    if (c.length) {
	*str = malloc(c.length + 1);
	INSIST(*str != NULL);
	memcpy(*str, c.data, c.length);
	(*str)[c.length] = '\0';
    }
    krb5_data_free(&c);
    return 0;
}

static int
store_principal_xdr(krb5_context contextp,
		    krb5_storage *sp,
		    krb5_principal p)
{
    char *str;
    CHECK(krb5_unparse_name(contextp, p, &str));
    CHECK(store_string_xdr(sp, str));
    free(str);
    return 0;
}

static int
ret_principal_xdr(krb5_context contextp,
		  krb5_storage *sp,
		  krb5_principal *p)
{
    char *str;
    *p = NULL;
    CHECK(ret_string_xdr(sp, &str));
    if (str) {
	CHECK(krb5_parse_name(contextp, str, p));
	free(str);
    }
    return 0;
}

static int
store_principal_ent(krb5_context contextp,
		    krb5_storage *sp,
		    kadm5_principal_ent_rec *ent)
{
    int i;

    CHECK(store_principal_xdr(contextp, sp, ent->principal));
    CHECK(krb5_store_uint32(sp, ent->princ_expire_time));
    CHECK(krb5_store_uint32(sp, ent->pw_expiration));
    CHECK(krb5_store_uint32(sp, ent->last_pwd_change));
    CHECK(krb5_store_uint32(sp, ent->max_life));
    CHECK(krb5_store_int32(sp, ent->mod_name == NULL));
    if (ent->mod_name)
	CHECK(store_principal_xdr(contextp, sp, ent->mod_name));
    CHECK(krb5_store_uint32(sp, ent->mod_date));
    CHECK(krb5_store_uint32(sp, ent->attributes));
    CHECK(krb5_store_uint32(sp, ent->kvno));
    CHECK(krb5_store_uint32(sp, ent->mkvno));
    CHECK(store_string_xdr(sp, ent->policy));
    CHECK(krb5_store_int32(sp, ent->aux_attributes));
    CHECK(krb5_store_int32(sp, ent->max_renewable_life));
    CHECK(krb5_store_int32(sp, ent->last_success));
    CHECK(krb5_store_int32(sp, ent->last_failed));
    CHECK(krb5_store_int32(sp, ent->fail_auth_count));
    CHECK(krb5_store_int32(sp, ent->n_key_data));
    CHECK(krb5_store_int32(sp, ent->n_tl_data));
    CHECK(krb5_store_int32(sp, ent->n_tl_data == 0));
    if (ent->n_tl_data) {
	krb5_tl_data *tp;

	for (tp = ent->tl_data; tp; tp = tp->tl_data_next) {
	    krb5_data c;
	    c.length = tp->tl_data_length;
	    c.data = tp->tl_data_contents;

	    CHECK(krb5_store_int32(sp, 0)); /* last item */
	    CHECK(krb5_store_int32(sp, tp->tl_data_type));
	    CHECK(store_data_xdr(sp, c));
	}
	CHECK(krb5_store_int32(sp, 1)); /* last item */
    }

    CHECK(krb5_store_int32(sp, ent->n_key_data));
    for (i = 0; i < ent->n_key_data; i++) {
	CHECK(krb5_store_uint32(sp, 2));
	CHECK(krb5_store_uint32(sp, ent->kvno));
	CHECK(krb5_store_uint32(sp, ent->key_data[i].key_data_type[0]));
	CHECK(krb5_store_uint32(sp, ent->key_data[i].key_data_type[1]));
    }

    return 0;
}

static int
ret_principal_ent(krb5_context contextp,
		  krb5_storage *sp,
		  kadm5_principal_ent_rec *ent)
{
    uint32_t flag, num;
    size_t i;

    memset(ent, 0, sizeof(*ent));

    CHECK(ret_principal_xdr(contextp, sp, &ent->principal));
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->princ_expire_time = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->pw_expiration = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->last_pwd_change = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->max_life = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    if (flag == 0)
	ret_principal_xdr(contextp, sp, &ent->mod_name);
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->mod_date = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->attributes = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->kvno = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->mkvno = flag;
    CHECK(ret_string_xdr(sp, &ent->policy));
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->aux_attributes = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->max_renewable_life = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->last_success = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->last_failed = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->fail_auth_count = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->n_key_data = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    ent->n_tl_data = flag;
    CHECK(krb5_ret_uint32(sp, &flag));
    if (flag == 0) {
	krb5_tl_data **tp = &ent->tl_data;
	size_t count = 0;

	while(1) {
	    krb5_data c;
	    CHECK(krb5_ret_uint32(sp, &flag)); /* last item */
	    if (flag)
		break;
	    *tp = calloc(1, sizeof(**tp));
	    INSIST(*tp != NULL);
	    CHECK(krb5_ret_uint32(sp, &flag));
	    (*tp)->tl_data_type = flag;
	    CHECK(ret_data_xdr(sp, &c));
	    (*tp)->tl_data_length = c.length;
	    (*tp)->tl_data_contents = c.data;
	    tp = &(*tp)->tl_data_next;

	    count++;
	}
	INSIST((size_t)ent->n_tl_data == count);
    } else {
	INSIST(ent->n_tl_data == 0);
    }

    CHECK(krb5_ret_uint32(sp, &num));
    INSIST(num == (uint32_t)ent->n_key_data);

    ent->key_data = calloc(num, sizeof(ent->key_data[0]));
    INSIST(ent->key_data != NULL);

    for (i = 0; i < num; i++) {
	CHECK(krb5_ret_uint32(sp, &flag)); /* data version */
	INSIST(flag > 1);
	CHECK(krb5_ret_uint32(sp, &flag));
	ent->kvno = flag;
	CHECK(krb5_ret_uint32(sp, &flag));
	ent->key_data[i].key_data_type[0] = flag;
	CHECK(krb5_ret_uint32(sp, &flag));
	ent->key_data[i].key_data_type[1] = flag;
    }

    return 0;
}

/*
 *
 */

static void
proc_create_principal(kadm5_server_context *contextp,
		      krb5_storage *in,
		      krb5_storage *out)
{
    uint32_t version, mask;
    kadm5_principal_ent_rec ent;
    krb5_error_code ret;
    char *password;

    memset(&ent, 0, sizeof(ent));

    CHECK(krb5_ret_uint32(in, &version));
    INSIST(version == VERSION2);
    CHECK(ret_principal_ent(contextp->context, in, &ent));
    CHECK(krb5_ret_uint32(in, &mask));
    CHECK(ret_string_xdr(in, &password));

    INSIST(ent.principal);


    ret = _kadm5_acl_check_permission(contextp, KADM5_PRIV_ADD, ent.principal);
    if (ret)
	goto fail;

    ret = kadm5_create_principal(contextp, &ent, mask, password);

 fail:
    krb5_warn(contextp->context, ret, "create principal");
    CHECK(krb5_store_uint32(out, VERSION2)); /* api version */
    CHECK(krb5_store_uint32(out, ret)); /* code */

    free(password);
    kadm5_free_principal_ent(contextp, &ent);
}

static void
proc_delete_principal(kadm5_server_context *contextp,
		      krb5_storage *in,
		      krb5_storage *out)
{
    uint32_t version;
    krb5_principal princ;
    krb5_error_code ret;

    CHECK(krb5_ret_uint32(in, &version));
    INSIST(version == VERSION2);
    CHECK(ret_principal_xdr(contextp->context, in, &princ));

    ret = _kadm5_acl_check_permission(contextp, KADM5_PRIV_DELETE, princ);
    if (ret)
	goto fail;

    ret = kadm5_delete_principal(contextp, princ);

 fail:
    krb5_warn(contextp->context, ret, "delete principal");
    CHECK(krb5_store_uint32(out, VERSION2)); /* api version */
    CHECK(krb5_store_uint32(out, ret)); /* code */

    krb5_free_principal(contextp->context, princ);
}

static void
proc_get_principal(kadm5_server_context *contextp,
		   krb5_storage *in,
		   krb5_storage *out)
{
    uint32_t version, mask;
    krb5_principal princ;
    kadm5_principal_ent_rec ent;
    krb5_error_code ret;

    memset(&ent, 0, sizeof(ent));

    CHECK(krb5_ret_uint32(in, &version));
    INSIST(version == VERSION2);
    CHECK(ret_principal_xdr(contextp->context, in, &princ));
    CHECK(krb5_ret_uint32(in, &mask));

    ret = _kadm5_acl_check_permission(contextp, KADM5_PRIV_GET, princ);
    if(ret)
	goto fail;

    ret = kadm5_get_principal(contextp, princ, &ent, mask);

 fail:
    krb5_warn(contextp->context, ret, "get principal principal");

    CHECK(krb5_store_uint32(out, VERSION2)); /* api version */
    CHECK(krb5_store_uint32(out, ret)); /* code */
    if (ret == 0) {
	CHECK(store_principal_ent(contextp->context, out, &ent));
    }
    krb5_free_principal(contextp->context, princ);
    kadm5_free_principal_ent(contextp, &ent);
}

static void
proc_chrand_principal_v2(kadm5_server_context *contextp,
			 krb5_storage *in,
			 krb5_storage *out)
{
    krb5_error_code ret;
    krb5_principal princ;
    uint32_t version;
    krb5_keyblock *new_keys;
    int n_keys;

    CHECK(krb5_ret_uint32(in, &version));
    INSIST(version == VERSION2);
    CHECK(ret_principal_xdr(contextp->context, in, &princ));

    ret = _kadm5_acl_check_permission(contextp, KADM5_PRIV_CPW, princ);
    if(ret)
	goto fail;

    ret = kadm5_randkey_principal(contextp, princ,
				  &new_keys, &n_keys);

 fail:
    krb5_warn(contextp->context, ret, "rand key principal");

    CHECK(krb5_store_uint32(out, VERSION2)); /* api version */
    CHECK(krb5_store_uint32(out, ret));
    if (ret == 0) {
	int i;
	CHECK(krb5_store_int32(out, n_keys));

	for(i = 0; i < n_keys; i++){
	    CHECK(krb5_store_uint32(out, new_keys[i].keytype));
	    CHECK(store_data_xdr(out, new_keys[i].keyvalue));
	    krb5_free_keyblock_contents(contextp->context, &new_keys[i]);
	}
	free(new_keys);
    }
    krb5_free_principal(contextp->context, princ);
}

static void
proc_init(kadm5_server_context *contextp,
	  krb5_storage *in,
	  krb5_storage *out)
{
    CHECK(krb5_store_uint32(out, VERSION2)); /* api version */
    CHECK(krb5_store_uint32(out, 0)); /* code */
    CHECK(krb5_store_uint32(out, 0)); /* code */
}

struct krb5_proc {
    const char *name;
    void (*func)(kadm5_server_context *, krb5_storage *, krb5_storage *);
} procs[] = {
    { "NULL", NULL },
    { "create principal", proc_create_principal },
    { "delete principal", proc_delete_principal },
    { "modify principal", NULL },
    { "rename principal", NULL },
    { "get principal", proc_get_principal },
    { "chpass principal", NULL },
    { "chrand principal", proc_chrand_principal_v2 },
    { "create policy", NULL },
    { "delete policy", NULL },
    { "modify policy", NULL },
    { "get policy", NULL },
    { "get privs", NULL },
    { "init", proc_init },
    { "get principals", NULL },
    { "get polices", NULL },
    { "setkey principal", NULL },
    { "setkey principal v4", NULL },
    { "create principal v3", NULL },
    { "chpass principal v3", NULL },
    { "chrand principal v3", NULL },
    { "setkey principal v3", NULL }
};

static krb5_error_code
copyheader(krb5_storage *sp, krb5_data *data)
{
    off_t off;
    ssize_t sret;

    off = krb5_storage_seek(sp, 0, SEEK_CUR);

    CHECK(krb5_data_alloc(data, off));
    INSIST((size_t)off == data->length);
    krb5_storage_seek(sp, 0, SEEK_SET);
    sret = krb5_storage_read(sp, data->data, data->length);
    INSIST(sret == off);
    INSIST(off == krb5_storage_seek(sp, 0, SEEK_CUR));

    return 0;
}

struct gctx {
    krb5_data handle;
    gss_ctx_id_t ctx;
    uint32_t seq_num;
    int done;
    int inprogress;
};

static int
process_stream(krb5_context contextp,
	       unsigned char *buf, size_t ilen,
	       krb5_storage *sp)
{
    krb5_error_code ret;
    krb5_storage *msg, *reply, *dreply;
    OM_uint32 maj_stat, min_stat;
    gss_buffer_desc gin, gout;
    struct gctx gctx;
    void *server_handle = NULL;

    memset(&gctx, 0, sizeof(gctx));

    msg = krb5_storage_emem();
    reply = krb5_storage_emem();
    dreply = krb5_storage_emem();

    /*
     * First packet comes partly from the caller
     */

    INSIST(ilen >= 4);

    while (1) {
	struct call_header chdr;
	struct gcred gcred;
	uint32_t mtype;
	krb5_data headercopy;

	krb5_storage_truncate(dreply, 0);
	krb5_storage_truncate(reply, 0);
	krb5_storage_truncate(msg, 0);

	krb5_data_zero(&headercopy);
	memset(&chdr, 0, sizeof(chdr));
	memset(&gcred, 0, sizeof(gcred));

	/*
	 * This is very icky to handle the the auto-detection between
	 * the Heimdal protocol and the MIT ONC-RPC based protocol.
	 */

	if (ilen) {
	    int last_fragment;
	    unsigned long len;
	    ssize_t slen;
	    unsigned char tmp[4];

	    if (ilen < 4) {
		memcpy(tmp, buf, ilen);
		slen = krb5_storage_read(sp, tmp + ilen, sizeof(tmp) - ilen);
		INSIST((size_t)slen == sizeof(tmp) - ilen);

		ilen = sizeof(tmp);
		buf = tmp;
	    }
	    INSIST(ilen >= 4);

	    _krb5_get_int(buf, &len, 4);
	    last_fragment = (len & LAST_FRAGMENT) != 0;
	    len &= ~LAST_FRAGMENT;

	    ilen -= 4;
	    buf += 4;

	    if (ilen) {
		if (len < ilen) {
		    slen = krb5_storage_write(msg, buf, len);
		    INSIST((size_t)slen == len);
		    ilen -= len;
		    len = 0;
		} else {
		    slen = krb5_storage_write(msg, buf, ilen);
		    INSIST((size_t)slen == ilen);
		    len -= ilen;
		}
	    }

	    CHECK(read_data(sp, msg, len));

	    if (!last_fragment) {
		ret = collect_framents(sp, msg);
		if (ret == HEIM_ERR_EOF)
		    krb5_errx(contextp, 0, "client disconnected");
		INSIST(ret == 0);
	    }
	} else {

	    ret = collect_framents(sp, msg);
	    if (ret == HEIM_ERR_EOF)
		krb5_errx(contextp, 0, "client disconnected");
	    INSIST(ret == 0);
	}
	krb5_storage_seek(msg, 0, SEEK_SET);

	CHECK(krb5_ret_uint32(msg, &chdr.xid));
	CHECK(krb5_ret_uint32(msg, &mtype));
	CHECK(krb5_ret_uint32(msg, &chdr.rpcvers));
	CHECK(krb5_ret_uint32(msg, &chdr.prog));
	CHECK(krb5_ret_uint32(msg, &chdr.vers));
	CHECK(krb5_ret_uint32(msg, &chdr.proc));
	CHECK(ret_auth_opaque(msg, &chdr.cred));
	CHECK(copyheader(msg, &headercopy));
	CHECK(ret_auth_opaque(msg, &chdr.verf));

	INSIST(chdr.rpcvers == RPC_VERSION);
	INSIST(chdr.prog == KADM_SERVER);
	INSIST(chdr.vers == VVERSION);
	INSIST(chdr.cred.flavor == FLAVOR_GSS);

	CHECK(ret_gcred(&chdr.cred.data, &gcred));

	INSIST(gcred.version == FLAVOR_GSS_VERSION);

	if (gctx.done) {
	    INSIST(chdr.verf.flavor == FLAVOR_GSS);

	    /* from first byte to last of credential */
	    gin.value = headercopy.data;
	    gin.length = headercopy.length;
	    gout.value = chdr.verf.data.data;
	    gout.length = chdr.verf.data.length;

	    maj_stat = gss_verify_mic(&min_stat, gctx.ctx, &gin, &gout, NULL);
	    INSIST(maj_stat == GSS_S_COMPLETE);
	}

	switch(gcred.proc) {
	case RPG_DATA: {
	    krb5_data data;
	    int conf_state;
	    uint32_t seq;
	    krb5_storage *sp1;

	    INSIST(gcred.service == rpg_privacy);

	    INSIST(gctx.done);

	    INSIST(krb5_data_cmp(&gcred.handle, &gctx.handle) == 0);

	    CHECK(ret_data_xdr(msg, &data));

	    gin.value = data.data;
	    gin.length = data.length;

	    maj_stat = gss_unwrap(&min_stat, gctx.ctx, &gin, &gout,
				  &conf_state, NULL);
	    krb5_data_free(&data);
	    INSIST(maj_stat == GSS_S_COMPLETE);
	    INSIST(conf_state != 0);

	    sp1 = krb5_storage_from_mem(gout.value, gout.length);
	    INSIST(sp1 != NULL);

	    CHECK(krb5_ret_uint32(sp1, &seq));
	    INSIST (seq == gcred.seq_num);

	    /*
	     * Check sequence number
	     */
	    INSIST(seq > gctx.seq_num);
	    gctx.seq_num = seq;

	    /*
	     * If contextp is setup, priv data have the seq_num stored
	     * first in the block, so add it here before users data is
	     * added.
	     */
	    CHECK(krb5_store_uint32(dreply, gctx.seq_num));

	    if (chdr.proc >= sizeof(procs)/sizeof(procs[0])) {
		krb5_warnx(contextp, "proc number out of array");
	    } else if (procs[chdr.proc].func == NULL) {
		krb5_warnx(contextp, "proc '%s' never implemented",
			  procs[chdr.proc].name);
	    } else {
		krb5_warnx(contextp, "proc %s", procs[chdr.proc].name);
		INSIST(server_handle != NULL);
		(*procs[chdr.proc].func)(server_handle, sp, dreply);
	    }
	    krb5_storage_free(sp);
	    gss_release_buffer(&min_stat, &gout);

	    break;
	}
	case RPG_INIT:
	    INSIST(gctx.inprogress == 0);
	    INSIST(gctx.ctx == NULL);

	    gctx.inprogress = 1;
	    /* FALL THOUGH */
	case RPG_CONTINUE_INIT: {
	    gss_name_t src_name = GSS_C_NO_NAME;
	    krb5_data in;

	    INSIST(gctx.inprogress);

	    CHECK(ret_data_xdr(msg, &in));

	    gin.value = in.data;
	    gin.length = in.length;
	    gout.value = NULL;
	    gout.length = 0;

	    maj_stat = gss_accept_sec_context(&min_stat,
					      &gctx.ctx,
					      GSS_C_NO_CREDENTIAL,
					      &gin,
					      GSS_C_NO_CHANNEL_BINDINGS,
					      &src_name,
					      NULL,
					      &gout,
					      NULL,
					      NULL,
					      NULL);
	    if (GSS_ERROR(maj_stat)) {
		gss_print_errors(contextp, maj_stat, min_stat);
		krb5_errx(contextp, 1, "gss error, exit");
	    }
	    if ((maj_stat & GSS_S_CONTINUE_NEEDED) == 0) {
		kadm5_config_params realm_params;
		gss_buffer_desc bufp;
		char *client;

		gctx.done = 1;

		memset(&realm_params, 0, sizeof(realm_params));

		maj_stat = gss_export_name(&min_stat, src_name, &bufp);
		INSIST(maj_stat == GSS_S_COMPLETE);

		CHECK(parse_name(bufp.value, bufp.length,
				 GSS_KRB5_MECHANISM, &client));

		gss_release_buffer(&min_stat, &bufp);

		krb5_warnx(contextp, "%s connected", client);

		ret = kadm5_s_init_with_password_ctx(contextp,
						     client,
						     NULL,
						     KADM5_ADMIN_SERVICE,
						     &realm_params,
						     0, 0,
						     &server_handle);
		INSIST(ret == 0);
	    }

	    INSIST(gctx.ctx != GSS_C_NO_CONTEXT);

	    CHECK(krb5_store_uint32(dreply, 0));
	    CHECK(store_gss_init_res(dreply, gctx.handle,
				     maj_stat, min_stat, 1, &gout));
	    if (gout.value)
		gss_release_buffer(&min_stat, &gout);
	    if (src_name)
		gss_release_name(&min_stat, &src_name);

	    break;
	}
	case RPG_DESTROY:
	    krb5_errx(contextp, 1, "client destroyed gss contextp");
	default:
	    krb5_errx(contextp, 1, "client sent unknown gsscode %d",
		      (int)gcred.proc);
	}

	krb5_data_free(&gcred.handle);
	krb5_data_free(&chdr.cred.data);
	krb5_data_free(&chdr.verf.data);
	krb5_data_free(&headercopy);

	CHECK(krb5_store_uint32(reply, chdr.xid));
	CHECK(krb5_store_uint32(reply, 1)); /* REPLY */
	CHECK(krb5_store_uint32(reply, 0)); /* MSG_ACCEPTED */

	if (!gctx.done) {
	    krb5_data data;

	    CHECK(krb5_store_uint32(reply, 0)); /* flavor_none */
	    CHECK(krb5_store_uint32(reply, 0)); /* length */

	    CHECK(krb5_store_uint32(reply, 0)); /* SUCCESS */

	    CHECK(krb5_storage_to_data(dreply, &data));
	    INSIST((size_t)krb5_storage_write(reply, data.data, data.length) == data.length);
	    krb5_data_free(&data);

	} else {
	    uint32_t seqnum = htonl(gctx.seq_num);
	    krb5_data data;

	    gin.value = &seqnum;
	    gin.length = sizeof(seqnum);

	    maj_stat = gss_get_mic(&min_stat, gctx.ctx, 0, &gin, &gout);
	    INSIST(maj_stat == GSS_S_COMPLETE);

	    data.data = gout.value;
	    data.length = gout.length;

	    CHECK(krb5_store_uint32(reply, FLAVOR_GSS));
	    CHECK(store_data_xdr(reply, data));
	    gss_release_buffer(&min_stat, &gout);

	    CHECK(krb5_store_uint32(reply, 0)); /* SUCCESS */

	    CHECK(krb5_storage_to_data(dreply, &data));

	    if (gctx.inprogress) {
		ssize_t sret;
		gctx.inprogress = 0;
		sret = krb5_storage_write(reply, data.data, data.length);
		INSIST((size_t)sret == data.length);
		krb5_data_free(&data);
	    } else {
		int conf_state;

		gin.value = data.data;
		gin.length = data.length;

		maj_stat = gss_wrap(&min_stat, gctx.ctx, 1, 0,
				    &gin, &conf_state, &gout);
		INSIST(maj_stat == GSS_S_COMPLETE);
		INSIST(conf_state != 0);
		krb5_data_free(&data);

		data.data = gout.value;
		data.length = gout.length;

		store_data_xdr(reply, data);
		gss_release_buffer(&min_stat, &gout);
	    }
	}

	{
	    krb5_data data;
	    ssize_t sret;
	    CHECK(krb5_storage_to_data(reply, &data));
	    CHECK(krb5_store_uint32(sp, data.length | LAST_FRAGMENT));
	    sret = krb5_storage_write(sp, data.data, data.length);
	    INSIST((size_t)sret == data.length);
	    krb5_data_free(&data);
	}

    }
}


int
handle_mit(krb5_context contextp, void *buf, size_t len, krb5_socket_t sock)
{
    krb5_storage *sp;

    dcontext = contextp;

    sp = krb5_storage_from_fd(sock);
    INSIST(sp != NULL);

    process_stream(contextp, buf, len, sp);

    return 0;
}
