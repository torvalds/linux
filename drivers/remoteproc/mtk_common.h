/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __RPROC_MTK_COMMON_H
#define __RPROC_MTK_COMMON_H

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/mtk_scp.h>

#define MT8183_SW_RSTN			0x0
#define MT8183_SW_RSTN_BIT		BIT(0)
#define MT8183_SCP_TO_HOST		0x1C
#define MT8183_SCP_IPC_INT_BIT		BIT(0)
#define MT8183_SCP_WDT_INT_BIT		BIT(8)
#define MT8183_HOST_TO_SCP		0x28
#define MT8183_HOST_IPC_INT_BIT		BIT(0)
#define MT8183_WDT_CFG			0x84
#define MT8183_SCP_CLK_SW_SEL		0x4000
#define MT8183_SCP_CLK_DIV_SEL		0x4024
#define MT8183_SCP_SRAM_PDN		0x402C
#define MT8183_SCP_L1_SRAM_PD		0x4080
#define MT8183_SCP_TCM_TAIL_SRAM_PD	0x4094

#define MT8183_SCP_CACHE_SEL(x)		(0x14000 + (x) * 0x3000)
#define MT8183_SCP_CACHE_CON		MT8183_SCP_CACHE_SEL(0)
#define MT8183_SCP_DCACHE_CON		MT8183_SCP_CACHE_SEL(1)
#define MT8183_SCP_CACHESIZE_8KB	BIT(8)
#define MT8183_SCP_CACHE_CON_WAYEN	BIT(10)

#define MT8192_L2TCM_SRAM_PD_0		0x10C0
#define MT8192_L2TCM_SRAM_PD_1		0x10C4
#define MT8192_L2TCM_SRAM_PD_2		0x10C8
#define MT8192_L1TCM_SRAM_PDN		0x102C
#define MT8192_CPU0_SRAM_PD		0x1080

#define MT8192_SCP2APMCU_IPC_SET	0x4080
#define MT8192_SCP2APMCU_IPC_CLR	0x4084
#define MT8192_SCP_IPC_INT_BIT		BIT(0)
#define MT8192_SCP2SPM_IPC_CLR		0x4094
#define MT8192_GIPC_IN_SET		0x4098
#define MT8192_HOST_IPC_INT_BIT		BIT(0)

#define MT8192_CORE0_SW_RSTN_CLR	0x10000
#define MT8192_CORE0_SW_RSTN_SET	0x10004
#define MT8192_CORE0_WDT_CFG		0x10034

#define SCP_FW_VER_LEN			32
#define SCP_SHARE_BUFFER_SIZE		288

struct scp_run {
	u32 signaled;
	s8 fw_ver[SCP_FW_VER_LEN];
	u32 dec_capability;
	u32 enc_capability;
	wait_queue_head_t wq;
};

struct scp_ipi_desc {
	/* For protecting handler. */
	struct mutex lock;
	scp_ipi_handler_t handler;
	void *priv;
};

struct mtk_scp;

struct mtk_scp_of_data {
	int (*scp_before_load)(struct mtk_scp *scp);
	void (*scp_irq_handler)(struct mtk_scp *scp);
	void (*scp_reset_assert)(struct mtk_scp *scp);
	void (*scp_reset_deassert)(struct mtk_scp *scp);
	void (*scp_stop)(struct mtk_scp *scp);

	u32 host_to_scp_reg;
	u32 host_to_scp_int_bit;

	size_t ipi_buf_offset;
};

struct mtk_scp {
	struct device *dev;
	struct rproc *rproc;
	struct clk *clk;
	void __iomem *reg_base;
	void __iomem *sram_base;
	size_t sram_size;

	const struct mtk_scp_of_data *data;

	struct mtk_share_obj __iomem *recv_buf;
	struct mtk_share_obj __iomem *send_buf;
	struct scp_run run;
	/* To prevent multiple ipi_send run concurrently. */
	struct mutex send_lock;
	struct scp_ipi_desc ipi_desc[SCP_IPI_MAX];
	bool ipi_id_ack[SCP_IPI_MAX];
	wait_queue_head_t ack_wq;

	void *cpu_addr;
	dma_addr_t dma_addr;
	size_t dram_size;

	struct rproc_subdev *rpmsg_subdev;
};

/**
 * struct mtk_share_obj - SRAM buffer shared with AP and SCP
 *
 * @id:		IPI id
 * @len:	share buffer length
 * @share_buf:	share buffer data
 */
struct mtk_share_obj {
	u32 id;
	u32 len;
	u8 share_buf[SCP_SHARE_BUFFER_SIZE];
};

void scp_memcpy_aligned(void __iomem *dst, const void *src, unsigned int len);
void scp_ipi_lock(struct mtk_scp *scp, u32 id);
void scp_ipi_unlock(struct mtk_scp *scp, u32 id);

#endif
