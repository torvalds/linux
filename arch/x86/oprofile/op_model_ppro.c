/*
 * @file op_model_ppro.h
 * Family 6 perfmon and architectural perfmon MSR operations
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Copyright 2008 Intel Corporation
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Philippe Elie
 * @author Graydon Hoare
 * @author Andi Kleen
 */

#include <linux/oprofile.h>
#include <linux/slab.h>
#include <asm/ptrace.h>
#include <asm/msr.h>
#include <asm/apic.h>
#include <asm/nmi.h>
#include <asm/intel_arch_perfmon.h>

#include "op_x86_model.h"
#include "op_counter.h"

static int num_counters = 2;
static int counter_width = 32;

#define CTR_IS_RESERVED(msrs, c) (msrs->counters[(c)].addr ? 1 : 0)
#define CTR_OVERFLOWED(n) (!((n) & (1ULL<<(counter_width-1))))

#define CTRL_IS_RESERVED(msrs, c) (msrs->controls[(c)].addr ? 1 : 0)
#define CTRL_READ(l, h, msrs, c) do {rdmsr((msrs->controls[(c)].addr), (l), (h)); } while (0)
#define CTRL_WRITE(l, h, msrs, c) do {wrmsr((msrs->controls[(c)].addr), (l), (h)); } while (0)
#define CTRL_SET_ACTIVE(n) (n |= (1<<22))
#define CTRL_SET_INACTIVE(n) (n &= ~(1<<22))
#define CTRL_CLEAR(x) (x &= (1<<21))
#define CTRL_SET_ENABLE(val) (val |= 1<<20)
#define CTRL_SET_USR(val, u) (val |= ((u & 1) << 16))
#define CTRL_SET_KERN(val, k) (val |= ((k & 1) << 17))
#define CTRL_SET_UM(val, m) (val |= (m << 8))
#define CTRL_SET_EVENT(val, e) (val |= e)

static u64 *reset_value;

static void ppro_fill_in_addresses(struct op_msrs * const msrs)
{
	int i;

	for (i = 0; i < num_counters; i++) {
		if (reserve_perfctr_nmi(MSR_P6_PERFCTR0 + i))
			msrs->counters[i].addr = MSR_P6_PERFCTR0 + i;
		else
			msrs->counters[i].addr = 0;
	}

	for (i = 0; i < num_counters; i++) {
		if (reserve_evntsel_nmi(MSR_P6_EVNTSEL0 + i))
			msrs->controls[i].addr = MSR_P6_EVNTSEL0 + i;
		else
			msrs->controls[i].addr = 0;
	}
}


static void ppro_setup_ctrs(struct op_msrs const * const msrs)
{
	unsigned int low, high;
	int i;

	if (!reset_value) {
		reset_value = kmalloc(sizeof(reset_value[0]) * num_counters,
					GFP_ATOMIC);
		if (!reset_value)
			return;
	}

	if (cpu_has_arch_perfmon) {
		union cpuid10_eax eax;
		eax.full = cpuid_eax(0xa);

		/*
		 * For Core2 (family 6, model 15), don't reset the
		 * counter width:
		 */
		if (!(eax.split.version_id == 0 &&
			current_cpu_data.x86 == 6 &&
				current_cpu_data.x86_model == 15)) {

			if (counter_width < eax.split.bit_width)
				counter_width = eax.split.bit_width;
		}
	}

	/* clear all counters */
	for (i = 0 ; i < num_counters; ++i) {
		if (unlikely(!CTRL_IS_RESERVED(msrs, i)))
			continue;
		CTRL_READ(low, high, msrs, i);
		CTRL_CLEAR(low);
		CTRL_WRITE(low, high, msrs, i);
	}

	/* avoid a false detection of ctr overflows in NMI handler */
	for (i = 0; i < num_counters; ++i) {
		if (unlikely(!CTR_IS_RESERVED(msrs, i)))
			continue;
		wrmsrl(msrs->counters[i].addr, -1LL);
	}

	/* enable active counters */
	for (i = 0; i < num_counters; ++i) {
		if ((counter_config[i].enabled) && (CTR_IS_RESERVED(msrs, i))) {
			reset_value[i] = counter_config[i].count;

			wrmsrl(msrs->counters[i].addr, -reset_value[i]);

			CTRL_READ(low, high, msrs, i);
			CTRL_CLEAR(low);
			CTRL_SET_ENABLE(low);
			CTRL_SET_USR(low, counter_config[i].user);
			CTRL_SET_KERN(low, counter_config[i].kernel);
			CTRL_SET_UM(low, counter_config[i].unit_mask);
			CTRL_SET_EVENT(low, counter_config[i].event);
			CTRL_WRITE(low, high, msrs, i);
		} else {
			reset_value[i] = 0;
		}
	}
}


