// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the HiSilicon SEC units found on Hip06 Hip07
 *
 * Copyright (c) 2016-2017 HiSilicon Limited.
 */
#include <linux/acpi.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "sec_drv.h"

#define SEC_QUEUE_AR_FROCE_ALLOC			0
#define SEC_QUEUE_AR_FROCE_NOALLOC			1
#define SEC_QUEUE_AR_FROCE_DIS				2

#define SEC_QUEUE_AW_FROCE_ALLOC			0
#define SEC_QUEUE_AW_FROCE_NOALLOC			1
#define SEC_QUEUE_AW_FROCE_DIS				2

/* SEC_ALGSUB registers */
#define SEC_ALGSUB_CLK_EN_REG				0x03b8
#define SEC_ALGSUB_CLK_DIS_REG				0x03bc
#define SEC_ALGSUB_CLK_ST_REG				0x535c
#define SEC_ALGSUB_RST_REQ_REG				0x0aa8
#define SEC_ALGSUB_RST_DREQ_REG				0x0aac
#define SEC_ALGSUB_RST_ST_REG				0x5a54
#define   SEC_ALGSUB_RST_ST_IS_RST			BIT(0)

#define SEC_ALGSUB_BUILD_RST_REQ_REG			0x0ab8
#define SEC_ALGSUB_BUILD_RST_DREQ_REG			0x0abc
#define SEC_ALGSUB_BUILD_RST_ST_REG			0x5a5c
#define   SEC_ALGSUB_BUILD_RST_ST_IS_RST		BIT(0)

#define SEC_SAA_BASE					0x00001000UL

/* SEC_SAA registers */
#define SEC_SAA_CTRL_REG(x)	((x) * SEC_SAA_ADDR_SIZE)
#define   SEC_SAA_CTRL_GET_QM_EN			BIT(0)

#define SEC_ST_INTMSK1_REG				0x0200
#define SEC_ST_RINT1_REG				0x0400
#define SEC_ST_INTSTS1_REG				0x0600
#define SEC_BD_MNG_STAT_REG				0x0800
#define SEC_PARSING_STAT_REG				0x0804
#define SEC_LOAD_TIME_OUT_CNT_REG			0x0808
#define SEC_CORE_WORK_TIME_OUT_CNT_REG			0x080c
#define SEC_BACK_TIME_OUT_CNT_REG			0x0810
#define SEC_BD1_PARSING_RD_TIME_OUT_CNT_REG		0x0814
#define SEC_BD1_PARSING_WR_TIME_OUT_CNT_REG		0x0818
#define SEC_BD2_PARSING_RD_TIME_OUT_CNT_REG		0x081c
#define SEC_BD2_PARSING_WR_TIME_OUT_CNT_REG		0x0820
#define SEC_SAA_ACC_REG					0x083c
#define SEC_BD_NUM_CNT_IN_SEC_REG			0x0858
#define SEC_LOAD_WORK_TIME_CNT_REG			0x0860
#define SEC_CORE_WORK_WORK_TIME_CNT_REG			0x0864
#define SEC_BACK_WORK_TIME_CNT_REG			0x0868
#define SEC_SAA_IDLE_TIME_CNT_REG			0x086c
#define SEC_SAA_CLK_CNT_REG				0x0870

/* SEC_COMMON registers */
#define SEC_CLK_EN_REG					0x0000
#define SEC_CTRL_REG					0x0004

#define SEC_COMMON_CNT_CLR_CE_REG			0x0008
#define   SEC_COMMON_CNT_CLR_CE_CLEAR			BIT(0)
#define   SEC_COMMON_CNT_CLR_CE_SNAP_EN			BIT(1)

#define SEC_SECURE_CTRL_REG				0x000c
#define SEC_AXI_CACHE_CFG_REG				0x0010
#define SEC_AXI_QOS_CFG_REG				0x0014
#define SEC_IPV4_MASK_TABLE_REG				0x0020
#define SEC_IPV6_MASK_TABLE_X_REG(x)	(0x0024 + (x) * 4)
#define SEC_FSM_MAX_CNT_REG				0x0064

#define SEC_CTRL2_REG					0x0068
#define   SEC_CTRL2_DATA_AXI_RD_OTSD_CFG_M		GENMASK(3, 0)
#define   SEC_CTRL2_DATA_AXI_RD_OTSD_CFG_S		0
#define   SEC_CTRL2_DATA_AXI_WR_OTSD_CFG_M		GENMASK(6, 4)
#define   SEC_CTRL2_DATA_AXI_WR_OTSD_CFG_S		4
#define   SEC_CTRL2_CLK_GATE_EN				BIT(7)
#define   SEC_CTRL2_ENDIAN_BD				BIT(8)
#define   SEC_CTRL2_ENDIAN_BD_TYPE			BIT(9)

#define SEC_CNT_PRECISION_CFG_REG			0x006c
#define SEC_DEBUG_BD_CFG_REG				0x0070
#define   SEC_DEBUG_BD_CFG_WB_NORMAL			BIT(0)
#define   SEC_DEBUG_BD_CFG_WB_EN			BIT(1)

#define SEC_Q_SIGHT_SEL					0x0074
#define SEC_Q_SIGHT_HIS_CLR				0x0078
#define SEC_Q_VMID_CFG_REG(q)		(0x0100 + (q) * 4)
#define SEC_Q_WEIGHT_CFG_REG(q)		(0x200 + (q) * 4)
#define SEC_STAT_CLR_REG				0x0a00
#define SEC_SAA_IDLE_CNT_CLR_REG			0x0a04
#define SEC_QM_CPL_Q_IDBUF_DFX_CFG_REG			0x0b00
#define SEC_QM_CPL_Q_IDBUF_DFX_RESULT_REG		0x0b04
#define SEC_QM_BD_DFX_CFG_REG				0x0b08
#define SEC_QM_BD_DFX_RESULT_REG			0x0b0c
#define SEC_QM_BDID_DFX_RESULT_REG			0x0b10
#define SEC_QM_BD_DFIFO_STATUS_REG			0x0b14
#define SEC_QM_BD_DFX_CFG2_REG				0x0b1c
#define SEC_QM_BD_DFX_RESULT2_REG			0x0b20
#define SEC_QM_BD_IDFIFO_STATUS_REG			0x0b18
#define SEC_QM_BD_DFIFO_STATUS2_REG			0x0b28
#define SEC_QM_BD_IDFIFO_STATUS2_REG			0x0b2c

