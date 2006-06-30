/*
 * Signal trampolines for 32 bits processes in a ppc64 kernel for
 * use in the vDSO
 *
 * Copyright (C) 2004 Benjamin Herrenschmuidt (benh@kernel.crashing.org), IBM Corp.
 * Copyright (C) 2004 Alan Modra (amodra@au.ibm.com)), IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/unistd.h>
#include <asm/vdso.h>

	.text

/* The nop here is a hack.  The dwarf2 unwind routines subtract 1 from
   the return address to get an address in the middle of the presumed
   call instruction.  Since we don't have a call here, we artifically
   extend the range covered by the unwind info by adding a nop before
   the real start.  */
	nop
V_FUNCTION_BEGIN(__kernel_sigtramp32)
.Lsig_start = . - 4
	li	r0,__NR_sigreturn
	sc
.Lsig_end:
V_FUNCTION_END(__kernel_sigtramp32)

.Lsigrt_start:
	nop
V_FUNCTION_BEGIN(__kernel_sigtramp_rt32)
	li	r0,__NR_rt_sigreturn
	sc
.Lsigrt_end:
V_FUNCTION_END(__kernel_sigtramp_rt32)

	.section .eh_frame,"a",@progbits

/* Register r1 can be found at offset 4 of a pt_regs structure.
   A pointer to the pt_regs is stored in memory at the old sp plus PTREGS.  */
#define cfa_save \
  .byte 0x0f;			/* DW_CFA_def_cfa_expression */		\
  .uleb128 9f - 1f;		/*   length */				\
1:									\
  .byte 0x71; .sleb128 PTREGS;	/*     DW_OP_breg1 */			\
  .byte 0x06;			/*     DW_OP_deref */			\
  .byte 0x23; .uleb128 RSIZE;	/*     DW_OP_plus_uconst */		\
  .byte 0x06;			/*     DW_OP_deref */			\
9:

/* Register REGNO can be found at offset OFS of a pt_regs structure.
   A pointer to the pt_regs is stored in memory at the old sp plus PTREGS.  */
#define rsave(regno, ofs) \
  .byte 0x10;			/* DW_CFA_expression */			\
  .uleb128 regno;		/*   regno */				\
  .uleb128 9f - 1f;		/*   length */				\
1:									\
  .byte 0x71; .sleb128 PTREGS;	/*     DW_OP_breg1 */			\
  .byte 0x06;			/*     DW_OP_deref */			\
  .ifne ofs;								\
    .byte 0x23; .uleb128 ofs;	/*     DW_OP_plus_uconst */		\
  .endif;								\
9:

/* If msr bit 1<<25 is set, then VMX register REGNO is at offset REGNO*16
   of the VMX reg struct.  The VMX reg struct is at offset VREGS of
   the pt_regs struct.  This macro is for REGNO == 0, and contains
   'subroutines' that the other macros jump to.  */
#define vsave_msr0(regno) \
  .byte 0x10;			/* DW_CFA_expression */			\
  .uleb128 regno + 77;		/*   regno */				\
  .uleb128 9f - 1f;		/*   length */				\
1:									\
  .byte 0x30 + regno;		/*     DW_OP_lit0 */			\
2:									\
  .byte 0x40;			/*     DW_OP_lit16 */			\
  .byte 0x1e;			/*     DW_OP_mul */			\
3:									\
  .byte 0x71; .sleb128 PTREGS;	/*     DW_OP_breg1 */			\
  .byte 0x06;			/*     DW_OP_deref */			\
  .byte 0x12;			/*     DW_OP_dup */			\
  .byte 0x23;			/*     DW_OP_plus_uconst */		\
    .uleb128 33*RSIZE;		/*       msr offset */			\
  .byte 0x06;			/*     DW_OP_deref */			\
  .byte 0x0c; .long 1 << 25;	/*     DW_OP_const4u */			\
  .byte 0x1a;			/*     DW_OP_and */			\
  .byte 0x12;			/*     DW_OP_dup, ret 0 if bra taken */	\
  .byte 0x30;			/*     DW_OP_lit0 */			\
  .byte 0x29;			/*     DW_OP_eq */			\
  .byte 0x28; .short 0x7fff;	/*     DW_OP_bra to end */		\
  .byte 0x13;			/*     DW_OP_drop, pop the 0 */		\
  .byte 0x23; .uleb128 VREGS;	/*     DW_OP_plus_uconst */		\
  .byte 0x22;			/*     DW_OP_plus */			\
  .byte 0x2f; .short 0x7fff;	/*     DW_OP_skip to end */		\
