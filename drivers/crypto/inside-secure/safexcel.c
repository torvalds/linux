// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Marvell
 *
 * Antoine Tenart <antoine.tenart@free-electrons.com>
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
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>

#include "safexcel.h"

static u32 max_rings = EIP197_MAX_RINGS;
module_param(max_rings, uint, 0644);
MODULE_PARM_DESC(max_rings, "Maximum number of rings to use.");

static void eip197_trc_cache_init(struct safexcel_crypto_priv *priv)
{
	u32 val, htable_offset;
	int i, cs_rc_max, cs_ht_wc, cs_trc_rec_wc, cs_trc_lg_rec_wc;

	if (priv->version == EIP197D_MRVL) {
		cs_rc_max = EIP197D_CS_RC_MAX;
		cs_ht_wc = EIP197D_CS_HT_WC;
		cs_trc_rec_wc = EIP197D_CS_TRC_REC_WC;
		cs_trc_lg_rec_wc = EIP197D_CS_TRC_LG_REC_WC;
	} else {
		/* Default to minimum "safe" settings */
		cs_rc_max = EIP197B_CS_RC_MAX;
		cs_ht_wc = EIP197B_CS_HT_WC;
		cs_trc_rec_wc = EIP197B_CS_TRC_REC_WC;
		cs_trc_lg_rec_wc = EIP197B_CS_TRC_LG_REC_WC;
	}

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
	for (i = 0; i < cs_rc_max; i++) {
		u32 val, offset = EIP197_CLASSIFICATION_RAMS + i * EIP197_CS_RC_SIZE;

		writel(EIP197_CS_RC_NEXT(EIP197_RC_NULL) |
		       EIP197_CS_RC_PREV(EIP197_RC_NULL),
		       priv->base + offset);

		val = EIP197_CS_RC_NEXT(i+1) | EIP197_CS_RC_PREV(i-1);
		if (i == 0)
			val |= EIP197_CS_RC_PREV(EIP197_RC_NULL);
		else if (i == cs_rc_max - 1)
			val |= EIP197_CS_RC_NEXT(EIP197_RC_NULL);
		writel(val, priv->base + offset + sizeof(u32));
	}

	/* Clear the hash table entries */
	htable_offset = cs_rc_max * EIP197_CS_RC_SIZE;
	for (i = 0; i < cs_ht_wc; i++)
		writel(GENMASK(29, 0),
		       priv->base + EIP197_CLASSIFICATION_RAMS + htable_offset + i * sizeof(u32));

	/* Disable the record cache memory access */
	val = readl(priv->base + EIP197_CS_RAM_CTRL);
	val &= ~EIP197_TRC_ENABLE_MASK;
	writel(val, priv->base + EIP197_CS_RAM_CTRL);

	/* Write head and tail pointers of the record free chain */
	val = EIP197_TRC_FREECHAIN_HEAD_PTR(0) |
	      EIP197_TRC_FREECHAIN_TAIL_PTR(cs_rc_max - 1);
	writel(val, priv->base + EIP197_TRC_FREECHAIN);

	/* Configure the record cache #1 */
	val = EIP197_TRC_PARAMS2_RC_SZ_SMALL(cs_trc_rec_wc) |
	      EIP197_TRC_PARAMS2_HTABLE_PTR(cs_rc_max);
	writel(val, priv->base + EIP197_TRC_PARAMS2);

	/* Configure the record cache #2 */
	val = EIP197_TRC_PARAMS_RC_SZ_LARGE(cs_trc_lg_rec_wc) |
	      EIP197_TRC_PARAMS_BLK_TIMER_SPEED(1) |
	      EIP197_TRC_PARAMS_HTABLE_SZ(2);
	writel(val, priv->base + EIP197_TRC_PARAMS);
}

static void eip197_init_firmware(struct safexcel_crypto_priv *priv)
{
	int pe, i;
	u32 val;

	for (pe = 0; pe < priv->config.pes; pe++) {
		/* Configure the token FIFO's */
		writel(3, EIP197_PE(priv) + EIP197_PE_ICE_PUTF_CTRL(pe));
		writel(0, EIP197_PE(priv) + EIP197_PE_ICE_PPTF_CTRL(pe));

		/* Clear the ICE scratchpad memory */
		val = readl(EIP197_PE(priv) + EIP197_PE_ICE_SCRATCH_CTRL(pe));
		val |= EIP197_PE_ICE_SCRATCH_CTRL_CHANGE_TIMER |
		       EIP197_PE_ICE_SCRATCH_CTRL_TIMER_EN |
		       EIP197_PE_ICE_SCRATCH_CTRL_SCRATCH_ACCESS |
		       EIP197_PE_ICE_SCRATCH_CTRL_CHANGE_ACCESS;
		writel(val, EIP197_PE(priv) + EIP197_PE_ICE_SCRATCH_CTRL(pe));

		/* clear the scratchpad RAM using 32 bit writes only */
		for (i = 0; i < EIP197_NUM_OF_SCRATCH_BLOCKS; i++)
			writel(0, EIP197_PE(priv) +
				  EIP197_PE_ICE_SCRATCH_RAM(pe) + (i<<2));

		/* Reset the IFPP engine to make its program mem accessible */
		writel(EIP197_PE_ICE_x_CTRL_SW_RESET |
		       EIP197_PE_ICE_x_CTRL_CLR_ECC_CORR |
		       EIP197_PE_ICE_x_CTRL_CLR_ECC_NON_CORR,
		       EIP197_PE(priv) + EIP197_PE_ICE_FPP_CTRL(pe));

		/* Reset the IPUE engine to make its program mem accessible */
		writel(EIP197_PE_ICE_x_CTRL_SW_RESET |
		       EIP197_PE_ICE_x_CTRL_CLR_ECC_CORR |
		       EIP197_PE_ICE_x_CTRL_CLR_ECC_NON_CORR,
		       EIP197_PE(priv) + EIP197_PE_ICE_PUE_CTRL(pe));

		/* Enable access to all IFPP program memories */
		writel(EIP197_PE_ICE_RAM_CTRL_FPP_PROG_EN,
		       EIP197_PE(priv) + EIP197_PE_ICE_RAM_CTRL(pe));
	}

}

