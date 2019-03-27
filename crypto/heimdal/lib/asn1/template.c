/*
 * Copyright (c) 2009 Kungliga Tekniska Högskolan
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

#include "der_locl.h"
#include <com_err.h>

#if 0
#define ABORT_ON_ERROR() abort()
#else
#define ABORT_ON_ERROR() do { } while(0)
#endif

#define DPOC(data,offset) ((const void *)(((const unsigned char *)data)  + offset))
#define DPO(data,offset) ((void *)(((unsigned char *)data)  + offset))


static struct asn1_type_func prim[] = {
#define el(name, type) {				\
	(asn1_type_encode)der_put_##name,		\
	(asn1_type_decode)der_get_##name,		\
	(asn1_type_length)der_length_##name,		\
	(asn1_type_copy)der_copy_##name,		\
	(asn1_type_release)der_free_##name,		\
	sizeof(type)					\
    }
#define elber(name, type) {				\
	(asn1_type_encode)der_put_##name,		\
	(asn1_type_decode)der_get_##name##_ber,		\
	(asn1_type_length)der_length_##name,		\
	(asn1_type_copy)der_copy_##name,		\
	(asn1_type_release)der_free_##name,		\
	sizeof(type)					\
    }
    el(integer, int),
    el(heim_integer, heim_integer),
    el(integer, int),
    el(unsigned, unsigned),
    el(general_string, heim_general_string),
    el(octet_string, heim_octet_string),
    elber(octet_string, heim_octet_string),
    el(ia5_string, heim_ia5_string),
    el(bmp_string, heim_bmp_string),
    el(universal_string, heim_universal_string),
    el(printable_string, heim_printable_string),
    el(visible_string, heim_visible_string),
    el(utf8string, heim_utf8_string),
    el(generalized_time, time_t),
    el(utctime, time_t),
    el(bit_string, heim_bit_string),
    { (asn1_type_encode)der_put_boolean, (asn1_type_decode)der_get_boolean,
      (asn1_type_length)der_length_boolean, (asn1_type_copy)der_copy_integer,
      (asn1_type_release)der_free_integer, sizeof(int)
    },
    el(oid, heim_oid),
    el(general_string, heim_general_string),
#undef el
#undef elber
};

static size_t
sizeofType(const struct asn1_template *t)
{
    return t->offset;
}

/*
 * Here is abstraction to not so well evil fact of bit fields in C,
 * they are endian dependent, so when getting and setting bits in the
 * host local structure we need to know the endianness of the host.
 *
 * Its not the first time in Heimdal this have bitten us, and some day
 * we'll grow up and use #defined constant, but bit fields are still
 * so pretty and shiny.
 */

static void
bmember_get_bit(const unsigned char *p, void *data,
		unsigned int bit, size_t size)
{
    unsigned int localbit = bit % 8;
    if ((*p >> (7 - localbit)) & 1) {
#ifdef WORDS_BIGENDIAN
	*(unsigned int *)data |= (1 << ((size * 8) - bit - 1));
#else
	*(unsigned int *)data |= (1 << bit);
#endif
    }
}

static int
bmember_isset_bit(const void *data, unsigned int bit, size_t size)
{
#ifdef WORDS_BIGENDIAN
    if ((*(unsigned int *)data) & (1 << ((size * 8) - bit - 1)))
	return 1;
    return 0;
#else
    if ((*(unsigned int *)data) & (1 << bit))
	return 1;
    return 0;
#endif
}

static void
bmember_put_bit(unsigned char *p, const void *data, unsigned int bit,
		size_t size, unsigned int *bitset)
{
    unsigned int localbit = bit % 8;

    if (bmember_isset_bit(data, bit, size)) {
	*p |= (1 << (7 - localbit));
	if (*bitset == 0)
	    *bitset = (7 - localbit) + 1;
    }
}

