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
#define RISCV_ISA_EXT_b		('b' - 'a')
#define RISCV_ISA_EXT_c		('c' - 'a')
#define RISCV_ISA_EXT_d		('d' - 'a')
#define RISCV_ISA_EXT_f		('f' - 'a')
#define RISCV_ISA_EXT_h		('h' - 'a')
#define RISCV_ISA_EXT_i		('i' - 'a')
#define RISCV_ISA_EXT_j		('j' - 'a')
#define RISCV_ISA_EXT_k		('k' - 'a')
#define RISCV_ISA_EXT_m		('m' - 'a')
#define RISCV_ISA_EXT_p		('p' - 'a')
#define RISCV_ISA_EXT_q		('q' - 'a')
#define RISCV_ISA_EXT_s		('s' - 'a')
#define RISCV_ISA_EXT_u		('u' - 'a')
#define RISCV_ISA_EXT_v		('v' - 'a')

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
#define RISCV_ISA_EXT_ZBA		37
#define RISCV_ISA_EXT_ZBS		38
#define RISCV_ISA_EXT_ZICNTR		39
#define RISCV_ISA_EXT_ZICSR		40
#define RISCV_ISA_EXT_ZIFENCEI		41
#define RISCV_ISA_EXT_ZIHPM		42

#define RISCV_ISA_EXT_MAX		64

#ifdef CONFIG_RISCV_M_MODE
#define RISCV_ISA_EXT_SxAIA		RISCV_ISA_EXT_SMAIA
#else
#define RISCV_ISA_EXT_SxAIA		RISCV_ISA_EXT_SSAIA
#endif

#ifndef __ASSEMBLY__

#include <linux/jump_label.h>

unsigned long riscv_get_elf_hwcap(void);

struct riscv_isa_ext_data {
	const unsigned int id;
	const char *name;
	const char *property;
};

extern const struct riscv_isa_ext_data riscv_isa_ext[];
extern const size_t riscv_isa_ext_count;
extern bool riscv_isa_fallback;

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
