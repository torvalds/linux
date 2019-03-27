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
 *
 */

#if 0
#ifndef lint
static const char sccsid[] = "@(#)auth.c	8.3 (Berkeley) 5/30/95";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


/*
 * Copyright (C) 1990 by the Massachusetts Institute of Technology
 *
 * Export of this software from the United States of America is assumed
 * to require a specific license from the United States Government.
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


#ifdef	AUTHENTICATION
#define	AUTH_NAMES
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/telnet.h>

#include "encrypt.h"
#include "auth.h"
#include "misc-proto.h"
#include "auth-proto.h"

#define	typemask(x)	((x) > 0 ? 1 << ((x)-1) : 0)

#ifdef	KRB4_ENCPWD
extern krb4encpwd_init();
extern krb4encpwd_send();
extern krb4encpwd_is();
extern krb4encpwd_reply();
extern krb4encpwd_status();
extern krb4encpwd_printsub();
#endif

#ifdef	RSA_ENCPWD
extern rsaencpwd_init();
extern rsaencpwd_send();
extern rsaencpwd_is();
extern rsaencpwd_reply();
extern rsaencpwd_status();
extern rsaencpwd_printsub();
#endif

int auth_debug_mode = 0;
static 	const char	*Name = "Noname";
static	int	Server = 0;
static	Authenticator	*authenticated = 0;
static	int	authenticating = 0;
static	int	validuser = 0;
static	unsigned char	_auth_send_data[256];
static	unsigned char	*auth_send_data;
static	int	auth_send_cnt = 0;

int auth_onoff(char *type, int on);
void auth_encrypt_user(char *name);

/*
 * Authentication types supported.  Plese note that these are stored
 * in priority order, i.e. try the first one first.
 */
Authenticator authenticators[] = {
#ifdef	KRB5
# ifdef	ENCRYPTION
	{ AUTHTYPE_KERBEROS_V5, AUTH_WHO_CLIENT|AUTH_HOW_MUTUAL,
				kerberos5_init,
				kerberos5_send_mutual,
				kerberos5_is,
				kerberos5_reply,
				kerberos5_status,
				kerberos5_printsub },
# endif	/* ENCRYPTION */
	{ AUTHTYPE_KERBEROS_V5, AUTH_WHO_CLIENT|AUTH_HOW_ONE_WAY,
				kerberos5_init,
				kerberos5_send_oneway,
				kerberos5_is,
				kerberos5_reply,
				kerberos5_status,
				kerberos5_printsub },
#endif
#ifdef	KRB4
# ifdef ENCRYPTION
	{ AUTHTYPE_KERBEROS_V4, AUTH_WHO_CLIENT|AUTH_HOW_MUTUAL,
				kerberos4_init,
				kerberos4_send,
				kerberos4_is,
				kerberos4_reply,
				kerberos4_status,
				kerberos4_printsub },
# endif	/* ENCRYPTION */
	{ AUTHTYPE_KERBEROS_V4, AUTH_WHO_CLIENT|AUTH_HOW_ONE_WAY,
				kerberos4_init,
				kerberos4_send,
				kerberos4_is,
				kerberos4_reply,
				kerberos4_status,
				kerberos4_printsub },
#endif
#ifdef	KRB4_ENCPWD
	{ AUTHTYPE_KRB4_ENCPWD, AUTH_WHO_CLIENT|AUTH_HOW_MUTUAL,
				krb4encpwd_init,
				krb4encpwd_send,
				krb4encpwd_is,
				krb4encpwd_reply,
				krb4encpwd_status,
				krb4encpwd_printsub },
#endif
#ifdef	RSA_ENCPWD
	{ AUTHTYPE_RSA_ENCPWD, AUTH_WHO_CLIENT|AUTH_HOW_ONE_WAY,
				rsaencpwd_init,
				rsaencpwd_send,
				rsaencpwd_is,
				rsaencpwd_reply,
				rsaencpwd_status,
				rsaencpwd_printsub },
#endif
#ifdef SRA
	{ AUTHTYPE_SRA, AUTH_WHO_CLIENT|AUTH_HOW_ONE_WAY,
				sra_init,
				sra_send,
				sra_is,
				sra_reply,
				sra_status,
				sra_printsub },

#endif
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
};

