/**
 * @file op_model_ppro.h
 * pentium pro / P6 model-specific MSR operations
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Philippe Elie
 * @author Graydon Hoare
 */

#include <linux/oprofile.h>
#include <asm/ptrace.h>
#include <asm/msr.h>
#include <asm/apic.h>
#include <asm/nmi.h>
 
#include "op_x86_model.h"
#include "op_counter.h"

#define NUM_COUNTERS 2
#define NUM_CONTROLS 2

#define CTR_IS_RESERVED(msrs,c) (msrs->counters[(c)].addr ? 1 : 0)
#define CTR_READ(l,h,msrs,c) do {rdmsr(msrs->counters[(c)].addr, (l), (h));} while (0)
#define CTR_32BIT_WRITE(l,msrs,c)	\
	do {wrmsr(msrs->counters[(c)].addr, -(u32)(l), 0);} while (0)
#define CTR_OVERFLOWED(n) (!((n) & (1U<<31)))

#define CTRL_IS_RESERVED(msrs,c) (msrs->controls[(c)].addr ? 1 : 0)
#define CTRL_READ(l,h,msrs,c) do {rdmsr((msrs->controls[(c)].addr), (l), (h));} while (0)
#define CTRL_WRITE(l,h,msrs,c) do {wrmsr((msrs->controls[(c)].addr), (l), (h));} while (0)
#define CTRL_SET_ACTIVE(n) (n |= (1<<22))
#define CTRL_SET_INACTIVE(n) (n &= ~(1<<22))
#define CTRL_CLEAR(x) (x &= (1<<21))
#define CTRL_SET_ENABLE(val) (val |= 1<<20)
#define CTRL_SET_USR(val,u) (val |= ((u & 1) << 16))
#define CTRL_SET_KERN(val,k) (val |= ((k & 1) << 17))
#define CTRL_SET_UM(val, m) (val |= (m << 8))
#define CTRL_SET_EVENT(val, e) (val |= e)

static unsigned long reset_value[NUM_COUNTERS];
 
static void ppro_fill_in_addresses(struct op_msrs * const msrs)
{
	int i;

	for (i=0; i < NUM_COUNTERS; i++) {
		if (reserve_perfctr_nmi(MSR_P6_PERFCTR0 + i))
			msrs->counters[i].addr = MSR_P6_PERFCTR0 + i;
		else
			msrs->counters[i].addr = 0;
	}
	
	for (i=0; i < NUM_CONTROLS; i++) {
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

	/* clear all counters */
	for (i = 0 ; i < NUM_CONTROLS; ++i) {
		if (unlikely(!CTRL_IS_RESERVED(msrs,i)))
			continue;
		CTRL_READ(low, high, msrs, i);
		CTRL_CLEAR(low);
		CTRL_WRITE(low, high, msrs, i);
	}
	
	/* avoid a false detection of ctr overflows in NMI handler */
	for (i = 0; i < NUM_COUNTERS; ++i) {
		if (unlikely(!CTR_IS_RESERVED(msrs,i)))
			continue;
		CTR_32BIT_WRITE(1, msrs, i);
	}

	/* enable active counters */
	for (i = 0; i < NUM_COUNTERS; ++i) {
		if ((counter_config[i].enabled) && (CTR_IS_RESERVED(msrs,i))) {
			reset_value[i] = counter_config[i].count;

			CTR_32BIT_WRITE(counter_config[i].count, msrs, i);

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
	unsigned int low, high;
	int i;
 
	for (i = 0 ; i < NUM_COUNTERS; ++i) {
		if (!reset_value[i])
			continue;
		CTR_READ(low, high, msrs, i);
		if (CTR_OVERFLOWED(low)) {
			oprofile_add_sample(regs, i);
			CTR_32BIT_WRITE(reset_value[i], msrs, i);
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
	unsigned int low,high;
	int i;

	for (i = 0; i < NUM_COUNTERS; ++i) {
		if (reset_value[i]) {
			CTRL_READ(low, high, msrs, i);
			CTRL_SET_ACTIVE(low);
			CTRL_WRITE(low, high, msrs, i);
		}
	}
}


static void ppro_stop(struct op_msrs const * const msrs)
{
	unsigned int low,high;
	int i;

	for (i = 0; i < NUM_COUNTERS; ++i) {
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

	for (i = 0 ; i < NUM_COUNTERS ; ++i) {
		if (CTR_IS_RESERVED(msrs,i))
			release_perfctr_nmi(MSR_P6_PERFCTR0 + i);
	}
	for (i = 0 ; i < NUM_CONTROLS ; ++i) {
		if (CTRL_IS_RESERVED(msrs,i))
			release_evntsel_nmi(MSR_P6_EVNTSEL0 + i);
	}
}


struct op_x86_model_spec const op_ppro_spec = {
	.num_counters = NUM_COUNTERS,
	.num_controls = NUM_CONTROLS,
	.fill_in_addresses = &ppro_fill_in_addresses,
	.setup_ctrs = &ppro_setup_ctrs,
	.check_ctrs = &ppro_check_ctrs,
	.start = &ppro_start,
	.stop = &ppro_stop,
	.shutdown = &ppro_shutdown
};
