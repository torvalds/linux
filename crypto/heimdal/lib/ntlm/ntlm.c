/*
 * Copyright (c) 2006 - 2008 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2010 Apple Inc. All rights reserved.
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include <roken.h>
#include <parse_units.h>
#include <krb5.h>

#define HC_DEPRECATED_CRYPTO

#include "krb5-types.h"
#include "crypto-headers.h"

#include <heimntlm.h>

/*! \mainpage Heimdal NTLM library
 *
 * \section intro Introduction
 *
 * Heimdal libheimntlm library is a implementation of the NTLM
 * protocol, both version 1 and 2. The GSS-API mech that uses this
 * library adds support for transport encryption and integrity
 * checking.
 *
 * NTLM is a protocol for mutual authentication, its still used in
 * many protocol where Kerberos is not support, one example is
 * EAP/X802.1x mechanism LEAP from Microsoft and Cisco.
 *
 * This is a support library for the core protocol, its used in
 * Heimdal to implement and GSS-API mechanism. There is also support
 * in the KDC to do remote digest authenticiation, this to allow
 * services to authenticate users w/o direct access to the users ntlm
 * hashes (same as Kerberos arcfour enctype keys).
 *
 * More information about the NTLM protocol can found here
 * http://davenport.sourceforge.net/ntlm.html .
 *
 * The Heimdal projects web page: http://www.h5l.org/
 *
 * @section ntlm_example NTLM Example
 *
 * Example to to use @ref test_ntlm.c .
 *
 * @example test_ntlm.c
 *
 * Example how to use the NTLM primitives.
 *
 */

/** @defgroup ntlm_core Heimdal NTLM library
 *
 * The NTLM core functions implement the string2key generation
 * function, message encode and decode function, and the hash function
 * functions.
 */

struct sec_buffer {
    uint16_t length;
    uint16_t allocated;
    uint32_t offset;
};

static const unsigned char ntlmsigature[8] = "NTLMSSP\x00";

/*
 *
 */

#define CHECK(f, e)							\
    do {								\
	ret = f;							\
	if (ret != (ssize_t)(e)) {					\
	    ret = HNTLM_ERR_DECODE;					\
	    goto out;							\
	}								\
    } while(/*CONSTCOND*/0)

static struct units ntlm_flag_units[] = {
#define ntlm_flag(x) { #x, NTLM_##x }
    ntlm_flag(ENC_56),
    ntlm_flag(NEG_KEYEX),
    ntlm_flag(ENC_128),
    ntlm_flag(MBZ1),
    ntlm_flag(MBZ2),
    ntlm_flag(MBZ3),
    ntlm_flag(NEG_VERSION),
    ntlm_flag(MBZ4),
    ntlm_flag(NEG_TARGET_INFO),
    ntlm_flag(NON_NT_SESSION_KEY),
    ntlm_flag(MBZ5),
    ntlm_flag(NEG_IDENTIFY),
    ntlm_flag(NEG_NTLM2),
    ntlm_flag(TARGET_SHARE),
    ntlm_flag(TARGET_SERVER),
    ntlm_flag(TARGET_DOMAIN),
    ntlm_flag(NEG_ALWAYS_SIGN),
    ntlm_flag(MBZ6),
    ntlm_flag(OEM_SUPPLIED_WORKSTATION),
    ntlm_flag(OEM_SUPPLIED_DOMAIN),
    ntlm_flag(NEG_ANONYMOUS),
    ntlm_flag(NEG_NT_ONLY),
    ntlm_flag(NEG_NTLM),
    ntlm_flag(MBZ8),
    ntlm_flag(NEG_LM_KEY),
    ntlm_flag(NEG_DATAGRAM),
    ntlm_flag(NEG_SEAL),
    ntlm_flag(NEG_SIGN),
    ntlm_flag(MBZ9),
    ntlm_flag(NEG_TARGET),
    ntlm_flag(NEG_OEM),
    ntlm_flag(NEG_UNICODE),
#undef ntlm_flag
    {NULL, 0}
};

size_t
heim_ntlm_unparse_flags(uint32_t flags, char *s, size_t len)
{
    return unparse_flags(flags, ntlm_flag_units, s, len);
}


/**
 * heim_ntlm_free_buf frees the ntlm buffer
 *
 * @param p buffer to be freed
 *
 * @ingroup ntlm_core
 */

void
heim_ntlm_free_buf(struct ntlm_buf *p)
{
    if (p->data)
	free(p->data);
    p->data = NULL;
    p->length = 0;
}


static int
ascii2ucs2le(const char *string, int up, struct ntlm_buf *buf)
{
    unsigned char *p;
    size_t len, i;

    len = strlen(string);
    if (len / 2 > UINT_MAX)
	return ERANGE;

    buf->length = len * 2;
    buf->data = malloc(buf->length);
    if (buf->data == NULL && len != 0) {
	heim_ntlm_free_buf(buf);
	return ENOMEM;
    }

    p = buf->data;
    for (i = 0; i < len; i++) {
	unsigned char t = (unsigned char)string[i];
	if (t & 0x80) {
	    heim_ntlm_free_buf(buf);
	    return EINVAL;
	}
	if (up)
	    t = toupper(t);
	p[(i * 2) + 0] = t;
	p[(i * 2) + 1] = 0;
    }
    return 0;
}

/*
 *
 */

static krb5_error_code
ret_sec_buffer(krb5_storage *sp, struct sec_buffer *buf)
{
    krb5_error_code ret;
    CHECK(krb5_ret_uint16(sp, &buf->length), 0);
    CHECK(krb5_ret_uint16(sp, &buf->allocated), 0);
    CHECK(krb5_ret_uint32(sp, &buf->offset), 0);
out:
    return ret;
}

static krb5_error_code
store_sec_buffer(krb5_storage *sp, const struct sec_buffer *buf)
{
    krb5_error_code ret;
    CHECK(krb5_store_uint16(sp, buf->length), 0);
    CHECK(krb5_store_uint16(sp, buf->allocated), 0);
    CHECK(krb5_store_uint32(sp, buf->offset), 0);
out:
    return ret;
}

/*
 * Strings are either OEM or UNICODE. The later is encoded as ucs2 on
 * wire, but using utf8 in memory.
 */

static krb5_error_code
len_string(int ucs2, const char *s)
{
    size_t len = strlen(s);
    if (ucs2)
	len *= 2;
    return len;
}

/*
 *
 */

