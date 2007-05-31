/*
 * arch/sh/drivers/dma/dma-api.c
 *
 * SuperH-specific DMA management API
 *
 * Copyright (C) 2003, 2004, 2005  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <asm/dma.h>

DEFINE_SPINLOCK(dma_spin_lock);
static LIST_HEAD(registered_dmac_list);

struct dma_info *get_dma_info(unsigned int chan)
{
	struct dma_info *info;

	/*
	 * Look for each DMAC's range to determine who the owner of
	 * the channel is.
	 */
	list_for_each_entry(info, &registered_dmac_list, list) {
		if ((chan <  info->first_channel_nr) ||
		    (chan >= info->first_channel_nr + info->nr_channels))
			continue;

		return info;
	}

	return NULL;
}
EXPORT_SYMBOL(get_dma_info);

struct dma_info *get_dma_info_by_name(const char *dmac_name)
{
	struct dma_info *info;

	list_for_each_entry(info, &registered_dmac_list, list) {
		if (dmac_name && (strcmp(dmac_name, info->name) != 0))
			continue;
		else
			return info;
	}

	return NULL;
}
EXPORT_SYMBOL(get_dma_info_by_name);

static unsigned int get_nr_channels(void)
{
	struct dma_info *info;
	unsigned int nr = 0;

	if (unlikely(list_empty(&registered_dmac_list)))
		return nr;

	list_for_each_entry(info, &registered_dmac_list, list)
		nr += info->nr_channels;

	return nr;
}

struct dma_channel *get_dma_channel(unsigned int chan)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel;
	int i;

	if (unlikely(!info))
		return ERR_PTR(-EINVAL);

	for (i = 0; i < info->nr_channels; i++) {
		channel = &info->channels[i];
		if (channel->chan == chan)
			return channel;
	}

	return NULL;
}
EXPORT_SYMBOL(get_dma_channel);

int get_dma_residue(unsigned int chan)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = get_dma_channel(chan);

	if (info->ops->get_residue)
		return info->ops->get_residue(channel);

	return 0;
}
EXPORT_SYMBOL(get_dma_residue);

static int search_cap(const char **haystack, const char *needle)
{
	const char **p;

	for (p = haystack; *p; p++)
		if (strcmp(*p, needle) == 0)
			return 1;

	return 0;
}

/**
 * request_dma_bycap - Allocate a DMA channel based on its capabilities
 * @dmac: List of DMA controllers to search
 * @caps: List of capabilities
 *
 * Search all channels of all DMA controllers to find a channel which
 * matches the requested capabilities. The result is the channel
 * number if a match is found, or %-ENODEV if no match is found.
 *
 * Note that not all DMA controllers export capabilities, in which
 * case they can never be allocated using this API, and so
 * request_dma() must be used specifying the channel number.
 */
int request_dma_bycap(const char **dmac, const char **caps, const char *dev_id)
{
	unsigned int found = 0;
	struct dma_info *info;
	const char **p;
	int i;

	BUG_ON(!dmac || !caps);

	list_for_each_entry(info, &registered_dmac_list, list)
		if (strcmp(*dmac, info->name) == 0) {
			found = 1;
			break;
		}

	if (!found)
		return -ENODEV;

	for (i = 0; i < info->nr_channels; i++) {
		struct dma_channel *channel = &info->channels[i];

		if (unlikely(!channel->caps))
			continue;

		for (p = caps; *p; p++) {
			if (!search_cap(channel->caps, *p))
				break;
			if (request_dma(channel->chan, dev_id) == 0)
				return channel->chan;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL(request_dma_bycap);

int dmac_search_free_channel(const char *dev_id)
{
	struct dma_channel *channel = { 0 };
	struct dma_info *info = get_dma_info(0);
	int i;

	for (i = 0; i < info->nr_channels; i++) {
		channel = &info->channels[i];
		if (unlikely(!channel))
			return -ENODEV;

		if (atomic_read(&channel->busy) == 0)
			break;
	}

	if (info->ops->request) {
		int result = info->ops->request(channel);
		if (result)
			return result;

		atomic_set(&channel->busy, 1);
		return channel->chan;
	}

	return -ENOSYS;
}

int request_dma(unsigned int chan, const char *dev_id)
{
	struct dma_channel *channel = { 0 };
	struct dma_info *info = get_dma_info(chan);
	int result;

	channel = get_dma_channel(chan);
	if (atomic_xchg(&channel->busy, 1))
		return -EBUSY;

	strlcpy(channel->dev_id, dev_id, sizeof(channel->dev_id));

	if (info->ops->request) {
		result = info->ops->request(channel);
		if (result)
			atomic_set(&channel->busy, 0);

		return result;
	}

	return 0;
}
EXPORT_SYMBOL(request_dma);

void free_dma(unsigned int chan)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = get_dma_channel(chan);

	if (info->ops->free)
		info->ops->free(channel);

	atomic_set(&channel->busy, 0);
}
EXPORT_SYMBOL(free_dma);

void dma_wait_for_completion(unsigned int chan)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = get_dma_channel(chan);

	if (channel->flags & DMA_TEI_CAPABLE) {
		wait_event(channel->wait_queue,
			   (info->ops->get_residue(channel) == 0));
		return;
	}

	while (info->ops->get_residue(channel))
		cpu_relax();
}
EXPORT_SYMBOL(dma_wait_for_completion);

int register_chan_caps(const char *dmac, struct dma_chan_caps *caps)
{
	struct dma_info *info;
	unsigned int found = 0;
	int i;

	list_for_each_entry(info, &registered_dmac_list, list)
		if (strcmp(dmac, info->name) == 0) {
			found = 1;
			break;
		}

	if (unlikely(!found))
		return -ENODEV;

	for (i = 0; i < info->nr_channels; i++, caps++) {
		struct dma_channel *channel;

		if ((info->first_channel_nr + i) != caps->ch_num)
			return -EINVAL;

		channel = &info->channels[i];
		channel->caps = caps->caplist;
	}

	return 0;
}
EXPORT_SYMBOL(register_chan_caps);

void dma_configure_channel(unsigned int chan, unsigned long flags)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = get_dma_channel(chan);

	if (info->ops->configure)
		info->ops->configure(channel, flags);
}
EXPORT_SYMBOL(dma_configure_channel);

