// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/devcoredump.h>
#include <linux/firmware.h>
#include <linux/limits.h>
#include <linux/mhi.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/overflow.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include "sahara.h"

#define SAHARA_HELLO_CMD		0x1  /* Min protocol version 1.0 */
#define SAHARA_HELLO_RESP_CMD		0x2  /* Min protocol version 1.0 */
#define SAHARA_READ_DATA_CMD		0x3  /* Min protocol version 1.0 */
#define SAHARA_END_OF_IMAGE_CMD		0x4  /* Min protocol version 1.0 */
#define SAHARA_DONE_CMD			0x5  /* Min protocol version 1.0 */
#define SAHARA_DONE_RESP_CMD		0x6  /* Min protocol version 1.0 */
#define SAHARA_RESET_CMD		0x7  /* Min protocol version 1.0 */
#define SAHARA_RESET_RESP_CMD		0x8  /* Min protocol version 1.0 */
#define SAHARA_MEM_DEBUG_CMD		0x9  /* Min protocol version 2.0 */
#define SAHARA_MEM_READ_CMD		0xa  /* Min protocol version 2.0 */
#define SAHARA_CMD_READY_CMD		0xb  /* Min protocol version 2.1 */
#define SAHARA_SWITCH_MODE_CMD		0xc  /* Min protocol version 2.1 */
#define SAHARA_EXECUTE_CMD		0xd  /* Min protocol version 2.1 */
#define SAHARA_EXECUTE_RESP_CMD		0xe  /* Min protocol version 2.1 */
#define SAHARA_EXECUTE_DATA_CMD		0xf  /* Min protocol version 2.1 */
#define SAHARA_MEM_DEBUG64_CMD		0x10 /* Min protocol version 2.5 */
#define SAHARA_MEM_READ64_CMD		0x11 /* Min protocol version 2.5 */
#define SAHARA_READ_DATA64_CMD		0x12 /* Min protocol version 2.8 */
#define SAHARA_RESET_STATE_CMD		0x13 /* Min protocol version 2.9 */
#define SAHARA_WRITE_DATA_CMD		0x14 /* Min protocol version 3.0 */

#define SAHARA_PACKET_MAX_SIZE		0xffffU /* MHI_MAX_MTU */
#define SAHARA_TRANSFER_MAX_SIZE	0x80000
#define SAHARA_READ_MAX_SIZE		0xfff0U /* Avoid unaligned requests */
#define SAHARA_NUM_TX_BUF		DIV_ROUND_UP(SAHARA_TRANSFER_MAX_SIZE,\
							SAHARA_PACKET_MAX_SIZE)
#define SAHARA_IMAGE_ID_NONE		U32_MAX

#define SAHARA_VERSION			2
#define SAHARA_SUCCESS			0
#define SAHARA_TABLE_ENTRY_STR_LEN	20

#define SAHARA_MODE_IMAGE_TX_PENDING	0x0
#define SAHARA_MODE_IMAGE_TX_COMPLETE	0x1
#define SAHARA_MODE_MEMORY_DEBUG	0x2
#define SAHARA_MODE_COMMAND		0x3

#define SAHARA_HELLO_LENGTH		0x30
#define SAHARA_READ_DATA_LENGTH		0x14
#define SAHARA_END_OF_IMAGE_LENGTH	0x10
#define SAHARA_DONE_LENGTH		0x8
#define SAHARA_RESET_LENGTH		0x8
#define SAHARA_MEM_DEBUG64_LENGTH	0x18
#define SAHARA_MEM_READ64_LENGTH	0x18

struct sahara_packet {
	__le32 cmd;
	__le32 length;

	union {
		struct {
			__le32 version;
			__le32 version_compat;
			__le32 max_length;
			__le32 mode;
		} hello;
		struct {
			__le32 version;
			__le32 version_compat;
			__le32 status;
			__le32 mode;
		} hello_resp;
		struct {
			__le32 image;
			__le32 offset;
			__le32 length;
		} read_data;
		struct {
			__le32 image;
			__le32 status;
		} end_of_image;
		struct {
			__le64 table_address;
			__le64 table_length;
		} memory_debug64;
		struct {
			__le64 memory_address;
			__le64 memory_length;
		} memory_read64;
	};
};

