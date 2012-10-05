/*
 * Helper module for board specific I2C bus registration
 *
 * Copyright (C) 2009 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "soc.h"
#include "common.h"
#include "omap_hwmod.h"
#include "omap_device.h"

#include "mux.h"
#include "i2c.h"

/* In register I2C_CON, Bit 15 is the I2C enable bit */
#define I2C_EN					BIT(15)
#define OMAP2_I2C_CON_OFFSET			0x24
#define OMAP4_I2C_CON_OFFSET			0xA4

/* Maximum microseconds to wait for OMAP module to softreset */
#define MAX_MODULE_SOFTRESET_WAIT	10000

#define MAX_OMAP_I2C_HWMOD_NAME_LEN	16

static void __init omap2_i2c_mux_pins(int bus_id)
{
	char mux_name[sizeof("i2c2_scl.i2c2_scl")];

	/* First I2C bus is not muxable */
	if (bus_id == 1)
		return;

	sprintf(mux_name, "i2c%i_scl.i2c%i_scl", bus_id, bus_id);
	omap_mux_init_signal(mux_name, OMAP_PIN_INPUT);
	sprintf(mux_name, "i2c%i_sda.i2c%i_sda", bus_id, bus_id);
	omap_mux_init_signal(mux_name, OMAP_PIN_INPUT);
}

/**
 * omap_i2c_reset - reset the omap i2c module.
 * @oh: struct omap_hwmod *
 *
 * The i2c moudle in omap2, omap3 had a special sequence to reset. The
 * sequence is:
 * - Disable the I2C.
 * - Write to SOFTRESET bit.
 * - Enable the I2C.
 * - Poll on the RESETDONE bit.
 * The sequence is implemented in below function. This is called for 2420,
 * 2430 and omap3.
 */
int omap_i2c_reset(struct omap_hwmod *oh)
{
	u32 v;
	u16 i2c_con;
	int c = 0;

	if (oh->class->rev == OMAP_I2C_IP_VERSION_2) {
		i2c_con = OMAP4_I2C_CON_OFFSET;
	} else if (oh->class->rev == OMAP_I2C_IP_VERSION_1) {
		i2c_con = OMAP2_I2C_CON_OFFSET;
	} else {
		WARN(1, "Cannot reset I2C block %s: unsupported revision\n",
		     oh->name);
		return -EINVAL;
	}

	/* Disable I2C */
	v = omap_hwmod_read(oh, i2c_con);
	v &= ~I2C_EN;
	omap_hwmod_write(v, oh, i2c_con);

	/* Write to the SOFTRESET bit */
	omap_hwmod_softreset(oh);

	/* Enable I2C */
	v = omap_hwmod_read(oh, i2c_con);
	v |= I2C_EN;
	omap_hwmod_write(v, oh, i2c_con);

	/* Poll on RESETDONE bit */
	omap_test_timeout((omap_hwmod_read(oh,
				oh->class->sysc->syss_offs)
				& SYSS_RESETDONE_MASK),
				MAX_MODULE_SOFTRESET_WAIT, c);

	if (c == MAX_MODULE_SOFTRESET_WAIT)
		pr_warning("%s: %s: softreset failed (waited %d usec)\n",
			__func__, oh->name, MAX_MODULE_SOFTRESET_WAIT);
	else
		pr_debug("%s: %s: softreset in %d usec\n", __func__,
			oh->name, c);

	return 0;
}

static const char name[] = "omap_i2c";

int __init omap_i2c_add_bus(struct omap_i2c_bus_platform_data *i2c_pdata,
				int bus_id)
{
	int l;
	struct omap_hwmod *oh;
	struct platform_device *pdev;
	char oh_name[MAX_OMAP_I2C_HWMOD_NAME_LEN];
	struct omap_i2c_bus_platform_data *pdata;
	struct omap_i2c_dev_attr *dev_attr;

	omap2_i2c_mux_pins(bus_id);

	l = snprintf(oh_name, MAX_OMAP_I2C_HWMOD_NAME_LEN, "i2c%d", bus_id);
	WARN(l >= MAX_OMAP_I2C_HWMOD_NAME_LEN,
		"String buffer overflow in I2C%d device setup\n", bus_id);
	oh = omap_hwmod_lookup(oh_name);
	if (!oh) {
			pr_err("Could not look up %s\n", oh_name);
			return -EEXIST;
	}

	pdata = i2c_pdata;
	/*
	 * pass the hwmod class's CPU-specific knowledge of I2C IP revision in
	 * use, and functionality implementation flags, up to the OMAP I2C
	 * driver via platform data
	 */
	pdata->rev = oh->class->rev;

	dev_attr = (struct omap_i2c_dev_attr *)oh->dev_attr;
	pdata->flags = dev_attr->flags;

	pdev = omap_device_build(name, bus_id, oh, pdata,
			sizeof(struct omap_i2c_bus_platform_data),
			NULL, 0, 0);
	WARN(IS_ERR(pdev), "Could not build omap_device for %s\n", name);

	return PTR_RET(pdev);
}

