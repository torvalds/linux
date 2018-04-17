/*
 * arch/xtensa/kernel/align.S
 *
 * Handle unalignment exceptions in kernel space.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica, Inc.
 * Copyright (C) 2014 Cadence Design Systems Inc.
 *
 * Rewritten by Chris Zankel <chris@zankel.net>
 *
 * Based on work from Joe Taylor <joe@tensilica.com, joetylr@yahoo.com>
 * and Marc Gauthier <marc@tensilica.com, marc@alimni.uwaterloo.ca>
 */

#include <linux/linkage.h>
#include <asm/current.h>
#include <asm/asm-offsets.h>
#include <asm/asmmacro.h>
#include <asm/processor.h>

#if XCHAL_UNALIGNED_LOAD_EXCEPTION || XCHAL_UNALIGNED_STORE_EXCEPTION

/*  First-level exception handler for unaligned exceptions.
 *
 *  Note: This handler works only for kernel exceptions.  Unaligned user
 *        access should get a seg fault.
 */

/* Big and little endian 16-bit values are located in
 * different halves of a register.  HWORD_START helps to
 * abstract the notion of extracting a 16-bit value from a
 * register.
 * We also have to define new shifting instructions because
 * lsb and msb are on 'opposite' ends in a register for
 * different endian machines.
 *
 * Assume a memory region in ascending address:
 *   	0 1 2 3|4 5 6 7
 *
 * When loading one word into a register, the content of that register is:
 *  LE	3 2 1 0, 7 6 5 4
 *  BE  0 1 2 3, 4 5 6 7
 *
 * Masking the bits of the higher/lower address means:
 *  LE  X X 0 0, 0 0 X X
 *  BE	0 0 X X, X X 0 0
 *
 * Shifting to higher/lower addresses, means:
 *  LE  shift left / shift right
 *  BE  shift right / shift left
 *
 * Extracting 16 bits from a 32 bit reg. value to higher/lower address means:
 *  LE  mask 0 0 X X / shift left
 *  BE  shift left / mask 0 0 X X
 */

#define UNALIGNED_USER_EXCEPTION

#if XCHAL_HAVE_BE

#define HWORD_START	16
#define	INSN_OP0	28
#define	INSN_T		24
#define	INSN_OP1	16

.macro __ssa8r	r;		ssa8l	\r;		.endm
.macro __sh	r, s;		srl	\r, \s;		.endm
.macro __sl	r, s;		sll	\r, \s;		.endm
.macro __exth	r, s;		extui	\r, \s, 0, 16;	.endm
.macro __extl	r, s;		slli	\r, \s, 16;	.endm

#else

#define HWORD_START	0
#define	INSN_OP0	0
#define	INSN_T		4
#define	INSN_OP1	12

.macro __ssa8r	r;		ssa8b	\r;		.endm
.macro __sh	r, s;		sll	\r, \s;		.endm
.macro __sl	r, s;		srl	\r, \s;		.endm
.macro __exth	r, s;		slli	\r, \s, 16;	.endm
.macro __extl	r, s;		extui	\r, \s, 0, 16;	.endm

#endif

/*
 *	xxxx xxxx = imm8 field
 *	     yyyy = imm4 field
 *	     ssss = s field
 *	     tttt = t field
 *
 *	       		 16		    0
 *		          -------------------
 *	L32I.N		  yyyy ssss tttt 1000
 *	S32I.N	          yyyy ssss tttt 1001
 *
 *	       23			    0
 *		-----------------------------
 *	res	          0000           0010
 *	L16UI	xxxx xxxx 0001 ssss tttt 0010
 *	L32I	xxxx xxxx 0010 ssss tttt 0010
 *	XXX	          0011 ssss tttt 0010
 *	XXX	          0100 ssss tttt 0010
 *	S16I	xxxx xxxx 0101 ssss tttt 0010
 *	S32I	xxxx xxxx 0110 ssss tttt 0010
 *	XXX	          0111 ssss tttt 0010
 *	XXX	          1000 ssss tttt 0010
 *	L16SI	xxxx xxxx 1001 ssss tttt 0010
 *	XXX	          1010           0010
 *      **L32AI	xxxx xxxx 1011 ssss tttt 0010 unsupported
 *	XXX	          1100           0010
 *	XXX	          1101           0010
 *	XXX	          1110           0010
 *	**S32RI	xxxx xxxx 1111 ssss tttt 0010 unsupported
 *		-----------------------------
 *                           ^         ^    ^
 *    sub-opcode (NIBBLE_R) -+         |    |
 *       t field (NIBBLE_T) -----------+    |
 *  major opcode (NIBBLE_OP0) --------------+
 */

