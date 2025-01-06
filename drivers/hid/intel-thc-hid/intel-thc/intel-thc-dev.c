/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#include <linux/regmap.h>

#include "intel-thc-dev.h"

static int thc_regmap_read(void *context, unsigned int reg,
			   unsigned int *val)
{
	struct thc_device *thc_ctx = context;
	void __iomem *base = thc_ctx->mmio_addr;

	*val = ioread32(base + reg);
	return 0;
}

static int thc_regmap_write(void *context, unsigned int reg,
			    unsigned int val)
{
	struct thc_device *thc_ctx = context;
	void __iomem *base = thc_ctx->mmio_addr;

	iowrite32(val, base + reg);
	return 0;
}

static const struct regmap_range thc_rw_ranges[] = {
	regmap_reg_range(0x10, 0x14),
	regmap_reg_range(0x1000, 0x1320),
};

static const struct regmap_access_table thc_rw_table = {
	.yes_ranges = thc_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(thc_rw_ranges),
};

static const struct regmap_config thc_regmap_cfg = {
	.name = "thc_regmap_common",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x1320,
	.reg_read = thc_regmap_read,
	.reg_write = thc_regmap_write,
	.cache_type = REGCACHE_NONE,
	.fast_io = true,
	.rd_table = &thc_rw_table,
	.wr_table = &thc_rw_table,
	.volatile_table	= &thc_rw_table,
};

/**
 * thc_clear_state - Clear THC hardware state
 *
 * @dev: The pointer of THC device structure
 */