static krb5_error_code
ret_string(krb5_storage *sp, int ucs2, size_t len, char **s)
{
    krb5_error_code ret;

    *s = malloc(len + 1);
    if (*s == NULL)
	return ENOMEM;
    CHECK(krb5_storage_read(sp, *s, len), len);

    (*s)[len] = '\0';

    if (ucs2) {
	size_t i;
	for (i = 0; i < len / 2; i++) {
	    (*s)[i] = (*s)[i * 2];
	    if ((*s)[i * 2 + 1]) {
		free(*s);
		*s = NULL;
		return EINVAL;
	    }
	}
	(*s)[i] = '\0';
    }
    ret = 0;
 out:
    return ret;
}



static krb5_error_code
ret_sec_string(krb5_storage *sp, int ucs2, struct sec_buffer *desc, char **s)
{
    krb5_error_code ret = 0;
    CHECK(krb5_storage_seek(sp, desc->offset, SEEK_SET), desc->offset);
    CHECK(ret_string(sp, ucs2, desc->length, s), 0);
 out:
    return ret;
}

static krb5_error_code
put_string(krb5_storage *sp, int ucs2, const char *s)
{
    krb5_error_code ret;
    struct ntlm_buf buf;

    if (ucs2) {
	ret = ascii2ucs2le(s, 0, &buf);
	if (ret)
	    return ret;
    } else {
	buf.data = rk_UNCONST(s);
	buf.length = strlen(s);
    }

    CHECK(krb5_storage_write(sp, buf.data, buf.length), buf.length);
    if (ucs2)
	heim_ntlm_free_buf(&buf);
    ret = 0;
out:
    return ret;
}

/*
 *
 */

static krb5_error_code
ret_buf(krb5_storage *sp, struct sec_buffer *desc, struct ntlm_buf *buf)
{
    krb5_error_code ret;

    buf->data = malloc(desc->length);
    buf->length = desc->length;
    CHECK(krb5_storage_seek(sp, desc->offset, SEEK_SET), desc->offset);
    CHECK(krb5_storage_read(sp, buf->data, buf->length), buf->length);
    ret = 0;
out:
    return ret;
}

static krb5_error_code
put_buf(krb5_storage *sp, const struct ntlm_buf *buf)
{
    krb5_error_code ret;
    CHECK(krb5_storage_write(sp, buf->data, buf->length), buf->length);
    ret = 0;
out:
    return ret;
}

/**
 * Frees the ntlm_targetinfo message
 *
 * @param ti targetinfo to be freed
 *
 * @ingroup ntlm_core
 */

void
heim_ntlm_free_targetinfo(struct ntlm_targetinfo *ti)
{
    free(ti->servername);
    free(ti->domainname);
    free(ti->dnsdomainname);
    free(ti->dnsservername);
    free(ti->dnstreename);
    memset(ti, 0, sizeof(*ti));
}

static int
encode_ti_string(krb5_storage *out, uint16_t type, int ucs2, char *s)
{
    krb5_error_code ret;
    CHECK(krb5_store_uint16(out, type), 0);
    CHECK(krb5_store_uint16(out, len_string(ucs2, s)), 0);
    CHECK(put_string(out, ucs2, s), 0);
out:
    return ret;
}

/**
 * Encodes a ntlm_targetinfo message.
 *
 * @param ti the ntlm_targetinfo message to encode.
 * @param ucs2 ignored
 * @param data is the return buffer with the encoded message, should be
 * freed with heim_ntlm_free_buf().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_encode_targetinfo(const struct ntlm_targetinfo *ti,
			    int ucs2,
			    struct ntlm_buf *data)
{
    krb5_error_code ret;
    krb5_storage *out;

    data->data = NULL;
    data->length = 0;

    out = krb5_storage_emem();
    if (out == NULL)
	return ENOMEM;

    krb5_storage_set_byteorder(out, KRB5_STORAGE_BYTEORDER_LE);

    if (ti->servername)
	CHECK(encode_ti_string(out, 1, ucs2, ti->servername), 0);
    if (ti->domainname)
	CHECK(encode_ti_string(out, 2, ucs2, ti->domainname), 0);
    if (ti->dnsservername)
	CHECK(encode_ti_string(out, 3, ucs2, ti->dnsservername), 0);
    if (ti->dnsdomainname)
	CHECK(encode_ti_string(out, 4, ucs2, ti->dnsdomainname), 0);
    if (ti->dnstreename)
	CHECK(encode_ti_string(out, 5, ucs2, ti->dnstreename), 0);
    if (ti->avflags) {
	CHECK(krb5_store_uint16(out, 6), 0);
	CHECK(krb5_store_uint16(out, 4), 0);
	CHECK(krb5_store_uint32(out, ti->avflags), 0);
    }

    /* end tag */
    CHECK(krb5_store_int16(out, 0), 0);
    CHECK(krb5_store_int16(out, 0), 0);

    {
	krb5_data d;
	ret = krb5_storage_to_data(out, &d);
	data->data = d.data;
	data->length = d.length;
    }
out:
    krb5_storage_free(out);
    return ret;
}

/**
 * Decodes an NTLM targetinfo message
 *
 * @param data input data buffer with the encode NTLM targetinfo message
 * @param ucs2 if the strings should be encoded with ucs2 (selected by flag in message).
 * @param ti the decoded target info, should be freed with heim_ntlm_free_targetinfo().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_decode_targetinfo(const struct ntlm_buf *data,
			    int ucs2,
			    struct ntlm_targetinfo *ti)
{
    uint16_t type, len;
    krb5_storage *in;
    int ret = 0, done = 0;

    memset(ti, 0, sizeof(*ti));

    if (data->length == 0)
	return 0;

    in = krb5_storage_from_readonly_mem(data->data, data->length);
    if (in == NULL)
	return ENOMEM;
    krb5_storage_set_byteorder(in, KRB5_STORAGE_BYTEORDER_LE);

    while (!done) {
	CHECK(krb5_ret_uint16(in, &type), 0);
	CHECK(krb5_ret_uint16(in, &len), 0);

	switch (type) {
	case 0:
	    done = 1;
	    break;
	case 1:
	    CHECK(ret_string(in, ucs2, len, &ti->servername), 0);
	    break;
	case 2:
	    CHECK(ret_string(in, ucs2, len, &ti->domainname), 0);
	    break;
	case 3:
	    CHECK(ret_string(in, ucs2, len, &ti->dnsservername), 0);
	    break;
	case 4:
	    CHECK(ret_string(in, ucs2, len, &ti->dnsdomainname), 0);
	    break;
	case 5:
	    CHECK(ret_string(in, ucs2, len, &ti->dnstreename), 0);
	    break;
	case 6:
	    CHECK(krb5_ret_uint32(in, &ti->avflags), 0);
	    break;
	default:
	    krb5_storage_seek(in, len, SEEK_CUR);
	    break;
	}
    }
 out:
    if (in)
	krb5_storage_free(in);
    return ret;
}

/**
 * Frees the ntlm_type1 message
 *
 * @param data message to be freed
 *
 * @ingroup ntlm_core
 */

