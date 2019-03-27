/* mips16 floating point support code
   Copyright (C) 1996, 1997, 1998 Free Software Foundation, Inc.
   Contributed by Cygnus Support

This file is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file with other programs, and to distribute
those programs without any restriction coming from the use of this
file.  (The General Public License restrictions do apply in other
respects; for example, they cover modification of the file, and
distribution when not linked into another program.)

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

/* This file contains mips16 floating point support functions.  These
   functions are called by mips16 code to handle floating point when
   -msoft-float is not used.  They accept the arguments and return
   values using the soft-float calling convention, but do the actual
   operation using the hard floating point instructions.  */

/* This file contains 32 bit assembly code.  */
	.set nomips16

/* Start a function.  */

#define STARTFN(NAME) .globl NAME; .ent NAME; NAME:

/* Finish a function.  */

#define ENDFN(NAME) .end NAME

/* Single precision math.  */

/* This macro defines a function which loads two single precision
   values, performs an operation, and returns the single precision
   result.  */

#define SFOP(NAME, OPCODE)	\
STARTFN (NAME);			\
	.set	noreorder;	\
	mtc1	$4,$f0;		\
	mtc1	$5,$f2;		\
	nop;			\
	OPCODE	$f0,$f0,$f2;	\
	mfc1	$2,$f0;		\
	j	$31;		\
	nop;			\
	.set	reorder;	\
	ENDFN (NAME)

#ifdef L_m16addsf3
SFOP(__mips16_addsf3, add.s)
#endif
#ifdef L_m16subsf3
SFOP(__mips16_subsf3, sub.s)
#endif
#ifdef L_m16mulsf3
SFOP(__mips16_mulsf3, mul.s)
#endif
#ifdef L_m16divsf3
SFOP(__mips16_divsf3, div.s)
#endif

#define SFOP2(NAME, OPCODE)	\
STARTFN (NAME);			\
	.set	noreorder;	\
	mtc1	$4,$f0;		\
	nop;			\
	OPCODE	$f0,$f0;	\
	mfc1	$2,$f0;		\
	j	$31;		\
	nop;			\
	.set	reorder;	\
	ENDFN (NAME)

#ifdef L_m16negsf2
SFOP2(__mips16_negsf2, neg.s)
#endif
#ifdef L_m16abssf2
SFOP2(__mips16_abssf2, abs.s)
#endif

/* Single precision comparisons.  */

/* This macro defines a function which loads two single precision
   values, performs a floating point comparison, and returns the
   specified values according to whether the comparison is true or
   false.  */

#define SFCMP(NAME, OPCODE, TRUE, FALSE)	\
STARTFN (NAME);					\
	mtc1	$4,$f0;				\
	mtc1	$5,$f2;				\
	OPCODE	$f0,$f2;			\
	li	$2,TRUE;			\
	bc1t	1f;				\
	li	$2,FALSE;			\
1:;						\
	j	$31;				\
	ENDFN (NAME)

/* This macro is like SFCMP, but it reverses the comparison.  */

#define SFREVCMP(NAME, OPCODE, TRUE, FALSE)	\
STARTFN (NAME);					\
	mtc1	$4,$f0;				\
	mtc1	$5,$f2;				\
	OPCODE	$f2,$f0;			\
	li	$2,TRUE;			\
	bc1t	1f;				\
	li	$2,FALSE;			\
1:;						\
	j	$31;				\
	ENDFN (NAME)

#ifdef L_m16eqsf2
SFCMP(__mips16_eqsf2, c.eq.s, 0, 1)
#endif
#ifdef L_m16nesf2
SFCMP(__mips16_nesf2, c.eq.s, 0, 1)
#endif
#ifdef L_m16gtsf2
SFREVCMP(__mips16_gtsf2, c.lt.s, 1, 0)
#endif
#ifdef L_m16gesf2
SFREVCMP(__mips16_gesf2, c.le.s, 0, -1)
#endif
#ifdef L_m16lesf2
SFCMP(__mips16_lesf2, c.le.s, 0, 1)
#endif
#ifdef L_m16ltsf2
SFCMP(__mips16_ltsf2, c.lt.s, -1, 0)
#endif

/* Single precision conversions.  */

#ifdef L_m16fltsisf
STARTFN (__mips16_floatsisf)
	.set	noreorder
	mtc1	$4,$f0
	nop
	cvt.s.w	$f0,$f0
	mfc1	$2,$f0
	j	$31
	nop
	.set	reorder
	ENDFN (__mips16_floatsisf)
#endif

