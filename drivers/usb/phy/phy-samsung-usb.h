/* linux/drivers/usb/phy/phy-samsung-usb.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * Samsung USB-PHY transceiver; talks to S3C HS OTG controller, EHCI-S5P and
 * OHCI-EXYNOS controllers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/usb/phy.h>

/* Register definitions */

#define SAMSUNG_PHYPWR				(0x00)

#define PHYPWR_NORMAL_MASK			(0x19 << 0)
#define PHYPWR_OTG_DISABLE			(0x1 << 4)
#define PHYPWR_ANALOG_POWERDOWN			(0x1 << 3)
#define PHYPWR_FORCE_SUSPEND			(0x1 << 1)
/* For Exynos4 */
#define PHYPWR_NORMAL_MASK_PHY0			(0x39 << 0)
#define PHYPWR_SLEEP_PHY0			(0x1 << 5)

#define SAMSUNG_PHYCLK				(0x04)

#define PHYCLK_MODE_USB11			(0x1 << 6)
#define PHYCLK_EXT_OSC				(0x1 << 5)
#define PHYCLK_COMMON_ON_N			(0x1 << 4)
#define PHYCLK_ID_PULL				(0x1 << 2)
#define PHYCLK_CLKSEL_MASK			(0x3 << 0)
#define PHYCLK_CLKSEL_48M			(0x0 << 0)
#define PHYCLK_CLKSEL_12M			(0x2 << 0)
#define PHYCLK_CLKSEL_24M			(0x3 << 0)

#define SAMSUNG_RSTCON				(0x08)

#define RSTCON_PHYLINK_SWRST			(0x1 << 2)
#define RSTCON_HLINK_SWRST			(0x1 << 1)
#define RSTCON_SWRST				(0x1 << 0)

/* EXYNOS4X12 */
#define EXYNOS4X12_PHY_HSIC_CTRL0		(0x04)
#define EXYNOS4X12_PHY_HSIC_CTRL1		(0x08)

#define PHYPWR_NORMAL_MASK_HSIC1		(0x7 << 12)
#define PHYPWR_NORMAL_MASK_HSIC0		(0x7 << 9)
#define PHYPWR_NORMAL_MASK_PHY1			(0x7 << 6)

#define RSTCON_HOSTPHY_SWRST			(0xf << 3)

/* EXYNOS5 */
#define EXYNOS5_PHY_HOST_CTRL0			(0x00)

#define HOST_CTRL0_PHYSWRSTALL			(0x1 << 31)

#define HOST_CTRL0_REFCLKSEL_MASK		(0x3 << 19)
#define HOST_CTRL0_REFCLKSEL_XTAL		(0x0 << 19)
#define HOST_CTRL0_REFCLKSEL_EXTL		(0x1 << 19)
#define HOST_CTRL0_REFCLKSEL_CLKCORE		(0x2 << 19)

#define HOST_CTRL0_FSEL_MASK			(0x7 << 16)
#define HOST_CTRL0_FSEL(_x)			((_x) << 16)

#define FSEL_CLKSEL_50M				(0x7)
#define FSEL_CLKSEL_24M				(0x5)
#define FSEL_CLKSEL_20M				(0x4)
#define FSEL_CLKSEL_19200K			(0x3)
#define FSEL_CLKSEL_12M				(0x2)
#define FSEL_CLKSEL_10M				(0x1)
#define FSEL_CLKSEL_9600K			(0x0)

#define HOST_CTRL0_TESTBURNIN			(0x1 << 11)
#define HOST_CTRL0_RETENABLE			(0x1 << 10)
#define HOST_CTRL0_COMMONON_N			(0x1 << 9)
#define HOST_CTRL0_SIDDQ			(0x1 << 6)
#define HOST_CTRL0_FORCESLEEP			(0x1 << 5)
#define HOST_CTRL0_FORCESUSPEND			(0x1 << 4)
#define HOST_CTRL0_WORDINTERFACE		(0x1 << 3)
#define HOST_CTRL0_UTMISWRST			(0x1 << 2)
#define HOST_CTRL0_LINKSWRST			(0x1 << 1)
#define HOST_CTRL0_PHYSWRST			(0x1 << 0)

