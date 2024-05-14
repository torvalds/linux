// SPDX-License-Identifier: GPL-2.0-only
/*
 * omap-usb-host.c - The USBHS core driver for OMAP EHCI & OHCI
 *
 * Copyright (C) 2011-2013 Texas Instruments Incorporated - https://www.ti.com
 * Author: Keshava Munegowda <keshava_mgowda@ti.com>
 * Author: Roger Quadros <rogerq@ti.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/platform_data/usb-omap.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/err.h>

#include "omap-usb.h"

#define USBHS_DRIVER_NAME	"usbhs_omap"
#define OMAP_EHCI_DEVICE	"ehci-omap"
#define OMAP_OHCI_DEVICE	"ohci-omap3"

/* OMAP USBHOST Register addresses  */

/* UHH Register Set */
#define	OMAP_UHH_REVISION				(0x00)
#define	OMAP_UHH_SYSCONFIG				(0x10)
#define	OMAP_UHH_SYSCONFIG_MIDLEMODE			(1 << 12)
#define	OMAP_UHH_SYSCONFIG_CACTIVITY			(1 << 8)
#define	OMAP_UHH_SYSCONFIG_SIDLEMODE			(1 << 3)
#define	OMAP_UHH_SYSCONFIG_ENAWAKEUP			(1 << 2)
#define	OMAP_UHH_SYSCONFIG_SOFTRESET			(1 << 1)
#define	OMAP_UHH_SYSCONFIG_AUTOIDLE			(1 << 0)

#define	OMAP_UHH_SYSSTATUS				(0x14)
#define	OMAP_UHH_HOSTCONFIG				(0x40)
#define	OMAP_UHH_HOSTCONFIG_ULPI_BYPASS			(1 << 0)
#define	OMAP_UHH_HOSTCONFIG_ULPI_P1_BYPASS		(1 << 0)
#define	OMAP_UHH_HOSTCONFIG_ULPI_P2_BYPASS		(1 << 11)
#define	OMAP_UHH_HOSTCONFIG_ULPI_P3_BYPASS		(1 << 12)
#define OMAP_UHH_HOSTCONFIG_INCR4_BURST_EN		(1 << 2)
#define OMAP_UHH_HOSTCONFIG_INCR8_BURST_EN		(1 << 3)
#define OMAP_UHH_HOSTCONFIG_INCR16_BURST_EN		(1 << 4)
#define OMAP_UHH_HOSTCONFIG_INCRX_ALIGN_EN		(1 << 5)
#define OMAP_UHH_HOSTCONFIG_P1_CONNECT_STATUS		(1 << 8)
#define OMAP_UHH_HOSTCONFIG_P2_CONNECT_STATUS		(1 << 9)
#define OMAP_UHH_HOSTCONFIG_P3_CONNECT_STATUS		(1 << 10)
#define OMAP4_UHH_HOSTCONFIG_APP_START_CLK		(1 << 31)

/* OMAP4-specific defines */
#define OMAP4_UHH_SYSCONFIG_IDLEMODE_CLEAR		(3 << 2)
#define OMAP4_UHH_SYSCONFIG_NOIDLE			(1 << 2)
#define OMAP4_UHH_SYSCONFIG_STDBYMODE_CLEAR		(3 << 4)
#define OMAP4_UHH_SYSCONFIG_NOSTDBY			(1 << 4)
#define OMAP4_UHH_SYSCONFIG_SOFTRESET			(1 << 0)

#define OMAP4_P1_MODE_CLEAR				(3 << 16)
#define OMAP4_P1_MODE_TLL				(1 << 16)
#define OMAP4_P1_MODE_HSIC				(3 << 16)
#define OMAP4_P2_MODE_CLEAR				(3 << 18)
#define OMAP4_P2_MODE_TLL				(1 << 18)
#define OMAP4_P2_MODE_HSIC				(3 << 18)

#define	OMAP_UHH_DEBUG_CSR				(0x44)

