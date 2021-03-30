/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 2004 Ralf Baechle
 * Copyright (C) 2004  Maciej W. Rozycki
 */
#ifndef __ASM_CPU_FEATURES_H
#define __ASM_CPU_FEATURES_H

#include <asm/cpu.h>
#include <asm/cpu-info.h>
#include <asm/isa-rev.h>
#include <cpu-feature-overrides.h>

#define __ase(ase)			(cpu_data[0].ases & (ase))
#define __isa(isa)			(cpu_data[0].isa_level & (isa))
#define __opt(opt)			(cpu_data[0].options & (opt))

/*
 * Check if MIPS_ISA_REV is >= isa *and* an option or ASE is detected during
 * boot (typically by cpu_probe()).
 *
 * Note that these should only be used in cases where a kernel built for an
 * older ISA *cannot* run on a CPU which supports the feature in question. For
 * example this may be used for features introduced with MIPSr6, since a kernel
 * built for an older ISA cannot run on a MIPSr6 CPU. This should not be used
 * for MIPSr2 features however, since a MIPSr1 or earlier kernel might run on a
 * MIPSr2 CPU.
 */
#define __isa_ge_and_ase(isa, ase)	((MIPS_ISA_REV >= (isa)) && __ase(ase))
#define __isa_ge_and_opt(isa, opt)	((MIPS_ISA_REV >= (isa)) && __opt(opt))

/*
 * Check if MIPS_ISA_REV is >= isa *or* an option or ASE is detected during
 * boot (typically by cpu_probe()).
 *
 * These are for use with features that are optional up until a particular ISA
 * revision & then become required.
 */
#define __isa_ge_or_ase(isa, ase)	((MIPS_ISA_REV >= (isa)) || __ase(ase))
#define __isa_ge_or_opt(isa, opt)	((MIPS_ISA_REV >= (isa)) || __opt(opt))

/*
 * Check if MIPS_ISA_REV is < isa *and* an option or ASE is detected during
 * boot (typically by cpu_probe()).
 *
 * These are for use with features that are optional up until a particular ISA
 * revision & are then removed - ie. no longer present in any CPU implementing
 * the given ISA revision.
 */
#define __isa_lt_and_ase(isa, ase)	((MIPS_ISA_REV < (isa)) && __ase(ase))
#define __isa_lt_and_opt(isa, opt)	((MIPS_ISA_REV < (isa)) && __opt(opt))

/*
 * Similarly allow for ISA level checks that take into account knowledge of the
 * ISA targeted by the kernel build, provided by MIPS_ISA_REV.
 */
#define __isa_ge_and_flag(isa, flag)	((MIPS_ISA_REV >= (isa)) && __isa(flag))
#define __isa_ge_or_flag(isa, flag)	((MIPS_ISA_REV >= (isa)) || __isa(flag))
#define __isa_lt_and_flag(isa, flag)	((MIPS_ISA_REV < (isa)) && __isa(flag))
#define __isa_range(ge, lt) \
	((MIPS_ISA_REV >= (ge)) && (MIPS_ISA_REV < (lt)))
#define __isa_range_or_flag(ge, lt, flag) \
	(__isa_range(ge, lt) || ((MIPS_ISA_REV < (lt)) && __isa(flag)))

/*
 * SMP assumption: Options of CPU 0 are a superset of all processors.
 * This is true for all known MIPS systems.
 */
#ifndef cpu_has_tlb
#define cpu_has_tlb		__opt(MIPS_CPU_TLB)
#endif
#ifndef cpu_has_ftlb
#define cpu_has_ftlb		__opt(MIPS_CPU_FTLB)
#endif
#ifndef cpu_has_tlbinv
#define cpu_has_tlbinv		__opt(MIPS_CPU_TLBINV)
#endif
#ifndef cpu_has_segments
#define cpu_has_segments	__opt(MIPS_CPU_SEGMENTS)
#endif
#ifndef cpu_has_eva
#define cpu_has_eva		__opt(MIPS_CPU_EVA)
#endif
#ifndef cpu_has_htw
#define cpu_has_htw		__opt(MIPS_CPU_HTW)
#endif
#ifndef cpu_has_ldpte
#define cpu_has_ldpte		__opt(MIPS_CPU_LDPTE)
#endif
#ifndef cpu_has_rixiex
#define cpu_has_rixiex		__isa_ge_or_opt(6, MIPS_CPU_RIXIEX)
#endif
#ifndef cpu_has_maar
#define cpu_has_maar		__opt(MIPS_CPU_MAAR)
#endif
#ifndef cpu_has_rw_llb
#define cpu_has_rw_llb		__isa_ge_or_opt(6, MIPS_CPU_RW_LLB)
#endif

