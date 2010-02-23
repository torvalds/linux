/**
 * @file op_x86_model.h
 * interface to x86 model-specific MSR operations
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Graydon Hoare
 * @author Robert Richter <robert.richter@amd.com>
 */

#ifndef OP_X86_MODEL_H
#define OP_X86_MODEL_H

#include <asm/types.h>
#include <asm/perf_event.h>

struct op_msr {
	unsigned long	addr;
	u64		saved;
};

struct op_msrs {
	struct op_msr *counters;
	struct op_msr *controls;
	struct op_msr *multiplex;
};

struct pt_regs;

struct oprofile_operations;

/* The model vtable abstracts the differences between
 * various x86 CPU models' perfctr support.
 */
struct op_x86_model_spec {
	unsigned int	num_counters;
	unsigned int	num_controls;
	unsigned int	num_virt_counters;
	u64		reserved;
	u16		event_mask;
	int		(*init)(struct oprofile_operations *ops);
	void		(*exit)(void);
	void		(*fill_in_addresses)(struct op_msrs * const msrs);
	void		(*setup_ctrs)(struct op_x86_model_spec const *model,
				      struct op_msrs const * const msrs);
	int		(*check_ctrs)(struct pt_regs * const regs,
				      struct op_msrs const * const msrs);
	void		(*start)(struct op_msrs const * const msrs);
	void		(*stop)(struct op_msrs const * const msrs);
	void		(*shutdown)(struct op_msrs const * const msrs);
#ifdef CONFIG_OPROFILE_EVENT_MULTIPLEX
	void		(*switch_ctrl)(struct op_x86_model_spec const *model,
				       struct op_msrs const * const msrs);
#endif
};

struct op_counter_config;

static inline void op_x86_warn_in_use(int counter)
{
	pr_warning("oprofile: counter #%d on cpu #%d may already be used\n",
		   counter, smp_processor_id());
}

static inline void op_x86_warn_reserved(int counter)
{
	pr_warning("oprofile: counter #%d is already reserved\n", counter);
}

extern u64 op_x86_get_ctrl(struct op_x86_model_spec const *model,
			   struct op_counter_config *counter_config);
extern int op_x86_phys_to_virt(int phys);
extern int op_x86_virt_to_phys(int virt);

extern struct op_x86_model_spec op_ppro_spec;
extern struct op_x86_model_spec op_p4_spec;
extern struct op_x86_model_spec op_p4_ht2_spec;
extern struct op_x86_model_spec op_amd_spec;
extern struct op_x86_model_spec op_arch_perfmon_spec;

#endif /* OP_X86_MODEL_H */
