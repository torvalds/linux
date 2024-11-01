// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip Polarfire SoC "Auto Update" FPGA reprogramming.
 *
 * Documentation of this functionality is available in the "PolarFire® FPGA and
 * PolarFire SoC FPGA Programming" User Guide.
 *
 * Copyright (c) 2022-2023 Microchip Corporation. All rights reserved.
 *
 * Author: Conor Dooley <conor.dooley@microchip.com>
 */
#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/firmware.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

#include <soc/microchip/mpfs.h>

#define AUTO_UPDATE_DEFAULT_MBOX_OFFSET		0u
#define AUTO_UPDATE_DEFAULT_RESP_OFFSET		0u

#define AUTO_UPDATE_FEATURE_CMD_OPCODE		0x05u
#define AUTO_UPDATE_FEATURE_CMD_DATA_SIZE	0u
#define AUTO_UPDATE_FEATURE_RESP_SIZE		33u
#define AUTO_UPDATE_FEATURE_CMD_DATA		NULL
#define AUTO_UPDATE_FEATURE_ENABLED		BIT(5)

#define AUTO_UPDATE_AUTHENTICATE_CMD_OPCODE	0x22u
#define AUTO_UPDATE_AUTHENTICATE_CMD_DATA_SIZE	0u
#define AUTO_UPDATE_AUTHENTICATE_RESP_SIZE	1u
#define AUTO_UPDATE_AUTHENTICATE_CMD_DATA	NULL

#define AUTO_UPDATE_PROGRAM_CMD_OPCODE		0x46u
#define AUTO_UPDATE_PROGRAM_CMD_DATA_SIZE	0u
#define AUTO_UPDATE_PROGRAM_RESP_SIZE		1u
#define AUTO_UPDATE_PROGRAM_CMD_DATA		NULL

/*
 * SPI Flash layout example:
 * |------------------------------| 0x0000000
 * | 1 KiB                        |
 * | SPI "directories"            |
 * |------------------------------| 0x0000400
 * | 1 MiB                        |
 * | Reserved area                |
 * | Used for bitstream info      |
 * |------------------------------| 0x0100400
 * | 20 MiB                       |
 * | Golden Image                 |
 * |------------------------------| 0x1500400
 * | 20 MiB                       |
 * | Auto Upgrade Image           |
 * |------------------------------| 0x2900400
 * | 20 MiB                       |
 * | Reserved for multi-image IAP |
 * | Unused for Auto Upgrade      |
 * |------------------------------| 0x3D00400
 * | ? B                          |
 * | Unused                       |
 * |------------------------------| 0x?
 */
#define AUTO_UPDATE_DIRECTORY_BASE	0u
#define AUTO_UPDATE_DIRECTORY_WIDTH	4u
#define AUTO_UPDATE_GOLDEN_INDEX	0u
#define AUTO_UPDATE_UPGRADE_INDEX	1u
#define AUTO_UPDATE_BLANK_INDEX		2u
#define AUTO_UPDATE_GOLDEN_DIRECTORY	(AUTO_UPDATE_DIRECTORY_WIDTH * AUTO_UPDATE_GOLDEN_INDEX)
#define AUTO_UPDATE_UPGRADE_DIRECTORY	(AUTO_UPDATE_DIRECTORY_WIDTH * AUTO_UPDATE_UPGRADE_INDEX)
#define AUTO_UPDATE_BLANK_DIRECTORY	(AUTO_UPDATE_DIRECTORY_WIDTH * AUTO_UPDATE_BLANK_INDEX)
#define AUTO_UPDATE_DIRECTORY_SIZE	SZ_1K
#define AUTO_UPDATE_INFO_BASE		AUTO_UPDATE_DIRECTORY_SIZE
#define AUTO_UPDATE_INFO_SIZE		SZ_1M
#define AUTO_UPDATE_BITSTREAM_BASE	(AUTO_UPDATE_DIRECTORY_SIZE + AUTO_UPDATE_INFO_SIZE)

struct mpfs_auto_update_priv {
	struct mpfs_sys_controller *sys_controller;
	struct device *dev;
	struct mtd_info *flash;
	struct fw_upload *fw_uploader;
	size_t size_per_bitstream;
	bool cancel_request;
};

static bool mpfs_auto_update_is_bitstream_info(const u8 *data, u32 size)
{
	if (size < 4)
		return false;

	if (data[0] == 0x4d && data[1] == 0x43 && data[2] == 0x48 && data[3] == 0x50)
		return true;

	return false;
}

