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
 *	@(#)enc-proto.h	8.1 (Berkeley) 6/4/93
 *
 *	@(#)enc-proto.h	5.2 (Berkeley) 3/22/91
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

/* $Id$ */

#if	defined(ENCRYPTION)
Encryptions *findencryption (int);
Encryptions *finddecryption(int);
int EncryptAutoDec(int);
int EncryptAutoEnc(int);
int EncryptDebug(int);
int EncryptDisable(char*, char*);
int EncryptEnable(char*, char*);
int EncryptStart(char*);
int EncryptStartInput(void);
int EncryptStartOutput(void);
int EncryptStatus(void);
int EncryptStop(char*);
int EncryptStopInput(void);
int EncryptStopOutput(void);
int EncryptType(char*, char*);
int EncryptVerbose(int);
void decrypt_auto(int);
void encrypt_auto(int);
void encrypt_debug(int);
void encrypt_dec_keyid(unsigned char*, int);
void encrypt_display(void);
void encrypt_enc_keyid(unsigned char*, int);
void encrypt_end(void);
void encrypt_gen_printsub(unsigned char*, size_t, unsigned char*, size_t);
void encrypt_init(const char*, int);
void encrypt_is(unsigned char*, int);
void encrypt_list_types(void);
void encrypt_not(void);
void encrypt_printsub(unsigned char*, size_t, unsigned char*, size_t);
void encrypt_reply(unsigned char*, int);
void encrypt_request_end(void);
void encrypt_request_start(unsigned char*, int);
void encrypt_send_end(void);
void encrypt_send_keyid(int, unsigned char*, int, int);
void encrypt_send_request_end(void);
int encrypt_is_encrypting(void);
void encrypt_send_request_start(void);
void encrypt_send_support(void);
void encrypt_session_key(Session_Key*, int);
void encrypt_start(unsigned char*, int);
void encrypt_start_output(int);
void encrypt_support(unsigned char*, int);
void encrypt_verbose_quiet(int);
void encrypt_wait(void);
int encrypt_delay(void);

#ifdef	TELENTD
void encrypt_wait (void);
#else
void encrypt_display (void);
#endif

void cfb64_encrypt (unsigned char *, int);
int cfb64_decrypt (int);
void cfb64_init (int);
int cfb64_start (int, int);
int cfb64_is (unsigned char *, int);
int cfb64_reply (unsigned char *, int);
void cfb64_session (Session_Key *, int);
int cfb64_keyid (int, unsigned char *, int *);
void cfb64_printsub (unsigned char *, size_t, unsigned char *, size_t);

void ofb64_encrypt (unsigned char *, int);
int ofb64_decrypt (int);
void ofb64_init (int);
int ofb64_start (int, int);
int ofb64_is (unsigned char *, int);
int ofb64_reply (unsigned char *, int);
void ofb64_session (Session_Key *, int);
int ofb64_keyid (int, unsigned char *, int *);
void ofb64_printsub (unsigned char *, size_t, unsigned char *, size_t);

#endif
