/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/of.h>

#include "core.h"
#include "mac.h"
#include "htc.h"
#include "hif.h"
#include "wmi.h"
#include "bmi.h"
#include "debug.h"
#include "htt.h"
#include "testmode.h"

unsigned int ath10k_debug_mask;
static bool uart_print;
static unsigned int ath10k_p2p;
static bool skip_otp;

module_param_named(debug_mask, ath10k_debug_mask, uint, 0644);
module_param(uart_print, bool, 0644);
module_param_named(p2p, ath10k_p2p, uint, 0644);
module_param(skip_otp, bool, 0644);

MODULE_PARM_DESC(debug_mask, "Debugging mask");
MODULE_PARM_DESC(uart_print, "Uart target debugging");
MODULE_PARM_DESC(p2p, "Enable ath10k P2P support");
MODULE_PARM_DESC(skip_otp, "Skip otp failure for calibration in testmode");

static const struct ath10k_hw_params ath10k_hw_params_list[] = {
	{
		.id = QCA988X_HW_2_0_VERSION,
		.name = "qca988x hw2.0",
		.patch_load_addr = QCA988X_HW_2_0_PATCH_LOAD_ADDR,
		.uart_pin = 7,
		.fw = {
			.dir = QCA988X_HW_2_0_FW_DIR,
			.fw = QCA988X_HW_2_0_FW_FILE,
			.otp = QCA988X_HW_2_0_OTP_FILE,
			.board = QCA988X_HW_2_0_BOARD_DATA_FILE,
			.board_size = QCA988X_BOARD_DATA_SZ,
			.board_ext_size = QCA988X_BOARD_EXT_DATA_SZ,
		},
	},
};

static void ath10k_send_suspend_complete(struct ath10k *ar)
{
	ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot suspend complete\n");

	complete(&ar->target_suspend);
}

static int ath10k_init_configure_target(struct ath10k *ar)
{
	u32 param_host;
	int ret;

	/* tell target which HTC version it is used*/
	ret = ath10k_bmi_write32(ar, hi_app_host_interest,
				 HTC_PROTOCOL_VERSION);
	if (ret) {
		ath10k_err(ar, "settings HTC version failed\n");
		return ret;
	}

	/* set the firmware mode to STA/IBSS/AP */
	ret = ath10k_bmi_read32(ar, hi_option_flag, &param_host);
	if (ret) {
		ath10k_err(ar, "setting firmware mode (1/2) failed\n");
		return ret;
	}

	/* TODO following parameters need to be re-visited. */
	/* num_device */
	param_host |= (1 << HI_OPTION_NUM_DEV_SHIFT);
	/* Firmware mode */
	/* FIXME: Why FW_MODE_AP ??.*/
	param_host |= (HI_OPTION_FW_MODE_AP << HI_OPTION_FW_MODE_SHIFT);
	/* mac_addr_method */
	param_host |= (1 << HI_OPTION_MAC_ADDR_METHOD_SHIFT);
	/* firmware_bridge */
	param_host |= (0 << HI_OPTION_FW_BRIDGE_SHIFT);
	/* fwsubmode */
	param_host |= (0 << HI_OPTION_FW_SUBMODE_SHIFT);

	ret = ath10k_bmi_write32(ar, hi_option_flag, param_host);
	if (ret) {
		ath10k_err(ar, "setting firmware mode (2/2) failed\n");
		return ret;
	}

	/* We do all byte-swapping on the host */
	ret = ath10k_bmi_write32(ar, hi_be, 0);
	if (ret) {
		ath10k_err(ar, "setting host CPU BE mode failed\n");
		return ret;
	}

	/* FW descriptor/Data swap flags */
	ret = ath10k_bmi_write32(ar, hi_fw_swap, 0);

	if (ret) {
		ath10k_err(ar, "setting FW data/desc swap flags failed\n");
		return ret;
	}

	return 0;
}

static const struct firmware *ath10k_fetch_fw_file(struct ath10k *ar,
						   const char *dir,
						   const char *file)
{
	char filename[100];
	const struct firmware *fw;
	int ret;

	if (file == NULL)
		return ERR_PTR(-ENOENT);

	if (dir == NULL)
		dir = ".";

	snprintf(filename, sizeof(filename), "%s/%s", dir, file);
	ret = request_firmware(&fw, filename, ar->dev);
	if (ret)
		return ERR_PTR(ret);

	return fw;
}

