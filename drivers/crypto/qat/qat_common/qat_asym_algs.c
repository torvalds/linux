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
#include <crypto/internal/rsa.h>
#include <crypto/internal/akcipher.h>
#include <crypto/akcipher.h>
#include <linux/dma-mapping.h>
#include <linux/fips.h>
#include "qat_rsakey-asn1.h"
#include "icp_qat_fw_pke.h"
#include "adf_accel_devices.h"
#include "adf_transport.h"
#include "adf_common_drv.h"
#include "qat_crypto.h"

struct qat_rsa_input_params {
	union {
		struct {
			dma_addr_t m;
			dma_addr_t e;
			dma_addr_t n;
		} enc;
		struct {
			dma_addr_t c;
			dma_addr_t d;
			dma_addr_t n;
		} dec;
		u64 in_tab[8];
	};
} __packed __aligned(64);

struct qat_rsa_output_params {
	union {
		struct {
			dma_addr_t c;
		} enc;
		struct {
			dma_addr_t m;
		} dec;
		u64 out_tab[8];
	};
} __packed __aligned(64);

struct qat_rsa_ctx {
	char *n;
	char *e;
	char *d;
	dma_addr_t dma_n;
	dma_addr_t dma_e;
	dma_addr_t dma_d;
	unsigned int key_sz;
	struct qat_crypto_instance *inst;
} __packed __aligned(64);

struct qat_rsa_request {
	struct qat_rsa_input_params in;
	struct qat_rsa_output_params out;
	dma_addr_t phy_in;
	dma_addr_t phy_out;
	char *src_align;
	struct icp_qat_fw_pke_request req;
	struct qat_rsa_ctx *ctx;
	int err;
} __aligned(64);

static void qat_rsa_cb(struct icp_qat_fw_pke_resp *resp)
{
	struct akcipher_request *areq = (void *)(__force long)resp->opaque;
	struct qat_rsa_request *req = PTR_ALIGN(akcipher_request_ctx(areq), 64);
	struct device *dev = &GET_DEV(req->ctx->inst->accel_dev);
	int err = ICP_QAT_FW_PKE_RESP_PKE_STAT_GET(
				resp->pke_resp_hdr.comn_resp_flags);
	char *ptr = areq->dst;

	err = (err == ICP_QAT_FW_COMN_STATUS_FLAG_OK) ? 0 : -EINVAL;

	if (req->src_align)
		dma_free_coherent(dev, req->ctx->key_sz, req->src_align,
				  req->in.enc.m);
	else
		dma_unmap_single(dev, req->in.enc.m, req->ctx->key_sz,
				 DMA_TO_DEVICE);

	dma_unmap_single(dev, req->out.enc.c, req->ctx->key_sz,
			 DMA_FROM_DEVICE);
	dma_unmap_single(dev, req->phy_in, sizeof(struct qat_rsa_input_params),
			 DMA_TO_DEVICE);
	dma_unmap_single(dev, req->phy_out,
			 sizeof(struct qat_rsa_output_params),
			 DMA_TO_DEVICE);

	areq->dst_len = req->ctx->key_sz;
	/* Need to set the corect length of the output */
	while (!(*ptr) && areq->dst_len) {
		areq->dst_len--;
		ptr++;
	}

	if (areq->dst_len != req->ctx->key_sz)
		memcpy(areq->dst, ptr, areq->dst_len);

	akcipher_request_complete(areq, err);
}

void qat_alg_asym_callback(void *_resp)
{
	struct icp_qat_fw_pke_resp *resp = _resp;

	qat_rsa_cb(resp);
}

#define PKE_RSA_EP_512 0x1c161b21
#define PKE_RSA_EP_1024 0x35111bf7
#define PKE_RSA_EP_1536 0x4d111cdc
#define PKE_RSA_EP_2048 0x6e111dba
#define PKE_RSA_EP_3072 0x7d111ea3
#define PKE_RSA_EP_4096 0xa5101f7e

static unsigned long qat_rsa_enc_fn_id(unsigned int len)
{
	unsigned int bitslen = len << 3;

	switch (bitslen) {
	case 512:
		return PKE_RSA_EP_512;
	case 1024:
		return PKE_RSA_EP_1024;
	case 1536:
		return PKE_RSA_EP_1536;
	case 2048:
		return PKE_RSA_EP_2048;
	case 3072:
		return PKE_RSA_EP_3072;
	case 4096:
		return PKE_RSA_EP_4096;
	default:
		return 0;
	};
}

