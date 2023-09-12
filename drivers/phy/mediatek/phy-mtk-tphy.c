// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 *
 */

#include <dt-bindings/phy/phy.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "phy-mtk-io.h"

/* version V1 sub-banks offset base address */
/* banks shared by multiple phys */
#define SSUSB_SIFSLV_V1_SPLLC		0x000	/* shared by u3 phys */
#define SSUSB_SIFSLV_V1_U2FREQ		0x100	/* shared by u2 phys */
#define SSUSB_SIFSLV_V1_CHIP		0x300	/* shared by u3 phys */
/* u2 phy bank */
#define SSUSB_SIFSLV_V1_U2PHY_COM	0x000
/* u3/pcie/sata phy banks */
#define SSUSB_SIFSLV_V1_U3PHYD		0x000
#define SSUSB_SIFSLV_V1_U3PHYA		0x200

/* version V2/V3 sub-banks offset base address */
/* V3: U2FREQ is not used anymore, but reserved */
/* u2 phy banks */
#define SSUSB_SIFSLV_V2_MISC		0x000
#define SSUSB_SIFSLV_V2_U2FREQ		0x100
#define SSUSB_SIFSLV_V2_U2PHY_COM	0x300
/* u3/pcie/sata phy banks */
#define SSUSB_SIFSLV_V2_SPLLC		0x000
#define SSUSB_SIFSLV_V2_CHIP		0x100
#define SSUSB_SIFSLV_V2_U3PHYD		0x200
#define SSUSB_SIFSLV_V2_U3PHYA		0x400

#define U3P_MISC_REG1		0x04
#define MR1_EFUSE_AUTO_LOAD_DIS		BIT(6)

#define U3P_USBPHYACR0		0x000
#define PA0_RG_U2PLL_FORCE_ON		BIT(15)
#define PA0_USB20_PLL_PREDIV		GENMASK(7, 6)
#define PA0_RG_USB20_INTR_EN		BIT(5)

#define U3P_USBPHYACR1		0x004
#define PA1_RG_INTR_CAL		GENMASK(23, 19)
#define PA1_RG_VRT_SEL			GENMASK(14, 12)
#define PA1_RG_TERM_SEL		GENMASK(10, 8)

#define U3P_USBPHYACR2		0x008
#define PA2_RG_U2PLL_BW			GENMASK(21, 19)
#define PA2_RG_SIF_U2PLL_FORCE_EN	BIT(18)

#define U3P_USBPHYACR5		0x014
#define PA5_RG_U2_HSTX_SRCAL_EN	BIT(15)
#define PA5_RG_U2_HSTX_SRCTRL		GENMASK(14, 12)
#define PA5_RG_U2_HS_100U_U3_EN	BIT(11)

#define U3P_USBPHYACR6		0x018
#define PA6_RG_U2_PRE_EMP		GENMASK(31, 30)
#define PA6_RG_U2_BC11_SW_EN		BIT(23)
#define PA6_RG_U2_OTG_VBUSCMP_EN	BIT(20)
#define PA6_RG_U2_DISCTH		GENMASK(7, 4)
#define PA6_RG_U2_SQTH		GENMASK(3, 0)

#define U3P_U2PHYACR4		0x020
#define P2C_RG_USB20_GPIO_CTL		BIT(9)
#define P2C_USB20_GPIO_MODE		BIT(8)
#define P2C_U2_GPIO_CTR_MSK	(P2C_RG_USB20_GPIO_CTL | P2C_USB20_GPIO_MODE)

#define U3P_U2PHYA_RESV		0x030
#define P2R_RG_U2PLL_FBDIV_26M		0x1bb13b
#define P2R_RG_U2PLL_FBDIV_48M		0x3c0000

#define U3P_U2PHYA_RESV1	0x044
#define P2R_RG_U2PLL_REFCLK_SEL	BIT(5)
#define P2R_RG_U2PLL_FRA_EN		BIT(3)

#define U3D_U2PHYDCR0		0x060
#define P2C_RG_SIF_U2PLL_FORCE_ON	BIT(24)

#define U3P_U2PHYDTM0		0x068
#define P2C_FORCE_UART_EN		BIT(26)
#define P2C_FORCE_DATAIN		BIT(23)
#define P2C_FORCE_DM_PULLDOWN		BIT(21)
#define P2C_FORCE_DP_PULLDOWN		BIT(20)
#define P2C_FORCE_XCVRSEL		BIT(19)
#define P2C_FORCE_SUSPENDM		BIT(18)
#define P2C_FORCE_TERMSEL		BIT(17)
#define P2C_RG_DATAIN			GENMASK(13, 10)
#define P2C_RG_DMPULLDOWN		BIT(7)
#define P2C_RG_DPPULLDOWN		BIT(6)
#define P2C_RG_XCVRSEL			GENMASK(5, 4)
#define P2C_RG_SUSPENDM			BIT(3)
#define P2C_RG_TERMSEL			BIT(2)
#define P2C_DTM0_PART_MASK \
		(P2C_FORCE_DATAIN | P2C_FORCE_DM_PULLDOWN | \
		P2C_FORCE_DP_PULLDOWN | P2C_FORCE_XCVRSEL | \
		P2C_FORCE_TERMSEL | P2C_RG_DMPULLDOWN | \
		P2C_RG_DPPULLDOWN | P2C_RG_TERMSEL)

#define U3P_U2PHYDTM1		0x06C
#define P2C_RG_UART_EN			BIT(16)
#define P2C_FORCE_IDDIG		BIT(9)
#define P2C_RG_VBUSVALID		BIT(5)
#define P2C_RG_SESSEND			BIT(4)
#define P2C_RG_AVALID			BIT(2)
#define P2C_RG_IDDIG			BIT(1)

#define U3P_U2PHYBC12C		0x080
#define P2C_RG_CHGDT_EN		BIT(0)

#define U3P_U3_CHIP_GPIO_CTLD		0x0c
#define P3C_REG_IP_SW_RST		BIT(31)
#define P3C_MCU_BUS_CK_GATE_EN		BIT(30)
#define P3C_FORCE_IP_SW_RST		BIT(29)

#define U3P_U3_CHIP_GPIO_CTLE		0x10
#define P3C_RG_SWRST_U3_PHYD		BIT(25)
#define P3C_RG_SWRST_U3_PHYD_FORCE_EN	BIT(24)

#define U3P_U3_PHYA_REG0	0x000
#define P3A_RG_IEXT_INTR		GENMASK(15, 10)
#define P3A_RG_CLKDRV_OFF		GENMASK(3, 2)

#define U3P_U3_PHYA_REG1	0x004
#define P3A_RG_CLKDRV_AMP		GENMASK(31, 29)

#define U3P_U3_PHYA_REG6	0x018
#define P3A_RG_TX_EIDLE_CM		GENMASK(31, 28)

#define U3P_U3_PHYA_REG9	0x024
#define P3A_RG_RX_DAC_MUX		GENMASK(5, 1)

#define U3P_U3_PHYA_DA_REG0	0x100
#define P3A_RG_XTAL_EXT_PE2H		GENMASK(17, 16)
#define P3A_RG_XTAL_EXT_PE1H		GENMASK(13, 12)
#define P3A_RG_XTAL_EXT_EN_U3		GENMASK(11, 10)

#define U3P_U3_PHYA_DA_REG4	0x108
#define P3A_RG_PLL_DIVEN_PE2H		GENMASK(21, 19)
#define P3A_RG_PLL_BC_PE2H		GENMASK(7, 6)

#define U3P_U3_PHYA_DA_REG5	0x10c
#define P3A_RG_PLL_BR_PE2H		GENMASK(29, 28)
#define P3A_RG_PLL_IC_PE2H		GENMASK(15, 12)

#define U3P_U3_PHYA_DA_REG6	0x110
#define P3A_RG_PLL_IR_PE2H		GENMASK(19, 16)

#define U3P_U3_PHYA_DA_REG7	0x114
#define P3A_RG_PLL_BP_PE2H		GENMASK(19, 16)

#define U3P_U3_PHYA_DA_REG20	0x13c
#define P3A_RG_PLL_DELTA1_PE2H		GENMASK(31, 16)

#define U3P_U3_PHYA_DA_REG25	0x148
#define P3A_RG_PLL_DELTA_PE2H		GENMASK(15, 0)

#define U3P_U3_PHYD_LFPS1		0x00c
#define P3D_RG_FWAKE_TH		GENMASK(21, 16)

#define U3P_U3_PHYD_IMPCAL0		0x010
#define P3D_RG_FORCE_TX_IMPEL		BIT(31)
#define P3D_RG_TX_IMPEL			GENMASK(28, 24)

