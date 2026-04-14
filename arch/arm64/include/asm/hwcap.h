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

#ifndef __ASSEMBLER__
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
#define __khwcap2_feature(x)		(const_ilog2(HWCAP2_ ## x) + 64)
#define __khwcap3_feature(x)		(const_ilog2(HWCAP3_ ## x) + 128)

#include "asm/kernel-hwcap.h"

/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this cpu supports.
 */
#define ELF_HWCAP		cpu_get_elf_hwcap()
#define ELF_HWCAP2		cpu_get_elf_hwcap2()
#define ELF_HWCAP3		cpu_get_elf_hwcap3()

#ifdef CONFIG_COMPAT
#define COMPAT_ELF_HWCAP	(compat_elf_hwcap)
#define COMPAT_ELF_HWCAP2	(compat_elf_hwcap2)
#define COMPAT_ELF_HWCAP3	(compat_elf_hwcap3)
extern unsigned int compat_elf_hwcap, compat_elf_hwcap2, compat_elf_hwcap3;
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
