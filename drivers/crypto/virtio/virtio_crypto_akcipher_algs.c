// SPDX-License-Identifier: GPL-2.0-or-later
 /* Asymmetric algorithms supported by virtio crypto device
  *
  * Authors: zhenwei pi <pizhenwei@bytedance.com>
  *          lei he <helei.sig11@bytedance.com>
  *
  * Copyright 2022 Bytedance CO., LTD.
  */

#include <linux/mpi.h>
#include <linux/scatterlist.h>
#include <crypto/algapi.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/rsa.h>
#include <linux/err.h>
#include <crypto/scatterwalk.h>
#include <linux/atomic.h>

#include <uapi/linux/virtio_crypto.h>
#include "virtio_crypto_common.h"

struct virtio_crypto_rsa_ctx {
	MPI n;
};

struct virtio_crypto_akcipher_ctx {
	struct crypto_engine_ctx enginectx;
	struct virtio_crypto *vcrypto;
	struct crypto_akcipher *tfm;
	bool session_valid;
	__u64 session_id;
	union {
		struct virtio_crypto_rsa_ctx rsa_ctx;
	};
};

struct virtio_crypto_akcipher_request {
	struct virtio_crypto_request base;
	struct virtio_crypto_akcipher_ctx *akcipher_ctx;
	struct akcipher_request *akcipher_req;
	void *src_buf;
	void *dst_buf;
	uint32_t opcode;
};

struct virtio_crypto_akcipher_algo {
	uint32_t algonum;
	uint32_t service;
	unsigned int active_devs;
	struct akcipher_alg algo;
};

static DEFINE_MUTEX(algs_lock);

static void virtio_crypto_akcipher_finalize_req(
	struct virtio_crypto_akcipher_request *vc_akcipher_req,
	struct akcipher_request *req, int err)
{
	kfree(vc_akcipher_req->src_buf);
	kfree(vc_akcipher_req->dst_buf);
	vc_akcipher_req->src_buf = NULL;
	vc_akcipher_req->dst_buf = NULL;
	virtcrypto_clear_request(&vc_akcipher_req->base);

	crypto_finalize_akcipher_request(vc_akcipher_req->base.dataq->engine, req, err);
}

static void virtio_crypto_dataq_akcipher_callback(struct virtio_crypto_request *vc_req, int len)
{
	struct virtio_crypto_akcipher_request *vc_akcipher_req =
		container_of(vc_req, struct virtio_crypto_akcipher_request, base);
	struct akcipher_request *akcipher_req;
	int error;

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

	case VIRTIO_CRYPTO_KEY_REJECTED:
		error = -EKEYREJECTED;
		break;

	default:
		error = -EIO;
		break;
	}

	akcipher_req = vc_akcipher_req->akcipher_req;
	if (vc_akcipher_req->opcode != VIRTIO_CRYPTO_AKCIPHER_VERIFY) {
		/* actuall length maybe less than dst buffer */
		akcipher_req->dst_len = len - sizeof(vc_req->status);
		sg_copy_from_buffer(akcipher_req->dst, sg_nents(akcipher_req->dst),
				    vc_akcipher_req->dst_buf, akcipher_req->dst_len);
	}
	virtio_crypto_akcipher_finalize_req(vc_akcipher_req, akcipher_req, error);
}