static int eip197_write_firmware(struct safexcel_crypto_priv *priv,
				  const struct firmware *fw)
{
	const u32 *data = (const u32 *)fw->data;
	int i;

	/* Write the firmware */
	for (i = 0; i < fw->size / sizeof(u32); i++)
		writel(be32_to_cpu(data[i]),
		       priv->base + EIP197_CLASSIFICATION_RAMS + i * sizeof(u32));

	/* Exclude final 2 NOPs from size */
	return i - EIP197_FW_TERMINAL_NOPS;
}

/*
 * If FW is actual production firmware, then poll for its initialization
 * to complete and check if it is good for the HW, otherwise just return OK.
 */
static bool poll_fw_ready(struct safexcel_crypto_priv *priv, int fpp)
{
	int pe, pollcnt;
	u32 base, pollofs;

	if (fpp)
		pollofs  = EIP197_FW_FPP_READY;
	else
		pollofs  = EIP197_FW_PUE_READY;

	for (pe = 0; pe < priv->config.pes; pe++) {
		base = EIP197_PE_ICE_SCRATCH_RAM(pe);
		pollcnt = EIP197_FW_START_POLLCNT;
		while (pollcnt &&
		       (readl_relaxed(EIP197_PE(priv) + base +
			      pollofs) != 1)) {
			pollcnt--;
		}
		if (!pollcnt) {
			dev_err(priv->dev, "FW(%d) for PE %d failed to start\n",
				fpp, pe);
			return false;
		}
	}
	return true;
}

static bool eip197_start_firmware(struct safexcel_crypto_priv *priv,
				  int ipuesz, int ifppsz, int minifw)
{
	int pe;
	u32 val;

	for (pe = 0; pe < priv->config.pes; pe++) {
		/* Disable access to all program memory */
		writel(0, EIP197_PE(priv) + EIP197_PE_ICE_RAM_CTRL(pe));

		/* Start IFPP microengines */
		if (minifw)
			val = 0;
		else
			val = EIP197_PE_ICE_UENG_START_OFFSET((ifppsz - 1) &
					EIP197_PE_ICE_UENG_INIT_ALIGN_MASK) |
				EIP197_PE_ICE_UENG_DEBUG_RESET;
		writel(val, EIP197_PE(priv) + EIP197_PE_ICE_FPP_CTRL(pe));

		/* Start IPUE microengines */
		if (minifw)
			val = 0;
		else
			val = EIP197_PE_ICE_UENG_START_OFFSET((ipuesz - 1) &
					EIP197_PE_ICE_UENG_INIT_ALIGN_MASK) |
				EIP197_PE_ICE_UENG_DEBUG_RESET;
		writel(val, EIP197_PE(priv) + EIP197_PE_ICE_PUE_CTRL(pe));
	}

	/* For miniFW startup, there is no initialization, so always succeed */
	if (minifw)
		return true;

	/* Wait until all the firmwares have properly started up */
	if (!poll_fw_ready(priv, 1))
		return false;
	if (!poll_fw_ready(priv, 0))
		return false;

	return true;
}

