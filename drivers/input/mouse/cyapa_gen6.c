/*
 * Cypress APA trackpad with I2C interface
 *
 * Author: Dudley Du <dudl@cypress.com>
 *
 * Copyright (C) 2015 Cypress Semiconductor, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <linux/crc-itu-t.h>
#include "cyapa.h"


#define GEN6_ENABLE_CMD_IRQ	0x41
#define GEN6_DISABLE_CMD_IRQ	0x42
#define GEN6_ENABLE_DEV_IRQ	0x43
#define GEN6_DISABLE_DEV_IRQ	0x44

#define GEN6_POWER_MODE_ACTIVE		0x01
#define GEN6_POWER_MODE_LP_MODE1	0x02
#define GEN6_POWER_MODE_LP_MODE2	0x03
#define GEN6_POWER_MODE_BTN_ONLY	0x04

#define GEN6_SET_POWER_MODE_INTERVAL	0x47
#define GEN6_GET_POWER_MODE_INTERVAL	0x48

#define GEN6_MAX_RX_NUM 14
#define GEN6_RETRIEVE_DATA_ID_RX_ATTENURATOR_IDAC	0x00
#define GEN6_RETRIEVE_DATA_ID_ATTENURATOR_TRIM		0x12


struct pip_app_cmd_head {
	__le16 addr;
	__le16 length;
	u8 report_id;
	u8 resv;  /* Reserved, must be 0 */
	u8 cmd_code;  /* bit7: resv, set to 0; bit6~0: command code.*/
} __packed;

struct pip_app_resp_head {
	__le16 length;
	u8 report_id;
	u8 resv;  /* Reserved, must be 0 */
	u8 cmd_code;  /* bit7: TGL; bit6~0: command code.*/
	/*
	 * The value of data_status can be the first byte of data or
	 * the command status or the unsupported command code depending on the
	 * requested command code.
	*/
	u8 data_status;
} __packed;

struct pip_fixed_info {
	u8 silicon_id_high;
	u8 silicon_id_low;
	u8 family_id;
};

static u8 pip_get_bl_info[] = {
	0x04, 0x00, 0x0B, 0x00, 0x40, 0x00, 0x01, 0x38,
	0x00, 0x00, 0x70, 0x9E, 0x17
};

static bool cyapa_sort_pip_hid_descriptor_data(struct cyapa *cyapa,
		u8 *buf, int len)
{
	if (len != PIP_HID_DESCRIPTOR_SIZE)
		return false;

	if (buf[PIP_RESP_REPORT_ID_OFFSET] == PIP_HID_APP_REPORT_ID ||
		buf[PIP_RESP_REPORT_ID_OFFSET] == PIP_HID_BL_REPORT_ID)
		return true;

	return false;
}

static int cyapa_get_pip_fixed_info(struct cyapa *cyapa,
		struct pip_fixed_info *pip_info, bool is_bootloader)
{
	u8 resp_data[PIP_READ_SYS_INFO_RESP_LENGTH];
	int resp_len;
	u16 product_family;
	int error;

	if (is_bootloader) {
		/* Read Bootloader Information to determine Gen5 or Gen6. */
		resp_len = sizeof(resp_data);
		error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
				pip_get_bl_info, sizeof(pip_get_bl_info),
				resp_data, &resp_len,
				2000, cyapa_sort_tsg_pip_bl_resp_data,
				false);
		if (error || resp_len < PIP_BL_GET_INFO_RESP_LENGTH)
			return error ? error : -EIO;

		pip_info->family_id = resp_data[8];
		pip_info->silicon_id_low = resp_data[10];
		pip_info->silicon_id_high = resp_data[11];

