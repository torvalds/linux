// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Exynos5 SoC series USB DRD PHY driver
 *
 * Phy provider for USB 3.0 DRD controller on Exynos5 SoC series
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 * Author: Vivek Gautam <gautam.vivek@samsung.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/iopoll.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>

/* Exynos USB PHY registers */
#define EXYNOS5_FSEL_9MHZ6		0x0
#define EXYNOS5_FSEL_10MHZ		0x1
#define EXYNOS5_FSEL_12MHZ		0x2
#define EXYNOS5_FSEL_19MHZ2		0x3
#define EXYNOS5_FSEL_20MHZ		0x4
#define EXYNOS5_FSEL_24MHZ		0x5
#define EXYNOS5_FSEL_26MHZ		0x6
#define EXYNOS5_FSEL_50MHZ		0x7

/* Exynos5: USB 3.0 DRD PHY registers */
#define EXYNOS5_DRD_LINKSYSTEM			0x04
#define LINKSYSTEM_XHCI_VERSION_CONTROL		BIT(27)
#define LINKSYSTEM_FLADJ_MASK			(0x3f << 1)
#define LINKSYSTEM_FLADJ(_x)			((_x) << 1)

#define EXYNOS5_DRD_PHYUTMI			0x08
#define PHYUTMI_OTGDISABLE			BIT(6)
#define PHYUTMI_FORCESUSPEND			BIT(1)
#define PHYUTMI_FORCESLEEP			BIT(0)

#define EXYNOS5_DRD_PHYPIPE			0x0c

#define EXYNOS5_DRD_PHYCLKRST			0x10
#define PHYCLKRST_EN_UTMISUSPEND		BIT(31)
#define PHYCLKRST_SSC_REFCLKSEL_MASK		(0xff << 23)
#define PHYCLKRST_SSC_REFCLKSEL(_x)		((_x) << 23)
#define PHYCLKRST_SSC_RANGE_MASK		(0x03 << 21)
#define PHYCLKRST_SSC_RANGE(_x)			((_x) << 21)
#define PHYCLKRST_SSC_EN			BIT(20)
#define PHYCLKRST_REF_SSP_EN			BIT(19)
#define PHYCLKRST_REF_CLKDIV2			BIT(18)
#define PHYCLKRST_MPLL_MULTIPLIER_MASK		(0x7f << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_100MHZ_REF	(0x19 << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_50M_REF	(0x32 << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_24MHZ_REF	(0x68 << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_20MHZ_REF	(0x7d << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_19200KHZ_REF	(0x02 << 11)
#define PHYCLKRST_FSEL_PIPE_MASK		(0x7 << 8)
#define PHYCLKRST_FSEL_UTMI_MASK		(0x7 << 5)
#define PHYCLKRST_FSEL(_x)			((_x) << 5)
#define PHYCLKRST_FSEL_PAD_100MHZ		(0x27 << 5)
#define PHYCLKRST_FSEL_PAD_24MHZ		(0x2a << 5)
#define PHYCLKRST_FSEL_PAD_20MHZ		(0x31 << 5)
#define PHYCLKRST_FSEL_PAD_19_2MHZ		(0x38 << 5)
#define PHYCLKRST_RETENABLEN			BIT(4)
#define PHYCLKRST_REFCLKSEL_MASK		(0x03 << 2)
#define PHYCLKRST_REFCLKSEL_PAD_REFCLK		(0x2 << 2)
#define PHYCLKRST_REFCLKSEL_EXT_REFCLK		(0x3 << 2)
#define PHYCLKRST_PORTRESET			BIT(1)
#define PHYCLKRST_COMMONONN			BIT(0)

#define EXYNOS5_DRD_PHYREG0			0x14
#define PHYREG0_SSC_REF_CLK_SEL			BIT(21)
#define PHYREG0_SSC_RANGE			BIT(20)
#define PHYREG0_CR_WRITE			BIT(19)
#define PHYREG0_CR_READ				BIT(18)
#define PHYREG0_CR_DATA_IN(_x)			((_x) << 2)
#define PHYREG0_CR_CAP_DATA			BIT(1)
#define PHYREG0_CR_CAP_ADDR			BIT(0)

#define EXYNOS5_DRD_PHYREG1			0x18
#define PHYREG1_CR_DATA_OUT(_x)			((_x) << 1)
#define PHYREG1_CR_ACK				BIT(0)

#define EXYNOS5_DRD_PHYPARAM0			0x1c
#define PHYPARAM0_REF_USE_PAD			BIT(31)
#define PHYPARAM0_REF_LOSLEVEL_MASK		(0x1f << 26)
#define PHYPARAM0_REF_LOSLEVEL			(0x9 << 26)

#define EXYNOS5_DRD_PHYPARAM1			0x20
#define PHYPARAM1_PCS_TXDEEMPH_MASK		(0x1f << 0)
#define PHYPARAM1_PCS_TXDEEMPH			(0x1c)

#define EXYNOS5_DRD_PHYTERM			0x24

#define EXYNOS5_DRD_PHYTEST			0x28
#define PHYTEST_POWERDOWN_SSP			BIT(3)
#define PHYTEST_POWERDOWN_HSP			BIT(2)

#define EXYNOS5_DRD_PHYADP			0x2c

#define EXYNOS5_DRD_PHYUTMICLKSEL		0x30
#define PHYUTMICLKSEL_UTMI_CLKSEL		BIT(2)

#define EXYNOS5_DRD_PHYRESUME			0x34

#define EXYNOS5_DRD_LINKPORT			0x44

/* USB 3.0 DRD PHY SS Function Control Reg; accessed by CR_PORT */
#define EXYNOS5_DRD_PHYSS_LOSLEVEL_OVRD_IN		(0x15)
#define LOSLEVEL_OVRD_IN_LOS_BIAS_5420			(0x5 << 13)
#define LOSLEVEL_OVRD_IN_LOS_BIAS_DEFAULT		(0x0 << 13)
#define LOSLEVEL_OVRD_IN_EN				(0x1 << 10)
#define LOSLEVEL_OVRD_IN_LOS_LEVEL_DEFAULT		(0x9 << 0)

#define EXYNOS5_DRD_PHYSS_TX_VBOOSTLEVEL_OVRD_IN	(0x12)
#define TX_VBOOSTLEVEL_OVRD_IN_VBOOST_5420		(0x5 << 13)
#define TX_VBOOSTLEVEL_OVRD_IN_VBOOST_DEFAULT		(0x4 << 13)

#define EXYNOS5_DRD_PHYSS_LANE0_TX_DEBUG		(0x1010)
#define LANE0_TX_DEBUG_RXDET_MEAS_TIME_19M2_20M		(0x4 << 4)
#define LANE0_TX_DEBUG_RXDET_MEAS_TIME_24M		(0x8 << 4)
#define LANE0_TX_DEBUG_RXDET_MEAS_TIME_25M_26M		(0x8 << 4)
#define LANE0_TX_DEBUG_RXDET_MEAS_TIME_48M_50M_52M	(0x20 << 4)
#define LANE0_TX_DEBUG_RXDET_MEAS_TIME_62M5		(0x20 << 4)
#define LANE0_TX_DEBUG_RXDET_MEAS_TIME_96M_100M		(0x40 << 4)

/* Exynos850: USB DRD PHY registers */
#define EXYNOS850_DRD_LINKCTRL			0x04
#define LINKCTRL_FORCE_RXELECIDLE		BIT(18)
#define LINKCTRL_FORCE_PHYSTATUS		BIT(17)
#define LINKCTRL_FORCE_PIPE_EN			BIT(16)
#define LINKCTRL_FORCE_QACT			BIT(8)
#define LINKCTRL_BUS_FILTER_BYPASS(_x)		((_x) << 4)

#define EXYNOS850_DRD_LINKPORT			0x08
#define LINKPORT_HOST_NUM_U3			GENMASK(19, 16)
#define LINKPORT_HOST_NUM_U2			GENMASK(15, 12)

#define EXYNOS850_DRD_CLKRST			0x20
/*
 * On versions without SS ports (like E850), bit 3 is for the 2.0 phy (HS),
 * while on versions with (like gs101), bits 2 and 3 are for the 3.0 phy (SS)
 * and bits 12 & 13 for the 2.0 phy.
 */
#define CLKRST_PHY20_SW_POR			BIT(13)
#define CLKRST_PHY20_SW_POR_SEL			BIT(12)
#define CLKRST_LINK_PCLK_SEL			BIT(7)
#define CLKRST_PHY_SW_RST			BIT(3)
#define CLKRST_PHY_RESET_SEL			BIT(2)
#define CLKRST_PORT_RST				BIT(1)
#define CLKRST_LINK_SW_RST			BIT(0)

#define EXYNOS850_DRD_SSPPLLCTL			0x30
#define SSPPLLCTL_FSEL				GENMASK(2, 0)

#define EXYNOS850_DRD_UTMI			0x50
#define UTMI_FORCE_VBUSVALID			BIT(5)
#define UTMI_FORCE_BVALID			BIT(4)
#define UTMI_DP_PULLDOWN			BIT(3)
#define UTMI_DM_PULLDOWN			BIT(2)
#define UTMI_FORCE_SUSPEND			BIT(1)
#define UTMI_FORCE_SLEEP			BIT(0)

#define EXYNOS850_DRD_HSP			0x54
#define HSP_FSV_OUT_EN				BIT(24)
#define HSP_VBUSVLDEXTSEL			BIT(13)
#define HSP_VBUSVLDEXT				BIT(12)
#define HSP_EN_UTMISUSPEND			BIT(9)
#define HSP_COMMONONN				BIT(8)

#define EXYNOS850_DRD_HSPPARACON		0x58
#define HSPPARACON_TXVREF			GENMASK(31, 28)
#define HSPPARACON_TXRISE			GENMASK(25, 24)
#define HSPPARACON_TXRES			GENMASK(22, 21)
#define HSPPARACON_TXPREEMPPULSE		BIT(20)
#define HSPPARACON_TXPREEMPAMP			GENMASK(19, 18)
#define HSPPARACON_TXHSXV			GENMASK(17, 16)
#define HSPPARACON_TXFSLS			GENMASK(15, 12)
#define HSPPARACON_SQRX				GENMASK(10, 8)
#define HSPPARACON_OTG				GENMASK(6, 4)
#define HSPPARACON_COMPDIS			GENMASK(2, 0)