static int eip197_load_firmwares(struct safexcel_crypto_priv *priv)
{
	const char *fw_name[] = {"ifpp.bin", "ipue.bin"};
	const struct firmware *fw[FW_NB];
	char fw_path[37], *dir = NULL;
	int i, j, ret = 0, pe;
	int ipuesz, ifppsz, minifw = 0;

	if (priv->version == EIP197D_MRVL)
		dir = "eip197d";
	else if (priv->version == EIP197B_MRVL ||
		 priv->version == EIP197_DEVBRD)
		dir = "eip197b";
	else
		return -ENODEV;

retry_fw:
	for (i = 0; i < FW_NB; i++) {
		snprintf(fw_path, 37, "inside-secure/%s/%s", dir, fw_name[i]);
		ret = firmware_request_nowarn(&fw[i], fw_path, priv->dev);
		if (ret) {
			if (minifw || priv->version != EIP197B_MRVL)
				goto release_fw;

			/* Fallback to the old firmware location for the
			 * EIP197b.
			 */
			ret = firmware_request_nowarn(&fw[i], fw_name[i],
						      priv->dev);
			if (ret)
				goto release_fw;
		}
	}

	eip197_init_firmware(priv);

	ifppsz = eip197_write_firmware(priv, fw[FW_IFPP]);

	/* Enable access to IPUE program memories */
	for (pe = 0; pe < priv->config.pes; pe++)
		writel(EIP197_PE_ICE_RAM_CTRL_PUE_PROG_EN,
		       EIP197_PE(priv) + EIP197_PE_ICE_RAM_CTRL(pe));

	ipuesz = eip197_write_firmware(priv, fw[FW_IPUE]);

	if (eip197_start_firmware(priv, ipuesz, ifppsz, minifw)) {
		dev_dbg(priv->dev, "Firmware loaded successfully");
		return 0;
	}

	ret = -ENODEV;

release_fw:
	for (j = 0; j < i; j++)
		release_firmware(fw[j]);

	if (!minifw) {
		/* Retry with minifw path */
		dev_dbg(priv->dev, "Firmware set not (fully) present or init failed, falling back to BCLA mode\n");
		dir = "eip197_minifw";
		minifw = 1;
		goto retry_fw;
	}

	dev_dbg(priv->dev, "Firmware load failed.\n");

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
	int i, ret, pe;

	dev_dbg(priv->dev, "HW init: using %d pipe(s) and %d ring(s)\n",
		priv->config.pes, priv->config.rings);

	/* Determine endianess and configure byte swap */
	version = readl(EIP197_HIA_AIC(priv) + EIP197_HIA_VERSION);
	val = readl(EIP197_HIA_AIC(priv) + EIP197_HIA_MST_CTRL);

	if ((version & 0xffff) == EIP197_HIA_VERSION_BE)
		val |= EIP197_MST_CTRL_BYTE_SWAP;
	else if (((version >> 16) & 0xffff) == EIP197_HIA_VERSION_LE)
		val |= (EIP197_MST_CTRL_NO_BYTE_SWAP >> 24);

	/*
	 * For EIP197's only set maximum number of TX commands to 2^5 = 32
	 * Skip for the EIP97 as it does not have this field.
	 */
	if (priv->version != EIP97IES_MRVL)
		val |= EIP197_MST_CTRL_TX_MAX_CMD(5);

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

	/* Processing Engine configuration */
	for (pe = 0; pe < priv->config.pes; pe++) {
		/* Data Fetch Engine configuration */

		/* Reset all DFE threads */
		writel(EIP197_DxE_THR_CTRL_RESET_PE,
		       EIP197_HIA_DFE_THR(priv) + EIP197_HIA_DFE_THR_CTRL(pe));

		if (priv->version != EIP97IES_MRVL)
			/* Reset HIA input interface arbiter (EIP197 only) */
			writel(EIP197_HIA_RA_PE_CTRL_RESET,
			       EIP197_HIA_AIC(priv) + EIP197_HIA_RA_PE_CTRL(pe));

		/* DMA transfer size to use */
		val = EIP197_HIA_DFE_CFG_DIS_DEBUG;
		val |= EIP197_HIA_DxE_CFG_MIN_DATA_SIZE(6) |
		       EIP197_HIA_DxE_CFG_MAX_DATA_SIZE(9);
		val |= EIP197_HIA_DxE_CFG_MIN_CTRL_SIZE(6) |
		       EIP197_HIA_DxE_CFG_MAX_CTRL_SIZE(7);
		val |= EIP197_HIA_DxE_CFG_DATA_CACHE_CTRL(RD_CACHE_3BITS);
		val |= EIP197_HIA_DxE_CFG_CTRL_CACHE_CTRL(RD_CACHE_3BITS);
		writel(val, EIP197_HIA_DFE(priv) + EIP197_HIA_DFE_CFG(pe));

		/* Leave the DFE threads reset state */
		writel(0, EIP197_HIA_DFE_THR(priv) + EIP197_HIA_DFE_THR_CTRL(pe));

		/* Configure the processing engine thresholds */
		writel(EIP197_PE_IN_xBUF_THRES_MIN(6) |
		       EIP197_PE_IN_xBUF_THRES_MAX(9),
		       EIP197_PE(priv) + EIP197_PE_IN_DBUF_THRES(pe));
		writel(EIP197_PE_IN_xBUF_THRES_MIN(6) |
		       EIP197_PE_IN_xBUF_THRES_MAX(7),
		       EIP197_PE(priv) + EIP197_PE_IN_TBUF_THRES(pe));

		if (priv->version != EIP97IES_MRVL)
			/* enable HIA input interface arbiter and rings */
			writel(EIP197_HIA_RA_PE_CTRL_EN |
			       GENMASK(priv->config.rings - 1, 0),
			       EIP197_HIA_AIC(priv) + EIP197_HIA_RA_PE_CTRL(pe));

		/* Data Store Engine configuration */

		/* Reset all DSE threads */
		writel(EIP197_DxE_THR_CTRL_RESET_PE,
		       EIP197_HIA_DSE_THR(priv) + EIP197_HIA_DSE_THR_CTRL(pe));

		/* Wait for all DSE threads to complete */
		while ((readl(EIP197_HIA_DSE_THR(priv) + EIP197_HIA_DSE_THR_STAT(pe)) &
			GENMASK(15, 12)) != GENMASK(15, 12))
			;

		/* DMA transfer size to use */
		val = EIP197_HIA_DSE_CFG_DIS_DEBUG;
		val |= EIP197_HIA_DxE_CFG_MIN_DATA_SIZE(7) |
		       EIP197_HIA_DxE_CFG_MAX_DATA_SIZE(8);
		val |= EIP197_HIA_DxE_CFG_DATA_CACHE_CTRL(WR_CACHE_3BITS);
		val |= EIP197_HIA_DSE_CFG_ALWAYS_BUFFERABLE;
		/* FIXME: instability issues can occur for EIP97 but disabling
		 * it impacts performance.
		 */
		if (priv->version != EIP97IES_MRVL)
			val |= EIP197_HIA_DSE_CFG_EN_SINGLE_WR;
		writel(val, EIP197_HIA_DSE(priv) + EIP197_HIA_DSE_CFG(pe));

		/* Leave the DSE threads reset state */
		writel(0, EIP197_HIA_DSE_THR(priv) + EIP197_HIA_DSE_THR_CTRL(pe));

		/* Configure the procesing engine thresholds */
		writel(EIP197_PE_OUT_DBUF_THRES_MIN(7) |
		       EIP197_PE_OUT_DBUF_THRES_MAX(8),
		       EIP197_PE(priv) + EIP197_PE_OUT_DBUF_THRES(pe));

		/* Processing Engine configuration */

		/* Token & context configuration */
		val = EIP197_PE_EIP96_TOKEN_CTRL_CTX_UPDATES |
		      EIP197_PE_EIP96_TOKEN_CTRL_REUSE_CTX |
		      EIP197_PE_EIP96_TOKEN_CTRL_POST_REUSE_CTX;
		writel(val, EIP197_PE(priv) + EIP197_PE_EIP96_TOKEN_CTRL(pe));

		/* H/W capabilities selection: just enable everything */
		writel(EIP197_FUNCTION_ALL,
		       EIP197_PE(priv) + EIP197_PE_EIP96_FUNCTION_EN(pe));
	}

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

	for (pe = 0; pe < priv->config.pes; pe++) {
		/* Enable command descriptor rings */
		writel(EIP197_DxE_THR_CTRL_EN | GENMASK(priv->config.rings - 1, 0),
		       EIP197_HIA_DFE_THR(priv) + EIP197_HIA_DFE_THR_CTRL(pe));

		/* Enable result descriptor rings */
		writel(EIP197_DxE_THR_CTRL_EN | GENMASK(priv->config.rings - 1, 0),
		       EIP197_HIA_DSE_THR(priv) + EIP197_HIA_DSE_THR_CTRL(pe));
	}

	/* Clear any HIA interrupt */
	writel(GENMASK(30, 20), EIP197_HIA_AIC_G(priv) + EIP197_HIA_AIC_G_ACK);

	if (priv->version != EIP97IES_MRVL) {
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
		ctx = crypto_tfm_ctx(req->tfm);
		ret = ctx->send(req, ring, &commands, &results);
		if (ret)
			goto request_failed;

		if (backlog)
			backlog->complete(backlog, -EINPROGRESS);

		/* In case the send() helper did not issue any command to push
		 * to the engine because the input data was cached, continue to
		 * dequeue other requests as this is valid and not an error.
		 */
		if (!commands && !results)
			continue;

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

	spin_lock_bh(&priv->ring[ring].lock);

	priv->ring[ring].requests += nreq;

	if (!priv->ring[ring].busy) {
		safexcel_try_push_requests(priv, ring);
		priv->ring[ring].busy = true;
	}

	spin_unlock_bh(&priv->ring[ring].lock);

	/* let the RDR know we have pending descriptors */
	writel((rdesc * priv->config.rd_offset) << 2,
	       EIP197_HIA_RDR(priv, ring) + EIP197_HIA_xDR_PREP_COUNT);

	/* let the CDR know we have pending descriptors */
	writel((cdesc * priv->config.cd_offset) << 2,
	       EIP197_HIA_CDR(priv, ring) + EIP197_HIA_xDR_PREP_COUNT);
}

inline int safexcel_rdesc_check_errors(struct safexcel_crypto_priv *priv,
				       struct safexcel_result_desc *rdesc)
{
	if (likely((!rdesc->descriptor_overflow) &&
		   (!rdesc->buffer_overflow) &&
		   (!rdesc->result_data.error_code)))
		return 0;

	if (rdesc->descriptor_overflow)
		dev_err(priv->dev, "Descriptor overflow detected");

	if (rdesc->buffer_overflow)
		dev_err(priv->dev, "Buffer overflow detected");

	if (rdesc->result_data.error_code & 0x4066) {
		/* Fatal error (bits 1,2,5,6 & 14) */
		dev_err(priv->dev,
			"result descriptor error (%x)",
			rdesc->result_data.error_code);
		return -EIO;
	} else if (rdesc->result_data.error_code &
		   (BIT(7) | BIT(4) | BIT(3) | BIT(0))) {
		/*
		 * Give priority over authentication fails:
		 * Blocksize, length & overflow errors,
		 * something wrong with the input!
		 */
		return -EINVAL;
	} else if (rdesc->result_data.error_code & BIT(9)) {
		/* Authentication failed */
		return -EBADMSG;
	}

	/* All other non-fatal errors */
	return -EINVAL;
}

inline void safexcel_rdr_req_set(struct safexcel_crypto_priv *priv,
				 int ring,
				 struct safexcel_result_desc *rdesc,
				 struct crypto_async_request *req)
{
	int i = safexcel_ring_rdr_rdesc_index(priv, ring, rdesc);

	priv->ring[ring].rdr_req[i] = req;
}

inline struct crypto_async_request *
safexcel_rdr_req_get(struct safexcel_crypto_priv *priv, int ring)
{
	int i = safexcel_ring_first_rdr_index(priv, ring);

	return priv->ring[ring].rdr_req[i];
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
			      dma_addr_t ctxr_dma, int ring)
{
	struct safexcel_command_desc *cdesc;
	struct safexcel_result_desc *rdesc;
	int ret = 0;

	/* Prepare command descriptor */
	cdesc = safexcel_add_cdesc(priv, ring, true, true, 0, 0, 0, ctxr_dma);
	if (IS_ERR(cdesc))
		return PTR_ERR(cdesc);

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

	safexcel_rdr_req_set(priv, ring, rdesc, async);

	return ret;

cdesc_rollback:
	safexcel_ring_rollback_wptr(priv, &priv->ring[ring].cdr);

	return ret;
}

static inline void safexcel_handle_result_descriptor(struct safexcel_crypto_priv *priv,
						     int ring)
{
	struct crypto_async_request *req;
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
		req = safexcel_rdr_req_get(priv, ring);

		ctx = crypto_tfm_ctx(req->tfm);
		ndesc = ctx->handle_result(priv, ring, req,
					   &should_complete, &ret);
		if (ndesc < 0) {
			dev_err(priv->dev, "failed to handle result (%d)\n",
				ndesc);
			goto acknowledge;
		}

		if (should_complete) {
			local_bh_disable();
			req->complete(req, ret);
			local_bh_enable();
		}

		tot_descs += ndesc;
		handled++;
	}

acknowledge:
	if (i)
		writel(EIP197_xDR_PROC_xD_PKT(i) |
		       EIP197_xDR_PROC_xD_COUNT(tot_descs * priv->config.rd_offset),
		       EIP197_HIA_RDR(priv, ring) + EIP197_HIA_xDR_PROC_COUNT);

	/* If the number of requests overflowed the counter, try to proceed more
	 * requests.
	 */
	if (nreq == EIP197_xDR_PROC_xD_PKT_MASK)
		goto handle_results;

requests_left:
	spin_lock_bh(&priv->ring[ring].lock);

	priv->ring[ring].requests -= handled;
	safexcel_try_push_requests(priv, ring);

	if (!priv->ring[ring].requests)
		priv->ring[ring].busy = false;

	spin_unlock_bh(&priv->ring[ring].lock);
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
			dev_err(priv->dev, "RDR: fatal error.\n");
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

static int safexcel_request_ring_irq(void *pdev, int irqid,
				     int is_pci_dev,
				     irq_handler_t handler,
				     irq_handler_t threaded_handler,
				     struct safexcel_ring_irq_data *ring_irq_priv)
{
	int ret, irq;
	struct device *dev;

	if (IS_ENABLED(CONFIG_PCI) && is_pci_dev) {
		struct pci_dev *pci_pdev = pdev;

		dev = &pci_pdev->dev;
		irq = pci_irq_vector(pci_pdev, irqid);
		if (irq < 0) {
			dev_err(dev, "unable to get device MSI IRQ %d (err %d)\n",
				irqid, irq);
			return irq;
		}
	} else if (IS_ENABLED(CONFIG_OF)) {
		struct platform_device *plf_pdev = pdev;
		char irq_name[6] = {0}; /* "ringX\0" */

		snprintf(irq_name, 6, "ring%d", irqid);
		dev = &plf_pdev->dev;
		irq = platform_get_irq_byname(plf_pdev, irq_name);

		if (irq < 0) {
			dev_err(dev, "unable to get IRQ '%s' (err %d)\n",
				irq_name, irq);
			return irq;
		}
	}

	ret = devm_request_threaded_irq(dev, irq, handler,
					threaded_handler, IRQF_ONESHOT,
					dev_name(dev), ring_irq_priv);
	if (ret) {
		dev_err(dev, "unable to request IRQ %d\n", irq);
		return ret;
	}

	return irq;
}

static struct safexcel_alg_template *safexcel_algs[] = {
	&safexcel_alg_ecb_des,
	&safexcel_alg_cbc_des,
	&safexcel_alg_ecb_des3_ede,
	&safexcel_alg_cbc_des3_ede,
	&safexcel_alg_ecb_aes,
	&safexcel_alg_cbc_aes,
	&safexcel_alg_ctr_aes,
	&safexcel_alg_md5,
	&safexcel_alg_sha1,
	&safexcel_alg_sha224,
	&safexcel_alg_sha256,
	&safexcel_alg_sha384,
	&safexcel_alg_sha512,
	&safexcel_alg_hmac_md5,
	&safexcel_alg_hmac_sha1,
	&safexcel_alg_hmac_sha224,
	&safexcel_alg_hmac_sha256,
	&safexcel_alg_hmac_sha384,
	&safexcel_alg_hmac_sha512,
	&safexcel_alg_authenc_hmac_sha1_cbc_aes,
	&safexcel_alg_authenc_hmac_sha224_cbc_aes,
	&safexcel_alg_authenc_hmac_sha256_cbc_aes,
	&safexcel_alg_authenc_hmac_sha384_cbc_aes,
	&safexcel_alg_authenc_hmac_sha512_cbc_aes,
	&safexcel_alg_authenc_hmac_sha1_cbc_des3_ede,
	&safexcel_alg_authenc_hmac_sha1_ctr_aes,
	&safexcel_alg_authenc_hmac_sha224_ctr_aes,
	&safexcel_alg_authenc_hmac_sha256_ctr_aes,
	&safexcel_alg_authenc_hmac_sha384_ctr_aes,
	&safexcel_alg_authenc_hmac_sha512_ctr_aes,
};

static int safexcel_register_algorithms(struct safexcel_crypto_priv *priv)
{
	int i, j, ret = 0;

	for (i = 0; i < ARRAY_SIZE(safexcel_algs); i++) {
		safexcel_algs[i]->priv = priv;

		if (safexcel_algs[i]->type == SAFEXCEL_ALG_TYPE_SKCIPHER)
			ret = crypto_register_skcipher(&safexcel_algs[i]->alg.skcipher);
		else if (safexcel_algs[i]->type == SAFEXCEL_ALG_TYPE_AEAD)
			ret = crypto_register_aead(&safexcel_algs[i]->alg.aead);
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
		else if (safexcel_algs[j]->type == SAFEXCEL_ALG_TYPE_AEAD)
			crypto_unregister_aead(&safexcel_algs[j]->alg.aead);
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
		else if (safexcel_algs[i]->type == SAFEXCEL_ALG_TYPE_AEAD)
			crypto_unregister_aead(&safexcel_algs[i]->alg.aead);
		else
			crypto_unregister_ahash(&safexcel_algs[i]->alg.ahash);
	}
}

static void safexcel_configure(struct safexcel_crypto_priv *priv)
{
	u32 val, mask = 0;

	val = readl(EIP197_HIA_AIC_G(priv) + EIP197_HIA_OPTIONS);

	/* Read number of PEs from the engine */
	if (priv->version == EIP97IES_MRVL)
		/* Narrow field width for EIP97 type engine */
		mask = EIP97_N_PES_MASK;
	else
		/* Wider field width for all EIP197 type engines */
		mask = EIP197_N_PES_MASK;

	priv->config.pes = (val >> EIP197_N_PES_OFFSET) & mask;

	priv->config.rings = min_t(u32, val & GENMASK(3, 0), max_rings);

	val = (val & GENMASK(27, 25)) >> 25;
	mask = BIT(val) - 1;

	priv->config.cd_size = (sizeof(struct safexcel_command_desc) / sizeof(u32));
	priv->config.cd_offset = (priv->config.cd_size + mask) & ~mask;

	priv->config.rd_size = (sizeof(struct safexcel_result_desc) / sizeof(u32));
	priv->config.rd_offset = (priv->config.rd_size + mask) & ~mask;
}

static void safexcel_init_register_offsets(struct safexcel_crypto_priv *priv)
{
	struct safexcel_register_offsets *offsets = &priv->offsets;

	if (priv->version == EIP97IES_MRVL) {
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
	} else {
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
	}
}

/*
 * Generic part of probe routine, shared by platform and PCI driver
 *
 * Assumes IO resources have been mapped, private data mem has been allocated,
 * clocks have been enabled, device pointer has been assigned etc.
 *
 */
static int safexcel_probe_generic(void *pdev,
				  struct safexcel_crypto_priv *priv,
				  int is_pci_dev)
{
	struct device *dev = priv->dev;
	int i, ret;

	priv->context_pool = dmam_pool_create("safexcel-context", dev,
					      sizeof(struct safexcel_context_record),
					      1, 0);
	if (!priv->context_pool)
		return -ENOMEM;

	safexcel_init_register_offsets(priv);

	if (priv->version != EIP97IES_MRVL)
		priv->flags |= EIP197_TRC_CACHE;

	safexcel_configure(priv);

	if (IS_ENABLED(CONFIG_PCI) && priv->version == EIP197_DEVBRD) {
		/*
		 * Request MSI vectors for global + 1 per ring -
		 * or just 1 for older dev images
		 */
		struct pci_dev *pci_pdev = pdev;

		ret = pci_alloc_irq_vectors(pci_pdev,
					    priv->config.rings + 1,
					    priv->config.rings + 1,
					    PCI_IRQ_MSI | PCI_IRQ_MSIX);
		if (ret < 0) {
			dev_err(dev, "Failed to allocate PCI MSI interrupts\n");
			return ret;
		}
	}

	/* Register the ring IRQ handlers and configure the rings */
	priv->ring = devm_kcalloc(dev, priv->config.rings,
				  sizeof(*priv->ring),
				  GFP_KERNEL);
	if (!priv->ring)
		return -ENOMEM;

	for (i = 0; i < priv->config.rings; i++) {
		char wq_name[9] = {0};
		int irq;
		struct safexcel_ring_irq_data *ring_irq;

		ret = safexcel_init_ring_descriptors(priv,
						     &priv->ring[i].cdr,
						     &priv->ring[i].rdr);
		if (ret) {
			dev_err(dev, "Failed to initialize rings\n");
			return ret;
		}

		priv->ring[i].rdr_req = devm_kcalloc(dev,
			EIP197_DEFAULT_RING_SIZE,
			sizeof(priv->ring[i].rdr_req),
			GFP_KERNEL);
		if (!priv->ring[i].rdr_req)
			return -ENOMEM;

		ring_irq = devm_kzalloc(dev, sizeof(*ring_irq), GFP_KERNEL);
		if (!ring_irq)
			return -ENOMEM;

		ring_irq->priv = priv;
		ring_irq->ring = i;

		irq = safexcel_request_ring_irq(pdev,
						EIP197_IRQ_NUMBER(i, is_pci_dev),
						is_pci_dev,
						safexcel_irq_ring,
						safexcel_irq_ring_thread,
						ring_irq);
		if (irq < 0) {
			dev_err(dev, "Failed to get IRQ ID for ring %d\n", i);
			return irq;
		}

		priv->ring[i].work_data.priv = priv;
		priv->ring[i].work_data.ring = i;
		INIT_WORK(&priv->ring[i].work_data.work,
			  safexcel_dequeue_work);

		snprintf(wq_name, 9, "wq_ring%d", i);
		priv->ring[i].workqueue =
			create_singlethread_workqueue(wq_name);
		if (!priv->ring[i].workqueue)
			return -ENOMEM;

		priv->ring[i].requests = 0;
		priv->ring[i].busy = false;

		crypto_init_queue(&priv->ring[i].queue,
				  EIP197_DEFAULT_RING_SIZE);

		spin_lock_init(&priv->ring[i].lock);
		spin_lock_init(&priv->ring[i].queue_lock);
	}

	atomic_set(&priv->ring_used, 0);

	ret = safexcel_hw_init(priv);
	if (ret) {
		dev_err(dev, "HW init failed (%d)\n", ret);
		return ret;
	}

	ret = safexcel_register_algorithms(priv);
	if (ret) {
		dev_err(dev, "Failed to register algorithms (%d)\n", ret);
		return ret;
	}

	return 0;
}

static void safexcel_hw_reset_rings(struct safexcel_crypto_priv *priv)
{
	int i;

	for (i = 0; i < priv->config.rings; i++) {
		/* clear any pending interrupt */
		writel(GENMASK(5, 0), EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_STAT);
		writel(GENMASK(7, 0), EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_STAT);

		/* Reset the CDR base address */
		writel(0, EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_RING_BASE_ADDR_LO);
		writel(0, EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_RING_BASE_ADDR_HI);

		/* Reset the RDR base address */
		writel(0, EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_RING_BASE_ADDR_LO);
		writel(0, EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_RING_BASE_ADDR_HI);
	}
}

#if IS_ENABLED(CONFIG_OF)
/* for Device Tree platform driver */

static int safexcel_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct safexcel_crypto_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->version = (enum safexcel_eip_version)of_device_get_match_data(dev);

	platform_set_drvdata(pdev, priv);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
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

	/* Generic EIP97/EIP197 device probing */
	ret = safexcel_probe_generic(pdev, priv, 0);
	if (ret)
		goto err_reg_clk;

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
	safexcel_hw_reset_rings(priv);

	clk_disable_unprepare(priv->clk);

	for (i = 0; i < priv->config.rings; i++)
		destroy_workqueue(priv->ring[i].workqueue);

	return 0;
}

