// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Renesas R-Car MIPI CSI-2 Receiver
 *
 * Copyright (C) 2018 Renesas Electronics Corp.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/sys_soc.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

struct rcar_csi2;

/* Register offsets and bits */

/* Control Timing Select */
#define TREF_REG			0x00
#define TREF_TREF			BIT(0)

/* Software Reset */
#define SRST_REG			0x04
#define SRST_SRST			BIT(0)

/* PHY Operation Control */
#define PHYCNT_REG			0x08
#define PHYCNT_SHUTDOWNZ		BIT(17)
#define PHYCNT_RSTZ			BIT(16)
#define PHYCNT_ENABLECLK		BIT(4)
#define PHYCNT_ENABLE_3			BIT(3)
#define PHYCNT_ENABLE_2			BIT(2)
#define PHYCNT_ENABLE_1			BIT(1)
#define PHYCNT_ENABLE_0			BIT(0)

/* Checksum Control */
#define CHKSUM_REG			0x0c
#define CHKSUM_ECC_EN			BIT(1)
#define CHKSUM_CRC_EN			BIT(0)

/*
 * Channel Data Type Select
 * VCDT[0-15]:  Channel 0 VCDT[16-31]:  Channel 1
 * VCDT2[0-15]: Channel 2 VCDT2[16-31]: Channel 3
 */
#define VCDT_REG			0x10
#define VCDT2_REG			0x14
#define VCDT_VCDTN_EN			BIT(15)
#define VCDT_SEL_VC(n)			(((n) & 0x3) << 8)
#define VCDT_SEL_DTN_ON			BIT(6)
#define VCDT_SEL_DT(n)			(((n) & 0x3f) << 0)

/* Frame Data Type Select */
#define FRDT_REG			0x18

/* Field Detection Control */
#define FLD_REG				0x1c
#define FLD_FLD_NUM(n)			(((n) & 0xff) << 16)
#define FLD_DET_SEL(n)			(((n) & 0x3) << 4)
#define FLD_FLD_EN4			BIT(3)
#define FLD_FLD_EN3			BIT(2)
#define FLD_FLD_EN2			BIT(1)
#define FLD_FLD_EN			BIT(0)

/* Automatic Standby Control */
#define ASTBY_REG			0x20

/* Long Data Type Setting 0 */
#define LNGDT0_REG			0x28

/* Long Data Type Setting 1 */
#define LNGDT1_REG			0x2c

/* Interrupt Enable */
#define INTEN_REG			0x30
#define INTEN_INT_AFIFO_OF		BIT(27)
#define INTEN_INT_ERRSOTHS		BIT(4)
#define INTEN_INT_ERRSOTSYNCHS		BIT(3)

/* Interrupt Source Mask */
#define INTCLOSE_REG			0x34

/* Interrupt Status Monitor */
#define INTSTATE_REG			0x38
#define INTSTATE_INT_ULPS_START		BIT(7)
#define INTSTATE_INT_ULPS_END		BIT(6)

/* Interrupt Error Status Monitor */
#define INTERRSTATE_REG			0x3c

/* Short Packet Data */
#define SHPDAT_REG			0x40

/* Short Packet Count */
#define SHPCNT_REG			0x44

/* LINK Operation Control */
#define LINKCNT_REG			0x48
#define LINKCNT_MONITOR_EN		BIT(31)
#define LINKCNT_REG_MONI_PACT_EN	BIT(25)
#define LINKCNT_ICLK_NONSTOP		BIT(24)

/* Lane Swap */
#define LSWAP_REG			0x4c
#define LSWAP_L3SEL(n)			(((n) & 0x3) << 6)
#define LSWAP_L2SEL(n)			(((n) & 0x3) << 4)
#define LSWAP_L1SEL(n)			(((n) & 0x3) << 2)
#define LSWAP_L0SEL(n)			(((n) & 0x3) << 0)

/* PHY Test Interface Write Register */
#define PHTW_REG			0x50
#define PHTW_DWEN			BIT(24)
#define PHTW_TESTDIN_DATA(n)		(((n & 0xff)) << 16)
#define PHTW_CWEN			BIT(8)
#define PHTW_TESTDIN_CODE(n)		((n & 0xff))

#define PHYFRX_REG			0x64
#define PHYFRX_FORCERX_MODE_3		BIT(3)
#define PHYFRX_FORCERX_MODE_2		BIT(2)
#define PHYFRX_FORCERX_MODE_1		BIT(1)
#define PHYFRX_FORCERX_MODE_0		BIT(0)

/* V4H BASE registers */
#define V4H_N_LANES_REG					0x0004
#define V4H_CSI2_RESETN_REG				0x0008

#define V4H_PHY_MODE_REG				0x001c
#define V4H_PHY_MODE_DPHY				0
#define V4H_PHY_MODE_CPHY				1

#define V4H_PHY_SHUTDOWNZ_REG				0x0040
#define V4H_DPHY_RSTZ_REG				0x0044
#define V4H_FLDC_REG					0x0804
#define V4H_FLDD_REG					0x0808
#define V4H_IDIC_REG					0x0810

#define V4H_PHY_EN_REG					0x2000
#define V4H_PHY_EN_ENABLE_3				BIT(7)
#define V4H_PHY_EN_ENABLE_2				BIT(6)
#define V4H_PHY_EN_ENABLE_1				BIT(5)
#define V4H_PHY_EN_ENABLE_0				BIT(4)
#define V4H_PHY_EN_ENABLE_CLK				BIT(0)

#define V4H_ST_PHYST_REG				0x2814
#define V4H_ST_PHYST_ST_PHY_READY			BIT(31)
#define V4H_ST_PHYST_ST_STOPSTATE_3			BIT(3)
#define V4H_ST_PHYST_ST_STOPSTATE_2			BIT(2)
#define V4H_ST_PHYST_ST_STOPSTATE_1			BIT(1)
#define V4H_ST_PHYST_ST_STOPSTATE_0			BIT(0)

/* V4H PPI registers */
#define V4H_PPI_STARTUP_RW_COMMON_DPHY_REG(n)		(0x21800 + ((n) * 2)) /* n = 0 - 9 */
#define V4H_PPI_STARTUP_RW_COMMON_STARTUP_1_1_REG	0x21822
#define V4H_PPI_CALIBCTRL_RW_COMMON_BG_0_REG		0x2184c
#define V4H_PPI_RW_LPDCOCAL_TIMEBASE_REG		0x21c02
#define V4H_PPI_RW_LPDCOCAL_NREF_REG			0x21c04
#define V4H_PPI_RW_LPDCOCAL_NREF_RANGE_REG		0x21c06
#define V4H_PPI_RW_LPDCOCAL_TWAIT_CONFIG_REG		0x21c0a
#define V4H_PPI_RW_LPDCOCAL_VT_CONFIG_REG		0x21c0c
#define V4H_PPI_RW_LPDCOCAL_COARSE_CFG_REG		0x21c10
#define V4H_PPI_RW_COMMON_CFG_REG			0x21c6c
#define V4H_PPI_RW_TERMCAL_CFG_0_REG			0x21c80
#define V4H_PPI_RW_OFFSETCAL_CFG_0_REG			0x21ca0

/* V4H CORE registers */
#define V4H_CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_REG(n)	(0x22040 + ((n) * 2)) /* n = 0 - 15 */
#define V4H_CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_REG(n)	(0x22440 + ((n) * 2)) /* n = 0 - 15 */
#define V4H_CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_REG(n)	(0x22840 + ((n) * 2)) /* n = 0 - 15 */
#define V4H_CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_REG(n)	(0x22c40 + ((n) * 2)) /* n = 0 - 15 */
#define V4H_CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_REG(n)	(0x23040 + ((n) * 2)) /* n = 0 - 15 */
#define V4H_CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_REG(n)	(0x23840 + ((n) * 2)) /* n = 0 - 11 */
#define V4H_CORE_DIG_RW_COMMON_REG(n)			(0x23880 + ((n) * 2)) /* n = 0 - 15 */
#define V4H_CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_REG(n)	(0x239e0 + ((n) * 2)) /* n = 0 - 3 */
#define V4H_CORE_DIG_CLANE_1_RW_HS_TX_6_REG		0x2a60c

/* V4H C-PHY */
#define V4H_CORE_DIG_RW_TRIO0_REG(n)			(0x22100 + ((n) * 2)) /* n = 0 - 3 */
#define V4H_CORE_DIG_RW_TRIO1_REG(n)			(0x22500 + ((n) * 2)) /* n = 0 - 3 */
#define V4H_CORE_DIG_RW_TRIO2_REG(n)			(0x22900 + ((n) * 2)) /* n = 0 - 3 */
#define V4H_CORE_DIG_CLANE_0_RW_CFG_0_REG		0x2a000
#define V4H_CORE_DIG_CLANE_0_RW_LP_0_REG		0x2a080
#define V4H_CORE_DIG_CLANE_0_RW_HS_RX_REG(n)		(0x2a100 + ((n) * 2)) /* n = 0 - 6 */
#define V4H_CORE_DIG_CLANE_1_RW_CFG_0_REG		0x2a400
#define V4H_CORE_DIG_CLANE_1_RW_LP_0_REG		0x2a480
#define V4H_CORE_DIG_CLANE_1_RW_HS_RX_REG(n)		(0x2a500 + ((n) * 2)) /* n = 0 - 6 */
#define V4H_CORE_DIG_CLANE_2_RW_CFG_0_REG		0x2a800
#define V4H_CORE_DIG_CLANE_2_RW_LP_0_REG		0x2a880
#define V4H_CORE_DIG_CLANE_2_RW_HS_RX_REG(n)		(0x2a900 + ((n) * 2)) /* n = 0 - 6 */

struct rcsi2_cphy_setting {
	u16 msps;
	u16 rx2;
	u16 trio0;
	u16 trio1;
	u16 trio2;
	u16 lane27;
	u16 lane29;
};

