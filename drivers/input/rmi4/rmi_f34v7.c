// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, Zodiac Inflight Innovations
 * Copyright (c) 2007-2016, Synaptics Incorporated
 * Copyright (C) 2012 Alexandra Chin <alexandra.chin@tw.synaptics.com>
 * Copyright (C) 2012 Scott Lin <scott.lin@tw.synaptics.com>
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/rmi.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <asm/unaligned.h>

#include "rmi_driver.h"
#include "rmi_f34.h"

static int rmi_f34v7_read_flash_status(struct f34_data *f34)
{
	u8 status;
	u8 command;
	int ret;

	ret = rmi_read_block(f34->fn->rmi_dev,
			f34->fn->fd.data_base_addr + f34->v7.off.flash_status,
			&status,
			sizeof(status));
	if (ret < 0) {
		rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
			"%s: Error %d reading flash status\n", __func__, ret);
		return ret;
	}

	f34->v7.in_bl_mode = status >> 7;
	f34->v7.flash_status = status & 0x1f;

	if (f34->v7.flash_status != 0x00) {
		dev_err(&f34->fn->dev, "%s: status=%d, command=0x%02x\n",
			__func__, f34->v7.flash_status, f34->v7.command);
	}

	ret = rmi_read_block(f34->fn->rmi_dev,
			f34->fn->fd.data_base_addr + f34->v7.off.flash_cmd,
			&command,
			sizeof(command));
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to read flash command\n",
			__func__);
		return ret;
	}

	f34->v7.command = command;

	return 0;
}

static int rmi_f34v7_wait_for_idle(struct f34_data *f34, int timeout_ms)
{
	unsigned long timeout;

	timeout = msecs_to_jiffies(timeout_ms);

	if (!wait_for_completion_timeout(&f34->v7.cmd_done, timeout)) {
		dev_warn(&f34->fn->dev, "%s: Timed out waiting for idle status\n",
			 __func__);
		return -ETIMEDOUT;
	}

	return 0;
}

static int rmi_f34v7_write_command_single_transaction(struct f34_data *f34,
						      u8 cmd)
{
	int ret;
	u8 base;
	struct f34v7_data_1_5 data_1_5;

	base = f34->fn->fd.data_base_addr;

	memset(&data_1_5, 0, sizeof(data_1_5));

	switch (cmd) {
	case v7_CMD_ERASE_ALL:
		data_1_5.partition_id = CORE_CODE_PARTITION;
		data_1_5.command = CMD_V7_ERASE_AP;
		break;
	case v7_CMD_ERASE_UI_FIRMWARE:
		data_1_5.partition_id = CORE_CODE_PARTITION;
		data_1_5.command = CMD_V7_ERASE;
		break;
	case v7_CMD_ERASE_BL_CONFIG:
		data_1_5.partition_id = GLOBAL_PARAMETERS_PARTITION;
		data_1_5.command = CMD_V7_ERASE;
		break;
	case v7_CMD_ERASE_UI_CONFIG:
		data_1_5.partition_id = CORE_CONFIG_PARTITION;
		data_1_5.command = CMD_V7_ERASE;
		break;
	case v7_CMD_ERASE_DISP_CONFIG:
		data_1_5.partition_id = DISPLAY_CONFIG_PARTITION;
		data_1_5.command = CMD_V7_ERASE;
		break;
	case v7_CMD_ERASE_FLASH_CONFIG:
		data_1_5.partition_id = FLASH_CONFIG_PARTITION;
		data_1_5.command = CMD_V7_ERASE;
		break;
	case v7_CMD_ERASE_GUEST_CODE:
		data_1_5.partition_id = GUEST_CODE_PARTITION;
		data_1_5.command = CMD_V7_ERASE;
		break;
	case v7_CMD_ENABLE_FLASH_PROG:
		data_1_5.partition_id = BOOTLOADER_PARTITION;
		data_1_5.command = CMD_V7_ENTER_BL;
		break;
	}

	data_1_5.payload[0] = f34->bootloader_id[0];
	data_1_5.payload[1] = f34->bootloader_id[1];

	ret = rmi_write_block(f34->fn->rmi_dev,
			base + f34->v7.off.partition_id,
			&data_1_5, sizeof(data_1_5));
	if (ret < 0) {
		dev_err(&f34->fn->dev,
			"%s: Failed to write single transaction command\n",
			__func__);
		return ret;
	}

	return 0;
}

