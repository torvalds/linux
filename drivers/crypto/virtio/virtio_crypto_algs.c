// SPDX-License-Identifier: GPL-2.0-or-later
 /* Algorithms supported by virtio crypto device
  *
  * Authors: Gonglei <arei.gonglei@huawei.com>
  *
  * Copyright 2016 HUAWEI TECHNOLOGIES CO., LTD.
  */

#include <linux/scatterlist.h>
#include <crypto/algapi.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <crypto/scatterwalk.h>
#include <linux/atomic.h>

#include <uapi/linux/virtio_crypto.h>
#include "virtio_crypto_common.h"


struct virtio_crypto_skcipher_ctx {
	struct crypto_engine_ctx enginectx;
	struct virtio_crypto *vcrypto;
	struct crypto_skcipher *tfm;

	struct virtio_crypto_sym_session_info enc_sess_info;
	struct virtio_crypto_sym_session_info dec_sess_info;
};

struct virtio_crypto_sym_request {
	struct virtio_crypto_request base;

	/* Cipher or aead */
	uint32_t type;
	struct virtio_crypto_skcipher_ctx *skcipher_ctx;
	struct skcipher_request *skcipher_req;
	uint8_t *iv;
	/* Encryption? */
	bool encrypt;
};

struct virtio_crypto_algo {
	uint32_t algonum;
	uint32_t service;
	unsigned int active_devs;
	struct skcipher_alg algo;
};

/*
 * The algs_lock protects the below global virtio_crypto_active_devs
 * and crypto algorithms registion.
 */
static DEFINE_MUTEX(algs_lock);
static void virtio_crypto_skcipher_finalize_req(
	struct virtio_crypto_sym_request *vc_sym_req,
	struct skcipher_request *req,
	int err);

static void virtio_crypto_dataq_sym_callback
		(struct virtio_crypto_request *vc_req, int len)
{
	struct virtio_crypto_sym_request *vc_sym_req =
		container_of(vc_req, struct virtio_crypto_sym_request, base);
	struct skcipher_request *ablk_req;
	int error;

	/* Finish the encrypt or decrypt process */
	if (vc_sym_req->type == VIRTIO_CRYPTO_SYM_OP_CIPHER) {
		switch (vc_req->status) {
		case VIRTIO_CRYPTO_OK:
			error = 0;
			break;
		case VIRTIO_CRYPTO_INVSESS:
		case VIRTIO_CRYPTO_ERR:
			error = -EINVAL;
			break;
		case VIRTIO_CRYPTO_BADMSG:
			error = -EBADMSG;
			break;
		default:
			error = -EIO;
			break;
		}
		ablk_req = vc_sym_req->skcipher_req;
		virtio_crypto_skcipher_finalize_req(vc_sym_req,
							ablk_req, error);
	}
}

static u64 virtio_crypto_alg_sg_nents_length(struct scatterlist *sg)
{
	u64 total = 0;

	for (total = 0; sg; sg = sg_next(sg))
		total += sg->length;

	return total;
}

