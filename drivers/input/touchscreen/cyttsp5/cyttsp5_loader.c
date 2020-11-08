/*
 * cyttsp5_loader.c
 * Parade TrueTouch(TM) Standard Product V5 FW Loader Module.
 * For use with Parade touchscreen controllers.
 * Supported parts include:
 * CYTMA5XX
 * CYTMA448
 * CYTMA445A
 * CYTT21XXX
 * CYTT31XXX
 *
 * Copyright (C) 2015 Parade Technologies
 * Copyright (C) 2012-2015 Cypress Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contact Parade Technologies at www.paradetech.com <ttdrivers@paradetech.com>
 *
 */

#include "cyttsp5_regs.h"
#include <linux/firmware.h>

#define CYTTSP5_LOADER_NAME "cyttsp5_loader"
#define CY_FW_MANUAL_UPGRADE_FILE_NAME "cyttsp5_fw_manual_upgrade"

/* Enable UPGRADE_FW_AND_CONFIG_IN_PROBE definition
 * to perform FW and config upgrade during probe
 * instead of scheduling a work for it
 */
/* #define UPGRADE_FW_AND_CONFIG_IN_PROBE */

#define CYTTSP5_AUTO_LOAD_FOR_CORRUPTED_FW 1
#define CYTTSP5_LOADER_FW_UPGRADE_RETRY_COUNT 3


#if defined(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_FW_UPGRADE) || defined(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_BINARY_FW_UPGRADE)
#define CYTTSP5_FW_UPGRADE 1
#else
#define CYTTSP5_FW_UPGRADE 0
#endif

#if defined(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_TTCONFIG_UPGRADE) || defined(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_MANUAL_TTCONFIG_UPGRADE)
#define CYTTSP5_TTCONFIG_UPGRADE 1
#else
#define CYTTSP5_TTCONFIG_UPGRADE 0
#endif

static const u8 cyttsp5_security_key[] = {
	0xA5, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0xFD, 0x5A
};

/* Timeout values in ms. */
#define CY_LDR_REQUEST_EXCLUSIVE_TIMEOUT		500
#define CY_LDR_SWITCH_TO_APP_MODE_TIMEOUT		300

#define CY_MAX_STATUS_SIZE				32

#define CY_DATA_MAX_ROW_SIZE				256
#define CY_DATA_ROW_SIZE				128

#define CY_ARRAY_ID_OFFSET				0
#define CY_ROW_NUM_OFFSET				1
#define CY_ROW_SIZE_OFFSET				3
#define CY_ROW_DATA_OFFSET				5

#define CY_POST_TT_CFG_CRC_MASK				0x2

struct cyttsp5_loader_data {
	struct device *dev;
	struct cyttsp5_sysinfo *si;
	u8 status_buf[CY_MAX_STATUS_SIZE];
	struct completion int_running;
	struct completion calibration_complete;
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_BINARY_FW_UPGRADE
	struct completion builtin_bin_fw_complete;
	int builtin_bin_fw_status;
	bool is_manual_upgrade_enabled;
#endif
	struct work_struct fw_and_config_upgrade;
	struct work_struct calibration_work;
	struct cyttsp5_loader_platform_data *loader_pdata;
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_MANUAL_TTCONFIG_UPGRADE
	struct mutex config_lock;
	u8 *config_data;
	int config_size;
	bool config_loading;
#endif
};

struct cyttsp5_dev_id {
	u32 silicon_id;
	u8 rev_id;
	u32 bl_ver;
};

struct cyttsp5_hex_image {
	u8 array_id;
	u16 row_num;
	u16 row_size;
	u8 row_data[CY_DATA_ROW_SIZE];
} __packed;

static struct cyttsp5_core_commands *cmd;

static struct cyttsp5_module loader_module;

static inline struct cyttsp5_loader_data *cyttsp5_get_loader_data(
		struct device *dev)
{
	return cyttsp5_get_module_data(dev, &loader_module);
}

#if CYTTSP5_FW_UPGRADE \
	|| defined(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_TTCONFIG_UPGRADE)
static u8 cyttsp5_get_panel_id(struct device *dev)
{
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);

	return cd->panel_id;
}
#endif

#if CYTTSP5_FW_UPGRADE || CYTTSP5_TTCONFIG_UPGRADE
/*
 * return code:
 * -1: Do not upgrade firmware
 *  0: Version info same, let caller decide
 *  1: Do a firmware upgrade
 */
static int cyttsp5_check_firmware_version(struct device *dev,
		u32 fw_ver_new, u32 fw_revctrl_new)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	u32 fw_ver_img;
	u32 fw_revctrl_img;

	fw_ver_img = ld->si->cydata.fw_ver_major << 8;
	fw_ver_img += ld->si->cydata.fw_ver_minor;

	parade_debug(dev, DEBUG_LEVEL_1,
		"%s: img vers:0x%04X new vers:0x%04X\n", __func__,
			fw_ver_img, fw_ver_new);

	if (fw_ver_new > fw_ver_img) {
		parade_debug(dev, DEBUG_LEVEL_1,
			"%s: Image is newer, will upgrade\n", __func__);
		return 1;
	}

	if (fw_ver_new < fw_ver_img) {
		parade_debug(dev, DEBUG_LEVEL_1,
			"%s: Image is older, will NOT upgrade\n", __func__);
		return -1;
	}

	fw_revctrl_img = ld->si->cydata.revctrl;

	parade_debug(dev, DEBUG_LEVEL_1,
		"%s: img revctrl:0x%04X new revctrl:0x%04X\n",
		__func__, fw_revctrl_img, fw_revctrl_new);

	if (fw_revctrl_new > fw_revctrl_img) {
		parade_debug(dev, DEBUG_LEVEL_1,
			"%s: Image is newer, will upgrade\n", __func__);
		return 1;
	}

	if (fw_revctrl_new < fw_revctrl_img) {
		parade_debug(dev, DEBUG_LEVEL_1,
			"%s: Image is older, will NOT upgrade\n", __func__);
		return -1;
	}

	return 0;
}

