/*
 * Copyright (c) 2006 - 2007 Kungliga Tekniska Högskolan
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

#include "krb5_locl.h"
#include <wind.h>

struct PAC_INFO_BUFFER {
    uint32_t type;
    uint32_t buffersize;
    uint32_t offset_hi;
    uint32_t offset_lo;
};

struct PACTYPE {
    uint32_t numbuffers;
    uint32_t version;
    struct PAC_INFO_BUFFER buffers[1];
};

struct krb5_pac_data {
    struct PACTYPE *pac;
    krb5_data data;
    struct PAC_INFO_BUFFER *server_checksum;
    struct PAC_INFO_BUFFER *privsvr_checksum;
    struct PAC_INFO_BUFFER *logon_name;
};

#define PAC_ALIGNMENT			8

#define PACTYPE_SIZE			8
#define PAC_INFO_BUFFER_SIZE		16

#define PAC_SERVER_CHECKSUM		6
#define PAC_PRIVSVR_CHECKSUM		7
#define PAC_LOGON_NAME			10
#define PAC_CONSTRAINED_DELEGATION	11

#define CHECK(r,f,l)						\
	do {							\
		if (((r) = f ) != 0) {				\
			krb5_clear_error_message(context);	\
			goto l;					\
		}						\
	} while(0)

static const char zeros[PAC_ALIGNMENT] = { 0 };

/*
 * HMAC-MD5 checksum over any key (needed for the PAC routines)
 */

static krb5_error_code
HMAC_MD5_any_checksum(krb5_context context,
		      const krb5_keyblock *key,
		      const void *data,
		      size_t len,
		      unsigned usage,
		      Checksum *result)
{
    struct _krb5_key_data local_key;
    krb5_error_code ret;

    memset(&local_key, 0, sizeof(local_key));

    ret = krb5_copy_keyblock(context, key, &local_key.key);
    if (ret)
	return ret;

    ret = krb5_data_alloc (&result->checksum, 16);
    if (ret) {
	krb5_free_keyblock(context, local_key.key);
	return ret;
    }

    result->cksumtype = CKSUMTYPE_HMAC_MD5;
    ret = _krb5_HMAC_MD5_checksum(context, &local_key, data, len, usage, result);
    if (ret)
	krb5_data_free(&result->checksum);

    krb5_free_keyblock(context, local_key.key);
    return ret;
}