#define SEC_HASH_IPV4_MASK				0xfff00000
#define SEC_MAX_SAA_NUM					0xa
#define SEC_SAA_ADDR_SIZE				0x1000

#define SEC_Q_INIT_REG					0x0
#define   SEC_Q_INIT_WO_STAT_CLEAR			0x2
#define   SEC_Q_INIT_AND_STAT_CLEAR			0x3

#define SEC_Q_CFG_REG					0x8
#define   SEC_Q_CFG_REORDER				BIT(0)

#define SEC_Q_PROC_NUM_CFG_REG				0x10
#define SEC_QUEUE_ENB_REG				0x18

#define SEC_Q_DEPTH_CFG_REG				0x50
#define   SEC_Q_DEPTH_CFG_DEPTH_M			GENMASK(11, 0)
#define   SEC_Q_DEPTH_CFG_DEPTH_S			0

#define SEC_Q_BASE_HADDR_REG				0x54
#define SEC_Q_BASE_LADDR_REG				0x58
#define SEC_Q_WR_PTR_REG				0x5c
#define SEC_Q_OUTORDER_BASE_HADDR_REG			0x60
#define SEC_Q_OUTORDER_BASE_LADDR_REG			0x64
#define SEC_Q_OUTORDER_RD_PTR_REG			0x68
#define SEC_Q_OT_TH_REG					0x6c

#define SEC_Q_ARUSER_CFG_REG				0x70
#define   SEC_Q_ARUSER_CFG_FA				BIT(0)
#define   SEC_Q_ARUSER_CFG_FNA				BIT(1)
#define   SEC_Q_ARUSER_CFG_RINVLD			BIT(2)
#define   SEC_Q_ARUSER_CFG_PKG				BIT(3)

#define SEC_Q_AWUSER_CFG_REG				0x74
#define   SEC_Q_AWUSER_CFG_FA				BIT(0)
#define   SEC_Q_AWUSER_CFG_FNA				BIT(1)
#define   SEC_Q_AWUSER_CFG_PKG				BIT(2)

#define SEC_Q_ERR_BASE_HADDR_REG			0x7c
#define SEC_Q_ERR_BASE_LADDR_REG			0x80
#define SEC_Q_CFG_VF_NUM_REG				0x84
#define SEC_Q_SOFT_PROC_PTR_REG				0x88
#define SEC_Q_FAIL_INT_MSK_REG				0x300
#define SEC_Q_FLOW_INT_MKS_REG				0x304
#define SEC_Q_FAIL_RINT_REG				0x400
#define SEC_Q_FLOW_RINT_REG				0x404
#define SEC_Q_FAIL_INT_STATUS_REG			0x500
#define SEC_Q_FLOW_INT_STATUS_REG			0x504
#define SEC_Q_STATUS_REG				0x600
#define SEC_Q_RD_PTR_REG				0x604
#define SEC_Q_PRO_PTR_REG				0x608
#define SEC_Q_OUTORDER_WR_PTR_REG			0x60c
#define SEC_Q_OT_CNT_STATUS_REG				0x610
#define SEC_Q_INORDER_BD_NUM_ST_REG			0x650
#define SEC_Q_INORDER_GET_FLAG_ST_REG			0x654
#define SEC_Q_INORDER_ADD_FLAG_ST_REG			0x658
#define SEC_Q_INORDER_TASK_INT_NUM_LEFT_ST_REG		0x65c
#define SEC_Q_RD_DONE_PTR_REG				0x660
#define SEC_Q_CPL_Q_BD_NUM_ST_REG			0x700
#define SEC_Q_CPL_Q_PTR_ST_REG				0x704
#define SEC_Q_CPL_Q_H_ADDR_ST_REG			0x708
#define SEC_Q_CPL_Q_L_ADDR_ST_REG			0x70c
#define SEC_Q_CPL_TASK_INT_NUM_LEFT_ST_REG		0x710
#define SEC_Q_WRR_ID_CHECK_REG				0x714
#define SEC_Q_CPLQ_FULL_CHECK_REG			0x718
#define SEC_Q_SUCCESS_BD_CNT_REG			0x800
#define SEC_Q_FAIL_BD_CNT_REG				0x804
#define SEC_Q_GET_BD_CNT_REG				0x808
#define SEC_Q_IVLD_CNT_REG				0x80c
#define SEC_Q_BD_PROC_GET_CNT_REG			0x810
#define SEC_Q_BD_PROC_DONE_CNT_REG			0x814
#define SEC_Q_LAT_CLR_REG				0x850
#define SEC_Q_PKT_LAT_MAX_REG				0x854
#define SEC_Q_PKT_LAT_AVG_REG				0x858
#define SEC_Q_PKT_LAT_MIN_REG				0x85c
#define SEC_Q_ID_CLR_CFG_REG				0x900
#define SEC_Q_1ST_BD_ERR_ID_REG				0x904
#define SEC_Q_1ST_AUTH_FAIL_ID_REG			0x908
#define SEC_Q_1ST_RD_ERR_ID_REG				0x90c
#define SEC_Q_1ST_ECC2_ERR_ID_REG			0x910
#define SEC_Q_1ST_IVLD_ID_REG				0x914
#define SEC_Q_1ST_BD_WR_ERR_ID_REG			0x918
#define SEC_Q_1ST_ERR_BD_WR_ERR_ID_REG			0x91c
#define SEC_Q_1ST_BD_MAC_WR_ERR_ID_REG			0x920

struct sec_debug_bd_info {
#define SEC_DEBUG_BD_INFO_SOFT_ERR_CHECK_M	GENMASK(22, 0)
	u32 soft_err_check;
#define SEC_DEBUG_BD_INFO_HARD_ERR_CHECK_M	GENMASK(9, 0)
	u32 hard_err_check;
	u32 icv_mac1st_word;
#define SEC_DEBUG_BD_INFO_GET_ID_M		GENMASK(19, 0)
	u32 sec_get_id;
	/* W4---W15 */
	u32 reserv_left[12];
};