#define PKE_RSA_DP1_512 0x1c161b3c
#define PKE_RSA_DP1_1024 0x35111c12
#define PKE_RSA_DP1_1536 0x4d111cf7
#define PKE_RSA_DP1_2048 0x6e111dda
#define PKE_RSA_DP1_3072 0x7d111ebe
#define PKE_RSA_DP1_4096 0xa5101f98

static unsigned long qat_rsa_dec_fn_id(unsigned int len)
{
	unsigned int bitslen = len << 3;

	switch (bitslen) {
	case 512:
		return PKE_RSA_DP1_512;
	case 1024:
		return PKE_RSA_DP1_1024;
	case 1536:
		return PKE_RSA_DP1_1536;
	case 2048:
		return PKE_RSA_DP1_2048;
	case 3072:
		return PKE_RSA_DP1_3072;
	case 4096:
		return PKE_RSA_DP1_4096;
	default:
		return 0;
	};
}

static int qat_rsa_enc(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct qat_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev = &GET_DEV(inst->accel_dev);
	struct qat_rsa_request *qat_req =
			PTR_ALIGN(akcipher_request_ctx(req), 64);
	struct icp_qat_fw_pke_request *msg = &qat_req->req;
	int ret, ctr = 0;

	if (unlikely(!ctx->n || !ctx->e))
		return -EINVAL;

	if (req->dst_len < ctx->key_sz) {
		req->dst_len = ctx->key_sz;
		return -EOVERFLOW;
	}
	memset(msg, '\0', sizeof(*msg));
	ICP_QAT_FW_PKE_HDR_VALID_FLAG_SET(msg->pke_hdr,
					  ICP_QAT_FW_COMN_REQ_FLAG_SET);
	msg->pke_hdr.cd_pars.func_id = qat_rsa_enc_fn_id(ctx->key_sz);
	if (unlikely(!msg->pke_hdr.cd_pars.func_id))
		return -EINVAL;

	qat_req->ctx = ctx;
	msg->pke_hdr.service_type = ICP_QAT_FW_COMN_REQ_CPM_FW_PKE;
	msg->pke_hdr.comn_req_flags =
		ICP_QAT_FW_COMN_FLAGS_BUILD(QAT_COMN_PTR_TYPE_FLAT,
					    QAT_COMN_CD_FLD_TYPE_64BIT_ADR);

	qat_req->in.enc.e = ctx->dma_e;
	qat_req->in.enc.n = ctx->dma_n;
	ret = -ENOMEM;

	/*
	 * src can be of any size in valid range, but HW expects it to be the
	 * same as modulo n so in case it is different we need to allocate a
	 * new buf and copy src data.
	 * In other case we just need to map the user provided buffer.
	 */
	if (req->src_len < ctx->key_sz) {
		int shift = ctx->key_sz - req->src_len;

		qat_req->src_align = dma_zalloc_coherent(dev, ctx->key_sz,
							 &qat_req->in.enc.m,
							 GFP_KERNEL);
		if (unlikely(!qat_req->src_align))
			return ret;

		memcpy(qat_req->src_align + shift, req->src, req->src_len);
	} else {
		qat_req->src_align = NULL;
		qat_req->in.enc.m = dma_map_single(dev, req->src, req->src_len,
					   DMA_TO_DEVICE);
	}
	qat_req->in.in_tab[3] = 0;
	qat_req->out.enc.c = dma_map_single(dev, req->dst, req->dst_len,
					    DMA_FROM_DEVICE);
	qat_req->out.out_tab[1] = 0;
	qat_req->phy_in = dma_map_single(dev, &qat_req->in.enc.m,
					 sizeof(struct qat_rsa_input_params),
					 DMA_TO_DEVICE);
	qat_req->phy_out = dma_map_single(dev, &qat_req->out.enc.c,
					  sizeof(struct qat_rsa_output_params),
					    DMA_TO_DEVICE);

	if (unlikely((!qat_req->src_align &&
		      dma_mapping_error(dev, qat_req->in.enc.m)) ||
		     dma_mapping_error(dev, qat_req->out.enc.c) ||
		     dma_mapping_error(dev, qat_req->phy_in) ||
		     dma_mapping_error(dev, qat_req->phy_out)))
		goto unmap;

	msg->pke_mid.src_data_addr = qat_req->phy_in;
	msg->pke_mid.dest_data_addr = qat_req->phy_out;
	msg->pke_mid.opaque = (uint64_t)(__force long)req;
	msg->input_param_count = 3;
	msg->output_param_count = 1;
	do {
		ret = adf_send_message(ctx->inst->pke_tx, (uint32_t *)msg);
	} while (ret == -EBUSY && ctr++ < 100);

	if (!ret)
		return -EINPROGRESS;
unmap:
	if (qat_req->src_align)
		dma_free_coherent(dev, ctx->key_sz, qat_req->src_align,
				  qat_req->in.enc.m);
	else
		if (!dma_mapping_error(dev, qat_req->in.enc.m))
			dma_unmap_single(dev, qat_req->in.enc.m, ctx->key_sz,
					 DMA_TO_DEVICE);
	if (!dma_mapping_error(dev, qat_req->out.enc.c))
		dma_unmap_single(dev, qat_req->out.enc.c, ctx->key_sz,
				 DMA_FROM_DEVICE);
	if (!dma_mapping_error(dev, qat_req->phy_in))
		dma_unmap_single(dev, qat_req->phy_in,
				 sizeof(struct qat_rsa_input_params),
				 DMA_TO_DEVICE);
	if (!dma_mapping_error(dev, qat_req->phy_out))
		dma_unmap_single(dev, qat_req->phy_out,
				 sizeof(struct qat_rsa_output_params),
				 DMA_TO_DEVICE);
	return ret;
}