		return 0;
	}

	/* Get App System Information to determine Gen5 or Gen6. */
	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			pip_read_sys_info, PIP_READ_SYS_INFO_CMD_LENGTH,
			resp_data, &resp_len,
			2000, cyapa_pip_sort_system_info_data, false);
	if (error || resp_len < PIP_READ_SYS_INFO_RESP_LENGTH)
		return error ? error : -EIO;

	product_family = get_unaligned_le16(&resp_data[7]);
	if ((product_family & PIP_PRODUCT_FAMILY_MASK) !=
		PIP_PRODUCT_FAMILY_TRACKPAD)
		return -EINVAL;

	pip_info->family_id = resp_data[19];
	pip_info->silicon_id_low = resp_data[21];
	pip_info->silicon_id_high = resp_data[22];

	return 0;

}

int cyapa_pip_state_parse(struct cyapa *cyapa, u8 *reg_data, int len)
{
	u8 cmd[] = { 0x01, 0x00};
	struct pip_fixed_info pip_info;
	u8 resp_data[PIP_HID_DESCRIPTOR_SIZE];
	int resp_len;
	bool is_bootloader;
	int error;

	cyapa->state = CYAPA_STATE_NO_DEVICE;

	/* Try to wake from it deep sleep state if it is. */
	cyapa_pip_deep_sleep(cyapa, PIP_DEEP_SLEEP_STATE_ON);

	/* Empty the buffer queue to get fresh data with later commands. */
	cyapa_empty_pip_output_data(cyapa, NULL, NULL, NULL);

	/*
	 * Read description info from trackpad device to determine running in
	 * APP mode or Bootloader mode.
	 */
	resp_len = PIP_HID_DESCRIPTOR_SIZE;
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			cmd, sizeof(cmd),
			resp_data, &resp_len,
			300,
			cyapa_sort_pip_hid_descriptor_data,
			false);
	if (error)
		return error;

	if (resp_data[PIP_RESP_REPORT_ID_OFFSET] == PIP_HID_BL_REPORT_ID)
		is_bootloader = true;
	else if (resp_data[PIP_RESP_REPORT_ID_OFFSET] == PIP_HID_APP_REPORT_ID)
		is_bootloader = false;
	else
		return -EAGAIN;

	/* Get PIP fixed information to determine Gen5 or Gen6. */
	memset(&pip_info, 0, sizeof(struct pip_fixed_info));
	error = cyapa_get_pip_fixed_info(cyapa, &pip_info, is_bootloader);
	if (error)
		return error;

	if (pip_info.family_id == 0x9B && pip_info.silicon_id_high == 0x0B) {
		cyapa->gen = CYAPA_GEN6;
		cyapa->state = is_bootloader ? CYAPA_STATE_GEN6_BL
					     : CYAPA_STATE_GEN6_APP;
	} else if (pip_info.family_id == 0x91 &&
		   pip_info.silicon_id_high == 0x02) {
		cyapa->gen = CYAPA_GEN5;
		cyapa->state = is_bootloader ? CYAPA_STATE_GEN5_BL
					     : CYAPA_STATE_GEN5_APP;
	}

	return 0;
}

