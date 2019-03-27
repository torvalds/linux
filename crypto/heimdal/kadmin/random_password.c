/*
 * Copyright (c) 1998, 1999 Kungliga Tekniska Högskolan
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

/* This file defines some a function that generates a random password,
   that can be used when creating a large amount of principals (such
   as for a batch of students). Since this is a political matter, you
   should think about how secure generated passwords has to be.

   Both methods defined here will give you at least 55 bits of
   entropy.
   */

/* If you want OTP-style passwords, define OTP_STYLE */

#ifdef OTP_STYLE
#include <otp.h>
#else
static void generate_password(char **pw, int num_classes, ...);
#endif

void
random_password(char *pw, size_t len)
{
#ifdef OTP_STYLE
    {
	OtpKey newkey;

	krb5_generate_random_block(&newkey, sizeof(newkey));
	otp_print_stddict (newkey, pw, len);
	strlwr(pw);
    }
#else
    char *pass;
    generate_password(&pass, 3,
		      "abcdefghijklmnopqrstuvwxyz", 7,
		      "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 2,
		      "@$%&*()-+=:,/<>1234567890", 1);
    strlcpy(pw, pass, len);
    memset(pass, 0, strlen(pass));
    free(pass);
#endif
}

/* some helper functions */

#ifndef OTP_STYLE
/* return a random value in range 0-127 */
static int
RND(unsigned char *key, int keylen, int *left)
{
    if(*left == 0){
	krb5_generate_random_block(key, keylen);
	*left = keylen;
    }
    (*left)--;
    return ((unsigned char*)key)[*left];
}

/* This a helper function that generates a random password with a
   number of characters from a set of character classes.

   If there are n classes, and the size of each class is Pi, and the
   number of characters from each class is Ni, the number of possible
   passwords are (given that the character classes are disjoint):

     n             n
   -----        /  ----  \
   |   |  Ni    |  \     |
   |   | Pi     |   \  Ni| !
   |   | ---- * |   /    |
   |   | Ni!    |  /___  |
    i=1          \  i=1  /

    Since it uses the RND function above, neither the size of each
    class, nor the total length of the generated password should be
    larger than 127 (without fixing RND).

   */
static void
generate_password(char **pw, int num_classes, ...)
{
    struct {
	const char *str;
	int len;
	int freq;
    } *classes;
    va_list ap;
    int len, i;
    unsigned char rbuf[8]; /* random buffer */
    int rleft = 0;

    *pw = NULL;

    classes = malloc(num_classes * sizeof(*classes));
    if(classes == NULL)
	return;
    va_start(ap, num_classes);
    len = 0;
    for(i = 0; i < num_classes; i++){
	classes[i].str = va_arg(ap, const char*);
	classes[i].len = strlen(classes[i].str);
	classes[i].freq = va_arg(ap, int);
	len += classes[i].freq;
    }
    va_end(ap);
    *pw = malloc(len + 1);
    if(*pw == NULL) {
	free(classes);
	return;
    }
    for(i = 0; i < len; i++) {
	int j;
	int x = RND(rbuf, sizeof(rbuf), &rleft) % (len - i);
	int t = 0;
	for(j = 0; j < num_classes; j++) {
	    if(x < t + classes[j].freq) {
		(*pw)[i] = classes[j].str[RND(rbuf, sizeof(rbuf), &rleft)
					 % classes[j].len];
		classes[j].freq--;
		break;
	    }
	    t += classes[j].freq;
	}
    }
    (*pw)[len] = '\0';
    memset(rbuf, 0, sizeof(rbuf));
    free(classes);
}
#endif