struct sec_out_bd_info	{
#define SEC_OUT_BD_INFO_Q_ID_M			GENMASK(11, 0)
#define SEC_OUT_BD_INFO_ECC_2BIT_ERR		BIT(14)
	u16 data;
};

#define SEC_MAX_DEVICES				8
static struct sec_dev_info *sec_devices[SEC_MAX_DEVICES];
static DEFINE_MUTEX(sec_id_lock);

static int sec_queue_map_io(struct sec_queue *queue)
{
	struct device *dev = queue->dev_info->dev;
	struct resource *res;

	res = platform_get_resource(to_platform_device(dev),
				    IORESOURCE_MEM,
				    2 + queue->queue_id);
	if (!res) {
		dev_err(dev, "Failed to get queue %d memory resource\n",
			queue->queue_id);
		return -ENOMEM;
	}
	queue->regs = ioremap(res->start, resource_size(res));
	if (!queue->regs)
		return -ENOMEM;

	return 0;
}

static void sec_queue_unmap_io(struct sec_queue *queue)
{
	 iounmap(queue->regs);
}

static int sec_queue_ar_pkgattr(struct sec_queue *queue, u32 ar_pkg)
{
	void __iomem *addr = queue->regs +  SEC_Q_ARUSER_CFG_REG;
	u32 regval;

	regval = readl_relaxed(addr);
	if (ar_pkg)
		regval |= SEC_Q_ARUSER_CFG_PKG;
	else
		regval &= ~SEC_Q_ARUSER_CFG_PKG;
	writel_relaxed(regval, addr);

	return 0;
}

static int sec_queue_aw_pkgattr(struct sec_queue *queue, u32 aw_pkg)
{
	void __iomem *addr = queue->regs + SEC_Q_AWUSER_CFG_REG;
	u32 regval;

	regval = readl_relaxed(addr);
	regval |= SEC_Q_AWUSER_CFG_PKG;
	writel_relaxed(regval, addr);

	return 0;
}

static int sec_clk_en(struct sec_dev_info *info)
{
	void __iomem *base = info->regs[SEC_COMMON];
	u32 i = 0;

	writel_relaxed(0x7, base + SEC_ALGSUB_CLK_EN_REG);
	do {
		usleep_range(1000, 10000);
		if ((readl_relaxed(base + SEC_ALGSUB_CLK_ST_REG) & 0x7) == 0x7)
			return 0;
		i++;
	} while (i < 10);
	dev_err(info->dev, "sec clock enable fail!\n");

	return -EIO;
}

static int sec_clk_dis(struct sec_dev_info *info)
{
	void __iomem *base = info->regs[SEC_COMMON];
	u32 i = 0;

	writel_relaxed(0x7, base + SEC_ALGSUB_CLK_DIS_REG);
	do {
		usleep_range(1000, 10000);
		if ((readl_relaxed(base + SEC_ALGSUB_CLK_ST_REG) & 0x7) == 0)
			return 0;
		i++;
	} while (i < 10);
	dev_err(info->dev, "sec clock disable fail!\n");

	return -EIO;
}

static int sec_reset_whole_module(struct sec_dev_info *info)
{
	void __iomem *base = info->regs[SEC_COMMON];
	bool is_reset, b_is_reset;
	u32 i = 0;

	writel_relaxed(1, base + SEC_ALGSUB_RST_REQ_REG);
	writel_relaxed(1, base + SEC_ALGSUB_BUILD_RST_REQ_REG);
	while (1) {
		usleep_range(1000, 10000);
		is_reset = readl_relaxed(base + SEC_ALGSUB_RST_ST_REG) &
			SEC_ALGSUB_RST_ST_IS_RST;
		b_is_reset = readl_relaxed(base + SEC_ALGSUB_BUILD_RST_ST_REG) &
			SEC_ALGSUB_BUILD_RST_ST_IS_RST;
		if (is_reset && b_is_reset)
			break;
		i++;
		if (i > 10) {
			dev_err(info->dev, "Reset req failed\n");
			return -EIO;
		}
	}

	i = 0;
	writel_relaxed(1, base + SEC_ALGSUB_RST_DREQ_REG);
	writel_relaxed(1, base + SEC_ALGSUB_BUILD_RST_DREQ_REG);
	while (1) {
		usleep_range(1000, 10000);
		is_reset = readl_relaxed(base + SEC_ALGSUB_RST_ST_REG) &
			SEC_ALGSUB_RST_ST_IS_RST;
		b_is_reset = readl_relaxed(base + SEC_ALGSUB_BUILD_RST_ST_REG) &
			SEC_ALGSUB_BUILD_RST_ST_IS_RST;
		if (!is_reset && !b_is_reset)
			break;

		i++;
		if (i > 10) {
			dev_err(info->dev, "Reset dreq failed\n");
			return -EIO;
		}
	}

	return 0;
}

static void sec_bd_endian_little(struct sec_dev_info *info)
{
	void __iomem *addr = info->regs[SEC_SAA] + SEC_CTRL2_REG;
	u32 regval;

	regval = readl_relaxed(addr);
	regval &= ~(SEC_CTRL2_ENDIAN_BD | SEC_CTRL2_ENDIAN_BD_TYPE);
	writel_relaxed(regval, addr);
}

/*
 * sec_cache_config - configure optimum cache placement
 */
static void sec_cache_config(struct sec_dev_info *info)
{
	struct iommu_domain *domain;
	void __iomem *addr = info->regs[SEC_SAA] + SEC_CTRL_REG;

	domain = iommu_get_domain_for_dev(info->dev);

	/* Check that translation is occurring */
	if (domain && (domain->type & __IOMMU_DOMAIN_PAGING))
		writel_relaxed(0x44cf9e, addr);
	else
		writel_relaxed(0x4cfd9, addr);
}

static void sec_data_axiwr_otsd_cfg(struct sec_dev_info *info, u32 cfg)
{
	void __iomem *addr = info->regs[SEC_SAA] + SEC_CTRL2_REG;
	u32 regval;

	regval = readl_relaxed(addr);
	regval &= ~SEC_CTRL2_DATA_AXI_WR_OTSD_CFG_M;
	regval |= (cfg << SEC_CTRL2_DATA_AXI_WR_OTSD_CFG_S) &
		SEC_CTRL2_DATA_AXI_WR_OTSD_CFG_M;
	writel_relaxed(regval, addr);
}

