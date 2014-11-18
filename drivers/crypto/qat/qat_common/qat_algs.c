/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/crypto.h>
#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/sha.h>
#include <crypto/hash.h>
#include <crypto/algapi.h>
#include <crypto/authenc.h>
#include <crypto/rng.h>
#include <linux/dma-mapping.h>
#include "adf_accel_devices.h"
#include "adf_transport.h"
#include "adf_common_drv.h"
#include "qat_crypto.h"
#include "icp_qat_hw.h"
#include "icp_qat_fw.h"
#include "icp_qat_fw_la.h"

#define QAT_AES_HW_CONFIG_ENC(alg) \
	ICP_QAT_HW_CIPHER_CONFIG_BUILD(ICP_QAT_HW_CIPHER_CBC_MODE, alg, \
			ICP_QAT_HW_CIPHER_NO_CONVERT, \
			ICP_QAT_HW_CIPHER_ENCRYPT)

#define QAT_AES_HW_CONFIG_DEC(alg) \
	ICP_QAT_HW_CIPHER_CONFIG_BUILD(ICP_QAT_HW_CIPHER_CBC_MODE, alg, \
			ICP_QAT_HW_CIPHER_KEY_CONVERT, \
			ICP_QAT_HW_CIPHER_DECRYPT)

static atomic_t active_dev;

struct qat_alg_buf {
	uint32_t len;
	uint32_t resrvd;
	uint64_t addr;
} __packed;

struct qat_alg_buf_list {
	uint64_t resrvd;
	uint32_t num_bufs;
	uint32_t num_mapped_bufs;
	struct qat_alg_buf bufers[];
} __packed __aligned(64);

/* Common content descriptor */
struct qat_alg_cd {
	union {
		struct qat_enc { /* Encrypt content desc */
			struct icp_qat_hw_cipher_algo_blk cipher;
			struct icp_qat_hw_auth_algo_blk hash;
		} qat_enc_cd;
		struct qat_dec { /* Decrytp content desc */
			struct icp_qat_hw_auth_algo_blk hash;
			struct icp_qat_hw_cipher_algo_blk cipher;
		} qat_dec_cd;
	};
} __aligned(64);

#define MAX_AUTH_STATE_SIZE sizeof(struct icp_qat_hw_auth_algo_blk)

struct qat_auth_state {
	uint8_t data[MAX_AUTH_STATE_SIZE + 64];
} __aligned(64);

struct qat_alg_session_ctx {
	struct qat_alg_cd *enc_cd;
	dma_addr_t enc_cd_paddr;
	struct qat_alg_cd *dec_cd;
	dma_addr_t dec_cd_paddr;
	struct icp_qat_fw_la_bulk_req enc_fw_req_tmpl;
	struct icp_qat_fw_la_bulk_req dec_fw_req_tmpl;
	struct qat_crypto_instance *inst;
	struct crypto_tfm *tfm;
	struct crypto_shash *hash_tfm;
	enum icp_qat_hw_auth_algo qat_hash_alg;
	uint8_t salt[AES_BLOCK_SIZE];
	spinlock_t lock;	/* protects qat_alg_session_ctx struct */
};

static int get_current_node(void)
{
	return cpu_data(current_thread_info()->cpu).phys_proc_id;
}

static int qat_get_inter_state_size(enum icp_qat_hw_auth_algo qat_hash_alg)
{
	switch (qat_hash_alg) {
	case ICP_QAT_HW_AUTH_ALGO_SHA1:
		return ICP_QAT_HW_SHA1_STATE1_SZ;
	case ICP_QAT_HW_AUTH_ALGO_SHA256:
		return ICP_QAT_HW_SHA256_STATE1_SZ;
	case ICP_QAT_HW_AUTH_ALGO_SHA512:
		return ICP_QAT_HW_SHA512_STATE1_SZ;
	default:
		return -EFAULT;
	};
	return -EFAULT;
}