struct sahara_debug_table_entry64 {
	__le64	type;
	__le64	address;
	__le64	length;
	char	description[SAHARA_TABLE_ENTRY_STR_LEN];
	char	filename[SAHARA_TABLE_ENTRY_STR_LEN];
};

struct sahara_dump_table_entry {
	u64	type;
	u64	address;
	u64	length;
	char	description[SAHARA_TABLE_ENTRY_STR_LEN];
	char	filename[SAHARA_TABLE_ENTRY_STR_LEN];
};

#define SAHARA_DUMP_V1_MAGIC 0x1234567890abcdef
#define SAHARA_DUMP_V1_VER   1
struct sahara_memory_dump_meta_v1 {
	u64	magic;
	u64	version;
	u64	dump_size;
	u64	table_size;
};

/*
 * Layout of crashdump provided to user via devcoredump
 *              +------------------------------------------+
 *              |         Crashdump Meta structure         |
 *              | type: struct sahara_memory_dump_meta_v1  |
 *              +------------------------------------------+
 *              |             Crashdump Table              |
 *              | type: array of struct                    |
 *              |       sahara_dump_table_entry            |
 *              |                                          |
 *              |                                          |
 *              +------------------------------------------+
 *              |                Crashdump                 |
 *              |                                          |
 *              |                                          |
 *              |                                          |
 *              |                                          |
 *              |                                          |
 *              +------------------------------------------+
 *
 * First is the metadata header. Userspace can use the magic number to verify
 * the content type, and then check the version for the rest of the format.
 * New versions should keep the magic number location/value, and version
 * location, but increment the version value.
 *
 * For v1, the metadata lists the size of the entire dump (header + table +
 * dump) and the size of the table. Then the dump image table, which describes
 * the contents of the dump. Finally all the images are listed in order, with
 * no deadspace in between. Userspace can use the sizes listed in the image
 * table to reconstruct the individual images.
 */

struct sahara_context {
	struct sahara_packet		*tx[SAHARA_NUM_TX_BUF];
	struct sahara_packet		*rx;
	struct work_struct		fw_work;
	struct work_struct		dump_work;
	struct mhi_device		*mhi_dev;
	const char			**image_table;
	u32				table_size;
	u32				active_image_id;
	const struct firmware		*firmware;
	u64				dump_table_address;
	u64				dump_table_length;
	size_t				rx_size;
	size_t				rx_size_requested;
	void				*mem_dump;
	size_t				mem_dump_sz;
	struct sahara_dump_table_entry	*dump_image;
	u64				dump_image_offset;
	void				*mem_dump_freespace;
	u64				dump_images_left;
	bool				is_mem_dump_mode;
};

static const char *aic100_image_table[] = {
	[1]  = "qcom/aic100/fw1.bin",
	[2]  = "qcom/aic100/fw2.bin",
	[4]  = "qcom/aic100/fw4.bin",
	[5]  = "qcom/aic100/fw5.bin",
	[6]  = "qcom/aic100/fw6.bin",
	[8]  = "qcom/aic100/fw8.bin",
	[9]  = "qcom/aic100/fw9.bin",
	[10] = "qcom/aic100/fw10.bin",
};

static int sahara_find_image(struct sahara_context *context, u32 image_id)
{
	int ret;

	if (image_id == context->active_image_id)
		return 0;

	if (context->active_image_id != SAHARA_IMAGE_ID_NONE) {
		dev_err(&context->mhi_dev->dev, "image id %d is not valid as %d is active\n",
			image_id, context->active_image_id);
		return -EINVAL;
	}

	if (image_id >= context->table_size || !context->image_table[image_id]) {
		dev_err(&context->mhi_dev->dev, "request for unknown image: %d\n", image_id);
		return -EINVAL;
	}

	/*
	 * This image might be optional. The device may continue without it.
	 * Only the device knows. Suppress error messages that could suggest an
	 * a problem when we were actually able to continue.
	 */
	ret = firmware_request_nowarn(&context->firmware,
				      context->image_table[image_id],
				      &context->mhi_dev->dev);
	if (ret) {
		dev_dbg(&context->mhi_dev->dev, "request for image id %d / file %s failed %d\n",
			image_id, context->image_table[image_id], ret);
		return ret;
	}

	context->active_image_id = image_id;

	return 0;
}

