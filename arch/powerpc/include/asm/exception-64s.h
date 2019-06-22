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
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
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

/* PACA save area offsets (exgen, exmc, etc) */
#define EX_R9		0
#define EX_R10		8
#define EX_R11		16
#define EX_R12		24
#define EX_R13		32
#define EX_DAR		40
#define EX_DSISR	48
#define EX_CCR		52
#define EX_CFAR		56
#define EX_PPR		64
#if defined(CONFIG_RELOCATABLE)
#define EX_CTR		72
#define EX_SIZE		10	/* size in u64 units */
#else
#define EX_SIZE		9	/* size in u64 units */
#endif

/*
 * maximum recursive depth of MCE exceptions
 */
#define MAX_MCE_DEPTH	4

/*
 * EX_R3 is only used by the bad_stack handler. bad_stack reloads and
 * saves DAR from SPRN_DAR, and EX_DAR is not used. So EX_R3 can overlap
 * with EX_DAR.
 */
#define EX_R3		EX_DAR

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

/*
 * r10 must be free to use, r13 must be paca
 */
#define INTERRUPT_TO_KERNEL						\
	STF_ENTRY_BARRIER_SLOT

/*
 * Macros for annotating the expected destination of (h)rfid
 *
 * The nop instructions allow us to insert one or more instructions to flush the
 * L1-D cache when returning to userspace or a guest.
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

#endif /* __ASSEMBLY__ */

#endif	/* _ASM_POWERPC_EXCEPTION_H */