#define U3P_U3_PHYD_IMPCAL1		0x014
#define P3D_RG_FORCE_RX_IMPEL		BIT(31)
#define P3D_RG_RX_IMPEL			GENMASK(28, 24)

#define U3P_U3_PHYD_RSV			0x054
#define P3D_RG_EFUSE_AUTO_LOAD_DIS	BIT(12)

#define U3P_U3_PHYD_CDR1		0x05c
#define P3D_RG_CDR_BIR_LTD1		GENMASK(28, 24)
#define P3D_RG_CDR_BIR_LTD0		GENMASK(12, 8)

#define U3P_U3_PHYD_RXDET1		0x128
#define P3D_RG_RXDET_STB2_SET		GENMASK(17, 9)

#define U3P_U3_PHYD_RXDET2		0x12c
#define P3D_RG_RXDET_STB2_SET_P3	GENMASK(8, 0)

#define U3P_SPLLC_XTALCTL3		0x018
#define XC3_RG_U3_XTAL_RX_PWD		BIT(9)
#define XC3_RG_U3_FRC_XTAL_RX_PWD	BIT(8)

#define U3P_U2FREQ_FMCR0	0x00
#define P2F_RG_MONCLK_SEL	GENMASK(27, 26)
#define P2F_RG_FREQDET_EN	BIT(24)
#define P2F_RG_CYCLECNT		GENMASK(23, 0)

#define U3P_U2FREQ_VALUE	0x0c

#define U3P_U2FREQ_FMMONR1	0x10
#define P2F_USB_FM_VALID	BIT(0)
#define P2F_RG_FRCK_EN		BIT(8)

#define U3P_REF_CLK		26	/* MHZ */
#define U3P_SLEW_RATE_COEF	28
#define U3P_SR_COEF_DIVISOR	1000
#define U3P_FM_DET_CYCLE_CNT	1024

/* SATA register setting */
#define PHYD_CTRL_SIGNAL_MODE4		0x1c
/* CDR Charge Pump P-path current adjustment */
#define RG_CDR_BICLTD1_GEN1_MSK		GENMASK(23, 20)
#define RG_CDR_BICLTD0_GEN1_MSK		GENMASK(11, 8)

#define PHYD_DESIGN_OPTION2		0x24
/* Symbol lock count selection */
#define RG_LOCK_CNT_SEL_MSK		GENMASK(5, 4)

#define PHYD_DESIGN_OPTION9	0x40
/* COMWAK GAP width window */
#define RG_TG_MAX_MSK		GENMASK(20, 16)
/* COMINIT GAP width window */
#define RG_T2_MAX_MSK		GENMASK(13, 8)
/* COMWAK GAP width window */
#define RG_TG_MIN_MSK		GENMASK(7, 5)
/* COMINIT GAP width window */
#define RG_T2_MIN_MSK		GENMASK(4, 0)

#define ANA_RG_CTRL_SIGNAL1		0x4c
/* TX driver tail current control for 0dB de-empahsis mdoe for Gen1 speed */
#define RG_IDRV_0DB_GEN1_MSK		GENMASK(13, 8)

#define ANA_RG_CTRL_SIGNAL4		0x58
#define RG_CDR_BICLTR_GEN1_MSK		GENMASK(23, 20)
/* Loop filter R1 resistance adjustment for Gen1 speed */
#define RG_CDR_BR_GEN2_MSK		GENMASK(10, 8)

#define ANA_RG_CTRL_SIGNAL6		0x60
/* I-path capacitance adjustment for Gen1 */
#define RG_CDR_BC_GEN1_MSK		GENMASK(28, 24)
#define RG_CDR_BIRLTR_GEN1_MSK		GENMASK(4, 0)

#define ANA_EQ_EYE_CTRL_SIGNAL1		0x6c
/* RX Gen1 LEQ tuning step */
#define RG_EQ_DLEQ_LFI_GEN1_MSK		GENMASK(11, 8)

#define ANA_EQ_EYE_CTRL_SIGNAL4		0xd8
#define RG_CDR_BIRLTD0_GEN1_MSK		GENMASK(20, 16)

#define ANA_EQ_EYE_CTRL_SIGNAL5		0xdc
#define RG_CDR_BIRLTD0_GEN3_MSK		GENMASK(4, 0)

/* PHY switch between pcie/usb3/sgmii/sata */
#define USB_PHY_SWITCH_CTRL	0x0
#define RG_PHY_SW_TYPE		GENMASK(3, 0)
#define RG_PHY_SW_PCIE		0x0
#define RG_PHY_SW_USB3		0x1
#define RG_PHY_SW_SGMII		0x2
#define RG_PHY_SW_SATA		0x3

#define TPHY_CLKS_CNT	2

#define USER_BUF_LEN(count) min_t(size_t, 8, (count))

enum mtk_phy_version {
	MTK_PHY_V1 = 1,
	MTK_PHY_V2,
	MTK_PHY_V3,
};

struct mtk_phy_pdata {
	/* avoid RX sensitivity level degradation only for mt8173 */
	bool avoid_rx_sen_degradation;
	/*
	 * workaround only for mt8195, HW fix it for others of V3,
	 * u2phy should use integer mode instead of fractional mode of
	 * 48M PLL, fix it by switching PLL to 26M from default 48M
	 */
	bool sw_pll_48m_to_26m;
	/*
	 * Some SoCs (e.g. mt8195) drop a bit when use auto load efuse,
	 * support sw way, also support it for v2/v3 optionally.
	 */
	bool sw_efuse_supported;
	enum mtk_phy_version version;
};

struct u2phy_banks {
	void __iomem *misc;
	void __iomem *fmreg;
	void __iomem *com;
};

struct u3phy_banks {
	void __iomem *spllc;
	void __iomem *chip;
	void __iomem *phyd; /* include u3phyd_bank2 */
	void __iomem *phya; /* include u3phya_da */
};

struct mtk_phy_instance {
	struct phy *phy;
	void __iomem *port_base;
	union {
		struct u2phy_banks u2_banks;
		struct u3phy_banks u3_banks;
	};
	struct clk_bulk_data clks[TPHY_CLKS_CNT];
	u32 index;
	u32 type;
	struct regmap *type_sw;
	u32 type_sw_reg;
	u32 type_sw_index;
	u32 efuse_sw_en;
	u32 efuse_intr;
	u32 efuse_tx_imp;
	u32 efuse_rx_imp;
	int eye_src;
	int eye_vrt;
	int eye_term;
	int intr;
	int discth;
	int pre_emphasis;
	bool bc12_en;
};

struct mtk_tphy {
	struct device *dev;
	void __iomem *sif_base;	/* only shared sif */
	const struct mtk_phy_pdata *pdata;
	struct mtk_phy_instance **phys;
	int nphys;
	int src_ref_clk; /* MHZ, reference clock for slew rate calibrate */
	int src_coef; /* coefficient for slew rate calibrate */
};

#if IS_ENABLED(CONFIG_DEBUG_FS)

enum u2_phy_params {
	U2P_EYE_VRT = 0,
	U2P_EYE_TERM,
	U2P_EFUSE_EN,
	U2P_EFUSE_INTR,
	U2P_DISCTH,
	U2P_PRE_EMPHASIS,
};

enum u3_phy_params {
	U3P_EFUSE_EN = 0,
	U3P_EFUSE_INTR,
	U3P_EFUSE_TX_IMP,
	U3P_EFUSE_RX_IMP,
};

static const char *const u2_phy_files[] = {
	[U2P_EYE_VRT] = "vrt",
	[U2P_EYE_TERM] = "term",
	[U2P_EFUSE_EN] = "efuse",
	[U2P_EFUSE_INTR] = "intr",
	[U2P_DISCTH] = "discth",
	[U2P_PRE_EMPHASIS] = "preemph",
};

static const char *const u3_phy_files[] = {
	[U3P_EFUSE_EN] = "efuse",
	[U3P_EFUSE_INTR] = "intr",
	[U3P_EFUSE_TX_IMP] = "tx-imp",
	[U3P_EFUSE_RX_IMP] = "rx-imp",
};