static const struct rcsi2_cphy_setting cphy_setting_table_r8a779g0[] = {
	{ .msps =   80, .rx2 = 0x38, .trio0 = 0x024a, .trio1 = 0x0134, .trio2 = 0x6a, .lane27 = 0x0000, .lane29 = 0x0a24 },
	{ .msps =  100, .rx2 = 0x38, .trio0 = 0x024a, .trio1 = 0x00f5, .trio2 = 0x55, .lane27 = 0x0000, .lane29 = 0x0a24 },
	{ .msps =  200, .rx2 = 0x38, .trio0 = 0x024a, .trio1 = 0x0077, .trio2 = 0x2b, .lane27 = 0x0000, .lane29 = 0x0a44 },
	{ .msps =  300, .rx2 = 0x38, .trio0 = 0x024a, .trio1 = 0x004d, .trio2 = 0x1d, .lane27 = 0x0000, .lane29 = 0x0a44 },
	{ .msps =  400, .rx2 = 0x38, .trio0 = 0x024a, .trio1 = 0x0038, .trio2 = 0x16, .lane27 = 0x0000, .lane29 = 0x0a64 },
	{ .msps =  500, .rx2 = 0x38, .trio0 = 0x024a, .trio1 = 0x002b, .trio2 = 0x12, .lane27 = 0x0000, .lane29 = 0x0a64 },
	{ .msps =  600, .rx2 = 0x38, .trio0 = 0x024a, .trio1 = 0x0023, .trio2 = 0x0f, .lane27 = 0x0000, .lane29 = 0x0a64 },
	{ .msps =  700, .rx2 = 0x38, .trio0 = 0x024a, .trio1 = 0x001d, .trio2 = 0x0d, .lane27 = 0x0000, .lane29 = 0x0a84 },
	{ .msps =  800, .rx2 = 0x38, .trio0 = 0x024a, .trio1 = 0x0018, .trio2 = 0x0c, .lane27 = 0x0000, .lane29 = 0x0a84 },
	{ .msps =  900, .rx2 = 0x38, .trio0 = 0x024a, .trio1 = 0x0015, .trio2 = 0x0b, .lane27 = 0x0000, .lane29 = 0x0a84 },
	{ .msps = 1000, .rx2 = 0x3e, .trio0 = 0x024a, .trio1 = 0x0012, .trio2 = 0x0a, .lane27 = 0x0400, .lane29 = 0x0a84 },
	{ .msps = 1100, .rx2 = 0x44, .trio0 = 0x024a, .trio1 = 0x000f, .trio2 = 0x09, .lane27 = 0x0800, .lane29 = 0x0a84 },
	{ .msps = 1200, .rx2 = 0x4a, .trio0 = 0x024a, .trio1 = 0x000e, .trio2 = 0x08, .lane27 = 0x0c00, .lane29 = 0x0a84 },
	{ .msps = 1300, .rx2 = 0x51, .trio0 = 0x024a, .trio1 = 0x000c, .trio2 = 0x08, .lane27 = 0x0c00, .lane29 = 0x0aa4 },
	{ .msps = 1400, .rx2 = 0x57, .trio0 = 0x024a, .trio1 = 0x000b, .trio2 = 0x07, .lane27 = 0x1000, .lane29 = 0x0aa4 },
	{ .msps = 1500, .rx2 = 0x5d, .trio0 = 0x044a, .trio1 = 0x0009, .trio2 = 0x07, .lane27 = 0x1000, .lane29 = 0x0aa4 },
	{ .msps = 1600, .rx2 = 0x63, .trio0 = 0x044a, .trio1 = 0x0008, .trio2 = 0x07, .lane27 = 0x1400, .lane29 = 0x0aa4 },
	{ .msps = 1700, .rx2 = 0x6a, .trio0 = 0x044a, .trio1 = 0x0007, .trio2 = 0x06, .lane27 = 0x1400, .lane29 = 0x0aa4 },
	{ .msps = 1800, .rx2 = 0x70, .trio0 = 0x044a, .trio1 = 0x0007, .trio2 = 0x06, .lane27 = 0x1400, .lane29 = 0x0aa4 },
	{ .msps = 1900, .rx2 = 0x76, .trio0 = 0x044a, .trio1 = 0x0006, .trio2 = 0x06, .lane27 = 0x1400, .lane29 = 0x0aa4 },
	{ .msps = 2000, .rx2 = 0x7c, .trio0 = 0x044a, .trio1 = 0x0005, .trio2 = 0x06, .lane27 = 0x1800, .lane29 = 0x0aa4 },
	{ .msps = 2100, .rx2 = 0x83, .trio0 = 0x044a, .trio1 = 0x0005, .trio2 = 0x05, .lane27 = 0x1800, .lane29 = 0x0aa4 },
	{ .msps = 2200, .rx2 = 0x89, .trio0 = 0x064a, .trio1 = 0x0004, .trio2 = 0x05, .lane27 = 0x1800, .lane29 = 0x0aa4 },
	{ .msps = 2300, .rx2 = 0x8f, .trio0 = 0x064a, .trio1 = 0x0003, .trio2 = 0x05, .lane27 = 0x1800, .lane29 = 0x0aa4 },
	{ .msps = 2400, .rx2 = 0x95, .trio0 = 0x064a, .trio1 = 0x0003, .trio2 = 0x05, .lane27 = 0x1800, .lane29 = 0x0aa4 },
	{ .msps = 2500, .rx2 = 0x9c, .trio0 = 0x064a, .trio1 = 0x0003, .trio2 = 0x05, .lane27 = 0x1c00, .lane29 = 0x0aa4 },
	{ .msps = 2600, .rx2 = 0xa2, .trio0 = 0x064a, .trio1 = 0x0002, .trio2 = 0x05, .lane27 = 0x1c00, .lane29 = 0x0ad4 },
	{ .msps = 2700, .rx2 = 0xa8, .trio0 = 0x064a, .trio1 = 0x0002, .trio2 = 0x05, .lane27 = 0x1c00, .lane29 = 0x0ad4 },
	{ .msps = 2800, .rx2 = 0xae, .trio0 = 0x064a, .trio1 = 0x0002, .trio2 = 0x04, .lane27 = 0x1c00, .lane29 = 0x0ad4 },
	{ .msps = 2900, .rx2 = 0xb5, .trio0 = 0x084a, .trio1 = 0x0001, .trio2 = 0x04, .lane27 = 0x1c00, .lane29 = 0x0ad4 },
	{ .msps = 3000, .rx2 = 0xbb, .trio0 = 0x084a, .trio1 = 0x0001, .trio2 = 0x04, .lane27 = 0x1c00, .lane29 = 0x0ad4 },
	{ .msps = 3100, .rx2 = 0xc1, .trio0 = 0x084a, .trio1 = 0x0001, .trio2 = 0x04, .lane27 = 0x1c00, .lane29 = 0x0ad4 },
	{ .msps = 3200, .rx2 = 0xc7, .trio0 = 0x084a, .trio1 = 0x0001, .trio2 = 0x04, .lane27 = 0x1c00, .lane29 = 0x0ad4 },
	{ .msps = 3300, .rx2 = 0xce, .trio0 = 0x084a, .trio1 = 0x0001, .trio2 = 0x04, .lane27 = 0x1c00, .lane29 = 0x0ad4 },
	{ .msps = 3400, .rx2 = 0xd4, .trio0 = 0x084a, .trio1 = 0x0001, .trio2 = 0x04, .lane27 = 0x1c00, .lane29 = 0x0ad4 },
	{ .msps = 3500, .rx2 = 0xda, .trio0 = 0x084a, .trio1 = 0x0001, .trio2 = 0x04, .lane27 = 0x1c00, .lane29 = 0x0ad4 },
	{ /* sentinel */ },
};

/* V4M registers */
#define V4M_OVR1_REG					0x0848
#define V4M_OVR1_FORCERXMODE_3				BIT(12)
#define V4M_OVR1_FORCERXMODE_2				BIT(11)
#define V4M_OVR1_FORCERXMODE_1				BIT(10)
#define V4M_OVR1_FORCERXMODE_0				BIT(9)

#define V4M_FRXM_REG					0x2004
#define V4M_FRXM_FORCERXMODE_3				BIT(3)
#define V4M_FRXM_FORCERXMODE_2				BIT(2)
#define V4M_FRXM_FORCERXMODE_1				BIT(1)
#define V4M_FRXM_FORCERXMODE_0				BIT(0)

#define V4M_PHYPLL_REG					0x02050
#define V4M_CSI0CLKFCPR_REG				0x02054
#define V4M_PHTW_REG					0x02060
#define V4M_PHTR_REG					0x02064
#define V4M_PHTC_REG					0x02068

struct phtw_value {
	u8 data;
	u8 code;
};

struct rcsi2_mbps_info {
	u16 mbps;
	u8 reg;
	u16 osc_freq; /* V4M */
};

static const struct rcsi2_mbps_info phtw_mbps_v3u[] = {
	{ .mbps = 1500, .reg = 0xcc },
	{ .mbps = 1550, .reg = 0x1d },
	{ .mbps = 1600, .reg = 0x27 },
	{ .mbps = 1650, .reg = 0x30 },
	{ .mbps = 1700, .reg = 0x39 },
	{ .mbps = 1750, .reg = 0x42 },
	{ .mbps = 1800, .reg = 0x4b },
	{ .mbps = 1850, .reg = 0x55 },
	{ .mbps = 1900, .reg = 0x5e },
	{ .mbps = 1950, .reg = 0x67 },
	{ .mbps = 2000, .reg = 0x71 },
	{ .mbps = 2050, .reg = 0x79 },
	{ .mbps = 2100, .reg = 0x83 },
	{ .mbps = 2150, .reg = 0x8c },
	{ .mbps = 2200, .reg = 0x95 },
	{ .mbps = 2250, .reg = 0x9e },
	{ .mbps = 2300, .reg = 0xa7 },
	{ .mbps = 2350, .reg = 0xb0 },
	{ .mbps = 2400, .reg = 0xba },
	{ .mbps = 2450, .reg = 0xc3 },
	{ .mbps = 2500, .reg = 0xcc },
	{ /* sentinel */ },
};

static const struct rcsi2_mbps_info phtw_mbps_h3_v3h_m3n[] = {
	{ .mbps =   80, .reg = 0x86 },
	{ .mbps =   90, .reg = 0x86 },
	{ .mbps =  100, .reg = 0x87 },
	{ .mbps =  110, .reg = 0x87 },
	{ .mbps =  120, .reg = 0x88 },
	{ .mbps =  130, .reg = 0x88 },
	{ .mbps =  140, .reg = 0x89 },
	{ .mbps =  150, .reg = 0x89 },
	{ .mbps =  160, .reg = 0x8a },
	{ .mbps =  170, .reg = 0x8a },
	{ .mbps =  180, .reg = 0x8b },
	{ .mbps =  190, .reg = 0x8b },
	{ .mbps =  205, .reg = 0x8c },
	{ .mbps =  220, .reg = 0x8d },
	{ .mbps =  235, .reg = 0x8e },
	{ .mbps =  250, .reg = 0x8e },
	{ /* sentinel */ },
};

static const struct rcsi2_mbps_info phtw_mbps_v3m_e3[] = {
	{ .mbps =   80, .reg = 0x00 },
	{ .mbps =   90, .reg = 0x20 },
	{ .mbps =  100, .reg = 0x40 },
	{ .mbps =  110, .reg = 0x02 },
	{ .mbps =  130, .reg = 0x22 },
	{ .mbps =  140, .reg = 0x42 },
	{ .mbps =  150, .reg = 0x04 },
	{ .mbps =  170, .reg = 0x24 },
	{ .mbps =  180, .reg = 0x44 },
	{ .mbps =  200, .reg = 0x06 },
	{ .mbps =  220, .reg = 0x26 },
	{ .mbps =  240, .reg = 0x46 },
	{ .mbps =  250, .reg = 0x08 },
	{ .mbps =  270, .reg = 0x28 },
	{ .mbps =  300, .reg = 0x0a },
	{ .mbps =  330, .reg = 0x2a },
	{ .mbps =  360, .reg = 0x4a },
	{ .mbps =  400, .reg = 0x0c },
	{ .mbps =  450, .reg = 0x2c },
	{ .mbps =  500, .reg = 0x0e },
	{ .mbps =  550, .reg = 0x2e },
	{ .mbps =  600, .reg = 0x10 },
	{ .mbps =  650, .reg = 0x30 },
	{ .mbps =  700, .reg = 0x12 },
	{ .mbps =  750, .reg = 0x32 },
	{ .mbps =  800, .reg = 0x52 },
	{ .mbps =  850, .reg = 0x72 },
	{ .mbps =  900, .reg = 0x14 },
	{ .mbps =  950, .reg = 0x34 },
	{ .mbps = 1000, .reg = 0x54 },
	{ .mbps = 1050, .reg = 0x74 },
	{ .mbps = 1125, .reg = 0x16 },
	{ /* sentinel */ },
};

/* PHY Test Interface Clear */
#define PHTC_REG			0x58
#define PHTC_TESTCLR			BIT(0)

/* PHY Frequency Control */
#define PHYPLL_REG			0x68
#define PHYPLL_HSFREQRANGE(n)		((n) << 16)

