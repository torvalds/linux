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

#include <config.h>

RCSID("$Id$");

#if	defined(AUTHENTICATION) && defined(ENCRYPTION) && defined(DES_ENCRYPTION)
#include <arpa/telnet.h>
#include <stdio.h>
#ifdef	__STDC__
#include <stdlib.h>
#include <string.h>
#endif
#include <roken.h>
#ifdef SOCKS
#include <socks.h>
#endif

#include "encrypt.h"
#include "misc-proto.h"

#include "crypto-headers.h"

extern int encrypt_debug_mode;

#define	CFB	0
#define	OFB	1

#define	NO_SEND_IV	1
#define	NO_RECV_IV	2
#define	NO_KEYID	4
#define	IN_PROGRESS	(NO_SEND_IV|NO_RECV_IV|NO_KEYID)
#define	SUCCESS		0
#define	FAILED		-1


struct stinfo {
  DES_cblock	str_output;
  DES_cblock	str_feed;
  DES_cblock	str_iv;
  DES_cblock	str_ikey;
  DES_key_schedule str_sched;
  int		str_index;
  int		str_flagshift;
};

struct fb {
	DES_cblock krbdes_key;
	DES_key_schedule krbdes_sched;
	DES_cblock temp_feed;
	unsigned char fb_feed[64];
	int need_start;
	int state[2];
	int keyid[2];
	struct stinfo streams[2];
};

static struct fb fb[2];

struct keyidlist {
	char	*keyid;
	int	keyidlen;
	char	*key;
	int	keylen;
	int	flags;
} keyidlist [] = {
	{ "\0", 1, 0, 0, 0 },		/* default key of zero */
	{ 0, 0, 0, 0, 0 }
};

#define	KEYFLAG_MASK	03

#define	KEYFLAG_NOINIT	00
#define	KEYFLAG_INIT	01
#define	KEYFLAG_OK	02
#define	KEYFLAG_BAD	03

#define	KEYFLAG_SHIFT	2

#define	SHIFT_VAL(a,b)	(KEYFLAG_SHIFT*((a)+((b)*2)))

#define	FB64_IV		1
#define	FB64_IV_OK	2
#define	FB64_IV_BAD	3


void fb64_stream_iv (DES_cblock, struct stinfo *);
void fb64_init (struct fb *);
static int fb64_start (struct fb *, int, int);
int fb64_is (unsigned char *, int, struct fb *);
int fb64_reply (unsigned char *, int, struct fb *);
static void fb64_session (Session_Key *, int, struct fb *);
void fb64_stream_key (DES_cblock, struct stinfo *);
int fb64_keyid (int, unsigned char *, int *, struct fb *);
void fb64_printsub(unsigned char *, size_t ,
		   unsigned char *, size_t , char *);

void cfb64_init(int server)
{
	fb64_init(&fb[CFB]);
	fb[CFB].fb_feed[4] = ENCTYPE_DES_CFB64;
	fb[CFB].streams[0].str_flagshift = SHIFT_VAL(0, CFB);
	fb[CFB].streams[1].str_flagshift = SHIFT_VAL(1, CFB);
}


void ofb64_init(int server)
{
	fb64_init(&fb[OFB]);
	fb[OFB].fb_feed[4] = ENCTYPE_DES_OFB64;
	fb[CFB].streams[0].str_flagshift = SHIFT_VAL(0, OFB);
	fb[CFB].streams[1].str_flagshift = SHIFT_VAL(1, OFB);
}

void fb64_init(struct fb *fbp)
{
	memset(fbp,0, sizeof(*fbp));
	fbp->state[0] = fbp->state[1] = FAILED;
	fbp->fb_feed[0] = IAC;
	fbp->fb_feed[1] = SB;
	fbp->fb_feed[2] = TELOPT_ENCRYPT;
	fbp->fb_feed[3] = ENCRYPT_IS;
}