static int cyapa_gen6_read_sys_info(struct cyapa *cyapa)
{
	u8 resp_data[PIP_READ_SYS_INFO_RESP_LENGTH];
	int resp_len;
	u16 product_family;
	u8 rotat_align;
	int error;

	/* Get App System Information to determine Gen5 or Gen6. */
	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			pip_read_sys_info, PIP_READ_SYS_INFO_CMD_LENGTH,
			resp_data, &resp_len,
			2000, cyapa_pip_sort_system_info_data, false);
	if (error || resp_len < sizeof(resp_data))
		return error ? error : -EIO;

	product_family = get_unaligned_le16(&resp_data[7]);
	if ((product_family & PIP_PRODUCT_FAMILY_MASK) !=
		PIP_PRODUCT_FAMILY_TRACKPAD)
		return -EINVAL;

	cyapa->platform_ver = (resp_data[67] >> PIP_BL_PLATFORM_VER_SHIFT) &
			      PIP_BL_PLATFORM_VER_MASK;
	cyapa->fw_maj_ver = resp_data[9];
	cyapa->fw_min_ver = resp_data[10];

	cyapa->electrodes_x = resp_data[33];
	cyapa->electrodes_y = resp_data[34];

	cyapa->physical_size_x =  get_unaligned_le16(&resp_data[35]) / 100;
	cyapa->physical_size_y = get_unaligned_le16(&resp_data[37]) / 100;

	cyapa->max_abs_x = get_unaligned_le16(&resp_data[39]);
	cyapa->max_abs_y = get_unaligned_le16(&resp_data[41]);

	cyapa->max_z = get_unaligned_le16(&resp_data[43]);

	cyapa->x_origin = resp_data[45] & 0x01;
	cyapa->y_origin = resp_data[46] & 0x01;

	cyapa->btn_capability = (resp_data[70] << 3) & CAPABILITY_BTN_MASK;

	memcpy(&cyapa->product_id[0], &resp_data[51], 5);
	cyapa->product_id[5] = '-';
	memcpy(&cyapa->product_id[6], &resp_data[56], 6);
	cyapa->product_id[12] = '-';
	memcpy(&cyapa->product_id[13], &resp_data[62], 2);
	cyapa->product_id[15] = '\0';

	/* Get the number of Rx electrodes. */
	rotat_align = resp_data[68];
	cyapa->electrodes_rx =
		rotat_align ? cyapa->electrodes_y : cyapa->electrodes_x;
	cyapa->aligned_electrodes_rx = (cyapa->electrodes_rx + 3) & ~3u;

	if (!cyapa->electrodes_x || !cyapa->electrodes_y ||
		!cyapa->physical_size_x || !cyapa->physical_size_y ||
		!cyapa->max_abs_x || !cyapa->max_abs_y || !cyapa->max_z)
		return -EINVAL;

	return 0;
}

static int cyapa_gen6_bl_read_app_info(struct cyapa *cyapa)
{
	u8 resp_data[PIP_BL_APP_INFO_RESP_LENGTH];
	int resp_len;
	int error;

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			pip_bl_read_app_info, PIP_BL_READ_APP_INFO_CMD_LENGTH,
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_bl_resp_data, false);
	if (error || resp_len < PIP_BL_APP_INFO_RESP_LENGTH ||
		!PIP_CMD_COMPLETE_SUCCESS(resp_data))
		return error ? error : -EIO;

	cyapa->fw_maj_ver = resp_data[8];
	cyapa->fw_min_ver = resp_data[9];

	cyapa->platform_ver = (resp_data[12] >> PIP_BL_PLATFORM_VER_SHIFT) &
			      PIP_BL_PLATFORM_VER_MASK;

	memcpy(&cyapa->product_id[0], &resp_data[13], 5);
	cyapa->product_id[5] = '-';
	memcpy(&cyapa->product_id[6], &resp_data[18], 6);
	cyapa->product_id[12] = '-';
	memcpy(&cyapa->product_id[13], &resp_data[24], 2);
	cyapa->product_id[15] = '\0';

	return 0;

}

static int cyapa_gen6_config_dev_irq(struct cyapa *cyapa, u8 cmd_code)
{
	u8 cmd[] = { 0x04, 0x00, 0x05, 0x00, 0x2f, 0x00, cmd_code };
	u8 resp_data[6];
	int resp_len;
	int error;

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa, cmd, sizeof(cmd),
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, false);
	if (error || !VALID_CMD_RESP_HEADER(resp_data, cmd_code) ||
			!PIP_CMD_COMPLETE_SUCCESS(resp_data)
			)
		return error < 0 ? error : -EINVAL;

	return 0;
}

static int cyapa_gen6_set_proximity(struct cyapa *cyapa, bool enable)
{
	int error;

	cyapa_gen6_config_dev_irq(cyapa, GEN6_DISABLE_CMD_IRQ);
	error = cyapa_pip_set_proximity(cyapa, enable);
	cyapa_gen6_config_dev_irq(cyapa, GEN6_ENABLE_CMD_IRQ);

	return error;
}

