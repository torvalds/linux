#ifndef __ASM_MACH_BCM963XX_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_BCM963XX_CPU_FEATURE_OVERRIDES_H

#include <bcm63xx_cpu.h>

#define cpu_has_tlb			1
#define cpu_has_4kex			1
#define cpu_has_4k_cache		1
#define cpu_has_fpu			0
#define cpu_has_32fpr			0
#define cpu_has_counter			1
#define cpu_has_watch			0
#define cpu_has_divec			1
#define cpu_has_vce			0
#define cpu_has_cache_cdex_p		0
#define cpu_has_cache_cdex_s		0
#define cpu_has_prefetch		1
#define cpu_has_mcheck			1
#define cpu_has_ejtag			1
#define cpu_has_llsc			1
#define cpu_has_mips16			0
#define cpu_has_mdmx			0
#define cpu_has_mips3d			0
#define cpu_has_smartmips		0
#define cpu_has_vtag_icache		0

#if !defined(BCMCPU_RUNTIME_DETECT) && (defined(CONFIG_BCMCPU_IS_6348) || defined(CONFIG_CPU_IS_6338) || defined(CONFIG_CPU_IS_BCM6345))
#define cpu_has_dc_aliases		0
#endif

#define cpu_has_ic_fills_f_dc		0
#define cpu_has_pindexed_dcache		0

#define cpu_has_mips32r1		1
#define cpu_has_mips32r2		0
#define cpu_has_mips64r1		0
#define cpu_has_mips64r2		0

#define cpu_has_dsp			0
#define cpu_has_mipsmt			0
#define cpu_has_userlocal		0

#define cpu_has_nofpuex			0
#define cpu_has_64bits			0
#define cpu_has_64bit_zero_reg		0

#define cpu_dcache_line_size()		16
#define cpu_icache_line_size()		16
#define cpu_scache_line_size()		0

#endif /* __ASM_MACH_BCM963XX_CPU_FEATURE_OVERRIDES_H */
