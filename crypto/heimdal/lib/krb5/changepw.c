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

#include "krb5_locl.h"

#undef __attribute__
#define __attribute__(X)


static void
str2data (krb5_data *d,
	  const char *fmt,
	  ...) __attribute__ ((format (printf, 2, 3)));

static void
str2data (krb5_data *d,
	  const char *fmt,
	  ...)
{
    va_list args;
    char *str;

    va_start(args, fmt);
    d->length = vasprintf (&str, fmt, args);
    va_end(args);
    d->data = str;
}

/*
 * Change password protocol defined by
 * draft-ietf-cat-kerb-chg-password-02.txt
 *
 * Share the response part of the protocol with MS set password
 * (RFC3244)
 */

static krb5_error_code
chgpw_send_request (krb5_context context,
		    krb5_auth_context *auth_context,
		    krb5_creds *creds,
		    krb5_principal targprinc,
		    int is_stream,
		    rk_socket_t sock,
		    const char *passwd,
		    const char *host)
{
    krb5_error_code ret;
    krb5_data ap_req_data;
    krb5_data krb_priv_data;
    krb5_data passwd_data;
    size_t len;
    u_char header[6];
    struct iovec iov[3];
    struct msghdr msghdr;

    if (is_stream)
	return KRB5_KPASSWD_MALFORMED;

    if (targprinc &&
	krb5_principal_compare(context, creds->client, targprinc) != TRUE)
	return KRB5_KPASSWD_MALFORMED;

    krb5_data_zero (&ap_req_data);

    ret = krb5_mk_req_extended (context,
				auth_context,
				AP_OPTS_MUTUAL_REQUIRED | AP_OPTS_USE_SUBKEY,
				NULL, /* in_data */
				creds,
				&ap_req_data);
    if (ret)
	return ret;

    passwd_data.data   = rk_UNCONST(passwd);
    passwd_data.length = strlen(passwd);

    krb5_data_zero (&krb_priv_data);

    ret = krb5_mk_priv (context,
			*auth_context,
			&passwd_data,
			&krb_priv_data,
			NULL);
    if (ret)
	goto out2;

    len = 6 + ap_req_data.length + krb_priv_data.length;
    header[0] = (len >> 8) & 0xFF;
    header[1] = (len >> 0) & 0xFF;
    header[2] = 0;
    header[3] = 1;
    header[4] = (ap_req_data.length >> 8) & 0xFF;
    header[5] = (ap_req_data.length >> 0) & 0xFF;

    memset(&msghdr, 0, sizeof(msghdr));
    msghdr.msg_name       = NULL;
    msghdr.msg_namelen    = 0;
    msghdr.msg_iov        = iov;
    msghdr.msg_iovlen     = sizeof(iov)/sizeof(*iov);
#if 0
    msghdr.msg_control    = NULL;
    msghdr.msg_controllen = 0;
#endif

    iov[0].iov_base    = (void*)header;
    iov[0].iov_len     = 6;
    iov[1].iov_base    = ap_req_data.data;
    iov[1].iov_len     = ap_req_data.length;
    iov[2].iov_base    = krb_priv_data.data;
    iov[2].iov_len     = krb_priv_data.length;

    if (rk_IS_SOCKET_ERROR( sendmsg (sock, &msghdr, 0) )) {
	ret = rk_SOCK_ERRNO;
	krb5_set_error_message(context, ret, "sendmsg %s: %s",
			       host, strerror(ret));
    }

    krb5_data_free (&krb_priv_data);
out2:
    krb5_data_free (&ap_req_data);
    return ret;
}

/*
 * Set password protocol as defined by RFC3244 --
 * Microsoft Windows 2000 Kerberos Change Password and Set Password Protocols
 */