static int ppro_check_ctrs(struct pt_regs * const regs,
			   struct op_msrs const * const msrs)
{
	u64 val;
	int i;

	for (i = 0 ; i < num_counters; ++i) {
		if (!reset_value[i])
			continue;
		rdmsrl(msrs->counters[i].addr, val);
		if (CTR_OVERFLOWED(val)) {
			oprofile_add_sample(regs, i);
			wrmsrl(msrs->counters[i].addr, -reset_value[i]);
		}
	}

	/* Only P6 based Pentium M need to re-unmask the apic vector but it
	 * doesn't hurt other P6 variant */
	apic_write(APIC_LVTPC, apic_read(APIC_LVTPC) & ~APIC_LVT_MASKED);

	/* We can't work out if we really handled an interrupt. We
	 * might have caught a *second* counter just after overflowing
	 * the interrupt for this counter then arrives
	 * and we don't find a counter that's overflowed, so we
	 * would return 0 and get dazed + confused. Instead we always
	 * assume we found an overflow. This sucks.
	 */
	return 1;
}


static void ppro_start(struct op_msrs const * const msrs)
{
	unsigned int low, high;
	int i;

	if (!reset_value)
		return;
	for (i = 0; i < num_counters; ++i) {
		if (reset_value[i]) {
			CTRL_READ(low, high, msrs, i);
			CTRL_SET_ACTIVE(low);
			CTRL_WRITE(low, high, msrs, i);
		}
	}
}


static void ppro_stop(struct op_msrs const * const msrs)
{
	unsigned int low, high;
	int i;

	if (!reset_value)
		return;
	for (i = 0; i < num_counters; ++i) {
		if (!reset_value[i])
			continue;
		CTRL_READ(low, high, msrs, i);
		CTRL_SET_INACTIVE(low);
		CTRL_WRITE(low, high, msrs, i);
	}
}

static void ppro_shutdown(struct op_msrs const * const msrs)
{
	int i;

	for (i = 0 ; i < num_counters ; ++i) {
		if (CTR_IS_RESERVED(msrs, i))
			release_perfctr_nmi(MSR_P6_PERFCTR0 + i);
	}
	for (i = 0 ; i < num_counters ; ++i) {
		if (CTRL_IS_RESERVED(msrs, i))
			release_evntsel_nmi(MSR_P6_EVNTSEL0 + i);
	}
	if (reset_value) {
		kfree(reset_value);
		reset_value = NULL;
	}
}


struct op_x86_model_spec op_ppro_spec = {
	.num_counters		= 2,	/* can be overriden */
	.num_controls		= 2,	/* dito */
	.fill_in_addresses	= &ppro_fill_in_addresses,
	.setup_ctrs		= &ppro_setup_ctrs,
	.check_ctrs		= &ppro_check_ctrs,
	.start			= &ppro_start,
	.stop			= &ppro_stop,
	.shutdown		= &ppro_shutdown
};

/*
 * Architectural performance monitoring.
 *
 * Newer Intel CPUs (Core1+) have support for architectural
 * events described in CPUID 0xA. See the IA32 SDM Vol3b.18 for details.
 * The advantage of this is that it can be done without knowing about
 * the specific CPU.
 */

void arch_perfmon_setup_counters(void)
{
	union cpuid10_eax eax;

	eax.full = cpuid_eax(0xa);

	/* Workaround for BIOS bugs in 6/15. Taken from perfmon2 */
	if (eax.split.version_id == 0 && current_cpu_data.x86 == 6 &&
		current_cpu_data.x86_model == 15) {
		eax.split.version_id = 2;
		eax.split.num_counters = 2;
		eax.split.bit_width = 40;
	}

	num_counters = eax.split.num_counters;

	op_arch_perfmon_spec.num_counters = num_counters;
	op_arch_perfmon_spec.num_controls = num_counters;
	op_ppro_spec.num_counters = num_counters;
	op_ppro_spec.num_controls = num_counters;
}

struct op_x86_model_spec op_arch_perfmon_spec = {
	/* num_counters/num_controls filled in at runtime */
	.fill_in_addresses	= &ppro_fill_in_addresses,
	/* user space does the cpuid check for available events */
	.setup_ctrs		= &ppro_setup_ctrs,
	.check_ctrs		= &ppro_check_ctrs,
	.start			= &ppro_start,
	.stop			= &ppro_stop,
	.shutdown		= &ppro_shutdown
};