/* Values of UHH_REVISION - Note: these are not given in the TRM */
#define OMAP_USBHS_REV1		0x00000010	/* OMAP3 */
#define OMAP_USBHS_REV2		0x50700100	/* OMAP4 */

#define is_omap_usbhs_rev1(x)	(x->usbhs_rev == OMAP_USBHS_REV1)
#define is_omap_usbhs_rev2(x)	(x->usbhs_rev == OMAP_USBHS_REV2)

#define is_ehci_phy_mode(x)	(x == OMAP_EHCI_PORT_MODE_PHY)
#define is_ehci_tll_mode(x)	(x == OMAP_EHCI_PORT_MODE_TLL)
#define is_ehci_hsic_mode(x)	(x == OMAP_EHCI_PORT_MODE_HSIC)


struct usbhs_hcd_omap {
	int				nports;
	struct clk			**utmi_clk;
	struct clk			**hsic60m_clk;
	struct clk			**hsic480m_clk;

	struct clk			*xclk60mhsp1_ck;
	struct clk			*xclk60mhsp2_ck;
	struct clk			*utmi_p1_gfclk;
	struct clk			*utmi_p2_gfclk;
	struct clk			*init_60m_fclk;
	struct clk			*ehci_logic_fck;

	void __iomem			*uhh_base;

	struct usbhs_omap_platform_data	*pdata;

	u32				usbhs_rev;
};
/*-------------------------------------------------------------------------*/

static const char usbhs_driver_name[] = USBHS_DRIVER_NAME;
static u64 usbhs_dmamask = DMA_BIT_MASK(32);

/*-------------------------------------------------------------------------*/

static inline void usbhs_write(void __iomem *base, u32 reg, u32 val)
{
	writel_relaxed(val, base + reg);
}

static inline u32 usbhs_read(void __iomem *base, u32 reg)
{
	return readl_relaxed(base + reg);
}

/*-------------------------------------------------------------------------*/

/*
 * Map 'enum usbhs_omap_port_mode' found in <linux/platform_data/usb-omap.h>
 * to the device tree binding portN-mode found in
 * 'Documentation/devicetree/bindings/mfd/omap-usb-host.txt'
 */
static const char * const port_modes[] = {
	[OMAP_USBHS_PORT_MODE_UNUSED]	= "",
	[OMAP_EHCI_PORT_MODE_PHY]	= "ehci-phy",
	[OMAP_EHCI_PORT_MODE_TLL]	= "ehci-tll",
	[OMAP_EHCI_PORT_MODE_HSIC]	= "ehci-hsic",
	[OMAP_OHCI_PORT_MODE_PHY_6PIN_DATSE0]	= "ohci-phy-6pin-datse0",
	[OMAP_OHCI_PORT_MODE_PHY_6PIN_DPDM]	= "ohci-phy-6pin-dpdm",
	[OMAP_OHCI_PORT_MODE_PHY_3PIN_DATSE0]	= "ohci-phy-3pin-datse0",
	[OMAP_OHCI_PORT_MODE_PHY_4PIN_DPDM]	= "ohci-phy-4pin-dpdm",
	[OMAP_OHCI_PORT_MODE_TLL_6PIN_DATSE0]	= "ohci-tll-6pin-datse0",
	[OMAP_OHCI_PORT_MODE_TLL_6PIN_DPDM]	= "ohci-tll-6pin-dpdm",
	[OMAP_OHCI_PORT_MODE_TLL_3PIN_DATSE0]	= "ohci-tll-3pin-datse0",
	[OMAP_OHCI_PORT_MODE_TLL_4PIN_DPDM]	= "ohci-tll-4pin-dpdm",
	[OMAP_OHCI_PORT_MODE_TLL_2PIN_DATSE0]	= "ohci-tll-2pin-datse0",
	[OMAP_OHCI_PORT_MODE_TLL_2PIN_DPDM]	= "ohci-tll-2pin-dpdm",
};