static void sahara_release_image(struct sahara_context *context)
{
	if (context->active_image_id != SAHARA_IMAGE_ID_NONE)
		release_firmware(context->firmware);
	context->active_image_id = SAHARA_IMAGE_ID_NONE;
}

static void sahara_send_reset(struct sahara_context *context)
{
	int ret;

	context->is_mem_dump_mode = false;

	context->tx[0]->cmd = cpu_to_le32(SAHARA_RESET_CMD);
	context->tx[0]->length = cpu_to_le32(SAHARA_RESET_LENGTH);

	ret = mhi_queue_buf(context->mhi_dev, DMA_TO_DEVICE, context->tx[0],
			    SAHARA_RESET_LENGTH, MHI_EOT);
	if (ret)
		dev_err(&context->mhi_dev->dev, "Unable to send reset response %d\n", ret);
}

static void sahara_hello(struct sahara_context *context)
{
	int ret;

	dev_dbg(&context->mhi_dev->dev,
		"HELLO cmd received. length:%d version:%d version_compat:%d max_length:%d mode:%d\n",
		le32_to_cpu(context->rx->length),
		le32_to_cpu(context->rx->hello.version),
		le32_to_cpu(context->rx->hello.version_compat),
		le32_to_cpu(context->rx->hello.max_length),
		le32_to_cpu(context->rx->hello.mode));

	if (le32_to_cpu(context->rx->length) != SAHARA_HELLO_LENGTH) {
		dev_err(&context->mhi_dev->dev, "Malformed hello packet - length %d\n",
			le32_to_cpu(context->rx->length));
		return;
	}
	if (le32_to_cpu(context->rx->hello.version) != SAHARA_VERSION) {
		dev_err(&context->mhi_dev->dev, "Unsupported hello packet - version %d\n",
			le32_to_cpu(context->rx->hello.version));
		return;
	}

	if (le32_to_cpu(context->rx->hello.mode) != SAHARA_MODE_IMAGE_TX_PENDING &&
	    le32_to_cpu(context->rx->hello.mode) != SAHARA_MODE_IMAGE_TX_COMPLETE &&
	    le32_to_cpu(context->rx->hello.mode) != SAHARA_MODE_MEMORY_DEBUG) {
		dev_err(&context->mhi_dev->dev, "Unsupported hello packet - mode %d\n",
			le32_to_cpu(context->rx->hello.mode));
		return;
	}

	context->tx[0]->cmd = cpu_to_le32(SAHARA_HELLO_RESP_CMD);
	context->tx[0]->length = cpu_to_le32(SAHARA_HELLO_LENGTH);
	context->tx[0]->hello_resp.version = cpu_to_le32(SAHARA_VERSION);
	context->tx[0]->hello_resp.version_compat = cpu_to_le32(SAHARA_VERSION);
	context->tx[0]->hello_resp.status = cpu_to_le32(SAHARA_SUCCESS);
	context->tx[0]->hello_resp.mode = context->rx->hello_resp.mode;

	ret = mhi_queue_buf(context->mhi_dev, DMA_TO_DEVICE, context->tx[0],
			    SAHARA_HELLO_LENGTH, MHI_EOT);
	if (ret)
		dev_err(&context->mhi_dev->dev, "Unable to send hello response %d\n", ret);
}