static krb5_error_code
setpw_send_request (krb5_context context,
		    krb5_auth_context *auth_context,
		    krb5_creds *creds,
		    krb5_principal targprinc,
		    int is_stream,
		    rk_socket_t sock,
		    const char *passwd,
		    const char *host)
{
    krb5_error_code ret;
    krb5_data ap_req_data;
    krb5_data krb_priv_data;
    krb5_data pwd_data;
    ChangePasswdDataMS chpw;
    size_t len = 0;
    u_char header[4 + 6];
    u_char *p;
    struct iovec iov[3];
    struct msghdr msghdr;

    krb5_data_zero (&ap_req_data);

    ret = krb5_mk_req_extended (context,
				auth_context,
				AP_OPTS_MUTUAL_REQUIRED | AP_OPTS_USE_SUBKEY,
				NULL, /* in_data */
				creds,
				&ap_req_data);
    if (ret)
	return ret;

    chpw.newpasswd.length = strlen(passwd);
    chpw.newpasswd.data = rk_UNCONST(passwd);
    if (targprinc) {
	chpw.targname = &targprinc->name;
	chpw.targrealm = &targprinc->realm;
    } else {
	chpw.targname = NULL;
	chpw.targrealm = NULL;
    }

    ASN1_MALLOC_ENCODE(ChangePasswdDataMS, pwd_data.data, pwd_data.length,
		       &chpw, &len, ret);
    if (ret) {
	krb5_data_free (&ap_req_data);
	return ret;
    }

    if(pwd_data.length != len)
	krb5_abortx(context, "internal error in ASN.1 encoder");

    ret = krb5_mk_priv (context,
			*auth_context,
			&pwd_data,
			&krb_priv_data,
			NULL);
    if (ret)
	goto out2;

    len = 6 + ap_req_data.length + krb_priv_data.length;
    p = header;
    if (is_stream) {
	_krb5_put_int(p, len, 4);
	p += 4;
    }
    *p++ = (len >> 8) & 0xFF;
    *p++ = (len >> 0) & 0xFF;
    *p++ = 0xff;
    *p++ = 0x80;
    *p++ = (ap_req_data.length >> 8) & 0xFF;
    *p   = (ap_req_data.length >> 0) & 0xFF;

    memset(&msghdr, 0, sizeof(msghdr));
    msghdr.msg_name       = NULL;
    msghdr.msg_namelen    = 0;
    msghdr.msg_iov        = iov;
    msghdr.msg_iovlen     = sizeof(iov)/sizeof(*iov);
#if 0
    msghdr.msg_control    = NULL;
    msghdr.msg_controllen = 0;
#endif

    iov[0].iov_base    = (void*)header;
    if (is_stream)
	iov[0].iov_len     = 10;
    else
	iov[0].iov_len     = 6;
    iov[1].iov_base    = ap_req_data.data;
    iov[1].iov_len     = ap_req_data.length;
    iov[2].iov_base    = krb_priv_data.data;
    iov[2].iov_len     = krb_priv_data.length;

    if (rk_IS_SOCKET_ERROR( sendmsg (sock, &msghdr, 0) )) {
	ret = rk_SOCK_ERRNO;
	krb5_set_error_message(context, ret, "sendmsg %s: %s",
			       host, strerror(ret));
    }

    krb5_data_free (&krb_priv_data);
out2:
    krb5_data_free (&ap_req_data);
    krb5_data_free (&pwd_data);
    return ret;
}

