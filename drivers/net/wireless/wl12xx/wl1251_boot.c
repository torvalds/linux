/*
 * This file is part of wl1251
 *
 * Copyright (C) 2008 Nokia Corporation
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

#include <linux/gpio.h>
#include <linux/slab.h>

#include "wl1251_reg.h"
#include "wl1251_boot.h"
#include "wl1251_io.h"
#include "wl1251_spi.h"
#include "wl1251_event.h"
#include "wl1251_acx.h"

void wl1251_boot_target_enable_interrupts(struct wl1251 *wl)
{
	wl1251_reg_write32(wl, ACX_REG_INTERRUPT_MASK, ~(wl->intr_mask));
	wl1251_reg_write32(wl, HI_CFG, HI_CFG_DEF_VAL);
}

int wl1251_boot_soft_reset(struct wl1251 *wl)
{
	unsigned long timeout;
	u32 boot_data;

	/* perform soft reset */
	wl1251_reg_write32(wl, ACX_REG_SLV_SOFT_RESET, ACX_SLV_SOFT_RESET_BIT);

	/* SOFT_RESET is self clearing */
	timeout = jiffies + usecs_to_jiffies(SOFT_RESET_MAX_TIME);
	while (1) {
		boot_data = wl1251_reg_read32(wl, ACX_REG_SLV_SOFT_RESET);
		wl1251_debug(DEBUG_BOOT, "soft reset bootdata 0x%x", boot_data);
		if ((boot_data & ACX_SLV_SOFT_RESET_BIT) == 0)
			break;

		if (time_after(jiffies, timeout)) {
			/* 1.2 check pWhalBus->uSelfClearTime if the
			 * timeout was reached */
			wl1251_error("soft reset timeout");
			return -1;
		}

		udelay(SOFT_RESET_STALL_TIME);
	}

	/* disable Rx/Tx */
	wl1251_reg_write32(wl, ENABLE, 0x0);

	/* disable auto calibration on start*/
	wl1251_reg_write32(wl, SPARE_A2, 0xffff);

	return 0;
}

