/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_HWCAP_H
#define __ASM_HWCAP_H

#include <uapi/asm/hwcap.h>

#define COMPAT_HWCAP_HALF	(1 << 1)
#define COMPAT_HWCAP_THUMB	(1 << 2)
#define COMPAT_HWCAP_FAST_MULT	(1 << 4)
#define COMPAT_HWCAP_VFP	(1 << 6)
#define COMPAT_HWCAP_EDSP	(1 << 7)
#define COMPAT_HWCAP_NEON	(1 << 12)
#define COMPAT_HWCAP_VFPv3	(1 << 13)
#define COMPAT_HWCAP_TLS	(1 << 15)
#define COMPAT_HWCAP_VFPv4	(1 << 16)
#define COMPAT_HWCAP_IDIVA	(1 << 17)
#define COMPAT_HWCAP_IDIVT	(1 << 18)
#define COMPAT_HWCAP_IDIV	(COMPAT_HWCAP_IDIVA|COMPAT_HWCAP_IDIVT)
#define COMPAT_HWCAP_EVTSTRM	(1 << 21)

#ifndef __ASSEMBLY__
/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this cpu supports.
 */
#define ELF_HWCAP		(elf_hwcap)

#ifdef CONFIG_COMPAT
#define COMPAT_ELF_HWCAP	(compat_elf_hwcap)
extern unsigned int compat_elf_hwcap;
#endif

extern unsigned int elf_hwcap;
#endif
#endif
