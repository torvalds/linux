// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/ipc_logging.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define MAX_PRINT_SIZE		1024
#define MAX_BUF_NUM		4
#define MAX_RESIDUAL_SIZE	MAX_PRINT_SIZE
#define SIZE_ADJUST		4
#define SRC_OFFSET		4

#define CREATE_TRACE_POINTS
#include "trace_cpucp.h"

struct remote_mem {
	void __iomem *start;
	unsigned long long size;
};

struct cpucp_buf {
	struct list_head node;
	char *buf;
	u32 size;
	u32 cpy_idx;
};

struct cpucp_log_info {
	struct remote_mem *rmem;
	struct mbox_client cl;
	struct mbox_chan *ch;
	struct delayed_work work;
	struct device *dev;
	void __iomem *base;
	unsigned int rmem_idx;
	unsigned int num_bufs;
	unsigned int total_buf_size;
	char *rem_buf;
	char *glb_buf;
	int  rem_len;
	spinlock_t free_list_lock;
	spinlock_t full_list_lock;
};

static LIST_HEAD(full_buffers_list);
static LIST_HEAD(free_buffers_list);
static struct workqueue_struct *cpucp_wq;

static inline bool get_last_newline(char *buf, int size, int *cnt)
{
	int i;

	for (i = (size - 1); i >= 0 ; i--) {
		if (buf[i] == '\n') {
			buf[i] = '\0';
			*cnt = i + 1;
			return true;
		}
	}

	*cnt = size;
	return false;
}

static void cpucp_log_work(struct work_struct *work)
{
	struct cpucp_log_info *info = container_of(work,
						struct cpucp_log_info,
						work.work);
	char *src;
	int buf_start = 0;
	int cnt = 0, print_size = 0, buf_size = 0;
	bool ret;
	char tmp_buf[MAX_PRINT_SIZE + 1];
	struct cpucp_buf *buf_node;
	unsigned long flags;

	while (1) {
		spin_lock_irqsave(&info->full_list_lock, flags);
		if (list_empty(&full_buffers_list)) {
			spin_unlock_irqrestore(&info->full_list_lock, flags);
			return;
		}
		buf_node = list_first_entry(&full_buffers_list,
					struct cpucp_buf, node);
		list_del(&buf_node->node);
		spin_unlock_irqrestore(&info->full_list_lock, flags);
		buf_start = buf_node->cpy_idx - info->rem_len;
		src = &buf_node->buf[buf_start];
		buf_size = buf_node->size + info->rem_len;
		if (info->rem_len) {
			memcpy(&buf_node->buf[buf_start],
					info->rem_buf, info->rem_len);
			info->rem_len = 0;
		}
		do {
			print_size = (buf_size >= MAX_PRINT_SIZE) ?
						MAX_PRINT_SIZE : buf_size;
			ret = get_last_newline(src, print_size, &cnt);
			if (cnt == print_size) {
				if (!ret && buf_size < MAX_PRINT_SIZE) {
					info->rem_len = buf_size;
					memcpy(info->rem_buf, src, buf_size);
					goto out;
				} else {
					snprintf(tmp_buf, print_size + 1, "%s", src);
					trace_cpucp_log(tmp_buf);
				}
			} else
				trace_cpucp_log(src);

			buf_start += cnt;
			buf_size -= cnt;
			src = &buf_node->buf[buf_start];
		} while (buf_size > 0);

out:
		spin_lock_irqsave(&info->free_list_lock, flags);
		list_add_tail(&buf_node->node, &free_buffers_list);
		spin_unlock_irqrestore(&info->free_list_lock, flags);
	}
}

static struct cpucp_buf *get_free_buffer(struct cpucp_log_info *info)
{
	struct cpucp_buf *buf_node;
	unsigned long flags;

	spin_lock_irqsave(&info->free_list_lock, flags);
	if (list_empty(&free_buffers_list)) {
		spin_unlock_irqrestore(&info->free_list_lock, flags);
		return NULL;
	}

	buf_node = list_first_entry(&free_buffers_list,
					struct cpucp_buf, node);
	list_del(&buf_node->node);
	spin_unlock_irqrestore(&info->free_list_lock, flags);
	return buf_node;
}

static void cpucp_log_rx(struct mbox_client *client, void *msg)
{
	struct cpucp_log_info *info = dev_get_drvdata(client->dev);
	struct device *dev = info->dev;
	struct cpucp_buf *buf_node;
	struct remote_mem *rmem;
	void __iomem *src;
	u32 marker;
	unsigned long long rmem_size;
	unsigned long flags;
	int src_offset = 0;
	int size_adj = 0;

	buf_node = get_free_buffer(info);
	if (!buf_node) {
		dev_err(dev, "global buffer full dropping buffers\n");
		return;
	}

	marker = *(u32 *)(info->rmem)->start;
	if (marker <= info->rmem->size) {
		info->rmem_idx = 0;
		rmem_size = marker;
	} else if (marker <= info->total_buf_size) {
		info->rmem_idx = 1;
		rmem_size = marker - info->rmem->size;
	} else {
		pr_err("%s: Log marker incorrect: %u\n", __func__, marker);
		return;
	}

	if (info->rmem_idx == 0) {
		size_adj = SIZE_ADJUST;
		src_offset = SRC_OFFSET;
	}

	rmem = info->rmem + info->rmem_idx;
	rmem_size -= size_adj;
	src = rmem->start + src_offset;
	memcpy_fromio(&buf_node->buf[buf_node->cpy_idx], src, rmem_size);
	buf_node->size = rmem_size;
	spin_lock_irqsave(&info->full_list_lock, flags);
	list_add_tail(&buf_node->node, &full_buffers_list);
	spin_unlock_irqrestore(&info->full_list_lock, flags);

	if (!delayed_work_pending(&info->work))
		queue_delayed_work(cpucp_wq, &info->work, 0);
}