#ifdef L_m16fix_truncsfsi
STARTFN (__mips16_fix_truncsfsi)
	.set	noreorder
	mtc1	$4,$f0
	nop
	trunc.w.s $f0,$f0,$4
	mfc1	$2,$f0
	j	$31
	nop
	.set	reorder
	ENDFN (__mips16_fix_truncsfsi)
#endif

#if !defined(__mips_single_float) && !defined(__SINGLE_FLOAT)

/* The double precision operations.  We need to use different code
   based on the preprocessor symbol __mips64, because the way in which
   double precision values will change.  Without __mips64, the value
   is passed in two 32 bit registers.  With __mips64, the value is
   passed in a single 64 bit register.  */

/* Load the first double precision operand.  */

#if defined(__mips64)
#define LDDBL1 dmtc1 $4,$f12
#elif defined(__mipsfp64)
#define LDDBL1 sw $4,0($29); sw $5,4($29); l.d $f12,0($29)
#elif defined(__MIPSEB__)
#define LDDBL1 mtc1 $4,$f13; mtc1 $5,$f12
#else
#define LDDBL1 mtc1 $4,$f12; mtc1 $5,$f13
#endif

/* Load the second double precision operand.  */

#if defined(__mips64)
/* XXX this should be $6 for Algo arg passing model */
#define LDDBL2 dmtc1 $5,$f14
#elif defined(__mipsfp64)
#define LDDBL2 sw $6,8($29); sw $7,12($29); l.d $f14,8($29)
#elif defined(__MIPSEB__)
#define LDDBL2 mtc1 $6,$f15; mtc1 $7,$f14
#else
#define LDDBL2 mtc1 $6,$f14; mtc1 $7,$f15
#endif

/* Move the double precision return value to the right place.  */

#if defined(__mips64)
#define RETDBL dmfc1 $2,$f0
#elif defined(__mipsfp64)
#define RETDBL s.d $f0,0($29); lw $2,0($29); lw $3,4($29)
#elif defined(__MIPSEB__)
#define RETDBL mfc1 $2,$f1; mfc1 $3,$f0
#else
#define RETDBL mfc1 $2,$f0; mfc1 $3,$f1
#endif

/* Double precision math.  */

/* This macro defines a function which loads two double precision
   values, performs an operation, and returns the double precision
   result.  */

#define DFOP(NAME, OPCODE)	\
STARTFN (NAME);			\
	.set	noreorder;	\
	LDDBL1;			\
	LDDBL2;			\
	nop;			\
	OPCODE	$f0,$f12,$f14;	\
	RETDBL;			\
	j	$31;		\
	nop;			\
	.set	reorder;	\
	ENDFN (NAME)

#ifdef L_m16adddf3
DFOP(__mips16_adddf3, add.d)
#endif
#ifdef L_m16subdf3
DFOP(__mips16_subdf3, sub.d)
#endif
#ifdef L_m16muldf3
DFOP(__mips16_muldf3, mul.d)
#endif
#ifdef L_m16divdf3
DFOP(__mips16_divdf3, div.d)
#endif

#define DFOP2(NAME, OPCODE)	\
STARTFN (NAME);			\
	.set	noreorder;	\
	LDDBL1;			\
	nop;			\
	OPCODE	$f0,$f12;	\
	RETDBL;			\
	j	$31;		\
	nop;			\
	.set	reorder;	\
	ENDFN (NAME)

#ifdef L_m16negdf2
DFOP2(__mips16_negdf2, neg.d)
#endif
#ifdef L_m16absdf2
DFOP2(__mips16_absdf2, abs.d)
#endif


/* Conversions between single and double precision.  */

#ifdef L_m16extsfdf2
STARTFN (__mips16_extendsfdf2)
	.set	noreorder
	mtc1	$4,$f12
	nop
	cvt.d.s	$f0,$f12
	RETDBL
	j	$31
	nop
	.set	reorder
	ENDFN (__mips16_extendsfdf2)
#endif

#ifdef L_m16trdfsf2
STARTFN (__mips16_truncdfsf2)
	.set	noreorder
	LDDBL1
	nop
	cvt.s.d	$f0,$f12
	mfc1	$2,$f0
	j	$31
	nop
	.set	reorder
	ENDFN (__mips16_truncdfsf2)
#endif

/* Double precision comparisons.  */

/* This macro defines a function which loads two double precision
   values, performs a floating point comparison, and returns the
   specified values according to whether the comparison is true or
   false.  */

#define DFCMP(NAME, OPCODE, TRUE, FALSE)	\
STARTFN (NAME);					\
	LDDBL1;					\
	LDDBL2;					\
	OPCODE	$f12,$f14;			\
	li	$2,TRUE;			\
	bc1t	1f;				\
	li	$2,FALSE;			\
1:;						\
	j	$31;				\
	ENDFN (NAME)