static const struct of_device_id safexcel_of_match_table[] = {
	{
		.compatible = "inside-secure,safexcel-eip97ies",
		.data = (void *)EIP97IES_MRVL,
	},
	{
		.compatible = "inside-secure,safexcel-eip197b",
		.data = (void *)EIP197B_MRVL,
	},
	{
		.compatible = "inside-secure,safexcel-eip197d",
		.data = (void *)EIP197D_MRVL,
	},
	/* For backward compatibility and intended for generic use */
	{
		.compatible = "inside-secure,safexcel-eip97",
		.data = (void *)EIP97IES_MRVL,
	},
	{
		.compatible = "inside-secure,safexcel-eip197",
		.data = (void *)EIP197B_MRVL,
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
#endif

#if IS_ENABLED(CONFIG_PCI)
/* PCIE devices - i.e. Inside Secure development boards */

static int safexcel_pci_probe(struct pci_dev *pdev,
			       const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct safexcel_crypto_priv *priv;
	void __iomem *pciebase;
	int rc;
	u32 val;

	dev_dbg(dev, "Probing PCIE device: vendor %04x, device %04x, subv %04x, subdev %04x, ctxt %lx\n",
		ent->vendor, ent->device, ent->subvendor,
		ent->subdevice, ent->driver_data);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->version = (enum safexcel_eip_version)ent->driver_data;

	pci_set_drvdata(pdev, priv);

	/* enable the device */
	rc = pcim_enable_device(pdev);
	if (rc) {
		dev_err(dev, "Failed to enable PCI device\n");
		return rc;
	}

	/* take ownership of PCI BAR0 */
	rc = pcim_iomap_regions(pdev, 1, "crypto_safexcel");
	if (rc) {
		dev_err(dev, "Failed to map IO region for BAR0\n");
		return rc;
	}
	priv->base = pcim_iomap_table(pdev)[0];

	if (priv->version == EIP197_DEVBRD) {
		dev_dbg(dev, "Device identified as FPGA based development board - applying HW reset\n");

		rc = pcim_iomap_regions(pdev, 4, "crypto_safexcel");
		if (rc) {
			dev_err(dev, "Failed to map IO region for BAR4\n");
			return rc;
		}

		pciebase = pcim_iomap_table(pdev)[2];
		val = readl(pciebase + EIP197_XLX_IRQ_BLOCK_ID_ADDR);
		if ((val >> 16) == EIP197_XLX_IRQ_BLOCK_ID_VALUE) {
			dev_dbg(dev, "Detected Xilinx PCIE IRQ block version %d, multiple MSI support enabled\n",
				(val & 0xff));

			/* Setup MSI identity map mapping */
			writel(EIP197_XLX_USER_VECT_LUT0_IDENT,
			       pciebase + EIP197_XLX_USER_VECT_LUT0_ADDR);
			writel(EIP197_XLX_USER_VECT_LUT1_IDENT,
			       pciebase + EIP197_XLX_USER_VECT_LUT1_ADDR);
			writel(EIP197_XLX_USER_VECT_LUT2_IDENT,
			       pciebase + EIP197_XLX_USER_VECT_LUT2_ADDR);
			writel(EIP197_XLX_USER_VECT_LUT3_IDENT,
			       pciebase + EIP197_XLX_USER_VECT_LUT3_ADDR);

			/* Enable all device interrupts */
			writel(GENMASK(31, 0),
			       pciebase + EIP197_XLX_USER_INT_ENB_MSK);
		} else {
			dev_err(dev, "Unrecognised IRQ block identifier %x\n",
				val);
			return -ENODEV;
		}

		/* HW reset FPGA dev board */
		/* assert reset */
		writel(1, priv->base + EIP197_XLX_GPIO_BASE);
		wmb(); /* maintain strict ordering for accesses here */
		/* deassert reset */
		writel(0, priv->base + EIP197_XLX_GPIO_BASE);
		wmb(); /* maintain strict ordering for accesses here */
	}

	/* enable bus mastering */
	pci_set_master(pdev);

	/* Generic EIP97/EIP197 device probing */
	rc = safexcel_probe_generic(pdev, priv, 1);
	return rc;
}

void safexcel_pci_remove(struct pci_dev *pdev)
{
	struct safexcel_crypto_priv *priv = pci_get_drvdata(pdev);
	int i;

	safexcel_unregister_algorithms(priv);

	for (i = 0; i < priv->config.rings; i++)
		destroy_workqueue(priv->ring[i].workqueue);

	safexcel_hw_reset_rings(priv);
}

static const struct pci_device_id safexcel_pci_ids[] = {
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_XILINX, 0x9038,
			       0x16ae, 0xc522),
		/* assume EIP197B for now */
		.driver_data = EIP197_DEVBRD,
	},
	{},
};

