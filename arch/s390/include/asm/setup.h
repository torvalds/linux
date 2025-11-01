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

#define PARMAREA		0x10400

#define COMMAND_LINE_SIZE CONFIG_COMMAND_LINE_SIZE

#define LPP_MAGIC		BIT(31)
#define LPP_PID_MASK		_AC(0xffffffff, UL)

/* Offsets to entry points in kernel/head.S  */

#define STARTUP_NORMAL_OFFSET	0x10000
#define STARTUP_KDUMP_OFFSET	0x10010

#define LEGACY_COMMAND_LINE_SIZE	896

#ifndef __ASSEMBLER__

#include <asm/lowcore.h>
#include <asm/types.h>

struct parmarea {
	unsigned long ipl_device;			/* 0x10400 */
	unsigned long initrd_start;			/* 0x10408 */
	unsigned long initrd_size;			/* 0x10410 */
	unsigned long oldmem_base;			/* 0x10418 */
	unsigned long oldmem_size;			/* 0x10420 */
	unsigned long kernel_version;			/* 0x10428 */
	unsigned long max_command_line_size;		/* 0x10430 */
	char pad1[0x10480-0x10438];			/* 0x10438 - 0x10480 */
	char command_line[COMMAND_LINE_SIZE];		/* 0x10480 */
};

extern char arch_hw_string[128];

extern struct parmarea parmarea;

extern unsigned int zlib_dfltcc_support;
#define ZLIB_DFLTCC_DISABLED		0
#define ZLIB_DFLTCC_FULL		1
#define ZLIB_DFLTCC_DEFLATE_ONLY	2
#define ZLIB_DFLTCC_INFLATE_ONLY	3
#define ZLIB_DFLTCC_FULL_DEBUG		4

extern unsigned long ident_map_size;
extern unsigned long max_mappable;

/* The Write Back bit position in the physaddr is given by the SLPC PCI */
extern unsigned long mio_wb_bit_mask;

/*
 * Console mode. Override with conmode=
 */
extern unsigned int console_mode;
extern unsigned int console_devno;
extern unsigned int console_irq;

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

void register_early_console(void);

#ifdef CONFIG_VMCP
void vmcp_cma_reserve(void);
#else
static inline void vmcp_cma_reserve(void) { }
#endif

void report_user_fault(struct pt_regs *regs, long signr, int is_mm_fault);

extern void (*_machine_restart)(char *command);
extern void (*_machine_halt)(void);
extern void (*_machine_power_off)(void);

struct oldmem_data {
	unsigned long start;
	unsigned long size;
};
extern struct oldmem_data oldmem_data;

static __always_inline u32 gen_lpswe(unsigned long addr)
{
	BUILD_BUG_ON(addr > 0xfff);
	return 0xb2b20000 | addr;
}
#endif /* __ASSEMBLER__ */
#endif /* _ASM_S390_SETUP_H */
