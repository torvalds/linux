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
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <asm/dma.h>

DEFINE_SPINLOCK(dma_spin_lock);
static LIST_HEAD(registered_dmac_list);

/*
 * A brief note about the reasons for this API as it stands.
 *
 * For starters, the old ISA DMA API didn't work for us for a number of
 * reasons, for one, the vast majority of channels on the SH DMAC are
 * dual-address mode only, and both the new and the old DMA APIs are after the
 * concept of managing a DMA buffer, which doesn't overly fit this model very
 * well. In addition to which, the new API is largely geared at IOMMUs and
 * GARTs, and doesn't even support the channel notion very well.
 *
 * The other thing that's a marginal issue, is the sheer number of random DMA
 * engines that are present (ie, in boards like the Dreamcast), some of which
 * cascade off of the SH DMAC, and others do not. As such, there was a real
 * need for a scalable subsystem that could deal with both single and
 * dual-address mode usage, in addition to interoperating with cascaded DMACs.
 *
 * There really isn't any reason why this needs to be SH specific, though I'm
 * not aware of too many other processors (with the exception of some MIPS)
 * that have the same concept of a dual address mode, or any real desire to
 * actually make use of the DMAC even if such a subsystem were exposed
 * elsewhere.
 *
 * The idea for this was derived from the ARM port, which acted as an excellent
 * reference when trying to address these issues.
 *
 * It should also be noted that the decision to add Yet Another DMA API(tm) to
 * the kernel wasn't made easily, and was only decided upon after conferring
 * with jejb with regards to the state of the old and new APIs as they applied
 * to these circumstances. Philip Blundell was also a great help in figuring
 * out some single-address mode DMA semantics that were otherwise rather
 * confusing.
 */

struct dma_info *get_dma_info(unsigned int chan)
{
	struct dma_info *info;
	unsigned int total = 0;

	/*
	 * Look for each DMAC's range to determine who the owner of
	 * the channel is.
	 */
	list_for_each_entry(info, &registered_dmac_list, list) {
		total += info->nr_channels;
		if (chan > total)
			continue;

		return info;
	}

	return NULL;
}

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

	if (!info)
		return ERR_PTR(-EINVAL);

	return info->channels + chan;
}

int get_dma_residue(unsigned int chan)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = &info->channels[chan];

	if (info->ops->get_residue)
		return info->ops->get_residue(channel);

	return 0;
}

int request_dma(unsigned int chan, const char *dev_id)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = &info->channels[chan];

	down(&channel->sem);

	if (!info->ops || chan >= MAX_DMA_CHANNELS) {
		up(&channel->sem);
		return -EINVAL;
	}

	atomic_set(&channel->busy, 1);

	strlcpy(channel->dev_id, dev_id, sizeof(channel->dev_id));

	up(&channel->sem);

	if (info->ops->request)
		return info->ops->request(channel);

	return 0;
}

void free_dma(unsigned int chan)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = &info->channels[chan];

	if (info->ops->free)
		info->ops->free(channel);

	atomic_set(&channel->busy, 0);
}

void dma_wait_for_completion(unsigned int chan)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = &info->channels[chan];

	if (channel->flags & DMA_TEI_CAPABLE) {
		wait_event(channel->wait_queue,
			   (info->ops->get_residue(channel) == 0));
		return;
	}

	while (info->ops->get_residue(channel))
		cpu_relax();
}

void dma_configure_channel(unsigned int chan, unsigned long flags)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = &info->channels[chan];

	if (info->ops->configure)
		info->ops->configure(channel, flags);
}

int dma_xfer(unsigned int chan, unsigned long from,
	     unsigned long to, size_t size, unsigned int mode)
{
	struct dma_info *info = get_dma_info(chan);
	struct dma_channel *channel = &info->channels[chan];

	channel->sar	= from;
	channel->dar	= to;
	channel->count	= size;
	channel->mode	= mode;

	return info->ops->xfer(channel);
}

#ifdef CONFIG_PROC_FS
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
#endif


int register_dmac(struct dma_info *info)
{
	unsigned int total_channels, i;

	INIT_LIST_HEAD(&info->list);

	printk(KERN_INFO "DMA: Registering %s handler (%d channel%s).\n",
	       info->name, info->nr_channels,
	       info->nr_channels > 1 ? "s" : "");

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

		info->channels = kmalloc(size, GFP_KERNEL);
		if (!info->channels)
			return -ENOMEM;

		memset(info->channels, 0, size);
	}

	total_channels = get_nr_channels();
	for (i = 0; i < info->nr_channels; i++) {
		struct dma_channel *chan = info->channels + i;

		chan->chan = i;
		chan->vchan = i + total_channels;

		memcpy(chan->dev_id, "Unused", 7);

		if (info->flags & DMAC_CHANNELS_TEI_CAPABLE)
			chan->flags |= DMA_TEI_CAPABLE;

		init_MUTEX(&chan->sem);
		init_waitqueue_head(&chan->wait_queue);

		dma_create_sysfs_files(chan, info);
	}

	list_add(&info->list, &registered_dmac_list);

	return 0;
}

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

static int __init dma_api_init(void)
{
	printk("DMA: Registering DMA API.\n");

#ifdef CONFIG_PROC_FS
	create_proc_read_entry("dma", 0, 0, dma_read_proc, 0);
#endif

	return 0;
}

subsys_initcall(dma_api_init);

MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>");
MODULE_DESCRIPTION("DMA API for SuperH");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(request_dma);
EXPORT_SYMBOL(free_dma);
EXPORT_SYMBOL(register_dmac);
EXPORT_SYMBOL(get_dma_residue);
EXPORT_SYMBOL(get_dma_info);
EXPORT_SYMBOL(get_dma_channel);
EXPORT_SYMBOL(dma_xfer);
EXPORT_SYMBOL(dma_wait_for_completion);
EXPORT_SYMBOL(dma_configure_channel);

