/*
 * Copyright (c) 1997 - 2005 Kungliga Tekniska Högskolan
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
#include <krb5-private.h>

static kadm5_ret_t
kadmind_dispatch(void *kadm_handlep, krb5_boolean initial,
		 krb5_data *in, krb5_data *out)
{
    kadm5_ret_t ret;
    int32_t cmd, mask, tmp;
    kadm5_server_context *contextp = kadm_handlep;
    char client[128], name[128], name2[128];
    const char *op = "";
    krb5_principal princ, princ2;
    kadm5_principal_ent_rec ent;
    char *password, *expression;
    krb5_keyblock *new_keys;
    int n_keys;
    char **princs;
    int n_princs;
    krb5_storage *sp;

    krb5_unparse_name_fixed(contextp->context, contextp->caller,
			    client, sizeof(client));

    sp = krb5_storage_from_data(in);
    if (sp == NULL)
	krb5_errx(contextp->context, 1, "out of memory");

    krb5_ret_int32(sp, &cmd);
    switch(cmd){
    case kadm_get:{
	op = "GET";
	ret = krb5_ret_principal(sp, &princ);
	if(ret)
	    goto fail;
	ret = krb5_ret_int32(sp, &mask);
	if(ret){
	    krb5_free_principal(contextp->context, princ);
	    goto fail;
	}
	mask |= KADM5_PRINCIPAL;
	krb5_unparse_name_fixed(contextp->context, princ, name, sizeof(name));
	krb5_warnx(contextp->context, "%s: %s %s", client, op, name);
	ret = _kadm5_acl_check_permission(contextp, KADM5_PRIV_GET, princ);
	if(ret){
	    krb5_free_principal(contextp->context, princ);
	    goto fail;
	}
	ret = kadm5_get_principal(kadm_handlep, princ, &ent, mask);
	krb5_storage_free(sp);
	sp = krb5_storage_emem();
	krb5_store_int32(sp, ret);
	if(ret == 0){
	    kadm5_store_principal_ent(sp, &ent);
	    kadm5_free_principal_ent(kadm_handlep, &ent);
	}
	krb5_free_principal(contextp->context, princ);
	break;
    }
    case kadm_delete:{
	op = "DELETE";
	ret = krb5_ret_principal(sp, &princ);
	if(ret)
	    goto fail;
	krb5_unparse_name_fixed(contextp->context, princ, name, sizeof(name));
	krb5_warnx(contextp->context, "%s: %s %s", client, op, name);
	ret = _kadm5_acl_check_permission(contextp, KADM5_PRIV_DELETE, princ);
	if(ret){
	    krb5_free_principal(contextp->context, princ);
	    goto fail;
	}
	ret = kadm5_delete_principal(kadm_handlep, princ);
	krb5_free_principal(contextp->context, princ);
	krb5_storage_free(sp);
	sp = krb5_storage_emem();
	krb5_store_int32(sp, ret);
	break;
    }
    case kadm_create:{
	op = "CREATE";
	ret = kadm5_ret_principal_ent(sp, &ent);
	if(ret)
	    goto fail;
	ret = krb5_ret_int32(sp, &mask);
	if(ret){
	    kadm5_free_principal_ent(contextp->context, &ent);
	    goto fail;
	}
	ret = krb5_ret_string(sp, &password);
	if(ret){
	    kadm5_free_principal_ent(contextp->context, &ent);
	    goto fail;
	}
	krb5_unparse_name_fixed(contextp->context, ent.principal,
				name, sizeof(name));
	krb5_warnx(contextp->context, "%s: %s %s", client, op, name);
	ret = _kadm5_acl_check_permission(contextp, KADM5_PRIV_ADD,
					  ent.principal);
	if(ret){
	    kadm5_free_principal_ent(contextp->context, &ent);
	    memset(password, 0, strlen(password));
	    free(password);
	    goto fail;
	}
	ret = kadm5_create_principal(kadm_handlep, &ent,
				     mask, password);
	kadm5_free_principal_ent(kadm_handlep, &ent);
	memset(password, 0, strlen(password));
	free(password);
	krb5_storage_free(sp);
	sp = krb5_storage_emem();
	krb5_store_int32(sp, ret);
	break;
    }
    case kadm_modify:{
	op = "MODIFY";
	ret = kadm5_ret_principal_ent(sp, &ent);
	if(ret)
	    goto fail;
	ret = krb5_ret_int32(sp, &mask);
	if(ret){
	    kadm5_free_principal_ent(contextp, &ent);
	    goto fail;
	}
	krb5_unparse_name_fixed(contextp->context, ent.principal,
				name, sizeof(name));
	krb5_warnx(contextp->context, "%s: %s %s", client, op, name);
	ret = _kadm5_acl_check_permission(contextp, KADM5_PRIV_MODIFY,
					  ent.principal);
	if(ret){
	    kadm5_free_principal_ent(contextp, &ent);
	    goto fail;
	}
	ret = kadm5_modify_principal(kadm_handlep, &ent, mask);
	kadm5_free_principal_ent(kadm_handlep, &ent);
	krb5_storage_free(sp);
	sp = krb5_storage_emem();
	krb5_store_int32(sp, ret);
	break;
    }
    case kadm_rename:{
	op = "RENAME";
	ret = krb5_ret_principal(sp, &princ);
	if(ret)
	    goto fail;
	ret = krb5_ret_principal(sp, &princ2);
	if(ret){
	    krb5_free_principal(contextp->context, princ);
	    goto fail;
	}
	krb5_unparse_name_fixed(contextp->context, princ, name, sizeof(name));
	krb5_unparse_name_fixed(contextp->context, princ2, name2, sizeof(name2));
	krb5_warnx(contextp->context, "%s: %s %s -> %s",
		   client, op, name, name2);
	ret = _kadm5_acl_check_permission(contextp,
					  KADM5_PRIV_ADD,
					  princ2)
	    || _kadm5_acl_check_permission(contextp,
					   KADM5_PRIV_DELETE,
					   princ);
	if(ret){
	    krb5_free_principal(contextp->context, princ);
	    krb5_free_principal(contextp->context, princ2);
	    goto fail;
	}
	ret = kadm5_rename_principal(kadm_handlep, princ, princ2);
	krb5_free_principal(contextp->context, princ);
	krb5_free_principal(contextp->context, princ2);
	krb5_storage_free(sp);
	sp = krb5_storage_emem();
	krb5_store_int32(sp, ret);
	break;
    }
    case kadm_chpass:{
	op = "CHPASS";
	ret = krb5_ret_principal(sp, &princ);
	if(ret)
	    goto fail;
	ret = krb5_ret_string(sp, &password);
	if(ret){
	    krb5_free_principal(contextp->context, princ);
	    goto fail;
	}
	krb5_unparse_name_fixed(contextp->context, princ, name, sizeof(name));
	krb5_warnx(contextp->context, "%s: %s %s", client, op, name);

	/*
	 * The change is allowed if at least one of:
	 *
	 * a) allowed by sysadmin
	 * b) it's for the principal him/herself and this was an
	 *    initial ticket, but then, check with the password quality
	 *    function.
	 * c) the user is on the CPW ACL.
	 */

	if (krb5_config_get_bool_default(contextp->context, NULL, TRUE,
					 "kadmin", "allow_self_change_password", NULL)
	    && initial
	    && krb5_principal_compare (contextp->context, contextp->caller,
				       princ))
	{
	    krb5_data pwd_data;
	    const char *pwd_reason;

	    pwd_data.data = password;
	    pwd_data.length = strlen(password);

	    pwd_reason = kadm5_check_password_quality (contextp->context,
						       princ, &pwd_data);
	    if (pwd_reason != NULL)
		ret = KADM5_PASS_Q_DICT;
	    else
		ret = 0;
	} else
	    ret = _kadm5_acl_check_permission(contextp, KADM5_PRIV_CPW, princ);

	if(ret) {
	    krb5_free_principal(contextp->context, princ);
	    memset(password, 0, strlen(password));
	    free(password);
	    goto fail;
	}
	ret = kadm5_chpass_principal(kadm_handlep, princ, password);
	krb5_free_principal(contextp->context, princ);
	memset(password, 0, strlen(password));
	free(password);
	krb5_storage_free(sp);
	sp = krb5_storage_emem();
	krb5_store_int32(sp, ret);
	break;
    }
    case kadm_chpass_with_key:{
	int i;
	krb5_key_data *key_data;
	int n_key_data;

	op = "CHPASS_WITH_KEY";
	ret = krb5_ret_principal(sp, &princ);
	if(ret)
	    goto fail;
	ret = krb5_ret_int32(sp, &n_key_data);
	if (ret) {
	    krb5_free_principal(contextp->context, princ);
	    goto fail;
	}
	/* n_key_data will be squeezed into an int16_t below. */
	if (n_key_data < 0 || n_key_data >= 1 << 16 ||
	    (size_t)n_key_data > UINT_MAX/sizeof(*key_data)) {
	    ret = ERANGE;
	    krb5_free_principal(contextp->context, princ);
	    goto fail;
	}

	key_data = malloc (n_key_data * sizeof(*key_data));
	if (key_data == NULL && n_key_data != 0) {
	    ret = ENOMEM;
	    krb5_free_principal(contextp->context, princ);
	    goto fail;
	}

	for (i = 0; i < n_key_data; ++i) {
	    ret = kadm5_ret_key_data (sp, &key_data[i]);
	    if (ret) {
		int16_t dummy = i;

		kadm5_free_key_data (contextp, &dummy, key_data);
		free (key_data);
		krb5_free_principal(contextp->context, princ);
		goto fail;
	    }
	}

	krb5_unparse_name_fixed(contextp->context, princ, name, sizeof(name));
	krb5_warnx(contextp->context, "%s: %s %s", client, op, name);

	/*
	 * The change is only allowed if the user is on the CPW ACL,
	 * this it to force password quality check on the user.
	 */

	ret = _kadm5_acl_check_permission(contextp, KADM5_PRIV_CPW, princ);
	if(ret) {
	    int16_t dummy = n_key_data;

	    kadm5_free_key_data (contextp, &dummy, key_data);
	    free (key_data);
	    krb5_free_principal(contextp->context, princ);
	    goto fail;
	}
	ret = kadm5_chpass_principal_with_key(kadm_handlep, princ,
					      n_key_data, key_data);
	{
	    int16_t dummy = n_key_data;
	    kadm5_free_key_data (contextp, &dummy, key_data);
	}
	free (key_data);
	krb5_free_principal(contextp->context, princ);
	krb5_storage_free(sp);
	sp = krb5_storage_emem();
	krb5_store_int32(sp, ret);
	break;
    }
    case kadm_randkey:{
	op = "RANDKEY";
	ret = krb5_ret_principal(sp, &princ);
	if(ret)
	    goto fail;
	krb5_unparse_name_fixed(contextp->context, princ, name, sizeof(name));
	krb5_warnx(contextp->context, "%s: %s %s", client, op, name);
	/*
	 * The change is allowed if at least one of:
	 * a) it's for the principal him/herself and this was an initial ticket
	 * b) the user is on the CPW ACL.
	 */

	if (initial
	    && krb5_principal_compare (contextp->context, contextp->caller,
				       princ))
	    ret = 0;
	else
	    ret = _kadm5_acl_check_permission(contextp, KADM5_PRIV_CPW, princ);

	if(ret) {
	    krb5_free_principal(contextp->context, princ);
	    goto fail;
	}
	ret = kadm5_randkey_principal(kadm_handlep, princ,
				      &new_keys, &n_keys);
	krb5_free_principal(contextp->context, princ);
	krb5_storage_free(sp);
	sp = krb5_storage_emem();
	krb5_store_int32(sp, ret);
	if(ret == 0){
	    int i;
	    krb5_store_int32(sp, n_keys);
	    for(i = 0; i < n_keys; i++){
		krb5_store_keyblock(sp, new_keys[i]);
		krb5_free_keyblock_contents(contextp->context, &new_keys[i]);
	    }
	    free(new_keys);
	}
	break;
    }
    case kadm_get_privs:{
	uint32_t privs;
	ret = kadm5_get_privs(kadm_handlep, &privs);
	krb5_storage_free(sp);
	sp = krb5_storage_emem();
	krb5_store_int32(sp, ret);
	if(ret == 0)
	    krb5_store_uint32(sp, privs);
	break;
    }
    case kadm_get_princs:{
	op = "LIST";
	ret = krb5_ret_int32(sp, &tmp);
	if(ret)
	    goto fail;
	if(tmp){
	    ret = krb5_ret_string(sp, &expression);
	    if(ret)
		goto fail;
	}else
	    expression = NULL;
	krb5_warnx(contextp->context, "%s: %s %s", client, op,
		   expression ? expression : "*");
	ret = _kadm5_acl_check_permission(contextp, KADM5_PRIV_LIST, NULL);
	if(ret){
	    free(expression);
	    goto fail;
	}
	ret = kadm5_get_principals(kadm_handlep, expression, &princs, &n_princs);
	free(expression);
	krb5_storage_free(sp);
	sp = krb5_storage_emem();
	krb5_store_int32(sp, ret);
	if(ret == 0){
	    int i;
	    krb5_store_int32(sp, n_princs);
	    for(i = 0; i < n_princs; i++)
		krb5_store_string(sp, princs[i]);
	    kadm5_free_name_list(kadm_handlep, princs, &n_princs);
	}
	break;
    }
    default:
	krb5_warnx(contextp->context, "%s: UNKNOWN OP %d", client, cmd);
	krb5_storage_free(sp);
	sp = krb5_storage_emem();
	krb5_store_int32(sp, KADM5_FAILURE);
	break;
    }
    krb5_storage_to_data(sp, out);
    krb5_storage_free(sp);
    return 0;
