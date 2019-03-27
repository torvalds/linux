/*
 * Copyright (c) 1997-2005 Kungliga Tekniska Högskolan
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

#include "kpasswd_locl.h"
RCSID("$Id$");

#include <kadm5/admin.h>
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#include <hdb.h>
#include <kadm5/private.h>

static krb5_context context;
static krb5_log_facility *log_facility;

static struct getarg_strings addresses_str;
krb5_addresses explicit_addresses;

static sig_atomic_t exit_flag = 0;

static void
add_one_address (const char *str, int first)
{
    krb5_error_code ret;
    krb5_addresses tmp;

    ret = krb5_parse_address (context, str, &tmp);
    if (ret)
	krb5_err (context, 1, ret, "parse_address `%s'", str);
    if (first)
	krb5_copy_addresses(context, &tmp, &explicit_addresses);
    else
	krb5_append_addresses(context, &explicit_addresses, &tmp);
    krb5_free_addresses (context, &tmp);
}

static void
send_reply (int s,
	    struct sockaddr *sa,
	    int sa_size,
	    krb5_data *ap_rep,
	    krb5_data *rest)
{
    struct msghdr msghdr;
    struct iovec iov[3];
    uint16_t len, ap_rep_len;
    u_char header[6];
    u_char *p;

    if (ap_rep)
	ap_rep_len = ap_rep->length;
    else
	ap_rep_len = 0;

    len = 6 + ap_rep_len + rest->length;
    p = header;
    *p++ = (len >> 8) & 0xFF;
    *p++ = (len >> 0) & 0xFF;
    *p++ = 0;
    *p++ = 1;
    *p++ = (ap_rep_len >> 8) & 0xFF;
    *p++ = (ap_rep_len >> 0) & 0xFF;

    memset (&msghdr, 0, sizeof(msghdr));
    msghdr.msg_name       = (void *)sa;
    msghdr.msg_namelen    = sa_size;
    msghdr.msg_iov        = iov;
    msghdr.msg_iovlen     = sizeof(iov)/sizeof(*iov);
#if 0
    msghdr.msg_control    = NULL;
    msghdr.msg_controllen = 0;
#endif

    iov[0].iov_base       = (char *)header;
    iov[0].iov_len        = 6;
    if (ap_rep_len) {
	iov[1].iov_base   = ap_rep->data;
	iov[1].iov_len    = ap_rep->length;
    } else {
	iov[1].iov_base   = NULL;
	iov[1].iov_len    = 0;
    }
    iov[2].iov_base       = rest->data;
    iov[2].iov_len        = rest->length;

    if (sendmsg (s, &msghdr, 0) < 0)
	krb5_warn (context, errno, "sendmsg");
}

static int
make_result (krb5_data *data,
	     uint16_t result_code,
	     const char *expl)
{
    char *str;
    krb5_data_zero (data);

    data->length = asprintf (&str,
			     "%c%c%s",
			     (result_code >> 8) & 0xFF,
			     result_code & 0xFF,
			     expl);

    if (str == NULL) {
	krb5_warnx (context, "Out of memory generating error reply");
	return 1;
    }
    data->data = str;
    return 0;
}

static void
reply_error (krb5_realm realm,
	     int s,
	     struct sockaddr *sa,
	     int sa_size,
	     krb5_error_code error_code,
	     uint16_t result_code,
	     const char *expl)
{
    krb5_error_code ret;
    krb5_data error_data;
    krb5_data e_data;
    krb5_principal server = NULL;

    if (make_result(&e_data, result_code, expl))
	return;

    if (realm) {
	ret = krb5_make_principal (context, &server, realm,
				   "kadmin", "changepw", NULL);
	if (ret) {
	    krb5_data_free (&e_data);
	    return;
	}
    }

    ret = krb5_mk_error (context,
			 error_code,
			 NULL,
			 &e_data,
			 NULL,
			 server,
			 NULL,
			 NULL,
			 &error_data);
    if (server)
	krb5_free_principal(context, server);
    krb5_data_free (&e_data);
    if (ret) {
	krb5_warn (context, ret, "Could not even generate error reply");
	return;
    }
    send_reply (s, sa, sa_size, NULL, &error_data);
    krb5_data_free (&error_data);
}

static void
reply_priv (krb5_auth_context auth_context,
	    int s,
	    struct sockaddr *sa,
	    int sa_size,
	    uint16_t result_code,
	    const char *expl)
{
    krb5_error_code ret;
    krb5_data krb_priv_data;
    krb5_data ap_rep_data;
    krb5_data e_data;

    ret = krb5_mk_rep (context,
		       auth_context,
		       &ap_rep_data);
    if (ret) {
	krb5_warn (context, ret, "Could not even generate error reply");
	return;
    }

    if (make_result(&e_data, result_code, expl))
	return;

    ret = krb5_mk_priv (context,
			auth_context,
			&e_data,
			&krb_priv_data,
			NULL);
    krb5_data_free (&e_data);
    if (ret) {
	krb5_warn (context, ret, "Could not even generate error reply");
	return;
    }
    send_reply (s, sa, sa_size, &ap_rep_data, &krb_priv_data);
    krb5_data_free (&ap_rep_data);
    krb5_data_free (&krb_priv_data);
}

/*
 * Change the password for `principal', sending the reply back on `s'
 * (`sa', `sa_size') to `pwd_data'.
 */

