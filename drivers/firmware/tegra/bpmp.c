// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk/tegra.h>
#include <linux/genalloc.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/semaphore.h>
#include <linux/sched/clock.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>
#include <soc/tegra/ivc.h>

#include "bpmp-private.h"

#define MSG_ACK		BIT(0)
#define MSG_RING	BIT(1)
#define TAG_SZ		32

static inline struct tegra_bpmp *
mbox_client_to_bpmp(struct mbox_client *client)
{
	return container_of(client, struct tegra_bpmp, mbox.client);
}

static inline const struct tegra_bpmp_ops *
channel_to_ops(struct tegra_bpmp_channel *channel)
{
	struct tegra_bpmp *bpmp = channel->bpmp;

	return bpmp->soc->ops;
}

struct tegra_bpmp *tegra_bpmp_get(struct device *dev)
{
	struct platform_device *pdev;
	struct tegra_bpmp *bpmp;
	struct device_node *np;

	np = of_parse_phandle(dev->of_node, "nvidia,bpmp", 0);
	if (!np)
		return ERR_PTR(-ENOENT);

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		bpmp = ERR_PTR(-ENODEV);
		goto put;
	}

	bpmp = platform_get_drvdata(pdev);
	if (!bpmp) {
		bpmp = ERR_PTR(-EPROBE_DEFER);
		put_device(&pdev->dev);
		goto put;
	}

put:
	of_node_put(np);
	return bpmp;
}
EXPORT_SYMBOL_GPL(tegra_bpmp_get);

void tegra_bpmp_put(struct tegra_bpmp *bpmp)
{
	if (bpmp)
		put_device(bpmp->dev);
}
EXPORT_SYMBOL_GPL(tegra_bpmp_put);

static int
tegra_bpmp_channel_get_thread_index(struct tegra_bpmp_channel *channel)
{
	struct tegra_bpmp *bpmp = channel->bpmp;
	unsigned int count;
	int index;

	count = bpmp->soc->channels.thread.count;

	index = channel - channel->bpmp->threaded_channels;
	if (index < 0 || index >= count)
		return -EINVAL;

	return index;
}

static bool tegra_bpmp_message_valid(const struct tegra_bpmp_message *msg)
{
	return (msg->tx.size <= MSG_DATA_MIN_SZ) &&
	       (msg->rx.size <= MSG_DATA_MIN_SZ) &&
	       (msg->tx.size == 0 || msg->tx.data) &&
	       (msg->rx.size == 0 || msg->rx.data);
}

static bool tegra_bpmp_is_response_ready(struct tegra_bpmp_channel *channel)
{
	const struct tegra_bpmp_ops *ops = channel_to_ops(channel);

	return ops->is_response_ready(channel);
}

static bool tegra_bpmp_is_request_ready(struct tegra_bpmp_channel *channel)
{
	const struct tegra_bpmp_ops *ops = channel_to_ops(channel);

	return ops->is_request_ready(channel);
}

static int tegra_bpmp_wait_response(struct tegra_bpmp_channel *channel)
{
	unsigned long timeout = channel->bpmp->soc->channels.cpu_tx.timeout;
	ktime_t end;

	end = ktime_add_us(ktime_get(), timeout);

	do {
		if (tegra_bpmp_is_response_ready(channel))
			return 0;
	} while (ktime_before(ktime_get(), end));

	return -ETIMEDOUT;
}

static int tegra_bpmp_ack_response(struct tegra_bpmp_channel *channel)
{
	const struct tegra_bpmp_ops *ops = channel_to_ops(channel);

	return ops->ack_response(channel);
}

static int tegra_bpmp_ack_request(struct tegra_bpmp_channel *channel)
{
	const struct tegra_bpmp_ops *ops = channel_to_ops(channel);

	return ops->ack_request(channel);
}

static bool
tegra_bpmp_is_request_channel_free(struct tegra_bpmp_channel *channel)
{
	const struct tegra_bpmp_ops *ops = channel_to_ops(channel);

	return ops->is_request_channel_free(channel);
}

static bool
tegra_bpmp_is_response_channel_free(struct tegra_bpmp_channel *channel)
{
	const struct tegra_bpmp_ops *ops = channel_to_ops(channel);

	return ops->is_response_channel_free(channel);
}