static int qat_rsa_dec(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct qat_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev = &GET_DEV(inst->accel_dev);
	struct qat_rsa_request *qat_req =
			PTR_ALIGN(akcipher_request_ctx(req), 64);
	struct icp_qat_fw_pke_request *msg = &qat_req->req;
	int ret, ctr = 0;

	if (unlikely(!ctx->n || !ctx->d))
		return -EINVAL;

	if (req->dst_len < ctx->key_sz) {
		req->dst_len = ctx->key_sz;
		return -EOVERFLOW;
	}
	memset(msg, '\0', sizeof(*msg));
	ICP_QAT_FW_PKE_HDR_VALID_FLAG_SET(msg->pke_hdr,
					  ICP_QAT_FW_COMN_REQ_FLAG_SET);
	msg->pke_hdr.cd_pars.func_id = qat_rsa_dec_fn_id(ctx->key_sz);
	if (unlikely(!msg->pke_hdr.cd_pars.func_id))
		return -EINVAL;

	qat_req->ctx = ctx;
	msg->pke_hdr.service_type = ICP_QAT_FW_COMN_REQ_CPM_FW_PKE;
	msg->pke_hdr.comn_req_flags =
		ICP_QAT_FW_COMN_FLAGS_BUILD(QAT_COMN_PTR_TYPE_FLAT,
					    QAT_COMN_CD_FLD_TYPE_64BIT_ADR);

	qat_req->in.dec.d = ctx->dma_d;
	qat_req->in.dec.n = ctx->dma_n;
	ret = -ENOMEM;

	/*
	 * src can be of any size in valid range, but HW expects it to be the
	 * same as modulo n so in case it is different we need to allocate a
	 * new buf and copy src data.
	 * In other case we just need to map the user provided buffer.
	 */
	if (req->src_len < ctx->key_sz) {
		int shift = ctx->key_sz - req->src_len;

		qat_req->src_align = dma_zalloc_coherent(dev, ctx->key_sz,
							 &qat_req->in.dec.c,
							 GFP_KERNEL);
		if (unlikely(!qat_req->src_align))
			return ret;

		memcpy(qat_req->src_align + shift, req->src, req->src_len);
	} else {
		qat_req->src_align = NULL;
		qat_req->in.dec.c = dma_map_single(dev, req->src, req->src_len,
						   DMA_TO_DEVICE);
	}
	qat_req->in.in_tab[3] = 0;
	qat_req->out.dec.m = dma_map_single(dev, req->dst, req->dst_len,
					    DMA_FROM_DEVICE);
	qat_req->out.out_tab[1] = 0;
	qat_req->phy_in = dma_map_single(dev, &qat_req->in.dec.c,
					 sizeof(struct qat_rsa_input_params),
					 DMA_TO_DEVICE);
	qat_req->phy_out = dma_map_single(dev, &qat_req->out.dec.m,
					  sizeof(struct qat_rsa_output_params),
					    DMA_TO_DEVICE);

	if (unlikely((!qat_req->src_align &&
		      dma_mapping_error(dev, qat_req->in.dec.c)) ||
		     dma_mapping_error(dev, qat_req->out.dec.m) ||
		     dma_mapping_error(dev, qat_req->phy_in) ||
		     dma_mapping_error(dev, qat_req->phy_out)))
		goto unmap;

	msg->pke_mid.src_data_addr = qat_req->phy_in;
	msg->pke_mid.dest_data_addr = qat_req->phy_out;
	msg->pke_mid.opaque = (uint64_t)(__force long)req;
	msg->input_param_count = 3;
	msg->output_param_count = 1;
	do {
		ret = adf_send_message(ctx->inst->pke_tx, (uint32_t *)msg);
	} while (ret == -EBUSY && ctr++ < 100);

	if (!ret)
		return -EINPROGRESS;
unmap:
	if (qat_req->src_align)
		dma_free_coherent(dev, ctx->key_sz, qat_req->src_align,
				  qat_req->in.dec.c);
	else
		if (!dma_mapping_error(dev, qat_req->in.dec.c))
			dma_unmap_single(dev, qat_req->in.dec.c, ctx->key_sz,
					 DMA_TO_DEVICE);
	if (!dma_mapping_error(dev, qat_req->out.dec.m))
		dma_unmap_single(dev, qat_req->out.dec.m, ctx->key_sz,
				 DMA_FROM_DEVICE);
	if (!dma_mapping_error(dev, qat_req->phy_in))
		dma_unmap_single(dev, qat_req->phy_in,
				 sizeof(struct qat_rsa_input_params),
				 DMA_TO_DEVICE);
	if (!dma_mapping_error(dev, qat_req->phy_out))
		dma_unmap_single(dev, qat_req->phy_out,
				 sizeof(struct qat_rsa_output_params),
				 DMA_TO_DEVICE);
	return ret;
}

