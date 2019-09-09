// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/kernel/dma.c
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 *  Front-end to the DMA handling.  This handles the allocation/freeing
 *  of DMA channels, and provides a unified interface to the machines
 *  DMA facilities.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#include <asm/dma.h>

#include <asm/mach/dma.h>

DEFINE_RAW_SPINLOCK(dma_spin_lock);
EXPORT_SYMBOL(dma_spin_lock);

static dma_t *dma_chan[MAX_DMA_CHANNELS];

static inline dma_t *dma_channel(unsigned int chan)
{
	if (chan >= MAX_DMA_CHANNELS)
		return NULL;

	return dma_chan[chan];
}

int __init isa_dma_add(unsigned int chan, dma_t *dma)
{
	if (!dma->d_ops)
		return -EINVAL;

	sg_init_table(&dma->buf, 1);

	if (dma_chan[chan])
		return -EBUSY;
	dma_chan[chan] = dma;
	return 0;
}

/*
 * Request DMA channel
 *
 * On certain platforms, we have to allocate an interrupt as well...
 */
int request_dma(unsigned int chan, const char *device_id)
{
	dma_t *dma = dma_channel(chan);
	int ret;

	if (!dma)
		goto bad_dma;

	if (xchg(&dma->lock, 1) != 0)
		goto busy;

	dma->device_id = device_id;
	dma->active    = 0;
	dma->invalid   = 1;

	ret = 0;
	if (dma->d_ops->request)
		ret = dma->d_ops->request(chan, dma);

	if (ret)
		xchg(&dma->lock, 0);

	return ret;

bad_dma:
	pr_err("dma: trying to allocate DMA%d\n", chan);
	return -EINVAL;

busy:
	return -EBUSY;
}
EXPORT_SYMBOL(request_dma);

/*
 * Free DMA channel
 *
 * On certain platforms, we have to free interrupt as well...
 */
void free_dma(unsigned int chan)
{
	dma_t *dma = dma_channel(chan);

	if (!dma)
		goto bad_dma;

	if (dma->active) {
		pr_err("dma%d: freeing active DMA\n", chan);
		dma->d_ops->disable(chan, dma);
		dma->active = 0;
	}

	if (xchg(&dma->lock, 0) != 0) {
		if (dma->d_ops->free)
			dma->d_ops->free(chan, dma);
		return;
	}

	pr_err("dma%d: trying to free free DMA\n", chan);
	return;

bad_dma:
	pr_err("dma: trying to free DMA%d\n", chan);
}
EXPORT_SYMBOL(free_dma);

/* Set DMA Scatter-Gather list
 */
void set_dma_sg (unsigned int chan, struct scatterlist *sg, int nr_sg)
{
	dma_t *dma = dma_channel(chan);

	if (dma->active)
		pr_err("dma%d: altering DMA SG while DMA active\n", chan);

	dma->sg = sg;
	dma->sgcount = nr_sg;
	dma->invalid = 1;
}
EXPORT_SYMBOL(set_dma_sg);

/* Set DMA address
 *
 * Copy address to the structure, and set the invalid bit
 */
void __set_dma_addr (unsigned int chan, void *addr)
{
	dma_t *dma = dma_channel(chan);

	if (dma->active)
		pr_err("dma%d: altering DMA address while DMA active\n", chan);

	dma->sg = NULL;
	dma->addr = addr;
	dma->invalid = 1;
}
EXPORT_SYMBOL(__set_dma_addr);

/* Set DMA byte count
 *
 * Copy address to the structure, and set the invalid bit
 */
void set_dma_count (unsigned int chan, unsigned long count)
{
	dma_t *dma = dma_channel(chan);

	if (dma->active)
		pr_err("dma%d: altering DMA count while DMA active\n", chan);

	dma->sg = NULL;
	dma->count = count;
	dma->invalid = 1;
}
EXPORT_SYMBOL(set_dma_count);

/* Set DMA direction mode
 */
void set_dma_mode (unsigned int chan, unsigned int mode)
{
	dma_t *dma = dma_channel(chan);

	if (dma->active)
		pr_err("dma%d: altering DMA mode while DMA active\n", chan);

	dma->dma_mode = mode;
	dma->invalid = 1;
}
EXPORT_SYMBOL(set_dma_mode);

/* Enable DMA channel
 */
void enable_dma (unsigned int chan)
{
	dma_t *dma = dma_channel(chan);

	if (!dma->lock)
		goto free_dma;

	if (dma->active == 0) {
		dma->active = 1;
		dma->d_ops->enable(chan, dma);
	}
	return;

free_dma:
	pr_err("dma%d: trying to enable free DMA\n", chan);
	BUG();
}
EXPORT_SYMBOL(enable_dma);

/* Disable DMA channel
 */
void disable_dma (unsigned int chan)
{
	dma_t *dma = dma_channel(chan);

	if (!dma->lock)
		goto free_dma;

	if (dma->active == 1) {
		dma->active = 0;
		dma->d_ops->disable(chan, dma);
	}
	return;

free_dma:
	pr_err("dma%d: trying to disable free DMA\n", chan);
	BUG();
}
EXPORT_SYMBOL(disable_dma);

/*
 * Is the specified DMA channel active?
 */
int dma_channel_active(unsigned int chan)
{
	dma_t *dma = dma_channel(chan);
	return dma->active;
}
EXPORT_SYMBOL(dma_channel_active);

void set_dma_page(unsigned int chan, char pagenr)
{
	pr_err("dma%d: trying to set_dma_page\n", chan);
}
EXPORT_SYMBOL(set_dma_page);

void set_dma_speed(unsigned int chan, int cycle_ns)
{
	dma_t *dma = dma_channel(chan);
	int ret = 0;

	if (dma->d_ops->setspeed)
		ret = dma->d_ops->setspeed(chan, dma, cycle_ns);
	dma->speed = ret;
}
EXPORT_SYMBOL(set_dma_speed);

int get_dma_residue(unsigned int chan)
{
	dma_t *dma = dma_channel(chan);
	int ret = 0;

	if (dma->d_ops->residue)
		ret = dma->d_ops->residue(chan, dma);

	return ret;
}
EXPORT_SYMBOL(get_dma_residue);

#ifdef CONFIG_PROC_FS
static int proc_dma_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0 ; i < MAX_DMA_CHANNELS ; i++) {
		dma_t *dma = dma_channel(i);
		if (dma && dma->lock)
			seq_printf(m, "%2d: %s\n", i, dma->device_id);
	}
	return 0;
}

static int __init proc_dma_init(void)
{
	proc_create_single("dma", 0, NULL, proc_dma_show);
	return 0;
}

__initcall(proc_dma_init);
#endif