static int u2_phy_params_show(struct seq_file *sf, void *unused)
{
	struct mtk_phy_instance *inst = sf->private;
	const char *fname = file_dentry(sf->file)->d_iname;
	struct u2phy_banks *u2_banks = &inst->u2_banks;
	void __iomem *com = u2_banks->com;
	u32 max = 0;
	u32 tmp = 0;
	u32 val = 0;
	int ret;

	ret = match_string(u2_phy_files, ARRAY_SIZE(u2_phy_files), fname);
	if (ret < 0)
		return ret;

	switch (ret) {
	case U2P_EYE_VRT:
		tmp = readl(com + U3P_USBPHYACR1);
		val = FIELD_GET(PA1_RG_VRT_SEL, tmp);
		max = FIELD_MAX(PA1_RG_VRT_SEL);
		break;

	case U2P_EYE_TERM:
		tmp = readl(com + U3P_USBPHYACR1);
		val = FIELD_GET(PA1_RG_TERM_SEL, tmp);
		max = FIELD_MAX(PA1_RG_TERM_SEL);
		break;

	case U2P_EFUSE_EN:
		if (u2_banks->misc) {
			tmp = readl(u2_banks->misc + U3P_MISC_REG1);
			max = 1;
		}

		val = !!(tmp & MR1_EFUSE_AUTO_LOAD_DIS);
		break;

	case U2P_EFUSE_INTR:
		tmp = readl(com + U3P_USBPHYACR1);
		val = FIELD_GET(PA1_RG_INTR_CAL, tmp);
		max = FIELD_MAX(PA1_RG_INTR_CAL);
		break;

	case U2P_DISCTH:
		tmp = readl(com + U3P_USBPHYACR6);
		val = FIELD_GET(PA6_RG_U2_DISCTH, tmp);
		max = FIELD_MAX(PA6_RG_U2_DISCTH);
		break;

	case U2P_PRE_EMPHASIS:
		tmp = readl(com + U3P_USBPHYACR6);
		val = FIELD_GET(PA6_RG_U2_PRE_EMP, tmp);
		max = FIELD_MAX(PA6_RG_U2_PRE_EMP);
		break;

	default:
		seq_printf(sf, "invalid, %d\n", ret);
		break;
	}

	seq_printf(sf, "%s : %d [0, %d]\n", fname, val, max);

	return 0;
}

static int u2_phy_params_open(struct inode *inode, struct file *file)
{
	return single_open(file, u2_phy_params_show, inode->i_private);
}

static ssize_t u2_phy_params_write(struct file *file, const char __user *ubuf,
				   size_t count, loff_t *ppos)
{
	const char *fname = file_dentry(file)->d_iname;
	struct seq_file *sf = file->private_data;
	struct mtk_phy_instance *inst = sf->private;
	struct u2phy_banks *u2_banks = &inst->u2_banks;
	void __iomem *com = u2_banks->com;
	ssize_t rc;
	u32 val;
	int ret;

	rc = kstrtouint_from_user(ubuf, USER_BUF_LEN(count), 0, &val);
	if (rc)
		return rc;

	ret = match_string(u2_phy_files, ARRAY_SIZE(u2_phy_files), fname);
	if (ret < 0)
		return (ssize_t)ret;

	switch (ret) {
	case U2P_EYE_VRT:
		mtk_phy_update_field(com + U3P_USBPHYACR1, PA1_RG_VRT_SEL, val);
		break;

	case U2P_EYE_TERM:
		mtk_phy_update_field(com + U3P_USBPHYACR1, PA1_RG_TERM_SEL, val);
		break;

	case U2P_EFUSE_EN:
		if (u2_banks->misc)
			mtk_phy_update_field(u2_banks->misc + U3P_MISC_REG1,
					     MR1_EFUSE_AUTO_LOAD_DIS, !!val);
		break;

	case U2P_EFUSE_INTR:
		mtk_phy_update_field(com + U3P_USBPHYACR1, PA1_RG_INTR_CAL, val);
		break;

	case U2P_DISCTH:
		mtk_phy_update_field(com + U3P_USBPHYACR6, PA6_RG_U2_DISCTH, val);
		break;

	case U2P_PRE_EMPHASIS:
		mtk_phy_update_field(com + U3P_USBPHYACR6, PA6_RG_U2_PRE_EMP, val);
		break;

	default:
		break;
	}

	return count;
}

