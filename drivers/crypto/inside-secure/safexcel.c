/*
 * Copyright (C) 2017 Marvell
 *
 * Antoine Tenart <antoine.tenart@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>

#include "safexcel.h"

static u32 max_rings = EIP197_MAX_RINGS;
module_param(max_rings, uint, 0644);
MODULE_PARM_DESC(max_rings, "Maximum number of rings to use.");

static void eip197_trc_cache_init(struct safexcel_crypto_priv *priv)
{
	u32 val, htable_offset;
	int i;

	/* Enable the record cache memory access */
	val = readl(priv->base + EIP197_CS_RAM_CTRL);
	val &= ~EIP197_TRC_ENABLE_MASK;
	val |= EIP197_TRC_ENABLE_0;
	writel(val, priv->base + EIP197_CS_RAM_CTRL);

	/* Clear all ECC errors */
	writel(0, priv->base + EIP197_TRC_ECCCTRL);

	/*
	 * Make sure the cache memory is accessible by taking record cache into
	 * reset.
	 */
	val = readl(priv->base + EIP197_TRC_PARAMS);
	val |= EIP197_TRC_PARAMS_SW_RESET;
	val &= ~EIP197_TRC_PARAMS_DATA_ACCESS;
	writel(val, priv->base + EIP197_TRC_PARAMS);

	/* Clear all records */
	for (i = 0; i < EIP197_CS_RC_MAX; i++) {
		u32 val, offset = EIP197_CLASSIFICATION_RAMS + i * EIP197_CS_RC_SIZE;

		writel(EIP197_CS_RC_NEXT(EIP197_RC_NULL) |
		       EIP197_CS_RC_PREV(EIP197_RC_NULL),
		       priv->base + offset);

		val = EIP197_CS_RC_NEXT(i+1) | EIP197_CS_RC_PREV(i-1);
		if (i == 0)
			val |= EIP197_CS_RC_PREV(EIP197_RC_NULL);
		else if (i == EIP197_CS_RC_MAX - 1)
			val |= EIP197_CS_RC_NEXT(EIP197_RC_NULL);
		writel(val, priv->base + offset + sizeof(u32));
	}

	/* Clear the hash table entries */
	htable_offset = EIP197_CS_RC_MAX * EIP197_CS_RC_SIZE;
	for (i = 0; i < 64; i++)
		writel(GENMASK(29, 0),
		       priv->base + EIP197_CLASSIFICATION_RAMS + htable_offset + i * sizeof(u32));

	/* Disable the record cache memory access */
	val = readl(priv->base + EIP197_CS_RAM_CTRL);
	val &= ~EIP197_TRC_ENABLE_MASK;
	writel(val, priv->base + EIP197_CS_RAM_CTRL);

	/* Write head and tail pointers of the record free chain */
	val = EIP197_TRC_FREECHAIN_HEAD_PTR(0) |
	      EIP197_TRC_FREECHAIN_TAIL_PTR(EIP197_CS_RC_MAX - 1);
	writel(val, priv->base + EIP197_TRC_FREECHAIN);

	/* Configure the record cache #1 */
	val = EIP197_TRC_PARAMS2_RC_SZ_SMALL(EIP197_CS_TRC_REC_WC) |
	      EIP197_TRC_PARAMS2_HTABLE_PTR(EIP197_CS_RC_MAX);
	writel(val, priv->base + EIP197_TRC_PARAMS2);

	/* Configure the record cache #2 */
	val = EIP197_TRC_PARAMS_RC_SZ_LARGE(EIP197_CS_TRC_LG_REC_WC) |
	      EIP197_TRC_PARAMS_BLK_TIMER_SPEED(1) |
	      EIP197_TRC_PARAMS_HTABLE_SZ(2);
	writel(val, priv->base + EIP197_TRC_PARAMS);
}

static void eip197_write_firmware(struct safexcel_crypto_priv *priv,
				  const struct firmware *fw, u32 ctrl,
				  u32 prog_en)
{
	const u32 *data = (const u32 *)fw->data;
	u32 val;
	int i;

	/* Reset the engine to make its program memory accessible */
	writel(EIP197_PE_ICE_x_CTRL_SW_RESET |
	       EIP197_PE_ICE_x_CTRL_CLR_ECC_CORR |
	       EIP197_PE_ICE_x_CTRL_CLR_ECC_NON_CORR,
	       EIP197_PE(priv) + ctrl);

	/* Enable access to the program memory */
	writel(prog_en, EIP197_PE(priv) + EIP197_PE_ICE_RAM_CTRL);

	/* Write the firmware */
	for (i = 0; i < fw->size / sizeof(u32); i++)
		writel(be32_to_cpu(data[i]),
		       priv->base + EIP197_CLASSIFICATION_RAMS + i * sizeof(u32));

	/* Disable access to the program memory */
	writel(0, EIP197_PE(priv) + EIP197_PE_ICE_RAM_CTRL);

	/* Release engine from reset */
	val = readl(EIP197_PE(priv) + ctrl);
	val &= ~EIP197_PE_ICE_x_CTRL_SW_RESET;
	writel(val, EIP197_PE(priv) + ctrl);
}