static int rmi_f34v7_write_command(struct f34_data *f34, u8 cmd)
{
	int ret;
	u8 base;
	u8 command;

	base = f34->fn->fd.data_base_addr;

	switch (cmd) {
	case v7_CMD_WRITE_FW:
	case v7_CMD_WRITE_CONFIG:
	case v7_CMD_WRITE_GUEST_CODE:
		command = CMD_V7_WRITE;
		break;
	case v7_CMD_READ_CONFIG:
		command = CMD_V7_READ;
		break;
	case v7_CMD_ERASE_ALL:
		command = CMD_V7_ERASE_AP;
		break;
	case v7_CMD_ERASE_UI_FIRMWARE:
	case v7_CMD_ERASE_BL_CONFIG:
	case v7_CMD_ERASE_UI_CONFIG:
	case v7_CMD_ERASE_DISP_CONFIG:
	case v7_CMD_ERASE_FLASH_CONFIG:
	case v7_CMD_ERASE_GUEST_CODE:
		command = CMD_V7_ERASE;
		break;
	case v7_CMD_ENABLE_FLASH_PROG:
		command = CMD_V7_ENTER_BL;
		break;
	default:
		dev_err(&f34->fn->dev, "%s: Invalid command 0x%02x\n",
			__func__, cmd);
		return -EINVAL;
	}

	f34->v7.command = command;

	switch (cmd) {
	case v7_CMD_ERASE_ALL:
	case v7_CMD_ERASE_UI_FIRMWARE:
	case v7_CMD_ERASE_BL_CONFIG:
	case v7_CMD_ERASE_UI_CONFIG:
	case v7_CMD_ERASE_DISP_CONFIG:
	case v7_CMD_ERASE_FLASH_CONFIG:
	case v7_CMD_ERASE_GUEST_CODE:
	case v7_CMD_ENABLE_FLASH_PROG:
		ret = rmi_f34v7_write_command_single_transaction(f34, cmd);
		if (ret < 0)
			return ret;
		else
			return 0;
	default:
		break;
	}

	rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev, "%s: writing cmd %02X\n",
		__func__, command);

	ret = rmi_write_block(f34->fn->rmi_dev,
			base + f34->v7.off.flash_cmd,
			&command, sizeof(command));
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to write flash command\n",
			__func__);
		return ret;
	}

	return 0;
}

static int rmi_f34v7_write_partition_id(struct f34_data *f34, u8 cmd)
{
	int ret;
	u8 base;
	u8 partition;

	base = f34->fn->fd.data_base_addr;

	switch (cmd) {
	case v7_CMD_WRITE_FW:
		partition = CORE_CODE_PARTITION;
		break;
	case v7_CMD_WRITE_CONFIG:
	case v7_CMD_READ_CONFIG:
		if (f34->v7.config_area == v7_UI_CONFIG_AREA)
			partition = CORE_CONFIG_PARTITION;
		else if (f34->v7.config_area == v7_DP_CONFIG_AREA)
			partition = DISPLAY_CONFIG_PARTITION;
		else if (f34->v7.config_area == v7_PM_CONFIG_AREA)
			partition = GUEST_SERIALIZATION_PARTITION;
		else if (f34->v7.config_area == v7_BL_CONFIG_AREA)
			partition = GLOBAL_PARAMETERS_PARTITION;
		else if (f34->v7.config_area == v7_FLASH_CONFIG_AREA)
			partition = FLASH_CONFIG_PARTITION;
		break;
	case v7_CMD_WRITE_GUEST_CODE:
		partition = GUEST_CODE_PARTITION;
		break;
	case v7_CMD_ERASE_ALL:
		partition = CORE_CODE_PARTITION;
		break;
	case v7_CMD_ERASE_BL_CONFIG:
		partition = GLOBAL_PARAMETERS_PARTITION;
		break;
	case v7_CMD_ERASE_UI_CONFIG:
		partition = CORE_CONFIG_PARTITION;
		break;
	case v7_CMD_ERASE_DISP_CONFIG:
		partition = DISPLAY_CONFIG_PARTITION;
		break;
	case v7_CMD_ERASE_FLASH_CONFIG:
		partition = FLASH_CONFIG_PARTITION;
		break;
	case v7_CMD_ERASE_GUEST_CODE:
		partition = GUEST_CODE_PARTITION;
		break;
	case v7_CMD_ENABLE_FLASH_PROG:
		partition = BOOTLOADER_PARTITION;
		break;
	default:
		dev_err(&f34->fn->dev, "%s: Invalid command 0x%02x\n",
			__func__, cmd);
		return -EINVAL;
	}

	ret = rmi_write_block(f34->fn->rmi_dev,
			base + f34->v7.off.partition_id,
			&partition, sizeof(partition));
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to write partition ID\n",
			__func__);
		return ret;
	}

	return 0;
}

static int rmi_f34v7_read_partition_table(struct f34_data *f34)
{
	int ret;
	unsigned long timeout;
	u8 base;
	__le16 length;
	u16 block_number = 0;

	base = f34->fn->fd.data_base_addr;

	f34->v7.config_area = v7_FLASH_CONFIG_AREA;

	ret = rmi_f34v7_write_partition_id(f34, v7_CMD_READ_CONFIG);
	if (ret < 0)
		return ret;

	ret = rmi_write_block(f34->fn->rmi_dev,
			base + f34->v7.off.block_number,
			&block_number, sizeof(block_number));
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to write block number\n",
			__func__);
		return ret;
	}

	put_unaligned_le16(f34->v7.flash_config_length, &length);

	ret = rmi_write_block(f34->fn->rmi_dev,
			base + f34->v7.off.transfer_length,
			&length, sizeof(length));
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to write transfer length\n",
			__func__);
		return ret;
	}

	init_completion(&f34->v7.cmd_done);

	ret = rmi_f34v7_write_command(f34, v7_CMD_READ_CONFIG);
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to write command\n",
			__func__);
		return ret;
	}

	timeout = msecs_to_jiffies(F34_WRITE_WAIT_MS);
	while (time_before(jiffies, timeout)) {
		usleep_range(5000, 6000);
		rmi_f34v7_read_flash_status(f34);

		if (f34->v7.command == v7_CMD_IDLE &&
		    f34->v7.flash_status == 0x00) {
			break;
		}
	}

	ret = rmi_read_block(f34->fn->rmi_dev,
			base + f34->v7.off.payload,
			f34->v7.read_config_buf,
			f34->v7.partition_table_bytes);
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to read block data\n",
			__func__);
		return ret;
	}

	return 0;
}

