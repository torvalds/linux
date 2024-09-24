// SPDX-License-Identifier: GPL-2.0-only
/*
 * STM32 DMA3 controller driver
 *
 * Copyright (C) STMicroelectronics 2024
 * Author(s): Amelie Delaunay <amelie.delaunay@foss.st.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "../virt-dma.h"

#define STM32_DMA3_SECCFGR		0x00
#define STM32_DMA3_PRIVCFGR		0x04
#define STM32_DMA3_RCFGLOCKR		0x08
#define STM32_DMA3_MISR			0x0c
#define STM32_DMA3_SMISR		0x10

#define STM32_DMA3_CLBAR(x)		(0x50 + 0x80 * (x))
#define STM32_DMA3_CCIDCFGR(x)		(0x54 + 0x80 * (x))
#define STM32_DMA3_CSEMCR(x)		(0x58 + 0x80 * (x))
#define STM32_DMA3_CFCR(x)		(0x5c + 0x80 * (x))
#define STM32_DMA3_CSR(x)		(0x60 + 0x80 * (x))
#define STM32_DMA3_CCR(x)		(0x64 + 0x80 * (x))
#define STM32_DMA3_CTR1(x)		(0x90 + 0x80 * (x))
#define STM32_DMA3_CTR2(x)		(0x94 + 0x80 * (x))
#define STM32_DMA3_CBR1(x)		(0x98 + 0x80 * (x))
#define STM32_DMA3_CSAR(x)		(0x9c + 0x80 * (x))
#define STM32_DMA3_CDAR(x)		(0xa0 + 0x80 * (x))
#define STM32_DMA3_CLLR(x)		(0xcc + 0x80 * (x))

#define STM32_DMA3_HWCFGR13		0xfc0 /* G_PER_CTRL(X) x=8..15 */
#define STM32_DMA3_HWCFGR12		0xfc4 /* G_PER_CTRL(X) x=0..7 */
#define STM32_DMA3_HWCFGR4		0xfe4 /* G_FIFO_SIZE(X) x=8..15 */
#define STM32_DMA3_HWCFGR3		0xfe8 /* G_FIFO_SIZE(X) x=0..7 */
#define STM32_DMA3_HWCFGR2		0xfec /* G_MAX_REQ_ID */
#define STM32_DMA3_HWCFGR1		0xff0 /* G_MASTER_PORTS, G_NUM_CHANNELS, G_Mx_DATA_WIDTH */
#define STM32_DMA3_VERR			0xff4

/* SECCFGR DMA secure configuration register */
#define SECCFGR_SEC(x)			BIT(x)

/* MISR DMA non-secure/secure masked interrupt status register */
#define MISR_MIS(x)			BIT(x)

/* CxLBAR DMA channel x linked_list base address register */
#define CLBAR_LBA			GENMASK(31, 16)

/* CxCIDCFGR DMA channel x CID register */
#define CCIDCFGR_CFEN			BIT(0)
#define CCIDCFGR_SEM_EN			BIT(1)
#define CCIDCFGR_SCID			GENMASK(5, 4)
#define CCIDCFGR_SEM_WLIST_CID0		BIT(16)
#define CCIDCFGR_SEM_WLIST_CID1		BIT(17)
#define CCIDCFGR_SEM_WLIST_CID2		BIT(18)

enum ccidcfgr_cid {
	CCIDCFGR_CID0,
	CCIDCFGR_CID1,
	CCIDCFGR_CID2,
};

/* CxSEMCR DMA channel x semaphore control register */
#define CSEMCR_SEM_MUTEX		BIT(0)
#define CSEMCR_SEM_CCID			GENMASK(5, 4)

/* CxFCR DMA channel x flag clear register */
#define CFCR_TCF			BIT(8)
#define CFCR_HTF			BIT(9)
#define CFCR_DTEF			BIT(10)
#define CFCR_ULEF			BIT(11)
#define CFCR_USEF			BIT(12)
#define CFCR_SUSPF			BIT(13)

/* CxSR DMA channel x status register */
#define CSR_IDLEF			BIT(0)
#define CSR_TCF				BIT(8)
#define CSR_HTF				BIT(9)
#define CSR_DTEF			BIT(10)
#define CSR_ULEF			BIT(11)
#define CSR_USEF			BIT(12)
#define CSR_SUSPF			BIT(13)
#define CSR_ALL_F			GENMASK(13, 8)
#define CSR_FIFOL			GENMASK(24, 16)

/* CxCR DMA channel x control register */
#define CCR_EN				BIT(0)
#define CCR_RESET			BIT(1)
#define CCR_SUSP			BIT(2)
#define CCR_TCIE			BIT(8)
#define CCR_HTIE			BIT(9)
#define CCR_DTEIE			BIT(10)
#define CCR_ULEIE			BIT(11)
#define CCR_USEIE			BIT(12)
#define CCR_SUSPIE			BIT(13)
#define CCR_ALLIE			GENMASK(13, 8)
#define CCR_LSM				BIT(16)
#define CCR_LAP				BIT(17)
#define CCR_PRIO			GENMASK(23, 22)

enum ccr_prio {
	CCR_PRIO_LOW,
	CCR_PRIO_MID,
	CCR_PRIO_HIGH,
	CCR_PRIO_VERY_HIGH,
};

/* CxTR1 DMA channel x transfer register 1 */
#define CTR1_SINC			BIT(3)
#define CTR1_SBL_1			GENMASK(9, 4)
#define CTR1_DINC			BIT(19)
#define CTR1_DBL_1			GENMASK(25, 20)
#define CTR1_SDW_LOG2			GENMASK(1, 0)
#define CTR1_PAM			GENMASK(12, 11)
#define CTR1_SAP			BIT(14)
#define CTR1_DDW_LOG2			GENMASK(17, 16)
#define CTR1_DAP			BIT(30)

enum ctr1_dw {
	CTR1_DW_BYTE,
	CTR1_DW_HWORD,
	CTR1_DW_WORD,
	CTR1_DW_DWORD, /* Depends on HWCFGR1.G_M0_DATA_WIDTH_ENC and .G_M1_DATA_WIDTH_ENC */
};

enum ctr1_pam {
	CTR1_PAM_0S_LT,		/* if DDW > SDW, padded with 0s else left-truncated */
	CTR1_PAM_SE_RT,		/* if DDW > SDW, sign extended else right-truncated */
	CTR1_PAM_PACK_UNPACK,	/* FIFO queued */
};

/* CxTR2 DMA channel x transfer register 2 */
#define CTR2_REQSEL			GENMASK(7, 0)
#define CTR2_SWREQ			BIT(9)
#define CTR2_DREQ			BIT(10)
#define CTR2_BREQ			BIT(11)
#define CTR2_PFREQ			BIT(12)
#define CTR2_TCEM			GENMASK(31, 30)

enum ctr2_tcem {
	CTR2_TCEM_BLOCK,
	CTR2_TCEM_REPEAT_BLOCK,
	CTR2_TCEM_LLI,
	CTR2_TCEM_CHANNEL,
};

/* CxBR1 DMA channel x block register 1 */
#define CBR1_BNDT			GENMASK(15, 0)

/* CxLLR DMA channel x linked-list address register */
#define CLLR_LA				GENMASK(15, 2)
#define CLLR_ULL			BIT(16)
#define CLLR_UDA			BIT(27)
#define CLLR_USA			BIT(28)
#define CLLR_UB1			BIT(29)
#define CLLR_UT2			BIT(30)
#define CLLR_UT1			BIT(31)

/* HWCFGR13 DMA hardware configuration register 13 x=8..15 */
/* HWCFGR12 DMA hardware configuration register 12 x=0..7 */
#define G_PER_CTRL(x)			(ULL(0x1) << (4 * (x)))

/* HWCFGR4 DMA hardware configuration register 4 x=8..15 */
/* HWCFGR3 DMA hardware configuration register 3 x=0..7 */
#define G_FIFO_SIZE(x)			(ULL(0x7) << (4 * (x)))

#define get_chan_hwcfg(x, mask, reg)	(((reg) & (mask)) >> (4 * (x)))

/* HWCFGR2 DMA hardware configuration register 2 */
#define G_MAX_REQ_ID			GENMASK(7, 0)

/* HWCFGR1 DMA hardware configuration register 1 */
#define G_MASTER_PORTS			GENMASK(2, 0)
#define G_NUM_CHANNELS			GENMASK(12, 8)
#define G_M0_DATA_WIDTH_ENC		GENMASK(25, 24)
#define G_M1_DATA_WIDTH_ENC		GENMASK(29, 28)

enum stm32_dma3_master_ports {
	AXI64,		/* 1x AXI: 64-bit port 0 */
	AHB32,		/* 1x AHB: 32-bit port 0 */
	AHB32_AHB32,	/* 2x AHB: 32-bit port 0 and 32-bit port 1 */
	AXI64_AHB32,	/* 1x AXI 64-bit port 0 and 1x AHB 32-bit port 1 */
	AXI64_AXI64,	/* 2x AXI: 64-bit port 0 and 64-bit port 1 */
	AXI128_AHB32,	/* 1x AXI 128-bit port 0 and 1x AHB 32-bit port 1 */
};

enum stm32_dma3_port_data_width {
	DW_32,		/* 32-bit, for AHB */
	DW_64,		/* 64-bit, for AXI */
	DW_128,		/* 128-bit, for AXI */
	DW_INVALID,
};

/* VERR DMA version register */
#define VERR_MINREV			GENMASK(3, 0)
#define VERR_MAJREV			GENMASK(7, 4)