static void sahara_read_data(struct sahara_context *context)
{
	u32 image_id, data_offset, data_len, pkt_data_len;
	int ret;
	int i;

	dev_dbg(&context->mhi_dev->dev,
		"READ_DATA cmd received. length:%d image:%d offset:%d data_length:%d\n",
		le32_to_cpu(context->rx->length),
		le32_to_cpu(context->rx->read_data.image),
		le32_to_cpu(context->rx->read_data.offset),
		le32_to_cpu(context->rx->read_data.length));

	if (le32_to_cpu(context->rx->length) != SAHARA_READ_DATA_LENGTH) {
		dev_err(&context->mhi_dev->dev, "Malformed read_data packet - length %d\n",
			le32_to_cpu(context->rx->length));
		return;
	}

	image_id = le32_to_cpu(context->rx->read_data.image);
	data_offset = le32_to_cpu(context->rx->read_data.offset);
	data_len = le32_to_cpu(context->rx->read_data.length);

	ret = sahara_find_image(context, image_id);
	if (ret) {
		sahara_send_reset(context);
		return;
	}

	/*
	 * Image is released when the device is done with it via
	 * SAHARA_END_OF_IMAGE_CMD. sahara_send_reset() will either cause the
	 * device to retry the operation with a modification, or decide to be
	 * done with the image and trigger SAHARA_END_OF_IMAGE_CMD.
	 * release_image() is called from SAHARA_END_OF_IMAGE_CMD. processing
	 * and is not needed here on error.
	 */

	if (data_len > SAHARA_TRANSFER_MAX_SIZE) {
		dev_err(&context->mhi_dev->dev, "Malformed read_data packet - data len %d exceeds max xfer size %d\n",
			data_len, SAHARA_TRANSFER_MAX_SIZE);
		sahara_send_reset(context);
		return;
	}

	if (data_offset >= context->firmware->size) {
		dev_err(&context->mhi_dev->dev, "Malformed read_data packet - data offset %d exceeds file size %zu\n",
			data_offset, context->firmware->size);
		sahara_send_reset(context);
		return;
	}

	if (size_add(data_offset, data_len) > context->firmware->size) {
		dev_err(&context->mhi_dev->dev, "Malformed read_data packet - data offset %d and length %d exceeds file size %zu\n",
			data_offset, data_len, context->firmware->size);
		sahara_send_reset(context);
		return;
	}

	for (i = 0; i < SAHARA_NUM_TX_BUF && data_len; ++i) {
		pkt_data_len = min(data_len, SAHARA_PACKET_MAX_SIZE);

		memcpy(context->tx[i], &context->firmware->data[data_offset], pkt_data_len);

		data_offset += pkt_data_len;
		data_len -= pkt_data_len;

		ret = mhi_queue_buf(context->mhi_dev, DMA_TO_DEVICE,
				    context->tx[i], pkt_data_len,
				    !data_len ? MHI_EOT : MHI_CHAIN);
		if (ret) {
			dev_err(&context->mhi_dev->dev, "Unable to send read_data response %d\n",
				ret);
			return;
		}
	}
}

static void sahara_end_of_image(struct sahara_context *context)
{
	int ret;

	dev_dbg(&context->mhi_dev->dev,
		"END_OF_IMAGE cmd received. length:%d image:%d status:%d\n",
		le32_to_cpu(context->rx->length),
		le32_to_cpu(context->rx->end_of_image.image),
		le32_to_cpu(context->rx->end_of_image.status));

	if (le32_to_cpu(context->rx->length) != SAHARA_END_OF_IMAGE_LENGTH) {
		dev_err(&context->mhi_dev->dev, "Malformed end_of_image packet - length %d\n",
			le32_to_cpu(context->rx->length));
		return;
	}

	if (context->active_image_id != SAHARA_IMAGE_ID_NONE &&
	    le32_to_cpu(context->rx->end_of_image.image) != context->active_image_id) {
		dev_err(&context->mhi_dev->dev, "Malformed end_of_image packet - image %d is not the active image\n",
			le32_to_cpu(context->rx->end_of_image.image));
		return;
	}

	sahara_release_image(context);

	if (le32_to_cpu(context->rx->end_of_image.status))
		return;

	context->tx[0]->cmd = cpu_to_le32(SAHARA_DONE_CMD);
	context->tx[0]->length = cpu_to_le32(SAHARA_DONE_LENGTH);

	ret = mhi_queue_buf(context->mhi_dev, DMA_TO_DEVICE, context->tx[0],
			    SAHARA_DONE_LENGTH, MHI_EOT);
	if (ret)
		dev_dbg(&context->mhi_dev->dev, "Unable to send done response %d\n", ret);
}