int
_asn1_decode(const struct asn1_template *t, unsigned flags,
	     const unsigned char *p, size_t len, void *data, size_t *size)
{
    size_t elements = A1_HEADER_LEN(t);
    size_t oldlen = len;
    int ret = 0;
    const unsigned char *startp = NULL;
    unsigned int template_flags = t->tt;

    /* skip over header */
    t++;

    if (template_flags & A1_HF_PRESERVE)
	startp = p;

    while (elements) {
	switch (t->tt & A1_OP_MASK) {
	case A1_OP_TYPE:
	case A1_OP_TYPE_EXTERN: {
	    size_t newsize, size;
	    void *el = DPO(data, t->offset);
	    void **pel = (void **)el;

	    if ((t->tt & A1_OP_MASK) == A1_OP_TYPE) {
		size = sizeofType(t->ptr);
	    } else {
		const struct asn1_type_func *f = t->ptr;
		size = f->size;
	    }

	    if (t->tt & A1_FLAG_OPTIONAL) {
		*pel = calloc(1, size);
		if (*pel == NULL)
		    return ENOMEM;
		el = *pel;
	    }
	    if ((t->tt & A1_OP_MASK) == A1_OP_TYPE) {
		ret = _asn1_decode(t->ptr, flags, p, len, el, &newsize);
	    } else {
		const struct asn1_type_func *f = t->ptr;
		ret = (f->decode)(p, len, el, &newsize);
	    }
	    if (ret) {
		if (t->tt & A1_FLAG_OPTIONAL) {
		    free(*pel);
		    *pel = NULL;
		    break;
		}
		return ret;
	    }
	    p += newsize; len -= newsize;

	    break;
	}
	case A1_OP_TAG: {
	    Der_type dertype;
	    size_t newsize;
	    size_t datalen, l;
	    void *olddata = data;
	    int is_indefinite = 0;
	    int subflags = flags;

	    ret = der_match_tag_and_length(p, len, A1_TAG_CLASS(t->tt),
					   &dertype, A1_TAG_TAG(t->tt),
					   &datalen, &l);
	    if (ret) {
		if (t->tt & A1_FLAG_OPTIONAL)
		    break;
		return ret;
	    }

	    p += l; len -= l;

	    /*
	     * Only allow indefinite encoding for OCTET STRING and BER
	     * for now. Should handle BIT STRING too.
	     */

	    if (dertype != A1_TAG_TYPE(t->tt) && (flags & A1_PF_ALLOW_BER)) {
		const struct asn1_template *subtype = t->ptr;
		subtype++; /* skip header */

		if (((subtype->tt & A1_OP_MASK) == A1_OP_PARSE) &&
		    A1_PARSE_TYPE(subtype->tt) == A1T_OCTET_STRING)
		    subflags |= A1_PF_INDEFINTE;
	    }

	    if (datalen == ASN1_INDEFINITE) {
		if ((flags & A1_PF_ALLOW_BER) == 0)
		    return ASN1_GOT_BER;
		is_indefinite = 1;
		datalen = len;
		if (datalen < 2)
		    return ASN1_OVERRUN;
		/* hide EndOfContent for sub-decoder, catching it below */
		datalen -= 2;
	    } else if (datalen > len)
		return ASN1_OVERRUN;

	    data = DPO(data, t->offset);

	    if (t->tt & A1_FLAG_OPTIONAL) {
		void **el = (void **)data;
		size_t ellen = sizeofType(t->ptr);

		*el = calloc(1, ellen);
		if (*el == NULL)
		    return ENOMEM;
		data = *el;
	    }

	    ret = _asn1_decode(t->ptr, subflags, p, datalen, data, &newsize);
	    if (ret)
		return ret;

	    if (newsize != datalen)
		return ASN1_EXTRA_DATA;

	    len -= datalen;
	    p += datalen;

	    /*
	     * Indefinite encoding needs a trailing EndOfContent,
	     * check for that.
	     */
	    if (is_indefinite) {
		ret = der_match_tag_and_length(p, len, ASN1_C_UNIV,
					       &dertype, UT_EndOfContent,
					       &datalen, &l);
		if (ret)
		    return ret;
		if (dertype != PRIM)
		    return ASN1_BAD_ID;
		if (datalen != 0)
		    return ASN1_INDEF_EXTRA_DATA;
		p += l; len -= l;
	    }
	    data = olddata;

	    break;
	}
	case A1_OP_PARSE: {
	    unsigned int type = A1_PARSE_TYPE(t->tt);
	    size_t newsize;
	    void *el = DPO(data, t->offset);

	    /*
	     * INDEFINITE primitive types are one element after the
	     * same type but non-INDEFINITE version.
	    */
	    if (flags & A1_PF_INDEFINTE)
		type++;

	    if (type >= sizeof(prim)/sizeof(prim[0])) {
		ABORT_ON_ERROR();
		return ASN1_PARSE_ERROR;
	    }

	    ret = (prim[type].decode)(p, len, el, &newsize);
	    if (ret)
		return ret;
	    p += newsize; len -= newsize;

	    break;
	}
	case A1_OP_SETOF:
	case A1_OP_SEQOF: {
	    struct template_of *el = DPO(data, t->offset);
	    size_t newsize;
	    size_t ellen = sizeofType(t->ptr);
	    size_t vallength = 0;

	    while (len > 0) {
		void *tmp;
		size_t newlen = vallength + ellen;
		if (vallength > newlen)
		    return ASN1_OVERFLOW;

		tmp = realloc(el->val, newlen);
		if (tmp == NULL)
		    return ENOMEM;

		memset(DPO(tmp, vallength), 0, ellen);
		el->val = tmp;

		ret = _asn1_decode(t->ptr, flags & (~A1_PF_INDEFINTE), p, len,
				   DPO(el->val, vallength), &newsize);
		if (ret)
		    return ret;
		vallength = newlen;
		el->len++;
		p += newsize; len -= newsize;
	    }

	    break;
	}
	case A1_OP_BMEMBER: {
	    const struct asn1_template *bmember = t->ptr;
	    size_t size = bmember->offset;
	    size_t elements = A1_HEADER_LEN(bmember);
	    size_t pos = 0;

	    bmember++;

	    memset(data, 0, size);

	    if (len < 1)
		return ASN1_OVERRUN;
	    p++; len--;

	    while (elements && len) {
		while (bmember->offset / 8 > pos / 8) {
		    if (len < 1)
			break;
		    p++; len--;
		    pos += 8;
		}
		if (len) {
		    bmember_get_bit(p, data, bmember->offset, size);
		    elements--; bmember++;
		}
	    }
	    len = 0;
	    break;
	}
	case A1_OP_CHOICE: {
	    const struct asn1_template *choice = t->ptr;
	    unsigned int *element = DPO(data, choice->offset);
	    size_t datalen;
	    unsigned int i;

	    for (i = 1; i < A1_HEADER_LEN(choice) + 1; i++) {
		/* should match first tag instead, store it in choice.tt */
		ret = _asn1_decode(choice[i].ptr, 0, p, len,
				   DPO(data, choice[i].offset), &datalen);
		if (ret == 0) {
		    *element = i;
		    p += datalen; len -= datalen;
		    break;
		} else if (ret != ASN1_BAD_ID && ret != ASN1_MISPLACED_FIELD && ret != ASN1_MISSING_FIELD) {
		    return ret;
		}
	    }
	    if (i >= A1_HEADER_LEN(choice) + 1) {
		if (choice->tt == 0)
		    return ASN1_BAD_ID;

		*element = 0;
		ret = der_get_octet_string(p, len,
					   DPO(data, choice->tt), &datalen);
		if (ret)
		    return ret;
		p += datalen; len -= datalen;
	    }

	    break;
	}
	default:
	    ABORT_ON_ERROR();
	    return ASN1_PARSE_ERROR;
	}
	t++;
	elements--;
    }
    /* if we are using padding, eat up read of context */
    if (template_flags & A1_HF_ELLIPSIS)
	len = 0;

    oldlen -= len;

    if (size)
	*size = oldlen;

    /*
     * saved the raw bits if asked for it, useful for signature
     * verification.
     */
    if (startp) {
	heim_octet_string *save = data;

	save->data = malloc(oldlen);
	if (save->data == NULL)
	    return ENOMEM;
	else {
	    save->length = oldlen;
	    memcpy(save->data, startp, oldlen);
	}
    }
    return 0;
}