static int qat_alg_do_precomputes(struct icp_qat_hw_auth_algo_blk *hash,
				  struct qat_alg_session_ctx *ctx,
				  const uint8_t *auth_key,
				  unsigned int auth_keylen)
{
	struct qat_auth_state auth_state;
	SHASH_DESC_ON_STACK(shash, ctx->hash_tfm);
	struct sha1_state sha1;
	struct sha256_state sha256;
	struct sha512_state sha512;
	int block_size = crypto_shash_blocksize(ctx->hash_tfm);
	int digest_size = crypto_shash_digestsize(ctx->hash_tfm);
	uint8_t *ipad = auth_state.data;
	uint8_t *opad = ipad + block_size;
	__be32 *hash_state_out;
	__be64 *hash512_state_out;
	int i, offset;

	memset(auth_state.data, '\0', MAX_AUTH_STATE_SIZE + 64);
	shash->tfm = ctx->hash_tfm;
	shash->flags = 0x0;

	if (auth_keylen > block_size) {
		char buff[SHA512_BLOCK_SIZE];
		int ret = crypto_shash_digest(shash, auth_key,
					      auth_keylen, buff);
		if (ret)
			return ret;

		memcpy(ipad, buff, digest_size);
		memcpy(opad, buff, digest_size);
		memset(ipad + digest_size, 0, block_size - digest_size);
		memset(opad + digest_size, 0, block_size - digest_size);
	} else {
		memcpy(ipad, auth_key, auth_keylen);
		memcpy(opad, auth_key, auth_keylen);
		memset(ipad + auth_keylen, 0, block_size - auth_keylen);
		memset(opad + auth_keylen, 0, block_size - auth_keylen);
	}

	for (i = 0; i < block_size; i++) {
		char *ipad_ptr = ipad + i;
		char *opad_ptr = opad + i;
		*ipad_ptr ^= 0x36;
		*opad_ptr ^= 0x5C;
	}

	if (crypto_shash_init(shash))
		return -EFAULT;

	if (crypto_shash_update(shash, ipad, block_size))
		return -EFAULT;

	hash_state_out = (__be32 *)hash->sha.state1;
	hash512_state_out = (__be64 *)hash_state_out;

	switch (ctx->qat_hash_alg) {
	case ICP_QAT_HW_AUTH_ALGO_SHA1:
		if (crypto_shash_export(shash, &sha1))
			return -EFAULT;
		for (i = 0; i < digest_size >> 2; i++, hash_state_out++)
			*hash_state_out = cpu_to_be32(*(sha1.state + i));
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA256:
		if (crypto_shash_export(shash, &sha256))
			return -EFAULT;
		for (i = 0; i < digest_size >> 2; i++, hash_state_out++)
			*hash_state_out = cpu_to_be32(*(sha256.state + i));
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA512:
		if (crypto_shash_export(shash, &sha512))
			return -EFAULT;
		for (i = 0; i < digest_size >> 3; i++, hash512_state_out++)
			*hash512_state_out = cpu_to_be64(*(sha512.state + i));
		break;
	default:
		return -EFAULT;
	}

	if (crypto_shash_init(shash))
		return -EFAULT;

	if (crypto_shash_update(shash, opad, block_size))
		return -EFAULT;

	offset = round_up(qat_get_inter_state_size(ctx->qat_hash_alg), 8);
	hash_state_out = (__be32 *)(hash->sha.state1 + offset);
	hash512_state_out = (__be64 *)hash_state_out;

	switch (ctx->qat_hash_alg) {
	case ICP_QAT_HW_AUTH_ALGO_SHA1:
		if (crypto_shash_export(shash, &sha1))
			return -EFAULT;
		for (i = 0; i < digest_size >> 2; i++, hash_state_out++)
			*hash_state_out = cpu_to_be32(*(sha1.state + i));
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA256:
		if (crypto_shash_export(shash, &sha256))
			return -EFAULT;
		for (i = 0; i < digest_size >> 2; i++, hash_state_out++)
			*hash_state_out = cpu_to_be32(*(sha256.state + i));
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA512:
		if (crypto_shash_export(shash, &sha512))
			return -EFAULT;
		for (i = 0; i < digest_size >> 3; i++, hash512_state_out++)
			*hash512_state_out = cpu_to_be64(*(sha512.state + i));
		break;
	default:
		return -EFAULT;
	}
	return 0;
}

static void qat_alg_init_common_hdr(struct icp_qat_fw_comn_req_hdr *header)
{
	header->hdr_flags =
		ICP_QAT_FW_COMN_HDR_FLAGS_BUILD(ICP_QAT_FW_COMN_REQ_FLAG_SET);
	header->service_type = ICP_QAT_FW_COMN_REQ_CPM_FW_LA;
	header->comn_req_flags =
		ICP_QAT_FW_COMN_FLAGS_BUILD(QAT_COMN_CD_FLD_TYPE_64BIT_ADR,
					    QAT_COMN_PTR_TYPE_SGL);
	ICP_QAT_FW_LA_DIGEST_IN_BUFFER_SET(header->serv_specif_flags,
					   ICP_QAT_FW_LA_DIGEST_IN_BUFFER);
	ICP_QAT_FW_LA_PARTIAL_SET(header->serv_specif_flags,
				  ICP_QAT_FW_LA_PARTIAL_NONE);
	ICP_QAT_FW_LA_CIPH_IV_FLD_FLAG_SET(header->serv_specif_flags,
					   ICP_QAT_FW_CIPH_IV_16BYTE_DATA);
	ICP_QAT_FW_LA_PROTO_SET(header->serv_specif_flags,
				ICP_QAT_FW_LA_NO_PROTO);
	ICP_QAT_FW_LA_UPDATE_STATE_SET(header->serv_specif_flags,
				       ICP_QAT_FW_LA_NO_UPDATE_STATE);
}

