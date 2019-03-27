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


#include <config.h>

RCSID("$Id$");

#if	defined(ENCRYPTION)

#define	ENCRYPT_NAMES
#include <arpa/telnet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <roken.h>
#ifdef SOCKS
#include <socks.h>
#endif

#include "encrypt.h"
#include "misc.h"


/*
 * These functions pointers point to the current routines
 * for encrypting and decrypting data.
 */
void	(*encrypt_output) (unsigned char *, int);
int	(*decrypt_input) (int);
char	*nclearto;

int encrypt_debug_mode = 0;
static int decrypt_mode = 0;
static int encrypt_mode = 0;
static int encrypt_verbose = 0;
static int autoencrypt = 0;
static int autodecrypt = 0;
static int havesessionkey = 0;
static int Server = 0;
static const char *Name = "Noname";

#define	typemask(x)	((x) > 0 ? 1 << ((x)-1) : 0)

static long i_support_encrypt = typemask(ENCTYPE_DES_CFB64)
     | typemask(ENCTYPE_DES_OFB64);
     static long i_support_decrypt = typemask(ENCTYPE_DES_CFB64)
     | typemask(ENCTYPE_DES_OFB64);
     static long i_wont_support_encrypt = 0;
     static long i_wont_support_decrypt = 0;
#define	I_SUPPORT_ENCRYPT	(i_support_encrypt & ~i_wont_support_encrypt)
#define	I_SUPPORT_DECRYPT	(i_support_decrypt & ~i_wont_support_decrypt)

     static long remote_supports_encrypt = 0;
     static long remote_supports_decrypt = 0;

     static Encryptions encryptions[] = {
#if	defined(DES_ENCRYPTION)
	 { "DES_CFB64",	ENCTYPE_DES_CFB64,
	   cfb64_encrypt,
	   cfb64_decrypt,
	   cfb64_init,
	   cfb64_start,
	   cfb64_is,
	   cfb64_reply,
	   cfb64_session,
	   cfb64_keyid,
	   cfb64_printsub },
	 { "DES_OFB64",	ENCTYPE_DES_OFB64,
	   ofb64_encrypt,
	   ofb64_decrypt,
	   ofb64_init,
	   ofb64_start,
	   ofb64_is,
	   ofb64_reply,
	   ofb64_session,
	   ofb64_keyid,
	   ofb64_printsub },
#endif
	 { 0, },
     };

static unsigned char str_send[64] = { IAC, SB, TELOPT_ENCRYPT,
				      ENCRYPT_SUPPORT };
static unsigned char str_suplen = 0;
static unsigned char str_start[72] = { IAC, SB, TELOPT_ENCRYPT };
static unsigned char str_end[] = { IAC, SB, TELOPT_ENCRYPT, 0, IAC, SE };

Encryptions *
findencryption(int type)
{
    Encryptions *ep = encryptions;

    if (!(I_SUPPORT_ENCRYPT & remote_supports_decrypt & typemask(type)))
	return(0);
    while (ep->type && ep->type != type)
	++ep;
    return(ep->type ? ep : 0);
}

Encryptions *
finddecryption(int type)
{
    Encryptions *ep = encryptions;

    if (!(I_SUPPORT_DECRYPT & remote_supports_encrypt & typemask(type)))
	return(0);
    while (ep->type && ep->type != type)
	++ep;
    return(ep->type ? ep : 0);
}

#define	MAXKEYLEN 64

static struct key_info {
    unsigned char keyid[MAXKEYLEN];
    int keylen;
    int dir;
    int *modep;
    Encryptions *(*getcrypt)();
} ki[2] = {
    { { 0 }, 0, DIR_ENCRYPT, &encrypt_mode, findencryption },
    { { 0 }, 0, DIR_DECRYPT, &decrypt_mode, finddecryption },
};