#define EXYNOS5_PHY_HOST_TUNE0			(0x04)

#define EXYNOS5_PHY_HSIC_CTRL1			(0x10)

#define EXYNOS5_PHY_HSIC_TUNE1			(0x14)

#define EXYNOS5_PHY_HSIC_CTRL2			(0x20)

#define EXYNOS5_PHY_HSIC_TUNE2			(0x24)

#define HSIC_CTRL_REFCLKSEL_MASK		(0x3 << 23)
#define HSIC_CTRL_REFCLKSEL			(0x2 << 23)

#define HSIC_CTRL_REFCLKDIV_MASK		(0x7f << 16)
#define HSIC_CTRL_REFCLKDIV(_x)			((_x) << 16)
#define HSIC_CTRL_REFCLKDIV_12			(0x24 << 16)
#define HSIC_CTRL_REFCLKDIV_15			(0x1c << 16)
#define HSIC_CTRL_REFCLKDIV_16			(0x1a << 16)
#define HSIC_CTRL_REFCLKDIV_19_2		(0x15 << 16)
#define HSIC_CTRL_REFCLKDIV_20			(0x14 << 16)

#define HSIC_CTRL_SIDDQ				(0x1 << 6)
#define HSIC_CTRL_FORCESLEEP			(0x1 << 5)
#define HSIC_CTRL_FORCESUSPEND			(0x1 << 4)
#define HSIC_CTRL_WORDINTERFACE			(0x1 << 3)
#define HSIC_CTRL_UTMISWRST			(0x1 << 2)
#define HSIC_CTRL_PHYSWRST			(0x1 << 0)

#define EXYNOS5_PHY_HOST_EHCICTRL		(0x30)

#define HOST_EHCICTRL_ENAINCRXALIGN		(0x1 << 29)
#define HOST_EHCICTRL_ENAINCR4			(0x1 << 28)
#define HOST_EHCICTRL_ENAINCR8			(0x1 << 27)
#define HOST_EHCICTRL_ENAINCR16			(0x1 << 26)

#define EXYNOS5_PHY_HOST_OHCICTRL		(0x34)

#define HOST_OHCICTRL_SUSPLGCY			(0x1 << 3)
#define HOST_OHCICTRL_APPSTARTCLK		(0x1 << 2)
#define HOST_OHCICTRL_CNTSEL			(0x1 << 1)
#define HOST_OHCICTRL_CLKCKTRST			(0x1 << 0)

#define EXYNOS5_PHY_OTG_SYS			(0x38)

#define OTG_SYS_PHYLINK_SWRESET			(0x1 << 14)
#define OTG_SYS_LINKSWRST_UOTG			(0x1 << 13)
#define OTG_SYS_PHY0_SWRST			(0x1 << 12)

#define OTG_SYS_REFCLKSEL_MASK			(0x3 << 9)
#define OTG_SYS_REFCLKSEL_XTAL			(0x0 << 9)
#define OTG_SYS_REFCLKSEL_EXTL			(0x1 << 9)
#define OTG_SYS_REFCLKSEL_CLKCORE		(0x2 << 9)

#define OTG_SYS_IDPULLUP_UOTG			(0x1 << 8)
#define OTG_SYS_COMMON_ON			(0x1 << 7)

#define OTG_SYS_FSEL_MASK			(0x7 << 4)
#define OTG_SYS_FSEL(_x)			((_x) << 4)

#define OTG_SYS_FORCESLEEP			(0x1 << 3)
#define OTG_SYS_OTGDISABLE			(0x1 << 2)
#define OTG_SYS_SIDDQ_UOTG			(0x1 << 1)
#define OTG_SYS_FORCESUSPEND			(0x1 << 0)

#define EXYNOS5_PHY_OTG_TUNE			(0x40)

/* EXYNOS5: USB 3.0 DRD */
#define EXYNOS5_DRD_LINKSYSTEM			(0x04)

#define LINKSYSTEM_FLADJ_MASK			(0x3f << 1)
#define LINKSYSTEM_FLADJ(_x)			((_x) << 1)
#define LINKSYSTEM_XHCI_VERSION_CONTROL		(0x1 << 27)

