/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2022, Google LLC
 */

#ifndef _USB_MISC_ONBOARD_USB_DEV_H
#define _USB_MISC_ONBOARD_USB_DEV_H

#define MAX_SUPPLIES 2

struct onboard_dev_pdata {
	unsigned long reset_us;		/* reset pulse width in us */
	unsigned long power_on_delay_us; /* power on delay in us */
	unsigned int num_supplies;	/* number of supplies */
	const char * const supply_names[MAX_SUPPLIES];
	bool is_hub;
};

static const struct onboard_dev_pdata microchip_usb424_data = {
	.reset_us = 1,
	.num_supplies = 1,
	.supply_names = { "vdd" },
	.is_hub = true,
};

static const struct onboard_dev_pdata microchip_usb2514_data = {
	.reset_us = 1,
	.num_supplies = 2,
	.supply_names = { "vdd", "vdda" },
	.is_hub = true,
};

static const struct onboard_dev_pdata microchip_usb5744_data = {
	.reset_us = 0,
	.power_on_delay_us = 10000,
	.num_supplies = 2,
	.supply_names = { "vdd", "vdd2" },
	.is_hub = true,
};

static const struct onboard_dev_pdata parade_ps5511_data = {
	.reset_us = 500,
	.num_supplies = 2,
	.supply_names = { "vddd11", "vdd33"},
	.is_hub = true,
};

static const struct onboard_dev_pdata realtek_rts5411_data = {
	.reset_us = 0,
	.num_supplies = 1,
	.supply_names = { "vdd" },
	.is_hub = true,
};

static const struct onboard_dev_pdata realtek_rtl8188etv_data = {
	.reset_us = 0,
	.num_supplies = 1,
	.supply_names = { "vdd" },
	.is_hub = false,
};

static const struct onboard_dev_pdata ti_tusb8020b_data = {
	.reset_us = 3000,
	.num_supplies = 1,
	.supply_names = { "vdd" },
	.is_hub = true,
};

static const struct onboard_dev_pdata ti_tusb8041_data = {
	.reset_us = 3000,
	.num_supplies = 1,
	.supply_names = { "vdd" },
	.is_hub = true,
};

static const struct onboard_dev_pdata bison_intcamera_data = {
	.reset_us = 1000,
	.num_supplies = 1,
	.supply_names = { "vdd" },
	.is_hub = false,
};

static const struct onboard_dev_pdata cypress_hx3_data = {
	.reset_us = 10000,
	.num_supplies = 2,
	.supply_names = { "vdd", "vdd2" },
	.is_hub = true,
};

static const struct onboard_dev_pdata cypress_hx2vl_data = {
	.reset_us = 1,
	.num_supplies = 1,
	.supply_names = { "vdd" },
	.is_hub = true,
};

static const struct onboard_dev_pdata genesys_gl850g_data = {
	.reset_us = 3,
	.num_supplies = 1,
	.supply_names = { "vdd" },
	.is_hub = true,
};

static const struct onboard_dev_pdata genesys_gl852g_data = {
	.reset_us = 50,
	.num_supplies = 1,
	.supply_names = { "vdd" },
	.is_hub = true,
};

static const struct onboard_dev_pdata vialab_vl817_data = {
	.reset_us = 10,
	.num_supplies = 1,
	.supply_names = { "vdd" },
	.is_hub = true,
};

static const struct onboard_dev_pdata xmos_xvf3500_data = {
	.reset_us = 1,
	.num_supplies = 2,
	.supply_names = { "vdd", "vddio" },
	.is_hub = false,
};

static const struct of_device_id onboard_dev_match[] = {
	{ .compatible = "usb424,2412", .data = &microchip_usb424_data, },
	{ .compatible = "usb424,2514", .data = &microchip_usb2514_data, },
	{ .compatible = "usb424,2517", .data = &microchip_usb424_data, },
	{ .compatible = "usb424,2744", .data = &microchip_usb5744_data, },
	{ .compatible = "usb424,5744", .data = &microchip_usb5744_data, },
	{ .compatible = "usb451,8025", .data = &ti_tusb8020b_data, },
	{ .compatible = "usb451,8027", .data = &ti_tusb8020b_data, },
	{ .compatible = "usb451,8140", .data = &ti_tusb8041_data, },
	{ .compatible = "usb451,8142", .data = &ti_tusb8041_data, },
	{ .compatible = "usb451,8440", .data = &ti_tusb8041_data, },
	{ .compatible = "usb451,8442", .data = &ti_tusb8041_data, },
	{ .compatible = "usb4b4,6504", .data = &cypress_hx3_data, },
	{ .compatible = "usb4b4,6506", .data = &cypress_hx3_data, },
	{ .compatible = "usb4b4,6570", .data = &cypress_hx2vl_data, },
	{ .compatible = "usb5e3,608", .data = &genesys_gl850g_data, },
	{ .compatible = "usb5e3,610", .data = &genesys_gl852g_data, },
	{ .compatible = "usb5e3,620", .data = &genesys_gl852g_data, },
	{ .compatible = "usb5e3,626", .data = &genesys_gl852g_data, },
	{ .compatible = "usbbda,179", .data = &realtek_rtl8188etv_data, },
	{ .compatible = "usbbda,411", .data = &realtek_rts5411_data, },
	{ .compatible = "usbbda,5411", .data = &realtek_rts5411_data, },
	{ .compatible = "usbbda,414", .data = &realtek_rts5411_data, },
	{ .compatible = "usbbda,5414", .data = &realtek_rts5411_data, },
	{ .compatible = "usb1da0,5511", .data = &parade_ps5511_data, },
	{ .compatible = "usb1da0,55a1", .data = &parade_ps5511_data, },
	{ .compatible = "usb2109,817", .data = &vialab_vl817_data, },
	{ .compatible = "usb2109,2817", .data = &vialab_vl817_data, },
	{ .compatible = "usb20b1,0013", .data = &xmos_xvf3500_data, },
	{ .compatible = "usb5986,1198", .data = &bison_intcamera_data, },
	{}
};

#endif /* _USB_MISC_ONBOARD_USB_DEV_H */
