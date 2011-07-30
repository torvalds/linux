#ifndef _ASM_X86_CPU_DEBUG_H
#define _ASM_X86_CPU_DEBUG_H

/*
 * CPU x86 architecture debug
 *
 * Copyright(C) 2009 Jaswinder Singh Rajput
 */

/* Register flags */
enum cpu_debug_bit {
/* Model Specific Registers (MSRs)					*/
	CPU_MC_BIT,				/* Machine Check	*/
	CPU_MONITOR_BIT,			/* Monitor		*/
	CPU_TIME_BIT,				/* Time			*/
	CPU_PMC_BIT,				/* Performance Monitor	*/
	CPU_PLATFORM_BIT,			/* Platform		*/
	CPU_APIC_BIT,				/* APIC			*/
	CPU_POWERON_BIT,			/* Power-on		*/
	CPU_CONTROL_BIT,			/* Control		*/
	CPU_FEATURES_BIT,			/* Features control	*/
	CPU_LBRANCH_BIT,			/* Last Branch		*/
	CPU_BIOS_BIT,				/* BIOS			*/
	CPU_FREQ_BIT,				/* Frequency		*/
	CPU_MTTR_BIT,				/* MTRR			*/
	CPU_PERF_BIT,				/* Performance		*/
	CPU_CACHE_BIT,				/* Cache		*/
	CPU_SYSENTER_BIT,			/* Sysenter		*/
	CPU_THERM_BIT,				/* Thermal		*/
	CPU_MISC_BIT,				/* Miscellaneous	*/
	CPU_DEBUG_BIT,				/* Debug		*/
	CPU_PAT_BIT,				/* PAT			*/
	CPU_VMX_BIT,				/* VMX			*/
	CPU_CALL_BIT,				/* System Call		*/
	CPU_BASE_BIT,				/* BASE Address		*/
	CPU_VER_BIT,				/* Version ID		*/
	CPU_CONF_BIT,				/* Configuration	*/
	CPU_SMM_BIT,				/* System mgmt mode	*/
	CPU_SVM_BIT,				/*Secure Virtual Machine*/
	CPU_OSVM_BIT,				/* OS-Visible Workaround*/
/* Standard Registers							*/
	CPU_TSS_BIT,				/* Task Stack Segment	*/
	CPU_CR_BIT,				/* Control Registers	*/
	CPU_DT_BIT,				/* Descriptor Table	*/
/* End of Registers flags						*/
	CPU_REG_ALL_BIT,			/* Select all Registers	*/
};

#define	CPU_REG_ALL		(~0)		/* Select all Registers	*/

#define	CPU_MC			(1 << CPU_MC_BIT)
#define	CPU_MONITOR		(1 << CPU_MONITOR_BIT)
#define	CPU_TIME		(1 << CPU_TIME_BIT)
#define	CPU_PMC			(1 << CPU_PMC_BIT)
#define	CPU_PLATFORM		(1 << CPU_PLATFORM_BIT)
#define	CPU_APIC		(1 << CPU_APIC_BIT)
#define	CPU_POWERON		(1 << CPU_POWERON_BIT)
#define	CPU_CONTROL		(1 << CPU_CONTROL_BIT)
#define	CPU_FEATURES		(1 << CPU_FEATURES_BIT)
#define	CPU_LBRANCH		(1 << CPU_LBRANCH_BIT)
#define	CPU_BIOS		(1 << CPU_BIOS_BIT)
#define	CPU_FREQ		(1 << CPU_FREQ_BIT)
#define	CPU_MTRR		(1 << CPU_MTTR_BIT)
#define	CPU_PERF		(1 << CPU_PERF_BIT)
#define	CPU_CACHE		(1 << CPU_CACHE_BIT)
#define	CPU_SYSENTER		(1 << CPU_SYSENTER_BIT)
#define	CPU_THERM		(1 << CPU_THERM_BIT)
#define	CPU_MISC		(1 << CPU_MISC_BIT)
#define	CPU_DEBUG		(1 << CPU_DEBUG_BIT)
#define	CPU_PAT			(1 << CPU_PAT_BIT)
#define	CPU_VMX			(1 << CPU_VMX_BIT)
#define	CPU_CALL		(1 << CPU_CALL_BIT)
#define	CPU_BASE		(1 << CPU_BASE_BIT)
#define	CPU_VER			(1 << CPU_VER_BIT)
#define	CPU_CONF		(1 << CPU_CONF_BIT)
#define	CPU_SMM			(1 << CPU_SMM_BIT)
#define	CPU_SVM			(1 << CPU_SVM_BIT)
#define	CPU_OSVM		(1 << CPU_OSVM_BIT)
#define	CPU_TSS			(1 << CPU_TSS_BIT)
#define	CPU_CR			(1 << CPU_CR_BIT)
#define	CPU_DT			(1 << CPU_DT_BIT)

/* Register file flags */
enum cpu_file_bit {
	CPU_INDEX_BIT,				/* index		*/
	CPU_VALUE_BIT,				/* value		*/
};

#define	CPU_FILE_VALUE		(1 << CPU_VALUE_BIT)

#define MAX_CPU_FILES		512

struct cpu_private {
	unsigned		cpu;
	unsigned		type;
	unsigned		reg;
	unsigned		file;
};

struct cpu_debug_base {
	char			*name;		/* Register name	*/
	unsigned		flag;		/* Register flag	*/
	unsigned		write;		/* Register write flag	*/
};

/*
 * Currently it looks similar to cpu_debug_base but once we add more files
 * cpu_file_base will go in different direction
 */
struct cpu_file_base {
	char			*name;		/* Register file name	*/
	unsigned		flag;		/* Register file flag	*/
	unsigned		write;		/* Register write flag	*/
};

struct cpu_cpuX_base {
	struct dentry		*dentry;	/* Register dentry	*/
	int			init;		/* Register index file	*/
};

struct cpu_debug_range {
	unsigned		min;		/* Register range min	*/
	unsigned		max;		/* Register range max	*/
	unsigned		flag;		/* Supported flags	*/
};

#endif /* _ASM_X86_CPU_DEBUG_H */