static void sahara_memory_debug64(struct sahara_context *context)
{
	int ret;

	dev_dbg(&context->mhi_dev->dev,
		"MEMORY DEBUG64 cmd received. length:%d table_address:%#llx table_length:%#llx\n",
		le32_to_cpu(context->rx->length),
		le64_to_cpu(context->rx->memory_debug64.table_address),
		le64_to_cpu(context->rx->memory_debug64.table_length));

	if (le32_to_cpu(context->rx->length) != SAHARA_MEM_DEBUG64_LENGTH) {
		dev_err(&context->mhi_dev->dev, "Malformed memory debug64 packet - length %d\n",
			le32_to_cpu(context->rx->length));
		return;
	}

	context->dump_table_address = le64_to_cpu(context->rx->memory_debug64.table_address);
	context->dump_table_length = le64_to_cpu(context->rx->memory_debug64.table_length);

	if (context->dump_table_length % sizeof(struct sahara_debug_table_entry64) != 0 ||
	    !context->dump_table_length) {
		dev_err(&context->mhi_dev->dev, "Malformed memory debug64 packet - table length %lld\n",
			context->dump_table_length);
		return;
	}

	/*
	 * From this point, the protocol flips. We make memory_read requests to
	 * the device, and the device responds with the raw data. If the device
	 * has an error, it will send an End of Image command. First we need to
	 * request the memory dump table so that we know where all the pieces
	 * of the dump are that we can consume.
	 */

	context->is_mem_dump_mode = true;

	/*
	 * Assume that the table is smaller than our MTU so that we can read it
	 * in one shot. The spec does not put an upper limit on the table, but
	 * no known device will exceed this.
	 */
	if (context->dump_table_length > SAHARA_PACKET_MAX_SIZE) {
		dev_err(&context->mhi_dev->dev, "Memory dump table length %lld exceeds supported size. Discarding dump\n",
			context->dump_table_length);
		sahara_send_reset(context);
		return;
	}

	context->tx[0]->cmd = cpu_to_le32(SAHARA_MEM_READ64_CMD);
	context->tx[0]->length = cpu_to_le32(SAHARA_MEM_READ64_LENGTH);
	context->tx[0]->memory_read64.memory_address = cpu_to_le64(context->dump_table_address);
	context->tx[0]->memory_read64.memory_length = cpu_to_le64(context->dump_table_length);

	context->rx_size_requested = context->dump_table_length;

	ret = mhi_queue_buf(context->mhi_dev, DMA_TO_DEVICE, context->tx[0],
			    SAHARA_MEM_READ64_LENGTH, MHI_EOT);
	if (ret)
		dev_err(&context->mhi_dev->dev, "Unable to send read for dump table %d\n", ret);
}

static void sahara_processing(struct work_struct *work)
{
	struct sahara_context *context = container_of(work, struct sahara_context, fw_work);
	int ret;

	switch (le32_to_cpu(context->rx->cmd)) {
	case SAHARA_HELLO_CMD:
		sahara_hello(context);
		break;
	case SAHARA_READ_DATA_CMD:
		sahara_read_data(context);
		break;
	case SAHARA_END_OF_IMAGE_CMD:
		sahara_end_of_image(context);
		break;
	case SAHARA_DONE_RESP_CMD:
		/* Intentional do nothing as we don't need to exit an app */
		break;
	case SAHARA_RESET_RESP_CMD:
		/* Intentional do nothing as we don't need to exit an app */
		break;
	case SAHARA_MEM_DEBUG64_CMD:
		sahara_memory_debug64(context);
		break;
	default:
		dev_err(&context->mhi_dev->dev, "Unknown command %d\n",
			le32_to_cpu(context->rx->cmd));
		break;
	}

	ret = mhi_queue_buf(context->mhi_dev, DMA_FROM_DEVICE, context->rx,
			    SAHARA_PACKET_MAX_SIZE, MHI_EOT);
	if (ret)
		dev_err(&context->mhi_dev->dev, "Unable to requeue rx buf %d\n", ret);
}

