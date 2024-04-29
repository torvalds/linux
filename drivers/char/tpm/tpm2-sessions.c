// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2018 James.Bottomley@HansenPartnership.com
 *
 * Cryptographic helper routines for handling TPM2 sessions for
 * authorization HMAC and request response encryption.
 *
 * The idea is to ensure that every TPM command is HMAC protected by a
 * session, meaning in-flight tampering would be detected and in
 * addition all sensitive inputs and responses should be encrypted.
 *
 * The basic way this works is to use a TPM feature called salted
 * sessions where a random secret used in session construction is
 * encrypted to the public part of a known TPM key.  The problem is we
 * have no known keys, so initially a primary Elliptic Curve key is
 * derived from the NULL seed (we use EC because most TPMs generate
 * these keys much faster than RSA ones).  The curve used is NIST_P256
 * because that's now mandated to be present in 'TCG TPM v2.0
 * Provisioning Guidance'
 *
 * Threat problems: the initial TPM2_CreatePrimary is not (and cannot
 * be) session protected, so a clever Man in the Middle could return a
 * public key they control to this command and from there intercept
 * and decode all subsequent session based transactions.  The kernel
 * cannot mitigate this threat but, after boot, userspace can get
 * proof this has not happened by asking the TPM to certify the NULL
 * key.  This certification would chain back to the TPM Endorsement
 * Certificate and prove the NULL seed primary had not been tampered
 * with and thus all sessions must have been cryptographically secure.
 * To assist with this, the initial NULL seed public key name is made
 * available in a sysfs file.
 *
 * Use of these functions:
 *
 * The design is all the crypto, hash and hmac gunk is confined in this
 * file and never needs to be seen even by the kernel internal user.  To
 * the user there's an init function tpm2_sessions_init() that needs to
 * be called once per TPM which generates the NULL seed primary key.
 *
 * These are the usage functions:
 *
 * tpm2_start_auth_session() which allocates the opaque auth structure
 *	and gets a session from the TPM.  This must be called before
 *	any of the following functions.  The session is protected by a
 *	session_key which is derived from a random salt value
 *	encrypted to the NULL seed.
 * tpm2_end_auth_session() kills the session and frees the resources.
 *	Under normal operation this function is done by
 *	tpm_buf_check_hmac_response(), so this is only to be used on
 *	error legs where the latter is not executed.
 * tpm_buf_append_name() to add a handle to the buffer.  This must be
 *	used in place of the usual tpm_buf_append_u32() for adding
 *	handles because handles have to be processed specially when
 *	calculating the HMAC.  In particular, for NV, volatile and
 *	permanent objects you now need to provide the name.
 */

#include "tpm.h"
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <asm/unaligned.h>
#include <crypto/kpp.h>
#include <crypto/ecdh.h>
#include <crypto/hash.h>
#include <crypto/hmac.h>

/* maximum number of names the TPM must remember for authorization */
#define AUTH_MAX_NAMES	3

/*
 * This is the structure that carries all the auth information (like
 * session handle, nonces, session key and auth) from use to use it is
 * designed to be opaque to anything outside.
 */
struct tpm2_auth {
	u32 handle;
	/*
	 * This has two meanings: before tpm_buf_fill_hmac_session()
	 * it marks the offset in the buffer of the start of the
	 * sessions (i.e. after all the handles).  Once the buffer has
	 * been filled it markes the session number of our auth
	 * session so we can find it again in the response buffer.
	 *
	 * The two cases are distinguished because the first offset
	 * must always be greater than TPM_HEADER_SIZE and the second
	 * must be less than or equal to 5.
	 */
	u32 session;
	/*
	 * the size here is variable and set by the size of our_nonce
	 * which must be between 16 and the name hash length. we set
	 * the maximum sha256 size for the greatest protection
	 */
	u8 our_nonce[SHA256_DIGEST_SIZE];
	u8 tpm_nonce[SHA256_DIGEST_SIZE];
	/*
	 * the salt is only used across the session command/response
	 * after that it can be used as a scratch area
	 */
	union {
		u8 salt[EC_PT_SZ];
		/* scratch for key + IV */
		u8 scratch[AES_KEY_BYTES + AES_BLOCK_SIZE];
	};
	u8 session_key[SHA256_DIGEST_SIZE];

