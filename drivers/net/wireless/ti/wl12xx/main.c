/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2010 Nokia Corporation
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

#include <linux/err.h>

#include "../wlcore/wlcore.h"
#include "../wlcore/debug.h"
#include "../wlcore/io.h"

#include "reg.h"

static struct wlcore_partition_set wl12xx_ptable[PART_TABLE_LEN] = {
	[PART_DOWN] = {
		.mem = {
			.start = 0x00000000,
			.size  = 0x000177c0
		},
		.reg = {
			.start = REGISTERS_BASE,
			.size  = 0x00008800
		},
		.mem2 = {
			.start = 0x00000000,
			.size  = 0x00000000
		},
		.mem3 = {
			.start = 0x00000000,
			.size  = 0x00000000
		},
	},

	[PART_BOOT] = { /* in wl12xx we can use a mix of work and down
			 * partition here */
		.mem = {
			.start = 0x00040000,
			.size  = 0x00014fc0
		},
		.reg = {
			.start = REGISTERS_BASE,
			.size  = 0x00008800
		},
		.mem2 = {
			.start = 0x00000000,
			.size  = 0x00000000
		},
		.mem3 = {
			.start = 0x00000000,
			.size  = 0x00000000
		},
	},

	[PART_WORK] = {
		.mem = {
			.start = 0x00040000,
			.size  = 0x00014fc0
		},
		.reg = {
			.start = REGISTERS_BASE,
			.size  = 0x0000a000
		},
		.mem2 = {
			.start = 0x003004f8,
			.size  = 0x00000004
		},
		.mem3 = {
			.start = 0x00040404,
			.size  = 0x00000000
		},
	},

	[PART_DRPW] = {
		.mem = {
			.start = 0x00040000,
			.size  = 0x00014fc0
		},
		.reg = {
			.start = DRPW_BASE,
			.size  = 0x00006000
		},
		.mem2 = {
			.start = 0x00000000,
			.size  = 0x00000000
		},
		.mem3 = {
			.start = 0x00000000,
			.size  = 0x00000000
		}
	}
};

static const int wl12xx_rtable[REG_TABLE_LEN] = {
	[REG_ECPU_CONTROL]		= WL12XX_REG_ECPU_CONTROL,
	[REG_INTERRUPT_NO_CLEAR]	= WL12XX_REG_INTERRUPT_NO_CLEAR,
	[REG_INTERRUPT_ACK]		= WL12XX_REG_INTERRUPT_ACK,
	[REG_COMMAND_MAILBOX_PTR]	= WL12XX_REG_COMMAND_MAILBOX_PTR,
	[REG_EVENT_MAILBOX_PTR]		= WL12XX_REG_EVENT_MAILBOX_PTR,
	[REG_INTERRUPT_TRIG]		= WL12XX_REG_INTERRUPT_TRIG,
	[REG_INTERRUPT_MASK]		= WL12XX_REG_INTERRUPT_MASK,
	[REG_PC_ON_RECOVERY]		= WL12XX_SCR_PAD4,
	[REG_CHIP_ID_B]			= WL12XX_CHIP_ID_B,
	[REG_CMD_MBOX_ADDRESS]		= WL12XX_CMD_MBOX_ADDRESS,

	/* data access memory addresses, used with partition translation */
	[REG_SLV_MEM_DATA]		= WL1271_SLV_MEM_DATA,
	[REG_SLV_REG_DATA]		= WL1271_SLV_REG_DATA,

	/* raw data access memory addresses */
	[REG_RAW_FW_STATUS_ADDR]	= FW_STATUS_ADDR,
};

/* TODO: maybe move to a new header file? */
#define WL127X_FW_NAME_MULTI	"ti-connectivity/wl127x-fw-4-mr.bin"
#define WL127X_FW_NAME_SINGLE	"ti-connectivity/wl127x-fw-4-sr.bin"
#define WL127X_PLT_FW_NAME	"ti-connectivity/wl127x-fw-4-plt.bin"

#define WL128X_FW_NAME_MULTI	"ti-connectivity/wl128x-fw-4-mr.bin"
#define WL128X_FW_NAME_SINGLE	"ti-connectivity/wl128x-fw-4-sr.bin"
#define WL128X_PLT_FW_NAME	"ti-connectivity/wl128x-fw-4-plt.bin"