/*
 * Returns:
 *	-1: some error.  Negotiation is done, encryption not ready.
 *	 0: Successful, initial negotiation all done.
 *	 1: successful, negotiation not done yet.
 *	 2: Not yet.  Other things (like getting the key from
 *	    Kerberos) have to happen before we can continue.
 */
int cfb64_start(int dir, int server)
{
	return(fb64_start(&fb[CFB], dir, server));
}

int ofb64_start(int dir, int server)
{
	return(fb64_start(&fb[OFB], dir, server));
}

static int fb64_start(struct fb *fbp, int dir, int server)
{
	int x;
	unsigned char *p;
	int state;

	switch (dir) {
	case DIR_DECRYPT:
		/*
		 * This is simply a request to have the other side
		 * start output (our input).  He will negotiate an
		 * IV so we need not look for it.
		 */
		state = fbp->state[dir-1];
		if (state == FAILED)
			state = IN_PROGRESS;
		break;

	case DIR_ENCRYPT:
		state = fbp->state[dir-1];
		if (state == FAILED)
			state = IN_PROGRESS;
		else if ((state & NO_SEND_IV) == 0) {
			break;
		}

		if (!VALIDKEY(fbp->krbdes_key)) {
		        fbp->need_start = 1;
			break;
		}

		state &= ~NO_SEND_IV;
		state |= NO_RECV_IV;
		if (encrypt_debug_mode)
			printf("Creating new feed\r\n");
		/*
		 * Create a random feed and send it over.
		 */
		do {
		    if (RAND_bytes(fbp->temp_feed,
				   sizeof(*fbp->temp_feed)) != 1)
			abort();
		    DES_set_odd_parity(&fbp->temp_feed);
		} while(DES_is_weak_key(&fbp->temp_feed));

		p = fbp->fb_feed + 3;
		*p++ = ENCRYPT_IS;
		p++;
		*p++ = FB64_IV;
		for (x = 0; x < sizeof(DES_cblock); ++x) {
			if ((*p++ = fbp->temp_feed[x]) == IAC)
				*p++ = IAC;
		}
		*p++ = IAC;
		*p++ = SE;
		printsub('>', &fbp->fb_feed[2], p - &fbp->fb_feed[2]);
		telnet_net_write(fbp->fb_feed, p - fbp->fb_feed);
		break;
	default:
		return(FAILED);
	}
	return(fbp->state[dir-1] = state);
}

/*
 * Returns:
 *	-1: some error.  Negotiation is done, encryption not ready.
 *	 0: Successful, initial negotiation all done.
 *	 1: successful, negotiation not done yet.
 */

int cfb64_is(unsigned char *data, int cnt)
{
	return(fb64_is(data, cnt, &fb[CFB]));
}

int ofb64_is(unsigned char *data, int cnt)
{
	return(fb64_is(data, cnt, &fb[OFB]));
}