static void sec_data_axird_otsd_cfg(struct sec_dev_info *info, u32 cfg)
{
	void __iomem *addr = info->regs[SEC_SAA] + SEC_CTRL2_REG;
	u32 regval;

	regval = readl_relaxed(addr);
	regval &= ~SEC_CTRL2_DATA_AXI_RD_OTSD_CFG_M;
	regval |= (cfg << SEC_CTRL2_DATA_AXI_RD_OTSD_CFG_S) &
		SEC_CTRL2_DATA_AXI_RD_OTSD_CFG_M;
	writel_relaxed(regval, addr);
}

static void sec_clk_gate_en(struct sec_dev_info *info, bool clkgate)
{
	void __iomem *addr = info->regs[SEC_SAA] + SEC_CTRL2_REG;
	u32 regval;

	regval = readl_relaxed(addr);
	if (clkgate)
		regval |= SEC_CTRL2_CLK_GATE_EN;
	else
		regval &= ~SEC_CTRL2_CLK_GATE_EN;
	writel_relaxed(regval, addr);
}

static void sec_comm_cnt_cfg(struct sec_dev_info *info, bool clr_ce)
{
	void __iomem *addr = info->regs[SEC_SAA] + SEC_COMMON_CNT_CLR_CE_REG;
	u32 regval;

	regval = readl_relaxed(addr);
	if (clr_ce)
		regval |= SEC_COMMON_CNT_CLR_CE_CLEAR;
	else
		regval &= ~SEC_COMMON_CNT_CLR_CE_CLEAR;
	writel_relaxed(regval, addr);
}

static void sec_commsnap_en(struct sec_dev_info *info, bool snap_en)
{
	void __iomem *addr = info->regs[SEC_SAA] + SEC_COMMON_CNT_CLR_CE_REG;
	u32 regval;

	regval = readl_relaxed(addr);
	if (snap_en)
		regval |= SEC_COMMON_CNT_CLR_CE_SNAP_EN;
	else
		regval &= ~SEC_COMMON_CNT_CLR_CE_SNAP_EN;
	writel_relaxed(regval, addr);
}

static void sec_ipv6_hashmask(struct sec_dev_info *info, u32 hash_mask[])
{
	void __iomem *base = info->regs[SEC_SAA];
	int i;

	for (i = 0; i < 10; i++)
		writel_relaxed(hash_mask[0],
			       base + SEC_IPV6_MASK_TABLE_X_REG(i));
}

static int sec_ipv4_hashmask(struct sec_dev_info *info, u32 hash_mask)
{
	if (hash_mask & SEC_HASH_IPV4_MASK) {
		dev_err(info->dev, "Sec Ipv4 Hash Mask Input Error!\n ");
		return -EINVAL;
	}

	writel_relaxed(hash_mask,
		       info->regs[SEC_SAA] + SEC_IPV4_MASK_TABLE_REG);

	return 0;
}

static void sec_set_dbg_bd_cfg(struct sec_dev_info *info, u32 cfg)
{
	void __iomem *addr = info->regs[SEC_SAA] + SEC_DEBUG_BD_CFG_REG;
	u32 regval;

	regval = readl_relaxed(addr);
	/* Always disable write back of normal bd */
	regval &= ~SEC_DEBUG_BD_CFG_WB_NORMAL;

	if (cfg)
		regval &= ~SEC_DEBUG_BD_CFG_WB_EN;
	else
		regval |= SEC_DEBUG_BD_CFG_WB_EN;

	writel_relaxed(regval, addr);
}

static void sec_saa_getqm_en(struct sec_dev_info *info, u32 saa_indx, u32 en)
{
	void __iomem *addr = info->regs[SEC_SAA] + SEC_SAA_BASE +
		SEC_SAA_CTRL_REG(saa_indx);
	u32 regval;

	regval = readl_relaxed(addr);
	if (en)
		regval |= SEC_SAA_CTRL_GET_QM_EN;
	else
		regval &= ~SEC_SAA_CTRL_GET_QM_EN;
	writel_relaxed(regval, addr);
}

static void sec_saa_int_mask(struct sec_dev_info *info, u32 saa_indx,
			     u32 saa_int_mask)
{
	writel_relaxed(saa_int_mask,
		       info->regs[SEC_SAA] + SEC_SAA_BASE + SEC_ST_INTMSK1_REG +
		       saa_indx * SEC_SAA_ADDR_SIZE);
}

static void sec_streamid(struct sec_dev_info *info, int i)
{
	#define SEC_SID 0x600
	#define SEC_VMID 0

	writel_relaxed((SEC_VMID | ((SEC_SID & 0xffff) << 8)),
		       info->regs[SEC_SAA] + SEC_Q_VMID_CFG_REG(i));
}

static void sec_queue_ar_alloc(struct sec_queue *queue, u32 alloc)
{
	void __iomem *addr = queue->regs + SEC_Q_ARUSER_CFG_REG;
	u32 regval;

	regval = readl_relaxed(addr);
	if (alloc == SEC_QUEUE_AR_FROCE_ALLOC) {
		regval |= SEC_Q_ARUSER_CFG_FA;
		regval &= ~SEC_Q_ARUSER_CFG_FNA;
	} else {
		regval &= ~SEC_Q_ARUSER_CFG_FA;
		regval |= SEC_Q_ARUSER_CFG_FNA;
	}

	writel_relaxed(regval, addr);
}

static void sec_queue_aw_alloc(struct sec_queue *queue, u32 alloc)
{
	void __iomem *addr = queue->regs + SEC_Q_AWUSER_CFG_REG;
	u32 regval;

	regval = readl_relaxed(addr);
	if (alloc == SEC_QUEUE_AW_FROCE_ALLOC) {
		regval |= SEC_Q_AWUSER_CFG_FA;
		regval &= ~SEC_Q_AWUSER_CFG_FNA;
	} else {
		regval &= ~SEC_Q_AWUSER_CFG_FA;
		regval |= SEC_Q_AWUSER_CFG_FNA;
	}

	writel_relaxed(regval, addr);
}

