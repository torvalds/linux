// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2017, Linaro Ltd
 */

#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/mailbox_client.h>

#include "rpmsg_internal.h"
#include "qcom_glink_native.h"

#define RPM_TOC_SIZE		256
#define RPM_TOC_MAGIC		0x67727430 /* grt0 */
#define RPM_TOC_MAX_ENTRIES	((RPM_TOC_SIZE - sizeof(struct rpm_toc)) / \
				 sizeof(struct rpm_toc_entry))

#define RPM_TX_FIFO_ID		0x61703272 /* ap2r */
#define RPM_RX_FIFO_ID		0x72326170 /* r2ap */

#define to_rpm_pipe(p) container_of(p, struct glink_rpm_pipe, native)

struct rpm_toc_entry {
	__le32 id;
	__le32 offset;
	__le32 size;
} __packed;

struct rpm_toc {
	__le32 magic;
	__le32 count;

	struct rpm_toc_entry entries[];
} __packed;

struct glink_rpm_pipe {
	struct qcom_glink_pipe native;

	void __iomem *tail;
	void __iomem *head;

	void __iomem *fifo;
};

struct glink_rpm {
	struct qcom_glink *glink;

	int irq;

	struct mbox_client mbox_client;
	struct mbox_chan *mbox_chan;

	struct glink_rpm_pipe rx_pipe;
	struct glink_rpm_pipe tx_pipe;
};

static size_t glink_rpm_rx_avail(struct qcom_glink_pipe *glink_pipe)
{
	struct glink_rpm_pipe *pipe = to_rpm_pipe(glink_pipe);
	unsigned int head;
	unsigned int tail;

	head = readl(pipe->head);
	tail = readl(pipe->tail);

	if (head < tail)
		return pipe->native.length - tail + head;
	else
		return head - tail;
}

static void glink_rpm_rx_peek(struct qcom_glink_pipe *glink_pipe,
			      void *data, unsigned int offset, size_t count)
{
	struct glink_rpm_pipe *pipe = to_rpm_pipe(glink_pipe);
	unsigned int tail;
	size_t len;

	tail = readl(pipe->tail);
	tail += offset;
	if (tail >= pipe->native.length)
		tail -= pipe->native.length;

	len = min_t(size_t, count, pipe->native.length - tail);
	if (len) {
		__ioread32_copy(data, pipe->fifo + tail,
				len / sizeof(u32));
	}

	if (len != count) {
		__ioread32_copy(data + len, pipe->fifo,
				(count - len) / sizeof(u32));
	}
}

static void glink_rpm_rx_advance(struct qcom_glink_pipe *glink_pipe,
				 size_t count)
{
	struct glink_rpm_pipe *pipe = to_rpm_pipe(glink_pipe);
	unsigned int tail;

	tail = readl(pipe->tail);

	tail += count;
	if (tail >= pipe->native.length)
		tail -= pipe->native.length;

	writel(tail, pipe->tail);
}

static size_t glink_rpm_tx_avail(struct qcom_glink_pipe *glink_pipe)
{
	struct glink_rpm_pipe *pipe = to_rpm_pipe(glink_pipe);
	unsigned int head;
	unsigned int tail;

	head = readl(pipe->head);
	tail = readl(pipe->tail);

	if (tail <= head)
		return pipe->native.length - head + tail;
	else
		return tail - head;
}

static unsigned int glink_rpm_tx_write_one(struct glink_rpm_pipe *pipe,
					   unsigned int head,
					   const void *data, size_t count)
{
	size_t len;

	len = min_t(size_t, count, pipe->native.length - head);
	if (len) {
		__iowrite32_copy(pipe->fifo + head, data,
				 len / sizeof(u32));
	}

	if (len != count) {
		__iowrite32_copy(pipe->fifo, data + len,
				 (count - len) / sizeof(u32));
	}

	head += count;
	if (head >= pipe->native.length)
		head -= pipe->native.length;

	return head;
}

