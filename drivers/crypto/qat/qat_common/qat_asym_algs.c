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
#include <crypto/kpp.h>
#include <crypto/internal/kpp.h>
#include <crypto/dh.h>
#include <linux/dma-mapping.h>
#include <linux/fips.h>
#include <crypto/scatterwalk.h>
#include "icp_qat_fw_pke.h"
#include "adf_accel_devices.h"
#include "adf_transport.h"
#include "adf_common_drv.h"
#include "qat_crypto.h"

static DEFINE_MUTEX(algs_lock);
static unsigned int active_devs;

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
		struct {
			dma_addr_t c;
			dma_addr_t p;
			dma_addr_t q;
			dma_addr_t dp;
			dma_addr_t dq;
			dma_addr_t qinv;
		} dec_crt;
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
	char *p;
	char *q;
	char *dp;
	char *dq;
	char *qinv;
	dma_addr_t dma_n;
	dma_addr_t dma_e;
	dma_addr_t dma_d;
	dma_addr_t dma_p;
	dma_addr_t dma_q;
	dma_addr_t dma_dp;
	dma_addr_t dma_dq;
	dma_addr_t dma_qinv;
	unsigned int key_sz;
	bool crt_mode;
	struct qat_crypto_instance *inst;
} __packed __aligned(64);

struct qat_dh_input_params {
	union {
		struct {
			dma_addr_t b;
			dma_addr_t xa;
			dma_addr_t p;
		} in;
		struct {
			dma_addr_t xa;
			dma_addr_t p;
		} in_g2;
		u64 in_tab[8];
	};
} __packed __aligned(64);

struct qat_dh_output_params {
	union {
		dma_addr_t r;
		u64 out_tab[8];
	};
} __packed __aligned(64);

struct qat_dh_ctx {
	char *g;
	char *xa;
	char *p;
	dma_addr_t dma_g;
	dma_addr_t dma_xa;
	dma_addr_t dma_p;
	unsigned int p_size;
	bool g2;
	struct qat_crypto_instance *inst;
} __packed __aligned(64);

struct qat_asym_request {
	union {
		struct qat_rsa_input_params rsa;
		struct qat_dh_input_params dh;
	} in;
	union {
		struct qat_rsa_output_params rsa;
		struct qat_dh_output_params dh;
	} out;
	dma_addr_t phy_in;
	dma_addr_t phy_out;
	char *src_align;
	char *dst_align;
	struct icp_qat_fw_pke_request req;
	union {
		struct qat_rsa_ctx *rsa;
		struct qat_dh_ctx *dh;
	} ctx;
	union {
		struct akcipher_request *rsa;
		struct kpp_request *dh;
	} areq;
	int err;
	void (*cb)(struct icp_qat_fw_pke_resp *resp);
} __aligned(64);

static void qat_dh_cb(struct icp_qat_fw_pke_resp *resp)
{
	struct qat_asym_request *req = (void *)(__force long)resp->opaque;
	struct kpp_request *areq = req->areq.dh;
	struct device *dev = &GET_DEV(req->ctx.dh->inst->accel_dev);
	int err = ICP_QAT_FW_PKE_RESP_PKE_STAT_GET(
				resp->pke_resp_hdr.comn_resp_flags);

	err = (err == ICP_QAT_FW_COMN_STATUS_FLAG_OK) ? 0 : -EINVAL;

	if (areq->src) {
		if (req->src_align)
			dma_free_coherent(dev, req->ctx.dh->p_size,
					  req->src_align, req->in.dh.in.b);
		else
			dma_unmap_single(dev, req->in.dh.in.b,
					 req->ctx.dh->p_size, DMA_TO_DEVICE);
	}

	areq->dst_len = req->ctx.dh->p_size;
	if (req->dst_align) {
		scatterwalk_map_and_copy(req->dst_align, areq->dst, 0,
					 areq->dst_len, 1);

		dma_free_coherent(dev, req->ctx.dh->p_size, req->dst_align,
				  req->out.dh.r);
	} else {
		dma_unmap_single(dev, req->out.dh.r, req->ctx.dh->p_size,
				 DMA_FROM_DEVICE);
	}

	dma_unmap_single(dev, req->phy_in, sizeof(struct qat_dh_input_params),
			 DMA_TO_DEVICE);
	dma_unmap_single(dev, req->phy_out,
			 sizeof(struct qat_dh_output_params),
			 DMA_TO_DEVICE);

	kpp_request_complete(areq, err);
}

#define PKE_DH_1536 0x390c1a49
#define PKE_DH_G2_1536 0x2e0b1a3e
#define PKE_DH_2048 0x4d0c1a60
#define PKE_DH_G2_2048 0x3e0b1a55
#define PKE_DH_3072 0x510c1a77
#define PKE_DH_G2_3072 0x3a0b1a6c
#define PKE_DH_4096 0x690c1a8e
#define PKE_DH_G2_4096 0x4a0b1a83