static int ath10k_push_board_ext_data(struct ath10k *ar, const void *data,
				      size_t data_len)
{
	u32 board_data_size = ar->hw_params.fw.board_size;
	u32 board_ext_data_size = ar->hw_params.fw.board_ext_size;
	u32 board_ext_data_addr;
	int ret;

	ret = ath10k_bmi_read32(ar, hi_board_ext_data, &board_ext_data_addr);
	if (ret) {
		ath10k_err(ar, "could not read board ext data addr (%d)\n",
			   ret);
		return ret;
	}

	ath10k_dbg(ar, ATH10K_DBG_BOOT,
		   "boot push board extended data addr 0x%x\n",
		   board_ext_data_addr);

	if (board_ext_data_addr == 0)
		return 0;

	if (data_len != (board_data_size + board_ext_data_size)) {
		ath10k_err(ar, "invalid board (ext) data sizes %zu != %d+%d\n",
			   data_len, board_data_size, board_ext_data_size);
		return -EINVAL;
	}

	ret = ath10k_bmi_write_memory(ar, board_ext_data_addr,
				      data + board_data_size,
				      board_ext_data_size);
	if (ret) {
		ath10k_err(ar, "could not write board ext data (%d)\n", ret);
		return ret;
	}

	ret = ath10k_bmi_write32(ar, hi_board_ext_data_config,
				 (board_ext_data_size << 16) | 1);
	if (ret) {
		ath10k_err(ar, "could not write board ext data bit (%d)\n",
			   ret);
		return ret;
	}

	return 0;
}

static int ath10k_download_board_data(struct ath10k *ar, const void *data,
				      size_t data_len)
{
	u32 board_data_size = ar->hw_params.fw.board_size;
	u32 address;
	int ret;

	ret = ath10k_push_board_ext_data(ar, data, data_len);
	if (ret) {
		ath10k_err(ar, "could not push board ext data (%d)\n", ret);
		goto exit;
	}

	ret = ath10k_bmi_read32(ar, hi_board_data, &address);
	if (ret) {
		ath10k_err(ar, "could not read board data addr (%d)\n", ret);
		goto exit;
	}

	ret = ath10k_bmi_write_memory(ar, address, data,
				      min_t(u32, board_data_size,
					    data_len));
	if (ret) {
		ath10k_err(ar, "could not write board data (%d)\n", ret);
		goto exit;
	}

	ret = ath10k_bmi_write32(ar, hi_board_data_initialized, 1);
	if (ret) {
		ath10k_err(ar, "could not write board data bit (%d)\n", ret);
		goto exit;
	}

exit:
	return ret;
}

static int ath10k_download_cal_file(struct ath10k *ar)
{
	int ret;

	if (!ar->cal_file)
		return -ENOENT;

	if (IS_ERR(ar->cal_file))
		return PTR_ERR(ar->cal_file);

	ret = ath10k_download_board_data(ar, ar->cal_file->data,
					 ar->cal_file->size);
	if (ret) {
		ath10k_err(ar, "failed to download cal_file data: %d\n", ret);
		return ret;
	}

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot cal file downloaded\n");

	return 0;
}

static int ath10k_download_cal_dt(struct ath10k *ar)
{
	struct device_node *node;
	int data_len;
	void *data;
	int ret;

	node = ar->dev->of_node;
	if (!node)
		/* Device Tree is optional, don't print any warnings if
		 * there's no node for ath10k.
		 */
		return -ENOENT;

	if (!of_get_property(node, "qcom,ath10k-calibration-data",
			     &data_len)) {
		/* The calibration data node is optional */
		return -ENOENT;
	}

	if (data_len != QCA988X_CAL_DATA_LEN) {
		ath10k_warn(ar, "invalid calibration data length in DT: %d\n",
			    data_len);
		ret = -EMSGSIZE;
		goto out;
	}

	data = kmalloc(data_len, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto out;
	}

	ret = of_property_read_u8_array(node, "qcom,ath10k-calibration-data",
					data, data_len);
	if (ret) {
		ath10k_warn(ar, "failed to read calibration data from DT: %d\n",
			    ret);
		goto out_free;
	}

	ret = ath10k_download_board_data(ar, data, data_len);
	if (ret) {
		ath10k_warn(ar, "failed to download calibration data from Device Tree: %d\n",
			    ret);
		goto out_free;
	}

	ret = 0;

out_free:
	kfree(data);

out:
	return ret;
}

static int ath10k_download_and_run_otp(struct ath10k *ar)
{
	u32 result, address = ar->hw_params.patch_load_addr;
	int ret;

	ret = ath10k_download_board_data(ar, ar->board_data, ar->board_len);
	if (ret) {
		ath10k_err(ar, "failed to download board data: %d\n", ret);
		return ret;
	}

	/* OTP is optional */

	if (!ar->otp_data || !ar->otp_len) {
		ath10k_warn(ar, "Not running otp, calibration will be incorrect (otp-data %p otp_len %zd)!\n",
			    ar->otp_data, ar->otp_len);
		return 0;
	}

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot upload otp to 0x%x len %zd\n",
		   address, ar->otp_len);

	ret = ath10k_bmi_fast_download(ar, address, ar->otp_data, ar->otp_len);
	if (ret) {
		ath10k_err(ar, "could not write otp (%d)\n", ret);
		return ret;
	}

	ret = ath10k_bmi_execute(ar, address, 0, &result);
	if (ret) {
		ath10k_err(ar, "could not execute otp (%d)\n", ret);
		return ret;
	}

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot otp execute result %d\n", result);

	if (!skip_otp && result != 0) {
		ath10k_err(ar, "otp calibration failed: %d", result);
		return -EINVAL;
	}

	return 0;
}

