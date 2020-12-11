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

static void eip197_trc_cache_setupvirt(struct safexcel_crypto_priv *priv)
{
	int i;

	/*
	 * Map all interfaces/rings to register index 0
	 * so they can share contexts. Without this, the EIP197 will
	 * assume each interface/ring to be in its own memory domain
	 * i.e. have its own subset of UNIQUE memory addresses.
	 * Which would cause records with the SAME memory address to
	 * use DIFFERENT cache buffers, causing both poor cache utilization
	 * AND serious coherence/invalidation issues.
	 */
	for (i = 0; i < 4; i++)
		writel(0, priv->base + EIP197_FLUE_IFC_LUT(i));

	/*
	 * Initialize other virtualization regs for cache
	 * These may not be in their reset state ...
	 */
	for (i = 0; i < priv->config.rings; i++) {
		writel(0, priv->base + EIP197_FLUE_CACHEBASE_LO(i));
		writel(0, priv->base + EIP197_FLUE_CACHEBASE_HI(i));
		writel(EIP197_FLUE_CONFIG_MAGIC,
		       priv->base + EIP197_FLUE_CONFIG(i));
	}
	writel(0, priv->base + EIP197_FLUE_OFFSETS);
	writel(0, priv->base + EIP197_FLUE_ARC4_OFFSET);
}

static void eip197_trc_cache_banksel(struct safexcel_crypto_priv *priv,
				     u32 addrmid, int *actbank)
{
	u32 val;
	int curbank;

	curbank = addrmid >> 16;
	if (curbank != *actbank) {
		val = readl(priv->base + EIP197_CS_RAM_CTRL);
		val = (val & ~EIP197_CS_BANKSEL_MASK) |
		      (curbank << EIP197_CS_BANKSEL_OFS);
		writel(val, priv->base + EIP197_CS_RAM_CTRL);
		*actbank = curbank;
	}
}

static u32 eip197_trc_cache_probe(struct safexcel_crypto_priv *priv,
				  int maxbanks, u32 probemask, u32 stride)
{
	u32 val, addrhi, addrlo, addrmid, addralias, delta, marker;
	int actbank;

	/*
	 * And probe the actual size of the physically attached cache data RAM
	 * Using a binary subdivision algorithm downto 32 byte cache lines.
	 */
	addrhi = 1 << (16 + maxbanks);
	addrlo = 0;
	actbank = min(maxbanks - 1, 0);
	while ((addrhi - addrlo) > stride) {
		/* write marker to lowest address in top half */
		addrmid = (addrhi + addrlo) >> 1;
		marker = (addrmid ^ 0xabadbabe) & probemask; /* Unique */
		eip197_trc_cache_banksel(priv, addrmid, &actbank);
		writel(marker,
			priv->base + EIP197_CLASSIFICATION_RAMS +
			(addrmid & 0xffff));

		/* write invalid markers to possible aliases */
		delta = 1 << __fls(addrmid);
		while (delta >= stride) {
			addralias = addrmid - delta;
			eip197_trc_cache_banksel(priv, addralias, &actbank);
			writel(~marker,
			       priv->base + EIP197_CLASSIFICATION_RAMS +
			       (addralias & 0xffff));
			delta >>= 1;
		}

		/* read back marker from top half */
		eip197_trc_cache_banksel(priv, addrmid, &actbank);
		val = readl(priv->base + EIP197_CLASSIFICATION_RAMS +
			    (addrmid & 0xffff));

		if ((val & probemask) == marker)
			/* read back correct, continue with top half */
			addrlo = addrmid;
		else
			/* not read back correct, continue with bottom half */
			addrhi = addrmid;
	}
	return addrhi;
}

static void eip197_trc_cache_clear(struct safexcel_crypto_priv *priv,
				   int cs_rc_max, int cs_ht_wc)
{
	int i;
	u32 htable_offset, val, offset;

	/* Clear all records in administration RAM */
	for (i = 0; i < cs_rc_max; i++) {
		offset = EIP197_CLASSIFICATION_RAMS + i * EIP197_CS_RC_SIZE;

		writel(EIP197_CS_RC_NEXT(EIP197_RC_NULL) |
		       EIP197_CS_RC_PREV(EIP197_RC_NULL),
		       priv->base + offset);

		val = EIP197_CS_RC_NEXT(i + 1) | EIP197_CS_RC_PREV(i - 1);
		if (i == 0)
			val |= EIP197_CS_RC_PREV(EIP197_RC_NULL);
		else if (i == cs_rc_max - 1)
			val |= EIP197_CS_RC_NEXT(EIP197_RC_NULL);
		writel(val, priv->base + offset + 4);
		/* must also initialize the address key due to ECC! */
		writel(0, priv->base + offset + 8);
		writel(0, priv->base + offset + 12);
	}

	/* Clear the hash table entries */
	htable_offset = cs_rc_max * EIP197_CS_RC_SIZE;
	for (i = 0; i < cs_ht_wc; i++)
		writel(GENMASK(29, 0),
		       priv->base + EIP197_CLASSIFICATION_RAMS +
		       htable_offset + i * sizeof(u32));
}