static void cyttsp5_calibrate_idacs(struct work_struct *calibration_work)
{
	struct cyttsp5_loader_data *ld = container_of(calibration_work,
			struct cyttsp5_loader_data, calibration_work);
	struct device *dev = ld->dev;
	u8 mode;
	u8 status;
	int rc;

	rc = cmd->request_exclusive(dev, CY_LDR_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0)
		goto exit;

	rc = cmd->nonhid_cmd->suspend_scanning(dev, 0);
	if (rc < 0)
		goto release;

	for (mode = 0; mode < 3; mode++) {
		rc = cmd->nonhid_cmd->calibrate_idacs(dev, 0, mode, &status);
		if (rc < 0)
			goto release;
	}

	rc = cmd->nonhid_cmd->resume_scanning(dev, 0);
	if (rc < 0)
		goto release;

	parade_debug(dev, DEBUG_LEVEL_1, "%s: Calibration Done\n", __func__);

release:
	cmd->release_exclusive(dev);
exit:
	complete(&ld->calibration_complete);
}

static int cyttsp5_calibration_attention(struct device *dev)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	int rc = 0;

	schedule_work(&ld->calibration_work);

	cmd->unsubscribe_attention(dev, CY_ATTEN_STARTUP, CYTTSP5_LOADER_NAME,
		cyttsp5_calibration_attention, 0);

	return rc;
}


#endif /* CYTTSP5_FW_UPGRADE || CYTTSP5_TTCONFIG_UPGRADE */

#if CYTTSP5_FW_UPGRADE
static u8 *cyttsp5_get_row_(struct device *dev, u8 *row_buf,
		u8 *image_buf, int size)
{
	memcpy(row_buf, image_buf, size);
	return image_buf + size;
}

static int cyttsp5_ldr_enter_(struct device *dev, struct cyttsp5_dev_id *dev_id)
{
	int rc;
	u8 return_data[8];
	u8 mode;

	dev_id->silicon_id = 0;
	dev_id->rev_id = 0;
	dev_id->bl_ver = 0;

	cmd->request_reset(dev);

	rc = cmd->request_get_mode(dev, 0, &mode);
	if (rc < 0)
		return rc;

	if (mode == CY_MODE_UNKNOWN)
		return -EINVAL;

	if (mode == CY_MODE_OPERATIONAL) {
		rc = cmd->nonhid_cmd->start_bl(dev, 0);
		if (rc < 0)
			return rc;
	}

	rc = cmd->nonhid_cmd->get_bl_info(dev, 0, return_data);
	if (rc < 0)
		return rc;

	dev_id->silicon_id = get_unaligned_le32(&return_data[0]);
	dev_id->rev_id = return_data[4];
	dev_id->bl_ver = return_data[5] + (return_data[6] << 8)
		+ (return_data[7] << 16);

	return 0;
}

static int cyttsp5_ldr_init_(struct device *dev,
		struct cyttsp5_hex_image *row_image)
{
	return cmd->nonhid_cmd->initiate_bl(dev, 0, 8,
			(u8 *)cyttsp5_security_key, row_image->row_size,
			row_image->row_data);
}

static int cyttsp5_ldr_parse_row_(struct device *dev, u8 *row_buf,
	struct cyttsp5_hex_image *row_image)
{
	int rc = 0;

	row_image->array_id = row_buf[CY_ARRAY_ID_OFFSET];
	row_image->row_num = get_unaligned_be16(&row_buf[CY_ROW_NUM_OFFSET]);
	row_image->row_size = get_unaligned_be16(&row_buf[CY_ROW_SIZE_OFFSET]);

	if (row_image->row_size > ARRAY_SIZE(row_image->row_data)) {
		dev_err(dev, "%s: row data buffer overflow\n", __func__);
		rc = -EOVERFLOW;
		goto cyttsp5_ldr_parse_row_exit;
	}

	memcpy(row_image->row_data, &row_buf[CY_ROW_DATA_OFFSET],
	       row_image->row_size);
cyttsp5_ldr_parse_row_exit:
	return rc;
}

static int cyttsp5_ldr_prog_row_(struct device *dev,
				 struct cyttsp5_hex_image *row_image)
{
	u16 length = row_image->row_size + 3;
	u8 data[3 + row_image->row_size];
	u8 offset = 0;

	data[offset++] = row_image->array_id;
	data[offset++] = LOW_BYTE(row_image->row_num);
	data[offset++] = HI_BYTE(row_image->row_num);
	memcpy(data + 3, row_image->row_data, row_image->row_size);
	return cmd->nonhid_cmd->prog_and_verify(dev, 0, length, data);
}

static int cyttsp5_ldr_verify_chksum_(struct device *dev)
{
	u8 result;
	int rc;

	rc = cmd->nonhid_cmd->verify_app_integrity(dev, 0, &result);
	if (rc)
		return rc;

	/* fail */
	if (result == 0)
		return -EINVAL;

	return 0;
}

static int cyttsp5_ldr_exit_(struct device *dev)
{
	return cmd->nonhid_cmd->launch_app(dev, 0);
}