static int populate_free_buffers(struct cpucp_log_info *info,
							int rmem_size)
{
	int i = 0;
	struct cpucp_buf *buf_nodes;

	buf_nodes = devm_kzalloc(info->dev,
				MAX_BUF_NUM * sizeof(struct cpucp_buf),
				GFP_KERNEL);
	if (!buf_nodes)
		return -ENOMEM;

	for (i = 0; i < MAX_BUF_NUM; i++) {
		buf_nodes[i].buf = &info->glb_buf[i * (rmem_size + MAX_PRINT_SIZE)];
		buf_nodes[i].size = rmem_size;
		buf_nodes[i].cpy_idx = MAX_PRINT_SIZE;
		list_add_tail(&buf_nodes[i].node, &free_buffers_list);
	}
	return 0;
}

static int cpucp_log_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cpucp_log_info *info;
	struct mbox_client *cl;
	int ret, i = 0;
	struct resource *res;
	void __iomem *mem_base;
	struct remote_mem *rmem;
	int prev_size = 0;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;

	rmem = kcalloc(pdev->num_resources, sizeof(struct remote_mem),
			GFP_KERNEL);
	if (!rmem)
		return -ENOMEM;

	info->rmem = rmem;

	for (i = 0; i < pdev->num_resources; i++) {
		struct remote_mem *rmem = &info->rmem[i];

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			dev_err(dev,
				"Failed to get the device base address\n");
			ret = -ENODEV;
			goto exit;
		}

		mem_base = devm_ioremap(&pdev->dev, res->start,
					resource_size(res));
		if (IS_ERR(mem_base)) {
			ret =  PTR_ERR(mem_base);
			dev_err(dev, "Failed to io remap the region err: %d\n", ret);
			goto exit;
		}
		rmem->start = mem_base;
		rmem->size = resource_size(res);
		if (prev_size && (rmem->size != prev_size)) {
			ret = -EINVAL;
			goto exit;
		} else if (!prev_size) {
			prev_size = rmem->size;
		}

		info->total_buf_size += rmem->size;
		info->num_bufs++;
	}
	info->glb_buf = devm_kzalloc(dev, MAX_BUF_NUM *
					(rmem->size + MAX_PRINT_SIZE),
					GFP_KERNEL);
	if (!info->glb_buf) {
		ret = -ENOMEM;
		goto exit;
	}

	info->rem_buf = devm_kzalloc(dev, MAX_RESIDUAL_SIZE, GFP_KERNEL);
	if (!info->rem_buf) {
		ret = -ENOMEM;
		goto exit;
	}

	ret = populate_free_buffers(info, rmem->size);
	if (ret < 0)
		goto exit;

	cl = &info->cl;
	cl->dev = dev;
	cl->tx_block = false;
	cl->knows_txdone = true;
	cl->rx_callback = cpucp_log_rx;

	dev_set_drvdata(dev, info);
	INIT_DEFERRABLE_WORK(&info->work, &cpucp_log_work);
	spin_lock_init(&info->free_list_lock);
	spin_lock_init(&info->full_list_lock);
	cpucp_wq = create_freezable_workqueue("cpucp_wq");

	info->ch = mbox_request_channel(cl, 0);
	if (IS_ERR(info->ch)) {
		ret = PTR_ERR(info->ch);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to request mbox info: %d\n", ret);
		goto exit;
	}

	dev_dbg(dev, "CPUCP logging initialized\n");

	return 0;

exit:
	kfree(info->rmem);
	/* devm will free up buffers in lists so just re-initialize lists */
	INIT_LIST_HEAD(&full_buffers_list);
	INIT_LIST_HEAD(&free_buffers_list);
	return ret;
}

static int cpucp_log_remove(struct platform_device *pdev)
{
	struct cpucp_log_info *info;

	info = dev_get_drvdata(&pdev->dev);

	mbox_free_channel(info->ch);

	return 0;
}

static const struct of_device_id cpucp_log[] = {
	{.compatible = "qcom,cpucp-log"},
	{},
};

static struct platform_driver cpucp_log_driver = {
	.driver = {
		.name = "cpucp-log",
		.of_match_table = cpucp_log,
	},
	.probe = cpucp_log_probe,
	.remove = cpucp_log_remove,
};
builtin_platform_driver(cpucp_log_driver);

MODULE_LICENSE("GPL");
