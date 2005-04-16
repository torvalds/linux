/* unaligned.c: unalignment fixup handler for CPUs on which it is supported (FR451 only)
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/user.h>
#include <linux/string.h>
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#if 0
#define kdebug(fmt, ...) printk("FDPIC "fmt"\n" ,##__VA_ARGS__ )
#else
#define kdebug(fmt, ...) do {} while(0)
#endif

#define _MA_SIGNED	0x01
#define _MA_HALF	0x02
#define _MA_WORD	0x04
#define _MA_DWORD	0x08
#define _MA_SZ_MASK	0x0e
#define _MA_LOAD	0x10
#define _MA_STORE	0x20
#define _MA_UPDATE	0x40
#define _MA_IMM		0x80

#define _MA_LDxU	_MA_LOAD | _MA_UPDATE
#define _MA_LDxI	_MA_LOAD | _MA_IMM
#define _MA_STxU	_MA_STORE | _MA_UPDATE
#define _MA_STxI	_MA_STORE | _MA_IMM

static const uint8_t tbl_LDGRk_reg[0x40] = {
	[0x02] = _MA_LOAD | _MA_HALF | _MA_SIGNED,	/* LDSH  @(GRi,GRj),GRk */
	[0x03] = _MA_LOAD | _MA_HALF,			/* LDUH  @(GRi,GRj),GRk */
	[0x04] = _MA_LOAD | _MA_WORD,			/* LD	 @(GRi,GRj),GRk */
	[0x05] = _MA_LOAD | _MA_DWORD,			/* LDD	 @(GRi,GRj),GRk */
	[0x12] = _MA_LDxU | _MA_HALF | _MA_SIGNED,	/* LDSHU @(GRi,GRj),GRk */
	[0x13] = _MA_LDxU | _MA_HALF,			/* LDUHU @(GRi,GRj),GRk */
	[0x14] = _MA_LDxU | _MA_WORD,			/* LDU	 @(GRi,GRj),GRk */
	[0x15] = _MA_LDxU | _MA_DWORD,			/* LDDU	 @(GRi,GRj),GRk */
};

static const uint8_t tbl_STGRk_reg[0x40] = {
	[0x01] = _MA_STORE | _MA_HALF,			/* STH   @(GRi,GRj),GRk */
	[0x02] = _MA_STORE | _MA_WORD,			/* ST	 @(GRi,GRj),GRk */
	[0x03] = _MA_STORE | _MA_DWORD,			/* STD	 @(GRi,GRj),GRk */
	[0x11] = _MA_STxU  | _MA_HALF,			/* STHU  @(GRi,GRj),GRk */
	[0x12] = _MA_STxU  | _MA_WORD,			/* STU	 @(GRi,GRj),GRk */
	[0x13] = _MA_STxU  | _MA_DWORD,			/* STDU	 @(GRi,GRj),GRk */
};

static const uint8_t tbl_LDSTGRk_imm[0x80] = {
	[0x31] = _MA_LDxI | _MA_HALF | _MA_SIGNED,	/* LDSHI @(GRi,d12),GRk */
	[0x32] = _MA_LDxI | _MA_WORD,			/* LDI   @(GRi,d12),GRk */
	[0x33] = _MA_LDxI | _MA_DWORD,			/* LDDI  @(GRi,d12),GRk */
	[0x36] = _MA_LDxI | _MA_HALF,			/* LDUHI @(GRi,d12),GRk */
	[0x51] = _MA_STxI | _MA_HALF,			/* STHI  @(GRi,d12),GRk */
	[0x52] = _MA_STxI | _MA_WORD,			/* STI   @(GRi,d12),GRk */
	[0x53] = _MA_STxI | _MA_DWORD,			/* STDI  @(GRi,d12),GRk */
};


/*****************************************************************************/
/*
 * see if we can handle the exception by fixing up a misaligned memory access
 */