void
heim_ntlm_free_type1(struct ntlm_type1 *data)
{
    if (data->domain)
	free(data->domain);
    if (data->hostname)
	free(data->hostname);
    memset(data, 0, sizeof(*data));
}

int
heim_ntlm_decode_type1(const struct ntlm_buf *buf, struct ntlm_type1 *data)
{
    krb5_error_code ret;
    unsigned char sig[8];
    uint32_t type;
    struct sec_buffer domain, hostname;
    krb5_storage *in;

    memset(data, 0, sizeof(*data));

    in = krb5_storage_from_readonly_mem(buf->data, buf->length);
    if (in == NULL) {
	ret = ENOMEM;
	goto out;
    }
    krb5_storage_set_byteorder(in, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(krb5_storage_read(in, sig, sizeof(sig)), sizeof(sig));
    CHECK(memcmp(ntlmsigature, sig, sizeof(ntlmsigature)), 0);
    CHECK(krb5_ret_uint32(in, &type), 0);
    CHECK(type, 1);
    CHECK(krb5_ret_uint32(in, &data->flags), 0);
    if (data->flags & NTLM_OEM_SUPPLIED_DOMAIN)
	CHECK(ret_sec_buffer(in, &domain), 0);
    if (data->flags & NTLM_OEM_SUPPLIED_WORKSTATION)
	CHECK(ret_sec_buffer(in, &hostname), 0);
#if 0
    if (domain.offset > 32) {
	CHECK(krb5_ret_uint32(in, &data->os[0]), 0);
	CHECK(krb5_ret_uint32(in, &data->os[1]), 0);
    }
#endif
    if (data->flags & NTLM_OEM_SUPPLIED_DOMAIN)
	CHECK(ret_sec_string(in, 0, &domain, &data->domain), 0);
    if (data->flags & NTLM_OEM_SUPPLIED_WORKSTATION)
	CHECK(ret_sec_string(in, 0, &hostname, &data->hostname), 0);

out:
    if (in)
	krb5_storage_free(in);
    if (ret)
	heim_ntlm_free_type1(data);

    return ret;
}

/**
 * Encodes an ntlm_type1 message.
 *
 * @param type1 the ntlm_type1 message to encode.
 * @param data is the return buffer with the encoded message, should be
 * freed with heim_ntlm_free_buf().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_encode_type1(const struct ntlm_type1 *type1, struct ntlm_buf *data)
{
    krb5_error_code ret;
    struct sec_buffer domain, hostname;
    krb5_storage *out;
    uint32_t base, flags;

    flags = type1->flags;
    base = 16;

    if (type1->domain) {
	base += 8;
	flags |= NTLM_OEM_SUPPLIED_DOMAIN;
    }
    if (type1->hostname) {
	base += 8;
	flags |= NTLM_OEM_SUPPLIED_WORKSTATION;
    }
    if (type1->os[0])
	base += 8;

    domain.offset = base;
    if (type1->domain) {
	domain.length = len_string(0, type1->domain);
	domain.allocated = domain.length;
    } else {
	domain.length = 0;
	domain.allocated = 0;
    }

    hostname.offset = domain.allocated + domain.offset;
    if (type1->hostname) {
	hostname.length = len_string(0, type1->hostname);
	hostname.allocated = hostname.length;
    } else {
	hostname.length = 0;
	hostname.allocated = 0;
    }

    out = krb5_storage_emem();
    if (out == NULL)
	return ENOMEM;

    krb5_storage_set_byteorder(out, KRB5_STORAGE_BYTEORDER_LE);
    CHECK(krb5_storage_write(out, ntlmsigature, sizeof(ntlmsigature)),
	  sizeof(ntlmsigature));
    CHECK(krb5_store_uint32(out, 1), 0);
    CHECK(krb5_store_uint32(out, flags), 0);

    CHECK(store_sec_buffer(out, &domain), 0);
    CHECK(store_sec_buffer(out, &hostname), 0);
#if 0
	CHECK(krb5_store_uint32(out, type1->os[0]), 0);
	CHECK(krb5_store_uint32(out, type1->os[1]), 0);
#endif
    if (type1->domain)
	CHECK(put_string(out, 0, type1->domain), 0);
    if (type1->hostname)
	CHECK(put_string(out, 0, type1->hostname), 0);

    {
	krb5_data d;
	ret = krb5_storage_to_data(out, &d);
	data->data = d.data;
	data->length = d.length;
    }
out:
    krb5_storage_free(out);

    return ret;
}

/**
 * Frees the ntlm_type2 message
 *
 * @param data message to be freed
 *
 * @ingroup ntlm_core
 */

void
heim_ntlm_free_type2(struct ntlm_type2 *data)
{
    if (data->targetname)
	free(data->targetname);
    heim_ntlm_free_buf(&data->targetinfo);
    memset(data, 0, sizeof(*data));
}

int
heim_ntlm_decode_type2(const struct ntlm_buf *buf, struct ntlm_type2 *type2)
{
    krb5_error_code ret;
    unsigned char sig[8];
    uint32_t type, ctx[2];
    struct sec_buffer targetname, targetinfo;
    krb5_storage *in;
    int ucs2 = 0;

    memset(type2, 0, sizeof(*type2));

    in = krb5_storage_from_readonly_mem(buf->data, buf->length);
    if (in == NULL) {
	ret = ENOMEM;
	goto out;
    }
    krb5_storage_set_byteorder(in, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(krb5_storage_read(in, sig, sizeof(sig)), sizeof(sig));
    CHECK(memcmp(ntlmsigature, sig, sizeof(ntlmsigature)), 0);
    CHECK(krb5_ret_uint32(in, &type), 0);
    CHECK(type, 2);

    CHECK(ret_sec_buffer(in, &targetname), 0);
    CHECK(krb5_ret_uint32(in, &type2->flags), 0);
    if (type2->flags & NTLM_NEG_UNICODE)
	ucs2 = 1;
    CHECK(krb5_storage_read(in, type2->challenge, sizeof(type2->challenge)),
	  sizeof(type2->challenge));
    CHECK(krb5_ret_uint32(in, &ctx[0]), 0); /* context */
    CHECK(krb5_ret_uint32(in, &ctx[1]), 0);
    CHECK(ret_sec_buffer(in, &targetinfo), 0);
    /* os version */
    if (type2->flags & NTLM_NEG_VERSION) {
	CHECK(krb5_ret_uint32(in, &type2->os[0]), 0);
	CHECK(krb5_ret_uint32(in, &type2->os[1]), 0);
    }

    CHECK(ret_sec_string(in, ucs2, &targetname, &type2->targetname), 0);
    CHECK(ret_buf(in, &targetinfo, &type2->targetinfo), 0);
    ret = 0;

out:
    if (in)
	krb5_storage_free(in);
    if (ret)
	heim_ntlm_free_type2(type2);

    return ret;
}

/**
 * Encodes an ntlm_type2 message.
 *
 * @param type2 the ntlm_type2 message to encode.
 * @param data is the return buffer with the encoded message, should be
 * freed with heim_ntlm_free_buf().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_encode_type2(const struct ntlm_type2 *type2, struct ntlm_buf *data)
{
    struct sec_buffer targetname, targetinfo;
    krb5_error_code ret;
    krb5_storage *out = NULL;
    uint32_t base;
    int ucs2 = 0;

    base = 48;

    if (type2->flags & NTLM_NEG_VERSION)
	base += 8;

    if (type2->flags & NTLM_NEG_UNICODE)
	ucs2 = 1;

    targetname.offset = base;
    targetname.length = len_string(ucs2, type2->targetname);
    targetname.allocated = targetname.length;

    targetinfo.offset = targetname.allocated + targetname.offset;
    targetinfo.length = type2->targetinfo.length;
    targetinfo.allocated = type2->targetinfo.length;

    out = krb5_storage_emem();
    if (out == NULL)
	return ENOMEM;

    krb5_storage_set_byteorder(out, KRB5_STORAGE_BYTEORDER_LE);
    CHECK(krb5_storage_write(out, ntlmsigature, sizeof(ntlmsigature)),
	  sizeof(ntlmsigature));
    CHECK(krb5_store_uint32(out, 2), 0);
    CHECK(store_sec_buffer(out, &targetname), 0);
    CHECK(krb5_store_uint32(out, type2->flags), 0);
    CHECK(krb5_storage_write(out, type2->challenge, sizeof(type2->challenge)),
	  sizeof(type2->challenge));
    CHECK(krb5_store_uint32(out, 0), 0); /* context */
    CHECK(krb5_store_uint32(out, 0), 0);
    CHECK(store_sec_buffer(out, &targetinfo), 0);
    /* os version */
    if (type2->flags & NTLM_NEG_VERSION) {
	CHECK(krb5_store_uint32(out, type2->os[0]), 0);
	CHECK(krb5_store_uint32(out, type2->os[1]), 0);
    }
    CHECK(put_string(out, ucs2, type2->targetname), 0);
    CHECK(krb5_storage_write(out, type2->targetinfo.data,
			     type2->targetinfo.length),
	  type2->targetinfo.length);

    {
	krb5_data d;
	ret = krb5_storage_to_data(out, &d);
	data->data = d.data;
	data->length = d.length;
    }

out:
    krb5_storage_free(out);

    return ret;
}

/**
 * Frees the ntlm_type3 message
 *
 * @param data message to be freed
 *
 * @ingroup ntlm_core
 */

void
heim_ntlm_free_type3(struct ntlm_type3 *data)
{
    heim_ntlm_free_buf(&data->lm);
    heim_ntlm_free_buf(&data->ntlm);
    if (data->targetname)
	free(data->targetname);
    if (data->username)
	free(data->username);
    if (data->ws)
	free(data->ws);
    heim_ntlm_free_buf(&data->sessionkey);
    memset(data, 0, sizeof(*data));
}

/*
 *
 */

int
heim_ntlm_decode_type3(const struct ntlm_buf *buf,
		       int ucs2,
		       struct ntlm_type3 *type3)
{
    krb5_error_code ret;
    unsigned char sig[8];
    uint32_t type;
    krb5_storage *in;
    struct sec_buffer lm, ntlm, target, username, sessionkey, ws;
    uint32_t min_offset = 72;

    memset(type3, 0, sizeof(*type3));
    memset(&sessionkey, 0, sizeof(sessionkey));

    in = krb5_storage_from_readonly_mem(buf->data, buf->length);
    if (in == NULL) {
	ret = ENOMEM;
	goto out;
    }
    krb5_storage_set_byteorder(in, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(krb5_storage_read(in, sig, sizeof(sig)), sizeof(sig));
    CHECK(memcmp(ntlmsigature, sig, sizeof(ntlmsigature)), 0);
    CHECK(krb5_ret_uint32(in, &type), 0);
    CHECK(type, 3);
    CHECK(ret_sec_buffer(in, &lm), 0);
    if (lm.allocated)
	min_offset = min(min_offset, lm.offset);
    CHECK(ret_sec_buffer(in, &ntlm), 0);
    if (ntlm.allocated)
	min_offset = min(min_offset, ntlm.offset);
    CHECK(ret_sec_buffer(in, &target), 0);
    if (target.allocated)
	min_offset = min(min_offset, target.offset);
    CHECK(ret_sec_buffer(in, &username), 0);
    if (username.allocated)
	min_offset = min(min_offset, username.offset);
    CHECK(ret_sec_buffer(in, &ws), 0);
    if (ws.allocated)
	min_offset = min(min_offset, ws.offset);

    if (min_offset > 52) {
	CHECK(ret_sec_buffer(in, &sessionkey), 0);
	min_offset = max(min_offset, sessionkey.offset);
	CHECK(krb5_ret_uint32(in, &type3->flags), 0);
    }
    if (min_offset > 52 + 8 + 4 + 8) {
	CHECK(krb5_ret_uint32(in, &type3->os[0]), 0);
	CHECK(krb5_ret_uint32(in, &type3->os[1]), 0);
    }
    CHECK(ret_buf(in, &lm, &type3->lm), 0);
    CHECK(ret_buf(in, &ntlm, &type3->ntlm), 0);
    CHECK(ret_sec_string(in, ucs2, &target, &type3->targetname), 0);
    CHECK(ret_sec_string(in, ucs2, &username, &type3->username), 0);
    CHECK(ret_sec_string(in, ucs2, &ws, &type3->ws), 0);
    if (sessionkey.offset)
	CHECK(ret_buf(in, &sessionkey, &type3->sessionkey), 0);

out:
    if (in)
	krb5_storage_free(in);
    if (ret)
	heim_ntlm_free_type3(type3);

    return ret;
}

/**
 * Encodes an ntlm_type3 message.
 *
 * @param type3 the ntlm_type3 message to encode.
 * @param data is the return buffer with the encoded message, should be
 * freed with heim_ntlm_free_buf().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_encode_type3(const struct ntlm_type3 *type3, struct ntlm_buf *data)
{
    struct sec_buffer lm, ntlm, target, username, sessionkey, ws;
    krb5_error_code ret;
    krb5_storage *out = NULL;
    uint32_t base;
    int ucs2 = 0;

    memset(&lm, 0, sizeof(lm));
    memset(&ntlm, 0, sizeof(ntlm));
    memset(&target, 0, sizeof(target));
    memset(&username, 0, sizeof(username));
    memset(&ws, 0, sizeof(ws));
    memset(&sessionkey, 0, sizeof(sessionkey));

    base = 52;

    base += 8; /* sessionkey sec buf */
    base += 4; /* flags */

    if (type3->os[0]) {
	base += 8;
    }

    if (type3->flags & NTLM_NEG_UNICODE)
	ucs2 = 1;

    target.offset = base;
    target.length = len_string(ucs2, type3->targetname);
    target.allocated = target.length;

    username.offset = target.offset + target.allocated;
    username.length = len_string(ucs2, type3->username);
    username.allocated = username.length;

    ws.offset = username.offset + username.allocated;
    ws.length = len_string(ucs2, type3->ws);
    ws.allocated = ws.length;

    lm.offset = ws.offset + ws.allocated;
    lm.length = type3->lm.length;
    lm.allocated = type3->lm.length;

    ntlm.offset = lm.offset + lm.allocated;
    ntlm.length = type3->ntlm.length;
    ntlm.allocated = ntlm.length;

    sessionkey.offset = ntlm.offset + ntlm.allocated;
    sessionkey.length = type3->sessionkey.length;
    sessionkey.allocated = type3->sessionkey.length;

    out = krb5_storage_emem();
    if (out == NULL)
	return ENOMEM;

    krb5_storage_set_byteorder(out, KRB5_STORAGE_BYTEORDER_LE);
    CHECK(krb5_storage_write(out, ntlmsigature, sizeof(ntlmsigature)),
	  sizeof(ntlmsigature));
    CHECK(krb5_store_uint32(out, 3), 0);

    CHECK(store_sec_buffer(out, &lm), 0);
    CHECK(store_sec_buffer(out, &ntlm), 0);
    CHECK(store_sec_buffer(out, &target), 0);
    CHECK(store_sec_buffer(out, &username), 0);
    CHECK(store_sec_buffer(out, &ws), 0);
    CHECK(store_sec_buffer(out, &sessionkey), 0);
    CHECK(krb5_store_uint32(out, type3->flags), 0);

#if 0
    CHECK(krb5_store_uint32(out, 0), 0); /* os0 */
    CHECK(krb5_store_uint32(out, 0), 0); /* os1 */
#endif

    CHECK(put_string(out, ucs2, type3->targetname), 0);
    CHECK(put_string(out, ucs2, type3->username), 0);
    CHECK(put_string(out, ucs2, type3->ws), 0);
    CHECK(put_buf(out, &type3->lm), 0);
    CHECK(put_buf(out, &type3->ntlm), 0);
    CHECK(put_buf(out, &type3->sessionkey), 0);

    {
	krb5_data d;
	ret = krb5_storage_to_data(out, &d);
	data->data = d.data;
	data->length = d.length;
    }

out:
    krb5_storage_free(out);

    return ret;
}


/*
 *
 */

static int
splitandenc(unsigned char *hash,
	    unsigned char *challenge,
	    unsigned char *answer)
{
    EVP_CIPHER_CTX *ctx;
    unsigned char key[8];

    key[0] =  hash[0];
    key[1] = (hash[0] << 7) | (hash[1] >> 1);
    key[2] = (hash[1] << 6) | (hash[2] >> 2);
    key[3] = (hash[2] << 5) | (hash[3] >> 3);
    key[4] = (hash[3] << 4) | (hash[4] >> 4);
    key[5] = (hash[4] << 3) | (hash[5] >> 5);
    key[6] = (hash[5] << 2) | (hash[6] >> 6);
    key[7] = (hash[6] << 1);

    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL)
	return ENOMEM;

    EVP_CipherInit_ex(ctx, EVP_des_cbc(), NULL, key, NULL, 1);
    EVP_Cipher(ctx, answer, challenge, 8);
    EVP_CIPHER_CTX_free(ctx);
    memset(key, 0, sizeof(key));
    return 0;
}