/* This macro is like DFCMP, but it reverses the comparison.  */

#define DFREVCMP(NAME, OPCODE, TRUE, FALSE)	\
STARTFN (NAME);					\
	LDDBL1;					\
	LDDBL2;					\
	OPCODE	$f14,$f12;			\
	li	$2,TRUE;			\
	bc1t	1f;				\
	li	$2,FALSE;			\
1:;						\
	j	$31;				\
	ENDFN (NAME)

#ifdef L_m16eqdf2
DFCMP(__mips16_eqdf2, c.eq.d, 0, 1)
#endif
#ifdef L_m16nedf2
DFCMP(__mips16_nedf2, c.eq.d, 0, 1)
#endif
#ifdef L_m16gtdf2
DFREVCMP(__mips16_gtdf2, c.lt.d, 1, 0)
#endif
#ifdef L_m16gedf2
DFREVCMP(__mips16_gedf2, c.le.d, 0, -1)
#endif
#ifdef L_m16ledf2
DFCMP(__mips16_ledf2, c.le.d, 0, 1)
#endif
#ifdef L_m16ltdf2
DFCMP(__mips16_ltdf2, c.lt.d, -1, 0)
#endif

/* Double precision conversions.  */

#ifdef L_m16fltsidf
STARTFN (__mips16_floatsidf)
	.set	noreorder
	mtc1	$4,$f12
	nop
	cvt.d.w	$f0,$f12
	RETDBL
	j	$31
	nop
	.set	reorder
	ENDFN (__mips16_floatsidf)
#endif

#ifdef L_m16fix_truncdfsi
STARTFN (__mips16_fix_truncdfsi)
	.set	noreorder
	LDDBL1
	nop
	trunc.w.d $f0,$f12,$4
	mfc1	$2,$f0
	j	$31
	nop
	.set	reorder
	ENDFN (__mips16_fix_truncdfsi)
#endif
#endif /* !__mips_single_float */

/* These functions are used to return floating point values from
   mips16 functions.  In this case we can put mtc1 in a jump delay slot,
   because we know that the next instruction will not refer to a floating
   point register.  */

#ifdef L_m16retsf
STARTFN (__mips16_ret_sf)
	.set	noreorder
	j	$31
	mtc1	$2,$f0
	.set	reorder
	ENDFN (__mips16_ret_sf)
#endif

#if !defined(__mips_single_float) && !defined(__SINGLE_FLOAT)
#ifdef L_m16retdf
STARTFN (__mips16_ret_df)
	.set	noreorder
#if defined(__mips64)
	j	$31
	dmtc1	$2,$f0
#elif defined(__mipsfp64)
	sw	$2,0($29)
	sw	$3,4($29)
	l.d	$f0,0($29)
#elif defined(__MIPSEB__)
	mtc1	$2,$f1
	j	$31
	mtc1	$3,$f0
#else
	mtc1	$2,$f0
	j	$31
	mtc1	$3,$f1
#endif
	.set	reorder
	ENDFN (__mips16_ret_df)
#endif
#endif /* !__mips_single_float */

/* These functions are used by 16 bit code when calling via a function
   pointer.  They must copy the floating point arguments from the gp
   regs into the fp regs.  The function to call will be in $2.  The
   exact set of floating point arguments to copy is encoded in the
   function name; the final number is an fp_code, as described in
   mips.h in the comment about CUMULATIVE_ARGS.  */

#ifdef L_m16stub1
/* (float) */
STARTFN (__mips16_call_stub_1)
	.set	noreorder
	mtc1	$4,$f12
	j	$2
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_1)
#endif

#ifdef L_m16stub5
/* (float, float) */
STARTFN (__mips16_call_stub_5)
	.set	noreorder
	mtc1	$4,$f12
	mtc1	$5,$f14
	j	$2
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_5)
#endif

#if !defined(__mips_single_float) && !defined(__SINGLE_FLOAT)

#ifdef L_m16stub2
/* (double) */
STARTFN (__mips16_call_stub_2)
	.set	noreorder
	LDDBL1
	j	$2
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_2)
#endif

#ifdef L_m16stub6
/* (double, float) */
STARTFN (__mips16_call_stub_6)
	.set	noreorder
	LDDBL1
	mtc1	$6,$f14
	j	$2
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_6)
#endif

#ifdef L_m16stub9
/* (float, double) */
STARTFN (__mips16_call_stub_9)
	.set	noreorder
	mtc1	$4,$f12
	LDDBL2
	j	$2
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_9)
#endif

#ifdef L_m16stub10
/* (double, double) */
STARTFN (__mips16_call_stub_10)
	.set	noreorder
	LDDBL1
	LDDBL2
	j	$2
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_10)
#endif
#endif /* !__mips_single_float */

