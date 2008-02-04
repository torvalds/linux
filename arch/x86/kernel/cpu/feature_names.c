/*
 * Strings for the various x86 capability flags.
 *
 * This file must not contain any executable code.
 */

#include "asm/cpufeature.h"

/*
 * These flag bits must match the definitions in <asm/cpufeature.h>.
 * NULL means this bit is undefined or reserved; either way it doesn't
 * have meaning as far as Linux is concerned.  Note that it's important
 * to realize there is a difference between this table and CPUID -- if
 * applications want to get the raw CPUID data, they should access
 * /dev/cpu/<cpu_nr>/cpuid instead.
 */
const char * const x86_cap_flags[NCAPINTS*32] = {
	/* Intel-defined */
	"fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
	"cx8", "apic", NULL, "sep", "mtrr", "pge", "mca", "cmov",
	"pat", "pse36", "pn", "clflush", NULL, "dts", "acpi", "mmx",
	"fxsr", "sse", "sse2", "ss", "ht", "tm", "ia64", "pbe",

	/* AMD-defined */
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, "syscall", NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, "mp", "nx", NULL, "mmxext", NULL,
	NULL, "fxsr_opt", "pdpe1gb", "rdtscp", NULL, "lm",
	"3dnowext", "3dnow",

	/* Transmeta-defined */
	"recovery", "longrun", NULL, "lrti", NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

	/* Other (Linux-defined) */
	"cxmmx", "k6_mtrr", "cyrix_arr", "centaur_mcr",
	NULL, NULL, NULL, NULL,
	"constant_tsc", "up", NULL, "arch_perfmon",
	"pebs", "bts", NULL, NULL,
	"rep_good", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

	/* Intel-defined (#2) */
	"pni", NULL, NULL, "monitor", "ds_cpl", "vmx", "smx", "est",
	"tm2", "ssse3", "cid", NULL, NULL, "cx16", "xtpr", NULL,
	NULL, NULL, "dca", "sse4_1", "sse4_2", NULL, NULL, "popcnt",
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

	/* VIA/Cyrix/Centaur-defined */
	NULL, NULL, "rng", "rng_en", NULL, NULL, "ace", "ace_en",
	"ace2", "ace2_en", "phe", "phe_en", "pmm", "pmm_en", NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

	/* AMD-defined (#2) */
	"lahf_lm", "cmp_legacy", "svm", "extapic",
	"cr8_legacy", "abm", "sse4a", "misalignsse",
	"3dnowprefetch", "osvw", "ibs", "sse5",
	"skinit", "wdt", NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

	/* Auxiliary (Linux-defined) */
	"ida", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

const char *const x86_power_flags[32] = {
	"ts",	/* temperature sensor */
	"fid",  /* frequency id control */
	"vid",  /* voltage id control */
	"ttp",  /* thermal trip */
	"tm",
	"stc",
	"100mhzsteps",
	"hwpstate",
	"",	/* tsc invariant mapped to constant_tsc */
		/* nothing */
};
