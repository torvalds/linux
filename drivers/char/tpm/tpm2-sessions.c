// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2018 James.Bottomley@HansenPartnership.com
 *
 */

#include "tpm.h"
#include <asm/unaligned.h>
#include <crypto/hash.h>
#include <crypto/hmac.h>

/*
 * It turns out the crypto hmac(sha256) is hard for us to consume
 * because it assumes a fixed key and the TPM seems to change the key
 * on every operation, so we weld the hmac init and final functions in
 * here to give it the same usage characteristics as a regular hash
 */
static void tpm2_hmac_init(struct sha256_state *sctx, u8 *key, u32 key_len)
{
	u8 pad[SHA256_BLOCK_SIZE];
	int i;

	sha256_init(sctx);
	for (i = 0; i < sizeof(pad); i++) {
		if (i < key_len)
			pad[i] = key[i];
		else
			pad[i] = 0;
		pad[i] ^= HMAC_IPAD_VALUE;
	}
	sha256_update(sctx, pad, sizeof(pad));
}

static void tpm2_hmac_final(struct sha256_state *sctx, u8 *key, u32 key_len,
			    u8 *out)
{
	u8 pad[SHA256_BLOCK_SIZE];
	int i;

	for (i = 0; i < sizeof(pad); i++) {
		if (i < key_len)
			pad[i] = key[i];
		else
			pad[i] = 0;
		pad[i] ^= HMAC_OPAD_VALUE;
	}

	/* collect the final hash;  use out as temporary storage */
	sha256_final(sctx, out);

	sha256_init(sctx);
	sha256_update(sctx, pad, sizeof(pad));
	sha256_update(sctx, out, SHA256_DIGEST_SIZE);
	sha256_final(sctx, out);
}

/*
 * assume hash sha256 and nonces u, v of size SHA256_DIGEST_SIZE but
 * otherwise standard tpm2_KDFa.  Note output is in bytes not bits.
 */
static void tpm2_KDFa(u8 *key, u32 key_len, const char *label, u8 *u,
		      u8 *v, u32 bytes, u8 *out)
{
	u32 counter = 1;
	const __be32 bits = cpu_to_be32(bytes * 8);

	while (bytes > 0) {
		struct sha256_state sctx;
		__be32 c = cpu_to_be32(counter);

		tpm2_hmac_init(&sctx, key, key_len);
		sha256_update(&sctx, (u8 *)&c, sizeof(c));
		sha256_update(&sctx, label, strlen(label)+1);
		sha256_update(&sctx, u, SHA256_DIGEST_SIZE);
		sha256_update(&sctx, v, SHA256_DIGEST_SIZE);
		sha256_update(&sctx, (u8 *)&bits, sizeof(bits));
		tpm2_hmac_final(&sctx, key, key_len, out);

		bytes -= SHA256_DIGEST_SIZE;
		counter++;
		out += SHA256_DIGEST_SIZE;
	}
}

/*
 * Somewhat of a bastardization of the real KDFe.  We're assuming
 * we're working with known point sizes for the input parameters and
 * the hash algorithm is fixed at sha256.  Because we know that the
 * point size is 32 bytes like the hash size, there's no need to loop
 * in this KDF.
 */
static void tpm2_KDFe(u8 z[EC_PT_SZ], const char *str, u8 *pt_u, u8 *pt_v,
		      u8 *out)
{
	struct sha256_state sctx;
	/*
	 * this should be an iterative counter, but because we know
	 *  we're only taking 32 bytes for the point using a sha256
	 *  hash which is also 32 bytes, there's only one loop
	 */
	__be32 c = cpu_to_be32(1);

	sha256_init(&sctx);
	/* counter (BE) */
	sha256_update(&sctx, (u8 *)&c, sizeof(c));
	/* secret value */
	sha256_update(&sctx, z, EC_PT_SZ);
	/* string including trailing zero */
	sha256_update(&sctx, str, strlen(str)+1);
	sha256_update(&sctx, pt_u, EC_PT_SZ);
	sha256_update(&sctx, pt_v, EC_PT_SZ);
	sha256_final(&sctx, out);
}

/**
 * tpm2_parse_create_primary() - parse the data returned from TPM_CC_CREATE_PRIMARY
 *
 * @chip:	The TPM the primary was created under
 * @buf:	The response buffer from the chip
 * @handle:	pointer to be filled in with the return handle of the primary
 * @hierarchy:	The hierarchy the primary was created for
 *
 * Return:
 * * 0		- OK
 * * -errno	- A system error
 * * TPM_RC	- A TPM error
 */