static int wl12xx_identify_chip(struct wl1271 *wl)
{
	int ret = 0;

	switch (wl->chip.id) {
	case CHIP_ID_1271_PG10:
		wl1271_warning("chip id 0x%x (1271 PG10) support is obsolete",
			       wl->chip.id);

		wl->quirks |= WLCORE_QUIRK_NO_BLOCKSIZE_ALIGNMENT;
		wl->plt_fw_name = WL127X_PLT_FW_NAME;
		wl->sr_fw_name = WL127X_FW_NAME_SINGLE;
		wl->mr_fw_name = WL127X_FW_NAME_MULTI;
		break;

	case CHIP_ID_1271_PG20:
		wl1271_debug(DEBUG_BOOT, "chip id 0x%x (1271 PG20)",
			     wl->chip.id);

		wl->quirks |= WLCORE_QUIRK_NO_BLOCKSIZE_ALIGNMENT;
		wl->plt_fw_name = WL127X_PLT_FW_NAME;
		wl->sr_fw_name = WL127X_FW_NAME_SINGLE;
		wl->mr_fw_name = WL127X_FW_NAME_MULTI;
		break;

	case CHIP_ID_1283_PG20:
		wl1271_debug(DEBUG_BOOT, "chip id 0x%x (1283 PG20)",
			     wl->chip.id);
		wl->plt_fw_name = WL128X_PLT_FW_NAME;
		wl->sr_fw_name = WL128X_FW_NAME_SINGLE;
		wl->mr_fw_name = WL128X_FW_NAME_MULTI;
		break;
	case CHIP_ID_1283_PG10:
	default:
		wl1271_warning("unsupported chip id: 0x%x", wl->chip.id);
		ret = -ENODEV;
		goto out;
	}

out:
	return ret;
}

static s8 wl12xx_get_pg_ver(struct wl1271 *wl)
{
	u32 die_info;

	if (wl->chip.id == CHIP_ID_1283_PG20)
		die_info = wl1271_top_reg_read(wl, WL128X_REG_FUSE_DATA_2_1);
	else
		die_info = wl1271_top_reg_read(wl, WL127X_REG_FUSE_DATA_2_1);

	return (s8) (die_info & PG_VER_MASK) >> PG_VER_OFFSET;
}

static struct wlcore_ops wl12xx_ops = {
	.identify_chip = wl12xx_identify_chip,
	.get_pg_ver     = wl12xx_get_pg_ver,
};

static int __devinit wl12xx_probe(struct platform_device *pdev)
{
	struct wl1271 *wl;
	struct ieee80211_hw *hw;

	hw = wlcore_alloc_hw();
	if (IS_ERR(hw)) {
		wl1271_error("can't allocate hw");
		return PTR_ERR(hw);
	}

	wl = hw->priv;
	wl->ops = &wl12xx_ops;
	wl->ptable = wl12xx_ptable;
	wl->rtable = wl12xx_rtable;

	return wlcore_probe(wl, pdev);
}

static const struct platform_device_id wl12xx_id_table[] __devinitconst = {
	{ "wl12xx", 0 },
	{  } /* Terminating Entry */
};
MODULE_DEVICE_TABLE(platform, wl12xx_id_table);

static struct platform_driver wl12xx_driver = {
	.probe		= wl12xx_probe,
	.remove		= __devexit_p(wlcore_remove),
	.id_table	= wl12xx_id_table,
	.driver = {
		.name	= "wl12xx_driver",
		.owner	= THIS_MODULE,
	}
};

static int __init wl12xx_init(void)
{
	return platform_driver_register(&wl12xx_driver);
}
module_init(wl12xx_init);

static void __exit wl12xx_exit(void)
{
	platform_driver_unregister(&wl12xx_driver);
}
module_exit(wl12xx_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Luciano Coelho <coelho@ti.com>");
MODULE_FIRMWARE(WL127X_FW_NAME_SINGLE);
MODULE_FIRMWARE(WL127X_FW_NAME_MULTI);
MODULE_FIRMWARE(WL127X_PLT_FW_NAME);
MODULE_FIRMWARE(WL128X_FW_NAME_SINGLE);
MODULE_FIRMWARE(WL128X_FW_NAME_MULTI);
MODULE_FIRMWARE(WL128X_PLT_FW_NAME);