static krb5_error_code
process_reply (krb5_context context,
	       krb5_auth_context auth_context,
	       int is_stream,
	       rk_socket_t sock,
	       int *result_code,
	       krb5_data *result_code_string,
	       krb5_data *result_string,
	       const char *host)
{
    krb5_error_code ret;
    u_char reply[1024 * 3];
    size_t len;
    uint16_t pkt_len, pkt_ver;
    krb5_data ap_rep_data;
    int save_errno;

    len = 0;
    if (is_stream) {
	while (len < sizeof(reply)) {
	    unsigned long size;

	    ret = recvfrom (sock, reply + len, sizeof(reply) - len,
			    0, NULL, NULL);
	    if (rk_IS_SOCKET_ERROR(ret)) {
		save_errno = rk_SOCK_ERRNO;
		krb5_set_error_message(context, save_errno,
				       "recvfrom %s: %s",
				       host, strerror(save_errno));
		return save_errno;
	    } else if (ret == 0) {
		krb5_set_error_message(context, 1,"recvfrom timeout %s", host);
		return 1;
	    }
	    len += ret;
	    if (len < 4)
		continue;
	    _krb5_get_int(reply, &size, 4);
	    if (size + 4 < len)
		continue;
	    memmove(reply, reply + 4, size);
	    len = size;
	    break;
	}
	if (len == sizeof(reply)) {
	    krb5_set_error_message(context, ENOMEM,
				   N_("Message too large from %s", "host"),
				   host);
	    return ENOMEM;
	}
    } else {
	ret = recvfrom (sock, reply, sizeof(reply), 0, NULL, NULL);
	if (rk_IS_SOCKET_ERROR(ret)) {
	    save_errno = rk_SOCK_ERRNO;
	    krb5_set_error_message(context, save_errno,
				   "recvfrom %s: %s",
				   host, strerror(save_errno));
	    return save_errno;
	}
	len = ret;
    }

    if (len < 6) {
	str2data (result_string, "server %s sent to too short message "
		  "(%zu bytes)", host, len);
	*result_code = KRB5_KPASSWD_MALFORMED;
	return 0;
    }

    pkt_len = (reply[0] << 8) | (reply[1]);
    pkt_ver = (reply[2] << 8) | (reply[3]);

    if ((pkt_len != len) || (reply[1] == 0x7e || reply[1] == 0x5e)) {
	KRB_ERROR error;
	size_t size;
	u_char *p;

	memset(&error, 0, sizeof(error));

	ret = decode_KRB_ERROR(reply, len, &error, &size);
	if (ret)
	    return ret;

	if (error.e_data->length < 2) {
	    str2data(result_string, "server %s sent too short "
		     "e_data to print anything usable", host);
	    free_KRB_ERROR(&error);
	    *result_code = KRB5_KPASSWD_MALFORMED;
	    return 0;
	}

	p = error.e_data->data;
	*result_code = (p[0] << 8) | p[1];
	if (error.e_data->length == 2)
	    str2data(result_string, "server only sent error code");
	else
	    krb5_data_copy (result_string,
			    p + 2,
			    error.e_data->length - 2);
	free_KRB_ERROR(&error);
	return 0;
    }

    if (pkt_len != len) {
	str2data (result_string, "client: wrong len in reply");
	*result_code = KRB5_KPASSWD_MALFORMED;
	return 0;
    }
    if (pkt_ver != KRB5_KPASSWD_VERS_CHANGEPW) {
	str2data (result_string,
		  "client: wrong version number (%d)", pkt_ver);
	*result_code = KRB5_KPASSWD_MALFORMED;
	return 0;
    }

    ap_rep_data.data = reply + 6;
    ap_rep_data.length  = (reply[4] << 8) | (reply[5]);

    if (reply + len < (u_char *)ap_rep_data.data + ap_rep_data.length) {
	str2data (result_string, "client: wrong AP len in reply");
	*result_code = KRB5_KPASSWD_MALFORMED;
	return 0;
    }

    if (ap_rep_data.length) {
	krb5_ap_rep_enc_part *ap_rep;
	krb5_data priv_data;
	u_char *p;

	priv_data.data   = (u_char*)ap_rep_data.data + ap_rep_data.length;
	priv_data.length = len - ap_rep_data.length - 6;

	ret = krb5_rd_rep (context,
			   auth_context,
			   &ap_rep_data,
			   &ap_rep);
	if (ret)
	    return ret;

	krb5_free_ap_rep_enc_part (context, ap_rep);

	ret = krb5_rd_priv (context,
			    auth_context,
			    &priv_data,
			    result_code_string,
			    NULL);
	if (ret) {
	    krb5_data_free (result_code_string);
	    return ret;
	}

	if (result_code_string->length < 2) {
	    *result_code = KRB5_KPASSWD_MALFORMED;
	    str2data (result_string,
		      "client: bad length in result");
	    return 0;
	}

        p = result_code_string->data;

        *result_code = (p[0] << 8) | p[1];
        krb5_data_copy (result_string,
                        (unsigned char*)result_code_string->data + 2,
                        result_code_string->length - 2);
        return 0;
    } else {
	KRB_ERROR error;
	size_t size;
	u_char *p;

	ret = decode_KRB_ERROR(reply + 6, len - 6, &error, &size);
	if (ret) {
	    return ret;
	}
	if (error.e_data->length < 2) {
	    krb5_warnx (context, "too short e_data to print anything usable");
	    return 1;		/* XXX */
	}

	p = error.e_data->data;
	*result_code = (p[0] << 8) | p[1];
	krb5_data_copy (result_string,
			p + 2,
			error.e_data->length - 2);
	return 0;
    }
}


