/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2017
 */
#ifndef _ASM_S390_SETUP_H
#define _ASM_S390_SETUP_H

#include <linux/bits.h>
#include <uapi/asm/setup.h>
#include <linux/build_bug.h>

#define EP_OFFSET		0x10008
#define EP_STRING		"S390EP"
#define PARMAREA		0x10400
#define EARLY_SCCB_OFFSET	0x11000
#define HEAD_END		0x12000

#define EARLY_SCCB_SIZE		PAGE_SIZE

/*
 * Machine features detected in early.c
 */

#define MACHINE_FLAG_VM		BIT(0)
#define MACHINE_FLAG_KVM	BIT(1)
#define MACHINE_FLAG_LPAR	BIT(2)
#define MACHINE_FLAG_DIAG9C	BIT(3)
#define MACHINE_FLAG_ESOP	BIT(4)
#define MACHINE_FLAG_IDTE	BIT(5)
#define MACHINE_FLAG_EDAT1	BIT(7)
#define MACHINE_FLAG_EDAT2	BIT(8)
#define MACHINE_FLAG_TOPOLOGY	BIT(10)
#define MACHINE_FLAG_TE		BIT(11)
#define MACHINE_FLAG_TLB_LC	BIT(12)
#define MACHINE_FLAG_VX		BIT(13)
#define MACHINE_FLAG_TLB_GUEST	BIT(14)
#define MACHINE_FLAG_NX		BIT(15)
#define MACHINE_FLAG_GS		BIT(16)
#define MACHINE_FLAG_SCC	BIT(17)

#define LPP_MAGIC		BIT(31)
#define LPP_PID_MASK		_AC(0xffffffff, UL)

/* Offsets to entry points in kernel/head.S  */

#define STARTUP_NORMAL_OFFSET	0x10000
#define STARTUP_KDUMP_OFFSET	0x10010

/* Offsets to parameters in kernel/head.S  */

#define IPL_DEVICE_OFFSET	0x10400
#define INITRD_START_OFFSET	0x10408
#define INITRD_SIZE_OFFSET	0x10410
#define OLDMEM_BASE_OFFSET	0x10418
#define OLDMEM_SIZE_OFFSET	0x10420
#define KERNEL_VERSION_OFFSET	0x10428
#define COMMAND_LINE_OFFSET	0x10480

#ifndef __ASSEMBLY__

#include <asm/lowcore.h>
#include <asm/types.h>

#define IPL_DEVICE	(*(unsigned long *)  (IPL_DEVICE_OFFSET))
#define INITRD_START	(*(unsigned long *)  (INITRD_START_OFFSET))
#define INITRD_SIZE	(*(unsigned long *)  (INITRD_SIZE_OFFSET))
#define OLDMEM_BASE	(*(unsigned long *)  (OLDMEM_BASE_OFFSET))
#define OLDMEM_SIZE	(*(unsigned long *)  (OLDMEM_SIZE_OFFSET))
#define COMMAND_LINE	((char *)	     (COMMAND_LINE_OFFSET))

struct parmarea {
	unsigned long ipl_device;			/* 0x10400 */
	unsigned long initrd_start;			/* 0x10408 */
	unsigned long initrd_size;			/* 0x10410 */
	unsigned long oldmem_base;			/* 0x10418 */
	unsigned long oldmem_size;			/* 0x10420 */
	unsigned long kernel_version;			/* 0x10428 */
	char pad1[0x10480 - 0x10430];			/* 0x10430 - 0x10480 */
	char command_line[ARCH_COMMAND_LINE_SIZE];	/* 0x10480 */
};

extern unsigned int zlib_dfltcc_support;
#define ZLIB_DFLTCC_DISABLED		0
#define ZLIB_DFLTCC_FULL		1
#define ZLIB_DFLTCC_DEFLATE_ONLY	2
#define ZLIB_DFLTCC_INFLATE_ONLY	3
#define ZLIB_DFLTCC_FULL_DEBUG		4

extern int noexec_disabled;
extern int memory_end_set;
extern unsigned long memory_end;
extern unsigned long vmalloc_size;
extern unsigned long max_physmem_end;

/* The Write Back bit position in the physaddr is given by the SLPC PCI */
extern unsigned long mio_wb_bit_mask;