/*
 * For the moment we don't consider R6000 and R8000 so we can assume that
 * anything that doesn't support R4000-style exceptions and interrupts is
 * R3000-like.  Users should still treat these two macro definitions as
 * opaque.
 */
#ifndef cpu_has_3kex
#define cpu_has_3kex		(!cpu_has_4kex)
#endif
#ifndef cpu_has_4kex
#define cpu_has_4kex		__isa_ge_or_opt(1, MIPS_CPU_4KEX)
#endif
#ifndef cpu_has_3k_cache
#define cpu_has_3k_cache	__isa_lt_and_opt(1, MIPS_CPU_3K_CACHE)
#endif
#ifndef cpu_has_4k_cache
#define cpu_has_4k_cache	__isa_ge_or_opt(1, MIPS_CPU_4K_CACHE)
#endif
#ifndef cpu_has_tx39_cache
#define cpu_has_tx39_cache	__opt(MIPS_CPU_TX39_CACHE)
#endif
#ifndef cpu_has_octeon_cache
#define cpu_has_octeon_cache	0
#endif
/* Don't override `cpu_has_fpu' to 1 or the "nofpu" option won't work.  */
#ifndef cpu_has_fpu
# ifdef CONFIG_MIPS_FP_SUPPORT
#  define cpu_has_fpu		(current_cpu_data.options & MIPS_CPU_FPU)
#  define raw_cpu_has_fpu	(raw_current_cpu_data.options & MIPS_CPU_FPU)
# else
#  define cpu_has_fpu		0
#  define raw_cpu_has_fpu	0
# endif
#else
# define raw_cpu_has_fpu	cpu_has_fpu
#endif
#ifndef cpu_has_32fpr
#define cpu_has_32fpr		__isa_ge_or_opt(1, MIPS_CPU_32FPR)
#endif
#ifndef cpu_has_counter
#define cpu_has_counter		__opt(MIPS_CPU_COUNTER)
#endif
#ifndef cpu_has_watch
#define cpu_has_watch		__opt(MIPS_CPU_WATCH)
#endif
#ifndef cpu_has_divec
#define cpu_has_divec		__isa_ge_or_opt(1, MIPS_CPU_DIVEC)
#endif
#ifndef cpu_has_vce
#define cpu_has_vce		__opt(MIPS_CPU_VCE)
#endif
#ifndef cpu_has_cache_cdex_p
#define cpu_has_cache_cdex_p	__opt(MIPS_CPU_CACHE_CDEX_P)
#endif
#ifndef cpu_has_cache_cdex_s
#define cpu_has_cache_cdex_s	__opt(MIPS_CPU_CACHE_CDEX_S)
#endif
#ifndef cpu_has_prefetch
#define cpu_has_prefetch	__isa_ge_or_opt(1, MIPS_CPU_PREFETCH)
#endif
#ifndef cpu_has_mcheck
#define cpu_has_mcheck		__isa_ge_or_opt(1, MIPS_CPU_MCHECK)
#endif
#ifndef cpu_has_ejtag
#define cpu_has_ejtag		__opt(MIPS_CPU_EJTAG)
#endif
#ifndef cpu_has_llsc
#define cpu_has_llsc		__isa_ge_or_opt(1, MIPS_CPU_LLSC)
#endif
#ifndef kernel_uses_llsc
#define kernel_uses_llsc	cpu_has_llsc
#endif
#ifndef cpu_has_guestctl0ext
#define cpu_has_guestctl0ext	__opt(MIPS_CPU_GUESTCTL0EXT)
#endif
#ifndef cpu_has_guestctl1
#define cpu_has_guestctl1	__opt(MIPS_CPU_GUESTCTL1)
#endif
#ifndef cpu_has_guestctl2
#define cpu_has_guestctl2	__opt(MIPS_CPU_GUESTCTL2)
#endif
#ifndef cpu_has_guestid
#define cpu_has_guestid		__opt(MIPS_CPU_GUESTID)
#endif
#ifndef cpu_has_drg
#define cpu_has_drg		__opt(MIPS_CPU_DRG)
#endif
#ifndef cpu_has_mips16
#define cpu_has_mips16		__isa_lt_and_ase(6, MIPS_ASE_MIPS16)
#endif
#ifndef cpu_has_mips16e2
#define cpu_has_mips16e2	__isa_lt_and_ase(6, MIPS_ASE_MIPS16E2)
#endif
#ifndef cpu_has_mdmx
#define cpu_has_mdmx		__isa_lt_and_ase(6, MIPS_ASE_MDMX)
#endif
#ifndef cpu_has_mips3d
#define cpu_has_mips3d		__isa_lt_and_ase(6, MIPS_ASE_MIPS3D)
#endif
#ifndef cpu_has_smartmips
#define cpu_has_smartmips	__isa_lt_and_ase(6, MIPS_ASE_SMARTMIPS)
#endif