/*
 * change the password using the credentials in `creds' (for the
 * principal indicated in them) to `newpw', storing the result of
 * the operation in `result_*' and an error code or 0.
 */

typedef krb5_error_code (*kpwd_send_request) (krb5_context,
					      krb5_auth_context *,
					      krb5_creds *,
					      krb5_principal,
					      int,
					      rk_socket_t,
					      const char *,
					      const char *);
typedef krb5_error_code (*kpwd_process_reply) (krb5_context,
					       krb5_auth_context,
					       int,
					       rk_socket_t,
					       int *,
					       krb5_data *,
					       krb5_data *,
					       const char *);

static struct kpwd_proc {
    const char *name;
    int flags;
#define SUPPORT_TCP	1
#define SUPPORT_UDP	2
    kpwd_send_request send_req;
    kpwd_process_reply process_rep;
} procs[] = {
    {
	"MS set password",
	SUPPORT_TCP|SUPPORT_UDP,
	setpw_send_request,
	process_reply
    },
    {
	"change password",
	SUPPORT_UDP,
	chgpw_send_request,
	process_reply
    },
    { NULL, 0, NULL, NULL }
};

/*
 *
 */

static krb5_error_code
change_password_loop (krb5_context	context,
		      krb5_creds	*creds,
		      krb5_principal	targprinc,
		      const char	*newpw,
		      int		*result_code,
		      krb5_data		*result_code_string,
		      krb5_data		*result_string,
		      struct kpwd_proc	*proc)
{
    krb5_error_code ret;
    krb5_auth_context auth_context = NULL;
    krb5_krbhst_handle handle = NULL;
    krb5_krbhst_info *hi;
    rk_socket_t sock;
    unsigned int i;
    int done = 0;
    krb5_realm realm;

    if (targprinc)
	realm = targprinc->realm;
    else
	realm = creds->client->realm;

    ret = krb5_auth_con_init (context, &auth_context);
    if (ret)
	return ret;

    krb5_auth_con_setflags (context, auth_context,
			    KRB5_AUTH_CONTEXT_DO_SEQUENCE);

    ret = krb5_krbhst_init (context, realm, KRB5_KRBHST_CHANGEPW, &handle);
    if (ret)
	goto out;

