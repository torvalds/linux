/* SMP IPI low-level handler
 *
 * Copyright (C) 2006-2007 Matsushita Electric Industrial Co., Ltd.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/smp.h>
#include <asm/thread_info.h>
#include <asm/cpu-regs.h>
#include <asm/intctl-regs.h>
#include <proc/smp-regs.h>
#include <asm/asm-offsets.h>
#include <asm/frame.inc>

	.am33_2

###############################################################################
#
# IPI interrupt handler
#
###############################################################################
	.globl mn10300_low_ipi_handler
mn10300_low_ipi_handler:
	add	-4,sp
	mov	d0,(sp)
	movhu	(IAGR),d0
	and	IAGR_GN,d0
	lsr	0x2,d0
#ifdef CONFIG_MN10300_CACHE_ENABLED
	cmp	FLUSH_CACHE_IPI,d0
	beq	mn10300_flush_cache_ipi
#endif
	cmp	SMP_BOOT_IRQ,d0
	beq	mn10300_smp_boot_ipi
	/* OTHERS */
	mov	(sp),d0
	add	4,sp
#ifdef CONFIG_GDBSTUB
	jmp	gdbstub_io_rx_handler
#else
	jmp	end
#endif

###############################################################################
#
# Cache flush IPI interrupt handler
#
###############################################################################
#ifdef CONFIG_MN10300_CACHE_ENABLED
mn10300_flush_cache_ipi:
	mov	(sp),d0
	add	4,sp

	/* FLUSH_CACHE_IPI */
	add	-4,sp
	SAVE_ALL
	mov	GxICR_DETECT,d2
	movbu	d2,(GxICR(FLUSH_CACHE_IPI))	# ACK the interrupt
	movhu	(GxICR(FLUSH_CACHE_IPI)),d2
	call	smp_cache_interrupt[],0
	RESTORE_ALL
	jmp	end
#endif

###############################################################################
#
# SMP boot CPU IPI interrupt handler
#
###############################################################################
mn10300_smp_boot_ipi:
	/* clear interrupt */
	movhu	(GxICR(SMP_BOOT_IRQ)),d0
	and	~GxICR_REQUEST,d0
	movhu	d0,(GxICR(SMP_BOOT_IRQ))
	mov	(sp),d0
	add	4,sp

	# get stack
	mov	(CPUID),a0
	add	-1,a0
	add	a0,a0
	add	a0,a0
	mov	(start_stack,a0),a0
	mov	a0,sp
	jmp	initialize_secondary


# Jump here after RTI to suppress the icache lookahead
end:
