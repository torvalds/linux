/*
 * Copyright (C) 2008
 * Guennadi Liakhovetski, DENX Software Engineering, <lg@denx.de>
 *
 * Copyright (C) 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IPU_INTERN_H_
#define _IPU_INTERN_H_

#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

/* IPU Common registers */
#define IPU_CONF		0x00
#define IPU_CHA_BUF0_RDY	0x04
#define IPU_CHA_BUF1_RDY	0x08
#define IPU_CHA_DB_MODE_SEL	0x0C
#define IPU_CHA_CUR_BUF		0x10
#define IPU_FS_PROC_FLOW	0x14
#define IPU_FS_DISP_FLOW	0x18
#define IPU_TASKS_STAT		0x1C
#define IPU_IMA_ADDR		0x20
#define IPU_IMA_DATA		0x24
#define IPU_INT_CTRL_1		0x28
#define IPU_INT_CTRL_2		0x2C
#define IPU_INT_CTRL_3		0x30
#define IPU_INT_CTRL_4		0x34
#define IPU_INT_CTRL_5		0x38
#define IPU_INT_STAT_1		0x3C
#define IPU_INT_STAT_2		0x40
#define IPU_INT_STAT_3		0x44
#define IPU_INT_STAT_4		0x48
#define IPU_INT_STAT_5		0x4C
#define IPU_BRK_CTRL_1		0x50
#define IPU_BRK_CTRL_2		0x54
#define IPU_BRK_STAT		0x58
#define IPU_DIAGB_CTRL		0x5C

/* IPU_CONF Register bits */
#define IPU_CONF_CSI_EN		0x00000001
#define IPU_CONF_IC_EN		0x00000002
#define IPU_CONF_ROT_EN		0x00000004
#define IPU_CONF_PF_EN		0x00000008
#define IPU_CONF_SDC_EN		0x00000010
#define IPU_CONF_ADC_EN		0x00000020
#define IPU_CONF_DI_EN		0x00000040
#define IPU_CONF_DU_EN		0x00000080
#define IPU_CONF_PXL_ENDIAN	0x00000100

/* Image Converter Registers */
#define IC_CONF			0x88
#define IC_PRP_ENC_RSC		0x8C
#define IC_PRP_VF_RSC		0x90
#define IC_PP_RSC		0x94
#define IC_CMBP_1		0x98
#define IC_CMBP_2		0x9C
#define PF_CONF			0xA0
#define IDMAC_CONF		0xA4
#define IDMAC_CHA_EN		0xA8
#define IDMAC_CHA_PRI		0xAC
#define IDMAC_CHA_BUSY		0xB0

/* Image Converter Register bits */
#define IC_CONF_PRPENC_EN	0x00000001
#define IC_CONF_PRPENC_CSC1	0x00000002
#define IC_CONF_PRPENC_ROT_EN	0x00000004
#define IC_CONF_PRPVF_EN	0x00000100
#define IC_CONF_PRPVF_CSC1	0x00000200
#define IC_CONF_PRPVF_CSC2	0x00000400
#define IC_CONF_PRPVF_CMB	0x00000800
#define IC_CONF_PRPVF_ROT_EN	0x00001000
#define IC_CONF_PP_EN		0x00010000
#define IC_CONF_PP_CSC1		0x00020000
#define IC_CONF_PP_CSC2		0x00040000
#define IC_CONF_PP_CMB		0x00080000
#define IC_CONF_PP_ROT_EN	0x00100000
#define IC_CONF_IC_GLB_LOC_A	0x10000000
#define IC_CONF_KEY_COLOR_EN	0x20000000
#define IC_CONF_RWS_EN		0x40000000
#define IC_CONF_CSI_MEM_WR_EN	0x80000000