static int
virtio_crypto_alg_validate_key(int key_len, uint32_t *alg)
{
	switch (key_len) {
	case AES_KEYSIZE_128:
	case AES_KEYSIZE_192:
	case AES_KEYSIZE_256:
		*alg = VIRTIO_CRYPTO_CIPHER_AES_CBC;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int virtio_crypto_alg_skcipher_init_session(
		struct virtio_crypto_skcipher_ctx *ctx,
		uint32_t alg, const uint8_t *key,
		unsigned int keylen,
		int encrypt)
{
	struct scatterlist outhdr, key_sg, inhdr, *sgs[3];
	unsigned int tmp;
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	int op = encrypt ? VIRTIO_CRYPTO_OP_ENCRYPT : VIRTIO_CRYPTO_OP_DECRYPT;
	int err;
	unsigned int num_out = 0, num_in = 0;

	/*
	 * Avoid to do DMA from the stack, switch to using
	 * dynamically-allocated for the key
	 */
	uint8_t *cipher_key = kmemdup(key, keylen, GFP_ATOMIC);

	if (!cipher_key)
		return -ENOMEM;

	spin_lock(&vcrypto->ctrl_lock);
	/* Pad ctrl header */
	vcrypto->ctrl.header.opcode =
		cpu_to_le32(VIRTIO_CRYPTO_CIPHER_CREATE_SESSION);
	vcrypto->ctrl.header.algo = cpu_to_le32(alg);
	/* Set the default dataqueue id to 0 */
	vcrypto->ctrl.header.queue_id = 0;

	vcrypto->input.status = cpu_to_le32(VIRTIO_CRYPTO_ERR);
	/* Pad cipher's parameters */
	vcrypto->ctrl.u.sym_create_session.op_type =
		cpu_to_le32(VIRTIO_CRYPTO_SYM_OP_CIPHER);
	vcrypto->ctrl.u.sym_create_session.u.cipher.para.algo =
		vcrypto->ctrl.header.algo;
	vcrypto->ctrl.u.sym_create_session.u.cipher.para.keylen =
		cpu_to_le32(keylen);
	vcrypto->ctrl.u.sym_create_session.u.cipher.para.op =
		cpu_to_le32(op);

	sg_init_one(&outhdr, &vcrypto->ctrl, sizeof(vcrypto->ctrl));
	sgs[num_out++] = &outhdr;

	/* Set key */
	sg_init_one(&key_sg, cipher_key, keylen);
	sgs[num_out++] = &key_sg;

	/* Return status and session id back */
	sg_init_one(&inhdr, &vcrypto->input, sizeof(vcrypto->input));
	sgs[num_out + num_in++] = &inhdr;

	err = virtqueue_add_sgs(vcrypto->ctrl_vq, sgs, num_out,
				num_in, vcrypto, GFP_ATOMIC);
	if (err < 0) {
		spin_unlock(&vcrypto->ctrl_lock);
		kzfree(cipher_key);
		return err;
	}
	virtqueue_kick(vcrypto->ctrl_vq);

	/*
	 * Trapping into the hypervisor, so the request should be
	 * handled immediately.
	 */
	while (!virtqueue_get_buf(vcrypto->ctrl_vq, &tmp) &&
	       !virtqueue_is_broken(vcrypto->ctrl_vq))
		cpu_relax();

	if (le32_to_cpu(vcrypto->input.status) != VIRTIO_CRYPTO_OK) {
		spin_unlock(&vcrypto->ctrl_lock);
		pr_err("virtio_crypto: Create session failed status: %u\n",
			le32_to_cpu(vcrypto->input.status));
		kzfree(cipher_key);
		return -EINVAL;
	}

	if (encrypt)
		ctx->enc_sess_info.session_id =
			le64_to_cpu(vcrypto->input.session_id);
	else
		ctx->dec_sess_info.session_id =
			le64_to_cpu(vcrypto->input.session_id);

	spin_unlock(&vcrypto->ctrl_lock);

	kzfree(cipher_key);
	return 0;
}

static int virtio_crypto_alg_skcipher_close_session(
		struct virtio_crypto_skcipher_ctx *ctx,
		int encrypt)
{
	struct scatterlist outhdr, status_sg, *sgs[2];
	unsigned int tmp;
	struct virtio_crypto_destroy_session_req *destroy_session;
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	int err;
	unsigned int num_out = 0, num_in = 0;

	spin_lock(&vcrypto->ctrl_lock);
	vcrypto->ctrl_status.status = VIRTIO_CRYPTO_ERR;
	/* Pad ctrl header */
	vcrypto->ctrl.header.opcode =
		cpu_to_le32(VIRTIO_CRYPTO_CIPHER_DESTROY_SESSION);
	/* Set the default virtqueue id to 0 */
	vcrypto->ctrl.header.queue_id = 0;

	destroy_session = &vcrypto->ctrl.u.destroy_session;

	if (encrypt)
		destroy_session->session_id =
			cpu_to_le64(ctx->enc_sess_info.session_id);
	else
		destroy_session->session_id =
			cpu_to_le64(ctx->dec_sess_info.session_id);

	sg_init_one(&outhdr, &vcrypto->ctrl, sizeof(vcrypto->ctrl));
	sgs[num_out++] = &outhdr;

	/* Return status and session id back */
	sg_init_one(&status_sg, &vcrypto->ctrl_status.status,
		sizeof(vcrypto->ctrl_status.status));
	sgs[num_out + num_in++] = &status_sg;

	err = virtqueue_add_sgs(vcrypto->ctrl_vq, sgs, num_out,
			num_in, vcrypto, GFP_ATOMIC);
	if (err < 0) {
		spin_unlock(&vcrypto->ctrl_lock);
		return err;
	}
	virtqueue_kick(vcrypto->ctrl_vq);

	while (!virtqueue_get_buf(vcrypto->ctrl_vq, &tmp) &&
	       !virtqueue_is_broken(vcrypto->ctrl_vq))
		cpu_relax();

	if (vcrypto->ctrl_status.status != VIRTIO_CRYPTO_OK) {
		spin_unlock(&vcrypto->ctrl_lock);
		pr_err("virtio_crypto: Close session failed status: %u, session_id: 0x%llx\n",
			vcrypto->ctrl_status.status,
			destroy_session->session_id);

		return -EINVAL;
	}
	spin_unlock(&vcrypto->ctrl_lock);

	return 0;
}

static int virtio_crypto_alg_skcipher_init_sessions(
		struct virtio_crypto_skcipher_ctx *ctx,
		const uint8_t *key, unsigned int keylen)
{
	uint32_t alg;
	int ret;
	struct virtio_crypto *vcrypto = ctx->vcrypto;

	if (keylen > vcrypto->max_cipher_key_len) {
		pr_err("virtio_crypto: the key is too long\n");
		return -EINVAL;
	}

	if (virtio_crypto_alg_validate_key(keylen, &alg))
		return -EINVAL;

	/* Create encryption session */
	ret = virtio_crypto_alg_skcipher_init_session(ctx,
			alg, key, keylen, 1);
	if (ret)
		return ret;
	/* Create decryption session */
	ret = virtio_crypto_alg_skcipher_init_session(ctx,
			alg, key, keylen, 0);
	if (ret) {
		virtio_crypto_alg_skcipher_close_session(ctx, 1);
		return ret;
	}
	return 0;
}

/* Note: kernel crypto API realization */
static int virtio_crypto_skcipher_setkey(struct crypto_skcipher *tfm,
					 const uint8_t *key,
					 unsigned int keylen)
{
	struct virtio_crypto_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	uint32_t alg;
	int ret;

	ret = virtio_crypto_alg_validate_key(keylen, &alg);
	if (ret)
		return ret;

	if (!ctx->vcrypto) {
		/* New key */
		int node = virtio_crypto_get_current_node();
		struct virtio_crypto *vcrypto =
				      virtcrypto_get_dev_node(node,
				      VIRTIO_CRYPTO_SERVICE_CIPHER, alg);
		if (!vcrypto) {
			pr_err("virtio_crypto: Could not find a virtio device in the system or unsupported algo\n");
			return -ENODEV;
		}

		ctx->vcrypto = vcrypto;
	} else {
		/* Rekeying, we should close the created sessions previously */
		virtio_crypto_alg_skcipher_close_session(ctx, 1);
		virtio_crypto_alg_skcipher_close_session(ctx, 0);
	}

	ret = virtio_crypto_alg_skcipher_init_sessions(ctx, key, keylen);
	if (ret) {
		virtcrypto_dev_put(ctx->vcrypto);
		ctx->vcrypto = NULL;

		return ret;
	}

	return 0;
}

static int
__virtio_crypto_skcipher_do_req(struct virtio_crypto_sym_request *vc_sym_req,
		struct skcipher_request *req,
		struct data_queue *data_vq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct virtio_crypto_skcipher_ctx *ctx = vc_sym_req->skcipher_ctx;
	struct virtio_crypto_request *vc_req = &vc_sym_req->base;
	unsigned int ivsize = crypto_skcipher_ivsize(tfm);
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	struct virtio_crypto_op_data_req *req_data;
	int src_nents, dst_nents;
	int err;
	unsigned long flags;
	struct scatterlist outhdr, iv_sg, status_sg, **sgs;
	u64 dst_len;
	unsigned int num_out = 0, num_in = 0;
	int sg_total;
	uint8_t *iv;
	struct scatterlist *sg;

	src_nents = sg_nents_for_len(req->src, req->cryptlen);
	if (src_nents < 0) {
		pr_err("Invalid number of src SG.\n");
		return src_nents;
	}

	dst_nents = sg_nents(req->dst);

	pr_debug("virtio_crypto: Number of sgs (src_nents: %d, dst_nents: %d)\n",
			src_nents, dst_nents);

	/* Why 3?  outhdr + iv + inhdr */
	sg_total = src_nents + dst_nents + 3;
	sgs = kcalloc_node(sg_total, sizeof(*sgs), GFP_KERNEL,
				dev_to_node(&vcrypto->vdev->dev));
	if (!sgs)
		return -ENOMEM;

	req_data = kzalloc_node(sizeof(*req_data), GFP_KERNEL,
				dev_to_node(&vcrypto->vdev->dev));
	if (!req_data) {
		kfree(sgs);
		return -ENOMEM;
	}

	vc_req->req_data = req_data;
	vc_sym_req->type = VIRTIO_CRYPTO_SYM_OP_CIPHER;
	/* Head of operation */
	if (vc_sym_req->encrypt) {
		req_data->header.session_id =
			cpu_to_le64(ctx->enc_sess_info.session_id);
		req_data->header.opcode =
			cpu_to_le32(VIRTIO_CRYPTO_CIPHER_ENCRYPT);
	} else {
		req_data->header.session_id =
			cpu_to_le64(ctx->dec_sess_info.session_id);
		req_data->header.opcode =
			cpu_to_le32(VIRTIO_CRYPTO_CIPHER_DECRYPT);
	}
	req_data->u.sym_req.op_type = cpu_to_le32(VIRTIO_CRYPTO_SYM_OP_CIPHER);
	req_data->u.sym_req.u.cipher.para.iv_len = cpu_to_le32(ivsize);
	req_data->u.sym_req.u.cipher.para.src_data_len =
			cpu_to_le32(req->cryptlen);

	dst_len = virtio_crypto_alg_sg_nents_length(req->dst);
	if (unlikely(dst_len > U32_MAX)) {
		pr_err("virtio_crypto: The dst_len is beyond U32_MAX\n");
		err = -EINVAL;
		goto free;
	}

	dst_len = min_t(unsigned int, req->cryptlen, dst_len);
	pr_debug("virtio_crypto: src_len: %u, dst_len: %llu\n",
			req->cryptlen, dst_len);

	if (unlikely(req->cryptlen + dst_len + ivsize +
		sizeof(vc_req->status) > vcrypto->max_size)) {
		pr_err("virtio_crypto: The length is too big\n");
		err = -EINVAL;
		goto free;
	}

	req_data->u.sym_req.u.cipher.para.dst_data_len =
			cpu_to_le32((uint32_t)dst_len);

	/* Outhdr */
	sg_init_one(&outhdr, req_data, sizeof(*req_data));
	sgs[num_out++] = &outhdr;

	/* IV */

	/*
	 * Avoid to do DMA from the stack, switch to using
	 * dynamically-allocated for the IV
	 */
	iv = kzalloc_node(ivsize, GFP_ATOMIC,
				dev_to_node(&vcrypto->vdev->dev));
	if (!iv) {
		err = -ENOMEM;
		goto free;
	}
	memcpy(iv, req->iv, ivsize);
	if (!vc_sym_req->encrypt)
		scatterwalk_map_and_copy(req->iv, req->src,
					 req->cryptlen - AES_BLOCK_SIZE,
					 AES_BLOCK_SIZE, 0);

	sg_init_one(&iv_sg, iv, ivsize);
	sgs[num_out++] = &iv_sg;
	vc_sym_req->iv = iv;

	/* Source data */
	for (sg = req->src; src_nents; sg = sg_next(sg), src_nents--)
		sgs[num_out++] = sg;

	/* Destination data */
	for (sg = req->dst; sg; sg = sg_next(sg))
		sgs[num_out + num_in++] = sg;

	/* Status */
	sg_init_one(&status_sg, &vc_req->status, sizeof(vc_req->status));
	sgs[num_out + num_in++] = &status_sg;

	vc_req->sgs = sgs;

	spin_lock_irqsave(&data_vq->lock, flags);
	err = virtqueue_add_sgs(data_vq->vq, sgs, num_out,
				num_in, vc_req, GFP_ATOMIC);
	virtqueue_kick(data_vq->vq);
	spin_unlock_irqrestore(&data_vq->lock, flags);
	if (unlikely(err < 0))
		goto free_iv;

	return 0;

free_iv:
	kzfree(iv);
free:
	kzfree(req_data);
	kfree(sgs);
	return err;
}

static int virtio_crypto_skcipher_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *atfm = crypto_skcipher_reqtfm(req);
	struct virtio_crypto_skcipher_ctx *ctx = crypto_skcipher_ctx(atfm);
	struct virtio_crypto_sym_request *vc_sym_req =
				skcipher_request_ctx(req);
	struct virtio_crypto_request *vc_req = &vc_sym_req->base;
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	/* Use the first data virtqueue as default */
	struct data_queue *data_vq = &vcrypto->data_vq[0];

	if (!req->cryptlen)
		return 0;
	if (req->cryptlen % AES_BLOCK_SIZE)
		return -EINVAL;

	vc_req->dataq = data_vq;
	vc_req->alg_cb = virtio_crypto_dataq_sym_callback;
	vc_sym_req->skcipher_ctx = ctx;
	vc_sym_req->skcipher_req = req;
	vc_sym_req->encrypt = true;

	return crypto_transfer_skcipher_request_to_engine(data_vq->engine, req);
}

static int virtio_crypto_skcipher_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *atfm = crypto_skcipher_reqtfm(req);
	struct virtio_crypto_skcipher_ctx *ctx = crypto_skcipher_ctx(atfm);
	struct virtio_crypto_sym_request *vc_sym_req =
				skcipher_request_ctx(req);
	struct virtio_crypto_request *vc_req = &vc_sym_req->base;
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	/* Use the first data virtqueue as default */
	struct data_queue *data_vq = &vcrypto->data_vq[0];

	if (!req->cryptlen)
		return 0;
	if (req->cryptlen % AES_BLOCK_SIZE)
		return -EINVAL;

	vc_req->dataq = data_vq;
	vc_req->alg_cb = virtio_crypto_dataq_sym_callback;
	vc_sym_req->skcipher_ctx = ctx;
	vc_sym_req->skcipher_req = req;
	vc_sym_req->encrypt = false;

	return crypto_transfer_skcipher_request_to_engine(data_vq->engine, req);
}

