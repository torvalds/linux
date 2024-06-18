/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_CPU_INFO_H
#define __ASM_CPU_INFO_H

#include <linux/cache.h>
#include <linux/types.h>

#include <asm/loongarch.h>

/* cache_desc->flags */
enum {
	CACHE_PRESENT	= (1 << 0),
	CACHE_PRIVATE	= (1 << 1),	/* core private cache */
	CACHE_INCLUSIVE	= (1 << 2),	/* include the inner level caches */
};

/*
 * Descriptor for a cache
 */
struct cache_desc {
	unsigned char type;
	unsigned char level;
	unsigned short sets;	/* Number of lines per set */
	unsigned char ways;	/* Number of ways */
	unsigned char linesz;	/* Size of line in bytes */
	unsigned char flags;	/* Flags describing cache properties */
};

#define CACHE_LEVEL_MAX		3
#define CACHE_LEAVES_MAX	6

struct cpuinfo_loongarch {
	u64			asid_cache;
	unsigned long		asid_mask;

	/*
	 * Capability and feature descriptor structure for LoongArch CPU
	 */
	unsigned long long	options;
	unsigned int		processor_id;
	unsigned int		fpu_vers;
	unsigned int		fpu_csr0;
	unsigned int		fpu_mask;
	unsigned int		cputype;
	int			isa_level;
	int			tlbsize;
	int			tlbsizemtlb;
	int			tlbsizestlbsets;
	int			tlbsizestlbways;
	int			cache_leaves_present; /* number of cache_leaves[] elements */
	struct cache_desc	cache_leaves[CACHE_LEAVES_MAX];
	int			core;   /* physical core number in package */
	int			package;/* physical package number */
	int			global_id; /* physical global thread number */
	int			vabits; /* Virtual Address size in bits */
	int			pabits; /* Physical Address size in bits */
	unsigned int		ksave_mask; /* Usable KSave mask. */
	unsigned int		watch_dreg_count;   /* Number data breakpoints */
	unsigned int		watch_ireg_count;   /* Number instruction breakpoints */
	unsigned int		watch_reg_use_cnt; /* min(NUM_WATCH_REGS, watch_dreg_count + watch_ireg_count), Usable by ptrace */
} __aligned(SMP_CACHE_BYTES);

extern struct cpuinfo_loongarch cpu_data[];
#define boot_cpu_data cpu_data[0]
#define current_cpu_data cpu_data[smp_processor_id()]
#define raw_current_cpu_data cpu_data[raw_smp_processor_id()]

extern void cpu_probe(void);

extern const char *__cpu_family[];
extern const char *__cpu_full_name[];
#define cpu_family_string()	__cpu_family[raw_smp_processor_id()]
#define cpu_full_name_string()	__cpu_full_name[raw_smp_processor_id()]

struct seq_file;
struct notifier_block;

extern int register_proc_cpuinfo_notifier(struct notifier_block *nb);
extern int proc_cpuinfo_notifier_call_chain(unsigned long val, void *v);

#define proc_cpuinfo_notifier(fn, pri)					\
({									\
	static struct notifier_block fn##_nb = {			\
		.notifier_call = fn,					\
		.priority = pri						\
	};								\
									\
	register_proc_cpuinfo_notifier(&fn##_nb);			\
})

struct proc_cpuinfo_notifier_args {
	struct seq_file *m;
	unsigned long n;
};

static inline bool cpus_are_siblings(int cpua, int cpub)
{
	struct cpuinfo_loongarch *infoa = &cpu_data[cpua];
	struct cpuinfo_loongarch *infob = &cpu_data[cpub];

	if (infoa->package != infob->package)
		return false;

	if (infoa->core != infob->core)
		return false;

	return true;
}

static inline unsigned long cpu_asid_mask(struct cpuinfo_loongarch *cpuinfo)
{
	return cpuinfo->asid_mask;
}

static inline void set_cpu_asid_mask(struct cpuinfo_loongarch *cpuinfo,
				     unsigned long asid_mask)
{
	cpuinfo->asid_mask = asid_mask;
}

#endif /* __ASM_CPU_INFO_H */