/* Device tree */
/* struct stm32_dma3_dt_conf */
/* .ch_conf */
#define STM32_DMA3_DT_PRIO		GENMASK(1, 0) /* CCR_PRIO */
#define STM32_DMA3_DT_FIFO		GENMASK(7, 4)
/* .tr_conf */
#define STM32_DMA3_DT_SINC		BIT(0) /* CTR1_SINC */
#define STM32_DMA3_DT_SAP		BIT(1) /* CTR1_SAP */
#define STM32_DMA3_DT_DINC		BIT(4) /* CTR1_DINC */
#define STM32_DMA3_DT_DAP		BIT(5) /* CTR1_DAP */
#define STM32_DMA3_DT_BREQ		BIT(8) /* CTR2_BREQ */
#define STM32_DMA3_DT_PFREQ		BIT(9) /* CTR2_PFREQ */
#define STM32_DMA3_DT_TCEM		GENMASK(13, 12) /* CTR2_TCEM */

/* struct stm32_dma3_chan .config_set bitfield */
#define STM32_DMA3_CFG_SET_DT		BIT(0)
#define STM32_DMA3_CFG_SET_DMA		BIT(1)
#define STM32_DMA3_CFG_SET_BOTH		(STM32_DMA3_CFG_SET_DT | STM32_DMA3_CFG_SET_DMA)

#define STM32_DMA3_MAX_BLOCK_SIZE	ALIGN_DOWN(CBR1_BNDT, 64)
#define port_is_ahb(maxdw)		({ typeof(maxdw) (_maxdw) = (maxdw); \
					   ((_maxdw) != DW_INVALID) && ((_maxdw) == DW_32); })
#define port_is_axi(maxdw)		({ typeof(maxdw) (_maxdw) = (maxdw); \
					   ((_maxdw) != DW_INVALID) && ((_maxdw) != DW_32); })
#define get_chan_max_dw(maxdw, maxburst)((port_is_ahb(maxdw) ||			     \
					  (maxburst) < DMA_SLAVE_BUSWIDTH_8_BYTES) ? \
					 DMA_SLAVE_BUSWIDTH_4_BYTES : DMA_SLAVE_BUSWIDTH_8_BYTES)

/* Static linked-list data structure (depends on update bits UT1/UT2/UB1/USA/UDA/ULL) */
struct stm32_dma3_hwdesc {
	u32 ctr1;
	u32 ctr2;
	u32 cbr1;
	u32 csar;
	u32 cdar;
	u32 cllr;
} __packed __aligned(32);

/*
 * CLLR_LA / sizeof(struct stm32_dma3_hwdesc) represents the number of hdwdesc that can be addressed
 * by the pointer to the next linked-list data structure. The __aligned forces the 32-byte
 * alignment. So use hardcoded 32. Multiplied by the max block size of each item, it represents
 * the sg size limitation.
 */
#define STM32_DMA3_MAX_SEG_SIZE		((CLLR_LA / 32) * STM32_DMA3_MAX_BLOCK_SIZE)

/*
 * Linked-list items
 */
struct stm32_dma3_lli {
	struct stm32_dma3_hwdesc *hwdesc;
	dma_addr_t hwdesc_addr;
};

struct stm32_dma3_swdesc {
	struct virt_dma_desc vdesc;
	u32 ccr;
	bool cyclic;
	u32 lli_size;
	struct stm32_dma3_lli lli[] __counted_by(lli_size);
};

struct stm32_dma3_dt_conf {
	u32 ch_id;
	u32 req_line;
	u32 ch_conf;
	u32 tr_conf;
};

struct stm32_dma3_chan {
	struct virt_dma_chan vchan;
	u32 id;
	int irq;
	u32 fifo_size;
	u32 max_burst;
	bool semaphore_mode;
	struct stm32_dma3_dt_conf dt_config;
	struct dma_slave_config dma_config;
	u8 config_set;
	struct dma_pool *lli_pool;
	struct stm32_dma3_swdesc *swdesc;
	enum ctr2_tcem tcem;
	u32 dma_status;
};

struct stm32_dma3_ddata {
	struct dma_device dma_dev;
	void __iomem *base;
	struct clk *clk;
	struct stm32_dma3_chan *chans;
	u32 dma_channels;
	u32 dma_requests;
	enum stm32_dma3_port_data_width ports_max_dw[2];
};

static inline struct stm32_dma3_ddata *to_stm32_dma3_ddata(struct stm32_dma3_chan *chan)
{
	return container_of(chan->vchan.chan.device, struct stm32_dma3_ddata, dma_dev);
}

static inline struct stm32_dma3_chan *to_stm32_dma3_chan(struct dma_chan *c)
{
	return container_of(c, struct stm32_dma3_chan, vchan.chan);
}

static inline struct stm32_dma3_swdesc *to_stm32_dma3_swdesc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct stm32_dma3_swdesc, vdesc);
}

static struct device *chan2dev(struct stm32_dma3_chan *chan)
{
	return &chan->vchan.chan.dev->device;
}

static void stm32_dma3_chan_dump_reg(struct stm32_dma3_chan *chan)
{
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	struct device *dev = chan2dev(chan);
	u32 id = chan->id, offset;

	offset = STM32_DMA3_SECCFGR;
	dev_dbg(dev, "SECCFGR(0x%03x): %08x\n", offset, readl_relaxed(ddata->base + offset));
	offset = STM32_DMA3_PRIVCFGR;
	dev_dbg(dev, "PRIVCFGR(0x%03x): %08x\n", offset, readl_relaxed(ddata->base + offset));
	offset = STM32_DMA3_CCIDCFGR(id);
	dev_dbg(dev, "C%dCIDCFGR(0x%03x): %08x\n", id, offset, readl_relaxed(ddata->base + offset));
	offset = STM32_DMA3_CSEMCR(id);
	dev_dbg(dev, "C%dSEMCR(0x%03x): %08x\n", id, offset, readl_relaxed(ddata->base + offset));
	offset = STM32_DMA3_CSR(id);
	dev_dbg(dev, "C%dSR(0x%03x): %08x\n", id, offset, readl_relaxed(ddata->base + offset));
	offset = STM32_DMA3_CCR(id);
	dev_dbg(dev, "C%dCR(0x%03x): %08x\n", id, offset, readl_relaxed(ddata->base + offset));
	offset = STM32_DMA3_CTR1(id);
	dev_dbg(dev, "C%dTR1(0x%03x): %08x\n", id, offset, readl_relaxed(ddata->base + offset));
	offset = STM32_DMA3_CTR2(id);
	dev_dbg(dev, "C%dTR2(0x%03x): %08x\n", id, offset, readl_relaxed(ddata->base + offset));
	offset = STM32_DMA3_CBR1(id);
	dev_dbg(dev, "C%dBR1(0x%03x): %08x\n", id, offset, readl_relaxed(ddata->base + offset));
	offset = STM32_DMA3_CSAR(id);
	dev_dbg(dev, "C%dSAR(0x%03x): %08x\n", id, offset, readl_relaxed(ddata->base + offset));
	offset = STM32_DMA3_CDAR(id);
	dev_dbg(dev, "C%dDAR(0x%03x): %08x\n", id, offset, readl_relaxed(ddata->base + offset));
	offset = STM32_DMA3_CLLR(id);
	dev_dbg(dev, "C%dLLR(0x%03x): %08x\n", id, offset, readl_relaxed(ddata->base + offset));
	offset = STM32_DMA3_CLBAR(id);
	dev_dbg(dev, "C%dLBAR(0x%03x): %08x\n", id, offset, readl_relaxed(ddata->base + offset));
}

static void stm32_dma3_chan_dump_hwdesc(struct stm32_dma3_chan *chan,
					struct stm32_dma3_swdesc *swdesc)
{
	struct stm32_dma3_hwdesc *hwdesc;
	int i;

	for (i = 0; i < swdesc->lli_size; i++) {
		hwdesc = swdesc->lli[i].hwdesc;
		if (i)
			dev_dbg(chan2dev(chan), "V\n");
		dev_dbg(chan2dev(chan), "[%d]@%pad\n", i, &swdesc->lli[i].hwdesc_addr);
		dev_dbg(chan2dev(chan), "| C%dTR1: %08x\n", chan->id, hwdesc->ctr1);
		dev_dbg(chan2dev(chan), "| C%dTR2: %08x\n", chan->id, hwdesc->ctr2);
		dev_dbg(chan2dev(chan), "| C%dBR1: %08x\n", chan->id, hwdesc->cbr1);
		dev_dbg(chan2dev(chan), "| C%dSAR: %08x\n", chan->id, hwdesc->csar);
		dev_dbg(chan2dev(chan), "| C%dDAR: %08x\n", chan->id, hwdesc->cdar);
		dev_dbg(chan2dev(chan), "| C%dLLR: %08x\n", chan->id, hwdesc->cllr);
	}

	if (swdesc->cyclic) {
		dev_dbg(chan2dev(chan), "|\n");
		dev_dbg(chan2dev(chan), "-->[0]@%pad\n", &swdesc->lli[0].hwdesc_addr);
	} else {
		dev_dbg(chan2dev(chan), "X\n");
	}
}

static struct stm32_dma3_swdesc *stm32_dma3_chan_desc_alloc(struct stm32_dma3_chan *chan, u32 count)
{
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	struct stm32_dma3_swdesc *swdesc;
	int i;

	/*
	 * If the memory to be allocated for the number of hwdesc (6 u32 members but 32-bytes
	 * aligned) is greater than the maximum address of CLLR_LA, then the last items can't be
	 * addressed, so abort the allocation.
	 */
	if ((count * 32) > CLLR_LA) {
		dev_err(chan2dev(chan), "Transfer is too big (> %luB)\n", STM32_DMA3_MAX_SEG_SIZE);
		return NULL;
	}

	swdesc = kzalloc(struct_size(swdesc, lli, count), GFP_NOWAIT);
	if (!swdesc)
		return NULL;
	swdesc->lli_size = count;