int dma_xfer(unsigned int chan, unsigned long from,
	     unsigned long to, size_t size, unsigned int mode)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = get_dma_channel(chan);

	channel->sar	= from;
	channel->dar	= to;
	channel->count	= size;
	channel->mode	= mode;

	return info->ops->xfer(channel);
}
EXPORT_SYMBOL(dma_xfer);

int dma_extend(unsigned int chan, unsigned long op, void *param)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = get_dma_channel(chan);

	if (info->ops->extend)
		return info->ops->extend(channel, op, param);

	return -ENOSYS;
}
EXPORT_SYMBOL(dma_extend);

static int dma_read_proc(char *buf, char **start, off_t off,
			 int len, int *eof, void *data)
{
	struct dma_info *info;
	char *p = buf;

	if (list_empty(&registered_dmac_list))
		return 0;

	/*
	 * Iterate over each registered DMAC
	 */
	list_for_each_entry(info, &registered_dmac_list, list) {
		int i;

		/*
		 * Iterate over each channel
		 */
		for (i = 0; i < info->nr_channels; i++) {
			struct dma_channel *channel = info->channels + i;

			if (!(channel->flags & DMA_CONFIGURED))
				continue;

			p += sprintf(p, "%2d: %14s    %s\n", i,
				     info->name, channel->dev_id);
		}
	}

	return p - buf;
}

int register_dmac(struct dma_info *info)
{
	unsigned int total_channels, i;

	INIT_LIST_HEAD(&info->list);

	printk(KERN_INFO "DMA: Registering %s handler (%d channel%s).\n",
	       info->name, info->nr_channels, info->nr_channels > 1 ? "s" : "");

	BUG_ON((info->flags & DMAC_CHANNELS_CONFIGURED) && !info->channels);

	info->pdev = platform_device_register_simple((char *)info->name, -1,
						     NULL, 0);
	if (IS_ERR(info->pdev))
		return PTR_ERR(info->pdev);

	/*
	 * Don't touch pre-configured channels
	 */
	if (!(info->flags & DMAC_CHANNELS_CONFIGURED)) {
		unsigned int size;

		size = sizeof(struct dma_channel) * info->nr_channels;

		info->channels = kzalloc(size, GFP_KERNEL);
		if (!info->channels)
			return -ENOMEM;
	}

	total_channels = get_nr_channels();
	for (i = 0; i < info->nr_channels; i++) {
		struct dma_channel *chan = &info->channels[i];

		atomic_set(&chan->busy, 0);

		chan->chan  = info->first_channel_nr + i;
		chan->vchan = info->first_channel_nr + i + total_channels;

		memcpy(chan->dev_id, "Unused", 7);

		if (info->flags & DMAC_CHANNELS_TEI_CAPABLE)
			chan->flags |= DMA_TEI_CAPABLE;

		init_waitqueue_head(&chan->wait_queue);
		dma_create_sysfs_files(chan, info);
	}

	list_add(&info->list, &registered_dmac_list);

	return 0;
}
EXPORT_SYMBOL(register_dmac);

void unregister_dmac(struct dma_info *info)
{
	unsigned int i;

	for (i = 0; i < info->nr_channels; i++)
		dma_remove_sysfs_files(info->channels + i, info);

	if (!(info->flags & DMAC_CHANNELS_CONFIGURED))
		kfree(info->channels);

	list_del(&info->list);
	platform_device_unregister(info->pdev);
}
EXPORT_SYMBOL(unregister_dmac);

static int __init dma_api_init(void)
{
	printk(KERN_NOTICE "DMA: Registering DMA API.\n");
	create_proc_read_entry("dma", 0, 0, dma_read_proc, 0);
	return 0;
}
subsys_initcall(dma_api_init);

MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>");
MODULE_DESCRIPTION("DMA API for SuperH");
MODULE_LICENSE("GPL");