static unsigned long qat_dh_fn_id(unsigned int len, bool g2)
{
	unsigned int bitslen = len << 3;

	switch (bitslen) {
	case 1536:
		return g2 ? PKE_DH_G2_1536 : PKE_DH_1536;
	case 2048:
		return g2 ? PKE_DH_G2_2048 : PKE_DH_2048;
	case 3072:
		return g2 ? PKE_DH_G2_3072 : PKE_DH_3072;
	case 4096:
		return g2 ? PKE_DH_G2_4096 : PKE_DH_4096;
	default:
		return 0;
	};
}

static inline struct qat_dh_ctx *qat_dh_get_params(struct crypto_kpp *tfm)
{
	return kpp_tfm_ctx(tfm);
}

static int qat_dh_compute_value(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct qat_dh_ctx *ctx = kpp_tfm_ctx(tfm);
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev = &GET_DEV(inst->accel_dev);
	struct qat_asym_request *qat_req =
			PTR_ALIGN(kpp_request_ctx(req), 64);
	struct icp_qat_fw_pke_request *msg = &qat_req->req;
	int ret, ctr = 0;
	int n_input_params = 0;

	if (unlikely(!ctx->xa))
		return -EINVAL;

	if (req->dst_len < ctx->p_size) {
		req->dst_len = ctx->p_size;
		return -EOVERFLOW;
	}
	memset(msg, '\0', sizeof(*msg));
	ICP_QAT_FW_PKE_HDR_VALID_FLAG_SET(msg->pke_hdr,
					  ICP_QAT_FW_COMN_REQ_FLAG_SET);

	msg->pke_hdr.cd_pars.func_id = qat_dh_fn_id(ctx->p_size,
						    !req->src && ctx->g2);
	if (unlikely(!msg->pke_hdr.cd_pars.func_id))
		return -EINVAL;

	qat_req->cb = qat_dh_cb;
	qat_req->ctx.dh = ctx;
	qat_req->areq.dh = req;
	msg->pke_hdr.service_type = ICP_QAT_FW_COMN_REQ_CPM_FW_PKE;
	msg->pke_hdr.comn_req_flags =
		ICP_QAT_FW_COMN_FLAGS_BUILD(QAT_COMN_PTR_TYPE_FLAT,
					    QAT_COMN_CD_FLD_TYPE_64BIT_ADR);

	/*
	 * If no source is provided use g as base
	 */
	if (req->src) {
		qat_req->in.dh.in.xa = ctx->dma_xa;
		qat_req->in.dh.in.p = ctx->dma_p;
		n_input_params = 3;
	} else {
		if (ctx->g2) {
			qat_req->in.dh.in_g2.xa = ctx->dma_xa;
			qat_req->in.dh.in_g2.p = ctx->dma_p;
			n_input_params = 2;
		} else {
			qat_req->in.dh.in.b = ctx->dma_g;
			qat_req->in.dh.in.xa = ctx->dma_xa;
			qat_req->in.dh.in.p = ctx->dma_p;
			n_input_params = 3;
		}
	}

	ret = -ENOMEM;
	if (req->src) {
		/*
		 * src can be of any size in valid range, but HW expects it to
		 * be the same as modulo p so in case it is different we need
		 * to allocate a new buf and copy src data.
		 * In other case we just need to map the user provided buffer.
		 * Also need to make sure that it is in contiguous buffer.
		 */
		if (sg_is_last(req->src) && req->src_len == ctx->p_size) {
			qat_req->src_align = NULL;
			qat_req->in.dh.in.b = dma_map_single(dev,
							     sg_virt(req->src),
							     req->src_len,
							     DMA_TO_DEVICE);
			if (unlikely(dma_mapping_error(dev,
						       qat_req->in.dh.in.b)))
				return ret;

		} else {
			int shift = ctx->p_size - req->src_len;

			qat_req->src_align = dma_alloc_coherent(dev,
								ctx->p_size,
								&qat_req->in.dh.in.b,
								GFP_KERNEL);
			if (unlikely(!qat_req->src_align))
				return ret;

			scatterwalk_map_and_copy(qat_req->src_align + shift,
						 req->src, 0, req->src_len, 0);
		}
	}
	/*
	 * dst can be of any size in valid range, but HW expects it to be the
	 * same as modulo m so in case it is different we need to allocate a
	 * new buf and copy src data.
	 * In other case we just need to map the user provided buffer.
	 * Also need to make sure that it is in contiguous buffer.
	 */
	if (sg_is_last(req->dst) && req->dst_len == ctx->p_size) {
		qat_req->dst_align = NULL;
		qat_req->out.dh.r = dma_map_single(dev, sg_virt(req->dst),
						   req->dst_len,
						   DMA_FROM_DEVICE);

		if (unlikely(dma_mapping_error(dev, qat_req->out.dh.r)))
			goto unmap_src;

	} else {
		qat_req->dst_align = dma_alloc_coherent(dev, ctx->p_size,
							&qat_req->out.dh.r,
							GFP_KERNEL);
		if (unlikely(!qat_req->dst_align))
			goto unmap_src;
	}

	qat_req->in.dh.in_tab[n_input_params] = 0;
	qat_req->out.dh.out_tab[1] = 0;
	/* Mapping in.in.b or in.in_g2.xa is the same */
	qat_req->phy_in = dma_map_single(dev, &qat_req->in.dh.in.b,
					 sizeof(struct qat_dh_input_params),
					 DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, qat_req->phy_in)))
		goto unmap_dst;

	qat_req->phy_out = dma_map_single(dev, &qat_req->out.dh.r,
					  sizeof(struct qat_dh_output_params),
					  DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, qat_req->phy_out)))
		goto unmap_in_params;

	msg->pke_mid.src_data_addr = qat_req->phy_in;
	msg->pke_mid.dest_data_addr = qat_req->phy_out;
	msg->pke_mid.opaque = (uint64_t)(__force long)qat_req;
	msg->input_param_count = n_input_params;
	msg->output_param_count = 1;

	do {
		ret = adf_send_message(ctx->inst->pke_tx, (uint32_t *)msg);
	} while (ret == -EBUSY && ctr++ < 100);

	if (!ret)
		return -EINPROGRESS;

	if (!dma_mapping_error(dev, qat_req->phy_out))
		dma_unmap_single(dev, qat_req->phy_out,
				 sizeof(struct qat_dh_output_params),
				 DMA_TO_DEVICE);
