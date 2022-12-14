// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Platform level USB initialization for FS USB OTG controller on omap1
 *
 * Copyright (C) 2004 Texas Instruments, Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-map-ops.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/soc/ti/omap1-io.h>

#include <asm/irq.h>

#include "hardware.h"
#include "mux.h"
#include "usb.h"
#include "common.h"

/* These routines should handle the standard chip-specific modes
 * for usb0/1/2 ports, covering basic mux and transceiver setup.
 *
 * Some board-*.c files will need to set up additional mux options,
 * like for suspend handling, vbus sensing, GPIOs, and the D+ pullup.
 */

/* TESTED ON:
 *  - 1611B H2 (with usb1 mini-AB) using standard Mini-B or OTG cables
 *  - 5912 OSK OHCI (with usb0 standard-A), standard A-to-B cables
 *  - 5912 OSK UDC, with *nonstandard* A-to-A cable
 *  - 1510 Innovator UDC with bundled usb0 cable
 *  - 1510 Innovator OHCI with bundled usb1/usb2 cable
 *  - 1510 Innovator OHCI with custom usb0 cable, feeding 5V VBUS
 *  - 1710 custom development board using alternate pin group
 *  - 1710 H3 (with usb1 mini-AB) using standard Mini-B or OTG cables
 */

#define INT_USB_IRQ_GEN		IH2_BASE + 20
#define INT_USB_IRQ_NISO	IH2_BASE + 30
#define INT_USB_IRQ_ISO		IH2_BASE + 29
#define INT_USB_IRQ_HGEN	INT_USB_HHC_1
#define INT_USB_IRQ_OTG		IH2_BASE + 8

#ifdef	CONFIG_ARCH_OMAP_OTG

static void __init
omap_otg_init(struct omap_usb_config *config)
{
	u32		syscon;
	int		alt_pingroup = 0;
	u16		w;

	/* NOTE:  no bus or clock setup (yet?) */

	syscon = omap_readl(OTG_SYSCON_1) & 0xffff;
	if (!(syscon & OTG_RESET_DONE))
		pr_debug("USB resets not complete?\n");

	//omap_writew(0, OTG_IRQ_EN);

	/* pin muxing and transceiver pinouts */
	if (config->pins[0] > 2)	/* alt pingroup 2 */
		alt_pingroup = 1;
	syscon |= config->usb0_init(config->pins[0], is_usb0_device(config));
	syscon |= config->usb1_init(config->pins[1]);
	syscon |= config->usb2_init(config->pins[2], alt_pingroup);
	pr_debug("OTG_SYSCON_1 = %08x\n", omap_readl(OTG_SYSCON_1));
	omap_writel(syscon, OTG_SYSCON_1);

	syscon = config->hmc_mode;
	syscon |= USBX_SYNCHRO | (4 << 16) /* B_ASE0_BRST */;
#ifdef	CONFIG_USB_OTG
	if (config->otg)
		syscon |= OTG_EN;
#endif
	pr_debug("USB_TRANSCEIVER_CTRL = %03x\n",
		 omap_readl(USB_TRANSCEIVER_CTRL));
	pr_debug("OTG_SYSCON_2 = %08x\n", omap_readl(OTG_SYSCON_2));
	omap_writel(syscon, OTG_SYSCON_2);

	printk("USB: hmc %d", config->hmc_mode);
	if (!alt_pingroup)
		pr_cont(", usb2 alt %d wires", config->pins[2]);
	else if (config->pins[0])
		pr_cont(", usb0 %d wires%s", config->pins[0],
			is_usb0_device(config) ? " (dev)" : "");
	if (config->pins[1])
		pr_cont(", usb1 %d wires", config->pins[1]);
	if (!alt_pingroup && config->pins[2])
		pr_cont(", usb2 %d wires", config->pins[2]);
	if (config->otg)
		pr_cont(", Mini-AB on usb%d", config->otg - 1);
	pr_cont("\n");

	/* leave USB clocks/controllers off until needed */
	w = omap_readw(ULPD_SOFT_REQ);
	w &= ~SOFT_USB_CLK_REQ;
	omap_writew(w, ULPD_SOFT_REQ);

	w = omap_readw(ULPD_CLOCK_CTRL);
	w &= ~USB_MCLK_EN;
	w |= DIS_USB_PVCI_CLK;
	omap_writew(w, ULPD_CLOCK_CTRL);

	syscon = omap_readl(OTG_SYSCON_1);
	syscon |= HST_IDLE_EN|DEV_IDLE_EN|OTG_IDLE_EN;

#if IS_ENABLED(CONFIG_USB_OMAP)
	if (config->otg || config->register_dev) {
		struct platform_device *udc_device = config->udc_device;
		int status;

		syscon &= ~DEV_IDLE_EN;
		udc_device->dev.platform_data = config;
		status = platform_device_register(udc_device);
		if (status)
			pr_debug("can't register UDC device, %d\n", status);
	}
#endif

#if	IS_ENABLED(CONFIG_USB_OHCI_HCD)
	if (config->otg || config->register_host) {
		struct platform_device *ohci_device = config->ohci_device;
		int status;

		syscon &= ~HST_IDLE_EN;
		ohci_device->dev.platform_data = config;
		status = platform_device_register(ohci_device);
		if (status)
			pr_debug("can't register OHCI device, %d\n", status);
	}
#endif

#ifdef	CONFIG_USB_OTG
	if (config->otg) {
		struct platform_device *otg_device = config->otg_device;
		int status;

		syscon &= ~OTG_IDLE_EN;
		otg_device->dev.platform_data = config;
		status = platform_device_register(otg_device);
		if (status)
			pr_debug("can't register OTG device, %d\n", status);
	}
#endif
	pr_debug("OTG_SYSCON_1 = %08x\n", omap_readl(OTG_SYSCON_1));
	omap_writel(syscon, OTG_SYSCON_1);
}