static Authenticator NoAuth = { 0, 0, 0, 0, 0, 0, 0, 0 };

static int	i_support = 0;
static int	i_wont_support = 0;

Authenticator *
findauthenticator(int type, int way)
{
	Authenticator *ap = authenticators;

	while (ap->type && (ap->type != type || ap->way != way))
		++ap;
	return(ap->type ? ap : 0);
}

void
auth_init(const char *name, int server)
{
	Authenticator *ap = authenticators;

	Server = server;
	Name = name;

	i_support = 0;
	authenticated = 0;
	authenticating = 0;
	while (ap->type) {
		if (!ap->init || (*ap->init)(ap, server)) {
			i_support |= typemask(ap->type);
			if (auth_debug_mode)
				printf(">>>%s: I support auth type %d %d\r\n",
					Name,
					ap->type, ap->way);
		}
		else if (auth_debug_mode)
			printf(">>>%s: Init failed: auth type %d %d\r\n",
				Name, ap->type, ap->way);
		++ap;
	}
}

void
auth_disable_name(char *name)
{
	int x;
	for (x = 0; x < AUTHTYPE_CNT; ++x) {
		if (AUTHTYPE_NAME(x) && !strcasecmp(name, AUTHTYPE_NAME(x))) {
			i_wont_support |= typemask(x);
			break;
		}
	}
}

int
getauthmask(char *type, int *maskp)
{
	int x;

	if (AUTHTYPE_NAME(0) && !strcasecmp(type, AUTHTYPE_NAME(0))) {
		*maskp = -1;
		return(1);
	}

	for (x = 1; x < AUTHTYPE_CNT; ++x) {
		if (AUTHTYPE_NAME(x) && !strcasecmp(type, AUTHTYPE_NAME(x))) {
			*maskp = typemask(x);
			return(1);
		}
	}
	return(0);
}

int
auth_enable(char *type)
{
	return(auth_onoff(type, 1));
}

int
auth_disable(char *type)
{
	return(auth_onoff(type, 0));
}

int
auth_onoff(char *type, int on)
{
	int i, mask = -1;
	Authenticator *ap;

	if (!strcasecmp(type, "?") || !strcasecmp(type, "help")) {
		printf("auth %s 'type'\n", on ? "enable" : "disable");
		printf("Where 'type' is one of:\n");
		printf("\t%s\n", AUTHTYPE_NAME(0));
		mask = 0;
		for (ap = authenticators; ap->type; ap++) {
			if ((mask & (i = typemask(ap->type))) != 0)
				continue;
			mask |= i;
			printf("\t%s\n", AUTHTYPE_NAME(ap->type));
		}
		return(0);
	}

	if (!getauthmask(type, &mask)) {
		printf("%s: invalid authentication type\n", type);
		return(0);
	}
	if (on)
		i_wont_support &= ~mask;
	else
		i_wont_support |= mask;
	return(1);
}

int
auth_togdebug(int on)
{
	if (on < 0)
		auth_debug_mode ^= 1;
	else
		auth_debug_mode = on;
	printf("auth debugging %s\n", auth_debug_mode ? "enabled" : "disabled");
	return(1);
}

int
auth_status(void)
{
	Authenticator *ap;
	int i, mask;

	if (i_wont_support == -1)
		printf("Authentication disabled\n");
	else
		printf("Authentication enabled\n");

	mask = 0;
	for (ap = authenticators; ap->type; ap++) {
		if ((mask & (i = typemask(ap->type))) != 0)
			continue;
		mask |= i;
		printf("%s: %s\n", AUTHTYPE_NAME(ap->type),
			(i_wont_support & typemask(ap->type)) ?
					"disabled" : "enabled");
	}
	return(1);
}

/*
 * This routine is called by the server to start authentication
 * negotiation.
 */