#define IDMA_CHAN_INVALID	0x000000FF
#define IDMA_IC_0		0x00000001
#define IDMA_IC_1		0x00000002
#define IDMA_IC_2		0x00000004
#define IDMA_IC_3		0x00000008
#define IDMA_IC_4		0x00000010
#define IDMA_IC_5		0x00000020
#define IDMA_IC_6		0x00000040
#define IDMA_IC_7		0x00000080
#define IDMA_IC_8		0x00000100
#define IDMA_IC_9		0x00000200
#define IDMA_IC_10		0x00000400
#define IDMA_IC_11		0x00000800
#define IDMA_IC_12		0x00001000
#define IDMA_IC_13		0x00002000
#define IDMA_SDC_BG		0x00004000
#define IDMA_SDC_FG		0x00008000
#define IDMA_SDC_MASK		0x00010000
#define IDMA_SDC_PARTIAL	0x00020000
#define IDMA_ADC_SYS1_WR	0x00040000
#define IDMA_ADC_SYS2_WR	0x00080000
#define IDMA_ADC_SYS1_CMD	0x00100000
#define IDMA_ADC_SYS2_CMD	0x00200000
#define IDMA_ADC_SYS1_RD	0x00400000
#define IDMA_ADC_SYS2_RD	0x00800000
#define IDMA_PF_QP		0x01000000
#define IDMA_PF_BSP		0x02000000
#define IDMA_PF_Y_IN		0x04000000
#define IDMA_PF_U_IN		0x08000000
#define IDMA_PF_V_IN		0x10000000
#define IDMA_PF_Y_OUT		0x20000000
#define IDMA_PF_U_OUT		0x40000000
#define IDMA_PF_V_OUT		0x80000000

#define TSTAT_PF_H264_PAUSE	0x00000001
#define TSTAT_CSI2MEM_MASK	0x0000000C
#define TSTAT_CSI2MEM_OFFSET	2
#define TSTAT_VF_MASK		0x00000600
#define TSTAT_VF_OFFSET		9
#define TSTAT_VF_ROT_MASK	0x000C0000
#define TSTAT_VF_ROT_OFFSET	18
#define TSTAT_ENC_MASK		0x00000180
#define TSTAT_ENC_OFFSET	7
#define TSTAT_ENC_ROT_MASK	0x00030000
#define TSTAT_ENC_ROT_OFFSET	16
#define TSTAT_PP_MASK		0x00001800
#define TSTAT_PP_OFFSET		11
#define TSTAT_PP_ROT_MASK	0x00300000
#define TSTAT_PP_ROT_OFFSET	20
#define TSTAT_PF_MASK		0x00C00000
#define TSTAT_PF_OFFSET		22
#define TSTAT_ADCSYS1_MASK	0x03000000
#define TSTAT_ADCSYS1_OFFSET	24
#define TSTAT_ADCSYS2_MASK	0x0C000000
#define TSTAT_ADCSYS2_OFFSET	26

#define TASK_STAT_IDLE		0
#define TASK_STAT_ACTIVE	1
#define TASK_STAT_WAIT4READY	2

struct idmac {
	struct dma_device	dma;
};

struct ipu {
	void __iomem		*reg_ipu;
	void __iomem		*reg_ic;
	unsigned int		irq_fn;		/* IPU Function IRQ to the CPU */
	unsigned int		irq_err;	/* IPU Error IRQ to the CPU */
	unsigned int		irq_base;	/* Beginning of the IPU IRQ range */
	unsigned long		channel_init_mask;
	spinlock_t		lock;
	struct clk		*ipu_clk;
	struct device		*dev;
	struct idmac		idmac;
	struct idmac_channel	channel[IPU_CHANNELS_NUM];
	struct tasklet_struct	tasklet;
};

#define to_idmac(d) container_of(d, struct idmac, dma)

extern int ipu_irq_attach_irq(struct ipu *ipu, struct platform_device *dev);
extern void ipu_irq_detach_irq(struct ipu *ipu, struct platform_device *dev);

extern bool ipu_irq_status(uint32_t irq);
extern int ipu_irq_map(unsigned int source);
extern int ipu_irq_unmap(unsigned int source);

#endif
