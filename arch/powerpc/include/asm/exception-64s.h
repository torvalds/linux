/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_EXCEPTION_H
#define _ASM_POWERPC_EXCEPTION_H
/*
 * Extracted from head_64.S
 *
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen, Peter Bergner, and
 *    Mike Corrigan {engebret|bergner|mikejc}@us.ibm.com
 *
 *  This file contains the low-level support and setup for the
 *  PowerPC-64 platform, including trap and interrupt dispatch.
 */
/*
 * The following macros define the code that appears as
 * the prologue to each of the exception handlers.  They
 * are split into two parts to allow a single kernel binary
 * to be used for pSeries and iSeries.
 *
 * We make as much of the exception code common between native
 * exception handlers (including pSeries LPAR) and iSeries LPAR
 * implementations as possible.
 */
#include <asm/feature-fixups.h>

/* PACA save area size in u64 units (exgen, exmc, etc) */
#define EX_SIZE		10

/*
 * maximum recursive depth of MCE exceptions
 */
#define MAX_MCE_DEPTH	4

#ifdef __ASSEMBLY__

#define STF_ENTRY_BARRIER_SLOT						\
	STF_ENTRY_BARRIER_FIXUP_SECTION;				\
	nop;								\
	nop;								\
	nop

#define STF_EXIT_BARRIER_SLOT						\
	STF_EXIT_BARRIER_FIXUP_SECTION;					\
	nop;								\
	nop;								\
	nop;								\
	nop;								\
	nop;								\
	nop

#define ENTRY_FLUSH_SLOT						\
	ENTRY_FLUSH_FIXUP_SECTION;					\
	nop;								\
	nop;								\
	nop;

/*
 * r10 must be free to use, r13 must be paca
 */
#define INTERRUPT_TO_KERNEL						\
	STF_ENTRY_BARRIER_SLOT;						\
	ENTRY_FLUSH_SLOT

/*
 * Macros for annotating the expected destination of (h)rfid
 *
 * The nop instructions allow us to insert one or more instructions to flush the
 * L1-D cache when returning to userspace or a guest.
 *
 * powerpc relies on return from interrupt/syscall being context synchronising
 * (which hrfid, rfid, and rfscv are) to support ARCH_HAS_MEMBARRIER_SYNC_CORE
 * without additional synchronisation instructions.
 *
 * soft-masked interrupt replay does not include a context-synchronising rfid,
 * but those always return to kernel, the sync is only required when returning
 * to user.
 */
#define RFI_FLUSH_SLOT							\
	RFI_FLUSH_FIXUP_SECTION;					\
	nop;								\
	nop;								\
	nop

#define RFI_TO_KERNEL							\
	rfid

#define RFI_TO_USER							\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	rfid;								\
	b	rfi_flush_fallback

#define RFI_TO_USER_OR_KERNEL						\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	rfid;								\
	b	rfi_flush_fallback

#define RFI_TO_GUEST							\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	rfid;								\
	b	rfi_flush_fallback

#define HRFI_TO_KERNEL							\
	hrfid

#define HRFI_TO_USER							\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	hrfid;								\
	b	hrfi_flush_fallback

#define HRFI_TO_USER_OR_KERNEL						\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	hrfid;								\
	b	hrfi_flush_fallback

#define HRFI_TO_GUEST							\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	hrfid;								\
	b	hrfi_flush_fallback

#define HRFI_TO_UNKNOWN							\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	hrfid;								\
	b	hrfi_flush_fallback

#define RFSCV_TO_USER							\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	RFSCV;								\
	b	rfscv_flush_fallback

#else /* __ASSEMBLY__ */
/* Prototype for function defined in exceptions-64s.S */
void do_uaccess_flush(void);
#endif /* __ASSEMBLY__ */

#endif	/* _ASM_POWERPC_EXCEPTION_H */