static int virtio_crypto_alg_akcipher_init_session(struct virtio_crypto_akcipher_ctx *ctx,
		struct virtio_crypto_ctrl_header *header, void *para,
		const uint8_t *key, unsigned int keylen)
{
	struct scatterlist outhdr_sg, key_sg, inhdr_sg, *sgs[3];
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	uint8_t *pkey;
	int err;
	unsigned int num_out = 0, num_in = 0;
	struct virtio_crypto_op_ctrl_req *ctrl;
	struct virtio_crypto_session_input *input;
	struct virtio_crypto_ctrl_request *vc_ctrl_req;

	pkey = kmemdup(key, keylen, GFP_ATOMIC);
	if (!pkey)
		return -ENOMEM;

	vc_ctrl_req = kzalloc(sizeof(*vc_ctrl_req), GFP_KERNEL);
	if (!vc_ctrl_req) {
		err = -ENOMEM;
		goto out;
	}

	ctrl = &vc_ctrl_req->ctrl;
	memcpy(&ctrl->header, header, sizeof(ctrl->header));
	memcpy(&ctrl->u, para, sizeof(ctrl->u));
	input = &vc_ctrl_req->input;
	input->status = cpu_to_le32(VIRTIO_CRYPTO_ERR);

	sg_init_one(&outhdr_sg, ctrl, sizeof(*ctrl));
	sgs[num_out++] = &outhdr_sg;

	sg_init_one(&key_sg, pkey, keylen);
	sgs[num_out++] = &key_sg;

	sg_init_one(&inhdr_sg, input, sizeof(*input));
	sgs[num_out + num_in++] = &inhdr_sg;

	err = virtio_crypto_ctrl_vq_request(vcrypto, sgs, num_out, num_in, vc_ctrl_req);
	if (err < 0)
		goto out;

	if (le32_to_cpu(input->status) != VIRTIO_CRYPTO_OK) {
		pr_err("virtio_crypto: Create session failed status: %u\n",
			le32_to_cpu(input->status));
		err = -EINVAL;
		goto out;
	}

	ctx->session_id = le64_to_cpu(input->session_id);
	ctx->session_valid = true;
	err = 0;

out:
	kfree(vc_ctrl_req);
	kfree_sensitive(pkey);

	return err;
}

static int virtio_crypto_alg_akcipher_close_session(struct virtio_crypto_akcipher_ctx *ctx)
{
	struct scatterlist outhdr_sg, inhdr_sg, *sgs[2];
	struct virtio_crypto_destroy_session_req *destroy_session;
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	unsigned int num_out = 0, num_in = 0;
	int err;
	struct virtio_crypto_op_ctrl_req *ctrl;
	struct virtio_crypto_inhdr *ctrl_status;
	struct virtio_crypto_ctrl_request *vc_ctrl_req;

	if (!ctx->session_valid)
		return 0;

	vc_ctrl_req = kzalloc(sizeof(*vc_ctrl_req), GFP_KERNEL);
	if (!vc_ctrl_req)
		return -ENOMEM;

	ctrl_status = &vc_ctrl_req->ctrl_status;
	ctrl_status->status = VIRTIO_CRYPTO_ERR;
	ctrl = &vc_ctrl_req->ctrl;
	ctrl->header.opcode = cpu_to_le32(VIRTIO_CRYPTO_AKCIPHER_DESTROY_SESSION);
	ctrl->header.queue_id = 0;

	destroy_session = &ctrl->u.destroy_session;
	destroy_session->session_id = cpu_to_le64(ctx->session_id);

	sg_init_one(&outhdr_sg, ctrl, sizeof(*ctrl));
	sgs[num_out++] = &outhdr_sg;

	sg_init_one(&inhdr_sg, &ctrl_status->status, sizeof(ctrl_status->status));
	sgs[num_out + num_in++] = &inhdr_sg;

	err = virtio_crypto_ctrl_vq_request(vcrypto, sgs, num_out, num_in, vc_ctrl_req);
	if (err < 0)
		goto out;

	if (ctrl_status->status != VIRTIO_CRYPTO_OK) {
		pr_err("virtio_crypto: Close session failed status: %u, session_id: 0x%llx\n",
			ctrl_status->status, destroy_session->session_id);
		err = -EINVAL;
		goto out;
	}

	err = 0;
	ctx->session_valid = false;

out:
	kfree(vc_ctrl_req);

	return err;
}