fail:
    krb5_warn(contextp->context, ret, "%s", op);
    krb5_storage_seek(sp, 0, SEEK_SET);
    krb5_store_int32(sp, ret);
    krb5_storage_to_data(sp, out);
    krb5_storage_free(sp);
    return 0;
}

static void
v5_loop (krb5_context contextp,
	 krb5_auth_context ac,
	 krb5_boolean initial,
	 void *kadm_handlep,
	 krb5_socket_t fd)
{
    krb5_error_code ret;
    krb5_data in, out;

    for (;;) {
	doing_useful_work = 0;
	if(term_flag)
	    exit(0);
	ret = krb5_read_priv_message(contextp, ac, &fd, &in);
	if(ret == HEIM_ERR_EOF)
	    exit(0);
	if(ret)
	    krb5_err(contextp, 1, ret, "krb5_read_priv_message");
	doing_useful_work = 1;
	kadmind_dispatch(kadm_handlep, initial, &in, &out);
	krb5_data_free(&in);
	ret = krb5_write_priv_message(contextp, ac, &fd, &out);
	if(ret)
	    krb5_err(contextp, 1, ret, "krb5_write_priv_message");
    }
}

static krb5_boolean
match_appl_version(const void *data, const char *appl_version)
{
    unsigned minor;
    if(sscanf(appl_version, "KADM0.%u", &minor) != 1)
	return 0;
    /*XXX*/
    *(unsigned*)(intptr_t)data = minor;
    return 1;
}

