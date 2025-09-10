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
 * tpm2_end_auth_session() kills the session and frees the resources.
 *	Under normal operation this function is done by
 *	tpm_buf_check_hmac_response(), so this is only to be used on
 *	error legs where the latter is not executed.
 * tpm_buf_append_name() to add a handle to the buffer.  This must be
 *	used in place of the usual tpm_buf_append_u32() for adding
 *	handles because handles have to be processed specially when
 *	calculating the HMAC.  In particular, for NV, volatile and
 *	permanent objects you now need to provide the name.
 * tpm_buf_append_hmac_session() which appends the hmac session to the
 *	buf in the same way tpm_buf_append_auth does().
 * tpm_buf_fill_hmac_session() This calculates the correct hash and
 *	places it in the buffer.  It must be called after the complete
 *	command buffer is finalized so it can fill in the correct HMAC
 *	based on the parameters.
 * tpm_buf_check_hmac_response() which checks the session response in
 *	the buffer and calculates what it should be.  If there's a
 *	mismatch it will log a warning and return an error.  If
 *	tpm_buf_append_hmac_session() did not specify
 *	TPM_SA_CONTINUE_SESSION then the session will be closed (if it
 *	hasn't been consumed) and the auth structure freed.
 */

#include "tpm.h"
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/unaligned.h>
#include <crypto/kpp.h>
#include <crypto/ecdh.h>
#include <crypto/hash.h>
#include <crypto/hmac.h>

/* maximum number of names the TPM must remember for authorization */
#define AUTH_MAX_NAMES	3

#define AES_KEY_BYTES	AES_KEYSIZE_128
#define AES_KEY_BITS	(AES_KEY_BYTES*8)

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
	/*
	 * the session key and passphrase are the same size as the
	 * name digest (sha256 again).  The session key is constant
	 * for the use of the session and the passphrase can change
	 * with every invocation.
	 *
	 * Note: these fields must be adjacent and in this order
	 * because several HMAC/KDF schemes use the combination of the
	 * session_key and passphrase.
	 */
	u8 session_key[SHA256_DIGEST_SIZE];
	u8 passphrase[SHA256_DIGEST_SIZE];
	int passphrase_len;
	struct crypto_aes_ctx aes_ctx;
	/* saved session attributes: */
	u8 attrs;
	__be32 ordinal;

	/*
	 * memory for three authorization handles.  We know them by
	 * handle, but they are part of the session by name, which
	 * we must compute and remember
	 */
	u32 name_h[AUTH_MAX_NAMES];
	u8 name[AUTH_MAX_NAMES][2 + SHA512_DIGEST_SIZE];
};

#ifdef CONFIG_TCG_TPM2_HMAC
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
#endif /* CONFIG_TCG_TPM2_HMAC */

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
#ifdef CONFIG_TCG_TPM2_HMAC
	enum tpm2_mso_type mso = tpm2_handle_mso(handle);
	struct tpm2_auth *auth;
	int slot;
#endif

	if (!tpm2_chip_auth(chip)) {
		tpm_buf_append_handle(chip, buf, handle);
		return;
	}

#ifdef CONFIG_TCG_TPM2_HMAC
	slot = (tpm_buf_length(buf) - TPM_HEADER_SIZE) / 4;
	if (slot >= AUTH_MAX_NAMES) {
		dev_err(&chip->dev, "TPM: too many handles\n");
		return;
	}
	auth = chip->auth;
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
#endif
}
EXPORT_SYMBOL_GPL(tpm_buf_append_name);

void tpm_buf_append_auth(struct tpm_chip *chip, struct tpm_buf *buf,
			 u8 attributes, u8 *passphrase, int passphrase_len)
{
	/* offset tells us where the sessions area begins */
	int offset = buf->handles * 4 + TPM_HEADER_SIZE;
	u32 len = 9 + passphrase_len;

	if (tpm_buf_length(buf) != offset) {
		/* not the first session so update the existing length */
		len += get_unaligned_be32(&buf->data[offset]);
		put_unaligned_be32(len, &buf->data[offset]);
	} else {
		tpm_buf_append_u32(buf, len);
	}
	/* auth handle */
	tpm_buf_append_u32(buf, TPM2_RS_PW);
	/* nonce */
	tpm_buf_append_u16(buf, 0);
	/* attributes */
	tpm_buf_append_u8(buf, 0);
	/* passphrase */
	tpm_buf_append_u16(buf, passphrase_len);
	tpm_buf_append(buf, passphrase, passphrase_len);
}

