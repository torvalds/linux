/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <dt-bindings/phy/phy.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

/*
 * for sifslv2 register, but exclude port's;
 * relative to USB3_SIF2_BASE base address
 */
#define SSUSB_SIFSLV_SPLLC		0x0000
#define SSUSB_SIFSLV_U2FREQ		0x0100

/* offsets of sub-segment in each port registers */
#define SSUSB_SIFSLV_U2PHY_COM_BASE	0x0000
#define SSUSB_SIFSLV_U3PHYD_BASE	0x0100
#define SSUSB_USB30_PHYA_SIV_B_BASE	0x0300
#define SSUSB_SIFSLV_U3PHYA_DA_BASE	0x0400

#define U3P_USBPHYACR0		(SSUSB_SIFSLV_U2PHY_COM_BASE + 0x0000)
#define PA0_RG_U2PLL_FORCE_ON		BIT(15)

#define U3P_USBPHYACR2		(SSUSB_SIFSLV_U2PHY_COM_BASE + 0x0008)
#define PA2_RG_SIF_U2PLL_FORCE_EN	BIT(18)

#define U3P_USBPHYACR5		(SSUSB_SIFSLV_U2PHY_COM_BASE + 0x0014)
#define PA5_RG_U2_HSTX_SRCAL_EN	BIT(15)
#define PA5_RG_U2_HSTX_SRCTRL		GENMASK(14, 12)
#define PA5_RG_U2_HSTX_SRCTRL_VAL(x)	((0x7 & (x)) << 12)
#define PA5_RG_U2_HS_100U_U3_EN	BIT(11)

#define U3P_USBPHYACR6		(SSUSB_SIFSLV_U2PHY_COM_BASE + 0x0018)
#define PA6_RG_U2_ISO_EN		BIT(31)
#define PA6_RG_U2_BC11_SW_EN		BIT(23)
#define PA6_RG_U2_OTG_VBUSCMP_EN	BIT(20)
#define PA6_RG_U2_SQTH		GENMASK(3, 0)
#define PA6_RG_U2_SQTH_VAL(x)	(0xf & (x))

#define U3P_U2PHYACR4		(SSUSB_SIFSLV_U2PHY_COM_BASE + 0x0020)
#define P2C_RG_USB20_GPIO_CTL		BIT(9)
#define P2C_USB20_GPIO_MODE		BIT(8)
#define P2C_U2_GPIO_CTR_MSK	(P2C_RG_USB20_GPIO_CTL | P2C_USB20_GPIO_MODE)

#define U3D_U2PHYDCR0		(SSUSB_SIFSLV_U2PHY_COM_BASE + 0x0060)
#define P2C_RG_SIF_U2PLL_FORCE_ON	BIT(24)

#define U3P_U2PHYDTM0		(SSUSB_SIFSLV_U2PHY_COM_BASE + 0x0068)
#define P2C_FORCE_UART_EN		BIT(26)
#define P2C_FORCE_DATAIN		BIT(23)
#define P2C_FORCE_DM_PULLDOWN		BIT(21)
#define P2C_FORCE_DP_PULLDOWN		BIT(20)
#define P2C_FORCE_XCVRSEL		BIT(19)
#define P2C_FORCE_SUSPENDM		BIT(18)
#define P2C_FORCE_TERMSEL		BIT(17)
#define P2C_RG_DATAIN			GENMASK(13, 10)
#define P2C_RG_DATAIN_VAL(x)		((0xf & (x)) << 10)
#define P2C_RG_DMPULLDOWN		BIT(7)
#define P2C_RG_DPPULLDOWN		BIT(6)
#define P2C_RG_XCVRSEL			GENMASK(5, 4)
#define P2C_RG_XCVRSEL_VAL(x)		((0x3 & (x)) << 4)
#define P2C_RG_SUSPENDM			BIT(3)
#define P2C_RG_TERMSEL			BIT(2)
#define P2C_DTM0_PART_MASK \
		(P2C_FORCE_DATAIN | P2C_FORCE_DM_PULLDOWN | \
		P2C_FORCE_DP_PULLDOWN | P2C_FORCE_XCVRSEL | \
		P2C_FORCE_TERMSEL | P2C_RG_DMPULLDOWN | \
		P2C_RG_DPPULLDOWN | P2C_RG_TERMSEL)