static int eip197_load_firmwares(struct safexcel_crypto_priv *priv)
{
	const char *fw_name[] = {"ifpp.bin", "ipue.bin"};
	const struct firmware *fw[FW_NB];
	int i, j, ret = 0;
	u32 val;

	for (i = 0; i < FW_NB; i++) {
		ret = request_firmware(&fw[i], fw_name[i], priv->dev);
		if (ret) {
			dev_err(priv->dev,
				"Failed to request firmware %s (%d)\n",
				fw_name[i], ret);
			goto release_fw;
		}
	 }

	/* Clear the scratchpad memory */
	val = readl(EIP197_PE(priv) + EIP197_PE_ICE_SCRATCH_CTRL);
	val |= EIP197_PE_ICE_SCRATCH_CTRL_CHANGE_TIMER |
	       EIP197_PE_ICE_SCRATCH_CTRL_TIMER_EN |
	       EIP197_PE_ICE_SCRATCH_CTRL_SCRATCH_ACCESS |
	       EIP197_PE_ICE_SCRATCH_CTRL_CHANGE_ACCESS;
	writel(val, EIP197_PE(priv) + EIP197_PE_ICE_SCRATCH_CTRL);

	memset(EIP197_PE(priv) + EIP197_PE_ICE_SCRATCH_RAM, 0,
	       EIP197_NUM_OF_SCRATCH_BLOCKS * sizeof(u32));

	eip197_write_firmware(priv, fw[FW_IFPP], EIP197_PE_ICE_FPP_CTRL,
			      EIP197_PE_ICE_RAM_CTRL_FPP_PROG_EN);

	eip197_write_firmware(priv, fw[FW_IPUE], EIP197_PE_ICE_PUE_CTRL,
			      EIP197_PE_ICE_RAM_CTRL_PUE_PROG_EN);

release_fw:
	for (j = 0; j < i; j++)
		release_firmware(fw[j]);

	return ret;
}

static int safexcel_hw_setup_cdesc_rings(struct safexcel_crypto_priv *priv)
{
	u32 hdw, cd_size_rnd, val;
	int i;

	hdw = readl(EIP197_HIA_AIC_G(priv) + EIP197_HIA_OPTIONS);
	hdw &= GENMASK(27, 25);
	hdw >>= 25;

	cd_size_rnd = (priv->config.cd_size + (BIT(hdw) - 1)) >> hdw;

	for (i = 0; i < priv->config.rings; i++) {
		/* ring base address */
		writel(lower_32_bits(priv->ring[i].cdr.base_dma),
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_RING_BASE_ADDR_LO);
		writel(upper_32_bits(priv->ring[i].cdr.base_dma),
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_RING_BASE_ADDR_HI);

		writel(EIP197_xDR_DESC_MODE_64BIT | (priv->config.cd_offset << 16) |
		       priv->config.cd_size,
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_DESC_SIZE);
		writel(((EIP197_FETCH_COUNT * (cd_size_rnd << hdw)) << 16) |
		       (EIP197_FETCH_COUNT * priv->config.cd_offset),
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_CFG);

		/* Configure DMA tx control */
		val = EIP197_HIA_xDR_CFG_WR_CACHE(WR_CACHE_3BITS);
		val |= EIP197_HIA_xDR_CFG_RD_CACHE(RD_CACHE_3BITS);
		writel(val, EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_DMA_CFG);

		/* clear any pending interrupt */
		writel(GENMASK(5, 0),
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_STAT);
	}

	return 0;
}

static int safexcel_hw_setup_rdesc_rings(struct safexcel_crypto_priv *priv)
{
	u32 hdw, rd_size_rnd, val;
	int i;

	hdw = readl(EIP197_HIA_AIC_G(priv) + EIP197_HIA_OPTIONS);
	hdw &= GENMASK(27, 25);
	hdw >>= 25;

	rd_size_rnd = (priv->config.rd_size + (BIT(hdw) - 1)) >> hdw;

	for (i = 0; i < priv->config.rings; i++) {
		/* ring base address */
		writel(lower_32_bits(priv->ring[i].rdr.base_dma),
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_RING_BASE_ADDR_LO);
		writel(upper_32_bits(priv->ring[i].rdr.base_dma),
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_RING_BASE_ADDR_HI);

		writel(EIP197_xDR_DESC_MODE_64BIT | (priv->config.rd_offset << 16) |
		       priv->config.rd_size,
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_DESC_SIZE);

		writel(((EIP197_FETCH_COUNT * (rd_size_rnd << hdw)) << 16) |
		       (EIP197_FETCH_COUNT * priv->config.rd_offset),
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_CFG);

		/* Configure DMA tx control */
		val = EIP197_HIA_xDR_CFG_WR_CACHE(WR_CACHE_3BITS);
		val |= EIP197_HIA_xDR_CFG_RD_CACHE(RD_CACHE_3BITS);
		val |= EIP197_HIA_xDR_WR_RES_BUF | EIP197_HIA_xDR_WR_CTRL_BUF;
		writel(val,
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_DMA_CFG);

		/* clear any pending interrupt */
		writel(GENMASK(7, 0),
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_STAT);

		/* enable ring interrupt */
		val = readl(EIP197_HIA_AIC_R(priv) + EIP197_HIA_AIC_R_ENABLE_CTRL(i));
		val |= EIP197_RDR_IRQ(i);
		writel(val, EIP197_HIA_AIC_R(priv) + EIP197_HIA_AIC_R_ENABLE_CTRL(i));
	}

	return 0;
}

