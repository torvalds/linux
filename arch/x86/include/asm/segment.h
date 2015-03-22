#ifndef _ASM_X86_SEGMENT_H
#define _ASM_X86_SEGMENT_H

#include <linux/const.h>

/* Constructor for a conventional segment GDT (or LDT) entry */
/* This is a macro so it can be used in initializers */
#define GDT_ENTRY(flags, base, limit)			\
	((((base)  & _AC(0xff000000,ULL)) << (56-24)) |	\
	 (((flags) & _AC(0x0000f0ff,ULL)) << 40) |	\
	 (((limit) & _AC(0x000f0000,ULL)) << (48-16)) |	\
	 (((base)  & _AC(0x00ffffff,ULL)) << 16) |	\
	 (((limit) & _AC(0x0000ffff,ULL))))

/* Simple and small GDT entries for booting only */

#define GDT_ENTRY_BOOT_CS	2
#define GDT_ENTRY_BOOT_DS	3
#define GDT_ENTRY_BOOT_TSS	4
#define __BOOT_CS		(GDT_ENTRY_BOOT_CS * 8)
#define __BOOT_DS		(GDT_ENTRY_BOOT_DS * 8)
#define __BOOT_TSS		(GDT_ENTRY_BOOT_TSS * 8)

#define SEGMENT_RPL_MASK	0x3 /*
				     * Bottom two bits of selector give the ring
				     * privilege level
				     */
#define SEGMENT_TI_MASK		0x4 /* Bit 2 is table indicator (LDT/GDT) */
#define USER_RPL		0x3 /* User mode is privilege level 3 */
#define SEGMENT_LDT		0x4 /* LDT segment has TI set... */
#define SEGMENT_GDT		0x0 /* ... GDT has it cleared */

#ifdef CONFIG_X86_32
/*
 * The layout of the per-CPU GDT under Linux:
 *
 *   0 - null
 *   1 - reserved
 *   2 - reserved
 *   3 - reserved
 *
 *   4 - unused			<==== new cacheline
 *   5 - unused
 *
 *  ------- start of TLS (Thread-Local Storage) segments:
 *
 *   6 - TLS segment #1			[ glibc's TLS segment ]
 *   7 - TLS segment #2			[ Wine's %fs Win32 segment ]
 *   8 - TLS segment #3
 *   9 - reserved
 *  10 - reserved
 *  11 - reserved
 *
 *  ------- start of kernel segments:
 *
 *  12 - kernel code segment		<==== new cacheline
 *  13 - kernel data segment
 *  14 - default user CS
 *  15 - default user DS
 *  16 - TSS
 *  17 - LDT
 *  18 - PNPBIOS support (16->32 gate)
 *  19 - PNPBIOS support
 *  20 - PNPBIOS support
 *  21 - PNPBIOS support
 *  22 - PNPBIOS support
 *  23 - APM BIOS support
 *  24 - APM BIOS support
 *  25 - APM BIOS support
 *
 *  26 - ESPFIX small SS
 *  27 - per-cpu			[ offset to per-cpu data area ]
 *  28 - stack_canary-20		[ for stack protector ]
 *  29 - unused
 *  30 - unused
 *  31 - TSS for double fault handler
 */
#define GDT_ENTRY_TLS_MIN	6
#define GDT_ENTRY_TLS_MAX 	(GDT_ENTRY_TLS_MIN + GDT_ENTRY_TLS_ENTRIES - 1)

#define GDT_ENTRY_KERNEL_CS		12
#define GDT_ENTRY_KERNEL_DS		13
#define GDT_ENTRY_DEFAULT_USER_CS	14
#define GDT_ENTRY_DEFAULT_USER_DS	15
#define GDT_ENTRY_TSS			16
#define GDT_ENTRY_LDT			17
#define GDT_ENTRY_PNPBIOS_CS32		18
#define GDT_ENTRY_PNPBIOS_CS16		19
#define GDT_ENTRY_PNPBIOS_DS		20
#define GDT_ENTRY_PNPBIOS_TS1		21
#define GDT_ENTRY_PNPBIOS_TS2		22
#define GDT_ENTRY_APMBIOS_BASE		23

#define GDT_ENTRY_ESPFIX_SS		26
#define GDT_ENTRY_PERCPU		27
#define GDT_ENTRY_STACK_CANARY		28

#define GDT_ENTRY_DOUBLEFAULT_TSS	31

#define __KERNEL_CS			(GDT_ENTRY_KERNEL_CS*8)
#define __KERNEL_DS			(GDT_ENTRY_KERNEL_DS*8)
#define __USER_DS			(GDT_ENTRY_DEFAULT_USER_DS*8+3)
#define __USER_CS			(GDT_ENTRY_DEFAULT_USER_CS*8+3)
#define __ESPFIX_SS			(GDT_ENTRY_ESPFIX_SS*8)
#define PNP_CS32   (GDT_ENTRY_PNPBIOS_CS32 * 8) /* segment for calling fn */
#define PNP_CS16   (GDT_ENTRY_PNPBIOS_CS16 * 8) /* code segment for BIOS */
/* "Is this PNP code selector (PNP_CS32 or PNP_CS16)?" */
#define SEGMENT_IS_PNP_CODE(x)   (((x) & 0xf4) == PNP_CS32)
#define PNP_DS     (GDT_ENTRY_PNPBIOS_DS * 8)	/* data segment for BIOS */
#define PNP_TS1    (GDT_ENTRY_PNPBIOS_TS1 * 8)	/* transfer data segment */
#define PNP_TS2    (GDT_ENTRY_PNPBIOS_TS2 * 8)	/* another data segment */
#ifdef CONFIG_SMP
#define __KERNEL_PERCPU			(GDT_ENTRY_PERCPU*8)
#else
#define __KERNEL_PERCPU			0
#endif
#ifdef CONFIG_CC_STACKPROTECTOR
#define __KERNEL_STACK_CANARY		(GDT_ENTRY_STACK_CANARY*8)
#else
#define __KERNEL_STACK_CANARY		0
#endif