void
encrypt_init(const char *name, int server)
{
    Encryptions *ep = encryptions;

    Name = name;
    Server = server;
    i_support_encrypt = i_support_decrypt = 0;
    remote_supports_encrypt = remote_supports_decrypt = 0;
    encrypt_mode = 0;
    decrypt_mode = 0;
    encrypt_output = 0;
    decrypt_input = 0;
#ifdef notdef
    encrypt_verbose = !server;
#endif

    str_suplen = 4;

    while (ep->type) {
	if (encrypt_debug_mode)
	    printf(">>>%s: I will support %s\r\n",
		   Name, ENCTYPE_NAME(ep->type));
	i_support_encrypt |= typemask(ep->type);
	i_support_decrypt |= typemask(ep->type);
	if ((i_wont_support_decrypt & typemask(ep->type)) == 0)
	    if ((str_send[str_suplen++] = ep->type) == IAC)
		str_send[str_suplen++] = IAC;
	if (ep->init)
	    (*ep->init)(Server);
	++ep;
    }
    str_send[str_suplen++] = IAC;
    str_send[str_suplen++] = SE;
}

void
encrypt_list_types(void)
{
    Encryptions *ep = encryptions;

    printf("Valid encryption types:\n");
    while (ep->type) {
	printf("\t%s (%d)\r\n", ENCTYPE_NAME(ep->type), ep->type);
	++ep;
    }
}

int
EncryptEnable(char *type, char *mode)
{
    if (isprefix(type, "help") || isprefix(type, "?")) {
	printf("Usage: encrypt enable <type> [input|output]\n");
	encrypt_list_types();
	return(0);
    }
    if (EncryptType(type, mode))
	return(EncryptStart(mode));
    return(0);
}

int
EncryptDisable(char *type, char *mode)
{
    Encryptions *ep;
    int ret = 0;

    if (isprefix(type, "help") || isprefix(type, "?")) {
	printf("Usage: encrypt disable <type> [input|output]\n");
	encrypt_list_types();
    } else if ((ep = (Encryptions *)genget(type, (char**)encryptions,
					   sizeof(Encryptions))) == 0) {
	printf("%s: invalid encryption type\n", type);
    } else if (Ambiguous(ep)) {
	printf("Ambiguous type '%s'\n", type);
    } else {
	if ((mode == 0) || (isprefix(mode, "input") ? 1 : 0)) {
	    if (decrypt_mode == ep->type)
		EncryptStopInput();
	    i_wont_support_decrypt |= typemask(ep->type);
	    ret = 1;
	}
	if ((mode == 0) || (isprefix(mode, "output"))) {
	    if (encrypt_mode == ep->type)
		EncryptStopOutput();
	    i_wont_support_encrypt |= typemask(ep->type);
	    ret = 1;
	}
	if (ret == 0)
	    printf("%s: invalid encryption mode\n", mode);
    }
    return(ret);
}

int
EncryptType(char *type, char *mode)
{
    Encryptions *ep;
    int ret = 0;

    if (isprefix(type, "help") || isprefix(type, "?")) {
	printf("Usage: encrypt type <type> [input|output]\n");
	encrypt_list_types();
    } else if ((ep = (Encryptions *)genget(type, (char**)encryptions,
					   sizeof(Encryptions))) == 0) {
	printf("%s: invalid encryption type\n", type);
    } else if (Ambiguous(ep)) {
	printf("Ambiguous type '%s'\n", type);
    } else {
	if ((mode == 0) || isprefix(mode, "input")) {
	    decrypt_mode = ep->type;
	    i_wont_support_decrypt &= ~typemask(ep->type);
	    ret = 1;
	}
	if ((mode == 0) || isprefix(mode, "output")) {
	    encrypt_mode = ep->type;
	    i_wont_support_encrypt &= ~typemask(ep->type);
	    ret = 1;
	}
	if (ret == 0)
	    printf("%s: invalid encryption mode\n", mode);
    }
    return(ret);
}