#define EXYNOS850_DRD_HSP_TEST			0x5c
#define HSP_TEST_SIDDQ				BIT(24)

/* Exynos9 - GS101 */
#define EXYNOS850_DRD_SECPMACTL			0x48
#define SECPMACTL_PMA_ROPLL_REF_CLK_SEL		GENMASK(13, 12)
#define SECPMACTL_PMA_LCPLL_REF_CLK_SEL		GENMASK(11, 10)
#define SECPMACTL_PMA_REF_FREQ_SEL		GENMASK(9, 8)
#define SECPMACTL_PMA_LOW_PWR			BIT(4)
#define SECPMACTL_PMA_TRSV_SW_RST		BIT(3)
#define SECPMACTL_PMA_CMN_SW_RST		BIT(2)
#define SECPMACTL_PMA_INIT_SW_RST		BIT(1)
#define SECPMACTL_PMA_APB_SW_RST		BIT(0)

/* PMA registers */
#define EXYNOS9_PMA_USBDP_CMN_REG0008		0x0020
#define CMN_REG0008_OVRD_AUX_EN			BIT(3)
#define CMN_REG0008_AUX_EN			BIT(2)

#define EXYNOS9_PMA_USBDP_CMN_REG00B8		0x02e0
#define CMN_REG00B8_LANE_MUX_SEL_DP		GENMASK(3, 0)

#define EXYNOS9_PMA_USBDP_CMN_REG01C0		0x0700
#define CMN_REG01C0_ANA_LCPLL_LOCK_DONE		BIT(7)
#define CMN_REG01C0_ANA_LCPLL_AFC_DONE		BIT(6)

/* these have similar register layout, for lanes 0 and 2 */
#define EXYNOS9_PMA_USBDP_TRSV_REG03C3			0x0f0c
#define EXYNOS9_PMA_USBDP_TRSV_REG07C3			0x1f0c
#define TRSV_REG03C3_LN0_MON_RX_CDR_AFC_DONE		BIT(3)
#define TRSV_REG03C3_LN0_MON_RX_CDR_CAL_DONE		BIT(2)
#define TRSV_REG03C3_LN0_MON_RX_CDR_FLD_PLL_MODE_DONE	BIT(1)
#define TRSV_REG03C3_LN0_MON_RX_CDR_LOCK_DONE		BIT(0)

/* TRSV_REG0413 and TRSV_REG0813 have similar register layout */
#define EXYNOS9_PMA_USBDP_TRSV_REG0413		0x104c
#define TRSV_REG0413_OVRD_LN1_TX_RXD_COMP_EN	BIT(7)
#define TRSV_REG0413_OVRD_LN1_TX_RXD_EN		BIT(5)

#define EXYNOS9_PMA_USBDP_TRSV_REG0813		0x204c
#define TRSV_REG0813_OVRD_LN3_TX_RXD_COMP_EN	BIT(7)
#define TRSV_REG0813_OVRD_LN3_TX_RXD_EN		BIT(5)

/* PCS registers */
#define EXYNOS9_PCS_NS_VEC_PS1_N1		0x010c
#define EXYNOS9_PCS_NS_VEC_PS2_N0		0x0110
#define EXYNOS9_PCS_NS_VEC_PS3_N0		0x0118
#define NS_VEC_NS_REQ				GENMASK(31, 24)
#define NS_VEC_ENABLE_TIMER			BIT(22)
#define NS_VEC_SEL_TIMEOUT			GENMASK(21, 20)
#define NS_VEC_INV_MASK				GENMASK(19, 16)
#define NS_VEC_COND_MASK			GENMASK(11, 8)
#define NS_VEC_EXP_COND				GENMASK(3, 0)

#define EXYNOS9_PCS_OUT_VEC_2			0x014c
#define EXYNOS9_PCS_OUT_VEC_3			0x0150
#define PCS_OUT_VEC_B9_DYNAMIC			BIT(19)
#define PCS_OUT_VEC_B9_SEL_OUT			BIT(18)
#define PCS_OUT_VEC_B8_DYNAMIC			BIT(17)
#define PCS_OUT_VEC_B8_SEL_OUT			BIT(16)
#define PCS_OUT_VEC_B7_DYNAMIC			BIT(15)
#define PCS_OUT_VEC_B7_SEL_OUT			BIT(14)
#define PCS_OUT_VEC_B6_DYNAMIC			BIT(13)
#define PCS_OUT_VEC_B6_SEL_OUT			BIT(12)
#define PCS_OUT_VEC_B5_DYNAMIC			BIT(11)
#define PCS_OUT_VEC_B5_SEL_OUT			BIT(10)
#define PCS_OUT_VEC_B4_DYNAMIC			BIT(9)
#define PCS_OUT_VEC_B4_SEL_OUT			BIT(8)
#define PCS_OUT_VEC_B3_DYNAMIC			BIT(7)
#define PCS_OUT_VEC_B3_SEL_OUT			BIT(6)
#define PCS_OUT_VEC_B2_DYNAMIC			BIT(5)
#define PCS_OUT_VEC_B2_SEL_OUT			BIT(4)
#define PCS_OUT_VEC_B1_DYNAMIC			BIT(3)
#define PCS_OUT_VEC_B1_SEL_OUT			BIT(2)
#define PCS_OUT_VEC_B0_DYNAMIC			BIT(1)
#define PCS_OUT_VEC_B0_SEL_OUT			BIT(0)

#define EXYNOS9_PCS_TIMEOUT_0			0x0170

#define EXYNOS9_PCS_TIMEOUT_3			0x017c

#define EXYNOS9_PCS_EBUF_PARAM			0x0304
#define EBUF_PARAM_SKP_REMOVE_TH_EMPTY_MODE	GENMASK(29, 24)

#define EXYNOS9_PCS_BACK_END_MODE_VEC		0x030c
#define BACK_END_MODE_VEC_FORCE_EBUF_EMPTY_MODE	BIT(1)
#define BACK_END_MODE_VEC_DISABLE_DATA_MASK	BIT(0)

#define EXYNOS9_PCS_RX_CONTROL			0x03f0
#define RX_CONTROL_EN_BLOCK_ALIGNER_TYPE_B	BIT(22)

#define EXYNOS9_PCS_RX_CONTROL_DEBUG		0x03f4
#define RX_CONTROL_DEBUG_EN_TS_CHECK		BIT(5)
#define RX_CONTROL_DEBUG_NUM_COM_FOUND		GENMASK(3, 0)

#define EXYNOS9_PCS_LOCAL_COEF			0x040c
#define LOCAL_COEF_PMA_CENTER_COEF		GENMASK(21, 16)
#define LOCAL_COEF_LF				GENMASK(13, 8)
#define LOCAL_COEF_FS				GENMASK(5, 0)

#define EXYNOS9_PCS_HS_TX_COEF_MAP_0		0x0410
#define HS_TX_COEF_MAP_0_SSTX_DEEMP		GENMASK(17, 12)
#define HS_TX_COEF_MAP_0_SSTX_LEVEL		GENMASK(11, 6)
#define HS_TX_COEF_MAP_0_SSTX_PRE_SHOOT		GENMASK(5, 0)


#define KHZ	1000
#define MHZ	(KHZ * KHZ)

#define PHY_TUNING_ENTRY_PHY(o, m, v) {	\
		.off = (o),		\
		.mask = (m),		\
		.val = (v),		\
		.region = PTR_PHY	\
	}

#define PHY_TUNING_ENTRY_PCS(o, m, v) {	\
		.off = (o),		\
		.mask = (m),		\
		.val = (v),		\
		.region = PTR_PCS	\
	}

#define PHY_TUNING_ENTRY_PMA(o, m, v) {	\
		.off = (o),		\
		.mask = (m),		\
		.val = (v),		\
		.region = PTR_PMA,	\
	}

#define PHY_TUNING_ENTRY_LAST { .region = PTR_INVALID }

#define for_each_phy_tune(tune) \
	for (; (tune)->region != PTR_INVALID; ++(tune))

struct exynos5_usbdrd_phy_tuning {
	u32 off;
	u32 mask;
	u32 val;
	char region;
#define PTR_INVALID	0
#define PTR_PHY		1
#define PTR_PCS		2
#define PTR_PMA		3
};

enum exynos5_usbdrd_phy_tuning_state {
	PTS_UTMI_POSTINIT,
	PTS_PIPE3_PREINIT,
	PTS_PIPE3_INIT,
	PTS_PIPE3_POSTINIT,
	PTS_PIPE3_POSTLOCK,
	PTS_MAX,
};

enum exynos5_usbdrd_phy_id {
	EXYNOS5_DRDPHY_UTMI,
	EXYNOS5_DRDPHY_PIPE3,
	EXYNOS5_DRDPHYS_NUM,
};

struct phy_usb_instance;
struct exynos5_usbdrd_phy;

struct exynos5_usbdrd_phy_config {
	u32 id;
	void (*phy_isol)(struct phy_usb_instance *inst, bool isolate);
	void (*phy_init)(struct exynos5_usbdrd_phy *phy_drd);
	unsigned int (*set_refclk)(struct phy_usb_instance *inst);
};

struct exynos5_usbdrd_phy_drvdata {
	const struct exynos5_usbdrd_phy_config *phy_cfg;
	const struct exynos5_usbdrd_phy_tuning **phy_tunes;
	const struct phy_ops *phy_ops;
	const char * const *clk_names;
	int n_clks;
	const char * const *core_clk_names;
	int n_core_clks;
	const char * const *regulator_names;
	int n_regulators;
	u32 pmu_offset_usbdrd0_phy;
	u32 pmu_offset_usbdrd0_phy_ss;
	u32 pmu_offset_usbdrd1_phy;
};