static int safexcel_hw_init(struct safexcel_crypto_priv *priv)
{
	u32 version, val;
	int i, ret;

	/* Determine endianess and configure byte swap */
	version = readl(EIP197_HIA_AIC(priv) + EIP197_HIA_VERSION);
	val = readl(EIP197_HIA_AIC(priv) + EIP197_HIA_MST_CTRL);

	if ((version & 0xffff) == EIP197_HIA_VERSION_BE)
		val |= EIP197_MST_CTRL_BYTE_SWAP;
	else if (((version >> 16) & 0xffff) == EIP197_HIA_VERSION_LE)
		val |= (EIP197_MST_CTRL_NO_BYTE_SWAP >> 24);

	writel(val, EIP197_HIA_AIC(priv) + EIP197_HIA_MST_CTRL);

	/* Configure wr/rd cache values */
	writel(EIP197_MST_CTRL_RD_CACHE(RD_CACHE_4BITS) |
	       EIP197_MST_CTRL_WD_CACHE(WR_CACHE_4BITS),
	       EIP197_HIA_GEN_CFG(priv) + EIP197_MST_CTRL);

	/* Interrupts reset */

	/* Disable all global interrupts */
	writel(0, EIP197_HIA_AIC_G(priv) + EIP197_HIA_AIC_G_ENABLE_CTRL);

	/* Clear any pending interrupt */
	writel(GENMASK(31, 0), EIP197_HIA_AIC_G(priv) + EIP197_HIA_AIC_G_ACK);

	/* Data Fetch Engine configuration */

	/* Reset all DFE threads */
	writel(EIP197_DxE_THR_CTRL_RESET_PE,
	       EIP197_HIA_DFE_THR(priv) + EIP197_HIA_DFE_THR_CTRL);

	if (priv->version == EIP197) {
		/* Reset HIA input interface arbiter */
		writel(EIP197_HIA_RA_PE_CTRL_RESET,
		       EIP197_HIA_AIC(priv) + EIP197_HIA_RA_PE_CTRL);
	}

	/* DMA transfer size to use */
	val = EIP197_HIA_DFE_CFG_DIS_DEBUG;
	val |= EIP197_HIA_DxE_CFG_MIN_DATA_SIZE(5) | EIP197_HIA_DxE_CFG_MAX_DATA_SIZE(9);
	val |= EIP197_HIA_DxE_CFG_MIN_CTRL_SIZE(5) | EIP197_HIA_DxE_CFG_MAX_CTRL_SIZE(7);
	val |= EIP197_HIA_DxE_CFG_DATA_CACHE_CTRL(RD_CACHE_3BITS);
	val |= EIP197_HIA_DxE_CFG_CTRL_CACHE_CTRL(RD_CACHE_3BITS);
	writel(val, EIP197_HIA_DFE(priv) + EIP197_HIA_DFE_CFG);

	/* Leave the DFE threads reset state */
	writel(0, EIP197_HIA_DFE_THR(priv) + EIP197_HIA_DFE_THR_CTRL);

	/* Configure the procesing engine thresholds */
	writel(EIP197_PE_IN_xBUF_THRES_MIN(5) | EIP197_PE_IN_xBUF_THRES_MAX(9),
	       EIP197_PE(priv) + EIP197_PE_IN_DBUF_THRES);
	writel(EIP197_PE_IN_xBUF_THRES_MIN(5) | EIP197_PE_IN_xBUF_THRES_MAX(7),
	       EIP197_PE(priv) + EIP197_PE_IN_TBUF_THRES);

	if (priv->version == EIP197) {
		/* enable HIA input interface arbiter and rings */
		writel(EIP197_HIA_RA_PE_CTRL_EN |
		       GENMASK(priv->config.rings - 1, 0),
		       EIP197_HIA_AIC(priv) + EIP197_HIA_RA_PE_CTRL);
	}

	/* Data Store Engine configuration */

	/* Reset all DSE threads */
	writel(EIP197_DxE_THR_CTRL_RESET_PE,
	       EIP197_HIA_DSE_THR(priv) + EIP197_HIA_DSE_THR_CTRL);

	/* Wait for all DSE threads to complete */
	while ((readl(EIP197_HIA_DSE_THR(priv) + EIP197_HIA_DSE_THR_STAT) &
		GENMASK(15, 12)) != GENMASK(15, 12))
		;

	/* DMA transfer size to use */
	val = EIP197_HIA_DSE_CFG_DIS_DEBUG;
	val |= EIP197_HIA_DxE_CFG_MIN_DATA_SIZE(7) | EIP197_HIA_DxE_CFG_MAX_DATA_SIZE(8);
	val |= EIP197_HIA_DxE_CFG_DATA_CACHE_CTRL(WR_CACHE_3BITS);
	val |= EIP197_HIA_DSE_CFG_ALWAYS_BUFFERABLE;
	/* FIXME: instability issues can occur for EIP97 but disabling it impact
	 * performances.
	 */
	if (priv->version == EIP197)
		val |= EIP197_HIA_DSE_CFG_EN_SINGLE_WR;
	writel(val, EIP197_HIA_DSE(priv) + EIP197_HIA_DSE_CFG);

	/* Leave the DSE threads reset state */
	writel(0, EIP197_HIA_DSE_THR(priv) + EIP197_HIA_DSE_THR_CTRL);

	/* Configure the procesing engine thresholds */
	writel(EIP197_PE_OUT_DBUF_THRES_MIN(7) | EIP197_PE_OUT_DBUF_THRES_MAX(8),
	       EIP197_PE(priv) + EIP197_PE_OUT_DBUF_THRES);

	/* Processing Engine configuration */

	/* H/W capabilities selection */
	val = EIP197_FUNCTION_RSVD;
	val |= EIP197_PROTOCOL_ENCRYPT_ONLY | EIP197_PROTOCOL_HASH_ONLY;
	val |= EIP197_ALG_AES_ECB | EIP197_ALG_AES_CBC;
	val |= EIP197_ALG_SHA1 | EIP197_ALG_HMAC_SHA1;
	val |= EIP197_ALG_SHA2 | EIP197_ALG_HMAC_SHA2;
	writel(val, EIP197_PE(priv) + EIP197_PE_EIP96_FUNCTION_EN);

	/* Command Descriptor Rings prepare */
	for (i = 0; i < priv->config.rings; i++) {
		/* Clear interrupts for this ring */
		writel(GENMASK(31, 0),
		       EIP197_HIA_AIC_R(priv) + EIP197_HIA_AIC_R_ENABLE_CLR(i));

		/* Disable external triggering */
		writel(0, EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_CFG);

		/* Clear the pending prepared counter */
		writel(EIP197_xDR_PREP_CLR_COUNT,
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_PREP_COUNT);

		/* Clear the pending processed counter */
		writel(EIP197_xDR_PROC_CLR_COUNT,
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_PROC_COUNT);

		writel(0,
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_PREP_PNTR);
		writel(0,
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_PROC_PNTR);

		writel((EIP197_DEFAULT_RING_SIZE * priv->config.cd_offset) << 2,
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_RING_SIZE);
	}

	/* Result Descriptor Ring prepare */
	for (i = 0; i < priv->config.rings; i++) {
		/* Disable external triggering*/
		writel(0, EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_CFG);

		/* Clear the pending prepared counter */
		writel(EIP197_xDR_PREP_CLR_COUNT,
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_PREP_COUNT);

		/* Clear the pending processed counter */
		writel(EIP197_xDR_PROC_CLR_COUNT,
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_PROC_COUNT);

		writel(0,
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_PREP_PNTR);
		writel(0,
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_PROC_PNTR);

		/* Ring size */
		writel((EIP197_DEFAULT_RING_SIZE * priv->config.rd_offset) << 2,
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_RING_SIZE);
	}

	/* Enable command descriptor rings */
	writel(EIP197_DxE_THR_CTRL_EN | GENMASK(priv->config.rings - 1, 0),
	       EIP197_HIA_DFE_THR(priv) + EIP197_HIA_DFE_THR_CTRL);

	/* Enable result descriptor rings */
	writel(EIP197_DxE_THR_CTRL_EN | GENMASK(priv->config.rings - 1, 0),
	       EIP197_HIA_DSE_THR(priv) + EIP197_HIA_DSE_THR_CTRL);

	/* Clear any HIA interrupt */
	writel(GENMASK(30, 20), EIP197_HIA_AIC_G(priv) + EIP197_HIA_AIC_G_ACK);

	if (priv->version == EIP197) {
		eip197_trc_cache_init(priv);

		ret = eip197_load_firmwares(priv);
		if (ret)
			return ret;
	}

	safexcel_hw_setup_cdesc_rings(priv);
	safexcel_hw_setup_rdesc_rings(priv);

	return 0;
}

