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
#include <linux/of.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mpc8260.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/cpm2.h>
#include <asm/rheap.h>
#include <asm/fs_pd.h>

#include <sysdev/fsl_soc.h>

cpm_cpm2_t __iomem *cpmp; /* Pointer to comm processor space */

/* We allocate this here because it is used almost exclusively for
 * the communication processor devices.
 */
cpm2_map_t __iomem *cpm2_immr;

#define CPM_MAP_SIZE	(0x40000)	/* 256k - the PQ3 reserve this amount
					   of space for CPM as it is larger
					   than on PQ2 */

void __init cpm2_reset(void)
{
#ifdef CONFIG_PPC_85xx
	cpm2_immr = ioremap(CPM_MAP_ADDR, CPM_MAP_SIZE);
#else
	cpm2_immr = ioremap(get_immrbase(), CPM_MAP_SIZE);
#endif

	/* Reclaim the DP memory for our use.
	 */
	cpm_muram_init();

	/* Tell everyone where the comm processor resides.
	 */
	cpmp = &cpm2_immr->im_cpm;

#ifndef CONFIG_PPC_EARLY_DEBUG_CPM
	/* Reset the CPM.
	 */
	cpm_command(CPM_CR_RST, 0);
#endif
}

static DEFINE_SPINLOCK(cmd_lock);

#define MAX_CR_CMD_LOOPS        10000

int cpm_command(u32 command, u8 opcode)
{
	int i, ret;
	unsigned long flags;

	spin_lock_irqsave(&cmd_lock, flags);

	ret = 0;
	out_be32(&cpmp->cp_cpcr, command | opcode | CPM_CR_FLG);
	for (i = 0; i < MAX_CR_CMD_LOOPS; i++)
		if ((in_be32(&cpmp->cp_cpcr) & CPM_CR_FLG) == 0)
			goto out;

	printk(KERN_ERR "%s(): Not able to issue CPM command\n", __func__);
	ret = -EIO;
out:
	spin_unlock_irqrestore(&cmd_lock, flags);
	return ret;
}
EXPORT_SYMBOL(cpm_command);

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
void __cpm2_setbrg(uint brg, uint rate, uint clk, int div16, int src)
{
	u32 __iomem *bp;
	u32 val;

	/* This is good enough to get SMCs running.....
	*/
	if (brg < 4) {
		bp = cpm2_map_size(im_brgc1, 16);
	} else {
		bp = cpm2_map_size(im_brgc5, 16);
		brg -= 4;
	}
	bp += brg;
	val = (((clk / rate) - 1) << 1) | CPM_BRG_EN | src;
	if (div16)
		val |= CPM_BRG_DIV16;

	out_be32(bp, val);
	cpm2_unmap(bp);
}
EXPORT_SYMBOL(__cpm2_setbrg);