static struct platform_device *omap_usbhs_alloc_child(const char *name,
			struct resource	*res, int num_resources, void *pdata,
			size_t pdata_size, struct device *dev)
{
	struct platform_device	*child;
	int			ret;

	child = platform_device_alloc(name, 0);

	if (!child) {
		dev_err(dev, "platform_device_alloc %s failed\n", name);
		goto err_end;
	}

	ret = platform_device_add_resources(child, res, num_resources);
	if (ret) {
		dev_err(dev, "platform_device_add_resources failed\n");
		goto err_alloc;
	}

	ret = platform_device_add_data(child, pdata, pdata_size);
	if (ret) {
		dev_err(dev, "platform_device_add_data failed\n");
		goto err_alloc;
	}

	child->dev.dma_mask		= &usbhs_dmamask;
	dma_set_coherent_mask(&child->dev, DMA_BIT_MASK(32));
	child->dev.parent		= dev;

	ret = platform_device_add(child);
	if (ret) {
		dev_err(dev, "platform_device_add failed\n");
		goto err_alloc;
	}

	return child;

err_alloc:
	platform_device_put(child);

err_end:
	return NULL;
}

static int omap_usbhs_alloc_children(struct platform_device *pdev)
{
	struct device				*dev = &pdev->dev;
	struct usbhs_omap_platform_data		*pdata = dev_get_platdata(dev);
	struct platform_device			*ehci;
	struct platform_device			*ohci;
	struct resource				*res;
	struct resource				resources[2];
	int					ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ehci");
	if (!res) {
		dev_err(dev, "EHCI get resource IORESOURCE_MEM failed\n");
		ret = -ENODEV;
		goto err_end;
	}
	resources[0] = *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "ehci-irq");
	if (!res) {
		dev_err(dev, " EHCI get resource IORESOURCE_IRQ failed\n");
		ret = -ENODEV;
		goto err_end;
	}
	resources[1] = *res;

	ehci = omap_usbhs_alloc_child(OMAP_EHCI_DEVICE, resources, 2, pdata,
		sizeof(*pdata), dev);

	if (!ehci) {
		dev_err(dev, "omap_usbhs_alloc_child failed\n");
		ret = -ENOMEM;
		goto err_end;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ohci");
	if (!res) {
		dev_err(dev, "OHCI get resource IORESOURCE_MEM failed\n");
		ret = -ENODEV;
		goto err_ehci;
	}
	resources[0] = *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "ohci-irq");
	if (!res) {
		dev_err(dev, "OHCI get resource IORESOURCE_IRQ failed\n");
		ret = -ENODEV;
		goto err_ehci;
	}
	resources[1] = *res;

	ohci = omap_usbhs_alloc_child(OMAP_OHCI_DEVICE, resources, 2, pdata,
		sizeof(*pdata), dev);
	if (!ohci) {
		dev_err(dev, "omap_usbhs_alloc_child failed\n");
		ret = -ENOMEM;
		goto err_ehci;
	}

	return 0;

err_ehci:
	platform_device_unregister(ehci);

err_end:
	return ret;
}

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

static int usbhs_runtime_resume(struct device *dev)
{
	struct usbhs_hcd_omap		*omap = dev_get_drvdata(dev);
	struct usbhs_omap_platform_data	*pdata = omap->pdata;
	int i, r;

	dev_dbg(dev, "usbhs_runtime_resume\n");

	omap_tll_enable(pdata);

	if (!IS_ERR(omap->ehci_logic_fck))
		clk_prepare_enable(omap->ehci_logic_fck);

	for (i = 0; i < omap->nports; i++) {
		switch (pdata->port_mode[i]) {
		case OMAP_EHCI_PORT_MODE_HSIC:
			if (!IS_ERR(omap->hsic60m_clk[i])) {
				r = clk_prepare_enable(omap->hsic60m_clk[i]);
				if (r) {
					dev_err(dev,
					 "Can't enable port %d hsic60m clk:%d\n",
					 i, r);
				}
			}

			if (!IS_ERR(omap->hsic480m_clk[i])) {
				r = clk_prepare_enable(omap->hsic480m_clk[i]);
				if (r) {
					dev_err(dev,
					 "Can't enable port %d hsic480m clk:%d\n",
					 i, r);
				}
			}
			fallthrough;	/* as HSIC mode needs utmi_clk */

		case OMAP_EHCI_PORT_MODE_TLL:
			if (!IS_ERR(omap->utmi_clk[i])) {
				r = clk_prepare_enable(omap->utmi_clk[i]);
				if (r) {
					dev_err(dev,
					 "Can't enable port %d clk : %d\n",
					 i, r);
				}
			}
			break;
		default:
			break;
		}
	}

	return 0;
}

