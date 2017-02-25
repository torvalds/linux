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
#include "omap_hwmod.h"
#include "omap_device.h"
#include "omap-pm.h"

#include "prm.h"
#include "common.h"
#include "i2c.h"

/* In register I2C_CON, Bit 15 is the I2C enable bit */
#define I2C_EN					BIT(15)
#define OMAP2_I2C_CON_OFFSET			0x24
#define OMAP4_I2C_CON_OFFSET			0xA4

#define MAX_OMAP_I2C_HWMOD_NAME_LEN	16

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
		pr_warn("%s: %s: softreset failed (waited %d usec)\n",
			__func__, oh->name, MAX_MODULE_SOFTRESET_WAIT);
	else
		pr_debug("%s: %s: softreset in %d usec\n", __func__,
			oh->name, c);

	return 0;
}
