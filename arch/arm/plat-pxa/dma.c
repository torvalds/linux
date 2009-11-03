/*
 *  linux/arch/arm/plat-pxa/dma.c
 *
 *  PXA DMA registration and IRQ dispatching
 *
 *  Author:	Nicolas Pitre
 *  Created:	Nov 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/memory.h>
#include <mach/hardware.h>
#include <mach/dma.h>

#define DMA_DEBUG_NAME		"pxa_dma"
#define DMA_MAX_REQUESTERS	64

struct dma_channel {
	char *name;
	pxa_dma_prio prio;
	void (*irq_handler)(int, void *);
	void *data;
	spinlock_t lock;
};

static struct dma_channel *dma_channels;
static int num_dma_channels;

/*
 * Debug fs
 */
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>

static struct dentry *dbgfs_root, *dbgfs_state, **dbgfs_chan;

static int dbg_show_requester_chan(struct seq_file *s, void *p)
{
	int pos = 0;
	int chan = (int)s->private;
	int i;
	u32 drcmr;

	pos += seq_printf(s, "DMA channel %d requesters list :\n", chan);
	for (i = 0; i < DMA_MAX_REQUESTERS; i++) {
		drcmr = DRCMR(i);
		if ((drcmr & DRCMR_CHLNUM) == chan)
			pos += seq_printf(s, "\tRequester %d (MAPVLD=%d)\n", i,
					  !!(drcmr & DRCMR_MAPVLD));
	}
	return pos;
}

static inline int dbg_burst_from_dcmd(u32 dcmd)
{
	int burst = (dcmd >> 16) & 0x3;

	return burst ? 4 << burst : 0;
}

static int is_phys_valid(unsigned long addr)
{
	return pfn_valid(__phys_to_pfn(addr));
}

#define DCSR_STR(flag) (dcsr & DCSR_##flag ? #flag" " : "")
#define DCMD_STR(flag) (dcmd & DCMD_##flag ? #flag" " : "")

static int dbg_show_descriptors(struct seq_file *s, void *p)
{
	int pos = 0;
	int chan = (int)s->private;
	int i, max_show = 20, burst, width;
	u32 dcmd;
	unsigned long phys_desc;
	struct pxa_dma_desc *desc;
	unsigned long flags;

	spin_lock_irqsave(&dma_channels[chan].lock, flags);
	phys_desc = DDADR(chan);

	pos += seq_printf(s, "DMA channel %d descriptors :\n", chan);
	pos += seq_printf(s, "[%03d] First descriptor unknown\n", 0);
	for (i = 1; i < max_show && is_phys_valid(phys_desc); i++) {
		desc = phys_to_virt(phys_desc);
		dcmd = desc->dcmd;
		burst = dbg_burst_from_dcmd(dcmd);
		width = (1 << ((dcmd >> 14) & 0x3)) >> 1;

		pos += seq_printf(s, "[%03d] Desc at %08lx(virt %p)\n",
				  i, phys_desc, desc);
		pos += seq_printf(s, "\tDDADR = %08x\n", desc->ddadr);
		pos += seq_printf(s, "\tDSADR = %08x\n", desc->dsadr);
		pos += seq_printf(s, "\tDTADR = %08x\n", desc->dtadr);
		pos += seq_printf(s, "\tDCMD  = %08x (%s%s%s%s%s%s%sburst=%d"
				  " width=%d len=%d)\n",
				  dcmd,
				  DCMD_STR(INCSRCADDR), DCMD_STR(INCTRGADDR),
				  DCMD_STR(FLOWSRC), DCMD_STR(FLOWTRG),
				  DCMD_STR(STARTIRQEN), DCMD_STR(ENDIRQEN),
				  DCMD_STR(ENDIAN), burst, width,
				  dcmd & DCMD_LENGTH);
		phys_desc = desc->ddadr;
	}
	if (i == max_show)
		pos += seq_printf(s, "[%03d] Desc at %08lx ... max display reached\n",
				  i, phys_desc);
	else
		pos += seq_printf(s, "[%03d] Desc at %08lx is %s\n",
				  i, phys_desc, phys_desc == DDADR_STOP ?
				  "DDADR_STOP" : "invalid");

	spin_unlock_irqrestore(&dma_channels[chan].lock, flags);
	return pos;
}