/**
 * struct exynos5_usbdrd_phy - driver data for USB 3.0 PHY
 * @dev: pointer to device instance of this platform device
 * @reg_phy: usb phy controller register memory base
 * @reg_pcs: usb phy physical coding sublayer register memory base
 * @reg_pma: usb phy physical media attachment register memory base
 * @clks: clocks for register access
 * @core_clks: core clocks for phy (ref, pipe3, utmi+, ITP, etc. as required)
 * @drv_data: pointer to SoC level driver data structure
 * @phys: array for 'EXYNOS5_DRDPHYS_NUM' number of PHY
 *	    instances each with its 'phy' and 'phy_cfg'.
 * @extrefclk: frequency select settings when using 'separate
 *	       reference clocks' for SS and HS operations
 * @regulators: regulators for phy
 */
struct exynos5_usbdrd_phy {
	struct device *dev;
	void __iomem *reg_phy;
	void __iomem *reg_pcs;
	void __iomem *reg_pma;
	struct clk_bulk_data *clks;
	struct clk_bulk_data *core_clks;
	const struct exynos5_usbdrd_phy_drvdata *drv_data;
	struct phy_usb_instance {
		struct phy *phy;
		u32 index;
		struct regmap *reg_pmu;
		u32 pmu_offset;
		const struct exynos5_usbdrd_phy_config *phy_cfg;
	} phys[EXYNOS5_DRDPHYS_NUM];
	u32 extrefclk;
	struct regulator_bulk_data *regulators;
};

static inline
struct exynos5_usbdrd_phy *to_usbdrd_phy(struct phy_usb_instance *inst)
{
	return container_of((inst), struct exynos5_usbdrd_phy,
			    phys[(inst)->index]);
}

/*
 * exynos5_rate_to_clk() converts the supplied clock rate to the value that
 * can be written to the phy register.
 */
static unsigned int exynos5_rate_to_clk(unsigned long rate, u32 *reg)
{
	/* EXYNOS5_FSEL_MASK */

	switch (rate) {
	case 9600 * KHZ:
		*reg = EXYNOS5_FSEL_9MHZ6;
		break;
	case 10 * MHZ:
		*reg = EXYNOS5_FSEL_10MHZ;
		break;
	case 12 * MHZ:
		*reg = EXYNOS5_FSEL_12MHZ;
		break;
	case 19200 * KHZ:
		*reg = EXYNOS5_FSEL_19MHZ2;
		break;
	case 20 * MHZ:
		*reg = EXYNOS5_FSEL_20MHZ;
		break;
	case 24 * MHZ:
		*reg = EXYNOS5_FSEL_24MHZ;
		break;
	case 26 * MHZ:
		*reg = EXYNOS5_FSEL_26MHZ;
		break;
	case 50 * MHZ:
		*reg = EXYNOS5_FSEL_50MHZ;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void exynos5_usbdrd_phy_isol(struct phy_usb_instance *inst,
				    bool isolate)
{
	unsigned int val;

	if (!inst->reg_pmu)
		return;

	val = isolate ? 0 : EXYNOS4_PHY_ENABLE;

	regmap_update_bits(inst->reg_pmu, inst->pmu_offset,
			   EXYNOS4_PHY_ENABLE, val);
}

/*
 * Sets the pipe3 phy's clk as EXTREFCLK (XXTI) which is internal clock
 * from clock core. Further sets multiplier values and spread spectrum
 * clock settings for SuperSpeed operations.
 */
static unsigned int
exynos5_usbdrd_pipe3_set_refclk(struct phy_usb_instance *inst)
{
	u32 reg;
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	/* restore any previous reference clock settings */
	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYCLKRST);

	/* Use EXTREFCLK as ref clock */
	reg &= ~PHYCLKRST_REFCLKSEL_MASK;
	reg |=	PHYCLKRST_REFCLKSEL_EXT_REFCLK;

	/* FSEL settings corresponding to reference clock */
	reg &= ~PHYCLKRST_FSEL_PIPE_MASK |
		PHYCLKRST_MPLL_MULTIPLIER_MASK |
		PHYCLKRST_SSC_REFCLKSEL_MASK;
	switch (phy_drd->extrefclk) {
	case EXYNOS5_FSEL_50MHZ:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_50M_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x00));
		break;
	case EXYNOS5_FSEL_24MHZ:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_24MHZ_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x88));
		break;
	case EXYNOS5_FSEL_20MHZ:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_20MHZ_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x00));
		break;
	case EXYNOS5_FSEL_19MHZ2:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_19200KHZ_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x88));
		break;
	default:
		dev_dbg(phy_drd->dev, "unsupported ref clk\n");
		break;
	}

	return reg;
}

/*
 * Sets the utmi phy's clk as EXTREFCLK (XXTI) which is internal clock
 * from clock core. Further sets the FSEL values for HighSpeed operations.
 */
static unsigned int
exynos5_usbdrd_utmi_set_refclk(struct phy_usb_instance *inst)
{
	u32 reg;
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	/* restore any previous reference clock settings */
	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYCLKRST);

	reg &= ~PHYCLKRST_REFCLKSEL_MASK;
	reg |=	PHYCLKRST_REFCLKSEL_EXT_REFCLK;

	reg &= ~PHYCLKRST_FSEL_UTMI_MASK |
		PHYCLKRST_MPLL_MULTIPLIER_MASK |
		PHYCLKRST_SSC_REFCLKSEL_MASK;
	reg |= PHYCLKRST_FSEL(phy_drd->extrefclk);

	return reg;
}

static void
exynos5_usbdrd_apply_phy_tunes(struct exynos5_usbdrd_phy *phy_drd,
			       enum exynos5_usbdrd_phy_tuning_state state)
{
	const struct exynos5_usbdrd_phy_tuning *tune;

	tune = phy_drd->drv_data->phy_tunes[state];
	if (!tune)
		return;

	for_each_phy_tune(tune) {
		void __iomem *reg_base;
		u32 reg = 0;

		switch (tune->region) {
		case PTR_PHY:
			reg_base = phy_drd->reg_phy;
			break;
		case PTR_PCS:
			reg_base = phy_drd->reg_pcs;
			break;
		case PTR_PMA:
			reg_base = phy_drd->reg_pma;
			break;
		default:
			dev_warn_once(phy_drd->dev,
				      "unknown phy region %d\n", tune->region);
			continue;
		}

		if (~tune->mask) {
			reg = readl(reg_base + tune->off);
			reg &= ~tune->mask;
		}
		reg |= tune->val;
		writel(reg, reg_base + tune->off);
	}
}

static void exynos5_usbdrd_pipe3_init(struct exynos5_usbdrd_phy *phy_drd)
{
	u32 reg;

	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM1);
	/* Set Tx De-Emphasis level */
	reg &= ~PHYPARAM1_PCS_TXDEEMPH_MASK;
	reg |=	PHYPARAM1_PCS_TXDEEMPH;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM1);

	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYTEST);
	reg &= ~PHYTEST_POWERDOWN_SSP;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYTEST);
}

static void
exynos5_usbdrd_usbdp_g2_v4_ctrl_pma_ready(struct exynos5_usbdrd_phy *phy_drd)
{
	void __iomem *regs_base = phy_drd->reg_phy;
	u32 reg;

	/* link pipe_clock selection to pclk of PMA */
	reg = readl(regs_base + EXYNOS850_DRD_CLKRST);
	reg |= CLKRST_LINK_PCLK_SEL;
	writel(reg, regs_base + EXYNOS850_DRD_CLKRST);

	reg = readl(regs_base + EXYNOS850_DRD_SECPMACTL);
	reg &= ~SECPMACTL_PMA_REF_FREQ_SEL;
	reg |= FIELD_PREP_CONST(SECPMACTL_PMA_REF_FREQ_SEL, 1);
	/* SFR reset */
	reg |= (SECPMACTL_PMA_LOW_PWR | SECPMACTL_PMA_APB_SW_RST);
	reg &= ~(SECPMACTL_PMA_ROPLL_REF_CLK_SEL |
		 SECPMACTL_PMA_LCPLL_REF_CLK_SEL);
	/* PMA power off */
	reg |= (SECPMACTL_PMA_TRSV_SW_RST | SECPMACTL_PMA_CMN_SW_RST |
		SECPMACTL_PMA_INIT_SW_RST);
	writel(reg, regs_base + EXYNOS850_DRD_SECPMACTL);

	udelay(1);

	reg = readl(regs_base + EXYNOS850_DRD_SECPMACTL);
	reg &= ~SECPMACTL_PMA_LOW_PWR;
	writel(reg, regs_base + EXYNOS850_DRD_SECPMACTL);

	udelay(1);

	/* release override */
	reg = readl(regs_base + EXYNOS850_DRD_LINKCTRL);
	reg &= ~LINKCTRL_FORCE_PIPE_EN;
	writel(reg, regs_base + EXYNOS850_DRD_LINKCTRL);

	udelay(1);

	/* APB enable */
	reg = readl(regs_base + EXYNOS850_DRD_SECPMACTL);
	reg &= ~SECPMACTL_PMA_APB_SW_RST;
	writel(reg, regs_base + EXYNOS850_DRD_SECPMACTL);
}

static void
exynos5_usbdrd_usbdp_g2_v4_pma_lane_mux_sel(struct exynos5_usbdrd_phy *phy_drd)
{
	void __iomem *regs_base = phy_drd->reg_pma;
	u32 reg;

	/* lane configuration: USB on all lanes */
	reg = readl(regs_base + EXYNOS9_PMA_USBDP_CMN_REG00B8);
	reg &= ~CMN_REG00B8_LANE_MUX_SEL_DP;
	writel(reg, regs_base + EXYNOS9_PMA_USBDP_CMN_REG00B8);

	/*
	 * FIXME: below code supports one connector orientation only. It needs
	 * updating once we can receive connector events.
	 */
	/* override of TX receiver detector and comparator: lane 1 */
	reg = readl(regs_base + EXYNOS9_PMA_USBDP_TRSV_REG0413);
	reg &= ~TRSV_REG0413_OVRD_LN1_TX_RXD_COMP_EN;
	reg &= ~TRSV_REG0413_OVRD_LN1_TX_RXD_EN;
	writel(reg, regs_base + EXYNOS9_PMA_USBDP_TRSV_REG0413);

	/* lane 3 */
	reg = readl(regs_base + EXYNOS9_PMA_USBDP_TRSV_REG0813);
	reg |= TRSV_REG0813_OVRD_LN3_TX_RXD_COMP_EN;
	reg |= TRSV_REG0813_OVRD_LN3_TX_RXD_EN;
	writel(reg, regs_base + EXYNOS9_PMA_USBDP_TRSV_REG0813);
}

