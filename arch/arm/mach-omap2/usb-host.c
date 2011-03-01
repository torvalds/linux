/*
 * usb-host.c - OMAP USB Host
 *
 * This file will contain the board specific details for the
 * Synopsys EHCI/OHCI host controller on OMAP3430 and onwards
 *
 * Copyright (C) 2007-2011 Texas Instruments
 * Author: Vikram Pandita <vikram.pandita@ti.com>
 * Author: Keshava Munegowda <keshava_mgowda@ti.com>
 *
 * Generalization by:
 * Felipe Balbi <balbi@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <plat/usb.h>

#include "mux.h"

#ifdef CONFIG_MFD_OMAP_USB_HOST

#define OMAP_USBHS_DEVICE	"usbhs-omap"

static struct resource usbhs_resources[] = {
	{
		.name	= "uhh",
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "tll",
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "ehci",
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "ehci-irq",
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "ohci",
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "ohci-irq",
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device usbhs_device = {
	.name		= OMAP_USBHS_DEVICE,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(usbhs_resources),
	.resource	= usbhs_resources,
};

static struct usbhs_omap_platform_data		usbhs_data;
static struct ehci_hcd_omap_platform_data	ehci_data;
static struct ohci_hcd_omap_platform_data	ohci_data;

/* MUX settings for EHCI pins */
/*
 * setup_ehci_io_mux - initialize IO pad mux for USBHOST
 */
