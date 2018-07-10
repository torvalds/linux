/*
 * Copyright (C) 2017 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "tls.h"

/* The code below is taken from parts of
 *  matrixssl-3-7-2b-open/crypto/pubkey/pkcs.c
 *  matrixssl-3-7-2b-open/crypto/pubkey/rsa.c
 * and (so far) almost not modified. Changes are flagged with //bbox
 */

#define pkcs1Pad(in, inlen, out, outlen, cryptType, userPtr) \
        pkcs1Pad(in, inlen, out, outlen, cryptType)
static //bbox
int32 pkcs1Pad(unsigned char *in, uint32 inlen, unsigned char *out,
					   uint32 outlen, int32 cryptType, void *userPtr)
{
	unsigned char   *c;
	int32           randomLen;

	randomLen = outlen - 3 - inlen;
	if (randomLen < 8) {
		psTraceCrypto("pkcs1Pad failure\n");
		return PS_LIMIT_FAIL;
	}
	c = out;
	*c = 0x00;
	c++;
	*c = (unsigned char)cryptType;
	c++;
	if (cryptType == PUBKEY_TYPE) {
		while (randomLen-- > 0) {
			*c++ = 0xFF;
		}
	} else {
		if (matrixCryptoGetPrngData(c, (uint32)randomLen, userPtr) < 0) {
			return PS_PLATFORM_FAIL;
		}
/*
		SECURITY:  Read through the random data and change all 0x0 to 0x01.
		This is per spec that no random bytes should be 0
*/
		while (randomLen-- > 0) {
			if (*c == 0x0) {
				*c = 0x01;
			}
			c++;
		}
	}
	*c = 0x00;
	c++;
	memcpy(c, in, inlen);

	return outlen;
}

#define psRsaCrypt(pool, in, inlen, out, outlen, key, type, data) \
        psRsaCrypt(      in, inlen, out, outlen, key, type)