static int eip197_trc_cache_init(struct safexcel_crypto_priv *priv)
{
	u32 val, dsize, asize;
	int cs_rc_max, cs_ht_wc, cs_trc_rec_wc, cs_trc_lg_rec_wc;
	int cs_rc_abs_max, cs_ht_sz;
	int maxbanks;

	/* Setup (dummy) virtualization for cache */
	eip197_trc_cache_setupvirt(priv);

	/*
	 * Enable the record cache memory access and
	 * probe the bank select width
	 */
	val = readl(priv->base + EIP197_CS_RAM_CTRL);
	val &= ~EIP197_TRC_ENABLE_MASK;
	val |= EIP197_TRC_ENABLE_0 | EIP197_CS_BANKSEL_MASK;
	writel(val, priv->base + EIP197_CS_RAM_CTRL);
	val = readl(priv->base + EIP197_CS_RAM_CTRL);
	maxbanks = ((val&EIP197_CS_BANKSEL_MASK)>>EIP197_CS_BANKSEL_OFS) + 1;

	/* Clear all ECC errors */
	writel(0, priv->base + EIP197_TRC_ECCCTRL);

	/*
	 * Make sure the cache memory is accessible by taking record cache into
	 * reset. Need data memory access here, not admin access.
	 */
	val = readl(priv->base + EIP197_TRC_PARAMS);
	val |= EIP197_TRC_PARAMS_SW_RESET | EIP197_TRC_PARAMS_DATA_ACCESS;
	writel(val, priv->base + EIP197_TRC_PARAMS);

	/* Probed data RAM size in bytes */
	dsize = eip197_trc_cache_probe(priv, maxbanks, 0xffffffff, 32);

	/*
	 * Now probe the administration RAM size pretty much the same way
	 * Except that only the lower 30 bits are writable and we don't need
	 * bank selects
	 */
	val = readl(priv->base + EIP197_TRC_PARAMS);
	/* admin access now */
	val &= ~(EIP197_TRC_PARAMS_DATA_ACCESS | EIP197_CS_BANKSEL_MASK);
	writel(val, priv->base + EIP197_TRC_PARAMS);

	/* Probed admin RAM size in admin words */
	asize = eip197_trc_cache_probe(priv, 0, 0x3fffffff, 16) >> 4;

	/* Clear any ECC errors detected while probing! */
	writel(0, priv->base + EIP197_TRC_ECCCTRL);

	/* Sanity check probing results */
	if (dsize < EIP197_MIN_DSIZE || asize < EIP197_MIN_ASIZE) {
		dev_err(priv->dev, "Record cache probing failed (%d,%d).",
			dsize, asize);
		return -ENODEV;
	}

	/*
	 * Determine optimal configuration from RAM sizes
	 * Note that we assume that the physical RAM configuration is sane
	 * Therefore, we don't do any parameter error checking here ...
	 */

	/* For now, just use a single record format covering everything */
	cs_trc_rec_wc = EIP197_CS_TRC_REC_WC;
	cs_trc_lg_rec_wc = EIP197_CS_TRC_REC_WC;

	/*
	 * Step #1: How many records will physically fit?
	 * Hard upper limit is 1023!
	 */
	cs_rc_abs_max = min_t(uint, ((dsize >> 2) / cs_trc_lg_rec_wc), 1023);
	/* Step #2: Need at least 2 words in the admin RAM per record */
	cs_rc_max = min_t(uint, cs_rc_abs_max, (asize >> 1));
	/* Step #3: Determine log2 of hash table size */
	cs_ht_sz = __fls(asize - cs_rc_max) - 2;
	/* Step #4: determine current size of hash table in dwords */
	cs_ht_wc = 16 << cs_ht_sz; /* dwords, not admin words */
	/* Step #5: add back excess words and see if we can fit more records */
	cs_rc_max = min_t(uint, cs_rc_abs_max, asize - (cs_ht_wc >> 2));

	/* Clear the cache RAMs */
	eip197_trc_cache_clear(priv, cs_rc_max, cs_ht_wc);

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
	      EIP197_TRC_PARAMS_HTABLE_SZ(cs_ht_sz);
	writel(val, priv->base + EIP197_TRC_PARAMS);

	dev_info(priv->dev, "TRC init: %dd,%da (%dr,%dh)\n",
		 dsize, asize, cs_rc_max, cs_ht_wc + cs_ht_wc);
	return 0;
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
				  EIP197_PE_ICE_SCRATCH_RAM(pe) + (i << 2));

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

		/* bypass the OCE, if present */
		if (priv->flags & EIP197_OCE)
			writel(EIP197_DEBUG_OCE_BYPASS, EIP197_PE(priv) +
							EIP197_PE_DEBUG(pe));
	}

}