int
_asn1_encode(const struct asn1_template *t, unsigned char *p, size_t len, const void *data, size_t *size)
{
    size_t elements = A1_HEADER_LEN(t);
    int ret = 0;
    size_t oldlen = len;

    t += A1_HEADER_LEN(t);

    while (elements) {
	switch (t->tt & A1_OP_MASK) {
	case A1_OP_TYPE:
	case A1_OP_TYPE_EXTERN: {
	    size_t newsize;
	    const void *el = DPOC(data, t->offset);

	    if (t->tt & A1_FLAG_OPTIONAL) {
		void **pel = (void **)el;
		if (*pel == NULL)
		    break;
		el = *pel;
	    }

	    if ((t->tt & A1_OP_MASK) == A1_OP_TYPE) {
		ret = _asn1_encode(t->ptr, p, len, el, &newsize);
	    } else {
		const struct asn1_type_func *f = t->ptr;
		ret = (f->encode)(p, len, el, &newsize);
	    }

	    if (ret)
		return ret;
	    p -= newsize; len -= newsize;

	    break;
	}
	case A1_OP_TAG: {
	    const void *olddata = data;
	    size_t l, datalen;

	    data = DPOC(data, t->offset);

	    if (t->tt & A1_FLAG_OPTIONAL) {
		void **el = (void **)data;
		if (*el == NULL) {
		    data = olddata;
		    break;
		}
		data = *el;
	    }

	    ret = _asn1_encode(t->ptr, p, len, data, &datalen);
	    if (ret)
		return ret;

	    len -= datalen; p -= datalen;

	    ret = der_put_length_and_tag(p, len, datalen,
					 A1_TAG_CLASS(t->tt),
					 A1_TAG_TYPE(t->tt),
					 A1_TAG_TAG(t->tt), &l);
	    if (ret)
		return ret;

	    p -= l; len -= l;

	    data = olddata;

	    break;
	}
	case A1_OP_PARSE: {
	    unsigned int type = A1_PARSE_TYPE(t->tt);
	    size_t newsize;
	    const void *el = DPOC(data, t->offset);

	    if (type > sizeof(prim)/sizeof(prim[0])) {
		ABORT_ON_ERROR();
		return ASN1_PARSE_ERROR;
	    }

	    ret = (prim[type].encode)(p, len, el, &newsize);
	    if (ret)
		return ret;
	    p -= newsize; len -= newsize;

	    break;
	}
	case A1_OP_SETOF: {
	    const struct template_of *el = DPOC(data, t->offset);
	    size_t ellen = sizeofType(t->ptr);
	    struct heim_octet_string *val;
	    unsigned char *elptr = el->val;
	    size_t i, totallen;

	    if (el->len == 0)
		break;

	    if (el->len > UINT_MAX/sizeof(val[0]))
		return ERANGE;

	    val = malloc(sizeof(val[0]) * el->len);
	    if (val == NULL)
		return ENOMEM;

	    for(totallen = 0, i = 0; i < el->len; i++) {
		unsigned char *next;
		size_t l;

		val[i].length = _asn1_length(t->ptr, elptr);
		val[i].data = malloc(val[i].length);

		ret = _asn1_encode(t->ptr, DPO(val[i].data, val[i].length - 1),
				   val[i].length, elptr, &l);
		if (ret)
		    break;

		next = elptr + ellen;
		if (next < elptr) {
		    ret = ASN1_OVERFLOW;
		    break;
		}
		elptr = next;
		totallen += val[i].length;
	    }
	    if (ret == 0 && totallen > len)
		ret = ASN1_OVERFLOW;
	    if (ret) {
		do {
		    free(val[i].data);
		} while(i-- > 0);
		free(val);
		return ret;
	    }

	    len -= totallen;

	    qsort(val, el->len, sizeof(val[0]), _heim_der_set_sort);

	    i = el->len - 1;
	    do {
		p -= val[i].length;
		memcpy(p + 1, val[i].data, val[i].length);
		free(val[i].data);
	    } while(i-- > 0);
	    free(val);

	    break;

	}
	case A1_OP_SEQOF: {
	    struct template_of *el = DPO(data, t->offset);
	    size_t ellen = sizeofType(t->ptr);
	    size_t newsize;
	    unsigned int i;
	    unsigned char *elptr = el->val;

	    if (el->len == 0)
		break;

	    elptr += ellen * (el->len - 1);

	    for (i = 0; i < el->len; i++) {
		ret = _asn1_encode(t->ptr, p, len,
				   elptr,
				   &newsize);
		if (ret)
		    return ret;
		p -= newsize; len -= newsize;
		elptr -= ellen;
	    }

	    break;
	}
	case A1_OP_BMEMBER: {
	    const struct asn1_template *bmember = t->ptr;
	    size_t size = bmember->offset;
	    size_t elements = A1_HEADER_LEN(bmember);
	    size_t pos;
	    unsigned char c = 0;
	    unsigned int bitset = 0;
	    int rfc1510 = (bmember->tt & A1_HBF_RFC1510);

	    bmember += elements;

	    if (rfc1510)
		pos = 31;
	    else
		pos = bmember->offset;

	    while (elements && len) {
		while (bmember->offset / 8 < pos / 8) {
		    if (rfc1510 || bitset || c) {
			if (len < 1)
			    return ASN1_OVERFLOW;
			*p-- = c; len--;
		    }
		    c = 0;
		    pos -= 8;
		}
		bmember_put_bit(&c, data, bmember->offset, size, &bitset);
		elements--; bmember--;
	    }
	    if (rfc1510 || bitset) {
		if (len < 1)
		    return ASN1_OVERFLOW;
		*p-- = c; len--;
	    }

	    if (len < 1)
		return ASN1_OVERFLOW;
	    if (rfc1510 || bitset == 0)
		*p-- = 0;
	    else
		*p-- = bitset - 1;

	    len--;

	    break;
	}
	case A1_OP_CHOICE: {
	    const struct asn1_template *choice = t->ptr;
	    const unsigned int *element = DPOC(data, choice->offset);
	    size_t datalen;
	    const void *el;

	    if (*element > A1_HEADER_LEN(choice)) {
		printf("element: %d\n", *element);
		return ASN1_PARSE_ERROR;
	    }

	    if (*element == 0) {
		ret += der_put_octet_string(p, len,
					    DPOC(data, choice->tt), &datalen);
	    } else {
		choice += *element;
		el = DPOC(data, choice->offset);
		ret = _asn1_encode(choice->ptr, p, len, el, &datalen);
		if (ret)
		    return ret;
	    }
	    len -= datalen; p -= datalen;

	    break;
	}
	default:
	    ABORT_ON_ERROR();
	}
	t--;
	elements--;
    }
    if (size)
	*size = oldlen - len;

    return 0;
}

