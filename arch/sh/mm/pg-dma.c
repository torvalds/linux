/*
 * arch/sh/mm/pg-dma.c
 *
 * Fast clear_page()/copy_page() implementation using the SH DMAC
 *
 * Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/semaphore.h>
#include <asm/mmu_context.h>
#include <asm/addrspace.h>
#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>

/* Channel to use for page ops, must be dual-address mode capable. */
static int dma_channel = CONFIG_DMA_PAGE_OPS_CHANNEL;

static void copy_page_dma(void *to, void *from)
{
	/* 
	 * This doesn't seem to get triggered until further along in the
	 * boot process, at which point the DMAC is already initialized.
	 * Fix this in the same fashion as clear_page_dma() in the event
	 * that this crashes due to the DMAC not being initialized.
	 */

	flush_icache_range((unsigned long)from, PAGE_SIZE);
	dma_write_page(dma_channel, (unsigned long)from, (unsigned long)to);
	dma_wait_for_completion(dma_channel);
}

static void clear_page_dma(void *to)
{
	extern unsigned long empty_zero_page[1024];

	/*
	 * We get invoked quite early on, if the DMAC hasn't been initialized
	 * yet, fall back on the slow manual implementation.
	 */
	if (dma_info[dma_channel].chan != dma_channel) {
		clear_page_slow(to);
		return;
	}

	dma_write_page(dma_channel, (unsigned long)empty_zero_page,
				    (unsigned long)to);

	/*
	 * FIXME: Something is a bit racy here, if we poll the counter right
	 * away, we seem to lock. flushing the page from the dcache doesn't
	 * seem to make a difference one way or the other, though either a full
	 * icache or dcache flush does.
	 *
	 * The location of this is important as well, and must happen prior to
	 * the completion loop but after the transfer was initiated.
	 *
	 * Oddly enough, this doesn't appear to be an issue for copy_page()..
	 */
	flush_icache_range((unsigned long)to, PAGE_SIZE);

	dma_wait_for_completion(dma_channel);
}

static int __init pg_dma_init(void)
{
	int ret;
	
	ret = request_dma(dma_channel, "page ops");
	if (ret != 0)
		return ret;

	copy_page = copy_page_dma;
	clear_page = clear_page_dma;

	return ret;
}

static void __exit pg_dma_exit(void)
{
	free_dma(dma_channel);
}

module_init(pg_dma_init);
module_exit(pg_dma_exit);

MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>");
MODULE_DESCRIPTION("Optimized page copy/clear routines using a dual-address mode capable DMAC channel");
MODULE_LICENSE("GPL");