static int ath10k_download_fw(struct ath10k *ar, enum ath10k_firmware_mode mode)
{
	u32 address, data_len;
	const char *mode_name;
	const void *data;
	int ret;

	address = ar->hw_params.patch_load_addr;

	switch (mode) {
	case ATH10K_FIRMWARE_MODE_NORMAL:
		data = ar->firmware_data;
		data_len = ar->firmware_len;
		mode_name = "normal";
		break;
	case ATH10K_FIRMWARE_MODE_UTF:
		data = ar->testmode.utf->data;
		data_len = ar->testmode.utf->size;
		mode_name = "utf";
		break;
	default:
		ath10k_err(ar, "unknown firmware mode: %d\n", mode);
		return -EINVAL;
	}

	ath10k_dbg(ar, ATH10K_DBG_BOOT,
		   "boot uploading firmware image %p len %d mode %s\n",
		   data, data_len, mode_name);

	ret = ath10k_bmi_fast_download(ar, address, data, data_len);
	if (ret) {
		ath10k_err(ar, "failed to download %s firmware: %d\n",
			   mode_name, ret);
		return ret;
	}

	return ret;
}

static void ath10k_core_free_firmware_files(struct ath10k *ar)
{
	if (ar->board && !IS_ERR(ar->board))
		release_firmware(ar->board);

	if (ar->otp && !IS_ERR(ar->otp))
		release_firmware(ar->otp);

	if (ar->firmware && !IS_ERR(ar->firmware))
		release_firmware(ar->firmware);

	if (ar->cal_file && !IS_ERR(ar->cal_file))
		release_firmware(ar->cal_file);

	ar->board = NULL;
	ar->board_data = NULL;
	ar->board_len = 0;

	ar->otp = NULL;
	ar->otp_data = NULL;
	ar->otp_len = 0;

	ar->firmware = NULL;
	ar->firmware_data = NULL;
	ar->firmware_len = 0;

	ar->cal_file = NULL;
}

static int ath10k_fetch_cal_file(struct ath10k *ar)
{
	char filename[100];

	/* cal-<bus>-<id>.bin */
	scnprintf(filename, sizeof(filename), "cal-%s-%s.bin",
		  ath10k_bus_str(ar->hif.bus), dev_name(ar->dev));

	ar->cal_file = ath10k_fetch_fw_file(ar, ATH10K_FW_DIR, filename);
	if (IS_ERR(ar->cal_file))
		/* calibration file is optional, don't print any warnings */
		return PTR_ERR(ar->cal_file);

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "found calibration file %s/%s\n",
		   ATH10K_FW_DIR, filename);

	return 0;
}

static int ath10k_core_fetch_firmware_api_1(struct ath10k *ar)
{
	int ret = 0;

	if (ar->hw_params.fw.fw == NULL) {
		ath10k_err(ar, "firmware file not defined\n");
		return -EINVAL;
	}

	if (ar->hw_params.fw.board == NULL) {
		ath10k_err(ar, "board data file not defined");
		return -EINVAL;
	}

	ar->board = ath10k_fetch_fw_file(ar,
					 ar->hw_params.fw.dir,
					 ar->hw_params.fw.board);
	if (IS_ERR(ar->board)) {
		ret = PTR_ERR(ar->board);
		ath10k_err(ar, "could not fetch board data (%d)\n", ret);
		goto err;
	}

	ar->board_data = ar->board->data;
	ar->board_len = ar->board->size;

	ar->firmware = ath10k_fetch_fw_file(ar,
					    ar->hw_params.fw.dir,
					    ar->hw_params.fw.fw);
	if (IS_ERR(ar->firmware)) {
		ret = PTR_ERR(ar->firmware);
		ath10k_err(ar, "could not fetch firmware (%d)\n", ret);
		goto err;
	}

	ar->firmware_data = ar->firmware->data;
	ar->firmware_len = ar->firmware->size;

	/* OTP may be undefined. If so, don't fetch it at all */
	if (ar->hw_params.fw.otp == NULL)
		return 0;

	ar->otp = ath10k_fetch_fw_file(ar,
				       ar->hw_params.fw.dir,
				       ar->hw_params.fw.otp);
	if (IS_ERR(ar->otp)) {
		ret = PTR_ERR(ar->otp);
		ath10k_err(ar, "could not fetch otp (%d)\n", ret);
		goto err;
	}

	ar->otp_data = ar->otp->data;
	ar->otp_len = ar->otp->size;

	return 0;

err:
	ath10k_core_free_firmware_files(ar);
	return ret;
}

