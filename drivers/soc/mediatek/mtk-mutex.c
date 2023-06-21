// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/soc/mediatek/mtk-mmsys.h>
#include <linux/soc/mediatek/mtk-mutex.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#define MTK_MUTEX_MAX_HANDLES			10

#define MT2701_MUTEX0_MOD0			0x2c
#define MT2701_MUTEX0_SOF0			0x30
#define MT8183_MUTEX0_MOD0			0x30
#define MT8183_MUTEX0_SOF0			0x2c

#define DISP_REG_MUTEX_EN(n)			(0x20 + 0x20 * (n))
#define DISP_REG_MUTEX(n)			(0x24 + 0x20 * (n))
#define DISP_REG_MUTEX_RST(n)			(0x28 + 0x20 * (n))
#define DISP_REG_MUTEX_MOD(mutex_mod_reg, n)	(mutex_mod_reg + 0x20 * (n))
#define DISP_REG_MUTEX_MOD1(mutex_mod_reg, n)	((mutex_mod_reg) + 0x20 * (n) + 0x4)
#define DISP_REG_MUTEX_SOF(mutex_sof_reg, n)	(mutex_sof_reg + 0x20 * (n))
#define DISP_REG_MUTEX_MOD2(n)			(0x34 + 0x20 * (n))

#define INT_MUTEX				BIT(1)

#define MT8186_MUTEX_MOD_DISP_OVL0		0
#define MT8186_MUTEX_MOD_DISP_OVL0_2L		1
#define MT8186_MUTEX_MOD_DISP_RDMA0		2
#define MT8186_MUTEX_MOD_DISP_COLOR0		4
#define MT8186_MUTEX_MOD_DISP_CCORR0		5
#define MT8186_MUTEX_MOD_DISP_AAL0		7
#define MT8186_MUTEX_MOD_DISP_GAMMA0		8
#define MT8186_MUTEX_MOD_DISP_POSTMASK0		9
#define MT8186_MUTEX_MOD_DISP_DITHER0		10
#define MT8186_MUTEX_MOD_DISP_RDMA1		17

#define MT8186_MUTEX_SOF_SINGLE_MODE		0
#define MT8186_MUTEX_SOF_DSI0			1
#define MT8186_MUTEX_SOF_DPI0			2
#define MT8186_MUTEX_EOF_DSI0			(MT8186_MUTEX_SOF_DSI0 << 6)
#define MT8186_MUTEX_EOF_DPI0			(MT8186_MUTEX_SOF_DPI0 << 6)

#define MT8167_MUTEX_MOD_DISP_PWM		1
#define MT8167_MUTEX_MOD_DISP_OVL0		6
#define MT8167_MUTEX_MOD_DISP_OVL1		7
#define MT8167_MUTEX_MOD_DISP_RDMA0		8
#define MT8167_MUTEX_MOD_DISP_RDMA1		9
#define MT8167_MUTEX_MOD_DISP_WDMA0		10
#define MT8167_MUTEX_MOD_DISP_CCORR		11
#define MT8167_MUTEX_MOD_DISP_COLOR		12
#define MT8167_MUTEX_MOD_DISP_AAL		13
#define MT8167_MUTEX_MOD_DISP_GAMMA		14
#define MT8167_MUTEX_MOD_DISP_DITHER		15
#define MT8167_MUTEX_MOD_DISP_UFOE		16

#define MT8192_MUTEX_MOD_DISP_OVL0		0
#define MT8192_MUTEX_MOD_DISP_OVL0_2L		1
#define MT8192_MUTEX_MOD_DISP_RDMA0		2
#define MT8192_MUTEX_MOD_DISP_COLOR0		4
#define MT8192_MUTEX_MOD_DISP_CCORR0		5
#define MT8192_MUTEX_MOD_DISP_AAL0		6
#define MT8192_MUTEX_MOD_DISP_GAMMA0		7
#define MT8192_MUTEX_MOD_DISP_POSTMASK0		8
#define MT8192_MUTEX_MOD_DISP_DITHER0		9
#define MT8192_MUTEX_MOD_DISP_OVL2_2L		16
#define MT8192_MUTEX_MOD_DISP_RDMA4		17

#define MT8183_MUTEX_MOD_DISP_RDMA0		0
#define MT8183_MUTEX_MOD_DISP_RDMA1		1
#define MT8183_MUTEX_MOD_DISP_OVL0		9
#define MT8183_MUTEX_MOD_DISP_OVL0_2L		10
#define MT8183_MUTEX_MOD_DISP_OVL1_2L		11
#define MT8183_MUTEX_MOD_DISP_WDMA0		12
#define MT8183_MUTEX_MOD_DISP_COLOR0		13
#define MT8183_MUTEX_MOD_DISP_CCORR0		14
#define MT8183_MUTEX_MOD_DISP_AAL0		15
#define MT8183_MUTEX_MOD_DISP_GAMMA0		16
#define MT8183_MUTEX_MOD_DISP_DITHER0		17

#define MT8183_MUTEX_MOD_MDP_RDMA0		2
#define MT8183_MUTEX_MOD_MDP_RSZ0		4
#define MT8183_MUTEX_MOD_MDP_RSZ1		5
#define MT8183_MUTEX_MOD_MDP_TDSHP0		6
#define MT8183_MUTEX_MOD_MDP_WROT0		7
#define MT8183_MUTEX_MOD_MDP_WDMA		8
#define MT8183_MUTEX_MOD_MDP_AAL0		23
#define MT8183_MUTEX_MOD_MDP_CCORR0		24

#define MT8186_MUTEX_MOD_MDP_RDMA0		0
#define MT8186_MUTEX_MOD_MDP_AAL0		2
#define MT8186_MUTEX_MOD_MDP_HDR0		4
#define MT8186_MUTEX_MOD_MDP_RSZ0		5
#define MT8186_MUTEX_MOD_MDP_RSZ1		6
#define MT8186_MUTEX_MOD_MDP_WROT0		7
#define MT8186_MUTEX_MOD_MDP_TDSHP0		9
#define MT8186_MUTEX_MOD_MDP_COLOR0		14

#define MT8173_MUTEX_MOD_DISP_OVL0		11
#define MT8173_MUTEX_MOD_DISP_OVL1		12
#define MT8173_MUTEX_MOD_DISP_RDMA0		13
#define MT8173_MUTEX_MOD_DISP_RDMA1		14
#define MT8173_MUTEX_MOD_DISP_RDMA2		15
#define MT8173_MUTEX_MOD_DISP_WDMA0		16
#define MT8173_MUTEX_MOD_DISP_WDMA1		17
#define MT8173_MUTEX_MOD_DISP_COLOR0		18
#define MT8173_MUTEX_MOD_DISP_COLOR1		19
#define MT8173_MUTEX_MOD_DISP_AAL		20
#define MT8173_MUTEX_MOD_DISP_GAMMA		21
#define MT8173_MUTEX_MOD_DISP_UFOE		22
#define MT8173_MUTEX_MOD_DISP_PWM0		23
#define MT8173_MUTEX_MOD_DISP_PWM1		24
#define MT8173_MUTEX_MOD_DISP_OD		25

#define MT8188_MUTEX_MOD_DISP_OVL0		0
#define MT8188_MUTEX_MOD_DISP_WDMA0		1
#define MT8188_MUTEX_MOD_DISP_RDMA0		2
#define MT8188_MUTEX_MOD_DISP_COLOR0		3
#define MT8188_MUTEX_MOD_DISP_CCORR0		4
#define MT8188_MUTEX_MOD_DISP_AAL0		5
#define MT8188_MUTEX_MOD_DISP_GAMMA0		6
#define MT8188_MUTEX_MOD_DISP_DITHER0		7
#define MT8188_MUTEX_MOD_DISP_DSI0		8
#define MT8188_MUTEX_MOD_DISP_DSC_WRAP0_CORE0	9
#define MT8188_MUTEX_MOD_DISP_VPP_MERGE		20
#define MT8188_MUTEX_MOD_DISP_DP_INTF0		21
#define MT8188_MUTEX_MOD_DISP_POSTMASK0		24
#define MT8188_MUTEX_MOD2_DISP_PWM0		33

