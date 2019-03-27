/*
 * Copyright (c) 1997 - 2007 Kungliga Tekniska Högskolan
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

#include "der_locl.h"

/*
 * All decoding functions take a pointer `p' to first position in
 * which to read, from the left, `len' which means the maximum number
 * of characters we are able to read, `ret' were the value will be
 * returned and `size' where the number of used bytes is stored.
 * Either 0 or an error code is returned.
 */

int
der_get_unsigned (const unsigned char *p, size_t len,
		  unsigned *ret, size_t *size)
{
    unsigned val = 0;
    size_t oldlen = len;

    if (len == sizeof(unsigned) + 1 && p[0] == 0)
	;
    else if (len > sizeof(unsigned))
	return ASN1_OVERRUN;

    while (len--)
	val = val * 256 + *p++;
    *ret = val;
    if(size) *size = oldlen;
    return 0;
}

int
der_get_integer (const unsigned char *p, size_t len,
		 int *ret, size_t *size)
{
    int val = 0;
    size_t oldlen = len;

    if (len > sizeof(int))
	return ASN1_OVERRUN;

    if (len > 0) {
	val = (signed char)*p++;
	while (--len)
	    val = val * 256 + *p++;
    }
    *ret = val;
    if(size) *size = oldlen;
    return 0;
}

int
der_get_length (const unsigned char *p, size_t len,
		size_t *val, size_t *size)
{
    size_t v;

    if (len <= 0)
	return ASN1_OVERRUN;
    --len;
    v = *p++;
    if (v < 128) {
	*val = v;
	if(size) *size = 1;
    } else {
	int e;
	size_t l;
	unsigned tmp;

	if(v == 0x80){
	    *val = ASN1_INDEFINITE;
	    if(size) *size = 1;
	    return 0;
	}
	v &= 0x7F;
	if (len < v)
	    return ASN1_OVERRUN;
	e = der_get_unsigned (p, v, &tmp, &l);
	if(e) return e;
	*val = tmp;
	if(size) *size = l + 1;
    }
    return 0;
}

int
der_get_boolean(const unsigned char *p, size_t len, int *data, size_t *size)
{
    if(len < 1)
	return ASN1_OVERRUN;
    if(*p != 0)
	*data = 1;
    else
	*data = 0;
    *size = 1;
    return 0;
}

int
der_get_general_string (const unsigned char *p, size_t len,
			heim_general_string *str, size_t *size)
{
    const unsigned char *p1;
    char *s;

    p1 = memchr(p, 0, len);
    if (p1 != NULL) {
	/*
	 * Allow trailing NULs. We allow this since MIT Kerberos sends
	 * an strings in the NEED_PREAUTH case that includes a
	 * trailing NUL.
	 */
	while ((size_t)(p1 - p) < len && *p1 == '\0')
	    p1++;
       if ((size_t)(p1 - p) != len)
	    return ASN1_BAD_CHARACTER;
    }
    if (len > len + 1)
	return ASN1_BAD_LENGTH;

    s = malloc (len + 1);
    if (s == NULL)
	return ENOMEM;
    memcpy (s, p, len);
    s[len] = '\0';
    *str = s;
    if(size) *size = len;
    return 0;
}

int
der_get_utf8string (const unsigned char *p, size_t len,
		    heim_utf8_string *str, size_t *size)
{
    return der_get_general_string(p, len, str, size);
}

int
der_get_printable_string(const unsigned char *p, size_t len,
			 heim_printable_string *str, size_t *size)
{
    str->length = len;
    str->data = malloc(len + 1);
    if (str->data == NULL)
	return ENOMEM;
    memcpy(str->data, p, len);
    ((char *)str->data)[len] = '\0';
    if(size) *size = len;
    return 0;
}

int
der_get_ia5_string(const unsigned char *p, size_t len,
		   heim_ia5_string *str, size_t *size)
{
    return der_get_printable_string(p, len, str, size);
}