static int ath10k_core_fetch_firmware_api_n(struct ath10k *ar, const char *name)
{
	size_t magic_len, len, ie_len;
	int ie_id, i, index, bit, ret;
	struct ath10k_fw_ie *hdr;
	const u8 *data;
	__le32 *timestamp;

	/* first fetch the firmware file (firmware-*.bin) */
	ar->firmware = ath10k_fetch_fw_file(ar, ar->hw_params.fw.dir, name);
	if (IS_ERR(ar->firmware)) {
		ath10k_err(ar, "could not fetch firmware file '%s/%s': %ld\n",
			   ar->hw_params.fw.dir, name, PTR_ERR(ar->firmware));
		return PTR_ERR(ar->firmware);
	}

	data = ar->firmware->data;
	len = ar->firmware->size;

	/* magic also includes the null byte, check that as well */
	magic_len = strlen(ATH10K_FIRMWARE_MAGIC) + 1;

	if (len < magic_len) {
		ath10k_err(ar, "firmware file '%s/%s' too small to contain magic: %zu\n",
			   ar->hw_params.fw.dir, name, len);
		ret = -EINVAL;
		goto err;
	}

	if (memcmp(data, ATH10K_FIRMWARE_MAGIC, magic_len) != 0) {
		ath10k_err(ar, "invalid firmware magic\n");
		ret = -EINVAL;
		goto err;
	}

	/* jump over the padding */
	magic_len = ALIGN(magic_len, 4);

	len -= magic_len;
	data += magic_len;

	/* loop elements */
	while (len > sizeof(struct ath10k_fw_ie)) {
		hdr = (struct ath10k_fw_ie *)data;

		ie_id = le32_to_cpu(hdr->id);
		ie_len = le32_to_cpu(hdr->len);

		len -= sizeof(*hdr);
		data += sizeof(*hdr);

		if (len < ie_len) {
			ath10k_err(ar, "invalid length for FW IE %d (%zu < %zu)\n",
				   ie_id, len, ie_len);
			ret = -EINVAL;
			goto err;
		}

		switch (ie_id) {
		case ATH10K_FW_IE_FW_VERSION:
			if (ie_len > sizeof(ar->hw->wiphy->fw_version) - 1)
				break;

			memcpy(ar->hw->wiphy->fw_version, data, ie_len);
			ar->hw->wiphy->fw_version[ie_len] = '\0';

			ath10k_dbg(ar, ATH10K_DBG_BOOT,
				   "found fw version %s\n",
				    ar->hw->wiphy->fw_version);
			break;
		case ATH10K_FW_IE_TIMESTAMP:
			if (ie_len != sizeof(u32))
				break;

			timestamp = (__le32 *)data;

			ath10k_dbg(ar, ATH10K_DBG_BOOT, "found fw timestamp %d\n",
				   le32_to_cpup(timestamp));
			break;
		case ATH10K_FW_IE_FEATURES:
			ath10k_dbg(ar, ATH10K_DBG_BOOT,
				   "found firmware features ie (%zd B)\n",
				   ie_len);

			for (i = 0; i < ATH10K_FW_FEATURE_COUNT; i++) {
				index = i / 8;
				bit = i % 8;

				if (index == ie_len)
					break;

				if (data[index] & (1 << bit)) {
					ath10k_dbg(ar, ATH10K_DBG_BOOT,
						   "Enabling feature bit: %i\n",
						   i);
					__set_bit(i, ar->fw_features);
				}
			}

			ath10k_dbg_dump(ar, ATH10K_DBG_BOOT, "features", "",
					ar->fw_features,
					sizeof(ar->fw_features));
			break;
		case ATH10K_FW_IE_FW_IMAGE:
			ath10k_dbg(ar, ATH10K_DBG_BOOT,
				   "found fw image ie (%zd B)\n",
				   ie_len);

			ar->firmware_data = data;
			ar->firmware_len = ie_len;

			break;
		case ATH10K_FW_IE_OTP_IMAGE:
			ath10k_dbg(ar, ATH10K_DBG_BOOT,
				   "found otp image ie (%zd B)\n",
				   ie_len);

			ar->otp_data = data;
			ar->otp_len = ie_len;

			break;
		default:
			ath10k_warn(ar, "Unknown FW IE: %u\n",
				    le32_to_cpu(hdr->id));
			break;
		}

		/* jump over the padding */
		ie_len = ALIGN(ie_len, 4);

		len -= ie_len;
		data += ie_len;
	}

	if (!ar->firmware_data || !ar->firmware_len) {
		ath10k_warn(ar, "No ATH10K_FW_IE_FW_IMAGE found from '%s/%s', skipping\n",
			    ar->hw_params.fw.dir, name);
		ret = -ENOMEDIUM;
		goto err;
	}

	if (test_bit(ATH10K_FW_FEATURE_WMI_10_2, ar->fw_features) &&
	    !test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features)) {
		ath10k_err(ar, "feature bits corrupted: 10.2 feature requires 10.x feature to be set as well");
		ret = -EINVAL;
		goto err;
	}

	/* now fetch the board file */
	if (ar->hw_params.fw.board == NULL) {
		ath10k_err(ar, "board data file not defined");
		ret = -EINVAL;
		goto err;
	}

	ar->board = ath10k_fetch_fw_file(ar,
					 ar->hw_params.fw.dir,
					 ar->hw_params.fw.board);
	if (IS_ERR(ar->board)) {
		ret = PTR_ERR(ar->board);
		ath10k_err(ar, "could not fetch board data '%s/%s' (%d)\n",
			   ar->hw_params.fw.dir, ar->hw_params.fw.board,
			   ret);
		goto err;
	}

	ar->board_data = ar->board->data;
	ar->board_len = ar->board->size;

	return 0;