unmap_in_params:
	if (!dma_mapping_error(dev, qat_req->phy_in))
		dma_unmap_single(dev, qat_req->phy_in,
				 sizeof(struct qat_dh_input_params),
				 DMA_TO_DEVICE);
unmap_dst:
	if (qat_req->dst_align)
		dma_free_coherent(dev, ctx->p_size, qat_req->dst_align,
				  qat_req->out.dh.r);
	else
		if (!dma_mapping_error(dev, qat_req->out.dh.r))
			dma_unmap_single(dev, qat_req->out.dh.r, ctx->p_size,
					 DMA_FROM_DEVICE);
unmap_src:
	if (req->src) {
		if (qat_req->src_align)
			dma_free_coherent(dev, ctx->p_size, qat_req->src_align,
					  qat_req->in.dh.in.b);
		else
			if (!dma_mapping_error(dev, qat_req->in.dh.in.b))
				dma_unmap_single(dev, qat_req->in.dh.in.b,
						 ctx->p_size,
						 DMA_TO_DEVICE);
	}
	return ret;
}

static int qat_dh_check_params_length(unsigned int p_len)
{
	switch (p_len) {
	case 1536:
	case 2048:
	case 3072:
	case 4096:
		return 0;
	}
	return -EINVAL;
}

static int qat_dh_set_params(struct qat_dh_ctx *ctx, struct dh *params)
{
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev = &GET_DEV(inst->accel_dev);

	if (qat_dh_check_params_length(params->p_size << 3))
		return -EINVAL;

	ctx->p_size = params->p_size;
	ctx->p = dma_alloc_coherent(dev, ctx->p_size, &ctx->dma_p, GFP_KERNEL);
	if (!ctx->p)
		return -ENOMEM;
	memcpy(ctx->p, params->p, ctx->p_size);

	/* If g equals 2 don't copy it */
	if (params->g_size == 1 && *(char *)params->g == 0x02) {
		ctx->g2 = true;
		return 0;
	}

	ctx->g = dma_alloc_coherent(dev, ctx->p_size, &ctx->dma_g, GFP_KERNEL);
	if (!ctx->g)
		return -ENOMEM;
	memcpy(ctx->g + (ctx->p_size - params->g_size), params->g,
	       params->g_size);

	return 0;
}

static void qat_dh_clear_ctx(struct device *dev, struct qat_dh_ctx *ctx)
{
	if (ctx->g) {
		dma_free_coherent(dev, ctx->p_size, ctx->g, ctx->dma_g);
		ctx->g = NULL;
	}
	if (ctx->xa) {
		dma_free_coherent(dev, ctx->p_size, ctx->xa, ctx->dma_xa);
		ctx->xa = NULL;
	}
	if (ctx->p) {
		dma_free_coherent(dev, ctx->p_size, ctx->p, ctx->dma_p);
		ctx->p = NULL;
	}
	ctx->p_size = 0;
	ctx->g2 = false;
}

static int qat_dh_set_secret(struct crypto_kpp *tfm, const void *buf,
			     unsigned int len)
{
	struct qat_dh_ctx *ctx = kpp_tfm_ctx(tfm);
	struct device *dev = &GET_DEV(ctx->inst->accel_dev);
	struct dh params;
	int ret;

	if (crypto_dh_decode_key(buf, len, &params) < 0)
		return -EINVAL;

	/* Free old secret if any */
	qat_dh_clear_ctx(dev, ctx);

	ret = qat_dh_set_params(ctx, &params);
	if (ret < 0)
		goto err_clear_ctx;

	ctx->xa = dma_alloc_coherent(dev, ctx->p_size, &ctx->dma_xa,
				     GFP_KERNEL);
	if (!ctx->xa) {
		ret = -ENOMEM;
		goto err_clear_ctx;
	}
	memcpy(ctx->xa + (ctx->p_size - params.key_size), params.key,
	       params.key_size);

	return 0;

err_clear_ctx:
	qat_dh_clear_ctx(dev, ctx);
	return ret;
}

