/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Kalle Valo <kalle.valo@nokia.com>
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

#include <linux/kernel.h>
#include <linux/module.h>

#include "wl1251.h"
#include "reg.h"
#include "spi.h"
#include "boot.h"
#include "event.h"
#include "acx.h"
#include "tx.h"
#include "rx.h"
#include "ps.h"
#include "init.h"

static struct wl12xx_partition_set wl1251_part_table[PART_TABLE_LEN] = {
	[PART_DOWN] = {
		.mem = {
			.start = 0x00000000,
			.size  = 0x00016800
		},
		.reg = {
			.start = REGISTERS_BASE,
			.size  = REGISTERS_DOWN_SIZE
		},
	},

	[PART_WORK] = {
		.mem = {
			.start = 0x00028000,
			.size  = 0x00014000
		},
		.reg = {
			.start = REGISTERS_BASE,
			.size  = REGISTERS_WORK_SIZE
		},
	},

	/* WL1251 doesn't use the DRPW partition, so we don't set it here */
};

static enum wl12xx_acx_int_reg wl1251_acx_reg_table[ACX_REG_TABLE_LEN] = {
	[ACX_REG_INTERRUPT_TRIG]     = (REGISTERS_BASE + 0x0474),
	[ACX_REG_INTERRUPT_TRIG_H]   = (REGISTERS_BASE + 0x0478),
	[ACX_REG_INTERRUPT_MASK]     = (REGISTERS_BASE + 0x0494),
	[ACX_REG_HINT_MASK_SET]      = (REGISTERS_BASE + 0x0498),
	[ACX_REG_HINT_MASK_CLR]      = (REGISTERS_BASE + 0x049C),
	[ACX_REG_INTERRUPT_NO_CLEAR] = (REGISTERS_BASE + 0x04B0),
	[ACX_REG_INTERRUPT_CLEAR]    = (REGISTERS_BASE + 0x04A4),
	[ACX_REG_INTERRUPT_ACK]      = (REGISTERS_BASE + 0x04A8),
	[ACX_REG_SLV_SOFT_RESET]     = (REGISTERS_BASE + 0x0000),
	[ACX_REG_EE_START]           = (REGISTERS_BASE + 0x080C),
	[ACX_REG_ECPU_CONTROL]       = (REGISTERS_BASE + 0x0804)
};

static int wl1251_upload_firmware(struct wl12xx *wl)
{
	struct wl12xx_partition_set *p_table = wl->chip.p_table;
	int addr, chunk_num, partition_limit;
	size_t fw_data_len;
	u8 *p;

	/* whal_FwCtrl_LoadFwImageSm() */

	wl12xx_debug(DEBUG_BOOT, "chip id before fw upload: 0x%x",
		     wl12xx_reg_read32(wl, CHIP_ID_B));

	/* 10.0 check firmware length and set partition */
	fw_data_len =  (wl->fw[4] << 24) | (wl->fw[5] << 16) |
		(wl->fw[6] << 8) | (wl->fw[7]);

	wl12xx_debug(DEBUG_BOOT, "fw_data_len %zu chunk_size %d", fw_data_len,
		CHUNK_SIZE);

	if ((fw_data_len % 4) != 0) {
		wl12xx_error("firmware length not multiple of four");
		return -EIO;
	}

	wl12xx_set_partition(wl,
			     p_table[PART_DOWN].mem.start,
			     p_table[PART_DOWN].mem.size,
			     p_table[PART_DOWN].reg.start,
			     p_table[PART_DOWN].reg.size);

	/* 10.1 set partition limit and chunk num */
	chunk_num = 0;
	partition_limit = p_table[PART_DOWN].mem.size;

	while (chunk_num < fw_data_len / CHUNK_SIZE) {
		/* 10.2 update partition, if needed */
		addr = p_table[PART_DOWN].mem.start +
			(chunk_num + 2) * CHUNK_SIZE;
		if (addr > partition_limit) {
			addr = p_table[PART_DOWN].mem.start +
				chunk_num * CHUNK_SIZE;
			partition_limit = chunk_num * CHUNK_SIZE +
				p_table[PART_DOWN].mem.size;
			wl12xx_set_partition(wl,
					     addr,
					     p_table[PART_DOWN].mem.size,
					     p_table[PART_DOWN].reg.start,
					     p_table[PART_DOWN].reg.size);
		}

		/* 10.3 upload the chunk */
		addr = p_table[PART_DOWN].mem.start + chunk_num * CHUNK_SIZE;
		p = wl->fw + FW_HDR_SIZE + chunk_num * CHUNK_SIZE;
		wl12xx_debug(DEBUG_BOOT, "uploading fw chunk 0x%p to 0x%x",
			     p, addr);
		wl12xx_spi_mem_write(wl, addr, p, CHUNK_SIZE);

		chunk_num++;
	}

	/* 10.4 upload the last chunk */
	addr = p_table[PART_DOWN].mem.start + chunk_num * CHUNK_SIZE;
	p = wl->fw + FW_HDR_SIZE + chunk_num * CHUNK_SIZE;
	wl12xx_debug(DEBUG_BOOT, "uploading fw last chunk (%zu B) 0x%p to 0x%x",
		     fw_data_len % CHUNK_SIZE, p, addr);
	wl12xx_spi_mem_write(wl, addr, p, fw_data_len % CHUNK_SIZE);

	return 0;
}