#else
static void omap_otg_init(struct omap_usb_config *config) {}
#endif

#if IS_ENABLED(CONFIG_USB_OMAP)

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
	/* IRQ numbers for omap7xx */
	if(cpu_is_omap7xx()) {
		udc_resources[1].start = INT_7XX_USB_GENI;
		udc_resources[2].start = INT_7XX_USB_NON_ISO;
		udc_resources[3].start = INT_7XX_USB_ISO;
	}
	pdata->udc_device = &udc_device;
}

#else

static inline void udc_device_init(struct omap_usb_config *pdata)
{
}

#endif

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
	if (!IS_ENABLED(CONFIG_USB_OHCI_HCD))
		return;

	if (cpu_is_omap7xx())
		ohci_resources[1].start = INT_7XX_USB_HHC_1;
	pdata->ohci_device = &ohci_device;
	pdata->ocpi_enable = &ocpi_enable;
}

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
	if (cpu_is_omap7xx())
		otg_resources[1].start = INT_7XX_USB_OTG;
	pdata->otg_device = &otg_device;
}

#else

static inline void otg_device_init(struct omap_usb_config *pdata)
{
}

#endif

static u32 __init omap1_usb0_init(unsigned nwires, unsigned is_device)
{
	u32	syscon1 = 0;

	if (nwires == 0) {
		if (!cpu_is_omap15xx()) {
			u32 l;

			/* pulldown D+/D- */
			l = omap_readl(USB_TRANSCEIVER_CTRL);
			l &= ~(3 << 1);
			omap_writel(l, USB_TRANSCEIVER_CTRL);
		}
		return 0;
	}

	if (is_device) {
		if (cpu_is_omap7xx()) {
			omap_cfg_reg(AA17_7XX_USB_DM);
			omap_cfg_reg(W16_7XX_USB_PU_EN);
			omap_cfg_reg(W17_7XX_USB_VBUSI);
			omap_cfg_reg(W18_7XX_USB_DMCK_OUT);
			omap_cfg_reg(W19_7XX_USB_DCRST);
		} else
			omap_cfg_reg(W4_USB_PUEN);
	}

	if (nwires == 2) {
		u32 l;

		// omap_cfg_reg(P9_USB_DP);
		// omap_cfg_reg(R8_USB_DM);

		if (cpu_is_omap15xx()) {
			/* This works on 1510-Innovator */
			return 0;
		}

		/* NOTES:
		 *  - peripheral should configure VBUS detection!
		 *  - only peripherals may use the internal D+/D- pulldowns
		 *  - OTG support on this port not yet written
		 */

		/* Don't do this for omap7xx -- it causes USB to not work correctly */
		if (!cpu_is_omap7xx()) {
			l = omap_readl(USB_TRANSCEIVER_CTRL);
			l &= ~(7 << 4);
			if (!is_device)
				l |= (3 << 1);
			omap_writel(l, USB_TRANSCEIVER_CTRL);
		}

		return 3 << 16;
	}

	/* alternate pin config, external transceiver */
	if (cpu_is_omap15xx()) {
		printk(KERN_ERR "no usb0 alt pin config on 15xx\n");
		return 0;
	}

	omap_cfg_reg(V6_USB0_TXD);
	omap_cfg_reg(W9_USB0_TXEN);
	omap_cfg_reg(W5_USB0_SE0);
	if (nwires != 3)
		omap_cfg_reg(Y5_USB0_RCV);

	/* NOTE:  SPEED and SUSP aren't configured here.  OTG hosts
	 * may be able to use I2C requests to set those bits along
	 * with VBUS switching and overcurrent detection.
	 */

	if (nwires != 6) {
		u32 l;

		l = omap_readl(USB_TRANSCEIVER_CTRL);
		l &= ~CONF_USB2_UNI_R;
		omap_writel(l, USB_TRANSCEIVER_CTRL);
	}

	switch (nwires) {
	case 3:
		syscon1 = 2;
		break;
	case 4:
		syscon1 = 1;
		break;
	case 6:
		syscon1 = 3;
		{
			u32 l;

			omap_cfg_reg(AA9_USB0_VP);
			omap_cfg_reg(R9_USB0_VM);
			l = omap_readl(USB_TRANSCEIVER_CTRL);
			l |= CONF_USB2_UNI_R;
			omap_writel(l, USB_TRANSCEIVER_CTRL);
		}
		break;
	default:
		printk(KERN_ERR "illegal usb%d %d-wire transceiver\n",
			0, nwires);
	}

	return syscon1 << 16;
}

