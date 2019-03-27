/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (C) 1990 by the Massachusetts Institute of Technology
 *
 * Export of this software from the United States of America may
 * require a specific license from the United States Government.
 * It is the responsibility of any person or organization contemplating
 * export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 */

#include <config.h>

RCSID("$Id$");

#ifdef	KRB5

#include <arpa/telnet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <ctype.h>
#include <pwd.h>
#define Authenticator k5_Authenticator
#include <krb5.h>
#undef Authenticator
#include <roken.h>
#ifdef SOCKS
#include <socks.h>
#endif


#include "encrypt.h"
#include "auth.h"
#include "misc.h"

#if defined(DCE)
int dfsk5ok = 0;
int dfspag = 0;
int dfsfwd = 0;
#endif

int forward_flags = 0;  /* Flags get set in telnet/main.c on -f and -F */

int forward(int);
int forwardable(int);

/* These values need to be the same as those defined in telnet/main.c. */
/* Either define them in both places, or put in some common header file. */
#define OPTS_FORWARD_CREDS	0x00000002
#define OPTS_FORWARDABLE_CREDS	0x00000001


void kerberos5_forward (Authenticator *);

static unsigned char str_data[4] = { IAC, SB, TELOPT_AUTHENTICATION, 0 };

#define	KRB_AUTH		0	/* Authentication data follows */
#define	KRB_REJECT		1	/* Rejected (reason might follow) */
#define	KRB_ACCEPT		2	/* Accepted */
#define	KRB_RESPONSE		3	/* Response for mutual auth. */

#define KRB_FORWARD     	4       /* Forwarded credentials follow */
#define KRB_FORWARD_ACCEPT     	5       /* Forwarded credentials accepted */
#define KRB_FORWARD_REJECT     	6       /* Forwarded credentials rejected */

static	krb5_data auth;
static  krb5_ticket *ticket;

static krb5_context context;
static krb5_auth_context auth_context;

static int
Data(Authenticator *ap, int type, const void *d, int c)
{
    const unsigned char *cp, *cd = d;
    unsigned char *p0, *p;
    size_t len = sizeof(str_data) + 3 + 2;
    int ret;

    if (c == -1)
	c = strlen((const char*)cd);

    for (cp = cd; cp - cd < c; cp++, len++)
	if (*cp == IAC)
	    len++;

    p0 = malloc(len);
    if (p0 == NULL)
	return 0;

    memcpy(p0, str_data, sizeof(str_data));
    p = p0 + sizeof(str_data);

    if (auth_debug_mode) {
	printf("%s:%d: [%d] (%d)",
	       str_data[3] == TELQUAL_IS ? ">>>IS" : ">>>REPLY",
	       str_data[3],
	       type, c);
	printd(d, c);
	printf("\r\n");
    }
    *p++ = ap->type;
    *p++ = ap->way;
    *p++ = type;
    while (c-- > 0) {
	if ((*p++ = *cd++) == IAC)
	    *p++ = IAC;
    }
    *p++ = IAC;
    *p++ = SE;
    if (str_data[3] == TELQUAL_IS)
	printsub('>', &p0[2], len - 2);
    ret = telnet_net_write(p0, len);
    free(p0);
    return ret;
}

int
kerberos5_init(Authenticator *ap, int server)
{
    krb5_error_code ret;

    ret = krb5_init_context(&context);
    if (ret)
	return 0;
    if (server) {
	krb5_keytab kt;
	krb5_kt_cursor cursor;

	ret = krb5_kt_default(context, &kt);
	if (ret)
	    return 0;

	ret = krb5_kt_start_seq_get (context, kt, &cursor);
	if (ret) {
	    krb5_kt_close (context, kt);
	    return 0;
	}
	krb5_kt_end_seq_get (context, kt, &cursor);
	krb5_kt_close (context, kt);

	str_data[3] = TELQUAL_REPLY;
    } else
	str_data[3] = TELQUAL_IS;
    return(1);
}