/**
 * Calculate the NTLM key, the password is assumed to be in UTF8.
 *
 * @param password password to calcute the key for.
 * @param key calcuted key, should be freed with heim_ntlm_free_buf().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_nt_key(const char *password, struct ntlm_buf *key)
{
    struct ntlm_buf buf;
    EVP_MD_CTX *m;
    int ret;

    key->data = malloc(MD5_DIGEST_LENGTH);
    if (key->data == NULL)
	return ENOMEM;
    key->length = MD5_DIGEST_LENGTH;

    ret = ascii2ucs2le(password, 0, &buf);
    if (ret) {
	heim_ntlm_free_buf(key);
	return ret;
    }

    m = EVP_MD_CTX_create();
    if (m == NULL) {
	heim_ntlm_free_buf(key);
	heim_ntlm_free_buf(&buf);
	return ENOMEM;
    }

    EVP_DigestInit_ex(m, EVP_md4(), NULL);
    EVP_DigestUpdate(m, buf.data, buf.length);
    EVP_DigestFinal_ex(m, key->data, NULL);
    EVP_MD_CTX_destroy(m);

    heim_ntlm_free_buf(&buf);
    return 0;
}

/**
 * Calculate NTLMv1 response hash
 *
 * @param key the ntlm v1 key
 * @param len length of key
 * @param challenge sent by the server
 * @param answer calculated answer, should be freed with heim_ntlm_free_buf().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_calculate_ntlm1(void *key, size_t len,
			  unsigned char challenge[8],
			  struct ntlm_buf *answer)
{
    unsigned char res[21];
    int ret;

    if (len != MD4_DIGEST_LENGTH)
	return HNTLM_ERR_INVALID_LENGTH;

    memcpy(res, key, len);
    memset(&res[MD4_DIGEST_LENGTH], 0, sizeof(res) - MD4_DIGEST_LENGTH);

    answer->data = malloc(24);
    if (answer->data == NULL)
	return ENOMEM;
    answer->length = 24;

    ret = splitandenc(&res[0],  challenge, ((unsigned char *)answer->data) + 0);
    if (ret)
	goto out;
    ret = splitandenc(&res[7],  challenge, ((unsigned char *)answer->data) + 8);
    if (ret)
	goto out;
    ret = splitandenc(&res[14], challenge, ((unsigned char *)answer->data) + 16);
    if (ret)
	goto out;

    return 0;

out:
    heim_ntlm_free_buf(answer);
    return ret;
}

int
heim_ntlm_v1_base_session(void *key, size_t len,
			  struct ntlm_buf *session)
{
    EVP_MD_CTX *m;

    session->length = MD4_DIGEST_LENGTH;
    session->data = malloc(session->length);
    if (session->data == NULL) {
	session->length = 0;
	return ENOMEM;
    }

    m = EVP_MD_CTX_create();
    if (m == NULL) {
	heim_ntlm_free_buf(session);
	return ENOMEM;
    }
    EVP_DigestInit_ex(m, EVP_md4(), NULL);
    EVP_DigestUpdate(m, key, len);
    EVP_DigestFinal_ex(m, session->data, NULL);
    EVP_MD_CTX_destroy(m);

    return 0;
}

int
heim_ntlm_v2_base_session(void *key, size_t len,
			  struct ntlm_buf *ntlmResponse,
			  struct ntlm_buf *session)
{
    unsigned int hmaclen;
    HMAC_CTX *c;

    if (ntlmResponse->length <= 16)
        return HNTLM_ERR_INVALID_LENGTH;

    session->data = malloc(16);
    if (session->data == NULL)
	return ENOMEM;
    session->length = 16;

    /* Note: key is the NTLMv2 key */
    c = HMAC_CTX_new();
    if (c == NULL) {
	heim_ntlm_free_buf(session);
	return ENOMEM;
    }
    HMAC_Init_ex(c, key, len, EVP_md5(), NULL);
    HMAC_Update(c, ntlmResponse->data, 16);
    HMAC_Final(c, session->data, &hmaclen);
    HMAC_CTX_free(c);

    return 0;
}


