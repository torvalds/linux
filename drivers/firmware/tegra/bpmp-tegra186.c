// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, NVIDIA CORPORATION.
 */

#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>
#include <soc/tegra/ivc.h>

#include "bpmp-private.h"

struct tegra186_bpmp {
	struct tegra_bpmp *parent;

	struct {
		struct gen_pool *pool;
		union {
			void __iomem *sram;
			void *dram;
		};
		dma_addr_t phys;
	} tx, rx;

	struct {
		struct mbox_client client;
		struct mbox_chan *channel;
	} mbox;
};

static inline struct tegra_bpmp *
mbox_client_to_bpmp(struct mbox_client *client)
{
	struct tegra186_bpmp *priv;

	priv = container_of(client, struct tegra186_bpmp, mbox.client);

	return priv->parent;
}

static bool tegra186_bpmp_is_message_ready(struct tegra_bpmp_channel *channel)
{
	int err;

	err = tegra_ivc_read_get_next_frame(channel->ivc, &channel->ib);
	if (err) {
		iosys_map_clear(&channel->ib);
		return false;
	}

	return true;
}

static bool tegra186_bpmp_is_channel_free(struct tegra_bpmp_channel *channel)
{
	int err;

	err = tegra_ivc_write_get_next_frame(channel->ivc, &channel->ob);
	if (err) {
		iosys_map_clear(&channel->ob);
		return false;
	}

	return true;
}

static int tegra186_bpmp_ack_message(struct tegra_bpmp_channel *channel)
{
	return tegra_ivc_read_advance(channel->ivc);
}

static int tegra186_bpmp_post_message(struct tegra_bpmp_channel *channel)
{
	return tegra_ivc_write_advance(channel->ivc);
}

static int tegra186_bpmp_ring_doorbell(struct tegra_bpmp *bpmp)
{
	struct tegra186_bpmp *priv = bpmp->priv;
	int err;

	err = mbox_send_message(priv->mbox.channel, NULL);
	if (err < 0)
		return err;

	mbox_client_txdone(priv->mbox.channel, 0);

	return 0;
}

static void tegra186_bpmp_ivc_notify(struct tegra_ivc *ivc, void *data)
{
	struct tegra_bpmp *bpmp = data;
	struct tegra186_bpmp *priv = bpmp->priv;

	if (WARN_ON(priv->mbox.channel == NULL))
		return;

	tegra186_bpmp_ring_doorbell(bpmp);
}

static int tegra186_bpmp_channel_init(struct tegra_bpmp_channel *channel,
				      struct tegra_bpmp *bpmp,
				      unsigned int index)
{
	struct tegra186_bpmp *priv = bpmp->priv;
	size_t message_size, queue_size;
	struct iosys_map rx, tx;
	unsigned int offset;
	int err;

	channel->ivc = devm_kzalloc(bpmp->dev, sizeof(*channel->ivc),
				    GFP_KERNEL);
	if (!channel->ivc)
		return -ENOMEM;

	message_size = tegra_ivc_align(MSG_MIN_SZ);
	queue_size = tegra_ivc_total_queue_size(message_size);
	offset = queue_size * index;

	if (priv->rx.pool) {
		iosys_map_set_vaddr_iomem(&rx, priv->rx.sram + offset);
		iosys_map_set_vaddr_iomem(&tx, priv->tx.sram + offset);
	} else {
		iosys_map_set_vaddr(&rx, priv->rx.dram + offset);
		iosys_map_set_vaddr(&tx, priv->tx.dram + offset);
	}

	err = tegra_ivc_init(channel->ivc, NULL, &rx, priv->rx.phys + offset, &tx,
			     priv->tx.phys + offset, 1, message_size, tegra186_bpmp_ivc_notify,
			     bpmp);
	if (err < 0) {
		dev_err(bpmp->dev, "failed to setup IVC for channel %u: %d\n",
			index, err);
		return err;
	}

	init_completion(&channel->completion);
	channel->bpmp = bpmp;

	return 0;
}