static int cyttsp5_load_app_(struct device *dev, const u8 *fw, int fw_size)
{
	struct cyttsp5_dev_id *dev_id;
	struct cyttsp5_hex_image *row_image;
	u8 *row_buf;
	size_t image_rec_size;
	size_t row_buf_size = CY_DATA_MAX_ROW_SIZE;
	int row_count = 0;
	u8 *p;
	u8 *last_row;
	int rc;
	int rc_tmp;

	image_rec_size = sizeof(struct cyttsp5_hex_image);
	if (fw_size % image_rec_size != 0) {
		dev_err(dev, "%s: Firmware image is misaligned\n", __func__);
		rc = -EINVAL;
		goto _cyttsp5_load_app_error;
	}

	dev_info(dev, "%s: start load app\n", __func__);
#ifdef TTHE_TUNER_SUPPORT
	cmd->request_tthe_print(dev, NULL, 0, "start load app");
#endif

	row_buf = kzalloc(row_buf_size, GFP_KERNEL);
	row_image = kzalloc(sizeof(struct cyttsp5_hex_image), GFP_KERNEL);
	dev_id = kzalloc(sizeof(struct cyttsp5_dev_id), GFP_KERNEL);
	if (!row_buf || !row_image || !dev_id) {
		rc = -ENOMEM;
		goto _cyttsp5_load_app_exit;
	}

	cmd->request_stop_wd(dev);

	dev_info(dev, "%s: Send BL Loader Enter\n", __func__);
#ifdef TTHE_TUNER_SUPPORT
	cmd->request_tthe_print(dev, NULL, 0, "Send BL Loader Enter");
#endif
	rc = cyttsp5_ldr_enter_(dev, dev_id);
	if (rc) {
		dev_err(dev, "%s: Error cannot start Loader (ret=%d)\n",
			__func__, rc);
		goto _cyttsp5_load_app_exit;
	}
	parade_debug(dev, DEBUG_LEVEL_2, "%s: dev: silicon id=%08X rev=%02X bl=%08X\n",
		__func__, dev_id->silicon_id,
		dev_id->rev_id, dev_id->bl_ver);

	/* get last row */
	last_row = (u8 *)fw + fw_size - image_rec_size;
	cyttsp5_get_row_(dev, row_buf, last_row, image_rec_size);
	cyttsp5_ldr_parse_row_(dev, row_buf, row_image);

	/* initialise bootloader */
	rc = cyttsp5_ldr_init_(dev, row_image);
	if (rc) {
		dev_err(dev, "%s: Error cannot init Loader (ret=%d)\n",
			__func__, rc);
		goto _cyttsp5_load_app_exit;
	}

	dev_info(dev, "%s: Send BL Loader Blocks\n", __func__);
#ifdef TTHE_TUNER_SUPPORT
	cmd->request_tthe_print(dev, NULL, 0, "Send BL Loader Blocks");
#endif
	p = (u8 *)fw;
	while (p < last_row) {
		/* Get row */
		parade_debug(dev, DEBUG_LEVEL_1, "%s: read row=%d\n",
			__func__, ++row_count);
		memset(row_buf, 0, row_buf_size);
		p = cyttsp5_get_row_(dev, row_buf, p, image_rec_size);

		/* Parse row */
		parade_debug(dev, DEBUG_LEVEL_2, "%s: p=%p buf=%p buf[0]=%02X\n",
			__func__, p, row_buf, row_buf[0]);
		rc = cyttsp5_ldr_parse_row_(dev, row_buf, row_image);
		parade_debug(dev, DEBUG_LEVEL_2, "%s: array_id=%02X row_num=%04X(%d) row_size=%04X(%d)\n",
			__func__, row_image->array_id,
			row_image->row_num, row_image->row_num,
			row_image->row_size, row_image->row_size);
		if (rc) {
			dev_err(dev, "%s: Parse Row Error (a=%d r=%d ret=%d\n",
				__func__, row_image->array_id,
				row_image->row_num, rc);
			goto _cyttsp5_load_app_exit;
		} else {
			parade_debug(dev, DEBUG_LEVEL_2, "%s: Parse Row (a=%d r=%d ret=%d\n",
				__func__, row_image->array_id,
				row_image->row_num, rc);
		}

		/* program row */
		rc = cyttsp5_ldr_prog_row_(dev, row_image);
		if (rc) {
			dev_err(dev, "%s: Program Row Error (array=%d row=%d ret=%d)\n",
				__func__, row_image->array_id,
				row_image->row_num, rc);
			goto _cyttsp5_load_app_exit;
		}

		parade_debug(dev, DEBUG_LEVEL_2, "%s: array=%d row_cnt=%d row_num=%04X\n",
			__func__, row_image->array_id, row_count,
			row_image->row_num);
	}

	/* exit loader */
	dev_info(dev, "%s: Send BL Loader Terminate\n", __func__);
#ifdef TTHE_TUNER_SUPPORT
	cmd->request_tthe_print(dev, NULL, 0, "Send BL Loader Terminate");
#endif
	rc = cyttsp5_ldr_exit_(dev);
	if (rc) {
		dev_err(dev, "%s: Error on exit Loader (ret=%d)\n",
			__func__, rc);

		/* verify app checksum */
		rc_tmp = cyttsp5_ldr_verify_chksum_(dev);
		if (rc_tmp)
			dev_err(dev, "%s: ldr_verify_chksum fail r=%d\n",
				__func__, rc_tmp);
		else
			dev_info(dev, "%s: APP Checksum Verified\n", __func__);
	}

_cyttsp5_load_app_exit:
	kfree(row_buf);
	kfree(row_image);
	kfree(dev_id);
_cyttsp5_load_app_error:
	return rc;
}

static int cyttsp5_upgrade_firmware(struct device *dev, const u8 *fw_img,
		int fw_size)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	int retry = CYTTSP5_LOADER_FW_UPGRADE_RETRY_COUNT;
	bool wait_for_calibration_complete = false;
	int rc;

	pm_runtime_get_sync(dev);

	rc = cmd->request_exclusive(dev, CY_LDR_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0)
		goto exit;

	while (retry--) {
		rc = cyttsp5_load_app_(dev, fw_img, fw_size);
		if (rc < 0)
			dev_err(dev, "%s: Firmware update failed rc=%d, retry:%d\n",
				__func__, rc, retry);
		else
			break;
		msleep(20);
	}
	if (rc < 0) {
		dev_err(dev, "%s: Firmware update failed with error code %d\n",
			__func__, rc);
	} else if (ld->loader_pdata &&
			(ld->loader_pdata->flags
			 & CY_LOADER_FLAG_CALIBRATE_AFTER_FW_UPGRADE)) {
#if (KERNEL_VERSION(3, 13, 0) <= LINUX_VERSION_CODE)
		reinit_completion(&ld->calibration_complete);
#else
		INIT_COMPLETION(ld->calibration_complete);
#endif
		/* set up call back for startup */
		parade_debug(dev, DEBUG_LEVEL_2, "%s: Adding callback for calibration\n",
			__func__);
		rc = cmd->subscribe_attention(dev, CY_ATTEN_STARTUP,
			CYTTSP5_LOADER_NAME, cyttsp5_calibration_attention, 0);
		if (rc) {
			dev_err(dev, "%s: Failed adding callback for calibration\n",
				__func__);
			dev_err(dev, "%s: No calibration will be performed\n",
				__func__);
			rc = 0;
		} else
			wait_for_calibration_complete = true;
	}

	cmd->release_exclusive(dev);

exit:
	if (!rc)
		cmd->request_restart(dev, true);

	pm_runtime_put_sync(dev);

	if (wait_for_calibration_complete)
		wait_for_completion(&ld->calibration_complete);

	return rc;
}

static int cyttsp5_loader_attention(struct device *dev)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);

	complete(&ld->int_running);
	return 0;
}
#endif /* CYTTSP5_FW_UPGRADE */

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_FW_UPGRADE
static int cyttsp5_check_firmware_version_platform(struct device *dev,
		struct cyttsp5_touch_firmware *fw)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	u32 fw_ver_new;
	u32 fw_revctrl_new;
	int upgrade;

	if (!ld->si) {
		dev_info(dev, "%s: No firmware infomation found, device FW may be corrupted\n",
			__func__);
		return CYTTSP5_AUTO_LOAD_FOR_CORRUPTED_FW;
	}

	fw_ver_new = get_unaligned_be16(fw->ver + 2);
	/* 4 middle bytes are not used */
	fw_revctrl_new = get_unaligned_be32(fw->ver + 8);

	upgrade = cyttsp5_check_firmware_version(dev, fw_ver_new,
		fw_revctrl_new);

	if (upgrade > 0)
		return 1;

	return 0;
}