int fb64_is(unsigned char *data, int cnt, struct fb *fbp)
{
	unsigned char *p;
	int state = fbp->state[DIR_DECRYPT-1];

	if (cnt-- < 1)
		goto failure;

	switch (*data++) {
	case FB64_IV:
		if (cnt != sizeof(DES_cblock)) {
			if (encrypt_debug_mode)
				printf("CFB64: initial vector failed on size\r\n");
			state = FAILED;
			goto failure;
		}

		if (encrypt_debug_mode)
			printf("CFB64: initial vector received\r\n");

		if (encrypt_debug_mode)
			printf("Initializing Decrypt stream\r\n");

		fb64_stream_iv(data, &fbp->streams[DIR_DECRYPT-1]);

		p = fbp->fb_feed + 3;
		*p++ = ENCRYPT_REPLY;
		p++;
		*p++ = FB64_IV_OK;
		*p++ = IAC;
		*p++ = SE;
		printsub('>', &fbp->fb_feed[2], p - &fbp->fb_feed[2]);
		telnet_net_write(fbp->fb_feed, p - fbp->fb_feed);

		state = fbp->state[DIR_DECRYPT-1] = IN_PROGRESS;
		break;

	default:
		if (encrypt_debug_mode) {
			printf("Unknown option type: %d\r\n", *(data-1));
			printd(data, cnt);
			printf("\r\n");
		}
		/* FALL THROUGH */
	failure:
		/*
		 * We failed.  Send an FB64_IV_BAD option
		 * to the other side so it will know that
		 * things failed.
		 */
		p = fbp->fb_feed + 3;
		*p++ = ENCRYPT_REPLY;
		p++;
		*p++ = FB64_IV_BAD;
		*p++ = IAC;
		*p++ = SE;
		printsub('>', &fbp->fb_feed[2], p - &fbp->fb_feed[2]);
		telnet_net_write(fbp->fb_feed, p - fbp->fb_feed);

		break;
	}
	return(fbp->state[DIR_DECRYPT-1] = state);
}

/*
 * Returns:
 *	-1: some error.  Negotiation is done, encryption not ready.
 *	 0: Successful, initial negotiation all done.
 *	 1: successful, negotiation not done yet.
 */

int cfb64_reply(unsigned char *data, int cnt)
{
	return(fb64_reply(data, cnt, &fb[CFB]));
}

int ofb64_reply(unsigned char *data, int cnt)
{
	return(fb64_reply(data, cnt, &fb[OFB]));
}


int fb64_reply(unsigned char *data, int cnt, struct fb *fbp)
{
	int state = fbp->state[DIR_ENCRYPT-1];

	if (cnt-- < 1)
		goto failure;

	switch (*data++) {
	case FB64_IV_OK:
		fb64_stream_iv(fbp->temp_feed, &fbp->streams[DIR_ENCRYPT-1]);
		if (state == FAILED)
			state = IN_PROGRESS;
		state &= ~NO_RECV_IV;
		encrypt_send_keyid(DIR_ENCRYPT, (unsigned char *)"\0", 1, 1);
		break;

	case FB64_IV_BAD:
		memset(fbp->temp_feed, 0, sizeof(DES_cblock));
		fb64_stream_iv(fbp->temp_feed, &fbp->streams[DIR_ENCRYPT-1]);
		state = FAILED;
		break;

	default:
		if (encrypt_debug_mode) {
			printf("Unknown option type: %d\r\n", data[-1]);
			printd(data, cnt);
			printf("\r\n");
		}
		/* FALL THROUGH */
	failure:
		state = FAILED;
		break;
	}
	return(fbp->state[DIR_ENCRYPT-1] = state);
}

void cfb64_session(Session_Key *key, int server)
{
	fb64_session(key, server, &fb[CFB]);
}

void ofb64_session(Session_Key *key, int server)
{
	fb64_session(key, server, &fb[OFB]);
}

static void fb64_session(Session_Key *key, int server, struct fb *fbp)
{

	if (!key || key->type != SK_DES) {
		if (encrypt_debug_mode)
			printf("Can't set krbdes's session key (%d != %d)\r\n",
				key ? key->type : -1, SK_DES);
		return;
	}
	memcpy(fbp->krbdes_key, key->data, sizeof(DES_cblock));

	fb64_stream_key(fbp->krbdes_key, &fbp->streams[DIR_ENCRYPT-1]);
	fb64_stream_key(fbp->krbdes_key, &fbp->streams[DIR_DECRYPT-1]);

	RAND_seed(key->data, key->length);

	DES_set_key_checked((DES_cblock *)&fbp->krbdes_key,
			    &fbp->krbdes_sched);
	/*
	 * Now look to see if krbdes_start() was waiting for the key to
	 * show up.  If so, go ahead an call it now that we have the key.
	 */
	if (fbp->need_start) {
		fbp->need_start = 0;
		fb64_start(fbp, DIR_ENCRYPT, server);
	}
}