err:
	ath10k_core_free_firmware_files(ar);
	return ret;
}

static int ath10k_core_fetch_firmware_files(struct ath10k *ar)
{
	int ret;

	/* calibration file is optional, don't check for any errors */
	ath10k_fetch_cal_file(ar);

	ar->fw_api = 3;
	ath10k_dbg(ar, ATH10K_DBG_BOOT, "trying fw api %d\n", ar->fw_api);

	ret = ath10k_core_fetch_firmware_api_n(ar, ATH10K_FW_API3_FILE);
	if (ret == 0)
		goto success;

	ar->fw_api = 2;
	ath10k_dbg(ar, ATH10K_DBG_BOOT, "trying fw api %d\n", ar->fw_api);

	ret = ath10k_core_fetch_firmware_api_n(ar, ATH10K_FW_API2_FILE);
	if (ret == 0)
		goto success;

	ar->fw_api = 1;
	ath10k_dbg(ar, ATH10K_DBG_BOOT, "trying fw api %d\n", ar->fw_api);

	ret = ath10k_core_fetch_firmware_api_1(ar);
	if (ret)
		return ret;

success:
	ath10k_dbg(ar, ATH10K_DBG_BOOT, "using fw api %d\n", ar->fw_api);

	return 0;
}

static int ath10k_download_cal_data(struct ath10k *ar)
{
	int ret;

	ret = ath10k_download_cal_file(ar);
	if (ret == 0) {
		ar->cal_mode = ATH10K_CAL_MODE_FILE;
		goto done;
	}

	ath10k_dbg(ar, ATH10K_DBG_BOOT,
		   "boot did not find a calibration file, try DT next: %d\n",
		   ret);

	ret = ath10k_download_cal_dt(ar);
	if (ret == 0) {
		ar->cal_mode = ATH10K_CAL_MODE_DT;
		goto done;
	}

	ath10k_dbg(ar, ATH10K_DBG_BOOT,
		   "boot did not find DT entry, try OTP next: %d\n",
		   ret);

	ret = ath10k_download_and_run_otp(ar);
	if (ret) {
		ath10k_err(ar, "failed to run otp: %d\n", ret);
		return ret;
	}

	ar->cal_mode = ATH10K_CAL_MODE_OTP;

done:
	ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot using calibration mode %s\n",
		   ath10k_cal_mode_str(ar->cal_mode));
	return 0;
}

static int ath10k_init_uart(struct ath10k *ar)
{
	int ret;

	/*
	 * Explicitly setting UART prints to zero as target turns it on
	 * based on scratch registers.
	 */
	ret = ath10k_bmi_write32(ar, hi_serial_enable, 0);
	if (ret) {
		ath10k_warn(ar, "could not disable UART prints (%d)\n", ret);
		return ret;
	}

	if (!uart_print)
		return 0;

	ret = ath10k_bmi_write32(ar, hi_dbg_uart_txpin, ar->hw_params.uart_pin);
	if (ret) {
		ath10k_warn(ar, "could not enable UART prints (%d)\n", ret);
		return ret;
	}

	ret = ath10k_bmi_write32(ar, hi_serial_enable, 1);
	if (ret) {
		ath10k_warn(ar, "could not enable UART prints (%d)\n", ret);
		return ret;
	}

	/* Set the UART baud rate to 19200. */
	ret = ath10k_bmi_write32(ar, hi_desired_baud_rate, 19200);
	if (ret) {
		ath10k_warn(ar, "could not set the baud rate (%d)\n", ret);
		return ret;
	}

	ath10k_info(ar, "UART prints enabled\n");
	return 0;
}

static int ath10k_init_hw_params(struct ath10k *ar)
{
	const struct ath10k_hw_params *uninitialized_var(hw_params);
	int i;

	for (i = 0; i < ARRAY_SIZE(ath10k_hw_params_list); i++) {
		hw_params = &ath10k_hw_params_list[i];

		if (hw_params->id == ar->target_version)
			break;
	}

	if (i == ARRAY_SIZE(ath10k_hw_params_list)) {
		ath10k_err(ar, "Unsupported hardware version: 0x%x\n",
			   ar->target_version);
		return -EINVAL;
	}

	ar->hw_params = *hw_params;

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "Hardware name %s version 0x%x\n",
		   ar->hw_params.name, ar->target_version);

	return 0;
}