#ifndef cpu_has_rixi
#define cpu_has_rixi		__isa_ge_or_opt(6, MIPS_CPU_RIXI)
#endif

#ifndef cpu_has_mmips
# if defined(__mips_micromips)
#  define cpu_has_mmips		1
# elif defined(CONFIG_SYS_SUPPORTS_MICROMIPS)
#  define cpu_has_mmips		__opt(MIPS_CPU_MICROMIPS)
# else
#  define cpu_has_mmips		0
# endif
#endif

#ifndef cpu_has_lpa
#define cpu_has_lpa		__opt(MIPS_CPU_LPA)
#endif
#ifndef cpu_has_mvh
#define cpu_has_mvh		__opt(MIPS_CPU_MVH)
#endif
#ifndef cpu_has_xpa
#define cpu_has_xpa		(cpu_has_lpa && cpu_has_mvh)
#endif
#ifndef cpu_has_vtag_icache
#define cpu_has_vtag_icache	(cpu_data[0].icache.flags & MIPS_CACHE_VTAG)
#endif
#ifndef cpu_has_dc_aliases
#define cpu_has_dc_aliases	(cpu_data[0].dcache.flags & MIPS_CACHE_ALIASES)
#endif
#ifndef cpu_has_ic_fills_f_dc
#define cpu_has_ic_fills_f_dc	(cpu_data[0].icache.flags & MIPS_CACHE_IC_F_DC)
#endif
#ifndef cpu_has_pindexed_dcache
#define cpu_has_pindexed_dcache	(cpu_data[0].dcache.flags & MIPS_CACHE_PINDEX)
#endif

/*
 * I-Cache snoops remote store.	 This only matters on SMP.  Some multiprocessors
 * such as the R10000 have I-Caches that snoop local stores; the embedded ones
 * don't.  For maintaining I-cache coherency this means we need to flush the
 * D-cache all the way back to whever the I-cache does refills from, so the
 * I-cache has a chance to see the new data at all.  Then we have to flush the
 * I-cache also.
 * Note we may have been rescheduled and may no longer be running on the CPU
 * that did the store so we can't optimize this into only doing the flush on
 * the local CPU.
 */
#ifndef cpu_icache_snoops_remote_store
#ifdef CONFIG_SMP
#define cpu_icache_snoops_remote_store	(cpu_data[0].icache.flags & MIPS_IC_SNOOPS_REMOTE)
#else
#define cpu_icache_snoops_remote_store	1
#endif
#endif