/* Called with ring's lock taken */
static void safexcel_try_push_requests(struct safexcel_crypto_priv *priv,
				       int ring)
{
	int coal = min_t(int, priv->ring[ring].requests, EIP197_MAX_BATCH_SZ);

	if (!coal)
		return;

	/* Configure when we want an interrupt */
	writel(EIP197_HIA_RDR_THRESH_PKT_MODE |
	       EIP197_HIA_RDR_THRESH_PROC_PKT(coal),
	       EIP197_HIA_RDR(priv, ring) + EIP197_HIA_xDR_THRESH);
}

void safexcel_dequeue(struct safexcel_crypto_priv *priv, int ring)
{
	struct crypto_async_request *req, *backlog;
	struct safexcel_context *ctx;
	struct safexcel_request *request;
	int ret, nreq = 0, cdesc = 0, rdesc = 0, commands, results;

	/* If a request wasn't properly dequeued because of a lack of resources,
	 * proceeded it first,
	 */
	req = priv->ring[ring].req;
	backlog = priv->ring[ring].backlog;
	if (req)
		goto handle_req;

	while (true) {
		spin_lock_bh(&priv->ring[ring].queue_lock);
		backlog = crypto_get_backlog(&priv->ring[ring].queue);
		req = crypto_dequeue_request(&priv->ring[ring].queue);
		spin_unlock_bh(&priv->ring[ring].queue_lock);

		if (!req) {
			priv->ring[ring].req = NULL;
			priv->ring[ring].backlog = NULL;
			goto finalize;
		}

handle_req:
		request = kzalloc(sizeof(*request), EIP197_GFP_FLAGS(*req));
		if (!request)
			goto request_failed;

		ctx = crypto_tfm_ctx(req->tfm);
		ret = ctx->send(req, ring, request, &commands, &results);
		if (ret) {
			kfree(request);
			goto request_failed;
		}

		if (backlog)
			backlog->complete(backlog, -EINPROGRESS);

		/* In case the send() helper did not issue any command to push
		 * to the engine because the input data was cached, continue to
		 * dequeue other requests as this is valid and not an error.
		 */
		if (!commands && !results) {
			kfree(request);
			continue;
		}

		spin_lock_bh(&priv->ring[ring].egress_lock);
		list_add_tail(&request->list, &priv->ring[ring].list);
		spin_unlock_bh(&priv->ring[ring].egress_lock);

		cdesc += commands;
		rdesc += results;
		nreq++;
	}

request_failed:
	/* Not enough resources to handle all the requests. Bail out and save
	 * the request and the backlog for the next dequeue call (per-ring).
	 */
	priv->ring[ring].req = req;
	priv->ring[ring].backlog = backlog;

finalize:
	if (!nreq)
		return;

	spin_lock_bh(&priv->ring[ring].egress_lock);

	priv->ring[ring].requests += nreq;

	if (!priv->ring[ring].busy) {
		safexcel_try_push_requests(priv, ring);
		priv->ring[ring].busy = true;
	}

	spin_unlock_bh(&priv->ring[ring].egress_lock);

	/* let the RDR know we have pending descriptors */
	writel((rdesc * priv->config.rd_offset) << 2,
	       EIP197_HIA_RDR(priv, ring) + EIP197_HIA_xDR_PREP_COUNT);

	/* let the CDR know we have pending descriptors */
	writel((cdesc * priv->config.cd_offset) << 2,
	       EIP197_HIA_CDR(priv, ring) + EIP197_HIA_xDR_PREP_COUNT);
}