static const struct rcsi2_mbps_info hsfreqrange_v3u[] = {
	{ .mbps =   80, .reg = 0x00 },
	{ .mbps =   90, .reg = 0x10 },
	{ .mbps =  100, .reg = 0x20 },
	{ .mbps =  110, .reg = 0x30 },
	{ .mbps =  120, .reg = 0x01 },
	{ .mbps =  130, .reg = 0x11 },
	{ .mbps =  140, .reg = 0x21 },
	{ .mbps =  150, .reg = 0x31 },
	{ .mbps =  160, .reg = 0x02 },
	{ .mbps =  170, .reg = 0x12 },
	{ .mbps =  180, .reg = 0x22 },
	{ .mbps =  190, .reg = 0x32 },
	{ .mbps =  205, .reg = 0x03 },
	{ .mbps =  220, .reg = 0x13 },
	{ .mbps =  235, .reg = 0x23 },
	{ .mbps =  250, .reg = 0x33 },
	{ .mbps =  275, .reg = 0x04 },
	{ .mbps =  300, .reg = 0x14 },
	{ .mbps =  325, .reg = 0x25 },
	{ .mbps =  350, .reg = 0x35 },
	{ .mbps =  400, .reg = 0x05 },
	{ .mbps =  450, .reg = 0x16 },
	{ .mbps =  500, .reg = 0x26 },
	{ .mbps =  550, .reg = 0x37 },
	{ .mbps =  600, .reg = 0x07 },
	{ .mbps =  650, .reg = 0x18 },
	{ .mbps =  700, .reg = 0x28 },
	{ .mbps =  750, .reg = 0x39 },
	{ .mbps =  800, .reg = 0x09 },
	{ .mbps =  850, .reg = 0x19 },
	{ .mbps =  900, .reg = 0x29 },
	{ .mbps =  950, .reg = 0x3a },
	{ .mbps = 1000, .reg = 0x0a },
	{ .mbps = 1050, .reg = 0x1a },
	{ .mbps = 1100, .reg = 0x2a },
	{ .mbps = 1150, .reg = 0x3b },
	{ .mbps = 1200, .reg = 0x0b },
	{ .mbps = 1250, .reg = 0x1b },
	{ .mbps = 1300, .reg = 0x2b },
	{ .mbps = 1350, .reg = 0x3c },
	{ .mbps = 1400, .reg = 0x0c },
	{ .mbps = 1450, .reg = 0x1c },
	{ .mbps = 1500, .reg = 0x2c },
	{ .mbps = 1550, .reg = 0x3d },
	{ .mbps = 1600, .reg = 0x0d },
	{ .mbps = 1650, .reg = 0x1d },
	{ .mbps = 1700, .reg = 0x2e },
	{ .mbps = 1750, .reg = 0x3e },
	{ .mbps = 1800, .reg = 0x0e },
	{ .mbps = 1850, .reg = 0x1e },
	{ .mbps = 1900, .reg = 0x2f },
	{ .mbps = 1950, .reg = 0x3f },
	{ .mbps = 2000, .reg = 0x0f },
	{ .mbps = 2050, .reg = 0x40 },
	{ .mbps = 2100, .reg = 0x41 },
	{ .mbps = 2150, .reg = 0x42 },
	{ .mbps = 2200, .reg = 0x43 },
	{ .mbps = 2300, .reg = 0x45 },
	{ .mbps = 2350, .reg = 0x46 },
	{ .mbps = 2400, .reg = 0x47 },
	{ .mbps = 2450, .reg = 0x48 },
	{ .mbps = 2500, .reg = 0x49 },
	{ /* sentinel */ },
};

static const struct rcsi2_mbps_info hsfreqrange_h3_v3h_m3n[] = {
	{ .mbps =   80, .reg = 0x00 },
	{ .mbps =   90, .reg = 0x10 },
	{ .mbps =  100, .reg = 0x20 },
	{ .mbps =  110, .reg = 0x30 },
	{ .mbps =  120, .reg = 0x01 },
	{ .mbps =  130, .reg = 0x11 },
	{ .mbps =  140, .reg = 0x21 },
	{ .mbps =  150, .reg = 0x31 },
	{ .mbps =  160, .reg = 0x02 },
	{ .mbps =  170, .reg = 0x12 },
	{ .mbps =  180, .reg = 0x22 },
	{ .mbps =  190, .reg = 0x32 },
	{ .mbps =  205, .reg = 0x03 },
	{ .mbps =  220, .reg = 0x13 },
	{ .mbps =  235, .reg = 0x23 },
	{ .mbps =  250, .reg = 0x33 },
	{ .mbps =  275, .reg = 0x04 },
	{ .mbps =  300, .reg = 0x14 },
	{ .mbps =  325, .reg = 0x25 },
	{ .mbps =  350, .reg = 0x35 },
	{ .mbps =  400, .reg = 0x05 },
	{ .mbps =  450, .reg = 0x16 },
	{ .mbps =  500, .reg = 0x26 },
	{ .mbps =  550, .reg = 0x37 },
	{ .mbps =  600, .reg = 0x07 },
	{ .mbps =  650, .reg = 0x18 },
	{ .mbps =  700, .reg = 0x28 },
	{ .mbps =  750, .reg = 0x39 },
	{ .mbps =  800, .reg = 0x09 },
	{ .mbps =  850, .reg = 0x19 },
	{ .mbps =  900, .reg = 0x29 },
	{ .mbps =  950, .reg = 0x3a },
	{ .mbps = 1000, .reg = 0x0a },
	{ .mbps = 1050, .reg = 0x1a },
	{ .mbps = 1100, .reg = 0x2a },
	{ .mbps = 1150, .reg = 0x3b },
	{ .mbps = 1200, .reg = 0x0b },
	{ .mbps = 1250, .reg = 0x1b },
	{ .mbps = 1300, .reg = 0x2b },
	{ .mbps = 1350, .reg = 0x3c },
	{ .mbps = 1400, .reg = 0x0c },
	{ .mbps = 1450, .reg = 0x1c },
	{ .mbps = 1500, .reg = 0x2c },
	{ /* sentinel */ },
};

static const struct rcsi2_mbps_info hsfreqrange_m3w[] = {
	{ .mbps =   80,	.reg = 0x00 },
	{ .mbps =   90,	.reg = 0x10 },
	{ .mbps =  100,	.reg = 0x20 },
	{ .mbps =  110,	.reg = 0x30 },
	{ .mbps =  120,	.reg = 0x01 },
	{ .mbps =  130,	.reg = 0x11 },
	{ .mbps =  140,	.reg = 0x21 },
	{ .mbps =  150,	.reg = 0x31 },
	{ .mbps =  160,	.reg = 0x02 },
	{ .mbps =  170,	.reg = 0x12 },
	{ .mbps =  180,	.reg = 0x22 },
	{ .mbps =  190,	.reg = 0x32 },
	{ .mbps =  205,	.reg = 0x03 },
	{ .mbps =  220,	.reg = 0x13 },
	{ .mbps =  235,	.reg = 0x23 },
	{ .mbps =  250,	.reg = 0x33 },
	{ .mbps =  275,	.reg = 0x04 },
	{ .mbps =  300,	.reg = 0x14 },
	{ .mbps =  325,	.reg = 0x05 },
	{ .mbps =  350,	.reg = 0x15 },
	{ .mbps =  400,	.reg = 0x25 },
	{ .mbps =  450,	.reg = 0x06 },
	{ .mbps =  500,	.reg = 0x16 },
	{ .mbps =  550,	.reg = 0x07 },
	{ .mbps =  600,	.reg = 0x17 },
	{ .mbps =  650,	.reg = 0x08 },
	{ .mbps =  700,	.reg = 0x18 },
	{ .mbps =  750,	.reg = 0x09 },
	{ .mbps =  800,	.reg = 0x19 },
	{ .mbps =  850,	.reg = 0x29 },
	{ .mbps =  900,	.reg = 0x39 },
	{ .mbps =  950,	.reg = 0x0a },
	{ .mbps = 1000,	.reg = 0x1a },
	{ .mbps = 1050,	.reg = 0x2a },
	{ .mbps = 1100,	.reg = 0x3a },
	{ .mbps = 1150,	.reg = 0x0b },
	{ .mbps = 1200,	.reg = 0x1b },
	{ .mbps = 1250,	.reg = 0x2b },
	{ .mbps = 1300,	.reg = 0x3b },
	{ .mbps = 1350,	.reg = 0x0c },
	{ .mbps = 1400,	.reg = 0x1c },
	{ .mbps = 1450,	.reg = 0x2c },
	{ .mbps = 1500,	.reg = 0x3c },
	{ /* sentinel */ },
};

static const struct rcsi2_mbps_info hsfreqrange_v4m[] = {
	{ .mbps =   80, .reg = 0x00, .osc_freq = 0x01a9 },
	{ .mbps =   90, .reg = 0x10, .osc_freq = 0x01a9 },
	{ .mbps =  100, .reg = 0x20, .osc_freq = 0x01a9 },
	{ .mbps =  110, .reg = 0x30, .osc_freq = 0x01a9 },
	{ .mbps =  120, .reg = 0x01, .osc_freq = 0x01a9 },
	{ .mbps =  130, .reg = 0x11, .osc_freq = 0x01a9 },
	{ .mbps =  140, .reg = 0x21, .osc_freq = 0x01a9 },
	{ .mbps =  150, .reg = 0x31, .osc_freq = 0x01a9 },
	{ .mbps =  160, .reg = 0x02, .osc_freq = 0x01a9 },
	{ .mbps =  170, .reg = 0x12, .osc_freq = 0x01a9 },
	{ .mbps =  180, .reg = 0x22, .osc_freq = 0x01a9 },
	{ .mbps =  190, .reg = 0x32, .osc_freq = 0x01a9 },
	{ .mbps =  205, .reg = 0x03, .osc_freq = 0x01a9 },
	{ .mbps =  220, .reg = 0x13, .osc_freq = 0x01a9 },
	{ .mbps =  235, .reg = 0x23, .osc_freq = 0x01a9 },
	{ .mbps =  250, .reg = 0x33, .osc_freq = 0x01a9 },
	{ .mbps =  275, .reg = 0x04, .osc_freq = 0x01a9 },
	{ .mbps =  300, .reg = 0x14, .osc_freq = 0x01a9 },
	{ .mbps =  325, .reg = 0x25, .osc_freq = 0x01a9 },
	{ .mbps =  350, .reg = 0x35, .osc_freq = 0x01a9 },
	{ .mbps =  400, .reg = 0x05, .osc_freq = 0x01a9 },
	{ .mbps =  450, .reg = 0x16, .osc_freq = 0x01a9 },
	{ .mbps =  500, .reg = 0x26, .osc_freq = 0x01a9 },
	{ .mbps =  550, .reg = 0x37, .osc_freq = 0x01a9 },
	{ .mbps =  600, .reg = 0x07, .osc_freq = 0x01a9 },
	{ .mbps =  650, .reg = 0x18, .osc_freq = 0x01a9 },
	{ .mbps =  700, .reg = 0x28, .osc_freq = 0x01a9 },
	{ .mbps =  750, .reg = 0x39, .osc_freq = 0x01a9 },
	{ .mbps =  800, .reg = 0x09, .osc_freq = 0x01a9 },
	{ .mbps =  850, .reg = 0x19, .osc_freq = 0x01a9 },
	{ .mbps =  900, .reg = 0x29, .osc_freq = 0x01a9 },
	{ .mbps =  950, .reg = 0x3a, .osc_freq = 0x01a9 },
	{ .mbps = 1000, .reg = 0x0a, .osc_freq = 0x01a9 },
	{ .mbps = 1050, .reg = 0x1a, .osc_freq = 0x01a9 },
	{ .mbps = 1100, .reg = 0x2a, .osc_freq = 0x01a9 },
	{ .mbps = 1150, .reg = 0x3b, .osc_freq = 0x01a9 },
	{ .mbps = 1200, .reg = 0x0b, .osc_freq = 0x01a9 },
	{ .mbps = 1250, .reg = 0x1b, .osc_freq = 0x01a9 },
	{ .mbps = 1300, .reg = 0x2b, .osc_freq = 0x01a9 },
	{ .mbps = 1350, .reg = 0x3c, .osc_freq = 0x01a9 },
	{ .mbps = 1400, .reg = 0x0c, .osc_freq = 0x01a9 },
	{ .mbps = 1450, .reg = 0x1c, .osc_freq = 0x01a9 },
	{ .mbps = 1500, .reg = 0x2c, .osc_freq = 0x01a9 },
	{ .mbps = 1550, .reg = 0x3d, .osc_freq = 0x0108 },
	{ .mbps = 1600, .reg = 0x0d, .osc_freq = 0x0110 },
	{ .mbps = 1650, .reg = 0x1d, .osc_freq = 0x0119 },
	{ .mbps = 1700, .reg = 0x2e, .osc_freq = 0x0121 },
	{ .mbps = 1750, .reg = 0x3e, .osc_freq = 0x012a },
	{ .mbps = 1800, .reg = 0x0e, .osc_freq = 0x0132 },
	{ .mbps = 1850, .reg = 0x1e, .osc_freq = 0x013b },
	{ .mbps = 1900, .reg = 0x2f, .osc_freq = 0x0143 },
	{ .mbps = 1950, .reg = 0x3f, .osc_freq = 0x014c },
	{ .mbps = 2000, .reg = 0x0f, .osc_freq = 0x0154 },
	{ .mbps = 2050, .reg = 0x40, .osc_freq = 0x015d },
	{ .mbps = 2100, .reg = 0x41, .osc_freq = 0x0165 },
	{ .mbps = 2150, .reg = 0x42, .osc_freq = 0x016e },
	{ .mbps = 2200, .reg = 0x43, .osc_freq = 0x0176 },
	{ .mbps = 2250, .reg = 0x44, .osc_freq = 0x017f },
	{ .mbps = 2300, .reg = 0x45, .osc_freq = 0x0187 },
	{ .mbps = 2350, .reg = 0x46, .osc_freq = 0x0190 },
	{ .mbps = 2400, .reg = 0x47, .osc_freq = 0x0198 },
	{ .mbps = 2450, .reg = 0x48, .osc_freq = 0x01a1 },
	{ .mbps = 2500, .reg = 0x49, .osc_freq = 0x01a9 },
	{ /* sentinel */ },
};