static void sahara_parse_dump_table(struct sahara_context *context)
{
	struct sahara_dump_table_entry *image_out_table;
	struct sahara_debug_table_entry64 *dev_table;
	struct sahara_memory_dump_meta_v1 *dump_meta;
	u64 table_nents;
	u64 dump_length;
	int ret;
	u64 i;

	table_nents = context->dump_table_length / sizeof(*dev_table);
	context->dump_images_left = table_nents;
	dump_length = 0;

	dev_table = (struct sahara_debug_table_entry64 *)(context->rx);
	for (i = 0; i < table_nents; ++i) {
		/* Do not trust the device, ensure the strings are terminated */
		dev_table[i].description[SAHARA_TABLE_ENTRY_STR_LEN - 1] = 0;
		dev_table[i].filename[SAHARA_TABLE_ENTRY_STR_LEN - 1] = 0;

		dump_length = size_add(dump_length, le64_to_cpu(dev_table[i].length));
		if (dump_length == SIZE_MAX) {
			/* Discard the dump */
			sahara_send_reset(context);
			return;
		}

		dev_dbg(&context->mhi_dev->dev,
			"Memory dump table entry %lld type: %lld address: %#llx length: %#llx description: \"%s\" filename \"%s\"\n",
			i,
			le64_to_cpu(dev_table[i].type),
			le64_to_cpu(dev_table[i].address),
			le64_to_cpu(dev_table[i].length),
			dev_table[i].description,
			dev_table[i].filename);
	}

	dump_length = size_add(dump_length, sizeof(*dump_meta));
	if (dump_length == SIZE_MAX) {
		/* Discard the dump */
		sahara_send_reset(context);
		return;
	}
	dump_length = size_add(dump_length, size_mul(sizeof(*image_out_table), table_nents));
	if (dump_length == SIZE_MAX) {
		/* Discard the dump */
		sahara_send_reset(context);
		return;
	}

	context->mem_dump_sz = dump_length;
	context->mem_dump = vzalloc(dump_length);
	if (!context->mem_dump) {
		/* Discard the dump */
		sahara_send_reset(context);
		return;
	}

	/* Populate the dump metadata and table for userspace */
	dump_meta = context->mem_dump;
	dump_meta->magic = SAHARA_DUMP_V1_MAGIC;
	dump_meta->version = SAHARA_DUMP_V1_VER;
	dump_meta->dump_size = dump_length;
	dump_meta->table_size = context->dump_table_length;

	image_out_table = context->mem_dump + sizeof(*dump_meta);
	for (i = 0; i < table_nents; ++i) {
		image_out_table[i].type = le64_to_cpu(dev_table[i].type);
		image_out_table[i].address = le64_to_cpu(dev_table[i].address);
		image_out_table[i].length = le64_to_cpu(dev_table[i].length);
		strscpy(image_out_table[i].description, dev_table[i].description,
			SAHARA_TABLE_ENTRY_STR_LEN);
		strscpy(image_out_table[i].filename,
			dev_table[i].filename,
			SAHARA_TABLE_ENTRY_STR_LEN);
	}

	context->mem_dump_freespace = &image_out_table[i];

	/* Done parsing the table, switch to image dump mode */
	context->dump_table_length = 0;

	/* Request the first chunk of the first image */
	context->dump_image = &image_out_table[0];
	dump_length = min(context->dump_image->length, SAHARA_READ_MAX_SIZE);
	/* Avoid requesting EOI sized data so that we can identify errors */
	if (dump_length == SAHARA_END_OF_IMAGE_LENGTH)
		dump_length = SAHARA_END_OF_IMAGE_LENGTH / 2;

	context->dump_image_offset = dump_length;

	context->tx[0]->cmd = cpu_to_le32(SAHARA_MEM_READ64_CMD);
	context->tx[0]->length = cpu_to_le32(SAHARA_MEM_READ64_LENGTH);
	context->tx[0]->memory_read64.memory_address = cpu_to_le64(context->dump_image->address);
	context->tx[0]->memory_read64.memory_length = cpu_to_le64(dump_length);

	context->rx_size_requested = dump_length;

	ret = mhi_queue_buf(context->mhi_dev, DMA_TO_DEVICE, context->tx[0],
			    SAHARA_MEM_READ64_LENGTH, MHI_EOT);
	if (ret)
		dev_err(&context->mhi_dev->dev, "Unable to send read for dump content %d\n", ret);
}

