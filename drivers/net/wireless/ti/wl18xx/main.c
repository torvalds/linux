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
#include "../wlcore/io.h"
#include "../wlcore/acx.h"
#include "../wlcore/tx.h"
#include "../wlcore/boot.h"

#include "reg.h"
#include "conf.h"
#include "wl18xx.h"

#define WL18XX_TX_HW_BLOCK_SPARE        1
#define WL18XX_TX_HW_GEM_BLOCK_SPARE    2
#define WL18XX_TX_HW_BLOCK_SIZE         268

static struct wl18xx_conf wl18xx_default_conf = {
	.phy = {
		.phy_standalone			= 0x00,
		.primary_clock_setting_time	= 0x05,
		.clock_valid_on_wake_up		= 0x00,
		.secondary_clock_setting_time	= 0x05,
		.rdl				= 0x01,
		.auto_detect			= 0x00,
		.dedicated_fem			= FEM_NONE,
		.low_band_component		= COMPONENT_2_WAY_SWITCH,
		.low_band_component_type	= 0x05,
		.high_band_component		= COMPONENT_2_WAY_SWITCH,
		.high_band_component_type	= 0x09,
		.number_of_assembled_ant2_4	= 0x01,
		.number_of_assembled_ant5	= 0x01,
		.external_pa_dc2dc		= 0x00,
		.tcxo_ldo_voltage		= 0x00,
		.xtal_itrim_val			= 0x04,
		.srf_state			= 0x00,
		.io_configuration		= 0x01,
		.sdio_configuration		= 0x00,
		.settings			= 0x00,
		.enable_clpc			= 0x00,
		.enable_tx_low_pwr_on_siso_rdl	= 0x00,
		.rx_profile			= 0x00,
	},
};

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

static void wl18xx_set_clk(struct wl1271 *wl)
{
	/*
	 * TODO: this is hardcoded just for DVP/EVB, fix according to
	 * new unified_drv.
	 */
	wl1271_write32(wl, WL18XX_SCR_PAD2, 0xB3);

	wlcore_set_partition(wl, &wl->ptable[PART_TOP_PRCM_ELP_SOC]);
	wl1271_write32(wl, 0x00A02360, 0xD0078);
	wl1271_write32(wl, 0x00A0236c, 0x12);
	wl1271_write32(wl, 0x00A02390, 0x20118);
}

static void wl18xx_boot_soft_reset(struct wl1271 *wl)
{
	/* disable Rx/Tx */
	wl1271_write32(wl, WL18XX_ENABLE, 0x0);

	/* disable auto calibration on start*/
	wl1271_write32(wl, WL18XX_SPARE_A2, 0xffff);
}

static int wl18xx_pre_boot(struct wl1271 *wl)
{
	/* TODO: add hw_pg_ver reading */

	wl18xx_set_clk(wl);

	/* Continue the ELP wake up sequence */
	wl1271_write32(wl, WL18XX_WELP_ARM_COMMAND, WELP_ARM_COMMAND_VAL);
	udelay(500);

	wlcore_set_partition(wl, &wl->ptable[PART_BOOT]);

	/* Disable interrupts */
	wlcore_write_reg(wl, REG_INTERRUPT_MASK, WL1271_ACX_INTR_ALL);

	wl18xx_boot_soft_reset(wl);

	return 0;
}

static void wl18xx_pre_upload(struct wl1271 *wl)
{
	u32 tmp;

	wlcore_set_partition(wl, &wl->ptable[PART_BOOT]);

	/* TODO: check if this is all needed */
	wl1271_write32(wl, WL18XX_EEPROMLESS_IND, WL18XX_EEPROMLESS_IND);

	tmp = wlcore_read_reg(wl, REG_CHIP_ID_B);

	wl1271_debug(DEBUG_BOOT, "chip id 0x%x", tmp);

	tmp = wl1271_read32(wl, WL18XX_SCR_PAD2);
}

static void wl18xx_set_mac_and_phy(struct wl1271 *wl)
{
	struct wl18xx_mac_and_phy_params params;

	memset(&params, 0, sizeof(params));

	params.phy_standalone = wl18xx_default_conf.phy.phy_standalone;
	params.rdl = wl18xx_default_conf.phy.rdl;
	params.enable_clpc = wl18xx_default_conf.phy.enable_clpc;
	params.enable_tx_low_pwr_on_siso_rdl =
		wl18xx_default_conf.phy.enable_tx_low_pwr_on_siso_rdl;
	params.auto_detect = wl18xx_default_conf.phy.auto_detect;
	params.dedicated_fem = wl18xx_default_conf.phy.dedicated_fem;
	params.low_band_component = wl18xx_default_conf.phy.low_band_component;
	params.low_band_component_type =
		wl18xx_default_conf.phy.low_band_component_type;
	params.high_band_component =
		wl18xx_default_conf.phy.high_band_component;
	params.high_band_component_type =
		wl18xx_default_conf.phy.high_band_component_type;
	params.number_of_assembled_ant2_4 =
		wl18xx_default_conf.phy.number_of_assembled_ant2_4;
	params.number_of_assembled_ant5 =
		wl18xx_default_conf.phy.number_of_assembled_ant5;
	params.external_pa_dc2dc = wl18xx_default_conf.phy.external_pa_dc2dc;
	params.tcxo_ldo_voltage = wl18xx_default_conf.phy.tcxo_ldo_voltage;
	params.xtal_itrim_val = wl18xx_default_conf.phy.xtal_itrim_val;
	params.srf_state = wl18xx_default_conf.phy.srf_state;
	params.io_configuration = wl18xx_default_conf.phy.io_configuration;
	params.sdio_configuration = wl18xx_default_conf.phy.sdio_configuration;
	params.settings = wl18xx_default_conf.phy.settings;
	params.rx_profile = wl18xx_default_conf.phy.rx_profile;
	params.primary_clock_setting_time =
		wl18xx_default_conf.phy.primary_clock_setting_time;
	params.clock_valid_on_wake_up =
		wl18xx_default_conf.phy.clock_valid_on_wake_up;
	params.secondary_clock_setting_time =
		wl18xx_default_conf.phy.secondary_clock_setting_time;

	/* TODO: hardcoded for now */
	params.board_type = BOARD_TYPE_DVP_EVB_18XX;

	wlcore_set_partition(wl, &wl->ptable[PART_PHY_INIT]);
	wl1271_write(wl, WL18XX_PHY_INIT_MEM_ADDR, (u8 *)&params,
		     sizeof(params), false);
}