MODULE_DEVICE_TABLE(pci, safexcel_pci_ids);

static struct pci_driver safexcel_pci_driver = {
	.name          = "crypto-safexcel",
	.id_table      = safexcel_pci_ids,
	.probe         = safexcel_pci_probe,
	.remove        = safexcel_pci_remove,
};
#endif

static int __init safexcel_init(void)
{
	int rc;

#if IS_ENABLED(CONFIG_OF)
		/* Register platform driver */
		platform_driver_register(&crypto_safexcel);
#endif

#if IS_ENABLED(CONFIG_PCI)
		/* Register PCI driver */
		rc = pci_register_driver(&safexcel_pci_driver);
#endif

	return 0;
}

static void __exit safexcel_exit(void)
{
#if IS_ENABLED(CONFIG_OF)
		/* Unregister platform driver */
		platform_driver_unregister(&crypto_safexcel);
#endif

#if IS_ENABLED(CONFIG_PCI)
		/* Unregister PCI driver if successfully registered before */
		pci_unregister_driver(&safexcel_pci_driver);
#endif
}

module_init(safexcel_init);
module_exit(safexcel_exit);

MODULE_AUTHOR("Antoine Tenart <antoine.tenart@free-electrons.com>");
MODULE_AUTHOR("Ofer Heifetz <oferh@marvell.com>");
MODULE_AUTHOR("Igal Liberman <igall@marvell.com>");
MODULE_DESCRIPTION("Support for SafeXcel cryptographic engines: EIP97 & EIP197");
MODULE_LICENSE("GPL v2");