9:

/* If msr bit 1<<25 is set, then VMX register REGNO is at offset REGNO*16
   of the VMX reg struct.  REGNO is 1 thru 31.  */
#define vsave_msr1(regno) \
  .byte 0x10;			/* DW_CFA_expression */			\
  .uleb128 regno + 77;		/*   regno */				\
  .uleb128 9f - 1f;		/*   length */				\
1:									\
  .byte 0x30 + regno;		/*     DW_OP_lit n */			\
  .byte 0x2f; .short 2b - 9f;	/*     DW_OP_skip */			\
9:

/* If msr bit 1<<25 is set, then VMX register REGNO is at offset OFS of
   the VMX save block.  */
#define vsave_msr2(regno, ofs) \
  .byte 0x10;			/* DW_CFA_expression */			\
  .uleb128 regno + 77;		/*   regno */				\
  .uleb128 9f - 1f;		/*   length */				\
1:									\
  .byte 0x0a; .short ofs;	/*     DW_OP_const2u */			\
  .byte 0x2f; .short 3b - 9f;	/*     DW_OP_skip */			\
9:

/* VMX register REGNO is at offset OFS of the VMX save area.  */
#define vsave(regno, ofs) \
  .byte 0x10;			/* DW_CFA_expression */			\
  .uleb128 regno + 77;		/*   regno */				\
  .uleb128 9f - 1f;		/*   length */				\
1:									\
  .byte 0x71; .sleb128 PTREGS;	/*     DW_OP_breg1 */			\
  .byte 0x06;			/*     DW_OP_deref */			\
  .byte 0x23; .uleb128 VREGS;	/*     DW_OP_plus_uconst */		\
  .byte 0x23; .uleb128 ofs;	/*     DW_OP_plus_uconst */		\
9:

/* This is where the pt_regs pointer can be found on the stack.  */
#define PTREGS 64+28

/* Size of regs.  */
#define RSIZE 4

/* This is the offset of the VMX regs.  */
#define VREGS 48*RSIZE+34*8

/* Describe where general purpose regs are saved.  */
#define EH_FRAME_GEN \
  cfa_save;								\
  rsave ( 0,  0*RSIZE);							\
  rsave ( 2,  2*RSIZE);							\
  rsave ( 3,  3*RSIZE);							\
  rsave ( 4,  4*RSIZE);							\
  rsave ( 5,  5*RSIZE);							\
  rsave ( 6,  6*RSIZE);							\
  rsave ( 7,  7*RSIZE);							\
  rsave ( 8,  8*RSIZE);							\
  rsave ( 9,  9*RSIZE);							\
  rsave (10, 10*RSIZE);							\
  rsave (11, 11*RSIZE);							\
  rsave (12, 12*RSIZE);							\
  rsave (13, 13*RSIZE);							\
  rsave (14, 14*RSIZE);							\
  rsave (15, 15*RSIZE);							\
  rsave (16, 16*RSIZE);							\
  rsave (17, 17*RSIZE);							\
  rsave (18, 18*RSIZE);							\
  rsave (19, 19*RSIZE);							\
  rsave (20, 20*RSIZE);							\
  rsave (21, 21*RSIZE);							\
  rsave (22, 22*RSIZE);							\
  rsave (23, 23*RSIZE);							\
  rsave (24, 24*RSIZE);							\
  rsave (25, 25*RSIZE);							\
  rsave (26, 26*RSIZE);							\
  rsave (27, 27*RSIZE);							\
  rsave (28, 28*RSIZE);							\
  rsave (29, 29*RSIZE);							\
  rsave (30, 30*RSIZE);							\
  rsave (31, 31*RSIZE);							\
  rsave (67, 32*RSIZE);		/* ap, used as temp for nip */		\
  rsave (65, 36*RSIZE);		/* lr */				\
  rsave (70, 38*RSIZE)		/* cr */

