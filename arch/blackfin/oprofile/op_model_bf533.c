/*
 * File:         arch/blackfin/oprofile/op_model_bf533.c
 * Based on:
 * Author:       Anton Blanchard <anton@au.ibm.com>
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
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

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/blackfin.h>
#include <asm/irq.h>
#include <asm/io.h>

#include "op_blackfin.h"

#define PM_ENABLE 0x01;
#define PM_CTL1_ENABLE  0x18
#define PM_CTL0_ENABLE  0xC000
#define COUNT_EDGE_ONLY 0x3000000

static int oprofile_running;

static unsigned curr_pfctl, curr_count[2];

static int bfin533_reg_setup(struct op_counter_config *ctr)
{
	unsigned int pfctl = ctr_read();
	unsigned int count[2];

	/* set Blackfin perf monitor regs with ctr */
	if (ctr[0].enabled) {
		pfctl |= (PM_CTL0_ENABLE | ((char)ctr[0].event << 5));
		count[0] = 0xFFFFFFFF - ctr[0].count;
		curr_count[0] = count[0];
	}
	if (ctr[1].enabled) {
		pfctl |= (PM_CTL1_ENABLE | ((char)ctr[1].event << 16));
		count[1] = 0xFFFFFFFF - ctr[1].count;
		curr_count[1] = count[1];
	}

	pr_debug("ctr[0].enabled=%d,ctr[1].enabled=%d,ctr[0].event<<5=0x%x,ctr[1].event<<16=0x%x\n", ctr[0].enabled, ctr[1].enabled, ctr[0].event << 5, ctr[1].event << 16);
	pfctl |= COUNT_EDGE_ONLY;
	curr_pfctl = pfctl;

	pr_debug("write 0x%x to pfctl\n", pfctl);
	ctr_write(pfctl);
	count_write(count);

	return 0;
}

static int bfin533_start(struct op_counter_config *ctr)
{
	unsigned int pfctl = ctr_read();

	pfctl |= PM_ENABLE;
	curr_pfctl = pfctl;

	ctr_write(pfctl);

	oprofile_running = 1;
	pr_debug("start oprofile counter \n");

	return 0;
}

static void bfin533_stop(void)
{
	int pfctl;

	pfctl = ctr_read();
	pfctl &= ~PM_ENABLE;
	/* freeze counters */
	ctr_write(pfctl);

	oprofile_running = 0;
	pr_debug("stop oprofile counter \n");
}

static int get_kernel(void)
{
	int ipend, is_kernel;

	ipend = bfin_read_IPEND();

	/* test bit 15 */
	is_kernel = ((ipend & 0x8000) != 0);

	return is_kernel;
}

int pm_overflow_handler(int irq, struct pt_regs *regs)
{
	int is_kernel;
	int i, cpu;
	unsigned int pc, pfctl;
	unsigned int count[2];

	pr_debug("get interrupt in %s\n", __FUNCTION__);
	if (oprofile_running == 0) {
		pr_debug("error: entering interrupt when oprofile is stopped.\n\r");
		return -1;
	}

	is_kernel = get_kernel();
	cpu = smp_processor_id();
	pc = regs->pc;
	pfctl = ctr_read();

	/* read the two event counter regs */
	count_read(count);

	/* if the counter overflows, add sample to oprofile buffer */
	for (i = 0; i < 2; ++i) {
		if (oprofile_running) {
			oprofile_add_sample(regs, i);
		}
	}

	/* reset the perfmon counter */
	ctr_write(curr_pfctl);
	count_write(curr_count);
	return 0;
}

struct op_bfin533_model op_model_bfin533 = {
	.reg_setup = bfin533_reg_setup,
	.start = bfin533_start,
	.stop = bfin533_stop,
	.num_counters = 2,
	.name = "blackfin/bf533"
};