int handle_misalignment(unsigned long esr0, unsigned long ear0, unsigned long epcr0)
{
	unsigned long insn, addr, *greg;
	int GRi, GRj, GRk, D12, op;

	union {
		uint64_t _64;
		uint32_t _32[2];
		uint16_t _16;
		uint8_t _8[8];
	} x;

	if (!(esr0 & ESR0_EAV) || !(epcr0 & EPCR0_V) || !(ear0 & 7))
		return -EAGAIN;

	epcr0 &= EPCR0_PC;

	if (__frame->pc != epcr0) {
		kdebug("MISALIGN: Execution not halted on excepting instruction\n");
		BUG();
	}

	if (__get_user(insn, (unsigned long *) epcr0) < 0)
		return -EFAULT;

	/* determine the instruction type first */
	switch ((insn >> 18) & 0x7f) {
	case 0x2:
		/* LDx @(GRi,GRj),GRk */
		op = tbl_LDGRk_reg[(insn >> 6) & 0x3f];
		break;

	case 0x3:
		/* STx GRk,@(GRi,GRj) */
		op = tbl_STGRk_reg[(insn >> 6) & 0x3f];
		break;

	default:
		op = tbl_LDSTGRk_imm[(insn >> 18) & 0x7f];
		break;
	}

	if (!op)
		return -EAGAIN;

	kdebug("MISALIGN: pc=%08lx insn=%08lx ad=%08lx op=%02x\n", epcr0, insn, ear0, op);

	memset(&x, 0xba, 8);

	/* validate the instruction parameters */
	greg = (unsigned long *) &__frame->tbr;

	GRi = (insn >> 12) & 0x3f;
	GRk = (insn >> 25) & 0x3f;

	if (GRi > 31 || GRk > 31)
		return -ENOENT;

	if (op & _MA_DWORD && GRk & 1)
		return -EINVAL;

	if (op & _MA_IMM) {
		D12 = insn & 0xfff;
		asm ("slli %0,#20,%0 ! srai %0,#20,%0" : "=r"(D12) : "0"(D12)); /* sign extend */
		addr = (GRi ? greg[GRi] : 0) + D12;
	}
	else {
		GRj = (insn >>  0) & 0x3f;
		if (GRj > 31)
			return -ENOENT;
		addr = (GRi ? greg[GRi] : 0) + (GRj ? greg[GRj] : 0);
	}

	if (addr != ear0) {
		kdebug("MISALIGN: Calculated addr (%08lx) does not match EAR0 (%08lx)\n",
		       addr, ear0);
		return -EFAULT;
	}

	/* check the address is okay */
	if (user_mode(__frame) && ___range_ok(ear0, 8) < 0)
		return -EFAULT;

	/* perform the memory op */
	if (op & _MA_STORE) {
		/* perform a store */
		x._32[0] = 0;
		if (GRk != 0) {
			if (op & _MA_HALF) {
				x._16 = greg[GRk];
			}
			else {
				x._32[0] = greg[GRk];
			}
		}
		if (op & _MA_DWORD)
			x._32[1] = greg[GRk + 1];

		kdebug("MISALIGN: Store GR%d { %08x:%08x } -> %08lx (%dB)\n",
		       GRk, x._32[1], x._32[0], addr, op & _MA_SZ_MASK);

		if (__memcpy_user((void *) addr, &x, op & _MA_SZ_MASK) != 0)
			return -EFAULT;
	}
	else {
		/* perform a load */
		if (__memcpy_user(&x, (void *) addr, op & _MA_SZ_MASK) != 0)
			return -EFAULT;

		if (op & _MA_HALF) {
			if (op & _MA_SIGNED)
				asm ("slli %0,#16,%0 ! srai %0,#16,%0"
				     : "=r"(x._32[0]) : "0"(x._16));
			else
				asm ("sethi #0,%0"
				     : "=r"(x._32[0]) : "0"(x._16));
		}

		kdebug("MISALIGN: Load %08lx (%dB) -> GR%d, { %08x:%08x }\n",
		       addr, op & _MA_SZ_MASK, GRk, x._32[1], x._32[0]);

		if (GRk != 0)
			greg[GRk] = x._32[0];
		if (op & _MA_DWORD)
			greg[GRk + 1] = x._32[1];
	}

	/* update the base pointer if required */
	if (op & _MA_UPDATE)
		greg[GRi] = addr;

	/* well... we've done that insn */
	__frame->pc = __frame->pc + 4;

	return 0;
} /* end handle_misalignment() */