static void rmi_f34v7_parse_partition_table(struct f34_data *f34,
					    const void *partition_table,
					    struct block_count *blkcount,
					    struct physical_address *phyaddr)
{
	int i;
	int index;
	u16 partition_length;
	u16 physical_address;
	const struct partition_table *ptable;

	for (i = 0; i < f34->v7.partitions; i++) {
		index = i * 8 + 2;
		ptable = partition_table + index;
		partition_length = le16_to_cpu(ptable->partition_length);
		physical_address = le16_to_cpu(ptable->start_physical_address);
		rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
			"%s: Partition entry %d: %*ph\n",
			__func__, i, sizeof(struct partition_table), ptable);
		switch (ptable->partition_id & 0x1f) {
		case CORE_CODE_PARTITION:
			blkcount->ui_firmware = partition_length;
			phyaddr->ui_firmware = physical_address;
			rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
				"%s: Core code block count: %d\n",
				__func__, blkcount->ui_firmware);
			break;
		case CORE_CONFIG_PARTITION:
			blkcount->ui_config = partition_length;
			phyaddr->ui_config = physical_address;
			rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
				"%s: Core config block count: %d\n",
				__func__, blkcount->ui_config);
			break;
		case DISPLAY_CONFIG_PARTITION:
			blkcount->dp_config = partition_length;
			phyaddr->dp_config = physical_address;
			rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
				"%s: Display config block count: %d\n",
				__func__, blkcount->dp_config);
			break;
		case FLASH_CONFIG_PARTITION:
			blkcount->fl_config = partition_length;
			rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
				"%s: Flash config block count: %d\n",
				__func__, blkcount->fl_config);
			break;
		case GUEST_CODE_PARTITION:
			blkcount->guest_code = partition_length;
			phyaddr->guest_code = physical_address;
			rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
				"%s: Guest code block count: %d\n",
				__func__, blkcount->guest_code);
			break;
		case GUEST_SERIALIZATION_PARTITION:
			blkcount->pm_config = partition_length;
			rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
				"%s: Guest serialization block count: %d\n",
				__func__, blkcount->pm_config);
			break;
		case GLOBAL_PARAMETERS_PARTITION:
			blkcount->bl_config = partition_length;
			rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
				"%s: Global parameters block count: %d\n",
				__func__, blkcount->bl_config);
			break;
		case DEVICE_CONFIG_PARTITION:
			blkcount->lockdown = partition_length;
			rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
				"%s: Device config block count: %d\n",
				__func__, blkcount->lockdown);
			break;
		}
	}
}

static int rmi_f34v7_read_queries_bl_version(struct f34_data *f34)
{
	int ret;
	u8 base;
	int offset;
	u8 query_0;
	struct f34v7_query_1_7 query_1_7;

	base = f34->fn->fd.query_base_addr;

	ret = rmi_read_block(f34->fn->rmi_dev,
			base,
			&query_0,
			sizeof(query_0));
	if (ret < 0) {
		dev_err(&f34->fn->dev,
			"%s: Failed to read query 0\n", __func__);
		return ret;
	}

	offset = (query_0 & 0x7) + 1;

	ret = rmi_read_block(f34->fn->rmi_dev,
			base + offset,
			&query_1_7,
			sizeof(query_1_7));
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to read queries 1 to 7\n",
			__func__);
		return ret;
	}

	f34->bootloader_id[0] = query_1_7.bl_minor_revision;
	f34->bootloader_id[1] = query_1_7.bl_major_revision;

	rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev, "Bootloader V%d.%d\n",
		f34->bootloader_id[1], f34->bootloader_id[0]);

	return 0;
}