static void sahara_parse_dump_image(struct sahara_context *context)
{
	u64 dump_length;
	int ret;

	memcpy(context->mem_dump_freespace, context->rx, context->rx_size);
	context->mem_dump_freespace += context->rx_size;

	if (context->dump_image_offset >= context->dump_image->length) {
		/* Need to move to next image */
		context->dump_image++;
		context->dump_images_left--;
		context->dump_image_offset = 0;

		if (!context->dump_images_left) {
			/* Dump done */
			dev_coredumpv(context->mhi_dev->mhi_cntrl->cntrl_dev,
				      context->mem_dump,
				      context->mem_dump_sz,
				      GFP_KERNEL);
			context->mem_dump = NULL;
			sahara_send_reset(context);
			return;
		}
	}

	/* Get next image chunk */
	dump_length = context->dump_image->length - context->dump_image_offset;
	dump_length = min(dump_length, SAHARA_READ_MAX_SIZE);
	/* Avoid requesting EOI sized data so that we can identify errors */
	if (dump_length == SAHARA_END_OF_IMAGE_LENGTH)
		dump_length = SAHARA_END_OF_IMAGE_LENGTH / 2;

	context->tx[0]->cmd = cpu_to_le32(SAHARA_MEM_READ64_CMD);
	context->tx[0]->length = cpu_to_le32(SAHARA_MEM_READ64_LENGTH);
	context->tx[0]->memory_read64.memory_address =
		cpu_to_le64(context->dump_image->address + context->dump_image_offset);
	context->tx[0]->memory_read64.memory_length = cpu_to_le64(dump_length);

	context->dump_image_offset += dump_length;
	context->rx_size_requested = dump_length;

	ret = mhi_queue_buf(context->mhi_dev, DMA_TO_DEVICE, context->tx[0],
			    SAHARA_MEM_READ64_LENGTH, MHI_EOT);
	if (ret)
		dev_err(&context->mhi_dev->dev,
			"Unable to send read for dump content %d\n", ret);
}

static void sahara_dump_processing(struct work_struct *work)
{
	struct sahara_context *context = container_of(work, struct sahara_context, dump_work);
	int ret;

	/*
	 * We should get the expected raw data, but if the device has an error
	 * it is supposed to send EOI with an error code.
	 */
	if (context->rx_size != context->rx_size_requested &&
	    context->rx_size != SAHARA_END_OF_IMAGE_LENGTH) {
		dev_err(&context->mhi_dev->dev,
			"Unexpected response to read_data. Expected size: %#zx got: %#zx\n",
			context->rx_size_requested,
			context->rx_size);
		goto error;
	}

	if (context->rx_size == SAHARA_END_OF_IMAGE_LENGTH &&
	    le32_to_cpu(context->rx->cmd) == SAHARA_END_OF_IMAGE_CMD) {
		dev_err(&context->mhi_dev->dev,
			"Unexpected EOI response to read_data. Status: %d\n",
			le32_to_cpu(context->rx->end_of_image.status));
		goto error;
	}

	if (context->rx_size == SAHARA_END_OF_IMAGE_LENGTH &&
	    le32_to_cpu(context->rx->cmd) != SAHARA_END_OF_IMAGE_CMD) {
		dev_err(&context->mhi_dev->dev,
			"Invalid EOI response to read_data. CMD: %d\n",
			le32_to_cpu(context->rx->cmd));
		goto error;
	}

	/*
	 * Need to know if we received the dump table, or part of a dump image.
	 * Since we get raw data, we cannot tell from the data itself. Instead,
	 * we use the stored dump_table_length, which we zero after we read and
	 * process the entire table.
	 */
	if (context->dump_table_length)
		sahara_parse_dump_table(context);
	else
		sahara_parse_dump_image(context);

	ret = mhi_queue_buf(context->mhi_dev, DMA_FROM_DEVICE, context->rx,
			    SAHARA_PACKET_MAX_SIZE, MHI_EOT);
	if (ret)
		dev_err(&context->mhi_dev->dev, "Unable to requeue rx buf %d\n", ret);

	return;

error:
	vfree(context->mem_dump);
	context->mem_dump = NULL;
	sahara_send_reset(context);
}