static int cyapa_gen6_change_power_state(struct cyapa *cyapa, u8 power_mode)
{
	u8 cmd[] = { 0x04, 0x00, 0x06, 0x00, 0x2f, 0x00, 0x46, power_mode };
	u8 resp_data[6];
	int resp_len;
	int error;

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa, cmd, sizeof(cmd),
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, false);
	if (error || !VALID_CMD_RESP_HEADER(resp_data, 0x46))
		return error < 0 ? error : -EINVAL;

	/* New power state applied in device not match the set power state. */
	if (resp_data[5] != power_mode)
		return -EAGAIN;

	return 0;
}

static int cyapa_gen6_set_interval_setting(struct cyapa *cyapa,
		struct gen6_interval_setting *interval_setting)
{
	struct gen6_set_interval_cmd {
		__le16 addr;
		__le16 length;
		u8 report_id;
		u8 rsvd;  /* Reserved, must be 0 */
		u8 cmd_code;
		__le16 active_interval;
		__le16 lp1_interval;
		__le16 lp2_interval;
	} __packed set_interval_cmd;
	u8 resp_data[11];
	int resp_len;
	int error;

	memset(&set_interval_cmd, 0, sizeof(set_interval_cmd));
	put_unaligned_le16(PIP_OUTPUT_REPORT_ADDR, &set_interval_cmd.addr);
	put_unaligned_le16(sizeof(set_interval_cmd) - 2,
			   &set_interval_cmd.length);
	set_interval_cmd.report_id = PIP_APP_CMD_REPORT_ID;
	set_interval_cmd.cmd_code = GEN6_SET_POWER_MODE_INTERVAL;
	put_unaligned_le16(interval_setting->active_interval,
			   &set_interval_cmd.active_interval);
	put_unaligned_le16(interval_setting->lp1_interval,
			   &set_interval_cmd.lp1_interval);
	put_unaligned_le16(interval_setting->lp2_interval,
			   &set_interval_cmd.lp2_interval);

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
			(u8 *)&set_interval_cmd, sizeof(set_interval_cmd),
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, false);
	if (error ||
		!VALID_CMD_RESP_HEADER(resp_data, GEN6_SET_POWER_MODE_INTERVAL))
		return error < 0 ? error : -EINVAL;

	/* Get the real set intervals from response. */
	interval_setting->active_interval = get_unaligned_le16(&resp_data[5]);
	interval_setting->lp1_interval = get_unaligned_le16(&resp_data[7]);
	interval_setting->lp2_interval = get_unaligned_le16(&resp_data[9]);

	return 0;
}

static int cyapa_gen6_get_interval_setting(struct cyapa *cyapa,
		struct gen6_interval_setting *interval_setting)
{
	u8 cmd[] = { 0x04, 0x00, 0x05, 0x00, 0x2f, 0x00,
		     GEN6_GET_POWER_MODE_INTERVAL };
	u8 resp_data[11];
	int resp_len;
	int error;

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa, cmd, sizeof(cmd),
			resp_data, &resp_len,
			500, cyapa_sort_tsg_pip_app_resp_data, false);
	if (error ||
		!VALID_CMD_RESP_HEADER(resp_data, GEN6_GET_POWER_MODE_INTERVAL))
		return error < 0 ? error : -EINVAL;

	interval_setting->active_interval = get_unaligned_le16(&resp_data[5]);
	interval_setting->lp1_interval = get_unaligned_le16(&resp_data[7]);
	interval_setting->lp2_interval = get_unaligned_le16(&resp_data[9]);

	return 0;
}

static int cyapa_gen6_deep_sleep(struct cyapa *cyapa, u8 state)
{
	u8 ping[] = { 0x04, 0x00, 0x05, 0x00, 0x2f, 0x00, 0x00 };

	if (state == PIP_DEEP_SLEEP_STATE_ON)
		/*
		 * Send ping command to notify device prepare for wake up
		 * when it's in deep sleep mode. At this time, device will
		 * response nothing except an I2C NAK.
		 */
		cyapa_i2c_pip_write(cyapa, ping, sizeof(ping));

	return cyapa_pip_deep_sleep(cyapa, state);
}