/*
 * We only accept a keyid of 0.  If we get a keyid of
 * 0, then mark the state as SUCCESS.
 */

int cfb64_keyid(int dir, unsigned char *kp, int *lenp)
{
	return(fb64_keyid(dir, kp, lenp, &fb[CFB]));
}

int ofb64_keyid(int dir, unsigned char *kp, int *lenp)
{
	return(fb64_keyid(dir, kp, lenp, &fb[OFB]));
}

int fb64_keyid(int dir, unsigned char *kp, int *lenp, struct fb *fbp)
{
	int state = fbp->state[dir-1];

	if (*lenp != 1 || (*kp != '\0')) {
		*lenp = 0;
		return(state);
	}

	if (state == FAILED)
		state = IN_PROGRESS;

	state &= ~NO_KEYID;

	return(fbp->state[dir-1] = state);
}

void fb64_printsub(unsigned char *data, size_t cnt,
		   unsigned char *buf, size_t buflen, char *type)
{
	char lbuf[32];
	int i;
	char *cp;

	buf[buflen-1] = '\0';		/* make sure it's NULL terminated */
	buflen -= 1;

	switch(data[2]) {
	case FB64_IV:
		snprintf(lbuf, sizeof(lbuf), "%s_IV", type);
		cp = lbuf;
		goto common;

	case FB64_IV_OK:
		snprintf(lbuf, sizeof(lbuf), "%s_IV_OK", type);
		cp = lbuf;
		goto common;

	case FB64_IV_BAD:
		snprintf(lbuf, sizeof(lbuf), "%s_IV_BAD", type);
		cp = lbuf;
		goto common;

	default:
		snprintf(lbuf, sizeof(lbuf), " %d (unknown)", data[2]);
		cp = lbuf;
	common:
		for (; (buflen > 0) && (*buf = *cp++); buf++)
			buflen--;
		for (i = 3; i < cnt; i++) {
			snprintf(lbuf, sizeof(lbuf), " %d", data[i]);
			for (cp = lbuf; (buflen > 0) && (*buf = *cp++); buf++)
				buflen--;
		}
		break;
	}
}

void cfb64_printsub(unsigned char *data, size_t cnt,
		    unsigned char *buf, size_t buflen)
{
	fb64_printsub(data, cnt, buf, buflen, "CFB64");
}

void ofb64_printsub(unsigned char *data, size_t cnt,
		    unsigned char *buf, size_t buflen)
{
	fb64_printsub(data, cnt, buf, buflen, "OFB64");
}

void fb64_stream_iv(DES_cblock seed, struct stinfo *stp)
{

	memcpy(stp->str_iv, seed,sizeof(DES_cblock));
	memcpy(stp->str_output, seed, sizeof(DES_cblock));

	DES_set_key_checked(&stp->str_ikey, &stp->str_sched);

	stp->str_index = sizeof(DES_cblock);
}

void fb64_stream_key(DES_cblock key, struct stinfo *stp)
{
	memcpy(stp->str_ikey, key, sizeof(DES_cblock));
	DES_set_key_checked((DES_cblock*)key, &stp->str_sched);

	memcpy(stp->str_output, stp->str_iv, sizeof(DES_cblock));

	stp->str_index = sizeof(DES_cblock);
}

/*
 * DES 64 bit Cipher Feedback
 *
 *     key --->+-----+
 *          +->| DES |--+
 *          |  +-----+  |
 *	    |           v
 *  INPUT --(--------->(+)+---> DATA
 *          |             |
 *	    +-------------+
 *
 *
 * Given:
 *	iV: Initial vector, 64 bits (8 bytes) long.
 *	Dn: the nth chunk of 64 bits (8 bytes) of data to encrypt (decrypt).
 *	On: the nth chunk of 64 bits (8 bytes) of encrypted (decrypted) output.
 *
 *	V0 = DES(iV, key)
 *	On = Dn ^ Vn
 *	V(n+1) = DES(On, key)
 */

