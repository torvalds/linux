/*
 * General Purpose functions for the global management of the
 * 8260 Communication Processor Module.
 * Copyright (c) 1999-2001 Dan Malek <dan@embeddedalley.com>
 * Copyright (c) 2000 MontaVista Software, Inc (source@mvista.com)
 *	2.3.99 Updates
 *
 * 2006 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 * 	Merged to arch/powerpc from arch/ppc/syslib/cpm2_common.c
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

/*
 *
 * In addition to the individual control of the communication
 * channels, there are a few functions that globally affect the
 * communication processor.
 *
 * Buffer descriptors must be allocated from the dual ported memory
 * space.  The allocator for that is here.  When the communication
 * process is reset, we reclaim the memory available.  There is
 * currently no deallocator for this memory.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mpc8260.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/cpm2.h>
#include <asm/rheap.h>
#include <asm/fs_pd.h>

#include <sysdev/fsl_soc.h>

static void cpm2_dpinit(void);
cpm_cpm2_t	*cpmp;		/* Pointer to comm processor space */

/* We allocate this here because it is used almost exclusively for
 * the communication processor devices.
 */
cpm2_map_t *cpm2_immr;
intctl_cpm2_t *cpm2_intctl;

#define CPM_MAP_SIZE	(0x40000)	/* 256k - the PQ3 reserve this amount
					   of space for CPM as it is larger
					   than on PQ2 */

void
cpm2_reset(void)
{
	cpm2_immr = (cpm2_map_t *)ioremap(CPM_MAP_ADDR, CPM_MAP_SIZE);
	cpm2_intctl = cpm2_map(im_intctl);

	/* Reclaim the DP memory for our use.
	 */
	cpm2_dpinit();

	/* Tell everyone where the comm processor resides.
	 */
	cpmp = &cpm2_immr->im_cpm;
}

/* Set a baud rate generator.  This needs lots of work.  There are
 * eight BRGs, which can be connected to the CPM channels or output
 * as clocks.  The BRGs are in two different block of internal
 * memory mapped space.
 * The baud rate clock is the system clock divided by something.
 * It was set up long ago during the initial boot phase and is
 * is given to us.
 * Baud rate clocks are zero-based in the driver code (as that maps
 * to port numbers).  Documentation uses 1-based numbering.
 */
#define BRG_INT_CLK	(get_brgfreq())
#define BRG_UART_CLK	(BRG_INT_CLK/16)

/* This function is used by UARTS, or anything else that uses a 16x
 * oversampled clock.
 */
void
cpm_setbrg(uint brg, uint rate)
{
	volatile uint	*bp;

	/* This is good enough to get SMCs running.....
	*/
	if (brg < 4) {
		bp = cpm2_map_size(im_brgc1, 16);
	} else {
		bp = cpm2_map_size(im_brgc5, 16);
		brg -= 4;
	}
	bp += brg;
	*bp = ((BRG_UART_CLK / rate) << 1) | CPM_BRG_EN;

	cpm2_unmap(bp);
}

/* This function is used to set high speed synchronous baud rate
 * clocks.
 */
void
cpm2_fastbrg(uint brg, uint rate, int div16)
{
	volatile uint	*bp;

	if (brg < 4) {
		bp = cpm2_map_size(im_brgc1, 16);
	}
	else {
		bp = cpm2_map_size(im_brgc5, 16);
		brg -= 4;
	}
	bp += brg;
	*bp = ((BRG_INT_CLK / rate) << 1) | CPM_BRG_EN;
	if (div16)
		*bp |= CPM_BRG_DIV16;

	cpm2_unmap(bp);
}

