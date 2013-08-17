/*
 * Platform level USB initialization for FS USB OTG controller on omap1 and 24xx
 *
 * Copyright (C) 2004 Texas Instruments, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <asm/irq.h>

#include <plat/usb.h>
#include <plat/board.h>

#include "control.h"
#include "mux.h"

#define INT_USB_IRQ_GEN		INT_24XX_USB_IRQ_GEN
#define INT_USB_IRQ_NISO	INT_24XX_USB_IRQ_NISO
#define INT_USB_IRQ_ISO		INT_24XX_USB_IRQ_ISO
#define INT_USB_IRQ_HGEN	INT_24XX_USB_IRQ_HGEN
#define INT_USB_IRQ_OTG		INT_24XX_USB_IRQ_OTG

#if defined(CONFIG_ARCH_OMAP2)

#ifdef	CONFIG_USB_GADGET_OMAP

static struct resource udc_resources[] = {
	/* order is significant! */
	{		/* registers */
		.start		= UDC_BASE,
		.end		= UDC_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {		/* general IRQ */
		.start		= INT_USB_IRQ_GEN,
		.flags		= IORESOURCE_IRQ,
	}, {		/* PIO IRQ */
		.start		= INT_USB_IRQ_NISO,
		.flags		= IORESOURCE_IRQ,
	}, {		/* SOF IRQ */
		.start		= INT_USB_IRQ_ISO,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 udc_dmamask = ~(u32)0;

static struct platform_device udc_device = {
	.name		= "omap_udc",
	.id		= -1,
	.dev = {
		.dma_mask		= &udc_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(udc_resources),
	.resource	= udc_resources,
};

static inline void udc_device_init(struct omap_usb_config *pdata)
{
	pdata->udc_device = &udc_device;
}

#else

static inline void udc_device_init(struct omap_usb_config *pdata)
{
}

#endif

#if	defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)

/* The dmamask must be set for OHCI to work */
static u64 ohci_dmamask = ~(u32)0;

static struct resource ohci_resources[] = {
	{
		.start	= OMAP_OHCI_BASE,
		.end	= OMAP_OHCI_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_USB_IRQ_HGEN,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ohci_device = {
	.name			= "ohci",
	.id			= -1,
	.dev = {
		.dma_mask		= &ohci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(ohci_resources),
	.resource		= ohci_resources,
};

static inline void ohci_device_init(struct omap_usb_config *pdata)
{
	pdata->ohci_device = &ohci_device;
}

#else

static inline void ohci_device_init(struct omap_usb_config *pdata)
{
}

#endif

#if	defined(CONFIG_USB_OTG) && defined(CONFIG_ARCH_OMAP_OTG)

static struct resource otg_resources[] = {
	/* order is significant! */
	{
		.start		= OTG_BASE,
		.end		= OTG_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= INT_USB_IRQ_OTG,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device otg_device = {
	.name		= "omap_otg",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(otg_resources),
	.resource	= otg_resources,
};

static inline void otg_device_init(struct omap_usb_config *pdata)
{
	pdata->otg_device = &otg_device;
}

#else

static inline void otg_device_init(struct omap_usb_config *pdata)
{
}

#endif

static void omap2_usb_devconf_clear(u8 port, u32 mask)
{
	u32 r;

	r = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
	r &= ~USBTXWRMODEI(port, mask);
	omap_ctrl_writel(r, OMAP2_CONTROL_DEVCONF0);
}

static void omap2_usb_devconf_set(u8 port, u32 mask)
{
	u32 r;

	r = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
	r |= USBTXWRMODEI(port, mask);
	omap_ctrl_writel(r, OMAP2_CONTROL_DEVCONF0);
}

static void omap2_usb2_disable_5pinbitll(void)
{
	u32 r;

	r = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
	r &= ~(USBTXWRMODEI(2, USB_BIDIR_TLL) | USBT2TLL5PI);
	omap_ctrl_writel(r, OMAP2_CONTROL_DEVCONF0);
}

static void omap2_usb2_enable_5pinunitll(void)
{
	u32 r;

	r = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
	r |= USBTXWRMODEI(2, USB_UNIDIR_TLL) | USBT2TLL5PI;
	omap_ctrl_writel(r, OMAP2_CONTROL_DEVCONF0);
}

static u32 __init omap2_usb0_init(unsigned nwires, unsigned is_device)
{
	u32	syscon1 = 0;

	omap2_usb_devconf_clear(0, USB_BIDIR_TLL);

	if (nwires == 0)
		return 0;

	if (is_device)
		omap_mux_init_signal("usb0_puen", 0);

	omap_mux_init_signal("usb0_dat", 0);
	omap_mux_init_signal("usb0_txen", 0);
	omap_mux_init_signal("usb0_se0", 0);
	if (nwires != 3)
		omap_mux_init_signal("usb0_rcv", 0);

	switch (nwires) {
	case 3:
		syscon1 = 2;
		omap2_usb_devconf_set(0, USB_BIDIR);
		break;
	case 4:
		syscon1 = 1;
		omap2_usb_devconf_set(0, USB_BIDIR);
		break;
	case 6:
		syscon1 = 3;
		omap_mux_init_signal("usb0_vp", 0);
		omap_mux_init_signal("usb0_vm", 0);
		omap2_usb_devconf_set(0, USB_UNIDIR);
		break;
	default:
		printk(KERN_ERR "illegal usb%d %d-wire transceiver\n",
			0, nwires);
	}

	return syscon1 << 16;
}

static u32 __init omap2_usb1_init(unsigned nwires)
{
	u32	syscon1 = 0;

	omap2_usb_devconf_clear(1, USB_BIDIR_TLL);

	if (nwires == 0)
		return 0;

	/* NOTE:  board-specific code must set up pin muxing for usb1,
	 * since each signal could come out on either of two balls.
	 */

	switch (nwires) {
	case 2:
		/* NOTE: board-specific code must override this setting if
		 * this TLL link is not using DP/DM
		 */
		syscon1 = 1;
		omap2_usb_devconf_set(1, USB_BIDIR_TLL);
		break;
	case 3:
		syscon1 = 2;
		omap2_usb_devconf_set(1, USB_BIDIR);
		break;
	case 4:
		syscon1 = 1;
		omap2_usb_devconf_set(1, USB_BIDIR);
		break;
	case 6:
	default:
		printk(KERN_ERR "illegal usb%d %d-wire transceiver\n",
			1, nwires);
	}

	return syscon1 << 20;
}

static u32 __init omap2_usb2_init(unsigned nwires, unsigned alt_pingroup)
{
	u32	syscon1 = 0;

	omap2_usb2_disable_5pinbitll();
	alt_pingroup = 0;

	/* NOTE omap1 erratum: must leave USB2_UNI_R set if usb0 in use */
	if (alt_pingroup || nwires == 0)
		return 0;

	omap_mux_init_signal("usb2_dat", 0);
	omap_mux_init_signal("usb2_se0", 0);
	if (nwires > 2)
		omap_mux_init_signal("usb2_txen", 0);
	if (nwires > 3)
		omap_mux_init_signal("usb2_rcv", 0);

	switch (nwires) {
	case 2:
		/* NOTE: board-specific code must override this setting if
		 * this TLL link is not using DP/DM
		 */
		syscon1 = 1;
		omap2_usb_devconf_set(2, USB_BIDIR_TLL);
		break;
	case 3:
		syscon1 = 2;
		omap2_usb_devconf_set(2, USB_BIDIR);
		break;
	case 4:
		syscon1 = 1;
		omap2_usb_devconf_set(2, USB_BIDIR);
		break;
	case 5:
		/* NOTE: board-specific code must mux this setting depending
		 * on TLL link using DP/DM.  Something must also
		 * set up OTG_SYSCON2.HMC_TLL{ATTACH,SPEED}
		 * 2420: hdq_sio.usb2_tllse0 or vlynq_rx0.usb2_tllse0
		 * 2430: hdq_sio.usb2_tllse0 or sdmmc2_dat0.usb2_tllse0
		 */

		syscon1 = 3;
		omap2_usb2_enable_5pinunitll();
		break;
	case 6:
	default:
		printk(KERN_ERR "illegal usb%d %d-wire transceiver\n",
			2, nwires);
	}

	return syscon1 << 24;
}

void __init omap2_usbfs_init(struct omap_usb_config *pdata)
{
	struct clk *ick;

	if (!cpu_is_omap24xx())
		return;

	ick = clk_get(NULL, "usb_l4_ick");
	if (IS_ERR(ick))
		return;

	clk_enable(ick);
	pdata->usb0_init = omap2_usb0_init;
	pdata->usb1_init = omap2_usb1_init;
	pdata->usb2_init = omap2_usb2_init;
	udc_device_init(pdata);
	ohci_device_init(pdata);
	otg_device_init(pdata);
	omap_otg_init(pdata);
	clk_disable(ick);
	clk_put(ick);
}

#endif
