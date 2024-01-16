// SPDX-License-Identifier: GPL-2.0-only
/*
 * Goodix Touchscreen firmware upload support
 *
 * Copyright (c) 2021 Hans de Goede <hdegoede@redhat.com>
 *
 * This is a rewrite of gt9xx_update.c from the Allwinner H3 BSP which is:
 * Copyright (c) 2010 - 2012 Goodix Technology.
 * Author: andrew@goodix.com
 */

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include "goodix.h"

#define GOODIX_FW_HEADER_LENGTH		sizeof(struct goodix_fw_header)
#define GOODIX_FW_SECTION_LENGTH	0x2000
#define GOODIX_FW_DSP_LENGTH		0x1000
#define GOODIX_FW_UPLOAD_ADDRESS	0xc000

#define GOODIX_CFG_LOC_HAVE_KEY		 7
#define GOODIX_CFG_LOC_DRVA_NUM		27
#define GOODIX_CFG_LOC_DRVB_NUM		28
#define GOODIX_CFG_LOC_SENS_NUM		29

struct goodix_fw_header {
	u8 hw_info[4];
	u8 pid[8];
	u8 vid[2];
} __packed;

static u16 goodix_firmware_checksum(const u8 *data, int size)
{
	u16 checksum = 0;
	int i;

	for (i = 0; i < size; i += 2)
		checksum += (data[i] << 8) + data[i + 1];

	return checksum;
}

static int goodix_firmware_verify(struct device *dev, const struct firmware *fw)
{
	const struct goodix_fw_header *fw_header;
	size_t expected_size;
	const u8 *data;
	u16 checksum;
	char buf[9];

	expected_size = GOODIX_FW_HEADER_LENGTH + 4 * GOODIX_FW_SECTION_LENGTH +
			GOODIX_FW_DSP_LENGTH;
	if (fw->size != expected_size) {
		dev_err(dev, "Firmware has wrong size, expected %zu got %zu\n",
			expected_size, fw->size);
		return -EINVAL;
	}

	data = fw->data + GOODIX_FW_HEADER_LENGTH;
	checksum = goodix_firmware_checksum(data, 4 * GOODIX_FW_SECTION_LENGTH);
	if (checksum) {
		dev_err(dev, "Main firmware checksum error\n");
		return -EINVAL;
	}

	data += 4 * GOODIX_FW_SECTION_LENGTH;
	checksum = goodix_firmware_checksum(data, GOODIX_FW_DSP_LENGTH);
	if (checksum) {
		dev_err(dev, "DSP firmware checksum error\n");
		return -EINVAL;
	}

	fw_header = (const struct goodix_fw_header *)fw->data;
	dev_info(dev, "Firmware hardware info %02x%02x%02x%02x\n",
		 fw_header->hw_info[0], fw_header->hw_info[1],
		 fw_header->hw_info[2], fw_header->hw_info[3]);
	/* pid is a 8 byte buffer containing a string, weird I know */
	memcpy(buf, fw_header->pid, 8);
	buf[8] = 0;
	dev_info(dev, "Firmware PID: %s VID: %02x%02x\n", buf,
		 fw_header->vid[0], fw_header->vid[1]);
	return 0;
}

static int goodix_enter_upload_mode(struct i2c_client *client)
{
	int tries, error;
	u8 val;

	tries = 200;
	do {
		error = goodix_i2c_write_u8(client,
					    GOODIX_REG_MISCTL_SWRST, 0x0c);
		if (error)
			return error;

		error = goodix_i2c_read(client,
					GOODIX_REG_MISCTL_SWRST, &val, 1);
		if (error)
			return error;

		if (val == 0x0c)
			break;
	} while (--tries);

	if (!tries) {
		dev_err(&client->dev, "Error could not hold ss51 & dsp\n");
		return -EIO;
	}

	/* DSP_CK and DSP_ALU_CK PowerOn */
	error = goodix_i2c_write_u8(client, GOODIX_REG_MISCTL_DSP_CTL, 0x00);
	if (error)
		return error;

	/* Disable watchdog */
	error = goodix_i2c_write_u8(client, GOODIX_REG_MISCTL_TMR0_EN, 0x00);
	if (error)
		return error;

	/* Clear cache enable */
	error = goodix_i2c_write_u8(client, GOODIX_REG_MISCTL_CACHE_EN, 0x00);
	if (error)
		return error;

	/* Set boot from SRAM */
	error = goodix_i2c_write_u8(client, GOODIX_REG_MISCTL_BOOTCTL, 0x02);
	if (error)
		return error;

	/* Software reboot */
	error = goodix_i2c_write_u8(client,
				    GOODIX_REG_MISCTL_CPU_SWRST_PULSE, 0x01);
	if (error)
		return error;

	/* Clear control flag */
	error = goodix_i2c_write_u8(client, GOODIX_REG_MISCTL_BOOTCTL, 0x00);
	if (error)
		return error;

	/* Set scramble */
	error = goodix_i2c_write_u8(client, GOODIX_REG_MISCTL_BOOT_OPT, 0x00);
	if (error)
		return error;

	/* Enable accessing code */
	error = goodix_i2c_write_u8(client, GOODIX_REG_MISCTL_MEM_CD_EN, 0x01);
	if (error)
		return error;

	return 0;
}