/* PHY ESC Error Monitor */
#define PHEERM_REG			0x74

/* PHY Clock Lane Monitor */
#define PHCLM_REG			0x78
#define PHCLM_STOPSTATECKL		BIT(0)

/* PHY Data Lane Monitor */
#define PHDLM_REG			0x7c

/* CSI0CLK Frequency Configuration Preset Register */
#define CSI0CLKFCPR_REG			0x260
#define CSI0CLKFREQRANGE(n)		((n & 0x3f) << 16)

struct rcar_csi2_format {
	u32 code;
	unsigned int datatype;
	unsigned int bpp;
};

static const struct rcar_csi2_format rcar_csi2_formats[] = {
	{
		.code = MEDIA_BUS_FMT_RGB888_1X24,
		.datatype = MIPI_CSI2_DT_RGB888,
		.bpp = 24,
	}, {
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.datatype = MIPI_CSI2_DT_YUV422_8B,
		.bpp = 16,
	}, {
		.code = MEDIA_BUS_FMT_YUYV8_1X16,
		.datatype = MIPI_CSI2_DT_YUV422_8B,
		.bpp = 16,
	}, {
		.code = MEDIA_BUS_FMT_UYVY8_2X8,
		.datatype = MIPI_CSI2_DT_YUV422_8B,
		.bpp = 16,
	}, {
		.code = MEDIA_BUS_FMT_YUYV10_2X10,
		.datatype = MIPI_CSI2_DT_YUV422_8B,
		.bpp = 20,
	}, {
		.code = MEDIA_BUS_FMT_Y10_1X10,
		.datatype = MIPI_CSI2_DT_RAW10,
		.bpp = 10,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.bpp = 8,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.bpp = 8,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.bpp = 8,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.bpp = 8,
	}, {
		.code = MEDIA_BUS_FMT_Y8_1X8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.bpp = 8,
	},
};

static const struct rcar_csi2_format *rcsi2_code_to_fmt(unsigned int code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rcar_csi2_formats); i++)
		if (rcar_csi2_formats[i].code == code)
			return &rcar_csi2_formats[i];

	return NULL;
}

struct rcsi2_cphy_line_order {
	enum v4l2_mbus_csi2_cphy_line_orders_type order;
	u16 cfg;
	u16 ctrl29;
};

static const struct rcsi2_cphy_line_order rcsi2_cphy_line_orders[] = {
	{ .order = V4L2_MBUS_CSI2_CPHY_LINE_ORDER_ABC, .cfg = 0x0, .ctrl29 = 0x0 },
	{ .order = V4L2_MBUS_CSI2_CPHY_LINE_ORDER_ACB, .cfg = 0xa, .ctrl29 = 0x1 },
	{ .order = V4L2_MBUS_CSI2_CPHY_LINE_ORDER_BAC, .cfg = 0xc, .ctrl29 = 0x1 },
	{ .order = V4L2_MBUS_CSI2_CPHY_LINE_ORDER_BCA, .cfg = 0x5, .ctrl29 = 0x0 },
	{ .order = V4L2_MBUS_CSI2_CPHY_LINE_ORDER_CAB, .cfg = 0x3, .ctrl29 = 0x0 },
	{ .order = V4L2_MBUS_CSI2_CPHY_LINE_ORDER_CBA, .cfg = 0x9, .ctrl29 = 0x1 }
};

enum rcar_csi2_pads {
	RCAR_CSI2_SINK,
	RCAR_CSI2_SOURCE_VC0,
	RCAR_CSI2_SOURCE_VC1,
	RCAR_CSI2_SOURCE_VC2,
	RCAR_CSI2_SOURCE_VC3,
	NR_OF_RCAR_CSI2_PAD,
};

struct rcsi2_register_layout {
	unsigned int phtw;
	unsigned int phypll;
};

struct rcar_csi2_info {
	const struct rcsi2_register_layout *regs;
	int (*init_phtw)(struct rcar_csi2 *priv, unsigned int mbps);
	int (*phy_post_init)(struct rcar_csi2 *priv);
	int (*start_receiver)(struct rcar_csi2 *priv,
			      struct v4l2_subdev_state *state);
	void (*enter_standby)(struct rcar_csi2 *priv);
	const struct rcsi2_mbps_info *hsfreqrange;
	unsigned int csi0clkfreqrange;
	unsigned int num_channels;
	bool clear_ulps;
	bool use_isp;
	bool support_dphy;
	bool support_cphy;
};

struct rcar_csi2 {
	struct device *dev;
	void __iomem *base;
	const struct rcar_csi2_info *info;
	struct reset_control *rstc;

	struct v4l2_subdev subdev;
	struct media_pad pads[NR_OF_RCAR_CSI2_PAD];

	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *remote;
	unsigned int remote_pad;

	int channel_vc[4];

	int stream_count;

	bool cphy;
	unsigned short lanes;
	unsigned char lane_swap[4];
	enum v4l2_mbus_csi2_cphy_line_orders_type line_orders[3];
};

static inline struct rcar_csi2 *sd_to_csi2(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rcar_csi2, subdev);
}

static inline struct rcar_csi2 *notifier_to_csi2(struct v4l2_async_notifier *n)
{
	return container_of(n, struct rcar_csi2, notifier);
}

static unsigned int rcsi2_num_pads(const struct rcar_csi2 *priv)
{
	/* Used together with R-Car ISP: one sink and one source pad. */
	if (priv->info->use_isp)
		return 2;

	/* Used together with R-Car VIN: one sink and four source pads. */
	return 5;
}

static u32 rcsi2_read(struct rcar_csi2 *priv, unsigned int reg)
{
	return ioread32(priv->base + reg);
}

static void rcsi2_write(struct rcar_csi2 *priv, unsigned int reg, u32 data)
{
	iowrite32(data, priv->base + reg);
}

static u16 rcsi2_read16(struct rcar_csi2 *priv, unsigned int reg)
{
	return ioread16(priv->base + reg);
}

static void rcsi2_write16(struct rcar_csi2 *priv, unsigned int reg, u16 data)
{
	iowrite16(data, priv->base + reg);
}

static void rcsi2_modify16(struct rcar_csi2 *priv, unsigned int reg, u16 data, u16 mask)
{
	u16 val;

	val = rcsi2_read16(priv, reg) & ~mask;
	rcsi2_write16(priv, reg, val | data);
}

static int rcsi2_phtw_write(struct rcar_csi2 *priv, u8 data, u8 code)
{
	unsigned int timeout;

	rcsi2_write(priv, priv->info->regs->phtw,
		    PHTW_DWEN | PHTW_TESTDIN_DATA(data) |
		    PHTW_CWEN | PHTW_TESTDIN_CODE(code));

	/* Wait for DWEN and CWEN to be cleared by hardware. */
	for (timeout = 0; timeout <= 20; timeout++) {
		if (!(rcsi2_read(priv, priv->info->regs->phtw) & (PHTW_DWEN | PHTW_CWEN)))
			return 0;

		usleep_range(1000, 2000);
	}

	dev_err(priv->dev, "Timeout waiting for PHTW_DWEN and/or PHTW_CWEN\n");

	return -ETIMEDOUT;
}

