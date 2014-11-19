/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
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
	int status;
	void *buffer = NULL;
	bool result = false;
	u16 length;
	int ii, rc;

	dev_dbg(dev, "---->Download firmware\n");

	rc = request_firmware(&fw, FIRMWARE_NAME, dev);
	if (rc) {
		dev_err(dev, "firmware file %s request failed (%d)\n",
			FIRMWARE_NAME, rc);
			goto out;
	}

	buffer = kmalloc(FIRMWARE_CHUNK_SIZE, GFP_KERNEL);
	if (!buffer)
		goto out;

	for (ii = 0; ii < fw->size; ii += FIRMWARE_CHUNK_SIZE) {
		length = min_t(int, fw->size - ii, FIRMWARE_CHUNK_SIZE);
		memcpy(buffer, fw->data + ii, length);

		status = vnt_control_out(priv,
						0,
						0x1200+ii,
						0x0000,
						length,
						buffer);

		dev_dbg(dev, "Download firmware...%d %zu\n", ii, fw->size);

		if (status != STATUS_SUCCESS)
			goto free_fw;
	}

	result = true;
free_fw:
	release_firmware(fw);

out:
	kfree(buffer);

	return result;
}
MODULE_FIRMWARE(FIRMWARE_NAME);

int vnt_firmware_branch_to_sram(struct vnt_private *priv)
{
	int status;

	dev_dbg(&priv->usb->dev, "---->Branch to Sram\n");

	status = vnt_control_out(priv,
					1,
					0x1200,
					0x0000,
					0,
					NULL);
	if (status != STATUS_SUCCESS)
		return false;
	else
		return true;
}

int vnt_check_firmware_version(struct vnt_private *priv)
{
	int status;

	status = vnt_control_in(priv,
					MESSAGE_TYPE_READ,
					0,
					MESSAGE_REQUEST_VERSION,
					2,
					(u8 *)&priv->firmware_version);

	dev_dbg(&priv->usb->dev, "Firmware Version [%04x]\n",
						priv->firmware_version);

	if (status != STATUS_SUCCESS) {
		dev_dbg(&priv->usb->dev, "Firmware Invalid.\n");
		return false;
	}
	if (priv->firmware_version == 0xFFFF) {
		dev_dbg(&priv->usb->dev, "In Loader.\n");
		return false;
	}

	dev_dbg(&priv->usb->dev, "Firmware Version [%04x]\n",
						priv->firmware_version);

	if (priv->firmware_version < FIRMWARE_VERSION) {
		/* branch to loader for download new firmware */
		vnt_firmware_branch_to_sram(priv);
		return false;
	}
	return true;
}
