/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_HWCAP_H
#define __ASM_HWCAP_H

#include <uapi/asm/hwcap.h>
#include <asm/cpufeature.h>

#define COMPAT_HWCAP_SWP	(1 << 0)
#define COMPAT_HWCAP_HALF	(1 << 1)
#define COMPAT_HWCAP_THUMB	(1 << 2)
#define COMPAT_HWCAP_26BIT	(1 << 3)
#define COMPAT_HWCAP_FAST_MULT	(1 << 4)
#define COMPAT_HWCAP_FPA	(1 << 5)
#define COMPAT_HWCAP_VFP	(1 << 6)
#define COMPAT_HWCAP_EDSP	(1 << 7)
#define COMPAT_HWCAP_JAVA	(1 << 8)
#define COMPAT_HWCAP_IWMMXT	(1 << 9)
#define COMPAT_HWCAP_CRUNCH	(1 << 10) /* Obsolete */
#define COMPAT_HWCAP_THUMBEE	(1 << 11)
#define COMPAT_HWCAP_NEON	(1 << 12)
#define COMPAT_HWCAP_VFPv3	(1 << 13)
#define COMPAT_HWCAP_VFPV3D16	(1 << 14)
#define COMPAT_HWCAP_TLS	(1 << 15)
#define COMPAT_HWCAP_VFPv4	(1 << 16)
#define COMPAT_HWCAP_IDIVA	(1 << 17)
#define COMPAT_HWCAP_IDIVT	(1 << 18)
#define COMPAT_HWCAP_IDIV	(COMPAT_HWCAP_IDIVA|COMPAT_HWCAP_IDIVT)
#define COMPAT_HWCAP_VFPD32	(1 << 19)
#define COMPAT_HWCAP_LPAE	(1 << 20)
#define COMPAT_HWCAP_EVTSTRM	(1 << 21)
#define COMPAT_HWCAP_FPHP	(1 << 22)
#define COMPAT_HWCAP_ASIMDHP	(1 << 23)
#define COMPAT_HWCAP_ASIMDDP	(1 << 24)
#define COMPAT_HWCAP_ASIMDFHM	(1 << 25)
#define COMPAT_HWCAP_ASIMDBF16	(1 << 26)
#define COMPAT_HWCAP_I8MM	(1 << 27)

#define COMPAT_HWCAP2_AES	(1 << 0)
#define COMPAT_HWCAP2_PMULL	(1 << 1)
#define COMPAT_HWCAP2_SHA1	(1 << 2)
#define COMPAT_HWCAP2_SHA2	(1 << 3)
#define COMPAT_HWCAP2_CRC32	(1 << 4)
#define COMPAT_HWCAP2_SB	(1 << 5)
#define COMPAT_HWCAP2_SSBS	(1 << 6)

#ifndef __ASSEMBLY__
#include <linux/log2.h>

/*
 * For userspace we represent hwcaps as a collection of HWCAP{,2}_x bitfields
 * as described in uapi/asm/hwcap.h. For the kernel we represent hwcaps as
 * natural numbers (in a single range of size MAX_CPU_FEATURES) defined here
 * with prefix KERNEL_HWCAP_ mapped to their HWCAP{,2}_x counterpart.
 *
 * Hwcaps should be set and tested within the kernel via the
 * cpu_{set,have}_named_feature(feature) where feature is the unique suffix
 * of KERNEL_HWCAP_{feature}.
 */
