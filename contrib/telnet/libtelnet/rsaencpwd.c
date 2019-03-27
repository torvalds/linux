/*-
 * Copyright (c) 1992, 1993
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

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

#ifndef lint
static char sccsid[] = "@(#)rsaencpwd.c	8.3 (Berkeley) 5/30/95";
#endif /* not lint */


#ifdef	RSA_ENCPWD
/*
 * COPYRIGHT (C) 1990 DIGITAL EQUIPMENT CORPORATION
 * ALL RIGHTS RESERVED
 *
 * "Digital Equipment Corporation authorizes the reproduction,
 * distribution and modification of this software subject to the following
 * restrictions:
 *
 * 1.  Any partial or whole copy of this software, or any modification
 * thereof, must include this copyright notice in its entirety.
 *
 * 2.  This software is supplied "as is" with no warranty of any kind,
 * expressed or implied, for any purpose, including any warranty of fitness
 * or merchantibility.  DIGITAL assumes no responsibility for the use or
 * reliability of this software, nor promises to provide any form of
 * support for it on any basis.
 *
 * 3.  Distribution of this software is authorized only if no profit or
 * remuneration of any kind is received in exchange for such distribution.
 *
 * 4.  This software produces public key authentication certificates
 * bearing an expiration date established by DIGITAL and RSA Data
 * Security, Inc.  It may cease to generate certificates after the expiration
 * date.  Any modification of this software that changes or defeats
 * the expiration date or its effect is unauthorized.
 *
 * 5.  Software that will renew or extend the expiration date of
 * authentication certificates produced by this software may be obtained
 * from RSA Data Security, Inc., 10 Twin Dolphin Drive, Redwood City, CA
 * 94065, (415)595-8782, or from DIGITAL"
 *
 */

#include <sys/types.h>
#include <arpa/telnet.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "encrypt.h"
#include "auth.h"
#include "misc.h"
#include "cdc.h"

extern auth_debug_mode;

static unsigned char str_data[1024] = { IAC, SB, TELOPT_AUTHENTICATION, 0,
			  		AUTHTYPE_RSA_ENCPWD, };
static unsigned char str_name[1024] = { IAC, SB, TELOPT_AUTHENTICATION,
					TELQUAL_NAME, };

#define	RSA_ENCPWD_AUTH	0	/* Authentication data follows */
#define	RSA_ENCPWD_REJECT	1	/* Rejected (reason might follow) */
#define RSA_ENCPWD_ACCEPT	2	/* Accepted */
#define	RSA_ENCPWD_CHALLENGEKEY	3	/* Challenge and public key */

#define NAME_SZ   40
#define CHAL_SZ   20
#define PWD_SZ    40

static	KTEXT_ST auth;
static	char name[NAME_SZ];
static	char user_passwd[PWD_SZ];
static  char key_file[2*NAME_SZ];
static  char lhostname[NAME_SZ];
static char  challenge[CHAL_SZ];
static int   challenge_len;

	static int
Data(ap, type, d, c)
	Authenticator *ap;
	int type;
	void *d;
	int c;
{
	unsigned char *p = str_data + 4;
	unsigned char *cd = (unsigned char *)d;

	if (c == -1)
		c = strlen((char *)cd);

	if (0) {
		printf("%s:%d: [%d] (%d)",
			str_data[3] == TELQUAL_IS ? ">>>IS" : ">>>REPLY",
			str_data[3],
			type, c);
		printd(d, c);
		printf("\r\n");
	}
	*p++ = ap->type;
	*p++ = ap->way;
	if (type != NULL) *p++ = type;
	while (c-- > 0) {
		if ((*p++ = *cd++) == IAC)
			*p++ = IAC;
	}
	*p++ = IAC;
	*p++ = SE;
	if (str_data[3] == TELQUAL_IS)
		printsub('>', &str_data[2], p - (&str_data[2]));
	return(net_write(str_data, p - str_data));
}

	int
rsaencpwd_init(ap, server)
	Authenticator *ap;
	int server;
{
	char  *cp;
	FILE  *fp;

	if (server) {
		str_data[3] = TELQUAL_REPLY;
		memset(key_file, 0, sizeof(key_file));
		gethostname(lhostname, sizeof(lhostname));
		if ((cp = strchr(lhostname, '.')) != 0)  *cp = '\0';
		strcpy(key_file, "/etc/.");
		strcat(key_file, lhostname);
		strcat(key_file, "_privkey");
		if ((fp=fopen(key_file, "r"))==NULL) return(0);
		fclose(fp);
	} else {
		str_data[3] = TELQUAL_IS;
	}
	return(1);
}

	int
