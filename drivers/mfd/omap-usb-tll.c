// SPDX-License-Identifier: GPL-2.0-only
/*
 * omap-usb-tll.c - The USB TLL driver for OMAP EHCI & OHCI
 *
 * Copyright (C) 2012-2013 Texas Instruments Incorporated - https://www.ti.com
 * Author: Keshava Munegowda <keshava_mgowda@ti.com>
 * Author: Roger Quadros <rogerq@ti.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/platform_data/usb-omap.h>
#include <linux/of.h>

#include "omap-usb.h"

#define USBTLL_DRIVER_NAME	"usbhs_tll"

/* TLL Register Set */
#define	OMAP_USBTLL_REVISION				(0x00)
#define	OMAP_USBTLL_SYSCONFIG				(0x10)
#define	OMAP_USBTLL_SYSCONFIG_CACTIVITY			(1 << 8)
#define	OMAP_USBTLL_SYSCONFIG_SIDLEMODE			(1 << 3)
#define	OMAP_USBTLL_SYSCONFIG_ENAWAKEUP			(1 << 2)
#define	OMAP_USBTLL_SYSCONFIG_SOFTRESET			(1 << 1)
#define	OMAP_USBTLL_SYSCONFIG_AUTOIDLE			(1 << 0)

#define	OMAP_USBTLL_SYSSTATUS				(0x14)
#define	OMAP_USBTLL_SYSSTATUS_RESETDONE			(1 << 0)

#define	OMAP_USBTLL_IRQSTATUS				(0x18)
#define	OMAP_USBTLL_IRQENABLE				(0x1C)

#define	OMAP_TLL_SHARED_CONF				(0x30)
#define	OMAP_TLL_SHARED_CONF_USB_90D_DDR_EN		(1 << 6)
#define	OMAP_TLL_SHARED_CONF_USB_180D_SDR_EN		(1 << 5)
#define	OMAP_TLL_SHARED_CONF_USB_DIVRATION		(1 << 2)
#define	OMAP_TLL_SHARED_CONF_FCLK_REQ			(1 << 1)
#define	OMAP_TLL_SHARED_CONF_FCLK_IS_ON			(1 << 0)

#define	OMAP_TLL_CHANNEL_CONF(num)			(0x040 + 0x004 * num)
#define OMAP_TLL_CHANNEL_CONF_FSLSMODE_SHIFT		24
#define OMAP_TLL_CHANNEL_CONF_DRVVBUS			(1 << 16)
#define OMAP_TLL_CHANNEL_CONF_CHRGVBUS			(1 << 15)
#define	OMAP_TLL_CHANNEL_CONF_ULPINOBITSTUFF		(1 << 11)
#define	OMAP_TLL_CHANNEL_CONF_ULPI_ULPIAUTOIDLE		(1 << 10)
#define	OMAP_TLL_CHANNEL_CONF_UTMIAUTOIDLE		(1 << 9)
#define	OMAP_TLL_CHANNEL_CONF_ULPIDDRMODE		(1 << 8)
#define OMAP_TLL_CHANNEL_CONF_MODE_TRANSPARENT_UTMI	(2 << 1)
#define OMAP_TLL_CHANNEL_CONF_CHANMODE_FSLS		(1 << 1)
#define	OMAP_TLL_CHANNEL_CONF_CHANEN			(1 << 0)

#define OMAP_TLL_FSLSMODE_6PIN_PHY_DAT_SE0		0x0
#define OMAP_TLL_FSLSMODE_6PIN_PHY_DP_DM		0x1
#define OMAP_TLL_FSLSMODE_3PIN_PHY			0x2
#define OMAP_TLL_FSLSMODE_4PIN_PHY			0x3
#define OMAP_TLL_FSLSMODE_6PIN_TLL_DAT_SE0		0x4
#define OMAP_TLL_FSLSMODE_6PIN_TLL_DP_DM		0x5
#define OMAP_TLL_FSLSMODE_3PIN_TLL			0x6
#define OMAP_TLL_FSLSMODE_4PIN_TLL			0x7
#define OMAP_TLL_FSLSMODE_2PIN_TLL_DAT_SE0		0xA
#define OMAP_TLL_FSLSMODE_2PIN_DAT_DP_DM		0xB

