/*-
 * Copyright (c) 1991, 1993
 *      Dave Safford.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

/* public key routines */
/* functions:
	genkeys(char *public, char *secret)
	common_key(char *secret, char *public, desData *deskey)
        pk_encode(char *in, *out, DesData *deskey);
        pk_decode(char *in, *out, DesData *deskey);
      where
	char public[HEXKEYBYTES + 1];
	char secret[HEXKEYBYTES + 1];
 */

#include <sys/time.h>
#include <openssl/des.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mp.h"
#include "pk.h"
 
static void adjust(char keyout[HEXKEYBYTES+1], char *keyin);

/*
 * Choose top 128 bits of the common key to use as our idea key.
 */
static void
extractideakey(MINT *ck, IdeaData *ideakey)
{
        MINT *a;
        MINT *z;
        short r;
        int i;
        short base = (1 << 8);
        char *k;

        z = mp_itom(0);
        a = mp_itom(0);
        mp_madd(ck, z, a);
        for (i = 0; i < ((KEYSIZE - 128) / 8); i++) {
                mp_sdiv(a, base, a, &r);
        }
        k = (char *)ideakey;
        for (i = 0; i < 16; i++) {
                mp_sdiv(a, base, a, &r);
                *k++ = r;
        }
	mp_mfree(z);
        mp_mfree(a);
}

/*
 * Choose middle 64 bits of the common key to use as our des key, possibly
 * overwriting the lower order bits by setting parity. 
 */
static void
extractdeskey(MINT *ck, DesData *deskey)
{
        MINT *a;
        MINT *z;
        short r;
        int i;
        short base = (1 << 8);
        char *k;

        z = mp_itom(0);
        a = mp_itom(0);
        mp_madd(ck, z, a);
        for (i = 0; i < ((KEYSIZE - 64) / 2) / 8; i++) {
                mp_sdiv(a, base, a, &r);
        }
        k = (char *)deskey;
        for (i = 0; i < 8; i++) {
                mp_sdiv(a, base, a, &r);
                *k++ = r;
        }
	mp_mfree(z);
        mp_mfree(a);
}

/*
 * get common key from my secret key and his public key
 */
void
common_key(char *xsecret, char *xpublic, IdeaData *ideakey, DesData *deskey)
{
        MINT *public;
        MINT *secret;
        MINT *common;
	MINT *modulus = mp_xtom(HEXMODULUS);

        public = mp_xtom(xpublic);
        secret = mp_xtom(xsecret);
        common = mp_itom(0);
        mp_pow(public, secret, modulus, common);
        extractdeskey(common, deskey);
        extractideakey(common, ideakey);
	DES_set_odd_parity(deskey);
        mp_mfree(common);
        mp_mfree(secret);
        mp_mfree(public);
	mp_mfree(modulus);
}

/*
 * Generate a seed
 */
static void
getseed(char *seed, int seedsize)
{
	int i;

	srandomdev();
	for (i = 0; i < seedsize; i++) {
		seed[i] = random() & 0xff;
	}
}

/*
 * Generate a random public/secret key pair
 */
void
genkeys(char *public, char *secret)
{
        size_t i;
 
#       define BASEBITS (8*sizeof(short) - 1)
#       define BASE (1 << BASEBITS)
 
        MINT *pk = mp_itom(0);
        MINT *sk = mp_itom(0);
        MINT *tmp;
        MINT *base = mp_itom((short)BASE);
        MINT *root = mp_itom(PROOT);
        MINT *modulus = mp_xtom(HEXMODULUS);
        short r;
        unsigned short seed[KEYSIZE/BASEBITS + 1];
        char *xkey;

        getseed((char *)seed, sizeof(seed));    
        for (i = 0; i < KEYSIZE/BASEBITS + 1; i++) {
                r = seed[i] % BASE;
                tmp = mp_itom(r);
                mp_mult(sk, base, sk);
                mp_madd(sk, tmp, sk);
                mp_mfree(tmp);  
        }
        tmp = mp_itom(0);
        mp_mdiv(sk, modulus, tmp, sk);
        mp_mfree(tmp);
        mp_pow(root, sk, modulus, pk); 
        xkey = mp_mtox(sk);   
        adjust(secret, xkey);
        xkey = mp_mtox(pk);
        adjust(public, xkey);
        mp_mfree(sk);
        mp_mfree(base);
        mp_mfree(pk);
        mp_mfree(root);
        mp_mfree(modulus);
} 

/*
 * Adjust the input key so that it is 0-filled on the left
 */
static void
adjust(char keyout[HEXKEYBYTES+1], char *keyin)
{
        char *p;
        char *s;

        for (p = keyin; *p; p++) 
                ;
        for (s = keyout + HEXKEYBYTES; p >= keyin; p--, s--) {
                *s = *p;
        }
        while (s >= keyout) {
                *s-- = '0';
        }
}

static char hextab[17] = "0123456789ABCDEF";

/* given a DES key, cbc encrypt and translate input to terminated hex */
void
pk_encode(char *in, char *out, DesData *key)
{
	char buf[256];
	DesData i;
	DES_key_schedule k;
	int l,op,deslen;

	memset(&i,0,sizeof(i));
	memset(buf,0,sizeof(buf));
	deslen = ((strlen(in) + 7)/8)*8;
	DES_key_sched(key, &k);
	DES_cbc_encrypt(in, buf, deslen, &k, &i, DES_ENCRYPT);
	for (l=0,op=0;l<deslen;l++) {
		out[op++] = hextab[(buf[l] & 0xf0) >> 4];
		out[op++] = hextab[(buf[l] & 0x0f)];
	}
	out[op] = '\0';
}

/* given a DES key, translate input from hex and decrypt */
void
pk_decode(char *in, char *out, DesData *key)
{
	char buf[256];
	DesData i;
	DES_key_schedule k;
	int n1,n2,op;
	size_t l;

	memset(&i,0,sizeof(i));
	memset(buf,0,sizeof(buf));
	for (l=0,op=0;l<strlen(in)/2;l++,op+=2) {
		if (in[op] > '9')
			n1 = in[op] - 'A' + 10;
		else
			n1 = in[op] - '0';
		if (in[op+1] > '9')
			n2 = in[op+1] - 'A' + 10;
		else
			n2 = in[op+1] - '0';
		buf[l] = n1*16 +n2;
	}
	DES_key_sched(key, &k);
	DES_cbc_encrypt(buf, out, strlen(in) / 2, &k, &i, DES_DECRYPT);
	out[strlen(in)/2] = '\0';
}