static int
tegra_bpmp_wait_request_channel_free(struct tegra_bpmp_channel *channel)
{
	unsigned long timeout = channel->bpmp->soc->channels.cpu_tx.timeout;
	ktime_t start, now;

	start = ns_to_ktime(local_clock());

	do {
		if (tegra_bpmp_is_request_channel_free(channel))
			return 0;

		now = ns_to_ktime(local_clock());
	} while (ktime_us_delta(now, start) < timeout);

	return -ETIMEDOUT;
}

static int tegra_bpmp_post_request(struct tegra_bpmp_channel *channel)
{
	const struct tegra_bpmp_ops *ops = channel_to_ops(channel);

	return ops->post_request(channel);
}

static int tegra_bpmp_post_response(struct tegra_bpmp_channel *channel)
{
	const struct tegra_bpmp_ops *ops = channel_to_ops(channel);

	return ops->post_response(channel);
}

static int tegra_bpmp_ring_doorbell(struct tegra_bpmp *bpmp)
{
	return bpmp->soc->ops->ring_doorbell(bpmp);
}

static ssize_t __tegra_bpmp_channel_read(struct tegra_bpmp_channel *channel,
					 void *data, size_t size, int *ret)
{
	int err;

	if (data && size > 0)
		tegra_bpmp_mb_read(data, &channel->ib, size);

	err = tegra_bpmp_ack_response(channel);
	if (err < 0)
		return err;

	*ret = tegra_bpmp_mb_read_field(&channel->ib, code);

	return 0;
}

static ssize_t tegra_bpmp_channel_read(struct tegra_bpmp_channel *channel,
				       void *data, size_t size, int *ret)
{
	struct tegra_bpmp *bpmp = channel->bpmp;
	unsigned long flags;
	ssize_t err;
	int index;

	index = tegra_bpmp_channel_get_thread_index(channel);
	if (index < 0) {
		err = index;
		goto unlock;
	}

	spin_lock_irqsave(&bpmp->lock, flags);
	err = __tegra_bpmp_channel_read(channel, data, size, ret);
	clear_bit(index, bpmp->threaded.allocated);
	spin_unlock_irqrestore(&bpmp->lock, flags);

unlock:
	up(&bpmp->threaded.lock);

	return err;
}

static ssize_t __tegra_bpmp_channel_write(struct tegra_bpmp_channel *channel,
					  unsigned int mrq, unsigned long flags,
					  const void *data, size_t size)
{
	tegra_bpmp_mb_write_field(&channel->ob, code, mrq);
	tegra_bpmp_mb_write_field(&channel->ob, flags, flags);

	if (data && size > 0)
		tegra_bpmp_mb_write(&channel->ob, data, size);

	return tegra_bpmp_post_request(channel);
}

static struct tegra_bpmp_channel *
tegra_bpmp_write_threaded(struct tegra_bpmp *bpmp, unsigned int mrq,
			  const void *data, size_t size)
{
	unsigned long timeout = bpmp->soc->channels.thread.timeout;
	unsigned int count = bpmp->soc->channels.thread.count;
	struct tegra_bpmp_channel *channel;
	unsigned long flags;
	unsigned int index;
	int err;

	err = down_timeout(&bpmp->threaded.lock, usecs_to_jiffies(timeout));
	if (err < 0)
		return ERR_PTR(err);

	spin_lock_irqsave(&bpmp->lock, flags);

	index = find_first_zero_bit(bpmp->threaded.allocated, count);
	if (index == count) {
		err = -EBUSY;
		goto unlock;
	}

	channel = &bpmp->threaded_channels[index];

	if (!tegra_bpmp_is_request_channel_free(channel)) {
		err = -EBUSY;
		goto unlock;
	}

	set_bit(index, bpmp->threaded.allocated);

	err = __tegra_bpmp_channel_write(channel, mrq, MSG_ACK | MSG_RING,
					 data, size);
	if (err < 0)
		goto clear_allocated;

	set_bit(index, bpmp->threaded.busy);

	spin_unlock_irqrestore(&bpmp->lock, flags);
	return channel;

clear_allocated:
	clear_bit(index, bpmp->threaded.allocated);
unlock:
	spin_unlock_irqrestore(&bpmp->lock, flags);
	up(&bpmp->threaded.lock);

	return ERR_PTR(err);
}

static ssize_t tegra_bpmp_channel_write(struct tegra_bpmp_channel *channel,
					unsigned int mrq, unsigned long flags,
					const void *data, size_t size)
{
	int err;

	err = tegra_bpmp_wait_request_channel_free(channel);
	if (err < 0)
		return err;

	return __tegra_bpmp_channel_write(channel, mrq, flags, data, size);
}

