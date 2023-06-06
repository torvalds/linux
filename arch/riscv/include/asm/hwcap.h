/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copied from arch/arm64/include/asm/hwcap.h
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */
#ifndef _ASM_RISCV_HWCAP_H
#define _ASM_RISCV_HWCAP_H

#include <asm/alternative-macros.h>
#include <asm/errno.h>
#include <linux/bits.h>
#include <uapi/asm/hwcap.h>

#define RISCV_ISA_EXT_a		('a' - 'a')
#define RISCV_ISA_EXT_c		('c' - 'a')
#define RISCV_ISA_EXT_d		('d' - 'a')
#define RISCV_ISA_EXT_f		('f' - 'a')
#define RISCV_ISA_EXT_h		('h' - 'a')
#define RISCV_ISA_EXT_i		('i' - 'a')
#define RISCV_ISA_EXT_m		('m' - 'a')
#define RISCV_ISA_EXT_s		('s' - 'a')
#define RISCV_ISA_EXT_u		('u' - 'a')

/*
 * These macros represent the logical IDs of each multi-letter RISC-V ISA
 * extension and are used in the ISA bitmap. The logical IDs start from
 * RISCV_ISA_EXT_BASE, which allows the 0-25 range to be reserved for single
 * letter extensions. The maximum, RISCV_ISA_EXT_MAX, is defined in order
 * to allocate the bitmap and may be increased when necessary.
 *
 * New extensions should just be added to the bottom, rather than added
 * alphabetically, in order to avoid unnecessary shuffling.
 */
#define RISCV_ISA_EXT_BASE		26

#define RISCV_ISA_EXT_SSCOFPMF		26
#define RISCV_ISA_EXT_SSTC		27
#define RISCV_ISA_EXT_SVINVAL		28
#define RISCV_ISA_EXT_SVPBMT		29
#define RISCV_ISA_EXT_ZBB		30
#define RISCV_ISA_EXT_ZICBOM		31
#define RISCV_ISA_EXT_ZIHINTPAUSE	32
#define RISCV_ISA_EXT_SVNAPOT		33
#define RISCV_ISA_EXT_ZICBOZ		34
#define RISCV_ISA_EXT_SMAIA		35
#define RISCV_ISA_EXT_SSAIA		36

#define RISCV_ISA_EXT_MAX		64
#define RISCV_ISA_EXT_NAME_LEN_MAX	32

#ifdef CONFIG_RISCV_M_MODE
#define RISCV_ISA_EXT_SxAIA		RISCV_ISA_EXT_SMAIA
#else
#define RISCV_ISA_EXT_SxAIA		RISCV_ISA_EXT_SSAIA
#endif

#ifndef __ASSEMBLY__

#include <linux/jump_label.h>

struct riscv_isa_ext_data {
	/* Name of the extension displayed to userspace via /proc/cpuinfo */
	char uprop[RISCV_ISA_EXT_NAME_LEN_MAX];
	/* The logical ISA extension ID */
	unsigned int isa_ext_id;
};

unsigned long riscv_isa_extension_base(const unsigned long *isa_bitmap);

#define riscv_isa_extension_mask(ext) BIT_MASK(RISCV_ISA_EXT_##ext)

bool __riscv_isa_extension_available(const unsigned long *isa_bitmap, int bit);
#define riscv_isa_extension_available(isa_bitmap, ext)	\
	__riscv_isa_extension_available(isa_bitmap, RISCV_ISA_EXT_##ext)

static __always_inline bool
riscv_has_extension_likely(const unsigned long ext)
{
	compiletime_assert(ext < RISCV_ISA_EXT_MAX,
			   "ext must be < RISCV_ISA_EXT_MAX");

	if (IS_ENABLED(CONFIG_RISCV_ALTERNATIVE)) {
		asm_volatile_goto(
		ALTERNATIVE("j	%l[l_no]", "nop", 0, %[ext], 1)
		:
		: [ext] "i" (ext)
		:
		: l_no);
	} else {
		if (!__riscv_isa_extension_available(NULL, ext))
			goto l_no;
	}

	return true;
l_no:
	return false;
}

static __always_inline bool
riscv_has_extension_unlikely(const unsigned long ext)
{
	compiletime_assert(ext < RISCV_ISA_EXT_MAX,
			   "ext must be < RISCV_ISA_EXT_MAX");

	if (IS_ENABLED(CONFIG_RISCV_ALTERNATIVE)) {
		asm_volatile_goto(
		ALTERNATIVE("nop", "j	%l[l_yes]", 0, %[ext], 1)
		:
		: [ext] "i" (ext)
		:
		: l_yes);
	} else {
		if (__riscv_isa_extension_available(NULL, ext))
			goto l_yes;
	}

	return false;
l_yes:
	return true;
}

#endif

#endif /* _ASM_RISCV_HWCAP_H */
