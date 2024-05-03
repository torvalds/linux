// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/firmware.h>
#include <linux/limits.h>
#include <linux/mhi.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/overflow.h>
#include <linux/types.h>
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
#define SAHARA_NUM_TX_BUF		DIV_ROUND_UP(SAHARA_TRANSFER_MAX_SIZE,\
							SAHARA_PACKET_MAX_SIZE)
#define SAHARA_IMAGE_ID_NONE		U32_MAX

#define SAHARA_VERSION			2
#define SAHARA_SUCCESS			0

#define SAHARA_MODE_IMAGE_TX_PENDING	0x0
#define SAHARA_MODE_IMAGE_TX_COMPLETE	0x1
#define SAHARA_MODE_MEMORY_DEBUG	0x2
#define SAHARA_MODE_COMMAND		0x3

#define SAHARA_HELLO_LENGTH		0x30
#define SAHARA_READ_DATA_LENGTH		0x14
#define SAHARA_END_OF_IMAGE_LENGTH	0x10
#define SAHARA_DONE_LENGTH		0x8
#define SAHARA_RESET_LENGTH		0x8

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
	};
};

struct sahara_context {
	struct sahara_packet		*tx[SAHARA_NUM_TX_BUF];
	struct sahara_packet		*rx;
	struct work_struct		work;
	struct mhi_device		*mhi_dev;
	const char			**image_table;
	u32				table_size;
	u32				active_image_id;
	const struct firmware		*firmware;
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
	    le32_to_cpu(context->rx->hello.mode) != SAHARA_MODE_IMAGE_TX_COMPLETE) {
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

static void sahara_processing(struct work_struct *work)
{
	struct sahara_context *context = container_of(work, struct sahara_context, work);
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
	INIT_WORK(&context->work, sahara_processing);
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

	cancel_work_sync(&context->work);
	sahara_release_image(context);
	mhi_unprepare_from_transfer(mhi_dev);
}

static void sahara_mhi_ul_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
}

static void sahara_mhi_dl_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
	struct sahara_context *context = dev_get_drvdata(&mhi_dev->dev);

	if (!mhi_result->transaction_status)
		schedule_work(&context->work);
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