static const struct file_operations u2_phy_fops = {
	.open = u2_phy_params_open,
	.write = u2_phy_params_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void u2_phy_dbgfs_files_create(struct mtk_phy_instance *inst)
{
	u32 count = ARRAY_SIZE(u2_phy_files);
	int i;

	for (i = 0; i < count; i++)
		debugfs_create_file(u2_phy_files[i], 0644, inst->phy->debugfs,
				    inst, &u2_phy_fops);
}

static int u3_phy_params_show(struct seq_file *sf, void *unused)
{
	struct mtk_phy_instance *inst = sf->private;
	const char *fname = file_dentry(sf->file)->d_iname;
	struct u3phy_banks *u3_banks = &inst->u3_banks;
	u32 val = 0;
	u32 max = 0;
	u32 tmp;
	int ret;

	ret = match_string(u3_phy_files, ARRAY_SIZE(u3_phy_files), fname);
	if (ret < 0)
		return ret;

	switch (ret) {
	case U3P_EFUSE_EN:
		tmp = readl(u3_banks->phyd + U3P_U3_PHYD_RSV);
		val = !!(tmp & P3D_RG_EFUSE_AUTO_LOAD_DIS);
		max = 1;
		break;

	case U3P_EFUSE_INTR:
		tmp = readl(u3_banks->phya + U3P_U3_PHYA_REG0);
		val = FIELD_GET(P3A_RG_IEXT_INTR, tmp);
		max = FIELD_MAX(P3A_RG_IEXT_INTR);
		break;

	case U3P_EFUSE_TX_IMP:
		tmp = readl(u3_banks->phyd + U3P_U3_PHYD_IMPCAL0);
		val = FIELD_GET(P3D_RG_TX_IMPEL, tmp);
		max = FIELD_MAX(P3D_RG_TX_IMPEL);
		break;

	case U3P_EFUSE_RX_IMP:
		tmp = readl(u3_banks->phyd + U3P_U3_PHYD_IMPCAL1);
		val = FIELD_GET(P3D_RG_RX_IMPEL, tmp);
		max = FIELD_MAX(P3D_RG_RX_IMPEL);
		break;

	default:
		seq_printf(sf, "invalid, %d\n", ret);
		break;
	}

	seq_printf(sf, "%s : %d [0, %d]\n", fname, val, max);

	return 0;
}

static int u3_phy_params_open(struct inode *inode, struct file *file)
{
	return single_open(file, u3_phy_params_show, inode->i_private);
}

static ssize_t u3_phy_params_write(struct file *file, const char __user *ubuf,
				   size_t count, loff_t *ppos)
{
	const char *fname = file_dentry(file)->d_iname;
	struct seq_file *sf = file->private_data;
	struct mtk_phy_instance *inst = sf->private;
	struct u3phy_banks *u3_banks = &inst->u3_banks;
	void __iomem *phyd = u3_banks->phyd;
	ssize_t rc;
	u32 val;
	int ret;

	rc = kstrtouint_from_user(ubuf, USER_BUF_LEN(count), 0, &val);
	if (rc)
		return rc;

	ret = match_string(u3_phy_files, ARRAY_SIZE(u3_phy_files), fname);
	if (ret < 0)
		return (ssize_t)ret;

	switch (ret) {
	case U3P_EFUSE_EN:
		mtk_phy_update_field(phyd + U3P_U3_PHYD_RSV,
				     P3D_RG_EFUSE_AUTO_LOAD_DIS, !!val);
		break;

	case U3P_EFUSE_INTR:
		mtk_phy_update_field(u3_banks->phya + U3P_U3_PHYA_REG0,
				     P3A_RG_IEXT_INTR, val);
		break;

	case U3P_EFUSE_TX_IMP:
		mtk_phy_update_field(phyd + U3P_U3_PHYD_IMPCAL0, P3D_RG_TX_IMPEL, val);
		mtk_phy_set_bits(phyd + U3P_U3_PHYD_IMPCAL0, P3D_RG_FORCE_TX_IMPEL);
		break;

	case U3P_EFUSE_RX_IMP:
		mtk_phy_update_field(phyd + U3P_U3_PHYD_IMPCAL1, P3D_RG_RX_IMPEL, val);
		mtk_phy_set_bits(phyd + U3P_U3_PHYD_IMPCAL1, P3D_RG_FORCE_RX_IMPEL);
		break;

	default:
		break;
	}

	return count;
}

static const struct file_operations u3_phy_fops = {
	.open = u3_phy_params_open,
	.write = u3_phy_params_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void u3_phy_dbgfs_files_create(struct mtk_phy_instance *inst)
{
	u32 count = ARRAY_SIZE(u3_phy_files);
	int i;

	for (i = 0; i < count; i++)
		debugfs_create_file(u3_phy_files[i], 0644, inst->phy->debugfs,
				    inst, &u3_phy_fops);
}

static int phy_type_show(struct seq_file *sf, void *unused)
{
	struct mtk_phy_instance *inst = sf->private;
	const char *type;

	switch (inst->type) {
	case PHY_TYPE_USB2:
		type = "USB2";
		break;
	case PHY_TYPE_USB3:
		type = "USB3";
		break;
	case PHY_TYPE_PCIE:
		type = "PCIe";
		break;
	case PHY_TYPE_SGMII:
		type = "SGMII";
		break;
	case PHY_TYPE_SATA:
		type = "SATA";
		break;
	default:
		type = "";
	}

	seq_printf(sf, "%s\n", type);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(phy_type);

/* these files will be removed when phy is released by phy core */
static void phy_debugfs_init(struct mtk_phy_instance *inst)
{
	debugfs_create_file("type", 0444, inst->phy->debugfs, inst, &phy_type_fops);

	switch (inst->type) {
	case PHY_TYPE_USB2:
		u2_phy_dbgfs_files_create(inst);
		break;
	case PHY_TYPE_USB3:
	case PHY_TYPE_PCIE:
		u3_phy_dbgfs_files_create(inst);
		break;
	default:
		break;
	}
}

#else

static void phy_debugfs_init(struct mtk_phy_instance *inst)
{}

#endif

static void hs_slew_rate_calibrate(struct mtk_tphy *tphy,
	struct mtk_phy_instance *instance)
{
	struct u2phy_banks *u2_banks = &instance->u2_banks;
	void __iomem *fmreg = u2_banks->fmreg;
	void __iomem *com = u2_banks->com;
	int calibration_val;
	int fm_out;
	u32 tmp;

	/* HW V3 doesn't support slew rate cal anymore */
	if (tphy->pdata->version == MTK_PHY_V3)
		return;

	/* use force value */
	if (instance->eye_src)
		return;

	/* enable USB ring oscillator */
	mtk_phy_set_bits(com + U3P_USBPHYACR5, PA5_RG_U2_HSTX_SRCAL_EN);
	udelay(1);

	/*enable free run clock */
	mtk_phy_set_bits(fmreg + U3P_U2FREQ_FMMONR1, P2F_RG_FRCK_EN);

	/* set cycle count as 1024, and select u2 channel */
	tmp = readl(fmreg + U3P_U2FREQ_FMCR0);
	tmp &= ~(P2F_RG_CYCLECNT | P2F_RG_MONCLK_SEL);
	tmp |= FIELD_PREP(P2F_RG_CYCLECNT, U3P_FM_DET_CYCLE_CNT);
	if (tphy->pdata->version == MTK_PHY_V1)
		tmp |= FIELD_PREP(P2F_RG_MONCLK_SEL, instance->index >> 1);

	writel(tmp, fmreg + U3P_U2FREQ_FMCR0);

	/* enable frequency meter */
	mtk_phy_set_bits(fmreg + U3P_U2FREQ_FMCR0, P2F_RG_FREQDET_EN);

	/* ignore return value */
	readl_poll_timeout(fmreg + U3P_U2FREQ_FMMONR1, tmp,
			   (tmp & P2F_USB_FM_VALID), 10, 200);

	fm_out = readl(fmreg + U3P_U2FREQ_VALUE);

	/* disable frequency meter */
	mtk_phy_clear_bits(fmreg + U3P_U2FREQ_FMCR0, P2F_RG_FREQDET_EN);

	/*disable free run clock */
	mtk_phy_clear_bits(fmreg + U3P_U2FREQ_FMMONR1, P2F_RG_FRCK_EN);

	if (fm_out) {
		/* ( 1024 / FM_OUT ) x reference clock frequency x coef */
		tmp = tphy->src_ref_clk * tphy->src_coef;
		tmp = (tmp * U3P_FM_DET_CYCLE_CNT) / fm_out;
		calibration_val = DIV_ROUND_CLOSEST(tmp, U3P_SR_COEF_DIVISOR);
	} else {
		/* if FM detection fail, set default value */
		calibration_val = 4;
	}
	dev_dbg(tphy->dev, "phy:%d, fm_out:%d, calib:%d (clk:%d, coef:%d)\n",
		instance->index, fm_out, calibration_val,
		tphy->src_ref_clk, tphy->src_coef);

	/* set HS slew rate */
	mtk_phy_update_field(com + U3P_USBPHYACR5, PA5_RG_U2_HSTX_SRCTRL,
			     calibration_val);

	/* disable USB ring oscillator */
	mtk_phy_clear_bits(com + U3P_USBPHYACR5, PA5_RG_U2_HSTX_SRCAL_EN);
}

static void u3_phy_instance_init(struct mtk_tphy *tphy,
	struct mtk_phy_instance *instance)
{
	struct u3phy_banks *u3_banks = &instance->u3_banks;
	void __iomem *phya = u3_banks->phya;
	void __iomem *phyd = u3_banks->phyd;

	/* gating PCIe Analog XTAL clock */
	mtk_phy_set_bits(u3_banks->spllc + U3P_SPLLC_XTALCTL3,
			 XC3_RG_U3_XTAL_RX_PWD | XC3_RG_U3_FRC_XTAL_RX_PWD);

	/* gating XSQ */
	mtk_phy_update_field(phya + U3P_U3_PHYA_DA_REG0, P3A_RG_XTAL_EXT_EN_U3, 2);

	mtk_phy_update_field(phya + U3P_U3_PHYA_REG9, P3A_RG_RX_DAC_MUX, 4);

	mtk_phy_update_field(phya + U3P_U3_PHYA_REG6, P3A_RG_TX_EIDLE_CM, 0xe);

	mtk_phy_update_bits(u3_banks->phyd + U3P_U3_PHYD_CDR1,
			    P3D_RG_CDR_BIR_LTD0 | P3D_RG_CDR_BIR_LTD1,
			    FIELD_PREP(P3D_RG_CDR_BIR_LTD0, 0xc) |
			    FIELD_PREP(P3D_RG_CDR_BIR_LTD1, 0x3));

	mtk_phy_update_field(phyd + U3P_U3_PHYD_LFPS1, P3D_RG_FWAKE_TH, 0x34);

	mtk_phy_update_field(phyd + U3P_U3_PHYD_RXDET1, P3D_RG_RXDET_STB2_SET, 0x10);

	mtk_phy_update_field(phyd + U3P_U3_PHYD_RXDET2, P3D_RG_RXDET_STB2_SET_P3, 0x10);

	dev_dbg(tphy->dev, "%s(%d)\n", __func__, instance->index);
}

static void u2_phy_pll_26m_set(struct mtk_tphy *tphy,
	struct mtk_phy_instance *instance)
{
	struct u2phy_banks *u2_banks = &instance->u2_banks;
	void __iomem *com = u2_banks->com;

	if (!tphy->pdata->sw_pll_48m_to_26m)
		return;

	mtk_phy_update_field(com + U3P_USBPHYACR0, PA0_USB20_PLL_PREDIV, 0);

	mtk_phy_update_field(com + U3P_USBPHYACR2, PA2_RG_U2PLL_BW, 3);

	writel(P2R_RG_U2PLL_FBDIV_26M, com + U3P_U2PHYA_RESV);

	mtk_phy_set_bits(com + U3P_U2PHYA_RESV1,
			 P2R_RG_U2PLL_FRA_EN | P2R_RG_U2PLL_REFCLK_SEL);
}

static void u2_phy_instance_init(struct mtk_tphy *tphy,
	struct mtk_phy_instance *instance)
{
	struct u2phy_banks *u2_banks = &instance->u2_banks;
	void __iomem *com = u2_banks->com;
	u32 index = instance->index;

	/* switch to USB function, and enable usb pll */
	mtk_phy_clear_bits(com + U3P_U2PHYDTM0, P2C_FORCE_UART_EN | P2C_FORCE_SUSPENDM);

	mtk_phy_clear_bits(com + U3P_U2PHYDTM0,
			   P2C_RG_XCVRSEL | P2C_RG_DATAIN | P2C_DTM0_PART_MASK);

	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_UART_EN);

	mtk_phy_set_bits(com + U3P_USBPHYACR0, PA0_RG_USB20_INTR_EN);

	/* disable switch 100uA current to SSUSB */
	mtk_phy_clear_bits(com + U3P_USBPHYACR5, PA5_RG_U2_HS_100U_U3_EN);

	mtk_phy_clear_bits(com + U3P_U2PHYACR4, P2C_U2_GPIO_CTR_MSK);

	if (tphy->pdata->avoid_rx_sen_degradation) {
		if (!index) {
			mtk_phy_set_bits(com + U3P_USBPHYACR2, PA2_RG_SIF_U2PLL_FORCE_EN);

			mtk_phy_clear_bits(com + U3D_U2PHYDCR0, P2C_RG_SIF_U2PLL_FORCE_ON);
		} else {
			mtk_phy_set_bits(com + U3D_U2PHYDCR0, P2C_RG_SIF_U2PLL_FORCE_ON);

			mtk_phy_set_bits(com + U3P_U2PHYDTM0,
					 P2C_RG_SUSPENDM | P2C_FORCE_SUSPENDM);
		}
	}

	/* DP/DM BC1.1 path Disable */
	mtk_phy_clear_bits(com + U3P_USBPHYACR6, PA6_RG_U2_BC11_SW_EN);

	mtk_phy_update_field(com + U3P_USBPHYACR6, PA6_RG_U2_SQTH, 2);

	/* Workaround only for mt8195, HW fix it for others (V3) */
	u2_phy_pll_26m_set(tphy, instance);

	dev_dbg(tphy->dev, "%s(%d)\n", __func__, index);
}

static void u2_phy_instance_power_on(struct mtk_tphy *tphy,
	struct mtk_phy_instance *instance)
{
	struct u2phy_banks *u2_banks = &instance->u2_banks;
	void __iomem *com = u2_banks->com;
	u32 index = instance->index;

	/* OTG Enable */
	mtk_phy_set_bits(com + U3P_USBPHYACR6, PA6_RG_U2_OTG_VBUSCMP_EN);

	mtk_phy_set_bits(com + U3P_U2PHYDTM1, P2C_RG_VBUSVALID | P2C_RG_AVALID);

	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_SESSEND);

	if (tphy->pdata->avoid_rx_sen_degradation && index) {
		mtk_phy_set_bits(com + U3D_U2PHYDCR0, P2C_RG_SIF_U2PLL_FORCE_ON);

		mtk_phy_set_bits(com + U3P_U2PHYDTM0, P2C_RG_SUSPENDM | P2C_FORCE_SUSPENDM);
	}
	dev_dbg(tphy->dev, "%s(%d)\n", __func__, index);
}

static void u2_phy_instance_power_off(struct mtk_tphy *tphy,
	struct mtk_phy_instance *instance)
{
	struct u2phy_banks *u2_banks = &instance->u2_banks;
	void __iomem *com = u2_banks->com;
	u32 index = instance->index;

	/* OTG Disable */
	mtk_phy_clear_bits(com + U3P_USBPHYACR6, PA6_RG_U2_OTG_VBUSCMP_EN);

	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_VBUSVALID | P2C_RG_AVALID);

	mtk_phy_set_bits(com + U3P_U2PHYDTM1, P2C_RG_SESSEND);

	if (tphy->pdata->avoid_rx_sen_degradation && index) {
		mtk_phy_clear_bits(com + U3P_U2PHYDTM0, P2C_RG_SUSPENDM | P2C_FORCE_SUSPENDM);

		mtk_phy_clear_bits(com + U3D_U2PHYDCR0, P2C_RG_SIF_U2PLL_FORCE_ON);
	}

	dev_dbg(tphy->dev, "%s(%d)\n", __func__, index);
}