void
auth_request(void)
{
	static unsigned char str_request[64] = { IAC, SB,
						 TELOPT_AUTHENTICATION,
						 TELQUAL_SEND, };
	Authenticator *ap = authenticators;
	unsigned char *e = str_request + 4;

	if (!authenticating) {
		authenticating = 1;
		while (ap->type) {
			if (i_support & ~i_wont_support & typemask(ap->type)) {
				if (auth_debug_mode) {
					printf(">>>%s: Sending type %d %d\r\n",
						Name, ap->type, ap->way);
				}
				*e++ = ap->type;
				*e++ = ap->way;
			}
			++ap;
		}
		*e++ = IAC;
		*e++ = SE;
		net_write(str_request, e - str_request);
		printsub('>', &str_request[2], e - str_request - 2);
	}
}

/*
 * This is called when an AUTH SEND is received.
 * It should never arrive on the server side (as only the server can
 * send an AUTH SEND).
 * You should probably respond to it if you can...
 *
 * If you want to respond to the types out of order (i.e. even
 * if he sends  LOGIN KERBEROS and you support both, you respond
 * with KERBEROS instead of LOGIN (which is against what the
 * protocol says)) you will have to hack this code...
 */
void
auth_send(unsigned char *data, int cnt)
{
	Authenticator *ap;
	static unsigned char str_none[] = { IAC, SB, TELOPT_AUTHENTICATION,
					    TELQUAL_IS, AUTHTYPE_NULL, 0,
					    IAC, SE };
	if (Server) {
		if (auth_debug_mode) {
			printf(">>>%s: auth_send called!\r\n", Name);
		}
		return;
	}

	if (auth_debug_mode) {
		printf(">>>%s: auth_send got:", Name);
		printd(data, cnt); printf("\r\n");
	}

	/*
	 * Save the data, if it is new, so that we can continue looking
	 * at it if the authorization we try doesn't work
	 */
	if (data < _auth_send_data ||
	    data > _auth_send_data + sizeof(_auth_send_data)) {
		auth_send_cnt = (size_t)cnt > sizeof(_auth_send_data)
					? sizeof(_auth_send_data)
					: cnt;
		memmove((void *)_auth_send_data, (void *)data, auth_send_cnt);
		auth_send_data = _auth_send_data;
	} else {
		/*
		 * This is probably a no-op, but we just make sure
		 */
		auth_send_data = data;
		auth_send_cnt = cnt;
	}
	while ((auth_send_cnt -= 2) >= 0) {
		if (auth_debug_mode)
			printf(">>>%s: He supports %d\r\n",
				Name, *auth_send_data);
		if ((i_support & ~i_wont_support) & typemask(*auth_send_data)) {
			ap = findauthenticator(auth_send_data[0],
					       auth_send_data[1]);
			if (ap && ap->send) {
				if (auth_debug_mode)
					printf(">>>%s: Trying %d %d\r\n",
						Name, auth_send_data[0],
							auth_send_data[1]);
				if ((*ap->send)(ap)) {
					/*
					 * Okay, we found one we like
					 * and did it.
					 * we can go home now.
					 */
					if (auth_debug_mode)
						printf(">>>%s: Using type %d\r\n",
							Name, *auth_send_data);
					auth_send_data += 2;
					return;
				}
			}
			/* else
			 *	just continue on and look for the
			 *	next one if we didn't do anything.
			 */
		}
		auth_send_data += 2;
	}
	net_write(str_none, sizeof(str_none));
	printsub('>', &str_none[2], sizeof(str_none) - 2);
	if (auth_debug_mode)
		printf(">>>%s: Sent failure message\r\n", Name);
	auth_finished(0, AUTH_REJECT);
}

void
auth_send_retry(void)
{
	/*
	 * if auth_send_cnt <= 0 then auth_send will end up rejecting
	 * the authentication and informing the other side of this.
	 */
	auth_send(auth_send_data, auth_send_cnt);
}

void
auth_is(unsigned char *data, int cnt)
{
	Authenticator *ap;

	if (cnt < 2)
		return;

	if (data[0] == AUTHTYPE_NULL) {
		auth_finished(0, AUTH_REJECT);
		return;
	}

	if ((ap = findauthenticator(data[0], data[1]))) {
		if (ap->is)
			(*ap->is)(ap, data+2, cnt-2);
	} else if (auth_debug_mode)
		printf(">>>%s: Invalid authentication in IS: %d\r\n",
			Name, *data);
}