int
der_get_bmp_string (const unsigned char *p, size_t len,
		    heim_bmp_string *data, size_t *size)
{
    size_t i;

    if (len & 1)
	return ASN1_BAD_FORMAT;
    data->length = len / 2;
    if (data->length > UINT_MAX/sizeof(data->data[0]))
	return ERANGE;
    data->data = malloc(data->length * sizeof(data->data[0]));
    if (data->data == NULL && data->length != 0)
	return ENOMEM;

    for (i = 0; i < data->length; i++) {
	data->data[i] = (p[0] << 8) | p[1];
	p += 2;
	/* check for NUL in the middle of the string */
	if (data->data[i] == 0 && i != (data->length - 1)) {
	    free(data->data);
	    data->data = NULL;
	    data->length = 0;
	    return ASN1_BAD_CHARACTER;
	}
    }
    if (size) *size = len;

    return 0;
}

int
der_get_universal_string (const unsigned char *p, size_t len,
			  heim_universal_string *data, size_t *size)
{
    size_t i;

    if (len & 3)
	return ASN1_BAD_FORMAT;
    data->length = len / 4;
    if (data->length > UINT_MAX/sizeof(data->data[0]))
	return ERANGE;
    data->data = malloc(data->length * sizeof(data->data[0]));
    if (data->data == NULL && data->length != 0)
	return ENOMEM;

    for (i = 0; i < data->length; i++) {
	data->data[i] = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
	p += 4;
	/* check for NUL in the middle of the string */
	if (data->data[i] == 0 && i != (data->length - 1)) {
	    free(data->data);
	    data->data = NULL;
	    data->length = 0;
	    return ASN1_BAD_CHARACTER;
	}
    }
    if (size) *size = len;
    return 0;
}

int
der_get_visible_string (const unsigned char *p, size_t len,
			heim_visible_string *str, size_t *size)
{
    return der_get_general_string(p, len, str, size);
}

int
der_get_octet_string (const unsigned char *p, size_t len,
		      heim_octet_string *data, size_t *size)
{
    data->length = len;
    data->data = malloc(len);
    if (data->data == NULL && data->length != 0)
	return ENOMEM;
    memcpy (data->data, p, len);
    if(size) *size = len;
    return 0;
}

int
der_get_octet_string_ber (const unsigned char *p, size_t len,
			  heim_octet_string *data, size_t *size)
{
    int e;
    Der_type type;
    Der_class class;
    unsigned int tag, depth = 0;
    size_t l, datalen, oldlen = len;

    data->length = 0;
    data->data = NULL;

    while (len) {
	e = der_get_tag (p, len, &class, &type, &tag, &l);
	if (e) goto out;
	if (class != ASN1_C_UNIV) {
	    e = ASN1_BAD_ID;
	    goto out;
	}
	if (type == PRIM && tag == UT_EndOfContent) {
	    if (depth == 0)
		break;
	    depth--;
	}
	if (tag != UT_OctetString) {
	    e = ASN1_BAD_ID;
	    goto out;
	}

	p += l;
	len -= l;
	e = der_get_length (p, len, &datalen, &l);
	if (e) goto out;
	p += l;
	len -= l;

	if (datalen > len)
	    return ASN1_OVERRUN;

	if (type == PRIM) {
	    void *ptr;

	    ptr = realloc(data->data, data->length + datalen);
	    if (ptr == NULL) {
		e = ENOMEM;
		goto out;
	    }
	    data->data = ptr;
	    memcpy(((unsigned char *)data->data) + data->length, p, datalen);
	    data->length += datalen;
	} else
	    depth++;

	p += datalen;
	len -= datalen;
    }
    if (depth != 0)
	return ASN1_INDEF_OVERRUN;
    if(size) *size = oldlen - len;
    return 0;
 out:
    free(data->data);
    data->data = NULL;
    data->length = 0;
    return e;
}