#define EXYNOS5_DRD_PHYUTMI			(0x08)

#define PHYUTMI_OTGDISABLE			(0x1 << 6)
#define PHYUTMI_FORCESUSPEND			(0x1 << 1)
#define PHYUTMI_FORCESLEEP			(0x1 << 0)

#define EXYNOS5_DRD_PHYPIPE			(0x0c)

#define EXYNOS5_DRD_PHYCLKRST			(0x10)

#define PHYCLKRST_SSC_REFCLKSEL_MASK		(0xff << 23)
#define PHYCLKRST_SSC_REFCLKSEL(_x)		((_x) << 23)

#define PHYCLKRST_SSC_RANGE_MASK		(0x03 << 21)
#define PHYCLKRST_SSC_RANGE(_x)			((_x) << 21)

#define PHYCLKRST_SSC_EN			(0x1 << 20)
#define PHYCLKRST_REF_SSP_EN			(0x1 << 19)
#define PHYCLKRST_REF_CLKDIV2			(0x1 << 18)

#define PHYCLKRST_MPLL_MULTIPLIER_MASK		(0x7f << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_100MHZ_REF	(0x19 << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_50M_REF	(0x02 << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_24MHZ_REF	(0x68 << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_20MHZ_REF	(0x7d << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_19200KHZ_REF	(0x02 << 11)

#define PHYCLKRST_FSEL_MASK			(0x3f << 5)
#define PHYCLKRST_FSEL(_x)			((_x) << 5)
#define PHYCLKRST_FSEL_PAD_100MHZ		(0x27 << 5)
#define PHYCLKRST_FSEL_PAD_24MHZ		(0x2a << 5)
#define PHYCLKRST_FSEL_PAD_20MHZ		(0x31 << 5)
#define PHYCLKRST_FSEL_PAD_19_2MHZ		(0x38 << 5)

#define PHYCLKRST_RETENABLEN			(0x1 << 4)

#define PHYCLKRST_REFCLKSEL_MASK		(0x03 << 2)
#define PHYCLKRST_REFCLKSEL_PAD_REFCLK		(0x2 << 2)
#define PHYCLKRST_REFCLKSEL_EXT_REFCLK		(0x3 << 2)

#define PHYCLKRST_PORTRESET			(0x1 << 1)
#define PHYCLKRST_COMMONONN			(0x1 << 0)

#define EXYNOS5_DRD_PHYREG0			(0x14)
#define EXYNOS5_DRD_PHYREG1			(0x18)

#define EXYNOS5_DRD_PHYPARAM0			(0x1c)

#define PHYPARAM0_REF_USE_PAD			(0x1 << 31)
#define PHYPARAM0_REF_LOSLEVEL_MASK		(0x1f << 26)
#define PHYPARAM0_REF_LOSLEVEL			(0x9 << 26)

#define EXYNOS5_DRD_PHYPARAM1			(0x20)

#define PHYPARAM1_PCS_TXDEEMPH_MASK		(0x3f << 0)
#define PHYPARAM1_PCS_TXDEEMPH			(0x1c)

#define EXYNOS5_DRD_PHYTERM			(0x24)

#define EXYNOS5_DRD_PHYTEST			(0x28)

#define PHYTEST_POWERDOWN_SSP			(0x1 << 3)
#define PHYTEST_POWERDOWN_HSP			(0x1 << 2)

#define EXYNOS5_DRD_PHYADP			(0x2c)

#define EXYNOS5_DRD_PHYBATCHG			(0x30)

#define PHYBATCHG_UTMI_CLKSEL			(0x1 << 2)

#define EXYNOS5_DRD_PHYRESUME			(0x34)
#define EXYNOS5_DRD_LINKPORT			(0x44)

#ifndef MHZ
#define MHZ (1000*1000)
#endif

#ifndef KHZ
#define KHZ (1000)
#endif

#define EXYNOS_USBHOST_PHY_CTRL_OFFSET		(0x4)
#define S3C64XX_USBPHY_ENABLE			(0x1 << 16)
#define EXYNOS_USBPHY_ENABLE			(0x1 << 0)
#define EXYNOS_USB20PHY_CFG_HOST_LINK		(0x1 << 0)