	/*
	 * memory for three authorization handles.  We know them by
	 * handle, but they are part of the session by name, which
	 * we must compute and remember
	 */
	u32 name_h[AUTH_MAX_NAMES];
	u8 name[AUTH_MAX_NAMES][2 + SHA512_DIGEST_SIZE];
};

/*
 * Name Size based on TPM algorithm (assumes no hash bigger than 255)
 */
static u8 name_size(const u8 *name)
{
	static u8 size_map[] = {
		[TPM_ALG_SHA1] = SHA1_DIGEST_SIZE,
		[TPM_ALG_SHA256] = SHA256_DIGEST_SIZE,
		[TPM_ALG_SHA384] = SHA384_DIGEST_SIZE,
		[TPM_ALG_SHA512] = SHA512_DIGEST_SIZE,
	};
	u16 alg = get_unaligned_be16(name);
	return size_map[alg] + 2;
}

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

static void tpm_buf_append_salt(struct tpm_buf *buf, struct tpm_chip *chip)
{
	struct crypto_kpp *kpp;
	struct kpp_request *req;
	struct scatterlist s[2], d[1];
	struct ecdh p = {0};
	u8 encoded_key[EC_PT_SZ], *x, *y;
	unsigned int buf_len;

	/* secret is two sized points */
	tpm_buf_append_u16(buf, (EC_PT_SZ + 2)*2);
	/*
	 * we cheat here and append uninitialized data to form
	 * the points.  All we care about is getting the two
	 * co-ordinate pointers, which will be used to overwrite
	 * the uninitialized data
	 */
	tpm_buf_append_u16(buf, EC_PT_SZ);
	x = &buf->data[tpm_buf_length(buf)];
	tpm_buf_append(buf, encoded_key, EC_PT_SZ);
	tpm_buf_append_u16(buf, EC_PT_SZ);
	y = &buf->data[tpm_buf_length(buf)];
	tpm_buf_append(buf, encoded_key, EC_PT_SZ);
	sg_init_table(s, 2);
	sg_set_buf(&s[0], x, EC_PT_SZ);
	sg_set_buf(&s[1], y, EC_PT_SZ);

	kpp = crypto_alloc_kpp("ecdh-nist-p256", CRYPTO_ALG_INTERNAL, 0);
	if (IS_ERR(kpp)) {
		dev_err(&chip->dev, "crypto ecdh allocation failed\n");
		return;
	}

	buf_len = crypto_ecdh_key_len(&p);
	if (sizeof(encoded_key) < buf_len) {
		dev_err(&chip->dev, "salt buffer too small needs %d\n",
			buf_len);
		goto out;
	}
	crypto_ecdh_encode_key(encoded_key, buf_len, &p);
	/* this generates a random private key */
	crypto_kpp_set_secret(kpp, encoded_key, buf_len);

	/* salt is now the public point of this private key */
	req = kpp_request_alloc(kpp, GFP_KERNEL);
	if (!req)
		goto out;
	kpp_request_set_input(req, NULL, 0);
	kpp_request_set_output(req, s, EC_PT_SZ*2);
	crypto_kpp_generate_public_key(req);
	/*
	 * we're not done: now we have to compute the shared secret
	 * which is our private key multiplied by the tpm_key public
	 * point, we actually only take the x point and discard the y
	 * point and feed it through KDFe to get the final secret salt
	 */
	sg_set_buf(&s[0], chip->null_ec_key_x, EC_PT_SZ);
	sg_set_buf(&s[1], chip->null_ec_key_y, EC_PT_SZ);
	kpp_request_set_input(req, s, EC_PT_SZ*2);
	sg_init_one(d, chip->auth->salt, EC_PT_SZ);
	kpp_request_set_output(req, d, EC_PT_SZ);
	crypto_kpp_compute_shared_secret(req);
	kpp_request_free(req);

	/*
	 * pass the shared secret through KDFe for salt. Note salt
	 * area is used both for input shared secret and output salt.
	 * This works because KDFe fully consumes the secret before it
	 * writes the salt
	 */
	tpm2_KDFe(chip->auth->salt, "SECRET", x, chip->null_ec_key_x,
		  chip->auth->salt);

 out:
	crypto_free_kpp(kpp);
}