static int qat_alg_init_enc_session(struct qat_alg_session_ctx *ctx,
				    int alg, struct crypto_authenc_keys *keys)
{
	struct crypto_aead *aead_tfm = __crypto_aead_cast(ctx->tfm);
	unsigned int digestsize = crypto_aead_crt(aead_tfm)->authsize;
	struct qat_enc *enc_ctx = &ctx->enc_cd->qat_enc_cd;
	struct icp_qat_hw_cipher_algo_blk *cipher = &enc_ctx->cipher;
	struct icp_qat_hw_auth_algo_blk *hash =
		(struct icp_qat_hw_auth_algo_blk *)((char *)enc_ctx +
		sizeof(struct icp_qat_hw_auth_setup) + keys->enckeylen);
	struct icp_qat_fw_la_bulk_req *req_tmpl = &ctx->enc_fw_req_tmpl;
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req_tmpl->cd_pars;
	struct icp_qat_fw_comn_req_hdr *header = &req_tmpl->comn_hdr;
	void *ptr = &req_tmpl->cd_ctrl;
	struct icp_qat_fw_cipher_cd_ctrl_hdr *cipher_cd_ctrl = ptr;
	struct icp_qat_fw_auth_cd_ctrl_hdr *hash_cd_ctrl = ptr;

	/* CD setup */
	cipher->aes.cipher_config.val = QAT_AES_HW_CONFIG_ENC(alg);
	memcpy(cipher->aes.key, keys->enckey, keys->enckeylen);
	hash->sha.inner_setup.auth_config.config =
		ICP_QAT_HW_AUTH_CONFIG_BUILD(ICP_QAT_HW_AUTH_MODE1,
					     ctx->qat_hash_alg, digestsize);
	hash->sha.inner_setup.auth_counter.counter =
		cpu_to_be32(crypto_shash_blocksize(ctx->hash_tfm));

	if (qat_alg_do_precomputes(hash, ctx, keys->authkey, keys->authkeylen))
		return -EFAULT;

	/* Request setup */
	qat_alg_init_common_hdr(header);
	header->service_cmd_id = ICP_QAT_FW_LA_CMD_CIPHER_HASH;
	ICP_QAT_FW_LA_RET_AUTH_SET(header->serv_specif_flags,
				   ICP_QAT_FW_LA_RET_AUTH_RES);
	ICP_QAT_FW_LA_CMP_AUTH_SET(header->serv_specif_flags,
				   ICP_QAT_FW_LA_NO_CMP_AUTH_RES);
	cd_pars->u.s.content_desc_addr = ctx->enc_cd_paddr;
	cd_pars->u.s.content_desc_params_sz = sizeof(struct qat_alg_cd) >> 3;

	/* Cipher CD config setup */
	cipher_cd_ctrl->cipher_key_sz = keys->enckeylen >> 3;
	cipher_cd_ctrl->cipher_state_sz = AES_BLOCK_SIZE >> 3;
	cipher_cd_ctrl->cipher_cfg_offset = 0;
	ICP_QAT_FW_COMN_CURR_ID_SET(cipher_cd_ctrl, ICP_QAT_FW_SLICE_CIPHER);
	ICP_QAT_FW_COMN_NEXT_ID_SET(cipher_cd_ctrl, ICP_QAT_FW_SLICE_AUTH);
	/* Auth CD config setup */
	hash_cd_ctrl->hash_cfg_offset = ((char *)hash - (char *)cipher) >> 3;
	hash_cd_ctrl->hash_flags = ICP_QAT_FW_AUTH_HDR_FLAG_NO_NESTED;
	hash_cd_ctrl->inner_res_sz = digestsize;
	hash_cd_ctrl->final_sz = digestsize;

	switch (ctx->qat_hash_alg) {
	case ICP_QAT_HW_AUTH_ALGO_SHA1:
		hash_cd_ctrl->inner_state1_sz =
			round_up(ICP_QAT_HW_SHA1_STATE1_SZ, 8);
		hash_cd_ctrl->inner_state2_sz =
			round_up(ICP_QAT_HW_SHA1_STATE2_SZ, 8);
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA256:
		hash_cd_ctrl->inner_state1_sz = ICP_QAT_HW_SHA256_STATE1_SZ;
		hash_cd_ctrl->inner_state2_sz = ICP_QAT_HW_SHA256_STATE2_SZ;
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA512:
		hash_cd_ctrl->inner_state1_sz = ICP_QAT_HW_SHA512_STATE1_SZ;
		hash_cd_ctrl->inner_state2_sz = ICP_QAT_HW_SHA512_STATE2_SZ;
		break;
	default:
		break;
	}
	hash_cd_ctrl->inner_state2_offset = hash_cd_ctrl->hash_cfg_offset +
			((sizeof(struct icp_qat_hw_auth_setup) +
			 round_up(hash_cd_ctrl->inner_state1_sz, 8)) >> 3);
	ICP_QAT_FW_COMN_CURR_ID_SET(hash_cd_ctrl, ICP_QAT_FW_SLICE_AUTH);
	ICP_QAT_FW_COMN_NEXT_ID_SET(hash_cd_ctrl, ICP_QAT_FW_SLICE_DRAM_WR);
	return 0;
}