/**
 * tpm_buf_append_hmac_session() - Append a TPM session element
 * @chip: the TPM chip structure
 * @buf: The buffer to be appended
 * @attributes: The session attributes
 * @passphrase: The session authority (NULL if none)
 * @passphrase_len: The length of the session authority (0 if none)
 *
 * This fills in a session structure in the TPM command buffer, except
 * for the HMAC which cannot be computed until the command buffer is
 * complete.  The type of session is controlled by the @attributes,
 * the main ones of which are TPM2_SA_CONTINUE_SESSION which means the
 * session won't terminate after tpm_buf_check_hmac_response(),
 * TPM2_SA_DECRYPT which means this buffers first parameter should be
 * encrypted with a session key and TPM2_SA_ENCRYPT, which means the
 * response buffer's first parameter needs to be decrypted (confusing,
 * but the defines are written from the point of view of the TPM).
 *
 * Any session appended by this command must be finalized by calling
 * tpm_buf_fill_hmac_session() otherwise the HMAC will be incorrect
 * and the TPM will reject the command.
 *
 * As with most tpm_buf operations, success is assumed because failure
 * will be caused by an incorrect programming model and indicated by a
 * kernel message.
 */
void tpm_buf_append_hmac_session(struct tpm_chip *chip, struct tpm_buf *buf,
				 u8 attributes, u8 *passphrase,
				 int passphrase_len)
{
#ifdef CONFIG_TCG_TPM2_HMAC
	u8 nonce[SHA256_DIGEST_SIZE];
	struct tpm2_auth *auth;
	u32 len;
#endif

	if (!tpm2_chip_auth(chip)) {
		tpm_buf_append_auth(chip, buf, attributes, passphrase,
				    passphrase_len);
		return;
	}

#ifdef CONFIG_TCG_TPM2_HMAC
	/* The first write to /dev/tpm{rm0} will flush the session. */
	attributes |= TPM2_SA_CONTINUE_SESSION;

	/*
	 * The Architecture Guide requires us to strip trailing zeros
	 * before computing the HMAC
	 */
	while (passphrase && passphrase_len > 0 && passphrase[passphrase_len - 1] == '\0')
		passphrase_len--;

	auth = chip->auth;
	auth->attrs = attributes;
	auth->passphrase_len = passphrase_len;
	if (passphrase_len)
		memcpy(auth->passphrase, passphrase, passphrase_len);

	if (auth->session != tpm_buf_length(buf)) {
		/* we're not the first session */
		len = get_unaligned_be32(&buf->data[auth->session]);
		if (4 + len + auth->session != tpm_buf_length(buf)) {
			WARN(1, "session length mismatch, cannot append");
			return;
		}

		/* add our new session */
		len += 9 + 2 * SHA256_DIGEST_SIZE;
		put_unaligned_be32(len, &buf->data[auth->session]);
	} else {
		tpm_buf_append_u32(buf, 9 + 2 * SHA256_DIGEST_SIZE);
	}

	/* random number for our nonce */
	get_random_bytes(nonce, sizeof(nonce));
	memcpy(auth->our_nonce, nonce, sizeof(nonce));
	tpm_buf_append_u32(buf, auth->handle);
	/* our new nonce */
	tpm_buf_append_u16(buf, SHA256_DIGEST_SIZE);
	tpm_buf_append(buf, nonce, SHA256_DIGEST_SIZE);
	tpm_buf_append_u8(buf, auth->attrs);
	/* and put a placeholder for the hmac */
	tpm_buf_append_u16(buf, SHA256_DIGEST_SIZE);
	tpm_buf_append(buf, nonce, SHA256_DIGEST_SIZE);
#endif
}
EXPORT_SYMBOL_GPL(tpm_buf_append_hmac_session);

