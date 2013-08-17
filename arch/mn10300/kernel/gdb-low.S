###############################################################################
#
# MN10300 Low-level gdbstub routines
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
#include <asm/cache.h>
#include <asm/cpu-regs.h>
#include <asm/exceptions.h>
#include <asm/frame.inc>
#include <asm/serial-regs.h>

	.text

###############################################################################
#
# GDB stub read memory with guard
# - D0 holds the memory address to read
# - D1 holds the address to store the byte into
#
###############################################################################
	.globl gdbstub_read_byte_guard
	.globl gdbstub_read_byte_cont
ENTRY(gdbstub_read_byte)
	mov	d0,a0
	mov	d1,a1
	clr	d0
gdbstub_read_byte_guard:
	movbu	(a0),d1
gdbstub_read_byte_cont:
	movbu	d1,(a1)
	ret	[],0

	.globl gdbstub_read_word_guard
	.globl gdbstub_read_word_cont
ENTRY(gdbstub_read_word)
	mov	d0,a0
	mov	d1,a1
	clr	d0
gdbstub_read_word_guard:
	movhu	(a0),d1
gdbstub_read_word_cont:
	movhu	d1,(a1)
	ret	[],0

	.globl gdbstub_read_dword_guard
	.globl gdbstub_read_dword_cont
ENTRY(gdbstub_read_dword)
	mov	d0,a0
	mov	d1,a1
	clr	d0
gdbstub_read_dword_guard:
	mov	(a0),d1
gdbstub_read_dword_cont:
	mov	d1,(a1)
	ret	[],0

###############################################################################
#
# GDB stub write memory with guard
# - D0 holds the byte to store
# - D1 holds the memory address to write
#
###############################################################################
	.globl gdbstub_write_byte_guard
	.globl gdbstub_write_byte_cont
ENTRY(gdbstub_write_byte)
	mov	d0,a0
	mov	d1,a1
	clr	d0
gdbstub_write_byte_guard:
	movbu	a0,(a1)
gdbstub_write_byte_cont:
	ret	[],0

	.globl gdbstub_write_word_guard
	.globl gdbstub_write_word_cont
ENTRY(gdbstub_write_word)
	mov	d0,a0
	mov	d1,a1
	clr	d0
gdbstub_write_word_guard:
	movhu	a0,(a1)
gdbstub_write_word_cont:
	ret	[],0

	.globl gdbstub_write_dword_guard
	.globl gdbstub_write_dword_cont
ENTRY(gdbstub_write_dword)
	mov	d0,a0
	mov	d1,a1
	clr	d0
gdbstub_write_dword_guard:
	mov	a0,(a1)
gdbstub_write_dword_cont:
	ret	[],0

###############################################################################
#
# GDB stub BUG() trap
#
###############################################################################
ENTRY(__gdbstub_bug_trap)
	.byte	0xF7,0xF7	# don't use 0xFF as the JTAG unit preempts that
	ret	[],0