#define MACHINE_IS_VM		(S390_lowcore.machine_flags & MACHINE_FLAG_VM)
#define MACHINE_IS_KVM		(S390_lowcore.machine_flags & MACHINE_FLAG_KVM)
#define MACHINE_IS_LPAR		(S390_lowcore.machine_flags & MACHINE_FLAG_LPAR)

#define MACHINE_HAS_DIAG9C	(S390_lowcore.machine_flags & MACHINE_FLAG_DIAG9C)
#define MACHINE_HAS_ESOP	(S390_lowcore.machine_flags & MACHINE_FLAG_ESOP)
#define MACHINE_HAS_IDTE	(S390_lowcore.machine_flags & MACHINE_FLAG_IDTE)
#define MACHINE_HAS_EDAT1	(S390_lowcore.machine_flags & MACHINE_FLAG_EDAT1)
#define MACHINE_HAS_EDAT2	(S390_lowcore.machine_flags & MACHINE_FLAG_EDAT2)
#define MACHINE_HAS_TOPOLOGY	(S390_lowcore.machine_flags & MACHINE_FLAG_TOPOLOGY)
#define MACHINE_HAS_TE		(S390_lowcore.machine_flags & MACHINE_FLAG_TE)
#define MACHINE_HAS_TLB_LC	(S390_lowcore.machine_flags & MACHINE_FLAG_TLB_LC)
#define MACHINE_HAS_VX		(S390_lowcore.machine_flags & MACHINE_FLAG_VX)
#define MACHINE_HAS_TLB_GUEST	(S390_lowcore.machine_flags & MACHINE_FLAG_TLB_GUEST)
#define MACHINE_HAS_NX		(S390_lowcore.machine_flags & MACHINE_FLAG_NX)
#define MACHINE_HAS_GS		(S390_lowcore.machine_flags & MACHINE_FLAG_GS)
#define MACHINE_HAS_SCC		(S390_lowcore.machine_flags & MACHINE_FLAG_SCC)

/*
 * Console mode. Override with conmode=
 */
extern unsigned int console_mode;
extern unsigned int console_devno;
extern unsigned int console_irq;

extern char vmhalt_cmd[];
extern char vmpoff_cmd[];

#define CONSOLE_IS_UNDEFINED	(console_mode == 0)
#define CONSOLE_IS_SCLP		(console_mode == 1)
#define CONSOLE_IS_3215		(console_mode == 2)
#define CONSOLE_IS_3270		(console_mode == 3)
#define CONSOLE_IS_VT220	(console_mode == 4)
#define CONSOLE_IS_HVC		(console_mode == 5)
#define SET_CONSOLE_SCLP	do { console_mode = 1; } while (0)
#define SET_CONSOLE_3215	do { console_mode = 2; } while (0)
#define SET_CONSOLE_3270	do { console_mode = 3; } while (0)
#define SET_CONSOLE_VT220	do { console_mode = 4; } while (0)
#define SET_CONSOLE_HVC		do { console_mode = 5; } while (0)

#ifdef CONFIG_PFAULT
extern int pfault_init(void);
extern void pfault_fini(void);
#else /* CONFIG_PFAULT */
#define pfault_init()		({-1;})
#define pfault_fini()		do { } while (0)
#endif /* CONFIG_PFAULT */

#ifdef CONFIG_VMCP
void vmcp_cma_reserve(void);
#else
static inline void vmcp_cma_reserve(void) { }
#endif

void report_user_fault(struct pt_regs *regs, long signr, int is_mm_fault);

void cmma_init(void);
void cmma_init_nodat(void);

extern void (*_machine_restart)(char *command);
extern void (*_machine_halt)(void);
extern void (*_machine_power_off)(void);

extern unsigned long __kaslr_offset;
static inline unsigned long kaslr_offset(void)
{
	return __kaslr_offset;
}

static inline u32 gen_lpswe(unsigned long addr)
{
	BUILD_BUG_ON(addr > 0xfff);
	return 0xb2b20000 | addr;
}

#else /* __ASSEMBLY__ */

#define IPL_DEVICE	(IPL_DEVICE_OFFSET)
#define INITRD_START	(INITRD_START_OFFSET)
#define INITRD_SIZE	(INITRD_SIZE_OFFSET)
#define OLDMEM_BASE	(OLDMEM_BASE_OFFSET)
#define OLDMEM_SIZE	(OLDMEM_SIZE_OFFSET)
#define COMMAND_LINE	(COMMAND_LINE_OFFSET)

#endif /* __ASSEMBLY__ */
#endif /* _ASM_S390_SETUP_H */