int tegra_bpmp_transfer_atomic(struct tegra_bpmp *bpmp,
			       struct tegra_bpmp_message *msg)
{
	struct tegra_bpmp_channel *channel;
	int err;

	if (WARN_ON(!irqs_disabled()))
		return -EPERM;

	if (!tegra_bpmp_message_valid(msg))
		return -EINVAL;

	channel = bpmp->tx_channel;

	spin_lock(&bpmp->atomic_tx_lock);

	err = tegra_bpmp_channel_write(channel, msg->mrq, MSG_ACK,
				       msg->tx.data, msg->tx.size);
	if (err < 0) {
		spin_unlock(&bpmp->atomic_tx_lock);
		return err;
	}

	spin_unlock(&bpmp->atomic_tx_lock);

	err = tegra_bpmp_ring_doorbell(bpmp);
	if (err < 0)
		return err;

	err = tegra_bpmp_wait_response(channel);
	if (err < 0)
		return err;

	return __tegra_bpmp_channel_read(channel, msg->rx.data, msg->rx.size,
					 &msg->rx.ret);
}
EXPORT_SYMBOL_GPL(tegra_bpmp_transfer_atomic);

int tegra_bpmp_transfer(struct tegra_bpmp *bpmp,
			struct tegra_bpmp_message *msg)
{
	struct tegra_bpmp_channel *channel;
	unsigned long timeout;
	int err;

	if (WARN_ON(irqs_disabled()))
		return -EPERM;

	if (!tegra_bpmp_message_valid(msg))
		return -EINVAL;

	channel = tegra_bpmp_write_threaded(bpmp, msg->mrq, msg->tx.data,
					    msg->tx.size);
	if (IS_ERR(channel))
		return PTR_ERR(channel);

	err = tegra_bpmp_ring_doorbell(bpmp);
	if (err < 0)
		return err;

	timeout = usecs_to_jiffies(bpmp->soc->channels.thread.timeout);

	err = wait_for_completion_timeout(&channel->completion, timeout);
	if (err == 0)
		return -ETIMEDOUT;

	return tegra_bpmp_channel_read(channel, msg->rx.data, msg->rx.size,
				       &msg->rx.ret);
}
EXPORT_SYMBOL_GPL(tegra_bpmp_transfer);

static struct tegra_bpmp_mrq *tegra_bpmp_find_mrq(struct tegra_bpmp *bpmp,
						  unsigned int mrq)
{
	struct tegra_bpmp_mrq *entry;

	list_for_each_entry(entry, &bpmp->mrqs, list)
		if (entry->mrq == mrq)
			return entry;

	return NULL;
}

void tegra_bpmp_mrq_return(struct tegra_bpmp_channel *channel, int code,
			   const void *data, size_t size)
{
	unsigned long flags = tegra_bpmp_mb_read_field(&channel->ib, flags);
	struct tegra_bpmp *bpmp = channel->bpmp;
	int err;

	if (WARN_ON(size > MSG_DATA_MIN_SZ))
		return;

	err = tegra_bpmp_ack_request(channel);
	if (WARN_ON(err < 0))
		return;

	if ((flags & MSG_ACK) == 0)
		return;

	if (WARN_ON(!tegra_bpmp_is_response_channel_free(channel)))
		return;

	tegra_bpmp_mb_write_field(&channel->ob, code, code);

	if (data && size > 0)
		tegra_bpmp_mb_write(&channel->ob, data, size);

	err = tegra_bpmp_post_response(channel);
	if (WARN_ON(err < 0))
		return;

	if (flags & MSG_RING) {
		err = tegra_bpmp_ring_doorbell(bpmp);
		if (WARN_ON(err < 0))
			return;
	}
}
EXPORT_SYMBOL_GPL(tegra_bpmp_mrq_return);

static void tegra_bpmp_handle_mrq(struct tegra_bpmp *bpmp,
				  unsigned int mrq,
				  struct tegra_bpmp_channel *channel)
{
	struct tegra_bpmp_mrq *entry;
	u32 zero = 0;

	spin_lock(&bpmp->lock);

	entry = tegra_bpmp_find_mrq(bpmp, mrq);
	if (!entry) {
		spin_unlock(&bpmp->lock);
		tegra_bpmp_mrq_return(channel, -EINVAL, &zero, sizeof(zero));
		return;
	}