static void glink_rpm_tx_write(struct qcom_glink_pipe *glink_pipe,
			       const void *hdr, size_t hlen,
			       const void *data, size_t dlen)
{
	struct glink_rpm_pipe *pipe = to_rpm_pipe(glink_pipe);
	size_t tlen = hlen + dlen;
	size_t aligned_dlen;
	unsigned int head;
	char padding[8] = {0};
	size_t pad;

	/* Header length comes from glink native and is always 4 byte aligned */
	if (WARN(hlen % 4, "Glink Header length must be 4 bytes aligned\n"))
		return;

	/*
	 * Move the unaligned tail of the message to the padding chunk, to
	 * ensure word aligned accesses
	 */
	aligned_dlen = ALIGN_DOWN(dlen, 4);
	if (aligned_dlen != dlen)
		memcpy(padding, data + aligned_dlen, dlen - aligned_dlen);

	head = readl(pipe->head);
	head = glink_rpm_tx_write_one(pipe, head, hdr, hlen);
	head = glink_rpm_tx_write_one(pipe, head, data, aligned_dlen);

	pad = ALIGN(tlen, 8) - ALIGN_DOWN(tlen, 4);
	if (pad)
		head = glink_rpm_tx_write_one(pipe, head, padding, pad);
	writel(head, pipe->head);
}

static void glink_rpm_tx_kick(struct qcom_glink_pipe *glink_pipe)
{
	struct glink_rpm_pipe *pipe = to_rpm_pipe(glink_pipe);
	struct glink_rpm *rpm = container_of(pipe, struct glink_rpm, tx_pipe);

	mbox_send_message(rpm->mbox_chan, NULL);
	mbox_client_txdone(rpm->mbox_chan, 0);
}

static irqreturn_t qcom_glink_rpm_intr(int irq, void *data)
{
	struct glink_rpm *rpm = data;

	qcom_glink_native_rx(rpm->glink);

	return IRQ_HANDLED;
}

static int glink_rpm_parse_toc(struct device *dev,
			       void __iomem *msg_ram,
			       size_t msg_ram_size,
			       struct glink_rpm_pipe *rx,
			       struct glink_rpm_pipe *tx)
{
	struct rpm_toc *toc;
	int num_entries;
	unsigned int id;
	size_t offset;
	size_t size;
	void *buf;
	int i;