#define MT8195_MUTEX_MOD_DISP_OVL0		0
#define MT8195_MUTEX_MOD_DISP_WDMA0		1
#define MT8195_MUTEX_MOD_DISP_RDMA0		2
#define MT8195_MUTEX_MOD_DISP_COLOR0		3
#define MT8195_MUTEX_MOD_DISP_CCORR0		4
#define MT8195_MUTEX_MOD_DISP_AAL0		5
#define MT8195_MUTEX_MOD_DISP_GAMMA0		6
#define MT8195_MUTEX_MOD_DISP_DITHER0		7
#define MT8195_MUTEX_MOD_DISP_DSI0		8
#define MT8195_MUTEX_MOD_DISP_DSC_WRAP0_CORE0	9
#define MT8195_MUTEX_MOD_DISP_VPP_MERGE		20
#define MT8195_MUTEX_MOD_DISP_DP_INTF0		21
#define MT8195_MUTEX_MOD_DISP_PWM0		27

#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA0	0
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA1	1
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA2	2
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA3	3
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA4	4
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA5	5
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA6	6
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA7	7
#define MT8195_MUTEX_MOD_DISP1_VPP_MERGE0	8
#define MT8195_MUTEX_MOD_DISP1_VPP_MERGE1	9
#define MT8195_MUTEX_MOD_DISP1_VPP_MERGE2	10
#define MT8195_MUTEX_MOD_DISP1_VPP_MERGE3	11
#define MT8195_MUTEX_MOD_DISP1_VPP_MERGE4	12
#define MT8195_MUTEX_MOD_DISP1_DISP_MIXER	18
#define MT8195_MUTEX_MOD_DISP1_DPI0		25
#define MT8195_MUTEX_MOD_DISP1_DPI1		26
#define MT8195_MUTEX_MOD_DISP1_DP_INTF0		27

/* VPPSYS0 */
#define MT8195_MUTEX_MOD_MDP_RDMA0             0
#define MT8195_MUTEX_MOD_MDP_FG0               1
#define MT8195_MUTEX_MOD_MDP_STITCH0           2
#define MT8195_MUTEX_MOD_MDP_HDR0              3
#define MT8195_MUTEX_MOD_MDP_AAL0              4
#define MT8195_MUTEX_MOD_MDP_RSZ0              5
#define MT8195_MUTEX_MOD_MDP_TDSHP0            6
#define MT8195_MUTEX_MOD_MDP_COLOR0            7
#define MT8195_MUTEX_MOD_MDP_OVL0              8
#define MT8195_MUTEX_MOD_MDP_PAD0              9
#define MT8195_MUTEX_MOD_MDP_TCC0              10
#define MT8195_MUTEX_MOD_MDP_WROT0             11

/* VPPSYS1 */
#define MT8195_MUTEX_MOD_MDP_TCC1              3
#define MT8195_MUTEX_MOD_MDP_RDMA1             4
#define MT8195_MUTEX_MOD_MDP_RDMA2             5
#define MT8195_MUTEX_MOD_MDP_RDMA3             6
#define MT8195_MUTEX_MOD_MDP_FG1               7
#define MT8195_MUTEX_MOD_MDP_FG2               8
#define MT8195_MUTEX_MOD_MDP_FG3               9
#define MT8195_MUTEX_MOD_MDP_HDR1              10
#define MT8195_MUTEX_MOD_MDP_HDR2              11
#define MT8195_MUTEX_MOD_MDP_HDR3              12
#define MT8195_MUTEX_MOD_MDP_AAL1              13
#define MT8195_MUTEX_MOD_MDP_AAL2              14
#define MT8195_MUTEX_MOD_MDP_AAL3              15
#define MT8195_MUTEX_MOD_MDP_RSZ1              16
#define MT8195_MUTEX_MOD_MDP_RSZ2              17
#define MT8195_MUTEX_MOD_MDP_RSZ3              18
#define MT8195_MUTEX_MOD_MDP_TDSHP1            19
#define MT8195_MUTEX_MOD_MDP_TDSHP2            20
#define MT8195_MUTEX_MOD_MDP_TDSHP3            21
#define MT8195_MUTEX_MOD_MDP_MERGE2            22
#define MT8195_MUTEX_MOD_MDP_MERGE3            23
#define MT8195_MUTEX_MOD_MDP_COLOR1            24
#define MT8195_MUTEX_MOD_MDP_COLOR2            25
#define MT8195_MUTEX_MOD_MDP_COLOR3            26
#define MT8195_MUTEX_MOD_MDP_OVL1              27
#define MT8195_MUTEX_MOD_MDP_PAD1              28
#define MT8195_MUTEX_MOD_MDP_PAD2              29
#define MT8195_MUTEX_MOD_MDP_PAD3              30
#define MT8195_MUTEX_MOD_MDP_WROT1             31
#define MT8195_MUTEX_MOD_MDP_WROT2             32
#define MT8195_MUTEX_MOD_MDP_WROT3             33

#define MT8365_MUTEX_MOD_DISP_OVL0		7
#define MT8365_MUTEX_MOD_DISP_OVL0_2L		8
#define MT8365_MUTEX_MOD_DISP_RDMA0		9
#define MT8365_MUTEX_MOD_DISP_RDMA1		10
#define MT8365_MUTEX_MOD_DISP_WDMA0		11
#define MT8365_MUTEX_MOD_DISP_COLOR0		12
#define MT8365_MUTEX_MOD_DISP_CCORR		13
#define MT8365_MUTEX_MOD_DISP_AAL		14
#define MT8365_MUTEX_MOD_DISP_GAMMA		15
#define MT8365_MUTEX_MOD_DISP_DITHER		16
#define MT8365_MUTEX_MOD_DISP_DSI0		17
#define MT8365_MUTEX_MOD_DISP_PWM0		20
#define MT8365_MUTEX_MOD_DISP_DPI0		22

#define MT2712_MUTEX_MOD_DISP_PWM2		10
#define MT2712_MUTEX_MOD_DISP_OVL0		11
#define MT2712_MUTEX_MOD_DISP_OVL1		12
#define MT2712_MUTEX_MOD_DISP_RDMA0		13
#define MT2712_MUTEX_MOD_DISP_RDMA1		14
#define MT2712_MUTEX_MOD_DISP_RDMA2		15
#define MT2712_MUTEX_MOD_DISP_WDMA0		16
#define MT2712_MUTEX_MOD_DISP_WDMA1		17
#define MT2712_MUTEX_MOD_DISP_COLOR0		18
#define MT2712_MUTEX_MOD_DISP_COLOR1		19
#define MT2712_MUTEX_MOD_DISP_AAL0		20
#define MT2712_MUTEX_MOD_DISP_UFOE		22
#define MT2712_MUTEX_MOD_DISP_PWM0		23
#define MT2712_MUTEX_MOD_DISP_PWM1		24
#define MT2712_MUTEX_MOD_DISP_OD0		25
#define MT2712_MUTEX_MOD2_DISP_AAL1		33
#define MT2712_MUTEX_MOD2_DISP_OD1		34