static int rmi_f34v7_read_queries(struct f34_data *f34)
{
	int ret;
	int i;
	u8 base;
	int offset;
	u8 *ptable;
	u8 query_0;
	struct f34v7_query_1_7 query_1_7;

	base = f34->fn->fd.query_base_addr;

	ret = rmi_read_block(f34->fn->rmi_dev,
			base,
			&query_0,
			sizeof(query_0));
	if (ret < 0) {
		dev_err(&f34->fn->dev,
			"%s: Failed to read query 0\n", __func__);
		return ret;
	}

	offset = (query_0 & 0x07) + 1;

	ret = rmi_read_block(f34->fn->rmi_dev,
			base + offset,
			&query_1_7,
			sizeof(query_1_7));
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to read queries 1 to 7\n",
			__func__);
		return ret;
	}

	f34->bootloader_id[0] = query_1_7.bl_minor_revision;
	f34->bootloader_id[1] = query_1_7.bl_major_revision;

	f34->v7.block_size = le16_to_cpu(query_1_7.block_size);
	f34->v7.flash_config_length =
			le16_to_cpu(query_1_7.flash_config_length);
	f34->v7.payload_length = le16_to_cpu(query_1_7.payload_length);

	rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev, "%s: f34->v7.block_size = %d\n",
		 __func__, f34->v7.block_size);

	f34->v7.off.flash_status = V7_FLASH_STATUS_OFFSET;
	f34->v7.off.partition_id = V7_PARTITION_ID_OFFSET;
	f34->v7.off.block_number = V7_BLOCK_NUMBER_OFFSET;
	f34->v7.off.transfer_length = V7_TRANSFER_LENGTH_OFFSET;
	f34->v7.off.flash_cmd = V7_COMMAND_OFFSET;
	f34->v7.off.payload = V7_PAYLOAD_OFFSET;

	f34->v7.has_display_cfg = query_1_7.partition_support[1] & HAS_DISP_CFG;
	f34->v7.has_guest_code =
			query_1_7.partition_support[1] & HAS_GUEST_CODE;

	if (query_0 & HAS_CONFIG_ID) {
		u8 f34_ctrl[CONFIG_ID_SIZE];

		ret = rmi_read_block(f34->fn->rmi_dev,
				f34->fn->fd.control_base_addr,
				f34_ctrl,
				sizeof(f34_ctrl));
		if (ret)
			return ret;

		/* Eat leading zeros */
		for (i = 0; i < sizeof(f34_ctrl) - 1 && !f34_ctrl[i]; i++)
			/* Empty */;

		snprintf(f34->configuration_id, sizeof(f34->configuration_id),
			 "%*phN", (int)sizeof(f34_ctrl) - i, f34_ctrl + i);

		rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev, "Configuration ID: %s\n",
			f34->configuration_id);
	}

	f34->v7.partitions = 0;
	for (i = 0; i < sizeof(query_1_7.partition_support); i++)
		f34->v7.partitions += hweight8(query_1_7.partition_support[i]);

	rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev, "%s: Supported partitions: %*ph\n",
		__func__, sizeof(query_1_7.partition_support),
		query_1_7.partition_support);


	f34->v7.partition_table_bytes = f34->v7.partitions * 8 + 2;

	f34->v7.read_config_buf = devm_kzalloc(&f34->fn->dev,
			f34->v7.partition_table_bytes,
			GFP_KERNEL);
	if (!f34->v7.read_config_buf) {
		f34->v7.read_config_buf_size = 0;
		return -ENOMEM;
	}

	f34->v7.read_config_buf_size = f34->v7.partition_table_bytes;
	ptable = f34->v7.read_config_buf;

	ret = rmi_f34v7_read_partition_table(f34);
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to read partition table\n",
				__func__);
		return ret;
	}

	rmi_f34v7_parse_partition_table(f34, ptable,
					&f34->v7.blkcount, &f34->v7.phyaddr);

	return 0;
}

static int rmi_f34v7_check_ui_firmware_size(struct f34_data *f34)
{
	u16 block_count;

	block_count = f34->v7.img.ui_firmware.size / f34->v7.block_size;
	f34->update_size += block_count;

	if (block_count != f34->v7.blkcount.ui_firmware) {
		dev_err(&f34->fn->dev,
			"UI firmware size mismatch: %d != %d\n",
			block_count, f34->v7.blkcount.ui_firmware);
		return -EINVAL;
	}

	return 0;
}

static int rmi_f34v7_check_ui_config_size(struct f34_data *f34)
{
	u16 block_count;

	block_count = f34->v7.img.ui_config.size / f34->v7.block_size;
	f34->update_size += block_count;

	if (block_count != f34->v7.blkcount.ui_config) {
		dev_err(&f34->fn->dev, "UI config size mismatch\n");
		return -EINVAL;
	}

	return 0;
}

static int rmi_f34v7_check_dp_config_size(struct f34_data *f34)
{
	u16 block_count;

	block_count = f34->v7.img.dp_config.size / f34->v7.block_size;
	f34->update_size += block_count;

	if (block_count != f34->v7.blkcount.dp_config) {
		dev_err(&f34->fn->dev, "Display config size mismatch\n");
		return -EINVAL;
	}

	return 0;
}

static int rmi_f34v7_check_guest_code_size(struct f34_data *f34)
{
	u16 block_count;

	block_count = f34->v7.img.guest_code.size / f34->v7.block_size;
	f34->update_size += block_count;

	if (block_count != f34->v7.blkcount.guest_code) {
		dev_err(&f34->fn->dev, "Guest code size mismatch\n");
		return -EINVAL;
	}

	return 0;
}

static int rmi_f34v7_check_bl_config_size(struct f34_data *f34)
{
	u16 block_count;

	block_count = f34->v7.img.bl_config.size / f34->v7.block_size;
	f34->update_size += block_count;

	if (block_count != f34->v7.blkcount.bl_config) {
		dev_err(&f34->fn->dev, "Bootloader config size mismatch\n");
		return -EINVAL;
	}

	return 0;
}

static int rmi_f34v7_erase_config(struct f34_data *f34)
{
	int ret;

	dev_info(&f34->fn->dev, "Erasing config...\n");

	init_completion(&f34->v7.cmd_done);

	switch (f34->v7.config_area) {
	case v7_UI_CONFIG_AREA:
		ret = rmi_f34v7_write_command(f34, v7_CMD_ERASE_UI_CONFIG);
		if (ret < 0)
			return ret;
		break;
	case v7_DP_CONFIG_AREA:
		ret = rmi_f34v7_write_command(f34, v7_CMD_ERASE_DISP_CONFIG);
		if (ret < 0)
			return ret;
		break;
	case v7_BL_CONFIG_AREA:
		ret = rmi_f34v7_write_command(f34, v7_CMD_ERASE_BL_CONFIG);
		if (ret < 0)
			return ret;
		break;
	}

	ret = rmi_f34v7_wait_for_idle(f34, F34_ERASE_WAIT_MS);
	if (ret < 0)
		return ret;

	return 0;
}

static int rmi_f34v7_erase_guest_code(struct f34_data *f34)
{
	int ret;

	dev_info(&f34->fn->dev, "Erasing guest code...\n");

	init_completion(&f34->v7.cmd_done);

	ret = rmi_f34v7_write_command(f34, v7_CMD_ERASE_GUEST_CODE);
	if (ret < 0)
		return ret;

	ret = rmi_f34v7_wait_for_idle(f34, F34_ERASE_WAIT_MS);
	if (ret < 0)
		return ret;

	return 0;
}

