/**
 * @file op_x86_model.h
 * interface to x86 model-specific MSR operations
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Graydon Hoare
 */

#ifndef OP_X86_MODEL_H
#define OP_X86_MODEL_H

struct op_saved_msr {
	unsigned int high;
	unsigned int low;
};

struct op_msr {
	unsigned long addr;
	struct op_saved_msr saved;
};

struct op_msrs {
	struct op_msr *counters;
	struct op_msr *controls;
};

struct pt_regs;

/* The model vtable abstracts the differences between
 * various x86 CPU models' perfctr support.
 */
struct op_x86_model_spec {
	int (*init)(struct oprofile_operations *ops);
	void (*exit)(void);
	unsigned int num_counters;
	unsigned int num_controls;
	void (*fill_in_addresses)(struct op_msrs * const msrs);
	void (*setup_ctrs)(struct op_msrs const * const msrs);
	int (*check_ctrs)(struct pt_regs * const regs,
		struct op_msrs const * const msrs);
	void (*start)(struct op_msrs const * const msrs);
	void (*stop)(struct op_msrs const * const msrs);
	void (*shutdown)(struct op_msrs const * const msrs);
};

extern struct op_x86_model_spec op_ppro_spec;
extern struct op_x86_model_spec const op_p4_spec;
extern struct op_x86_model_spec const op_p4_ht2_spec;
extern struct op_x86_model_spec const op_amd_spec;
extern struct op_x86_model_spec op_arch_perfmon_spec;

extern void arch_perfmon_setup_counters(void);

#endif /* OP_X86_MODEL_H */