static unsigned int qat_dh_max_size(struct crypto_kpp *tfm)
{
	struct qat_dh_ctx *ctx = kpp_tfm_ctx(tfm);

	return ctx->p_size;
}

static int qat_dh_init_tfm(struct crypto_kpp *tfm)
{
	struct qat_dh_ctx *ctx = kpp_tfm_ctx(tfm);
	struct qat_crypto_instance *inst =
			qat_crypto_get_instance_node(get_current_node());

	if (!inst)
		return -EINVAL;

	ctx->p_size = 0;
	ctx->g2 = false;
	ctx->inst = inst;
	return 0;
}

static void qat_dh_exit_tfm(struct crypto_kpp *tfm)
{
	struct qat_dh_ctx *ctx = kpp_tfm_ctx(tfm);
	struct device *dev = &GET_DEV(ctx->inst->accel_dev);

	qat_dh_clear_ctx(dev, ctx);
	qat_crypto_put_instance(ctx->inst);
}

static void qat_rsa_cb(struct icp_qat_fw_pke_resp *resp)
{
	struct qat_asym_request *req = (void *)(__force long)resp->opaque;
	struct akcipher_request *areq = req->areq.rsa;
	struct device *dev = &GET_DEV(req->ctx.rsa->inst->accel_dev);
	int err = ICP_QAT_FW_PKE_RESP_PKE_STAT_GET(
				resp->pke_resp_hdr.comn_resp_flags);

	err = (err == ICP_QAT_FW_COMN_STATUS_FLAG_OK) ? 0 : -EINVAL;

	if (req->src_align)
		dma_free_coherent(dev, req->ctx.rsa->key_sz, req->src_align,
				  req->in.rsa.enc.m);
	else
		dma_unmap_single(dev, req->in.rsa.enc.m, req->ctx.rsa->key_sz,
				 DMA_TO_DEVICE);

	areq->dst_len = req->ctx.rsa->key_sz;
	if (req->dst_align) {
		scatterwalk_map_and_copy(req->dst_align, areq->dst, 0,
					 areq->dst_len, 1);

		dma_free_coherent(dev, req->ctx.rsa->key_sz, req->dst_align,
				  req->out.rsa.enc.c);
	} else {
		dma_unmap_single(dev, req->out.rsa.enc.c, req->ctx.rsa->key_sz,
				 DMA_FROM_DEVICE);
	}

	dma_unmap_single(dev, req->phy_in, sizeof(struct qat_rsa_input_params),
			 DMA_TO_DEVICE);
	dma_unmap_single(dev, req->phy_out,
			 sizeof(struct qat_rsa_output_params),
			 DMA_TO_DEVICE);

	akcipher_request_complete(areq, err);
}