#ifndef cpu_has_mips_1
# define cpu_has_mips_1		(MIPS_ISA_REV < 6)
#endif
#ifndef cpu_has_mips_2
# define cpu_has_mips_2		__isa_lt_and_flag(6, MIPS_CPU_ISA_II)
#endif
#ifndef cpu_has_mips_3
# define cpu_has_mips_3		__isa_lt_and_flag(6, MIPS_CPU_ISA_III)
#endif
#ifndef cpu_has_mips_4
# define cpu_has_mips_4		__isa_lt_and_flag(6, MIPS_CPU_ISA_IV)
#endif
#ifndef cpu_has_mips_5
# define cpu_has_mips_5		__isa_lt_and_flag(6, MIPS_CPU_ISA_V)
#endif
#ifndef cpu_has_mips32r1
# define cpu_has_mips32r1	__isa_range_or_flag(1, 6, MIPS_CPU_ISA_M32R1)
#endif
#ifndef cpu_has_mips32r2
# define cpu_has_mips32r2	__isa_range_or_flag(2, 6, MIPS_CPU_ISA_M32R2)
#endif
#ifndef cpu_has_mips32r5
# define cpu_has_mips32r5	__isa_range_or_flag(5, 6, MIPS_CPU_ISA_M32R5)
#endif
#ifndef cpu_has_mips32r6
# define cpu_has_mips32r6	__isa_ge_or_flag(6, MIPS_CPU_ISA_M32R6)
#endif
#ifndef cpu_has_mips64r1
# define cpu_has_mips64r1	(cpu_has_64bits && \
				 __isa_range_or_flag(1, 6, MIPS_CPU_ISA_M64R1))
#endif
#ifndef cpu_has_mips64r2
# define cpu_has_mips64r2	(cpu_has_64bits && \
				 __isa_range_or_flag(2, 6, MIPS_CPU_ISA_M64R2))
#endif
#ifndef cpu_has_mips64r5
# define cpu_has_mips64r5	(cpu_has_64bits && \
				 __isa_range_or_flag(5, 6, MIPS_CPU_ISA_M64R5))
#endif
#ifndef cpu_has_mips64r6
# define cpu_has_mips64r6	__isa_ge_and_flag(6, MIPS_CPU_ISA_M64R6)
#endif

/*
 * Shortcuts ...
 */
#define cpu_has_mips_2_3_4_5	(cpu_has_mips_2 | cpu_has_mips_3_4_5)
#define cpu_has_mips_3_4_5	(cpu_has_mips_3 | cpu_has_mips_4_5)
#define cpu_has_mips_4_5	(cpu_has_mips_4 | cpu_has_mips_5)

#define cpu_has_mips_2_3_4_5_r	(cpu_has_mips_2 | cpu_has_mips_3_4_5_r)
#define cpu_has_mips_3_4_5_r	(cpu_has_mips_3 | cpu_has_mips_4_5_r)
#define cpu_has_mips_4_5_r	(cpu_has_mips_4 | cpu_has_mips_5_r)
#define cpu_has_mips_5_r	(cpu_has_mips_5 | cpu_has_mips_r)

#define cpu_has_mips_3_4_5_64_r2_r6					\
				(cpu_has_mips_3 | cpu_has_mips_4_5_64_r2_r6)
#define cpu_has_mips_4_5_64_r2_r6					\
				(cpu_has_mips_4_5 | cpu_has_mips64r1 |	\
				 cpu_has_mips_r2 | cpu_has_mips_r5 | \
				 cpu_has_mips_r6)

#define cpu_has_mips32	(cpu_has_mips32r1 | cpu_has_mips32r2 | \
			 cpu_has_mips32r5 | cpu_has_mips32r6)
#define cpu_has_mips64	(cpu_has_mips64r1 | cpu_has_mips64r2 | \
			 cpu_has_mips64r5 | cpu_has_mips64r6)
#define cpu_has_mips_r1 (cpu_has_mips32r1 | cpu_has_mips64r1)
#define cpu_has_mips_r2 (cpu_has_mips32r2 | cpu_has_mips64r2)
#define cpu_has_mips_r5	(cpu_has_mips32r5 | cpu_has_mips64r5)
#define cpu_has_mips_r6	(cpu_has_mips32r6 | cpu_has_mips64r6)
#define cpu_has_mips_r	(cpu_has_mips32r1 | cpu_has_mips32r2 | \
			 cpu_has_mips32r5 | cpu_has_mips32r6 | \
			 cpu_has_mips64r1 | cpu_has_mips64r2 | \
			 cpu_has_mips64r5 | cpu_has_mips64r6)

