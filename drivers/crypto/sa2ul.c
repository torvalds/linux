// SPDX-License-Identifier: GPL-2.0
/*
 * K3 SA2UL crypto accelerator driver
 *
 * Copyright (C) 2018-2020 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors:	Keerthy
 *		Vitaly Andrianov
 *		Tero Kristo
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <crypto/aes.h>
#include <crypto/authenc.h>
#include <crypto/des.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>

#include "sa2ul.h"

/* Byte offset for key in encryption security context */
#define SC_ENC_KEY_OFFSET (1 + 27 + 4)
/* Byte offset for Aux-1 in encryption security context */
#define SC_ENC_AUX1_OFFSET (1 + 27 + 4 + 32)

#define SA_CMDL_UPD_ENC         0x0001
#define SA_CMDL_UPD_AUTH        0x0002
#define SA_CMDL_UPD_ENC_IV      0x0004
#define SA_CMDL_UPD_AUTH_IV     0x0008
#define SA_CMDL_UPD_AUX_KEY     0x0010

#define SA_AUTH_SUBKEY_LEN	16
#define SA_CMDL_PAYLOAD_LENGTH_MASK	0xFFFF
#define SA_CMDL_SOP_BYPASS_LEN_MASK	0xFF000000

#define MODE_CONTROL_BYTES	27
#define SA_HASH_PROCESSING	0
#define SA_CRYPTO_PROCESSING	0
#define SA_UPLOAD_HASH_TO_TLR	BIT(6)

#define SA_SW0_FLAGS_MASK	0xF0000
#define SA_SW0_CMDL_INFO_MASK	0x1F00000
#define SA_SW0_CMDL_PRESENT	BIT(4)
#define SA_SW0_ENG_ID_MASK	0x3E000000
#define SA_SW0_DEST_INFO_PRESENT	BIT(30)
#define SA_SW2_EGRESS_LENGTH		0xFF000000
#define SA_BASIC_HASH		0x10

#define SHA256_DIGEST_WORDS    8
/* Make 32-bit word from 4 bytes */
#define SA_MK_U32(b0, b1, b2, b3) (((b0) << 24) | ((b1) << 16) | \
				   ((b2) << 8) | (b3))

/* size of SCCTL structure in bytes */
#define SA_SCCTL_SZ 16

/* Max Authentication tag size */
#define SA_MAX_AUTH_TAG_SZ 64

enum sa_algo_id {
	SA_ALG_CBC_AES = 0,
	SA_ALG_EBC_AES,
	SA_ALG_CBC_DES3,
	SA_ALG_ECB_DES3,
	SA_ALG_SHA1,
	SA_ALG_SHA256,
	SA_ALG_SHA512,
	SA_ALG_AUTHENC_SHA1_AES,
	SA_ALG_AUTHENC_SHA256_AES,
};

struct sa_match_data {
	u8 priv;
	u8 priv_id;
	u32 supported_algos;
};

static struct device *sa_k3_dev;

/**
 * struct sa_cmdl_cfg - Command label configuration descriptor
 * @aalg: authentication algorithm ID
 * @enc_eng_id: Encryption Engine ID supported by the SA hardware
 * @auth_eng_id: Authentication Engine ID
 * @iv_size: Initialization Vector size
 * @akey: Authentication key
 * @akey_len: Authentication key length
 * @enc: True, if this is an encode request
 */
struct sa_cmdl_cfg {
	int aalg;
	u8 enc_eng_id;
	u8 auth_eng_id;
	u8 iv_size;
	const u8 *akey;
	u16 akey_len;
	bool enc;
};

/**
 * struct algo_data - Crypto algorithm specific data
 * @enc_eng: Encryption engine info structure
 * @auth_eng: Authentication engine info structure
 * @auth_ctrl: Authentication control word
 * @hash_size: Size of digest
 * @iv_idx: iv index in psdata
 * @iv_out_size: iv out size
 * @ealg_id: Encryption Algorithm ID
 * @aalg_id: Authentication algorithm ID
 * @mci_enc: Mode Control Instruction for Encryption algorithm
 * @mci_dec: Mode Control Instruction for Decryption
 * @inv_key: Whether the encryption algorithm demands key inversion
 * @ctx: Pointer to the algorithm context
 * @keyed_mac: Whether the authentication algorithm has key
 * @prep_iopad: Function pointer to generate intermediate ipad/opad
 */
struct algo_data {
	struct sa_eng_info enc_eng;
	struct sa_eng_info auth_eng;
	u8 auth_ctrl;
	u8 hash_size;
	u8 iv_idx;
	u8 iv_out_size;
	u8 ealg_id;
	u8 aalg_id;
	u8 *mci_enc;
	u8 *mci_dec;
	bool inv_key;
	struct sa_tfm_ctx *ctx;
	bool keyed_mac;
	void (*prep_iopad)(struct algo_data *algo, const u8 *key,
			   u16 key_sz, __be32 *ipad, __be32 *opad);
};

/**
 * struct sa_alg_tmpl: A generic template encompassing crypto/aead algorithms
 * @type: Type of the crypto algorithm.
 * @alg: Union of crypto algorithm definitions.
 * @registered: Flag indicating if the crypto algorithm is already registered
 */
struct sa_alg_tmpl {
	u32 type;		/* CRYPTO_ALG_TYPE from <linux/crypto.h> */
	union {
		struct skcipher_alg skcipher;
		struct ahash_alg ahash;
		struct aead_alg aead;
	} alg;
	bool registered;
};

/**
 * struct sa_mapped_sg: scatterlist information for tx and rx
 * @mapped: Set to true if the @sgt is mapped
 * @dir: mapping direction used for @sgt
 * @split_sg: Set if the sg is split and needs to be freed up
 * @static_sg: Static scatterlist entry for overriding data
 * @sgt: scatterlist table for DMA API use
 */
struct sa_mapped_sg {
	bool mapped;
	enum dma_data_direction dir;
	struct scatterlist static_sg;
	struct scatterlist *split_sg;
	struct sg_table sgt;
};
/**
 * struct sa_rx_data: RX Packet miscellaneous data place holder
 * @req: crypto request data pointer
 * @ddev: pointer to the DMA device
 * @tx_in: dma_async_tx_descriptor pointer for rx channel
 * @mapped_sg: Information on tx (0) and rx (1) scatterlist DMA mapping
 * @enc: Flag indicating either encryption or decryption
 * @enc_iv_size: Initialisation vector size
 * @iv_idx: Initialisation vector index
 */
struct sa_rx_data {
	void *req;
	struct device *ddev;
	struct dma_async_tx_descriptor *tx_in;
	struct sa_mapped_sg mapped_sg[2];
	u8 enc;
	u8 enc_iv_size;
	u8 iv_idx;
};

/**
 * struct sa_req: SA request definition
 * @dev: device for the request
 * @size: total data to the xmitted via DMA
 * @enc_offset: offset of cipher data
 * @enc_size: data to be passed to cipher engine
 * @enc_iv: cipher IV
 * @auth_offset: offset of the authentication data
 * @auth_size: size of the authentication data
 * @auth_iv: authentication IV
 * @type: algorithm type for the request
 * @cmdl: command label pointer
 * @base: pointer to the base request
 * @ctx: pointer to the algorithm context data
 * @enc: true if this is an encode request
 * @src: source data
 * @dst: destination data
 * @callback: DMA callback for the request
 * @mdata_size: metadata size passed to DMA
 */
struct sa_req {
	struct device *dev;
	u16 size;
	u8 enc_offset;
	u16 enc_size;
	u8 *enc_iv;
	u8 auth_offset;
	u16 auth_size;
	u8 *auth_iv;
	u32 type;
	u32 *cmdl;
	struct crypto_async_request *base;
	struct sa_tfm_ctx *ctx;
	bool enc;
	struct scatterlist *src;
	struct scatterlist *dst;
	dma_async_tx_callback callback;
	u16 mdata_size;
};

/*
 * Mode Control Instructions for various Key lengths 128, 192, 256
 * For CBC (Cipher Block Chaining) mode for encryption
 */
