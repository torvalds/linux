###############################################################################
#
# MN10300 Watchdog interrupt handler
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
#include <asm/intctl-regs.h>
#include <asm/timer-regs.h>
#include <asm/frame.inc>
#include <linux/threads.h>

	.text

###############################################################################
#
# Watchdog handler entry point
# - special non-maskable interrupt
#
###############################################################################
	.globl	watchdog_handler
	.type	watchdog_handler,@function
watchdog_handler:
	add	-4,sp
	SAVE_ALL

	mov	0xffffffff,d0
	mov	d0,(REG_ORIG_D0,fp)

	mov	fp,d0
	lsr	2,d1
	call	watchdog_interrupt[],0		# watchdog_interrupt(regs,irq)

	jmp	ret_from_intr

	.size	watchdog_handler,.-watchdog_handler

###############################################################################
#
# Watchdog touch entry point
# - kept to absolute minimum (unfortunately, it's prototyped in linux/nmi.h so
#   we can't inline it)
#
###############################################################################
	.globl	touch_nmi_watchdog
	.type	touch_nmi_watchdog,@function
touch_nmi_watchdog:
	clr	d0
	clr	d1
	mov	watchdog_alert_counter, a0
	setlb
	mov	d0, (a0+)
	inc	d1
	cmp	NR_CPUS, d1
	lne
	ret	[],0

	.size	touch_nmi_watchdog,.-touch_nmi_watchdog
