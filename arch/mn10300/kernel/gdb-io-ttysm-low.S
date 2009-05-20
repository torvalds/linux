###############################################################################
#
# MN10300 On-chip serial Rx interrupt handler for GDB stub I/O
#
# Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
# Written by David Howells (dhowells@redhat.com)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public Licence
# as published by the Free Software Foundation; either version
# 2 of the Licence, or (at your option) any later version.
#
###############################################################################
#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/smp.h>
#include <asm/thread_info.h>
#include <asm/cpu-regs.h>
#include <asm/frame.inc>
#include <asm/intctl-regs.h>
#include <unit/serial.h>
#include "mn10300-serial.h"

	.text

###############################################################################
#
# GDB stub serial receive interrupt entry point
# - intended to run at interrupt priority 0
#
###############################################################################
	.globl	gdbstub_io_rx_handler
	.type	gdbstub_io_rx_handler,@function
gdbstub_io_rx_handler:
	movm	[d2,d3,a2,a3],(sp)

	mov	(gdbstub_rx_inp),a3
gdbstub_io_rx_more:
	mov	a3,a2
	add	2,a3
	and	PAGE_SIZE_asm-1,a3
	mov	(gdbstub_rx_outp),d3
	cmp	a3,d3
	beq	gdbstub_io_rx_overflow

	movbu	(SCgSTR),d3
	btst	SC01STR_RBF,d3
	beq	gdbstub_io_rx_done
	movbu	(SCgRXB),d2
	movbu	d3,(gdbstub_rx_buffer+1,a2)
	movbu	d2,(gdbstub_rx_buffer,a2)
	mov	a3,(gdbstub_rx_inp)
	bra	gdbstub_io_rx_more

gdbstub_io_rx_done:
	mov	GxICR_DETECT,d2
	movbu	d2,(GxICR(SCgRXIRQ))	# ACK the interrupt
	movhu	(GxICR(SCgRXIRQ)),d2	# flush

	movm	(sp),[d2,d3,a2,a3]
	bset	0x01,(gdbstub_busy)
	beq	gdbstub_io_rx_enter
	rti

gdbstub_io_rx_overflow:
	bset	0x01,(gdbstub_rx_overflow)
	bra	gdbstub_io_rx_done

###############################################################################
#
# debugging interrupt - enter the GDB stub proper
#
###############################################################################
gdbstub_io_rx_enter:
	or	EPSW_IE|EPSW_IM_1,epsw
	add	-4,sp
	SAVE_ALL

	mov	0xffffffff,d0
	mov	d0,(REG_ORIG_D0,fp)
	mov	0x280,d1

	mov	fp,d0
	call	gdbstub_rx_irq[],0	# gdbstub_io_rx_irq(regs,excep)

	and	~EPSW_IE,epsw
	bclr	0x01,(gdbstub_busy)

	.globl gdbstub_return
gdbstub_return:
	RESTORE_ALL

	.size	gdbstub_io_rx_handler,.-gdbstub_io_rx_handler