void cfb64_encrypt(unsigned char *s, int c)
{
	struct stinfo *stp = &fb[CFB].streams[DIR_ENCRYPT-1];
	int index;

	index = stp->str_index;
	while (c-- > 0) {
		if (index == sizeof(DES_cblock)) {
			DES_cblock b;
			DES_ecb_encrypt(&stp->str_output, &b,&stp->str_sched, 1);
			memcpy(stp->str_feed, b, sizeof(DES_cblock));
			index = 0;
		}

		/* On encryption, we store (feed ^ data) which is cypher */
		*s = stp->str_output[index] = (stp->str_feed[index] ^ *s);
		s++;
		index++;
	}
	stp->str_index = index;
}

int cfb64_decrypt(int data)
{
	struct stinfo *stp = &fb[CFB].streams[DIR_DECRYPT-1];
	int index;

	if (data == -1) {
		/*
		 * Back up one byte.  It is assumed that we will
		 * never back up more than one byte.  If we do, this
		 * may or may not work.
		 */
		if (stp->str_index)
			--stp->str_index;
		return(0);
	}

	index = stp->str_index++;
	if (index == sizeof(DES_cblock)) {
		DES_cblock b;
		DES_ecb_encrypt(&stp->str_output,&b, &stp->str_sched, 1);
		memcpy(stp->str_feed, b, sizeof(DES_cblock));
		stp->str_index = 1;	/* Next time will be 1 */
		index = 0;		/* But now use 0 */
	}

	/* On decryption we store (data) which is cypher. */
	stp->str_output[index] = data;
	return(data ^ stp->str_feed[index]);
}

/*
 * DES 64 bit Output Feedback
 *
 * key --->+-----+
 *	+->| DES |--+
 *	|  +-----+  |
 *	+-----------+
 *	            v
 *  INPUT -------->(+) ----> DATA
 *
 * Given:
 *	iV: Initial vector, 64 bits (8 bytes) long.
 *	Dn: the nth chunk of 64 bits (8 bytes) of data to encrypt (decrypt).
 *	On: the nth chunk of 64 bits (8 bytes) of encrypted (decrypted) output.
 *
 *	V0 = DES(iV, key)
 *	V(n+1) = DES(Vn, key)
 *	On = Dn ^ Vn
 */

void ofb64_encrypt(unsigned char *s, int c)
{
	struct stinfo *stp = &fb[OFB].streams[DIR_ENCRYPT-1];
	int index;

	index = stp->str_index;
	while (c-- > 0) {
		if (index == sizeof(DES_cblock)) {
			DES_cblock b;
			DES_ecb_encrypt(&stp->str_feed,&b, &stp->str_sched, 1);
			memcpy(stp->str_feed, b, sizeof(DES_cblock));
			index = 0;
		}
		*s++ ^= stp->str_feed[index];
		index++;
	}
	stp->str_index = index;
}

int ofb64_decrypt(int data)
{
	struct stinfo *stp = &fb[OFB].streams[DIR_DECRYPT-1];
	int index;

	if (data == -1) {
		/*
		 * Back up one byte.  It is assumed that we will
		 * never back up more than one byte.  If we do, this
		 * may or may not work.
		 */
		if (stp->str_index)
			--stp->str_index;
		return(0);
	}

	index = stp->str_index++;
	if (index == sizeof(DES_cblock)) {
		DES_cblock b;
		DES_ecb_encrypt(&stp->str_feed,&b,&stp->str_sched, 1);
		memcpy(stp->str_feed, b, sizeof(DES_cblock));
		stp->str_index = 1;	/* Next time will be 1 */
		index = 0;		/* But now use 0 */
	}

	return(data ^ stp->str_feed[index]);
}
#endif