static int wl1251_upload_nvs(struct wl12xx *wl)
{
	size_t nvs_len, nvs_bytes_written, burst_len;
	int nvs_start, i;
	u32 dest_addr, val;
	u8 *nvs_ptr, *nvs;

	nvs = wl->nvs;
	if (nvs == NULL)
		return -ENODEV;

	nvs_ptr = nvs;

	nvs_len = wl->nvs_len;
	nvs_start = wl->fw_len;

	/*
	 * Layout before the actual NVS tables:
	 * 1 byte : burst length.
	 * 2 bytes: destination address.
	 * n bytes: data to burst copy.
	 *
	 * This is ended by a 0 length, then the NVS tables.
	 */

	while (nvs_ptr[0]) {
		burst_len = nvs_ptr[0];
		dest_addr = (nvs_ptr[1] & 0xfe) | ((u32)(nvs_ptr[2] << 8));

		/* We move our pointer to the data */
		nvs_ptr += 3;

		for (i = 0; i < burst_len; i++) {
			val = (nvs_ptr[0] | (nvs_ptr[1] << 8)
			       | (nvs_ptr[2] << 16) | (nvs_ptr[3] << 24));

			wl12xx_debug(DEBUG_BOOT,
				     "nvs burst write 0x%x: 0x%x",
				     dest_addr, val);
			wl12xx_mem_write32(wl, dest_addr, val);

			nvs_ptr += 4;
			dest_addr += 4;
		}
	}

	/*
	 * We've reached the first zero length, the first NVS table
	 * is 7 bytes further.
	 */
	nvs_ptr += 7;
	nvs_len -= nvs_ptr - nvs;
	nvs_len = ALIGN(nvs_len, 4);

	/* Now we must set the partition correctly */
	wl12xx_set_partition(wl, nvs_start,
			     wl->chip.p_table[PART_DOWN].mem.size,
			     wl->chip.p_table[PART_DOWN].reg.start,
			     wl->chip.p_table[PART_DOWN].reg.size);

	/* And finally we upload the NVS tables */
	nvs_bytes_written = 0;
	while (nvs_bytes_written < nvs_len) {
		val = (nvs_ptr[0] | (nvs_ptr[1] << 8)
		       | (nvs_ptr[2] << 16) | (nvs_ptr[3] << 24));

		val = cpu_to_le32(val);

		wl12xx_debug(DEBUG_BOOT,
			     "nvs write table 0x%x: 0x%x",
			     nvs_start, val);
		wl12xx_mem_write32(wl, nvs_start, val);

		nvs_ptr += 4;
		nvs_bytes_written += 4;
		nvs_start += 4;
	}

	return 0;
}