enum samsung_cpu_type {
	TYPE_S3C64XX,
	TYPE_EXYNOS4210,
	TYPE_EXYNOS4X12,
	TYPE_EXYNOS5250,
};

struct samsung_usbphy;

/*
 * struct samsung_usbphy_drvdata - driver data for various SoC variants
 * @cpu_type: machine identifier
 * @devphy_en_mask: device phy enable mask for PHY CONTROL register
 * @hostphy_en_mask: host phy enable mask for PHY CONTROL register
 * @devphy_reg_offset: offset to DEVICE PHY CONTROL register from
 *		       mapped address of system controller.
 * @hostphy_reg_offset: offset to HOST PHY CONTROL register from
 *		       mapped address of system controller.
 *
 *	Here we have a separate mask for device type phy.
 *	Having different masks for host and device type phy helps
 *	in setting independent masks in case of SoCs like S5PV210,
 *	in which PHY0 and PHY1 enable bits belong to same register
 *	placed at position 0 and 1 respectively.
 *	Although for newer SoCs like exynos these bits belong to
 *	different registers altogether placed at position 0.
 */
struct samsung_usbphy_drvdata {
	int cpu_type;
	int devphy_en_mask;
	int hostphy_en_mask;
	u32 devphy_reg_offset;
	u32 hostphy_reg_offset;
	int (*rate_to_clksel)(struct samsung_usbphy *, unsigned long);
	void (*set_isolation)(struct samsung_usbphy *, bool);
	void (*phy_enable)(struct samsung_usbphy *);
	void (*phy_disable)(struct samsung_usbphy *);
};

/*
 * struct samsung_usbphy - transceiver driver state
 * @phy: transceiver structure
 * @plat: platform data
 * @dev: The parent device supplied to the probe function
 * @clk: usb phy clock
 * @regs: usb phy controller registers memory base
 * @pmuregs: USB device PHY_CONTROL register memory base
 * @sysreg: USB2.0 PHY_CFG register memory base
 * @ref_clk_freq: reference clock frequency selection
 * @drv_data: driver data available for different SoCs
 * @phy_type: Samsung SoCs specific phy types:	#HOST
 *						#DEVICE
 * @phy_usage: usage count for phy
 * @lock: lock for phy operations
 */
struct samsung_usbphy {
	struct usb_phy	phy;
	struct samsung_usbphy_data *plat;
	struct device	*dev;
	struct clk	*clk;
	void __iomem	*regs;
	void __iomem	*pmuregs;
	void __iomem	*sysreg;
	int		ref_clk_freq;
	const struct samsung_usbphy_drvdata *drv_data;
	enum samsung_usb_phy_type phy_type;
	atomic_t	phy_usage;
	spinlock_t	lock;
};

#define phy_to_sphy(x)		container_of((x), struct samsung_usbphy, phy)

static const struct of_device_id samsung_usbphy_dt_match[];

static inline const struct samsung_usbphy_drvdata
*samsung_usbphy_get_driver_data(struct platform_device *pdev)
{
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(samsung_usbphy_dt_match,
							pdev->dev.of_node);
		return match->data;
	}

	return (struct samsung_usbphy_drvdata *)
				platform_get_device_id(pdev)->driver_data;
}

extern int samsung_usbphy_parse_dt(struct samsung_usbphy *sphy);
extern void samsung_usbphy_set_isolation_4210(struct samsung_usbphy *sphy,
								bool on);
extern void samsung_usbphy_cfg_sel(struct samsung_usbphy *sphy);
extern int samsung_usbphy_set_type(struct usb_phy *phy,
					enum samsung_usb_phy_type phy_type);
extern int samsung_usbphy_get_refclk_freq(struct samsung_usbphy *sphy);
extern int samsung_usbphy_rate_to_clksel_64xx(struct samsung_usbphy *sphy,
							unsigned long rate);
extern int samsung_usbphy_rate_to_clksel_4x12(struct samsung_usbphy *sphy,
							unsigned long rate);