static struct cyttsp5_touch_firmware *cyttsp5_get_platform_firmware(
		struct device *dev)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	struct cyttsp5_touch_firmware **fws;
	struct cyttsp5_touch_firmware *fw;
	u8 panel_id;

	panel_id = cyttsp5_get_panel_id(dev);
	if (panel_id == PANEL_ID_NOT_ENABLED) {
		parade_debug(dev, DEBUG_LEVEL_1, "%s: Panel ID not enabled, using legacy firmware\n",
			__func__);
		return ld->loader_pdata->fw;
	}

	fws = ld->loader_pdata->fws;
	if (!fws) {
		dev_err(dev, "%s: No firmwares provided\n", __func__);
		return NULL;
	}

	/* Find FW according to the Panel ID */
	while ((fw = *fws++)) {
		if (fw->panel_id == panel_id) {
			parade_debug(dev, DEBUG_LEVEL_1, "%s: Found matching fw:%p with Panel ID: 0x%02X\n",
				__func__, fw, fw->panel_id);
			return fw;
		}
		parade_debug(dev, DEBUG_LEVEL_2, "%s: Found mismatching fw:%p with Panel ID: 0x%02X\n",
			__func__, fw, fw->panel_id);
	}

	return NULL;
}

static int upgrade_firmware_from_platform(struct device *dev,
		bool forced)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	struct cyttsp5_touch_firmware *fw;
	int rc = -ENODEV;
	int upgrade;

	if (!ld->loader_pdata) {
		dev_err(dev, "%s: No loader platform data\n", __func__);
		return rc;
	}

	fw = cyttsp5_get_platform_firmware(dev);
	if (!fw || !fw->img || !fw->size) {
		dev_err(dev, "%s: No platform firmware\n", __func__);
		return rc;
	}

	if (!fw->ver || !fw->vsize) {
		dev_err(dev, "%s: No platform firmware version\n",
			__func__);
		return rc;
	}

	if (forced)
		upgrade = forced;
	else
		upgrade = cyttsp5_check_firmware_version_platform(dev, fw);

	if (upgrade)
		return cyttsp5_upgrade_firmware(dev, fw->img, fw->size);

	return rc;
}
#endif /* CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_FW_UPGRADE */

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_BINARY_FW_UPGRADE
static void _cyttsp5_firmware_cont(const struct firmware *fw, void *context)
{
	struct device *dev = context;
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	u8 header_size = 0;

	if (!fw)
		goto cyttsp5_firmware_cont_exit;

	if (!fw->data || !fw->size) {
		dev_err(dev, "%s: No firmware received\n", __func__);
		goto cyttsp5_firmware_cont_release_exit;
	}

	header_size = fw->data[0];
	if (header_size >= (fw->size + 1)) {
		dev_err(dev, "%s: Firmware format is invalid\n", __func__);
		goto cyttsp5_firmware_cont_release_exit;
	}

	cyttsp5_upgrade_firmware(dev, &(fw->data[header_size + 1]),
		fw->size - (header_size + 1));

cyttsp5_firmware_cont_release_exit:
	release_firmware(fw);

cyttsp5_firmware_cont_exit:
	ld->is_manual_upgrade_enabled = 0;
}

static int cyttsp5_check_firmware_version_builtin(struct device *dev,
		const struct firmware *fw)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	u32 fw_ver_new;
	u32 fw_revctrl_new;
	int upgrade;

	if (!ld->si) {
		dev_info(dev, "%s: No firmware infomation found, device FW may be corrupted\n",
			__func__);
		return CYTTSP5_AUTO_LOAD_FOR_CORRUPTED_FW;
	}

	fw_ver_new = get_unaligned_be16(fw->data + 3);
	/* 4 middle bytes are not used */
	fw_revctrl_new = get_unaligned_be32(fw->data + 9);

	upgrade = cyttsp5_check_firmware_version(dev, fw_ver_new,
			fw_revctrl_new);

	if (upgrade > 0)
		return 1;

	return 0;
}

static void _cyttsp5_firmware_cont_builtin(const struct firmware *fw,
		void *context)
{
	struct device *dev = context;
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	int upgrade;

	if (!fw) {
		dev_info(dev, "%s: No builtin firmware\n", __func__);
		goto _cyttsp5_firmware_cont_builtin_exit;
	}

	if (!fw->data || !fw->size) {
		dev_err(dev, "%s: Invalid builtin firmware\n", __func__);
		goto _cyttsp5_firmware_cont_builtin_exit;
	}

	parade_debug(dev, DEBUG_LEVEL_1, "%s: Found firmware\n", __func__);

	upgrade = cyttsp5_check_firmware_version_builtin(dev, fw);
	if (upgrade) {
		_cyttsp5_firmware_cont(fw, dev);
		ld->builtin_bin_fw_status = 0;
		complete(&ld->builtin_bin_fw_complete);
		return;
	}

_cyttsp5_firmware_cont_builtin_exit:
	release_firmware(fw);

	ld->builtin_bin_fw_status = -EINVAL;
	complete(&ld->builtin_bin_fw_complete);
}

static int upgrade_firmware_from_class(struct device *dev)
{
	int retval;

	parade_debug(dev, DEBUG_LEVEL_2,
		"%s: Enabling firmware class loader\n", __func__);

	retval = request_firmware_nowait(THIS_MODULE, FW_ACTION_NOHOTPLUG,
			CY_FW_MANUAL_UPGRADE_FILE_NAME, dev, GFP_KERNEL, dev,
			_cyttsp5_firmware_cont);
	if (retval < 0) {
		dev_err(dev, "%s: Fail request firmware class file load\n",
			__func__);
		return retval;
	}

	return 0;
}

/*
 * Generates binary FW filename as following:
 * - Panel ID not enabled: cyttsp5_fw.bin
 * - Panel ID enabled: cyttsp5_fw_pidXX.bin
 */
static char *generate_firmware_filename(struct device *dev)
{
	char *filename;
	u8 panel_id;

#define FILENAME_LEN_MAX 64
	filename = kzalloc(FILENAME_LEN_MAX, GFP_KERNEL);
	if (!filename)
		return NULL;

	panel_id = cyttsp5_get_panel_id(dev);
	if (panel_id == PANEL_ID_NOT_ENABLED)
		snprintf(filename, FILENAME_LEN_MAX, "%s", CY_FW_FILE_NAME);
	else
		snprintf(filename, FILENAME_LEN_MAX, "%s_pid%02X%s",
			CY_FW_FILE_PREFIX, panel_id, CY_FW_FILE_SUFFIX);

	parade_debug(dev, DEBUG_LEVEL_1, "%s: Filename: %s\n",
		__func__, filename);

	return filename;
}

