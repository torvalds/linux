/*
 * Copyright (c) 1997 - 2006 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

/* asn1 templates */

#ifndef __TEMPLATE_H__
#define __TEMPLATE_H__

/* tag:
 *  0..20 tag
 * 21     type
 * 22..23 class
 * 24..27 flags
 * 28..31 op
 */

/* parse:
 *  0..11 type
 * 12..23 unused
 * 24..27 flags
 * 28..31 op
 */

#define A1_OP_MASK		(0xf0000000)
#define A1_OP_TYPE		(0x10000000)
#define A1_OP_TYPE_EXTERN	(0x20000000)
#define A1_OP_TAG		(0x30000000)
#define A1_OP_PARSE		(0x40000000)
#define A1_OP_SEQOF		(0x50000000)
#define A1_OP_SETOF		(0x60000000)
#define A1_OP_BMEMBER		(0x70000000)
#define A1_OP_CHOICE		(0x80000000)

#define A1_FLAG_MASK		(0x0f000000)
#define A1_FLAG_OPTIONAL	(0x01000000)
#define A1_FLAG_IMPLICIT	(0x02000000)

#define A1_TAG_T(CLASS,TYPE,TAG)	((A1_OP_TAG) | (((CLASS) << 22) | ((TYPE) << 21) | (TAG)))
#define A1_TAG_CLASS(x)		(((x) >> 22) & 0x3)
#define A1_TAG_TYPE(x)		(((x) >> 21) & 0x1)
#define A1_TAG_TAG(x)		((x) & 0x1fffff)

#define A1_TAG_LEN(t)		((uintptr_t)(t)->ptr)
#define A1_HEADER_LEN(t)	((uintptr_t)(t)->ptr)

#define A1_PARSE_T(type)	((A1_OP_PARSE) | (type))
#define A1_PARSE_TYPE_MASK	0xfff
#define A1_PARSE_TYPE(x)	(A1_PARSE_TYPE_MASK & (x))

#define A1_PF_INDEFINTE		0x1
#define A1_PF_ALLOW_BER		0x2

#define A1_HF_PRESERVE		0x1
#define A1_HF_ELLIPSIS		0x2

#define A1_HBF_RFC1510		0x1


struct asn1_template {
    uint32_t tt;
    size_t offset;
    const void *ptr;
};

typedef int (*asn1_type_decode)(const unsigned char *, size_t, void *, size_t *);
typedef int (*asn1_type_encode)(unsigned char *, size_t, const void *, size_t *);
typedef size_t (*asn1_type_length)(const void *);
typedef void (*asn1_type_release)(void *);
typedef int (*asn1_type_copy)(const void *, void *);

struct asn1_type_func {
    asn1_type_encode encode;
    asn1_type_decode decode;
    asn1_type_length length;
    asn1_type_copy copy;
    asn1_type_release release;
    size_t size;
};

struct template_of {
    unsigned int len;
    void *val;
};

enum template_types {
    A1T_IMEMBER = 0,
    A1T_HEIM_INTEGER,
    A1T_INTEGER,
    A1T_UNSIGNED,
    A1T_GENERAL_STRING,
    A1T_OCTET_STRING,
    A1T_OCTET_STRING_BER,
    A1T_IA5_STRING,
    A1T_BMP_STRING,
    A1T_UNIVERSAL_STRING,
    A1T_PRINTABLE_STRING,
    A1T_VISIBLE_STRING,
    A1T_UTF8_STRING,
    A1T_GENERALIZED_TIME,
    A1T_UTC_TIME,
    A1T_HEIM_BIT_STRING,
    A1T_BOOLEAN,
    A1T_OID,
    A1T_TELETEX_STRING,
    A1T_NULL
};


#endif
