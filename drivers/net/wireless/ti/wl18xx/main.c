/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2011 Texas Instruments
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

#include <linux/module.h>
#include <linux/platform_device.h>

#include "../wlcore/wlcore.h"
#include "../wlcore/debug.h"

#include "reg.h"

static const struct wlcore_partition_set wl18xx_ptable[PART_TABLE_LEN] = {
	[PART_TOP_PRCM_ELP_SOC] = {
		.mem  = { .start = 0x00A02000, .size  = 0x00010000 },
		.reg  = { .start = 0x00807000, .size  = 0x00005000 },
		.mem2 = { .start = 0x00800000, .size  = 0x0000B000 },
		.mem3 = { .start = 0x00000000, .size  = 0x00000000 },
	},
	[PART_DOWN] = {
		.mem  = { .start = 0x00000000, .size  = 0x00014000 },
		.reg  = { .start = 0x00810000, .size  = 0x0000BFFF },
		.mem2 = { .start = 0x00000000, .size  = 0x00000000 },
		.mem3 = { .start = 0x00000000, .size  = 0x00000000 },
	},
	[PART_BOOT] = {
		.mem  = { .start = 0x00700000, .size = 0x0000030c },
		.reg  = { .start = 0x00802000, .size = 0x00014578 },
		.mem2 = { .start = 0x00B00404, .size = 0x00001000 },
		.mem3 = { .start = 0x00C00000, .size = 0x00000400 },
	},
	[PART_WORK] = {
		.mem  = { .start = 0x00800000, .size  = 0x000050FC },
		.reg  = { .start = 0x00B00404, .size  = 0x00001000 },
		.mem2 = { .start = 0x00C00000, .size  = 0x00000400 },
		.mem3 = { .start = 0x00000000, .size  = 0x00000000 },
	},
	[PART_PHY_INIT] = {
		/* TODO: use the phy_conf struct size here */
		.mem  = { .start = 0x80926000, .size = 252 },
		.reg  = { .start = 0x00000000, .size = 0x00000000 },
		.mem2 = { .start = 0x00000000, .size = 0x00000000 },
		.mem3 = { .start = 0x00000000, .size = 0x00000000 },
	},
};

static const int wl18xx_rtable[REG_TABLE_LEN] = {
	[REG_ECPU_CONTROL]		= WL18XX_REG_ECPU_CONTROL,
	[REG_INTERRUPT_NO_CLEAR]	= WL18XX_REG_INTERRUPT_NO_CLEAR,
	[REG_INTERRUPT_ACK]		= WL18XX_REG_INTERRUPT_ACK,
	[REG_COMMAND_MAILBOX_PTR]	= WL18XX_REG_COMMAND_MAILBOX_PTR,
	[REG_EVENT_MAILBOX_PTR]		= WL18XX_REG_EVENT_MAILBOX_PTR,
	[REG_INTERRUPT_TRIG]		= WL18XX_REG_INTERRUPT_TRIG_H,
	[REG_INTERRUPT_MASK]		= WL18XX_REG_INTERRUPT_MASK,
	[REG_PC_ON_RECOVERY]		= 0, /* TODO: where is the PC? */
	[REG_CHIP_ID_B]			= WL18XX_REG_CHIP_ID_B,
	[REG_CMD_MBOX_ADDRESS]		= WL18XX_CMD_MBOX_ADDRESS,

	/* data access memory addresses, used with partition translation */
	[REG_SLV_MEM_DATA]		= WL18XX_SLV_MEM_DATA,
	[REG_SLV_REG_DATA]		= WL18XX_SLV_REG_DATA,

	/* raw data access memory addresses */
	[REG_RAW_FW_STATUS_ADDR]	= WL18XX_FW_STATUS_ADDR,
};

/* TODO: maybe move to a new header file? */
#define WL18XX_FW_NAME "ti-connectivity/wl18xx-fw.bin"

static int wl18xx_identify_chip(struct wl1271 *wl)
{
	int ret = 0;

	switch (wl->chip.id) {
	case CHIP_ID_185x_PG10:
		wl1271_debug(DEBUG_BOOT, "chip id 0x%x (185x PG10)",
			     wl->chip.id);
		wl->sr_fw_name = WL18XX_FW_NAME;
		wl->quirks |= WLCORE_QUIRK_NO_ELP;

		/* TODO: need to blocksize alignment for RX/TX separately? */
		break;
	default:
		wl1271_warning("unsupported chip id: 0x%x", wl->chip.id);
		ret = -ENODEV;
		goto out;
	}

out:
	return ret;
}

static struct wlcore_ops wl18xx_ops = {
	.identify_chip = wl18xx_identify_chip,
};

int __devinit wl18xx_probe(struct platform_device *pdev)
{
	struct wl1271 *wl;
	struct ieee80211_hw *hw;

	hw = wlcore_alloc_hw(0);
	if (IS_ERR(hw)) {
		wl1271_error("can't allocate hw");
		return PTR_ERR(hw);
	}

	wl = hw->priv;
	wl->ops = &wl18xx_ops;
	wl->ptable = wl18xx_ptable;
	wl->rtable = wl18xx_rtable;

	return wlcore_probe(wl, pdev);
}

static const struct platform_device_id wl18xx_id_table[] __devinitconst = {
	{ "wl18xx", 0 },
	{  } /* Terminating Entry */
};
MODULE_DEVICE_TABLE(platform, wl18xx_id_table);

static struct platform_driver wl18xx_driver = {
	.probe		= wl18xx_probe,
	.remove		= __devexit_p(wlcore_remove),
	.id_table	= wl18xx_id_table,
	.driver = {
		.name	= "wl18xx_driver",
		.owner	= THIS_MODULE,
	}
};

static int __init wl18xx_init(void)
{
	return platform_driver_register(&wl18xx_driver);
}
module_init(wl18xx_init);

static void __exit wl18xx_exit(void)
{
	platform_driver_unregister(&wl18xx_driver);
}
module_exit(wl18xx_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Luciano Coelho <coelho@ti.com>");
MODULE_FIRMWARE(WL18XX_FW_NAME);