static int upgrade_firmware_from_builtin(struct device *dev)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	char *filename;
	int retval;

	parade_debug(dev, DEBUG_LEVEL_2,
		"%s: Enabling firmware class loader built-in\n",
		__func__);

	filename = generate_firmware_filename(dev);
	if (!filename)
		return -ENOMEM;

	retval = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
			filename, dev, GFP_KERNEL, dev,
			_cyttsp5_firmware_cont_builtin);
	if (retval < 0) {
		dev_err(dev, "%s: Fail request firmware class file load\n",
			__func__);
		goto exit;
	}

	/* wait until FW binary upgrade finishes */
	wait_for_completion(&ld->builtin_bin_fw_complete);

	retval = ld->builtin_bin_fw_status;

exit:
	kfree(filename);

	return retval;
}
#endif /* CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_BINARY_FW_UPGRADE */

#if CYTTSP5_TTCONFIG_UPGRADE
static int cyttsp5_write_config_row_(struct device *dev, u8 ebid,
		u16 row_number, u16 row_size, u8 *data)
{
	int rc;
	u16 actual_write_len;

	rc = cmd->nonhid_cmd->write_conf_block(dev, 0, row_number,
			row_size, ebid, data, (u8 *)cyttsp5_security_key,
			&actual_write_len);
	if (rc) {
		dev_err(dev, "%s: Fail Put EBID=%d row=%d cmd fail r=%d\n",
			__func__, ebid, row_number, rc);
		return rc;
	}

	if (actual_write_len != row_size) {
		dev_err(dev, "%s: Fail Put EBID=%d row=%d wrong write size=%d\n",
			__func__, ebid, row_number, actual_write_len);
		rc = -EINVAL;
	}

	return rc;
}

static int cyttsp5_upgrade_ttconfig(struct device *dev,
		const u8 *ttconfig_data, int ttconfig_size)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	bool wait_for_calibration_complete = false;
	u8 ebid = CY_TCH_PARM_EBID;
	u16 row_size = CY_DATA_ROW_SIZE;
	u16 table_size;
	u16 row_count;
	u16 residue;
	u8 *row_buf;
	u8 verify_crc_status;
	u16 calculated_crc;
	u16 stored_crc;
	int rc = 0;
	int i;

	table_size = ttconfig_size;
	row_count = table_size / row_size;
	row_buf = (u8 *)ttconfig_data;
	parade_debug(dev, DEBUG_LEVEL_1, "%s: size:%d row_size=%d row_count=%d\n",
		__func__, table_size, row_size, row_count);

	pm_runtime_get_sync(dev);

	rc = cmd->request_exclusive(dev, CY_LDR_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0)
		goto exit;

	rc = cmd->nonhid_cmd->suspend_scanning(dev, 0);
	if (rc < 0)
		goto release;

	for (i = 0; i < row_count; i++) {
		parade_debug(dev, DEBUG_LEVEL_1, "%s: row=%d size=%d\n",
			__func__, i, row_size);
		rc = cyttsp5_write_config_row_(dev, ebid, i, row_size,
				row_buf);
		if (rc) {
			dev_err(dev, "%s: Fail put row=%d r=%d\n",
				__func__, i, rc);
			break;
		}
		row_buf += row_size;
	}
	if (!rc) {
		residue = table_size % row_size;
		parade_debug(dev, DEBUG_LEVEL_1, "%s: row=%d size=%d\n",
			__func__, i, residue);
		rc = cyttsp5_write_config_row_(dev, ebid, i, residue,
				row_buf);
		row_count++;
		if (rc)
			dev_err(dev, "%s: Fail put row=%d r=%d\n",
				__func__, i, rc);
	}

	if (!rc)
		parade_debug(dev, DEBUG_LEVEL_1,
			"%s: TT_CFG updated: rows:%d bytes:%d\n",
			__func__, row_count, table_size);

	rc = cmd->nonhid_cmd->verify_config_block_crc(dev, 0, ebid,
			&verify_crc_status, &calculated_crc, &stored_crc);
	if (rc || verify_crc_status)
		dev_err(dev, "%s: CRC Failed, ebid=%d, status=%d, scrc=%X ccrc=%X\n",
			__func__, ebid, verify_crc_status,
			calculated_crc, stored_crc);
	else
		parade_debug(dev, DEBUG_LEVEL_1,
			"%s: CRC PASS, ebid=%d, status=%d, scrc=%X ccrc=%X\n",
			__func__, ebid, verify_crc_status,
			calculated_crc, stored_crc);

	rc = cmd->nonhid_cmd->resume_scanning(dev, 0);
	if (rc < 0)
		goto release;

	if (ld->loader_pdata &&
			(ld->loader_pdata->flags
			 & CY_LOADER_FLAG_CALIBRATE_AFTER_TTCONFIG_UPGRADE)) {
#if (KERNEL_VERSION(3, 13, 0) <= LINUX_VERSION_CODE)
		reinit_completion(&ld->calibration_complete);
#else
		INIT_COMPLETION(ld->calibration_complete);
#endif
		/* set up call back for startup */
		parade_debug(dev, DEBUG_LEVEL_2, "%s: Adding callback for calibration\n",
			__func__);
		rc = cmd->subscribe_attention(dev, CY_ATTEN_STARTUP,
			CYTTSP5_LOADER_NAME, cyttsp5_calibration_attention, 0);
		if (rc) {
			dev_err(dev, "%s: Failed adding callback for calibration\n",
				__func__);
			dev_err(dev, "%s: No calibration will be performed\n",
				__func__);
			rc = 0;
		} else
			wait_for_calibration_complete = true;
	}

release:
	cmd->release_exclusive(dev);

exit:
	if (!rc)
		cmd->request_restart(dev, true);

	pm_runtime_put_sync(dev);

	if (wait_for_calibration_complete)
		wait_for_completion(&ld->calibration_complete);

	return rc;
}
#endif /* CYTTSP5_TTCONFIG_UPGRADE */

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_TTCONFIG_UPGRADE
static int cyttsp5_get_ttconfig_crc(struct device *dev,
		const u8 *ttconfig_data, int ttconfig_size, u16 *crc)
{
	u16 crc_loc;

	crc_loc = get_unaligned_le16(&ttconfig_data[2]);
	if (ttconfig_size < crc_loc + 2)
		return -EINVAL;

	*crc = get_unaligned_le16(&ttconfig_data[crc_loc]);

	return 0;
}

