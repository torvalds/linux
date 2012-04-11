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

#include <linux/wl12xx.h>

#include "../wlcore/wlcore.h"
#include "../wlcore/debug.h"
#include "../wlcore/io.h"
#include "../wlcore/acx.h"
#include "../wlcore/boot.h"

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

static void wl12xx_top_reg_write(struct wl1271 *wl, int addr, u16 val)
{
	/* write address >> 1 + 0x30000 to OCP_POR_CTR */
	addr = (addr >> 1) + 0x30000;
	wl1271_write32(wl, WL12XX_OCP_POR_CTR, addr);

	/* write value to OCP_POR_WDATA */
	wl1271_write32(wl, WL12XX_OCP_DATA_WRITE, val);

	/* write 1 to OCP_CMD */
	wl1271_write32(wl, WL12XX_OCP_CMD, OCP_CMD_WRITE);
}

static u16 wl12xx_top_reg_read(struct wl1271 *wl, int addr)
{
	u32 val;
	int timeout = OCP_CMD_LOOP;

	/* write address >> 1 + 0x30000 to OCP_POR_CTR */
	addr = (addr >> 1) + 0x30000;
	wl1271_write32(wl, WL12XX_OCP_POR_CTR, addr);

	/* write 2 to OCP_CMD */
	wl1271_write32(wl, WL12XX_OCP_CMD, OCP_CMD_READ);

	/* poll for data ready */
	do {
		val = wl1271_read32(wl, WL12XX_OCP_DATA_READ);
	} while (!(val & OCP_READY_MASK) && --timeout);

	if (!timeout) {
		wl1271_warning("Top register access timed out.");
		return 0xffff;
	}

	/* check data status and return if OK */
	if ((val & OCP_STATUS_MASK) == OCP_STATUS_OK)
		return val & 0xffff;
	else {
		wl1271_warning("Top register access returned error.");
		return 0xffff;
	}
}

static int wl128x_switch_tcxo_to_fref(struct wl1271 *wl)
{
	u16 spare_reg;

	/* Mask bits [2] & [8:4] in the sys_clk_cfg register */
	spare_reg = wl12xx_top_reg_read(wl, WL_SPARE_REG);
	if (spare_reg == 0xFFFF)
		return -EFAULT;
	spare_reg |= (BIT(3) | BIT(5) | BIT(6));
	wl12xx_top_reg_write(wl, WL_SPARE_REG, spare_reg);

	/* Enable FREF_CLK_REQ & mux MCS and coex PLLs to FREF */
	wl12xx_top_reg_write(wl, SYS_CLK_CFG_REG,
			     WL_CLK_REQ_TYPE_PG2 | MCS_PLL_CLK_SEL_FREF);

	/* Delay execution for 15msec, to let the HW settle */
	mdelay(15);

	return 0;
}

static bool wl128x_is_tcxo_valid(struct wl1271 *wl)
{
	u16 tcxo_detection;

	tcxo_detection = wl12xx_top_reg_read(wl, TCXO_CLK_DETECT_REG);
	if (tcxo_detection & TCXO_DET_FAILED)
		return false;

	return true;
}

static bool wl128x_is_fref_valid(struct wl1271 *wl)
{
	u16 fref_detection;

	fref_detection = wl12xx_top_reg_read(wl, FREF_CLK_DETECT_REG);
	if (fref_detection & FREF_CLK_DETECT_FAIL)
		return false;

	return true;
}

static int wl128x_manually_configure_mcs_pll(struct wl1271 *wl)
{
	wl12xx_top_reg_write(wl, MCS_PLL_M_REG, MCS_PLL_M_REG_VAL);
	wl12xx_top_reg_write(wl, MCS_PLL_N_REG, MCS_PLL_N_REG_VAL);
	wl12xx_top_reg_write(wl, MCS_PLL_CONFIG_REG, MCS_PLL_CONFIG_REG_VAL);

	return 0;
}

