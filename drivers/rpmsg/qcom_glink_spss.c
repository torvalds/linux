// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include <linux/soc/qcom/smem.h>
#include <linux/rpmsg/qcom_glink.h>

#include "qcom_glink_native.h"

#define FIFO_FULL_RESERVE 8
#define FIFO_ALIGNMENT 8
#define TX_BLOCKED_CMD_RESERVE 8 /* size of struct read_notif_request */

#define SMEM_GLINK_NATIVE_XPRT_DESCRIPTOR	478
#define SPSS_TX_FIFO_SIZE SZ_2K
#define SPSS_RX_FIFO_SIZE SZ_2K

struct glink_spss_cfg {
	__le32 tx_tail;
	__le32 tx_head;
	__le32 tx_fifo_size;
	__le32 rx_tail;
	__le32 rx_head;
	__le32 rx_fifo_size;
};

struct glink_spss_pipe {
	struct qcom_glink_pipe native;

	__le32 *tail;
	__le32 *head;

	void *fifo;

	int remote_pid;
};

#define to_spss_pipe(p) container_of(p, struct glink_spss_pipe, native)

static void glink_spss_reset(struct qcom_glink_pipe *np)
{
	struct glink_spss_pipe *pipe = to_spss_pipe(np);

	*pipe->head = cpu_to_le32(0);
	*pipe->tail = cpu_to_le32(0);
}

static size_t glink_spss_rx_avail(struct qcom_glink_pipe *np)
{
	struct glink_spss_pipe *pipe = to_spss_pipe(np);
	u32 head;
	u32 tail;

	head = le32_to_cpu(*pipe->head);
	tail = le32_to_cpu(*pipe->tail);

	if (head < tail)
		return pipe->native.length - tail + head;
	else
		return head - tail;
}

static void glink_spss_rx_peak(struct qcom_glink_pipe *np,
			       void *data, unsigned int offset, size_t count)
{
	struct glink_spss_pipe *pipe = to_spss_pipe(np);
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
		memcpy_fromio(data + len, pipe->fifo, count - len);
}

static void glink_spss_rx_advance(struct qcom_glink_pipe *np,
				  size_t count)
{
	struct glink_spss_pipe *pipe = to_spss_pipe(np);
	u32 tail;

	tail = le32_to_cpu(*pipe->tail);

	tail += count;
	if (tail >= pipe->native.length)
		tail -= pipe->native.length;

	*pipe->tail = cpu_to_le32(tail);
}