static enum fw_upload_err mpfs_auto_update_prepare(struct fw_upload *fw_uploader, const u8 *data,
						   u32 size)
{
	struct mpfs_auto_update_priv *priv = fw_uploader->dd_handle;
	size_t erase_size = AUTO_UPDATE_DIRECTORY_SIZE;

	/*
	 * Verifying the Golden Image is idealistic. It will be evaluated
	 * against the currently programmed image and thus may fail - due to
	 * either rollback protection (if its an older version than that in use)
	 * or if the version is the same as that of the in-use image.
	 * Extracting the information as to why a failure occurred is not
	 * currently possible due to limitations of the system controller
	 * driver. If those are fixed, verification of the Golden Image should
	 * be added here.
	 */

	priv->flash = mpfs_sys_controller_get_flash(priv->sys_controller);
	if (!priv->flash)
		return FW_UPLOAD_ERR_HW_ERROR;

	erase_size = round_up(erase_size, (u64)priv->flash->erasesize);

	/*
	 * We need to calculate if we have enough space in the flash for the
	 * new image.
	 * First, chop off the first 1 KiB as it's reserved for the directory.
	 * The 1 MiB reserved for design info needs to be ignored also.
	 * All that remains is carved into 3 & rounded down to the erasesize.
	 * If this is smaller than the image size, we abort.
	 * There's also no need to consume more than 20 MiB per image.
	 */
	priv->size_per_bitstream = priv->flash->size - SZ_1K - SZ_1M;
	priv->size_per_bitstream = round_down(priv->size_per_bitstream / 3, erase_size);
	if (priv->size_per_bitstream > 20 * SZ_1M)
		priv->size_per_bitstream = 20 * SZ_1M;

	if (priv->size_per_bitstream < size) {
		dev_err(priv->dev,
			"flash device has insufficient capacity to store this bitstream\n");
		return FW_UPLOAD_ERR_INVALID_SIZE;
	}

	priv->cancel_request = false;

	return FW_UPLOAD_ERR_NONE;
}

static void mpfs_auto_update_cancel(struct fw_upload *fw_uploader)
{
	struct mpfs_auto_update_priv *priv = fw_uploader->dd_handle;

	priv->cancel_request = true;
}

static enum fw_upload_err mpfs_auto_update_poll_complete(struct fw_upload *fw_uploader)
{
	return FW_UPLOAD_ERR_NONE;
}

static int mpfs_auto_update_verify_image(struct fw_upload *fw_uploader)
{
	struct mpfs_auto_update_priv *priv = fw_uploader->dd_handle;
	u32 *response_msg __free(kfree) =
		kzalloc(AUTO_UPDATE_FEATURE_RESP_SIZE * sizeof(*response_msg), GFP_KERNEL);
	struct mpfs_mss_response *response __free(kfree) =
		kzalloc(sizeof(struct mpfs_mss_response), GFP_KERNEL);
	struct mpfs_mss_msg *message __free(kfree) =
		kzalloc(sizeof(struct mpfs_mss_msg), GFP_KERNEL);
	int ret;

	if (!response_msg || !response || !message)
		return -ENOMEM;

	/*
	 * The system controller can verify that an image in the flash is valid.
	 * Rather than duplicate the check in this driver, call the relevant
	 * service from the system controller instead.
	 * This service has no command data and no response data. It overloads
	 * mbox_offset with the image index in the flash's SPI directory where
	 * the bitstream is located.
	 */
	response->resp_msg = response_msg;
	response->resp_size = AUTO_UPDATE_AUTHENTICATE_RESP_SIZE;
	message->cmd_opcode = AUTO_UPDATE_AUTHENTICATE_CMD_OPCODE;
	message->cmd_data_size = AUTO_UPDATE_AUTHENTICATE_CMD_DATA_SIZE;
	message->response = response;
	message->cmd_data = AUTO_UPDATE_AUTHENTICATE_CMD_DATA;
	message->mbox_offset = AUTO_UPDATE_UPGRADE_INDEX;
	message->resp_offset = AUTO_UPDATE_DEFAULT_RESP_OFFSET;

	dev_info(priv->dev, "Running verification of Upgrade Image\n");
	ret = mpfs_blocking_transaction(priv->sys_controller, message);
	if (ret | response->resp_status) {
		dev_warn(priv->dev, "Verification of Upgrade Image failed!\n");
		return ret ? ret : -EBADMSG;
	}

	dev_info(priv->dev, "Verification of Upgrade Image passed!\n");

	return 0;
}