static int rmi_f34v7_erase_all(struct f34_data *f34)
{
	int ret;

	dev_info(&f34->fn->dev, "Erasing firmware...\n");

	init_completion(&f34->v7.cmd_done);

	ret = rmi_f34v7_write_command(f34, v7_CMD_ERASE_UI_FIRMWARE);
	if (ret < 0)
		return ret;

	ret = rmi_f34v7_wait_for_idle(f34, F34_ERASE_WAIT_MS);
	if (ret < 0)
		return ret;

	f34->v7.config_area = v7_UI_CONFIG_AREA;
	ret = rmi_f34v7_erase_config(f34);
	if (ret < 0)
		return ret;

	if (f34->v7.has_display_cfg) {
		f34->v7.config_area = v7_DP_CONFIG_AREA;
		ret = rmi_f34v7_erase_config(f34);
		if (ret < 0)
			return ret;
	}

	if (f34->v7.new_partition_table && f34->v7.has_guest_code) {
		ret = rmi_f34v7_erase_guest_code(f34);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int rmi_f34v7_read_blocks(struct f34_data *f34,
				 u16 block_cnt, u8 command)
{
	int ret;
	u8 base;
	__le16 length;
	u16 transfer;
	u16 max_transfer;
	u16 remaining = block_cnt;
	u16 block_number = 0;
	u16 index = 0;

	base = f34->fn->fd.data_base_addr;

	ret = rmi_f34v7_write_partition_id(f34, command);
	if (ret < 0)
		return ret;

	ret = rmi_write_block(f34->fn->rmi_dev,
			base + f34->v7.off.block_number,
			&block_number, sizeof(block_number));
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to write block number\n",
			__func__);
		return ret;
	}

	max_transfer = min(f34->v7.payload_length,
			   (u16)(PAGE_SIZE / f34->v7.block_size));

	do {
		transfer = min(remaining, max_transfer);
		put_unaligned_le16(transfer, &length);

		ret = rmi_write_block(f34->fn->rmi_dev,
				base + f34->v7.off.transfer_length,
				&length, sizeof(length));
		if (ret < 0) {
			dev_err(&f34->fn->dev,
				"%s: Write transfer length fail (%d remaining)\n",
				__func__, remaining);
			return ret;
		}

		init_completion(&f34->v7.cmd_done);

		ret = rmi_f34v7_write_command(f34, command);
		if (ret < 0)
			return ret;

		ret = rmi_f34v7_wait_for_idle(f34, F34_ENABLE_WAIT_MS);
		if (ret < 0)
			return ret;

		ret = rmi_read_block(f34->fn->rmi_dev,
				base + f34->v7.off.payload,
				&f34->v7.read_config_buf[index],
				transfer * f34->v7.block_size);
		if (ret < 0) {
			dev_err(&f34->fn->dev,
				"%s: Read block failed (%d blks remaining)\n",
				__func__, remaining);
			return ret;
		}

		index += (transfer * f34->v7.block_size);
		remaining -= transfer;
	} while (remaining);

	return 0;
}

static int rmi_f34v7_write_f34v7_blocks(struct f34_data *f34,
					const void *block_ptr, u16 block_cnt,
					u8 command)
{
	int ret;
	u8 base;
	__le16 length;
	u16 transfer;
	u16 max_transfer;
	u16 remaining = block_cnt;
	u16 block_number = 0;

	base = f34->fn->fd.data_base_addr;

	ret = rmi_f34v7_write_partition_id(f34, command);
	if (ret < 0)
		return ret;

	ret = rmi_write_block(f34->fn->rmi_dev,
			base + f34->v7.off.block_number,
			&block_number, sizeof(block_number));
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to write block number\n",
			__func__);
		return ret;
	}

	if (f34->v7.payload_length > (PAGE_SIZE / f34->v7.block_size))
		max_transfer = PAGE_SIZE / f34->v7.block_size;
	else
		max_transfer = f34->v7.payload_length;

	do {
		transfer = min(remaining, max_transfer);
		put_unaligned_le16(transfer, &length);

		init_completion(&f34->v7.cmd_done);

		ret = rmi_write_block(f34->fn->rmi_dev,
				base + f34->v7.off.transfer_length,
				&length, sizeof(length));
		if (ret < 0) {
			dev_err(&f34->fn->dev,
				"%s: Write transfer length fail (%d remaining)\n",
				__func__, remaining);
			return ret;
		}

		ret = rmi_f34v7_write_command(f34, command);
		if (ret < 0)
			return ret;

		ret = rmi_write_block(f34->fn->rmi_dev,
				base + f34->v7.off.payload,
				block_ptr, transfer * f34->v7.block_size);
		if (ret < 0) {
			dev_err(&f34->fn->dev,
				"%s: Failed writing data (%d blks remaining)\n",
				__func__, remaining);
			return ret;
		}

		ret = rmi_f34v7_wait_for_idle(f34, F34_ENABLE_WAIT_MS);
		if (ret < 0)
			return ret;

		block_ptr += (transfer * f34->v7.block_size);
		remaining -= transfer;
		f34->update_progress += transfer;
		f34->update_status = (f34->update_progress * 100) /
				     f34->update_size;
	} while (remaining);

	return 0;
}

static int rmi_f34v7_write_config(struct f34_data *f34)
{
	return rmi_f34v7_write_f34v7_blocks(f34, f34->v7.config_data,
					    f34->v7.config_block_count,
					    v7_CMD_WRITE_CONFIG);
}