#define __khwcap_feature(x)		const_ilog2(HWCAP_ ## x)
#define KERNEL_HWCAP_FP			__khwcap_feature(FP)
#define KERNEL_HWCAP_ASIMD		__khwcap_feature(ASIMD)
#define KERNEL_HWCAP_EVTSTRM		__khwcap_feature(EVTSTRM)
#define KERNEL_HWCAP_AES		__khwcap_feature(AES)
#define KERNEL_HWCAP_PMULL		__khwcap_feature(PMULL)
#define KERNEL_HWCAP_SHA1		__khwcap_feature(SHA1)
#define KERNEL_HWCAP_SHA2		__khwcap_feature(SHA2)
#define KERNEL_HWCAP_CRC32		__khwcap_feature(CRC32)
#define KERNEL_HWCAP_ATOMICS		__khwcap_feature(ATOMICS)
#define KERNEL_HWCAP_FPHP		__khwcap_feature(FPHP)
#define KERNEL_HWCAP_ASIMDHP		__khwcap_feature(ASIMDHP)
#define KERNEL_HWCAP_CPUID		__khwcap_feature(CPUID)
#define KERNEL_HWCAP_ASIMDRDM		__khwcap_feature(ASIMDRDM)
#define KERNEL_HWCAP_JSCVT		__khwcap_feature(JSCVT)
#define KERNEL_HWCAP_FCMA		__khwcap_feature(FCMA)
#define KERNEL_HWCAP_LRCPC		__khwcap_feature(LRCPC)
#define KERNEL_HWCAP_DCPOP		__khwcap_feature(DCPOP)
#define KERNEL_HWCAP_SHA3		__khwcap_feature(SHA3)
#define KERNEL_HWCAP_SM3		__khwcap_feature(SM3)
#define KERNEL_HWCAP_SM4		__khwcap_feature(SM4)
#define KERNEL_HWCAP_ASIMDDP		__khwcap_feature(ASIMDDP)
#define KERNEL_HWCAP_SHA512		__khwcap_feature(SHA512)
#define KERNEL_HWCAP_SVE		__khwcap_feature(SVE)
#define KERNEL_HWCAP_ASIMDFHM		__khwcap_feature(ASIMDFHM)
#define KERNEL_HWCAP_DIT		__khwcap_feature(DIT)
#define KERNEL_HWCAP_USCAT		__khwcap_feature(USCAT)
#define KERNEL_HWCAP_ILRCPC		__khwcap_feature(ILRCPC)
#define KERNEL_HWCAP_FLAGM		__khwcap_feature(FLAGM)
#define KERNEL_HWCAP_SSBS		__khwcap_feature(SSBS)
#define KERNEL_HWCAP_SB			__khwcap_feature(SB)
#define KERNEL_HWCAP_PACA		__khwcap_feature(PACA)
#define KERNEL_HWCAP_PACG		__khwcap_feature(PACG)

