/*
 * Apple Motion Sensor driver (PMU variant)
 *
 * Copyright (C) 2006 Michael Hanselmann (linux-kernel@hansmi.ch)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/adb.h>
#include <linux/pmu.h>

#include "ams.h"

/* Attitude */
#define AMS_X			0x00
#define AMS_Y			0x01
#define AMS_Z			0x02

/* Not exactly known, maybe chip vendor */
#define AMS_VENDOR		0x03

/* Freefall registers */
#define AMS_FF_CLEAR		0x04
#define AMS_FF_ENABLE		0x05
#define AMS_FF_LOW_LIMIT	0x06
#define AMS_FF_DEBOUNCE		0x07

/* Shock registers */
#define AMS_SHOCK_CLEAR		0x08
#define AMS_SHOCK_ENABLE	0x09
#define AMS_SHOCK_HIGH_LIMIT	0x0a
#define AMS_SHOCK_DEBOUNCE	0x0b

/* Global interrupt and power control register */
#define AMS_CONTROL		0x0c

static u8 ams_pmu_cmd;

static void ams_pmu_req_complete(struct adb_request *req)
{
	complete((struct completion *)req->arg);
}

/* Only call this function from task context */
static void ams_pmu_set_register(u8 reg, u8 value)
{
	static struct adb_request req;
	DECLARE_COMPLETION(req_complete);

	req.arg = &req_complete;
	if (pmu_request(&req, ams_pmu_req_complete, 4, ams_pmu_cmd, 0x00, reg, value))
		return;

	wait_for_completion(&req_complete);
}

/* Only call this function from task context */
static u8 ams_pmu_get_register(u8 reg)
{
	static struct adb_request req;
	DECLARE_COMPLETION(req_complete);

	req.arg = &req_complete;
	if (pmu_request(&req, ams_pmu_req_complete, 3, ams_pmu_cmd, 0x01, reg))
		return 0;

	wait_for_completion(&req_complete);

	if (req.reply_len > 0)
		return req.reply[0];
	else
		return 0;
}

/* Enables or disables the specified interrupts */
static void ams_pmu_set_irq(enum ams_irq reg, char enable)
{
	if (reg & AMS_IRQ_FREEFALL) {
		u8 val = ams_pmu_get_register(AMS_FF_ENABLE);
		if (enable)
			val |= 0x80;
		else
			val &= ~0x80;
		ams_pmu_set_register(AMS_FF_ENABLE, val);
	}

	if (reg & AMS_IRQ_SHOCK) {
		u8 val = ams_pmu_get_register(AMS_SHOCK_ENABLE);
		if (enable)
			val |= 0x80;
		else
			val &= ~0x80;
		ams_pmu_set_register(AMS_SHOCK_ENABLE, val);
	}

	if (reg & AMS_IRQ_GLOBAL) {
		u8 val = ams_pmu_get_register(AMS_CONTROL);
		if (enable)
			val |= 0x80;
		else
			val &= ~0x80;
		ams_pmu_set_register(AMS_CONTROL, val);
	}
}

static void ams_pmu_clear_irq(enum ams_irq reg)
{
	if (reg & AMS_IRQ_FREEFALL)
		ams_pmu_set_register(AMS_FF_CLEAR, 0x00);

	if (reg & AMS_IRQ_SHOCK)
		ams_pmu_set_register(AMS_SHOCK_CLEAR, 0x00);
}

static u8 ams_pmu_get_vendor(void)
{
	return ams_pmu_get_register(AMS_VENDOR);
}

static void ams_pmu_get_xyz(s8 *x, s8 *y, s8 *z)
{
	*x = ams_pmu_get_register(AMS_X);
	*y = ams_pmu_get_register(AMS_Y);
	*z = ams_pmu_get_register(AMS_Z);
}

static void ams_pmu_exit(void)
{
	/* Disable interrupts */
	ams_pmu_set_irq(AMS_IRQ_ALL, 0);

	/* Clear interrupts */
	ams_pmu_clear_irq(AMS_IRQ_ALL);

	ams_info.has_device = 0;

	printk(KERN_INFO "ams: Unloading\n");
}

int __init ams_pmu_init(struct device_node *np)
{
	u32 *prop;
	int result;

	mutex_lock(&ams_info.lock);

	/* Set implementation stuff */
	ams_info.of_node = np;
	ams_info.exit = ams_pmu_exit;
	ams_info.get_vendor = ams_pmu_get_vendor;
	ams_info.get_xyz = ams_pmu_get_xyz;
	ams_info.clear_irq = ams_pmu_clear_irq;
	ams_info.bustype = BUS_HOST;

	/* Get PMU command, should be 0x4e, but we can never know */
	prop = (u32*)get_property(ams_info.of_node, "reg", NULL);
	if (!prop) {
		result = -ENODEV;
		goto exit;
	}
	ams_pmu_cmd = ((*prop) >> 8) & 0xff;

	/* Disable interrupts */
	ams_pmu_set_irq(AMS_IRQ_ALL, 0);

	/* Clear interrupts */
	ams_pmu_clear_irq(AMS_IRQ_ALL);

	result = ams_sensor_attach();
	if (result < 0)
		goto exit;

	/* Set default values */
	ams_pmu_set_register(AMS_FF_LOW_LIMIT, 0x15);
	ams_pmu_set_register(AMS_FF_ENABLE, 0x08);
	ams_pmu_set_register(AMS_FF_DEBOUNCE, 0x14);

	ams_pmu_set_register(AMS_SHOCK_HIGH_LIMIT, 0x60);
	ams_pmu_set_register(AMS_SHOCK_ENABLE, 0x0f);
	ams_pmu_set_register(AMS_SHOCK_DEBOUNCE, 0x14);

	ams_pmu_set_register(AMS_CONTROL, 0x4f);

	/* Clear interrupts */
	ams_pmu_clear_irq(AMS_IRQ_ALL);

	ams_info.has_device = 1;

	/* Enable interrupts */
	ams_pmu_set_irq(AMS_IRQ_ALL, 1);

	printk(KERN_INFO "ams: Found PMU based motion sensor\n");

	result = 0;

exit:
	mutex_unlock(&ams_info.lock);

	return result;
}