	for (i = 0; i < count; i++) {
		swdesc->lli[i].hwdesc = dma_pool_zalloc(chan->lli_pool, GFP_NOWAIT,
							&swdesc->lli[i].hwdesc_addr);
		if (!swdesc->lli[i].hwdesc)
			goto err_pool_free;
	}
	swdesc->ccr = 0;

	/* Set LL base address */
	writel_relaxed(swdesc->lli[0].hwdesc_addr & CLBAR_LBA,
		       ddata->base + STM32_DMA3_CLBAR(chan->id));

	/* Set LL allocated port */
	swdesc->ccr &= ~CCR_LAP;

	return swdesc;

err_pool_free:
	dev_err(chan2dev(chan), "Failed to alloc descriptors\n");
	while (--i >= 0)
		dma_pool_free(chan->lli_pool, swdesc->lli[i].hwdesc, swdesc->lli[i].hwdesc_addr);
	kfree(swdesc);

	return NULL;
}

static void stm32_dma3_chan_desc_free(struct stm32_dma3_chan *chan,
				      struct stm32_dma3_swdesc *swdesc)
{
	int i;

	for (i = 0; i < swdesc->lli_size; i++)
		dma_pool_free(chan->lli_pool, swdesc->lli[i].hwdesc, swdesc->lli[i].hwdesc_addr);

	kfree(swdesc);
}

static void stm32_dma3_chan_vdesc_free(struct virt_dma_desc *vdesc)
{
	struct stm32_dma3_swdesc *swdesc = to_stm32_dma3_swdesc(vdesc);
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(vdesc->tx.chan);

	stm32_dma3_chan_desc_free(chan, swdesc);
}

static void stm32_dma3_check_user_setting(struct stm32_dma3_chan *chan)
{
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	struct device *dev = chan2dev(chan);
	u32 ctr1 = readl_relaxed(ddata->base + STM32_DMA3_CTR1(chan->id));
	u32 cbr1 = readl_relaxed(ddata->base + STM32_DMA3_CBR1(chan->id));
	u32 csar = readl_relaxed(ddata->base + STM32_DMA3_CSAR(chan->id));
	u32 cdar = readl_relaxed(ddata->base + STM32_DMA3_CDAR(chan->id));
	u32 cllr = readl_relaxed(ddata->base + STM32_DMA3_CLLR(chan->id));
	u32 bndt = FIELD_GET(CBR1_BNDT, cbr1);
	u32 sdw = 1 << FIELD_GET(CTR1_SDW_LOG2, ctr1);
	u32 ddw = 1 << FIELD_GET(CTR1_DDW_LOG2, ctr1);
	u32 sap = FIELD_GET(CTR1_SAP, ctr1);
	u32 dap = FIELD_GET(CTR1_DAP, ctr1);

	if (!bndt && !FIELD_GET(CLLR_UB1, cllr))
		dev_err(dev, "null source block size and no update of this value\n");
	if (bndt % sdw)
		dev_err(dev, "source block size not multiple of src data width\n");
	if (FIELD_GET(CTR1_PAM, ctr1) == CTR1_PAM_PACK_UNPACK && bndt % ddw)
		dev_err(dev, "(un)packing mode w/ src block size not multiple of dst data width\n");
	if (csar % sdw)
		dev_err(dev, "unaligned source address not multiple of src data width\n");
	if (cdar % ddw)
		dev_err(dev, "unaligned destination address not multiple of dst data width\n");
	if (sdw == DMA_SLAVE_BUSWIDTH_8_BYTES && port_is_ahb(ddata->ports_max_dw[sap]))
		dev_err(dev, "double-word source data width not supported on port %u\n", sap);
	if (ddw == DMA_SLAVE_BUSWIDTH_8_BYTES && port_is_ahb(ddata->ports_max_dw[dap]))
		dev_err(dev, "double-word destination data width not supported on port %u\n", dap);
}

static void stm32_dma3_chan_prep_hwdesc(struct stm32_dma3_chan *chan,
					struct stm32_dma3_swdesc *swdesc,
					u32 curr, dma_addr_t src, dma_addr_t dst, u32 len,
					u32 ctr1, u32 ctr2, bool is_last, bool is_cyclic)
{
	struct stm32_dma3_hwdesc *hwdesc;
	dma_addr_t next_lli;
	u32 next = curr + 1;

	hwdesc = swdesc->lli[curr].hwdesc;
	hwdesc->ctr1 = ctr1;
	hwdesc->ctr2 = ctr2;
	hwdesc->cbr1 = FIELD_PREP(CBR1_BNDT, len);
	hwdesc->csar = src;
	hwdesc->cdar = dst;

	if (is_last) {
		if (is_cyclic)
			next_lli = swdesc->lli[0].hwdesc_addr;
		else
			next_lli = 0;
	} else {
		next_lli = swdesc->lli[next].hwdesc_addr;
	}

	hwdesc->cllr = 0;
	if (next_lli) {
		hwdesc->cllr |= CLLR_UT1 | CLLR_UT2 | CLLR_UB1;
		hwdesc->cllr |= CLLR_USA | CLLR_UDA | CLLR_ULL;
		hwdesc->cllr |= (next_lli & CLLR_LA);
	}

	/*
	 * Make sure to flush the CPU's write buffers so that the descriptors are ready to be read
	 * by DMA3. By explicitly using a write memory barrier here, instead of doing it with writel
	 * to enable the channel, we avoid an unnecessary barrier in the case where the descriptors
	 * are reused (DMA_CTRL_REUSE).
	 */
	if (is_last)
		dma_wmb();
}

static enum dma_slave_buswidth stm32_dma3_get_max_dw(u32 chan_max_burst,
						     enum stm32_dma3_port_data_width port_max_dw,
						     u32 len, dma_addr_t addr)
{
	enum dma_slave_buswidth max_dw = get_chan_max_dw(port_max_dw, chan_max_burst);

	/* len and addr must be a multiple of dw */
	return 1 << __ffs(len | addr | max_dw);
}

static u32 stm32_dma3_get_max_burst(u32 len, enum dma_slave_buswidth dw, u32 chan_max_burst)
{
	u32 max_burst = chan_max_burst ? chan_max_burst / dw : 1;

	/* len is a multiple of dw, so if len is < chan_max_burst, shorten burst */
	if (len < chan_max_burst)
		max_burst = len / dw;

	/*
	 * HW doesn't modify the burst if burst size <= half of the fifo size.
	 * If len is not a multiple of burst size, last burst is shortened by HW.
	 */
	return max_burst;
}