static int wl1251_boot(struct wl12xx *wl)
{
	int ret = 0, minor_minor_e2_ver;
	u32 tmp, boot_data;

	ret = wl12xx_boot_soft_reset(wl);
	if (ret < 0)
		goto out;

	/* 2. start processing NVS file */
	ret = wl->chip.op_upload_nvs(wl);
	if (ret < 0)
		goto out;

	/* write firmware's last address (ie. it's length) to
	 * ACX_EEPROMLESS_IND_REG */
	wl12xx_reg_write32(wl, ACX_EEPROMLESS_IND_REG, wl->fw_len);

	/* 6. read the EEPROM parameters */
	tmp = wl12xx_reg_read32(wl, SCR_PAD2);

	/* 7. read bootdata */
	wl->boot_attr.radio_type = (tmp & 0x0000FF00) >> 8;
	wl->boot_attr.major = (tmp & 0x00FF0000) >> 16;
	tmp = wl12xx_reg_read32(wl, SCR_PAD3);

	/* 8. check bootdata and call restart sequence */
	wl->boot_attr.minor = (tmp & 0x00FF0000) >> 16;
	minor_minor_e2_ver = (tmp & 0xFF000000) >> 24;

	wl12xx_debug(DEBUG_BOOT, "radioType 0x%x majorE2Ver 0x%x "
		     "minorE2Ver 0x%x minor_minor_e2_ver 0x%x",
		     wl->boot_attr.radio_type, wl->boot_attr.major,
		     wl->boot_attr.minor, minor_minor_e2_ver);

	ret = wl12xx_boot_init_seq(wl);
	if (ret < 0)
		goto out;

	/* 9. NVS processing done */
	boot_data = wl12xx_reg_read32(wl, ACX_REG_ECPU_CONTROL);

	wl12xx_debug(DEBUG_BOOT, "halt boot_data 0x%x", boot_data);

	/* 10. check that ECPU_CONTROL_HALT bits are set in
	 * pWhalBus->uBootData and start uploading firmware
	 */
	if ((boot_data & ECPU_CONTROL_HALT) == 0) {
		wl12xx_error("boot failed, ECPU_CONTROL_HALT not set");
		ret = -EIO;
		goto out;
	}

	ret = wl->chip.op_upload_fw(wl);
	if (ret < 0)
		goto out;

	/* 10.5 start firmware */
	ret = wl12xx_boot_run_firmware(wl);
	if (ret < 0)
		goto out;

	/* Get and save the firmware version */
	wl12xx_acx_fw_version(wl, wl->chip.fw_ver, sizeof(wl->chip.fw_ver));

out:
	return ret;
}

static int wl1251_mem_cfg(struct wl12xx *wl)
{
	struct wl1251_acx_config_memory mem_conf;
	int ret, i;

	wl12xx_debug(DEBUG_ACX, "wl1251 mem cfg");

	/* memory config */
	mem_conf.mem_config.num_stations = cpu_to_le16(DEFAULT_NUM_STATIONS);
	mem_conf.mem_config.rx_mem_block_num = 35;
	mem_conf.mem_config.tx_min_mem_block_num = 64;
	mem_conf.mem_config.num_tx_queues = MAX_TX_QUEUES;
	mem_conf.mem_config.host_if_options = HOSTIF_PKT_RING;
	mem_conf.mem_config.num_ssid_profiles = 1;
	mem_conf.mem_config.debug_buffer_size =
		cpu_to_le16(TRACE_BUFFER_MAX_SIZE);

	/* RX queue config */
	mem_conf.rx_queue_config.dma_address = 0;
	mem_conf.rx_queue_config.num_descs = ACX_RX_DESC_DEF;
	mem_conf.rx_queue_config.priority = DEFAULT_RXQ_PRIORITY;
	mem_conf.rx_queue_config.type = DEFAULT_RXQ_TYPE;

	/* TX queue config */
	for (i = 0; i < MAX_TX_QUEUES; i++) {
		mem_conf.tx_queue_config[i].num_descs = ACX_TX_DESC_DEF;
		mem_conf.tx_queue_config[i].attributes = i;
	}

	mem_conf.header.id = ACX_MEM_CFG;
	mem_conf.header.len = sizeof(struct wl1251_acx_config_memory) -
		sizeof(struct acx_header);
	mem_conf.header.len -=
		(MAX_TX_QUEUE_CONFIGS - mem_conf.mem_config.num_tx_queues) *
		sizeof(struct wl1251_acx_tx_queue_config);

	ret = wl12xx_cmd_configure(wl, &mem_conf,
				   sizeof(struct wl1251_acx_config_memory));
	if (ret < 0)
		wl12xx_warning("wl1251 mem config failed: %d", ret);

	return ret;
}