#define U3P_U2PHYDTM1		(SSUSB_SIFSLV_U2PHY_COM_BASE + 0x006C)
#define P2C_RG_UART_EN			BIT(16)
#define P2C_RG_VBUSVALID		BIT(5)
#define P2C_RG_SESSEND			BIT(4)
#define P2C_RG_AVALID			BIT(2)

#define U3P_U3_PHYA_REG0	(SSUSB_USB30_PHYA_SIV_B_BASE + 0x0000)
#define P3A_RG_U3_VUSB10_ON		BIT(5)

#define U3P_U3_PHYA_REG6	(SSUSB_USB30_PHYA_SIV_B_BASE + 0x0018)
#define P3A_RG_TX_EIDLE_CM		GENMASK(31, 28)
#define P3A_RG_TX_EIDLE_CM_VAL(x)	((0xf & (x)) << 28)

#define U3P_U3_PHYA_REG9	(SSUSB_USB30_PHYA_SIV_B_BASE + 0x0024)
#define P3A_RG_RX_DAC_MUX		GENMASK(5, 1)
#define P3A_RG_RX_DAC_MUX_VAL(x)	((0x1f & (x)) << 1)

#define U3P_U3PHYA_DA_REG0	(SSUSB_SIFSLV_U3PHYA_DA_BASE + 0x0000)
#define P3A_RG_XTAL_EXT_EN_U3		GENMASK(11, 10)
#define P3A_RG_XTAL_EXT_EN_U3_VAL(x)	((0x3 & (x)) << 10)

#define U3P_PHYD_CDR1		(SSUSB_SIFSLV_U3PHYD_BASE + 0x005c)
#define P3D_RG_CDR_BIR_LTD1		GENMASK(28, 24)
#define P3D_RG_CDR_BIR_LTD1_VAL(x)	((0x1f & (x)) << 24)
#define P3D_RG_CDR_BIR_LTD0		GENMASK(12, 8)
#define P3D_RG_CDR_BIR_LTD0_VAL(x)	((0x1f & (x)) << 8)

#define U3P_XTALCTL3		(SSUSB_SIFSLV_SPLLC + 0x0018)
#define XC3_RG_U3_XTAL_RX_PWD		BIT(9)
#define XC3_RG_U3_FRC_XTAL_RX_PWD	BIT(8)

#define U3P_U2FREQ_FMCR0	(SSUSB_SIFSLV_U2FREQ + 0x00)
#define P2F_RG_MONCLK_SEL	GENMASK(27, 26)
#define P2F_RG_MONCLK_SEL_VAL(x)	((0x3 & (x)) << 26)
#define P2F_RG_FREQDET_EN	BIT(24)
#define P2F_RG_CYCLECNT		GENMASK(23, 0)
#define P2F_RG_CYCLECNT_VAL(x)	((P2F_RG_CYCLECNT) & (x))

#define U3P_U2FREQ_VALUE	(SSUSB_SIFSLV_U2FREQ + 0x0c)

#define U3P_U2FREQ_FMMONR1	(SSUSB_SIFSLV_U2FREQ + 0x10)
#define P2F_USB_FM_VALID	BIT(0)
#define P2F_RG_FRCK_EN		BIT(8)

#define U3P_REF_CLK		26	/* MHZ */
#define U3P_SLEW_RATE_COEF	28
#define U3P_SR_COEF_DIVISOR	1000
#define U3P_FM_DET_CYCLE_CNT	1024

struct mt65xx_phy_pdata {
	/* avoid RX sensitivity level degradation only for mt8173 */
	bool avoid_rx_sen_degradation;
};

struct mt65xx_phy_instance {
	struct phy *phy;
	void __iomem *port_base;
	u32 index;
	u8 type;
};

