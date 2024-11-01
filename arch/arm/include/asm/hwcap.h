/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASMARM_HWCAP_H
#define __ASMARM_HWCAP_H

#include <uapi/asm/hwcap.h>

#if !defined(__ASSEMBLY__)
/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this cpu supports.
 */
#define ELF_HWCAP	(elf_hwcap)
#define ELF_HWCAP2	(elf_hwcap2)
extern unsigned int elf_hwcap, elf_hwcap2;
#endif
#endif