static int __virtio_crypto_akcipher_do_req(struct virtio_crypto_akcipher_request *vc_akcipher_req,
		struct akcipher_request *req, struct data_queue *data_vq)
{
	struct virtio_crypto_akcipher_ctx *ctx = vc_akcipher_req->akcipher_ctx;
	struct virtio_crypto_request *vc_req = &vc_akcipher_req->base;
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	struct virtio_crypto_op_data_req *req_data = vc_req->req_data;
	struct scatterlist *sgs[4], outhdr_sg, inhdr_sg, srcdata_sg, dstdata_sg;
	void *src_buf = NULL, *dst_buf = NULL;
	unsigned int num_out = 0, num_in = 0;
	int node = dev_to_node(&vcrypto->vdev->dev);
	unsigned long flags;
	int ret = -ENOMEM;
	bool verify = vc_akcipher_req->opcode == VIRTIO_CRYPTO_AKCIPHER_VERIFY;
	unsigned int src_len = verify ? req->src_len + req->dst_len : req->src_len;

	/* out header */
	sg_init_one(&outhdr_sg, req_data, sizeof(*req_data));
	sgs[num_out++] = &outhdr_sg;

	/* src data */
	src_buf = kcalloc_node(src_len, 1, GFP_KERNEL, node);
	if (!src_buf)
		goto err;

	if (verify) {
		/* for verify operation, both src and dst data work as OUT direction */
		sg_copy_to_buffer(req->src, sg_nents(req->src), src_buf, src_len);
		sg_init_one(&srcdata_sg, src_buf, src_len);
		sgs[num_out++] = &srcdata_sg;
	} else {
		sg_copy_to_buffer(req->src, sg_nents(req->src), src_buf, src_len);
		sg_init_one(&srcdata_sg, src_buf, src_len);
		sgs[num_out++] = &srcdata_sg;

		/* dst data */
		dst_buf = kcalloc_node(req->dst_len, 1, GFP_KERNEL, node);
		if (!dst_buf)
			goto err;

		sg_init_one(&dstdata_sg, dst_buf, req->dst_len);
		sgs[num_out + num_in++] = &dstdata_sg;
	}

	vc_akcipher_req->src_buf = src_buf;
	vc_akcipher_req->dst_buf = dst_buf;

	/* in header */
	sg_init_one(&inhdr_sg, &vc_req->status, sizeof(vc_req->status));
	sgs[num_out + num_in++] = &inhdr_sg;

	spin_lock_irqsave(&data_vq->lock, flags);
	ret = virtqueue_add_sgs(data_vq->vq, sgs, num_out, num_in, vc_req, GFP_ATOMIC);
	virtqueue_kick(data_vq->vq);
	spin_unlock_irqrestore(&data_vq->lock, flags);
	if (ret)
		goto err;

	return 0;

err:
	kfree(src_buf);
	kfree(dst_buf);

	return -ENOMEM;
}

static int virtio_crypto_rsa_do_req(struct crypto_engine *engine, void *vreq)
{
	struct akcipher_request *req = container_of(vreq, struct akcipher_request, base);
	struct virtio_crypto_akcipher_request *vc_akcipher_req = akcipher_request_ctx(req);
	struct virtio_crypto_request *vc_req = &vc_akcipher_req->base;
	struct virtio_crypto_akcipher_ctx *ctx = vc_akcipher_req->akcipher_ctx;
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	struct data_queue *data_vq = vc_req->dataq;
	struct virtio_crypto_op_header *header;
	struct virtio_crypto_akcipher_data_req *akcipher_req;
	int ret;

	vc_req->sgs = NULL;
	vc_req->req_data = kzalloc_node(sizeof(*vc_req->req_data),
		GFP_KERNEL, dev_to_node(&vcrypto->vdev->dev));
	if (!vc_req->req_data)
		return -ENOMEM;

	/* build request header */
	header = &vc_req->req_data->header;
	header->opcode = cpu_to_le32(vc_akcipher_req->opcode);
	header->algo = cpu_to_le32(VIRTIO_CRYPTO_AKCIPHER_RSA);
	header->session_id = cpu_to_le64(ctx->session_id);

	/* build request akcipher data */
	akcipher_req = &vc_req->req_data->u.akcipher_req;
	akcipher_req->para.src_data_len = cpu_to_le32(req->src_len);
	akcipher_req->para.dst_data_len = cpu_to_le32(req->dst_len);

	ret = __virtio_crypto_akcipher_do_req(vc_akcipher_req, req, data_vq);
	if (ret < 0) {
		kfree_sensitive(vc_req->req_data);
		vc_req->req_data = NULL;
		return ret;
	}

	return 0;
}