struct mt65xx_u3phy {
	struct device *dev;
	void __iomem *sif_base;	/* include sif2, but exclude port's */
	struct clk *u3phya_ref;	/* reference clock of usb3 anolog phy */
	const struct mt65xx_phy_pdata *pdata;
	struct mt65xx_phy_instance **phys;
	int nphys;
};

static void hs_slew_rate_calibrate(struct mt65xx_u3phy *u3phy,
	struct mt65xx_phy_instance *instance)
{
	void __iomem *sif_base = u3phy->sif_base;
	int calibration_val;
	int fm_out;
	u32 tmp;

	/* enable USB ring oscillator */
	tmp = readl(instance->port_base + U3P_USBPHYACR5);
	tmp |= PA5_RG_U2_HSTX_SRCAL_EN;
	writel(tmp, instance->port_base + U3P_USBPHYACR5);
	udelay(1);

	/*enable free run clock */
	tmp = readl(sif_base + U3P_U2FREQ_FMMONR1);
	tmp |= P2F_RG_FRCK_EN;
	writel(tmp, sif_base + U3P_U2FREQ_FMMONR1);

	/* set cycle count as 1024, and select u2 channel */
	tmp = readl(sif_base + U3P_U2FREQ_FMCR0);
	tmp &= ~(P2F_RG_CYCLECNT | P2F_RG_MONCLK_SEL);
	tmp |= P2F_RG_CYCLECNT_VAL(U3P_FM_DET_CYCLE_CNT);
	tmp |= P2F_RG_MONCLK_SEL_VAL(instance->index);
	writel(tmp, sif_base + U3P_U2FREQ_FMCR0);

	/* enable frequency meter */
	tmp = readl(sif_base + U3P_U2FREQ_FMCR0);
	tmp |= P2F_RG_FREQDET_EN;
	writel(tmp, sif_base + U3P_U2FREQ_FMCR0);

	/* ignore return value */
	readl_poll_timeout(sif_base + U3P_U2FREQ_FMMONR1, tmp,
		  (tmp & P2F_USB_FM_VALID), 10, 200);

	fm_out = readl(sif_base + U3P_U2FREQ_VALUE);

	/* disable frequency meter */
	tmp = readl(sif_base + U3P_U2FREQ_FMCR0);
	tmp &= ~P2F_RG_FREQDET_EN;
	writel(tmp, sif_base + U3P_U2FREQ_FMCR0);

	/*disable free run clock */
	tmp = readl(sif_base + U3P_U2FREQ_FMMONR1);
	tmp &= ~P2F_RG_FRCK_EN;
	writel(tmp, sif_base + U3P_U2FREQ_FMMONR1);

	if (fm_out) {
		/* ( 1024 / FM_OUT ) x reference clock frequency x 0.028 */
		tmp = U3P_FM_DET_CYCLE_CNT * U3P_REF_CLK * U3P_SLEW_RATE_COEF;
		tmp /= fm_out;
		calibration_val = DIV_ROUND_CLOSEST(tmp, U3P_SR_COEF_DIVISOR);
	} else {
		/* if FM detection fail, set default value */
		calibration_val = 4;
	}
	dev_dbg(u3phy->dev, "phy:%d, fm_out:%d, calib:%d\n",
		instance->index, fm_out, calibration_val);

	/* set HS slew rate */
	tmp = readl(instance->port_base + U3P_USBPHYACR5);
	tmp &= ~PA5_RG_U2_HSTX_SRCTRL;
	tmp |= PA5_RG_U2_HSTX_SRCTRL_VAL(calibration_val);
	writel(tmp, instance->port_base + U3P_USBPHYACR5);

	/* disable USB ring oscillator */
	tmp = readl(instance->port_base + U3P_USBPHYACR5);
	tmp &= ~PA5_RG_U2_HSTX_SRCAL_EN;
	writel(tmp, instance->port_base + U3P_USBPHYACR5);
}

static void phy_instance_init(struct mt65xx_u3phy *u3phy,
	struct mt65xx_phy_instance *instance)
{
	void __iomem *port_base = instance->port_base;
	u32 index = instance->index;
	u32 tmp;

