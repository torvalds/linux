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
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/usb/phy.h>
#include <linux/usb/usb_phy_gen_xceiv.h>

#include "soc.h"
#include "omap_device.h"
#include "mux.h"
#include "usb.h"

#ifdef CONFIG_MFD_OMAP_USB_HOST

#define OMAP_USBHS_DEVICE	"usbhs_omap"
#define OMAP_USBTLL_DEVICE	"usbhs_tll"
#define	USBHS_UHH_HWMODNAME	"usb_host_hs"
#define USBHS_TLL_HWMODNAME	"usb_tll_hs"

/* MUX settings for EHCI pins */
/*
 * setup_ehci_io_mux - initialize IO pad mux for USBHOST
 */
static void __init setup_ehci_io_mux(const enum usbhs_omap_port_mode *port_mode)
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

static void __init setup_ohci_io_mux(const enum usbhs_omap_port_mode *port_mode)
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

void __init usbhs_init(struct usbhs_omap_platform_data *pdata)
{
	struct omap_hwmod	*uhh_hwm, *tll_hwm;
	struct platform_device	*pdev;
	int			bus_id = -1;

	if (cpu_is_omap34xx()) {
		setup_ehci_io_mux(pdata->port_mode);
		setup_ohci_io_mux(pdata->port_mode);

		if (omap_rev() <= OMAP3430_REV_ES2_1)
			pdata->single_ulpi_bypass = true;

	}

	uhh_hwm = omap_hwmod_lookup(USBHS_UHH_HWMODNAME);
	if (!uhh_hwm) {
		pr_err("Could not look up %s\n", USBHS_UHH_HWMODNAME);
		return;
	}

	tll_hwm = omap_hwmod_lookup(USBHS_TLL_HWMODNAME);
	if (!tll_hwm) {
		pr_err("Could not look up %s\n", USBHS_TLL_HWMODNAME);
		return;
	}

	pdev = omap_device_build(OMAP_USBTLL_DEVICE, bus_id, tll_hwm,
				pdata, sizeof(*pdata));
	if (IS_ERR(pdev)) {
		pr_err("Could not build hwmod device %s\n",
		       USBHS_TLL_HWMODNAME);
		return;
	}

	pdev = omap_device_build(OMAP_USBHS_DEVICE, bus_id, uhh_hwm,
				pdata, sizeof(*pdata));
	if (IS_ERR(pdev)) {
		pr_err("Could not build hwmod devices %s\n",
		       USBHS_UHH_HWMODNAME);
		return;
	}
}

#else

void __init usbhs_init(struct usbhs_omap_platform_data *pdata)
{
}

#endif

/* Template for PHY regulators */
static struct fixed_voltage_config hsusb_reg_config = {
	/* .supply_name filled later */
	.microvolts = 3300000,
	.gpio = -1,		/* updated later */
	.startup_delay = 70000, /* 70msec */
	.enable_high = 1,	/* updated later */
	.enabled_at_boot = 0,	/* keep in RESET */
	/* .init_data filled later */
};

static const char *nop_name = "usb_phy_gen_xceiv"; /* NOP PHY driver */
static const char *reg_name = "reg-fixed-voltage"; /* Regulator driver */

/**
 * usbhs_add_regulator - Add a gpio based fixed voltage regulator device
 * @name: name for the regulator
 * @dev_id: device id of the device this regulator supplies power to
 * @dev_supply: supply name that the device expects
 * @gpio: GPIO number
 * @polarity: 1 - Active high, 0 - Active low
 */
static int usbhs_add_regulator(char *name, char *dev_id, char *dev_supply,
						int gpio, int polarity)
{
	struct regulator_consumer_supply *supplies;
	struct regulator_init_data *reg_data;
	struct fixed_voltage_config *config;
	struct platform_device *pdev;
	struct platform_device_info pdevinfo;
	int ret = -ENOMEM;

	supplies = kzalloc(sizeof(*supplies), GFP_KERNEL);
	if (!supplies)
		return -ENOMEM;

	supplies->supply = dev_supply;
	supplies->dev_name = dev_id;

	reg_data = kzalloc(sizeof(*reg_data), GFP_KERNEL);
	if (!reg_data)
		goto err_data;

	reg_data->constraints.valid_ops_mask = REGULATOR_CHANGE_STATUS;
	reg_data->consumer_supplies = supplies;
	reg_data->num_consumer_supplies = 1;

	config = kmemdup(&hsusb_reg_config, sizeof(hsusb_reg_config),
			GFP_KERNEL);
	if (!config)
		goto err_config;

	config->supply_name = kstrdup(name, GFP_KERNEL);
	if (!config->supply_name)
		goto err_supplyname;

	config->gpio = gpio;
	config->enable_high = polarity;
	config->init_data = reg_data;

	/* create a regulator device */
	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.name = reg_name;
	pdevinfo.id = PLATFORM_DEVID_AUTO;
	pdevinfo.data = config;
	pdevinfo.size_data = sizeof(*config);

	pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		pr_err("%s: Failed registering regulator %s for %s : %d\n",
				__func__, name, dev_id, ret);
		goto err_register;
	}

	return 0;

err_register:
	kfree(config->supply_name);
err_supplyname:
	kfree(config);
err_config:
	kfree(reg_data);
err_data:
	kfree(supplies);
	return ret;
}

#define MAX_STR 20

int usbhs_init_phys(struct usbhs_phy_data *phy, int num_phys)
{
	char rail_name[MAX_STR];
	int i;
	struct platform_device *pdev;
	char *phy_id;
	struct platform_device_info pdevinfo;

	for (i = 0; i < num_phys; i++) {

		if (!phy->port) {
			pr_err("%s: Invalid port 0. Must start from 1\n",
						__func__);
			continue;
		}

		/* do we need a NOP PHY device ? */
		if (!gpio_is_valid(phy->reset_gpio) &&
			!gpio_is_valid(phy->vcc_gpio))
			continue;

		phy_id = kmalloc(MAX_STR, GFP_KERNEL);
		if (!phy_id) {
			pr_err("%s: kmalloc() failed\n", __func__);
			return -ENOMEM;
		}

		/* create a NOP PHY device */
		memset(&pdevinfo, 0, sizeof(pdevinfo));
		pdevinfo.name = nop_name;
		pdevinfo.id = phy->port;
		pdevinfo.data = phy->platform_data;
		pdevinfo.size_data =
			sizeof(struct usb_phy_gen_xceiv_platform_data);
		scnprintf(phy_id, MAX_STR, "usb_phy_gen_xceiv.%d",
					phy->port);
		pdev = platform_device_register_full(&pdevinfo);
		if (IS_ERR(pdev)) {
			pr_err("%s: Failed to register device %s : %ld\n",
				__func__,  phy_id, PTR_ERR(pdev));
			kfree(phy_id);
			continue;
		}

		usb_bind_phy("ehci-omap.0", phy->port - 1, phy_id);

		/* Do we need RESET regulator ? */
		if (gpio_is_valid(phy->reset_gpio)) {
			scnprintf(rail_name, MAX_STR,
					"hsusb%d_reset", phy->port);
			usbhs_add_regulator(rail_name, phy_id, "reset",
						phy->reset_gpio, 1);
		}

		/* Do we need VCC regulator ? */
		if (gpio_is_valid(phy->vcc_gpio)) {
			scnprintf(rail_name, MAX_STR, "hsusb%d_vcc", phy->port);
			usbhs_add_regulator(rail_name, phy_id, "vcc",
					phy->vcc_gpio, phy->vcc_polarity);
		}

		phy++;
	}

	return 0;
}