static int qat_alg_init_dec_session(struct qat_alg_session_ctx *ctx,
				    int alg, struct crypto_authenc_keys *keys)
{
	struct crypto_aead *aead_tfm = __crypto_aead_cast(ctx->tfm);
	unsigned int digestsize = crypto_aead_crt(aead_tfm)->authsize;
	struct qat_dec *dec_ctx = &ctx->dec_cd->qat_dec_cd;
	struct icp_qat_hw_auth_algo_blk *hash = &dec_ctx->hash;
	struct icp_qat_hw_cipher_algo_blk *cipher =
		(struct icp_qat_hw_cipher_algo_blk *)((char *)dec_ctx +
		sizeof(struct icp_qat_hw_auth_setup) +
		roundup(crypto_shash_digestsize(ctx->hash_tfm), 8) * 2);
	struct icp_qat_fw_la_bulk_req *req_tmpl = &ctx->dec_fw_req_tmpl;
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req_tmpl->cd_pars;
	struct icp_qat_fw_comn_req_hdr *header = &req_tmpl->comn_hdr;
	void *ptr = &req_tmpl->cd_ctrl;
	struct icp_qat_fw_cipher_cd_ctrl_hdr *cipher_cd_ctrl = ptr;
	struct icp_qat_fw_auth_cd_ctrl_hdr *hash_cd_ctrl = ptr;
	struct icp_qat_fw_la_auth_req_params *auth_param =
		(struct icp_qat_fw_la_auth_req_params *)
		((char *)&req_tmpl->serv_specif_rqpars +
		sizeof(struct icp_qat_fw_la_cipher_req_params));

	/* CD setup */
	cipher->aes.cipher_config.val = QAT_AES_HW_CONFIG_DEC(alg);
	memcpy(cipher->aes.key, keys->enckey, keys->enckeylen);
	hash->sha.inner_setup.auth_config.config =
		ICP_QAT_HW_AUTH_CONFIG_BUILD(ICP_QAT_HW_AUTH_MODE1,
					     ctx->qat_hash_alg,
					     digestsize);
	hash->sha.inner_setup.auth_counter.counter =
		cpu_to_be32(crypto_shash_blocksize(ctx->hash_tfm));

	if (qat_alg_do_precomputes(hash, ctx, keys->authkey, keys->authkeylen))
		return -EFAULT;

	/* Request setup */
	qat_alg_init_common_hdr(header);
	header->service_cmd_id = ICP_QAT_FW_LA_CMD_HASH_CIPHER;
	ICP_QAT_FW_LA_RET_AUTH_SET(header->serv_specif_flags,
				   ICP_QAT_FW_LA_NO_RET_AUTH_RES);
	ICP_QAT_FW_LA_CMP_AUTH_SET(header->serv_specif_flags,
				   ICP_QAT_FW_LA_CMP_AUTH_RES);
	cd_pars->u.s.content_desc_addr = ctx->dec_cd_paddr;
	cd_pars->u.s.content_desc_params_sz = sizeof(struct qat_alg_cd) >> 3;

	/* Cipher CD config setup */
	cipher_cd_ctrl->cipher_key_sz = keys->enckeylen >> 3;
	cipher_cd_ctrl->cipher_state_sz = AES_BLOCK_SIZE >> 3;
	cipher_cd_ctrl->cipher_cfg_offset =
		(sizeof(struct icp_qat_hw_auth_setup) +
		 roundup(crypto_shash_digestsize(ctx->hash_tfm), 8) * 2) >> 3;
	ICP_QAT_FW_COMN_CURR_ID_SET(cipher_cd_ctrl, ICP_QAT_FW_SLICE_CIPHER);
	ICP_QAT_FW_COMN_NEXT_ID_SET(cipher_cd_ctrl, ICP_QAT_FW_SLICE_DRAM_WR);

	/* Auth CD config setup */
	hash_cd_ctrl->hash_cfg_offset = 0;
	hash_cd_ctrl->hash_flags = ICP_QAT_FW_AUTH_HDR_FLAG_NO_NESTED;
	hash_cd_ctrl->inner_res_sz = digestsize;
	hash_cd_ctrl->final_sz = digestsize;

	switch (ctx->qat_hash_alg) {
	case ICP_QAT_HW_AUTH_ALGO_SHA1:
		hash_cd_ctrl->inner_state1_sz =
			round_up(ICP_QAT_HW_SHA1_STATE1_SZ, 8);
		hash_cd_ctrl->inner_state2_sz =
			round_up(ICP_QAT_HW_SHA1_STATE2_SZ, 8);
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA256:
		hash_cd_ctrl->inner_state1_sz = ICP_QAT_HW_SHA256_STATE1_SZ;
		hash_cd_ctrl->inner_state2_sz = ICP_QAT_HW_SHA256_STATE2_SZ;
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA512:
		hash_cd_ctrl->inner_state1_sz = ICP_QAT_HW_SHA512_STATE1_SZ;
		hash_cd_ctrl->inner_state2_sz = ICP_QAT_HW_SHA512_STATE2_SZ;
		break;
	default:
		break;
	}

	hash_cd_ctrl->inner_state2_offset = hash_cd_ctrl->hash_cfg_offset +
			((sizeof(struct icp_qat_hw_auth_setup) +
			 round_up(hash_cd_ctrl->inner_state1_sz, 8)) >> 3);
	auth_param->auth_res_sz = digestsize;
	ICP_QAT_FW_COMN_CURR_ID_SET(hash_cd_ctrl, ICP_QAT_FW_SLICE_AUTH);
	ICP_QAT_FW_COMN_NEXT_ID_SET(hash_cd_ctrl, ICP_QAT_FW_SLICE_CIPHER);
	return 0;
}