#define MT2701_MUTEX_MOD_DISP_OVL		3
#define MT2701_MUTEX_MOD_DISP_WDMA		6
#define MT2701_MUTEX_MOD_DISP_COLOR		7
#define MT2701_MUTEX_MOD_DISP_BLS		9
#define MT2701_MUTEX_MOD_DISP_RDMA0		10
#define MT2701_MUTEX_MOD_DISP_RDMA1		12

#define MT2712_MUTEX_SOF_SINGLE_MODE		0
#define MT2712_MUTEX_SOF_DSI0			1
#define MT2712_MUTEX_SOF_DSI1			2
#define MT2712_MUTEX_SOF_DPI0			3
#define MT2712_MUTEX_SOF_DPI1			4
#define MT2712_MUTEX_SOF_DSI2			5
#define MT2712_MUTEX_SOF_DSI3			6
#define MT8167_MUTEX_SOF_DPI0			2
#define MT8167_MUTEX_SOF_DPI1			3
#define MT8183_MUTEX_SOF_DSI0			1
#define MT8183_MUTEX_SOF_DPI0			2
#define MT8188_MUTEX_SOF_DSI0			1
#define MT8188_MUTEX_SOF_DP_INTF0		3
#define MT8195_MUTEX_SOF_DSI0			1
#define MT8195_MUTEX_SOF_DSI1			2
#define MT8195_MUTEX_SOF_DP_INTF0		3
#define MT8195_MUTEX_SOF_DP_INTF1		4
#define MT8195_MUTEX_SOF_DPI0			6 /* for HDMI_TX */
#define MT8195_MUTEX_SOF_DPI1			5 /* for digital video out */

#define MT8183_MUTEX_EOF_DSI0			(MT8183_MUTEX_SOF_DSI0 << 6)
#define MT8183_MUTEX_EOF_DPI0			(MT8183_MUTEX_SOF_DPI0 << 6)
#define MT8188_MUTEX_EOF_DSI0			(MT8188_MUTEX_SOF_DSI0 << 7)
#define MT8188_MUTEX_EOF_DP_INTF0		(MT8188_MUTEX_SOF_DP_INTF0 << 7)
#define MT8195_MUTEX_EOF_DSI0			(MT8195_MUTEX_SOF_DSI0 << 7)
#define MT8195_MUTEX_EOF_DSI1			(MT8195_MUTEX_SOF_DSI1 << 7)
#define MT8195_MUTEX_EOF_DP_INTF0		(MT8195_MUTEX_SOF_DP_INTF0 << 7)
#define MT8195_MUTEX_EOF_DP_INTF1		(MT8195_MUTEX_SOF_DP_INTF1 << 7)
#define MT8195_MUTEX_EOF_DPI0			(MT8195_MUTEX_SOF_DPI0 << 7)
#define MT8195_MUTEX_EOF_DPI1			(MT8195_MUTEX_SOF_DPI1 << 7)

struct mtk_mutex {
	u8 id;
	bool claimed;
};

enum mtk_mutex_sof_id {
	MUTEX_SOF_SINGLE_MODE,
	MUTEX_SOF_DSI0,
	MUTEX_SOF_DSI1,
	MUTEX_SOF_DPI0,
	MUTEX_SOF_DPI1,
	MUTEX_SOF_DSI2,
	MUTEX_SOF_DSI3,
	MUTEX_SOF_DP_INTF0,
	MUTEX_SOF_DP_INTF1,
	DDP_MUTEX_SOF_MAX,
};

struct mtk_mutex_data {
	const unsigned int *mutex_mod;
	const unsigned int *mutex_sof;
	const unsigned int mutex_mod_reg;
	const unsigned int mutex_sof_reg;
	const unsigned int *mutex_table_mod;
	const bool no_clk;
};

struct mtk_mutex_ctx {
	struct device			*dev;
	struct clk			*clk;
	void __iomem			*regs;
	struct mtk_mutex		mutex[MTK_MUTEX_MAX_HANDLES];
	const struct mtk_mutex_data	*data;
	phys_addr_t			addr;
	struct cmdq_client_reg		cmdq_reg;
};

static const unsigned int mt2701_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_BLS] = MT2701_MUTEX_MOD_DISP_BLS,
	[DDP_COMPONENT_COLOR0] = MT2701_MUTEX_MOD_DISP_COLOR,
	[DDP_COMPONENT_OVL0] = MT2701_MUTEX_MOD_DISP_OVL,
	[DDP_COMPONENT_RDMA0] = MT2701_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT2701_MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_WDMA0] = MT2701_MUTEX_MOD_DISP_WDMA,
};

static const unsigned int mt2712_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0] = MT2712_MUTEX_MOD_DISP_AAL0,
	[DDP_COMPONENT_AAL1] = MT2712_MUTEX_MOD2_DISP_AAL1,
	[DDP_COMPONENT_COLOR0] = MT2712_MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_COLOR1] = MT2712_MUTEX_MOD_DISP_COLOR1,
	[DDP_COMPONENT_OD0] = MT2712_MUTEX_MOD_DISP_OD0,
	[DDP_COMPONENT_OD1] = MT2712_MUTEX_MOD2_DISP_OD1,
	[DDP_COMPONENT_OVL0] = MT2712_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL1] = MT2712_MUTEX_MOD_DISP_OVL1,
	[DDP_COMPONENT_PWM0] = MT2712_MUTEX_MOD_DISP_PWM0,
	[DDP_COMPONENT_PWM1] = MT2712_MUTEX_MOD_DISP_PWM1,
	[DDP_COMPONENT_PWM2] = MT2712_MUTEX_MOD_DISP_PWM2,
	[DDP_COMPONENT_RDMA0] = MT2712_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT2712_MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_RDMA2] = MT2712_MUTEX_MOD_DISP_RDMA2,
	[DDP_COMPONENT_UFOE] = MT2712_MUTEX_MOD_DISP_UFOE,
	[DDP_COMPONENT_WDMA0] = MT2712_MUTEX_MOD_DISP_WDMA0,
	[DDP_COMPONENT_WDMA1] = MT2712_MUTEX_MOD_DISP_WDMA1,
};

static const unsigned int mt8167_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0] = MT8167_MUTEX_MOD_DISP_AAL,
	[DDP_COMPONENT_CCORR] = MT8167_MUTEX_MOD_DISP_CCORR,
	[DDP_COMPONENT_COLOR0] = MT8167_MUTEX_MOD_DISP_COLOR,
	[DDP_COMPONENT_DITHER0] = MT8167_MUTEX_MOD_DISP_DITHER,
	[DDP_COMPONENT_GAMMA] = MT8167_MUTEX_MOD_DISP_GAMMA,
	[DDP_COMPONENT_OVL0] = MT8167_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL1] = MT8167_MUTEX_MOD_DISP_OVL1,
	[DDP_COMPONENT_PWM0] = MT8167_MUTEX_MOD_DISP_PWM,
	[DDP_COMPONENT_RDMA0] = MT8167_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT8167_MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_UFOE] = MT8167_MUTEX_MOD_DISP_UFOE,
	[DDP_COMPONENT_WDMA0] = MT8167_MUTEX_MOD_DISP_WDMA0,
};

