/*
 *  Copyright (C) 2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Device access for Altera MAX5 Arria10 System Control
 * Adapted from DA9052
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mfd/a10sycon.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/slab.h>

static bool a10sycon_reg_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case A10SYCON_LED_RD_REG:
	case A10SYCON_PBDSW_RD_REG:
	case A10SYCON_PBDSW_CLR_REG:
	case A10SYCON_PBDSW_IRQ_RD_REG:
	case A10SYCON_PWR_GOOD1_RD_REG:
	case A10SYCON_PWR_GOOD2_RD_REG:
	case A10SYCON_PWR_GOOD3_RD_REG:
	case A10SYCON_FMCAB_RD_REG:
	case A10SYCON_HPS_RST_RD_REG:
	case A10SYCON_USB_QSPI_RD_REG:
	case A10SYCON_SFPA_RD_REG:
	case A10SYCON_SFPB_RD_REG:
	case A10SYCON_I2C_M_RD_REG:
	case A10SYCON_WARM_RST_RD_REG:
	case A10SYCON_WR_KEY_RD_REG:
		return true;
	default:
		return false;
	}
}

static bool a10sycon_reg_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case A10SYCON_LED_WR_REG:
	case A10SYCON_PBDSW_CLR_REG:
	case A10SYCON_FMCAB_WR_REG:
	case A10SYCON_HPS_RST_WR_REG:
	case A10SYCON_USB_QSPI_WR_REG:
	case A10SYCON_SFPA_WR_REG:
	case A10SYCON_SFPB_WR_REG:
	case A10SYCON_WARM_RST_WR_REG:
	case A10SYCON_WR_KEY_WR_REG:
		return true;
	default:
		return false;
	}
}

static bool a10sycon_reg_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case A10SYCON_PBDSW_RD_REG:
	case A10SYCON_PBDSW_IRQ_RD_REG:
	case A10SYCON_PWR_GOOD1_RD_REG:
	case A10SYCON_PWR_GOOD2_RD_REG:
	case A10SYCON_PWR_GOOD3_RD_REG:
	case A10SYCON_HPS_RST_RD_REG:
	case A10SYCON_I2C_M_RD_REG:
	case A10SYCON_WARM_RST_RD_REG:
	case A10SYCON_WR_KEY_RD_REG:
		return true;
	default:
		return false;
	}
}

struct regmap_config a10sycon_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.cache_type = REGCACHE_NONE,

	.use_single_rw = true,

	.max_register = A10SYCON_WR_KEY_RD_REG,
	.readable_reg = a10sycon_reg_readable,
	.writeable_reg = a10sycon_reg_writeable,
	.volatile_reg = a10sycon_reg_volatile,
};
EXPORT_SYMBOL_GPL(a10sycon_regmap_config);

int a10sycon_device_init(struct a10sycon *a10sc)
{
	int ret;

	init_completion(&a10sc->done);

	return 0;
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Thor Thayer");
MODULE_DESCRIPTION("Altera Arria10 System Control Chip");
