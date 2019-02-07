/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef __ASM_MACH_AU1X00_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_AU1X00_CPU_FEATURE_OVERRIDES_H

#define cpu_has_tlb			1
#define cpu_has_ftlb			0
#define cpu_has_tlbinv			0
#define cpu_has_segments		0
#define cpu_has_eva			0
#define cpu_has_htw			0
#define cpu_has_ldpte			0
#define cpu_has_rixiex			0
#define cpu_has_maar			0
#define cpu_has_rw_llb			0
#define cpu_has_3kex			0
#define cpu_has_4kex			1
#define cpu_has_3k_cache		0
#define cpu_has_4k_cache		1
#define cpu_has_tx39_cache		0
#define cpu_has_fpu			0
#define cpu_has_32fpr			0
#define cpu_has_counter			1
#define cpu_has_watch			1
#define cpu_has_divec			1
#define cpu_has_vce			0
#define cpu_has_cache_cdex_p		0
#define cpu_has_cache_cdex_s		0
#define cpu_has_prefetch		1
#define cpu_has_mcheck			1
#define cpu_has_ejtag			1
#define cpu_has_llsc			1
#define cpu_has_guestctl0ext		0
#define cpu_has_guestctl1		0
#define cpu_has_guestctl2		0
#define cpu_has_guestid			0
#define cpu_has_drg			0
#define cpu_has_bp_ghist		0
#define cpu_has_mips16			0
#define cpu_has_mips16e2		0
#define cpu_has_mdmx			0
#define cpu_has_mips3d			0
#define cpu_has_smartmips		0
#define cpu_has_rixi			0
#define cpu_has_mmips			0
#define cpu_has_lpa			0
#define cpu_has_mhv			0
#define cpu_has_vtag_icache		0
#define cpu_has_dc_aliases		0
#define cpu_has_ic_fills_f_dc		1
#define cpu_has_pindexed_dcache		0
#define cpu_has_mips32r1		1
#define cpu_has_mips32r2		0
#define cpu_has_mips32r6		0
#define cpu_has_mips64r1		0
#define cpu_has_mips64r2		0
#define cpu_has_mips64r6		0
#define cpu_has_dsp			0
#define cpu_has_dsp2			0
#define cpu_has_dsp3			0
#define cpu_has_mipsmt			0
#define cpu_has_vp			0
#define cpu_has_userlocal		0
#define cpu_has_nofpuex			0
#define cpu_has_64bits			0
#define cpu_has_64bit_zero_reg		0
#define cpu_has_vint			0
#define cpu_has_veic			0
#define cpu_has_inclusive_pcaches	0

#define cpu_dcache_line_size()		32
#define cpu_icache_line_size()		32
#define cpu_scache_line_size()		0
#define cpu_tcache_line_size()		0

#define cpu_has_perf_cntr_intr_bit	0
#define cpu_has_vz			0
#define cpu_has_msa			0
#define cpu_has_ufr			0
#define cpu_has_fre			0
#define cpu_has_cdmm			0
#define cpu_has_small_pages		0
#define cpu_has_nan_legacy		1
#define cpu_has_nan_2008		1
#define cpu_has_ebase_wg		0
#define cpu_has_badinstr		0
#define cpu_has_badinstrp		0
#define cpu_has_contextconfig		0
#define cpu_has_perf			0

#endif /* __ASM_MACH_AU1X00_CPU_FEATURE_OVERRIDES_H */
