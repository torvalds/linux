/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * asm/bootinfo.h -- Definition of the Linux/m68k boot information structure
 *
 * Copyright 1992 by Greg Harp
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef _UAPI_ASM_M68K_BOOTINFO_H
#define _UAPI_ASM_M68K_BOOTINFO_H


#include <linux/types.h>


#ifndef __ASSEMBLY__

    /*
     *  Bootinfo definitions
     *
     *  This is an easily parsable and extendable structure containing all
     *  information to be passed from the bootstrap to the kernel.
     *
     *  This way I hope to keep all future changes back/forewards compatible.
     *  Thus, keep your fingers crossed...
     *
     *  This structure is copied right after the kernel by the bootstrap
     *  routine.
     */

struct bi_record {
	__be16 tag;			/* tag ID */
	__be16 size;			/* size of record (in bytes) */
	__be32 data[0];			/* data */
};


struct mem_info {
	__be32 addr;			/* physical address of memory chunk */
	__be32 size;			/* length of memory chunk (in bytes) */
};

#endif /* __ASSEMBLY__ */


    /*
     *  Tag Definitions
     *
     *  Machine independent tags start counting from 0x0000
     *  Machine dependent tags start counting from 0x8000
     */

#define BI_LAST			0x0000	/* last record (sentinel) */
#define BI_MACHTYPE		0x0001	/* machine type (__be32) */
#define BI_CPUTYPE		0x0002	/* cpu type (__be32) */
#define BI_FPUTYPE		0x0003	/* fpu type (__be32) */
#define BI_MMUTYPE		0x0004	/* mmu type (__be32) */
#define BI_MEMCHUNK		0x0005	/* memory chunk address and size */
					/* (struct mem_info) */
#define BI_RAMDISK		0x0006	/* ramdisk address and size */
					/* (struct mem_info) */
#define BI_COMMAND_LINE		0x0007	/* kernel command line parameters */
					/* (string) */


    /*
     *  Linux/m68k Architectures (BI_MACHTYPE)
     */

#define MACH_AMIGA		1
#define MACH_ATARI		2
#define MACH_MAC		3
#define MACH_APOLLO		4
#define MACH_SUN3		5
#define MACH_MVME147		6
#define MACH_MVME16x		7
#define MACH_BVME6000		8
#define MACH_HP300		9
#define MACH_Q40		10
#define MACH_SUN3X		11
#define MACH_M54XX		12
#define MACH_M5441X		13
#define MACH_VIRT		14


    /*
     *  CPU, FPU and MMU types (BI_CPUTYPE, BI_FPUTYPE, BI_MMUTYPE)
     *
     *  Note: we may rely on the following equalities:
     *
     *      CPU_68020 == MMU_68851
     *      CPU_68030 == MMU_68030
     *      CPU_68040 == FPU_68040 == MMU_68040
     *      CPU_68060 == FPU_68060 == MMU_68060
     */

#define CPUB_68020		0
#define CPUB_68030		1
#define CPUB_68040		2
#define CPUB_68060		3
#define CPUB_COLDFIRE		4

#define CPU_68020		(1 << CPUB_68020)
#define CPU_68030		(1 << CPUB_68030)
#define CPU_68040		(1 << CPUB_68040)
#define CPU_68060		(1 << CPUB_68060)
#define CPU_COLDFIRE		(1 << CPUB_COLDFIRE)

#define FPUB_68881		0
#define FPUB_68882		1
#define FPUB_68040		2	/* Internal FPU */
#define FPUB_68060		3	/* Internal FPU */
#define FPUB_SUNFPA		4	/* Sun-3 FPA */
#define FPUB_COLDFIRE		5	/* ColdFire FPU */

#define FPU_68881		(1 << FPUB_68881)
#define FPU_68882		(1 << FPUB_68882)
#define FPU_68040		(1 << FPUB_68040)
#define FPU_68060		(1 << FPUB_68060)
#define FPU_SUNFPA		(1 << FPUB_SUNFPA)
#define FPU_COLDFIRE		(1 << FPUB_COLDFIRE)

#define MMUB_68851		0
#define MMUB_68030		1	/* Internal MMU */
#define MMUB_68040		2	/* Internal MMU */
#define MMUB_68060		3	/* Internal MMU */
#define MMUB_APOLLO		4	/* Custom Apollo */
#define MMUB_SUN3		5	/* Custom Sun-3 */
#define MMUB_COLDFIRE		6	/* Internal MMU */

#define MMU_68851		(1 << MMUB_68851)
#define MMU_68030		(1 << MMUB_68030)
#define MMU_68040		(1 << MMUB_68040)
#define MMU_68060		(1 << MMUB_68060)
#define MMU_SUN3		(1 << MMUB_SUN3)
#define MMU_APOLLO		(1 << MMUB_APOLLO)
#define MMU_COLDFIRE		(1 << MMUB_COLDFIRE)


    /*
     * Stuff for bootinfo interface versioning
     *
     * At the start of kernel code, a 'struct bootversion' is located.
     * bootstrap checks for a matching version of the interface before booting
     * a kernel, to avoid user confusion if kernel and bootstrap don't work
     * together :-)
     *
     * If incompatible changes are made to the bootinfo interface, the major
     * number below should be stepped (and the minor reset to 0) for the
     * appropriate machine. If a change is backward-compatible, the minor
     * should be stepped. "Backwards-compatible" means that booting will work,
     * but certain features may not.
     */

#define BOOTINFOV_MAGIC			0x4249561A	/* 'BIV^Z' */
#define MK_BI_VERSION(major, minor)	(((major) << 16) + (minor))
#define BI_VERSION_MAJOR(v)		(((v) >> 16) & 0xffff)
#define BI_VERSION_MINOR(v)		((v) & 0xffff)

#ifndef __ASSEMBLY__

struct bootversion {
	__be16 branch;
	__be32 magic;
	struct {
		__be32 machtype;
		__be32 version;
	} machversions[0];
} __packed;

#endif /* __ASSEMBLY__ */


#endif /* _UAPI_ASM_M68K_BOOTINFO_H */