static void
change (krb5_auth_context auth_context,
	krb5_principal admin_principal,
	uint16_t version,
	int s,
	struct sockaddr *sa,
	int sa_size,
	krb5_data *in_data)
{
    krb5_error_code ret;
    char *client = NULL, *admin = NULL;
    const char *pwd_reason;
    kadm5_config_params conf;
    void *kadm5_handle = NULL;
    krb5_principal principal = NULL;
    krb5_data *pwd_data = NULL;
    char *tmp;
    ChangePasswdDataMS chpw;

    memset (&conf, 0, sizeof(conf));
    memset(&chpw, 0, sizeof(chpw));

    if (version == KRB5_KPASSWD_VERS_CHANGEPW) {
	ret = krb5_copy_data(context, in_data, &pwd_data);
	if (ret) {
	    krb5_warn (context, ret, "krb5_copy_data");
	    reply_priv (auth_context, s, sa, sa_size, KRB5_KPASSWD_MALFORMED,
			"out out memory copying password");
	    return;
	}
	principal = admin_principal;
    } else if (version == KRB5_KPASSWD_VERS_SETPW) {
	size_t len;

	ret = decode_ChangePasswdDataMS(in_data->data, in_data->length,
					&chpw, &len);
	if (ret) {
	    krb5_warn (context, ret, "decode_ChangePasswdDataMS");
	    reply_priv (auth_context, s, sa, sa_size, KRB5_KPASSWD_MALFORMED,
			"malformed ChangePasswdData");
	    return;
	}


	ret = krb5_copy_data(context, &chpw.newpasswd, &pwd_data);
	if (ret) {
	    krb5_warn (context, ret, "krb5_copy_data");
	    reply_priv (auth_context, s, sa, sa_size, KRB5_KPASSWD_MALFORMED,
			"out out memory copying password");
	    goto out;
	}

	if (chpw.targname == NULL && chpw.targrealm != NULL) {
	    krb5_warn (context, ret, "kadm5_init_with_password_ctx");
	    reply_priv (auth_context, s, sa, sa_size,
			KRB5_KPASSWD_MALFORMED,
			"targrealm but not targname");
	    goto out;
	}

	if (chpw.targname) {
	    krb5_principal_data princ;

	    princ.name = *chpw.targname;
	    princ.realm = *chpw.targrealm;
	    if (princ.realm == NULL) {
		ret = krb5_get_default_realm(context, &princ.realm);

		if (ret) {
		    krb5_warnx (context,
				"kadm5_init_with_password_ctx: "
				"failed to allocate realm");
		    reply_priv (auth_context, s, sa, sa_size,
				KRB5_KPASSWD_SOFTERROR,
				"failed to allocate realm");
		    goto out;
		}
	    }
	    ret = krb5_copy_principal(context, &princ, &principal);
	    if (*chpw.targrealm == NULL)
		free(princ.realm);
	    if (ret) {
		krb5_warn(context, ret, "krb5_copy_principal");
		reply_priv(auth_context, s, sa, sa_size,
			   KRB5_KPASSWD_HARDERROR,
			   "failed to allocate principal");
		goto out;
	    }
	} else
	    principal = admin_principal;
    } else {
	krb5_warnx (context, "kadm5_init_with_password_ctx: unknown proto");
	reply_priv (auth_context, s, sa, sa_size,
		    KRB5_KPASSWD_HARDERROR,
		    "Unknown protocol used");
	return;
    }

    ret = krb5_unparse_name (context, admin_principal, &admin);
    if (ret) {
	krb5_warn (context, ret, "unparse_name failed");
	reply_priv (auth_context, s, sa, sa_size,
		    KRB5_KPASSWD_HARDERROR, "out of memory error");
	goto out;
    }

    conf.realm = principal->realm;
    conf.mask |= KADM5_CONFIG_REALM;

    ret = kadm5_init_with_password_ctx(context,
				       admin,
				       NULL,
				       KADM5_ADMIN_SERVICE,
				       &conf, 0, 0,
				       &kadm5_handle);
    if (ret) {
	krb5_warn (context, ret, "kadm5_init_with_password_ctx");
	reply_priv (auth_context, s, sa, sa_size, 2,
		    "Internal error");
	goto out;
    }

    ret = krb5_unparse_name(context, principal, &client);
    if (ret) {
	krb5_warn (context, ret, "unparse_name failed");
	reply_priv (auth_context, s, sa, sa_size,
		    KRB5_KPASSWD_HARDERROR, "out of memory error");
	goto out;
    }

    /*
     * Check password quality if not changing as administrator
     */

    if (krb5_principal_compare(context, admin_principal, principal) == TRUE) {

	pwd_reason = kadm5_check_password_quality (context, principal,
						   pwd_data);
	if (pwd_reason != NULL ) {
	    krb5_warnx (context,
			"%s didn't pass password quality check with error: %s",
			client, pwd_reason);
	    reply_priv (auth_context, s, sa, sa_size,
			KRB5_KPASSWD_SOFTERROR, pwd_reason);
	    goto out;
	}
	krb5_warnx (context, "Changing password for %s", client);
    } else {
	ret = _kadm5_acl_check_permission(kadm5_handle, KADM5_PRIV_CPW,
					  principal);
	if (ret) {
	    krb5_warn (context, ret,
		       "Check ACL failed for %s for changing %s password",
		       admin, client);
	    reply_priv (auth_context, s, sa, sa_size,
			KRB5_KPASSWD_HARDERROR, "permission denied");
	    goto out;
	}
	krb5_warnx (context, "%s is changing password for %s", admin, client);
    }

    ret = krb5_data_realloc(pwd_data, pwd_data->length + 1);
    if (ret) {
	krb5_warn (context, ret, "malloc: out of memory");
	reply_priv (auth_context, s, sa, sa_size, KRB5_KPASSWD_HARDERROR,
		    "Internal error");
	goto out;
    }
    tmp = pwd_data->data;
    tmp[pwd_data->length - 1] = '\0';

    ret = kadm5_s_chpass_principal_cond (kadm5_handle, principal, tmp);
    krb5_free_data (context, pwd_data);
    pwd_data = NULL;
    if (ret) {
	const char *str = krb5_get_error_message(context, ret);
	krb5_warnx(context, "kadm5_s_chpass_principal_cond: %s", str);
	reply_priv (auth_context, s, sa, sa_size, KRB5_KPASSWD_SOFTERROR,
		    str ? str : "Internal error");
	krb5_free_error_message(context, str);
	goto out;
    }
    reply_priv (auth_context, s, sa, sa_size, KRB5_KPASSWD_SUCCESS,
		"Password changed");
out:
    free_ChangePasswdDataMS(&chpw);
    if (principal != admin_principal)
	krb5_free_principal(context, principal);
    if (admin)
	free(admin);
    if (client)
	free(client);
    if (pwd_data)
	krb5_free_data(context, pwd_data);
    if (kadm5_handle)
	kadm5_destroy (kadm5_handle);
}

