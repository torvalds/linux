/*
 * bfin_dma_5xx.c - Blackfin DMA implementation
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <asm/blackfin.h>
#include <asm/cacheflush.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
#include <asm/early_printk.h>

/*
 * To make sure we work around 05000119 - we always check DMA_DONE bit,
 * never the DMA_RUN bit
 */

struct dma_channel dma_ch[MAX_DMA_CHANNELS];
EXPORT_SYMBOL(dma_ch);

static int __init blackfin_dma_init(void)
{
	int i;

	printk(KERN_INFO "Blackfin DMA Controller\n");

	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		atomic_set(&dma_ch[i].chan_status, 0);
		dma_ch[i].regs = dma_io_base_addr[i];
	}
	/* Mark MEMDMA Channel 0 as requested since we're using it internally */
	request_dma(CH_MEM_STREAM0_DEST, "Blackfin dma_memcpy");
	request_dma(CH_MEM_STREAM0_SRC, "Blackfin dma_memcpy");

#if defined(CONFIG_DEB_DMA_URGENT)
	bfin_write_EBIU_DDRQUE(bfin_read_EBIU_DDRQUE()
			 | DEB1_URGENT | DEB2_URGENT | DEB3_URGENT);
#endif

	return 0;
}
arch_initcall(blackfin_dma_init);

#ifdef CONFIG_PROC_FS
static int proc_dma_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; i < MAX_DMA_CHANNELS; ++i)
		if (dma_channel_active(i))
			seq_printf(m, "%2d: %s\n", i, dma_ch[i].device_id);

	return 0;
}

static int proc_dma_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_dma_show, NULL);
}