static size_t glink_spss_tx_avail(struct qcom_glink_pipe *np)
{
	struct glink_spss_pipe *pipe = to_spss_pipe(np);
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

static unsigned int glink_spss_tx_write_one(struct glink_spss_pipe *pipe,
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

static void glink_spss_tx_write(struct qcom_glink_pipe *glink_pipe,
				const void *hdr, size_t hlen,
				const void *data, size_t dlen)
{
	struct glink_spss_pipe *pipe = to_spss_pipe(glink_pipe);
	unsigned int head;

	head = le32_to_cpu(*pipe->head);

	head = glink_spss_tx_write_one(pipe, head, hdr, hlen);
	head = glink_spss_tx_write_one(pipe, head, data, dlen);

	/* Ensure head is always aligned to 8 bytes */
	head = ALIGN(head, 8);
	if (head >= pipe->native.length)
		head -= pipe->native.length;

	/* Ensure ordering of fifo and head update */
	wmb();

	*pipe->head = cpu_to_le32(head);
}

static void qcom_glink_spss_release(struct device *dev)
{
	kfree(dev);
}

static int glink_spss_advertise_cfg(struct device *dev,
				    u32 size, phys_addr_t addr)
{
	struct device_node *np = dev->of_node;
	__le64 __iomem *spss_addr;
	__le32 __iomem *spss_size;
	struct resource addr_r;
	struct resource size_r;
	int addr_idx;
	int size_idx;

	addr_idx = of_property_match_string(np, "reg-names", "qcom,spss-addr");
	size_idx = of_property_match_string(np, "reg-names", "qcom,spss-size");
	if (addr_idx < 0 || size_idx < 0) {
		dev_err(dev, "failed to find location registers\n");
		return -EINVAL;
	}

	if (of_address_to_resource(np, addr_idx, &addr_r))
		return -ENOMEM;
	spss_addr = ioremap(addr_r.start, resource_size(&addr_r));
	if (IS_ERR_OR_NULL(spss_addr)) {
		dev_err(dev, "failed to map spss addr resource\n");
		return -ENOMEM;
	}

	if (of_address_to_resource(np, size_idx, &size_r)) {
		iounmap(spss_addr);
		return -ENOMEM;
	}
	spss_size = ioremap(size_r.start, resource_size(&size_r));
	if (IS_ERR_OR_NULL(spss_size)) {
		iounmap(spss_addr);
		dev_err(dev, "failed to map spss size resource\n");
		return -ENOMEM;
	}

	writeq_relaxed(addr, spss_addr);
	writel_relaxed(size, spss_size);
	iounmap(spss_addr);
	iounmap(spss_size);

	return 0;
}

struct qcom_glink *qcom_glink_spss_register(struct device *parent,
					    struct device_node *node)
{
	struct glink_spss_pipe *rx_pipe;
	struct glink_spss_pipe *tx_pipe;
	struct glink_spss_cfg *cfg;
	struct qcom_glink *glink;
	struct device *dev;
	u32 remote_pid;
	size_t tx_size;
	size_t rx_size;
	size_t size;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->parent = parent;
	dev->of_node = node;
	dev->release = qcom_glink_spss_release;
	dev_set_name(dev, "%s:%s", node->parent->name, node->name);
	ret = device_register(dev);
	if (ret) {
		pr_err("failed to register glink edge %s\n", node->name);
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

	tx_size = SPSS_TX_FIFO_SIZE;
	rx_size = SPSS_RX_FIFO_SIZE;
	size = tx_size + rx_size + sizeof(*cfg);
	ret = qcom_smem_alloc(remote_pid,
			      SMEM_GLINK_NATIVE_XPRT_DESCRIPTOR, size);
	if (ret && ret != -EEXIST) {
		dev_err(dev, "failed to allocate glink descriptors\n");
		goto err_put_dev;
	}

	cfg = qcom_smem_get(remote_pid,
			    SMEM_GLINK_NATIVE_XPRT_DESCRIPTOR, &size);
	if (IS_ERR(cfg)) {
		dev_err(dev, "failed to acquire xprt descriptor\n");
		ret = PTR_ERR(cfg);
		goto err_put_dev;
	}
	if (size != tx_size + rx_size + sizeof(*cfg)) {
		dev_err(dev, "glink descriptor of invalid size\n");
		ret = -EINVAL;
		goto err_put_dev;
	}
	cfg->tx_fifo_size = cpu_to_le32(tx_size);
	cfg->rx_fifo_size = cpu_to_le32(rx_size);

	tx_pipe->tail = &cfg->tx_tail;
	tx_pipe->head = &cfg->tx_head;
	tx_pipe->native.length = tx_size;
	tx_pipe->fifo = (u8 *)cfg + sizeof(*cfg);

	rx_pipe->tail = &cfg->rx_tail;
	rx_pipe->head = &cfg->rx_head;
	rx_pipe->native.length = rx_size;
	rx_pipe->fifo = (u8 *)cfg + sizeof(*cfg) + tx_size;

	rx_pipe->native.avail = glink_spss_rx_avail;
	rx_pipe->native.peak = glink_spss_rx_peak;
	rx_pipe->native.advance = glink_spss_rx_advance;
	rx_pipe->native.reset = glink_spss_reset;
	rx_pipe->remote_pid = remote_pid;

	tx_pipe->native.avail = glink_spss_tx_avail;
	tx_pipe->native.write = glink_spss_tx_write;
	tx_pipe->native.reset = glink_spss_reset;
	tx_pipe->remote_pid = remote_pid;

	*rx_pipe->tail = 0;
	*tx_pipe->head = 0;

	ret = glink_spss_advertise_cfg(dev, size, qcom_smem_virt_to_phys(cfg));
	if (ret)
		goto err_put_dev;

	glink = qcom_glink_native_probe(dev,
					GLINK_FEATURE_INTENT_REUSE,
					&rx_pipe->native, &tx_pipe->native,
					false);
	if (IS_ERR(glink)) {
		ret = PTR_ERR(glink);
		goto err_put_dev;
	}

	ret = qcom_glink_native_start(glink);
	if (ret)
		goto err_put_dev;

	return glink;

err_put_dev:
	put_device(dev);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL(qcom_glink_spss_register);

void qcom_glink_spss_unregister(struct qcom_glink *glink)
{
	if (!glink)
		return;

	qcom_glink_native_remove(glink);
	qcom_glink_native_unregister(glink);
}
EXPORT_SYMBOL(qcom_glink_spss_unregister);

MODULE_DESCRIPTION("QTI GLINK SPSS driver");
MODULE_LICENSE("GPL");