static int virtio_crypto_skcipher_init(struct crypto_skcipher *tfm)
{
	struct virtio_crypto_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_skcipher_set_reqsize(tfm, sizeof(struct virtio_crypto_sym_request));
	ctx->tfm = tfm;

	ctx->enginectx.op.do_one_request = virtio_crypto_skcipher_crypt_req;
	ctx->enginectx.op.prepare_request = NULL;
	ctx->enginectx.op.unprepare_request = NULL;
	return 0;
}

static void virtio_crypto_skcipher_exit(struct crypto_skcipher *tfm)
{
	struct virtio_crypto_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (!ctx->vcrypto)
		return;

	virtio_crypto_alg_skcipher_close_session(ctx, 1);
	virtio_crypto_alg_skcipher_close_session(ctx, 0);
	virtcrypto_dev_put(ctx->vcrypto);
	ctx->vcrypto = NULL;
}

int virtio_crypto_skcipher_crypt_req(
	struct crypto_engine *engine, void *vreq)
{
	struct skcipher_request *req = container_of(vreq, struct skcipher_request, base);
	struct virtio_crypto_sym_request *vc_sym_req =
				skcipher_request_ctx(req);
	struct virtio_crypto_request *vc_req = &vc_sym_req->base;
	struct data_queue *data_vq = vc_req->dataq;
	int ret;