static int usbhs_runtime_suspend(struct device *dev)
{
	struct usbhs_hcd_omap		*omap = dev_get_drvdata(dev);
	struct usbhs_omap_platform_data	*pdata = omap->pdata;
	int i;

	dev_dbg(dev, "usbhs_runtime_suspend\n");

	for (i = 0; i < omap->nports; i++) {
		switch (pdata->port_mode[i]) {
		case OMAP_EHCI_PORT_MODE_HSIC:
			if (!IS_ERR(omap->hsic60m_clk[i]))
				clk_disable_unprepare(omap->hsic60m_clk[i]);

			if (!IS_ERR(omap->hsic480m_clk[i]))
				clk_disable_unprepare(omap->hsic480m_clk[i]);
			fallthrough;	/* as utmi_clks were used in HSIC mode */

		case OMAP_EHCI_PORT_MODE_TLL:
			if (!IS_ERR(omap->utmi_clk[i]))
				clk_disable_unprepare(omap->utmi_clk[i]);
			break;
		default:
			break;
		}
	}

	if (!IS_ERR(omap->ehci_logic_fck))
		clk_disable_unprepare(omap->ehci_logic_fck);

	omap_tll_disable(pdata);

	return 0;
}

static unsigned omap_usbhs_rev1_hostconfig(struct usbhs_hcd_omap *omap,
						unsigned reg)
{
	struct usbhs_omap_platform_data	*pdata = omap->pdata;
	int i;

	for (i = 0; i < omap->nports; i++) {
		switch (pdata->port_mode[i]) {
		case OMAP_USBHS_PORT_MODE_UNUSED:
			reg &= ~(OMAP_UHH_HOSTCONFIG_P1_CONNECT_STATUS << i);
			break;
		case OMAP_EHCI_PORT_MODE_PHY:
			if (pdata->single_ulpi_bypass)
				break;

			if (i == 0)
				reg &= ~OMAP_UHH_HOSTCONFIG_ULPI_P1_BYPASS;
			else
				reg &= ~(OMAP_UHH_HOSTCONFIG_ULPI_P2_BYPASS
								<< (i-1));
			break;
		default:
			if (pdata->single_ulpi_bypass)
				break;

			if (i == 0)
				reg |= OMAP_UHH_HOSTCONFIG_ULPI_P1_BYPASS;
			else
				reg |= OMAP_UHH_HOSTCONFIG_ULPI_P2_BYPASS
								<< (i-1);
			break;
		}
	}

	if (pdata->single_ulpi_bypass) {
		/* bypass ULPI only if none of the ports use PHY mode */
		reg |= OMAP_UHH_HOSTCONFIG_ULPI_BYPASS;

		for (i = 0; i < omap->nports; i++) {
			if (is_ehci_phy_mode(pdata->port_mode[i])) {
				reg &= ~OMAP_UHH_HOSTCONFIG_ULPI_BYPASS;
				break;
			}
		}
	}

	return reg;
}

static unsigned omap_usbhs_rev2_hostconfig(struct usbhs_hcd_omap *omap,
						unsigned reg)
{
	struct usbhs_omap_platform_data	*pdata = omap->pdata;
	int i;

	for (i = 0; i < omap->nports; i++) {
		/* Clear port mode fields for PHY mode */
		reg &= ~(OMAP4_P1_MODE_CLEAR << 2 * i);

		if (is_ehci_tll_mode(pdata->port_mode[i]) ||
				(is_ohci_port(pdata->port_mode[i])))
			reg |= OMAP4_P1_MODE_TLL << 2 * i;
		else if (is_ehci_hsic_mode(pdata->port_mode[i]))
			reg |= OMAP4_P1_MODE_HSIC << 2 * i;
	}

	return reg;
}