#define OP0_L32I_N	0x8		/* load immediate narrow */
#define OP0_S32I_N	0x9		/* store immediate narrow */
#define OP1_SI_MASK	0x4		/* OP1 bit set for stores */
#define OP1_SI_BIT	2		/* OP1 bit number for stores */

#define OP1_L32I	0x2
#define OP1_L16UI	0x1
#define OP1_L16SI	0x9
#define OP1_L32AI	0xb

#define OP1_S32I	0x6
#define OP1_S16I	0x5
#define OP1_S32RI	0xf

/*
 * Entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	a3
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	dispatch table
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 */

	.literal_position
ENTRY(fast_unaligned)

	/* Note: We don't expect the address to be aligned on a word
	 *       boundary. After all, the processor generated that exception
	 *       and it would be a hardware fault.
	 */

	/* Save some working register */

	s32i	a4, a2, PT_AREG4
	s32i	a5, a2, PT_AREG5
	s32i	a6, a2, PT_AREG6
	s32i	a7, a2, PT_AREG7
	s32i	a8, a2, PT_AREG8

	rsr	a0, depc
	s32i	a0, a2, PT_AREG2
	s32i	a3, a2, PT_AREG3

	rsr	a3, excsave1
	movi	a4, fast_unaligned_fixup
	s32i	a4, a3, EXC_TABLE_FIXUP

	/* Keep value of SAR in a0 */

	rsr	a0, sar
	rsr	a8, excvaddr		# load unaligned memory address

	/* Now, identify one of the following load/store instructions.
	 *
	 * The only possible danger of a double exception on the
	 * following l32i instructions is kernel code in vmalloc
	 * memory. The processor was just executing at the EPC_1
	 * address, and indeed, already fetched the instruction.  That
	 * guarantees a TLB mapping, which hasn't been replaced by
	 * this unaligned exception handler that uses only static TLB
	 * mappings. However, high-level interrupt handlers might
	 * modify TLB entries, so for the generic case, we register a
	 * TABLE_FIXUP handler here, too.
	 */

	/* a3...a6 saved on stack, a2 = SP */

	/* Extract the instruction that caused the unaligned access. */

	rsr	a7, epc1	# load exception address
	movi	a3, ~3
	and	a3, a3, a7	# mask lower bits

	l32i	a4, a3, 0	# load 2 words
	l32i	a5, a3, 4

	__ssa8	a7
	__src_b	a4, a4, a5	# a4 has the instruction

	/* Analyze the instruction (load or store?). */

	extui	a5, a4, INSN_OP0, 4	# get insn.op0 nibble

#if XCHAL_HAVE_DENSITY
	_beqi	a5, OP0_L32I_N, .Lload	# L32I.N, jump
	addi	a6, a5, -OP0_S32I_N
	_beqz	a6, .Lstore		# S32I.N, do a store
#endif
	/* 'store indicator bit' not set, jump */
	_bbci.l	a4, OP1_SI_BIT + INSN_OP1, .Lload

	/* Store: Jump to table entry to get the value in the source register.*/

.Lstore:movi	a5, .Lstore_table	# table
	extui	a6, a4, INSN_T, 4	# get source register
	addx8	a5, a6, a5
	jx	a5			# jump into table

	/* Load: Load memory address. */

.Lload: movi	a3, ~3
	and	a3, a3, a8		# align memory address

	__ssa8	a8
#ifdef UNALIGNED_USER_EXCEPTION
	addi	a3, a3, 8
	l32e	a5, a3, -8
	l32e	a6, a3, -4
#else
	l32i	a5, a3, 0
	l32i	a6, a3, 4
#endif
	__src_b	a3, a5, a6		# a3 has the data word

#if XCHAL_HAVE_DENSITY
	addi	a7, a7, 2		# increment PC (assume 16-bit insn)

	extui	a5, a4, INSN_OP0, 4
	_beqi	a5, OP0_L32I_N, 1f	# l32i.n: jump

	addi	a7, a7, 1
#else
	addi	a7, a7, 3
