/*
 * Copyright (c) 1997 - 2006 Kungliga Tekniska Högskolan
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

#ifndef __DER_H__
#define __DER_H__

typedef enum {
    ASN1_C_UNIV = 0,
    ASN1_C_APPL = 1,
    ASN1_C_CONTEXT = 2,
    ASN1_C_PRIVATE = 3
} Der_class;

typedef enum {PRIM = 0, CONS = 1} Der_type;

#define MAKE_TAG(CLASS, TYPE, TAG)  (((CLASS) << 6) | ((TYPE) << 5) | (TAG))

/* Universal tags */

enum {
    UT_EndOfContent	= 0,
    UT_Boolean		= 1,
    UT_Integer		= 2,
    UT_BitString	= 3,
    UT_OctetString	= 4,
    UT_Null		= 5,
    UT_OID		= 6,
    UT_Enumerated	= 10,
    UT_UTF8String	= 12,
    UT_Sequence		= 16,
    UT_Set		= 17,
    UT_PrintableString	= 19,
    UT_IA5String	= 22,
    UT_UTCTime		= 23,
    UT_GeneralizedTime	= 24,
    UT_UniversalString	= 25,
    UT_VisibleString	= 26,
    UT_GeneralString	= 27,
    UT_BMPString	= 30,
    /* unsupported types */
    UT_ObjectDescriptor = 7,
    UT_External		= 8,
    UT_Real		= 9,
    UT_EmbeddedPDV	= 11,
    UT_RelativeOID	= 13,
    UT_NumericString	= 18,
    UT_TeletexString	= 20,
    UT_VideotexString	= 21,
    UT_GraphicString	= 25
};

#define ASN1_INDEFINITE 0xdce0deed

typedef struct heim_der_time_t {
    time_t dt_sec;
    unsigned long dt_nsec;
} heim_der_time_t;

typedef struct heim_ber_time_t {
    time_t bt_sec;
    unsigned bt_nsec;
    int bt_zone;
} heim_ber_time_t;

struct asn1_template;

#include <der-protos.h>

int _heim_fix_dce(size_t reallen, size_t *len);
int _heim_der_set_sort(const void *, const void *);
int _heim_time2generalizedtime (time_t, heim_octet_string *, int);

#endif /* __DER_H__ */
