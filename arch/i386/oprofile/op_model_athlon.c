/**
 * @file op_model_athlon.h
 * athlon / K7 model-specific MSR operations
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
 
#include "op_x86_model.h"
#include "op_counter.h"

#define NUM_COUNTERS 4
#define NUM_CONTROLS 4

#define CTR_READ(l,h,msrs,c) do {rdmsr(msrs->counters[(c)].addr, (l), (h));} while (0)
#define CTR_WRITE(l,msrs,c) do {wrmsr(msrs->counters[(c)].addr, -(unsigned int)(l), -1);} while (0)
#define CTR_OVERFLOWED(n) (!((n) & (1U<<31)))

#define CTRL_READ(l,h,msrs,c) do {rdmsr(msrs->controls[(c)].addr, (l), (h));} while (0)
#define CTRL_WRITE(l,h,msrs,c) do {wrmsr(msrs->controls[(c)].addr, (l), (h));} while (0)
#define CTRL_SET_ACTIVE(n) (n |= (1<<22))
#define CTRL_SET_INACTIVE(n) (n &= ~(1<<22))
#define CTRL_CLEAR(x) (x &= (1<<21))
#define CTRL_SET_ENABLE(val) (val |= 1<<20)
#define CTRL_SET_USR(val,u) (val |= ((u & 1) << 16))
#define CTRL_SET_KERN(val,k) (val |= ((k & 1) << 17))
#define CTRL_SET_UM(val, m) (val |= (m << 8))
#define CTRL_SET_EVENT(val, e) (val |= e)

static unsigned long reset_value[NUM_COUNTERS];
 
static void athlon_fill_in_addresses(struct op_msrs * const msrs)
{
	msrs->counters[0].addr = MSR_K7_PERFCTR0;
	msrs->counters[1].addr = MSR_K7_PERFCTR1;
	msrs->counters[2].addr = MSR_K7_PERFCTR2;
	msrs->counters[3].addr = MSR_K7_PERFCTR3;

	msrs->controls[0].addr = MSR_K7_EVNTSEL0;
	msrs->controls[1].addr = MSR_K7_EVNTSEL1;
	msrs->controls[2].addr = MSR_K7_EVNTSEL2;
	msrs->controls[3].addr = MSR_K7_EVNTSEL3;
}

 
static void athlon_setup_ctrs(struct op_msrs const * const msrs)
{
	unsigned int low, high;
	int i;
 
	/* clear all counters */
	for (i = 0 ; i < NUM_CONTROLS; ++i) {
		CTRL_READ(low, high, msrs, i);
		CTRL_CLEAR(low);
		CTRL_WRITE(low, high, msrs, i);
	}
	
	/* avoid a false detection of ctr overflows in NMI handler */
	for (i = 0; i < NUM_COUNTERS; ++i) {
		CTR_WRITE(1, msrs, i);
	}

	/* enable active counters */
	for (i = 0; i < NUM_COUNTERS; ++i) {
		if (counter_config[i].enabled) {
			reset_value[i] = counter_config[i].count;

			CTR_WRITE(counter_config[i].count, msrs, i);

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

 
static int athlon_check_ctrs(struct pt_regs * const regs,
			     struct op_msrs const * const msrs)
{
	unsigned int low, high;
	int i;

	for (i = 0 ; i < NUM_COUNTERS; ++i) {
		CTR_READ(low, high, msrs, i);
		if (CTR_OVERFLOWED(low)) {
			oprofile_add_sample(regs, i);
			CTR_WRITE(reset_value[i], msrs, i);
		}
	}

	/* See op_model_ppro.c */
	return 1;
}

 
static void athlon_start(struct op_msrs const * const msrs)
{
	unsigned int low, high;
	int i;
	for (i = 0 ; i < NUM_COUNTERS ; ++i) {
		if (reset_value[i]) {
			CTRL_READ(low, high, msrs, i);
			CTRL_SET_ACTIVE(low);
			CTRL_WRITE(low, high, msrs, i);
		}
	}
}


static void athlon_stop(struct op_msrs const * const msrs)
{
	unsigned int low,high;
	int i;

	/* Subtle: stop on all counters to avoid race with
	 * setting our pm callback */
	for (i = 0 ; i < NUM_COUNTERS ; ++i) {
		CTRL_READ(low, high, msrs, i);
		CTRL_SET_INACTIVE(low);
		CTRL_WRITE(low, high, msrs, i);
	}
}


struct op_x86_model_spec const op_athlon_spec = {
	.num_counters = NUM_COUNTERS,
	.num_controls = NUM_CONTROLS,
	.fill_in_addresses = &athlon_fill_in_addresses,
	.setup_ctrs = &athlon_setup_ctrs,
	.check_ctrs = &athlon_check_ctrs,
	.start = &athlon_start,
	.stop = &athlon_stop
};
