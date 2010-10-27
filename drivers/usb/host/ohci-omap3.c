/*
 * ohci-omap3.c - driver for OHCI on OMAP3 and later processors
 *
 * Bus Glue for OMAP3 USBHOST 3 port OHCI controller
 * This controller is also used in later OMAPs and AM35x chips
 *
 * Copyright (C) 2007-2010 Texas Instruments, Inc.
 * Author: Vikram Pandita <vikram.pandita@ti.com>
 * Author: Anand Gadiyar <gadiyar@ti.com>
 *
 * Based on ehci-omap.c and some other ohci glue layers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * TODO (last updated Mar 10th, 2010):
 *	- add kernel-doc
 *	- Factor out code common to EHCI to a separate file
 *	- Make EHCI and OHCI coexist together
 *	  - needs newer silicon versions to actually work
 *	  - the last one to be loaded currently steps on the other's toes
 *	- Add hooks for configuring transceivers, etc. at init/exit
 *	- Add aggressive clock-management code
 */

#include <linux/platform_device.h>
#include <linux/clk.h>

#include <plat/usb.h>

/*
 * OMAP USBHOST Register addresses: VIRTUAL ADDRESSES
 *	Use ohci_omap_readl()/ohci_omap_writel() functions
 */

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
#define	OMAP_TLL_CHANNEL_CONF_ULPINOBITSTUFF		(1 << 11)
#define	OMAP_TLL_CHANNEL_CONF_ULPI_ULPIAUTOIDLE		(1 << 10)
#define	OMAP_TLL_CHANNEL_CONF_UTMIAUTOIDLE		(1 << 9)
#define	OMAP_TLL_CHANNEL_CONF_ULPIDDRMODE		(1 << 8)
#define OMAP_TLL_CHANNEL_CONF_CHANMODE_FSLS		(1 << 1)
#define	OMAP_TLL_CHANNEL_CONF_CHANEN			(1 << 0)

#define OMAP_TLL_CHANNEL_COUNT				3

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
#define OMAP_UHH_SYSSTATUS_UHHRESETDONE			(1 << 0)
#define OMAP_UHH_SYSSTATUS_OHCIRESETDONE		(1 << 1)
#define OMAP_UHH_SYSSTATUS_EHCIRESETDONE		(1 << 2)
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

#define	OMAP_UHH_DEBUG_CSR				(0x44)

/*-------------------------------------------------------------------------*/

static inline void ohci_omap_writel(void __iomem *base, u32 reg, u32 val)
{
	__raw_writel(val, base + reg);
}

static inline u32 ohci_omap_readl(void __iomem *base, u32 reg)
{
	return __raw_readl(base + reg);
}

static inline void ohci_omap_writeb(void __iomem *base, u8 reg, u8 val)
{
	__raw_writeb(val, base + reg);
}

static inline u8 ohci_omap_readb(void __iomem *base, u8 reg)
{
	return __raw_readb(base + reg);
}

/*-------------------------------------------------------------------------*/

struct ohci_hcd_omap3 {
	struct ohci_hcd		*ohci;
	struct device		*dev;

	struct clk		*usbhost_ick;
	struct clk		*usbhost2_120m_fck;
	struct clk		*usbhost1_48m_fck;
	struct clk		*usbtll_fck;
	struct clk		*usbtll_ick;

	/* port_mode: TLL/PHY, 2/3/4/6-PIN, DP-DM/DAT-SE0 */
	enum ohci_omap3_port_mode	port_mode[OMAP3_HS_USB_PORTS];
	void __iomem		*uhh_base;
	void __iomem		*tll_base;
	void __iomem		*ohci_base;

	unsigned		es2_compatibility:1;
};

/*-------------------------------------------------------------------------*/