/*
 *
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pac_parse(krb5_context context, const void *ptr, size_t len,
	       krb5_pac *pac)
{
    krb5_error_code ret;
    krb5_pac p;
    krb5_storage *sp = NULL;
    uint32_t i, tmp, tmp2, header_end;

    p = calloc(1, sizeof(*p));
    if (p == NULL) {
	ret = krb5_enomem(context);
	goto out;
    }

    sp = krb5_storage_from_readonly_mem(ptr, len);
    if (sp == NULL) {
	ret = krb5_enomem(context);
	goto out;
    }
    krb5_storage_set_flags(sp, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(ret, krb5_ret_uint32(sp, &tmp), out);
    CHECK(ret, krb5_ret_uint32(sp, &tmp2), out);
    if (tmp < 1) {
	ret = EINVAL; /* Too few buffers */
	krb5_set_error_message(context, ret, N_("PAC have too few buffer", ""));
	goto out;
    }
    if (tmp2 != 0) {
	ret = EINVAL; /* Wrong version */
	krb5_set_error_message(context, ret,
			       N_("PAC have wrong version %d", ""),
			       (int)tmp2);
	goto out;
    }

    p->pac = calloc(1,
		    sizeof(*p->pac) + (sizeof(p->pac->buffers[0]) * (tmp - 1)));
    if (p->pac == NULL) {
	ret = krb5_enomem(context);
	goto out;
    }

    p->pac->numbuffers = tmp;
    p->pac->version = tmp2;

    header_end = PACTYPE_SIZE + (PAC_INFO_BUFFER_SIZE * p->pac->numbuffers);
    if (header_end > len) {
	ret = EINVAL;
	goto out;
    }

    for (i = 0; i < p->pac->numbuffers; i++) {
	CHECK(ret, krb5_ret_uint32(sp, &p->pac->buffers[i].type), out);
	CHECK(ret, krb5_ret_uint32(sp, &p->pac->buffers[i].buffersize), out);
	CHECK(ret, krb5_ret_uint32(sp, &p->pac->buffers[i].offset_lo), out);
	CHECK(ret, krb5_ret_uint32(sp, &p->pac->buffers[i].offset_hi), out);

	/* consistency checks */
	if (p->pac->buffers[i].offset_lo & (PAC_ALIGNMENT - 1)) {
	    ret = EINVAL;
	    krb5_set_error_message(context, ret,
				   N_("PAC out of allignment", ""));
	    goto out;
	}
	if (p->pac->buffers[i].offset_hi) {
	    ret = EINVAL;
	    krb5_set_error_message(context, ret,
				   N_("PAC high offset set", ""));
	    goto out;
	}
	if (p->pac->buffers[i].offset_lo > len) {
	    ret = EINVAL;
	    krb5_set_error_message(context, ret,
				   N_("PAC offset off end", ""));
	    goto out;
	}
	if (p->pac->buffers[i].offset_lo < header_end) {
	    ret = EINVAL;
	    krb5_set_error_message(context, ret,
				   N_("PAC offset inside header: %lu %lu", ""),
				   (unsigned long)p->pac->buffers[i].offset_lo,
				   (unsigned long)header_end);
	    goto out;
	}
	if (p->pac->buffers[i].buffersize > len - p->pac->buffers[i].offset_lo){
	    ret = EINVAL;
	    krb5_set_error_message(context, ret, N_("PAC length off end", ""));
	    goto out;
	}

	/* let save pointer to data we need later */
	if (p->pac->buffers[i].type == PAC_SERVER_CHECKSUM) {
	    if (p->server_checksum) {
		ret = EINVAL;
		krb5_set_error_message(context, ret,
				       N_("PAC have two server checksums", ""));
		goto out;
	    }
	    p->server_checksum = &p->pac->buffers[i];
	} else if (p->pac->buffers[i].type == PAC_PRIVSVR_CHECKSUM) {
	    if (p->privsvr_checksum) {
		ret = EINVAL;
		krb5_set_error_message(context, ret,
				       N_("PAC have two KDC checksums", ""));
		goto out;
	    }
	    p->privsvr_checksum = &p->pac->buffers[i];
	} else if (p->pac->buffers[i].type == PAC_LOGON_NAME) {
	    if (p->logon_name) {
		ret = EINVAL;
		krb5_set_error_message(context, ret,
				       N_("PAC have two logon names", ""));
		goto out;
	    }
	    p->logon_name = &p->pac->buffers[i];
	}
    }

    ret = krb5_data_copy(&p->data, ptr, len);
    if (ret)
	goto out;

    krb5_storage_free(sp);

    *pac = p;
    return 0;