/* Describe where the FP regs are saved.  */
#define EH_FRAME_FP \
  rsave (32, 48*RSIZE +  0*8);						\
  rsave (33, 48*RSIZE +  1*8);						\
  rsave (34, 48*RSIZE +  2*8);						\
  rsave (35, 48*RSIZE +  3*8);						\
  rsave (36, 48*RSIZE +  4*8);						\
  rsave (37, 48*RSIZE +  5*8);						\
  rsave (38, 48*RSIZE +  6*8);						\
  rsave (39, 48*RSIZE +  7*8);						\
  rsave (40, 48*RSIZE +  8*8);						\
  rsave (41, 48*RSIZE +  9*8);						\
  rsave (42, 48*RSIZE + 10*8);						\
  rsave (43, 48*RSIZE + 11*8);						\
  rsave (44, 48*RSIZE + 12*8);						\
  rsave (45, 48*RSIZE + 13*8);						\
  rsave (46, 48*RSIZE + 14*8);						\
  rsave (47, 48*RSIZE + 15*8);						\
  rsave (48, 48*RSIZE + 16*8);						\
  rsave (49, 48*RSIZE + 17*8);						\
  rsave (50, 48*RSIZE + 18*8);						\
  rsave (51, 48*RSIZE + 19*8);						\
  rsave (52, 48*RSIZE + 20*8);						\
  rsave (53, 48*RSIZE + 21*8);						\
  rsave (54, 48*RSIZE + 22*8);						\
  rsave (55, 48*RSIZE + 23*8);						\
  rsave (56, 48*RSIZE + 24*8);						\
  rsave (57, 48*RSIZE + 25*8);						\
  rsave (58, 48*RSIZE + 26*8);						\
  rsave (59, 48*RSIZE + 27*8);						\
  rsave (60, 48*RSIZE + 28*8);						\
  rsave (61, 48*RSIZE + 29*8);						\
  rsave (62, 48*RSIZE + 30*8);						\
  rsave (63, 48*RSIZE + 31*8)

/* Describe where the VMX regs are saved.  */
#ifdef CONFIG_ALTIVEC
#define EH_FRAME_VMX \
  vsave_msr0 ( 0);							\
  vsave_msr1 ( 1);							\
  vsave_msr1 ( 2);							\
  vsave_msr1 ( 3);							\
  vsave_msr1 ( 4);							\
  vsave_msr1 ( 5);							\
  vsave_msr1 ( 6);							\
  vsave_msr1 ( 7);							\
  vsave_msr1 ( 8);							\
  vsave_msr1 ( 9);							\
  vsave_msr1 (10);							\
  vsave_msr1 (11);							\
  vsave_msr1 (12);							\
  vsave_msr1 (13);							\
  vsave_msr1 (14);							\
  vsave_msr1 (15);							\
  vsave_msr1 (16);							\
  vsave_msr1 (17);							\
  vsave_msr1 (18);							\
  vsave_msr1 (19);							\
  vsave_msr1 (20);							\
  vsave_msr1 (21);							\
  vsave_msr1 (22);							\
  vsave_msr1 (23);							\
  vsave_msr1 (24);							\
  vsave_msr1 (25);							\
  vsave_msr1 (26);							\
  vsave_msr1 (27);							\
  vsave_msr1 (28);							\
  vsave_msr1 (29);							\
  vsave_msr1 (30);							\
  vsave_msr1 (31);							\
  vsave_msr2 (33, 32*16+12);						\
  vsave      (32, 32*16)
#else
#define EH_FRAME_VMX
#endif

.Lcie:
	.long .Lcie_end - .Lcie_start
.Lcie_start:
	.long 0			/* CIE ID */
	.byte 1			/* Version number */
	.string "zRS"		/* NUL-terminated augmentation string */
	.uleb128 4		/* Code alignment factor */
	.sleb128 -4		/* Data alignment factor */
	.byte 67		/* Return address register column, ap */
	.uleb128 1		/* Augmentation value length */
	.byte 0x1b		/* DW_EH_PE_pcrel | DW_EH_PE_sdata4. */
	.byte 0x0c,1,0		/* DW_CFA_def_cfa: r1 ofs 0 */
	.balign 4
.Lcie_end:

	.long .Lfde0_end - .Lfde0_start
.Lfde0_start:
	.long .Lfde0_start - .Lcie	/* CIE pointer. */
	.long .Lsig_start - .		/* PC start, length */
	.long .Lsig_end - .Lsig_start
	.uleb128 0			/* Augmentation */
	EH_FRAME_GEN
	EH_FRAME_FP
	EH_FRAME_VMX
	.balign 4
.Lfde0_end:

/* We have a different stack layout for rt_sigreturn.  */
#undef PTREGS
#define PTREGS 64+16+128+20+28

	.long .Lfde1_end - .Lfde1_start
.Lfde1_start:
	.long .Lfde1_start - .Lcie	/* CIE pointer. */
	.long .Lsigrt_start - .		/* PC start, length */
	.long .Lsigrt_end - .Lsigrt_start
	.uleb128 0			/* Augmentation */
	EH_FRAME_GEN
	EH_FRAME_FP
	EH_FRAME_VMX
	.balign 4
.Lfde1_end:
