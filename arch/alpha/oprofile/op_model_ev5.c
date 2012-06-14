/**
 * @file arch/alpha/oprofile/op_model_ev5.c
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

#include "op_impl.h"


/* Compute all of the registers in preparation for enabling profiling.

   The 21164 (EV5) and 21164PC (PCA65) vary in the bit placement and
   meaning of the "CBOX" events.  Given that we don't care about meaning
   at this point, arrange for the difference in bit placement to be
   handled by common code.  */

static void
common_reg_setup(struct op_register_config *reg,
		 struct op_counter_config *ctr,
		 struct op_system_config *sys,
		 int cbox1_ofs, int cbox2_ofs)
{
	int i, ctl, reset, need_reset;

	/* Select desired events.  The event numbers are selected such
	   that they map directly into the event selection fields:

		PCSEL0:	0, 1
		PCSEL1:	24-39
		 CBOX1: 40-47
		PCSEL2: 48-63
		 CBOX2: 64-71

	   There are two special cases, in that CYCLES can be measured
	   on PCSEL[02], and SCACHE_WRITE can be measured on CBOX[12].
	   These event numbers are canonicalizes to their first appearance.  */

	ctl = 0;
	for (i = 0; i < 3; ++i) {
		unsigned long event = ctr[i].event;
		if (!ctr[i].enabled)
			continue;

		/* Remap the duplicate events, as described above.  */
		if (i == 2) {
			if (event == 0)
				event = 12+48;
			else if (event == 2+41)
				event = 4+65;
		}

		/* Convert the event numbers onto mux_select bit mask.  */
		if (event < 2)
			ctl |= event << 31;
		else if (event < 24)
			/* error */;
		else if (event < 40)
			ctl |= (event - 24) << 4;
		else if (event < 48)
			ctl |= (event - 40) << cbox1_ofs | 15 << 4;
		else if (event < 64)
			ctl |= event - 48;
		else if (event < 72)
			ctl |= (event - 64) << cbox2_ofs | 15;
	}
	reg->mux_select = ctl;

	/* Select processor mode.  */
	/* ??? Need to come up with some mechanism to trace only selected
	   processes.  For now select from pal, kernel and user mode.  */
	ctl = 0;
	ctl |= !sys->enable_pal << 9;
	ctl |= !sys->enable_kernel << 8;
	ctl |= !sys->enable_user << 30;
	reg->proc_mode = ctl;

	/* Select interrupt frequencies.  Take the interrupt count selected
	   by the user, and map it onto one of the possible counter widths.
	   If the user value is in between, compute a value to which the
	   counter is reset at each interrupt.  */

	ctl = reset = need_reset = 0;
	for (i = 0; i < 3; ++i) {
		unsigned long max, hilo, count = ctr[i].count;
		if (!ctr[i].enabled)
			continue;

		if (count <= 256)
			count = 256, hilo = 3, max = 256;
		else {
			max = (i == 2 ? 16384 : 65536);
			hilo = 2;
			if (count > max)
				count = max;
		}
		ctr[i].count = count;

		ctl |= hilo << (8 - i*2);
		reset |= (max - count) << (48 - 16*i);
		if (count != max)
			need_reset |= 1 << i;
	}
	reg->freq = ctl;
	reg->reset_values = reset;
	reg->need_reset = need_reset;
}

static void
ev5_reg_setup(struct op_register_config *reg,
	      struct op_counter_config *ctr,
	      struct op_system_config *sys)
{
	common_reg_setup(reg, ctr, sys, 19, 22);
}

static void
pca56_reg_setup(struct op_register_config *reg,
	        struct op_counter_config *ctr,
	        struct op_system_config *sys)
{
	common_reg_setup(reg, ctr, sys, 8, 11);
}

/* Program all of the registers in preparation for enabling profiling.  */

static void
ev5_cpu_setup (void *x)
{
	struct op_register_config *reg = x;

	wrperfmon(2, reg->mux_select);
	wrperfmon(3, reg->proc_mode);
	wrperfmon(4, reg->freq);
	wrperfmon(6, reg->reset_values);
}

/* CTR is a counter for which the user has requested an interrupt count
   in between one of the widths selectable in hardware.  Reset the count
   for CTR to the value stored in REG->RESET_VALUES.

   For EV5, this means disabling profiling, reading the current values,
   masking in the value for the desired register, writing, then turning
   profiling back on.

   This can be streamlined if profiling is only enabled for user mode.
   In that case we know that the counters are not currently incrementing
   (due to being in kernel mode).  */

static void
ev5_reset_ctr(struct op_register_config *reg, unsigned long ctr)
{
	unsigned long values, mask, not_pk, reset_values;

	mask = (ctr == 0 ? 0xfffful << 48
	        : ctr == 1 ? 0xfffful << 32
		: 0x3fff << 16);

	not_pk = 1 << 9 | 1 << 8;

	reset_values = reg->reset_values;

	if ((reg->proc_mode & not_pk) == not_pk) {
		values = wrperfmon(5, 0);
		values = (reset_values & mask) | (values & ~mask & -2);
		wrperfmon(6, values);
	} else {
		wrperfmon(0, -1);
		values = wrperfmon(5, 0);
		values = (reset_values & mask) | (values & ~mask & -2);
		wrperfmon(6, values);
		wrperfmon(1, reg->enable);
	}
}

static void
ev5_handle_interrupt(unsigned long which, struct pt_regs *regs,
		     struct op_counter_config *ctr)
{
	/* Record the sample.  */
	oprofile_add_sample(regs, which);
}


struct op_axp_model op_model_ev5 = {
	.reg_setup		= ev5_reg_setup,
	.cpu_setup		= ev5_cpu_setup,
	.reset_ctr		= ev5_reset_ctr,
	.handle_interrupt	= ev5_handle_interrupt,
	.cpu_type		= "alpha/ev5",
	.num_counters		= 3,
	.can_set_proc_mode	= 1,
};

struct op_axp_model op_model_pca56 = {
	.reg_setup		= pca56_reg_setup,
	.cpu_setup		= ev5_cpu_setup,
	.reset_ctr		= ev5_reset_ctr,
	.handle_interrupt	= ev5_handle_interrupt,
	.cpu_type		= "alpha/pca56",
	.num_counters		= 3,
	.can_set_proc_mode	= 1,
};