	/* switch to USB function. (system register, force ip into usb mode) */
	tmp = readl(port_base + U3P_U2PHYDTM0);
	tmp &= ~P2C_FORCE_UART_EN;
	tmp |= P2C_RG_XCVRSEL_VAL(1) | P2C_RG_DATAIN_VAL(0);
	writel(tmp, port_base + U3P_U2PHYDTM0);

	tmp = readl(port_base + U3P_U2PHYDTM1);
	tmp &= ~P2C_RG_UART_EN;
	writel(tmp, port_base + U3P_U2PHYDTM1);

	if (!index) {
		tmp = readl(port_base + U3P_U2PHYACR4);
		tmp &= ~P2C_U2_GPIO_CTR_MSK;
		writel(tmp, port_base + U3P_U2PHYACR4);
	}

	if (u3phy->pdata->avoid_rx_sen_degradation) {
		if (!index) {
			tmp = readl(port_base + U3P_USBPHYACR2);
			tmp |= PA2_RG_SIF_U2PLL_FORCE_EN;
			writel(tmp, port_base + U3P_USBPHYACR2);

			tmp = readl(port_base + U3D_U2PHYDCR0);
			tmp &= ~P2C_RG_SIF_U2PLL_FORCE_ON;
			writel(tmp, port_base + U3D_U2PHYDCR0);
		} else {
			tmp = readl(port_base + U3D_U2PHYDCR0);
			tmp |= P2C_RG_SIF_U2PLL_FORCE_ON;
			writel(tmp, port_base + U3D_U2PHYDCR0);

			tmp = readl(port_base + U3P_U2PHYDTM0);
			tmp |= P2C_RG_SUSPENDM | P2C_FORCE_SUSPENDM;
			writel(tmp, port_base + U3P_U2PHYDTM0);
		}
	}

	tmp = readl(port_base + U3P_USBPHYACR6);
	tmp &= ~PA6_RG_U2_BC11_SW_EN;	/* DP/DM BC1.1 path Disable */
	tmp &= ~PA6_RG_U2_SQTH;
	tmp |= PA6_RG_U2_SQTH_VAL(2);
	writel(tmp, port_base + U3P_USBPHYACR6);

	tmp = readl(port_base + U3P_U3PHYA_DA_REG0);
	tmp &= ~P3A_RG_XTAL_EXT_EN_U3;
	tmp |= P3A_RG_XTAL_EXT_EN_U3_VAL(2);
	writel(tmp, port_base + U3P_U3PHYA_DA_REG0);

	tmp = readl(port_base + U3P_U3_PHYA_REG9);
	tmp &= ~P3A_RG_RX_DAC_MUX;
	tmp |= P3A_RG_RX_DAC_MUX_VAL(4);
	writel(tmp, port_base + U3P_U3_PHYA_REG9);

	tmp = readl(port_base + U3P_U3_PHYA_REG6);
	tmp &= ~P3A_RG_TX_EIDLE_CM;
	tmp |= P3A_RG_TX_EIDLE_CM_VAL(0xe);
	writel(tmp, port_base + U3P_U3_PHYA_REG6);

	tmp = readl(port_base + U3P_PHYD_CDR1);
	tmp &= ~(P3D_RG_CDR_BIR_LTD0 | P3D_RG_CDR_BIR_LTD1);
	tmp |= P3D_RG_CDR_BIR_LTD0_VAL(0xc) | P3D_RG_CDR_BIR_LTD1_VAL(0x3);
	writel(tmp, port_base + U3P_PHYD_CDR1);

	dev_dbg(u3phy->dev, "%s(%d)\n", __func__, index);
}

static void phy_instance_power_on(struct mt65xx_u3phy *u3phy,
	struct mt65xx_phy_instance *instance)
{
	void __iomem *port_base = instance->port_base;
	u32 index = instance->index;
	u32 tmp;

	if (!index) {
		/* Set RG_SSUSB_VUSB10_ON as 1 after VUSB10 ready */
		tmp = readl(port_base + U3P_U3_PHYA_REG0);
		tmp |= P3A_RG_U3_VUSB10_ON;
		writel(tmp, port_base + U3P_U3_PHYA_REG0);
	}