static const unsigned int mt8173_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0] = MT8173_MUTEX_MOD_DISP_AAL,
	[DDP_COMPONENT_COLOR0] = MT8173_MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_COLOR1] = MT8173_MUTEX_MOD_DISP_COLOR1,
	[DDP_COMPONENT_GAMMA] = MT8173_MUTEX_MOD_DISP_GAMMA,
	[DDP_COMPONENT_OD0] = MT8173_MUTEX_MOD_DISP_OD,
	[DDP_COMPONENT_OVL0] = MT8173_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL1] = MT8173_MUTEX_MOD_DISP_OVL1,
	[DDP_COMPONENT_PWM0] = MT8173_MUTEX_MOD_DISP_PWM0,
	[DDP_COMPONENT_PWM1] = MT8173_MUTEX_MOD_DISP_PWM1,
	[DDP_COMPONENT_RDMA0] = MT8173_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT8173_MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_RDMA2] = MT8173_MUTEX_MOD_DISP_RDMA2,
	[DDP_COMPONENT_UFOE] = MT8173_MUTEX_MOD_DISP_UFOE,
	[DDP_COMPONENT_WDMA0] = MT8173_MUTEX_MOD_DISP_WDMA0,
	[DDP_COMPONENT_WDMA1] = MT8173_MUTEX_MOD_DISP_WDMA1,
};

static const unsigned int mt8183_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0] = MT8183_MUTEX_MOD_DISP_AAL0,
	[DDP_COMPONENT_CCORR] = MT8183_MUTEX_MOD_DISP_CCORR0,
	[DDP_COMPONENT_COLOR0] = MT8183_MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_DITHER0] = MT8183_MUTEX_MOD_DISP_DITHER0,
	[DDP_COMPONENT_GAMMA] = MT8183_MUTEX_MOD_DISP_GAMMA0,
	[DDP_COMPONENT_OVL0] = MT8183_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL_2L0] = MT8183_MUTEX_MOD_DISP_OVL0_2L,
	[DDP_COMPONENT_OVL_2L1] = MT8183_MUTEX_MOD_DISP_OVL1_2L,
	[DDP_COMPONENT_RDMA0] = MT8183_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT8183_MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_WDMA0] = MT8183_MUTEX_MOD_DISP_WDMA0,
};

static const unsigned int mt8183_mutex_table_mod[MUTEX_MOD_IDX_MAX] = {
	[MUTEX_MOD_IDX_MDP_RDMA0] = MT8183_MUTEX_MOD_MDP_RDMA0,
	[MUTEX_MOD_IDX_MDP_RSZ0] = MT8183_MUTEX_MOD_MDP_RSZ0,
	[MUTEX_MOD_IDX_MDP_RSZ1] = MT8183_MUTEX_MOD_MDP_RSZ1,
	[MUTEX_MOD_IDX_MDP_TDSHP0] = MT8183_MUTEX_MOD_MDP_TDSHP0,
	[MUTEX_MOD_IDX_MDP_WROT0] = MT8183_MUTEX_MOD_MDP_WROT0,
	[MUTEX_MOD_IDX_MDP_WDMA] = MT8183_MUTEX_MOD_MDP_WDMA,
	[MUTEX_MOD_IDX_MDP_AAL0] = MT8183_MUTEX_MOD_MDP_AAL0,
	[MUTEX_MOD_IDX_MDP_CCORR0] = MT8183_MUTEX_MOD_MDP_CCORR0,
};

static const unsigned int mt8186_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0] = MT8186_MUTEX_MOD_DISP_AAL0,
	[DDP_COMPONENT_CCORR] = MT8186_MUTEX_MOD_DISP_CCORR0,
	[DDP_COMPONENT_COLOR0] = MT8186_MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_DITHER0] = MT8186_MUTEX_MOD_DISP_DITHER0,
	[DDP_COMPONENT_GAMMA] = MT8186_MUTEX_MOD_DISP_GAMMA0,
	[DDP_COMPONENT_OVL0] = MT8186_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL_2L0] = MT8186_MUTEX_MOD_DISP_OVL0_2L,
	[DDP_COMPONENT_POSTMASK0] = MT8186_MUTEX_MOD_DISP_POSTMASK0,
	[DDP_COMPONENT_RDMA0] = MT8186_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT8186_MUTEX_MOD_DISP_RDMA1,
};

static const unsigned int mt8186_mdp_mutex_table_mod[MUTEX_MOD_IDX_MAX] = {
	[MUTEX_MOD_IDX_MDP_RDMA0] = MT8186_MUTEX_MOD_MDP_RDMA0,
	[MUTEX_MOD_IDX_MDP_RSZ0] = MT8186_MUTEX_MOD_MDP_RSZ0,
	[MUTEX_MOD_IDX_MDP_RSZ1] = MT8186_MUTEX_MOD_MDP_RSZ1,
	[MUTEX_MOD_IDX_MDP_TDSHP0] = MT8186_MUTEX_MOD_MDP_TDSHP0,
	[MUTEX_MOD_IDX_MDP_WROT0] = MT8186_MUTEX_MOD_MDP_WROT0,
	[MUTEX_MOD_IDX_MDP_HDR0] = MT8186_MUTEX_MOD_MDP_HDR0,
	[MUTEX_MOD_IDX_MDP_AAL0] = MT8186_MUTEX_MOD_MDP_AAL0,
	[MUTEX_MOD_IDX_MDP_COLOR0] = MT8186_MUTEX_MOD_MDP_COLOR0,
};

static const unsigned int mt8188_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_OVL0] = MT8188_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_WDMA0] = MT8188_MUTEX_MOD_DISP_WDMA0,
	[DDP_COMPONENT_RDMA0] = MT8188_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_COLOR0] = MT8188_MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_CCORR] = MT8188_MUTEX_MOD_DISP_CCORR0,
	[DDP_COMPONENT_AAL0] = MT8188_MUTEX_MOD_DISP_AAL0,
	[DDP_COMPONENT_GAMMA] = MT8188_MUTEX_MOD_DISP_GAMMA0,
	[DDP_COMPONENT_POSTMASK0] = MT8188_MUTEX_MOD_DISP_POSTMASK0,
	[DDP_COMPONENT_DITHER0] = MT8188_MUTEX_MOD_DISP_DITHER0,
	[DDP_COMPONENT_MERGE0] = MT8188_MUTEX_MOD_DISP_VPP_MERGE,
	[DDP_COMPONENT_DSC0] = MT8188_MUTEX_MOD_DISP_DSC_WRAP0_CORE0,
	[DDP_COMPONENT_DSI0] = MT8188_MUTEX_MOD_DISP_DSI0,
	[DDP_COMPONENT_PWM0] = MT8188_MUTEX_MOD2_DISP_PWM0,
	[DDP_COMPONENT_DP_INTF0] = MT8188_MUTEX_MOD_DISP_DP_INTF0,
};

static const unsigned int mt8192_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0] = MT8192_MUTEX_MOD_DISP_AAL0,
	[DDP_COMPONENT_CCORR] = MT8192_MUTEX_MOD_DISP_CCORR0,
	[DDP_COMPONENT_COLOR0] = MT8192_MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_DITHER0] = MT8192_MUTEX_MOD_DISP_DITHER0,
	[DDP_COMPONENT_GAMMA] = MT8192_MUTEX_MOD_DISP_GAMMA0,
	[DDP_COMPONENT_POSTMASK0] = MT8192_MUTEX_MOD_DISP_POSTMASK0,
	[DDP_COMPONENT_OVL0] = MT8192_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL_2L0] = MT8192_MUTEX_MOD_DISP_OVL0_2L,
	[DDP_COMPONENT_OVL_2L2] = MT8192_MUTEX_MOD_DISP_OVL2_2L,
	[DDP_COMPONENT_RDMA0] = MT8192_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA4] = MT8192_MUTEX_MOD_DISP_RDMA4,
};