static void setup_ehci_io_mux(const enum usbhs_omap_port_mode *port_mode)
{
	switch (port_mode[0]) {
	case OMAP_EHCI_PORT_MODE_PHY:
		omap_mux_init_signal("hsusb1_stp", OMAP_PIN_OUTPUT);
		omap_mux_init_signal("hsusb1_clk", OMAP_PIN_OUTPUT);
		omap_mux_init_signal("hsusb1_dir", OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_nxt", OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_data0", OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_data1", OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_data2", OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_data3", OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_data4", OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_data5", OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_data6", OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_data7", OMAP_PIN_INPUT_PULLDOWN);
		break;
	case OMAP_EHCI_PORT_MODE_TLL:
		omap_mux_init_signal("hsusb1_tll_stp",
			OMAP_PIN_INPUT_PULLUP);
		omap_mux_init_signal("hsusb1_tll_clk",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_tll_dir",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_tll_nxt",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_tll_data0",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_tll_data1",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_tll_data2",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_tll_data3",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_tll_data4",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_tll_data5",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_tll_data6",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb1_tll_data7",
			OMAP_PIN_INPUT_PULLDOWN);
		break;
	case OMAP_USBHS_PORT_MODE_UNUSED:
		/* FALLTHROUGH */
	default:
		break;
	}

	switch (port_mode[1]) {
	case OMAP_EHCI_PORT_MODE_PHY:
		omap_mux_init_signal("hsusb2_stp", OMAP_PIN_OUTPUT);
		omap_mux_init_signal("hsusb2_clk", OMAP_PIN_OUTPUT);
		omap_mux_init_signal("hsusb2_dir", OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_nxt", OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_data0",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_data1",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_data2",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_data3",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_data4",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_data5",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_data6",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_data7",
			OMAP_PIN_INPUT_PULLDOWN);
		break;
	case OMAP_EHCI_PORT_MODE_TLL:
		omap_mux_init_signal("hsusb2_tll_stp",
			OMAP_PIN_INPUT_PULLUP);
		omap_mux_init_signal("hsusb2_tll_clk",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_tll_dir",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_tll_nxt",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_tll_data0",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_tll_data1",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_tll_data2",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_tll_data3",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_tll_data4",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_tll_data5",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_tll_data6",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb2_tll_data7",
			OMAP_PIN_INPUT_PULLDOWN);
		break;
	case OMAP_USBHS_PORT_MODE_UNUSED:
		/* FALLTHROUGH */
	default:
		break;
	}

	switch (port_mode[2]) {
	case OMAP_EHCI_PORT_MODE_PHY:
		printk(KERN_WARNING "Port3 can't be used in PHY mode\n");
		break;
	case OMAP_EHCI_PORT_MODE_TLL:
		omap_mux_init_signal("hsusb3_tll_stp",
			OMAP_PIN_INPUT_PULLUP);
		omap_mux_init_signal("hsusb3_tll_clk",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb3_tll_dir",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb3_tll_nxt",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb3_tll_data0",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb3_tll_data1",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb3_tll_data2",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb3_tll_data3",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb3_tll_data4",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb3_tll_data5",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb3_tll_data6",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("hsusb3_tll_data7",
			OMAP_PIN_INPUT_PULLDOWN);
		break;
	case OMAP_USBHS_PORT_MODE_UNUSED:
		/* FALLTHROUGH */
	default:
		break;
	}

	return;
}

static void setup_4430ehci_io_mux(const enum usbhs_omap_port_mode *port_mode)
{
	switch (port_mode[0]) {
	case OMAP_EHCI_PORT_MODE_PHY:
		omap_mux_init_signal("usbb1_ulpiphy_stp",
			OMAP_PIN_OUTPUT);
		omap_mux_init_signal("usbb1_ulpiphy_clk",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpiphy_dir",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpiphy_nxt",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpiphy_dat0",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpiphy_dat1",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpiphy_dat2",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpiphy_dat3",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpiphy_dat4",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpiphy_dat5",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpiphy_dat6",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpiphy_dat7",
			OMAP_PIN_INPUT_PULLDOWN);
			break;
	case OMAP_EHCI_PORT_MODE_TLL:
		omap_mux_init_signal("usbb1_ulpitll_stp",
			OMAP_PIN_INPUT_PULLUP);
		omap_mux_init_signal("usbb1_ulpitll_clk",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpitll_dir",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpitll_nxt",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpitll_dat0",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpitll_dat1",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpitll_dat2",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpitll_dat3",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpitll_dat4",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpitll_dat5",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpitll_dat6",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_ulpitll_dat7",
			OMAP_PIN_INPUT_PULLDOWN);
			break;
	case OMAP_USBHS_PORT_MODE_UNUSED:
	default:
			break;
	}
	switch (port_mode[1]) {
	case OMAP_EHCI_PORT_MODE_PHY:
		omap_mux_init_signal("usbb2_ulpiphy_stp",
			OMAP_PIN_OUTPUT);
		omap_mux_init_signal("usbb2_ulpiphy_clk",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpiphy_dir",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpiphy_nxt",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpiphy_dat0",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpiphy_dat1",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpiphy_dat2",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpiphy_dat3",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpiphy_dat4",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpiphy_dat5",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpiphy_dat6",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpiphy_dat7",
			OMAP_PIN_INPUT_PULLDOWN);
			break;
	case OMAP_EHCI_PORT_MODE_TLL:
		omap_mux_init_signal("usbb2_ulpitll_stp",
			OMAP_PIN_INPUT_PULLUP);
		omap_mux_init_signal("usbb2_ulpitll_clk",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpitll_dir",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpitll_nxt",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpitll_dat0",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpitll_dat1",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpitll_dat2",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpitll_dat3",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpitll_dat4",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpitll_dat5",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpitll_dat6",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_ulpitll_dat7",
			OMAP_PIN_INPUT_PULLDOWN);
			break;
	case OMAP_USBHS_PORT_MODE_UNUSED:
	default:
			break;
	}
}

static void setup_ohci_io_mux(const enum usbhs_omap_port_mode *port_mode)
{
	switch (port_mode[0]) {
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DPDM:
		omap_mux_init_signal("mm1_rxdp",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("mm1_rxdm",
			OMAP_PIN_INPUT_PULLDOWN);
		/* FALLTHROUGH */
	case OMAP_OHCI_PORT_MODE_PHY_4PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_4PIN_DPDM:
		omap_mux_init_signal("mm1_rxrcv",
			OMAP_PIN_INPUT_PULLDOWN);
		/* FALLTHROUGH */
	case OMAP_OHCI_PORT_MODE_PHY_3PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_3PIN_DATSE0:
		omap_mux_init_signal("mm1_txen_n", OMAP_PIN_OUTPUT);
		/* FALLTHROUGH */
	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DPDM:
		omap_mux_init_signal("mm1_txse0",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("mm1_txdat",
			OMAP_PIN_INPUT_PULLDOWN);
		break;
	case OMAP_USBHS_PORT_MODE_UNUSED:
		/* FALLTHROUGH */
	default:
		break;
	}
	switch (port_mode[1]) {
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DPDM:
		omap_mux_init_signal("mm2_rxdp",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("mm2_rxdm",
			OMAP_PIN_INPUT_PULLDOWN);
		/* FALLTHROUGH */
	case OMAP_OHCI_PORT_MODE_PHY_4PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_4PIN_DPDM:
		omap_mux_init_signal("mm2_rxrcv",
			OMAP_PIN_INPUT_PULLDOWN);
		/* FALLTHROUGH */
	case OMAP_OHCI_PORT_MODE_PHY_3PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_3PIN_DATSE0:
		omap_mux_init_signal("mm2_txen_n", OMAP_PIN_OUTPUT);
		/* FALLTHROUGH */
	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DPDM:
		omap_mux_init_signal("mm2_txse0",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("mm2_txdat",
			OMAP_PIN_INPUT_PULLDOWN);
		break;
	case OMAP_USBHS_PORT_MODE_UNUSED:
		/* FALLTHROUGH */
	default:
		break;
	}
	switch (port_mode[2]) {
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DPDM:
		omap_mux_init_signal("mm3_rxdp",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("mm3_rxdm",
			OMAP_PIN_INPUT_PULLDOWN);
		/* FALLTHROUGH */
	case OMAP_OHCI_PORT_MODE_PHY_4PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_4PIN_DPDM:
		omap_mux_init_signal("mm3_rxrcv",
			OMAP_PIN_INPUT_PULLDOWN);
		/* FALLTHROUGH */
	case OMAP_OHCI_PORT_MODE_PHY_3PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_3PIN_DATSE0:
		omap_mux_init_signal("mm3_txen_n", OMAP_PIN_OUTPUT);
		/* FALLTHROUGH */
	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DPDM:
		omap_mux_init_signal("mm3_txse0",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("mm3_txdat",
			OMAP_PIN_INPUT_PULLDOWN);
		break;
	case OMAP_USBHS_PORT_MODE_UNUSED:
		/* FALLTHROUGH */
	default:
		break;
	}
}

static void setup_4430ohci_io_mux(const enum usbhs_omap_port_mode *port_mode)
{
	switch (port_mode[0]) {
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DPDM:
		omap_mux_init_signal("usbb1_mm_rxdp",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_mm_rxdm",
			OMAP_PIN_INPUT_PULLDOWN);

	case OMAP_OHCI_PORT_MODE_PHY_4PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_4PIN_DPDM:
		omap_mux_init_signal("usbb1_mm_rxrcv",
			OMAP_PIN_INPUT_PULLDOWN);

	case OMAP_OHCI_PORT_MODE_PHY_3PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_3PIN_DATSE0:
		omap_mux_init_signal("usbb1_mm_txen",
			OMAP_PIN_INPUT_PULLDOWN);


	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DPDM:
		omap_mux_init_signal("usbb1_mm_txdat",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb1_mm_txse0",
			OMAP_PIN_INPUT_PULLDOWN);
		break;

	case OMAP_USBHS_PORT_MODE_UNUSED:
	default:
		break;
	}

	switch (port_mode[1]) {
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_PHY_6PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_6PIN_DPDM:
		omap_mux_init_signal("usbb2_mm_rxdp",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_mm_rxdm",
			OMAP_PIN_INPUT_PULLDOWN);

	case OMAP_OHCI_PORT_MODE_PHY_4PIN_DPDM:
	case OMAP_OHCI_PORT_MODE_TLL_4PIN_DPDM:
		omap_mux_init_signal("usbb2_mm_rxrcv",
			OMAP_PIN_INPUT_PULLDOWN);

	case OMAP_OHCI_PORT_MODE_PHY_3PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_3PIN_DATSE0:
		omap_mux_init_signal("usbb2_mm_txen",
			OMAP_PIN_INPUT_PULLDOWN);


	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DATSE0:
	case OMAP_OHCI_PORT_MODE_TLL_2PIN_DPDM:
		omap_mux_init_signal("usbb2_mm_txdat",
			OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usbb2_mm_txse0",
			OMAP_PIN_INPUT_PULLDOWN);
		break;

	case OMAP_USBHS_PORT_MODE_UNUSED:
	default:
		break;
	}
}

void __init usbhs_init(const struct usbhs_omap_board_data *pdata)
{
	int	i;

	for (i = 0; i < OMAP3_HS_USB_PORTS; i++) {
		usbhs_data.port_mode[i] = pdata->port_mode[i];
		ohci_data.port_mode[i] = pdata->port_mode[i];
		ehci_data.port_mode[i] = pdata->port_mode[i];
		ehci_data.reset_gpio_port[i] = pdata->reset_gpio_port[i];
		ehci_data.regulator[i] = pdata->regulator[i];
	}
	ehci_data.phy_reset = pdata->phy_reset;
	ohci_data.es2_compatibility = pdata->es2_compatibility;
	usbhs_data.ehci_data = &ehci_data;
	usbhs_data.ohci_data = &ohci_data;

	if (cpu_is_omap34xx()) {
		usbhs_resources[0].start = OMAP34XX_UHH_CONFIG_BASE;
		usbhs_resources[0].end = OMAP34XX_UHH_CONFIG_BASE + SZ_1K - 1;
		usbhs_resources[1].start = OMAP34XX_USBTLL_BASE;
		usbhs_resources[1].end = OMAP34XX_USBTLL_BASE + SZ_4K - 1;
		usbhs_resources[2].start	= OMAP34XX_EHCI_BASE;
		usbhs_resources[2].end	= OMAP34XX_EHCI_BASE + SZ_1K - 1;
		usbhs_resources[3].start = INT_34XX_EHCI_IRQ;
		usbhs_resources[4].start	= OMAP34XX_OHCI_BASE;
		usbhs_resources[4].end	= OMAP34XX_OHCI_BASE + SZ_1K - 1;
		usbhs_resources[5].start = INT_34XX_OHCI_IRQ;
		setup_ehci_io_mux(pdata->port_mode);
		setup_ohci_io_mux(pdata->port_mode);
	} else if (cpu_is_omap44xx()) {
		usbhs_resources[0].start = OMAP44XX_UHH_CONFIG_BASE;
		usbhs_resources[0].end = OMAP44XX_UHH_CONFIG_BASE + SZ_1K - 1;
		usbhs_resources[1].start = OMAP44XX_USBTLL_BASE;
		usbhs_resources[1].end = OMAP44XX_USBTLL_BASE + SZ_4K - 1;
		usbhs_resources[2].start = OMAP44XX_HSUSB_EHCI_BASE;
		usbhs_resources[2].end = OMAP44XX_HSUSB_EHCI_BASE + SZ_1K - 1;
		usbhs_resources[3].start = OMAP44XX_IRQ_EHCI;
		usbhs_resources[4].start = OMAP44XX_HSUSB_OHCI_BASE;
		usbhs_resources[4].end = OMAP44XX_HSUSB_OHCI_BASE + SZ_1K - 1;
		usbhs_resources[5].start = OMAP44XX_IRQ_OHCI;
		setup_4430ehci_io_mux(pdata->port_mode);
		setup_4430ohci_io_mux(pdata->port_mode);
	}

	if (platform_device_add_data(&usbhs_device,
				&usbhs_data, sizeof(usbhs_data)) < 0) {
		printk(KERN_ERR "USBHS platform_device_add_data failed\n");
		goto init_end;
	}

	if (platform_device_register(&usbhs_device) < 0)
		printk(KERN_ERR "USBHS platform_device_register failed\n");

init_end:
	return;
}

#else

void __init usbhs_init(const struct usbhs_omap_board_data *pdata)
{
}

#endif