rsaencpwd_send(ap)
	Authenticator *ap;
{

	printf("[ Trying RSAENCPWD ... ]\n");
	if (!UserNameRequested) {
		return(0);
	}
	if (!auth_sendname(UserNameRequested, strlen(UserNameRequested))) {
		return(0);
	}
	if (!Data(ap, NULL, (void *)NULL, 0)) {
		return(0);
	}


	return(1);
}

	void
rsaencpwd_is(ap, data, cnt)
	Authenticator *ap;
	unsigned char *data;
	int cnt;
{
	Session_Key skey;
	Block datablock;
	char  r_passwd[PWD_SZ], r_user[NAME_SZ];
	char  *cp, key[160];
	char  chalkey[160], *ptr;
	FILE  *fp;
	int r, i, j, chalkey_len, len;
	time_t now;

	cnt--;
	switch (*data++) {
	case RSA_ENCPWD_AUTH:
		memmove((void *)auth.dat, (void *)data, auth.length = cnt);

		if ((fp=fopen(key_file, "r"))==NULL) {
		  Data(ap, RSA_ENCPWD_REJECT, (void *)"Auth failed", -1);
		  auth_finished(ap, AUTH_REJECT);
		  return;
		}
		/*
		 *  get privkey
		 */
		fscanf(fp, "%x;", &len);
		for (i=0;i<len;i++) {
		  j = getc(fp);  key[i]=j;
		}
		fclose(fp);

		r = accept_rsa_encpwd(&auth, key, challenge,
				      challenge_len, r_passwd);
		if (r < 0) {
		  Data(ap, RSA_ENCPWD_REJECT, (void *)"Auth failed", -1);
		  auth_finished(ap, AUTH_REJECT);
		  return;
		}
		auth_encrypt_userpwd(r_passwd);
		if (rsaencpwd_passwdok(UserNameRequested, UserPassword) == 0) {
		  /*
		   *  illegal username and password
		   */
		  Data(ap, RSA_ENCPWD_REJECT, (void *)"Illegal password", -1);
		  auth_finished(ap, AUTH_REJECT);
		  return;
		}

		Data(ap, RSA_ENCPWD_ACCEPT, (void *)0, 0);
		auth_finished(ap, AUTH_USER);
		break;


	case IAC:

		/*
		 * If we are doing mutual authentication, get set up to send
		 * the challenge, and verify it when the response comes back.
		 */
		if ((ap->way & AUTH_HOW_MASK) == AUTH_HOW_ONE_WAY) {
		  register int i;


		  time(&now);
		  if ((now % 2) == 0) {
		    sprintf(challenge, "%x", now);
		    challenge_len = strlen(challenge);
		  } else {
		    strcpy(challenge, "randchal");
		    challenge_len = 8;
		  }

		  if ((fp=fopen(key_file, "r"))==NULL) {
		    Data(ap, RSA_ENCPWD_REJECT, (void *)"Auth failed", -1);
		    auth_finished(ap, AUTH_REJECT);
		    return;
		  }
		  /*
		   *  skip privkey
		   */
		  fscanf(fp, "%x;", &len);
		  for (i=0;i<len;i++) {
		    j = getc(fp);
		  }
		  /*
		   * get pubkey
		   */
		  fscanf(fp, "%x;", &len);
		  for (i=0;i<len;i++) {
		    j = getc(fp);  key[i]=j;
		  }
		  fclose(fp);
		  chalkey[0] = 0x30;
		  ptr = (char *) &chalkey[1];
		  chalkey_len = 1+NumEncodeLengthOctets(i)+i+1+NumEncodeLengthOctets(challenge_len)+challenge_len;
		  EncodeLength(ptr, chalkey_len);
		  ptr +=NumEncodeLengthOctets(chalkey_len);
		  *ptr++ = 0x04;  /* OCTET STRING */
		  *ptr++ = challenge_len;
		  memmove(ptr, challenge, challenge_len);
		  ptr += challenge_len;
		  *ptr++ = 0x04;  /* OCTET STRING */
		  EncodeLength(ptr, i);
		  ptr += NumEncodeLengthOctets(i);
		  memmove(ptr, key, i);
		  chalkey_len = 1+NumEncodeLengthOctets(chalkey_len)+chalkey_len;
		  Data(ap, RSA_ENCPWD_CHALLENGEKEY, (void *)chalkey, chalkey_len);
		}
		break;

	default:
		Data(ap, RSA_ENCPWD_REJECT, 0, 0);
		break;
	}
}


	void