out:
    if (sp)
	krb5_storage_free(sp);
    if (p) {
	if (p->pac)
	    free(p->pac);
	free(p);
    }
    *pac = NULL;

    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pac_init(krb5_context context, krb5_pac *pac)
{
    krb5_error_code ret;
    krb5_pac p;

    p = calloc(1, sizeof(*p));
    if (p == NULL) {
	return krb5_enomem(context);
    }

    p->pac = calloc(1, sizeof(*p->pac));
    if (p->pac == NULL) {
	free(p);
	return krb5_enomem(context);
    }

    ret = krb5_data_alloc(&p->data, PACTYPE_SIZE);
    if (ret) {
	free (p->pac);
	free(p);
	return krb5_enomem(context);
    }

    *pac = p;
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pac_add_buffer(krb5_context context, krb5_pac p,
		    uint32_t type, const krb5_data *data)
{
    krb5_error_code ret;
    void *ptr;
    size_t len, offset, header_end, old_end;
    uint32_t i;

    len = p->pac->numbuffers;

    ptr = realloc(p->pac,
		  sizeof(*p->pac) + (sizeof(p->pac->buffers[0]) * len));
    if (ptr == NULL)
	return krb5_enomem(context);

    p->pac = ptr;

    for (i = 0; i < len; i++)
	p->pac->buffers[i].offset_lo += PAC_INFO_BUFFER_SIZE;

    offset = p->data.length + PAC_INFO_BUFFER_SIZE;

    p->pac->buffers[len].type = type;
    p->pac->buffers[len].buffersize = data->length;
    p->pac->buffers[len].offset_lo = offset;
    p->pac->buffers[len].offset_hi = 0;

    old_end = p->data.length;
    len = p->data.length + data->length + PAC_INFO_BUFFER_SIZE;
    if (len < p->data.length) {
	krb5_set_error_message(context, EINVAL, "integer overrun");
	return EINVAL;
    }

    /* align to PAC_ALIGNMENT */
    len = ((len + PAC_ALIGNMENT - 1) / PAC_ALIGNMENT) * PAC_ALIGNMENT;

    ret = krb5_data_realloc(&p->data, len);
    if (ret) {
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	return ret;
    }

    /*
     * make place for new PAC INFO BUFFER header
     */
    header_end = PACTYPE_SIZE + (PAC_INFO_BUFFER_SIZE * p->pac->numbuffers);
    memmove((unsigned char *)p->data.data + header_end + PAC_INFO_BUFFER_SIZE,
	    (unsigned char *)p->data.data + header_end ,
	    old_end - header_end);
    memset((unsigned char *)p->data.data + header_end, 0, PAC_INFO_BUFFER_SIZE);

    /*
     * copy in new data part
     */

    memcpy((unsigned char *)p->data.data + offset,
	   data->data, data->length);
    memset((unsigned char *)p->data.data + offset + data->length,
	   0, p->data.length - offset - data->length);

    p->pac->numbuffers += 1;

    return 0;
}

/**
 * Get the PAC buffer of specific type from the pac.
 *
 * @param context Kerberos 5 context.
 * @param p the pac structure returned by krb5_pac_parse().
 * @param type type of buffer to get
 * @param data return data, free with krb5_data_free().
 *
 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5_pac
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pac_get_buffer(krb5_context context, krb5_pac p,
		    uint32_t type, krb5_data *data)
{
    krb5_error_code ret;
    uint32_t i;

    for (i = 0; i < p->pac->numbuffers; i++) {
	const size_t len = p->pac->buffers[i].buffersize;
	const size_t offset = p->pac->buffers[i].offset_lo;

	if (p->pac->buffers[i].type != type)
	    continue;

	ret = krb5_data_copy(data, (unsigned char *)p->data.data + offset, len);
	if (ret) {
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    return ret;
	}
	return 0;
    }
    krb5_set_error_message(context, ENOENT, "No PAC buffer of type %lu was found",
			   (unsigned long)type);
    return ENOENT;
}

/*
 *
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pac_get_types(krb5_context context,
		   krb5_pac p,
		   size_t *len,
		   uint32_t **types)
{
    size_t i;

    *types = calloc(p->pac->numbuffers, sizeof(*types));
    if (*types == NULL) {
	*len = 0;
	return krb5_enomem(context);
    }
    for (i = 0; i < p->pac->numbuffers; i++)
	(*types)[i] = p->pac->buffers[i].type;
    *len = p->pac->numbuffers;

    return 0;
}

/*
 *
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_pac_free(krb5_context context, krb5_pac pac)
{
    krb5_data_free(&pac->data);
    free(pac->pac);
    free(pac);
}

/*
 *
 */

static krb5_error_code
verify_checksum(krb5_context context,
		const struct PAC_INFO_BUFFER *sig,
		const krb5_data *data,
		void *ptr, size_t len,
		const krb5_keyblock *key)
{
    krb5_storage *sp = NULL;
    uint32_t type;
    krb5_error_code ret;
    Checksum cksum;

    memset(&cksum, 0, sizeof(cksum));

    sp = krb5_storage_from_mem((char *)data->data + sig->offset_lo,
			       sig->buffersize);
    if (sp == NULL)
	return krb5_enomem(context);

    krb5_storage_set_flags(sp, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(ret, krb5_ret_uint32(sp, &type), out);
    cksum.cksumtype = type;
    cksum.checksum.length =
	sig->buffersize - krb5_storage_seek(sp, 0, SEEK_CUR);
    cksum.checksum.data = malloc(cksum.checksum.length);
    if (cksum.checksum.data == NULL) {
	ret = krb5_enomem(context);
	goto out;
    }
    ret = krb5_storage_read(sp, cksum.checksum.data, cksum.checksum.length);
    if (ret != (int)cksum.checksum.length) {
	ret = EINVAL;
	krb5_set_error_message(context, ret, "PAC checksum missing checksum");
	goto out;
    }

    if (!krb5_checksum_is_keyed(context, cksum.cksumtype)) {
	ret = EINVAL;
	krb5_set_error_message(context, ret, "Checksum type %d not keyed",
			       cksum.cksumtype);
	goto out;
    }

    /* If the checksum is HMAC-MD5, the checksum type is not tied to
     * the key type, instead the HMAC-MD5 checksum is applied blindly
     * on whatever key is used for this connection, avoiding issues
     * with unkeyed checksums on des-cbc-md5 and des-cbc-crc.  See
     * http://comments.gmane.org/gmane.comp.encryption.kerberos.devel/8743
     * for the same issue in MIT, and
     * http://blogs.msdn.com/b/openspecification/archive/2010/01/01/verifying-the-server-signature-in-kerberos-privilege-account-certificate.aspx
     * for Microsoft's explaination */

    if (cksum.cksumtype == CKSUMTYPE_HMAC_MD5) {
	Checksum local_checksum;

	memset(&local_checksum, 0, sizeof(local_checksum));

	ret = HMAC_MD5_any_checksum(context, key, ptr, len,
				    KRB5_KU_OTHER_CKSUM, &local_checksum);

	if (ret != 0 || krb5_data_ct_cmp(&local_checksum.checksum, &cksum.checksum) != 0) {
	    ret = KRB5KRB_AP_ERR_BAD_INTEGRITY;
	    krb5_set_error_message(context, ret,
				   N_("PAC integrity check failed for "
				      "hmac-md5 checksum", ""));
	}
	krb5_data_free(&local_checksum.checksum);

   } else {
	krb5_crypto crypto = NULL;

	ret = krb5_crypto_init(context, key, 0, &crypto);
	if (ret)
		goto out;

	ret = krb5_verify_checksum(context, crypto, KRB5_KU_OTHER_CKSUM,
				   ptr, len, &cksum);
	krb5_crypto_destroy(context, crypto);
    }
    free(cksum.checksum.data);
    krb5_storage_free(sp);

    return ret;

out:
    if (cksum.checksum.data)
	free(cksum.checksum.data);
    if (sp)
	krb5_storage_free(sp);
    return ret;
}

static krb5_error_code
create_checksum(krb5_context context,
		const krb5_keyblock *key,
		uint32_t cksumtype,
		void *data, size_t datalen,
		void *sig, size_t siglen)
{
    krb5_crypto crypto = NULL;
    krb5_error_code ret;
    Checksum cksum;

    /* If the checksum is HMAC-MD5, the checksum type is not tied to
     * the key type, instead the HMAC-MD5 checksum is applied blindly
     * on whatever key is used for this connection, avoiding issues
     * with unkeyed checksums on des-cbc-md5 and des-cbc-crc.  See
     * http://comments.gmane.org/gmane.comp.encryption.kerberos.devel/8743
     * for the same issue in MIT, and
     * http://blogs.msdn.com/b/openspecification/archive/2010/01/01/verifying-the-server-signature-in-kerberos-privilege-account-certificate.aspx
     * for Microsoft's explaination */

    if (cksumtype == (uint32_t)CKSUMTYPE_HMAC_MD5) {
	ret = HMAC_MD5_any_checksum(context, key, data, datalen,
				    KRB5_KU_OTHER_CKSUM, &cksum);
    } else {
	ret = krb5_crypto_init(context, key, 0, &crypto);
	if (ret)
	    return ret;

	ret = krb5_create_checksum(context, crypto, KRB5_KU_OTHER_CKSUM, 0,
				   data, datalen, &cksum);
	krb5_crypto_destroy(context, crypto);
	if (ret)
	    return ret;
    }
    if (cksum.checksum.length != siglen) {
	krb5_set_error_message(context, EINVAL, "pac checksum wrong length");
	free_Checksum(&cksum);
	return EINVAL;
    }

    memcpy(sig, cksum.checksum.data, siglen);
    free_Checksum(&cksum);

    return 0;
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

static krb5_error_code
verify_logonname(krb5_context context,
		 const struct PAC_INFO_BUFFER *logon_name,
		 const krb5_data *data,
		 time_t authtime,
		 krb5_const_principal principal)
{
    krb5_error_code ret;
    krb5_principal p2;
    uint32_t time1, time2;
    krb5_storage *sp;
    uint16_t len;
    char *s;

    sp = krb5_storage_from_readonly_mem((const char *)data->data + logon_name->offset_lo,
					logon_name->buffersize);
    if (sp == NULL)
	return krb5_enomem(context);

    krb5_storage_set_flags(sp, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(ret, krb5_ret_uint32(sp, &time1), out);
    CHECK(ret, krb5_ret_uint32(sp, &time2), out);

    {
	uint64_t t1, t2;
	t1 = unix2nttime(authtime);
	t2 = ((uint64_t)time2 << 32) | time1;
	if (t1 != t2) {
	    krb5_storage_free(sp);
	    krb5_set_error_message(context, EINVAL, "PAC timestamp mismatch");
	    return EINVAL;
	}
    }
    CHECK(ret, krb5_ret_uint16(sp, &len), out);
    if (len == 0) {
	krb5_storage_free(sp);
	krb5_set_error_message(context, EINVAL, "PAC logon name length missing");
	return EINVAL;
    }

    s = malloc(len);
    if (s == NULL) {
	krb5_storage_free(sp);
	return krb5_enomem(context);
    }
    ret = krb5_storage_read(sp, s, len);
    if (ret != len) {
	krb5_storage_free(sp);
	krb5_set_error_message(context, EINVAL, "Failed to read PAC logon name");
	return EINVAL;
    }
    krb5_storage_free(sp);
    {
	size_t ucs2len = len / 2;
	uint16_t *ucs2;
	size_t u8len;
	unsigned int flags = WIND_RW_LE;

	ucs2 = malloc(sizeof(ucs2[0]) * ucs2len);
	if (ucs2 == NULL)
	    return krb5_enomem(context);

	ret = wind_ucs2read(s, len, &flags, ucs2, &ucs2len);
	free(s);
	if (ret) {
	    free(ucs2);
	    krb5_set_error_message(context, ret, "Failed to convert string to UCS-2");
	    return ret;
	}
	ret = wind_ucs2utf8_length(ucs2, ucs2len, &u8len);
	if (ret) {
	    free(ucs2);
	    krb5_set_error_message(context, ret, "Failed to count length of UCS-2 string");
	    return ret;
	}
	u8len += 1; /* Add space for NUL */
	s = malloc(u8len);
	if (s == NULL) {
	    free(ucs2);
	    return krb5_enomem(context);
	}
	ret = wind_ucs2utf8(ucs2, ucs2len, s, &u8len);
	free(ucs2);
	if (ret) {
	    free(s);
	    krb5_set_error_message(context, ret, "Failed to convert to UTF-8");
	    return ret;
	}
    }
    ret = krb5_parse_name_flags(context, s, KRB5_PRINCIPAL_PARSE_NO_REALM, &p2);
    free(s);
    if (ret)
	return ret;

    if (krb5_principal_compare_any_realm(context, principal, p2) != TRUE) {
	ret = EINVAL;
	krb5_set_error_message(context, ret, "PAC logon name mismatch");
    }
    krb5_free_principal(context, p2);
    return ret;
out:
    return ret;
}

/*
 *
 */

static krb5_error_code
build_logon_name(krb5_context context,
		 time_t authtime,
		 krb5_const_principal principal,
		 krb5_data *logon)
{
    krb5_error_code ret;
    krb5_storage *sp;
    uint64_t t;
    char *s, *s2;
    size_t s2_len;

    t = unix2nttime(authtime);

    krb5_data_zero(logon);

    sp = krb5_storage_emem();
    if (sp == NULL)
	return krb5_enomem(context);

    krb5_storage_set_flags(sp, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(ret, krb5_store_uint32(sp, t & 0xffffffff), out);
    CHECK(ret, krb5_store_uint32(sp, t >> 32), out);

    ret = krb5_unparse_name_flags(context, principal,
				  KRB5_PRINCIPAL_UNPARSE_NO_REALM, &s);
    if (ret)
	goto out;

    {
	size_t ucs2_len;
	uint16_t *ucs2;
	unsigned int flags;

	ret = wind_utf8ucs2_length(s, &ucs2_len);
	if (ret) {
	    free(s);
	    krb5_set_error_message(context, ret, "Failed to count length of UTF-8 string");
	    return ret;
	}

	ucs2 = malloc(sizeof(ucs2[0]) * ucs2_len);
	if (ucs2 == NULL) {
	    free(s);
	    return krb5_enomem(context);
	}

	ret = wind_utf8ucs2(s, ucs2, &ucs2_len);
	free(s);
	if (ret) {
	    free(ucs2);
	    krb5_set_error_message(context, ret, "Failed to convert string to UCS-2");
	    return ret;
	}

	s2_len = (ucs2_len + 1) * 2;
	s2 = malloc(s2_len);
	if (ucs2 == NULL) {
	    free(ucs2);
	    return krb5_enomem(context);
	}

	flags = WIND_RW_LE;
	ret = wind_ucs2write(ucs2, ucs2_len,
			     &flags, s2, &s2_len);
	free(ucs2);
	if (ret) {
	    free(s2);
	    krb5_set_error_message(context, ret, "Failed to write to UCS-2 buffer");
	    return ret;
	}

	/*
	 * we do not want zero termination
	 */
	s2_len = ucs2_len * 2;
    }

    CHECK(ret, krb5_store_uint16(sp, s2_len), out);

    ret = krb5_storage_write(sp, s2, s2_len);
    free(s2);
    if (ret != (int)s2_len) {
	ret = krb5_enomem(context);
	goto out;
    }
    ret = krb5_storage_to_data(sp, logon);
    if (ret)
	goto out;
    krb5_storage_free(sp);

    return 0;
out:
    krb5_storage_free(sp);
    return ret;
}


/**
 * Verify the PAC.
 *
 * @param context Kerberos 5 context.
 * @param pac the pac structure returned by krb5_pac_parse().
 * @param authtime The time of the ticket the PAC belongs to.
 * @param principal the principal to verify.
 * @param server The service key, most always be given.
 * @param privsvr The KDC key, may be given.

 * @return Returns 0 to indicate success. Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5_pac
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_pac_verify(krb5_context context,
		const krb5_pac pac,
		time_t authtime,
		krb5_const_principal principal,
		const krb5_keyblock *server,
		const krb5_keyblock *privsvr)
{
    krb5_error_code ret;

    if (pac->server_checksum == NULL) {
	krb5_set_error_message(context, EINVAL, "PAC missing server checksum");
	return EINVAL;
    }
    if (pac->privsvr_checksum == NULL) {
	krb5_set_error_message(context, EINVAL, "PAC missing kdc checksum");
	return EINVAL;
    }
    if (pac->logon_name == NULL) {
	krb5_set_error_message(context, EINVAL, "PAC missing logon name");
	return EINVAL;
    }

    ret = verify_logonname(context,
			   pac->logon_name,
			   &pac->data,
			   authtime,
			   principal);
    if (ret)
	return ret;

    /*
     * in the service case, clean out data option of the privsvr and
     * server checksum before checking the checksum.
     */
    {
	krb5_data *copy;

	ret = krb5_copy_data(context, &pac->data, &copy);
	if (ret)
	    return ret;

	if (pac->server_checksum->buffersize < 4)
	    return EINVAL;
	if (pac->privsvr_checksum->buffersize < 4)
	    return EINVAL;

	memset((char *)copy->data + pac->server_checksum->offset_lo + 4,
	       0,
	       pac->server_checksum->buffersize - 4);

	memset((char *)copy->data + pac->privsvr_checksum->offset_lo + 4,
	       0,
	       pac->privsvr_checksum->buffersize - 4);

	ret = verify_checksum(context,
			      pac->server_checksum,
			      &pac->data,
			      copy->data,
			      copy->length,
			      server);
	krb5_free_data(context, copy);
	if (ret)
	    return ret;
    }
    if (privsvr) {
	/* The priv checksum covers the server checksum */
	ret = verify_checksum(context,
			      pac->privsvr_checksum,
			      &pac->data,
			      (char *)pac->data.data
			      + pac->server_checksum->offset_lo + 4,
			      pac->server_checksum->buffersize - 4,
			      privsvr);
	if (ret)
	    return ret;
    }

    return 0;
}

/*
 *
 */

static krb5_error_code
fill_zeros(krb5_context context, krb5_storage *sp, size_t len)
{
    ssize_t sret;
    size_t l;

    while (len) {
	l = len;
	if (l > sizeof(zeros))
	    l = sizeof(zeros);
	sret = krb5_storage_write(sp, zeros, l);
	if (sret <= 0)
	    return krb5_enomem(context);

	len -= sret;
    }
    return 0;
}

static krb5_error_code
pac_checksum(krb5_context context,
	     const krb5_keyblock *key,
	     uint32_t *cksumtype,
	     size_t *cksumsize)
{
    krb5_cksumtype cktype;
    krb5_error_code ret;
    krb5_crypto crypto = NULL;

    ret = krb5_crypto_init(context, key, 0, &crypto);
    if (ret)
	return ret;

    ret = krb5_crypto_get_checksum_type(context, crypto, &cktype);
    krb5_crypto_destroy(context, crypto);
    if (ret)
	return ret;

    if (krb5_checksum_is_keyed(context, cktype) == FALSE) {
	*cksumtype = CKSUMTYPE_HMAC_MD5;
	*cksumsize = 16;
    }

    ret = krb5_checksumsize(context, cktype, cksumsize);
    if (ret)
	return ret;

    *cksumtype = (uint32_t)cktype;

    return 0;
}

krb5_error_code
_krb5_pac_sign(krb5_context context,
	       krb5_pac p,
	       time_t authtime,
	       krb5_principal principal,
	       const krb5_keyblock *server_key,
	       const krb5_keyblock *priv_key,
	       krb5_data *data)
{
    krb5_error_code ret;
    krb5_storage *sp = NULL, *spdata = NULL;
    uint32_t end;
    size_t server_size, priv_size;
    uint32_t server_offset = 0, priv_offset = 0;
    uint32_t server_cksumtype = 0, priv_cksumtype = 0;
    int num = 0;
    size_t i;
    krb5_data logon, d;

    krb5_data_zero(&logon);

    if (p->logon_name == NULL)
	num++;
    if (p->server_checksum == NULL)
	num++;
    if (p->privsvr_checksum == NULL)
	num++;

    if (num) {
	void *ptr;

	ptr = realloc(p->pac, sizeof(*p->pac) + (sizeof(p->pac->buffers[0]) * (p->pac->numbuffers + num - 1)));
	if (ptr == NULL)
	    return krb5_enomem(context);

	p->pac = ptr;

	if (p->logon_name == NULL) {
	    p->logon_name = &p->pac->buffers[p->pac->numbuffers++];
	    memset(p->logon_name, 0, sizeof(*p->logon_name));
	    p->logon_name->type = PAC_LOGON_NAME;
	}
	if (p->server_checksum == NULL) {
	    p->server_checksum = &p->pac->buffers[p->pac->numbuffers++];
	    memset(p->server_checksum, 0, sizeof(*p->server_checksum));
	    p->server_checksum->type = PAC_SERVER_CHECKSUM;
	}
	if (p->privsvr_checksum == NULL) {
	    p->privsvr_checksum = &p->pac->buffers[p->pac->numbuffers++];
	    memset(p->privsvr_checksum, 0, sizeof(*p->privsvr_checksum));
	    p->privsvr_checksum->type = PAC_PRIVSVR_CHECKSUM;
	}
    }

    /* Calculate LOGON NAME */
    ret = build_logon_name(context, authtime, principal, &logon);
    if (ret)
	goto out;

    /* Set lengths for checksum */
    ret = pac_checksum(context, server_key, &server_cksumtype, &server_size);
    if (ret)
	goto out;
    ret = pac_checksum(context, priv_key, &priv_cksumtype, &priv_size);
    if (ret)
	goto out;

    /* Encode PAC */
    sp = krb5_storage_emem();
    if (sp == NULL)
	return krb5_enomem(context);

    krb5_storage_set_flags(sp, KRB5_STORAGE_BYTEORDER_LE);

    spdata = krb5_storage_emem();
    if (spdata == NULL) {
	krb5_storage_free(sp);
	return krb5_enomem(context);
    }
    krb5_storage_set_flags(spdata, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(ret, krb5_store_uint32(sp, p->pac->numbuffers), out);
    CHECK(ret, krb5_store_uint32(sp, p->pac->version), out);

    end = PACTYPE_SIZE + (PAC_INFO_BUFFER_SIZE * p->pac->numbuffers);

    for (i = 0; i < p->pac->numbuffers; i++) {
	uint32_t len;
	size_t sret;
	void *ptr = NULL;

	/* store data */

	if (p->pac->buffers[i].type == PAC_SERVER_CHECKSUM) {
	    len = server_size + 4;
	    server_offset = end + 4;
	    CHECK(ret, krb5_store_uint32(spdata, server_cksumtype), out);
	    CHECK(ret, fill_zeros(context, spdata, server_size), out);
	} else if (p->pac->buffers[i].type == PAC_PRIVSVR_CHECKSUM) {
	    len = priv_size + 4;
	    priv_offset = end + 4;
	    CHECK(ret, krb5_store_uint32(spdata, priv_cksumtype), out);
	    CHECK(ret, fill_zeros(context, spdata, priv_size), out);
	} else if (p->pac->buffers[i].type == PAC_LOGON_NAME) {
	    len = krb5_storage_write(spdata, logon.data, logon.length);
	    if (logon.length != len) {
		ret = EINVAL;
		goto out;
	    }
	} else {
	    len = p->pac->buffers[i].buffersize;
	    ptr = (char *)p->data.data + p->pac->buffers[i].offset_lo;

	    sret = krb5_storage_write(spdata, ptr, len);
	    if (sret != len) {
		ret = krb5_enomem(context);
		goto out;
	    }
	    /* XXX if not aligned, fill_zeros */
	}

	/* write header */
	CHECK(ret, krb5_store_uint32(sp, p->pac->buffers[i].type), out);
	CHECK(ret, krb5_store_uint32(sp, len), out);
	CHECK(ret, krb5_store_uint32(sp, end), out);
	CHECK(ret, krb5_store_uint32(sp, 0), out);

	/* advance data endpointer and align */
	{
	    int32_t e;

	    end += len;
	    e = ((end + PAC_ALIGNMENT - 1) / PAC_ALIGNMENT) * PAC_ALIGNMENT;
	    if ((int32_t)end != e) {
		CHECK(ret, fill_zeros(context, spdata, e - end), out);
	    }
	    end = e;
	}

    }

    /* assert (server_offset != 0 && priv_offset != 0); */

    /* export PAC */
    ret = krb5_storage_to_data(spdata, &d);
    if (ret) {
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	goto out;
    }
    ret = krb5_storage_write(sp, d.data, d.length);
    if (ret != (int)d.length) {
	krb5_data_free(&d);
	ret = krb5_enomem(context);
	goto out;
    }
    krb5_data_free(&d);

    ret = krb5_storage_to_data(sp, &d);
    if (ret) {
	ret = krb5_enomem(context);
	goto out;
    }

    /* sign */
    ret = create_checksum(context, server_key, server_cksumtype,
			  d.data, d.length,
			  (char *)d.data + server_offset, server_size);
    if (ret) {
	krb5_data_free(&d);
	goto out;
    }
    ret = create_checksum(context, priv_key, priv_cksumtype,
			  (char *)d.data + server_offset, server_size,
			  (char *)d.data + priv_offset, priv_size);
    if (ret) {
	krb5_data_free(&d);
	goto out;
    }

    /* done */
    *data = d;

    krb5_data_free(&logon);
    krb5_storage_free(sp);
    krb5_storage_free(spdata);

    return 0;
out:
    krb5_data_free(&logon);
    if (sp)
	krb5_storage_free(sp);
    if (spdata)
	krb5_storage_free(spdata);
    return ret;
}
