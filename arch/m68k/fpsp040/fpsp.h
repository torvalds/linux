|
|	fpsp.h 3.3 3.3
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

|	fpsp.h --- stack frame offsets during FPSP exception handling
|
|	These equates are used to access the exception frame, the fsave
|	frame and any local variables needed by the FPSP package.
|
|	All FPSP handlers begin by executing:
|
|		link	a6,#-LOCAL_SIZE
|		fsave	-(a7)
|		movem.l	d0-d1/a0-a1,USER_DA(a6)
|		fmovem.x fp0-fp3,USER_FP0(a6)
|		fmove.l	fpsr/fpcr/fpiar,USER_FPSR(a6)
|
|	After initialization, the stack looks like this:
|
|	A7 --->	+-------------------------------+
|		|				|
|		|	FPU fsave area		|
|		|				|
|		+-------------------------------+
|		|				|
|		|	FPSP Local Variables	|
|		|	     including		|
|		|	  saved registers	|
|		|				|
|		+-------------------------------+
|	A6 --->	|	Saved A6		|
|		+-------------------------------+
|		|				|
|		|	Exception Frame		|
|		|				|
|		|				|
|
|	Positive offsets from A6 refer to the exception frame.  Negative
|	offsets refer to the Local Variable area and the fsave area.
|	The fsave frame is also accessible from the top via A7.
|
|	On exit, the handlers execute:
|
|		movem.l	USER_DA(a6),d0-d1/a0-a1
|		fmovem.x USER_FP0(a6),fp0-fp3
|		fmove.l	USER_FPSR(a6),fpsr/fpcr/fpiar
|		frestore (a7)+
|		unlk	a6
|
|	and then either "bra fpsp_done" if the exception was completely
|	handled	by the package, or "bra real_xxxx" which is an external
|	label to a routine that will process a real exception of the
|	type that was generated.  Some handlers may omit the "frestore"
|	if the FPU state after the exception is idle.
|
|	Sometimes the exception handler will transform the fsave area
|	because it needs to report an exception back to the user.  This
|	can happen if the package is entered for an unimplemented float
|	instruction that generates (say) an underflow.  Alternatively,
|	a second fsave frame can be pushed onto the stack and the
|	handler	exit code will reload the new frame and discard the old.
|
|	The registers d0, d1, a0, a1 and fp0-fp3 are always saved and
|	restored from the "local variable" area and can be used as
|	temporaries.  If a routine needs to change any
|	of these registers, it should modify the saved copy and let
|	the handler exit code restore the value.
|
|----------------------------------------------------------------------
|
|	Local Variables on the stack
|
	.set	LOCAL_SIZE,192		| bytes needed for local variables
	.set	LV,-LOCAL_SIZE	| convenient base value
|
	.set	USER_DA,LV+0		| save space for D0-D1,A0-A1
	.set	USER_D0,LV+0		| saved user D0
	.set	USER_D1,LV+4		| saved user D1
	.set	USER_A0,LV+8		| saved user A0
	.set	USER_A1,LV+12		| saved user A1
	.set	USER_FP0,LV+16		| saved user FP0
	.set	USER_FP1,LV+28		| saved user FP1
	.set	USER_FP2,LV+40		| saved user FP2
	.set	USER_FP3,LV+52		| saved user FP3
	.set	USER_FPCR,LV+64		| saved user FPCR
	.set	FPCR_ENABLE,USER_FPCR+2	|	FPCR exception enable
	.set	FPCR_MODE,USER_FPCR+3	|	FPCR rounding mode control
	.set	USER_FPSR,LV+68		| saved user FPSR
	.set	FPSR_CC,USER_FPSR+0	|	FPSR condition code
	.set	FPSR_QBYTE,USER_FPSR+1	|	FPSR quotient
	.set	FPSR_EXCEPT,USER_FPSR+2	|	FPSR exception
	.set	FPSR_AEXCEPT,USER_FPSR+3	|	FPSR accrued exception
	.set	USER_FPIAR,LV+72		| saved user FPIAR
	.set	FP_SCR1,LV+76		| room for a temporary float value
	.set	FP_SCR2,LV+92		| room for a temporary float value
	.set	L_SCR1,LV+108		| room for a temporary long value
	.set	L_SCR2,LV+112		| room for a temporary long value
	.set	STORE_FLG,LV+116
	.set	BINDEC_FLG,LV+117		| used in bindec
	.set	DNRM_FLG,LV+118		| used in res_func
	.set	RES_FLG,LV+119		| used in res_func
	.set	DY_MO_FLG,LV+120		| dyadic/monadic flag
	.set	UFLG_TMP,LV+121		| temporary for uflag errata
	.set	CU_ONLY,LV+122		| cu-only flag
	.set	VER_TMP,LV+123		| temp holding for version number
	.set	L_SCR3,LV+124		| room for a temporary long value
	.set	FP_SCR3,LV+128		| room for a temporary float value
	.set	FP_SCR4,LV+144		| room for a temporary float value
	.set	FP_SCR5,LV+160		| room for a temporary float value
	.set	FP_SCR6,LV+176