	/* (force_suspendm=0) (let suspendm=1, enable usb 480MHz pll) */
	tmp = readl(port_base + U3P_U2PHYDTM0);
	tmp &= ~(P2C_FORCE_SUSPENDM | P2C_RG_XCVRSEL);
	tmp &= ~(P2C_RG_DATAIN | P2C_DTM0_PART_MASK);
	writel(tmp, port_base + U3P_U2PHYDTM0);

	/* OTG Enable */
	tmp = readl(port_base + U3P_USBPHYACR6);
	tmp |= PA6_RG_U2_OTG_VBUSCMP_EN;
	writel(tmp, port_base + U3P_USBPHYACR6);

	if (!index) {
		tmp = readl(u3phy->sif_base + U3P_XTALCTL3);
		tmp |= XC3_RG_U3_XTAL_RX_PWD | XC3_RG_U3_FRC_XTAL_RX_PWD;
		writel(tmp, u3phy->sif_base + U3P_XTALCTL3);

		/* switch 100uA current to SSUSB */
		tmp = readl(port_base + U3P_USBPHYACR5);
		tmp |= PA5_RG_U2_HS_100U_U3_EN;
		writel(tmp, port_base + U3P_USBPHYACR5);
	}

	tmp = readl(port_base + U3P_U2PHYDTM1);
	tmp |= P2C_RG_VBUSVALID | P2C_RG_AVALID;
	tmp &= ~P2C_RG_SESSEND;
	writel(tmp, port_base + U3P_U2PHYDTM1);

	/* USB 2.0 slew rate calibration */
	tmp = readl(port_base + U3P_USBPHYACR5);
	tmp &= ~PA5_RG_U2_HSTX_SRCTRL;
	tmp |= PA5_RG_U2_HSTX_SRCTRL_VAL(4);
	writel(tmp, port_base + U3P_USBPHYACR5);

	if (u3phy->pdata->avoid_rx_sen_degradation && index) {
		tmp = readl(port_base + U3D_U2PHYDCR0);
		tmp |= P2C_RG_SIF_U2PLL_FORCE_ON;
		writel(tmp, port_base + U3D_U2PHYDCR0);

		tmp = readl(port_base + U3P_U2PHYDTM0);
		tmp |= P2C_RG_SUSPENDM | P2C_FORCE_SUSPENDM;
		writel(tmp, port_base + U3P_U2PHYDTM0);
	}
	dev_dbg(u3phy->dev, "%s(%d)\n", __func__, index);
}

static void phy_instance_power_off(struct mt65xx_u3phy *u3phy,
	struct mt65xx_phy_instance *instance)
{
	void __iomem *port_base = instance->port_base;
	u32 index = instance->index;
	u32 tmp;

	tmp = readl(port_base + U3P_U2PHYDTM0);
	tmp &= ~(P2C_RG_XCVRSEL | P2C_RG_DATAIN);
	tmp |= P2C_FORCE_SUSPENDM;
	writel(tmp, port_base + U3P_U2PHYDTM0);

	/* OTG Disable */
	tmp = readl(port_base + U3P_USBPHYACR6);
	tmp &= ~PA6_RG_U2_OTG_VBUSCMP_EN;
	writel(tmp, port_base + U3P_USBPHYACR6);

	if (!index) {
		/* switch 100uA current back to USB2.0 */
		tmp = readl(port_base + U3P_USBPHYACR5);
		tmp &= ~PA5_RG_U2_HS_100U_U3_EN;
		writel(tmp, port_base + U3P_USBPHYACR5);
	}

	/* let suspendm=0, set utmi into analog power down */
	tmp = readl(port_base + U3P_U2PHYDTM0);
	tmp &= ~P2C_RG_SUSPENDM;
	writel(tmp, port_base + U3P_U2PHYDTM0);
	udelay(1);

	tmp = readl(port_base + U3P_U2PHYDTM1);
	tmp &= ~(P2C_RG_VBUSVALID | P2C_RG_AVALID);
	tmp |= P2C_RG_SESSEND;
	writel(tmp, port_base + U3P_U2PHYDTM1);