static u32 __init omap1_usb1_init(unsigned nwires)
{
	u32	syscon1 = 0;

	if (!cpu_is_omap15xx() && nwires != 6) {
		u32 l;

		l = omap_readl(USB_TRANSCEIVER_CTRL);
		l &= ~CONF_USB1_UNI_R;
		omap_writel(l, USB_TRANSCEIVER_CTRL);
	}
	if (nwires == 0)
		return 0;

	/* external transceiver */
	omap_cfg_reg(USB1_TXD);
	omap_cfg_reg(USB1_TXEN);
	if (nwires != 3)
		omap_cfg_reg(USB1_RCV);

	if (cpu_is_omap15xx()) {
		omap_cfg_reg(USB1_SEO);
		omap_cfg_reg(USB1_SPEED);
		// SUSP
	} else if (cpu_is_omap1610() || cpu_is_omap5912()) {
		omap_cfg_reg(W13_1610_USB1_SE0);
		omap_cfg_reg(R13_1610_USB1_SPEED);
		// SUSP
	} else if (cpu_is_omap1710()) {
		omap_cfg_reg(R13_1710_USB1_SE0);
		// SUSP
	} else {
		pr_debug("usb%d cpu unrecognized\n", 1);
		return 0;
	}

	switch (nwires) {
	case 2:
		goto bad;
	case 3:
		syscon1 = 2;
		break;
	case 4:
		syscon1 = 1;
		break;
	case 6:
		syscon1 = 3;
		omap_cfg_reg(USB1_VP);
		omap_cfg_reg(USB1_VM);
		if (!cpu_is_omap15xx()) {
			u32 l;

			l = omap_readl(USB_TRANSCEIVER_CTRL);
			l |= CONF_USB1_UNI_R;
			omap_writel(l, USB_TRANSCEIVER_CTRL);
		}
		break;
	default:
bad:
		printk(KERN_ERR "illegal usb%d %d-wire transceiver\n",
			1, nwires);
	}

	return syscon1 << 20;
}