/* MIPSR2 - MIPSR6 have a lot of similarities */
#define cpu_has_mips_r2_r6	(cpu_has_mips_r2 | cpu_has_mips_r5 | \
				 cpu_has_mips_r6)

/*
 * cpu_has_mips_r2_exec_hazard - return if IHB is required on current processor
 *
 * Returns non-zero value if the current processor implementation requires
 * an IHB instruction to deal with an instruction hazard as per MIPS R2
 * architecture specification, zero otherwise.
 */
#ifndef cpu_has_mips_r2_exec_hazard
#define cpu_has_mips_r2_exec_hazard					\
({									\
	int __res;							\
									\
	switch (current_cpu_type()) {					\
	case CPU_M14KC:							\
	case CPU_74K:							\
	case CPU_1074K:							\
	case CPU_PROAPTIV:						\
	case CPU_P5600:							\
	case CPU_M5150:							\
	case CPU_QEMU_GENERIC:						\
	case CPU_CAVIUM_OCTEON:						\
	case CPU_CAVIUM_OCTEON_PLUS:					\
	case CPU_CAVIUM_OCTEON2:					\
	case CPU_CAVIUM_OCTEON3:					\
		__res = 0;						\
		break;							\
									\
	default:							\
		__res = 1;						\
	}								\
									\
	__res;								\
})
#endif

/*
 * MIPS32, MIPS64, VR5500, IDT32332, IDT32334 and maybe a few other
 * pre-MIPS32/MIPS64 processors have CLO, CLZ.	The IDT RC64574 is 64-bit and
 * has CLO and CLZ but not DCLO nor DCLZ.  For 64-bit kernels
 * cpu_has_clo_clz also indicates the availability of DCLO and DCLZ.
 */
#ifndef cpu_has_clo_clz
#define cpu_has_clo_clz	cpu_has_mips_r
#endif

/*
 * MIPS32 R2, MIPS64 R2, Loongson 3A and Octeon have WSBH.
 * MIPS64 R2, Loongson 3A and Octeon have WSBH, DSBH and DSHD.
 * This indicates the availability of WSBH and in case of 64 bit CPUs also
 * DSBH and DSHD.
 */
#ifndef cpu_has_wsbh
#define cpu_has_wsbh		cpu_has_mips_r2
#endif

#ifndef cpu_has_dsp
#define cpu_has_dsp		__ase(MIPS_ASE_DSP)
#endif

#ifndef cpu_has_dsp2
#define cpu_has_dsp2		__ase(MIPS_ASE_DSP2P)
#endif

#ifndef cpu_has_dsp3
#define cpu_has_dsp3		__ase(MIPS_ASE_DSP3)
#endif

#ifndef cpu_has_loongson_mmi
#define cpu_has_loongson_mmi		__ase(MIPS_ASE_LOONGSON_MMI)
#endif

#ifndef cpu_has_loongson_cam
#define cpu_has_loongson_cam		__ase(MIPS_ASE_LOONGSON_CAM)
#endif

#ifndef cpu_has_loongson_ext
#define cpu_has_loongson_ext		__ase(MIPS_ASE_LOONGSON_EXT)
#endif

#ifndef cpu_has_loongson_ext2
#define cpu_has_loongson_ext2		__ase(MIPS_ASE_LOONGSON_EXT2)
#endif

#ifndef cpu_has_mipsmt
#define cpu_has_mipsmt		__isa_lt_and_ase(6, MIPS_ASE_MIPSMT)
#endif

#ifndef cpu_has_vp
#define cpu_has_vp		__isa_ge_and_opt(6, MIPS_CPU_VP)
#endif

#ifndef cpu_has_userlocal
#define cpu_has_userlocal	__isa_ge_or_opt(6, MIPS_CPU_ULRI)
#endif

#ifdef CONFIG_32BIT
# ifndef cpu_has_nofpuex
# define cpu_has_nofpuex	__isa_lt_and_opt(1, MIPS_CPU_NOFPUEX)
# endif
# ifndef cpu_has_64bits
# define cpu_has_64bits		(cpu_data[0].isa_level & MIPS_CPU_ISA_64BIT)
# endif
# ifndef cpu_has_64bit_zero_reg
# define cpu_has_64bit_zero_reg	(cpu_data[0].isa_level & MIPS_CPU_ISA_64BIT)
# endif
# ifndef cpu_has_64bit_gp_regs
# define cpu_has_64bit_gp_regs		0
# endif
# ifndef cpu_vmbits
# define cpu_vmbits 31
# endif
#endif