int cpm2_clk_setup(enum cpm_clk_target target, int clock, int mode)
{
	int ret = 0;
	int shift;
	int i, bits = 0;
	cpmux_t __iomem *im_cpmux;
	u32 __iomem *reg;
	u32 mask = 7;

	u8 clk_map[][3] = {
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
		{CPM_CLK_FCC3, CPM_CLK16, 7},
		{CPM_CLK_SCC1, CPM_BRG1, 0},
		{CPM_CLK_SCC1, CPM_BRG2, 1},
		{CPM_CLK_SCC1, CPM_BRG3, 2},
		{CPM_CLK_SCC1, CPM_BRG4, 3},
		{CPM_CLK_SCC1, CPM_CLK11, 4},
		{CPM_CLK_SCC1, CPM_CLK12, 5},
		{CPM_CLK_SCC1, CPM_CLK3, 6},
		{CPM_CLK_SCC1, CPM_CLK4, 7},
		{CPM_CLK_SCC2, CPM_BRG1, 0},
		{CPM_CLK_SCC2, CPM_BRG2, 1},
		{CPM_CLK_SCC2, CPM_BRG3, 2},
		{CPM_CLK_SCC2, CPM_BRG4, 3},
		{CPM_CLK_SCC2, CPM_CLK11, 4},
		{CPM_CLK_SCC2, CPM_CLK12, 5},
		{CPM_CLK_SCC2, CPM_CLK3, 6},
		{CPM_CLK_SCC2, CPM_CLK4, 7},
		{CPM_CLK_SCC3, CPM_BRG1, 0},
		{CPM_CLK_SCC3, CPM_BRG2, 1},
		{CPM_CLK_SCC3, CPM_BRG3, 2},
		{CPM_CLK_SCC3, CPM_BRG4, 3},
		{CPM_CLK_SCC3, CPM_CLK5, 4},
		{CPM_CLK_SCC3, CPM_CLK6, 5},
		{CPM_CLK_SCC3, CPM_CLK7, 6},
		{CPM_CLK_SCC3, CPM_CLK8, 7},
		{CPM_CLK_SCC4, CPM_BRG1, 0},
		{CPM_CLK_SCC4, CPM_BRG2, 1},
		{CPM_CLK_SCC4, CPM_BRG3, 2},
		{CPM_CLK_SCC4, CPM_BRG4, 3},
		{CPM_CLK_SCC4, CPM_CLK5, 4},
		{CPM_CLK_SCC4, CPM_CLK6, 5},
		{CPM_CLK_SCC4, CPM_CLK7, 6},
		{CPM_CLK_SCC4, CPM_CLK8, 7},
	};

	im_cpmux = cpm2_map(im_cpmux);

	switch (target) {
	case CPM_CLK_SCC1:
		reg = &im_cpmux->cmx_scr;
		shift = 24;
		break;
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
		shift += 3;

	for (i = 0; i < ARRAY_SIZE(clk_map); i++) {
		if (clk_map[i][0] == target && clk_map[i][1] == clock) {
			bits = clk_map[i][2];
			break;
		}
	}
	if (i == ARRAY_SIZE(clk_map))
	    ret = -EINVAL;

	bits <<= shift;
	mask <<= shift;

	out_be32(reg, (in_be32(reg) & ~mask) | bits);

	cpm2_unmap(im_cpmux);
	return ret;
}

int cpm2_smc_clk_setup(enum cpm_clk_target target, int clock)
{
	int ret = 0;
	int shift;
	int i, bits = 0;
	cpmux_t __iomem *im_cpmux;
	u8 __iomem *reg;
	u8 mask = 3;

	u8 clk_map[][3] = {
		{CPM_CLK_SMC1, CPM_BRG1, 0},
		{CPM_CLK_SMC1, CPM_BRG7, 1},
		{CPM_CLK_SMC1, CPM_CLK7, 2},
		{CPM_CLK_SMC1, CPM_CLK9, 3},
		{CPM_CLK_SMC2, CPM_BRG2, 0},
		{CPM_CLK_SMC2, CPM_BRG8, 1},
		{CPM_CLK_SMC2, CPM_CLK4, 2},
		{CPM_CLK_SMC2, CPM_CLK15, 3},
	};

	im_cpmux = cpm2_map(im_cpmux);

	switch (target) {
	case CPM_CLK_SMC1:
		reg = &im_cpmux->cmx_smr;
		mask = 3;
		shift = 4;
		break;
	case CPM_CLK_SMC2:
		reg = &im_cpmux->cmx_smr;
		mask = 3;
		shift = 0;
		break;
	default:
		printk(KERN_ERR "cpm2_smc_clock_setup: invalid clock target\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(clk_map); i++) {
		if (clk_map[i][0] == target && clk_map[i][1] == clock) {
			bits = clk_map[i][2];
			break;
		}
	}
	if (i == ARRAY_SIZE(clk_map))
	    ret = -EINVAL;

	bits <<= shift;
	mask <<= shift;

	out_8(reg, (in_8(reg) & ~mask) | bits);

	cpm2_unmap(im_cpmux);
	return ret;
}

struct cpm2_ioports {
	u32 dir, par, sor, odr, dat;
	u32 res[3];
};

void cpm2_set_pin(int port, int pin, int flags)
{
	struct cpm2_ioports __iomem *iop =
		(struct cpm2_ioports __iomem *)&cpm2_immr->im_ioport;

	pin = 1 << (31 - pin);

	if (flags & CPM_PIN_OUTPUT)
		setbits32(&iop[port].dir, pin);
	else
		clrbits32(&iop[port].dir, pin);

	if (!(flags & CPM_PIN_GPIO))
		setbits32(&iop[port].par, pin);
	else
		clrbits32(&iop[port].par, pin);

	if (flags & CPM_PIN_SECONDARY)
		setbits32(&iop[port].sor, pin);
	else
		clrbits32(&iop[port].sor, pin);

	if (flags & CPM_PIN_OPENDRAIN)
		setbits32(&iop[port].odr, pin);
	else
		clrbits32(&iop[port].odr, pin);
}

static int cpm_init_par_io(void)
{
	struct device_node *np;

	for_each_compatible_node(np, NULL, "fsl,cpm2-pario-bank")
		cpm2_gpiochip_add32(np);
	return 0;
}
arch_initcall(cpm_init_par_io);

