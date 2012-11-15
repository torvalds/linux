/*
 * include/asm-xtensa/processor.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_PROCESSOR_H
#define _XTENSA_PROCESSOR_H

#include <variant/core.h>
#include <platform/hardware.h>

#include <linux/compiler.h>
#include <asm/ptrace.h>
#include <asm/types.h>
#include <asm/regs.h>

/* Assertions. */

#if (XCHAL_HAVE_WINDOWED != 1)
# error Linux requires the Xtensa Windowed Registers Option.
#endif

#define ARCH_SLAB_MINALIGN	XCHAL_DATA_WIDTH

/*
 * User space process size: 1 GB.
 * Windowed call ABI requires caller and callee to be located within the same
 * 1 GB region. The C compiler places trampoline code on the stack for sources
 * that take the address of a nested C function (a feature used by glibc), so
 * the 1 GB requirement applies to the stack as well.
 */

#ifdef CONFIG_MMU
#define TASK_SIZE	__XTENSA_UL_CONST(0x40000000)
#else
#define TASK_SIZE	(PLATFORM_DEFAULT_MEM_START + PLATFORM_DEFAULT_MEM_SIZE)
#endif

#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX	STACK_TOP

/*
 * General exception cause assigned to debug exceptions. Debug exceptions go
 * to their own vector, rather than the general exception vectors (user,
 * kernel, double); and their specific causes are reported via DEBUGCAUSE
 * rather than EXCCAUSE.  However it is sometimes convenient to redirect debug
 * exceptions to the general exception mechanism.  To do this, an otherwise
 * unused EXCCAUSE value was assigned to debug exceptions for this purpose.
 */

#define EXCCAUSE_MAPPED_DEBUG	63

/*
 * We use DEPC also as a flag to distinguish between double and regular
 * exceptions. For performance reasons, DEPC might contain the value of
 * EXCCAUSE for regular exceptions, so we use this definition to mark a
 * valid double exception address.
 * (Note: We use it in bgeui, so it should be 64, 128, or 256)
 */

#define VALID_DOUBLE_EXCEPTION_ADDRESS	64

/* LOCKLEVEL defines the interrupt level that masks all
 * general-purpose interrupts.
 */
#define LOCKLEVEL 1

/* WSBITS and WBBITS are the width of the WINDOWSTART and WINDOWBASE
 * registers
 */
#define WSBITS  (XCHAL_NUM_AREGS / 4)      /* width of WINDOWSTART in bits */
#define WBBITS  (XCHAL_NUM_AREGS_LOG2 - 2) /* width of WINDOWBASE in bits */

#ifndef __ASSEMBLY__

/* Build a valid return address for the specified call winsize.
 * winsize must be 1 (call4), 2 (call8), or 3 (call12)
 */
#define MAKE_RA_FOR_CALL(ra,ws)   (((ra) & 0x3fffffff) | (ws) << 30)

/* Convert return address to a valid pc
 * Note: We assume that the stack pointer is in the same 1GB ranges as the ra
 */
#define MAKE_PC_FROM_RA(ra,sp)    (((ra) & 0x3fffffff) | ((sp) & 0xc0000000))

typedef struct {
    unsigned long seg;
} mm_segment_t;

struct thread_struct {

	/* kernel's return address and stack pointer for context switching */
	unsigned long ra; /* kernel's a0: return address and window call size */
	unsigned long sp; /* kernel's a1: stack pointer */

	mm_segment_t current_ds;    /* see uaccess.h for example uses */

	/* struct xtensa_cpuinfo info; */

	unsigned long bad_vaddr; /* last user fault */
	unsigned long bad_uaddr; /* last kernel fault accessing user space */
	unsigned long error_code;

	unsigned long ibreak[XCHAL_NUM_IBREAK];
	unsigned long dbreaka[XCHAL_NUM_DBREAK];
	unsigned long dbreakc[XCHAL_NUM_DBREAK];

	/* Make structure 16 bytes aligned. */
	int align[0] __attribute__ ((aligned(16)));
};


/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr()  ({ __label__ _l; _l: &&_l;})


/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(TASK_SIZE / 2)

#define INIT_THREAD  \
{									\
	ra:		0, 						\
	sp:		sizeof(init_stack) + (long) &init_stack,	\
	current_ds:	{0},						\
	/*info:		{0}, */						\
	bad_vaddr:	0,						\
	bad_uaddr:	0,						\
	error_code:	0,						\
}


/*
 * Do necessary setup to start up a newly executed thread.
 * Note: We set-up ps as if we did a call4 to the new pc.
 *       set_thread_state in signal.c depends on it.
 */
#define USER_PS_VALUE ((1 << PS_WOE_BIT) |				\
                       (1 << PS_CALLINC_SHIFT) |			\
                       (USER_RING << PS_RING_SHIFT) |			\
                       (1 << PS_UM_BIT) |				\
                       (1 << PS_EXCM_BIT))

/* Clearing a0 terminates the backtrace. */
#define start_thread(regs, new_pc, new_sp) \
	memset(regs, 0, sizeof(*regs)); \
	regs->pc = new_pc; \
	regs->ps = USER_PS_VALUE; \
	regs->areg[1] = new_sp; \
	regs->areg[0] = 0; \
	regs->wmask = 1; \
	regs->depc = 0; \
	regs->windowbase = 0; \
	regs->windowstart = 1;

/* Forward declaration */
struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
#define release_thread(thread) do { } while(0)

/* Copy and release all segment info associated with a VM */
#define copy_segments(p, mm)	do { } while(0)
#define release_segments(mm)	do { } while(0)
#define forget_segments()	do { } while (0)

#define thread_saved_pc(tsk)	(task_pt_regs(tsk)->pc)

extern unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)		(task_pt_regs(tsk)->pc)
#define KSTK_ESP(tsk)		(task_pt_regs(tsk)->areg[1])

#define cpu_relax()  barrier()

/* Special register access. */

#define WSR(v,sr) __asm__ __volatile__ ("wsr %0,"__stringify(sr) :: "a"(v));
#define RSR(v,sr) __asm__ __volatile__ ("rsr %0,"__stringify(sr) : "=a"(v));

#define set_sr(x,sr) ({unsigned int v=(unsigned int)x; WSR(v,sr);})
#define get_sr(sr) ({unsigned int v; RSR(v,sr); v; })

#endif	/* __ASSEMBLY__ */
#endif	/* _XTENSA_PROCESSOR_H */