static int cyapa_gen6_set_power_mode(struct cyapa *cyapa,
		u8 power_mode, u16 sleep_time, enum cyapa_pm_stage pm_stage)
{
	struct device *dev = &cyapa->client->dev;
	struct gen6_interval_setting *interval_setting =
			&cyapa->gen6_interval_setting;
	u8 lp_mode;
	int error;

	if (cyapa->state != CYAPA_STATE_GEN6_APP)
		return 0;

	if (PIP_DEV_GET_PWR_STATE(cyapa) == UNINIT_PWR_MODE) {
		/*
		 * Assume TP in deep sleep mode when driver is loaded,
		 * avoid driver unload and reload command IO issue caused by TP
		 * has been set into deep sleep mode when unloading.
		 */
		PIP_DEV_SET_PWR_STATE(cyapa, PWR_MODE_OFF);
	}

	if (PIP_DEV_UNINIT_SLEEP_TIME(cyapa) &&
		PIP_DEV_GET_PWR_STATE(cyapa) != PWR_MODE_OFF)
		PIP_DEV_SET_SLEEP_TIME(cyapa, UNINIT_SLEEP_TIME);

	if (PIP_DEV_GET_PWR_STATE(cyapa) == power_mode) {
		if (power_mode == PWR_MODE_OFF ||
			power_mode == PWR_MODE_FULL_ACTIVE ||
			power_mode == PWR_MODE_BTN_ONLY ||
			PIP_DEV_GET_SLEEP_TIME(cyapa) == sleep_time) {
			/* Has in correct power mode state, early return. */
			return 0;
		}
	}

	if (power_mode == PWR_MODE_OFF) {
		cyapa_gen6_config_dev_irq(cyapa, GEN6_DISABLE_CMD_IRQ);

		error = cyapa_gen6_deep_sleep(cyapa, PIP_DEEP_SLEEP_STATE_OFF);
		if (error) {
			dev_err(dev, "enter deep sleep fail: %d\n", error);
			return error;
		}

		PIP_DEV_SET_PWR_STATE(cyapa, PWR_MODE_OFF);
		return 0;
	}

	/*
	 * When trackpad in power off mode, it cannot change to other power
	 * state directly, must be wake up from sleep firstly, then
	 * continue to do next power sate change.
	 */
	if (PIP_DEV_GET_PWR_STATE(cyapa) == PWR_MODE_OFF) {
		error = cyapa_gen6_deep_sleep(cyapa, PIP_DEEP_SLEEP_STATE_ON);
		if (error) {
			dev_err(dev, "deep sleep wake fail: %d\n", error);
			return error;
		}
	}

	/*
	 * Disable device assert interrupts for command response to avoid
	 * disturbing system suspending or hibernating process.
	 */
	cyapa_gen6_config_dev_irq(cyapa, GEN6_DISABLE_CMD_IRQ);

	if (power_mode == PWR_MODE_FULL_ACTIVE) {
		error = cyapa_gen6_change_power_state(cyapa,
				GEN6_POWER_MODE_ACTIVE);
		if (error) {
			dev_err(dev, "change to active fail: %d\n", error);
			goto out;
		}

		PIP_DEV_SET_PWR_STATE(cyapa, PWR_MODE_FULL_ACTIVE);

		/* Sync the interval setting from device. */
		cyapa_gen6_get_interval_setting(cyapa, interval_setting);

	} else if (power_mode == PWR_MODE_BTN_ONLY) {
		error = cyapa_gen6_change_power_state(cyapa,
				GEN6_POWER_MODE_BTN_ONLY);
		if (error) {
			dev_err(dev, "fail to button only mode: %d\n", error);
			goto out;
		}

		PIP_DEV_SET_PWR_STATE(cyapa, PWR_MODE_BTN_ONLY);
	} else {
		/*
		 * Gen6 internally supports to 2 low power scan interval time,
		 * so can help to switch power mode quickly.
		 * such as runtime suspend and system suspend.
		 */
		if (interval_setting->lp1_interval == sleep_time) {
			lp_mode = GEN6_POWER_MODE_LP_MODE1;
		} else if (interval_setting->lp2_interval == sleep_time) {
			lp_mode = GEN6_POWER_MODE_LP_MODE2;
		} else {
			if (interval_setting->lp1_interval == 0) {
				interval_setting->lp1_interval = sleep_time;
				lp_mode = GEN6_POWER_MODE_LP_MODE1;
			} else {
				interval_setting->lp2_interval = sleep_time;
				lp_mode = GEN6_POWER_MODE_LP_MODE2;
			}
			cyapa_gen6_set_interval_setting(cyapa,
							interval_setting);
		}

		error = cyapa_gen6_change_power_state(cyapa, lp_mode);
		if (error) {
			dev_err(dev, "set power state to 0x%02x failed: %d\n",
				lp_mode, error);
			goto out;
		}

		PIP_DEV_SET_SLEEP_TIME(cyapa, sleep_time);
		PIP_DEV_SET_PWR_STATE(cyapa,
			cyapa_sleep_time_to_pwr_cmd(sleep_time));
	}

out:
	cyapa_gen6_config_dev_irq(cyapa, GEN6_ENABLE_CMD_IRQ);
	return error;
}