int
heim_ntlm_keyex_wrap(struct ntlm_buf *base_session,
		     struct ntlm_buf *session,
		     struct ntlm_buf *encryptedSession)
{
    EVP_CIPHER_CTX *c;
    int ret;

    session->length = MD4_DIGEST_LENGTH;
    session->data = malloc(session->length);
    if (session->data == NULL) {
	session->length = 0;
	return ENOMEM;
    }
    encryptedSession->length = MD4_DIGEST_LENGTH;
    encryptedSession->data = malloc(encryptedSession->length);
    if (encryptedSession->data == NULL) {
	heim_ntlm_free_buf(session);
	encryptedSession->length = 0;
	return ENOMEM;
    }

    c = EVP_CIPHER_CTX_new();
    if (c == NULL) {
	heim_ntlm_free_buf(encryptedSession);
	heim_ntlm_free_buf(session);
	return ENOMEM;
    }

    ret = EVP_CipherInit_ex(c, EVP_rc4(), NULL, base_session->data, NULL, 1);
    if (ret != 1) {
	EVP_CIPHER_CTX_free(c);
	heim_ntlm_free_buf(encryptedSession);
	heim_ntlm_free_buf(session);
	return HNTLM_ERR_CRYPTO;
    }

    if (RAND_bytes(session->data, session->length) != 1) {
	EVP_CIPHER_CTX_free(c);
	heim_ntlm_free_buf(encryptedSession);
	heim_ntlm_free_buf(session);
	return HNTLM_ERR_RAND;
    }

