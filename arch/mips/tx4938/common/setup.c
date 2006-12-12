/*
 * linux/arch/mips/tx4938/common/setup.c
 *
 * common tx4938 setup routines
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/irq.h>
#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/time.h>
#include <asm/tx4938/rbtx4938.h>

extern void toshiba_rbtx4938_setup(void);
extern void rbtx4938_time_init(void);

void __init tx4938_setup(void);
void __init tx4938_time_init(void);
void dump_cp0(char *key);

void __init
plat_mem_setup(void)
{
	board_time_init = tx4938_time_init;
	toshiba_rbtx4938_setup();
}

void __init
tx4938_time_init(void)
{
	rbtx4938_time_init();
}

void __init plat_timer_setup(struct irqaction *irq)
{
	u32 count;
	u32 c1;
	u32 c2;

	setup_irq(TX4938_IRQ_CPU_TIMER, irq);

	c1 = read_c0_count();
	count = c1 + (mips_hpt_frequency / HZ);
	write_c0_compare(count);
	c2 = read_c0_count();
}