static int cyapa_gen6_initialize(struct cyapa *cyapa)
{
	return 0;
}

static int cyapa_pip_retrieve_data_structure(struct cyapa *cyapa,
		u16 read_offset, u16 read_len, u8 data_id,
		u8 *data, int *data_buf_lens)
{
	struct retrieve_data_struct_cmd {
		struct pip_app_cmd_head head;
		__le16 read_offset;
		__le16 read_length;
		u8 data_id;
	} __packed cmd;
	u8 resp_data[GEN6_MAX_RX_NUM + 10];
	int resp_len;
	int error;

	memset(&cmd, 0, sizeof(cmd));
	put_unaligned_le16(PIP_OUTPUT_REPORT_ADDR, &cmd.head.addr);
	put_unaligned_le16(sizeof(cmd), &cmd.head.length - 2);
	cmd.head.report_id = PIP_APP_CMD_REPORT_ID;
	cmd.head.cmd_code = PIP_RETRIEVE_DATA_STRUCTURE;
	put_unaligned_le16(read_offset, &cmd.read_offset);
	put_unaligned_le16(read_len, &cmd.read_length);
	cmd.data_id = data_id;

	resp_len = sizeof(resp_data);
	error = cyapa_i2c_pip_cmd_irq_sync(cyapa,
				(u8 *)&cmd, sizeof(cmd),
				resp_data, &resp_len,
				500, cyapa_sort_tsg_pip_app_resp_data,
				true);
	if (error || !PIP_CMD_COMPLETE_SUCCESS(resp_data) ||
		resp_data[6] != data_id ||
		!VALID_CMD_RESP_HEADER(resp_data, PIP_RETRIEVE_DATA_STRUCTURE))
		return (error < 0) ? error : -EAGAIN;

	read_len = get_unaligned_le16(&resp_data[7]);
	if (*data_buf_lens < read_len) {
		*data_buf_lens = read_len;
		return -ENOBUFS;
	}

	memcpy(data, &resp_data[10], read_len);
	*data_buf_lens = read_len;
	return 0;
}