static u8 mci_cbc_enc_array[3][MODE_CONTROL_BYTES] = {
	{	0x61, 0x00, 0x00, 0x18, 0x88, 0x0a, 0xaa, 0x4b, 0x7e, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
	{	0x61, 0x00, 0x00, 0x18, 0x88, 0x4a, 0xaa, 0x4b, 0x7e, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
	{	0x61, 0x00, 0x00, 0x18, 0x88, 0x8a, 0xaa, 0x4b, 0x7e, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
};

/*
 * Mode Control Instructions for various Key lengths 128, 192, 256
 * For CBC (Cipher Block Chaining) mode for decryption
 */
static u8 mci_cbc_dec_array[3][MODE_CONTROL_BYTES] = {
	{	0x71, 0x00, 0x00, 0x80, 0x8a, 0xca, 0x98, 0xf4, 0x40, 0xc0,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
	{	0x71, 0x00, 0x00, 0x84, 0x8a, 0xca, 0x98, 0xf4, 0x40, 0xc0,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
	{	0x71, 0x00, 0x00, 0x88, 0x8a, 0xca, 0x98, 0xf4, 0x40, 0xc0,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
};

/*
 * Mode Control Instructions for various Key lengths 128, 192, 256
 * For CBC (Cipher Block Chaining) mode for encryption
 */
static u8 mci_cbc_enc_no_iv_array[3][MODE_CONTROL_BYTES] = {
	{	0x21, 0x00, 0x00, 0x18, 0x88, 0x0a, 0xaa, 0x4b, 0x7e, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
	{	0x21, 0x00, 0x00, 0x18, 0x88, 0x4a, 0xaa, 0x4b, 0x7e, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
	{	0x21, 0x00, 0x00, 0x18, 0x88, 0x8a, 0xaa, 0x4b, 0x7e, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
};

/*
 * Mode Control Instructions for various Key lengths 128, 192, 256
 * For CBC (Cipher Block Chaining) mode for decryption
 */
static u8 mci_cbc_dec_no_iv_array[3][MODE_CONTROL_BYTES] = {
	{	0x31, 0x00, 0x00, 0x80, 0x8a, 0xca, 0x98, 0xf4, 0x40, 0xc0,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
	{	0x31, 0x00, 0x00, 0x84, 0x8a, 0xca, 0x98, 0xf4, 0x40, 0xc0,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
	{	0x31, 0x00, 0x00, 0x88, 0x8a, 0xca, 0x98, 0xf4, 0x40, 0xc0,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
};

/*
 * Mode Control Instructions for various Key lengths 128, 192, 256
 * For ECB (Electronic Code Book) mode for encryption
 */
static u8 mci_ecb_enc_array[3][27] = {
	{	0x21, 0x00, 0x00, 0x80, 0x8a, 0x04, 0xb7, 0x90, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
	{	0x21, 0x00, 0x00, 0x84, 0x8a, 0x04, 0xb7, 0x90, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
	{	0x21, 0x00, 0x00, 0x88, 0x8a, 0x04, 0xb7, 0x90, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
};

/*
 * Mode Control Instructions for various Key lengths 128, 192, 256
 * For ECB (Electronic Code Book) mode for decryption
 */
static u8 mci_ecb_dec_array[3][27] = {
	{	0x31, 0x00, 0x00, 0x80, 0x8a, 0x04, 0xb7, 0x90, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
	{	0x31, 0x00, 0x00, 0x84, 0x8a, 0x04, 0xb7, 0x90, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
	{	0x31, 0x00, 0x00, 0x88, 0x8a, 0x04, 0xb7, 0x90, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	},
};

/*
 * Mode Control Instructions for DES algorithm
 * For CBC (Cipher Block Chaining) mode and ECB mode
 * encryption and for decryption respectively
 */
static u8 mci_cbc_3des_enc_array[MODE_CONTROL_BYTES] = {
	0x60, 0x00, 0x00, 0x18, 0x88, 0x52, 0xaa, 0x4b, 0x7e, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00,
};

static u8 mci_cbc_3des_dec_array[MODE_CONTROL_BYTES] = {
	0x70, 0x00, 0x00, 0x85, 0x0a, 0xca, 0x98, 0xf4, 0x40, 0xc0, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00,
};

static u8 mci_ecb_3des_enc_array[MODE_CONTROL_BYTES] = {
	0x20, 0x00, 0x00, 0x85, 0x0a, 0x04, 0xb7, 0x90, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00,
};

static u8 mci_ecb_3des_dec_array[MODE_CONTROL_BYTES] = {
	0x30, 0x00, 0x00, 0x85, 0x0a, 0x04, 0xb7, 0x90, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00,
};

/*
 * Perform 16 byte or 128 bit swizzling
 * The SA2UL Expects the security context to
 * be in little Endian and the bus width is 128 bits or 16 bytes
 * Hence swap 16 bytes at a time from higher to lower address
 */
static void sa_swiz_128(u8 *in, u16 len)
{
	u8 data[16];
	int i, j;

	for (i = 0; i < len; i += 16) {
		memcpy(data, &in[i], 16);
		for (j = 0; j < 16; j++)
			in[i + j] = data[15 - j];
	}
}

/* Prepare the ipad and opad from key as per SHA algorithm step 1*/
static void prepare_kipad(u8 *k_ipad, const u8 *key, u16 key_sz)
{
	int i;

	for (i = 0; i < key_sz; i++)
		k_ipad[i] = key[i] ^ 0x36;

	/* Instead of XOR with 0 */
	for (; i < SHA1_BLOCK_SIZE; i++)
		k_ipad[i] = 0x36;
}

static void prepare_kopad(u8 *k_opad, const u8 *key, u16 key_sz)
{
	int i;

	for (i = 0; i < key_sz; i++)
		k_opad[i] = key[i] ^ 0x5c;

	/* Instead of XOR with 0 */
	for (; i < SHA1_BLOCK_SIZE; i++)
		k_opad[i] = 0x5c;
}

static void sa_export_shash(void *state, struct shash_desc *hash,
			    int digest_size, __be32 *out)
{
	struct sha1_state *sha1;
	struct sha256_state *sha256;
	u32 *result;

	switch (digest_size) {
	case SHA1_DIGEST_SIZE:
		sha1 = state;
		result = sha1->state;
		break;
	case SHA256_DIGEST_SIZE:
		sha256 = state;
		result = sha256->state;
		break;
	default:
		dev_err(sa_k3_dev, "%s: bad digest_size=%d\n", __func__,
			digest_size);
		return;
	}

	crypto_shash_export(hash, state);

	cpu_to_be32_array(out, result, digest_size / 4);
}

static void sa_prepare_iopads(struct algo_data *data, const u8 *key,
			      u16 key_sz, __be32 *ipad, __be32 *opad)
{
	SHASH_DESC_ON_STACK(shash, data->ctx->shash);
	int block_size = crypto_shash_blocksize(data->ctx->shash);
	int digest_size = crypto_shash_digestsize(data->ctx->shash);
	union {
		struct sha1_state sha1;
		struct sha256_state sha256;
		u8 k_pad[SHA1_BLOCK_SIZE];
	} sha;

	shash->tfm = data->ctx->shash;

	prepare_kipad(sha.k_pad, key, key_sz);

	crypto_shash_init(shash);
	crypto_shash_update(shash, sha.k_pad, block_size);
	sa_export_shash(&sha, shash, digest_size, ipad);

	prepare_kopad(sha.k_pad, key, key_sz);

	crypto_shash_init(shash);
	crypto_shash_update(shash, sha.k_pad, block_size);

	sa_export_shash(&sha, shash, digest_size, opad);

	memzero_explicit(&sha, sizeof(sha));
}

/* Derive the inverse key used in AES-CBC decryption operation */
static inline int sa_aes_inv_key(u8 *inv_key, const u8 *key, u16 key_sz)
{
	struct crypto_aes_ctx ctx;
	int key_pos;

	if (aes_expandkey(&ctx, key, key_sz)) {
		dev_err(sa_k3_dev, "%s: bad key len(%d)\n", __func__, key_sz);
		return -EINVAL;
	}

	/* work around to get the right inverse for AES_KEYSIZE_192 size keys */
	if (key_sz == AES_KEYSIZE_192) {
		ctx.key_enc[52] = ctx.key_enc[51] ^ ctx.key_enc[46];
		ctx.key_enc[53] = ctx.key_enc[52] ^ ctx.key_enc[47];
	}

	/* Based crypto_aes_expand_key logic */
	switch (key_sz) {
	case AES_KEYSIZE_128:
	case AES_KEYSIZE_192:
		key_pos = key_sz + 24;
		break;

	case AES_KEYSIZE_256:
		key_pos = key_sz + 24 - 4;
		break;

	default:
		dev_err(sa_k3_dev, "%s: bad key len(%d)\n", __func__, key_sz);
		return -EINVAL;
	}

	memcpy(inv_key, &ctx.key_enc[key_pos], key_sz);
	return 0;
}

/* Set Security context for the encryption engine */
static int sa_set_sc_enc(struct algo_data *ad, const u8 *key, u16 key_sz,
			 u8 enc, u8 *sc_buf)
{
	const u8 *mci = NULL;

	/* Set Encryption mode selector to crypto processing */
	sc_buf[0] = SA_CRYPTO_PROCESSING;

	if (enc)
		mci = ad->mci_enc;
	else
		mci = ad->mci_dec;
	/* Set the mode control instructions in security context */
	if (mci)
		memcpy(&sc_buf[1], mci, MODE_CONTROL_BYTES);

	/* For AES-CBC decryption get the inverse key */
	if (ad->inv_key && !enc) {
		if (sa_aes_inv_key(&sc_buf[SC_ENC_KEY_OFFSET], key, key_sz))
			return -EINVAL;
	/* For all other cases: key is used */
	} else {
		memcpy(&sc_buf[SC_ENC_KEY_OFFSET], key, key_sz);
	}

	return 0;
}

/* Set Security context for the authentication engine */
static void sa_set_sc_auth(struct algo_data *ad, const u8 *key, u16 key_sz,
			   u8 *sc_buf)
{
	__be32 *ipad = (void *)(sc_buf + 32);
	__be32 *opad = (void *)(sc_buf + 64);

	/* Set Authentication mode selector to hash processing */
	sc_buf[0] = SA_HASH_PROCESSING;
	/* Auth SW ctrl word: bit[6]=1 (upload computed hash to TLR section) */
	sc_buf[1] = SA_UPLOAD_HASH_TO_TLR;
	sc_buf[1] |= ad->auth_ctrl;

	/* Copy the keys or ipad/opad */
	if (ad->keyed_mac)
		ad->prep_iopad(ad, key, key_sz, ipad, opad);
	else {
		/* basic hash */
		sc_buf[1] |= SA_BASIC_HASH;
	}
}

static inline void sa_copy_iv(__be32 *out, const u8 *iv, bool size16)
{
	int j;

	for (j = 0; j < ((size16) ? 4 : 2); j++) {
		*out = cpu_to_be32(*((u32 *)iv));
		iv += 4;
		out++;
	}
}

/* Format general command label */
static int sa_format_cmdl_gen(struct sa_cmdl_cfg *cfg, u8 *cmdl,
			      struct sa_cmdl_upd_info *upd_info)
{
	u8 enc_offset = 0, auth_offset = 0, total = 0;
	u8 enc_next_eng = SA_ENG_ID_OUTPORT2;
	u8 auth_next_eng = SA_ENG_ID_OUTPORT2;
	u32 *word_ptr = (u32 *)cmdl;
	int i;

	/* Clear the command label */
	memzero_explicit(cmdl, (SA_MAX_CMDL_WORDS * sizeof(u32)));

	/* Iniialize the command update structure */
	memzero_explicit(upd_info, sizeof(*upd_info));

	if (cfg->enc_eng_id && cfg->auth_eng_id) {
		if (cfg->enc) {
			auth_offset = SA_CMDL_HEADER_SIZE_BYTES;
			enc_next_eng = cfg->auth_eng_id;

			if (cfg->iv_size)
				auth_offset += cfg->iv_size;
		} else {
			enc_offset = SA_CMDL_HEADER_SIZE_BYTES;
			auth_next_eng = cfg->enc_eng_id;
		}
	}

	if (cfg->enc_eng_id) {
		upd_info->flags |= SA_CMDL_UPD_ENC;
		upd_info->enc_size.index = enc_offset >> 2;
		upd_info->enc_offset.index = upd_info->enc_size.index + 1;
		/* Encryption command label */
		cmdl[enc_offset + SA_CMDL_OFFSET_NESC] = enc_next_eng;

		/* Encryption modes requiring IV */
		if (cfg->iv_size) {
			upd_info->flags |= SA_CMDL_UPD_ENC_IV;
			upd_info->enc_iv.index =
				(enc_offset + SA_CMDL_HEADER_SIZE_BYTES) >> 2;
			upd_info->enc_iv.size = cfg->iv_size;

			cmdl[enc_offset + SA_CMDL_OFFSET_LABEL_LEN] =
				SA_CMDL_HEADER_SIZE_BYTES + cfg->iv_size;

			cmdl[enc_offset + SA_CMDL_OFFSET_OPTION_CTRL1] =
				(SA_CTX_ENC_AUX2_OFFSET | (cfg->iv_size >> 3));
			total += SA_CMDL_HEADER_SIZE_BYTES + cfg->iv_size;
		} else {
			cmdl[enc_offset + SA_CMDL_OFFSET_LABEL_LEN] =
						SA_CMDL_HEADER_SIZE_BYTES;
			total += SA_CMDL_HEADER_SIZE_BYTES;
		}
	}

	if (cfg->auth_eng_id) {
		upd_info->flags |= SA_CMDL_UPD_AUTH;
		upd_info->auth_size.index = auth_offset >> 2;
		upd_info->auth_offset.index = upd_info->auth_size.index + 1;
		cmdl[auth_offset + SA_CMDL_OFFSET_NESC] = auth_next_eng;
		cmdl[auth_offset + SA_CMDL_OFFSET_LABEL_LEN] =
			SA_CMDL_HEADER_SIZE_BYTES;
		total += SA_CMDL_HEADER_SIZE_BYTES;
	}

	total = roundup(total, 8);

	for (i = 0; i < total / 4; i++)
		word_ptr[i] = swab32(word_ptr[i]);

	return total;
}

/* Update Command label */
static inline void sa_update_cmdl(struct sa_req *req, u32 *cmdl,
				  struct sa_cmdl_upd_info *upd_info)
{
	int i = 0, j;

	if (likely(upd_info->flags & SA_CMDL_UPD_ENC)) {
		cmdl[upd_info->enc_size.index] &= ~SA_CMDL_PAYLOAD_LENGTH_MASK;
		cmdl[upd_info->enc_size.index] |= req->enc_size;
		cmdl[upd_info->enc_offset.index] &=
						~SA_CMDL_SOP_BYPASS_LEN_MASK;
		cmdl[upd_info->enc_offset.index] |=
			FIELD_PREP(SA_CMDL_SOP_BYPASS_LEN_MASK,
				   req->enc_offset);

		if (likely(upd_info->flags & SA_CMDL_UPD_ENC_IV)) {
			__be32 *data = (__be32 *)&cmdl[upd_info->enc_iv.index];
			u32 *enc_iv = (u32 *)req->enc_iv;

			for (j = 0; i < upd_info->enc_iv.size; i += 4, j++) {
				data[j] = cpu_to_be32(*enc_iv);
				enc_iv++;
			}
		}
	}

	if (likely(upd_info->flags & SA_CMDL_UPD_AUTH)) {
		cmdl[upd_info->auth_size.index] &= ~SA_CMDL_PAYLOAD_LENGTH_MASK;
		cmdl[upd_info->auth_size.index] |= req->auth_size;
		cmdl[upd_info->auth_offset.index] &=
			~SA_CMDL_SOP_BYPASS_LEN_MASK;
		cmdl[upd_info->auth_offset.index] |=
			FIELD_PREP(SA_CMDL_SOP_BYPASS_LEN_MASK,
				   req->auth_offset);
		if (upd_info->flags & SA_CMDL_UPD_AUTH_IV) {
			sa_copy_iv((void *)&cmdl[upd_info->auth_iv.index],
				   req->auth_iv,
				   (upd_info->auth_iv.size > 8));
		}
		if (upd_info->flags & SA_CMDL_UPD_AUX_KEY) {
			int offset = (req->auth_size & 0xF) ? 4 : 0;

			memcpy(&cmdl[upd_info->aux_key_info.index],
			       &upd_info->aux_key[offset], 16);
		}
	}
}

/* Format SWINFO words to be sent to SA */
static
void sa_set_swinfo(u8 eng_id, u16 sc_id, dma_addr_t sc_phys,
		   u8 cmdl_present, u8 cmdl_offset, u8 flags,
		   u8 hash_size, u32 *swinfo)
{
	swinfo[0] = sc_id;
	swinfo[0] |= FIELD_PREP(SA_SW0_FLAGS_MASK, flags);
	if (likely(cmdl_present))
		swinfo[0] |= FIELD_PREP(SA_SW0_CMDL_INFO_MASK,
					cmdl_offset | SA_SW0_CMDL_PRESENT);
	swinfo[0] |= FIELD_PREP(SA_SW0_ENG_ID_MASK, eng_id);

	swinfo[0] |= SA_SW0_DEST_INFO_PRESENT;
	swinfo[1] = (u32)(sc_phys & 0xFFFFFFFFULL);
	swinfo[2] = (u32)((sc_phys & 0xFFFFFFFF00000000ULL) >> 32);
	swinfo[2] |= FIELD_PREP(SA_SW2_EGRESS_LENGTH, hash_size);
}

/* Dump the security context */
static void sa_dump_sc(u8 *buf, dma_addr_t dma_addr)
{
#ifdef DEBUG
	dev_info(sa_k3_dev, "Security context dump:: 0x%pad\n", &dma_addr);
	print_hex_dump(KERN_CONT, "", DUMP_PREFIX_OFFSET,
		       16, 1, buf, SA_CTX_MAX_SZ, false);
#endif
}

static
int sa_init_sc(struct sa_ctx_info *ctx, const struct sa_match_data *match_data,
	       const u8 *enc_key, u16 enc_key_sz,
	       const u8 *auth_key, u16 auth_key_sz,
	       struct algo_data *ad, u8 enc, u32 *swinfo)
{
	int enc_sc_offset = 0;
	int auth_sc_offset = 0;
	u8 *sc_buf = ctx->sc;
	u16 sc_id = ctx->sc_id;
	u8 first_engine = 0;

	memzero_explicit(sc_buf, SA_CTX_MAX_SZ);

	if (ad->auth_eng.eng_id) {
		if (enc)
			first_engine = ad->enc_eng.eng_id;
		else
			first_engine = ad->auth_eng.eng_id;

		enc_sc_offset = SA_CTX_PHP_PE_CTX_SZ;
		auth_sc_offset = enc_sc_offset + ad->enc_eng.sc_size;
		sc_buf[1] = SA_SCCTL_FE_AUTH_ENC;
		if (!ad->hash_size)
			return -EINVAL;
		ad->hash_size = roundup(ad->hash_size, 8);

	} else if (ad->enc_eng.eng_id && !ad->auth_eng.eng_id) {
		enc_sc_offset = SA_CTX_PHP_PE_CTX_SZ;
		first_engine = ad->enc_eng.eng_id;
		sc_buf[1] = SA_SCCTL_FE_ENC;
		ad->hash_size = ad->iv_out_size;
	}

	/* SCCTL Owner info: 0=host, 1=CP_ACE */
	sc_buf[SA_CTX_SCCTL_OWNER_OFFSET] = 0;
	memcpy(&sc_buf[2], &sc_id, 2);
	sc_buf[4] = 0x0;
	sc_buf[5] = match_data->priv_id;
	sc_buf[6] = match_data->priv;
	sc_buf[7] = 0x0;

	/* Prepare context for encryption engine */
	if (ad->enc_eng.sc_size) {
		if (sa_set_sc_enc(ad, enc_key, enc_key_sz, enc,
				  &sc_buf[enc_sc_offset]))
			return -EINVAL;
	}

	/* Prepare context for authentication engine */
	if (ad->auth_eng.sc_size)
		sa_set_sc_auth(ad, auth_key, auth_key_sz,
			       &sc_buf[auth_sc_offset]);

	/* Set the ownership of context to CP_ACE */
	sc_buf[SA_CTX_SCCTL_OWNER_OFFSET] = 0x80;

	/* swizzle the security context */
	sa_swiz_128(sc_buf, SA_CTX_MAX_SZ);

	sa_set_swinfo(first_engine, ctx->sc_id, ctx->sc_phys, 1, 0,
		      SA_SW_INFO_FLAG_EVICT, ad->hash_size, swinfo);

	sa_dump_sc(sc_buf, ctx->sc_phys);

	return 0;
}

/* Free the per direction context memory */
static void sa_free_ctx_info(struct sa_ctx_info *ctx,
			     struct sa_crypto_data *data)
{
	unsigned long bn;

	bn = ctx->sc_id - data->sc_id_start;
	spin_lock(&data->scid_lock);
	__clear_bit(bn, data->ctx_bm);
	data->sc_id--;
	spin_unlock(&data->scid_lock);

	if (ctx->sc) {
		dma_pool_free(data->sc_pool, ctx->sc, ctx->sc_phys);
		ctx->sc = NULL;
	}
}

static int sa_init_ctx_info(struct sa_ctx_info *ctx,
			    struct sa_crypto_data *data)
{
	unsigned long bn;
	int err;

	spin_lock(&data->scid_lock);
	bn = find_first_zero_bit(data->ctx_bm, SA_MAX_NUM_CTX);
	__set_bit(bn, data->ctx_bm);
	data->sc_id++;
	spin_unlock(&data->scid_lock);

	ctx->sc_id = (u16)(data->sc_id_start + bn);

	ctx->sc = dma_pool_alloc(data->sc_pool, GFP_KERNEL, &ctx->sc_phys);
	if (!ctx->sc) {
		dev_err(&data->pdev->dev, "Failed to allocate SC memory\n");
		err = -ENOMEM;
		goto scid_rollback;
	}

	return 0;

scid_rollback:
	spin_lock(&data->scid_lock);
	__clear_bit(bn, data->ctx_bm);
	data->sc_id--;
	spin_unlock(&data->scid_lock);

	return err;
}

static void sa_cipher_cra_exit(struct crypto_skcipher *tfm)
{
	struct sa_tfm_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct sa_crypto_data *data = dev_get_drvdata(sa_k3_dev);

	dev_dbg(sa_k3_dev, "%s(0x%p) sc-ids(0x%x(0x%pad), 0x%x(0x%pad))\n",
		__func__, tfm, ctx->enc.sc_id, &ctx->enc.sc_phys,
		ctx->dec.sc_id, &ctx->dec.sc_phys);

	sa_free_ctx_info(&ctx->enc, data);
	sa_free_ctx_info(&ctx->dec, data);

	crypto_free_skcipher(ctx->fallback.skcipher);
}

static int sa_cipher_cra_init(struct crypto_skcipher *tfm)
{
	struct sa_tfm_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct sa_crypto_data *data = dev_get_drvdata(sa_k3_dev);
	const char *name = crypto_tfm_alg_name(&tfm->base);
	struct crypto_skcipher *child;
	int ret;

	memzero_explicit(ctx, sizeof(*ctx));
	ctx->dev_data = data;

	ret = sa_init_ctx_info(&ctx->enc, data);
	if (ret)
		return ret;
	ret = sa_init_ctx_info(&ctx->dec, data);
	if (ret) {
		sa_free_ctx_info(&ctx->enc, data);
		return ret;
	}

	child = crypto_alloc_skcipher(name, 0, CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(child)) {
		dev_err(sa_k3_dev, "Error allocating fallback algo %s\n", name);
		return PTR_ERR(child);
	}

	ctx->fallback.skcipher = child;
	crypto_skcipher_set_reqsize(tfm, crypto_skcipher_reqsize(child) +
					 sizeof(struct skcipher_request));

	dev_dbg(sa_k3_dev, "%s(0x%p) sc-ids(0x%x(0x%pad), 0x%x(0x%pad))\n",
		__func__, tfm, ctx->enc.sc_id, &ctx->enc.sc_phys,
		ctx->dec.sc_id, &ctx->dec.sc_phys);
	return 0;
}

static int sa_cipher_setkey(struct crypto_skcipher *tfm, const u8 *key,
			    unsigned int keylen, struct algo_data *ad)
{
	struct sa_tfm_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *child = ctx->fallback.skcipher;
	int cmdl_len;
	struct sa_cmdl_cfg cfg;
	int ret;

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;

	ad->enc_eng.eng_id = SA_ENG_ID_EM1;
	ad->enc_eng.sc_size = SA_CTX_ENC_TYPE1_SZ;

	memzero_explicit(&cfg, sizeof(cfg));
	cfg.enc_eng_id = ad->enc_eng.eng_id;
	cfg.iv_size = crypto_skcipher_ivsize(tfm);

	crypto_skcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(child, tfm->base.crt_flags &
					 CRYPTO_TFM_REQ_MASK);
	ret = crypto_skcipher_setkey(child, key, keylen);
	if (ret)
		return ret;

	/* Setup Encryption Security Context & Command label template */
	if (sa_init_sc(&ctx->enc, ctx->dev_data->match_data, key, keylen, NULL, 0,
		       ad, 1, &ctx->enc.epib[1]))
		goto badkey;

	cmdl_len = sa_format_cmdl_gen(&cfg,
				      (u8 *)ctx->enc.cmdl,
				      &ctx->enc.cmdl_upd_info);
	if (cmdl_len <= 0 || (cmdl_len > SA_MAX_CMDL_WORDS * sizeof(u32)))
		goto badkey;

	ctx->enc.cmdl_size = cmdl_len;

	/* Setup Decryption Security Context & Command label template */
	if (sa_init_sc(&ctx->dec, ctx->dev_data->match_data, key, keylen, NULL, 0,
		       ad, 0, &ctx->dec.epib[1]))
		goto badkey;

	cfg.enc_eng_id = ad->enc_eng.eng_id;
	cmdl_len = sa_format_cmdl_gen(&cfg, (u8 *)ctx->dec.cmdl,
				      &ctx->dec.cmdl_upd_info);

	if (cmdl_len <= 0 || (cmdl_len > SA_MAX_CMDL_WORDS * sizeof(u32)))
		goto badkey;

	ctx->dec.cmdl_size = cmdl_len;
	ctx->iv_idx = ad->iv_idx;

	return 0;

badkey:
	dev_err(sa_k3_dev, "%s: badkey\n", __func__);
	return -EINVAL;
}

static int sa_aes_cbc_setkey(struct crypto_skcipher *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct algo_data ad = { 0 };
	/* Convert the key size (16/24/32) to the key size index (0/1/2) */
	int key_idx = (keylen >> 3) - 2;

	if (key_idx >= 3)
		return -EINVAL;

	ad.mci_enc = mci_cbc_enc_array[key_idx];
	ad.mci_dec = mci_cbc_dec_array[key_idx];
	ad.inv_key = true;
	ad.ealg_id = SA_EALG_ID_AES_CBC;
	ad.iv_idx = 4;
	ad.iv_out_size = 16;

	return sa_cipher_setkey(tfm, key, keylen, &ad);
}

static int sa_aes_ecb_setkey(struct crypto_skcipher *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct algo_data ad = { 0 };
	/* Convert the key size (16/24/32) to the key size index (0/1/2) */
	int key_idx = (keylen >> 3) - 2;

	if (key_idx >= 3)
		return -EINVAL;

	ad.mci_enc = mci_ecb_enc_array[key_idx];
	ad.mci_dec = mci_ecb_dec_array[key_idx];
	ad.inv_key = true;
	ad.ealg_id = SA_EALG_ID_AES_ECB;

	return sa_cipher_setkey(tfm, key, keylen, &ad);
}

static int sa_3des_cbc_setkey(struct crypto_skcipher *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct algo_data ad = { 0 };

	ad.mci_enc = mci_cbc_3des_enc_array;
	ad.mci_dec = mci_cbc_3des_dec_array;
	ad.ealg_id = SA_EALG_ID_3DES_CBC;
	ad.iv_idx = 6;
	ad.iv_out_size = 8;

	return sa_cipher_setkey(tfm, key, keylen, &ad);
}

static int sa_3des_ecb_setkey(struct crypto_skcipher *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct algo_data ad = { 0 };

	ad.mci_enc = mci_ecb_3des_enc_array;
	ad.mci_dec = mci_ecb_3des_dec_array;

	return sa_cipher_setkey(tfm, key, keylen, &ad);
}

static void sa_sync_from_device(struct sa_rx_data *rxd)
{
	struct sg_table *sgt;

	if (rxd->mapped_sg[0].dir == DMA_BIDIRECTIONAL)
		sgt = &rxd->mapped_sg[0].sgt;
	else
		sgt = &rxd->mapped_sg[1].sgt;

	dma_sync_sgtable_for_cpu(rxd->ddev, sgt, DMA_FROM_DEVICE);
}

static void sa_free_sa_rx_data(struct sa_rx_data *rxd)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rxd->mapped_sg); i++) {
		struct sa_mapped_sg *mapped_sg = &rxd->mapped_sg[i];

		if (mapped_sg->mapped) {
			dma_unmap_sgtable(rxd->ddev, &mapped_sg->sgt,
					  mapped_sg->dir, 0);
			kfree(mapped_sg->split_sg);
		}
	}

	kfree(rxd);
}

static void sa_aes_dma_in_callback(void *data)
{
	struct sa_rx_data *rxd = data;
	struct skcipher_request *req;
	u32 *result;
	__be32 *mdptr;
	size_t ml, pl;
	int i;

	sa_sync_from_device(rxd);
	req = container_of(rxd->req, struct skcipher_request, base);

	if (req->iv) {
		mdptr = (__be32 *)dmaengine_desc_get_metadata_ptr(rxd->tx_in, &pl,
							       &ml);
		result = (u32 *)req->iv;

		for (i = 0; i < (rxd->enc_iv_size / 4); i++)
			result[i] = be32_to_cpu(mdptr[i + rxd->iv_idx]);
	}

	sa_free_sa_rx_data(rxd);

	skcipher_request_complete(req, 0);
}

static void
sa_prepare_tx_desc(u32 *mdptr, u32 pslen, u32 *psdata, u32 epiblen, u32 *epib)
{
	u32 *out, *in;
	int i;

	for (out = mdptr, in = epib, i = 0; i < epiblen / sizeof(u32); i++)
		*out++ = *in++;

	mdptr[4] = (0xFFFF << 16);
	for (out = &mdptr[5], in = psdata, i = 0;
	     i < pslen / sizeof(u32); i++)
		*out++ = *in++;
}

static int sa_run(struct sa_req *req)
{
	struct sa_rx_data *rxd;
	gfp_t gfp_flags;
	u32 cmdl[SA_MAX_CMDL_WORDS];
	struct sa_crypto_data *pdata = dev_get_drvdata(sa_k3_dev);
	struct device *ddev;
	struct dma_chan *dma_rx;
	int sg_nents, src_nents, dst_nents;
	struct scatterlist *src, *dst;
	size_t pl, ml, split_size;
	struct sa_ctx_info *sa_ctx = req->enc ? &req->ctx->enc : &req->ctx->dec;
	int ret;
	struct dma_async_tx_descriptor *tx_out;
	u32 *mdptr;
	bool diff_dst;
	enum dma_data_direction dir_src;
	struct sa_mapped_sg *mapped_sg;

	gfp_flags = req->base->flags & CRYPTO_TFM_REQ_MAY_SLEEP ?
		GFP_KERNEL : GFP_ATOMIC;

	rxd = kzalloc(sizeof(*rxd), gfp_flags);
	if (!rxd)
		return -ENOMEM;

	if (req->src != req->dst) {
		diff_dst = true;
		dir_src = DMA_TO_DEVICE;
	} else {
		diff_dst = false;
		dir_src = DMA_BIDIRECTIONAL;
	}

	/*
	 * SA2UL has an interesting feature where the receive DMA channel
	 * is selected based on the data passed to the engine. Within the
	 * transition range, there is also a space where it is impossible
	 * to determine where the data will end up, and this should be
	 * avoided. This will be handled by the SW fallback mechanism by
	 * the individual algorithm implementations.
	 */
	if (req->size >= 256)
		dma_rx = pdata->dma_rx2;
	else
		dma_rx = pdata->dma_rx1;

	ddev = dmaengine_get_dma_device(pdata->dma_tx);
	rxd->ddev = ddev;

	memcpy(cmdl, sa_ctx->cmdl, sa_ctx->cmdl_size);

	sa_update_cmdl(req, cmdl, &sa_ctx->cmdl_upd_info);

	if (req->type != CRYPTO_ALG_TYPE_AHASH) {
		if (req->enc)
			req->type |=
				(SA_REQ_SUBTYPE_ENC << SA_REQ_SUBTYPE_SHIFT);
		else
			req->type |=
				(SA_REQ_SUBTYPE_DEC << SA_REQ_SUBTYPE_SHIFT);
	}

	cmdl[sa_ctx->cmdl_size / sizeof(u32)] = req->type;

	/*
	 * Map the packets, first we check if the data fits into a single
	 * sg entry and use that if possible. If it does not fit, we check
	 * if we need to do sg_split to align the scatterlist data on the
	 * actual data size being processed by the crypto engine.
	 */
	src = req->src;
	sg_nents = sg_nents_for_len(src, req->size);

	split_size = req->size;

	mapped_sg = &rxd->mapped_sg[0];
	if (sg_nents == 1 && split_size <= req->src->length) {
		src = &mapped_sg->static_sg;
		src_nents = 1;
		sg_init_table(src, 1);
		sg_set_page(src, sg_page(req->src), split_size,
			    req->src->offset);

		mapped_sg->sgt.sgl = src;
		mapped_sg->sgt.orig_nents = src_nents;
		ret = dma_map_sgtable(ddev, &mapped_sg->sgt, dir_src, 0);
		if (ret) {
			kfree(rxd);
			return ret;
		}

		mapped_sg->dir = dir_src;
		mapped_sg->mapped = true;
	} else {
		mapped_sg->sgt.sgl = req->src;
		mapped_sg->sgt.orig_nents = sg_nents;
		ret = dma_map_sgtable(ddev, &mapped_sg->sgt, dir_src, 0);
		if (ret) {
			kfree(rxd);
			return ret;
		}

		mapped_sg->dir = dir_src;
		mapped_sg->mapped = true;

		ret = sg_split(mapped_sg->sgt.sgl, mapped_sg->sgt.nents, 0, 1,
			       &split_size, &src, &src_nents, gfp_flags);
		if (ret) {
			src_nents = mapped_sg->sgt.nents;
			src = mapped_sg->sgt.sgl;
		} else {
			mapped_sg->split_sg = src;
		}
	}

	dma_sync_sgtable_for_device(ddev, &mapped_sg->sgt, DMA_TO_DEVICE);

	if (!diff_dst) {
		dst_nents = src_nents;
		dst = src;
	} else {
		dst_nents = sg_nents_for_len(req->dst, req->size);
		mapped_sg = &rxd->mapped_sg[1];

		if (dst_nents == 1 && split_size <= req->dst->length) {
			dst = &mapped_sg->static_sg;
			dst_nents = 1;
			sg_init_table(dst, 1);
			sg_set_page(dst, sg_page(req->dst), split_size,
				    req->dst->offset);

			mapped_sg->sgt.sgl = dst;
			mapped_sg->sgt.orig_nents = dst_nents;
			ret = dma_map_sgtable(ddev, &mapped_sg->sgt,
					      DMA_FROM_DEVICE, 0);
			if (ret)
				goto err_cleanup;

			mapped_sg->dir = DMA_FROM_DEVICE;
			mapped_sg->mapped = true;
		} else {
			mapped_sg->sgt.sgl = req->dst;
			mapped_sg->sgt.orig_nents = dst_nents;
			ret = dma_map_sgtable(ddev, &mapped_sg->sgt,
					      DMA_FROM_DEVICE, 0);
			if (ret)
				goto err_cleanup;

			mapped_sg->dir = DMA_FROM_DEVICE;
			mapped_sg->mapped = true;

			ret = sg_split(mapped_sg->sgt.sgl, mapped_sg->sgt.nents,
				       0, 1, &split_size, &dst, &dst_nents,
				       gfp_flags);
			if (ret) {
				dst_nents = mapped_sg->sgt.nents;
				dst = mapped_sg->sgt.sgl;
			} else {
				mapped_sg->split_sg = dst;
			}
		}
	}

	rxd->tx_in = dmaengine_prep_slave_sg(dma_rx, dst, dst_nents,
					     DMA_DEV_TO_MEM,
					     DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!rxd->tx_in) {
		dev_err(pdata->dev, "IN prep_slave_sg() failed\n");
		ret = -EINVAL;
		goto err_cleanup;
	}

	rxd->req = (void *)req->base;
	rxd->enc = req->enc;
	rxd->iv_idx = req->ctx->iv_idx;
	rxd->enc_iv_size = sa_ctx->cmdl_upd_info.enc_iv.size;
	rxd->tx_in->callback = req->callback;
	rxd->tx_in->callback_param = rxd;

	tx_out = dmaengine_prep_slave_sg(pdata->dma_tx, src,
					 src_nents, DMA_MEM_TO_DEV,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	if (!tx_out) {
		dev_err(pdata->dev, "OUT prep_slave_sg() failed\n");
		ret = -EINVAL;
		goto err_cleanup;
	}

	/*
	 * Prepare metadata for DMA engine. This essentially describes the
	 * crypto algorithm to be used, data sizes, different keys etc.
	 */
	mdptr = (u32 *)dmaengine_desc_get_metadata_ptr(tx_out, &pl, &ml);

	sa_prepare_tx_desc(mdptr, (sa_ctx->cmdl_size + (SA_PSDATA_CTX_WORDS *
				   sizeof(u32))), cmdl, sizeof(sa_ctx->epib),
			   sa_ctx->epib);

	ml = sa_ctx->cmdl_size + (SA_PSDATA_CTX_WORDS * sizeof(u32));
	dmaengine_desc_set_metadata_len(tx_out, req->mdata_size);

	dmaengine_submit(tx_out);
	dmaengine_submit(rxd->tx_in);

	dma_async_issue_pending(dma_rx);
	dma_async_issue_pending(pdata->dma_tx);

	return -EINPROGRESS;

err_cleanup:
	sa_free_sa_rx_data(rxd);

	return ret;
}

static int sa_cipher_run(struct skcipher_request *req, u8 *iv, int enc)
{
	struct sa_tfm_ctx *ctx =
	    crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	struct crypto_alg *alg = req->base.tfm->__crt_alg;
	struct sa_req sa_req = { 0 };

	if (!req->cryptlen)
		return 0;

	if (req->cryptlen % alg->cra_blocksize)
		return -EINVAL;

	/* Use SW fallback if the data size is not supported */
	if (req->cryptlen > SA_MAX_DATA_SZ ||
	    (req->cryptlen >= SA_UNSAFE_DATA_SZ_MIN &&
	     req->cryptlen <= SA_UNSAFE_DATA_SZ_MAX)) {
		struct skcipher_request *subreq = skcipher_request_ctx(req);

		skcipher_request_set_tfm(subreq, ctx->fallback.skcipher);
		skcipher_request_set_callback(subreq, req->base.flags,
					      req->base.complete,
					      req->base.data);
		skcipher_request_set_crypt(subreq, req->src, req->dst,
					   req->cryptlen, req->iv);
		if (enc)
			return crypto_skcipher_encrypt(subreq);
		else
			return crypto_skcipher_decrypt(subreq);
	}

	sa_req.size = req->cryptlen;
	sa_req.enc_size = req->cryptlen;
	sa_req.src = req->src;
	sa_req.dst = req->dst;
	sa_req.enc_iv = iv;
	sa_req.type = CRYPTO_ALG_TYPE_SKCIPHER;
	sa_req.enc = enc;
	sa_req.callback = sa_aes_dma_in_callback;
	sa_req.mdata_size = 44;
	sa_req.base = &req->base;
	sa_req.ctx = ctx;

	return sa_run(&sa_req);
}

static int sa_encrypt(struct skcipher_request *req)
{
	return sa_cipher_run(req, req->iv, 1);
}

static int sa_decrypt(struct skcipher_request *req)
{
	return sa_cipher_run(req, req->iv, 0);
}

static void sa_sha_dma_in_callback(void *data)
{
	struct sa_rx_data *rxd = data;
	struct ahash_request *req;
	struct crypto_ahash *tfm;
	unsigned int authsize;
	int i;
	size_t ml, pl;
	u32 *result;
	__be32 *mdptr;

	sa_sync_from_device(rxd);
	req = container_of(rxd->req, struct ahash_request, base);
	tfm = crypto_ahash_reqtfm(req);
	authsize = crypto_ahash_digestsize(tfm);

	mdptr = (__be32 *)dmaengine_desc_get_metadata_ptr(rxd->tx_in, &pl, &ml);
	result = (u32 *)req->result;

	for (i = 0; i < (authsize / 4); i++)
		result[i] = be32_to_cpu(mdptr[i + 4]);

	sa_free_sa_rx_data(rxd);

	ahash_request_complete(req, 0);
}

static int zero_message_process(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	int sa_digest_size = crypto_ahash_digestsize(tfm);

	switch (sa_digest_size) {
	case SHA1_DIGEST_SIZE:
		memcpy(req->result, sha1_zero_message_hash, sa_digest_size);
		break;
	case SHA256_DIGEST_SIZE:
		memcpy(req->result, sha256_zero_message_hash, sa_digest_size);
		break;
	case SHA512_DIGEST_SIZE:
		memcpy(req->result, sha512_zero_message_hash, sa_digest_size);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sa_sha_run(struct ahash_request *req)
{
	struct sa_tfm_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct sa_sha_req_ctx *rctx = ahash_request_ctx(req);
	struct sa_req sa_req = { 0 };
	size_t auth_len;

	auth_len = req->nbytes;

	if (!auth_len)
		return zero_message_process(req);

	if (auth_len > SA_MAX_DATA_SZ ||
	    (auth_len >= SA_UNSAFE_DATA_SZ_MIN &&
	     auth_len <= SA_UNSAFE_DATA_SZ_MAX)) {
		struct ahash_request *subreq = &rctx->fallback_req;
		int ret = 0;

		ahash_request_set_tfm(subreq, ctx->fallback.ahash);
		subreq->base.flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;

		crypto_ahash_init(subreq);

		subreq->nbytes = auth_len;
		subreq->src = req->src;
		subreq->result = req->result;

		ret |= crypto_ahash_update(subreq);

		subreq->nbytes = 0;

		ret |= crypto_ahash_final(subreq);

		return ret;
	}

	sa_req.size = auth_len;
	sa_req.auth_size = auth_len;
	sa_req.src = req->src;
	sa_req.dst = req->src;
	sa_req.enc = true;
	sa_req.type = CRYPTO_ALG_TYPE_AHASH;
	sa_req.callback = sa_sha_dma_in_callback;
	sa_req.mdata_size = 28;
	sa_req.ctx = ctx;
	sa_req.base = &req->base;

	return sa_run(&sa_req);
}

static int sa_sha_setup(struct sa_tfm_ctx *ctx, struct  algo_data *ad)
{
	int bs = crypto_shash_blocksize(ctx->shash);
	int cmdl_len;
	struct sa_cmdl_cfg cfg;

	ad->enc_eng.sc_size = SA_CTX_ENC_TYPE1_SZ;
	ad->auth_eng.eng_id = SA_ENG_ID_AM1;
	ad->auth_eng.sc_size = SA_CTX_AUTH_TYPE2_SZ;

	memset(ctx->authkey, 0, bs);
	memset(&cfg, 0, sizeof(cfg));
	cfg.aalg = ad->aalg_id;
	cfg.enc_eng_id = ad->enc_eng.eng_id;
	cfg.auth_eng_id = ad->auth_eng.eng_id;
	cfg.iv_size = 0;
	cfg.akey = NULL;
	cfg.akey_len = 0;

	ctx->dev_data = dev_get_drvdata(sa_k3_dev);
	/* Setup Encryption Security Context & Command label template */
	if (sa_init_sc(&ctx->enc, ctx->dev_data->match_data, NULL, 0, NULL, 0,
		       ad, 0, &ctx->enc.epib[1]))
		goto badkey;

	cmdl_len = sa_format_cmdl_gen(&cfg,
				      (u8 *)ctx->enc.cmdl,
				      &ctx->enc.cmdl_upd_info);
	if (cmdl_len <= 0 || (cmdl_len > SA_MAX_CMDL_WORDS * sizeof(u32)))
		goto badkey;

	ctx->enc.cmdl_size = cmdl_len;

	return 0;

badkey:
	dev_err(sa_k3_dev, "%s: badkey\n", __func__);
	return -EINVAL;
}

static int sa_sha_cra_init_alg(struct crypto_tfm *tfm, const char *alg_base)
{
	struct sa_tfm_ctx *ctx = crypto_tfm_ctx(tfm);
	struct sa_crypto_data *data = dev_get_drvdata(sa_k3_dev);
	int ret;

	memset(ctx, 0, sizeof(*ctx));
	ctx->dev_data = data;
	ret = sa_init_ctx_info(&ctx->enc, data);
	if (ret)
		return ret;

	if (alg_base) {
		ctx->shash = crypto_alloc_shash(alg_base, 0,
						CRYPTO_ALG_NEED_FALLBACK);
		if (IS_ERR(ctx->shash)) {
			dev_err(sa_k3_dev, "base driver %s couldn't be loaded\n",
				alg_base);
			return PTR_ERR(ctx->shash);
		}
		/* for fallback */
		ctx->fallback.ahash =
			crypto_alloc_ahash(alg_base, 0,
					   CRYPTO_ALG_NEED_FALLBACK);
		if (IS_ERR(ctx->fallback.ahash)) {
			dev_err(ctx->dev_data->dev,
				"Could not load fallback driver\n");
			return PTR_ERR(ctx->fallback.ahash);
		}
	}

	dev_dbg(sa_k3_dev, "%s(0x%p) sc-ids(0x%x(0x%pad), 0x%x(0x%pad))\n",
		__func__, tfm, ctx->enc.sc_id, &ctx->enc.sc_phys,
		ctx->dec.sc_id, &ctx->dec.sc_phys);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct sa_sha_req_ctx) +
				 crypto_ahash_reqsize(ctx->fallback.ahash));

	return 0;
}

static int sa_sha_digest(struct ahash_request *req)
{
	return sa_sha_run(req);
}

static int sa_sha_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sa_sha_req_ctx *rctx = ahash_request_ctx(req);
	struct sa_tfm_ctx *ctx = crypto_ahash_ctx(tfm);

	dev_dbg(sa_k3_dev, "init: digest size: %u, rctx=%p\n",
		crypto_ahash_digestsize(tfm), rctx);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback.ahash);
	rctx->fallback_req.base.flags =
		req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_init(&rctx->fallback_req);
}

static int sa_sha_update(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sa_sha_req_ctx *rctx = ahash_request_ctx(req);
	struct sa_tfm_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback.ahash);
	rctx->fallback_req.base.flags =
		req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;

	return crypto_ahash_update(&rctx->fallback_req);
}

static int sa_sha_final(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sa_sha_req_ctx *rctx = ahash_request_ctx(req);
	struct sa_tfm_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback.ahash);
	rctx->fallback_req.base.flags =
		req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_final(&rctx->fallback_req);
}

static int sa_sha_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sa_sha_req_ctx *rctx = ahash_request_ctx(req);
	struct sa_tfm_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback.ahash);
	rctx->fallback_req.base.flags =
		req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_finup(&rctx->fallback_req);
}

static int sa_sha_import(struct ahash_request *req, const void *in)
{
	struct sa_sha_req_ctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sa_tfm_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback.ahash);
	rctx->fallback_req.base.flags = req->base.flags &
		CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_import(&rctx->fallback_req, in);
}

static int sa_sha_export(struct ahash_request *req, void *out)
{
	struct sa_sha_req_ctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sa_tfm_ctx *ctx = crypto_ahash_ctx(tfm);
	struct ahash_request *subreq = &rctx->fallback_req;

	ahash_request_set_tfm(subreq, ctx->fallback.ahash);
	subreq->base.flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_export(subreq, out);
}

static int sa_sha1_cra_init(struct crypto_tfm *tfm)
{
	struct algo_data ad = { 0 };
	struct sa_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	sa_sha_cra_init_alg(tfm, "sha1");

	ad.aalg_id = SA_AALG_ID_SHA1;
	ad.hash_size = SHA1_DIGEST_SIZE;
	ad.auth_ctrl = SA_AUTH_SW_CTRL_SHA1;

	sa_sha_setup(ctx, &ad);

	return 0;
}

static int sa_sha256_cra_init(struct crypto_tfm *tfm)
{
	struct algo_data ad = { 0 };
	struct sa_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	sa_sha_cra_init_alg(tfm, "sha256");

	ad.aalg_id = SA_AALG_ID_SHA2_256;
	ad.hash_size = SHA256_DIGEST_SIZE;
	ad.auth_ctrl = SA_AUTH_SW_CTRL_SHA256;

	sa_sha_setup(ctx, &ad);

	return 0;
}

static int sa_sha512_cra_init(struct crypto_tfm *tfm)
{
	struct algo_data ad = { 0 };
	struct sa_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	sa_sha_cra_init_alg(tfm, "sha512");

	ad.aalg_id = SA_AALG_ID_SHA2_512;
	ad.hash_size = SHA512_DIGEST_SIZE;
	ad.auth_ctrl = SA_AUTH_SW_CTRL_SHA512;

	sa_sha_setup(ctx, &ad);

	return 0;
}

static void sa_sha_cra_exit(struct crypto_tfm *tfm)
{
	struct sa_tfm_ctx *ctx = crypto_tfm_ctx(tfm);
	struct sa_crypto_data *data = dev_get_drvdata(sa_k3_dev);

	dev_dbg(sa_k3_dev, "%s(0x%p) sc-ids(0x%x(0x%pad), 0x%x(0x%pad))\n",
		__func__, tfm, ctx->enc.sc_id, &ctx->enc.sc_phys,
		ctx->dec.sc_id, &ctx->dec.sc_phys);

	if (crypto_tfm_alg_type(tfm) == CRYPTO_ALG_TYPE_AHASH)
		sa_free_ctx_info(&ctx->enc, data);

	crypto_free_shash(ctx->shash);
	crypto_free_ahash(ctx->fallback.ahash);
}

static void sa_aead_dma_in_callback(void *data)
{
	struct sa_rx_data *rxd = data;
	struct aead_request *req;
	struct crypto_aead *tfm;
	unsigned int start;
	unsigned int authsize;
	u8 auth_tag[SA_MAX_AUTH_TAG_SZ];
	size_t pl, ml;
	int i;
	int err = 0;
	u32 *mdptr;

	sa_sync_from_device(rxd);
	req = container_of(rxd->req, struct aead_request, base);
	tfm = crypto_aead_reqtfm(req);
	start = req->assoclen + req->cryptlen;
	authsize = crypto_aead_authsize(tfm);

	mdptr = (u32 *)dmaengine_desc_get_metadata_ptr(rxd->tx_in, &pl, &ml);
	for (i = 0; i < (authsize / 4); i++)
		mdptr[i + 4] = swab32(mdptr[i + 4]);

	if (rxd->enc) {
		scatterwalk_map_and_copy(&mdptr[4], req->dst, start, authsize,
					 1);
	} else {
		start -= authsize;
		scatterwalk_map_and_copy(auth_tag, req->src, start, authsize,
					 0);

		err = memcmp(&mdptr[4], auth_tag, authsize) ? -EBADMSG : 0;
	}

	sa_free_sa_rx_data(rxd);

	aead_request_complete(req, err);
}

static int sa_cra_init_aead(struct crypto_aead *tfm, const char *hash,
			    const char *fallback)
{
	struct sa_tfm_ctx *ctx = crypto_aead_ctx(tfm);
	struct sa_crypto_data *data = dev_get_drvdata(sa_k3_dev);
	int ret;

	memzero_explicit(ctx, sizeof(*ctx));
	ctx->dev_data = data;

	ctx->shash = crypto_alloc_shash(hash, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->shash)) {
		dev_err(sa_k3_dev, "base driver %s couldn't be loaded\n", hash);
		return PTR_ERR(ctx->shash);
	}

	ctx->fallback.aead = crypto_alloc_aead(fallback, 0,
					       CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(ctx->fallback.aead)) {
		dev_err(sa_k3_dev, "fallback driver %s couldn't be loaded\n",
			fallback);
		return PTR_ERR(ctx->fallback.aead);
	}

	crypto_aead_set_reqsize(tfm, sizeof(struct aead_request) +
				crypto_aead_reqsize(ctx->fallback.aead));

	ret = sa_init_ctx_info(&ctx->enc, data);
	if (ret)
		return ret;

	ret = sa_init_ctx_info(&ctx->dec, data);
	if (ret) {
		sa_free_ctx_info(&ctx->enc, data);
		return ret;
	}

	dev_dbg(sa_k3_dev, "%s(0x%p) sc-ids(0x%x(0x%pad), 0x%x(0x%pad))\n",
		__func__, tfm, ctx->enc.sc_id, &ctx->enc.sc_phys,
		ctx->dec.sc_id, &ctx->dec.sc_phys);

	return ret;
}

static int sa_cra_init_aead_sha1(struct crypto_aead *tfm)
{
	return sa_cra_init_aead(tfm, "sha1",
				"authenc(hmac(sha1-ce),cbc(aes-ce))");
}

static int sa_cra_init_aead_sha256(struct crypto_aead *tfm)
{
	return sa_cra_init_aead(tfm, "sha256",
				"authenc(hmac(sha256-ce),cbc(aes-ce))");
}

static void sa_exit_tfm_aead(struct crypto_aead *tfm)
{
	struct sa_tfm_ctx *ctx = crypto_aead_ctx(tfm);
	struct sa_crypto_data *data = dev_get_drvdata(sa_k3_dev);

	crypto_free_shash(ctx->shash);
	crypto_free_aead(ctx->fallback.aead);

	sa_free_ctx_info(&ctx->enc, data);
	sa_free_ctx_info(&ctx->dec, data);
}

/* AEAD algorithm configuration interface function */
static int sa_aead_setkey(struct crypto_aead *authenc,
			  const u8 *key, unsigned int keylen,
			  struct algo_data *ad)
{
	struct sa_tfm_ctx *ctx = crypto_aead_ctx(authenc);
	struct crypto_authenc_keys keys;
	int cmdl_len;
	struct sa_cmdl_cfg cfg;
	int key_idx;

	if (crypto_authenc_extractkeys(&keys, key, keylen) != 0)
		return -EINVAL;

	/* Convert the key size (16/24/32) to the key size index (0/1/2) */
	key_idx = (keys.enckeylen >> 3) - 2;
	if (key_idx >= 3)
		return -EINVAL;

	ad->ctx = ctx;
	ad->enc_eng.eng_id = SA_ENG_ID_EM1;
	ad->enc_eng.sc_size = SA_CTX_ENC_TYPE1_SZ;
	ad->auth_eng.eng_id = SA_ENG_ID_AM1;
	ad->auth_eng.sc_size = SA_CTX_AUTH_TYPE2_SZ;
	ad->mci_enc = mci_cbc_enc_no_iv_array[key_idx];
	ad->mci_dec = mci_cbc_dec_no_iv_array[key_idx];
	ad->inv_key = true;
	ad->keyed_mac = true;
	ad->ealg_id = SA_EALG_ID_AES_CBC;
	ad->prep_iopad = sa_prepare_iopads;

	memset(&cfg, 0, sizeof(cfg));
	cfg.enc = true;
	cfg.aalg = ad->aalg_id;
	cfg.enc_eng_id = ad->enc_eng.eng_id;
	cfg.auth_eng_id = ad->auth_eng.eng_id;
	cfg.iv_size = crypto_aead_ivsize(authenc);
	cfg.akey = keys.authkey;
	cfg.akey_len = keys.authkeylen;

	/* Setup Encryption Security Context & Command label template */
	if (sa_init_sc(&ctx->enc, ctx->dev_data->match_data, keys.enckey,
		       keys.enckeylen, keys.authkey, keys.authkeylen,
		       ad, 1, &ctx->enc.epib[1]))
		return -EINVAL;

	cmdl_len = sa_format_cmdl_gen(&cfg,
				      (u8 *)ctx->enc.cmdl,
				      &ctx->enc.cmdl_upd_info);
	if (cmdl_len <= 0 || (cmdl_len > SA_MAX_CMDL_WORDS * sizeof(u32)))
		return -EINVAL;

	ctx->enc.cmdl_size = cmdl_len;

	/* Setup Decryption Security Context & Command label template */
	if (sa_init_sc(&ctx->dec, ctx->dev_data->match_data, keys.enckey,
		       keys.enckeylen, keys.authkey, keys.authkeylen,
		       ad, 0, &ctx->dec.epib[1]))
		return -EINVAL;

	cfg.enc = false;
	cmdl_len = sa_format_cmdl_gen(&cfg, (u8 *)ctx->dec.cmdl,
				      &ctx->dec.cmdl_upd_info);

	if (cmdl_len <= 0 || (cmdl_len > SA_MAX_CMDL_WORDS * sizeof(u32)))
		return -EINVAL;

	ctx->dec.cmdl_size = cmdl_len;

	crypto_aead_clear_flags(ctx->fallback.aead, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(ctx->fallback.aead,
			      crypto_aead_get_flags(authenc) &
			      CRYPTO_TFM_REQ_MASK);
	crypto_aead_setkey(ctx->fallback.aead, key, keylen);

	return 0;
}

static int sa_aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	struct sa_tfm_ctx *ctx = crypto_tfm_ctx(crypto_aead_tfm(tfm));

	return crypto_aead_setauthsize(ctx->fallback.aead, authsize);
}

static int sa_aead_cbc_sha1_setkey(struct crypto_aead *authenc,
				   const u8 *key, unsigned int keylen)
{
	struct algo_data ad = { 0 };

	ad.ealg_id = SA_EALG_ID_AES_CBC;
	ad.aalg_id = SA_AALG_ID_HMAC_SHA1;
	ad.hash_size = SHA1_DIGEST_SIZE;
	ad.auth_ctrl = SA_AUTH_SW_CTRL_SHA1;

	return sa_aead_setkey(authenc, key, keylen, &ad);
}

static int sa_aead_cbc_sha256_setkey(struct crypto_aead *authenc,
				     const u8 *key, unsigned int keylen)
{
	struct algo_data ad = { 0 };

	ad.ealg_id = SA_EALG_ID_AES_CBC;
	ad.aalg_id = SA_AALG_ID_HMAC_SHA2_256;
	ad.hash_size = SHA256_DIGEST_SIZE;
	ad.auth_ctrl = SA_AUTH_SW_CTRL_SHA256;

	return sa_aead_setkey(authenc, key, keylen, &ad);
}

static int sa_aead_run(struct aead_request *req, u8 *iv, int enc)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct sa_tfm_ctx *ctx = crypto_aead_ctx(tfm);
	struct sa_req sa_req = { 0 };
	size_t auth_size, enc_size;

	enc_size = req->cryptlen;
	auth_size = req->assoclen + req->cryptlen;

	if (!enc) {
		enc_size -= crypto_aead_authsize(tfm);
		auth_size -= crypto_aead_authsize(tfm);
	}

	if (auth_size > SA_MAX_DATA_SZ ||
	    (auth_size >= SA_UNSAFE_DATA_SZ_MIN &&
	     auth_size <= SA_UNSAFE_DATA_SZ_MAX)) {
		struct aead_request *subreq = aead_request_ctx(req);
		int ret;

		aead_request_set_tfm(subreq, ctx->fallback.aead);
		aead_request_set_callback(subreq, req->base.flags,
					  req->base.complete, req->base.data);
		aead_request_set_crypt(subreq, req->src, req->dst,
				       req->cryptlen, req->iv);
		aead_request_set_ad(subreq, req->assoclen);

		ret = enc ? crypto_aead_encrypt(subreq) :
			crypto_aead_decrypt(subreq);
		return ret;
	}

	sa_req.enc_offset = req->assoclen;
	sa_req.enc_size = enc_size;
	sa_req.auth_size = auth_size;
	sa_req.size = auth_size;
	sa_req.enc_iv = iv;
	sa_req.type = CRYPTO_ALG_TYPE_AEAD;
	sa_req.enc = enc;
	sa_req.callback = sa_aead_dma_in_callback;
	sa_req.mdata_size = 52;
	sa_req.base = &req->base;
	sa_req.ctx = ctx;
	sa_req.src = req->src;
	sa_req.dst = req->dst;

	return sa_run(&sa_req);
}

/* AEAD algorithm encrypt interface function */
static int sa_aead_encrypt(struct aead_request *req)
{
	return sa_aead_run(req, req->iv, 1);
}

/* AEAD algorithm decrypt interface function */
static int sa_aead_decrypt(struct aead_request *req)
{
	return sa_aead_run(req, req->iv, 0);
}

static struct sa_alg_tmpl sa_algs[] = {
	[SA_ALG_CBC_AES] = {
		.type = CRYPTO_ALG_TYPE_SKCIPHER,
		.alg.skcipher = {
			.base.cra_name		= "cbc(aes)",
			.base.cra_driver_name	= "cbc-aes-sa2ul",
			.base.cra_priority	= 30000,
			.base.cra_flags		= CRYPTO_ALG_TYPE_SKCIPHER |
						  CRYPTO_ALG_KERN_DRIVER_ONLY |
						  CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_NEED_FALLBACK,
			.base.cra_blocksize	= AES_BLOCK_SIZE,
			.base.cra_ctxsize	= sizeof(struct sa_tfm_ctx),
			.base.cra_module	= THIS_MODULE,
			.init			= sa_cipher_cra_init,
			.exit			= sa_cipher_cra_exit,
			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,
			.setkey			= sa_aes_cbc_setkey,
			.encrypt		= sa_encrypt,
			.decrypt		= sa_decrypt,
		}
	},
	[SA_ALG_EBC_AES] = {
		.type = CRYPTO_ALG_TYPE_SKCIPHER,
		.alg.skcipher = {
			.base.cra_name		= "ecb(aes)",
			.base.cra_driver_name	= "ecb-aes-sa2ul",
			.base.cra_priority	= 30000,
			.base.cra_flags		= CRYPTO_ALG_TYPE_SKCIPHER |
						  CRYPTO_ALG_KERN_DRIVER_ONLY |
						  CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_NEED_FALLBACK,
			.base.cra_blocksize	= AES_BLOCK_SIZE,
			.base.cra_ctxsize	= sizeof(struct sa_tfm_ctx),
			.base.cra_module	= THIS_MODULE,
			.init			= sa_cipher_cra_init,
			.exit			= sa_cipher_cra_exit,
			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.setkey			= sa_aes_ecb_setkey,
			.encrypt		= sa_encrypt,
			.decrypt		= sa_decrypt,
		}
	},
	[SA_ALG_CBC_DES3] = {
		.type = CRYPTO_ALG_TYPE_SKCIPHER,
		.alg.skcipher = {
			.base.cra_name		= "cbc(des3_ede)",
			.base.cra_driver_name	= "cbc-des3-sa2ul",
			.base.cra_priority	= 30000,
			.base.cra_flags		= CRYPTO_ALG_TYPE_SKCIPHER |
						  CRYPTO_ALG_KERN_DRIVER_ONLY |
						  CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_NEED_FALLBACK,
			.base.cra_blocksize	= DES_BLOCK_SIZE,
			.base.cra_ctxsize	= sizeof(struct sa_tfm_ctx),
			.base.cra_module	= THIS_MODULE,
			.init			= sa_cipher_cra_init,
			.exit			= sa_cipher_cra_exit,
			.min_keysize		= 3 * DES_KEY_SIZE,
			.max_keysize		= 3 * DES_KEY_SIZE,
			.ivsize			= DES_BLOCK_SIZE,
			.setkey			= sa_3des_cbc_setkey,
			.encrypt		= sa_encrypt,
			.decrypt		= sa_decrypt,
		}
	},
	[SA_ALG_ECB_DES3] = {
		.type = CRYPTO_ALG_TYPE_SKCIPHER,
		.alg.skcipher = {
			.base.cra_name		= "ecb(des3_ede)",
			.base.cra_driver_name	= "ecb-des3-sa2ul",
			.base.cra_priority	= 30000,
			.base.cra_flags		= CRYPTO_ALG_TYPE_SKCIPHER |
						  CRYPTO_ALG_KERN_DRIVER_ONLY |
						  CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_NEED_FALLBACK,
			.base.cra_blocksize	= DES_BLOCK_SIZE,
			.base.cra_ctxsize	= sizeof(struct sa_tfm_ctx),
			.base.cra_module	= THIS_MODULE,
			.init			= sa_cipher_cra_init,
			.exit			= sa_cipher_cra_exit,
			.min_keysize		= 3 * DES_KEY_SIZE,
			.max_keysize		= 3 * DES_KEY_SIZE,
			.setkey			= sa_3des_ecb_setkey,
			.encrypt		= sa_encrypt,
			.decrypt		= sa_decrypt,
		}
	},
	[SA_ALG_SHA1] = {
		.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.ahash = {
			.halg.base = {
				.cra_name	= "sha1",
				.cra_driver_name	= "sha1-sa2ul",
				.cra_priority	= 400,
				.cra_flags	= CRYPTO_ALG_TYPE_AHASH |
						  CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_KERN_DRIVER_ONLY |
						  CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize	= SHA1_BLOCK_SIZE,
				.cra_ctxsize	= sizeof(struct sa_tfm_ctx),
				.cra_module	= THIS_MODULE,
				.cra_init	= sa_sha1_cra_init,
				.cra_exit	= sa_sha_cra_exit,
			},
			.halg.digestsize	= SHA1_DIGEST_SIZE,
			.halg.statesize		= sizeof(struct sa_sha_req_ctx) +
						  sizeof(struct sha1_state),
			.init			= sa_sha_init,
			.update			= sa_sha_update,
			.final			= sa_sha_final,
			.finup			= sa_sha_finup,
			.digest			= sa_sha_digest,
			.export			= sa_sha_export,
			.import			= sa_sha_import,
		},
	},
	[SA_ALG_SHA256] = {
		.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.ahash = {
			.halg.base = {
				.cra_name	= "sha256",
				.cra_driver_name	= "sha256-sa2ul",
				.cra_priority	= 400,
				.cra_flags	= CRYPTO_ALG_TYPE_AHASH |
						  CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_KERN_DRIVER_ONLY |
						  CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize	= SHA256_BLOCK_SIZE,
				.cra_ctxsize	= sizeof(struct sa_tfm_ctx),
				.cra_module	= THIS_MODULE,
				.cra_init	= sa_sha256_cra_init,
				.cra_exit	= sa_sha_cra_exit,
			},
			.halg.digestsize	= SHA256_DIGEST_SIZE,
			.halg.statesize		= sizeof(struct sa_sha_req_ctx) +
						  sizeof(struct sha256_state),
			.init			= sa_sha_init,
			.update			= sa_sha_update,
			.final			= sa_sha_final,
			.finup			= sa_sha_finup,
			.digest			= sa_sha_digest,
			.export			= sa_sha_export,
			.import			= sa_sha_import,
		},
	},
	[SA_ALG_SHA512] = {
		.type = CRYPTO_ALG_TYPE_AHASH,
		.alg.ahash = {
			.halg.base = {
				.cra_name	= "sha512",
				.cra_driver_name	= "sha512-sa2ul",
				.cra_priority	= 400,
				.cra_flags	= CRYPTO_ALG_TYPE_AHASH |
						  CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_KERN_DRIVER_ONLY |
						  CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize	= SHA512_BLOCK_SIZE,
				.cra_ctxsize	= sizeof(struct sa_tfm_ctx),
				.cra_module	= THIS_MODULE,
				.cra_init	= sa_sha512_cra_init,
				.cra_exit	= sa_sha_cra_exit,
			},
			.halg.digestsize	= SHA512_DIGEST_SIZE,
			.halg.statesize		= sizeof(struct sa_sha_req_ctx) +
						  sizeof(struct sha512_state),
			.init			= sa_sha_init,
			.update			= sa_sha_update,
			.final			= sa_sha_final,
			.finup			= sa_sha_finup,
			.digest			= sa_sha_digest,
			.export			= sa_sha_export,
			.import			= sa_sha_import,
		},
	},
	[SA_ALG_AUTHENC_SHA1_AES] = {
		.type	= CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha1),cbc(aes))",
				.cra_driver_name =
					"authenc(hmac(sha1),cbc(aes))-sa2ul",
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AEAD |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_ctxsize = sizeof(struct sa_tfm_ctx),
				.cra_module = THIS_MODULE,
				.cra_priority = 3000,
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,

			.init = sa_cra_init_aead_sha1,
			.exit = sa_exit_tfm_aead,
			.setkey = sa_aead_cbc_sha1_setkey,
			.setauthsize = sa_aead_setauthsize,
			.encrypt = sa_aead_encrypt,
			.decrypt = sa_aead_decrypt,
		},
	},
	[SA_ALG_AUTHENC_SHA256_AES] = {
		.type	= CRYPTO_ALG_TYPE_AEAD,
		.alg.aead = {
			.base = {
				.cra_name = "authenc(hmac(sha256),cbc(aes))",
				.cra_driver_name =
					"authenc(hmac(sha256),cbc(aes))-sa2ul",
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_flags = CRYPTO_ALG_TYPE_AEAD |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_ctxsize = sizeof(struct sa_tfm_ctx),
				.cra_module = THIS_MODULE,
				.cra_alignmask = 0,
				.cra_priority = 3000,
			},
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,

			.init = sa_cra_init_aead_sha256,
			.exit = sa_exit_tfm_aead,
			.setkey = sa_aead_cbc_sha256_setkey,
			.setauthsize = sa_aead_setauthsize,
			.encrypt = sa_aead_encrypt,
			.decrypt = sa_aead_decrypt,
		},
	},
};

/* Register the algorithms in crypto framework */
static void sa_register_algos(struct sa_crypto_data *dev_data)
{
	const struct sa_match_data *match_data = dev_data->match_data;
	struct device *dev = dev_data->dev;
	char *alg_name;
	u32 type;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(sa_algs); i++) {
		/* Skip unsupported algos */
		if (!(match_data->supported_algos & BIT(i)))
			continue;

		type = sa_algs[i].type;
		if (type == CRYPTO_ALG_TYPE_SKCIPHER) {
			alg_name = sa_algs[i].alg.skcipher.base.cra_name;
			err = crypto_register_skcipher(&sa_algs[i].alg.skcipher);
		} else if (type == CRYPTO_ALG_TYPE_AHASH) {
			alg_name = sa_algs[i].alg.ahash.halg.base.cra_name;
			err = crypto_register_ahash(&sa_algs[i].alg.ahash);
		} else if (type == CRYPTO_ALG_TYPE_AEAD) {
			alg_name = sa_algs[i].alg.aead.base.cra_name;
			err = crypto_register_aead(&sa_algs[i].alg.aead);
		} else {
			dev_err(dev,
				"un-supported crypto algorithm (%d)",
				sa_algs[i].type);
			continue;
		}

		if (err)
			dev_err(dev, "Failed to register '%s'\n", alg_name);
		else
			sa_algs[i].registered = true;
	}
}

/* Unregister the algorithms in crypto framework */
static void sa_unregister_algos(const struct device *dev)
{
	u32 type;
	int i;

	for (i = 0; i < ARRAY_SIZE(sa_algs); i++) {
		type = sa_algs[i].type;
		if (!sa_algs[i].registered)
			continue;
		if (type == CRYPTO_ALG_TYPE_SKCIPHER)
			crypto_unregister_skcipher(&sa_algs[i].alg.skcipher);
		else if (type == CRYPTO_ALG_TYPE_AHASH)
			crypto_unregister_ahash(&sa_algs[i].alg.ahash);
		else if (type == CRYPTO_ALG_TYPE_AEAD)
			crypto_unregister_aead(&sa_algs[i].alg.aead);

		sa_algs[i].registered = false;
	}
}

static int sa_init_mem(struct sa_crypto_data *dev_data)
{
	struct device *dev = &dev_data->pdev->dev;
	/* Setup dma pool for security context buffers */
	dev_data->sc_pool = dma_pool_create("keystone-sc", dev,
					    SA_CTX_MAX_SZ, 64, 0);
	if (!dev_data->sc_pool) {
		dev_err(dev, "Failed to create dma pool");
		return -ENOMEM;
	}

	return 0;
}

static int sa_dma_init(struct sa_crypto_data *dd)
{
	int ret;
	struct dma_slave_config cfg;

	dd->dma_rx1 = NULL;
	dd->dma_tx = NULL;
	dd->dma_rx2 = NULL;

	ret = dma_coerce_mask_and_coherent(dd->dev, DMA_BIT_MASK(48));
	if (ret)
		return ret;

	dd->dma_rx1 = dma_request_chan(dd->dev, "rx1");
	if (IS_ERR(dd->dma_rx1))
		return dev_err_probe(dd->dev, PTR_ERR(dd->dma_rx1),
				     "Unable to request rx1 DMA channel\n");

	dd->dma_rx2 = dma_request_chan(dd->dev, "rx2");
	if (IS_ERR(dd->dma_rx2)) {
		ret = dev_err_probe(dd->dev, PTR_ERR(dd->dma_rx2),
				    "Unable to request rx2 DMA channel\n");
		goto err_dma_rx2;
	}

	dd->dma_tx = dma_request_chan(dd->dev, "tx");
	if (IS_ERR(dd->dma_tx)) {
		ret = dev_err_probe(dd->dev, PTR_ERR(dd->dma_tx),
				    "Unable to request tx DMA channel\n");
		goto err_dma_tx;
	}

	memzero_explicit(&cfg, sizeof(cfg));

	cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.src_maxburst = 4;
	cfg.dst_maxburst = 4;

	ret = dmaengine_slave_config(dd->dma_rx1, &cfg);
	if (ret) {
		dev_err(dd->dev, "can't configure IN dmaengine slave: %d\n",
			ret);
		goto err_dma_config;
	}

	ret = dmaengine_slave_config(dd->dma_rx2, &cfg);
	if (ret) {
		dev_err(dd->dev, "can't configure IN dmaengine slave: %d\n",
			ret);
		goto err_dma_config;
	}

	ret = dmaengine_slave_config(dd->dma_tx, &cfg);
	if (ret) {
		dev_err(dd->dev, "can't configure OUT dmaengine slave: %d\n",
			ret);
		goto err_dma_config;
	}

	return 0;

err_dma_config:
	dma_release_channel(dd->dma_tx);
err_dma_tx:
	dma_release_channel(dd->dma_rx2);
err_dma_rx2:
	dma_release_channel(dd->dma_rx1);

	return ret;
}

static int sa_link_child(struct device *dev, void *data)
{
	struct device *parent = data;

	device_link_add(dev, parent, DL_FLAG_AUTOPROBE_CONSUMER);

	return 0;
}

static struct sa_match_data am654_match_data = {
	.priv = 1,
	.priv_id = 1,
	.supported_algos = BIT(SA_ALG_CBC_AES) |
			   BIT(SA_ALG_EBC_AES) |
			   BIT(SA_ALG_CBC_DES3) |
			   BIT(SA_ALG_ECB_DES3) |
			   BIT(SA_ALG_SHA1) |
			   BIT(SA_ALG_SHA256) |
			   BIT(SA_ALG_SHA512) |
			   BIT(SA_ALG_AUTHENC_SHA1_AES) |
			   BIT(SA_ALG_AUTHENC_SHA256_AES),
};

static struct sa_match_data am64_match_data = {
	.priv = 0,
	.priv_id = 0,
	.supported_algos = BIT(SA_ALG_CBC_AES) |
			   BIT(SA_ALG_EBC_AES) |
			   BIT(SA_ALG_SHA256) |
			   BIT(SA_ALG_SHA512) |
			   BIT(SA_ALG_AUTHENC_SHA256_AES),
};

static const struct of_device_id of_match[] = {
	{ .compatible = "ti,j721e-sa2ul", .data = &am654_match_data, },
	{ .compatible = "ti,am654-sa2ul", .data = &am654_match_data, },
	{ .compatible = "ti,am64-sa2ul", .data = &am64_match_data, },
	{ .compatible = "ti,am62-sa3ul", .data = &am64_match_data, },
	{},
};
MODULE_DEVICE_TABLE(of, of_match);

static int sa_ul_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	static void __iomem *saul_base;
	struct sa_crypto_data *dev_data;
	u32 status, val;
	int ret;

	dev_data = devm_kzalloc(dev, sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data)
		return -ENOMEM;

	dev_data->match_data = of_device_get_match_data(dev);
	if (!dev_data->match_data)
		return -ENODEV;

	saul_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(saul_base))
		return PTR_ERR(saul_base);

	sa_k3_dev = dev;
	dev_data->dev = dev;
	dev_data->pdev = pdev;
	dev_data->base = saul_base;
	platform_set_drvdata(pdev, dev_data);
	dev_set_drvdata(sa_k3_dev, dev_data);

	pm_runtime_enable(dev);
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		dev_err(dev, "%s: failed to get sync: %d\n", __func__, ret);
		pm_runtime_disable(dev);
		return ret;
	}

	sa_init_mem(dev_data);
	ret = sa_dma_init(dev_data);
	if (ret)
		goto destroy_dma_pool;

	spin_lock_init(&dev_data->scid_lock);

	val = SA_EEC_ENCSS_EN | SA_EEC_AUTHSS_EN | SA_EEC_CTXCACH_EN |
	      SA_EEC_CPPI_PORT_IN_EN | SA_EEC_CPPI_PORT_OUT_EN |
	      SA_EEC_TRNG_EN;
	status = readl_relaxed(saul_base + SA_ENGINE_STATUS);
	/* Only enable engines if all are not already enabled */
	if (val & ~status)
		writel_relaxed(val, saul_base + SA_ENGINE_ENABLE_CONTROL);

	sa_register_algos(dev_data);

	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret)
		goto release_dma;

	device_for_each_child(dev, dev, sa_link_child);

	return 0;

release_dma:
	sa_unregister_algos(dev);

	dma_release_channel(dev_data->dma_rx2);
	dma_release_channel(dev_data->dma_rx1);
	dma_release_channel(dev_data->dma_tx);

destroy_dma_pool:
	dma_pool_destroy(dev_data->sc_pool);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return ret;
}

static void sa_ul_remove(struct platform_device *pdev)
{
	struct sa_crypto_data *dev_data = platform_get_drvdata(pdev);

	of_platform_depopulate(&pdev->dev);

	sa_unregister_algos(&pdev->dev);

	dma_release_channel(dev_data->dma_rx2);
	dma_release_channel(dev_data->dma_rx1);
	dma_release_channel(dev_data->dma_tx);

	dma_pool_destroy(dev_data->sc_pool);

	platform_set_drvdata(pdev, NULL);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static struct platform_driver sa_ul_driver = {
	.probe = sa_ul_probe,
	.remove_new = sa_ul_remove,
	.driver = {
		   .name = "saul-crypto",
		   .of_match_table = of_match,
		   },
};
module_platform_driver(sa_ul_driver);
MODULE_LICENSE("GPL v2");