#ifdef CONFIG_64BIT
# ifndef cpu_has_nofpuex
# define cpu_has_nofpuex		0
# endif
# ifndef cpu_has_64bits
# define cpu_has_64bits			1
# endif
# ifndef cpu_has_64bit_zero_reg
# define cpu_has_64bit_zero_reg		1
# endif
# ifndef cpu_has_64bit_gp_regs
# define cpu_has_64bit_gp_regs		1
# endif
# ifndef cpu_vmbits
# define cpu_vmbits cpu_data[0].vmbits
# define __NEED_VMBITS_PROBE
# endif
#endif

#if defined(CONFIG_CPU_MIPSR2_IRQ_VI) && !defined(cpu_has_vint)
# define cpu_has_vint		__opt(MIPS_CPU_VINT)
#elif !defined(cpu_has_vint)
# define cpu_has_vint			0
#endif

#if defined(CONFIG_CPU_MIPSR2_IRQ_EI) && !defined(cpu_has_veic)
# define cpu_has_veic		__opt(MIPS_CPU_VEIC)
#elif !defined(cpu_has_veic)
# define cpu_has_veic			0
#endif

#ifndef cpu_has_inclusive_pcaches
#define cpu_has_inclusive_pcaches	__opt(MIPS_CPU_INCLUSIVE_CACHES)
#endif

#ifndef cpu_dcache_line_size
#define cpu_dcache_line_size()	cpu_data[0].dcache.linesz
#endif
#ifndef cpu_icache_line_size
#define cpu_icache_line_size()	cpu_data[0].icache.linesz
#endif
#ifndef cpu_scache_line_size
#define cpu_scache_line_size()	cpu_data[0].scache.linesz
#endif
#ifndef cpu_tcache_line_size
#define cpu_tcache_line_size()	cpu_data[0].tcache.linesz
#endif

#ifndef cpu_hwrena_impl_bits
#define cpu_hwrena_impl_bits		0
#endif

#ifndef cpu_has_perf_cntr_intr_bit
#define cpu_has_perf_cntr_intr_bit	__opt(MIPS_CPU_PCI)
#endif

#ifndef cpu_has_vz
#define cpu_has_vz		__ase(MIPS_ASE_VZ)
#endif

#if defined(CONFIG_CPU_HAS_MSA) && !defined(cpu_has_msa)
# define cpu_has_msa		__ase(MIPS_ASE_MSA)
#elif !defined(cpu_has_msa)
# define cpu_has_msa		0
#endif

#ifndef cpu_has_ufr
# define cpu_has_ufr		__opt(MIPS_CPU_UFR)
#endif

#ifndef cpu_has_fre
# define cpu_has_fre		__opt(MIPS_CPU_FRE)
#endif

#ifndef cpu_has_cdmm
# define cpu_has_cdmm		__opt(MIPS_CPU_CDMM)
#endif

#ifndef cpu_has_small_pages
# define cpu_has_small_pages	__opt(MIPS_CPU_SP)
#endif

#ifndef cpu_has_nan_legacy
#define cpu_has_nan_legacy	__isa_lt_and_opt(6, MIPS_CPU_NAN_LEGACY)
#endif
#ifndef cpu_has_nan_2008
#define cpu_has_nan_2008	__isa_ge_or_opt(6, MIPS_CPU_NAN_2008)
#endif

#ifndef cpu_has_ebase_wg
# define cpu_has_ebase_wg	__opt(MIPS_CPU_EBASE_WG)
#endif

#ifndef cpu_has_badinstr
# define cpu_has_badinstr	__isa_ge_or_opt(6, MIPS_CPU_BADINSTR)
#endif

#ifndef cpu_has_badinstrp
# define cpu_has_badinstrp	__isa_ge_or_opt(6, MIPS_CPU_BADINSTRP)
#endif

#ifndef cpu_has_contextconfig
# define cpu_has_contextconfig	__opt(MIPS_CPU_CTXTC)
#endif

