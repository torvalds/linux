/**
 * @file arch/alpha/oprofile/op_model_ev67.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Richard Henderson <rth@twiddle.net>
 * @author Falk Hueffner <falk@debian.org>
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <asm/ptrace.h>
#include <asm/system.h>

#include "op_impl.h"


/* Compute all of the registers in preparation for enabling profiling.  */

static void
ev67_reg_setup(struct op_register_config *reg,
	       struct op_counter_config *ctr,
	       struct op_system_config *sys)
{
	unsigned long ctl, reset, need_reset, i;

	/* Select desired events.  */
	ctl = 1UL << 4;		/* Enable ProfileMe mode. */

	/* The event numbers are chosen so we can use them directly if
	   PCTR1 is enabled.  */
	if (ctr[1].enabled) {
		ctl |= (ctr[1].event & 3) << 2;
	} else {
		if (ctr[0].event == 0) /* cycles */
			ctl |= 1UL << 2;
	}
	reg->mux_select = ctl;

	/* Select logging options.  */
	/* ??? Need to come up with some mechanism to trace only
	   selected processes.  EV67 does not have a mechanism to
	   select kernel or user mode only.  For now, enable always.  */
	reg->proc_mode = 0;

	/* EV67 cannot change the width of the counters as with the
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
ev67_cpu_setup (void *x)
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
ev67_reset_ctr(struct op_register_config *reg, unsigned long ctr)
{
	wrperfmon(6, reg->reset_values | (1 << ctr));
}

/* ProfileMe conditions which will show up as counters. We can also
   detect the following, but it seems unlikely that anybody is
   interested in counting them:
    * Reset
    * MT_FPCR (write to floating point control register)
    * Arithmetic trap
    * Dstream Fault
    * Machine Check (ECC fault, etc.)
    * OPCDEC (illegal opcode)
    * Floating point disabled
    * Differentiate between DTB single/double misses and 3 or 4 level
      page tables
    * Istream access violation
    * Interrupt
    * Icache Parity Error.
    * Instruction killed (nop, trapb)

   Unfortunately, there seems to be no way to detect Dcache and Bcache
   misses; the latter could be approximated by making the counter
   count Bcache misses, but that is not precise.

   We model this as 20 counters:
    * PCTR0
    * PCTR1
    * 9 ProfileMe events, induced by PCTR0
    * 9 ProfileMe events, induced by PCTR1
*/

enum profileme_counters {
	PM_STALLED,		/* Stalled for at least one cycle
				   between the fetch and map stages  */
	PM_TAKEN,		/* Conditional branch taken */
	PM_MISPREDICT,		/* Branch caused mispredict trap */
	PM_ITB_MISS,		/* ITB miss */
	PM_DTB_MISS,		/* DTB miss */
	PM_REPLAY,		/* Replay trap */
	PM_LOAD_STORE,		/* Load-store order trap */
	PM_ICACHE_MISS,		/* Icache miss */
	PM_UNALIGNED,		/* Unaligned Load/Store */
	PM_NUM_COUNTERS
};

static inline void
op_add_pm(unsigned long pc, int kern, unsigned long counter,
	  struct op_counter_config *ctr, unsigned long event)
{
	unsigned long fake_counter = 2 + event;
	if (counter == 1)
		fake_counter += PM_NUM_COUNTERS;
	if (ctr[fake_counter].enabled)
		oprofile_add_pc(pc, kern, fake_counter);
}

static void
ev67_handle_interrupt(unsigned long which, struct pt_regs *regs,
		      struct op_counter_config *ctr)
{
	unsigned long pmpc, pctr_ctl;
	int kern = !user_mode(regs);
	int mispredict = 0;
	union {
		unsigned long v;
		struct {
			unsigned reserved:	30; /*  0-29 */
			unsigned overcount:	 3; /* 30-32 */
			unsigned icache_miss:	 1; /*    33 */
			unsigned trap_type:	 4; /* 34-37 */
			unsigned load_store:	 1; /*    38 */
			unsigned trap:		 1; /*    39 */
			unsigned mispredict:	 1; /*    40 */
		} fields;
	} i_stat;