int cpm2_clk_setup(enum cpm_clk_target target, int clock, int mode)
{
	int ret = 0;
	int shift;
	int i, bits = 0;
	cpmux_t *im_cpmux;
	u32 *reg;
	u32 mask = 7;
	u8 clk_map [24][3] = {
		{CPM_CLK_FCC1, CPM_BRG5, 0},
		{CPM_CLK_FCC1, CPM_BRG6, 1},
		{CPM_CLK_FCC1, CPM_BRG7, 2},
		{CPM_CLK_FCC1, CPM_BRG8, 3},
		{CPM_CLK_FCC1, CPM_CLK9, 4},
		{CPM_CLK_FCC1, CPM_CLK10, 5},
		{CPM_CLK_FCC1, CPM_CLK11, 6},
		{CPM_CLK_FCC1, CPM_CLK12, 7},
		{CPM_CLK_FCC2, CPM_BRG5, 0},
		{CPM_CLK_FCC2, CPM_BRG6, 1},
		{CPM_CLK_FCC2, CPM_BRG7, 2},
		{CPM_CLK_FCC2, CPM_BRG8, 3},
		{CPM_CLK_FCC2, CPM_CLK13, 4},
		{CPM_CLK_FCC2, CPM_CLK14, 5},
		{CPM_CLK_FCC2, CPM_CLK15, 6},
		{CPM_CLK_FCC2, CPM_CLK16, 7},
		{CPM_CLK_FCC3, CPM_BRG5, 0},
		{CPM_CLK_FCC3, CPM_BRG6, 1},
		{CPM_CLK_FCC3, CPM_BRG7, 2},
		{CPM_CLK_FCC3, CPM_BRG8, 3},
		{CPM_CLK_FCC3, CPM_CLK13, 4},
		{CPM_CLK_FCC3, CPM_CLK14, 5},
		{CPM_CLK_FCC3, CPM_CLK15, 6},
		{CPM_CLK_FCC3, CPM_CLK16, 7}
		};

	im_cpmux = cpm2_map(im_cpmux);

	switch (target) {
	case CPM_CLK_SCC1:
		reg = &im_cpmux->cmx_scr;
		shift = 24;
	case CPM_CLK_SCC2:
		reg = &im_cpmux->cmx_scr;
		shift = 16;
		break;
	case CPM_CLK_SCC3:
		reg = &im_cpmux->cmx_scr;
		shift = 8;
		break;
	case CPM_CLK_SCC4:
		reg = &im_cpmux->cmx_scr;
		shift = 0;
		break;
	case CPM_CLK_FCC1:
		reg = &im_cpmux->cmx_fcr;
		shift = 24;
		break;
	case CPM_CLK_FCC2:
		reg = &im_cpmux->cmx_fcr;
		shift = 16;
		break;
	case CPM_CLK_FCC3:
		reg = &im_cpmux->cmx_fcr;
		shift = 8;
		break;
	default:
		printk(KERN_ERR "cpm2_clock_setup: invalid clock target\n");
		return -EINVAL;
	}

	if (mode == CPM_CLK_RX)
		shift +=3;

	for (i=0; i<24; i++) {
		if (clk_map[i][0] == target && clk_map[i][1] == clock) {
			bits = clk_map[i][2];
			break;
		}
	}
	if (i == sizeof(clk_map)/3)
	    ret = -EINVAL;

	bits <<= shift;
	mask <<= shift;
	out_be32(reg, (in_be32(reg) & ~mask) | bits);

	cpm2_unmap(im_cpmux);
	return ret;
}

/*
 * dpalloc / dpfree bits.
 */
static spinlock_t cpm_dpmem_lock;
/* 16 blocks should be enough to satisfy all requests
 * until the memory subsystem goes up... */
static rh_block_t cpm_boot_dpmem_rh_block[16];
static rh_info_t cpm_dpmem_info;
static u8* im_dprambase;

static void cpm2_dpinit(void)
{
	spin_lock_init(&cpm_dpmem_lock);

	im_dprambase = ioremap(CPM_MAP_ADDR, CPM_DATAONLY_BASE + CPM_DATAONLY_SIZE);

	/* initialize the info header */
	rh_init(&cpm_dpmem_info, 1,
			sizeof(cpm_boot_dpmem_rh_block) /
			sizeof(cpm_boot_dpmem_rh_block[0]),
			cpm_boot_dpmem_rh_block);

	/* Attach the usable dpmem area */
	/* XXX: This is actually crap. CPM_DATAONLY_BASE and
	 * CPM_DATAONLY_SIZE is only a subset of the available dpram. It
	 * varies with the processor and the microcode patches activated.
	 * But the following should be at least safe.
	 */
	rh_attach_region(&cpm_dpmem_info, (void *)CPM_DATAONLY_BASE,
			CPM_DATAONLY_SIZE);
}

/* This function returns an index into the DPRAM area.
 */
uint cpm_dpalloc(uint size, uint align)
{
	void *start;
	unsigned long flags;

	spin_lock_irqsave(&cpm_dpmem_lock, flags);
	cpm_dpmem_info.alignment = align;
	start = rh_alloc(&cpm_dpmem_info, size, "commproc");
	spin_unlock_irqrestore(&cpm_dpmem_lock, flags);

	return (uint)start;
}
EXPORT_SYMBOL(cpm_dpalloc);

int cpm_dpfree(uint offset)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&cpm_dpmem_lock, flags);
	ret = rh_free(&cpm_dpmem_info, (void *)offset);
	spin_unlock_irqrestore(&cpm_dpmem_lock, flags);

	return ret;
}
EXPORT_SYMBOL(cpm_dpfree);

/* not sure if this is ever needed */
uint cpm_dpalloc_fixed(uint offset, uint size, uint align)
{
	void *start;
	unsigned long flags;

	spin_lock_irqsave(&cpm_dpmem_lock, flags);
	cpm_dpmem_info.alignment = align;
	start = rh_alloc_fixed(&cpm_dpmem_info, (void *)offset, size, "commproc");
	spin_unlock_irqrestore(&cpm_dpmem_lock, flags);

	return (uint)start;
}
EXPORT_SYMBOL(cpm_dpalloc_fixed);

void cpm_dpdump(void)
{
	rh_dump(&cpm_dpmem_info);
}
EXPORT_SYMBOL(cpm_dpdump);

void *cpm_dpram_addr(uint offset)
{
	return (void *)(im_dprambase + offset);
}
EXPORT_SYMBOL(cpm_dpram_addr);