void safexcel_complete(struct safexcel_crypto_priv *priv, int ring)
{
	struct safexcel_command_desc *cdesc;

	/* Acknowledge the command descriptors */
	do {
		cdesc = safexcel_ring_next_rptr(priv, &priv->ring[ring].cdr);
		if (IS_ERR(cdesc)) {
			dev_err(priv->dev,
				"Could not retrieve the command descriptor\n");
			return;
		}
	} while (!cdesc->last_seg);
}

void safexcel_inv_complete(struct crypto_async_request *req, int error)
{
	struct safexcel_inv_result *result = req->data;

	if (error == -EINPROGRESS)
		return;

	result->error = error;
	complete(&result->completion);
}

int safexcel_invalidate_cache(struct crypto_async_request *async,
			      struct safexcel_crypto_priv *priv,
			      dma_addr_t ctxr_dma, int ring,
			      struct safexcel_request *request)
{
	struct safexcel_command_desc *cdesc;
	struct safexcel_result_desc *rdesc;
	int ret = 0;

	spin_lock_bh(&priv->ring[ring].egress_lock);

	/* Prepare command descriptor */
	cdesc = safexcel_add_cdesc(priv, ring, true, true, 0, 0, 0, ctxr_dma);
	if (IS_ERR(cdesc)) {
		ret = PTR_ERR(cdesc);
		goto unlock;
	}

	cdesc->control_data.type = EIP197_TYPE_EXTENDED;
	cdesc->control_data.options = 0;
	cdesc->control_data.refresh = 0;
	cdesc->control_data.control0 = CONTEXT_CONTROL_INV_TR;

	/* Prepare result descriptor */
	rdesc = safexcel_add_rdesc(priv, ring, true, true, 0, 0);

	if (IS_ERR(rdesc)) {
		ret = PTR_ERR(rdesc);
		goto cdesc_rollback;
	}

	request->req = async;
	goto unlock;

cdesc_rollback:
	safexcel_ring_rollback_wptr(priv, &priv->ring[ring].cdr);

unlock:
	spin_unlock_bh(&priv->ring[ring].egress_lock);
	return ret;
}