static void sec_queue_reorder(struct sec_queue *queue, bool reorder)
{
	void __iomem *base = queue->regs;
	u32 regval;

	regval = readl_relaxed(base + SEC_Q_CFG_REG);
	if (reorder)
		regval |= SEC_Q_CFG_REORDER;
	else
		regval &= ~SEC_Q_CFG_REORDER;
	writel_relaxed(regval, base + SEC_Q_CFG_REG);
}

static void sec_queue_depth(struct sec_queue *queue, u32 depth)
{
	void __iomem *addr = queue->regs + SEC_Q_DEPTH_CFG_REG;
	u32 regval;

	regval = readl_relaxed(addr);
	regval &= ~SEC_Q_DEPTH_CFG_DEPTH_M;
	regval |= (depth << SEC_Q_DEPTH_CFG_DEPTH_S) & SEC_Q_DEPTH_CFG_DEPTH_M;

	writel_relaxed(regval, addr);
}

static void sec_queue_cmdbase_addr(struct sec_queue *queue, u64 addr)
{
	writel_relaxed(upper_32_bits(addr), queue->regs + SEC_Q_BASE_HADDR_REG);
	writel_relaxed(lower_32_bits(addr), queue->regs + SEC_Q_BASE_LADDR_REG);
}

static void sec_queue_outorder_addr(struct sec_queue *queue, u64 addr)
{
	writel_relaxed(upper_32_bits(addr),
		       queue->regs + SEC_Q_OUTORDER_BASE_HADDR_REG);
	writel_relaxed(lower_32_bits(addr),
		       queue->regs + SEC_Q_OUTORDER_BASE_LADDR_REG);
}

static void sec_queue_errbase_addr(struct sec_queue *queue, u64 addr)
{
	writel_relaxed(upper_32_bits(addr),
		       queue->regs + SEC_Q_ERR_BASE_HADDR_REG);
	writel_relaxed(lower_32_bits(addr),
		       queue->regs + SEC_Q_ERR_BASE_LADDR_REG);
}

static void sec_queue_irq_disable(struct sec_queue *queue)
{
	writel_relaxed((u32)~0, queue->regs + SEC_Q_FLOW_INT_MKS_REG);
}

static void sec_queue_irq_enable(struct sec_queue *queue)
{
	writel_relaxed(0, queue->regs + SEC_Q_FLOW_INT_MKS_REG);
}

static void sec_queue_abn_irq_disable(struct sec_queue *queue)
{
	writel_relaxed((u32)~0, queue->regs + SEC_Q_FAIL_INT_MSK_REG);
}

static void sec_queue_stop(struct sec_queue *queue)
{
	disable_irq(queue->task_irq);
	sec_queue_irq_disable(queue);
	writel_relaxed(0x0, queue->regs + SEC_QUEUE_ENB_REG);
}

static void sec_queue_start(struct sec_queue *queue)
{
	sec_queue_irq_enable(queue);
	enable_irq(queue->task_irq);
	queue->expected = 0;
	writel_relaxed(SEC_Q_INIT_AND_STAT_CLEAR, queue->regs + SEC_Q_INIT_REG);
	writel_relaxed(0x1, queue->regs + SEC_QUEUE_ENB_REG);
}

static struct sec_queue *sec_alloc_queue(struct sec_dev_info *info)
{
	int i;

	mutex_lock(&info->dev_lock);

	/* Get the first idle queue in SEC device */
	for (i = 0; i < SEC_Q_NUM; i++)
		if (!info->queues[i].in_use) {
			info->queues[i].in_use = true;
			info->queues_in_use++;
			mutex_unlock(&info->dev_lock);

			return &info->queues[i];
		}
	mutex_unlock(&info->dev_lock);

	return ERR_PTR(-ENODEV);
}

static int sec_queue_free(struct sec_queue *queue)
{
	struct sec_dev_info *info = queue->dev_info;

	if (queue->queue_id >= SEC_Q_NUM) {
		dev_err(info->dev, "No queue %d\n", queue->queue_id);
		return -ENODEV;
	}

	if (!queue->in_use) {
		dev_err(info->dev, "Queue %d is idle\n", queue->queue_id);
		return -ENODEV;
	}

	mutex_lock(&info->dev_lock);
	queue->in_use = false;
	info->queues_in_use--;
	mutex_unlock(&info->dev_lock);

	return 0;
}

