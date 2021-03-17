// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016, Linaro Ltd
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>
#include <linux/rpmsg.h>
#include <linux/idr.h>
#include <linux/circ_buf.h>
#include <linux/soc/qcom/smem.h>
#include <linux/sizes.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <linux/list.h>

#include <linux/rpmsg/qcom_glink.h>

#include "qcom_glink_native.h"

#define FIFO_FULL_RESERVE 8
#define FIFO_ALIGNMENT 8
#define TX_BLOCKED_CMD_RESERVE 8 /* size of struct read_notif_request */

#define SMEM_GLINK_NATIVE_XPRT_DESCRIPTOR	478
#define SMEM_GLINK_NATIVE_XPRT_FIFO_0		479
#define SMEM_GLINK_NATIVE_XPRT_FIFO_1		480

struct glink_smem_pipe {
	struct qcom_glink_pipe native;

	__le32 *tail;
	__le32 *head;

	void *fifo;

	int remote_pid;
};

#define to_smem_pipe(p) container_of(p, struct glink_smem_pipe, native)

static size_t glink_smem_rx_avail(struct qcom_glink_pipe *np)
{
	struct glink_smem_pipe *pipe = to_smem_pipe(np);
	size_t len;
	void *fifo;
	u32 head;
	u32 tail;

	if (!pipe->fifo) {
		fifo = qcom_smem_get(pipe->remote_pid,
				     SMEM_GLINK_NATIVE_XPRT_FIFO_1, &len);
		if (IS_ERR(fifo)) {
			pr_err("failed to acquire RX fifo handle: %ld\n",
			       PTR_ERR(fifo));
			return 0;
		}

		pipe->fifo = fifo;
		pipe->native.length = len;
	}

	head = le32_to_cpu(*pipe->head);
	tail = le32_to_cpu(*pipe->tail);

	if (head < tail)
		return pipe->native.length - tail + head;
	else
		return head - tail;
}

static void glink_smem_rx_peak(struct qcom_glink_pipe *np,
			       void *data, unsigned int offset, size_t count)
{
	struct glink_smem_pipe *pipe = to_smem_pipe(np);
	size_t len;
	u32 tail;

	tail = le32_to_cpu(*pipe->tail);
	tail += offset;
	if (tail >= pipe->native.length)
		tail -= pipe->native.length;

	len = min_t(size_t, count, pipe->native.length - tail);
	if (len)
		memcpy_fromio(data, pipe->fifo + tail, len);

	if (len != count)
		memcpy_fromio(data + len, pipe->fifo, (count - len));
}

static void glink_smem_rx_advance(struct qcom_glink_pipe *np,
				  size_t count)
{
	struct glink_smem_pipe *pipe = to_smem_pipe(np);
	u32 tail;

	tail = le32_to_cpu(*pipe->tail);

	tail += count;
	if (tail >= pipe->native.length)
		tail -= pipe->native.length;

	*pipe->tail = cpu_to_le32(tail);
}

static size_t glink_smem_tx_avail(struct qcom_glink_pipe *np)
{
	struct glink_smem_pipe *pipe = to_smem_pipe(np);
	u32 head;
	u32 tail;
	u32 avail;

	head = le32_to_cpu(*pipe->head);
	tail = le32_to_cpu(*pipe->tail);

	if (tail <= head)
		avail = pipe->native.length - head + tail;
	else
		avail = tail - head;

	if (avail < (FIFO_FULL_RESERVE + TX_BLOCKED_CMD_RESERVE))
		avail = 0;
	else
		avail -= FIFO_FULL_RESERVE + TX_BLOCKED_CMD_RESERVE;

	return avail;
}

static unsigned int glink_smem_tx_write_one(struct glink_smem_pipe *pipe,
					    unsigned int head,
					    const void *data, size_t count)
{
	size_t len;

	len = min_t(size_t, count, pipe->native.length - head);
	if (len)
		memcpy(pipe->fifo + head, data, len);

	if (len != count)
		memcpy(pipe->fifo, data + len, count - len);

	head += count;
	if (head >= pipe->native.length)
		head -= pipe->native.length;

	return head;
}

static void glink_smem_tx_write(struct qcom_glink_pipe *glink_pipe,
				const void *hdr, size_t hlen,
				const void *data, size_t dlen)
{
	struct glink_smem_pipe *pipe = to_smem_pipe(glink_pipe);
	unsigned int head;

	head = le32_to_cpu(*pipe->head);

	head = glink_smem_tx_write_one(pipe, head, hdr, hlen);
	head = glink_smem_tx_write_one(pipe, head, data, dlen);

	/* Ensure head is always aligned to 8 bytes */
	head = ALIGN(head, 8);
	if (head >= pipe->native.length)
		head -= pipe->native.length;

	/* Ensure ordering of fifo and head update */
	wmb();

	*pipe->head = cpu_to_le32(head);
}