static int wl128x_configure_mcs_pll(struct wl1271 *wl, int clk)
{
	u16 spare_reg;
	u16 pll_config;
	u8 input_freq;

	/* Mask bits [3:1] in the sys_clk_cfg register */
	spare_reg = wl12xx_top_reg_read(wl, WL_SPARE_REG);
	if (spare_reg == 0xFFFF)
		return -EFAULT;
	spare_reg |= BIT(2);
	wl12xx_top_reg_write(wl, WL_SPARE_REG, spare_reg);

	/* Handle special cases of the TCXO clock */
	if (wl->tcxo_clock == WL12XX_TCXOCLOCK_16_8 ||
	    wl->tcxo_clock == WL12XX_TCXOCLOCK_33_6)
		return wl128x_manually_configure_mcs_pll(wl);

	/* Set the input frequency according to the selected clock source */
	input_freq = (clk & 1) + 1;

	pll_config = wl12xx_top_reg_read(wl, MCS_PLL_CONFIG_REG);
	if (pll_config == 0xFFFF)
		return -EFAULT;
	pll_config |= (input_freq << MCS_SEL_IN_FREQ_SHIFT);
	pll_config |= MCS_PLL_ENABLE_HP;
	wl12xx_top_reg_write(wl, MCS_PLL_CONFIG_REG, pll_config);

	return 0;
}

/*
 * WL128x has two clocks input - TCXO and FREF.
 * TCXO is the main clock of the device, while FREF is used to sync
 * between the GPS and the cellular modem.
 * In cases where TCXO is 32.736MHz or 16.368MHz, the FREF will be used
 * as the WLAN/BT main clock.
 */
static int wl128x_boot_clk(struct wl1271 *wl, int *selected_clock)
{
	u16 sys_clk_cfg;

	/* For XTAL-only modes, FREF will be used after switching from TCXO */
	if (wl->ref_clock == WL12XX_REFCLOCK_26_XTAL ||
	    wl->ref_clock == WL12XX_REFCLOCK_38_XTAL) {
		if (!wl128x_switch_tcxo_to_fref(wl))
			return -EINVAL;
		goto fref_clk;
	}

	/* Query the HW, to determine which clock source we should use */
	sys_clk_cfg = wl12xx_top_reg_read(wl, SYS_CLK_CFG_REG);
	if (sys_clk_cfg == 0xFFFF)
		return -EINVAL;
	if (sys_clk_cfg & PRCM_CM_EN_MUX_WLAN_FREF)
		goto fref_clk;

	/* If TCXO is either 32.736MHz or 16.368MHz, switch to FREF */
	if (wl->tcxo_clock == WL12XX_TCXOCLOCK_16_368 ||
	    wl->tcxo_clock == WL12XX_TCXOCLOCK_32_736) {
		if (!wl128x_switch_tcxo_to_fref(wl))
			return -EINVAL;
		goto fref_clk;
	}

	/* TCXO clock is selected */
	if (!wl128x_is_tcxo_valid(wl))
		return -EINVAL;
	*selected_clock = wl->tcxo_clock;
	goto config_mcs_pll;

fref_clk:
	/* FREF clock is selected */
	if (!wl128x_is_fref_valid(wl))
		return -EINVAL;
	*selected_clock = wl->ref_clock;

config_mcs_pll:
	return wl128x_configure_mcs_pll(wl, *selected_clock);
}

static int wl127x_boot_clk(struct wl1271 *wl)
{
	u32 pause;
	u32 clk;

	if (WL127X_PG_GET_MAJOR(wl->hw_pg_ver) < 3)
		wl->quirks |= WLCORE_QUIRK_END_OF_TRANSACTION;

	if (wl->ref_clock == CONF_REF_CLK_19_2_E ||
	    wl->ref_clock == CONF_REF_CLK_38_4_E ||
	    wl->ref_clock == CONF_REF_CLK_38_4_M_XTAL)
		/* ref clk: 19.2/38.4/38.4-XTAL */
		clk = 0x3;
	else if (wl->ref_clock == CONF_REF_CLK_26_E ||
		 wl->ref_clock == CONF_REF_CLK_52_E)
		/* ref clk: 26/52 */
		clk = 0x5;
	else
		return -EINVAL;

	if (wl->ref_clock != CONF_REF_CLK_19_2_E) {
		u16 val;
		/* Set clock type (open drain) */
		val = wl12xx_top_reg_read(wl, OCP_REG_CLK_TYPE);
		val &= FREF_CLK_TYPE_BITS;
		wl12xx_top_reg_write(wl, OCP_REG_CLK_TYPE, val);

		/* Set clock pull mode (no pull) */
		val = wl12xx_top_reg_read(wl, OCP_REG_CLK_PULL);
		val |= NO_PULL;
		wl12xx_top_reg_write(wl, OCP_REG_CLK_PULL, val);
	} else {
		u16 val;
		/* Set clock polarity */
		val = wl12xx_top_reg_read(wl, OCP_REG_CLK_POLARITY);
		val &= FREF_CLK_POLARITY_BITS;
		val |= CLK_REQ_OUTN_SEL;
		wl12xx_top_reg_write(wl, OCP_REG_CLK_POLARITY, val);
	}

	wl1271_write32(wl, WL12XX_PLL_PARAMETERS, clk);

	pause = wl1271_read32(wl, WL12XX_PLL_PARAMETERS);

	wl1271_debug(DEBUG_BOOT, "pause1 0x%x", pause);

	pause &= ~(WU_COUNTER_PAUSE_VAL);
	pause |= WU_COUNTER_PAUSE_VAL;
	wl1271_write32(wl, WL12XX_WU_COUNTER_PAUSE, pause);

	return 0;
}

