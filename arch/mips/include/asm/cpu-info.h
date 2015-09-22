/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 Waldorf GMBH
 * Copyright (C) 1995, 1996, 1997, 1998, 1999, 2001, 2002, 2003 Ralf Baechle
 * Copyright (C) 1996 Paul M. Antoine
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2004  Maciej W. Rozycki
 */
#ifndef __ASM_CPU_INFO_H
#define __ASM_CPU_INFO_H

#include <linux/types.h>

#include <asm/cache.h>

/*
 * Descriptor for a cache
 */
struct cache_desc {
	unsigned int waysize;	/* Bytes per way */
	unsigned short sets;	/* Number of lines per set */
	unsigned char ways;	/* Number of ways */
	unsigned char linesz;	/* Size of line in bytes */
	unsigned char waybit;	/* Bits to select in a cache set */
	unsigned char flags;	/* Flags describing cache properties */
};

/*
 * Flag definitions
 */
#define MIPS_CACHE_NOT_PRESENT	0x00000001
#define MIPS_CACHE_VTAG		0x00000002	/* Virtually tagged cache */
#define MIPS_CACHE_ALIASES	0x00000004	/* Cache could have aliases */
#define MIPS_CACHE_IC_F_DC	0x00000008	/* Ic can refill from D-cache */
#define MIPS_IC_SNOOPS_REMOTE	0x00000010	/* Ic snoops remote stores */
#define MIPS_CACHE_PINDEX	0x00000020	/* Physically indexed cache */

struct cpuinfo_mips {
	unsigned long		asid_cache;

	/*
	 * Capability and feature descriptor structure for MIPS CPU
	 */
	unsigned long		ases;
	unsigned long long	options;
	unsigned int		udelay_val;
	unsigned int		processor_id;
	unsigned int		fpu_id;
	unsigned int		fpu_csr31;
	unsigned int		fpu_msk31;
	unsigned int		msa_id;
	unsigned int		cputype;
	int			isa_level;
	int			tlbsize;
	int			tlbsizevtlb;
	int			tlbsizeftlbsets;
	int			tlbsizeftlbways;
	struct cache_desc	icache; /* Primary I-cache */
	struct cache_desc	dcache; /* Primary D or combined I/D cache */
	struct cache_desc	scache; /* Secondary cache */
	struct cache_desc	tcache; /* Tertiary/split secondary cache */
	int			srsets; /* Shadow register sets */
	int			package;/* physical package number */
	int			core;	/* physical core number */
#ifdef CONFIG_64BIT
	int			vmbits; /* Virtual memory size in bits */
#endif
#ifdef CONFIG_MIPS_MT_SMP
	/*
	 * There is not necessarily a 1:1 mapping of VPE num to CPU number
	 * in particular on multi-core systems.
	 */
	int			vpe_id;	 /* Virtual Processor number */
#endif
	void			*data;	/* Additional data */
	unsigned int		watch_reg_count;   /* Number that exist */
	unsigned int		watch_reg_use_cnt; /* Usable by ptrace */
#define NUM_WATCH_REGS 4
	u16			watch_reg_masks[NUM_WATCH_REGS];
	unsigned int		kscratch_mask; /* Usable KScratch mask. */
	/*
	 * Cache Coherency attribute for write-combine memory writes.
	 * (shifted by _CACHE_SHIFT)
	 */
	unsigned int		writecombine;
	/*
	 * Simple counter to prevent enabling HTW in nested
	 * htw_start/htw_stop calls
	 */
	unsigned int		htw_seq;
} __attribute__((aligned(SMP_CACHE_BYTES)));

extern struct cpuinfo_mips cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]
#define raw_current_cpu_data cpu_data[raw_smp_processor_id()]
#define boot_cpu_data cpu_data[0]

extern void cpu_probe(void);
extern void cpu_report(void);

extern const char *__cpu_name[];
#define cpu_name_string()	__cpu_name[smp_processor_id()]

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

#ifdef CONFIG_MIPS_MT_SMP
# define cpu_vpe_id(cpuinfo)	((cpuinfo)->vpe_id)
#else
# define cpu_vpe_id(cpuinfo)	({ (void)cpuinfo; 0; })
#endif

#endif /* __ASM_CPU_INFO_H */