static void thc_clear_state(const struct thc_device *dev)
{
	u32 val;

	/* Clear interrupt cause register */
	val = THC_M_PRT_ERR_CAUSE_INVLD_DEV_ENTRY |
	      THC_M_PRT_ERR_CAUSE_FRAME_BABBLE_ERR |
	      THC_M_PRT_ERR_CAUSE_BUF_OVRRUN_ERR |
	      THC_M_PRT_ERR_CAUSE_PRD_ENTRY_ERR;
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_ERR_CAUSE_OFFSET, val, val);

	/* Clear interrupt error state */
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_CNTRL_1_OFFSET,
			  THC_M_PRT_READ_DMA_CNTRL_IE_STALL,
			  THC_M_PRT_READ_DMA_CNTRL_IE_STALL);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_CNTRL_2_OFFSET,
			  THC_M_PRT_READ_DMA_CNTRL_IE_STALL,
			  THC_M_PRT_READ_DMA_CNTRL_IE_STALL);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			  THC_M_PRT_INT_STATUS_TXN_ERR_INT_STS,
			  THC_M_PRT_INT_STATUS_TXN_ERR_INT_STS);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_INT_STATUS_OFFSET,
			  THC_M_PRT_INT_STATUS_FATAL_ERR_INT_STS,
			  THC_M_PRT_INT_STATUS_FATAL_ERR_INT_STS);

	val = THC_M_PRT_INT_EN_TXN_ERR_INT_EN |
	      THC_M_PRT_INT_EN_FATAL_ERR_INT_EN |
	      THC_M_PRT_INT_EN_BUF_OVRRUN_ERR_INT_EN;
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_INT_EN_OFFSET, val, val);

	val = THC_M_PRT_SW_SEQ_STS_THC_SS_ERR |
	      THC_M_PRT_SW_SEQ_STS_TSSDONE;
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_SW_SEQ_STS_OFFSET, val, val);

	/* Clear RxDMA state */
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_CNTRL_1_OFFSET,
			  THC_M_PRT_READ_DMA_CNTRL_IE_EOF, 0);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_CNTRL_2_OFFSET,
			  THC_M_PRT_READ_DMA_CNTRL_IE_EOF, 0);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_INT_STS_1_OFFSET,
			  THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS,
			  THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_INT_STS_2_OFFSET,
			  THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS,
			  THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_INT_STS_1_OFFSET,
			  THC_M_PRT_READ_DMA_INT_STS_NONDMA_INT_STS,
			  THC_M_PRT_READ_DMA_INT_STS_NONDMA_INT_STS);

	/* Clear TxDMA state */
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_WRITE_DMA_CNTRL_OFFSET,
			  THC_M_PRT_WRITE_DMA_CNTRL_THC_WRDMA_IE_IOC_DMACPL,
			  THC_M_PRT_WRITE_DMA_CNTRL_THC_WRDMA_IE_IOC_DMACPL);

	val = THC_M_PRT_WRITE_INT_STS_THC_WRDMA_ERROR_STS |
	      THC_M_PRT_WRITE_INT_STS_THC_WRDMA_IOC_STS |
	      THC_M_PRT_WRITE_INT_STS_THC_WRDMA_CMPL_STATUS;
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_WRITE_INT_STS_OFFSET, val, val);

	/* Reset all DMAs count */
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_DB_CNT_1_OFFSET,
			  THC_M_PRT_DB_CNT_1_THC_M_PRT_DB_CNT_RST,
			  THC_M_PRT_DB_CNT_1_THC_M_PRT_DB_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_DEVINT_CNT_OFFSET,
			  THC_M_PRT_DEVINT_CNT_THC_M_PRT_DEVINT_CNT_RST,
			  THC_M_PRT_DEVINT_CNT_THC_M_PRT_DEVINT_CNT_RST);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_READ_DMA_CNTRL_1_OFFSET,
			  THC_M_PRT_READ_DMA_CNTRL_TPCPR,
			  THC_M_PRT_READ_DMA_CNTRL_TPCPR);

	/* Reset THC hardware sequence state */
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_FRAME_DROP_CNT_1_OFFSET,
			  THC_M_PRT_FRAME_DROP_CNT_1_RFDC,
			  THC_M_PRT_FRAME_DROP_CNT_1_RFDC);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_FRAME_DROP_CNT_2_OFFSET,
			  THC_M_PRT_FRAME_DROP_CNT_2_RFDC,
			  THC_M_PRT_FRAME_DROP_CNT_2_RFDC);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_FRM_CNT_1_OFFSET,
			  THC_M_PRT_FRM_CNT_1_THC_M_PRT_FRM_CNT_RST,
			  THC_M_PRT_FRM_CNT_1_THC_M_PRT_FRM_CNT_RST);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_FRM_CNT_2_OFFSET,
			  THC_M_PRT_FRM_CNT_2_THC_M_PRT_FRM_CNT_RST,
			  THC_M_PRT_FRM_CNT_2_THC_M_PRT_FRM_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_RXDMA_PKT_CNT_1_OFFSET,
			  THC_M_PRT_RXDMA_PKT_CNT_1_THC_M_PRT_RXDMA_PKT_CNT_RST,
			  THC_M_PRT_RXDMA_PKT_CNT_1_THC_M_PRT_RXDMA_PKT_CNT_RST);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_RXDMA_PKT_CNT_2_OFFSET,
			  THC_M_PRT_RXDMA_PKT_CNT_2_THC_M_PRT_RXDMA_PKT_CNT_RST,
			  THC_M_PRT_RXDMA_PKT_CNT_2_THC_M_PRT_RXDMA_PKT_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_SWINT_CNT_1_OFFSET,
			  THC_M_PRT_SWINT_CNT_1_THC_M_PRT_SWINT_CNT_RST,
			  THC_M_PRT_SWINT_CNT_1_THC_M_PRT_SWINT_CNT_RST);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_SWINT_CNT_1_OFFSET,
			  THC_M_PRT_SWINT_CNT_1_THC_M_PRT_SWINT_CNT_RST,
			  THC_M_PRT_SWINT_CNT_1_THC_M_PRT_SWINT_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_TX_FRM_CNT_OFFSET,
			  THC_M_PRT_TX_FRM_CNT_THC_M_PRT_TX_FRM_CNT_RST,
			  THC_M_PRT_TX_FRM_CNT_THC_M_PRT_TX_FRM_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_TXDMA_PKT_CNT_OFFSET,
			  THC_M_PRT_TXDMA_PKT_CNT_THC_M_PRT_TXDMA_PKT_CNT_RST,
			  THC_M_PRT_TXDMA_PKT_CNT_THC_M_PRT_TXDMA_PKT_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_UFRM_CNT_1_OFFSET,
			  THC_M_PRT_UFRM_CNT_1_THC_M_PRT_UFRM_CNT_RST,
			  THC_M_PRT_UFRM_CNT_1_THC_M_PRT_UFRM_CNT_RST);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_UFRM_CNT_2_OFFSET,
			  THC_M_PRT_UFRM_CNT_2_THC_M_PRT_UFRM_CNT_RST,
			  THC_M_PRT_UFRM_CNT_2_THC_M_PRT_UFRM_CNT_RST);

	regmap_write_bits(dev->thc_regmap, THC_M_PRT_PRD_EMPTY_CNT_1_OFFSET,
			  THC_M_PRT_PRD_EMPTY_CNT_1_RPTEC,
			  THC_M_PRT_PRD_EMPTY_CNT_1_RPTEC);
	regmap_write_bits(dev->thc_regmap, THC_M_PRT_PRD_EMPTY_CNT_2_OFFSET,
			  THC_M_PRT_PRD_EMPTY_CNT_2_RPTEC,
			  THC_M_PRT_PRD_EMPTY_CNT_2_RPTEC);
}

/**
 * thc_dev_init - Allocate and initialize the THC device structure
 *
 * @device: The pointer of device structure
 * @mem_addr: The pointer of MMIO memory address
 *
 * Return: The thc_device pointer on success, NULL on failed.
 */
struct thc_device *thc_dev_init(struct device *device, void __iomem *mem_addr)
{
	struct thc_device *thc_dev;
	int ret;

	thc_dev = devm_kzalloc(device, sizeof(*thc_dev), GFP_KERNEL);
	if (!thc_dev)
		return ERR_PTR(-ENOMEM);

	thc_dev->dev = device;
	thc_dev->mmio_addr = mem_addr;
	thc_dev->thc_regmap = devm_regmap_init(device, NULL, thc_dev, &thc_regmap_cfg);
	if (IS_ERR(thc_dev->thc_regmap)) {
		ret = PTR_ERR(thc_dev->thc_regmap);
		dev_err_once(device, "Failed to init thc_regmap: %d\n", ret);
		return ERR_PTR(ret);
	}

	thc_clear_state(thc_dev);

	return thc_dev;
}
EXPORT_SYMBOL_NS_GPL(thc_dev_init, "INTEL_THC");

MODULE_AUTHOR("Xinpeng Sun <xinpeng.sun@intel.com>");
MODULE_AUTHOR("Even Xu <even.xu@intel.com>");

MODULE_DESCRIPTION("Intel(R) Intel THC Hardware Driver");
MODULE_LICENSE("GPL");