void qat_alg_asym_callback(void *_resp)
{
	struct icp_qat_fw_pke_resp *resp = _resp;
	struct qat_asym_request *areq = (void *)(__force long)resp->opaque;

	areq->cb(resp);
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

#define PKE_RSA_DP2_512 0x1c131b57
#define PKE_RSA_DP2_1024 0x26131c2d
#define PKE_RSA_DP2_1536 0x45111d12
#define PKE_RSA_DP2_2048 0x59121dfa
#define PKE_RSA_DP2_3072 0x81121ed9
#define PKE_RSA_DP2_4096 0xb1111fb2

static unsigned long qat_rsa_dec_fn_id_crt(unsigned int len)
{
	unsigned int bitslen = len << 3;

	switch (bitslen) {
	case 512:
		return PKE_RSA_DP2_512;
	case 1024:
		return PKE_RSA_DP2_1024;
	case 1536:
		return PKE_RSA_DP2_1536;
	case 2048:
		return PKE_RSA_DP2_2048;
	case 3072:
		return PKE_RSA_DP2_3072;
	case 4096:
		return PKE_RSA_DP2_4096;
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
	struct qat_asym_request *qat_req =
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

	qat_req->cb = qat_rsa_cb;
	qat_req->ctx.rsa = ctx;
	qat_req->areq.rsa = req;
	msg->pke_hdr.service_type = ICP_QAT_FW_COMN_REQ_CPM_FW_PKE;
	msg->pke_hdr.comn_req_flags =
		ICP_QAT_FW_COMN_FLAGS_BUILD(QAT_COMN_PTR_TYPE_FLAT,
					    QAT_COMN_CD_FLD_TYPE_64BIT_ADR);

	qat_req->in.rsa.enc.e = ctx->dma_e;
	qat_req->in.rsa.enc.n = ctx->dma_n;
	ret = -ENOMEM;

	/*
	 * src can be of any size in valid range, but HW expects it to be the
	 * same as modulo n so in case it is different we need to allocate a
	 * new buf and copy src data.
	 * In other case we just need to map the user provided buffer.
	 * Also need to make sure that it is in contiguous buffer.
	 */
	if (sg_is_last(req->src) && req->src_len == ctx->key_sz) {
		qat_req->src_align = NULL;
		qat_req->in.rsa.enc.m = dma_map_single(dev, sg_virt(req->src),
						   req->src_len, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev, qat_req->in.rsa.enc.m)))
			return ret;

	} else {
		int shift = ctx->key_sz - req->src_len;

		qat_req->src_align = dma_alloc_coherent(dev, ctx->key_sz,
							&qat_req->in.rsa.enc.m,
							GFP_KERNEL);
		if (unlikely(!qat_req->src_align))
			return ret;

		scatterwalk_map_and_copy(qat_req->src_align + shift, req->src,
					 0, req->src_len, 0);
	}
	if (sg_is_last(req->dst) && req->dst_len == ctx->key_sz) {
		qat_req->dst_align = NULL;
		qat_req->out.rsa.enc.c = dma_map_single(dev, sg_virt(req->dst),
							req->dst_len,
							DMA_FROM_DEVICE);

		if (unlikely(dma_mapping_error(dev, qat_req->out.rsa.enc.c)))
			goto unmap_src;

	} else {
		qat_req->dst_align = dma_alloc_coherent(dev, ctx->key_sz,
							&qat_req->out.rsa.enc.c,
							GFP_KERNEL);
		if (unlikely(!qat_req->dst_align))
			goto unmap_src;

	}
	qat_req->in.rsa.in_tab[3] = 0;
	qat_req->out.rsa.out_tab[1] = 0;
	qat_req->phy_in = dma_map_single(dev, &qat_req->in.rsa.enc.m,
					 sizeof(struct qat_rsa_input_params),
					 DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, qat_req->phy_in)))
		goto unmap_dst;

	qat_req->phy_out = dma_map_single(dev, &qat_req->out.rsa.enc.c,
					  sizeof(struct qat_rsa_output_params),
					  DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, qat_req->phy_out)))
		goto unmap_in_params;

	msg->pke_mid.src_data_addr = qat_req->phy_in;
	msg->pke_mid.dest_data_addr = qat_req->phy_out;
	msg->pke_mid.opaque = (uint64_t)(__force long)qat_req;
	msg->input_param_count = 3;
	msg->output_param_count = 1;
	do {
		ret = adf_send_message(ctx->inst->pke_tx, (uint32_t *)msg);
	} while (ret == -EBUSY && ctr++ < 100);

	if (!ret)
		return -EINPROGRESS;

	if (!dma_mapping_error(dev, qat_req->phy_out))
		dma_unmap_single(dev, qat_req->phy_out,
				 sizeof(struct qat_rsa_output_params),
				 DMA_TO_DEVICE);
unmap_in_params:
	if (!dma_mapping_error(dev, qat_req->phy_in))
		dma_unmap_single(dev, qat_req->phy_in,
				 sizeof(struct qat_rsa_input_params),
				 DMA_TO_DEVICE);
unmap_dst:
	if (qat_req->dst_align)
		dma_free_coherent(dev, ctx->key_sz, qat_req->dst_align,
				  qat_req->out.rsa.enc.c);
	else
		if (!dma_mapping_error(dev, qat_req->out.rsa.enc.c))
			dma_unmap_single(dev, qat_req->out.rsa.enc.c,
					 ctx->key_sz, DMA_FROM_DEVICE);
unmap_src:
	if (qat_req->src_align)
		dma_free_coherent(dev, ctx->key_sz, qat_req->src_align,
				  qat_req->in.rsa.enc.m);
	else
		if (!dma_mapping_error(dev, qat_req->in.rsa.enc.m))
			dma_unmap_single(dev, qat_req->in.rsa.enc.m,
					 ctx->key_sz, DMA_TO_DEVICE);
	return ret;
}