static const unsigned int mt8195_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_OVL0] = MT8195_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_WDMA0] = MT8195_MUTEX_MOD_DISP_WDMA0,
	[DDP_COMPONENT_RDMA0] = MT8195_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_COLOR0] = MT8195_MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_CCORR] = MT8195_MUTEX_MOD_DISP_CCORR0,
	[DDP_COMPONENT_AAL0] = MT8195_MUTEX_MOD_DISP_AAL0,
	[DDP_COMPONENT_GAMMA] = MT8195_MUTEX_MOD_DISP_GAMMA0,
	[DDP_COMPONENT_DITHER0] = MT8195_MUTEX_MOD_DISP_DITHER0,
	[DDP_COMPONENT_MERGE0] = MT8195_MUTEX_MOD_DISP_VPP_MERGE,
	[DDP_COMPONENT_DSC0] = MT8195_MUTEX_MOD_DISP_DSC_WRAP0_CORE0,
	[DDP_COMPONENT_DSI0] = MT8195_MUTEX_MOD_DISP_DSI0,
	[DDP_COMPONENT_PWM0] = MT8195_MUTEX_MOD_DISP_PWM0,
	[DDP_COMPONENT_DP_INTF0] = MT8195_MUTEX_MOD_DISP_DP_INTF0,
	[DDP_COMPONENT_MDP_RDMA0] = MT8195_MUTEX_MOD_DISP1_MDP_RDMA0,
	[DDP_COMPONENT_MDP_RDMA1] = MT8195_MUTEX_MOD_DISP1_MDP_RDMA1,
	[DDP_COMPONENT_MDP_RDMA2] = MT8195_MUTEX_MOD_DISP1_MDP_RDMA2,
	[DDP_COMPONENT_MDP_RDMA3] = MT8195_MUTEX_MOD_DISP1_MDP_RDMA3,
	[DDP_COMPONENT_MDP_RDMA4] = MT8195_MUTEX_MOD_DISP1_MDP_RDMA4,
	[DDP_COMPONENT_MDP_RDMA5] = MT8195_MUTEX_MOD_DISP1_MDP_RDMA5,
	[DDP_COMPONENT_MDP_RDMA6] = MT8195_MUTEX_MOD_DISP1_MDP_RDMA6,
	[DDP_COMPONENT_MDP_RDMA7] = MT8195_MUTEX_MOD_DISP1_MDP_RDMA7,
	[DDP_COMPONENT_MERGE1] = MT8195_MUTEX_MOD_DISP1_VPP_MERGE0,
	[DDP_COMPONENT_MERGE2] = MT8195_MUTEX_MOD_DISP1_VPP_MERGE1,
	[DDP_COMPONENT_MERGE3] = MT8195_MUTEX_MOD_DISP1_VPP_MERGE2,
	[DDP_COMPONENT_MERGE4] = MT8195_MUTEX_MOD_DISP1_VPP_MERGE3,
	[DDP_COMPONENT_ETHDR_MIXER] = MT8195_MUTEX_MOD_DISP1_DISP_MIXER,
	[DDP_COMPONENT_MERGE5] = MT8195_MUTEX_MOD_DISP1_VPP_MERGE4,
	[DDP_COMPONENT_DP_INTF1] = MT8195_MUTEX_MOD_DISP1_DP_INTF0,
};

static const unsigned int mt8195_mutex_table_mod[MUTEX_MOD_IDX_MAX] = {
	[MUTEX_MOD_IDX_MDP_RDMA0] = MT8195_MUTEX_MOD_MDP_RDMA0,
	[MUTEX_MOD_IDX_MDP_RDMA1] = MT8195_MUTEX_MOD_MDP_RDMA1,
	[MUTEX_MOD_IDX_MDP_RDMA2] = MT8195_MUTEX_MOD_MDP_RDMA2,
	[MUTEX_MOD_IDX_MDP_RDMA3] = MT8195_MUTEX_MOD_MDP_RDMA3,
	[MUTEX_MOD_IDX_MDP_STITCH0] = MT8195_MUTEX_MOD_MDP_STITCH0,
	[MUTEX_MOD_IDX_MDP_FG0] = MT8195_MUTEX_MOD_MDP_FG0,
	[MUTEX_MOD_IDX_MDP_FG1] = MT8195_MUTEX_MOD_MDP_FG1,
	[MUTEX_MOD_IDX_MDP_FG2] = MT8195_MUTEX_MOD_MDP_FG2,
	[MUTEX_MOD_IDX_MDP_FG3] = MT8195_MUTEX_MOD_MDP_FG3,
	[MUTEX_MOD_IDX_MDP_HDR0] = MT8195_MUTEX_MOD_MDP_HDR0,
	[MUTEX_MOD_IDX_MDP_HDR1] = MT8195_MUTEX_MOD_MDP_HDR1,
	[MUTEX_MOD_IDX_MDP_HDR2] = MT8195_MUTEX_MOD_MDP_HDR2,
	[MUTEX_MOD_IDX_MDP_HDR3] = MT8195_MUTEX_MOD_MDP_HDR3,
	[MUTEX_MOD_IDX_MDP_AAL0] = MT8195_MUTEX_MOD_MDP_AAL0,
	[MUTEX_MOD_IDX_MDP_AAL1] = MT8195_MUTEX_MOD_MDP_AAL1,
	[MUTEX_MOD_IDX_MDP_AAL2] = MT8195_MUTEX_MOD_MDP_AAL2,
	[MUTEX_MOD_IDX_MDP_AAL3] = MT8195_MUTEX_MOD_MDP_AAL3,
	[MUTEX_MOD_IDX_MDP_RSZ0] = MT8195_MUTEX_MOD_MDP_RSZ0,
	[MUTEX_MOD_IDX_MDP_RSZ1] = MT8195_MUTEX_MOD_MDP_RSZ1,
	[MUTEX_MOD_IDX_MDP_RSZ2] = MT8195_MUTEX_MOD_MDP_RSZ2,
	[MUTEX_MOD_IDX_MDP_RSZ3] = MT8195_MUTEX_MOD_MDP_RSZ3,
	[MUTEX_MOD_IDX_MDP_MERGE2] = MT8195_MUTEX_MOD_MDP_MERGE2,
	[MUTEX_MOD_IDX_MDP_MERGE3] = MT8195_MUTEX_MOD_MDP_MERGE3,
	[MUTEX_MOD_IDX_MDP_TDSHP0] = MT8195_MUTEX_MOD_MDP_TDSHP0,
	[MUTEX_MOD_IDX_MDP_TDSHP1] = MT8195_MUTEX_MOD_MDP_TDSHP1,
	[MUTEX_MOD_IDX_MDP_TDSHP2] = MT8195_MUTEX_MOD_MDP_TDSHP2,
	[MUTEX_MOD_IDX_MDP_TDSHP3] = MT8195_MUTEX_MOD_MDP_TDSHP3,
	[MUTEX_MOD_IDX_MDP_COLOR0] = MT8195_MUTEX_MOD_MDP_COLOR0,
	[MUTEX_MOD_IDX_MDP_COLOR1] = MT8195_MUTEX_MOD_MDP_COLOR1,
	[MUTEX_MOD_IDX_MDP_COLOR2] = MT8195_MUTEX_MOD_MDP_COLOR2,
	[MUTEX_MOD_IDX_MDP_COLOR3] = MT8195_MUTEX_MOD_MDP_COLOR3,
	[MUTEX_MOD_IDX_MDP_OVL0] = MT8195_MUTEX_MOD_MDP_OVL0,
	[MUTEX_MOD_IDX_MDP_OVL1] = MT8195_MUTEX_MOD_MDP_OVL1,
	[MUTEX_MOD_IDX_MDP_PAD0] = MT8195_MUTEX_MOD_MDP_PAD0,
	[MUTEX_MOD_IDX_MDP_PAD1] = MT8195_MUTEX_MOD_MDP_PAD1,
	[MUTEX_MOD_IDX_MDP_PAD2] = MT8195_MUTEX_MOD_MDP_PAD2,
	[MUTEX_MOD_IDX_MDP_PAD3] = MT8195_MUTEX_MOD_MDP_PAD3,
	[MUTEX_MOD_IDX_MDP_TCC0] = MT8195_MUTEX_MOD_MDP_TCC0,
	[MUTEX_MOD_IDX_MDP_TCC1] = MT8195_MUTEX_MOD_MDP_TCC1,
	[MUTEX_MOD_IDX_MDP_WROT0] = MT8195_MUTEX_MOD_MDP_WROT0,
	[MUTEX_MOD_IDX_MDP_WROT1] = MT8195_MUTEX_MOD_MDP_WROT1,
	[MUTEX_MOD_IDX_MDP_WROT2] = MT8195_MUTEX_MOD_MDP_WROT2,
	[MUTEX_MOD_IDX_MDP_WROT3] = MT8195_MUTEX_MOD_MDP_WROT3,
};