static int wl1251_hw_init_mem_config(struct wl12xx *wl)
{
	int ret;

	ret = wl1251_mem_cfg(wl);
	if (ret < 0)
		return ret;

	wl->target_mem_map = kzalloc(sizeof(struct wl1251_acx_mem_map),
					  GFP_KERNEL);
	if (!wl->target_mem_map) {
		wl12xx_error("couldn't allocate target memory map");
		return -ENOMEM;
	}

	/* we now ask for the firmware built memory map */
	ret = wl12xx_acx_mem_map(wl, wl->target_mem_map,
				 sizeof(struct wl1251_acx_mem_map));
	if (ret < 0) {
		wl12xx_error("couldn't retrieve firmware memory map");
		kfree(wl->target_mem_map);
		wl->target_mem_map = NULL;
		return ret;
	}

	return 0;
}

static void wl1251_set_ecpu_ctrl(struct wl12xx *wl, u32 flag)
{
	u32 cpu_ctrl;

	/* 10.5.0 run the firmware (I) */
	cpu_ctrl = wl12xx_reg_read32(wl, ACX_REG_ECPU_CONTROL);

	/* 10.5.1 run the firmware (II) */
	cpu_ctrl &= ~flag;
	wl12xx_reg_write32(wl, ACX_REG_ECPU_CONTROL, cpu_ctrl);
}

static void wl1251_target_enable_interrupts(struct wl12xx *wl)
{
	/* Enable target's interrupts */
	wl->intr_mask = WL1251_ACX_INTR_RX0_DATA |
		WL1251_ACX_INTR_RX1_DATA |
		WL1251_ACX_INTR_TX_RESULT |
		WL1251_ACX_INTR_EVENT_A |
		WL1251_ACX_INTR_EVENT_B |
		WL1251_ACX_INTR_INIT_COMPLETE;
	wl12xx_boot_target_enable_interrupts(wl);
}

static void wl1251_irq_work(struct work_struct *work)
{
	u32 intr;
	struct wl12xx *wl =
		container_of(work, struct wl12xx, irq_work);

	mutex_lock(&wl->mutex);

	wl12xx_debug(DEBUG_IRQ, "IRQ work");

	if (wl->state == WL12XX_STATE_OFF)
		goto out;

	wl12xx_ps_elp_wakeup(wl);

	wl12xx_reg_write32(wl, ACX_REG_INTERRUPT_MASK, WL1251_ACX_INTR_ALL);

	intr = wl12xx_reg_read32(wl, ACX_REG_INTERRUPT_CLEAR);
	wl12xx_debug(DEBUG_IRQ, "intr: 0x%x", intr);

	if (wl->data_path) {
		wl12xx_spi_mem_read(wl, wl->data_path->rx_control_addr,
				    &wl->rx_counter, sizeof(u32));

		/* We handle a frmware bug here */
		switch ((wl->rx_counter - wl->rx_handled) & 0xf) {
		case 0:
			wl12xx_debug(DEBUG_IRQ, "RX: FW and host in sync");
			intr &= ~WL1251_ACX_INTR_RX0_DATA;
			intr &= ~WL1251_ACX_INTR_RX1_DATA;
			break;
		case 1:
			wl12xx_debug(DEBUG_IRQ, "RX: FW +1");
			intr |= WL1251_ACX_INTR_RX0_DATA;
			intr &= ~WL1251_ACX_INTR_RX1_DATA;
			break;
		case 2:
			wl12xx_debug(DEBUG_IRQ, "RX: FW +2");
			intr |= WL1251_ACX_INTR_RX0_DATA;
			intr |= WL1251_ACX_INTR_RX1_DATA;
			break;
		default:
			wl12xx_warning("RX: FW and host out of sync: %d",
				       wl->rx_counter - wl->rx_handled);
			break;
		}

		wl->rx_handled = wl->rx_counter;


		wl12xx_debug(DEBUG_IRQ, "RX counter: %d", wl->rx_counter);
	}

	intr &= wl->intr_mask;

	if (intr == 0) {
		wl12xx_debug(DEBUG_IRQ, "INTR is 0");
		wl12xx_reg_write32(wl, ACX_REG_INTERRUPT_MASK,
				   ~(wl->intr_mask));

		goto out_sleep;
	}

	if (intr & WL1251_ACX_INTR_RX0_DATA) {
		wl12xx_debug(DEBUG_IRQ, "WL1251_ACX_INTR_RX0_DATA");
		wl12xx_rx(wl);
	}

	if (intr & WL1251_ACX_INTR_RX1_DATA) {
		wl12xx_debug(DEBUG_IRQ, "WL1251_ACX_INTR_RX1_DATA");
		wl12xx_rx(wl);
	}

	if (intr & WL1251_ACX_INTR_TX_RESULT) {
		wl12xx_debug(DEBUG_IRQ, "WL1251_ACX_INTR_TX_RESULT");
		wl12xx_tx_complete(wl);
	}

	if (intr & (WL1251_ACX_INTR_EVENT_A | WL1251_ACX_INTR_EVENT_B)) {
		wl12xx_debug(DEBUG_IRQ, "WL1251_ACX_INTR_EVENT (0x%x)", intr);
		if (intr & WL1251_ACX_INTR_EVENT_A)
			wl12xx_event_handle(wl, 0);
		else
			wl12xx_event_handle(wl, 1);
	}

	if (intr & WL1251_ACX_INTR_INIT_COMPLETE)
		wl12xx_debug(DEBUG_IRQ, "WL1251_ACX_INTR_INIT_COMPLETE");

	wl12xx_reg_write32(wl, ACX_REG_INTERRUPT_MASK, ~(wl->intr_mask));

out_sleep:
	wl12xx_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);
}