static int cyttsp5_get_ttconfig_version(struct device *dev,
		const u8 *ttconfig_data, int ttconfig_size, u16 *version)
{
	if (ttconfig_size < CY_TTCONFIG_VERSION_OFFSET
			+ CY_TTCONFIG_VERSION_SIZE)
		return -EINVAL;

	*version = get_unaligned_le16(
		&ttconfig_data[CY_TTCONFIG_VERSION_OFFSET]);

	return 0;
}

static int cyttsp5_check_ttconfig_version(struct device *dev,
		const u8 *ttconfig_data, int ttconfig_size)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	u16 cfg_crc_new;
	int rc;

	if (!ld->si)
		return 0;

	/* Check for config version */
	if (ld->loader_pdata->flags &
			CY_LOADER_FLAG_CHECK_TTCONFIG_VERSION) {
		u16 cfg_ver_new;

		rc = cyttsp5_get_ttconfig_version(dev, ttconfig_data,
				ttconfig_size, &cfg_ver_new);
		if (rc)
			return 0;

		parade_debug(dev, DEBUG_LEVEL_1, "%s: img_ver:0x%04X new_ver:0x%04X\n",
			__func__, ld->si->cydata.fw_ver_conf, cfg_ver_new);

		/* Check if config version is newer */
		if (cfg_ver_new > ld->si->cydata.fw_ver_conf) {
			parade_debug(dev, DEBUG_LEVEL_1, "%s: Config version newer, will upgrade\n",
			__func__);
			return 1;
		}

		parade_debug(dev, DEBUG_LEVEL_1, "%s: Config version is identical or older, will NOT upgrade\n",
			__func__);
	/* Check for config CRC */
	} else {
		rc = cyttsp5_get_ttconfig_crc(dev, ttconfig_data,
				ttconfig_size, &cfg_crc_new);
		if (rc)
			return 0;

		parade_debug(dev, DEBUG_LEVEL_1, "%s: img_crc:0x%04X new_crc:0x%04X\n",
			__func__, ld->si->ttconfig.crc, cfg_crc_new);

		if (cfg_crc_new != ld->si->ttconfig.crc) {
			parade_debug(dev, DEBUG_LEVEL_1, "%s: Config CRC different, will upgrade\n",
				__func__);
			return 1;
		}

		parade_debug(dev, DEBUG_LEVEL_1, "%s: Config CRC equal, will NOT upgrade\n",
			__func__);
	}

	return 0;
}

static int cyttsp5_check_ttconfig_version_platform(struct device *dev,
		struct cyttsp5_touch_config *ttconfig)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	u32 fw_ver_config;
	u32 fw_revctrl_config;

	if (!ld->si) {
		dev_info(dev, "%s: No firmware infomation found, device FW may be corrupted\n",
			__func__);
		return 0;
	}

	fw_ver_config = get_unaligned_be16(ttconfig->fw_ver + 2);
	/* 4 middle bytes are not used */
	fw_revctrl_config = get_unaligned_be32(ttconfig->fw_ver + 8);

	/* FW versions should match */
	if (cyttsp5_check_firmware_version(dev, fw_ver_config,
			fw_revctrl_config)) {
		dev_err(dev, "%s: FW versions mismatch\n", __func__);
		return 0;
	}

	/* Check PowerOn Self Test, TT_CFG CRC bit */
	if ((ld->si->cydata.post_code & CY_POST_TT_CFG_CRC_MASK) == 0) {
		parade_debug(dev, DEBUG_LEVEL_1, "%s: POST, TT_CFG failed (%X), will upgrade\n",
			__func__, ld->si->cydata.post_code);
		return 1;
	}

	return cyttsp5_check_ttconfig_version(dev, ttconfig->param_regs->data,
			ttconfig->param_regs->size);
}

static struct cyttsp5_touch_config *cyttsp5_get_platform_ttconfig(
		struct device *dev)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	struct cyttsp5_touch_config **ttconfigs;
	struct cyttsp5_touch_config *ttconfig;
	u8 panel_id;

	panel_id = cyttsp5_get_panel_id(dev);
	if (panel_id == PANEL_ID_NOT_ENABLED) {
		/* TODO: Make debug message */
		dev_info(dev, "%s: Panel ID not enabled, using legacy ttconfig\n",
			__func__);
		return ld->loader_pdata->ttconfig;
	}

	ttconfigs = ld->loader_pdata->ttconfigs;
	if (!ttconfigs)
		return NULL;

	/* Find TT config according to the Panel ID */
	while ((ttconfig = *ttconfigs++)) {
		if (ttconfig->panel_id == panel_id) {
			/* TODO: Make debug message */
			dev_info(dev, "%s: Found matching ttconfig:%p with Panel ID: 0x%02X\n",
				__func__, ttconfig, ttconfig->panel_id);
			return ttconfig;
		}
		parade_debug(dev, DEBUG_LEVEL_2, "%s: Found mismatching ttconfig:%p with Panel ID: 0x%02X\n",
			__func__, ttconfig, ttconfig->panel_id);
	}

	return NULL;
}

static int upgrade_ttconfig_from_platform(struct device *dev)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	struct cyttsp5_touch_config *ttconfig;
	struct touch_settings *param_regs;
	struct cyttsp5_touch_fw;
	int rc = -ENODEV;
	int upgrade;

	if (!ld->loader_pdata) {
		dev_info(dev, "%s: No loader platform data\n", __func__);
		return rc;
	}

	ttconfig = cyttsp5_get_platform_ttconfig(dev);
	if (!ttconfig) {
		dev_info(dev, "%s: No ttconfig data\n", __func__);
		return rc;
	}

	param_regs = ttconfig->param_regs;
	if (!param_regs) {
		dev_info(dev, "%s: No touch parameters\n", __func__);
		return rc;
	}

	if (!param_regs->data || !param_regs->size) {
		dev_info(dev, "%s: Invalid touch parameters\n", __func__);
		return rc;
	}

	if (!ttconfig->fw_ver || !ttconfig->fw_vsize) {
		dev_info(dev, "%s: Invalid FW version for touch parameters\n",
			__func__);
		return rc;
	}

	upgrade = cyttsp5_check_ttconfig_version_platform(dev, ttconfig);
	if (upgrade)
		return cyttsp5_upgrade_ttconfig(dev, param_regs->data,
				param_regs->size);

	return rc;
}
#endif /* CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_TTCONFIG_UPGRADE */

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_MANUAL_TTCONFIG_UPGRADE
static ssize_t cyttsp5_config_data_write(struct file *filp,
		struct kobject *kobj, struct bin_attribute *bin_attr,
		char *buf, loff_t offset, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct cyttsp5_loader_data *data = cyttsp5_get_loader_data(dev);
	u8 *p;

	parade_debug(dev, DEBUG_LEVEL_2, "%s: offset:%lld count:%zu\n",
		__func__, offset, count);

	mutex_lock(&data->config_lock);

	if (!data->config_loading) {
		mutex_unlock(&data->config_lock);
		return -ENODEV;
	}

	p = krealloc(data->config_data, offset + count, GFP_KERNEL);
	if (!p) {
		kfree(data->config_data);
		data->config_data = NULL;
		mutex_unlock(&data->config_lock);
		return -ENOMEM;
	}
	data->config_data = p;

	memcpy(&data->config_data[offset], buf, count);
	data->config_size += count;

	mutex_unlock(&data->config_lock);

	return count;
}