static void omap_usbhs_init(struct device *dev)
{
	struct usbhs_hcd_omap		*omap = dev_get_drvdata(dev);
	unsigned			reg;

	dev_dbg(dev, "starting TI HSUSB Controller\n");

	pm_runtime_get_sync(dev);

	reg = usbhs_read(omap->uhh_base, OMAP_UHH_HOSTCONFIG);
	/* setup ULPI bypass and burst configurations */
	reg |= (OMAP_UHH_HOSTCONFIG_INCR4_BURST_EN
			| OMAP_UHH_HOSTCONFIG_INCR8_BURST_EN
			| OMAP_UHH_HOSTCONFIG_INCR16_BURST_EN);
	reg |= OMAP4_UHH_HOSTCONFIG_APP_START_CLK;
	reg &= ~OMAP_UHH_HOSTCONFIG_INCRX_ALIGN_EN;

	switch (omap->usbhs_rev) {
	case OMAP_USBHS_REV1:
		reg = omap_usbhs_rev1_hostconfig(omap, reg);
		break;

	case OMAP_USBHS_REV2:
		reg = omap_usbhs_rev2_hostconfig(omap, reg);
		break;

	default:	/* newer revisions */
		reg = omap_usbhs_rev2_hostconfig(omap, reg);
		break;
	}

	usbhs_write(omap->uhh_base, OMAP_UHH_HOSTCONFIG, reg);
	dev_dbg(dev, "UHH setup done, uhh_hostconfig=%x\n", reg);

	pm_runtime_put_sync(dev);
}

static int usbhs_omap_get_dt_pdata(struct device *dev,
					struct usbhs_omap_platform_data *pdata)
{
	int ret, i;
	struct device_node *node = dev->of_node;

	ret = of_property_read_u32(node, "num-ports", &pdata->nports);
	if (ret)
		pdata->nports = 0;

	if (pdata->nports > OMAP3_HS_USB_PORTS) {
		dev_warn(dev, "Too many num_ports <%d> in device tree. Max %d\n",
				pdata->nports, OMAP3_HS_USB_PORTS);
		return -ENODEV;
	}

	/* get port modes */
	for (i = 0; i < OMAP3_HS_USB_PORTS; i++) {
		char prop[11];
		const char *mode;

		pdata->port_mode[i] = OMAP_USBHS_PORT_MODE_UNUSED;

		snprintf(prop, sizeof(prop), "port%d-mode", i + 1);
		ret = of_property_read_string(node, prop, &mode);
		if (ret < 0)
			continue;

		/* get 'enum usbhs_omap_port_mode' from port mode string */
		ret = match_string(port_modes, ARRAY_SIZE(port_modes), mode);
		if (ret < 0) {
			dev_warn(dev, "Invalid port%d-mode \"%s\" in device tree\n",
					i, mode);
			return -ENODEV;
		}

		dev_dbg(dev, "port%d-mode: %s -> %d\n", i, mode, ret);
		pdata->port_mode[i] = ret;
	}

	/* get flags */
	pdata->single_ulpi_bypass = of_property_read_bool(node,
						"single-ulpi-bypass");

	return 0;
}

static const struct of_device_id usbhs_child_match_table[] = {
	{ .compatible = "ti,ehci-omap", },
	{ .compatible = "ti,ohci-omap3", },
	{ }
};

/**
 * usbhs_omap_probe - initialize TI-based HCDs
 *
 * Allocates basic resources for this USB host controller.
 *
 * @pdev: Pointer to this device's platform device structure
 */