static int rcsi2_phtw_write_array(struct rcar_csi2 *priv,
				  const struct phtw_value *values,
				  unsigned int size)
{
	int ret;

	for (unsigned int i = 0; i < size; i++) {
		ret = rcsi2_phtw_write(priv, values[i].data, values[i].code);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct rcsi2_mbps_info *
rcsi2_mbps_to_info(struct rcar_csi2 *priv,
		   const struct rcsi2_mbps_info *infotable, unsigned int mbps)
{
	const struct rcsi2_mbps_info *info;
	const struct rcsi2_mbps_info *prev = NULL;

	if (mbps < infotable->mbps)
		dev_warn(priv->dev, "%u Mbps less than min PHY speed %u Mbps",
			 mbps, infotable->mbps);

	for (info = infotable; info->mbps != 0; info++) {
		if (info->mbps >= mbps)
			break;
		prev = info;
	}

	if (!info->mbps) {
		dev_err(priv->dev, "Unsupported PHY speed (%u Mbps)", mbps);
		return NULL;
	}

	if (prev && ((mbps - prev->mbps) <= (info->mbps - mbps)))
		info = prev;

	return info;
}

static void rcsi2_enter_standby_gen3(struct rcar_csi2 *priv)
{
	rcsi2_write(priv, PHYCNT_REG, 0);
	rcsi2_write(priv, PHTC_REG, PHTC_TESTCLR);
}

static void rcsi2_enter_standby(struct rcar_csi2 *priv)
{
	if (priv->info->enter_standby)
		priv->info->enter_standby(priv);

	reset_control_assert(priv->rstc);
	usleep_range(100, 150);
	pm_runtime_put(priv->dev);
}

static int rcsi2_exit_standby(struct rcar_csi2 *priv)
{
	int ret;

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret < 0)
		return ret;

	reset_control_deassert(priv->rstc);

	return 0;
}

static int rcsi2_wait_phy_start(struct rcar_csi2 *priv,
				unsigned int lanes)
{
	unsigned int timeout;

	/* Wait for the clock and data lanes to enter LP-11 state. */
	for (timeout = 0; timeout <= 20; timeout++) {
		const u32 lane_mask = (1 << lanes) - 1;

		if ((rcsi2_read(priv, PHCLM_REG) & PHCLM_STOPSTATECKL)  &&
		    (rcsi2_read(priv, PHDLM_REG) & lane_mask) == lane_mask)
			return 0;

		usleep_range(1000, 2000);
	}

	dev_err(priv->dev, "Timeout waiting for LP-11 state\n");

	return -ETIMEDOUT;
}

static int rcsi2_set_phypll(struct rcar_csi2 *priv, unsigned int mbps)
{
	const struct rcsi2_mbps_info *info;

	info = rcsi2_mbps_to_info(priv, priv->info->hsfreqrange, mbps);
	if (!info)
		return -ERANGE;

	rcsi2_write(priv, priv->info->regs->phypll, PHYPLL_HSFREQRANGE(info->reg));

	return 0;
}

static int rcsi2_calc_mbps(struct rcar_csi2 *priv, unsigned int bpp,
			   unsigned int lanes)
{
	struct v4l2_subdev *source;
	struct v4l2_ctrl *ctrl;
	u64 mbps;

	if (!priv->remote)
		return -ENODEV;

	source = priv->remote;

	/* Read the pixel rate control from remote. */
	ctrl = v4l2_ctrl_find(source->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!ctrl) {
		dev_err(priv->dev, "no pixel rate control in subdev %s\n",
			source->name);
		return -EINVAL;
	}

	/*
	 * Calculate the phypll in mbps.
	 * link_freq = (pixel_rate * bits_per_sample) / (2 * nr_of_lanes)
	 * bps = link_freq * 2
	 */
	mbps = v4l2_ctrl_g_ctrl_int64(ctrl) * bpp;
	do_div(mbps, lanes * 1000000);

	/* Adjust for C-PHY, divide by 2.8. */
	if (priv->cphy)
		mbps = div_u64(mbps * 5, 14);

	return mbps;
}

static int rcsi2_get_active_lanes(struct rcar_csi2 *priv,
				  unsigned int *lanes)
{
	struct v4l2_mbus_config mbus_config = { 0 };
	int ret;

	*lanes = priv->lanes;

	ret = v4l2_subdev_call(priv->remote, pad, get_mbus_config,
			       priv->remote_pad, &mbus_config);
	if (ret == -ENOIOCTLCMD) {
		dev_dbg(priv->dev, "No remote mbus configuration available\n");
		return 0;
	}

	if (ret) {
		dev_err(priv->dev, "Failed to get remote mbus configuration\n");
		return ret;
	}

	switch (mbus_config.type) {
	case V4L2_MBUS_CSI2_CPHY:
		if (!priv->cphy)
			return -EINVAL;
		break;
	case V4L2_MBUS_CSI2_DPHY:
		if (priv->cphy)
			return -EINVAL;
		break;
	default:
		dev_err(priv->dev, "Unsupported media bus type %u\n",
			mbus_config.type);
		return -EINVAL;
	}

	if (mbus_config.bus.mipi_csi2.num_data_lanes > priv->lanes) {
		dev_err(priv->dev,
			"Unsupported mbus config: too many data lanes %u\n",
			mbus_config.bus.mipi_csi2.num_data_lanes);
		return -EINVAL;
	}

	*lanes = mbus_config.bus.mipi_csi2.num_data_lanes;

	return 0;
}

static int rcsi2_start_receiver_gen3(struct rcar_csi2 *priv,
				     struct v4l2_subdev_state *state)
{
	const struct rcar_csi2_format *format;
	u32 phycnt, vcdt = 0, vcdt2 = 0, fld = 0;
	const struct v4l2_mbus_framefmt *fmt;
	unsigned int lanes;
	unsigned int i;
	int mbps, ret;

	/* Use the format on the sink pad to compute the receiver config. */
	fmt = v4l2_subdev_state_get_format(state, RCAR_CSI2_SINK);

	dev_dbg(priv->dev, "Input size (%ux%u%c)\n",
		fmt->width, fmt->height,
		fmt->field == V4L2_FIELD_NONE ? 'p' : 'i');

	/* Code is validated in set_fmt. */
	format = rcsi2_code_to_fmt(fmt->code);
	if (!format)
		return -EINVAL;

	/*
	 * Enable all supported CSI-2 channels with virtual channel and
	 * data type matching.
	 *
	 * NOTE: It's not possible to get individual datatype for each
	 *       source virtual channel. Once this is possible in V4L2
	 *       it should be used here.
	 */
	for (i = 0; i < priv->info->num_channels; i++) {
		u32 vcdt_part;

		if (priv->channel_vc[i] < 0)
			continue;

		vcdt_part = VCDT_SEL_VC(priv->channel_vc[i]) | VCDT_VCDTN_EN |
			VCDT_SEL_DTN_ON | VCDT_SEL_DT(format->datatype);

		/* Store in correct reg and offset. */
		if (i < 2)
			vcdt |= vcdt_part << ((i % 2) * 16);
		else
			vcdt2 |= vcdt_part << ((i % 2) * 16);
	}

	if (fmt->field == V4L2_FIELD_ALTERNATE) {
		fld = FLD_DET_SEL(1) | FLD_FLD_EN4 | FLD_FLD_EN3 | FLD_FLD_EN2
			| FLD_FLD_EN;

		if (fmt->height == 240)
			fld |= FLD_FLD_NUM(0);
		else
			fld |= FLD_FLD_NUM(1);
	}

	/*
	 * Get the number of active data lanes inspecting the remote mbus
	 * configuration.
	 */
	ret = rcsi2_get_active_lanes(priv, &lanes);
	if (ret)
		return ret;

	phycnt = PHYCNT_ENABLECLK;
	phycnt |= (1 << lanes) - 1;

	mbps = rcsi2_calc_mbps(priv, format->bpp, lanes);
	if (mbps < 0)
		return mbps;

	/* Enable interrupts. */
	rcsi2_write(priv, INTEN_REG, INTEN_INT_AFIFO_OF | INTEN_INT_ERRSOTHS
		    | INTEN_INT_ERRSOTSYNCHS);

	/* Init */
	rcsi2_write(priv, TREF_REG, TREF_TREF);
	rcsi2_write(priv, PHTC_REG, 0);

	/* Configure */
	if (!priv->info->use_isp) {
		rcsi2_write(priv, VCDT_REG, vcdt);
		if (vcdt2)
			rcsi2_write(priv, VCDT2_REG, vcdt2);
	}

	/* Lanes are zero indexed. */
	rcsi2_write(priv, LSWAP_REG,
		    LSWAP_L0SEL(priv->lane_swap[0] - 1) |
		    LSWAP_L1SEL(priv->lane_swap[1] - 1) |
		    LSWAP_L2SEL(priv->lane_swap[2] - 1) |
		    LSWAP_L3SEL(priv->lane_swap[3] - 1));

	/* Start */
	if (priv->info->init_phtw) {
		ret = priv->info->init_phtw(priv, mbps);
		if (ret)
			return ret;
	}

	if (priv->info->hsfreqrange) {
		ret = rcsi2_set_phypll(priv, mbps);
		if (ret)
			return ret;
	}

	if (priv->info->csi0clkfreqrange)
		rcsi2_write(priv, CSI0CLKFCPR_REG,
			    CSI0CLKFREQRANGE(priv->info->csi0clkfreqrange));

	if (priv->info->use_isp)
		rcsi2_write(priv, PHYFRX_REG,
			    PHYFRX_FORCERX_MODE_3 | PHYFRX_FORCERX_MODE_2 |
			    PHYFRX_FORCERX_MODE_1 | PHYFRX_FORCERX_MODE_0);

	rcsi2_write(priv, PHYCNT_REG, phycnt);
	rcsi2_write(priv, LINKCNT_REG, LINKCNT_MONITOR_EN |
		    LINKCNT_REG_MONI_PACT_EN | LINKCNT_ICLK_NONSTOP);
	rcsi2_write(priv, FLD_REG, fld);
	rcsi2_write(priv, PHYCNT_REG, phycnt | PHYCNT_SHUTDOWNZ);
	rcsi2_write(priv, PHYCNT_REG, phycnt | PHYCNT_SHUTDOWNZ | PHYCNT_RSTZ);

	ret = rcsi2_wait_phy_start(priv, lanes);
	if (ret)
		return ret;

	if (priv->info->use_isp)
		rcsi2_write(priv, PHYFRX_REG, 0);

	/* Run post PHY start initialization, if needed. */
	if (priv->info->phy_post_init) {
		ret = priv->info->phy_post_init(priv);
		if (ret)
			return ret;
	}

	/* Clear Ultra Low Power interrupt. */
	if (priv->info->clear_ulps)
		rcsi2_write(priv, INTSTATE_REG,
			    INTSTATE_INT_ULPS_START |
			    INTSTATE_INT_ULPS_END);
	return 0;
}

static void rsci2_set_line_order(struct rcar_csi2 *priv,
				 enum v4l2_mbus_csi2_cphy_line_orders_type order,
				 unsigned int cfgreg, unsigned int ctrlreg)
{
	const struct rcsi2_cphy_line_order *info = NULL;

	for (unsigned int i = 0; i < ARRAY_SIZE(rcsi2_cphy_line_orders); i++) {
		if (rcsi2_cphy_line_orders[i].order == order) {
			info = &rcsi2_cphy_line_orders[i];
			break;
		}
	}

	if (!info)
		return;

	rcsi2_modify16(priv, cfgreg, info->cfg, 0x000f);
	rcsi2_modify16(priv, ctrlreg, info->ctrl29, 0x0100);
}

static int rcsi2_wait_phy_start_v4h(struct rcar_csi2 *priv, u32 match)
{
	unsigned int timeout;
	u32 status;

	for (timeout = 0; timeout <= 10; timeout++) {
		status = rcsi2_read(priv, V4H_ST_PHYST_REG);
		if ((status & match) == match)
			return 0;

		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

static int rcsi2_c_phy_setting_v4h(struct rcar_csi2 *priv, int msps)
{
	const struct rcsi2_cphy_setting *conf;

	for (conf = cphy_setting_table_r8a779g0; conf->msps != 0; conf++) {
		if (conf->msps > msps)
			break;
	}

	if (!conf->msps) {
		dev_err(priv->dev, "Unsupported PHY speed for msps setting (%u Msps)", msps);
		return -ERANGE;
	}

	/* C-PHY specific */
	rcsi2_write16(priv, V4H_CORE_DIG_RW_COMMON_REG(7), 0x0155);
	rcsi2_write16(priv, V4H_PPI_STARTUP_RW_COMMON_DPHY_REG(7), 0x0068);
	rcsi2_write16(priv, V4H_PPI_STARTUP_RW_COMMON_DPHY_REG(8), 0x0010);

	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_0_RW_LP_0_REG, 0x463c);
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_1_RW_LP_0_REG, 0x463c);
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_2_RW_LP_0_REG, 0x463c);

	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_0_RW_HS_RX_REG(0), 0x00d5);
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_1_RW_HS_RX_REG(0), 0x00d5);
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_2_RW_HS_RX_REG(0), 0x00d5);

	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_0_RW_HS_RX_REG(1), 0x0013);
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_1_RW_HS_RX_REG(1), 0x0013);
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_2_RW_HS_RX_REG(1), 0x0013);

	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_0_RW_HS_RX_REG(5), 0x0013);
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_1_RW_HS_RX_REG(5), 0x0013);
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_2_RW_HS_RX_REG(5), 0x0013);

	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_0_RW_HS_RX_REG(6), 0x000a);
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_1_RW_HS_RX_REG(6), 0x000a);
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_2_RW_HS_RX_REG(6), 0x000a);

	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_0_RW_HS_RX_REG(2), conf->rx2);
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_1_RW_HS_RX_REG(2), conf->rx2);
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_2_RW_HS_RX_REG(2), conf->rx2);

	rcsi2_write16(priv, V4H_CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_REG(2), 0x0001);
	rcsi2_write16(priv, V4H_CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_REG(2), 0);
	rcsi2_write16(priv, V4H_CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_REG(2), 0x0001);
	rcsi2_write16(priv, V4H_CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_REG(2), 0x0001);
	rcsi2_write16(priv, V4H_CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_REG(2), 0);

	rcsi2_write16(priv, V4H_CORE_DIG_RW_TRIO0_REG(0), conf->trio0);
	rcsi2_write16(priv, V4H_CORE_DIG_RW_TRIO1_REG(0), conf->trio0);
	rcsi2_write16(priv, V4H_CORE_DIG_RW_TRIO2_REG(0), conf->trio0);

	rcsi2_write16(priv, V4H_CORE_DIG_RW_TRIO0_REG(2), conf->trio2);
	rcsi2_write16(priv, V4H_CORE_DIG_RW_TRIO1_REG(2), conf->trio2);
	rcsi2_write16(priv, V4H_CORE_DIG_RW_TRIO2_REG(2), conf->trio2);

	rcsi2_write16(priv, V4H_CORE_DIG_RW_TRIO0_REG(1), conf->trio1);
	rcsi2_write16(priv, V4H_CORE_DIG_RW_TRIO1_REG(1), conf->trio1);
	rcsi2_write16(priv, V4H_CORE_DIG_RW_TRIO2_REG(1), conf->trio1);

	/* Configure data line order. */
	rsci2_set_line_order(priv, priv->line_orders[0],
			     V4H_CORE_DIG_CLANE_0_RW_CFG_0_REG,
			     V4H_CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_REG(9));
	rsci2_set_line_order(priv, priv->line_orders[1],
			     V4H_CORE_DIG_CLANE_1_RW_CFG_0_REG,
			     V4H_CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_REG(9));
	rsci2_set_line_order(priv, priv->line_orders[2],
			     V4H_CORE_DIG_CLANE_2_RW_CFG_0_REG,
			     V4H_CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_REG(9));

	/* TODO: This registers is not documented. */
	rcsi2_write16(priv, V4H_CORE_DIG_CLANE_1_RW_HS_TX_6_REG, 0x5000);

	/* Leave Shutdown mode */
	rcsi2_write(priv, V4H_DPHY_RSTZ_REG, BIT(0));
	rcsi2_write(priv, V4H_PHY_SHUTDOWNZ_REG, BIT(0));

	/* Wait for calibration */
	if (rcsi2_wait_phy_start_v4h(priv, V4H_ST_PHYST_ST_PHY_READY)) {
		dev_err(priv->dev, "PHY calibration failed\n");
		return -ETIMEDOUT;
	}

	/* C-PHY setting - analog programing*/
	rcsi2_write16(priv, V4H_CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_REG(9), conf->lane29);
	rcsi2_write16(priv, V4H_CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_REG(7), conf->lane27);

	return 0;
}