static int
verify (krb5_auth_context *auth_context,
	krb5_realm *realms,
	krb5_keytab keytab,
	krb5_ticket **ticket,
	krb5_data *out_data,
	uint16_t *version,
	int s,
	struct sockaddr *sa,
	int sa_size,
	u_char *msg,
	size_t len,
	krb5_address *client_addr)
{
    krb5_error_code ret;
    uint16_t pkt_len, pkt_ver, ap_req_len;
    krb5_data ap_req_data;
    krb5_data krb_priv_data;
    krb5_realm *r;

    /*
     * Only send an error reply if the request passes basic length
     * verification.  Otherwise, kpasswdd would reply to every UDP packet,
     * allowing an attacker to set up a ping-pong DoS attack via a spoofed UDP
     * packet with a source address of another UDP service that also replies
     * to every packet.
     *
     * Also suppress the error reply if ap_req_len is 0, which indicates
     * either an invalid request or an error packet.  An error packet may be
     * the result of a ping-pong attacker pointing us at another kpasswdd.
     */
    pkt_len = (msg[0] << 8) | (msg[1]);
    pkt_ver = (msg[2] << 8) | (msg[3]);
    ap_req_len = (msg[4] << 8) | (msg[5]);
    if (pkt_len != len) {
	krb5_warnx (context, "Strange len: %ld != %ld",
		    (long)pkt_len, (long)len);
	return 1;
    }
    if (ap_req_len == 0) {
	krb5_warnx (context, "Request is error packet (ap_req_len == 0)");
	return 1;
    }
    if (pkt_ver != KRB5_KPASSWD_VERS_CHANGEPW &&
	pkt_ver != KRB5_KPASSWD_VERS_SETPW) {
	krb5_warnx (context, "Bad version (%d)", pkt_ver);
	reply_error (NULL, s, sa, sa_size, 0, 1, "Wrong program version");
	return 1;
    }
    *version = pkt_ver;

    ap_req_data.data   = msg + 6;
    ap_req_data.length = ap_req_len;

    ret = krb5_rd_req (context,
		       auth_context,
		       &ap_req_data,
		       NULL,
		       keytab,
		       NULL,
		       ticket);
    if (ret) {
	krb5_warn (context, ret, "krb5_rd_req");
	reply_error (NULL, s, sa, sa_size, ret, 3, "Authentication failed");
	return 1;
    }

    /* verify realm and principal */
    for (r = realms; *r != NULL; r++) {
	krb5_principal principal;
	krb5_boolean same;

	ret = krb5_make_principal (context,
				   &principal,
				   *r,
				   "kadmin",
				   "changepw",
				   NULL);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_make_principal");

	same = krb5_principal_compare(context, principal, (*ticket)->server);
	krb5_free_principal(context, principal);
	if (same == TRUE)
	    break;
    }
    if (*r == NULL) {
	char *str;
	krb5_unparse_name(context, (*ticket)->server, &str);
	krb5_warnx (context, "client used not valid principal %s", str);
	free(str);
	reply_error (NULL, s, sa, sa_size, ret, 1,
		     "Bad request");
	goto out;
    }

    if (strcmp((*ticket)->server->realm, (*ticket)->client->realm) != 0) {
	krb5_warnx (context, "server realm (%s) not same a client realm (%s)",
		    (*ticket)->server->realm, (*ticket)->client->realm);
	reply_error ((*ticket)->server->realm, s, sa, sa_size, ret, 1,
		     "Bad request");
	goto out;
    }

    if (!(*ticket)->ticket.flags.initial) {
	krb5_warnx (context, "initial flag not set");
	reply_error ((*ticket)->server->realm, s, sa, sa_size, ret, 1,
		     "Bad request");
	goto out;
    }
    krb_priv_data.data   = msg + 6 + ap_req_len;
    krb_priv_data.length = len - 6 - ap_req_len;

    /*
     * Only enforce client addresses on on tickets with addresses.  If
     * its addressless, we are guessing its behind NAT and really
     * can't know this information.
     */

    if ((*ticket)->ticket.caddr && (*ticket)->ticket.caddr->len > 0) {
	ret = krb5_auth_con_setaddrs (context, *auth_context,
				      NULL, client_addr);
	if (ret) {
	    krb5_warn (context, ret, "krb5_auth_con_setaddr(this)");
	    goto out;
	}
    }

    ret = krb5_rd_priv (context,
			*auth_context,
			&krb_priv_data,
			out_data,
			NULL);

    if (ret) {
	krb5_warn (context, ret, "krb5_rd_priv");
	reply_error ((*ticket)->server->realm, s, sa, sa_size, ret, 3,
		     "Bad request");
	goto out;
    }
    return 0;
out:
    krb5_free_ticket (context, *ticket);
    ticket = NULL;
    return 1;
}