int qat_rsa_get_n(void *context, size_t hdrlen, unsigned char tag,
		  const void *value, size_t vlen)
{
	struct qat_rsa_ctx *ctx = context;
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev = &GET_DEV(inst->accel_dev);
	const char *ptr = value;
	int ret;

	while (!*ptr && vlen) {
		ptr++;
		vlen--;
	}

	ctx->key_sz = vlen;
	ret = -EINVAL;
	/* In FIPS mode only allow key size 2K & 3K */
	if (fips_enabled && (ctx->key_sz != 256 && ctx->key_sz != 384)) {
		pr_err("QAT: RSA: key size not allowed in FIPS mode\n");
		goto err;
	}
	/* invalid key size provided */
	if (!qat_rsa_enc_fn_id(ctx->key_sz))
		goto err;

	ret = -ENOMEM;
	ctx->n = dma_zalloc_coherent(dev, ctx->key_sz, &ctx->dma_n, GFP_KERNEL);
	if (!ctx->n)
		goto err;

	memcpy(ctx->n, ptr, ctx->key_sz);
	return 0;
err:
	ctx->key_sz = 0;
	ctx->n = NULL;
	return ret;
}

int qat_rsa_get_e(void *context, size_t hdrlen, unsigned char tag,
		  const void *value, size_t vlen)
{
	struct qat_rsa_ctx *ctx = context;
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev = &GET_DEV(inst->accel_dev);
	const char *ptr = value;

	while (!*ptr && vlen) {
		ptr++;
		vlen--;
	}

	if (!ctx->key_sz || !vlen || vlen > ctx->key_sz) {
		ctx->e = NULL;
		return -EINVAL;
	}

	ctx->e = dma_zalloc_coherent(dev, ctx->key_sz, &ctx->dma_e, GFP_KERNEL);
	if (!ctx->e) {
		ctx->e = NULL;
		return -ENOMEM;
	}
	memcpy(ctx->e + (ctx->key_sz - vlen), ptr, vlen);
	return 0;
}

