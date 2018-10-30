 /* Algorithms supported by virtio crypto device
  *
  * Authors: Gonglei <arei.gonglei@huawei.com>
  *
  * Copyright 2016 HUAWEI TECHNOLOGIES CO., LTD.
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation; either version 2 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program; if not, see <http://www.gnu.org/licenses/>.
  */

#include <linux/scatterlist.h>
#include <crypto/algapi.h>
#include <linux/err.h>
#include <crypto/scatterwalk.h>
#include <linux/atomic.h>

#include <uapi/linux/virtio_crypto.h>
#include "virtio_crypto_common.h"


struct virtio_crypto_ablkcipher_ctx {
	struct crypto_engine_ctx enginectx;
	struct virtio_crypto *vcrypto;
	struct crypto_tfm *tfm;

	struct virtio_crypto_sym_session_info enc_sess_info;
	struct virtio_crypto_sym_session_info dec_sess_info;
};

struct virtio_crypto_sym_request {
	struct virtio_crypto_request base;

	/* Cipher or aead */
	uint32_t type;
	struct virtio_crypto_ablkcipher_ctx *ablkcipher_ctx;
	struct ablkcipher_request *ablkcipher_req;
	uint8_t *iv;
	/* Encryption? */
	bool encrypt;
};

struct virtio_crypto_algo {
	uint32_t algonum;
	uint32_t service;
	unsigned int active_devs;
	struct crypto_alg algo;
};

/*
 * The algs_lock protects the below global virtio_crypto_active_devs
 * and crypto algorithms registion.
 */
static DEFINE_MUTEX(algs_lock);
static void virtio_crypto_ablkcipher_finalize_req(
	struct virtio_crypto_sym_request *vc_sym_req,
	struct ablkcipher_request *req,
	int err);

static void virtio_crypto_dataq_sym_callback
		(struct virtio_crypto_request *vc_req, int len)
{
	struct virtio_crypto_sym_request *vc_sym_req =
		container_of(vc_req, struct virtio_crypto_sym_request, base);
	struct ablkcipher_request *ablk_req;
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
		ablk_req = vc_sym_req->ablkcipher_req;
		virtio_crypto_ablkcipher_finalize_req(vc_sym_req,
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
		pr_err("virtio_crypto: Unsupported key length: %d\n",
			key_len);
		return -EINVAL;
	}
	return 0;
}