static void
process (krb5_realm *realms,
	 krb5_keytab keytab,
	 int s,
	 krb5_address *this_addr,
	 struct sockaddr *sa,
	 int sa_size,
	 u_char *msg,
	 int len)
{
    krb5_error_code ret;
    krb5_auth_context auth_context = NULL;
    krb5_data out_data;
    krb5_ticket *ticket;
    krb5_address other_addr;
    uint16_t version;

    memset(&other_addr, 0, sizeof(other_addr));
    krb5_data_zero (&out_data);

    ret = krb5_auth_con_init (context, &auth_context);
    if (ret) {
	krb5_warn (context, ret, "krb5_auth_con_init");
	return;
    }

    krb5_auth_con_setflags (context, auth_context,
			    KRB5_AUTH_CONTEXT_DO_SEQUENCE);

    ret = krb5_sockaddr2address (context, sa, &other_addr);
    if (ret) {
	krb5_warn (context, ret, "krb5_sockaddr2address");
	goto out;
    }

    ret = krb5_auth_con_setaddrs (context, auth_context, this_addr, NULL);
    if (ret) {
	krb5_warn (context, ret, "krb5_auth_con_setaddr(this)");
	goto out;
    }

    if (verify (&auth_context, realms, keytab, &ticket, &out_data,
		&version, s, sa, sa_size, msg, len, &other_addr) == 0)
    {
	/*
	 * We always set the client_addr, to assume that the client
	 * can ignore it if it choose to do so (just the server does
	 * so for addressless tickets).
	 */
	ret = krb5_auth_con_setaddrs (context, auth_context, 
				      this_addr, &other_addr);
	if (ret) {
	    krb5_warn (context, ret, "krb5_auth_con_setaddr(other)");
	    goto out;
	}

	change (auth_context,
		ticket->client,
		version,
		s,
		sa, sa_size,
		&out_data);
	memset (out_data.data, 0, out_data.length);
	krb5_free_ticket (context, ticket);
    }

out:
    krb5_free_address(context, &other_addr);
    krb5_data_free(&out_data);
    krb5_auth_con_free(context, auth_context);
}