static void u2_phy_instance_exit(struct mtk_tphy *tphy,
	struct mtk_phy_instance *instance)
{
	struct u2phy_banks *u2_banks = &instance->u2_banks;
	void __iomem *com = u2_banks->com;
	u32 index = instance->index;

	if (tphy->pdata->avoid_rx_sen_degradation && index) {
		mtk_phy_clear_bits(com + U3D_U2PHYDCR0, P2C_RG_SIF_U2PLL_FORCE_ON);

		mtk_phy_clear_bits(com + U3P_U2PHYDTM0, P2C_FORCE_SUSPENDM);
	}
}

static void u2_phy_instance_set_mode(struct mtk_tphy *tphy,
				     struct mtk_phy_instance *instance,
				     enum phy_mode mode)
{
	struct u2phy_banks *u2_banks = &instance->u2_banks;
	u32 tmp;

	tmp = readl(u2_banks->com + U3P_U2PHYDTM1);
	switch (mode) {
	case PHY_MODE_USB_DEVICE:
		tmp |= P2C_FORCE_IDDIG | P2C_RG_IDDIG;
		break;
	case PHY_MODE_USB_HOST:
		tmp |= P2C_FORCE_IDDIG;
		tmp &= ~P2C_RG_IDDIG;
		break;
	case PHY_MODE_USB_OTG:
		tmp &= ~(P2C_FORCE_IDDIG | P2C_RG_IDDIG);
		break;
	default:
		return;
	}
	writel(tmp, u2_banks->com + U3P_U2PHYDTM1);
}

static void pcie_phy_instance_init(struct mtk_tphy *tphy,
	struct mtk_phy_instance *instance)
{
	struct u3phy_banks *u3_banks = &instance->u3_banks;
	void __iomem *phya = u3_banks->phya;

	if (tphy->pdata->version != MTK_PHY_V1)
		return;

	mtk_phy_update_bits(phya + U3P_U3_PHYA_DA_REG0,
			    P3A_RG_XTAL_EXT_PE1H | P3A_RG_XTAL_EXT_PE2H,
			    FIELD_PREP(P3A_RG_XTAL_EXT_PE1H, 0x2) |
			    FIELD_PREP(P3A_RG_XTAL_EXT_PE2H, 0x2));

	/* ref clk drive */
	mtk_phy_update_field(phya + U3P_U3_PHYA_REG1, P3A_RG_CLKDRV_AMP, 0x4);

	mtk_phy_update_field(phya + U3P_U3_PHYA_REG0, P3A_RG_CLKDRV_OFF, 0x1);

	/* SSC delta -5000ppm */
	mtk_phy_update_field(phya + U3P_U3_PHYA_DA_REG20, P3A_RG_PLL_DELTA1_PE2H, 0x3c);

	mtk_phy_update_field(phya + U3P_U3_PHYA_DA_REG25, P3A_RG_PLL_DELTA_PE2H, 0x36);

	/* change pll BW 0.6M */
	mtk_phy_update_bits(phya + U3P_U3_PHYA_DA_REG5,
			    P3A_RG_PLL_BR_PE2H | P3A_RG_PLL_IC_PE2H,
			    FIELD_PREP(P3A_RG_PLL_BR_PE2H, 0x1) |
			    FIELD_PREP(P3A_RG_PLL_IC_PE2H, 0x1));

	mtk_phy_update_bits(phya + U3P_U3_PHYA_DA_REG4,
			    P3A_RG_PLL_DIVEN_PE2H | P3A_RG_PLL_BC_PE2H,
			    FIELD_PREP(P3A_RG_PLL_BC_PE2H, 0x3));

	mtk_phy_update_field(phya + U3P_U3_PHYA_DA_REG6, P3A_RG_PLL_IR_PE2H, 0x2);

	mtk_phy_update_field(phya + U3P_U3_PHYA_DA_REG7, P3A_RG_PLL_BP_PE2H, 0xa);

	/* Tx Detect Rx Timing: 10us -> 5us */
	mtk_phy_update_field(u3_banks->phyd + U3P_U3_PHYD_RXDET1,
			     P3D_RG_RXDET_STB2_SET, 0x10);

	mtk_phy_update_field(u3_banks->phyd + U3P_U3_PHYD_RXDET2,
			     P3D_RG_RXDET_STB2_SET_P3, 0x10);

	/* wait for PCIe subsys register to active */
	usleep_range(2500, 3000);
	dev_dbg(tphy->dev, "%s(%d)\n", __func__, instance->index);
}

static void pcie_phy_instance_power_on(struct mtk_tphy *tphy,
	struct mtk_phy_instance *instance)
{
	struct u3phy_banks *bank = &instance->u3_banks;

	mtk_phy_clear_bits(bank->chip + U3P_U3_CHIP_GPIO_CTLD,
			   P3C_FORCE_IP_SW_RST | P3C_REG_IP_SW_RST);

	mtk_phy_clear_bits(bank->chip + U3P_U3_CHIP_GPIO_CTLE,
			   P3C_RG_SWRST_U3_PHYD_FORCE_EN | P3C_RG_SWRST_U3_PHYD);
}

static void pcie_phy_instance_power_off(struct mtk_tphy *tphy,
	struct mtk_phy_instance *instance)

{
	struct u3phy_banks *bank = &instance->u3_banks;

