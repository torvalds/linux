/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copied from arch/arm64/include/asm/hwcap.h
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */
#ifndef _ASM_RISCV_HWCAP_H
#define _ASM_RISCV_HWCAP_H

#include <linux/bits.h>
#include <uapi/asm/hwcap.h>

#ifndef __ASSEMBLY__
/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this cpu supports.
 */
#define ELF_HWCAP		(elf_hwcap)

enum {
	CAP_HWCAP = 1,
};

extern unsigned long elf_hwcap;

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
 * Increse this to higher value as kernel support more ISA extensions.
 */
#define RISCV_ISA_EXT_MAX	64
#define RISCV_ISA_EXT_NAME_LEN_MAX 32

/* The base ID for multi-letter ISA extensions */
#define RISCV_ISA_EXT_BASE 26

/*
 * This enum represent the logical ID for each multi-letter RISC-V ISA extension.
 * The logical ID should start from RISCV_ISA_EXT_BASE and must not exceed
 * RISCV_ISA_EXT_MAX. 0-25 range is reserved for single letter
 * extensions while all the multi-letter extensions should define the next
 * available logical extension id.
 */
enum riscv_isa_ext_id {
	RISCV_ISA_EXT_SSCOFPMF = RISCV_ISA_EXT_BASE,
	RISCV_ISA_EXT_SVPBMT,
	RISCV_ISA_EXT_ID_MAX = RISCV_ISA_EXT_MAX,
};

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

#endif

#endif /* _ASM_RISCV_HWCAP_H */