static int rcsi2_start_receiver_v4h(struct rcar_csi2 *priv,
				    struct v4l2_subdev_state *state)
{
	const struct rcar_csi2_format *format;
	const struct v4l2_mbus_framefmt *fmt;
	unsigned int lanes;
	int msps;
	int ret;

	/* Use the format on the sink pad to compute the receiver config. */
	fmt = v4l2_subdev_state_get_format(state, RCAR_CSI2_SINK);
	format = rcsi2_code_to_fmt(fmt->code);
	if (!format)
		return -EINVAL;

	ret = rcsi2_get_active_lanes(priv, &lanes);
	if (ret)
		return ret;

	msps = rcsi2_calc_mbps(priv, format->bpp, lanes);
	if (msps < 0)
		return msps;

	/* Reset LINK and PHY*/
	rcsi2_write(priv, V4H_CSI2_RESETN_REG, 0);
	rcsi2_write(priv, V4H_DPHY_RSTZ_REG, 0);
	rcsi2_write(priv, V4H_PHY_SHUTDOWNZ_REG, 0);

	/* PHY static setting */
	rcsi2_write(priv, V4H_PHY_EN_REG, V4H_PHY_EN_ENABLE_CLK);
	rcsi2_write(priv, V4H_FLDC_REG, 0);
	rcsi2_write(priv, V4H_FLDD_REG, 0);
	rcsi2_write(priv, V4H_IDIC_REG, 0);
	rcsi2_write(priv, V4H_PHY_MODE_REG, V4H_PHY_MODE_CPHY);
	rcsi2_write(priv, V4H_N_LANES_REG, lanes - 1);

	/* Reset CSI2 */
	rcsi2_write(priv, V4H_CSI2_RESETN_REG, BIT(0));

	/* Registers static setting through APB */
	/* Common setting */
	rcsi2_write16(priv, V4H_CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_REG(0), 0x1bfd);
	rcsi2_write16(priv, V4H_PPI_STARTUP_RW_COMMON_STARTUP_1_1_REG, 0x0233);
	rcsi2_write16(priv, V4H_PPI_STARTUP_RW_COMMON_DPHY_REG(6), 0x0027);
	rcsi2_write16(priv, V4H_PPI_CALIBCTRL_RW_COMMON_BG_0_REG, 0x01f4);
	rcsi2_write16(priv, V4H_PPI_RW_TERMCAL_CFG_0_REG, 0x0013);
	rcsi2_write16(priv, V4H_PPI_RW_OFFSETCAL_CFG_0_REG, 0x0003);
	rcsi2_write16(priv, V4H_PPI_RW_LPDCOCAL_TIMEBASE_REG, 0x004f);
	rcsi2_write16(priv, V4H_PPI_RW_LPDCOCAL_NREF_REG, 0x0320);
	rcsi2_write16(priv, V4H_PPI_RW_LPDCOCAL_NREF_RANGE_REG, 0x000f);
	rcsi2_write16(priv, V4H_PPI_RW_LPDCOCAL_TWAIT_CONFIG_REG, 0xfe18);
	rcsi2_write16(priv, V4H_PPI_RW_LPDCOCAL_VT_CONFIG_REG, 0x0c3c);
	rcsi2_write16(priv, V4H_PPI_RW_LPDCOCAL_COARSE_CFG_REG, 0x0105);
	rcsi2_write16(priv, V4H_CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_REG(6), 0x1000);
	rcsi2_write16(priv, V4H_PPI_RW_COMMON_CFG_REG, 0x0003);

	/* C-PHY settings */
	ret = rcsi2_c_phy_setting_v4h(priv, msps);
	if (ret)
		return ret;

	rcsi2_wait_phy_start_v4h(priv, V4H_ST_PHYST_ST_STOPSTATE_0 |
				 V4H_ST_PHYST_ST_STOPSTATE_1 |
				 V4H_ST_PHYST_ST_STOPSTATE_2);

	return 0;
}

