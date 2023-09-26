// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/circ_buf.h>
#include <linux/delay.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/rpmsg.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/rpmsg/qcom_glink.h>

#include "qcom_glink_native.h"
#include "qcom_glink_cma.h"

#define FIFO_FULL_RESERVE	8
#define FIFO_ALIGNMENT		8
#define TX_BLOCKED_CMD_RESERVE	8 /* size of struct read_notif_request */

#define FIFO_SIZE		0x4000

#define HDR_KEY_VALUE		0xdead

#define MAGIC_KEY_VALUE		0x24495043 /* "$IPC" */
#define MAGIC_KEY		0x0
#define BUFFER_SIZE		0x4

#define FIFO_0_START_OFFSET	0x1000
#define FIFO_0_BASE		0x8
#define FIFO_0_SIZE		0xc
#define FIFO_0_TAIL		0x10
#define FIFO_0_HEAD		0x14
#define FIFO_0_NOTIFY		0x18

#define FIFO_1_START_OFFSET	(FIFO_0_START_OFFSET + FIFO_SIZE)
#define FIFO_1_BASE		0x1c
#define FIFO_1_SIZE		0x20
#define FIFO_1_TAIL		0x24
#define FIFO_1_HEAD		0x28
#define FIFO_1_NOTIFY		0x2c

struct glink_cma_hdr {
	__le16 len;
	__le16 magic;
};

struct glink_cma_pipe {
	struct qcom_glink_pipe native;

	__le32 *tail;
	__le32 *head;

	void *fifo;
};

/**
 * glink_cma_dev - GLINK CMA fifo transport structure
 * @dev: device for this node.
 * @config: configuration setting for this transport.
 * @rx_pipe: RX CMA GLINK fifo specific info.
 * @tx_pipe: TX CMA GLINK fifo specific info.
 */
struct glink_cma_dev {
	struct device dev;
	struct glink_cma_config *config;
	struct glink_cma_pipe rx_pipe;
	struct glink_cma_pipe tx_pipe;
};
#define to_glink_cma_pipe(p) container_of(p, struct glink_cma_pipe, native)

static size_t glink_cma_rx_avail(struct qcom_glink_pipe *np)
{
	struct glink_cma_pipe *pipe = to_glink_cma_pipe(np);
	size_t len;
	u32 head;
	u32 tail;

	head = le32_to_cpu(*pipe->head);
	tail = le32_to_cpu(*pipe->tail);

	if (head < tail)
		len = pipe->native.length - tail + head;
	else
		len = head - tail;

	if (WARN_ON_ONCE(len > pipe->native.length))
		len = 0;

	return len;
}

static void glink_cma_rx_peek(struct qcom_glink_pipe *np, void *data,
				unsigned int offset, size_t count)
{
	struct glink_cma_pipe *pipe = to_glink_cma_pipe(np);
	size_t len;
	u32 tail;

	tail = le32_to_cpu(*pipe->tail);

	if (WARN_ON_ONCE(tail > pipe->native.length))
		return;

	tail += offset;
	if (tail >= pipe->native.length)
		tail %= pipe->native.length;

	len = min_t(size_t, count, pipe->native.length - tail);
	if (len)
		memcpy_fromio(data, pipe->fifo + tail, len);

	if (len != count)
		memcpy_fromio(data + len, pipe->fifo, (count - len));
}

static void glink_cma_rx_advance(struct qcom_glink_pipe *np, size_t count)
{
	struct glink_cma_pipe *pipe = to_glink_cma_pipe(np);
	u32 tail;

	tail = le32_to_cpu(*pipe->tail);

	tail += count;
	if (tail >= pipe->native.length)
		tail %= pipe->native.length;

	*pipe->tail = cpu_to_le32(tail);
}

static size_t glink_cma_tx_avail(struct qcom_glink_pipe *np)
{
	struct glink_cma_pipe *pipe = to_glink_cma_pipe(np);
	u32 avail;
	u32 head;
	u32 tail;

	head = le32_to_cpu(*pipe->head);
	tail = le32_to_cpu(*pipe->tail);

	if (tail <= head)
		avail = pipe->native.length - head + tail;
	else
		avail = tail - head;

	if (WARN_ON_ONCE(avail > pipe->native.length))
		return 0;

	if (avail < (FIFO_FULL_RESERVE + TX_BLOCKED_CMD_RESERVE))
		return 0;

	return avail - (FIFO_FULL_RESERVE + TX_BLOCKED_CMD_RESERVE);
}

static unsigned int glink_cma_tx_write_one(struct glink_cma_pipe *pipe, unsigned int head,
					    const void *data, size_t count)
{
	size_t len;

	if (WARN_ON_ONCE(head > pipe->native.length))
		return head;

	len = min_t(size_t, count, pipe->native.length - head);
	if (len)
		memcpy(pipe->fifo + head, data, len);

	if (len != count)
		memcpy(pipe->fifo, data + len, count - len);

	head += count;
	if (head >= pipe->native.length)
		head %= pipe->native.length;

	return head;
}

