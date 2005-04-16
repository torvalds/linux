/*
 *  linux/arch/arm/kernel/dma.c
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Front-end to the DMA handling.  This handles the allocation/freeing
 *  of DMA channels, and provides a unified interface to the machines
 *  DMA facilities.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mman.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/errno.h>

#include <asm/dma.h>

#include <asm/mach/dma.h>

DEFINE_SPINLOCK(dma_spin_lock);

#if MAX_DMA_CHANNELS > 0

static dma_t dma_chan[MAX_DMA_CHANNELS];

/*
 * Get dma list for /proc/dma
 */
int get_dma_list(char *buf)
{
	dma_t *dma;
	char *p = buf;
	int i;

	for (i = 0, dma = dma_chan; i < MAX_DMA_CHANNELS; i++, dma++)
		if (dma->lock)
			p += sprintf(p, "%2d: %14s %s\n", i,
				     dma->d_ops->type, dma->device_id);

	return p - buf;
}

/*
 * Request DMA channel
 *
 * On certain platforms, we have to allocate an interrupt as well...
 */
int request_dma(dmach_t channel, const char *device_id)
{
	dma_t *dma = dma_chan + channel;
	int ret;

	if (channel >= MAX_DMA_CHANNELS || !dma->d_ops)
		goto bad_dma;

	if (xchg(&dma->lock, 1) != 0)
		goto busy;

	dma->device_id = device_id;
	dma->active    = 0;
	dma->invalid   = 1;

	ret = 0;
	if (dma->d_ops->request)
		ret = dma->d_ops->request(channel, dma);

	if (ret)
		xchg(&dma->lock, 0);

	return ret;

bad_dma:
	printk(KERN_ERR "dma: trying to allocate DMA%d\n", channel);
	return -EINVAL;

busy:
	return -EBUSY;
}

/*
 * Free DMA channel
 *
 * On certain platforms, we have to free interrupt as well...
 */
void free_dma(dmach_t channel)
{
	dma_t *dma = dma_chan + channel;

	if (channel >= MAX_DMA_CHANNELS || !dma->d_ops)
		goto bad_dma;

	if (dma->active) {
		printk(KERN_ERR "dma%d: freeing active DMA\n", channel);
		dma->d_ops->disable(channel, dma);
		dma->active = 0;
	}

	if (xchg(&dma->lock, 0) != 0) {
		if (dma->d_ops->free)
			dma->d_ops->free(channel, dma);
		return;
	}

	printk(KERN_ERR "dma%d: trying to free free DMA\n", channel);
	return;

bad_dma:
	printk(KERN_ERR "dma: trying to free DMA%d\n", channel);
}

/* Set DMA Scatter-Gather list
 */
void set_dma_sg (dmach_t channel, struct scatterlist *sg, int nr_sg)
{
	dma_t *dma = dma_chan + channel;

	if (dma->active)
		printk(KERN_ERR "dma%d: altering DMA SG while "
		       "DMA active\n", channel);

	dma->sg = sg;
	dma->sgcount = nr_sg;
	dma->using_sg = 1;
	dma->invalid = 1;
}

/* Set DMA address
 *
 * Copy address to the structure, and set the invalid bit
 */
void set_dma_addr (dmach_t channel, unsigned long physaddr)
{
	dma_t *dma = dma_chan + channel;

	if (dma->active)
		printk(KERN_ERR "dma%d: altering DMA address while "
		       "DMA active\n", channel);

	dma->sg = &dma->buf;
	dma->sgcount = 1;
	dma->buf.__address = bus_to_virt(physaddr);
	dma->using_sg = 0;
	dma->invalid = 1;
}

/* Set DMA byte count
 *
 * Copy address to the structure, and set the invalid bit
 */
void set_dma_count (dmach_t channel, unsigned long count)
{
	dma_t *dma = dma_chan + channel;

	if (dma->active)
		printk(KERN_ERR "dma%d: altering DMA count while "
		       "DMA active\n", channel);

	dma->sg = &dma->buf;
	dma->sgcount = 1;
	dma->buf.length = count;
	dma->using_sg = 0;
	dma->invalid = 1;
}

