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

#define	CPU_FILE_VALUE			(1 << CPU_VALUE_BIT)

/*
 * DisplayFamily_DisplayModel	Processor Families/Processor Number Series
 * --------------------------	------------------------------------------
 * 05_01, 05_02, 05_04		Pentium, Pentium with MMX
 *
 * 06_01			Pentium Pro
 * 06_03, 06_05			Pentium II Xeon, Pentium II
 * 06_07, 06_08, 06_0A, 06_0B	Pentium III Xeon, Pentum III
 *
 * 06_09, 060D			Pentium M
 *
 * 06_0E			Core Duo, Core Solo
 *
 * 06_0F			Xeon 3000, 3200, 5100, 5300, 7300 series,
 *				Core 2 Quad, Core 2 Extreme, Core 2 Duo,
 *				Pentium dual-core
 * 06_17			Xeon 5200, 5400 series, Core 2 Quad Q9650
 *
 * 06_1C			Atom
 *
 * 0F_00, 0F_01, 0F_02		Xeon, Xeon MP, Pentium 4
 * 0F_03, 0F_04			Xeon, Xeon MP, Pentium 4, Pentium D
 *
 * 0F_06			Xeon 7100, 5000 Series, Xeon MP,
 *				Pentium 4, Pentium D
 */

/* Register processors bits */
enum cpu_processor_bit {
	CPU_NONE,
/* Intel */
	CPU_INTEL_PENTIUM_BIT,
	CPU_INTEL_P6_BIT,
	CPU_INTEL_PENTIUM_M_BIT,
	CPU_INTEL_CORE_BIT,
	CPU_INTEL_CORE2_BIT,
	CPU_INTEL_ATOM_BIT,
	CPU_INTEL_XEON_P4_BIT,
	CPU_INTEL_XEON_MP_BIT,
/* AMD */
	CPU_AMD_K6_BIT,
	CPU_AMD_K7_BIT,
	CPU_AMD_K8_BIT,
	CPU_AMD_0F_BIT,
	CPU_AMD_10_BIT,
	CPU_AMD_11_BIT,
};

#define	CPU_INTEL_PENTIUM	(1 << CPU_INTEL_PENTIUM_BIT)
#define	CPU_INTEL_P6		(1 << CPU_INTEL_P6_BIT)
#define	CPU_INTEL_PENTIUM_M	(1 << CPU_INTEL_PENTIUM_M_BIT)
#define	CPU_INTEL_CORE		(1 << CPU_INTEL_CORE_BIT)
#define	CPU_INTEL_CORE2		(1 << CPU_INTEL_CORE2_BIT)
#define	CPU_INTEL_ATOM		(1 << CPU_INTEL_ATOM_BIT)
#define	CPU_INTEL_XEON_P4	(1 << CPU_INTEL_XEON_P4_BIT)
#define	CPU_INTEL_XEON_MP	(1 << CPU_INTEL_XEON_MP_BIT)

#define	CPU_INTEL_PX		(CPU_INTEL_P6 | CPU_INTEL_PENTIUM_M)
#define	CPU_INTEL_COREX		(CPU_INTEL_CORE | CPU_INTEL_CORE2)
#define	CPU_INTEL_XEON		(CPU_INTEL_XEON_P4 | CPU_INTEL_XEON_MP)
#define	CPU_CO_AT		(CPU_INTEL_CORE | CPU_INTEL_ATOM)
#define	CPU_C2_AT		(CPU_INTEL_CORE2 | CPU_INTEL_ATOM)
#define	CPU_CX_AT		(CPU_INTEL_COREX | CPU_INTEL_ATOM)
#define	CPU_CX_XE		(CPU_INTEL_COREX | CPU_INTEL_XEON)
#define	CPU_P6_XE		(CPU_INTEL_P6 | CPU_INTEL_XEON)
#define	CPU_PM_CO_AT		(CPU_INTEL_PENTIUM_M | CPU_CO_AT)
#define	CPU_C2_AT_XE		(CPU_C2_AT | CPU_INTEL_XEON)
#define	CPU_CX_AT_XE		(CPU_CX_AT | CPU_INTEL_XEON)
#define	CPU_P6_CX_AT		(CPU_INTEL_P6 | CPU_CX_AT)
#define	CPU_P6_CX_XE		(CPU_P6_XE | CPU_INTEL_COREX)
#define	CPU_P6_CX_AT_XE		(CPU_INTEL_P6 | CPU_CX_AT_XE)
#define	CPU_PM_CX_AT_XE		(CPU_INTEL_PENTIUM_M | CPU_CX_AT_XE)
#define	CPU_PM_CX_AT		(CPU_INTEL_PENTIUM_M | CPU_CX_AT)
#define	CPU_PM_CX_XE		(CPU_INTEL_PENTIUM_M | CPU_CX_XE)
#define	CPU_PX_CX_AT		(CPU_INTEL_PX | CPU_CX_AT)
#define	CPU_PX_CX_AT_XE		(CPU_INTEL_PX | CPU_CX_AT_XE)

/* Select all supported Intel CPUs */
#define	CPU_INTEL_ALL		(CPU_INTEL_PENTIUM | CPU_PX_CX_AT_XE)

#define	CPU_AMD_K6		(1 << CPU_AMD_K6_BIT)
#define	CPU_AMD_K7		(1 << CPU_AMD_K7_BIT)
#define	CPU_AMD_K8		(1 << CPU_AMD_K8_BIT)
#define	CPU_AMD_0F		(1 << CPU_AMD_0F_BIT)
#define	CPU_AMD_10		(1 << CPU_AMD_10_BIT)
#define	CPU_AMD_11		(1 << CPU_AMD_11_BIT)

#define	CPU_K10_PLUS		(CPU_AMD_10 | CPU_AMD_11)
#define	CPU_K0F_PLUS		(CPU_AMD_0F | CPU_K10_PLUS)
#define	CPU_K8_PLUS		(CPU_AMD_K8 | CPU_K0F_PLUS)
#define	CPU_K7_PLUS		(CPU_AMD_K7 | CPU_K8_PLUS)

/* Select all supported AMD CPUs */
#define	CPU_AMD_ALL		(CPU_AMD_K6 | CPU_K7_PLUS)

/* Select all supported CPUs */
#define	CPU_ALL			(CPU_INTEL_ALL | CPU_AMD_ALL)

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
	unsigned		model;		/* Supported models	*/
};

#endif /* _ASM_X86_CPU_DEBUG_H */