static int virtio_crypto_rsa_req(struct akcipher_request *req, uint32_t opcode)
{
	struct crypto_akcipher *atfm = crypto_akcipher_reqtfm(req);
	struct virtio_crypto_akcipher_ctx *ctx = akcipher_tfm_ctx(atfm);
	struct virtio_crypto_akcipher_request *vc_akcipher_req = akcipher_request_ctx(req);
	struct virtio_crypto_request *vc_req = &vc_akcipher_req->base;
	struct virtio_crypto *vcrypto = ctx->vcrypto;
	/* Use the first data virtqueue as default */
	struct data_queue *data_vq = &vcrypto->data_vq[0];

	vc_req->dataq = data_vq;
	vc_req->alg_cb = virtio_crypto_dataq_akcipher_callback;
	vc_akcipher_req->akcipher_ctx = ctx;
	vc_akcipher_req->akcipher_req = req;
	vc_akcipher_req->opcode = opcode;

	return crypto_transfer_akcipher_request_to_engine(data_vq->engine, req);
}

static int virtio_crypto_rsa_encrypt(struct akcipher_request *req)
{
	return virtio_crypto_rsa_req(req, VIRTIO_CRYPTO_AKCIPHER_ENCRYPT);
}

static int virtio_crypto_rsa_decrypt(struct akcipher_request *req)
{
	return virtio_crypto_rsa_req(req, VIRTIO_CRYPTO_AKCIPHER_DECRYPT);
}

static int virtio_crypto_rsa_sign(struct akcipher_request *req)
{
	return virtio_crypto_rsa_req(req, VIRTIO_CRYPTO_AKCIPHER_SIGN);
}

static int virtio_crypto_rsa_verify(struct akcipher_request *req)
{
	return virtio_crypto_rsa_req(req, VIRTIO_CRYPTO_AKCIPHER_VERIFY);
}

static int virtio_crypto_rsa_set_key(struct crypto_akcipher *tfm,
				     const void *key,
				     unsigned int keylen,
				     bool private,
				     int padding_algo,
				     int hash_algo)
{
	struct virtio_crypto_akcipher_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct virtio_crypto_rsa_ctx *rsa_ctx = &ctx->rsa_ctx;
	struct virtio_crypto *vcrypto;
	struct virtio_crypto_ctrl_header header;
	struct virtio_crypto_akcipher_session_para para;
	struct rsa_key rsa_key = {0};
	int node = virtio_crypto_get_current_node();
	uint32_t keytype;
	int ret;

	/* mpi_free will test n, just free it. */
	mpi_free(rsa_ctx->n);
	rsa_ctx->n = NULL;

	if (private) {
		keytype = VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PRIVATE;
		ret = rsa_parse_priv_key(&rsa_key, key, keylen);
	} else {
		keytype = VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PUBLIC;
		ret = rsa_parse_pub_key(&rsa_key, key, keylen);
	}

	if (ret)
		return ret;

	rsa_ctx->n = mpi_read_raw_data(rsa_key.n, rsa_key.n_sz);
	if (!rsa_ctx->n)
		return -ENOMEM;

	if (!ctx->vcrypto) {
		vcrypto = virtcrypto_get_dev_node(node, VIRTIO_CRYPTO_SERVICE_AKCIPHER,
						VIRTIO_CRYPTO_AKCIPHER_RSA);
		if (!vcrypto) {
			pr_err("virtio_crypto: Could not find a virtio device in the system or unsupported algo\n");
			return -ENODEV;
		}

		ctx->vcrypto = vcrypto;
	} else {
		virtio_crypto_alg_akcipher_close_session(ctx);
	}

	/* set ctrl header */
	header.opcode =	cpu_to_le32(VIRTIO_CRYPTO_AKCIPHER_CREATE_SESSION);
	header.algo = cpu_to_le32(VIRTIO_CRYPTO_AKCIPHER_RSA);
	header.queue_id = 0;

	/* set RSA para */
	para.algo = cpu_to_le32(VIRTIO_CRYPTO_AKCIPHER_RSA);
	para.keytype = cpu_to_le32(keytype);
	para.keylen = cpu_to_le32(keylen);
	para.u.rsa.padding_algo = cpu_to_le32(padding_algo);
	para.u.rsa.hash_algo = cpu_to_le32(hash_algo);

	return virtio_crypto_alg_akcipher_init_session(ctx, &header, &para, key, keylen);
}