static int
exynos5_usbdrd_usbdp_g2_v4_pma_check_pll_lock(struct exynos5_usbdrd_phy *phy_drd)
{
	static const unsigned int timeout_us = 40000;
	static const unsigned int sleep_us = 40;
	static const u32 locked = (CMN_REG01C0_ANA_LCPLL_LOCK_DONE |
				   CMN_REG01C0_ANA_LCPLL_AFC_DONE);
	u32 reg;
	int err;

	err = readl_poll_timeout(
			phy_drd->reg_pma + EXYNOS9_PMA_USBDP_CMN_REG01C0,
			reg, (reg & locked) == locked, sleep_us, timeout_us);
	if (err)
		dev_err(phy_drd->dev,
			"timed out waiting for PLL lock: %#.8x\n", reg);

	return err;
}

static void
exynos5_usbdrd_usbdp_g2_v4_pma_check_cdr_lock(struct exynos5_usbdrd_phy *phy_drd)
{
	static const unsigned int timeout_us = 40000;
	static const unsigned int sleep_us = 40;
	static const u32 locked =
		(TRSV_REG03C3_LN0_MON_RX_CDR_AFC_DONE
		 | TRSV_REG03C3_LN0_MON_RX_CDR_CAL_DONE
		 | TRSV_REG03C3_LN0_MON_RX_CDR_FLD_PLL_MODE_DONE
		 | TRSV_REG03C3_LN0_MON_RX_CDR_LOCK_DONE);
	u32 reg;
	int err;

	err = readl_poll_timeout(
			phy_drd->reg_pma + EXYNOS9_PMA_USBDP_TRSV_REG03C3,
			reg, (reg & locked) == locked, sleep_us, timeout_us);
	if (!err)
		return;

	dev_err(phy_drd->dev,
		"timed out waiting for CDR lock (l0): %#.8x, retrying\n", reg);

	/* based on cable orientation, this might be on the other phy port */
	err = readl_poll_timeout(
			phy_drd->reg_pma + EXYNOS9_PMA_USBDP_TRSV_REG07C3,
			reg, (reg & locked) == locked, sleep_us, timeout_us);
	if (err)
		dev_err(phy_drd->dev,
			"timed out waiting for CDR lock (l2): %#.8x\n", reg);
}

static void exynos5_usbdrd_utmi_init(struct exynos5_usbdrd_phy *phy_drd)
{
	u32 reg;

	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM0);
	/* Set Loss-of-Signal Detector sensitivity */
	reg &= ~PHYPARAM0_REF_LOSLEVEL_MASK;
	reg |=	PHYPARAM0_REF_LOSLEVEL;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM0);

	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM1);
	/* Set Tx De-Emphasis level */
	reg &= ~PHYPARAM1_PCS_TXDEEMPH_MASK;
	reg |=	PHYPARAM1_PCS_TXDEEMPH;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM1);

	/* UTMI Power Control */
	writel(PHYUTMI_OTGDISABLE, phy_drd->reg_phy + EXYNOS5_DRD_PHYUTMI);

	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYTEST);
	reg &= ~PHYTEST_POWERDOWN_HSP;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYTEST);
}

static int exynos5_usbdrd_phy_init(struct phy *phy)
{
	int ret;
	u32 reg;
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	ret = clk_bulk_prepare_enable(phy_drd->drv_data->n_clks, phy_drd->clks);
	if (ret)
		return ret;

	/* Reset USB 3.0 PHY */
	writel(0x0, phy_drd->reg_phy + EXYNOS5_DRD_PHYREG0);
	writel(0x0, phy_drd->reg_phy + EXYNOS5_DRD_PHYRESUME);

	/*
	 * Setting the Frame length Adj value[6:1] to default 0x20
	 * See xHCI 1.0 spec, 5.2.4
	 */
	reg =	LINKSYSTEM_XHCI_VERSION_CONTROL |
		LINKSYSTEM_FLADJ(0x20);
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_LINKSYSTEM);

	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM0);
	/* Select PHY CLK source */
	reg &= ~PHYPARAM0_REF_USE_PAD;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM0);

	/* This bit must be set for both HS and SS operations */
	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYUTMICLKSEL);
	reg |= PHYUTMICLKSEL_UTMI_CLKSEL;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYUTMICLKSEL);

	/* UTMI or PIPE3 specific init */
	inst->phy_cfg->phy_init(phy_drd);

	/* reference clock settings */
	reg = inst->phy_cfg->set_refclk(inst);

		/* Digital power supply in normal operating mode */
	reg |=	PHYCLKRST_RETENABLEN |
		/* Enable ref clock for SS function */
		PHYCLKRST_REF_SSP_EN |
		/* Enable spread spectrum */
		PHYCLKRST_SSC_EN |
		/* Power down HS Bias and PLL blocks in suspend mode */
		PHYCLKRST_COMMONONN |
		/* Reset the port */
		PHYCLKRST_PORTRESET;

	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYCLKRST);

	fsleep(10);

	reg &= ~PHYCLKRST_PORTRESET;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYCLKRST);

	clk_bulk_disable_unprepare(phy_drd->drv_data->n_clks, phy_drd->clks);

	return 0;
}

static int exynos5_usbdrd_phy_exit(struct phy *phy)
{
	int ret;
	u32 reg;
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	ret = clk_bulk_prepare_enable(phy_drd->drv_data->n_clks, phy_drd->clks);
	if (ret)
		return ret;

	reg =	PHYUTMI_OTGDISABLE |
		PHYUTMI_FORCESUSPEND |
		PHYUTMI_FORCESLEEP;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYUTMI);

	/* Resetting the PHYCLKRST enable bits to reduce leakage current */
	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYCLKRST);
	reg &= ~(PHYCLKRST_REF_SSP_EN |
		 PHYCLKRST_SSC_EN |
		 PHYCLKRST_COMMONONN);
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYCLKRST);

	/* Control PHYTEST to remove leakage current */
	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYTEST);
	reg |=	PHYTEST_POWERDOWN_SSP |
		PHYTEST_POWERDOWN_HSP;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYTEST);

	clk_bulk_disable_unprepare(phy_drd->drv_data->n_clks, phy_drd->clks);

	return 0;
}

static int exynos5_usbdrd_phy_power_on(struct phy *phy)
{
	int ret;
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	dev_dbg(phy_drd->dev, "Request to power_on usbdrd_phy phy\n");

	ret = clk_bulk_prepare_enable(phy_drd->drv_data->n_core_clks,
				      phy_drd->core_clks);
	if (ret)
		return ret;

	/* Enable VBUS supply */
	ret = regulator_bulk_enable(phy_drd->drv_data->n_regulators,
				    phy_drd->regulators);
	if (ret) {
		dev_err(phy_drd->dev, "Failed to enable PHY regulator(s)\n");
		goto fail_vbus;
	}

	/* Power-on PHY */
	inst->phy_cfg->phy_isol(inst, false);

	return 0;

fail_vbus:
	clk_bulk_disable_unprepare(phy_drd->drv_data->n_core_clks,
				   phy_drd->core_clks);

	return ret;
}

static int exynos5_usbdrd_phy_power_off(struct phy *phy)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	dev_dbg(phy_drd->dev, "Request to power_off usbdrd_phy phy\n");

	/* Power-off the PHY */
	inst->phy_cfg->phy_isol(inst, true);

	/* Disable VBUS supply */
	regulator_bulk_disable(phy_drd->drv_data->n_regulators,
			       phy_drd->regulators);

	clk_bulk_disable_unprepare(phy_drd->drv_data->n_core_clks,
				   phy_drd->core_clks);

	return 0;
}

static int crport_handshake(struct exynos5_usbdrd_phy *phy_drd,
			    u32 val, u32 cmd)
{
	unsigned int result;
	int err;

	writel(val | cmd, phy_drd->reg_phy + EXYNOS5_DRD_PHYREG0);

	err = readl_poll_timeout(phy_drd->reg_phy + EXYNOS5_DRD_PHYREG1,
				 result, (result & PHYREG1_CR_ACK), 1, 100);
	if (err == -ETIMEDOUT) {
		dev_err(phy_drd->dev, "CRPORT handshake timeout1 (0x%08x)\n", val);
		return err;
	}

	writel(val, phy_drd->reg_phy + EXYNOS5_DRD_PHYREG0);

	err = readl_poll_timeout(phy_drd->reg_phy + EXYNOS5_DRD_PHYREG1,
				 result, !(result & PHYREG1_CR_ACK), 1, 100);
	if (err == -ETIMEDOUT) {
		dev_err(phy_drd->dev, "CRPORT handshake timeout2 (0x%08x)\n", val);
		return err;
	}

	return 0;
}

static int crport_ctrl_write(struct exynos5_usbdrd_phy *phy_drd,
			     u32 addr, u32 data)
{
	int ret;

	/* Write Address */
	writel(PHYREG0_CR_DATA_IN(addr),
	       phy_drd->reg_phy + EXYNOS5_DRD_PHYREG0);
	ret = crport_handshake(phy_drd, PHYREG0_CR_DATA_IN(addr),
			       PHYREG0_CR_CAP_ADDR);
	if (ret)
		return ret;

	/* Write Data */
	writel(PHYREG0_CR_DATA_IN(data),
	       phy_drd->reg_phy + EXYNOS5_DRD_PHYREG0);
	ret = crport_handshake(phy_drd, PHYREG0_CR_DATA_IN(data),
			       PHYREG0_CR_CAP_DATA);
	if (ret)
		return ret;

	ret = crport_handshake(phy_drd, PHYREG0_CR_DATA_IN(data),
			       PHYREG0_CR_WRITE);

	return ret;
}