extern int net;
static int
kerberos5_send(char *name, Authenticator *ap)
{
    krb5_error_code ret;
    krb5_ccache ccache;
    int ap_opts;
    krb5_data cksum_data;
    char ap_msg[2];

    if (!UserNameRequested) {
	if (auth_debug_mode) {
	    printf("Kerberos V5: no user name supplied\r\n");
	}
	return(0);
    }

    ret = krb5_cc_default(context, &ccache);
    if (ret) {
	if (auth_debug_mode) {
	    printf("Kerberos V5: could not get default ccache: %s\r\n",
		   krb5_get_err_text (context, ret));
	}
	return 0;
    }

    if ((ap->way & AUTH_HOW_MASK) == AUTH_HOW_MUTUAL)
	ap_opts = AP_OPTS_MUTUAL_REQUIRED;
    else
	ap_opts = 0;

    ap_opts |= AP_OPTS_USE_SUBKEY;

    ret = krb5_auth_con_init (context, &auth_context);
    if (ret) {
	if (auth_debug_mode) {
	    printf("Kerberos V5: krb5_auth_con_init failed (%s)\r\n",
		   krb5_get_err_text(context, ret));
	}
	return(0);
    }

    ret = krb5_auth_con_setaddrs_from_fd (context,
					  auth_context,
					  &net);
    if (ret) {
	if (auth_debug_mode) {
	    printf ("Kerberos V5:"
		    " krb5_auth_con_setaddrs_from_fd failed (%s)\r\n",
		    krb5_get_err_text(context, ret));
	}
	return(0);
    }

    krb5_auth_con_setkeytype (context, auth_context, KRB5_ENCTYPE_DES_CBC_CRC);

    ap_msg[0] = ap->type;
    ap_msg[1] = ap->way;

    cksum_data.length = sizeof(ap_msg);
    cksum_data.data   = ap_msg;


    {
	krb5_principal service;
	char sname[128];


	ret = krb5_sname_to_principal (context,
				       RemoteHostName,
				       NULL,
				       KRB5_NT_SRV_HST,
				       &service);
	if(ret) {
	    if (auth_debug_mode) {
		printf ("Kerberos V5:"
			" krb5_sname_to_principal(%s) failed (%s)\r\n",
			RemoteHostName, krb5_get_err_text(context, ret));
	    }
	    return 0;
	}
	ret = krb5_unparse_name_fixed(context, service, sname, sizeof(sname));
	if(ret) {
	    if (auth_debug_mode) {
		printf ("Kerberos V5:"
			" krb5_unparse_name_fixed failed (%s)\r\n",
			krb5_get_err_text(context, ret));
	    }
	    return 0;
	}
	printf("[ Trying %s (%s)... ]\r\n", name, sname);
	ret = krb5_mk_req_exact(context, &auth_context, ap_opts,
				service,
				&cksum_data, ccache, &auth);
	krb5_free_principal (context, service);

    }
    if (ret) {
	if (1 || auth_debug_mode) {
	    printf("Kerberos V5: mk_req failed (%s)\r\n",
		   krb5_get_err_text(context, ret));
	}
	return(0);
    }

    if (!auth_sendname((unsigned char *)UserNameRequested,
		       strlen(UserNameRequested))) {
	if (auth_debug_mode)
	    printf("Not enough room for user name\r\n");
	return(0);
    }
    if (!Data(ap, KRB_AUTH, auth.data, auth.length)) {
	if (auth_debug_mode)
	    printf("Not enough room for authentication data\r\n");
	return(0);
    }
    if (auth_debug_mode) {
	printf("Sent Kerberos V5 credentials to server\r\n");
    }
    return(1);
}

int
kerberos5_send_mutual(Authenticator *ap)
{
    return kerberos5_send("mutual KERBEROS5", ap);
}

int
kerberos5_send_oneway(Authenticator *ap)
{
    return kerberos5_send("KERBEROS5", ap);
}

static void log_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (auth_debug_mode) {
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fprintf(stdout, "\r\n");
    }
    va_start(ap, fmt);
    vsyslog(LOG_NOTICE, fmt, ap);
    va_end(ap);
}

