/*
 * File:         arch/blackfin/mach-common/irqpanic.c
 * Based on:
 * Author:
 *
 * Created:      ?
 * Description:  panic kernel with dump information
 *
 * Modified:     rgetz - added cache checking code 14Feb06
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <asm/traps.h>
#include <asm/blackfin.h>

#include "../oprofile/op_blackfin.h"

#ifdef CONFIG_DEBUG_ICACHE_CHECK
#define L1_ICACHE_START 0xffa10000
#define L1_ICACHE_END   0xffa13fff
void irq_panic(int reason, struct pt_regs *regs) __attribute__ ((l1_text));
#endif

/*
 * irq_panic - calls panic with string setup
 */
asmlinkage void irq_panic(int reason, struct pt_regs *regs)
{
#ifdef CONFIG_DEBUG_ICACHE_CHECK
	unsigned int cmd, tag, ca, cache_hi, cache_lo, *pa;
	unsigned short i, j, die;
	unsigned int bad[10][6];

	/* check entire cache for coherency
	 * Since printk is in cacheable memory,
	 * don't call it until you have checked everything
	*/

	die = 0;
	i = 0;

	/* check icache */

	for (ca = L1_ICACHE_START; ca <= L1_ICACHE_END && i < 10; ca += 32) {

		/* Grab various address bits for the itest_cmd fields                      */
		cmd = (((ca & 0x3000) << 4) |	/* ca[13:12] for SBNK[1:0]             */
		       ((ca & 0x0c00) << 16) |	/* ca[11:10] for WAYSEL[1:0]           */
		       ((ca & 0x3f8)) |	/* ca[09:03] for SET[4:0] and DW[1:0]  */
		       0);	/* Access Tag, Read access             */

		SSYNC();
		bfin_write_ITEST_COMMAND(cmd);
		SSYNC();
		tag = bfin_read_ITEST_DATA0();
		SSYNC();

		/* if tag is marked as valid, check it */
		if (tag & 1) {
			/* The icache is arranged in 4 groups of 64-bits */
			for (j = 0; j < 32; j += 8) {
				cmd = ((((ca + j) & 0x3000) << 4) |	/* ca[13:12] for SBNK[1:0]             */
				       (((ca + j) & 0x0c00) << 16) |	/* ca[11:10] for WAYSEL[1:0]           */
				       (((ca + j) & 0x3f8)) |	/* ca[09:03] for SET[4:0] and DW[1:0]  */
				       4);	/* Access Data, Read access             */

				SSYNC();
				bfin_write_ITEST_COMMAND(cmd);
				SSYNC();

				cache_hi = bfin_read_ITEST_DATA1();
				cache_lo = bfin_read_ITEST_DATA0();

				pa = ((unsigned int *)((tag & 0xffffcc00) |
						       ((ca + j) & ~(0xffffcc00))));

				/*
				 * Debugging this, enable
				 *
				 * printk("addr: %08x %08x%08x | %08x%08x\n",
				 *  ((unsigned int *)((tag & 0xffffcc00)  | ((ca+j) & ~(0xffffcc00)))),
				 *   cache_hi, cache_lo, *(pa+1), *pa);
				 */

				if (cache_hi != *(pa + 1) || cache_lo != *pa) {
					/* Since icache is not working, stay out of it, by not printing */
					die = 1;
					bad[i][0] = (ca + j);
					bad[i][1] = cache_hi;
					bad[i][2] = cache_lo;
					bad[i][3] = ((tag & 0xffffcc00) |
					     	((ca + j) & ~(0xffffcc00)));
					bad[i][4] = *(pa + 1);
					bad[i][5] = *(pa);
					i++;
				}
			}
		}
	}
	if (die) {
		printk(KERN_EMERG "icache coherency error\n");
		for (j = 0; j <= i; j++) {
			printk(KERN_EMERG
			    "cache address   : %08x  cache value : %08x%08x\n",
			     bad[j][0], bad[j][1], bad[j][2]);
			printk(KERN_EMERG
			    "physical address: %08x  SDRAM value : %08x%08x\n",
			     bad[j][3], bad[j][4], bad[j][5]);
		}
		panic("icache coherency error");
	} else {
		printk(KERN_EMERG "icache checked, and OK\n");
	}
#endif

}

#ifdef CONFIG_HARDWARE_PM
/*
 * call the handler of Performance overflow
 */
asmlinkage void pm_overflow(int irq, struct pt_regs *regs)
{
	pm_overflow_handler(irq, regs);
}
#endif