	entry->handler(mrq, channel, entry->data);

	spin_unlock(&bpmp->lock);
}

int tegra_bpmp_request_mrq(struct tegra_bpmp *bpmp, unsigned int mrq,
			   tegra_bpmp_mrq_handler_t handler, void *data)
{
	struct tegra_bpmp_mrq *entry;
	unsigned long flags;

	if (!handler)
		return -EINVAL;

	entry = devm_kzalloc(bpmp->dev, sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	spin_lock_irqsave(&bpmp->lock, flags);

	entry->mrq = mrq;
	entry->handler = handler;
	entry->data = data;
	list_add(&entry->list, &bpmp->mrqs);

	spin_unlock_irqrestore(&bpmp->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra_bpmp_request_mrq);

void tegra_bpmp_free_mrq(struct tegra_bpmp *bpmp, unsigned int mrq, void *data)
{
	struct tegra_bpmp_mrq *entry;
	unsigned long flags;

	spin_lock_irqsave(&bpmp->lock, flags);

	entry = tegra_bpmp_find_mrq(bpmp, mrq);
	if (!entry)
		goto unlock;

	list_del(&entry->list);
	devm_kfree(bpmp->dev, entry);

unlock:
	spin_unlock_irqrestore(&bpmp->lock, flags);
}
EXPORT_SYMBOL_GPL(tegra_bpmp_free_mrq);

bool tegra_bpmp_mrq_is_supported(struct tegra_bpmp *bpmp, unsigned int mrq)
{
	struct mrq_query_abi_request req = { .mrq = mrq };
	struct mrq_query_abi_response resp;
	struct tegra_bpmp_message msg = {
		.mrq = MRQ_QUERY_ABI,
		.tx = {
			.data = &req,
			.size = sizeof(req),
		},
		.rx = {
			.data = &resp,
			.size = sizeof(resp),
		},
	};
	int err;

	err = tegra_bpmp_transfer(bpmp, &msg);
	if (err || msg.rx.ret)
		return false;

	return resp.status == 0;
}
EXPORT_SYMBOL_GPL(tegra_bpmp_mrq_is_supported);

static void tegra_bpmp_mrq_handle_ping(unsigned int mrq,
				       struct tegra_bpmp_channel *channel,
				       void *data)
{
	struct mrq_ping_request request;
	struct mrq_ping_response response;

	tegra_bpmp_mb_read(&request, &channel->ib, sizeof(request));

	memset(&response, 0, sizeof(response));
	response.reply = request.challenge << 1;

	tegra_bpmp_mrq_return(channel, 0, &response, sizeof(response));
}

static int tegra_bpmp_ping(struct tegra_bpmp *bpmp)
{
	struct mrq_ping_response response;
	struct mrq_ping_request request;
	struct tegra_bpmp_message msg;
	unsigned long flags;
	ktime_t start, end;
	int err;

	memset(&request, 0, sizeof(request));
	request.challenge = 1;

	memset(&response, 0, sizeof(response));

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_PING;
	msg.tx.data = &request;
	msg.tx.size = sizeof(request);
	msg.rx.data = &response;
	msg.rx.size = sizeof(response);

	local_irq_save(flags);
	start = ktime_get();
	err = tegra_bpmp_transfer_atomic(bpmp, &msg);
	end = ktime_get();
	local_irq_restore(flags);

	if (!err)
		dev_dbg(bpmp->dev,
			"ping ok: challenge: %u, response: %u, time: %lld\n",
			request.challenge, response.reply,
			ktime_to_us(ktime_sub(end, start)));

	return err;
}

/* deprecated version of tag query */
static int tegra_bpmp_get_firmware_tag_old(struct tegra_bpmp *bpmp, char *tag,
					   size_t size)
{
	struct mrq_query_tag_request request;
	struct tegra_bpmp_message msg;
	unsigned long flags;
	dma_addr_t phys;
	void *virt;
	int err;

	if (size != TAG_SZ)
		return -EINVAL;

	virt = dma_alloc_coherent(bpmp->dev, TAG_SZ, &phys,
				  GFP_KERNEL | GFP_DMA32);
	if (!virt)
		return -ENOMEM;

	memset(&request, 0, sizeof(request));
	request.addr = phys;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_QUERY_TAG;
	msg.tx.data = &request;
	msg.tx.size = sizeof(request);

	local_irq_save(flags);
	err = tegra_bpmp_transfer_atomic(bpmp, &msg);
	local_irq_restore(flags);

	if (err == 0)
		memcpy(tag, virt, TAG_SZ);

	dma_free_coherent(bpmp->dev, TAG_SZ, virt, phys);

	return err;
}

static int tegra_bpmp_get_firmware_tag(struct tegra_bpmp *bpmp, char *tag,
				       size_t size)
{
	if (tegra_bpmp_mrq_is_supported(bpmp, MRQ_QUERY_FW_TAG)) {
		struct mrq_query_fw_tag_response resp;
		struct tegra_bpmp_message msg = {
			.mrq = MRQ_QUERY_FW_TAG,
			.rx = {
				.data = &resp,
				.size = sizeof(resp),
			},
		};
		int err;

		if (size != sizeof(resp.tag))
			return -EINVAL;

		err = tegra_bpmp_transfer(bpmp, &msg);

		if (err)
			return err;
		if (msg.rx.ret < 0)
			return -EINVAL;

		memcpy(tag, resp.tag, sizeof(resp.tag));
		return 0;
	}

	return tegra_bpmp_get_firmware_tag_old(bpmp, tag, size);
}

static void tegra_bpmp_channel_signal(struct tegra_bpmp_channel *channel)
{
	unsigned long flags = tegra_bpmp_mb_read_field(&channel->ob, flags);

	if ((flags & MSG_RING) == 0)
		return;

	complete(&channel->completion);
}

void tegra_bpmp_handle_rx(struct tegra_bpmp *bpmp)
{
	struct tegra_bpmp_channel *channel;
	unsigned int i, count;
	unsigned long *busy;

	channel = bpmp->rx_channel;
	count = bpmp->soc->channels.thread.count;
	busy = bpmp->threaded.busy;

	if (tegra_bpmp_is_request_ready(channel)) {
		unsigned int mrq = tegra_bpmp_mb_read_field(&channel->ib, code);

		tegra_bpmp_handle_mrq(bpmp, mrq, channel);
	}

	spin_lock(&bpmp->lock);

	for_each_set_bit(i, busy, count) {
		struct tegra_bpmp_channel *channel;

		channel = &bpmp->threaded_channels[i];

		if (tegra_bpmp_is_response_ready(channel)) {
			tegra_bpmp_channel_signal(channel);
			clear_bit(i, busy);
		}
	}

	spin_unlock(&bpmp->lock);
}

static int tegra_bpmp_probe(struct platform_device *pdev)
{
	struct tegra_bpmp *bpmp;
	char tag[TAG_SZ];
	size_t size;
	int err;

	bpmp = devm_kzalloc(&pdev->dev, sizeof(*bpmp), GFP_KERNEL);
	if (!bpmp)
		return -ENOMEM;

	bpmp->soc = of_device_get_match_data(&pdev->dev);
	bpmp->dev = &pdev->dev;

	INIT_LIST_HEAD(&bpmp->mrqs);
	spin_lock_init(&bpmp->lock);

	bpmp->threaded.count = bpmp->soc->channels.thread.count;
	sema_init(&bpmp->threaded.lock, bpmp->threaded.count);

	size = BITS_TO_LONGS(bpmp->threaded.count) * sizeof(long);

	bpmp->threaded.allocated = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!bpmp->threaded.allocated)
		return -ENOMEM;

	bpmp->threaded.busy = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!bpmp->threaded.busy)
		return -ENOMEM;

	spin_lock_init(&bpmp->atomic_tx_lock);
	bpmp->tx_channel = devm_kzalloc(&pdev->dev, sizeof(*bpmp->tx_channel),
					GFP_KERNEL);
	if (!bpmp->tx_channel)
		return -ENOMEM;

	bpmp->rx_channel = devm_kzalloc(&pdev->dev, sizeof(*bpmp->rx_channel),
	                                GFP_KERNEL);
	if (!bpmp->rx_channel)
		return -ENOMEM;

	bpmp->threaded_channels = devm_kcalloc(&pdev->dev, bpmp->threaded.count,
					       sizeof(*bpmp->threaded_channels),
					       GFP_KERNEL);
	if (!bpmp->threaded_channels)
		return -ENOMEM;

	err = bpmp->soc->ops->init(bpmp);
	if (err < 0)
		return err;

	err = tegra_bpmp_request_mrq(bpmp, MRQ_PING,
				     tegra_bpmp_mrq_handle_ping, bpmp);
	if (err < 0)
		goto deinit;

	err = tegra_bpmp_ping(bpmp);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to ping BPMP: %d\n", err);
		goto free_mrq;
	}

	err = tegra_bpmp_get_firmware_tag(bpmp, tag, sizeof(tag));
	if (err < 0) {
		dev_err(&pdev->dev, "failed to get firmware tag: %d\n", err);
		goto free_mrq;
	}

	dev_info(&pdev->dev, "firmware: %.*s\n", (int)sizeof(tag), tag);

	platform_set_drvdata(pdev, bpmp);

	err = of_platform_default_populate(pdev->dev.of_node, NULL, &pdev->dev);
	if (err < 0)
		goto free_mrq;

	if (of_find_property(pdev->dev.of_node, "#clock-cells", NULL)) {
		err = tegra_bpmp_init_clocks(bpmp);
		if (err < 0)
			goto free_mrq;
	}

	if (of_find_property(pdev->dev.of_node, "#reset-cells", NULL)) {
		err = tegra_bpmp_init_resets(bpmp);
		if (err < 0)
			goto free_mrq;
	}

	if (of_find_property(pdev->dev.of_node, "#power-domain-cells", NULL)) {
		err = tegra_bpmp_init_powergates(bpmp);
		if (err < 0)
			goto free_mrq;
	}

	err = tegra_bpmp_init_debugfs(bpmp);
	if (err < 0)
		dev_err(&pdev->dev, "debugfs initialization failed: %d\n", err);

	return 0;