static int dbg_show_chan_state(struct seq_file *s, void *p)
{
	int pos = 0;
	int chan = (int)s->private;
	u32 dcsr, dcmd;
	int burst, width;
	static char *str_prio[] = { "high", "normal", "low" };

	dcsr = DCSR(chan);
	dcmd = DCMD(chan);
	burst = dbg_burst_from_dcmd(dcmd);
	width = (1 << ((dcmd >> 14) & 0x3)) >> 1;

	pos += seq_printf(s, "DMA channel %d\n", chan);
	pos += seq_printf(s, "\tPriority : %s\n",
			  str_prio[dma_channels[chan].prio]);
	pos += seq_printf(s, "\tUnaligned transfer bit: %s\n",
			  DALGN & (1 << chan) ? "yes" : "no");
	pos += seq_printf(s, "\tDCSR  = %08x (%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s)\n",
			  dcsr, DCSR_STR(RUN), DCSR_STR(NODESC),
			  DCSR_STR(STOPIRQEN), DCSR_STR(EORIRQEN),
			  DCSR_STR(EORJMPEN), DCSR_STR(EORSTOPEN),
			  DCSR_STR(SETCMPST), DCSR_STR(CLRCMPST),
			  DCSR_STR(CMPST), DCSR_STR(EORINTR), DCSR_STR(REQPEND),
			  DCSR_STR(STOPSTATE), DCSR_STR(ENDINTR),
			  DCSR_STR(STARTINTR), DCSR_STR(BUSERR));

	pos += seq_printf(s, "\tDCMD  = %08x (%s%s%s%s%s%s%sburst=%d width=%d"
			  " len=%d)\n",
			  dcmd,
			  DCMD_STR(INCSRCADDR), DCMD_STR(INCTRGADDR),
			  DCMD_STR(FLOWSRC), DCMD_STR(FLOWTRG),
			  DCMD_STR(STARTIRQEN), DCMD_STR(ENDIRQEN),
			  DCMD_STR(ENDIAN), burst, width, dcmd & DCMD_LENGTH);
	pos += seq_printf(s, "\tDSADR = %08x\n", DSADR(chan));
	pos += seq_printf(s, "\tDTADR = %08x\n", DTADR(chan));
	pos += seq_printf(s, "\tDDADR = %08x\n", DDADR(chan));
	return pos;
}

static int dbg_show_state(struct seq_file *s, void *p)
{
	int pos = 0;

	/* basic device status */
	pos += seq_printf(s, "DMA engine status\n");
	pos += seq_printf(s, "\tChannel number: %d\n", num_dma_channels);

	return pos;
}

#define DBGFS_FUNC_DECL(name) \
static int dbg_open_##name(struct inode *inode, struct file *file) \
{ \
	return single_open(file, dbg_show_##name, inode->i_private); \
} \
static const struct file_operations dbg_fops_##name = { \
	.owner		= THIS_MODULE, \
	.open		= dbg_open_##name, \
	.llseek		= seq_lseek, \
	.read		= seq_read, \
	.release	= single_release, \
}

DBGFS_FUNC_DECL(state);
DBGFS_FUNC_DECL(chan_state);
DBGFS_FUNC_DECL(descriptors);
DBGFS_FUNC_DECL(requester_chan);

static struct dentry *pxa_dma_dbg_alloc_chan(int ch, struct dentry *chandir)
{
	char chan_name[11];
	struct dentry *chan, *chan_state = NULL, *chan_descr = NULL;
	struct dentry *chan_reqs = NULL;
	void *dt;

	scnprintf(chan_name, sizeof(chan_name), "%d", ch);
	chan = debugfs_create_dir(chan_name, chandir);
	dt = (void *)ch;

	if (chan)
		chan_state = debugfs_create_file("state", 0400, chan, dt,
						 &dbg_fops_chan_state);
	if (chan_state)
		chan_descr = debugfs_create_file("descriptors", 0400, chan, dt,
						 &dbg_fops_descriptors);
	if (chan_descr)
		chan_reqs = debugfs_create_file("requesters", 0400, chan, dt,
						&dbg_fops_requester_chan);
	if (!chan_reqs)
		goto err_state;

	return chan;

err_state:
	debugfs_remove_recursive(chan);
	return NULL;
}