static int stm32_dma3_chan_prep_hw(struct stm32_dma3_chan *chan, enum dma_transfer_direction dir,
				   u32 *ccr, u32 *ctr1, u32 *ctr2,
				   dma_addr_t src_addr, dma_addr_t dst_addr, u32 len)
{
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	struct dma_device dma_device = ddata->dma_dev;
	u32 sdw, ddw, sbl_max, dbl_max, tcem, init_dw, init_bl_max;
	u32 _ctr1 = 0, _ctr2 = 0;
	u32 ch_conf = chan->dt_config.ch_conf;
	u32 tr_conf = chan->dt_config.tr_conf;
	u32 sap = FIELD_GET(STM32_DMA3_DT_SAP, tr_conf), sap_max_dw;
	u32 dap = FIELD_GET(STM32_DMA3_DT_DAP, tr_conf), dap_max_dw;

	dev_dbg(chan2dev(chan), "%s from %pad to %pad\n",
		dmaengine_get_direction_text(dir), &src_addr, &dst_addr);

	sdw = chan->dma_config.src_addr_width ? : get_chan_max_dw(sap, chan->max_burst);
	ddw = chan->dma_config.dst_addr_width ? : get_chan_max_dw(dap, chan->max_burst);
	sbl_max = chan->dma_config.src_maxburst ? : 1;
	dbl_max = chan->dma_config.dst_maxburst ? : 1;

	/* Following conditions would raise User Setting Error interrupt */
	if (!(dma_device.src_addr_widths & BIT(sdw)) || !(dma_device.dst_addr_widths & BIT(ddw))) {
		dev_err(chan2dev(chan), "Bus width (src=%u, dst=%u) not supported\n", sdw, ddw);
		return -EINVAL;
	}

	if (ddata->ports_max_dw[1] == DW_INVALID && (sap || dap)) {
		dev_err(chan2dev(chan), "Only one master port, port 1 is not supported\n");
		return -EINVAL;
	}

	sap_max_dw = ddata->ports_max_dw[sap];
	dap_max_dw = ddata->ports_max_dw[dap];
	if ((port_is_ahb(sap_max_dw) && sdw == DMA_SLAVE_BUSWIDTH_8_BYTES) ||
	    (port_is_ahb(dap_max_dw) && ddw == DMA_SLAVE_BUSWIDTH_8_BYTES)) {
		dev_err(chan2dev(chan),
			"8 bytes buswidth (src=%u, dst=%u) not supported on port (sap=%u, dap=%u\n",
			sdw, ddw, sap, dap);
		return -EINVAL;
	}

	if (FIELD_GET(STM32_DMA3_DT_SINC, tr_conf))
		_ctr1 |= CTR1_SINC;
	if (sap)
		_ctr1 |= CTR1_SAP;
	if (FIELD_GET(STM32_DMA3_DT_DINC, tr_conf))
		_ctr1 |= CTR1_DINC;
	if (dap)
		_ctr1 |= CTR1_DAP;

	_ctr2 |= FIELD_PREP(CTR2_REQSEL, chan->dt_config.req_line) & ~CTR2_SWREQ;
	if (FIELD_GET(STM32_DMA3_DT_BREQ, tr_conf))
		_ctr2 |= CTR2_BREQ;
	if (dir == DMA_DEV_TO_MEM && FIELD_GET(STM32_DMA3_DT_PFREQ, tr_conf))
		_ctr2 |= CTR2_PFREQ;
	tcem = FIELD_GET(STM32_DMA3_DT_TCEM, tr_conf);
	_ctr2 |= FIELD_PREP(CTR2_TCEM, tcem);

	/* Store TCEM to know on which event TC flag occurred */
	chan->tcem = tcem;
	/* Store direction for residue computation */
	chan->dma_config.direction = dir;

	switch (dir) {
	case DMA_MEM_TO_DEV:
		/* Set destination (device) data width and burst */
		ddw = min_t(u32, ddw, stm32_dma3_get_max_dw(chan->max_burst, dap_max_dw,
							    len, dst_addr));
		dbl_max = min_t(u32, dbl_max, stm32_dma3_get_max_burst(len, ddw, chan->max_burst));

		/* Set source (memory) data width and burst */
		sdw = stm32_dma3_get_max_dw(chan->max_burst, sap_max_dw, len, src_addr);
		sbl_max = stm32_dma3_get_max_burst(len, sdw, chan->max_burst);

		_ctr1 |= FIELD_PREP(CTR1_SDW_LOG2, ilog2(sdw));
		_ctr1 |= FIELD_PREP(CTR1_SBL_1, sbl_max - 1);
		_ctr1 |= FIELD_PREP(CTR1_DDW_LOG2, ilog2(ddw));
		_ctr1 |= FIELD_PREP(CTR1_DBL_1, dbl_max - 1);

		if (ddw != sdw) {
			_ctr1 |= FIELD_PREP(CTR1_PAM, CTR1_PAM_PACK_UNPACK);
			/* Should never reach this case as ddw is clamped down */
			if (len & (ddw - 1)) {
				dev_err(chan2dev(chan),
					"Packing mode is enabled and len is not multiple of ddw");
				return -EINVAL;
			}
		}

		/* dst = dev */
		_ctr2 |= CTR2_DREQ;

		break;

	case DMA_DEV_TO_MEM:
		/* Set source (device) data width and burst */
		sdw = min_t(u32, sdw, stm32_dma3_get_max_dw(chan->max_burst, sap_max_dw,
							    len, src_addr));
		sbl_max = min_t(u32, sbl_max, stm32_dma3_get_max_burst(len, sdw, chan->max_burst));

		/* Set destination (memory) data width and burst */
		ddw = stm32_dma3_get_max_dw(chan->max_burst, dap_max_dw, len, dst_addr);
		dbl_max = stm32_dma3_get_max_burst(len, ddw, chan->max_burst);

		_ctr1 |= FIELD_PREP(CTR1_SDW_LOG2, ilog2(sdw));
		_ctr1 |= FIELD_PREP(CTR1_SBL_1, sbl_max - 1);
		_ctr1 |= FIELD_PREP(CTR1_DDW_LOG2, ilog2(ddw));
		_ctr1 |= FIELD_PREP(CTR1_DBL_1, dbl_max - 1);

		if (ddw != sdw) {
			_ctr1 |= FIELD_PREP(CTR1_PAM, CTR1_PAM_PACK_UNPACK);
			/* Should never reach this case as ddw is clamped down */
			if (len & (ddw - 1)) {
				dev_err(chan2dev(chan),
					"Packing mode is enabled and len is not multiple of ddw\n");
				return -EINVAL;
			}
		}

		/* dst = mem */
		_ctr2 &= ~CTR2_DREQ;

		break;

	case DMA_MEM_TO_MEM:
		/* Set source (memory) data width and burst */
		init_dw = sdw;
		init_bl_max = sbl_max;
		sdw = stm32_dma3_get_max_dw(chan->max_burst, sap_max_dw, len, src_addr);
		sbl_max = stm32_dma3_get_max_burst(len, sdw, chan->max_burst);
		if (chan->config_set & STM32_DMA3_CFG_SET_DMA) {
			sdw = min_t(u32, init_dw, sdw);
			sbl_max = min_t(u32, init_bl_max,
					stm32_dma3_get_max_burst(len, sdw, chan->max_burst));
		}

		/* Set destination (memory) data width and burst */
		init_dw = ddw;
		init_bl_max = dbl_max;
		ddw = stm32_dma3_get_max_dw(chan->max_burst, dap_max_dw, len, dst_addr);
		dbl_max = stm32_dma3_get_max_burst(len, ddw, chan->max_burst);
		if (chan->config_set & STM32_DMA3_CFG_SET_DMA) {
			ddw = min_t(u32, init_dw, ddw);
			dbl_max = min_t(u32, init_bl_max,
					stm32_dma3_get_max_burst(len, ddw, chan->max_burst));
		}

		_ctr1 |= FIELD_PREP(CTR1_SDW_LOG2, ilog2(sdw));
		_ctr1 |= FIELD_PREP(CTR1_SBL_1, sbl_max - 1);
		_ctr1 |= FIELD_PREP(CTR1_DDW_LOG2, ilog2(ddw));
		_ctr1 |= FIELD_PREP(CTR1_DBL_1, dbl_max - 1);

		if (ddw != sdw) {
			_ctr1 |= FIELD_PREP(CTR1_PAM, CTR1_PAM_PACK_UNPACK);
			/* Should never reach this case as ddw is clamped down */
			if (len & (ddw - 1)) {
				dev_err(chan2dev(chan),
					"Packing mode is enabled and len is not multiple of ddw");
				return -EINVAL;
			}
		}

		/* CTR2_REQSEL/DREQ/BREQ/PFREQ are ignored with CTR2_SWREQ=1 */
		_ctr2 |= CTR2_SWREQ;

		break;

	default:
		dev_err(chan2dev(chan), "Direction %s not supported\n",
			dmaengine_get_direction_text(dir));
		return -EINVAL;
	}

	*ccr |= FIELD_PREP(CCR_PRIO, FIELD_GET(STM32_DMA3_DT_PRIO, ch_conf));
	*ctr1 = _ctr1;
	*ctr2 = _ctr2;

	dev_dbg(chan2dev(chan), "%s: sdw=%u bytes sbl=%u beats ddw=%u bytes dbl=%u beats\n",
		__func__, sdw, sbl_max, ddw, dbl_max);

	return 0;
}

static void stm32_dma3_chan_start(struct stm32_dma3_chan *chan)
{
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	struct virt_dma_desc *vdesc;
	struct stm32_dma3_hwdesc *hwdesc;
	u32 id = chan->id;
	u32 csr, ccr;

	vdesc = vchan_next_desc(&chan->vchan);
	if (!vdesc) {
		chan->swdesc = NULL;
		return;
	}
	list_del(&vdesc->node);

	chan->swdesc = to_stm32_dma3_swdesc(vdesc);
	hwdesc = chan->swdesc->lli[0].hwdesc;

	stm32_dma3_chan_dump_hwdesc(chan, chan->swdesc);

	writel_relaxed(chan->swdesc->ccr, ddata->base + STM32_DMA3_CCR(id));
	writel_relaxed(hwdesc->ctr1, ddata->base + STM32_DMA3_CTR1(id));
	writel_relaxed(hwdesc->ctr2, ddata->base + STM32_DMA3_CTR2(id));
	writel_relaxed(hwdesc->cbr1, ddata->base + STM32_DMA3_CBR1(id));
	writel_relaxed(hwdesc->csar, ddata->base + STM32_DMA3_CSAR(id));
	writel_relaxed(hwdesc->cdar, ddata->base + STM32_DMA3_CDAR(id));
	writel_relaxed(hwdesc->cllr, ddata->base + STM32_DMA3_CLLR(id));

	/* Clear any pending interrupts */
	csr = readl_relaxed(ddata->base + STM32_DMA3_CSR(id));
	if (csr & CSR_ALL_F)
		writel_relaxed(csr, ddata->base + STM32_DMA3_CFCR(id));

	stm32_dma3_chan_dump_reg(chan);

	ccr = readl_relaxed(ddata->base + STM32_DMA3_CCR(id));
	writel_relaxed(ccr | CCR_EN, ddata->base + STM32_DMA3_CCR(id));

	chan->dma_status = DMA_IN_PROGRESS;

	dev_dbg(chan2dev(chan), "vchan %pK: started\n", &chan->vchan);
}

static int stm32_dma3_chan_suspend(struct stm32_dma3_chan *chan, bool susp)
{
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	u32 csr, ccr = readl_relaxed(ddata->base + STM32_DMA3_CCR(chan->id)) & ~CCR_EN;
	int ret = 0;

	if (susp)
		ccr |= CCR_SUSP;
	else
		ccr &= ~CCR_SUSP;

	writel_relaxed(ccr, ddata->base + STM32_DMA3_CCR(chan->id));

	if (susp) {
		ret = readl_relaxed_poll_timeout_atomic(ddata->base + STM32_DMA3_CSR(chan->id), csr,
							csr & CSR_SUSPF, 1, 10);
		if (!ret)
			writel_relaxed(CFCR_SUSPF, ddata->base + STM32_DMA3_CFCR(chan->id));

		stm32_dma3_chan_dump_reg(chan);
	}

	return ret;
}

static void stm32_dma3_chan_reset(struct stm32_dma3_chan *chan)
{
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	u32 ccr = readl_relaxed(ddata->base + STM32_DMA3_CCR(chan->id)) & ~CCR_EN;

	writel_relaxed(ccr |= CCR_RESET, ddata->base + STM32_DMA3_CCR(chan->id));
}