static void ohci_omap3_clock_power(struct ohci_hcd_omap3 *omap, int on)
{
	if (on) {
		clk_enable(omap->usbtll_ick);
		clk_enable(omap->usbtll_fck);
		clk_enable(omap->usbhost_ick);
		clk_enable(omap->usbhost1_48m_fck);
		clk_enable(omap->usbhost2_120m_fck);
	} else {
		clk_disable(omap->usbhost2_120m_fck);
		clk_disable(omap->usbhost1_48m_fck);
		clk_disable(omap->usbhost_ick);
		clk_disable(omap->usbtll_fck);
		clk_disable(omap->usbtll_ick);
	}
}

static int ohci_omap3_init(struct usb_hcd *hcd)
{
	dev_dbg(hcd->self.controller, "starting OHCI controller\n");

	return ohci_init(hcd_to_ohci(hcd));
}


/*-------------------------------------------------------------------------*/

static int ohci_omap3_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	/*
	 * RemoteWakeupConnected has to be set explicitly before
	 * calling ohci_run. The reset value of RWC is 0.
	 */
	ohci->hc_control = OHCI_CTRL_RWC;
	writel(OHCI_CTRL_RWC, &ohci->regs->control);

	ret = ohci_run(ohci);

	if (ret < 0) {
		dev_err(hcd->self.controller, "can't start\n");
		ohci_stop(hcd);
	}

	return ret;
}

/*-------------------------------------------------------------------------*/

/*
 * convert the port-mode enum to a value we can use in the FSLSMODE
 * field of USBTLL_CHANNEL_CONF
 */
static unsigned ohci_omap3_fslsmode(enum ohci_omap3_port_mode mode)
{
	switch (mode) {
	case OMAP_OHCI_PORT_MODE_UNUSED:
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DATSE0:
		return 0x0;

	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DPDM:
		return 0x1;

	case OMAP_OHCI_PORT_MODE_PHY_3PIN_DATSE0:
		return 0x2;

	case OMAP_OHCI_PORT_MODE_PHY_4PIN_DPDM:
		return 0x3;

	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DATSE0:
		return 0x4;

	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DPDM:
		return 0x5;

	case OMAP_OHCI_PORT_MODE_TLL_3PIN_DATSE0:
		return 0x6;

	case OMAP_OHCI_PORT_MODE_TLL_4PIN_DPDM:
		return 0x7;

	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DATSE0:
		return 0xA;

	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DPDM:
		return 0xB;
	default:
		pr_warning("Invalid port mode, using default\n");
		return 0x0;
	}
}

static void ohci_omap3_tll_config(struct ohci_hcd_omap3 *omap)
{
	u32 reg;
	int i;

	/* Program TLL SHARED CONF */
	reg = ohci_omap_readl(omap->tll_base, OMAP_TLL_SHARED_CONF);
	reg &= ~OMAP_TLL_SHARED_CONF_USB_90D_DDR_EN;
	reg &= ~OMAP_TLL_SHARED_CONF_USB_180D_SDR_EN;
	reg |= OMAP_TLL_SHARED_CONF_USB_DIVRATION;
	reg |= OMAP_TLL_SHARED_CONF_FCLK_IS_ON;
	ohci_omap_writel(omap->tll_base, OMAP_TLL_SHARED_CONF, reg);

	/* Program each TLL channel */
	/*
	 * REVISIT: Only the 3-pin and 4-pin PHY modes have
	 * actually been tested.
	 */
	for (i = 0; i < OMAP_TLL_CHANNEL_COUNT; i++) {

		/* Enable only those channels that are actually used */
		if (omap->port_mode[i] == OMAP_OHCI_PORT_MODE_UNUSED)
			continue;

		reg = ohci_omap_readl(omap->tll_base, OMAP_TLL_CHANNEL_CONF(i));
		reg |= ohci_omap3_fslsmode(omap->port_mode[i])
				<< OMAP_TLL_CHANNEL_CONF_FSLSMODE_SHIFT;
		reg |= OMAP_TLL_CHANNEL_CONF_CHANMODE_FSLS;
		reg |= OMAP_TLL_CHANNEL_CONF_CHANEN;
		ohci_omap_writel(omap->tll_base, OMAP_TLL_CHANNEL_CONF(i), reg);
	}
}