static const unsigned int mt8365_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0] = MT8365_MUTEX_MOD_DISP_AAL,
	[DDP_COMPONENT_CCORR] = MT8365_MUTEX_MOD_DISP_CCORR,
	[DDP_COMPONENT_COLOR0] = MT8365_MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_DITHER0] = MT8365_MUTEX_MOD_DISP_DITHER,
	[DDP_COMPONENT_DPI0] = MT8365_MUTEX_MOD_DISP_DPI0,
	[DDP_COMPONENT_DSI0] = MT8365_MUTEX_MOD_DISP_DSI0,
	[DDP_COMPONENT_GAMMA] = MT8365_MUTEX_MOD_DISP_GAMMA,
	[DDP_COMPONENT_OVL0] = MT8365_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL_2L0] = MT8365_MUTEX_MOD_DISP_OVL0_2L,
	[DDP_COMPONENT_PWM0] = MT8365_MUTEX_MOD_DISP_PWM0,
	[DDP_COMPONENT_RDMA0] = MT8365_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT8365_MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_WDMA0] = MT8365_MUTEX_MOD_DISP_WDMA0,
};

static const unsigned int mt2712_mutex_sof[DDP_MUTEX_SOF_MAX] = {
	[MUTEX_SOF_SINGLE_MODE] = MUTEX_SOF_SINGLE_MODE,
	[MUTEX_SOF_DSI0] = MUTEX_SOF_DSI0,
	[MUTEX_SOF_DSI1] = MUTEX_SOF_DSI1,
	[MUTEX_SOF_DPI0] = MUTEX_SOF_DPI0,
	[MUTEX_SOF_DPI1] = MUTEX_SOF_DPI1,
	[MUTEX_SOF_DSI2] = MUTEX_SOF_DSI2,
	[MUTEX_SOF_DSI3] = MUTEX_SOF_DSI3,
};

static const unsigned int mt6795_mutex_sof[DDP_MUTEX_SOF_MAX] = {
	[MUTEX_SOF_SINGLE_MODE] = MUTEX_SOF_SINGLE_MODE,
	[MUTEX_SOF_DSI0] = MUTEX_SOF_DSI0,
	[MUTEX_SOF_DSI1] = MUTEX_SOF_DSI1,
	[MUTEX_SOF_DPI0] = MUTEX_SOF_DPI0,
};

static const unsigned int mt8167_mutex_sof[DDP_MUTEX_SOF_MAX] = {
	[MUTEX_SOF_SINGLE_MODE] = MUTEX_SOF_SINGLE_MODE,
	[MUTEX_SOF_DSI0] = MUTEX_SOF_DSI0,
	[MUTEX_SOF_DPI0] = MT8167_MUTEX_SOF_DPI0,
	[MUTEX_SOF_DPI1] = MT8167_MUTEX_SOF_DPI1,
};

/* Add EOF setting so overlay hardware can receive frame done irq */
static const unsigned int mt8183_mutex_sof[DDP_MUTEX_SOF_MAX] = {
	[MUTEX_SOF_SINGLE_MODE] = MUTEX_SOF_SINGLE_MODE,
	[MUTEX_SOF_DSI0] = MUTEX_SOF_DSI0 | MT8183_MUTEX_EOF_DSI0,
	[MUTEX_SOF_DPI0] = MT8183_MUTEX_SOF_DPI0 | MT8183_MUTEX_EOF_DPI0,
};

static const unsigned int mt8186_mutex_sof[MUTEX_SOF_DSI3 + 1] = {
	[MUTEX_SOF_SINGLE_MODE] = MUTEX_SOF_SINGLE_MODE,
	[MUTEX_SOF_DSI0] = MT8186_MUTEX_SOF_DSI0 | MT8186_MUTEX_EOF_DSI0,
	[MUTEX_SOF_DPI0] = MT8186_MUTEX_SOF_DPI0 | MT8186_MUTEX_EOF_DPI0,
};

/*
 * To support refresh mode(video mode), DISP_REG_MUTEX_SOF should
 * select the EOF source and configure the EOF plus timing from the
 * module that provides the timing signal.
 * So that MUTEX can not only send a STREAM_DONE event to GCE
 * but also detect the error at end of frame(EAEOF) when EOF signal
 * arrives.
 */
static const unsigned int mt8188_mutex_sof[DDP_MUTEX_SOF_MAX] = {
	[MUTEX_SOF_SINGLE_MODE] = MUTEX_SOF_SINGLE_MODE,
	[MUTEX_SOF_DSI0] =
		MT8188_MUTEX_SOF_DSI0 | MT8188_MUTEX_EOF_DSI0,
	[MUTEX_SOF_DP_INTF0] =
		MT8188_MUTEX_SOF_DP_INTF0 | MT8188_MUTEX_EOF_DP_INTF0,
};

static const unsigned int mt8195_mutex_sof[DDP_MUTEX_SOF_MAX] = {
	[MUTEX_SOF_SINGLE_MODE] = MUTEX_SOF_SINGLE_MODE,
	[MUTEX_SOF_DSI0] = MT8195_MUTEX_SOF_DSI0 | MT8195_MUTEX_EOF_DSI0,
	[MUTEX_SOF_DSI1] = MT8195_MUTEX_SOF_DSI1 | MT8195_MUTEX_EOF_DSI1,
	[MUTEX_SOF_DPI0] = MT8195_MUTEX_SOF_DPI0 | MT8195_MUTEX_EOF_DPI0,
	[MUTEX_SOF_DPI1] = MT8195_MUTEX_SOF_DPI1 | MT8195_MUTEX_EOF_DPI1,
	[MUTEX_SOF_DP_INTF0] =
		MT8195_MUTEX_SOF_DP_INTF0 | MT8195_MUTEX_EOF_DP_INTF0,
	[MUTEX_SOF_DP_INTF1] =
		MT8195_MUTEX_SOF_DP_INTF1 | MT8195_MUTEX_EOF_DP_INTF1,
};

static const struct mtk_mutex_data mt2701_mutex_driver_data = {
	.mutex_mod = mt2701_mutex_mod,
	.mutex_sof = mt2712_mutex_sof,
	.mutex_mod_reg = MT2701_MUTEX0_MOD0,
	.mutex_sof_reg = MT2701_MUTEX0_SOF0,
};

static const struct mtk_mutex_data mt2712_mutex_driver_data = {
	.mutex_mod = mt2712_mutex_mod,
	.mutex_sof = mt2712_mutex_sof,
	.mutex_mod_reg = MT2701_MUTEX0_MOD0,
	.mutex_sof_reg = MT2701_MUTEX0_SOF0,
};

static const struct mtk_mutex_data mt6795_mutex_driver_data = {
	.mutex_mod = mt8173_mutex_mod,
	.mutex_sof = mt6795_mutex_sof,
	.mutex_mod_reg = MT2701_MUTEX0_MOD0,
	.mutex_sof_reg = MT2701_MUTEX0_SOF0,
};