static int stm32_dma3_chan_get_curr_hwdesc(struct stm32_dma3_swdesc *swdesc, u32 cllr, u32 *residue)
{
	u32 i, lli_offset, next_lli_offset = cllr & CLLR_LA;

	/* If cllr is null, it means it is either the last or single item */
	if (!cllr)
		return swdesc->lli_size - 1;

	/* In cyclic mode, go fast and first check we are not on the last item */
	if (swdesc->cyclic && next_lli_offset == (swdesc->lli[0].hwdesc_addr & CLLR_LA))
		return swdesc->lli_size - 1;

	/* As transfer is in progress, look backward from the last item */
	for (i = swdesc->lli_size - 1; i > 0; i--) {
		*residue += FIELD_GET(CBR1_BNDT, swdesc->lli[i].hwdesc->cbr1);
		lli_offset = swdesc->lli[i].hwdesc_addr & CLLR_LA;
		if (lli_offset == next_lli_offset)
			return i - 1;
	}

	return -EINVAL;
}

static void stm32_dma3_chan_set_residue(struct stm32_dma3_chan *chan,
					struct stm32_dma3_swdesc *swdesc,
					struct dma_tx_state *txstate)
{
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	struct device *dev = chan2dev(chan);
	struct stm32_dma3_hwdesc *hwdesc;
	u32 residue, curr_lli, csr, cdar, cbr1, cllr, bndt, fifol;
	bool pack_unpack;
	int ret;

	csr = readl_relaxed(ddata->base + STM32_DMA3_CSR(chan->id));
	if (!(csr & CSR_IDLEF) && chan->dma_status != DMA_PAUSED) {
		/* Suspend current transfer to read registers for a snapshot */
		writel_relaxed(swdesc->ccr | CCR_SUSP, ddata->base + STM32_DMA3_CCR(chan->id));
		ret = readl_relaxed_poll_timeout_atomic(ddata->base + STM32_DMA3_CSR(chan->id), csr,
							csr & (CSR_SUSPF | CSR_IDLEF), 1, 10);

		if (ret || ((csr & CSR_TCF) && (csr & CSR_IDLEF))) {
			writel_relaxed(CFCR_SUSPF, ddata->base + STM32_DMA3_CFCR(chan->id));
			writel_relaxed(swdesc->ccr, ddata->base + STM32_DMA3_CCR(chan->id));
			if (ret)
				dev_err(dev, "Channel suspension timeout, csr=%08x\n", csr);
		}
	}

	/* If channel is still active (CSR_IDLEF is not set), can't get a reliable residue */
	if (!(csr & CSR_IDLEF))
		dev_warn(dev, "Can't get residue: channel still active, csr=%08x\n", csr);

	/*
	 * If channel is not suspended, but Idle and Transfer Complete are set,
	 * linked-list is over, no residue
	 */
	if (!(csr & CSR_SUSPF) && (csr & CSR_TCF) && (csr & CSR_IDLEF))
		return;

	/* Read registers to have a snapshot */
	cllr = readl_relaxed(ddata->base + STM32_DMA3_CLLR(chan->id));
	cbr1 = readl_relaxed(ddata->base + STM32_DMA3_CBR1(chan->id));
	cdar = readl_relaxed(ddata->base + STM32_DMA3_CDAR(chan->id));

	/* Resume current transfer */
	if (csr & CSR_SUSPF) {
		writel_relaxed(CFCR_SUSPF, ddata->base + STM32_DMA3_CFCR(chan->id));
		writel_relaxed(swdesc->ccr, ddata->base + STM32_DMA3_CCR(chan->id));
	}

	/* Add current BNDT */
	bndt = FIELD_GET(CBR1_BNDT, cbr1);
	residue = bndt;

	/* Get current hwdesc and cumulate residue of pending hwdesc BNDT */
	ret = stm32_dma3_chan_get_curr_hwdesc(swdesc, cllr, &residue);
	if (ret < 0) {
		dev_err(chan2dev(chan), "Can't get residue: current hwdesc not found\n");
		return;
	}
	curr_lli = ret;

	/* Read current FIFO level - in units of programmed destination data width */
	hwdesc = swdesc->lli[curr_lli].hwdesc;
	fifol = FIELD_GET(CSR_FIFOL, csr) * (1 << FIELD_GET(CTR1_DDW_LOG2, hwdesc->ctr1));
	/* If the FIFO contains as many bytes as its size, it can't contain more */
	if (fifol == (1 << (chan->fifo_size + 1)))
		goto skip_fifol_update;

	/*
	 * In case of PACKING (Destination burst length > Source burst length) or UNPACKING
	 * (Source burst length > Destination burst length), bytes could be pending in the FIFO
	 * (to be packed up to Destination burst length or unpacked into Destination burst length
	 * chunks).
	 * BNDT is not reliable, as it reflects the number of bytes read from the source but not the
	 * number of bytes written to the destination.
	 * FIFOL is also not sufficient, because it reflects the number of available write beats in
	 * units of Destination data width but not the bytes not yet packed or unpacked.
	 * In case of Destination increment DINC, it is possible to compute the number of bytes in
	 * the FIFO:
	 * fifol_in_bytes = bytes_read - bytes_written.
	 */
	pack_unpack = !!(FIELD_GET(CTR1_PAM, hwdesc->ctr1) == CTR1_PAM_PACK_UNPACK);
	if (pack_unpack && (hwdesc->ctr1 & CTR1_DINC)) {
		int bytes_read = FIELD_GET(CBR1_BNDT, hwdesc->cbr1) - bndt;
		int bytes_written = cdar - hwdesc->cdar;

		if (bytes_read > 0)
			fifol = bytes_read - bytes_written;
	}

skip_fifol_update:
	if (fifol) {
		dev_dbg(chan2dev(chan), "%u byte(s) in the FIFO\n", fifol);
		dma_set_in_flight_bytes(txstate, fifol);
		/*
		 * Residue is already accurate for DMA_MEM_TO_DEV as BNDT reflects data read from
		 * the source memory buffer, so just need to add fifol to residue in case of
		 * DMA_DEV_TO_MEM transfer because these bytes are not yet written in destination
		 * memory buffer.
		 */
		if (chan->dma_config.direction == DMA_DEV_TO_MEM)
			residue += fifol;
	}
	dma_set_residue(txstate, residue);
}

static int stm32_dma3_chan_stop(struct stm32_dma3_chan *chan)
{
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	u32 ccr;
	int ret = 0;

	chan->dma_status = DMA_COMPLETE;

	/* Disable interrupts */
	ccr = readl_relaxed(ddata->base + STM32_DMA3_CCR(chan->id));
	writel_relaxed(ccr & ~(CCR_ALLIE | CCR_EN), ddata->base + STM32_DMA3_CCR(chan->id));

	if (!(ccr & CCR_SUSP) && (ccr & CCR_EN)) {
		/* Suspend the channel */
		ret = stm32_dma3_chan_suspend(chan, true);
		if (ret)
			dev_warn(chan2dev(chan), "%s: timeout, data might be lost\n", __func__);
	}

	/*
	 * Reset the channel: this causes the reset of the FIFO and the reset of the channel
	 * internal state, the reset of CCR_EN and CCR_SUSP bits.
	 */
	stm32_dma3_chan_reset(chan);

	return ret;
}

static void stm32_dma3_chan_complete(struct stm32_dma3_chan *chan)
{
	if (!chan->swdesc)
		return;

	vchan_cookie_complete(&chan->swdesc->vdesc);
	chan->swdesc = NULL;
	stm32_dma3_chan_start(chan);
}

static irqreturn_t stm32_dma3_chan_irq(int irq, void *devid)
{
	struct stm32_dma3_chan *chan = devid;
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	u32 misr, csr, ccr;

	spin_lock(&chan->vchan.lock);

	misr = readl_relaxed(ddata->base + STM32_DMA3_MISR);
	if (!(misr & MISR_MIS(chan->id))) {
		spin_unlock(&chan->vchan.lock);
		return IRQ_NONE;
	}

	csr = readl_relaxed(ddata->base + STM32_DMA3_CSR(chan->id));
	ccr = readl_relaxed(ddata->base + STM32_DMA3_CCR(chan->id)) & CCR_ALLIE;

	if (csr & CSR_TCF && ccr & CCR_TCIE) {
		if (chan->swdesc->cyclic)
			vchan_cyclic_callback(&chan->swdesc->vdesc);
		else
			stm32_dma3_chan_complete(chan);
	}

	if (csr & CSR_USEF && ccr & CCR_USEIE) {
		dev_err(chan2dev(chan), "User setting error\n");
		chan->dma_status = DMA_ERROR;
		/* CCR.EN automatically cleared by HW */
		stm32_dma3_check_user_setting(chan);
		stm32_dma3_chan_reset(chan);
	}

	if (csr & CSR_ULEF && ccr & CCR_ULEIE) {
		dev_err(chan2dev(chan), "Update link transfer error\n");
		chan->dma_status = DMA_ERROR;
		/* CCR.EN automatically cleared by HW */
		stm32_dma3_chan_reset(chan);
	}

	if (csr & CSR_DTEF && ccr & CCR_DTEIE) {
		dev_err(chan2dev(chan), "Data transfer error\n");
		chan->dma_status = DMA_ERROR;
		/* CCR.EN automatically cleared by HW */
		stm32_dma3_chan_reset(chan);
	}

	/*
	 * Half Transfer Interrupt may be disabled but Half Transfer Flag can be set,
	 * ensure HTF flag to be cleared, with other flags.
	 */
	csr &= (ccr | CCR_HTIE);

	if (csr)
		writel_relaxed(csr, ddata->base + STM32_DMA3_CFCR(chan->id));

	spin_unlock(&chan->vchan.lock);

	return IRQ_HANDLED;
}