#endif

	extui	a5, a4, INSN_OP1, 4
	_beqi	a5, OP1_L32I, 1f	# l32i: jump

	extui	a3, a3, 0, 16		# extract lower 16 bits
	_beqi	a5, OP1_L16UI, 1f
	addi	a5, a5, -OP1_L16SI
	_bnez	a5, .Linvalid_instruction_load

	/* sign extend value */

	slli	a3, a3, 16
	srai	a3, a3, 16

	/* Set target register. */

1:
	extui	a4, a4, INSN_T, 4	# extract target register
	movi	a5, .Lload_table
	addx8	a4, a4, a5
	jx	a4			# jump to entry for target register

	.align	8
.Lload_table:
	s32i	a3, a2, PT_AREG0;	_j .Lexit;	.align 8
	mov	a1, a3;			_j .Lexit;	.align 8 # fishy??
	s32i	a3, a2, PT_AREG2;	_j .Lexit;	.align 8
	s32i	a3, a2, PT_AREG3;	_j .Lexit;	.align 8
	s32i	a3, a2, PT_AREG4;	_j .Lexit;	.align 8
	s32i	a3, a2, PT_AREG5;	_j .Lexit;	.align 8
	s32i	a3, a2, PT_AREG6;	_j .Lexit;	.align 8
	s32i	a3, a2, PT_AREG7;	_j .Lexit;	.align 8
	s32i	a3, a2, PT_AREG8;	_j .Lexit;	.align 8
	mov	a9, a3		;	_j .Lexit;	.align 8
	mov	a10, a3		;	_j .Lexit;	.align 8
	mov	a11, a3		;	_j .Lexit;	.align 8
	mov	a12, a3		;	_j .Lexit;	.align 8
	mov	a13, a3		;	_j .Lexit;	.align 8
	mov	a14, a3		;	_j .Lexit;	.align 8
	mov	a15, a3		;	_j .Lexit;	.align 8

.Lstore_table:
	l32i	a3, a2, PT_AREG0;	_j 1f;	.align 8
	mov	a3, a1;			_j 1f;	.align 8	# fishy??
	l32i	a3, a2, PT_AREG2;	_j 1f;	.align 8
	l32i	a3, a2, PT_AREG3;	_j 1f;	.align 8
	l32i	a3, a2, PT_AREG4;	_j 1f;	.align 8
	l32i	a3, a2, PT_AREG5;	_j 1f;	.align 8
	l32i	a3, a2, PT_AREG6;	_j 1f;	.align 8
	l32i	a3, a2, PT_AREG7;	_j 1f;	.align 8
	l32i	a3, a2, PT_AREG8;	_j 1f;	.align 8
	mov	a3, a9		;	_j 1f;	.align 8
	mov	a3, a10		;	_j 1f;	.align 8
	mov	a3, a11		;	_j 1f;	.align 8
	mov	a3, a12		;	_j 1f;	.align 8
	mov	a3, a13		;	_j 1f;	.align 8
	mov	a3, a14		;	_j 1f;	.align 8
	mov	a3, a15		;	_j 1f;	.align 8

	/* We cannot handle this exception. */

	.extern _kernel_exception
.Linvalid_instruction_load:
.Linvalid_instruction_store:

	movi	a4, 0
	rsr	a3, excsave1
	s32i	a4, a3, EXC_TABLE_FIXUP

	/* Restore a4...a8 and SAR, set SP, and jump to default exception. */

	l32i	a8, a2, PT_AREG8
	l32i	a7, a2, PT_AREG7
	l32i	a6, a2, PT_AREG6
	l32i	a5, a2, PT_AREG5
	l32i	a4, a2, PT_AREG4
	wsr	a0, sar
	mov	a1, a2

	rsr	a0, ps
	bbsi.l  a0, PS_UM_BIT, 2f     # jump if user mode

	movi	a0, _kernel_exception
	jx	a0

2:	movi	a0, _user_exception
	jx	a0

1: 	# a7: instruction pointer, a4: instruction, a3: value

	movi	a6, 0			# mask: ffffffff:00000000

#if XCHAL_HAVE_DENSITY
	addi	a7, a7, 2		# incr. PC,assume 16-bit instruction

	extui	a5, a4, INSN_OP0, 4	# extract OP0
	addi	a5, a5, -OP0_S32I_N
	_beqz	a5, 1f			# s32i.n: jump

	addi	a7, a7, 1		# increment PC, 32-bit instruction
#else
	addi	a7, a7, 3		# increment PC, 32-bit instruction