/*
 * Calibrate few PHY parameters using CR_PORT register to meet
 * SuperSpeed requirements on Exynos5420 and Exynos5800 systems,
 * which have 28nm USB 3.0 DRD PHY.
 */
static int exynos5420_usbdrd_phy_calibrate(struct exynos5_usbdrd_phy *phy_drd)
{
	unsigned int temp;
	int ret = 0;

	/*
	 * Change los_bias to (0x5) for 28nm PHY from a
	 * default value (0x0); los_level is set as default
	 * (0x9) as also reflected in los_level[30:26] bits
	 * of PHYPARAM0 register.
	 */
	temp = LOSLEVEL_OVRD_IN_LOS_BIAS_5420 |
		LOSLEVEL_OVRD_IN_EN |
		LOSLEVEL_OVRD_IN_LOS_LEVEL_DEFAULT;
	ret = crport_ctrl_write(phy_drd,
				EXYNOS5_DRD_PHYSS_LOSLEVEL_OVRD_IN,
				temp);
	if (ret) {
		dev_err(phy_drd->dev,
			"Failed setting Loss-of-Signal level for SuperSpeed\n");
		return ret;
	}

	/*
	 * Set tx_vboost_lvl to (0x5) for 28nm PHY Tuning,
	 * to raise Tx signal level from its default value of (0x4)
	 */
	temp = TX_VBOOSTLEVEL_OVRD_IN_VBOOST_5420;
	ret = crport_ctrl_write(phy_drd,
				EXYNOS5_DRD_PHYSS_TX_VBOOSTLEVEL_OVRD_IN,
				temp);
	if (ret) {
		dev_err(phy_drd->dev,
			"Failed setting Tx-Vboost-Level for SuperSpeed\n");
		return ret;
	}

	/*
	 * Set proper time to wait for RxDetect measurement, for
	 * desired reference clock of PHY, by tuning the CR_PORT
	 * register LANE0.TX_DEBUG which is internal to PHY.
	 * This fixes issue with few USB 3.0 devices, which are
	 * not detected (not even generate interrupts on the bus
	 * on insertion) without this change.
	 * e.g. Samsung SUM-TSB16S 3.0 USB drive.
	 */
	switch (phy_drd->extrefclk) {
	case EXYNOS5_FSEL_50MHZ:
		temp = LANE0_TX_DEBUG_RXDET_MEAS_TIME_48M_50M_52M;
		break;
	case EXYNOS5_FSEL_20MHZ:
	case EXYNOS5_FSEL_19MHZ2:
		temp = LANE0_TX_DEBUG_RXDET_MEAS_TIME_19M2_20M;
		break;
	case EXYNOS5_FSEL_24MHZ:
	default:
		temp = LANE0_TX_DEBUG_RXDET_MEAS_TIME_24M;
		break;
	}

	ret = crport_ctrl_write(phy_drd,
				EXYNOS5_DRD_PHYSS_LANE0_TX_DEBUG,
				temp);
	if (ret)
		dev_err(phy_drd->dev,
			"Fail to set RxDet measurement time for SuperSpeed\n");

	return ret;
}

static struct phy *exynos5_usbdrd_phy_xlate(struct device *dev,
					const struct of_phandle_args *args)
{
	struct exynos5_usbdrd_phy *phy_drd = dev_get_drvdata(dev);

	if (WARN_ON(args->args[0] >= EXYNOS5_DRDPHYS_NUM))
		return ERR_PTR(-ENODEV);

	return phy_drd->phys[args->args[0]].phy;
}

static int exynos5_usbdrd_phy_calibrate(struct phy *phy)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	if (inst->phy_cfg->id == EXYNOS5_DRDPHY_UTMI)
		return exynos5420_usbdrd_phy_calibrate(phy_drd);
	return 0;
}

static const struct phy_ops exynos5_usbdrd_phy_ops = {
	.init		= exynos5_usbdrd_phy_init,
	.exit		= exynos5_usbdrd_phy_exit,
	.power_on	= exynos5_usbdrd_phy_power_on,
	.power_off	= exynos5_usbdrd_phy_power_off,
	.calibrate	= exynos5_usbdrd_phy_calibrate,
	.owner		= THIS_MODULE,
};

static void
exynos5_usbdrd_usb_v3p1_pipe_override(struct exynos5_usbdrd_phy *phy_drd)
{
	void __iomem *regs_base = phy_drd->reg_phy;
	u32 reg;

	/* force pipe3 signal for link */
	reg = readl(regs_base + EXYNOS850_DRD_LINKCTRL);
	reg &= ~LINKCTRL_FORCE_PHYSTATUS;
	reg |= LINKCTRL_FORCE_PIPE_EN | LINKCTRL_FORCE_RXELECIDLE;
	writel(reg, regs_base + EXYNOS850_DRD_LINKCTRL);

	/* PMA disable */
	reg = readl(regs_base + EXYNOS850_DRD_SECPMACTL);
	reg |= SECPMACTL_PMA_LOW_PWR;
	writel(reg, regs_base + EXYNOS850_DRD_SECPMACTL);
}

static void exynos850_usbdrd_utmi_init(struct exynos5_usbdrd_phy *phy_drd)
{
	void __iomem *regs_base = phy_drd->reg_phy;
	u32 reg;
	u32 ss_ports;

	/*
	 * Disable HWACG (hardware auto clock gating control). This will force
	 * QACTIVE signal in Q-Channel interface to HIGH level, to make sure
	 * the PHY clock is not gated by the hardware.
	 */
	reg = readl(regs_base + EXYNOS850_DRD_LINKCTRL);
	reg |= LINKCTRL_FORCE_QACT;
	writel(reg, regs_base + EXYNOS850_DRD_LINKCTRL);

	reg = readl(regs_base + EXYNOS850_DRD_LINKPORT);
	ss_ports = FIELD_GET(LINKPORT_HOST_NUM_U3, reg);

	/* Start PHY Reset (POR=high) */
	reg = readl(regs_base + EXYNOS850_DRD_CLKRST);
	if (ss_ports) {
		reg |= CLKRST_PHY20_SW_POR;
		reg |= CLKRST_PHY20_SW_POR_SEL;
		reg |= CLKRST_PHY_RESET_SEL;
	}
	reg |= CLKRST_PHY_SW_RST;
	writel(reg, regs_base + EXYNOS850_DRD_CLKRST);

	/* Enable UTMI+ */
	reg = readl(regs_base + EXYNOS850_DRD_UTMI);
	reg &= ~(UTMI_FORCE_SUSPEND | UTMI_FORCE_SLEEP | UTMI_DP_PULLDOWN |
		 UTMI_DM_PULLDOWN);
	writel(reg, regs_base + EXYNOS850_DRD_UTMI);

	/* Set PHY clock and control HS PHY */
	reg = readl(regs_base + EXYNOS850_DRD_HSP);
	reg |= HSP_EN_UTMISUSPEND | HSP_COMMONONN;
	writel(reg, regs_base + EXYNOS850_DRD_HSP);

	/* Set VBUS Valid and D+ pull-up control by VBUS pad usage */
	reg = readl(regs_base + EXYNOS850_DRD_LINKCTRL);
	reg |= LINKCTRL_BUS_FILTER_BYPASS(0xf);
	writel(reg, regs_base + EXYNOS850_DRD_LINKCTRL);

	reg = readl(regs_base + EXYNOS850_DRD_UTMI);
	reg |= UTMI_FORCE_BVALID | UTMI_FORCE_VBUSVALID;
	writel(reg, regs_base + EXYNOS850_DRD_UTMI);

	reg = readl(regs_base + EXYNOS850_DRD_HSP);
	reg |= HSP_VBUSVLDEXT | HSP_VBUSVLDEXTSEL;
	writel(reg, regs_base + EXYNOS850_DRD_HSP);

	reg = readl(regs_base + EXYNOS850_DRD_SSPPLLCTL);
	reg &= ~SSPPLLCTL_FSEL;
	switch (phy_drd->extrefclk) {
	case EXYNOS5_FSEL_50MHZ:
		reg |= FIELD_PREP_CONST(SSPPLLCTL_FSEL, 7);
		break;
	case EXYNOS5_FSEL_26MHZ:
		reg |= FIELD_PREP_CONST(SSPPLLCTL_FSEL, 6);
		break;
	case EXYNOS5_FSEL_24MHZ:
		reg |= FIELD_PREP_CONST(SSPPLLCTL_FSEL, 2);
		break;
	case EXYNOS5_FSEL_20MHZ:
		reg |= FIELD_PREP_CONST(SSPPLLCTL_FSEL, 1);
		break;
	case EXYNOS5_FSEL_19MHZ2:
		reg |= FIELD_PREP_CONST(SSPPLLCTL_FSEL, 0);
		break;
	default:
		dev_warn(phy_drd->dev, "unsupported ref clk: %#.2x\n",
			 phy_drd->extrefclk);
		break;
	}
	writel(reg, regs_base + EXYNOS850_DRD_SSPPLLCTL);

	if (phy_drd->drv_data->phy_tunes)
		exynos5_usbdrd_apply_phy_tunes(phy_drd,
					       PTS_UTMI_POSTINIT);

	/* Power up PHY analog blocks */
	reg = readl(regs_base + EXYNOS850_DRD_HSP_TEST);
	reg &= ~HSP_TEST_SIDDQ;
	writel(reg, regs_base + EXYNOS850_DRD_HSP_TEST);

	/* Finish PHY reset (POR=low) */
	fsleep(10); /* required before doing POR=low */
	reg = readl(regs_base + EXYNOS850_DRD_CLKRST);
	if (ss_ports) {
		reg |= CLKRST_PHY20_SW_POR_SEL;
		reg &= ~CLKRST_PHY20_SW_POR;
	}
	reg &= ~(CLKRST_PHY_SW_RST | CLKRST_PORT_RST);
	writel(reg, regs_base + EXYNOS850_DRD_CLKRST);
	fsleep(75); /* required after POR=low for guaranteed PHY clock */

	/* Disable single ended signal out */
	reg = readl(regs_base + EXYNOS850_DRD_HSP);
	reg &= ~HSP_FSV_OUT_EN;
	writel(reg, regs_base + EXYNOS850_DRD_HSP);

	if (ss_ports)
		exynos5_usbdrd_usb_v3p1_pipe_override(phy_drd);
}