#define GDT_ENTRIES 32

#else /* 64-bit: */

#include <asm/cache.h>

#define GDT_ENTRY_KERNEL32_CS	1
#define GDT_ENTRY_KERNEL_CS	2
#define GDT_ENTRY_KERNEL_DS	3
/*
 * we cannot use the same code segment descriptor for user and kernel
 * -- not even in the long flat mode, because of different DPL /kkeil
 * GDT layout to get 64bit syscall/sysret right. sysret hardcodes selectors:
 * if returning to 32-bit userspace: cs = STAR.SYSRET_CS,
 * if returning to 64-bit userspace: cs = STAR.SYSRET_CS+16,
 * ss = STAR.SYSRET_CS+8 (in either case)
 * thus USER_DS should be between 32-bit and 64-bit code selectors:
 */
#define GDT_ENTRY_DEFAULT_USER32_CS 4
#define GDT_ENTRY_DEFAULT_USER_DS 5
#define GDT_ENTRY_DEFAULT_USER_CS 6

#define GDT_ENTRY_TSS		8  /* needs two entries */
#define GDT_ENTRY_LDT		10 /* needs two entries */
#define GDT_ENTRY_TLS_MIN	12
#define GDT_ENTRY_TLS_MAX	14

#define GDT_ENTRY_PER_CPU	15 /* abused to load per CPU data from limit */

/* Selectors need to also have a correct RPL (+3 thingy) */
#define __KERNEL_CS	(GDT_ENTRY_KERNEL_CS*8)
#define __KERNEL_DS	(GDT_ENTRY_KERNEL_DS*8)
#define __USER_DS	(GDT_ENTRY_DEFAULT_USER_DS*8+3)
#define __USER_CS	(GDT_ENTRY_DEFAULT_USER_CS*8+3)
#define __KERNEL32_CS	(GDT_ENTRY_KERNEL32_CS*8)
#define __USER32_CS	(GDT_ENTRY_DEFAULT_USER32_CS*8+3)
#define __USER32_DS	__USER_DS
#define __PER_CPU_SEG	(GDT_ENTRY_PER_CPU*8+3)

/* TLS indexes for 64bit - hardcoded in arch_prctl */
#define FS_TLS 0
#define GS_TLS 1

#define GS_TLS_SEL ((GDT_ENTRY_TLS_MIN+GS_TLS)*8 + 3)
#define FS_TLS_SEL ((GDT_ENTRY_TLS_MIN+FS_TLS)*8 + 3)

#define GDT_ENTRIES 16

#endif

#ifndef CONFIG_PARAVIRT
#define get_kernel_rpl()  0
#endif

#define IDT_ENTRIES 256
#define NUM_EXCEPTION_VECTORS 32
/* Bitmask of exception vectors which push an error code on the stack */
#define EXCEPTION_ERRCODE_MASK  0x00027d00
#define GDT_SIZE (GDT_ENTRIES * 8)
#define GDT_ENTRY_TLS_ENTRIES 3
#define TLS_SIZE (GDT_ENTRY_TLS_ENTRIES * 8)

#ifdef __KERNEL__
#ifndef __ASSEMBLY__
extern const char early_idt_handlers[NUM_EXCEPTION_VECTORS][2+2+5];
#ifdef CONFIG_TRACING
#define trace_early_idt_handlers early_idt_handlers
#endif

/*
 * Load a segment. Fall back on loading the zero
 * segment if something goes wrong..
 */
#define loadsegment(seg, value)						\
do {									\
	unsigned short __val = (value);					\
									\
	asm volatile("						\n"	\
		     "1:	movl %k0,%%" #seg "		\n"	\
									\
		     ".section .fixup,\"ax\"			\n"	\
		     "2:	xorl %k0,%k0			\n"	\
		     "		jmp 1b				\n"	\
		     ".previous					\n"	\
									\
		     _ASM_EXTABLE(1b, 2b)				\
									\
		     : "+r" (__val) : : "memory");			\
} while (0)

/*
 * Save a segment register away
 */
#define savesegment(seg, value)				\
	asm("mov %%" #seg ",%0":"=r" (value) : : "memory")

/*
 * x86_32 user gs accessors.
 */
#ifdef CONFIG_X86_32
#ifdef CONFIG_X86_32_LAZY_GS
#define get_user_gs(regs)	(u16)({unsigned long v; savesegment(gs, v); v;})
#define set_user_gs(regs, v)	loadsegment(gs, (unsigned long)(v))
#define task_user_gs(tsk)	((tsk)->thread.gs)
#define lazy_save_gs(v)		savesegment(gs, (v))
#define lazy_load_gs(v)		loadsegment(gs, (v))
#else	/* X86_32_LAZY_GS */
#define get_user_gs(regs)	(u16)((regs)->gs)
#define set_user_gs(regs, v)	do { (regs)->gs = (v); } while (0)
#define task_user_gs(tsk)	(task_pt_regs(tsk)->gs)
#define lazy_save_gs(v)		do { } while (0)
#define lazy_load_gs(v)		do { } while (0)
#endif	/* X86_32_LAZY_GS */
#endif	/* X86_32 */

static inline unsigned long get_limit(unsigned long segment)
{
	unsigned long __limit;
	asm("lsll %1,%0" : "=r" (__limit) : "r" (segment));
	return __limit + 1;
}

#endif /* !__ASSEMBLY__ */
#endif /* __KERNEL__ */

#endif /* _ASM_X86_SEGMENT_H */
