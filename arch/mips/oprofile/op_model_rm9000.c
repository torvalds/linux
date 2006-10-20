/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 by Ralf Baechle
 */
#include <linux/init.h>
#include <linux/oprofile.h>
#include <linux/interrupt.h>
#include <linux/smp.h>

#include "op_impl.h"

#define RM9K_COUNTER1_EVENT(event)	((event) << 0)
#define RM9K_COUNTER1_SUPERVISOR	(1ULL    <<  7)
#define RM9K_COUNTER1_KERNEL		(1ULL    <<  8)
#define RM9K_COUNTER1_USER		(1ULL    <<  9)
#define RM9K_COUNTER1_ENABLE		(1ULL    << 10)
#define RM9K_COUNTER1_OVERFLOW		(1ULL    << 15)

#define RM9K_COUNTER2_EVENT(event)	((event) << 16)
#define RM9K_COUNTER2_SUPERVISOR	(1ULL    << 23)
#define RM9K_COUNTER2_KERNEL		(1ULL    << 24)
#define RM9K_COUNTER2_USER		(1ULL    << 25)
#define RM9K_COUNTER2_ENABLE		(1ULL    << 26)
#define RM9K_COUNTER2_OVERFLOW		(1ULL    << 31)

extern unsigned int rm9000_perfcount_irq;

static struct rm9k_register_config {
	unsigned int control;
	unsigned int reset_counter1;
	unsigned int reset_counter2;
} reg;

/* Compute all of the registers in preparation for enabling profiling.  */

static void rm9000_reg_setup(struct op_counter_config *ctr)
{
	unsigned int control = 0;

	/* Compute the performance counter control word.  */
	/* For now count kernel and user mode */
	if (ctr[0].enabled)
		control |= RM9K_COUNTER1_EVENT(ctr[0].event) |
		           RM9K_COUNTER1_KERNEL |
		           RM9K_COUNTER1_USER |
		           RM9K_COUNTER1_ENABLE;
	if (ctr[1].enabled)
		control |= RM9K_COUNTER2_EVENT(ctr[1].event) |
		           RM9K_COUNTER2_KERNEL |
		           RM9K_COUNTER2_USER |
		           RM9K_COUNTER2_ENABLE;
	reg.control = control;

	reg.reset_counter1 = 0x80000000 - ctr[0].count;
	reg.reset_counter2 = 0x80000000 - ctr[1].count;
}

/* Program all of the registers in preparation for enabling profiling.  */

static void rm9000_cpu_setup (void *args)
{
	uint64_t perfcount;

	perfcount = ((uint64_t) reg.reset_counter2 << 32) | reg.reset_counter1;
	write_c0_perfcount(perfcount);
}

static void rm9000_cpu_start(void *args)
{
	/* Start all counters on current CPU */
	write_c0_perfcontrol(reg.control);
}

static void rm9000_cpu_stop(void *args)
{
	/* Stop all counters on current CPU */
	write_c0_perfcontrol(0);
}

static irqreturn_t rm9000_perfcount_handler(int irq, void * dev_id)
{
	unsigned int control = read_c0_perfcontrol();
	uint32_t counter1, counter2;
	uint64_t counters;

	/*
	 * RM9000 combines two 32-bit performance counters into a single
	 * 64-bit coprocessor zero register.  To avoid a race updating the
	 * registers we need to stop the counters while we're messing with
	 * them ...
	 */
	write_c0_perfcontrol(0);

	counters = read_c0_perfcount();
	counter1 = counters;
	counter2 = counters >> 32;

	if (control & RM9K_COUNTER1_OVERFLOW) {
		oprofile_add_sample(regs, 0);
		counter1 = reg.reset_counter1;
	}
	if (control & RM9K_COUNTER2_OVERFLOW) {
		oprofile_add_sample(regs, 1);
		counter2 = reg.reset_counter2;
	}

	counters = ((uint64_t)counter2 << 32) | counter1;
	write_c0_perfcount(counters);
	write_c0_perfcontrol(reg.control);

	return IRQ_HANDLED;
}

static int __init rm9000_init(void)
{
	return request_irq(rm9000_perfcount_irq, rm9000_perfcount_handler,
	                   0, "Perfcounter", NULL);
}

static void rm9000_exit(void)
{
	free_irq(rm9000_perfcount_irq, NULL);
}

struct op_mips_model op_model_rm9000_ops = {
	.reg_setup	= rm9000_reg_setup,
	.cpu_setup	= rm9000_cpu_setup,
	.init		= rm9000_init,
	.exit		= rm9000_exit,
	.cpu_start	= rm9000_cpu_start,
	.cpu_stop	= rm9000_cpu_stop,
	.cpu_type	= "mips/rm9000",
	.num_counters	= 2
};