int
EncryptStart(char *mode)
{
    int ret = 0;
    if (mode) {
	if (isprefix(mode, "input"))
	    return(EncryptStartInput());
	if (isprefix(mode, "output"))
	    return(EncryptStartOutput());
	if (isprefix(mode, "help") || isprefix(mode, "?")) {
	    printf("Usage: encrypt start [input|output]\n");
	    return(0);
	}
	printf("%s: invalid encryption mode 'encrypt start ?' for help\n", mode);
	return(0);
    }
    ret += EncryptStartInput();
    ret += EncryptStartOutput();
    return(ret);
}

int
EncryptStartInput(void)
{
    if (decrypt_mode) {
	encrypt_send_request_start();
	return(1);
    }
    printf("No previous decryption mode, decryption not enabled\r\n");
    return(0);
}

int
EncryptStartOutput(void)
{
    if (encrypt_mode) {
	encrypt_start_output(encrypt_mode);
	return(1);
    }
    printf("No previous encryption mode, encryption not enabled\r\n");
    return(0);
}

int
EncryptStop(char *mode)
{
    int ret = 0;
    if (mode) {
	if (isprefix(mode, "input"))
	    return(EncryptStopInput());
	if (isprefix(mode, "output"))
	    return(EncryptStopOutput());
	if (isprefix(mode, "help") || isprefix(mode, "?")) {
	    printf("Usage: encrypt stop [input|output]\n");
	    return(0);
	}
	printf("%s: invalid encryption mode 'encrypt stop ?' for help\n", mode);
	return(0);
    }
    ret += EncryptStopInput();
    ret += EncryptStopOutput();
    return(ret);
}

int
EncryptStopInput(void)
{
    encrypt_send_request_end();
    return(1);
}

int
EncryptStopOutput(void)
{
    encrypt_send_end();
    return(1);
}

void
encrypt_display(void)
{
    printf("Autoencrypt for output is %s. Autodecrypt for input is %s.\r\n",
	   autoencrypt?"on":"off", autodecrypt?"on":"off");

    if (encrypt_output)
	printf("Currently encrypting output with %s\r\n",
	       ENCTYPE_NAME(encrypt_mode));
    else
	printf("Currently not encrypting output\r\n");

    if (decrypt_input)
	printf("Currently decrypting input with %s\r\n",
	       ENCTYPE_NAME(decrypt_mode));
    else
	printf("Currently not decrypting input\r\n");
}

int
EncryptStatus(void)
{
    printf("Autoencrypt for output is %s. Autodecrypt for input is %s.\r\n",
	   autoencrypt?"on":"off", autodecrypt?"on":"off");

    if (encrypt_output)
	printf("Currently encrypting output with %s\r\n",
	       ENCTYPE_NAME(encrypt_mode));
    else if (encrypt_mode) {
	printf("Currently output is clear text.\r\n");
	printf("Last encryption mode was %s\r\n",
	       ENCTYPE_NAME(encrypt_mode));
    } else
	printf("Currently not encrypting output\r\n");

    if (decrypt_input) {
	printf("Currently decrypting input with %s\r\n",
	       ENCTYPE_NAME(decrypt_mode));
    } else if (decrypt_mode) {
	printf("Currently input is clear text.\r\n");
	printf("Last decryption mode was %s\r\n",
	       ENCTYPE_NAME(decrypt_mode));
    } else
	printf("Currently not decrypting input\r\n");

    return 1;
}

void
encrypt_send_support(void)
{
    if (str_suplen) {
	/*
	 * If the user has requested that decryption start
	 * immediatly, then send a "REQUEST START" before
	 * we negotiate the type.
	 */
	if (!Server && autodecrypt)
	    encrypt_send_request_start();
	telnet_net_write(str_send, str_suplen);
	printsub('>', &str_send[2], str_suplen - 2);
	str_suplen = 0;
    }
}

int
EncryptDebug(int on)
{
    if (on < 0)
	encrypt_debug_mode ^= 1;
    else
	encrypt_debug_mode = on;
    printf("Encryption debugging %s\r\n",
	   encrypt_debug_mode ? "enabled" : "disabled");
    return(1);
}

/* turn on verbose encryption, but dont keep telling the whole world
 */