static void
handle_v5(krb5_context contextp,
	  krb5_keytab keytab,
	  krb5_socket_t fd)
{
    krb5_error_code ret;
    krb5_ticket *ticket;
    char *server_name;
    char *client;
    void *kadm_handlep;
    krb5_boolean initial;
    krb5_auth_context ac = NULL;

    unsigned kadm_version;
    kadm5_config_params realm_params;

    ret = krb5_recvauth_match_version(contextp, &ac, &fd,
				      match_appl_version, &kadm_version,
				      NULL, KRB5_RECVAUTH_IGNORE_VERSION,
				      keytab, &ticket);
    if (ret)
	krb5_err(contextp, 1, ret, "krb5_recvauth");

    ret = krb5_unparse_name (contextp, ticket->server, &server_name);
    if (ret)
	krb5_err (contextp, 1, ret, "krb5_unparse_name");

    if (strncmp (server_name, KADM5_ADMIN_SERVICE,
		 strlen(KADM5_ADMIN_SERVICE)) != 0)
	krb5_errx (contextp, 1, "ticket for strange principal (%s)",
		   server_name);

    free (server_name);

    memset(&realm_params, 0, sizeof(realm_params));

    if(kadm_version == 1) {
	krb5_data params;
	ret = krb5_read_priv_message(contextp, ac, &fd, &params);
	if(ret)
	    krb5_err(contextp, 1, ret, "krb5_read_priv_message");
	_kadm5_unmarshal_params(contextp, &params, &realm_params);
    }

    initial = ticket->ticket.flags.initial;
    ret = krb5_unparse_name(contextp, ticket->client, &client);
    if (ret)
	krb5_err (contextp, 1, ret, "krb5_unparse_name");
    krb5_free_ticket (contextp, ticket);
    ret = kadm5_s_init_with_password_ctx(contextp,
					 client,
					 NULL,
					 KADM5_ADMIN_SERVICE,
					 &realm_params,
					 0, 0,
					 &kadm_handlep);
    if(ret)
	krb5_err (contextp, 1, ret, "kadm5_init_with_password_ctx");
    v5_loop (contextp, ac, initial, kadm_handlep, fd);
}

krb5_error_code
kadmind_loop(krb5_context contextp,
	     krb5_keytab keytab,
	     krb5_socket_t sock)
{
    u_char buf[sizeof(KRB5_SENDAUTH_VERSION) + 4];
    ssize_t n;
    unsigned long len;

    n = krb5_net_read(contextp, &sock, buf, 4);
    if(n == 0)
	exit(0);
    if(n < 0)
	krb5_err(contextp, 1, errno, "read");
    _krb5_get_int(buf, &len, 4);

    if (len == sizeof(KRB5_SENDAUTH_VERSION)) {

	n = krb5_net_read(contextp, &sock, buf + 4, len);
	if (n < 0)
	    krb5_err (contextp, 1, errno, "reading sendauth version");
	if (n == 0)
	    krb5_errx (contextp, 1, "EOF reading sendauth version");

	if(memcmp(buf + 4, KRB5_SENDAUTH_VERSION, len) == 0) {
	    handle_v5(contextp, keytab, sock);
	    return 0;
	}
	len += 4;
    } else
	len = 4;

    handle_mit(contextp, buf, len, sock);

    return 0;
}