static void qcom_glink_smem_release(struct device *dev)
{
	kfree(dev);
}

struct qcom_glink *qcom_glink_smem_register(struct device *parent,
					    struct device_node *node)
{
	struct glink_smem_pipe *rx_pipe;
	struct glink_smem_pipe *tx_pipe;
	struct qcom_glink *glink;
	struct device *dev;
	u32 remote_pid;
	__le32 *descs;
	size_t size;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->parent = parent;
	dev->of_node = node;
	dev->release = qcom_glink_smem_release;
	dev_set_name(dev, "%s:%pOFn", dev_name(parent->parent), node);
	ret = device_register(dev);
	if (ret) {
		pr_err("failed to register glink edge\n");
		put_device(dev);
		return ERR_PTR(ret);
	}

	ret = of_property_read_u32(dev->of_node, "qcom,remote-pid",
				   &remote_pid);
	if (ret) {
		dev_err(dev, "failed to parse qcom,remote-pid\n");
		goto err_put_dev;
	}

	rx_pipe = devm_kzalloc(dev, sizeof(*rx_pipe), GFP_KERNEL);
	tx_pipe = devm_kzalloc(dev, sizeof(*tx_pipe), GFP_KERNEL);
	if (!rx_pipe || !tx_pipe) {
		ret = -ENOMEM;
		goto err_put_dev;
	}

	ret = qcom_smem_alloc(remote_pid,
			      SMEM_GLINK_NATIVE_XPRT_DESCRIPTOR, 32);
	if (ret && ret != -EEXIST) {
		dev_err(dev, "failed to allocate glink descriptors\n");
		goto err_put_dev;
	}

	descs = qcom_smem_get(remote_pid,
			      SMEM_GLINK_NATIVE_XPRT_DESCRIPTOR, &size);
	if (IS_ERR(descs)) {
		dev_err(dev, "failed to acquire xprt descriptor\n");
		ret = PTR_ERR(descs);
		goto err_put_dev;
	}

	if (size != 32) {
		dev_err(dev, "glink descriptor of invalid size\n");
		ret = -EINVAL;
		goto err_put_dev;
	}

	tx_pipe->tail = &descs[0];
	tx_pipe->head = &descs[1];
	rx_pipe->tail = &descs[2];
	rx_pipe->head = &descs[3];

	ret = qcom_smem_alloc(remote_pid, SMEM_GLINK_NATIVE_XPRT_FIFO_0,
			      SZ_16K);
	if (ret && ret != -EEXIST) {
		dev_err(dev, "failed to allocate TX fifo\n");
		goto err_put_dev;
	}

	tx_pipe->fifo = qcom_smem_get(remote_pid, SMEM_GLINK_NATIVE_XPRT_FIFO_0,
				      &tx_pipe->native.length);
	if (IS_ERR(tx_pipe->fifo)) {
		dev_err(dev, "failed to acquire TX fifo\n");
		ret = PTR_ERR(tx_pipe->fifo);
		goto err_put_dev;
	}

	rx_pipe->native.avail = glink_smem_rx_avail;
	rx_pipe->native.peak = glink_smem_rx_peak;
	rx_pipe->native.advance = glink_smem_rx_advance;
	rx_pipe->remote_pid = remote_pid;

	tx_pipe->native.avail = glink_smem_tx_avail;
	tx_pipe->native.write = glink_smem_tx_write;
	tx_pipe->remote_pid = remote_pid;

	*rx_pipe->tail = 0;
	*tx_pipe->head = 0;

	glink = qcom_glink_native_probe(dev,
					GLINK_FEATURE_INTENT_REUSE,
					&rx_pipe->native, &tx_pipe->native,
					false);
	if (IS_ERR(glink)) {
		ret = PTR_ERR(glink);
		goto err_put_dev;
	}

	return glink;

err_put_dev:
	device_unregister(dev);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(qcom_glink_smem_register);

void qcom_glink_smem_unregister(struct qcom_glink *glink)
{
	qcom_glink_native_remove(glink);
	qcom_glink_native_unregister(glink);
}
EXPORT_SYMBOL_GPL(qcom_glink_smem_unregister);

MODULE_AUTHOR("Bjorn Andersson <bjorn.andersson@linaro.org>");
MODULE_DESCRIPTION("Qualcomm GLINK SMEM driver");
MODULE_LICENSE("GPL v2");
