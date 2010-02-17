/*
** asm/bootinfo.h -- Definition of the Linux/m68k boot information structure
**
** Copyright 1992 by Greg Harp
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
** Created 09/29/92 by Greg Harp
**
** 5/2/94 Roman Hodek:
**   Added bi_atari part of the machine dependent union bi_un; for now it
**   contains just a model field to distinguish between TT and Falcon.
** 26/7/96 Roman Zippel:
**   Renamed to setup.h; added some useful macros to allow gcc some
**   optimizations if possible.
** 5/10/96 Geert Uytterhoeven:
**   Redesign of the boot information structure; renamed to bootinfo.h again
** 27/11/96 Geert Uytterhoeven:
**   Backwards compatibility with bootinfo interface version 1.0
*/

#ifndef _M68K_BOOTINFO_H
#define _M68K_BOOTINFO_H


    /*
     *  Bootinfo definitions
     *
     *  This is an easily parsable and extendable structure containing all
     *  information to be passed from the bootstrap to the kernel.
     *
     *  This way I hope to keep all future changes back/forewards compatible.
     *  Thus, keep your fingers crossed...
     *
     *  This structure is copied right after the kernel bss by the bootstrap
     *  routine.
     */

#ifndef __ASSEMBLY__

struct bi_record {
    unsigned short tag;			/* tag ID */
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
     *  Amiga-specific tags
     */

#define BI_AMIGA_MODEL		0x8000	/* model (u_long) */
#define BI_AMIGA_AUTOCON	0x8001	/* AutoConfig device */
					/* (struct ConfigDev) */
#define BI_AMIGA_CHIP_SIZE	0x8002	/* size of Chip RAM (u_long) */
#define BI_AMIGA_VBLANK		0x8003	/* VBLANK frequency (u_char) */
#define BI_AMIGA_PSFREQ		0x8004	/* power supply frequency (u_char) */
#define BI_AMIGA_ECLOCK		0x8005	/* EClock frequency (u_long) */
#define BI_AMIGA_CHIPSET	0x8006	/* native chipset present (u_long) */
#define BI_AMIGA_SERPER		0x8007	/* serial port period (u_short) */

    /*
     *  Atari-specific tags
     */

#define BI_ATARI_MCH_COOKIE	0x8000	/* _MCH cookie from TOS (u_long) */
#define BI_ATARI_MCH_TYPE	0x8001	/* special machine type (u_long) */
					/* (values are ATARI_MACH_* defines */

/* mch_cookie values (upper word) */
#define ATARI_MCH_ST		0
#define ATARI_MCH_STE		1
#define ATARI_MCH_TT		2
#define ATARI_MCH_FALCON	3

/* mch_type values */
#define ATARI_MACH_NORMAL	0	/* no special machine type */
#define ATARI_MACH_MEDUSA	1	/* Medusa 040 */
#define ATARI_MACH_HADES	2	/* Hades 040 or 060 */
#define ATARI_MACH_AB40		3	/* Afterburner040 on Falcon */

    /*
     *  VME-specific tags
     */

#define BI_VME_TYPE		0x8000	/* VME sub-architecture (u_long) */
#define BI_VME_BRDINFO		0x8001	/* VME board information (struct) */

/* BI_VME_TYPE codes */
#define	VME_TYPE_TP34V		0x0034	/* Tadpole TP34V */
#define VME_TYPE_MVME147	0x0147	/* Motorola MVME147 */
#define VME_TYPE_MVME162	0x0162	/* Motorola MVME162 */
#define VME_TYPE_MVME166	0x0166	/* Motorola MVME166 */
#define VME_TYPE_MVME167	0x0167	/* Motorola MVME167 */
#define VME_TYPE_MVME172	0x0172	/* Motorola MVME172 */
#define VME_TYPE_MVME177	0x0177	/* Motorola MVME177 */
#define VME_TYPE_BVME4000	0x4000	/* BVM Ltd. BVME4000 */
#define VME_TYPE_BVME6000	0x6000	/* BVM Ltd. BVME6000 */

/* BI_VME_BRDINFO is a 32 byte struct as returned by the Bug code on
 * Motorola VME boards.  Contains board number, Bug version, board
 * configuration options, etc.  See include/asm/mvme16xhw.h for details.
 */