static int wl1271_boot_soft_reset(struct wl1271 *wl)
{
	unsigned long timeout;
	u32 boot_data;

	/* perform soft reset */
	wl1271_write32(wl, WL12XX_SLV_SOFT_RESET, ACX_SLV_SOFT_RESET_BIT);

	/* SOFT_RESET is self clearing */
	timeout = jiffies + usecs_to_jiffies(SOFT_RESET_MAX_TIME);
	while (1) {
		boot_data = wl1271_read32(wl, WL12XX_SLV_SOFT_RESET);
		wl1271_debug(DEBUG_BOOT, "soft reset bootdata 0x%x", boot_data);
		if ((boot_data & ACX_SLV_SOFT_RESET_BIT) == 0)
			break;

		if (time_after(jiffies, timeout)) {
			/* 1.2 check pWhalBus->uSelfClearTime if the
			 * timeout was reached */
			wl1271_error("soft reset timeout");
			return -1;
		}

		udelay(SOFT_RESET_STALL_TIME);
	}

	/* disable Rx/Tx */
	wl1271_write32(wl, WL12XX_ENABLE, 0x0);

	/* disable auto calibration on start*/
	wl1271_write32(wl, WL12XX_SPARE_A2, 0xffff);

	return 0;
}

static int wl12xx_pre_boot(struct wl1271 *wl)
{
	int ret = 0;
	u32 clk;
	int selected_clock = -1;

	if (wl->chip.id == CHIP_ID_1283_PG20) {
		ret = wl128x_boot_clk(wl, &selected_clock);
		if (ret < 0)
			goto out;
	} else {
		ret = wl127x_boot_clk(wl);
		if (ret < 0)
			goto out;
	}

	/* Continue the ELP wake up sequence */
	wl1271_write32(wl, WL12XX_WELP_ARM_COMMAND, WELP_ARM_COMMAND_VAL);
	udelay(500);

	wlcore_set_partition(wl, &wl->ptable[PART_DRPW]);

	/* Read-modify-write DRPW_SCRATCH_START register (see next state)
	   to be used by DRPw FW. The RTRIM value will be added by the FW
	   before taking DRPw out of reset */

	clk = wl1271_read32(wl, WL12XX_DRPW_SCRATCH_START);

	wl1271_debug(DEBUG_BOOT, "clk2 0x%x", clk);

	if (wl->chip.id == CHIP_ID_1283_PG20)
		clk |= ((selected_clock & 0x3) << 1) << 4;
	else
		clk |= (wl->ref_clock << 1) << 4;

	wl1271_write32(wl, WL12XX_DRPW_SCRATCH_START, clk);

	wlcore_set_partition(wl, &wl->ptable[PART_WORK]);

	/* Disable interrupts */
	wlcore_write_reg(wl, REG_INTERRUPT_MASK, WL1271_ACX_INTR_ALL);

	ret = wl1271_boot_soft_reset(wl);
	if (ret < 0)
		goto out;

out:
	return ret;
}

static void wl12xx_pre_upload(struct wl1271 *wl)
{
	u32 tmp;

	/* write firmware's last address (ie. it's length) to
	 * ACX_EEPROMLESS_IND_REG */
	wl1271_debug(DEBUG_BOOT, "ACX_EEPROMLESS_IND_REG");

	wl1271_write32(wl, WL12XX_EEPROMLESS_IND, WL12XX_EEPROMLESS_IND);

	tmp = wlcore_read_reg(wl, REG_CHIP_ID_B);

	wl1271_debug(DEBUG_BOOT, "chip id 0x%x", tmp);

	/* 6. read the EEPROM parameters */
	tmp = wl1271_read32(wl, WL12XX_SCR_PAD2);

	/* WL1271: The reference driver skips steps 7 to 10 (jumps directly
	 * to upload_fw) */

	if (wl->chip.id == CHIP_ID_1283_PG20)
		wl12xx_top_reg_write(wl, SDIO_IO_DS, HCI_IO_DS_6MA);
}