static int qat_alg_init_sessions(struct qat_alg_session_ctx *ctx,
				 const uint8_t *key, unsigned int keylen)
{
	struct crypto_authenc_keys keys;
	int alg;

	if (crypto_rng_get_bytes(crypto_default_rng, ctx->salt, AES_BLOCK_SIZE))
		return -EFAULT;

	if (crypto_authenc_extractkeys(&keys, key, keylen))
		goto bad_key;

	switch (keys.enckeylen) {
	case AES_KEYSIZE_128:
		alg = ICP_QAT_HW_CIPHER_ALGO_AES128;
		break;
	case AES_KEYSIZE_192:
		alg = ICP_QAT_HW_CIPHER_ALGO_AES192;
		break;
	case AES_KEYSIZE_256:
		alg = ICP_QAT_HW_CIPHER_ALGO_AES256;
		break;
	default:
		goto bad_key;
		break;
	}

	if (qat_alg_init_enc_session(ctx, alg, &keys))
		goto error;

	if (qat_alg_init_dec_session(ctx, alg, &keys))
		goto error;

	return 0;
bad_key:
	crypto_tfm_set_flags(ctx->tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
error:
	return -EFAULT;
}

static int qat_alg_setkey(struct crypto_aead *tfm, const uint8_t *key,
			  unsigned int keylen)
{
	struct qat_alg_session_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev;

	spin_lock(&ctx->lock);
	if (ctx->enc_cd) {
		/* rekeying */
		dev = &GET_DEV(ctx->inst->accel_dev);
		memset(ctx->enc_cd, 0, sizeof(struct qat_alg_cd));
		memset(ctx->dec_cd, 0, sizeof(struct qat_alg_cd));
		memset(&ctx->enc_fw_req_tmpl, 0,
		       sizeof(struct icp_qat_fw_la_bulk_req));
		memset(&ctx->dec_fw_req_tmpl, 0,
		       sizeof(struct icp_qat_fw_la_bulk_req));
	} else {
		/* new key */
		int node = get_current_node();
		struct qat_crypto_instance *inst =
				qat_crypto_get_instance_node(node);
		if (!inst) {
			spin_unlock(&ctx->lock);
			return -EINVAL;
		}

		dev = &GET_DEV(inst->accel_dev);
		ctx->inst = inst;
		ctx->enc_cd = dma_zalloc_coherent(dev,
						  sizeof(struct qat_alg_cd),
						  &ctx->enc_cd_paddr,
						  GFP_ATOMIC);
		if (!ctx->enc_cd) {
			spin_unlock(&ctx->lock);
			return -ENOMEM;
		}
		ctx->dec_cd = dma_zalloc_coherent(dev,
						  sizeof(struct qat_alg_cd),
						  &ctx->dec_cd_paddr,
						  GFP_ATOMIC);
		if (!ctx->dec_cd) {
			spin_unlock(&ctx->lock);
			goto out_free_enc;
		}
	}
	spin_unlock(&ctx->lock);
	if (qat_alg_init_sessions(ctx, key, keylen))
		goto out_free_all;

	return 0;

out_free_all:
	dma_free_coherent(dev, sizeof(struct qat_alg_cd),
			  ctx->dec_cd, ctx->dec_cd_paddr);
	ctx->dec_cd = NULL;
out_free_enc:
	dma_free_coherent(dev, sizeof(struct qat_alg_cd),
			  ctx->enc_cd, ctx->enc_cd_paddr);
	ctx->enc_cd = NULL;
	return -ENOMEM;
}

static void qat_alg_free_bufl(struct qat_crypto_instance *inst,
			      struct qat_crypto_request *qat_req)
{
	struct device *dev = &GET_DEV(inst->accel_dev);
	struct qat_alg_buf_list *bl = qat_req->buf.bl;
	struct qat_alg_buf_list *blout = qat_req->buf.blout;
	dma_addr_t blp = qat_req->buf.blp;
	dma_addr_t blpout = qat_req->buf.bloutp;
	size_t sz = qat_req->buf.sz;
	int i, bufs = bl->num_bufs;

	for (i = 0; i < bl->num_bufs; i++)
		dma_unmap_single(dev, bl->bufers[i].addr,
				 bl->bufers[i].len, DMA_BIDIRECTIONAL);

	dma_unmap_single(dev, blp, sz, DMA_TO_DEVICE);
	kfree(bl);
	if (blp != blpout) {
		/* If out of place operation dma unmap only data */
		int bufless = bufs - blout->num_mapped_bufs;

		for (i = bufless; i < bufs; i++) {
			dma_unmap_single(dev, blout->bufers[i].addr,
					 blout->bufers[i].len,
					 DMA_BIDIRECTIONAL);
		}
		dma_unmap_single(dev, blpout, sz, DMA_TO_DEVICE);
		kfree(blout);
	}
}

static int qat_alg_sgl_to_bufl(struct qat_crypto_instance *inst,
			       struct scatterlist *assoc,
			       struct scatterlist *sgl,
			       struct scatterlist *sglout, uint8_t *iv,
			       uint8_t ivlen,
			       struct qat_crypto_request *qat_req)
{
	struct device *dev = &GET_DEV(inst->accel_dev);
	int i, bufs = 0, n = sg_nents(sgl), assoc_n = sg_nents(assoc);
	struct qat_alg_buf_list *bufl;
	struct qat_alg_buf_list *buflout = NULL;
	dma_addr_t blp;
	dma_addr_t bloutp = 0;
	struct scatterlist *sg;
	size_t sz = sizeof(struct qat_alg_buf_list) +
			((1 + n + assoc_n) * sizeof(struct qat_alg_buf));

	if (unlikely(!n))
		return -EINVAL;

	bufl = kmalloc_node(sz, GFP_ATOMIC, inst->accel_dev->numa_node);
	if (unlikely(!bufl))
		return -ENOMEM;

	blp = dma_map_single(dev, bufl, sz, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, blp)))
		goto err;

	for_each_sg(assoc, sg, assoc_n, i) {
		bufl->bufers[bufs].addr = dma_map_single(dev,
							 sg_virt(sg),
							 sg->length,
							 DMA_BIDIRECTIONAL);
		bufl->bufers[bufs].len = sg->length;
		if (unlikely(dma_mapping_error(dev, bufl->bufers[bufs].addr)))
			goto err;
		bufs++;
	}
	bufl->bufers[bufs].addr = dma_map_single(dev, iv, ivlen,
						 DMA_BIDIRECTIONAL);
	bufl->bufers[bufs].len = ivlen;
	if (unlikely(dma_mapping_error(dev, bufl->bufers[bufs].addr)))
		goto err;
	bufs++;

	for_each_sg(sgl, sg, n, i) {
		int y = i + bufs;

		bufl->bufers[y].addr = dma_map_single(dev, sg_virt(sg),
						      sg->length,
						      DMA_BIDIRECTIONAL);
		bufl->bufers[y].len = sg->length;
		if (unlikely(dma_mapping_error(dev, bufl->bufers[y].addr)))
			goto err;
	}
	bufl->num_bufs = n + bufs;
	qat_req->buf.bl = bufl;
	qat_req->buf.blp = blp;
	qat_req->buf.sz = sz;
	/* Handle out of place operation */
	if (sgl != sglout) {
		struct qat_alg_buf *bufers;

		buflout = kmalloc_node(sz, GFP_ATOMIC,
				       inst->accel_dev->numa_node);
		if (unlikely(!buflout))
			goto err;
		bloutp = dma_map_single(dev, buflout, sz, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev, bloutp)))
			goto err;
		bufers = buflout->bufers;
		/* For out of place operation dma map only data and
		 * reuse assoc mapping and iv */
		for (i = 0; i < bufs; i++) {
			bufers[i].len = bufl->bufers[i].len;
			bufers[i].addr = bufl->bufers[i].addr;
		}
		for_each_sg(sglout, sg, n, i) {
			int y = i + bufs;

			bufers[y].addr = dma_map_single(dev, sg_virt(sg),
							sg->length,
							DMA_BIDIRECTIONAL);
			buflout->bufers[y].len = sg->length;
			if (unlikely(dma_mapping_error(dev, bufers[y].addr)))
				goto err;
		}
		buflout->num_bufs = n + bufs;
		buflout->num_mapped_bufs = n;
		qat_req->buf.blout = buflout;
		qat_req->buf.bloutp = bloutp;
	} else {
		/* Otherwise set the src and dst to the same address */
		qat_req->buf.bloutp = qat_req->buf.blp;
	}
	return 0;