#ifdef CONFIG_TCG_TPM2_HMAC

static int tpm2_create_primary(struct tpm_chip *chip, u32 hierarchy,
			       u32 *handle, u8 *name);

/*
 * It turns out the crypto hmac(sha256) is hard for us to consume
 * because it assumes a fixed key and the TPM seems to change the key
 * on every operation, so we weld the hmac init and final functions in
 * here to give it the same usage characteristics as a regular hash
 */
static void tpm2_hmac_init(struct sha256_ctx *sctx, u8 *key, u32 key_len)
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

static void tpm2_hmac_final(struct sha256_ctx *sctx, u8 *key, u32 key_len,
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
		struct sha256_ctx sctx;
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
	struct sha256_ctx sctx;
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

static void tpm_buf_append_salt(struct tpm_buf *buf, struct tpm_chip *chip,
				struct tpm2_auth *auth)
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
	sg_init_one(d, auth->salt, EC_PT_SZ);
	kpp_request_set_output(req, d, EC_PT_SZ);
	crypto_kpp_compute_shared_secret(req);
	kpp_request_free(req);

	/*
	 * pass the shared secret through KDFe for salt. Note salt
	 * area is used both for input shared secret and output salt.
	 * This works because KDFe fully consumes the secret before it
	 * writes the salt
	 */
	tpm2_KDFe(auth->salt, "SECRET", x, chip->null_ec_key_x, auth->salt);

 out:
	crypto_free_kpp(kpp);
}

/**
 * tpm_buf_fill_hmac_session() - finalize the session HMAC
 * @chip: the TPM chip structure
 * @buf: The buffer to be appended
 *
 * This command must not be called until all of the parameters have
 * been appended to @buf otherwise the computed HMAC will be
 * incorrect.
 *
 * This function computes and fills in the session HMAC using the
 * session key and, if TPM2_SA_DECRYPT was specified, computes the
 * encryption key and encrypts the first parameter of the command
 * buffer with it.
 *
 * As with most tpm_buf operations, success is assumed because failure
 * will be caused by an incorrect programming model and indicated by a
 * kernel message.
 */