static int goodix_start_firmware(struct i2c_client *client)
{
	int error;
	u8 val;

	/* Init software watchdog */
	error = goodix_i2c_write_u8(client, GOODIX_REG_SW_WDT, 0xaa);
	if (error)
		return error;

	/* Release SS51 & DSP */
	error = goodix_i2c_write_u8(client, GOODIX_REG_MISCTL_SWRST, 0x00);
	if (error)
		return error;

	error = goodix_i2c_read(client, GOODIX_REG_SW_WDT, &val, 1);
	if (error)
		return error;

	/* The value we've written to SW_WDT should have been cleared now */
	if (val == 0xaa) {
		dev_err(&client->dev, "Error SW_WDT reg not cleared on fw startup\n");
		return -EIO;
	}

	/* Re-init software watchdog */
	error = goodix_i2c_write_u8(client, GOODIX_REG_SW_WDT, 0xaa);
	if (error)
		return error;

	return 0;
}

static int goodix_firmware_upload(struct goodix_ts_data *ts)
{
	const struct firmware *fw;
	char fw_name[64];
	const u8 *data;
	int error;

	snprintf(fw_name, sizeof(fw_name), "goodix/%s", ts->firmware_name);

	error = request_firmware(&fw, fw_name, &ts->client->dev);
	if (error) {
		dev_err(&ts->client->dev, "Firmware request error %d\n", error);
		return error;
	}

	error = goodix_firmware_verify(&ts->client->dev, fw);
	if (error)
		goto release;

	error = goodix_reset_no_int_sync(ts);
	if (error)
		goto release;

	error = goodix_enter_upload_mode(ts->client);
	if (error)
		goto release;

	/* Select SRAM bank 0 and upload section 1 & 2 */
	error = goodix_i2c_write_u8(ts->client,
				    GOODIX_REG_MISCTL_SRAM_BANK, 0x00);
	if (error)
		goto release;

	data = fw->data + GOODIX_FW_HEADER_LENGTH;
	error = goodix_i2c_write(ts->client, GOODIX_FW_UPLOAD_ADDRESS,
				 data, 2 * GOODIX_FW_SECTION_LENGTH);
	if (error)
		goto release;

	/* Select SRAM bank 1 and upload section 3 & 4 */
	error = goodix_i2c_write_u8(ts->client,
				    GOODIX_REG_MISCTL_SRAM_BANK, 0x01);
	if (error)
		goto release;

	data += 2 * GOODIX_FW_SECTION_LENGTH;
	error = goodix_i2c_write(ts->client, GOODIX_FW_UPLOAD_ADDRESS,
				 data, 2 * GOODIX_FW_SECTION_LENGTH);
	if (error)
		goto release;

	/* Select SRAM bank 2 and upload the DSP firmware */
	error = goodix_i2c_write_u8(ts->client,
				    GOODIX_REG_MISCTL_SRAM_BANK, 0x02);
	if (error)
		goto release;

	data += 2 * GOODIX_FW_SECTION_LENGTH;
	error = goodix_i2c_write(ts->client, GOODIX_FW_UPLOAD_ADDRESS,
				 data, GOODIX_FW_DSP_LENGTH);
	if (error)
		goto release;

	error = goodix_start_firmware(ts->client);
	if (error)
		goto release;

	error = goodix_int_sync(ts);
release:
	release_firmware(fw);
	return error;
}