int wl1251_boot_init_seq(struct wl1251 *wl)
{
	u32 scr_pad6, init_data, tmp, elp_cmd, ref_freq;

	/*
	 * col #1: INTEGER_DIVIDER
	 * col #2: FRACTIONAL_DIVIDER
	 * col #3: ATTN_BB
	 * col #4: ALPHA_BB
	 * col #5: STOP_TIME_BB
	 * col #6: BB_PLL_LOOP_FILTER
	 */
	static const u32 LUT[REF_FREQ_NUM][LUT_PARAM_NUM] = {

		{   83, 87381,  0xB, 5, 0xF00,  3}, /* REF_FREQ_19_2*/
		{   61, 141154, 0xB, 5, 0x1450, 2}, /* REF_FREQ_26_0*/
		{   41, 174763, 0xC, 6, 0x2D00, 1}, /* REF_FREQ_38_4*/
		{   40, 0,      0xC, 6, 0x2EE0, 1}, /* REF_FREQ_40_0*/
		{   47, 162280, 0xC, 6, 0x2760, 1}  /* REF_FREQ_33_6        */
	};

	/* read NVS params */
	scr_pad6 = wl1251_reg_read32(wl, SCR_PAD6);
	wl1251_debug(DEBUG_BOOT, "scr_pad6 0x%x", scr_pad6);

	/* read ELP_CMD */
	elp_cmd = wl1251_reg_read32(wl, ELP_CMD);
	wl1251_debug(DEBUG_BOOT, "elp_cmd 0x%x", elp_cmd);

	/* set the BB calibration time to be 300 usec (PLL_CAL_TIME) */
	ref_freq = scr_pad6 & 0x000000FF;
	wl1251_debug(DEBUG_BOOT, "ref_freq 0x%x", ref_freq);

	wl1251_reg_write32(wl, PLL_CAL_TIME, 0x9);

	/*
	 * PG 1.2: set the clock buffer time to be 210 usec (CLK_BUF_TIME)
	 */
	wl1251_reg_write32(wl, CLK_BUF_TIME, 0x6);

	/*
	 * set the clock detect feature to work in the restart wu procedure
	 * (ELP_CFG_MODE[14]) and Select the clock source type
	 * (ELP_CFG_MODE[13:12])
	 */
	tmp = ((scr_pad6 & 0x0000FF00) << 4) | 0x00004000;
	wl1251_reg_write32(wl, ELP_CFG_MODE, tmp);

	/* PG 1.2: enable the BB PLL fix. Enable the PLL_LIMP_CLK_EN_CMD */
	elp_cmd |= 0x00000040;
	wl1251_reg_write32(wl, ELP_CMD, elp_cmd);

	/* PG 1.2: Set the BB PLL stable time to be 1000usec
	 * (PLL_STABLE_TIME) */
	wl1251_reg_write32(wl, CFG_PLL_SYNC_CNT, 0x20);

	/* PG 1.2: read clock request time */
	init_data = wl1251_reg_read32(wl, CLK_REQ_TIME);

	/*
	 * PG 1.2: set the clock request time to be ref_clk_settling_time -
	 * 1ms = 4ms
	 */
	if (init_data > 0x21)
		tmp = init_data - 0x21;
	else
		tmp = 0;
	wl1251_reg_write32(wl, CLK_REQ_TIME, tmp);

	/* set BB PLL configurations in RF AFE */
	wl1251_reg_write32(wl, 0x003058cc, 0x4B5);

	/* set RF_AFE_REG_5 */
	wl1251_reg_write32(wl, 0x003058d4, 0x50);

	/* set RF_AFE_CTRL_REG_2 */
	wl1251_reg_write32(wl, 0x00305948, 0x11c001);

	/*
	 * change RF PLL and BB PLL divider for VCO clock and adjust VCO
	 * bais current(RF_AFE_REG_13)
	 */
	wl1251_reg_write32(wl, 0x003058f4, 0x1e);

	/* set BB PLL configurations */
	tmp = LUT[ref_freq][LUT_PARAM_INTEGER_DIVIDER] | 0x00017000;
	wl1251_reg_write32(wl, 0x00305840, tmp);

	/* set fractional divider according to Appendix C-BB PLL
	 * Calculations
	 */
	tmp = LUT[ref_freq][LUT_PARAM_FRACTIONAL_DIVIDER];
	wl1251_reg_write32(wl, 0x00305844, tmp);

	/* set the initial data for the sigma delta */
	wl1251_reg_write32(wl, 0x00305848, 0x3039);

	/*
	 * set the accumulator attenuation value, calibration loop1
	 * (alpha), calibration loop2 (beta), calibration loop3 (gamma) and
	 * the VCO gain
	 */
	tmp = (LUT[ref_freq][LUT_PARAM_ATTN_BB] << 16) |
		(LUT[ref_freq][LUT_PARAM_ALPHA_BB] << 12) | 0x1;
	wl1251_reg_write32(wl, 0x00305854, tmp);

	/*
	 * set the calibration stop time after holdoff time expires and set
	 * settling time HOLD_OFF_TIME_BB
	 */
	tmp = LUT[ref_freq][LUT_PARAM_STOP_TIME_BB] | 0x000A0000;
	wl1251_reg_write32(wl, 0x00305858, tmp);

	/*
	 * set BB PLL Loop filter capacitor3- BB_C3[2:0] and set BB PLL
	 * constant leakage current to linearize PFD to 0uA -
	 * BB_ILOOPF[7:3]
	 */
	tmp = LUT[ref_freq][LUT_PARAM_BB_PLL_LOOP_FILTER] | 0x00000030;
	wl1251_reg_write32(wl, 0x003058f8, tmp);

	/*
	 * set regulator output voltage for n divider to
	 * 1.35-BB_REFDIV[1:0], set charge pump current- BB_CPGAIN[4:2],
	 * set BB PLL Loop filter capacitor2- BB_C2[7:5], set gain of BB
	 * PLL auto-call to normal mode- BB_CALGAIN_3DB[8]
	 */
	wl1251_reg_write32(wl, 0x003058f0, 0x29);

	/* enable restart wakeup sequence (ELP_CMD[0]) */
	wl1251_reg_write32(wl, ELP_CMD, elp_cmd | 0x1);

	/* restart sequence completed */
	udelay(2000);

	return 0;
}