static void ath10k_core_restart(struct work_struct *work)
{
	struct ath10k *ar = container_of(work, struct ath10k, restart_work);

	set_bit(ATH10K_FLAG_CRASH_FLUSH, &ar->dev_flags);

	/* Place a barrier to make sure the compiler doesn't reorder
	 * CRASH_FLUSH and calling other functions.
	 */
	barrier();

	ieee80211_stop_queues(ar->hw);
	ath10k_drain_tx(ar);
	complete_all(&ar->scan.started);
	complete_all(&ar->scan.completed);
	complete_all(&ar->scan.on_channel);
	complete_all(&ar->offchan_tx_completed);
	complete_all(&ar->install_key_done);
	complete_all(&ar->vdev_setup_done);
	wake_up(&ar->htt.empty_tx_wq);
	wake_up(&ar->wmi.tx_credits_wq);
	wake_up(&ar->peer_mapping_wq);

	mutex_lock(&ar->conf_mutex);

	switch (ar->state) {
	case ATH10K_STATE_ON:
		ar->state = ATH10K_STATE_RESTARTING;
		ath10k_hif_stop(ar);
		ath10k_scan_finish(ar);
		ieee80211_restart_hw(ar->hw);
		break;
	case ATH10K_STATE_OFF:
		/* this can happen if driver is being unloaded
		 * or if the crash happens during FW probing */
		ath10k_warn(ar, "cannot restart a device that hasn't been started\n");
		break;
	case ATH10K_STATE_RESTARTING:
		/* hw restart might be requested from multiple places */
		break;
	case ATH10K_STATE_RESTARTED:
		ar->state = ATH10K_STATE_WEDGED;
		/* fall through */
	case ATH10K_STATE_WEDGED:
		ath10k_warn(ar, "device is wedged, will not restart\n");
		break;
	case ATH10K_STATE_UTF:
		ath10k_warn(ar, "firmware restart in UTF mode not supported\n");
		break;
	}

	mutex_unlock(&ar->conf_mutex);
}

static void ath10k_core_init_max_sta_count(struct ath10k *ar)
{
	if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features)) {
		ar->max_num_peers = TARGET_10X_NUM_PEERS;
		ar->max_num_stations = TARGET_10X_NUM_STATIONS;
	} else {
		ar->max_num_peers = TARGET_NUM_PEERS;
		ar->max_num_stations = TARGET_NUM_STATIONS;
	}
}

int ath10k_core_start(struct ath10k *ar, enum ath10k_firmware_mode mode)
{
	int status;

	lockdep_assert_held(&ar->conf_mutex);

	clear_bit(ATH10K_FLAG_CRASH_FLUSH, &ar->dev_flags);

	ath10k_bmi_start(ar);

	if (ath10k_init_configure_target(ar)) {
		status = -EINVAL;
		goto err;
	}

	status = ath10k_download_cal_data(ar);
	if (status)
		goto err;

	status = ath10k_download_fw(ar, mode);
	if (status)
		goto err;

	status = ath10k_init_uart(ar);
	if (status)
		goto err;

	ar->htc.htc_ops.target_send_suspend_complete =
		ath10k_send_suspend_complete;

	status = ath10k_htc_init(ar);
	if (status) {
		ath10k_err(ar, "could not init HTC (%d)\n", status);
		goto err;
	}

	status = ath10k_bmi_done(ar);
	if (status)
		goto err;

	status = ath10k_wmi_attach(ar);
	if (status) {
		ath10k_err(ar, "WMI attach failed: %d\n", status);
		goto err;
	}

	status = ath10k_htt_init(ar);
	if (status) {
		ath10k_err(ar, "failed to init htt: %d\n", status);
		goto err_wmi_detach;
	}

	status = ath10k_htt_tx_alloc(&ar->htt);
	if (status) {
		ath10k_err(ar, "failed to alloc htt tx: %d\n", status);
		goto err_wmi_detach;
	}

	status = ath10k_htt_rx_alloc(&ar->htt);
	if (status) {
		ath10k_err(ar, "failed to alloc htt rx: %d\n", status);
		goto err_htt_tx_detach;
	}

	status = ath10k_hif_start(ar);
	if (status) {
		ath10k_err(ar, "could not start HIF: %d\n", status);
		goto err_htt_rx_detach;
	}

	status = ath10k_htc_wait_target(&ar->htc);
	if (status) {
		ath10k_err(ar, "failed to connect to HTC: %d\n", status);
		goto err_hif_stop;
	}

	if (mode == ATH10K_FIRMWARE_MODE_NORMAL) {
		status = ath10k_htt_connect(&ar->htt);
		if (status) {
			ath10k_err(ar, "failed to connect htt (%d)\n", status);
			goto err_hif_stop;
		}
	}

	status = ath10k_wmi_connect(ar);
	if (status) {
		ath10k_err(ar, "could not connect wmi: %d\n", status);
		goto err_hif_stop;
	}

	status = ath10k_htc_start(&ar->htc);
	if (status) {
		ath10k_err(ar, "failed to start htc: %d\n", status);
		goto err_hif_stop;
	}

	if (mode == ATH10K_FIRMWARE_MODE_NORMAL) {
		status = ath10k_wmi_wait_for_service_ready(ar);
		if (status <= 0) {
			ath10k_warn(ar, "wmi service ready event not received");
			status = -ETIMEDOUT;
			goto err_hif_stop;
		}
	}

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "firmware %s booted\n",
		   ar->hw->wiphy->fw_version);

	status = ath10k_wmi_cmd_init(ar);
	if (status) {
		ath10k_err(ar, "could not send WMI init command (%d)\n",
			   status);
		goto err_hif_stop;
	}

	status = ath10k_wmi_wait_for_unified_ready(ar);
	if (status <= 0) {
		ath10k_err(ar, "wmi unified ready event not received\n");
		status = -ETIMEDOUT;
		goto err_hif_stop;
	}

	/* we don't care about HTT in UTF mode */
	if (mode == ATH10K_FIRMWARE_MODE_NORMAL) {
		status = ath10k_htt_setup(&ar->htt);
		if (status) {
			ath10k_err(ar, "failed to setup htt: %d\n", status);
			goto err_hif_stop;
		}
	}

	status = ath10k_debug_start(ar);
	if (status)
		goto err_hif_stop;

	if (test_bit(ATH10K_FW_FEATURE_WMI_10X, ar->fw_features))
		ar->free_vdev_map = (1LL << TARGET_10X_NUM_VDEVS) - 1;
	else
		ar->free_vdev_map = (1LL << TARGET_NUM_VDEVS) - 1;

	INIT_LIST_HEAD(&ar->arvifs);

	return 0;