static inline void safexcel_handle_result_descriptor(struct safexcel_crypto_priv *priv,
						     int ring)
{
	struct safexcel_request *sreq;
	struct safexcel_context *ctx;
	int ret, i, nreq, ndesc, tot_descs, handled = 0;
	bool should_complete;

handle_results:
	tot_descs = 0;

	nreq = readl(EIP197_HIA_RDR(priv, ring) + EIP197_HIA_xDR_PROC_COUNT);
	nreq >>= EIP197_xDR_PROC_xD_PKT_OFFSET;
	nreq &= EIP197_xDR_PROC_xD_PKT_MASK;
	if (!nreq)
		goto requests_left;

	for (i = 0; i < nreq; i++) {
		spin_lock_bh(&priv->ring[ring].egress_lock);
		sreq = list_first_entry(&priv->ring[ring].list,
					struct safexcel_request, list);
		list_del(&sreq->list);
		spin_unlock_bh(&priv->ring[ring].egress_lock);

		ctx = crypto_tfm_ctx(sreq->req->tfm);
		ndesc = ctx->handle_result(priv, ring, sreq->req,
					   &should_complete, &ret);
		if (ndesc < 0) {
			kfree(sreq);
			dev_err(priv->dev, "failed to handle result (%d)", ndesc);
			goto acknowledge;
		}

		if (should_complete) {
			local_bh_disable();
			sreq->req->complete(sreq->req, ret);
			local_bh_enable();
		}

		kfree(sreq);
		tot_descs += ndesc;
		handled++;
	}

acknowledge:
	if (i) {
		writel(EIP197_xDR_PROC_xD_PKT(i) |
		       EIP197_xDR_PROC_xD_COUNT(tot_descs * priv->config.rd_offset),
		       EIP197_HIA_RDR(priv, ring) + EIP197_HIA_xDR_PROC_COUNT);
	}

	/* If the number of requests overflowed the counter, try to proceed more
	 * requests.
	 */
	if (nreq == EIP197_xDR_PROC_xD_PKT_MASK)
		goto handle_results;

requests_left:
	spin_lock_bh(&priv->ring[ring].egress_lock);

	priv->ring[ring].requests -= handled;
	safexcel_try_push_requests(priv, ring);

	if (!priv->ring[ring].requests)
		priv->ring[ring].busy = false;

	spin_unlock_bh(&priv->ring[ring].egress_lock);
}

static void safexcel_dequeue_work(struct work_struct *work)
{
	struct safexcel_work_data *data =
			container_of(work, struct safexcel_work_data, work);

	safexcel_dequeue(data->priv, data->ring);
}

struct safexcel_ring_irq_data {
	struct safexcel_crypto_priv *priv;
	int ring;
};

static irqreturn_t safexcel_irq_ring(int irq, void *data)
{
	struct safexcel_ring_irq_data *irq_data = data;
	struct safexcel_crypto_priv *priv = irq_data->priv;
	int ring = irq_data->ring, rc = IRQ_NONE;
	u32 status, stat;

	status = readl(EIP197_HIA_AIC_R(priv) + EIP197_HIA_AIC_R_ENABLED_STAT(ring));
	if (!status)
		return rc;

	/* RDR interrupts */
	if (status & EIP197_RDR_IRQ(ring)) {
		stat = readl(EIP197_HIA_RDR(priv, ring) + EIP197_HIA_xDR_STAT);

		if (unlikely(stat & EIP197_xDR_ERR)) {
			/*
			 * Fatal error, the RDR is unusable and must be
			 * reinitialized. This should not happen under
			 * normal circumstances.
			 */
			dev_err(priv->dev, "RDR: fatal error.");
		} else if (likely(stat & EIP197_xDR_THRESH)) {
			rc = IRQ_WAKE_THREAD;
		}

		/* ACK the interrupts */
		writel(stat & 0xff,
		       EIP197_HIA_RDR(priv, ring) + EIP197_HIA_xDR_STAT);
	}

	/* ACK the interrupts */
	writel(status, EIP197_HIA_AIC_R(priv) + EIP197_HIA_AIC_R_ACK(ring));

	return rc;
}

static irqreturn_t safexcel_irq_ring_thread(int irq, void *data)
{
	struct safexcel_ring_irq_data *irq_data = data;
	struct safexcel_crypto_priv *priv = irq_data->priv;
	int ring = irq_data->ring;

	safexcel_handle_result_descriptor(priv, ring);

	queue_work(priv->ring[ring].workqueue,
		   &priv->ring[ring].work_data.work);

	return IRQ_HANDLED;
}

static int safexcel_request_ring_irq(struct platform_device *pdev, const char *name,
				     irq_handler_t handler,
				     irq_handler_t threaded_handler,
				     struct safexcel_ring_irq_data *ring_irq_priv)
{
	int ret, irq = platform_get_irq_byname(pdev, name);

	if (irq < 0) {
		dev_err(&pdev->dev, "unable to get IRQ '%s'\n", name);
		return irq;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, handler,
					threaded_handler, IRQF_ONESHOT,
					dev_name(&pdev->dev), ring_irq_priv);
	if (ret) {
		dev_err(&pdev->dev, "unable to request IRQ %d\n", irq);
		return ret;
	}