rsaencpwd_reply(ap, data, cnt)
	Authenticator *ap;
	unsigned char *data;
	int cnt;
{
	Session_Key skey;
	KTEXT_ST token;
	Block enckey;
	int r, pubkey_len;
	char	randchal[CHAL_SZ], *cp;
	char	chalkey[160], pubkey[128], *ptr;

	if (cnt-- < 1)
		return;
	switch (*data++) {
	case RSA_ENCPWD_REJECT:
		if (cnt > 0) {
			printf("[ RSA_ENCPWD refuses authentication because %.*s ]\r\n",
				cnt, data);
		} else
			printf("[ RSA_ENCPWD refuses authentication ]\r\n");
		auth_send_retry();
		return;
	case RSA_ENCPWD_ACCEPT:
		printf("[ RSA_ENCPWD accepts you ]\n");
		auth_finished(ap, AUTH_USER);
		return;
	case RSA_ENCPWD_CHALLENGEKEY:
		/*
		 * Verify that the response to the challenge is correct.
		 */

		memmove((void *)chalkey, (void *)data, cnt);
		ptr = (char *) &chalkey[0];
		ptr += DecodeHeaderLength(chalkey);
		if (*ptr != 0x04) {
		  return;
		}
		*ptr++;
		challenge_len = DecodeValueLength(ptr);
		ptr += NumEncodeLengthOctets(challenge_len);
		memmove(challenge, ptr, challenge_len);
		ptr += challenge_len;
		if (*ptr != 0x04) {
		  return;
		}
		*ptr++;
		pubkey_len = DecodeValueLength(ptr);
		ptr += NumEncodeLengthOctets(pubkey_len);
		memmove(pubkey, ptr, pubkey_len);
		memset(user_passwd, 0, sizeof(user_passwd));
		local_des_read_pw_string(user_passwd, sizeof(user_passwd)-1, "Password: ", 0);
		UserPassword = user_passwd;
		Challenge = challenge;
		r = init_rsa_encpwd(&token, user_passwd, challenge, challenge_len, pubkey);
		if (r < 0) {
		  token.length = 1;
		}

		if (!Data(ap, RSA_ENCPWD_AUTH, (void *)token.dat, token.length)) {
		  return;
		}

		break;

	default:
		return;
	}
}

	int
rsaencpwd_status(ap, name, level)
	Authenticator *ap;
	char *name;
	int level;
{

	if (level < AUTH_USER)
		return(level);

	if (UserNameRequested && rsaencpwd_passwdok(UserNameRequested, UserPassword)) {
		strcpy(name, UserNameRequested);
		return(AUTH_VALID);
	} else {
		return(AUTH_USER);
	}
}

#define	BUMP(buf, len)		while (*(buf)) {++(buf), --(len);}
#define	ADDC(buf, len, c)	if ((len) > 0) {*(buf)++ = (c); --(len);}

	void
rsaencpwd_printsub(data, cnt, buf, buflen)
	unsigned char *data, *buf;
	int cnt, buflen;
{
	char lbuf[32];
	register int i;

	buf[buflen-1] = '\0';		/* make sure its NULL terminated */
	buflen -= 1;

	switch(data[3]) {
	case RSA_ENCPWD_REJECT:	/* Rejected (reason might follow) */
		strncpy((char *)buf, " REJECT ", buflen);
		goto common;

	case RSA_ENCPWD_ACCEPT:	/* Accepted (name might follow) */
		strncpy((char *)buf, " ACCEPT ", buflen);
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

	case RSA_ENCPWD_AUTH:		/* Authentication data follows */
		strncpy((char *)buf, " AUTH", buflen);
		goto common2;

	case RSA_ENCPWD_CHALLENGEKEY:
		strncpy((char *)buf, " CHALLENGEKEY", buflen);
		goto common2;

	default:
		sprintf(lbuf, " %d (unknown)", data[3]);
		strncpy((char *)buf, lbuf, buflen);
	common2:
		BUMP(buf, buflen);
		for (i = 4; i < cnt; i++) {
			sprintf(lbuf, " %d", data[i]);
			strncpy((char *)buf, lbuf, buflen);
			BUMP(buf, buflen);
		}
		break;
	}
}

int rsaencpwd_passwdok(name, passwd)
char *name, *passwd;
{
  char *crypt();
  char *salt, *p;
  struct passwd *pwd;
  int   passwdok_status = 0;

  if (pwd = getpwnam(name))
    salt = pwd->pw_passwd;
  else salt = "xx";

  p = crypt(passwd, salt);

  if (pwd && !strcmp(p, pwd->pw_passwd)) {
    passwdok_status = 1;
  } else passwdok_status = 0;
  return(passwdok_status);
}

#endif