void
kerberos5_is(Authenticator *ap, unsigned char *data, int cnt)
{
    krb5_error_code ret;
    krb5_data outbuf;
    krb5_keyblock *key_block;
    char *name;
    krb5_principal server;
    int zero = 0;

    if (cnt-- < 1)
	return;
    switch (*data++) {
    case KRB_AUTH:
	auth.data = (char *)data;
	auth.length = cnt;

	auth_context = NULL;

	ret = krb5_auth_con_init (context, &auth_context);
	if (ret) {
	    Data(ap, KRB_REJECT, "krb5_auth_con_init failed", -1);
	    auth_finished(ap, AUTH_REJECT);
	    log_message("Kerberos V5: krb5_auth_con_init failed (%s)",
			krb5_get_err_text(context, ret));
	    return;
	}

	ret = krb5_auth_con_setaddrs_from_fd (context,
					      auth_context,
					      &zero);
	if (ret) {
	    Data(ap, KRB_REJECT, "krb5_auth_con_setaddrs_from_fd failed", -1);
	    auth_finished(ap, AUTH_REJECT);
	    log_message("Kerberos V5: "
			"krb5_auth_con_setaddrs_from_fd failed (%s)",
			krb5_get_err_text(context, ret));
	    return;
	}

	ret = krb5_sock_to_principal (context,
				      0,
				      "host",
				      KRB5_NT_SRV_HST,
				      &server);
	if (ret) {
	    Data(ap, KRB_REJECT, "krb5_sock_to_principal failed", -1);
	    auth_finished(ap, AUTH_REJECT);
	    log_message("Kerberos V5: "
			"krb5_sock_to_principal failed (%s)",
			krb5_get_err_text(context, ret));
	    return;
	}

	ret = krb5_rd_req(context,
			  &auth_context,
			  &auth,
			  server,
			  NULL,
			  NULL,
			  &ticket);

	krb5_free_principal (context, server);
	if (ret) {
	    const char *errbuf2 = "Read req failed";
	    char *errbuf;
	    int ret2;

	    ret2 = asprintf(&errbuf,
			    "Read req failed: %s",
			    krb5_get_err_text(context, ret));
	    if (ret2 != -1)
		errbuf2 = errbuf;
	    Data(ap, KRB_REJECT, errbuf2, -1);
	    log_message("%s", errbuf2);
	    if (ret2 != -1)
		free (errbuf);
	    return;
	}

	{
	    char ap_msg[2];

	    ap_msg[0] = ap->type;
	    ap_msg[1] = ap->way;

	    ret = krb5_verify_authenticator_checksum(context,
						     auth_context,
						     ap_msg,
						     sizeof(ap_msg));

	    if (ret) {
		const char *errbuf2 = "Bad checksum";
		char *errbuf;
		int ret2;

		ret2 = asprintf(&errbuf, "Bad checksum: %s",
				krb5_get_err_text(context, ret));
		if (ret2 != -1)
		    errbuf2 = errbuf;
		Data(ap, KRB_REJECT, errbuf2, -1);
		log_message("%s", errbuf2);
		if (ret2 != -1)
		    free(errbuf);
		return;
	    }
	}
	ret = krb5_auth_con_getremotesubkey (context,
					     auth_context,
					     &key_block);

	if (ret) {
	    Data(ap, KRB_REJECT, "krb5_auth_con_getremotesubkey failed", -1);
	    auth_finished(ap, AUTH_REJECT);
	    log_message("Kerberos V5: "
			"krb5_auth_con_getremotesubkey failed (%s)",
			krb5_get_err_text(context, ret));
	    return;
	}

	if (key_block == NULL) {
	    ret = krb5_auth_con_getkey(context,
				       auth_context,
				       &key_block);
	}
	if (ret) {
	    Data(ap, KRB_REJECT, "krb5_auth_con_getkey failed", -1);
	    auth_finished(ap, AUTH_REJECT);
	    log_message("Kerberos V5: "
			"krb5_auth_con_getkey failed (%s)",
			krb5_get_err_text(context, ret));
	    return;
	}
	if (key_block == NULL) {
	    Data(ap, KRB_REJECT, "no subkey received", -1);
	    auth_finished(ap, AUTH_REJECT);
	    log_message("Kerberos V5: "
			"krb5_auth_con_getremotesubkey returned NULL key");
	    return;
	}

	if ((ap->way & AUTH_HOW_MASK) == AUTH_HOW_MUTUAL) {
	    ret = krb5_mk_rep(context, auth_context, &outbuf);
	    if (ret) {
		Data(ap, KRB_REJECT,
		     "krb5_mk_rep failed", -1);
		auth_finished(ap, AUTH_REJECT);
		log_message("Kerberos V5: "
			    "krb5_mk_rep failed (%s)",
			    krb5_get_err_text(context, ret));
		krb5_free_keyblock(context, key_block);
		return;
	    }
	    Data(ap, KRB_RESPONSE, outbuf.data, outbuf.length);
	}
	if (krb5_unparse_name(context, ticket->client, &name))
	    name = 0;

	if(UserNameRequested && krb5_kuserok(context,
					     ticket->client,
					     UserNameRequested)) {
	    Data(ap, KRB_ACCEPT, name, name ? -1 : 0);
	    log_message("%s accepted as user %s from %s",
			name ? name : "<unknown>",
			UserNameRequested ? UserNameRequested : "<unknown>",
			RemoteHostName ? RemoteHostName : "<unknown>");

	    if(key_block->keytype == ETYPE_DES_CBC_MD5 ||
	       key_block->keytype == ETYPE_DES_CBC_MD4 ||
	       key_block->keytype == ETYPE_DES_CBC_CRC) {
		Session_Key skey;

		skey.type = SK_DES;
		skey.length = 8;
		skey.data = key_block->keyvalue.data;
		encrypt_session_key(&skey, 0);
	    }

	} else {
	    const char *msg2 = "user is not authorized to login";
	    char *msg;

	    ret = asprintf (&msg, "user `%s' is not authorized to "
			    "login as `%s'",
			    name ? name : "<unknown>",
			    UserNameRequested ? UserNameRequested : "<nobody>");
	    if (ret != -1)
		msg2 = msg;
	    Data(ap, KRB_REJECT, (void *)msg2, -1);
	    if (ret != -1)
		free(msg);
	    auth_finished (ap, AUTH_REJECT);
	    krb5_free_keyblock(context, key_block);
	    break;
	}
	auth_finished(ap, AUTH_USER);
	krb5_free_keyblock(context, key_block);

	break;
    case KRB_FORWARD: {
	struct passwd *pwd;
	char ccname[1024];	/* XXX */
	krb5_data inbuf;
	krb5_ccache ccache;
	inbuf.data = (char *)data;
	inbuf.length = cnt;

	pwd = getpwnam (UserNameRequested);
	if (pwd == NULL)
	    break;

	snprintf (ccname, sizeof(ccname),
		  "FILE:/tmp/krb5cc_%lu", (unsigned long)pwd->pw_uid);

	ret = krb5_cc_resolve (context, ccname, &ccache);
	if (ret) {
	    log_message("Kerberos V5: could not get ccache: %s",
			krb5_get_err_text(context, ret));
	    break;
	}

	ret = krb5_cc_initialize (context,
				  ccache,
				  ticket->client);
	if (ret) {
	    log_message("Kerberos V5: could not init ccache: %s",
			krb5_get_err_text(context, ret));
	    break;
	}

#if defined(DCE)
	esetenv("KRB5CCNAME", ccname, 1);
#endif
	ret = krb5_rd_cred2 (context,
			     auth_context,
			     ccache,
			     &inbuf);
	if(ret) {
	    const char *errbuf2 = "Read forwarded creds failed";
	    char *errbuf;
	    int ret2;

	    ret2 = asprintf (&errbuf,
			     "Read forwarded creds failed: %s",
			     krb5_get_err_text (context, ret));
	    if (ret2 != -1)
		errbuf2 = errbuf;
	    Data(ap, KRB_FORWARD_REJECT, errbuf, -1);
	    log_message("Could not read forwarded credentials: %s", errbuf);

	    if (ret2 != -1)
		free (errbuf);
	} else {
	    Data(ap, KRB_FORWARD_ACCEPT, 0, 0);
#if defined(DCE)
	    dfsfwd = 1;
#endif
	}
	chown (ccname + 5, pwd->pw_uid, -1);
	log_message("Forwarded credentials obtained");
	break;
    }
    default:
	log_message("Unknown Kerberos option %d", data[-1]);
	Data(ap, KRB_REJECT, 0, 0);
	break;
    }
}