	return irq;
}

static struct safexcel_alg_template *safexcel_algs[] = {
	&safexcel_alg_ecb_aes,
	&safexcel_alg_cbc_aes,
	&safexcel_alg_sha1,
	&safexcel_alg_sha224,
	&safexcel_alg_sha256,
	&safexcel_alg_hmac_sha1,
	&safexcel_alg_hmac_sha224,
	&safexcel_alg_hmac_sha256,
};

static int safexcel_register_algorithms(struct safexcel_crypto_priv *priv)
{
	int i, j, ret = 0;

	for (i = 0; i < ARRAY_SIZE(safexcel_algs); i++) {
		safexcel_algs[i]->priv = priv;

		if (safexcel_algs[i]->type == SAFEXCEL_ALG_TYPE_SKCIPHER)
			ret = crypto_register_skcipher(&safexcel_algs[i]->alg.skcipher);
		else
			ret = crypto_register_ahash(&safexcel_algs[i]->alg.ahash);

		if (ret)
			goto fail;
	}

	return 0;

fail:
	for (j = 0; j < i; j++) {
		if (safexcel_algs[j]->type == SAFEXCEL_ALG_TYPE_SKCIPHER)
			crypto_unregister_skcipher(&safexcel_algs[j]->alg.skcipher);
		else
			crypto_unregister_ahash(&safexcel_algs[j]->alg.ahash);
	}

	return ret;
}

static void safexcel_unregister_algorithms(struct safexcel_crypto_priv *priv)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(safexcel_algs); i++) {
		if (safexcel_algs[i]->type == SAFEXCEL_ALG_TYPE_SKCIPHER)
			crypto_unregister_skcipher(&safexcel_algs[i]->alg.skcipher);
		else
			crypto_unregister_ahash(&safexcel_algs[i]->alg.ahash);
	}
}

static void safexcel_configure(struct safexcel_crypto_priv *priv)
{
	u32 val, mask;

	val = readl(EIP197_HIA_AIC_G(priv) + EIP197_HIA_OPTIONS);
	val = (val & GENMASK(27, 25)) >> 25;
	mask = BIT(val) - 1;

	val = readl(EIP197_HIA_AIC_G(priv) + EIP197_HIA_OPTIONS);
	priv->config.rings = min_t(u32, val & GENMASK(3, 0), max_rings);

	priv->config.cd_size = (sizeof(struct safexcel_command_desc) / sizeof(u32));
	priv->config.cd_offset = (priv->config.cd_size + mask) & ~mask;

	priv->config.rd_size = (sizeof(struct safexcel_result_desc) / sizeof(u32));
	priv->config.rd_offset = (priv->config.rd_size + mask) & ~mask;
}

static void safexcel_init_register_offsets(struct safexcel_crypto_priv *priv)
{
	struct safexcel_register_offsets *offsets = &priv->offsets;

	if (priv->version == EIP197) {
		offsets->hia_aic	= EIP197_HIA_AIC_BASE;
		offsets->hia_aic_g	= EIP197_HIA_AIC_G_BASE;
		offsets->hia_aic_r	= EIP197_HIA_AIC_R_BASE;
		offsets->hia_aic_xdr	= EIP197_HIA_AIC_xDR_BASE;
		offsets->hia_dfe	= EIP197_HIA_DFE_BASE;
		offsets->hia_dfe_thr	= EIP197_HIA_DFE_THR_BASE;
		offsets->hia_dse	= EIP197_HIA_DSE_BASE;
		offsets->hia_dse_thr	= EIP197_HIA_DSE_THR_BASE;
		offsets->hia_gen_cfg	= EIP197_HIA_GEN_CFG_BASE;
		offsets->pe		= EIP197_PE_BASE;
	} else {
		offsets->hia_aic	= EIP97_HIA_AIC_BASE;
		offsets->hia_aic_g	= EIP97_HIA_AIC_G_BASE;
		offsets->hia_aic_r	= EIP97_HIA_AIC_R_BASE;
		offsets->hia_aic_xdr	= EIP97_HIA_AIC_xDR_BASE;
		offsets->hia_dfe	= EIP97_HIA_DFE_BASE;
		offsets->hia_dfe_thr	= EIP97_HIA_DFE_THR_BASE;
		offsets->hia_dse	= EIP97_HIA_DSE_BASE;
		offsets->hia_dse_thr	= EIP97_HIA_DSE_THR_BASE;
		offsets->hia_gen_cfg	= EIP97_HIA_GEN_CFG_BASE;
		offsets->pe		= EIP97_PE_BASE;
	}
}