static int stm32_dma3_alloc_chan_resources(struct dma_chan *c)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	u32 id = chan->id, csemcr, ccid;
	int ret;

	ret = pm_runtime_resume_and_get(ddata->dma_dev.dev);
	if (ret < 0)
		return ret;

	/* Ensure the channel is free */
	if (chan->semaphore_mode &&
	    readl_relaxed(ddata->base + STM32_DMA3_CSEMCR(chan->id)) & CSEMCR_SEM_MUTEX) {
		ret = -EBUSY;
		goto err_put_sync;
	}

	chan->lli_pool = dmam_pool_create(dev_name(&c->dev->device), c->device->dev,
					  sizeof(struct stm32_dma3_hwdesc),
					  __alignof__(struct stm32_dma3_hwdesc), SZ_64K);
	if (!chan->lli_pool) {
		dev_err(chan2dev(chan), "Failed to create LLI pool\n");
		ret = -ENOMEM;
		goto err_put_sync;
	}

	/* Take the channel semaphore */
	if (chan->semaphore_mode) {
		writel_relaxed(CSEMCR_SEM_MUTEX, ddata->base + STM32_DMA3_CSEMCR(id));
		csemcr = readl_relaxed(ddata->base + STM32_DMA3_CSEMCR(id));
		ccid = FIELD_GET(CSEMCR_SEM_CCID, csemcr);
		/* Check that the channel is well taken */
		if (ccid != CCIDCFGR_CID1) {
			dev_err(chan2dev(chan), "Not under CID1 control (in-use by CID%d)\n", ccid);
			ret = -EPERM;
			goto err_pool_destroy;
		}
		dev_dbg(chan2dev(chan), "Under CID1 control (semcr=0x%08x)\n", csemcr);
	}

	return 0;

err_pool_destroy:
	dmam_pool_destroy(chan->lli_pool);
	chan->lli_pool = NULL;

err_put_sync:
	pm_runtime_put_sync(ddata->dma_dev.dev);

	return ret;
}

static void stm32_dma3_free_chan_resources(struct dma_chan *c)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	unsigned long flags;

	/* Ensure channel is in idle state */
	spin_lock_irqsave(&chan->vchan.lock, flags);
	stm32_dma3_chan_stop(chan);
	chan->swdesc = NULL;
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	vchan_free_chan_resources(to_virt_chan(c));

	dmam_pool_destroy(chan->lli_pool);
	chan->lli_pool = NULL;

	/* Release the channel semaphore */
	if (chan->semaphore_mode)
		writel_relaxed(0, ddata->base + STM32_DMA3_CSEMCR(chan->id));

	pm_runtime_put_sync(ddata->dma_dev.dev);

	/* Reset configuration */
	memset(&chan->dt_config, 0, sizeof(chan->dt_config));
	memset(&chan->dma_config, 0, sizeof(chan->dma_config));
	chan->config_set = 0;
}

static void stm32_dma3_init_chan_config_for_memcpy(struct stm32_dma3_chan *chan,
						   dma_addr_t dst, dma_addr_t src)
{
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	u32 dw = get_chan_max_dw(ddata->ports_max_dw[0], chan->max_burst); /* port 0 by default */
	u32 burst = chan->max_burst / dw;

	/* Initialize dt_config if channel not pre-configured through DT */
	if (!(chan->config_set & STM32_DMA3_CFG_SET_DT)) {
		chan->dt_config.ch_conf = FIELD_PREP(STM32_DMA3_DT_PRIO, CCR_PRIO_VERY_HIGH);
		chan->dt_config.ch_conf |= FIELD_PREP(STM32_DMA3_DT_FIFO, chan->fifo_size);
		chan->dt_config.tr_conf = STM32_DMA3_DT_SINC | STM32_DMA3_DT_DINC;
		chan->dt_config.tr_conf |= FIELD_PREP(STM32_DMA3_DT_TCEM, CTR2_TCEM_CHANNEL);
	}

	/* Initialize dma_config if dmaengine_slave_config() not used */
	if (!(chan->config_set & STM32_DMA3_CFG_SET_DMA)) {
		chan->dma_config.src_addr_width = dw;
		chan->dma_config.dst_addr_width = dw;
		chan->dma_config.src_maxburst = burst;
		chan->dma_config.dst_maxburst = burst;
		chan->dma_config.src_addr = src;
		chan->dma_config.dst_addr = dst;
	}
}

static struct dma_async_tx_descriptor *stm32_dma3_prep_dma_memcpy(struct dma_chan *c,
								  dma_addr_t dst, dma_addr_t src,
								  size_t len, unsigned long flags)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);
	struct stm32_dma3_swdesc *swdesc;
	size_t next_size, offset;
	u32 count, i, ctr1, ctr2;

	count = DIV_ROUND_UP(len, STM32_DMA3_MAX_BLOCK_SIZE);

	swdesc = stm32_dma3_chan_desc_alloc(chan, count);
	if (!swdesc)
		return NULL;

	if (chan->config_set != STM32_DMA3_CFG_SET_BOTH)
		stm32_dma3_init_chan_config_for_memcpy(chan, dst, src);

	for (i = 0, offset = 0; offset < len; i++, offset += next_size) {
		size_t remaining;
		int ret;

		remaining = len - offset;
		next_size = min_t(size_t, remaining, STM32_DMA3_MAX_BLOCK_SIZE);

		ret = stm32_dma3_chan_prep_hw(chan, DMA_MEM_TO_MEM, &swdesc->ccr, &ctr1, &ctr2,
					      src + offset, dst + offset, next_size);
		if (ret)
			goto err_desc_free;

		stm32_dma3_chan_prep_hwdesc(chan, swdesc, i, src + offset, dst + offset, next_size,
					    ctr1, ctr2, next_size == remaining, false);
	}

	/* Enable Errors interrupts */
	swdesc->ccr |= CCR_USEIE | CCR_ULEIE | CCR_DTEIE;
	/* Enable Transfer state interrupts */
	swdesc->ccr |= CCR_TCIE;

	swdesc->cyclic = false;

	return vchan_tx_prep(&chan->vchan, &swdesc->vdesc, flags);

err_desc_free:
	stm32_dma3_chan_desc_free(chan, swdesc);

	return NULL;
}

static struct dma_async_tx_descriptor *stm32_dma3_prep_slave_sg(struct dma_chan *c,
								struct scatterlist *sgl,
								unsigned int sg_len,
								enum dma_transfer_direction dir,
								unsigned long flags, void *context)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);
	struct stm32_dma3_swdesc *swdesc;
	struct scatterlist *sg;
	size_t len;
	dma_addr_t sg_addr, dev_addr, src, dst;
	u32 i, j, count, ctr1, ctr2;
	int ret;

	count = sg_len;
	for_each_sg(sgl, sg, sg_len, i) {
		len = sg_dma_len(sg);
		if (len > STM32_DMA3_MAX_BLOCK_SIZE)
			count += DIV_ROUND_UP(len, STM32_DMA3_MAX_BLOCK_SIZE) - 1;
	}

	swdesc = stm32_dma3_chan_desc_alloc(chan, count);
	if (!swdesc)
		return NULL;

	/* sg_len and i correspond to the initial sgl; count and j correspond to the hwdesc LL */
	j = 0;
	for_each_sg(sgl, sg, sg_len, i) {
		sg_addr = sg_dma_address(sg);
		dev_addr = (dir == DMA_MEM_TO_DEV) ? chan->dma_config.dst_addr :
						     chan->dma_config.src_addr;
		len = sg_dma_len(sg);

		do {
			size_t chunk = min_t(size_t, len, STM32_DMA3_MAX_BLOCK_SIZE);

			if (dir == DMA_MEM_TO_DEV) {
				src = sg_addr;
				dst = dev_addr;

				ret = stm32_dma3_chan_prep_hw(chan, dir, &swdesc->ccr, &ctr1, &ctr2,
							      src, dst, chunk);

				if (FIELD_GET(CTR1_DINC, ctr1))
					dev_addr += chunk;
			} else { /* (dir == DMA_DEV_TO_MEM || dir == DMA_MEM_TO_MEM) */
				src = dev_addr;
				dst = sg_addr;

				ret = stm32_dma3_chan_prep_hw(chan, dir, &swdesc->ccr, &ctr1, &ctr2,
							      src, dst, chunk);

				if (FIELD_GET(CTR1_SINC, ctr1))
					dev_addr += chunk;
			}

			if (ret)
				goto err_desc_free;

			stm32_dma3_chan_prep_hwdesc(chan, swdesc, j, src, dst, chunk,
						    ctr1, ctr2, j == (count - 1), false);

			sg_addr += chunk;
			len -= chunk;
			j++;
		} while (len);
	}

	/* Enable Error interrupts */
	swdesc->ccr |= CCR_USEIE | CCR_ULEIE | CCR_DTEIE;
	/* Enable Transfer state interrupts */
	swdesc->ccr |= CCR_TCIE;

	swdesc->cyclic = false;

	return vchan_tx_prep(&chan->vchan, &swdesc->vdesc, flags);

err_desc_free:
	stm32_dma3_chan_desc_free(chan, swdesc);

	return NULL;
}