	if (!index) {
		tmp = readl(port_base + U3P_U3_PHYA_REG0);
		tmp &= ~P3A_RG_U3_VUSB10_ON;
		writel(tmp, port_base + U3P_U3_PHYA_REG0);
	}

	if (u3phy->pdata->avoid_rx_sen_degradation && index) {
		tmp = readl(port_base + U3D_U2PHYDCR0);
		tmp &= ~P2C_RG_SIF_U2PLL_FORCE_ON;
		writel(tmp, port_base + U3D_U2PHYDCR0);
	}

	dev_dbg(u3phy->dev, "%s(%d)\n", __func__, index);
}

static void phy_instance_exit(struct mt65xx_u3phy *u3phy,
	struct mt65xx_phy_instance *instance)
{
	void __iomem *port_base = instance->port_base;
	u32 index = instance->index;
	u32 tmp;

	if (u3phy->pdata->avoid_rx_sen_degradation && index) {
		tmp = readl(port_base + U3D_U2PHYDCR0);
		tmp &= ~P2C_RG_SIF_U2PLL_FORCE_ON;
		writel(tmp, port_base + U3D_U2PHYDCR0);

		tmp = readl(port_base + U3P_U2PHYDTM0);
		tmp &= ~P2C_FORCE_SUSPENDM;
		writel(tmp, port_base + U3P_U2PHYDTM0);
	}
}

static int mt65xx_phy_init(struct phy *phy)
{
	struct mt65xx_phy_instance *instance = phy_get_drvdata(phy);
	struct mt65xx_u3phy *u3phy = dev_get_drvdata(phy->dev.parent);
	int ret;

	ret = clk_prepare_enable(u3phy->u3phya_ref);
	if (ret) {
		dev_err(u3phy->dev, "failed to enable u3phya_ref\n");
		return ret;
	}

	phy_instance_init(u3phy, instance);
	return 0;
}

static int mt65xx_phy_power_on(struct phy *phy)
{
	struct mt65xx_phy_instance *instance = phy_get_drvdata(phy);
	struct mt65xx_u3phy *u3phy = dev_get_drvdata(phy->dev.parent);

	phy_instance_power_on(u3phy, instance);
	hs_slew_rate_calibrate(u3phy, instance);
	return 0;
}

static int mt65xx_phy_power_off(struct phy *phy)
{
	struct mt65xx_phy_instance *instance = phy_get_drvdata(phy);
	struct mt65xx_u3phy *u3phy = dev_get_drvdata(phy->dev.parent);

	phy_instance_power_off(u3phy, instance);
	return 0;
}

static int mt65xx_phy_exit(struct phy *phy)
{
	struct mt65xx_phy_instance *instance = phy_get_drvdata(phy);
	struct mt65xx_u3phy *u3phy = dev_get_drvdata(phy->dev.parent);

	phy_instance_exit(u3phy, instance);
	clk_disable_unprepare(u3phy->u3phya_ref);
	return 0;
}

static struct phy *mt65xx_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	struct mt65xx_u3phy *u3phy = dev_get_drvdata(dev);
	struct mt65xx_phy_instance *instance = NULL;
	struct device_node *phy_np = args->np;
	int index;


	if (args->args_count != 1) {
		dev_err(dev, "invalid number of cells in 'phy' property\n");
		return ERR_PTR(-EINVAL);
	}

	for (index = 0; index < u3phy->nphys; index++)
		if (phy_np == u3phy->phys[index]->phy->dev.of_node) {
			instance = u3phy->phys[index];
			break;
		}

	if (!instance) {
		dev_err(dev, "failed to find appropriate phy\n");
		return ERR_PTR(-EINVAL);
	}

	instance->type = args->args[0];

	if (!(instance->type == PHY_TYPE_USB2 ||
	      instance->type == PHY_TYPE_USB3)) {
		dev_err(dev, "unsupported device type: %d\n", instance->type);
		return ERR_PTR(-EINVAL);
	}

	return instance->phy;
}