|
|NEXT		equ	LV+192		;need to increase LOCAL_SIZE
|
|--------------------------------------------------------------------------
|
|	fsave offsets and bit definitions
|
|	Offsets are defined from the end of an fsave because the last 10
|	words of a busy frame are the same as the unimplemented frame.
|
	.set	CU_SAVEPC,LV-92		| micro-pc for CU (1 byte)
	.set	FPR_DIRTY_BITS,LV-91		| fpr dirty bits
|
	.set	WBTEMP,LV-76		| write back temp (12 bytes)
	.set	WBTEMP_EX,WBTEMP		| wbtemp sign and exponent (2 bytes)
	.set	WBTEMP_HI,WBTEMP+4	| wbtemp mantissa [63:32] (4 bytes)
	.set	WBTEMP_LO,WBTEMP+8	| wbtemp mantissa [31:00] (4 bytes)
|
	.set	WBTEMP_SGN,WBTEMP+2	| used to store sign
|
	.set	FPSR_SHADOW,LV-64		| fpsr shadow reg
|
	.set	FPIARCU,LV-60		| Instr. addr. reg. for CU (4 bytes)
|
	.set	CMDREG2B,LV-52		| cmd reg for machine 2
	.set	CMDREG3B,LV-48		| cmd reg for E3 exceptions (2 bytes)
|
	.set	NMNEXC,LV-44		| NMNEXC (unsup,snan bits only)
	.set	nmn_unsup_bit,1	|
	.set	nmn_snan_bit,0	|
|
	.set	NMCEXC,LV-43		| NMNEXC & NMCEXC
	.set	nmn_operr_bit,7
	.set	nmn_ovfl_bit,6
	.set	nmn_unfl_bit,5
	.set	nmc_unsup_bit,4
	.set	nmc_snan_bit,3
	.set	nmc_operr_bit,2
	.set	nmc_ovfl_bit,1
	.set	nmc_unfl_bit,0
|
	.set	STAG,LV-40		| source tag (1 byte)
	.set	WBTEMP_GRS,LV-40		| alias wbtemp guard, round, sticky
	.set	guard_bit,1		| guard bit is bit number 1
	.set	round_bit,0		| round bit is bit number 0
	.set	stag_mask,0xE0		| upper 3 bits are source tag type
	.set	denorm_bit,7		| bit determines if denorm or unnorm
	.set	etemp15_bit,4		| etemp exponent bit #15
	.set	wbtemp66_bit,2		| wbtemp mantissa bit #66
	.set	wbtemp1_bit,1		| wbtemp mantissa bit #1
	.set	wbtemp0_bit,0		| wbtemp mantissa bit #0
|
	.set	STICKY,LV-39		| holds sticky bit
	.set	sticky_bit,7
|
	.set	CMDREG1B,LV-36		| cmd reg for E1 exceptions (2 bytes)
	.set	kfact_bit,12		| distinguishes static/dynamic k-factor