void tpm_buf_fill_hmac_session(struct tpm_chip *chip, struct tpm_buf *buf)
{
	u32 cc, handles, val;
	struct tpm2_auth *auth = chip->auth;
	int i;
	struct tpm_header *head = (struct tpm_header *)buf->data;
	off_t offset_s = TPM_HEADER_SIZE, offset_p;
	u8 *hmac = NULL;
	u32 attrs;
	u8 cphash[SHA256_DIGEST_SIZE];
	struct sha256_ctx sctx;

	if (!auth)
		return;

	/* save the command code in BE format */
	auth->ordinal = head->ordinal;

	cc = be32_to_cpu(head->ordinal);

	i = tpm2_find_cc(chip, cc);
	if (i < 0) {
		dev_err(&chip->dev, "Command 0x%x not found in TPM\n", cc);
		return;
	}
	attrs = chip->cc_attrs_tbl[i];

	handles = (attrs >> TPM2_CC_ATTR_CHANDLES) & GENMASK(2, 0);

	/*
	 * just check the names, it's easy to make mistakes.  This
	 * would happen if someone added a handle via
	 * tpm_buf_append_u32() instead of tpm_buf_append_name()
	 */
	for (i = 0; i < handles; i++) {
		u32 handle = tpm_buf_read_u32(buf, &offset_s);

		if (auth->name_h[i] != handle) {
			dev_err(&chip->dev, "TPM: handle %d wrong for name\n",
				  i);
			return;
		}
	}
	/* point offset_s to the start of the sessions */
	val = tpm_buf_read_u32(buf, &offset_s);
	/* point offset_p to the start of the parameters */
	offset_p = offset_s + val;
	for (i = 1; offset_s < offset_p; i++) {
		u32 handle = tpm_buf_read_u32(buf, &offset_s);
		u16 len;
		u8 a;

		/* nonce (already in auth) */
		len = tpm_buf_read_u16(buf, &offset_s);
		offset_s += len;

		a = tpm_buf_read_u8(buf, &offset_s);

		len = tpm_buf_read_u16(buf, &offset_s);
		if (handle == auth->handle && auth->attrs == a) {
			hmac = &buf->data[offset_s];
			/*
			 * save our session number so we know which
			 * session in the response belongs to us
			 */
			auth->session = i;
		}

		offset_s += len;
	}
	if (offset_s != offset_p) {
		dev_err(&chip->dev, "TPM session length is incorrect\n");
		return;
	}
	if (!hmac) {
		dev_err(&chip->dev, "TPM could not find HMAC session\n");
		return;
	}

	/* encrypt before HMAC */
	if (auth->attrs & TPM2_SA_DECRYPT) {
		u16 len;

		/* need key and IV */
		tpm2_KDFa(auth->session_key, SHA256_DIGEST_SIZE
			  + auth->passphrase_len, "CFB", auth->our_nonce,
			  auth->tpm_nonce, AES_KEY_BYTES + AES_BLOCK_SIZE,
			  auth->scratch);

		len = tpm_buf_read_u16(buf, &offset_p);
		aes_expandkey(&auth->aes_ctx, auth->scratch, AES_KEY_BYTES);
		aescfb_encrypt(&auth->aes_ctx, &buf->data[offset_p],
			       &buf->data[offset_p], len,
			       auth->scratch + AES_KEY_BYTES);
		/* reset p to beginning of parameters for HMAC */
		offset_p -= 2;
	}

	sha256_init(&sctx);
	/* ordinal is already BE */
	sha256_update(&sctx, (u8 *)&head->ordinal, sizeof(head->ordinal));
	/* add the handle names */
	for (i = 0; i < handles; i++) {
		enum tpm2_mso_type mso = tpm2_handle_mso(auth->name_h[i]);

		if (mso == TPM2_MSO_PERSISTENT ||
		    mso == TPM2_MSO_VOLATILE ||
		    mso == TPM2_MSO_NVRAM) {
			sha256_update(&sctx, auth->name[i],
				      name_size(auth->name[i]));
		} else {
			__be32 h = cpu_to_be32(auth->name_h[i]);

			sha256_update(&sctx, (u8 *)&h, 4);
		}
	}
	if (offset_s != tpm_buf_length(buf))
		sha256_update(&sctx, &buf->data[offset_s],
			      tpm_buf_length(buf) - offset_s);
	sha256_final(&sctx, cphash);

	/* now calculate the hmac */
	tpm2_hmac_init(&sctx, auth->session_key, sizeof(auth->session_key)
		       + auth->passphrase_len);
	sha256_update(&sctx, cphash, sizeof(cphash));
	sha256_update(&sctx, auth->our_nonce, sizeof(auth->our_nonce));
	sha256_update(&sctx, auth->tpm_nonce, sizeof(auth->tpm_nonce));
	sha256_update(&sctx, &auth->attrs, 1);
	tpm2_hmac_final(&sctx, auth->session_key, sizeof(auth->session_key)
			+ auth->passphrase_len, hmac);
}
EXPORT_SYMBOL(tpm_buf_fill_hmac_session);

/**
 * tpm_buf_check_hmac_response() - check the TPM return HMAC for correctness
 * @chip: the TPM chip structure
 * @buf: the original command buffer (which now contains the response)
 * @rc: the return code from tpm_transmit_cmd
 *
 * If @rc is non zero, @buf may not contain an actual return, so @rc
 * is passed through as the return and the session cleaned up and
 * de-allocated if required (this is required if
 * TPM2_SA_CONTINUE_SESSION was not specified as a session flag).
 *
 * If @rc is zero, the response HMAC is computed against the returned
 * @buf and matched to the TPM one in the session area.  If there is a
 * mismatch, an error is logged and -EINVAL returned.
 *
 * The reason for this is that the command issue and HMAC check
 * sequence should look like:
 *
 *	rc = tpm_transmit_cmd(...);
 *	rc = tpm_buf_check_hmac_response(&buf, auth, rc);
 *	if (rc)
 *		...
 *
 * Which is easily layered into the current contrl flow.
 *
 * Returns: 0 on success or an error.
 */