static int tpm2_parse_create_primary(struct tpm_chip *chip, struct tpm_buf *buf,
				     u32 *handle, u32 hierarchy)
{
	struct tpm_header *head = (struct tpm_header *)buf->data;
	off_t offset_r = TPM_HEADER_SIZE, offset_t;
	u16 len = TPM_HEADER_SIZE;
	u32 total_len = be32_to_cpu(head->length);
	u32 val, param_len;

	*handle = tpm_buf_read_u32(buf, &offset_r);
	param_len = tpm_buf_read_u32(buf, &offset_r);
	/*
	 * param_len doesn't include the header, but all the other
	 * lengths and offsets do, so add it to parm len to make
	 * the comparisons easier
	 */
	param_len += TPM_HEADER_SIZE;

	if (param_len + 8 > total_len)
		return -EINVAL;
	len = tpm_buf_read_u16(buf, &offset_r);
	offset_t = offset_r;
	/* now we have the public area, compute the name of the object */
	put_unaligned_be16(TPM_ALG_SHA256, chip->null_key_name);
	sha256(&buf->data[offset_r], len, chip->null_key_name + 2);

	/* validate the public key */
	val = tpm_buf_read_u16(buf, &offset_t);

	/* key type (must be what we asked for) */
	if (val != TPM_ALG_ECC)
		return -EINVAL;
	val = tpm_buf_read_u16(buf, &offset_t);

	/* name algorithm */
	if (val != TPM_ALG_SHA256)
		return -EINVAL;
	val = tpm_buf_read_u32(buf, &offset_t);

	/* object properties */
	if (val != TPM2_OA_TMPL)
		return -EINVAL;

	/* auth policy (empty) */
	val = tpm_buf_read_u16(buf, &offset_t);
	if (val != 0)
		return -EINVAL;

	/* symmetric key parameters */
	val = tpm_buf_read_u16(buf, &offset_t);
	if (val != TPM_ALG_AES)
		return -EINVAL;

	/* symmetric key length */
	val = tpm_buf_read_u16(buf, &offset_t);
	if (val != AES_KEY_BITS)
		return -EINVAL;

	/* symmetric encryption scheme */
	val = tpm_buf_read_u16(buf, &offset_t);
	if (val != TPM_ALG_CFB)
		return -EINVAL;

	/* signing scheme */
	val = tpm_buf_read_u16(buf, &offset_t);
	if (val != TPM_ALG_NULL)
		return -EINVAL;

	/* ECC Curve */
	val = tpm_buf_read_u16(buf, &offset_t);
	if (val != TPM2_ECC_NIST_P256)
		return -EINVAL;

	/* KDF Scheme */
	val = tpm_buf_read_u16(buf, &offset_t);
	if (val != TPM_ALG_NULL)
		return -EINVAL;

	/* extract public key (x and y points) */
	val = tpm_buf_read_u16(buf, &offset_t);
	if (val != EC_PT_SZ)
		return -EINVAL;
	memcpy(chip->null_ec_key_x, &buf->data[offset_t], val);
	offset_t += val;
	val = tpm_buf_read_u16(buf, &offset_t);
	if (val != EC_PT_SZ)
		return -EINVAL;
	memcpy(chip->null_ec_key_y, &buf->data[offset_t], val);
	offset_t += val;

	/* original length of the whole TPM2B */
	offset_r += len;

	/* should have exactly consumed the TPM2B public structure */
	if (offset_t != offset_r)
		return -EINVAL;
	if (offset_r > param_len)
		return -EINVAL;

	/* creation data (skip) */
	len = tpm_buf_read_u16(buf, &offset_r);
	offset_r += len;
	if (offset_r > param_len)
		return -EINVAL;

	/* creation digest (must be sha256) */
	len = tpm_buf_read_u16(buf, &offset_r);
	offset_r += len;
	if (len != SHA256_DIGEST_SIZE || offset_r > param_len)
		return -EINVAL;

	/* TPMT_TK_CREATION follows */
	/* tag, must be TPM_ST_CREATION (0x8021) */
	val = tpm_buf_read_u16(buf, &offset_r);
	if (val != TPM2_ST_CREATION || offset_r > param_len)
		return -EINVAL;

	/* hierarchy */
	val = tpm_buf_read_u32(buf, &offset_r);
	if (val != hierarchy || offset_r > param_len)
		return -EINVAL;

	/* the ticket digest HMAC (might not be sha256) */
	len = tpm_buf_read_u16(buf, &offset_r);
	offset_r += len;
	if (offset_r > param_len)
		return -EINVAL;

	/*
	 * finally we have the name, which is a sha256 digest plus a 2
	 * byte algorithm type
	 */
	len = tpm_buf_read_u16(buf, &offset_r);
	if (offset_r + len != param_len + 8)
		return -EINVAL;
	if (len != SHA256_DIGEST_SIZE + 2)
		return -EINVAL;

	if (memcmp(chip->null_key_name, &buf->data[offset_r],
		   SHA256_DIGEST_SIZE + 2) != 0) {
		dev_err(&chip->dev, "NULL Seed name comparison failed\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * tpm2_create_primary() - create a primary key using a fixed P-256 template
 *
 * @chip:      the TPM chip to create under
 * @hierarchy: The hierarchy handle to create under
 * @handle:    The returned volatile handle on success
 *
 * For platforms that might not have a persistent primary, this can be
 * used to create one quickly on the fly (it uses Elliptic Curve not
 * RSA, so even slow TPMs can create one fast).  The template uses the
 * TCG mandated H one for non-endorsement ECC primaries, i.e. P-256
 * elliptic curve (the only current one all TPM2s are required to
 * have) a sha256 name hash and no policy.
 *
 * Return:
 * * 0		- OK
 * * -errno	- A system error
 * * TPM_RC	- A TPM error
 */
static int tpm2_create_primary(struct tpm_chip *chip, u32 hierarchy,
			       u32 *handle)
{
	int rc;
	struct tpm_buf buf;
	struct tpm_buf template;

	rc = tpm_buf_init(&buf, TPM2_ST_SESSIONS, TPM2_CC_CREATE_PRIMARY);
	if (rc)
		return rc;

	rc = tpm_buf_init_sized(&template);
	if (rc) {
		tpm_buf_destroy(&buf);
		return rc;
	}

	/*
	 * create the template.  Note: in order for userspace to
	 * verify the security of the system, it will have to create
	 * and certify this NULL primary, meaning all the template
	 * parameters will have to be identical, so conform exactly to
	 * the TCG TPM v2.0 Provisioning Guidance for the SRK ECC
	 * key H template (H has zero size unique points)
	 */

	/* key type */
	tpm_buf_append_u16(&template, TPM_ALG_ECC);

	/* name algorithm */
	tpm_buf_append_u16(&template, TPM_ALG_SHA256);

	/* object properties */
	tpm_buf_append_u32(&template, TPM2_OA_TMPL);

	/* sauth policy (empty) */
	tpm_buf_append_u16(&template, 0);

	/* BEGIN parameters: key specific; for ECC*/

	/* symmetric algorithm */
	tpm_buf_append_u16(&template, TPM_ALG_AES);

	/* bits for symmetric algorithm */
	tpm_buf_append_u16(&template, AES_KEY_BITS);

	/* algorithm mode (must be CFB) */
	tpm_buf_append_u16(&template, TPM_ALG_CFB);

	/* scheme (NULL means any scheme) */
	tpm_buf_append_u16(&template, TPM_ALG_NULL);

	/* ECC Curve ID */
	tpm_buf_append_u16(&template, TPM2_ECC_NIST_P256);

	/* KDF Scheme */
	tpm_buf_append_u16(&template, TPM_ALG_NULL);

	/* unique: key specific; for ECC it is two zero size points */
	tpm_buf_append_u16(&template, 0);
	tpm_buf_append_u16(&template, 0);

	/* END parameters */

	/* primary handle */
	tpm_buf_append_u32(&buf, hierarchy);
	tpm_buf_append_empty_auth(&buf, TPM2_RS_PW);

	/* sensitive create size is 4 for two empty buffers */
	tpm_buf_append_u16(&buf, 4);

	/* sensitive create auth data (empty) */
	tpm_buf_append_u16(&buf, 0);

	/* sensitive create sensitive data (empty) */
	tpm_buf_append_u16(&buf, 0);

	/* the public template */
	tpm_buf_append(&buf, template.data, template.length);
	tpm_buf_destroy(&template);

	/* outside info (empty) */
	tpm_buf_append_u16(&buf, 0);

	/* creation PCR (none) */
	tpm_buf_append_u32(&buf, 0);

	rc = tpm_transmit_cmd(chip, &buf, 0,
			      "attempting to create NULL primary");

	if (rc == TPM2_RC_SUCCESS)
		rc = tpm2_parse_create_primary(chip, &buf, handle, hierarchy);

	tpm_buf_destroy(&buf);

	return rc;
}

static int tpm2_create_null_primary(struct tpm_chip *chip)
{
	u32 null_key;
	int rc;

	rc = tpm2_create_primary(chip, TPM2_RH_NULL, &null_key);

	if (rc == TPM2_RC_SUCCESS) {
		unsigned int offset = 0; /* dummy offset for null key context */

		rc = tpm2_save_context(chip, null_key, chip->null_key_context,
				       sizeof(chip->null_key_context), &offset);
		tpm2_flush_context(chip, null_key);
	}

	return rc;
}

/**
 * tpm2_sessions_init() - start of day initialization for the sessions code
 * @chip: TPM chip
 *
 * Derive and context save the null primary and allocate memory in the
 * struct tpm_chip for the authorizations.
 */
int tpm2_sessions_init(struct tpm_chip *chip)
{
	int rc;

	rc = tpm2_create_null_primary(chip);
	if (rc)
		dev_err(&chip->dev, "TPM: security failed (NULL seed derivation): %d\n", rc);

	return rc;
}
