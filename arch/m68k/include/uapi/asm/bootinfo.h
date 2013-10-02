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
	unsigned short tag;		/* tag ID */
	unsigned short size;		/* size of record (in bytes) */
	unsigned long data[0];		/* data */
};

#endif /* __ASSEMBLY__ */


    /*
     *  Tag Definitions
     *
     *  Machine independent tags start counting from 0x0000
     *  Machine dependent tags start counting from 0x8000
     */

#define BI_LAST			0x0000	/* last record (sentinel) */
#define BI_MACHTYPE		0x0001	/* machine type (u_long) */
#define BI_CPUTYPE		0x0002	/* cpu type (u_long) */
#define BI_FPUTYPE		0x0003	/* fpu type (u_long) */
#define BI_MMUTYPE		0x0004	/* mmu type (u_long) */
#define BI_MEMCHUNK		0x0005	/* memory chunk address and size */
					/* (struct mem_info) */
#define BI_RAMDISK		0x0006	/* ramdisk address and size */
					/* (struct mem_info) */
#define BI_COMMAND_LINE		0x0007	/* kernel command line parameters */
					/* (string) */


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
	unsigned short branch;
	unsigned long magic;
	struct {
		unsigned long machtype;
		unsigned long version;
	} machversions[0];
} __packed;

#endif /* __ASSEMBLY__ */


#endif /* _UAPI_ASM_M68K_BOOTINFO_H */