	ret = __virtio_crypto_skcipher_do_req(vc_sym_req, req, data_vq);
	if (ret < 0)
		return ret;

	virtqueue_kick(data_vq->vq);

	return 0;
}

static void virtio_crypto_skcipher_finalize_req(
	struct virtio_crypto_sym_request *vc_sym_req,
	struct skcipher_request *req,
	int err)
{
	if (vc_sym_req->encrypt)
		scatterwalk_map_and_copy(req->iv, req->dst,
					 req->cryptlen - AES_BLOCK_SIZE,
					 AES_BLOCK_SIZE, 0);
	kzfree(vc_sym_req->iv);
	virtcrypto_clear_request(&vc_sym_req->base);

	crypto_finalize_skcipher_request(vc_sym_req->base.dataq->engine,
					   req, err);
}

static struct virtio_crypto_algo virtio_crypto_algs[] = { {
	.algonum = VIRTIO_CRYPTO_CIPHER_AES_CBC,
	.service = VIRTIO_CRYPTO_SERVICE_CIPHER,
	.algo = {
		.base.cra_name		= "cbc(aes)",
		.base.cra_driver_name	= "virtio_crypto_aes_cbc",
		.base.cra_priority	= 150,
		.base.cra_flags		= CRYPTO_ALG_ASYNC,
		.base.cra_blocksize	= AES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct virtio_crypto_skcipher_ctx),
		.base.cra_module	= THIS_MODULE,
		.init			= virtio_crypto_skcipher_init,
		.exit			= virtio_crypto_skcipher_exit,
		.setkey			= virtio_crypto_skcipher_setkey,
		.decrypt		= virtio_crypto_skcipher_decrypt,
		.encrypt		= virtio_crypto_skcipher_encrypt,
		.min_keysize		= AES_MIN_KEY_SIZE,
		.max_keysize		= AES_MAX_KEY_SIZE,
		.ivsize			= AES_BLOCK_SIZE,
	},
} };