#ifndef cpu_has_perf
# define cpu_has_perf		__opt(MIPS_CPU_PERF)
#endif

#ifndef cpu_has_mac2008_only
# define cpu_has_mac2008_only	__opt(MIPS_CPU_MAC_2008_ONLY)
#endif

#ifndef cpu_has_ftlbparex
# define cpu_has_ftlbparex	__opt(MIPS_CPU_FTLBPAREX)
#endif

#ifndef cpu_has_gsexcex
# define cpu_has_gsexcex	__opt(MIPS_CPU_GSEXCEX)
#endif

#ifdef CONFIG_SMP
/*
 * Some systems share FTLB RAMs between threads within a core (siblings in
 * kernel parlance). This means that FTLB entries may become invalid at almost
 * any point when an entry is evicted due to a sibling thread writing an entry
 * to the shared FTLB RAM.
 *
 * This is only relevant to SMP systems, and the only systems that exhibit this
 * property implement MIPSr6 or higher so we constrain support for this to
 * kernels that will run on such systems.
 */
# ifndef cpu_has_shared_ftlb_ram
#  define cpu_has_shared_ftlb_ram \
	__isa_ge_and_opt(6, MIPS_CPU_SHARED_FTLB_RAM)
# endif

/*
 * Some systems take this a step further & share FTLB entries between siblings.
 * This is implemented as TLB writes happening as usual, but if an entry
 * written by a sibling exists in the shared FTLB for a translation which would
 * otherwise cause a TLB refill exception then the CPU will use the entry
 * written by its sibling rather than triggering a refill & writing a matching
 * TLB entry for itself.
 *
 * This is naturally only valid if a TLB entry is known to be suitable for use
 * on all siblings in a CPU, and so it only takes effect when MMIDs are in use
 * rather than ASIDs or when a TLB entry is marked global.
 */
# ifndef cpu_has_shared_ftlb_entries
#  define cpu_has_shared_ftlb_entries \
	__isa_ge_and_opt(6, MIPS_CPU_SHARED_FTLB_ENTRIES)
# endif
#endif /* SMP */

#ifndef cpu_has_shared_ftlb_ram
# define cpu_has_shared_ftlb_ram 0
#endif
#ifndef cpu_has_shared_ftlb_entries
# define cpu_has_shared_ftlb_entries 0
#endif

#ifdef CONFIG_MIPS_MT_SMP
# define cpu_has_mipsmt_pertccounters \
	__isa_lt_and_opt(6, MIPS_CPU_MT_PER_TC_PERF_COUNTERS)
#else
# define cpu_has_mipsmt_pertccounters 0
#endif /* CONFIG_MIPS_MT_SMP */

/*
 * We only enable MMID support for configurations which natively support 64 bit
 * atomics because getting good performance from the allocator relies upon
 * efficient atomic64_*() functions.
 */
#ifndef cpu_has_mmid
# ifdef CONFIG_GENERIC_ATOMIC64
#  define cpu_has_mmid		0
# else
#  define cpu_has_mmid		__isa_ge_and_opt(6, MIPS_CPU_MMID)
# endif
#endif

#ifndef cpu_has_mm_sysad
# define cpu_has_mm_sysad	__opt(MIPS_CPU_MM_SYSAD)
#endif

#ifndef cpu_has_mm_full
# define cpu_has_mm_full	__opt(MIPS_CPU_MM_FULL)
#endif

/*
 * Guest capabilities
 */