static int
doit (krb5_keytab keytab, int port)
{
    krb5_error_code ret;
    int *sockets;
    int maxfd;
    krb5_realm *realms;
    krb5_addresses addrs;
    unsigned n, i;
    fd_set real_fdset;
    struct sockaddr_storage __ss;
    struct sockaddr *sa = (struct sockaddr *)&__ss;

    ret = krb5_get_default_realms(context, &realms);
    if (ret)
	krb5_err (context, 1, ret, "krb5_get_default_realms");

    if (explicit_addresses.len) {
	addrs = explicit_addresses;
    } else {
	ret = krb5_get_all_server_addrs (context, &addrs);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_get_all_server_addrs");
    }
    n = addrs.len;

    sockets = malloc (n * sizeof(*sockets));
    if (sockets == NULL)
	krb5_errx (context, 1, "out of memory");
    maxfd = -1;
    FD_ZERO(&real_fdset);
    for (i = 0; i < n; ++i) {
	krb5_socklen_t sa_size = sizeof(__ss);

	krb5_addr2sockaddr (context, &addrs.val[i], sa, &sa_size, port);

	sockets[i] = socket (sa->sa_family, SOCK_DGRAM, 0);
	if (sockets[i] < 0)
	    krb5_err (context, 1, errno, "socket");
	if (bind (sockets[i], sa, sa_size) < 0) {
	    char str[128];
	    size_t len;
	    int save_errno = errno;

	    ret = krb5_print_address (&addrs.val[i], str, sizeof(str), &len);
	    if (ret)
		strlcpy(str, "unknown address", sizeof(str));
	    krb5_warn (context, save_errno, "bind(%s)", str);
	    continue;
	}
	maxfd = max (maxfd, sockets[i]);
	if (maxfd >= FD_SETSIZE)
	    krb5_errx (context, 1, "fd too large");
	FD_SET(sockets[i], &real_fdset);
    }
    if (maxfd == -1)
	krb5_errx (context, 1, "No sockets!");

    while(exit_flag == 0) {
	krb5_ssize_t retx;
	fd_set fdset = real_fdset;

	retx = select (maxfd + 1, &fdset, NULL, NULL, NULL);
	if (retx < 0) {
	    if (errno == EINTR)
		continue;
	    else
		krb5_err (context, 1, errno, "select");
	}
	for (i = 0; i < n; ++i)
	    if (FD_ISSET(sockets[i], &fdset)) {
		u_char buf[BUFSIZ];
		socklen_t addrlen = sizeof(__ss);

		retx = recvfrom(sockets[i], buf, sizeof(buf), 0,
				sa, &addrlen);
		if (retx < 0) {
		    if(errno == EINTR)
			break;
		    else
			krb5_err (context, 1, errno, "recvfrom");
		}

		process (realms, keytab, sockets[i],
			 &addrs.val[i],
			 sa, addrlen,
			 buf, retx);
	    }
    }

    for (i = 0; i < n; ++i)
	close(sockets[i]);
    free(sockets);

    krb5_free_addresses (context, &addrs);
    krb5_free_host_realm (context, realms);
    krb5_free_context (context);
    return 0;
}