int tpm_buf_check_hmac_response(struct tpm_chip *chip, struct tpm_buf *buf,
				int rc)
{
	struct tpm_header *head = (struct tpm_header *)buf->data;
	struct tpm2_auth *auth = chip->auth;
	off_t offset_s, offset_p;
	u8 rphash[SHA256_DIGEST_SIZE];
	u32 attrs, cc;
	struct sha256_ctx sctx;
	u16 tag = be16_to_cpu(head->tag);
	int parm_len, len, i, handles;

	if (!auth)
		return rc;

	cc = be32_to_cpu(auth->ordinal);

	if (auth->session >= TPM_HEADER_SIZE) {
		WARN(1, "tpm session not filled correctly\n");
		goto out;
	}

	if (rc != 0)
		/* pass non success rc through and close the session */
		goto out;

	rc = -EINVAL;
	if (tag != TPM2_ST_SESSIONS) {
		dev_err(&chip->dev, "TPM: HMAC response check has no sessions tag\n");
		goto out;
	}

	i = tpm2_find_cc(chip, cc);
	if (i < 0)
		goto out;
	attrs = chip->cc_attrs_tbl[i];
	handles = (attrs >> TPM2_CC_ATTR_RHANDLE) & 1;

	/* point to area beyond handles */
	offset_s = TPM_HEADER_SIZE + handles * 4;
	parm_len = tpm_buf_read_u32(buf, &offset_s);
	offset_p = offset_s;
	offset_s += parm_len;
	/* skip over any sessions before ours */
	for (i = 0; i < auth->session - 1; i++) {
		len = tpm_buf_read_u16(buf, &offset_s);
		offset_s += len + 1;
		len = tpm_buf_read_u16(buf, &offset_s);
		offset_s += len;
	}
	/* TPM nonce */
	len = tpm_buf_read_u16(buf, &offset_s);
	if (offset_s + len > tpm_buf_length(buf))
		goto out;
	if (len != SHA256_DIGEST_SIZE)
		goto out;
	memcpy(auth->tpm_nonce, &buf->data[offset_s], len);
	offset_s += len;
	attrs = tpm_buf_read_u8(buf, &offset_s);
	len = tpm_buf_read_u16(buf, &offset_s);
	if (offset_s + len != tpm_buf_length(buf))
		goto out;
	if (len != SHA256_DIGEST_SIZE)
		goto out;
	/*
	 * offset_s points to the HMAC. now calculate comparison, beginning
	 * with rphash
	 */
	sha256_init(&sctx);
	/* yes, I know this is now zero, but it's what the standard says */
	sha256_update(&sctx, (u8 *)&head->return_code,
		      sizeof(head->return_code));
	/* ordinal is already BE */
	sha256_update(&sctx, (u8 *)&auth->ordinal, sizeof(auth->ordinal));
	sha256_update(&sctx, &buf->data[offset_p], parm_len);
	sha256_final(&sctx, rphash);

	/* now calculate the hmac */
	tpm2_hmac_init(&sctx, auth->session_key, sizeof(auth->session_key)
		       + auth->passphrase_len);
	sha256_update(&sctx, rphash, sizeof(rphash));
	sha256_update(&sctx, auth->tpm_nonce, sizeof(auth->tpm_nonce));
	sha256_update(&sctx, auth->our_nonce, sizeof(auth->our_nonce));
	sha256_update(&sctx, &auth->attrs, 1);
	/* we're done with the rphash, so put our idea of the hmac there */
	tpm2_hmac_final(&sctx, auth->session_key, sizeof(auth->session_key)
			+ auth->passphrase_len, rphash);
	if (memcmp(rphash, &buf->data[offset_s], SHA256_DIGEST_SIZE) == 0) {
		rc = 0;
	} else {
		dev_err(&chip->dev, "TPM: HMAC check failed\n");
		goto out;
	}

	/* now do response decryption */
	if (auth->attrs & TPM2_SA_ENCRYPT) {
		/* need key and IV */
		tpm2_KDFa(auth->session_key, SHA256_DIGEST_SIZE
			  + auth->passphrase_len, "CFB", auth->tpm_nonce,
			  auth->our_nonce, AES_KEY_BYTES + AES_BLOCK_SIZE,
			  auth->scratch);

		len = tpm_buf_read_u16(buf, &offset_p);
		aes_expandkey(&auth->aes_ctx, auth->scratch, AES_KEY_BYTES);
		aescfb_decrypt(&auth->aes_ctx, &buf->data[offset_p],
			       &buf->data[offset_p], len,
			       auth->scratch + AES_KEY_BYTES);
	}

 out:
	if ((auth->attrs & TPM2_SA_CONTINUE_SESSION) == 0) {
		if (rc)
			/* manually close the session if it wasn't consumed */
			tpm2_flush_context(chip, auth->handle);

		kfree_sensitive(auth);
		chip->auth = NULL;
	} else {
		/* reset for next use  */
		auth->session = TPM_HEADER_SIZE;
	}

	return rc;
}
EXPORT_SYMBOL(tpm_buf_check_hmac_response);

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
	struct tpm2_auth *auth = chip->auth;

	if (!auth)
		return;

	tpm2_flush_context(chip, auth->handle);
	kfree_sensitive(auth);
	chip->auth = NULL;
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