static void wl1251_boot_set_ecpu_ctrl(struct wl1251 *wl, u32 flag)
{
	u32 cpu_ctrl;

	/* 10.5.0 run the firmware (I) */
	cpu_ctrl = wl1251_reg_read32(wl, ACX_REG_ECPU_CONTROL);

	/* 10.5.1 run the firmware (II) */
	cpu_ctrl &= ~flag;
	wl1251_reg_write32(wl, ACX_REG_ECPU_CONTROL, cpu_ctrl);
}

int wl1251_boot_run_firmware(struct wl1251 *wl)
{
	int loop, ret;
	u32 chip_id, interrupt;

	wl1251_boot_set_ecpu_ctrl(wl, ECPU_CONTROL_HALT);

	chip_id = wl1251_reg_read32(wl, CHIP_ID_B);

	wl1251_debug(DEBUG_BOOT, "chip id after firmware boot: 0x%x", chip_id);

	if (chip_id != wl->chip_id) {
		wl1251_error("chip id doesn't match after firmware boot");
		return -EIO;
	}

	/* wait for init to complete */
	loop = 0;
	while (loop++ < INIT_LOOP) {
		udelay(INIT_LOOP_DELAY);
		interrupt = wl1251_reg_read32(wl, ACX_REG_INTERRUPT_NO_CLEAR);

		if (interrupt == 0xffffffff) {
			wl1251_error("error reading hardware complete "
				     "init indication");
			return -EIO;
		}
		/* check that ACX_INTR_INIT_COMPLETE is enabled */
		else if (interrupt & WL1251_ACX_INTR_INIT_COMPLETE) {
			wl1251_reg_write32(wl, ACX_REG_INTERRUPT_ACK,
					   WL1251_ACX_INTR_INIT_COMPLETE);
			break;
		}
	}

	if (loop > INIT_LOOP) {
		wl1251_error("timeout waiting for the hardware to "
			     "complete initialization");
		return -EIO;
	}

	/* get hardware config command mail box */
	wl->cmd_box_addr = wl1251_reg_read32(wl, REG_COMMAND_MAILBOX_PTR);

	/* get hardware config event mail box */
	wl->event_box_addr = wl1251_reg_read32(wl, REG_EVENT_MAILBOX_PTR);

	/* set the working partition to its "running" mode offset */
	wl1251_set_partition(wl, WL1251_PART_WORK_MEM_START,
			     WL1251_PART_WORK_MEM_SIZE,
			     WL1251_PART_WORK_REG_START,
			     WL1251_PART_WORK_REG_SIZE);

	wl1251_debug(DEBUG_MAILBOX, "cmd_box_addr 0x%x event_box_addr 0x%x",
		     wl->cmd_box_addr, wl->event_box_addr);

	wl1251_acx_fw_version(wl, wl->fw_ver, sizeof(wl->fw_ver));

	/*
	 * in case of full asynchronous mode the firmware event must be
	 * ready to receive event from the command mailbox
	 */

	/* enable gpio interrupts */
	wl1251_enable_interrupts(wl);

	/* Enable target's interrupts */
	wl->intr_mask = WL1251_ACX_INTR_RX0_DATA |
		WL1251_ACX_INTR_RX1_DATA |
		WL1251_ACX_INTR_TX_RESULT |
		WL1251_ACX_INTR_EVENT_A |
		WL1251_ACX_INTR_EVENT_B |
		WL1251_ACX_INTR_INIT_COMPLETE;
	wl1251_boot_target_enable_interrupts(wl);

	wl->event_mask = SCAN_COMPLETE_EVENT_ID | BSS_LOSE_EVENT_ID |
		SYNCHRONIZATION_TIMEOUT_EVENT_ID |
		ROAMING_TRIGGER_LOW_RSSI_EVENT_ID |
		ROAMING_TRIGGER_REGAINED_RSSI_EVENT_ID |
		REGAINED_BSS_EVENT_ID | BT_PTA_SENSE_EVENT_ID |
		BT_PTA_PREDICTION_EVENT_ID;

	ret = wl1251_event_unmask(wl);
	if (ret < 0) {
		wl1251_error("EVENT mask setting failed");
		return ret;
	}

	wl1251_event_mbox_config(wl);

	/* firmware startup completed */
	return 0;
}