static ssize_t cyapa_gen6_show_baseline(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyapa *cyapa = dev_get_drvdata(dev);
	u8 data[GEN6_MAX_RX_NUM];
	int data_len;
	int size = 0;
	int i;
	int error;
	int resume_error;

	if (!cyapa_is_pip_app_mode(cyapa))
		return -EBUSY;

	/* 1. Suspend Scanning*/
	error = cyapa_pip_suspend_scanning(cyapa);
	if (error)
		return error;

	/* 2. IDAC and RX Attenuator Calibration Data (Center Frequency). */
	data_len = sizeof(data);
	error = cyapa_pip_retrieve_data_structure(cyapa, 0, data_len,
			GEN6_RETRIEVE_DATA_ID_RX_ATTENURATOR_IDAC,
			data, &data_len);
	if (error)
		goto resume_scanning;

	size = scnprintf(buf, PAGE_SIZE, "%d %d %d %d %d %d ",
			data[0],  /* RX Attenuator Mutual */
			data[1],  /* IDAC Mutual */
			data[2],  /* RX Attenuator Self RX */
			data[3],  /* IDAC Self RX */
			data[4],  /* RX Attenuator Self TX */
			data[5]	  /* IDAC Self TX */
			);

	/* 3. Read Attenuator Trim. */
	data_len = sizeof(data);
	error = cyapa_pip_retrieve_data_structure(cyapa, 0, data_len,
			GEN6_RETRIEVE_DATA_ID_ATTENURATOR_TRIM,
			data, &data_len);
	if (error)
		goto resume_scanning;

	/* set attenuator trim values. */
	for (i = 0; i < data_len; i++)
		size += scnprintf(buf + size, PAGE_SIZE - size,	"%d ", data[i]);
	size += scnprintf(buf + size, PAGE_SIZE - size, "\n");

resume_scanning:
	/* 4. Resume Scanning*/
	resume_error = cyapa_pip_resume_scanning(cyapa);
	if (resume_error || error) {
		memset(buf, 0, PAGE_SIZE);
		return resume_error ? resume_error : error;
	}

	return size;
}

static int cyapa_gen6_operational_check(struct cyapa *cyapa)
{
	struct device *dev = &cyapa->client->dev;
	int error;

	if (cyapa->gen != CYAPA_GEN6)
		return -ENODEV;

	switch (cyapa->state) {
	case CYAPA_STATE_GEN6_BL:
		error = cyapa_pip_bl_exit(cyapa);
		if (error) {
			/* Try to update trackpad product information. */
			cyapa_gen6_bl_read_app_info(cyapa);
			goto out;
		}

		cyapa->state = CYAPA_STATE_GEN6_APP;
		/* fall through */

	case CYAPA_STATE_GEN6_APP:
		/*
		 * If trackpad device in deep sleep mode,
		 * the app command will fail.
		 * So always try to reset trackpad device to full active when
		 * the device state is required.
		 */
		error = cyapa_gen6_set_power_mode(cyapa,
				PWR_MODE_FULL_ACTIVE, 0, CYAPA_PM_ACTIVE);
		if (error)
			dev_warn(dev, "%s: failed to set power active mode.\n",
				__func__);

		/* By default, the trackpad proximity function is enabled. */
		error = cyapa_pip_set_proximity(cyapa, true);
		if (error)
			dev_warn(dev, "%s: failed to enable proximity.\n",
				__func__);

		/* Get trackpad product information. */
		error = cyapa_gen6_read_sys_info(cyapa);
		if (error)
			goto out;
		/* Only support product ID starting with CYTRA */
		if (memcmp(cyapa->product_id, product_id,
				strlen(product_id)) != 0) {
			dev_err(dev, "%s: unknown product ID (%s)\n",
				__func__, cyapa->product_id);
			error = -EINVAL;
		}
		break;
	default:
		error = -EINVAL;
	}

out:
	return error;
}

const struct cyapa_dev_ops cyapa_gen6_ops = {
	.check_fw = cyapa_pip_check_fw,
	.bl_enter = cyapa_pip_bl_enter,
	.bl_initiate = cyapa_pip_bl_initiate,
	.update_fw = cyapa_pip_do_fw_update,
	.bl_activate = cyapa_pip_bl_activate,
	.bl_deactivate = cyapa_pip_bl_deactivate,

	.show_baseline = cyapa_gen6_show_baseline,
	.calibrate_store = cyapa_pip_do_calibrate,

	.initialize = cyapa_gen6_initialize,

	.state_parse = cyapa_pip_state_parse,
	.operational_check = cyapa_gen6_operational_check,

	.irq_handler = cyapa_pip_irq_handler,
	.irq_cmd_handler = cyapa_pip_irq_cmd_handler,
	.sort_empty_output_data = cyapa_empty_pip_output_data,
	.set_power_mode = cyapa_gen6_set_power_mode,

	.set_proximity = cyapa_gen6_set_proximity,
};