static //bbox
int32 psRsaCrypt(psPool_t *pool, const unsigned char *in, uint32 inlen,
			unsigned char *out, uint32 *outlen,	psRsaKey_t *key, int32 type,
			void *data)
{
	pstm_int		tmp, tmpa, tmpb;
	int32			res;
	uint32			x;

//bbox
//	if (in == NULL || out == NULL || outlen == NULL || key == NULL) {
//		psTraceCrypto("NULL parameter error in psRsaCrypt\n");
//		return PS_ARG_FAIL;
//	}

	tmp.dp = tmpa.dp = tmpb.dp = NULL;

	/* Init and copy into tmp */
	if (pstm_init_for_read_unsigned_bin(pool, &tmp, inlen + sizeof(pstm_digit))
			!= PS_SUCCESS) {
		return PS_FAILURE;
	}
	if (pstm_read_unsigned_bin(&tmp, (unsigned char *)in, inlen) != PS_SUCCESS){
		pstm_clear(&tmp);
		return PS_FAILURE;
	}
	/* Sanity check on the input */
	if (pstm_cmp(&key->N, &tmp) == PSTM_LT) {
		res = PS_LIMIT_FAIL;
		goto done;
	}
	if (type == PRIVKEY_TYPE) {
		if (key->optimized) {
			if (pstm_init_size(pool, &tmpa, key->p.alloc) != PS_SUCCESS) {
				res = PS_FAILURE;
				goto done;
			}
			if (pstm_init_size(pool, &tmpb, key->q.alloc) != PS_SUCCESS) {
				pstm_clear(&tmpa);
				res = PS_FAILURE;
				goto done;
			}
			if (pstm_exptmod(pool, &tmp, &key->dP, &key->p, &tmpa) !=
					PS_SUCCESS) {
				psTraceCrypto("decrypt error: pstm_exptmod dP, p\n");
				goto error;
			}
			if (pstm_exptmod(pool, &tmp, &key->dQ, &key->q, &tmpb) !=
					PS_SUCCESS) {
				psTraceCrypto("decrypt error: pstm_exptmod dQ, q\n");
				goto error;
			}
			if (pstm_sub(&tmpa, &tmpb, &tmp) != PS_SUCCESS) {
				psTraceCrypto("decrypt error: sub tmpb, tmp\n");
				goto error;
			}
			if (pstm_mulmod(pool, &tmp, &key->qP, &key->p, &tmp) != PS_SUCCESS) {
				psTraceCrypto("decrypt error: pstm_mulmod qP, p\n");
				goto error;
			}
			if (pstm_mul_comba(pool, &tmp, &key->q, &tmp, NULL, 0)
					!= PS_SUCCESS){
				psTraceCrypto("decrypt error: pstm_mul q \n");
				goto error;
			}
			if (pstm_add(&tmp, &tmpb, &tmp) != PS_SUCCESS) {
				psTraceCrypto("decrypt error: pstm_add tmp \n");
				goto error;
			}
		} else {
			if (pstm_exptmod(pool, &tmp, &key->d, &key->N, &tmp) !=
					PS_SUCCESS) {
				psTraceCrypto("psRsaCrypt error: pstm_exptmod\n");
				goto error;
			}
		}
	} else if (type == PUBKEY_TYPE) {
		if (pstm_exptmod(pool, &tmp, &key->e, &key->N, &tmp) != PS_SUCCESS) {
			psTraceCrypto("psRsaCrypt error: pstm_exptmod\n");
			goto error;
		}
	} else {
		psTraceCrypto("psRsaCrypt error: invalid type param\n");
		goto error;
	}
	/* Read it back */
	x = pstm_unsigned_bin_size(&key->N);

	if ((uint32)x > *outlen) {
		res = -1;
		psTraceCrypto("psRsaCrypt error: pstm_unsigned_bin_size\n");
		goto done;
	}
	/* We want the encrypted value to always be the key size.  Pad with 0x0 */
	while ((uint32)x < (unsigned long)key->size) {
		*out++ = 0x0;
		x++;
	}

	*outlen = x;
	/* Convert it */
	memset(out, 0x0, x);

	if (pstm_to_unsigned_bin(pool, &tmp, out+(x-pstm_unsigned_bin_size(&tmp)))
			!= PS_SUCCESS) {
		psTraceCrypto("psRsaCrypt error: pstm_to_unsigned_bin\n");
		goto error;
	}
	/* Clean up and return */
	res = PS_SUCCESS;
	goto done;
error:
	res = PS_FAILURE;
done:
	if (type == PRIVKEY_TYPE && key->optimized) {
		pstm_clear_multi(&tmpa, &tmpb, NULL, NULL, NULL, NULL, NULL, NULL);
	}
	pstm_clear(&tmp);
	return res;
}

int32 psRsaEncryptPub(psPool_t *pool, psRsaKey_t *key,
						unsigned char *in, uint32 inlen,
						unsigned char *out, uint32 outlen, void *data)
{
	int32	err;
	uint32	size;

	size = key->size;
	if (outlen < size) {
//bbox		psTraceCrypto("Error on bad outlen parameter to psRsaEncryptPub\n");
		bb_error_msg_and_die("RSA crypt outlen:%d < size:%d", outlen, size);
		return PS_ARG_FAIL;
	}

	if ((err = pkcs1Pad(in, inlen, out, size, PRIVKEY_TYPE, data))
			< PS_SUCCESS) {
		psTraceCrypto("Error padding psRsaEncryptPub. Likely data too long\n");
		return err;
	}
	if ((err = psRsaCrypt(pool, out, size, out, (uint32*)&outlen, key,
			PUBKEY_TYPE, data)) < PS_SUCCESS) {
		psTraceCrypto("Error performing psRsaEncryptPub\n");
		return err;
	}
	if (outlen != size) {
		psTraceCrypto("Encrypted size error in psRsaEncryptPub\n");
		return PS_FAILURE;
	}
	return size;
}