static int safexcel_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct safexcel_crypto_priv *priv;
	int i, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->version = (enum safexcel_eip_version)of_device_get_match_data(dev);

	safexcel_init_register_offsets(priv);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base)) {
		dev_err(dev, "failed to get resource\n");
		return PTR_ERR(priv->base);
	}

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	ret = PTR_ERR_OR_ZERO(priv->clk);
	/* The clock isn't mandatory */
	if  (ret != -ENOENT) {
		if (ret)
			return ret;

		ret = clk_prepare_enable(priv->clk);
		if (ret) {
			dev_err(dev, "unable to enable clk (%d)\n", ret);
			return ret;
		}
	}

	priv->reg_clk = devm_clk_get(&pdev->dev, "reg");
	ret = PTR_ERR_OR_ZERO(priv->reg_clk);
	/* The clock isn't mandatory */
	if  (ret != -ENOENT) {
		if (ret)
			goto err_core_clk;

		ret = clk_prepare_enable(priv->reg_clk);
		if (ret) {
			dev_err(dev, "unable to enable reg clk (%d)\n", ret);
			goto err_core_clk;
		}
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret)
		goto err_reg_clk;

	priv->context_pool = dmam_pool_create("safexcel-context", dev,
					      sizeof(struct safexcel_context_record),
					      1, 0);
	if (!priv->context_pool) {
		ret = -ENOMEM;
		goto err_reg_clk;
	}

	safexcel_configure(priv);

	for (i = 0; i < priv->config.rings; i++) {
		char irq_name[6] = {0}; /* "ringX\0" */
		char wq_name[9] = {0}; /* "wq_ringX\0" */
		int irq;
		struct safexcel_ring_irq_data *ring_irq;

		ret = safexcel_init_ring_descriptors(priv,
						     &priv->ring[i].cdr,
						     &priv->ring[i].rdr);
		if (ret)
			goto err_reg_clk;

		ring_irq = devm_kzalloc(dev, sizeof(*ring_irq), GFP_KERNEL);
		if (!ring_irq) {
			ret = -ENOMEM;
			goto err_reg_clk;
		}

		ring_irq->priv = priv;
		ring_irq->ring = i;

		snprintf(irq_name, 6, "ring%d", i);
		irq = safexcel_request_ring_irq(pdev, irq_name, safexcel_irq_ring,
						safexcel_irq_ring_thread,
						ring_irq);
		if (irq < 0) {
			ret = irq;
			goto err_reg_clk;
		}

		priv->ring[i].work_data.priv = priv;
		priv->ring[i].work_data.ring = i;
		INIT_WORK(&priv->ring[i].work_data.work, safexcel_dequeue_work);

		snprintf(wq_name, 9, "wq_ring%d", i);
		priv->ring[i].workqueue = create_singlethread_workqueue(wq_name);
		if (!priv->ring[i].workqueue) {
			ret = -ENOMEM;
			goto err_reg_clk;
		}

		priv->ring[i].requests = 0;
		priv->ring[i].busy = false;

		crypto_init_queue(&priv->ring[i].queue,
				  EIP197_DEFAULT_RING_SIZE);

		INIT_LIST_HEAD(&priv->ring[i].list);
		spin_lock_init(&priv->ring[i].lock);
		spin_lock_init(&priv->ring[i].egress_lock);
		spin_lock_init(&priv->ring[i].queue_lock);
	}

	platform_set_drvdata(pdev, priv);
	atomic_set(&priv->ring_used, 0);

	ret = safexcel_hw_init(priv);
	if (ret) {
		dev_err(dev, "EIP h/w init failed (%d)\n", ret);
		goto err_reg_clk;
	}

	ret = safexcel_register_algorithms(priv);
	if (ret) {
		dev_err(dev, "Failed to register algorithms (%d)\n", ret);
		goto err_reg_clk;
	}

	return 0;

err_reg_clk:
	clk_disable_unprepare(priv->reg_clk);
err_core_clk:
	clk_disable_unprepare(priv->clk);
	return ret;
}


static int safexcel_remove(struct platform_device *pdev)
{
	struct safexcel_crypto_priv *priv = platform_get_drvdata(pdev);
	int i;

	safexcel_unregister_algorithms(priv);
	clk_disable_unprepare(priv->clk);

	for (i = 0; i < priv->config.rings; i++)
		destroy_workqueue(priv->ring[i].workqueue);

	return 0;
}

static const struct of_device_id safexcel_of_match_table[] = {
	{
		.compatible = "inside-secure,safexcel-eip97",
		.data = (void *)EIP97,
	},
	{
		.compatible = "inside-secure,safexcel-eip197",
		.data = (void *)EIP197,
	},
	{},
};


static struct platform_driver  crypto_safexcel = {
	.probe		= safexcel_probe,
	.remove		= safexcel_remove,
	.driver		= {
		.name	= "crypto-safexcel",
		.of_match_table = safexcel_of_match_table,
	},
};
module_platform_driver(crypto_safexcel);

MODULE_AUTHOR("Antoine Tenart <antoine.tenart@free-electrons.com>");
MODULE_AUTHOR("Ofer Heifetz <oferh@marvell.com>");
MODULE_AUTHOR("Igal Liberman <igall@marvell.com>");
MODULE_DESCRIPTION("Support for SafeXcel cryptographic engine EIP197");
MODULE_LICENSE("GPL v2");