static int wl1251_boot_upload_firmware(struct wl1251 *wl)
{
	int addr, chunk_num, partition_limit;
	size_t fw_data_len, len;
	u8 *p, *buf;

	/* whal_FwCtrl_LoadFwImageSm() */

	wl1251_debug(DEBUG_BOOT, "chip id before fw upload: 0x%x",
		     wl1251_reg_read32(wl, CHIP_ID_B));

	/* 10.0 check firmware length and set partition */
	fw_data_len =  (wl->fw[4] << 24) | (wl->fw[5] << 16) |
		(wl->fw[6] << 8) | (wl->fw[7]);

	wl1251_debug(DEBUG_BOOT, "fw_data_len %zu chunk_size %d", fw_data_len,
		CHUNK_SIZE);

	if ((fw_data_len % 4) != 0) {
		wl1251_error("firmware length not multiple of four");
		return -EIO;
	}

	buf = kmalloc(CHUNK_SIZE, GFP_KERNEL);
	if (!buf) {
		wl1251_error("allocation for firmware upload chunk failed");
		return -ENOMEM;
	}

	wl1251_set_partition(wl, WL1251_PART_DOWN_MEM_START,
			     WL1251_PART_DOWN_MEM_SIZE,
			     WL1251_PART_DOWN_REG_START,
			     WL1251_PART_DOWN_REG_SIZE);

	/* 10.1 set partition limit and chunk num */
	chunk_num = 0;
	partition_limit = WL1251_PART_DOWN_MEM_SIZE;

	while (chunk_num < fw_data_len / CHUNK_SIZE) {
		/* 10.2 update partition, if needed */
		addr = WL1251_PART_DOWN_MEM_START +
			(chunk_num + 2) * CHUNK_SIZE;
		if (addr > partition_limit) {
			addr = WL1251_PART_DOWN_MEM_START +
				chunk_num * CHUNK_SIZE;
			partition_limit = chunk_num * CHUNK_SIZE +
				WL1251_PART_DOWN_MEM_SIZE;
			wl1251_set_partition(wl,
					     addr,
					     WL1251_PART_DOWN_MEM_SIZE,
					     WL1251_PART_DOWN_REG_START,
					     WL1251_PART_DOWN_REG_SIZE);
		}

		/* 10.3 upload the chunk */
		addr = WL1251_PART_DOWN_MEM_START + chunk_num * CHUNK_SIZE;
		p = wl->fw + FW_HDR_SIZE + chunk_num * CHUNK_SIZE;
		wl1251_debug(DEBUG_BOOT, "uploading fw chunk 0x%p to 0x%x",
			     p, addr);

		/* need to copy the chunk for dma */
		len = CHUNK_SIZE;
		memcpy(buf, p, len);
		wl1251_mem_write(wl, addr, buf, len);

		chunk_num++;
	}

	/* 10.4 upload the last chunk */
	addr = WL1251_PART_DOWN_MEM_START + chunk_num * CHUNK_SIZE;
	p = wl->fw + FW_HDR_SIZE + chunk_num * CHUNK_SIZE;

	/* need to copy the chunk for dma */
	len = fw_data_len % CHUNK_SIZE;
	memcpy(buf, p, len);

	wl1251_debug(DEBUG_BOOT, "uploading fw last chunk (%zu B) 0x%p to 0x%x",
		     len, p, addr);
	wl1251_mem_write(wl, addr, buf, len);

	kfree(buf);

	return 0;
}