    EVP_Cipher(c, encryptedSession->data, session->data, encryptedSession->length);
    EVP_CIPHER_CTX_free(c);

    return 0;



}



/**
 * Generates an NTLMv1 session random with assosited session master key.
 *
 * @param key the ntlm v1 key
 * @param len length of key
 * @param session generated session nonce, should be freed with heim_ntlm_free_buf().
 * @param master calculated session master key, should be freed with heim_ntlm_free_buf().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_build_ntlm1_master(void *key, size_t len,
			     struct ntlm_buf *session,
			     struct ntlm_buf *master)
{
    struct ntlm_buf sess;
    int ret;

    ret = heim_ntlm_v1_base_session(key, len, &sess);
    if (ret)
	return ret;

    ret = heim_ntlm_keyex_wrap(&sess, session, master);
    heim_ntlm_free_buf(&sess);

    return ret;
}

/**
 * Generates an NTLMv2 session random with associated session master key.
 *
 * @param key the NTLMv2 key
 * @param len length of key
 * @param blob the NTLMv2 "blob"
 * @param session generated session nonce, should be freed with heim_ntlm_free_buf().
 * @param master calculated session master key, should be freed with heim_ntlm_free_buf().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */


int
heim_ntlm_build_ntlm2_master(void *key, size_t len,
			     struct ntlm_buf *blob,
			     struct ntlm_buf *session,
			     struct ntlm_buf *master)
{
    struct ntlm_buf sess;
    int ret;

    ret = heim_ntlm_v2_base_session(key, len, blob, &sess);
    if (ret)
	return ret;

    ret = heim_ntlm_keyex_wrap(&sess, session, master);
    heim_ntlm_free_buf(&sess);

    return ret;
}

/**
 * Given a key and encrypted session, unwrap the session key
 *
 * @param baseKey the sessionBaseKey
 * @param encryptedSession encrypted session, type3.session field.
 * @param session generated session nonce, should be freed with heim_ntlm_free_buf().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_keyex_unwrap(struct ntlm_buf *baseKey,
		       struct ntlm_buf *encryptedSession,
		       struct ntlm_buf *session)
{
    EVP_CIPHER_CTX *c;

    memset(session, 0, sizeof(*session));

    if (baseKey->length != MD4_DIGEST_LENGTH)
	return HNTLM_ERR_INVALID_LENGTH;

    session->length = MD4_DIGEST_LENGTH;
    session->data = malloc(session->length);
    if (session->data == NULL) {
	session->length = 0;
	return ENOMEM;
    }
    c = EVP_CIPHER_CTX_new();
    if (c == NULL) {
	heim_ntlm_free_buf(session);
	return ENOMEM;
    }

    if (EVP_CipherInit_ex(c, EVP_rc4(), NULL, baseKey->data, NULL, 0) != 1) {
	EVP_CIPHER_CTX_free(c);
	heim_ntlm_free_buf(session);
	return HNTLM_ERR_CRYPTO;
    }

    EVP_Cipher(c, session->data, encryptedSession->data, session->length);
    EVP_CIPHER_CTX_free(c);

    return 0;
}


/**
 * Generates an NTLMv2 session key.
 *
 * @param key the ntlm key
 * @param len length of key
 * @param username name of the user, as sent in the message, assumed to be in UTF8.
 * @param target the name of the target, assumed to be in UTF8.
 * @param ntlmv2 the ntlmv2 session key
 *
 * @return 0 on success, or an error code on failure.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_ntlmv2_key(const void *key, size_t len,
		     const char *username,
		     const char *target,
		     unsigned char ntlmv2[16])
{
    int ret;
    unsigned int hmaclen;
    HMAC_CTX *c;

    c = HMAC_CTX_new();
    if (c == NULL)
	return ENOMEM;
    HMAC_Init_ex(c, key, len, EVP_md5(), NULL);
    {
	struct ntlm_buf buf;
	/* uppercase username and turn it into ucs2-le */
	ret = ascii2ucs2le(username, 1, &buf);
	if (ret)
	    goto out;
	HMAC_Update(c, buf.data, buf.length);
	free(buf.data);
	/* uppercase target and turn into ucs2-le */
	ret = ascii2ucs2le(target, 1, &buf);
	if (ret)
	    goto out;
	HMAC_Update(c, buf.data, buf.length);
	free(buf.data);
    }
    HMAC_Final(c, ntlmv2, &hmaclen);
 out:
    HMAC_CTX_free(c);

    return ret;
}

