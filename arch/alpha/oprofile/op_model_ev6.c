/**
 * @file arch/alpha/oprofile/op_model_ev6.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Richard Henderson <rth@twiddle.net>
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <asm/ptrace.h>
#include <asm/system.h>

#include "op_impl.h"


/* Compute all of the registers in preparation for enabling profiling.  */

static void
ev6_reg_setup(struct op_register_config *reg,
	      struct op_counter_config *ctr,
	      struct op_system_config *sys)
{
	unsigned long ctl, reset, need_reset, i;

	/* Select desired events.  We've mapped the event numbers
	   such that they fit directly into the event selection fields.  */
	ctl = 0;
	if (ctr[0].enabled && ctr[0].event)
		ctl |= (ctr[0].event & 1) << 4;
	if (ctr[1].enabled)
		ctl |= (ctr[1].event - 2) & 15;
	reg->mux_select = ctl;

	/* Select logging options.  */
	/* ??? Need to come up with some mechanism to trace only
	   selected processes.  EV6 does not have a mechanism to
	   select kernel or user mode only.  For now, enable always.  */
	reg->proc_mode = 0;

	/* EV6 cannot change the width of the counters as with the
	   other implementations.  But fortunately, we can write to
	   the counters and set the value such that it will overflow
	   at the right time.  */
	reset = need_reset = 0;
	for (i = 0; i < 2; ++i) {
		unsigned long count = ctr[i].count;
		if (!ctr[i].enabled)
			continue;

		if (count > 0x100000)
			count = 0x100000;
		ctr[i].count = count;
		reset |= (0x100000 - count) << (i ? 6 : 28);
		if (count != 0x100000)
			need_reset |= 1 << i;
	}
	reg->reset_values = reset;
	reg->need_reset = need_reset;
}

/* Program all of the registers in preparation for enabling profiling.  */

static void
ev6_cpu_setup (void *x)
{
	struct op_register_config *reg = x;

	wrperfmon(2, reg->mux_select);
	wrperfmon(3, reg->proc_mode);
	wrperfmon(6, reg->reset_values | 3);
}

/* CTR is a counter for which the user has requested an interrupt count
   in between one of the widths selectable in hardware.  Reset the count
   for CTR to the value stored in REG->RESET_VALUES.  */

static void
ev6_reset_ctr(struct op_register_config *reg, unsigned long ctr)
{
	wrperfmon(6, reg->reset_values | (1 << ctr));
}

static void
ev6_handle_interrupt(unsigned long which, struct pt_regs *regs,
		     struct op_counter_config *ctr)
{
	/* Record the sample.  */
	oprofile_add_sample(regs, which);
}


struct op_axp_model op_model_ev6 = {
	.reg_setup		= ev6_reg_setup,
	.cpu_setup		= ev6_cpu_setup,
	.reset_ctr		= ev6_reset_ctr,
	.handle_interrupt	= ev6_handle_interrupt,
	.cpu_type		= "alpha/ev6",
	.num_counters		= 2,
	.can_set_proc_mode	= 0,
};