size_t
_asn1_length(const struct asn1_template *t, const void *data)
{
    size_t elements = A1_HEADER_LEN(t);
    size_t ret = 0;

    t += A1_HEADER_LEN(t);

    while (elements) {
	switch (t->tt & A1_OP_MASK) {
	case A1_OP_TYPE:
	case A1_OP_TYPE_EXTERN: {
	    const void *el = DPOC(data, t->offset);

	    if (t->tt & A1_FLAG_OPTIONAL) {
		void **pel = (void **)el;
		if (*pel == NULL)
		    break;
		el = *pel;
	    }

	    if ((t->tt & A1_OP_MASK) == A1_OP_TYPE) {
		ret += _asn1_length(t->ptr, el);
	    } else {
		const struct asn1_type_func *f = t->ptr;
		ret += (f->length)(el);
	    }
	    break;
	}
	case A1_OP_TAG: {
	    size_t datalen;
	    const void *olddata = data;

	    data = DPO(data, t->offset);

	    if (t->tt & A1_FLAG_OPTIONAL) {
		void **el = (void **)data;
		if (*el == NULL) {
		    data = olddata;
		    break;
		}
		data = *el;
	    }
	    datalen = _asn1_length(t->ptr, data);
	    ret += der_length_tag(A1_TAG_TAG(t->tt)) + der_length_len(datalen);
	    ret += datalen;
	    data = olddata;
	    break;
	}
	case A1_OP_PARSE: {
	    unsigned int type = A1_PARSE_TYPE(t->tt);
	    const void *el = DPOC(data, t->offset);

	    if (type > sizeof(prim)/sizeof(prim[0])) {
		ABORT_ON_ERROR();
		break;
	    }
	    ret += (prim[type].length)(el);
	    break;
	}
	case A1_OP_SETOF:
	case A1_OP_SEQOF: {
	    const struct template_of *el = DPOC(data, t->offset);
	    size_t ellen = sizeofType(t->ptr);
	    const unsigned char *element = el->val;
	    unsigned int i;

	    for (i = 0; i < el->len; i++) {
		ret += _asn1_length(t->ptr, element);
		element += ellen;
	    }

	    break;
	}
	case A1_OP_BMEMBER: {
	    const struct asn1_template *bmember = t->ptr;
	    size_t size = bmember->offset;
	    size_t elements = A1_HEADER_LEN(bmember);
	    int rfc1510 = (bmember->tt & A1_HBF_RFC1510);

	    if (rfc1510) {
		ret += 5;
	    } else {

		ret += 1;

		bmember += elements;

		while (elements) {
		    if (bmember_isset_bit(data, bmember->offset, size)) {
			ret += (bmember->offset / 8) + 1;
			break;
		    }
		    elements--; bmember--;
		}
	    }
	    break;
	}
	case A1_OP_CHOICE: {
	    const struct asn1_template *choice = t->ptr;
	    const unsigned int *element = DPOC(data, choice->offset);

	    if (*element > A1_HEADER_LEN(choice))
		break;

	    if (*element == 0) {
		ret += der_length_octet_string(DPOC(data, choice->tt));
	    } else {
		choice += *element;
		ret += _asn1_length(choice->ptr, DPOC(data, choice->offset));
	    }
	    break;
	}
	default:
	    ABORT_ON_ERROR();
	    break;
	}
	elements--;
	t--;
    }
    return ret;
}