static struct dma_async_tx_descriptor *stm32_dma3_prep_dma_cyclic(struct dma_chan *c,
								  dma_addr_t buf_addr,
								  size_t buf_len, size_t period_len,
								  enum dma_transfer_direction dir,
								  unsigned long flags)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);
	struct stm32_dma3_swdesc *swdesc;
	dma_addr_t src, dst;
	u32 count, i, ctr1, ctr2;
	int ret;

	if (!buf_len || !period_len || period_len > STM32_DMA3_MAX_BLOCK_SIZE) {
		dev_err(chan2dev(chan), "Invalid buffer/period length\n");
		return NULL;
	}

	if (buf_len % period_len) {
		dev_err(chan2dev(chan), "Buffer length not multiple of period length\n");
		return NULL;
	}

	count = buf_len / period_len;
	swdesc = stm32_dma3_chan_desc_alloc(chan, count);
	if (!swdesc)
		return NULL;

	if (dir == DMA_MEM_TO_DEV) {
		src = buf_addr;
		dst = chan->dma_config.dst_addr;

		ret = stm32_dma3_chan_prep_hw(chan, DMA_MEM_TO_DEV, &swdesc->ccr, &ctr1, &ctr2,
					      src, dst, period_len);
	} else if (dir == DMA_DEV_TO_MEM) {
		src = chan->dma_config.src_addr;
		dst = buf_addr;

		ret = stm32_dma3_chan_prep_hw(chan, DMA_DEV_TO_MEM, &swdesc->ccr, &ctr1, &ctr2,
					      src, dst, period_len);
	} else {
		dev_err(chan2dev(chan), "Invalid direction\n");
		ret = -EINVAL;
	}

	if (ret)
		goto err_desc_free;

	for (i = 0; i < count; i++) {
		if (dir == DMA_MEM_TO_DEV) {
			src = buf_addr + i * period_len;
			dst = chan->dma_config.dst_addr;
		} else { /* (dir == DMA_DEV_TO_MEM) */
			src = chan->dma_config.src_addr;
			dst = buf_addr + i * period_len;
		}

		stm32_dma3_chan_prep_hwdesc(chan, swdesc, i, src, dst, period_len,
					    ctr1, ctr2, i == (count - 1), true);
	}

	/* Enable Error interrupts */
	swdesc->ccr |= CCR_USEIE | CCR_ULEIE | CCR_DTEIE;
	/* Enable Transfer state interrupts */
	swdesc->ccr |= CCR_TCIE;

	swdesc->cyclic = true;

	return vchan_tx_prep(&chan->vchan, &swdesc->vdesc, flags);

err_desc_free:
	stm32_dma3_chan_desc_free(chan, swdesc);

	return NULL;
}

static void stm32_dma3_caps(struct dma_chan *c, struct dma_slave_caps *caps)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);

	if (!chan->fifo_size) {
		caps->max_burst = 0;
		caps->src_addr_widths &= ~BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
		caps->dst_addr_widths &= ~BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
	} else {
		/* Burst transfer should not exceed half of the fifo size */
		caps->max_burst = chan->max_burst;
		if (caps->max_burst < DMA_SLAVE_BUSWIDTH_8_BYTES) {
			caps->src_addr_widths &= ~BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
			caps->dst_addr_widths &= ~BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
		}
	}
}

static int stm32_dma3_config(struct dma_chan *c, struct dma_slave_config *config)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);

	memcpy(&chan->dma_config, config, sizeof(*config));
	chan->config_set |= STM32_DMA3_CFG_SET_DMA;

	return 0;
}

static int stm32_dma3_pause(struct dma_chan *c)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);
	int ret;

	ret = stm32_dma3_chan_suspend(chan, true);
	if (ret)
		return ret;

	chan->dma_status = DMA_PAUSED;

	dev_dbg(chan2dev(chan), "vchan %pK: paused\n", &chan->vchan);

	return 0;
}

static int stm32_dma3_resume(struct dma_chan *c)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);

	stm32_dma3_chan_suspend(chan, false);

	chan->dma_status = DMA_IN_PROGRESS;

	dev_dbg(chan2dev(chan), "vchan %pK: resumed\n", &chan->vchan);

	return 0;
}

static int stm32_dma3_terminate_all(struct dma_chan *c)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&chan->vchan.lock, flags);

	if (chan->swdesc) {
		vchan_terminate_vdesc(&chan->swdesc->vdesc);
		chan->swdesc = NULL;
	}

	stm32_dma3_chan_stop(chan);

	vchan_get_all_descriptors(&chan->vchan, &head);

	spin_unlock_irqrestore(&chan->vchan.lock, flags);
	vchan_dma_desc_free_list(&chan->vchan, &head);

	dev_dbg(chan2dev(chan), "vchan %pK: terminated\n", &chan->vchan);

	return 0;
}

static void stm32_dma3_synchronize(struct dma_chan *c)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);

	vchan_synchronize(&chan->vchan);
}

static enum dma_status stm32_dma3_tx_status(struct dma_chan *c, dma_cookie_t cookie,
					    struct dma_tx_state *txstate)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);
	struct stm32_dma3_swdesc *swdesc = NULL;
	enum dma_status status;
	unsigned long flags;
	struct virt_dma_desc *vd;

	status = dma_cookie_status(c, cookie, txstate);
	if (status == DMA_COMPLETE)
		return status;

	if (!txstate)
		return chan->dma_status;

	spin_lock_irqsave(&chan->vchan.lock, flags);

	vd = vchan_find_desc(&chan->vchan, cookie);
	if (vd)
		swdesc = to_stm32_dma3_swdesc(vd);
	else if (chan->swdesc && chan->swdesc->vdesc.tx.cookie == cookie)
		swdesc = chan->swdesc;

	/* Get residue/in_flight_bytes only if a transfer is currently running (swdesc != NULL) */
	if (swdesc)
		stm32_dma3_chan_set_residue(chan, swdesc, txstate);

	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	return chan->dma_status;
}

static void stm32_dma3_issue_pending(struct dma_chan *c)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);
	unsigned long flags;

	spin_lock_irqsave(&chan->vchan.lock, flags);

	if (vchan_issue_pending(&chan->vchan) && !chan->swdesc) {
		dev_dbg(chan2dev(chan), "vchan %pK: issued\n", &chan->vchan);
		stm32_dma3_chan_start(chan);
	}

	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

static bool stm32_dma3_filter_fn(struct dma_chan *c, void *fn_param)
{
	struct stm32_dma3_chan *chan = to_stm32_dma3_chan(c);
	struct stm32_dma3_ddata *ddata = to_stm32_dma3_ddata(chan);
	struct stm32_dma3_dt_conf *conf = fn_param;
	u32 mask, semcr;
	int ret;

	dev_dbg(c->device->dev, "%s(%s): req_line=%d ch_conf=%08x tr_conf=%08x\n",
		__func__, dma_chan_name(c), conf->req_line, conf->ch_conf, conf->tr_conf);

	if (!of_property_read_u32(c->device->dev->of_node, "dma-channel-mask", &mask))
		if (!(mask & BIT(chan->id)))
			return false;

	ret = pm_runtime_resume_and_get(ddata->dma_dev.dev);
	if (ret < 0)
		return false;
	semcr = readl_relaxed(ddata->base + STM32_DMA3_CSEMCR(chan->id));
	pm_runtime_put_sync(ddata->dma_dev.dev);

	/* Check if chan is free */
	if (semcr & CSEMCR_SEM_MUTEX)
		return false;

	/* Check if chan fifo fits well */
	if (FIELD_GET(STM32_DMA3_DT_FIFO, conf->ch_conf) != chan->fifo_size)
		return false;

	return true;
}

static struct dma_chan *stm32_dma3_of_xlate(struct of_phandle_args *dma_spec, struct of_dma *ofdma)
{
	struct stm32_dma3_ddata *ddata = ofdma->of_dma_data;
	dma_cap_mask_t mask = ddata->dma_dev.cap_mask;
	struct stm32_dma3_dt_conf conf;
	struct stm32_dma3_chan *chan;
	struct dma_chan *c;

	if (dma_spec->args_count < 3) {
		dev_err(ddata->dma_dev.dev, "Invalid args count\n");
		return NULL;
	}

	conf.req_line = dma_spec->args[0];
	conf.ch_conf = dma_spec->args[1];
	conf.tr_conf = dma_spec->args[2];

	if (conf.req_line >= ddata->dma_requests) {
		dev_err(ddata->dma_dev.dev, "Invalid request line\n");
		return NULL;
	}

	/* Request dma channel among the generic dma controller list */
	c = dma_request_channel(mask, stm32_dma3_filter_fn, &conf);
	if (!c) {
		dev_err(ddata->dma_dev.dev, "No suitable channel found\n");
		return NULL;
	}

	chan = to_stm32_dma3_chan(c);
	chan->dt_config = conf;
	chan->config_set |= STM32_DMA3_CFG_SET_DT;

	return c;
}

static u32 stm32_dma3_check_rif(struct stm32_dma3_ddata *ddata)
{
	u32 chan_reserved, mask = 0, i, ccidcfgr, invalid_cid = 0;

	/* Reserve Secure channels */
	chan_reserved = readl_relaxed(ddata->base + STM32_DMA3_SECCFGR);

	/*
	 * CID filtering must be configured to ensure that the DMA3 channel will inherit the CID of
	 * the processor which is configuring and using the given channel.
	 * In case CID filtering is not configured, dma-channel-mask property can be used to
	 * specify available DMA channels to the kernel.
	 */
	of_property_read_u32(ddata->dma_dev.dev->of_node, "dma-channel-mask", &mask);

	/* Reserve !CID-filtered not in dma-channel-mask, static CID != CID1, CID1 not allowed */
	for (i = 0; i < ddata->dma_channels; i++) {
		ccidcfgr = readl_relaxed(ddata->base + STM32_DMA3_CCIDCFGR(i));

		if (!(ccidcfgr & CCIDCFGR_CFEN)) { /* !CID-filtered */
			invalid_cid |= BIT(i);
			if (!(mask & BIT(i))) /* Not in dma-channel-mask */
				chan_reserved |= BIT(i);
		} else { /* CID-filtered */
			if (!(ccidcfgr & CCIDCFGR_SEM_EN)) { /* Static CID mode */
				if (FIELD_GET(CCIDCFGR_SCID, ccidcfgr) != CCIDCFGR_CID1)
					chan_reserved |= BIT(i);
			} else { /* Semaphore mode */
				if (!FIELD_GET(CCIDCFGR_SEM_WLIST_CID1, ccidcfgr))
					chan_reserved |= BIT(i);
				ddata->chans[i].semaphore_mode = true;
			}
		}
		dev_dbg(ddata->dma_dev.dev, "chan%d: %s mode, %s\n", i,
			!(ccidcfgr & CCIDCFGR_CFEN) ? "!CID-filtered" :
			ddata->chans[i].semaphore_mode ? "Semaphore" : "Static CID",
			(chan_reserved & BIT(i)) ? "denied" :
			mask & BIT(i) ? "force allowed" : "allowed");
	}

	if (invalid_cid)
		dev_warn(ddata->dma_dev.dev, "chan%*pbl have invalid CID configuration\n",
			 ddata->dma_channels, &invalid_cid);

	return chan_reserved;
}