err:
	dev_err(dev, "Failed to map buf for dma\n");
	for_each_sg(sgl, sg, n + bufs, i) {
		if (!dma_mapping_error(dev, bufl->bufers[i].addr)) {
			dma_unmap_single(dev, bufl->bufers[i].addr,
					 bufl->bufers[i].len,
					 DMA_BIDIRECTIONAL);
		}
	}
	if (!dma_mapping_error(dev, blp))
		dma_unmap_single(dev, blp, sz, DMA_TO_DEVICE);
	kfree(bufl);
	if (sgl != sglout && buflout) {
		for_each_sg(sglout, sg, n, i) {
			int y = i + bufs;

			if (!dma_mapping_error(dev, buflout->bufers[y].addr))
				dma_unmap_single(dev, buflout->bufers[y].addr,
						 buflout->bufers[y].len,
						 DMA_BIDIRECTIONAL);
		}
		if (!dma_mapping_error(dev, bloutp))
			dma_unmap_single(dev, bloutp, sz, DMA_TO_DEVICE);
		kfree(buflout);
	}
	return -ENOMEM;
}

void qat_alg_callback(void *resp)
{
	struct icp_qat_fw_la_resp *qat_resp = resp;
	struct qat_crypto_request *qat_req =
				(void *)(__force long)qat_resp->opaque_data;
	struct qat_alg_session_ctx *ctx = qat_req->ctx;
	struct qat_crypto_instance *inst = ctx->inst;
	struct aead_request *areq = qat_req->areq;
	uint8_t stat_filed = qat_resp->comn_resp.comn_status;
	int res = 0, qat_res = ICP_QAT_FW_COMN_RESP_CRYPTO_STAT_GET(stat_filed);

	qat_alg_free_bufl(inst, qat_req);
	if (unlikely(qat_res != ICP_QAT_FW_COMN_STATUS_FLAG_OK))
		res = -EBADMSG;
	areq->base.complete(&areq->base, res);
}