static int goodix_prepare_bak_ref(struct goodix_ts_data *ts)
{
	u8 have_key, driver_num, sensor_num;

	if (ts->bak_ref)
		return 0; /* Already done */

	have_key = (ts->config[GOODIX_CFG_LOC_HAVE_KEY] & 0x01);

	driver_num = (ts->config[GOODIX_CFG_LOC_DRVA_NUM] & 0x1f) +
		     (ts->config[GOODIX_CFG_LOC_DRVB_NUM] & 0x1f);
	if (have_key)
		driver_num--;

	sensor_num = (ts->config[GOODIX_CFG_LOC_SENS_NUM] & 0x0f) +
		     ((ts->config[GOODIX_CFG_LOC_SENS_NUM] >> 4) & 0x0f);

	dev_dbg(&ts->client->dev, "Drv %d Sen %d Key %d\n",
		driver_num, sensor_num, have_key);

	ts->bak_ref_len = (driver_num * (sensor_num - 2) + 2) * 2;

	ts->bak_ref = devm_kzalloc(&ts->client->dev,
				   ts->bak_ref_len, GFP_KERNEL);
	if (!ts->bak_ref)
		return -ENOMEM;

	/*
	 * The bak_ref array contains the backup of an array of (self/auto)
	 * calibration related values which the Android version of the driver
	 * stores on the filesystem so that it can be restored after reboot.
	 * The mainline kernel never writes directly to the filesystem like
	 * this, we always start will all the values which give a correction
	 * factor in approx. the -20 - +20 range (in 2s complement) set to 0.
	 *
	 * Note the touchscreen works fine without restoring the reference
	 * values after a reboot / power-cycle.
	 *
	 * The last 2 bytes are a 16 bits unsigned checksum which is expected
	 * to make the addition al all 16 bit unsigned values in the array add
	 * up to 1 (rather then the usual 0), so we must set the last byte to 1.
	 */
	ts->bak_ref[ts->bak_ref_len - 1] = 1;

	return 0;
}

static int goodix_send_main_clock(struct goodix_ts_data *ts)
{
	u32 main_clk = 54; /* Default main clock */
	u8 checksum = 0;
	int i;

	device_property_read_u32(&ts->client->dev,
				 "goodix,main-clk", &main_clk);

	for (i = 0; i < (GOODIX_MAIN_CLK_LEN - 1); i++) {
		ts->main_clk[i] = main_clk;
		checksum += main_clk;
	}

	/* The value of all bytes combines must be 0 */
	ts->main_clk[GOODIX_MAIN_CLK_LEN - 1] = 256 - checksum;

	return goodix_i2c_write(ts->client, GOODIX_REG_MAIN_CLK,
				ts->main_clk, GOODIX_MAIN_CLK_LEN);
}

int goodix_firmware_check(struct goodix_ts_data *ts)
{
	device_property_read_string(&ts->client->dev,
				    "firmware-name", &ts->firmware_name);
	if (!ts->firmware_name)
		return 0;

	if (ts->irq_pin_access_method == IRQ_PIN_ACCESS_NONE) {
		dev_err(&ts->client->dev, "Error no IRQ-pin access method, cannot upload fw.\n");
		return -EINVAL;
	}

	dev_info(&ts->client->dev, "Touchscreen controller needs fw-upload\n");
	ts->load_cfg_from_disk = true;

	return goodix_firmware_upload(ts);
}

bool goodix_handle_fw_request(struct goodix_ts_data *ts)
{
	int error;
	u8 val;

	error = goodix_i2c_read(ts->client, GOODIX_REG_REQUEST, &val, 1);
	if (error)
		return false;

	switch (val) {
	case GOODIX_RQST_RESPONDED:
		/*
		 * If we read back our own last ack the IRQ was not for
		 * a request.
		 */
		return false;
	case GOODIX_RQST_CONFIG:
		error = goodix_send_cfg(ts, ts->config, ts->chip->config_len);
		if (error)
			return false;

		break;
	case GOODIX_RQST_BAK_REF:
		error = goodix_prepare_bak_ref(ts);
		if (error)
			return false;

		error = goodix_i2c_write(ts->client, GOODIX_REG_BAK_REF,
					 ts->bak_ref, ts->bak_ref_len);
		if (error)
			return false;

		break;
	case GOODIX_RQST_RESET:
		error = goodix_firmware_upload(ts);
		if (error)
			return false;

		break;
	case GOODIX_RQST_MAIN_CLOCK:
		error = goodix_send_main_clock(ts);
		if (error)
			return false;

		break;
	case GOODIX_RQST_UNKNOWN:
	case GOODIX_RQST_IDLE:
		break;
	default:
		dev_err_ratelimited(&ts->client->dev, "Unknown Request: 0x%02x\n", val);
	}

	/* Ack the request */
	goodix_i2c_write_u8(ts->client,
			    GOODIX_REG_REQUEST, GOODIX_RQST_RESPONDED);
	return true;
}

void goodix_save_bak_ref(struct goodix_ts_data *ts)
{
	int error;
	u8 val;

	if (!ts->firmware_name)
		return;

	error = goodix_i2c_read(ts->client, GOODIX_REG_STATUS, &val, 1);
	if (error)
		return;

	if (!(val & 0x80))
		return;

	error = goodix_i2c_read(ts->client, GOODIX_REG_BAK_REF,
				ts->bak_ref, ts->bak_ref_len);
	if (error) {
		memset(ts->bak_ref, 0, ts->bak_ref_len);
		ts->bak_ref[ts->bak_ref_len - 1] = 1;
	}
}