static RETSIGTYPE
sigterm(int sig)
{
    exit_flag = 1;
}

static const char *check_library  = NULL;
static const char *check_function = NULL;
static getarg_strings policy_libraries = { 0, NULL };
static char sHDB[] = "HDB:";
static char *keytab_str = sHDB;
static char *realm_str;
static int version_flag;
static int help_flag;
static char *port_str;
static char *config_file;

struct getargs args[] = {
#ifdef HAVE_DLOPEN
    { "check-library", 0, arg_string, &check_library,
      "library to load password check function from", "library" },
    { "check-function", 0, arg_string, &check_function,
      "password check function to load", "function" },
    { "policy-libraries", 0, arg_strings, &policy_libraries,
      "password check function to load", "function" },
#endif
    { "addresses",	0,	arg_strings, &addresses_str,
      "addresses to listen on", "list of addresses" },
    { "keytab", 'k', arg_string, &keytab_str,
      "keytab to get authentication key from", "kspec" },
    { "config-file", 'c', arg_string, &config_file, NULL, NULL },
    { "realm", 'r', arg_string, &realm_str, "default realm", "realm" },
    { "port",  'p', arg_string, &port_str, "port", NULL },
    { "version", 0, arg_flag, &version_flag, NULL, NULL },
    { "help", 0, arg_flag, &help_flag, NULL, NULL }
};
int num_args = sizeof(args) / sizeof(args[0]);