/*
 *
 */

#define NTTIME_EPOCH 0x019DB1DED53E8000LL

static uint64_t
unix2nttime(time_t unix_time)
{
    long long wt;
    wt = unix_time * (uint64_t)10000000 + (uint64_t)NTTIME_EPOCH;
    return wt;
}

static time_t
nt2unixtime(uint64_t t)
{
    t = ((t - (uint64_t)NTTIME_EPOCH) / (uint64_t)10000000);
    if (t > (((uint64_t)(time_t)(~(uint64_t)0)) >> 1))
	return 0;
    return (time_t)t;
}

/**
 * Calculate LMv2 response
 *
 * @param key the ntlm key
 * @param len length of key
 * @param username name of the user, as sent in the message, assumed to be in UTF8.
 * @param target the name of the target, assumed to be in UTF8.
 * @param serverchallenge challenge as sent by the server in the type2 message.
 * @param ntlmv2 calculated session key
 * @param answer ntlm response answer, should be freed with heim_ntlm_free_buf().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_calculate_lm2(const void *key, size_t len,
			const char *username,
			const char *target,
			const unsigned char serverchallenge[8],
			unsigned char ntlmv2[16],
			struct ntlm_buf *answer)
{
    unsigned char clientchallenge[8];
    int ret;

    if (RAND_bytes(clientchallenge, sizeof(clientchallenge)) != 1)
	return HNTLM_ERR_RAND;

    /* calculate ntlmv2 key */

    heim_ntlm_ntlmv2_key(key, len, username, target, ntlmv2);

    answer->data = malloc(24);
    if (answer->data == NULL)
        return ENOMEM;
    answer->length = 24;

    ret = heim_ntlm_derive_ntlm2_sess(ntlmv2, clientchallenge, 8,
				serverchallenge, answer->data);
    if (ret)
	return ret;

    memcpy(((uint8_t *)answer->data) + 16, clientchallenge, 8);

    return 0;
}


/**
 * Calculate NTLMv2 response
 *
 * @param key the ntlm key
 * @param len length of key
 * @param username name of the user, as sent in the message, assumed to be in UTF8.
 * @param target the name of the target, assumed to be in UTF8.
 * @param serverchallenge challenge as sent by the server in the type2 message.
 * @param infotarget infotarget as sent by the server in the type2 message.
 * @param ntlmv2 calculated session key
 * @param answer ntlm response answer, should be freed with heim_ntlm_free_buf().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_calculate_ntlm2(const void *key, size_t len,
			  const char *username,
			  const char *target,
			  const unsigned char serverchallenge[8],
			  const struct ntlm_buf *infotarget,
			  unsigned char ntlmv2[16],
			  struct ntlm_buf *answer)
{
    krb5_error_code ret;
    krb5_data data;
    unsigned char ntlmv2answer[16];
    krb5_storage *sp;
    unsigned char clientchallenge[8];
    uint64_t t;
    int code;

    t = unix2nttime(time(NULL));

    if (RAND_bytes(clientchallenge, sizeof(clientchallenge)) != 1)
	return HNTLM_ERR_RAND;

    /* calculate ntlmv2 key */

    heim_ntlm_ntlmv2_key(key, len, username, target, ntlmv2);

    /* calculate and build ntlmv2 answer */

    sp = krb5_storage_emem();
    if (sp == NULL)
	return ENOMEM;
    krb5_storage_set_flags(sp, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(krb5_store_uint32(sp, 0x00000101), 0);
    CHECK(krb5_store_uint32(sp, 0), 0);
    /* timestamp le 64 bit ts */
    CHECK(krb5_store_uint32(sp, t & 0xffffffff), 0);
    CHECK(krb5_store_uint32(sp, t >> 32), 0);

    CHECK(krb5_storage_write(sp, clientchallenge, 8), 8);

    CHECK(krb5_store_uint32(sp, 0), 0);  /* unknown but zero will work */
    CHECK(krb5_storage_write(sp, infotarget->data, infotarget->length),
	  infotarget->length);
    CHECK(krb5_store_uint32(sp, 0), 0); /* unknown but zero will work */

    CHECK(krb5_storage_to_data(sp, &data), 0);
    krb5_storage_free(sp);
    sp = NULL;

    code = heim_ntlm_derive_ntlm2_sess(ntlmv2, data.data, data.length, serverchallenge, ntlmv2answer);
    if (code) {
	krb5_data_free(&data);
	return code;
    }

    sp = krb5_storage_emem();
    if (sp == NULL) {
	krb5_data_free(&data);
	return ENOMEM;
    }

    CHECK(krb5_storage_write(sp, ntlmv2answer, 16), 16);
    CHECK(krb5_storage_write(sp, data.data, data.length), data.length);
    krb5_data_free(&data);

    CHECK(krb5_storage_to_data(sp, &data), 0);
    krb5_storage_free(sp);
    sp = NULL;

    answer->data = data.data;
    answer->length = data.length;

    return 0;
out:
    if (sp)
	krb5_storage_free(sp);
    return ret;
}

static const int authtimediff = 3600 * 2; /* 2 hours */