static int wl1251_hw_init_txq_fill(u8 qid,
				   struct acx_tx_queue_qos_config *config,
				   u32 num_blocks)
{
	config->qid = qid;

	switch (qid) {
	case QOS_AC_BE:
		config->high_threshold =
			(QOS_TX_HIGH_BE_DEF * num_blocks) / 100;
		config->low_threshold =
			(QOS_TX_LOW_BE_DEF * num_blocks) / 100;
		break;
	case QOS_AC_BK:
		config->high_threshold =
			(QOS_TX_HIGH_BK_DEF * num_blocks) / 100;
		config->low_threshold =
			(QOS_TX_LOW_BK_DEF * num_blocks) / 100;
		break;
	case QOS_AC_VI:
		config->high_threshold =
			(QOS_TX_HIGH_VI_DEF * num_blocks) / 100;
		config->low_threshold =
			(QOS_TX_LOW_VI_DEF * num_blocks) / 100;
		break;
	case QOS_AC_VO:
		config->high_threshold =
			(QOS_TX_HIGH_VO_DEF * num_blocks) / 100;
		config->low_threshold =
			(QOS_TX_LOW_VO_DEF * num_blocks) / 100;
		break;
	default:
		wl12xx_error("Invalid TX queue id: %d", qid);
		return -EINVAL;
	}

	return 0;
}