static int rcsi2_d_phy_setting_v4m(struct rcar_csi2 *priv, int data_rate)
{
	unsigned int timeout;
	int ret;

	static const struct phtw_value step1[] = {
		{ .data = 0x00, .code = 0x00 },
		{ .data = 0x00, .code = 0x1e },
	};

	/* Shutdown and reset PHY. */
	rcsi2_write(priv, V4H_DPHY_RSTZ_REG, BIT(0));
	rcsi2_write(priv, V4H_PHY_SHUTDOWNZ_REG, BIT(0));

	/* Start internal calibration (POR). */
	ret = rcsi2_phtw_write_array(priv, step1, ARRAY_SIZE(step1));
	if (ret)
		return ret;

	/* Wait for POR to complete. */
	for (timeout = 10; timeout > 0; timeout--) {
		if ((rcsi2_read(priv, V4M_PHTR_REG) & 0xf0000) == 0x70000)
			break;
		usleep_range(1000, 2000);
	}

	if (!timeout) {
		dev_err(priv->dev, "D-PHY calibration failed\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int rcsi2_set_osc_freq(struct rcar_csi2 *priv, unsigned int mbps)
{
	const struct rcsi2_mbps_info *info;
	struct phtw_value steps[] = {
		{ .data = 0x00, .code = 0x00 },
		{ .code = 0xe2 }, /* Data filled in below. */
		{ .code = 0xe3 }, /* Data filled in below. */
		{ .data = 0x01, .code = 0xe4 },
	};

	info = rcsi2_mbps_to_info(priv, priv->info->hsfreqrange, mbps);
	if (!info)
		return -ERANGE;

	/* Fill in data for command. */
	steps[1].data = (info->osc_freq & 0x00ff) >> 0;
	steps[2].data = (info->osc_freq & 0x0f00) >> 8;

	return rcsi2_phtw_write_array(priv, steps, ARRAY_SIZE(steps));
}

static int rcsi2_init_common_v4m(struct rcar_csi2 *priv, unsigned int mbps)
{
	int ret;

	static const struct phtw_value step1[] = {
		{ .data = 0x00, .code = 0x00 },
		{ .data = 0x3c, .code = 0x08 },
	};

	static const struct phtw_value step2[] = {
		{ .data = 0x00, .code = 0x00 },
		{ .data = 0x80, .code = 0xe0 },
		{ .data = 0x31, .code = 0xe1 },
		{ .data = 0x06, .code = 0x00 },
		{ .data = 0x11, .code = 0x11 },
		{ .data = 0x08, .code = 0x00 },
		{ .data = 0x11, .code = 0x11 },
		{ .data = 0x0a, .code = 0x00 },
		{ .data = 0x11, .code = 0x11 },
		{ .data = 0x0c, .code = 0x00 },
		{ .data = 0x11, .code = 0x11 },
		{ .data = 0x01, .code = 0x00 },
		{ .data = 0x31, .code = 0xaa },
		{ .data = 0x05, .code = 0x00 },
		{ .data = 0x05, .code = 0x09 },
		{ .data = 0x07, .code = 0x00 },
		{ .data = 0x05, .code = 0x09 },
		{ .data = 0x09, .code = 0x00 },
		{ .data = 0x05, .code = 0x09 },
		{ .data = 0x0b, .code = 0x00 },
		{ .data = 0x05, .code = 0x09 },
	};

	static const struct phtw_value step3[] = {
		{ .data = 0x01, .code = 0x00 },
		{ .data = 0x06, .code = 0xab },
	};

	if (priv->info->hsfreqrange) {
		ret = rcsi2_set_phypll(priv, mbps);
		if (ret)
			return ret;

		ret = rcsi2_set_osc_freq(priv, mbps);
		if (ret)
			return ret;
	}

	if (mbps <= 1500) {
		ret = rcsi2_phtw_write_array(priv, step1, ARRAY_SIZE(step1));
		if (ret)
			return ret;
	}

	if (priv->info->csi0clkfreqrange)
		rcsi2_write(priv, V4M_CSI0CLKFCPR_REG,
			    CSI0CLKFREQRANGE(priv->info->csi0clkfreqrange));

	rcsi2_write(priv, V4H_PHY_EN_REG, V4H_PHY_EN_ENABLE_CLK |
		    V4H_PHY_EN_ENABLE_0 | V4H_PHY_EN_ENABLE_1 |
		    V4H_PHY_EN_ENABLE_2 | V4H_PHY_EN_ENABLE_3);

	if (mbps > 1500) {
		ret = rcsi2_phtw_write_array(priv, step2, ARRAY_SIZE(step2));
		if (ret)
			return ret;
	}

	return rcsi2_phtw_write_array(priv, step3, ARRAY_SIZE(step3));
}

static int rcsi2_start_receiver_v4m(struct rcar_csi2 *priv,
				    struct v4l2_subdev_state *state)
{
	const struct rcar_csi2_format *format;
	const struct v4l2_mbus_framefmt *fmt;
	unsigned int lanes;
	int mbps;
	int ret;

	/* Calculate parameters */
	fmt = v4l2_subdev_state_get_format(state, RCAR_CSI2_SINK);
	format = rcsi2_code_to_fmt(fmt->code);
	if (!format)
		return -EINVAL;

	ret = rcsi2_get_active_lanes(priv, &lanes);
	if (ret)
		return ret;

	mbps = rcsi2_calc_mbps(priv, format->bpp, lanes);
	if (mbps < 0)
		return mbps;

	/* Reset LINK and PHY */
	rcsi2_write(priv, V4H_CSI2_RESETN_REG, 0);
	rcsi2_write(priv, V4H_DPHY_RSTZ_REG, 0);
	rcsi2_write(priv, V4H_PHY_SHUTDOWNZ_REG, 0);
	rcsi2_write(priv, V4M_PHTC_REG, PHTC_TESTCLR);

	/* PHY static setting */
	rcsi2_write(priv, V4H_PHY_EN_REG, V4H_PHY_EN_ENABLE_CLK);
	rcsi2_write(priv, V4H_FLDC_REG, 0);
	rcsi2_write(priv, V4H_FLDD_REG, 0);
	rcsi2_write(priv, V4H_IDIC_REG, 0);
	rcsi2_write(priv, V4H_PHY_MODE_REG, V4H_PHY_MODE_DPHY);
	rcsi2_write(priv, V4H_N_LANES_REG, lanes - 1);

	rcsi2_write(priv, V4M_FRXM_REG,
		    V4M_FRXM_FORCERXMODE_0 | V4M_FRXM_FORCERXMODE_1 |
		    V4M_FRXM_FORCERXMODE_2 | V4M_FRXM_FORCERXMODE_3);
	rcsi2_write(priv, V4M_OVR1_REG,
		    V4M_OVR1_FORCERXMODE_0 | V4M_OVR1_FORCERXMODE_1 |
		    V4M_OVR1_FORCERXMODE_2 | V4M_OVR1_FORCERXMODE_3);

	/* Reset CSI2 */
	rcsi2_write(priv, V4M_PHTC_REG, 0);
	rcsi2_write(priv, V4H_CSI2_RESETN_REG, BIT(0));

	/* Common settings */
	ret = rcsi2_init_common_v4m(priv, mbps);
	if (ret)
		return ret;

	/* D-PHY settings */
	ret = rcsi2_d_phy_setting_v4m(priv, mbps);
	if (ret)
		return ret;

	rcsi2_wait_phy_start_v4h(priv, V4H_ST_PHYST_ST_STOPSTATE_0 |
				 V4H_ST_PHYST_ST_STOPSTATE_1 |
				 V4H_ST_PHYST_ST_STOPSTATE_2 |
				 V4H_ST_PHYST_ST_STOPSTATE_3);

	rcsi2_write(priv, V4M_FRXM_REG, 0);

	return 0;
}

static int rcsi2_start(struct rcar_csi2 *priv, struct v4l2_subdev_state *state)
{
	int ret;

	ret = rcsi2_exit_standby(priv);
	if (ret < 0)
		return ret;

	ret = priv->info->start_receiver(priv, state);
	if (ret) {
		rcsi2_enter_standby(priv);
		return ret;
	}

	ret = v4l2_subdev_call(priv->remote, video, s_stream, 1);
	if (ret) {
		rcsi2_enter_standby(priv);
		return ret;
	}

	return 0;
}

static void rcsi2_stop(struct rcar_csi2 *priv)
{
	rcsi2_enter_standby(priv);
	v4l2_subdev_call(priv->remote, video, s_stream, 0);
}

static int rcsi2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rcar_csi2 *priv = sd_to_csi2(sd);
	struct v4l2_subdev_state *state;
	int ret = 0;

	if (!priv->remote)
		return -ENODEV;

	state = v4l2_subdev_lock_and_get_active_state(&priv->subdev);

	if (enable && priv->stream_count == 0) {
		ret = rcsi2_start(priv, state);
		if (ret)
			goto out;
	} else if (!enable && priv->stream_count == 1) {
		rcsi2_stop(priv);
	}

	priv->stream_count += enable ? 1 : -1;
out:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int rcsi2_set_pad_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_format *format)
{
	struct rcar_csi2 *priv = sd_to_csi2(sd);
	unsigned int num_pads = rcsi2_num_pads(priv);

	if (format->pad > RCAR_CSI2_SINK)
		return v4l2_subdev_get_fmt(sd, state, format);

	if (!rcsi2_code_to_fmt(format->format.code))
		format->format.code = rcar_csi2_formats[0].code;

	*v4l2_subdev_state_get_format(state, format->pad) = format->format;

	/* Propagate the format to the source pads. */
	for (unsigned int i = RCAR_CSI2_SOURCE_VC0; i < num_pads; i++)
		*v4l2_subdev_state_get_format(state, i) = format->format;

	return 0;
}

static const struct v4l2_subdev_video_ops rcar_csi2_video_ops = {
	.s_stream = rcsi2_s_stream,
};

static const struct v4l2_subdev_pad_ops rcar_csi2_pad_ops = {
	.set_fmt = rcsi2_set_pad_format,
	.get_fmt = v4l2_subdev_get_fmt,
};

static const struct v4l2_subdev_ops rcar_csi2_subdev_ops = {
	.video	= &rcar_csi2_video_ops,
	.pad	= &rcar_csi2_pad_ops,
};

static int rcsi2_init_state(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state)
{
	struct rcar_csi2 *priv = sd_to_csi2(sd);
	unsigned int num_pads = rcsi2_num_pads(priv);

	static const struct v4l2_mbus_framefmt rcar_csi2_default_fmt = {
		.width		= 1920,
		.height		= 1080,
		.code		= MEDIA_BUS_FMT_RGB888_1X24,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.field		= V4L2_FIELD_NONE,
		.ycbcr_enc	= V4L2_YCBCR_ENC_DEFAULT,
		.quantization	= V4L2_QUANTIZATION_DEFAULT,
		.xfer_func	= V4L2_XFER_FUNC_DEFAULT,
	};

	for (unsigned int i = RCAR_CSI2_SINK; i < num_pads; i++)
		*v4l2_subdev_state_get_format(state, i) = rcar_csi2_default_fmt;

	return 0;
}

static const struct v4l2_subdev_internal_ops rcar_csi2_internal_ops = {
	.init_state = rcsi2_init_state,
};

static irqreturn_t rcsi2_irq(int irq, void *data)
{
	struct rcar_csi2 *priv = data;
	u32 status, err_status;

	status = rcsi2_read(priv, INTSTATE_REG);
	err_status = rcsi2_read(priv, INTERRSTATE_REG);

	if (!status)
		return IRQ_HANDLED;

	rcsi2_write(priv, INTSTATE_REG, status);

	if (!err_status)
		return IRQ_HANDLED;

	rcsi2_write(priv, INTERRSTATE_REG, err_status);

	dev_info(priv->dev, "Transfer error, restarting CSI-2 receiver\n");

	return IRQ_WAKE_THREAD;
}

static irqreturn_t rcsi2_irq_thread(int irq, void *data)
{
	struct v4l2_subdev_state *state;
	struct rcar_csi2 *priv = data;

	state = v4l2_subdev_lock_and_get_active_state(&priv->subdev);

	rcsi2_stop(priv);
	usleep_range(1000, 2000);
	if (rcsi2_start(priv, state))
		dev_warn(priv->dev, "Failed to restart CSI-2 receiver\n");

	v4l2_subdev_unlock_state(state);

	return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * Async handling and registration of subdevices and links.
 */

static int rcsi2_notify_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *subdev,
			      struct v4l2_async_connection *asc)
{
	struct rcar_csi2 *priv = notifier_to_csi2(notifier);
	int pad;

	pad = media_entity_get_fwnode_pad(&subdev->entity, asc->match.fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (pad < 0) {
		dev_err(priv->dev, "Failed to find pad for %s\n", subdev->name);
		return pad;
	}

	priv->remote = subdev;
	priv->remote_pad = pad;

	dev_dbg(priv->dev, "Bound %s pad: %d\n", subdev->name, pad);

	return media_create_pad_link(&subdev->entity, pad,
				     &priv->subdev.entity, 0,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static void rcsi2_notify_unbind(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_connection *asc)
{
	struct rcar_csi2 *priv = notifier_to_csi2(notifier);

	priv->remote = NULL;

	dev_dbg(priv->dev, "Unbind %s\n", subdev->name);
}

static const struct v4l2_async_notifier_operations rcar_csi2_notify_ops = {
	.bound = rcsi2_notify_bound,
	.unbind = rcsi2_notify_unbind,
};

static int rcsi2_parse_v4l2(struct rcar_csi2 *priv,
			    struct v4l2_fwnode_endpoint *vep)
{
	unsigned int i;

	/* Only port 0 endpoint 0 is valid. */
	if (vep->base.port || vep->base.id)
		return -ENOTCONN;

	priv->lanes = vep->bus.mipi_csi2.num_data_lanes;

	switch (vep->bus_type) {
	case V4L2_MBUS_CSI2_DPHY:
		if (!priv->info->support_dphy) {
			dev_err(priv->dev, "D-PHY not supported\n");
			return -EINVAL;
		}

		if (priv->lanes != 1 && priv->lanes != 2 && priv->lanes != 4) {
			dev_err(priv->dev,
				"Unsupported number of data-lanes for D-PHY: %u\n",
				priv->lanes);
			return -EINVAL;
		}

		priv->cphy = false;
		break;
	case V4L2_MBUS_CSI2_CPHY:
		if (!priv->info->support_cphy) {
			dev_err(priv->dev, "C-PHY not supported\n");
			return -EINVAL;
		}

		if (priv->lanes != 3) {
			dev_err(priv->dev,
				"Unsupported number of data-lanes for C-PHY: %u\n",
				priv->lanes);
			return -EINVAL;
		}

		priv->cphy = true;
		break;
	default:
		dev_err(priv->dev, "Unsupported bus: %u\n", vep->bus_type);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(priv->lane_swap); i++) {
		priv->lane_swap[i] = i < priv->lanes ?
			vep->bus.mipi_csi2.data_lanes[i] : i;

		/* Check for valid lane number. */
		if (priv->lane_swap[i] < 1 || priv->lane_swap[i] > 4) {
			dev_err(priv->dev, "data-lanes must be in 1-4 range\n");
			return -EINVAL;
		}
	}

	for (i = 0; i < ARRAY_SIZE(priv->line_orders); i++)
		priv->line_orders[i] = vep->bus.mipi_csi2.line_orders[i];

	return 0;
}

static int rcsi2_parse_dt(struct rcar_csi2 *priv)
{
	struct v4l2_async_connection *asc;
	struct fwnode_handle *fwnode;
	struct fwnode_handle *ep;
	struct v4l2_fwnode_endpoint v4l2_ep = {
		.bus_type = V4L2_MBUS_UNKNOWN,
	};
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(priv->dev), 0, 0, 0);
	if (!ep) {
		dev_err(priv->dev, "Not connected to subdevice\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(ep, &v4l2_ep);
	if (ret) {
		dev_err(priv->dev, "Could not parse v4l2 endpoint\n");
		fwnode_handle_put(ep);
		return -EINVAL;
	}

	ret = rcsi2_parse_v4l2(priv, &v4l2_ep);
	if (ret) {
		fwnode_handle_put(ep);
		return ret;
	}

	fwnode = fwnode_graph_get_remote_endpoint(ep);
	fwnode_handle_put(ep);

	dev_dbg(priv->dev, "Found '%pOF'\n", to_of_node(fwnode));

	v4l2_async_subdev_nf_init(&priv->notifier, &priv->subdev);
	priv->notifier.ops = &rcar_csi2_notify_ops;

	asc = v4l2_async_nf_add_fwnode(&priv->notifier, fwnode,
				       struct v4l2_async_connection);
	fwnode_handle_put(fwnode);
	if (IS_ERR(asc))
		return PTR_ERR(asc);

	ret = v4l2_async_nf_register(&priv->notifier);
	if (ret)
		v4l2_async_nf_cleanup(&priv->notifier);

	return ret;
}

/* -----------------------------------------------------------------------------
 * PHTW initialization sequences.
 *
 * NOTE: Magic values are from the datasheet and lack documentation.
 */

static int rcsi2_phtw_write_mbps(struct rcar_csi2 *priv, unsigned int mbps,
				 const struct rcsi2_mbps_info *values, u8 code)
{
	const struct rcsi2_mbps_info *info;

	info = rcsi2_mbps_to_info(priv, values, mbps);
	if (!info)
		return -ERANGE;

	return rcsi2_phtw_write(priv, info->reg, code);
}

static int __rcsi2_init_phtw_h3_v3h_m3n(struct rcar_csi2 *priv,
					unsigned int mbps)
{
	static const struct phtw_value step1[] = {
		{ .data = 0xcc, .code = 0xe2 },
		{ .data = 0x01, .code = 0xe3 },
		{ .data = 0x11, .code = 0xe4 },
		{ .data = 0x01, .code = 0xe5 },
		{ .data = 0x10, .code = 0x04 },
	};

	static const struct phtw_value step2[] = {
		{ .data = 0x38, .code = 0x08 },
		{ .data = 0x01, .code = 0x00 },
		{ .data = 0x4b, .code = 0xac },
		{ .data = 0x03, .code = 0x00 },
		{ .data = 0x80, .code = 0x07 },
	};

	int ret;

	ret = rcsi2_phtw_write_array(priv, step1, ARRAY_SIZE(step1));
	if (ret)
		return ret;

	if (mbps != 0 && mbps <= 250) {
		ret = rcsi2_phtw_write(priv, 0x39, 0x05);
		if (ret)
			return ret;

		ret = rcsi2_phtw_write_mbps(priv, mbps, phtw_mbps_h3_v3h_m3n,
					    0xf1);
		if (ret)
			return ret;
	}

	return rcsi2_phtw_write_array(priv, step2, ARRAY_SIZE(step2));
}

static int rcsi2_init_phtw_h3_v3h_m3n(struct rcar_csi2 *priv, unsigned int mbps)
{
	return __rcsi2_init_phtw_h3_v3h_m3n(priv, mbps);
}

static int rcsi2_init_phtw_h3es2(struct rcar_csi2 *priv, unsigned int mbps)
{
	return __rcsi2_init_phtw_h3_v3h_m3n(priv, 0);
}

static int rcsi2_init_phtw_v3m_e3(struct rcar_csi2 *priv, unsigned int mbps)
{
	return rcsi2_phtw_write_mbps(priv, mbps, phtw_mbps_v3m_e3, 0x44);
}

static int rcsi2_phy_post_init_v3m_e3(struct rcar_csi2 *priv)
{
	static const struct phtw_value step1[] = {
		{ .data = 0xee, .code = 0x34 },
		{ .data = 0xee, .code = 0x44 },
		{ .data = 0xee, .code = 0x54 },
		{ .data = 0xee, .code = 0x84 },
		{ .data = 0xee, .code = 0x94 },
	};

	return rcsi2_phtw_write_array(priv, step1, ARRAY_SIZE(step1));
}

static int rcsi2_init_phtw_v3u(struct rcar_csi2 *priv,
			       unsigned int mbps)
{
	/* In case of 1500Mbps or less */
	static const struct phtw_value step1[] = {
		{ .data = 0xcc, .code = 0xe2 },
	};

	static const struct phtw_value step2[] = {
		{ .data = 0x01, .code = 0xe3 },
		{ .data = 0x11, .code = 0xe4 },
		{ .data = 0x01, .code = 0xe5 },
	};

	/* In case of 1500Mbps or less */
	static const struct phtw_value step3[] = {
		{ .data = 0x38, .code = 0x08 },
	};

	static const struct phtw_value step4[] = {
		{ .data = 0x01, .code = 0x00 },
		{ .data = 0x4b, .code = 0xac },
		{ .data = 0x03, .code = 0x00 },
		{ .data = 0x80, .code = 0x07 },
	};

	int ret;

	if (mbps != 0 && mbps <= 1500)
		ret = rcsi2_phtw_write_array(priv, step1, ARRAY_SIZE(step1));
	else
		ret = rcsi2_phtw_write_mbps(priv, mbps, phtw_mbps_v3u, 0xe2);
	if (ret)
		return ret;

	ret = rcsi2_phtw_write_array(priv, step2, ARRAY_SIZE(step2));
	if (ret)
		return ret;

	if (mbps != 0 && mbps <= 1500) {
		ret = rcsi2_phtw_write_array(priv, step3, ARRAY_SIZE(step3));
		if (ret)
			return ret;
	}

	ret = rcsi2_phtw_write_array(priv, step4, ARRAY_SIZE(step4));
	if (ret)
		return ret;

	return ret;
}

/* -----------------------------------------------------------------------------
 * Platform Device Driver.
 */

static int rcsi2_link_setup(struct media_entity *entity,
			    const struct media_pad *local,
			    const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct rcar_csi2 *priv = sd_to_csi2(sd);
	struct video_device *vdev;
	int channel, vc;
	u32 id;

	if (!is_media_entity_v4l2_video_device(remote->entity)) {
		dev_err(priv->dev, "Remote is not a video device\n");
		return -EINVAL;
	}

	vdev = media_entity_to_video_device(remote->entity);

	if (of_property_read_u32(vdev->dev_parent->of_node, "renesas,id", &id)) {
		dev_err(priv->dev, "No renesas,id, can't configure routing\n");
		return -EINVAL;
	}

	channel = id % 4;

	if (flags & MEDIA_LNK_FL_ENABLED) {
		if (media_pad_remote_pad_first(local)) {
			dev_dbg(priv->dev,
				"Each VC can only be routed to one output channel\n");
			return -EINVAL;
		}

		vc = local->index - 1;

		dev_dbg(priv->dev, "Route VC%d to VIN%u on output channel %d\n",
			vc, id, channel);
	} else {
		vc = -1;
	}

	priv->channel_vc[channel] = vc;

	return 0;
}

static const struct media_entity_operations rcar_csi2_entity_ops = {
	.link_setup = rcsi2_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

static int rcsi2_probe_resources(struct rcar_csi2 *priv,
				 struct platform_device *pdev)
{
	int irq, ret;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(&pdev->dev, irq, rcsi2_irq,
					rcsi2_irq_thread, IRQF_SHARED,
					KBUILD_MODNAME, priv);
	if (ret)
		return ret;

	priv->rstc = devm_reset_control_get(&pdev->dev, NULL);

	return PTR_ERR_OR_ZERO(priv->rstc);
}

static const struct rcsi2_register_layout rcsi2_registers_gen3 = {
	.phtw = PHTW_REG,
	.phypll = PHYPLL_REG,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7795 = {
	.regs = &rcsi2_registers_gen3,
	.init_phtw = rcsi2_init_phtw_h3_v3h_m3n,
	.start_receiver = rcsi2_start_receiver_gen3,
	.enter_standby = rcsi2_enter_standby_gen3,
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.csi0clkfreqrange = 0x20,
	.num_channels = 4,
	.clear_ulps = true,
	.support_dphy = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7795es2 = {
	.regs = &rcsi2_registers_gen3,
	.init_phtw = rcsi2_init_phtw_h3es2,
	.start_receiver = rcsi2_start_receiver_gen3,
	.enter_standby = rcsi2_enter_standby_gen3,
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.csi0clkfreqrange = 0x20,
	.num_channels = 4,
	.clear_ulps = true,
	.support_dphy = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7796 = {
	.regs = &rcsi2_registers_gen3,
	.start_receiver = rcsi2_start_receiver_gen3,
	.enter_standby = rcsi2_enter_standby_gen3,
	.hsfreqrange = hsfreqrange_m3w,
	.num_channels = 4,
	.support_dphy = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77961 = {
	.regs = &rcsi2_registers_gen3,
	.start_receiver = rcsi2_start_receiver_gen3,
	.enter_standby = rcsi2_enter_standby_gen3,
	.hsfreqrange = hsfreqrange_m3w,
	.num_channels = 4,
	.support_dphy = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77965 = {
	.regs = &rcsi2_registers_gen3,
	.init_phtw = rcsi2_init_phtw_h3_v3h_m3n,
	.start_receiver = rcsi2_start_receiver_gen3,
	.enter_standby = rcsi2_enter_standby_gen3,
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.csi0clkfreqrange = 0x20,
	.num_channels = 4,
	.clear_ulps = true,
	.support_dphy = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77970 = {
	.regs = &rcsi2_registers_gen3,
	.init_phtw = rcsi2_init_phtw_v3m_e3,
	.phy_post_init = rcsi2_phy_post_init_v3m_e3,
	.start_receiver = rcsi2_start_receiver_gen3,
	.enter_standby = rcsi2_enter_standby_gen3,
	.num_channels = 4,
	.support_dphy = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77980 = {
	.regs = &rcsi2_registers_gen3,
	.init_phtw = rcsi2_init_phtw_h3_v3h_m3n,
	.start_receiver = rcsi2_start_receiver_gen3,
	.enter_standby = rcsi2_enter_standby_gen3,
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.csi0clkfreqrange = 0x20,
	.clear_ulps = true,
	.support_dphy = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77990 = {
	.regs = &rcsi2_registers_gen3,
	.init_phtw = rcsi2_init_phtw_v3m_e3,
	.phy_post_init = rcsi2_phy_post_init_v3m_e3,
	.start_receiver = rcsi2_start_receiver_gen3,
	.enter_standby = rcsi2_enter_standby_gen3,
	.num_channels = 2,
	.support_dphy = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a779a0 = {
	.regs = &rcsi2_registers_gen3,
	.init_phtw = rcsi2_init_phtw_v3u,
	.start_receiver = rcsi2_start_receiver_gen3,
	.enter_standby = rcsi2_enter_standby_gen3,
	.hsfreqrange = hsfreqrange_v3u,
	.csi0clkfreqrange = 0x20,
	.clear_ulps = true,
	.use_isp = true,
	.support_dphy = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a779g0 = {
	.regs = &rcsi2_registers_gen3,
	.start_receiver = rcsi2_start_receiver_v4h,
	.use_isp = true,
	.support_cphy = true,
};

static const struct rcsi2_register_layout rcsi2_registers_v4m = {
	.phtw = V4M_PHTW_REG,
	.phypll = V4M_PHYPLL_REG,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a779h0 = {
	.regs = &rcsi2_registers_v4m,
	.start_receiver = rcsi2_start_receiver_v4m,
	.hsfreqrange = hsfreqrange_v4m,
	.csi0clkfreqrange = 0x0c,
	.use_isp = true,
	.support_dphy = true,
};

static const struct of_device_id rcar_csi2_of_table[] = {
	{
		.compatible = "renesas,r8a774a1-csi2",
		.data = &rcar_csi2_info_r8a7796,
	},
	{
		.compatible = "renesas,r8a774b1-csi2",
		.data = &rcar_csi2_info_r8a77965,
	},
	{
		.compatible = "renesas,r8a774c0-csi2",
		.data = &rcar_csi2_info_r8a77990,
	},
	{
		.compatible = "renesas,r8a774e1-csi2",
		.data = &rcar_csi2_info_r8a7795,
	},
	{
		.compatible = "renesas,r8a7795-csi2",
		.data = &rcar_csi2_info_r8a7795,
	},
	{
		.compatible = "renesas,r8a7796-csi2",
		.data = &rcar_csi2_info_r8a7796,
	},
	{
		.compatible = "renesas,r8a77961-csi2",
		.data = &rcar_csi2_info_r8a77961,
	},
	{
		.compatible = "renesas,r8a77965-csi2",
		.data = &rcar_csi2_info_r8a77965,
	},
	{
		.compatible = "renesas,r8a77970-csi2",
		.data = &rcar_csi2_info_r8a77970,
	},
	{
		.compatible = "renesas,r8a77980-csi2",
		.data = &rcar_csi2_info_r8a77980,
	},
	{
		.compatible = "renesas,r8a77990-csi2",
		.data = &rcar_csi2_info_r8a77990,
	},
	{
		.compatible = "renesas,r8a779a0-csi2",
		.data = &rcar_csi2_info_r8a779a0,
	},
	{
		.compatible = "renesas,r8a779g0-csi2",
		.data = &rcar_csi2_info_r8a779g0,
	},
	{
		.compatible = "renesas,r8a779h0-csi2",
		.data = &rcar_csi2_info_r8a779h0,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rcar_csi2_of_table);

static const struct soc_device_attribute r8a7795[] = {
	{
		.soc_id = "r8a7795", .revision = "ES2.*",
		.data = &rcar_csi2_info_r8a7795es2,
	},
	{ /* sentinel */ }
};

static int rcsi2_probe(struct platform_device *pdev)
{
	const struct soc_device_attribute *attr;
	struct rcar_csi2 *priv;
	unsigned int i, num_pads;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->info = of_device_get_match_data(&pdev->dev);

	/*
	 * The different ES versions of r8a7795 (H3) behave differently but
	 * share the same compatible string.
	 */
	attr = soc_device_match(r8a7795);
	if (attr)
		priv->info = attr->data;

	priv->dev = &pdev->dev;

	priv->stream_count = 0;

	ret = rcsi2_probe_resources(priv, pdev);
	if (ret) {
		dev_err(priv->dev, "Failed to get resources\n");
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	ret = rcsi2_parse_dt(priv);
	if (ret)
		return ret;

	priv->subdev.owner = THIS_MODULE;
	priv->subdev.dev = &pdev->dev;
	priv->subdev.internal_ops = &rcar_csi2_internal_ops;
	v4l2_subdev_init(&priv->subdev, &rcar_csi2_subdev_ops);
	v4l2_set_subdevdata(&priv->subdev, &pdev->dev);
	snprintf(priv->subdev.name, sizeof(priv->subdev.name), "%s %s",
		 KBUILD_MODNAME, dev_name(&pdev->dev));
	priv->subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	priv->subdev.entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	priv->subdev.entity.ops = &rcar_csi2_entity_ops;

	num_pads = rcsi2_num_pads(priv);

	priv->pads[RCAR_CSI2_SINK].flags = MEDIA_PAD_FL_SINK;
	for (i = RCAR_CSI2_SOURCE_VC0; i < num_pads; i++)
		priv->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&priv->subdev.entity, num_pads,
				     priv->pads);
	if (ret)
		goto error_async;

	for (i = 0; i < ARRAY_SIZE(priv->channel_vc); i++)
		priv->channel_vc[i] = -1;

	pm_runtime_enable(&pdev->dev);

	ret = v4l2_subdev_init_finalize(&priv->subdev);
	if (ret)
		goto error_pm_runtime;

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0)
		goto error_subdev;

	dev_info(priv->dev, "%d lanes found\n", priv->lanes);

	return 0;

error_subdev:
	v4l2_subdev_cleanup(&priv->subdev);
error_pm_runtime:
	pm_runtime_disable(&pdev->dev);
error_async:
	v4l2_async_nf_unregister(&priv->notifier);
	v4l2_async_nf_cleanup(&priv->notifier);

	return ret;
}

static void rcsi2_remove(struct platform_device *pdev)
{
	struct rcar_csi2 *priv = platform_get_drvdata(pdev);

	v4l2_async_nf_unregister(&priv->notifier);
	v4l2_async_nf_cleanup(&priv->notifier);
	v4l2_async_unregister_subdev(&priv->subdev);
	v4l2_subdev_cleanup(&priv->subdev);

	pm_runtime_disable(&pdev->dev);
}

static struct platform_driver rcar_csi2_pdrv = {
	.remove = rcsi2_remove,
	.probe	= rcsi2_probe,
	.driver	= {
		.name	= "rcar-csi2",
		.suppress_bind_attrs = true,
		.of_match_table	= rcar_csi2_of_table,
	},
};

module_platform_driver(rcar_csi2_pdrv);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("Renesas R-Car MIPI CSI-2 receiver driver");
MODULE_LICENSE("GPL");