static int tpm2_load_null(struct tpm_chip *chip, u32 *null_key)
{
	unsigned int offset = 0; /* dummy offset for null seed context */
	u8 name[SHA256_DIGEST_SIZE + 2];
	u32 tmp_null_key;
	int rc;

	rc = tpm2_load_context(chip, chip->null_key_context, &offset,
			       &tmp_null_key);
	if (rc != -EINVAL) {
		if (!rc)
			*null_key = tmp_null_key;
		goto err;
	}

	/* Try to re-create null key, given the integrity failure: */
	rc = tpm2_create_primary(chip, TPM2_RH_NULL, &tmp_null_key, name);
	if (rc)
		goto err;

	/* Return null key if the name has not been changed: */
	if (!memcmp(name, chip->null_key_name, sizeof(name))) {
		*null_key = tmp_null_key;
		return 0;
	}

	/* Deduce from the name change TPM interference: */
	dev_err(&chip->dev, "null key integrity check failed\n");
	tpm2_flush_context(chip, tmp_null_key);

err:
	if (rc) {
		chip->flags |= TPM_CHIP_FLAG_DISABLE;
		rc = -ENODEV;
	}
	return rc;
}

/**
 * tpm2_start_auth_session() - Create an a HMAC authentication session
 * @chip:	A TPM chip
 *
 * Loads the ephemeral key (null seed), and starts an HMAC authenticated
 * session. The null seed is flushed before the return.
 *
 * Returns zero on success, or a POSIX error code.
 */
int tpm2_start_auth_session(struct tpm_chip *chip)
{
	struct tpm2_auth *auth;
	struct tpm_buf buf;
	u32 null_key;
	int rc;

	if (chip->auth) {
		dev_dbg_once(&chip->dev, "auth session is active\n");
		return 0;
	}

	auth = kzalloc(sizeof(*auth), GFP_KERNEL);
	if (!auth)
		return -ENOMEM;

	rc = tpm2_load_null(chip, &null_key);
	if (rc)
		goto out;

	auth->session = TPM_HEADER_SIZE;

	rc = tpm_buf_init(&buf, TPM2_ST_NO_SESSIONS, TPM2_CC_START_AUTH_SESS);
	if (rc)
		goto out;

	/* salt key handle */
	tpm_buf_append_u32(&buf, null_key);
	/* bind key handle */
	tpm_buf_append_u32(&buf, TPM2_RH_NULL);
	/* nonce caller */
	get_random_bytes(auth->our_nonce, sizeof(auth->our_nonce));
	tpm_buf_append_u16(&buf, sizeof(auth->our_nonce));
	tpm_buf_append(&buf, auth->our_nonce, sizeof(auth->our_nonce));

	/* append encrypted salt and squirrel away unencrypted in auth */
	tpm_buf_append_salt(&buf, chip, auth);
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

	rc = tpm_ret_to_err(tpm_transmit_cmd(chip, &buf, 0, "StartAuthSession"));
	tpm2_flush_context(chip, null_key);

	if (rc == TPM2_RC_SUCCESS)
		rc = tpm2_parse_start_auth_session(auth, &buf);

	tpm_buf_destroy(&buf);

	if (rc == TPM2_RC_SUCCESS) {
		chip->auth = auth;
		return 0;
	}

out:
	kfree_sensitive(auth);
	return rc;
}
EXPORT_SYMBOL(tpm2_start_auth_session);