static void pxa_dma_init_debugfs(void)
{
	int i;
	struct dentry *chandir;

	dbgfs_root = debugfs_create_dir(DMA_DEBUG_NAME, NULL);
	if (IS_ERR(dbgfs_root) || !dbgfs_root)
		goto err_root;

	dbgfs_state = debugfs_create_file("state", 0400, dbgfs_root, NULL,
					  &dbg_fops_state);
	if (!dbgfs_state)
		goto err_state;

	dbgfs_chan = kmalloc(sizeof(*dbgfs_state) * num_dma_channels,
			     GFP_KERNEL);
	if (!dbgfs_state)
		goto err_alloc;

	chandir = debugfs_create_dir("channels", dbgfs_root);
	if (!chandir)
		goto err_chandir;

	for (i = 0; i < num_dma_channels; i++) {
		dbgfs_chan[i] = pxa_dma_dbg_alloc_chan(i, chandir);
		if (!dbgfs_chan[i])
			goto err_chans;
	}

	return;
err_chans:
err_chandir:
	kfree(dbgfs_chan);
err_alloc:
err_state:
	debugfs_remove_recursive(dbgfs_root);
err_root:
	pr_err("pxa_dma: debugfs is not available\n");
}

static void __exit pxa_dma_cleanup_debugfs(void)
{
	debugfs_remove_recursive(dbgfs_root);
}
#else
static inline void pxa_dma_init_debugfs(void) {}
static inline void pxa_dma_cleanup_debugfs(void) {}
#endif

int pxa_request_dma (char *name, pxa_dma_prio prio,
			void (*irq_handler)(int, void *),
			void *data)
{
	unsigned long flags;
	int i, found = 0;

	/* basic sanity checks */
	if (!name || !irq_handler)
		return -EINVAL;

	local_irq_save(flags);

	do {
		/* try grabbing a DMA channel with the requested priority */
		for (i = 0; i < num_dma_channels; i++) {
			if ((dma_channels[i].prio == prio) &&
			    !dma_channels[i].name) {
				found = 1;
				break;
			}
		}
		/* if requested prio group is full, try a hier priority */
	} while (!found && prio--);

	if (found) {
		DCSR(i) = DCSR_STARTINTR|DCSR_ENDINTR|DCSR_BUSERR;
		dma_channels[i].name = name;
		dma_channels[i].irq_handler = irq_handler;
		dma_channels[i].data = data;
	} else {
		printk (KERN_WARNING "No more available DMA channels for %s\n", name);
		i = -ENODEV;
	}

	local_irq_restore(flags);
	return i;
}
EXPORT_SYMBOL(pxa_request_dma);

void pxa_free_dma (int dma_ch)
{
	unsigned long flags;

	if (!dma_channels[dma_ch].name) {
		printk (KERN_CRIT
			"%s: trying to free channel %d which is already freed\n",
			__func__, dma_ch);
		return;
	}

	local_irq_save(flags);
	DCSR(dma_ch) = DCSR_STARTINTR|DCSR_ENDINTR|DCSR_BUSERR;
	dma_channels[dma_ch].name = NULL;
	local_irq_restore(flags);
}
EXPORT_SYMBOL(pxa_free_dma);

static irqreturn_t dma_irq_handler(int irq, void *dev_id)
{
	int i, dint = DINT;
	struct dma_channel *channel;

	while (dint) {
		i = __ffs(dint);
		dint &= (dint - 1);
		channel = &dma_channels[i];
		if (channel->name && channel->irq_handler) {
			channel->irq_handler(i, channel->data);
		} else {
			/*
			 * IRQ for an unregistered DMA channel:
			 * let's clear the interrupts and disable it.
			 */
			printk (KERN_WARNING "spurious IRQ for DMA channel %d\n", i);
			DCSR(i) = DCSR_STARTINTR|DCSR_ENDINTR|DCSR_BUSERR;
		}
	}
	return IRQ_HANDLED;
}

int __init pxa_init_dma(int irq, int num_ch)
{
	int i, ret;

	dma_channels = kzalloc(sizeof(struct dma_channel) * num_ch, GFP_KERNEL);
	if (dma_channels == NULL)
		return -ENOMEM;

	/* dma channel priorities on pxa2xx processors:
	 * ch 0 - 3,  16 - 19  <--> (0) DMA_PRIO_HIGH
	 * ch 4 - 7,  20 - 23  <--> (1) DMA_PRIO_MEDIUM
	 * ch 8 - 15, 24 - 31  <--> (2) DMA_PRIO_LOW
	 */
	for (i = 0; i < num_ch; i++) {
		DCSR(i) = 0;
		dma_channels[i].prio = min((i & 0xf) >> 2, DMA_PRIO_LOW);
		spin_lock_init(&dma_channels[i].lock);
	}

	ret = request_irq(irq, dma_irq_handler, IRQF_DISABLED, "DMA", NULL);
	if (ret) {
		printk (KERN_CRIT "Wow!  Can't register IRQ for DMA\n");
		kfree(dma_channels);
		return ret;
	}
	num_dma_channels = num_ch;

	pxa_dma_init_debugfs();

	return 0;
}