	buf = kzalloc(RPM_TOC_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	__ioread32_copy(buf, msg_ram + msg_ram_size - RPM_TOC_SIZE,
			RPM_TOC_SIZE / sizeof(u32));

	toc = buf;

	if (le32_to_cpu(toc->magic) != RPM_TOC_MAGIC) {
		dev_err(dev, "RPM TOC has invalid magic\n");
		goto err_inval;
	}

	num_entries = le32_to_cpu(toc->count);
	if (num_entries > RPM_TOC_MAX_ENTRIES) {
		dev_err(dev, "Invalid number of toc entries\n");
		goto err_inval;
	}

	for (i = 0; i < num_entries; i++) {
		id = le32_to_cpu(toc->entries[i].id);
		offset = le32_to_cpu(toc->entries[i].offset);
		size = le32_to_cpu(toc->entries[i].size);

		if (offset > msg_ram_size || offset + size > msg_ram_size) {
			dev_err(dev, "TOC entry with invalid size\n");
			continue;
		}

		switch (id) {
		case RPM_RX_FIFO_ID:
			rx->native.length = size;

			rx->tail = msg_ram + offset;
			rx->head = msg_ram + offset + sizeof(u32);
			rx->fifo = msg_ram + offset + 2 * sizeof(u32);
			break;
		case RPM_TX_FIFO_ID:
			tx->native.length = size;

			tx->tail = msg_ram + offset;
			tx->head = msg_ram + offset + sizeof(u32);
			tx->fifo = msg_ram + offset + 2 * sizeof(u32);
			break;
		}
	}

	if (!rx->fifo || !tx->fifo) {
		dev_err(dev, "Unable to find rx and tx descriptors\n");
		goto err_inval;
	}

	kfree(buf);
	return 0;

err_inval:
	kfree(buf);
	return -EINVAL;
}

static int glink_rpm_probe(struct platform_device *pdev)
{
	struct qcom_glink *glink;
	struct glink_rpm *rpm;
	struct device_node *np;
	void __iomem *msg_ram;
	size_t msg_ram_size;
	struct device *dev = &pdev->dev;
	struct resource r;
	int ret;

	rpm = devm_kzalloc(&pdev->dev, sizeof(*rpm), GFP_KERNEL);
	if (!rpm)
		return -ENOMEM;

	np = of_parse_phandle(dev->of_node, "qcom,rpm-msg-ram", 0);
	ret = of_address_to_resource(np, 0, &r);
	of_node_put(np);
	if (ret)
		return ret;

	msg_ram = devm_ioremap(dev, r.start, resource_size(&r));
	msg_ram_size = resource_size(&r);
	if (!msg_ram)
		return -ENOMEM;

	ret = glink_rpm_parse_toc(dev, msg_ram, msg_ram_size,
				  &rpm->rx_pipe, &rpm->tx_pipe);
	if (ret)
		return ret;

	rpm->irq = of_irq_get(dev->of_node, 0);
	ret = devm_request_irq(dev, rpm->irq, qcom_glink_rpm_intr,
			       IRQF_NO_SUSPEND | IRQF_NO_AUTOEN,
			       "glink-rpm", rpm);
	if (ret) {
		dev_err(dev, "failed to request IRQ\n");
		return ret;
	}

	rpm->mbox_client.dev = dev;
	rpm->mbox_client.knows_txdone = true;
	rpm->mbox_chan = mbox_request_channel(&rpm->mbox_client, 0);
	if (IS_ERR(rpm->mbox_chan))
		return dev_err_probe(dev, PTR_ERR(rpm->mbox_chan), "failed to acquire IPC channel\n");

	/* Pipe specific accessors */
	rpm->rx_pipe.native.avail = glink_rpm_rx_avail;
	rpm->rx_pipe.native.peek = glink_rpm_rx_peek;
	rpm->rx_pipe.native.advance = glink_rpm_rx_advance;
	rpm->tx_pipe.native.avail = glink_rpm_tx_avail;
	rpm->tx_pipe.native.write = glink_rpm_tx_write;
	rpm->tx_pipe.native.kick = glink_rpm_tx_kick;

	writel(0, rpm->tx_pipe.head);
	writel(0, rpm->rx_pipe.tail);

	glink = qcom_glink_native_probe(dev,
					0,
					&rpm->rx_pipe.native,
					&rpm->tx_pipe.native,
					true);
	if (IS_ERR(glink)) {
		mbox_free_channel(rpm->mbox_chan);
		return PTR_ERR(glink);
	}

	rpm->glink = glink;

	platform_set_drvdata(pdev, rpm);

	enable_irq(rpm->irq);

	return 0;
}

static void glink_rpm_remove(struct platform_device *pdev)
{
	struct glink_rpm *rpm = platform_get_drvdata(pdev);
	struct qcom_glink *glink = rpm->glink;

	disable_irq(rpm->irq);

	qcom_glink_native_remove(glink);

	mbox_free_channel(rpm->mbox_chan);
}

static const struct of_device_id glink_rpm_of_match[] = {
	{ .compatible = "qcom,glink-rpm" },
	{}
};
MODULE_DEVICE_TABLE(of, glink_rpm_of_match);

static struct platform_driver glink_rpm_driver = {
	.probe = glink_rpm_probe,
	.remove = glink_rpm_remove,
	.driver = {
		.name = "qcom_glink_rpm",
		.of_match_table = glink_rpm_of_match,
	},
};

static int __init glink_rpm_init(void)
{
	return platform_driver_register(&glink_rpm_driver);
}
subsys_initcall(glink_rpm_init);

static void __exit glink_rpm_exit(void)
{
	platform_driver_unregister(&glink_rpm_driver);
}
module_exit(glink_rpm_exit);

MODULE_AUTHOR("Bjorn Andersson <bjorn.andersson@linaro.org>");
MODULE_DESCRIPTION("Qualcomm GLINK RPM driver");
MODULE_LICENSE("GPL v2");