#endif

	extui	a5, a4, INSN_OP1, 4	# extract OP1
	_beqi	a5, OP1_S32I, 1f	# jump if 32 bit store
	_bnei	a5, OP1_S16I, .Linvalid_instruction_store

	movi	a5, -1
	__extl	a3, a3			# get 16-bit value
	__exth	a6, a5			# get 16-bit mask ffffffff:ffff0000

	/* Get memory address */

1:
	movi	a4, ~3
	and	a4, a4, a8		# align memory address

	/* Insert value into memory */

	movi	a5, -1			# mask: ffffffff:XXXX0000
#ifdef UNALIGNED_USER_EXCEPTION
	addi	a4, a4, 8
#endif

	__ssa8r a8
	__src_b	a8, a5, a6		# lo-mask  F..F0..0 (BE) 0..0F..F (LE)
	__src_b	a6, a6, a5		# hi-mask  0..0F..F (BE) F..F0..0 (LE)
#ifdef UNALIGNED_USER_EXCEPTION
	l32e	a5, a4, -8
#else
	l32i	a5, a4, 0		# load lower address word
#endif
	and	a5, a5, a8		# mask
	__sh	a8, a3 			# shift value
	or	a5, a5, a8		# or with original value
#ifdef UNALIGNED_USER_EXCEPTION
	s32e	a5, a4, -8
	l32e	a8, a4, -4
#else
	s32i	a5, a4, 0		# store
	l32i	a8, a4, 4		# same for upper address word
#endif
	__sl	a5, a3
	and	a6, a8, a6
	or	a6, a6, a5
#ifdef UNALIGNED_USER_EXCEPTION
	s32e	a6, a4, -4
#else
	s32i	a6, a4, 4
#endif

.Lexit:
#if XCHAL_HAVE_LOOPS
	rsr	a4, lend		# check if we reached LEND
	bne	a7, a4, 1f
	rsr	a4, lcount		# and LCOUNT != 0
	beqz	a4, 1f
	addi	a4, a4, -1		# decrement LCOUNT and set
	rsr	a7, lbeg		# set PC to LBEGIN
	wsr	a4, lcount
#endif

1:	wsr	a7, epc1		# skip emulated instruction

	/* Update icount if we're single-stepping in userspace. */
	rsr	a4, icountlevel
	beqz	a4, 1f
	bgeui	a4, LOCKLEVEL + 1, 1f
	rsr	a4, icount
	addi	a4, a4, 1
	wsr	a4, icount
1:
	movi	a4, 0
	rsr	a3, excsave1
	s32i	a4, a3, EXC_TABLE_FIXUP

	/* Restore working register */

	l32i	a8, a2, PT_AREG8
	l32i	a7, a2, PT_AREG7
	l32i	a6, a2, PT_AREG6
	l32i	a5, a2, PT_AREG5
	l32i	a4, a2, PT_AREG4
	l32i	a3, a2, PT_AREG3

	/* restore SAR and return */

	wsr	a0, sar
	l32i	a0, a2, PT_AREG0
	l32i	a2, a2, PT_AREG2
	rfe

ENDPROC(fast_unaligned)

ENTRY(fast_unaligned_fixup)

	l32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
	wsr	a3, excsave1

	l32i	a8, a2, PT_AREG8
	l32i	a7, a2, PT_AREG7
	l32i	a6, a2, PT_AREG6
	l32i	a5, a2, PT_AREG5
	l32i	a4, a2, PT_AREG4
	l32i	a0, a2, PT_AREG2
	xsr	a0, depc			# restore depc and a0
	wsr	a0, sar

	rsr	a0, exccause
	s32i	a0, a2, PT_DEPC			# mark as a regular exception

	rsr	a0, ps
	bbsi.l  a0, PS_UM_BIT, 1f		# jump if user mode

	rsr	a0, exccause
	addx4	a0, a0, a3              	# find entry in table
	l32i	a0, a0, EXC_TABLE_FAST_KERNEL   # load handler
	l32i	a3, a2, PT_AREG3
	jx	a0
1:
	rsr	a0, exccause
	addx4	a0, a0, a3              	# find entry in table
	l32i	a0, a0, EXC_TABLE_FAST_USER     # load handler
	l32i	a3, a2, PT_AREG3
	jx	a0

ENDPROC(fast_unaligned_fixup)

#endif /* XCHAL_UNALIGNED_LOAD_EXCEPTION || XCHAL_UNALIGNED_STORE_EXCEPTION */