void encrypt_verbose_quiet(int on)
{
    if(on < 0)
	encrypt_verbose ^= 1;
    else
	encrypt_verbose = on ? 1 : 0;
}

int
EncryptVerbose(int on)
{
    encrypt_verbose_quiet(on);
    printf("Encryption %s verbose\r\n",
	   encrypt_verbose ? "is" : "is not");
    return(1);
}

int
EncryptAutoEnc(int on)
{
    encrypt_auto(on);
    printf("Automatic encryption of output is %s\r\n",
	   autoencrypt ? "enabled" : "disabled");
    return(1);
}

int
EncryptAutoDec(int on)
{
    decrypt_auto(on);
    printf("Automatic decryption of input is %s\r\n",
	   autodecrypt ? "enabled" : "disabled");
    return(1);
}

/* Called when we receive a WONT or a DONT ENCRYPT after we sent a DO
   encrypt */
void
encrypt_not(void)
{
    if (encrypt_verbose)
  	printf("[ Connection is NOT encrypted ]\r\n");
    else
  	printf("\r\n*** Connection not encrypted! "
	       "Communication may be eavesdropped. ***\r\n");
}

/*
 * Called when ENCRYPT SUPPORT is received.
 */
void
encrypt_support(unsigned char *typelist, int cnt)
{
    int type, use_type = 0;
    Encryptions *ep;

    /*
     * Forget anything the other side has previously told us.
     */
    remote_supports_decrypt = 0;

    while (cnt-- > 0) {
	type = *typelist++;
	if (encrypt_debug_mode)
	    printf(">>>%s: He is supporting %s (%d)\r\n",
		   Name,
		   ENCTYPE_NAME(type), type);
	if ((type < ENCTYPE_CNT) &&
	    (I_SUPPORT_ENCRYPT & typemask(type))) {
	    remote_supports_decrypt |= typemask(type);
	    if (use_type == 0)
		use_type = type;
	}
    }
    if (use_type) {
	ep = findencryption(use_type);
	if (!ep)
	    return;
	type = ep->start ? (*ep->start)(DIR_ENCRYPT, Server) : 0;
	if (encrypt_debug_mode)
	    printf(">>>%s: (*ep->start)() returned %d\r\n",
		   Name, type);
	if (type < 0)
	    return;
	encrypt_mode = use_type;
	if (type == 0)
	    encrypt_start_output(use_type);
    }
}

void
encrypt_is(unsigned char *data, int cnt)
{
    Encryptions *ep;
    int type, ret;

    if (--cnt < 0)
	return;
    type = *data++;
    if (type < ENCTYPE_CNT)
	remote_supports_encrypt |= typemask(type);
    if (!(ep = finddecryption(type))) {
	if (encrypt_debug_mode)
	    printf(">>>%s: Can't find type %s (%d) for initial negotiation\r\n",
		   Name,
		   ENCTYPE_NAME_OK(type)
		   ? ENCTYPE_NAME(type) : "(unknown)",
		   type);
	return;
    }
    if (!ep->is) {
	if (encrypt_debug_mode)
	    printf(">>>%s: No initial negotiation needed for type %s (%d)\r\n",
		   Name,
		   ENCTYPE_NAME_OK(type)
		   ? ENCTYPE_NAME(type) : "(unknown)",
		   type);
	ret = 0;
    } else {
	ret = (*ep->is)(data, cnt);
	if (encrypt_debug_mode)
	    printf("(*ep->is)(%p, %d) returned %s(%d)\n", data, cnt,
		   (ret < 0) ? "FAIL " :
		   (ret == 0) ? "SUCCESS " : "MORE_TO_DO ", ret);
    }
    if (ret < 0) {
	autodecrypt = 0;
    } else {
	decrypt_mode = type;
	if (ret == 0 && autodecrypt)
	    encrypt_send_request_start();
    }
}