static int virtio_crypto_alg_ablkcipher_init_session(
		struct virtio_crypto_ablkcipher_ctx *ctx,
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
	uint8_t *cipher_key = kmalloc(keylen, GFP_ATOMIC);

	if (!cipher_key)
		return -ENOMEM;

	memcpy(cipher_key, key, keylen);

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

static int virtio_crypto_alg_ablkcipher_close_session(
		struct virtio_crypto_ablkcipher_ctx *ctx,
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

static int virtio_crypto_alg_ablkcipher_init_sessions(
		struct virtio_crypto_ablkcipher_ctx *ctx,
		const uint8_t *key, unsigned int keylen)
{
	uint32_t alg;
	int ret;
	struct virtio_crypto *vcrypto = ctx->vcrypto;

	if (keylen > vcrypto->max_cipher_key_len) {
		pr_err("virtio_crypto: the key is too long\n");
		goto bad_key;
	}

	if (virtio_crypto_alg_validate_key(keylen, &alg))
		goto bad_key;

	/* Create encryption session */
	ret = virtio_crypto_alg_ablkcipher_init_session(ctx,
			alg, key, keylen, 1);
	if (ret)
		return ret;
	/* Create decryption session */
	ret = virtio_crypto_alg_ablkcipher_init_session(ctx,
			alg, key, keylen, 0);
	if (ret) {
		virtio_crypto_alg_ablkcipher_close_session(ctx, 1);
		return ret;
	}
	return 0;

bad_key:
	crypto_tfm_set_flags(ctx->tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
}

/* Note: kernel crypto API realization */
static int virtio_crypto_ablkcipher_setkey(struct crypto_ablkcipher *tfm,
					 const uint8_t *key,
					 unsigned int keylen)
{
	struct virtio_crypto_ablkcipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
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
		virtio_crypto_alg_ablkcipher_close_session(ctx, 1);
		virtio_crypto_alg_ablkcipher_close_session(ctx, 0);
	}

	ret = virtio_crypto_alg_ablkcipher_init_sessions(ctx, key, keylen);
	if (ret) {
		virtcrypto_dev_put(ctx->vcrypto);
		ctx->vcrypto = NULL;

		return ret;
	}

	return 0;
}

static int
__virtio_crypto_ablkcipher_do_req(struct virtio_crypto_sym_request *vc_sym_req,
		struct ablkcipher_request *req,
		struct data_queue *data_vq)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct virtio_crypto_ablkcipher_ctx *ctx = vc_sym_req->ablkcipher_ctx;
	struct virtio_crypto_request *vc_req = &vc_sym_req->base;
	unsigned int ivsize = crypto_ablkcipher_ivsize(tfm);
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	struct virtio_crypto_op_data_req *req_data;
	int src_nents, dst_nents;
	int err;
	unsigned long flags;
	struct scatterlist outhdr, iv_sg, status_sg, **sgs;
	int i;
	u64 dst_len;
	unsigned int num_out = 0, num_in = 0;
	int sg_total;
	uint8_t *iv;

	src_nents = sg_nents_for_len(req->src, req->nbytes);
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
			cpu_to_le32(req->nbytes);

	dst_len = virtio_crypto_alg_sg_nents_length(req->dst);
	if (unlikely(dst_len > U32_MAX)) {
		pr_err("virtio_crypto: The dst_len is beyond U32_MAX\n");
		err = -EINVAL;
		goto free;
	}

	pr_debug("virtio_crypto: src_len: %u, dst_len: %llu\n",
			req->nbytes, dst_len);

	if (unlikely(req->nbytes + dst_len + ivsize +
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
	memcpy(iv, req->info, ivsize);
	sg_init_one(&iv_sg, iv, ivsize);
	sgs[num_out++] = &iv_sg;
	vc_sym_req->iv = iv;

	/* Source data */
	for (i = 0; i < src_nents; i++)
		sgs[num_out++] = &req->src[i];

	/* Destination data */
	for (i = 0; i < dst_nents; i++)
		sgs[num_out + num_in++] = &req->dst[i];

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

static int virtio_crypto_ablkcipher_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *atfm = crypto_ablkcipher_reqtfm(req);
	struct virtio_crypto_ablkcipher_ctx *ctx = crypto_ablkcipher_ctx(atfm);
	struct virtio_crypto_sym_request *vc_sym_req =
				ablkcipher_request_ctx(req);
	struct virtio_crypto_request *vc_req = &vc_sym_req->base;
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	/* Use the first data virtqueue as default */
	struct data_queue *data_vq = &vcrypto->data_vq[0];

	vc_req->dataq = data_vq;
	vc_req->alg_cb = virtio_crypto_dataq_sym_callback;
	vc_sym_req->ablkcipher_ctx = ctx;
	vc_sym_req->ablkcipher_req = req;
	vc_sym_req->encrypt = true;

	return crypto_transfer_ablkcipher_request_to_engine(data_vq->engine, req);
}

static int virtio_crypto_ablkcipher_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *atfm = crypto_ablkcipher_reqtfm(req);
	struct virtio_crypto_ablkcipher_ctx *ctx = crypto_ablkcipher_ctx(atfm);
	struct virtio_crypto_sym_request *vc_sym_req =
				ablkcipher_request_ctx(req);
	struct virtio_crypto_request *vc_req = &vc_sym_req->base;
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	/* Use the first data virtqueue as default */
	struct data_queue *data_vq = &vcrypto->data_vq[0];

	vc_req->dataq = data_vq;
	vc_req->alg_cb = virtio_crypto_dataq_sym_callback;
	vc_sym_req->ablkcipher_ctx = ctx;
	vc_sym_req->ablkcipher_req = req;
	vc_sym_req->encrypt = false;

	return crypto_transfer_ablkcipher_request_to_engine(data_vq->engine, req);
}

static int virtio_crypto_ablkcipher_init(struct crypto_tfm *tfm)
{
	struct virtio_crypto_ablkcipher_ctx *ctx = crypto_tfm_ctx(tfm);

	tfm->crt_ablkcipher.reqsize = sizeof(struct virtio_crypto_sym_request);
	ctx->tfm = tfm;

	ctx->enginectx.op.do_one_request = virtio_crypto_ablkcipher_crypt_req;
	ctx->enginectx.op.prepare_request = NULL;
	ctx->enginectx.op.unprepare_request = NULL;
	return 0;
}

static void virtio_crypto_ablkcipher_exit(struct crypto_tfm *tfm)
{
	struct virtio_crypto_ablkcipher_ctx *ctx = crypto_tfm_ctx(tfm);

	if (!ctx->vcrypto)
		return;

	virtio_crypto_alg_ablkcipher_close_session(ctx, 1);
	virtio_crypto_alg_ablkcipher_close_session(ctx, 0);
	virtcrypto_dev_put(ctx->vcrypto);
	ctx->vcrypto = NULL;
}

int virtio_crypto_ablkcipher_crypt_req(
	struct crypto_engine *engine, void *vreq)
{
	struct ablkcipher_request *req = container_of(vreq, struct ablkcipher_request, base);
	struct virtio_crypto_sym_request *vc_sym_req =
				ablkcipher_request_ctx(req);
	struct virtio_crypto_request *vc_req = &vc_sym_req->base;
	struct data_queue *data_vq = vc_req->dataq;
	int ret;

	ret = __virtio_crypto_ablkcipher_do_req(vc_sym_req, req, data_vq);
	if (ret < 0)
		return ret;

	virtqueue_kick(data_vq->vq);

	return 0;
}

static void virtio_crypto_ablkcipher_finalize_req(
	struct virtio_crypto_sym_request *vc_sym_req,
	struct ablkcipher_request *req,
	int err)
{
	crypto_finalize_ablkcipher_request(vc_sym_req->base.dataq->engine,
					   req, err);
	kzfree(vc_sym_req->iv);
	virtcrypto_clear_request(&vc_sym_req->base);
}

static struct virtio_crypto_algo virtio_crypto_algs[] = { {
	.algonum = VIRTIO_CRYPTO_CIPHER_AES_CBC,
	.service = VIRTIO_CRYPTO_SERVICE_CIPHER,
	.algo = {
		.cra_name = "cbc(aes)",
		.cra_driver_name = "virtio_crypto_aes_cbc",
		.cra_priority = 150,
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize  = sizeof(struct virtio_crypto_ablkcipher_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
		.cra_type = &crypto_ablkcipher_type,
		.cra_init = virtio_crypto_ablkcipher_init,
		.cra_exit = virtio_crypto_ablkcipher_exit,
		.cra_u = {
			.ablkcipher = {
				.setkey = virtio_crypto_ablkcipher_setkey,
				.decrypt = virtio_crypto_ablkcipher_decrypt,
				.encrypt = virtio_crypto_ablkcipher_encrypt,
				.min_keysize = AES_MIN_KEY_SIZE,
				.max_keysize = AES_MAX_KEY_SIZE,
				.ivsize = AES_BLOCK_SIZE,
			},
		},
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
			ret = crypto_register_alg(&virtio_crypto_algs[i].algo);
			if (ret)
				goto unlock;
		}

		virtio_crypto_algs[i].active_devs++;
		dev_info(&vcrypto->vdev->dev, "Registered algo %s\n",
			 virtio_crypto_algs[i].algo.cra_name);
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
			crypto_unregister_alg(&virtio_crypto_algs[i].algo);

		virtio_crypto_algs[i].active_devs--;
	}

	mutex_unlock(&algs_lock);
}