static int exynos850_usbdrd_phy_init(struct phy *phy)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);
	int ret;

	ret = clk_bulk_prepare_enable(phy_drd->drv_data->n_clks, phy_drd->clks);
	if (ret)
		return ret;

	/* UTMI or PIPE3 specific init */
	inst->phy_cfg->phy_init(phy_drd);

	clk_bulk_disable_unprepare(phy_drd->drv_data->n_clks, phy_drd->clks);

	return 0;
}

static int exynos850_usbdrd_phy_exit(struct phy *phy)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);
	void __iomem *regs_base = phy_drd->reg_phy;
	u32 reg;
	int ret;

	ret = clk_bulk_prepare_enable(phy_drd->drv_data->n_clks, phy_drd->clks);
	if (ret)
		return ret;

	/* Set PHY clock and control HS PHY */
	reg = readl(regs_base + EXYNOS850_DRD_UTMI);
	reg &= ~(UTMI_DP_PULLDOWN | UTMI_DM_PULLDOWN);
	reg |= UTMI_FORCE_SUSPEND | UTMI_FORCE_SLEEP;
	writel(reg, regs_base + EXYNOS850_DRD_UTMI);

	/* Power down PHY analog blocks */
	reg = readl(regs_base + EXYNOS850_DRD_HSP_TEST);
	reg |= HSP_TEST_SIDDQ;
	writel(reg, regs_base + EXYNOS850_DRD_HSP_TEST);

	/* Link reset */
	reg = readl(regs_base + EXYNOS850_DRD_CLKRST);
	reg |= CLKRST_LINK_SW_RST;
	writel(reg, regs_base + EXYNOS850_DRD_CLKRST);
	fsleep(10); /* required before doing POR=low */
	reg &= ~CLKRST_LINK_SW_RST;
	writel(reg, regs_base + EXYNOS850_DRD_CLKRST);

	clk_bulk_disable_unprepare(phy_drd->drv_data->n_clks, phy_drd->clks);

	return 0;
}

static const struct phy_ops exynos850_usbdrd_phy_ops = {
	.init		= exynos850_usbdrd_phy_init,
	.exit		= exynos850_usbdrd_phy_exit,
	.power_on	= exynos5_usbdrd_phy_power_on,
	.power_off	= exynos5_usbdrd_phy_power_off,
	.owner		= THIS_MODULE,
};

static void exynos5_usbdrd_gs101_pipe3_init(struct exynos5_usbdrd_phy *phy_drd)
{
	void __iomem *regs_pma = phy_drd->reg_pma;
	void __iomem *regs_phy = phy_drd->reg_phy;
	u32 reg;

	exynos5_usbdrd_usbdp_g2_v4_ctrl_pma_ready(phy_drd);

	/* force aux off */
	reg = readl(regs_pma + EXYNOS9_PMA_USBDP_CMN_REG0008);
	reg &= ~CMN_REG0008_AUX_EN;
	reg |= CMN_REG0008_OVRD_AUX_EN;
	writel(reg, regs_pma + EXYNOS9_PMA_USBDP_CMN_REG0008);

	exynos5_usbdrd_apply_phy_tunes(phy_drd, PTS_PIPE3_PREINIT);
	exynos5_usbdrd_apply_phy_tunes(phy_drd, PTS_PIPE3_INIT);
	exynos5_usbdrd_apply_phy_tunes(phy_drd, PTS_PIPE3_POSTINIT);

	exynos5_usbdrd_usbdp_g2_v4_pma_lane_mux_sel(phy_drd);

	/* reset release from port */
	reg = readl(regs_phy + EXYNOS850_DRD_SECPMACTL);
	reg &= ~(SECPMACTL_PMA_TRSV_SW_RST | SECPMACTL_PMA_CMN_SW_RST |
		 SECPMACTL_PMA_INIT_SW_RST);
	writel(reg, regs_phy + EXYNOS850_DRD_SECPMACTL);

	if (!exynos5_usbdrd_usbdp_g2_v4_pma_check_pll_lock(phy_drd))
		exynos5_usbdrd_usbdp_g2_v4_pma_check_cdr_lock(phy_drd);
}

static int exynos5_usbdrd_gs101_phy_init(struct phy *phy)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);
	int ret;

	if (inst->phy_cfg->id == EXYNOS5_DRDPHY_UTMI) {
		/* Power-on PHY ... */
		ret = regulator_bulk_enable(phy_drd->drv_data->n_regulators,
					    phy_drd->regulators);
		if (ret) {
			dev_err(phy_drd->dev,
				"Failed to enable PHY regulator(s)\n");
			return ret;
		}
	}
	/*
	 * ... and ungate power via PMU. Without this here, we get an SError
	 * trying to access PMA registers
	 */
	exynos5_usbdrd_phy_isol(inst, false);

	return exynos850_usbdrd_phy_init(phy);
}

static int exynos5_usbdrd_gs101_phy_exit(struct phy *phy)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);
	int ret;

	if (inst->phy_cfg->id != EXYNOS5_DRDPHY_UTMI)
		return 0;

	ret = exynos850_usbdrd_phy_exit(phy);
	if (ret)
		return ret;

	exynos5_usbdrd_phy_isol(inst, true);
	return regulator_bulk_disable(phy_drd->drv_data->n_regulators,
				      phy_drd->regulators);
}

static const struct phy_ops gs101_usbdrd_phy_ops = {
	.init		= exynos5_usbdrd_gs101_phy_init,
	.exit		= exynos5_usbdrd_gs101_phy_exit,
	.owner		= THIS_MODULE,
};

static int exynos5_usbdrd_phy_clk_handle(struct exynos5_usbdrd_phy *phy_drd)
{
	int ret;
	struct clk *ref_clk;
	unsigned long ref_rate;

	phy_drd->clks = devm_kcalloc(phy_drd->dev, phy_drd->drv_data->n_clks,
				     sizeof(*phy_drd->clks), GFP_KERNEL);
	if (!phy_drd->clks)
		return -ENOMEM;

	for (int i = 0; i < phy_drd->drv_data->n_clks; ++i)
		phy_drd->clks[i].id = phy_drd->drv_data->clk_names[i];

	ret = devm_clk_bulk_get(phy_drd->dev, phy_drd->drv_data->n_clks,
				phy_drd->clks);
	if (ret)
		return dev_err_probe(phy_drd->dev, ret,
				     "failed to get phy clock(s)\n");

	phy_drd->core_clks = devm_kcalloc(phy_drd->dev,
					  phy_drd->drv_data->n_core_clks,
					  sizeof(*phy_drd->core_clks),
					  GFP_KERNEL);
	if (!phy_drd->core_clks)
		return -ENOMEM;

	for (int i = 0; i < phy_drd->drv_data->n_core_clks; ++i)
		phy_drd->core_clks[i].id = phy_drd->drv_data->core_clk_names[i];

	ret = devm_clk_bulk_get(phy_drd->dev, phy_drd->drv_data->n_core_clks,
				phy_drd->core_clks);
	if (ret)
		return dev_err_probe(phy_drd->dev, ret,
				     "failed to get phy core clock(s)\n");

	ref_clk = NULL;
	for (int i = 0; i < phy_drd->drv_data->n_core_clks; ++i) {
		if (!strcmp(phy_drd->core_clks[i].id, "ref")) {
			ref_clk = phy_drd->core_clks[i].clk;
			break;
		}
	}
	if (!ref_clk)
		return dev_err_probe(phy_drd->dev, -ENODEV,
				     "failed to find phy reference clock\n");

	ref_rate = clk_get_rate(ref_clk);
	ret = exynos5_rate_to_clk(ref_rate, &phy_drd->extrefclk);
	if (ret)
		return dev_err_probe(phy_drd->dev, ret,
				     "clock rate (%ld) not supported\n",
				     ref_rate);

	return 0;
}

static const struct exynos5_usbdrd_phy_config phy_cfg_exynos5[] = {
	{
		.id		= EXYNOS5_DRDPHY_UTMI,
		.phy_isol	= exynos5_usbdrd_phy_isol,
		.phy_init	= exynos5_usbdrd_utmi_init,
		.set_refclk	= exynos5_usbdrd_utmi_set_refclk,
	},
	{
		.id		= EXYNOS5_DRDPHY_PIPE3,
		.phy_isol	= exynos5_usbdrd_phy_isol,
		.phy_init	= exynos5_usbdrd_pipe3_init,
		.set_refclk	= exynos5_usbdrd_pipe3_set_refclk,
	},
};

static const struct exynos5_usbdrd_phy_config phy_cfg_exynos850[] = {
	{
		.id		= EXYNOS5_DRDPHY_UTMI,
		.phy_isol	= exynos5_usbdrd_phy_isol,
		.phy_init	= exynos850_usbdrd_utmi_init,
	},
};

static const char * const exynos5_clk_names[] = {
	"phy",
};

static const char * const exynos5_core_clk_names[] = {
	"ref",
};

static const char * const exynos5433_core_clk_names[] = {
	"ref", "phy_pipe", "phy_utmi", "itp",
};

static const char * const exynos5_regulator_names[] = {
	"vbus", "vbus-boost",
};

static const struct exynos5_usbdrd_phy_drvdata exynos5420_usbdrd_phy = {
	.phy_cfg		= phy_cfg_exynos5,
	.phy_ops		= &exynos5_usbdrd_phy_ops,
	.pmu_offset_usbdrd0_phy	= EXYNOS5_USBDRD_PHY_CONTROL,
	.pmu_offset_usbdrd1_phy	= EXYNOS5420_USBDRD1_PHY_CONTROL,
	.clk_names		= exynos5_clk_names,
	.n_clks			= ARRAY_SIZE(exynos5_clk_names),
	.core_clk_names		= exynos5_core_clk_names,
	.n_core_clks		= ARRAY_SIZE(exynos5_core_clk_names),
	.regulator_names	= exynos5_regulator_names,
	.n_regulators		= ARRAY_SIZE(exynos5_regulator_names),
};