static irqreturn_t sec_isr_handle_th(int irq, void *q)
{
	sec_queue_irq_disable(q);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t sec_isr_handle(int irq, void *q)
{
	struct sec_queue *queue = q;
	struct sec_queue_ring_cmd *msg_ring = &queue->ring_cmd;
	struct sec_queue_ring_cq *cq_ring = &queue->ring_cq;
	struct sec_out_bd_info *outorder_msg;
	struct sec_bd_info *msg;
	u32 ooo_read, ooo_write;
	void __iomem *base = queue->regs;
	int q_id;

	ooo_read = readl(base + SEC_Q_OUTORDER_RD_PTR_REG);
	ooo_write = readl(base + SEC_Q_OUTORDER_WR_PTR_REG);
	outorder_msg = cq_ring->vaddr + ooo_read;
	q_id = outorder_msg->data & SEC_OUT_BD_INFO_Q_ID_M;
	msg = msg_ring->vaddr + q_id;

	while ((ooo_write != ooo_read) && msg->w0 & SEC_BD_W0_DONE) {
		/*
		 * Must be before callback otherwise blocks adding other chained
		 * elements
		 */
		set_bit(q_id, queue->unprocessed);
		if (q_id == queue->expected)
			while (test_bit(queue->expected, queue->unprocessed)) {
				clear_bit(queue->expected, queue->unprocessed);
				msg = msg_ring->vaddr + queue->expected;
				msg->w0 &= ~SEC_BD_W0_DONE;
				msg_ring->callback(msg,
						queue->shadow[queue->expected]);
				queue->shadow[queue->expected] = NULL;
				queue->expected = (queue->expected + 1) %
					SEC_QUEUE_LEN;
				atomic_dec(&msg_ring->used);
			}

		ooo_read = (ooo_read + 1) % SEC_QUEUE_LEN;
		writel(ooo_read, base + SEC_Q_OUTORDER_RD_PTR_REG);
		ooo_write = readl(base + SEC_Q_OUTORDER_WR_PTR_REG);
		outorder_msg = cq_ring->vaddr + ooo_read;
		q_id = outorder_msg->data & SEC_OUT_BD_INFO_Q_ID_M;
		msg = msg_ring->vaddr + q_id;
	}

	sec_queue_irq_enable(queue);

	return IRQ_HANDLED;
}

static int sec_queue_irq_init(struct sec_queue *queue)
{
	struct sec_dev_info *info = queue->dev_info;
	int irq = queue->task_irq;
	int ret;

	ret = request_threaded_irq(irq, sec_isr_handle_th, sec_isr_handle,
				   IRQF_TRIGGER_RISING, queue->name, queue);
	if (ret) {
		dev_err(info->dev, "request irq(%d) failed %d\n", irq, ret);
		return ret;
	}
	disable_irq(irq);

	return 0;
}

static int sec_queue_irq_uninit(struct sec_queue *queue)
{
	free_irq(queue->task_irq, queue);

	return 0;
}

static struct sec_dev_info *sec_device_get(void)
{
	struct sec_dev_info *sec_dev = NULL;
	struct sec_dev_info *this_sec_dev;
	int least_busy_n = SEC_Q_NUM + 1;
	int i;

	/* Find which one is least busy and use that first */
	for (i = 0; i < SEC_MAX_DEVICES; i++) {
		this_sec_dev = sec_devices[i];
		if (this_sec_dev &&
		    this_sec_dev->queues_in_use < least_busy_n) {
			least_busy_n = this_sec_dev->queues_in_use;
			sec_dev = this_sec_dev;
		}
	}

	return sec_dev;
}

static struct sec_queue *sec_queue_alloc_start(struct sec_dev_info *info)
{
	struct sec_queue *queue;

	queue = sec_alloc_queue(info);
	if (IS_ERR(queue)) {
		dev_err(info->dev, "alloc sec queue failed! %ld\n",
			PTR_ERR(queue));
		return queue;
	}

	sec_queue_start(queue);

	return queue;
}

/**
 * sec_queue_alloc_start_safe - get a hw queue from appropriate instance
 *
 * This function does extremely simplistic load balancing. It does not take into
 * account NUMA locality of the accelerator, or which cpu has requested the
 * queue.  Future work may focus on optimizing this in order to improve full
 * machine throughput.
 */
struct sec_queue *sec_queue_alloc_start_safe(void)
{
	struct sec_dev_info *info;
	struct sec_queue *queue = ERR_PTR(-ENODEV);

	mutex_lock(&sec_id_lock);
	info = sec_device_get();
	if (!info)
		goto unlock;

	queue = sec_queue_alloc_start(info);

unlock:
	mutex_unlock(&sec_id_lock);

	return queue;
}

/**
 * sec_queue_stop_release() - free up a hw queue for reuse
 * @queue: The queue we are done with.
 *
 * This will stop the current queue, terminanting any transactions
 * that are inflight an return it to the pool of available hw queuess
 */
int sec_queue_stop_release(struct sec_queue *queue)
{
	struct device *dev = queue->dev_info->dev;
	int ret;

	sec_queue_stop(queue);

	ret = sec_queue_free(queue);
	if (ret)
		dev_err(dev, "Releasing queue failed %d\n", ret);

	return ret;
}

/**
 * sec_queue_empty() - Is this hardware queue currently empty.
 * @queue: The queue to test
 *
 * We need to know if we have an empty queue for some of the chaining modes
 * as if it is not empty we may need to hold the message in a software queue
 * until the hw queue is drained.
 */
bool sec_queue_empty(struct sec_queue *queue)
{
	struct sec_queue_ring_cmd *msg_ring = &queue->ring_cmd;

	return !atomic_read(&msg_ring->used);
}

/**
 * sec_queue_send() - queue up a single operation in the hw queue
 * @queue: The queue in which to put the message
 * @msg: The message
 * @ctx: Context to be put in the shadow array and passed back to cb on result.
 *
 * This function will return -EAGAIN if the queue is currently full.
 */
int sec_queue_send(struct sec_queue *queue, struct sec_bd_info *msg, void *ctx)
{
	struct sec_queue_ring_cmd *msg_ring = &queue->ring_cmd;
	void __iomem *base = queue->regs;
	u32 write, read;

	mutex_lock(&msg_ring->lock);
	read = readl(base + SEC_Q_RD_PTR_REG);
	write = readl(base + SEC_Q_WR_PTR_REG);
	if (write == read && atomic_read(&msg_ring->used) == SEC_QUEUE_LEN) {
		mutex_unlock(&msg_ring->lock);
		return -EAGAIN;
	}
	memcpy(msg_ring->vaddr + write, msg, sizeof(*msg));
	queue->shadow[write] = ctx;
	write = (write + 1) % SEC_QUEUE_LEN;

	/* Ensure content updated before queue advance */
	wmb();
	writel(write, base + SEC_Q_WR_PTR_REG);

	atomic_inc(&msg_ring->used);
	mutex_unlock(&msg_ring->lock);

	return 0;
}

bool sec_queue_can_enqueue(struct sec_queue *queue, int num)
{
	struct sec_queue_ring_cmd *msg_ring = &queue->ring_cmd;

	return SEC_QUEUE_LEN - atomic_read(&msg_ring->used) >= num;
}

static void sec_queue_hw_init(struct sec_queue *queue)
{
	sec_queue_ar_alloc(queue, SEC_QUEUE_AR_FROCE_NOALLOC);
	sec_queue_aw_alloc(queue, SEC_QUEUE_AR_FROCE_NOALLOC);
	sec_queue_ar_pkgattr(queue, 1);
	sec_queue_aw_pkgattr(queue, 1);

	/* Enable out of order queue */
	sec_queue_reorder(queue, true);

	/* Interrupt after a single complete element */
	writel_relaxed(1, queue->regs + SEC_Q_PROC_NUM_CFG_REG);

	sec_queue_depth(queue, SEC_QUEUE_LEN - 1);

	sec_queue_cmdbase_addr(queue, queue->ring_cmd.paddr);

	sec_queue_outorder_addr(queue, queue->ring_cq.paddr);

	sec_queue_errbase_addr(queue, queue->ring_db.paddr);

	writel_relaxed(0x100, queue->regs + SEC_Q_OT_TH_REG);

	sec_queue_abn_irq_disable(queue);
	sec_queue_irq_disable(queue);
	writel_relaxed(SEC_Q_INIT_AND_STAT_CLEAR, queue->regs + SEC_Q_INIT_REG);
}

static int sec_hw_init(struct sec_dev_info *info)
{
	struct iommu_domain *domain;
	u32 sec_ipv4_mask = 0;
	u32 sec_ipv6_mask[10] = {};
	u32 i, ret;

	domain = iommu_get_domain_for_dev(info->dev);

	/*
	 * Enable all available processing unit clocks.
	 * Only the first cluster is usable with translations.
	 */
	if (domain && (domain->type & __IOMMU_DOMAIN_PAGING))
		info->num_saas = 5;

	else
		info->num_saas = 10;

	writel_relaxed(GENMASK(info->num_saas - 1, 0),
		       info->regs[SEC_SAA] + SEC_CLK_EN_REG);

	/* 32 bit little endian */
	sec_bd_endian_little(info);

	sec_cache_config(info);

	/* Data axi port write and read outstanding config as per datasheet */
	sec_data_axiwr_otsd_cfg(info, 0x7);
	sec_data_axird_otsd_cfg(info, 0x7);

	/* Enable clock gating */
	sec_clk_gate_en(info, true);

	/* Set CNT_CYC register not read clear */
	sec_comm_cnt_cfg(info, false);

	/* Enable CNT_CYC */
	sec_commsnap_en(info, false);

	writel_relaxed((u32)~0, info->regs[SEC_SAA] + SEC_FSM_MAX_CNT_REG);

	ret = sec_ipv4_hashmask(info, sec_ipv4_mask);
	if (ret) {
		dev_err(info->dev, "Failed to set ipv4 hashmask %d\n", ret);
		return -EIO;
	}

	sec_ipv6_hashmask(info, sec_ipv6_mask);

	/*  do not use debug bd */
	sec_set_dbg_bd_cfg(info, 0);

	if (domain && (domain->type & __IOMMU_DOMAIN_PAGING)) {
		for (i = 0; i < SEC_Q_NUM; i++) {
			sec_streamid(info, i);
			/* Same QoS for all queues */
			writel_relaxed(0x3f,
				       info->regs[SEC_SAA] +
				       SEC_Q_WEIGHT_CFG_REG(i));
		}
	}

	for (i = 0; i < info->num_saas; i++) {
		sec_saa_getqm_en(info, i, 1);
		sec_saa_int_mask(info, i, 0);
	}

	return 0;
}

static void sec_hw_exit(struct sec_dev_info *info)
{
	int i;

	for (i = 0; i < SEC_MAX_SAA_NUM; i++) {
		sec_saa_int_mask(info, i, (u32)~0);
		sec_saa_getqm_en(info, i, 0);
	}
}

static void sec_queue_base_init(struct sec_dev_info *info,
				struct sec_queue *queue, int queue_id)
{
	queue->dev_info = info;
	queue->queue_id = queue_id;
	snprintf(queue->name, sizeof(queue->name),
		 "%s_%d", dev_name(info->dev), queue->queue_id);
}

static int sec_map_io(struct sec_dev_info *info, struct platform_device *pdev)
{
	struct resource *res;
	int i;

	for (i = 0; i < SEC_NUM_ADDR_REGIONS; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);

		if (!res) {
			dev_err(info->dev, "Memory resource %d not found\n", i);
			return -EINVAL;
		}

		info->regs[i] = devm_ioremap(info->dev, res->start,
					     resource_size(res));
		if (!info->regs[i]) {
			dev_err(info->dev,
				"Memory resource %d could not be remapped\n",
				i);
			return -EINVAL;
		}
	}

	return 0;
}