void
_asn1_free(const struct asn1_template *t, void *data)
{
    size_t elements = A1_HEADER_LEN(t);

    if (t->tt & A1_HF_PRESERVE)
	der_free_octet_string(data);

    t++;

    while (elements) {
	switch (t->tt & A1_OP_MASK) {
	case A1_OP_TYPE:
	case A1_OP_TYPE_EXTERN: {
	    void *el = DPO(data, t->offset);

	    if (t->tt & A1_FLAG_OPTIONAL) {
		void **pel = (void **)el;
		if (*pel == NULL)
		    break;
		el = *pel;
	    }

	    if ((t->tt & A1_OP_MASK) == A1_OP_TYPE) {
		_asn1_free(t->ptr, el);
	    } else {
		const struct asn1_type_func *f = t->ptr;
		(f->release)(el);
	    }
	    if (t->tt & A1_FLAG_OPTIONAL)
		free(el);

	    break;
	}
	case A1_OP_PARSE: {
	    unsigned int type = A1_PARSE_TYPE(t->tt);
	    void *el = DPO(data, t->offset);

	    if (type > sizeof(prim)/sizeof(prim[0])) {
		ABORT_ON_ERROR();
		break;
	    }
	    (prim[type].release)(el);
	    break;
	}
	case A1_OP_TAG: {
	    void *el = DPO(data, t->offset);

	    if (t->tt & A1_FLAG_OPTIONAL) {
		void **pel = (void **)el;
		if (*pel == NULL)
		    break;
		el = *pel;
	    }

	    _asn1_free(t->ptr, el);

	    if (t->tt & A1_FLAG_OPTIONAL)
		free(el);

	    break;
	}
	case A1_OP_SETOF:
	case A1_OP_SEQOF: {
	    struct template_of *el = DPO(data, t->offset);
	    size_t ellen = sizeofType(t->ptr);
	    unsigned char *element = el->val;
	    unsigned int i;

	    for (i = 0; i < el->len; i++) {
		_asn1_free(t->ptr, element);
		element += ellen;
	    }
	    free(el->val);
	    el->val = NULL;
	    el->len = 0;

	    break;
	}
	case A1_OP_BMEMBER:
	    break;
	case A1_OP_CHOICE: {
	    const struct asn1_template *choice = t->ptr;
	    const unsigned int *element = DPOC(data, choice->offset);

	    if (*element > A1_HEADER_LEN(choice))
		break;

	    if (*element == 0) {
		der_free_octet_string(DPO(data, choice->tt));
	    } else {
		choice += *element;
		_asn1_free(choice->ptr, DPO(data, choice->offset));
	    }
	    break;
	}
	default:
	    ABORT_ON_ERROR();
	    break;
	}
	t++;
	elements--;
    }
}

