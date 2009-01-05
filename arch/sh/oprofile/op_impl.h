#ifndef __OP_IMPL_H
#define __OP_IMPL_H

/* Per-counter configuration as set via oprofilefs.  */
struct op_counter_config {
	unsigned long enabled;
	unsigned long event;

	unsigned long long count;

	/* Dummy values for userspace tool compliance */
	unsigned long kernel;
	unsigned long user;
	unsigned long unit_mask;
};

/* Per-architecture configury and hooks.  */
struct op_sh_model {
	void (*reg_setup)(struct op_counter_config *);
	int (*create_files)(struct super_block *sb, struct dentry *dir);
	void (*cpu_setup)(void *dummy);
	int (*init)(void);
	void (*exit)(void);
	void (*cpu_start)(void *args);
	void (*cpu_stop)(void *args);
	char *cpu_type;
	unsigned char num_counters;
};

/* arch/sh/oprofile/common.c */
extern void sh_backtrace(struct pt_regs * const regs, unsigned int depth);

#endif /* __OP_IMPL_H */