static int virtio_crypto_rsa_raw_set_priv_key(struct crypto_akcipher *tfm,
					      const void *key,
					      unsigned int keylen)
{
	return virtio_crypto_rsa_set_key(tfm, key, keylen, 1,
					 VIRTIO_CRYPTO_RSA_RAW_PADDING,
					 VIRTIO_CRYPTO_RSA_NO_HASH);
}


static int virtio_crypto_p1pad_rsa_sha1_set_priv_key(struct crypto_akcipher *tfm,
						     const void *key,
						     unsigned int keylen)
{
	return virtio_crypto_rsa_set_key(tfm, key, keylen, 1,
					 VIRTIO_CRYPTO_RSA_PKCS1_PADDING,
					 VIRTIO_CRYPTO_RSA_SHA1);
}

static int virtio_crypto_rsa_raw_set_pub_key(struct crypto_akcipher *tfm,
					     const void *key,
					     unsigned int keylen)
{
	return virtio_crypto_rsa_set_key(tfm, key, keylen, 0,
					 VIRTIO_CRYPTO_RSA_RAW_PADDING,
					 VIRTIO_CRYPTO_RSA_NO_HASH);
}

static int virtio_crypto_p1pad_rsa_sha1_set_pub_key(struct crypto_akcipher *tfm,
						    const void *key,
						    unsigned int keylen)
{
	return virtio_crypto_rsa_set_key(tfm, key, keylen, 0,
					 VIRTIO_CRYPTO_RSA_PKCS1_PADDING,
					 VIRTIO_CRYPTO_RSA_SHA1);
}

static unsigned int virtio_crypto_rsa_max_size(struct crypto_akcipher *tfm)
{
	struct virtio_crypto_akcipher_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct virtio_crypto_rsa_ctx *rsa_ctx = &ctx->rsa_ctx;

	return mpi_get_size(rsa_ctx->n);
}

static int virtio_crypto_rsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct virtio_crypto_akcipher_ctx *ctx = akcipher_tfm_ctx(tfm);

	ctx->tfm = tfm;
	ctx->enginectx.op.do_one_request = virtio_crypto_rsa_do_req;
	ctx->enginectx.op.prepare_request = NULL;
	ctx->enginectx.op.unprepare_request = NULL;

	return 0;
}

static void virtio_crypto_rsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct virtio_crypto_akcipher_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct virtio_crypto_rsa_ctx *rsa_ctx = &ctx->rsa_ctx;

	virtio_crypto_alg_akcipher_close_session(ctx);
	virtcrypto_dev_put(ctx->vcrypto);
	mpi_free(rsa_ctx->n);
	rsa_ctx->n = NULL;
}