void
kerberos5_reply(Authenticator *ap, unsigned char *data, int cnt)
{
    static int mutual_complete = 0;

    if (cnt-- < 1)
	return;
    switch (*data++) {
    case KRB_REJECT:
	if (cnt > 0) {
	    printf("[ Kerberos V5 refuses authentication because %.*s ]\r\n",
		   cnt, data);
	} else
	    printf("[ Kerberos V5 refuses authentication ]\r\n");
	auth_send_retry();
	return;
    case KRB_ACCEPT: {
	krb5_error_code ret;
	Session_Key skey;
	krb5_keyblock *keyblock;

	if ((ap->way & AUTH_HOW_MASK) == AUTH_HOW_MUTUAL &&
	    !mutual_complete) {
	    printf("[ Kerberos V5 accepted you, but didn't provide mutual authentication! ]\r\n");
	    auth_send_retry();
	    return;
	}
	if (cnt)
	    printf("[ Kerberos V5 accepts you as ``%.*s'' ]\r\n", cnt, data);
	else
	    printf("[ Kerberos V5 accepts you ]\r\n");

	ret = krb5_auth_con_getlocalsubkey (context,
					    auth_context,
					    &keyblock);
	if (ret)
	    ret = krb5_auth_con_getkey (context,
					auth_context,
					&keyblock);
	if(ret) {
	    printf("[ krb5_auth_con_getkey: %s ]\r\n",
		   krb5_get_err_text(context, ret));
	    auth_send_retry();
	    return;
	}

	skey.type = SK_DES;
	skey.length = 8;
	skey.data = keyblock->keyvalue.data;
	encrypt_session_key(&skey, 0);
	krb5_free_keyblock (context, keyblock);
	auth_finished(ap, AUTH_USER);
	if (forward_flags & OPTS_FORWARD_CREDS)
	    kerberos5_forward(ap);
	break;
    }
    case KRB_RESPONSE:
	if ((ap->way & AUTH_HOW_MASK) == AUTH_HOW_MUTUAL) {
	    /* the rest of the reply should contain a krb_ap_rep */
	  krb5_ap_rep_enc_part *reply;
	  krb5_data inbuf;
	  krb5_error_code ret;

	  inbuf.length = cnt;
	  inbuf.data = (char *)data;

	  ret = krb5_rd_rep(context, auth_context, &inbuf, &reply);
	  if (ret) {
	      printf("[ Mutual authentication failed: %s ]\r\n",
		     krb5_get_err_text (context, ret));
	      auth_send_retry();
	      return;
	  }
	  krb5_free_ap_rep_enc_part(context, reply);
	  mutual_complete = 1;
	}
	return;
    case KRB_FORWARD_ACCEPT:
	printf("[ Kerberos V5 accepted forwarded credentials ]\r\n");
	return;
    case KRB_FORWARD_REJECT:
	printf("[ Kerberos V5 refuses forwarded credentials because %.*s ]\r\n",
	       cnt, data);
	return;
    default:
	if (auth_debug_mode)
	    printf("Unknown Kerberos option %d\r\n", data[-1]);
	return;
    }
}