int
der_get_heim_integer (const unsigned char *p, size_t len,
		      heim_integer *data, size_t *size)
{
    data->length = 0;
    data->negative = 0;
    data->data = NULL;

    if (len == 0) {
	if (size)
	    *size = 0;
	return 0;
    }
    if (p[0] & 0x80) {
	unsigned char *q;
	int carry = 1;
	data->negative = 1;

	data->length = len;

	if (p[0] == 0xff) {
	    p++;
	    data->length--;
	}
	data->data = malloc(data->length);
	if (data->data == NULL) {
	    data->length = 0;
	    if (size)
		*size = 0;
	    return ENOMEM;
	}
	q = &((unsigned char*)data->data)[data->length - 1];
	p += data->length - 1;
	while (q >= (unsigned char*)data->data) {
	    *q = *p ^ 0xff;
	    if (carry)
		carry = !++*q;
	    p--;
	    q--;
	}
    } else {
	data->negative = 0;
	data->length = len;

	if (p[0] == 0) {
	    p++;
	    data->length--;
	}
	data->data = malloc(data->length);
	if (data->data == NULL && data->length != 0) {
	    data->length = 0;
	    if (size)
		*size = 0;
	    return ENOMEM;
	}
	memcpy(data->data, p, data->length);
    }
    if (size)
	*size = len;
    return 0;
}