void
encrypt_reply(unsigned char *data, int cnt)
{
    Encryptions *ep;
    int ret, type;

    if (--cnt < 0)
	return;
    type = *data++;
    if (!(ep = findencryption(type))) {
	if (encrypt_debug_mode)
	    printf(">>>%s: Can't find type %s (%d) for initial negotiation\r\n",
		   Name,
		   ENCTYPE_NAME_OK(type)
		   ? ENCTYPE_NAME(type) : "(unknown)",
		   type);
	return;
    }
    if (!ep->reply) {
	if (encrypt_debug_mode)
	    printf(">>>%s: No initial negotiation needed for type %s (%d)\r\n",
		   Name,
		   ENCTYPE_NAME_OK(type)
		   ? ENCTYPE_NAME(type) : "(unknown)",
		   type);
	ret = 0;
    } else {
	ret = (*ep->reply)(data, cnt);
	if (encrypt_debug_mode)
	    printf("(*ep->reply)(%p, %d) returned %s(%d)\n",
		   data, cnt,
		   (ret < 0) ? "FAIL " :
		   (ret == 0) ? "SUCCESS " : "MORE_TO_DO ", ret);
    }
    if (encrypt_debug_mode)
	printf(">>>%s: encrypt_reply returned %d\n", Name, ret);
    if (ret < 0) {
	autoencrypt = 0;
    } else {
	encrypt_mode = type;
	if (ret == 0 && autoencrypt)
	    encrypt_start_output(type);
    }
}

/*
 * Called when ENCRYPT START is received.
 */
void
encrypt_start(unsigned char *data, int cnt)
{
    Encryptions *ep;

    if (!decrypt_mode) {
	/*
	 * Something is wrong.  We should not get a START
	 * command without having already picked our
	 * decryption scheme.  Send a REQUEST-END to
	 * attempt to clear the channel...
	 */
	printf("%s: Warning, Cannot decrypt input stream!!!\r\n", Name);
	encrypt_send_request_end();
	return;
    }

    if ((ep = finddecryption(decrypt_mode))) {
	decrypt_input = ep->input;
	if (encrypt_verbose)
	    printf("[ Input is now decrypted with type %s ]\r\n",
		   ENCTYPE_NAME(decrypt_mode));
	if (encrypt_debug_mode)
	    printf(">>>%s: Start to decrypt input with type %s\r\n",
		   Name, ENCTYPE_NAME(decrypt_mode));
    } else {
	printf("%s: Warning, Cannot decrypt type %s (%d)!!!\r\n",
	       Name,
	       ENCTYPE_NAME_OK(decrypt_mode)
	       ? ENCTYPE_NAME(decrypt_mode)
	       : "(unknown)",
	       decrypt_mode);
	encrypt_send_request_end();
    }
}

void
encrypt_session_key(Session_Key *key, int server)
{
    Encryptions *ep = encryptions;

    havesessionkey = 1;

    while (ep->type) {
	if (ep->session)
	    (*ep->session)(key, server);
	++ep;
    }
}

/*
 * Called when ENCRYPT END is received.
 */
void
encrypt_end(void)
{
    decrypt_input = 0;
    if (encrypt_debug_mode)
	printf(">>>%s: Input is back to clear text\r\n", Name);
    if (encrypt_verbose)
	printf("[ Input is now clear text ]\r\n");
}

/*
 * Called when ENCRYPT REQUEST-END is received.
 */
void
encrypt_request_end(void)
{
    encrypt_send_end();
}

/*
 * Called when ENCRYPT REQUEST-START is received.  If we receive
 * this before a type is picked, then that indicates that the
 * other side wants us to start encrypting data as soon as we
 * can.
 */
void
encrypt_request_start(unsigned char *data, int cnt)
{
    if (encrypt_mode == 0)  {
	if (Server)
	    autoencrypt = 1;
	return;
    }
    encrypt_start_output(encrypt_mode);
}

static unsigned char str_keyid[(MAXKEYLEN*2)+5] = { IAC, SB, TELOPT_ENCRYPT };