static int sec_base_init(struct sec_dev_info *info,
			 struct platform_device *pdev)
{
	int ret;

	ret = sec_map_io(info, pdev);
	if (ret)
		return ret;

	ret = sec_clk_en(info);
	if (ret)
		return ret;

	ret = sec_reset_whole_module(info);
	if (ret)
		goto sec_clk_disable;

	ret = sec_hw_init(info);
	if (ret)
		goto sec_clk_disable;

	return 0;

sec_clk_disable:
	sec_clk_dis(info);

	return ret;
}

static void sec_base_exit(struct sec_dev_info *info)
{
	sec_hw_exit(info);
	sec_clk_dis(info);
}

#define SEC_Q_CMD_SIZE \
	round_up(SEC_QUEUE_LEN * sizeof(struct sec_bd_info), PAGE_SIZE)
#define SEC_Q_CQ_SIZE \
	round_up(SEC_QUEUE_LEN * sizeof(struct sec_out_bd_info), PAGE_SIZE)
#define SEC_Q_DB_SIZE \
	round_up(SEC_QUEUE_LEN * sizeof(struct sec_debug_bd_info), PAGE_SIZE)

static int sec_queue_res_cfg(struct sec_queue *queue)
{
	struct device *dev = queue->dev_info->dev;
	struct sec_queue_ring_cmd *ring_cmd = &queue->ring_cmd;
	struct sec_queue_ring_cq *ring_cq = &queue->ring_cq;
	struct sec_queue_ring_db *ring_db = &queue->ring_db;
	int ret;

	ring_cmd->vaddr = dma_alloc_coherent(dev, SEC_Q_CMD_SIZE,
					     &ring_cmd->paddr, GFP_KERNEL);
	if (!ring_cmd->vaddr)
		return -ENOMEM;

	atomic_set(&ring_cmd->used, 0);
	mutex_init(&ring_cmd->lock);
	ring_cmd->callback = sec_alg_callback;

	ring_cq->vaddr = dma_alloc_coherent(dev, SEC_Q_CQ_SIZE,
					    &ring_cq->paddr, GFP_KERNEL);
	if (!ring_cq->vaddr) {
		ret = -ENOMEM;
		goto err_free_ring_cmd;
	}

	ring_db->vaddr = dma_alloc_coherent(dev, SEC_Q_DB_SIZE,
					    &ring_db->paddr, GFP_KERNEL);
	if (!ring_db->vaddr) {
		ret = -ENOMEM;
		goto err_free_ring_cq;
	}
	queue->task_irq = platform_get_irq(to_platform_device(dev),
					   queue->queue_id * 2 + 1);
	if (queue->task_irq <= 0) {
		ret = -EINVAL;
		goto err_free_ring_db;
	}

	return 0;

err_free_ring_db:
	dma_free_coherent(dev, SEC_Q_DB_SIZE, queue->ring_db.vaddr,
			  queue->ring_db.paddr);
err_free_ring_cq:
	dma_free_coherent(dev, SEC_Q_CQ_SIZE, queue->ring_cq.vaddr,
			  queue->ring_cq.paddr);
err_free_ring_cmd:
	dma_free_coherent(dev, SEC_Q_CMD_SIZE, queue->ring_cmd.vaddr,
			  queue->ring_cmd.paddr);

	return ret;
}