static void tegra186_bpmp_channel_reset(struct tegra_bpmp_channel *channel)
{
	/* reset the channel state */
	tegra_ivc_reset(channel->ivc);

	/* sync the channel state with BPMP */
	while (tegra_ivc_notified(channel->ivc))
		;
}

static void tegra186_bpmp_channel_cleanup(struct tegra_bpmp_channel *channel)
{
	tegra_ivc_cleanup(channel->ivc);
}

static void mbox_handle_rx(struct mbox_client *client, void *data)
{
	struct tegra_bpmp *bpmp = mbox_client_to_bpmp(client);

	tegra_bpmp_handle_rx(bpmp);
}

static void tegra186_bpmp_teardown_channels(struct tegra_bpmp *bpmp)
{
	struct tegra186_bpmp *priv = bpmp->priv;
	unsigned int i;

	for (i = 0; i < bpmp->threaded.count; i++) {
		if (!bpmp->threaded_channels[i].bpmp)
			continue;

		tegra186_bpmp_channel_cleanup(&bpmp->threaded_channels[i]);
	}

	tegra186_bpmp_channel_cleanup(bpmp->rx_channel);
	tegra186_bpmp_channel_cleanup(bpmp->tx_channel);

	if (priv->tx.pool) {
		gen_pool_free(priv->tx.pool, (unsigned long)priv->tx.sram, 4096);
		gen_pool_free(priv->rx.pool, (unsigned long)priv->rx.sram, 4096);
	}
}

static int tegra186_bpmp_dram_init(struct tegra_bpmp *bpmp)
{
	struct tegra186_bpmp *priv = bpmp->priv;
	struct device_node *np;
	struct resource res;
	size_t size;
	int err;

	np = of_parse_phandle(bpmp->dev->of_node, "memory-region", 0);
	if (!np)
		return -ENODEV;

	err = of_address_to_resource(np, 0, &res);
	if (err < 0) {
		dev_warn(bpmp->dev, "failed to parse memory region: %d\n", err);
		return err;
	}

	size = resource_size(&res);

	if (size < SZ_8K) {
		dev_warn(bpmp->dev, "DRAM region must be larger than 8 KiB\n");
		return -EINVAL;
	}

	priv->tx.phys = res.start;
	priv->rx.phys = res.start + SZ_4K;

	priv->tx.dram = devm_memremap(bpmp->dev, priv->tx.phys, size,
				      MEMREMAP_WC);
	if (IS_ERR(priv->tx.dram)) {
		err = PTR_ERR(priv->tx.dram);
		dev_warn(bpmp->dev, "failed to map DRAM region: %d\n", err);
		return err;
	}

	priv->rx.dram = priv->tx.dram + SZ_4K;

	return 0;
}

static int tegra186_bpmp_sram_init(struct tegra_bpmp *bpmp)
{
	struct tegra186_bpmp *priv = bpmp->priv;
	int err;

	priv->tx.pool = of_gen_pool_get(bpmp->dev->of_node, "shmem", 0);
	if (!priv->tx.pool) {
		dev_err(bpmp->dev, "TX shmem pool not found\n");
		return -EPROBE_DEFER;
	}

	priv->tx.sram = (void __iomem *)gen_pool_dma_alloc(priv->tx.pool, 4096,
							   &priv->tx.phys);
	if (!priv->tx.sram) {
		dev_err(bpmp->dev, "failed to allocate from TX pool\n");
		return -ENOMEM;
	}

	priv->rx.pool = of_gen_pool_get(bpmp->dev->of_node, "shmem", 1);
	if (!priv->rx.pool) {
		dev_err(bpmp->dev, "RX shmem pool not found\n");
		err = -EPROBE_DEFER;
		goto free_tx;
	}

	priv->rx.sram = (void __iomem *)gen_pool_dma_alloc(priv->rx.pool, 4096,
							   &priv->rx.phys);
	if (!priv->rx.sram) {
		dev_err(bpmp->dev, "failed to allocate from RX pool\n");
		err = -ENOMEM;
		goto free_tx;
	}

	return 0;

free_tx:
	gen_pool_free(priv->tx.pool, (unsigned long)priv->tx.sram, 4096);

	return err;
}