static int eip197_write_firmware(struct safexcel_crypto_priv *priv,
				  const struct firmware *fw)
{
	const __be32 *data = (const __be32 *)fw->data;
	int i;

	/* Write the firmware */
	for (i = 0; i < fw->size / sizeof(u32); i++)
		writel(be32_to_cpu(data[i]),
		       priv->base + EIP197_CLASSIFICATION_RAMS +
		       i * sizeof(__be32));

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
		dev_dbg(priv->dev, "Firmware loaded successfully\n");
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
	u32 cd_size_rnd, val;
	int i, cd_fetch_cnt;

	cd_size_rnd  = (priv->config.cd_size +
			(BIT(priv->hwconfig.hwdataw) - 1)) >>
		       priv->hwconfig.hwdataw;
	/* determine number of CD's we can fetch into the CD FIFO as 1 block */
	if (priv->flags & SAFEXCEL_HW_EIP197) {
		/* EIP197: try to fetch enough in 1 go to keep all pipes busy */
		cd_fetch_cnt = (1 << priv->hwconfig.hwcfsize) / cd_size_rnd;
		cd_fetch_cnt = min_t(uint, cd_fetch_cnt,
				     (priv->config.pes * EIP197_FETCH_DEPTH));
	} else {
		/* for the EIP97, just fetch all that fits minus 1 */
		cd_fetch_cnt = ((1 << priv->hwconfig.hwcfsize) /
				cd_size_rnd) - 1;
	}
	/*
	 * Since we're using command desc's way larger than formally specified,
	 * we need to check whether we can fit even 1 for low-end EIP196's!
	 */
	if (!cd_fetch_cnt) {
		dev_err(priv->dev, "Unable to fit even 1 command desc!\n");
		return -ENODEV;
	}

	for (i = 0; i < priv->config.rings; i++) {
		/* ring base address */
		writel(lower_32_bits(priv->ring[i].cdr.base_dma),
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_RING_BASE_ADDR_LO);
		writel(upper_32_bits(priv->ring[i].cdr.base_dma),
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_RING_BASE_ADDR_HI);

		writel(EIP197_xDR_DESC_MODE_64BIT | EIP197_CDR_DESC_MODE_ADCP |
		       (priv->config.cd_offset << 14) | priv->config.cd_size,
		       EIP197_HIA_CDR(priv, i) + EIP197_HIA_xDR_DESC_SIZE);
		writel(((cd_fetch_cnt *
			 (cd_size_rnd << priv->hwconfig.hwdataw)) << 16) |
		       (cd_fetch_cnt * (priv->config.cd_offset / sizeof(u32))),
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
	u32 rd_size_rnd, val;
	int i, rd_fetch_cnt;

	/* determine number of RD's we can fetch into the FIFO as one block */
	rd_size_rnd = (EIP197_RD64_FETCH_SIZE +
		       (BIT(priv->hwconfig.hwdataw) - 1)) >>
		      priv->hwconfig.hwdataw;
	if (priv->flags & SAFEXCEL_HW_EIP197) {
		/* EIP197: try to fetch enough in 1 go to keep all pipes busy */
		rd_fetch_cnt = (1 << priv->hwconfig.hwrfsize) / rd_size_rnd;
		rd_fetch_cnt = min_t(uint, rd_fetch_cnt,
				     (priv->config.pes * EIP197_FETCH_DEPTH));
	} else {
		/* for the EIP97, just fetch all that fits minus 1 */
		rd_fetch_cnt = ((1 << priv->hwconfig.hwrfsize) /
				rd_size_rnd) - 1;
	}

	for (i = 0; i < priv->config.rings; i++) {
		/* ring base address */
		writel(lower_32_bits(priv->ring[i].rdr.base_dma),
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_RING_BASE_ADDR_LO);
		writel(upper_32_bits(priv->ring[i].rdr.base_dma),
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_RING_BASE_ADDR_HI);

		writel(EIP197_xDR_DESC_MODE_64BIT | (priv->config.rd_offset << 14) |
		       priv->config.rd_size,
		       EIP197_HIA_RDR(priv, i) + EIP197_HIA_xDR_DESC_SIZE);

		writel(((rd_fetch_cnt *
			 (rd_size_rnd << priv->hwconfig.hwdataw)) << 16) |
		       (rd_fetch_cnt * (priv->config.rd_offset / sizeof(u32))),
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
	u32 val;
	int i, ret, pe, opbuflo, opbufhi;

	dev_dbg(priv->dev, "HW init: using %d pipe(s) and %d ring(s)\n",
		priv->config.pes, priv->config.rings);

	/*
	 * For EIP197's only set maximum number of TX commands to 2^5 = 32
	 * Skip for the EIP97 as it does not have this field.
	 */
	if (priv->flags & SAFEXCEL_HW_EIP197) {
		val = readl(EIP197_HIA_AIC(priv) + EIP197_HIA_MST_CTRL);
		val |= EIP197_MST_CTRL_TX_MAX_CMD(5);
		writel(val, EIP197_HIA_AIC(priv) + EIP197_HIA_MST_CTRL);
	}

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

		if (priv->flags & EIP197_PE_ARB)
			/* Reset HIA input interface arbiter (if present) */
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

		if (priv->flags & SAFEXCEL_HW_EIP197)
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
		if (priv->hwconfig.hwnumpes > 4) {
			opbuflo = 9;
			opbufhi = 10;
		} else {
			opbuflo = 7;
			opbufhi = 8;
		}
		val = EIP197_HIA_DSE_CFG_DIS_DEBUG;
		val |= EIP197_HIA_DxE_CFG_MIN_DATA_SIZE(opbuflo) |
		       EIP197_HIA_DxE_CFG_MAX_DATA_SIZE(opbufhi);
		val |= EIP197_HIA_DxE_CFG_DATA_CACHE_CTRL(WR_CACHE_3BITS);
		val |= EIP197_HIA_DSE_CFG_ALWAYS_BUFFERABLE;
		/* FIXME: instability issues can occur for EIP97 but disabling
		 * it impacts performance.
		 */
		if (priv->flags & SAFEXCEL_HW_EIP197)
			val |= EIP197_HIA_DSE_CFG_EN_SINGLE_WR;
		writel(val, EIP197_HIA_DSE(priv) + EIP197_HIA_DSE_CFG(pe));

		/* Leave the DSE threads reset state */
		writel(0, EIP197_HIA_DSE_THR(priv) + EIP197_HIA_DSE_THR_CTRL(pe));

		/* Configure the procesing engine thresholds */
		writel(EIP197_PE_OUT_DBUF_THRES_MIN(opbuflo) |
		       EIP197_PE_OUT_DBUF_THRES_MAX(opbufhi),
		       EIP197_PE(priv) + EIP197_PE_OUT_DBUF_THRES(pe));

		/* Processing Engine configuration */

		/* Token & context configuration */
		val = EIP197_PE_EIP96_TOKEN_CTRL_CTX_UPDATES |
		      EIP197_PE_EIP96_TOKEN_CTRL_NO_TOKEN_WAIT |
		      EIP197_PE_EIP96_TOKEN_CTRL_ENABLE_TIMEOUT;
		writel(val, EIP197_PE(priv) + EIP197_PE_EIP96_TOKEN_CTRL(pe));

		/* H/W capabilities selection: just enable everything */
		writel(EIP197_FUNCTION_ALL,
		       EIP197_PE(priv) + EIP197_PE_EIP96_FUNCTION_EN(pe));
		writel(EIP197_FUNCTION_ALL,
		       EIP197_PE(priv) + EIP197_PE_EIP96_FUNCTION2_EN(pe));
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

		writel((EIP197_DEFAULT_RING_SIZE * priv->config.cd_offset),
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
		writel((EIP197_DEFAULT_RING_SIZE * priv->config.rd_offset),
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

	if (priv->flags & EIP197_SIMPLE_TRC) {
		writel(EIP197_STRC_CONFIG_INIT |
		       EIP197_STRC_CONFIG_LARGE_REC(EIP197_CS_TRC_REC_WC) |
		       EIP197_STRC_CONFIG_SMALL_REC(EIP197_CS_TRC_REC_WC),
		       priv->base + EIP197_STRC_CONFIG);
		writel(EIP197_PE_EIP96_TOKEN_CTRL2_CTX_DONE,
		       EIP197_PE(priv) + EIP197_PE_EIP96_TOKEN_CTRL2(0));
	} else if (priv->flags & SAFEXCEL_HW_EIP197) {
		ret = eip197_trc_cache_init(priv);
		if (ret)
			return ret;
	}

	if (priv->flags & EIP197_ICE) {
		ret = eip197_load_firmwares(priv);
		if (ret)
			return ret;
	}

	return safexcel_hw_setup_cdesc_rings(priv) ?:
	       safexcel_hw_setup_rdesc_rings(priv) ?:
	       0;
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
	writel((rdesc * priv->config.rd_offset),
	       EIP197_HIA_RDR(priv, ring) + EIP197_HIA_xDR_PREP_COUNT);

	/* let the CDR know we have pending descriptors */
	writel((cdesc * priv->config.cd_offset),
	       EIP197_HIA_CDR(priv, ring) + EIP197_HIA_xDR_PREP_COUNT);
}

inline int safexcel_rdesc_check_errors(struct safexcel_crypto_priv *priv,
				       void *rdp)
{
	struct safexcel_result_desc *rdesc = rdp;
	struct result_data_desc *result_data = rdp + priv->config.res_offset;

	if (likely((!rdesc->last_seg) || /* Rest only valid if last seg! */
		   ((!rdesc->descriptor_overflow) &&
		    (!rdesc->buffer_overflow) &&
		    (!result_data->error_code))))
		return 0;

	if (rdesc->descriptor_overflow)
		dev_err(priv->dev, "Descriptor overflow detected");

	if (rdesc->buffer_overflow)
		dev_err(priv->dev, "Buffer overflow detected");

	if (result_data->error_code & 0x4066) {
		/* Fatal error (bits 1,2,5,6 & 14) */
		dev_err(priv->dev,
			"result descriptor error (%x)",
			result_data->error_code);

		return -EIO;
	} else if (result_data->error_code &
		   (BIT(7) | BIT(4) | BIT(3) | BIT(0))) {
		/*
		 * Give priority over authentication fails:
		 * Blocksize, length & overflow errors,
		 * something wrong with the input!
		 */
		return -EINVAL;
	} else if (result_data->error_code & BIT(9)) {
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
	struct safexcel_token  *dmmy;
	int ret = 0;

	/* Prepare command descriptor */
	cdesc = safexcel_add_cdesc(priv, ring, true, true, 0, 0, 0, ctxr_dma,
				   &dmmy);
	if (IS_ERR(cdesc))
		return PTR_ERR(cdesc);

	cdesc->control_data.type = EIP197_TYPE_EXTENDED;
	cdesc->control_data.options = 0;
	cdesc->control_data.context_lo &= ~EIP197_CONTEXT_SIZE_MASK;
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
		       (tot_descs * priv->config.rd_offset),
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
				     int ring_id,
				     irq_handler_t handler,
				     irq_handler_t threaded_handler,
				     struct safexcel_ring_irq_data *ring_irq_priv)
{
	int ret, irq, cpu;
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
	} else {
		return -ENXIO;
	}

	ret = devm_request_threaded_irq(dev, irq, handler,
					threaded_handler, IRQF_ONESHOT,
					dev_name(dev), ring_irq_priv);
	if (ret) {
		dev_err(dev, "unable to request IRQ %d\n", irq);
		return ret;
	}

	/* Set affinity */
	cpu = cpumask_local_spread(ring_id, NUMA_NO_NODE);
	irq_set_affinity_hint(irq, get_cpu_mask(cpu));

	return irq;
}

static struct safexcel_alg_template *safexcel_algs[] = {
	&safexcel_alg_ecb_des,
	&safexcel_alg_cbc_des,
	&safexcel_alg_ecb_des3_ede,
	&safexcel_alg_cbc_des3_ede,
	&safexcel_alg_ecb_aes,
	&safexcel_alg_cbc_aes,
	&safexcel_alg_cfb_aes,
	&safexcel_alg_ofb_aes,
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
	&safexcel_alg_xts_aes,
	&safexcel_alg_gcm,
	&safexcel_alg_ccm,
	&safexcel_alg_crc32,
	&safexcel_alg_cbcmac,
	&safexcel_alg_xcbcmac,
	&safexcel_alg_cmac,
	&safexcel_alg_chacha20,
	&safexcel_alg_chachapoly,
	&safexcel_alg_chachapoly_esp,
	&safexcel_alg_sm3,
	&safexcel_alg_hmac_sm3,
	&safexcel_alg_ecb_sm4,
	&safexcel_alg_cbc_sm4,
	&safexcel_alg_ofb_sm4,
	&safexcel_alg_cfb_sm4,
	&safexcel_alg_ctr_sm4,
	&safexcel_alg_authenc_hmac_sha1_cbc_sm4,
	&safexcel_alg_authenc_hmac_sm3_cbc_sm4,
	&safexcel_alg_authenc_hmac_sha1_ctr_sm4,
	&safexcel_alg_authenc_hmac_sm3_ctr_sm4,
	&safexcel_alg_sha3_224,
	&safexcel_alg_sha3_256,
	&safexcel_alg_sha3_384,
	&safexcel_alg_sha3_512,
	&safexcel_alg_hmac_sha3_224,
	&safexcel_alg_hmac_sha3_256,
	&safexcel_alg_hmac_sha3_384,
	&safexcel_alg_hmac_sha3_512,
	&safexcel_alg_authenc_hmac_sha1_cbc_des,
	&safexcel_alg_authenc_hmac_sha256_cbc_des3_ede,
	&safexcel_alg_authenc_hmac_sha224_cbc_des3_ede,
	&safexcel_alg_authenc_hmac_sha512_cbc_des3_ede,
	&safexcel_alg_authenc_hmac_sha384_cbc_des3_ede,
	&safexcel_alg_authenc_hmac_sha256_cbc_des,
	&safexcel_alg_authenc_hmac_sha224_cbc_des,
	&safexcel_alg_authenc_hmac_sha512_cbc_des,
	&safexcel_alg_authenc_hmac_sha384_cbc_des,
	&safexcel_alg_rfc4106_gcm,
	&safexcel_alg_rfc4543_gcm,
	&safexcel_alg_rfc4309_ccm,
};

static int safexcel_register_algorithms(struct safexcel_crypto_priv *priv)
{
	int i, j, ret = 0;

	for (i = 0; i < ARRAY_SIZE(safexcel_algs); i++) {
		safexcel_algs[i]->priv = priv;

		/* Do we have all required base algorithms available? */
		if ((safexcel_algs[i]->algo_mask & priv->hwconfig.algo_flags) !=
		    safexcel_algs[i]->algo_mask)
			/* No, so don't register this ciphersuite */
			continue;

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
		/* Do we have all required base algorithms available? */
		if ((safexcel_algs[j]->algo_mask & priv->hwconfig.algo_flags) !=
		    safexcel_algs[j]->algo_mask)
			/* No, so don't unregister this ciphersuite */
			continue;

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
		/* Do we have all required base algorithms available? */
		if ((safexcel_algs[i]->algo_mask & priv->hwconfig.algo_flags) !=
		    safexcel_algs[i]->algo_mask)
			/* No, so don't unregister this ciphersuite */
			continue;

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
	u32 mask = BIT(priv->hwconfig.hwdataw) - 1;

	priv->config.pes = priv->hwconfig.hwnumpes;
	priv->config.rings = min_t(u32, priv->hwconfig.hwnumrings, max_rings);
	/* Cannot currently support more rings than we have ring AICs! */
	priv->config.rings = min_t(u32, priv->config.rings,
					priv->hwconfig.hwnumraic);

	priv->config.cd_size = EIP197_CD64_FETCH_SIZE;
	priv->config.cd_offset = (priv->config.cd_size + mask) & ~mask;
	priv->config.cdsh_offset = (EIP197_MAX_TOKENS + mask) & ~mask;

	/* res token is behind the descr, but ofs must be rounded to buswdth */
	priv->config.res_offset = (EIP197_RD64_FETCH_SIZE + mask) & ~mask;
	/* now the size of the descr is this 1st part plus the result struct */
	priv->config.rd_size    = priv->config.res_offset +
				  EIP197_RD64_RESULT_SIZE;
	priv->config.rd_offset = (priv->config.rd_size + mask) & ~mask;

	/* convert dwords to bytes */
	priv->config.cd_offset *= sizeof(u32);
	priv->config.cdsh_offset *= sizeof(u32);
	priv->config.rd_offset *= sizeof(u32);
	priv->config.res_offset *= sizeof(u32);
}

static void safexcel_init_register_offsets(struct safexcel_crypto_priv *priv)
{
	struct safexcel_register_offsets *offsets = &priv->offsets;

	if (priv->flags & SAFEXCEL_HW_EIP197) {
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
		offsets->global		= EIP197_GLOBAL_BASE;
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
		offsets->global		= EIP97_GLOBAL_BASE;
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
	u32 peid, version, mask, val, hiaopt, hwopt, peopt;
	int i, ret, hwctg;

	priv->context_pool = dmam_pool_create("safexcel-context", dev,
					      sizeof(struct safexcel_context_record),
					      1, 0);
	if (!priv->context_pool)
		return -ENOMEM;

	/*
	 * First try the EIP97 HIA version regs
	 * For the EIP197, this is guaranteed to NOT return any of the test
	 * values
	 */
	version = readl(priv->base + EIP97_HIA_AIC_BASE + EIP197_HIA_VERSION);

	mask = 0;  /* do not swap */
	if (EIP197_REG_LO16(version) == EIP197_HIA_VERSION_LE) {
		priv->hwconfig.hiaver = EIP197_VERSION_MASK(version);
	} else if (EIP197_REG_HI16(version) == EIP197_HIA_VERSION_BE) {
		/* read back byte-swapped, so complement byte swap bits */
		mask = EIP197_MST_CTRL_BYTE_SWAP_BITS;
		priv->hwconfig.hiaver = EIP197_VERSION_SWAP(version);
	} else {
		/* So it wasn't an EIP97 ... maybe it's an EIP197? */
		version = readl(priv->base + EIP197_HIA_AIC_BASE +
				EIP197_HIA_VERSION);
		if (EIP197_REG_LO16(version) == EIP197_HIA_VERSION_LE) {
			priv->hwconfig.hiaver = EIP197_VERSION_MASK(version);
			priv->flags |= SAFEXCEL_HW_EIP197;
		} else if (EIP197_REG_HI16(version) ==
			   EIP197_HIA_VERSION_BE) {
			/* read back byte-swapped, so complement swap bits */
			mask = EIP197_MST_CTRL_BYTE_SWAP_BITS;
			priv->hwconfig.hiaver = EIP197_VERSION_SWAP(version);
			priv->flags |= SAFEXCEL_HW_EIP197;
		} else {
			return -ENODEV;
		}
	}

	/* Now initialize the reg offsets based on the probing info so far */
	safexcel_init_register_offsets(priv);

	/*
	 * If the version was read byte-swapped, we need to flip the device
	 * swapping Keep in mind here, though, that what we write will also be
	 * byte-swapped ...
	 */
	if (mask) {
		val = readl(EIP197_HIA_AIC(priv) + EIP197_HIA_MST_CTRL);
		val = val ^ (mask >> 24); /* toggle byte swap bits */
		writel(val, EIP197_HIA_AIC(priv) + EIP197_HIA_MST_CTRL);
	}

	/*
	 * We're not done probing yet! We may fall through to here if no HIA
	 * was found at all. So, with the endianness presumably correct now and
	 * the offsets setup, *really* probe for the EIP97/EIP197.
	 */
	version = readl(EIP197_GLOBAL(priv) + EIP197_VERSION);
	if (((priv->flags & SAFEXCEL_HW_EIP197) &&
	     (EIP197_REG_LO16(version) != EIP197_VERSION_LE) &&
	     (EIP197_REG_LO16(version) != EIP196_VERSION_LE)) ||
	    ((!(priv->flags & SAFEXCEL_HW_EIP197) &&
	     (EIP197_REG_LO16(version) != EIP97_VERSION_LE)))) {
		/*
		 * We did not find the device that matched our initial probing
		 * (or our initial probing failed) Report appropriate error.
		 */
		dev_err(priv->dev, "Probing for EIP97/EIP19x failed - no such device (read %08x)\n",
			version);
		return -ENODEV;
	}

	priv->hwconfig.hwver = EIP197_VERSION_MASK(version);
	hwctg = version >> 28;
	peid = version & 255;

	/* Detect EIP206 processing pipe */
	version = readl(EIP197_PE(priv) + + EIP197_PE_VERSION(0));
	if (EIP197_REG_LO16(version) != EIP206_VERSION_LE) {
		dev_err(priv->dev, "EIP%d: EIP206 not detected\n", peid);
		return -ENODEV;
	}
	priv->hwconfig.ppver = EIP197_VERSION_MASK(version);

	/* Detect EIP96 packet engine and version */
	version = readl(EIP197_PE(priv) + EIP197_PE_EIP96_VERSION(0));
	if (EIP197_REG_LO16(version) != EIP96_VERSION_LE) {
		dev_err(dev, "EIP%d: EIP96 not detected.\n", peid);
		return -ENODEV;
	}
	priv->hwconfig.pever = EIP197_VERSION_MASK(version);

	hwopt = readl(EIP197_GLOBAL(priv) + EIP197_OPTIONS);
	hiaopt = readl(EIP197_HIA_AIC(priv) + EIP197_HIA_OPTIONS);

	priv->hwconfig.icever = 0;
	priv->hwconfig.ocever = 0;
	priv->hwconfig.psever = 0;
	if (priv->flags & SAFEXCEL_HW_EIP197) {
		/* EIP197 */
		peopt = readl(EIP197_PE(priv) + EIP197_PE_OPTIONS(0));

		priv->hwconfig.hwdataw  = (hiaopt >> EIP197_HWDATAW_OFFSET) &
					  EIP197_HWDATAW_MASK;
		priv->hwconfig.hwcfsize = ((hiaopt >> EIP197_CFSIZE_OFFSET) &
					   EIP197_CFSIZE_MASK) +
					  EIP197_CFSIZE_ADJUST;
		priv->hwconfig.hwrfsize = ((hiaopt >> EIP197_RFSIZE_OFFSET) &
					   EIP197_RFSIZE_MASK) +
					  EIP197_RFSIZE_ADJUST;
		priv->hwconfig.hwnumpes	= (hiaopt >> EIP197_N_PES_OFFSET) &
					  EIP197_N_PES_MASK;
		priv->hwconfig.hwnumrings = (hiaopt >> EIP197_N_RINGS_OFFSET) &
					    EIP197_N_RINGS_MASK;
		if (hiaopt & EIP197_HIA_OPT_HAS_PE_ARB)
			priv->flags |= EIP197_PE_ARB;
		if (EIP206_OPT_ICE_TYPE(peopt) == 1) {
			priv->flags |= EIP197_ICE;
			/* Detect ICE EIP207 class. engine and version */
			version = readl(EIP197_PE(priv) +
				  EIP197_PE_ICE_VERSION(0));
			if (EIP197_REG_LO16(version) != EIP207_VERSION_LE) {
				dev_err(dev, "EIP%d: ICE EIP207 not detected.\n",
					peid);
				return -ENODEV;
			}
			priv->hwconfig.icever = EIP197_VERSION_MASK(version);
		}
		if (EIP206_OPT_OCE_TYPE(peopt) == 1) {
			priv->flags |= EIP197_OCE;
			/* Detect EIP96PP packet stream editor and version */
			version = readl(EIP197_PE(priv) + EIP197_PE_PSE_VERSION(0));
			if (EIP197_REG_LO16(version) != EIP96_VERSION_LE) {
				dev_err(dev, "EIP%d: EIP96PP not detected.\n", peid);
				return -ENODEV;
			}
			priv->hwconfig.psever = EIP197_VERSION_MASK(version);
			/* Detect OCE EIP207 class. engine and version */
			version = readl(EIP197_PE(priv) +
				  EIP197_PE_ICE_VERSION(0));
			if (EIP197_REG_LO16(version) != EIP207_VERSION_LE) {
				dev_err(dev, "EIP%d: OCE EIP207 not detected.\n",
					peid);
				return -ENODEV;
			}
			priv->hwconfig.ocever = EIP197_VERSION_MASK(version);
		}
		/* If not a full TRC, then assume simple TRC */
		if (!(hwopt & EIP197_OPT_HAS_TRC))
			priv->flags |= EIP197_SIMPLE_TRC;
		/* EIP197 always has SOME form of TRC */
		priv->flags |= EIP197_TRC_CACHE;
	} else {
		/* EIP97 */
		priv->hwconfig.hwdataw  = (hiaopt >> EIP197_HWDATAW_OFFSET) &
					  EIP97_HWDATAW_MASK;
		priv->hwconfig.hwcfsize = (hiaopt >> EIP97_CFSIZE_OFFSET) &
					  EIP97_CFSIZE_MASK;
		priv->hwconfig.hwrfsize = (hiaopt >> EIP97_RFSIZE_OFFSET) &
					  EIP97_RFSIZE_MASK;
		priv->hwconfig.hwnumpes	= 1; /* by definition */
		priv->hwconfig.hwnumrings = (hiaopt >> EIP197_N_RINGS_OFFSET) &
					    EIP197_N_RINGS_MASK;
	}

	/* Scan for ring AIC's */
	for (i = 0; i < EIP197_MAX_RING_AIC; i++) {
		version = readl(EIP197_HIA_AIC_R(priv) +
				EIP197_HIA_AIC_R_VERSION(i));
		if (EIP197_REG_LO16(version) != EIP201_VERSION_LE)
			break;
	}
	priv->hwconfig.hwnumraic = i;
	/* Low-end EIP196 may not have any ring AIC's ... */
	if (!priv->hwconfig.hwnumraic) {
		dev_err(priv->dev, "No ring interrupt controller present!\n");
		return -ENODEV;
	}

	/* Get supported algorithms from EIP96 transform engine */
	priv->hwconfig.algo_flags = readl(EIP197_PE(priv) +
				    EIP197_PE_EIP96_OPTIONS(0));

	/* Print single info line describing what we just detected */
	dev_info(priv->dev, "EIP%d:%x(%d,%d,%d,%d)-HIA:%x(%d,%d,%d),PE:%x/%x(alg:%08x)/%x/%x/%x\n",
		 peid, priv->hwconfig.hwver, hwctg, priv->hwconfig.hwnumpes,
		 priv->hwconfig.hwnumrings, priv->hwconfig.hwnumraic,
		 priv->hwconfig.hiaver, priv->hwconfig.hwdataw,
		 priv->hwconfig.hwcfsize, priv->hwconfig.hwrfsize,
		 priv->hwconfig.ppver, priv->hwconfig.pever,
		 priv->hwconfig.algo_flags, priv->hwconfig.icever,
		 priv->hwconfig.ocever, priv->hwconfig.psever);

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
			sizeof(*priv->ring[i].rdr_req),
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
						i,
						safexcel_irq_ring,
						safexcel_irq_ring_thread,
						ring_irq);
		if (irq < 0) {
			dev_err(dev, "Failed to get IRQ ID for ring %d\n", i);
			return irq;
		}

		priv->ring[i].irq = irq;
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

	clk_disable_unprepare(priv->reg_clk);
	clk_disable_unprepare(priv->clk);

	for (i = 0; i < priv->config.rings; i++) {
		irq_set_affinity_hint(priv->ring[i].irq, NULL);
		destroy_workqueue(priv->ring[i].workqueue);
	}

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

static void safexcel_pci_remove(struct pci_dev *pdev)
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

static int __init safexcel_init(void)
{
	int ret;

	/* Register PCI driver */
	ret = pci_register_driver(&safexcel_pci_driver);

	/* Register platform driver */
	if (IS_ENABLED(CONFIG_OF) && !ret) {
		ret = platform_driver_register(&crypto_safexcel);
		if (ret)
			pci_unregister_driver(&safexcel_pci_driver);
	}

	return ret;
}

static void __exit safexcel_exit(void)
{
	/* Unregister platform driver */
	if (IS_ENABLED(CONFIG_OF))
		platform_driver_unregister(&crypto_safexcel);

	/* Unregister PCI driver if successfully registered before */
	pci_unregister_driver(&safexcel_pci_driver);
}

module_init(safexcel_init);
module_exit(safexcel_exit);

MODULE_AUTHOR("Antoine Tenart <antoine.tenart@free-electrons.com>");
MODULE_AUTHOR("Ofer Heifetz <oferh@marvell.com>");
MODULE_AUTHOR("Igal Liberman <igall@marvell.com>");
MODULE_DESCRIPTION("Support for SafeXcel cryptographic engines: EIP97 & EIP197");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CRYPTO_INTERNAL);