static int wl1251_boot_upload_nvs(struct wl1251 *wl)
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

			wl1251_debug(DEBUG_BOOT,
				     "nvs burst write 0x%x: 0x%x",
				     dest_addr, val);
			wl1251_mem_write32(wl, dest_addr, val);

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
	wl1251_set_partition(wl, nvs_start,
			     WL1251_PART_DOWN_MEM_SIZE,
			     WL1251_PART_DOWN_REG_START,
			     WL1251_PART_DOWN_REG_SIZE);

	/* And finally we upload the NVS tables */
	nvs_bytes_written = 0;
	while (nvs_bytes_written < nvs_len) {
		val = (nvs_ptr[0] | (nvs_ptr[1] << 8)
		       | (nvs_ptr[2] << 16) | (nvs_ptr[3] << 24));

		val = cpu_to_le32(val);

		wl1251_debug(DEBUG_BOOT,
			     "nvs write table 0x%x: 0x%x",
			     nvs_start, val);
		wl1251_mem_write32(wl, nvs_start, val);

		nvs_ptr += 4;
		nvs_bytes_written += 4;
		nvs_start += 4;
	}

	return 0;
}

int wl1251_boot(struct wl1251 *wl)
{
	int ret = 0, minor_minor_e2_ver;
	u32 tmp, boot_data;

	/* halt embedded ARM CPU while loading firmware */
	wl1251_reg_write32(wl, ACX_REG_ECPU_CONTROL, ECPU_CONTROL_HALT);

	ret = wl1251_boot_soft_reset(wl);
	if (ret < 0)
		goto out;

	/* 2. start processing NVS file */
	if (wl->use_eeprom) {
		wl1251_reg_write32(wl, ACX_REG_EE_START, START_EEPROM_MGR);
		/* Wait for EEPROM NVS burst read to complete */
		msleep(40);
		wl1251_reg_write32(wl, ACX_EEPROMLESS_IND_REG, USE_EEPROM);
	} else {
		ret = wl1251_boot_upload_nvs(wl);
		if (ret < 0)
			goto out;

		/* write firmware's last address (ie. it's length) to
		 * ACX_EEPROMLESS_IND_REG */
		wl1251_reg_write32(wl, ACX_EEPROMLESS_IND_REG, wl->fw_len);
	}

	/* 6. read the EEPROM parameters */
	tmp = wl1251_reg_read32(wl, SCR_PAD2);

	/* 7. read bootdata */
	wl->boot_attr.radio_type = (tmp & 0x0000FF00) >> 8;
	wl->boot_attr.major = (tmp & 0x00FF0000) >> 16;
	tmp = wl1251_reg_read32(wl, SCR_PAD3);

	/* 8. check bootdata and call restart sequence */
	wl->boot_attr.minor = (tmp & 0x00FF0000) >> 16;
	minor_minor_e2_ver = (tmp & 0xFF000000) >> 24;

	wl1251_debug(DEBUG_BOOT, "radioType 0x%x majorE2Ver 0x%x "
		     "minorE2Ver 0x%x minor_minor_e2_ver 0x%x",
		     wl->boot_attr.radio_type, wl->boot_attr.major,
		     wl->boot_attr.minor, minor_minor_e2_ver);

	ret = wl1251_boot_init_seq(wl);
	if (ret < 0)
		goto out;

	/* 9. NVS processing done */
	boot_data = wl1251_reg_read32(wl, ACX_REG_ECPU_CONTROL);

	wl1251_debug(DEBUG_BOOT, "halt boot_data 0x%x", boot_data);

	/* 10. check that ECPU_CONTROL_HALT bits are set in
	 * pWhalBus->uBootData and start uploading firmware
	 */
	if ((boot_data & ECPU_CONTROL_HALT) == 0) {
		wl1251_error("boot failed, ECPU_CONTROL_HALT not set");
		ret = -EIO;
		goto out;
	}

	ret = wl1251_boot_upload_firmware(wl);
	if (ret < 0)
		goto out;

	/* 10.5 start firmware */
	ret = wl1251_boot_run_firmware(wl);
	if (ret < 0)
		goto out;

out:
	return ret;
}
