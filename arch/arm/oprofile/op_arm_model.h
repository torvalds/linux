/**
 * @file op_arm_model.h
 * interface to ARM machine specific operations
 *
 * @remark Copyright 2004 Oprofile Authors
 * @remark Read the file COPYING
 *
 * @author Zwane Mwaikambo
 */

#ifndef OP_ARM_MODEL_H
#define OP_ARM_MODEL_H

struct op_arm_model_spec {
	int (*init)(void);
	unsigned int num_counters;
	int (*setup_ctrs)(void);
	int (*start)(void);
	void (*stop)(void);
	char *name;
};

#ifdef CONFIG_CPU_XSCALE
extern struct op_arm_model_spec op_xscale_spec;
#endif

extern struct op_arm_model_spec op_armv6_spec;
extern struct op_arm_model_spec op_mpcore_spec;
extern struct op_arm_model_spec op_armv7_spec;

extern void arm_backtrace(struct pt_regs * const regs, unsigned int depth);

extern int __init op_arm_init(struct oprofile_operations *ops, struct op_arm_model_spec *spec);
extern void op_arm_exit(void);
#endif /* OP_ARM_MODEL_H */
