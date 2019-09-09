// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * File: baseband.c
 *
 * Purpose: Implement functions to access baseband
 *
 * Author: Yiching Chen
 *
 * Date: May 20, 2004
 *
 * Functions:
 *
 * Revision History:
 *
 */

#include <linux/compiler.h>
#include "firmware.h"
#include "usbpipe.h"

#define FIRMWARE_VERSION	0x133		/* version 1.51 */
#define FIRMWARE_NAME		"vntwusb.fw"

#define FIRMWARE_CHUNK_SIZE	0x400

int vnt_download_firmware(struct vnt_private *priv)
{
	struct device *dev = &priv->usb->dev;
	const struct firmware *fw;
	void *buffer = NULL;
	u16 length;
	int ii;
	int ret = 0;

	dev_dbg(dev, "---->Download firmware\n");

	ret = request_firmware(&fw, FIRMWARE_NAME, dev);
	if (ret) {
		dev_err(dev, "firmware file %s request failed (%d)\n",
			FIRMWARE_NAME, ret);
		goto end;
	}

	buffer = kmalloc(FIRMWARE_CHUNK_SIZE, GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto free_fw;
	}

	for (ii = 0; ii < fw->size; ii += FIRMWARE_CHUNK_SIZE) {
		length = min_t(int, fw->size - ii, FIRMWARE_CHUNK_SIZE);
		memcpy(buffer, fw->data + ii, length);

		ret = vnt_control_out(priv, 0, 0x1200 + ii, 0x0000, length,
				      buffer);
		if (ret)
			goto free_buffer;

		dev_dbg(dev, "Download firmware...%d %zu\n", ii, fw->size);
	}

free_buffer:
	kfree(buffer);
free_fw:
	release_firmware(fw);
end:
	return ret;
}
MODULE_FIRMWARE(FIRMWARE_NAME);

int vnt_firmware_branch_to_sram(struct vnt_private *priv)
{
	dev_dbg(&priv->usb->dev, "---->Branch to Sram\n");

	return vnt_control_out(priv, 1, 0x1200, 0x0000, 0, NULL);
}

int vnt_check_firmware_version(struct vnt_private *priv)
{
	int ret = 0;

	ret = vnt_control_in(priv, MESSAGE_TYPE_READ, 0,
			     MESSAGE_REQUEST_VERSION, 2,
			     (u8 *)&priv->firmware_version);
	if (ret) {
		dev_dbg(&priv->usb->dev,
			"Could not get firmware version: %d.\n", ret);
		goto end;
	}

	dev_dbg(&priv->usb->dev, "Firmware Version [%04x]\n",
		priv->firmware_version);

	if (priv->firmware_version == 0xFFFF) {
		dev_dbg(&priv->usb->dev, "In Loader.\n");
		ret = -EINVAL;
		goto end;
	}

	if (priv->firmware_version < FIRMWARE_VERSION) {
		/* branch to loader for download new firmware */
		ret = vnt_firmware_branch_to_sram(priv);
		if (ret) {
			dev_dbg(&priv->usb->dev,
				"Could not branch to SRAM: %d.\n", ret);
		} else {
			ret = -EINVAL;
		}
	}

end:
	return ret;
}