static int qat_rsa_dec(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct qat_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev = &GET_DEV(inst->accel_dev);
	struct qat_asym_request *qat_req =
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
	msg->pke_hdr.cd_pars.func_id = ctx->crt_mode ?
		qat_rsa_dec_fn_id_crt(ctx->key_sz) :
		qat_rsa_dec_fn_id(ctx->key_sz);
	if (unlikely(!msg->pke_hdr.cd_pars.func_id))
		return -EINVAL;

	qat_req->cb = qat_rsa_cb;
	qat_req->ctx.rsa = ctx;
	qat_req->areq.rsa = req;
	msg->pke_hdr.service_type = ICP_QAT_FW_COMN_REQ_CPM_FW_PKE;
	msg->pke_hdr.comn_req_flags =
		ICP_QAT_FW_COMN_FLAGS_BUILD(QAT_COMN_PTR_TYPE_FLAT,
					    QAT_COMN_CD_FLD_TYPE_64BIT_ADR);

	if (ctx->crt_mode) {
		qat_req->in.rsa.dec_crt.p = ctx->dma_p;
		qat_req->in.rsa.dec_crt.q = ctx->dma_q;
		qat_req->in.rsa.dec_crt.dp = ctx->dma_dp;
		qat_req->in.rsa.dec_crt.dq = ctx->dma_dq;
		qat_req->in.rsa.dec_crt.qinv = ctx->dma_qinv;
	} else {
		qat_req->in.rsa.dec.d = ctx->dma_d;
		qat_req->in.rsa.dec.n = ctx->dma_n;
	}
	ret = -ENOMEM;

	/*
	 * src can be of any size in valid range, but HW expects it to be the
	 * same as modulo n so in case it is different we need to allocate a
	 * new buf and copy src data.
	 * In other case we just need to map the user provided buffer.
	 * Also need to make sure that it is in contiguous buffer.
	 */
	if (sg_is_last(req->src) && req->src_len == ctx->key_sz) {
		qat_req->src_align = NULL;
		qat_req->in.rsa.dec.c = dma_map_single(dev, sg_virt(req->src),
						   req->dst_len, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev, qat_req->in.rsa.dec.c)))
			return ret;

	} else {
		int shift = ctx->key_sz - req->src_len;

		qat_req->src_align = dma_alloc_coherent(dev, ctx->key_sz,
							&qat_req->in.rsa.dec.c,
							GFP_KERNEL);
		if (unlikely(!qat_req->src_align))
			return ret;

		scatterwalk_map_and_copy(qat_req->src_align + shift, req->src,
					 0, req->src_len, 0);
	}
	if (sg_is_last(req->dst) && req->dst_len == ctx->key_sz) {
		qat_req->dst_align = NULL;
		qat_req->out.rsa.dec.m = dma_map_single(dev, sg_virt(req->dst),
						    req->dst_len,
						    DMA_FROM_DEVICE);

		if (unlikely(dma_mapping_error(dev, qat_req->out.rsa.dec.m)))
			goto unmap_src;

	} else {
		qat_req->dst_align = dma_alloc_coherent(dev, ctx->key_sz,
							&qat_req->out.rsa.dec.m,
							GFP_KERNEL);
		if (unlikely(!qat_req->dst_align))
			goto unmap_src;

	}

	if (ctx->crt_mode)
		qat_req->in.rsa.in_tab[6] = 0;
	else
		qat_req->in.rsa.in_tab[3] = 0;
	qat_req->out.rsa.out_tab[1] = 0;
	qat_req->phy_in = dma_map_single(dev, &qat_req->in.rsa.dec.c,
					 sizeof(struct qat_rsa_input_params),
					 DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, qat_req->phy_in)))
		goto unmap_dst;

	qat_req->phy_out = dma_map_single(dev, &qat_req->out.rsa.dec.m,
					  sizeof(struct qat_rsa_output_params),
					  DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, qat_req->phy_out)))
		goto unmap_in_params;

	msg->pke_mid.src_data_addr = qat_req->phy_in;
	msg->pke_mid.dest_data_addr = qat_req->phy_out;
	msg->pke_mid.opaque = (uint64_t)(__force long)qat_req;
	if (ctx->crt_mode)
		msg->input_param_count = 6;
	else
		msg->input_param_count = 3;

	msg->output_param_count = 1;
	do {
		ret = adf_send_message(ctx->inst->pke_tx, (uint32_t *)msg);
	} while (ret == -EBUSY && ctr++ < 100);

	if (!ret)
		return -EINPROGRESS;

	if (!dma_mapping_error(dev, qat_req->phy_out))
		dma_unmap_single(dev, qat_req->phy_out,
				 sizeof(struct qat_rsa_output_params),
				 DMA_TO_DEVICE);
unmap_in_params:
	if (!dma_mapping_error(dev, qat_req->phy_in))
		dma_unmap_single(dev, qat_req->phy_in,
				 sizeof(struct qat_rsa_input_params),
				 DMA_TO_DEVICE);
unmap_dst:
	if (qat_req->dst_align)
		dma_free_coherent(dev, ctx->key_sz, qat_req->dst_align,
				  qat_req->out.rsa.dec.m);
	else
		if (!dma_mapping_error(dev, qat_req->out.rsa.dec.m))
			dma_unmap_single(dev, qat_req->out.rsa.dec.m,
					 ctx->key_sz, DMA_FROM_DEVICE);
unmap_src:
	if (qat_req->src_align)
		dma_free_coherent(dev, ctx->key_sz, qat_req->src_align,
				  qat_req->in.rsa.dec.c);
	else
		if (!dma_mapping_error(dev, qat_req->in.rsa.dec.c))
			dma_unmap_single(dev, qat_req->in.rsa.dec.c,
					 ctx->key_sz, DMA_TO_DEVICE);
	return ret;
}

static int qat_rsa_set_n(struct qat_rsa_ctx *ctx, const char *value,
			 size_t vlen)
{
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
	/* invalid key size provided */
	if (!qat_rsa_enc_fn_id(ctx->key_sz))
		goto err;

	ret = -ENOMEM;
	ctx->n = dma_alloc_coherent(dev, ctx->key_sz, &ctx->dma_n, GFP_KERNEL);
	if (!ctx->n)
		goto err;

	memcpy(ctx->n, ptr, ctx->key_sz);
	return 0;
err:
	ctx->key_sz = 0;
	ctx->n = NULL;
	return ret;
}