int
main (int argc, char **argv)
{
    krb5_keytab keytab;
    krb5_error_code ret;
    char **files;
    int port, i;

    krb5_program_setup(&context, argc, argv, args, num_args, NULL);

    if(help_flag)
	krb5_std_usage(0, args, num_args);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    if (config_file == NULL) {
	asprintf(&config_file, "%s/kdc.conf", hdb_db_dir(context));
	if (config_file == NULL)
	    errx(1, "out of memory");
    }

    ret = krb5_prepend_config_files_default(config_file, &files);
    if (ret)
	krb5_err(context, 1, ret, "getting configuration files");

    ret = krb5_set_config_files(context, files);
    krb5_free_config_files(files);
    if (ret)
	krb5_err(context, 1, ret, "reading configuration files");

    if(realm_str)
	krb5_set_default_realm(context, realm_str);

    krb5_openlog (context, "kpasswdd", &log_facility);
    krb5_set_warn_dest(context, log_facility);

    if (port_str != NULL) {
	struct servent *s = roken_getservbyname (port_str, "udp");

	if (s != NULL)
	    port = s->s_port;
	else {
	    char *ptr;

	    port = strtol (port_str, &ptr, 10);
	    if (port == 0 && ptr == port_str)
		krb5_errx (context, 1, "bad port `%s'", port_str);
	    port = htons(port);
	}
    } else
	port = krb5_getportbyname (context, "kpasswd", "udp", KPASSWD_PORT);

    ret = krb5_kt_register(context, &hdb_kt_ops);
    if(ret)
	krb5_err(context, 1, ret, "krb5_kt_register");

    ret = krb5_kt_resolve(context, keytab_str, &keytab);
    if(ret)
	krb5_err(context, 1, ret, "%s", keytab_str);

    kadm5_setup_passwd_quality_check (context, check_library, check_function);

    for (i = 0; i < policy_libraries.num_strings; i++) {
	ret = kadm5_add_passwd_quality_verifier(context,
						policy_libraries.strings[i]);
	if (ret)
	    krb5_err(context, 1, ret, "kadm5_add_passwd_quality_verifier");
    }
    ret = kadm5_add_passwd_quality_verifier(context, NULL);
    if (ret)
	krb5_err(context, 1, ret, "kadm5_add_passwd_quality_verifier");


    explicit_addresses.len = 0;

    if (addresses_str.num_strings) {
	int j;

	for (j = 0; j < addresses_str.num_strings; ++j)
	    add_one_address (addresses_str.strings[j], j == 0);
	free_getarg_strings (&addresses_str);
    } else {
	char **foo = krb5_config_get_strings (context, NULL,
					      "kdc", "addresses", NULL);

	if (foo != NULL) {
	    add_one_address (*foo++, TRUE);
	    while (*foo)
		add_one_address (*foo++, FALSE);
	}
    }

#ifdef HAVE_SIGACTION
    {
	struct sigaction sa;

	sa.sa_flags = 0;
	sa.sa_handler = sigterm;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
    }
#else
    signal(SIGINT,  sigterm);
    signal(SIGTERM, sigterm);
#endif

    pidfile(NULL);

    return doit (keytab, port);
}