static struct bin_attribute bin_attr_config_data = {
	.attr = {
		.name = "config_data",
		.mode = S_IWUSR,
	},
	.size = 0,
	.write = cyttsp5_config_data_write,
};

static ssize_t cyttsp5_config_loading_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	bool config_loading;

	mutex_lock(&ld->config_lock);
	config_loading = ld->config_loading;
	mutex_unlock(&ld->config_lock);

	return sprintf(buf, "%d\n", config_loading);
}

static int cyttsp5_verify_ttconfig_binary(struct device *dev,
		u8 *bin_config_data, int bin_config_size, u8 **start, int *len)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	int header_size;
	u16 config_size;
	u32 fw_ver_config;
	u32 fw_revctrl_config;

	if (!ld->si) {
		dev_err(dev, "%s: No firmware infomation found, device FW may be corrupted\n",
			__func__);
		return -ENODEV;
	}

	/*
	 * We need 11 bytes for FW version control info and at
	 * least 6 bytes in config (Length + Max Length + CRC)
	 */
	header_size = bin_config_data[0] + 1;
	if (header_size < 11 || header_size >= bin_config_size - 6) {
		dev_err(dev, "%s: Invalid header size %d\n", __func__,
			header_size);
		return -EINVAL;
	}

	fw_ver_config = get_unaligned_be16(&bin_config_data[1]);
	/* 4 middle bytes are not used */
	fw_revctrl_config = get_unaligned_be32(&bin_config_data[7]);

	/* FW versions should match */
	if (cyttsp5_check_firmware_version(dev, fw_ver_config,
			fw_revctrl_config)) {
		dev_err(dev, "%s: FW versions mismatch\n", __func__);
		return -EINVAL;
	}

	config_size = get_unaligned_le16(&bin_config_data[header_size]);
	/* Perform a simple size check (2 bytes for CRC) */
	if (config_size != bin_config_size - header_size - 2) {
		dev_err(dev, "%s: Config size invalid\n", __func__);
		return -EINVAL;
	}

	*start = &bin_config_data[header_size];
	*len = bin_config_size - header_size;

	return 0;
}

/*
 * 1: Start loading TT Config
 * 0: End loading TT Config and perform upgrade
 *-1: Exit loading
 */
static ssize_t cyttsp5_config_loading_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	long value;
	u8 *start;
	int length;
	int rc;

	rc = kstrtol(buf, 10, &value);
	if (rc < 0 || value < -1 || value > 1) {
		dev_err(dev, "%s: Invalid value\n", __func__);
		return size;
	}

	mutex_lock(&ld->config_lock);

	if (value == 1)
		ld->config_loading = true;
	else if (value == -1)
		ld->config_loading = false;
	else if (value == 0 && ld->config_loading) {
		ld->config_loading = false;
		if (ld->config_size == 0) {
			dev_err(dev, "%s: No config data\n", __func__);
			goto exit_free;
		}

		rc = cyttsp5_verify_ttconfig_binary(dev,
				ld->config_data, ld->config_size,
				&start, &length);
		if (rc)
			goto exit_free;

		rc = cyttsp5_upgrade_ttconfig(dev, start, length);
	}

exit_free:
	kfree(ld->config_data);
	ld->config_data = NULL;
	ld->config_size = 0;

	mutex_unlock(&ld->config_lock);

	if (rc)
		return rc;

	return size;
}

static DEVICE_ATTR(config_loading, S_IRUSR | S_IWUSR,
	cyttsp5_config_loading_show, cyttsp5_config_loading_store);
#endif /* CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_MANUAL_TTCONFIG_UPGRADE */

static void cyttsp5_fw_and_config_upgrade(
		struct work_struct *fw_and_config_upgrade)
{
	struct cyttsp5_loader_data *ld = container_of(fw_and_config_upgrade,
			struct cyttsp5_loader_data, fw_and_config_upgrade);
	struct device *dev = ld->dev;

	ld->si = cmd->request_sysinfo(dev);
	if (!ld->si)
		dev_err(dev, "%s: Fail get sysinfo pointer from core\n",
			__func__);
#if !CYTTSP5_FW_UPGRADE
	dev_info(dev, "%s: No FW upgrade method selected!\n", __func__);
#endif

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_FW_UPGRADE
	if (!upgrade_firmware_from_platform(dev, false))
		return;
#endif

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_BINARY_FW_UPGRADE
	if (!upgrade_firmware_from_builtin(dev))
		return;
#endif

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_TTCONFIG_UPGRADE
	if (!upgrade_ttconfig_from_platform(dev))
		return;
#endif
}

#if CYTTSP5_FW_UPGRADE
static int cyttsp5_fw_upgrade_cb(struct device *dev)
{
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_FW_UPGRADE
	if (!upgrade_firmware_from_platform(dev, false))
		return 1;
#endif

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_BINARY_FW_UPGRADE
	if (!upgrade_firmware_from_builtin(dev))
		return 1;
#endif
	return 0;
}
#endif

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_FW_UPGRADE
static ssize_t cyttsp5_forced_upgrade_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int rc = upgrade_firmware_from_platform(dev, true);

	if (rc)
		return rc;
	return size;
}

static DEVICE_ATTR(forced_upgrade, S_IWUSR,
	NULL, cyttsp5_forced_upgrade_store);