static int rmi_f34v7_write_ui_config(struct f34_data *f34)
{
	f34->v7.config_area = v7_UI_CONFIG_AREA;
	f34->v7.config_data = f34->v7.img.ui_config.data;
	f34->v7.config_size = f34->v7.img.ui_config.size;
	f34->v7.config_block_count = f34->v7.config_size / f34->v7.block_size;

	return rmi_f34v7_write_config(f34);
}

static int rmi_f34v7_write_dp_config(struct f34_data *f34)
{
	f34->v7.config_area = v7_DP_CONFIG_AREA;
	f34->v7.config_data = f34->v7.img.dp_config.data;
	f34->v7.config_size = f34->v7.img.dp_config.size;
	f34->v7.config_block_count = f34->v7.config_size / f34->v7.block_size;

	return rmi_f34v7_write_config(f34);
}

static int rmi_f34v7_write_guest_code(struct f34_data *f34)
{
	return rmi_f34v7_write_f34v7_blocks(f34, f34->v7.img.guest_code.data,
					    f34->v7.img.guest_code.size /
							f34->v7.block_size,
					    v7_CMD_WRITE_GUEST_CODE);
}

static int rmi_f34v7_write_flash_config(struct f34_data *f34)
{
	int ret;

	f34->v7.config_area = v7_FLASH_CONFIG_AREA;
	f34->v7.config_data = f34->v7.img.fl_config.data;
	f34->v7.config_size = f34->v7.img.fl_config.size;
	f34->v7.config_block_count = f34->v7.config_size / f34->v7.block_size;

	if (f34->v7.config_block_count != f34->v7.blkcount.fl_config) {
		dev_err(&f34->fn->dev, "%s: Flash config size mismatch\n",
			__func__);
		return -EINVAL;
	}

	init_completion(&f34->v7.cmd_done);

	ret = rmi_f34v7_write_command(f34, v7_CMD_ERASE_FLASH_CONFIG);
	if (ret < 0)
		return ret;

	rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
		"%s: Erase flash config command written\n", __func__);

	ret = rmi_f34v7_wait_for_idle(f34, F34_WRITE_WAIT_MS);
	if (ret < 0)
		return ret;

	ret = rmi_f34v7_write_config(f34);
	if (ret < 0)
		return ret;

	return 0;
}

static int rmi_f34v7_write_partition_table(struct f34_data *f34)
{
	u16 block_count;
	int ret;

	block_count = f34->v7.blkcount.bl_config;
	f34->v7.config_area = v7_BL_CONFIG_AREA;
	f34->v7.config_size = f34->v7.block_size * block_count;
	devm_kfree(&f34->fn->dev, f34->v7.read_config_buf);
	f34->v7.read_config_buf = devm_kzalloc(&f34->fn->dev,
					       f34->v7.config_size, GFP_KERNEL);
	if (!f34->v7.read_config_buf) {
		f34->v7.read_config_buf_size = 0;
		return -ENOMEM;
	}

	f34->v7.read_config_buf_size = f34->v7.config_size;

	ret = rmi_f34v7_read_blocks(f34, block_count, v7_CMD_READ_CONFIG);
	if (ret < 0)
		return ret;

	ret = rmi_f34v7_erase_config(f34);
	if (ret < 0)
		return ret;

	ret = rmi_f34v7_write_flash_config(f34);
	if (ret < 0)
		return ret;

	f34->v7.config_area = v7_BL_CONFIG_AREA;
	f34->v7.config_data = f34->v7.read_config_buf;
	f34->v7.config_size = f34->v7.img.bl_config.size;
	f34->v7.config_block_count = f34->v7.config_size / f34->v7.block_size;

	ret = rmi_f34v7_write_config(f34);
	if (ret < 0)
		return ret;

	return 0;
}

static int rmi_f34v7_write_firmware(struct f34_data *f34)
{
	u16 blk_count;

	blk_count = f34->v7.img.ui_firmware.size / f34->v7.block_size;

	return rmi_f34v7_write_f34v7_blocks(f34, f34->v7.img.ui_firmware.data,
					    blk_count, v7_CMD_WRITE_FW);
}

static void rmi_f34v7_compare_partition_tables(struct f34_data *f34)
{
	if (f34->v7.phyaddr.ui_firmware != f34->v7.img.phyaddr.ui_firmware) {
		f34->v7.new_partition_table = true;
		return;
	}

	if (f34->v7.phyaddr.ui_config != f34->v7.img.phyaddr.ui_config) {
		f34->v7.new_partition_table = true;
		return;
	}

	if (f34->v7.has_display_cfg &&
	    f34->v7.phyaddr.dp_config != f34->v7.img.phyaddr.dp_config) {
		f34->v7.new_partition_table = true;
		return;
	}

	if (f34->v7.has_guest_code &&
	    f34->v7.phyaddr.guest_code != f34->v7.img.phyaddr.guest_code) {
		f34->v7.new_partition_table = true;
		return;
	}

	f34->v7.new_partition_table = false;
}