int
kerberos5_status(Authenticator *ap, char *name, size_t name_sz, int level)
{
    if (level < AUTH_USER)
	return(level);

    if (UserNameRequested &&
	krb5_kuserok(context,
		     ticket->client,
		     UserNameRequested))
	{
	    strlcpy(name, UserNameRequested, name_sz);
#if defined(DCE)
	    dfsk5ok = 1;
#endif
	    return(AUTH_VALID);
	} else
	    return(AUTH_USER);
}

#define	BUMP(buf, len)		while (*(buf)) {++(buf), --(len);}
#define	ADDC(buf, len, c)	if ((len) > 0) {*(buf)++ = (c); --(len);}

void
kerberos5_printsub(unsigned char *data, size_t cnt,
		   unsigned char *buf, size_t buflen)
{
    int i;

    buf[buflen-1] = '\0';		/* make sure it's NULL terminated */
    buflen -= 1;

    switch(data[3]) {
    case KRB_REJECT:		/* Rejected (reason might follow) */
	strlcpy((char *)buf, " REJECT ", buflen);
	goto common;

    case KRB_ACCEPT:		/* Accepted (name might follow) */
	strlcpy((char *)buf, " ACCEPT ", buflen);
    common:
	BUMP(buf, buflen);
	if (cnt <= 4)
	    break;
	ADDC(buf, buflen, '"');
	for (i = 4; i < cnt; i++)
	    ADDC(buf, buflen, data[i]);
	ADDC(buf, buflen, '"');
	ADDC(buf, buflen, '\0');
	break;


    case KRB_AUTH:			/* Authentication data follows */
	strlcpy((char *)buf, " AUTH", buflen);
	goto common2;

    case KRB_RESPONSE:
	strlcpy((char *)buf, " RESPONSE", buflen);
	goto common2;

    case KRB_FORWARD:		/* Forwarded credentials follow */
	strlcpy((char *)buf, " FORWARD", buflen);
	goto common2;

    case KRB_FORWARD_ACCEPT:	/* Forwarded credentials accepted */
	strlcpy((char *)buf, " FORWARD_ACCEPT", buflen);
	goto common2;

    case KRB_FORWARD_REJECT:	/* Forwarded credentials rejected */
	/* (reason might follow) */
	strlcpy((char *)buf, " FORWARD_REJECT", buflen);
	goto common2;

    default:
	snprintf((char*)buf, buflen, " %d (unknown)", data[3]);
    common2:
	BUMP(buf, buflen);
	for (i = 4; i < cnt; i++) {
	    snprintf((char*)buf, buflen, " %d", data[i]);
	    BUMP(buf, buflen);
	}
	break;
    }
}