	mtk_phy_set_bits(bank->chip + U3P_U3_CHIP_GPIO_CTLD,
			 P3C_FORCE_IP_SW_RST | P3C_REG_IP_SW_RST);

	mtk_phy_set_bits(bank->chip + U3P_U3_CHIP_GPIO_CTLE,
			 P3C_RG_SWRST_U3_PHYD_FORCE_EN | P3C_RG_SWRST_U3_PHYD);
}

static void sata_phy_instance_init(struct mtk_tphy *tphy,
	struct mtk_phy_instance *instance)
{
	struct u3phy_banks *u3_banks = &instance->u3_banks;
	void __iomem *phyd = u3_banks->phyd;

	/* charge current adjustment */
	mtk_phy_update_bits(phyd + ANA_RG_CTRL_SIGNAL6,
			    RG_CDR_BIRLTR_GEN1_MSK | RG_CDR_BC_GEN1_MSK,
			    FIELD_PREP(RG_CDR_BIRLTR_GEN1_MSK, 0x6) |
			    FIELD_PREP(RG_CDR_BC_GEN1_MSK, 0x1a));

	mtk_phy_update_field(phyd + ANA_EQ_EYE_CTRL_SIGNAL4, RG_CDR_BIRLTD0_GEN1_MSK, 0x18);

	mtk_phy_update_field(phyd + ANA_EQ_EYE_CTRL_SIGNAL5, RG_CDR_BIRLTD0_GEN3_MSK, 0x06);

	mtk_phy_update_bits(phyd + ANA_RG_CTRL_SIGNAL4,
			    RG_CDR_BICLTR_GEN1_MSK | RG_CDR_BR_GEN2_MSK,
			    FIELD_PREP(RG_CDR_BICLTR_GEN1_MSK, 0x0c) |
			    FIELD_PREP(RG_CDR_BR_GEN2_MSK, 0x07));

	mtk_phy_update_bits(phyd + PHYD_CTRL_SIGNAL_MODE4,
			    RG_CDR_BICLTD0_GEN1_MSK | RG_CDR_BICLTD1_GEN1_MSK,
			    FIELD_PREP(RG_CDR_BICLTD0_GEN1_MSK, 0x08) |
			    FIELD_PREP(RG_CDR_BICLTD1_GEN1_MSK, 0x02));

	mtk_phy_update_field(phyd + PHYD_DESIGN_OPTION2, RG_LOCK_CNT_SEL_MSK, 0x02);

	mtk_phy_update_bits(phyd + PHYD_DESIGN_OPTION9,
			    RG_T2_MIN_MSK | RG_TG_MIN_MSK,
			    FIELD_PREP(RG_T2_MIN_MSK, 0x12) |
			    FIELD_PREP(RG_TG_MIN_MSK, 0x04));

	mtk_phy_update_bits(phyd + PHYD_DESIGN_OPTION9,
			    RG_T2_MAX_MSK | RG_TG_MAX_MSK,
			    FIELD_PREP(RG_T2_MAX_MSK, 0x31) |
			    FIELD_PREP(RG_TG_MAX_MSK, 0x0e));

	mtk_phy_update_field(phyd + ANA_RG_CTRL_SIGNAL1, RG_IDRV_0DB_GEN1_MSK, 0x20);

	mtk_phy_update_field(phyd + ANA_EQ_EYE_CTRL_SIGNAL1, RG_EQ_DLEQ_LFI_GEN1_MSK, 0x03);

	dev_dbg(tphy->dev, "%s(%d)\n", __func__, instance->index);
}

static void phy_v1_banks_init(struct mtk_tphy *tphy,
			      struct mtk_phy_instance *instance)
{
	struct u2phy_banks *u2_banks = &instance->u2_banks;
	struct u3phy_banks *u3_banks = &instance->u3_banks;

	switch (instance->type) {
	case PHY_TYPE_USB2:
		u2_banks->misc = NULL;
		u2_banks->fmreg = tphy->sif_base + SSUSB_SIFSLV_V1_U2FREQ;
		u2_banks->com = instance->port_base + SSUSB_SIFSLV_V1_U2PHY_COM;
		break;
	case PHY_TYPE_USB3:
	case PHY_TYPE_PCIE:
		u3_banks->spllc = tphy->sif_base + SSUSB_SIFSLV_V1_SPLLC;
		u3_banks->chip = tphy->sif_base + SSUSB_SIFSLV_V1_CHIP;
		u3_banks->phyd = instance->port_base + SSUSB_SIFSLV_V1_U3PHYD;
		u3_banks->phya = instance->port_base + SSUSB_SIFSLV_V1_U3PHYA;
		break;
	case PHY_TYPE_SATA:
		u3_banks->phyd = instance->port_base + SSUSB_SIFSLV_V1_U3PHYD;
		break;
	default:
		dev_err(tphy->dev, "incompatible PHY type\n");
		return;
	}
}

static void phy_v2_banks_init(struct mtk_tphy *tphy,
			      struct mtk_phy_instance *instance)
{
	struct u2phy_banks *u2_banks = &instance->u2_banks;
	struct u3phy_banks *u3_banks = &instance->u3_banks;

	switch (instance->type) {
	case PHY_TYPE_USB2:
		u2_banks->misc = instance->port_base + SSUSB_SIFSLV_V2_MISC;
		u2_banks->fmreg = instance->port_base + SSUSB_SIFSLV_V2_U2FREQ;
		u2_banks->com = instance->port_base + SSUSB_SIFSLV_V2_U2PHY_COM;
		break;
	case PHY_TYPE_USB3:
	case PHY_TYPE_PCIE:
		u3_banks->spllc = instance->port_base + SSUSB_SIFSLV_V2_SPLLC;
		u3_banks->chip = instance->port_base + SSUSB_SIFSLV_V2_CHIP;
		u3_banks->phyd = instance->port_base + SSUSB_SIFSLV_V2_U3PHYD;
		u3_banks->phya = instance->port_base + SSUSB_SIFSLV_V2_U3PHYA;
		break;
	default:
		dev_err(tphy->dev, "incompatible PHY type\n");
		return;
	}
}

static void phy_parse_property(struct mtk_tphy *tphy,
				struct mtk_phy_instance *instance)
{
	struct device *dev = &instance->phy->dev;

	if (instance->type != PHY_TYPE_USB2)
		return;

	instance->bc12_en = device_property_read_bool(dev, "mediatek,bc12");
	device_property_read_u32(dev, "mediatek,eye-src",
				 &instance->eye_src);
	device_property_read_u32(dev, "mediatek,eye-vrt",
				 &instance->eye_vrt);
	device_property_read_u32(dev, "mediatek,eye-term",
				 &instance->eye_term);
	device_property_read_u32(dev, "mediatek,intr",
				 &instance->intr);
	device_property_read_u32(dev, "mediatek,discth",
				 &instance->discth);
	device_property_read_u32(dev, "mediatek,pre-emphasis",
				 &instance->pre_emphasis);
	dev_dbg(dev, "bc12:%d, src:%d, vrt:%d, term:%d, intr:%d, disc:%d\n",
		instance->bc12_en, instance->eye_src,
		instance->eye_vrt, instance->eye_term,
		instance->intr, instance->discth);
	dev_dbg(dev, "pre-emp:%d\n", instance->pre_emphasis);
}

static void u2_phy_props_set(struct mtk_tphy *tphy,
			     struct mtk_phy_instance *instance)
{
	struct u2phy_banks *u2_banks = &instance->u2_banks;
	void __iomem *com = u2_banks->com;

	if (instance->bc12_en) /* BC1.2 path Enable */
		mtk_phy_set_bits(com + U3P_U2PHYBC12C, P2C_RG_CHGDT_EN);

	if (tphy->pdata->version < MTK_PHY_V3 && instance->eye_src)
		mtk_phy_update_field(com + U3P_USBPHYACR5, PA5_RG_U2_HSTX_SRCTRL,
				     instance->eye_src);

	if (instance->eye_vrt)
		mtk_phy_update_field(com + U3P_USBPHYACR1, PA1_RG_VRT_SEL,
				     instance->eye_vrt);

	if (instance->eye_term)
		mtk_phy_update_field(com + U3P_USBPHYACR1, PA1_RG_TERM_SEL,
				     instance->eye_term);

	if (instance->intr) {
		if (u2_banks->misc)
			mtk_phy_set_bits(u2_banks->misc + U3P_MISC_REG1,
					 MR1_EFUSE_AUTO_LOAD_DIS);

		mtk_phy_update_field(com + U3P_USBPHYACR1, PA1_RG_INTR_CAL,
				     instance->intr);
	}