void
auth_reply(unsigned char *data, int cnt)
{
	Authenticator *ap;

	if (cnt < 2)
		return;

	if ((ap = findauthenticator(data[0], data[1]))) {
		if (ap->reply)
			(*ap->reply)(ap, data+2, cnt-2);
	} else if (auth_debug_mode)
		printf(">>>%s: Invalid authentication in SEND: %d\r\n",
			Name, *data);
}

void
auth_name(unsigned char *data, int cnt)
{
	unsigned char savename[256];

	if (cnt < 1) {
		if (auth_debug_mode)
			printf(">>>%s: Empty name in NAME\r\n", Name);
		return;
	}
	if ((size_t)cnt > sizeof(savename) - 1) {
		if (auth_debug_mode)
			printf(">>>%s: Name in NAME (%d) exceeds %d length\r\n",
					Name, cnt, (u_int)sizeof(savename)-1);
		return;
	}
	memmove((void *)savename, (void *)data, cnt);
	savename[cnt] = '\0';	/* Null terminate */
	if (auth_debug_mode)
		printf(">>>%s: Got NAME [%s]\r\n", Name, savename);
	auth_encrypt_user(savename);
}

int
auth_sendname(unsigned char *cp, int len)
{
	static unsigned char str_request[256+6]
			= { IAC, SB, TELOPT_AUTHENTICATION, TELQUAL_NAME, };
	unsigned char *e = str_request + 4;
	unsigned char *ee = &str_request[sizeof(str_request)-2];

	while (--len >= 0) {
		if ((*e++ = *cp++) == IAC)
			*e++ = IAC;
		if (e >= ee)
			return(0);
	}
	*e++ = IAC;
	*e++ = SE;
	net_write(str_request, e - str_request);
	printsub('>', &str_request[2], e - &str_request[2]);
	return(1);
}

void
auth_finished(Authenticator *ap, int result)
{
	if (!(authenticated = ap))
		authenticated = &NoAuth;
	validuser = result;
}

/* ARGSUSED */
static void
auth_intr(int sig __unused)
{
	auth_finished(0, AUTH_REJECT);
}

int
auth_wait(char *name)
{
	if (auth_debug_mode)
		printf(">>>%s: in auth_wait.\r\n", Name);

	if (Server && !authenticating)
		return(0);

	(void) signal(SIGALRM, auth_intr);
	alarm(30);
	while (!authenticated)
		if (telnet_spin())
			break;
	alarm(0);
	(void) signal(SIGALRM, SIG_DFL);

	/*
	 * Now check to see if the user is valid or not
	 */
	if (!authenticated || authenticated == &NoAuth)
		return(AUTH_REJECT);

	if (validuser == AUTH_VALID)
		validuser = AUTH_USER;

	if (authenticated->status)
		validuser = (*authenticated->status)(authenticated,
						     name, validuser);
	return(validuser);
}

void
auth_printsub(unsigned char *data, int cnt, unsigned char *buf, int buflen)
{
	Authenticator *ap;

	if ((ap = findauthenticator(data[1], data[2])) && ap->printsub)
		(*ap->printsub)(data, cnt, buf, buflen);
	else
		auth_gen_printsub(data, cnt, buf, buflen);
}

void
auth_gen_printsub(unsigned char *data, int cnt, unsigned char *buf, int buflen)
{
	unsigned char *cp;
	unsigned char tbuf[16];

	cnt -= 3;
	data += 3;
	buf[buflen-1] = '\0';
	buf[buflen-2] = '*';
	buflen -= 2;
	for (; cnt > 0; cnt--, data++) {
		sprintf((char *)tbuf, " %d", *data);
		for (cp = tbuf; *cp && buflen > 0; --buflen)
			*buf++ = *cp++;
		if (buflen <= 0)
			return;
	}
	*buf = '\0';
}
#endif