static const struct mtk_mutex_data mt8167_mutex_driver_data = {
	.mutex_mod = mt8167_mutex_mod,
	.mutex_sof = mt8167_mutex_sof,
	.mutex_mod_reg = MT2701_MUTEX0_MOD0,
	.mutex_sof_reg = MT2701_MUTEX0_SOF0,
	.no_clk = true,
};

static const struct mtk_mutex_data mt8173_mutex_driver_data = {
	.mutex_mod = mt8173_mutex_mod,
	.mutex_sof = mt2712_mutex_sof,
	.mutex_mod_reg = MT2701_MUTEX0_MOD0,
	.mutex_sof_reg = MT2701_MUTEX0_SOF0,
};

static const struct mtk_mutex_data mt8183_mutex_driver_data = {
	.mutex_mod = mt8183_mutex_mod,
	.mutex_sof = mt8183_mutex_sof,
	.mutex_mod_reg = MT8183_MUTEX0_MOD0,
	.mutex_sof_reg = MT8183_MUTEX0_SOF0,
	.mutex_table_mod = mt8183_mutex_table_mod,
	.no_clk = true,
};

static const struct mtk_mutex_data mt8186_mdp_mutex_driver_data = {
	.mutex_mod_reg = MT8183_MUTEX0_MOD0,
	.mutex_sof_reg = MT8183_MUTEX0_SOF0,
	.mutex_table_mod = mt8186_mdp_mutex_table_mod,
};

static const struct mtk_mutex_data mt8186_mutex_driver_data = {
	.mutex_mod = mt8186_mutex_mod,
	.mutex_sof = mt8186_mutex_sof,
	.mutex_mod_reg = MT8183_MUTEX0_MOD0,
	.mutex_sof_reg = MT8183_MUTEX0_SOF0,
};

static const struct mtk_mutex_data mt8188_mutex_driver_data = {
	.mutex_mod = mt8188_mutex_mod,
	.mutex_sof = mt8188_mutex_sof,
	.mutex_mod_reg = MT8183_MUTEX0_MOD0,
	.mutex_sof_reg = MT8183_MUTEX0_SOF0,
};

static const struct mtk_mutex_data mt8192_mutex_driver_data = {
	.mutex_mod = mt8192_mutex_mod,
	.mutex_sof = mt8183_mutex_sof,
	.mutex_mod_reg = MT8183_MUTEX0_MOD0,
	.mutex_sof_reg = MT8183_MUTEX0_SOF0,
};

static const struct mtk_mutex_data mt8195_mutex_driver_data = {
	.mutex_mod = mt8195_mutex_mod,
	.mutex_sof = mt8195_mutex_sof,
	.mutex_mod_reg = MT8183_MUTEX0_MOD0,
	.mutex_sof_reg = MT8183_MUTEX0_SOF0,
};

static const struct mtk_mutex_data mt8195_vpp_mutex_driver_data = {
	.mutex_sof = mt8195_mutex_sof,
	.mutex_mod_reg = MT8183_MUTEX0_MOD0,
	.mutex_sof_reg = MT8183_MUTEX0_SOF0,
	.mutex_table_mod = mt8195_mutex_table_mod,
};

static const struct mtk_mutex_data mt8365_mutex_driver_data = {
	.mutex_mod = mt8365_mutex_mod,
	.mutex_sof = mt8183_mutex_sof,
	.mutex_mod_reg = MT8183_MUTEX0_MOD0,
	.mutex_sof_reg = MT8183_MUTEX0_SOF0,
	.no_clk = true,
};

struct mtk_mutex *mtk_mutex_get(struct device *dev)
{
	struct mtk_mutex_ctx *mtx = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < MTK_MUTEX_MAX_HANDLES; i++)
		if (!mtx->mutex[i].claimed) {
			mtx->mutex[i].claimed = true;
			return &mtx->mutex[i];
		}

	return ERR_PTR(-EBUSY);
}
EXPORT_SYMBOL_GPL(mtk_mutex_get);

void mtk_mutex_put(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	mutex->claimed = false;
}
EXPORT_SYMBOL_GPL(mtk_mutex_put);

int mtk_mutex_prepare(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);
	return clk_prepare_enable(mtx->clk);
}
EXPORT_SYMBOL_GPL(mtk_mutex_prepare);

void mtk_mutex_unprepare(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);
	clk_disable_unprepare(mtx->clk);
}
EXPORT_SYMBOL_GPL(mtk_mutex_unprepare);

void mtk_mutex_add_comp(struct mtk_mutex *mutex,
			enum mtk_ddp_comp_id id)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);
	unsigned int reg;
	unsigned int sof_id;
	unsigned int offset;

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	switch (id) {
	case DDP_COMPONENT_DSI0:
		sof_id = MUTEX_SOF_DSI0;
		break;
	case DDP_COMPONENT_DSI1:
		sof_id = MUTEX_SOF_DSI0;
		break;
	case DDP_COMPONENT_DSI2:
		sof_id = MUTEX_SOF_DSI2;
		break;
	case DDP_COMPONENT_DSI3:
		sof_id = MUTEX_SOF_DSI3;
		break;
	case DDP_COMPONENT_DPI0:
		sof_id = MUTEX_SOF_DPI0;
		break;
	case DDP_COMPONENT_DPI1:
		sof_id = MUTEX_SOF_DPI1;
		break;
	case DDP_COMPONENT_DP_INTF0:
		sof_id = MUTEX_SOF_DP_INTF0;
		break;
	case DDP_COMPONENT_DP_INTF1:
		sof_id = MUTEX_SOF_DP_INTF1;
		break;
	default:
		if (mtx->data->mutex_mod[id] < 32) {
			offset = DISP_REG_MUTEX_MOD(mtx->data->mutex_mod_reg,
						    mutex->id);
			reg = readl_relaxed(mtx->regs + offset);
			reg |= 1 << mtx->data->mutex_mod[id];
			writel_relaxed(reg, mtx->regs + offset);
		} else {
			offset = DISP_REG_MUTEX_MOD2(mutex->id);
			reg = readl_relaxed(mtx->regs + offset);
			reg |= 1 << (mtx->data->mutex_mod[id] - 32);
			writel_relaxed(reg, mtx->regs + offset);
		}
		return;
	}

	writel_relaxed(mtx->data->mutex_sof[sof_id],
		       mtx->regs +
		       DISP_REG_MUTEX_SOF(mtx->data->mutex_sof_reg, mutex->id));
}
EXPORT_SYMBOL_GPL(mtk_mutex_add_comp);

void mtk_mutex_remove_comp(struct mtk_mutex *mutex,
			   enum mtk_ddp_comp_id id)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);
	unsigned int reg;
	unsigned int offset;

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	switch (id) {
	case DDP_COMPONENT_DSI0:
	case DDP_COMPONENT_DSI1:
	case DDP_COMPONENT_DSI2:
	case DDP_COMPONENT_DSI3:
	case DDP_COMPONENT_DPI0:
	case DDP_COMPONENT_DPI1:
	case DDP_COMPONENT_DP_INTF0:
	case DDP_COMPONENT_DP_INTF1:
		writel_relaxed(MUTEX_SOF_SINGLE_MODE,
			       mtx->regs +
			       DISP_REG_MUTEX_SOF(mtx->data->mutex_sof_reg,
						  mutex->id));
		break;
	default:
		if (mtx->data->mutex_mod[id] < 32) {
			offset = DISP_REG_MUTEX_MOD(mtx->data->mutex_mod_reg,
						    mutex->id);
			reg = readl_relaxed(mtx->regs + offset);
			reg &= ~(1 << mtx->data->mutex_mod[id]);
			writel_relaxed(reg, mtx->regs + offset);
		} else {
			offset = DISP_REG_MUTEX_MOD2(mutex->id);
			reg = readl_relaxed(mtx->regs + offset);
			reg &= ~(1 << (mtx->data->mutex_mod[id] - 32));
			writel_relaxed(reg, mtx->regs + offset);
		}
		break;
	}
}
EXPORT_SYMBOL_GPL(mtk_mutex_remove_comp);