static void sec_queue_free_ring_pages(struct sec_queue *queue)
{
	struct device *dev = queue->dev_info->dev;

	dma_free_coherent(dev, SEC_Q_DB_SIZE, queue->ring_db.vaddr,
			  queue->ring_db.paddr);
	dma_free_coherent(dev, SEC_Q_CQ_SIZE, queue->ring_cq.vaddr,
			  queue->ring_cq.paddr);
	dma_free_coherent(dev, SEC_Q_CMD_SIZE, queue->ring_cmd.vaddr,
			  queue->ring_cmd.paddr);
}

static int sec_queue_config(struct sec_dev_info *info, struct sec_queue *queue,
			    int queue_id)
{
	int ret;

	sec_queue_base_init(info, queue, queue_id);

	ret = sec_queue_res_cfg(queue);
	if (ret)
		return ret;

	ret = sec_queue_map_io(queue);
	if (ret) {
		dev_err(info->dev, "Queue map failed %d\n", ret);
		sec_queue_free_ring_pages(queue);
		return ret;
	}

	sec_queue_hw_init(queue);

	return 0;
}

static void sec_queue_unconfig(struct sec_dev_info *info,
			       struct sec_queue *queue)
{
	sec_queue_unmap_io(queue);
	sec_queue_free_ring_pages(queue);
}

static int sec_id_alloc(struct sec_dev_info *info)
{
	int ret = 0;
	int i;

	mutex_lock(&sec_id_lock);

	for (i = 0; i < SEC_MAX_DEVICES; i++)
		if (!sec_devices[i])
			break;
	if (i == SEC_MAX_DEVICES) {
		ret = -ENOMEM;
		goto unlock;
	}
	info->sec_id = i;
	sec_devices[info->sec_id] = info;

unlock:
	mutex_unlock(&sec_id_lock);

	return ret;
}

static void sec_id_free(struct sec_dev_info *info)
{
	mutex_lock(&sec_id_lock);
	sec_devices[info->sec_id] = NULL;
	mutex_unlock(&sec_id_lock);
}

static int sec_probe(struct platform_device *pdev)
{
	struct sec_dev_info *info;
	struct device *dev = &pdev->dev;
	int i, j;
	int ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev, "Failed to set 64 bit dma mask %d", ret);
		return -ENODEV;
	}

	info = devm_kzalloc(dev, (sizeof(*info)), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	mutex_init(&info->dev_lock);

	info->hw_sgl_pool = dmam_pool_create("sgl", dev,
					     sizeof(struct sec_hw_sgl), 64, 0);
	if (!info->hw_sgl_pool) {
		dev_err(dev, "Failed to create sec sgl dma pool\n");
		return -ENOMEM;
	}

	ret = sec_base_init(info, pdev);
	if (ret) {
		dev_err(dev, "Base initialization fail! %d\n", ret);
		return ret;
	}

	for (i = 0; i < SEC_Q_NUM; i++) {
		ret = sec_queue_config(info, &info->queues[i], i);
		if (ret)
			goto queues_unconfig;

		ret = sec_queue_irq_init(&info->queues[i]);
		if (ret) {
			sec_queue_unconfig(info, &info->queues[i]);
			goto queues_unconfig;
		}
	}

	ret = sec_algs_register();
	if (ret) {
		dev_err(dev, "Failed to register algorithms with crypto %d\n",
			ret);
		goto queues_unconfig;
	}

	platform_set_drvdata(pdev, info);

	ret = sec_id_alloc(info);
	if (ret)
		goto algs_unregister;

	return 0;

algs_unregister:
	sec_algs_unregister();
queues_unconfig:
	for (j = i - 1; j >= 0; j--) {
		sec_queue_irq_uninit(&info->queues[j]);
		sec_queue_unconfig(info, &info->queues[j]);
	}
	sec_base_exit(info);

	return ret;
}

static int sec_remove(struct platform_device *pdev)
{
	struct sec_dev_info *info = platform_get_drvdata(pdev);
	int i;

	/* Unexpose as soon as possible, reuse during remove is fine */
	sec_id_free(info);

	sec_algs_unregister();

	for (i = 0; i < SEC_Q_NUM; i++) {
		sec_queue_irq_uninit(&info->queues[i]);
		sec_queue_unconfig(info, &info->queues[i]);
	}

	sec_base_exit(info);

	return 0;
}

static const __maybe_unused struct of_device_id sec_match[] = {
	{ .compatible = "hisilicon,hip06-sec" },
	{ .compatible = "hisilicon,hip07-sec" },
	{}
};
MODULE_DEVICE_TABLE(of, sec_match);

static const __maybe_unused struct acpi_device_id sec_acpi_match[] = {
	{ "HISI02C1", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, sec_acpi_match);

static struct platform_driver sec_driver = {
	.probe = sec_probe,
	.remove = sec_remove,
	.driver = {
		.name = "hisi_sec_platform_driver",
		.of_match_table = sec_match,
		.acpi_match_table = ACPI_PTR(sec_acpi_match),
	},
};
module_platform_driver(sec_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HiSilicon Security Accelerators");
MODULE_AUTHOR("Zaibo Xu <xuzaibo@huawei.com");
MODULE_AUTHOR("Jonathan Cameron <jonathan.cameron@huawei.com>");