|					;on packed move outs.  NOTE: this
|					;equate only works when CMDREG1B is in
|					;a register.
|
	.set	CMDWORD,LV-35		| command word in cmd1b
	.set	direction_bit,5		| bit 0 in opclass
	.set	size_bit2,12		| bit 2 in size field
|
	.set	DTAG,LV-32		| dest tag (1 byte)
	.set	dtag_mask,0xE0		| upper 3 bits are dest type tag
	.set	fptemp15_bit,4		| fptemp exponent bit #15
|
	.set	WB_BYTE,LV-31		| holds WBTE15 bit (1 byte)
	.set	wbtemp15_bit,4		| wbtemp exponent bit #15
|
	.set	E_BYTE,LV-28		| holds E1 and E3 bits (1 byte)
	.set	E1,2		| which bit is E1 flag
	.set	E3,1		| which bit is E3 flag
	.set	SFLAG,0		| which bit is S flag
|
	.set	T_BYTE,LV-27		| holds T and U bits (1 byte)
	.set	XFLAG,7		| which bit is X flag
	.set	UFLAG,5		| which bit is U flag
	.set	TFLAG,4		| which bit is T flag
|
	.set	FPTEMP,LV-24		| fptemp (12 bytes)
	.set	FPTEMP_EX,FPTEMP		| fptemp sign and exponent (2 bytes)
	.set	FPTEMP_HI,FPTEMP+4	| fptemp mantissa [63:32] (4 bytes)
	.set	FPTEMP_LO,FPTEMP+8	| fptemp mantissa [31:00] (4 bytes)
|
	.set	FPTEMP_SGN,FPTEMP+2	| used to store sign
|
	.set	ETEMP,LV-12		| etemp (12 bytes)
	.set	ETEMP_EX,ETEMP		| etemp sign and exponent (2 bytes)
	.set	ETEMP_HI,ETEMP+4		| etemp mantissa [63:32] (4 bytes)
	.set	ETEMP_LO,ETEMP+8		| etemp mantissa [31:00] (4 bytes)
|
	.set	ETEMP_SGN,ETEMP+2		| used to store sign