err_hif_stop:
	ath10k_hif_stop(ar);
err_htt_rx_detach:
	ath10k_htt_rx_free(&ar->htt);
err_htt_tx_detach:
	ath10k_htt_tx_free(&ar->htt);
err_wmi_detach:
	ath10k_wmi_detach(ar);
err:
	return status;
}
EXPORT_SYMBOL(ath10k_core_start);

int ath10k_wait_for_suspend(struct ath10k *ar, u32 suspend_opt)
{
	int ret;

	reinit_completion(&ar->target_suspend);

	ret = ath10k_wmi_pdev_suspend_target(ar, suspend_opt);
	if (ret) {
		ath10k_warn(ar, "could not suspend target (%d)\n", ret);
		return ret;
	}

	ret = wait_for_completion_timeout(&ar->target_suspend, 1 * HZ);

	if (ret == 0) {
		ath10k_warn(ar, "suspend timed out - target pause event never came\n");
		return -ETIMEDOUT;
	}

	return 0;
}

void ath10k_core_stop(struct ath10k *ar)
{
	lockdep_assert_held(&ar->conf_mutex);

	/* try to suspend target */
	if (ar->state != ATH10K_STATE_RESTARTING &&
	    ar->state != ATH10K_STATE_UTF)
		ath10k_wait_for_suspend(ar, WMI_PDEV_SUSPEND_AND_DISABLE_INTR);

	ath10k_debug_stop(ar);
	ath10k_hif_stop(ar);
	ath10k_htt_tx_free(&ar->htt);
	ath10k_htt_rx_free(&ar->htt);
	ath10k_wmi_detach(ar);
}
EXPORT_SYMBOL(ath10k_core_stop);

/* mac80211 manages fw/hw initialization through start/stop hooks. However in
 * order to know what hw capabilities should be advertised to mac80211 it is
 * necessary to load the firmware (and tear it down immediately since start
 * hook will try to init it again) before registering */
static int ath10k_core_probe_fw(struct ath10k *ar)
{
	struct bmi_target_info target_info;
	int ret = 0;

	ret = ath10k_hif_power_up(ar);
	if (ret) {
		ath10k_err(ar, "could not start pci hif (%d)\n", ret);
		return ret;
	}

	memset(&target_info, 0, sizeof(target_info));
	ret = ath10k_bmi_get_target_info(ar, &target_info);
	if (ret) {
		ath10k_err(ar, "could not get target info (%d)\n", ret);
		ath10k_hif_power_down(ar);
		return ret;
	}

	ar->target_version = target_info.version;
	ar->hw->wiphy->hw_version = target_info.version;

	ret = ath10k_init_hw_params(ar);
	if (ret) {
		ath10k_err(ar, "could not get hw params (%d)\n", ret);
		ath10k_hif_power_down(ar);
		return ret;
	}

	ret = ath10k_core_fetch_firmware_files(ar);
	if (ret) {
		ath10k_err(ar, "could not fetch firmware files (%d)\n", ret);
		ath10k_hif_power_down(ar);
		return ret;
	}

	ath10k_core_init_max_sta_count(ar);

	mutex_lock(&ar->conf_mutex);

	ret = ath10k_core_start(ar, ATH10K_FIRMWARE_MODE_NORMAL);
	if (ret) {
		ath10k_err(ar, "could not init core (%d)\n", ret);
		ath10k_core_free_firmware_files(ar);
		ath10k_hif_power_down(ar);
		mutex_unlock(&ar->conf_mutex);
		return ret;
	}

	ath10k_print_driver_info(ar);
	ath10k_core_stop(ar);

	mutex_unlock(&ar->conf_mutex);

	ath10k_hif_power_down(ar);
	return 0;
}