static int wl1251_hw_init_tx_queue_config(struct wl12xx *wl)
{
	struct acx_tx_queue_qos_config config;
	struct wl1251_acx_mem_map *wl_mem_map = wl->target_mem_map;
	int ret, i;

	wl12xx_debug(DEBUG_ACX, "acx tx queue config");

	config.header.id = ACX_TX_QUEUE_CFG;
	config.header.len = sizeof(struct acx_tx_queue_qos_config) -
		sizeof(struct acx_header);

	for (i = 0; i < MAX_NUM_OF_AC; i++) {
		ret = wl1251_hw_init_txq_fill(i, &config,
					      wl_mem_map->num_tx_mem_blocks);
		if (ret < 0)
			return ret;

		ret = wl12xx_cmd_configure(wl, &config, sizeof(config));
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int wl1251_hw_init_data_path_config(struct wl12xx *wl)
{
	int ret;

	/* asking for the data path parameters */
	wl->data_path = kzalloc(sizeof(struct acx_data_path_params_resp),
				GFP_KERNEL);
	if (!wl->data_path) {
		wl12xx_error("Couldnt allocate data path parameters");
		return -ENOMEM;
	}

	ret = wl12xx_acx_data_path_params(wl, wl->data_path);
	if (ret < 0) {
		kfree(wl->data_path);
		wl->data_path = NULL;
		return ret;
	}

	return 0;
}

static int wl1251_hw_init(struct wl12xx *wl)
{
	struct wl1251_acx_mem_map *wl_mem_map;
	int ret;

	ret = wl12xx_hw_init_hwenc_config(wl);
	if (ret < 0)
		return ret;

	/* Template settings */
	ret = wl12xx_hw_init_templates_config(wl);
	if (ret < 0)
		return ret;

	/* Default memory configuration */
	ret = wl1251_hw_init_mem_config(wl);
	if (ret < 0)
		return ret;

	/* Default data path configuration  */
	ret = wl1251_hw_init_data_path_config(wl);
	if (ret < 0)
		goto out_free_memmap;

	/* RX config */
	ret = wl12xx_hw_init_rx_config(wl,
				       RX_CFG_PROMISCUOUS | RX_CFG_TSF,
				       RX_FILTER_OPTION_DEF);
	/* RX_CONFIG_OPTION_ANY_DST_ANY_BSS,
	   RX_FILTER_OPTION_FILTER_ALL); */
	if (ret < 0)
		goto out_free_data_path;

	/* TX queues config */
	ret = wl1251_hw_init_tx_queue_config(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* PHY layer config */
	ret = wl12xx_hw_init_phy_config(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* Beacon filtering */
	ret = wl12xx_hw_init_beacon_filter(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* Bluetooth WLAN coexistence */
	ret = wl12xx_hw_init_pta(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* Energy detection */
	ret = wl12xx_hw_init_energy_detection(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* Beacons and boradcast settings */
	ret = wl12xx_hw_init_beacon_broadcast(wl);
	if (ret < 0)
		goto out_free_data_path;

	/* Enable data path */
	ret = wl12xx_cmd_data_path(wl, wl->channel, 1);
	if (ret < 0)
		goto out_free_data_path;

	/* Default power state */
	ret = wl12xx_hw_init_power_auth(wl);
	if (ret < 0)
		goto out_free_data_path;

	wl_mem_map = wl->target_mem_map;
	wl12xx_info("%d tx blocks at 0x%x, %d rx blocks at 0x%x",
		    wl_mem_map->num_tx_mem_blocks,
		    wl->data_path->tx_control_addr,
		    wl_mem_map->num_rx_mem_blocks,
		    wl->data_path->rx_control_addr);

	return 0;

 out_free_data_path:
	kfree(wl->data_path);

 out_free_memmap:
	kfree(wl->target_mem_map);

	return ret;
}

static int wl1251_plt_init(struct wl12xx *wl)
{
	int ret;

	ret = wl1251_hw_init_mem_config(wl);
	if (ret < 0)
		return ret;

	ret = wl12xx_cmd_data_path(wl, wl->channel, 1);
	if (ret < 0)
		return ret;

	return 0;
}

void wl1251_setup(struct wl12xx *wl)
{
	/* FIXME: Is it better to use strncpy here or is this ok? */
	wl->chip.fw_filename = WL1251_FW_NAME;
	wl->chip.nvs_filename = WL1251_NVS_NAME;

	/* Now we know what chip we're using, so adjust the power on sleep
	 * time accordingly */
	wl->chip.power_on_sleep = WL1251_POWER_ON_SLEEP;

	wl->chip.intr_cmd_complete = WL1251_ACX_INTR_CMD_COMPLETE;
	wl->chip.intr_init_complete = WL1251_ACX_INTR_INIT_COMPLETE;

	wl->chip.op_upload_nvs = wl1251_upload_nvs;
	wl->chip.op_upload_fw = wl1251_upload_firmware;
	wl->chip.op_boot = wl1251_boot;
	wl->chip.op_set_ecpu_ctrl = wl1251_set_ecpu_ctrl;
	wl->chip.op_target_enable_interrupts = wl1251_target_enable_interrupts;
	wl->chip.op_hw_init = wl1251_hw_init;
	wl->chip.op_plt_init = wl1251_plt_init;

	wl->chip.p_table = wl1251_part_table;
	wl->chip.acx_reg_table = wl1251_acx_reg_table;

	INIT_WORK(&wl->irq_work, wl1251_irq_work);
}
