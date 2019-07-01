/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copied from arch/arm64/include/asm/hwcap.h
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */
#ifndef __ASM_HWCAP_H
#define __ASM_HWCAP_H

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
#endif
#endif