/* Now we have the same set of functions, except that this time the
   function being called returns an SFmode value.  The calling
   function will arrange to preserve $18, so these functions are free
   to use it to hold the return address.

   Note that we do not know whether the function we are calling is 16
   bit or 32 bit.  However, it does not matter, because 16 bit
   functions always return floating point values in both the gp and
   the fp regs.  It would be possible to check whether the function
   being called is 16 bits, in which case the copy is unnecessary;
   however, it's faster to always do the copy.  */

#ifdef L_m16stubsf0
/* () */
STARTFN (__mips16_call_stub_sf_0)
	.set	noreorder
	move	$18,$31
	jal	$2
	nop
	mfc1	$2,$f0
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_sf_0)
#endif

#ifdef L_m16stubsf1
/* (float) */
STARTFN (__mips16_call_stub_sf_1)
	.set	noreorder
	mtc1	$4,$f12
	move	$18,$31
	jal	$2
	nop
	mfc1	$2,$f0
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_sf_1)
#endif

#ifdef L_m16stubsf5
/* (float, float) */
STARTFN (__mips16_call_stub_sf_5)
	.set	noreorder
	mtc1	$4,$f12
	mtc1	$5,$f14
	move	$18,$31
	jal	$2
	nop
	mfc1	$2,$f0
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_sf_5)
#endif

#if !defined(__mips_single_float) && !defined(__SINGLE_FLOAT)
#ifdef L_m16stubsf2
/* (double) */
STARTFN (__mips16_call_stub_sf_2)
	.set	noreorder
	LDDBL1
	move	$18,$31
	jal	$2
	nop
	mfc1	$2,$f0
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_sf_2)
#endif

#ifdef L_m16stubsf6
/* (double, float) */
STARTFN (__mips16_call_stub_sf_6)
	.set	noreorder
	LDDBL1
	mtc1	$6,$f14
	move	$18,$31
	jal	$2
	nop
	mfc1	$2,$f0
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_sf_6)
#endif

#ifdef L_m16stubsf9
/* (float, double) */
STARTFN (__mips16_call_stub_sf_9)
	.set	noreorder
	mtc1	$4,$f12
	LDDBL2
	move	$18,$31
	jal	$2
	nop
	mfc1	$2,$f0
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_sf_9)
#endif

#ifdef L_m16stubsf10
/* (double, double) */
STARTFN (__mips16_call_stub_sf_10)
	.set	noreorder
	LDDBL1
	LDDBL2
	move	$18,$31
	jal	$2
	nop
	mfc1	$2,$f0
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_sf_10)
#endif

/* Now we have the same set of functions again, except that this time
   the function being called returns an DFmode value.  */

#ifdef L_m16stubdf0
/* () */
STARTFN (__mips16_call_stub_df_0)
	.set	noreorder
	move	$18,$31
	jal	$2
	nop
	RETDBL
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_df_0)
#endif

#ifdef L_m16stubdf1
/* (float) */
STARTFN (__mips16_call_stub_df_1)
	.set	noreorder
	mtc1	$4,$f12
	move	$18,$31
	jal	$2
	nop
	RETDBL
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_df_1)
#endif

#ifdef L_m16stubdf2
/* (double) */
STARTFN (__mips16_call_stub_df_2)
	.set	noreorder
	LDDBL1
	move	$18,$31
	jal	$2
	nop
	RETDBL
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_df_2)
#endif

#ifdef L_m16stubdf5
/* (float, float) */
STARTFN (__mips16_call_stub_df_5)
	.set	noreorder
	mtc1	$4,$f12
	mtc1	$5,$f14
	move	$18,$31
	jal	$2
	nop
	RETDBL
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_df_5)
#endif

#ifdef L_m16stubdf6
/* (double, float) */
STARTFN (__mips16_call_stub_df_6)
	.set	noreorder
	LDDBL1
	mtc1	$6,$f14
	move	$18,$31
	jal	$2
	nop
	RETDBL
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_df_6)
#endif

#ifdef L_m16stubdf9
/* (float, double) */
STARTFN (__mips16_call_stub_df_9)
	.set	noreorder
	mtc1	$4,$f12
	LDDBL2
	move	$18,$31
	jal	$2
	nop
	RETDBL
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_df_9)
#endif

#ifdef L_m16stubdf10
/* (double, double) */
STARTFN (__mips16_call_stub_df_10)
	.set	noreorder
	LDDBL1
	LDDBL2
	move	$18,$31
	jal	$2
	nop
	RETDBL
	j	$18
	nop
	.set	reorder
	ENDFN (__mips16_call_stub_df_10)
#endif
#endif /* !__mips_single_float */