    while (!done && (ret = krb5_krbhst_next(context, handle, &hi)) == 0) {
	struct addrinfo *ai, *a;
	int is_stream;

	switch (hi->proto) {
	case KRB5_KRBHST_UDP:
	    if ((proc->flags & SUPPORT_UDP) == 0)
		continue;
	    is_stream = 0;
	    break;
	case KRB5_KRBHST_TCP:
	    if ((proc->flags & SUPPORT_TCP) == 0)
		continue;
	    is_stream = 1;
	    break;
	default:
	    continue;
	}

	ret = krb5_krbhst_get_addrinfo(context, hi, &ai);
	if (ret)
	    continue;

	for (a = ai; !done && a != NULL; a = a->ai_next) {
	    int replied = 0;

	    sock = socket (a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
	    if (rk_IS_BAD_SOCKET(sock))
		continue;
	    rk_cloexec(sock);

	    ret = connect(sock, a->ai_addr, a->ai_addrlen);
	    if (rk_IS_SOCKET_ERROR(ret)) {
		rk_closesocket (sock);
		goto out;
	    }

	    ret = krb5_auth_con_genaddrs (context, auth_context, sock,
					  KRB5_AUTH_CONTEXT_GENERATE_LOCAL_ADDR);
	    if (ret) {
		rk_closesocket (sock);
		goto out;
	    }

	    for (i = 0; !done && i < 5; ++i) {
		fd_set fdset;
		struct timeval tv;

		if (!replied) {
		    replied = 0;

		    ret = (*proc->send_req) (context,
					     &auth_context,
					     creds,
					     targprinc,
					     is_stream,
					     sock,
					     newpw,
					     hi->hostname);
		    if (ret) {
			rk_closesocket(sock);
			goto out;
		    }
		}

#ifndef NO_LIMIT_FD_SETSIZE
		if (sock >= FD_SETSIZE) {
		    ret = ERANGE;
		    krb5_set_error_message(context, ret,
					   "fd %d too large", sock);
		    rk_closesocket (sock);
		    goto out;
		}
#endif

		FD_ZERO(&fdset);
		FD_SET(sock, &fdset);
		tv.tv_usec = 0;
		tv.tv_sec  = 1 + (1 << i);

		ret = select (sock + 1, &fdset, NULL, NULL, &tv);
		if (rk_IS_SOCKET_ERROR(ret) && rk_SOCK_ERRNO != EINTR) {
		    rk_closesocket(sock);
		    goto out;
		}
		if (ret == 1) {
		    ret = (*proc->process_rep) (context,
						auth_context,
						is_stream,
						sock,
						result_code,
						result_code_string,
						result_string,
						hi->hostname);
		    if (ret == 0)
			done = 1;
		    else if (i > 0 && ret == KRB5KRB_AP_ERR_MUT_FAIL)
			replied = 1;
		} else {
		    ret = KRB5_KDC_UNREACH;
		}
	    }
	    rk_closesocket (sock);
	}
    }

 out:
    krb5_krbhst_free (context, handle);
    krb5_auth_con_free (context, auth_context);

    if (ret == KRB5_KDC_UNREACH) {
	krb5_set_error_message(context,
			       ret,
			       N_("Unable to reach any changepw server "
				 " in realm %s", "realm"), realm);
	*result_code = KRB5_KPASSWD_HARDERROR;
    }
    return ret;
}

#ifndef HEIMDAL_SMALLER

static struct kpwd_proc *
find_chpw_proto(const char *name)
{
    struct kpwd_proc *p;
    for (p = procs; p->name != NULL; p++) {
	if (strcmp(p->name, name) == 0)
	    return p;
    }
    return NULL;
}

/**
 * Deprecated: krb5_change_password() is deprecated, use krb5_set_password().
 *
 * @param context a Keberos context
 * @param creds
 * @param newpw
 * @param result_code
 * @param result_code_string
 * @param result_string
 *
 * @return On sucess password is changed.

 * @ingroup @krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_change_password (krb5_context	context,
		      krb5_creds	*creds,
		      const char	*newpw,
		      int		*result_code,
		      krb5_data		*result_code_string,
		      krb5_data		*result_string)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    struct kpwd_proc *p = find_chpw_proto("change password");

    *result_code = KRB5_KPASSWD_MALFORMED;
    result_code_string->data = result_string->data = NULL;
    result_code_string->length = result_string->length = 0;

    if (p == NULL)
	return KRB5_KPASSWD_MALFORMED;

    return change_password_loop(context, creds, NULL, newpw,
				result_code, result_code_string,
				result_string, p);
}
#endif /* HEIMDAL_SMALLER */

/**
 * Change password using creds.
 *
 * @param context a Keberos context
 * @param creds The initial kadmin/passwd for the principal or an admin principal
 * @param newpw The new password to set
 * @param targprinc if unset, the default principal is used.
 * @param result_code Result code, KRB5_KPASSWD_SUCCESS is when password is changed.
 * @param result_code_string binary message from the server, contains
 * at least the result_code.
 * @param result_string A message from the kpasswd service or the
 * library in human printable form. The string is NUL terminated.
 *
 * @return On sucess and *result_code is KRB5_KPASSWD_SUCCESS, the password is changed.

 * @ingroup @krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_password(krb5_context context,
		  krb5_creds *creds,
		  const char *newpw,
		  krb5_principal targprinc,
		  int *result_code,
		  krb5_data *result_code_string,
		  krb5_data *result_string)
{
    krb5_principal principal = NULL;
    krb5_error_code ret = 0;
    int i;

    *result_code = KRB5_KPASSWD_MALFORMED;
    krb5_data_zero(result_code_string);
    krb5_data_zero(result_string);

    if (targprinc == NULL) {
	ret = krb5_get_default_principal(context, &principal);
	if (ret)
	    return ret;
    } else
	principal = targprinc;

    for (i = 0; procs[i].name != NULL; i++) {
	*result_code = 0;
	ret = change_password_loop(context, creds, principal, newpw,
				   result_code, result_code_string,
				   result_string,
				   &procs[i]);
	if (ret == 0 && *result_code == 0)
	    break;
    }

    if (targprinc == NULL)
	krb5_free_principal(context, principal);
    return ret;
}

/*
 *
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_set_password_using_ccache(krb5_context context,
			       krb5_ccache ccache,
			       const char *newpw,
			       krb5_principal targprinc,
			       int *result_code,
			       krb5_data *result_code_string,
			       krb5_data *result_string)
{
    krb5_creds creds, *credsp;
    krb5_error_code ret;
    krb5_principal principal = NULL;

    *result_code = KRB5_KPASSWD_MALFORMED;
    result_code_string->data = result_string->data = NULL;
    result_code_string->length = result_string->length = 0;

    memset(&creds, 0, sizeof(creds));

    if (targprinc == NULL) {
	ret = krb5_cc_get_principal(context, ccache, &principal);
	if (ret)
	    return ret;
    } else
	principal = targprinc;

    ret = krb5_make_principal(context, &creds.server,
			      krb5_principal_get_realm(context, principal),
			      "kadmin", "changepw", NULL);
    if (ret)
	goto out;

    ret = krb5_cc_get_principal(context, ccache, &creds.client);
    if (ret) {
        krb5_free_principal(context, creds.server);
	goto out;
    }

    ret = krb5_get_credentials(context, 0, ccache, &creds, &credsp);
    krb5_free_principal(context, creds.server);
    krb5_free_principal(context, creds.client);
    if (ret)
	goto out;

    ret = krb5_set_password(context,
			    credsp,
			    newpw,
			    principal,
			    result_code,
			    result_code_string,
			    result_string);

    krb5_free_creds(context, credsp);

    return ret;
 out:
    if (targprinc == NULL)
	krb5_free_principal(context, principal);
    return ret;
}

/*
 *
 */

KRB5_LIB_FUNCTION const char* KRB5_LIB_CALL
krb5_passwd_result_to_string (krb5_context context,
			      int result)
{
    static const char *strings[] = {
	"Success",
	"Malformed",
	"Hard error",
	"Auth error",
	"Soft error" ,
	"Access denied",
	"Bad version",
	"Initial flag needed"
    };

    if (result < 0 || result > KRB5_KPASSWD_INITIAL_FLAG_NEEDED)
	return "unknown result code";
    else
	return strings[result];
}
