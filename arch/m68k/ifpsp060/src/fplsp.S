~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
MOTOROLA MICROPROCESSOR & MEMORY TECHNOLOGY GROUP
M68000 Hi-Performance Microprocessor Division
M68060 Software Package
Production Release P1.00 -- October 10, 1994

M68060 Software Package Copyright © 1993, 1994 Motorola Inc.  All rights reserved.

THE SOFTWARE is provided on an "AS IS" basis and without warranty.
To the maximum extent permitted by applicable law,
MOTOROLA DISCLAIMS ALL WARRANTIES WHETHER EXPRESS OR IMPLIED,
INCLUDING IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
and any warranty against infringement with regard to the SOFTWARE
(INCLUDING ANY MODIFIED VERSIONS THEREOF) and any accompanying written materials.

To the maximum extent permitted by applicable law,
IN NO EVENT SHALL MOTOROLA BE LIABLE FOR ANY DAMAGES WHATSOEVER
(INCLUDING WITHOUT LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS,
BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR OTHER PECUNIARY LOSS)
ARISING OF THE USE OR INABILITY TO USE THE SOFTWARE.
Motorola assumes no responsibility for the maintenance and support of the SOFTWARE.

You are hereby granted a copyright license to use, modify, and distribute the SOFTWARE
so long as this entire notice is retained without alteration in any modified and/or
redistributed versions, and that such modified versions are clearly identified as such.
No licenses are granted by implication, estoppel or otherwise under any patents
or trademarks of Motorola, Inc.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# lfptop.s:
#	This file is appended to the top of the 060ILSP package
# and contains the entry points into the package. The user, in
# effect, branches to one of the branch table entries located here.
#

	bra.l	_facoss_
	short	0x0000
	bra.l	_facosd_
	short	0x0000
	bra.l	_facosx_
	short	0x0000

	bra.l	_fasins_
	short	0x0000
	bra.l	_fasind_
	short	0x0000
	bra.l	_fasinx_
	short	0x0000

	bra.l	_fatans_
	short	0x0000
	bra.l	_fatand_
	short	0x0000
	bra.l	_fatanx_
	short	0x0000

	bra.l	_fatanhs_
	short	0x0000
	bra.l	_fatanhd_
	short	0x0000
	bra.l	_fatanhx_
	short	0x0000

	bra.l	_fcoss_
	short	0x0000
	bra.l	_fcosd_
	short	0x0000
	bra.l	_fcosx_
	short	0x0000

	bra.l	_fcoshs_
	short	0x0000
	bra.l	_fcoshd_
	short	0x0000
	bra.l	_fcoshx_
	short	0x0000

	bra.l	_fetoxs_
	short	0x0000
	bra.l	_fetoxd_
	short	0x0000
	bra.l	_fetoxx_
	short	0x0000

	bra.l	_fetoxm1s_
	short	0x0000
	bra.l	_fetoxm1d_
	short	0x0000
	bra.l	_fetoxm1x_
	short	0x0000

	bra.l	_fgetexps_
	short	0x0000
	bra.l	_fgetexpd_
	short	0x0000
	bra.l	_fgetexpx_
	short	0x0000

	bra.l	_fgetmans_
	short	0x0000
	bra.l	_fgetmand_
	short	0x0000
	bra.l	_fgetmanx_
	short	0x0000

	bra.l	_flog10s_
	short	0x0000
	bra.l	_flog10d_
	short	0x0000
	bra.l	_flog10x_
	short	0x0000

	bra.l	_flog2s_
	short	0x0000
	bra.l	_flog2d_
	short	0x0000
	bra.l	_flog2x_
	short	0x0000

	bra.l	_flogns_
	short	0x0000
	bra.l	_flognd_
	short	0x0000
	bra.l	_flognx_
	short	0x0000

	bra.l	_flognp1s_
	short	0x0000
	bra.l	_flognp1d_
	short	0x0000
	bra.l	_flognp1x_
	short	0x0000

	bra.l	_fmods_
	short	0x0000
	bra.l	_fmodd_
	short	0x0000
	bra.l	_fmodx_
	short	0x0000

	bra.l	_frems_
	short	0x0000
	bra.l	_fremd_
	short	0x0000
	bra.l	_fremx_
	short	0x0000

	bra.l	_fscales_
	short	0x0000
	bra.l	_fscaled_
	short	0x0000
	bra.l	_fscalex_
	short	0x0000

	bra.l	_fsins_
	short	0x0000
	bra.l	_fsind_
	short	0x0000
	bra.l	_fsinx_
	short	0x0000

	bra.l	_fsincoss_
	short	0x0000
	bra.l	_fsincosd_
	short	0x0000
	bra.l	_fsincosx_
	short	0x0000

	bra.l	_fsinhs_
	short	0x0000
	bra.l	_fsinhd_
	short	0x0000
	bra.l	_fsinhx_
	short	0x0000

	bra.l	_ftans_
	short	0x0000
	bra.l	_ftand_
	short	0x0000
	bra.l	_ftanx_
	short	0x0000

	bra.l	_ftanhs_
	short	0x0000
	bra.l	_ftanhd_
	short	0x0000
	bra.l	_ftanhx_
	short	0x0000

	bra.l	_ftentoxs_
	short	0x0000
	bra.l	_ftentoxd_
	short	0x0000
	bra.l	_ftentoxx_
	short	0x0000

	bra.l	_ftwotoxs_
	short	0x0000
	bra.l	_ftwotoxd_
	short	0x0000
	bra.l	_ftwotoxx_
	short	0x0000

	bra.l	_fabss_
	short	0x0000
	bra.l	_fabsd_
	short	0x0000
	bra.l	_fabsx_
	short	0x0000

	bra.l	_fadds_
	short	0x0000
	bra.l	_faddd_
	short	0x0000
	bra.l	_faddx_
	short	0x0000

	bra.l	_fdivs_
	short	0x0000
	bra.l	_fdivd_
	short	0x0000
	bra.l	_fdivx_
	short	0x0000

	bra.l	_fints_
	short	0x0000
	bra.l	_fintd_
	short	0x0000
	bra.l	_fintx_
	short	0x0000

	bra.l	_fintrzs_
	short	0x0000
	bra.l	_fintrzd_
	short	0x0000
	bra.l	_fintrzx_
	short	0x0000

	bra.l	_fmuls_
	short	0x0000
	bra.l	_fmuld_
	short	0x0000
	bra.l	_fmulx_
	short	0x0000

	bra.l	_fnegs_
	short	0x0000
	bra.l	_fnegd_
	short	0x0000
	bra.l	_fnegx_
	short	0x0000

	bra.l	_fsqrts_
	short	0x0000
	bra.l	_fsqrtd_
	short	0x0000
	bra.l	_fsqrtx_
	short	0x0000

	bra.l	_fsubs_
	short	0x0000
	bra.l	_fsubd_
	short	0x0000
	bra.l	_fsubx_
	short	0x0000

# leave room for future possible additions
	align	0x400

#
# This file contains a set of define statements for constants
# in order to promote readability within the corecode itself.
#

set LOCAL_SIZE,		192			# stack frame size(bytes)
set LV,			-LOCAL_SIZE		# stack offset

set EXC_SR,		0x4			# stack status register
set EXC_PC,		0x6			# stack pc
set EXC_VOFF,		0xa			# stacked vector offset
set EXC_EA,		0xc			# stacked <ea>

set EXC_FP,		0x0			# frame pointer

set EXC_AREGS,		-68			# offset of all address regs
set EXC_DREGS,		-100			# offset of all data regs
set EXC_FPREGS,		-36			# offset of all fp regs

set EXC_A7,		EXC_AREGS+(7*4)		# offset of saved a7
set OLD_A7,		EXC_AREGS+(6*4)		# extra copy of saved a7
set EXC_A6,		EXC_AREGS+(6*4)		# offset of saved a6
set EXC_A5,		EXC_AREGS+(5*4)
set EXC_A4,		EXC_AREGS+(4*4)
set EXC_A3,		EXC_AREGS+(3*4)
set EXC_A2,		EXC_AREGS+(2*4)
set EXC_A1,		EXC_AREGS+(1*4)
set EXC_A0,		EXC_AREGS+(0*4)
set EXC_D7,		EXC_DREGS+(7*4)
set EXC_D6,		EXC_DREGS+(6*4)
set EXC_D5,		EXC_DREGS+(5*4)
set EXC_D4,		EXC_DREGS+(4*4)
set EXC_D3,		EXC_DREGS+(3*4)
set EXC_D2,		EXC_DREGS+(2*4)
set EXC_D1,		EXC_DREGS+(1*4)
set EXC_D0,		EXC_DREGS+(0*4)

set EXC_FP0,		EXC_FPREGS+(0*12)	# offset of saved fp0
set EXC_FP1,		EXC_FPREGS+(1*12)	# offset of saved fp1
set EXC_FP2,		EXC_FPREGS+(2*12)	# offset of saved fp2 (not used)

set FP_SCR1,		LV+80			# fp scratch 1
set FP_SCR1_EX,		FP_SCR1+0
set FP_SCR1_SGN,	FP_SCR1+2
set FP_SCR1_HI,		FP_SCR1+4
set FP_SCR1_LO,		FP_SCR1+8

set FP_SCR0,		LV+68			# fp scratch 0
set FP_SCR0_EX,		FP_SCR0+0
set FP_SCR0_SGN,	FP_SCR0+2
set FP_SCR0_HI,		FP_SCR0+4
set FP_SCR0_LO,		FP_SCR0+8

set FP_DST,		LV+56			# fp destination operand
set FP_DST_EX,		FP_DST+0
set FP_DST_SGN,		FP_DST+2
set FP_DST_HI,		FP_DST+4
set FP_DST_LO,		FP_DST+8

set FP_SRC,		LV+44			# fp source operand
set FP_SRC_EX,		FP_SRC+0
set FP_SRC_SGN,		FP_SRC+2
set FP_SRC_HI,		FP_SRC+4
set FP_SRC_LO,		FP_SRC+8

set USER_FPIAR,		LV+40			# FP instr address register

set USER_FPSR,		LV+36			# FP status register
set FPSR_CC,		USER_FPSR+0		# FPSR condition codes
set FPSR_QBYTE,		USER_FPSR+1		# FPSR qoutient byte
set FPSR_EXCEPT,	USER_FPSR+2		# FPSR exception status byte
set FPSR_AEXCEPT,	USER_FPSR+3		# FPSR accrued exception byte

set USER_FPCR,		LV+32			# FP control register
set FPCR_ENABLE,	USER_FPCR+2		# FPCR exception enable
set FPCR_MODE,		USER_FPCR+3		# FPCR rounding mode control

set L_SCR3,		LV+28			# integer scratch 3
set L_SCR2,		LV+24			# integer scratch 2
set L_SCR1,		LV+20			# integer scratch 1

set STORE_FLG,		LV+19			# flag: operand store (ie. not fcmp/ftst)

set EXC_TEMP2,		LV+24			# temporary space
set EXC_TEMP,		LV+16			# temporary space

set DTAG,		LV+15			# destination operand type
set STAG,		LV+14			# source operand type

set SPCOND_FLG,		LV+10			# flag: special case (see below)

set EXC_CC,		LV+8			# saved condition codes
set EXC_EXTWPTR,	LV+4			# saved current PC (active)
set EXC_EXTWORD,	LV+2			# saved extension word
set EXC_CMDREG,		LV+2			# saved extension word
set EXC_OPWORD,		LV+0			# saved operation word

################################

# Helpful macros

set FTEMP,		0			# offsets within an
set FTEMP_EX,		0			# extended precision
set FTEMP_SGN,		2			# value saved in memory.
set FTEMP_HI,		4
set FTEMP_LO,		8
set FTEMP_GRS,		12

set LOCAL,		0			# offsets within an
set LOCAL_EX,		0			# extended precision
set LOCAL_SGN,		2			# value saved in memory.
set LOCAL_HI,		4
set LOCAL_LO,		8
set LOCAL_GRS,		12

set DST,		0			# offsets within an
set DST_EX,		0			# extended precision
set DST_HI,		4			# value saved in memory.
set DST_LO,		8

set SRC,		0			# offsets within an
set SRC_EX,		0			# extended precision
set SRC_HI,		4			# value saved in memory.
set SRC_LO,		8

set SGL_LO,		0x3f81			# min sgl prec exponent
set SGL_HI,		0x407e			# max sgl prec exponent
set DBL_LO,		0x3c01			# min dbl prec exponent
set DBL_HI,		0x43fe			# max dbl prec exponent
set EXT_LO,		0x0			# min ext prec exponent
set EXT_HI,		0x7ffe			# max ext prec exponent

set EXT_BIAS,		0x3fff			# extended precision bias
set SGL_BIAS,		0x007f			# single precision bias
set DBL_BIAS,		0x03ff			# double precision bias

set NORM,		0x00			# operand type for STAG/DTAG
set ZERO,		0x01			# operand type for STAG/DTAG
set INF,		0x02			# operand type for STAG/DTAG
set QNAN,		0x03			# operand type for STAG/DTAG
set DENORM,		0x04			# operand type for STAG/DTAG
set SNAN,		0x05			# operand type for STAG/DTAG
set UNNORM,		0x06			# operand type for STAG/DTAG

##################
# FPSR/FPCR bits #
##################
set neg_bit,		0x3			# negative result
set z_bit,		0x2			# zero result
set inf_bit,		0x1			# infinite result
set nan_bit,		0x0			# NAN result

set q_sn_bit,		0x7			# sign bit of quotient byte

set bsun_bit,		7			# branch on unordered
set snan_bit,		6			# signalling NAN
set operr_bit,		5			# operand error
set ovfl_bit,		4			# overflow
set unfl_bit,		3			# underflow
set dz_bit,		2			# divide by zero
set inex2_bit,		1			# inexact result 2
set inex1_bit,		0			# inexact result 1

set aiop_bit,		7			# accrued inexact operation bit
set aovfl_bit,		6			# accrued overflow bit
set aunfl_bit,		5			# accrued underflow bit
set adz_bit,		4			# accrued dz bit
set ainex_bit,		3			# accrued inexact bit

#############################
# FPSR individual bit masks #
#############################
set neg_mask,		0x08000000		# negative bit mask (lw)
set inf_mask,		0x02000000		# infinity bit mask (lw)
set z_mask,		0x04000000		# zero bit mask (lw)
set nan_mask,		0x01000000		# nan bit mask (lw)

set neg_bmask,		0x08			# negative bit mask (byte)
set inf_bmask,		0x02			# infinity bit mask (byte)
set z_bmask,		0x04			# zero bit mask (byte)
set nan_bmask,		0x01			# nan bit mask (byte)

set bsun_mask,		0x00008000		# bsun exception mask
set snan_mask,		0x00004000		# snan exception mask
set operr_mask,		0x00002000		# operr exception mask
set ovfl_mask,		0x00001000		# overflow exception mask
set unfl_mask,		0x00000800		# underflow exception mask
set dz_mask,		0x00000400		# dz exception mask
set inex2_mask,		0x00000200		# inex2 exception mask
set inex1_mask,		0x00000100		# inex1 exception mask

set aiop_mask,		0x00000080		# accrued illegal operation
set aovfl_mask,		0x00000040		# accrued overflow
set aunfl_mask,		0x00000020		# accrued underflow
set adz_mask,		0x00000010		# accrued divide by zero
set ainex_mask,		0x00000008		# accrued inexact

######################################
# FPSR combinations used in the FPSP #
######################################
set dzinf_mask,		inf_mask+dz_mask+adz_mask
set opnan_mask,		nan_mask+operr_mask+aiop_mask
set nzi_mask,		0x01ffffff		#clears N, Z, and I
set unfinx_mask,	unfl_mask+inex2_mask+aunfl_mask+ainex_mask
set unf2inx_mask,	unfl_mask+inex2_mask+ainex_mask
set ovfinx_mask,	ovfl_mask+inex2_mask+aovfl_mask+ainex_mask
set inx1a_mask,		inex1_mask+ainex_mask
set inx2a_mask,		inex2_mask+ainex_mask
set snaniop_mask,	nan_mask+snan_mask+aiop_mask
set snaniop2_mask,	snan_mask+aiop_mask
set naniop_mask,	nan_mask+aiop_mask
set neginf_mask,	neg_mask+inf_mask
set infaiop_mask,	inf_mask+aiop_mask
set negz_mask,		neg_mask+z_mask
set opaop_mask,		operr_mask+aiop_mask
set unfl_inx_mask,	unfl_mask+aunfl_mask+ainex_mask
set ovfl_inx_mask,	ovfl_mask+aovfl_mask+ainex_mask

#########
# misc. #
#########
set rnd_stky_bit,	29			# stky bit pos in longword

set sign_bit,		0x7			# sign bit
set signan_bit,		0x6			# signalling nan bit

set sgl_thresh,		0x3f81			# minimum sgl exponent
set dbl_thresh,		0x3c01			# minimum dbl exponent

set x_mode,		0x0			# extended precision
set s_mode,		0x4			# single precision
set d_mode,		0x8			# double precision

set rn_mode,		0x0			# round-to-nearest
set rz_mode,		0x1			# round-to-zero
set rm_mode,		0x2			# round-tp-minus-infinity
set rp_mode,		0x3			# round-to-plus-infinity

set mantissalen,	64			# length of mantissa in bits

set BYTE,		1			# len(byte) == 1 byte
set WORD,		2			# len(word) == 2 bytes
set LONG,		4			# len(longword) == 2 bytes

set BSUN_VEC,		0xc0			# bsun    vector offset
set INEX_VEC,		0xc4			# inexact vector offset
set DZ_VEC,		0xc8			# dz      vector offset
set UNFL_VEC,		0xcc			# unfl    vector offset
set OPERR_VEC,		0xd0			# operr   vector offset
set OVFL_VEC,		0xd4			# ovfl    vector offset
set SNAN_VEC,		0xd8			# snan    vector offset

###########################
# SPecial CONDition FLaGs #
###########################
set ftrapcc_flg,	0x01			# flag bit: ftrapcc exception
set fbsun_flg,		0x02			# flag bit: bsun exception
set mia7_flg,		0x04			# flag bit: (a7)+ <ea>
set mda7_flg,		0x08			# flag bit: -(a7) <ea>
set fmovm_flg,		0x40			# flag bit: fmovm instruction
set immed_flg,		0x80			# flag bit: &<data> <ea>

set ftrapcc_bit,	0x0
set fbsun_bit,		0x1
set mia7_bit,		0x2
set mda7_bit,		0x3
set immed_bit,		0x7

##################################
# TRANSCENDENTAL "LAST-OP" FLAGS #
##################################
set FMUL_OP,		0x0			# fmul instr performed last
set FDIV_OP,		0x1			# fdiv performed last
set FADD_OP,		0x2			# fadd performed last
set FMOV_OP,		0x3			# fmov performed last

#############
# CONSTANTS #
#############
T1:	long		0x40C62D38,0xD3D64634	# 16381 LOG2 LEAD
T2:	long		0x3D6F90AE,0xB1E75CC7	# 16381 LOG2 TRAIL

PI:	long		0x40000000,0xC90FDAA2,0x2168C235,0x00000000
PIBY2:	long		0x3FFF0000,0xC90FDAA2,0x2168C235,0x00000000

TWOBYPI:
	long		0x3FE45F30,0x6DC9C883

#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_fsins_
_fsins_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L0_2s
	bsr.l		ssin			# operand is a NORM
	bra.b		_L0_6s
_L0_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L0_3s			# no
	bsr.l		src_zero			# yes
	bra.b		_L0_6s
_L0_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L0_4s			# no
	bsr.l		t_operr			# yes
	bra.b		_L0_6s
_L0_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L0_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L0_6s
_L0_5s:
	bsr.l		ssind			# operand is a DENORM
_L0_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fsind_
_fsind_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L0_2d
	bsr.l		ssin			# operand is a NORM
	bra.b		_L0_6d
_L0_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L0_3d			# no
	bsr.l		src_zero			# yes
	bra.b		_L0_6d
_L0_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L0_4d			# no
	bsr.l		t_operr			# yes
	bra.b		_L0_6d
_L0_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L0_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L0_6d
_L0_5d:
	bsr.l		ssind			# operand is a DENORM
_L0_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fsinx_
_fsinx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L0_2x
	bsr.l		ssin			# operand is a NORM
	bra.b		_L0_6x
_L0_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L0_3x			# no
	bsr.l		src_zero			# yes
	bra.b		_L0_6x
_L0_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L0_4x			# no
	bsr.l		t_operr			# yes
	bra.b		_L0_6x
_L0_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L0_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L0_6x
_L0_5x:
	bsr.l		ssind			# operand is a DENORM
_L0_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_fcoss_
_fcoss_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L1_2s
	bsr.l		scos			# operand is a NORM
	bra.b		_L1_6s
_L1_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L1_3s			# no
	bsr.l		ld_pone			# yes
	bra.b		_L1_6s
_L1_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L1_4s			# no
	bsr.l		t_operr			# yes
	bra.b		_L1_6s
_L1_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L1_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L1_6s
_L1_5s:
	bsr.l		scosd			# operand is a DENORM
_L1_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fcosd_
_fcosd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L1_2d
	bsr.l		scos			# operand is a NORM
	bra.b		_L1_6d
_L1_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L1_3d			# no
	bsr.l		ld_pone			# yes
	bra.b		_L1_6d
_L1_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L1_4d			# no
	bsr.l		t_operr			# yes
	bra.b		_L1_6d
_L1_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L1_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L1_6d
_L1_5d:
	bsr.l		scosd			# operand is a DENORM
_L1_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fcosx_
_fcosx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L1_2x
	bsr.l		scos			# operand is a NORM
	bra.b		_L1_6x
_L1_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L1_3x			# no
	bsr.l		ld_pone			# yes
	bra.b		_L1_6x
_L1_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L1_4x			# no
	bsr.l		t_operr			# yes
	bra.b		_L1_6x
_L1_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L1_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L1_6x
_L1_5x:
	bsr.l		scosd			# operand is a DENORM
_L1_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_fsinhs_
_fsinhs_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L2_2s
	bsr.l		ssinh			# operand is a NORM
	bra.b		_L2_6s
_L2_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L2_3s			# no
	bsr.l		src_zero			# yes
	bra.b		_L2_6s
_L2_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L2_4s			# no
	bsr.l		src_inf			# yes
	bra.b		_L2_6s
_L2_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L2_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L2_6s
_L2_5s:
	bsr.l		ssinhd			# operand is a DENORM
_L2_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fsinhd_
_fsinhd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L2_2d
	bsr.l		ssinh			# operand is a NORM
	bra.b		_L2_6d
_L2_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L2_3d			# no
	bsr.l		src_zero			# yes
	bra.b		_L2_6d
_L2_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L2_4d			# no
	bsr.l		src_inf			# yes
	bra.b		_L2_6d
_L2_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L2_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L2_6d
_L2_5d:
	bsr.l		ssinhd			# operand is a DENORM
_L2_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fsinhx_
_fsinhx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L2_2x
	bsr.l		ssinh			# operand is a NORM
	bra.b		_L2_6x
_L2_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L2_3x			# no
	bsr.l		src_zero			# yes
	bra.b		_L2_6x
_L2_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L2_4x			# no
	bsr.l		src_inf			# yes
	bra.b		_L2_6x
_L2_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L2_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L2_6x
_L2_5x:
	bsr.l		ssinhd			# operand is a DENORM
_L2_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_flognp1s_
_flognp1s_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L3_2s
	bsr.l		slognp1			# operand is a NORM
	bra.b		_L3_6s
_L3_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L3_3s			# no
	bsr.l		src_zero			# yes
	bra.b		_L3_6s
_L3_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L3_4s			# no
	bsr.l		sopr_inf			# yes
	bra.b		_L3_6s
_L3_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L3_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L3_6s
_L3_5s:
	bsr.l		slognp1d			# operand is a DENORM
_L3_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_flognp1d_
_flognp1d_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L3_2d
	bsr.l		slognp1			# operand is a NORM
	bra.b		_L3_6d
_L3_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L3_3d			# no
	bsr.l		src_zero			# yes
	bra.b		_L3_6d
_L3_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L3_4d			# no
	bsr.l		sopr_inf			# yes
	bra.b		_L3_6d
_L3_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L3_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L3_6d
_L3_5d:
	bsr.l		slognp1d			# operand is a DENORM
_L3_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_flognp1x_
_flognp1x_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L3_2x
	bsr.l		slognp1			# operand is a NORM
	bra.b		_L3_6x
_L3_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L3_3x			# no
	bsr.l		src_zero			# yes
	bra.b		_L3_6x
_L3_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L3_4x			# no
	bsr.l		sopr_inf			# yes
	bra.b		_L3_6x
_L3_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L3_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L3_6x
_L3_5x:
	bsr.l		slognp1d			# operand is a DENORM
_L3_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_fetoxm1s_
_fetoxm1s_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L4_2s
	bsr.l		setoxm1			# operand is a NORM
	bra.b		_L4_6s
_L4_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L4_3s			# no
	bsr.l		src_zero			# yes
	bra.b		_L4_6s
_L4_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L4_4s			# no
	bsr.l		setoxm1i			# yes
	bra.b		_L4_6s
_L4_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L4_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L4_6s
_L4_5s:
	bsr.l		setoxm1d			# operand is a DENORM
_L4_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fetoxm1d_
_fetoxm1d_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L4_2d
	bsr.l		setoxm1			# operand is a NORM
	bra.b		_L4_6d
_L4_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L4_3d			# no
	bsr.l		src_zero			# yes
	bra.b		_L4_6d
_L4_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L4_4d			# no
	bsr.l		setoxm1i			# yes
	bra.b		_L4_6d
_L4_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L4_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L4_6d
_L4_5d:
	bsr.l		setoxm1d			# operand is a DENORM
_L4_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fetoxm1x_
_fetoxm1x_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L4_2x
	bsr.l		setoxm1			# operand is a NORM
	bra.b		_L4_6x
_L4_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L4_3x			# no
	bsr.l		src_zero			# yes
	bra.b		_L4_6x
_L4_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L4_4x			# no
	bsr.l		setoxm1i			# yes
	bra.b		_L4_6x
_L4_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L4_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L4_6x
_L4_5x:
	bsr.l		setoxm1d			# operand is a DENORM
_L4_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_ftanhs_
_ftanhs_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L5_2s
	bsr.l		stanh			# operand is a NORM
	bra.b		_L5_6s
_L5_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L5_3s			# no
	bsr.l		src_zero			# yes
	bra.b		_L5_6s
_L5_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L5_4s			# no
	bsr.l		src_one			# yes
	bra.b		_L5_6s
_L5_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L5_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L5_6s
_L5_5s:
	bsr.l		stanhd			# operand is a DENORM
_L5_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_ftanhd_
_ftanhd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L5_2d
	bsr.l		stanh			# operand is a NORM
	bra.b		_L5_6d
_L5_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L5_3d			# no
	bsr.l		src_zero			# yes
	bra.b		_L5_6d
_L5_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L5_4d			# no
	bsr.l		src_one			# yes
	bra.b		_L5_6d
_L5_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L5_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L5_6d
_L5_5d:
	bsr.l		stanhd			# operand is a DENORM
_L5_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_ftanhx_
_ftanhx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L5_2x
	bsr.l		stanh			# operand is a NORM
	bra.b		_L5_6x
_L5_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L5_3x			# no
	bsr.l		src_zero			# yes
	bra.b		_L5_6x
_L5_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L5_4x			# no
	bsr.l		src_one			# yes
	bra.b		_L5_6x
_L5_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L5_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L5_6x
_L5_5x:
	bsr.l		stanhd			# operand is a DENORM
_L5_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_fatans_
_fatans_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L6_2s
	bsr.l		satan			# operand is a NORM
	bra.b		_L6_6s
_L6_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L6_3s			# no
	bsr.l		src_zero			# yes
	bra.b		_L6_6s
_L6_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L6_4s			# no
	bsr.l		spi_2			# yes
	bra.b		_L6_6s
_L6_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L6_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L6_6s
_L6_5s:
	bsr.l		satand			# operand is a DENORM
_L6_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fatand_
_fatand_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L6_2d
	bsr.l		satan			# operand is a NORM
	bra.b		_L6_6d
_L6_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L6_3d			# no
	bsr.l		src_zero			# yes
	bra.b		_L6_6d
_L6_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L6_4d			# no
	bsr.l		spi_2			# yes
	bra.b		_L6_6d
_L6_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L6_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L6_6d
_L6_5d:
	bsr.l		satand			# operand is a DENORM
_L6_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fatanx_
_fatanx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L6_2x
	bsr.l		satan			# operand is a NORM
	bra.b		_L6_6x
_L6_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L6_3x			# no
	bsr.l		src_zero			# yes
	bra.b		_L6_6x
_L6_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L6_4x			# no
	bsr.l		spi_2			# yes
	bra.b		_L6_6x
_L6_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L6_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L6_6x
_L6_5x:
	bsr.l		satand			# operand is a DENORM
_L6_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_fasins_
_fasins_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L7_2s
	bsr.l		sasin			# operand is a NORM
	bra.b		_L7_6s
_L7_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L7_3s			# no
	bsr.l		src_zero			# yes
	bra.b		_L7_6s
_L7_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L7_4s			# no
	bsr.l		t_operr			# yes
	bra.b		_L7_6s
_L7_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L7_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L7_6s
_L7_5s:
	bsr.l		sasind			# operand is a DENORM
_L7_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fasind_
_fasind_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L7_2d
	bsr.l		sasin			# operand is a NORM
	bra.b		_L7_6d
_L7_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L7_3d			# no
	bsr.l		src_zero			# yes
	bra.b		_L7_6d
_L7_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L7_4d			# no
	bsr.l		t_operr			# yes
	bra.b		_L7_6d
_L7_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L7_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L7_6d
_L7_5d:
	bsr.l		sasind			# operand is a DENORM
_L7_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fasinx_
_fasinx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L7_2x
	bsr.l		sasin			# operand is a NORM
	bra.b		_L7_6x
_L7_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L7_3x			# no
	bsr.l		src_zero			# yes
	bra.b		_L7_6x
_L7_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L7_4x			# no
	bsr.l		t_operr			# yes
	bra.b		_L7_6x
_L7_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L7_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L7_6x
_L7_5x:
	bsr.l		sasind			# operand is a DENORM
_L7_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_fatanhs_
_fatanhs_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L8_2s
	bsr.l		satanh			# operand is a NORM
	bra.b		_L8_6s
_L8_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L8_3s			# no
	bsr.l		src_zero			# yes
	bra.b		_L8_6s
_L8_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L8_4s			# no
	bsr.l		t_operr			# yes
	bra.b		_L8_6s
_L8_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L8_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L8_6s
_L8_5s:
	bsr.l		satanhd			# operand is a DENORM
_L8_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fatanhd_
_fatanhd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L8_2d
	bsr.l		satanh			# operand is a NORM
	bra.b		_L8_6d
_L8_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L8_3d			# no
	bsr.l		src_zero			# yes
	bra.b		_L8_6d
_L8_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L8_4d			# no
	bsr.l		t_operr			# yes
	bra.b		_L8_6d
_L8_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L8_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L8_6d
_L8_5d:
	bsr.l		satanhd			# operand is a DENORM
_L8_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fatanhx_
_fatanhx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L8_2x
	bsr.l		satanh			# operand is a NORM
	bra.b		_L8_6x
_L8_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L8_3x			# no
	bsr.l		src_zero			# yes
	bra.b		_L8_6x
_L8_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L8_4x			# no
	bsr.l		t_operr			# yes
	bra.b		_L8_6x
_L8_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L8_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L8_6x
_L8_5x:
	bsr.l		satanhd			# operand is a DENORM
_L8_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_ftans_
_ftans_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L9_2s
	bsr.l		stan			# operand is a NORM
	bra.b		_L9_6s
_L9_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L9_3s			# no
	bsr.l		src_zero			# yes
	bra.b		_L9_6s
_L9_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L9_4s			# no
	bsr.l		t_operr			# yes
	bra.b		_L9_6s
_L9_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L9_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L9_6s
_L9_5s:
	bsr.l		stand			# operand is a DENORM
_L9_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_ftand_
_ftand_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L9_2d
	bsr.l		stan			# operand is a NORM
	bra.b		_L9_6d
_L9_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L9_3d			# no
	bsr.l		src_zero			# yes
	bra.b		_L9_6d
_L9_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L9_4d			# no
	bsr.l		t_operr			# yes
	bra.b		_L9_6d
_L9_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L9_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L9_6d
_L9_5d:
	bsr.l		stand			# operand is a DENORM
_L9_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_ftanx_
_ftanx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L9_2x
	bsr.l		stan			# operand is a NORM
	bra.b		_L9_6x
_L9_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L9_3x			# no
	bsr.l		src_zero			# yes
	bra.b		_L9_6x
_L9_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L9_4x			# no
	bsr.l		t_operr			# yes
	bra.b		_L9_6x
_L9_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L9_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L9_6x
_L9_5x:
	bsr.l		stand			# operand is a DENORM
_L9_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_fetoxs_
_fetoxs_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L10_2s
	bsr.l		setox			# operand is a NORM
	bra.b		_L10_6s
_L10_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L10_3s			# no
	bsr.l		ld_pone			# yes
	bra.b		_L10_6s
_L10_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L10_4s			# no
	bsr.l		szr_inf			# yes
	bra.b		_L10_6s
_L10_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L10_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L10_6s
_L10_5s:
	bsr.l		setoxd			# operand is a DENORM
_L10_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fetoxd_
_fetoxd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L10_2d
	bsr.l		setox			# operand is a NORM
	bra.b		_L10_6d
_L10_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L10_3d			# no
	bsr.l		ld_pone			# yes
	bra.b		_L10_6d
_L10_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L10_4d			# no
	bsr.l		szr_inf			# yes
	bra.b		_L10_6d
_L10_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L10_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L10_6d
_L10_5d:
	bsr.l		setoxd			# operand is a DENORM
_L10_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fetoxx_
_fetoxx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L10_2x
	bsr.l		setox			# operand is a NORM
	bra.b		_L10_6x
_L10_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L10_3x			# no
	bsr.l		ld_pone			# yes
	bra.b		_L10_6x
_L10_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L10_4x			# no
	bsr.l		szr_inf			# yes
	bra.b		_L10_6x
_L10_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L10_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L10_6x
_L10_5x:
	bsr.l		setoxd			# operand is a DENORM
_L10_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_ftwotoxs_
_ftwotoxs_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L11_2s
	bsr.l		stwotox			# operand is a NORM
	bra.b		_L11_6s
_L11_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L11_3s			# no
	bsr.l		ld_pone			# yes
	bra.b		_L11_6s
_L11_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L11_4s			# no
	bsr.l		szr_inf			# yes
	bra.b		_L11_6s
_L11_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L11_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L11_6s
_L11_5s:
	bsr.l		stwotoxd			# operand is a DENORM
_L11_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_ftwotoxd_
_ftwotoxd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L11_2d
	bsr.l		stwotox			# operand is a NORM
	bra.b		_L11_6d
_L11_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L11_3d			# no
	bsr.l		ld_pone			# yes
	bra.b		_L11_6d
_L11_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L11_4d			# no
	bsr.l		szr_inf			# yes
	bra.b		_L11_6d
_L11_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L11_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L11_6d
_L11_5d:
	bsr.l		stwotoxd			# operand is a DENORM
_L11_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_ftwotoxx_
_ftwotoxx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L11_2x
	bsr.l		stwotox			# operand is a NORM
	bra.b		_L11_6x
_L11_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L11_3x			# no
	bsr.l		ld_pone			# yes
	bra.b		_L11_6x
_L11_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L11_4x			# no
	bsr.l		szr_inf			# yes
	bra.b		_L11_6x
_L11_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L11_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L11_6x
_L11_5x:
	bsr.l		stwotoxd			# operand is a DENORM
_L11_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_ftentoxs_
_ftentoxs_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L12_2s
	bsr.l		stentox			# operand is a NORM
	bra.b		_L12_6s
_L12_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L12_3s			# no
	bsr.l		ld_pone			# yes
	bra.b		_L12_6s
_L12_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L12_4s			# no
	bsr.l		szr_inf			# yes
	bra.b		_L12_6s
_L12_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L12_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L12_6s
_L12_5s:
	bsr.l		stentoxd			# operand is a DENORM
_L12_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_ftentoxd_
_ftentoxd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L12_2d
	bsr.l		stentox			# operand is a NORM
	bra.b		_L12_6d
_L12_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L12_3d			# no
	bsr.l		ld_pone			# yes
	bra.b		_L12_6d
_L12_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L12_4d			# no
	bsr.l		szr_inf			# yes
	bra.b		_L12_6d
_L12_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L12_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L12_6d
_L12_5d:
	bsr.l		stentoxd			# operand is a DENORM
_L12_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_ftentoxx_
_ftentoxx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L12_2x
	bsr.l		stentox			# operand is a NORM
	bra.b		_L12_6x
_L12_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L12_3x			# no
	bsr.l		ld_pone			# yes
	bra.b		_L12_6x
_L12_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L12_4x			# no
	bsr.l		szr_inf			# yes
	bra.b		_L12_6x
_L12_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L12_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L12_6x
_L12_5x:
	bsr.l		stentoxd			# operand is a DENORM
_L12_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_flogns_
_flogns_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L13_2s
	bsr.l		slogn			# operand is a NORM
	bra.b		_L13_6s
_L13_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L13_3s			# no
	bsr.l		t_dz2			# yes
	bra.b		_L13_6s
_L13_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L13_4s			# no
	bsr.l		sopr_inf			# yes
	bra.b		_L13_6s
_L13_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L13_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L13_6s
_L13_5s:
	bsr.l		slognd			# operand is a DENORM
_L13_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_flognd_
_flognd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L13_2d
	bsr.l		slogn			# operand is a NORM
	bra.b		_L13_6d
_L13_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L13_3d			# no
	bsr.l		t_dz2			# yes
	bra.b		_L13_6d
_L13_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L13_4d			# no
	bsr.l		sopr_inf			# yes
	bra.b		_L13_6d
_L13_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L13_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L13_6d
_L13_5d:
	bsr.l		slognd			# operand is a DENORM
_L13_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_flognx_
_flognx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L13_2x
	bsr.l		slogn			# operand is a NORM
	bra.b		_L13_6x
_L13_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L13_3x			# no
	bsr.l		t_dz2			# yes
	bra.b		_L13_6x
_L13_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L13_4x			# no
	bsr.l		sopr_inf			# yes
	bra.b		_L13_6x
_L13_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L13_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L13_6x
_L13_5x:
	bsr.l		slognd			# operand is a DENORM
_L13_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_flog10s_
_flog10s_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L14_2s
	bsr.l		slog10			# operand is a NORM
	bra.b		_L14_6s
_L14_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L14_3s			# no
	bsr.l		t_dz2			# yes
	bra.b		_L14_6s
_L14_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L14_4s			# no
	bsr.l		sopr_inf			# yes
	bra.b		_L14_6s
_L14_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L14_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L14_6s
_L14_5s:
	bsr.l		slog10d			# operand is a DENORM
_L14_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_flog10d_
_flog10d_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L14_2d
	bsr.l		slog10			# operand is a NORM
	bra.b		_L14_6d
_L14_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L14_3d			# no
	bsr.l		t_dz2			# yes
	bra.b		_L14_6d
_L14_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L14_4d			# no
	bsr.l		sopr_inf			# yes
	bra.b		_L14_6d
_L14_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L14_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L14_6d
_L14_5d:
	bsr.l		slog10d			# operand is a DENORM
_L14_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_flog10x_
_flog10x_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L14_2x
	bsr.l		slog10			# operand is a NORM
	bra.b		_L14_6x
_L14_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L14_3x			# no
	bsr.l		t_dz2			# yes
	bra.b		_L14_6x
_L14_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L14_4x			# no
	bsr.l		sopr_inf			# yes
	bra.b		_L14_6x
_L14_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L14_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L14_6x
_L14_5x:
	bsr.l		slog10d			# operand is a DENORM
_L14_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_flog2s_
_flog2s_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L15_2s
	bsr.l		slog2			# operand is a NORM
	bra.b		_L15_6s
_L15_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L15_3s			# no
	bsr.l		t_dz2			# yes
	bra.b		_L15_6s
_L15_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L15_4s			# no
	bsr.l		sopr_inf			# yes
	bra.b		_L15_6s
_L15_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L15_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L15_6s
_L15_5s:
	bsr.l		slog2d			# operand is a DENORM
_L15_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_flog2d_
_flog2d_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L15_2d
	bsr.l		slog2			# operand is a NORM
	bra.b		_L15_6d
_L15_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L15_3d			# no
	bsr.l		t_dz2			# yes
	bra.b		_L15_6d
_L15_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L15_4d			# no
	bsr.l		sopr_inf			# yes
	bra.b		_L15_6d
_L15_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L15_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L15_6d
_L15_5d:
	bsr.l		slog2d			# operand is a DENORM
_L15_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_flog2x_
_flog2x_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L15_2x
	bsr.l		slog2			# operand is a NORM
	bra.b		_L15_6x
_L15_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L15_3x			# no
	bsr.l		t_dz2			# yes
	bra.b		_L15_6x
_L15_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L15_4x			# no
	bsr.l		sopr_inf			# yes
	bra.b		_L15_6x
_L15_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L15_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L15_6x
_L15_5x:
	bsr.l		slog2d			# operand is a DENORM
_L15_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_fcoshs_
_fcoshs_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L16_2s
	bsr.l		scosh			# operand is a NORM
	bra.b		_L16_6s
_L16_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L16_3s			# no
	bsr.l		ld_pone			# yes
	bra.b		_L16_6s
_L16_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L16_4s			# no
	bsr.l		ld_pinf			# yes
	bra.b		_L16_6s
_L16_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L16_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L16_6s
_L16_5s:
	bsr.l		scoshd			# operand is a DENORM
_L16_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fcoshd_
_fcoshd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L16_2d
	bsr.l		scosh			# operand is a NORM
	bra.b		_L16_6d
_L16_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L16_3d			# no
	bsr.l		ld_pone			# yes
	bra.b		_L16_6d
_L16_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L16_4d			# no
	bsr.l		ld_pinf			# yes
	bra.b		_L16_6d
_L16_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L16_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L16_6d
_L16_5d:
	bsr.l		scoshd			# operand is a DENORM
_L16_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fcoshx_
_fcoshx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L16_2x
	bsr.l		scosh			# operand is a NORM
	bra.b		_L16_6x
_L16_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L16_3x			# no
	bsr.l		ld_pone			# yes
	bra.b		_L16_6x
_L16_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L16_4x			# no
	bsr.l		ld_pinf			# yes
	bra.b		_L16_6x
_L16_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L16_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L16_6x
_L16_5x:
	bsr.l		scoshd			# operand is a DENORM
_L16_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_facoss_
_facoss_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L17_2s
	bsr.l		sacos			# operand is a NORM
	bra.b		_L17_6s
_L17_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L17_3s			# no
	bsr.l		ld_ppi2			# yes
	bra.b		_L17_6s
_L17_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L17_4s			# no
	bsr.l		t_operr			# yes
	bra.b		_L17_6s
_L17_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L17_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L17_6s
_L17_5s:
	bsr.l		sacosd			# operand is a DENORM
_L17_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_facosd_
_facosd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L17_2d
	bsr.l		sacos			# operand is a NORM
	bra.b		_L17_6d
_L17_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L17_3d			# no
	bsr.l		ld_ppi2			# yes
	bra.b		_L17_6d
_L17_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L17_4d			# no
	bsr.l		t_operr			# yes
	bra.b		_L17_6d
_L17_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L17_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L17_6d
_L17_5d:
	bsr.l		sacosd			# operand is a DENORM
_L17_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_facosx_
_facosx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L17_2x
	bsr.l		sacos			# operand is a NORM
	bra.b		_L17_6x
_L17_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L17_3x			# no
	bsr.l		ld_ppi2			# yes
	bra.b		_L17_6x
_L17_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L17_4x			# no
	bsr.l		t_operr			# yes
	bra.b		_L17_6x
_L17_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L17_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L17_6x
_L17_5x:
	bsr.l		sacosd			# operand is a DENORM
_L17_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_fgetexps_
_fgetexps_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L18_2s
	bsr.l		sgetexp			# operand is a NORM
	bra.b		_L18_6s
_L18_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L18_3s			# no
	bsr.l		src_zero			# yes
	bra.b		_L18_6s
_L18_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L18_4s			# no
	bsr.l		t_operr			# yes
	bra.b		_L18_6s
_L18_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L18_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L18_6s
_L18_5s:
	bsr.l		sgetexpd			# operand is a DENORM
_L18_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fgetexpd_
_fgetexpd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L18_2d
	bsr.l		sgetexp			# operand is a NORM
	bra.b		_L18_6d
_L18_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L18_3d			# no
	bsr.l		src_zero			# yes
	bra.b		_L18_6d
_L18_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L18_4d			# no
	bsr.l		t_operr			# yes
	bra.b		_L18_6d
_L18_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L18_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L18_6d
_L18_5d:
	bsr.l		sgetexpd			# operand is a DENORM
_L18_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fgetexpx_
_fgetexpx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L18_2x
	bsr.l		sgetexp			# operand is a NORM
	bra.b		_L18_6x
_L18_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L18_3x			# no
	bsr.l		src_zero			# yes
	bra.b		_L18_6x
_L18_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L18_4x			# no
	bsr.l		t_operr			# yes
	bra.b		_L18_6x
_L18_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L18_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L18_6x
_L18_5x:
	bsr.l		sgetexpd			# operand is a DENORM
_L18_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_fgetmans_
_fgetmans_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L19_2s
	bsr.l		sgetman			# operand is a NORM
	bra.b		_L19_6s
_L19_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L19_3s			# no
	bsr.l		src_zero			# yes
	bra.b		_L19_6s
_L19_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L19_4s			# no
	bsr.l		t_operr			# yes
	bra.b		_L19_6s
_L19_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L19_5s			# no
	bsr.l		src_qnan			# yes
	bra.b		_L19_6s
_L19_5s:
	bsr.l		sgetmand			# operand is a DENORM
_L19_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fgetmand_
_fgetmand_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L19_2d
	bsr.l		sgetman			# operand is a NORM
	bra.b		_L19_6d
_L19_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L19_3d			# no
	bsr.l		src_zero			# yes
	bra.b		_L19_6d
_L19_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L19_4d			# no
	bsr.l		t_operr			# yes
	bra.b		_L19_6d
_L19_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L19_5d			# no
	bsr.l		src_qnan			# yes
	bra.b		_L19_6d
_L19_5d:
	bsr.l		sgetmand			# operand is a DENORM
_L19_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fgetmanx_
_fgetmanx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L19_2x
	bsr.l		sgetman			# operand is a NORM
	bra.b		_L19_6x
_L19_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L19_3x			# no
	bsr.l		src_zero			# yes
	bra.b		_L19_6x
_L19_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L19_4x			# no
	bsr.l		t_operr			# yes
	bra.b		_L19_6x
_L19_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L19_5x			# no
	bsr.l		src_qnan			# yes
	bra.b		_L19_6x
_L19_5x:
	bsr.l		sgetmand			# operand is a DENORM
_L19_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# MONADIC TEMPLATE							#
#########################################################################
	global		_fsincoss_
_fsincoss_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L20_2s
	bsr.l		ssincos			# operand is a NORM
	bra.b		_L20_6s
_L20_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L20_3s			# no
	bsr.l		ssincosz			# yes
	bra.b		_L20_6s
_L20_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L20_4s			# no
	bsr.l		ssincosi			# yes
	bra.b		_L20_6s
_L20_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L20_5s			# no
	bsr.l		ssincosqnan			# yes
	bra.b		_L20_6s
_L20_5s:
	bsr.l		ssincosd			# operand is a DENORM
_L20_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		&0x03,-(%sp)		# store off fp0/fp1
	fmovm.x		(%sp)+,&0x40		# fp0 now in fp1
	fmovm.x		(%sp)+,&0x80		# fp1 now in fp0
	unlk		%a6
	rts

	global		_fsincosd_
_fsincosd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl input
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	mov.b		%d1,STAG(%a6)
	tst.b		%d1
	bne.b		_L20_2d
	bsr.l		ssincos			# operand is a NORM
	bra.b		_L20_6d
_L20_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L20_3d			# no
	bsr.l		ssincosz			# yes
	bra.b		_L20_6d
_L20_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L20_4d			# no
	bsr.l		ssincosi			# yes
	bra.b		_L20_6d
_L20_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L20_5d			# no
	bsr.l		ssincosqnan			# yes
	bra.b		_L20_6d
_L20_5d:
	bsr.l		ssincosd			# operand is a DENORM
_L20_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		&0x03,-(%sp)		# store off fp0/fp1
	fmovm.x		(%sp)+,&0x40		# fp0 now in fp1
	fmovm.x		(%sp)+,&0x80		# fp1 now in fp0
	unlk		%a6
	rts

	global		_fsincosx_
_fsincosx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_SRC(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext input
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.b		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	tst.b		%d1
	bne.b		_L20_2x
	bsr.l		ssincos			# operand is a NORM
	bra.b		_L20_6x
_L20_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L20_3x			# no
	bsr.l		ssincosz			# yes
	bra.b		_L20_6x
_L20_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L20_4x			# no
	bsr.l		ssincosi			# yes
	bra.b		_L20_6x
_L20_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L20_5x			# no
	bsr.l		ssincosqnan			# yes
	bra.b		_L20_6x
_L20_5x:
	bsr.l		ssincosd			# operand is a DENORM
_L20_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		&0x03,-(%sp)		# store off fp0/fp1
	fmovm.x		(%sp)+,&0x40		# fp0 now in fp1
	fmovm.x		(%sp)+,&0x80		# fp1 now in fp0
	unlk		%a6
	rts


#########################################################################
# DYADIC TEMPLATE							#
#########################################################################
	global		_frems_
_frems_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl dst
	fmov.x		%fp0,FP_DST(%a6)
	lea		FP_DST(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,DTAG(%a6)

	fmov.s		0xc(%a6),%fp0		# load sgl src
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.l		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	lea		FP_SRC(%a6),%a0		# pass ptr to src
	lea		FP_DST(%a6),%a1		# pass ptr to dst

	tst.b		%d1
	bne.b		_L21_2s
	bsr.l		srem_snorm			# operand is a NORM
	bra.b		_L21_6s
_L21_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L21_3s			# no
	bsr.l		srem_szero			# yes
	bra.b		_L21_6s
_L21_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L21_4s			# no
	bsr.l		srem_sinf			# yes
	bra.b		_L21_6s
_L21_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L21_5s			# no
	bsr.l		sop_sqnan			# yes
	bra.b		_L21_6s
_L21_5s:
	bsr.l		srem_sdnrm			# operand is a DENORM
_L21_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fremd_
_fremd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl dst
	fmov.x		%fp0,FP_DST(%a6)
	lea		FP_DST(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,DTAG(%a6)

	fmov.d		0x10(%a6),%fp0		# load dbl src
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.l		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	lea		FP_SRC(%a6),%a0		# pass ptr to src
	lea		FP_DST(%a6),%a1		# pass ptr to dst

	tst.b		%d1
	bne.b		_L21_2d
	bsr.l		srem_snorm			# operand is a NORM
	bra.b		_L21_6d
_L21_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L21_3d			# no
	bsr.l		srem_szero			# yes
	bra.b		_L21_6d
_L21_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L21_4d			# no
	bsr.l		srem_sinf			# yes
	bra.b		_L21_6d
_L21_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L21_5d			# no
	bsr.l		sop_sqnan			# yes
	bra.b		_L21_6d
_L21_5d:
	bsr.l		srem_sdnrm			# operand is a DENORM
_L21_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fremx_
_fremx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_DST(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext dst
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,DTAG(%a6)

	lea		FP_SRC(%a6),%a0
	mov.l		0x14+0x0(%a6),0x0(%a0)	# load ext src
	mov.l		0x14+0x4(%a6),0x4(%a0)
	mov.l		0x14+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.l		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	lea		FP_SRC(%a6),%a0		# pass ptr to src
	lea		FP_DST(%a6),%a1		# pass ptr to dst

	tst.b		%d1
	bne.b		_L21_2x
	bsr.l		srem_snorm			# operand is a NORM
	bra.b		_L21_6x
_L21_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L21_3x			# no
	bsr.l		srem_szero			# yes
	bra.b		_L21_6x
_L21_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L21_4x			# no
	bsr.l		srem_sinf			# yes
	bra.b		_L21_6x
_L21_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L21_5x			# no
	bsr.l		sop_sqnan			# yes
	bra.b		_L21_6x
_L21_5x:
	bsr.l		srem_sdnrm			# operand is a DENORM
_L21_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# DYADIC TEMPLATE							#
#########################################################################
	global		_fmods_
_fmods_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl dst
	fmov.x		%fp0,FP_DST(%a6)
	lea		FP_DST(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,DTAG(%a6)

	fmov.s		0xc(%a6),%fp0		# load sgl src
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.l		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	lea		FP_SRC(%a6),%a0		# pass ptr to src
	lea		FP_DST(%a6),%a1		# pass ptr to dst

	tst.b		%d1
	bne.b		_L22_2s
	bsr.l		smod_snorm			# operand is a NORM
	bra.b		_L22_6s
_L22_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L22_3s			# no
	bsr.l		smod_szero			# yes
	bra.b		_L22_6s
_L22_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L22_4s			# no
	bsr.l		smod_sinf			# yes
	bra.b		_L22_6s
_L22_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L22_5s			# no
	bsr.l		sop_sqnan			# yes
	bra.b		_L22_6s
_L22_5s:
	bsr.l		smod_sdnrm			# operand is a DENORM
_L22_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fmodd_
_fmodd_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl dst
	fmov.x		%fp0,FP_DST(%a6)
	lea		FP_DST(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,DTAG(%a6)

	fmov.d		0x10(%a6),%fp0		# load dbl src
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.l		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	lea		FP_SRC(%a6),%a0		# pass ptr to src
	lea		FP_DST(%a6),%a1		# pass ptr to dst

	tst.b		%d1
	bne.b		_L22_2d
	bsr.l		smod_snorm			# operand is a NORM
	bra.b		_L22_6d
_L22_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L22_3d			# no
	bsr.l		smod_szero			# yes
	bra.b		_L22_6d
_L22_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L22_4d			# no
	bsr.l		smod_sinf			# yes
	bra.b		_L22_6d
_L22_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L22_5d			# no
	bsr.l		sop_sqnan			# yes
	bra.b		_L22_6d
_L22_5d:
	bsr.l		smod_sdnrm			# operand is a DENORM
_L22_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fmodx_
_fmodx_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_DST(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext dst
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,DTAG(%a6)

	lea		FP_SRC(%a6),%a0
	mov.l		0x14+0x0(%a6),0x0(%a0)	# load ext src
	mov.l		0x14+0x4(%a6),0x4(%a0)
	mov.l		0x14+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.l		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	lea		FP_SRC(%a6),%a0		# pass ptr to src
	lea		FP_DST(%a6),%a1		# pass ptr to dst

	tst.b		%d1
	bne.b		_L22_2x
	bsr.l		smod_snorm			# operand is a NORM
	bra.b		_L22_6x
_L22_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L22_3x			# no
	bsr.l		smod_szero			# yes
	bra.b		_L22_6x
_L22_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L22_4x			# no
	bsr.l		smod_sinf			# yes
	bra.b		_L22_6x
_L22_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L22_5x			# no
	bsr.l		sop_sqnan			# yes
	bra.b		_L22_6x
_L22_5x:
	bsr.l		smod_sdnrm			# operand is a DENORM
_L22_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# DYADIC TEMPLATE							#
#########################################################################
	global		_fscales_
_fscales_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.s		0x8(%a6),%fp0		# load sgl dst
	fmov.x		%fp0,FP_DST(%a6)
	lea		FP_DST(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,DTAG(%a6)

	fmov.s		0xc(%a6),%fp0		# load sgl src
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.l		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	lea		FP_SRC(%a6),%a0		# pass ptr to src
	lea		FP_DST(%a6),%a1		# pass ptr to dst

	tst.b		%d1
	bne.b		_L23_2s
	bsr.l		sscale_snorm			# operand is a NORM
	bra.b		_L23_6s
_L23_2s:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L23_3s			# no
	bsr.l		sscale_szero			# yes
	bra.b		_L23_6s
_L23_3s:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L23_4s			# no
	bsr.l		sscale_sinf			# yes
	bra.b		_L23_6s
_L23_4s:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L23_5s			# no
	bsr.l		sop_sqnan			# yes
	bra.b		_L23_6s
_L23_5s:
	bsr.l		sscale_sdnrm			# operand is a DENORM
_L23_6s:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fscaled_
_fscaled_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	fmov.d		0x8(%a6),%fp0		# load dbl dst
	fmov.x		%fp0,FP_DST(%a6)
	lea		FP_DST(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,DTAG(%a6)

	fmov.d		0x10(%a6),%fp0		# load dbl src
	fmov.x		%fp0,FP_SRC(%a6)
	lea		FP_SRC(%a6),%a0
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.l		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	lea		FP_SRC(%a6),%a0		# pass ptr to src
	lea		FP_DST(%a6),%a1		# pass ptr to dst

	tst.b		%d1
	bne.b		_L23_2d
	bsr.l		sscale_snorm			# operand is a NORM
	bra.b		_L23_6d
_L23_2d:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L23_3d			# no
	bsr.l		sscale_szero			# yes
	bra.b		_L23_6d
_L23_3d:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L23_4d			# no
	bsr.l		sscale_sinf			# yes
	bra.b		_L23_6d
_L23_4d:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L23_5d			# no
	bsr.l		sop_sqnan			# yes
	bra.b		_L23_6d
_L23_5d:
	bsr.l		sscale_sdnrm			# operand is a DENORM
_L23_6d:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts

	global		_fscalex_
_fscalex_:
	link		%a6,&-LOCAL_SIZE

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FP0(%a6)	# save fp0/fp1

	fmov.l		&0x0,%fpcr		# zero FPCR

#
#	copy, convert, and tag input argument
#
	lea		FP_DST(%a6),%a0
	mov.l		0x8+0x0(%a6),0x0(%a0)	# load ext dst
	mov.l		0x8+0x4(%a6),0x4(%a0)
	mov.l		0x8+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,DTAG(%a6)

	lea		FP_SRC(%a6),%a0
	mov.l		0x14+0x0(%a6),0x0(%a0)	# load ext src
	mov.l		0x14+0x4(%a6),0x4(%a0)
	mov.l		0x14+0x8(%a6),0x8(%a0)
	bsr.l		tag			# fetch operand type
	mov.b		%d0,STAG(%a6)
	mov.l		%d0,%d1

	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd mode,prec

	lea		FP_SRC(%a6),%a0		# pass ptr to src
	lea		FP_DST(%a6),%a1		# pass ptr to dst

	tst.b		%d1
	bne.b		_L23_2x
	bsr.l		sscale_snorm			# operand is a NORM
	bra.b		_L23_6x
_L23_2x:
	cmpi.b		%d1,&ZERO		# is operand a ZERO?
	bne.b		_L23_3x			# no
	bsr.l		sscale_szero			# yes
	bra.b		_L23_6x
_L23_3x:
	cmpi.b		%d1,&INF		# is operand an INF?
	bne.b		_L23_4x			# no
	bsr.l		sscale_sinf			# yes
	bra.b		_L23_6x
_L23_4x:
	cmpi.b		%d1,&QNAN		# is operand a QNAN?
	bne.b		_L23_5x			# no
	bsr.l		sop_sqnan			# yes
	bra.b		_L23_6x
_L23_5x:
	bsr.l		sscale_sdnrm			# operand is a DENORM
_L23_6x:

#
#	Result is now in FP0
#
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr # restore ctrl regs
	fmovm.x		EXC_FP1(%a6),&0x40	# restore fp1
	unlk		%a6
	rts


#########################################################################
# ssin():     computes the sine of a normalized input			#
# ssind():    computes the sine of a denormalized input			#
# scos():     computes the cosine of a normalized input			#
# scosd():    computes the cosine of a denormalized input		#
# ssincos():  computes the sine and cosine of a normalized input	#
# ssincosd(): computes the sine and cosine of a denormalized input	#
#									#
# INPUT *************************************************************** #
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT ************************************************************** #
#	fp0 = sin(X) or cos(X)						#
#									#
#    For ssincos(X):							#
#	fp0 = sin(X)							#
#	fp1 = cos(X)							#
#									#
# ACCURACY and MONOTONICITY ******************************************* #
#	The returned result is within 1 ulp in 64 significant bit, i.e.	#
#	within 0.5001 ulp to 53 bits if the result is subsequently	#
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM ***********************************************************	#
#									#
#	SIN and COS:							#
#	1. If SIN is invoked, set AdjN := 0; otherwise, set AdjN := 1.	#
#									#
#	2. If |X| >= 15Pi or |X| < 2**(-40), go to 7.			#
#									#
#	3. Decompose X as X = N(Pi/2) + r where |r| <= Pi/4. Let	#
#		k = N mod 4, so in particular, k = 0,1,2,or 3.		#
#		Overwrite k by k := k + AdjN.				#
#									#
#	4. If k is even, go to 6.					#
#									#
#	5. (k is odd) Set j := (k-1)/2, sgn := (-1)**j.			#
#		Return sgn*cos(r) where cos(r) is approximated by an	#
#		even polynomial in r, 1 + r*r*(B1+s*(B2+ ... + s*B8)),	#
#		s = r*r.						#
#		Exit.							#
#									#
#	6. (k is even) Set j := k/2, sgn := (-1)**j. Return sgn*sin(r)	#
#		where sin(r) is approximated by an odd polynomial in r	#
#		r + r*s*(A1+s*(A2+ ... + s*A7)),	s = r*r.	#
#		Exit.							#
#									#
#	7. If |X| > 1, go to 9.						#
#									#
#	8. (|X|<2**(-40)) If SIN is invoked, return X;			#
#		otherwise return 1.					#
#									#
#	9. Overwrite X by X := X rem 2Pi. Now that |X| <= Pi,		#
#		go back to 3.						#
#									#
#	SINCOS:								#
#	1. If |X| >= 15Pi or |X| < 2**(-40), go to 6.			#
#									#
#	2. Decompose X as X = N(Pi/2) + r where |r| <= Pi/4. Let	#
#		k = N mod 4, so in particular, k = 0,1,2,or 3.		#
#									#
#	3. If k is even, go to 5.					#
#									#
#	4. (k is odd) Set j1 := (k-1)/2, j2 := j1 (EOR) (k mod 2), ie.	#
#		j1 exclusive or with the l.s.b. of k.			#
#		sgn1 := (-1)**j1, sgn2 := (-1)**j2.			#
#		SIN(X) = sgn1 * cos(r) and COS(X) = sgn2*sin(r) where	#
#		sin(r) and cos(r) are computed as odd and even		#
#		polynomials in r, respectively. Exit			#
#									#
#	5. (k is even) Set j1 := k/2, sgn1 := (-1)**j1.			#
#		SIN(X) = sgn1 * sin(r) and COS(X) = sgn1*cos(r) where	#
#		sin(r) and cos(r) are computed as odd and even		#
#		polynomials in r, respectively. Exit			#
#									#
#	6. If |X| > 1, go to 8.						#
#									#
#	7. (|X|<2**(-40)) SIN(X) = X and COS(X) = 1. Exit.		#
#									#
#	8. Overwrite X by X := X rem 2Pi. Now that |X| <= Pi,		#
#		go back to 2.						#
#									#
#########################################################################

SINA7:	long		0xBD6AAA77,0xCCC994F5
SINA6:	long		0x3DE61209,0x7AAE8DA1
SINA5:	long		0xBE5AE645,0x2A118AE4
SINA4:	long		0x3EC71DE3,0xA5341531
SINA3:	long		0xBF2A01A0,0x1A018B59,0x00000000,0x00000000
SINA2:	long		0x3FF80000,0x88888888,0x888859AF,0x00000000
SINA1:	long		0xBFFC0000,0xAAAAAAAA,0xAAAAAA99,0x00000000

COSB8:	long		0x3D2AC4D0,0xD6011EE3
COSB7:	long		0xBDA9396F,0x9F45AC19
COSB6:	long		0x3E21EED9,0x0612C972
COSB5:	long		0xBE927E4F,0xB79D9FCF
COSB4:	long		0x3EFA01A0,0x1A01D423,0x00000000,0x00000000
COSB3:	long		0xBFF50000,0xB60B60B6,0x0B61D438,0x00000000
COSB2:	long		0x3FFA0000,0xAAAAAAAA,0xAAAAAB5E
COSB1:	long		0xBF000000

	set		INARG,FP_SCR0

	set		X,FP_SCR0
#	set		XDCARE,X+2
	set		XFRAC,X+4

	set		RPRIME,FP_SCR0
	set		SPRIME,FP_SCR1

	set		POSNEG1,L_SCR1
	set		TWOTO63,L_SCR1

	set		ENDFLAG,L_SCR2
	set		INT,L_SCR2

	set		ADJN,L_SCR3

############################################
	global		ssin
ssin:
	mov.l		&0,ADJN(%a6)		# yes; SET ADJN TO 0
	bra.b		SINBGN

############################################
	global		scos
scos:
	mov.l		&1,ADJN(%a6)		# yes; SET ADJN TO 1

############################################
SINBGN:
#--SAVE FPCR, FP1. CHECK IF |X| IS TOO SMALL OR LARGE

	fmov.x		(%a0),%fp0		# LOAD INPUT
	fmov.x		%fp0,X(%a6)		# save input at X

# "COMPACTIFY" X
	mov.l		(%a0),%d1		# put exp in hi word
	mov.w		4(%a0),%d1		# fetch hi(man)
	and.l		&0x7FFFFFFF,%d1		# strip sign

	cmpi.l		%d1,&0x3FD78000		# is |X| >= 2**(-40)?
	bge.b		SOK1			# no
	bra.w		SINSM			# yes; input is very small

SOK1:
	cmp.l		%d1,&0x4004BC7E		# is |X| < 15 PI?
	blt.b		SINMAIN			# no
	bra.w		SREDUCEX		# yes; input is very large

#--THIS IS THE USUAL CASE, |X| <= 15 PI.
#--THE ARGUMENT REDUCTION IS DONE BY TABLE LOOK UP.
SINMAIN:
	fmov.x		%fp0,%fp1
	fmul.d		TWOBYPI(%pc),%fp1	# X*2/PI

	lea		PITBL+0x200(%pc),%a1	# TABLE OF N*PI/2, N = -32,...,32

	fmov.l		%fp1,INT(%a6)		# CONVERT TO INTEGER

	mov.l		INT(%a6),%d1		# make a copy of N
	asl.l		&4,%d1			# N *= 16
	add.l		%d1,%a1			# tbl_addr = a1 + (N*16)

# A1 IS THE ADDRESS OF N*PIBY2
# ...WHICH IS IN TWO PIECES Y1 & Y2
	fsub.x		(%a1)+,%fp0		# X-Y1
	fsub.s		(%a1),%fp0		# fp0 = R = (X-Y1)-Y2

SINCONT:
#--continuation from REDUCEX

#--GET N+ADJN AND SEE IF SIN(R) OR COS(R) IS NEEDED
	mov.l		INT(%a6),%d1
	add.l		ADJN(%a6),%d1		# SEE IF D0 IS ODD OR EVEN
	ror.l		&1,%d1			# D0 WAS ODD IFF D0 IS NEGATIVE
	cmp.l		%d1,&0
	blt.w		COSPOLY

#--LET J BE THE LEAST SIG. BIT OF D0, LET SGN := (-1)**J.
#--THEN WE RETURN	SGN*SIN(R). SGN*SIN(R) IS COMPUTED BY
#--R' + R'*S*(A1 + S(A2 + S(A3 + S(A4 + ... + SA7)))), WHERE
#--R' = SGN*R, S=R*R. THIS CAN BE REWRITTEN AS
#--R' + R'*S*( [A1+T(A3+T(A5+TA7))] + [S(A2+T(A4+TA6))])
#--WHERE T=S*S.
#--NOTE THAT A3 THROUGH A7 ARE STORED IN DOUBLE PRECISION
#--WHILE A1 AND A2 ARE IN DOUBLE-EXTENDED FORMAT.
SINPOLY:
	fmovm.x		&0x0c,-(%sp)		# save fp2/fp3

	fmov.x		%fp0,X(%a6)		# X IS R
	fmul.x		%fp0,%fp0		# FP0 IS S

	fmov.d		SINA7(%pc),%fp3
	fmov.d		SINA6(%pc),%fp2

	fmov.x		%fp0,%fp1
	fmul.x		%fp1,%fp1		# FP1 IS T

	ror.l		&1,%d1
	and.l		&0x80000000,%d1
# ...LEAST SIG. BIT OF D0 IN SIGN POSITION
	eor.l		%d1,X(%a6)		# X IS NOW R'= SGN*R

	fmul.x		%fp1,%fp3		# TA7
	fmul.x		%fp1,%fp2		# TA6

	fadd.d		SINA5(%pc),%fp3		# A5+TA7
	fadd.d		SINA4(%pc),%fp2		# A4+TA6

	fmul.x		%fp1,%fp3		# T(A5+TA7)
	fmul.x		%fp1,%fp2		# T(A4+TA6)

	fadd.d		SINA3(%pc),%fp3		# A3+T(A5+TA7)
	fadd.x		SINA2(%pc),%fp2		# A2+T(A4+TA6)

	fmul.x		%fp3,%fp1		# T(A3+T(A5+TA7))

	fmul.x		%fp0,%fp2		# S(A2+T(A4+TA6))
	fadd.x		SINA1(%pc),%fp1		# A1+T(A3+T(A5+TA7))
	fmul.x		X(%a6),%fp0		# R'*S

	fadd.x		%fp2,%fp1		# [A1+T(A3+T(A5+TA7))]+[S(A2+T(A4+TA6))]

	fmul.x		%fp1,%fp0		# SIN(R')-R'

	fmovm.x		(%sp)+,&0x30		# restore fp2/fp3

	fmov.l		%d0,%fpcr		# restore users round mode,prec
	fadd.x		X(%a6),%fp0		# last inst - possible exception set
	bra		t_inx2

#--LET J BE THE LEAST SIG. BIT OF D0, LET SGN := (-1)**J.
#--THEN WE RETURN	SGN*COS(R). SGN*COS(R) IS COMPUTED BY
#--SGN + S'*(B1 + S(B2 + S(B3 + S(B4 + ... + SB8)))), WHERE
#--S=R*R AND S'=SGN*S. THIS CAN BE REWRITTEN AS
#--SGN + S'*([B1+T(B3+T(B5+TB7))] + [S(B2+T(B4+T(B6+TB8)))])
#--WHERE T=S*S.
#--NOTE THAT B4 THROUGH B8 ARE STORED IN DOUBLE PRECISION
#--WHILE B2 AND B3 ARE IN DOUBLE-EXTENDED FORMAT, B1 IS -1/2
#--AND IS THEREFORE STORED AS SINGLE PRECISION.
COSPOLY:
	fmovm.x		&0x0c,-(%sp)		# save fp2/fp3

	fmul.x		%fp0,%fp0		# FP0 IS S

	fmov.d		COSB8(%pc),%fp2
	fmov.d		COSB7(%pc),%fp3

	fmov.x		%fp0,%fp1
	fmul.x		%fp1,%fp1		# FP1 IS T

	fmov.x		%fp0,X(%a6)		# X IS S
	ror.l		&1,%d1
	and.l		&0x80000000,%d1
# ...LEAST SIG. BIT OF D0 IN SIGN POSITION

	fmul.x		%fp1,%fp2		# TB8

	eor.l		%d1,X(%a6)		# X IS NOW S'= SGN*S
	and.l		&0x80000000,%d1

	fmul.x		%fp1,%fp3		# TB7

	or.l		&0x3F800000,%d1		# D0 IS SGN IN SINGLE
	mov.l		%d1,POSNEG1(%a6)

	fadd.d		COSB6(%pc),%fp2		# B6+TB8
	fadd.d		COSB5(%pc),%fp3		# B5+TB7

	fmul.x		%fp1,%fp2		# T(B6+TB8)
	fmul.x		%fp1,%fp3		# T(B5+TB7)

	fadd.d		COSB4(%pc),%fp2		# B4+T(B6+TB8)
	fadd.x		COSB3(%pc),%fp3		# B3+T(B5+TB7)

	fmul.x		%fp1,%fp2		# T(B4+T(B6+TB8))
	fmul.x		%fp3,%fp1		# T(B3+T(B5+TB7))

	fadd.x		COSB2(%pc),%fp2		# B2+T(B4+T(B6+TB8))
	fadd.s		COSB1(%pc),%fp1		# B1+T(B3+T(B5+TB7))

	fmul.x		%fp2,%fp0		# S(B2+T(B4+T(B6+TB8)))

	fadd.x		%fp1,%fp0

	fmul.x		X(%a6),%fp0

	fmovm.x		(%sp)+,&0x30		# restore fp2/fp3

	fmov.l		%d0,%fpcr		# restore users round mode,prec
	fadd.s		POSNEG1(%a6),%fp0	# last inst - possible exception set
	bra		t_inx2

##############################################

# SINe: Big OR Small?
#--IF |X| > 15PI, WE USE THE GENERAL ARGUMENT REDUCTION.
#--IF |X| < 2**(-40), RETURN X OR 1.
SINBORS:
	cmp.l		%d1,&0x3FFF8000
	bgt.l		SREDUCEX

SINSM:
	mov.l		ADJN(%a6),%d1
	cmp.l		%d1,&0
	bgt.b		COSTINY

# here, the operation may underflow iff the precision is sgl or dbl.
# extended denorms are handled through another entry point.
SINTINY:
#	mov.w		&0x0000,XDCARE(%a6)	# JUST IN CASE

	fmov.l		%d0,%fpcr		# restore users round mode,prec
	mov.b		&FMOV_OP,%d1		# last inst is MOVE
	fmov.x		X(%a6),%fp0		# last inst - possible exception set
	bra		t_catch

COSTINY:
	fmov.s		&0x3F800000,%fp0	# fp0 = 1.0
	fmov.l		%d0,%fpcr		# restore users round mode,prec
	fadd.s		&0x80800000,%fp0	# last inst - possible exception set
	bra		t_pinx2

################################################
	global		ssind
#--SIN(X) = X FOR DENORMALIZED X
ssind:
	bra		t_extdnrm

############################################
	global		scosd
#--COS(X) = 1 FOR DENORMALIZED X
scosd:
	fmov.s		&0x3F800000,%fp0	# fp0 = 1.0
	bra		t_pinx2

##################################################

	global		ssincos
ssincos:
#--SET ADJN TO 4
	mov.l		&4,ADJN(%a6)

	fmov.x		(%a0),%fp0		# LOAD INPUT
	fmov.x		%fp0,X(%a6)

	mov.l		(%a0),%d1
	mov.w		4(%a0),%d1
	and.l		&0x7FFFFFFF,%d1		# COMPACTIFY X

	cmp.l		%d1,&0x3FD78000		# |X| >= 2**(-40)?
	bge.b		SCOK1
	bra.w		SCSM

SCOK1:
	cmp.l		%d1,&0x4004BC7E		# |X| < 15 PI?
	blt.b		SCMAIN
	bra.w		SREDUCEX


#--THIS IS THE USUAL CASE, |X| <= 15 PI.
#--THE ARGUMENT REDUCTION IS DONE BY TABLE LOOK UP.
SCMAIN:
	fmov.x		%fp0,%fp1

	fmul.d		TWOBYPI(%pc),%fp1	# X*2/PI

	lea		PITBL+0x200(%pc),%a1	# TABLE OF N*PI/2, N = -32,...,32

	fmov.l		%fp1,INT(%a6)		# CONVERT TO INTEGER

	mov.l		INT(%a6),%d1
	asl.l		&4,%d1
	add.l		%d1,%a1			# ADDRESS OF N*PIBY2, IN Y1, Y2

	fsub.x		(%a1)+,%fp0		# X-Y1
	fsub.s		(%a1),%fp0		# FP0 IS R = (X-Y1)-Y2

SCCONT:
#--continuation point from REDUCEX

	mov.l		INT(%a6),%d1
	ror.l		&1,%d1
	cmp.l		%d1,&0			# D0 < 0 IFF N IS ODD
	bge.w		NEVEN

SNODD:
#--REGISTERS SAVED SO FAR: D0, A0, FP2.
	fmovm.x		&0x04,-(%sp)		# save fp2

	fmov.x		%fp0,RPRIME(%a6)
	fmul.x		%fp0,%fp0		# FP0 IS S = R*R
	fmov.d		SINA7(%pc),%fp1		# A7
	fmov.d		COSB8(%pc),%fp2		# B8
	fmul.x		%fp0,%fp1		# SA7
	fmul.x		%fp0,%fp2		# SB8

	mov.l		%d2,-(%sp)
	mov.l		%d1,%d2
	ror.l		&1,%d2
	and.l		&0x80000000,%d2
	eor.l		%d1,%d2
	and.l		&0x80000000,%d2

	fadd.d		SINA6(%pc),%fp1		# A6+SA7
	fadd.d		COSB7(%pc),%fp2		# B7+SB8

	fmul.x		%fp0,%fp1		# S(A6+SA7)
	eor.l		%d2,RPRIME(%a6)
	mov.l		(%sp)+,%d2
	fmul.x		%fp0,%fp2		# S(B7+SB8)
	ror.l		&1,%d1
	and.l		&0x80000000,%d1
	mov.l		&0x3F800000,POSNEG1(%a6)
	eor.l		%d1,POSNEG1(%a6)

	fadd.d		SINA5(%pc),%fp1		# A5+S(A6+SA7)
	fadd.d		COSB6(%pc),%fp2		# B6+S(B7+SB8)

	fmul.x		%fp0,%fp1		# S(A5+S(A6+SA7))
	fmul.x		%fp0,%fp2		# S(B6+S(B7+SB8))
	fmov.x		%fp0,SPRIME(%a6)

	fadd.d		SINA4(%pc),%fp1		# A4+S(A5+S(A6+SA7))
	eor.l		%d1,SPRIME(%a6)
	fadd.d		COSB5(%pc),%fp2		# B5+S(B6+S(B7+SB8))

	fmul.x		%fp0,%fp1		# S(A4+...)
	fmul.x		%fp0,%fp2		# S(B5+...)

	fadd.d		SINA3(%pc),%fp1		# A3+S(A4+...)
	fadd.d		COSB4(%pc),%fp2		# B4+S(B5+...)

	fmul.x		%fp0,%fp1		# S(A3+...)
	fmul.x		%fp0,%fp2		# S(B4+...)

	fadd.x		SINA2(%pc),%fp1		# A2+S(A3+...)
	fadd.x		COSB3(%pc),%fp2		# B3+S(B4+...)

	fmul.x		%fp0,%fp1		# S(A2+...)
	fmul.x		%fp0,%fp2		# S(B3+...)

	fadd.x		SINA1(%pc),%fp1		# A1+S(A2+...)
	fadd.x		COSB2(%pc),%fp2		# B2+S(B3+...)

	fmul.x		%fp0,%fp1		# S(A1+...)
	fmul.x		%fp2,%fp0		# S(B2+...)

	fmul.x		RPRIME(%a6),%fp1	# R'S(A1+...)
	fadd.s		COSB1(%pc),%fp0		# B1+S(B2...)
	fmul.x		SPRIME(%a6),%fp0	# S'(B1+S(B2+...))

	fmovm.x		(%sp)+,&0x20		# restore fp2

	fmov.l		%d0,%fpcr
	fadd.x		RPRIME(%a6),%fp1	# COS(X)
	bsr		sto_cos			# store cosine result
	fadd.s		POSNEG1(%a6),%fp0	# SIN(X)
	bra		t_inx2

NEVEN:
#--REGISTERS SAVED SO FAR: FP2.
	fmovm.x		&0x04,-(%sp)		# save fp2

	fmov.x		%fp0,RPRIME(%a6)
	fmul.x		%fp0,%fp0		# FP0 IS S = R*R

	fmov.d		COSB8(%pc),%fp1		# B8
	fmov.d		SINA7(%pc),%fp2		# A7

	fmul.x		%fp0,%fp1		# SB8
	fmov.x		%fp0,SPRIME(%a6)
	fmul.x		%fp0,%fp2		# SA7

	ror.l		&1,%d1
	and.l		&0x80000000,%d1

	fadd.d		COSB7(%pc),%fp1		# B7+SB8
	fadd.d		SINA6(%pc),%fp2		# A6+SA7

	eor.l		%d1,RPRIME(%a6)
	eor.l		%d1,SPRIME(%a6)

	fmul.x		%fp0,%fp1		# S(B7+SB8)

	or.l		&0x3F800000,%d1
	mov.l		%d1,POSNEG1(%a6)

	fmul.x		%fp0,%fp2		# S(A6+SA7)

	fadd.d		COSB6(%pc),%fp1		# B6+S(B7+SB8)
	fadd.d		SINA5(%pc),%fp2		# A5+S(A6+SA7)

	fmul.x		%fp0,%fp1		# S(B6+S(B7+SB8))
	fmul.x		%fp0,%fp2		# S(A5+S(A6+SA7))

	fadd.d		COSB5(%pc),%fp1		# B5+S(B6+S(B7+SB8))
	fadd.d		SINA4(%pc),%fp2		# A4+S(A5+S(A6+SA7))

	fmul.x		%fp0,%fp1		# S(B5+...)
	fmul.x		%fp0,%fp2		# S(A4+...)

	fadd.d		COSB4(%pc),%fp1		# B4+S(B5+...)
	fadd.d		SINA3(%pc),%fp2		# A3+S(A4+...)

	fmul.x		%fp0,%fp1		# S(B4+...)
	fmul.x		%fp0,%fp2		# S(A3+...)

	fadd.x		COSB3(%pc),%fp1		# B3+S(B4+...)
	fadd.x		SINA2(%pc),%fp2		# A2+S(A3+...)

	fmul.x		%fp0,%fp1		# S(B3+...)
	fmul.x		%fp0,%fp2		# S(A2+...)

	fadd.x		COSB2(%pc),%fp1		# B2+S(B3+...)
	fadd.x		SINA1(%pc),%fp2		# A1+S(A2+...)

	fmul.x		%fp0,%fp1		# S(B2+...)
	fmul.x		%fp2,%fp0		# s(a1+...)


	fadd.s		COSB1(%pc),%fp1		# B1+S(B2...)
	fmul.x		RPRIME(%a6),%fp0	# R'S(A1+...)
	fmul.x		SPRIME(%a6),%fp1	# S'(B1+S(B2+...))

	fmovm.x		(%sp)+,&0x20		# restore fp2

	fmov.l		%d0,%fpcr
	fadd.s		POSNEG1(%a6),%fp1	# COS(X)
	bsr		sto_cos			# store cosine result
	fadd.x		RPRIME(%a6),%fp0	# SIN(X)
	bra		t_inx2

################################################

SCBORS:
	cmp.l		%d1,&0x3FFF8000
	bgt.w		SREDUCEX

################################################

SCSM:
#	mov.w		&0x0000,XDCARE(%a6)
	fmov.s		&0x3F800000,%fp1

	fmov.l		%d0,%fpcr
	fsub.s		&0x00800000,%fp1
	bsr		sto_cos			# store cosine result
	fmov.l		%fpcr,%d0		# d0 must have fpcr,too
	mov.b		&FMOV_OP,%d1		# last inst is MOVE
	fmov.x		X(%a6),%fp0
	bra		t_catch

##############################################

	global		ssincosd
#--SIN AND COS OF X FOR DENORMALIZED X
ssincosd:
	mov.l		%d0,-(%sp)		# save d0
	fmov.s		&0x3F800000,%fp1
	bsr		sto_cos			# store cosine result
	mov.l		(%sp)+,%d0		# restore d0
	bra		t_extdnrm

############################################

#--WHEN REDUCEX IS USED, THE CODE WILL INEVITABLY BE SLOW.
#--THIS REDUCTION METHOD, HOWEVER, IS MUCH FASTER THAN USING
#--THE REMAINDER INSTRUCTION WHICH IS NOW IN SOFTWARE.
SREDUCEX:
	fmovm.x		&0x3c,-(%sp)		# save {fp2-fp5}
	mov.l		%d2,-(%sp)		# save d2
	fmov.s		&0x00000000,%fp1	# fp1 = 0

#--If compact form of abs(arg) in d0=$7ffeffff, argument is so large that
#--there is a danger of unwanted overflow in first LOOP iteration.  In this
#--case, reduce argument by one remainder step to make subsequent reduction
#--safe.
	cmp.l		%d1,&0x7ffeffff		# is arg dangerously large?
	bne.b		SLOOP			# no

# yes; create 2**16383*PI/2
	mov.w		&0x7ffe,FP_SCR0_EX(%a6)
	mov.l		&0xc90fdaa2,FP_SCR0_HI(%a6)
	clr.l		FP_SCR0_LO(%a6)

# create low half of 2**16383*PI/2 at FP_SCR1
	mov.w		&0x7fdc,FP_SCR1_EX(%a6)
	mov.l		&0x85a308d3,FP_SCR1_HI(%a6)
	clr.l		FP_SCR1_LO(%a6)

	ftest.x		%fp0			# test sign of argument
	fblt.w		sred_neg

	or.b		&0x80,FP_SCR0_EX(%a6)	# positive arg
	or.b		&0x80,FP_SCR1_EX(%a6)
sred_neg:
	fadd.x		FP_SCR0(%a6),%fp0	# high part of reduction is exact
	fmov.x		%fp0,%fp1		# save high result in fp1
	fadd.x		FP_SCR1(%a6),%fp0	# low part of reduction
	fsub.x		%fp0,%fp1		# determine low component of result
	fadd.x		FP_SCR1(%a6),%fp1	# fp0/fp1 are reduced argument.

#--ON ENTRY, FP0 IS X, ON RETURN, FP0 IS X REM PI/2, |X| <= PI/4.
#--integer quotient will be stored in N
#--Intermeditate remainder is 66-bit long; (R,r) in (FP0,FP1)
SLOOP:
	fmov.x		%fp0,INARG(%a6)		# +-2**K * F, 1 <= F < 2
	mov.w		INARG(%a6),%d1
	mov.l		%d1,%a1			# save a copy of D0
	and.l		&0x00007FFF,%d1
	sub.l		&0x00003FFF,%d1		# d0 = K
	cmp.l		%d1,&28
	ble.b		SLASTLOOP
SCONTLOOP:
	sub.l		&27,%d1			# d0 = L := K-27
	mov.b		&0,ENDFLAG(%a6)
	bra.b		SWORK
SLASTLOOP:
	clr.l		%d1			# d0 = L := 0
	mov.b		&1,ENDFLAG(%a6)

SWORK:
#--FIND THE REMAINDER OF (R,r) W.R.T.	2**L * (PI/2). L IS SO CHOSEN
#--THAT	INT( X * (2/PI) / 2**(L) ) < 2**29.

#--CREATE 2**(-L) * (2/PI), SIGN(INARG)*2**(63),
#--2**L * (PIby2_1), 2**L * (PIby2_2)

	mov.l		&0x00003FFE,%d2		# BIASED EXP OF 2/PI
	sub.l		%d1,%d2			# BIASED EXP OF 2**(-L)*(2/PI)

	mov.l		&0xA2F9836E,FP_SCR0_HI(%a6)
	mov.l		&0x4E44152A,FP_SCR0_LO(%a6)
	mov.w		%d2,FP_SCR0_EX(%a6)	# FP_SCR0 = 2**(-L)*(2/PI)

	fmov.x		%fp0,%fp2
	fmul.x		FP_SCR0(%a6),%fp2	# fp2 = X * 2**(-L)*(2/PI)

#--WE MUST NOW FIND INT(FP2). SINCE WE NEED THIS VALUE IN
#--FLOATING POINT FORMAT, THE TWO FMOVE'S	FMOVE.L FP <--> N
#--WILL BE TOO INEFFICIENT. THE WAY AROUND IT IS THAT
#--(SIGN(INARG)*2**63	+	FP2) - SIGN(INARG)*2**63 WILL GIVE
#--US THE DESIRED VALUE IN FLOATING POINT.
	mov.l		%a1,%d2
	swap		%d2
	and.l		&0x80000000,%d2
	or.l		&0x5F000000,%d2		# d2 = SIGN(INARG)*2**63 IN SGL
	mov.l		%d2,TWOTO63(%a6)
	fadd.s		TWOTO63(%a6),%fp2	# THE FRACTIONAL PART OF FP1 IS ROUNDED
	fsub.s		TWOTO63(%a6),%fp2	# fp2 = N
#	fint.x		%fp2

#--CREATING 2**(L)*Piby2_1 and 2**(L)*Piby2_2
	mov.l		%d1,%d2			# d2 = L

	add.l		&0x00003FFF,%d2		# BIASED EXP OF 2**L * (PI/2)
	mov.w		%d2,FP_SCR0_EX(%a6)
	mov.l		&0xC90FDAA2,FP_SCR0_HI(%a6)
	clr.l		FP_SCR0_LO(%a6)		# FP_SCR0 = 2**(L) * Piby2_1

	add.l		&0x00003FDD,%d1
	mov.w		%d1,FP_SCR1_EX(%a6)
	mov.l		&0x85A308D3,FP_SCR1_HI(%a6)
	clr.l		FP_SCR1_LO(%a6)		# FP_SCR1 = 2**(L) * Piby2_2

	mov.b		ENDFLAG(%a6),%d1

#--We are now ready to perform (R+r) - N*P1 - N*P2, P1 = 2**(L) * Piby2_1 and
#--P2 = 2**(L) * Piby2_2
	fmov.x		%fp2,%fp4		# fp4 = N
	fmul.x		FP_SCR0(%a6),%fp4	# fp4 = W = N*P1
	fmov.x		%fp2,%fp5		# fp5 = N
	fmul.x		FP_SCR1(%a6),%fp5	# fp5 = w = N*P2
	fmov.x		%fp4,%fp3		# fp3 = W = N*P1

#--we want P+p = W+w  but  |p| <= half ulp of P
#--Then, we need to compute  A := R-P   and  a := r-p
	fadd.x		%fp5,%fp3		# fp3 = P
	fsub.x		%fp3,%fp4		# fp4 = W-P

	fsub.x		%fp3,%fp0		# fp0 = A := R - P
	fadd.x		%fp5,%fp4		# fp4 = p = (W-P)+w

	fmov.x		%fp0,%fp3		# fp3 = A
	fsub.x		%fp4,%fp1		# fp1 = a := r - p

#--Now we need to normalize (A,a) to  "new (R,r)" where R+r = A+a but
#--|r| <= half ulp of R.
	fadd.x		%fp1,%fp0		# fp0 = R := A+a
#--No need to calculate r if this is the last loop
	cmp.b		%d1,&0
	bgt.w		SRESTORE

#--Need to calculate r
	fsub.x		%fp0,%fp3		# fp3 = A-R
	fadd.x		%fp3,%fp1		# fp1 = r := (A-R)+a
	bra.w		SLOOP

SRESTORE:
	fmov.l		%fp2,INT(%a6)
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		(%sp)+,&0x3c		# restore {fp2-fp5}

	mov.l		ADJN(%a6),%d1
	cmp.l		%d1,&4

	blt.w		SINCONT
	bra.w		SCCONT

#########################################################################
# stan():  computes the tangent of a normalized input			#
# stand(): computes the tangent of a denormalized input			#
#									#
# INPUT *************************************************************** #
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT ************************************************************** #
#	fp0 = tan(X)							#
#									#
# ACCURACY and MONOTONICITY ******************************************* #
#	The returned result is within 3 ulp in 64 significant bit, i.e. #
#	within 0.5001 ulp to 53 bits if the result is subsequently	#
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM *********************************************************** #
#									#
#	1. If |X| >= 15Pi or |X| < 2**(-40), go to 6.			#
#									#
#	2. Decompose X as X = N(Pi/2) + r where |r| <= Pi/4. Let	#
#		k = N mod 2, so in particular, k = 0 or 1.		#
#									#
#	3. If k is odd, go to 5.					#
#									#
#	4. (k is even) Tan(X) = tan(r) and tan(r) is approximated by a	#
#		rational function U/V where				#
#		U = r + r*s*(P1 + s*(P2 + s*P3)), and			#
#		V = 1 + s*(Q1 + s*(Q2 + s*(Q3 + s*Q4))),  s = r*r.	#
#		Exit.							#
#									#
#	4. (k is odd) Tan(X) = -cot(r). Since tan(r) is approximated by #
#		a rational function U/V where				#
#		U = r + r*s*(P1 + s*(P2 + s*P3)), and			#
#		V = 1 + s*(Q1 + s*(Q2 + s*(Q3 + s*Q4))), s = r*r,	#
#		-Cot(r) = -V/U. Exit.					#
#									#
#	6. If |X| > 1, go to 8.						#
#									#
#	7. (|X|<2**(-40)) Tan(X) = X. Exit.				#
#									#
#	8. Overwrite X by X := X rem 2Pi. Now that |X| <= Pi, go back	#
#		to 2.							#
#									#
#########################################################################

TANQ4:
	long		0x3EA0B759,0xF50F8688
TANP3:
	long		0xBEF2BAA5,0xA8924F04

TANQ3:
	long		0xBF346F59,0xB39BA65F,0x00000000,0x00000000

TANP2:
	long		0x3FF60000,0xE073D3FC,0x199C4A00,0x00000000

TANQ2:
	long		0x3FF90000,0xD23CD684,0x15D95FA1,0x00000000

TANP1:
	long		0xBFFC0000,0x8895A6C5,0xFB423BCA,0x00000000

TANQ1:
	long		0xBFFD0000,0xEEF57E0D,0xA84BC8CE,0x00000000

INVTWOPI:
	long		0x3FFC0000,0xA2F9836E,0x4E44152A,0x00000000

TWOPI1:
	long		0x40010000,0xC90FDAA2,0x00000000,0x00000000
TWOPI2:
	long		0x3FDF0000,0x85A308D4,0x00000000,0x00000000

#--N*PI/2, -32 <= N <= 32, IN A LEADING TERM IN EXT. AND TRAILING
#--TERM IN SGL. NOTE THAT PI IS 64-BIT LONG, THUS N*PI/2 IS AT
#--MOST 69 BITS LONG.
#	global		PITBL
PITBL:
	long		0xC0040000,0xC90FDAA2,0x2168C235,0x21800000
	long		0xC0040000,0xC2C75BCD,0x105D7C23,0xA0D00000
	long		0xC0040000,0xBC7EDCF7,0xFF523611,0xA1E80000
	long		0xC0040000,0xB6365E22,0xEE46F000,0x21480000
	long		0xC0040000,0xAFEDDF4D,0xDD3BA9EE,0xA1200000
	long		0xC0040000,0xA9A56078,0xCC3063DD,0x21FC0000
	long		0xC0040000,0xA35CE1A3,0xBB251DCB,0x21100000
	long		0xC0040000,0x9D1462CE,0xAA19D7B9,0xA1580000
	long		0xC0040000,0x96CBE3F9,0x990E91A8,0x21E00000
	long		0xC0040000,0x90836524,0x88034B96,0x20B00000
	long		0xC0040000,0x8A3AE64F,0x76F80584,0xA1880000
	long		0xC0040000,0x83F2677A,0x65ECBF73,0x21C40000
	long		0xC0030000,0xFB53D14A,0xA9C2F2C2,0x20000000
	long		0xC0030000,0xEEC2D3A0,0x87AC669F,0x21380000
	long		0xC0030000,0xE231D5F6,0x6595DA7B,0xA1300000
	long		0xC0030000,0xD5A0D84C,0x437F4E58,0x9FC00000
	long		0xC0030000,0xC90FDAA2,0x2168C235,0x21000000
	long		0xC0030000,0xBC7EDCF7,0xFF523611,0xA1680000
	long		0xC0030000,0xAFEDDF4D,0xDD3BA9EE,0xA0A00000
	long		0xC0030000,0xA35CE1A3,0xBB251DCB,0x20900000
	long		0xC0030000,0x96CBE3F9,0x990E91A8,0x21600000
	long		0xC0030000,0x8A3AE64F,0x76F80584,0xA1080000
	long		0xC0020000,0xFB53D14A,0xA9C2F2C2,0x1F800000
	long		0xC0020000,0xE231D5F6,0x6595DA7B,0xA0B00000
	long		0xC0020000,0xC90FDAA2,0x2168C235,0x20800000
	long		0xC0020000,0xAFEDDF4D,0xDD3BA9EE,0xA0200000
	long		0xC0020000,0x96CBE3F9,0x990E91A8,0x20E00000
	long		0xC0010000,0xFB53D14A,0xA9C2F2C2,0x1F000000
	long		0xC0010000,0xC90FDAA2,0x2168C235,0x20000000
	long		0xC0010000,0x96CBE3F9,0x990E91A8,0x20600000
	long		0xC0000000,0xC90FDAA2,0x2168C235,0x1F800000
	long		0xBFFF0000,0xC90FDAA2,0x2168C235,0x1F000000
	long		0x00000000,0x00000000,0x00000000,0x00000000
	long		0x3FFF0000,0xC90FDAA2,0x2168C235,0x9F000000
	long		0x40000000,0xC90FDAA2,0x2168C235,0x9F800000
	long		0x40010000,0x96CBE3F9,0x990E91A8,0xA0600000
	long		0x40010000,0xC90FDAA2,0x2168C235,0xA0000000
	long		0x40010000,0xFB53D14A,0xA9C2F2C2,0x9F000000
	long		0x40020000,0x96CBE3F9,0x990E91A8,0xA0E00000
	long		0x40020000,0xAFEDDF4D,0xDD3BA9EE,0x20200000
	long		0x40020000,0xC90FDAA2,0x2168C235,0xA0800000
	long		0x40020000,0xE231D5F6,0x6595DA7B,0x20B00000
	long		0x40020000,0xFB53D14A,0xA9C2F2C2,0x9F800000
	long		0x40030000,0x8A3AE64F,0x76F80584,0x21080000
	long		0x40030000,0x96CBE3F9,0x990E91A8,0xA1600000
	long		0x40030000,0xA35CE1A3,0xBB251DCB,0xA0900000
	long		0x40030000,0xAFEDDF4D,0xDD3BA9EE,0x20A00000
	long		0x40030000,0xBC7EDCF7,0xFF523611,0x21680000
	long		0x40030000,0xC90FDAA2,0x2168C235,0xA1000000
	long		0x40030000,0xD5A0D84C,0x437F4E58,0x1FC00000
	long		0x40030000,0xE231D5F6,0x6595DA7B,0x21300000
	long		0x40030000,0xEEC2D3A0,0x87AC669F,0xA1380000
	long		0x40030000,0xFB53D14A,0xA9C2F2C2,0xA0000000
	long		0x40040000,0x83F2677A,0x65ECBF73,0xA1C40000
	long		0x40040000,0x8A3AE64F,0x76F80584,0x21880000
	long		0x40040000,0x90836524,0x88034B96,0xA0B00000
	long		0x40040000,0x96CBE3F9,0x990E91A8,0xA1E00000
	long		0x40040000,0x9D1462CE,0xAA19D7B9,0x21580000
	long		0x40040000,0xA35CE1A3,0xBB251DCB,0xA1100000
	long		0x40040000,0xA9A56078,0xCC3063DD,0xA1FC0000
	long		0x40040000,0xAFEDDF4D,0xDD3BA9EE,0x21200000
	long		0x40040000,0xB6365E22,0xEE46F000,0xA1480000
	long		0x40040000,0xBC7EDCF7,0xFF523611,0x21E80000
	long		0x40040000,0xC2C75BCD,0x105D7C23,0x20D00000
	long		0x40040000,0xC90FDAA2,0x2168C235,0xA1800000

	set		INARG,FP_SCR0

	set		TWOTO63,L_SCR1
	set		INT,L_SCR1
	set		ENDFLAG,L_SCR2

	global		stan
stan:
	fmov.x		(%a0),%fp0		# LOAD INPUT

	mov.l		(%a0),%d1
	mov.w		4(%a0),%d1
	and.l		&0x7FFFFFFF,%d1

	cmp.l		%d1,&0x3FD78000		# |X| >= 2**(-40)?
	bge.b		TANOK1
	bra.w		TANSM
TANOK1:
	cmp.l		%d1,&0x4004BC7E		# |X| < 15 PI?
	blt.b		TANMAIN
	bra.w		REDUCEX

TANMAIN:
#--THIS IS THE USUAL CASE, |X| <= 15 PI.
#--THE ARGUMENT REDUCTION IS DONE BY TABLE LOOK UP.
	fmov.x		%fp0,%fp1
	fmul.d		TWOBYPI(%pc),%fp1	# X*2/PI

	lea.l		PITBL+0x200(%pc),%a1	# TABLE OF N*PI/2, N = -32,...,32

	fmov.l		%fp1,%d1		# CONVERT TO INTEGER

	asl.l		&4,%d1
	add.l		%d1,%a1			# ADDRESS N*PIBY2 IN Y1, Y2

	fsub.x		(%a1)+,%fp0		# X-Y1

	fsub.s		(%a1),%fp0		# FP0 IS R = (X-Y1)-Y2

	ror.l		&5,%d1
	and.l		&0x80000000,%d1		# D0 WAS ODD IFF D0 < 0

TANCONT:
	fmovm.x		&0x0c,-(%sp)		# save fp2,fp3

	cmp.l		%d1,&0
	blt.w		NODD

	fmov.x		%fp0,%fp1
	fmul.x		%fp1,%fp1		# S = R*R

	fmov.d		TANQ4(%pc),%fp3
	fmov.d		TANP3(%pc),%fp2

	fmul.x		%fp1,%fp3		# SQ4
	fmul.x		%fp1,%fp2		# SP3

	fadd.d		TANQ3(%pc),%fp3		# Q3+SQ4
	fadd.x		TANP2(%pc),%fp2		# P2+SP3

	fmul.x		%fp1,%fp3		# S(Q3+SQ4)
	fmul.x		%fp1,%fp2		# S(P2+SP3)

	fadd.x		TANQ2(%pc),%fp3		# Q2+S(Q3+SQ4)
	fadd.x		TANP1(%pc),%fp2		# P1+S(P2+SP3)

	fmul.x		%fp1,%fp3		# S(Q2+S(Q3+SQ4))
	fmul.x		%fp1,%fp2		# S(P1+S(P2+SP3))

	fadd.x		TANQ1(%pc),%fp3		# Q1+S(Q2+S(Q3+SQ4))
	fmul.x		%fp0,%fp2		# RS(P1+S(P2+SP3))

	fmul.x		%fp3,%fp1		# S(Q1+S(Q2+S(Q3+SQ4)))

	fadd.x		%fp2,%fp0		# R+RS(P1+S(P2+SP3))

	fadd.s		&0x3F800000,%fp1	# 1+S(Q1+...)

	fmovm.x		(%sp)+,&0x30		# restore fp2,fp3

	fmov.l		%d0,%fpcr		# restore users round mode,prec
	fdiv.x		%fp1,%fp0		# last inst - possible exception set
	bra		t_inx2

NODD:
	fmov.x		%fp0,%fp1
	fmul.x		%fp0,%fp0		# S = R*R

	fmov.d		TANQ4(%pc),%fp3
	fmov.d		TANP3(%pc),%fp2

	fmul.x		%fp0,%fp3		# SQ4
	fmul.x		%fp0,%fp2		# SP3

	fadd.d		TANQ3(%pc),%fp3		# Q3+SQ4
	fadd.x		TANP2(%pc),%fp2		# P2+SP3

	fmul.x		%fp0,%fp3		# S(Q3+SQ4)
	fmul.x		%fp0,%fp2		# S(P2+SP3)

	fadd.x		TANQ2(%pc),%fp3		# Q2+S(Q3+SQ4)
	fadd.x		TANP1(%pc),%fp2		# P1+S(P2+SP3)

	fmul.x		%fp0,%fp3		# S(Q2+S(Q3+SQ4))
	fmul.x		%fp0,%fp2		# S(P1+S(P2+SP3))

	fadd.x		TANQ1(%pc),%fp3		# Q1+S(Q2+S(Q3+SQ4))
	fmul.x		%fp1,%fp2		# RS(P1+S(P2+SP3))

	fmul.x		%fp3,%fp0		# S(Q1+S(Q2+S(Q3+SQ4)))

	fadd.x		%fp2,%fp1		# R+RS(P1+S(P2+SP3))
	fadd.s		&0x3F800000,%fp0	# 1+S(Q1+...)

	fmovm.x		(%sp)+,&0x30		# restore fp2,fp3

	fmov.x		%fp1,-(%sp)
	eor.l		&0x80000000,(%sp)

	fmov.l		%d0,%fpcr		# restore users round mode,prec
	fdiv.x		(%sp)+,%fp0		# last inst - possible exception set
	bra		t_inx2

TANBORS:
#--IF |X| > 15PI, WE USE THE GENERAL ARGUMENT REDUCTION.
#--IF |X| < 2**(-40), RETURN X OR 1.
	cmp.l		%d1,&0x3FFF8000
	bgt.b		REDUCEX

TANSM:
	fmov.x		%fp0,-(%sp)
	fmov.l		%d0,%fpcr		# restore users round mode,prec
	mov.b		&FMOV_OP,%d1		# last inst is MOVE
	fmov.x		(%sp)+,%fp0		# last inst - posibble exception set
	bra		t_catch

	global		stand
#--TAN(X) = X FOR DENORMALIZED X
stand:
	bra		t_extdnrm

#--WHEN REDUCEX IS USED, THE CODE WILL INEVITABLY BE SLOW.
#--THIS REDUCTION METHOD, HOWEVER, IS MUCH FASTER THAN USING
#--THE REMAINDER INSTRUCTION WHICH IS NOW IN SOFTWARE.
REDUCEX:
	fmovm.x		&0x3c,-(%sp)		# save {fp2-fp5}
	mov.l		%d2,-(%sp)		# save d2
	fmov.s		&0x00000000,%fp1	# fp1 = 0

#--If compact form of abs(arg) in d0=$7ffeffff, argument is so large that
#--there is a danger of unwanted overflow in first LOOP iteration.  In this
#--case, reduce argument by one remainder step to make subsequent reduction
#--safe.
	cmp.l		%d1,&0x7ffeffff		# is arg dangerously large?
	bne.b		LOOP			# no

# yes; create 2**16383*PI/2
	mov.w		&0x7ffe,FP_SCR0_EX(%a6)
	mov.l		&0xc90fdaa2,FP_SCR0_HI(%a6)
	clr.l		FP_SCR0_LO(%a6)

# create low half of 2**16383*PI/2 at FP_SCR1
	mov.w		&0x7fdc,FP_SCR1_EX(%a6)
	mov.l		&0x85a308d3,FP_SCR1_HI(%a6)
	clr.l		FP_SCR1_LO(%a6)

	ftest.x		%fp0			# test sign of argument
	fblt.w		red_neg

	or.b		&0x80,FP_SCR0_EX(%a6)	# positive arg
	or.b		&0x80,FP_SCR1_EX(%a6)
red_neg:
	fadd.x		FP_SCR0(%a6),%fp0	# high part of reduction is exact
	fmov.x		%fp0,%fp1		# save high result in fp1
	fadd.x		FP_SCR1(%a6),%fp0	# low part of reduction
	fsub.x		%fp0,%fp1		# determine low component of result
	fadd.x		FP_SCR1(%a6),%fp1	# fp0/fp1 are reduced argument.

#--ON ENTRY, FP0 IS X, ON RETURN, FP0 IS X REM PI/2, |X| <= PI/4.
#--integer quotient will be stored in N
#--Intermeditate remainder is 66-bit long; (R,r) in (FP0,FP1)
LOOP:
	fmov.x		%fp0,INARG(%a6)		# +-2**K * F, 1 <= F < 2
	mov.w		INARG(%a6),%d1
	mov.l		%d1,%a1			# save a copy of D0
	and.l		&0x00007FFF,%d1
	sub.l		&0x00003FFF,%d1		# d0 = K
	cmp.l		%d1,&28
	ble.b		LASTLOOP
CONTLOOP:
	sub.l		&27,%d1			# d0 = L := K-27
	mov.b		&0,ENDFLAG(%a6)
	bra.b		WORK
LASTLOOP:
	clr.l		%d1			# d0 = L := 0
	mov.b		&1,ENDFLAG(%a6)

WORK:
#--FIND THE REMAINDER OF (R,r) W.R.T.	2**L * (PI/2). L IS SO CHOSEN
#--THAT	INT( X * (2/PI) / 2**(L) ) < 2**29.

#--CREATE 2**(-L) * (2/PI), SIGN(INARG)*2**(63),
#--2**L * (PIby2_1), 2**L * (PIby2_2)

	mov.l		&0x00003FFE,%d2		# BIASED EXP OF 2/PI
	sub.l		%d1,%d2			# BIASED EXP OF 2**(-L)*(2/PI)

	mov.l		&0xA2F9836E,FP_SCR0_HI(%a6)
	mov.l		&0x4E44152A,FP_SCR0_LO(%a6)
	mov.w		%d2,FP_SCR0_EX(%a6)	# FP_SCR0 = 2**(-L)*(2/PI)

	fmov.x		%fp0,%fp2
	fmul.x		FP_SCR0(%a6),%fp2	# fp2 = X * 2**(-L)*(2/PI)

#--WE MUST NOW FIND INT(FP2). SINCE WE NEED THIS VALUE IN
#--FLOATING POINT FORMAT, THE TWO FMOVE'S	FMOVE.L FP <--> N
#--WILL BE TOO INEFFICIENT. THE WAY AROUND IT IS THAT
#--(SIGN(INARG)*2**63	+	FP2) - SIGN(INARG)*2**63 WILL GIVE
#--US THE DESIRED VALUE IN FLOATING POINT.
	mov.l		%a1,%d2
	swap		%d2
	and.l		&0x80000000,%d2
	or.l		&0x5F000000,%d2		# d2 = SIGN(INARG)*2**63 IN SGL
	mov.l		%d2,TWOTO63(%a6)
	fadd.s		TWOTO63(%a6),%fp2	# THE FRACTIONAL PART OF FP1 IS ROUNDED
	fsub.s		TWOTO63(%a6),%fp2	# fp2 = N
#	fintrz.x	%fp2,%fp2

#--CREATING 2**(L)*Piby2_1 and 2**(L)*Piby2_2
	mov.l		%d1,%d2			# d2 = L

	add.l		&0x00003FFF,%d2		# BIASED EXP OF 2**L * (PI/2)
	mov.w		%d2,FP_SCR0_EX(%a6)
	mov.l		&0xC90FDAA2,FP_SCR0_HI(%a6)
	clr.l		FP_SCR0_LO(%a6)		# FP_SCR0 = 2**(L) * Piby2_1

	add.l		&0x00003FDD,%d1
	mov.w		%d1,FP_SCR1_EX(%a6)
	mov.l		&0x85A308D3,FP_SCR1_HI(%a6)
	clr.l		FP_SCR1_LO(%a6)		# FP_SCR1 = 2**(L) * Piby2_2

	mov.b		ENDFLAG(%a6),%d1

#--We are now ready to perform (R+r) - N*P1 - N*P2, P1 = 2**(L) * Piby2_1 and
#--P2 = 2**(L) * Piby2_2
	fmov.x		%fp2,%fp4		# fp4 = N
	fmul.x		FP_SCR0(%a6),%fp4	# fp4 = W = N*P1
	fmov.x		%fp2,%fp5		# fp5 = N
	fmul.x		FP_SCR1(%a6),%fp5	# fp5 = w = N*P2
	fmov.x		%fp4,%fp3		# fp3 = W = N*P1

#--we want P+p = W+w  but  |p| <= half ulp of P
#--Then, we need to compute  A := R-P   and  a := r-p
	fadd.x		%fp5,%fp3		# fp3 = P
	fsub.x		%fp3,%fp4		# fp4 = W-P

	fsub.x		%fp3,%fp0		# fp0 = A := R - P
	fadd.x		%fp5,%fp4		# fp4 = p = (W-P)+w

	fmov.x		%fp0,%fp3		# fp3 = A
	fsub.x		%fp4,%fp1		# fp1 = a := r - p

#--Now we need to normalize (A,a) to  "new (R,r)" where R+r = A+a but
#--|r| <= half ulp of R.
	fadd.x		%fp1,%fp0		# fp0 = R := A+a
#--No need to calculate r if this is the last loop
	cmp.b		%d1,&0
	bgt.w		RESTORE

#--Need to calculate r
	fsub.x		%fp0,%fp3		# fp3 = A-R
	fadd.x		%fp3,%fp1		# fp1 = r := (A-R)+a
	bra.w		LOOP

RESTORE:
	fmov.l		%fp2,INT(%a6)
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		(%sp)+,&0x3c		# restore {fp2-fp5}

	mov.l		INT(%a6),%d1
	ror.l		&1,%d1

	bra.w		TANCONT

#########################################################################
# satan():  computes the arctangent of a normalized number		#
# satand(): computes the arctangent of a denormalized number		#
#									#
# INPUT	*************************************************************** #
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT ************************************************************** #
#	fp0 = arctan(X)							#
#									#
# ACCURACY and MONOTONICITY ******************************************* #
#	The returned result is within 2 ulps in	64 significant bit,	#
#	i.e. within 0.5001 ulp to 53 bits if the result is subsequently	#
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM *********************************************************** #
#	Step 1. If |X| >= 16 or |X| < 1/16, go to Step 5.		#
#									#
#	Step 2. Let X = sgn * 2**k * 1.xxxxxxxx...x.			#
#		Note that k = -4, -3,..., or 3.				#
#		Define F = sgn * 2**k * 1.xxxx1, i.e. the first 5	#
#		significant bits of X with a bit-1 attached at the 6-th	#
#		bit position. Define u to be u = (X-F) / (1 + X*F).	#
#									#
#	Step 3. Approximate arctan(u) by a polynomial poly.		#
#									#
#	Step 4. Return arctan(F) + poly, arctan(F) is fetched from a	#
#		table of values calculated beforehand. Exit.		#
#									#
#	Step 5. If |X| >= 16, go to Step 7.				#
#									#
#	Step 6. Approximate arctan(X) by an odd polynomial in X. Exit.	#
#									#
#	Step 7. Define X' = -1/X. Approximate arctan(X') by an odd	#
#		polynomial in X'.					#
#		Arctan(X) = sign(X)*Pi/2 + arctan(X'). Exit.		#
#									#
#########################################################################

ATANA3:	long		0xBFF6687E,0x314987D8
ATANA2:	long		0x4002AC69,0x34A26DB3
ATANA1:	long		0xBFC2476F,0x4E1DA28E

ATANB6:	long		0x3FB34444,0x7F876989
ATANB5:	long		0xBFB744EE,0x7FAF45DB
ATANB4:	long		0x3FBC71C6,0x46940220
ATANB3:	long		0xBFC24924,0x921872F9
ATANB2:	long		0x3FC99999,0x99998FA9
ATANB1:	long		0xBFD55555,0x55555555

ATANC5:	long		0xBFB70BF3,0x98539E6A
ATANC4:	long		0x3FBC7187,0x962D1D7D
ATANC3:	long		0xBFC24924,0x827107B8
ATANC2:	long		0x3FC99999,0x9996263E
ATANC1:	long		0xBFD55555,0x55555536

PPIBY2:	long		0x3FFF0000,0xC90FDAA2,0x2168C235,0x00000000
NPIBY2:	long		0xBFFF0000,0xC90FDAA2,0x2168C235,0x00000000

PTINY:	long		0x00010000,0x80000000,0x00000000,0x00000000
NTINY:	long		0x80010000,0x80000000,0x00000000,0x00000000

ATANTBL:
	long		0x3FFB0000,0x83D152C5,0x060B7A51,0x00000000
	long		0x3FFB0000,0x8BC85445,0x65498B8B,0x00000000
	long		0x3FFB0000,0x93BE4060,0x17626B0D,0x00000000
	long		0x3FFB0000,0x9BB3078D,0x35AEC202,0x00000000
	long		0x3FFB0000,0xA3A69A52,0x5DDCE7DE,0x00000000
	long		0x3FFB0000,0xAB98E943,0x62765619,0x00000000
	long		0x3FFB0000,0xB389E502,0xF9C59862,0x00000000
	long		0x3FFB0000,0xBB797E43,0x6B09E6FB,0x00000000
	long		0x3FFB0000,0xC367A5C7,0x39E5F446,0x00000000
	long		0x3FFB0000,0xCB544C61,0xCFF7D5C6,0x00000000
	long		0x3FFB0000,0xD33F62F8,0x2488533E,0x00000000
	long		0x3FFB0000,0xDB28DA81,0x62404C77,0x00000000
	long		0x3FFB0000,0xE310A407,0x8AD34F18,0x00000000
	long		0x3FFB0000,0xEAF6B0A8,0x188EE1EB,0x00000000
	long		0x3FFB0000,0xF2DAF194,0x9DBE79D5,0x00000000
	long		0x3FFB0000,0xFABD5813,0x61D47E3E,0x00000000
	long		0x3FFC0000,0x8346AC21,0x0959ECC4,0x00000000
	long		0x3FFC0000,0x8B232A08,0x304282D8,0x00000000
	long		0x3FFC0000,0x92FB70B8,0xD29AE2F9,0x00000000
	long		0x3FFC0000,0x9ACF476F,0x5CCD1CB4,0x00000000
	long		0x3FFC0000,0xA29E7630,0x4954F23F,0x00000000
	long		0x3FFC0000,0xAA68C5D0,0x8AB85230,0x00000000
	long		0x3FFC0000,0xB22DFFFD,0x9D539F83,0x00000000
	long		0x3FFC0000,0xB9EDEF45,0x3E900EA5,0x00000000
	long		0x3FFC0000,0xC1A85F1C,0xC75E3EA5,0x00000000
	long		0x3FFC0000,0xC95D1BE8,0x28138DE6,0x00000000
	long		0x3FFC0000,0xD10BF300,0x840D2DE4,0x00000000
	long		0x3FFC0000,0xD8B4B2BA,0x6BC05E7A,0x00000000
	long		0x3FFC0000,0xE0572A6B,0xB42335F6,0x00000000
	long		0x3FFC0000,0xE7F32A70,0xEA9CAA8F,0x00000000
	long		0x3FFC0000,0xEF888432,0x64ECEFAA,0x00000000
	long		0x3FFC0000,0xF7170A28,0xECC06666,0x00000000
	long		0x3FFD0000,0x812FD288,0x332DAD32,0x00000000
	long		0x3FFD0000,0x88A8D1B1,0x218E4D64,0x00000000
	long		0x3FFD0000,0x9012AB3F,0x23E4AEE8,0x00000000
	long		0x3FFD0000,0x976CC3D4,0x11E7F1B9,0x00000000
	long		0x3FFD0000,0x9EB68949,0x3889A227,0x00000000
	long		0x3FFD0000,0xA5EF72C3,0x4487361B,0x00000000
	long		0x3FFD0000,0xAD1700BA,0xF07A7227,0x00000000
	long		0x3FFD0000,0xB42CBCFA,0xFD37EFB7,0x00000000
	long		0x3FFD0000,0xBB303A94,0x0BA80F89,0x00000000
	long		0x3FFD0000,0xC22115C6,0xFCAEBBAF,0x00000000
	long		0x3FFD0000,0xC8FEF3E6,0x86331221,0x00000000
	long		0x3FFD0000,0xCFC98330,0xB4000C70,0x00000000
	long		0x3FFD0000,0xD6807AA1,0x102C5BF9,0x00000000
	long		0x3FFD0000,0xDD2399BC,0x31252AA3,0x00000000
	long		0x3FFD0000,0xE3B2A855,0x6B8FC517,0x00000000
	long		0x3FFD0000,0xEA2D764F,0x64315989,0x00000000
	long		0x3FFD0000,0xF3BF5BF8,0xBAD1A21D,0x00000000
	long		0x3FFE0000,0x801CE39E,0x0D205C9A,0x00000000
	long		0x3FFE0000,0x8630A2DA,0xDA1ED066,0x00000000
	long		0x3FFE0000,0x8C1AD445,0xF3E09B8C,0x00000000
	long		0x3FFE0000,0x91DB8F16,0x64F350E2,0x00000000
	long		0x3FFE0000,0x97731420,0x365E538C,0x00000000
	long		0x3FFE0000,0x9CE1C8E6,0xA0B8CDBA,0x00000000
	long		0x3FFE0000,0xA22832DB,0xCADAAE09,0x00000000
	long		0x3FFE0000,0xA746F2DD,0xB7602294,0x00000000
	long		0x3FFE0000,0xAC3EC0FB,0x997DD6A2,0x00000000
	long		0x3FFE0000,0xB110688A,0xEBDC6F6A,0x00000000
	long		0x3FFE0000,0xB5BCC490,0x59ECC4B0,0x00000000
	long		0x3FFE0000,0xBA44BC7D,0xD470782F,0x00000000
	long		0x3FFE0000,0xBEA94144,0xFD049AAC,0x00000000
	long		0x3FFE0000,0xC2EB4ABB,0x661628B6,0x00000000
	long		0x3FFE0000,0xC70BD54C,0xE602EE14,0x00000000
	long		0x3FFE0000,0xCD000549,0xADEC7159,0x00000000
	long		0x3FFE0000,0xD48457D2,0xD8EA4EA3,0x00000000
	long		0x3FFE0000,0xDB948DA7,0x12DECE3B,0x00000000
	long		0x3FFE0000,0xE23855F9,0x69E8096A,0x00000000
	long		0x3FFE0000,0xE8771129,0xC4353259,0x00000000
	long		0x3FFE0000,0xEE57C16E,0x0D379C0D,0x00000000
	long		0x3FFE0000,0xF3E10211,0xA87C3779,0x00000000
	long		0x3FFE0000,0xF919039D,0x758B8D41,0x00000000
	long		0x3FFE0000,0xFE058B8F,0x64935FB3,0x00000000
	long		0x3FFF0000,0x8155FB49,0x7B685D04,0x00000000
	long		0x3FFF0000,0x83889E35,0x49D108E1,0x00000000
	long		0x3FFF0000,0x859CFA76,0x511D724B,0x00000000
	long		0x3FFF0000,0x87952ECF,0xFF8131E7,0x00000000
	long		0x3FFF0000,0x89732FD1,0x9557641B,0x00000000
	long		0x3FFF0000,0x8B38CAD1,0x01932A35,0x00000000
	long		0x3FFF0000,0x8CE7A8D8,0x301EE6B5,0x00000000
	long		0x3FFF0000,0x8F46A39E,0x2EAE5281,0x00000000
	long		0x3FFF0000,0x922DA7D7,0x91888487,0x00000000
	long		0x3FFF0000,0x94D19FCB,0xDEDF5241,0x00000000
	long		0x3FFF0000,0x973AB944,0x19D2A08B,0x00000000
	long		0x3FFF0000,0x996FF00E,0x08E10B96,0x00000000
	long		0x3FFF0000,0x9B773F95,0x12321DA7,0x00000000
	long		0x3FFF0000,0x9D55CC32,0x0F935624,0x00000000
	long		0x3FFF0000,0x9F100575,0x006CC571,0x00000000
	long		0x3FFF0000,0xA0A9C290,0xD97CC06C,0x00000000
	long		0x3FFF0000,0xA22659EB,0xEBC0630A,0x00000000
	long		0x3FFF0000,0xA388B4AF,0xF6EF0EC9,0x00000000
	long		0x3FFF0000,0xA4D35F10,0x61D292C4,0x00000000
	long		0x3FFF0000,0xA60895DC,0xFBE3187E,0x00000000
	long		0x3FFF0000,0xA72A51DC,0x7367BEAC,0x00000000
	long		0x3FFF0000,0xA83A5153,0x0956168F,0x00000000
	long		0x3FFF0000,0xA93A2007,0x7539546E,0x00000000
	long		0x3FFF0000,0xAA9E7245,0x023B2605,0x00000000
	long		0x3FFF0000,0xAC4C84BA,0x6FE4D58F,0x00000000
	long		0x3FFF0000,0xADCE4A4A,0x606B9712,0x00000000
	long		0x3FFF0000,0xAF2A2DCD,0x8D263C9C,0x00000000
	long		0x3FFF0000,0xB0656F81,0xF22265C7,0x00000000
	long		0x3FFF0000,0xB1846515,0x0F71496A,0x00000000
	long		0x3FFF0000,0xB28AAA15,0x6F9ADA35,0x00000000
	long		0x3FFF0000,0xB37B44FF,0x3766B895,0x00000000
	long		0x3FFF0000,0xB458C3DC,0xE9630433,0x00000000
	long		0x3FFF0000,0xB525529D,0x562246BD,0x00000000
	long		0x3FFF0000,0xB5E2CCA9,0x5F9D88CC,0x00000000
	long		0x3FFF0000,0xB692CADA,0x7ACA1ADA,0x00000000
	long		0x3FFF0000,0xB736AEA7,0xA6925838,0x00000000
	long		0x3FFF0000,0xB7CFAB28,0x7E9F7B36,0x00000000
	long		0x3FFF0000,0xB85ECC66,0xCB219835,0x00000000
	long		0x3FFF0000,0xB8E4FD5A,0x20A593DA,0x00000000
	long		0x3FFF0000,0xB99F41F6,0x4AFF9BB5,0x00000000
	long		0x3FFF0000,0xBA7F1E17,0x842BBE7B,0x00000000
	long		0x3FFF0000,0xBB471285,0x7637E17D,0x00000000
	long		0x3FFF0000,0xBBFABE8A,0x4788DF6F,0x00000000
	long		0x3FFF0000,0xBC9D0FAD,0x2B689D79,0x00000000
	long		0x3FFF0000,0xBD306A39,0x471ECD86,0x00000000
	long		0x3FFF0000,0xBDB6C731,0x856AF18A,0x00000000
	long		0x3FFF0000,0xBE31CAC5,0x02E80D70,0x00000000
	long		0x3FFF0000,0xBEA2D55C,0xE33194E2,0x00000000
	long		0x3FFF0000,0xBF0B10B7,0xC03128F0,0x00000000
	long		0x3FFF0000,0xBF6B7A18,0xDACB778D,0x00000000
	long		0x3FFF0000,0xBFC4EA46,0x63FA18F6,0x00000000
	long		0x3FFF0000,0xC0181BDE,0x8B89A454,0x00000000
	long		0x3FFF0000,0xC065B066,0xCFBF6439,0x00000000
	long		0x3FFF0000,0xC0AE345F,0x56340AE6,0x00000000
	long		0x3FFF0000,0xC0F22291,0x9CB9E6A7,0x00000000

	set		X,FP_SCR0
	set		XDCARE,X+2
	set		XFRAC,X+4
	set		XFRACLO,X+8

	set		ATANF,FP_SCR1
	set		ATANFHI,ATANF+4
	set		ATANFLO,ATANF+8

	global		satan
#--ENTRY POINT FOR ATAN(X), HERE X IS FINITE, NON-ZERO, AND NOT NAN'S
satan:
	fmov.x		(%a0),%fp0		# LOAD INPUT

	mov.l		(%a0),%d1
	mov.w		4(%a0),%d1
	fmov.x		%fp0,X(%a6)
	and.l		&0x7FFFFFFF,%d1

	cmp.l		%d1,&0x3FFB8000		# |X| >= 1/16?
	bge.b		ATANOK1
	bra.w		ATANSM

ATANOK1:
	cmp.l		%d1,&0x4002FFFF		# |X| < 16 ?
	ble.b		ATANMAIN
	bra.w		ATANBIG

#--THE MOST LIKELY CASE, |X| IN [1/16, 16). WE USE TABLE TECHNIQUE
#--THE IDEA IS ATAN(X) = ATAN(F) + ATAN( [X-F] / [1+XF] ).
#--SO IF F IS CHOSEN TO BE CLOSE TO X AND ATAN(F) IS STORED IN
#--A TABLE, ALL WE NEED IS TO APPROXIMATE ATAN(U) WHERE
#--U = (X-F)/(1+XF) IS SMALL (REMEMBER F IS CLOSE TO X). IT IS
#--TRUE THAT A DIVIDE IS NOW NEEDED, BUT THE APPROXIMATION FOR
#--ATAN(U) IS A VERY SHORT POLYNOMIAL AND THE INDEXING TO
#--FETCH F AND SAVING OF REGISTERS CAN BE ALL HIDED UNDER THE
#--DIVIDE. IN THE END THIS METHOD IS MUCH FASTER THAN A TRADITIONAL
#--ONE. NOTE ALSO THAT THE TRADITIONAL SCHEME THAT APPROXIMATE
#--ATAN(X) DIRECTLY WILL NEED TO USE A RATIONAL APPROXIMATION
#--(DIVISION NEEDED) ANYWAY BECAUSE A POLYNOMIAL APPROXIMATION
#--WILL INVOLVE A VERY LONG POLYNOMIAL.

#--NOW WE SEE X AS +-2^K * 1.BBBBBBB....B <- 1. + 63 BITS
#--WE CHOSE F TO BE +-2^K * 1.BBBB1
#--THAT IS IT MATCHES THE EXPONENT AND FIRST 5 BITS OF X, THE
#--SIXTH BITS IS SET TO BE 1. SINCE K = -4, -3, ..., 3, THERE
#--ARE ONLY 8 TIMES 16 = 2^7 = 128 |F|'S. SINCE ATAN(-|F|) IS
#-- -ATAN(|F|), WE NEED TO STORE ONLY ATAN(|F|).

ATANMAIN:

	and.l		&0xF8000000,XFRAC(%a6)	# FIRST 5 BITS
	or.l		&0x04000000,XFRAC(%a6)	# SET 6-TH BIT TO 1
	mov.l		&0x00000000,XFRACLO(%a6) # LOCATION OF X IS NOW F

	fmov.x		%fp0,%fp1		# FP1 IS X
	fmul.x		X(%a6),%fp1		# FP1 IS X*F, NOTE THAT X*F > 0
	fsub.x		X(%a6),%fp0		# FP0 IS X-F
	fadd.s		&0x3F800000,%fp1	# FP1 IS 1 + X*F
	fdiv.x		%fp1,%fp0		# FP0 IS U = (X-F)/(1+X*F)

#--WHILE THE DIVISION IS TAKING ITS TIME, WE FETCH ATAN(|F|)
#--CREATE ATAN(F) AND STORE IT IN ATANF, AND
#--SAVE REGISTERS FP2.

	mov.l		%d2,-(%sp)		# SAVE d2 TEMPORARILY
	mov.l		%d1,%d2			# THE EXP AND 16 BITS OF X
	and.l		&0x00007800,%d1		# 4 VARYING BITS OF F'S FRACTION
	and.l		&0x7FFF0000,%d2		# EXPONENT OF F
	sub.l		&0x3FFB0000,%d2		# K+4
	asr.l		&1,%d2
	add.l		%d2,%d1			# THE 7 BITS IDENTIFYING F
	asr.l		&7,%d1			# INDEX INTO TBL OF ATAN(|F|)
	lea		ATANTBL(%pc),%a1
	add.l		%d1,%a1			# ADDRESS OF ATAN(|F|)
	mov.l		(%a1)+,ATANF(%a6)
	mov.l		(%a1)+,ATANFHI(%a6)
	mov.l		(%a1)+,ATANFLO(%a6)	# ATANF IS NOW ATAN(|F|)
	mov.l		X(%a6),%d1		# LOAD SIGN AND EXPO. AGAIN
	and.l		&0x80000000,%d1		# SIGN(F)
	or.l		%d1,ATANF(%a6)		# ATANF IS NOW SIGN(F)*ATAN(|F|)
	mov.l		(%sp)+,%d2		# RESTORE d2

#--THAT'S ALL I HAVE TO DO FOR NOW,
#--BUT ALAS, THE DIVIDE IS STILL CRANKING!

#--U IN FP0, WE ARE NOW READY TO COMPUTE ATAN(U) AS
#--U + A1*U*V*(A2 + V*(A3 + V)), V = U*U
#--THE POLYNOMIAL MAY LOOK STRANGE, BUT IS NEVERTHELESS CORRECT.
#--THE NATURAL FORM IS U + U*V*(A1 + V*(A2 + V*A3))
#--WHAT WE HAVE HERE IS MERELY	A1 = A3, A2 = A1/A3, A3 = A2/A3.
#--THE REASON FOR THIS REARRANGEMENT IS TO MAKE THE INDEPENDENT
#--PARTS A1*U*V AND (A2 + ... STUFF) MORE LOAD-BALANCED

	fmovm.x		&0x04,-(%sp)		# save fp2

	fmov.x		%fp0,%fp1
	fmul.x		%fp1,%fp1
	fmov.d		ATANA3(%pc),%fp2
	fadd.x		%fp1,%fp2		# A3+V
	fmul.x		%fp1,%fp2		# V*(A3+V)
	fmul.x		%fp0,%fp1		# U*V
	fadd.d		ATANA2(%pc),%fp2	# A2+V*(A3+V)
	fmul.d		ATANA1(%pc),%fp1	# A1*U*V
	fmul.x		%fp2,%fp1		# A1*U*V*(A2+V*(A3+V))
	fadd.x		%fp1,%fp0		# ATAN(U), FP1 RELEASED

	fmovm.x		(%sp)+,&0x20		# restore fp2

	fmov.l		%d0,%fpcr		# restore users rnd mode,prec
	fadd.x		ATANF(%a6),%fp0		# ATAN(X)
	bra		t_inx2

ATANBORS:
#--|X| IS IN d0 IN COMPACT FORM. FP1, d0 SAVED.
#--FP0 IS X AND |X| <= 1/16 OR |X| >= 16.
	cmp.l		%d1,&0x3FFF8000
	bgt.w		ATANBIG			# I.E. |X| >= 16

ATANSM:
#--|X| <= 1/16
#--IF |X| < 2^(-40), RETURN X AS ANSWER. OTHERWISE, APPROXIMATE
#--ATAN(X) BY X + X*Y*(B1+Y*(B2+Y*(B3+Y*(B4+Y*(B5+Y*B6)))))
#--WHICH IS X + X*Y*( [B1+Z*(B3+Z*B5)] + [Y*(B2+Z*(B4+Z*B6)] )
#--WHERE Y = X*X, AND Z = Y*Y.

	cmp.l		%d1,&0x3FD78000
	blt.w		ATANTINY

#--COMPUTE POLYNOMIAL
	fmovm.x		&0x0c,-(%sp)		# save fp2/fp3

	fmul.x		%fp0,%fp0		# FPO IS Y = X*X

	fmov.x		%fp0,%fp1
	fmul.x		%fp1,%fp1		# FP1 IS Z = Y*Y

	fmov.d		ATANB6(%pc),%fp2
	fmov.d		ATANB5(%pc),%fp3

	fmul.x		%fp1,%fp2		# Z*B6
	fmul.x		%fp1,%fp3		# Z*B5

	fadd.d		ATANB4(%pc),%fp2	# B4+Z*B6
	fadd.d		ATANB3(%pc),%fp3	# B3+Z*B5

	fmul.x		%fp1,%fp2		# Z*(B4+Z*B6)
	fmul.x		%fp3,%fp1		# Z*(B3+Z*B5)

	fadd.d		ATANB2(%pc),%fp2	# B2+Z*(B4+Z*B6)
	fadd.d		ATANB1(%pc),%fp1	# B1+Z*(B3+Z*B5)

	fmul.x		%fp0,%fp2		# Y*(B2+Z*(B4+Z*B6))
	fmul.x		X(%a6),%fp0		# X*Y

	fadd.x		%fp2,%fp1		# [B1+Z*(B3+Z*B5)]+[Y*(B2+Z*(B4+Z*B6))]

	fmul.x		%fp1,%fp0		# X*Y*([B1+Z*(B3+Z*B5)]+[Y*(B2+Z*(B4+Z*B6))])

	fmovm.x		(%sp)+,&0x30		# restore fp2/fp3

	fmov.l		%d0,%fpcr		# restore users rnd mode,prec
	fadd.x		X(%a6),%fp0
	bra		t_inx2

ATANTINY:
#--|X| < 2^(-40), ATAN(X) = X

	fmov.l		%d0,%fpcr		# restore users rnd mode,prec
	mov.b		&FMOV_OP,%d1		# last inst is MOVE
	fmov.x		X(%a6),%fp0		# last inst - possible exception set

	bra		t_catch

ATANBIG:
#--IF |X| > 2^(100), RETURN	SIGN(X)*(PI/2 - TINY). OTHERWISE,
#--RETURN SIGN(X)*PI/2 + ATAN(-1/X).
	cmp.l		%d1,&0x40638000
	bgt.w		ATANHUGE

#--APPROXIMATE ATAN(-1/X) BY
#--X'+X'*Y*(C1+Y*(C2+Y*(C3+Y*(C4+Y*C5)))), X' = -1/X, Y = X'*X'
#--THIS CAN BE RE-WRITTEN AS
#--X'+X'*Y*( [C1+Z*(C3+Z*C5)] + [Y*(C2+Z*C4)] ), Z = Y*Y.

	fmovm.x		&0x0c,-(%sp)		# save fp2/fp3

	fmov.s		&0xBF800000,%fp1	# LOAD -1
	fdiv.x		%fp0,%fp1		# FP1 IS -1/X

#--DIVIDE IS STILL CRANKING

	fmov.x		%fp1,%fp0		# FP0 IS X'
	fmul.x		%fp0,%fp0		# FP0 IS Y = X'*X'
	fmov.x		%fp1,X(%a6)		# X IS REALLY X'

	fmov.x		%fp0,%fp1
	fmul.x		%fp1,%fp1		# FP1 IS Z = Y*Y

	fmov.d		ATANC5(%pc),%fp3
	fmov.d		ATANC4(%pc),%fp2

	fmul.x		%fp1,%fp3		# Z*C5
	fmul.x		%fp1,%fp2		# Z*B4

	fadd.d		ATANC3(%pc),%fp3	# C3+Z*C5
	fadd.d		ATANC2(%pc),%fp2	# C2+Z*C4

	fmul.x		%fp3,%fp1		# Z*(C3+Z*C5), FP3 RELEASED
	fmul.x		%fp0,%fp2		# Y*(C2+Z*C4)

	fadd.d		ATANC1(%pc),%fp1	# C1+Z*(C3+Z*C5)
	fmul.x		X(%a6),%fp0		# X'*Y

	fadd.x		%fp2,%fp1		# [Y*(C2+Z*C4)]+[C1+Z*(C3+Z*C5)]

	fmul.x		%fp1,%fp0		# X'*Y*([B1+Z*(B3+Z*B5)]
#					...	+[Y*(B2+Z*(B4+Z*B6))])
	fadd.x		X(%a6),%fp0

	fmovm.x		(%sp)+,&0x30		# restore fp2/fp3

	fmov.l		%d0,%fpcr		# restore users rnd mode,prec
	tst.b		(%a0)
	bpl.b		pos_big

neg_big:
	fadd.x		NPIBY2(%pc),%fp0
	bra		t_minx2

pos_big:
	fadd.x		PPIBY2(%pc),%fp0
	bra		t_pinx2

ATANHUGE:
#--RETURN SIGN(X)*(PIBY2 - TINY) = SIGN(X)*PIBY2 - SIGN(X)*TINY
	tst.b		(%a0)
	bpl.b		pos_huge

neg_huge:
	fmov.x		NPIBY2(%pc),%fp0
	fmov.l		%d0,%fpcr
	fadd.x		PTINY(%pc),%fp0
	bra		t_minx2

pos_huge:
	fmov.x		PPIBY2(%pc),%fp0
	fmov.l		%d0,%fpcr
	fadd.x		NTINY(%pc),%fp0
	bra		t_pinx2

	global		satand
#--ENTRY POINT FOR ATAN(X) FOR DENORMALIZED ARGUMENT
satand:
	bra		t_extdnrm

#########################################################################
# sasin():  computes the inverse sine of a normalized input		#
# sasind(): computes the inverse sine of a denormalized input		#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT **************************************************************	#
#	fp0 = arcsin(X)							#
#									#
# ACCURACY and MONOTONICITY *******************************************	#
#	The returned result is within 3 ulps in	64 significant bit,	#
#	i.e. within 0.5001 ulp to 53 bits if the result is subsequently	#
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM ***********************************************************	#
#									#
#	ASIN								#
#	1. If |X| >= 1, go to 3.					#
#									#
#	2. (|X| < 1) Calculate asin(X) by				#
#		z := sqrt( [1-X][1+X] )					#
#		asin(X) = atan( x / z ).				#
#		Exit.							#
#									#
#	3. If |X| > 1, go to 5.						#
#									#
#	4. (|X| = 1) sgn := sign(X), return asin(X) := sgn * Pi/2. Exit.#
#									#
#	5. (|X| > 1) Generate an invalid operation by 0 * infinity.	#
#		Exit.							#
#									#
#########################################################################

	global		sasin
sasin:
	fmov.x		(%a0),%fp0		# LOAD INPUT

	mov.l		(%a0),%d1
	mov.w		4(%a0),%d1
	and.l		&0x7FFFFFFF,%d1
	cmp.l		%d1,&0x3FFF8000
	bge.b		ASINBIG

# This catch is added here for the '060 QSP. Originally, the call to
# satan() would handle this case by causing the exception which would
# not be caught until gen_except(). Now, with the exceptions being
# detected inside of satan(), the exception would have been handled there
# instead of inside sasin() as expected.
	cmp.l		%d1,&0x3FD78000
	blt.w		ASINTINY

#--THIS IS THE USUAL CASE, |X| < 1
#--ASIN(X) = ATAN( X / SQRT( (1-X)(1+X) ) )

ASINMAIN:
	fmov.s		&0x3F800000,%fp1
	fsub.x		%fp0,%fp1		# 1-X
	fmovm.x		&0x4,-(%sp)		#  {fp2}
	fmov.s		&0x3F800000,%fp2
	fadd.x		%fp0,%fp2		# 1+X
	fmul.x		%fp2,%fp1		# (1+X)(1-X)
	fmovm.x		(%sp)+,&0x20		#  {fp2}
	fsqrt.x		%fp1			# SQRT([1-X][1+X])
	fdiv.x		%fp1,%fp0		# X/SQRT([1-X][1+X])
	fmovm.x		&0x01,-(%sp)		# save X/SQRT(...)
	lea		(%sp),%a0		# pass ptr to X/SQRT(...)
	bsr		satan
	add.l		&0xc,%sp		# clear X/SQRT(...) from stack
	bra		t_inx2

ASINBIG:
	fabs.x		%fp0			# |X|
	fcmp.s		%fp0,&0x3F800000
	fbgt		t_operr			# cause an operr exception

#--|X| = 1, ASIN(X) = +- PI/2.
ASINONE:
	fmov.x		PIBY2(%pc),%fp0
	mov.l		(%a0),%d1
	and.l		&0x80000000,%d1		# SIGN BIT OF X
	or.l		&0x3F800000,%d1		# +-1 IN SGL FORMAT
	mov.l		%d1,-(%sp)		# push SIGN(X) IN SGL-FMT
	fmov.l		%d0,%fpcr
	fmul.s		(%sp)+,%fp0
	bra		t_inx2

#--|X| < 2^(-40), ATAN(X) = X
ASINTINY:
	fmov.l		%d0,%fpcr		# restore users rnd mode,prec
	mov.b		&FMOV_OP,%d1		# last inst is MOVE
	fmov.x		(%a0),%fp0		# last inst - possible exception
	bra		t_catch

	global		sasind
#--ASIN(X) = X FOR DENORMALIZED X
sasind:
	bra		t_extdnrm

#########################################################################
# sacos():  computes the inverse cosine of a normalized input		#
# sacosd(): computes the inverse cosine of a denormalized input		#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT ************************************************************** #
#	fp0 = arccos(X)							#
#									#
# ACCURACY and MONOTONICITY *******************************************	#
#	The returned result is within 3 ulps in	64 significant bit,	#
#	i.e. within 0.5001 ulp to 53 bits if the result is subsequently	#
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM *********************************************************** #
#									#
#	ACOS								#
#	1. If |X| >= 1, go to 3.					#
#									#
#	2. (|X| < 1) Calculate acos(X) by				#
#		z := (1-X) / (1+X)					#
#		acos(X) = 2 * atan( sqrt(z) ).				#
#		Exit.							#
#									#
#	3. If |X| > 1, go to 5.						#
#									#
#	4. (|X| = 1) If X > 0, return 0. Otherwise, return Pi. Exit.	#
#									#
#	5. (|X| > 1) Generate an invalid operation by 0 * infinity.	#
#		Exit.							#
#									#
#########################################################################

	global		sacos
sacos:
	fmov.x		(%a0),%fp0		# LOAD INPUT

	mov.l		(%a0),%d1		# pack exp w/ upper 16 fraction
	mov.w		4(%a0),%d1
	and.l		&0x7FFFFFFF,%d1
	cmp.l		%d1,&0x3FFF8000
	bge.b		ACOSBIG

#--THIS IS THE USUAL CASE, |X| < 1
#--ACOS(X) = 2 * ATAN(	SQRT( (1-X)/(1+X) ) )

ACOSMAIN:
	fmov.s		&0x3F800000,%fp1
	fadd.x		%fp0,%fp1		# 1+X
	fneg.x		%fp0			# -X
	fadd.s		&0x3F800000,%fp0	# 1-X
	fdiv.x		%fp1,%fp0		# (1-X)/(1+X)
	fsqrt.x		%fp0			# SQRT((1-X)/(1+X))
	mov.l		%d0,-(%sp)		# save original users fpcr
	clr.l		%d0
	fmovm.x		&0x01,-(%sp)		# save SQRT(...) to stack
	lea		(%sp),%a0		# pass ptr to sqrt
	bsr		satan			# ATAN(SQRT([1-X]/[1+X]))
	add.l		&0xc,%sp		# clear SQRT(...) from stack

	fmov.l		(%sp)+,%fpcr		# restore users round prec,mode
	fadd.x		%fp0,%fp0		# 2 * ATAN( STUFF )
	bra		t_pinx2

ACOSBIG:
	fabs.x		%fp0
	fcmp.s		%fp0,&0x3F800000
	fbgt		t_operr			# cause an operr exception

#--|X| = 1, ACOS(X) = 0 OR PI
	tst.b		(%a0)			# is X positive or negative?
	bpl.b		ACOSP1

#--X = -1
#Returns PI and inexact exception
ACOSM1:
	fmov.x		PI(%pc),%fp0		# load PI
	fmov.l		%d0,%fpcr		# load round mode,prec
	fadd.s		&0x00800000,%fp0	# add a small value
	bra		t_pinx2

ACOSP1:
	bra		ld_pzero		# answer is positive zero

	global		sacosd
#--ACOS(X) = PI/2 FOR DENORMALIZED X
sacosd:
	fmov.l		%d0,%fpcr		# load user's rnd mode/prec
	fmov.x		PIBY2(%pc),%fp0
	bra		t_pinx2

#########################################################################
# setox():    computes the exponential for a normalized input		#
# setoxd():   computes the exponential for a denormalized input		#
# setoxm1():  computes the exponential minus 1 for a normalized input	#
# setoxm1d(): computes the exponential minus 1 for a denormalized input	#
#									#
# INPUT	*************************************************************** #
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT ************************************************************** #
#	fp0 = exp(X) or exp(X)-1					#
#									#
# ACCURACY and MONOTONICITY ******************************************* #
#	The returned result is within 0.85 ulps in 64 significant bit,	#
#	i.e. within 0.5001 ulp to 53 bits if the result is subsequently #
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM and IMPLEMENTATION **************************************** #
#									#
#	setoxd								#
#	------								#
#	Step 1.	Set ans := 1.0						#
#									#
#	Step 2.	Return	ans := ans + sign(X)*2^(-126). Exit.		#
#	Notes:	This will always generate one exception -- inexact.	#
#									#
#									#
#	setox								#
#	-----								#
#									#
#	Step 1.	Filter out extreme cases of input argument.		#
#		1.1	If |X| >= 2^(-65), go to Step 1.3.		#
#		1.2	Go to Step 7.					#
#		1.3	If |X| < 16380 log(2), go to Step 2.		#
#		1.4	Go to Step 8.					#
#	Notes:	The usual case should take the branches 1.1 -> 1.3 -> 2.#
#		To avoid the use of floating-point comparisons, a	#
#		compact representation of |X| is used. This format is a	#
#		32-bit integer, the upper (more significant) 16 bits	#
#		are the sign and biased exponent field of |X|; the	#
#		lower 16 bits are the 16 most significant fraction	#
#		(including the explicit bit) bits of |X|. Consequently,	#
#		the comparisons in Steps 1.1 and 1.3 can be performed	#
#		by integer comparison. Note also that the constant	#
#		16380 log(2) used in Step 1.3 is also in the compact	#
#		form. Thus taking the branch to Step 2 guarantees	#
#		|X| < 16380 log(2). There is no harm to have a small	#
#		number of cases where |X| is less than,	but close to,	#
#		16380 log(2) and the branch to Step 9 is taken.		#
#									#
#	Step 2.	Calculate N = round-to-nearest-int( X * 64/log2 ).	#
#		2.1	Set AdjFlag := 0 (indicates the branch 1.3 -> 2 #
#			was taken)					#
#		2.2	N := round-to-nearest-integer( X * 64/log2 ).	#
#		2.3	Calculate	J = N mod 64; so J = 0,1,2,..., #
#			or 63.						#
#		2.4	Calculate	M = (N - J)/64; so N = 64M + J.	#
#		2.5	Calculate the address of the stored value of	#
#			2^(J/64).					#
#		2.6	Create the value Scale = 2^M.			#
#	Notes:	The calculation in 2.2 is really performed by		#
#			Z := X * constant				#
#			N := round-to-nearest-integer(Z)		#
#		where							#
#			constant := single-precision( 64/log 2 ).	#
#									#
#		Using a single-precision constant avoids memory		#
#		access. Another effect of using a single-precision	#
#		"constant" is that the calculated value Z is		#
#									#
#			Z = X*(64/log2)*(1+eps), |eps| <= 2^(-24).	#
#									#
#		This error has to be considered later in Steps 3 and 4.	#
#									#
#	Step 3.	Calculate X - N*log2/64.				#
#		3.1	R := X + N*L1,					#
#				where L1 := single-precision(-log2/64).	#
#		3.2	R := R + N*L2,					#
#				L2 := extended-precision(-log2/64 - L1).#
#	Notes:	a) The way L1 and L2 are chosen ensures L1+L2		#
#		approximate the value -log2/64 to 88 bits of accuracy.	#
#		b) N*L1 is exact because N is no longer than 22 bits	#
#		and L1 is no longer than 24 bits.			#
#		c) The calculation X+N*L1 is also exact due to		#
#		cancellation. Thus, R is practically X+N(L1+L2) to full	#
#		64 bits.						#
#		d) It is important to estimate how large can |R| be	#
#		after Step 3.2.						#
#									#
#		N = rnd-to-int( X*64/log2 (1+eps) ), |eps|<=2^(-24)	#
#		X*64/log2 (1+eps)	=	N + f,	|f| <= 0.5	#
#		X*64/log2 - N	=	f - eps*X 64/log2		#
#		X - N*log2/64	=	f*log2/64 - eps*X		#
#									#
#									#
#		Now |X| <= 16446 log2, thus				#
#									#
#			|X - N*log2/64| <= (0.5 + 16446/2^(18))*log2/64	#
#					<= 0.57 log2/64.		#
#		 This bound will be used in Step 4.			#
#									#
#	Step 4.	Approximate exp(R)-1 by a polynomial			#
#		p = R + R*R*(A1 + R*(A2 + R*(A3 + R*(A4 + R*A5))))	#
#	Notes:	a) In order to reduce memory access, the coefficients	#
#		are made as "short" as possible: A1 (which is 1/2), A4	#
#		and A5 are single precision; A2 and A3 are double	#
#		precision.						#
#		b) Even with the restrictions above,			#
#		   |p - (exp(R)-1)| < 2^(-68.8) for all |R| <= 0.0062.	#
#		Note that 0.0062 is slightly bigger than 0.57 log2/64.	#
#		c) To fully utilize the pipeline, p is separated into	#
#		two independent pieces of roughly equal complexities	#
#			p = [ R + R*S*(A2 + S*A4) ]	+		#
#				[ S*(A1 + S*(A3 + S*A5)) ]		#
#		where S = R*R.						#
#									#
#	Step 5.	Compute 2^(J/64)*exp(R) = 2^(J/64)*(1+p) by		#
#				ans := T + ( T*p + t)			#
#		where T and t are the stored values for 2^(J/64).	#
#	Notes:	2^(J/64) is stored as T and t where T+t approximates	#
#		2^(J/64) to roughly 85 bits; T is in extended precision	#
#		and t is in single precision. Note also that T is	#
#		rounded to 62 bits so that the last two bits of T are	#
#		zero. The reason for such a special form is that T-1,	#
#		T-2, and T-8 will all be exact --- a property that will	#
#		give much more accurate computation of the function	#
#		EXPM1.							#
#									#
#	Step 6.	Reconstruction of exp(X)				#
#			exp(X) = 2^M * 2^(J/64) * exp(R).		#
#		6.1	If AdjFlag = 0, go to 6.3			#
#		6.2	ans := ans * AdjScale				#
#		6.3	Restore the user FPCR				#
#		6.4	Return ans := ans * Scale. Exit.		#
#	Notes:	If AdjFlag = 0, we have X = Mlog2 + Jlog2/64 + R,	#
#		|M| <= 16380, and Scale = 2^M. Moreover, exp(X) will	#
#		neither overflow nor underflow. If AdjFlag = 1, that	#
#		means that						#
#			X = (M1+M)log2 + Jlog2/64 + R, |M1+M| >= 16380.	#
#		Hence, exp(X) may overflow or underflow or neither.	#
#		When that is the case, AdjScale = 2^(M1) where M1 is	#
#		approximately M. Thus 6.2 will never cause		#
#		over/underflow. Possible exception in 6.4 is overflow	#
#		or underflow. The inexact exception is not generated in	#
#		6.4. Although one can argue that the inexact flag	#
#		should always be raised, to simulate that exception	#
#		cost to much than the flag is worth in practical uses.	#
#									#
#	Step 7.	Return 1 + X.						#
#		7.1	ans := X					#
#		7.2	Restore user FPCR.				#
#		7.3	Return ans := 1 + ans. Exit			#
#	Notes:	For non-zero X, the inexact exception will always be	#
#		raised by 7.3. That is the only exception raised by 7.3.#
#		Note also that we use the FMOVEM instruction to move X	#
#		in Step 7.1 to avoid unnecessary trapping. (Although	#
#		the FMOVEM may not seem relevant since X is normalized,	#
#		the precaution will be useful in the library version of	#
#		this code where the separate entry for denormalized	#
#		inputs will be done away with.)				#
#									#
#	Step 8.	Handle exp(X) where |X| >= 16380log2.			#
#		8.1	If |X| > 16480 log2, go to Step 9.		#
#		(mimic 2.2 - 2.6)					#
#		8.2	N := round-to-integer( X * 64/log2 )		#
#		8.3	Calculate J = N mod 64, J = 0,1,...,63		#
#		8.4	K := (N-J)/64, M1 := truncate(K/2), M = K-M1,	#
#			AdjFlag := 1.					#
#		8.5	Calculate the address of the stored value	#
#			2^(J/64).					#
#		8.6	Create the values Scale = 2^M, AdjScale = 2^M1.	#
#		8.7	Go to Step 3.					#
#	Notes:	Refer to notes for 2.2 - 2.6.				#
#									#
#	Step 9.	Handle exp(X), |X| > 16480 log2.			#
#		9.1	If X < 0, go to 9.3				#
#		9.2	ans := Huge, go to 9.4				#
#		9.3	ans := Tiny.					#
#		9.4	Restore user FPCR.				#
#		9.5	Return ans := ans * ans. Exit.			#
#	Notes:	Exp(X) will surely overflow or underflow, depending on	#
#		X's sign. "Huge" and "Tiny" are respectively large/tiny	#
#		extended-precision numbers whose square over/underflow	#
#		with an inexact result. Thus, 9.5 always raises the	#
#		inexact together with either overflow or underflow.	#
#									#
#	setoxm1d							#
#	--------							#
#									#
#	Step 1.	Set ans := 0						#
#									#
#	Step 2.	Return	ans := X + ans. Exit.				#
#	Notes:	This will return X with the appropriate rounding	#
#		 precision prescribed by the user FPCR.			#
#									#
#	setoxm1								#
#	-------								#
#									#
#	Step 1.	Check |X|						#
#		1.1	If |X| >= 1/4, go to Step 1.3.			#
#		1.2	Go to Step 7.					#
#		1.3	If |X| < 70 log(2), go to Step 2.		#
#		1.4	Go to Step 10.					#
#	Notes:	The usual case should take the branches 1.1 -> 1.3 -> 2.#
#		However, it is conceivable |X| can be small very often	#
#		because EXPM1 is intended to evaluate exp(X)-1		#
#		accurately when |X| is small. For further details on	#
#		the comparisons, see the notes on Step 1 of setox.	#
#									#
#	Step 2.	Calculate N = round-to-nearest-int( X * 64/log2 ).	#
#		2.1	N := round-to-nearest-integer( X * 64/log2 ).	#
#		2.2	Calculate	J = N mod 64; so J = 0,1,2,..., #
#			or 63.						#
#		2.3	Calculate	M = (N - J)/64; so N = 64M + J.	#
#		2.4	Calculate the address of the stored value of	#
#			2^(J/64).					#
#		2.5	Create the values Sc = 2^M and			#
#			OnebySc := -2^(-M).				#
#	Notes:	See the notes on Step 2 of setox.			#
#									#
#	Step 3.	Calculate X - N*log2/64.				#
#		3.1	R := X + N*L1,					#
#				where L1 := single-precision(-log2/64).	#
#		3.2	R := R + N*L2,					#
#				L2 := extended-precision(-log2/64 - L1).#
#	Notes:	Applying the analysis of Step 3 of setox in this case	#
#		shows that |R| <= 0.0055 (note that |X| <= 70 log2 in	#
#		this case).						#
#									#
#	Step 4.	Approximate exp(R)-1 by a polynomial			#
#			p = R+R*R*(A1+R*(A2+R*(A3+R*(A4+R*(A5+R*A6)))))	#
#	Notes:	a) In order to reduce memory access, the coefficients	#
#		are made as "short" as possible: A1 (which is 1/2), A5	#
#		and A6 are single precision; A2, A3 and A4 are double	#
#		precision.						#
#		b) Even with the restriction above,			#
#			|p - (exp(R)-1)| <	|R| * 2^(-72.7)		#
#		for all |R| <= 0.0055.					#
#		c) To fully utilize the pipeline, p is separated into	#
#		two independent pieces of roughly equal complexity	#
#			p = [ R*S*(A2 + S*(A4 + S*A6)) ]	+	#
#				[ R + S*(A1 + S*(A3 + S*A5)) ]		#
#		where S = R*R.						#
#									#
#	Step 5.	Compute 2^(J/64)*p by					#
#				p := T*p				#
#		where T and t are the stored values for 2^(J/64).	#
#	Notes:	2^(J/64) is stored as T and t where T+t approximates	#
#		2^(J/64) to roughly 85 bits; T is in extended precision	#
#		and t is in single precision. Note also that T is	#
#		rounded to 62 bits so that the last two bits of T are	#
#		zero. The reason for such a special form is that T-1,	#
#		T-2, and T-8 will all be exact --- a property that will	#
#		be exploited in Step 6 below. The total relative error	#
#		in p is no bigger than 2^(-67.7) compared to the final	#
#		result.							#
#									#
#	Step 6.	Reconstruction of exp(X)-1				#
#			exp(X)-1 = 2^M * ( 2^(J/64) + p - 2^(-M) ).	#
#		6.1	If M <= 63, go to Step 6.3.			#
#		6.2	ans := T + (p + (t + OnebySc)). Go to 6.6	#
#		6.3	If M >= -3, go to 6.5.				#
#		6.4	ans := (T + (p + t)) + OnebySc. Go to 6.6	#
#		6.5	ans := (T + OnebySc) + (p + t).			#
#		6.6	Restore user FPCR.				#
#		6.7	Return ans := Sc * ans. Exit.			#
#	Notes:	The various arrangements of the expressions give	#
#		accurate evaluations.					#
#									#
#	Step 7.	exp(X)-1 for |X| < 1/4.					#
#		7.1	If |X| >= 2^(-65), go to Step 9.		#
#		7.2	Go to Step 8.					#
#									#
#	Step 8.	Calculate exp(X)-1, |X| < 2^(-65).			#
#		8.1	If |X| < 2^(-16312), goto 8.3			#
#		8.2	Restore FPCR; return ans := X - 2^(-16382).	#
#			Exit.						#
#		8.3	X := X * 2^(140).				#
#		8.4	Restore FPCR; ans := ans - 2^(-16382).		#
#		 Return ans := ans*2^(140). Exit			#
#	Notes:	The idea is to return "X - tiny" under the user		#
#		precision and rounding modes. To avoid unnecessary	#
#		inefficiency, we stay away from denormalized numbers	#
#		the best we can. For |X| >= 2^(-16312), the		#
#		straightforward 8.2 generates the inexact exception as	#
#		the case warrants.					#
#									#
#	Step 9.	Calculate exp(X)-1, |X| < 1/4, by a polynomial		#
#			p = X + X*X*(B1 + X*(B2 + ... + X*B12))		#
#	Notes:	a) In order to reduce memory access, the coefficients	#
#		are made as "short" as possible: B1 (which is 1/2), B9	#
#		to B12 are single precision; B3 to B8 are double	#
#		precision; and B2 is double extended.			#
#		b) Even with the restriction above,			#
#			|p - (exp(X)-1)| < |X| 2^(-70.6)		#
#		for all |X| <= 0.251.					#
#		Note that 0.251 is slightly bigger than 1/4.		#
#		c) To fully preserve accuracy, the polynomial is	#
#		computed as						#
#			X + ( S*B1 +	Q ) where S = X*X and		#
#			Q	=	X*S*(B2 + X*(B3 + ... + X*B12))	#
#		d) To fully utilize the pipeline, Q is separated into	#
#		two independent pieces of roughly equal complexity	#
#			Q = [ X*S*(B2 + S*(B4 + ... + S*B12)) ] +	#
#				[ S*S*(B3 + S*(B5 + ... + S*B11)) ]	#
#									#
#	Step 10. Calculate exp(X)-1 for |X| >= 70 log 2.		#
#		10.1 If X >= 70log2 , exp(X) - 1 = exp(X) for all	#
#		practical purposes. Therefore, go to Step 1 of setox.	#
#		10.2 If X <= -70log2, exp(X) - 1 = -1 for all practical	#
#		purposes.						#
#		ans := -1						#
#		Restore user FPCR					#
#		Return ans := ans + 2^(-126). Exit.			#
#	Notes:	10.2 will always create an inexact and return -1 + tiny	#
#		in the user rounding precision and mode.		#
#									#
#########################################################################

L2:	long		0x3FDC0000,0x82E30865,0x4361C4C6,0x00000000

EEXPA3:	long		0x3FA55555,0x55554CC1
EEXPA2:	long		0x3FC55555,0x55554A54

EM1A4:	long		0x3F811111,0x11174385
EM1A3:	long		0x3FA55555,0x55554F5A

EM1A2:	long		0x3FC55555,0x55555555,0x00000000,0x00000000

EM1B8:	long		0x3EC71DE3,0xA5774682
EM1B7:	long		0x3EFA01A0,0x19D7CB68

EM1B6:	long		0x3F2A01A0,0x1A019DF3
EM1B5:	long		0x3F56C16C,0x16C170E2

EM1B4:	long		0x3F811111,0x11111111
EM1B3:	long		0x3FA55555,0x55555555

EM1B2:	long		0x3FFC0000,0xAAAAAAAA,0xAAAAAAAB
	long		0x00000000

TWO140:	long		0x48B00000,0x00000000
TWON140:
	long		0x37300000,0x00000000

EEXPTBL:
	long		0x3FFF0000,0x80000000,0x00000000,0x00000000
	long		0x3FFF0000,0x8164D1F3,0xBC030774,0x9F841A9B
	long		0x3FFF0000,0x82CD8698,0xAC2BA1D8,0x9FC1D5B9
	long		0x3FFF0000,0x843A28C3,0xACDE4048,0xA0728369
	long		0x3FFF0000,0x85AAC367,0xCC487B14,0x1FC5C95C
	long		0x3FFF0000,0x871F6196,0x9E8D1010,0x1EE85C9F
	long		0x3FFF0000,0x88980E80,0x92DA8528,0x9FA20729
	long		0x3FFF0000,0x8A14D575,0x496EFD9C,0xA07BF9AF
	long		0x3FFF0000,0x8B95C1E3,0xEA8BD6E8,0xA0020DCF
	long		0x3FFF0000,0x8D1ADF5B,0x7E5BA9E4,0x205A63DA
	long		0x3FFF0000,0x8EA4398B,0x45CD53C0,0x1EB70051
	long		0x3FFF0000,0x9031DC43,0x1466B1DC,0x1F6EB029
	long		0x3FFF0000,0x91C3D373,0xAB11C338,0xA0781494
	long		0x3FFF0000,0x935A2B2F,0x13E6E92C,0x9EB319B0
	long		0x3FFF0000,0x94F4EFA8,0xFEF70960,0x2017457D
	long		0x3FFF0000,0x96942D37,0x20185A00,0x1F11D537
	long		0x3FFF0000,0x9837F051,0x8DB8A970,0x9FB952DD
	long		0x3FFF0000,0x99E04593,0x20B7FA64,0x1FE43087
	long		0x3FFF0000,0x9B8D39B9,0xD54E5538,0x1FA2A818
	long		0x3FFF0000,0x9D3ED9A7,0x2CFFB750,0x1FDE494D
	long		0x3FFF0000,0x9EF53260,0x91A111AC,0x20504890
	long		0x3FFF0000,0xA0B0510F,0xB9714FC4,0xA073691C
	long		0x3FFF0000,0xA2704303,0x0C496818,0x1F9B7A05
	long		0x3FFF0000,0xA43515AE,0x09E680A0,0xA0797126
	long		0x3FFF0000,0xA5FED6A9,0xB15138EC,0xA071A140
	long		0x3FFF0000,0xA7CD93B4,0xE9653568,0x204F62DA
	long		0x3FFF0000,0xA9A15AB4,0xEA7C0EF8,0x1F283C4A
	long		0x3FFF0000,0xAB7A39B5,0xA93ED338,0x9F9A7FDC
	long		0x3FFF0000,0xAD583EEA,0x42A14AC8,0xA05B3FAC
	long		0x3FFF0000,0xAF3B78AD,0x690A4374,0x1FDF2610
	long		0x3FFF0000,0xB123F581,0xD2AC2590,0x9F705F90
	long		0x3FFF0000,0xB311C412,0xA9112488,0x201F678A
	long		0x3FFF0000,0xB504F333,0xF9DE6484,0x1F32FB13
	long		0x3FFF0000,0xB6FD91E3,0x28D17790,0x20038B30
	long		0x3FFF0000,0xB8FBAF47,0x62FB9EE8,0x200DC3CC
	long		0x3FFF0000,0xBAFF5AB2,0x133E45FC,0x9F8B2AE6
	long		0x3FFF0000,0xBD08A39F,0x580C36C0,0xA02BBF70
	long		0x3FFF0000,0xBF1799B6,0x7A731084,0xA00BF518
	long		0x3FFF0000,0xC12C4CCA,0x66709458,0xA041DD41
	long		0x3FFF0000,0xC346CCDA,0x24976408,0x9FDF137B
	long		0x3FFF0000,0xC5672A11,0x5506DADC,0x201F1568
	long		0x3FFF0000,0xC78D74C8,0xABB9B15C,0x1FC13A2E
	long		0x3FFF0000,0xC9B9BD86,0x6E2F27A4,0xA03F8F03
	long		0x3FFF0000,0xCBEC14FE,0xF2727C5C,0x1FF4907D
	long		0x3FFF0000,0xCE248C15,0x1F8480E4,0x9E6E53E4
	long		0x3FFF0000,0xD06333DA,0xEF2B2594,0x1FD6D45C
	long		0x3FFF0000,0xD2A81D91,0xF12AE45C,0xA076EDB9
	long		0x3FFF0000,0xD4F35AAB,0xCFEDFA20,0x9FA6DE21
	long		0x3FFF0000,0xD744FCCA,0xD69D6AF4,0x1EE69A2F
	long		0x3FFF0000,0xD99D15C2,0x78AFD7B4,0x207F439F
	long		0x3FFF0000,0xDBFBB797,0xDAF23754,0x201EC207
	long		0x3FFF0000,0xDE60F482,0x5E0E9124,0x9E8BE175
	long		0x3FFF0000,0xE0CCDEEC,0x2A94E110,0x20032C4B
	long		0x3FFF0000,0xE33F8972,0xBE8A5A50,0x2004DFF5
	long		0x3FFF0000,0xE5B906E7,0x7C8348A8,0x1E72F47A
	long		0x3FFF0000,0xE8396A50,0x3C4BDC68,0x1F722F22
	long		0x3FFF0000,0xEAC0C6E7,0xDD243930,0xA017E945
	long		0x3FFF0000,0xED4F301E,0xD9942B84,0x1F401A5B
	long		0x3FFF0000,0xEFE4B99B,0xDCDAF5CC,0x9FB9A9E3
	long		0x3FFF0000,0xF281773C,0x59FFB138,0x20744C05
	long		0x3FFF0000,0xF5257D15,0x2486CC2C,0x1F773A19
	long		0x3FFF0000,0xF7D0DF73,0x0AD13BB8,0x1FFE90D5
	long		0x3FFF0000,0xFA83B2DB,0x722A033C,0xA041ED22
	long		0x3FFF0000,0xFD3E0C0C,0xF486C174,0x1F853F3A

	set		ADJFLAG,L_SCR2
	set		SCALE,FP_SCR0
	set		ADJSCALE,FP_SCR1
	set		SC,FP_SCR0
	set		ONEBYSC,FP_SCR1

	global		setox
setox:
#--entry point for EXP(X), here X is finite, non-zero, and not NaN's

#--Step 1.
	mov.l		(%a0),%d1		# load part of input X
	and.l		&0x7FFF0000,%d1		# biased expo. of X
	cmp.l		%d1,&0x3FBE0000		# 2^(-65)
	bge.b		EXPC1			# normal case
	bra		EXPSM

EXPC1:
#--The case |X| >= 2^(-65)
	mov.w		4(%a0),%d1		# expo. and partial sig. of |X|
	cmp.l		%d1,&0x400CB167		# 16380 log2 trunc. 16 bits
	blt.b		EXPMAIN			# normal case
	bra		EEXPBIG

EXPMAIN:
#--Step 2.
#--This is the normal branch:	2^(-65) <= |X| < 16380 log2.
	fmov.x		(%a0),%fp0		# load input from (a0)

	fmov.x		%fp0,%fp1
	fmul.s		&0x42B8AA3B,%fp0	# 64/log2 * X
	fmovm.x		&0xc,-(%sp)		# save fp2 {%fp2/%fp3}
	mov.l		&0,ADJFLAG(%a6)
	fmov.l		%fp0,%d1		# N = int( X * 64/log2 )
	lea		EEXPTBL(%pc),%a1
	fmov.l		%d1,%fp0		# convert to floating-format

	mov.l		%d1,L_SCR1(%a6)		# save N temporarily
	and.l		&0x3F,%d1		# D0 is J = N mod 64
	lsl.l		&4,%d1
	add.l		%d1,%a1			# address of 2^(J/64)
	mov.l		L_SCR1(%a6),%d1
	asr.l		&6,%d1			# D0 is M
	add.w		&0x3FFF,%d1		# biased expo. of 2^(M)
	mov.w		L2(%pc),L_SCR1(%a6)	# prefetch L2, no need in CB

EXPCONT1:
#--Step 3.
#--fp1,fp2 saved on the stack. fp0 is N, fp1 is X,
#--a0 points to 2^(J/64), D0 is biased expo. of 2^(M)
	fmov.x		%fp0,%fp2
	fmul.s		&0xBC317218,%fp0	# N * L1, L1 = lead(-log2/64)
	fmul.x		L2(%pc),%fp2		# N * L2, L1+L2 = -log2/64
	fadd.x		%fp1,%fp0		# X + N*L1
	fadd.x		%fp2,%fp0		# fp0 is R, reduced arg.

#--Step 4.
#--WE NOW COMPUTE EXP(R)-1 BY A POLYNOMIAL
#-- R + R*R*(A1 + R*(A2 + R*(A3 + R*(A4 + R*A5))))
#--TO FULLY UTILIZE THE PIPELINE, WE COMPUTE S = R*R
#--[R+R*S*(A2+S*A4)] + [S*(A1+S*(A3+S*A5))]

	fmov.x		%fp0,%fp1
	fmul.x		%fp1,%fp1		# fp1 IS S = R*R

	fmov.s		&0x3AB60B70,%fp2	# fp2 IS A5

	fmul.x		%fp1,%fp2		# fp2 IS S*A5
	fmov.x		%fp1,%fp3
	fmul.s		&0x3C088895,%fp3	# fp3 IS S*A4

	fadd.d		EEXPA3(%pc),%fp2	# fp2 IS A3+S*A5
	fadd.d		EEXPA2(%pc),%fp3	# fp3 IS A2+S*A4

	fmul.x		%fp1,%fp2		# fp2 IS S*(A3+S*A5)
	mov.w		%d1,SCALE(%a6)		# SCALE is 2^(M) in extended
	mov.l		&0x80000000,SCALE+4(%a6)
	clr.l		SCALE+8(%a6)

	fmul.x		%fp1,%fp3		# fp3 IS S*(A2+S*A4)

	fadd.s		&0x3F000000,%fp2	# fp2 IS A1+S*(A3+S*A5)
	fmul.x		%fp0,%fp3		# fp3 IS R*S*(A2+S*A4)

	fmul.x		%fp1,%fp2		# fp2 IS S*(A1+S*(A3+S*A5))
	fadd.x		%fp3,%fp0		# fp0 IS R+R*S*(A2+S*A4),

	fmov.x		(%a1)+,%fp1		# fp1 is lead. pt. of 2^(J/64)
	fadd.x		%fp2,%fp0		# fp0 is EXP(R) - 1

#--Step 5
#--final reconstruction process
#--EXP(X) = 2^M * ( 2^(J/64) + 2^(J/64)*(EXP(R)-1) )

	fmul.x		%fp1,%fp0		# 2^(J/64)*(Exp(R)-1)
	fmovm.x		(%sp)+,&0x30		# fp2 restored {%fp2/%fp3}
	fadd.s		(%a1),%fp0		# accurate 2^(J/64)

	fadd.x		%fp1,%fp0		# 2^(J/64) + 2^(J/64)*...
	mov.l		ADJFLAG(%a6),%d1

#--Step 6
	tst.l		%d1
	beq.b		NORMAL
ADJUST:
	fmul.x		ADJSCALE(%a6),%fp0
NORMAL:
	fmov.l		%d0,%fpcr		# restore user FPCR
	mov.b		&FMUL_OP,%d1		# last inst is MUL
	fmul.x		SCALE(%a6),%fp0		# multiply 2^(M)
	bra		t_catch

EXPSM:
#--Step 7
	fmovm.x		(%a0),&0x80		# load X
	fmov.l		%d0,%fpcr
	fadd.s		&0x3F800000,%fp0	# 1+X in user mode
	bra		t_pinx2

EEXPBIG:
#--Step 8
	cmp.l		%d1,&0x400CB27C		# 16480 log2
	bgt.b		EXP2BIG
#--Steps 8.2 -- 8.6
	fmov.x		(%a0),%fp0		# load input from (a0)

	fmov.x		%fp0,%fp1
	fmul.s		&0x42B8AA3B,%fp0	# 64/log2 * X
	fmovm.x		&0xc,-(%sp)		# save fp2 {%fp2/%fp3}
	mov.l		&1,ADJFLAG(%a6)
	fmov.l		%fp0,%d1		# N = int( X * 64/log2 )
	lea		EEXPTBL(%pc),%a1
	fmov.l		%d1,%fp0		# convert to floating-format
	mov.l		%d1,L_SCR1(%a6)		# save N temporarily
	and.l		&0x3F,%d1		# D0 is J = N mod 64
	lsl.l		&4,%d1
	add.l		%d1,%a1			# address of 2^(J/64)
	mov.l		L_SCR1(%a6),%d1
	asr.l		&6,%d1			# D0 is K
	mov.l		%d1,L_SCR1(%a6)		# save K temporarily
	asr.l		&1,%d1			# D0 is M1
	sub.l		%d1,L_SCR1(%a6)		# a1 is M
	add.w		&0x3FFF,%d1		# biased expo. of 2^(M1)
	mov.w		%d1,ADJSCALE(%a6)	# ADJSCALE := 2^(M1)
	mov.l		&0x80000000,ADJSCALE+4(%a6)
	clr.l		ADJSCALE+8(%a6)
	mov.l		L_SCR1(%a6),%d1		# D0 is M
	add.w		&0x3FFF,%d1		# biased expo. of 2^(M)
	bra.w		EXPCONT1		# go back to Step 3

EXP2BIG:
#--Step 9
	tst.b		(%a0)			# is X positive or negative?
	bmi		t_unfl2
	bra		t_ovfl2

	global		setoxd
setoxd:
#--entry point for EXP(X), X is denormalized
	mov.l		(%a0),-(%sp)
	andi.l		&0x80000000,(%sp)
	ori.l		&0x00800000,(%sp)	# sign(X)*2^(-126)

	fmov.s		&0x3F800000,%fp0

	fmov.l		%d0,%fpcr
	fadd.s		(%sp)+,%fp0
	bra		t_pinx2

	global		setoxm1
setoxm1:
#--entry point for EXPM1(X), here X is finite, non-zero, non-NaN

#--Step 1.
#--Step 1.1
	mov.l		(%a0),%d1		# load part of input X
	and.l		&0x7FFF0000,%d1		# biased expo. of X
	cmp.l		%d1,&0x3FFD0000		# 1/4
	bge.b		EM1CON1			# |X| >= 1/4
	bra		EM1SM

EM1CON1:
#--Step 1.3
#--The case |X| >= 1/4
	mov.w		4(%a0),%d1		# expo. and partial sig. of |X|
	cmp.l		%d1,&0x4004C215		# 70log2 rounded up to 16 bits
	ble.b		EM1MAIN			# 1/4 <= |X| <= 70log2
	bra		EM1BIG

EM1MAIN:
#--Step 2.
#--This is the case:	1/4 <= |X| <= 70 log2.
	fmov.x		(%a0),%fp0		# load input from (a0)

	fmov.x		%fp0,%fp1
	fmul.s		&0x42B8AA3B,%fp0	# 64/log2 * X
	fmovm.x		&0xc,-(%sp)		# save fp2 {%fp2/%fp3}
	fmov.l		%fp0,%d1		# N = int( X * 64/log2 )
	lea		EEXPTBL(%pc),%a1
	fmov.l		%d1,%fp0		# convert to floating-format

	mov.l		%d1,L_SCR1(%a6)		# save N temporarily
	and.l		&0x3F,%d1		# D0 is J = N mod 64
	lsl.l		&4,%d1
	add.l		%d1,%a1			# address of 2^(J/64)
	mov.l		L_SCR1(%a6),%d1
	asr.l		&6,%d1			# D0 is M
	mov.l		%d1,L_SCR1(%a6)		# save a copy of M

#--Step 3.
#--fp1,fp2 saved on the stack. fp0 is N, fp1 is X,
#--a0 points to 2^(J/64), D0 and a1 both contain M
	fmov.x		%fp0,%fp2
	fmul.s		&0xBC317218,%fp0	# N * L1, L1 = lead(-log2/64)
	fmul.x		L2(%pc),%fp2		# N * L2, L1+L2 = -log2/64
	fadd.x		%fp1,%fp0		# X + N*L1
	fadd.x		%fp2,%fp0		# fp0 is R, reduced arg.
	add.w		&0x3FFF,%d1		# D0 is biased expo. of 2^M

#--Step 4.
#--WE NOW COMPUTE EXP(R)-1 BY A POLYNOMIAL
#-- R + R*R*(A1 + R*(A2 + R*(A3 + R*(A4 + R*(A5 + R*A6)))))
#--TO FULLY UTILIZE THE PIPELINE, WE COMPUTE S = R*R
#--[R*S*(A2+S*(A4+S*A6))] + [R+S*(A1+S*(A3+S*A5))]

	fmov.x		%fp0,%fp1
	fmul.x		%fp1,%fp1		# fp1 IS S = R*R

	fmov.s		&0x3950097B,%fp2	# fp2 IS a6

	fmul.x		%fp1,%fp2		# fp2 IS S*A6
	fmov.x		%fp1,%fp3
	fmul.s		&0x3AB60B6A,%fp3	# fp3 IS S*A5

	fadd.d		EM1A4(%pc),%fp2		# fp2 IS A4+S*A6
	fadd.d		EM1A3(%pc),%fp3		# fp3 IS A3+S*A5
	mov.w		%d1,SC(%a6)		# SC is 2^(M) in extended
	mov.l		&0x80000000,SC+4(%a6)
	clr.l		SC+8(%a6)

	fmul.x		%fp1,%fp2		# fp2 IS S*(A4+S*A6)
	mov.l		L_SCR1(%a6),%d1		# D0 is	M
	neg.w		%d1			# D0 is -M
	fmul.x		%fp1,%fp3		# fp3 IS S*(A3+S*A5)
	add.w		&0x3FFF,%d1		# biased expo. of 2^(-M)
	fadd.d		EM1A2(%pc),%fp2		# fp2 IS A2+S*(A4+S*A6)
	fadd.s		&0x3F000000,%fp3	# fp3 IS A1+S*(A3+S*A5)

	fmul.x		%fp1,%fp2		# fp2 IS S*(A2+S*(A4+S*A6))
	or.w		&0x8000,%d1		# signed/expo. of -2^(-M)
	mov.w		%d1,ONEBYSC(%a6)	# OnebySc is -2^(-M)
	mov.l		&0x80000000,ONEBYSC+4(%a6)
	clr.l		ONEBYSC+8(%a6)
	fmul.x		%fp3,%fp1		# fp1 IS S*(A1+S*(A3+S*A5))

	fmul.x		%fp0,%fp2		# fp2 IS R*S*(A2+S*(A4+S*A6))
	fadd.x		%fp1,%fp0		# fp0 IS R+S*(A1+S*(A3+S*A5))

	fadd.x		%fp2,%fp0		# fp0 IS EXP(R)-1

	fmovm.x		(%sp)+,&0x30		# fp2 restored {%fp2/%fp3}

#--Step 5
#--Compute 2^(J/64)*p

	fmul.x		(%a1),%fp0		# 2^(J/64)*(Exp(R)-1)

#--Step 6
#--Step 6.1
	mov.l		L_SCR1(%a6),%d1		# retrieve M
	cmp.l		%d1,&63
	ble.b		MLE63
#--Step 6.2	M >= 64
	fmov.s		12(%a1),%fp1		# fp1 is t
	fadd.x		ONEBYSC(%a6),%fp1	# fp1 is t+OnebySc
	fadd.x		%fp1,%fp0		# p+(t+OnebySc), fp1 released
	fadd.x		(%a1),%fp0		# T+(p+(t+OnebySc))
	bra		EM1SCALE
MLE63:
#--Step 6.3	M <= 63
	cmp.l		%d1,&-3
	bge.b		MGEN3
MLTN3:
#--Step 6.4	M <= -4
	fadd.s		12(%a1),%fp0		# p+t
	fadd.x		(%a1),%fp0		# T+(p+t)
	fadd.x		ONEBYSC(%a6),%fp0	# OnebySc + (T+(p+t))
	bra		EM1SCALE
MGEN3:
#--Step 6.5	-3 <= M <= 63
	fmov.x		(%a1)+,%fp1		# fp1 is T
	fadd.s		(%a1),%fp0		# fp0 is p+t
	fadd.x		ONEBYSC(%a6),%fp1	# fp1 is T+OnebySc
	fadd.x		%fp1,%fp0		# (T+OnebySc)+(p+t)

EM1SCALE:
#--Step 6.6
	fmov.l		%d0,%fpcr
	fmul.x		SC(%a6),%fp0
	bra		t_inx2

EM1SM:
#--Step 7	|X| < 1/4.
	cmp.l		%d1,&0x3FBE0000		# 2^(-65)
	bge.b		EM1POLY

EM1TINY:
#--Step 8	|X| < 2^(-65)
	cmp.l		%d1,&0x00330000		# 2^(-16312)
	blt.b		EM12TINY
#--Step 8.2
	mov.l		&0x80010000,SC(%a6)	# SC is -2^(-16382)
	mov.l		&0x80000000,SC+4(%a6)
	clr.l		SC+8(%a6)
	fmov.x		(%a0),%fp0
	fmov.l		%d0,%fpcr
	mov.b		&FADD_OP,%d1		# last inst is ADD
	fadd.x		SC(%a6),%fp0
	bra		t_catch

EM12TINY:
#--Step 8.3
	fmov.x		(%a0),%fp0
	fmul.d		TWO140(%pc),%fp0
	mov.l		&0x80010000,SC(%a6)
	mov.l		&0x80000000,SC+4(%a6)
	clr.l		SC+8(%a6)
	fadd.x		SC(%a6),%fp0
	fmov.l		%d0,%fpcr
	mov.b		&FMUL_OP,%d1		# last inst is MUL
	fmul.d		TWON140(%pc),%fp0
	bra		t_catch

EM1POLY:
#--Step 9	exp(X)-1 by a simple polynomial
	fmov.x		(%a0),%fp0		# fp0 is X
	fmul.x		%fp0,%fp0		# fp0 is S := X*X
	fmovm.x		&0xc,-(%sp)		# save fp2 {%fp2/%fp3}
	fmov.s		&0x2F30CAA8,%fp1	# fp1 is B12
	fmul.x		%fp0,%fp1		# fp1 is S*B12
	fmov.s		&0x310F8290,%fp2	# fp2 is B11
	fadd.s		&0x32D73220,%fp1	# fp1 is B10+S*B12

	fmul.x		%fp0,%fp2		# fp2 is S*B11
	fmul.x		%fp0,%fp1		# fp1 is S*(B10 + ...

	fadd.s		&0x3493F281,%fp2	# fp2 is B9+S*...
	fadd.d		EM1B8(%pc),%fp1		# fp1 is B8+S*...

	fmul.x		%fp0,%fp2		# fp2 is S*(B9+...
	fmul.x		%fp0,%fp1		# fp1 is S*(B8+...

	fadd.d		EM1B7(%pc),%fp2		# fp2 is B7+S*...
	fadd.d		EM1B6(%pc),%fp1		# fp1 is B6+S*...

	fmul.x		%fp0,%fp2		# fp2 is S*(B7+...
	fmul.x		%fp0,%fp1		# fp1 is S*(B6+...

	fadd.d		EM1B5(%pc),%fp2		# fp2 is B5+S*...
	fadd.d		EM1B4(%pc),%fp1		# fp1 is B4+S*...

	fmul.x		%fp0,%fp2		# fp2 is S*(B5+...
	fmul.x		%fp0,%fp1		# fp1 is S*(B4+...

	fadd.d		EM1B3(%pc),%fp2		# fp2 is B3+S*...
	fadd.x		EM1B2(%pc),%fp1		# fp1 is B2+S*...

	fmul.x		%fp0,%fp2		# fp2 is S*(B3+...
	fmul.x		%fp0,%fp1		# fp1 is S*(B2+...

	fmul.x		%fp0,%fp2		# fp2 is S*S*(B3+...)
	fmul.x		(%a0),%fp1		# fp1 is X*S*(B2...

	fmul.s		&0x3F000000,%fp0	# fp0 is S*B1
	fadd.x		%fp2,%fp1		# fp1 is Q

	fmovm.x		(%sp)+,&0x30		# fp2 restored {%fp2/%fp3}

	fadd.x		%fp1,%fp0		# fp0 is S*B1+Q

	fmov.l		%d0,%fpcr
	fadd.x		(%a0),%fp0
	bra		t_inx2

EM1BIG:
#--Step 10	|X| > 70 log2
	mov.l		(%a0),%d1
	cmp.l		%d1,&0
	bgt.w		EXPC1
#--Step 10.2
	fmov.s		&0xBF800000,%fp0	# fp0 is -1
	fmov.l		%d0,%fpcr
	fadd.s		&0x00800000,%fp0	# -1 + 2^(-126)
	bra		t_minx2

	global		setoxm1d
setoxm1d:
#--entry point for EXPM1(X), here X is denormalized
#--Step 0.
	bra		t_extdnrm

#########################################################################
# sgetexp():  returns the exponent portion of the input argument.	#
#	      The exponent bias is removed and the exponent value is	#
#	      returned as an extended precision number in fp0.		#
# sgetexpd(): handles denormalized numbers.				#
#									#
# sgetman():  extracts the mantissa of the input argument. The		#
#	      mantissa is converted to an extended precision number w/	#
#	      an exponent of $3fff and is returned in fp0. The range of #
#	      the result is [1.0 - 2.0).				#
# sgetmand(): handles denormalized numbers.				#
#									#
# INPUT *************************************************************** #
#	a0  = pointer to extended precision input			#
#									#
# OUTPUT ************************************************************** #
#	fp0 = exponent(X) or mantissa(X)				#
#									#
#########################################################################

	global		sgetexp
sgetexp:
	mov.w		SRC_EX(%a0),%d0		# get the exponent
	bclr		&0xf,%d0		# clear the sign bit
	subi.w		&0x3fff,%d0		# subtract off the bias
	fmov.w		%d0,%fp0		# return exp in fp0
	blt.b		sgetexpn		# it's negative
	rts

sgetexpn:
	mov.b		&neg_bmask,FPSR_CC(%a6)	# set 'N' ccode bit
	rts

	global		sgetexpd
sgetexpd:
	bsr.l		norm			# normalize
	neg.w		%d0			# new exp = -(shft amt)
	subi.w		&0x3fff,%d0		# subtract off the bias
	fmov.w		%d0,%fp0		# return exp in fp0
	mov.b		&neg_bmask,FPSR_CC(%a6)	# set 'N' ccode bit
	rts

	global		sgetman
sgetman:
	mov.w		SRC_EX(%a0),%d0		# get the exp
	ori.w		&0x7fff,%d0		# clear old exp
	bclr		&0xe,%d0		# make it the new exp +-3fff

# here, we build the result in a tmp location so as not to disturb the input
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6) # copy to tmp loc
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6) # copy to tmp loc
	mov.w		%d0,FP_SCR0_EX(%a6)	# insert new exponent
	fmov.x		FP_SCR0(%a6),%fp0	# put new value back in fp0
	bmi.b		sgetmann		# it's negative
	rts

sgetmann:
	mov.b		&neg_bmask,FPSR_CC(%a6)	# set 'N' ccode bit
	rts

#
# For denormalized numbers, shift the mantissa until the j-bit = 1,
# then load the exponent with +/1 $3fff.
#
	global		sgetmand
sgetmand:
	bsr.l		norm			# normalize exponent
	bra.b		sgetman

#########################################################################
# scosh():  computes the hyperbolic cosine of a normalized input	#
# scoshd(): computes the hyperbolic cosine of a denormalized input	#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT **************************************************************	#
#	fp0 = cosh(X)							#
#									#
# ACCURACY and MONOTONICITY *******************************************	#
#	The returned result is within 3 ulps in 64 significant bit,	#
#	i.e. within 0.5001 ulp to 53 bits if the result is subsequently	#
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM ***********************************************************	#
#									#
#	COSH								#
#	1. If |X| > 16380 log2, go to 3.				#
#									#
#	2. (|X| <= 16380 log2) Cosh(X) is obtained by the formulae	#
#		y = |X|, z = exp(Y), and				#
#		cosh(X) = (1/2)*( z + 1/z ).				#
#		Exit.							#
#									#
#	3. (|X| > 16380 log2). If |X| > 16480 log2, go to 5.		#
#									#
#	4. (16380 log2 < |X| <= 16480 log2)				#
#		cosh(X) = sign(X) * exp(|X|)/2.				#
#		However, invoking exp(|X|) may cause premature		#
#		overflow. Thus, we calculate sinh(X) as follows:	#
#		Y	:= |X|						#
#		Fact	:=	2**(16380)				#
#		Y'	:= Y - 16381 log2				#
#		cosh(X) := Fact * exp(Y').				#
#		Exit.							#
#									#
#	5. (|X| > 16480 log2) sinh(X) must overflow. Return		#
#		Huge*Huge to generate overflow and an infinity with	#
#		the appropriate sign. Huge is the largest finite number	#
#		in extended format. Exit.				#
#									#
#########################################################################

TWO16380:
	long		0x7FFB0000,0x80000000,0x00000000,0x00000000

	global		scosh
scosh:
	fmov.x		(%a0),%fp0		# LOAD INPUT

	mov.l		(%a0),%d1
	mov.w		4(%a0),%d1
	and.l		&0x7FFFFFFF,%d1
	cmp.l		%d1,&0x400CB167
	bgt.b		COSHBIG

#--THIS IS THE USUAL CASE, |X| < 16380 LOG2
#--COSH(X) = (1/2) * ( EXP(X) + 1/EXP(X) )

	fabs.x		%fp0			# |X|

	mov.l		%d0,-(%sp)
	clr.l		%d0
	fmovm.x		&0x01,-(%sp)		# save |X| to stack
	lea		(%sp),%a0		# pass ptr to |X|
	bsr		setox			# FP0 IS EXP(|X|)
	add.l		&0xc,%sp		# erase |X| from stack
	fmul.s		&0x3F000000,%fp0	# (1/2)EXP(|X|)
	mov.l		(%sp)+,%d0

	fmov.s		&0x3E800000,%fp1	# (1/4)
	fdiv.x		%fp0,%fp1		# 1/(2 EXP(|X|))

	fmov.l		%d0,%fpcr
	mov.b		&FADD_OP,%d1		# last inst is ADD
	fadd.x		%fp1,%fp0
	bra		t_catch

COSHBIG:
	cmp.l		%d1,&0x400CB2B3
	bgt.b		COSHHUGE

	fabs.x		%fp0
	fsub.d		T1(%pc),%fp0		# (|X|-16381LOG2_LEAD)
	fsub.d		T2(%pc),%fp0		# |X| - 16381 LOG2, ACCURATE

	mov.l		%d0,-(%sp)
	clr.l		%d0
	fmovm.x		&0x01,-(%sp)		# save fp0 to stack
	lea		(%sp),%a0		# pass ptr to fp0
	bsr		setox
	add.l		&0xc,%sp		# clear fp0 from stack
	mov.l		(%sp)+,%d0

	fmov.l		%d0,%fpcr
	mov.b		&FMUL_OP,%d1		# last inst is MUL
	fmul.x		TWO16380(%pc),%fp0
	bra		t_catch

COSHHUGE:
	bra		t_ovfl2

	global		scoshd
#--COSH(X) = 1 FOR DENORMALIZED X
scoshd:
	fmov.s		&0x3F800000,%fp0

	fmov.l		%d0,%fpcr
	fadd.s		&0x00800000,%fp0
	bra		t_pinx2

#########################################################################
# ssinh():  computes the hyperbolic sine of a normalized input		#
# ssinhd(): computes the hyperbolic sine of a denormalized input	#
#									#
# INPUT *************************************************************** #
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT ************************************************************** #
#	fp0 = sinh(X)							#
#									#
# ACCURACY and MONOTONICITY *******************************************	#
#	The returned result is within 3 ulps in 64 significant bit,	#
#	i.e. within 0.5001 ulp to 53 bits if the result is subsequently #
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM *********************************************************** #
#									#
#       SINH								#
#       1. If |X| > 16380 log2, go to 3.				#
#									#
#       2. (|X| <= 16380 log2) Sinh(X) is obtained by the formula	#
#               y = |X|, sgn = sign(X), and z = expm1(Y),		#
#               sinh(X) = sgn*(1/2)*( z + z/(1+z) ).			#
#          Exit.							#
#									#
#       3. If |X| > 16480 log2, go to 5.				#
#									#
#       4. (16380 log2 < |X| <= 16480 log2)				#
#               sinh(X) = sign(X) * exp(|X|)/2.				#
#          However, invoking exp(|X|) may cause premature overflow.	#
#          Thus, we calculate sinh(X) as follows:			#
#             Y       := |X|						#
#             sgn     := sign(X)					#
#             sgnFact := sgn * 2**(16380)				#
#             Y'      := Y - 16381 log2					#
#             sinh(X) := sgnFact * exp(Y').				#
#          Exit.							#
#									#
#       5. (|X| > 16480 log2) sinh(X) must overflow. Return		#
#          sign(X)*Huge*Huge to generate overflow and an infinity with	#
#          the appropriate sign. Huge is the largest finite number in	#
#          extended format. Exit.					#
#									#
#########################################################################

	global		ssinh
ssinh:
	fmov.x		(%a0),%fp0		# LOAD INPUT

	mov.l		(%a0),%d1
	mov.w		4(%a0),%d1
	mov.l		%d1,%a1			# save (compacted) operand
	and.l		&0x7FFFFFFF,%d1
	cmp.l		%d1,&0x400CB167
	bgt.b		SINHBIG

#--THIS IS THE USUAL CASE, |X| < 16380 LOG2
#--Y = |X|, Z = EXPM1(Y), SINH(X) = SIGN(X)*(1/2)*( Z + Z/(1+Z) )

	fabs.x		%fp0			# Y = |X|

	movm.l		&0x8040,-(%sp)		# {a1/d0}
	fmovm.x		&0x01,-(%sp)		# save Y on stack
	lea		(%sp),%a0		# pass ptr to Y
	clr.l		%d0
	bsr		setoxm1			# FP0 IS Z = EXPM1(Y)
	add.l		&0xc,%sp		# clear Y from stack
	fmov.l		&0,%fpcr
	movm.l		(%sp)+,&0x0201		# {a1/d0}

	fmov.x		%fp0,%fp1
	fadd.s		&0x3F800000,%fp1	# 1+Z
	fmov.x		%fp0,-(%sp)
	fdiv.x		%fp1,%fp0		# Z/(1+Z)
	mov.l		%a1,%d1
	and.l		&0x80000000,%d1
	or.l		&0x3F000000,%d1
	fadd.x		(%sp)+,%fp0
	mov.l		%d1,-(%sp)

	fmov.l		%d0,%fpcr
	mov.b		&FMUL_OP,%d1		# last inst is MUL
	fmul.s		(%sp)+,%fp0		# last fp inst - possible exceptions set
	bra		t_catch

SINHBIG:
	cmp.l		%d1,&0x400CB2B3
	bgt		t_ovfl
	fabs.x		%fp0
	fsub.d		T1(%pc),%fp0		# (|X|-16381LOG2_LEAD)
	mov.l		&0,-(%sp)
	mov.l		&0x80000000,-(%sp)
	mov.l		%a1,%d1
	and.l		&0x80000000,%d1
	or.l		&0x7FFB0000,%d1
	mov.l		%d1,-(%sp)		# EXTENDED FMT
	fsub.d		T2(%pc),%fp0		# |X| - 16381 LOG2, ACCURATE

	mov.l		%d0,-(%sp)
	clr.l		%d0
	fmovm.x		&0x01,-(%sp)		# save fp0 on stack
	lea		(%sp),%a0		# pass ptr to fp0
	bsr		setox
	add.l		&0xc,%sp		# clear fp0 from stack

	mov.l		(%sp)+,%d0
	fmov.l		%d0,%fpcr
	mov.b		&FMUL_OP,%d1		# last inst is MUL
	fmul.x		(%sp)+,%fp0		# possible exception
	bra		t_catch

	global		ssinhd
#--SINH(X) = X FOR DENORMALIZED X
ssinhd:
	bra		t_extdnrm

#########################################################################
# stanh():  computes the hyperbolic tangent of a normalized input	#
# stanhd(): computes the hyperbolic tangent of a denormalized input	#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT **************************************************************	#
#	fp0 = tanh(X)							#
#									#
# ACCURACY and MONOTONICITY *******************************************	#
#	The returned result is within 3 ulps in 64 significant bit,	#
#	i.e. within 0.5001 ulp to 53 bits if the result is subsequently #
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM ***********************************************************	#
#									#
#	TANH								#
#	1. If |X| >= (5/2) log2 or |X| <= 2**(-40), go to 3.		#
#									#
#	2. (2**(-40) < |X| < (5/2) log2) Calculate tanh(X) by		#
#		sgn := sign(X), y := 2|X|, z := expm1(Y), and		#
#		tanh(X) = sgn*( z/(2+z) ).				#
#		Exit.							#
#									#
#	3. (|X| <= 2**(-40) or |X| >= (5/2) log2). If |X| < 1,		#
#		go to 7.						#
#									#
#	4. (|X| >= (5/2) log2) If |X| >= 50 log2, go to 6.		#
#									#
#	5. ((5/2) log2 <= |X| < 50 log2) Calculate tanh(X) by		#
#		sgn := sign(X), y := 2|X|, z := exp(Y),			#
#		tanh(X) = sgn - [ sgn*2/(1+z) ].			#
#		Exit.							#
#									#
#	6. (|X| >= 50 log2) Tanh(X) = +-1 (round to nearest). Thus, we	#
#		calculate Tanh(X) by					#
#		sgn := sign(X), Tiny := 2**(-126),			#
#		tanh(X) := sgn - sgn*Tiny.				#
#		Exit.							#
#									#
#	7. (|X| < 2**(-40)). Tanh(X) = X.	Exit.			#
#									#
#########################################################################

	set		X,FP_SCR0
	set		XFRAC,X+4

	set		SGN,L_SCR3

	set		V,FP_SCR0

	global		stanh
stanh:
	fmov.x		(%a0),%fp0		# LOAD INPUT

	fmov.x		%fp0,X(%a6)
	mov.l		(%a0),%d1
	mov.w		4(%a0),%d1
	mov.l		%d1,X(%a6)
	and.l		&0x7FFFFFFF,%d1
	cmp.l		%d1, &0x3fd78000	# is |X| < 2^(-40)?
	blt.w		TANHBORS		# yes
	cmp.l		%d1, &0x3fffddce	# is |X| > (5/2)LOG2?
	bgt.w		TANHBORS		# yes

#--THIS IS THE USUAL CASE
#--Y = 2|X|, Z = EXPM1(Y), TANH(X) = SIGN(X) * Z / (Z+2).

	mov.l		X(%a6),%d1
	mov.l		%d1,SGN(%a6)
	and.l		&0x7FFF0000,%d1
	add.l		&0x00010000,%d1		# EXPONENT OF 2|X|
	mov.l		%d1,X(%a6)
	and.l		&0x80000000,SGN(%a6)
	fmov.x		X(%a6),%fp0		# FP0 IS Y = 2|X|

	mov.l		%d0,-(%sp)
	clr.l		%d0
	fmovm.x		&0x1,-(%sp)		# save Y on stack
	lea		(%sp),%a0		# pass ptr to Y
	bsr		setoxm1			# FP0 IS Z = EXPM1(Y)
	add.l		&0xc,%sp		# clear Y from stack
	mov.l		(%sp)+,%d0

	fmov.x		%fp0,%fp1
	fadd.s		&0x40000000,%fp1	# Z+2
	mov.l		SGN(%a6),%d1
	fmov.x		%fp1,V(%a6)
	eor.l		%d1,V(%a6)

	fmov.l		%d0,%fpcr		# restore users round prec,mode
	fdiv.x		V(%a6),%fp0
	bra		t_inx2

TANHBORS:
	cmp.l		%d1,&0x3FFF8000
	blt.w		TANHSM

	cmp.l		%d1,&0x40048AA1
	bgt.w		TANHHUGE

#-- (5/2) LOG2 < |X| < 50 LOG2,
#--TANH(X) = 1 - (2/[EXP(2X)+1]). LET Y = 2|X|, SGN = SIGN(X),
#--TANH(X) = SGN -	SGN*2/[EXP(Y)+1].

	mov.l		X(%a6),%d1
	mov.l		%d1,SGN(%a6)
	and.l		&0x7FFF0000,%d1
	add.l		&0x00010000,%d1		# EXPO OF 2|X|
	mov.l		%d1,X(%a6)		# Y = 2|X|
	and.l		&0x80000000,SGN(%a6)
	mov.l		SGN(%a6),%d1
	fmov.x		X(%a6),%fp0		# Y = 2|X|

	mov.l		%d0,-(%sp)
	clr.l		%d0
	fmovm.x		&0x01,-(%sp)		# save Y on stack
	lea		(%sp),%a0		# pass ptr to Y
	bsr		setox			# FP0 IS EXP(Y)
	add.l		&0xc,%sp		# clear Y from stack
	mov.l		(%sp)+,%d0
	mov.l		SGN(%a6),%d1
	fadd.s		&0x3F800000,%fp0	# EXP(Y)+1

	eor.l		&0xC0000000,%d1		# -SIGN(X)*2
	fmov.s		%d1,%fp1		# -SIGN(X)*2 IN SGL FMT
	fdiv.x		%fp0,%fp1		# -SIGN(X)2 / [EXP(Y)+1 ]

	mov.l		SGN(%a6),%d1
	or.l		&0x3F800000,%d1		# SGN
	fmov.s		%d1,%fp0		# SGN IN SGL FMT

	fmov.l		%d0,%fpcr		# restore users round prec,mode
	mov.b		&FADD_OP,%d1		# last inst is ADD
	fadd.x		%fp1,%fp0
	bra		t_inx2

TANHSM:
	fmov.l		%d0,%fpcr		# restore users round prec,mode
	mov.b		&FMOV_OP,%d1		# last inst is MOVE
	fmov.x		X(%a6),%fp0		# last inst - possible exception set
	bra		t_catch

#---RETURN SGN(X) - SGN(X)EPS
TANHHUGE:
	mov.l		X(%a6),%d1
	and.l		&0x80000000,%d1
	or.l		&0x3F800000,%d1
	fmov.s		%d1,%fp0
	and.l		&0x80000000,%d1
	eor.l		&0x80800000,%d1		# -SIGN(X)*EPS

	fmov.l		%d0,%fpcr		# restore users round prec,mode
	fadd.s		%d1,%fp0
	bra		t_inx2

	global		stanhd
#--TANH(X) = X FOR DENORMALIZED X
stanhd:
	bra		t_extdnrm

#########################################################################
# slogn():    computes the natural logarithm of a normalized input	#
# slognd():   computes the natural logarithm of a denormalized input	#
# slognp1():  computes the log(1+X) of a normalized input		#
# slognp1d(): computes the log(1+X) of a denormalized input		#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT **************************************************************	#
#	fp0 = log(X) or log(1+X)					#
#									#
# ACCURACY and MONOTONICITY *******************************************	#
#	The returned result is within 2 ulps in 64 significant bit,	#
#	i.e. within 0.5001 ulp to 53 bits if the result is subsequently	#
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM ***********************************************************	#
#	LOGN:								#
#	Step 1. If |X-1| < 1/16, approximate log(X) by an odd		#
#		polynomial in u, where u = 2(X-1)/(X+1). Otherwise,	#
#		move on to Step 2.					#
#									#
#	Step 2. X = 2**k * Y where 1 <= Y < 2. Define F to be the first	#
#		seven significant bits of Y plus 2**(-7), i.e.		#
#		F = 1.xxxxxx1 in base 2 where the six "x" match those	#
#		of Y. Note that |Y-F| <= 2**(-7).			#
#									#
#	Step 3. Define u = (Y-F)/F. Approximate log(1+u) by a		#
#		polynomial in u, log(1+u) = poly.			#
#									#
#	Step 4. Reconstruct						#
#		log(X) = log( 2**k * Y ) = k*log(2) + log(F) + log(1+u)	#
#		by k*log(2) + (log(F) + poly). The values of log(F) are	#
#		calculated beforehand and stored in the program.	#
#									#
#	lognp1:								#
#	Step 1: If |X| < 1/16, approximate log(1+X) by an odd		#
#		polynomial in u where u = 2X/(2+X). Otherwise, move on	#
#		to Step 2.						#
#									#
#	Step 2: Let 1+X = 2**k * Y, where 1 <= Y < 2. Define F as done	#
#		in Step 2 of the algorithm for LOGN and compute		#
#		log(1+X) as k*log(2) + log(F) + poly where poly		#
#		approximates log(1+u), u = (Y-F)/F.			#
#									#
#	Implementation Notes:						#
#	Note 1. There are 64 different possible values for F, thus 64	#
#		log(F)'s need to be tabulated. Moreover, the values of	#
#		1/F are also tabulated so that the division in (Y-F)/F	#
#		can be performed by a multiplication.			#
#									#
#	Note 2. In Step 2 of lognp1, in order to preserved accuracy,	#
#		the value Y-F has to be calculated carefully when	#
#		1/2 <= X < 3/2.						#
#									#
#	Note 3. To fully exploit the pipeline, polynomials are usually	#
#		separated into two parts evaluated independently before	#
#		being added up.						#
#									#
#########################################################################
LOGOF2:
	long		0x3FFE0000,0xB17217F7,0xD1CF79AC,0x00000000

one:
	long		0x3F800000
zero:
	long		0x00000000
infty:
	long		0x7F800000
negone:
	long		0xBF800000

LOGA6:
	long		0x3FC2499A,0xB5E4040B
LOGA5:
	long		0xBFC555B5,0x848CB7DB

LOGA4:
	long		0x3FC99999,0x987D8730
LOGA3:
	long		0xBFCFFFFF,0xFF6F7E97

LOGA2:
	long		0x3FD55555,0x555555A4
LOGA1:
	long		0xBFE00000,0x00000008

LOGB5:
	long		0x3F175496,0xADD7DAD6
LOGB4:
	long		0x3F3C71C2,0xFE80C7E0

LOGB3:
	long		0x3F624924,0x928BCCFF
LOGB2:
	long		0x3F899999,0x999995EC

LOGB1:
	long		0x3FB55555,0x55555555
TWO:
	long		0x40000000,0x00000000

LTHOLD:
	long		0x3f990000,0x80000000,0x00000000,0x00000000

LOGTBL:
	long		0x3FFE0000,0xFE03F80F,0xE03F80FE,0x00000000
	long		0x3FF70000,0xFF015358,0x833C47E2,0x00000000
	long		0x3FFE0000,0xFA232CF2,0x52138AC0,0x00000000
	long		0x3FF90000,0xBDC8D83E,0xAD88D549,0x00000000
	long		0x3FFE0000,0xF6603D98,0x0F6603DA,0x00000000
	long		0x3FFA0000,0x9CF43DCF,0xF5EAFD48,0x00000000
	long		0x3FFE0000,0xF2B9D648,0x0F2B9D65,0x00000000
	long		0x3FFA0000,0xDA16EB88,0xCB8DF614,0x00000000
	long		0x3FFE0000,0xEF2EB71F,0xC4345238,0x00000000
	long		0x3FFB0000,0x8B29B775,0x1BD70743,0x00000000
	long		0x3FFE0000,0xEBBDB2A5,0xC1619C8C,0x00000000
	long		0x3FFB0000,0xA8D839F8,0x30C1FB49,0x00000000
	long		0x3FFE0000,0xE865AC7B,0x7603A197,0x00000000
	long		0x3FFB0000,0xC61A2EB1,0x8CD907AD,0x00000000
	long		0x3FFE0000,0xE525982A,0xF70C880E,0x00000000
	long		0x3FFB0000,0xE2F2A47A,0xDE3A18AF,0x00000000
	long		0x3FFE0000,0xE1FC780E,0x1FC780E2,0x00000000
	long		0x3FFB0000,0xFF64898E,0xDF55D551,0x00000000
	long		0x3FFE0000,0xDEE95C4C,0xA037BA57,0x00000000
	long		0x3FFC0000,0x8DB956A9,0x7B3D0148,0x00000000
	long		0x3FFE0000,0xDBEB61EE,0xD19C5958,0x00000000
	long		0x3FFC0000,0x9B8FE100,0xF47BA1DE,0x00000000
	long		0x3FFE0000,0xD901B203,0x6406C80E,0x00000000
	long		0x3FFC0000,0xA9372F1D,0x0DA1BD17,0x00000000
	long		0x3FFE0000,0xD62B80D6,0x2B80D62C,0x00000000
	long		0x3FFC0000,0xB6B07F38,0xCE90E46B,0x00000000
	long		0x3FFE0000,0xD3680D36,0x80D3680D,0x00000000
	long		0x3FFC0000,0xC3FD0329,0x06488481,0x00000000
	long		0x3FFE0000,0xD0B69FCB,0xD2580D0B,0x00000000
	long		0x3FFC0000,0xD11DE0FF,0x15AB18CA,0x00000000
	long		0x3FFE0000,0xCE168A77,0x25080CE1,0x00000000
	long		0x3FFC0000,0xDE1433A1,0x6C66B150,0x00000000
	long		0x3FFE0000,0xCB8727C0,0x65C393E0,0x00000000
	long		0x3FFC0000,0xEAE10B5A,0x7DDC8ADD,0x00000000
	long		0x3FFE0000,0xC907DA4E,0x871146AD,0x00000000
	long		0x3FFC0000,0xF7856E5E,0xE2C9B291,0x00000000
	long		0x3FFE0000,0xC6980C69,0x80C6980C,0x00000000
	long		0x3FFD0000,0x82012CA5,0xA68206D7,0x00000000
	long		0x3FFE0000,0xC4372F85,0x5D824CA6,0x00000000
	long		0x3FFD0000,0x882C5FCD,0x7256A8C5,0x00000000
	long		0x3FFE0000,0xC1E4BBD5,0x95F6E947,0x00000000
	long		0x3FFD0000,0x8E44C60B,0x4CCFD7DE,0x00000000
	long		0x3FFE0000,0xBFA02FE8,0x0BFA02FF,0x00000000
	long		0x3FFD0000,0x944AD09E,0xF4351AF6,0x00000000
	long		0x3FFE0000,0xBD691047,0x07661AA3,0x00000000
	long		0x3FFD0000,0x9A3EECD4,0xC3EAA6B2,0x00000000
	long		0x3FFE0000,0xBB3EE721,0xA54D880C,0x00000000
	long		0x3FFD0000,0xA0218434,0x353F1DE8,0x00000000
	long		0x3FFE0000,0xB92143FA,0x36F5E02E,0x00000000
	long		0x3FFD0000,0xA5F2FCAB,0xBBC506DA,0x00000000
	long		0x3FFE0000,0xB70FBB5A,0x19BE3659,0x00000000
	long		0x3FFD0000,0xABB3B8BA,0x2AD362A5,0x00000000
	long		0x3FFE0000,0xB509E68A,0x9B94821F,0x00000000
	long		0x3FFD0000,0xB1641795,0xCE3CA97B,0x00000000
	long		0x3FFE0000,0xB30F6352,0x8917C80B,0x00000000
	long		0x3FFD0000,0xB7047551,0x5D0F1C61,0x00000000
	long		0x3FFE0000,0xB11FD3B8,0x0B11FD3C,0x00000000
	long		0x3FFD0000,0xBC952AFE,0xEA3D13E1,0x00000000
	long		0x3FFE0000,0xAF3ADDC6,0x80AF3ADE,0x00000000
	long		0x3FFD0000,0xC2168ED0,0xF458BA4A,0x00000000
	long		0x3FFE0000,0xAD602B58,0x0AD602B6,0x00000000
	long		0x3FFD0000,0xC788F439,0xB3163BF1,0x00000000
	long		0x3FFE0000,0xAB8F69E2,0x8359CD11,0x00000000
	long		0x3FFD0000,0xCCECAC08,0xBF04565D,0x00000000
	long		0x3FFE0000,0xA9C84A47,0xA07F5638,0x00000000
	long		0x3FFD0000,0xD2420487,0x2DD85160,0x00000000
	long		0x3FFE0000,0xA80A80A8,0x0A80A80B,0x00000000
	long		0x3FFD0000,0xD7894992,0x3BC3588A,0x00000000
	long		0x3FFE0000,0xA655C439,0x2D7B73A8,0x00000000
	long		0x3FFD0000,0xDCC2C4B4,0x9887DACC,0x00000000
	long		0x3FFE0000,0xA4A9CF1D,0x96833751,0x00000000
	long		0x3FFD0000,0xE1EEBD3E,0x6D6A6B9E,0x00000000
	long		0x3FFE0000,0xA3065E3F,0xAE7CD0E0,0x00000000
	long		0x3FFD0000,0xE70D785C,0x2F9F5BDC,0x00000000
	long		0x3FFE0000,0xA16B312E,0xA8FC377D,0x00000000
	long		0x3FFD0000,0xEC1F392C,0x5179F283,0x00000000
	long		0x3FFE0000,0x9FD809FD,0x809FD80A,0x00000000
	long		0x3FFD0000,0xF12440D3,0xE36130E6,0x00000000
	long		0x3FFE0000,0x9E4CAD23,0xDD5F3A20,0x00000000
	long		0x3FFD0000,0xF61CCE92,0x346600BB,0x00000000
	long		0x3FFE0000,0x9CC8E160,0xC3FB19B9,0x00000000
	long		0x3FFD0000,0xFB091FD3,0x8145630A,0x00000000
	long		0x3FFE0000,0x9B4C6F9E,0xF03A3CAA,0x00000000
	long		0x3FFD0000,0xFFE97042,0xBFA4C2AD,0x00000000
	long		0x3FFE0000,0x99D722DA,0xBDE58F06,0x00000000
	long		0x3FFE0000,0x825EFCED,0x49369330,0x00000000
	long		0x3FFE0000,0x9868C809,0x868C8098,0x00000000
	long		0x3FFE0000,0x84C37A7A,0xB9A905C9,0x00000000
	long		0x3FFE0000,0x97012E02,0x5C04B809,0x00000000
	long		0x3FFE0000,0x87224C2E,0x8E645FB7,0x00000000
	long		0x3FFE0000,0x95A02568,0x095A0257,0x00000000
	long		0x3FFE0000,0x897B8CAC,0x9F7DE298,0x00000000
	long		0x3FFE0000,0x94458094,0x45809446,0x00000000
	long		0x3FFE0000,0x8BCF55DE,0xC4CD05FE,0x00000000
	long		0x3FFE0000,0x92F11384,0x0497889C,0x00000000
	long		0x3FFE0000,0x8E1DC0FB,0x89E125E5,0x00000000
	long		0x3FFE0000,0x91A2B3C4,0xD5E6F809,0x00000000
	long		0x3FFE0000,0x9066E68C,0x955B6C9B,0x00000000
	long		0x3FFE0000,0x905A3863,0x3E06C43B,0x00000000
	long		0x3FFE0000,0x92AADE74,0xC7BE59E0,0x00000000
	long		0x3FFE0000,0x8F1779D9,0xFDC3A219,0x00000000
	long		0x3FFE0000,0x94E9BFF6,0x15845643,0x00000000
	long		0x3FFE0000,0x8DDA5202,0x37694809,0x00000000
	long		0x3FFE0000,0x9723A1B7,0x20134203,0x00000000
	long		0x3FFE0000,0x8CA29C04,0x6514E023,0x00000000
	long		0x3FFE0000,0x995899C8,0x90EB8990,0x00000000
	long		0x3FFE0000,0x8B70344A,0x139BC75A,0x00000000
	long		0x3FFE0000,0x9B88BDAA,0x3A3DAE2F,0x00000000
	long		0x3FFE0000,0x8A42F870,0x5669DB46,0x00000000
	long		0x3FFE0000,0x9DB4224F,0xFFE1157C,0x00000000
	long		0x3FFE0000,0x891AC73A,0xE9819B50,0x00000000
	long		0x3FFE0000,0x9FDADC26,0x8B7A12DA,0x00000000
	long		0x3FFE0000,0x87F78087,0xF78087F8,0x00000000
	long		0x3FFE0000,0xA1FCFF17,0xCE733BD4,0x00000000
	long		0x3FFE0000,0x86D90544,0x7A34ACC6,0x00000000
	long		0x3FFE0000,0xA41A9E8F,0x5446FB9F,0x00000000
	long		0x3FFE0000,0x85BF3761,0x2CEE3C9B,0x00000000
	long		0x3FFE0000,0xA633CD7E,0x6771CD8B,0x00000000
	long		0x3FFE0000,0x84A9F9C8,0x084A9F9D,0x00000000
	long		0x3FFE0000,0xA8489E60,0x0B435A5E,0x00000000
	long		0x3FFE0000,0x83993052,0x3FBE3368,0x00000000
	long		0x3FFE0000,0xAA59233C,0xCCA4BD49,0x00000000
	long		0x3FFE0000,0x828CBFBE,0xB9A020A3,0x00000000
	long		0x3FFE0000,0xAC656DAE,0x6BCC4985,0x00000000
	long		0x3FFE0000,0x81848DA8,0xFAF0D277,0x00000000
	long		0x3FFE0000,0xAE6D8EE3,0x60BB2468,0x00000000
	long		0x3FFE0000,0x80808080,0x80808081,0x00000000
	long		0x3FFE0000,0xB07197A2,0x3C46C654,0x00000000

	set		ADJK,L_SCR1

	set		X,FP_SCR0
	set		XDCARE,X+2
	set		XFRAC,X+4

	set		F,FP_SCR1
	set		FFRAC,F+4

	set		KLOG2,FP_SCR0

	set		SAVEU,FP_SCR0

	global		slogn
#--ENTRY POINT FOR LOG(X) FOR X FINITE, NON-ZERO, NOT NAN'S
slogn:
	fmov.x		(%a0),%fp0		# LOAD INPUT
	mov.l		&0x00000000,ADJK(%a6)

LOGBGN:
#--FPCR SAVED AND CLEARED, INPUT IS 2^(ADJK)*FP0, FP0 CONTAINS
#--A FINITE, NON-ZERO, NORMALIZED NUMBER.

	mov.l		(%a0),%d1
	mov.w		4(%a0),%d1

	mov.l		(%a0),X(%a6)
	mov.l		4(%a0),X+4(%a6)
	mov.l		8(%a0),X+8(%a6)

	cmp.l		%d1,&0			# CHECK IF X IS NEGATIVE
	blt.w		LOGNEG			# LOG OF NEGATIVE ARGUMENT IS INVALID
# X IS POSITIVE, CHECK IF X IS NEAR 1
	cmp.l		%d1,&0x3ffef07d		# IS X < 15/16?
	blt.b		LOGMAIN			# YES
	cmp.l		%d1,&0x3fff8841		# IS X > 17/16?
	ble.w		LOGNEAR1		# NO

LOGMAIN:
#--THIS SHOULD BE THE USUAL CASE, X NOT VERY CLOSE TO 1

#--X = 2^(K) * Y, 1 <= Y < 2. THUS, Y = 1.XXXXXXXX....XX IN BINARY.
#--WE DEFINE F = 1.XXXXXX1, I.E. FIRST 7 BITS OF Y AND ATTACH A 1.
#--THE IDEA IS THAT LOG(X) = K*LOG2 + LOG(Y)
#--			 = K*LOG2 + LOG(F) + LOG(1 + (Y-F)/F).
#--NOTE THAT U = (Y-F)/F IS VERY SMALL AND THUS APPROXIMATING
#--LOG(1+U) CAN BE VERY EFFICIENT.
#--ALSO NOTE THAT THE VALUE 1/F IS STORED IN A TABLE SO THAT NO
#--DIVISION IS NEEDED TO CALCULATE (Y-F)/F.

#--GET K, Y, F, AND ADDRESS OF 1/F.
	asr.l		&8,%d1
	asr.l		&8,%d1			# SHIFTED 16 BITS, BIASED EXPO. OF X
	sub.l		&0x3FFF,%d1		# THIS IS K
	add.l		ADJK(%a6),%d1		# ADJUST K, ORIGINAL INPUT MAY BE  DENORM.
	lea		LOGTBL(%pc),%a0		# BASE ADDRESS OF 1/F AND LOG(F)
	fmov.l		%d1,%fp1		# CONVERT K TO FLOATING-POINT FORMAT

#--WHILE THE CONVERSION IS GOING ON, WE GET F AND ADDRESS OF 1/F
	mov.l		&0x3FFF0000,X(%a6)	# X IS NOW Y, I.E. 2^(-K)*X
	mov.l		XFRAC(%a6),FFRAC(%a6)
	and.l		&0xFE000000,FFRAC(%a6)	# FIRST 7 BITS OF Y
	or.l		&0x01000000,FFRAC(%a6)	# GET F: ATTACH A 1 AT THE EIGHTH BIT
	mov.l		FFRAC(%a6),%d1	# READY TO GET ADDRESS OF 1/F
	and.l		&0x7E000000,%d1
	asr.l		&8,%d1
	asr.l		&8,%d1
	asr.l		&4,%d1			# SHIFTED 20, D0 IS THE DISPLACEMENT
	add.l		%d1,%a0			# A0 IS THE ADDRESS FOR 1/F

	fmov.x		X(%a6),%fp0
	mov.l		&0x3fff0000,F(%a6)
	clr.l		F+8(%a6)
	fsub.x		F(%a6),%fp0		# Y-F
	fmovm.x		&0xc,-(%sp)		# SAVE FP2-3 WHILE FP0 IS NOT READY
#--SUMMARY: FP0 IS Y-F, A0 IS ADDRESS OF 1/F, FP1 IS K
#--REGISTERS SAVED: FPCR, FP1, FP2

LP1CONT1:
#--AN RE-ENTRY POINT FOR LOGNP1
	fmul.x		(%a0),%fp0		# FP0 IS U = (Y-F)/F
	fmul.x		LOGOF2(%pc),%fp1	# GET K*LOG2 WHILE FP0 IS NOT READY
	fmov.x		%fp0,%fp2
	fmul.x		%fp2,%fp2		# FP2 IS V=U*U
	fmov.x		%fp1,KLOG2(%a6)		# PUT K*LOG2 IN MEMEORY, FREE FP1

#--LOG(1+U) IS APPROXIMATED BY
#--U + V*(A1+U*(A2+U*(A3+U*(A4+U*(A5+U*A6))))) WHICH IS
#--[U + V*(A1+V*(A3+V*A5))]  +  [U*V*(A2+V*(A4+V*A6))]

	fmov.x		%fp2,%fp3
	fmov.x		%fp2,%fp1

	fmul.d		LOGA6(%pc),%fp1		# V*A6
	fmul.d		LOGA5(%pc),%fp2		# V*A5

	fadd.d		LOGA4(%pc),%fp1		# A4+V*A6
	fadd.d		LOGA3(%pc),%fp2		# A3+V*A5

	fmul.x		%fp3,%fp1		# V*(A4+V*A6)
	fmul.x		%fp3,%fp2		# V*(A3+V*A5)

	fadd.d		LOGA2(%pc),%fp1		# A2+V*(A4+V*A6)
	fadd.d		LOGA1(%pc),%fp2		# A1+V*(A3+V*A5)

	fmul.x		%fp3,%fp1		# V*(A2+V*(A4+V*A6))
	add.l		&16,%a0			# ADDRESS OF LOG(F)
	fmul.x		%fp3,%fp2		# V*(A1+V*(A3+V*A5))

	fmul.x		%fp0,%fp1		# U*V*(A2+V*(A4+V*A6))
	fadd.x		%fp2,%fp0		# U+V*(A1+V*(A3+V*A5))

	fadd.x		(%a0),%fp1		# LOG(F)+U*V*(A2+V*(A4+V*A6))
	fmovm.x		(%sp)+,&0x30		# RESTORE FP2-3
	fadd.x		%fp1,%fp0		# FP0 IS LOG(F) + LOG(1+U)

	fmov.l		%d0,%fpcr
	fadd.x		KLOG2(%a6),%fp0		# FINAL ADD
	bra		t_inx2


LOGNEAR1:

# if the input is exactly equal to one, then exit through ld_pzero.
# if these 2 lines weren't here, the correct answer would be returned
# but the INEX2 bit would be set.
	fcmp.b		%fp0,&0x1		# is it equal to one?
	fbeq.l		ld_pzero		# yes

#--REGISTERS SAVED: FPCR, FP1. FP0 CONTAINS THE INPUT.
	fmov.x		%fp0,%fp1
	fsub.s		one(%pc),%fp1		# FP1 IS X-1
	fadd.s		one(%pc),%fp0		# FP0 IS X+1
	fadd.x		%fp1,%fp1		# FP1 IS 2(X-1)
#--LOG(X) = LOG(1+U/2)-LOG(1-U/2) WHICH IS AN ODD POLYNOMIAL
#--IN U, U = 2(X-1)/(X+1) = FP1/FP0

LP1CONT2:
#--THIS IS AN RE-ENTRY POINT FOR LOGNP1
	fdiv.x		%fp0,%fp1		# FP1 IS U
	fmovm.x		&0xc,-(%sp)		# SAVE FP2-3
#--REGISTERS SAVED ARE NOW FPCR,FP1,FP2,FP3
#--LET V=U*U, W=V*V, CALCULATE
#--U + U*V*(B1 + V*(B2 + V*(B3 + V*(B4 + V*B5)))) BY
#--U + U*V*(  [B1 + W*(B3 + W*B5)]  +  [V*(B2 + W*B4)]  )
	fmov.x		%fp1,%fp0
	fmul.x		%fp0,%fp0		# FP0 IS V
	fmov.x		%fp1,SAVEU(%a6)		# STORE U IN MEMORY, FREE FP1
	fmov.x		%fp0,%fp1
	fmul.x		%fp1,%fp1		# FP1 IS W

	fmov.d		LOGB5(%pc),%fp3
	fmov.d		LOGB4(%pc),%fp2

	fmul.x		%fp1,%fp3		# W*B5
	fmul.x		%fp1,%fp2		# W*B4

	fadd.d		LOGB3(%pc),%fp3		# B3+W*B5
	fadd.d		LOGB2(%pc),%fp2		# B2+W*B4

	fmul.x		%fp3,%fp1		# W*(B3+W*B5), FP3 RELEASED

	fmul.x		%fp0,%fp2		# V*(B2+W*B4)

	fadd.d		LOGB1(%pc),%fp1		# B1+W*(B3+W*B5)
	fmul.x		SAVEU(%a6),%fp0		# FP0 IS U*V

	fadd.x		%fp2,%fp1		# B1+W*(B3+W*B5) + V*(B2+W*B4), FP2 RELEASED
	fmovm.x		(%sp)+,&0x30		# FP2-3 RESTORED

	fmul.x		%fp1,%fp0		# U*V*( [B1+W*(B3+W*B5)] + [V*(B2+W*B4)] )

	fmov.l		%d0,%fpcr
	fadd.x		SAVEU(%a6),%fp0
	bra		t_inx2

#--REGISTERS SAVED FPCR. LOG(-VE) IS INVALID
LOGNEG:
	bra		t_operr

	global		slognd
slognd:
#--ENTRY POINT FOR LOG(X) FOR DENORMALIZED INPUT

	mov.l		&-100,ADJK(%a6)		# INPUT = 2^(ADJK) * FP0

#----normalize the input value by left shifting k bits (k to be determined
#----below), adjusting exponent and storing -k to  ADJK
#----the value TWOTO100 is no longer needed.
#----Note that this code assumes the denormalized input is NON-ZERO.

	movm.l		&0x3f00,-(%sp)		# save some registers  {d2-d7}
	mov.l		(%a0),%d3		# D3 is exponent of smallest norm. #
	mov.l		4(%a0),%d4
	mov.l		8(%a0),%d5		# (D4,D5) is (Hi_X,Lo_X)
	clr.l		%d2			# D2 used for holding K

	tst.l		%d4
	bne.b		Hi_not0

Hi_0:
	mov.l		%d5,%d4
	clr.l		%d5
	mov.l		&32,%d2
	clr.l		%d6
	bfffo		%d4{&0:&32},%d6
	lsl.l		%d6,%d4
	add.l		%d6,%d2			# (D3,D4,D5) is normalized

	mov.l		%d3,X(%a6)
	mov.l		%d4,XFRAC(%a6)
	mov.l		%d5,XFRAC+4(%a6)
	neg.l		%d2
	mov.l		%d2,ADJK(%a6)
	fmov.x		X(%a6),%fp0
	movm.l		(%sp)+,&0xfc		# restore registers {d2-d7}
	lea		X(%a6),%a0
	bra.w		LOGBGN			# begin regular log(X)

Hi_not0:
	clr.l		%d6
	bfffo		%d4{&0:&32},%d6		# find first 1
	mov.l		%d6,%d2			# get k
	lsl.l		%d6,%d4
	mov.l		%d5,%d7			# a copy of D5
	lsl.l		%d6,%d5
	neg.l		%d6
	add.l		&32,%d6
	lsr.l		%d6,%d7
	or.l		%d7,%d4			# (D3,D4,D5) normalized

	mov.l		%d3,X(%a6)
	mov.l		%d4,XFRAC(%a6)
	mov.l		%d5,XFRAC+4(%a6)
	neg.l		%d2
	mov.l		%d2,ADJK(%a6)
	fmov.x		X(%a6),%fp0
	movm.l		(%sp)+,&0xfc		# restore registers {d2-d7}
	lea		X(%a6),%a0
	bra.w		LOGBGN			# begin regular log(X)

	global		slognp1
#--ENTRY POINT FOR LOG(1+X) FOR X FINITE, NON-ZERO, NOT NAN'S
slognp1:
	fmov.x		(%a0),%fp0		# LOAD INPUT
	fabs.x		%fp0			# test magnitude
	fcmp.x		%fp0,LTHOLD(%pc)	# compare with min threshold
	fbgt.w		LP1REAL			# if greater, continue
	fmov.l		%d0,%fpcr
	mov.b		&FMOV_OP,%d1		# last inst is MOVE
	fmov.x		(%a0),%fp0		# return signed argument
	bra		t_catch

LP1REAL:
	fmov.x		(%a0),%fp0		# LOAD INPUT
	mov.l		&0x00000000,ADJK(%a6)
	fmov.x		%fp0,%fp1		# FP1 IS INPUT Z
	fadd.s		one(%pc),%fp0		# X := ROUND(1+Z)
	fmov.x		%fp0,X(%a6)
	mov.w		XFRAC(%a6),XDCARE(%a6)
	mov.l		X(%a6),%d1
	cmp.l		%d1,&0
	ble.w		LP1NEG0			# LOG OF ZERO OR -VE
	cmp.l		%d1,&0x3ffe8000		# IS BOUNDS [1/2,3/2]?
	blt.w		LOGMAIN
	cmp.l		%d1,&0x3fffc000
	bgt.w		LOGMAIN
#--IF 1+Z > 3/2 OR 1+Z < 1/2, THEN X, WHICH IS ROUNDING 1+Z,
#--CONTAINS AT LEAST 63 BITS OF INFORMATION OF Z. IN THAT CASE,
#--SIMPLY INVOKE LOG(X) FOR LOG(1+Z).

LP1NEAR1:
#--NEXT SEE IF EXP(-1/16) < X < EXP(1/16)
	cmp.l		%d1,&0x3ffef07d
	blt.w		LP1CARE
	cmp.l		%d1,&0x3fff8841
	bgt.w		LP1CARE

LP1ONE16:
#--EXP(-1/16) < X < EXP(1/16). LOG(1+Z) = LOG(1+U/2) - LOG(1-U/2)
#--WHERE U = 2Z/(2+Z) = 2Z/(1+X).
	fadd.x		%fp1,%fp1		# FP1 IS 2Z
	fadd.s		one(%pc),%fp0		# FP0 IS 1+X
#--U = FP1/FP0
	bra.w		LP1CONT2

LP1CARE:
#--HERE WE USE THE USUAL TABLE DRIVEN APPROACH. CARE HAS TO BE
#--TAKEN BECAUSE 1+Z CAN HAVE 67 BITS OF INFORMATION AND WE MUST
#--PRESERVE ALL THE INFORMATION. BECAUSE 1+Z IS IN [1/2,3/2],
#--THERE ARE ONLY TWO CASES.
#--CASE 1: 1+Z < 1, THEN K = -1 AND Y-F = (2-F) + 2Z
#--CASE 2: 1+Z > 1, THEN K = 0  AND Y-F = (1-F) + Z
#--ON RETURNING TO LP1CONT1, WE MUST HAVE K IN FP1, ADDRESS OF
#--(1/F) IN A0, Y-F IN FP0, AND FP2 SAVED.

	mov.l		XFRAC(%a6),FFRAC(%a6)
	and.l		&0xFE000000,FFRAC(%a6)
	or.l		&0x01000000,FFRAC(%a6)	# F OBTAINED
	cmp.l		%d1,&0x3FFF8000		# SEE IF 1+Z > 1
	bge.b		KISZERO

KISNEG1:
	fmov.s		TWO(%pc),%fp0
	mov.l		&0x3fff0000,F(%a6)
	clr.l		F+8(%a6)
	fsub.x		F(%a6),%fp0		# 2-F
	mov.l		FFRAC(%a6),%d1
	and.l		&0x7E000000,%d1
	asr.l		&8,%d1
	asr.l		&8,%d1
	asr.l		&4,%d1			# D0 CONTAINS DISPLACEMENT FOR 1/F
	fadd.x		%fp1,%fp1		# GET 2Z
	fmovm.x		&0xc,-(%sp)		# SAVE FP2  {%fp2/%fp3}
	fadd.x		%fp1,%fp0		# FP0 IS Y-F = (2-F)+2Z
	lea		LOGTBL(%pc),%a0		# A0 IS ADDRESS OF 1/F
	add.l		%d1,%a0
	fmov.s		negone(%pc),%fp1	# FP1 IS K = -1
	bra.w		LP1CONT1

KISZERO:
	fmov.s		one(%pc),%fp0
	mov.l		&0x3fff0000,F(%a6)
	clr.l		F+8(%a6)
	fsub.x		F(%a6),%fp0		# 1-F
	mov.l		FFRAC(%a6),%d1
	and.l		&0x7E000000,%d1
	asr.l		&8,%d1
	asr.l		&8,%d1
	asr.l		&4,%d1
	fadd.x		%fp1,%fp0		# FP0 IS Y-F
	fmovm.x		&0xc,-(%sp)		# FP2 SAVED {%fp2/%fp3}
	lea		LOGTBL(%pc),%a0
	add.l		%d1,%a0			# A0 IS ADDRESS OF 1/F
	fmov.s		zero(%pc),%fp1		# FP1 IS K = 0
	bra.w		LP1CONT1

LP1NEG0:
#--FPCR SAVED. D0 IS X IN COMPACT FORM.
	cmp.l		%d1,&0
	blt.b		LP1NEG
LP1ZERO:
	fmov.s		negone(%pc),%fp0

	fmov.l		%d0,%fpcr
	bra		t_dz

LP1NEG:
	fmov.s		zero(%pc),%fp0

	fmov.l		%d0,%fpcr
	bra		t_operr

	global		slognp1d
#--ENTRY POINT FOR LOG(1+Z) FOR DENORMALIZED INPUT
# Simply return the denorm
slognp1d:
	bra		t_extdnrm

#########################################################################
# satanh():  computes the inverse hyperbolic tangent of a norm input	#
# satanhd(): computes the inverse hyperbolic tangent of a denorm input	#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT **************************************************************	#
#	fp0 = arctanh(X)						#
#									#
# ACCURACY and MONOTONICITY *******************************************	#
#	The returned result is within 3 ulps in	64 significant bit,	#
#	i.e. within 0.5001 ulp to 53 bits if the result is subsequently	#
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM ***********************************************************	#
#									#
#	ATANH								#
#	1. If |X| >= 1, go to 3.					#
#									#
#	2. (|X| < 1) Calculate atanh(X) by				#
#		sgn := sign(X)						#
#		y := |X|						#
#		z := 2y/(1-y)						#
#		atanh(X) := sgn * (1/2) * logp1(z)			#
#		Exit.							#
#									#
#	3. If |X| > 1, go to 5.						#
#									#
#	4. (|X| = 1) Generate infinity with an appropriate sign and	#
#		divide-by-zero by					#
#		sgn := sign(X)						#
#		atan(X) := sgn / (+0).					#
#		Exit.							#
#									#
#	5. (|X| > 1) Generate an invalid operation by 0 * infinity.	#
#		Exit.							#
#									#
#########################################################################

	global		satanh
satanh:
	mov.l		(%a0),%d1
	mov.w		4(%a0),%d1
	and.l		&0x7FFFFFFF,%d1
	cmp.l		%d1,&0x3FFF8000
	bge.b		ATANHBIG

#--THIS IS THE USUAL CASE, |X| < 1
#--Y = |X|, Z = 2Y/(1-Y), ATANH(X) = SIGN(X) * (1/2) * LOG1P(Z).

	fabs.x		(%a0),%fp0		# Y = |X|
	fmov.x		%fp0,%fp1
	fneg.x		%fp1			# -Y
	fadd.x		%fp0,%fp0		# 2Y
	fadd.s		&0x3F800000,%fp1	# 1-Y
	fdiv.x		%fp1,%fp0		# 2Y/(1-Y)
	mov.l		(%a0),%d1
	and.l		&0x80000000,%d1
	or.l		&0x3F000000,%d1		# SIGN(X)*HALF
	mov.l		%d1,-(%sp)

	mov.l		%d0,-(%sp)		# save rnd prec,mode
	clr.l		%d0			# pass ext prec,RN
	fmovm.x		&0x01,-(%sp)		# save Z on stack
	lea		(%sp),%a0		# pass ptr to Z
	bsr		slognp1			# LOG1P(Z)
	add.l		&0xc,%sp		# clear Z from stack

	mov.l		(%sp)+,%d0		# fetch old prec,mode
	fmov.l		%d0,%fpcr		# load it
	mov.b		&FMUL_OP,%d1		# last inst is MUL
	fmul.s		(%sp)+,%fp0
	bra		t_catch

ATANHBIG:
	fabs.x		(%a0),%fp0		# |X|
	fcmp.s		%fp0,&0x3F800000
	fbgt		t_operr
	bra		t_dz

	global		satanhd
#--ATANH(X) = X FOR DENORMALIZED X
satanhd:
	bra		t_extdnrm

#########################################################################
# slog10():  computes the base-10 logarithm of a normalized input	#
# slog10d(): computes the base-10 logarithm of a denormalized input	#
# slog2():   computes the base-2 logarithm of a normalized input	#
# slog2d():  computes the base-2 logarithm of a denormalized input	#
#									#
# INPUT *************************************************************** #
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT **************************************************************	#
#	fp0 = log_10(X) or log_2(X)					#
#									#
# ACCURACY and MONOTONICITY *******************************************	#
#	The returned result is within 1.7 ulps in 64 significant bit,	#
#	i.e. within 0.5003 ulp to 53 bits if the result is subsequently	#
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM ***********************************************************	#
#									#
#       slog10d:							#
#									#
#       Step 0.	If X < 0, create a NaN and raise the invalid operation	#
#               flag. Otherwise, save FPCR in D1; set FpCR to default.	#
#       Notes:  Default means round-to-nearest mode, no floating-point	#
#               traps, and precision control = double extended.		#
#									#
#       Step 1. Call slognd to obtain Y = log(X), the natural log of X.	#
#       Notes:  Even if X is denormalized, log(X) is always normalized.	#
#									#
#       Step 2.  Compute log_10(X) = log(X) * (1/log(10)).		#
#            2.1 Restore the user FPCR					#
#            2.2 Return ans := Y * INV_L10.				#
#									#
#       slog10:								#
#									#
#       Step 0. If X < 0, create a NaN and raise the invalid operation	#
#               flag. Otherwise, save FPCR in D1; set FpCR to default.	#
#       Notes:  Default means round-to-nearest mode, no floating-point	#
#               traps, and precision control = double extended.		#
#									#
#       Step 1. Call sLogN to obtain Y = log(X), the natural log of X.	#
#									#
#       Step 2.   Compute log_10(X) = log(X) * (1/log(10)).		#
#            2.1  Restore the user FPCR					#
#            2.2  Return ans := Y * INV_L10.				#
#									#
#       sLog2d:								#
#									#
#       Step 0. If X < 0, create a NaN and raise the invalid operation	#
#               flag. Otherwise, save FPCR in D1; set FpCR to default.	#
#       Notes:  Default means round-to-nearest mode, no floating-point	#
#               traps, and precision control = double extended.		#
#									#
#       Step 1. Call slognd to obtain Y = log(X), the natural log of X.	#
#       Notes:  Even if X is denormalized, log(X) is always normalized.	#
#									#
#       Step 2.   Compute log_10(X) = log(X) * (1/log(2)).		#
#            2.1  Restore the user FPCR					#
#            2.2  Return ans := Y * INV_L2.				#
#									#
#       sLog2:								#
#									#
#       Step 0. If X < 0, create a NaN and raise the invalid operation	#
#               flag. Otherwise, save FPCR in D1; set FpCR to default.	#
#       Notes:  Default means round-to-nearest mode, no floating-point	#
#               traps, and precision control = double extended.		#
#									#
#       Step 1. If X is not an integer power of two, i.e., X != 2^k,	#
#               go to Step 3.						#
#									#
#       Step 2.   Return k.						#
#            2.1  Get integer k, X = 2^k.				#
#            2.2  Restore the user FPCR.				#
#            2.3  Return ans := convert-to-double-extended(k).		#
#									#
#       Step 3. Call sLogN to obtain Y = log(X), the natural log of X.	#
#									#
#       Step 4.   Compute log_2(X) = log(X) * (1/log(2)).		#
#            4.1  Restore the user FPCR					#
#            4.2  Return ans := Y * INV_L2.				#
#									#
#########################################################################

INV_L10:
	long		0x3FFD0000,0xDE5BD8A9,0x37287195,0x00000000

INV_L2:
	long		0x3FFF0000,0xB8AA3B29,0x5C17F0BC,0x00000000

	global		slog10
#--entry point for Log10(X), X is normalized
slog10:
	fmov.b		&0x1,%fp0
	fcmp.x		%fp0,(%a0)		# if operand == 1,
	fbeq.l		ld_pzero		# return an EXACT zero

	mov.l		(%a0),%d1
	blt.w		invalid
	mov.l		%d0,-(%sp)
	clr.l		%d0
	bsr		slogn			# log(X), X normal.
	fmov.l		(%sp)+,%fpcr
	fmul.x		INV_L10(%pc),%fp0
	bra		t_inx2

	global		slog10d
#--entry point for Log10(X), X is denormalized
slog10d:
	mov.l		(%a0),%d1
	blt.w		invalid
	mov.l		%d0,-(%sp)
	clr.l		%d0
	bsr		slognd			# log(X), X denorm.
	fmov.l		(%sp)+,%fpcr
	fmul.x		INV_L10(%pc),%fp0
	bra		t_minx2

	global		slog2
#--entry point for Log2(X), X is normalized
slog2:
	mov.l		(%a0),%d1
	blt.w		invalid

	mov.l		8(%a0),%d1
	bne.b		continue		# X is not 2^k

	mov.l		4(%a0),%d1
	and.l		&0x7FFFFFFF,%d1
	bne.b		continue

#--X = 2^k.
	mov.w		(%a0),%d1
	and.l		&0x00007FFF,%d1
	sub.l		&0x3FFF,%d1
	beq.l		ld_pzero
	fmov.l		%d0,%fpcr
	fmov.l		%d1,%fp0
	bra		t_inx2

continue:
	mov.l		%d0,-(%sp)
	clr.l		%d0
	bsr		slogn			# log(X), X normal.
	fmov.l		(%sp)+,%fpcr
	fmul.x		INV_L2(%pc),%fp0
	bra		t_inx2

invalid:
	bra		t_operr

	global		slog2d
#--entry point for Log2(X), X is denormalized
slog2d:
	mov.l		(%a0),%d1
	blt.w		invalid
	mov.l		%d0,-(%sp)
	clr.l		%d0
	bsr		slognd			# log(X), X denorm.
	fmov.l		(%sp)+,%fpcr
	fmul.x		INV_L2(%pc),%fp0
	bra		t_minx2

#########################################################################
# stwotox():  computes 2**X for a normalized input			#
# stwotoxd(): computes 2**X for a denormalized input			#
# stentox():  computes 10**X for a normalized input			#
# stentoxd(): computes 10**X for a denormalized input			#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision input			#
#	d0 = round precision,mode					#
#									#
# OUTPUT **************************************************************	#
#	fp0 = 2**X or 10**X						#
#									#
# ACCURACY and MONOTONICITY *******************************************	#
#	The returned result is within 2 ulps in 64 significant bit,	#
#	i.e. within 0.5001 ulp to 53 bits if the result is subsequently	#
#	rounded to double precision. The result is provably monotonic	#
#	in double precision.						#
#									#
# ALGORITHM ***********************************************************	#
#									#
#	twotox								#
#	1. If |X| > 16480, go to ExpBig.				#
#									#
#	2. If |X| < 2**(-70), go to ExpSm.				#
#									#
#	3. Decompose X as X = N/64 + r where |r| <= 1/128. Furthermore	#
#		decompose N as						#
#		 N = 64(M + M') + j,  j = 0,1,2,...,63.			#
#									#
#	4. Overwrite r := r * log2. Then				#
#		2**X = 2**(M') * 2**(M) * 2**(j/64) * exp(r).		#
#		Go to expr to compute that expression.			#
#									#
#	tentox								#
#	1. If |X| > 16480*log_10(2) (base 10 log of 2), go to ExpBig.	#
#									#
#	2. If |X| < 2**(-70), go to ExpSm.				#
#									#
#	3. Set y := X*log_2(10)*64 (base 2 log of 10). Set		#
#		N := round-to-int(y). Decompose N as			#
#		 N = 64(M + M') + j,  j = 0,1,2,...,63.			#
#									#
#	4. Define r as							#
#		r := ((X - N*L1)-N*L2) * L10				#
#		where L1, L2 are the leading and trailing parts of	#
#		log_10(2)/64 and L10 is the natural log of 10. Then	#
#		10**X = 2**(M') * 2**(M) * 2**(j/64) * exp(r).		#
#		Go to expr to compute that expression.			#
#									#
#	expr								#
#	1. Fetch 2**(j/64) from table as Fact1 and Fact2.		#
#									#
#	2. Overwrite Fact1 and Fact2 by					#
#		Fact1 := 2**(M) * Fact1					#
#		Fact2 := 2**(M) * Fact2					#
#		Thus Fact1 + Fact2 = 2**(M) * 2**(j/64).		#
#									#
#	3. Calculate P where 1 + P approximates exp(r):			#
#		P = r + r*r*(A1+r*(A2+...+r*A5)).			#
#									#
#	4. Let AdjFact := 2**(M'). Return				#
#		AdjFact * ( Fact1 + ((Fact1*P) + Fact2) ).		#
#		Exit.							#
#									#
#	ExpBig								#
#	1. Generate overflow by Huge * Huge if X > 0; otherwise,	#
#	        generate underflow by Tiny * Tiny.			#
#									#
#	ExpSm								#
#	1. Return 1 + X.						#
#									#
#########################################################################

L2TEN64:
	long		0x406A934F,0x0979A371	# 64LOG10/LOG2
L10TWO1:
	long		0x3F734413,0x509F8000	# LOG2/64LOG10

L10TWO2:
	long		0xBFCD0000,0xC0219DC1,0xDA994FD2,0x00000000

LOG10:	long		0x40000000,0x935D8DDD,0xAAA8AC17,0x00000000

LOG2:	long		0x3FFE0000,0xB17217F7,0xD1CF79AC,0x00000000

EXPA5:	long		0x3F56C16D,0x6F7BD0B2
EXPA4:	long		0x3F811112,0x302C712C
EXPA3:	long		0x3FA55555,0x55554CC1
EXPA2:	long		0x3FC55555,0x55554A54
EXPA1:	long		0x3FE00000,0x00000000,0x00000000,0x00000000

TEXPTBL:
	long		0x3FFF0000,0x80000000,0x00000000,0x3F738000
	long		0x3FFF0000,0x8164D1F3,0xBC030773,0x3FBEF7CA
	long		0x3FFF0000,0x82CD8698,0xAC2BA1D7,0x3FBDF8A9
	long		0x3FFF0000,0x843A28C3,0xACDE4046,0x3FBCD7C9
	long		0x3FFF0000,0x85AAC367,0xCC487B15,0xBFBDE8DA
	long		0x3FFF0000,0x871F6196,0x9E8D1010,0x3FBDE85C
	long		0x3FFF0000,0x88980E80,0x92DA8527,0x3FBEBBF1
	long		0x3FFF0000,0x8A14D575,0x496EFD9A,0x3FBB80CA
	long		0x3FFF0000,0x8B95C1E3,0xEA8BD6E7,0xBFBA8373
	long		0x3FFF0000,0x8D1ADF5B,0x7E5BA9E6,0xBFBE9670
	long		0x3FFF0000,0x8EA4398B,0x45CD53C0,0x3FBDB700
	long		0x3FFF0000,0x9031DC43,0x1466B1DC,0x3FBEEEB0
	long		0x3FFF0000,0x91C3D373,0xAB11C336,0x3FBBFD6D
	long		0x3FFF0000,0x935A2B2F,0x13E6E92C,0xBFBDB319
	long		0x3FFF0000,0x94F4EFA8,0xFEF70961,0x3FBDBA2B
	long		0x3FFF0000,0x96942D37,0x20185A00,0x3FBE91D5
	long		0x3FFF0000,0x9837F051,0x8DB8A96F,0x3FBE8D5A
	long		0x3FFF0000,0x99E04593,0x20B7FA65,0xBFBCDE7B
	long		0x3FFF0000,0x9B8D39B9,0xD54E5539,0xBFBEBAAF
	long		0x3FFF0000,0x9D3ED9A7,0x2CFFB751,0xBFBD86DA
	long		0x3FFF0000,0x9EF53260,0x91A111AE,0xBFBEBEDD
	long		0x3FFF0000,0xA0B0510F,0xB9714FC2,0x3FBCC96E
	long		0x3FFF0000,0xA2704303,0x0C496819,0xBFBEC90B
	long		0x3FFF0000,0xA43515AE,0x09E6809E,0x3FBBD1DB
	long		0x3FFF0000,0xA5FED6A9,0xB15138EA,0x3FBCE5EB
	long		0x3FFF0000,0xA7CD93B4,0xE965356A,0xBFBEC274
	long		0x3FFF0000,0xA9A15AB4,0xEA7C0EF8,0x3FBEA83C
	long		0x3FFF0000,0xAB7A39B5,0xA93ED337,0x3FBECB00
	long		0x3FFF0000,0xAD583EEA,0x42A14AC6,0x3FBE9301
	long		0x3FFF0000,0xAF3B78AD,0x690A4375,0xBFBD8367
	long		0x3FFF0000,0xB123F581,0xD2AC2590,0xBFBEF05F
	long		0x3FFF0000,0xB311C412,0xA9112489,0x3FBDFB3C
	long		0x3FFF0000,0xB504F333,0xF9DE6484,0x3FBEB2FB
	long		0x3FFF0000,0xB6FD91E3,0x28D17791,0x3FBAE2CB
	long		0x3FFF0000,0xB8FBAF47,0x62FB9EE9,0x3FBCDC3C
	long		0x3FFF0000,0xBAFF5AB2,0x133E45FB,0x3FBEE9AA
	long		0x3FFF0000,0xBD08A39F,0x580C36BF,0xBFBEAEFD
	long		0x3FFF0000,0xBF1799B6,0x7A731083,0xBFBCBF51
	long		0x3FFF0000,0xC12C4CCA,0x66709456,0x3FBEF88A
	long		0x3FFF0000,0xC346CCDA,0x24976407,0x3FBD83B2
	long		0x3FFF0000,0xC5672A11,0x5506DADD,0x3FBDF8AB
	long		0x3FFF0000,0xC78D74C8,0xABB9B15D,0xBFBDFB17
	long		0x3FFF0000,0xC9B9BD86,0x6E2F27A3,0xBFBEFE3C
	long		0x3FFF0000,0xCBEC14FE,0xF2727C5D,0xBFBBB6F8
	long		0x3FFF0000,0xCE248C15,0x1F8480E4,0xBFBCEE53
	long		0x3FFF0000,0xD06333DA,0xEF2B2595,0xBFBDA4AE
	long		0x3FFF0000,0xD2A81D91,0xF12AE45A,0x3FBC9124
	long		0x3FFF0000,0xD4F35AAB,0xCFEDFA1F,0x3FBEB243
	long		0x3FFF0000,0xD744FCCA,0xD69D6AF4,0x3FBDE69A
	long		0x3FFF0000,0xD99D15C2,0x78AFD7B6,0xBFB8BC61
	long		0x3FFF0000,0xDBFBB797,0xDAF23755,0x3FBDF610
	long		0x3FFF0000,0xDE60F482,0x5E0E9124,0xBFBD8BE1
	long		0x3FFF0000,0xE0CCDEEC,0x2A94E111,0x3FBACB12
	long		0x3FFF0000,0xE33F8972,0xBE8A5A51,0x3FBB9BFE
	long		0x3FFF0000,0xE5B906E7,0x7C8348A8,0x3FBCF2F4
	long		0x3FFF0000,0xE8396A50,0x3C4BDC68,0x3FBEF22F
	long		0x3FFF0000,0xEAC0C6E7,0xDD24392F,0xBFBDBF4A
	long		0x3FFF0000,0xED4F301E,0xD9942B84,0x3FBEC01A
	long		0x3FFF0000,0xEFE4B99B,0xDCDAF5CB,0x3FBE8CAC
	long		0x3FFF0000,0xF281773C,0x59FFB13A,0xBFBCBB3F
	long		0x3FFF0000,0xF5257D15,0x2486CC2C,0x3FBEF73A
	long		0x3FFF0000,0xF7D0DF73,0x0AD13BB9,0xBFB8B795
	long		0x3FFF0000,0xFA83B2DB,0x722A033A,0x3FBEF84B
	long		0x3FFF0000,0xFD3E0C0C,0xF486C175,0xBFBEF581

	set		INT,L_SCR1

	set		X,FP_SCR0
	set		XDCARE,X+2
	set		XFRAC,X+4

	set		ADJFACT,FP_SCR0

	set		FACT1,FP_SCR0
	set		FACT1HI,FACT1+4
	set		FACT1LOW,FACT1+8

	set		FACT2,FP_SCR1
	set		FACT2HI,FACT2+4
	set		FACT2LOW,FACT2+8

	global		stwotox
#--ENTRY POINT FOR 2**(X), HERE X IS FINITE, NON-ZERO, AND NOT NAN'S
stwotox:
	fmovm.x		(%a0),&0x80		# LOAD INPUT

	mov.l		(%a0),%d1
	mov.w		4(%a0),%d1
	fmov.x		%fp0,X(%a6)
	and.l		&0x7FFFFFFF,%d1

	cmp.l		%d1,&0x3FB98000		# |X| >= 2**(-70)?
	bge.b		TWOOK1
	bra.w		EXPBORS

TWOOK1:
	cmp.l		%d1,&0x400D80C0		# |X| > 16480?
	ble.b		TWOMAIN
	bra.w		EXPBORS

TWOMAIN:
#--USUAL CASE, 2^(-70) <= |X| <= 16480

	fmov.x		%fp0,%fp1
	fmul.s		&0x42800000,%fp1	# 64 * X
	fmov.l		%fp1,INT(%a6)		# N = ROUND-TO-INT(64 X)
	mov.l		%d2,-(%sp)
	lea		TEXPTBL(%pc),%a1	# LOAD ADDRESS OF TABLE OF 2^(J/64)
	fmov.l		INT(%a6),%fp1		# N --> FLOATING FMT
	mov.l		INT(%a6),%d1
	mov.l		%d1,%d2
	and.l		&0x3F,%d1		# D0 IS J
	asl.l		&4,%d1			# DISPLACEMENT FOR 2^(J/64)
	add.l		%d1,%a1			# ADDRESS FOR 2^(J/64)
	asr.l		&6,%d2			# d2 IS L, N = 64L + J
	mov.l		%d2,%d1
	asr.l		&1,%d1			# D0 IS M
	sub.l		%d1,%d2			# d2 IS M', N = 64(M+M') + J
	add.l		&0x3FFF,%d2

#--SUMMARY: a1 IS ADDRESS FOR THE LEADING PORTION OF 2^(J/64),
#--D0 IS M WHERE N = 64(M+M') + J. NOTE THAT |M| <= 16140 BY DESIGN.
#--ADJFACT = 2^(M').
#--REGISTERS SAVED SO FAR ARE (IN ORDER) FPCR, D0, FP1, a1, AND FP2.

	fmovm.x		&0x0c,-(%sp)		# save fp2/fp3

	fmul.s		&0x3C800000,%fp1	# (1/64)*N
	mov.l		(%a1)+,FACT1(%a6)
	mov.l		(%a1)+,FACT1HI(%a6)
	mov.l		(%a1)+,FACT1LOW(%a6)
	mov.w		(%a1)+,FACT2(%a6)

	fsub.x		%fp1,%fp0		# X - (1/64)*INT(64 X)

	mov.w		(%a1)+,FACT2HI(%a6)
	clr.w		FACT2HI+2(%a6)
	clr.l		FACT2LOW(%a6)
	add.w		%d1,FACT1(%a6)
	fmul.x		LOG2(%pc),%fp0		# FP0 IS R
	add.w		%d1,FACT2(%a6)

	bra.w		expr

EXPBORS:
#--FPCR, D0 SAVED
	cmp.l		%d1,&0x3FFF8000
	bgt.b		TEXPBIG

#--|X| IS SMALL, RETURN 1 + X

	fmov.l		%d0,%fpcr		# restore users round prec,mode
	fadd.s		&0x3F800000,%fp0	# RETURN 1 + X
	bra		t_pinx2

TEXPBIG:
#--|X| IS LARGE, GENERATE OVERFLOW IF X > 0; ELSE GENERATE UNDERFLOW
#--REGISTERS SAVE SO FAR ARE FPCR AND  D0
	mov.l		X(%a6),%d1
	cmp.l		%d1,&0
	blt.b		EXPNEG

	bra		t_ovfl2			# t_ovfl expects positive value

EXPNEG:
	bra		t_unfl2			# t_unfl expects positive value

	global		stwotoxd
stwotoxd:
#--ENTRY POINT FOR 2**(X) FOR DENORMALIZED ARGUMENT

	fmov.l		%d0,%fpcr		# set user's rounding mode/precision
	fmov.s		&0x3F800000,%fp0	# RETURN 1 + X
	mov.l		(%a0),%d1
	or.l		&0x00800001,%d1
	fadd.s		%d1,%fp0
	bra		t_pinx2

	global		stentox
#--ENTRY POINT FOR 10**(X), HERE X IS FINITE, NON-ZERO, AND NOT NAN'S
stentox:
	fmovm.x		(%a0),&0x80		# LOAD INPUT

	mov.l		(%a0),%d1
	mov.w		4(%a0),%d1
	fmov.x		%fp0,X(%a6)
	and.l		&0x7FFFFFFF,%d1

	cmp.l		%d1,&0x3FB98000		# |X| >= 2**(-70)?
	bge.b		TENOK1
	bra.w		EXPBORS

TENOK1:
	cmp.l		%d1,&0x400B9B07		# |X| <= 16480*log2/log10 ?
	ble.b		TENMAIN
	bra.w		EXPBORS

TENMAIN:
#--USUAL CASE, 2^(-70) <= |X| <= 16480 LOG 2 / LOG 10

	fmov.x		%fp0,%fp1
	fmul.d		L2TEN64(%pc),%fp1	# X*64*LOG10/LOG2
	fmov.l		%fp1,INT(%a6)		# N=INT(X*64*LOG10/LOG2)
	mov.l		%d2,-(%sp)
	lea		TEXPTBL(%pc),%a1	# LOAD ADDRESS OF TABLE OF 2^(J/64)
	fmov.l		INT(%a6),%fp1		# N --> FLOATING FMT
	mov.l		INT(%a6),%d1
	mov.l		%d1,%d2
	and.l		&0x3F,%d1		# D0 IS J
	asl.l		&4,%d1			# DISPLACEMENT FOR 2^(J/64)
	add.l		%d1,%a1			# ADDRESS FOR 2^(J/64)
	asr.l		&6,%d2			# d2 IS L, N = 64L + J
	mov.l		%d2,%d1
	asr.l		&1,%d1			# D0 IS M
	sub.l		%d1,%d2			# d2 IS M', N = 64(M+M') + J
	add.l		&0x3FFF,%d2

#--SUMMARY: a1 IS ADDRESS FOR THE LEADING PORTION OF 2^(J/64),
#--D0 IS M WHERE N = 64(M+M') + J. NOTE THAT |M| <= 16140 BY DESIGN.
#--ADJFACT = 2^(M').
#--REGISTERS SAVED SO FAR ARE (IN ORDER) FPCR, D0, FP1, a1, AND FP2.
	fmovm.x		&0x0c,-(%sp)		# save fp2/fp3

	fmov.x		%fp1,%fp2

	fmul.d		L10TWO1(%pc),%fp1	# N*(LOG2/64LOG10)_LEAD
	mov.l		(%a1)+,FACT1(%a6)

	fmul.x		L10TWO2(%pc),%fp2	# N*(LOG2/64LOG10)_TRAIL

	mov.l		(%a1)+,FACT1HI(%a6)
	mov.l		(%a1)+,FACT1LOW(%a6)
	fsub.x		%fp1,%fp0		# X - N L_LEAD
	mov.w		(%a1)+,FACT2(%a6)

	fsub.x		%fp2,%fp0		# X - N L_TRAIL

	mov.w		(%a1)+,FACT2HI(%a6)
	clr.w		FACT2HI+2(%a6)
	clr.l		FACT2LOW(%a6)

	fmul.x		LOG10(%pc),%fp0		# FP0 IS R
	add.w		%d1,FACT1(%a6)
	add.w		%d1,FACT2(%a6)

expr:
#--FPCR, FP2, FP3 ARE SAVED IN ORDER AS SHOWN.
#--ADJFACT CONTAINS 2**(M'), FACT1 + FACT2 = 2**(M) * 2**(J/64).
#--FP0 IS R. THE FOLLOWING CODE COMPUTES
#--	2**(M'+M) * 2**(J/64) * EXP(R)

	fmov.x		%fp0,%fp1
	fmul.x		%fp1,%fp1		# FP1 IS S = R*R

	fmov.d		EXPA5(%pc),%fp2		# FP2 IS A5
	fmov.d		EXPA4(%pc),%fp3		# FP3 IS A4

	fmul.x		%fp1,%fp2		# FP2 IS S*A5
	fmul.x		%fp1,%fp3		# FP3 IS S*A4

	fadd.d		EXPA3(%pc),%fp2		# FP2 IS A3+S*A5
	fadd.d		EXPA2(%pc),%fp3		# FP3 IS A2+S*A4

	fmul.x		%fp1,%fp2		# FP2 IS S*(A3+S*A5)
	fmul.x		%fp1,%fp3		# FP3 IS S*(A2+S*A4)

	fadd.d		EXPA1(%pc),%fp2		# FP2 IS A1+S*(A3+S*A5)
	fmul.x		%fp0,%fp3		# FP3 IS R*S*(A2+S*A4)

	fmul.x		%fp1,%fp2		# FP2 IS S*(A1+S*(A3+S*A5))
	fadd.x		%fp3,%fp0		# FP0 IS R+R*S*(A2+S*A4)
	fadd.x		%fp2,%fp0		# FP0 IS EXP(R) - 1

	fmovm.x		(%sp)+,&0x30		# restore fp2/fp3

#--FINAL RECONSTRUCTION PROCESS
#--EXP(X) = 2^M*2^(J/64) + 2^M*2^(J/64)*(EXP(R)-1)  -  (1 OR 0)

	fmul.x		FACT1(%a6),%fp0
	fadd.x		FACT2(%a6),%fp0
	fadd.x		FACT1(%a6),%fp0

	fmov.l		%d0,%fpcr		# restore users round prec,mode
	mov.w		%d2,ADJFACT(%a6)	# INSERT EXPONENT
	mov.l		(%sp)+,%d2
	mov.l		&0x80000000,ADJFACT+4(%a6)
	clr.l		ADJFACT+8(%a6)
	mov.b		&FMUL_OP,%d1		# last inst is MUL
	fmul.x		ADJFACT(%a6),%fp0	# FINAL ADJUSTMENT
	bra		t_catch

	global		stentoxd
stentoxd:
#--ENTRY POINT FOR 10**(X) FOR DENORMALIZED ARGUMENT

	fmov.l		%d0,%fpcr		# set user's rounding mode/precision
	fmov.s		&0x3F800000,%fp0	# RETURN 1 + X
	mov.l		(%a0),%d1
	or.l		&0x00800001,%d1
	fadd.s		%d1,%fp0
	bra		t_pinx2

#########################################################################
# sscale(): computes the destination operand scaled by the source	#
#	    operand. If the absoulute value of the source operand is	#
#	    >= 2^14, an overflow or underflow is returned.		#
#									#
# INPUT *************************************************************** #
#	a0  = pointer to double-extended source operand X		#
#	a1  = pointer to double-extended destination operand Y		#
#									#
# OUTPUT ************************************************************** #
#	fp0 =  scale(X,Y)						#
#									#
#########################################################################

set	SIGN,		L_SCR1

	global		sscale
sscale:
	mov.l		%d0,-(%sp)		# store off ctrl bits for now

	mov.w		DST_EX(%a1),%d1		# get dst exponent
	smi.b		SIGN(%a6)		# use SIGN to hold dst sign
	andi.l		&0x00007fff,%d1		# strip sign from dst exp

	mov.w		SRC_EX(%a0),%d0		# check src bounds
	andi.w		&0x7fff,%d0		# clr src sign bit
	cmpi.w		%d0,&0x3fff		# is src ~ ZERO?
	blt.w		src_small		# yes
	cmpi.w		%d0,&0x400c		# no; is src too big?
	bgt.w		src_out			# yes

#
# Source is within 2^14 range.
#
src_ok:
	fintrz.x	SRC(%a0),%fp0		# calc int of src
	fmov.l		%fp0,%d0		# int src to d0
# don't want any accrued bits from the fintrz showing up later since
# we may need to read the fpsr for the last fp op in t_catch2().
	fmov.l		&0x0,%fpsr

	tst.b		DST_HI(%a1)		# is dst denormalized?
	bmi.b		sok_norm

# the dst is a DENORM. normalize the DENORM and add the adjustment to
# the src value. then, jump to the norm part of the routine.
sok_dnrm:
	mov.l		%d0,-(%sp)		# save src for now

	mov.w		DST_EX(%a1),FP_SCR0_EX(%a6) # make a copy
	mov.l		DST_HI(%a1),FP_SCR0_HI(%a6)
	mov.l		DST_LO(%a1),FP_SCR0_LO(%a6)

	lea		FP_SCR0(%a6),%a0	# pass ptr to DENORM
	bsr.l		norm			# normalize the DENORM
	neg.l		%d0
	add.l		(%sp)+,%d0		# add adjustment to src

	fmovm.x		FP_SCR0(%a6),&0x80	# load normalized DENORM

	cmpi.w		%d0,&-0x3fff		# is the shft amt really low?
	bge.b		sok_norm2		# thank goodness no

# the multiply factor that we're trying to create should be a denorm
# for the multiply to work. therefore, we're going to actually do a
# multiply with a denorm which will cause an unimplemented data type
# exception to be put into the machine which will be caught and corrected
# later. we don't do this with the DENORMs above because this method
# is slower. but, don't fret, I don't see it being used much either.
	fmov.l		(%sp)+,%fpcr		# restore user fpcr
	mov.l		&0x80000000,%d1		# load normalized mantissa
	subi.l		&-0x3fff,%d0		# how many should we shift?
	neg.l		%d0			# make it positive
	cmpi.b		%d0,&0x20		# is it > 32?
	bge.b		sok_dnrm_32		# yes
	lsr.l		%d0,%d1			# no; bit stays in upper lw
	clr.l		-(%sp)			# insert zero low mantissa
	mov.l		%d1,-(%sp)		# insert new high mantissa
	clr.l		-(%sp)			# make zero exponent
	bra.b		sok_norm_cont
sok_dnrm_32:
	subi.b		&0x20,%d0		# get shift count
	lsr.l		%d0,%d1			# make low mantissa longword
	mov.l		%d1,-(%sp)		# insert new low mantissa
	clr.l		-(%sp)			# insert zero high mantissa
	clr.l		-(%sp)			# make zero exponent
	bra.b		sok_norm_cont

# the src will force the dst to a DENORM value or worse. so, let's
# create an fp multiply that will create the result.
sok_norm:
	fmovm.x		DST(%a1),&0x80		# load fp0 with normalized src
sok_norm2:
	fmov.l		(%sp)+,%fpcr		# restore user fpcr

	addi.w		&0x3fff,%d0		# turn src amt into exp value
	swap		%d0			# put exponent in high word
	clr.l		-(%sp)			# insert new exponent
	mov.l		&0x80000000,-(%sp)	# insert new high mantissa
	mov.l		%d0,-(%sp)		# insert new lo mantissa

sok_norm_cont:
	fmov.l		%fpcr,%d0		# d0 needs fpcr for t_catch2
	mov.b		&FMUL_OP,%d1		# last inst is MUL
	fmul.x		(%sp)+,%fp0		# do the multiply
	bra		t_catch2		# catch any exceptions

#
# Source is outside of 2^14 range.  Test the sign and branch
# to the appropriate exception handler.
#
src_out:
	mov.l		(%sp)+,%d0		# restore ctrl bits
	exg		%a0,%a1			# swap src,dst ptrs
	tst.b		SRC_EX(%a1)		# is src negative?
	bmi		t_unfl			# yes; underflow
	bra		t_ovfl_sc		# no; overflow

#
# The source input is below 1, so we check for denormalized numbers
# and set unfl.
#
src_small:
	tst.b		DST_HI(%a1)		# is dst denormalized?
	bpl.b		ssmall_done		# yes

	mov.l		(%sp)+,%d0
	fmov.l		%d0,%fpcr		# no; load control bits
	mov.b		&FMOV_OP,%d1		# last inst is MOVE
	fmov.x		DST(%a1),%fp0		# simply return dest
	bra		t_catch2
ssmall_done:
	mov.l		(%sp)+,%d0		# load control bits into d1
	mov.l		%a1,%a0			# pass ptr to dst
	bra		t_resdnrm

#########################################################################
# smod(): computes the fp MOD of the input values X,Y.			#
# srem(): computes the fp (IEEE) REM of the input values X,Y.		#
#									#
# INPUT *************************************************************** #
#	a0 = pointer to extended precision input X			#
#	a1 = pointer to extended precision input Y			#
#	d0 = round precision,mode					#
#									#
#	The input operands X and Y can be either normalized or		#
#	denormalized.							#
#									#
# OUTPUT ************************************************************** #
#      fp0 = FREM(X,Y) or FMOD(X,Y)					#
#									#
# ALGORITHM *********************************************************** #
#									#
#       Step 1.  Save and strip signs of X and Y: signX := sign(X),	#
#                signY := sign(Y), X := |X|, Y := |Y|,			#
#                signQ := signX EOR signY. Record whether MOD or REM	#
#                is requested.						#
#									#
#       Step 2.  Set L := expo(X)-expo(Y), k := 0, Q := 0.		#
#                If (L < 0) then					#
#                   R := X, go to Step 4.				#
#                else							#
#                   R := 2^(-L)X, j := L.				#
#                endif							#
#									#
#       Step 3.  Perform MOD(X,Y)					#
#            3.1 If R = Y, go to Step 9.				#
#            3.2 If R > Y, then { R := R - Y, Q := Q + 1}		#
#            3.3 If j = 0, go to Step 4.				#
#            3.4 k := k + 1, j := j - 1, Q := 2Q, R := 2R. Go to	#
#                Step 3.1.						#
#									#
#       Step 4.  At this point, R = X - QY = MOD(X,Y). Set		#
#                Last_Subtract := false (used in Step 7 below). If	#
#                MOD is requested, go to Step 6.			#
#									#
#       Step 5.  R = MOD(X,Y), but REM(X,Y) is requested.		#
#            5.1 If R < Y/2, then R = MOD(X,Y) = REM(X,Y). Go to	#
#                Step 6.						#
#            5.2 If R > Y/2, then { set Last_Subtract := true,		#
#                Q := Q + 1, Y := signY*Y }. Go to Step 6.		#
#            5.3 This is the tricky case of R = Y/2. If Q is odd,	#
#                then { Q := Q + 1, signX := -signX }.			#
#									#
#       Step 6.  R := signX*R.						#
#									#
#       Step 7.  If Last_Subtract = true, R := R - Y.			#
#									#
#       Step 8.  Return signQ, last 7 bits of Q, and R as required.	#
#									#
#       Step 9.  At this point, R = 2^(-j)*X - Q Y = Y. Thus,		#
#                X = 2^(j)*(Q+1)Y. set Q := 2^(j)*(Q+1),		#
#                R := 0. Return signQ, last 7 bits of Q, and R.		#
#									#
#########################################################################

	set		Mod_Flag,L_SCR3
	set		Sc_Flag,L_SCR3+1

	set		SignY,L_SCR2
	set		SignX,L_SCR2+2
	set		SignQ,L_SCR3+2

	set		Y,FP_SCR0
	set		Y_Hi,Y+4
	set		Y_Lo,Y+8

	set		R,FP_SCR1
	set		R_Hi,R+4
	set		R_Lo,R+8

Scale:
	long		0x00010000,0x80000000,0x00000000,0x00000000

	global		smod
smod:
	clr.b		FPSR_QBYTE(%a6)
	mov.l		%d0,-(%sp)		# save ctrl bits
	clr.b		Mod_Flag(%a6)
	bra.b		Mod_Rem

	global		srem
srem:
	clr.b		FPSR_QBYTE(%a6)
	mov.l		%d0,-(%sp)		# save ctrl bits
	mov.b		&0x1,Mod_Flag(%a6)

Mod_Rem:
#..Save sign of X and Y
	movm.l		&0x3f00,-(%sp)		# save data registers
	mov.w		SRC_EX(%a0),%d3
	mov.w		%d3,SignY(%a6)
	and.l		&0x00007FFF,%d3		# Y := |Y|

#
	mov.l		SRC_HI(%a0),%d4
	mov.l		SRC_LO(%a0),%d5		# (D3,D4,D5) is |Y|

	tst.l		%d3
	bne.b		Y_Normal

	mov.l		&0x00003FFE,%d3		# $3FFD + 1
	tst.l		%d4
	bne.b		HiY_not0

HiY_0:
	mov.l		%d5,%d4
	clr.l		%d5
	sub.l		&32,%d3
	clr.l		%d6
	bfffo		%d4{&0:&32},%d6
	lsl.l		%d6,%d4
	sub.l		%d6,%d3			# (D3,D4,D5) is normalized
#	                                        ...with bias $7FFD
	bra.b		Chk_X

HiY_not0:
	clr.l		%d6
	bfffo		%d4{&0:&32},%d6
	sub.l		%d6,%d3
	lsl.l		%d6,%d4
	mov.l		%d5,%d7			# a copy of D5
	lsl.l		%d6,%d5
	neg.l		%d6
	add.l		&32,%d6
	lsr.l		%d6,%d7
	or.l		%d7,%d4			# (D3,D4,D5) normalized
#                                       ...with bias $7FFD
	bra.b		Chk_X

Y_Normal:
	add.l		&0x00003FFE,%d3		# (D3,D4,D5) normalized
#                                       ...with bias $7FFD

Chk_X:
	mov.w		DST_EX(%a1),%d0
	mov.w		%d0,SignX(%a6)
	mov.w		SignY(%a6),%d1
	eor.l		%d0,%d1
	and.l		&0x00008000,%d1
	mov.w		%d1,SignQ(%a6)		# sign(Q) obtained
	and.l		&0x00007FFF,%d0
	mov.l		DST_HI(%a1),%d1
	mov.l		DST_LO(%a1),%d2		# (D0,D1,D2) is |X|
	tst.l		%d0
	bne.b		X_Normal
	mov.l		&0x00003FFE,%d0
	tst.l		%d1
	bne.b		HiX_not0

HiX_0:
	mov.l		%d2,%d1
	clr.l		%d2
	sub.l		&32,%d0
	clr.l		%d6
	bfffo		%d1{&0:&32},%d6
	lsl.l		%d6,%d1
	sub.l		%d6,%d0			# (D0,D1,D2) is normalized
#                                       ...with bias $7FFD
	bra.b		Init

HiX_not0:
	clr.l		%d6
	bfffo		%d1{&0:&32},%d6
	sub.l		%d6,%d0
	lsl.l		%d6,%d1
	mov.l		%d2,%d7			# a copy of D2
	lsl.l		%d6,%d2
	neg.l		%d6
	add.l		&32,%d6
	lsr.l		%d6,%d7
	or.l		%d7,%d1			# (D0,D1,D2) normalized
#                                       ...with bias $7FFD
	bra.b		Init

X_Normal:
	add.l		&0x00003FFE,%d0		# (D0,D1,D2) normalized
#                                       ...with bias $7FFD

Init:
#
	mov.l		%d3,L_SCR1(%a6)		# save biased exp(Y)
	mov.l		%d0,-(%sp)		# save biased exp(X)
	sub.l		%d3,%d0			# L := expo(X)-expo(Y)

	clr.l		%d6			# D6 := carry <- 0
	clr.l		%d3			# D3 is Q
	mov.l		&0,%a1			# A1 is k; j+k=L, Q=0

#..(Carry,D1,D2) is R
	tst.l		%d0
	bge.b		Mod_Loop_pre

#..expo(X) < expo(Y). Thus X = mod(X,Y)
#
	mov.l		(%sp)+,%d0		# restore d0
	bra.w		Get_Mod

Mod_Loop_pre:
	addq.l		&0x4,%sp		# erase exp(X)
#..At this point  R = 2^(-L)X; Q = 0; k = 0; and  k+j = L
Mod_Loop:
	tst.l		%d6			# test carry bit
	bgt.b		R_GT_Y

#..At this point carry = 0, R = (D1,D2), Y = (D4,D5)
	cmp.l		%d1,%d4			# compare hi(R) and hi(Y)
	bne.b		R_NE_Y
	cmp.l		%d2,%d5			# compare lo(R) and lo(Y)
	bne.b		R_NE_Y

#..At this point, R = Y
	bra.w		Rem_is_0

R_NE_Y:
#..use the borrow of the previous compare
	bcs.b		R_LT_Y			# borrow is set iff R < Y

R_GT_Y:
#..If Carry is set, then Y < (Carry,D1,D2) < 2Y. Otherwise, Carry = 0
#..and Y < (D1,D2) < 2Y. Either way, perform R - Y
	sub.l		%d5,%d2			# lo(R) - lo(Y)
	subx.l		%d4,%d1			# hi(R) - hi(Y)
	clr.l		%d6			# clear carry
	addq.l		&1,%d3			# Q := Q + 1

R_LT_Y:
#..At this point, Carry=0, R < Y. R = 2^(k-L)X - QY; k+j = L; j >= 0.
	tst.l		%d0			# see if j = 0.
	beq.b		PostLoop

	add.l		%d3,%d3			# Q := 2Q
	add.l		%d2,%d2			# lo(R) = 2lo(R)
	roxl.l		&1,%d1			# hi(R) = 2hi(R) + carry
	scs		%d6			# set Carry if 2(R) overflows
	addq.l		&1,%a1			# k := k+1
	subq.l		&1,%d0			# j := j - 1
#..At this point, R=(Carry,D1,D2) = 2^(k-L)X - QY, j+k=L, j >= 0, R < 2Y.

	bra.b		Mod_Loop

PostLoop:
#..k = L, j = 0, Carry = 0, R = (D1,D2) = X - QY, R < Y.

#..normalize R.
	mov.l		L_SCR1(%a6),%d0		# new biased expo of R
	tst.l		%d1
	bne.b		HiR_not0

HiR_0:
	mov.l		%d2,%d1
	clr.l		%d2
	sub.l		&32,%d0
	clr.l		%d6
	bfffo		%d1{&0:&32},%d6
	lsl.l		%d6,%d1
	sub.l		%d6,%d0			# (D0,D1,D2) is normalized
#                                       ...with bias $7FFD
	bra.b		Get_Mod

HiR_not0:
	clr.l		%d6
	bfffo		%d1{&0:&32},%d6
	bmi.b		Get_Mod			# already normalized
	sub.l		%d6,%d0
	lsl.l		%d6,%d1
	mov.l		%d2,%d7			# a copy of D2
	lsl.l		%d6,%d2
	neg.l		%d6
	add.l		&32,%d6
	lsr.l		%d6,%d7
	or.l		%d7,%d1			# (D0,D1,D2) normalized

#
Get_Mod:
	cmp.l		%d0,&0x000041FE
	bge.b		No_Scale
Do_Scale:
	mov.w		%d0,R(%a6)
	mov.l		%d1,R_Hi(%a6)
	mov.l		%d2,R_Lo(%a6)
	mov.l		L_SCR1(%a6),%d6
	mov.w		%d6,Y(%a6)
	mov.l		%d4,Y_Hi(%a6)
	mov.l		%d5,Y_Lo(%a6)
	fmov.x		R(%a6),%fp0		# no exception
	mov.b		&1,Sc_Flag(%a6)
	bra.b		ModOrRem
No_Scale:
	mov.l		%d1,R_Hi(%a6)
	mov.l		%d2,R_Lo(%a6)
	sub.l		&0x3FFE,%d0
	mov.w		%d0,R(%a6)
	mov.l		L_SCR1(%a6),%d6
	sub.l		&0x3FFE,%d6
	mov.l		%d6,L_SCR1(%a6)
	fmov.x		R(%a6),%fp0
	mov.w		%d6,Y(%a6)
	mov.l		%d4,Y_Hi(%a6)
	mov.l		%d5,Y_Lo(%a6)
	clr.b		Sc_Flag(%a6)

#
ModOrRem:
	tst.b		Mod_Flag(%a6)
	beq.b		Fix_Sign

	mov.l		L_SCR1(%a6),%d6		# new biased expo(Y)
	subq.l		&1,%d6			# biased expo(Y/2)
	cmp.l		%d0,%d6
	blt.b		Fix_Sign
	bgt.b		Last_Sub

	cmp.l		%d1,%d4
	bne.b		Not_EQ
	cmp.l		%d2,%d5
	bne.b		Not_EQ
	bra.w		Tie_Case

Not_EQ:
	bcs.b		Fix_Sign

Last_Sub:
#
	fsub.x		Y(%a6),%fp0		# no exceptions
	addq.l		&1,%d3			# Q := Q + 1

#
Fix_Sign:
#..Get sign of X
	mov.w		SignX(%a6),%d6
	bge.b		Get_Q
	fneg.x		%fp0

#..Get Q
#
Get_Q:
	clr.l		%d6
	mov.w		SignQ(%a6),%d6		# D6 is sign(Q)
	mov.l		&8,%d7
	lsr.l		%d7,%d6
	and.l		&0x0000007F,%d3		# 7 bits of Q
	or.l		%d6,%d3			# sign and bits of Q
#	swap		%d3
#	fmov.l		%fpsr,%d6
#	and.l		&0xFF00FFFF,%d6
#	or.l		%d3,%d6
#	fmov.l		%d6,%fpsr		# put Q in fpsr
	mov.b		%d3,FPSR_QBYTE(%a6)	# put Q in fpsr

#
Restore:
	movm.l		(%sp)+,&0xfc		#  {%d2-%d7}
	mov.l		(%sp)+,%d0
	fmov.l		%d0,%fpcr
	tst.b		Sc_Flag(%a6)
	beq.b		Finish
	mov.b		&FMUL_OP,%d1		# last inst is MUL
	fmul.x		Scale(%pc),%fp0		# may cause underflow
	bra		t_catch2
# the '040 package did this apparently to see if the dst operand for the
# preceding fmul was a denorm. but, it better not have been since the
# algorithm just got done playing with fp0 and expected no exceptions
# as a result. trust me...
#	bra		t_avoid_unsupp		# check for denorm as a
#						;result of the scaling

Finish:
	mov.b		&FMOV_OP,%d1		# last inst is MOVE
	fmov.x		%fp0,%fp0		# capture exceptions & round
	bra		t_catch2

Rem_is_0:
#..R = 2^(-j)X - Q Y = Y, thus R = 0 and quotient = 2^j (Q+1)
	addq.l		&1,%d3
	cmp.l		%d0,&8			# D0 is j
	bge.b		Q_Big

	lsl.l		%d0,%d3
	bra.b		Set_R_0

Q_Big:
	clr.l		%d3

Set_R_0:
	fmov.s		&0x00000000,%fp0
	clr.b		Sc_Flag(%a6)
	bra.w		Fix_Sign

Tie_Case:
#..Check parity of Q
	mov.l		%d3,%d6
	and.l		&0x00000001,%d6
	tst.l		%d6
	beq.w		Fix_Sign		# Q is even

#..Q is odd, Q := Q + 1, signX := -signX
	addq.l		&1,%d3
	mov.w		SignX(%a6),%d6
	eor.l		&0x00008000,%d6
	mov.w		%d6,SignX(%a6)
	bra.w		Fix_Sign

#########################################################################
# XDEF ****************************************************************	#
#	tag(): return the optype of the input ext fp number		#
#									#
#	This routine is used by the 060FPLSP.				#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision operand			#
#									#
# OUTPUT **************************************************************	#
#	d0 = value of type tag						#
#		one of: NORM, INF, QNAN, SNAN, DENORM, ZERO		#
#									#
# ALGORITHM ***********************************************************	#
#	Simply test the exponent, j-bit, and mantissa values to		#
# determine the type of operand.					#
#	If it's an unnormalized zero, alter the operand and force it	#
# to be a normal zero.							#
#									#
#########################################################################

	global		tag
tag:
	mov.w		FTEMP_EX(%a0), %d0	# extract exponent
	andi.w		&0x7fff, %d0		# strip off sign
	cmpi.w		%d0, &0x7fff		# is (EXP == MAX)?
	beq.b		inf_or_nan_x
not_inf_or_nan_x:
	btst		&0x7,FTEMP_HI(%a0)
	beq.b		not_norm_x
is_norm_x:
	mov.b		&NORM, %d0
	rts
not_norm_x:
	tst.w		%d0			# is exponent = 0?
	bne.b		is_unnorm_x
not_unnorm_x:
	tst.l		FTEMP_HI(%a0)
	bne.b		is_denorm_x
	tst.l		FTEMP_LO(%a0)
	bne.b		is_denorm_x
is_zero_x:
	mov.b		&ZERO, %d0
	rts
is_denorm_x:
	mov.b		&DENORM, %d0
	rts
is_unnorm_x:
	bsr.l		unnorm_fix		# convert to norm,denorm,or zero
	rts
is_unnorm_reg_x:
	mov.b		&UNNORM, %d0
	rts
inf_or_nan_x:
	tst.l		FTEMP_LO(%a0)
	bne.b		is_nan_x
	mov.l		FTEMP_HI(%a0), %d0
	and.l		&0x7fffffff, %d0	# msb is a don't care!
	bne.b		is_nan_x
is_inf_x:
	mov.b		&INF, %d0
	rts
is_nan_x:
	mov.b		&QNAN, %d0
	rts

#############################################################

qnan:	long		0x7fff0000, 0xffffffff, 0xffffffff

#########################################################################
# XDEF ****************************************************************	#
#	t_dz(): Handle 060FPLSP dz exception for "flogn" emulation.	#
#	t_dz2(): Handle 060FPLSP dz exception for "fatanh" emulation.	#
#									#
#	These rouitnes are used by the 060FPLSP package.		#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand.		#
#									#
# OUTPUT **************************************************************	#
#	fp0 = default DZ result.					#
#									#
# ALGORITHM ***********************************************************	#
#	Transcendental emulation for the 060FPLSP has detected that	#
# a DZ exception should occur for the instruction. If DZ is disabled,	#
# return the default result.						#
#	If DZ is enabled, the dst operand should be returned unscathed	#
# in fp0 while fp1 is used to create a DZ exception so that the		#
# operating system can log that such an event occurred.			#
#									#
#########################################################################

	global		t_dz
t_dz:
	tst.b		SRC_EX(%a0)		# check sign for neg or pos
	bpl.b		dz_pinf			# branch if pos sign

	global		t_dz2
t_dz2:
	ori.l		&dzinf_mask+neg_mask,USER_FPSR(%a6) # set N/I/DZ/ADZ

	btst		&dz_bit,FPCR_ENABLE(%a6)
	bne.b		dz_minf_ena

# dz is disabled. return a -INF.
	fmov.s		&0xff800000,%fp0	# return -INF
	rts

# dz is enabled. create a dz exception so the user can record it
# but use fp1 instead. return the dst operand unscathed in fp0.
dz_minf_ena:
	fmovm.x		EXC_FP0(%a6),&0x80	# return fp0 unscathed
	fmov.l		USER_FPCR(%a6),%fpcr
	fmov.s		&0xbf800000,%fp1	# load -1
	fdiv.s		&0x00000000,%fp1	# -1 / 0
	rts

dz_pinf:
	ori.l		&dzinf_mask,USER_FPSR(%a6) # set I/DZ/ADZ

	btst		&dz_bit,FPCR_ENABLE(%a6)
	bne.b		dz_pinf_ena

# dz is disabled. return a +INF.
	fmov.s		&0x7f800000,%fp0	# return +INF
	rts

# dz is enabled. create a dz exception so the user can record it
# but use fp1 instead. return the dst operand unscathed in fp0.
dz_pinf_ena:
	fmovm.x		EXC_FP0(%a6),&0x80	# return fp0 unscathed
	fmov.l		USER_FPCR(%a6),%fpcr
	fmov.s		&0x3f800000,%fp1	# load +1
	fdiv.s		&0x00000000,%fp1	# +1 / 0
	rts

#########################################################################
# XDEF ****************************************************************	#
#	t_operr(): Handle 060FPLSP OPERR exception during emulation.	#
#									#
#	This routine is used by the 060FPLSP package.			#
#									#
# XREF ****************************************************************	#
#	None.								#
#									#
# INPUT ***************************************************************	#
#	fp1 = source operand						#
#									#
# OUTPUT **************************************************************	#
#	fp0 = default result						#
#	fp1 = unchanged							#
#									#
# ALGORITHM ***********************************************************	#
#	An operand error should occur as the result of transcendental	#
# emulation in the 060FPLSP. If OPERR is disabled, just return a NAN	#
# in fp0. If OPERR is enabled, return the dst operand unscathed in fp0	#
# and the source operand in fp1. Use fp2 to create an OPERR exception	#
# so that the operating system can log the event.			#
#									#
#########################################################################

	global		t_operr
t_operr:
	ori.l		&opnan_mask,USER_FPSR(%a6) # set NAN/OPERR/AIOP

	btst		&operr_bit,FPCR_ENABLE(%a6)
	bne.b		operr_ena

# operr is disabled. return a QNAN in fp0
	fmovm.x		qnan(%pc),&0x80		# return QNAN
	rts

# operr is enabled. create an operr exception so the user can record it
# but use fp2 instead. return the dst operand unscathed in fp0.
operr_ena:
	fmovm.x		EXC_FP0(%a6),&0x80	# return fp0 unscathed
	fmov.l		USER_FPCR(%a6),%fpcr
	fmovm.x		&0x04,-(%sp)		# save fp2
	fmov.s		&0x7f800000,%fp2	# load +INF
	fmul.s		&0x00000000,%fp2	# +INF x 0
	fmovm.x		(%sp)+,&0x20		# restore fp2
	rts

pls_huge:
	long		0x7ffe0000,0xffffffff,0xffffffff
mns_huge:
	long		0xfffe0000,0xffffffff,0xffffffff
pls_tiny:
	long		0x00000000,0x80000000,0x00000000
mns_tiny:
	long		0x80000000,0x80000000,0x00000000

#########################################################################
# XDEF ****************************************************************	#
#	t_unfl(): Handle 060FPLSP underflow exception during emulation.	#
#	t_unfl2(): Handle 060FPLSP underflow exception during		#
#	           emulation. result always positive.			#
#									#
#	This routine is used by the 060FPLSP package.			#
#									#
# XREF ****************************************************************	#
#	None.								#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#									#
# OUTPUT **************************************************************	#
#	fp0 = default underflow result					#
#									#
# ALGORITHM ***********************************************************	#
#	An underflow should occur as the result of transcendental	#
# emulation in the 060FPLSP. Create an underflow by using "fmul"	#
# and two very small numbers of appropriate sign so the operating	#
# system can log the event.						#
#									#
#########################################################################

	global		t_unfl
t_unfl:
	tst.b		SRC_EX(%a0)
	bpl.b		unf_pos

	global		t_unfl2
t_unfl2:
	ori.l		&unfinx_mask+neg_mask,USER_FPSR(%a6) # set N/UNFL/INEX2/AUNFL/AINEX

	fmov.l		USER_FPCR(%a6),%fpcr
	fmovm.x		mns_tiny(%pc),&0x80
	fmul.x		pls_tiny(%pc),%fp0

	fmov.l		%fpsr,%d0
	rol.l		&0x8,%d0
	mov.b		%d0,FPSR_CC(%a6)
	rts
unf_pos:
	ori.w		&unfinx_mask,FPSR_EXCEPT(%a6) # set UNFL/INEX2/AUNFL/AINEX

	fmov.l		USER_FPCR(%a6),%fpcr
	fmovm.x		pls_tiny(%pc),&0x80
	fmul.x		%fp0,%fp0

	fmov.l		%fpsr,%d0
	rol.l		&0x8,%d0
	mov.b		%d0,FPSR_CC(%a6)
	rts

#########################################################################
# XDEF ****************************************************************	#
#	t_ovfl(): Handle 060FPLSP overflow exception during emulation.	#
#		  (monadic)						#
#	t_ovfl2(): Handle 060FPLSP overflow exception during		#
#	           emulation. result always positive. (dyadic)		#
#	t_ovfl_sc(): Handle 060FPLSP overflow exception during		#
#	             emulation for "fscale".				#
#									#
#	This routine is used by the 060FPLSP package.			#
#									#
# XREF ****************************************************************	#
#	None.								#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#									#
# OUTPUT **************************************************************	#
#	fp0 = default underflow result					#
#									#
# ALGORITHM ***********************************************************	#
#	An overflow should occur as the result of transcendental	#
# emulation in the 060FPLSP. Create an overflow by using "fmul"		#
# and two very lareg numbers of appropriate sign so the operating	#
# system can log the event.						#
#	For t_ovfl_sc() we take special care not to lose the INEX2 bit.	#
#									#
#########################################################################

	global		t_ovfl_sc
t_ovfl_sc:
	ori.l		&ovfl_inx_mask,USER_FPSR(%a6) # set OVFL/AOVFL/AINEX

	mov.b		%d0,%d1			# fetch rnd prec,mode
	andi.b		&0xc0,%d1		# extract prec
	beq.w		ovfl_work

# dst op is a DENORM. we have to normalize the mantissa to see if the
# result would be inexact for the given precision. make a copy of the
# dst so we don't screw up the version passed to us.
	mov.w		LOCAL_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		LOCAL_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		LOCAL_LO(%a0),FP_SCR0_LO(%a6)
	lea		FP_SCR0(%a6),%a0	# pass ptr to FP_SCR0
	movm.l		&0xc080,-(%sp)		# save d0-d1/a0
	bsr.l		norm			# normalize mantissa
	movm.l		(%sp)+,&0x0103		# restore d0-d1/a0

	cmpi.b		%d1,&0x40		# is precision sgl?
	bne.b		ovfl_sc_dbl		# no; dbl
ovfl_sc_sgl:
	tst.l		LOCAL_LO(%a0)		# is lo lw of sgl set?
	bne.b		ovfl_sc_inx		# yes
	tst.b		3+LOCAL_HI(%a0)		# is lo byte of hi lw set?
	bne.b		ovfl_sc_inx		# yes
	bra.w		ovfl_work		# don't set INEX2
ovfl_sc_dbl:
	mov.l		LOCAL_LO(%a0),%d1	# are any of lo 11 bits of
	andi.l		&0x7ff,%d1		# dbl mantissa set?
	beq.w		ovfl_work		# no; don't set INEX2
ovfl_sc_inx:
	ori.l		&inex2_mask,USER_FPSR(%a6) # set INEX2
	bra.b		ovfl_work		# continue

	global		t_ovfl
t_ovfl:
	ori.w		&ovfinx_mask,FPSR_EXCEPT(%a6) # set OVFL/INEX2/AOVFL/AINEX
ovfl_work:
	tst.b		SRC_EX(%a0)
	bpl.b		ovfl_p
ovfl_m:
	fmov.l		USER_FPCR(%a6),%fpcr
	fmovm.x		mns_huge(%pc),&0x80
	fmul.x		pls_huge(%pc),%fp0

	fmov.l		%fpsr,%d0
	rol.l		&0x8,%d0
	ori.b		&neg_mask,%d0
	mov.b		%d0,FPSR_CC(%a6)
	rts
ovfl_p:
	fmov.l		USER_FPCR(%a6),%fpcr
	fmovm.x		pls_huge(%pc),&0x80
	fmul.x		pls_huge(%pc),%fp0

	fmov.l		%fpsr,%d0
	rol.l		&0x8,%d0
	mov.b		%d0,FPSR_CC(%a6)
	rts

	global		t_ovfl2
t_ovfl2:
	ori.w		&ovfinx_mask,FPSR_EXCEPT(%a6) # set OVFL/INEX2/AOVFL/AINEX
	fmov.l		USER_FPCR(%a6),%fpcr
	fmovm.x		pls_huge(%pc),&0x80
	fmul.x		pls_huge(%pc),%fp0

	fmov.l		%fpsr,%d0
	rol.l		&0x8,%d0
	mov.b		%d0,FPSR_CC(%a6)
	rts

#########################################################################
# XDEF ****************************************************************	#
#	t_catch(): Handle 060FPLSP OVFL,UNFL,or INEX2 exception during	#
#		   emulation.						#
#	t_catch2(): Handle 060FPLSP OVFL,UNFL,or INEX2 exception during	#
#		    emulation.						#
#									#
#	These routines are used by the 060FPLSP package.		#
#									#
# XREF ****************************************************************	#
#	None.								#
#									#
# INPUT ***************************************************************	#
#	fp0 = default underflow or overflow result			#
#									#
# OUTPUT **************************************************************	#
#	fp0 = default result						#
#									#
# ALGORITHM ***********************************************************	#
#	If an overflow or underflow occurred during the last		#
# instruction of transcendental 060FPLSP emulation, then it has already	#
# occurred and has been logged. Now we need to see if an inexact	#
# exception should occur.						#
#									#
#########################################################################

	global		t_catch2
t_catch2:
	fmov.l		%fpsr,%d0
	or.l		%d0,USER_FPSR(%a6)
	bra.b		inx2_work

	global		t_catch
t_catch:
	fmov.l		%fpsr,%d0
	or.l		%d0,USER_FPSR(%a6)

#########################################################################
# XDEF ****************************************************************	#
#	t_inx2(): Handle inexact 060FPLSP exception during emulation.	#
#	t_pinx2(): Handle inexact 060FPLSP exception for "+" results.	#
#	t_minx2(): Handle inexact 060FPLSP exception for "-" results.	#
#									#
# XREF ****************************************************************	#
#	None.								#
#									#
# INPUT ***************************************************************	#
#	fp0 = default result						#
#									#
# OUTPUT **************************************************************	#
#	fp0 = default result						#
#									#
# ALGORITHM ***********************************************************	#
#	The last instruction of transcendental emulation for the	#
# 060FPLSP should be inexact. So, if inexact is enabled, then we create	#
# the event here by adding a large and very small number together	#
# so that the operating system can log the event.			#
#	Must check, too, if the result was zero, in which case we just	#
# set the FPSR bits and return.						#
#									#
#########################################################################

	global		t_inx2
t_inx2:
	fblt.w		t_minx2
	fbeq.w		inx2_zero

	global		t_pinx2
t_pinx2:
	ori.w		&inx2a_mask,FPSR_EXCEPT(%a6) # set INEX2/AINEX
	bra.b		inx2_work

	global		t_minx2
t_minx2:
	ori.l		&inx2a_mask+neg_mask,USER_FPSR(%a6)

inx2_work:
	btst		&inex2_bit,FPCR_ENABLE(%a6) # is inexact enabled?
	bne.b		inx2_work_ena		# yes
	rts
inx2_work_ena:
	fmov.l		USER_FPCR(%a6),%fpcr	# insert user's exceptions
	fmov.s		&0x3f800000,%fp1	# load +1
	fadd.x		pls_tiny(%pc),%fp1	# cause exception
	rts

inx2_zero:
	mov.b		&z_bmask,FPSR_CC(%a6)
	ori.w		&inx2a_mask,2+USER_FPSR(%a6) # set INEX/AINEX
	rts

#########################################################################
# XDEF ****************************************************************	#
#	t_extdnrm(): Handle DENORM inputs in 060FPLSP.			#
#	t_resdnrm(): Handle DENORM inputs in 060FPLSP for "fscale".	#
#									#
#	This routine is used by the 060FPLSP package.			#
#									#
# XREF ****************************************************************	#
#	None.								#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision input operand		#
#									#
# OUTPUT **************************************************************	#
#	fp0 = default result						#
#									#
# ALGORITHM ***********************************************************	#
#	For all functions that have a denormalized input and that	#
# f(x)=x, this is the entry point.					#
#	DENORM value is moved using "fmove" which triggers an exception	#
# if enabled so the operating system can log the event.			#
#									#
#########################################################################

	global		t_extdnrm
t_extdnrm:
	fmov.l		USER_FPCR(%a6),%fpcr
	fmov.x		SRC_EX(%a0),%fp0
	fmov.l		%fpsr,%d0
	ori.l		&unfinx_mask,%d0
	or.l		%d0,USER_FPSR(%a6)
	rts

	global		t_resdnrm
t_resdnrm:
	fmov.l		USER_FPCR(%a6),%fpcr
	fmov.x		SRC_EX(%a0),%fp0
	fmov.l		%fpsr,%d0
	or.l		%d0,USER_FPSR(%a6)
	rts

##########################################

#
# sto_cos:
#	This is used by fsincos library emulation. The correct
# values are already in fp0 and fp1 so we do nothing here.
#
	global		sto_cos
sto_cos:
	rts

##########################################

#
#	dst_qnan --- force result when destination is a NaN
#
	global		dst_qnan
dst_qnan:
	fmov.x		DST(%a1),%fp0
	tst.b		DST_EX(%a1)
	bmi.b		dst_qnan_m
dst_qnan_p:
	mov.b		&nan_bmask,FPSR_CC(%a6)
	rts
dst_qnan_m:
	mov.b		&nan_bmask+neg_bmask,FPSR_CC(%a6)
	rts

#
#	src_qnan --- force result when source is a NaN
#
	global		src_qnan
src_qnan:
	fmov.x		SRC(%a0),%fp0
	tst.b		SRC_EX(%a0)
	bmi.b		src_qnan_m
src_qnan_p:
	mov.b		&nan_bmask,FPSR_CC(%a6)
	rts
src_qnan_m:
	mov.b		&nan_bmask+neg_bmask,FPSR_CC(%a6)
	rts

##########################################

#
#	Native instruction support
#
#	Some systems may need entry points even for 68060 native
#	instructions.  These routines are provided for
#	convenience.
#
	global		_fadds_
_fadds_:
	fmov.l		%fpcr,-(%sp)		# save fpcr
	fmov.l		&0x00000000,%fpcr	# clear fpcr for load
	fmov.s		0x8(%sp),%fp0		# load sgl dst
	fmov.l		(%sp)+,%fpcr		# restore fpcr
	fadd.s		0x8(%sp),%fp0		# fadd w/ sgl src
	rts

	global		_faddd_
_faddd_:
	fmov.l		%fpcr,-(%sp)		# save fpcr
	fmov.l		&0x00000000,%fpcr	# clear fpcr for load
	fmov.d		0x8(%sp),%fp0		# load dbl dst
	fmov.l		(%sp)+,%fpcr		# restore fpcr
	fadd.d		0xc(%sp),%fp0		# fadd w/ dbl src
	rts

	global		_faddx_
_faddx_:
	fmovm.x		0x4(%sp),&0x80		# load ext dst
	fadd.x		0x10(%sp),%fp0		# fadd w/ ext src
	rts

	global		_fsubs_
_fsubs_:
	fmov.l		%fpcr,-(%sp)		# save fpcr
	fmov.l		&0x00000000,%fpcr	# clear fpcr for load
	fmov.s		0x8(%sp),%fp0		# load sgl dst
	fmov.l		(%sp)+,%fpcr		# restore fpcr
	fsub.s		0x8(%sp),%fp0		# fsub w/ sgl src
	rts

	global		_fsubd_
_fsubd_:
	fmov.l		%fpcr,-(%sp)		# save fpcr
	fmov.l		&0x00000000,%fpcr	# clear fpcr for load
	fmov.d		0x8(%sp),%fp0		# load dbl dst
	fmov.l		(%sp)+,%fpcr		# restore fpcr
	fsub.d		0xc(%sp),%fp0		# fsub w/ dbl src
	rts

	global		_fsubx_
_fsubx_:
	fmovm.x		0x4(%sp),&0x80		# load ext dst
	fsub.x		0x10(%sp),%fp0		# fsub w/ ext src
	rts

	global		_fmuls_
_fmuls_:
	fmov.l		%fpcr,-(%sp)		# save fpcr
	fmov.l		&0x00000000,%fpcr	# clear fpcr for load
	fmov.s		0x8(%sp),%fp0		# load sgl dst
	fmov.l		(%sp)+,%fpcr		# restore fpcr
	fmul.s		0x8(%sp),%fp0		# fmul w/ sgl src
	rts

	global		_fmuld_
_fmuld_:
	fmov.l		%fpcr,-(%sp)		# save fpcr
	fmov.l		&0x00000000,%fpcr	# clear fpcr for load
	fmov.d		0x8(%sp),%fp0		# load dbl dst
	fmov.l		(%sp)+,%fpcr		# restore fpcr
	fmul.d		0xc(%sp),%fp0		# fmul w/ dbl src
	rts

	global		_fmulx_
_fmulx_:
	fmovm.x		0x4(%sp),&0x80		# load ext dst
	fmul.x		0x10(%sp),%fp0		# fmul w/ ext src
	rts

	global		_fdivs_
_fdivs_:
	fmov.l		%fpcr,-(%sp)		# save fpcr
	fmov.l		&0x00000000,%fpcr	# clear fpcr for load
	fmov.s		0x8(%sp),%fp0		# load sgl dst
	fmov.l		(%sp)+,%fpcr		# restore fpcr
	fdiv.s		0x8(%sp),%fp0		# fdiv w/ sgl src
	rts

	global		_fdivd_
_fdivd_:
	fmov.l		%fpcr,-(%sp)		# save fpcr
	fmov.l		&0x00000000,%fpcr	# clear fpcr for load
	fmov.d		0x8(%sp),%fp0		# load dbl dst
	fmov.l		(%sp)+,%fpcr		# restore fpcr
	fdiv.d		0xc(%sp),%fp0		# fdiv w/ dbl src
	rts

	global		_fdivx_
_fdivx_:
	fmovm.x		0x4(%sp),&0x80		# load ext dst
	fdiv.x		0x10(%sp),%fp0		# fdiv w/ ext src
	rts

	global		_fabss_
_fabss_:
	fabs.s		0x4(%sp),%fp0		# fabs w/ sgl src
	rts

	global		_fabsd_
_fabsd_:
	fabs.d		0x4(%sp),%fp0		# fabs w/ dbl src
	rts

	global		_fabsx_
_fabsx_:
	fabs.x		0x4(%sp),%fp0		# fabs w/ ext src
	rts

	global		_fnegs_
_fnegs_:
	fneg.s		0x4(%sp),%fp0		# fneg w/ sgl src
	rts

	global		_fnegd_
_fnegd_:
	fneg.d		0x4(%sp),%fp0		# fneg w/ dbl src
	rts

	global		_fnegx_
_fnegx_:
	fneg.x		0x4(%sp),%fp0		# fneg w/ ext src
	rts

	global		_fsqrts_
_fsqrts_:
	fsqrt.s		0x4(%sp),%fp0		# fsqrt w/ sgl src
	rts

	global		_fsqrtd_
_fsqrtd_:
	fsqrt.d		0x4(%sp),%fp0		# fsqrt w/ dbl src
	rts

	global		_fsqrtx_
_fsqrtx_:
	fsqrt.x		0x4(%sp),%fp0		# fsqrt w/ ext src
	rts

	global		_fints_
_fints_:
	fint.s		0x4(%sp),%fp0		# fint w/ sgl src
	rts

	global		_fintd_
_fintd_:
	fint.d		0x4(%sp),%fp0		# fint w/ dbl src
	rts

	global		_fintx_
_fintx_:
	fint.x		0x4(%sp),%fp0		# fint w/ ext src
	rts

	global		_fintrzs_
_fintrzs_:
	fintrz.s	0x4(%sp),%fp0		# fintrz w/ sgl src
	rts

	global		_fintrzd_
_fintrzd_:
	fintrz.d	0x4(%sp),%fp0		# fintrx w/ dbl src
	rts

	global		_fintrzx_
_fintrzx_:
	fintrz.x	0x4(%sp),%fp0		# fintrz w/ ext src
	rts

########################################################################

#########################################################################
# src_zero(): Return signed zero according to sign of src operand.	#
#########################################################################
	global		src_zero
src_zero:
	tst.b		SRC_EX(%a0)		# get sign of src operand
	bmi.b		ld_mzero		# if neg, load neg zero

#
# ld_pzero(): return a positive zero.
#
	global		ld_pzero
ld_pzero:
	fmov.s		&0x00000000,%fp0	# load +0
	mov.b		&z_bmask,FPSR_CC(%a6)	# set 'Z' ccode bit
	rts

# ld_mzero(): return a negative zero.
	global		ld_mzero
ld_mzero:
	fmov.s		&0x80000000,%fp0	# load -0
	mov.b		&neg_bmask+z_bmask,FPSR_CC(%a6) # set 'N','Z' ccode bits
	rts

#########################################################################
# dst_zero(): Return signed zero according to sign of dst operand.	#
#########################################################################
	global		dst_zero
dst_zero:
	tst.b		DST_EX(%a1)		# get sign of dst operand
	bmi.b		ld_mzero		# if neg, load neg zero
	bra.b		ld_pzero		# load positive zero

#########################################################################
# src_inf(): Return signed inf according to sign of src operand.	#
#########################################################################
	global		src_inf
src_inf:
	tst.b		SRC_EX(%a0)		# get sign of src operand
	bmi.b		ld_minf			# if negative branch

#
# ld_pinf(): return a positive infinity.
#
	global		ld_pinf
ld_pinf:
	fmov.s		&0x7f800000,%fp0	# load +INF
	mov.b		&inf_bmask,FPSR_CC(%a6)	# set 'INF' ccode bit
	rts

#
# ld_minf():return a negative infinity.
#
	global		ld_minf
ld_minf:
	fmov.s		&0xff800000,%fp0	# load -INF
	mov.b		&neg_bmask+inf_bmask,FPSR_CC(%a6) # set 'N','I' ccode bits
	rts

#########################################################################
# dst_inf(): Return signed inf according to sign of dst operand.	#
#########################################################################
	global		dst_inf
dst_inf:
	tst.b		DST_EX(%a1)		# get sign of dst operand
	bmi.b		ld_minf			# if negative branch
	bra.b		ld_pinf

	global		szr_inf
#################################################################
# szr_inf(): Return +ZERO for a negative src operand or		#
#	            +INF for a positive src operand.		#
#	     Routine used for fetox, ftwotox, and ftentox.	#
#################################################################
szr_inf:
	tst.b		SRC_EX(%a0)		# check sign of source
	bmi.b		ld_pzero
	bra.b		ld_pinf

#########################################################################
# sopr_inf(): Return +INF for a positive src operand or			#
#	      jump to operand error routine for a negative src operand.	#
#	      Routine used for flogn, flognp1, flog10, and flog2.	#
#########################################################################
	global		sopr_inf
sopr_inf:
	tst.b		SRC_EX(%a0)		# check sign of source
	bmi.w		t_operr
	bra.b		ld_pinf

#################################################################
# setoxm1i(): Return minus one for a negative src operand or	#
#	      positive infinity for a positive src operand.	#
#	      Routine used for fetoxm1.				#
#################################################################
	global		setoxm1i
setoxm1i:
	tst.b		SRC_EX(%a0)		# check sign of source
	bmi.b		ld_mone
	bra.b		ld_pinf

#########################################################################
# src_one(): Return signed one according to sign of src operand.	#
#########################################################################
	global		src_one
src_one:
	tst.b		SRC_EX(%a0)		# check sign of source
	bmi.b		ld_mone

#
# ld_pone(): return positive one.
#
	global		ld_pone
ld_pone:
	fmov.s		&0x3f800000,%fp0	# load +1
	clr.b		FPSR_CC(%a6)
	rts

#
# ld_mone(): return negative one.
#
	global		ld_mone
ld_mone:
	fmov.s		&0xbf800000,%fp0	# load -1
	mov.b		&neg_bmask,FPSR_CC(%a6)	# set 'N' ccode bit
	rts

ppiby2:	long		0x3fff0000, 0xc90fdaa2, 0x2168c235
mpiby2:	long		0xbfff0000, 0xc90fdaa2, 0x2168c235

#################################################################
# spi_2(): Return signed PI/2 according to sign of src operand.	#
#################################################################
	global		spi_2
spi_2:
	tst.b		SRC_EX(%a0)		# check sign of source
	bmi.b		ld_mpi2

#
# ld_ppi2(): return positive PI/2.
#
	global		ld_ppi2
ld_ppi2:
	fmov.l		%d0,%fpcr
	fmov.x		ppiby2(%pc),%fp0	# load +pi/2
	bra.w		t_pinx2			# set INEX2

#
# ld_mpi2(): return negative PI/2.
#
	global		ld_mpi2
ld_mpi2:
	fmov.l		%d0,%fpcr
	fmov.x		mpiby2(%pc),%fp0	# load -pi/2
	bra.w		t_minx2			# set INEX2

####################################################
# The following routines give support for fsincos. #
####################################################

#
# ssincosz(): When the src operand is ZERO, store a one in the
#	      cosine register and return a ZERO in fp0 w/ the same sign
#	      as the src operand.
#
	global		ssincosz
ssincosz:
	fmov.s		&0x3f800000,%fp1
	tst.b		SRC_EX(%a0)		# test sign
	bpl.b		sincoszp
	fmov.s		&0x80000000,%fp0	# return sin result in fp0
	mov.b		&z_bmask+neg_bmask,FPSR_CC(%a6)
	rts
sincoszp:
	fmov.s		&0x00000000,%fp0	# return sin result in fp0
	mov.b		&z_bmask,FPSR_CC(%a6)
	rts

#
# ssincosi(): When the src operand is INF, store a QNAN in the cosine
#	      register and jump to the operand error routine for negative
#	      src operands.
#
	global		ssincosi
ssincosi:
	fmov.x		qnan(%pc),%fp1		# load NAN
	bra.w		t_operr

#
# ssincosqnan(): When the src operand is a QNAN, store the QNAN in the cosine
#		 register and branch to the src QNAN routine.
#
	global		ssincosqnan
ssincosqnan:
	fmov.x		LOCAL_EX(%a0),%fp1
	bra.w		src_qnan

########################################################################

	global		smod_sdnrm
	global		smod_snorm
smod_sdnrm:
smod_snorm:
	mov.b		DTAG(%a6),%d1
	beq.l		smod
	cmpi.b		%d1,&ZERO
	beq.w		smod_zro
	cmpi.b		%d1,&INF
	beq.l		t_operr
	cmpi.b		%d1,&DENORM
	beq.l		smod
	bra.l		dst_qnan

	global		smod_szero
smod_szero:
	mov.b		DTAG(%a6),%d1
	beq.l		t_operr
	cmpi.b		%d1,&ZERO
	beq.l		t_operr
	cmpi.b		%d1,&INF
	beq.l		t_operr
	cmpi.b		%d1,&DENORM
	beq.l		t_operr
	bra.l		dst_qnan

	global		smod_sinf
smod_sinf:
	mov.b		DTAG(%a6),%d1
	beq.l		smod_fpn
	cmpi.b		%d1,&ZERO
	beq.l		smod_zro
	cmpi.b		%d1,&INF
	beq.l		t_operr
	cmpi.b		%d1,&DENORM
	beq.l		smod_fpn
	bra.l		dst_qnan

smod_zro:
srem_zro:
	mov.b		SRC_EX(%a0),%d1		# get src sign
	mov.b		DST_EX(%a1),%d0		# get dst sign
	eor.b		%d0,%d1			# get qbyte sign
	andi.b		&0x80,%d1
	mov.b		%d1,FPSR_QBYTE(%a6)
	tst.b		%d0
	bpl.w		ld_pzero
	bra.w		ld_mzero

smod_fpn:
srem_fpn:
	clr.b		FPSR_QBYTE(%a6)
	mov.l		%d0,-(%sp)
	mov.b		SRC_EX(%a0),%d1		# get src sign
	mov.b		DST_EX(%a1),%d0		# get dst sign
	eor.b		%d0,%d1			# get qbyte sign
	andi.b		&0x80,%d1
	mov.b		%d1,FPSR_QBYTE(%a6)
	cmpi.b		DTAG(%a6),&DENORM
	bne.b		smod_nrm
	lea		DST(%a1),%a0
	mov.l		(%sp)+,%d0
	bra		t_resdnrm
smod_nrm:
	fmov.l		(%sp)+,%fpcr
	fmov.x		DST(%a1),%fp0
	tst.b		DST_EX(%a1)
	bmi.b		smod_nrm_neg
	rts

smod_nrm_neg:
	mov.b		&neg_bmask,FPSR_CC(%a6)	# set 'N' code
	rts

#########################################################################
	global		srem_snorm
	global		srem_sdnrm
srem_sdnrm:
srem_snorm:
	mov.b		DTAG(%a6),%d1
	beq.l		srem
	cmpi.b		%d1,&ZERO
	beq.w		srem_zro
	cmpi.b		%d1,&INF
	beq.l		t_operr
	cmpi.b		%d1,&DENORM
	beq.l		srem
	bra.l		dst_qnan

	global		srem_szero
srem_szero:
	mov.b		DTAG(%a6),%d1
	beq.l		t_operr
	cmpi.b		%d1,&ZERO
	beq.l		t_operr
	cmpi.b		%d1,&INF
	beq.l		t_operr
	cmpi.b		%d1,&DENORM
	beq.l		t_operr
	bra.l		dst_qnan

	global		srem_sinf
srem_sinf:
	mov.b		DTAG(%a6),%d1
	beq.w		srem_fpn
	cmpi.b		%d1,&ZERO
	beq.w		srem_zro
	cmpi.b		%d1,&INF
	beq.l		t_operr
	cmpi.b		%d1,&DENORM
	beq.l		srem_fpn
	bra.l		dst_qnan

#########################################################################

	global		sscale_snorm
	global		sscale_sdnrm
sscale_snorm:
sscale_sdnrm:
	mov.b		DTAG(%a6),%d1
	beq.l		sscale
	cmpi.b		%d1,&ZERO
	beq.l		dst_zero
	cmpi.b		%d1,&INF
	beq.l		dst_inf
	cmpi.b		%d1,&DENORM
	beq.l		sscale
	bra.l		dst_qnan

	global		sscale_szero
sscale_szero:
	mov.b		DTAG(%a6),%d1
	beq.l		sscale
	cmpi.b		%d1,&ZERO
	beq.l		dst_zero
	cmpi.b		%d1,&INF
	beq.l		dst_inf
	cmpi.b		%d1,&DENORM
	beq.l		sscale
	bra.l		dst_qnan

	global		sscale_sinf
sscale_sinf:
	mov.b		DTAG(%a6),%d1
	beq.l		t_operr
	cmpi.b		%d1,&QNAN
	beq.l		dst_qnan
	bra.l		t_operr

########################################################################

	global		sop_sqnan
sop_sqnan:
	mov.b		DTAG(%a6),%d1
	cmpi.b		%d1,&QNAN
	beq.l		dst_qnan
	bra.l		src_qnan

#########################################################################
# norm(): normalize the mantissa of an extended precision input. the	#
#	  input operand should not be normalized already.		#
#									#
# XDEF ****************************************************************	#
#	norm()								#
#									#
# XREF **************************************************************** #
#	none								#
#									#
# INPUT *************************************************************** #
#	a0 = pointer fp extended precision operand to normalize		#
#									#
# OUTPUT ************************************************************** #
#	d0 = number of bit positions the mantissa was shifted		#
#	a0 = the input operand's mantissa is normalized; the exponent	#
#	     is unchanged.						#
#									#
#########################################################################
	global		norm
norm:
	mov.l		%d2, -(%sp)		# create some temp regs
	mov.l		%d3, -(%sp)

	mov.l		FTEMP_HI(%a0), %d0	# load hi(mantissa)
	mov.l		FTEMP_LO(%a0), %d1	# load lo(mantissa)

	bfffo		%d0{&0:&32}, %d2	# how many places to shift?
	beq.b		norm_lo			# hi(man) is all zeroes!

norm_hi:
	lsl.l		%d2, %d0		# left shift hi(man)
	bfextu		%d1{&0:%d2}, %d3	# extract lo bits

	or.l		%d3, %d0		# create hi(man)
	lsl.l		%d2, %d1		# create lo(man)

	mov.l		%d0, FTEMP_HI(%a0)	# store new hi(man)
	mov.l		%d1, FTEMP_LO(%a0)	# store new lo(man)

	mov.l		%d2, %d0		# return shift amount

	mov.l		(%sp)+, %d3		# restore temp regs
	mov.l		(%sp)+, %d2

	rts

norm_lo:
	bfffo		%d1{&0:&32}, %d2	# how many places to shift?
	lsl.l		%d2, %d1		# shift lo(man)
	add.l		&32, %d2		# add 32 to shft amount

	mov.l		%d1, FTEMP_HI(%a0)	# store hi(man)
	clr.l		FTEMP_LO(%a0)		# lo(man) is now zero

	mov.l		%d2, %d0		# return shift amount

	mov.l		(%sp)+, %d3		# restore temp regs
	mov.l		(%sp)+, %d2

	rts

#########################################################################
# unnorm_fix(): - changes an UNNORM to one of NORM, DENORM, or ZERO	#
#		- returns corresponding optype tag			#
#									#
# XDEF ****************************************************************	#
#	unnorm_fix()							#
#									#
# XREF **************************************************************** #
#	norm() - normalize the mantissa					#
#									#
# INPUT *************************************************************** #
#	a0 = pointer to unnormalized extended precision number		#
#									#
# OUTPUT ************************************************************** #
#	d0 = optype tag - is corrected to one of NORM, DENORM, or ZERO	#
#	a0 = input operand has been converted to a norm, denorm, or	#
#	     zero; both the exponent and mantissa are changed.		#
#									#
#########################################################################

	global		unnorm_fix
unnorm_fix:
	bfffo		FTEMP_HI(%a0){&0:&32}, %d0 # how many shifts are needed?
	bne.b		unnorm_shift		# hi(man) is not all zeroes

#
# hi(man) is all zeroes so see if any bits in lo(man) are set
#
unnorm_chk_lo:
	bfffo		FTEMP_LO(%a0){&0:&32}, %d0 # is operand really a zero?
	beq.w		unnorm_zero		# yes

	add.w		&32, %d0		# no; fix shift distance

#
# d0 = # shifts needed for complete normalization
#
unnorm_shift:
	clr.l		%d1			# clear top word
	mov.w		FTEMP_EX(%a0), %d1	# extract exponent
	and.w		&0x7fff, %d1		# strip off sgn

	cmp.w		%d0, %d1		# will denorm push exp < 0?
	bgt.b		unnorm_nrm_zero		# yes; denorm only until exp = 0

#
# exponent would not go < 0. therefore, number stays normalized
#
	sub.w		%d0, %d1		# shift exponent value
	mov.w		FTEMP_EX(%a0), %d0	# load old exponent
	and.w		&0x8000, %d0		# save old sign
	or.w		%d0, %d1		# {sgn,new exp}
	mov.w		%d1, FTEMP_EX(%a0)	# insert new exponent

	bsr.l		norm			# normalize UNNORM

	mov.b		&NORM, %d0		# return new optype tag
	rts

#
# exponent would go < 0, so only denormalize until exp = 0
#
unnorm_nrm_zero:
	cmp.b		%d1, &32		# is exp <= 32?
	bgt.b		unnorm_nrm_zero_lrg	# no; go handle large exponent

	bfextu		FTEMP_HI(%a0){%d1:&32}, %d0 # extract new hi(man)
	mov.l		%d0, FTEMP_HI(%a0)	# save new hi(man)

	mov.l		FTEMP_LO(%a0), %d0	# fetch old lo(man)
	lsl.l		%d1, %d0		# extract new lo(man)
	mov.l		%d0, FTEMP_LO(%a0)	# save new lo(man)

	and.w		&0x8000, FTEMP_EX(%a0)	# set exp = 0

	mov.b		&DENORM, %d0		# return new optype tag
	rts

#
# only mantissa bits set are in lo(man)
#
unnorm_nrm_zero_lrg:
	sub.w		&32, %d1		# adjust shft amt by 32

	mov.l		FTEMP_LO(%a0), %d0	# fetch old lo(man)
	lsl.l		%d1, %d0		# left shift lo(man)

	mov.l		%d0, FTEMP_HI(%a0)	# store new hi(man)
	clr.l		FTEMP_LO(%a0)		# lo(man) = 0

	and.w		&0x8000, FTEMP_EX(%a0)	# set exp = 0

	mov.b		&DENORM, %d0		# return new optype tag
	rts

#
# whole mantissa is zero so this UNNORM is actually a zero
#
unnorm_zero:
	and.w		&0x8000, FTEMP_EX(%a0)	# force exponent to zero

	mov.b		&ZERO, %d0		# fix optype tag
	rts