static int mpfs_auto_update_set_image_address(struct mpfs_auto_update_priv *priv,
					      u32 image_address, loff_t directory_address)
{
	struct erase_info erase;
	size_t erase_size = round_up(AUTO_UPDATE_DIRECTORY_SIZE, (u64)priv->flash->erasesize);
	size_t bytes_written = 0, bytes_read = 0;
	char *buffer __free(kfree) = kzalloc(erase_size, GFP_KERNEL);
	int ret;

	if (!buffer)
		return -ENOMEM;

	erase.addr = AUTO_UPDATE_DIRECTORY_BASE;
	erase.len = erase_size;

	/*
	 * We need to write the "SPI DIRECTORY" to the first 1 KiB, telling
	 * the system controller where to find the actual bitstream. Since
	 * this is spi-nor, we have to read the first eraseblock, erase that
	 * portion of the flash, modify the data and then write it back.
	 * There's no need to do this though if things are already the way they
	 * should be, so check and save the write in that case.
	 */
	ret = mtd_read(priv->flash, AUTO_UPDATE_DIRECTORY_BASE, erase_size, &bytes_read,
		       (u_char *)buffer);
	if (ret)
		return ret;

	if (bytes_read != erase_size)
		return -EIO;

	if ((*(u32 *)(buffer + AUTO_UPDATE_UPGRADE_DIRECTORY) == image_address) &&
	    !(*(u32 *)(buffer + AUTO_UPDATE_BLANK_DIRECTORY)))
		return 0;

	ret = mtd_erase(priv->flash, &erase);
	if (ret)
		return ret;

	/*
	 * Populate the image address and then zero out the next directory so
	 * that the system controller doesn't complain if in "Single Image"
	 * mode.
	 */
	memcpy(buffer + AUTO_UPDATE_UPGRADE_DIRECTORY, &image_address,
	       AUTO_UPDATE_DIRECTORY_WIDTH);
	memset(buffer + AUTO_UPDATE_BLANK_DIRECTORY, 0x0, AUTO_UPDATE_DIRECTORY_WIDTH);

	dev_info(priv->dev, "Writing the image address (0x%x) to the flash directory (0x%llx)\n",
		 image_address, directory_address);

	ret = mtd_write(priv->flash, 0x0, erase_size, &bytes_written, (u_char *)buffer);
	if (ret)
		return ret;

	if (bytes_written != erase_size)
		return -EIO;

	return 0;
}

static int mpfs_auto_update_write_bitstream(struct fw_upload *fw_uploader, const u8 *data,
					    u32 offset, u32 size, u32 *written)
{
	struct mpfs_auto_update_priv *priv = fw_uploader->dd_handle;
	struct erase_info erase;
	loff_t directory_address = AUTO_UPDATE_UPGRADE_DIRECTORY;
	size_t erase_size = AUTO_UPDATE_DIRECTORY_SIZE;
	size_t bytes_written = 0;
	bool is_info = mpfs_auto_update_is_bitstream_info(data, size);
	u32 image_address;
	int ret;

	erase_size = round_up(erase_size, (u64)priv->flash->erasesize);

	if (is_info)
		image_address = AUTO_UPDATE_INFO_BASE;
	else
		image_address = AUTO_UPDATE_BITSTREAM_BASE +
				AUTO_UPDATE_UPGRADE_INDEX * priv->size_per_bitstream;

	/*
	 * For bitstream info, the descriptor is written to a fixed offset,
	 * so there is no need to set the image address.
	 */
	if (!is_info) {
		ret = mpfs_auto_update_set_image_address(priv, image_address, directory_address);
		if (ret) {
			dev_err(priv->dev, "failed to set image address in the SPI directory: %d\n", ret);
			return ret;
		}
	} else {
		if (size > AUTO_UPDATE_INFO_SIZE) {
			dev_err(priv->dev, "bitstream info exceeds permitted size\n");
			return -ENOSPC;
		}
	}

	/*
	 * Now the .spi image itself can be written to the flash. Preservation
	 * of contents here is not important here, unlike the spi "directory"
	 * which must be RMWed.
	 */
	erase.len = round_up(size, (size_t)priv->flash->erasesize);
	erase.addr = image_address;

	dev_info(priv->dev, "Erasing the flash at address (0x%x)\n", image_address);
	ret = mtd_erase(priv->flash, &erase);
	if (ret)
		return ret;

	/*
	 * No parsing etc of the bitstream is required. The system controller
	 * will do all of that itself - including verifying that the bitstream
	 * is valid.
	 */
	dev_info(priv->dev, "Writing the image to the flash at address (0x%x)\n", image_address);
	ret = mtd_write(priv->flash, (loff_t)image_address, size, &bytes_written, data);
	if (ret)
		return ret;

	if (bytes_written != size)
		return -EIO;

	*written = bytes_written;
	dev_info(priv->dev, "Wrote 0x%zx bytes to the flash\n", bytes_written);

	return 0;
}