static void
encrypt_keyid(struct key_info *kp, unsigned char *keyid, int len)
{
    Encryptions *ep;
    int dir = kp->dir;
    int ret = 0;

    if (len > MAXKEYLEN)
	len = MAXKEYLEN;

    if (!(ep = (*kp->getcrypt)(*kp->modep))) {
	if (len == 0)
	    return;
	kp->keylen = 0;
    } else if (len == 0) {
	/*
	 * Empty option, indicates a failure.
	 */
	if (kp->keylen == 0)
	    return;
	kp->keylen = 0;
	if (ep->keyid)
	    (void)(*ep->keyid)(dir, kp->keyid, &kp->keylen);

    } else if ((len != kp->keylen) || (memcmp(keyid,kp->keyid,len) != 0)) {
	/*
	 * Length or contents are different
	 */
	kp->keylen = len;
	memcpy(kp->keyid,keyid, len);
	if (ep->keyid)
	    (void)(*ep->keyid)(dir, kp->keyid, &kp->keylen);
    } else {
	if (ep->keyid)
	    ret = (*ep->keyid)(dir, kp->keyid, &kp->keylen);
	if ((ret == 0) && (dir == DIR_ENCRYPT) && autoencrypt)
	    encrypt_start_output(*kp->modep);
	return;
    }

    encrypt_send_keyid(dir, kp->keyid, kp->keylen, 0);
}

void encrypt_enc_keyid(unsigned char *keyid, int len)
{
    encrypt_keyid(&ki[1], keyid, len);
}

void encrypt_dec_keyid(unsigned char *keyid, int len)
{
    encrypt_keyid(&ki[0], keyid, len);
}


void encrypt_send_keyid(int dir, unsigned char *keyid, int keylen, int saveit)
{
    unsigned char *strp;

    str_keyid[3] = (dir == DIR_ENCRYPT)
	? ENCRYPT_ENC_KEYID : ENCRYPT_DEC_KEYID;
    if (saveit) {
	struct key_info *kp = &ki[(dir == DIR_ENCRYPT) ? 0 : 1];
	memcpy(kp->keyid,keyid, keylen);
	kp->keylen = keylen;
    }

    for (strp = &str_keyid[4]; keylen > 0; --keylen) {
	if ((*strp++ = *keyid++) == IAC)
	    *strp++ = IAC;
    }
    *strp++ = IAC;
    *strp++ = SE;
    telnet_net_write(str_keyid, strp - str_keyid);
    printsub('>', &str_keyid[2], strp - str_keyid - 2);
}

void
encrypt_auto(int on)
{
    if (on < 0)
	autoencrypt ^= 1;
    else
	autoencrypt = on ? 1 : 0;
}

void
decrypt_auto(int on)
{
    if (on < 0)
	autodecrypt ^= 1;
    else
	autodecrypt = on ? 1 : 0;
}

void
encrypt_start_output(int type)
{
    Encryptions *ep;
    unsigned char *p;
    int i;

    if (!(ep = findencryption(type))) {
	if (encrypt_debug_mode) {
	    printf(">>>%s: Can't encrypt with type %s (%d)\r\n",
		   Name,
		   ENCTYPE_NAME_OK(type)
		   ? ENCTYPE_NAME(type) : "(unknown)",
		   type);
	}
	return;
    }
    if (ep->start) {
	i = (*ep->start)(DIR_ENCRYPT, Server);
	if (encrypt_debug_mode) {
	    printf(">>>%s: Encrypt start: %s (%d) %s\r\n",
		   Name,
		   (i < 0) ? "failed" :
		   "initial negotiation in progress",
		   i, ENCTYPE_NAME(type));
	}
	if (i)
	    return;
    }
    p = str_start + 3;
    *p++ = ENCRYPT_START;
    for (i = 0; i < ki[0].keylen; ++i) {
	if ((*p++ = ki[0].keyid[i]) == IAC)
	    *p++ = IAC;
    }
    *p++ = IAC;
    *p++ = SE;
    telnet_net_write(str_start, p - str_start);
    net_encrypt();
    printsub('>', &str_start[2], p - &str_start[2]);
    /*
     * If we are already encrypting in some mode, then
     * encrypt the ring (which includes our request) in
     * the old mode, mark it all as "clear text" and then
     * switch to the new mode.
     */
    encrypt_output = ep->output;
    encrypt_mode = type;
    if (encrypt_debug_mode)
	printf(">>>%s: Started to encrypt output with type %s\r\n",
	       Name, ENCTYPE_NAME(type));
    if (encrypt_verbose)
	printf("[ Output is now encrypted with type %s ]\r\n",
	       ENCTYPE_NAME(type));
}