#endif

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_BINARY_FW_UPGRADE
static ssize_t cyttsp5_manual_upgrade_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp5_loader_data *ld = cyttsp5_get_loader_data(dev);
	int rc;

	if (ld->is_manual_upgrade_enabled)
		return -EBUSY;

	ld->is_manual_upgrade_enabled = 1;

	rc = upgrade_firmware_from_class(ld->dev);

	if (rc < 0)
		ld->is_manual_upgrade_enabled = 0;

	return size;
}

static DEVICE_ATTR(manual_upgrade, S_IWUSR,
	NULL, cyttsp5_manual_upgrade_store);
#endif

static int cyttsp5_loader_probe(struct device *dev, void **data)
{
	struct cyttsp5_loader_data *ld;
	struct cyttsp5_platform_data *pdata = dev_get_platdata(dev);
	int rc;

	if (!pdata || !pdata->loader_pdata) {
		dev_err(dev, "%s: Missing platform data\n", __func__);
		rc = -ENODEV;
		goto error_no_pdata;
	}

	ld = kzalloc(sizeof(*ld), GFP_KERNEL);
	if (!ld) {
		rc = -ENOMEM;
		goto error_alloc_data_failed;
	}

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_FW_UPGRADE
	rc = device_create_file(dev, &dev_attr_forced_upgrade);
	if (rc) {
		dev_err(dev, "%s: Error, could not create forced_upgrade\n",
				__func__);
		goto error_create_forced_upgrade;
	}
#endif

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_BINARY_FW_UPGRADE
	rc = device_create_file(dev, &dev_attr_manual_upgrade);
	if (rc) {
		dev_err(dev, "%s: Error, could not create manual_upgrade\n",
				__func__);
		goto error_create_manual_upgrade;
	}
#endif

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_MANUAL_TTCONFIG_UPGRADE
	rc = device_create_file(dev, &dev_attr_config_loading);
	if (rc) {
		dev_err(dev, "%s: Error, could not create config_loading\n",
				__func__);
		goto error_create_config_loading;
	}

	rc = device_create_bin_file(dev, &bin_attr_config_data);
	if (rc) {
		dev_err(dev, "%s: Error, could not create config_data\n",
				__func__);
		goto error_create_config_data;
	}
#endif

	ld->loader_pdata = pdata->loader_pdata;
	ld->dev = dev;
	*data = ld;

#if CYTTSP5_FW_UPGRADE
	init_completion(&ld->int_running);
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_BINARY_FW_UPGRADE
	init_completion(&ld->builtin_bin_fw_complete);
#endif
	cmd->subscribe_attention(dev, CY_ATTEN_IRQ, CYTTSP5_LOADER_NAME,
		cyttsp5_loader_attention, CY_MODE_BOOTLOADER);

	cmd->subscribe_attention(dev, CY_ATTEN_LOADER, CYTTSP5_LOADER_NAME,
		cyttsp5_fw_upgrade_cb, CY_MODE_UNKNOWN);
#endif
#if CYTTSP5_FW_UPGRADE || CYTTSP5_TTCONFIG_UPGRADE
	init_completion(&ld->calibration_complete);
	INIT_WORK(&ld->calibration_work, cyttsp5_calibrate_idacs);
#endif
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_MANUAL_TTCONFIG_UPGRADE
	mutex_init(&ld->config_lock);
#endif

#ifdef UPGRADE_FW_AND_CONFIG_IN_PROBE
	/* Call FW and config upgrade directly in probe */
	cyttsp5_fw_and_config_upgrade(&ld->fw_and_config_upgrade);
#else
	INIT_WORK(&ld->fw_and_config_upgrade, cyttsp5_fw_and_config_upgrade);
	schedule_work(&ld->fw_and_config_upgrade);
#endif

	dev_info(dev, "%s: Successful probe %s\n", __func__, dev_name(dev));
	return 0;

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_MANUAL_TTCONFIG_UPGRADE
error_create_config_data:
	device_remove_file(dev, &dev_attr_config_loading);
error_create_config_loading:
#endif
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_BINARY_FW_UPGRADE
	device_remove_file(dev, &dev_attr_manual_upgrade);
error_create_manual_upgrade:
#endif
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_FW_UPGRADE
	device_remove_file(dev, &dev_attr_forced_upgrade);
error_create_forced_upgrade:
#endif
	kfree(ld);
error_alloc_data_failed:
error_no_pdata:
	dev_err(dev, "%s failed.\n", __func__);
	return rc;
}

static void cyttsp5_loader_release(struct device *dev, void *data)
{
	struct cyttsp5_loader_data *ld = (struct cyttsp5_loader_data *)data;

#if CYTTSP5_FW_UPGRADE
	cmd->unsubscribe_attention(dev, CY_ATTEN_IRQ, CYTTSP5_LOADER_NAME,
		cyttsp5_loader_attention, CY_MODE_BOOTLOADER);

	cmd->unsubscribe_attention(dev, CY_ATTEN_LOADER, CYTTSP5_LOADER_NAME,
		cyttsp5_fw_upgrade_cb, CY_MODE_UNKNOWN);
#endif
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_MANUAL_TTCONFIG_UPGRADE
	device_remove_bin_file(dev, &bin_attr_config_data);
	device_remove_file(dev, &dev_attr_config_loading);
	kfree(ld->config_data);
#endif
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_BINARY_FW_UPGRADE
	device_remove_file(dev, &dev_attr_manual_upgrade);
#endif
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_PLATFORM_FW_UPGRADE
	device_remove_file(dev, &dev_attr_forced_upgrade);
#endif
	kfree(ld);
}

static struct cyttsp5_module loader_module = {
	.name = CYTTSP5_LOADER_NAME,
	.probe = cyttsp5_loader_probe,
	.release = cyttsp5_loader_release,
};

static int __init cyttsp5_loader_init(void)
{
	int rc;

	cmd = cyttsp5_get_commands();
	if (!cmd)
		return -EINVAL;

	rc = cyttsp5_register_module(&loader_module);
	if (rc < 0) {
		pr_err("%s: Error, failed registering module\n",
			__func__);
			return rc;
	}

	pr_info("%s: Parade TTSP FW Loader Driver (Built %s) rc=%d\n",
		 __func__, CY_DRIVER_VERSION, rc);
	return 0;
}
module_init(cyttsp5_loader_init);

static void __exit cyttsp5_loader_exit(void)
{
	cyttsp5_unregister_module(&loader_module);
}
module_exit(cyttsp5_loader_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Parade TrueTouch(R) Standard Product FW Loader Driver");
MODULE_AUTHOR("Parade Technologies <ttdrivers@paradetech.com>");
