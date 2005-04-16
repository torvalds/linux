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

extern int __init pmu_init(struct oprofile_operations *ops, struct op_arm_model_spec *spec);
extern void pmu_exit(void);
#endif /* OP_ARM_MODEL_H */