static int tpm2_parse_read_public(char *name, struct tpm_buf *buf)
{
	struct tpm_header *head = (struct tpm_header *)buf->data;
	off_t offset = TPM_HEADER_SIZE;
	u32 tot_len = be32_to_cpu(head->length);
	u32 val;

	/* we're starting after the header so adjust the length */
	tot_len -= TPM_HEADER_SIZE;

	/* skip public */
	val = tpm_buf_read_u16(buf, &offset);
	if (val > tot_len)
		return -EINVAL;
	offset += val;
	/* name */
	val = tpm_buf_read_u16(buf, &offset);
	if (val != name_size(&buf->data[offset]))
		return -EINVAL;
	memcpy(name, &buf->data[offset], val);
	/* forget the rest */
	return 0;
}

static int tpm2_read_public(struct tpm_chip *chip, u32 handle, char *name)
{
	struct tpm_buf buf;
	int rc;

	rc = tpm_buf_init(&buf, TPM2_ST_NO_SESSIONS, TPM2_CC_READ_PUBLIC);
	if (rc)
		return rc;

	tpm_buf_append_u32(&buf, handle);
	rc = tpm_transmit_cmd(chip, &buf, 0, "read public");
	if (rc == TPM2_RC_SUCCESS)
		rc = tpm2_parse_read_public(name, &buf);

	tpm_buf_destroy(&buf);

	return rc;
}

/**
 * tpm_buf_append_name() - add a handle area to the buffer
 * @chip: the TPM chip structure
 * @buf: The buffer to be appended
 * @handle: The handle to be appended
 * @name: The name of the handle (may be NULL)
 *
 * In order to compute session HMACs, we need to know the names of the
 * objects pointed to by the handles.  For most objects, this is simply
 * the actual 4 byte handle or an empty buf (in these cases @name
 * should be NULL) but for volatile objects, permanent objects and NV
 * areas, the name is defined as the hash (according to the name
 * algorithm which should be set to sha256) of the public area to
 * which the two byte algorithm id has been appended.  For these
 * objects, the @name pointer should point to this.  If a name is
 * required but @name is NULL, then TPM2_ReadPublic() will be called
 * on the handle to obtain the name.
 *
 * As with most tpm_buf operations, success is assumed because failure
 * will be caused by an incorrect programming model and indicated by a
 * kernel message.
 */
void tpm_buf_append_name(struct tpm_chip *chip, struct tpm_buf *buf,
			 u32 handle, u8 *name)
{
	enum tpm2_mso_type mso = tpm2_handle_mso(handle);
	struct tpm2_auth *auth = chip->auth;
	int slot;

	slot = (tpm_buf_length(buf) - TPM_HEADER_SIZE)/4;
	if (slot >= AUTH_MAX_NAMES) {
		dev_err(&chip->dev, "TPM: too many handles\n");
		return;
	}
	WARN(auth->session != tpm_buf_length(buf),
	     "name added in wrong place\n");
	tpm_buf_append_u32(buf, handle);
	auth->session += 4;

	if (mso == TPM2_MSO_PERSISTENT ||
	    mso == TPM2_MSO_VOLATILE ||
	    mso == TPM2_MSO_NVRAM) {
		if (!name)
			tpm2_read_public(chip, handle, auth->name[slot]);
	} else {
		if (name)
			dev_err(&chip->dev, "TPM: Handle does not require name but one is specified\n");
	}

	auth->name_h[slot] = handle;
	if (name)
		memcpy(auth->name[slot], name, name_size(name));
}
EXPORT_SYMBOL(tpm_buf_append_name);
/**
 * tpm2_end_auth_session() - kill the allocated auth session
 * @chip: the TPM chip structure
 *
 * ends the session started by tpm2_start_auth_session and frees all
 * the resources.  Under normal conditions,
 * tpm_buf_check_hmac_response() will correctly end the session if
 * required, so this function is only for use in error legs that will
 * bypass the normal invocation of tpm_buf_check_hmac_response().
 */