static int qat_alg_dec(struct aead_request *areq)
{
	struct crypto_aead *aead_tfm = crypto_aead_reqtfm(areq);
	struct crypto_tfm *tfm = crypto_aead_tfm(aead_tfm);
	struct qat_alg_session_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_request *qat_req = aead_request_ctx(areq);
	struct icp_qat_fw_la_cipher_req_params *cipher_param;
	struct icp_qat_fw_la_auth_req_params *auth_param;
	struct icp_qat_fw_la_bulk_req *msg;
	int digst_size = crypto_aead_crt(aead_tfm)->authsize;
	int ret, ctr = 0;

	ret = qat_alg_sgl_to_bufl(ctx->inst, areq->assoc, areq->src, areq->dst,
				  areq->iv, AES_BLOCK_SIZE, qat_req);
	if (unlikely(ret))
		return ret;

	msg = &qat_req->req;
	*msg = ctx->dec_fw_req_tmpl;
	qat_req->ctx = ctx;
	qat_req->areq = areq;
	qat_req->req.comn_mid.opaque_data = (uint64_t)(__force long)qat_req;
	qat_req->req.comn_mid.src_data_addr = qat_req->buf.blp;
	qat_req->req.comn_mid.dest_data_addr = qat_req->buf.bloutp;
	cipher_param = (void *)&qat_req->req.serv_specif_rqpars;
	cipher_param->cipher_length = areq->cryptlen - digst_size;
	cipher_param->cipher_offset = areq->assoclen + AES_BLOCK_SIZE;
	memcpy(cipher_param->u.cipher_IV_array, areq->iv, AES_BLOCK_SIZE);
	auth_param = (void *)((uint8_t *)cipher_param + sizeof(*cipher_param));
	auth_param->auth_off = 0;
	auth_param->auth_len = areq->assoclen +
				cipher_param->cipher_length + AES_BLOCK_SIZE;
	do {
		ret = adf_send_message(ctx->inst->sym_tx, (uint32_t *)msg);
	} while (ret == -EAGAIN && ctr++ < 10);

	if (ret == -EAGAIN) {
		qat_alg_free_bufl(ctx->inst, qat_req);
		return -EBUSY;
	}
	return -EINPROGRESS;
}

static int qat_alg_enc_internal(struct aead_request *areq, uint8_t *iv,
				int enc_iv)
{
	struct crypto_aead *aead_tfm = crypto_aead_reqtfm(areq);
	struct crypto_tfm *tfm = crypto_aead_tfm(aead_tfm);
	struct qat_alg_session_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_request *qat_req = aead_request_ctx(areq);
	struct icp_qat_fw_la_cipher_req_params *cipher_param;
	struct icp_qat_fw_la_auth_req_params *auth_param;
	struct icp_qat_fw_la_bulk_req *msg;
	int ret, ctr = 0;

	ret = qat_alg_sgl_to_bufl(ctx->inst, areq->assoc, areq->src, areq->dst,
				  iv, AES_BLOCK_SIZE, qat_req);
	if (unlikely(ret))
		return ret;

	msg = &qat_req->req;
	*msg = ctx->enc_fw_req_tmpl;
	qat_req->ctx = ctx;
	qat_req->areq = areq;
	qat_req->req.comn_mid.opaque_data = (uint64_t)(__force long)qat_req;
	qat_req->req.comn_mid.src_data_addr = qat_req->buf.blp;
	qat_req->req.comn_mid.dest_data_addr = qat_req->buf.bloutp;
	cipher_param = (void *)&qat_req->req.serv_specif_rqpars;
	auth_param = (void *)((uint8_t *)cipher_param + sizeof(*cipher_param));

	if (enc_iv) {
		cipher_param->cipher_length = areq->cryptlen + AES_BLOCK_SIZE;
		cipher_param->cipher_offset = areq->assoclen;
	} else {
		memcpy(cipher_param->u.cipher_IV_array, iv, AES_BLOCK_SIZE);
		cipher_param->cipher_length = areq->cryptlen;
		cipher_param->cipher_offset = areq->assoclen + AES_BLOCK_SIZE;
	}
	auth_param->auth_off = 0;
	auth_param->auth_len = areq->assoclen + areq->cryptlen + AES_BLOCK_SIZE;

	do {
		ret = adf_send_message(ctx->inst->sym_tx, (uint32_t *)msg);
	} while (ret == -EAGAIN && ctr++ < 10);

	if (ret == -EAGAIN) {
		qat_alg_free_bufl(ctx->inst, qat_req);
		return -EBUSY;
	}
	return -EINPROGRESS;
}

static int qat_alg_enc(struct aead_request *areq)
{
	return qat_alg_enc_internal(areq, areq->iv, 0);
}

static int qat_alg_genivenc(struct aead_givcrypt_request *req)
{
	struct crypto_aead *aead_tfm = crypto_aead_reqtfm(&req->areq);
	struct crypto_tfm *tfm = crypto_aead_tfm(aead_tfm);
	struct qat_alg_session_ctx *ctx = crypto_tfm_ctx(tfm);
	__be64 seq;

	memcpy(req->giv, ctx->salt, AES_BLOCK_SIZE);
	seq = cpu_to_be64(req->seq);
	memcpy(req->giv + AES_BLOCK_SIZE - sizeof(uint64_t),
	       &seq, sizeof(uint64_t));
	return qat_alg_enc_internal(&req->areq, req->giv, 1);
}

