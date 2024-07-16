/*
 * reset.c  -- common ColdFire SoC reset support
 *
 * (C) Copyright 2012, Greg Ungerer <gerg@uclinux.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>

/*
 *	There are 2 common methods amongst the ColdFure parts for reseting
 *	the CPU. But there are couple of exceptions, the 5272 and the 547x
 *	have something completely special to them, and we let their specific
 *	subarch code handle them.
 */

#ifdef MCFSIM_SYPCR
static void mcf_cpu_reset(void)
{
	local_irq_disable();
	/* Set watchdog to soft reset, and enabled */
	__raw_writeb(0xc0, MCFSIM_SYPCR);
	for (;;)
		/* wait for watchdog to timeout */;
}
#endif

#ifdef MCF_RCR
static void mcf_cpu_reset(void)
{
	local_irq_disable();
	__raw_writeb(MCF_RCR_SWRESET, MCF_RCR);
}
#endif

static int __init mcf_setup_reset(void)
{
	mach_reset = mcf_cpu_reset;
	return 0;
}

arch_initcall(mcf_setup_reset);