static const struct phy_ops mt65xx_u3phy_ops = {
	.init		= mt65xx_phy_init,
	.exit		= mt65xx_phy_exit,
	.power_on	= mt65xx_phy_power_on,
	.power_off	= mt65xx_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct mt65xx_phy_pdata mt2701_pdata = {
	.avoid_rx_sen_degradation = false,
};

static const struct mt65xx_phy_pdata mt8173_pdata = {
	.avoid_rx_sen_degradation = true,
};

static const struct of_device_id mt65xx_u3phy_id_table[] = {
	{ .compatible = "mediatek,mt2701-u3phy", .data = &mt2701_pdata },
	{ .compatible = "mediatek,mt8173-u3phy", .data = &mt8173_pdata },
	{ },
};
MODULE_DEVICE_TABLE(of, mt65xx_u3phy_id_table);

static int mt65xx_u3phy_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child_np;
	struct phy_provider *provider;
	struct resource *sif_res;
	struct mt65xx_u3phy *u3phy;
	struct resource res;
	int port, retval;

	match = of_match_node(mt65xx_u3phy_id_table, pdev->dev.of_node);
	if (!match)
		return -EINVAL;

	u3phy = devm_kzalloc(dev, sizeof(*u3phy), GFP_KERNEL);
	if (!u3phy)
		return -ENOMEM;

	u3phy->pdata = match->data;
	u3phy->nphys = of_get_child_count(np);
	u3phy->phys = devm_kcalloc(dev, u3phy->nphys,
				       sizeof(*u3phy->phys), GFP_KERNEL);
	if (!u3phy->phys)
		return -ENOMEM;

	u3phy->dev = dev;
	platform_set_drvdata(pdev, u3phy);

	sif_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	u3phy->sif_base = devm_ioremap_resource(dev, sif_res);
	if (IS_ERR(u3phy->sif_base)) {
		dev_err(dev, "failed to remap sif regs\n");
		return PTR_ERR(u3phy->sif_base);
	}

	u3phy->u3phya_ref = devm_clk_get(dev, "u3phya_ref");
	if (IS_ERR(u3phy->u3phya_ref)) {
		dev_err(dev, "error to get u3phya_ref\n");
		return PTR_ERR(u3phy->u3phya_ref);
	}

	port = 0;
	for_each_child_of_node(np, child_np) {
		struct mt65xx_phy_instance *instance;
		struct phy *phy;

		instance = devm_kzalloc(dev, sizeof(*instance), GFP_KERNEL);
		if (!instance) {
			retval = -ENOMEM;
			goto put_child;
		}

		u3phy->phys[port] = instance;

		phy = devm_phy_create(dev, child_np, &mt65xx_u3phy_ops);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create phy\n");
			retval = PTR_ERR(phy);
			goto put_child;
		}

		retval = of_address_to_resource(child_np, 0, &res);
		if (retval) {
			dev_err(dev, "failed to get address resource(id-%d)\n",
				port);
			goto put_child;
		}

		instance->port_base = devm_ioremap_resource(&phy->dev, &res);
		if (IS_ERR(instance->port_base)) {
			dev_err(dev, "failed to remap phy regs\n");
			retval = PTR_ERR(instance->port_base);
			goto put_child;
		}

		instance->phy = phy;
		instance->index = port;
		phy_set_drvdata(phy, instance);
		port++;
	}

	provider = devm_of_phy_provider_register(dev, mt65xx_phy_xlate);

	return PTR_ERR_OR_ZERO(provider);
put_child:
	of_node_put(child_np);
	return retval;
}

static struct platform_driver mt65xx_u3phy_driver = {
	.probe		= mt65xx_u3phy_probe,
	.driver		= {
		.name	= "mt65xx-u3phy",
		.of_match_table = mt65xx_u3phy_id_table,
	},
};

module_platform_driver(mt65xx_u3phy_driver);

MODULE_AUTHOR("Chunfeng Yun <chunfeng.yun@mediatek.com>");
MODULE_DESCRIPTION("mt65xx USB PHY driver");
MODULE_LICENSE("GPL v2");
