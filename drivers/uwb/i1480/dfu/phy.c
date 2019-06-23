// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Wireless UWB Link 1480
 * PHY parameters upload
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * Code for uploading the PHY parameters to the PHY through the UWB
 * Radio Control interface.
 *
 * We just send the data through the MPI interface using HWA-like
 * commands and then reset the PHY to make sure it is ok.
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/usb/wusb.h>
#include "i1480-dfu.h"


/**
 * Write a value array to an address of the MPI interface
 *
 * @i1480:	Device descriptor
 * @data:	Data array to write
 * @size:	Size of the data array
 * @returns:	0 if ok, < 0 errno code on error.
 *
 * The data array is organized into pairs:
 *
 * ADDRESS VALUE
 *
 * ADDRESS is BE 16 bit unsigned, VALUE 8 bit unsigned. Size thus has
 * to be a multiple of three.
 */
static
int i1480_mpi_write(struct i1480 *i1480, const void *data, size_t size)
{
	int result;
	struct i1480_cmd_mpi_write *cmd = i1480->cmd_buf;
	struct i1480_evt_confirm *reply = i1480->evt_buf;

	BUG_ON(size > 480);
	result = -ENOMEM;
	cmd->rccb.bCommandType = i1480_CET_VS1;
	cmd->rccb.wCommand = cpu_to_le16(i1480_CMD_MPI_WRITE);
	cmd->size = cpu_to_le16(size);
	memcpy(cmd->data, data, size);
	reply->rceb.bEventType = i1480_CET_VS1;
	reply->rceb.wEvent = i1480_CMD_MPI_WRITE;
	result = i1480_cmd(i1480, "MPI-WRITE", sizeof(*cmd) + size, sizeof(*reply));
	if (result < 0)
		goto out;
	if (reply->bResultCode != UWB_RC_RES_SUCCESS) {
		dev_err(i1480->dev, "MPI-WRITE: command execution failed: %d\n",
			reply->bResultCode);
		result = -EIO;
	}
out:
	return result;
}


/**
 * Read a value array to from an address of the MPI interface
 *
 * @i1480:	Device descriptor
 * @data:	where to place the read array
 * @srcaddr:	Where to read from
 * @size:	Size of the data read array
 * @returns:	0 if ok, < 0 errno code on error.
 *
 * The command data array is organized into pairs ADDR0 ADDR1..., and
 * the returned data in ADDR0 VALUE0 ADDR1 VALUE1...
 *
 * We generate the command array to be a sequential read and then
 * rearrange the result.
 *
 * We use the i1480->cmd_buf for the command, i1480->evt_buf for the reply.
 *
 * As the reply has to fit in 512 bytes (i1480->evt_buffer), the max amount
 * of values we can read is (512 - sizeof(*reply)) / 3
 */
static
int i1480_mpi_read(struct i1480 *i1480, u8 *data, u16 srcaddr, size_t size)
{
	int result;
	struct i1480_cmd_mpi_read *cmd = i1480->cmd_buf;
	struct i1480_evt_mpi_read *reply = i1480->evt_buf;
	unsigned cnt;

	memset(i1480->cmd_buf, 0x69, 512);
	memset(i1480->evt_buf, 0x69, 512);

	BUG_ON(size > (i1480->buf_size - sizeof(*reply)) / 3);
	result = -ENOMEM;
	cmd->rccb.bCommandType = i1480_CET_VS1;
	cmd->rccb.wCommand = cpu_to_le16(i1480_CMD_MPI_READ);
	cmd->size = cpu_to_le16(3*size);
	for (cnt = 0; cnt < size; cnt++) {
		cmd->data[cnt].page = (srcaddr + cnt) >> 8;
		cmd->data[cnt].offset = (srcaddr + cnt) & 0xff;
	}
	reply->rceb.bEventType = i1480_CET_VS1;
	reply->rceb.wEvent = i1480_CMD_MPI_READ;
	result = i1480_cmd(i1480, "MPI-READ", sizeof(*cmd) + 2*size,
			sizeof(*reply) + 3*size);
	if (result < 0)
		goto out;
	if (reply->bResultCode != UWB_RC_RES_SUCCESS) {
		dev_err(i1480->dev, "MPI-READ: command execution failed: %d\n",
			reply->bResultCode);
		result = -EIO;
		goto out;
	}
	for (cnt = 0; cnt < size; cnt++) {
		if (reply->data[cnt].page != (srcaddr + cnt) >> 8)
			dev_err(i1480->dev, "MPI-READ: page inconsistency at "
				"index %u: expected 0x%02x, got 0x%02x\n", cnt,
				(srcaddr + cnt) >> 8, reply->data[cnt].page);
		if (reply->data[cnt].offset != ((srcaddr + cnt) & 0x00ff))
			dev_err(i1480->dev, "MPI-READ: offset inconsistency at "
				"index %u: expected 0x%02x, got 0x%02x\n", cnt,
				(srcaddr + cnt) & 0x00ff,
				reply->data[cnt].offset);
		data[cnt] = reply->data[cnt].value;
	}
	result = 0;
out:
	return result;
}


/**
 * Upload a PHY firmware, wait for it to start
 *
 * @i1480:     Device instance
 * @fw_name: Name of the file that contains the firmware
 *
 * We assume the MAC fw is up and running. This means we can use the
 * MPI interface to write the PHY firmware. Once done, we issue an
 * MBOA Reset, which will force the MAC to reset and reinitialize the
 * PHY. If that works, we are ready to go.
 *
 * Max packet size for the MPI write is 512, so the max buffer is 480
 * (which gives us 160 byte triads of MSB, LSB and VAL for the data).
 */
int i1480_phy_fw_upload(struct i1480 *i1480)
{
	int result;
	const struct firmware *fw;
	const char *data_itr, *data_top;
	const size_t MAX_BLK_SIZE = 480;	/* 160 triads */
	size_t data_size;
	u8 phy_stat;

	result = request_firmware(&fw, i1480->phy_fw_name, i1480->dev);
	if (result < 0)
		goto out;
	/* Loop writing data in chunks as big as possible until done. */
	for (data_itr = fw->data, data_top = data_itr + fw->size;
	     data_itr < data_top; data_itr += MAX_BLK_SIZE) {
		data_size = min(MAX_BLK_SIZE, (size_t) (data_top - data_itr));
		result = i1480_mpi_write(i1480, data_itr, data_size);
		if (result < 0)
			goto error_mpi_write;
	}
	/* Read MPI page 0, offset 6; if 0, PHY was initialized correctly. */
	result = i1480_mpi_read(i1480, &phy_stat, 0x0006, 1);
	if (result < 0) {
		dev_err(i1480->dev, "PHY: can't get status: %d\n", result);
		goto error_mpi_status;
	}
	if (phy_stat != 0) {
		result = -ENODEV;
		dev_info(i1480->dev, "error, PHY not ready: %u\n", phy_stat);
		goto error_phy_status;
	}
	dev_info(i1480->dev, "PHY fw '%s': uploaded\n", i1480->phy_fw_name);
error_phy_status:
error_mpi_status:
error_mpi_write:
	release_firmware(fw);
	if (result < 0)
		dev_err(i1480->dev, "PHY fw '%s': failed to upload (%d), "
			"power cycle device\n", i1480->phy_fw_name, result);
out:
	return result;
}