static struct virtio_crypto_akcipher_algo virtio_crypto_akcipher_algs[] = {
	{
		.algonum = VIRTIO_CRYPTO_AKCIPHER_RSA,
		.service = VIRTIO_CRYPTO_SERVICE_AKCIPHER,
		.algo = {
			.encrypt = virtio_crypto_rsa_encrypt,
			.decrypt = virtio_crypto_rsa_decrypt,
			.set_pub_key = virtio_crypto_rsa_raw_set_pub_key,
			.set_priv_key = virtio_crypto_rsa_raw_set_priv_key,
			.max_size = virtio_crypto_rsa_max_size,
			.init = virtio_crypto_rsa_init_tfm,
			.exit = virtio_crypto_rsa_exit_tfm,
			.reqsize = sizeof(struct virtio_crypto_akcipher_request),
			.base = {
				.cra_name = "rsa",
				.cra_driver_name = "virtio-crypto-rsa",
				.cra_priority = 150,
				.cra_module = THIS_MODULE,
				.cra_ctxsize = sizeof(struct virtio_crypto_akcipher_ctx),
			},
		},
	},
	{
		.algonum = VIRTIO_CRYPTO_AKCIPHER_RSA,
		.service = VIRTIO_CRYPTO_SERVICE_AKCIPHER,
		.algo = {
			.encrypt = virtio_crypto_rsa_encrypt,
			.decrypt = virtio_crypto_rsa_decrypt,
			.sign = virtio_crypto_rsa_sign,
			.verify = virtio_crypto_rsa_verify,
			.set_pub_key = virtio_crypto_p1pad_rsa_sha1_set_pub_key,
			.set_priv_key = virtio_crypto_p1pad_rsa_sha1_set_priv_key,
			.max_size = virtio_crypto_rsa_max_size,
			.init = virtio_crypto_rsa_init_tfm,
			.exit = virtio_crypto_rsa_exit_tfm,
			.reqsize = sizeof(struct virtio_crypto_akcipher_request),
			.base = {
				.cra_name = "pkcs1pad(rsa,sha1)",
				.cra_driver_name = "virtio-pkcs1-rsa-with-sha1",
				.cra_priority = 150,
				.cra_module = THIS_MODULE,
				.cra_ctxsize = sizeof(struct virtio_crypto_akcipher_ctx),
			},
		},
	},
};

int virtio_crypto_akcipher_algs_register(struct virtio_crypto *vcrypto)
{
	int ret = 0;
	int i = 0;

	mutex_lock(&algs_lock);

	for (i = 0; i < ARRAY_SIZE(virtio_crypto_akcipher_algs); i++) {
		uint32_t service = virtio_crypto_akcipher_algs[i].service;
		uint32_t algonum = virtio_crypto_akcipher_algs[i].algonum;

		if (!virtcrypto_algo_is_supported(vcrypto, service, algonum))
			continue;

		if (virtio_crypto_akcipher_algs[i].active_devs == 0) {
			ret = crypto_register_akcipher(&virtio_crypto_akcipher_algs[i].algo);
			if (ret)
				goto unlock;
		}

		virtio_crypto_akcipher_algs[i].active_devs++;
		dev_info(&vcrypto->vdev->dev, "Registered akcipher algo %s\n",
			 virtio_crypto_akcipher_algs[i].algo.base.cra_name);
	}

unlock:
	mutex_unlock(&algs_lock);
	return ret;
}

void virtio_crypto_akcipher_algs_unregister(struct virtio_crypto *vcrypto)
{
	int i = 0;

	mutex_lock(&algs_lock);

	for (i = 0; i < ARRAY_SIZE(virtio_crypto_akcipher_algs); i++) {
		uint32_t service = virtio_crypto_akcipher_algs[i].service;
		uint32_t algonum = virtio_crypto_akcipher_algs[i].algonum;

		if (virtio_crypto_akcipher_algs[i].active_devs == 0 ||
		    !virtcrypto_algo_is_supported(vcrypto, service, algonum))
			continue;

		if (virtio_crypto_akcipher_algs[i].active_devs == 1)
			crypto_unregister_akcipher(&virtio_crypto_akcipher_algs[i].algo);

		virtio_crypto_akcipher_algs[i].active_devs--;
	}

	mutex_unlock(&algs_lock);
}