static int qat_alg_init(struct crypto_tfm *tfm,
			enum icp_qat_hw_auth_algo hash, const char *hash_name)
{
	struct qat_alg_session_ctx *ctx = crypto_tfm_ctx(tfm);

	memset(ctx, '\0', sizeof(*ctx));
	ctx->hash_tfm = crypto_alloc_shash(hash_name, 0, 0);
	if (IS_ERR(ctx->hash_tfm))
		return -EFAULT;
	spin_lock_init(&ctx->lock);
	ctx->qat_hash_alg = hash;
	tfm->crt_aead.reqsize = sizeof(struct aead_request) +
				sizeof(struct qat_crypto_request);
	ctx->tfm = tfm;
	return 0;
}

static int qat_alg_sha1_init(struct crypto_tfm *tfm)
{
	return qat_alg_init(tfm, ICP_QAT_HW_AUTH_ALGO_SHA1, "sha1");
}

static int qat_alg_sha256_init(struct crypto_tfm *tfm)
{
	return qat_alg_init(tfm, ICP_QAT_HW_AUTH_ALGO_SHA256, "sha256");
}

static int qat_alg_sha512_init(struct crypto_tfm *tfm)
{
	return qat_alg_init(tfm, ICP_QAT_HW_AUTH_ALGO_SHA512, "sha512");
}

static void qat_alg_exit(struct crypto_tfm *tfm)
{
	struct qat_alg_session_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev;

	if (!IS_ERR(ctx->hash_tfm))
		crypto_free_shash(ctx->hash_tfm);

	if (!inst)
		return;

	dev = &GET_DEV(inst->accel_dev);
	if (ctx->enc_cd)
		dma_free_coherent(dev, sizeof(struct qat_alg_cd),
				  ctx->enc_cd, ctx->enc_cd_paddr);
	if (ctx->dec_cd)
		dma_free_coherent(dev, sizeof(struct qat_alg_cd),
				  ctx->dec_cd, ctx->dec_cd_paddr);
	qat_crypto_put_instance(inst);
}

static struct crypto_alg qat_algs[] = { {
	.cra_name = "authenc(hmac(sha1),cbc(aes))",
	.cra_driver_name = "qat_aes_cbc_hmac_sha1",
	.cra_priority = 4001,
	.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct qat_alg_session_ctx),
	.cra_alignmask = 0,
	.cra_type = &crypto_aead_type,
	.cra_module = THIS_MODULE,
	.cra_init = qat_alg_sha1_init,
	.cra_exit = qat_alg_exit,
	.cra_u = {
		.aead = {
			.setkey = qat_alg_setkey,
			.decrypt = qat_alg_dec,
			.encrypt = qat_alg_enc,
			.givencrypt = qat_alg_genivenc,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
	},
}, {
	.cra_name = "authenc(hmac(sha256),cbc(aes))",
	.cra_driver_name = "qat_aes_cbc_hmac_sha256",
	.cra_priority = 4001,
	.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct qat_alg_session_ctx),
	.cra_alignmask = 0,
	.cra_type = &crypto_aead_type,
	.cra_module = THIS_MODULE,
	.cra_init = qat_alg_sha256_init,
	.cra_exit = qat_alg_exit,
	.cra_u = {
		.aead = {
			.setkey = qat_alg_setkey,
			.decrypt = qat_alg_dec,
			.encrypt = qat_alg_enc,
			.givencrypt = qat_alg_genivenc,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
	},
}, {
	.cra_name = "authenc(hmac(sha512),cbc(aes))",
	.cra_driver_name = "qat_aes_cbc_hmac_sha512",
	.cra_priority = 4001,
	.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct qat_alg_session_ctx),
	.cra_alignmask = 0,
	.cra_type = &crypto_aead_type,
	.cra_module = THIS_MODULE,
	.cra_init = qat_alg_sha512_init,
	.cra_exit = qat_alg_exit,
	.cra_u = {
		.aead = {
			.setkey = qat_alg_setkey,
			.decrypt = qat_alg_dec,
			.encrypt = qat_alg_enc,
			.givencrypt = qat_alg_genivenc,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
	},
} };

int qat_algs_register(void)
{
	if (atomic_add_return(1, &active_dev) == 1) {
		int i;

		for (i = 0; i < ARRAY_SIZE(qat_algs); i++)
			qat_algs[i].cra_flags =	CRYPTO_ALG_TYPE_AEAD |
						CRYPTO_ALG_ASYNC;
		return crypto_register_algs(qat_algs, ARRAY_SIZE(qat_algs));
	}
	return 0;
}

int qat_algs_unregister(void)
{
	if (atomic_sub_return(1, &active_dev) == 0)
		return crypto_unregister_algs(qat_algs, ARRAY_SIZE(qat_algs));
	return 0;
}

int qat_algs_init(void)
{
	atomic_set(&active_dev, 0);
	crypto_get_default_rng();
	return 0;
}

void qat_algs_exit(void)
{
	crypto_put_default_rng();
}