void mtk_mutex_enable(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	writel(1, mtx->regs + DISP_REG_MUTEX_EN(mutex->id));
}
EXPORT_SYMBOL_GPL(mtk_mutex_enable);

int mtk_mutex_enable_by_cmdq(struct mtk_mutex *mutex, void *pkt)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);
	struct cmdq_pkt *cmdq_pkt = (struct cmdq_pkt *)pkt;

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	if (!mtx->cmdq_reg.size) {
		dev_err(mtx->dev, "mediatek,gce-client-reg hasn't been set");
		return -ENODEV;
	}

	cmdq_pkt_write(cmdq_pkt, mtx->cmdq_reg.subsys,
		       mtx->addr + DISP_REG_MUTEX_EN(mutex->id), 1);
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_mutex_enable_by_cmdq);

void mtk_mutex_disable(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	writel(0, mtx->regs + DISP_REG_MUTEX_EN(mutex->id));
}
EXPORT_SYMBOL_GPL(mtk_mutex_disable);

void mtk_mutex_acquire(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);
	u32 tmp;

	writel(1, mtx->regs + DISP_REG_MUTEX_EN(mutex->id));
	writel(1, mtx->regs + DISP_REG_MUTEX(mutex->id));
	if (readl_poll_timeout_atomic(mtx->regs + DISP_REG_MUTEX(mutex->id),
				      tmp, tmp & INT_MUTEX, 1, 10000))
		pr_err("could not acquire mutex %d\n", mutex->id);
}
EXPORT_SYMBOL_GPL(mtk_mutex_acquire);

void mtk_mutex_release(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);

	writel(0, mtx->regs + DISP_REG_MUTEX(mutex->id));
}
EXPORT_SYMBOL_GPL(mtk_mutex_release);

int mtk_mutex_write_mod(struct mtk_mutex *mutex,
			enum mtk_mutex_mod_index idx, bool clear)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);
	unsigned int reg;
	u32 reg_offset, id_offset = 0;

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	if (idx < MUTEX_MOD_IDX_MDP_RDMA0 ||
	    idx >= MUTEX_MOD_IDX_MAX) {
		dev_err(mtx->dev, "Not supported MOD table index : %d", idx);
		return -EINVAL;
	}

	/*
	 * Some SoCs may have multiple MUTEX_MOD registers as more than 32 mods
	 * are present, hence requiring multiple 32-bits registers.
	 *
	 * The mutex_table_mod fully represents that by defining the number of
	 * the mod sequentially, later used as a bit number, which can be more
	 * than 0..31.
	 *
	 * In order to retain compatibility with older SoCs, we perform R/W on
	 * the single 32 bits registers, but this requires us to translate the
	 * mutex ID bit accordingly.
	 */
	if (mtx->data->mutex_table_mod[idx] < 32) {
		reg_offset = DISP_REG_MUTEX_MOD(mtx->data->mutex_mod_reg,
						mutex->id);
	} else {
		reg_offset = DISP_REG_MUTEX_MOD1(mtx->data->mutex_mod_reg,
						 mutex->id);
		id_offset = 32;
	}

	reg = readl_relaxed(mtx->regs + reg_offset);
	if (clear)
		reg &= ~BIT(mtx->data->mutex_table_mod[idx] - id_offset);
	else
		reg |= BIT(mtx->data->mutex_table_mod[idx] - id_offset);

	writel_relaxed(reg, mtx->regs + reg_offset);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_mutex_write_mod);

int mtk_mutex_write_sof(struct mtk_mutex *mutex,
			enum mtk_mutex_sof_index idx)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	if (idx < MUTEX_SOF_IDX_SINGLE_MODE ||
	    idx >= MUTEX_SOF_IDX_MAX) {
		dev_err(mtx->dev, "Not supported SOF index : %d", idx);
		return -EINVAL;
	}

	writel_relaxed(idx, mtx->regs +
		       DISP_REG_MUTEX_SOF(mtx->data->mutex_sof_reg, mutex->id));

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_mutex_write_sof);

static int mtk_mutex_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_mutex_ctx *mtx;
	struct resource *regs;
	int i, ret;

	mtx = devm_kzalloc(dev, sizeof(*mtx), GFP_KERNEL);
	if (!mtx)
		return -ENOMEM;

	for (i = 0; i < MTK_MUTEX_MAX_HANDLES; i++)
		mtx->mutex[i].id = i;

	mtx->data = of_device_get_match_data(dev);

	if (!mtx->data->no_clk) {
		mtx->clk = devm_clk_get(dev, NULL);
		if (IS_ERR(mtx->clk))
			return dev_err_probe(dev, PTR_ERR(mtx->clk), "Failed to get clock\n");
	}

	mtx->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &regs);
	if (IS_ERR(mtx->regs)) {
		dev_err(dev, "Failed to map mutex registers\n");
		return PTR_ERR(mtx->regs);
	}
	mtx->addr = regs->start;

	/* CMDQ is optional */
	ret = cmdq_dev_get_client_reg(dev, &mtx->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "No mediatek,gce-client-reg!\n");

	platform_set_drvdata(pdev, mtx);

	return 0;
}

static const struct of_device_id mutex_driver_dt_match[] = {
	{ .compatible = "mediatek,mt2701-disp-mutex", .data = &mt2701_mutex_driver_data },
	{ .compatible = "mediatek,mt2712-disp-mutex", .data = &mt2712_mutex_driver_data },
	{ .compatible = "mediatek,mt6795-disp-mutex", .data = &mt6795_mutex_driver_data },
	{ .compatible = "mediatek,mt8167-disp-mutex", .data = &mt8167_mutex_driver_data },
	{ .compatible = "mediatek,mt8173-disp-mutex", .data = &mt8173_mutex_driver_data },
	{ .compatible = "mediatek,mt8183-disp-mutex", .data = &mt8183_mutex_driver_data },
	{ .compatible = "mediatek,mt8186-disp-mutex", .data = &mt8186_mutex_driver_data },
	{ .compatible = "mediatek,mt8186-mdp3-mutex", .data = &mt8186_mdp_mutex_driver_data },
	{ .compatible = "mediatek,mt8188-disp-mutex", .data = &mt8188_mutex_driver_data },
	{ .compatible = "mediatek,mt8192-disp-mutex", .data = &mt8192_mutex_driver_data },
	{ .compatible = "mediatek,mt8195-disp-mutex", .data = &mt8195_mutex_driver_data },
	{ .compatible = "mediatek,mt8195-vpp-mutex",  .data = &mt8195_vpp_mutex_driver_data },
	{ .compatible = "mediatek,mt8365-disp-mutex", .data = &mt8365_mutex_driver_data },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mutex_driver_dt_match);

static struct platform_driver mtk_mutex_driver = {
	.probe		= mtk_mutex_probe,
	.driver		= {
		.name	= "mediatek-mutex",
		.of_match_table = mutex_driver_dt_match,
	},
};
module_platform_driver(mtk_mutex_driver);

MODULE_AUTHOR("Yongqiang Niu <yongqiang.niu@mediatek.com>");
MODULE_DESCRIPTION("MediaTek SoC MUTEX driver");
MODULE_LICENSE("GPL");
