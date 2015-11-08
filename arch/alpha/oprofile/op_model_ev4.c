/**
 * @file arch/alpha/oprofile/op_model_ev4.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Richard Henderson <rth@twiddle.net>
 */

#include <linux/oprofile.h>
#include <linux/smp.h>
#include <asm/ptrace.h>

#include "op_impl.h"


/* Compute all of the registers in preparation for enabling profiling.  */

static void
ev4_reg_setup(struct op_register_config *reg,
	      struct op_counter_config *ctr,
	      struct op_system_config *sys)
{
	unsigned long ctl = 0, count, hilo;

	/* Select desired events.  We've mapped the event numbers
	   such that they fit directly into the event selection fields.

	   Note that there is no "off" setting.  In both cases we select
	   the EXTERNAL event source, hoping that it'll be the lowest
	   frequency, and set the frequency counter to LOW.  The interrupts
	   for these "disabled" counter overflows are ignored by the
	   interrupt handler.

	   This is most irritating, because the hardware *can* enable and
	   disable the interrupts for these counters independently, but the
	   wrperfmon interface doesn't allow it.  */

	ctl |= (ctr[0].enabled ? ctr[0].event << 8 : 14 << 8);
	ctl |= (ctr[1].enabled ? (ctr[1].event - 16) << 32 : 7ul << 32);

	/* EV4 can not read or write its counter registers.  The only
	   thing one can do at all is see if you overflow and get an
	   interrupt.  We can set the width of the counters, to some
	   extent.  Take the interrupt count selected by the user,
	   map it onto one of the possible values, and write it back.  */

	count = ctr[0].count;
	if (count <= 4096)
		count = 4096, hilo = 1;
	else
		count = 65536, hilo = 0;
	ctr[0].count = count;
	ctl |= (ctr[0].enabled && hilo) << 3;

	count = ctr[1].count;
	if (count <= 256)
		count = 256, hilo = 1;
	else
		count = 4096, hilo = 0;
	ctr[1].count = count;
	ctl |= (ctr[1].enabled && hilo);

	reg->mux_select = ctl;

	/* Select performance monitoring options.  */
	/* ??? Need to come up with some mechanism to trace only
	   selected processes.  EV4 does not have a mechanism to
	   select kernel or user mode only.  For now, enable always.  */
	reg->proc_mode = 0;

	/* Frequency is folded into mux_select for EV4.  */
	reg->freq = 0;

	/* See above regarding no writes.  */
	reg->reset_values = 0;
	reg->need_reset = 0;

}

/* Program all of the registers in preparation for enabling profiling.  */

static void
ev4_cpu_setup(void *x)
{
	struct op_register_config *reg = x;

	wrperfmon(2, reg->mux_select);
	wrperfmon(3, reg->proc_mode);
}

static void
ev4_handle_interrupt(unsigned long which, struct pt_regs *regs,
		     struct op_counter_config *ctr)
{
	/* EV4 can't properly disable counters individually.
	   Discard "disabled" events now.  */
	if (!ctr[which].enabled)
		return;

	/* Record the sample.  */
	oprofile_add_sample(regs, which);
}


struct op_axp_model op_model_ev4 = {
	.reg_setup		= ev4_reg_setup,
	.cpu_setup		= ev4_cpu_setup,
	.reset_ctr		= NULL,
	.handle_interrupt	= ev4_handle_interrupt,
	.cpu_type		= "alpha/ev4",
	.num_counters		= 2,
	.can_set_proc_mode	= 0,
};