static void ath10k_core_register_work(struct work_struct *work)
{
	struct ath10k *ar = container_of(work, struct ath10k, register_work);
	int status;

	status = ath10k_core_probe_fw(ar);
	if (status) {
		ath10k_err(ar, "could not probe fw (%d)\n", status);
		goto err;
	}

	status = ath10k_mac_register(ar);
	if (status) {
		ath10k_err(ar, "could not register to mac80211 (%d)\n", status);
		goto err_release_fw;
	}

	status = ath10k_debug_register(ar);
	if (status) {
		ath10k_err(ar, "unable to initialize debugfs\n");
		goto err_unregister_mac;
	}

	status = ath10k_spectral_create(ar);
	if (status) {
		ath10k_err(ar, "failed to initialize spectral\n");
		goto err_debug_destroy;
	}

	set_bit(ATH10K_FLAG_CORE_REGISTERED, &ar->dev_flags);
	return;

err_debug_destroy:
	ath10k_debug_destroy(ar);
err_unregister_mac:
	ath10k_mac_unregister(ar);
err_release_fw:
	ath10k_core_free_firmware_files(ar);
err:
	/* TODO: It's probably a good idea to release device from the driver
	 * but calling device_release_driver() here will cause a deadlock.
	 */
	return;
}

int ath10k_core_register(struct ath10k *ar, u32 chip_id)
{
	ar->chip_id = chip_id;
	queue_work(ar->workqueue, &ar->register_work);

	return 0;
}
EXPORT_SYMBOL(ath10k_core_register);

void ath10k_core_unregister(struct ath10k *ar)
{
	cancel_work_sync(&ar->register_work);

	if (!test_bit(ATH10K_FLAG_CORE_REGISTERED, &ar->dev_flags))
		return;

	/* Stop spectral before unregistering from mac80211 to remove the
	 * relayfs debugfs file cleanly. Otherwise the parent debugfs tree
	 * would be already be free'd recursively, leading to a double free.
	 */
	ath10k_spectral_destroy(ar);

	/* We must unregister from mac80211 before we stop HTC and HIF.
	 * Otherwise we will fail to submit commands to FW and mac80211 will be
	 * unhappy about callback failures. */
	ath10k_mac_unregister(ar);

	ath10k_testmode_destroy(ar);

	ath10k_core_free_firmware_files(ar);

	ath10k_debug_unregister(ar);
}
EXPORT_SYMBOL(ath10k_core_unregister);

struct ath10k *ath10k_core_create(size_t priv_size, struct device *dev,
				  enum ath10k_bus bus,
				  const struct ath10k_hif_ops *hif_ops)
{
	struct ath10k *ar;
	int ret;

	ar = ath10k_mac_create(priv_size);
	if (!ar)
		return NULL;

	ar->ath_common.priv = ar;
	ar->ath_common.hw = ar->hw;

	ar->p2p = !!ath10k_p2p;
	ar->dev = dev;

	ar->hif.ops = hif_ops;
	ar->hif.bus = bus;

	init_completion(&ar->scan.started);
	init_completion(&ar->scan.completed);
	init_completion(&ar->scan.on_channel);
	init_completion(&ar->target_suspend);

	init_completion(&ar->install_key_done);
	init_completion(&ar->vdev_setup_done);

	INIT_DELAYED_WORK(&ar->scan.timeout, ath10k_scan_timeout_work);

	ar->workqueue = create_singlethread_workqueue("ath10k_wq");
	if (!ar->workqueue)
		goto err_free_mac;

	mutex_init(&ar->conf_mutex);
	spin_lock_init(&ar->data_lock);

	INIT_LIST_HEAD(&ar->peers);
	init_waitqueue_head(&ar->peer_mapping_wq);
	init_waitqueue_head(&ar->htt.empty_tx_wq);
	init_waitqueue_head(&ar->wmi.tx_credits_wq);

	init_completion(&ar->offchan_tx_completed);
	INIT_WORK(&ar->offchan_tx_work, ath10k_offchan_tx_work);
	skb_queue_head_init(&ar->offchan_tx_queue);

	INIT_WORK(&ar->wmi_mgmt_tx_work, ath10k_mgmt_over_wmi_tx_work);
	skb_queue_head_init(&ar->wmi_mgmt_tx_queue);

	INIT_WORK(&ar->register_work, ath10k_core_register_work);
	INIT_WORK(&ar->restart_work, ath10k_core_restart);

	ret = ath10k_debug_create(ar);
	if (ret)
		goto err_free_wq;

	return ar;

err_free_wq:
	destroy_workqueue(ar->workqueue);

err_free_mac:
	ath10k_mac_destroy(ar);

	return NULL;
}
EXPORT_SYMBOL(ath10k_core_create);

void ath10k_core_destroy(struct ath10k *ar)
{
	flush_workqueue(ar->workqueue);
	destroy_workqueue(ar->workqueue);

	ath10k_debug_destroy(ar);
	ath10k_mac_destroy(ar);
}
EXPORT_SYMBOL(ath10k_core_destroy);

MODULE_AUTHOR("Qualcomm Atheros");
MODULE_DESCRIPTION("Core module for QCA988X PCIe devices.");
MODULE_LICENSE("Dual BSD/GPL");