static void rmi_f34v7_parse_img_header_10_bl_container(struct f34_data *f34,
						       const void *image)
{
	int i;
	int num_of_containers;
	unsigned int addr;
	unsigned int container_id;
	unsigned int length;
	const void *content;
	const struct container_descriptor *descriptor;

	num_of_containers = f34->v7.img.bootloader.size / 4 - 1;

	for (i = 1; i <= num_of_containers; i++) {
		addr = get_unaligned_le32(f34->v7.img.bootloader.data + i * 4);
		descriptor = image + addr;
		container_id = le16_to_cpu(descriptor->container_id);
		content = image + le32_to_cpu(descriptor->content_address);
		length = le32_to_cpu(descriptor->content_length);
		switch (container_id) {
		case BL_CONFIG_CONTAINER:
		case GLOBAL_PARAMETERS_CONTAINER:
			f34->v7.img.bl_config.data = content;
			f34->v7.img.bl_config.size = length;
			break;
		case BL_LOCKDOWN_INFO_CONTAINER:
		case DEVICE_CONFIG_CONTAINER:
			f34->v7.img.lockdown.data = content;
			f34->v7.img.lockdown.size = length;
			break;
		default:
			break;
		}
	}
}

static void rmi_f34v7_parse_image_header_10(struct f34_data *f34)
{
	unsigned int i;
	unsigned int num_of_containers;
	unsigned int addr;
	unsigned int offset;
	unsigned int container_id;
	unsigned int length;
	const void *image = f34->v7.image;
	const u8 *content;
	const struct container_descriptor *descriptor;
	const struct image_header_10 *header = image;

	f34->v7.img.checksum = le32_to_cpu(header->checksum);

	rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev, "%s: f34->v7.img.checksum=%X\n",
		__func__, f34->v7.img.checksum);

	/* address of top level container */
	offset = le32_to_cpu(header->top_level_container_start_addr);
	descriptor = image + offset;

	/* address of top level container content */
	offset = le32_to_cpu(descriptor->content_address);
	num_of_containers = le32_to_cpu(descriptor->content_length) / 4;

	for (i = 0; i < num_of_containers; i++) {
		addr = get_unaligned_le32(image + offset);
		offset += 4;
		descriptor = image + addr;
		container_id = le16_to_cpu(descriptor->container_id);
		content = image + le32_to_cpu(descriptor->content_address);
		length = le32_to_cpu(descriptor->content_length);

		rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
			"%s: container_id=%d, length=%d\n", __func__,
			container_id, length);

		switch (container_id) {
		case UI_CONTAINER:
		case CORE_CODE_CONTAINER:
			f34->v7.img.ui_firmware.data = content;
			f34->v7.img.ui_firmware.size = length;
			break;
		case UI_CONFIG_CONTAINER:
		case CORE_CONFIG_CONTAINER:
			f34->v7.img.ui_config.data = content;
			f34->v7.img.ui_config.size = length;
			break;
		case BL_CONTAINER:
			f34->v7.img.bl_version = *content;
			f34->v7.img.bootloader.data = content;
			f34->v7.img.bootloader.size = length;
			rmi_f34v7_parse_img_header_10_bl_container(f34, image);
			break;
		case GUEST_CODE_CONTAINER:
			f34->v7.img.contains_guest_code = true;
			f34->v7.img.guest_code.data = content;
			f34->v7.img.guest_code.size = length;
			break;
		case DISPLAY_CONFIG_CONTAINER:
			f34->v7.img.contains_display_cfg = true;
			f34->v7.img.dp_config.data = content;
			f34->v7.img.dp_config.size = length;
			break;
		case FLASH_CONFIG_CONTAINER:
			f34->v7.img.contains_flash_config = true;
			f34->v7.img.fl_config.data = content;
			f34->v7.img.fl_config.size = length;
			break;
		case GENERAL_INFORMATION_CONTAINER:
			f34->v7.img.contains_firmware_id = true;
			f34->v7.img.firmware_id =
				get_unaligned_le32(content + 4);
			break;
		default:
			break;
		}
	}
}

static int rmi_f34v7_parse_image_info(struct f34_data *f34)
{
	const struct image_header_10 *header = f34->v7.image;

	memset(&f34->v7.img, 0x00, sizeof(f34->v7.img));

	rmi_dbg(RMI_DEBUG_FN, &f34->fn->dev,
		"%s: header->major_header_version = %d\n",
		__func__, header->major_header_version);

	switch (header->major_header_version) {
	case IMAGE_HEADER_VERSION_10:
		rmi_f34v7_parse_image_header_10(f34);
		break;
	default:
		dev_err(&f34->fn->dev, "Unsupported image file format %02X\n",
			header->major_header_version);
		return -EINVAL;
	}

	if (!f34->v7.img.contains_flash_config) {
		dev_err(&f34->fn->dev, "%s: No flash config in fw image\n",
			__func__);
		return -EINVAL;
	}

	rmi_f34v7_parse_partition_table(f34, f34->v7.img.fl_config.data,
			&f34->v7.img.blkcount, &f34->v7.img.phyaddr);

	rmi_f34v7_compare_partition_tables(f34);

	return 0;
}