static int tegra186_bpmp_setup_channels(struct tegra_bpmp *bpmp)
{
	unsigned int i;
	int err;

	err = tegra186_bpmp_dram_init(bpmp);
	if (err == -ENODEV) {
		err = tegra186_bpmp_sram_init(bpmp);
		if (err < 0)
			return err;
	}

	err = tegra186_bpmp_channel_init(bpmp->tx_channel, bpmp,
					 bpmp->soc->channels.cpu_tx.offset);
	if (err < 0)
		return err;

	err = tegra186_bpmp_channel_init(bpmp->rx_channel, bpmp,
					 bpmp->soc->channels.cpu_rx.offset);
	if (err < 0) {
		tegra186_bpmp_channel_cleanup(bpmp->tx_channel);
		return err;
	}

	for (i = 0; i < bpmp->threaded.count; i++) {
		unsigned int index = bpmp->soc->channels.thread.offset + i;

		err = tegra186_bpmp_channel_init(&bpmp->threaded_channels[i],
						 bpmp, index);
		if (err < 0)
			break;
	}

	if (err < 0)
		tegra186_bpmp_teardown_channels(bpmp);

	return err;
}

static void tegra186_bpmp_reset_channels(struct tegra_bpmp *bpmp)
{
	unsigned int i;

	/* reset message channels */
	tegra186_bpmp_channel_reset(bpmp->tx_channel);
	tegra186_bpmp_channel_reset(bpmp->rx_channel);

	for (i = 0; i < bpmp->threaded.count; i++)
		tegra186_bpmp_channel_reset(&bpmp->threaded_channels[i]);
}

static int tegra186_bpmp_init(struct tegra_bpmp *bpmp)
{
	struct tegra186_bpmp *priv;
	int err;

	priv = devm_kzalloc(bpmp->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->parent = bpmp;
	bpmp->priv = priv;

	err = tegra186_bpmp_setup_channels(bpmp);
	if (err < 0)
		return err;

	/* mbox registration */
	priv->mbox.client.dev = bpmp->dev;
	priv->mbox.client.rx_callback = mbox_handle_rx;
	priv->mbox.client.tx_block = false;
	priv->mbox.client.knows_txdone = false;

	priv->mbox.channel = mbox_request_channel(&priv->mbox.client, 0);
	if (IS_ERR(priv->mbox.channel)) {
		err = PTR_ERR(priv->mbox.channel);
		dev_err(bpmp->dev, "failed to get HSP mailbox: %d\n", err);
		tegra186_bpmp_teardown_channels(bpmp);
		return err;
	}

	tegra186_bpmp_reset_channels(bpmp);

	return 0;
}

static void tegra186_bpmp_deinit(struct tegra_bpmp *bpmp)
{
	struct tegra186_bpmp *priv = bpmp->priv;

	mbox_free_channel(priv->mbox.channel);

	tegra186_bpmp_teardown_channels(bpmp);
}

static int tegra186_bpmp_resume(struct tegra_bpmp *bpmp)
{
	tegra186_bpmp_reset_channels(bpmp);

	return 0;
}

const struct tegra_bpmp_ops tegra186_bpmp_ops = {
	.init = tegra186_bpmp_init,
	.deinit = tegra186_bpmp_deinit,
	.is_response_ready = tegra186_bpmp_is_message_ready,
	.is_request_ready = tegra186_bpmp_is_message_ready,
	.ack_response = tegra186_bpmp_ack_message,
	.ack_request = tegra186_bpmp_ack_message,
	.is_response_channel_free = tegra186_bpmp_is_channel_free,
	.is_request_channel_free = tegra186_bpmp_is_channel_free,
	.post_response = tegra186_bpmp_post_message,
	.post_request = tegra186_bpmp_post_message,
	.ring_doorbell = tegra186_bpmp_ring_doorbell,
	.resume = tegra186_bpmp_resume,
};