void
kerberos5_forward(Authenticator *ap)
{
    krb5_error_code ret;
    krb5_ccache     ccache;
    krb5_creds      creds;
    KDCOptions	    flags;
    krb5_data       out_data;
    krb5_principal  principal;

    ret = krb5_cc_default (context, &ccache);
    if (ret) {
	if (auth_debug_mode)
	    printf ("KerberosV5: could not get default ccache: %s\r\n",
		    krb5_get_err_text (context, ret));
	return;
    }

    ret = krb5_cc_get_principal (context, ccache, &principal);
    if (ret) {
	if (auth_debug_mode)
	    printf ("KerberosV5: could not get principal: %s\r\n",
		    krb5_get_err_text (context, ret));
	return;
    }

    memset (&creds, 0, sizeof(creds));

    creds.client = principal;

    ret = krb5_make_principal(context,
			      &creds.server,
			      principal->realm,
			      "krbtgt",
			      principal->realm,
			      NULL);

    if (ret) {
	if (auth_debug_mode)
	    printf ("KerberosV5: could not get principal: %s\r\n",
		    krb5_get_err_text (context, ret));
	return;
    }

    creds.times.endtime = 0;

    memset(&flags, 0, sizeof(flags));
    flags.forwarded = 1;
    if (forward_flags & OPTS_FORWARDABLE_CREDS)
	flags.forwardable = 1;

    ret = krb5_get_forwarded_creds (context,
				    auth_context,
				    ccache,
				    KDCOptions2int(flags),
				    RemoteHostName,
				    &creds,
				    &out_data);
    if (ret) {
	if (auth_debug_mode)
	    printf ("Kerberos V5: error getting forwarded creds: %s\r\n",
		    krb5_get_err_text (context, ret));
	return;
    }

    if(!Data(ap, KRB_FORWARD, out_data.data, out_data.length)) {
	if (auth_debug_mode)
	    printf("Not enough room for authentication data\r\n");
    } else {
	if (auth_debug_mode)
	    printf("Forwarded local Kerberos V5 credentials to server\r\n");
    }
}

#if defined(DCE)
/* if this was a K5 authentication try and join a PAG for the user. */
void
kerberos5_dfspag(void)
{
    if (dfsk5ok) {
	dfspag = krb5_dfs_pag(context, dfsfwd, ticket->client,
			      UserNameRequested);
    }
}
#endif

int
kerberos5_set_forward(int on)
{
    if(on == 0)
	forward_flags &= ~OPTS_FORWARD_CREDS;
    if(on == 1)
	forward_flags |= OPTS_FORWARD_CREDS;
    if(on == -1)
	forward_flags ^= OPTS_FORWARD_CREDS;
    return 0;
}

int
kerberos5_set_forwardable(int on)
{
    if(on == 0)
	forward_flags &= ~OPTS_FORWARDABLE_CREDS;
    if(on == 1)
	forward_flags |= OPTS_FORWARDABLE_CREDS;
    if(on == -1)
	forward_flags ^= OPTS_FORWARDABLE_CREDS;
    return 0;
}

#endif /* KRB5 */