static void wl12xx_enable_interrupts(struct wl1271 *wl)
{
	u32 polarity;

	polarity = wl12xx_top_reg_read(wl, OCP_REG_POLARITY);

	/* We use HIGH polarity, so unset the LOW bit */
	polarity &= ~POLARITY_LOW;
	wl12xx_top_reg_write(wl, OCP_REG_POLARITY, polarity);

	wlcore_write_reg(wl, REG_INTERRUPT_MASK, WL1271_ACX_ALL_EVENTS_VECTOR);

	wlcore_enable_interrupts(wl);
	wlcore_write_reg(wl, REG_INTERRUPT_MASK,
			 WL1271_ACX_INTR_ALL & ~(WL1271_INTR_MASK));

	wl1271_write32(wl, WL12XX_HI_CFG, HI_CFG_DEF_VAL);
}

static int wl12xx_boot(struct wl1271 *wl)
{
	int ret;

	ret = wl12xx_pre_boot(wl);
	if (ret < 0)
		goto out;

	ret = wlcore_boot_upload_nvs(wl);
	if (ret < 0)
		goto out;

	wl12xx_pre_upload(wl);

	ret = wlcore_boot_upload_firmware(wl);
	if (ret < 0)
		goto out;

	ret = wlcore_boot_run_firmware(wl);
	if (ret < 0)
		goto out;

	wl12xx_enable_interrupts(wl);

out:
	return ret;
}

static bool wl12xx_mac_in_fuse(struct wl1271 *wl)
{
	bool supported = false;
	u8 major, minor;

	if (wl->chip.id == CHIP_ID_1283_PG20) {
		major = WL128X_PG_GET_MAJOR(wl->hw_pg_ver);
		minor = WL128X_PG_GET_MINOR(wl->hw_pg_ver);

		/* in wl128x we have the MAC address if the PG is >= (2, 1) */
		if (major > 2 || (major == 2 && minor >= 1))
			supported = true;
	} else {
		major = WL127X_PG_GET_MAJOR(wl->hw_pg_ver);
		minor = WL127X_PG_GET_MINOR(wl->hw_pg_ver);

		/* in wl127x we have the MAC address if the PG is >= (3, 1) */
		if (major == 3 && minor >= 1)
			supported = true;
	}

	wl1271_debug(DEBUG_PROBE,
		     "PG Ver major = %d minor = %d, MAC %s present",
		     major, minor, supported ? "is" : "is not");

	return supported;
}

static void wl12xx_get_fuse_mac(struct wl1271 *wl)
{
	u32 mac1, mac2;

	wlcore_set_partition(wl, &wl->ptable[PART_DRPW]);

	mac1 = wl1271_read32(wl, WL12XX_REG_FUSE_BD_ADDR_1);
	mac2 = wl1271_read32(wl, WL12XX_REG_FUSE_BD_ADDR_2);

	/* these are the two parts of the BD_ADDR */
	wl->fuse_oui_addr = ((mac2 & 0xffff) << 8) +
		((mac1 & 0xff000000) >> 24);
	wl->fuse_nic_addr = mac1 & 0xffffff;

	wlcore_set_partition(wl, &wl->ptable[PART_DOWN]);
}

static s8 wl12xx_get_pg_ver(struct wl1271 *wl)
{
	u32 die_info;

	if (wl->chip.id == CHIP_ID_1283_PG20)
		die_info = wl12xx_top_reg_read(wl, WL128X_REG_FUSE_DATA_2_1);
	else
		die_info = wl12xx_top_reg_read(wl, WL127X_REG_FUSE_DATA_2_1);

	return (s8) (die_info & PG_VER_MASK) >> PG_VER_OFFSET;
}

static void wl12xx_get_mac(struct wl1271 *wl)
{
	if (wl12xx_mac_in_fuse(wl))
		wl12xx_get_fuse_mac(wl);
}

static struct wlcore_ops wl12xx_ops = {
	.identify_chip	= wl12xx_identify_chip,
	.boot		= wl12xx_boot,
	.get_pg_ver	= wl12xx_get_pg_ver,
	.get_mac	= wl12xx_get_mac,
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