#define	OMAP_TLL_ULPI_FUNCTION_CTRL(num)		(0x804 + 0x100 * num)
#define	OMAP_TLL_ULPI_INTERFACE_CTRL(num)		(0x807 + 0x100 * num)
#define	OMAP_TLL_ULPI_OTG_CTRL(num)			(0x80A + 0x100 * num)
#define	OMAP_TLL_ULPI_INT_EN_RISE(num)			(0x80D + 0x100 * num)
#define	OMAP_TLL_ULPI_INT_EN_FALL(num)			(0x810 + 0x100 * num)
#define	OMAP_TLL_ULPI_INT_STATUS(num)			(0x813 + 0x100 * num)
#define	OMAP_TLL_ULPI_INT_LATCH(num)			(0x814 + 0x100 * num)
#define	OMAP_TLL_ULPI_DEBUG(num)			(0x815 + 0x100 * num)
#define	OMAP_TLL_ULPI_SCRATCH_REGISTER(num)		(0x816 + 0x100 * num)

#define OMAP_REV2_TLL_CHANNEL_COUNT			2
#define OMAP_TLL_CHANNEL_COUNT				3
#define OMAP_TLL_CHANNEL_1_EN_MASK			(1 << 0)
#define OMAP_TLL_CHANNEL_2_EN_MASK			(1 << 1)
#define OMAP_TLL_CHANNEL_3_EN_MASK			(1 << 2)

/* Values of USBTLL_REVISION - Note: these are not given in the TRM */
#define OMAP_USBTLL_REV1		0x00000015	/* OMAP3 */
#define OMAP_USBTLL_REV2		0x00000018	/* OMAP 3630 */
#define OMAP_USBTLL_REV3		0x00000004	/* OMAP4 */
#define OMAP_USBTLL_REV4		0x00000006	/* OMAP5 */

#define is_ehci_tll_mode(x)	(x == OMAP_EHCI_PORT_MODE_TLL)

/* only PHY and UNUSED modes don't need TLL */
#define omap_usb_mode_needs_tll(x)	((x) != OMAP_USBHS_PORT_MODE_UNUSED &&\
					 (x) != OMAP_EHCI_PORT_MODE_PHY)

struct usbtll_omap {
	void __iomem	*base;
	int		nch;		/* num. of channels */
	struct clk	*ch_clk[];	/* must be the last member */
};

/*-------------------------------------------------------------------------*/

static const char usbtll_driver_name[] = USBTLL_DRIVER_NAME;
static struct device	*tll_dev;
static DEFINE_SPINLOCK(tll_lock);	/* serialize access to tll_dev */

/*-------------------------------------------------------------------------*/

static inline void usbtll_write(void __iomem *base, u32 reg, u32 val)
{
	writel_relaxed(val, base + reg);
}

static inline u32 usbtll_read(void __iomem *base, u32 reg)
{
	return readl_relaxed(base + reg);
}

static inline void usbtll_writeb(void __iomem *base, u32 reg, u8 val)
{
	writeb_relaxed(val, base + reg);
}

static inline u8 usbtll_readb(void __iomem *base, u32 reg)
{
	return readb_relaxed(base + reg);
}

/*-------------------------------------------------------------------------*/

static bool is_ohci_port(enum usbhs_omap_port_mode pmode)
{
	switch (pmode) {
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_PHY_3PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_PHY_4PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_3PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_4PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DPDM:
		return true;

	default:
		return false;
	}
}

/*
 * convert the port-mode enum to a value we can use in the FSLSMODE
 * field of USBTLL_CHANNEL_CONF
 */
static unsigned ohci_omap3_fslsmode(enum usbhs_omap_port_mode mode)
{
	switch (mode) {
	case OMAP_USBHS_PORT_MODE_UNUSED:
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DATSE0:
		return OMAP_TLL_FSLSMODE_6PIN_PHY_DAT_SE0;

	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DPDM:
		return OMAP_TLL_FSLSMODE_6PIN_PHY_DP_DM;

	case OMAP_OHCI_PORT_MODE_PHY_3PIN_DATSE0:
		return OMAP_TLL_FSLSMODE_3PIN_PHY;

	case OMAP_OHCI_PORT_MODE_PHY_4PIN_DPDM:
		return OMAP_TLL_FSLSMODE_4PIN_PHY;

	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DATSE0:
		return OMAP_TLL_FSLSMODE_6PIN_TLL_DAT_SE0;

	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DPDM:
		return OMAP_TLL_FSLSMODE_6PIN_TLL_DP_DM;

	case OMAP_OHCI_PORT_MODE_TLL_3PIN_DATSE0:
		return OMAP_TLL_FSLSMODE_3PIN_TLL;

	case OMAP_OHCI_PORT_MODE_TLL_4PIN_DPDM:
		return OMAP_TLL_FSLSMODE_4PIN_TLL;

	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DATSE0:
		return OMAP_TLL_FSLSMODE_2PIN_TLL_DAT_SE0;

	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DPDM:
		return OMAP_TLL_FSLSMODE_2PIN_DAT_DP_DM;
	default:
		pr_warn("Invalid port mode, using default\n");
		return OMAP_TLL_FSLSMODE_6PIN_PHY_DAT_SE0;
	}
}