static const struct of_device_id stm32_dma3_of_match[] = {
	{ .compatible = "st,stm32mp25-dma3", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, stm32_dma3_of_match);

static int stm32_dma3_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct stm32_dma3_ddata *ddata;
	struct reset_control *reset;
	struct stm32_dma3_chan *chan;
	struct dma_device *dma_dev;
	u32 master_ports, chan_reserved, i, verr;
	u64 hwcfgr;
	int ret;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;
	platform_set_drvdata(pdev, ddata);

	dma_dev = &ddata->dma_dev;

	ddata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ddata->base))
		return PTR_ERR(ddata->base);

	ddata->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ddata->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(ddata->clk), "Failed to get clk\n");

	reset = devm_reset_control_get_optional(&pdev->dev, NULL);
	if (IS_ERR(reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(reset), "Failed to get reset\n");

	ret = clk_prepare_enable(ddata->clk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to enable clk\n");

	reset_control_reset(reset);

	INIT_LIST_HEAD(&dma_dev->channels);

	dma_cap_set(DMA_SLAVE, dma_dev->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma_dev->cap_mask);
	dma_cap_set(DMA_CYCLIC, dma_dev->cap_mask);
	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	dma_dev->dev = &pdev->dev;
	/*
	 * This controller supports up to 8-byte buswidth depending on the port used and the
	 * channel, and can only access address at even boundaries, multiple of the buswidth.
	 */
	dma_dev->copy_align = DMAENGINE_ALIGN_8_BYTES;
	dma_dev->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
				   BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
				   BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
				   BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
	dma_dev->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
				   BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
				   BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
				   BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
	dma_dev->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV) | BIT(DMA_MEM_TO_MEM);

	dma_dev->descriptor_reuse = true;
	dma_dev->max_sg_burst = STM32_DMA3_MAX_SEG_SIZE;
	dma_dev->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	dma_dev->device_alloc_chan_resources = stm32_dma3_alloc_chan_resources;
	dma_dev->device_free_chan_resources = stm32_dma3_free_chan_resources;
	dma_dev->device_prep_dma_memcpy = stm32_dma3_prep_dma_memcpy;
	dma_dev->device_prep_slave_sg = stm32_dma3_prep_slave_sg;
	dma_dev->device_prep_dma_cyclic = stm32_dma3_prep_dma_cyclic;
	dma_dev->device_caps = stm32_dma3_caps;
	dma_dev->device_config = stm32_dma3_config;
	dma_dev->device_pause = stm32_dma3_pause;
	dma_dev->device_resume = stm32_dma3_resume;
	dma_dev->device_terminate_all = stm32_dma3_terminate_all;
	dma_dev->device_synchronize = stm32_dma3_synchronize;
	dma_dev->device_tx_status = stm32_dma3_tx_status;
	dma_dev->device_issue_pending = stm32_dma3_issue_pending;

	/* if dma_channels is not modified, get it from hwcfgr1 */
	if (of_property_read_u32(np, "dma-channels", &ddata->dma_channels)) {
		hwcfgr = readl_relaxed(ddata->base + STM32_DMA3_HWCFGR1);
		ddata->dma_channels = FIELD_GET(G_NUM_CHANNELS, hwcfgr);
	}

	/* if dma_requests is not modified, get it from hwcfgr2 */
	if (of_property_read_u32(np, "dma-requests", &ddata->dma_requests)) {
		hwcfgr = readl_relaxed(ddata->base + STM32_DMA3_HWCFGR2);
		ddata->dma_requests = FIELD_GET(G_MAX_REQ_ID, hwcfgr) + 1;
	}

	/* G_MASTER_PORTS, G_M0_DATA_WIDTH_ENC, G_M1_DATA_WIDTH_ENC in HWCFGR1 */
	hwcfgr = readl_relaxed(ddata->base + STM32_DMA3_HWCFGR1);
	master_ports = FIELD_GET(G_MASTER_PORTS, hwcfgr);

	ddata->ports_max_dw[0] = FIELD_GET(G_M0_DATA_WIDTH_ENC, hwcfgr);
	if (master_ports == AXI64 || master_ports == AHB32) /* Single master port */
		ddata->ports_max_dw[1] = DW_INVALID;
	else /* Dual master ports */
		ddata->ports_max_dw[1] = FIELD_GET(G_M1_DATA_WIDTH_ENC, hwcfgr);

	ddata->chans = devm_kcalloc(&pdev->dev, ddata->dma_channels, sizeof(*ddata->chans),
				    GFP_KERNEL);
	if (!ddata->chans) {
		ret = -ENOMEM;
		goto err_clk_disable;
	}

	chan_reserved = stm32_dma3_check_rif(ddata);

	if (chan_reserved == GENMASK(ddata->dma_channels - 1, 0)) {
		ret = -ENODEV;
		dev_err_probe(&pdev->dev, ret, "No channel available, abort registration\n");
		goto err_clk_disable;
	}

	/* G_FIFO_SIZE x=0..7 in HWCFGR3 and G_FIFO_SIZE x=8..15 in HWCFGR4 */
	hwcfgr = readl_relaxed(ddata->base + STM32_DMA3_HWCFGR3);
	hwcfgr |= ((u64)readl_relaxed(ddata->base + STM32_DMA3_HWCFGR4)) << 32;

	for (i = 0; i < ddata->dma_channels; i++) {
		if (chan_reserved & BIT(i))
			continue;

		chan = &ddata->chans[i];
		chan->id = i;
		chan->fifo_size = get_chan_hwcfg(i, G_FIFO_SIZE(i), hwcfgr);
		/* If chan->fifo_size > 0 then half of the fifo size, else no burst when no FIFO */
		chan->max_burst = (chan->fifo_size) ? (1 << (chan->fifo_size + 1)) / 2 : 0;
	}

	ret = dmaenginem_async_device_register(dma_dev);
	if (ret)
		goto err_clk_disable;

	for (i = 0; i < ddata->dma_channels; i++) {
		char name[12];

		if (chan_reserved & BIT(i))
			continue;

		chan = &ddata->chans[i];
		snprintf(name, sizeof(name), "dma%dchan%d", ddata->dma_dev.dev_id, chan->id);

		chan->vchan.desc_free = stm32_dma3_chan_vdesc_free;
		vchan_init(&chan->vchan, dma_dev);

		ret = dma_async_device_channel_register(&ddata->dma_dev, &chan->vchan.chan, name);
		if (ret) {
			dev_err_probe(&pdev->dev, ret, "Failed to register channel %s\n", name);
			goto err_clk_disable;
		}

		ret = platform_get_irq(pdev, i);
		if (ret < 0)
			goto err_clk_disable;
		chan->irq = ret;

		ret = devm_request_irq(&pdev->dev, chan->irq, stm32_dma3_chan_irq, 0,
				       dev_name(chan2dev(chan)), chan);
		if (ret) {
			dev_err_probe(&pdev->dev, ret, "Failed to request channel %s IRQ\n",
				      dev_name(chan2dev(chan)));
			goto err_clk_disable;
		}
	}

	ret = of_dma_controller_register(np, stm32_dma3_of_xlate, ddata);
	if (ret) {
		dev_err_probe(&pdev->dev, ret, "Failed to register controller\n");
		goto err_clk_disable;
	}

	verr = readl_relaxed(ddata->base + STM32_DMA3_VERR);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_put(&pdev->dev);

	dev_info(&pdev->dev, "STM32 DMA3 registered rev:%lu.%lu\n",
		 FIELD_GET(VERR_MAJREV, verr), FIELD_GET(VERR_MINREV, verr));

	return 0;

err_clk_disable:
	clk_disable_unprepare(ddata->clk);

	return ret;
}

static void stm32_dma3_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static int stm32_dma3_runtime_suspend(struct device *dev)
{
	struct stm32_dma3_ddata *ddata = dev_get_drvdata(dev);

	clk_disable_unprepare(ddata->clk);

	return 0;
}

static int stm32_dma3_runtime_resume(struct device *dev)
{
	struct stm32_dma3_ddata *ddata = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(ddata->clk);
	if (ret)
		dev_err(dev, "Failed to enable clk: %d\n", ret);

	return ret;
}

static const struct dev_pm_ops stm32_dma3_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	RUNTIME_PM_OPS(stm32_dma3_runtime_suspend, stm32_dma3_runtime_resume, NULL)
};

static struct platform_driver stm32_dma3_driver = {
	.probe = stm32_dma3_probe,
	.remove_new = stm32_dma3_remove,
	.driver = {
		.name = "stm32-dma3",
		.of_match_table = stm32_dma3_of_match,
		.pm = pm_ptr(&stm32_dma3_pm_ops),
	},
};

static int __init stm32_dma3_init(void)
{
	return platform_driver_register(&stm32_dma3_driver);
}

subsys_initcall(stm32_dma3_init);

MODULE_DESCRIPTION("STM32 DMA3 controller driver");
MODULE_AUTHOR("Amelie Delaunay <amelie.delaunay@foss.st.com>");
MODULE_LICENSE("GPL");