/*
 * A mask containing the object attributes for the kernel held null primary key
 * used in HMAC encryption. For more information on specific attributes look up
 * to "8.3 TPMA_OBJECT (Object Attributes)".
 */
#define TPM2_OA_NULL_KEY ( \
	TPM2_OA_NO_DA | \
	TPM2_OA_FIXED_TPM | \
	TPM2_OA_FIXED_PARENT | \
	TPM2_OA_SENSITIVE_DATA_ORIGIN |	\
	TPM2_OA_USER_WITH_AUTH | \
	TPM2_OA_DECRYPT | \
	TPM2_OA_RESTRICTED)

/**
 * tpm2_parse_create_primary() - parse the data returned from TPM_CC_CREATE_PRIMARY
 *
 * @chip:	The TPM the primary was created under
 * @buf:	The response buffer from the chip
 * @handle:	pointer to be filled in with the return handle of the primary
 * @hierarchy:	The hierarchy the primary was created for
 * @name:	pointer to be filled in with the primary key name
 *
 * Return:
 * * 0		- OK
 * * -errno	- A system error
 * * TPM_RC	- A TPM error
 */
static int tpm2_parse_create_primary(struct tpm_chip *chip, struct tpm_buf *buf,
				     u32 *handle, u32 hierarchy, u8 *name)
{
	struct tpm_header *head = (struct tpm_header *)buf->data;
	off_t offset_r = TPM_HEADER_SIZE, offset_t;
	u16 len = TPM_HEADER_SIZE;
	u32 total_len = be32_to_cpu(head->length);
	u32 val, param_len, keyhandle;

	keyhandle = tpm_buf_read_u32(buf, &offset_r);
	if (handle)
		*handle = keyhandle;
	else
		tpm2_flush_context(chip, keyhandle);

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
	if (name) {
		/*
		 * now we have the public area, compute the name of
		 * the object
		 */
		put_unaligned_be16(TPM_ALG_SHA256, name);
		sha256(&buf->data[offset_r], len, name + 2);
	}

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
	if (val != TPM2_OA_NULL_KEY)
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
 * @name:      The name of the returned key
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
			       u32 *handle, u8 *name)
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
	tpm_buf_append_u32(&template, TPM2_OA_NULL_KEY);

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
		rc = tpm2_parse_create_primary(chip, &buf, handle, hierarchy,
					       name);

	tpm_buf_destroy(&buf);

	return rc;
}

static int tpm2_create_null_primary(struct tpm_chip *chip)
{
	u32 null_key;
	int rc;

	rc = tpm2_create_primary(chip, TPM2_RH_NULL, &null_key,
				 chip->null_key_name);

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
 *
 * Return:
 * * 0		- OK
 * * -errno	- A system error
 * * TPM_RC	- A TPM error
 */
int tpm2_sessions_init(struct tpm_chip *chip)
{
	int rc;

	rc = tpm2_create_null_primary(chip);
	if (rc) {
		dev_err(&chip->dev, "null key creation failed with %d\n", rc);
		return rc;
	}

	return rc;
}
#endif /* CONFIG_TCG_TPM2_HMAC */