static const struct exynos5_usbdrd_phy_drvdata exynos5250_usbdrd_phy = {
	.phy_cfg		= phy_cfg_exynos5,
	.phy_ops		= &exynos5_usbdrd_phy_ops,
	.pmu_offset_usbdrd0_phy	= EXYNOS5_USBDRD_PHY_CONTROL,
	.clk_names		= exynos5_clk_names,
	.n_clks			= ARRAY_SIZE(exynos5_clk_names),
	.core_clk_names		= exynos5_core_clk_names,
	.n_core_clks		= ARRAY_SIZE(exynos5_core_clk_names),
	.regulator_names	= exynos5_regulator_names,
	.n_regulators		= ARRAY_SIZE(exynos5_regulator_names),
};

static const struct exynos5_usbdrd_phy_drvdata exynos5433_usbdrd_phy = {
	.phy_cfg		= phy_cfg_exynos5,
	.phy_ops		= &exynos5_usbdrd_phy_ops,
	.pmu_offset_usbdrd0_phy	= EXYNOS5_USBDRD_PHY_CONTROL,
	.pmu_offset_usbdrd1_phy	= EXYNOS5433_USBHOST30_PHY_CONTROL,
	.clk_names		= exynos5_clk_names,
	.n_clks			= ARRAY_SIZE(exynos5_clk_names),
	.core_clk_names		= exynos5433_core_clk_names,
	.n_core_clks		= ARRAY_SIZE(exynos5433_core_clk_names),
	.regulator_names	= exynos5_regulator_names,
	.n_regulators		= ARRAY_SIZE(exynos5_regulator_names),
};

static const struct exynos5_usbdrd_phy_drvdata exynos7_usbdrd_phy = {
	.phy_cfg		= phy_cfg_exynos5,
	.phy_ops		= &exynos5_usbdrd_phy_ops,
	.pmu_offset_usbdrd0_phy	= EXYNOS5_USBDRD_PHY_CONTROL,
	.clk_names		= exynos5_clk_names,
	.n_clks			= ARRAY_SIZE(exynos5_clk_names),
	.core_clk_names		= exynos5433_core_clk_names,
	.n_core_clks		= ARRAY_SIZE(exynos5433_core_clk_names),
	.regulator_names	= exynos5_regulator_names,
	.n_regulators		= ARRAY_SIZE(exynos5_regulator_names),
};

static const struct exynos5_usbdrd_phy_drvdata exynos850_usbdrd_phy = {
	.phy_cfg		= phy_cfg_exynos850,
	.phy_ops		= &exynos850_usbdrd_phy_ops,
	.pmu_offset_usbdrd0_phy	= EXYNOS5_USBDRD_PHY_CONTROL,
	.clk_names		= exynos5_clk_names,
	.n_clks			= ARRAY_SIZE(exynos5_clk_names),
	.core_clk_names		= exynos5_core_clk_names,
	.n_core_clks		= ARRAY_SIZE(exynos5_core_clk_names),
	.regulator_names	= exynos5_regulator_names,
	.n_regulators		= ARRAY_SIZE(exynos5_regulator_names),
};

static const struct exynos5_usbdrd_phy_config phy_cfg_gs101[] = {
	{
		.id		= EXYNOS5_DRDPHY_UTMI,
		.phy_isol	= exynos5_usbdrd_phy_isol,
		.phy_init	= exynos850_usbdrd_utmi_init,
	},
	{
		.id		= EXYNOS5_DRDPHY_PIPE3,
		.phy_isol	= exynos5_usbdrd_phy_isol,
		.phy_init	= exynos5_usbdrd_gs101_pipe3_init,
	},
};

static const struct exynos5_usbdrd_phy_tuning gs101_tunes_utmi_postinit[] = {
	PHY_TUNING_ENTRY_PHY(EXYNOS850_DRD_HSPPARACON,
			     (HSPPARACON_TXVREF | HSPPARACON_TXRES |
			      HSPPARACON_TXPREEMPAMP | HSPPARACON_SQRX |
			      HSPPARACON_COMPDIS),
			     (FIELD_PREP_CONST(HSPPARACON_TXVREF, 6) |
			      FIELD_PREP_CONST(HSPPARACON_TXRES, 1) |
			      FIELD_PREP_CONST(HSPPARACON_TXPREEMPAMP, 3) |
			      FIELD_PREP_CONST(HSPPARACON_SQRX, 5) |
			      FIELD_PREP_CONST(HSPPARACON_COMPDIS, 7))),
	PHY_TUNING_ENTRY_LAST
};

static const struct exynos5_usbdrd_phy_tuning gs101_tunes_pipe3_preinit[] = {
	/* preinit */
	/* CDR data mode exit GEN1 ON / GEN2 OFF */
	PHY_TUNING_ENTRY_PMA(0x0c8c, -1, 0xff),
	PHY_TUNING_ENTRY_PMA(0x1c8c, -1, 0xff),
	PHY_TUNING_ENTRY_PMA(0x0c9c, -1, 0x7d),
	PHY_TUNING_ENTRY_PMA(0x1c9c, -1, 0x7d),
	/* improve EDS distribution */
	PHY_TUNING_ENTRY_PMA(0x0e7c, -1, 0x06),
	PHY_TUNING_ENTRY_PMA(0x09e0, -1, 0x00),
	PHY_TUNING_ENTRY_PMA(0x09e4, -1, 0x36),
	PHY_TUNING_ENTRY_PMA(0x1e7c, -1, 0x06),
	PHY_TUNING_ENTRY_PMA(0x1e90, -1, 0x00),
	PHY_TUNING_ENTRY_PMA(0x1e94, -1, 0x36),
	/* improve LVCC */
	PHY_TUNING_ENTRY_PMA(0x08f0, -1, 0x30),
	PHY_TUNING_ENTRY_PMA(0x18f0, -1, 0x30),
	/* LFPS RX VIH shmoo hole */
	PHY_TUNING_ENTRY_PMA(0x0a08, -1, 0x0c),
	PHY_TUNING_ENTRY_PMA(0x1a08, -1, 0x0c),
	/* remove unrelated option for v4 phy */
	PHY_TUNING_ENTRY_PMA(0x0a0c, -1, 0x05),
	PHY_TUNING_ENTRY_PMA(0x1a0c, -1, 0x05),
	/* improve Gen2 LVCC */
	PHY_TUNING_ENTRY_PMA(0x00f8, -1, 0x1c),
	PHY_TUNING_ENTRY_PMA(0x00fc, -1, 0x54),
	/* Change Vth of RCV_DET because of TD 7.40 Polling Retry Test */
	PHY_TUNING_ENTRY_PMA(0x104c, -1, 0x07),
	PHY_TUNING_ENTRY_PMA(0x204c, -1, 0x07),
	/* reduce Ux Exit time, assuming 26MHz clock */
	/* Gen1 */
	PHY_TUNING_ENTRY_PMA(0x0ca8, -1, 0x00),
	PHY_TUNING_ENTRY_PMA(0x0cac, -1, 0x04),
	PHY_TUNING_ENTRY_PMA(0x1ca8, -1, 0x00),
	PHY_TUNING_ENTRY_PMA(0x1cac, -1, 0x04),
	/* Gen2 */
	PHY_TUNING_ENTRY_PMA(0x0cb8, -1, 0x00),
	PHY_TUNING_ENTRY_PMA(0x0cbc, -1, 0x04),
	PHY_TUNING_ENTRY_PMA(0x1cb8, -1, 0x00),
	PHY_TUNING_ENTRY_PMA(0x1cbc, -1, 0x04),
	/* RX impedance setting */
	PHY_TUNING_ENTRY_PMA(0x0bb0, 0x03, 0x01),
	PHY_TUNING_ENTRY_PMA(0x0bb4, 0xf0, 0xa0),
	PHY_TUNING_ENTRY_PMA(0x1bb0, 0x03, 0x01),
	PHY_TUNING_ENTRY_PMA(0x1bb4, 0xf0, 0xa0),

	PHY_TUNING_ENTRY_LAST
};