free_mrq:
	tegra_bpmp_free_mrq(bpmp, MRQ_PING, bpmp);
deinit:
	if (bpmp->soc->ops->deinit)
		bpmp->soc->ops->deinit(bpmp);

	return err;
}

static int __maybe_unused tegra_bpmp_resume(struct device *dev)
{
	struct tegra_bpmp *bpmp = dev_get_drvdata(dev);

	if (bpmp->soc->ops->resume)
		return bpmp->soc->ops->resume(bpmp);
	else
		return 0;
}

static const struct dev_pm_ops tegra_bpmp_pm_ops = {
	.resume_noirq = tegra_bpmp_resume,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_186_SOC) || \
    IS_ENABLED(CONFIG_ARCH_TEGRA_194_SOC) || \
    IS_ENABLED(CONFIG_ARCH_TEGRA_234_SOC)
static const struct tegra_bpmp_soc tegra186_soc = {
	.channels = {
		.cpu_tx = {
			.offset = 3,
			.timeout = 60 * USEC_PER_SEC,
		},
		.thread = {
			.offset = 0,
			.count = 3,
			.timeout = 600 * USEC_PER_SEC,
		},
		.cpu_rx = {
			.offset = 13,
			.timeout = 0,
		},
	},
	.ops = &tegra186_bpmp_ops,
	.num_resets = 193,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
static const struct tegra_bpmp_soc tegra210_soc = {
	.channels = {
		.cpu_tx = {
			.offset = 0,
			.count = 1,
			.timeout = 60 * USEC_PER_SEC,
		},
		.thread = {
			.offset = 4,
			.count = 1,
			.timeout = 600 * USEC_PER_SEC,
		},
		.cpu_rx = {
			.offset = 8,
			.count = 1,
			.timeout = 0,
		},
	},
	.ops = &tegra210_bpmp_ops,
};
#endif

static const struct of_device_id tegra_bpmp_match[] = {
#if IS_ENABLED(CONFIG_ARCH_TEGRA_186_SOC) || \
    IS_ENABLED(CONFIG_ARCH_TEGRA_194_SOC) || \
    IS_ENABLED(CONFIG_ARCH_TEGRA_234_SOC)
	{ .compatible = "nvidia,tegra186-bpmp", .data = &tegra186_soc },
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
	{ .compatible = "nvidia,tegra210-bpmp", .data = &tegra210_soc },
#endif
	{ }
};

static struct platform_driver tegra_bpmp_driver = {
	.driver = {
		.name = "tegra-bpmp",
		.of_match_table = tegra_bpmp_match,
		.pm = &tegra_bpmp_pm_ops,
		.suppress_bind_attrs = true,
	},
	.probe = tegra_bpmp_probe,
};
builtin_platform_driver(tegra_bpmp_driver);