void
encrypt_send_end(void)
{
    if (!encrypt_output)
	return;

    str_end[3] = ENCRYPT_END;
    telnet_net_write(str_end, sizeof(str_end));
    net_encrypt();
    printsub('>', &str_end[2], sizeof(str_end) - 2);
    /*
     * Encrypt the output buffer now because it will not be done by
     * netflush...
     */
    encrypt_output = 0;
    if (encrypt_debug_mode)
	printf(">>>%s: Output is back to clear text\r\n", Name);
    if (encrypt_verbose)
	printf("[ Output is now clear text ]\r\n");
}

void
encrypt_send_request_start(void)
{
    unsigned char *p;
    int i;

    p = &str_start[3];
    *p++ = ENCRYPT_REQSTART;
    for (i = 0; i < ki[1].keylen; ++i) {
	if ((*p++ = ki[1].keyid[i]) == IAC)
	    *p++ = IAC;
    }
    *p++ = IAC;
    *p++ = SE;
    telnet_net_write(str_start, p - str_start);
    printsub('>', &str_start[2], p - &str_start[2]);
    if (encrypt_debug_mode)
	printf(">>>%s: Request input to be encrypted\r\n", Name);
}

void
encrypt_send_request_end(void)
{
    str_end[3] = ENCRYPT_REQEND;
    telnet_net_write(str_end, sizeof(str_end));
    printsub('>', &str_end[2], sizeof(str_end) - 2);

    if (encrypt_debug_mode)
	printf(">>>%s: Request input to be clear text\r\n", Name);
}


void encrypt_wait(void)
{
    if (encrypt_debug_mode)
	printf(">>>%s: in encrypt_wait\r\n", Name);
    if (!havesessionkey || !(I_SUPPORT_ENCRYPT & remote_supports_decrypt))
	return;
    while (autoencrypt && !encrypt_output)
	if (telnet_spin())
	    return;
}

int
encrypt_delay(void)
{
    if(!havesessionkey ||
       (I_SUPPORT_ENCRYPT & remote_supports_decrypt) == 0 ||
       (I_SUPPORT_DECRYPT & remote_supports_encrypt) == 0)
	return 0;
    if(!(encrypt_output && decrypt_input))
	return 1;
    return 0;
}

int encrypt_is_encrypting()
{
    if (encrypt_output && decrypt_input)
	return 1;
    return 0;
}

void
encrypt_debug(int mode)
{
    encrypt_debug_mode = mode;
}

void encrypt_gen_printsub(unsigned char *data, size_t cnt,
			  unsigned char *buf, size_t buflen)
{
    char tbuf[16], *cp;

    cnt -= 2;
    data += 2;
    buf[buflen-1] = '\0';
    buf[buflen-2] = '*';
    buflen -= 2;;
    for (; cnt > 0; cnt--, data++) {
	snprintf(tbuf, sizeof(tbuf), " %d", *data);
	for (cp = tbuf; *cp && buflen > 0; --buflen)
	    *buf++ = *cp++;
	if (buflen <= 0)
	    return;
    }
    *buf = '\0';
}

void
encrypt_printsub(unsigned char *data, size_t cnt,
		 unsigned char *buf, size_t buflen)
{
    Encryptions *ep;
    int type = data[1];

    for (ep = encryptions; ep->type && ep->type != type; ep++)
	;

    if (ep->printsub)
	(*ep->printsub)(data, cnt, buf, buflen);
    else
	encrypt_gen_printsub(data, cnt, buf, buflen);
}
#endif