int qat_rsa_get_d(void *context, size_t hdrlen, unsigned char tag,
		  const void *value, size_t vlen)
{
	struct qat_rsa_ctx *ctx = context;
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev = &GET_DEV(inst->accel_dev);
	const char *ptr = value;
	int ret;

	while (!*ptr && vlen) {
		ptr++;
		vlen--;
	}

	ret = -EINVAL;
	if (!ctx->key_sz || !vlen || vlen > ctx->key_sz)
		goto err;

	/* In FIPS mode only allow key size 2K & 3K */
	if (fips_enabled && (vlen != 256 && vlen != 384)) {
		pr_err("QAT: RSA: key size not allowed in FIPS mode\n");
		goto err;
	}

	ret = -ENOMEM;
	ctx->d = dma_zalloc_coherent(dev, ctx->key_sz, &ctx->dma_d, GFP_KERNEL);
	if (!ctx->n)
		goto err;

	memcpy(ctx->d + (ctx->key_sz - vlen), ptr, vlen);
	return 0;
err:
	ctx->d = NULL;
	return ret;
}

static int qat_rsa_setkey(struct crypto_akcipher *tfm, const void *key,
			  unsigned int keylen)
{
	struct qat_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct device *dev = &GET_DEV(ctx->inst->accel_dev);
	int ret;

	/* Free the old key if any */
	if (ctx->n)
		dma_free_coherent(dev, ctx->key_sz, ctx->n, ctx->dma_n);
	if (ctx->e)
		dma_free_coherent(dev, ctx->key_sz, ctx->e, ctx->dma_e);
	if (ctx->d) {
		memset(ctx->d, '\0', ctx->key_sz);
		dma_free_coherent(dev, ctx->key_sz, ctx->d, ctx->dma_d);
	}

	ctx->n = NULL;
	ctx->e = NULL;
	ctx->d = NULL;
	ret = asn1_ber_decoder(&qat_rsakey_decoder, ctx, key, keylen);
	if (ret < 0)
		goto free;

	if (!ctx->n || !ctx->e) {
		/* invalid key provided */
		ret = -EINVAL;
		goto free;
	}

	return 0;
free:
	if (ctx->d) {
		memset(ctx->d, '\0', ctx->key_sz);
		dma_free_coherent(dev, ctx->key_sz, ctx->d, ctx->dma_d);
		ctx->d = NULL;
	}
	if (ctx->e) {
		dma_free_coherent(dev, ctx->key_sz, ctx->e, ctx->dma_e);
		ctx->e = NULL;
	}
	if (ctx->n) {
		dma_free_coherent(dev, ctx->key_sz, ctx->n, ctx->dma_n);
		ctx->n = NULL;
		ctx->key_sz = 0;
	}
	return ret;
}

static int qat_rsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct qat_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct qat_crypto_instance *inst =
			qat_crypto_get_instance_node(get_current_node());

	if (!inst)
		return -EINVAL;

	ctx->key_sz = 0;
	ctx->inst = inst;
	return 0;
}

static void qat_rsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct qat_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct device *dev = &GET_DEV(ctx->inst->accel_dev);

	if (ctx->n)
		dma_free_coherent(dev, ctx->key_sz, ctx->n, ctx->dma_n);
	if (ctx->e)
		dma_free_coherent(dev, ctx->key_sz, ctx->e, ctx->dma_e);
	if (ctx->d) {
		memset(ctx->d, '\0', ctx->key_sz);
		dma_free_coherent(dev, ctx->key_sz, ctx->d, ctx->dma_d);
	}
	qat_crypto_put_instance(ctx->inst);
	ctx->n = NULL;
	ctx->d = NULL;
	ctx->d = NULL;
}

static struct akcipher_alg rsa = {
	.encrypt = qat_rsa_enc,
	.decrypt = qat_rsa_dec,
	.sign = qat_rsa_dec,
	.verify = qat_rsa_enc,
	.setkey = qat_rsa_setkey,
	.init = qat_rsa_init_tfm,
	.exit = qat_rsa_exit_tfm,
	.reqsize = sizeof(struct qat_rsa_request) + 64,
	.base = {
		.cra_name = "rsa",
		.cra_driver_name = "qat-rsa",
		.cra_priority = 1000,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct qat_rsa_ctx),
	},
};

int qat_asym_algs_register(void)
{
	rsa.base.cra_flags = 0;
	return crypto_register_akcipher(&rsa);
}

void qat_asym_algs_unregister(void)
{
	crypto_unregister_akcipher(&rsa);
}