#define __khwcap2_feature(x)		(const_ilog2(HWCAP2_ ## x) + 64)
#define KERNEL_HWCAP_DCPODP		__khwcap2_feature(DCPODP)
#define KERNEL_HWCAP_SVE2		__khwcap2_feature(SVE2)
#define KERNEL_HWCAP_SVEAES		__khwcap2_feature(SVEAES)
#define KERNEL_HWCAP_SVEPMULL		__khwcap2_feature(SVEPMULL)
#define KERNEL_HWCAP_SVEBITPERM		__khwcap2_feature(SVEBITPERM)
#define KERNEL_HWCAP_SVESHA3		__khwcap2_feature(SVESHA3)
#define KERNEL_HWCAP_SVESM4		__khwcap2_feature(SVESM4)
#define KERNEL_HWCAP_FLAGM2		__khwcap2_feature(FLAGM2)
#define KERNEL_HWCAP_FRINT		__khwcap2_feature(FRINT)
#define KERNEL_HWCAP_SVEI8MM		__khwcap2_feature(SVEI8MM)
#define KERNEL_HWCAP_SVEF32MM		__khwcap2_feature(SVEF32MM)
#define KERNEL_HWCAP_SVEF64MM		__khwcap2_feature(SVEF64MM)
#define KERNEL_HWCAP_SVEBF16		__khwcap2_feature(SVEBF16)
#define KERNEL_HWCAP_I8MM		__khwcap2_feature(I8MM)
#define KERNEL_HWCAP_BF16		__khwcap2_feature(BF16)
#define KERNEL_HWCAP_DGH		__khwcap2_feature(DGH)
#define KERNEL_HWCAP_RNG		__khwcap2_feature(RNG)
#define KERNEL_HWCAP_BTI		__khwcap2_feature(BTI)
#define KERNEL_HWCAP_MTE		__khwcap2_feature(MTE)
#define KERNEL_HWCAP_ECV		__khwcap2_feature(ECV)
#define KERNEL_HWCAP_AFP		__khwcap2_feature(AFP)
#define KERNEL_HWCAP_RPRES		__khwcap2_feature(RPRES)
#define KERNEL_HWCAP_MTE3		__khwcap2_feature(MTE3)
#define KERNEL_HWCAP_SME		__khwcap2_feature(SME)
#define KERNEL_HWCAP_SME_I16I64		__khwcap2_feature(SME_I16I64)
#define KERNEL_HWCAP_SME_F64F64		__khwcap2_feature(SME_F64F64)
#define KERNEL_HWCAP_SME_I8I32		__khwcap2_feature(SME_I8I32)
#define KERNEL_HWCAP_SME_F16F32		__khwcap2_feature(SME_F16F32)
#define KERNEL_HWCAP_SME_B16F32		__khwcap2_feature(SME_B16F32)
#define KERNEL_HWCAP_SME_F32F32		__khwcap2_feature(SME_F32F32)
#define KERNEL_HWCAP_SME_FA64		__khwcap2_feature(SME_FA64)
#define KERNEL_HWCAP_WFXT		__khwcap2_feature(WFXT)
#define KERNEL_HWCAP_EBF16		__khwcap2_feature(EBF16)
#define KERNEL_HWCAP_SVE_EBF16		__khwcap2_feature(SVE_EBF16)
#define KERNEL_HWCAP_CSSC		__khwcap2_feature(CSSC)
#define KERNEL_HWCAP_RPRFM		__khwcap2_feature(RPRFM)
#define KERNEL_HWCAP_SVE2P1		__khwcap2_feature(SVE2P1)
#define KERNEL_HWCAP_SME2		__khwcap2_feature(SME2)
#define KERNEL_HWCAP_SME2P1		__khwcap2_feature(SME2P1)
#define KERNEL_HWCAP_SME_I16I32		__khwcap2_feature(SME_I16I32)
#define KERNEL_HWCAP_SME_BI32I32	__khwcap2_feature(SME_BI32I32)
#define KERNEL_HWCAP_SME_B16B16		__khwcap2_feature(SME_B16B16)
#define KERNEL_HWCAP_SME_F16F16		__khwcap2_feature(SME_F16F16)
#define KERNEL_HWCAP_MOPS		__khwcap2_feature(MOPS)
#define KERNEL_HWCAP_HBC		__khwcap2_feature(HBC)
#define KERNEL_HWCAP_SVE_B16B16		__khwcap2_feature(SVE_B16B16)
#define KERNEL_HWCAP_LRCPC3		__khwcap2_feature(LRCPC3)
#define KERNEL_HWCAP_LSE128		__khwcap2_feature(LSE128)

/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this cpu supports.
 */
#define ELF_HWCAP		cpu_get_elf_hwcap()
#define ELF_HWCAP2		cpu_get_elf_hwcap2()

#ifdef CONFIG_COMPAT
#define COMPAT_ELF_HWCAP	(compat_elf_hwcap)
#define COMPAT_ELF_HWCAP2	(compat_elf_hwcap2)
extern unsigned int compat_elf_hwcap, compat_elf_hwcap2;
#endif

enum {
	CAP_HWCAP = 1,
#ifdef CONFIG_COMPAT
	CAP_COMPAT_HWCAP,
	CAP_COMPAT_HWCAP2,
#endif
};

#endif
#endif