static int usbhs_omap_probe(struct platform_device *pdev)
{
	struct device			*dev =  &pdev->dev;
	struct usbhs_omap_platform_data	*pdata = dev_get_platdata(dev);
	struct usbhs_hcd_omap		*omap;
	struct resource			*res;
	int				ret = 0;
	int				i;
	bool				need_logic_fck;

	if (dev->of_node) {
		/* For DT boot we populate platform data from OF node */
		pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		ret = usbhs_omap_get_dt_pdata(dev, pdata);
		if (ret)
			return ret;

		dev->platform_data = pdata;
	}

	if (!pdata) {
		dev_err(dev, "Missing platform data\n");
		return -ENODEV;
	}

	if (pdata->nports > OMAP3_HS_USB_PORTS) {
		dev_info(dev, "Too many num_ports <%d> in platform_data. Max %d\n",
				pdata->nports, OMAP3_HS_USB_PORTS);
		return -ENODEV;
	}

	omap = devm_kzalloc(dev, sizeof(*omap), GFP_KERNEL);
	if (!omap) {
		dev_err(dev, "Memory allocation failed\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	omap->uhh_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(omap->uhh_base))
		return PTR_ERR(omap->uhh_base);

	omap->pdata = pdata;

	/* Initialize the TLL subsystem */
	omap_tll_init(pdata);

	pm_runtime_enable(dev);

	platform_set_drvdata(pdev, omap);
	pm_runtime_get_sync(dev);

	omap->usbhs_rev = usbhs_read(omap->uhh_base, OMAP_UHH_REVISION);

	/* we need to call runtime suspend before we update omap->nports
	 * to prevent unbalanced clk_disable()
	 */
	pm_runtime_put_sync(dev);

	/*
	 * If platform data contains nports then use that
	 * else make out number of ports from USBHS revision
	 */
	if (pdata->nports) {
		omap->nports = pdata->nports;
	} else {
		switch (omap->usbhs_rev) {
		case OMAP_USBHS_REV1:
			omap->nports = 3;
			break;
		case OMAP_USBHS_REV2:
			omap->nports = 2;
			break;
		default:
			omap->nports = OMAP3_HS_USB_PORTS;
			dev_dbg(dev,
			 "USB HOST Rev:0x%x not recognized, assuming %d ports\n",
			 omap->usbhs_rev, omap->nports);
			break;
		}
		pdata->nports = omap->nports;
	}

	i = sizeof(struct clk *) * omap->nports;
	omap->utmi_clk = devm_kzalloc(dev, i, GFP_KERNEL);
	omap->hsic480m_clk = devm_kzalloc(dev, i, GFP_KERNEL);
	omap->hsic60m_clk = devm_kzalloc(dev, i, GFP_KERNEL);

	if (!omap->utmi_clk || !omap->hsic480m_clk || !omap->hsic60m_clk) {
		dev_err(dev, "Memory allocation failed\n");
		ret = -ENOMEM;
		goto err_mem;
	}

	/* Set all clocks as invalid to begin with */
	omap->ehci_logic_fck = ERR_PTR(-ENODEV);
	omap->init_60m_fclk = ERR_PTR(-ENODEV);
	omap->utmi_p1_gfclk = ERR_PTR(-ENODEV);
	omap->utmi_p2_gfclk = ERR_PTR(-ENODEV);
	omap->xclk60mhsp1_ck = ERR_PTR(-ENODEV);
	omap->xclk60mhsp2_ck = ERR_PTR(-ENODEV);

	for (i = 0; i < omap->nports; i++) {
		omap->utmi_clk[i] = ERR_PTR(-ENODEV);
		omap->hsic480m_clk[i] = ERR_PTR(-ENODEV);
		omap->hsic60m_clk[i] = ERR_PTR(-ENODEV);
	}

	/* for OMAP3 i.e. USBHS REV1 */
	if (omap->usbhs_rev == OMAP_USBHS_REV1) {
		need_logic_fck = false;
		for (i = 0; i < omap->nports; i++) {
			if (is_ehci_phy_mode(pdata->port_mode[i]) ||
			    is_ehci_tll_mode(pdata->port_mode[i]) ||
			    is_ehci_hsic_mode(pdata->port_mode[i]))

				need_logic_fck |= true;
		}

		if (need_logic_fck) {
			omap->ehci_logic_fck = devm_clk_get(dev,
							    "usbhost_120m_fck");
			if (IS_ERR(omap->ehci_logic_fck)) {
				ret = PTR_ERR(omap->ehci_logic_fck);
				dev_err(dev, "usbhost_120m_fck failed:%d\n",
					ret);
				goto err_mem;
			}
		}
		goto initialize;
	}

	/* for OMAP4+ i.e. USBHS REV2+ */
	omap->utmi_p1_gfclk = devm_clk_get(dev, "utmi_p1_gfclk");
	if (IS_ERR(omap->utmi_p1_gfclk)) {
		ret = PTR_ERR(omap->utmi_p1_gfclk);
		dev_err(dev, "utmi_p1_gfclk failed error:%d\n", ret);
		goto err_mem;
	}

	omap->utmi_p2_gfclk = devm_clk_get(dev, "utmi_p2_gfclk");
	if (IS_ERR(omap->utmi_p2_gfclk)) {
		ret = PTR_ERR(omap->utmi_p2_gfclk);
		dev_err(dev, "utmi_p2_gfclk failed error:%d\n", ret);
		goto err_mem;
	}

	omap->xclk60mhsp1_ck = devm_clk_get(dev, "refclk_60m_ext_p1");
	if (IS_ERR(omap->xclk60mhsp1_ck)) {
		ret = PTR_ERR(omap->xclk60mhsp1_ck);
		dev_err(dev, "refclk_60m_ext_p1 failed error:%d\n", ret);
		goto err_mem;
	}

	omap->xclk60mhsp2_ck = devm_clk_get(dev, "refclk_60m_ext_p2");
	if (IS_ERR(omap->xclk60mhsp2_ck)) {
		ret = PTR_ERR(omap->xclk60mhsp2_ck);
		dev_err(dev, "refclk_60m_ext_p2 failed error:%d\n", ret);
		goto err_mem;
	}

	omap->init_60m_fclk = devm_clk_get(dev, "refclk_60m_int");
	if (IS_ERR(omap->init_60m_fclk)) {
		ret = PTR_ERR(omap->init_60m_fclk);
		dev_err(dev, "refclk_60m_int failed error:%d\n", ret);
		goto err_mem;
	}

	for (i = 0; i < omap->nports; i++) {
		char clkname[30];

		/* clock names are indexed from 1*/
		snprintf(clkname, sizeof(clkname),
				"usb_host_hs_utmi_p%d_clk", i + 1);

		/* If a clock is not found we won't bail out as not all
		 * platforms have all clocks and we can function without
		 * them
		 */
		omap->utmi_clk[i] = devm_clk_get(dev, clkname);
		if (IS_ERR(omap->utmi_clk[i])) {
			ret = PTR_ERR(omap->utmi_clk[i]);
			dev_err(dev, "Failed to get clock : %s : %d\n",
				clkname, ret);
			goto err_mem;
		}

		snprintf(clkname, sizeof(clkname),
				"usb_host_hs_hsic480m_p%d_clk", i + 1);
		omap->hsic480m_clk[i] = devm_clk_get(dev, clkname);
		if (IS_ERR(omap->hsic480m_clk[i])) {
			ret = PTR_ERR(omap->hsic480m_clk[i]);
			dev_err(dev, "Failed to get clock : %s : %d\n",
				clkname, ret);
			goto err_mem;
		}

		snprintf(clkname, sizeof(clkname),
				"usb_host_hs_hsic60m_p%d_clk", i + 1);
		omap->hsic60m_clk[i] = devm_clk_get(dev, clkname);
		if (IS_ERR(omap->hsic60m_clk[i])) {
			ret = PTR_ERR(omap->hsic60m_clk[i]);
			dev_err(dev, "Failed to get clock : %s : %d\n",
				clkname, ret);
			goto err_mem;
		}
	}

	if (is_ehci_phy_mode(pdata->port_mode[0])) {
		ret = clk_set_parent(omap->utmi_p1_gfclk,
					omap->xclk60mhsp1_ck);
		if (ret != 0) {
			dev_err(dev, "xclk60mhsp1_ck set parent failed: %d\n",
				ret);
			goto err_mem;
		}
	} else if (is_ehci_tll_mode(pdata->port_mode[0])) {
		ret = clk_set_parent(omap->utmi_p1_gfclk,
					omap->init_60m_fclk);
		if (ret != 0) {
			dev_err(dev, "P0 init_60m_fclk set parent failed: %d\n",
				ret);
			goto err_mem;
		}
	}

	if (is_ehci_phy_mode(pdata->port_mode[1])) {
		ret = clk_set_parent(omap->utmi_p2_gfclk,
					omap->xclk60mhsp2_ck);
		if (ret != 0) {
			dev_err(dev, "xclk60mhsp2_ck set parent failed: %d\n",
				ret);
			goto err_mem;
		}
	} else if (is_ehci_tll_mode(pdata->port_mode[1])) {
		ret = clk_set_parent(omap->utmi_p2_gfclk,
						omap->init_60m_fclk);
		if (ret != 0) {
			dev_err(dev, "P1 init_60m_fclk set parent failed: %d\n",
				ret);
			goto err_mem;
		}
	}

initialize:
	omap_usbhs_init(dev);

	if (dev->of_node) {
		ret = of_platform_populate(dev->of_node,
				usbhs_child_match_table, NULL, dev);

		if (ret) {
			dev_err(dev, "Failed to create DT children: %d\n", ret);
			goto err_mem;
		}

	} else {
		ret = omap_usbhs_alloc_children(pdev);
		if (ret) {
			dev_err(dev, "omap_usbhs_alloc_children failed: %d\n",
						ret);
			goto err_mem;
		}
	}

	return 0;

err_mem:
	pm_runtime_disable(dev);

	return ret;
}

static int usbhs_omap_remove_child(struct device *dev, void *data)
{
	dev_info(dev, "unregistering\n");
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

/**
 * usbhs_omap_remove - shutdown processing for UHH & TLL HCDs
 * @pdev: USB Host Controller being removed
 *
 * Reverses the effect of usbhs_omap_probe().
 */
static int usbhs_omap_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	/* remove children */
	device_for_each_child(&pdev->dev, NULL, usbhs_omap_remove_child);
	return 0;
}

static const struct dev_pm_ops usbhsomap_dev_pm_ops = {
	.runtime_suspend	= usbhs_runtime_suspend,
	.runtime_resume		= usbhs_runtime_resume,
};

static const struct of_device_id usbhs_omap_dt_ids[] = {
	{ .compatible = "ti,usbhs-host" },
	{ }
};

MODULE_DEVICE_TABLE(of, usbhs_omap_dt_ids);


static struct platform_driver usbhs_omap_driver = {
	.driver = {
		.name		= usbhs_driver_name,
		.pm		= &usbhsomap_dev_pm_ops,
		.of_match_table = usbhs_omap_dt_ids,
	},
	.probe		= usbhs_omap_probe,
	.remove		= usbhs_omap_remove,
};

MODULE_AUTHOR("Keshava Munegowda <keshava_mgowda@ti.com>");
MODULE_AUTHOR("Roger Quadros <rogerq@ti.com>");
MODULE_ALIAS("platform:" USBHS_DRIVER_NAME);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("usb host common core driver for omap EHCI and OHCI");

static int omap_usbhs_drvinit(void)
{
	return platform_driver_register(&usbhs_omap_driver);
}

/*
 * init before ehci and ohci drivers;
 * The usbhs core driver should be initialized much before
 * the omap ehci and ohci probe functions are called.
 * This usbhs core driver should be initialized after
 * usb tll driver
 */
fs_initcall_sync(omap_usbhs_drvinit);

static void omap_usbhs_drvexit(void)
{
	platform_driver_unregister(&usbhs_omap_driver);
}
module_exit(omap_usbhs_drvexit);