/* omap3_start_ohci
 *	- Start the TI USBHOST controller
 */
static int omap3_start_ohci(struct ohci_hcd_omap3 *omap, struct usb_hcd *hcd)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	u32 reg = 0;
	int ret = 0;

	dev_dbg(omap->dev, "starting TI OHCI USB Controller\n");

	/* Get all the clock handles we need */
	omap->usbhost_ick = clk_get(omap->dev, "usbhost_ick");
	if (IS_ERR(omap->usbhost_ick)) {
		dev_err(omap->dev, "could not get usbhost_ick\n");
		ret =  PTR_ERR(omap->usbhost_ick);
		goto err_host_ick;
	}

	omap->usbhost2_120m_fck = clk_get(omap->dev, "usbhost_120m_fck");
	if (IS_ERR(omap->usbhost2_120m_fck)) {
		dev_err(omap->dev, "could not get usbhost_120m_fck\n");
		ret = PTR_ERR(omap->usbhost2_120m_fck);
		goto err_host_120m_fck;
	}

	omap->usbhost1_48m_fck = clk_get(omap->dev, "usbhost_48m_fck");
	if (IS_ERR(omap->usbhost1_48m_fck)) {
		dev_err(omap->dev, "could not get usbhost_48m_fck\n");
		ret = PTR_ERR(omap->usbhost1_48m_fck);
		goto err_host_48m_fck;
	}

	omap->usbtll_fck = clk_get(omap->dev, "usbtll_fck");
	if (IS_ERR(omap->usbtll_fck)) {
		dev_err(omap->dev, "could not get usbtll_fck\n");
		ret = PTR_ERR(omap->usbtll_fck);
		goto err_tll_fck;
	}

	omap->usbtll_ick = clk_get(omap->dev, "usbtll_ick");
	if (IS_ERR(omap->usbtll_ick)) {
		dev_err(omap->dev, "could not get usbtll_ick\n");
		ret = PTR_ERR(omap->usbtll_ick);
		goto err_tll_ick;
	}

	/* Now enable all the clocks in the correct order */
	ohci_omap3_clock_power(omap, 1);

	/* perform TLL soft reset, and wait until reset is complete */
	ohci_omap_writel(omap->tll_base, OMAP_USBTLL_SYSCONFIG,
			OMAP_USBTLL_SYSCONFIG_SOFTRESET);

	/* Wait for TLL reset to complete */
	while (!(ohci_omap_readl(omap->tll_base, OMAP_USBTLL_SYSSTATUS)
			& OMAP_USBTLL_SYSSTATUS_RESETDONE)) {
		cpu_relax();

		if (time_after(jiffies, timeout)) {
			dev_dbg(omap->dev, "operation timed out\n");
			ret = -EINVAL;
			goto err_sys_status;
		}
	}

	dev_dbg(omap->dev, "TLL reset done\n");

	/* (1<<3) = no idle mode only for initial debugging */
	ohci_omap_writel(omap->tll_base, OMAP_USBTLL_SYSCONFIG,
			OMAP_USBTLL_SYSCONFIG_ENAWAKEUP |
			OMAP_USBTLL_SYSCONFIG_SIDLEMODE |
			OMAP_USBTLL_SYSCONFIG_CACTIVITY);


	/* Put UHH in NoIdle/NoStandby mode */
	reg = ohci_omap_readl(omap->uhh_base, OMAP_UHH_SYSCONFIG);
	reg |= (OMAP_UHH_SYSCONFIG_ENAWAKEUP
			| OMAP_UHH_SYSCONFIG_SIDLEMODE
			| OMAP_UHH_SYSCONFIG_CACTIVITY
			| OMAP_UHH_SYSCONFIG_MIDLEMODE);
	reg &= ~OMAP_UHH_SYSCONFIG_AUTOIDLE;
	reg &= ~OMAP_UHH_SYSCONFIG_SOFTRESET;

	ohci_omap_writel(omap->uhh_base, OMAP_UHH_SYSCONFIG, reg);

	reg = ohci_omap_readl(omap->uhh_base, OMAP_UHH_HOSTCONFIG);

	/* setup ULPI bypass and burst configurations */
	reg |= (OMAP_UHH_HOSTCONFIG_INCR4_BURST_EN
			| OMAP_UHH_HOSTCONFIG_INCR8_BURST_EN
			| OMAP_UHH_HOSTCONFIG_INCR16_BURST_EN);
	reg &= ~OMAP_UHH_HOSTCONFIG_INCRX_ALIGN_EN;

	/*
	 * REVISIT: Pi_CONNECT_STATUS controls MStandby
	 * assertion and Swakeup generation - let us not
	 * worry about this for now. OMAP HWMOD framework
	 * might take care of this later. If not, we can
	 * update these registers when adding aggressive
	 * clock management code.
	 *
	 * For now, turn off all the Pi_CONNECT_STATUS bits
	 *
	if (omap->port_mode[0] == OMAP_OHCI_PORT_MODE_UNUSED)
		reg &= ~OMAP_UHH_HOSTCONFIG_P1_CONNECT_STATUS;
	if (omap->port_mode[1] == OMAP_OHCI_PORT_MODE_UNUSED)
		reg &= ~OMAP_UHH_HOSTCONFIG_P2_CONNECT_STATUS;
	if (omap->port_mode[2] == OMAP_OHCI_PORT_MODE_UNUSED)
		reg &= ~OMAP_UHH_HOSTCONFIG_P3_CONNECT_STATUS;
	 */
	reg &= ~OMAP_UHH_HOSTCONFIG_P1_CONNECT_STATUS;
	reg &= ~OMAP_UHH_HOSTCONFIG_P2_CONNECT_STATUS;
	reg &= ~OMAP_UHH_HOSTCONFIG_P3_CONNECT_STATUS;

	 if (omap->es2_compatibility) {
		/*
		 * All OHCI modes need to go through the TLL,
		 * unlike in the EHCI case. So use UTMI mode
		 * for all ports for OHCI, on ES2.x silicon
		 */
		dev_dbg(omap->dev, "OMAP3 ES version <= ES2.1\n");
		reg |= OMAP_UHH_HOSTCONFIG_ULPI_BYPASS;
	} else {
		dev_dbg(omap->dev, "OMAP3 ES version > ES2.1\n");
		if (omap->port_mode[0] == OMAP_OHCI_PORT_MODE_UNUSED)
			reg &= ~OMAP_UHH_HOSTCONFIG_ULPI_P1_BYPASS;
		else
			reg |= OMAP_UHH_HOSTCONFIG_ULPI_P1_BYPASS;

		if (omap->port_mode[1] == OMAP_OHCI_PORT_MODE_UNUSED)
			reg &= ~OMAP_UHH_HOSTCONFIG_ULPI_P2_BYPASS;
		else
			reg |= OMAP_UHH_HOSTCONFIG_ULPI_P2_BYPASS;

		if (omap->port_mode[2] == OMAP_OHCI_PORT_MODE_UNUSED)
			reg &= ~OMAP_UHH_HOSTCONFIG_ULPI_P3_BYPASS;
		else
			reg |= OMAP_UHH_HOSTCONFIG_ULPI_P3_BYPASS;

	}
	ohci_omap_writel(omap->uhh_base, OMAP_UHH_HOSTCONFIG, reg);
	dev_dbg(omap->dev, "UHH setup done, uhh_hostconfig=%x\n", reg);

	ohci_omap3_tll_config(omap);

	return 0;