/**
 * usbtll_omap_probe - initialize TI-based HCDs
 *
 * Allocates basic resources for this USB host controller.
 *
 * @pdev: Pointer to this device's platform device structure
 */
static int usbtll_omap_probe(struct platform_device *pdev)
{
	struct device				*dev =  &pdev->dev;
	struct resource				*res;
	struct usbtll_omap			*tll;
	void __iomem				*base;
	int					i, nch, ver;

	dev_dbg(dev, "starting TI HSUSB TLL Controller\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	ver = usbtll_read(base, OMAP_USBTLL_REVISION);
	switch (ver) {
	case OMAP_USBTLL_REV1:
	case OMAP_USBTLL_REV4:
		nch = OMAP_TLL_CHANNEL_COUNT;
		break;
	case OMAP_USBTLL_REV2:
	case OMAP_USBTLL_REV3:
		nch = OMAP_REV2_TLL_CHANNEL_COUNT;
		break;
	default:
		nch = OMAP_TLL_CHANNEL_COUNT;
		dev_dbg(dev, "rev 0x%x not recognized, assuming %d channels\n",
			ver, nch);
		break;
	}

	tll = devm_kzalloc(dev, struct_size(tll, ch_clk, nch), GFP_KERNEL);
	if (!tll) {
		pm_runtime_put_sync(dev);
		pm_runtime_disable(dev);
		return -ENOMEM;
	}

	tll->base = base;
	tll->nch = nch;
	platform_set_drvdata(pdev, tll);

	for (i = 0; i < nch; i++) {
		char clkname[] = "usb_tll_hs_usb_chx_clk";

		snprintf(clkname, sizeof(clkname),
					"usb_tll_hs_usb_ch%d_clk", i);
		tll->ch_clk[i] = clk_get(dev, clkname);

		if (IS_ERR(tll->ch_clk[i]))
			dev_dbg(dev, "can't get clock : %s\n", clkname);
		else
			clk_prepare(tll->ch_clk[i]);
	}

	pm_runtime_put_sync(dev);
	/* only after this can omap_tll_enable/disable work */
	spin_lock(&tll_lock);
	tll_dev = dev;
	spin_unlock(&tll_lock);

	return 0;
}

/**
 * usbtll_omap_remove - shutdown processing for UHH & TLL HCDs
 * @pdev: USB Host Controller being removed
 *
 * Reverses the effect of usbtll_omap_probe().
 */
static int usbtll_omap_remove(struct platform_device *pdev)
{
	struct usbtll_omap *tll = platform_get_drvdata(pdev);
	int i;

	spin_lock(&tll_lock);
	tll_dev = NULL;
	spin_unlock(&tll_lock);

	for (i = 0; i < tll->nch; i++) {
		if (!IS_ERR(tll->ch_clk[i])) {
			clk_unprepare(tll->ch_clk[i]);
			clk_put(tll->ch_clk[i]);
		}
	}

	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct of_device_id usbtll_omap_dt_ids[] = {
	{ .compatible = "ti,usbhs-tll" },
	{ }
};

MODULE_DEVICE_TABLE(of, usbtll_omap_dt_ids);

static struct platform_driver usbtll_omap_driver = {
	.driver = {
		.name		= usbtll_driver_name,
		.of_match_table = usbtll_omap_dt_ids,
	},
	.probe		= usbtll_omap_probe,
	.remove		= usbtll_omap_remove,
};

int omap_tll_init(struct usbhs_omap_platform_data *pdata)
{
	int i;
	bool needs_tll;
	unsigned reg;
	struct usbtll_omap *tll;

	if (!tll_dev)
		return -ENODEV;

	pm_runtime_get_sync(tll_dev);

	spin_lock(&tll_lock);
	tll = dev_get_drvdata(tll_dev);
	needs_tll = false;
	for (i = 0; i < tll->nch; i++)
		needs_tll |= omap_usb_mode_needs_tll(pdata->port_mode[i]);

	if (needs_tll) {
		void __iomem *base = tll->base;

		/* Program Common TLL register */
		reg = usbtll_read(base, OMAP_TLL_SHARED_CONF);
		reg |= (OMAP_TLL_SHARED_CONF_FCLK_IS_ON
			| OMAP_TLL_SHARED_CONF_USB_DIVRATION);
		reg &= ~OMAP_TLL_SHARED_CONF_USB_90D_DDR_EN;
		reg &= ~OMAP_TLL_SHARED_CONF_USB_180D_SDR_EN;

		usbtll_write(base, OMAP_TLL_SHARED_CONF, reg);

		/* Enable channels now */
		for (i = 0; i < tll->nch; i++) {
			reg = usbtll_read(base,	OMAP_TLL_CHANNEL_CONF(i));

			if (is_ohci_port(pdata->port_mode[i])) {
				reg |= ohci_omap3_fslsmode(pdata->port_mode[i])
				<< OMAP_TLL_CHANNEL_CONF_FSLSMODE_SHIFT;
				reg |= OMAP_TLL_CHANNEL_CONF_CHANMODE_FSLS;
			} else if (pdata->port_mode[i] ==
					OMAP_EHCI_PORT_MODE_TLL) {
				/*
				 * Disable UTMI AutoIdle, BitStuffing
				 * and use SDR Mode. Enable ULPI AutoIdle.
				 */
				reg &= ~(OMAP_TLL_CHANNEL_CONF_UTMIAUTOIDLE
					| OMAP_TLL_CHANNEL_CONF_ULPIDDRMODE);
				reg |= OMAP_TLL_CHANNEL_CONF_ULPINOBITSTUFF;
				reg |= OMAP_TLL_CHANNEL_CONF_ULPI_ULPIAUTOIDLE;
			} else if (pdata->port_mode[i] ==
					OMAP_EHCI_PORT_MODE_HSIC) {
				/*
				 * HSIC Mode requires UTMI port configurations
				 */
				reg |= OMAP_TLL_CHANNEL_CONF_DRVVBUS
				 | OMAP_TLL_CHANNEL_CONF_CHRGVBUS
				 | OMAP_TLL_CHANNEL_CONF_MODE_TRANSPARENT_UTMI
				 | OMAP_TLL_CHANNEL_CONF_ULPINOBITSTUFF;
			} else {
				continue;
			}
			reg |= OMAP_TLL_CHANNEL_CONF_CHANEN;
			usbtll_write(base, OMAP_TLL_CHANNEL_CONF(i), reg);

			usbtll_writeb(base,
				      OMAP_TLL_ULPI_SCRATCH_REGISTER(i),
				      0xbe);
		}
	}

	spin_unlock(&tll_lock);
	pm_runtime_put_sync(tll_dev);

	return 0;
}
EXPORT_SYMBOL_GPL(omap_tll_init);

int omap_tll_enable(struct usbhs_omap_platform_data *pdata)
{
	int i;
	struct usbtll_omap *tll;

	if (!tll_dev)
		return -ENODEV;

	pm_runtime_get_sync(tll_dev);

	spin_lock(&tll_lock);
	tll = dev_get_drvdata(tll_dev);

	for (i = 0; i < tll->nch; i++) {
		if (omap_usb_mode_needs_tll(pdata->port_mode[i])) {
			int r;

			if (IS_ERR(tll->ch_clk[i]))
				continue;

			r = clk_enable(tll->ch_clk[i]);
			if (r) {
				dev_err(tll_dev,
				 "Error enabling ch %d clock: %d\n", i, r);
			}
		}
	}

	spin_unlock(&tll_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(omap_tll_enable);

int omap_tll_disable(struct usbhs_omap_platform_data *pdata)
{
	int i;
	struct usbtll_omap *tll;

	if (!tll_dev)
		return -ENODEV;

	spin_lock(&tll_lock);
	tll = dev_get_drvdata(tll_dev);

	for (i = 0; i < tll->nch; i++) {
		if (omap_usb_mode_needs_tll(pdata->port_mode[i])) {
			if (!IS_ERR(tll->ch_clk[i]))
				clk_disable(tll->ch_clk[i]);
		}
	}

	spin_unlock(&tll_lock);
	pm_runtime_put_sync(tll_dev);

	return 0;
}
EXPORT_SYMBOL_GPL(omap_tll_disable);

MODULE_AUTHOR("Keshava Munegowda <keshava_mgowda@ti.com>");
MODULE_AUTHOR("Roger Quadros <rogerq@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("usb tll driver for TI OMAP EHCI and OHCI controllers");

static int __init omap_usbtll_drvinit(void)
{
	return platform_driver_register(&usbtll_omap_driver);
}

/*
 * init before usbhs core driver;
 * The usbtll driver should be initialized before
 * the usbhs core driver probe function is called.
 */
fs_initcall(omap_usbtll_drvinit);

static void __exit omap_usbtll_drvexit(void)
{
	platform_driver_unregister(&usbtll_omap_driver);
}
module_exit(omap_usbtll_drvexit);