	if (instance->discth)
		mtk_phy_update_field(com + U3P_USBPHYACR6, PA6_RG_U2_DISCTH,
				     instance->discth);

	if (instance->pre_emphasis)
		mtk_phy_update_field(com + U3P_USBPHYACR6, PA6_RG_U2_PRE_EMP,
				     instance->pre_emphasis);
}

/* type switch for usb3/pcie/sgmii/sata */
static int phy_type_syscon_get(struct mtk_phy_instance *instance,
			       struct device_node *dn)
{
	struct of_phandle_args args;
	int ret;

	/* type switch function is optional */
	if (!of_property_read_bool(dn, "mediatek,syscon-type"))
		return 0;

	ret = of_parse_phandle_with_fixed_args(dn, "mediatek,syscon-type",
					       2, 0, &args);
	if (ret)
		return ret;

	instance->type_sw_reg = args.args[0];
	instance->type_sw_index = args.args[1] & 0x3; /* <=3 */
	instance->type_sw = syscon_node_to_regmap(args.np);
	of_node_put(args.np);
	dev_info(&instance->phy->dev, "type_sw - reg %#x, index %d\n",
		 instance->type_sw_reg, instance->type_sw_index);

	return PTR_ERR_OR_ZERO(instance->type_sw);
}

static int phy_type_set(struct mtk_phy_instance *instance)
{
	int type;
	u32 offset;

	if (!instance->type_sw)
		return 0;

	switch (instance->type) {
	case PHY_TYPE_USB3:
		type = RG_PHY_SW_USB3;
		break;
	case PHY_TYPE_PCIE:
		type = RG_PHY_SW_PCIE;
		break;
	case PHY_TYPE_SGMII:
		type = RG_PHY_SW_SGMII;
		break;
	case PHY_TYPE_SATA:
		type = RG_PHY_SW_SATA;
		break;
	case PHY_TYPE_USB2:
	default:
		return 0;
	}

	offset = instance->type_sw_index * BITS_PER_BYTE;
	regmap_update_bits(instance->type_sw, instance->type_sw_reg,
			   RG_PHY_SW_TYPE << offset, type << offset);

	return 0;
}

static int phy_efuse_get(struct mtk_tphy *tphy, struct mtk_phy_instance *instance)
{
	struct device *dev = &instance->phy->dev;
	int ret = 0;

	/* tphy v1 doesn't support sw efuse, skip it */
	if (!tphy->pdata->sw_efuse_supported) {
		instance->efuse_sw_en = 0;
		return 0;
	}

	/* software efuse is optional */
	instance->efuse_sw_en = device_property_read_bool(dev, "nvmem-cells");
	if (!instance->efuse_sw_en)
		return 0;

	switch (instance->type) {
	case PHY_TYPE_USB2:
		ret = nvmem_cell_read_variable_le_u32(dev, "intr", &instance->efuse_intr);
		if (ret) {
			dev_err(dev, "fail to get u2 intr efuse, %d\n", ret);
			break;
		}

		/* no efuse, ignore it */
		if (!instance->efuse_intr) {
			dev_warn(dev, "no u2 intr efuse, but dts enable it\n");
			instance->efuse_sw_en = 0;
			break;
		}

		dev_dbg(dev, "u2 efuse - intr %x\n", instance->efuse_intr);
		break;

	case PHY_TYPE_USB3:
	case PHY_TYPE_PCIE:
		ret = nvmem_cell_read_variable_le_u32(dev, "intr", &instance->efuse_intr);
		if (ret) {
			dev_err(dev, "fail to get u3 intr efuse, %d\n", ret);
			break;
		}

		ret = nvmem_cell_read_variable_le_u32(dev, "rx_imp", &instance->efuse_rx_imp);
		if (ret) {
			dev_err(dev, "fail to get u3 rx_imp efuse, %d\n", ret);
			break;
		}

		ret = nvmem_cell_read_variable_le_u32(dev, "tx_imp", &instance->efuse_tx_imp);
		if (ret) {
			dev_err(dev, "fail to get u3 tx_imp efuse, %d\n", ret);
			break;
		}

		/* no efuse, ignore it */
		if (!instance->efuse_intr &&
		    !instance->efuse_rx_imp &&
		    !instance->efuse_tx_imp) {
			dev_warn(dev, "no u3 intr efuse, but dts enable it\n");
			instance->efuse_sw_en = 0;
			break;
		}

		dev_dbg(dev, "u3 efuse - intr %x, rx_imp %x, tx_imp %x\n",
			instance->efuse_intr, instance->efuse_rx_imp,instance->efuse_tx_imp);
		break;
	default:
		dev_err(dev, "no sw efuse for type %d\n", instance->type);
		ret = -EINVAL;
	}

	return ret;
}

static void phy_efuse_set(struct mtk_phy_instance *instance)
{
	struct device *dev = &instance->phy->dev;
	struct u2phy_banks *u2_banks = &instance->u2_banks;
	struct u3phy_banks *u3_banks = &instance->u3_banks;

	if (!instance->efuse_sw_en)
		return;

	switch (instance->type) {
	case PHY_TYPE_USB2:
		mtk_phy_set_bits(u2_banks->misc + U3P_MISC_REG1, MR1_EFUSE_AUTO_LOAD_DIS);

		mtk_phy_update_field(u2_banks->com + U3P_USBPHYACR1, PA1_RG_INTR_CAL,
				     instance->efuse_intr);
		break;
	case PHY_TYPE_USB3:
	case PHY_TYPE_PCIE:
		mtk_phy_set_bits(u3_banks->phyd + U3P_U3_PHYD_RSV, P3D_RG_EFUSE_AUTO_LOAD_DIS);

		mtk_phy_update_field(u3_banks->phyd + U3P_U3_PHYD_IMPCAL0, P3D_RG_TX_IMPEL,
				    instance->efuse_tx_imp);
		mtk_phy_set_bits(u3_banks->phyd + U3P_U3_PHYD_IMPCAL0, P3D_RG_FORCE_TX_IMPEL);

		mtk_phy_update_field(u3_banks->phyd + U3P_U3_PHYD_IMPCAL1, P3D_RG_RX_IMPEL,
				    instance->efuse_rx_imp);
		mtk_phy_set_bits(u3_banks->phyd + U3P_U3_PHYD_IMPCAL1, P3D_RG_FORCE_RX_IMPEL);

		mtk_phy_update_field(u3_banks->phya + U3P_U3_PHYA_REG0, P3A_RG_IEXT_INTR,
				    instance->efuse_intr);
		break;
	default:
		dev_warn(dev, "no sw efuse for type %d\n", instance->type);
		break;
	}
}

static int mtk_phy_init(struct phy *phy)
{
	struct mtk_phy_instance *instance = phy_get_drvdata(phy);
	struct mtk_tphy *tphy = dev_get_drvdata(phy->dev.parent);
	int ret;

	ret = clk_bulk_prepare_enable(TPHY_CLKS_CNT, instance->clks);
	if (ret)
		return ret;

	phy_efuse_set(instance);

	switch (instance->type) {
	case PHY_TYPE_USB2:
		u2_phy_instance_init(tphy, instance);
		u2_phy_props_set(tphy, instance);
		break;
	case PHY_TYPE_USB3:
		u3_phy_instance_init(tphy, instance);
		break;
	case PHY_TYPE_PCIE:
		pcie_phy_instance_init(tphy, instance);
		break;
	case PHY_TYPE_SATA:
		sata_phy_instance_init(tphy, instance);
		break;
	case PHY_TYPE_SGMII:
		/* nothing to do, only used to set type */
		break;
	default:
		dev_err(tphy->dev, "incompatible PHY type\n");
		clk_bulk_disable_unprepare(TPHY_CLKS_CNT, instance->clks);
		return -EINVAL;
	}

	return 0;
}

static int mtk_phy_power_on(struct phy *phy)
{
	struct mtk_phy_instance *instance = phy_get_drvdata(phy);
	struct mtk_tphy *tphy = dev_get_drvdata(phy->dev.parent);

	if (instance->type == PHY_TYPE_USB2) {
		u2_phy_instance_power_on(tphy, instance);
		hs_slew_rate_calibrate(tphy, instance);
	} else if (instance->type == PHY_TYPE_PCIE) {
		pcie_phy_instance_power_on(tphy, instance);
	}

	return 0;
}