static const struct exynos5_usbdrd_phy_tuning gs101_tunes_pipe3_init[] = {
	/* init */
	/* abnormal common pattern mask */
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_BACK_END_MODE_VEC,
			     BACK_END_MODE_VEC_DISABLE_DATA_MASK, 0),
	/* de-serializer enabled when U2 */
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_OUT_VEC_2, PCS_OUT_VEC_B4_DYNAMIC,
			     PCS_OUT_VEC_B4_SEL_OUT),
	/* TX Keeper Disable, Squelch on when U3 */
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_OUT_VEC_3, PCS_OUT_VEC_B7_DYNAMIC,
			     PCS_OUT_VEC_B7_SEL_OUT | PCS_OUT_VEC_B2_SEL_OUT),
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_NS_VEC_PS1_N1, -1,
			     (FIELD_PREP_CONST(NS_VEC_NS_REQ, 5) |
			      NS_VEC_ENABLE_TIMER |
			      FIELD_PREP_CONST(NS_VEC_SEL_TIMEOUT, 3))),
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_NS_VEC_PS2_N0, -1,
			     (FIELD_PREP_CONST(NS_VEC_NS_REQ, 1) |
			      NS_VEC_ENABLE_TIMER |
			      FIELD_PREP_CONST(NS_VEC_SEL_TIMEOUT, 3) |
			      FIELD_PREP_CONST(NS_VEC_COND_MASK, 2) |
			      FIELD_PREP_CONST(NS_VEC_EXP_COND, 2))),
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_NS_VEC_PS3_N0, -1,
			     (FIELD_PREP_CONST(NS_VEC_NS_REQ, 1) |
			      NS_VEC_ENABLE_TIMER |
			      FIELD_PREP_CONST(NS_VEC_SEL_TIMEOUT, 3) |
			      FIELD_PREP_CONST(NS_VEC_COND_MASK, 7) |
			      FIELD_PREP_CONST(NS_VEC_EXP_COND, 7))),
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_TIMEOUT_0, -1, 112),
	/* Block Aligner Type B */
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_RX_CONTROL, 0,
			     RX_CONTROL_EN_BLOCK_ALIGNER_TYPE_B),
	/* Block align at TS1/TS2 for Gen2 stability (Gen2 only) */
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_RX_CONTROL_DEBUG,
		RX_CONTROL_DEBUG_NUM_COM_FOUND,
		(RX_CONTROL_DEBUG_EN_TS_CHECK |
		 /*
		  * increase pcs ts1 adding packet-cnt 1 --> 4
		  * lnx_rx_valid_rstn_delay_rise_sp/ssp :
		  * 19.6us(0x200) -> 15.3us(0x4)
		  */
		 FIELD_PREP_CONST(RX_CONTROL_DEBUG_NUM_COM_FOUND, 4))),
	/* Gen1 Tx DRIVER pre-shoot, de-emphasis, level ctrl */
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_HS_TX_COEF_MAP_0,
		(HS_TX_COEF_MAP_0_SSTX_DEEMP | HS_TX_COEF_MAP_0_SSTX_LEVEL |
		 HS_TX_COEF_MAP_0_SSTX_PRE_SHOOT),
		(FIELD_PREP_CONST(HS_TX_COEF_MAP_0_SSTX_DEEMP, 8) |
		 FIELD_PREP_CONST(HS_TX_COEF_MAP_0_SSTX_LEVEL, 0xb) |
		 FIELD_PREP_CONST(HS_TX_COEF_MAP_0_SSTX_PRE_SHOOT, 0))),
	/* Gen2 Tx DRIVER level ctrl */
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_LOCAL_COEF,
		LOCAL_COEF_PMA_CENTER_COEF,
		FIELD_PREP_CONST(LOCAL_COEF_PMA_CENTER_COEF, 0xb)),
	/* Gen2 U1 exit LFPS duration : 900ns ~ 1.2us */
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_TIMEOUT_3, -1, 4096),
	/* set skp_remove_th 0x2 -> 0x7 for avoiding retry problem. */
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_EBUF_PARAM,
		EBUF_PARAM_SKP_REMOVE_TH_EMPTY_MODE,
		FIELD_PREP_CONST(EBUF_PARAM_SKP_REMOVE_TH_EMPTY_MODE, 0x7)),

	PHY_TUNING_ENTRY_LAST
};

static const struct exynos5_usbdrd_phy_tuning gs101_tunes_pipe3_postlock[] = {
	/* Squelch off when U3 */
	PHY_TUNING_ENTRY_PCS(EXYNOS9_PCS_OUT_VEC_3, PCS_OUT_VEC_B2_SEL_OUT, 0),

	PHY_TUNING_ENTRY_LAST
};

static const struct exynos5_usbdrd_phy_tuning *gs101_tunes[PTS_MAX] = {
	[PTS_UTMI_POSTINIT] = gs101_tunes_utmi_postinit,
	[PTS_PIPE3_PREINIT] = gs101_tunes_pipe3_preinit,
	[PTS_PIPE3_INIT] = gs101_tunes_pipe3_init,
	[PTS_PIPE3_POSTLOCK] = gs101_tunes_pipe3_postlock,
};

static const char * const gs101_clk_names[] = {
	"phy", "ctrl_aclk", "ctrl_pclk", "scl_pclk",
};

static const char * const gs101_regulator_names[] = {
	"pll",
	"dvdd-usb20", "vddh-usb20", "vdd33-usb20",
	"vdda-usbdp", "vddh-usbdp",
};

static const struct exynos5_usbdrd_phy_drvdata gs101_usbd31rd_phy = {
	.phy_cfg			= phy_cfg_gs101,
	.phy_tunes			= gs101_tunes,
	.phy_ops			= &gs101_usbdrd_phy_ops,
	.pmu_offset_usbdrd0_phy		= GS101_PHY_CTRL_USB20,
	.pmu_offset_usbdrd0_phy_ss	= GS101_PHY_CTRL_USBDP,
	.clk_names			= gs101_clk_names,
	.n_clks				= ARRAY_SIZE(gs101_clk_names),
	.core_clk_names			= exynos5_core_clk_names,
	.n_core_clks			= ARRAY_SIZE(exynos5_core_clk_names),
	.regulator_names		= gs101_regulator_names,
	.n_regulators			= ARRAY_SIZE(gs101_regulator_names),
};

static const struct of_device_id exynos5_usbdrd_phy_of_match[] = {
	{
		.compatible = "google,gs101-usb31drd-phy",
		.data = &gs101_usbd31rd_phy
	}, {
		.compatible = "samsung,exynos5250-usbdrd-phy",
		.data = &exynos5250_usbdrd_phy
	}, {
		.compatible = "samsung,exynos5420-usbdrd-phy",
		.data = &exynos5420_usbdrd_phy
	}, {
		.compatible = "samsung,exynos5433-usbdrd-phy",
		.data = &exynos5433_usbdrd_phy
	}, {
		.compatible = "samsung,exynos7-usbdrd-phy",
		.data = &exynos7_usbdrd_phy
	}, {
		.compatible = "samsung,exynos850-usbdrd-phy",
		.data = &exynos850_usbdrd_phy
	},
	{ },
};
MODULE_DEVICE_TABLE(of, exynos5_usbdrd_phy_of_match);

static int exynos5_usbdrd_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct exynos5_usbdrd_phy *phy_drd;
	struct phy_provider *phy_provider;
	const struct exynos5_usbdrd_phy_drvdata *drv_data;
	struct regmap *reg_pmu;
	u32 pmu_offset;
	int i, ret;
	int channel;

	phy_drd = devm_kzalloc(dev, sizeof(*phy_drd), GFP_KERNEL);
	if (!phy_drd)
		return -ENOMEM;

	dev_set_drvdata(dev, phy_drd);
	phy_drd->dev = dev;

	drv_data = of_device_get_match_data(dev);
	if (!drv_data)
		return -EINVAL;
	phy_drd->drv_data = drv_data;

	if (of_property_present(dev->of_node, "reg-names")) {
		void __iomem *reg;

		reg = devm_platform_ioremap_resource_byname(pdev, "phy");
		if (IS_ERR(reg))
			return PTR_ERR(reg);
		phy_drd->reg_phy = reg;

		reg = devm_platform_ioremap_resource_byname(pdev, "pcs");
		if (IS_ERR(reg))
			return PTR_ERR(reg);
		phy_drd->reg_pcs = reg;

		reg = devm_platform_ioremap_resource_byname(pdev, "pma");
		if (IS_ERR(reg))
			return PTR_ERR(reg);
		phy_drd->reg_pma = reg;
	} else {
		/* DTB with just a single region */
		phy_drd->reg_phy = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(phy_drd->reg_phy))
			return PTR_ERR(phy_drd->reg_phy);
	}

	ret = exynos5_usbdrd_phy_clk_handle(phy_drd);
	if (ret)
		return ret;

	reg_pmu = syscon_regmap_lookup_by_phandle(dev->of_node,
						   "samsung,pmu-syscon");
	if (IS_ERR(reg_pmu)) {
		dev_err(dev, "Failed to lookup PMU regmap\n");
		return PTR_ERR(reg_pmu);
	}

	/*
	 * Exynos5420 SoC has multiple channels for USB 3.0 PHY, with
	 * each having separate power control registers.
	 * 'channel' facilitates to set such registers.
	 */
	channel = of_alias_get_id(node, "usbdrdphy");
	if (channel < 0)
		dev_dbg(dev, "Not a multi-controller usbdrd phy\n");

	/* Get regulators */
	phy_drd->regulators = devm_kcalloc(dev,
					   drv_data->n_regulators,
					   sizeof(*phy_drd->regulators),
					   GFP_KERNEL);
	if (!phy_drd->regulators)
		return -ENOMEM;
	regulator_bulk_set_supply_names(phy_drd->regulators,
					drv_data->regulator_names,
					drv_data->n_regulators);
	ret = devm_regulator_bulk_get(dev, drv_data->n_regulators,
				      phy_drd->regulators);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	dev_vdbg(dev, "Creating usbdrd_phy phy\n");

	for (i = 0; i < EXYNOS5_DRDPHYS_NUM; i++) {
		struct phy *phy = devm_phy_create(dev, NULL, drv_data->phy_ops);

		if (IS_ERR(phy)) {
			dev_err(dev, "Failed to create usbdrd_phy phy\n");
			return PTR_ERR(phy);
		}

		phy_drd->phys[i].phy = phy;
		phy_drd->phys[i].index = i;
		phy_drd->phys[i].reg_pmu = reg_pmu;
		switch (channel) {
		case 1:
			pmu_offset = drv_data->pmu_offset_usbdrd1_phy;
			break;
		case 0:
		default:
			pmu_offset = drv_data->pmu_offset_usbdrd0_phy;
			if (i == EXYNOS5_DRDPHY_PIPE3 && drv_data
						->pmu_offset_usbdrd0_phy_ss)
				pmu_offset = drv_data->pmu_offset_usbdrd0_phy_ss;
			break;
		}
		phy_drd->phys[i].pmu_offset = pmu_offset;
		phy_drd->phys[i].phy_cfg = &drv_data->phy_cfg[i];
		phy_set_drvdata(phy, &phy_drd->phys[i]);
	}

	phy_provider = devm_of_phy_provider_register(dev,
						     exynos5_usbdrd_phy_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(phy_drd->dev, "Failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	return 0;
}

static struct platform_driver exynos5_usb3drd_phy = {
	.probe	= exynos5_usbdrd_phy_probe,
	.driver = {
		.of_match_table	= exynos5_usbdrd_phy_of_match,
		.name		= "exynos5_usb3drd_phy",
		.suppress_bind_attrs = true,
	}
};

module_platform_driver(exynos5_usb3drd_phy);
MODULE_DESCRIPTION("Samsung Exynos5 SoCs USB 3.0 DRD controller PHY driver");
MODULE_AUTHOR("Vivek Gautam <gautam.vivek@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:exynos5_usb3drd_phy");