int virtio_crypto_algs_register(struct virtio_crypto *vcrypto)
{
	int ret = 0;
	int i = 0;

	mutex_lock(&algs_lock);

	for (i = 0; i < ARRAY_SIZE(virtio_crypto_algs); i++) {

		uint32_t service = virtio_crypto_algs[i].service;
		uint32_t algonum = virtio_crypto_algs[i].algonum;

		if (!virtcrypto_algo_is_supported(vcrypto, service, algonum))
			continue;

		if (virtio_crypto_algs[i].active_devs == 0) {
			ret = crypto_register_skcipher(&virtio_crypto_algs[i].algo);
			if (ret)
				goto unlock;
		}

		virtio_crypto_algs[i].active_devs++;
		dev_info(&vcrypto->vdev->dev, "Registered algo %s\n",
			 virtio_crypto_algs[i].algo.base.cra_name);
	}

unlock:
	mutex_unlock(&algs_lock);
	return ret;
}

void virtio_crypto_algs_unregister(struct virtio_crypto *vcrypto)
{
	int i = 0;

	mutex_lock(&algs_lock);

	for (i = 0; i < ARRAY_SIZE(virtio_crypto_algs); i++) {

		uint32_t service = virtio_crypto_algs[i].service;
		uint32_t algonum = virtio_crypto_algs[i].algonum;

		if (virtio_crypto_algs[i].active_devs == 0 ||
		    !virtcrypto_algo_is_supported(vcrypto, service, algonum))
			continue;

		if (virtio_crypto_algs[i].active_devs == 1)
			crypto_unregister_skcipher(&virtio_crypto_algs[i].algo);

		virtio_crypto_algs[i].active_devs--;
	}

	mutex_unlock(&algs_lock);
}