static enum fw_upload_err mpfs_auto_update_write(struct fw_upload *fw_uploader, const u8 *data,
						 u32 offset, u32 size, u32 *written)
{
	struct mpfs_auto_update_priv *priv = fw_uploader->dd_handle;
	int ret;

	ret = mpfs_auto_update_write_bitstream(fw_uploader, data, offset, size, written);
	if (ret)
		return FW_UPLOAD_ERR_RW_ERROR;

	if (priv->cancel_request)
		return FW_UPLOAD_ERR_CANCELED;

	if (mpfs_auto_update_is_bitstream_info(data, size))
		return FW_UPLOAD_ERR_NONE;

	ret = mpfs_auto_update_verify_image(fw_uploader);
	if (ret)
		return FW_UPLOAD_ERR_FW_INVALID;

	return FW_UPLOAD_ERR_NONE;
}

static const struct fw_upload_ops mpfs_auto_update_ops = {
	.prepare = mpfs_auto_update_prepare,
	.write = mpfs_auto_update_write,
	.poll_complete = mpfs_auto_update_poll_complete,
	.cancel = mpfs_auto_update_cancel,
};

static int mpfs_auto_update_available(struct mpfs_auto_update_priv *priv)
{
	u32 *response_msg __free(kfree) =
		kzalloc(AUTO_UPDATE_FEATURE_RESP_SIZE * sizeof(*response_msg), GFP_KERNEL);
	struct mpfs_mss_response *response __free(kfree) =
		kzalloc(sizeof(struct mpfs_mss_response), GFP_KERNEL);
	struct mpfs_mss_msg *message __free(kfree) =
		kzalloc(sizeof(struct mpfs_mss_msg), GFP_KERNEL);
	int ret;

	if (!response_msg || !response || !message)
		return -ENOMEM;

	/*
	 * To verify that Auto Update is possible, the "Query Security Service
	 * Request" is performed.
	 * This service has no command data & does not overload mbox_offset.
	 */
	response->resp_msg = response_msg;
	response->resp_size = AUTO_UPDATE_FEATURE_RESP_SIZE;
	message->cmd_opcode = AUTO_UPDATE_FEATURE_CMD_OPCODE;
	message->cmd_data_size = AUTO_UPDATE_FEATURE_CMD_DATA_SIZE;
	message->response = response;
	message->cmd_data = AUTO_UPDATE_FEATURE_CMD_DATA;
	message->mbox_offset = AUTO_UPDATE_DEFAULT_MBOX_OFFSET;
	message->resp_offset = AUTO_UPDATE_DEFAULT_RESP_OFFSET;

	ret = mpfs_blocking_transaction(priv->sys_controller, message);
	if (ret)
		return ret;

	/*
	 * Currently, the system controller's firmware does not generate any
	 * interrupts for failed services, so mpfs_blocking_transaction() should
	 * time out & therefore return an error.
	 * Hitting this check is highly unlikely at present, but if the system
	 * controller's behaviour changes so that it does generate interrupts
	 * for failed services, it will be required.
	 */
	if (response->resp_status)
		return -EIO;

	/*
	 * Bit 5 of byte 1 is "UL_Auto Update" & if it is set, Auto Update is
	 * not possible.
	 */
	if (response_msg[1] & AUTO_UPDATE_FEATURE_ENABLED)
		return -EPERM;

	return 0;
}

static int mpfs_auto_update_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpfs_auto_update_priv *priv;
	struct fw_upload *fw_uploader;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->sys_controller = mpfs_sys_controller_get(dev);
	if (IS_ERR(priv->sys_controller))
		return dev_err_probe(dev, PTR_ERR(priv->sys_controller),
				     "Could not register as a sub device of the system controller\n");

	priv->dev = dev;
	platform_set_drvdata(pdev, priv);

	ret = mpfs_auto_update_available(priv);
	if (ret)
		return dev_err_probe(dev, ret,
				     "The current bitstream does not support auto-update\n");

	fw_uploader = firmware_upload_register(THIS_MODULE, dev, "mpfs-auto-update",
					       &mpfs_auto_update_ops, priv);
	if (IS_ERR(fw_uploader))
		return dev_err_probe(dev, PTR_ERR(fw_uploader),
				     "Failed to register the bitstream uploader\n");

	priv->fw_uploader = fw_uploader;

	return 0;
}

static void mpfs_auto_update_remove(struct platform_device *pdev)
{
	struct mpfs_auto_update_priv *priv = platform_get_drvdata(pdev);

	firmware_upload_unregister(priv->fw_uploader);
}

static struct platform_driver mpfs_auto_update_driver = {
	.driver = {
		.name = "mpfs-auto-update",
	},
	.probe = mpfs_auto_update_probe,
	.remove_new = mpfs_auto_update_remove,
};
module_platform_driver(mpfs_auto_update_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_DESCRIPTION("PolarFire SoC Auto Update FPGA reprogramming");
