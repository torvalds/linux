/**
 * @file arch/alpha/oprofile/op_impl.h
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Richard Henderson <rth@twiddle.net>
 */

#ifndef OP_IMPL_H
#define OP_IMPL_H 1

extern int null_perf_irq(void);
extern int (*perf_irq)(void);

/* Per-counter configuration as set via oprofilefs.  */
struct op_counter_config {
	unsigned long enabled;
	unsigned long event;
	unsigned long count;
	/* Dummies because I am too lazy to hack the userspace tools.  */
	unsigned long kernel;
	unsigned long user;
	unsigned long exl;
	unsigned long unit_mask;
};

/* Per-architecture configury and hooks.  */
struct op_mips_model {
	void (*reg_setup) (struct op_counter_config *);
	void (*cpu_setup) (void * dummy);
	int (*init)(void);
	void (*exit)(void);
	void (*cpu_start)(void *args);
	void (*cpu_stop)(void *args);
	char *cpu_type;
	unsigned char num_counters;
};

#endif