int
_asn1_copy(const struct asn1_template *t, const void *from, void *to)
{
    size_t elements = A1_HEADER_LEN(t);
    int ret = 0;
    int preserve = (t->tt & A1_HF_PRESERVE);

    t++;

    if (preserve) {
	ret = der_copy_octet_string(from, to);
	if (ret)
	    return ret;
    }

    while (elements) {
	switch (t->tt & A1_OP_MASK) {
	case A1_OP_TYPE:
	case A1_OP_TYPE_EXTERN: {
	    const void *fel = DPOC(from, t->offset);
	    void *tel = DPO(to, t->offset);
	    void **ptel = (void **)tel;
	    size_t size;

	    if ((t->tt & A1_OP_MASK) == A1_OP_TYPE) {
		size = sizeofType(t->ptr);
	    } else {
		const struct asn1_type_func *f = t->ptr;
		size = f->size;
	    }

	    if (t->tt & A1_FLAG_OPTIONAL) {
		void **pfel = (void **)fel;
		if (*pfel == NULL)
		    break;
		fel = *pfel;

		tel = *ptel = calloc(1, size);
		if (tel == NULL)
		    return ENOMEM;
	    }

	    if ((t->tt & A1_OP_MASK) == A1_OP_TYPE) {
		ret = _asn1_copy(t->ptr, fel, tel);
	    } else {
		const struct asn1_type_func *f = t->ptr;
		ret = (f->copy)(fel, tel);
	    }

	    if (ret) {
		if (t->tt & A1_FLAG_OPTIONAL) {
		    free(*ptel);
		    *ptel = NULL;
		}
		return ret;
	    }
	    break;
	}
	case A1_OP_PARSE: {
	    unsigned int type = A1_PARSE_TYPE(t->tt);
	    const void *fel = DPOC(from, t->offset);
	    void *tel = DPO(to, t->offset);

	    if (type > sizeof(prim)/sizeof(prim[0])) {
		ABORT_ON_ERROR();
		return ASN1_PARSE_ERROR;
	    }
	    ret = (prim[type].copy)(fel, tel);
	    if (ret)
		return ret;
	    break;
	}
	case A1_OP_TAG: {
	    const void *oldfrom = from;
	    void *oldto = to;
	    void **tel = NULL;

	    from = DPOC(from, t->offset);
	    to = DPO(to, t->offset);

	    if (t->tt & A1_FLAG_OPTIONAL) {
		void **fel = (void **)from;
		tel = (void **)to;
		if (*fel == NULL) {
		    from = oldfrom;
		    to = oldto;
		    break;
		}
		from = *fel;

		to = *tel = calloc(1, sizeofType(t->ptr));
		if (to == NULL)
		    return ENOMEM;
	    }

	    ret = _asn1_copy(t->ptr, from, to);
	    if (ret) {
		if (t->tt & A1_FLAG_OPTIONAL) {
		    free(*tel);
		    *tel = NULL;
		}
		return ret;
	    }

	    from = oldfrom;
	    to = oldto;

	    break;
	}
	case A1_OP_SETOF:
	case A1_OP_SEQOF: {
	    const struct template_of *fel = DPOC(from, t->offset);
	    struct template_of *tel = DPO(to, t->offset);
	    size_t ellen = sizeofType(t->ptr);
	    unsigned int i;

	    tel->val = calloc(fel->len, ellen);
	    if (tel->val == NULL)
		return ENOMEM;

	    tel->len = fel->len;

	    for (i = 0; i < fel->len; i++) {
		ret = _asn1_copy(t->ptr,
				 DPOC(fel->val, (i * ellen)),
				 DPO(tel->val, (i *ellen)));
		if (ret)
		    return ret;
	    }
	    break;
	}
	case A1_OP_BMEMBER: {
	    const struct asn1_template *bmember = t->ptr;
	    size_t size = bmember->offset;
	    memcpy(to, from, size);
	    break;
	}
	case A1_OP_CHOICE: {
	    const struct asn1_template *choice = t->ptr;
	    const unsigned int *felement = DPOC(from, choice->offset);
	    unsigned int *telement = DPO(to, choice->offset);

	    if (*felement > A1_HEADER_LEN(choice))
		return ASN1_PARSE_ERROR;

	    *telement = *felement;

	    if (*felement == 0) {
		ret = der_copy_octet_string(DPOC(from, choice->tt), DPO(to, choice->tt));
	    } else {
		choice += *felement;
		ret = _asn1_copy(choice->ptr,
				 DPOC(from, choice->offset),
				 DPO(to, choice->offset));
	    }
	    if (ret)
		return ret;
	    break;
	}
	default:
	    ABORT_ON_ERROR();
	    break;
	}
	t++;
	elements--;
    }
    return 0;
}

int
_asn1_decode_top(const struct asn1_template *t, unsigned flags, const unsigned char *p, size_t len, void *data, size_t *size)
{
    int ret;
    memset(data, 0, t->offset);
    ret = _asn1_decode(t, flags, p, len, data, size);
    if (ret) {
	_asn1_free(t, data);
	memset(data, 0, t->offset);
    }

    return ret;
}

int
_asn1_copy_top(const struct asn1_template *t, const void *from, void *to)
{
    int ret;
    memset(to, 0, t->offset);
    ret = _asn1_copy(t, from, to);
    if (ret) {
	_asn1_free(t, to);
	memset(to, 0, t->offset);
    }
    return ret;
}