static int sahara_mhi_probe(struct mhi_device *mhi_dev, const struct mhi_device_id *id)
{
	struct sahara_context *context;
	int ret;
	int i;

	context = devm_kzalloc(&mhi_dev->dev, sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	context->rx = devm_kzalloc(&mhi_dev->dev, SAHARA_PACKET_MAX_SIZE, GFP_KERNEL);
	if (!context->rx)
		return -ENOMEM;

	/*
	 * AIC100 defines SAHARA_TRANSFER_MAX_SIZE as the largest value it
	 * will request for READ_DATA. This is larger than
	 * SAHARA_PACKET_MAX_SIZE, and we need 9x SAHARA_PACKET_MAX_SIZE to
	 * cover SAHARA_TRANSFER_MAX_SIZE. When the remote side issues a
	 * READ_DATA, it requires a transfer of the exact size requested. We
	 * can use MHI_CHAIN to link multiple buffers into a single transfer
	 * but the remote side will not consume the buffers until it sees an
	 * EOT, thus we need to allocate enough buffers to put in the tx fifo
	 * to cover an entire READ_DATA request of the max size.
	 */
	for (i = 0; i < SAHARA_NUM_TX_BUF; ++i) {
		context->tx[i] = devm_kzalloc(&mhi_dev->dev, SAHARA_PACKET_MAX_SIZE, GFP_KERNEL);
		if (!context->tx[i])
			return -ENOMEM;
	}

	context->mhi_dev = mhi_dev;
	INIT_WORK(&context->fw_work, sahara_processing);
	INIT_WORK(&context->dump_work, sahara_dump_processing);
	context->image_table = aic100_image_table;
	context->table_size = ARRAY_SIZE(aic100_image_table);
	context->active_image_id = SAHARA_IMAGE_ID_NONE;
	dev_set_drvdata(&mhi_dev->dev, context);

	ret = mhi_prepare_for_transfer(mhi_dev);
	if (ret)
		return ret;

	ret = mhi_queue_buf(mhi_dev, DMA_FROM_DEVICE, context->rx, SAHARA_PACKET_MAX_SIZE, MHI_EOT);
	if (ret) {
		mhi_unprepare_from_transfer(mhi_dev);
		return ret;
	}

	return 0;
}

static void sahara_mhi_remove(struct mhi_device *mhi_dev)
{
	struct sahara_context *context = dev_get_drvdata(&mhi_dev->dev);

	cancel_work_sync(&context->fw_work);
	cancel_work_sync(&context->dump_work);
	vfree(context->mem_dump);
	sahara_release_image(context);
	mhi_unprepare_from_transfer(mhi_dev);
}

static void sahara_mhi_ul_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
}

static void sahara_mhi_dl_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
	struct sahara_context *context = dev_get_drvdata(&mhi_dev->dev);

	if (!mhi_result->transaction_status) {
		context->rx_size = mhi_result->bytes_xferd;
		if (context->is_mem_dump_mode)
			schedule_work(&context->dump_work);
		else
			schedule_work(&context->fw_work);
	}

}

static const struct mhi_device_id sahara_mhi_match_table[] = {
	{ .chan = "QAIC_SAHARA", },
	{},
};

static struct mhi_driver sahara_mhi_driver = {
	.id_table = sahara_mhi_match_table,
	.remove = sahara_mhi_remove,
	.probe = sahara_mhi_probe,
	.ul_xfer_cb = sahara_mhi_ul_xfer_cb,
	.dl_xfer_cb = sahara_mhi_dl_xfer_cb,
	.driver = {
		.name = "sahara",
	},
};

int sahara_register(void)
{
	return mhi_driver_register(&sahara_mhi_driver);
}

void sahara_unregister(void)
{
	mhi_driver_unregister(&sahara_mhi_driver);
}