static u32 __init omap1_usb2_init(unsigned nwires, unsigned alt_pingroup)
{
	u32	syscon1 = 0;

	/* NOTE omap1 erratum: must leave USB2_UNI_R set if usb0 in use */
	if (alt_pingroup || nwires == 0)
		return 0;

	if (!cpu_is_omap15xx() && nwires != 6) {
		u32 l;

		l = omap_readl(USB_TRANSCEIVER_CTRL);
		l &= ~CONF_USB2_UNI_R;
		omap_writel(l, USB_TRANSCEIVER_CTRL);
	}

	/* external transceiver */
	if (cpu_is_omap15xx()) {
		omap_cfg_reg(USB2_TXD);
		omap_cfg_reg(USB2_TXEN);
		omap_cfg_reg(USB2_SEO);
		if (nwires != 3)
			omap_cfg_reg(USB2_RCV);
		/* there is no USB2_SPEED */
	} else if (cpu_is_omap16xx()) {
		omap_cfg_reg(V6_USB2_TXD);
		omap_cfg_reg(W9_USB2_TXEN);
		omap_cfg_reg(W5_USB2_SE0);
		if (nwires != 3)
			omap_cfg_reg(Y5_USB2_RCV);
		// FIXME omap_cfg_reg(USB2_SPEED);
	} else {
		pr_debug("usb%d cpu unrecognized\n", 1);
		return 0;
	}

	// omap_cfg_reg(USB2_SUSP);

	switch (nwires) {
	case 2:
		goto bad;
	case 3:
		syscon1 = 2;
		break;
	case 4:
		syscon1 = 1;
		break;
	case 5:
		goto bad;
	case 6:
		syscon1 = 3;
		if (cpu_is_omap15xx()) {
			omap_cfg_reg(USB2_VP);
			omap_cfg_reg(USB2_VM);
		} else {
			u32 l;

			omap_cfg_reg(AA9_USB2_VP);
			omap_cfg_reg(R9_USB2_VM);
			l = omap_readl(USB_TRANSCEIVER_CTRL);
			l |= CONF_USB2_UNI_R;
			omap_writel(l, USB_TRANSCEIVER_CTRL);
		}
		break;
	default:
bad:
		printk(KERN_ERR "illegal usb%d %d-wire transceiver\n",
			2, nwires);
	}

	return syscon1 << 24;
}

#ifdef	CONFIG_ARCH_OMAP15XX
/* OMAP-1510 OHCI has its own MMU for DMA */
#define OMAP1510_LB_MEMSIZE	32	/* Should be same as SDRAM size */
#define OMAP1510_LB_CLOCK_DIV	0xfffec10c
#define OMAP1510_LB_MMU_CTL	0xfffec208
#define OMAP1510_LB_MMU_LCK	0xfffec224
#define OMAP1510_LB_MMU_LD_TLB	0xfffec228
#define OMAP1510_LB_MMU_CAM_H	0xfffec22c
#define OMAP1510_LB_MMU_CAM_L	0xfffec230
#define OMAP1510_LB_MMU_RAM_H	0xfffec234
#define OMAP1510_LB_MMU_RAM_L	0xfffec238

/*
 * Bus address is physical address, except for OMAP-1510 Local Bus.
 * OMAP-1510 bus address is translated into a Local Bus address if the
 * OMAP bus type is lbus.
 */
#define OMAP1510_LB_OFFSET	   UL(0x30000000)

/*
 * OMAP-1510 specific Local Bus clock on/off
 */
static int omap_1510_local_bus_power(int on)
{
	if (on) {
		omap_writel((1 << 1) | (1 << 0), OMAP1510_LB_MMU_CTL);
		udelay(200);
	} else {
		omap_writel(0, OMAP1510_LB_MMU_CTL);
	}

	return 0;
}

/*
 * OMAP-1510 specific Local Bus initialization
 * NOTE: This assumes 32MB memory size in OMAP1510LB_MEMSIZE.
 *       See also arch/mach-omap/memory.h for __virt_to_dma() and
 *       __dma_to_virt() which need to match with the physical
 *       Local Bus address below.
 */
static int omap_1510_local_bus_init(void)
{
	unsigned int tlb;
	unsigned long lbaddr, physaddr;

	omap_writel((omap_readl(OMAP1510_LB_CLOCK_DIV) & 0xfffffff8) | 0x4,
	       OMAP1510_LB_CLOCK_DIV);

	/* Configure the Local Bus MMU table */
	for (tlb = 0; tlb < OMAP1510_LB_MEMSIZE; tlb++) {
		lbaddr = tlb * 0x00100000 + OMAP1510_LB_OFFSET;
		physaddr = tlb * 0x00100000 + PHYS_OFFSET;
		omap_writel((lbaddr & 0x0fffffff) >> 22, OMAP1510_LB_MMU_CAM_H);
		omap_writel(((lbaddr & 0x003ffc00) >> 6) | 0xc,
		       OMAP1510_LB_MMU_CAM_L);
		omap_writel(physaddr >> 16, OMAP1510_LB_MMU_RAM_H);
		omap_writel((physaddr & 0x0000fc00) | 0x300, OMAP1510_LB_MMU_RAM_L);
		omap_writel(tlb << 4, OMAP1510_LB_MMU_LCK);
		omap_writel(0x1, OMAP1510_LB_MMU_LD_TLB);
	}

	/* Enable the walking table */
	omap_writel(omap_readl(OMAP1510_LB_MMU_CTL) | (1 << 3), OMAP1510_LB_MMU_CTL);
	udelay(200);

	return 0;
}