/**
 * Verify NTLMv2 response.
 *
 * @param key the ntlm key
 * @param len length of key
 * @param username name of the user, as sent in the message, assumed to be in UTF8.
 * @param target the name of the target, assumed to be in UTF8.
 * @param now the time now (0 if the library should pick it up itself)
 * @param serverchallenge challenge as sent by the server in the type2 message.
 * @param answer ntlm response answer, should be freed with heim_ntlm_free_buf().
 * @param infotarget infotarget as sent by the server in the type2 message.
 * @param ntlmv2 calculated session key
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_verify_ntlm2(const void *key, size_t len,
		       const char *username,
		       const char *target,
		       time_t now,
		       const unsigned char serverchallenge[8],
		       const struct ntlm_buf *answer,
		       struct ntlm_buf *infotarget,
		       unsigned char ntlmv2[16])
{
    krb5_error_code ret;
    unsigned char clientanswer[16];
    unsigned char clientnonce[8];
    unsigned char serveranswer[16];
    krb5_storage *sp;
    time_t authtime;
    uint32_t temp;
    uint64_t t;
    int code;

    infotarget->length = 0;
    infotarget->data = NULL;

    if (answer->length < 16)
	return HNTLM_ERR_INVALID_LENGTH;

    if (now == 0)
	now = time(NULL);

    /* calculate ntlmv2 key */

    heim_ntlm_ntlmv2_key(key, len, username, target, ntlmv2);

    /* calculate and build ntlmv2 answer */

    sp = krb5_storage_from_readonly_mem(answer->data, answer->length);
    if (sp == NULL)
	return ENOMEM;
    krb5_storage_set_flags(sp, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(krb5_storage_read(sp, clientanswer, 16), 16);

    CHECK(krb5_ret_uint32(sp, &temp), 0);
    CHECK(temp, 0x00000101);
    CHECK(krb5_ret_uint32(sp, &temp), 0);
    CHECK(temp, 0);
    /* timestamp le 64 bit ts */
    CHECK(krb5_ret_uint32(sp, &temp), 0);
    t = temp;
    CHECK(krb5_ret_uint32(sp, &temp), 0);
    t |= ((uint64_t)temp)<< 32;

    authtime = nt2unixtime(t);

    if (abs((int)(authtime - now)) > authtimediff) {
	ret = HNTLM_ERR_TIME_SKEW;
	goto out;
    }

    /* client challenge */
    CHECK(krb5_storage_read(sp, clientnonce, 8), 8);

    CHECK(krb5_ret_uint32(sp, &temp), 0); /* unknown */

    /* should really unparse the infotarget, but lets pick up everything */
    infotarget->length = answer->length - krb5_storage_seek(sp, 0, SEEK_CUR);
    infotarget->data = malloc(infotarget->length);
    if (infotarget->data == NULL) {
	ret = ENOMEM;
	goto out;
    }
    CHECK(krb5_storage_read(sp, infotarget->data, infotarget->length),
	  infotarget->length);
    /* XXX remove the unknown ?? */
    krb5_storage_free(sp);
    sp = NULL;

    if (answer->length < 16) {
	ret = HNTLM_ERR_INVALID_LENGTH;
	goto out;
    }

    ret = heim_ntlm_derive_ntlm2_sess(ntlmv2,
				((unsigned char *)answer->data) + 16, answer->length - 16,
				serverchallenge,
				serveranswer);
    if (ret)
	goto out;

    if (memcmp(serveranswer, clientanswer, 16) != 0) {
	heim_ntlm_free_buf(infotarget);
	return HNTLM_ERR_AUTH;
    }

    return 0;
out:
    heim_ntlm_free_buf(infotarget);
    if (sp)
	krb5_storage_free(sp);
    return ret;
}


/*
 * Calculate the NTLM2 Session Response
 *
 * @param clnt_nonce client nonce
 * @param svr_chal server challage
 * @param ntlm2_hash ntlm hash
 * @param lm The LM response, should be freed with heim_ntlm_free_buf().
 * @param ntlm The NTLM response, should be freed with heim_ntlm_free_buf().
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_calculate_ntlm2_sess(const unsigned char clnt_nonce[8],
			       const unsigned char svr_chal[8],
			       const unsigned char ntlm_hash[16],
			       struct ntlm_buf *lm,
			       struct ntlm_buf *ntlm)
{
    unsigned char ntlm2_sess_hash[8];
    unsigned char res[21], *resp;
    int code;

    code = heim_ntlm_calculate_ntlm2_sess_hash(clnt_nonce, svr_chal,
					       ntlm2_sess_hash);
    if (code) {
	return code;
    }

    lm->data = malloc(24);
    if (lm->data == NULL) {
	return ENOMEM;
    }
    lm->length = 24;

    ntlm->data = malloc(24);
    if (ntlm->data == NULL) {
	free(lm->data);
	lm->data = NULL;
	return ENOMEM;
    }
    ntlm->length = 24;

    /* first setup the lm resp */
    memset(lm->data, 0, 24);
    memcpy(lm->data, clnt_nonce, 8);

    memset(res, 0, sizeof(res));
    memcpy(res, ntlm_hash, 16);

    resp = ntlm->data;
    code = splitandenc(&res[0], ntlm2_sess_hash, resp + 0);
    if (code)
	goto out;
    code = splitandenc(&res[7], ntlm2_sess_hash, resp + 8);
    if (code)
	goto out;
    code = splitandenc(&res[14], ntlm2_sess_hash, resp + 16);
    if (code)
	goto out;

    return 0;

out:
    heim_ntlm_free_buf(ntlm);
    heim_ntlm_free_buf(lm);
    return code;
}


/*
 * Calculate the NTLM2 Session "Verifier"
 *
 * @param clnt_nonce client nonce
 * @param svr_chal server challage
 * @param hash The NTLM session verifier
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_calculate_ntlm2_sess_hash(const unsigned char clnt_nonce[8],
				    const unsigned char svr_chal[8],
				    unsigned char verifier[8])
{
    unsigned char ntlm2_sess_hash[MD5_DIGEST_LENGTH];
    EVP_MD_CTX *m;

    m = EVP_MD_CTX_create();
    if (m == NULL)
	return ENOMEM;

    EVP_DigestInit_ex(m, EVP_md5(), NULL);
    EVP_DigestUpdate(m, svr_chal, 8); /* session nonce part 1 */
    EVP_DigestUpdate(m, clnt_nonce, 8); /* session nonce part 2 */
    EVP_DigestFinal_ex(m, ntlm2_sess_hash, NULL); /* will only use first 8 bytes */
    EVP_MD_CTX_destroy(m);

    memcpy(verifier, ntlm2_sess_hash, 8);

    return 0;
}


/*
 * Derive a NTLM2 session key
 *
 * @param sessionkey session key from domain controller
 * @param clnt_nonce client nonce
 * @param svr_chal server challenge
 * @param derivedkey salted session key
 *
 * @return In case of success 0 is return, an errors, a errno in what
 * went wrong.
 *
 * @ingroup ntlm_core
 */

int
heim_ntlm_derive_ntlm2_sess(const unsigned char sessionkey[16],
			    const unsigned char *clnt_nonce, size_t clnt_nonce_length,
			    const unsigned char svr_chal[8],
			    unsigned char derivedkey[16])
{
    unsigned int hmaclen;
    HMAC_CTX *c;

    /* HMAC(Ksession, serverchallenge || clientchallenge) */
    c = HMAC_CTX_new();
    if (c == NULL)
	return ENOMEM;
    HMAC_Init_ex(c, sessionkey, 16, EVP_md5(), NULL);
    HMAC_Update(c, svr_chal, 8);
    HMAC_Update(c, clnt_nonce, clnt_nonce_length);
    HMAC_Final(c, derivedkey, &hmaclen);
    HMAC_CTX_free(c);
    return 0;
}