static int mtk_phy_power_off(struct phy *phy)
{
	struct mtk_phy_instance *instance = phy_get_drvdata(phy);
	struct mtk_tphy *tphy = dev_get_drvdata(phy->dev.parent);

	if (instance->type == PHY_TYPE_USB2)
		u2_phy_instance_power_off(tphy, instance);
	else if (instance->type == PHY_TYPE_PCIE)
		pcie_phy_instance_power_off(tphy, instance);

	return 0;
}

static int mtk_phy_exit(struct phy *phy)
{
	struct mtk_phy_instance *instance = phy_get_drvdata(phy);
	struct mtk_tphy *tphy = dev_get_drvdata(phy->dev.parent);

	if (instance->type == PHY_TYPE_USB2)
		u2_phy_instance_exit(tphy, instance);

	clk_bulk_disable_unprepare(TPHY_CLKS_CNT, instance->clks);
	return 0;
}

static int mtk_phy_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct mtk_phy_instance *instance = phy_get_drvdata(phy);
	struct mtk_tphy *tphy = dev_get_drvdata(phy->dev.parent);

	if (instance->type == PHY_TYPE_USB2)
		u2_phy_instance_set_mode(tphy, instance, mode);

	return 0;
}

static struct phy *mtk_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	struct mtk_tphy *tphy = dev_get_drvdata(dev);
	struct mtk_phy_instance *instance = NULL;
	struct device_node *phy_np = args->np;
	int index;
	int ret;

	if (args->args_count != 1) {
		dev_err(dev, "invalid number of cells in 'phy' property\n");
		return ERR_PTR(-EINVAL);
	}

	for (index = 0; index < tphy->nphys; index++)
		if (phy_np == tphy->phys[index]->phy->dev.of_node) {
			instance = tphy->phys[index];
			break;
		}

	if (!instance) {
		dev_err(dev, "failed to find appropriate phy\n");
		return ERR_PTR(-EINVAL);
	}

	instance->type = args->args[0];
	if (!(instance->type == PHY_TYPE_USB2 ||
	      instance->type == PHY_TYPE_USB3 ||
	      instance->type == PHY_TYPE_PCIE ||
	      instance->type == PHY_TYPE_SATA ||
	      instance->type == PHY_TYPE_SGMII)) {
		dev_err(dev, "unsupported device type: %d\n", instance->type);
		return ERR_PTR(-EINVAL);
	}

	switch (tphy->pdata->version) {
	case MTK_PHY_V1:
		phy_v1_banks_init(tphy, instance);
		break;
	case MTK_PHY_V2:
	case MTK_PHY_V3:
		phy_v2_banks_init(tphy, instance);
		break;
	default:
		dev_err(dev, "phy version is not supported\n");
		return ERR_PTR(-EINVAL);
	}

	ret = phy_efuse_get(tphy, instance);
	if (ret)
		return ERR_PTR(ret);

	phy_parse_property(tphy, instance);
	phy_type_set(instance);
	phy_debugfs_init(instance);

	return instance->phy;
}

static const struct phy_ops mtk_tphy_ops = {
	.init		= mtk_phy_init,
	.exit		= mtk_phy_exit,
	.power_on	= mtk_phy_power_on,
	.power_off	= mtk_phy_power_off,
	.set_mode	= mtk_phy_set_mode,
	.owner		= THIS_MODULE,
};

static const struct mtk_phy_pdata tphy_v1_pdata = {
	.avoid_rx_sen_degradation = false,
	.version = MTK_PHY_V1,
};

static const struct mtk_phy_pdata tphy_v2_pdata = {
	.avoid_rx_sen_degradation = false,
	.sw_efuse_supported = true,
	.version = MTK_PHY_V2,
};

static const struct mtk_phy_pdata tphy_v3_pdata = {
	.sw_efuse_supported = true,
	.version = MTK_PHY_V3,
};

static const struct mtk_phy_pdata mt8173_pdata = {
	.avoid_rx_sen_degradation = true,
	.version = MTK_PHY_V1,
};

static const struct mtk_phy_pdata mt8195_pdata = {
	.sw_pll_48m_to_26m = true,
	.sw_efuse_supported = true,
	.version = MTK_PHY_V3,
};

static const struct of_device_id mtk_tphy_id_table[] = {
	{ .compatible = "mediatek,mt2701-u3phy", .data = &tphy_v1_pdata },
	{ .compatible = "mediatek,mt2712-u3phy", .data = &tphy_v2_pdata },
	{ .compatible = "mediatek,mt8173-u3phy", .data = &mt8173_pdata },
	{ .compatible = "mediatek,mt8195-tphy", .data = &mt8195_pdata },
	{ .compatible = "mediatek,generic-tphy-v1", .data = &tphy_v1_pdata },
	{ .compatible = "mediatek,generic-tphy-v2", .data = &tphy_v2_pdata },
	{ .compatible = "mediatek,generic-tphy-v3", .data = &tphy_v3_pdata },
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_tphy_id_table);

static int mtk_tphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child_np;
	struct phy_provider *provider;
	struct resource *sif_res;
	struct mtk_tphy *tphy;
	struct resource res;
	int port, retval;

	tphy = devm_kzalloc(dev, sizeof(*tphy), GFP_KERNEL);
	if (!tphy)
		return -ENOMEM;

	tphy->pdata = of_device_get_match_data(dev);
	if (!tphy->pdata)
		return -EINVAL;

	tphy->nphys = of_get_child_count(np);
	tphy->phys = devm_kcalloc(dev, tphy->nphys,
				       sizeof(*tphy->phys), GFP_KERNEL);
	if (!tphy->phys)
		return -ENOMEM;

	tphy->dev = dev;
	platform_set_drvdata(pdev, tphy);

	sif_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	/* SATA phy of V1 needn't it if not shared with PCIe or USB */
	if (sif_res && tphy->pdata->version == MTK_PHY_V1) {
		/* get banks shared by multiple phys */
		tphy->sif_base = devm_ioremap_resource(dev, sif_res);
		if (IS_ERR(tphy->sif_base)) {
			dev_err(dev, "failed to remap sif regs\n");
			return PTR_ERR(tphy->sif_base);
		}
	}

	if (tphy->pdata->version < MTK_PHY_V3) {
		tphy->src_ref_clk = U3P_REF_CLK;
		tphy->src_coef = U3P_SLEW_RATE_COEF;
		/* update parameters of slew rate calibrate if exist */
		device_property_read_u32(dev, "mediatek,src-ref-clk-mhz",
					 &tphy->src_ref_clk);
		device_property_read_u32(dev, "mediatek,src-coef",
					 &tphy->src_coef);
	}

	port = 0;
	for_each_child_of_node(np, child_np) {
		struct mtk_phy_instance *instance;
		struct clk_bulk_data *clks;
		struct device *subdev;
		struct phy *phy;

		instance = devm_kzalloc(dev, sizeof(*instance), GFP_KERNEL);
		if (!instance) {
			retval = -ENOMEM;
			goto put_child;
		}

		tphy->phys[port] = instance;

		phy = devm_phy_create(dev, child_np, &mtk_tphy_ops);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create phy\n");
			retval = PTR_ERR(phy);
			goto put_child;
		}

		subdev = &phy->dev;
		retval = of_address_to_resource(child_np, 0, &res);
		if (retval) {
			dev_err(subdev, "failed to get address resource(id-%d)\n",
				port);
			goto put_child;
		}

		instance->port_base = devm_ioremap_resource(subdev, &res);
		if (IS_ERR(instance->port_base)) {
			retval = PTR_ERR(instance->port_base);
			goto put_child;
		}

		instance->phy = phy;
		instance->index = port;
		phy_set_drvdata(phy, instance);
		port++;

		clks = instance->clks;
		clks[0].id = "ref";     /* digital (& analog) clock */
		clks[1].id = "da_ref";  /* analog clock */
		retval = devm_clk_bulk_get_optional(subdev, TPHY_CLKS_CNT, clks);
		if (retval)
			goto put_child;

		retval = phy_type_syscon_get(instance, child_np);
		if (retval)
			goto put_child;
	}

	provider = devm_of_phy_provider_register(dev, mtk_phy_xlate);

	return PTR_ERR_OR_ZERO(provider);
put_child:
	of_node_put(child_np);
	return retval;
}

static struct platform_driver mtk_tphy_driver = {
	.probe		= mtk_tphy_probe,
	.driver		= {
		.name	= "mtk-tphy",
		.of_match_table = mtk_tphy_id_table,
	},
};

module_platform_driver(mtk_tphy_driver);

MODULE_AUTHOR("Chunfeng Yun <chunfeng.yun@mediatek.com>");
MODULE_DESCRIPTION("MediaTek T-PHY driver");
MODULE_LICENSE("GPL v2");