err_sys_status:
	ohci_omap3_clock_power(omap, 0);
	clk_put(omap->usbtll_ick);

err_tll_ick:
	clk_put(omap->usbtll_fck);

err_tll_fck:
	clk_put(omap->usbhost1_48m_fck);

err_host_48m_fck:
	clk_put(omap->usbhost2_120m_fck);

err_host_120m_fck:
	clk_put(omap->usbhost_ick);

err_host_ick:
	return ret;
}

static void omap3_stop_ohci(struct ohci_hcd_omap3 *omap, struct usb_hcd *hcd)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(100);

	dev_dbg(omap->dev, "stopping TI EHCI USB Controller\n");

	/* Reset USBHOST for insmod/rmmod to work */
	ohci_omap_writel(omap->uhh_base, OMAP_UHH_SYSCONFIG,
			OMAP_UHH_SYSCONFIG_SOFTRESET);
	while (!(ohci_omap_readl(omap->uhh_base, OMAP_UHH_SYSSTATUS)
				& OMAP_UHH_SYSSTATUS_UHHRESETDONE)) {
		cpu_relax();

		if (time_after(jiffies, timeout))
			dev_dbg(omap->dev, "operation timed out\n");
	}

	while (!(ohci_omap_readl(omap->uhh_base, OMAP_UHH_SYSSTATUS)
				& OMAP_UHH_SYSSTATUS_OHCIRESETDONE)) {
		cpu_relax();

		if (time_after(jiffies, timeout))
			dev_dbg(omap->dev, "operation timed out\n");
	}

	while (!(ohci_omap_readl(omap->uhh_base, OMAP_UHH_SYSSTATUS)
				& OMAP_UHH_SYSSTATUS_EHCIRESETDONE)) {
		cpu_relax();

		if (time_after(jiffies, timeout))
			dev_dbg(omap->dev, "operation timed out\n");
	}

	ohci_omap_writel(omap->tll_base, OMAP_USBTLL_SYSCONFIG, (1 << 1));

	while (!(ohci_omap_readl(omap->tll_base, OMAP_USBTLL_SYSSTATUS)
				& (1 << 0))) {
		cpu_relax();

		if (time_after(jiffies, timeout))
			dev_dbg(omap->dev, "operation timed out\n");
	}

	ohci_omap3_clock_power(omap, 0);

	if (omap->usbtll_fck != NULL) {
		clk_put(omap->usbtll_fck);
		omap->usbtll_fck = NULL;
	}

	if (omap->usbhost_ick != NULL) {
		clk_put(omap->usbhost_ick);
		omap->usbhost_ick = NULL;
	}

	if (omap->usbhost1_48m_fck != NULL) {
		clk_put(omap->usbhost1_48m_fck);
		omap->usbhost1_48m_fck = NULL;
	}

	if (omap->usbhost2_120m_fck != NULL) {
		clk_put(omap->usbhost2_120m_fck);
		omap->usbhost2_120m_fck = NULL;
	}

	if (omap->usbtll_ick != NULL) {
		clk_put(omap->usbtll_ick);
		omap->usbtll_ick = NULL;
	}

	dev_dbg(omap->dev, "Clock to USB host has been disabled\n");
}