void tpm2_end_auth_session(struct tpm_chip *chip)
{
	tpm2_flush_context(chip, chip->auth->handle);
	memzero_explicit(chip->auth, sizeof(*chip->auth));
}
EXPORT_SYMBOL(tpm2_end_auth_session);

static int tpm2_parse_start_auth_session(struct tpm2_auth *auth,
					 struct tpm_buf *buf)
{
	struct tpm_header *head = (struct tpm_header *)buf->data;
	u32 tot_len = be32_to_cpu(head->length);
	off_t offset = TPM_HEADER_SIZE;
	u32 val;

	/* we're starting after the header so adjust the length */
	tot_len -= TPM_HEADER_SIZE;

	/* should have handle plus nonce */
	if (tot_len != 4 + 2 + sizeof(auth->tpm_nonce))
		return -EINVAL;

	auth->handle = tpm_buf_read_u32(buf, &offset);
	val = tpm_buf_read_u16(buf, &offset);
	if (val != sizeof(auth->tpm_nonce))
		return -EINVAL;
	memcpy(auth->tpm_nonce, &buf->data[offset], sizeof(auth->tpm_nonce));
	/* now compute the session key from the nonces */
	tpm2_KDFa(auth->salt, sizeof(auth->salt), "ATH", auth->tpm_nonce,
		  auth->our_nonce, sizeof(auth->session_key),
		  auth->session_key);

	return 0;
}

/**
 * tpm2_start_auth_session() - create a HMAC authentication session with the TPM
 * @chip: the TPM chip structure to create the session with
 *
 * This function loads the NULL seed from its saved context and starts
 * an authentication session on the null seed, fills in the
 * @chip->auth structure to contain all the session details necessary
 * for performing the HMAC, encrypt and decrypt operations and
 * returns.  The NULL seed is flushed before this function returns.
 *
 * Return: zero on success or actual error encountered.
 */
int tpm2_start_auth_session(struct tpm_chip *chip)
{
	struct tpm_buf buf;
	struct tpm2_auth *auth = chip->auth;
	int rc;
	/* null seed context has no offset, but we must provide one */
	unsigned int offset = 0;
	u32 nullkey;

	rc = tpm2_load_context(chip, chip->null_key_context, &offset,
			       &nullkey);
	if (rc)
		goto out;

	auth->session = TPM_HEADER_SIZE;

	rc = tpm_buf_init(&buf, TPM2_ST_NO_SESSIONS, TPM2_CC_START_AUTH_SESS);
	if (rc)
		goto out;

	/* salt key handle */
	tpm_buf_append_u32(&buf, nullkey);
	/* bind key handle */
	tpm_buf_append_u32(&buf, TPM2_RH_NULL);
	/* nonce caller */
	get_random_bytes(auth->our_nonce, sizeof(auth->our_nonce));
	tpm_buf_append_u16(&buf, sizeof(auth->our_nonce));
	tpm_buf_append(&buf, auth->our_nonce, sizeof(auth->our_nonce));

	/* append encrypted salt and squirrel away unencrypted in auth */
	tpm_buf_append_salt(&buf, chip);
	/* session type (HMAC, audit or policy) */
	tpm_buf_append_u8(&buf, TPM2_SE_HMAC);

	/* symmetric encryption parameters */
	/* symmetric algorithm */
	tpm_buf_append_u16(&buf, TPM_ALG_AES);
	/* bits for symmetric algorithm */
	tpm_buf_append_u16(&buf, AES_KEY_BITS);
	/* symmetric algorithm mode (must be CFB) */
	tpm_buf_append_u16(&buf, TPM_ALG_CFB);
	/* hash algorithm for session */
	tpm_buf_append_u16(&buf, TPM_ALG_SHA256);

	rc = tpm_transmit_cmd(chip, &buf, 0, "start auth session");
	tpm2_flush_context(chip, nullkey);

	if (rc == TPM2_RC_SUCCESS)
		rc = tpm2_parse_start_auth_session(auth, &buf);

	tpm_buf_destroy(&buf);

	if (rc)
		goto out;

 out:
	return rc;
}
EXPORT_SYMBOL(tpm2_start_auth_session);

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

	chip->auth = kmalloc(sizeof(*chip->auth), GFP_KERNEL);
	if (!chip->auth)
		return -ENOMEM;

	return rc;
}
