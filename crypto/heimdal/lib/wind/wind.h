/*
 * Copyright (c) 2004 Kungliga Tekniska Högskolan
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

/* $Id$ */

#ifndef _WIND_H_
#define _WIND_H_

#include <stddef.h>
#include <krb5-types.h>

#include <wind_err.h>

typedef unsigned int wind_profile_flags;

#define WIND_PROFILE_NAME 			0x00000001
#define WIND_PROFILE_SASL 			0x00000002
#define WIND_PROFILE_LDAP 			0x00000004
#define WIND_PROFILE_LDAP_CASE			0x00000008

#define WIND_PROFILE_LDAP_CASE_EXACT_ATTRIBUTE	0x00010000
#define WIND_PROFILE_LDAP_CASE_EXACT_ASSERTION	0x00020000
#define WIND_PROFILE_LDAP_NUMERIC		0x00040000
#define WIND_PROFILE_LDAP_TELEPHONE		0x00080000


/* flags to wind_ucs2read/wind_ucs2write */
#define WIND_RW_LE	1
#define WIND_RW_BE	2
#define WIND_RW_BOM	4

int wind_stringprep(const uint32_t *, size_t,
		    uint32_t *, size_t *,
		    wind_profile_flags);
int wind_profile(const char *, wind_profile_flags *);

int wind_punycode_label_toascii(const uint32_t *, size_t,
				char *, size_t *);

int wind_utf8ucs4(const char *, uint32_t *, size_t *);
int wind_utf8ucs4_length(const char *, size_t *);

int wind_ucs4utf8(const uint32_t *, size_t, char *, size_t *);
int wind_ucs4utf8_length(const uint32_t *, size_t, size_t *);

int wind_utf8ucs2(const char *, uint16_t *, size_t *);
int wind_utf8ucs2_length(const char *, size_t *);

int wind_ucs2utf8(const uint16_t *, size_t, char *, size_t *);
int wind_ucs2utf8_length(const uint16_t *, size_t, size_t *);


int wind_ucs2read(const void *, size_t, unsigned int *, uint16_t *, size_t *);
int wind_ucs2write(const uint16_t *, size_t, unsigned int *, void *, size_t *);

#endif /* _WIND_H_ */