static const struct file_operations proc_dma_operations = {
	.open		= proc_dma_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_dma_init(void)
{
	return proc_create("dma", 0, NULL, &proc_dma_operations) != NULL;
}
late_initcall(proc_dma_init);
#endif

/**
 *	request_dma - request a DMA channel
 *
 * Request the specific DMA channel from the system if it's available.
 */
int request_dma(unsigned int channel, const char *device_id)
{
	pr_debug("request_dma() : BEGIN\n");

	if (device_id == NULL)
		printk(KERN_WARNING "request_dma(%u): no device_id given\n", channel);

#if defined(CONFIG_BF561) && ANOMALY_05000182
	if (channel >= CH_IMEM_STREAM0_DEST && channel <= CH_IMEM_STREAM1_DEST) {
		if (get_cclk() > 500000000) {
			printk(KERN_WARNING
			       "Request IMDMA failed due to ANOMALY 05000182\n");
			return -EFAULT;
		}
	}
#endif

	if (atomic_cmpxchg(&dma_ch[channel].chan_status, 0, 1)) {
		pr_debug("DMA CHANNEL IN USE\n");
		return -EBUSY;
	}

#ifdef CONFIG_BF54x
	if (channel >= CH_UART2_RX && channel <= CH_UART3_TX) {
		unsigned int per_map;
		per_map = dma_ch[channel].regs->peripheral_map & 0xFFF;
		if (strncmp(device_id, "BFIN_UART", 9) == 0)
			dma_ch[channel].regs->peripheral_map = per_map |
				((channel - CH_UART2_RX + 0xC)<<12);
		else
			dma_ch[channel].regs->peripheral_map = per_map |
				((channel - CH_UART2_RX + 0x6)<<12);
	}
#endif

	dma_ch[channel].device_id = device_id;
	dma_ch[channel].irq = 0;

	/* This is to be enabled by putting a restriction -
	 * you have to request DMA, before doing any operations on
	 * descriptor/channel
	 */
	pr_debug("request_dma() : END\n");
	return 0;
}
EXPORT_SYMBOL(request_dma);

int set_dma_callback(unsigned int channel, irq_handler_t callback, void *data)
{
	int ret;
	unsigned int irq;

	BUG_ON(channel >= MAX_DMA_CHANNELS || !callback ||
			!atomic_read(&dma_ch[channel].chan_status));

	irq = channel2irq(channel);
	ret = request_irq(irq, callback, 0, dma_ch[channel].device_id, data);
	if (ret)
		return ret;

	dma_ch[channel].irq = irq;
	dma_ch[channel].data = data;

	return 0;
}
EXPORT_SYMBOL(set_dma_callback);

/**
 *	clear_dma_buffer - clear DMA fifos for specified channel
 *
 * Set the Buffer Clear bit in the Configuration register of specific DMA
 * channel. This will stop the descriptor based DMA operation.
 */
static void clear_dma_buffer(unsigned int channel)
{
	dma_ch[channel].regs->cfg |= RESTART;
	SSYNC();
	dma_ch[channel].regs->cfg &= ~RESTART;
}

void free_dma(unsigned int channel)
{
	pr_debug("freedma() : BEGIN\n");
	BUG_ON(channel >= MAX_DMA_CHANNELS ||
			!atomic_read(&dma_ch[channel].chan_status));

	/* Halt the DMA */
	disable_dma(channel);
	clear_dma_buffer(channel);

	if (dma_ch[channel].irq)
		free_irq(dma_ch[channel].irq, dma_ch[channel].data);

	/* Clear the DMA Variable in the Channel */
	atomic_set(&dma_ch[channel].chan_status, 0);

	pr_debug("freedma() : END\n");
}
EXPORT_SYMBOL(free_dma);

#ifdef CONFIG_PM
# ifndef MAX_DMA_SUSPEND_CHANNELS
#  define MAX_DMA_SUSPEND_CHANNELS MAX_DMA_CHANNELS
# endif
int blackfin_dma_suspend(void)
{
	int i;

	for (i = 0; i < MAX_DMA_CHANNELS; ++i) {
		if (dma_ch[i].regs->cfg & DMAEN) {
			printk(KERN_ERR "DMA Channel %d failed to suspend\n", i);
			return -EBUSY;
		}

		if (i < MAX_DMA_SUSPEND_CHANNELS)
			dma_ch[i].saved_peripheral_map = dma_ch[i].regs->peripheral_map;
	}

	return 0;
}

void blackfin_dma_resume(void)
{
	int i;

	for (i = 0; i < MAX_DMA_CHANNELS; ++i) {
		dma_ch[i].regs->cfg = 0;

		if (i < MAX_DMA_SUSPEND_CHANNELS)
			dma_ch[i].regs->peripheral_map = dma_ch[i].saved_peripheral_map;
	}
}
#endif

/**
 *	blackfin_dma_early_init - minimal DMA init
 *
 * Setup a few DMA registers so we can safely do DMA transfers early on in
 * the kernel booting process.  Really this just means using dma_memcpy().
 */
void __init blackfin_dma_early_init(void)
{
	early_shadow_stamp();
	bfin_write_MDMA_S0_CONFIG(0);
	bfin_write_MDMA_S1_CONFIG(0);
}

void __init early_dma_memcpy(void *pdst, const void *psrc, size_t size)
{
	unsigned long dst = (unsigned long)pdst;
	unsigned long src = (unsigned long)psrc;
	struct dma_register *dst_ch, *src_ch;

	early_shadow_stamp();

	/* We assume that everything is 4 byte aligned, so include
	 * a basic sanity check
	 */
	BUG_ON(dst % 4);
	BUG_ON(src % 4);
	BUG_ON(size % 4);

	src_ch = 0;
	/* Find an avalible memDMA channel */
	while (1) {
		if (src_ch == (struct dma_register *)MDMA_S0_NEXT_DESC_PTR) {
			dst_ch = (struct dma_register *)MDMA_D1_NEXT_DESC_PTR;
			src_ch = (struct dma_register *)MDMA_S1_NEXT_DESC_PTR;
		} else {
			dst_ch = (struct dma_register *)MDMA_D0_NEXT_DESC_PTR;
			src_ch = (struct dma_register *)MDMA_S0_NEXT_DESC_PTR;
		}

		if (!bfin_read16(&src_ch->cfg))
			break;
		else if (bfin_read16(&dst_ch->irq_status) & DMA_DONE) {
			bfin_write16(&src_ch->cfg, 0);
			break;
		}
	}

	/* Force a sync in case a previous config reset on this channel
	 * occurred.  This is needed so subsequent writes to DMA registers
	 * are not spuriously lost/corrupted.
	 */
	__builtin_bfin_ssync();

	/* Destination */
	bfin_write32(&dst_ch->start_addr, dst);
	bfin_write16(&dst_ch->x_count, size >> 2);
	bfin_write16(&dst_ch->x_modify, 1 << 2);
	bfin_write16(&dst_ch->irq_status, DMA_DONE | DMA_ERR);

	/* Source */
	bfin_write32(&src_ch->start_addr, src);
	bfin_write16(&src_ch->x_count, size >> 2);
	bfin_write16(&src_ch->x_modify, 1 << 2);
	bfin_write16(&src_ch->irq_status, DMA_DONE | DMA_ERR);

	/* Enable */
	bfin_write16(&src_ch->cfg, DMAEN | WDSIZE_32);
	bfin_write16(&dst_ch->cfg, WNR | DI_EN | DMAEN | WDSIZE_32);

	/* Since we are atomic now, don't use the workaround ssync */
	__builtin_bfin_ssync();
}

void __init early_dma_memcpy_done(void)
{
	early_shadow_stamp();

	while ((bfin_read_MDMA_S0_CONFIG() && !(bfin_read_MDMA_D0_IRQ_STATUS() & DMA_DONE)) ||
	       (bfin_read_MDMA_S1_CONFIG() && !(bfin_read_MDMA_D1_IRQ_STATUS() & DMA_DONE)))
		continue;

	bfin_write_MDMA_D0_IRQ_STATUS(DMA_DONE | DMA_ERR);
	bfin_write_MDMA_D1_IRQ_STATUS(DMA_DONE | DMA_ERR);
	/*
	 * Now that DMA is done, we would normally flush cache, but
	 * i/d cache isn't running this early, so we don't bother,
	 * and just clear out the DMA channel for next time
	 */
	bfin_write_MDMA_S0_CONFIG(0);
	bfin_write_MDMA_S1_CONFIG(0);
	bfin_write_MDMA_D0_CONFIG(0);
	bfin_write_MDMA_D1_CONFIG(0);

	__builtin_bfin_ssync();
}

/**
 *	__dma_memcpy - program the MDMA registers
 *
 * Actually program MDMA0 and wait for the transfer to finish.  Disable IRQs
 * while programming registers so that everything is fully configured.  Wait
 * for DMA to finish with IRQs enabled.  If interrupted, the initial DMA_DONE
 * check will make sure we don't clobber any existing transfer.
 */
static void __dma_memcpy(u32 daddr, s16 dmod, u32 saddr, s16 smod, size_t cnt, u32 conf)
{
	static DEFINE_SPINLOCK(mdma_lock);
	unsigned long flags;

	spin_lock_irqsave(&mdma_lock, flags);

	/* Force a sync in case a previous config reset on this channel
	 * occurred.  This is needed so subsequent writes to DMA registers
	 * are not spuriously lost/corrupted.  Do it under irq lock and
	 * without the anomaly version (because we are atomic already).
	 */
	__builtin_bfin_ssync();

	if (bfin_read_MDMA_S0_CONFIG())
		while (!(bfin_read_MDMA_D0_IRQ_STATUS() & DMA_DONE))
			continue;

	if (conf & DMA2D) {
		/* For larger bit sizes, we've already divided down cnt so it
		 * is no longer a multiple of 64k.  So we have to break down
		 * the limit here so it is a multiple of the incoming size.
		 * There is no limitation here in terms of total size other
		 * than the hardware though as the bits lost in the shift are
		 * made up by MODIFY (== we can hit the whole address space).
		 * X: (2^(16 - 0)) * 1 == (2^(16 - 1)) * 2 == (2^(16 - 2)) * 4
		 */
		u32 shift = abs(dmod) >> 1;
		size_t ycnt = cnt >> (16 - shift);
		cnt = 1 << (16 - shift);
		bfin_write_MDMA_D0_Y_COUNT(ycnt);
		bfin_write_MDMA_S0_Y_COUNT(ycnt);
		bfin_write_MDMA_D0_Y_MODIFY(dmod);
		bfin_write_MDMA_S0_Y_MODIFY(smod);
	}

	bfin_write_MDMA_D0_START_ADDR(daddr);
	bfin_write_MDMA_D0_X_COUNT(cnt);
	bfin_write_MDMA_D0_X_MODIFY(dmod);
	bfin_write_MDMA_D0_IRQ_STATUS(DMA_DONE | DMA_ERR);

	bfin_write_MDMA_S0_START_ADDR(saddr);
	bfin_write_MDMA_S0_X_COUNT(cnt);
	bfin_write_MDMA_S0_X_MODIFY(smod);
	bfin_write_MDMA_S0_IRQ_STATUS(DMA_DONE | DMA_ERR);

	bfin_write_MDMA_S0_CONFIG(DMAEN | conf);
	bfin_write_MDMA_D0_CONFIG(WNR | DI_EN | DMAEN | conf);

	spin_unlock_irqrestore(&mdma_lock, flags);

	SSYNC();

	while (!(bfin_read_MDMA_D0_IRQ_STATUS() & DMA_DONE))
		if (bfin_read_MDMA_S0_CONFIG())
			continue;
		else
			return;

	bfin_write_MDMA_D0_IRQ_STATUS(DMA_DONE | DMA_ERR);

	bfin_write_MDMA_S0_CONFIG(0);
	bfin_write_MDMA_D0_CONFIG(0);
}

/**
 *	_dma_memcpy - translate C memcpy settings into MDMA settings
 *
 * Handle all the high level steps before we touch the MDMA registers.  So
 * handle direction, tweaking of sizes, and formatting of addresses.
 */
static void *_dma_memcpy(void *pdst, const void *psrc, size_t size)
{
	u32 conf, shift;
	s16 mod;
	unsigned long dst = (unsigned long)pdst;
	unsigned long src = (unsigned long)psrc;

	if (size == 0)
		return NULL;

	if (dst % 4 == 0 && src % 4 == 0 && size % 4 == 0) {
		conf = WDSIZE_32;
		shift = 2;
	} else if (dst % 2 == 0 && src % 2 == 0 && size % 2 == 0) {
		conf = WDSIZE_16;
		shift = 1;
	} else {
		conf = WDSIZE_8;
		shift = 0;
	}

	/* If the two memory regions have a chance of overlapping, make
	 * sure the memcpy still works as expected.  Do this by having the
	 * copy run backwards instead.
	 */
	mod = 1 << shift;
	if (src < dst) {
		mod *= -1;
		dst += size + mod;
		src += size + mod;
	}
	size >>= shift;

	if (size > 0x10000)
		conf |= DMA2D;

	__dma_memcpy(dst, mod, src, mod, size, conf);

	return pdst;
}

/**
 *	dma_memcpy - DMA memcpy under mutex lock
 *
 * Do not check arguments before starting the DMA memcpy.  Break the transfer
 * up into two pieces.  The first transfer is in multiples of 64k and the
 * second transfer is the piece smaller than 64k.
 */
void *dma_memcpy(void *pdst, const void *psrc, size_t size)
{
	unsigned long dst = (unsigned long)pdst;
	unsigned long src = (unsigned long)psrc;
	size_t bulk, rest;

	if (bfin_addr_dcacheable(src))
		blackfin_dcache_flush_range(src, src + size);

	if (bfin_addr_dcacheable(dst))
		blackfin_dcache_invalidate_range(dst, dst + size);

	bulk = size & ~0xffff;
	rest = size - bulk;
	if (bulk)
		_dma_memcpy(pdst, psrc, bulk);
	_dma_memcpy(pdst + bulk, psrc + bulk, rest);
	return pdst;
}
EXPORT_SYMBOL(dma_memcpy);

/**
 *	safe_dma_memcpy - DMA memcpy w/argument checking
 *
 * Verify arguments are safe before heading to dma_memcpy().
 */
void *safe_dma_memcpy(void *dst, const void *src, size_t size)
{
	if (!access_ok(VERIFY_WRITE, dst, size))
		return NULL;
	if (!access_ok(VERIFY_READ, src, size))
		return NULL;
	return dma_memcpy(dst, src, size);
}
EXPORT_SYMBOL(safe_dma_memcpy);

static void _dma_out(unsigned long addr, unsigned long buf, unsigned short len,
                     u16 size, u16 dma_size)
{
	blackfin_dcache_flush_range(buf, buf + len * size);
	__dma_memcpy(addr, 0, buf, size, len, dma_size);
}

static void _dma_in(unsigned long addr, unsigned long buf, unsigned short len,
                    u16 size, u16 dma_size)
{
	blackfin_dcache_invalidate_range(buf, buf + len * size);
	__dma_memcpy(buf, size, addr, 0, len, dma_size);
}

#define MAKE_DMA_IO(io, bwl, isize, dmasize, cnst) \
void dma_##io##s##bwl(unsigned long addr, cnst void *buf, unsigned short len) \
{ \
	_dma_##io(addr, (unsigned long)buf, len, isize, WDSIZE_##dmasize); \
} \
EXPORT_SYMBOL(dma_##io##s##bwl)
MAKE_DMA_IO(out, b, 1,  8, const);
MAKE_DMA_IO(in,  b, 1,  8, );
MAKE_DMA_IO(out, w, 2, 16, const);
MAKE_DMA_IO(in,  w, 2, 16, );
MAKE_DMA_IO(out, l, 4, 32, const);
MAKE_DMA_IO(in,  l, 4, 32, );