static int qat_rsa_set_e(struct qat_rsa_ctx *ctx, const char *value,
			 size_t vlen)
{
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

	ctx->e = dma_alloc_coherent(dev, ctx->key_sz, &ctx->dma_e, GFP_KERNEL);
	if (!ctx->e)
		return -ENOMEM;

	memcpy(ctx->e + (ctx->key_sz - vlen), ptr, vlen);
	return 0;
}

static int qat_rsa_set_d(struct qat_rsa_ctx *ctx, const char *value,
			 size_t vlen)
{
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

	ret = -ENOMEM;
	ctx->d = dma_alloc_coherent(dev, ctx->key_sz, &ctx->dma_d, GFP_KERNEL);
	if (!ctx->d)
		goto err;

	memcpy(ctx->d + (ctx->key_sz - vlen), ptr, vlen);
	return 0;
err:
	ctx->d = NULL;
	return ret;
}

static void qat_rsa_drop_leading_zeros(const char **ptr, unsigned int *len)
{
	while (!**ptr && *len) {
		(*ptr)++;
		(*len)--;
	}
}

static void qat_rsa_setkey_crt(struct qat_rsa_ctx *ctx, struct rsa_key *rsa_key)
{
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev = &GET_DEV(inst->accel_dev);
	const char *ptr;
	unsigned int len;
	unsigned int half_key_sz = ctx->key_sz / 2;

	/* p */
	ptr = rsa_key->p;
	len = rsa_key->p_sz;
	qat_rsa_drop_leading_zeros(&ptr, &len);
	if (!len)
		goto err;
	ctx->p = dma_alloc_coherent(dev, half_key_sz, &ctx->dma_p, GFP_KERNEL);
	if (!ctx->p)
		goto err;
	memcpy(ctx->p + (half_key_sz - len), ptr, len);

	/* q */
	ptr = rsa_key->q;
	len = rsa_key->q_sz;
	qat_rsa_drop_leading_zeros(&ptr, &len);
	if (!len)
		goto free_p;
	ctx->q = dma_alloc_coherent(dev, half_key_sz, &ctx->dma_q, GFP_KERNEL);
	if (!ctx->q)
		goto free_p;
	memcpy(ctx->q + (half_key_sz - len), ptr, len);

	/* dp */
	ptr = rsa_key->dp;
	len = rsa_key->dp_sz;
	qat_rsa_drop_leading_zeros(&ptr, &len);
	if (!len)
		goto free_q;
	ctx->dp = dma_alloc_coherent(dev, half_key_sz, &ctx->dma_dp,
				     GFP_KERNEL);
	if (!ctx->dp)
		goto free_q;
	memcpy(ctx->dp + (half_key_sz - len), ptr, len);

	/* dq */
	ptr = rsa_key->dq;
	len = rsa_key->dq_sz;
	qat_rsa_drop_leading_zeros(&ptr, &len);
	if (!len)
		goto free_dp;
	ctx->dq = dma_alloc_coherent(dev, half_key_sz, &ctx->dma_dq,
				     GFP_KERNEL);
	if (!ctx->dq)
		goto free_dp;
	memcpy(ctx->dq + (half_key_sz - len), ptr, len);

	/* qinv */
	ptr = rsa_key->qinv;
	len = rsa_key->qinv_sz;
	qat_rsa_drop_leading_zeros(&ptr, &len);
	if (!len)
		goto free_dq;
	ctx->qinv = dma_alloc_coherent(dev, half_key_sz, &ctx->dma_qinv,
				       GFP_KERNEL);
	if (!ctx->qinv)
		goto free_dq;
	memcpy(ctx->qinv + (half_key_sz - len), ptr, len);

	ctx->crt_mode = true;
	return;

free_dq:
	memset(ctx->dq, '\0', half_key_sz);
	dma_free_coherent(dev, half_key_sz, ctx->dq, ctx->dma_dq);
	ctx->dq = NULL;
free_dp:
	memset(ctx->dp, '\0', half_key_sz);
	dma_free_coherent(dev, half_key_sz, ctx->dp, ctx->dma_dp);
	ctx->dp = NULL;
free_q:
	memset(ctx->q, '\0', half_key_sz);
	dma_free_coherent(dev, half_key_sz, ctx->q, ctx->dma_q);
	ctx->q = NULL;
free_p:
	memset(ctx->p, '\0', half_key_sz);
	dma_free_coherent(dev, half_key_sz, ctx->p, ctx->dma_p);
	ctx->p = NULL;
err:
	ctx->crt_mode = false;
}