static void wl18xx_enable_interrupts(struct wl1271 *wl)
{
	wlcore_write_reg(wl, REG_INTERRUPT_MASK, WL1271_ACX_ALL_EVENTS_VECTOR);

	wlcore_enable_interrupts(wl);
	wlcore_write_reg(wl, REG_INTERRUPT_MASK,
			 WL1271_ACX_INTR_ALL & ~(WL1271_INTR_MASK));
}

static int wl18xx_boot(struct wl1271 *wl)
{
	int ret;

	ret = wl18xx_pre_boot(wl);
	if (ret < 0)
		goto out;

	ret = wlcore_boot_upload_nvs(wl);
	if (ret < 0)
		goto out;

	wl18xx_pre_upload(wl);

	ret = wlcore_boot_upload_firmware(wl);
	if (ret < 0)
		goto out;

	wl18xx_set_mac_and_phy(wl);

	ret = wlcore_boot_run_firmware(wl);
	if (ret < 0)
		goto out;

	wl18xx_enable_interrupts(wl);

out:
	return ret;
}

static void wl18xx_trigger_cmd(struct wl1271 *wl, int cmd_box_addr,
			       void *buf, size_t len)
{
	struct wl18xx_priv *priv = wl->priv;

	memcpy(priv->cmd_buf, buf, len);
	memset(priv->cmd_buf + len, 0, WL18XX_CMD_MAX_SIZE - len);

	wl1271_write(wl, cmd_box_addr, priv->cmd_buf, WL18XX_CMD_MAX_SIZE,
		     false);
}

static void wl18xx_ack_event(struct wl1271 *wl)
{
	wlcore_write_reg(wl, REG_INTERRUPT_TRIG, WL18XX_INTR_TRIG_EVENT_ACK);
}

static u32 wl18xx_calc_tx_blocks(struct wl1271 *wl, u32 len, u32 spare_blks)
{
	u32 blk_size = WL18XX_TX_HW_BLOCK_SIZE;
	return (len + blk_size - 1) / blk_size + spare_blks;
}

static void
wl18xx_set_tx_desc_blocks(struct wl1271 *wl, struct wl1271_tx_hw_descr *desc,
			  u32 blks, u32 spare_blks)
{
	desc->wl18xx_mem.total_mem_blocks = blks;
	desc->wl18xx_mem.reserved = 0;
}

static void
wl18xx_set_tx_desc_data_len(struct wl1271 *wl, struct wl1271_tx_hw_descr *desc,
			    struct sk_buff *skb)
{
	desc->length = cpu_to_le16(skb->len);

	wl1271_debug(DEBUG_TX, "tx_fill_hdr: hlid: %d "
		     "len: %d life: %d mem: %d", desc->hlid,
		     le16_to_cpu(desc->length),
		     le16_to_cpu(desc->life_time),
		     desc->wl18xx_mem.total_mem_blocks);
}

static struct wlcore_ops wl18xx_ops = {
	.identify_chip	= wl18xx_identify_chip,
	.boot		= wl18xx_boot,
	.trigger_cmd	= wl18xx_trigger_cmd,
	.ack_event	= wl18xx_ack_event,
	.calc_tx_blocks = wl18xx_calc_tx_blocks,
	.set_tx_desc_blocks = wl18xx_set_tx_desc_blocks,
	.set_tx_desc_data_len = wl18xx_set_tx_desc_data_len,
};

int __devinit wl18xx_probe(struct platform_device *pdev)
{
	struct wl1271 *wl;
	struct ieee80211_hw *hw;
	struct wl18xx_priv *priv;

	hw = wlcore_alloc_hw(sizeof(*priv));
	if (IS_ERR(hw)) {
		wl1271_error("can't allocate hw");
		return PTR_ERR(hw);
	}

	wl = hw->priv;
	wl->ops = &wl18xx_ops;
	wl->ptable = wl18xx_ptable;
	wl->rtable = wl18xx_rtable;
	wl->num_tx_desc = 32;
	wl->normal_tx_spare = WL18XX_TX_HW_BLOCK_SPARE;
	wl->gem_tx_spare = WL18XX_TX_HW_GEM_BLOCK_SPARE;

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