int rmi_f34v7_do_reflash(struct f34_data *f34, const struct firmware *fw)
{
	int ret;

	f34->fn->rmi_dev->driver->set_irq_bits(f34->fn->rmi_dev,
					       f34->fn->irq_mask);

	rmi_f34v7_read_queries_bl_version(f34);

	f34->v7.image = fw->data;
	f34->update_progress = 0;
	f34->update_size = 0;

	ret = rmi_f34v7_parse_image_info(f34);
	if (ret < 0)
		goto fail;

	if (!f34->v7.new_partition_table) {
		ret = rmi_f34v7_check_ui_firmware_size(f34);
		if (ret < 0)
			goto fail;

		ret = rmi_f34v7_check_ui_config_size(f34);
		if (ret < 0)
			goto fail;

		if (f34->v7.has_display_cfg &&
		    f34->v7.img.contains_display_cfg) {
			ret = rmi_f34v7_check_dp_config_size(f34);
			if (ret < 0)
				goto fail;
		}

		if (f34->v7.has_guest_code && f34->v7.img.contains_guest_code) {
			ret = rmi_f34v7_check_guest_code_size(f34);
			if (ret < 0)
				goto fail;
		}
	} else {
		ret = rmi_f34v7_check_bl_config_size(f34);
		if (ret < 0)
			goto fail;
	}

	ret = rmi_f34v7_erase_all(f34);
	if (ret < 0)
		goto fail;

	if (f34->v7.new_partition_table) {
		ret = rmi_f34v7_write_partition_table(f34);
		if (ret < 0)
			goto fail;
		dev_info(&f34->fn->dev, "%s: Partition table programmed\n",
			 __func__);
	}

	dev_info(&f34->fn->dev, "Writing firmware (%d bytes)...\n",
		 f34->v7.img.ui_firmware.size);

	ret = rmi_f34v7_write_firmware(f34);
	if (ret < 0)
		goto fail;

	dev_info(&f34->fn->dev, "Writing config (%d bytes)...\n",
		 f34->v7.img.ui_config.size);

	f34->v7.config_area = v7_UI_CONFIG_AREA;
	ret = rmi_f34v7_write_ui_config(f34);
	if (ret < 0)
		goto fail;

	if (f34->v7.has_display_cfg && f34->v7.img.contains_display_cfg) {
		dev_info(&f34->fn->dev, "Writing display config...\n");

		ret = rmi_f34v7_write_dp_config(f34);
		if (ret < 0)
			goto fail;
	}

	if (f34->v7.new_partition_table) {
		if (f34->v7.has_guest_code && f34->v7.img.contains_guest_code) {
			dev_info(&f34->fn->dev, "Writing guest code...\n");

			ret = rmi_f34v7_write_guest_code(f34);
			if (ret < 0)
				goto fail;
		}
	}

fail:
	return ret;
}

static int rmi_f34v7_enter_flash_prog(struct f34_data *f34)
{
	int ret;

	f34->fn->rmi_dev->driver->set_irq_bits(f34->fn->rmi_dev, f34->fn->irq_mask);

	ret = rmi_f34v7_read_flash_status(f34);
	if (ret < 0)
		return ret;

	if (f34->v7.in_bl_mode)
		return 0;

	init_completion(&f34->v7.cmd_done);

	ret = rmi_f34v7_write_command(f34, v7_CMD_ENABLE_FLASH_PROG);
	if (ret < 0)
		return ret;

	ret = rmi_f34v7_wait_for_idle(f34, F34_ENABLE_WAIT_MS);
	if (ret < 0)
		return ret;

	return 0;
}

int rmi_f34v7_start_reflash(struct f34_data *f34, const struct firmware *fw)
{
	int ret = 0;

	f34->fn->rmi_dev->driver->set_irq_bits(f34->fn->rmi_dev, f34->fn->irq_mask);

	f34->v7.config_area = v7_UI_CONFIG_AREA;
	f34->v7.image = fw->data;

	ret = rmi_f34v7_parse_image_info(f34);
	if (ret < 0)
		goto exit;

	if (!f34->v7.force_update && f34->v7.new_partition_table) {
		dev_err(&f34->fn->dev, "%s: Partition table mismatch\n",
				__func__);
		ret = -EINVAL;
		goto exit;
	}

	dev_info(&f34->fn->dev, "Firmware image OK\n");

	ret = rmi_f34v7_read_flash_status(f34);
	if (ret < 0)
		goto exit;

	if (f34->v7.in_bl_mode) {
		dev_info(&f34->fn->dev, "%s: Device in bootloader mode\n",
				__func__);
	}

	rmi_f34v7_enter_flash_prog(f34);

	return 0;

exit:
	return ret;
}

int rmi_f34v7_probe(struct f34_data *f34)
{
	int ret;

	/* Read bootloader version */
	ret = rmi_read_block(f34->fn->rmi_dev,
			f34->fn->fd.query_base_addr + V7_BOOTLOADER_ID_OFFSET,
			f34->bootloader_id,
			sizeof(f34->bootloader_id));
	if (ret < 0) {
		dev_err(&f34->fn->dev, "%s: Failed to read bootloader ID\n",
			__func__);
		return ret;
	}

	if (f34->bootloader_id[1] == '5') {
		f34->bl_version = 5;
	} else if (f34->bootloader_id[1] == '6') {
		f34->bl_version = 6;
	} else if (f34->bootloader_id[1] == 7) {
		f34->bl_version = 7;
	} else if (f34->bootloader_id[1] == 8) {
		f34->bl_version = 8;
	} else {
		dev_err(&f34->fn->dev,
			"%s: Unrecognized bootloader version: %d (%c) %d (%c)\n",
			__func__,
			f34->bootloader_id[0], f34->bootloader_id[0],
			f34->bootloader_id[1], f34->bootloader_id[1]);
		return -EINVAL;
	}

	memset(&f34->v7.blkcount, 0x00, sizeof(f34->v7.blkcount));
	memset(&f34->v7.phyaddr, 0x00, sizeof(f34->v7.phyaddr));

	init_completion(&f34->v7.cmd_done);

	ret = rmi_f34v7_read_queries(f34);
	if (ret < 0)
		return ret;

	f34->v7.force_update = true;
	return 0;
}