    /*
     *  Macintosh-specific tags (all u_long)
     */

#define BI_MAC_MODEL		0x8000	/* Mac Gestalt ID (model type) */
#define BI_MAC_VADDR		0x8001	/* Mac video base address */
#define BI_MAC_VDEPTH		0x8002	/* Mac video depth */
#define BI_MAC_VROW		0x8003	/* Mac video rowbytes */
#define BI_MAC_VDIM		0x8004	/* Mac video dimensions */
#define BI_MAC_VLOGICAL		0x8005	/* Mac video logical base */
#define BI_MAC_SCCBASE		0x8006	/* Mac SCC base address */
#define BI_MAC_BTIME		0x8007	/* Mac boot time */
#define BI_MAC_GMTBIAS		0x8008	/* Mac GMT timezone offset */
#define BI_MAC_MEMSIZE		0x8009	/* Mac RAM size (sanity check) */
#define BI_MAC_CPUID		0x800a	/* Mac CPU type (sanity check) */
#define BI_MAC_ROMBASE		0x800b	/* Mac system ROM base address */

    /*
     *  Macintosh hardware profile data - unused, see macintosh.h for
     *  reasonable type values
     */

#define BI_MAC_VIA1BASE		0x8010	/* Mac VIA1 base address (always present) */
#define BI_MAC_VIA2BASE		0x8011	/* Mac VIA2 base address (type varies) */
#define BI_MAC_VIA2TYPE		0x8012	/* Mac VIA2 type (VIA, RBV, OSS) */
#define BI_MAC_ADBTYPE		0x8013	/* Mac ADB interface type */
#define BI_MAC_ASCBASE		0x8014	/* Mac Apple Sound Chip base address */
#define BI_MAC_SCSI5380		0x8015	/* Mac NCR 5380 SCSI (base address, multi) */
#define BI_MAC_SCSIDMA		0x8016	/* Mac SCSI DMA (base address) */
#define BI_MAC_SCSI5396		0x8017	/* Mac NCR 53C96 SCSI (base address, multi) */
#define BI_MAC_IDETYPE		0x8018	/* Mac IDE interface type */
#define BI_MAC_IDEBASE		0x8019	/* Mac IDE interface base address */
#define BI_MAC_NUBUS		0x801a	/* Mac Nubus type (none, regular, pseudo) */
#define BI_MAC_SLOTMASK		0x801b	/* Mac Nubus slots present */
#define BI_MAC_SCCTYPE		0x801c	/* Mac SCC serial type (normal, IOP) */
#define BI_MAC_ETHTYPE		0x801d	/* Mac builtin ethernet type (Sonic, MACE */
#define BI_MAC_ETHBASE		0x801e	/* Mac builtin ethernet base address */
#define BI_MAC_PMU		0x801f	/* Mac power management / poweroff hardware */
#define BI_MAC_IOP_SWIM		0x8020	/* Mac SWIM floppy IOP */
#define BI_MAC_IOP_ADB		0x8021	/* Mac ADB IOP */

    /*
     * Mac: compatibility with old booter data format (temporarily)
     * Fields unused with the new bootinfo can be deleted now; instead of
     * adding new fields the struct might be splitted into a hardware address
     * part and a hardware type part
     */

#ifndef __ASSEMBLY__

struct mac_booter_data
{
	unsigned long videoaddr;
	unsigned long videorow;
	unsigned long videodepth;
	unsigned long dimensions;
	unsigned long args;
	unsigned long boottime;
	unsigned long gmtbias;
	unsigned long bootver;
	unsigned long videological;
	unsigned long sccbase;
	unsigned long id;
	unsigned long memsize;
	unsigned long serialmf;
	unsigned long serialhsk;
	unsigned long serialgpi;
	unsigned long printmf;
	unsigned long printhsk;
	unsigned long printgpi;
	unsigned long cpuid;
	unsigned long rombase;
	unsigned long adbdelay;
	unsigned long timedbra;
};

extern struct mac_booter_data
	mac_bi_data;

#endif

    /*
     *  Apollo-specific tags
     */

#define BI_APOLLO_MODEL         0x8000  /* model (u_long) */

    /*
     *  HP300-specific tags
     */

#define BI_HP300_MODEL		0x8000	/* model (u_long) */
#define BI_HP300_UART_SCODE	0x8001	/* UART select code (u_long) */
#define BI_HP300_UART_ADDR	0x8002	/* phys. addr of UART (u_long) */

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
#define MK_BI_VERSION(major,minor)	(((major)<<16)+(minor))
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
};

#endif /* __ASSEMBLY__ */