|
	.set	EXC_SR,4		| exception frame status register
	.set	EXC_PC,6		| exception frame program counter
	.set	EXC_VEC,10		| exception frame vector (format+vector#)
	.set	EXC_EA,12		| exception frame effective address
|
|--------------------------------------------------------------------------
|
|	FPSR/FPCR bits
|
	.set	neg_bit,3	|  negative result
	.set	z_bit,2	|  zero result
	.set	inf_bit,1	|  infinity result
	.set	nan_bit,0	|  not-a-number result
|
	.set	q_sn_bit,7	|  sign bit of quotient byte
|
	.set	bsun_bit,7	|  branch on unordered
	.set	snan_bit,6	|  signalling nan
	.set	operr_bit,5	|  operand error
	.set	ovfl_bit,4	|  overflow
	.set	unfl_bit,3	|  underflow
	.set	dz_bit,2	|  divide by zero
	.set	inex2_bit,1	|  inexact result 2
	.set	inex1_bit,0	|  inexact result 1
|
	.set	aiop_bit,7	|  accrued illegal operation
	.set	aovfl_bit,6	|  accrued overflow
	.set	aunfl_bit,5	|  accrued underflow
	.set	adz_bit,4	|  accrued divide by zero
	.set	ainex_bit,3	|  accrued inexact
|
|	FPSR individual bit masks
|
	.set	neg_mask,0x08000000
	.set	z_mask,0x04000000
	.set	inf_mask,0x02000000
	.set	nan_mask,0x01000000
|
	.set	bsun_mask,0x00008000	|
	.set	snan_mask,0x00004000
	.set	operr_mask,0x00002000
	.set	ovfl_mask,0x00001000
	.set	unfl_mask,0x00000800
	.set	dz_mask,0x00000400
	.set	inex2_mask,0x00000200
	.set	inex1_mask,0x00000100
|
	.set	aiop_mask,0x00000080	|  accrued illegal operation
	.set	aovfl_mask,0x00000040	|  accrued overflow
	.set	aunfl_mask,0x00000020	|  accrued underflow
	.set	adz_mask,0x00000010	|  accrued divide by zero
	.set	ainex_mask,0x00000008	|  accrued inexact
|
|	FPSR combinations used in the FPSP
|
	.set	dzinf_mask,inf_mask+dz_mask+adz_mask
	.set	opnan_mask,nan_mask+operr_mask+aiop_mask
	.set	nzi_mask,0x01ffffff	|  clears N, Z, and I
	.set	unfinx_mask,unfl_mask+inex2_mask+aunfl_mask+ainex_mask
	.set	unf2inx_mask,unfl_mask+inex2_mask+ainex_mask
	.set	ovfinx_mask,ovfl_mask+inex2_mask+aovfl_mask+ainex_mask
	.set	inx1a_mask,inex1_mask+ainex_mask
	.set	inx2a_mask,inex2_mask+ainex_mask
	.set	snaniop_mask,nan_mask+snan_mask+aiop_mask
	.set	naniop_mask,nan_mask+aiop_mask
	.set	neginf_mask,neg_mask+inf_mask
	.set	infaiop_mask,inf_mask+aiop_mask
	.set	negz_mask,neg_mask+z_mask
	.set	opaop_mask,operr_mask+aiop_mask
	.set	unfl_inx_mask,unfl_mask+aunfl_mask+ainex_mask
	.set	ovfl_inx_mask,ovfl_mask+aovfl_mask+ainex_mask
|
|--------------------------------------------------------------------------
|
|	FPCR rounding modes
|
	.set	x_mode,0x00	|  round to extended
	.set	s_mode,0x40	|  round to single
	.set	d_mode,0x80	|  round to double
|
	.set	rn_mode,0x00	|  round nearest
	.set	rz_mode,0x10	|  round to zero
	.set	rm_mode,0x20	|  round to minus infinity
	.set	rp_mode,0x30	|  round to plus infinity
|
|--------------------------------------------------------------------------
|
|	Miscellaneous equates
|
	.set	signan_bit,6	|  signalling nan bit in mantissa
	.set	sign_bit,7
|
	.set	rnd_stky_bit,29	|  round/sticky bit of mantissa
|				this can only be used if in a data register
	.set	sx_mask,0x01800000 |  set s and x bits in word $48
|
	.set	LOCAL_EX,0
	.set	LOCAL_SGN,2
	.set	LOCAL_HI,4
	.set	LOCAL_LO,8
	.set	LOCAL_GRS,12	|  valid ONLY for FP_SCR1, FP_SCR2
|
|
	.set	norm_tag,0x00	|  tag bits in {7:5} position
	.set	zero_tag,0x20
	.set	inf_tag,0x40
	.set	nan_tag,0x60
	.set	dnrm_tag,0x80
|
|	fsave sizes and formats
|
	.set	VER_4,0x40		|  fpsp compatible version numbers
|					are in the $40s {$40-$4f}
	.set	VER_40,0x40		|  original version number
	.set	VER_41,0x41		|  revision version number
|
	.set	BUSY_SIZE,100		|  size of busy frame
	.set	BUSY_FRAME,LV-BUSY_SIZE	|  start of busy frame
|
	.set	UNIMP_40_SIZE,44		|  size of orig unimp frame
	.set	UNIMP_41_SIZE,52		|  size of rev unimp frame
|
	.set	IDLE_SIZE,4		|  size of idle frame
	.set	IDLE_FRAME,LV-IDLE_SIZE	|  start of idle frame
|
|	exception vectors
|
	.set	TRACE_VEC,0x2024		|  trace trap
	.set	FLINE_VEC,0x002C		|  real F-line
	.set	UNIMP_VEC,0x202C		|  unimplemented
	.set	INEX_VEC,0x00C4
|
	.set	dbl_thresh,0x3C01
	.set	sgl_thresh,0x3F81
|