	enum trap_types {
		TRAP_REPLAY,
		TRAP_INVALID0,
		TRAP_DTB_DOUBLE_MISS_3,
		TRAP_DTB_DOUBLE_MISS_4,
		TRAP_FP_DISABLED,
		TRAP_UNALIGNED,
		TRAP_DTB_SINGLE_MISS,
		TRAP_DSTREAM_FAULT,
		TRAP_OPCDEC,
		TRAP_INVALID1,
		TRAP_MACHINE_CHECK,
		TRAP_INVALID2,
		TRAP_ARITHMETIC,
		TRAP_INVALID3,
		TRAP_MT_FPCR,
		TRAP_RESET
	};

	pmpc = wrperfmon(9, 0);
	/* ??? Don't know how to handle physical-mode PALcode address.  */
	if (pmpc & 1)
		return;
	pmpc &= ~2;		/* clear reserved bit */

	i_stat.v = wrperfmon(8, 0);
	if (i_stat.fields.trap) {
		switch (i_stat.fields.trap_type) {
		case TRAP_INVALID1:
		case TRAP_INVALID2:
		case TRAP_INVALID3:
			/* Pipeline redirection ocurred. PMPC points
			   to PALcode. Recognize ITB miss by PALcode
			   offset address, and get actual PC from
			   EXC_ADDR.  */
			oprofile_add_pc(regs->pc, kern, which);
			if ((pmpc & ((1 << 15) - 1)) ==  581)
				op_add_pm(regs->pc, kern, which,
					  ctr, PM_ITB_MISS);
			/* Most other bit and counter values will be
			   those for the first instruction in the
			   fault handler, so we're done.  */
			return;
		case TRAP_REPLAY:
			op_add_pm(pmpc, kern, which, ctr,
				  (i_stat.fields.load_store
				   ? PM_LOAD_STORE : PM_REPLAY));
			break;
		case TRAP_DTB_DOUBLE_MISS_3:
		case TRAP_DTB_DOUBLE_MISS_4:
		case TRAP_DTB_SINGLE_MISS:
			op_add_pm(pmpc, kern, which, ctr, PM_DTB_MISS);
			break;
		case TRAP_UNALIGNED:
			op_add_pm(pmpc, kern, which, ctr, PM_UNALIGNED);
			break;
		case TRAP_INVALID0:
		case TRAP_FP_DISABLED:
		case TRAP_DSTREAM_FAULT:
		case TRAP_OPCDEC:
		case TRAP_MACHINE_CHECK:
		case TRAP_ARITHMETIC:
		case TRAP_MT_FPCR:
		case TRAP_RESET:
			break;
		}

		/* ??? JSR/JMP/RET/COR or HW_JSR/HW_JMP/HW_RET/HW_COR
		   mispredicts do not set this bit but can be
		   recognized by the presence of one of these
		   instructions at the PMPC location with bit 39
		   set.  */
		if (i_stat.fields.mispredict) {
			mispredict = 1;
			op_add_pm(pmpc, kern, which, ctr, PM_MISPREDICT);
		}
	}

	oprofile_add_pc(pmpc, kern, which);

	pctr_ctl = wrperfmon(5, 0);
	if (pctr_ctl & (1UL << 27))
		op_add_pm(pmpc, kern, which, ctr, PM_STALLED);

	/* Unfortunately, TAK is undefined on mispredicted branches.
	   ??? It is also undefined for non-cbranch insns, should
	   check that.  */
	if (!mispredict && pctr_ctl & (1UL << 0))
		op_add_pm(pmpc, kern, which, ctr, PM_TAKEN);
}

struct op_axp_model op_model_ev67 = {
	.reg_setup		= ev67_reg_setup,
	.cpu_setup		= ev67_cpu_setup,
	.reset_ctr		= ev67_reset_ctr,
	.handle_interrupt	= ev67_handle_interrupt,
	.cpu_type		= "alpha/ev67",
	.num_counters		= 20,
	.can_set_proc_mode	= 0,
};