static void qat_rsa_clear_ctx(struct device *dev, struct qat_rsa_ctx *ctx)
{
	unsigned int half_key_sz = ctx->key_sz / 2;

	/* Free the old key if any */
	if (ctx->n)
		dma_free_coherent(dev, ctx->key_sz, ctx->n, ctx->dma_n);
	if (ctx->e)
		dma_free_coherent(dev, ctx->key_sz, ctx->e, ctx->dma_e);
	if (ctx->d) {
		memset(ctx->d, '\0', ctx->key_sz);
		dma_free_coherent(dev, ctx->key_sz, ctx->d, ctx->dma_d);
	}
	if (ctx->p) {
		memset(ctx->p, '\0', half_key_sz);
		dma_free_coherent(dev, half_key_sz, ctx->p, ctx->dma_p);
	}
	if (ctx->q) {
		memset(ctx->q, '\0', half_key_sz);
		dma_free_coherent(dev, half_key_sz, ctx->q, ctx->dma_q);
	}
	if (ctx->dp) {
		memset(ctx->dp, '\0', half_key_sz);
		dma_free_coherent(dev, half_key_sz, ctx->dp, ctx->dma_dp);
	}
	if (ctx->dq) {
		memset(ctx->dq, '\0', half_key_sz);
		dma_free_coherent(dev, half_key_sz, ctx->dq, ctx->dma_dq);
	}
	if (ctx->qinv) {
		memset(ctx->qinv, '\0', half_key_sz);
		dma_free_coherent(dev, half_key_sz, ctx->qinv, ctx->dma_qinv);
	}

	ctx->n = NULL;
	ctx->e = NULL;
	ctx->d = NULL;
	ctx->p = NULL;
	ctx->q = NULL;
	ctx->dp = NULL;
	ctx->dq = NULL;
	ctx->qinv = NULL;
	ctx->crt_mode = false;
	ctx->key_sz = 0;
}

static int qat_rsa_setkey(struct crypto_akcipher *tfm, const void *key,
			  unsigned int keylen, bool private)
{
	struct qat_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct device *dev = &GET_DEV(ctx->inst->accel_dev);
	struct rsa_key rsa_key;
	int ret;

	qat_rsa_clear_ctx(dev, ctx);

	if (private)
		ret = rsa_parse_priv_key(&rsa_key, key, keylen);
	else
		ret = rsa_parse_pub_key(&rsa_key, key, keylen);
	if (ret < 0)
		goto free;

	ret = qat_rsa_set_n(ctx, rsa_key.n, rsa_key.n_sz);
	if (ret < 0)
		goto free;
	ret = qat_rsa_set_e(ctx, rsa_key.e, rsa_key.e_sz);
	if (ret < 0)
		goto free;
	if (private) {
		ret = qat_rsa_set_d(ctx, rsa_key.d, rsa_key.d_sz);
		if (ret < 0)
			goto free;
		qat_rsa_setkey_crt(ctx, &rsa_key);
	}

	if (!ctx->n || !ctx->e) {
		/* invalid key provided */
		ret = -EINVAL;
		goto free;
	}
	if (private && !ctx->d) {
		/* invalid private key provided */
		ret = -EINVAL;
		goto free;
	}

	return 0;
free:
	qat_rsa_clear_ctx(dev, ctx);
	return ret;
}

static int qat_rsa_setpubkey(struct crypto_akcipher *tfm, const void *key,
			     unsigned int keylen)
{
	return qat_rsa_setkey(tfm, key, keylen, false);
}

static int qat_rsa_setprivkey(struct crypto_akcipher *tfm, const void *key,
			      unsigned int keylen)
{
	return qat_rsa_setkey(tfm, key, keylen, true);
}

static unsigned int qat_rsa_max_size(struct crypto_akcipher *tfm)
{
	struct qat_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	return ctx->key_sz;
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
	ctx->e = NULL;
	ctx->d = NULL;
}

static struct akcipher_alg rsa = {
	.encrypt = qat_rsa_enc,
	.decrypt = qat_rsa_dec,
	.set_pub_key = qat_rsa_setpubkey,
	.set_priv_key = qat_rsa_setprivkey,
	.max_size = qat_rsa_max_size,
	.init = qat_rsa_init_tfm,
	.exit = qat_rsa_exit_tfm,
	.reqsize = sizeof(struct qat_asym_request) + 64,
	.base = {
		.cra_name = "rsa",
		.cra_driver_name = "qat-rsa",
		.cra_priority = 1000,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct qat_rsa_ctx),
	},
};

static struct kpp_alg dh = {
	.set_secret = qat_dh_set_secret,
	.generate_public_key = qat_dh_compute_value,
	.compute_shared_secret = qat_dh_compute_value,
	.max_size = qat_dh_max_size,
	.init = qat_dh_init_tfm,
	.exit = qat_dh_exit_tfm,
	.reqsize = sizeof(struct qat_asym_request) + 64,
	.base = {
		.cra_name = "dh",
		.cra_driver_name = "qat-dh",
		.cra_priority = 1000,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct qat_dh_ctx),
	},
};

int qat_asym_algs_register(void)
{
	int ret = 0;

	mutex_lock(&algs_lock);
	if (++active_devs == 1) {
		rsa.base.cra_flags = 0;
		ret = crypto_register_akcipher(&rsa);
		if (ret)
			goto unlock;
		ret = crypto_register_kpp(&dh);
	}
unlock:
	mutex_unlock(&algs_lock);
	return ret;
}

void qat_asym_algs_unregister(void)
{
	mutex_lock(&algs_lock);
	if (--active_devs == 0) {
		crypto_unregister_akcipher(&rsa);
		crypto_unregister_kpp(&dh);
	}
	mutex_unlock(&algs_lock);
}