/* Set DMA direction mode
 */
void set_dma_mode (dmach_t channel, dmamode_t mode)
{
	dma_t *dma = dma_chan + channel;

	if (dma->active)
		printk(KERN_ERR "dma%d: altering DMA mode while "
		       "DMA active\n", channel);

	dma->dma_mode = mode;
	dma->invalid = 1;
}

/* Enable DMA channel
 */
void enable_dma (dmach_t channel)
{
	dma_t *dma = dma_chan + channel;

	if (!dma->lock)
		goto free_dma;

	if (dma->active == 0) {
		dma->active = 1;
		dma->d_ops->enable(channel, dma);
	}
	return;

free_dma:
	printk(KERN_ERR "dma%d: trying to enable free DMA\n", channel);
	BUG();
}

/* Disable DMA channel
 */
void disable_dma (dmach_t channel)
{
	dma_t *dma = dma_chan + channel;

	if (!dma->lock)
		goto free_dma;

	if (dma->active == 1) {
		dma->active = 0;
		dma->d_ops->disable(channel, dma);
	}
	return;

free_dma:
	printk(KERN_ERR "dma%d: trying to disable free DMA\n", channel);
	BUG();
}

/*
 * Is the specified DMA channel active?
 */
int dma_channel_active(dmach_t channel)
{
	return dma_chan[channel].active;
}

void set_dma_page(dmach_t channel, char pagenr)
{
	printk(KERN_ERR "dma%d: trying to set_dma_page\n", channel);
}

void set_dma_speed(dmach_t channel, int cycle_ns)
{
	dma_t *dma = dma_chan + channel;
	int ret = 0;

	if (dma->d_ops->setspeed)
		ret = dma->d_ops->setspeed(channel, dma, cycle_ns);
	dma->speed = ret;
}

int get_dma_residue(dmach_t channel)
{
	dma_t *dma = dma_chan + channel;
	int ret = 0;

	if (dma->d_ops->residue)
		ret = dma->d_ops->residue(channel, dma);

	return ret;
}

void __init init_dma(void)
{
	arch_dma_init(dma_chan);
}

#else

int request_dma(dmach_t channel, const char *device_id)
{
	return -EINVAL;
}

int get_dma_residue(dmach_t channel)
{
	return 0;
}

#define GLOBAL_ALIAS(_a,_b) asm (".set " #_a "," #_b "; .globl " #_a)
GLOBAL_ALIAS(disable_dma, get_dma_residue);
GLOBAL_ALIAS(enable_dma, get_dma_residue);
GLOBAL_ALIAS(free_dma, get_dma_residue);
GLOBAL_ALIAS(get_dma_list, get_dma_residue);
GLOBAL_ALIAS(set_dma_mode, get_dma_residue);
GLOBAL_ALIAS(set_dma_page, get_dma_residue);
GLOBAL_ALIAS(set_dma_count, get_dma_residue);
GLOBAL_ALIAS(set_dma_addr, get_dma_residue);
GLOBAL_ALIAS(set_dma_sg, get_dma_residue);
GLOBAL_ALIAS(set_dma_speed, get_dma_residue);
GLOBAL_ALIAS(init_dma, get_dma_residue);

#endif

EXPORT_SYMBOL(request_dma);
EXPORT_SYMBOL(free_dma);
EXPORT_SYMBOL(enable_dma);
EXPORT_SYMBOL(disable_dma);
EXPORT_SYMBOL(set_dma_addr);
EXPORT_SYMBOL(set_dma_count);
EXPORT_SYMBOL(set_dma_mode);
EXPORT_SYMBOL(set_dma_page);
EXPORT_SYMBOL(get_dma_residue);
EXPORT_SYMBOL(set_dma_sg);
EXPORT_SYMBOL(set_dma_speed);

EXPORT_SYMBOL(dma_spin_lock);