static void glink_cma_tx_write(struct qcom_glink_pipe *glink_pipe,
				const void *hdr, size_t hlen,
				const void *data, size_t dlen)
{
	struct glink_cma_pipe *pipe = to_glink_cma_pipe(glink_pipe);
	unsigned int head;

	head = le32_to_cpu(*pipe->head);

	head = glink_cma_tx_write_one(pipe, head, hdr, hlen);
	head = glink_cma_tx_write_one(pipe, head, data, dlen);

	/* Ensure head is always aligned to 8 bytes */
	head = ALIGN(head, 8);
	if (head >= pipe->native.length)
		head %= pipe->native.length;

	/* Ensure ordering of fifo and head update */
	wmb();

	*pipe->head = cpu_to_le32(head);
}

static void glink_cma_native_init(struct glink_cma_dev *gdev)
{
	struct qcom_glink_pipe *tx_native = &gdev->tx_pipe.native;
	struct qcom_glink_pipe *rx_native = &gdev->rx_pipe.native;

	tx_native->length = FIFO_SIZE;
	tx_native->avail = glink_cma_tx_avail;
	tx_native->write = glink_cma_tx_write;

	rx_native->length = FIFO_SIZE;
	rx_native->avail = glink_cma_rx_avail;
	rx_native->peak = glink_cma_rx_peek;
	rx_native->advance = glink_cma_rx_advance;
}

static int glink_cma_fifo_init(struct glink_cma_dev *gdev)
{
	struct glink_cma_pipe *rx_pipe = &gdev->rx_pipe;
	struct glink_cma_pipe *tx_pipe = &gdev->tx_pipe;
	struct glink_cma_config *config = gdev->config;
	u8 *descs = config->base;

	if (!descs)
		return -EINVAL;

	memset(descs, 0, FIFO_0_START_OFFSET);

	*(u32 *)(descs + MAGIC_KEY) = MAGIC_KEY_VALUE;
	*(u32 *)(descs + BUFFER_SIZE) = config->size;

	*(u32 *)(descs + FIFO_0_BASE) = FIFO_0_START_OFFSET;
	*(u32 *)(descs + FIFO_0_SIZE) = FIFO_SIZE;
	tx_pipe->fifo = (u32 *)(descs + FIFO_0_START_OFFSET);
	tx_pipe->tail = (u32 *)(descs + FIFO_0_TAIL);
	tx_pipe->head = (u32 *)(descs + FIFO_0_HEAD);

	*(u32 *)(descs + FIFO_1_BASE) = FIFO_1_START_OFFSET;
	*(u32 *)(descs + FIFO_1_SIZE) = FIFO_SIZE;
	rx_pipe->fifo = (u32 *)(descs + FIFO_1_START_OFFSET);
	rx_pipe->tail = (u32 *)(descs + FIFO_1_TAIL);
	rx_pipe->head = (u32 *)(descs + FIFO_1_HEAD);

	/* Reset respective index */
	*tx_pipe->head = 0;
	*rx_pipe->tail = 0;

	return 0;
}

static void qcom_glink_cma_release(struct device *dev)
{
	struct glink_cma_dev *gdev = dev_get_drvdata(dev);

	kfree(gdev);
}

struct qcom_glink *qcom_glink_cma_register(struct device *parent, struct device_node *node,
					struct glink_cma_config *config)
{
	struct glink_cma_dev *gdev;
	struct qcom_glink *glink;
	struct device *dev;
	int rc;

	if (!parent || !node || !config)
		return ERR_PTR(-EINVAL);

	gdev = kzalloc(sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return ERR_PTR(-ENOMEM);

	dev = &gdev->dev;
	dev->parent = parent;
	dev->of_node = node;
	dev->release = qcom_glink_cma_release;
	dev_set_name(dev, "%s:%pOFn", dev_name(parent->parent), node);
	rc = device_register(dev);
	if (rc) {
		pr_err("failed to register glink edge\n");
		put_device(dev);
		return ERR_PTR(rc);
	}

	dev_set_drvdata(dev, gdev);
	gdev->config = config;

	rc = glink_cma_fifo_init(gdev);
	if (rc)
		return ERR_PTR(rc);

	glink_cma_native_init(gdev);

	glink = qcom_glink_native_probe(dev, GLINK_FEATURE_INTENT_REUSE,
					&gdev->rx_pipe.native, &gdev->tx_pipe.native, false);
	if (IS_ERR(glink)) {
		rc = PTR_ERR(glink);
		goto err_put_dev;
	}

	rc = qcom_glink_native_start(glink);
	if (rc)
		goto err_put_dev;

	return glink;
err_put_dev:
	device_unregister(dev);

	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(qcom_glink_cma_register);

int qcom_glink_cma_start(struct qcom_glink *glink)
{
	return qcom_glink_native_start(glink);
}
EXPORT_SYMBOL_GPL(qcom_glink_cma_start);

void qcom_glink_cma_unregister(struct qcom_glink *glink)
{
	if (!glink)
		return;

	qcom_glink_native_remove(glink);
	qcom_glink_native_unregister(glink);
}
EXPORT_SYMBOL_GPL(qcom_glink_cma_unregister);