/*-------------------------------------------------------------------------*/

static const struct hc_driver ohci_omap3_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"OMAP3 OHCI Host Controller",
	.hcd_priv_size =	sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.reset =		ohci_omap3_init,
	.start =		ohci_omap3_start,
	.stop =			ohci_stop,
	.shutdown =		ohci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ohci_urb_enqueue,
	.urb_dequeue =		ohci_urb_dequeue,
	.endpoint_disable =	ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ohci_hub_status_data,
	.hub_control =		ohci_hub_control,
#ifdef	CONFIG_PM
	.bus_suspend =		ohci_bus_suspend,
	.bus_resume =		ohci_bus_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

/*-------------------------------------------------------------------------*/

/*
 * configure so an HC device and id are always provided
 * always called with process context; sleeping is OK
 */

/**
 * ohci_hcd_omap3_probe - initialize OMAP-based HCDs
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 */
static int __devinit ohci_hcd_omap3_probe(struct platform_device *pdev)
{
	struct ohci_hcd_omap_platform_data *pdata = pdev->dev.platform_data;
	struct ohci_hcd_omap3 *omap;
	struct resource *res;
	struct usb_hcd *hcd;
	int ret = -ENODEV;
	int irq;

	if (usb_disabled())
		goto err_disabled;

	if (!pdata) {
		dev_dbg(&pdev->dev, "missing platform_data\n");
		goto err_pdata;
	}

	irq = platform_get_irq(pdev, 0);

	omap = kzalloc(sizeof(*omap), GFP_KERNEL);
	if (!omap) {
		ret = -ENOMEM;
		goto err_disabled;
	}

	hcd = usb_create_hcd(&ohci_omap3_hc_driver, &pdev->dev,
			dev_name(&pdev->dev));
	if (!hcd) {
		ret = -ENOMEM;
		goto err_create_hcd;
	}

	platform_set_drvdata(pdev, omap);
	omap->dev		= &pdev->dev;
	omap->port_mode[0]	= pdata->port_mode[0];
	omap->port_mode[1]	= pdata->port_mode[1];
	omap->port_mode[2]	= pdata->port_mode[2];
	omap->es2_compatibility	= pdata->es2_compatibility;
	omap->ohci		= hcd_to_ohci(hcd);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "OHCI ioremap failed\n");
		ret = -ENOMEM;
		goto err_ioremap;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	omap->uhh_base = ioremap(res->start, resource_size(res));
	if (!omap->uhh_base) {
		dev_err(&pdev->dev, "UHH ioremap failed\n");
		ret = -ENOMEM;
		goto err_uhh_ioremap;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	omap->tll_base = ioremap(res->start, resource_size(res));
	if (!omap->tll_base) {
		dev_err(&pdev->dev, "TLL ioremap failed\n");
		ret = -ENOMEM;
		goto err_tll_ioremap;
	}

	ret = omap3_start_ohci(omap, hcd);
	if (ret) {
		dev_dbg(&pdev->dev, "failed to start ehci\n");
		goto err_start;
	}

	ohci_hcd_init(omap->ohci);

	ret = usb_add_hcd(hcd, irq, IRQF_DISABLED);
	if (ret) {
		dev_dbg(&pdev->dev, "failed to add hcd with err %d\n", ret);
		goto err_add_hcd;
	}

	return 0;

err_add_hcd:
	omap3_stop_ohci(omap, hcd);

err_start:
	iounmap(omap->tll_base);

err_tll_ioremap:
	iounmap(omap->uhh_base);

err_uhh_ioremap:
	iounmap(hcd->regs);

err_ioremap:
	usb_put_hcd(hcd);

err_create_hcd:
	kfree(omap);
err_pdata:
err_disabled:
	return ret;
}