static void omap_1510_local_bus_reset(void)
{
	omap_1510_local_bus_power(1);
	omap_1510_local_bus_init();
}

/* ULPD_DPLL_CTRL */
#define DPLL_IOB		(1 << 13)
#define DPLL_PLL_ENABLE		(1 << 4)
#define DPLL_LOCK		(1 << 0)

/* ULPD_APLL_CTRL */
#define APLL_NDPLL_SWITCH	(1 << 0)

static void __init omap_1510_usb_init(struct omap_usb_config *config)
{
	unsigned int val;
	u16 w;

	config->usb0_init(config->pins[0], is_usb0_device(config));
	config->usb1_init(config->pins[1]);
	config->usb2_init(config->pins[2], 0);

	val = omap_readl(MOD_CONF_CTRL_0) & ~(0x3f << 1);
	val |= (config->hmc_mode << 1);
	omap_writel(val, MOD_CONF_CTRL_0);

	printk("USB: hmc %d", config->hmc_mode);
	if (config->pins[0])
		pr_cont(", usb0 %d wires%s", config->pins[0],
			is_usb0_device(config) ? " (dev)" : "");
	if (config->pins[1])
		pr_cont(", usb1 %d wires", config->pins[1]);
	if (config->pins[2])
		pr_cont(", usb2 %d wires", config->pins[2]);
	pr_cont("\n");

	/* use DPLL for 48 MHz function clock */
	pr_debug("APLL %04x DPLL %04x REQ %04x\n", omap_readw(ULPD_APLL_CTRL),
			omap_readw(ULPD_DPLL_CTRL), omap_readw(ULPD_SOFT_REQ));

	w = omap_readw(ULPD_APLL_CTRL);
	w &= ~APLL_NDPLL_SWITCH;
	omap_writew(w, ULPD_APLL_CTRL);

	w = omap_readw(ULPD_DPLL_CTRL);
	w |= DPLL_IOB | DPLL_PLL_ENABLE;
	omap_writew(w, ULPD_DPLL_CTRL);

	w = omap_readw(ULPD_SOFT_REQ);
	w |= SOFT_UDC_REQ | SOFT_DPLL_REQ;
	omap_writew(w, ULPD_SOFT_REQ);

	while (!(omap_readw(ULPD_DPLL_CTRL) & DPLL_LOCK))
		cpu_relax();

#if IS_ENABLED(CONFIG_USB_OMAP)
	if (config->register_dev) {
		int status;

		udc_device.dev.platform_data = config;
		status = platform_device_register(&udc_device);
		if (status)
			pr_debug("can't register UDC device, %d\n", status);
		/* udc driver gates 48MHz by D+ pullup */
	}
#endif

	if (IS_ENABLED(CONFIG_USB_OHCI_HCD) && config->register_host) {
		int status;

		ohci_device.dev.platform_data = config;
		dma_direct_set_offset(&ohci_device.dev, PHYS_OFFSET,
				      OMAP1510_LB_OFFSET, (u64)-1);
		status = platform_device_register(&ohci_device);
		if (status)
			pr_debug("can't register OHCI device, %d\n", status);
		/* hcd explicitly gates 48MHz */

		config->lb_reset = omap_1510_local_bus_reset;
	}
}

#else
static inline void omap_1510_usb_init(struct omap_usb_config *config) {}
#endif

void __init omap1_usb_init(struct omap_usb_config *_pdata)
{
	struct omap_usb_config *pdata;

	pdata = kmemdup(_pdata, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return;

	pdata->usb0_init = omap1_usb0_init;
	pdata->usb1_init = omap1_usb1_init;
	pdata->usb2_init = omap1_usb2_init;
	udc_device_init(pdata);
	ohci_device_init(pdata);
	otg_device_init(pdata);

	if (cpu_is_omap7xx() || cpu_is_omap16xx())
		omap_otg_init(pdata);
	else if (cpu_is_omap15xx())
		omap_1510_usb_init(pdata);
	else
		printk(KERN_ERR "USB: No init for your chip yet\n");
}
