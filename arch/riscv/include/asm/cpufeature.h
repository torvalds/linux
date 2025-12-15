/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022-2024 Rivos, Inc
 */

#ifndef _ASM_CPUFEATURE_H
#define _ASM_CPUFEATURE_H

#include <linux/bitmap.h>
#include <linux/jump_label.h>
#include <linux/workqueue.h>
#include <linux/kconfig.h>
#include <linux/percpu-defs.h>
#include <linux/threads.h>
#include <asm/hwcap.h>
#include <asm/cpufeature-macros.h>

/*
 * These are probed via a device_initcall(), via either the SBI or directly
 * from the corresponding CSRs.
 */
struct riscv_cpuinfo {
	unsigned long mvendorid;
	unsigned long marchid;
	unsigned long mimpid;
};

struct riscv_isainfo {
	DECLARE_BITMAP(isa, RISCV_ISA_EXT_MAX);
};

DECLARE_PER_CPU(struct riscv_cpuinfo, riscv_cpuinfo);

extern const struct seq_operations cpuinfo_op;

/* Per-cpu ISA extensions. */
extern struct riscv_isainfo hart_isa[NR_CPUS];

extern u32 thead_vlenb_of;

void __init riscv_user_isa_enable(void);

#define _RISCV_ISA_EXT_DATA(_name, _id, _subset_exts, _subset_exts_size, _validate) {	\
	.name = #_name,									\
	.property = #_name,								\
	.id = _id,									\
	.subset_ext_ids = _subset_exts,							\
	.subset_ext_size = _subset_exts_size,						\
	.validate = _validate								\
}

#define __RISCV_ISA_EXT_DATA(_name, _id) _RISCV_ISA_EXT_DATA(_name, _id, NULL, 0, NULL)

#define __RISCV_ISA_EXT_DATA_VALIDATE(_name, _id, _validate) \
			_RISCV_ISA_EXT_DATA(_name, _id, NULL, 0, _validate)

/* Used to declare pure "lasso" extension (Zk for instance) */
#define __RISCV_ISA_EXT_BUNDLE(_name, _bundled_exts) \
	_RISCV_ISA_EXT_DATA(_name, RISCV_ISA_EXT_INVALID, _bundled_exts, \
			    ARRAY_SIZE(_bundled_exts), NULL)
#define __RISCV_ISA_EXT_BUNDLE_VALIDATE(_name, _bundled_exts, _validate) \
	_RISCV_ISA_EXT_DATA(_name, RISCV_ISA_EXT_INVALID, _bundled_exts, \
			    ARRAY_SIZE(_bundled_exts), _validate)

/* Used to declare extensions that are a superset of other extensions (Zvbb for instance) */
#define __RISCV_ISA_EXT_SUPERSET(_name, _id, _sub_exts) \
	_RISCV_ISA_EXT_DATA(_name, _id, _sub_exts, ARRAY_SIZE(_sub_exts), NULL)
#define __RISCV_ISA_EXT_SUPERSET_VALIDATE(_name, _id, _sub_exts, _validate) \
	_RISCV_ISA_EXT_DATA(_name, _id, _sub_exts, ARRAY_SIZE(_sub_exts), _validate)

bool __init check_unaligned_access_emulated_all_cpus(void);
void unaligned_access_init(void);
int cpu_online_unaligned_access_init(unsigned int cpu);
#if defined(CONFIG_RISCV_SCALAR_MISALIGNED)
void unaligned_emulation_finish(void);
bool unaligned_ctl_available(void);
#else
static inline bool unaligned_ctl_available(void)
{
	return false;
}
#endif

#if defined(CONFIG_RISCV_MISALIGNED)
DECLARE_PER_CPU(long, misaligned_access_speed);
bool misaligned_traps_can_delegate(void);
#else
static inline bool misaligned_traps_can_delegate(void)
{
	return false;
}
#endif

bool __init check_vector_unaligned_access_emulated_all_cpus(void);
#if defined(CONFIG_RISCV_VECTOR_MISALIGNED)
void check_vector_unaligned_access_emulated(struct work_struct *work __always_unused);
DECLARE_PER_CPU(long, vector_misaligned_access);
#endif

#if defined(CONFIG_RISCV_PROBE_UNALIGNED_ACCESS)
DECLARE_STATIC_KEY_FALSE(fast_unaligned_access_speed_key);

static __always_inline bool has_fast_unaligned_accesses(void)
{
	return static_branch_likely(&fast_unaligned_access_speed_key);
}
#else
static __always_inline bool has_fast_unaligned_accesses(void)
{
	if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS))
		return true;
	else
		return false;
}
#endif

unsigned long riscv_get_elf_hwcap(void);

struct riscv_isa_ext_data {
	const unsigned int id;
	const char *name;
	const char *property;
	const unsigned int *subset_ext_ids;
	const unsigned int subset_ext_size;
	int (*validate)(const struct riscv_isa_ext_data *data, const unsigned long *isa_bitmap);
};

extern const struct riscv_isa_ext_data riscv_isa_ext[];
extern const size_t riscv_isa_ext_count;
extern bool riscv_isa_fallback;

unsigned long riscv_isa_extension_base(const unsigned long *isa_bitmap);
static __always_inline bool riscv_cpu_has_extension_likely(int cpu, const unsigned long ext)
{
	compiletime_assert(ext < RISCV_ISA_EXT_MAX, "ext must be < RISCV_ISA_EXT_MAX");

	if (IS_ENABLED(CONFIG_RISCV_ALTERNATIVE) &&
	    __riscv_has_extension_likely(STANDARD_EXT, ext))
		return true;

	return __riscv_isa_extension_available(hart_isa[cpu].isa, ext);
}

static __always_inline bool riscv_cpu_has_extension_unlikely(int cpu, const unsigned long ext)
{
	compiletime_assert(ext < RISCV_ISA_EXT_MAX, "ext must be < RISCV_ISA_EXT_MAX");

	if (IS_ENABLED(CONFIG_RISCV_ALTERNATIVE) &&
	    __riscv_has_extension_unlikely(STANDARD_EXT, ext))
		return true;

	return __riscv_isa_extension_available(hart_isa[cpu].isa, ext);
}

#endif