#ifndef cpu_guest_has_conf1
#define cpu_guest_has_conf1	(cpu_data[0].guest.conf & (1 << 1))
#endif
#ifndef cpu_guest_has_conf2
#define cpu_guest_has_conf2	(cpu_data[0].guest.conf & (1 << 2))
#endif
#ifndef cpu_guest_has_conf3
#define cpu_guest_has_conf3	(cpu_data[0].guest.conf & (1 << 3))
#endif
#ifndef cpu_guest_has_conf4
#define cpu_guest_has_conf4	(cpu_data[0].guest.conf & (1 << 4))
#endif
#ifndef cpu_guest_has_conf5
#define cpu_guest_has_conf5	(cpu_data[0].guest.conf & (1 << 5))
#endif
#ifndef cpu_guest_has_conf6
#define cpu_guest_has_conf6	(cpu_data[0].guest.conf & (1 << 6))
#endif
#ifndef cpu_guest_has_conf7
#define cpu_guest_has_conf7	(cpu_data[0].guest.conf & (1 << 7))
#endif
#ifndef cpu_guest_has_fpu
#define cpu_guest_has_fpu	(cpu_data[0].guest.options & MIPS_CPU_FPU)
#endif
#ifndef cpu_guest_has_watch
#define cpu_guest_has_watch	(cpu_data[0].guest.options & MIPS_CPU_WATCH)
#endif
#ifndef cpu_guest_has_contextconfig
#define cpu_guest_has_contextconfig (cpu_data[0].guest.options & MIPS_CPU_CTXTC)
#endif
#ifndef cpu_guest_has_segments
#define cpu_guest_has_segments	(cpu_data[0].guest.options & MIPS_CPU_SEGMENTS)
#endif
#ifndef cpu_guest_has_badinstr
#define cpu_guest_has_badinstr	(cpu_data[0].guest.options & MIPS_CPU_BADINSTR)
#endif
#ifndef cpu_guest_has_badinstrp
#define cpu_guest_has_badinstrp	(cpu_data[0].guest.options & MIPS_CPU_BADINSTRP)
#endif
#ifndef cpu_guest_has_htw
#define cpu_guest_has_htw	(cpu_data[0].guest.options & MIPS_CPU_HTW)
#endif
#ifndef cpu_guest_has_ldpte
#define cpu_guest_has_ldpte	(cpu_data[0].guest.options & MIPS_CPU_LDPTE)
#endif
#ifndef cpu_guest_has_mvh
#define cpu_guest_has_mvh	(cpu_data[0].guest.options & MIPS_CPU_MVH)
#endif
#ifndef cpu_guest_has_msa
#define cpu_guest_has_msa	(cpu_data[0].guest.ases & MIPS_ASE_MSA)
#endif
#ifndef cpu_guest_has_kscr
#define cpu_guest_has_kscr(n)	(cpu_data[0].guest.kscratch_mask & (1u << (n)))
#endif
#ifndef cpu_guest_has_rw_llb
#define cpu_guest_has_rw_llb	(cpu_has_mips_r6 || (cpu_data[0].guest.options & MIPS_CPU_RW_LLB))
#endif
#ifndef cpu_guest_has_perf
#define cpu_guest_has_perf	(cpu_data[0].guest.options & MIPS_CPU_PERF)
#endif
#ifndef cpu_guest_has_maar
#define cpu_guest_has_maar	(cpu_data[0].guest.options & MIPS_CPU_MAAR)
#endif
#ifndef cpu_guest_has_userlocal
#define cpu_guest_has_userlocal	(cpu_data[0].guest.options & MIPS_CPU_ULRI)
#endif

/*
 * Guest dynamic capabilities
 */
#ifndef cpu_guest_has_dyn_fpu
#define cpu_guest_has_dyn_fpu	(cpu_data[0].guest.options_dyn & MIPS_CPU_FPU)
#endif
#ifndef cpu_guest_has_dyn_watch
#define cpu_guest_has_dyn_watch	(cpu_data[0].guest.options_dyn & MIPS_CPU_WATCH)
#endif
#ifndef cpu_guest_has_dyn_contextconfig
#define cpu_guest_has_dyn_contextconfig (cpu_data[0].guest.options_dyn & MIPS_CPU_CTXTC)
#endif
#ifndef cpu_guest_has_dyn_perf
#define cpu_guest_has_dyn_perf	(cpu_data[0].guest.options_dyn & MIPS_CPU_PERF)
#endif
#ifndef cpu_guest_has_dyn_msa
#define cpu_guest_has_dyn_msa	(cpu_data[0].guest.ases_dyn & MIPS_ASE_MSA)
#endif
#ifndef cpu_guest_has_dyn_maar
#define cpu_guest_has_dyn_maar	(cpu_data[0].guest.options_dyn & MIPS_CPU_MAAR)
#endif

#endif /* __ASM_CPU_FEATURES_H */
