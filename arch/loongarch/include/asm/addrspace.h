/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1996, 99 Ralf Baechle
 * Copyright (C) 2000, 2002  Maciej W. Rozycki
 * Copyright (C) 1990, 1999 by Silicon Graphics, Inc.
 */
#ifndef _ASM_ADDRSPACE_H
#define _ASM_ADDRSPACE_H

#include <linux/const.h>
#include <linux/sizes.h>

#include <asm/loongarch.h>

/*
 * This gives the physical RAM offset.
 */
#ifndef __ASSEMBLER__
#ifndef PHYS_OFFSET
#define PHYS_OFFSET	_UL(0)
#endif
extern unsigned long vm_map_base;
#endif /* __ASSEMBLER__ */

#ifndef IO_BASE
#define IO_BASE			CSR_DMW0_BASE
#endif

#ifndef CACHE_BASE
#define CACHE_BASE		CSR_DMW1_BASE
#endif

#ifndef UNCACHE_BASE
#define UNCACHE_BASE		CSR_DMW0_BASE
#endif

#ifndef WRITECOMBINE_BASE
#define WRITECOMBINE_BASE	CSR_DMW2_BASE
#endif

#define DMW_PABITS	48
#define TO_PHYS_MASK	((1ULL << DMW_PABITS) - 1)

/*
 * Memory above this physical address will be considered highmem.
 */
#ifndef HIGHMEM_START
#define HIGHMEM_START		(_UL(1) << _UL(DMW_PABITS))
#endif

#define TO_PHYS(x)		(		((x) & TO_PHYS_MASK))
#define TO_CACHE(x)		(CACHE_BASE   |	((x) & TO_PHYS_MASK))
#define TO_UNCACHE(x)		(UNCACHE_BASE |	((x) & TO_PHYS_MASK))

/*
 * This handles the memory map.
 */
#ifndef PAGE_OFFSET
#define PAGE_OFFSET		(CACHE_BASE + PHYS_OFFSET)
#endif

#ifndef FIXADDR_TOP
#define FIXADDR_TOP		((unsigned long)(long)(int)0xfffe0000)
#endif

#ifdef __ASSEMBLER__
#define _ATYPE_
#define _ATYPE32_
#define _ATYPE64_
#else
#define _ATYPE_		__PTRDIFF_TYPE__
#define _ATYPE32_	int
#define _ATYPE64_	__s64
#endif

#ifdef CONFIG_64BIT
#define _CONST64_(x)	_UL(x)
#else
#define _CONST64_(x)	_ULL(x)
#endif

/*
 *  32/64-bit LoongArch address spaces
 */
#ifdef __ASSEMBLER__
#define _ACAST32_
#define _ACAST64_
#else
#define _ACAST32_		(_ATYPE_)(_ATYPE32_)	/* widen if necessary */
#define _ACAST64_		(_ATYPE64_)		/* do _not_ narrow */
#endif

#ifdef CONFIG_32BIT

#define UVRANGE			0x00000000
#define KPRANGE0		0x80000000
#define KPRANGE1		0xa0000000
#define KVRANGE			0xc0000000

#else

#define XUVRANGE		_CONST64_(0x0000000000000000)
#define XSPRANGE		_CONST64_(0x4000000000000000)
#define XKPRANGE		_CONST64_(0x8000000000000000)
#define XKVRANGE		_CONST64_(0xc000000000000000)

#endif

/*
 * Returns the physical address of a KPRANGEx / XKPRANGE address
 */
#define PHYSADDR(a)		((_ACAST64_(a)) & TO_PHYS_MASK)

/*
 * On LoongArch, I/O ports mappring is following:
 *
 *              |         ....          |
 *              |-----------------------|
 *              | pci io ports(16K~32M) |
 *              |-----------------------|
 *              | isa io ports(0  ~16K) |
 * PCI_IOBASE ->|-----------------------|
 *              |         ....          |
 */
#define PCI_IOBASE	((void __iomem *)(vm_map_base + (2 * PAGE_SIZE)))
#define PCI_IOSIZE	SZ_32M
#define ISA_IOSIZE	SZ_16K
#define IO_SPACE_LIMIT	(PCI_IOSIZE - 1)

#define PHYS_LINK_KADDR	PHYSADDR(VMLINUX_LOAD_ADDRESS)

#endif /* _ASM_ADDRSPACE_H */