/*
 * may be called without controller electrically present
 * may be called with controller, bus, and devices active
 */

/**
 * ohci_hcd_omap3_remove - shutdown processing for OHCI HCDs
 * @pdev: USB Host Controller being removed
 *
 * Reverses the effect of ohci_hcd_omap3_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 */
static int __devexit ohci_hcd_omap3_remove(struct platform_device *pdev)
{
	struct ohci_hcd_omap3 *omap = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ohci_to_hcd(omap->ohci);

	usb_remove_hcd(hcd);
	omap3_stop_ohci(omap, hcd);
	iounmap(hcd->regs);
	iounmap(omap->tll_base);
	iounmap(omap->uhh_base);
	usb_put_hcd(hcd);
	kfree(omap);

	return 0;
}

static void ohci_hcd_omap3_shutdown(struct platform_device *pdev)
{
	struct ohci_hcd_omap3 *omap = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ohci_to_hcd(omap->ohci);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static struct platform_driver ohci_hcd_omap3_driver = {
	.probe		= ohci_hcd_omap3_probe,
	.remove		= __devexit_p(ohci_hcd_omap3_remove),
	.shutdown	= ohci_hcd_omap3_shutdown,
	.driver		= {
		.name	= "ohci-omap3",
	},
};

MODULE_ALIAS("platform:ohci-omap3");
MODULE_AUTHOR("Anand Gadiyar <gadiyar@ti.com>");