#define AMIGA_BOOTI_VERSION    MK_BI_VERSION( 2, 0 )
#define ATARI_BOOTI_VERSION    MK_BI_VERSION( 2, 1 )
#define MAC_BOOTI_VERSION      MK_BI_VERSION( 2, 0 )
#define MVME147_BOOTI_VERSION  MK_BI_VERSION( 2, 0 )
#define MVME16x_BOOTI_VERSION  MK_BI_VERSION( 2, 0 )
#define BVME6000_BOOTI_VERSION MK_BI_VERSION( 2, 0 )
#define Q40_BOOTI_VERSION      MK_BI_VERSION( 2, 0 )
#define HP300_BOOTI_VERSION    MK_BI_VERSION( 2, 0 )

#ifdef BOOTINFO_COMPAT_1_0

    /*
     *  Backwards compatibility with bootinfo interface version 1.0
     */

#define COMPAT_AMIGA_BOOTI_VERSION    MK_BI_VERSION( 1, 0 )
#define COMPAT_ATARI_BOOTI_VERSION    MK_BI_VERSION( 1, 0 )
#define COMPAT_MAC_BOOTI_VERSION      MK_BI_VERSION( 1, 0 )

#include <linux/zorro.h>

#define COMPAT_NUM_AUTO    16

struct compat_bi_Amiga {
    int model;
    int num_autocon;
    struct ConfigDev autocon[COMPAT_NUM_AUTO];
    unsigned long chip_size;
    unsigned char vblank;
    unsigned char psfreq;
    unsigned long eclock;
    unsigned long chipset;
    unsigned long hw_present;
};

struct compat_bi_Atari {
    unsigned long hw_present;
    unsigned long mch_cookie;
};

#ifndef __ASSEMBLY__

struct compat_bi_Macintosh
{
	unsigned long videoaddr;
	unsigned long videorow;
	unsigned long videodepth;
	unsigned long dimensions;
	unsigned long args;
	unsigned long boottime;
	unsigned long gmtbias;
	unsigned long bootver;
	unsigned long videological;
	unsigned long sccbase;
	unsigned long id;
	unsigned long memsize;
	unsigned long serialmf;
	unsigned long serialhsk;
	unsigned long serialgpi;
	unsigned long printmf;
	unsigned long printhsk;
	unsigned long printgpi;
	unsigned long cpuid;
	unsigned long rombase;
	unsigned long adbdelay;
	unsigned long timedbra;
};

#endif

struct compat_mem_info {
    unsigned long addr;
    unsigned long size;
};

#define COMPAT_NUM_MEMINFO  4

#define COMPAT_CPUB_68020 0
#define COMPAT_CPUB_68030 1
#define COMPAT_CPUB_68040 2
#define COMPAT_CPUB_68060 3
#define COMPAT_FPUB_68881 5
#define COMPAT_FPUB_68882 6
#define COMPAT_FPUB_68040 7
#define COMPAT_FPUB_68060 8

#define COMPAT_CPU_68020    (1<<COMPAT_CPUB_68020)
#define COMPAT_CPU_68030    (1<<COMPAT_CPUB_68030)
#define COMPAT_CPU_68040    (1<<COMPAT_CPUB_68040)
#define COMPAT_CPU_68060    (1<<COMPAT_CPUB_68060)
#define COMPAT_CPU_MASK     (31)
#define COMPAT_FPU_68881    (1<<COMPAT_FPUB_68881)
#define COMPAT_FPU_68882    (1<<COMPAT_FPUB_68882)
#define COMPAT_FPU_68040    (1<<COMPAT_FPUB_68040)
#define COMPAT_FPU_68060    (1<<COMPAT_FPUB_68060)
#define COMPAT_FPU_MASK     (0xfe0)

#define COMPAT_CL_SIZE      (256)

struct compat_bootinfo {
    unsigned long machtype;
    unsigned long cputype;
    struct compat_mem_info memory[COMPAT_NUM_MEMINFO];
    int num_memory;
    unsigned long ramdisk_size;
    unsigned long ramdisk_addr;
    char command_line[COMPAT_CL_SIZE];
    union {
	struct compat_bi_Amiga     bi_ami;
	struct compat_bi_Atari     bi_ata;
	struct compat_bi_Macintosh bi_mac;
    } bi_un;
};

#define bi_amiga	bi_un.bi_ami
#define bi_atari	bi_un.bi_ata
#define bi_mac		bi_un.bi_mac

#endif /* BOOTINFO_COMPAT_1_0 */


#endif /* _M68K_BOOTINFO_H */