static int
generalizedtime2time (const char *s, time_t *t)
{
    struct tm tm;

    memset(&tm, 0, sizeof(tm));
    if (sscanf (s, "%04d%02d%02d%02d%02d%02dZ",
		&tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
		&tm.tm_min, &tm.tm_sec) != 6) {
	if (sscanf (s, "%02d%02d%02d%02d%02d%02dZ",
		    &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
		    &tm.tm_min, &tm.tm_sec) != 6)
	    return ASN1_BAD_TIMEFORMAT;
	if (tm.tm_year < 50)
	    tm.tm_year += 2000;
	else
	    tm.tm_year += 1900;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    *t = _der_timegm (&tm);
    return 0;
}

static int
der_get_time (const unsigned char *p, size_t len,
	      time_t *data, size_t *size)
{
    char *times;
    int e;

    if (len > len + 1 || len == 0)
	return ASN1_BAD_LENGTH;

    times = malloc(len + 1);
    if (times == NULL)
	return ENOMEM;
    memcpy(times, p, len);
    times[len] = '\0';
    e = generalizedtime2time(times, data);
    free (times);
    if(size) *size = len;
    return e;
}

int
der_get_generalized_time (const unsigned char *p, size_t len,
			  time_t *data, size_t *size)
{
    return der_get_time(p, len, data, size);
}

int
der_get_utctime (const unsigned char *p, size_t len,
			  time_t *data, size_t *size)
{
    return der_get_time(p, len, data, size);
}

int
der_get_oid (const unsigned char *p, size_t len,
	     heim_oid *data, size_t *size)
{
    size_t n;
    size_t oldlen = len;

    if (len < 1)
	return ASN1_OVERRUN;

    if (len > len + 1)
	return ASN1_BAD_LENGTH;

    if (len + 1 > UINT_MAX/sizeof(data->components[0]))
	return ERANGE;

    data->components = malloc((len + 1) * sizeof(data->components[0]));
    if (data->components == NULL)
	return ENOMEM;
    data->components[0] = (*p) / 40;
    data->components[1] = (*p) % 40;
    --len;
    ++p;
    for (n = 2; len > 0; ++n) {
	unsigned u = 0, u1;

	do {
	    --len;
	    u1 = u * 128 + (*p++ % 128);
	    /* check that we don't overflow the element */
	    if (u1 < u) {
		der_free_oid(data);
		return ASN1_OVERRUN;
	    }
	    u = u1;
	} while (len > 0 && p[-1] & 0x80);
	data->components[n] = u;
    }
    if (n > 2 && p[-1] & 0x80) {
	der_free_oid (data);
	return ASN1_OVERRUN;
    }
    data->length = n;
    if (size)
	*size = oldlen;
    return 0;
}

int
der_get_tag (const unsigned char *p, size_t len,
	     Der_class *class, Der_type *type,
	     unsigned int *tag, size_t *size)
{
    size_t ret = 0;
    if (len < 1)
	return ASN1_OVERRUN;
    *class = (Der_class)(((*p) >> 6) & 0x03);
    *type = (Der_type)(((*p) >> 5) & 0x01);
    *tag = (*p) & 0x1f;
    p++; len--; ret++;
    if(*tag == 0x1f) {
	unsigned int continuation;
	unsigned int tag1;
	*tag = 0;
	do {
	    if(len < 1)
		return ASN1_OVERRUN;
	    continuation = *p & 128;
	    tag1 = *tag * 128 + (*p % 128);
	    /* check that we don't overflow the tag */
	    if (tag1 < *tag)
		return ASN1_OVERFLOW;
	    *tag = tag1;
	    p++; len--; ret++;
	} while(continuation);
    }
    if(size) *size = ret;
    return 0;
}

int
der_match_tag (const unsigned char *p, size_t len,
	       Der_class class, Der_type type,
	       unsigned int tag, size_t *size)
{
    Der_type thistype;
    int e;

    e = der_match_tag2(p, len, class, &thistype, tag, size);
    if (e) return e;
    if (thistype != type) return ASN1_BAD_ID;
    return 0;
}

int
der_match_tag2 (const unsigned char *p, size_t len,
		Der_class class, Der_type *type,
		unsigned int tag, size_t *size)
{
    size_t l;
    Der_class thisclass;
    unsigned int thistag;
    int e;

    e = der_get_tag (p, len, &thisclass, type, &thistag, &l);
    if (e) return e;
    if (class != thisclass)
	return ASN1_BAD_ID;
    if(tag > thistag)
	return ASN1_MISPLACED_FIELD;
    if(tag < thistag)
	return ASN1_MISSING_FIELD;
    if(size) *size = l;
    return 0;
}

int
der_match_tag_and_length (const unsigned char *p, size_t len,
			  Der_class class, Der_type *type, unsigned int tag,
			  size_t *length_ret, size_t *size)
{
    size_t l, ret = 0;
    int e;

    e = der_match_tag2 (p, len, class, type, tag, &l);
    if (e) return e;
    p += l;
    len -= l;
    ret += l;
    e = der_get_length (p, len, length_ret, &l);
    if (e) return e;
    if(size) *size = ret + l;
    return 0;
}



/*
 * Old versions of DCE was based on a very early beta of the MIT code,
 * which used MAVROS for ASN.1 encoding. MAVROS had the interesting
 * feature that it encoded data in the forward direction, which has
 * it's problems, since you have no idea how long the data will be
 * until after you're done. MAVROS solved this by reserving one byte
 * for length, and later, if the actual length was longer, it reverted
 * to indefinite, BER style, lengths. The version of MAVROS used by
 * the DCE people could apparently generate correct X.509 DER encodings, and
 * did this by making space for the length after encoding, but
 * unfortunately this feature wasn't used with Kerberos.
 */

int
_heim_fix_dce(size_t reallen, size_t *len)
{
    if(reallen == ASN1_INDEFINITE)
	return 1;
    if(*len < reallen)
	return -1;
    *len = reallen;
    return 0;
}

int
der_get_bit_string (const unsigned char *p, size_t len,
		    heim_bit_string *data, size_t *size)
{
    if (len < 1)
	return ASN1_OVERRUN;
    if (p[0] > 7)
	return ASN1_BAD_FORMAT;
    if (len - 1 == 0 && p[0] != 0)
	return ASN1_BAD_FORMAT;
    /* check if any of the three upper bits are set
     * any of them will cause a interger overrun */
    if ((len - 1) >> (sizeof(len) * 8 - 3))
	return ASN1_OVERRUN;
    data->length = (len - 1) * 8;
    data->data = malloc(len - 1);
    if (data->data == NULL && (len - 1) != 0)
	return ENOMEM;
    /* copy data is there is data to copy */
    if (len - 1 != 0) {
      memcpy (data->data, p + 1, len - 1);
      data->length -= p[0];
    }
    if(size) *size = len;
    return 0;
}
