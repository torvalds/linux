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
# freal.s:
#	This file is appended to the top of the 060FPSP package
# and contains the entry points into the package. The user, in
# effect, branches to one of the branch table entries located
# after _060FPSP_TABLE.
#	Also, subroutine stubs exist in this file (_fpsp_done for
# example) that are referenced by the FPSP package itself in order
# to call a given routine. The stub routine actually performs the
# callout. The FPSP code does a "bsr" to the stub routine. This
# extra layer of hierarchy adds a slight performance penalty but
# it makes the FPSP code easier to read and more mainatinable.
#

set	_off_bsun,	0x00
set	_off_snan,	0x04
set	_off_operr,	0x08
set	_off_ovfl,	0x0c
set	_off_unfl,	0x10
set	_off_dz,	0x14
set	_off_inex,	0x18
set	_off_fline,	0x1c
set	_off_fpu_dis,	0x20
set	_off_trap,	0x24
set	_off_trace,	0x28
set	_off_access,	0x2c
set	_off_done,	0x30

set	_off_imr,	0x40
set	_off_dmr,	0x44
set	_off_dmw,	0x48
set	_off_irw,	0x4c
set	_off_irl,	0x50
set	_off_drb,	0x54
set	_off_drw,	0x58
set	_off_drl,	0x5c
set	_off_dwb,	0x60
set	_off_dww,	0x64
set	_off_dwl,	0x68

_060FPSP_TABLE:

###############################################################

# Here's the table of ENTRY POINTS for those linking the package.
	bra.l		_fpsp_snan
	short		0x0000
	bra.l		_fpsp_operr
	short		0x0000
	bra.l		_fpsp_ovfl
	short		0x0000
	bra.l		_fpsp_unfl
	short		0x0000
	bra.l		_fpsp_dz
	short		0x0000
	bra.l		_fpsp_inex
	short		0x0000
	bra.l		_fpsp_fline
	short		0x0000
	bra.l		_fpsp_unsupp
	short		0x0000
	bra.l		_fpsp_effadd
	short		0x0000

	space		56

###############################################################
	global		_fpsp_done
_fpsp_done:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_done,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_real_ovfl
_real_ovfl:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_ovfl,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_real_unfl
_real_unfl:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_unfl,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_real_inex
_real_inex:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_inex,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_real_bsun
_real_bsun:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_bsun,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_real_operr
_real_operr:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_operr,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_real_snan
_real_snan:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_snan,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_real_dz
_real_dz:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_dz,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_real_fline
_real_fline:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_fline,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_real_fpu_disabled
_real_fpu_disabled:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_fpu_dis,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_real_trap
_real_trap:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_trap,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_real_trace
_real_trace:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_trace,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_real_access
_real_access:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_access,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

#######################################

	global		_imem_read
_imem_read:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_imr,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_dmem_read
_dmem_read:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_dmr,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_dmem_write
_dmem_write:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_dmw,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_imem_read_word
_imem_read_word:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_irw,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_imem_read_long
_imem_read_long:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_irl,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_dmem_read_byte
_dmem_read_byte:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_drb,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_dmem_read_word
_dmem_read_word:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_drw,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_dmem_read_long
_dmem_read_long:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_drl,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_dmem_write_byte
_dmem_write_byte:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_dwb,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_dmem_write_word
_dmem_write_word:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_dww,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

	global		_dmem_write_long
_dmem_write_long:
	mov.l		%d0,-(%sp)
	mov.l		(_060FPSP_TABLE-0x80+_off_dwl,%pc),%d0
	pea.l		(_060FPSP_TABLE-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

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
# XDEF ****************************************************************	#
#	_fpsp_ovfl(): 060FPSP entry point for FP Overflow exception.	#
#									#
#	This handler should be the first code executed upon taking the	#
#	FP Overflow exception in an operating system.			#
#									#
# XREF ****************************************************************	#
#	_imem_read_long() - read instruction longword			#
#	fix_skewed_ops() - adjust src operand in fsave frame		#
#	set_tag_x() - determine optype of src/dst operands		#
#	store_fpreg() - store opclass 0 or 2 result to FP regfile	#
#	unnorm_fix() - change UNNORM operands to NORM or ZERO		#
#	load_fpn2() - load dst operand from FP regfile			#
#	fout() - emulate an opclass 3 instruction			#
#	tbl_unsupp - add of table of emulation routines for opclass 0,2	#
#	_fpsp_done() - "callout" for 060FPSP exit (all work done!)	#
#	_real_ovfl() - "callout" for Overflow exception enabled code	#
#	_real_inex() - "callout" for Inexact exception enabled code	#
#	_real_trace() - "callout" for Trace exception code		#
#									#
# INPUT ***************************************************************	#
#	- The system stack contains the FP Ovfl exception stack frame	#
#	- The fsave frame contains the source operand			#
#									#
# OUTPUT **************************************************************	#
#	Overflow Exception enabled:					#
#	- The system stack is unchanged					#
#	- The fsave frame contains the adjusted src op for opclass 0,2	#
#	Overflow Exception disabled:					#
#	- The system stack is unchanged					#
#	- The "exception present" flag in the fsave frame is cleared	#
#									#
# ALGORITHM ***********************************************************	#
#	On the 060, if an FP overflow is present as the result of any	#
# instruction, the 060 will take an overflow exception whether the	#
# exception is enabled or disabled in the FPCR. For the disabled case,	#
# This handler emulates the instruction to determine what the correct	#
# default result should be for the operation. This default result is	#
# then stored in either the FP regfile, data regfile, or memory.	#
# Finally, the handler exits through the "callout" _fpsp_done()		#
# denoting that no exceptional conditions exist within the machine.	#
#	If the exception is enabled, then this handler must create the	#
# exceptional operand and plave it in the fsave state frame, and store	#
# the default result (only if the instruction is opclass 3). For	#
# exceptions enabled, this handler must exit through the "callout"	#
# _real_ovfl() so that the operating system enabled overflow handler	#
# can handle this case.							#
#	Two other conditions exist. First, if overflow was disabled	#
# but the inexact exception was enabled, this handler must exit		#
# through the "callout" _real_inex() regardless of whether the result	#
# was inexact.								#
#	Also, in the case of an opclass three instruction where		#
# overflow was disabled and the trace exception was enabled, this	#
# handler must exit through the "callout" _real_trace().		#
#									#
#########################################################################

	global		_fpsp_ovfl
_fpsp_ovfl:

#$#	sub.l		&24,%sp			# make room for src/dst

	link.w		%a6,&-LOCAL_SIZE	# init stack frame

	fsave		FP_SRC(%a6)		# grab the "busy" frame

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,%fpiar,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FPREGS(%a6)	# save fp0-fp1 on stack

# the FPIAR holds the "current PC" of the faulting instruction
	mov.l		USER_FPIAR(%a6),EXC_EXTWPTR(%a6)
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch the instruction words
	mov.l		%d0,EXC_OPWORD(%a6)

##############################################################################

	btst		&0x5,EXC_CMDREG(%a6)	# is instr an fmove out?
	bne.w		fovfl_out


	lea		FP_SRC(%a6),%a0		# pass: ptr to src op
	bsr.l		fix_skewed_ops		# fix src op

# since, I believe, only NORMs and DENORMs can come through here,
# maybe we can avoid the subroutine call.
	lea		FP_SRC(%a6),%a0		# pass: ptr to src op
	bsr.l		set_tag_x		# tag the operand type
	mov.b		%d0,STAG(%a6)		# maybe NORM,DENORM

# bit five of the fp extension word separates the monadic and dyadic operations
# that can pass through fpsp_ovfl(). remember that fcmp, ftst, and fsincos
# will never take this exception.
	btst		&0x5,1+EXC_CMDREG(%a6)	# is operation monadic or dyadic?
	beq.b		fovfl_extract		# monadic

	bfextu		EXC_CMDREG(%a6){&6:&3},%d0 # dyadic; load dst reg
	bsr.l		load_fpn2		# load dst into FP_DST

	lea		FP_DST(%a6),%a0		# pass: ptr to dst op
	bsr.l		set_tag_x		# tag the operand type
	cmpi.b		%d0,&UNNORM		# is operand an UNNORM?
	bne.b		fovfl_op2_done		# no
	bsr.l		unnorm_fix		# yes; convert to NORM,DENORM,or ZERO
fovfl_op2_done:
	mov.b		%d0,DTAG(%a6)		# save dst optype tag

fovfl_extract:

#$#	mov.l		FP_SRC_EX(%a6),TRAP_SRCOP_EX(%a6)
#$#	mov.l		FP_SRC_HI(%a6),TRAP_SRCOP_HI(%a6)
#$#	mov.l		FP_SRC_LO(%a6),TRAP_SRCOP_LO(%a6)
#$#	mov.l		FP_DST_EX(%a6),TRAP_DSTOP_EX(%a6)
#$#	mov.l		FP_DST_HI(%a6),TRAP_DSTOP_HI(%a6)
#$#	mov.l		FP_DST_LO(%a6),TRAP_DSTOP_LO(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd prec/mode

	mov.b		1+EXC_CMDREG(%a6),%d1
	andi.w		&0x007f,%d1		# extract extension

	andi.l		&0x00ff01ff,USER_FPSR(%a6) # zero all but accured field

	fmov.l		&0x0,%fpcr		# zero current control regs
	fmov.l		&0x0,%fpsr

	lea		FP_SRC(%a6),%a0
	lea		FP_DST(%a6),%a1

# maybe we can make these entry points ONLY the OVFL entry points of each routine.
	mov.l		(tbl_unsupp.l,%pc,%d1.w*4),%d1 # fetch routine addr
	jsr		(tbl_unsupp.l,%pc,%d1.l*1)

# the operation has been emulated. the result is in fp0.
# the EXOP, if an exception occurred, is in fp1.
# we must save the default result regardless of whether
# traps are enabled or disabled.
	bfextu		EXC_CMDREG(%a6){&6:&3},%d0
	bsr.l		store_fpreg

# the exceptional possibilities we have left ourselves with are ONLY overflow
# and inexact. and, the inexact is such that overflow occurred and was disabled
# but inexact was enabled.
	btst		&ovfl_bit,FPCR_ENABLE(%a6)
	bne.b		fovfl_ovfl_on

	btst		&inex2_bit,FPCR_ENABLE(%a6)
	bne.b		fovfl_inex_on

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6
#$#	add.l		&24,%sp
	bra.l		_fpsp_done

# overflow is enabled AND overflow, of course, occurred. so, we have the EXOP
# in fp1. now, simply jump to _real_ovfl()!
fovfl_ovfl_on:
	fmovm.x		&0x40,FP_SRC(%a6)	# save EXOP (fp1) to stack

	mov.w		&0xe005,2+FP_SRC(%a6)	# save exc status

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)		# do this after fmovm,other f<op>s!

	unlk		%a6

	bra.l		_real_ovfl

# overflow occurred but is disabled. meanwhile, inexact is enabled. therefore,
# we must jump to real_inex().
fovfl_inex_on:

	fmovm.x		&0x40,FP_SRC(%a6)	# save EXOP (fp1) to stack

	mov.b		&0xc4,1+EXC_VOFF(%a6)	# vector offset = 0xc4
	mov.w		&0xe001,2+FP_SRC(%a6)	# save exc status

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)		# do this after fmovm,other f<op>s!

	unlk		%a6

	bra.l		_real_inex

########################################################################
fovfl_out:


#$#	mov.l		FP_SRC_EX(%a6),TRAP_SRCOP_EX(%a6)
#$#	mov.l		FP_SRC_HI(%a6),TRAP_SRCOP_HI(%a6)
#$#	mov.l		FP_SRC_LO(%a6),TRAP_SRCOP_LO(%a6)

# the src operand is definitely a NORM(!), so tag it as such
	mov.b		&NORM,STAG(%a6)		# set src optype tag

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd prec/mode

	and.l		&0xffff00ff,USER_FPSR(%a6) # zero all but accured field

	fmov.l		&0x0,%fpcr		# zero current control regs
	fmov.l		&0x0,%fpsr

	lea		FP_SRC(%a6),%a0		# pass ptr to src operand

	bsr.l		fout

	btst		&ovfl_bit,FPCR_ENABLE(%a6)
	bne.w		fovfl_ovfl_on

	btst		&inex2_bit,FPCR_ENABLE(%a6)
	bne.w		fovfl_inex_on

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6
#$#	add.l		&24,%sp

	btst		&0x7,(%sp)		# is trace on?
	beq.l		_fpsp_done		# no

	fmov.l		%fpiar,0x8(%sp)		# "Current PC" is in FPIAR
	mov.w		&0x2024,0x6(%sp)	# stk fmt = 0x2; voff = 0x024
	bra.l		_real_trace

#########################################################################
# XDEF ****************************************************************	#
#	_fpsp_unfl(): 060FPSP entry point for FP Underflow exception.	#
#									#
#	This handler should be the first code executed upon taking the	#
#	FP Underflow exception in an operating system.			#
#									#
# XREF ****************************************************************	#
#	_imem_read_long() - read instruction longword			#
#	fix_skewed_ops() - adjust src operand in fsave frame		#
#	set_tag_x() - determine optype of src/dst operands		#
#	store_fpreg() - store opclass 0 or 2 result to FP regfile	#
#	unnorm_fix() - change UNNORM operands to NORM or ZERO		#
#	load_fpn2() - load dst operand from FP regfile			#
#	fout() - emulate an opclass 3 instruction			#
#	tbl_unsupp - add of table of emulation routines for opclass 0,2	#
#	_fpsp_done() - "callout" for 060FPSP exit (all work done!)	#
#	_real_ovfl() - "callout" for Overflow exception enabled code	#
#	_real_inex() - "callout" for Inexact exception enabled code	#
#	_real_trace() - "callout" for Trace exception code		#
#									#
# INPUT ***************************************************************	#
#	- The system stack contains the FP Unfl exception stack frame	#
#	- The fsave frame contains the source operand			#
#									#
# OUTPUT **************************************************************	#
#	Underflow Exception enabled:					#
#	- The system stack is unchanged					#
#	- The fsave frame contains the adjusted src op for opclass 0,2	#
#	Underflow Exception disabled:					#
#	- The system stack is unchanged					#
#	- The "exception present" flag in the fsave frame is cleared	#
#									#
# ALGORITHM ***********************************************************	#
#	On the 060, if an FP underflow is present as the result of any	#
# instruction, the 060 will take an underflow exception whether the	#
# exception is enabled or disabled in the FPCR. For the disabled case,	#
# This handler emulates the instruction to determine what the correct	#
# default result should be for the operation. This default result is	#
# then stored in either the FP regfile, data regfile, or memory.	#
# Finally, the handler exits through the "callout" _fpsp_done()		#
# denoting that no exceptional conditions exist within the machine.	#
#	If the exception is enabled, then this handler must create the	#
# exceptional operand and plave it in the fsave state frame, and store	#
# the default result (only if the instruction is opclass 3). For	#
# exceptions enabled, this handler must exit through the "callout"	#
# _real_unfl() so that the operating system enabled overflow handler	#
# can handle this case.							#
#	Two other conditions exist. First, if underflow was disabled	#
# but the inexact exception was enabled and the result was inexact,	#
# this handler must exit through the "callout" _real_inex().		#
# was inexact.								#
#	Also, in the case of an opclass three instruction where		#
# underflow was disabled and the trace exception was enabled, this	#
# handler must exit through the "callout" _real_trace().		#
#									#
#########################################################################

	global		_fpsp_unfl
_fpsp_unfl:

#$#	sub.l		&24,%sp			# make room for src/dst

	link.w		%a6,&-LOCAL_SIZE	# init stack frame

	fsave		FP_SRC(%a6)		# grab the "busy" frame

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,%fpiar,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FPREGS(%a6)	# save fp0-fp1 on stack

# the FPIAR holds the "current PC" of the faulting instruction
	mov.l		USER_FPIAR(%a6),EXC_EXTWPTR(%a6)
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch the instruction words
	mov.l		%d0,EXC_OPWORD(%a6)

##############################################################################

	btst		&0x5,EXC_CMDREG(%a6)	# is instr an fmove out?
	bne.w		funfl_out


	lea		FP_SRC(%a6),%a0		# pass: ptr to src op
	bsr.l		fix_skewed_ops		# fix src op

	lea		FP_SRC(%a6),%a0		# pass: ptr to src op
	bsr.l		set_tag_x		# tag the operand type
	mov.b		%d0,STAG(%a6)		# maybe NORM,DENORM

# bit five of the fp ext word separates the monadic and dyadic operations
# that can pass through fpsp_unfl(). remember that fcmp, and ftst
# will never take this exception.
	btst		&0x5,1+EXC_CMDREG(%a6)	# is op monadic or dyadic?
	beq.b		funfl_extract		# monadic

# now, what's left that's not dyadic is fsincos. we can distinguish it
# from all dyadics by the '0110xxx pattern
	btst		&0x4,1+EXC_CMDREG(%a6)	# is op an fsincos?
	bne.b		funfl_extract		# yes

	bfextu		EXC_CMDREG(%a6){&6:&3},%d0 # dyadic; load dst reg
	bsr.l		load_fpn2		# load dst into FP_DST

	lea		FP_DST(%a6),%a0		# pass: ptr to dst op
	bsr.l		set_tag_x		# tag the operand type
	cmpi.b		%d0,&UNNORM		# is operand an UNNORM?
	bne.b		funfl_op2_done		# no
	bsr.l		unnorm_fix		# yes; convert to NORM,DENORM,or ZERO
funfl_op2_done:
	mov.b		%d0,DTAG(%a6)		# save dst optype tag

funfl_extract:

#$#	mov.l		FP_SRC_EX(%a6),TRAP_SRCOP_EX(%a6)
#$#	mov.l		FP_SRC_HI(%a6),TRAP_SRCOP_HI(%a6)
#$#	mov.l		FP_SRC_LO(%a6),TRAP_SRCOP_LO(%a6)
#$#	mov.l		FP_DST_EX(%a6),TRAP_DSTOP_EX(%a6)
#$#	mov.l		FP_DST_HI(%a6),TRAP_DSTOP_HI(%a6)
#$#	mov.l		FP_DST_LO(%a6),TRAP_DSTOP_LO(%a6)

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd prec/mode

	mov.b		1+EXC_CMDREG(%a6),%d1
	andi.w		&0x007f,%d1		# extract extension

	andi.l		&0x00ff01ff,USER_FPSR(%a6)

	fmov.l		&0x0,%fpcr		# zero current control regs
	fmov.l		&0x0,%fpsr

	lea		FP_SRC(%a6),%a0
	lea		FP_DST(%a6),%a1

# maybe we can make these entry points ONLY the OVFL entry points of each routine.
	mov.l		(tbl_unsupp.l,%pc,%d1.w*4),%d1 # fetch routine addr
	jsr		(tbl_unsupp.l,%pc,%d1.l*1)

	bfextu		EXC_CMDREG(%a6){&6:&3},%d0
	bsr.l		store_fpreg

# The `060 FPU multiplier hardware is such that if the result of a
# multiply operation is the smallest possible normalized number
# (0x00000000_80000000_00000000), then the machine will take an
# underflow exception. Since this is incorrect, we need to check
# if our emulation, after re-doing the operation, decided that
# no underflow was called for. We do these checks only in
# funfl_{unfl,inex}_on() because w/ both exceptions disabled, this
# special case will simply exit gracefully with the correct result.

# the exceptional possibilities we have left ourselves with are ONLY overflow
# and inexact. and, the inexact is such that overflow occurred and was disabled
# but inexact was enabled.
	btst		&unfl_bit,FPCR_ENABLE(%a6)
	bne.b		funfl_unfl_on

funfl_chkinex:
	btst		&inex2_bit,FPCR_ENABLE(%a6)
	bne.b		funfl_inex_on

funfl_exit:
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6
#$#	add.l		&24,%sp
	bra.l		_fpsp_done

# overflow is enabled AND overflow, of course, occurred. so, we have the EXOP
# in fp1 (don't forget to save fp0). what to do now?
# well, we simply have to get to go to _real_unfl()!
funfl_unfl_on:

# The `060 FPU multiplier hardware is such that if the result of a
# multiply operation is the smallest possible normalized number
# (0x00000000_80000000_00000000), then the machine will take an
# underflow exception. Since this is incorrect, we check here to see
# if our emulation, after re-doing the operation, decided that
# no underflow was called for.
	btst		&unfl_bit,FPSR_EXCEPT(%a6)
	beq.w		funfl_chkinex

funfl_unfl_on2:
	fmovm.x		&0x40,FP_SRC(%a6)	# save EXOP (fp1) to stack

	mov.w		&0xe003,2+FP_SRC(%a6)	# save exc status

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)		# do this after fmovm,other f<op>s!

	unlk		%a6

	bra.l		_real_unfl

# undeflow occurred but is disabled. meanwhile, inexact is enabled. therefore,
# we must jump to real_inex().
funfl_inex_on:

# The `060 FPU multiplier hardware is such that if the result of a
# multiply operation is the smallest possible normalized number
# (0x00000000_80000000_00000000), then the machine will take an
# underflow exception.
# But, whether bogus or not, if inexact is enabled AND it occurred,
# then we have to branch to real_inex.

	btst		&inex2_bit,FPSR_EXCEPT(%a6)
	beq.w		funfl_exit

funfl_inex_on2:

	fmovm.x		&0x40,FP_SRC(%a6)	# save EXOP to stack

	mov.b		&0xc4,1+EXC_VOFF(%a6)	# vector offset = 0xc4
	mov.w		&0xe001,2+FP_SRC(%a6)	# save exc status

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)		# do this after fmovm,other f<op>s!

	unlk		%a6

	bra.l		_real_inex

#######################################################################
funfl_out:


#$#	mov.l		FP_SRC_EX(%a6),TRAP_SRCOP_EX(%a6)
#$#	mov.l		FP_SRC_HI(%a6),TRAP_SRCOP_HI(%a6)
#$#	mov.l		FP_SRC_LO(%a6),TRAP_SRCOP_LO(%a6)

# the src operand is definitely a NORM(!), so tag it as such
	mov.b		&NORM,STAG(%a6)		# set src optype tag

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd prec/mode

	and.l		&0xffff00ff,USER_FPSR(%a6) # zero all but accured field

	fmov.l		&0x0,%fpcr		# zero current control regs
	fmov.l		&0x0,%fpsr

	lea		FP_SRC(%a6),%a0		# pass ptr to src operand

	bsr.l		fout

	btst		&unfl_bit,FPCR_ENABLE(%a6)
	bne.w		funfl_unfl_on2

	btst		&inex2_bit,FPCR_ENABLE(%a6)
	bne.w		funfl_inex_on2

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6
#$#	add.l		&24,%sp

	btst		&0x7,(%sp)		# is trace on?
	beq.l		_fpsp_done		# no

	fmov.l		%fpiar,0x8(%sp)		# "Current PC" is in FPIAR
	mov.w		&0x2024,0x6(%sp)	# stk fmt = 0x2; voff = 0x024
	bra.l		_real_trace

#########################################################################
# XDEF ****************************************************************	#
#	_fpsp_unsupp(): 060FPSP entry point for FP "Unimplemented	#
#		        Data Type" exception.				#
#									#
#	This handler should be the first code executed upon taking the	#
#	FP Unimplemented Data Type exception in an operating system.	#
#									#
# XREF ****************************************************************	#
#	_imem_read_{word,long}() - read instruction word/longword	#
#	fix_skewed_ops() - adjust src operand in fsave frame		#
#	set_tag_x() - determine optype of src/dst operands		#
#	store_fpreg() - store opclass 0 or 2 result to FP regfile	#
#	unnorm_fix() - change UNNORM operands to NORM or ZERO		#
#	load_fpn2() - load dst operand from FP regfile			#
#	load_fpn1() - load src operand from FP regfile			#
#	fout() - emulate an opclass 3 instruction			#
#	tbl_unsupp - add of table of emulation routines for opclass 0,2	#
#	_real_inex() - "callout" to operating system inexact handler	#
#	_fpsp_done() - "callout" for exit; work all done		#
#	_real_trace() - "callout" for Trace enabled exception		#
#	funimp_skew() - adjust fsave src ops to "incorrect" value	#
#	_real_snan() - "callout" for SNAN exception			#
#	_real_operr() - "callout" for OPERR exception			#
#	_real_ovfl() - "callout" for OVFL exception			#
#	_real_unfl() - "callout" for UNFL exception			#
#	get_packed() - fetch packed operand from memory			#
#									#
# INPUT ***************************************************************	#
#	- The system stack contains the "Unimp Data Type" stk frame	#
#	- The fsave frame contains the ssrc op (for UNNORM/DENORM)	#
#									#
# OUTPUT **************************************************************	#
#	If Inexact exception (opclass 3):				#
#	- The system stack is changed to an Inexact exception stk frame	#
#	If SNAN exception (opclass 3):					#
#	- The system stack is changed to an SNAN exception stk frame	#
#	If OPERR exception (opclass 3):					#
#	- The system stack is changed to an OPERR exception stk frame	#
#	If OVFL exception (opclass 3):					#
#	- The system stack is changed to an OVFL exception stk frame	#
#	If UNFL exception (opclass 3):					#
#	- The system stack is changed to an UNFL exception stack frame	#
#	If Trace exception enabled:					#
#	- The system stack is changed to a Trace exception stack frame	#
#	Else: (normal case)						#
#	- Correct result has been stored as appropriate			#
#									#
# ALGORITHM ***********************************************************	#
#	Two main instruction types can enter here: (1) DENORM or UNNORM	#
# unimplemented data types. These can be either opclass 0,2 or 3	#
# instructions, and (2) PACKED unimplemented data format instructions	#
# also of opclasses 0,2, or 3.						#
#	For UNNORM/DENORM opclass 0 and 2, the handler fetches the src	#
# operand from the fsave state frame and the dst operand (if dyadic)	#
# from the FP register file. The instruction is then emulated by	#
# choosing an emulation routine from a table of routines indexed by	#
# instruction type. Once the instruction has been emulated and result	#
# saved, then we check to see if any enabled exceptions resulted from	#
# instruction emulation. If none, then we exit through the "callout"	#
# _fpsp_done(). If there is an enabled FP exception, then we insert	#
# this exception into the FPU in the fsave state frame and then exit	#
# through _fpsp_done().							#
#	PACKED opclass 0 and 2 is similar in how the instruction is	#
# emulated and exceptions handled. The differences occur in how the	#
# handler loads the packed op (by calling get_packed() routine) and	#
# by the fact that a Trace exception could be pending for PACKED ops.	#
# If a Trace exception is pending, then the current exception stack	#
# frame is changed to a Trace exception stack frame and an exit is	#
# made through _real_trace().						#
#	For UNNORM/DENORM opclass 3, the actual move out to memory is	#
# performed by calling the routine fout(). If no exception should occur	#
# as the result of emulation, then an exit either occurs through	#
# _fpsp_done() or through _real_trace() if a Trace exception is pending	#
# (a Trace stack frame must be created here, too). If an FP exception	#
# should occur, then we must create an exception stack frame of that	#
# type and jump to either _real_snan(), _real_operr(), _real_inex(),	#
# _real_unfl(), or _real_ovfl() as appropriate. PACKED opclass 3	#
# emulation is performed in a similar manner.				#
#									#
#########################################################################

#
# (1) DENORM and UNNORM (unimplemented) data types:
#
#				post-instruction
#				*****************
#				*      EA	*
#	 pre-instruction	*		*
#	*****************	*****************
#	* 0x0 *  0x0dc  *	* 0x3 *  0x0dc  *
#	*****************	*****************
#	*     Next	*	*     Next	*
#	*      PC	*	*      PC	*
#	*****************	*****************
#	*      SR	*	*      SR	*
#	*****************	*****************
#
# (2) PACKED format (unsupported) opclasses two and three:
#	*****************
#	*      EA	*
#	*		*
#	*****************
#	* 0x2 *  0x0dc	*
#	*****************
#	*     Next	*
#	*      PC	*
#	*****************
#	*      SR	*
#	*****************
#
	global		_fpsp_unsupp
_fpsp_unsupp:

	link.w		%a6,&-LOCAL_SIZE	# init stack frame

	fsave		FP_SRC(%a6)		# save fp state

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,%fpiar,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FPREGS(%a6)	# save fp0-fp1 on stack

	btst		&0x5,EXC_SR(%a6)	# user or supervisor mode?
	bne.b		fu_s
fu_u:
	mov.l		%usp,%a0		# fetch user stack pointer
	mov.l		%a0,EXC_A7(%a6)		# save on stack
	bra.b		fu_cont
# if the exception is an opclass zero or two unimplemented data type
# exception, then the a7' calculated here is wrong since it doesn't
# stack an ea. however, we don't need an a7' for this case anyways.
fu_s:
	lea		0x4+EXC_EA(%a6),%a0	# load old a7'
	mov.l		%a0,EXC_A7(%a6)		# save on stack

fu_cont:

# the FPIAR holds the "current PC" of the faulting instruction
# the FPIAR should be set correctly for ALL exceptions passing through
# this point.
	mov.l		USER_FPIAR(%a6),EXC_EXTWPTR(%a6)
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch the instruction words
	mov.l		%d0,EXC_OPWORD(%a6)	# store OPWORD and EXTWORD

############################

	clr.b		SPCOND_FLG(%a6)		# clear special condition flag

# Separate opclass three (fpn-to-mem) ops since they have a different
# stack frame and protocol.
	btst		&0x5,EXC_CMDREG(%a6)	# is it an fmove out?
	bne.w		fu_out			# yes

# Separate packed opclass two instructions.
	bfextu		EXC_CMDREG(%a6){&0:&6},%d0
	cmpi.b		%d0,&0x13
	beq.w		fu_in_pack


# I'm not sure at this point what FPSR bits are valid for this instruction.
# so, since the emulation routines re-create them anyways, zero exception field
	andi.l		&0x00ff00ff,USER_FPSR(%a6) # zero exception field

	fmov.l		&0x0,%fpcr		# zero current control regs
	fmov.l		&0x0,%fpsr

# Opclass two w/ memory-to-fpn operation will have an incorrect extended
# precision format if the src format was single or double and the
# source data type was an INF, NAN, DENORM, or UNNORM
	lea		FP_SRC(%a6),%a0		# pass ptr to input
	bsr.l		fix_skewed_ops

# we don't know whether the src operand or the dst operand (or both) is the
# UNNORM or DENORM. call the function that tags the operand type. if the
# input is an UNNORM, then convert it to a NORM, DENORM, or ZERO.
	lea		FP_SRC(%a6),%a0		# pass: ptr to src op
	bsr.l		set_tag_x		# tag the operand type
	cmpi.b		%d0,&UNNORM		# is operand an UNNORM?
	bne.b		fu_op2			# no
	bsr.l		unnorm_fix		# yes; convert to NORM,DENORM,or ZERO

fu_op2:
	mov.b		%d0,STAG(%a6)		# save src optype tag

	bfextu		EXC_CMDREG(%a6){&6:&3},%d0 # dyadic; load dst reg

# bit five of the fp extension word separates the monadic and dyadic operations
# at this point
	btst		&0x5,1+EXC_CMDREG(%a6)	# is operation monadic or dyadic?
	beq.b		fu_extract		# monadic
	cmpi.b		1+EXC_CMDREG(%a6),&0x3a	# is operation an ftst?
	beq.b		fu_extract		# yes, so it's monadic, too

	bsr.l		load_fpn2		# load dst into FP_DST

	lea		FP_DST(%a6),%a0		# pass: ptr to dst op
	bsr.l		set_tag_x		# tag the operand type
	cmpi.b		%d0,&UNNORM		# is operand an UNNORM?
	bne.b		fu_op2_done		# no
	bsr.l		unnorm_fix		# yes; convert to NORM,DENORM,or ZERO
fu_op2_done:
	mov.b		%d0,DTAG(%a6)		# save dst optype tag

fu_extract:
	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# fetch rnd mode/prec

	bfextu		1+EXC_CMDREG(%a6){&1:&7},%d1 # extract extension

	lea		FP_SRC(%a6),%a0
	lea		FP_DST(%a6),%a1

	mov.l		(tbl_unsupp.l,%pc,%d1.l*4),%d1 # fetch routine addr
	jsr		(tbl_unsupp.l,%pc,%d1.l*1)

#
# Exceptions in order of precedence:
#	BSUN	: none
#	SNAN	: all dyadic ops
#	OPERR	: fsqrt(-NORM)
#	OVFL	: all except ftst,fcmp
#	UNFL	: all except ftst,fcmp
#	DZ	: fdiv
#	INEX2	: all except ftst,fcmp
#	INEX1	: none (packed doesn't go through here)
#

# we determine the highest priority exception(if any) set by the
# emulation routine that has also been enabled by the user.
	mov.b		FPCR_ENABLE(%a6),%d0	# fetch exceptions set
	bne.b		fu_in_ena		# some are enabled

fu_in_cont:
# fcmp and ftst do not store any result.
	mov.b		1+EXC_CMDREG(%a6),%d0	# fetch extension
	andi.b		&0x38,%d0		# extract bits 3-5
	cmpi.b		%d0,&0x38		# is instr fcmp or ftst?
	beq.b		fu_in_exit		# yes

	bfextu		EXC_CMDREG(%a6){&6:&3},%d0 # dyadic; load dst reg
	bsr.l		store_fpreg		# store the result

fu_in_exit:

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6

	bra.l		_fpsp_done

fu_in_ena:
	and.b		FPSR_EXCEPT(%a6),%d0	# keep only ones enabled
	bfffo		%d0{&24:&8},%d0		# find highest priority exception
	bne.b		fu_in_exc		# there is at least one set

#
# No exceptions occurred that were also enabled. Now:
#
#	if (OVFL && ovfl_disabled && inexact_enabled) {
#	    branch to _real_inex() (even if the result was exact!);
#	} else {
#	    save the result in the proper fp reg (unless the op is fcmp or ftst);
#	    return;
#	}
#
	btst		&ovfl_bit,FPSR_EXCEPT(%a6) # was overflow set?
	beq.b		fu_in_cont		# no

fu_in_ovflchk:
	btst		&inex2_bit,FPCR_ENABLE(%a6) # was inexact enabled?
	beq.b		fu_in_cont		# no
	bra.w		fu_in_exc_ovfl		# go insert overflow frame

#
# An exception occurred and that exception was enabled:
#
#	shift enabled exception field into lo byte of d0;
#	if (((INEX2 || INEX1) && inex_enabled && OVFL && ovfl_disabled) ||
#	    ((INEX2 || INEX1) && inex_enabled && UNFL && unfl_disabled)) {
#		/*
#		 * this is the case where we must call _real_inex() now or else
#		 * there will be no other way to pass it the exceptional operand
#		 */
#		call _real_inex();
#	} else {
#		restore exc state (SNAN||OPERR||OVFL||UNFL||DZ||INEX) into the FPU;
#	}
#
fu_in_exc:
	subi.l		&24,%d0			# fix offset to be 0-8
	cmpi.b		%d0,&0x6		# is exception INEX? (6)
	bne.b		fu_in_exc_exit		# no

# the enabled exception was inexact
	btst		&unfl_bit,FPSR_EXCEPT(%a6) # did disabled underflow occur?
	bne.w		fu_in_exc_unfl		# yes
	btst		&ovfl_bit,FPSR_EXCEPT(%a6) # did disabled overflow occur?
	bne.w		fu_in_exc_ovfl		# yes

# here, we insert the correct fsave status value into the fsave frame for the
# corresponding exception. the operand in the fsave frame should be the original
# src operand.
fu_in_exc_exit:
	mov.l		%d0,-(%sp)		# save d0
	bsr.l		funimp_skew		# skew sgl or dbl inputs
	mov.l		(%sp)+,%d0		# restore d0

	mov.w		(tbl_except.b,%pc,%d0.w*2),2+FP_SRC(%a6) # create exc status

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)		# restore src op

	unlk		%a6

	bra.l		_fpsp_done

tbl_except:
	short		0xe000,0xe006,0xe004,0xe005
	short		0xe003,0xe002,0xe001,0xe001

fu_in_exc_unfl:
	mov.w		&0x4,%d0
	bra.b		fu_in_exc_exit
fu_in_exc_ovfl:
	mov.w		&0x03,%d0
	bra.b		fu_in_exc_exit

# If the input operand to this operation was opclass two and a single
# or double precision denorm, inf, or nan, the operand needs to be
# "corrected" in order to have the proper equivalent extended precision
# number.
	global		fix_skewed_ops
fix_skewed_ops:
	bfextu		EXC_CMDREG(%a6){&0:&6},%d0 # extract opclass,src fmt
	cmpi.b		%d0,&0x11		# is class = 2 & fmt = sgl?
	beq.b		fso_sgl			# yes
	cmpi.b		%d0,&0x15		# is class = 2 & fmt = dbl?
	beq.b		fso_dbl			# yes
	rts					# no

fso_sgl:
	mov.w		LOCAL_EX(%a0),%d0	# fetch src exponent
	andi.w		&0x7fff,%d0		# strip sign
	cmpi.w		%d0,&0x3f80		# is |exp| == $3f80?
	beq.b		fso_sgl_dnrm_zero	# yes
	cmpi.w		%d0,&0x407f		# no; is |exp| == $407f?
	beq.b		fso_infnan		# yes
	rts					# no

fso_sgl_dnrm_zero:
	andi.l		&0x7fffffff,LOCAL_HI(%a0) # clear j-bit
	beq.b		fso_zero		# it's a skewed zero
fso_sgl_dnrm:
# here, we count on norm not to alter a0...
	bsr.l		norm			# normalize mantissa
	neg.w		%d0			# -shft amt
	addi.w		&0x3f81,%d0		# adjust new exponent
	andi.w		&0x8000,LOCAL_EX(%a0)	# clear old exponent
	or.w		%d0,LOCAL_EX(%a0)	# insert new exponent
	rts

fso_zero:
	andi.w		&0x8000,LOCAL_EX(%a0)	# clear bogus exponent
	rts

fso_infnan:
	andi.b		&0x7f,LOCAL_HI(%a0)	# clear j-bit
	ori.w		&0x7fff,LOCAL_EX(%a0)	# make exponent = $7fff
	rts

fso_dbl:
	mov.w		LOCAL_EX(%a0),%d0	# fetch src exponent
	andi.w		&0x7fff,%d0		# strip sign
	cmpi.w		%d0,&0x3c00		# is |exp| == $3c00?
	beq.b		fso_dbl_dnrm_zero	# yes
	cmpi.w		%d0,&0x43ff		# no; is |exp| == $43ff?
	beq.b		fso_infnan		# yes
	rts					# no

fso_dbl_dnrm_zero:
	andi.l		&0x7fffffff,LOCAL_HI(%a0) # clear j-bit
	bne.b		fso_dbl_dnrm		# it's a skewed denorm
	tst.l		LOCAL_LO(%a0)		# is it a zero?
	beq.b		fso_zero		# yes
fso_dbl_dnrm:
# here, we count on norm not to alter a0...
	bsr.l		norm			# normalize mantissa
	neg.w		%d0			# -shft amt
	addi.w		&0x3c01,%d0		# adjust new exponent
	andi.w		&0x8000,LOCAL_EX(%a0)	# clear old exponent
	or.w		%d0,LOCAL_EX(%a0)	# insert new exponent
	rts

#################################################################

# fmove out took an unimplemented data type exception.
# the src operand is in FP_SRC. Call _fout() to write out the result and
# to determine which exceptions, if any, to take.
fu_out:

# Separate packed move outs from the UNNORM and DENORM move outs.
	bfextu		EXC_CMDREG(%a6){&3:&3},%d0
	cmpi.b		%d0,&0x3
	beq.w		fu_out_pack
	cmpi.b		%d0,&0x7
	beq.w		fu_out_pack


# I'm not sure at this point what FPSR bits are valid for this instruction.
# so, since the emulation routines re-create them anyways, zero exception field.
# fmove out doesn't affect ccodes.
	and.l		&0xffff00ff,USER_FPSR(%a6) # zero exception field

	fmov.l		&0x0,%fpcr		# zero current control regs
	fmov.l		&0x0,%fpsr

# the src can ONLY be a DENORM or an UNNORM! so, don't make any big subroutine
# call here. just figure out what it is...
	mov.w		FP_SRC_EX(%a6),%d0	# get exponent
	andi.w		&0x7fff,%d0		# strip sign
	beq.b		fu_out_denorm		# it's a DENORM

	lea		FP_SRC(%a6),%a0
	bsr.l		unnorm_fix		# yes; fix it

	mov.b		%d0,STAG(%a6)

	bra.b		fu_out_cont
fu_out_denorm:
	mov.b		&DENORM,STAG(%a6)
fu_out_cont:

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# fetch rnd mode/prec

	lea		FP_SRC(%a6),%a0		# pass ptr to src operand

	mov.l		(%a6),EXC_A6(%a6)	# in case a6 changes
	bsr.l		fout			# call fmove out routine

# Exceptions in order of precedence:
#	BSUN	: none
#	SNAN	: none
#	OPERR	: fmove.{b,w,l} out of large UNNORM
#	OVFL	: fmove.{s,d}
#	UNFL	: fmove.{s,d,x}
#	DZ	: none
#	INEX2	: all
#	INEX1	: none (packed doesn't travel through here)

# determine the highest priority exception(if any) set by the
# emulation routine that has also been enabled by the user.
	mov.b		FPCR_ENABLE(%a6),%d0	# fetch exceptions enabled
	bne.w		fu_out_ena		# some are enabled

fu_out_done:

	mov.l		EXC_A6(%a6),(%a6)	# in case a6 changed

# on extended precision opclass three instructions using pre-decrement or
# post-increment addressing mode, the address register is not updated. is the
# address register was the stack pointer used from user mode, then let's update
# it here. if it was used from supervisor mode, then we have to handle this
# as a special case.
	btst		&0x5,EXC_SR(%a6)
	bne.b		fu_out_done_s

	mov.l		EXC_A7(%a6),%a0		# restore a7
	mov.l		%a0,%usp

fu_out_done_cont:
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6

	btst		&0x7,(%sp)		# is trace on?
	bne.b		fu_out_trace		# yes

	bra.l		_fpsp_done

# is the ea mode pre-decrement of the stack pointer from supervisor mode?
# ("fmov.x fpm,-(a7)") if so,
fu_out_done_s:
	cmpi.b		SPCOND_FLG(%a6),&mda7_flg
	bne.b		fu_out_done_cont

# the extended precision result is still in fp0. but, we need to save it
# somewhere on the stack until we can copy it to its final resting place.
# here, we're counting on the top of the stack to be the old place-holders
# for fp0/fp1 which have already been restored. that way, we can write
# over those destinations with the shifted stack frame.
	fmovm.x		&0x80,FP_SRC(%a6)	# put answer on stack

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	mov.l		(%a6),%a6		# restore frame pointer

	mov.l		LOCAL_SIZE+EXC_SR(%sp),LOCAL_SIZE+EXC_SR-0xc(%sp)
	mov.l		LOCAL_SIZE+2+EXC_PC(%sp),LOCAL_SIZE+2+EXC_PC-0xc(%sp)

# now, copy the result to the proper place on the stack
	mov.l		LOCAL_SIZE+FP_SRC_EX(%sp),LOCAL_SIZE+EXC_SR+0x0(%sp)
	mov.l		LOCAL_SIZE+FP_SRC_HI(%sp),LOCAL_SIZE+EXC_SR+0x4(%sp)
	mov.l		LOCAL_SIZE+FP_SRC_LO(%sp),LOCAL_SIZE+EXC_SR+0x8(%sp)

	add.l		&LOCAL_SIZE-0x8,%sp

	btst		&0x7,(%sp)
	bne.b		fu_out_trace

	bra.l		_fpsp_done

fu_out_ena:
	and.b		FPSR_EXCEPT(%a6),%d0	# keep only ones enabled
	bfffo		%d0{&24:&8},%d0		# find highest priority exception
	bne.b		fu_out_exc		# there is at least one set

# no exceptions were set.
# if a disabled overflow occurred and inexact was enabled but the result
# was exact, then a branch to _real_inex() is made.
	btst		&ovfl_bit,FPSR_EXCEPT(%a6) # was overflow set?
	beq.w		fu_out_done		# no

fu_out_ovflchk:
	btst		&inex2_bit,FPCR_ENABLE(%a6) # was inexact enabled?
	beq.w		fu_out_done		# no
	bra.w		fu_inex			# yes

#
# The fp move out that took the "Unimplemented Data Type" exception was
# being traced. Since the stack frames are similar, get the "current" PC
# from FPIAR and put it in the trace stack frame then jump to _real_trace().
#
#		  UNSUPP FRAME		   TRACE FRAME
#		*****************	*****************
#		*      EA	*	*    Current	*
#		*		*	*      PC	*
#		*****************	*****************
#		* 0x3 *  0x0dc	*	* 0x2 *  0x024	*
#		*****************	*****************
#		*     Next	*	*     Next	*
#		*      PC	*	*      PC	*
#		*****************	*****************
#		*      SR	*	*      SR	*
#		*****************	*****************
#
fu_out_trace:
	mov.w		&0x2024,0x6(%sp)
	fmov.l		%fpiar,0x8(%sp)
	bra.l		_real_trace

# an exception occurred and that exception was enabled.
fu_out_exc:
	subi.l		&24,%d0			# fix offset to be 0-8

# we don't mess with the existing fsave frame. just re-insert it and
# jump to the "_real_{}()" handler...
	mov.w		(tbl_fu_out.b,%pc,%d0.w*2),%d0
	jmp		(tbl_fu_out.b,%pc,%d0.w*1)

	swbeg		&0x8
tbl_fu_out:
	short		tbl_fu_out	- tbl_fu_out	# BSUN can't happen
	short		tbl_fu_out	- tbl_fu_out	# SNAN can't happen
	short		fu_operr	- tbl_fu_out	# OPERR
	short		fu_ovfl		- tbl_fu_out	# OVFL
	short		fu_unfl		- tbl_fu_out	# UNFL
	short		tbl_fu_out	- tbl_fu_out	# DZ can't happen
	short		fu_inex		- tbl_fu_out	# INEX2
	short		tbl_fu_out	- tbl_fu_out	# INEX1 won't make it here

# for snan,operr,ovfl,unfl, src op is still in FP_SRC so just
# frestore it.
fu_snan:
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	mov.w		&0x30d8,EXC_VOFF(%a6)	# vector offset = 0xd8
	mov.w		&0xe006,2+FP_SRC(%a6)

	frestore	FP_SRC(%a6)

	unlk		%a6


	bra.l		_real_snan

fu_operr:
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	mov.w		&0x30d0,EXC_VOFF(%a6)	# vector offset = 0xd0
	mov.w		&0xe004,2+FP_SRC(%a6)

	frestore	FP_SRC(%a6)

	unlk		%a6


	bra.l		_real_operr

fu_ovfl:
	fmovm.x		&0x40,FP_SRC(%a6)	# save EXOP to the stack

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	mov.w		&0x30d4,EXC_VOFF(%a6)	# vector offset = 0xd4
	mov.w		&0xe005,2+FP_SRC(%a6)

	frestore	FP_SRC(%a6)		# restore EXOP

	unlk		%a6

	bra.l		_real_ovfl

# underflow can happen for extended precision. extended precision opclass
# three instruction exceptions don't update the stack pointer. so, if the
# exception occurred from user mode, then simply update a7 and exit normally.
# if the exception occurred from supervisor mode, check if
fu_unfl:
	mov.l		EXC_A6(%a6),(%a6)	# restore a6

	btst		&0x5,EXC_SR(%a6)
	bne.w		fu_unfl_s

	mov.l		EXC_A7(%a6),%a0		# restore a7 whether we need
	mov.l		%a0,%usp		# to or not...

fu_unfl_cont:
	fmovm.x		&0x40,FP_SRC(%a6)	# save EXOP to the stack

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	mov.w		&0x30cc,EXC_VOFF(%a6)	# vector offset = 0xcc
	mov.w		&0xe003,2+FP_SRC(%a6)

	frestore	FP_SRC(%a6)		# restore EXOP

	unlk		%a6

	bra.l		_real_unfl

fu_unfl_s:
	cmpi.b		SPCOND_FLG(%a6),&mda7_flg # was the <ea> mode -(sp)?
	bne.b		fu_unfl_cont

# the extended precision result is still in fp0. but, we need to save it
# somewhere on the stack until we can copy it to its final resting place
# (where the exc frame is currently). make sure it's not at the top of the
# frame or it will get overwritten when the exc stack frame is shifted "down".
	fmovm.x		&0x80,FP_SRC(%a6)	# put answer on stack
	fmovm.x		&0x40,FP_DST(%a6)	# put EXOP on stack

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	mov.w		&0x30cc,EXC_VOFF(%a6)	# vector offset = 0xcc
	mov.w		&0xe003,2+FP_DST(%a6)

	frestore	FP_DST(%a6)		# restore EXOP

	mov.l		(%a6),%a6		# restore frame pointer

	mov.l		LOCAL_SIZE+EXC_SR(%sp),LOCAL_SIZE+EXC_SR-0xc(%sp)
	mov.l		LOCAL_SIZE+2+EXC_PC(%sp),LOCAL_SIZE+2+EXC_PC-0xc(%sp)
	mov.l		LOCAL_SIZE+EXC_EA(%sp),LOCAL_SIZE+EXC_EA-0xc(%sp)

# now, copy the result to the proper place on the stack
	mov.l		LOCAL_SIZE+FP_SRC_EX(%sp),LOCAL_SIZE+EXC_SR+0x0(%sp)
	mov.l		LOCAL_SIZE+FP_SRC_HI(%sp),LOCAL_SIZE+EXC_SR+0x4(%sp)
	mov.l		LOCAL_SIZE+FP_SRC_LO(%sp),LOCAL_SIZE+EXC_SR+0x8(%sp)

	add.l		&LOCAL_SIZE-0x8,%sp

	bra.l		_real_unfl

# fmove in and out enter here.
fu_inex:
	fmovm.x		&0x40,FP_SRC(%a6)	# save EXOP to the stack

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	mov.w		&0x30c4,EXC_VOFF(%a6)	# vector offset = 0xc4
	mov.w		&0xe001,2+FP_SRC(%a6)

	frestore	FP_SRC(%a6)		# restore EXOP

	unlk		%a6


	bra.l		_real_inex

#########################################################################
#########################################################################
fu_in_pack:


# I'm not sure at this point what FPSR bits are valid for this instruction.
# so, since the emulation routines re-create them anyways, zero exception field
	andi.l		&0x0ff00ff,USER_FPSR(%a6) # zero exception field

	fmov.l		&0x0,%fpcr		# zero current control regs
	fmov.l		&0x0,%fpsr

	bsr.l		get_packed		# fetch packed src operand

	lea		FP_SRC(%a6),%a0		# pass ptr to src
	bsr.l		set_tag_x		# set src optype tag

	mov.b		%d0,STAG(%a6)		# save src optype tag

	bfextu		EXC_CMDREG(%a6){&6:&3},%d0 # dyadic; load dst reg

# bit five of the fp extension word separates the monadic and dyadic operations
# at this point
	btst		&0x5,1+EXC_CMDREG(%a6)	# is operation monadic or dyadic?
	beq.b		fu_extract_p		# monadic
	cmpi.b		1+EXC_CMDREG(%a6),&0x3a	# is operation an ftst?
	beq.b		fu_extract_p		# yes, so it's monadic, too

	bsr.l		load_fpn2		# load dst into FP_DST

	lea		FP_DST(%a6),%a0		# pass: ptr to dst op
	bsr.l		set_tag_x		# tag the operand type
	cmpi.b		%d0,&UNNORM		# is operand an UNNORM?
	bne.b		fu_op2_done_p		# no
	bsr.l		unnorm_fix		# yes; convert to NORM,DENORM,or ZERO
fu_op2_done_p:
	mov.b		%d0,DTAG(%a6)		# save dst optype tag

fu_extract_p:
	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# fetch rnd mode/prec

	bfextu		1+EXC_CMDREG(%a6){&1:&7},%d1 # extract extension

	lea		FP_SRC(%a6),%a0
	lea		FP_DST(%a6),%a1

	mov.l		(tbl_unsupp.l,%pc,%d1.l*4),%d1 # fetch routine addr
	jsr		(tbl_unsupp.l,%pc,%d1.l*1)

#
# Exceptions in order of precedence:
#	BSUN	: none
#	SNAN	: all dyadic ops
#	OPERR	: fsqrt(-NORM)
#	OVFL	: all except ftst,fcmp
#	UNFL	: all except ftst,fcmp
#	DZ	: fdiv
#	INEX2	: all except ftst,fcmp
#	INEX1	: all
#

# we determine the highest priority exception(if any) set by the
# emulation routine that has also been enabled by the user.
	mov.b		FPCR_ENABLE(%a6),%d0	# fetch exceptions enabled
	bne.w		fu_in_ena_p		# some are enabled

fu_in_cont_p:
# fcmp and ftst do not store any result.
	mov.b		1+EXC_CMDREG(%a6),%d0	# fetch extension
	andi.b		&0x38,%d0		# extract bits 3-5
	cmpi.b		%d0,&0x38		# is instr fcmp or ftst?
	beq.b		fu_in_exit_p		# yes

	bfextu		EXC_CMDREG(%a6){&6:&3},%d0 # dyadic; load dst reg
	bsr.l		store_fpreg		# store the result

fu_in_exit_p:

	btst		&0x5,EXC_SR(%a6)	# user or supervisor?
	bne.w		fu_in_exit_s_p		# supervisor

	mov.l		EXC_A7(%a6),%a0		# update user a7
	mov.l		%a0,%usp

fu_in_exit_cont_p:
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6			# unravel stack frame

	btst		&0x7,(%sp)		# is trace on?
	bne.w		fu_trace_p		# yes

	bra.l		_fpsp_done		# exit to os

# the exception occurred in supervisor mode. check to see if the
# addressing mode was (a7)+. if so, we'll need to shift the
# stack frame "up".
fu_in_exit_s_p:
	btst		&mia7_bit,SPCOND_FLG(%a6) # was ea mode (a7)+
	beq.b		fu_in_exit_cont_p	# no

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6			# unravel stack frame

# shift the stack frame "up". we don't really care about the <ea> field.
	mov.l		0x4(%sp),0x10(%sp)
	mov.l		0x0(%sp),0xc(%sp)
	add.l		&0xc,%sp

	btst		&0x7,(%sp)		# is trace on?
	bne.w		fu_trace_p		# yes

	bra.l		_fpsp_done		# exit to os

fu_in_ena_p:
	and.b		FPSR_EXCEPT(%a6),%d0	# keep only ones enabled & set
	bfffo		%d0{&24:&8},%d0		# find highest priority exception
	bne.b		fu_in_exc_p		# at least one was set

#
# No exceptions occurred that were also enabled. Now:
#
#	if (OVFL && ovfl_disabled && inexact_enabled) {
#	    branch to _real_inex() (even if the result was exact!);
#	} else {
#	    save the result in the proper fp reg (unless the op is fcmp or ftst);
#	    return;
#	}
#
	btst		&ovfl_bit,FPSR_EXCEPT(%a6) # was overflow set?
	beq.w		fu_in_cont_p		# no

fu_in_ovflchk_p:
	btst		&inex2_bit,FPCR_ENABLE(%a6) # was inexact enabled?
	beq.w		fu_in_cont_p		# no
	bra.w		fu_in_exc_ovfl_p	# do _real_inex() now

#
# An exception occurred and that exception was enabled:
#
#	shift enabled exception field into lo byte of d0;
#	if (((INEX2 || INEX1) && inex_enabled && OVFL && ovfl_disabled) ||
#	    ((INEX2 || INEX1) && inex_enabled && UNFL && unfl_disabled)) {
#		/*
#		 * this is the case where we must call _real_inex() now or else
#		 * there will be no other way to pass it the exceptional operand
#		 */
#		call _real_inex();
#	} else {
#		restore exc state (SNAN||OPERR||OVFL||UNFL||DZ||INEX) into the FPU;
#	}
#
fu_in_exc_p:
	subi.l		&24,%d0			# fix offset to be 0-8
	cmpi.b		%d0,&0x6		# is exception INEX? (6 or 7)
	blt.b		fu_in_exc_exit_p	# no

# the enabled exception was inexact
	btst		&unfl_bit,FPSR_EXCEPT(%a6) # did disabled underflow occur?
	bne.w		fu_in_exc_unfl_p	# yes
	btst		&ovfl_bit,FPSR_EXCEPT(%a6) # did disabled overflow occur?
	bne.w		fu_in_exc_ovfl_p	# yes

# here, we insert the correct fsave status value into the fsave frame for the
# corresponding exception. the operand in the fsave frame should be the original
# src operand.
# as a reminder for future predicted pain and agony, we are passing in fsave the
# "non-skewed" operand for cases of sgl and dbl src INFs,NANs, and DENORMs.
# this is INCORRECT for enabled SNAN which would give to the user the skewed SNAN!!!
fu_in_exc_exit_p:
	btst		&0x5,EXC_SR(%a6)	# user or supervisor?
	bne.w		fu_in_exc_exit_s_p	# supervisor

	mov.l		EXC_A7(%a6),%a0		# update user a7
	mov.l		%a0,%usp

fu_in_exc_exit_cont_p:
	mov.w		(tbl_except_p.b,%pc,%d0.w*2),2+FP_SRC(%a6)

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)		# restore src op

	unlk		%a6

	btst		&0x7,(%sp)		# is trace enabled?
	bne.w		fu_trace_p		# yes

	bra.l		_fpsp_done

tbl_except_p:
	short		0xe000,0xe006,0xe004,0xe005
	short		0xe003,0xe002,0xe001,0xe001

fu_in_exc_ovfl_p:
	mov.w		&0x3,%d0
	bra.w		fu_in_exc_exit_p

fu_in_exc_unfl_p:
	mov.w		&0x4,%d0
	bra.w		fu_in_exc_exit_p

fu_in_exc_exit_s_p:
	btst		&mia7_bit,SPCOND_FLG(%a6)
	beq.b		fu_in_exc_exit_cont_p

	mov.w		(tbl_except_p.b,%pc,%d0.w*2),2+FP_SRC(%a6)

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)		# restore src op

	unlk		%a6			# unravel stack frame

# shift stack frame "up". who cares about <ea> field.
	mov.l		0x4(%sp),0x10(%sp)
	mov.l		0x0(%sp),0xc(%sp)
	add.l		&0xc,%sp

	btst		&0x7,(%sp)		# is trace on?
	bne.b		fu_trace_p		# yes

	bra.l		_fpsp_done		# exit to os

#
# The opclass two PACKED instruction that took an "Unimplemented Data Type"
# exception was being traced. Make the "current" PC the FPIAR and put it in the
# trace stack frame then jump to _real_trace().
#
#		  UNSUPP FRAME		   TRACE FRAME
#		*****************	*****************
#		*      EA	*	*    Current	*
#		*		*	*      PC	*
#		*****************	*****************
#		* 0x2 *	0x0dc	*	* 0x2 *  0x024	*
#		*****************	*****************
#		*     Next	*	*     Next	*
#		*      PC	*	*      PC	*
#		*****************	*****************
#		*      SR	*	*      SR	*
#		*****************	*****************
fu_trace_p:
	mov.w		&0x2024,0x6(%sp)
	fmov.l		%fpiar,0x8(%sp)

	bra.l		_real_trace

#########################################################
#########################################################
fu_out_pack:


# I'm not sure at this point what FPSR bits are valid for this instruction.
# so, since the emulation routines re-create them anyways, zero exception field.
# fmove out doesn't affect ccodes.
	and.l		&0xffff00ff,USER_FPSR(%a6) # zero exception field

	fmov.l		&0x0,%fpcr		# zero current control regs
	fmov.l		&0x0,%fpsr

	bfextu		EXC_CMDREG(%a6){&6:&3},%d0
	bsr.l		load_fpn1

# unlike other opclass 3, unimplemented data type exceptions, packed must be
# able to detect all operand types.
	lea		FP_SRC(%a6),%a0
	bsr.l		set_tag_x		# tag the operand type
	cmpi.b		%d0,&UNNORM		# is operand an UNNORM?
	bne.b		fu_op2_p		# no
	bsr.l		unnorm_fix		# yes; convert to NORM,DENORM,or ZERO

fu_op2_p:
	mov.b		%d0,STAG(%a6)		# save src optype tag

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# fetch rnd mode/prec

	lea		FP_SRC(%a6),%a0		# pass ptr to src operand

	mov.l		(%a6),EXC_A6(%a6)	# in case a6 changes
	bsr.l		fout			# call fmove out routine

# Exceptions in order of precedence:
#	BSUN	: no
#	SNAN	: yes
#	OPERR	: if ((k_factor > +17) || (dec. exp exceeds 3 digits))
#	OVFL	: no
#	UNFL	: no
#	DZ	: no
#	INEX2	: yes
#	INEX1	: no

# determine the highest priority exception(if any) set by the
# emulation routine that has also been enabled by the user.
	mov.b		FPCR_ENABLE(%a6),%d0	# fetch exceptions enabled
	bne.w		fu_out_ena_p		# some are enabled

fu_out_exit_p:
	mov.l		EXC_A6(%a6),(%a6)	# restore a6

	btst		&0x5,EXC_SR(%a6)	# user or supervisor?
	bne.b		fu_out_exit_s_p		# supervisor

	mov.l		EXC_A7(%a6),%a0		# update user a7
	mov.l		%a0,%usp

fu_out_exit_cont_p:
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6			# unravel stack frame

	btst		&0x7,(%sp)		# is trace on?
	bne.w		fu_trace_p		# yes

	bra.l		_fpsp_done		# exit to os

# the exception occurred in supervisor mode. check to see if the
# addressing mode was -(a7). if so, we'll need to shift the
# stack frame "down".
fu_out_exit_s_p:
	btst		&mda7_bit,SPCOND_FLG(%a6) # was ea mode -(a7)
	beq.b		fu_out_exit_cont_p	# no

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	mov.l		(%a6),%a6		# restore frame pointer

	mov.l		LOCAL_SIZE+EXC_SR(%sp),LOCAL_SIZE+EXC_SR-0xc(%sp)
	mov.l		LOCAL_SIZE+2+EXC_PC(%sp),LOCAL_SIZE+2+EXC_PC-0xc(%sp)

# now, copy the result to the proper place on the stack
	mov.l		LOCAL_SIZE+FP_DST_EX(%sp),LOCAL_SIZE+EXC_SR+0x0(%sp)
	mov.l		LOCAL_SIZE+FP_DST_HI(%sp),LOCAL_SIZE+EXC_SR+0x4(%sp)
	mov.l		LOCAL_SIZE+FP_DST_LO(%sp),LOCAL_SIZE+EXC_SR+0x8(%sp)

	add.l		&LOCAL_SIZE-0x8,%sp

	btst		&0x7,(%sp)
	bne.w		fu_trace_p

	bra.l		_fpsp_done

fu_out_ena_p:
	and.b		FPSR_EXCEPT(%a6),%d0	# keep only ones enabled
	bfffo		%d0{&24:&8},%d0		# find highest priority exception
	beq.w		fu_out_exit_p

	mov.l		EXC_A6(%a6),(%a6)	# restore a6

# an exception occurred and that exception was enabled.
# the only exception possible on packed move out are INEX, OPERR, and SNAN.
fu_out_exc_p:
	cmpi.b		%d0,&0x1a
	bgt.w		fu_inex_p2
	beq.w		fu_operr_p

fu_snan_p:
	btst		&0x5,EXC_SR(%a6)
	bne.b		fu_snan_s_p

	mov.l		EXC_A7(%a6),%a0
	mov.l		%a0,%usp
	bra.w		fu_snan

fu_snan_s_p:
	cmpi.b		SPCOND_FLG(%a6),&mda7_flg
	bne.w		fu_snan

# the instruction was "fmove.p fpn,-(a7)" from supervisor mode.
# the strategy is to move the exception frame "down" 12 bytes. then, we
# can store the default result where the exception frame was.
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	mov.w		&0x30d8,EXC_VOFF(%a6)	# vector offset = 0xd0
	mov.w		&0xe006,2+FP_SRC(%a6)	# set fsave status

	frestore	FP_SRC(%a6)		# restore src operand

	mov.l		(%a6),%a6		# restore frame pointer

	mov.l		LOCAL_SIZE+EXC_SR(%sp),LOCAL_SIZE+EXC_SR-0xc(%sp)
	mov.l		LOCAL_SIZE+2+EXC_PC(%sp),LOCAL_SIZE+2+EXC_PC-0xc(%sp)
	mov.l		LOCAL_SIZE+EXC_EA(%sp),LOCAL_SIZE+EXC_EA-0xc(%sp)

# now, we copy the default result to its proper location
	mov.l		LOCAL_SIZE+FP_DST_EX(%sp),LOCAL_SIZE+0x4(%sp)
	mov.l		LOCAL_SIZE+FP_DST_HI(%sp),LOCAL_SIZE+0x8(%sp)
	mov.l		LOCAL_SIZE+FP_DST_LO(%sp),LOCAL_SIZE+0xc(%sp)

	add.l		&LOCAL_SIZE-0x8,%sp


	bra.l		_real_snan

fu_operr_p:
	btst		&0x5,EXC_SR(%a6)
	bne.w		fu_operr_p_s

	mov.l		EXC_A7(%a6),%a0
	mov.l		%a0,%usp
	bra.w		fu_operr

fu_operr_p_s:
	cmpi.b		SPCOND_FLG(%a6),&mda7_flg
	bne.w		fu_operr

# the instruction was "fmove.p fpn,-(a7)" from supervisor mode.
# the strategy is to move the exception frame "down" 12 bytes. then, we
# can store the default result where the exception frame was.
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	mov.w		&0x30d0,EXC_VOFF(%a6)	# vector offset = 0xd0
	mov.w		&0xe004,2+FP_SRC(%a6)	# set fsave status

	frestore	FP_SRC(%a6)		# restore src operand

	mov.l		(%a6),%a6		# restore frame pointer

	mov.l		LOCAL_SIZE+EXC_SR(%sp),LOCAL_SIZE+EXC_SR-0xc(%sp)
	mov.l		LOCAL_SIZE+2+EXC_PC(%sp),LOCAL_SIZE+2+EXC_PC-0xc(%sp)
	mov.l		LOCAL_SIZE+EXC_EA(%sp),LOCAL_SIZE+EXC_EA-0xc(%sp)

# now, we copy the default result to its proper location
	mov.l		LOCAL_SIZE+FP_DST_EX(%sp),LOCAL_SIZE+0x4(%sp)
	mov.l		LOCAL_SIZE+FP_DST_HI(%sp),LOCAL_SIZE+0x8(%sp)
	mov.l		LOCAL_SIZE+FP_DST_LO(%sp),LOCAL_SIZE+0xc(%sp)

	add.l		&LOCAL_SIZE-0x8,%sp


	bra.l		_real_operr

fu_inex_p2:
	btst		&0x5,EXC_SR(%a6)
	bne.w		fu_inex_s_p2

	mov.l		EXC_A7(%a6),%a0
	mov.l		%a0,%usp
	bra.w		fu_inex

fu_inex_s_p2:
	cmpi.b		SPCOND_FLG(%a6),&mda7_flg
	bne.w		fu_inex

# the instruction was "fmove.p fpn,-(a7)" from supervisor mode.
# the strategy is to move the exception frame "down" 12 bytes. then, we
# can store the default result where the exception frame was.
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0/fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	mov.w		&0x30c4,EXC_VOFF(%a6)	# vector offset = 0xc4
	mov.w		&0xe001,2+FP_SRC(%a6)	# set fsave status

	frestore	FP_SRC(%a6)		# restore src operand

	mov.l		(%a6),%a6		# restore frame pointer

	mov.l		LOCAL_SIZE+EXC_SR(%sp),LOCAL_SIZE+EXC_SR-0xc(%sp)
	mov.l		LOCAL_SIZE+2+EXC_PC(%sp),LOCAL_SIZE+2+EXC_PC-0xc(%sp)
	mov.l		LOCAL_SIZE+EXC_EA(%sp),LOCAL_SIZE+EXC_EA-0xc(%sp)

# now, we copy the default result to its proper location
	mov.l		LOCAL_SIZE+FP_DST_EX(%sp),LOCAL_SIZE+0x4(%sp)
	mov.l		LOCAL_SIZE+FP_DST_HI(%sp),LOCAL_SIZE+0x8(%sp)
	mov.l		LOCAL_SIZE+FP_DST_LO(%sp),LOCAL_SIZE+0xc(%sp)

	add.l		&LOCAL_SIZE-0x8,%sp


	bra.l		_real_inex

#########################################################################

#
# if we're stuffing a source operand back into an fsave frame then we
# have to make sure that for single or double source operands that the
# format stuffed is as weird as the hardware usually makes it.
#
	global		funimp_skew
funimp_skew:
	bfextu		EXC_EXTWORD(%a6){&3:&3},%d0 # extract src specifier
	cmpi.b		%d0,&0x1		# was src sgl?
	beq.b		funimp_skew_sgl		# yes
	cmpi.b		%d0,&0x5		# was src dbl?
	beq.b		funimp_skew_dbl		# yes
	rts

funimp_skew_sgl:
	mov.w		FP_SRC_EX(%a6),%d0	# fetch DENORM exponent
	andi.w		&0x7fff,%d0		# strip sign
	beq.b		funimp_skew_sgl_not
	cmpi.w		%d0,&0x3f80
	bgt.b		funimp_skew_sgl_not
	neg.w		%d0			# make exponent negative
	addi.w		&0x3f81,%d0		# find amt to shift
	mov.l		FP_SRC_HI(%a6),%d1	# fetch DENORM hi(man)
	lsr.l		%d0,%d1			# shift it
	bset		&31,%d1			# set j-bit
	mov.l		%d1,FP_SRC_HI(%a6)	# insert new hi(man)
	andi.w		&0x8000,FP_SRC_EX(%a6)	# clear old exponent
	ori.w		&0x3f80,FP_SRC_EX(%a6)	# insert new "skewed" exponent
funimp_skew_sgl_not:
	rts

funimp_skew_dbl:
	mov.w		FP_SRC_EX(%a6),%d0	# fetch DENORM exponent
	andi.w		&0x7fff,%d0		# strip sign
	beq.b		funimp_skew_dbl_not
	cmpi.w		%d0,&0x3c00
	bgt.b		funimp_skew_dbl_not

	tst.b		FP_SRC_EX(%a6)		# make "internal format"
	smi.b		0x2+FP_SRC(%a6)
	mov.w		%d0,FP_SRC_EX(%a6)	# insert exponent with cleared sign
	clr.l		%d0			# clear g,r,s
	lea		FP_SRC(%a6),%a0		# pass ptr to src op
	mov.w		&0x3c01,%d1		# pass denorm threshold
	bsr.l		dnrm_lp			# denorm it
	mov.w		&0x3c00,%d0		# new exponent
	tst.b		0x2+FP_SRC(%a6)		# is sign set?
	beq.b		fss_dbl_denorm_done	# no
	bset		&15,%d0			# set sign
fss_dbl_denorm_done:
	bset		&0x7,FP_SRC_HI(%a6)	# set j-bit
	mov.w		%d0,FP_SRC_EX(%a6)	# insert new exponent
funimp_skew_dbl_not:
	rts

#########################################################################
	global		_mem_write2
_mem_write2:
	btst		&0x5,EXC_SR(%a6)
	beq.l		_dmem_write
	mov.l		0x0(%a0),FP_DST_EX(%a6)
	mov.l		0x4(%a0),FP_DST_HI(%a6)
	mov.l		0x8(%a0),FP_DST_LO(%a6)
	clr.l		%d1
	rts

#########################################################################
# XDEF ****************************************************************	#
#	_fpsp_effadd(): 060FPSP entry point for FP "Unimplemented	#
#			effective address" exception.			#
#									#
#	This handler should be the first code executed upon taking the	#
#	FP Unimplemented Effective Address exception in an operating	#
#	system.								#
#									#
# XREF ****************************************************************	#
#	_imem_read_long() - read instruction longword			#
#	fix_skewed_ops() - adjust src operand in fsave frame		#
#	set_tag_x() - determine optype of src/dst operands		#
#	store_fpreg() - store opclass 0 or 2 result to FP regfile	#
#	unnorm_fix() - change UNNORM operands to NORM or ZERO		#
#	load_fpn2() - load dst operand from FP regfile			#
#	tbl_unsupp - add of table of emulation routines for opclass 0,2	#
#	decbin() - convert packed data to FP binary data		#
#	_real_fpu_disabled() - "callout" for "FPU disabled" exception	#
#	_real_access() - "callout" for access error exception		#
#	_mem_read() - read extended immediate operand from memory	#
#	_fpsp_done() - "callout" for exit; work all done		#
#	_real_trace() - "callout" for Trace enabled exception		#
#	fmovm_dynamic() - emulate dynamic fmovm instruction		#
#	fmovm_ctrl() - emulate fmovm control instruction		#
#									#
# INPUT ***************************************************************	#
#	- The system stack contains the "Unimplemented <ea>" stk frame	#
#									#
# OUTPUT **************************************************************	#
#	If access error:						#
#	- The system stack is changed to an access error stack frame	#
#	If FPU disabled:						#
#	- The system stack is changed to an FPU disabled stack frame	#
#	If Trace exception enabled:					#
#	- The system stack is changed to a Trace exception stack frame	#
#	Else: (normal case)						#
#	- None (correct result has been stored as appropriate)		#
#									#
# ALGORITHM ***********************************************************	#
#	This exception handles 3 types of operations:			#
# (1) FP Instructions using extended precision or packed immediate	#
#     addressing mode.							#
# (2) The "fmovm.x" instruction w/ dynamic register specification.	#
# (3) The "fmovm.l" instruction w/ 2 or 3 control registers.		#
#									#
#	For immediate data operations, the data is read in w/ a		#
# _mem_read() "callout", converted to FP binary (if packed), and used	#
# as the source operand to the instruction specified by the instruction	#
# word. If no FP exception should be reported ads a result of the	#
# emulation, then the result is stored to the destination register and	#
# the handler exits through _fpsp_done(). If an enabled exc has been	#
# signalled as a result of emulation, then an fsave state frame		#
# corresponding to the FP exception type must be entered into the 060	#
# FPU before exiting. In either the enabled or disabled cases, we	#
# must also check if a Trace exception is pending, in which case, we	#
# must create a Trace exception stack frame from the current exception	#
# stack frame. If no Trace is pending, we simply exit through		#
# _fpsp_done().								#
#	For "fmovm.x", call the routine fmovm_dynamic() which will	#
# decode and emulate the instruction. No FP exceptions can be pending	#
# as a result of this operation emulation. A Trace exception can be	#
# pending, though, which means the current stack frame must be changed	#
# to a Trace stack frame and an exit made through _real_trace().	#
# For the case of "fmovm.x Dn,-(a7)", where the offending instruction	#
# was executed from supervisor mode, this handler must store the FP	#
# register file values to the system stack by itself since		#
# fmovm_dynamic() can't handle this. A normal exit is made through	#
# fpsp_done().								#
#	For "fmovm.l", fmovm_ctrl() is used to emulate the instruction.	#
# Again, a Trace exception may be pending and an exit made through	#
# _real_trace(). Else, a normal exit is made through _fpsp_done().	#
#									#
#	Before any of the above is attempted, it must be checked to	#
# see if the FPU is disabled. Since the "Unimp <ea>" exception is taken	#
# before the "FPU disabled" exception, but the "FPU disabled" exception	#
# has higher priority, we check the disabled bit in the PCR. If set,	#
# then we must create an 8 word "FPU disabled" exception stack frame	#
# from the current 4 word exception stack frame. This includes		#
# reproducing the effective address of the instruction to put on the	#
# new stack frame.							#
#									#
#	In the process of all emulation work, if a _mem_read()		#
# "callout" returns a failing result indicating an access error, then	#
# we must create an access error stack frame from the current stack	#
# frame. This information includes a faulting address and a fault-	#
# status-longword. These are created within this handler.		#
#									#
#########################################################################

	global		_fpsp_effadd
_fpsp_effadd:

# This exception type takes priority over the "Line F Emulator"
# exception. Therefore, the FPU could be disabled when entering here.
# So, we must check to see if it's disabled and handle that case separately.
	mov.l		%d0,-(%sp)		# save d0
	movc		%pcr,%d0		# load proc cr
	btst		&0x1,%d0		# is FPU disabled?
	bne.w		iea_disabled		# yes
	mov.l		(%sp)+,%d0		# restore d0

	link		%a6,&-LOCAL_SIZE	# init stack frame

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,%fpiar,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FPREGS(%a6)	# save fp0-fp1 on stack

# PC of instruction that took the exception is the PC in the frame
	mov.l		EXC_PC(%a6),EXC_EXTWPTR(%a6)

	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch the instruction words
	mov.l		%d0,EXC_OPWORD(%a6)	# store OPWORD and EXTWORD

#########################################################################

	tst.w		%d0			# is operation fmovem?
	bmi.w		iea_fmovm		# yes

#
# here, we will have:
#	fabs	fdabs	fsabs		facos		fmod
#	fadd	fdadd	fsadd		fasin		frem
#	fcmp				fatan		fscale
#	fdiv	fddiv	fsdiv		fatanh		fsin
#	fint				fcos		fsincos
#	fintrz				fcosh		fsinh
#	fmove	fdmove	fsmove		fetox		ftan
#	fmul	fdmul	fsmul		fetoxm1		ftanh
#	fneg	fdneg	fsneg		fgetexp		ftentox
#	fsgldiv				fgetman		ftwotox
#	fsglmul				flog10
#	fsqrt				flog2
#	fsub	fdsub	fssub		flogn
#	ftst				flognp1
# which can all use f<op>.{x,p}
# so, now it's immediate data extended precision AND PACKED FORMAT!
#
iea_op:
	andi.l		&0x00ff00ff,USER_FPSR(%a6)

	btst		&0xa,%d0		# is src fmt x or p?
	bne.b		iea_op_pack		# packed


	mov.l		EXC_EXTWPTR(%a6),%a0	# pass: ptr to #<data>
	lea		FP_SRC(%a6),%a1		# pass: ptr to super addr
	mov.l		&0xc,%d0		# pass: 12 bytes
	bsr.l		_imem_read		# read extended immediate

	tst.l		%d1			# did ifetch fail?
	bne.w		iea_iacc		# yes

	bra.b		iea_op_setsrc

iea_op_pack:

	mov.l		EXC_EXTWPTR(%a6),%a0	# pass: ptr to #<data>
	lea		FP_SRC(%a6),%a1		# pass: ptr to super dst
	mov.l		&0xc,%d0		# pass: 12 bytes
	bsr.l		_imem_read		# read packed operand

	tst.l		%d1			# did ifetch fail?
	bne.w		iea_iacc		# yes

# The packed operand is an INF or a NAN if the exponent field is all ones.
	bfextu		FP_SRC(%a6){&1:&15},%d0	# get exp
	cmpi.w		%d0,&0x7fff		# INF or NAN?
	beq.b		iea_op_setsrc		# operand is an INF or NAN

# The packed operand is a zero if the mantissa is all zero, else it's
# a normal packed op.
	mov.b		3+FP_SRC(%a6),%d0	# get byte 4
	andi.b		&0x0f,%d0		# clear all but last nybble
	bne.b		iea_op_gp_not_spec	# not a zero
	tst.l		FP_SRC_HI(%a6)		# is lw 2 zero?
	bne.b		iea_op_gp_not_spec	# not a zero
	tst.l		FP_SRC_LO(%a6)		# is lw 3 zero?
	beq.b		iea_op_setsrc		# operand is a ZERO
iea_op_gp_not_spec:
	lea		FP_SRC(%a6),%a0		# pass: ptr to packed op
	bsr.l		decbin			# convert to extended
	fmovm.x		&0x80,FP_SRC(%a6)	# make this the srcop

iea_op_setsrc:
	addi.l		&0xc,EXC_EXTWPTR(%a6)	# update extension word pointer

# FP_SRC now holds the src operand.
	lea		FP_SRC(%a6),%a0		# pass: ptr to src op
	bsr.l		set_tag_x		# tag the operand type
	mov.b		%d0,STAG(%a6)		# could be ANYTHING!!!
	cmpi.b		%d0,&UNNORM		# is operand an UNNORM?
	bne.b		iea_op_getdst		# no
	bsr.l		unnorm_fix		# yes; convert to NORM/DENORM/ZERO
	mov.b		%d0,STAG(%a6)		# set new optype tag
iea_op_getdst:
	clr.b		STORE_FLG(%a6)		# clear "store result" boolean

	btst		&0x5,1+EXC_CMDREG(%a6)	# is operation monadic or dyadic?
	beq.b		iea_op_extract		# monadic
	btst		&0x4,1+EXC_CMDREG(%a6)	# is operation fsincos,ftst,fcmp?
	bne.b		iea_op_spec		# yes

iea_op_loaddst:
	bfextu		EXC_CMDREG(%a6){&6:&3},%d0 # fetch dst regno
	bsr.l		load_fpn2		# load dst operand

	lea		FP_DST(%a6),%a0		# pass: ptr to dst op
	bsr.l		set_tag_x		# tag the operand type
	mov.b		%d0,DTAG(%a6)		# could be ANYTHING!!!
	cmpi.b		%d0,&UNNORM		# is operand an UNNORM?
	bne.b		iea_op_extract		# no
	bsr.l		unnorm_fix		# yes; convert to NORM/DENORM/ZERO
	mov.b		%d0,DTAG(%a6)		# set new optype tag
	bra.b		iea_op_extract

# the operation is fsincos, ftst, or fcmp. only fcmp is dyadic
iea_op_spec:
	btst		&0x3,1+EXC_CMDREG(%a6)	# is operation fsincos?
	beq.b		iea_op_extract		# yes
# now, we're left with ftst and fcmp. so, first let's tag them so that they don't
# store a result. then, only fcmp will branch back and pick up a dst operand.
	st		STORE_FLG(%a6)		# don't store a final result
	btst		&0x1,1+EXC_CMDREG(%a6)	# is operation fcmp?
	beq.b		iea_op_loaddst		# yes

iea_op_extract:
	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass: rnd mode,prec

	mov.b		1+EXC_CMDREG(%a6),%d1
	andi.w		&0x007f,%d1		# extract extension

	fmov.l		&0x0,%fpcr
	fmov.l		&0x0,%fpsr

	lea		FP_SRC(%a6),%a0
	lea		FP_DST(%a6),%a1

	mov.l		(tbl_unsupp.l,%pc,%d1.w*4),%d1 # fetch routine addr
	jsr		(tbl_unsupp.l,%pc,%d1.l*1)

#
# Exceptions in order of precedence:
#	BSUN	: none
#	SNAN	: all operations
#	OPERR	: all reg-reg or mem-reg operations that can normally operr
#	OVFL	: same as OPERR
#	UNFL	: same as OPERR
#	DZ	: same as OPERR
#	INEX2	: same as OPERR
#	INEX1	: all packed immediate operations
#

# we determine the highest priority exception(if any) set by the
# emulation routine that has also been enabled by the user.
	mov.b		FPCR_ENABLE(%a6),%d0	# fetch exceptions enabled
	bne.b		iea_op_ena		# some are enabled

# now, we save the result, unless, of course, the operation was ftst or fcmp.
# these don't save results.
iea_op_save:
	tst.b		STORE_FLG(%a6)		# does this op store a result?
	bne.b		iea_op_exit1		# exit with no frestore

iea_op_store:
	bfextu		EXC_CMDREG(%a6){&6:&3},%d0 # fetch dst regno
	bsr.l		store_fpreg		# store the result

iea_op_exit1:
	mov.l		EXC_PC(%a6),USER_FPIAR(%a6) # set FPIAR to "Current PC"
	mov.l		EXC_EXTWPTR(%a6),EXC_PC(%a6) # set "Next PC" in exc frame

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6			# unravel the frame

	btst		&0x7,(%sp)		# is trace on?
	bne.w		iea_op_trace		# yes

	bra.l		_fpsp_done		# exit to os

iea_op_ena:
	and.b		FPSR_EXCEPT(%a6),%d0	# keep only ones enable and set
	bfffo		%d0{&24:&8},%d0		# find highest priority exception
	bne.b		iea_op_exc		# at least one was set

# no exception occurred. now, did a disabled, exact overflow occur with inexact
# enabled? if so, then we have to stuff an overflow frame into the FPU.
	btst		&ovfl_bit,FPSR_EXCEPT(%a6) # did overflow occur?
	beq.b		iea_op_save

iea_op_ovfl:
	btst		&inex2_bit,FPCR_ENABLE(%a6) # is inexact enabled?
	beq.b		iea_op_store		# no
	bra.b		iea_op_exc_ovfl		# yes

# an enabled exception occurred. we have to insert the exception type back into
# the machine.
iea_op_exc:
	subi.l		&24,%d0			# fix offset to be 0-8
	cmpi.b		%d0,&0x6		# is exception INEX?
	bne.b		iea_op_exc_force	# no

# the enabled exception was inexact. so, if it occurs with an overflow
# or underflow that was disabled, then we have to force an overflow or
# underflow frame.
	btst		&ovfl_bit,FPSR_EXCEPT(%a6) # did overflow occur?
	bne.b		iea_op_exc_ovfl		# yes
	btst		&unfl_bit,FPSR_EXCEPT(%a6) # did underflow occur?
	bne.b		iea_op_exc_unfl		# yes

iea_op_exc_force:
	mov.w		(tbl_iea_except.b,%pc,%d0.w*2),2+FP_SRC(%a6)
	bra.b		iea_op_exit2		# exit with frestore

tbl_iea_except:
	short		0xe002, 0xe006, 0xe004, 0xe005
	short		0xe003, 0xe002, 0xe001, 0xe001

iea_op_exc_ovfl:
	mov.w		&0xe005,2+FP_SRC(%a6)
	bra.b		iea_op_exit2

iea_op_exc_unfl:
	mov.w		&0xe003,2+FP_SRC(%a6)

iea_op_exit2:
	mov.l		EXC_PC(%a6),USER_FPIAR(%a6) # set FPIAR to "Current PC"
	mov.l		EXC_EXTWPTR(%a6),EXC_PC(%a6) # set "Next PC" in exc frame

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)		# restore exceptional state

	unlk		%a6			# unravel the frame

	btst		&0x7,(%sp)		# is trace on?
	bne.b		iea_op_trace		# yes

	bra.l		_fpsp_done		# exit to os

#
# The opclass two instruction that took an "Unimplemented Effective Address"
# exception was being traced. Make the "current" PC the FPIAR and put it in
# the trace stack frame then jump to _real_trace().
#
#		 UNIMP EA FRAME		   TRACE FRAME
#		*****************	*****************
#		* 0x0 *  0x0f0	*	*    Current	*
#		*****************	*      PC	*
#		*    Current	*	*****************
#		*      PC	*	* 0x2 *  0x024	*
#		*****************	*****************
#		*      SR	*	*     Next	*
#		*****************	*      PC	*
#					*****************
#					*      SR	*
#					*****************
iea_op_trace:
	mov.l		(%sp),-(%sp)		# shift stack frame "down"
	mov.w		0x8(%sp),0x4(%sp)
	mov.w		&0x2024,0x6(%sp)	# stk fmt = 0x2; voff = 0x024
	fmov.l		%fpiar,0x8(%sp)		# "Current PC" is in FPIAR

	bra.l		_real_trace

#########################################################################
iea_fmovm:
	btst		&14,%d0			# ctrl or data reg
	beq.w		iea_fmovm_ctrl

iea_fmovm_data:

	btst		&0x5,EXC_SR(%a6)	# user or supervisor mode
	bne.b		iea_fmovm_data_s

iea_fmovm_data_u:
	mov.l		%usp,%a0
	mov.l		%a0,EXC_A7(%a6)		# store current a7
	bsr.l		fmovm_dynamic		# do dynamic fmovm
	mov.l		EXC_A7(%a6),%a0		# load possibly new a7
	mov.l		%a0,%usp		# update usp
	bra.w		iea_fmovm_exit

iea_fmovm_data_s:
	clr.b		SPCOND_FLG(%a6)
	lea		0x2+EXC_VOFF(%a6),%a0
	mov.l		%a0,EXC_A7(%a6)
	bsr.l		fmovm_dynamic		# do dynamic fmovm

	cmpi.b		SPCOND_FLG(%a6),&mda7_flg
	beq.w		iea_fmovm_data_predec
	cmpi.b		SPCOND_FLG(%a6),&mia7_flg
	bne.w		iea_fmovm_exit

# right now, d0 = the size.
# the data has been fetched from the supervisor stack, but we have not
# incremented the stack pointer by the appropriate number of bytes.
# do it here.
iea_fmovm_data_postinc:
	btst		&0x7,EXC_SR(%a6)
	bne.b		iea_fmovm_data_pi_trace

	mov.w		EXC_SR(%a6),(EXC_SR,%a6,%d0)
	mov.l		EXC_EXTWPTR(%a6),(EXC_PC,%a6,%d0)
	mov.w		&0x00f0,(EXC_VOFF,%a6,%d0)

	lea		(EXC_SR,%a6,%d0),%a0
	mov.l		%a0,EXC_SR(%a6)

	fmovm.x		EXC_FP0(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6
	mov.l		(%sp)+,%sp
	bra.l		_fpsp_done

iea_fmovm_data_pi_trace:
	mov.w		EXC_SR(%a6),(EXC_SR-0x4,%a6,%d0)
	mov.l		EXC_EXTWPTR(%a6),(EXC_PC-0x4,%a6,%d0)
	mov.w		&0x2024,(EXC_VOFF-0x4,%a6,%d0)
	mov.l		EXC_PC(%a6),(EXC_VOFF+0x2-0x4,%a6,%d0)

	lea		(EXC_SR-0x4,%a6,%d0),%a0
	mov.l		%a0,EXC_SR(%a6)

	fmovm.x		EXC_FP0(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6
	mov.l		(%sp)+,%sp
	bra.l		_real_trace

# right now, d1 = size and d0 = the strg.
iea_fmovm_data_predec:
	mov.b		%d1,EXC_VOFF(%a6)	# store strg
	mov.b		%d0,0x1+EXC_VOFF(%a6)	# store size

	fmovm.x		EXC_FP0(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	mov.l		(%a6),-(%sp)		# make a copy of a6
	mov.l		%d0,-(%sp)		# save d0
	mov.l		%d1,-(%sp)		# save d1
	mov.l		EXC_EXTWPTR(%a6),-(%sp)	# make a copy of Next PC

	clr.l		%d0
	mov.b		0x1+EXC_VOFF(%a6),%d0	# fetch size
	neg.l		%d0			# get negative of size

	btst		&0x7,EXC_SR(%a6)	# is trace enabled?
	beq.b		iea_fmovm_data_p2

	mov.w		EXC_SR(%a6),(EXC_SR-0x4,%a6,%d0)
	mov.l		EXC_PC(%a6),(EXC_VOFF-0x2,%a6,%d0)
	mov.l		(%sp)+,(EXC_PC-0x4,%a6,%d0)
	mov.w		&0x2024,(EXC_VOFF-0x4,%a6,%d0)

	pea		(%a6,%d0)		# create final sp
	bra.b		iea_fmovm_data_p3

iea_fmovm_data_p2:
	mov.w		EXC_SR(%a6),(EXC_SR,%a6,%d0)
	mov.l		(%sp)+,(EXC_PC,%a6,%d0)
	mov.w		&0x00f0,(EXC_VOFF,%a6,%d0)

	pea		(0x4,%a6,%d0)		# create final sp

iea_fmovm_data_p3:
	clr.l		%d1
	mov.b		EXC_VOFF(%a6),%d1	# fetch strg

	tst.b		%d1
	bpl.b		fm_1
	fmovm.x		&0x80,(0x4+0x8,%a6,%d0)
	addi.l		&0xc,%d0
fm_1:
	lsl.b		&0x1,%d1
	bpl.b		fm_2
	fmovm.x		&0x40,(0x4+0x8,%a6,%d0)
	addi.l		&0xc,%d0
fm_2:
	lsl.b		&0x1,%d1
	bpl.b		fm_3
	fmovm.x		&0x20,(0x4+0x8,%a6,%d0)
	addi.l		&0xc,%d0
fm_3:
	lsl.b		&0x1,%d1
	bpl.b		fm_4
	fmovm.x		&0x10,(0x4+0x8,%a6,%d0)
	addi.l		&0xc,%d0
fm_4:
	lsl.b		&0x1,%d1
	bpl.b		fm_5
	fmovm.x		&0x08,(0x4+0x8,%a6,%d0)
	addi.l		&0xc,%d0
fm_5:
	lsl.b		&0x1,%d1
	bpl.b		fm_6
	fmovm.x		&0x04,(0x4+0x8,%a6,%d0)
	addi.l		&0xc,%d0
fm_6:
	lsl.b		&0x1,%d1
	bpl.b		fm_7
	fmovm.x		&0x02,(0x4+0x8,%a6,%d0)
	addi.l		&0xc,%d0
fm_7:
	lsl.b		&0x1,%d1
	bpl.b		fm_end
	fmovm.x		&0x01,(0x4+0x8,%a6,%d0)
fm_end:
	mov.l		0x4(%sp),%d1
	mov.l		0x8(%sp),%d0
	mov.l		0xc(%sp),%a6
	mov.l		(%sp)+,%sp

	btst		&0x7,(%sp)		# is trace enabled?
	beq.l		_fpsp_done
	bra.l		_real_trace

#########################################################################
iea_fmovm_ctrl:

	bsr.l		fmovm_ctrl		# load ctrl regs

iea_fmovm_exit:
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	btst		&0x7,EXC_SR(%a6)	# is trace on?
	bne.b		iea_fmovm_trace		# yes

	mov.l		EXC_EXTWPTR(%a6),EXC_PC(%a6) # set Next PC

	unlk		%a6			# unravel the frame

	bra.l		_fpsp_done		# exit to os

#
# The control reg instruction that took an "Unimplemented Effective Address"
# exception was being traced. The "Current PC" for the trace frame is the
# PC stacked for Unimp EA. The "Next PC" is in EXC_EXTWPTR.
# After fixing the stack frame, jump to _real_trace().
#
#		 UNIMP EA FRAME		   TRACE FRAME
#		*****************	*****************
#		* 0x0 *  0x0f0	*	*    Current	*
#		*****************	*      PC	*
#		*    Current	*	*****************
#		*      PC	*	* 0x2 *  0x024	*
#		*****************	*****************
#		*      SR	*	*     Next	*
#		*****************	*      PC	*
#					*****************
#					*      SR	*
#					*****************
# this ain't a pretty solution, but it works:
# -restore a6 (not with unlk)
# -shift stack frame down over where old a6 used to be
# -add LOCAL_SIZE to stack pointer
iea_fmovm_trace:
	mov.l		(%a6),%a6		# restore frame pointer
	mov.w		EXC_SR+LOCAL_SIZE(%sp),0x0+LOCAL_SIZE(%sp)
	mov.l		EXC_PC+LOCAL_SIZE(%sp),0x8+LOCAL_SIZE(%sp)
	mov.l		EXC_EXTWPTR+LOCAL_SIZE(%sp),0x2+LOCAL_SIZE(%sp)
	mov.w		&0x2024,0x6+LOCAL_SIZE(%sp) # stk fmt = 0x2; voff = 0x024
	add.l		&LOCAL_SIZE,%sp		# clear stack frame

	bra.l		_real_trace

#########################################################################
# The FPU is disabled and so we should really have taken the "Line
# F Emulator" exception. So, here we create an 8-word stack frame
# from our 4-word stack frame. This means we must calculate the length
# the faulting instruction to get the "next PC". This is trivial for
# immediate operands but requires some extra work for fmovm dynamic
# which can use most addressing modes.
iea_disabled:
	mov.l		(%sp)+,%d0		# restore d0

	link		%a6,&-LOCAL_SIZE	# init stack frame

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1

# PC of instruction that took the exception is the PC in the frame
	mov.l		EXC_PC(%a6),EXC_EXTWPTR(%a6)
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch the instruction words
	mov.l		%d0,EXC_OPWORD(%a6)	# store OPWORD and EXTWORD

	tst.w		%d0			# is instr fmovm?
	bmi.b		iea_dis_fmovm		# yes
# instruction is using an extended precision immediate operand. therefore,
# the total instruction length is 16 bytes.
iea_dis_immed:
	mov.l		&0x10,%d0		# 16 bytes of instruction
	bra.b		iea_dis_cont
iea_dis_fmovm:
	btst		&0xe,%d0		# is instr fmovm ctrl
	bne.b		iea_dis_fmovm_data	# no
# the instruction is a fmovm.l with 2 or 3 registers.
	bfextu		%d0{&19:&3},%d1
	mov.l		&0xc,%d0
	cmpi.b		%d1,&0x7		# move all regs?
	bne.b		iea_dis_cont
	addq.l		&0x4,%d0
	bra.b		iea_dis_cont
# the instruction is an fmovm.x dynamic which can use many addressing
# modes and thus can have several different total instruction lengths.
# call fmovm_calc_ea which will go through the ea calc process and,
# as a by-product, will tell us how long the instruction is.
iea_dis_fmovm_data:
	clr.l		%d0
	bsr.l		fmovm_calc_ea
	mov.l		EXC_EXTWPTR(%a6),%d0
	sub.l		EXC_PC(%a6),%d0
iea_dis_cont:
	mov.w		%d0,EXC_VOFF(%a6)	# store stack shift value

	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6

# here, we actually create the 8-word frame from the 4-word frame,
# with the "next PC" as additional info.
# the <ea> field is let as undefined.
	subq.l		&0x8,%sp		# make room for new stack
	mov.l		%d0,-(%sp)		# save d0
	mov.w		0xc(%sp),0x4(%sp)	# move SR
	mov.l		0xe(%sp),0x6(%sp)	# move Current PC
	clr.l		%d0
	mov.w		0x12(%sp),%d0
	mov.l		0x6(%sp),0x10(%sp)	# move Current PC
	add.l		%d0,0x6(%sp)		# make Next PC
	mov.w		&0x402c,0xa(%sp)	# insert offset,frame format
	mov.l		(%sp)+,%d0		# restore d0

	bra.l		_real_fpu_disabled

##########

iea_iacc:
	movc		%pcr,%d0
	btst		&0x1,%d0
	bne.b		iea_iacc_cont
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1 on stack
iea_iacc_cont:
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6

	subq.w		&0x8,%sp		# make stack frame bigger
	mov.l		0x8(%sp),(%sp)		# store SR,hi(PC)
	mov.w		0xc(%sp),0x4(%sp)	# store lo(PC)
	mov.w		&0x4008,0x6(%sp)	# store voff
	mov.l		0x2(%sp),0x8(%sp)	# store ea
	mov.l		&0x09428001,0xc(%sp)	# store fslw

iea_acc_done:
	btst		&0x5,(%sp)		# user or supervisor mode?
	beq.b		iea_acc_done2		# user
	bset		&0x2,0xd(%sp)		# set supervisor TM bit

iea_acc_done2:
	bra.l		_real_access

iea_dacc:
	lea		-LOCAL_SIZE(%a6),%sp

	movc		%pcr,%d1
	btst		&0x1,%d1
	bne.b		iea_dacc_cont
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1 on stack
	fmovm.l		LOCAL_SIZE+USER_FPCR(%sp),%fpcr,%fpsr,%fpiar # restore ctrl regs
iea_dacc_cont:
	mov.l		(%a6),%a6

	mov.l		0x4+LOCAL_SIZE(%sp),-0x8+0x4+LOCAL_SIZE(%sp)
	mov.w		0x8+LOCAL_SIZE(%sp),-0x8+0x8+LOCAL_SIZE(%sp)
	mov.w		&0x4008,-0x8+0xa+LOCAL_SIZE(%sp)
	mov.l		%a0,-0x8+0xc+LOCAL_SIZE(%sp)
	mov.w		%d0,-0x8+0x10+LOCAL_SIZE(%sp)
	mov.w		&0x0001,-0x8+0x12+LOCAL_SIZE(%sp)

	movm.l		LOCAL_SIZE+EXC_DREGS(%sp),&0x0303 # restore d0-d1/a0-a1
	add.w		&LOCAL_SIZE-0x4,%sp

	bra.b		iea_acc_done

#########################################################################
# XDEF ****************************************************************	#
#	_fpsp_operr(): 060FPSP entry point for FP Operr exception.	#
#									#
#	This handler should be the first code executed upon taking the	#
#	FP Operand Error exception in an operating system.		#
#									#
# XREF ****************************************************************	#
#	_imem_read_long() - read instruction longword			#
#	fix_skewed_ops() - adjust src operand in fsave frame		#
#	_real_operr() - "callout" to operating system operr handler	#
#	_dmem_write_{byte,word,long}() - store data to mem (opclass 3)	#
#	store_dreg_{b,w,l}() - store data to data regfile (opclass 3)	#
#	facc_out_{b,w,l}() - store to memory took access error (opcl 3)	#
#									#
# INPUT ***************************************************************	#
#	- The system stack contains the FP Operr exception frame	#
#	- The fsave frame contains the source operand			#
#									#
# OUTPUT **************************************************************	#
#	No access error:						#
#	- The system stack is unchanged					#
#	- The fsave frame contains the adjusted src op for opclass 0,2	#
#									#
# ALGORITHM ***********************************************************	#
#	In a system where the FP Operr exception is enabled, the goal	#
# is to get to the handler specified at _real_operr(). But, on the 060,	#
# for opclass zero and two instruction taking this exception, the	#
# input operand in the fsave frame may be incorrect for some cases	#
# and needs to be corrected. This handler calls fix_skewed_ops() to	#
# do just this and then exits through _real_operr().			#
#	For opclass 3 instructions, the 060 doesn't store the default	#
# operr result out to memory or data register file as it should.	#
# This code must emulate the move out before finally exiting through	#
# _real_inex(). The move out, if to memory, is performed using		#
# _mem_write() "callout" routines that may return a failing result.	#
# In this special case, the handler must exit through facc_out()	#
# which creates an access error stack frame from the current operr	#
# stack frame.								#
#									#
#########################################################################

	global		_fpsp_operr
_fpsp_operr:

	link.w		%a6,&-LOCAL_SIZE	# init stack frame

	fsave		FP_SRC(%a6)		# grab the "busy" frame

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,%fpiar,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FPREGS(%a6)	# save fp0-fp1 on stack

# the FPIAR holds the "current PC" of the faulting instruction
	mov.l		USER_FPIAR(%a6),EXC_EXTWPTR(%a6)

	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch the instruction words
	mov.l		%d0,EXC_OPWORD(%a6)

##############################################################################

	btst		&13,%d0			# is instr an fmove out?
	bne.b		foperr_out		# fmove out


# here, we simply see if the operand in the fsave frame needs to be "unskewed".
# this would be the case for opclass two operations with a source infinity or
# denorm operand in the sgl or dbl format. NANs also become skewed, but can't
# cause an operr so we don't need to check for them here.
	lea		FP_SRC(%a6),%a0		# pass: ptr to src op
	bsr.l		fix_skewed_ops		# fix src op

foperr_exit:
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)

	unlk		%a6
	bra.l		_real_operr

########################################################################

#
# the hardware does not save the default result to memory on enabled
# operand error exceptions. we do this here before passing control to
# the user operand error handler.
#
# byte, word, and long destination format operations can pass
# through here. we simply need to test the sign of the src
# operand and save the appropriate minimum or maximum integer value
# to the effective address as pointed to by the stacked effective address.
#
# although packed opclass three operations can take operand error
# exceptions, they won't pass through here since they are caught
# first by the unsupported data format exception handler. that handler
# sends them directly to _real_operr() if necessary.
#
foperr_out:

	mov.w		FP_SRC_EX(%a6),%d1	# fetch exponent
	andi.w		&0x7fff,%d1
	cmpi.w		%d1,&0x7fff
	bne.b		foperr_out_not_qnan
# the operand is either an infinity or a QNAN.
	tst.l		FP_SRC_LO(%a6)
	bne.b		foperr_out_qnan
	mov.l		FP_SRC_HI(%a6),%d1
	andi.l		&0x7fffffff,%d1
	beq.b		foperr_out_not_qnan
foperr_out_qnan:
	mov.l		FP_SRC_HI(%a6),L_SCR1(%a6)
	bra.b		foperr_out_jmp

foperr_out_not_qnan:
	mov.l		&0x7fffffff,%d1
	tst.b		FP_SRC_EX(%a6)
	bpl.b		foperr_out_not_qnan2
	addq.l		&0x1,%d1
foperr_out_not_qnan2:
	mov.l		%d1,L_SCR1(%a6)

foperr_out_jmp:
	bfextu		%d0{&19:&3},%d0		# extract dst format field
	mov.b		1+EXC_OPWORD(%a6),%d1	# extract <ea> mode,reg
	mov.w		(tbl_operr.b,%pc,%d0.w*2),%a0
	jmp		(tbl_operr.b,%pc,%a0)

tbl_operr:
	short		foperr_out_l - tbl_operr # long word integer
	short		tbl_operr    - tbl_operr # sgl prec shouldn't happen
	short		tbl_operr    - tbl_operr # ext prec shouldn't happen
	short		foperr_exit  - tbl_operr # packed won't enter here
	short		foperr_out_w - tbl_operr # word integer
	short		tbl_operr    - tbl_operr # dbl prec shouldn't happen
	short		foperr_out_b - tbl_operr # byte integer
	short		tbl_operr    - tbl_operr # packed won't enter here

foperr_out_b:
	mov.b		L_SCR1(%a6),%d0		# load positive default result
	cmpi.b		%d1,&0x7		# is <ea> mode a data reg?
	ble.b		foperr_out_b_save_dn	# yes
	mov.l		EXC_EA(%a6),%a0		# pass: <ea> of default result
	bsr.l		_dmem_write_byte	# write the default result

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_b		# yes

	bra.w		foperr_exit
foperr_out_b_save_dn:
	andi.w		&0x0007,%d1
	bsr.l		store_dreg_b		# store result to regfile
	bra.w		foperr_exit

foperr_out_w:
	mov.w		L_SCR1(%a6),%d0		# load positive default result
	cmpi.b		%d1,&0x7		# is <ea> mode a data reg?
	ble.b		foperr_out_w_save_dn	# yes
	mov.l		EXC_EA(%a6),%a0		# pass: <ea> of default result
	bsr.l		_dmem_write_word	# write the default result

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_w		# yes

	bra.w		foperr_exit
foperr_out_w_save_dn:
	andi.w		&0x0007,%d1
	bsr.l		store_dreg_w		# store result to regfile
	bra.w		foperr_exit

foperr_out_l:
	mov.l		L_SCR1(%a6),%d0		# load positive default result
	cmpi.b		%d1,&0x7		# is <ea> mode a data reg?
	ble.b		foperr_out_l_save_dn	# yes
	mov.l		EXC_EA(%a6),%a0		# pass: <ea> of default result
	bsr.l		_dmem_write_long	# write the default result

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_l		# yes

	bra.w		foperr_exit
foperr_out_l_save_dn:
	andi.w		&0x0007,%d1
	bsr.l		store_dreg_l		# store result to regfile
	bra.w		foperr_exit

#########################################################################
# XDEF ****************************************************************	#
#	_fpsp_snan(): 060FPSP entry point for FP SNAN exception.	#
#									#
#	This handler should be the first code executed upon taking the	#
#	FP Signalling NAN exception in an operating system.		#
#									#
# XREF ****************************************************************	#
#	_imem_read_long() - read instruction longword			#
#	fix_skewed_ops() - adjust src operand in fsave frame		#
#	_real_snan() - "callout" to operating system SNAN handler	#
#	_dmem_write_{byte,word,long}() - store data to mem (opclass 3)	#
#	store_dreg_{b,w,l}() - store data to data regfile (opclass 3)	#
#	facc_out_{b,w,l,d,x}() - store to mem took acc error (opcl 3)	#
#	_calc_ea_fout() - fix An if <ea> is -() or ()+; also get <ea>	#
#									#
# INPUT ***************************************************************	#
#	- The system stack contains the FP SNAN exception frame		#
#	- The fsave frame contains the source operand			#
#									#
# OUTPUT **************************************************************	#
#	No access error:						#
#	- The system stack is unchanged					#
#	- The fsave frame contains the adjusted src op for opclass 0,2	#
#									#
# ALGORITHM ***********************************************************	#
#	In a system where the FP SNAN exception is enabled, the goal	#
# is to get to the handler specified at _real_snan(). But, on the 060,	#
# for opclass zero and two instructions taking this exception, the	#
# input operand in the fsave frame may be incorrect for some cases	#
# and needs to be corrected. This handler calls fix_skewed_ops() to	#
# do just this and then exits through _real_snan().			#
#	For opclass 3 instructions, the 060 doesn't store the default	#
# SNAN result out to memory or data register file as it should.		#
# This code must emulate the move out before finally exiting through	#
# _real_snan(). The move out, if to memory, is performed using		#
# _mem_write() "callout" routines that may return a failing result.	#
# In this special case, the handler must exit through facc_out()	#
# which creates an access error stack frame from the current SNAN	#
# stack frame.								#
#	For the case of an extended precision opclass 3 instruction,	#
# if the effective addressing mode was -() or ()+, then the address	#
# register must get updated by calling _calc_ea_fout(). If the <ea>	#
# was -(a7) from supervisor mode, then the exception frame currently	#
# on the system stack must be carefully moved "down" to make room	#
# for the operand being moved.						#
#									#
#########################################################################

	global		_fpsp_snan
_fpsp_snan:

	link.w		%a6,&-LOCAL_SIZE	# init stack frame

	fsave		FP_SRC(%a6)		# grab the "busy" frame

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,%fpiar,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FPREGS(%a6)	# save fp0-fp1 on stack

# the FPIAR holds the "current PC" of the faulting instruction
	mov.l		USER_FPIAR(%a6),EXC_EXTWPTR(%a6)

	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch the instruction words
	mov.l		%d0,EXC_OPWORD(%a6)

##############################################################################

	btst		&13,%d0			# is instr an fmove out?
	bne.w		fsnan_out		# fmove out


# here, we simply see if the operand in the fsave frame needs to be "unskewed".
# this would be the case for opclass two operations with a source infinity or
# denorm operand in the sgl or dbl format. NANs also become skewed and must be
# fixed here.
	lea		FP_SRC(%a6),%a0		# pass: ptr to src op
	bsr.l		fix_skewed_ops		# fix src op

fsnan_exit:
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)

	unlk		%a6
	bra.l		_real_snan

########################################################################

#
# the hardware does not save the default result to memory on enabled
# snan exceptions. we do this here before passing control to
# the user snan handler.
#
# byte, word, long, and packed destination format operations can pass
# through here. since packed format operations already were handled by
# fpsp_unsupp(), then we need to do nothing else for them here.
# for byte, word, and long, we simply need to test the sign of the src
# operand and save the appropriate minimum or maximum integer value
# to the effective address as pointed to by the stacked effective address.
#
fsnan_out:

	bfextu		%d0{&19:&3},%d0		# extract dst format field
	mov.b		1+EXC_OPWORD(%a6),%d1	# extract <ea> mode,reg
	mov.w		(tbl_snan.b,%pc,%d0.w*2),%a0
	jmp		(tbl_snan.b,%pc,%a0)

tbl_snan:
	short		fsnan_out_l - tbl_snan # long word integer
	short		fsnan_out_s - tbl_snan # sgl prec shouldn't happen
	short		fsnan_out_x - tbl_snan # ext prec shouldn't happen
	short		tbl_snan    - tbl_snan # packed needs no help
	short		fsnan_out_w - tbl_snan # word integer
	short		fsnan_out_d - tbl_snan # dbl prec shouldn't happen
	short		fsnan_out_b - tbl_snan # byte integer
	short		tbl_snan    - tbl_snan # packed needs no help

fsnan_out_b:
	mov.b		FP_SRC_HI(%a6),%d0	# load upper byte of SNAN
	bset		&6,%d0			# set SNAN bit
	cmpi.b		%d1,&0x7		# is <ea> mode a data reg?
	ble.b		fsnan_out_b_dn		# yes
	mov.l		EXC_EA(%a6),%a0		# pass: <ea> of default result
	bsr.l		_dmem_write_byte	# write the default result

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_b		# yes

	bra.w		fsnan_exit
fsnan_out_b_dn:
	andi.w		&0x0007,%d1
	bsr.l		store_dreg_b		# store result to regfile
	bra.w		fsnan_exit

fsnan_out_w:
	mov.w		FP_SRC_HI(%a6),%d0	# load upper word of SNAN
	bset		&14,%d0			# set SNAN bit
	cmpi.b		%d1,&0x7		# is <ea> mode a data reg?
	ble.b		fsnan_out_w_dn		# yes
	mov.l		EXC_EA(%a6),%a0		# pass: <ea> of default result
	bsr.l		_dmem_write_word	# write the default result

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_w		# yes

	bra.w		fsnan_exit
fsnan_out_w_dn:
	andi.w		&0x0007,%d1
	bsr.l		store_dreg_w		# store result to regfile
	bra.w		fsnan_exit

fsnan_out_l:
	mov.l		FP_SRC_HI(%a6),%d0	# load upper longword of SNAN
	bset		&30,%d0			# set SNAN bit
	cmpi.b		%d1,&0x7		# is <ea> mode a data reg?
	ble.b		fsnan_out_l_dn		# yes
	mov.l		EXC_EA(%a6),%a0		# pass: <ea> of default result
	bsr.l		_dmem_write_long	# write the default result

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_l		# yes

	bra.w		fsnan_exit
fsnan_out_l_dn:
	andi.w		&0x0007,%d1
	bsr.l		store_dreg_l		# store result to regfile
	bra.w		fsnan_exit

fsnan_out_s:
	cmpi.b		%d1,&0x7		# is <ea> mode a data reg?
	ble.b		fsnan_out_d_dn		# yes
	mov.l		FP_SRC_EX(%a6),%d0	# fetch SNAN sign
	andi.l		&0x80000000,%d0		# keep sign
	ori.l		&0x7fc00000,%d0		# insert new exponent,SNAN bit
	mov.l		FP_SRC_HI(%a6),%d1	# load mantissa
	lsr.l		&0x8,%d1		# shift mantissa for sgl
	or.l		%d1,%d0			# create sgl SNAN
	mov.l		EXC_EA(%a6),%a0		# pass: <ea> of default result
	bsr.l		_dmem_write_long	# write the default result

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_l		# yes

	bra.w		fsnan_exit
fsnan_out_d_dn:
	mov.l		FP_SRC_EX(%a6),%d0	# fetch SNAN sign
	andi.l		&0x80000000,%d0		# keep sign
	ori.l		&0x7fc00000,%d0		# insert new exponent,SNAN bit
	mov.l		%d1,-(%sp)
	mov.l		FP_SRC_HI(%a6),%d1	# load mantissa
	lsr.l		&0x8,%d1		# shift mantissa for sgl
	or.l		%d1,%d0			# create sgl SNAN
	mov.l		(%sp)+,%d1
	andi.w		&0x0007,%d1
	bsr.l		store_dreg_l		# store result to regfile
	bra.w		fsnan_exit

fsnan_out_d:
	mov.l		FP_SRC_EX(%a6),%d0	# fetch SNAN sign
	andi.l		&0x80000000,%d0		# keep sign
	ori.l		&0x7ff80000,%d0		# insert new exponent,SNAN bit
	mov.l		FP_SRC_HI(%a6),%d1	# load hi mantissa
	mov.l		%d0,FP_SCR0_EX(%a6)	# store to temp space
	mov.l		&11,%d0			# load shift amt
	lsr.l		%d0,%d1
	or.l		%d1,FP_SCR0_EX(%a6)	# create dbl hi
	mov.l		FP_SRC_HI(%a6),%d1	# load hi mantissa
	andi.l		&0x000007ff,%d1
	ror.l		%d0,%d1
	mov.l		%d1,FP_SCR0_HI(%a6)	# store to temp space
	mov.l		FP_SRC_LO(%a6),%d1	# load lo mantissa
	lsr.l		%d0,%d1
	or.l		%d1,FP_SCR0_HI(%a6)	# create dbl lo
	lea		FP_SCR0(%a6),%a0	# pass: ptr to operand
	mov.l		EXC_EA(%a6),%a1		# pass: dst addr
	movq.l		&0x8,%d0		# pass: size of 8 bytes
	bsr.l		_dmem_write		# write the default result

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_d		# yes

	bra.w		fsnan_exit

# for extended precision, if the addressing mode is pre-decrement or
# post-increment, then the address register did not get updated.
# in addition, for pre-decrement, the stacked <ea> is incorrect.
fsnan_out_x:
	clr.b		SPCOND_FLG(%a6)		# clear special case flag

	mov.w		FP_SRC_EX(%a6),FP_SCR0_EX(%a6)
	clr.w		2+FP_SCR0(%a6)
	mov.l		FP_SRC_HI(%a6),%d0
	bset		&30,%d0
	mov.l		%d0,FP_SCR0_HI(%a6)
	mov.l		FP_SRC_LO(%a6),FP_SCR0_LO(%a6)

	btst		&0x5,EXC_SR(%a6)	# supervisor mode exception?
	bne.b		fsnan_out_x_s		# yes

	mov.l		%usp,%a0		# fetch user stack pointer
	mov.l		%a0,EXC_A7(%a6)		# save on stack for calc_ea()
	mov.l		(%a6),EXC_A6(%a6)

	bsr.l		_calc_ea_fout		# find the correct ea,update An
	mov.l		%a0,%a1
	mov.l		%a0,EXC_EA(%a6)		# stack correct <ea>

	mov.l		EXC_A7(%a6),%a0
	mov.l		%a0,%usp		# restore user stack pointer
	mov.l		EXC_A6(%a6),(%a6)

fsnan_out_x_save:
	lea		FP_SCR0(%a6),%a0	# pass: ptr to operand
	movq.l		&0xc,%d0		# pass: size of extended
	bsr.l		_dmem_write		# write the default result

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_x		# yes

	bra.w		fsnan_exit

fsnan_out_x_s:
	mov.l		(%a6),EXC_A6(%a6)

	bsr.l		_calc_ea_fout		# find the correct ea,update An
	mov.l		%a0,%a1
	mov.l		%a0,EXC_EA(%a6)		# stack correct <ea>

	mov.l		EXC_A6(%a6),(%a6)

	cmpi.b		SPCOND_FLG(%a6),&mda7_flg # is <ea> mode -(a7)?
	bne.b		fsnan_out_x_save	# no

# the operation was "fmove.x SNAN,-(a7)" from supervisor mode.
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)

	mov.l		EXC_A6(%a6),%a6		# restore frame pointer

	mov.l		LOCAL_SIZE+EXC_SR(%sp),LOCAL_SIZE+EXC_SR-0xc(%sp)
	mov.l		LOCAL_SIZE+EXC_PC+0x2(%sp),LOCAL_SIZE+EXC_PC+0x2-0xc(%sp)
	mov.l		LOCAL_SIZE+EXC_EA(%sp),LOCAL_SIZE+EXC_EA-0xc(%sp)

	mov.l		LOCAL_SIZE+FP_SCR0_EX(%sp),LOCAL_SIZE+EXC_SR(%sp)
	mov.l		LOCAL_SIZE+FP_SCR0_HI(%sp),LOCAL_SIZE+EXC_PC+0x2(%sp)
	mov.l		LOCAL_SIZE+FP_SCR0_LO(%sp),LOCAL_SIZE+EXC_EA(%sp)

	add.l		&LOCAL_SIZE-0x8,%sp

	bra.l		_real_snan

#########################################################################
# XDEF ****************************************************************	#
#	_fpsp_inex(): 060FPSP entry point for FP Inexact exception.	#
#									#
#	This handler should be the first code executed upon taking the	#
#	FP Inexact exception in an operating system.			#
#									#
# XREF ****************************************************************	#
#	_imem_read_long() - read instruction longword			#
#	fix_skewed_ops() - adjust src operand in fsave frame		#
#	set_tag_x() - determine optype of src/dst operands		#
#	store_fpreg() - store opclass 0 or 2 result to FP regfile	#
#	unnorm_fix() - change UNNORM operands to NORM or ZERO		#
#	load_fpn2() - load dst operand from FP regfile			#
#	smovcr() - emulate an "fmovcr" instruction			#
#	fout() - emulate an opclass 3 instruction			#
#	tbl_unsupp - add of table of emulation routines for opclass 0,2	#
#	_real_inex() - "callout" to operating system inexact handler	#
#									#
# INPUT ***************************************************************	#
#	- The system stack contains the FP Inexact exception frame	#
#	- The fsave frame contains the source operand			#
#									#
# OUTPUT **************************************************************	#
#	- The system stack is unchanged					#
#	- The fsave frame contains the adjusted src op for opclass 0,2	#
#									#
# ALGORITHM ***********************************************************	#
#	In a system where the FP Inexact exception is enabled, the goal	#
# is to get to the handler specified at _real_inex(). But, on the 060,	#
# for opclass zero and two instruction taking this exception, the	#
# hardware doesn't store the correct result to the destination FP	#
# register as did the '040 and '881/2. This handler must emulate the	#
# instruction in order to get this value and then store it to the	#
# correct register before calling _real_inex().				#
#	For opclass 3 instructions, the 060 doesn't store the default	#
# inexact result out to memory or data register file as it should.	#
# This code must emulate the move out by calling fout() before finally	#
# exiting through _real_inex().						#
#									#
#########################################################################

	global		_fpsp_inex
_fpsp_inex:

	link.w		%a6,&-LOCAL_SIZE	# init stack frame

	fsave		FP_SRC(%a6)		# grab the "busy" frame

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,%fpiar,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FPREGS(%a6)	# save fp0-fp1 on stack

# the FPIAR holds the "current PC" of the faulting instruction
	mov.l		USER_FPIAR(%a6),EXC_EXTWPTR(%a6)

	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch the instruction words
	mov.l		%d0,EXC_OPWORD(%a6)

##############################################################################

	btst		&13,%d0			# is instr an fmove out?
	bne.w		finex_out		# fmove out


# the hardware, for "fabs" and "fneg" w/ a long source format, puts the
# longword integer directly into the upper longword of the mantissa along
# w/ an exponent value of 0x401e. we convert this to extended precision here.
	bfextu		%d0{&19:&3},%d0		# fetch instr size
	bne.b		finex_cont		# instr size is not long
	cmpi.w		FP_SRC_EX(%a6),&0x401e	# is exponent 0x401e?
	bne.b		finex_cont		# no
	fmov.l		&0x0,%fpcr
	fmov.l		FP_SRC_HI(%a6),%fp0	# load integer src
	fmov.x		%fp0,FP_SRC(%a6)	# store integer as extended precision
	mov.w		&0xe001,0x2+FP_SRC(%a6)

finex_cont:
	lea		FP_SRC(%a6),%a0		# pass: ptr to src op
	bsr.l		fix_skewed_ops		# fix src op

# Here, we zero the ccode and exception byte field since we're going to
# emulate the whole instruction. Notice, though, that we don't kill the
# INEX1 bit. This is because a packed op has long since been converted
# to extended before arriving here. Therefore, we need to retain the
# INEX1 bit from when the operand was first converted.
	andi.l		&0x00ff01ff,USER_FPSR(%a6) # zero all but accured field

	fmov.l		&0x0,%fpcr		# zero current control regs
	fmov.l		&0x0,%fpsr

	bfextu		EXC_EXTWORD(%a6){&0:&6},%d1 # extract upper 6 of cmdreg
	cmpi.b		%d1,&0x17		# is op an fmovecr?
	beq.w		finex_fmovcr		# yes

	lea		FP_SRC(%a6),%a0		# pass: ptr to src op
	bsr.l		set_tag_x		# tag the operand type
	mov.b		%d0,STAG(%a6)		# maybe NORM,DENORM

# bits four and five of the fp extension word separate the monadic and dyadic
# operations that can pass through fpsp_inex(). remember that fcmp and ftst
# will never take this exception, but fsincos will.
	btst		&0x5,1+EXC_CMDREG(%a6)	# is operation monadic or dyadic?
	beq.b		finex_extract		# monadic

	btst		&0x4,1+EXC_CMDREG(%a6)	# is operation an fsincos?
	bne.b		finex_extract		# yes

	bfextu		EXC_CMDREG(%a6){&6:&3},%d0 # dyadic; load dst reg
	bsr.l		load_fpn2		# load dst into FP_DST

	lea		FP_DST(%a6),%a0		# pass: ptr to dst op
	bsr.l		set_tag_x		# tag the operand type
	cmpi.b		%d0,&UNNORM		# is operand an UNNORM?
	bne.b		finex_op2_done		# no
	bsr.l		unnorm_fix		# yes; convert to NORM,DENORM,or ZERO
finex_op2_done:
	mov.b		%d0,DTAG(%a6)		# save dst optype tag

finex_extract:
	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd prec/mode

	mov.b		1+EXC_CMDREG(%a6),%d1
	andi.w		&0x007f,%d1		# extract extension

	lea		FP_SRC(%a6),%a0
	lea		FP_DST(%a6),%a1

	mov.l		(tbl_unsupp.l,%pc,%d1.w*4),%d1 # fetch routine addr
	jsr		(tbl_unsupp.l,%pc,%d1.l*1)

# the operation has been emulated. the result is in fp0.
finex_save:
	bfextu		EXC_CMDREG(%a6){&6:&3},%d0
	bsr.l		store_fpreg

finex_exit:
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)

	unlk		%a6
	bra.l		_real_inex

finex_fmovcr:
	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd prec,mode
	mov.b		1+EXC_CMDREG(%a6),%d1
	andi.l		&0x0000007f,%d1		# pass rom offset
	bsr.l		smovcr
	bra.b		finex_save

########################################################################

#
# the hardware does not save the default result to memory on enabled
# inexact exceptions. we do this here before passing control to
# the user inexact handler.
#
# byte, word, and long destination format operations can pass
# through here. so can double and single precision.
# although packed opclass three operations can take inexact
# exceptions, they won't pass through here since they are caught
# first by the unsupported data format exception handler. that handler
# sends them directly to _real_inex() if necessary.
#
finex_out:

	mov.b		&NORM,STAG(%a6)		# src is a NORM

	clr.l		%d0
	mov.b		FPCR_MODE(%a6),%d0	# pass rnd prec,mode

	andi.l		&0xffff00ff,USER_FPSR(%a6) # zero exception field

	lea		FP_SRC(%a6),%a0		# pass ptr to src operand

	bsr.l		fout			# store the default result

	bra.b		finex_exit

#########################################################################
# XDEF ****************************************************************	#
#	_fpsp_dz(): 060FPSP entry point for FP DZ exception.		#
#									#
#	This handler should be the first code executed upon taking	#
#	the FP DZ exception in an operating system.			#
#									#
# XREF ****************************************************************	#
#	_imem_read_long() - read instruction longword from memory	#
#	fix_skewed_ops() - adjust fsave operand				#
#	_real_dz() - "callout" exit point from FP DZ handler		#
#									#
# INPUT ***************************************************************	#
#	- The system stack contains the FP DZ exception stack.		#
#	- The fsave frame contains the source operand.			#
#									#
# OUTPUT **************************************************************	#
#	- The system stack contains the FP DZ exception stack.		#
#	- The fsave frame contains the adjusted source operand.		#
#									#
# ALGORITHM ***********************************************************	#
#	In a system where the DZ exception is enabled, the goal is to	#
# get to the handler specified at _real_dz(). But, on the 060, when the	#
# exception is taken, the input operand in the fsave state frame may	#
# be incorrect for some cases and need to be adjusted. So, this package	#
# adjusts the operand using fix_skewed_ops() and then branches to	#
# _real_dz().								#
#									#
#########################################################################

	global		_fpsp_dz
_fpsp_dz:

	link.w		%a6,&-LOCAL_SIZE	# init stack frame

	fsave		FP_SRC(%a6)		# grab the "busy" frame

	movm.l		&0x0303,EXC_DREGS(%a6)	# save d0-d1/a0-a1
	fmovm.l		%fpcr,%fpsr,%fpiar,USER_FPCR(%a6) # save ctrl regs
	fmovm.x		&0xc0,EXC_FPREGS(%a6)	# save fp0-fp1 on stack

# the FPIAR holds the "current PC" of the faulting instruction
	mov.l		USER_FPIAR(%a6),EXC_EXTWPTR(%a6)

	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch the instruction words
	mov.l		%d0,EXC_OPWORD(%a6)

##############################################################################


# here, we simply see if the operand in the fsave frame needs to be "unskewed".
# this would be the case for opclass two operations with a source zero
# in the sgl or dbl format.
	lea		FP_SRC(%a6),%a0		# pass: ptr to src op
	bsr.l		fix_skewed_ops		# fix src op

fdz_exit:
	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	frestore	FP_SRC(%a6)

	unlk		%a6
	bra.l		_real_dz

#########################################################################
# XDEF ****************************************************************	#
#	_fpsp_fline(): 060FPSP entry point for "Line F emulator"	#
#		       exception when the "reduced" version of the	#
#		       FPSP is implemented that does not emulate	#
#		       FP unimplemented instructions.			#
#									#
#	This handler should be the first code executed upon taking a	#
#	"Line F Emulator" exception in an operating system integrating	#
#	the reduced version of 060FPSP.					#
#									#
# XREF ****************************************************************	#
#	_real_fpu_disabled() - Handle "FPU disabled" exceptions		#
#	_real_fline() - Handle all other cases (treated equally)	#
#									#
# INPUT ***************************************************************	#
#	- The system stack contains a "Line F Emulator" exception	#
#	  stack frame.							#
#									#
# OUTPUT **************************************************************	#
#	- The system stack is unchanged.				#
#									#
# ALGORITHM ***********************************************************	#
#	When a "Line F Emulator" exception occurs in a system where	#
# "FPU Unimplemented" instructions will not be emulated, the exception	#
# can occur because then FPU is disabled or the instruction is to be	#
# classifed as "Line F". This module determines which case exists and	#
# calls the appropriate "callout".					#
#									#
#########################################################################

	global		_fpsp_fline
_fpsp_fline:

# check to see if the FPU is disabled. if so, jump to the OS entry
# point for that condition.
	cmpi.w		0x6(%sp),&0x402c
	beq.l		_real_fpu_disabled

	bra.l		_real_fline

#########################################################################
# XDEF ****************************************************************	#
#	_dcalc_ea(): calc correct <ea> from <ea> stacked on exception	#
#									#
# XREF ****************************************************************	#
#	inc_areg() - increment an address register			#
#	dec_areg() - decrement an address register			#
#									#
# INPUT ***************************************************************	#
#	d0 = number of bytes to adjust <ea> by				#
#									#
# OUTPUT **************************************************************	#
#	None								#
#									#
# ALGORITHM ***********************************************************	#
# "Dummy" CALCulate Effective Address:					#
#	The stacked <ea> for FP unimplemented instructions and opclass	#
#	two packed instructions is correct with the exception of...	#
#									#
#	1) -(An)   : The register is not updated regardless of size.	#
#		     Also, for extended precision and packed, the	#
#		     stacked <ea> value is 8 bytes too big		#
#	2) (An)+   : The register is not updated.			#
#	3) #<data> : The upper longword of the immediate operand is	#
#		     stacked b,w,l and s sizes are completely stacked.	#
#		     d,x, and p are not.				#
#									#
#########################################################################

	global		_dcalc_ea
_dcalc_ea:
	mov.l		%d0, %a0		# move # bytes to %a0

	mov.b		1+EXC_OPWORD(%a6), %d0	# fetch opcode word
	mov.l		%d0, %d1		# make a copy

	andi.w		&0x38, %d0		# extract mode field
	andi.l		&0x7, %d1		# extract reg  field

	cmpi.b		%d0,&0x18		# is mode (An)+ ?
	beq.b		dcea_pi			# yes

	cmpi.b		%d0,&0x20		# is mode -(An) ?
	beq.b		dcea_pd			# yes

	or.w		%d1,%d0			# concat mode,reg
	cmpi.b		%d0,&0x3c		# is mode #<data>?

	beq.b		dcea_imm		# yes

	mov.l		EXC_EA(%a6),%a0		# return <ea>
	rts

# need to set immediate data flag here since we'll need to do
# an imem_read to fetch this later.
dcea_imm:
	mov.b		&immed_flg,SPCOND_FLG(%a6)
	lea		([USER_FPIAR,%a6],0x4),%a0 # no; return <ea>
	rts

# here, the <ea> is stacked correctly. however, we must update the
# address register...
dcea_pi:
	mov.l		%a0,%d0			# pass amt to inc by
	bsr.l		inc_areg		# inc addr register

	mov.l		EXC_EA(%a6),%a0		# stacked <ea> is correct
	rts

# the <ea> is stacked correctly for all but extended and packed which
# the <ea>s are 8 bytes too large.
# it would make no sense to have a pre-decrement to a7 in supervisor
# mode so we don't even worry about this tricky case here : )
dcea_pd:
	mov.l		%a0,%d0			# pass amt to dec by
	bsr.l		dec_areg		# dec addr register

	mov.l		EXC_EA(%a6),%a0		# stacked <ea> is correct

	cmpi.b		%d0,&0xc		# is opsize ext or packed?
	beq.b		dcea_pd2		# yes
	rts
dcea_pd2:
	sub.l		&0x8,%a0		# correct <ea>
	mov.l		%a0,EXC_EA(%a6)		# put correct <ea> on stack
	rts

#########################################################################
# XDEF ****************************************************************	#
#	_calc_ea_fout(): calculate correct stacked <ea> for extended	#
#			 and packed data opclass 3 operations.		#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	None								#
#									#
# OUTPUT **************************************************************	#
#	a0 = return correct effective address				#
#									#
# ALGORITHM ***********************************************************	#
#	For opclass 3 extended and packed data operations, the <ea>	#
# stacked for the exception is incorrect for -(an) and (an)+ addressing	#
# modes. Also, while we're at it, the index register itself must get	#
# updated.								#
#	So, for -(an), we must subtract 8 off of the stacked <ea> value	#
# and return that value as the correct <ea> and store that value in An.	#
# For (an)+, the stacked <ea> is correct but we must adjust An by +12.	#
#									#
#########################################################################

# This calc_ea is currently used to retrieve the correct <ea>
# for fmove outs of type extended and packed.
	global		_calc_ea_fout
_calc_ea_fout:
	mov.b		1+EXC_OPWORD(%a6),%d0	# fetch opcode word
	mov.l		%d0,%d1			# make a copy

	andi.w		&0x38,%d0		# extract mode field
	andi.l		&0x7,%d1		# extract reg  field

	cmpi.b		%d0,&0x18		# is mode (An)+ ?
	beq.b		ceaf_pi			# yes

	cmpi.b		%d0,&0x20		# is mode -(An) ?
	beq.w		ceaf_pd			# yes

	mov.l		EXC_EA(%a6),%a0		# stacked <ea> is correct
	rts

# (An)+ : extended and packed fmove out
#	: stacked <ea> is correct
#	: "An" not updated
ceaf_pi:
	mov.w		(tbl_ceaf_pi.b,%pc,%d1.w*2),%d1
	mov.l		EXC_EA(%a6),%a0
	jmp		(tbl_ceaf_pi.b,%pc,%d1.w*1)

	swbeg		&0x8
tbl_ceaf_pi:
	short		ceaf_pi0 - tbl_ceaf_pi
	short		ceaf_pi1 - tbl_ceaf_pi
	short		ceaf_pi2 - tbl_ceaf_pi
	short		ceaf_pi3 - tbl_ceaf_pi
	short		ceaf_pi4 - tbl_ceaf_pi
	short		ceaf_pi5 - tbl_ceaf_pi
	short		ceaf_pi6 - tbl_ceaf_pi
	short		ceaf_pi7 - tbl_ceaf_pi

ceaf_pi0:
	addi.l		&0xc,EXC_DREGS+0x8(%a6)
	rts
ceaf_pi1:
	addi.l		&0xc,EXC_DREGS+0xc(%a6)
	rts
ceaf_pi2:
	add.l		&0xc,%a2
	rts
ceaf_pi3:
	add.l		&0xc,%a3
	rts
ceaf_pi4:
	add.l		&0xc,%a4
	rts
ceaf_pi5:
	add.l		&0xc,%a5
	rts
ceaf_pi6:
	addi.l		&0xc,EXC_A6(%a6)
	rts
ceaf_pi7:
	mov.b		&mia7_flg,SPCOND_FLG(%a6)
	addi.l		&0xc,EXC_A7(%a6)
	rts

# -(An) : extended and packed fmove out
#	: stacked <ea> = actual <ea> + 8
#	: "An" not updated
ceaf_pd:
	mov.w		(tbl_ceaf_pd.b,%pc,%d1.w*2),%d1
	mov.l		EXC_EA(%a6),%a0
	sub.l		&0x8,%a0
	sub.l		&0x8,EXC_EA(%a6)
	jmp		(tbl_ceaf_pd.b,%pc,%d1.w*1)

	swbeg		&0x8
tbl_ceaf_pd:
	short		ceaf_pd0 - tbl_ceaf_pd
	short		ceaf_pd1 - tbl_ceaf_pd
	short		ceaf_pd2 - tbl_ceaf_pd
	short		ceaf_pd3 - tbl_ceaf_pd
	short		ceaf_pd4 - tbl_ceaf_pd
	short		ceaf_pd5 - tbl_ceaf_pd
	short		ceaf_pd6 - tbl_ceaf_pd
	short		ceaf_pd7 - tbl_ceaf_pd

ceaf_pd0:
	mov.l		%a0,EXC_DREGS+0x8(%a6)
	rts
ceaf_pd1:
	mov.l		%a0,EXC_DREGS+0xc(%a6)
	rts
ceaf_pd2:
	mov.l		%a0,%a2
	rts
ceaf_pd3:
	mov.l		%a0,%a3
	rts
ceaf_pd4:
	mov.l		%a0,%a4
	rts
ceaf_pd5:
	mov.l		%a0,%a5
	rts
ceaf_pd6:
	mov.l		%a0,EXC_A6(%a6)
	rts
ceaf_pd7:
	mov.l		%a0,EXC_A7(%a6)
	mov.b		&mda7_flg,SPCOND_FLG(%a6)
	rts

#
# This table holds the offsets of the emulation routines for each individual
# math operation relative to the address of this table. Included are
# routines like fadd/fmul/fabs. The transcendentals ARE NOT. This is because
# this table is for the version if the 060FPSP without transcendentals.
# The location within the table is determined by the extension bits of the
# operation longword.
#

	swbeg		&109
tbl_unsupp:
	long		fin		- tbl_unsupp	# 00: fmove
	long		fint		- tbl_unsupp	# 01: fint
	long		tbl_unsupp	- tbl_unsupp	# 02: fsinh
	long		fintrz		- tbl_unsupp	# 03: fintrz
	long		fsqrt		- tbl_unsupp	# 04: fsqrt
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp	# 06: flognp1
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp	# 08: fetoxm1
	long		tbl_unsupp	- tbl_unsupp	# 09: ftanh
	long		tbl_unsupp	- tbl_unsupp	# 0a: fatan
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp	# 0c: fasin
	long		tbl_unsupp	- tbl_unsupp	# 0d: fatanh
	long		tbl_unsupp	- tbl_unsupp	# 0e: fsin
	long		tbl_unsupp	- tbl_unsupp	# 0f: ftan
	long		tbl_unsupp	- tbl_unsupp	# 10: fetox
	long		tbl_unsupp	- tbl_unsupp	# 11: ftwotox
	long		tbl_unsupp	- tbl_unsupp	# 12: ftentox
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp	# 14: flogn
	long		tbl_unsupp	- tbl_unsupp	# 15: flog10
	long		tbl_unsupp	- tbl_unsupp	# 16: flog2
	long		tbl_unsupp	- tbl_unsupp
	long		fabs		- tbl_unsupp	# 18: fabs
	long		tbl_unsupp	- tbl_unsupp	# 19: fcosh
	long		fneg		- tbl_unsupp	# 1a: fneg
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp	# 1c: facos
	long		tbl_unsupp	- tbl_unsupp	# 1d: fcos
	long		tbl_unsupp	- tbl_unsupp	# 1e: fgetexp
	long		tbl_unsupp	- tbl_unsupp	# 1f: fgetman
	long		fdiv		- tbl_unsupp	# 20: fdiv
	long		tbl_unsupp	- tbl_unsupp	# 21: fmod
	long		fadd		- tbl_unsupp	# 22: fadd
	long		fmul		- tbl_unsupp	# 23: fmul
	long		fsgldiv		- tbl_unsupp	# 24: fsgldiv
	long		tbl_unsupp	- tbl_unsupp	# 25: frem
	long		tbl_unsupp	- tbl_unsupp	# 26: fscale
	long		fsglmul		- tbl_unsupp	# 27: fsglmul
	long		fsub		- tbl_unsupp	# 28: fsub
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp	# 30: fsincos
	long		tbl_unsupp	- tbl_unsupp	# 31: fsincos
	long		tbl_unsupp	- tbl_unsupp	# 32: fsincos
	long		tbl_unsupp	- tbl_unsupp	# 33: fsincos
	long		tbl_unsupp	- tbl_unsupp	# 34: fsincos
	long		tbl_unsupp	- tbl_unsupp	# 35: fsincos
	long		tbl_unsupp	- tbl_unsupp	# 36: fsincos
	long		tbl_unsupp	- tbl_unsupp	# 37: fsincos
	long		fcmp		- tbl_unsupp	# 38: fcmp
	long		tbl_unsupp	- tbl_unsupp
	long		ftst		- tbl_unsupp	# 3a: ftst
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		fsin		- tbl_unsupp	# 40: fsmove
	long		fssqrt		- tbl_unsupp	# 41: fssqrt
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		fdin		- tbl_unsupp	# 44: fdmove
	long		fdsqrt		- tbl_unsupp	# 45: fdsqrt
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		fsabs		- tbl_unsupp	# 58: fsabs
	long		tbl_unsupp	- tbl_unsupp
	long		fsneg		- tbl_unsupp	# 5a: fsneg
	long		tbl_unsupp	- tbl_unsupp
	long		fdabs		- tbl_unsupp	# 5c: fdabs
	long		tbl_unsupp	- tbl_unsupp
	long		fdneg		- tbl_unsupp	# 5e: fdneg
	long		tbl_unsupp	- tbl_unsupp
	long		fsdiv		- tbl_unsupp	# 60: fsdiv
	long		tbl_unsupp	- tbl_unsupp
	long		fsadd		- tbl_unsupp	# 62: fsadd
	long		fsmul		- tbl_unsupp	# 63: fsmul
	long		fddiv		- tbl_unsupp	# 64: fddiv
	long		tbl_unsupp	- tbl_unsupp
	long		fdadd		- tbl_unsupp	# 66: fdadd
	long		fdmul		- tbl_unsupp	# 67: fdmul
	long		fssub		- tbl_unsupp	# 68: fssub
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		tbl_unsupp	- tbl_unsupp
	long		fdsub		- tbl_unsupp	# 6c: fdsub

#################################################
# Add this here so non-fp modules can compile.
# (smovcr is called from fpsp_inex.)
	global		smovcr
smovcr:
	bra.b		smovcr

#########################################################################
# XDEF ****************************************************************	#
#	fmovm_dynamic(): emulate "fmovm" dynamic instruction		#
#									#
# XREF ****************************************************************	#
#	fetch_dreg() - fetch data register				#
#	{i,d,}mem_read() - fetch data from memory			#
#	_mem_write() - write data to memory				#
#	iea_iacc() - instruction memory access error occurred		#
#	iea_dacc() - data memory access error occurred			#
#	restore() - restore An index regs if access error occurred	#
#									#
# INPUT ***************************************************************	#
#	None								#
#									#
# OUTPUT **************************************************************	#
#	If instr is "fmovm Dn,-(A7)" from supervisor mode,		#
#		d0 = size of dump					#
#		d1 = Dn							#
#	Else if instruction access error,				#
#		d0 = FSLW						#
#	Else if data access error,					#
#		d0 = FSLW						#
#		a0 = address of fault					#
#	Else								#
#		none.							#
#									#
# ALGORITHM ***********************************************************	#
#	The effective address must be calculated since this is entered	#
# from an "Unimplemented Effective Address" exception handler. So, we	#
# have our own fcalc_ea() routine here. If an access error is flagged	#
# by a _{i,d,}mem_read() call, we must exit through the special		#
# handler.								#
#	The data register is determined and its value loaded to get the	#
# string of FP registers affected. This value is used as an index into	#
# a lookup table such that we can determine the number of bytes		#
# involved.								#
#	If the instruction is "fmovm.x <ea>,Dn", a _mem_read() is used	#
# to read in all FP values. Again, _mem_read() may fail and require a	#
# special exit.								#
#	If the instruction is "fmovm.x DN,<ea>", a _mem_write() is used	#
# to write all FP values. _mem_write() may also fail.			#
#	If the instruction is "fmovm.x DN,-(a7)" from supervisor mode,	#
# then we return the size of the dump and the string to the caller	#
# so that the move can occur outside of this routine. This special	#
# case is required so that moves to the system stack are handled	#
# correctly.								#
#									#
# DYNAMIC:								#
#	fmovm.x	dn, <ea>						#
#	fmovm.x	<ea>, dn						#
#									#
#	      <WORD 1>		      <WORD2>				#
#	1111 0010 00 |<ea>|	11@& 1000 0$$$ 0000			#
#									#
#	& = (0): predecrement addressing mode				#
#	    (1): postincrement or control addressing mode		#
#	@ = (0): move listed regs from memory to the FPU		#
#	    (1): move listed regs from the FPU to memory		#
#	$$$    : index of data register holding reg select mask		#
#									#
# NOTES:								#
#	If the data register holds a zero, then the			#
#	instruction is a nop.						#
#									#
#########################################################################

	global		fmovm_dynamic
fmovm_dynamic:

# extract the data register in which the bit string resides...
	mov.b		1+EXC_EXTWORD(%a6),%d1	# fetch extword
	andi.w		&0x70,%d1		# extract reg bits
	lsr.b		&0x4,%d1		# shift into lo bits

# fetch the bit string into d0...
	bsr.l		fetch_dreg		# fetch reg string

	andi.l		&0x000000ff,%d0		# keep only lo byte

	mov.l		%d0,-(%sp)		# save strg
	mov.b		(tbl_fmovm_size.w,%pc,%d0),%d0
	mov.l		%d0,-(%sp)		# save size
	bsr.l		fmovm_calc_ea		# calculate <ea>
	mov.l		(%sp)+,%d0		# restore size
	mov.l		(%sp)+,%d1		# restore strg

# if the bit string is a zero, then the operation is a no-op
# but, make sure that we've calculated ea and advanced the opword pointer
	beq.w		fmovm_data_done

# separate move ins from move outs...
	btst		&0x5,EXC_EXTWORD(%a6)	# is it a move in or out?
	beq.w		fmovm_data_in		# it's a move out

#############
# MOVE OUT: #
#############
fmovm_data_out:
	btst		&0x4,EXC_EXTWORD(%a6)	# control or predecrement?
	bne.w		fmovm_out_ctrl		# control

############################
fmovm_out_predec:
# for predecrement mode, the bit string is the opposite of both control
# operations and postincrement mode. (bit7 = FP7 ... bit0 = FP0)
# here, we convert it to be just like the others...
	mov.b		(tbl_fmovm_convert.w,%pc,%d1.w*1),%d1

	btst		&0x5,EXC_SR(%a6)	# user or supervisor mode?
	beq.b		fmovm_out_ctrl		# user

fmovm_out_predec_s:
	cmpi.b		SPCOND_FLG(%a6),&mda7_flg # is <ea> mode -(a7)?
	bne.b		fmovm_out_ctrl

# the operation was unfortunately an: fmovm.x dn,-(sp)
# called from supervisor mode.
# we're also passing "size" and "strg" back to the calling routine
	rts

############################
fmovm_out_ctrl:
	mov.l		%a0,%a1			# move <ea> to a1

	sub.l		%d0,%sp			# subtract size of dump
	lea		(%sp),%a0

	tst.b		%d1			# should FP0 be moved?
	bpl.b		fmovm_out_ctrl_fp1	# no

	mov.l		0x0+EXC_FP0(%a6),(%a0)+	# yes
	mov.l		0x4+EXC_FP0(%a6),(%a0)+
	mov.l		0x8+EXC_FP0(%a6),(%a0)+

fmovm_out_ctrl_fp1:
	lsl.b		&0x1,%d1		# should FP1 be moved?
	bpl.b		fmovm_out_ctrl_fp2	# no

	mov.l		0x0+EXC_FP1(%a6),(%a0)+	# yes
	mov.l		0x4+EXC_FP1(%a6),(%a0)+
	mov.l		0x8+EXC_FP1(%a6),(%a0)+

fmovm_out_ctrl_fp2:
	lsl.b		&0x1,%d1		# should FP2 be moved?
	bpl.b		fmovm_out_ctrl_fp3	# no

	fmovm.x		&0x20,(%a0)		# yes
	add.l		&0xc,%a0

fmovm_out_ctrl_fp3:
	lsl.b		&0x1,%d1		# should FP3 be moved?
	bpl.b		fmovm_out_ctrl_fp4	# no

	fmovm.x		&0x10,(%a0)		# yes
	add.l		&0xc,%a0

fmovm_out_ctrl_fp4:
	lsl.b		&0x1,%d1		# should FP4 be moved?
	bpl.b		fmovm_out_ctrl_fp5	# no

	fmovm.x		&0x08,(%a0)		# yes
	add.l		&0xc,%a0

fmovm_out_ctrl_fp5:
	lsl.b		&0x1,%d1		# should FP5 be moved?
	bpl.b		fmovm_out_ctrl_fp6	# no

	fmovm.x		&0x04,(%a0)		# yes
	add.l		&0xc,%a0

fmovm_out_ctrl_fp6:
	lsl.b		&0x1,%d1		# should FP6 be moved?
	bpl.b		fmovm_out_ctrl_fp7	# no

	fmovm.x		&0x02,(%a0)		# yes
	add.l		&0xc,%a0

fmovm_out_ctrl_fp7:
	lsl.b		&0x1,%d1		# should FP7 be moved?
	bpl.b		fmovm_out_ctrl_done	# no

	fmovm.x		&0x01,(%a0)		# yes
	add.l		&0xc,%a0

fmovm_out_ctrl_done:
	mov.l		%a1,L_SCR1(%a6)

	lea		(%sp),%a0		# pass: supervisor src
	mov.l		%d0,-(%sp)		# save size
	bsr.l		_dmem_write		# copy data to user mem

	mov.l		(%sp)+,%d0
	add.l		%d0,%sp			# clear fpreg data from stack

	tst.l		%d1			# did dstore err?
	bne.w		fmovm_out_err		# yes

	rts

############
# MOVE IN: #
############
fmovm_data_in:
	mov.l		%a0,L_SCR1(%a6)

	sub.l		%d0,%sp			# make room for fpregs
	lea		(%sp),%a1

	mov.l		%d1,-(%sp)		# save bit string for later
	mov.l		%d0,-(%sp)		# save # of bytes

	bsr.l		_dmem_read		# copy data from user mem

	mov.l		(%sp)+,%d0		# retrieve # of bytes

	tst.l		%d1			# did dfetch fail?
	bne.w		fmovm_in_err		# yes

	mov.l		(%sp)+,%d1		# load bit string

	lea		(%sp),%a0		# addr of stack

	tst.b		%d1			# should FP0 be moved?
	bpl.b		fmovm_data_in_fp1	# no

	mov.l		(%a0)+,0x0+EXC_FP0(%a6)	# yes
	mov.l		(%a0)+,0x4+EXC_FP0(%a6)
	mov.l		(%a0)+,0x8+EXC_FP0(%a6)

fmovm_data_in_fp1:
	lsl.b		&0x1,%d1		# should FP1 be moved?
	bpl.b		fmovm_data_in_fp2	# no

	mov.l		(%a0)+,0x0+EXC_FP1(%a6)	# yes
	mov.l		(%a0)+,0x4+EXC_FP1(%a6)
	mov.l		(%a0)+,0x8+EXC_FP1(%a6)

fmovm_data_in_fp2:
	lsl.b		&0x1,%d1		# should FP2 be moved?
	bpl.b		fmovm_data_in_fp3	# no

	fmovm.x		(%a0)+,&0x20		# yes

fmovm_data_in_fp3:
	lsl.b		&0x1,%d1		# should FP3 be moved?
	bpl.b		fmovm_data_in_fp4	# no

	fmovm.x		(%a0)+,&0x10		# yes

fmovm_data_in_fp4:
	lsl.b		&0x1,%d1		# should FP4 be moved?
	bpl.b		fmovm_data_in_fp5	# no

	fmovm.x		(%a0)+,&0x08		# yes

fmovm_data_in_fp5:
	lsl.b		&0x1,%d1		# should FP5 be moved?
	bpl.b		fmovm_data_in_fp6	# no

	fmovm.x		(%a0)+,&0x04		# yes

fmovm_data_in_fp6:
	lsl.b		&0x1,%d1		# should FP6 be moved?
	bpl.b		fmovm_data_in_fp7	# no

	fmovm.x		(%a0)+,&0x02		# yes

fmovm_data_in_fp7:
	lsl.b		&0x1,%d1		# should FP7 be moved?
	bpl.b		fmovm_data_in_done	# no

	fmovm.x		(%a0)+,&0x01		# yes

fmovm_data_in_done:
	add.l		%d0,%sp			# remove fpregs from stack
	rts

#####################################

fmovm_data_done:
	rts

##############################################################################

#
# table indexed by the operation's bit string that gives the number
# of bytes that will be moved.
#
# number of bytes = (# of 1's in bit string) * 12(bytes/fpreg)
#
tbl_fmovm_size:
	byte	0x00,0x0c,0x0c,0x18,0x0c,0x18,0x18,0x24
	byte	0x0c,0x18,0x18,0x24,0x18,0x24,0x24,0x30
	byte	0x0c,0x18,0x18,0x24,0x18,0x24,0x24,0x30
	byte	0x18,0x24,0x24,0x30,0x24,0x30,0x30,0x3c
	byte	0x0c,0x18,0x18,0x24,0x18,0x24,0x24,0x30
	byte	0x18,0x24,0x24,0x30,0x24,0x30,0x30,0x3c
	byte	0x18,0x24,0x24,0x30,0x24,0x30,0x30,0x3c
	byte	0x24,0x30,0x30,0x3c,0x30,0x3c,0x3c,0x48
	byte	0x0c,0x18,0x18,0x24,0x18,0x24,0x24,0x30
	byte	0x18,0x24,0x24,0x30,0x24,0x30,0x30,0x3c
	byte	0x18,0x24,0x24,0x30,0x24,0x30,0x30,0x3c
	byte	0x24,0x30,0x30,0x3c,0x30,0x3c,0x3c,0x48
	byte	0x18,0x24,0x24,0x30,0x24,0x30,0x30,0x3c
	byte	0x24,0x30,0x30,0x3c,0x30,0x3c,0x3c,0x48
	byte	0x24,0x30,0x30,0x3c,0x30,0x3c,0x3c,0x48
	byte	0x30,0x3c,0x3c,0x48,0x3c,0x48,0x48,0x54
	byte	0x0c,0x18,0x18,0x24,0x18,0x24,0x24,0x30
	byte	0x18,0x24,0x24,0x30,0x24,0x30,0x30,0x3c
	byte	0x18,0x24,0x24,0x30,0x24,0x30,0x30,0x3c
	byte	0x24,0x30,0x30,0x3c,0x30,0x3c,0x3c,0x48
	byte	0x18,0x24,0x24,0x30,0x24,0x30,0x30,0x3c
	byte	0x24,0x30,0x30,0x3c,0x30,0x3c,0x3c,0x48
	byte	0x24,0x30,0x30,0x3c,0x30,0x3c,0x3c,0x48
	byte	0x30,0x3c,0x3c,0x48,0x3c,0x48,0x48,0x54
	byte	0x18,0x24,0x24,0x30,0x24,0x30,0x30,0x3c
	byte	0x24,0x30,0x30,0x3c,0x30,0x3c,0x3c,0x48
	byte	0x24,0x30,0x30,0x3c,0x30,0x3c,0x3c,0x48
	byte	0x30,0x3c,0x3c,0x48,0x3c,0x48,0x48,0x54
	byte	0x24,0x30,0x30,0x3c,0x30,0x3c,0x3c,0x48
	byte	0x30,0x3c,0x3c,0x48,0x3c,0x48,0x48,0x54
	byte	0x30,0x3c,0x3c,0x48,0x3c,0x48,0x48,0x54
	byte	0x3c,0x48,0x48,0x54,0x48,0x54,0x54,0x60

#
# table to convert a pre-decrement bit string into a post-increment
# or control bit string.
# ex:	0x00	==>	0x00
#	0x01	==>	0x80
#	0x02	==>	0x40
#		.
#		.
#	0xfd	==>	0xbf
#	0xfe	==>	0x7f
#	0xff	==>	0xff
#
tbl_fmovm_convert:
	byte	0x00,0x80,0x40,0xc0,0x20,0xa0,0x60,0xe0
	byte	0x10,0x90,0x50,0xd0,0x30,0xb0,0x70,0xf0
	byte	0x08,0x88,0x48,0xc8,0x28,0xa8,0x68,0xe8
	byte	0x18,0x98,0x58,0xd8,0x38,0xb8,0x78,0xf8
	byte	0x04,0x84,0x44,0xc4,0x24,0xa4,0x64,0xe4
	byte	0x14,0x94,0x54,0xd4,0x34,0xb4,0x74,0xf4
	byte	0x0c,0x8c,0x4c,0xcc,0x2c,0xac,0x6c,0xec
	byte	0x1c,0x9c,0x5c,0xdc,0x3c,0xbc,0x7c,0xfc
	byte	0x02,0x82,0x42,0xc2,0x22,0xa2,0x62,0xe2
	byte	0x12,0x92,0x52,0xd2,0x32,0xb2,0x72,0xf2
	byte	0x0a,0x8a,0x4a,0xca,0x2a,0xaa,0x6a,0xea
	byte	0x1a,0x9a,0x5a,0xda,0x3a,0xba,0x7a,0xfa
	byte	0x06,0x86,0x46,0xc6,0x26,0xa6,0x66,0xe6
	byte	0x16,0x96,0x56,0xd6,0x36,0xb6,0x76,0xf6
	byte	0x0e,0x8e,0x4e,0xce,0x2e,0xae,0x6e,0xee
	byte	0x1e,0x9e,0x5e,0xde,0x3e,0xbe,0x7e,0xfe
	byte	0x01,0x81,0x41,0xc1,0x21,0xa1,0x61,0xe1
	byte	0x11,0x91,0x51,0xd1,0x31,0xb1,0x71,0xf1
	byte	0x09,0x89,0x49,0xc9,0x29,0xa9,0x69,0xe9
	byte	0x19,0x99,0x59,0xd9,0x39,0xb9,0x79,0xf9
	byte	0x05,0x85,0x45,0xc5,0x25,0xa5,0x65,0xe5
	byte	0x15,0x95,0x55,0xd5,0x35,0xb5,0x75,0xf5
	byte	0x0d,0x8d,0x4d,0xcd,0x2d,0xad,0x6d,0xed
	byte	0x1d,0x9d,0x5d,0xdd,0x3d,0xbd,0x7d,0xfd
	byte	0x03,0x83,0x43,0xc3,0x23,0xa3,0x63,0xe3
	byte	0x13,0x93,0x53,0xd3,0x33,0xb3,0x73,0xf3
	byte	0x0b,0x8b,0x4b,0xcb,0x2b,0xab,0x6b,0xeb
	byte	0x1b,0x9b,0x5b,0xdb,0x3b,0xbb,0x7b,0xfb
	byte	0x07,0x87,0x47,0xc7,0x27,0xa7,0x67,0xe7
	byte	0x17,0x97,0x57,0xd7,0x37,0xb7,0x77,0xf7
	byte	0x0f,0x8f,0x4f,0xcf,0x2f,0xaf,0x6f,0xef
	byte	0x1f,0x9f,0x5f,0xdf,0x3f,0xbf,0x7f,0xff

	global		fmovm_calc_ea
###############################################
# _fmovm_calc_ea: calculate effective address #
###############################################
fmovm_calc_ea:
	mov.l		%d0,%a0			# move # bytes to a0

# currently, MODE and REG are taken from the EXC_OPWORD. this could be
# easily changed if they were inputs passed in registers.
	mov.w		EXC_OPWORD(%a6),%d0	# fetch opcode word
	mov.w		%d0,%d1			# make a copy

	andi.w		&0x3f,%d0		# extract mode field
	andi.l		&0x7,%d1		# extract reg  field

# jump to the corresponding function for each {MODE,REG} pair.
	mov.w		(tbl_fea_mode.b,%pc,%d0.w*2),%d0 # fetch jmp distance
	jmp		(tbl_fea_mode.b,%pc,%d0.w*1) # jmp to correct ea mode

	swbeg		&64
tbl_fea_mode:
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode

	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode

	short		faddr_ind_a0	-	tbl_fea_mode
	short		faddr_ind_a1	-	tbl_fea_mode
	short		faddr_ind_a2	-	tbl_fea_mode
	short		faddr_ind_a3	-	tbl_fea_mode
	short		faddr_ind_a4	-	tbl_fea_mode
	short		faddr_ind_a5	-	tbl_fea_mode
	short		faddr_ind_a6	-	tbl_fea_mode
	short		faddr_ind_a7	-	tbl_fea_mode

	short		faddr_ind_p_a0	-	tbl_fea_mode
	short		faddr_ind_p_a1	-	tbl_fea_mode
	short		faddr_ind_p_a2	-	tbl_fea_mode
	short		faddr_ind_p_a3	-	tbl_fea_mode
	short		faddr_ind_p_a4	-	tbl_fea_mode
	short		faddr_ind_p_a5	-	tbl_fea_mode
	short		faddr_ind_p_a6	-	tbl_fea_mode
	short		faddr_ind_p_a7	-	tbl_fea_mode

	short		faddr_ind_m_a0	-	tbl_fea_mode
	short		faddr_ind_m_a1	-	tbl_fea_mode
	short		faddr_ind_m_a2	-	tbl_fea_mode
	short		faddr_ind_m_a3	-	tbl_fea_mode
	short		faddr_ind_m_a4	-	tbl_fea_mode
	short		faddr_ind_m_a5	-	tbl_fea_mode
	short		faddr_ind_m_a6	-	tbl_fea_mode
	short		faddr_ind_m_a7	-	tbl_fea_mode

	short		faddr_ind_disp_a0	-	tbl_fea_mode
	short		faddr_ind_disp_a1	-	tbl_fea_mode
	short		faddr_ind_disp_a2	-	tbl_fea_mode
	short		faddr_ind_disp_a3	-	tbl_fea_mode
	short		faddr_ind_disp_a4	-	tbl_fea_mode
	short		faddr_ind_disp_a5	-	tbl_fea_mode
	short		faddr_ind_disp_a6	-	tbl_fea_mode
	short		faddr_ind_disp_a7	-	tbl_fea_mode

	short		faddr_ind_ext	-	tbl_fea_mode
	short		faddr_ind_ext	-	tbl_fea_mode
	short		faddr_ind_ext	-	tbl_fea_mode
	short		faddr_ind_ext	-	tbl_fea_mode
	short		faddr_ind_ext	-	tbl_fea_mode
	short		faddr_ind_ext	-	tbl_fea_mode
	short		faddr_ind_ext	-	tbl_fea_mode
	short		faddr_ind_ext	-	tbl_fea_mode

	short		fabs_short	-	tbl_fea_mode
	short		fabs_long	-	tbl_fea_mode
	short		fpc_ind		-	tbl_fea_mode
	short		fpc_ind_ext	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode
	short		tbl_fea_mode	-	tbl_fea_mode

###################################
# Address register indirect: (An) #
###################################
faddr_ind_a0:
	mov.l		EXC_DREGS+0x8(%a6),%a0	# Get current a0
	rts

faddr_ind_a1:
	mov.l		EXC_DREGS+0xc(%a6),%a0	# Get current a1
	rts

faddr_ind_a2:
	mov.l		%a2,%a0			# Get current a2
	rts

faddr_ind_a3:
	mov.l		%a3,%a0			# Get current a3
	rts

faddr_ind_a4:
	mov.l		%a4,%a0			# Get current a4
	rts

faddr_ind_a5:
	mov.l		%a5,%a0			# Get current a5
	rts

faddr_ind_a6:
	mov.l		(%a6),%a0		# Get current a6
	rts

faddr_ind_a7:
	mov.l		EXC_A7(%a6),%a0		# Get current a7
	rts

#####################################################
# Address register indirect w/ postincrement: (An)+ #
#####################################################
faddr_ind_p_a0:
	mov.l		EXC_DREGS+0x8(%a6),%d0	# Get current a0
	mov.l		%d0,%d1
	add.l		%a0,%d1			# Increment
	mov.l		%d1,EXC_DREGS+0x8(%a6)	# Save incr value
	mov.l		%d0,%a0
	rts

faddr_ind_p_a1:
	mov.l		EXC_DREGS+0xc(%a6),%d0	# Get current a1
	mov.l		%d0,%d1
	add.l		%a0,%d1			# Increment
	mov.l		%d1,EXC_DREGS+0xc(%a6)	# Save incr value
	mov.l		%d0,%a0
	rts

faddr_ind_p_a2:
	mov.l		%a2,%d0			# Get current a2
	mov.l		%d0,%d1
	add.l		%a0,%d1			# Increment
	mov.l		%d1,%a2			# Save incr value
	mov.l		%d0,%a0
	rts

faddr_ind_p_a3:
	mov.l		%a3,%d0			# Get current a3
	mov.l		%d0,%d1
	add.l		%a0,%d1			# Increment
	mov.l		%d1,%a3			# Save incr value
	mov.l		%d0,%a0
	rts

faddr_ind_p_a4:
	mov.l		%a4,%d0			# Get current a4
	mov.l		%d0,%d1
	add.l		%a0,%d1			# Increment
	mov.l		%d1,%a4			# Save incr value
	mov.l		%d0,%a0
	rts

faddr_ind_p_a5:
	mov.l		%a5,%d0			# Get current a5
	mov.l		%d0,%d1
	add.l		%a0,%d1			# Increment
	mov.l		%d1,%a5			# Save incr value
	mov.l		%d0,%a0
	rts

faddr_ind_p_a6:
	mov.l		(%a6),%d0		# Get current a6
	mov.l		%d0,%d1
	add.l		%a0,%d1			# Increment
	mov.l		%d1,(%a6)		# Save incr value
	mov.l		%d0,%a0
	rts

faddr_ind_p_a7:
	mov.b		&mia7_flg,SPCOND_FLG(%a6) # set "special case" flag

	mov.l		EXC_A7(%a6),%d0		# Get current a7
	mov.l		%d0,%d1
	add.l		%a0,%d1			# Increment
	mov.l		%d1,EXC_A7(%a6)		# Save incr value
	mov.l		%d0,%a0
	rts

####################################################
# Address register indirect w/ predecrement: -(An) #
####################################################
faddr_ind_m_a0:
	mov.l		EXC_DREGS+0x8(%a6),%d0	# Get current a0
	sub.l		%a0,%d0			# Decrement
	mov.l		%d0,EXC_DREGS+0x8(%a6)	# Save decr value
	mov.l		%d0,%a0
	rts

faddr_ind_m_a1:
	mov.l		EXC_DREGS+0xc(%a6),%d0	# Get current a1
	sub.l		%a0,%d0			# Decrement
	mov.l		%d0,EXC_DREGS+0xc(%a6)	# Save decr value
	mov.l		%d0,%a0
	rts

faddr_ind_m_a2:
	mov.l		%a2,%d0			# Get current a2
	sub.l		%a0,%d0			# Decrement
	mov.l		%d0,%a2			# Save decr value
	mov.l		%d0,%a0
	rts

faddr_ind_m_a3:
	mov.l		%a3,%d0			# Get current a3
	sub.l		%a0,%d0			# Decrement
	mov.l		%d0,%a3			# Save decr value
	mov.l		%d0,%a0
	rts

faddr_ind_m_a4:
	mov.l		%a4,%d0			# Get current a4
	sub.l		%a0,%d0			# Decrement
	mov.l		%d0,%a4			# Save decr value
	mov.l		%d0,%a0
	rts

faddr_ind_m_a5:
	mov.l		%a5,%d0			# Get current a5
	sub.l		%a0,%d0			# Decrement
	mov.l		%d0,%a5			# Save decr value
	mov.l		%d0,%a0
	rts

faddr_ind_m_a6:
	mov.l		(%a6),%d0		# Get current a6
	sub.l		%a0,%d0			# Decrement
	mov.l		%d0,(%a6)		# Save decr value
	mov.l		%d0,%a0
	rts

faddr_ind_m_a7:
	mov.b		&mda7_flg,SPCOND_FLG(%a6) # set "special case" flag

	mov.l		EXC_A7(%a6),%d0		# Get current a7
	sub.l		%a0,%d0			# Decrement
	mov.l		%d0,EXC_A7(%a6)		# Save decr value
	mov.l		%d0,%a0
	rts

########################################################
# Address register indirect w/ displacement: (d16, An) #
########################################################
faddr_ind_disp_a0:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.w		%d0,%a0			# sign extend displacement

	add.l		EXC_DREGS+0x8(%a6),%a0	# a0 + d16
	rts

faddr_ind_disp_a1:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.w		%d0,%a0			# sign extend displacement

	add.l		EXC_DREGS+0xc(%a6),%a0	# a1 + d16
	rts

faddr_ind_disp_a2:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.w		%d0,%a0			# sign extend displacement

	add.l		%a2,%a0			# a2 + d16
	rts

faddr_ind_disp_a3:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.w		%d0,%a0			# sign extend displacement

	add.l		%a3,%a0			# a3 + d16
	rts

faddr_ind_disp_a4:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.w		%d0,%a0			# sign extend displacement

	add.l		%a4,%a0			# a4 + d16
	rts

faddr_ind_disp_a5:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.w		%d0,%a0			# sign extend displacement

	add.l		%a5,%a0			# a5 + d16
	rts

faddr_ind_disp_a6:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.w		%d0,%a0			# sign extend displacement

	add.l		(%a6),%a0		# a6 + d16
	rts

faddr_ind_disp_a7:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.w		%d0,%a0			# sign extend displacement

	add.l		EXC_A7(%a6),%a0		# a7 + d16
	rts

########################################################################
# Address register indirect w/ index(8-bit displacement): (d8, An, Xn) #
#    "       "         "    w/   "  (base displacement): (bd, An, Xn)  #
# Memory indirect postindexed: ([bd, An], Xn, od)		       #
# Memory indirect preindexed: ([bd, An, Xn], od)		       #
########################################################################
faddr_ind_ext:
	addq.l		&0x8,%d1
	bsr.l		fetch_dreg		# fetch base areg
	mov.l		%d0,-(%sp)

	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word		# fetch extword in d0

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.l		(%sp)+,%a0

	btst		&0x8,%d0
	bne.w		fcalc_mem_ind

	mov.l		%d0,L_SCR1(%a6)		# hold opword

	mov.l		%d0,%d1
	rol.w		&0x4,%d1
	andi.w		&0xf,%d1		# extract index regno

# count on fetch_dreg() not to alter a0...
	bsr.l		fetch_dreg		# fetch index

	mov.l		%d2,-(%sp)		# save d2
	mov.l		L_SCR1(%a6),%d2		# fetch opword

	btst		&0xb,%d2		# is it word or long?
	bne.b		faii8_long
	ext.l		%d0			# sign extend word index
faii8_long:
	mov.l		%d2,%d1
	rol.w		&0x7,%d1
	andi.l		&0x3,%d1		# extract scale value

	lsl.l		%d1,%d0			# shift index by scale

	extb.l		%d2			# sign extend displacement
	add.l		%d2,%d0			# index + disp
	add.l		%d0,%a0			# An + (index + disp)

	mov.l		(%sp)+,%d2		# restore old d2
	rts

###########################
# Absolute short: (XXX).W #
###########################
fabs_short:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word		# fetch short address

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.w		%d0,%a0			# return <ea> in a0
	rts

##########################
# Absolute long: (XXX).L #
##########################
fabs_long:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch long address

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.l		%d0,%a0			# return <ea> in a0
	rts

#######################################################
# Program counter indirect w/ displacement: (d16, PC) #
#######################################################
fpc_ind:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word		# fetch word displacement

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.w		%d0,%a0			# sign extend displacement

	add.l		EXC_EXTWPTR(%a6),%a0	# pc + d16

# _imem_read_word() increased the extwptr by 2. need to adjust here.
	subq.l		&0x2,%a0		# adjust <ea>
	rts

##########################################################
# PC indirect w/ index(8-bit displacement): (d8, PC, An) #
# "     "     w/   "  (base displacement): (bd, PC, An)  #
# PC memory indirect postindexed: ([bd, PC], Xn, od)     #
# PC memory indirect preindexed: ([bd, PC, Xn], od)      #
##########################################################
fpc_ind_ext:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word		# fetch ext word

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.l		EXC_EXTWPTR(%a6),%a0	# put base in a0
	subq.l		&0x2,%a0		# adjust base

	btst		&0x8,%d0		# is disp only 8 bits?
	bne.w		fcalc_mem_ind		# calc memory indirect

	mov.l		%d0,L_SCR1(%a6)		# store opword

	mov.l		%d0,%d1			# make extword copy
	rol.w		&0x4,%d1		# rotate reg num into place
	andi.w		&0xf,%d1		# extract register number

# count on fetch_dreg() not to alter a0...
	bsr.l		fetch_dreg		# fetch index

	mov.l		%d2,-(%sp)		# save d2
	mov.l		L_SCR1(%a6),%d2		# fetch opword

	btst		&0xb,%d2		# is index word or long?
	bne.b		fpii8_long		# long
	ext.l		%d0			# sign extend word index
fpii8_long:
	mov.l		%d2,%d1
	rol.w		&0x7,%d1		# rotate scale value into place
	andi.l		&0x3,%d1		# extract scale value

	lsl.l		%d1,%d0			# shift index by scale

	extb.l		%d2			# sign extend displacement
	add.l		%d2,%d0			# disp + index
	add.l		%d0,%a0			# An + (index + disp)

	mov.l		(%sp)+,%d2		# restore temp register
	rts

# d2 = index
# d3 = base
# d4 = od
# d5 = extword
fcalc_mem_ind:
	btst		&0x6,%d0		# is the index suppressed?
	beq.b		fcalc_index

	movm.l		&0x3c00,-(%sp)		# save d2-d5

	mov.l		%d0,%d5			# put extword in d5
	mov.l		%a0,%d3			# put base in d3

	clr.l		%d2			# yes, so index = 0
	bra.b		fbase_supp_ck

# index:
fcalc_index:
	mov.l		%d0,L_SCR1(%a6)		# save d0 (opword)
	bfextu		%d0{&16:&4},%d1		# fetch dreg index
	bsr.l		fetch_dreg

	movm.l		&0x3c00,-(%sp)		# save d2-d5
	mov.l		%d0,%d2			# put index in d2
	mov.l		L_SCR1(%a6),%d5
	mov.l		%a0,%d3

	btst		&0xb,%d5		# is index word or long?
	bne.b		fno_ext
	ext.l		%d2

fno_ext:
	bfextu		%d5{&21:&2},%d0
	lsl.l		%d0,%d2

# base address (passed as parameter in d3):
# we clear the value here if it should actually be suppressed.
fbase_supp_ck:
	btst		&0x7,%d5		# is the bd suppressed?
	beq.b		fno_base_sup
	clr.l		%d3

# base displacement:
fno_base_sup:
	bfextu		%d5{&26:&2},%d0		# get bd size
#	beq.l		fmovm_error		# if (size == 0) it's reserved

	cmpi.b		%d0,&0x2
	blt.b		fno_bd
	beq.b		fget_word_bd

	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long

	tst.l		%d1			# did ifetch fail?
	bne.l		fcea_iacc		# yes

	bra.b		fchk_ind

fget_word_bd:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word

	tst.l		%d1			# did ifetch fail?
	bne.l		fcea_iacc		# yes

	ext.l		%d0			# sign extend bd

fchk_ind:
	add.l		%d0,%d3			# base += bd

# outer displacement:
fno_bd:
	bfextu		%d5{&30:&2},%d0		# is od suppressed?
	beq.w		faii_bd

	cmpi.b		%d0,&0x2
	blt.b		fnull_od
	beq.b		fword_od

	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long

	tst.l		%d1			# did ifetch fail?
	bne.l		fcea_iacc		# yes

	bra.b		fadd_them

fword_od:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x2,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_word

	tst.l		%d1			# did ifetch fail?
	bne.l		fcea_iacc		# yes

	ext.l		%d0			# sign extend od
	bra.b		fadd_them

fnull_od:
	clr.l		%d0

fadd_them:
	mov.l		%d0,%d4

	btst		&0x2,%d5		# pre or post indexing?
	beq.b		fpre_indexed

	mov.l		%d3,%a0
	bsr.l		_dmem_read_long

	tst.l		%d1			# did dfetch fail?
	bne.w		fcea_err		# yes

	add.l		%d2,%d0			# <ea> += index
	add.l		%d4,%d0			# <ea> += od
	bra.b		fdone_ea

fpre_indexed:
	add.l		%d2,%d3			# preindexing
	mov.l		%d3,%a0
	bsr.l		_dmem_read_long

	tst.l		%d1			# did dfetch fail?
	bne.w		fcea_err		# yes

	add.l		%d4,%d0			# ea += od
	bra.b		fdone_ea

faii_bd:
	add.l		%d2,%d3			# ea = (base + bd) + index
	mov.l		%d3,%d0
fdone_ea:
	mov.l		%d0,%a0

	movm.l		(%sp)+,&0x003c		# restore d2-d5
	rts

#########################################################
fcea_err:
	mov.l		%d3,%a0

	movm.l		(%sp)+,&0x003c		# restore d2-d5
	mov.w		&0x0101,%d0
	bra.l		iea_dacc

fcea_iacc:
	movm.l		(%sp)+,&0x003c		# restore d2-d5
	bra.l		iea_iacc

fmovm_out_err:
	bsr.l		restore
	mov.w		&0x00e1,%d0
	bra.b		fmovm_err

fmovm_in_err:
	bsr.l		restore
	mov.w		&0x0161,%d0

fmovm_err:
	mov.l		L_SCR1(%a6),%a0
	bra.l		iea_dacc

#########################################################################
# XDEF ****************************************************************	#
#	fmovm_ctrl(): emulate fmovm.l of control registers instr	#
#									#
# XREF ****************************************************************	#
#	_imem_read_long() - read longword from memory			#
#	iea_iacc() - _imem_read_long() failed; error recovery		#
#									#
# INPUT ***************************************************************	#
#	None								#
#									#
# OUTPUT **************************************************************	#
#	If _imem_read_long() doesn't fail:				#
#		USER_FPCR(a6)  = new FPCR value				#
#		USER_FPSR(a6)  = new FPSR value				#
#		USER_FPIAR(a6) = new FPIAR value			#
#									#
# ALGORITHM ***********************************************************	#
#	Decode the instruction type by looking at the extension word	#
# in order to see how many control registers to fetch from memory.	#
# Fetch them using _imem_read_long(). If this fetch fails, exit through	#
# the special access error exit handler iea_iacc().			#
#									#
# Instruction word decoding:						#
#									#
#	fmovem.l #<data>, {FPIAR&|FPCR&|FPSR}				#
#									#
#		WORD1			WORD2				#
#	1111 0010 00 111100	100$ $$00 0000 0000			#
#									#
#	$$$ (100): FPCR							#
#	    (010): FPSR							#
#	    (001): FPIAR						#
#	    (000): FPIAR						#
#									#
#########################################################################

	global		fmovm_ctrl
fmovm_ctrl:
	mov.b		EXC_EXTWORD(%a6),%d0	# fetch reg select bits
	cmpi.b		%d0,&0x9c		# fpcr & fpsr & fpiar ?
	beq.w		fctrl_in_7		# yes
	cmpi.b		%d0,&0x98		# fpcr & fpsr ?
	beq.w		fctrl_in_6		# yes
	cmpi.b		%d0,&0x94		# fpcr & fpiar ?
	beq.b		fctrl_in_5		# yes

# fmovem.l #<data>, fpsr/fpiar
fctrl_in_3:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch FPSR from mem

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.l		%d0,USER_FPSR(%a6)	# store new FPSR to stack
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch FPIAR from mem

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.l		%d0,USER_FPIAR(%a6)	# store new FPIAR to stack
	rts

# fmovem.l #<data>, fpcr/fpiar
fctrl_in_5:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch FPCR from mem

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.l		%d0,USER_FPCR(%a6)	# store new FPCR to stack
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch FPIAR from mem

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.l		%d0,USER_FPIAR(%a6)	# store new FPIAR to stack
	rts

# fmovem.l #<data>, fpcr/fpsr
fctrl_in_6:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch FPCR from mem

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.l		%d0,USER_FPCR(%a6)	# store new FPCR to mem
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch FPSR from mem

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.l		%d0,USER_FPSR(%a6)	# store new FPSR to mem
	rts

# fmovem.l #<data>, fpcr/fpsr/fpiar
fctrl_in_7:
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch FPCR from mem

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.l		%d0,USER_FPCR(%a6)	# store new FPCR to mem
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch FPSR from mem

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.l		%d0,USER_FPSR(%a6)	# store new FPSR to mem
	mov.l		EXC_EXTWPTR(%a6),%a0	# fetch instruction addr
	addq.l		&0x4,EXC_EXTWPTR(%a6)	# incr instruction ptr
	bsr.l		_imem_read_long		# fetch FPIAR from mem

	tst.l		%d1			# did ifetch fail?
	bne.l		iea_iacc		# yes

	mov.l		%d0,USER_FPIAR(%a6)	# store new FPIAR to mem
	rts

##########################################################################

#########################################################################
# XDEF ****************************************************************	#
#	addsub_scaler2(): scale inputs to fadd/fsub such that no	#
#			  OVFL/UNFL exceptions will result		#
#									#
# XREF ****************************************************************	#
#	norm() - normalize mantissa after adjusting exponent		#
#									#
# INPUT ***************************************************************	#
#	FP_SRC(a6) = fp op1(src)					#
#	FP_DST(a6) = fp op2(dst)					#
#									#
# OUTPUT **************************************************************	#
#	FP_SRC(a6) = fp op1 scaled(src)					#
#	FP_DST(a6) = fp op2 scaled(dst)					#
#	d0         = scale amount					#
#									#
# ALGORITHM ***********************************************************	#
#	If the DST exponent is > the SRC exponent, set the DST exponent	#
# equal to 0x3fff and scale the SRC exponent by the value that the	#
# DST exponent was scaled by. If the SRC exponent is greater or equal,	#
# do the opposite. Return this scale factor in d0.			#
#	If the two exponents differ by > the number of mantissa bits	#
# plus two, then set the smallest exponent to a very small value as a	#
# quick shortcut.							#
#									#
#########################################################################

	global		addsub_scaler2
addsub_scaler2:
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		DST_HI(%a1),FP_SCR1_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	mov.l		DST_LO(%a1),FP_SCR1_LO(%a6)
	mov.w		SRC_EX(%a0),%d0
	mov.w		DST_EX(%a1),%d1
	mov.w		%d0,FP_SCR0_EX(%a6)
	mov.w		%d1,FP_SCR1_EX(%a6)

	andi.w		&0x7fff,%d0
	andi.w		&0x7fff,%d1
	mov.w		%d0,L_SCR1(%a6)		# store src exponent
	mov.w		%d1,2+L_SCR1(%a6)	# store dst exponent

	cmp.w		%d0, %d1		# is src exp >= dst exp?
	bge.l		src_exp_ge2

# dst exp is >  src exp; scale dst to exp = 0x3fff
dst_exp_gt2:
	bsr.l		scale_to_zero_dst
	mov.l		%d0,-(%sp)		# save scale factor

	cmpi.b		STAG(%a6),&DENORM	# is dst denormalized?
	bne.b		cmpexp12

	lea		FP_SCR0(%a6),%a0
	bsr.l		norm			# normalize the denorm; result is new exp
	neg.w		%d0			# new exp = -(shft val)
	mov.w		%d0,L_SCR1(%a6)		# inset new exp

cmpexp12:
	mov.w		2+L_SCR1(%a6),%d0
	subi.w		&mantissalen+2,%d0	# subtract mantissalen+2 from larger exp

	cmp.w		%d0,L_SCR1(%a6)		# is difference >= len(mantissa)+2?
	bge.b		quick_scale12

	mov.w		L_SCR1(%a6),%d0
	add.w		0x2(%sp),%d0		# scale src exponent by scale factor
	mov.w		FP_SCR0_EX(%a6),%d1
	and.w		&0x8000,%d1
	or.w		%d1,%d0			# concat {sgn,new exp}
	mov.w		%d0,FP_SCR0_EX(%a6)	# insert new dst exponent

	mov.l		(%sp)+,%d0		# return SCALE factor
	rts

quick_scale12:
	andi.w		&0x8000,FP_SCR0_EX(%a6)	# zero src exponent
	bset		&0x0,1+FP_SCR0_EX(%a6)	# set exp = 1

	mov.l		(%sp)+,%d0		# return SCALE factor
	rts

# src exp is >= dst exp; scale src to exp = 0x3fff
src_exp_ge2:
	bsr.l		scale_to_zero_src
	mov.l		%d0,-(%sp)		# save scale factor

	cmpi.b		DTAG(%a6),&DENORM	# is dst denormalized?
	bne.b		cmpexp22
	lea		FP_SCR1(%a6),%a0
	bsr.l		norm			# normalize the denorm; result is new exp
	neg.w		%d0			# new exp = -(shft val)
	mov.w		%d0,2+L_SCR1(%a6)	# inset new exp

cmpexp22:
	mov.w		L_SCR1(%a6),%d0
	subi.w		&mantissalen+2,%d0	# subtract mantissalen+2 from larger exp

	cmp.w		%d0,2+L_SCR1(%a6)	# is difference >= len(mantissa)+2?
	bge.b		quick_scale22

	mov.w		2+L_SCR1(%a6),%d0
	add.w		0x2(%sp),%d0		# scale dst exponent by scale factor
	mov.w		FP_SCR1_EX(%a6),%d1
	andi.w		&0x8000,%d1
	or.w		%d1,%d0			# concat {sgn,new exp}
	mov.w		%d0,FP_SCR1_EX(%a6)	# insert new dst exponent

	mov.l		(%sp)+,%d0		# return SCALE factor
	rts

quick_scale22:
	andi.w		&0x8000,FP_SCR1_EX(%a6)	# zero dst exponent
	bset		&0x0,1+FP_SCR1_EX(%a6)	# set exp = 1

	mov.l		(%sp)+,%d0		# return SCALE factor
	rts

##########################################################################

#########################################################################
# XDEF ****************************************************************	#
#	scale_to_zero_src(): scale the exponent of extended precision	#
#			     value at FP_SCR0(a6).			#
#									#
# XREF ****************************************************************	#
#	norm() - normalize the mantissa if the operand was a DENORM	#
#									#
# INPUT ***************************************************************	#
#	FP_SCR0(a6) = extended precision operand to be scaled		#
#									#
# OUTPUT **************************************************************	#
#	FP_SCR0(a6) = scaled extended precision operand			#
#	d0	    = scale value					#
#									#
# ALGORITHM ***********************************************************	#
#	Set the exponent of the input operand to 0x3fff. Save the value	#
# of the difference between the original and new exponent. Then,	#
# normalize the operand if it was a DENORM. Add this normalization	#
# value to the previous value. Return the result.			#
#									#
#########################################################################

	global		scale_to_zero_src
scale_to_zero_src:
	mov.w		FP_SCR0_EX(%a6),%d1	# extract operand's {sgn,exp}
	mov.w		%d1,%d0			# make a copy

	andi.l		&0x7fff,%d1		# extract operand's exponent

	andi.w		&0x8000,%d0		# extract operand's sgn
	or.w		&0x3fff,%d0		# insert new operand's exponent(=0)

	mov.w		%d0,FP_SCR0_EX(%a6)	# insert biased exponent

	cmpi.b		STAG(%a6),&DENORM	# is operand normalized?
	beq.b		stzs_denorm		# normalize the DENORM

stzs_norm:
	mov.l		&0x3fff,%d0
	sub.l		%d1,%d0			# scale = BIAS + (-exp)

	rts

stzs_denorm:
	lea		FP_SCR0(%a6),%a0	# pass ptr to src op
	bsr.l		norm			# normalize denorm
	neg.l		%d0			# new exponent = -(shft val)
	mov.l		%d0,%d1			# prepare for op_norm call
	bra.b		stzs_norm		# finish scaling

###

#########################################################################
# XDEF ****************************************************************	#
#	scale_sqrt(): scale the input operand exponent so a subsequent	#
#		      fsqrt operation won't take an exception.		#
#									#
# XREF ****************************************************************	#
#	norm() - normalize the mantissa if the operand was a DENORM	#
#									#
# INPUT ***************************************************************	#
#	FP_SCR0(a6) = extended precision operand to be scaled		#
#									#
# OUTPUT **************************************************************	#
#	FP_SCR0(a6) = scaled extended precision operand			#
#	d0	    = scale value					#
#									#
# ALGORITHM ***********************************************************	#
#	If the input operand is a DENORM, normalize it.			#
#	If the exponent of the input operand is even, set the exponent	#
# to 0x3ffe and return a scale factor of "(exp-0x3ffe)/2". If the	#
# exponent of the input operand is off, set the exponent to ox3fff and	#
# return a scale factor of "(exp-0x3fff)/2".				#
#									#
#########################################################################

	global		scale_sqrt
scale_sqrt:
	cmpi.b		STAG(%a6),&DENORM	# is operand normalized?
	beq.b		ss_denorm		# normalize the DENORM

	mov.w		FP_SCR0_EX(%a6),%d1	# extract operand's {sgn,exp}
	andi.l		&0x7fff,%d1		# extract operand's exponent

	andi.w		&0x8000,FP_SCR0_EX(%a6)	# extract operand's sgn

	btst		&0x0,%d1		# is exp even or odd?
	beq.b		ss_norm_even

	ori.w		&0x3fff,FP_SCR0_EX(%a6)	# insert new operand's exponent(=0)

	mov.l		&0x3fff,%d0
	sub.l		%d1,%d0			# scale = BIAS + (-exp)
	asr.l		&0x1,%d0		# divide scale factor by 2
	rts

ss_norm_even:
	ori.w		&0x3ffe,FP_SCR0_EX(%a6)	# insert new operand's exponent(=0)

	mov.l		&0x3ffe,%d0
	sub.l		%d1,%d0			# scale = BIAS + (-exp)
	asr.l		&0x1,%d0		# divide scale factor by 2
	rts

ss_denorm:
	lea		FP_SCR0(%a6),%a0	# pass ptr to src op
	bsr.l		norm			# normalize denorm

	btst		&0x0,%d0		# is exp even or odd?
	beq.b		ss_denorm_even

	ori.w		&0x3fff,FP_SCR0_EX(%a6)	# insert new operand's exponent(=0)

	add.l		&0x3fff,%d0
	asr.l		&0x1,%d0		# divide scale factor by 2
	rts

ss_denorm_even:
	ori.w		&0x3ffe,FP_SCR0_EX(%a6)	# insert new operand's exponent(=0)

	add.l		&0x3ffe,%d0
	asr.l		&0x1,%d0		# divide scale factor by 2
	rts

###

#########################################################################
# XDEF ****************************************************************	#
#	scale_to_zero_dst(): scale the exponent of extended precision	#
#			     value at FP_SCR1(a6).			#
#									#
# XREF ****************************************************************	#
#	norm() - normalize the mantissa if the operand was a DENORM	#
#									#
# INPUT ***************************************************************	#
#	FP_SCR1(a6) = extended precision operand to be scaled		#
#									#
# OUTPUT **************************************************************	#
#	FP_SCR1(a6) = scaled extended precision operand			#
#	d0	    = scale value					#
#									#
# ALGORITHM ***********************************************************	#
#	Set the exponent of the input operand to 0x3fff. Save the value	#
# of the difference between the original and new exponent. Then,	#
# normalize the operand if it was a DENORM. Add this normalization	#
# value to the previous value. Return the result.			#
#									#
#########################################################################

	global		scale_to_zero_dst
scale_to_zero_dst:
	mov.w		FP_SCR1_EX(%a6),%d1	# extract operand's {sgn,exp}
	mov.w		%d1,%d0			# make a copy

	andi.l		&0x7fff,%d1		# extract operand's exponent

	andi.w		&0x8000,%d0		# extract operand's sgn
	or.w		&0x3fff,%d0		# insert new operand's exponent(=0)

	mov.w		%d0,FP_SCR1_EX(%a6)	# insert biased exponent

	cmpi.b		DTAG(%a6),&DENORM	# is operand normalized?
	beq.b		stzd_denorm		# normalize the DENORM

stzd_norm:
	mov.l		&0x3fff,%d0
	sub.l		%d1,%d0			# scale = BIAS + (-exp)
	rts

stzd_denorm:
	lea		FP_SCR1(%a6),%a0	# pass ptr to dst op
	bsr.l		norm			# normalize denorm
	neg.l		%d0			# new exponent = -(shft val)
	mov.l		%d0,%d1			# prepare for op_norm call
	bra.b		stzd_norm		# finish scaling

##########################################################################

#########################################################################
# XDEF ****************************************************************	#
#	res_qnan(): return default result w/ QNAN operand for dyadic	#
#	res_snan(): return default result w/ SNAN operand for dyadic	#
#	res_qnan_1op(): return dflt result w/ QNAN operand for monadic	#
#	res_snan_1op(): return dflt result w/ SNAN operand for monadic	#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	FP_SRC(a6) = pointer to extended precision src operand		#
#	FP_DST(a6) = pointer to extended precision dst operand		#
#									#
# OUTPUT **************************************************************	#
#	fp0 = default result						#
#									#
# ALGORITHM ***********************************************************	#
#	If either operand (but not both operands) of an operation is a	#
# nonsignalling NAN, then that NAN is returned as the result. If both	#
# operands are nonsignalling NANs, then the destination operand		#
# nonsignalling NAN is returned as the result.				#
#	If either operand to an operation is a signalling NAN (SNAN),	#
# then, the SNAN bit is set in the FPSR EXC byte. If the SNAN trap	#
# enable bit is set in the FPCR, then the trap is taken and the		#
# destination is not modified. If the SNAN trap enable bit is not set,	#
# then the SNAN is converted to a nonsignalling NAN (by setting the	#
# SNAN bit in the operand to one), and the operation continues as	#
# described in the preceding paragraph, for nonsignalling NANs.		#
#	Make sure the appropriate FPSR bits are set before exiting.	#
#									#
#########################################################################

	global		res_qnan
	global		res_snan
res_qnan:
res_snan:
	cmp.b		DTAG(%a6), &SNAN	# is the dst an SNAN?
	beq.b		dst_snan2
	cmp.b		DTAG(%a6), &QNAN	# is the dst a  QNAN?
	beq.b		dst_qnan2
src_nan:
	cmp.b		STAG(%a6), &QNAN
	beq.b		src_qnan2
	global		res_snan_1op
res_snan_1op:
src_snan2:
	bset		&0x6, FP_SRC_HI(%a6)	# set SNAN bit
	or.l		&nan_mask+aiop_mask+snan_mask, USER_FPSR(%a6)
	lea		FP_SRC(%a6), %a0
	bra.b		nan_comp
	global		res_qnan_1op
res_qnan_1op:
src_qnan2:
	or.l		&nan_mask, USER_FPSR(%a6)
	lea		FP_SRC(%a6), %a0
	bra.b		nan_comp
dst_snan2:
	or.l		&nan_mask+aiop_mask+snan_mask, USER_FPSR(%a6)
	bset		&0x6, FP_DST_HI(%a6)	# set SNAN bit
	lea		FP_DST(%a6), %a0
	bra.b		nan_comp
dst_qnan2:
	lea		FP_DST(%a6), %a0
	cmp.b		STAG(%a6), &SNAN
	bne		nan_done
	or.l		&aiop_mask+snan_mask, USER_FPSR(%a6)
nan_done:
	or.l		&nan_mask, USER_FPSR(%a6)
nan_comp:
	btst		&0x7, FTEMP_EX(%a0)	# is NAN neg?
	beq.b		nan_not_neg
	or.l		&neg_mask, USER_FPSR(%a6)
nan_not_neg:
	fmovm.x		(%a0), &0x80
	rts

#########################################################################
# XDEF ****************************************************************	#
#	res_operr(): return default result during operand error		#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	None								#
#									#
# OUTPUT **************************************************************	#
#	fp0 = default operand error result				#
#									#
# ALGORITHM ***********************************************************	#
#	An nonsignalling NAN is returned as the default result when	#
# an operand error occurs for the following cases:			#
#									#
#	Multiply: (Infinity x Zero)					#
#	Divide  : (Zero / Zero) || (Infinity / Infinity)		#
#									#
#########################################################################

	global		res_operr
res_operr:
	or.l		&nan_mask+operr_mask+aiop_mask, USER_FPSR(%a6)
	fmovm.x		nan_return(%pc), &0x80
	rts

nan_return:
	long		0x7fff0000, 0xffffffff, 0xffffffff

#########################################################################
# XDEF ****************************************************************	#
#	_denorm(): denormalize an intermediate result			#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT *************************************************************** #
#	a0 = points to the operand to be denormalized			#
#		(in the internal extended format)			#
#									#
#	d0 = rounding precision						#
#									#
# OUTPUT **************************************************************	#
#	a0 = pointer to the denormalized result				#
#		(in the internal extended format)			#
#									#
#	d0 = guard,round,sticky						#
#									#
# ALGORITHM ***********************************************************	#
#	According to the exponent underflow threshold for the given	#
# precision, shift the mantissa bits to the right in order raise the	#
# exponent of the operand to the threshold value. While shifting the	#
# mantissa bits right, maintain the value of the guard, round, and	#
# sticky bits.								#
# other notes:								#
#	(1) _denorm() is called by the underflow routines		#
#	(2) _denorm() does NOT affect the status register		#
#									#
#########################################################################

#
# table of exponent threshold values for each precision
#
tbl_thresh:
	short		0x0
	short		sgl_thresh
	short		dbl_thresh

	global		_denorm
_denorm:
#
# Load the exponent threshold for the precision selected and check
# to see if (threshold - exponent) is > 65 in which case we can
# simply calculate the sticky bit and zero the mantissa. otherwise
# we have to call the denormalization routine.
#
	lsr.b		&0x2, %d0		# shift prec to lo bits
	mov.w		(tbl_thresh.b,%pc,%d0.w*2), %d1 # load prec threshold
	mov.w		%d1, %d0		# copy d1 into d0
	sub.w		FTEMP_EX(%a0), %d0	# diff = threshold - exp
	cmpi.w		%d0, &66		# is diff > 65? (mant + g,r bits)
	bpl.b		denorm_set_stky		# yes; just calc sticky

	clr.l		%d0			# clear g,r,s
	btst		&inex2_bit, FPSR_EXCEPT(%a6) # yes; was INEX2 set?
	beq.b		denorm_call		# no; don't change anything
	bset		&29, %d0		# yes; set sticky bit

denorm_call:
	bsr.l		dnrm_lp			# denormalize the number
	rts

#
# all bit would have been shifted off during the denorm so simply
# calculate if the sticky should be set and clear the entire mantissa.
#
denorm_set_stky:
	mov.l		&0x20000000, %d0	# set sticky bit in return value
	mov.w		%d1, FTEMP_EX(%a0)	# load exp with threshold
	clr.l		FTEMP_HI(%a0)		# set d1 = 0 (ms mantissa)
	clr.l		FTEMP_LO(%a0)		# set d2 = 0 (ms mantissa)
	rts

#									#
# dnrm_lp(): normalize exponent/mantissa to specified threshhold	#
#									#
# INPUT:								#
#	%a0	   : points to the operand to be denormalized		#
#	%d0{31:29} : initial guard,round,sticky				#
#	%d1{15:0}  : denormalization threshold				#
# OUTPUT:								#
#	%a0	   : points to the denormalized operand			#
#	%d0{31:29} : final guard,round,sticky				#
#									#

# *** Local Equates *** #
set	GRS,		L_SCR2			# g,r,s temp storage
set	FTEMP_LO2,	L_SCR1			# FTEMP_LO copy

	global		dnrm_lp
dnrm_lp:

#
# make a copy of FTEMP_LO and place the g,r,s bits directly after it
# in memory so as to make the bitfield extraction for denormalization easier.
#
	mov.l		FTEMP_LO(%a0), FTEMP_LO2(%a6) # make FTEMP_LO copy
	mov.l		%d0, GRS(%a6)		# place g,r,s after it

#
# check to see how much less than the underflow threshold the operand
# exponent is.
#
	mov.l		%d1, %d0		# copy the denorm threshold
	sub.w		FTEMP_EX(%a0), %d1	# d1 = threshold - uns exponent
	ble.b		dnrm_no_lp		# d1 <= 0
	cmpi.w		%d1, &0x20		# is ( 0 <= d1 < 32) ?
	blt.b		case_1			# yes
	cmpi.w		%d1, &0x40		# is (32 <= d1 < 64) ?
	blt.b		case_2			# yes
	bra.w		case_3			# (d1 >= 64)

#
# No normalization necessary
#
dnrm_no_lp:
	mov.l		GRS(%a6), %d0		# restore original g,r,s
	rts

#
# case (0<d1<32)
#
# %d0 = denorm threshold
# %d1 = "n" = amt to shift
#
#	---------------------------------------------------------
#	|     FTEMP_HI	  |	FTEMP_LO     |grs000.........000|
#	---------------------------------------------------------
#	<-(32 - n)-><-(n)-><-(32 - n)-><-(n)-><-(32 - n)-><-(n)->
#	\	   \		      \			 \
#	 \	    \		       \		  \
#	  \	     \			\		   \
#	   \	      \			 \		    \
#	    \	       \		  \		     \
#	     \		\		   \		      \
#	      \		 \		    \		       \
#	       \	  \		     \			\
#	<-(n)-><-(32 - n)-><------(32)-------><------(32)------->
#	---------------------------------------------------------
#	|0.....0| NEW_HI  |  NEW_FTEMP_LO     |grs		|
#	---------------------------------------------------------
#
case_1:
	mov.l		%d2, -(%sp)		# create temp storage

	mov.w		%d0, FTEMP_EX(%a0)	# exponent = denorm threshold
	mov.l		&32, %d0
	sub.w		%d1, %d0		# %d0 = 32 - %d1

	cmpi.w		%d1, &29		# is shft amt >= 29
	blt.b		case1_extract		# no; no fix needed
	mov.b		GRS(%a6), %d2
	or.b		%d2, 3+FTEMP_LO2(%a6)

case1_extract:
	bfextu		FTEMP_HI(%a0){&0:%d0}, %d2 # %d2 = new FTEMP_HI
	bfextu		FTEMP_HI(%a0){%d0:&32}, %d1 # %d1 = new FTEMP_LO
	bfextu		FTEMP_LO2(%a6){%d0:&32}, %d0 # %d0 = new G,R,S

	mov.l		%d2, FTEMP_HI(%a0)	# store new FTEMP_HI
	mov.l		%d1, FTEMP_LO(%a0)	# store new FTEMP_LO

	bftst		%d0{&2:&30}		# were bits shifted off?
	beq.b		case1_sticky_clear	# no; go finish
	bset		&rnd_stky_bit, %d0	# yes; set sticky bit

case1_sticky_clear:
	and.l		&0xe0000000, %d0	# clear all but G,R,S
	mov.l		(%sp)+, %d2		# restore temp register
	rts

#
# case (32<=d1<64)
#
# %d0 = denorm threshold
# %d1 = "n" = amt to shift
#
#	---------------------------------------------------------
#	|     FTEMP_HI	  |	FTEMP_LO     |grs000.........000|
#	---------------------------------------------------------
#	<-(32 - n)-><-(n)-><-(32 - n)-><-(n)-><-(32 - n)-><-(n)->
#	\	   \		      \
#	 \	    \		       \
#	  \	     \			-------------------
#	   \	      --------------------		   \
#	    -------------------		  \		    \
#			       \	   \		     \
#				\	    \		      \
#				 \	     \		       \
#	<-------(32)------><-(n)-><-(32 - n)-><------(32)------->
#	---------------------------------------------------------
#	|0...............0|0....0| NEW_LO     |grs		|
#	---------------------------------------------------------
#
case_2:
	mov.l		%d2, -(%sp)		# create temp storage

	mov.w		%d0, FTEMP_EX(%a0)	# exponent = denorm threshold
	subi.w		&0x20, %d1		# %d1 now between 0 and 32
	mov.l		&0x20, %d0
	sub.w		%d1, %d0		# %d0 = 32 - %d1

# subtle step here; or in the g,r,s at the bottom of FTEMP_LO to minimize
# the number of bits to check for the sticky detect.
# it only plays a role in shift amounts of 61-63.
	mov.b		GRS(%a6), %d2
	or.b		%d2, 3+FTEMP_LO2(%a6)

	bfextu		FTEMP_HI(%a0){&0:%d0}, %d2 # %d2 = new FTEMP_LO
	bfextu		FTEMP_HI(%a0){%d0:&32}, %d1 # %d1 = new G,R,S

	bftst		%d1{&2:&30}		# were any bits shifted off?
	bne.b		case2_set_sticky	# yes; set sticky bit
	bftst		FTEMP_LO2(%a6){%d0:&31}	# were any bits shifted off?
	bne.b		case2_set_sticky	# yes; set sticky bit

	mov.l		%d1, %d0		# move new G,R,S to %d0
	bra.b		case2_end

case2_set_sticky:
	mov.l		%d1, %d0		# move new G,R,S to %d0
	bset		&rnd_stky_bit, %d0	# set sticky bit

case2_end:
	clr.l		FTEMP_HI(%a0)		# store FTEMP_HI = 0
	mov.l		%d2, FTEMP_LO(%a0)	# store FTEMP_LO
	and.l		&0xe0000000, %d0	# clear all but G,R,S

	mov.l		(%sp)+,%d2		# restore temp register
	rts

#
# case (d1>=64)
#
# %d0 = denorm threshold
# %d1 = amt to shift
#
case_3:
	mov.w		%d0, FTEMP_EX(%a0)	# insert denorm threshold

	cmpi.w		%d1, &65		# is shift amt > 65?
	blt.b		case3_64		# no; it's == 64
	beq.b		case3_65		# no; it's == 65

#
# case (d1>65)
#
# Shift value is > 65 and out of range. All bits are shifted off.
# Return a zero mantissa with the sticky bit set
#
	clr.l		FTEMP_HI(%a0)		# clear hi(mantissa)
	clr.l		FTEMP_LO(%a0)		# clear lo(mantissa)
	mov.l		&0x20000000, %d0	# set sticky bit
	rts

#
# case (d1 == 64)
#
#	---------------------------------------------------------
#	|     FTEMP_HI	  |	FTEMP_LO     |grs000.........000|
#	---------------------------------------------------------
#	<-------(32)------>
#	\		   \
#	 \		    \
#	  \		     \
#	   \		      ------------------------------
#	    -------------------------------		    \
#					   \		     \
#					    \		      \
#					     \		       \
#					      <-------(32)------>
#	---------------------------------------------------------
#	|0...............0|0................0|grs		|
#	---------------------------------------------------------
#
case3_64:
	mov.l		FTEMP_HI(%a0), %d0	# fetch hi(mantissa)
	mov.l		%d0, %d1		# make a copy
	and.l		&0xc0000000, %d0	# extract G,R
	and.l		&0x3fffffff, %d1	# extract other bits

	bra.b		case3_complete

#
# case (d1 == 65)
#
#	---------------------------------------------------------
#	|     FTEMP_HI	  |	FTEMP_LO     |grs000.........000|
#	---------------------------------------------------------
#	<-------(32)------>
#	\		   \
#	 \		    \
#	  \		     \
#	   \		      ------------------------------
#	    --------------------------------		    \
#					    \		     \
#					     \		      \
#					      \		       \
#					       <-------(31)----->
#	---------------------------------------------------------
#	|0...............0|0................0|0rs		|
#	---------------------------------------------------------
#
case3_65:
	mov.l		FTEMP_HI(%a0), %d0	# fetch hi(mantissa)
	and.l		&0x80000000, %d0	# extract R bit
	lsr.l		&0x1, %d0		# shift high bit into R bit
	and.l		&0x7fffffff, %d1	# extract other bits

case3_complete:
# last operation done was an "and" of the bits shifted off so the condition
# codes are already set so branch accordingly.
	bne.b		case3_set_sticky	# yes; go set new sticky
	tst.l		FTEMP_LO(%a0)		# were any bits shifted off?
	bne.b		case3_set_sticky	# yes; go set new sticky
	tst.b		GRS(%a6)		# were any bits shifted off?
	bne.b		case3_set_sticky	# yes; go set new sticky

#
# no bits were shifted off so don't set the sticky bit.
# the guard and
# the entire mantissa is zero.
#
	clr.l		FTEMP_HI(%a0)		# clear hi(mantissa)
	clr.l		FTEMP_LO(%a0)		# clear lo(mantissa)
	rts

#
# some bits were shifted off so set the sticky bit.
# the entire mantissa is zero.
#
case3_set_sticky:
	bset		&rnd_stky_bit,%d0	# set new sticky bit
	clr.l		FTEMP_HI(%a0)		# clear hi(mantissa)
	clr.l		FTEMP_LO(%a0)		# clear lo(mantissa)
	rts

#########################################################################
# XDEF ****************************************************************	#
#	_round(): round result according to precision/mode		#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	a0	  = ptr to input operand in internal extended format	#
#	d1(hi)    = contains rounding precision:			#
#			ext = $0000xxxx					#
#			sgl = $0004xxxx					#
#			dbl = $0008xxxx					#
#	d1(lo)	  = contains rounding mode:				#
#			RN  = $xxxx0000					#
#			RZ  = $xxxx0001					#
#			RM  = $xxxx0002					#
#			RP  = $xxxx0003					#
#	d0{31:29} = contains the g,r,s bits (extended)			#
#									#
# OUTPUT **************************************************************	#
#	a0 = pointer to rounded result					#
#									#
# ALGORITHM ***********************************************************	#
#	On return the value pointed to by a0 is correctly rounded,	#
#	a0 is preserved and the g-r-s bits in d0 are cleared.		#
#	The result is not typed - the tag field is invalid.  The	#
#	result is still in the internal extended format.		#
#									#
#	The INEX bit of USER_FPSR will be set if the rounded result was	#
#	inexact (i.e. if any of the g-r-s bits were set).		#
#									#
#########################################################################

	global		_round
_round:
#
# ext_grs() looks at the rounding precision and sets the appropriate
# G,R,S bits.
# If (G,R,S == 0) then result is exact and round is done, else set
# the inex flag in status reg and continue.
#
	bsr.l		ext_grs			# extract G,R,S

	tst.l		%d0			# are G,R,S zero?
	beq.w		truncate		# yes; round is complete

	or.w		&inx2a_mask, 2+USER_FPSR(%a6) # set inex2/ainex

#
# Use rounding mode as an index into a jump table for these modes.
# All of the following assumes grs != 0.
#
	mov.w		(tbl_mode.b,%pc,%d1.w*2), %a1 # load jump offset
	jmp		(tbl_mode.b,%pc,%a1)	# jmp to rnd mode handler

tbl_mode:
	short		rnd_near - tbl_mode
	short		truncate - tbl_mode	# RZ always truncates
	short		rnd_mnus - tbl_mode
	short		rnd_plus - tbl_mode

#################################################################
#	ROUND PLUS INFINITY					#
#								#
#	If sign of fp number = 0 (positive), then add 1 to l.	#
#################################################################
rnd_plus:
	tst.b		FTEMP_SGN(%a0)		# check for sign
	bmi.w		truncate		# if positive then truncate

	mov.l		&0xffffffff, %d0	# force g,r,s to be all f's
	swap		%d1			# set up d1 for round prec.

	cmpi.b		%d1, &s_mode		# is prec = sgl?
	beq.w		add_sgl			# yes
	bgt.w		add_dbl			# no; it's dbl
	bra.w		add_ext			# no; it's ext

#################################################################
#	ROUND MINUS INFINITY					#
#								#
#	If sign of fp number = 1 (negative), then add 1 to l.	#
#################################################################
rnd_mnus:
	tst.b		FTEMP_SGN(%a0)		# check for sign
	bpl.w		truncate		# if negative then truncate

	mov.l		&0xffffffff, %d0	# force g,r,s to be all f's
	swap		%d1			# set up d1 for round prec.

	cmpi.b		%d1, &s_mode		# is prec = sgl?
	beq.w		add_sgl			# yes
	bgt.w		add_dbl			# no; it's dbl
	bra.w		add_ext			# no; it's ext

#################################################################
#	ROUND NEAREST						#
#								#
#	If (g=1), then add 1 to l and if (r=s=0), then clear l	#
#	Note that this will round to even in case of a tie.	#
#################################################################
rnd_near:
	asl.l		&0x1, %d0		# shift g-bit to c-bit
	bcc.w		truncate		# if (g=1) then

	swap		%d1			# set up d1 for round prec.

	cmpi.b		%d1, &s_mode		# is prec = sgl?
	beq.w		add_sgl			# yes
	bgt.w		add_dbl			# no; it's dbl
	bra.w		add_ext			# no; it's ext

# *** LOCAL EQUATES ***
set	ad_1_sgl,	0x00000100	# constant to add 1 to l-bit in sgl prec
set	ad_1_dbl,	0x00000800	# constant to add 1 to l-bit in dbl prec

#########################
#	ADD SINGLE	#
#########################
add_sgl:
	add.l		&ad_1_sgl, FTEMP_HI(%a0)
	bcc.b		scc_clr			# no mantissa overflow
	roxr.w		FTEMP_HI(%a0)		# shift v-bit back in
	roxr.w		FTEMP_HI+2(%a0)		# shift v-bit back in
	add.w		&0x1, FTEMP_EX(%a0)	# and incr exponent
scc_clr:
	tst.l		%d0			# test for rs = 0
	bne.b		sgl_done
	and.w		&0xfe00, FTEMP_HI+2(%a0) # clear the l-bit
sgl_done:
	and.l		&0xffffff00, FTEMP_HI(%a0) # truncate bits beyond sgl limit
	clr.l		FTEMP_LO(%a0)		# clear d2
	rts

#########################
#	ADD EXTENDED	#
#########################
add_ext:
	addq.l		&1,FTEMP_LO(%a0)	# add 1 to l-bit
	bcc.b		xcc_clr			# test for carry out
	addq.l		&1,FTEMP_HI(%a0)	# propagate carry
	bcc.b		xcc_clr
	roxr.w		FTEMP_HI(%a0)		# mant is 0 so restore v-bit
	roxr.w		FTEMP_HI+2(%a0)		# mant is 0 so restore v-bit
	roxr.w		FTEMP_LO(%a0)
	roxr.w		FTEMP_LO+2(%a0)
	add.w		&0x1,FTEMP_EX(%a0)	# and inc exp
xcc_clr:
	tst.l		%d0			# test rs = 0
	bne.b		add_ext_done
	and.b		&0xfe,FTEMP_LO+3(%a0)	# clear the l bit
add_ext_done:
	rts

#########################
#	ADD DOUBLE	#
#########################
add_dbl:
	add.l		&ad_1_dbl, FTEMP_LO(%a0) # add 1 to lsb
	bcc.b		dcc_clr			# no carry
	addq.l		&0x1, FTEMP_HI(%a0)	# propagate carry
	bcc.b		dcc_clr			# no carry

	roxr.w		FTEMP_HI(%a0)		# mant is 0 so restore v-bit
	roxr.w		FTEMP_HI+2(%a0)		# mant is 0 so restore v-bit
	roxr.w		FTEMP_LO(%a0)
	roxr.w		FTEMP_LO+2(%a0)
	addq.w		&0x1, FTEMP_EX(%a0)	# incr exponent
dcc_clr:
	tst.l		%d0			# test for rs = 0
	bne.b		dbl_done
	and.w		&0xf000, FTEMP_LO+2(%a0) # clear the l-bit

dbl_done:
	and.l		&0xfffff800,FTEMP_LO(%a0) # truncate bits beyond dbl limit
	rts

###########################
# Truncate all other bits #
###########################
truncate:
	swap		%d1			# select rnd prec

	cmpi.b		%d1, &s_mode		# is prec sgl?
	beq.w		sgl_done		# yes
	bgt.b		dbl_done		# no; it's dbl
	rts					# no; it's ext


#
# ext_grs(): extract guard, round and sticky bits according to
#	     rounding precision.
#
# INPUT
#	d0	   = extended precision g,r,s (in d0{31:29})
#	d1	   = {PREC,ROUND}
# OUTPUT
#	d0{31:29}  = guard, round, sticky
#
# The ext_grs extract the guard/round/sticky bits according to the
# selected rounding precision. It is called by the round subroutine
# only.  All registers except d0 are kept intact. d0 becomes an
# updated guard,round,sticky in d0{31:29}
#
# Notes: the ext_grs uses the round PREC, and therefore has to swap d1
#	 prior to usage, and needs to restore d1 to original. this
#	 routine is tightly tied to the round routine and not meant to
#	 uphold standard subroutine calling practices.
#

ext_grs:
	swap		%d1			# have d1.w point to round precision
	tst.b		%d1			# is rnd prec = extended?
	bne.b		ext_grs_not_ext		# no; go handle sgl or dbl

#
# %d0 actually already hold g,r,s since _round() had it before calling
# this function. so, as long as we don't disturb it, we are "returning" it.
#
ext_grs_ext:
	swap		%d1			# yes; return to correct positions
	rts

ext_grs_not_ext:
	movm.l		&0x3000, -(%sp)		# make some temp registers {d2/d3}

	cmpi.b		%d1, &s_mode		# is rnd prec = sgl?
	bne.b		ext_grs_dbl		# no; go handle dbl

#
# sgl:
#	96		64	  40	32		0
#	-----------------------------------------------------
#	| EXP	|XXXXXXX|	  |xx	|		|grs|
#	-----------------------------------------------------
#			<--(24)--->nn\			   /
#				   ee ---------------------
#				   ww		|
#						v
#				   gr	   new sticky
#
ext_grs_sgl:
	bfextu		FTEMP_HI(%a0){&24:&2}, %d3 # sgl prec. g-r are 2 bits right
	mov.l		&30, %d2		# of the sgl prec. limits
	lsl.l		%d2, %d3		# shift g-r bits to MSB of d3
	mov.l		FTEMP_HI(%a0), %d2	# get word 2 for s-bit test
	and.l		&0x0000003f, %d2	# s bit is the or of all other
	bne.b		ext_grs_st_stky		# bits to the right of g-r
	tst.l		FTEMP_LO(%a0)		# test lower mantissa
	bne.b		ext_grs_st_stky		# if any are set, set sticky
	tst.l		%d0			# test original g,r,s
	bne.b		ext_grs_st_stky		# if any are set, set sticky
	bra.b		ext_grs_end_sd		# if words 3 and 4 are clr, exit

#
# dbl:
#	96		64		32	 11	0
#	-----------------------------------------------------
#	| EXP	|XXXXXXX|		|	 |xx	|grs|
#	-----------------------------------------------------
#						  nn\	    /
#						  ee -------
#						  ww	|
#							v
#						  gr	new sticky
#
ext_grs_dbl:
	bfextu		FTEMP_LO(%a0){&21:&2}, %d3 # dbl-prec. g-r are 2 bits right
	mov.l		&30, %d2		# of the dbl prec. limits
	lsl.l		%d2, %d3		# shift g-r bits to the MSB of d3
	mov.l		FTEMP_LO(%a0), %d2	# get lower mantissa  for s-bit test
	and.l		&0x000001ff, %d2	# s bit is the or-ing of all
	bne.b		ext_grs_st_stky		# other bits to the right of g-r
	tst.l		%d0			# test word original g,r,s
	bne.b		ext_grs_st_stky		# if any are set, set sticky
	bra.b		ext_grs_end_sd		# if clear, exit

ext_grs_st_stky:
	bset		&rnd_stky_bit, %d3	# set sticky bit
ext_grs_end_sd:
	mov.l		%d3, %d0		# return grs to d0

	movm.l		(%sp)+, &0xc		# restore scratch registers {d2/d3}

	swap		%d1			# restore d1 to original
	rts

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

#########################################################################
# XDEF ****************************************************************	#
#	set_tag_x(): return the optype of the input ext fp number	#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision operand			#
#									#
# OUTPUT **************************************************************	#
#	d0 = value of type tag						#
#		one of: NORM, INF, QNAN, SNAN, DENORM, UNNORM, ZERO	#
#									#
# ALGORITHM ***********************************************************	#
#	Simply test the exponent, j-bit, and mantissa values to		#
# determine the type of operand.					#
#	If it's an unnormalized zero, alter the operand and force it	#
# to be a normal zero.							#
#									#
#########################################################################

	global		set_tag_x
set_tag_x:
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
# must distinguish now "Unnormalized zeroes" which we
# must convert to zero.
is_unnorm_x:
	tst.l		FTEMP_HI(%a0)
	bne.b		is_unnorm_reg_x
	tst.l		FTEMP_LO(%a0)
	bne.b		is_unnorm_reg_x
# it's an "unnormalized zero". let's convert it to an actual zero...
	andi.w		&0x8000,FTEMP_EX(%a0)	# clear exponent
	mov.b		&ZERO, %d0
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
	btst		&0x6, FTEMP_HI(%a0)
	beq.b		is_snan_x
	mov.b		&QNAN, %d0
	rts
is_snan_x:
	mov.b		&SNAN, %d0
	rts

#########################################################################
# XDEF ****************************************************************	#
#	set_tag_d(): return the optype of the input dbl fp number	#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	a0 = points to double precision operand				#
#									#
# OUTPUT **************************************************************	#
#	d0 = value of type tag						#
#		one of: NORM, INF, QNAN, SNAN, DENORM, ZERO		#
#									#
# ALGORITHM ***********************************************************	#
#	Simply test the exponent, j-bit, and mantissa values to		#
# determine the type of operand.					#
#									#
#########################################################################

	global		set_tag_d
set_tag_d:
	mov.l		FTEMP(%a0), %d0
	mov.l		%d0, %d1

	andi.l		&0x7ff00000, %d0
	beq.b		zero_or_denorm_d

	cmpi.l		%d0, &0x7ff00000
	beq.b		inf_or_nan_d

is_norm_d:
	mov.b		&NORM, %d0
	rts
zero_or_denorm_d:
	and.l		&0x000fffff, %d1
	bne		is_denorm_d
	tst.l		4+FTEMP(%a0)
	bne		is_denorm_d
is_zero_d:
	mov.b		&ZERO, %d0
	rts
is_denorm_d:
	mov.b		&DENORM, %d0
	rts
inf_or_nan_d:
	and.l		&0x000fffff, %d1
	bne		is_nan_d
	tst.l		4+FTEMP(%a0)
	bne		is_nan_d
is_inf_d:
	mov.b		&INF, %d0
	rts
is_nan_d:
	btst		&19, %d1
	bne		is_qnan_d
is_snan_d:
	mov.b		&SNAN, %d0
	rts
is_qnan_d:
	mov.b		&QNAN, %d0
	rts

#########################################################################
# XDEF ****************************************************************	#
#	set_tag_s(): return the optype of the input sgl fp number	#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to single precision operand			#
#									#
# OUTPUT **************************************************************	#
#	d0 = value of type tag						#
#		one of: NORM, INF, QNAN, SNAN, DENORM, ZERO		#
#									#
# ALGORITHM ***********************************************************	#
#	Simply test the exponent, j-bit, and mantissa values to		#
# determine the type of operand.					#
#									#
#########################################################################

	global		set_tag_s
set_tag_s:
	mov.l		FTEMP(%a0), %d0
	mov.l		%d0, %d1

	andi.l		&0x7f800000, %d0
	beq.b		zero_or_denorm_s

	cmpi.l		%d0, &0x7f800000
	beq.b		inf_or_nan_s

is_norm_s:
	mov.b		&NORM, %d0
	rts
zero_or_denorm_s:
	and.l		&0x007fffff, %d1
	bne		is_denorm_s
is_zero_s:
	mov.b		&ZERO, %d0
	rts
is_denorm_s:
	mov.b		&DENORM, %d0
	rts
inf_or_nan_s:
	and.l		&0x007fffff, %d1
	bne		is_nan_s
is_inf_s:
	mov.b		&INF, %d0
	rts
is_nan_s:
	btst		&22, %d1
	bne		is_qnan_s
is_snan_s:
	mov.b		&SNAN, %d0
	rts
is_qnan_s:
	mov.b		&QNAN, %d0
	rts

#########################################################################
# XDEF ****************************************************************	#
#	unf_res(): routine to produce default underflow result of a	#
#		   scaled extended precision number; this is used by	#
#		   fadd/fdiv/fmul/etc. emulation routines.		#
#	unf_res4(): same as above but for fsglmul/fsgldiv which use	#
#		    single round prec and extended prec mode.		#
#									#
# XREF ****************************************************************	#
#	_denorm() - denormalize according to scale factor		#
#	_round() - round denormalized number according to rnd prec	#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precison operand			#
#	d0 = scale factor						#
#	d1 = rounding precision/mode					#
#									#
# OUTPUT **************************************************************	#
#	a0 = pointer to default underflow result in extended precision	#
#	d0.b = result FPSR_cc which caller may or may not want to save	#
#									#
# ALGORITHM ***********************************************************	#
#	Convert the input operand to "internal format" which means the	#
# exponent is extended to 16 bits and the sign is stored in the unused	#
# portion of the extended precison operand. Denormalize the number	#
# according to the scale factor passed in d0. Then, round the		#
# denormalized result.							#
#	Set the FPSR_exc bits as appropriate but return the cc bits in	#
# d0 in case the caller doesn't want to save them (as is the case for	#
# fmove out).								#
#	unf_res4() for fsglmul/fsgldiv forces the denorm to extended	#
# precision and the rounding mode to single.				#
#									#
#########################################################################
	global		unf_res
unf_res:
	mov.l		%d1, -(%sp)		# save rnd prec,mode on stack

	btst		&0x7, FTEMP_EX(%a0)	# make "internal" format
	sne		FTEMP_SGN(%a0)

	mov.w		FTEMP_EX(%a0), %d1	# extract exponent
	and.w		&0x7fff, %d1
	sub.w		%d0, %d1
	mov.w		%d1, FTEMP_EX(%a0)	# insert 16 bit exponent

	mov.l		%a0, -(%sp)		# save operand ptr during calls

	mov.l		0x4(%sp),%d0		# pass rnd prec.
	andi.w		&0x00c0,%d0
	lsr.w		&0x4,%d0
	bsr.l		_denorm			# denorm result

	mov.l		(%sp),%a0
	mov.w		0x6(%sp),%d1		# load prec:mode into %d1
	andi.w		&0xc0,%d1		# extract rnd prec
	lsr.w		&0x4,%d1
	swap		%d1
	mov.w		0x6(%sp),%d1
	andi.w		&0x30,%d1
	lsr.w		&0x4,%d1
	bsr.l		_round			# round the denorm

	mov.l		(%sp)+, %a0

# result is now rounded properly. convert back to normal format
	bclr		&0x7, FTEMP_EX(%a0)	# clear sgn first; may have residue
	tst.b		FTEMP_SGN(%a0)		# is "internal result" sign set?
	beq.b		unf_res_chkifzero	# no; result is positive
	bset		&0x7, FTEMP_EX(%a0)	# set result sgn
	clr.b		FTEMP_SGN(%a0)		# clear temp sign

# the number may have become zero after rounding. set ccodes accordingly.
unf_res_chkifzero:
	clr.l		%d0
	tst.l		FTEMP_HI(%a0)		# is value now a zero?
	bne.b		unf_res_cont		# no
	tst.l		FTEMP_LO(%a0)
	bne.b		unf_res_cont		# no
#	bset		&z_bit, FPSR_CC(%a6)	# yes; set zero ccode bit
	bset		&z_bit, %d0		# yes; set zero ccode bit

unf_res_cont:

#
# can inex1 also be set along with unfl and inex2???
#
# we know that underflow has occurred. aunfl should be set if INEX2 is also set.
#
	btst		&inex2_bit, FPSR_EXCEPT(%a6) # is INEX2 set?
	beq.b		unf_res_end		# no
	bset		&aunfl_bit, FPSR_AEXCEPT(%a6) # yes; set aunfl

unf_res_end:
	add.l		&0x4, %sp		# clear stack
	rts

# unf_res() for fsglmul() and fsgldiv().
	global		unf_res4
unf_res4:
	mov.l		%d1,-(%sp)		# save rnd prec,mode on stack

	btst		&0x7,FTEMP_EX(%a0)	# make "internal" format
	sne		FTEMP_SGN(%a0)

	mov.w		FTEMP_EX(%a0),%d1	# extract exponent
	and.w		&0x7fff,%d1
	sub.w		%d0,%d1
	mov.w		%d1,FTEMP_EX(%a0)	# insert 16 bit exponent

	mov.l		%a0,-(%sp)		# save operand ptr during calls

	clr.l		%d0			# force rnd prec = ext
	bsr.l		_denorm			# denorm result

	mov.l		(%sp),%a0
	mov.w		&s_mode,%d1		# force rnd prec = sgl
	swap		%d1
	mov.w		0x6(%sp),%d1		# load rnd mode
	andi.w		&0x30,%d1		# extract rnd prec
	lsr.w		&0x4,%d1
	bsr.l		_round			# round the denorm

	mov.l		(%sp)+,%a0

# result is now rounded properly. convert back to normal format
	bclr		&0x7,FTEMP_EX(%a0)	# clear sgn first; may have residue
	tst.b		FTEMP_SGN(%a0)		# is "internal result" sign set?
	beq.b		unf_res4_chkifzero	# no; result is positive
	bset		&0x7,FTEMP_EX(%a0)	# set result sgn
	clr.b		FTEMP_SGN(%a0)		# clear temp sign

# the number may have become zero after rounding. set ccodes accordingly.
unf_res4_chkifzero:
	clr.l		%d0
	tst.l		FTEMP_HI(%a0)		# is value now a zero?
	bne.b		unf_res4_cont		# no
	tst.l		FTEMP_LO(%a0)
	bne.b		unf_res4_cont		# no
#	bset		&z_bit,FPSR_CC(%a6)	# yes; set zero ccode bit
	bset		&z_bit,%d0		# yes; set zero ccode bit

unf_res4_cont:

#
# can inex1 also be set along with unfl and inex2???
#
# we know that underflow has occurred. aunfl should be set if INEX2 is also set.
#
	btst		&inex2_bit,FPSR_EXCEPT(%a6) # is INEX2 set?
	beq.b		unf_res4_end		# no
	bset		&aunfl_bit,FPSR_AEXCEPT(%a6) # yes; set aunfl

unf_res4_end:
	add.l		&0x4,%sp		# clear stack
	rts

#########################################################################
# XDEF ****************************************************************	#
#	ovf_res(): routine to produce the default overflow result of	#
#		   an overflowing number.				#
#	ovf_res2(): same as above but the rnd mode/prec are passed	#
#		    differently.					#
#									#
# XREF ****************************************************************	#
#	none								#
#									#
# INPUT ***************************************************************	#
#	d1.b	= '-1' => (-); '0' => (+)				#
#   ovf_res():								#
#	d0	= rnd mode/prec						#
#   ovf_res2():								#
#	hi(d0)	= rnd prec						#
#	lo(d0)	= rnd mode						#
#									#
# OUTPUT **************************************************************	#
#	a0	= points to extended precision result			#
#	d0.b	= condition code bits					#
#									#
# ALGORITHM ***********************************************************	#
#	The default overflow result can be determined by the sign of	#
# the result and the rounding mode/prec in effect. These bits are	#
# concatenated together to create an index into the default result	#
# table. A pointer to the correct result is returned in a0. The		#
# resulting condition codes are returned in d0 in case the caller	#
# doesn't want FPSR_cc altered (as is the case for fmove out).		#
#									#
#########################################################################

	global		ovf_res
ovf_res:
	andi.w		&0x10,%d1		# keep result sign
	lsr.b		&0x4,%d0		# shift prec/mode
	or.b		%d0,%d1			# concat the two
	mov.w		%d1,%d0			# make a copy
	lsl.b		&0x1,%d1		# multiply d1 by 2
	bra.b		ovf_res_load

	global		ovf_res2
ovf_res2:
	and.w		&0x10, %d1		# keep result sign
	or.b		%d0, %d1		# insert rnd mode
	swap		%d0
	or.b		%d0, %d1		# insert rnd prec
	mov.w		%d1, %d0		# make a copy
	lsl.b		&0x1, %d1		# shift left by 1

#
# use the rounding mode, precision, and result sign as in index into the
# two tables below to fetch the default result and the result ccodes.
#
ovf_res_load:
	mov.b		(tbl_ovfl_cc.b,%pc,%d0.w*1), %d0 # fetch result ccodes
	lea		(tbl_ovfl_result.b,%pc,%d1.w*8), %a0 # return result ptr

	rts

tbl_ovfl_cc:
	byte		0x2, 0x0, 0x0, 0x2
	byte		0x2, 0x0, 0x0, 0x2
	byte		0x2, 0x0, 0x0, 0x2
	byte		0x0, 0x0, 0x0, 0x0
	byte		0x2+0x8, 0x8, 0x2+0x8, 0x8
	byte		0x2+0x8, 0x8, 0x2+0x8, 0x8
	byte		0x2+0x8, 0x8, 0x2+0x8, 0x8

tbl_ovfl_result:
	long		0x7fff0000,0x00000000,0x00000000,0x00000000 # +INF; RN
	long		0x7ffe0000,0xffffffff,0xffffffff,0x00000000 # +EXT; RZ
	long		0x7ffe0000,0xffffffff,0xffffffff,0x00000000 # +EXT; RM
	long		0x7fff0000,0x00000000,0x00000000,0x00000000 # +INF; RP

	long		0x7fff0000,0x00000000,0x00000000,0x00000000 # +INF; RN
	long		0x407e0000,0xffffff00,0x00000000,0x00000000 # +SGL; RZ
	long		0x407e0000,0xffffff00,0x00000000,0x00000000 # +SGL; RM
	long		0x7fff0000,0x00000000,0x00000000,0x00000000 # +INF; RP

	long		0x7fff0000,0x00000000,0x00000000,0x00000000 # +INF; RN
	long		0x43fe0000,0xffffffff,0xfffff800,0x00000000 # +DBL; RZ
	long		0x43fe0000,0xffffffff,0xfffff800,0x00000000 # +DBL; RM
	long		0x7fff0000,0x00000000,0x00000000,0x00000000 # +INF; RP

	long		0x00000000,0x00000000,0x00000000,0x00000000
	long		0x00000000,0x00000000,0x00000000,0x00000000
	long		0x00000000,0x00000000,0x00000000,0x00000000
	long		0x00000000,0x00000000,0x00000000,0x00000000

	long		0xffff0000,0x00000000,0x00000000,0x00000000 # -INF; RN
	long		0xfffe0000,0xffffffff,0xffffffff,0x00000000 # -EXT; RZ
	long		0xffff0000,0x00000000,0x00000000,0x00000000 # -INF; RM
	long		0xfffe0000,0xffffffff,0xffffffff,0x00000000 # -EXT; RP

	long		0xffff0000,0x00000000,0x00000000,0x00000000 # -INF; RN
	long		0xc07e0000,0xffffff00,0x00000000,0x00000000 # -SGL; RZ
	long		0xffff0000,0x00000000,0x00000000,0x00000000 # -INF; RM
	long		0xc07e0000,0xffffff00,0x00000000,0x00000000 # -SGL; RP

	long		0xffff0000,0x00000000,0x00000000,0x00000000 # -INF; RN
	long		0xc3fe0000,0xffffffff,0xfffff800,0x00000000 # -DBL; RZ
	long		0xffff0000,0x00000000,0x00000000,0x00000000 # -INF; RM
	long		0xc3fe0000,0xffffffff,0xfffff800,0x00000000 # -DBL; RP

#########################################################################
# XDEF ****************************************************************	#
#	fout(): move from fp register to memory or data register	#
#									#
# XREF ****************************************************************	#
#	_round() - needed to create EXOP for sgl/dbl precision		#
#	norm() - needed to create EXOP for extended precision		#
#	ovf_res() - create default overflow result for sgl/dbl precision#
#	unf_res() - create default underflow result for sgl/dbl prec.	#
#	dst_dbl() - create rounded dbl precision result.		#
#	dst_sgl() - create rounded sgl precision result.		#
#	fetch_dreg() - fetch dynamic k-factor reg for packed.		#
#	bindec() - convert FP binary number to packed number.		#
#	_mem_write() - write data to memory.				#
#	_mem_write2() - write data to memory unless supv mode -(a7) exc.#
#	_dmem_write_{byte,word,long}() - write data to memory.		#
#	store_dreg_{b,w,l}() - store data to data register file.	#
#	facc_out_{b,w,l,d,x}() - data access error occurred.		#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	d0 = round prec,mode						#
#									#
# OUTPUT **************************************************************	#
#	fp0 : intermediate underflow or overflow result if		#
#	      OVFL/UNFL occurred for a sgl or dbl operand		#
#									#
# ALGORITHM ***********************************************************	#
#	This routine is accessed by many handlers that need to do an	#
# opclass three move of an operand out to memory.			#
#	Decode an fmove out (opclass 3) instruction to determine if	#
# it's b,w,l,s,d,x, or p in size. b,w,l can be stored to either a data	#
# register or memory. The algorithm uses a standard "fmove" to create	#
# the rounded result. Also, since exceptions are disabled, this also	#
# create the correct OPERR default result if appropriate.		#
#	For sgl or dbl precision, overflow or underflow can occur. If	#
# either occurs and is enabled, the EXOP.				#
#	For extended precision, the stacked <ea> must be fixed along	#
# w/ the address index register as appropriate w/ _calc_ea_fout(). If	#
# the source is a denorm and if underflow is enabled, an EXOP must be	#
# created.								#
#	For packed, the k-factor must be fetched from the instruction	#
# word or a data register. The <ea> must be fixed as w/ extended	#
# precision. Then, bindec() is called to create the appropriate		#
# packed result.							#
#	If at any time an access error is flagged by one of the move-	#
# to-memory routines, then a special exit must be made so that the	#
# access error can be handled properly.					#
#									#
#########################################################################

	global		fout
fout:
	bfextu		EXC_CMDREG(%a6){&3:&3},%d1 # extract dst fmt
	mov.w		(tbl_fout.b,%pc,%d1.w*2),%a1 # use as index
	jmp		(tbl_fout.b,%pc,%a1)	# jump to routine

	swbeg		&0x8
tbl_fout:
	short		fout_long	-	tbl_fout
	short		fout_sgl	-	tbl_fout
	short		fout_ext	-	tbl_fout
	short		fout_pack	-	tbl_fout
	short		fout_word	-	tbl_fout
	short		fout_dbl	-	tbl_fout
	short		fout_byte	-	tbl_fout
	short		fout_pack	-	tbl_fout

#################################################################
# fmove.b out ###################################################
#################################################################

# Only "Unimplemented Data Type" exceptions enter here. The operand
# is either a DENORM or a NORM.
fout_byte:
	tst.b		STAG(%a6)		# is operand normalized?
	bne.b		fout_byte_denorm	# no

	fmovm.x		SRC(%a0),&0x80		# load value

fout_byte_norm:
	fmov.l		%d0,%fpcr		# insert rnd prec,mode

	fmov.b		%fp0,%d0		# exec move out w/ correct rnd mode

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d1		# fetch FPSR
	or.w		%d1,2+USER_FPSR(%a6)	# save new exc,accrued bits

	mov.b		1+EXC_OPWORD(%a6),%d1	# extract dst mode
	andi.b		&0x38,%d1		# is mode == 0? (Dreg dst)
	beq.b		fout_byte_dn		# must save to integer regfile

	mov.l		EXC_EA(%a6),%a0		# stacked <ea> is correct
	bsr.l		_dmem_write_byte	# write byte

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_b		# yes

	rts

fout_byte_dn:
	mov.b		1+EXC_OPWORD(%a6),%d1	# extract Dn
	andi.w		&0x7,%d1
	bsr.l		store_dreg_b
	rts

fout_byte_denorm:
	mov.l		SRC_EX(%a0),%d1
	andi.l		&0x80000000,%d1		# keep DENORM sign
	ori.l		&0x00800000,%d1		# make smallest sgl
	fmov.s		%d1,%fp0
	bra.b		fout_byte_norm

#################################################################
# fmove.w out ###################################################
#################################################################

# Only "Unimplemented Data Type" exceptions enter here. The operand
# is either a DENORM or a NORM.
fout_word:
	tst.b		STAG(%a6)		# is operand normalized?
	bne.b		fout_word_denorm	# no

	fmovm.x		SRC(%a0),&0x80		# load value

fout_word_norm:
	fmov.l		%d0,%fpcr		# insert rnd prec:mode

	fmov.w		%fp0,%d0		# exec move out w/ correct rnd mode

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d1		# fetch FPSR
	or.w		%d1,2+USER_FPSR(%a6)	# save new exc,accrued bits

	mov.b		1+EXC_OPWORD(%a6),%d1	# extract dst mode
	andi.b		&0x38,%d1		# is mode == 0? (Dreg dst)
	beq.b		fout_word_dn		# must save to integer regfile

	mov.l		EXC_EA(%a6),%a0		# stacked <ea> is correct
	bsr.l		_dmem_write_word	# write word

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_w		# yes

	rts

fout_word_dn:
	mov.b		1+EXC_OPWORD(%a6),%d1	# extract Dn
	andi.w		&0x7,%d1
	bsr.l		store_dreg_w
	rts

fout_word_denorm:
	mov.l		SRC_EX(%a0),%d1
	andi.l		&0x80000000,%d1		# keep DENORM sign
	ori.l		&0x00800000,%d1		# make smallest sgl
	fmov.s		%d1,%fp0
	bra.b		fout_word_norm

#################################################################
# fmove.l out ###################################################
#################################################################

# Only "Unimplemented Data Type" exceptions enter here. The operand
# is either a DENORM or a NORM.
fout_long:
	tst.b		STAG(%a6)		# is operand normalized?
	bne.b		fout_long_denorm	# no

	fmovm.x		SRC(%a0),&0x80		# load value

fout_long_norm:
	fmov.l		%d0,%fpcr		# insert rnd prec:mode

	fmov.l		%fp0,%d0		# exec move out w/ correct rnd mode

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d1		# fetch FPSR
	or.w		%d1,2+USER_FPSR(%a6)	# save new exc,accrued bits

fout_long_write:
	mov.b		1+EXC_OPWORD(%a6),%d1	# extract dst mode
	andi.b		&0x38,%d1		# is mode == 0? (Dreg dst)
	beq.b		fout_long_dn		# must save to integer regfile

	mov.l		EXC_EA(%a6),%a0		# stacked <ea> is correct
	bsr.l		_dmem_write_long	# write long

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_l		# yes

	rts

fout_long_dn:
	mov.b		1+EXC_OPWORD(%a6),%d1	# extract Dn
	andi.w		&0x7,%d1
	bsr.l		store_dreg_l
	rts

fout_long_denorm:
	mov.l		SRC_EX(%a0),%d1
	andi.l		&0x80000000,%d1		# keep DENORM sign
	ori.l		&0x00800000,%d1		# make smallest sgl
	fmov.s		%d1,%fp0
	bra.b		fout_long_norm

#################################################################
# fmove.x out ###################################################
#################################################################

# Only "Unimplemented Data Type" exceptions enter here. The operand
# is either a DENORM or a NORM.
# The DENORM causes an Underflow exception.
fout_ext:

# we copy the extended precision result to FP_SCR0 so that the reserved
# 16-bit field gets zeroed. we do this since we promise not to disturb
# what's at SRC(a0).
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	clr.w		2+FP_SCR0_EX(%a6)	# clear reserved field
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)

	fmovm.x		SRC(%a0),&0x80		# return result

	bsr.l		_calc_ea_fout		# fix stacked <ea>

	mov.l		%a0,%a1			# pass: dst addr
	lea		FP_SCR0(%a6),%a0	# pass: src addr
	mov.l		&0xc,%d0		# pass: opsize is 12 bytes

# we must not yet write the extended precision data to the stack
# in the pre-decrement case from supervisor mode or else we'll corrupt
# the stack frame. so, leave it in FP_SRC for now and deal with it later...
	cmpi.b		SPCOND_FLG(%a6),&mda7_flg
	beq.b		fout_ext_a7

	bsr.l		_dmem_write		# write ext prec number to memory

	tst.l		%d1			# did dstore fail?
	bne.w		fout_ext_err		# yes

	tst.b		STAG(%a6)		# is operand normalized?
	bne.b		fout_ext_denorm		# no
	rts

# the number is a DENORM. must set the underflow exception bit
fout_ext_denorm:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set underflow exc bit

	mov.b		FPCR_ENABLE(%a6),%d0
	andi.b		&0x0a,%d0		# is UNFL or INEX enabled?
	bne.b		fout_ext_exc		# yes
	rts

# we don't want to do the write if the exception occurred in supervisor mode
# so _mem_write2() handles this for us.
fout_ext_a7:
	bsr.l		_mem_write2		# write ext prec number to memory

	tst.l		%d1			# did dstore fail?
	bne.w		fout_ext_err		# yes

	tst.b		STAG(%a6)		# is operand normalized?
	bne.b		fout_ext_denorm		# no
	rts

fout_ext_exc:
	lea		FP_SCR0(%a6),%a0
	bsr.l		norm			# normalize the mantissa
	neg.w		%d0			# new exp = -(shft amt)
	andi.w		&0x7fff,%d0
	andi.w		&0x8000,FP_SCR0_EX(%a6)	# keep only old sign
	or.w		%d0,FP_SCR0_EX(%a6)	# insert new exponent
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	rts

fout_ext_err:
	mov.l		EXC_A6(%a6),(%a6)	# fix stacked a6
	bra.l		facc_out_x

#########################################################################
# fmove.s out ###########################################################
#########################################################################
fout_sgl:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&s_mode*0x10,%d0	# insert sgl prec
	mov.l		%d0,L_SCR3(%a6)		# save rnd prec,mode on stack

#
# operand is a normalized number. first, we check to see if the move out
# would cause either an underflow or overflow. these cases are handled
# separately. otherwise, set the FPCR to the proper rounding mode and
# execute the move.
#
	mov.w		SRC_EX(%a0),%d0		# extract exponent
	andi.w		&0x7fff,%d0		# strip sign

	cmpi.w		%d0,&SGL_HI		# will operand overflow?
	bgt.w		fout_sgl_ovfl		# yes; go handle OVFL
	beq.w		fout_sgl_may_ovfl	# maybe; go handle possible OVFL
	cmpi.w		%d0,&SGL_LO		# will operand underflow?
	blt.w		fout_sgl_unfl		# yes; go handle underflow

#
# NORMs(in range) can be stored out by a simple "fmov.s"
# Unnormalized inputs can come through this point.
#
fout_sgl_exg:
	fmovm.x		SRC(%a0),&0x80		# fetch fop from stack

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fmov.s		%fp0,%d0		# store does convert and round

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d1		# save FPSR

	or.w		%d1,2+USER_FPSR(%a6)	# set possible inex2/ainex

fout_sgl_exg_write:
	mov.b		1+EXC_OPWORD(%a6),%d1	# extract dst mode
	andi.b		&0x38,%d1		# is mode == 0? (Dreg dst)
	beq.b		fout_sgl_exg_write_dn	# must save to integer regfile

	mov.l		EXC_EA(%a6),%a0		# stacked <ea> is correct
	bsr.l		_dmem_write_long	# write long

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_l		# yes

	rts

fout_sgl_exg_write_dn:
	mov.b		1+EXC_OPWORD(%a6),%d1	# extract Dn
	andi.w		&0x7,%d1
	bsr.l		store_dreg_l
	rts

#
# here, we know that the operand would UNFL if moved out to single prec,
# so, denorm and round and then use generic store single routine to
# write the value to memory.
#
fout_sgl_unfl:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set UNFL

	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	mov.l		%a0,-(%sp)

	clr.l		%d0			# pass: S.F. = 0

	cmpi.b		STAG(%a6),&DENORM	# fetch src optype tag
	bne.b		fout_sgl_unfl_cont	# let DENORMs fall through

	lea		FP_SCR0(%a6),%a0
	bsr.l		norm			# normalize the DENORM

fout_sgl_unfl_cont:
	lea		FP_SCR0(%a6),%a0	# pass: ptr to operand
	mov.l		L_SCR3(%a6),%d1		# pass: rnd prec,mode
	bsr.l		unf_res			# calc default underflow result

	lea		FP_SCR0(%a6),%a0	# pass: ptr to fop
	bsr.l		dst_sgl			# convert to single prec

	mov.b		1+EXC_OPWORD(%a6),%d1	# extract dst mode
	andi.b		&0x38,%d1		# is mode == 0? (Dreg dst)
	beq.b		fout_sgl_unfl_dn	# must save to integer regfile

	mov.l		EXC_EA(%a6),%a0		# stacked <ea> is correct
	bsr.l		_dmem_write_long	# write long

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_l		# yes

	bra.b		fout_sgl_unfl_chkexc

fout_sgl_unfl_dn:
	mov.b		1+EXC_OPWORD(%a6),%d1	# extract Dn
	andi.w		&0x7,%d1
	bsr.l		store_dreg_l

fout_sgl_unfl_chkexc:
	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0a,%d1		# is UNFL or INEX enabled?
	bne.w		fout_sd_exc_unfl	# yes
	addq.l		&0x4,%sp
	rts

#
# it's definitely an overflow so call ovf_res to get the correct answer
#
fout_sgl_ovfl:
	tst.b		3+SRC_HI(%a0)		# is result inexact?
	bne.b		fout_sgl_ovfl_inex2
	tst.l		SRC_LO(%a0)		# is result inexact?
	bne.b		fout_sgl_ovfl_inex2
	ori.w		&ovfl_inx_mask,2+USER_FPSR(%a6) # set ovfl/aovfl/ainex
	bra.b		fout_sgl_ovfl_cont
fout_sgl_ovfl_inex2:
	ori.w		&ovfinx_mask,2+USER_FPSR(%a6) # set ovfl/aovfl/ainex/inex2

fout_sgl_ovfl_cont:
	mov.l		%a0,-(%sp)

# call ovf_res() w/ sgl prec and the correct rnd mode to create the default
# overflow result. DON'T save the returned ccodes from ovf_res() since
# fmove out doesn't alter them.
	tst.b		SRC_EX(%a0)		# is operand negative?
	smi		%d1			# set if so
	mov.l		L_SCR3(%a6),%d0		# pass: sgl prec,rnd mode
	bsr.l		ovf_res			# calc OVFL result
	fmovm.x		(%a0),&0x80		# load default overflow result
	fmov.s		%fp0,%d0		# store to single

	mov.b		1+EXC_OPWORD(%a6),%d1	# extract dst mode
	andi.b		&0x38,%d1		# is mode == 0? (Dreg dst)
	beq.b		fout_sgl_ovfl_dn	# must save to integer regfile

	mov.l		EXC_EA(%a6),%a0		# stacked <ea> is correct
	bsr.l		_dmem_write_long	# write long

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_l		# yes

	bra.b		fout_sgl_ovfl_chkexc

fout_sgl_ovfl_dn:
	mov.b		1+EXC_OPWORD(%a6),%d1	# extract Dn
	andi.w		&0x7,%d1
	bsr.l		store_dreg_l

fout_sgl_ovfl_chkexc:
	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0a,%d1		# is UNFL or INEX enabled?
	bne.w		fout_sd_exc_ovfl	# yes
	addq.l		&0x4,%sp
	rts

#
# move out MAY overflow:
# (1) force the exp to 0x3fff
# (2) do a move w/ appropriate rnd mode
# (3) if exp still equals zero, then insert original exponent
#	for the correct result.
#     if exp now equals one, then it overflowed so call ovf_res.
#
fout_sgl_may_ovfl:
	mov.w		SRC_EX(%a0),%d1		# fetch current sign
	andi.w		&0x8000,%d1		# keep it,clear exp
	ori.w		&0x3fff,%d1		# insert exp = 0
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert scaled exp
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6) # copy hi(man)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6) # copy lo(man)

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fmov.x		FP_SCR0(%a6),%fp0	# force fop to be rounded
	fmov.l		&0x0,%fpcr		# clear FPCR

	fabs.x		%fp0			# need absolute value
	fcmp.b		%fp0,&0x2		# did exponent increase?
	fblt.w		fout_sgl_exg		# no; go finish NORM
	bra.w		fout_sgl_ovfl		# yes; go handle overflow

################

fout_sd_exc_unfl:
	mov.l		(%sp)+,%a0

	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)

	cmpi.b		STAG(%a6),&DENORM	# was src a DENORM?
	bne.b		fout_sd_exc_cont	# no

	lea		FP_SCR0(%a6),%a0
	bsr.l		norm
	neg.l		%d0
	andi.w		&0x7fff,%d0
	bfins		%d0,FP_SCR0_EX(%a6){&1:&15}
	bra.b		fout_sd_exc_cont

fout_sd_exc:
fout_sd_exc_ovfl:
	mov.l		(%sp)+,%a0		# restore a0

	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)

fout_sd_exc_cont:
	bclr		&0x7,FP_SCR0_EX(%a6)	# clear sign bit
	sne.b		2+FP_SCR0_EX(%a6)	# set internal sign bit
	lea		FP_SCR0(%a6),%a0	# pass: ptr to DENORM

	mov.b		3+L_SCR3(%a6),%d1
	lsr.b		&0x4,%d1
	andi.w		&0x0c,%d1
	swap		%d1
	mov.b		3+L_SCR3(%a6),%d1
	lsr.b		&0x4,%d1
	andi.w		&0x03,%d1
	clr.l		%d0			# pass: zero g,r,s
	bsr.l		_round			# round the DENORM

	tst.b		2+FP_SCR0_EX(%a6)	# is EXOP negative?
	beq.b		fout_sd_exc_done	# no
	bset		&0x7,FP_SCR0_EX(%a6)	# yes

fout_sd_exc_done:
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	rts

#################################################################
# fmove.d out ###################################################
#################################################################
fout_dbl:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&d_mode*0x10,%d0	# insert dbl prec
	mov.l		%d0,L_SCR3(%a6)		# save rnd prec,mode on stack

#
# operand is a normalized number. first, we check to see if the move out
# would cause either an underflow or overflow. these cases are handled
# separately. otherwise, set the FPCR to the proper rounding mode and
# execute the move.
#
	mov.w		SRC_EX(%a0),%d0		# extract exponent
	andi.w		&0x7fff,%d0		# strip sign

	cmpi.w		%d0,&DBL_HI		# will operand overflow?
	bgt.w		fout_dbl_ovfl		# yes; go handle OVFL
	beq.w		fout_dbl_may_ovfl	# maybe; go handle possible OVFL
	cmpi.w		%d0,&DBL_LO		# will operand underflow?
	blt.w		fout_dbl_unfl		# yes; go handle underflow

#
# NORMs(in range) can be stored out by a simple "fmov.d"
# Unnormalized inputs can come through this point.
#
fout_dbl_exg:
	fmovm.x		SRC(%a0),&0x80		# fetch fop from stack

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fmov.d		%fp0,L_SCR1(%a6)	# store does convert and round

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d0		# save FPSR

	or.w		%d0,2+USER_FPSR(%a6)	# set possible inex2/ainex

	mov.l		EXC_EA(%a6),%a1		# pass: dst addr
	lea		L_SCR1(%a6),%a0		# pass: src addr
	movq.l		&0x8,%d0		# pass: opsize is 8 bytes
	bsr.l		_dmem_write		# store dbl fop to memory

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_d		# yes

	rts					# no; so we're finished

#
# here, we know that the operand would UNFL if moved out to double prec,
# so, denorm and round and then use generic store double routine to
# write the value to memory.
#
fout_dbl_unfl:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set UNFL

	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	mov.l		%a0,-(%sp)

	clr.l		%d0			# pass: S.F. = 0

	cmpi.b		STAG(%a6),&DENORM	# fetch src optype tag
	bne.b		fout_dbl_unfl_cont	# let DENORMs fall through

	lea		FP_SCR0(%a6),%a0
	bsr.l		norm			# normalize the DENORM

fout_dbl_unfl_cont:
	lea		FP_SCR0(%a6),%a0	# pass: ptr to operand
	mov.l		L_SCR3(%a6),%d1		# pass: rnd prec,mode
	bsr.l		unf_res			# calc default underflow result

	lea		FP_SCR0(%a6),%a0	# pass: ptr to fop
	bsr.l		dst_dbl			# convert to single prec
	mov.l		%d0,L_SCR1(%a6)
	mov.l		%d1,L_SCR2(%a6)

	mov.l		EXC_EA(%a6),%a1		# pass: dst addr
	lea		L_SCR1(%a6),%a0		# pass: src addr
	movq.l		&0x8,%d0		# pass: opsize is 8 bytes
	bsr.l		_dmem_write		# store dbl fop to memory

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_d		# yes

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0a,%d1		# is UNFL or INEX enabled?
	bne.w		fout_sd_exc_unfl	# yes
	addq.l		&0x4,%sp
	rts

#
# it's definitely an overflow so call ovf_res to get the correct answer
#
fout_dbl_ovfl:
	mov.w		2+SRC_LO(%a0),%d0
	andi.w		&0x7ff,%d0
	bne.b		fout_dbl_ovfl_inex2

	ori.w		&ovfl_inx_mask,2+USER_FPSR(%a6) # set ovfl/aovfl/ainex
	bra.b		fout_dbl_ovfl_cont
fout_dbl_ovfl_inex2:
	ori.w		&ovfinx_mask,2+USER_FPSR(%a6) # set ovfl/aovfl/ainex/inex2

fout_dbl_ovfl_cont:
	mov.l		%a0,-(%sp)

# call ovf_res() w/ dbl prec and the correct rnd mode to create the default
# overflow result. DON'T save the returned ccodes from ovf_res() since
# fmove out doesn't alter them.
	tst.b		SRC_EX(%a0)		# is operand negative?
	smi		%d1			# set if so
	mov.l		L_SCR3(%a6),%d0		# pass: dbl prec,rnd mode
	bsr.l		ovf_res			# calc OVFL result
	fmovm.x		(%a0),&0x80		# load default overflow result
	fmov.d		%fp0,L_SCR1(%a6)	# store to double

	mov.l		EXC_EA(%a6),%a1		# pass: dst addr
	lea		L_SCR1(%a6),%a0		# pass: src addr
	movq.l		&0x8,%d0		# pass: opsize is 8 bytes
	bsr.l		_dmem_write		# store dbl fop to memory

	tst.l		%d1			# did dstore fail?
	bne.l		facc_out_d		# yes

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0a,%d1		# is UNFL or INEX enabled?
	bne.w		fout_sd_exc_ovfl	# yes
	addq.l		&0x4,%sp
	rts

#
# move out MAY overflow:
# (1) force the exp to 0x3fff
# (2) do a move w/ appropriate rnd mode
# (3) if exp still equals zero, then insert original exponent
#	for the correct result.
#     if exp now equals one, then it overflowed so call ovf_res.
#
fout_dbl_may_ovfl:
	mov.w		SRC_EX(%a0),%d1		# fetch current sign
	andi.w		&0x8000,%d1		# keep it,clear exp
	ori.w		&0x3fff,%d1		# insert exp = 0
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert scaled exp
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6) # copy hi(man)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6) # copy lo(man)

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fmov.x		FP_SCR0(%a6),%fp0	# force fop to be rounded
	fmov.l		&0x0,%fpcr		# clear FPCR

	fabs.x		%fp0			# need absolute value
	fcmp.b		%fp0,&0x2		# did exponent increase?
	fblt.w		fout_dbl_exg		# no; go finish NORM
	bra.w		fout_dbl_ovfl		# yes; go handle overflow

#########################################################################
# XDEF ****************************************************************	#
#	dst_dbl(): create double precision value from extended prec.	#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to source operand in extended precision		#
#									#
# OUTPUT **************************************************************	#
#	d0 = hi(double precision result)				#
#	d1 = lo(double precision result)				#
#									#
# ALGORITHM ***********************************************************	#
#									#
#  Changes extended precision to double precision.			#
#  Note: no attempt is made to round the extended value to double.	#
#	dbl_sign = ext_sign						#
#	dbl_exp = ext_exp - $3fff(ext bias) + $7ff(dbl bias)		#
#	get rid of ext integer bit					#
#	dbl_mant = ext_mant{62:12}					#
#									#
#		---------------   ---------------    ---------------	#
#  extended ->  |s|    exp    |   |1| ms mant   |    | ls mant     |	#
#		---------------   ---------------    ---------------	#
#		 95	    64    63 62	      32      31     11	  0	#
#				     |			     |		#
#				     |			     |		#
#				     |			     |		#
#			             v			     v		#
#			      ---------------   ---------------		#
#  double   ->		      |s|exp| mant  |   |  mant       |		#
#			      ---------------   ---------------		#
#			      63     51   32   31	       0	#
#									#
#########################################################################

dst_dbl:
	clr.l		%d0			# clear d0
	mov.w		FTEMP_EX(%a0),%d0	# get exponent
	subi.w		&EXT_BIAS,%d0		# subtract extended precision bias
	addi.w		&DBL_BIAS,%d0		# add double precision bias
	tst.b		FTEMP_HI(%a0)		# is number a denorm?
	bmi.b		dst_get_dupper		# no
	subq.w		&0x1,%d0		# yes; denorm bias = DBL_BIAS - 1
dst_get_dupper:
	swap		%d0			# d0 now in upper word
	lsl.l		&0x4,%d0		# d0 in proper place for dbl prec exp
	tst.b		FTEMP_EX(%a0)		# test sign
	bpl.b		dst_get_dman		# if postive, go process mantissa
	bset		&0x1f,%d0		# if negative, set sign
dst_get_dman:
	mov.l		FTEMP_HI(%a0),%d1	# get ms mantissa
	bfextu		%d1{&1:&20},%d1		# get upper 20 bits of ms
	or.l		%d1,%d0			# put these bits in ms word of double
	mov.l		%d0,L_SCR1(%a6)		# put the new exp back on the stack
	mov.l		FTEMP_HI(%a0),%d1	# get ms mantissa
	mov.l		&21,%d0			# load shift count
	lsl.l		%d0,%d1			# put lower 11 bits in upper bits
	mov.l		%d1,L_SCR2(%a6)		# build lower lword in memory
	mov.l		FTEMP_LO(%a0),%d1	# get ls mantissa
	bfextu		%d1{&0:&21},%d0		# get ls 21 bits of double
	mov.l		L_SCR2(%a6),%d1
	or.l		%d0,%d1			# put them in double result
	mov.l		L_SCR1(%a6),%d0
	rts

#########################################################################
# XDEF ****************************************************************	#
#	dst_sgl(): create single precision value from extended prec	#
#									#
# XREF ****************************************************************	#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to source operand in extended precision		#
#									#
# OUTPUT **************************************************************	#
#	d0 = single precision result					#
#									#
# ALGORITHM ***********************************************************	#
#									#
# Changes extended precision to single precision.			#
#	sgl_sign = ext_sign						#
#	sgl_exp = ext_exp - $3fff(ext bias) + $7f(sgl bias)		#
#	get rid of ext integer bit					#
#	sgl_mant = ext_mant{62:12}					#
#									#
#		---------------   ---------------    ---------------	#
#  extended ->  |s|    exp    |   |1| ms mant   |    | ls mant     |	#
#		---------------   ---------------    ---------------	#
#		 95	    64    63 62	   40 32      31     12	  0	#
#				     |	   |				#
#				     |	   |				#
#				     |	   |				#
#			             v     v				#
#			      ---------------				#
#  single   ->		      |s|exp| mant  |				#
#			      ---------------				#
#			      31     22     0				#
#									#
#########################################################################

dst_sgl:
	clr.l		%d0
	mov.w		FTEMP_EX(%a0),%d0	# get exponent
	subi.w		&EXT_BIAS,%d0		# subtract extended precision bias
	addi.w		&SGL_BIAS,%d0		# add single precision bias
	tst.b		FTEMP_HI(%a0)		# is number a denorm?
	bmi.b		dst_get_supper		# no
	subq.w		&0x1,%d0		# yes; denorm bias = SGL_BIAS - 1
dst_get_supper:
	swap		%d0			# put exp in upper word of d0
	lsl.l		&0x7,%d0		# shift it into single exp bits
	tst.b		FTEMP_EX(%a0)		# test sign
	bpl.b		dst_get_sman		# if positive, continue
	bset		&0x1f,%d0		# if negative, put in sign first
dst_get_sman:
	mov.l		FTEMP_HI(%a0),%d1	# get ms mantissa
	andi.l		&0x7fffff00,%d1		# get upper 23 bits of ms
	lsr.l		&0x8,%d1		# and put them flush right
	or.l		%d1,%d0			# put these bits in ms word of single
	rts

##############################################################################
fout_pack:
	bsr.l		_calc_ea_fout		# fetch the <ea>
	mov.l		%a0,-(%sp)

	mov.b		STAG(%a6),%d0		# fetch input type
	bne.w		fout_pack_not_norm	# input is not NORM

fout_pack_norm:
	btst		&0x4,EXC_CMDREG(%a6)	# static or dynamic?
	beq.b		fout_pack_s		# static

fout_pack_d:
	mov.b		1+EXC_CMDREG(%a6),%d1	# fetch dynamic reg
	lsr.b		&0x4,%d1
	andi.w		&0x7,%d1

	bsr.l		fetch_dreg		# fetch Dn w/ k-factor

	bra.b		fout_pack_type
fout_pack_s:
	mov.b		1+EXC_CMDREG(%a6),%d0	# fetch static field

fout_pack_type:
	bfexts		%d0{&25:&7},%d0		# extract k-factor
	mov.l	%d0,-(%sp)

	lea		FP_SRC(%a6),%a0		# pass: ptr to input

# bindec is currently scrambling FP_SRC for denorm inputs.
# we'll have to change this, but for now, tough luck!!!
	bsr.l		bindec			# convert xprec to packed

#	andi.l		&0xcfff000f,FP_SCR0(%a6) # clear unused fields
	andi.l		&0xcffff00f,FP_SCR0(%a6) # clear unused fields

	mov.l	(%sp)+,%d0

	tst.b		3+FP_SCR0_EX(%a6)
	bne.b		fout_pack_set
	tst.l		FP_SCR0_HI(%a6)
	bne.b		fout_pack_set
	tst.l		FP_SCR0_LO(%a6)
	bne.b		fout_pack_set

# add the extra condition that only if the k-factor was zero, too, should
# we zero the exponent
	tst.l		%d0
	bne.b		fout_pack_set
# "mantissa" is all zero which means that the answer is zero. but, the '040
# algorithm allows the exponent to be non-zero. the 881/2 do not. therefore,
# if the mantissa is zero, I will zero the exponent, too.
# the question now is whether the exponents sign bit is allowed to be non-zero
# for a zero, also...
	andi.w		&0xf000,FP_SCR0(%a6)

fout_pack_set:

	lea		FP_SCR0(%a6),%a0	# pass: src addr

fout_pack_write:
	mov.l		(%sp)+,%a1		# pass: dst addr
	mov.l		&0xc,%d0		# pass: opsize is 12 bytes

	cmpi.b		SPCOND_FLG(%a6),&mda7_flg
	beq.b		fout_pack_a7

	bsr.l		_dmem_write		# write ext prec number to memory

	tst.l		%d1			# did dstore fail?
	bne.w		fout_ext_err		# yes

	rts

# we don't want to do the write if the exception occurred in supervisor mode
# so _mem_write2() handles this for us.
fout_pack_a7:
	bsr.l		_mem_write2		# write ext prec number to memory

	tst.l		%d1			# did dstore fail?
	bne.w		fout_ext_err		# yes

	rts

fout_pack_not_norm:
	cmpi.b		%d0,&DENORM		# is it a DENORM?
	beq.w		fout_pack_norm		# yes
	lea		FP_SRC(%a6),%a0
	clr.w		2+FP_SRC_EX(%a6)
	cmpi.b		%d0,&SNAN		# is it an SNAN?
	beq.b		fout_pack_snan		# yes
	bra.b		fout_pack_write		# no

fout_pack_snan:
	ori.w		&snaniop2_mask,FPSR_EXCEPT(%a6) # set SNAN/AIOP
	bset		&0x6,FP_SRC_HI(%a6)	# set snan bit
	bra.b		fout_pack_write

#########################################################################
# XDEF ****************************************************************	#
#	fmul(): emulates the fmul instruction				#
#	fsmul(): emulates the fsmul instruction				#
#	fdmul(): emulates the fdmul instruction				#
#									#
# XREF ****************************************************************	#
#	scale_to_zero_src() - scale src exponent to zero		#
#	scale_to_zero_dst() - scale dst exponent to zero		#
#	unf_res() - return default underflow result			#
#	ovf_res() - return default overflow result			#
#	res_qnan() - return QNAN result					#
#	res_snan() - return SNAN result					#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	a1 = pointer to extended precision destination operand		#
#	d0  rnd prec,mode						#
#									#
# OUTPUT **************************************************************	#
#	fp0 = result							#
#	fp1 = EXOP (if exception occurred)				#
#									#
# ALGORITHM ***********************************************************	#
#	Handle NANs, infinities, and zeroes as special cases. Divide	#
# norms/denorms into ext/sgl/dbl precision.				#
#	For norms/denorms, scale the exponents such that a multiply	#
# instruction won't cause an exception. Use the regular fmul to		#
# compute a result. Check if the regular operands would have taken	#
# an exception. If so, return the default overflow/underflow result	#
# and return the EXOP if exceptions are enabled. Else, scale the	#
# result operand to the proper exponent.				#
#									#
#########################################################################

	align		0x10
tbl_fmul_ovfl:
	long		0x3fff - 0x7ffe		# ext_max
	long		0x3fff - 0x407e		# sgl_max
	long		0x3fff - 0x43fe		# dbl_max
tbl_fmul_unfl:
	long		0x3fff + 0x0001		# ext_unfl
	long		0x3fff - 0x3f80		# sgl_unfl
	long		0x3fff - 0x3c00		# dbl_unfl

	global		fsmul
fsmul:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&s_mode*0x10,%d0	# insert sgl prec
	bra.b		fmul

	global		fdmul
fdmul:
	andi.b		&0x30,%d0
	ori.b		&d_mode*0x10,%d0	# insert dbl prec

	global		fmul
fmul:
	mov.l		%d0,L_SCR3(%a6)		# store rnd info

	clr.w		%d1
	mov.b		DTAG(%a6),%d1
	lsl.b		&0x3,%d1
	or.b		STAG(%a6),%d1		# combine src tags
	bne.w		fmul_not_norm		# optimize on non-norm input

fmul_norm:
	mov.w		DST_EX(%a1),FP_SCR1_EX(%a6)
	mov.l		DST_HI(%a1),FP_SCR1_HI(%a6)
	mov.l		DST_LO(%a1),FP_SCR1_LO(%a6)

	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)

	bsr.l		scale_to_zero_src	# scale src exponent
	mov.l		%d0,-(%sp)		# save scale factor 1

	bsr.l		scale_to_zero_dst	# scale dst exponent

	add.l		%d0,(%sp)		# SCALE_FACTOR = scale1 + scale2

	mov.w		2+L_SCR3(%a6),%d1	# fetch precision
	lsr.b		&0x6,%d1		# shift to lo bits
	mov.l		(%sp)+,%d0		# load S.F.
	cmp.l		%d0,(tbl_fmul_ovfl.w,%pc,%d1.w*4) # would result ovfl?
	beq.w		fmul_may_ovfl		# result may rnd to overflow
	blt.w		fmul_ovfl		# result will overflow

	cmp.l		%d0,(tbl_fmul_unfl.w,%pc,%d1.w*4) # would result unfl?
	beq.w		fmul_may_unfl		# result may rnd to no unfl
	bgt.w		fmul_unfl		# result will underflow

#
# NORMAL:
# - the result of the multiply operation will neither overflow nor underflow.
# - do the multiply to the proper precision and rounding mode.
# - scale the result exponent using the scale factor. if both operands were
# normalized then we really don't need to go through this scaling. but for now,
# this will do.
#
fmul_normal:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst operand

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fmul.x		FP_SCR0(%a6),%fp0	# execute multiply

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fmul_normal_exit:
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result
	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# load {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	or.w		%d2,%d1			# concat old sign,new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0
	rts

#
# OVERFLOW:
# - the result of the multiply operation is an overflow.
# - do the multiply to the proper precision and rounding mode in order to
# set the inexact bits.
# - calculate the default result and return it in fp0.
# - if overflow or inexact is enabled, we need a multiply result rounded to
# extended precision. if the original operation was extended, then we have this
# result. if the original operation was single or double, we have to do another
# multiply using extended precision and the correct rounding mode. the result
# of this operation then has its exponent scaled by -0x6000 to create the
# exceptional operand.
#
fmul_ovfl:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst operand

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fmul.x		FP_SCR0(%a6),%fp0	# execute multiply

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

# save setting this until now because this is where fmul_may_ovfl may jump in
fmul_ovfl_tst:
	or.l		&ovfl_inx_mask,USER_FPSR(%a6) # set ovfl/aovfl/ainex

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x13,%d1		# is OVFL or INEX enabled?
	bne.b		fmul_ovfl_ena		# yes

# calculate the default result
fmul_ovfl_dis:
	btst		&neg_bit,FPSR_CC(%a6)	# is result negative?
	sne		%d1			# set sign param accordingly
	mov.l		L_SCR3(%a6),%d0		# pass rnd prec,mode
	bsr.l		ovf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# set INF,N if applicable
	fmovm.x		(%a0),&0x80		# return default result in fp0
	rts

#
# OVFL is enabled; Create EXOP:
# - if precision is extended, then we have the EXOP. simply bias the exponent
# with an extra -0x6000. if the precision is single or double, we need to
# calculate a result rounded to extended precision.
#
fmul_ovfl_ena:
	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# test the rnd prec
	bne.b		fmul_ovfl_ena_sd	# it's sgl or dbl

fmul_ovfl_ena_cont:
	fmovm.x		&0x80,FP_SCR0(%a6)	# move result to stack

	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.w		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	sub.l		%d0,%d1			# add scale factor
	subi.l		&0x6000,%d1		# subtract bias
	andi.w		&0x7fff,%d1		# clear sign bit
	andi.w		&0x8000,%d2		# keep old sign
	or.w		%d2,%d1			# concat old sign,new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	bra.b		fmul_ovfl_dis

fmul_ovfl_ena_sd:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst operand

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0x30,%d1		# keep rnd mode only
	fmov.l		%d1,%fpcr		# set FPCR

	fmul.x		FP_SCR0(%a6),%fp0	# execute multiply

	fmov.l		&0x0,%fpcr		# clear FPCR
	bra.b		fmul_ovfl_ena_cont

#
# may OVERFLOW:
# - the result of the multiply operation MAY overflow.
# - do the multiply to the proper precision and rounding mode in order to
# set the inexact bits.
# - calculate the default result and return it in fp0.
#
fmul_may_ovfl:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fmul.x		FP_SCR0(%a6),%fp0	# execute multiply

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	fabs.x		%fp0,%fp1		# make a copy of result
	fcmp.b		%fp1,&0x2		# is |result| >= 2.b?
	fbge.w		fmul_ovfl_tst		# yes; overflow has occurred

# no, it didn't overflow; we have correct result
	bra.w		fmul_normal_exit

#
# UNDERFLOW:
# - the result of the multiply operation is an underflow.
# - do the multiply to the proper precision and rounding mode in order to
# set the inexact bits.
# - calculate the default result and return it in fp0.
# - if overflow or inexact is enabled, we need a multiply result rounded to
# extended precision. if the original operation was extended, then we have this
# result. if the original operation was single or double, we have to do another
# multiply using extended precision and the correct rounding mode. the result
# of this operation then has its exponent scaled by -0x6000 to create the
# exceptional operand.
#
fmul_unfl:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit

# for fun, let's use only extended precision, round to zero. then, let
# the unf_res() routine figure out all the rest.
# will we get the correct answer.
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst operand

	fmov.l		&rz_mode*0x10,%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fmul.x		FP_SCR0(%a6),%fp0	# execute multiply

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0b,%d1		# is UNFL or INEX enabled?
	bne.b		fmul_unfl_ena		# yes

fmul_unfl_dis:
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result

	lea		FP_SCR0(%a6),%a0	# pass: result addr
	mov.l		L_SCR3(%a6),%d1		# pass: rnd prec,mode
	bsr.l		unf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# unf_res2 may have set 'Z'
	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0
	rts

#
# UNFL is enabled.
#
fmul_unfl_ena:
	fmovm.x		FP_SCR1(%a6),&0x40	# load dst op

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# is precision extended?
	bne.b		fmul_unfl_ena_sd	# no, sgl or dbl

# if the rnd mode is anything but RZ, then we have to re-do the above
# multiplication becuase we used RZ for all.
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

fmul_unfl_ena_cont:
	fmov.l		&0x0,%fpsr		# clear FPSR

	fmul.x		FP_SCR0(%a6),%fp1	# execute multiply

	fmov.l		&0x0,%fpcr		# clear FPCR

	fmovm.x		&0x40,FP_SCR0(%a6)	# save result to stack
	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	addi.l		&0x6000,%d1		# add bias
	andi.w		&0x7fff,%d1
	or.w		%d2,%d1			# concat old sign,new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	bra.w		fmul_unfl_dis

fmul_unfl_ena_sd:
	mov.l		L_SCR3(%a6),%d1
	andi.b		&0x30,%d1		# use only rnd mode
	fmov.l		%d1,%fpcr		# set FPCR

	bra.b		fmul_unfl_ena_cont

# MAY UNDERFLOW:
# -use the correct rounding mode and precision. this code favors operations
# that do not underflow.
fmul_may_unfl:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst operand

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fmul.x		FP_SCR0(%a6),%fp0	# execute multiply

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	fabs.x		%fp0,%fp1		# make a copy of result
	fcmp.b		%fp1,&0x2		# is |result| > 2.b?
	fbgt.w		fmul_normal_exit	# no; no underflow occurred
	fblt.w		fmul_unfl		# yes; underflow occurred

#
# we still don't know if underflow occurred. result is ~ equal to 2. but,
# we don't know if the result was an underflow that rounded up to a 2 or
# a normalized number that rounded down to a 2. so, redo the entire operation
# using RZ as the rounding mode to see what the pre-rounded result is.
# this case should be relatively rare.
#
	fmovm.x		FP_SCR1(%a6),&0x40	# load dst operand

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# keep rnd prec
	ori.b		&rz_mode*0x10,%d1	# insert RZ

	fmov.l		%d1,%fpcr		# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fmul.x		FP_SCR0(%a6),%fp1	# execute multiply

	fmov.l		&0x0,%fpcr		# clear FPCR
	fabs.x		%fp1			# make absolute value
	fcmp.b		%fp1,&0x2		# is |result| < 2.b?
	fbge.w		fmul_normal_exit	# no; no underflow occurred
	bra.w		fmul_unfl		# yes, underflow occurred

################################################################################

#
# Multiply: inputs are not both normalized; what are they?
#
fmul_not_norm:
	mov.w		(tbl_fmul_op.b,%pc,%d1.w*2),%d1
	jmp		(tbl_fmul_op.b,%pc,%d1.w)

	swbeg		&48
tbl_fmul_op:
	short		fmul_norm	- tbl_fmul_op # NORM x NORM
	short		fmul_zero	- tbl_fmul_op # NORM x ZERO
	short		fmul_inf_src	- tbl_fmul_op # NORM x INF
	short		fmul_res_qnan	- tbl_fmul_op # NORM x QNAN
	short		fmul_norm	- tbl_fmul_op # NORM x DENORM
	short		fmul_res_snan	- tbl_fmul_op # NORM x SNAN
	short		tbl_fmul_op	- tbl_fmul_op #
	short		tbl_fmul_op	- tbl_fmul_op #

	short		fmul_zero	- tbl_fmul_op # ZERO x NORM
	short		fmul_zero	- tbl_fmul_op # ZERO x ZERO
	short		fmul_res_operr	- tbl_fmul_op # ZERO x INF
	short		fmul_res_qnan	- tbl_fmul_op # ZERO x QNAN
	short		fmul_zero	- tbl_fmul_op # ZERO x DENORM
	short		fmul_res_snan	- tbl_fmul_op # ZERO x SNAN
	short		tbl_fmul_op	- tbl_fmul_op #
	short		tbl_fmul_op	- tbl_fmul_op #

	short		fmul_inf_dst	- tbl_fmul_op # INF x NORM
	short		fmul_res_operr	- tbl_fmul_op # INF x ZERO
	short		fmul_inf_dst	- tbl_fmul_op # INF x INF
	short		fmul_res_qnan	- tbl_fmul_op # INF x QNAN
	short		fmul_inf_dst	- tbl_fmul_op # INF x DENORM
	short		fmul_res_snan	- tbl_fmul_op # INF x SNAN
	short		tbl_fmul_op	- tbl_fmul_op #
	short		tbl_fmul_op	- tbl_fmul_op #

	short		fmul_res_qnan	- tbl_fmul_op # QNAN x NORM
	short		fmul_res_qnan	- tbl_fmul_op # QNAN x ZERO
	short		fmul_res_qnan	- tbl_fmul_op # QNAN x INF
	short		fmul_res_qnan	- tbl_fmul_op # QNAN x QNAN
	short		fmul_res_qnan	- tbl_fmul_op # QNAN x DENORM
	short		fmul_res_snan	- tbl_fmul_op # QNAN x SNAN
	short		tbl_fmul_op	- tbl_fmul_op #
	short		tbl_fmul_op	- tbl_fmul_op #

	short		fmul_norm	- tbl_fmul_op # NORM x NORM
	short		fmul_zero	- tbl_fmul_op # NORM x ZERO
	short		fmul_inf_src	- tbl_fmul_op # NORM x INF
	short		fmul_res_qnan	- tbl_fmul_op # NORM x QNAN
	short		fmul_norm	- tbl_fmul_op # NORM x DENORM
	short		fmul_res_snan	- tbl_fmul_op # NORM x SNAN
	short		tbl_fmul_op	- tbl_fmul_op #
	short		tbl_fmul_op	- tbl_fmul_op #

	short		fmul_res_snan	- tbl_fmul_op # SNAN x NORM
	short		fmul_res_snan	- tbl_fmul_op # SNAN x ZERO
	short		fmul_res_snan	- tbl_fmul_op # SNAN x INF
	short		fmul_res_snan	- tbl_fmul_op # SNAN x QNAN
	short		fmul_res_snan	- tbl_fmul_op # SNAN x DENORM
	short		fmul_res_snan	- tbl_fmul_op # SNAN x SNAN
	short		tbl_fmul_op	- tbl_fmul_op #
	short		tbl_fmul_op	- tbl_fmul_op #

fmul_res_operr:
	bra.l		res_operr
fmul_res_snan:
	bra.l		res_snan
fmul_res_qnan:
	bra.l		res_qnan

#
# Multiply: (Zero x Zero) || (Zero x norm) || (Zero x denorm)
#
	global		fmul_zero		# global for fsglmul
fmul_zero:
	mov.b		SRC_EX(%a0),%d0		# exclusive or the signs
	mov.b		DST_EX(%a1),%d1
	eor.b		%d0,%d1
	bpl.b		fmul_zero_p		# result ZERO is pos.
fmul_zero_n:
	fmov.s		&0x80000000,%fp0	# load -ZERO
	mov.b		&z_bmask+neg_bmask,FPSR_CC(%a6) # set Z/N
	rts
fmul_zero_p:
	fmov.s		&0x00000000,%fp0	# load +ZERO
	mov.b		&z_bmask,FPSR_CC(%a6)	# set Z
	rts

#
# Multiply: (inf x inf) || (inf x norm) || (inf x denorm)
#
# Note: The j-bit for an infinity is a don't-care. However, to be
# strictly compatible w/ the 68881/882, we make sure to return an
# INF w/ the j-bit set if the input INF j-bit was set. Destination
# INFs take priority.
#
	global		fmul_inf_dst		# global for fsglmul
fmul_inf_dst:
	fmovm.x		DST(%a1),&0x80		# return INF result in fp0
	mov.b		SRC_EX(%a0),%d0		# exclusive or the signs
	mov.b		DST_EX(%a1),%d1
	eor.b		%d0,%d1
	bpl.b		fmul_inf_dst_p		# result INF is pos.
fmul_inf_dst_n:
	fabs.x		%fp0			# clear result sign
	fneg.x		%fp0			# set result sign
	mov.b		&inf_bmask+neg_bmask,FPSR_CC(%a6) # set INF/N
	rts
fmul_inf_dst_p:
	fabs.x		%fp0			# clear result sign
	mov.b		&inf_bmask,FPSR_CC(%a6)	# set INF
	rts

	global		fmul_inf_src		# global for fsglmul
fmul_inf_src:
	fmovm.x		SRC(%a0),&0x80		# return INF result in fp0
	mov.b		SRC_EX(%a0),%d0		# exclusive or the signs
	mov.b		DST_EX(%a1),%d1
	eor.b		%d0,%d1
	bpl.b		fmul_inf_dst_p		# result INF is pos.
	bra.b		fmul_inf_dst_n

#########################################################################
# XDEF ****************************************************************	#
#	fin(): emulates the fmove instruction				#
#	fsin(): emulates the fsmove instruction				#
#	fdin(): emulates the fdmove instruction				#
#									#
# XREF ****************************************************************	#
#	norm() - normalize mantissa for EXOP on denorm			#
#	scale_to_zero_src() - scale src exponent to zero		#
#	ovf_res() - return default overflow result			#
#	unf_res() - return default underflow result			#
#	res_qnan_1op() - return QNAN result				#
#	res_snan_1op() - return SNAN result				#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	d0 = round prec/mode						#
#									#
# OUTPUT **************************************************************	#
#	fp0 = result							#
#	fp1 = EXOP (if exception occurred)				#
#									#
# ALGORITHM ***********************************************************	#
#	Handle NANs, infinities, and zeroes as special cases. Divide	#
# norms into extended, single, and double precision.			#
#	Norms can be emulated w/ a regular fmove instruction. For	#
# sgl/dbl, must scale exponent and perform an "fmove". Check to see	#
# if the result would have overflowed/underflowed. If so, use unf_res()	#
# or ovf_res() to return the default result. Also return EXOP if	#
# exception is enabled. If no exception, return the default result.	#
#	Unnorms don't pass through here.				#
#									#
#########################################################################

	global		fsin
fsin:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&s_mode*0x10,%d0	# insert sgl precision
	bra.b		fin

	global		fdin
fdin:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&d_mode*0x10,%d0	# insert dbl precision

	global		fin
fin:
	mov.l		%d0,L_SCR3(%a6)		# store rnd info

	mov.b		STAG(%a6),%d1		# fetch src optype tag
	bne.w		fin_not_norm		# optimize on non-norm input

#
# FP MOVE IN: NORMs and DENORMs ONLY!
#
fin_norm:
	andi.b		&0xc0,%d0		# is precision extended?
	bne.w		fin_not_ext		# no, so go handle dbl or sgl

#
# precision selected is extended. so...we cannot get an underflow
# or overflow because of rounding to the correct precision. so...
# skip the scaling and unscaling...
#
	tst.b		SRC_EX(%a0)		# is the operand negative?
	bpl.b		fin_norm_done		# no
	bset		&neg_bit,FPSR_CC(%a6)	# yes, so set 'N' ccode bit
fin_norm_done:
	fmovm.x		SRC(%a0),&0x80		# return result in fp0
	rts

#
# for an extended precision DENORM, the UNFL exception bit is set
# the accrued bit is NOT set in this instance(no inexactness!)
#
fin_denorm:
	andi.b		&0xc0,%d0		# is precision extended?
	bne.w		fin_not_ext		# no, so go handle dbl or sgl

	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit
	tst.b		SRC_EX(%a0)		# is the operand negative?
	bpl.b		fin_denorm_done		# no
	bset		&neg_bit,FPSR_CC(%a6)	# yes, so set 'N' ccode bit
fin_denorm_done:
	fmovm.x		SRC(%a0),&0x80		# return result in fp0
	btst		&unfl_bit,FPCR_ENABLE(%a6) # is UNFL enabled?
	bne.b		fin_denorm_unfl_ena	# yes
	rts

#
# the input is an extended DENORM and underflow is enabled in the FPCR.
# normalize the mantissa and add the bias of 0x6000 to the resulting negative
# exponent and insert back into the operand.
#
fin_denorm_unfl_ena:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	lea		FP_SCR0(%a6),%a0	# pass: ptr to operand
	bsr.l		norm			# normalize result
	neg.w		%d0			# new exponent = -(shft val)
	addi.w		&0x6000,%d0		# add new bias to exponent
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch old sign,exp
	andi.w		&0x8000,%d1		# keep old sign
	andi.w		&0x7fff,%d0		# clear sign position
	or.w		%d1,%d0			# concat new exo,old sign
	mov.w		%d0,FP_SCR0_EX(%a6)	# insert new exponent
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	rts

#
# operand is to be rounded to single or double precision
#
fin_not_ext:
	cmpi.b		%d0,&s_mode*0x10	# separate sgl/dbl prec
	bne.b		fin_dbl

#
# operand is to be rounded to single precision
#
fin_sgl:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	bsr.l		scale_to_zero_src	# calculate scale factor

	cmpi.l		%d0,&0x3fff-0x3f80	# will move in underflow?
	bge.w		fin_sd_unfl		# yes; go handle underflow
	cmpi.l		%d0,&0x3fff-0x407e	# will move in overflow?
	beq.w		fin_sd_may_ovfl		# maybe; go check
	blt.w		fin_sd_ovfl		# yes; go handle overflow

#
# operand will NOT overflow or underflow when moved into the fp reg file
#
fin_sd_normal:
	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fmov.x		FP_SCR0(%a6),%fp0	# perform move

	fmov.l		%fpsr,%d1		# save FPSR
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fin_sd_normal_exit:
	mov.l		%d2,-(%sp)		# save d2
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result
	mov.w		FP_SCR0_EX(%a6),%d1	# load {sgn,exp}
	mov.w		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	sub.l		%d0,%d1			# add scale factor
	andi.w		&0x8000,%d2		# keep old sign
	or.w		%d1,%d2			# concat old sign,new exponent
	mov.w		%d2,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x80	# return result in fp0
	rts

#
# operand is to be rounded to double precision
#
fin_dbl:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	bsr.l		scale_to_zero_src	# calculate scale factor

	cmpi.l		%d0,&0x3fff-0x3c00	# will move in underflow?
	bge.w		fin_sd_unfl		# yes; go handle underflow
	cmpi.l		%d0,&0x3fff-0x43fe	# will move in overflow?
	beq.w		fin_sd_may_ovfl		# maybe; go check
	blt.w		fin_sd_ovfl		# yes; go handle overflow
	bra.w		fin_sd_normal		# no; ho handle normalized op

#
# operand WILL underflow when moved in to the fp register file
#
fin_sd_unfl:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit

	tst.b		FP_SCR0_EX(%a6)		# is operand negative?
	bpl.b		fin_sd_unfl_tst
	bset		&neg_bit,FPSR_CC(%a6)	# set 'N' ccode bit

# if underflow or inexact is enabled, then go calculate the EXOP first.
fin_sd_unfl_tst:
	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0b,%d1		# is UNFL or INEX enabled?
	bne.b		fin_sd_unfl_ena		# yes

fin_sd_unfl_dis:
	lea		FP_SCR0(%a6),%a0	# pass: result addr
	mov.l		L_SCR3(%a6),%d1		# pass: rnd prec,mode
	bsr.l		unf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# unf_res may have set 'Z'
	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0
	rts

#
# operand will underflow AND underflow or inexact is enabled.
# therefore, we must return the result rounded to extended precision.
#
fin_sd_unfl_ena:
	mov.l		FP_SCR0_HI(%a6),FP_SCR1_HI(%a6)
	mov.l		FP_SCR0_LO(%a6),FP_SCR1_LO(%a6)
	mov.w		FP_SCR0_EX(%a6),%d1	# load current exponent

	mov.l		%d2,-(%sp)		# save d2
	mov.w		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	sub.l		%d0,%d1			# subtract scale factor
	andi.w		&0x8000,%d2		# extract old sign
	addi.l		&0x6000,%d1		# add new bias
	andi.w		&0x7fff,%d1
	or.w		%d1,%d2			# concat old sign,new exp
	mov.w		%d2,FP_SCR1_EX(%a6)	# insert new exponent
	fmovm.x		FP_SCR1(%a6),&0x40	# return EXOP in fp1
	mov.l		(%sp)+,%d2		# restore d2
	bra.b		fin_sd_unfl_dis

#
# operand WILL overflow.
#
fin_sd_ovfl:
	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fmov.x		FP_SCR0(%a6),%fp0	# perform move

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d1		# save FPSR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fin_sd_ovfl_tst:
	or.l		&ovfl_inx_mask,USER_FPSR(%a6) # set ovfl/aovfl/ainex

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x13,%d1		# is OVFL or INEX enabled?
	bne.b		fin_sd_ovfl_ena		# yes

#
# OVFL is not enabled; therefore, we must create the default result by
# calling ovf_res().
#
fin_sd_ovfl_dis:
	btst		&neg_bit,FPSR_CC(%a6)	# is result negative?
	sne		%d1			# set sign param accordingly
	mov.l		L_SCR3(%a6),%d0		# pass: prec,mode
	bsr.l		ovf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# set INF,N if applicable
	fmovm.x		(%a0),&0x80		# return default result in fp0
	rts

#
# OVFL is enabled.
# the INEX2 bit has already been updated by the round to the correct precision.
# now, round to extended(and don't alter the FPSR).
#
fin_sd_ovfl_ena:
	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	sub.l		&0x6000,%d1		# subtract bias
	andi.w		&0x7fff,%d1
	or.w		%d2,%d1
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	bra.b		fin_sd_ovfl_dis

#
# the move in MAY overflow. so...
#
fin_sd_may_ovfl:
	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fmov.x		FP_SCR0(%a6),%fp0	# perform the move

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	fabs.x		%fp0,%fp1		# make a copy of result
	fcmp.b		%fp1,&0x2		# is |result| >= 2.b?
	fbge.w		fin_sd_ovfl_tst		# yes; overflow has occurred

# no, it didn't overflow; we have correct result
	bra.w		fin_sd_normal_exit

##########################################################################

#
# operand is not a NORM: check its optype and branch accordingly
#
fin_not_norm:
	cmpi.b		%d1,&DENORM		# weed out DENORM
	beq.w		fin_denorm
	cmpi.b		%d1,&SNAN		# weed out SNANs
	beq.l		res_snan_1op
	cmpi.b		%d1,&QNAN		# weed out QNANs
	beq.l		res_qnan_1op

#
# do the fmove in; at this point, only possible ops are ZERO and INF.
# use fmov to determine ccodes.
# prec:mode should be zero at this point but it won't affect answer anyways.
#
	fmov.x		SRC(%a0),%fp0		# do fmove in
	fmov.l		%fpsr,%d0		# no exceptions possible
	rol.l		&0x8,%d0		# put ccodes in lo byte
	mov.b		%d0,FPSR_CC(%a6)	# insert correct ccodes
	rts

#########################################################################
# XDEF ****************************************************************	#
#	fdiv(): emulates the fdiv instruction				#
#	fsdiv(): emulates the fsdiv instruction				#
#	fddiv(): emulates the fddiv instruction				#
#									#
# XREF ****************************************************************	#
#	scale_to_zero_src() - scale src exponent to zero		#
#	scale_to_zero_dst() - scale dst exponent to zero		#
#	unf_res() - return default underflow result			#
#	ovf_res() - return default overflow result			#
#	res_qnan() - return QNAN result					#
#	res_snan() - return SNAN result					#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	a1 = pointer to extended precision destination operand		#
#	d0  rnd prec,mode						#
#									#
# OUTPUT **************************************************************	#
#	fp0 = result							#
#	fp1 = EXOP (if exception occurred)				#
#									#
# ALGORITHM ***********************************************************	#
#	Handle NANs, infinities, and zeroes as special cases. Divide	#
# norms/denorms into ext/sgl/dbl precision.				#
#	For norms/denorms, scale the exponents such that a divide	#
# instruction won't cause an exception. Use the regular fdiv to		#
# compute a result. Check if the regular operands would have taken	#
# an exception. If so, return the default overflow/underflow result	#
# and return the EXOP if exceptions are enabled. Else, scale the	#
# result operand to the proper exponent.				#
#									#
#########################################################################

	align		0x10
tbl_fdiv_unfl:
	long		0x3fff - 0x0000		# ext_unfl
	long		0x3fff - 0x3f81		# sgl_unfl
	long		0x3fff - 0x3c01		# dbl_unfl

tbl_fdiv_ovfl:
	long		0x3fff - 0x7ffe		# ext overflow exponent
	long		0x3fff - 0x407e		# sgl overflow exponent
	long		0x3fff - 0x43fe		# dbl overflow exponent

	global		fsdiv
fsdiv:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&s_mode*0x10,%d0	# insert sgl prec
	bra.b		fdiv

	global		fddiv
fddiv:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&d_mode*0x10,%d0	# insert dbl prec

	global		fdiv
fdiv:
	mov.l		%d0,L_SCR3(%a6)		# store rnd info

	clr.w		%d1
	mov.b		DTAG(%a6),%d1
	lsl.b		&0x3,%d1
	or.b		STAG(%a6),%d1		# combine src tags

	bne.w		fdiv_not_norm		# optimize on non-norm input

#
# DIVIDE: NORMs and DENORMs ONLY!
#
fdiv_norm:
	mov.w		DST_EX(%a1),FP_SCR1_EX(%a6)
	mov.l		DST_HI(%a1),FP_SCR1_HI(%a6)
	mov.l		DST_LO(%a1),FP_SCR1_LO(%a6)

	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)

	bsr.l		scale_to_zero_src	# scale src exponent
	mov.l		%d0,-(%sp)		# save scale factor 1

	bsr.l		scale_to_zero_dst	# scale dst exponent

	neg.l		(%sp)			# SCALE FACTOR = scale1 - scale2
	add.l		%d0,(%sp)

	mov.w		2+L_SCR3(%a6),%d1	# fetch precision
	lsr.b		&0x6,%d1		# shift to lo bits
	mov.l		(%sp)+,%d0		# load S.F.
	cmp.l		%d0,(tbl_fdiv_ovfl.b,%pc,%d1.w*4) # will result overflow?
	ble.w		fdiv_may_ovfl		# result will overflow

	cmp.l		%d0,(tbl_fdiv_unfl.w,%pc,%d1.w*4) # will result underflow?
	beq.w		fdiv_may_unfl		# maybe
	bgt.w		fdiv_unfl		# yes; go handle underflow

fdiv_normal:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# save FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fdiv.x		FP_SCR0(%a6),%fp0	# perform divide

	fmov.l		%fpsr,%d1		# save FPSR
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fdiv_normal_exit:
	fmovm.x		&0x80,FP_SCR0(%a6)	# store result on stack
	mov.l		%d2,-(%sp)		# store d2
	mov.w		FP_SCR0_EX(%a6),%d1	# load {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	or.w		%d2,%d1			# concat old sign,new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x80	# return result in fp0
	rts

tbl_fdiv_ovfl2:
	long		0x7fff
	long		0x407f
	long		0x43ff

fdiv_no_ovfl:
	mov.l		(%sp)+,%d0		# restore scale factor
	bra.b		fdiv_normal_exit

fdiv_may_ovfl:
	mov.l		%d0,-(%sp)		# save scale factor

	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# set FPSR

	fdiv.x		FP_SCR0(%a6),%fp0	# execute divide

	fmov.l		%fpsr,%d0
	fmov.l		&0x0,%fpcr

	or.l		%d0,USER_FPSR(%a6)	# save INEX,N

	fmovm.x		&0x01,-(%sp)		# save result to stack
	mov.w		(%sp),%d0		# fetch new exponent
	add.l		&0xc,%sp		# clear result from stack
	andi.l		&0x7fff,%d0		# strip sign
	sub.l		(%sp),%d0		# add scale factor
	cmp.l		%d0,(tbl_fdiv_ovfl2.b,%pc,%d1.w*4)
	blt.b		fdiv_no_ovfl
	mov.l		(%sp)+,%d0

fdiv_ovfl_tst:
	or.l		&ovfl_inx_mask,USER_FPSR(%a6) # set ovfl/aovfl/ainex

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x13,%d1		# is OVFL or INEX enabled?
	bne.b		fdiv_ovfl_ena		# yes

fdiv_ovfl_dis:
	btst		&neg_bit,FPSR_CC(%a6)	# is result negative?
	sne		%d1			# set sign param accordingly
	mov.l		L_SCR3(%a6),%d0		# pass prec:rnd
	bsr.l		ovf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# set INF if applicable
	fmovm.x		(%a0),&0x80		# return default result in fp0
	rts

fdiv_ovfl_ena:
	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# is precision extended?
	bne.b		fdiv_ovfl_ena_sd	# no, do sgl or dbl

fdiv_ovfl_ena_cont:
	fmovm.x		&0x80,FP_SCR0(%a6)	# move result to stack

	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.w		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	sub.l		%d0,%d1			# add scale factor
	subi.l		&0x6000,%d1		# subtract bias
	andi.w		&0x7fff,%d1		# clear sign bit
	andi.w		&0x8000,%d2		# keep old sign
	or.w		%d2,%d1			# concat old sign,new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	bra.b		fdiv_ovfl_dis

fdiv_ovfl_ena_sd:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst operand

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0x30,%d1		# keep rnd mode
	fmov.l		%d1,%fpcr		# set FPCR

	fdiv.x		FP_SCR0(%a6),%fp0	# execute divide

	fmov.l		&0x0,%fpcr		# clear FPCR
	bra.b		fdiv_ovfl_ena_cont

fdiv_unfl:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit

	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		&rz_mode*0x10,%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fdiv.x		FP_SCR0(%a6),%fp0	# execute divide

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0b,%d1		# is UNFL or INEX enabled?
	bne.b		fdiv_unfl_ena		# yes

fdiv_unfl_dis:
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result

	lea		FP_SCR0(%a6),%a0	# pass: result addr
	mov.l		L_SCR3(%a6),%d1		# pass: rnd prec,mode
	bsr.l		unf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# 'Z' may have been set
	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0
	rts

#
# UNFL is enabled.
#
fdiv_unfl_ena:
	fmovm.x		FP_SCR1(%a6),&0x40	# load dst op

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# is precision extended?
	bne.b		fdiv_unfl_ena_sd	# no, sgl or dbl

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

fdiv_unfl_ena_cont:
	fmov.l		&0x0,%fpsr		# clear FPSR

	fdiv.x		FP_SCR0(%a6),%fp1	# execute divide

	fmov.l		&0x0,%fpcr		# clear FPCR

	fmovm.x		&0x40,FP_SCR0(%a6)	# save result to stack
	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factoer
	addi.l		&0x6000,%d1		# add bias
	andi.w		&0x7fff,%d1
	or.w		%d2,%d1			# concat old sign,new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exp
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	bra.w		fdiv_unfl_dis

fdiv_unfl_ena_sd:
	mov.l		L_SCR3(%a6),%d1
	andi.b		&0x30,%d1		# use only rnd mode
	fmov.l		%d1,%fpcr		# set FPCR

	bra.b		fdiv_unfl_ena_cont

#
# the divide operation MAY underflow:
#
fdiv_may_unfl:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fdiv.x		FP_SCR0(%a6),%fp0	# execute divide

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	fabs.x		%fp0,%fp1		# make a copy of result
	fcmp.b		%fp1,&0x1		# is |result| > 1.b?
	fbgt.w		fdiv_normal_exit	# no; no underflow occurred
	fblt.w		fdiv_unfl		# yes; underflow occurred

#
# we still don't know if underflow occurred. result is ~ equal to 1. but,
# we don't know if the result was an underflow that rounded up to a 1
# or a normalized number that rounded down to a 1. so, redo the entire
# operation using RZ as the rounding mode to see what the pre-rounded
# result is. this case should be relatively rare.
#
	fmovm.x		FP_SCR1(%a6),&0x40	# load dst op into fp1

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# keep rnd prec
	ori.b		&rz_mode*0x10,%d1	# insert RZ

	fmov.l		%d1,%fpcr		# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fdiv.x		FP_SCR0(%a6),%fp1	# execute divide

	fmov.l		&0x0,%fpcr		# clear FPCR
	fabs.x		%fp1			# make absolute value
	fcmp.b		%fp1,&0x1		# is |result| < 1.b?
	fbge.w		fdiv_normal_exit	# no; no underflow occurred
	bra.w		fdiv_unfl		# yes; underflow occurred

############################################################################

#
# Divide: inputs are not both normalized; what are they?
#
fdiv_not_norm:
	mov.w		(tbl_fdiv_op.b,%pc,%d1.w*2),%d1
	jmp		(tbl_fdiv_op.b,%pc,%d1.w*1)

	swbeg		&48
tbl_fdiv_op:
	short		fdiv_norm	- tbl_fdiv_op # NORM / NORM
	short		fdiv_inf_load	- tbl_fdiv_op # NORM / ZERO
	short		fdiv_zero_load	- tbl_fdiv_op # NORM / INF
	short		fdiv_res_qnan	- tbl_fdiv_op # NORM / QNAN
	short		fdiv_norm	- tbl_fdiv_op # NORM / DENORM
	short		fdiv_res_snan	- tbl_fdiv_op # NORM / SNAN
	short		tbl_fdiv_op	- tbl_fdiv_op #
	short		tbl_fdiv_op	- tbl_fdiv_op #

	short		fdiv_zero_load	- tbl_fdiv_op # ZERO / NORM
	short		fdiv_res_operr	- tbl_fdiv_op # ZERO / ZERO
	short		fdiv_zero_load	- tbl_fdiv_op # ZERO / INF
	short		fdiv_res_qnan	- tbl_fdiv_op # ZERO / QNAN
	short		fdiv_zero_load	- tbl_fdiv_op # ZERO / DENORM
	short		fdiv_res_snan	- tbl_fdiv_op # ZERO / SNAN
	short		tbl_fdiv_op	- tbl_fdiv_op #
	short		tbl_fdiv_op	- tbl_fdiv_op #

	short		fdiv_inf_dst	- tbl_fdiv_op # INF / NORM
	short		fdiv_inf_dst	- tbl_fdiv_op # INF / ZERO
	short		fdiv_res_operr	- tbl_fdiv_op # INF / INF
	short		fdiv_res_qnan	- tbl_fdiv_op # INF / QNAN
	short		fdiv_inf_dst	- tbl_fdiv_op # INF / DENORM
	short		fdiv_res_snan	- tbl_fdiv_op # INF / SNAN
	short		tbl_fdiv_op	- tbl_fdiv_op #
	short		tbl_fdiv_op	- tbl_fdiv_op #

	short		fdiv_res_qnan	- tbl_fdiv_op # QNAN / NORM
	short		fdiv_res_qnan	- tbl_fdiv_op # QNAN / ZERO
	short		fdiv_res_qnan	- tbl_fdiv_op # QNAN / INF
	short		fdiv_res_qnan	- tbl_fdiv_op # QNAN / QNAN
	short		fdiv_res_qnan	- tbl_fdiv_op # QNAN / DENORM
	short		fdiv_res_snan	- tbl_fdiv_op # QNAN / SNAN
	short		tbl_fdiv_op	- tbl_fdiv_op #
	short		tbl_fdiv_op	- tbl_fdiv_op #

	short		fdiv_norm	- tbl_fdiv_op # DENORM / NORM
	short		fdiv_inf_load	- tbl_fdiv_op # DENORM / ZERO
	short		fdiv_zero_load	- tbl_fdiv_op # DENORM / INF
	short		fdiv_res_qnan	- tbl_fdiv_op # DENORM / QNAN
	short		fdiv_norm	- tbl_fdiv_op # DENORM / DENORM
	short		fdiv_res_snan	- tbl_fdiv_op # DENORM / SNAN
	short		tbl_fdiv_op	- tbl_fdiv_op #
	short		tbl_fdiv_op	- tbl_fdiv_op #

	short		fdiv_res_snan	- tbl_fdiv_op # SNAN / NORM
	short		fdiv_res_snan	- tbl_fdiv_op # SNAN / ZERO
	short		fdiv_res_snan	- tbl_fdiv_op # SNAN / INF
	short		fdiv_res_snan	- tbl_fdiv_op # SNAN / QNAN
	short		fdiv_res_snan	- tbl_fdiv_op # SNAN / DENORM
	short		fdiv_res_snan	- tbl_fdiv_op # SNAN / SNAN
	short		tbl_fdiv_op	- tbl_fdiv_op #
	short		tbl_fdiv_op	- tbl_fdiv_op #

fdiv_res_qnan:
	bra.l		res_qnan
fdiv_res_snan:
	bra.l		res_snan
fdiv_res_operr:
	bra.l		res_operr

	global		fdiv_zero_load		# global for fsgldiv
fdiv_zero_load:
	mov.b		SRC_EX(%a0),%d0		# result sign is exclusive
	mov.b		DST_EX(%a1),%d1		# or of input signs.
	eor.b		%d0,%d1
	bpl.b		fdiv_zero_load_p	# result is positive
	fmov.s		&0x80000000,%fp0	# load a -ZERO
	mov.b		&z_bmask+neg_bmask,FPSR_CC(%a6)	# set Z/N
	rts
fdiv_zero_load_p:
	fmov.s		&0x00000000,%fp0	# load a +ZERO
	mov.b		&z_bmask,FPSR_CC(%a6)	# set Z
	rts

#
# The destination was In Range and the source was a ZERO. The result,
# therefore, is an INF w/ the proper sign.
# So, determine the sign and return a new INF (w/ the j-bit cleared).
#
	global		fdiv_inf_load		# global for fsgldiv
fdiv_inf_load:
	ori.w		&dz_mask+adz_mask,2+USER_FPSR(%a6) # no; set DZ/ADZ
	mov.b		SRC_EX(%a0),%d0		# load both signs
	mov.b		DST_EX(%a1),%d1
	eor.b		%d0,%d1
	bpl.b		fdiv_inf_load_p		# result is positive
	fmov.s		&0xff800000,%fp0	# make result -INF
	mov.b		&inf_bmask+neg_bmask,FPSR_CC(%a6) # set INF/N
	rts
fdiv_inf_load_p:
	fmov.s		&0x7f800000,%fp0	# make result +INF
	mov.b		&inf_bmask,FPSR_CC(%a6)	# set INF
	rts

#
# The destination was an INF w/ an In Range or ZERO source, the result is
# an INF w/ the proper sign.
# The 68881/882 returns the destination INF w/ the new sign(if the j-bit of the
# dst INF is set, then then j-bit of the result INF is also set).
#
	global		fdiv_inf_dst		# global for fsgldiv
fdiv_inf_dst:
	mov.b		DST_EX(%a1),%d0		# load both signs
	mov.b		SRC_EX(%a0),%d1
	eor.b		%d0,%d1
	bpl.b		fdiv_inf_dst_p		# result is positive

	fmovm.x		DST(%a1),&0x80		# return result in fp0
	fabs.x		%fp0			# clear sign bit
	fneg.x		%fp0			# set sign bit
	mov.b		&inf_bmask+neg_bmask,FPSR_CC(%a6) # set INF/NEG
	rts

fdiv_inf_dst_p:
	fmovm.x		DST(%a1),&0x80		# return result in fp0
	fabs.x		%fp0			# return positive INF
	mov.b		&inf_bmask,FPSR_CC(%a6) # set INF
	rts

#########################################################################
# XDEF ****************************************************************	#
#	fneg(): emulates the fneg instruction				#
#	fsneg(): emulates the fsneg instruction				#
#	fdneg(): emulates the fdneg instruction				#
#									#
# XREF ****************************************************************	#
#	norm() - normalize a denorm to provide EXOP			#
#	scale_to_zero_src() - scale sgl/dbl source exponent		#
#	ovf_res() - return default overflow result			#
#	unf_res() - return default underflow result			#
#	res_qnan_1op() - return QNAN result				#
#	res_snan_1op() - return SNAN result				#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	d0 = rnd prec,mode						#
#									#
# OUTPUT **************************************************************	#
#	fp0 = result							#
#	fp1 = EXOP (if exception occurred)				#
#									#
# ALGORITHM ***********************************************************	#
#	Handle NANs, zeroes, and infinities as special cases. Separate	#
# norms/denorms into ext/sgl/dbl precisions. Extended precision can be	#
# emulated by simply setting sign bit. Sgl/dbl operands must be scaled	#
# and an actual fneg performed to see if overflow/underflow would have	#
# occurred. If so, return default underflow/overflow result. Else,	#
# scale the result exponent and return result. FPSR gets set based on	#
# the result value.							#
#									#
#########################################################################

	global		fsneg
fsneg:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&s_mode*0x10,%d0	# insert sgl precision
	bra.b		fneg

	global		fdneg
fdneg:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&d_mode*0x10,%d0	# insert dbl prec

	global		fneg
fneg:
	mov.l		%d0,L_SCR3(%a6)		# store rnd info
	mov.b		STAG(%a6),%d1
	bne.w		fneg_not_norm		# optimize on non-norm input

#
# NEGATE SIGN : norms and denorms ONLY!
#
fneg_norm:
	andi.b		&0xc0,%d0		# is precision extended?
	bne.w		fneg_not_ext		# no; go handle sgl or dbl

#
# precision selected is extended. so...we can not get an underflow
# or overflow because of rounding to the correct precision. so...
# skip the scaling and unscaling...
#
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	mov.w		SRC_EX(%a0),%d0
	eori.w		&0x8000,%d0		# negate sign
	bpl.b		fneg_norm_load		# sign is positive
	mov.b		&neg_bmask,FPSR_CC(%a6)	# set 'N' ccode bit
fneg_norm_load:
	mov.w		%d0,FP_SCR0_EX(%a6)
	fmovm.x		FP_SCR0(%a6),&0x80	# return result in fp0
	rts

#
# for an extended precision DENORM, the UNFL exception bit is set
# the accrued bit is NOT set in this instance(no inexactness!)
#
fneg_denorm:
	andi.b		&0xc0,%d0		# is precision extended?
	bne.b		fneg_not_ext		# no; go handle sgl or dbl

	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit

	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	mov.w		SRC_EX(%a0),%d0
	eori.w		&0x8000,%d0		# negate sign
	bpl.b		fneg_denorm_done	# no
	mov.b		&neg_bmask,FPSR_CC(%a6)	# yes, set 'N' ccode bit
fneg_denorm_done:
	mov.w		%d0,FP_SCR0_EX(%a6)
	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0

	btst		&unfl_bit,FPCR_ENABLE(%a6) # is UNFL enabled?
	bne.b		fneg_ext_unfl_ena	# yes
	rts

#
# the input is an extended DENORM and underflow is enabled in the FPCR.
# normalize the mantissa and add the bias of 0x6000 to the resulting negative
# exponent and insert back into the operand.
#
fneg_ext_unfl_ena:
	lea		FP_SCR0(%a6),%a0	# pass: ptr to operand
	bsr.l		norm			# normalize result
	neg.w		%d0			# new exponent = -(shft val)
	addi.w		&0x6000,%d0		# add new bias to exponent
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch old sign,exp
	andi.w		&0x8000,%d1		# keep old sign
	andi.w		&0x7fff,%d0		# clear sign position
	or.w		%d1,%d0			# concat old sign, new exponent
	mov.w		%d0,FP_SCR0_EX(%a6)	# insert new exponent
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	rts

#
# operand is either single or double
#
fneg_not_ext:
	cmpi.b		%d0,&s_mode*0x10	# separate sgl/dbl prec
	bne.b		fneg_dbl

#
# operand is to be rounded to single precision
#
fneg_sgl:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	bsr.l		scale_to_zero_src	# calculate scale factor

	cmpi.l		%d0,&0x3fff-0x3f80	# will move in underflow?
	bge.w		fneg_sd_unfl		# yes; go handle underflow
	cmpi.l		%d0,&0x3fff-0x407e	# will move in overflow?
	beq.w		fneg_sd_may_ovfl	# maybe; go check
	blt.w		fneg_sd_ovfl		# yes; go handle overflow

#
# operand will NOT overflow or underflow when moved in to the fp reg file
#
fneg_sd_normal:
	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fneg.x		FP_SCR0(%a6),%fp0	# perform negation

	fmov.l		%fpsr,%d1		# save FPSR
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fneg_sd_normal_exit:
	mov.l		%d2,-(%sp)		# save d2
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result
	mov.w		FP_SCR0_EX(%a6),%d1	# load sgn,exp
	mov.w		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	sub.l		%d0,%d1			# add scale factor
	andi.w		&0x8000,%d2		# keep old sign
	or.w		%d1,%d2			# concat old sign,new exp
	mov.w		%d2,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x80	# return result in fp0
	rts

#
# operand is to be rounded to double precision
#
fneg_dbl:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	bsr.l		scale_to_zero_src	# calculate scale factor

	cmpi.l		%d0,&0x3fff-0x3c00	# will move in underflow?
	bge.b		fneg_sd_unfl		# yes; go handle underflow
	cmpi.l		%d0,&0x3fff-0x43fe	# will move in overflow?
	beq.w		fneg_sd_may_ovfl	# maybe; go check
	blt.w		fneg_sd_ovfl		# yes; go handle overflow
	bra.w		fneg_sd_normal		# no; ho handle normalized op

#
# operand WILL underflow when moved in to the fp register file
#
fneg_sd_unfl:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit

	eori.b		&0x80,FP_SCR0_EX(%a6)	# negate sign
	bpl.b		fneg_sd_unfl_tst
	bset		&neg_bit,FPSR_CC(%a6)	# set 'N' ccode bit

# if underflow or inexact is enabled, go calculate EXOP first.
fneg_sd_unfl_tst:
	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0b,%d1		# is UNFL or INEX enabled?
	bne.b		fneg_sd_unfl_ena	# yes

fneg_sd_unfl_dis:
	lea		FP_SCR0(%a6),%a0	# pass: result addr
	mov.l		L_SCR3(%a6),%d1		# pass: rnd prec,mode
	bsr.l		unf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# unf_res may have set 'Z'
	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0
	rts

#
# operand will underflow AND underflow is enabled.
# therefore, we must return the result rounded to extended precision.
#
fneg_sd_unfl_ena:
	mov.l		FP_SCR0_HI(%a6),FP_SCR1_HI(%a6)
	mov.l		FP_SCR0_LO(%a6),FP_SCR1_LO(%a6)
	mov.w		FP_SCR0_EX(%a6),%d1	# load current exponent

	mov.l		%d2,-(%sp)		# save d2
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# subtract scale factor
	addi.l		&0x6000,%d1		# add new bias
	andi.w		&0x7fff,%d1
	or.w		%d2,%d1			# concat new sign,new exp
	mov.w		%d1,FP_SCR1_EX(%a6)	# insert new exp
	fmovm.x		FP_SCR1(%a6),&0x40	# return EXOP in fp1
	mov.l		(%sp)+,%d2		# restore d2
	bra.b		fneg_sd_unfl_dis

#
# operand WILL overflow.
#
fneg_sd_ovfl:
	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fneg.x		FP_SCR0(%a6),%fp0	# perform negation

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d1		# save FPSR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fneg_sd_ovfl_tst:
	or.l		&ovfl_inx_mask,USER_FPSR(%a6) # set ovfl/aovfl/ainex

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x13,%d1		# is OVFL or INEX enabled?
	bne.b		fneg_sd_ovfl_ena	# yes

#
# OVFL is not enabled; therefore, we must create the default result by
# calling ovf_res().
#
fneg_sd_ovfl_dis:
	btst		&neg_bit,FPSR_CC(%a6)	# is result negative?
	sne		%d1			# set sign param accordingly
	mov.l		L_SCR3(%a6),%d0		# pass: prec,mode
	bsr.l		ovf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# set INF,N if applicable
	fmovm.x		(%a0),&0x80		# return default result in fp0
	rts

#
# OVFL is enabled.
# the INEX2 bit has already been updated by the round to the correct precision.
# now, round to extended(and don't alter the FPSR).
#
fneg_sd_ovfl_ena:
	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	subi.l		&0x6000,%d1		# subtract bias
	andi.w		&0x7fff,%d1
	or.w		%d2,%d1			# concat sign,exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	mov.l		(%sp)+,%d2		# restore d2
	bra.b		fneg_sd_ovfl_dis

#
# the move in MAY underflow. so...
#
fneg_sd_may_ovfl:
	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fneg.x		FP_SCR0(%a6),%fp0	# perform negation

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	fabs.x		%fp0,%fp1		# make a copy of result
	fcmp.b		%fp1,&0x2		# is |result| >= 2.b?
	fbge.w		fneg_sd_ovfl_tst	# yes; overflow has occurred

# no, it didn't overflow; we have correct result
	bra.w		fneg_sd_normal_exit

##########################################################################

#
# input is not normalized; what is it?
#
fneg_not_norm:
	cmpi.b		%d1,&DENORM		# weed out DENORM
	beq.w		fneg_denorm
	cmpi.b		%d1,&SNAN		# weed out SNAN
	beq.l		res_snan_1op
	cmpi.b		%d1,&QNAN		# weed out QNAN
	beq.l		res_qnan_1op

#
# do the fneg; at this point, only possible ops are ZERO and INF.
# use fneg to determine ccodes.
# prec:mode should be zero at this point but it won't affect answer anyways.
#
	fneg.x		SRC_EX(%a0),%fp0	# do fneg
	fmov.l		%fpsr,%d0
	rol.l		&0x8,%d0		# put ccodes in lo byte
	mov.b		%d0,FPSR_CC(%a6)	# insert correct ccodes
	rts

#########################################################################
# XDEF ****************************************************************	#
#	ftst(): emulates the ftest instruction				#
#									#
# XREF ****************************************************************	#
#	res{s,q}nan_1op() - set NAN result for monadic instruction	#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#									#
# OUTPUT **************************************************************	#
#	none								#
#									#
# ALGORITHM ***********************************************************	#
#	Check the source operand tag (STAG) and set the FPCR according	#
# to the operand type and sign.						#
#									#
#########################################################################

	global		ftst
ftst:
	mov.b		STAG(%a6),%d1
	bne.b		ftst_not_norm		# optimize on non-norm input

#
# Norm:
#
ftst_norm:
	tst.b		SRC_EX(%a0)		# is operand negative?
	bmi.b		ftst_norm_m		# yes
	rts
ftst_norm_m:
	mov.b		&neg_bmask,FPSR_CC(%a6)	# set 'N' ccode bit
	rts

#
# input is not normalized; what is it?
#
ftst_not_norm:
	cmpi.b		%d1,&ZERO		# weed out ZERO
	beq.b		ftst_zero
	cmpi.b		%d1,&INF		# weed out INF
	beq.b		ftst_inf
	cmpi.b		%d1,&SNAN		# weed out SNAN
	beq.l		res_snan_1op
	cmpi.b		%d1,&QNAN		# weed out QNAN
	beq.l		res_qnan_1op

#
# Denorm:
#
ftst_denorm:
	tst.b		SRC_EX(%a0)		# is operand negative?
	bmi.b		ftst_denorm_m		# yes
	rts
ftst_denorm_m:
	mov.b		&neg_bmask,FPSR_CC(%a6)	# set 'N' ccode bit
	rts

#
# Infinity:
#
ftst_inf:
	tst.b		SRC_EX(%a0)		# is operand negative?
	bmi.b		ftst_inf_m		# yes
ftst_inf_p:
	mov.b		&inf_bmask,FPSR_CC(%a6)	# set 'I' ccode bit
	rts
ftst_inf_m:
	mov.b		&inf_bmask+neg_bmask,FPSR_CC(%a6) # set 'I','N' ccode bits
	rts

#
# Zero:
#
ftst_zero:
	tst.b		SRC_EX(%a0)		# is operand negative?
	bmi.b		ftst_zero_m		# yes
ftst_zero_p:
	mov.b		&z_bmask,FPSR_CC(%a6)	# set 'N' ccode bit
	rts
ftst_zero_m:
	mov.b		&z_bmask+neg_bmask,FPSR_CC(%a6)	# set 'Z','N' ccode bits
	rts

#########################################################################
# XDEF ****************************************************************	#
#	fint(): emulates the fint instruction				#
#									#
# XREF ****************************************************************	#
#	res_{s,q}nan_1op() - set NAN result for monadic operation	#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	d0 = round precision/mode					#
#									#
# OUTPUT **************************************************************	#
#	fp0 = result							#
#									#
# ALGORITHM ***********************************************************	#
#	Separate according to operand type. Unnorms don't pass through	#
# here. For norms, load the rounding mode/prec, execute a "fint", then	#
# store the resulting FPSR bits.					#
#	For denorms, force the j-bit to a one and do the same as for	#
# norms. Denorms are so low that the answer will either be a zero or a	#
# one.									#
#	For zeroes/infs/NANs, return the same while setting the FPSR	#
# as appropriate.							#
#									#
#########################################################################

	global		fint
fint:
	mov.b		STAG(%a6),%d1
	bne.b		fint_not_norm		# optimize on non-norm input

#
# Norm:
#
fint_norm:
	andi.b		&0x30,%d0		# set prec = ext

	fmov.l		%d0,%fpcr		# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fint.x		SRC(%a0),%fp0		# execute fint

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d0		# save FPSR
	or.l		%d0,USER_FPSR(%a6)	# set exception bits

	rts

#
# input is not normalized; what is it?
#
fint_not_norm:
	cmpi.b		%d1,&ZERO		# weed out ZERO
	beq.b		fint_zero
	cmpi.b		%d1,&INF		# weed out INF
	beq.b		fint_inf
	cmpi.b		%d1,&DENORM		# weed out DENORM
	beq.b		fint_denorm
	cmpi.b		%d1,&SNAN		# weed out SNAN
	beq.l		res_snan_1op
	bra.l		res_qnan_1op		# weed out QNAN

#
# Denorm:
#
# for DENORMs, the result will be either (+/-)ZERO or (+/-)1.
# also, the INEX2 and AINEX exception bits will be set.
# so, we could either set these manually or force the DENORM
# to a very small NORM and ship it to the NORM routine.
# I do the latter.
#
fint_denorm:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6) # copy sign, zero exp
	mov.b		&0x80,FP_SCR0_HI(%a6)	# force DENORM ==> small NORM
	lea		FP_SCR0(%a6),%a0
	bra.b		fint_norm

#
# Zero:
#
fint_zero:
	tst.b		SRC_EX(%a0)		# is ZERO negative?
	bmi.b		fint_zero_m		# yes
fint_zero_p:
	fmov.s		&0x00000000,%fp0	# return +ZERO in fp0
	mov.b		&z_bmask,FPSR_CC(%a6)	# set 'Z' ccode bit
	rts
fint_zero_m:
	fmov.s		&0x80000000,%fp0	# return -ZERO in fp0
	mov.b		&z_bmask+neg_bmask,FPSR_CC(%a6) # set 'Z','N' ccode bits
	rts

#
# Infinity:
#
fint_inf:
	fmovm.x		SRC(%a0),&0x80		# return result in fp0
	tst.b		SRC_EX(%a0)		# is INF negative?
	bmi.b		fint_inf_m		# yes
fint_inf_p:
	mov.b		&inf_bmask,FPSR_CC(%a6)	# set 'I' ccode bit
	rts
fint_inf_m:
	mov.b		&inf_bmask+neg_bmask,FPSR_CC(%a6) # set 'N','I' ccode bits
	rts

#########################################################################
# XDEF ****************************************************************	#
#	fintrz(): emulates the fintrz instruction			#
#									#
# XREF ****************************************************************	#
#	res_{s,q}nan_1op() - set NAN result for monadic operation	#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	d0 = round precision/mode					#
#									#
# OUTPUT **************************************************************	#
#	fp0 = result							#
#									#
# ALGORITHM ***********************************************************	#
#	Separate according to operand type. Unnorms don't pass through	#
# here. For norms, load the rounding mode/prec, execute a "fintrz",	#
# then store the resulting FPSR bits.					#
#	For denorms, force the j-bit to a one and do the same as for	#
# norms. Denorms are so low that the answer will either be a zero or a	#
# one.									#
#	For zeroes/infs/NANs, return the same while setting the FPSR	#
# as appropriate.							#
#									#
#########################################################################

	global		fintrz
fintrz:
	mov.b		STAG(%a6),%d1
	bne.b		fintrz_not_norm		# optimize on non-norm input

#
# Norm:
#
fintrz_norm:
	fmov.l		&0x0,%fpsr		# clear FPSR

	fintrz.x	SRC(%a0),%fp0		# execute fintrz

	fmov.l		%fpsr,%d0		# save FPSR
	or.l		%d0,USER_FPSR(%a6)	# set exception bits

	rts

#
# input is not normalized; what is it?
#
fintrz_not_norm:
	cmpi.b		%d1,&ZERO		# weed out ZERO
	beq.b		fintrz_zero
	cmpi.b		%d1,&INF		# weed out INF
	beq.b		fintrz_inf
	cmpi.b		%d1,&DENORM		# weed out DENORM
	beq.b		fintrz_denorm
	cmpi.b		%d1,&SNAN		# weed out SNAN
	beq.l		res_snan_1op
	bra.l		res_qnan_1op		# weed out QNAN

#
# Denorm:
#
# for DENORMs, the result will be (+/-)ZERO.
# also, the INEX2 and AINEX exception bits will be set.
# so, we could either set these manually or force the DENORM
# to a very small NORM and ship it to the NORM routine.
# I do the latter.
#
fintrz_denorm:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6) # copy sign, zero exp
	mov.b		&0x80,FP_SCR0_HI(%a6)	# force DENORM ==> small NORM
	lea		FP_SCR0(%a6),%a0
	bra.b		fintrz_norm

#
# Zero:
#
fintrz_zero:
	tst.b		SRC_EX(%a0)		# is ZERO negative?
	bmi.b		fintrz_zero_m		# yes
fintrz_zero_p:
	fmov.s		&0x00000000,%fp0	# return +ZERO in fp0
	mov.b		&z_bmask,FPSR_CC(%a6)	# set 'Z' ccode bit
	rts
fintrz_zero_m:
	fmov.s		&0x80000000,%fp0	# return -ZERO in fp0
	mov.b		&z_bmask+neg_bmask,FPSR_CC(%a6) # set 'Z','N' ccode bits
	rts

#
# Infinity:
#
fintrz_inf:
	fmovm.x		SRC(%a0),&0x80		# return result in fp0
	tst.b		SRC_EX(%a0)		# is INF negative?
	bmi.b		fintrz_inf_m		# yes
fintrz_inf_p:
	mov.b		&inf_bmask,FPSR_CC(%a6)	# set 'I' ccode bit
	rts
fintrz_inf_m:
	mov.b		&inf_bmask+neg_bmask,FPSR_CC(%a6) # set 'N','I' ccode bits
	rts

#########################################################################
# XDEF ****************************************************************	#
#	fabs():  emulates the fabs instruction				#
#	fsabs(): emulates the fsabs instruction				#
#	fdabs(): emulates the fdabs instruction				#
#									#
# XREF **************************************************************** #
#	norm() - normalize denorm mantissa to provide EXOP		#
#	scale_to_zero_src() - make exponent. = 0; get scale factor	#
#	unf_res() - calculate underflow result				#
#	ovf_res() - calculate overflow result				#
#	res_{s,q}nan_1op() - set NAN result for monadic operation	#
#									#
# INPUT *************************************************************** #
#	a0 = pointer to extended precision source operand		#
#	d0 = rnd precision/mode						#
#									#
# OUTPUT ************************************************************** #
#	fp0 = result							#
#	fp1 = EXOP (if exception occurred)				#
#									#
# ALGORITHM ***********************************************************	#
#	Handle NANs, infinities, and zeroes as special cases. Divide	#
# norms into extended, single, and double precision.			#
#	Simply clear sign for extended precision norm. Ext prec denorm	#
# gets an EXOP created for it since it's an underflow.			#
#	Double and single precision can overflow and underflow. First,	#
# scale the operand such that the exponent is zero. Perform an "fabs"	#
# using the correct rnd mode/prec. Check to see if the original		#
# exponent would take an exception. If so, use unf_res() or ovf_res()	#
# to calculate the default result. Also, create the EXOP for the	#
# exceptional case. If no exception should occur, insert the correct	#
# result exponent and return.						#
#	Unnorms don't pass through here.				#
#									#
#########################################################################

	global		fsabs
fsabs:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&s_mode*0x10,%d0	# insert sgl precision
	bra.b		fabs

	global		fdabs
fdabs:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&d_mode*0x10,%d0	# insert dbl precision

	global		fabs
fabs:
	mov.l		%d0,L_SCR3(%a6)		# store rnd info
	mov.b		STAG(%a6),%d1
	bne.w		fabs_not_norm		# optimize on non-norm input

#
# ABSOLUTE VALUE: norms and denorms ONLY!
#
fabs_norm:
	andi.b		&0xc0,%d0		# is precision extended?
	bne.b		fabs_not_ext		# no; go handle sgl or dbl

#
# precision selected is extended. so...we can not get an underflow
# or overflow because of rounding to the correct precision. so...
# skip the scaling and unscaling...
#
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	mov.w		SRC_EX(%a0),%d1
	bclr		&15,%d1			# force absolute value
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert exponent
	fmovm.x		FP_SCR0(%a6),&0x80	# return result in fp0
	rts

#
# for an extended precision DENORM, the UNFL exception bit is set
# the accrued bit is NOT set in this instance(no inexactness!)
#
fabs_denorm:
	andi.b		&0xc0,%d0		# is precision extended?
	bne.b		fabs_not_ext		# no

	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit

	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	mov.w		SRC_EX(%a0),%d0
	bclr		&15,%d0			# clear sign
	mov.w		%d0,FP_SCR0_EX(%a6)	# insert exponent

	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0

	btst		&unfl_bit,FPCR_ENABLE(%a6) # is UNFL enabled?
	bne.b		fabs_ext_unfl_ena
	rts

#
# the input is an extended DENORM and underflow is enabled in the FPCR.
# normalize the mantissa and add the bias of 0x6000 to the resulting negative
# exponent and insert back into the operand.
#
fabs_ext_unfl_ena:
	lea		FP_SCR0(%a6),%a0	# pass: ptr to operand
	bsr.l		norm			# normalize result
	neg.w		%d0			# new exponent = -(shft val)
	addi.w		&0x6000,%d0		# add new bias to exponent
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch old sign,exp
	andi.w		&0x8000,%d1		# keep old sign
	andi.w		&0x7fff,%d0		# clear sign position
	or.w		%d1,%d0			# concat old sign, new exponent
	mov.w		%d0,FP_SCR0_EX(%a6)	# insert new exponent
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	rts

#
# operand is either single or double
#
fabs_not_ext:
	cmpi.b		%d0,&s_mode*0x10	# separate sgl/dbl prec
	bne.b		fabs_dbl

#
# operand is to be rounded to single precision
#
fabs_sgl:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	bsr.l		scale_to_zero_src	# calculate scale factor

	cmpi.l		%d0,&0x3fff-0x3f80	# will move in underflow?
	bge.w		fabs_sd_unfl		# yes; go handle underflow
	cmpi.l		%d0,&0x3fff-0x407e	# will move in overflow?
	beq.w		fabs_sd_may_ovfl	# maybe; go check
	blt.w		fabs_sd_ovfl		# yes; go handle overflow

#
# operand will NOT overflow or underflow when moved in to the fp reg file
#
fabs_sd_normal:
	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fabs.x		FP_SCR0(%a6),%fp0	# perform absolute

	fmov.l		%fpsr,%d1		# save FPSR
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fabs_sd_normal_exit:
	mov.l		%d2,-(%sp)		# save d2
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result
	mov.w		FP_SCR0_EX(%a6),%d1	# load sgn,exp
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	sub.l		%d0,%d1			# add scale factor
	andi.w		&0x8000,%d2		# keep old sign
	or.w		%d1,%d2			# concat old sign,new exp
	mov.w		%d2,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x80	# return result in fp0
	rts

#
# operand is to be rounded to double precision
#
fabs_dbl:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	bsr.l		scale_to_zero_src	# calculate scale factor

	cmpi.l		%d0,&0x3fff-0x3c00	# will move in underflow?
	bge.b		fabs_sd_unfl		# yes; go handle underflow
	cmpi.l		%d0,&0x3fff-0x43fe	# will move in overflow?
	beq.w		fabs_sd_may_ovfl	# maybe; go check
	blt.w		fabs_sd_ovfl		# yes; go handle overflow
	bra.w		fabs_sd_normal		# no; ho handle normalized op

#
# operand WILL underflow when moved in to the fp register file
#
fabs_sd_unfl:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit

	bclr		&0x7,FP_SCR0_EX(%a6)	# force absolute value

# if underflow or inexact is enabled, go calculate EXOP first.
	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0b,%d1		# is UNFL or INEX enabled?
	bne.b		fabs_sd_unfl_ena	# yes

fabs_sd_unfl_dis:
	lea		FP_SCR0(%a6),%a0	# pass: result addr
	mov.l		L_SCR3(%a6),%d1		# pass: rnd prec,mode
	bsr.l		unf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# set possible 'Z' ccode
	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0
	rts

#
# operand will underflow AND underflow is enabled.
# therefore, we must return the result rounded to extended precision.
#
fabs_sd_unfl_ena:
	mov.l		FP_SCR0_HI(%a6),FP_SCR1_HI(%a6)
	mov.l		FP_SCR0_LO(%a6),FP_SCR1_LO(%a6)
	mov.w		FP_SCR0_EX(%a6),%d1	# load current exponent

	mov.l		%d2,-(%sp)		# save d2
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# subtract scale factor
	addi.l		&0x6000,%d1		# add new bias
	andi.w		&0x7fff,%d1
	or.w		%d2,%d1			# concat new sign,new exp
	mov.w		%d1,FP_SCR1_EX(%a6)	# insert new exp
	fmovm.x		FP_SCR1(%a6),&0x40	# return EXOP in fp1
	mov.l		(%sp)+,%d2		# restore d2
	bra.b		fabs_sd_unfl_dis

#
# operand WILL overflow.
#
fabs_sd_ovfl:
	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fabs.x		FP_SCR0(%a6),%fp0	# perform absolute

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d1		# save FPSR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fabs_sd_ovfl_tst:
	or.l		&ovfl_inx_mask,USER_FPSR(%a6) # set ovfl/aovfl/ainex

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x13,%d1		# is OVFL or INEX enabled?
	bne.b		fabs_sd_ovfl_ena	# yes

#
# OVFL is not enabled; therefore, we must create the default result by
# calling ovf_res().
#
fabs_sd_ovfl_dis:
	btst		&neg_bit,FPSR_CC(%a6)	# is result negative?
	sne		%d1			# set sign param accordingly
	mov.l		L_SCR3(%a6),%d0		# pass: prec,mode
	bsr.l		ovf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# set INF,N if applicable
	fmovm.x		(%a0),&0x80		# return default result in fp0
	rts

#
# OVFL is enabled.
# the INEX2 bit has already been updated by the round to the correct precision.
# now, round to extended(and don't alter the FPSR).
#
fabs_sd_ovfl_ena:
	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	subi.l		&0x6000,%d1		# subtract bias
	andi.w		&0x7fff,%d1
	or.w		%d2,%d1			# concat sign,exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	mov.l		(%sp)+,%d2		# restore d2
	bra.b		fabs_sd_ovfl_dis

#
# the move in MAY underflow. so...
#
fabs_sd_may_ovfl:
	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fabs.x		FP_SCR0(%a6),%fp0	# perform absolute

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	fabs.x		%fp0,%fp1		# make a copy of result
	fcmp.b		%fp1,&0x2		# is |result| >= 2.b?
	fbge.w		fabs_sd_ovfl_tst	# yes; overflow has occurred

# no, it didn't overflow; we have correct result
	bra.w		fabs_sd_normal_exit

##########################################################################

#
# input is not normalized; what is it?
#
fabs_not_norm:
	cmpi.b		%d1,&DENORM		# weed out DENORM
	beq.w		fabs_denorm
	cmpi.b		%d1,&SNAN		# weed out SNAN
	beq.l		res_snan_1op
	cmpi.b		%d1,&QNAN		# weed out QNAN
	beq.l		res_qnan_1op

	fabs.x		SRC(%a0),%fp0		# force absolute value

	cmpi.b		%d1,&INF		# weed out INF
	beq.b		fabs_inf
fabs_zero:
	mov.b		&z_bmask,FPSR_CC(%a6)	# set 'Z' ccode bit
	rts
fabs_inf:
	mov.b		&inf_bmask,FPSR_CC(%a6)	# set 'I' ccode bit
	rts

#########################################################################
# XDEF ****************************************************************	#
#	fcmp(): fp compare op routine					#
#									#
# XREF ****************************************************************	#
#	res_qnan() - return QNAN result					#
#	res_snan() - return SNAN result					#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	a1 = pointer to extended precision destination operand		#
#	d0 = round prec/mode						#
#									#
# OUTPUT ************************************************************** #
#	None								#
#									#
# ALGORITHM ***********************************************************	#
#	Handle NANs and denorms as special cases. For everything else,	#
# just use the actual fcmp instruction to produce the correct condition	#
# codes.								#
#									#
#########################################################################

	global		fcmp
fcmp:
	clr.w		%d1
	mov.b		DTAG(%a6),%d1
	lsl.b		&0x3,%d1
	or.b		STAG(%a6),%d1
	bne.b		fcmp_not_norm		# optimize on non-norm input

#
# COMPARE FP OPs : NORMs, ZEROs, INFs, and "corrected" DENORMs
#
fcmp_norm:
	fmovm.x		DST(%a1),&0x80		# load dst op

	fcmp.x		%fp0,SRC(%a0)		# do compare

	fmov.l		%fpsr,%d0		# save FPSR
	rol.l		&0x8,%d0		# extract ccode bits
	mov.b		%d0,FPSR_CC(%a6)	# set ccode bits(no exc bits are set)

	rts

#
# fcmp: inputs are not both normalized; what are they?
#
fcmp_not_norm:
	mov.w		(tbl_fcmp_op.b,%pc,%d1.w*2),%d1
	jmp		(tbl_fcmp_op.b,%pc,%d1.w*1)

	swbeg		&48
tbl_fcmp_op:
	short		fcmp_norm	- tbl_fcmp_op # NORM - NORM
	short		fcmp_norm	- tbl_fcmp_op # NORM - ZERO
	short		fcmp_norm	- tbl_fcmp_op # NORM - INF
	short		fcmp_res_qnan	- tbl_fcmp_op # NORM - QNAN
	short		fcmp_nrm_dnrm	- tbl_fcmp_op # NORM - DENORM
	short		fcmp_res_snan	- tbl_fcmp_op # NORM - SNAN
	short		tbl_fcmp_op	- tbl_fcmp_op #
	short		tbl_fcmp_op	- tbl_fcmp_op #

	short		fcmp_norm	- tbl_fcmp_op # ZERO - NORM
	short		fcmp_norm	- tbl_fcmp_op # ZERO - ZERO
	short		fcmp_norm	- tbl_fcmp_op # ZERO - INF
	short		fcmp_res_qnan	- tbl_fcmp_op # ZERO - QNAN
	short		fcmp_dnrm_s	- tbl_fcmp_op # ZERO - DENORM
	short		fcmp_res_snan	- tbl_fcmp_op # ZERO - SNAN
	short		tbl_fcmp_op	- tbl_fcmp_op #
	short		tbl_fcmp_op	- tbl_fcmp_op #

	short		fcmp_norm	- tbl_fcmp_op # INF - NORM
	short		fcmp_norm	- tbl_fcmp_op # INF - ZERO
	short		fcmp_norm	- tbl_fcmp_op # INF - INF
	short		fcmp_res_qnan	- tbl_fcmp_op # INF - QNAN
	short		fcmp_dnrm_s	- tbl_fcmp_op # INF - DENORM
	short		fcmp_res_snan	- tbl_fcmp_op # INF - SNAN
	short		tbl_fcmp_op	- tbl_fcmp_op #
	short		tbl_fcmp_op	- tbl_fcmp_op #

	short		fcmp_res_qnan	- tbl_fcmp_op # QNAN - NORM
	short		fcmp_res_qnan	- tbl_fcmp_op # QNAN - ZERO
	short		fcmp_res_qnan	- tbl_fcmp_op # QNAN - INF
	short		fcmp_res_qnan	- tbl_fcmp_op # QNAN - QNAN
	short		fcmp_res_qnan	- tbl_fcmp_op # QNAN - DENORM
	short		fcmp_res_snan	- tbl_fcmp_op # QNAN - SNAN
	short		tbl_fcmp_op	- tbl_fcmp_op #
	short		tbl_fcmp_op	- tbl_fcmp_op #

	short		fcmp_dnrm_nrm	- tbl_fcmp_op # DENORM - NORM
	short		fcmp_dnrm_d	- tbl_fcmp_op # DENORM - ZERO
	short		fcmp_dnrm_d	- tbl_fcmp_op # DENORM - INF
	short		fcmp_res_qnan	- tbl_fcmp_op # DENORM - QNAN
	short		fcmp_dnrm_sd	- tbl_fcmp_op # DENORM - DENORM
	short		fcmp_res_snan	- tbl_fcmp_op # DENORM - SNAN
	short		tbl_fcmp_op	- tbl_fcmp_op #
	short		tbl_fcmp_op	- tbl_fcmp_op #

	short		fcmp_res_snan	- tbl_fcmp_op # SNAN - NORM
	short		fcmp_res_snan	- tbl_fcmp_op # SNAN - ZERO
	short		fcmp_res_snan	- tbl_fcmp_op # SNAN - INF
	short		fcmp_res_snan	- tbl_fcmp_op # SNAN - QNAN
	short		fcmp_res_snan	- tbl_fcmp_op # SNAN - DENORM
	short		fcmp_res_snan	- tbl_fcmp_op # SNAN - SNAN
	short		tbl_fcmp_op	- tbl_fcmp_op #
	short		tbl_fcmp_op	- tbl_fcmp_op #

# unlike all other functions for QNAN and SNAN, fcmp does NOT set the
# 'N' bit for a negative QNAN or SNAN input so we must squelch it here.
fcmp_res_qnan:
	bsr.l		res_qnan
	andi.b		&0xf7,FPSR_CC(%a6)
	rts
fcmp_res_snan:
	bsr.l		res_snan
	andi.b		&0xf7,FPSR_CC(%a6)
	rts

#
# DENORMs are a little more difficult.
# If you have a 2 DENORMs, then you can just force the j-bit to a one
# and use the fcmp_norm routine.
# If you have a DENORM and an INF or ZERO, just force the DENORM's j-bit to a one
# and use the fcmp_norm routine.
# If you have a DENORM and a NORM with opposite signs, then use fcmp_norm, also.
# But with a DENORM and a NORM of the same sign, the neg bit is set if the
# (1) signs are (+) and the DENORM is the dst or
# (2) signs are (-) and the DENORM is the src
#

fcmp_dnrm_s:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),%d0
	bset		&31,%d0			# DENORM src; make into small norm
	mov.l		%d0,FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	lea		FP_SCR0(%a6),%a0
	bra.w		fcmp_norm

fcmp_dnrm_d:
	mov.l		DST_EX(%a1),FP_SCR0_EX(%a6)
	mov.l		DST_HI(%a1),%d0
	bset		&31,%d0			# DENORM src; make into small norm
	mov.l		%d0,FP_SCR0_HI(%a6)
	mov.l		DST_LO(%a1),FP_SCR0_LO(%a6)
	lea		FP_SCR0(%a6),%a1
	bra.w		fcmp_norm

fcmp_dnrm_sd:
	mov.w		DST_EX(%a1),FP_SCR1_EX(%a6)
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		DST_HI(%a1),%d0
	bset		&31,%d0			# DENORM dst; make into small norm
	mov.l		%d0,FP_SCR1_HI(%a6)
	mov.l		SRC_HI(%a0),%d0
	bset		&31,%d0			# DENORM dst; make into small norm
	mov.l		%d0,FP_SCR0_HI(%a6)
	mov.l		DST_LO(%a1),FP_SCR1_LO(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	lea		FP_SCR1(%a6),%a1
	lea		FP_SCR0(%a6),%a0
	bra.w		fcmp_norm

fcmp_nrm_dnrm:
	mov.b		SRC_EX(%a0),%d0		# determine if like signs
	mov.b		DST_EX(%a1),%d1
	eor.b		%d0,%d1
	bmi.w		fcmp_dnrm_s

# signs are the same, so must determine the answer ourselves.
	tst.b		%d0			# is src op negative?
	bmi.b		fcmp_nrm_dnrm_m		# yes
	rts
fcmp_nrm_dnrm_m:
	mov.b		&neg_bmask,FPSR_CC(%a6)	# set 'Z' ccode bit
	rts

fcmp_dnrm_nrm:
	mov.b		SRC_EX(%a0),%d0		# determine if like signs
	mov.b		DST_EX(%a1),%d1
	eor.b		%d0,%d1
	bmi.w		fcmp_dnrm_d

# signs are the same, so must determine the answer ourselves.
	tst.b		%d0			# is src op negative?
	bpl.b		fcmp_dnrm_nrm_m		# no
	rts
fcmp_dnrm_nrm_m:
	mov.b		&neg_bmask,FPSR_CC(%a6)	# set 'Z' ccode bit
	rts

#########################################################################
# XDEF ****************************************************************	#
#	fsglmul(): emulates the fsglmul instruction			#
#									#
# XREF ****************************************************************	#
#	scale_to_zero_src() - scale src exponent to zero		#
#	scale_to_zero_dst() - scale dst exponent to zero		#
#	unf_res4() - return default underflow result for sglop		#
#	ovf_res() - return default overflow result			#
#	res_qnan() - return QNAN result					#
#	res_snan() - return SNAN result					#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	a1 = pointer to extended precision destination operand		#
#	d0  rnd prec,mode						#
#									#
# OUTPUT **************************************************************	#
#	fp0 = result							#
#	fp1 = EXOP (if exception occurred)				#
#									#
# ALGORITHM ***********************************************************	#
#	Handle NANs, infinities, and zeroes as special cases. Divide	#
# norms/denorms into ext/sgl/dbl precision.				#
#	For norms/denorms, scale the exponents such that a multiply	#
# instruction won't cause an exception. Use the regular fsglmul to	#
# compute a result. Check if the regular operands would have taken	#
# an exception. If so, return the default overflow/underflow result	#
# and return the EXOP if exceptions are enabled. Else, scale the	#
# result operand to the proper exponent.				#
#									#
#########################################################################

	global		fsglmul
fsglmul:
	mov.l		%d0,L_SCR3(%a6)		# store rnd info

	clr.w		%d1
	mov.b		DTAG(%a6),%d1
	lsl.b		&0x3,%d1
	or.b		STAG(%a6),%d1

	bne.w		fsglmul_not_norm	# optimize on non-norm input

fsglmul_norm:
	mov.w		DST_EX(%a1),FP_SCR1_EX(%a6)
	mov.l		DST_HI(%a1),FP_SCR1_HI(%a6)
	mov.l		DST_LO(%a1),FP_SCR1_LO(%a6)

	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)

	bsr.l		scale_to_zero_src	# scale exponent
	mov.l		%d0,-(%sp)		# save scale factor 1

	bsr.l		scale_to_zero_dst	# scale dst exponent

	add.l		(%sp)+,%d0		# SCALE_FACTOR = scale1 + scale2

	cmpi.l		%d0,&0x3fff-0x7ffe	# would result ovfl?
	beq.w		fsglmul_may_ovfl	# result may rnd to overflow
	blt.w		fsglmul_ovfl		# result will overflow

	cmpi.l		%d0,&0x3fff+0x0001	# would result unfl?
	beq.w		fsglmul_may_unfl	# result may rnd to no unfl
	bgt.w		fsglmul_unfl		# result will underflow

fsglmul_normal:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsglmul.x	FP_SCR0(%a6),%fp0	# execute sgl multiply

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fsglmul_normal_exit:
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result
	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# load {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	or.w		%d2,%d1			# concat old sign,new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x80	# return result in fp0
	rts

fsglmul_ovfl:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsglmul.x	FP_SCR0(%a6),%fp0	# execute sgl multiply

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fsglmul_ovfl_tst:

# save setting this until now because this is where fsglmul_may_ovfl may jump in
	or.l		&ovfl_inx_mask, USER_FPSR(%a6) # set ovfl/aovfl/ainex

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x13,%d1		# is OVFL or INEX enabled?
	bne.b		fsglmul_ovfl_ena	# yes

fsglmul_ovfl_dis:
	btst		&neg_bit,FPSR_CC(%a6)	# is result negative?
	sne		%d1			# set sign param accordingly
	mov.l		L_SCR3(%a6),%d0		# pass prec:rnd
	andi.b		&0x30,%d0		# force prec = ext
	bsr.l		ovf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# set INF,N if applicable
	fmovm.x		(%a0),&0x80		# return default result in fp0
	rts

fsglmul_ovfl_ena:
	fmovm.x		&0x80,FP_SCR0(%a6)	# move result to stack

	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	sub.l		%d0,%d1			# add scale factor
	subi.l		&0x6000,%d1		# subtract bias
	andi.w		&0x7fff,%d1
	andi.w		&0x8000,%d2		# keep old sign
	or.w		%d2,%d1			# concat old sign,new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	bra.b		fsglmul_ovfl_dis

fsglmul_may_ovfl:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsglmul.x	FP_SCR0(%a6),%fp0	# execute sgl multiply

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	fabs.x		%fp0,%fp1		# make a copy of result
	fcmp.b		%fp1,&0x2		# is |result| >= 2.b?
	fbge.w		fsglmul_ovfl_tst	# yes; overflow has occurred

# no, it didn't overflow; we have correct result
	bra.w		fsglmul_normal_exit

fsglmul_unfl:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit

	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		&rz_mode*0x10,%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsglmul.x	FP_SCR0(%a6),%fp0	# execute sgl multiply

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0b,%d1		# is UNFL or INEX enabled?
	bne.b		fsglmul_unfl_ena	# yes

fsglmul_unfl_dis:
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result

	lea		FP_SCR0(%a6),%a0	# pass: result addr
	mov.l		L_SCR3(%a6),%d1		# pass: rnd prec,mode
	bsr.l		unf_res4		# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# 'Z' bit may have been set
	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0
	rts

#
# UNFL is enabled.
#
fsglmul_unfl_ena:
	fmovm.x		FP_SCR1(%a6),&0x40	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsglmul.x	FP_SCR0(%a6),%fp1	# execute sgl multiply

	fmov.l		&0x0,%fpcr		# clear FPCR

	fmovm.x		&0x40,FP_SCR0(%a6)	# save result to stack
	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	addi.l		&0x6000,%d1		# add bias
	andi.w		&0x7fff,%d1
	or.w		%d2,%d1			# concat old sign,new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	bra.w		fsglmul_unfl_dis

fsglmul_may_unfl:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsglmul.x	FP_SCR0(%a6),%fp0	# execute sgl multiply

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	fabs.x		%fp0,%fp1		# make a copy of result
	fcmp.b		%fp1,&0x2		# is |result| > 2.b?
	fbgt.w		fsglmul_normal_exit	# no; no underflow occurred
	fblt.w		fsglmul_unfl		# yes; underflow occurred

#
# we still don't know if underflow occurred. result is ~ equal to 2. but,
# we don't know if the result was an underflow that rounded up to a 2 or
# a normalized number that rounded down to a 2. so, redo the entire operation
# using RZ as the rounding mode to see what the pre-rounded result is.
# this case should be relatively rare.
#
	fmovm.x		FP_SCR1(%a6),&0x40	# load dst op into fp1

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# keep rnd prec
	ori.b		&rz_mode*0x10,%d1	# insert RZ

	fmov.l		%d1,%fpcr		# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsglmul.x	FP_SCR0(%a6),%fp1	# execute sgl multiply

	fmov.l		&0x0,%fpcr		# clear FPCR
	fabs.x		%fp1			# make absolute value
	fcmp.b		%fp1,&0x2		# is |result| < 2.b?
	fbge.w		fsglmul_normal_exit	# no; no underflow occurred
	bra.w		fsglmul_unfl		# yes, underflow occurred

##############################################################################

#
# Single Precision Multiply: inputs are not both normalized; what are they?
#
fsglmul_not_norm:
	mov.w		(tbl_fsglmul_op.b,%pc,%d1.w*2),%d1
	jmp		(tbl_fsglmul_op.b,%pc,%d1.w*1)

	swbeg		&48
tbl_fsglmul_op:
	short		fsglmul_norm		- tbl_fsglmul_op # NORM x NORM
	short		fsglmul_zero		- tbl_fsglmul_op # NORM x ZERO
	short		fsglmul_inf_src		- tbl_fsglmul_op # NORM x INF
	short		fsglmul_res_qnan	- tbl_fsglmul_op # NORM x QNAN
	short		fsglmul_norm		- tbl_fsglmul_op # NORM x DENORM
	short		fsglmul_res_snan	- tbl_fsglmul_op # NORM x SNAN
	short		tbl_fsglmul_op		- tbl_fsglmul_op #
	short		tbl_fsglmul_op		- tbl_fsglmul_op #

	short		fsglmul_zero		- tbl_fsglmul_op # ZERO x NORM
	short		fsglmul_zero		- tbl_fsglmul_op # ZERO x ZERO
	short		fsglmul_res_operr	- tbl_fsglmul_op # ZERO x INF
	short		fsglmul_res_qnan	- tbl_fsglmul_op # ZERO x QNAN
	short		fsglmul_zero		- tbl_fsglmul_op # ZERO x DENORM
	short		fsglmul_res_snan	- tbl_fsglmul_op # ZERO x SNAN
	short		tbl_fsglmul_op		- tbl_fsglmul_op #
	short		tbl_fsglmul_op		- tbl_fsglmul_op #

	short		fsglmul_inf_dst		- tbl_fsglmul_op # INF x NORM
	short		fsglmul_res_operr	- tbl_fsglmul_op # INF x ZERO
	short		fsglmul_inf_dst		- tbl_fsglmul_op # INF x INF
	short		fsglmul_res_qnan	- tbl_fsglmul_op # INF x QNAN
	short		fsglmul_inf_dst		- tbl_fsglmul_op # INF x DENORM
	short		fsglmul_res_snan	- tbl_fsglmul_op # INF x SNAN
	short		tbl_fsglmul_op		- tbl_fsglmul_op #
	short		tbl_fsglmul_op		- tbl_fsglmul_op #

	short		fsglmul_res_qnan	- tbl_fsglmul_op # QNAN x NORM
	short		fsglmul_res_qnan	- tbl_fsglmul_op # QNAN x ZERO
	short		fsglmul_res_qnan	- tbl_fsglmul_op # QNAN x INF
	short		fsglmul_res_qnan	- tbl_fsglmul_op # QNAN x QNAN
	short		fsglmul_res_qnan	- tbl_fsglmul_op # QNAN x DENORM
	short		fsglmul_res_snan	- tbl_fsglmul_op # QNAN x SNAN
	short		tbl_fsglmul_op		- tbl_fsglmul_op #
	short		tbl_fsglmul_op		- tbl_fsglmul_op #

	short		fsglmul_norm		- tbl_fsglmul_op # NORM x NORM
	short		fsglmul_zero		- tbl_fsglmul_op # NORM x ZERO
	short		fsglmul_inf_src		- tbl_fsglmul_op # NORM x INF
	short		fsglmul_res_qnan	- tbl_fsglmul_op # NORM x QNAN
	short		fsglmul_norm		- tbl_fsglmul_op # NORM x DENORM
	short		fsglmul_res_snan	- tbl_fsglmul_op # NORM x SNAN
	short		tbl_fsglmul_op		- tbl_fsglmul_op #
	short		tbl_fsglmul_op		- tbl_fsglmul_op #

	short		fsglmul_res_snan	- tbl_fsglmul_op # SNAN x NORM
	short		fsglmul_res_snan	- tbl_fsglmul_op # SNAN x ZERO
	short		fsglmul_res_snan	- tbl_fsglmul_op # SNAN x INF
	short		fsglmul_res_snan	- tbl_fsglmul_op # SNAN x QNAN
	short		fsglmul_res_snan	- tbl_fsglmul_op # SNAN x DENORM
	short		fsglmul_res_snan	- tbl_fsglmul_op # SNAN x SNAN
	short		tbl_fsglmul_op		- tbl_fsglmul_op #
	short		tbl_fsglmul_op		- tbl_fsglmul_op #

fsglmul_res_operr:
	bra.l		res_operr
fsglmul_res_snan:
	bra.l		res_snan
fsglmul_res_qnan:
	bra.l		res_qnan
fsglmul_zero:
	bra.l		fmul_zero
fsglmul_inf_src:
	bra.l		fmul_inf_src
fsglmul_inf_dst:
	bra.l		fmul_inf_dst

#########################################################################
# XDEF ****************************************************************	#
#	fsgldiv(): emulates the fsgldiv instruction			#
#									#
# XREF ****************************************************************	#
#	scale_to_zero_src() - scale src exponent to zero		#
#	scale_to_zero_dst() - scale dst exponent to zero		#
#	unf_res4() - return default underflow result for sglop		#
#	ovf_res() - return default overflow result			#
#	res_qnan() - return QNAN result					#
#	res_snan() - return SNAN result					#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	a1 = pointer to extended precision destination operand		#
#	d0  rnd prec,mode						#
#									#
# OUTPUT **************************************************************	#
#	fp0 = result							#
#	fp1 = EXOP (if exception occurred)				#
#									#
# ALGORITHM ***********************************************************	#
#	Handle NANs, infinities, and zeroes as special cases. Divide	#
# norms/denorms into ext/sgl/dbl precision.				#
#	For norms/denorms, scale the exponents such that a divide	#
# instruction won't cause an exception. Use the regular fsgldiv to	#
# compute a result. Check if the regular operands would have taken	#
# an exception. If so, return the default overflow/underflow result	#
# and return the EXOP if exceptions are enabled. Else, scale the	#
# result operand to the proper exponent.				#
#									#
#########################################################################

	global		fsgldiv
fsgldiv:
	mov.l		%d0,L_SCR3(%a6)		# store rnd info

	clr.w		%d1
	mov.b		DTAG(%a6),%d1
	lsl.b		&0x3,%d1
	or.b		STAG(%a6),%d1		# combine src tags

	bne.w		fsgldiv_not_norm	# optimize on non-norm input

#
# DIVIDE: NORMs and DENORMs ONLY!
#
fsgldiv_norm:
	mov.w		DST_EX(%a1),FP_SCR1_EX(%a6)
	mov.l		DST_HI(%a1),FP_SCR1_HI(%a6)
	mov.l		DST_LO(%a1),FP_SCR1_LO(%a6)

	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)

	bsr.l		scale_to_zero_src	# calculate scale factor 1
	mov.l		%d0,-(%sp)		# save scale factor 1

	bsr.l		scale_to_zero_dst	# calculate scale factor 2

	neg.l		(%sp)			# S.F. = scale1 - scale2
	add.l		%d0,(%sp)

	mov.w		2+L_SCR3(%a6),%d1	# fetch precision,mode
	lsr.b		&0x6,%d1
	mov.l		(%sp)+,%d0
	cmpi.l		%d0,&0x3fff-0x7ffe
	ble.w		fsgldiv_may_ovfl

	cmpi.l		%d0,&0x3fff-0x0000	# will result underflow?
	beq.w		fsgldiv_may_unfl	# maybe
	bgt.w		fsgldiv_unfl		# yes; go handle underflow

fsgldiv_normal:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# save FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsgldiv.x	FP_SCR0(%a6),%fp0	# perform sgl divide

	fmov.l		%fpsr,%d1		# save FPSR
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fsgldiv_normal_exit:
	fmovm.x		&0x80,FP_SCR0(%a6)	# store result on stack
	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# load {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	or.w		%d2,%d1			# concat old sign,new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x80	# return result in fp0
	rts

fsgldiv_may_ovfl:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# set FPSR

	fsgldiv.x	FP_SCR0(%a6),%fp0	# execute divide

	fmov.l		%fpsr,%d1
	fmov.l		&0x0,%fpcr

	or.l		%d1,USER_FPSR(%a6)	# save INEX,N

	fmovm.x		&0x01,-(%sp)		# save result to stack
	mov.w		(%sp),%d1		# fetch new exponent
	add.l		&0xc,%sp		# clear result
	andi.l		&0x7fff,%d1		# strip sign
	sub.l		%d0,%d1			# add scale factor
	cmp.l		%d1,&0x7fff		# did divide overflow?
	blt.b		fsgldiv_normal_exit

fsgldiv_ovfl_tst:
	or.w		&ovfl_inx_mask,2+USER_FPSR(%a6) # set ovfl/aovfl/ainex

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x13,%d1		# is OVFL or INEX enabled?
	bne.b		fsgldiv_ovfl_ena	# yes

fsgldiv_ovfl_dis:
	btst		&neg_bit,FPSR_CC(%a6)	# is result negative
	sne		%d1			# set sign param accordingly
	mov.l		L_SCR3(%a6),%d0		# pass prec:rnd
	andi.b		&0x30,%d0		# kill precision
	bsr.l		ovf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# set INF if applicable
	fmovm.x		(%a0),&0x80		# return default result in fp0
	rts

fsgldiv_ovfl_ena:
	fmovm.x		&0x80,FP_SCR0(%a6)	# move result to stack

	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	subi.l		&0x6000,%d1		# subtract new bias
	andi.w		&0x7fff,%d1		# clear ms bit
	or.w		%d2,%d1			# concat old sign,new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	bra.b		fsgldiv_ovfl_dis

fsgldiv_unfl:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit

	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		&rz_mode*0x10,%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsgldiv.x	FP_SCR0(%a6),%fp0	# execute sgl divide

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0b,%d1		# is UNFL or INEX enabled?
	bne.b		fsgldiv_unfl_ena	# yes

fsgldiv_unfl_dis:
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result

	lea		FP_SCR0(%a6),%a0	# pass: result addr
	mov.l		L_SCR3(%a6),%d1		# pass: rnd prec,mode
	bsr.l		unf_res4		# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# 'Z' bit may have been set
	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0
	rts

#
# UNFL is enabled.
#
fsgldiv_unfl_ena:
	fmovm.x		FP_SCR1(%a6),&0x40	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsgldiv.x	FP_SCR0(%a6),%fp1	# execute sgl divide

	fmov.l		&0x0,%fpcr		# clear FPCR

	fmovm.x		&0x40,FP_SCR0(%a6)	# save result to stack
	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	addi.l		&0x6000,%d1		# add bias
	andi.w		&0x7fff,%d1		# clear top bit
	or.w		%d2,%d1			# concat old sign, new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	bra.b		fsgldiv_unfl_dis

#
# the divide operation MAY underflow:
#
fsgldiv_may_unfl:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsgldiv.x	FP_SCR0(%a6),%fp0	# execute sgl divide

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	fabs.x		%fp0,%fp1		# make a copy of result
	fcmp.b		%fp1,&0x1		# is |result| > 1.b?
	fbgt.w		fsgldiv_normal_exit	# no; no underflow occurred
	fblt.w		fsgldiv_unfl		# yes; underflow occurred

#
# we still don't know if underflow occurred. result is ~ equal to 1. but,
# we don't know if the result was an underflow that rounded up to a 1
# or a normalized number that rounded down to a 1. so, redo the entire
# operation using RZ as the rounding mode to see what the pre-rounded
# result is. this case should be relatively rare.
#
	fmovm.x		FP_SCR1(%a6),&0x40	# load dst op into %fp1

	clr.l		%d1			# clear scratch register
	ori.b		&rz_mode*0x10,%d1	# force RZ rnd mode

	fmov.l		%d1,%fpcr		# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsgldiv.x	FP_SCR0(%a6),%fp1	# execute sgl divide

	fmov.l		&0x0,%fpcr		# clear FPCR
	fabs.x		%fp1			# make absolute value
	fcmp.b		%fp1,&0x1		# is |result| < 1.b?
	fbge.w		fsgldiv_normal_exit	# no; no underflow occurred
	bra.w		fsgldiv_unfl		# yes; underflow occurred

############################################################################

#
# Divide: inputs are not both normalized; what are they?
#
fsgldiv_not_norm:
	mov.w		(tbl_fsgldiv_op.b,%pc,%d1.w*2),%d1
	jmp		(tbl_fsgldiv_op.b,%pc,%d1.w*1)

	swbeg		&48
tbl_fsgldiv_op:
	short		fsgldiv_norm		- tbl_fsgldiv_op # NORM / NORM
	short		fsgldiv_inf_load	- tbl_fsgldiv_op # NORM / ZERO
	short		fsgldiv_zero_load	- tbl_fsgldiv_op # NORM / INF
	short		fsgldiv_res_qnan	- tbl_fsgldiv_op # NORM / QNAN
	short		fsgldiv_norm		- tbl_fsgldiv_op # NORM / DENORM
	short		fsgldiv_res_snan	- tbl_fsgldiv_op # NORM / SNAN
	short		tbl_fsgldiv_op		- tbl_fsgldiv_op #
	short		tbl_fsgldiv_op		- tbl_fsgldiv_op #

	short		fsgldiv_zero_load	- tbl_fsgldiv_op # ZERO / NORM
	short		fsgldiv_res_operr	- tbl_fsgldiv_op # ZERO / ZERO
	short		fsgldiv_zero_load	- tbl_fsgldiv_op # ZERO / INF
	short		fsgldiv_res_qnan	- tbl_fsgldiv_op # ZERO / QNAN
	short		fsgldiv_zero_load	- tbl_fsgldiv_op # ZERO / DENORM
	short		fsgldiv_res_snan	- tbl_fsgldiv_op # ZERO / SNAN
	short		tbl_fsgldiv_op		- tbl_fsgldiv_op #
	short		tbl_fsgldiv_op		- tbl_fsgldiv_op #

	short		fsgldiv_inf_dst		- tbl_fsgldiv_op # INF / NORM
	short		fsgldiv_inf_dst		- tbl_fsgldiv_op # INF / ZERO
	short		fsgldiv_res_operr	- tbl_fsgldiv_op # INF / INF
	short		fsgldiv_res_qnan	- tbl_fsgldiv_op # INF / QNAN
	short		fsgldiv_inf_dst		- tbl_fsgldiv_op # INF / DENORM
	short		fsgldiv_res_snan	- tbl_fsgldiv_op # INF / SNAN
	short		tbl_fsgldiv_op		- tbl_fsgldiv_op #
	short		tbl_fsgldiv_op		- tbl_fsgldiv_op #

	short		fsgldiv_res_qnan	- tbl_fsgldiv_op # QNAN / NORM
	short		fsgldiv_res_qnan	- tbl_fsgldiv_op # QNAN / ZERO
	short		fsgldiv_res_qnan	- tbl_fsgldiv_op # QNAN / INF
	short		fsgldiv_res_qnan	- tbl_fsgldiv_op # QNAN / QNAN
	short		fsgldiv_res_qnan	- tbl_fsgldiv_op # QNAN / DENORM
	short		fsgldiv_res_snan	- tbl_fsgldiv_op # QNAN / SNAN
	short		tbl_fsgldiv_op		- tbl_fsgldiv_op #
	short		tbl_fsgldiv_op		- tbl_fsgldiv_op #

	short		fsgldiv_norm		- tbl_fsgldiv_op # DENORM / NORM
	short		fsgldiv_inf_load	- tbl_fsgldiv_op # DENORM / ZERO
	short		fsgldiv_zero_load	- tbl_fsgldiv_op # DENORM / INF
	short		fsgldiv_res_qnan	- tbl_fsgldiv_op # DENORM / QNAN
	short		fsgldiv_norm		- tbl_fsgldiv_op # DENORM / DENORM
	short		fsgldiv_res_snan	- tbl_fsgldiv_op # DENORM / SNAN
	short		tbl_fsgldiv_op		- tbl_fsgldiv_op #
	short		tbl_fsgldiv_op		- tbl_fsgldiv_op #

	short		fsgldiv_res_snan	- tbl_fsgldiv_op # SNAN / NORM
	short		fsgldiv_res_snan	- tbl_fsgldiv_op # SNAN / ZERO
	short		fsgldiv_res_snan	- tbl_fsgldiv_op # SNAN / INF
	short		fsgldiv_res_snan	- tbl_fsgldiv_op # SNAN / QNAN
	short		fsgldiv_res_snan	- tbl_fsgldiv_op # SNAN / DENORM
	short		fsgldiv_res_snan	- tbl_fsgldiv_op # SNAN / SNAN
	short		tbl_fsgldiv_op		- tbl_fsgldiv_op #
	short		tbl_fsgldiv_op		- tbl_fsgldiv_op #

fsgldiv_res_qnan:
	bra.l		res_qnan
fsgldiv_res_snan:
	bra.l		res_snan
fsgldiv_res_operr:
	bra.l		res_operr
fsgldiv_inf_load:
	bra.l		fdiv_inf_load
fsgldiv_zero_load:
	bra.l		fdiv_zero_load
fsgldiv_inf_dst:
	bra.l		fdiv_inf_dst

#########################################################################
# XDEF ****************************************************************	#
#	fadd(): emulates the fadd instruction				#
#	fsadd(): emulates the fadd instruction				#
#	fdadd(): emulates the fdadd instruction				#
#									#
# XREF ****************************************************************	#
#	addsub_scaler2() - scale the operands so they won't take exc	#
#	ovf_res() - return default overflow result			#
#	unf_res() - return default underflow result			#
#	res_qnan() - set QNAN result					#
#	res_snan() - set SNAN result					#
#	res_operr() - set OPERR result					#
#	scale_to_zero_src() - set src operand exponent equal to zero	#
#	scale_to_zero_dst() - set dst operand exponent equal to zero	#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	a1 = pointer to extended precision destination operand		#
#									#
# OUTPUT **************************************************************	#
#	fp0 = result							#
#	fp1 = EXOP (if exception occurred)				#
#									#
# ALGORITHM ***********************************************************	#
#	Handle NANs, infinities, and zeroes as special cases. Divide	#
# norms into extended, single, and double precision.			#
#	Do addition after scaling exponents such that exception won't	#
# occur. Then, check result exponent to see if exception would have	#
# occurred. If so, return default result and maybe EXOP. Else, insert	#
# the correct result exponent and return. Set FPSR bits as appropriate.	#
#									#
#########################################################################

	global		fsadd
fsadd:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&s_mode*0x10,%d0	# insert sgl prec
	bra.b		fadd

	global		fdadd
fdadd:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&d_mode*0x10,%d0	# insert dbl prec

	global		fadd
fadd:
	mov.l		%d0,L_SCR3(%a6)		# store rnd info

	clr.w		%d1
	mov.b		DTAG(%a6),%d1
	lsl.b		&0x3,%d1
	or.b		STAG(%a6),%d1		# combine src tags

	bne.w		fadd_not_norm		# optimize on non-norm input

#
# ADD: norms and denorms
#
fadd_norm:
	bsr.l		addsub_scaler2		# scale exponents

fadd_zero_entry:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fadd.x		FP_SCR0(%a6),%fp0	# execute add

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d1		# fetch INEX2,N,Z

	or.l		%d1,USER_FPSR(%a6)	# save exc and ccode bits

	fbeq.w		fadd_zero_exit		# if result is zero, end now

	mov.l		%d2,-(%sp)		# save d2

	fmovm.x		&0x01,-(%sp)		# save result to stack

	mov.w		2+L_SCR3(%a6),%d1
	lsr.b		&0x6,%d1

	mov.w		(%sp),%d2		# fetch new sign, exp
	andi.l		&0x7fff,%d2		# strip sign
	sub.l		%d0,%d2			# add scale factor

	cmp.l		%d2,(tbl_fadd_ovfl.b,%pc,%d1.w*4) # is it an overflow?
	bge.b		fadd_ovfl		# yes

	cmp.l		%d2,(tbl_fadd_unfl.b,%pc,%d1.w*4) # is it an underflow?
	blt.w		fadd_unfl		# yes
	beq.w		fadd_may_unfl		# maybe; go find out

fadd_normal:
	mov.w		(%sp),%d1
	andi.w		&0x8000,%d1		# keep sign
	or.w		%d2,%d1			# concat sign,new exp
	mov.w		%d1,(%sp)		# insert new exponent

	fmovm.x		(%sp)+,&0x80		# return result in fp0

	mov.l		(%sp)+,%d2		# restore d2
	rts

fadd_zero_exit:
#	fmov.s		&0x00000000,%fp0	# return zero in fp0
	rts

tbl_fadd_ovfl:
	long		0x7fff			# ext ovfl
	long		0x407f			# sgl ovfl
	long		0x43ff			# dbl ovfl

tbl_fadd_unfl:
	long	        0x0000			# ext unfl
	long		0x3f81			# sgl unfl
	long		0x3c01			# dbl unfl

fadd_ovfl:
	or.l		&ovfl_inx_mask,USER_FPSR(%a6) # set ovfl/aovfl/ainex

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x13,%d1		# is OVFL or INEX enabled?
	bne.b		fadd_ovfl_ena		# yes

	add.l		&0xc,%sp
fadd_ovfl_dis:
	btst		&neg_bit,FPSR_CC(%a6)	# is result negative?
	sne		%d1			# set sign param accordingly
	mov.l		L_SCR3(%a6),%d0		# pass prec:rnd
	bsr.l		ovf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# set INF,N if applicable
	fmovm.x		(%a0),&0x80		# return default result in fp0
	mov.l		(%sp)+,%d2		# restore d2
	rts

fadd_ovfl_ena:
	mov.b		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# is precision extended?
	bne.b		fadd_ovfl_ena_sd	# no; prec = sgl or dbl

fadd_ovfl_ena_cont:
	mov.w		(%sp),%d1
	andi.w		&0x8000,%d1		# keep sign
	subi.l		&0x6000,%d2		# add extra bias
	andi.w		&0x7fff,%d2
	or.w		%d2,%d1			# concat sign,new exp
	mov.w		%d1,(%sp)		# insert new exponent

	fmovm.x		(%sp)+,&0x40		# return EXOP in fp1
	bra.b		fadd_ovfl_dis

fadd_ovfl_ena_sd:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0x30,%d1		# keep rnd mode
	fmov.l		%d1,%fpcr		# set FPCR

	fadd.x		FP_SCR0(%a6),%fp0	# execute add

	fmov.l		&0x0,%fpcr		# clear FPCR

	add.l		&0xc,%sp
	fmovm.x		&0x01,-(%sp)
	bra.b		fadd_ovfl_ena_cont

fadd_unfl:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit

	add.l		&0xc,%sp

	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		&rz_mode*0x10,%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fadd.x		FP_SCR0(%a6),%fp0	# execute add

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d1		# save status

	or.l		%d1,USER_FPSR(%a6)	# save INEX,N

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0b,%d1		# is UNFL or INEX enabled?
	bne.b		fadd_unfl_ena		# yes

fadd_unfl_dis:
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result

	lea		FP_SCR0(%a6),%a0	# pass: result addr
	mov.l		L_SCR3(%a6),%d1		# pass: rnd prec,mode
	bsr.l		unf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# 'Z' bit may have been set
	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0
	mov.l		(%sp)+,%d2		# restore d2
	rts

fadd_unfl_ena:
	fmovm.x		FP_SCR1(%a6),&0x40	# load dst op

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# is precision extended?
	bne.b		fadd_unfl_ena_sd	# no; sgl or dbl

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

fadd_unfl_ena_cont:
	fmov.l		&0x0,%fpsr		# clear FPSR

	fadd.x		FP_SCR0(%a6),%fp1	# execute multiply

	fmov.l		&0x0,%fpcr		# clear FPCR

	fmovm.x		&0x40,FP_SCR0(%a6)	# save result to stack
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	addi.l		&0x6000,%d1		# add new bias
	andi.w		&0x7fff,%d1		# clear top bit
	or.w		%d2,%d1			# concat sign,new exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	bra.w		fadd_unfl_dis

fadd_unfl_ena_sd:
	mov.l		L_SCR3(%a6),%d1
	andi.b		&0x30,%d1		# use only rnd mode
	fmov.l		%d1,%fpcr		# set FPCR

	bra.b		fadd_unfl_ena_cont

#
# result is equal to the smallest normalized number in the selected precision
# if the precision is extended, this result could not have come from an
# underflow that rounded up.
#
fadd_may_unfl:
	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1
	beq.w		fadd_normal		# yes; no underflow occurred

	mov.l		0x4(%sp),%d1		# extract hi(man)
	cmpi.l		%d1,&0x80000000		# is hi(man) = 0x80000000?
	bne.w		fadd_normal		# no; no underflow occurred

	tst.l		0x8(%sp)		# is lo(man) = 0x0?
	bne.w		fadd_normal		# no; no underflow occurred

	btst		&inex2_bit,FPSR_EXCEPT(%a6) # is INEX2 set?
	beq.w		fadd_normal		# no; no underflow occurred

#
# ok, so now the result has a exponent equal to the smallest normalized
# exponent for the selected precision. also, the mantissa is equal to
# 0x8000000000000000 and this mantissa is the result of rounding non-zero
# g,r,s.
# now, we must determine whether the pre-rounded result was an underflow
# rounded "up" or a normalized number rounded "down".
# so, we do this be re-executing the add using RZ as the rounding mode and
# seeing if the new result is smaller or equal to the current result.
#
	fmovm.x		FP_SCR1(%a6),&0x40	# load dst op into fp1

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# keep rnd prec
	ori.b		&rz_mode*0x10,%d1	# insert rnd mode
	fmov.l		%d1,%fpcr		# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fadd.x		FP_SCR0(%a6),%fp1	# execute add

	fmov.l		&0x0,%fpcr		# clear FPCR

	fabs.x		%fp0			# compare absolute values
	fabs.x		%fp1
	fcmp.x		%fp0,%fp1		# is first result > second?

	fbgt.w		fadd_unfl		# yes; it's an underflow
	bra.w		fadd_normal		# no; it's not an underflow

##########################################################################

#
# Add: inputs are not both normalized; what are they?
#
fadd_not_norm:
	mov.w		(tbl_fadd_op.b,%pc,%d1.w*2),%d1
	jmp		(tbl_fadd_op.b,%pc,%d1.w*1)

	swbeg		&48
tbl_fadd_op:
	short		fadd_norm	- tbl_fadd_op # NORM + NORM
	short		fadd_zero_src	- tbl_fadd_op # NORM + ZERO
	short		fadd_inf_src	- tbl_fadd_op # NORM + INF
	short		fadd_res_qnan	- tbl_fadd_op # NORM + QNAN
	short		fadd_norm	- tbl_fadd_op # NORM + DENORM
	short		fadd_res_snan	- tbl_fadd_op # NORM + SNAN
	short		tbl_fadd_op	- tbl_fadd_op #
	short		tbl_fadd_op	- tbl_fadd_op #

	short		fadd_zero_dst	- tbl_fadd_op # ZERO + NORM
	short		fadd_zero_2	- tbl_fadd_op # ZERO + ZERO
	short		fadd_inf_src	- tbl_fadd_op # ZERO + INF
	short		fadd_res_qnan	- tbl_fadd_op # NORM + QNAN
	short		fadd_zero_dst	- tbl_fadd_op # ZERO + DENORM
	short		fadd_res_snan	- tbl_fadd_op # NORM + SNAN
	short		tbl_fadd_op	- tbl_fadd_op #
	short		tbl_fadd_op	- tbl_fadd_op #

	short		fadd_inf_dst	- tbl_fadd_op # INF + NORM
	short		fadd_inf_dst	- tbl_fadd_op # INF + ZERO
	short		fadd_inf_2	- tbl_fadd_op # INF + INF
	short		fadd_res_qnan	- tbl_fadd_op # NORM + QNAN
	short		fadd_inf_dst	- tbl_fadd_op # INF + DENORM
	short		fadd_res_snan	- tbl_fadd_op # NORM + SNAN
	short		tbl_fadd_op	- tbl_fadd_op #
	short		tbl_fadd_op	- tbl_fadd_op #

	short		fadd_res_qnan	- tbl_fadd_op # QNAN + NORM
	short		fadd_res_qnan	- tbl_fadd_op # QNAN + ZERO
	short		fadd_res_qnan	- tbl_fadd_op # QNAN + INF
	short		fadd_res_qnan	- tbl_fadd_op # QNAN + QNAN
	short		fadd_res_qnan	- tbl_fadd_op # QNAN + DENORM
	short		fadd_res_snan	- tbl_fadd_op # QNAN + SNAN
	short		tbl_fadd_op	- tbl_fadd_op #
	short		tbl_fadd_op	- tbl_fadd_op #

	short		fadd_norm	- tbl_fadd_op # DENORM + NORM
	short		fadd_zero_src	- tbl_fadd_op # DENORM + ZERO
	short		fadd_inf_src	- tbl_fadd_op # DENORM + INF
	short		fadd_res_qnan	- tbl_fadd_op # NORM + QNAN
	short		fadd_norm	- tbl_fadd_op # DENORM + DENORM
	short		fadd_res_snan	- tbl_fadd_op # NORM + SNAN
	short		tbl_fadd_op	- tbl_fadd_op #
	short		tbl_fadd_op	- tbl_fadd_op #

	short		fadd_res_snan	- tbl_fadd_op # SNAN + NORM
	short		fadd_res_snan	- tbl_fadd_op # SNAN + ZERO
	short		fadd_res_snan	- tbl_fadd_op # SNAN + INF
	short		fadd_res_snan	- tbl_fadd_op # SNAN + QNAN
	short		fadd_res_snan	- tbl_fadd_op # SNAN + DENORM
	short		fadd_res_snan	- tbl_fadd_op # SNAN + SNAN
	short		tbl_fadd_op	- tbl_fadd_op #
	short		tbl_fadd_op	- tbl_fadd_op #

fadd_res_qnan:
	bra.l		res_qnan
fadd_res_snan:
	bra.l		res_snan

#
# both operands are ZEROes
#
fadd_zero_2:
	mov.b		SRC_EX(%a0),%d0		# are the signs opposite
	mov.b		DST_EX(%a1),%d1
	eor.b		%d0,%d1
	bmi.w		fadd_zero_2_chk_rm	# weed out (-ZERO)+(+ZERO)

# the signs are the same. so determine whether they are positive or negative
# and return the appropriately signed zero.
	tst.b		%d0			# are ZEROes positive or negative?
	bmi.b		fadd_zero_rm		# negative
	fmov.s		&0x00000000,%fp0	# return +ZERO
	mov.b		&z_bmask,FPSR_CC(%a6)	# set Z
	rts

#
# the ZEROes have opposite signs:
# - therefore, we return +ZERO if the rounding modes are RN,RZ, or RP.
# - -ZERO is returned in the case of RM.
#
fadd_zero_2_chk_rm:
	mov.b		3+L_SCR3(%a6),%d1
	andi.b		&0x30,%d1		# extract rnd mode
	cmpi.b		%d1,&rm_mode*0x10	# is rnd mode == RM?
	beq.b		fadd_zero_rm		# yes
	fmov.s		&0x00000000,%fp0	# return +ZERO
	mov.b		&z_bmask,FPSR_CC(%a6)	# set Z
	rts

fadd_zero_rm:
	fmov.s		&0x80000000,%fp0	# return -ZERO
	mov.b		&neg_bmask+z_bmask,FPSR_CC(%a6) # set NEG/Z
	rts

#
# one operand is a ZERO and the other is a DENORM or NORM. scale
# the DENORM or NORM and jump to the regular fadd routine.
#
fadd_zero_dst:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	bsr.l		scale_to_zero_src	# scale the operand
	clr.w		FP_SCR1_EX(%a6)
	clr.l		FP_SCR1_HI(%a6)
	clr.l		FP_SCR1_LO(%a6)
	bra.w		fadd_zero_entry		# go execute fadd

fadd_zero_src:
	mov.w		DST_EX(%a1),FP_SCR1_EX(%a6)
	mov.l		DST_HI(%a1),FP_SCR1_HI(%a6)
	mov.l		DST_LO(%a1),FP_SCR1_LO(%a6)
	bsr.l		scale_to_zero_dst	# scale the operand
	clr.w		FP_SCR0_EX(%a6)
	clr.l		FP_SCR0_HI(%a6)
	clr.l		FP_SCR0_LO(%a6)
	bra.w		fadd_zero_entry		# go execute fadd

#
# both operands are INFs. an OPERR will result if the INFs have
# different signs. else, an INF of the same sign is returned
#
fadd_inf_2:
	mov.b		SRC_EX(%a0),%d0		# exclusive or the signs
	mov.b		DST_EX(%a1),%d1
	eor.b		%d1,%d0
	bmi.l		res_operr		# weed out (-INF)+(+INF)

# ok, so it's not an OPERR. but, we do have to remember to return the
# src INF since that's where the 881/882 gets the j-bit from...

#
# operands are INF and one of {ZERO, INF, DENORM, NORM}
#
fadd_inf_src:
	fmovm.x		SRC(%a0),&0x80		# return src INF
	tst.b		SRC_EX(%a0)		# is INF positive?
	bpl.b		fadd_inf_done		# yes; we're done
	mov.b		&neg_bmask+inf_bmask,FPSR_CC(%a6) # set INF/NEG
	rts

#
# operands are INF and one of {ZERO, INF, DENORM, NORM}
#
fadd_inf_dst:
	fmovm.x		DST(%a1),&0x80		# return dst INF
	tst.b		DST_EX(%a1)		# is INF positive?
	bpl.b		fadd_inf_done		# yes; we're done
	mov.b		&neg_bmask+inf_bmask,FPSR_CC(%a6) # set INF/NEG
	rts

fadd_inf_done:
	mov.b		&inf_bmask,FPSR_CC(%a6) # set INF
	rts

#########################################################################
# XDEF ****************************************************************	#
#	fsub(): emulates the fsub instruction				#
#	fssub(): emulates the fssub instruction				#
#	fdsub(): emulates the fdsub instruction				#
#									#
# XREF ****************************************************************	#
#	addsub_scaler2() - scale the operands so they won't take exc	#
#	ovf_res() - return default overflow result			#
#	unf_res() - return default underflow result			#
#	res_qnan() - set QNAN result					#
#	res_snan() - set SNAN result					#
#	res_operr() - set OPERR result					#
#	scale_to_zero_src() - set src operand exponent equal to zero	#
#	scale_to_zero_dst() - set dst operand exponent equal to zero	#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	a1 = pointer to extended precision destination operand		#
#									#
# OUTPUT **************************************************************	#
#	fp0 = result							#
#	fp1 = EXOP (if exception occurred)				#
#									#
# ALGORITHM ***********************************************************	#
#	Handle NANs, infinities, and zeroes as special cases. Divide	#
# norms into extended, single, and double precision.			#
#	Do subtraction after scaling exponents such that exception won't#
# occur. Then, check result exponent to see if exception would have	#
# occurred. If so, return default result and maybe EXOP. Else, insert	#
# the correct result exponent and return. Set FPSR bits as appropriate.	#
#									#
#########################################################################

	global		fssub
fssub:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&s_mode*0x10,%d0	# insert sgl prec
	bra.b		fsub

	global		fdsub
fdsub:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&d_mode*0x10,%d0	# insert dbl prec

	global		fsub
fsub:
	mov.l		%d0,L_SCR3(%a6)		# store rnd info

	clr.w		%d1
	mov.b		DTAG(%a6),%d1
	lsl.b		&0x3,%d1
	or.b		STAG(%a6),%d1		# combine src tags

	bne.w		fsub_not_norm		# optimize on non-norm input

#
# SUB: norms and denorms
#
fsub_norm:
	bsr.l		addsub_scaler2		# scale exponents

fsub_zero_entry:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fsub.x		FP_SCR0(%a6),%fp0	# execute subtract

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d1		# fetch INEX2, N, Z

	or.l		%d1,USER_FPSR(%a6)	# save exc and ccode bits

	fbeq.w		fsub_zero_exit		# if result zero, end now

	mov.l		%d2,-(%sp)		# save d2

	fmovm.x		&0x01,-(%sp)		# save result to stack

	mov.w		2+L_SCR3(%a6),%d1
	lsr.b		&0x6,%d1

	mov.w		(%sp),%d2		# fetch new exponent
	andi.l		&0x7fff,%d2		# strip sign
	sub.l		%d0,%d2			# add scale factor

	cmp.l		%d2,(tbl_fsub_ovfl.b,%pc,%d1.w*4) # is it an overflow?
	bge.b		fsub_ovfl		# yes

	cmp.l		%d2,(tbl_fsub_unfl.b,%pc,%d1.w*4) # is it an underflow?
	blt.w		fsub_unfl		# yes
	beq.w		fsub_may_unfl		# maybe; go find out

fsub_normal:
	mov.w		(%sp),%d1
	andi.w		&0x8000,%d1		# keep sign
	or.w		%d2,%d1			# insert new exponent
	mov.w		%d1,(%sp)		# insert new exponent

	fmovm.x		(%sp)+,&0x80		# return result in fp0

	mov.l		(%sp)+,%d2		# restore d2
	rts

fsub_zero_exit:
#	fmov.s		&0x00000000,%fp0	# return zero in fp0
	rts

tbl_fsub_ovfl:
	long		0x7fff			# ext ovfl
	long		0x407f			# sgl ovfl
	long		0x43ff			# dbl ovfl

tbl_fsub_unfl:
	long	        0x0000			# ext unfl
	long		0x3f81			# sgl unfl
	long		0x3c01			# dbl unfl

fsub_ovfl:
	or.l		&ovfl_inx_mask,USER_FPSR(%a6) # set ovfl/aovfl/ainex

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x13,%d1		# is OVFL or INEX enabled?
	bne.b		fsub_ovfl_ena		# yes

	add.l		&0xc,%sp
fsub_ovfl_dis:
	btst		&neg_bit,FPSR_CC(%a6)	# is result negative?
	sne		%d1			# set sign param accordingly
	mov.l		L_SCR3(%a6),%d0		# pass prec:rnd
	bsr.l		ovf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# set INF,N if applicable
	fmovm.x		(%a0),&0x80		# return default result in fp0
	mov.l		(%sp)+,%d2		# restore d2
	rts

fsub_ovfl_ena:
	mov.b		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# is precision extended?
	bne.b		fsub_ovfl_ena_sd	# no

fsub_ovfl_ena_cont:
	mov.w		(%sp),%d1		# fetch {sgn,exp}
	andi.w		&0x8000,%d1		# keep sign
	subi.l		&0x6000,%d2		# subtract new bias
	andi.w		&0x7fff,%d2		# clear top bit
	or.w		%d2,%d1			# concat sign,exp
	mov.w		%d1,(%sp)		# insert new exponent

	fmovm.x		(%sp)+,&0x40		# return EXOP in fp1
	bra.b		fsub_ovfl_dis

fsub_ovfl_ena_sd:
	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0x30,%d1		# clear rnd prec
	fmov.l		%d1,%fpcr		# set FPCR

	fsub.x		FP_SCR0(%a6),%fp0	# execute subtract

	fmov.l		&0x0,%fpcr		# clear FPCR

	add.l		&0xc,%sp
	fmovm.x		&0x01,-(%sp)
	bra.b		fsub_ovfl_ena_cont

fsub_unfl:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit

	add.l		&0xc,%sp

	fmovm.x		FP_SCR1(%a6),&0x80	# load dst op

	fmov.l		&rz_mode*0x10,%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsub.x		FP_SCR0(%a6),%fp0	# execute subtract

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d1		# save status

	or.l		%d1,USER_FPSR(%a6)

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0b,%d1		# is UNFL or INEX enabled?
	bne.b		fsub_unfl_ena		# yes

fsub_unfl_dis:
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result

	lea		FP_SCR0(%a6),%a0	# pass: result addr
	mov.l		L_SCR3(%a6),%d1		# pass: rnd prec,mode
	bsr.l		unf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# 'Z' may have been set
	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0
	mov.l		(%sp)+,%d2		# restore d2
	rts

fsub_unfl_ena:
	fmovm.x		FP_SCR1(%a6),&0x40

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# is precision extended?
	bne.b		fsub_unfl_ena_sd	# no

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

fsub_unfl_ena_cont:
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsub.x		FP_SCR0(%a6),%fp1	# execute subtract

	fmov.l		&0x0,%fpcr		# clear FPCR

	fmovm.x		&0x40,FP_SCR0(%a6)	# store result to stack
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	addi.l		&0x6000,%d1		# subtract new bias
	andi.w		&0x7fff,%d1		# clear top bit
	or.w		%d2,%d1			# concat sgn,exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	bra.w		fsub_unfl_dis

fsub_unfl_ena_sd:
	mov.l		L_SCR3(%a6),%d1
	andi.b		&0x30,%d1		# clear rnd prec
	fmov.l		%d1,%fpcr		# set FPCR

	bra.b		fsub_unfl_ena_cont

#
# result is equal to the smallest normalized number in the selected precision
# if the precision is extended, this result could not have come from an
# underflow that rounded up.
#
fsub_may_unfl:
	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# fetch rnd prec
	beq.w		fsub_normal		# yes; no underflow occurred

	mov.l		0x4(%sp),%d1
	cmpi.l		%d1,&0x80000000		# is hi(man) = 0x80000000?
	bne.w		fsub_normal		# no; no underflow occurred

	tst.l		0x8(%sp)		# is lo(man) = 0x0?
	bne.w		fsub_normal		# no; no underflow occurred

	btst		&inex2_bit,FPSR_EXCEPT(%a6) # is INEX2 set?
	beq.w		fsub_normal		# no; no underflow occurred

#
# ok, so now the result has a exponent equal to the smallest normalized
# exponent for the selected precision. also, the mantissa is equal to
# 0x8000000000000000 and this mantissa is the result of rounding non-zero
# g,r,s.
# now, we must determine whether the pre-rounded result was an underflow
# rounded "up" or a normalized number rounded "down".
# so, we do this be re-executing the add using RZ as the rounding mode and
# seeing if the new result is smaller or equal to the current result.
#
	fmovm.x		FP_SCR1(%a6),&0x40	# load dst op into fp1

	mov.l		L_SCR3(%a6),%d1
	andi.b		&0xc0,%d1		# keep rnd prec
	ori.b		&rz_mode*0x10,%d1	# insert rnd mode
	fmov.l		%d1,%fpcr		# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsub.x		FP_SCR0(%a6),%fp1	# execute subtract

	fmov.l		&0x0,%fpcr		# clear FPCR

	fabs.x		%fp0			# compare absolute values
	fabs.x		%fp1
	fcmp.x		%fp0,%fp1		# is first result > second?

	fbgt.w		fsub_unfl		# yes; it's an underflow
	bra.w		fsub_normal		# no; it's not an underflow

##########################################################################

#
# Sub: inputs are not both normalized; what are they?
#
fsub_not_norm:
	mov.w		(tbl_fsub_op.b,%pc,%d1.w*2),%d1
	jmp		(tbl_fsub_op.b,%pc,%d1.w*1)

	swbeg		&48
tbl_fsub_op:
	short		fsub_norm	- tbl_fsub_op # NORM - NORM
	short		fsub_zero_src	- tbl_fsub_op # NORM - ZERO
	short		fsub_inf_src	- tbl_fsub_op # NORM - INF
	short		fsub_res_qnan	- tbl_fsub_op # NORM - QNAN
	short		fsub_norm	- tbl_fsub_op # NORM - DENORM
	short		fsub_res_snan	- tbl_fsub_op # NORM - SNAN
	short		tbl_fsub_op	- tbl_fsub_op #
	short		tbl_fsub_op	- tbl_fsub_op #

	short		fsub_zero_dst	- tbl_fsub_op # ZERO - NORM
	short		fsub_zero_2	- tbl_fsub_op # ZERO - ZERO
	short		fsub_inf_src	- tbl_fsub_op # ZERO - INF
	short		fsub_res_qnan	- tbl_fsub_op # NORM - QNAN
	short		fsub_zero_dst	- tbl_fsub_op # ZERO - DENORM
	short		fsub_res_snan	- tbl_fsub_op # NORM - SNAN
	short		tbl_fsub_op	- tbl_fsub_op #
	short		tbl_fsub_op	- tbl_fsub_op #

	short		fsub_inf_dst	- tbl_fsub_op # INF - NORM
	short		fsub_inf_dst	- tbl_fsub_op # INF - ZERO
	short		fsub_inf_2	- tbl_fsub_op # INF - INF
	short		fsub_res_qnan	- tbl_fsub_op # NORM - QNAN
	short		fsub_inf_dst	- tbl_fsub_op # INF - DENORM
	short		fsub_res_snan	- tbl_fsub_op # NORM - SNAN
	short		tbl_fsub_op	- tbl_fsub_op #
	short		tbl_fsub_op	- tbl_fsub_op #

	short		fsub_res_qnan	- tbl_fsub_op # QNAN - NORM
	short		fsub_res_qnan	- tbl_fsub_op # QNAN - ZERO
	short		fsub_res_qnan	- tbl_fsub_op # QNAN - INF
	short		fsub_res_qnan	- tbl_fsub_op # QNAN - QNAN
	short		fsub_res_qnan	- tbl_fsub_op # QNAN - DENORM
	short		fsub_res_snan	- tbl_fsub_op # QNAN - SNAN
	short		tbl_fsub_op	- tbl_fsub_op #
	short		tbl_fsub_op	- tbl_fsub_op #

	short		fsub_norm	- tbl_fsub_op # DENORM - NORM
	short		fsub_zero_src	- tbl_fsub_op # DENORM - ZERO
	short		fsub_inf_src	- tbl_fsub_op # DENORM - INF
	short		fsub_res_qnan	- tbl_fsub_op # NORM - QNAN
	short		fsub_norm	- tbl_fsub_op # DENORM - DENORM
	short		fsub_res_snan	- tbl_fsub_op # NORM - SNAN
	short		tbl_fsub_op	- tbl_fsub_op #
	short		tbl_fsub_op	- tbl_fsub_op #

	short		fsub_res_snan	- tbl_fsub_op # SNAN - NORM
	short		fsub_res_snan	- tbl_fsub_op # SNAN - ZERO
	short		fsub_res_snan	- tbl_fsub_op # SNAN - INF
	short		fsub_res_snan	- tbl_fsub_op # SNAN - QNAN
	short		fsub_res_snan	- tbl_fsub_op # SNAN - DENORM
	short		fsub_res_snan	- tbl_fsub_op # SNAN - SNAN
	short		tbl_fsub_op	- tbl_fsub_op #
	short		tbl_fsub_op	- tbl_fsub_op #

fsub_res_qnan:
	bra.l		res_qnan
fsub_res_snan:
	bra.l		res_snan

#
# both operands are ZEROes
#
fsub_zero_2:
	mov.b		SRC_EX(%a0),%d0
	mov.b		DST_EX(%a1),%d1
	eor.b		%d1,%d0
	bpl.b		fsub_zero_2_chk_rm

# the signs are opposite, so, return a ZERO w/ the sign of the dst ZERO
	tst.b		%d0			# is dst negative?
	bmi.b		fsub_zero_2_rm		# yes
	fmov.s		&0x00000000,%fp0	# no; return +ZERO
	mov.b		&z_bmask,FPSR_CC(%a6)	# set Z
	rts

#
# the ZEROes have the same signs:
# - therefore, we return +ZERO if the rounding mode is RN,RZ, or RP
# - -ZERO is returned in the case of RM.
#
fsub_zero_2_chk_rm:
	mov.b		3+L_SCR3(%a6),%d1
	andi.b		&0x30,%d1		# extract rnd mode
	cmpi.b		%d1,&rm_mode*0x10	# is rnd mode = RM?
	beq.b		fsub_zero_2_rm		# yes
	fmov.s		&0x00000000,%fp0	# no; return +ZERO
	mov.b		&z_bmask,FPSR_CC(%a6)	# set Z
	rts

fsub_zero_2_rm:
	fmov.s		&0x80000000,%fp0	# return -ZERO
	mov.b		&z_bmask+neg_bmask,FPSR_CC(%a6)	# set Z/NEG
	rts

#
# one operand is a ZERO and the other is a DENORM or a NORM.
# scale the DENORM or NORM and jump to the regular fsub routine.
#
fsub_zero_dst:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)
	bsr.l		scale_to_zero_src	# scale the operand
	clr.w		FP_SCR1_EX(%a6)
	clr.l		FP_SCR1_HI(%a6)
	clr.l		FP_SCR1_LO(%a6)
	bra.w		fsub_zero_entry		# go execute fsub

fsub_zero_src:
	mov.w		DST_EX(%a1),FP_SCR1_EX(%a6)
	mov.l		DST_HI(%a1),FP_SCR1_HI(%a6)
	mov.l		DST_LO(%a1),FP_SCR1_LO(%a6)
	bsr.l		scale_to_zero_dst	# scale the operand
	clr.w		FP_SCR0_EX(%a6)
	clr.l		FP_SCR0_HI(%a6)
	clr.l		FP_SCR0_LO(%a6)
	bra.w		fsub_zero_entry		# go execute fsub

#
# both operands are INFs. an OPERR will result if the INFs have the
# same signs. else,
#
fsub_inf_2:
	mov.b		SRC_EX(%a0),%d0		# exclusive or the signs
	mov.b		DST_EX(%a1),%d1
	eor.b		%d1,%d0
	bpl.l		res_operr		# weed out (-INF)+(+INF)

# ok, so it's not an OPERR. but we do have to remember to return
# the src INF since that's where the 881/882 gets the j-bit.

fsub_inf_src:
	fmovm.x		SRC(%a0),&0x80		# return src INF
	fneg.x		%fp0			# invert sign
	fbge.w		fsub_inf_done		# sign is now positive
	mov.b		&neg_bmask+inf_bmask,FPSR_CC(%a6) # set INF/NEG
	rts

fsub_inf_dst:
	fmovm.x		DST(%a1),&0x80		# return dst INF
	tst.b		DST_EX(%a1)		# is INF negative?
	bpl.b		fsub_inf_done		# no
	mov.b		&neg_bmask+inf_bmask,FPSR_CC(%a6) # set INF/NEG
	rts

fsub_inf_done:
	mov.b		&inf_bmask,FPSR_CC(%a6)	# set INF
	rts

#########################################################################
# XDEF ****************************************************************	#
#	fsqrt(): emulates the fsqrt instruction				#
#	fssqrt(): emulates the fssqrt instruction			#
#	fdsqrt(): emulates the fdsqrt instruction			#
#									#
# XREF ****************************************************************	#
#	scale_sqrt() - scale the source operand				#
#	unf_res() - return default underflow result			#
#	ovf_res() - return default overflow result			#
#	res_qnan_1op() - return QNAN result				#
#	res_snan_1op() - return SNAN result				#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to extended precision source operand		#
#	d0  rnd prec,mode						#
#									#
# OUTPUT **************************************************************	#
#	fp0 = result							#
#	fp1 = EXOP (if exception occurred)				#
#									#
# ALGORITHM ***********************************************************	#
#	Handle NANs, infinities, and zeroes as special cases. Divide	#
# norms/denorms into ext/sgl/dbl precision.				#
#	For norms/denorms, scale the exponents such that a sqrt		#
# instruction won't cause an exception. Use the regular fsqrt to	#
# compute a result. Check if the regular operands would have taken	#
# an exception. If so, return the default overflow/underflow result	#
# and return the EXOP if exceptions are enabled. Else, scale the	#
# result operand to the proper exponent.				#
#									#
#########################################################################

	global		fssqrt
fssqrt:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&s_mode*0x10,%d0	# insert sgl precision
	bra.b		fsqrt

	global		fdsqrt
fdsqrt:
	andi.b		&0x30,%d0		# clear rnd prec
	ori.b		&d_mode*0x10,%d0	# insert dbl precision

	global		fsqrt
fsqrt:
	mov.l		%d0,L_SCR3(%a6)		# store rnd info
	clr.w		%d1
	mov.b		STAG(%a6),%d1
	bne.w		fsqrt_not_norm		# optimize on non-norm input

#
# SQUARE ROOT: norms and denorms ONLY!
#
fsqrt_norm:
	tst.b		SRC_EX(%a0)		# is operand negative?
	bmi.l		res_operr		# yes

	andi.b		&0xc0,%d0		# is precision extended?
	bne.b		fsqrt_not_ext		# no; go handle sgl or dbl

	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsqrt.x		(%a0),%fp0		# execute square root

	fmov.l		%fpsr,%d1
	or.l		%d1,USER_FPSR(%a6)	# set N,INEX

	rts

fsqrt_denorm:
	tst.b		SRC_EX(%a0)		# is operand negative?
	bmi.l		res_operr		# yes

	andi.b		&0xc0,%d0		# is precision extended?
	bne.b		fsqrt_not_ext		# no; go handle sgl or dbl

	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)

	bsr.l		scale_sqrt		# calculate scale factor

	bra.w		fsqrt_sd_normal

#
# operand is either single or double
#
fsqrt_not_ext:
	cmpi.b		%d0,&s_mode*0x10	# separate sgl/dbl prec
	bne.w		fsqrt_dbl

#
# operand is to be rounded to single precision
#
fsqrt_sgl:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)

	bsr.l		scale_sqrt		# calculate scale factor

	cmpi.l		%d0,&0x3fff-0x3f81	# will move in underflow?
	beq.w		fsqrt_sd_may_unfl
	bgt.w		fsqrt_sd_unfl		# yes; go handle underflow
	cmpi.l		%d0,&0x3fff-0x407f	# will move in overflow?
	beq.w		fsqrt_sd_may_ovfl	# maybe; go check
	blt.w		fsqrt_sd_ovfl		# yes; go handle overflow

#
# operand will NOT overflow or underflow when moved in to the fp reg file
#
fsqrt_sd_normal:
	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fsqrt.x		FP_SCR0(%a6),%fp0	# perform absolute

	fmov.l		%fpsr,%d1		# save FPSR
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fsqrt_sd_normal_exit:
	mov.l		%d2,-(%sp)		# save d2
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result
	mov.w		FP_SCR0_EX(%a6),%d1	# load sgn,exp
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	sub.l		%d0,%d1			# add scale factor
	andi.w		&0x8000,%d2		# keep old sign
	or.w		%d1,%d2			# concat old sign,new exp
	mov.w		%d2,FP_SCR0_EX(%a6)	# insert new exponent
	mov.l		(%sp)+,%d2		# restore d2
	fmovm.x		FP_SCR0(%a6),&0x80	# return result in fp0
	rts

#
# operand is to be rounded to double precision
#
fsqrt_dbl:
	mov.w		SRC_EX(%a0),FP_SCR0_EX(%a6)
	mov.l		SRC_HI(%a0),FP_SCR0_HI(%a6)
	mov.l		SRC_LO(%a0),FP_SCR0_LO(%a6)

	bsr.l		scale_sqrt		# calculate scale factor

	cmpi.l		%d0,&0x3fff-0x3c01	# will move in underflow?
	beq.w		fsqrt_sd_may_unfl
	bgt.b		fsqrt_sd_unfl		# yes; go handle underflow
	cmpi.l		%d0,&0x3fff-0x43ff	# will move in overflow?
	beq.w		fsqrt_sd_may_ovfl	# maybe; go check
	blt.w		fsqrt_sd_ovfl		# yes; go handle overflow
	bra.w		fsqrt_sd_normal		# no; ho handle normalized op

# we're on the line here and the distinguising characteristic is whether
# the exponent is 3fff or 3ffe. if it's 3ffe, then it's a safe number
# elsewise fall through to underflow.
fsqrt_sd_may_unfl:
	btst		&0x0,1+FP_SCR0_EX(%a6)	# is exponent 0x3fff?
	bne.w		fsqrt_sd_normal		# yes, so no underflow

#
# operand WILL underflow when moved in to the fp register file
#
fsqrt_sd_unfl:
	bset		&unfl_bit,FPSR_EXCEPT(%a6) # set unfl exc bit

	fmov.l		&rz_mode*0x10,%fpcr	# set FPCR
	fmov.l		&0x0,%fpsr		# clear FPSR

	fsqrt.x		FP_SCR0(%a6),%fp0	# execute square root

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

# if underflow or inexact is enabled, go calculate EXOP first.
	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x0b,%d1		# is UNFL or INEX enabled?
	bne.b		fsqrt_sd_unfl_ena	# yes

fsqrt_sd_unfl_dis:
	fmovm.x		&0x80,FP_SCR0(%a6)	# store out result

	lea		FP_SCR0(%a6),%a0	# pass: result addr
	mov.l		L_SCR3(%a6),%d1		# pass: rnd prec,mode
	bsr.l		unf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# set possible 'Z' ccode
	fmovm.x		FP_SCR0(%a6),&0x80	# return default result in fp0
	rts

#
# operand will underflow AND underflow is enabled.
# therefore, we must return the result rounded to extended precision.
#
fsqrt_sd_unfl_ena:
	mov.l		FP_SCR0_HI(%a6),FP_SCR1_HI(%a6)
	mov.l		FP_SCR0_LO(%a6),FP_SCR1_LO(%a6)
	mov.w		FP_SCR0_EX(%a6),%d1	# load current exponent

	mov.l		%d2,-(%sp)		# save d2
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# subtract scale factor
	addi.l		&0x6000,%d1		# add new bias
	andi.w		&0x7fff,%d1
	or.w		%d2,%d1			# concat new sign,new exp
	mov.w		%d1,FP_SCR1_EX(%a6)	# insert new exp
	fmovm.x		FP_SCR1(%a6),&0x40	# return EXOP in fp1
	mov.l		(%sp)+,%d2		# restore d2
	bra.b		fsqrt_sd_unfl_dis

#
# operand WILL overflow.
#
fsqrt_sd_ovfl:
	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fsqrt.x		FP_SCR0(%a6),%fp0	# perform square root

	fmov.l		&0x0,%fpcr		# clear FPCR
	fmov.l		%fpsr,%d1		# save FPSR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

fsqrt_sd_ovfl_tst:
	or.l		&ovfl_inx_mask,USER_FPSR(%a6) # set ovfl/aovfl/ainex

	mov.b		FPCR_ENABLE(%a6),%d1
	andi.b		&0x13,%d1		# is OVFL or INEX enabled?
	bne.b		fsqrt_sd_ovfl_ena	# yes

#
# OVFL is not enabled; therefore, we must create the default result by
# calling ovf_res().
#
fsqrt_sd_ovfl_dis:
	btst		&neg_bit,FPSR_CC(%a6)	# is result negative?
	sne		%d1			# set sign param accordingly
	mov.l		L_SCR3(%a6),%d0		# pass: prec,mode
	bsr.l		ovf_res			# calculate default result
	or.b		%d0,FPSR_CC(%a6)	# set INF,N if applicable
	fmovm.x		(%a0),&0x80		# return default result in fp0
	rts

#
# OVFL is enabled.
# the INEX2 bit has already been updated by the round to the correct precision.
# now, round to extended(and don't alter the FPSR).
#
fsqrt_sd_ovfl_ena:
	mov.l		%d2,-(%sp)		# save d2
	mov.w		FP_SCR0_EX(%a6),%d1	# fetch {sgn,exp}
	mov.l		%d1,%d2			# make a copy
	andi.l		&0x7fff,%d1		# strip sign
	andi.w		&0x8000,%d2		# keep old sign
	sub.l		%d0,%d1			# add scale factor
	subi.l		&0x6000,%d1		# subtract bias
	andi.w		&0x7fff,%d1
	or.w		%d2,%d1			# concat sign,exp
	mov.w		%d1,FP_SCR0_EX(%a6)	# insert new exponent
	fmovm.x		FP_SCR0(%a6),&0x40	# return EXOP in fp1
	mov.l		(%sp)+,%d2		# restore d2
	bra.b		fsqrt_sd_ovfl_dis

#
# the move in MAY underflow. so...
#
fsqrt_sd_may_ovfl:
	btst		&0x0,1+FP_SCR0_EX(%a6)	# is exponent 0x3fff?
	bne.w		fsqrt_sd_ovfl		# yes, so overflow

	fmov.l		&0x0,%fpsr		# clear FPSR
	fmov.l		L_SCR3(%a6),%fpcr	# set FPCR

	fsqrt.x		FP_SCR0(%a6),%fp0	# perform absolute

	fmov.l		%fpsr,%d1		# save status
	fmov.l		&0x0,%fpcr		# clear FPCR

	or.l		%d1,USER_FPSR(%a6)	# save INEX2,N

	fmov.x		%fp0,%fp1		# make a copy of result
	fcmp.b		%fp1,&0x1		# is |result| >= 1.b?
	fbge.w		fsqrt_sd_ovfl_tst	# yes; overflow has occurred

# no, it didn't overflow; we have correct result
	bra.w		fsqrt_sd_normal_exit

##########################################################################

#
# input is not normalized; what is it?
#
fsqrt_not_norm:
	cmpi.b		%d1,&DENORM		# weed out DENORM
	beq.w		fsqrt_denorm
	cmpi.b		%d1,&ZERO		# weed out ZERO
	beq.b		fsqrt_zero
	cmpi.b		%d1,&INF		# weed out INF
	beq.b		fsqrt_inf
	cmpi.b		%d1,&SNAN		# weed out SNAN
	beq.l		res_snan_1op
	bra.l		res_qnan_1op

#
#	fsqrt(+0) = +0
#	fsqrt(-0) = -0
#	fsqrt(+INF) = +INF
#	fsqrt(-INF) = OPERR
#
fsqrt_zero:
	tst.b		SRC_EX(%a0)		# is ZERO positive or negative?
	bmi.b		fsqrt_zero_m		# negative
fsqrt_zero_p:
	fmov.s		&0x00000000,%fp0	# return +ZERO
	mov.b		&z_bmask,FPSR_CC(%a6)	# set 'Z' ccode bit
	rts
fsqrt_zero_m:
	fmov.s		&0x80000000,%fp0	# return -ZERO
	mov.b		&z_bmask+neg_bmask,FPSR_CC(%a6)	# set 'Z','N' ccode bits
	rts

fsqrt_inf:
	tst.b		SRC_EX(%a0)		# is INF positive or negative?
	bmi.l		res_operr		# negative
fsqrt_inf_p:
	fmovm.x		SRC(%a0),&0x80		# return +INF in fp0
	mov.b		&inf_bmask,FPSR_CC(%a6)	# set 'I' ccode bit
	rts

#########################################################################
# XDEF ****************************************************************	#
#	fetch_dreg(): fetch register according to index in d1		#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	d1 = index of register to fetch from				#
#									#
# OUTPUT **************************************************************	#
#	d0 = value of register fetched					#
#									#
# ALGORITHM ***********************************************************	#
#	According to the index value in d1 which can range from zero	#
# to fifteen, load the corresponding register file value (where		#
# address register indexes start at 8). D0/D1/A0/A1/A6/A7 are on the	#
# stack. The rest should still be in their original places.		#
#									#
#########################################################################

# this routine leaves d1 intact for subsequent store_dreg calls.
	global		fetch_dreg
fetch_dreg:
	mov.w		(tbl_fdreg.b,%pc,%d1.w*2),%d0
	jmp		(tbl_fdreg.b,%pc,%d0.w*1)

tbl_fdreg:
	short		fdreg0 - tbl_fdreg
	short		fdreg1 - tbl_fdreg
	short		fdreg2 - tbl_fdreg
	short		fdreg3 - tbl_fdreg
	short		fdreg4 - tbl_fdreg
	short		fdreg5 - tbl_fdreg
	short		fdreg6 - tbl_fdreg
	short		fdreg7 - tbl_fdreg
	short		fdreg8 - tbl_fdreg
	short		fdreg9 - tbl_fdreg
	short		fdrega - tbl_fdreg
	short		fdregb - tbl_fdreg
	short		fdregc - tbl_fdreg
	short		fdregd - tbl_fdreg
	short		fdrege - tbl_fdreg
	short		fdregf - tbl_fdreg

fdreg0:
	mov.l		EXC_DREGS+0x0(%a6),%d0
	rts
fdreg1:
	mov.l		EXC_DREGS+0x4(%a6),%d0
	rts
fdreg2:
	mov.l		%d2,%d0
	rts
fdreg3:
	mov.l		%d3,%d0
	rts
fdreg4:
	mov.l		%d4,%d0
	rts
fdreg5:
	mov.l		%d5,%d0
	rts
fdreg6:
	mov.l		%d6,%d0
	rts
fdreg7:
	mov.l		%d7,%d0
	rts
fdreg8:
	mov.l		EXC_DREGS+0x8(%a6),%d0
	rts
fdreg9:
	mov.l		EXC_DREGS+0xc(%a6),%d0
	rts
fdrega:
	mov.l		%a2,%d0
	rts
fdregb:
	mov.l		%a3,%d0
	rts
fdregc:
	mov.l		%a4,%d0
	rts
fdregd:
	mov.l		%a5,%d0
	rts
fdrege:
	mov.l		(%a6),%d0
	rts
fdregf:
	mov.l		EXC_A7(%a6),%d0
	rts

#########################################################################
# XDEF ****************************************************************	#
#	store_dreg_l(): store longword to data register specified by d1	#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	d0 = longowrd value to store					#
#	d1 = index of register to fetch from				#
#									#
# OUTPUT **************************************************************	#
#	(data register is updated)					#
#									#
# ALGORITHM ***********************************************************	#
#	According to the index value in d1, store the longword value	#
# in d0 to the corresponding data register. D0/D1 are on the stack	#
# while the rest are in their initial places.				#
#									#
#########################################################################

	global		store_dreg_l
store_dreg_l:
	mov.w		(tbl_sdregl.b,%pc,%d1.w*2),%d1
	jmp		(tbl_sdregl.b,%pc,%d1.w*1)

tbl_sdregl:
	short		sdregl0 - tbl_sdregl
	short		sdregl1 - tbl_sdregl
	short		sdregl2 - tbl_sdregl
	short		sdregl3 - tbl_sdregl
	short		sdregl4 - tbl_sdregl
	short		sdregl5 - tbl_sdregl
	short		sdregl6 - tbl_sdregl
	short		sdregl7 - tbl_sdregl

sdregl0:
	mov.l		%d0,EXC_DREGS+0x0(%a6)
	rts
sdregl1:
	mov.l		%d0,EXC_DREGS+0x4(%a6)
	rts
sdregl2:
	mov.l		%d0,%d2
	rts
sdregl3:
	mov.l		%d0,%d3
	rts
sdregl4:
	mov.l		%d0,%d4
	rts
sdregl5:
	mov.l		%d0,%d5
	rts
sdregl6:
	mov.l		%d0,%d6
	rts
sdregl7:
	mov.l		%d0,%d7
	rts

#########################################################################
# XDEF ****************************************************************	#
#	store_dreg_w(): store word to data register specified by d1	#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	d0 = word value to store					#
#	d1 = index of register to fetch from				#
#									#
# OUTPUT **************************************************************	#
#	(data register is updated)					#
#									#
# ALGORITHM ***********************************************************	#
#	According to the index value in d1, store the word value	#
# in d0 to the corresponding data register. D0/D1 are on the stack	#
# while the rest are in their initial places.				#
#									#
#########################################################################

	global		store_dreg_w
store_dreg_w:
	mov.w		(tbl_sdregw.b,%pc,%d1.w*2),%d1
	jmp		(tbl_sdregw.b,%pc,%d1.w*1)

tbl_sdregw:
	short		sdregw0 - tbl_sdregw
	short		sdregw1 - tbl_sdregw
	short		sdregw2 - tbl_sdregw
	short		sdregw3 - tbl_sdregw
	short		sdregw4 - tbl_sdregw
	short		sdregw5 - tbl_sdregw
	short		sdregw6 - tbl_sdregw
	short		sdregw7 - tbl_sdregw

sdregw0:
	mov.w		%d0,2+EXC_DREGS+0x0(%a6)
	rts
sdregw1:
	mov.w		%d0,2+EXC_DREGS+0x4(%a6)
	rts
sdregw2:
	mov.w		%d0,%d2
	rts
sdregw3:
	mov.w		%d0,%d3
	rts
sdregw4:
	mov.w		%d0,%d4
	rts
sdregw5:
	mov.w		%d0,%d5
	rts
sdregw6:
	mov.w		%d0,%d6
	rts
sdregw7:
	mov.w		%d0,%d7
	rts

#########################################################################
# XDEF ****************************************************************	#
#	store_dreg_b(): store byte to data register specified by d1	#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	d0 = byte value to store					#
#	d1 = index of register to fetch from				#
#									#
# OUTPUT **************************************************************	#
#	(data register is updated)					#
#									#
# ALGORITHM ***********************************************************	#
#	According to the index value in d1, store the byte value	#
# in d0 to the corresponding data register. D0/D1 are on the stack	#
# while the rest are in their initial places.				#
#									#
#########################################################################

	global		store_dreg_b
store_dreg_b:
	mov.w		(tbl_sdregb.b,%pc,%d1.w*2),%d1
	jmp		(tbl_sdregb.b,%pc,%d1.w*1)

tbl_sdregb:
	short		sdregb0 - tbl_sdregb
	short		sdregb1 - tbl_sdregb
	short		sdregb2 - tbl_sdregb
	short		sdregb3 - tbl_sdregb
	short		sdregb4 - tbl_sdregb
	short		sdregb5 - tbl_sdregb
	short		sdregb6 - tbl_sdregb
	short		sdregb7 - tbl_sdregb

sdregb0:
	mov.b		%d0,3+EXC_DREGS+0x0(%a6)
	rts
sdregb1:
	mov.b		%d0,3+EXC_DREGS+0x4(%a6)
	rts
sdregb2:
	mov.b		%d0,%d2
	rts
sdregb3:
	mov.b		%d0,%d3
	rts
sdregb4:
	mov.b		%d0,%d4
	rts
sdregb5:
	mov.b		%d0,%d5
	rts
sdregb6:
	mov.b		%d0,%d6
	rts
sdregb7:
	mov.b		%d0,%d7
	rts

#########################################################################
# XDEF ****************************************************************	#
#	inc_areg(): increment an address register by the value in d0	#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	d0 = amount to increment by					#
#	d1 = index of address register to increment			#
#									#
# OUTPUT **************************************************************	#
#	(address register is updated)					#
#									#
# ALGORITHM ***********************************************************	#
#	Typically used for an instruction w/ a post-increment <ea>,	#
# this routine adds the increment value in d0 to the address register	#
# specified by d1. A0/A1/A6/A7 reside on the stack. The rest reside	#
# in their original places.						#
#	For a7, if the increment amount is one, then we have to		#
# increment by two. For any a7 update, set the mia7_flag so that if	#
# an access error exception occurs later in emulation, this address	#
# register update can be undone.					#
#									#
#########################################################################

	global		inc_areg
inc_areg:
	mov.w		(tbl_iareg.b,%pc,%d1.w*2),%d1
	jmp		(tbl_iareg.b,%pc,%d1.w*1)

tbl_iareg:
	short		iareg0 - tbl_iareg
	short		iareg1 - tbl_iareg
	short		iareg2 - tbl_iareg
	short		iareg3 - tbl_iareg
	short		iareg4 - tbl_iareg
	short		iareg5 - tbl_iareg
	short		iareg6 - tbl_iareg
	short		iareg7 - tbl_iareg

iareg0:	add.l		%d0,EXC_DREGS+0x8(%a6)
	rts
iareg1:	add.l		%d0,EXC_DREGS+0xc(%a6)
	rts
iareg2:	add.l		%d0,%a2
	rts
iareg3:	add.l		%d0,%a3
	rts
iareg4:	add.l		%d0,%a4
	rts
iareg5:	add.l		%d0,%a5
	rts
iareg6:	add.l		%d0,(%a6)
	rts
iareg7:	mov.b		&mia7_flg,SPCOND_FLG(%a6)
	cmpi.b		%d0,&0x1
	beq.b		iareg7b
	add.l		%d0,EXC_A7(%a6)
	rts
iareg7b:
	addq.l		&0x2,EXC_A7(%a6)
	rts

#########################################################################
# XDEF ****************************************************************	#
#	dec_areg(): decrement an address register by the value in d0	#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	d0 = amount to decrement by					#
#	d1 = index of address register to decrement			#
#									#
# OUTPUT **************************************************************	#
#	(address register is updated)					#
#									#
# ALGORITHM ***********************************************************	#
#	Typically used for an instruction w/ a pre-decrement <ea>,	#
# this routine adds the decrement value in d0 to the address register	#
# specified by d1. A0/A1/A6/A7 reside on the stack. The rest reside	#
# in their original places.						#
#	For a7, if the decrement amount is one, then we have to		#
# decrement by two. For any a7 update, set the mda7_flag so that if	#
# an access error exception occurs later in emulation, this address	#
# register update can be undone.					#
#									#
#########################################################################

	global		dec_areg
dec_areg:
	mov.w		(tbl_dareg.b,%pc,%d1.w*2),%d1
	jmp		(tbl_dareg.b,%pc,%d1.w*1)

tbl_dareg:
	short		dareg0 - tbl_dareg
	short		dareg1 - tbl_dareg
	short		dareg2 - tbl_dareg
	short		dareg3 - tbl_dareg
	short		dareg4 - tbl_dareg
	short		dareg5 - tbl_dareg
	short		dareg6 - tbl_dareg
	short		dareg7 - tbl_dareg

dareg0:	sub.l		%d0,EXC_DREGS+0x8(%a6)
	rts
dareg1:	sub.l		%d0,EXC_DREGS+0xc(%a6)
	rts
dareg2:	sub.l		%d0,%a2
	rts
dareg3:	sub.l		%d0,%a3
	rts
dareg4:	sub.l		%d0,%a4
	rts
dareg5:	sub.l		%d0,%a5
	rts
dareg6:	sub.l		%d0,(%a6)
	rts
dareg7:	mov.b		&mda7_flg,SPCOND_FLG(%a6)
	cmpi.b		%d0,&0x1
	beq.b		dareg7b
	sub.l		%d0,EXC_A7(%a6)
	rts
dareg7b:
	subq.l		&0x2,EXC_A7(%a6)
	rts

##############################################################################

#########################################################################
# XDEF ****************************************************************	#
#	load_fpn1(): load FP register value into FP_SRC(a6).		#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	d0 = index of FP register to load				#
#									#
# OUTPUT **************************************************************	#
#	FP_SRC(a6) = value loaded from FP register file			#
#									#
# ALGORITHM ***********************************************************	#
#	Using the index in d0, load FP_SRC(a6) with a number from the	#
# FP register file.							#
#									#
#########################################################################

	global		load_fpn1
load_fpn1:
	mov.w		(tbl_load_fpn1.b,%pc,%d0.w*2), %d0
	jmp		(tbl_load_fpn1.b,%pc,%d0.w*1)

tbl_load_fpn1:
	short		load_fpn1_0 - tbl_load_fpn1
	short		load_fpn1_1 - tbl_load_fpn1
	short		load_fpn1_2 - tbl_load_fpn1
	short		load_fpn1_3 - tbl_load_fpn1
	short		load_fpn1_4 - tbl_load_fpn1
	short		load_fpn1_5 - tbl_load_fpn1
	short		load_fpn1_6 - tbl_load_fpn1
	short		load_fpn1_7 - tbl_load_fpn1

load_fpn1_0:
	mov.l		0+EXC_FP0(%a6), 0+FP_SRC(%a6)
	mov.l		4+EXC_FP0(%a6), 4+FP_SRC(%a6)
	mov.l		8+EXC_FP0(%a6), 8+FP_SRC(%a6)
	lea		FP_SRC(%a6), %a0
	rts
load_fpn1_1:
	mov.l		0+EXC_FP1(%a6), 0+FP_SRC(%a6)
	mov.l		4+EXC_FP1(%a6), 4+FP_SRC(%a6)
	mov.l		8+EXC_FP1(%a6), 8+FP_SRC(%a6)
	lea		FP_SRC(%a6), %a0
	rts
load_fpn1_2:
	fmovm.x		&0x20, FP_SRC(%a6)
	lea		FP_SRC(%a6), %a0
	rts
load_fpn1_3:
	fmovm.x		&0x10, FP_SRC(%a6)
	lea		FP_SRC(%a6), %a0
	rts
load_fpn1_4:
	fmovm.x		&0x08, FP_SRC(%a6)
	lea		FP_SRC(%a6), %a0
	rts
load_fpn1_5:
	fmovm.x		&0x04, FP_SRC(%a6)
	lea		FP_SRC(%a6), %a0
	rts
load_fpn1_6:
	fmovm.x		&0x02, FP_SRC(%a6)
	lea		FP_SRC(%a6), %a0
	rts
load_fpn1_7:
	fmovm.x		&0x01, FP_SRC(%a6)
	lea		FP_SRC(%a6), %a0
	rts

#############################################################################

#########################################################################
# XDEF ****************************************************************	#
#	load_fpn2(): load FP register value into FP_DST(a6).		#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	d0 = index of FP register to load				#
#									#
# OUTPUT **************************************************************	#
#	FP_DST(a6) = value loaded from FP register file			#
#									#
# ALGORITHM ***********************************************************	#
#	Using the index in d0, load FP_DST(a6) with a number from the	#
# FP register file.							#
#									#
#########################################################################

	global		load_fpn2
load_fpn2:
	mov.w		(tbl_load_fpn2.b,%pc,%d0.w*2), %d0
	jmp		(tbl_load_fpn2.b,%pc,%d0.w*1)

tbl_load_fpn2:
	short		load_fpn2_0 - tbl_load_fpn2
	short		load_fpn2_1 - tbl_load_fpn2
	short		load_fpn2_2 - tbl_load_fpn2
	short		load_fpn2_3 - tbl_load_fpn2
	short		load_fpn2_4 - tbl_load_fpn2
	short		load_fpn2_5 - tbl_load_fpn2
	short		load_fpn2_6 - tbl_load_fpn2
	short		load_fpn2_7 - tbl_load_fpn2

load_fpn2_0:
	mov.l		0+EXC_FP0(%a6), 0+FP_DST(%a6)
	mov.l		4+EXC_FP0(%a6), 4+FP_DST(%a6)
	mov.l		8+EXC_FP0(%a6), 8+FP_DST(%a6)
	lea		FP_DST(%a6), %a0
	rts
load_fpn2_1:
	mov.l		0+EXC_FP1(%a6), 0+FP_DST(%a6)
	mov.l		4+EXC_FP1(%a6), 4+FP_DST(%a6)
	mov.l		8+EXC_FP1(%a6), 8+FP_DST(%a6)
	lea		FP_DST(%a6), %a0
	rts
load_fpn2_2:
	fmovm.x		&0x20, FP_DST(%a6)
	lea		FP_DST(%a6), %a0
	rts
load_fpn2_3:
	fmovm.x		&0x10, FP_DST(%a6)
	lea		FP_DST(%a6), %a0
	rts
load_fpn2_4:
	fmovm.x		&0x08, FP_DST(%a6)
	lea		FP_DST(%a6), %a0
	rts
load_fpn2_5:
	fmovm.x		&0x04, FP_DST(%a6)
	lea		FP_DST(%a6), %a0
	rts
load_fpn2_6:
	fmovm.x		&0x02, FP_DST(%a6)
	lea		FP_DST(%a6), %a0
	rts
load_fpn2_7:
	fmovm.x		&0x01, FP_DST(%a6)
	lea		FP_DST(%a6), %a0
	rts

#############################################################################

#########################################################################
# XDEF ****************************************************************	#
#	store_fpreg(): store an fp value to the fpreg designated d0.	#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	fp0 = extended precision value to store				#
#	d0  = index of floating-point register				#
#									#
# OUTPUT **************************************************************	#
#	None								#
#									#
# ALGORITHM ***********************************************************	#
#	Store the value in fp0 to the FP register designated by the	#
# value in d0. The FP number can be DENORM or SNAN so we have to be	#
# careful that we don't take an exception here.				#
#									#
#########################################################################

	global		store_fpreg
store_fpreg:
	mov.w		(tbl_store_fpreg.b,%pc,%d0.w*2), %d0
	jmp		(tbl_store_fpreg.b,%pc,%d0.w*1)

tbl_store_fpreg:
	short		store_fpreg_0 - tbl_store_fpreg
	short		store_fpreg_1 - tbl_store_fpreg
	short		store_fpreg_2 - tbl_store_fpreg
	short		store_fpreg_3 - tbl_store_fpreg
	short		store_fpreg_4 - tbl_store_fpreg
	short		store_fpreg_5 - tbl_store_fpreg
	short		store_fpreg_6 - tbl_store_fpreg
	short		store_fpreg_7 - tbl_store_fpreg

store_fpreg_0:
	fmovm.x		&0x80, EXC_FP0(%a6)
	rts
store_fpreg_1:
	fmovm.x		&0x80, EXC_FP1(%a6)
	rts
store_fpreg_2:
	fmovm.x		&0x01, -(%sp)
	fmovm.x		(%sp)+, &0x20
	rts
store_fpreg_3:
	fmovm.x		&0x01, -(%sp)
	fmovm.x		(%sp)+, &0x10
	rts
store_fpreg_4:
	fmovm.x		&0x01, -(%sp)
	fmovm.x		(%sp)+, &0x08
	rts
store_fpreg_5:
	fmovm.x		&0x01, -(%sp)
	fmovm.x		(%sp)+, &0x04
	rts
store_fpreg_6:
	fmovm.x		&0x01, -(%sp)
	fmovm.x		(%sp)+, &0x02
	rts
store_fpreg_7:
	fmovm.x		&0x01, -(%sp)
	fmovm.x		(%sp)+, &0x01
	rts

#########################################################################
# XDEF ****************************************************************	#
#	get_packed(): fetch a packed operand from memory and then	#
#		      convert it to a floating-point binary number.	#
#									#
# XREF ****************************************************************	#
#	_dcalc_ea() - calculate the correct <ea>			#
#	_mem_read() - fetch the packed operand from memory		#
#	facc_in_x() - the fetch failed so jump to special exit code	#
#	decbin()    - convert packed to binary extended precision	#
#									#
# INPUT ***************************************************************	#
#	None								#
#									#
# OUTPUT **************************************************************	#
#	If no failure on _mem_read():					#
#	FP_SRC(a6) = packed operand now as a binary FP number		#
#									#
# ALGORITHM ***********************************************************	#
#	Get the correct <ea> whihc is the value on the exception stack	#
# frame w/ maybe a correction factor if the <ea> is -(an) or (an)+.	#
# Then, fetch the operand from memory. If the fetch fails, exit		#
# through facc_in_x().							#
#	If the packed operand is a ZERO,NAN, or INF, convert it to	#
# its binary representation here. Else, call decbin() which will	#
# convert the packed value to an extended precision binary value.	#
#									#
#########################################################################

# the stacked <ea> for packed is correct except for -(An).
# the base reg must be updated for both -(An) and (An)+.
	global		get_packed
get_packed:
	mov.l		&0xc,%d0		# packed is 12 bytes
	bsr.l		_dcalc_ea		# fetch <ea>; correct An

	lea		FP_SRC(%a6),%a1		# pass: ptr to super dst
	mov.l		&0xc,%d0		# pass: 12 bytes
	bsr.l		_dmem_read		# read packed operand

	tst.l		%d1			# did dfetch fail?
	bne.l		facc_in_x		# yes

# The packed operand is an INF or a NAN if the exponent field is all ones.
	bfextu		FP_SRC(%a6){&1:&15},%d0	# get exp
	cmpi.w		%d0,&0x7fff		# INF or NAN?
	bne.b		gp_try_zero		# no
	rts					# operand is an INF or NAN

# The packed operand is a zero if the mantissa is all zero, else it's
# a normal packed op.
gp_try_zero:
	mov.b		3+FP_SRC(%a6),%d0	# get byte 4
	andi.b		&0x0f,%d0		# clear all but last nybble
	bne.b		gp_not_spec		# not a zero
	tst.l		FP_SRC_HI(%a6)		# is lw 2 zero?
	bne.b		gp_not_spec		# not a zero
	tst.l		FP_SRC_LO(%a6)		# is lw 3 zero?
	bne.b		gp_not_spec		# not a zero
	rts					# operand is a ZERO
gp_not_spec:
	lea		FP_SRC(%a6),%a0		# pass: ptr to packed op
	bsr.l		decbin			# convert to extended
	fmovm.x		&0x80,FP_SRC(%a6)	# make this the srcop
	rts

#########################################################################
# decbin(): Converts normalized packed bcd value pointed to by register	#
#	    a0 to extended-precision value in fp0.			#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to normalized packed bcd value			#
#									#
# OUTPUT **************************************************************	#
#	fp0 = exact fp representation of the packed bcd value.		#
#									#
# ALGORITHM ***********************************************************	#
#	Expected is a normal bcd (i.e. non-exceptional; all inf, zero,	#
#	and NaN operands are dispatched without entering this routine)	#
#	value in 68881/882 format at location (a0).			#
#									#
#	A1. Convert the bcd exponent to binary by successive adds and	#
#	muls. Set the sign according to SE. Subtract 16 to compensate	#
#	for the mantissa which is to be interpreted as 17 integer	#
#	digits, rather than 1 integer and 16 fraction digits.		#
#	Note: this operation can never overflow.			#
#									#
#	A2. Convert the bcd mantissa to binary by successive		#
#	adds and muls in FP0. Set the sign according to SM.		#
#	The mantissa digits will be converted with the decimal point	#
#	assumed following the least-significant digit.			#
#	Note: this operation can never overflow.			#
#									#
#	A3. Count the number of leading/trailing zeros in the		#
#	bcd string.  If SE is positive, count the leading zeros;	#
#	if negative, count the trailing zeros.  Set the adjusted	#
#	exponent equal to the exponent from A1 and the zero count	#
#	added if SM = 1 and subtracted if SM = 0.  Scale the		#
#	mantissa the equivalent of forcing in the bcd value:		#
#									#
#	SM = 0	a non-zero digit in the integer position		#
#	SM = 1	a non-zero digit in Mant0, lsd of the fraction		#
#									#
#	this will insure that any value, regardless of its		#
#	representation (ex. 0.1E2, 1E1, 10E0, 100E-1), is converted	#
#	consistently.							#
#									#
#	A4. Calculate the factor 10^exp in FP1 using a table of		#
#	10^(2^n) values.  To reduce the error in forming factors	#
#	greater than 10^27, a directed rounding scheme is used with	#
#	tables rounded to RN, RM, and RP, according to the table	#
#	in the comments of the pwrten section.				#
#									#
#	A5. Form the final binary number by scaling the mantissa by	#
#	the exponent factor.  This is done by multiplying the		#
#	mantissa in FP0 by the factor in FP1 if the adjusted		#
#	exponent sign is positive, and dividing FP0 by FP1 if		#
#	it is negative.							#
#									#
#	Clean up and return. Check if the final mul or div was inexact.	#
#	If so, set INEX1 in USER_FPSR.					#
#									#
#########################################################################

#
#	PTENRN, PTENRM, and PTENRP are arrays of powers of 10 rounded
#	to nearest, minus, and plus, respectively.  The tables include
#	10**{1,2,4,8,16,32,64,128,256,512,1024,2048,4096}.  No rounding
#	is required until the power is greater than 27, however, all
#	tables include the first 5 for ease of indexing.
#
RTABLE:
	byte		0,0,0,0
	byte		2,3,2,3
	byte		2,3,3,2
	byte		3,2,2,3

	set		FNIBS,7
	set		FSTRT,0

	set		ESTRT,4
	set		EDIGITS,2

	global		decbin
decbin:
	mov.l		0x0(%a0),FP_SCR0_EX(%a6) # make a copy of input
	mov.l		0x4(%a0),FP_SCR0_HI(%a6) # so we don't alter it
	mov.l		0x8(%a0),FP_SCR0_LO(%a6)

	lea		FP_SCR0(%a6),%a0

	movm.l		&0x3c00,-(%sp)		# save d2-d5
	fmovm.x		&0x1,-(%sp)		# save fp1
#
# Calculate exponent:
#  1. Copy bcd value in memory for use as a working copy.
#  2. Calculate absolute value of exponent in d1 by mul and add.
#  3. Correct for exponent sign.
#  4. Subtract 16 to compensate for interpreting the mant as all integer digits.
#     (i.e., all digits assumed left of the decimal point.)
#
# Register usage:
#
#  calc_e:
#	(*)  d0: temp digit storage
#	(*)  d1: accumulator for binary exponent
#	(*)  d2: digit count
#	(*)  d3: offset pointer
#	( )  d4: first word of bcd
#	( )  a0: pointer to working bcd value
#	( )  a6: pointer to original bcd value
#	(*)  FP_SCR1: working copy of original bcd value
#	(*)  L_SCR1: copy of original exponent word
#
calc_e:
	mov.l		&EDIGITS,%d2		# # of nibbles (digits) in fraction part
	mov.l		&ESTRT,%d3		# counter to pick up digits
	mov.l		(%a0),%d4		# get first word of bcd
	clr.l		%d1			# zero d1 for accumulator
e_gd:
	mulu.l		&0xa,%d1		# mul partial product by one digit place
	bfextu		%d4{%d3:&4},%d0		# get the digit and zero extend into d0
	add.l		%d0,%d1			# d1 = d1 + d0
	addq.b		&4,%d3			# advance d3 to the next digit
	dbf.w		%d2,e_gd		# if we have used all 3 digits, exit loop
	btst		&30,%d4			# get SE
	beq.b		e_pos			# don't negate if pos
	neg.l		%d1			# negate before subtracting
e_pos:
	sub.l		&16,%d1			# sub to compensate for shift of mant
	bge.b		e_save			# if still pos, do not neg
	neg.l		%d1			# now negative, make pos and set SE
	or.l		&0x40000000,%d4		# set SE in d4,
	or.l		&0x40000000,(%a0)	# and in working bcd
e_save:
	mov.l		%d1,-(%sp)		# save exp on stack
#
#
# Calculate mantissa:
#  1. Calculate absolute value of mantissa in fp0 by mul and add.
#  2. Correct for mantissa sign.
#     (i.e., all digits assumed left of the decimal point.)
#
# Register usage:
#
#  calc_m:
#	(*)  d0: temp digit storage
#	(*)  d1: lword counter
#	(*)  d2: digit count
#	(*)  d3: offset pointer
#	( )  d4: words 2 and 3 of bcd
#	( )  a0: pointer to working bcd value
#	( )  a6: pointer to original bcd value
#	(*) fp0: mantissa accumulator
#	( )  FP_SCR1: working copy of original bcd value
#	( )  L_SCR1: copy of original exponent word
#
calc_m:
	mov.l		&1,%d1			# word counter, init to 1
	fmov.s		&0x00000000,%fp0	# accumulator
#
#
#  Since the packed number has a long word between the first & second parts,
#  get the integer digit then skip down & get the rest of the
#  mantissa.  We will unroll the loop once.
#
	bfextu		(%a0){&28:&4},%d0	# integer part is ls digit in long word
	fadd.b		%d0,%fp0		# add digit to sum in fp0
#
#
#  Get the rest of the mantissa.
#
loadlw:
	mov.l		(%a0,%d1.L*4),%d4	# load mantissa lonqword into d4
	mov.l		&FSTRT,%d3		# counter to pick up digits
	mov.l		&FNIBS,%d2		# reset number of digits per a0 ptr
md2b:
	fmul.s		&0x41200000,%fp0	# fp0 = fp0 * 10
	bfextu		%d4{%d3:&4},%d0		# get the digit and zero extend
	fadd.b		%d0,%fp0		# fp0 = fp0 + digit
#
#
#  If all the digits (8) in that long word have been converted (d2=0),
#  then inc d1 (=2) to point to the next long word and reset d3 to 0
#  to initialize the digit offset, and set d2 to 7 for the digit count;
#  else continue with this long word.
#
	addq.b		&4,%d3			# advance d3 to the next digit
	dbf.w		%d2,md2b		# check for last digit in this lw
nextlw:
	addq.l		&1,%d1			# inc lw pointer in mantissa
	cmp.l		%d1,&2			# test for last lw
	ble.b		loadlw			# if not, get last one
#
#  Check the sign of the mant and make the value in fp0 the same sign.
#
m_sign:
	btst		&31,(%a0)		# test sign of the mantissa
	beq.b		ap_st_z			# if clear, go to append/strip zeros
	fneg.x		%fp0			# if set, negate fp0
#
# Append/strip zeros:
#
#  For adjusted exponents which have an absolute value greater than 27*,
#  this routine calculates the amount needed to normalize the mantissa
#  for the adjusted exponent.  That number is subtracted from the exp
#  if the exp was positive, and added if it was negative.  The purpose
#  of this is to reduce the value of the exponent and the possibility
#  of error in calculation of pwrten.
#
#  1. Branch on the sign of the adjusted exponent.
#  2p.(positive exp)
#   2. Check M16 and the digits in lwords 2 and 3 in decending order.
#   3. Add one for each zero encountered until a non-zero digit.
#   4. Subtract the count from the exp.
#   5. Check if the exp has crossed zero in #3 above; make the exp abs
#	   and set SE.
#	6. Multiply the mantissa by 10**count.
#  2n.(negative exp)
#   2. Check the digits in lwords 3 and 2 in decending order.
#   3. Add one for each zero encountered until a non-zero digit.
#   4. Add the count to the exp.
#   5. Check if the exp has crossed zero in #3 above; clear SE.
#   6. Divide the mantissa by 10**count.
#
#  *Why 27?  If the adjusted exponent is within -28 < expA < 28, than
#   any adjustment due to append/strip zeros will drive the resultane
#   exponent towards zero.  Since all pwrten constants with a power
#   of 27 or less are exact, there is no need to use this routine to
#   attempt to lessen the resultant exponent.
#
# Register usage:
#
#  ap_st_z:
#	(*)  d0: temp digit storage
#	(*)  d1: zero count
#	(*)  d2: digit count
#	(*)  d3: offset pointer
#	( )  d4: first word of bcd
#	(*)  d5: lword counter
#	( )  a0: pointer to working bcd value
#	( )  FP_SCR1: working copy of original bcd value
#	( )  L_SCR1: copy of original exponent word
#
#
# First check the absolute value of the exponent to see if this
# routine is necessary.  If so, then check the sign of the exponent
# and do append (+) or strip (-) zeros accordingly.
# This section handles a positive adjusted exponent.
#
ap_st_z:
	mov.l		(%sp),%d1		# load expA for range test
	cmp.l		%d1,&27			# test is with 27
	ble.w		pwrten			# if abs(expA) <28, skip ap/st zeros
	btst		&30,(%a0)		# check sign of exp
	bne.b		ap_st_n			# if neg, go to neg side
	clr.l		%d1			# zero count reg
	mov.l		(%a0),%d4		# load lword 1 to d4
	bfextu		%d4{&28:&4},%d0		# get M16 in d0
	bne.b		ap_p_fx			# if M16 is non-zero, go fix exp
	addq.l		&1,%d1			# inc zero count
	mov.l		&1,%d5			# init lword counter
	mov.l		(%a0,%d5.L*4),%d4	# get lword 2 to d4
	bne.b		ap_p_cl			# if lw 2 is zero, skip it
	addq.l		&8,%d1			# and inc count by 8
	addq.l		&1,%d5			# inc lword counter
	mov.l		(%a0,%d5.L*4),%d4	# get lword 3 to d4
ap_p_cl:
	clr.l		%d3			# init offset reg
	mov.l		&7,%d2			# init digit counter
ap_p_gd:
	bfextu		%d4{%d3:&4},%d0		# get digit
	bne.b		ap_p_fx			# if non-zero, go to fix exp
	addq.l		&4,%d3			# point to next digit
	addq.l		&1,%d1			# inc digit counter
	dbf.w		%d2,ap_p_gd		# get next digit
ap_p_fx:
	mov.l		%d1,%d0			# copy counter to d2
	mov.l		(%sp),%d1		# get adjusted exp from memory
	sub.l		%d0,%d1			# subtract count from exp
	bge.b		ap_p_fm			# if still pos, go to pwrten
	neg.l		%d1			# now its neg; get abs
	mov.l		(%a0),%d4		# load lword 1 to d4
	or.l		&0x40000000,%d4		# and set SE in d4
	or.l		&0x40000000,(%a0)	# and in memory
#
# Calculate the mantissa multiplier to compensate for the striping of
# zeros from the mantissa.
#
ap_p_fm:
	lea.l		PTENRN(%pc),%a1		# get address of power-of-ten table
	clr.l		%d3			# init table index
	fmov.s		&0x3f800000,%fp1	# init fp1 to 1
	mov.l		&3,%d2			# init d2 to count bits in counter
ap_p_el:
	asr.l		&1,%d0			# shift lsb into carry
	bcc.b		ap_p_en			# if 1, mul fp1 by pwrten factor
	fmul.x		(%a1,%d3),%fp1		# mul by 10**(d3_bit_no)
ap_p_en:
	add.l		&12,%d3			# inc d3 to next rtable entry
	tst.l		%d0			# check if d0 is zero
	bne.b		ap_p_el			# if not, get next bit
	fmul.x		%fp1,%fp0		# mul mantissa by 10**(no_bits_shifted)
	bra.b		pwrten			# go calc pwrten
#
# This section handles a negative adjusted exponent.
#
ap_st_n:
	clr.l		%d1			# clr counter
	mov.l		&2,%d5			# set up d5 to point to lword 3
	mov.l		(%a0,%d5.L*4),%d4	# get lword 3
	bne.b		ap_n_cl			# if not zero, check digits
	sub.l		&1,%d5			# dec d5 to point to lword 2
	addq.l		&8,%d1			# inc counter by 8
	mov.l		(%a0,%d5.L*4),%d4	# get lword 2
ap_n_cl:
	mov.l		&28,%d3			# point to last digit
	mov.l		&7,%d2			# init digit counter
ap_n_gd:
	bfextu		%d4{%d3:&4},%d0		# get digit
	bne.b		ap_n_fx			# if non-zero, go to exp fix
	subq.l		&4,%d3			# point to previous digit
	addq.l		&1,%d1			# inc digit counter
	dbf.w		%d2,ap_n_gd		# get next digit
ap_n_fx:
	mov.l		%d1,%d0			# copy counter to d0
	mov.l		(%sp),%d1		# get adjusted exp from memory
	sub.l		%d0,%d1			# subtract count from exp
	bgt.b		ap_n_fm			# if still pos, go fix mantissa
	neg.l		%d1			# take abs of exp and clr SE
	mov.l		(%a0),%d4		# load lword 1 to d4
	and.l		&0xbfffffff,%d4		# and clr SE in d4
	and.l		&0xbfffffff,(%a0)	# and in memory
#
# Calculate the mantissa multiplier to compensate for the appending of
# zeros to the mantissa.
#
ap_n_fm:
	lea.l		PTENRN(%pc),%a1		# get address of power-of-ten table
	clr.l		%d3			# init table index
	fmov.s		&0x3f800000,%fp1	# init fp1 to 1
	mov.l		&3,%d2			# init d2 to count bits in counter
ap_n_el:
	asr.l		&1,%d0			# shift lsb into carry
	bcc.b		ap_n_en			# if 1, mul fp1 by pwrten factor
	fmul.x		(%a1,%d3),%fp1		# mul by 10**(d3_bit_no)
ap_n_en:
	add.l		&12,%d3			# inc d3 to next rtable entry
	tst.l		%d0			# check if d0 is zero
	bne.b		ap_n_el			# if not, get next bit
	fdiv.x		%fp1,%fp0		# div mantissa by 10**(no_bits_shifted)
#
#
# Calculate power-of-ten factor from adjusted and shifted exponent.
#
# Register usage:
#
#  pwrten:
#	(*)  d0: temp
#	( )  d1: exponent
#	(*)  d2: {FPCR[6:5],SM,SE} as index in RTABLE; temp
#	(*)  d3: FPCR work copy
#	( )  d4: first word of bcd
#	(*)  a1: RTABLE pointer
#  calc_p:
#	(*)  d0: temp
#	( )  d1: exponent
#	(*)  d3: PWRTxx table index
#	( )  a0: pointer to working copy of bcd
#	(*)  a1: PWRTxx pointer
#	(*) fp1: power-of-ten accumulator
#
# Pwrten calculates the exponent factor in the selected rounding mode
# according to the following table:
#
#	Sign of Mant  Sign of Exp  Rounding Mode  PWRTEN Rounding Mode
#
#	ANY	  ANY	RN	RN
#
#	 +	   +	RP	RP
#	 -	   +	RP	RM
#	 +	   -	RP	RM
#	 -	   -	RP	RP
#
#	 +	   +	RM	RM
#	 -	   +	RM	RP
#	 +	   -	RM	RP
#	 -	   -	RM	RM
#
#	 +	   +	RZ	RM
#	 -	   +	RZ	RM
#	 +	   -	RZ	RP
#	 -	   -	RZ	RP
#
#
pwrten:
	mov.l		USER_FPCR(%a6),%d3	# get user's FPCR
	bfextu		%d3{&26:&2},%d2		# isolate rounding mode bits
	mov.l		(%a0),%d4		# reload 1st bcd word to d4
	asl.l		&2,%d2			# format d2 to be
	bfextu		%d4{&0:&2},%d0		# {FPCR[6],FPCR[5],SM,SE}
	add.l		%d0,%d2			# in d2 as index into RTABLE
	lea.l		RTABLE(%pc),%a1		# load rtable base
	mov.b		(%a1,%d2),%d0		# load new rounding bits from table
	clr.l		%d3			# clear d3 to force no exc and extended
	bfins		%d0,%d3{&26:&2}		# stuff new rounding bits in FPCR
	fmov.l		%d3,%fpcr		# write new FPCR
	asr.l		&1,%d0			# write correct PTENxx table
	bcc.b		not_rp			# to a1
	lea.l		PTENRP(%pc),%a1		# it is RP
	bra.b		calc_p			# go to init section
not_rp:
	asr.l		&1,%d0			# keep checking
	bcc.b		not_rm
	lea.l		PTENRM(%pc),%a1		# it is RM
	bra.b		calc_p			# go to init section
not_rm:
	lea.l		PTENRN(%pc),%a1		# it is RN
calc_p:
	mov.l		%d1,%d0			# copy exp to d0;use d0
	bpl.b		no_neg			# if exp is negative,
	neg.l		%d0			# invert it
	or.l		&0x40000000,(%a0)	# and set SE bit
no_neg:
	clr.l		%d3			# table index
	fmov.s		&0x3f800000,%fp1	# init fp1 to 1
e_loop:
	asr.l		&1,%d0			# shift next bit into carry
	bcc.b		e_next			# if zero, skip the mul
	fmul.x		(%a1,%d3),%fp1		# mul by 10**(d3_bit_no)
e_next:
	add.l		&12,%d3			# inc d3 to next rtable entry
	tst.l		%d0			# check if d0 is zero
	bne.b		e_loop			# not zero, continue shifting
#
#
#  Check the sign of the adjusted exp and make the value in fp0 the
#  same sign. If the exp was pos then multiply fp1*fp0;
#  else divide fp0/fp1.
#
# Register Usage:
#  norm:
#	( )  a0: pointer to working bcd value
#	(*) fp0: mantissa accumulator
#	( ) fp1: scaling factor - 10**(abs(exp))
#
pnorm:
	btst		&30,(%a0)		# test the sign of the exponent
	beq.b		mul			# if clear, go to multiply
div:
	fdiv.x		%fp1,%fp0		# exp is negative, so divide mant by exp
	bra.b		end_dec
mul:
	fmul.x		%fp1,%fp0		# exp is positive, so multiply by exp
#
#
# Clean up and return with result in fp0.
#
# If the final mul/div in decbin incurred an inex exception,
# it will be inex2, but will be reported as inex1 by get_op.
#
end_dec:
	fmov.l		%fpsr,%d0		# get status register
	bclr		&inex2_bit+8,%d0	# test for inex2 and clear it
	beq.b		no_exc			# skip this if no exc
	ori.w		&inx1a_mask,2+USER_FPSR(%a6) # set INEX1/AINEX
no_exc:
	add.l		&0x4,%sp		# clear 1 lw param
	fmovm.x		(%sp)+,&0x40		# restore fp1
	movm.l		(%sp)+,&0x3c		# restore d2-d5
	fmov.l		&0x0,%fpcr
	fmov.l		&0x0,%fpsr
	rts

#########################################################################
# bindec(): Converts an input in extended precision format to bcd format#
#									#
# INPUT ***************************************************************	#
#	a0 = pointer to the input extended precision value in memory.	#
#	     the input may be either normalized, unnormalized, or	#
#	     denormalized.						#
#	d0 = contains the k-factor sign-extended to 32-bits.		#
#									#
# OUTPUT **************************************************************	#
#	FP_SCR0(a6) = bcd format result on the stack.			#
#									#
# ALGORITHM ***********************************************************	#
#									#
#	A1.	Set RM and size ext;  Set SIGMA = sign of input.	#
#		The k-factor is saved for use in d7. Clear the		#
#		BINDEC_FLG for separating normalized/denormalized	#
#		input.  If input is unnormalized or denormalized,	#
#		normalize it.						#
#									#
#	A2.	Set X = abs(input).					#
#									#
#	A3.	Compute ILOG.						#
#		ILOG is the log base 10 of the input value.  It is	#
#		approximated by adding e + 0.f when the original	#
#		value is viewed as 2^^e * 1.f in extended precision.	#
#		This value is stored in d6.				#
#									#
#	A4.	Clr INEX bit.						#
#		The operation in A3 above may have set INEX2.		#
#									#
#	A5.	Set ICTR = 0;						#
#		ICTR is a flag used in A13.  It must be set before the	#
#		loop entry A6.						#
#									#
#	A6.	Calculate LEN.						#
#		LEN is the number of digits to be displayed.  The	#
#		k-factor can dictate either the total number of digits,	#
#		if it is a positive number, or the number of digits	#
#		after the decimal point which are to be included as	#
#		significant.  See the 68882 manual for examples.	#
#		If LEN is computed to be greater than 17, set OPERR in	#
#		USER_FPSR.  LEN is stored in d4.			#
#									#
#	A7.	Calculate SCALE.					#
#		SCALE is equal to 10^ISCALE, where ISCALE is the number	#
#		of decimal places needed to insure LEN integer digits	#
#		in the output before conversion to bcd. LAMBDA is the	#
#		sign of ISCALE, used in A9. Fp1 contains		#
#		10^^(abs(ISCALE)) using a rounding mode which is a	#
#		function of the original rounding mode and the signs	#
#		of ISCALE and X.  A table is given in the code.		#
#									#
#	A8.	Clr INEX; Force RZ.					#
#		The operation in A3 above may have set INEX2.		#
#		RZ mode is forced for the scaling operation to insure	#
#		only one rounding error.  The grs bits are collected in #
#		the INEX flag for use in A10.				#
#									#
#	A9.	Scale X -> Y.						#
#		The mantissa is scaled to the desired number of		#
#		significant digits.  The excess digits are collected	#
#		in INEX2.						#
#									#
#	A10.	Or in INEX.						#
#		If INEX is set, round error occurred.  This is		#
#		compensated for by 'or-ing' in the INEX2 flag to	#
#		the lsb of Y.						#
#									#
#	A11.	Restore original FPCR; set size ext.			#
#		Perform FINT operation in the user's rounding mode.	#
#		Keep the size to extended.				#
#									#
#	A12.	Calculate YINT = FINT(Y) according to user's rounding	#
#		mode.  The FPSP routine sintd0 is used.  The output	#
#		is in fp0.						#
#									#
#	A13.	Check for LEN digits.					#
#		If the int operation results in more than LEN digits,	#
#		or less than LEN -1 digits, adjust ILOG and repeat from	#
#		A6.  This test occurs only on the first pass.  If the	#
#		result is exactly 10^LEN, decrement ILOG and divide	#
#		the mantissa by 10.					#
#									#
#	A14.	Convert the mantissa to bcd.				#
#		The binstr routine is used to convert the LEN digit	#
#		mantissa to bcd in memory.  The input to binstr is	#
#		to be a fraction; i.e. (mantissa)/10^LEN and adjusted	#
#		such that the decimal point is to the left of bit 63.	#
#		The bcd digits are stored in the correct position in	#
#		the final string area in memory.			#
#									#
#	A15.	Convert the exponent to bcd.				#
#		As in A14 above, the exp is converted to bcd and the	#
#		digits are stored in the final string.			#
#		Test the length of the final exponent string.  If the	#
#		length is 4, set operr.					#
#									#
#	A16.	Write sign bits to final string.			#
#									#
#########################################################################

set	BINDEC_FLG,	EXC_TEMP	# DENORM flag

# Constants in extended precision
PLOG2:
	long		0x3FFD0000,0x9A209A84,0xFBCFF798,0x00000000
PLOG2UP1:
	long		0x3FFD0000,0x9A209A84,0xFBCFF799,0x00000000

# Constants in single precision
FONE:
	long		0x3F800000,0x00000000,0x00000000,0x00000000
FTWO:
	long		0x40000000,0x00000000,0x00000000,0x00000000
FTEN:
	long		0x41200000,0x00000000,0x00000000,0x00000000
F4933:
	long		0x459A2800,0x00000000,0x00000000,0x00000000

RBDTBL:
	byte		0,0,0,0
	byte		3,3,2,2
	byte		3,2,2,3
	byte		2,3,3,2

#	Implementation Notes:
#
#	The registers are used as follows:
#
#		d0: scratch; LEN input to binstr
#		d1: scratch
#		d2: upper 32-bits of mantissa for binstr
#		d3: scratch;lower 32-bits of mantissa for binstr
#		d4: LEN
#		d5: LAMBDA/ICTR
#		d6: ILOG
#		d7: k-factor
#		a0: ptr for original operand/final result
#		a1: scratch pointer
#		a2: pointer to FP_X; abs(original value) in ext
#		fp0: scratch
#		fp1: scratch
#		fp2: scratch
#		F_SCR1:
#		F_SCR2:
#		L_SCR1:
#		L_SCR2:

	global		bindec
bindec:
	movm.l		&0x3f20,-(%sp)	#  {%d2-%d7/%a2}
	fmovm.x		&0x7,-(%sp)	#  {%fp0-%fp2}

# A1. Set RM and size ext. Set SIGMA = sign input;
#     The k-factor is saved for use in d7.  Clear BINDEC_FLG for
#     separating  normalized/denormalized input.  If the input
#     is a denormalized number, set the BINDEC_FLG memory word
#     to signal denorm.  If the input is unnormalized, normalize
#     the input and test for denormalized result.
#
	fmov.l		&rm_mode*0x10,%fpcr	# set RM and ext
	mov.l		(%a0),L_SCR2(%a6)	# save exponent for sign check
	mov.l		%d0,%d7		# move k-factor to d7

	clr.b		BINDEC_FLG(%a6)	# clr norm/denorm flag
	cmpi.b		STAG(%a6),&DENORM # is input a DENORM?
	bne.w		A2_str		# no; input is a NORM

#
# Normalize the denorm
#
un_de_norm:
	mov.w		(%a0),%d0
	and.w		&0x7fff,%d0	# strip sign of normalized exp
	mov.l		4(%a0),%d1
	mov.l		8(%a0),%d2
norm_loop:
	sub.w		&1,%d0
	lsl.l		&1,%d2
	roxl.l		&1,%d1
	tst.l		%d1
	bge.b		norm_loop
#
# Test if the normalized input is denormalized
#
	tst.w		%d0
	bgt.b		pos_exp		# if greater than zero, it is a norm
	st		BINDEC_FLG(%a6)	# set flag for denorm
pos_exp:
	and.w		&0x7fff,%d0	# strip sign of normalized exp
	mov.w		%d0,(%a0)
	mov.l		%d1,4(%a0)
	mov.l		%d2,8(%a0)

# A2. Set X = abs(input).
#
A2_str:
	mov.l		(%a0),FP_SCR1(%a6)	# move input to work space
	mov.l		4(%a0),FP_SCR1+4(%a6)	# move input to work space
	mov.l		8(%a0),FP_SCR1+8(%a6)	# move input to work space
	and.l		&0x7fffffff,FP_SCR1(%a6)	# create abs(X)

# A3. Compute ILOG.
#     ILOG is the log base 10 of the input value.  It is approx-
#     imated by adding e + 0.f when the original value is viewed
#     as 2^^e * 1.f in extended precision.  This value is stored
#     in d6.
#
# Register usage:
#	Input/Output
#	d0: k-factor/exponent
#	d2: x/x
#	d3: x/x
#	d4: x/x
#	d5: x/x
#	d6: x/ILOG
#	d7: k-factor/Unchanged
#	a0: ptr for original operand/final result
#	a1: x/x
#	a2: x/x
#	fp0: x/float(ILOG)
#	fp1: x/x
#	fp2: x/x
#	F_SCR1:x/x
#	F_SCR2:Abs(X)/Abs(X) with $3fff exponent
#	L_SCR1:x/x
#	L_SCR2:first word of X packed/Unchanged

	tst.b		BINDEC_FLG(%a6)	# check for denorm
	beq.b		A3_cont		# if clr, continue with norm
	mov.l		&-4933,%d6	# force ILOG = -4933
	bra.b		A4_str
A3_cont:
	mov.w		FP_SCR1(%a6),%d0	# move exp to d0
	mov.w		&0x3fff,FP_SCR1(%a6)	# replace exponent with 0x3fff
	fmov.x		FP_SCR1(%a6),%fp0	# now fp0 has 1.f
	sub.w		&0x3fff,%d0	# strip off bias
	fadd.w		%d0,%fp0	# add in exp
	fsub.s		FONE(%pc),%fp0	# subtract off 1.0
	fbge.w		pos_res		# if pos, branch
	fmul.x		PLOG2UP1(%pc),%fp0	# if neg, mul by LOG2UP1
	fmov.l		%fp0,%d6	# put ILOG in d6 as a lword
	bra.b		A4_str		# go move out ILOG
pos_res:
	fmul.x		PLOG2(%pc),%fp0	# if pos, mul by LOG2
	fmov.l		%fp0,%d6	# put ILOG in d6 as a lword


# A4. Clr INEX bit.
#     The operation in A3 above may have set INEX2.

A4_str:
	fmov.l		&0,%fpsr	# zero all of fpsr - nothing needed


# A5. Set ICTR = 0;
#     ICTR is a flag used in A13.  It must be set before the
#     loop entry A6. The lower word of d5 is used for ICTR.

	clr.w		%d5		# clear ICTR

# A6. Calculate LEN.
#     LEN is the number of digits to be displayed.  The k-factor
#     can dictate either the total number of digits, if it is
#     a positive number, or the number of digits after the
#     original decimal point which are to be included as
#     significant.  See the 68882 manual for examples.
#     If LEN is computed to be greater than 17, set OPERR in
#     USER_FPSR.  LEN is stored in d4.
#
# Register usage:
#	Input/Output
#	d0: exponent/Unchanged
#	d2: x/x/scratch
#	d3: x/x
#	d4: exc picture/LEN
#	d5: ICTR/Unchanged
#	d6: ILOG/Unchanged
#	d7: k-factor/Unchanged
#	a0: ptr for original operand/final result
#	a1: x/x
#	a2: x/x
#	fp0: float(ILOG)/Unchanged
#	fp1: x/x
#	fp2: x/x
#	F_SCR1:x/x
#	F_SCR2:Abs(X) with $3fff exponent/Unchanged
#	L_SCR1:x/x
#	L_SCR2:first word of X packed/Unchanged

A6_str:
	tst.l		%d7		# branch on sign of k
	ble.b		k_neg		# if k <= 0, LEN = ILOG + 1 - k
	mov.l		%d7,%d4		# if k > 0, LEN = k
	bra.b		len_ck		# skip to LEN check
k_neg:
	mov.l		%d6,%d4		# first load ILOG to d4
	sub.l		%d7,%d4		# subtract off k
	addq.l		&1,%d4		# add in the 1
len_ck:
	tst.l		%d4		# LEN check: branch on sign of LEN
	ble.b		LEN_ng		# if neg, set LEN = 1
	cmp.l		%d4,&17		# test if LEN > 17
	ble.b		A7_str		# if not, forget it
	mov.l		&17,%d4		# set max LEN = 17
	tst.l		%d7		# if negative, never set OPERR
	ble.b		A7_str		# if positive, continue
	or.l		&opaop_mask,USER_FPSR(%a6)	# set OPERR & AIOP in USER_FPSR
	bra.b		A7_str		# finished here
LEN_ng:
	mov.l		&1,%d4		# min LEN is 1


# A7. Calculate SCALE.
#     SCALE is equal to 10^ISCALE, where ISCALE is the number
#     of decimal places needed to insure LEN integer digits
#     in the output before conversion to bcd. LAMBDA is the sign
#     of ISCALE, used in A9.  Fp1 contains 10^^(abs(ISCALE)) using
#     the rounding mode as given in the following table (see
#     Coonen, p. 7.23 as ref.; however, the SCALE variable is
#     of opposite sign in bindec.sa from Coonen).
#
#	Initial					USE
#	FPCR[6:5]	LAMBDA	SIGN(X)		FPCR[6:5]
#	----------------------------------------------
#	 RN	00	   0	   0		00/0	RN
#	 RN	00	   0	   1		00/0	RN
#	 RN	00	   1	   0		00/0	RN
#	 RN	00	   1	   1		00/0	RN
#	 RZ	01	   0	   0		11/3	RP
#	 RZ	01	   0	   1		11/3	RP
#	 RZ	01	   1	   0		10/2	RM
#	 RZ	01	   1	   1		10/2	RM
#	 RM	10	   0	   0		11/3	RP
#	 RM	10	   0	   1		10/2	RM
#	 RM	10	   1	   0		10/2	RM
#	 RM	10	   1	   1		11/3	RP
#	 RP	11	   0	   0		10/2	RM
#	 RP	11	   0	   1		11/3	RP
#	 RP	11	   1	   0		11/3	RP
#	 RP	11	   1	   1		10/2	RM
#
# Register usage:
#	Input/Output
#	d0: exponent/scratch - final is 0
#	d2: x/0 or 24 for A9
#	d3: x/scratch - offset ptr into PTENRM array
#	d4: LEN/Unchanged
#	d5: 0/ICTR:LAMBDA
#	d6: ILOG/ILOG or k if ((k<=0)&(ILOG<k))
#	d7: k-factor/Unchanged
#	a0: ptr for original operand/final result
#	a1: x/ptr to PTENRM array
#	a2: x/x
#	fp0: float(ILOG)/Unchanged
#	fp1: x/10^ISCALE
#	fp2: x/x
#	F_SCR1:x/x
#	F_SCR2:Abs(X) with $3fff exponent/Unchanged
#	L_SCR1:x/x
#	L_SCR2:first word of X packed/Unchanged

A7_str:
	tst.l		%d7		# test sign of k
	bgt.b		k_pos		# if pos and > 0, skip this
	cmp.l		%d7,%d6		# test k - ILOG
	blt.b		k_pos		# if ILOG >= k, skip this
	mov.l		%d7,%d6		# if ((k<0) & (ILOG < k)) ILOG = k
k_pos:
	mov.l		%d6,%d0		# calc ILOG + 1 - LEN in d0
	addq.l		&1,%d0		# add the 1
	sub.l		%d4,%d0		# sub off LEN
	swap		%d5		# use upper word of d5 for LAMBDA
	clr.w		%d5		# set it zero initially
	clr.w		%d2		# set up d2 for very small case
	tst.l		%d0		# test sign of ISCALE
	bge.b		iscale		# if pos, skip next inst
	addq.w		&1,%d5		# if neg, set LAMBDA true
	cmp.l		%d0,&0xffffecd4	# test iscale <= -4908
	bgt.b		no_inf		# if false, skip rest
	add.l		&24,%d0		# add in 24 to iscale
	mov.l		&24,%d2		# put 24 in d2 for A9
no_inf:
	neg.l		%d0		# and take abs of ISCALE
iscale:
	fmov.s		FONE(%pc),%fp1	# init fp1 to 1
	bfextu		USER_FPCR(%a6){&26:&2},%d1	# get initial rmode bits
	lsl.w		&1,%d1		# put them in bits 2:1
	add.w		%d5,%d1		# add in LAMBDA
	lsl.w		&1,%d1		# put them in bits 3:1
	tst.l		L_SCR2(%a6)	# test sign of original x
	bge.b		x_pos		# if pos, don't set bit 0
	addq.l		&1,%d1		# if neg, set bit 0
x_pos:
	lea.l		RBDTBL(%pc),%a2	# load rbdtbl base
	mov.b		(%a2,%d1),%d3	# load d3 with new rmode
	lsl.l		&4,%d3		# put bits in proper position
	fmov.l		%d3,%fpcr	# load bits into fpu
	lsr.l		&4,%d3		# put bits in proper position
	tst.b		%d3		# decode new rmode for pten table
	bne.b		not_rn		# if zero, it is RN
	lea.l		PTENRN(%pc),%a1	# load a1 with RN table base
	bra.b		rmode		# exit decode
not_rn:
	lsr.b		&1,%d3		# get lsb in carry
	bcc.b		not_rp2		# if carry clear, it is RM
	lea.l		PTENRP(%pc),%a1	# load a1 with RP table base
	bra.b		rmode		# exit decode
not_rp2:
	lea.l		PTENRM(%pc),%a1	# load a1 with RM table base
rmode:
	clr.l		%d3		# clr table index
e_loop2:
	lsr.l		&1,%d0		# shift next bit into carry
	bcc.b		e_next2		# if zero, skip the mul
	fmul.x		(%a1,%d3),%fp1	# mul by 10**(d3_bit_no)
e_next2:
	add.l		&12,%d3		# inc d3 to next pwrten table entry
	tst.l		%d0		# test if ISCALE is zero
	bne.b		e_loop2		# if not, loop

# A8. Clr INEX; Force RZ.
#     The operation in A3 above may have set INEX2.
#     RZ mode is forced for the scaling operation to insure
#     only one rounding error.  The grs bits are collected in
#     the INEX flag for use in A10.
#
# Register usage:
#	Input/Output

	fmov.l		&0,%fpsr	# clr INEX
	fmov.l		&rz_mode*0x10,%fpcr	# set RZ rounding mode

# A9. Scale X -> Y.
#     The mantissa is scaled to the desired number of significant
#     digits.  The excess digits are collected in INEX2. If mul,
#     Check d2 for excess 10 exponential value.  If not zero,
#     the iscale value would have caused the pwrten calculation
#     to overflow.  Only a negative iscale can cause this, so
#     multiply by 10^(d2), which is now only allowed to be 24,
#     with a multiply by 10^8 and 10^16, which is exact since
#     10^24 is exact.  If the input was denormalized, we must
#     create a busy stack frame with the mul command and the
#     two operands, and allow the fpu to complete the multiply.
#
# Register usage:
#	Input/Output
#	d0: FPCR with RZ mode/Unchanged
#	d2: 0 or 24/unchanged
#	d3: x/x
#	d4: LEN/Unchanged
#	d5: ICTR:LAMBDA
#	d6: ILOG/Unchanged
#	d7: k-factor/Unchanged
#	a0: ptr for original operand/final result
#	a1: ptr to PTENRM array/Unchanged
#	a2: x/x
#	fp0: float(ILOG)/X adjusted for SCALE (Y)
#	fp1: 10^ISCALE/Unchanged
#	fp2: x/x
#	F_SCR1:x/x
#	F_SCR2:Abs(X) with $3fff exponent/Unchanged
#	L_SCR1:x/x
#	L_SCR2:first word of X packed/Unchanged

A9_str:
	fmov.x		(%a0),%fp0	# load X from memory
	fabs.x		%fp0		# use abs(X)
	tst.w		%d5		# LAMBDA is in lower word of d5
	bne.b		sc_mul		# if neg (LAMBDA = 1), scale by mul
	fdiv.x		%fp1,%fp0	# calculate X / SCALE -> Y to fp0
	bra.w		A10_st		# branch to A10

sc_mul:
	tst.b		BINDEC_FLG(%a6)	# check for denorm
	beq.w		A9_norm		# if norm, continue with mul

# for DENORM, we must calculate:
#	fp0 = input_op * 10^ISCALE * 10^24
# since the input operand is a DENORM, we can't multiply it directly.
# so, we do the multiplication of the exponents and mantissas separately.
# in this way, we avoid underflow on intermediate stages of the
# multiplication and guarantee a result without exception.
	fmovm.x		&0x2,-(%sp)	# save 10^ISCALE to stack

	mov.w		(%sp),%d3	# grab exponent
	andi.w		&0x7fff,%d3	# clear sign
	ori.w		&0x8000,(%a0)	# make DENORM exp negative
	add.w		(%a0),%d3	# add DENORM exp to 10^ISCALE exp
	subi.w		&0x3fff,%d3	# subtract BIAS
	add.w		36(%a1),%d3
	subi.w		&0x3fff,%d3	# subtract BIAS
	add.w		48(%a1),%d3
	subi.w		&0x3fff,%d3	# subtract BIAS

	bmi.w		sc_mul_err	# is result is DENORM, punt!!!

	andi.w		&0x8000,(%sp)	# keep sign
	or.w		%d3,(%sp)	# insert new exponent
	andi.w		&0x7fff,(%a0)	# clear sign bit on DENORM again
	mov.l		0x8(%a0),-(%sp) # put input op mantissa on stk
	mov.l		0x4(%a0),-(%sp)
	mov.l		&0x3fff0000,-(%sp) # force exp to zero
	fmovm.x		(%sp)+,&0x80	# load normalized DENORM into fp0
	fmul.x		(%sp)+,%fp0

#	fmul.x	36(%a1),%fp0	# multiply fp0 by 10^8
#	fmul.x	48(%a1),%fp0	# multiply fp0 by 10^16
	mov.l		36+8(%a1),-(%sp) # get 10^8 mantissa
	mov.l		36+4(%a1),-(%sp)
	mov.l		&0x3fff0000,-(%sp) # force exp to zero
	mov.l		48+8(%a1),-(%sp) # get 10^16 mantissa
	mov.l		48+4(%a1),-(%sp)
	mov.l		&0x3fff0000,-(%sp)# force exp to zero
	fmul.x		(%sp)+,%fp0	# multiply fp0 by 10^8
	fmul.x		(%sp)+,%fp0	# multiply fp0 by 10^16
	bra.b		A10_st

sc_mul_err:
	bra.b		sc_mul_err

A9_norm:
	tst.w		%d2		# test for small exp case
	beq.b		A9_con		# if zero, continue as normal
	fmul.x		36(%a1),%fp0	# multiply fp0 by 10^8
	fmul.x		48(%a1),%fp0	# multiply fp0 by 10^16
A9_con:
	fmul.x		%fp1,%fp0	# calculate X * SCALE -> Y to fp0

# A10. Or in INEX.
#      If INEX is set, round error occurred.  This is compensated
#      for by 'or-ing' in the INEX2 flag to the lsb of Y.
#
# Register usage:
#	Input/Output
#	d0: FPCR with RZ mode/FPSR with INEX2 isolated
#	d2: x/x
#	d3: x/x
#	d4: LEN/Unchanged
#	d5: ICTR:LAMBDA
#	d6: ILOG/Unchanged
#	d7: k-factor/Unchanged
#	a0: ptr for original operand/final result
#	a1: ptr to PTENxx array/Unchanged
#	a2: x/ptr to FP_SCR1(a6)
#	fp0: Y/Y with lsb adjusted
#	fp1: 10^ISCALE/Unchanged
#	fp2: x/x

A10_st:
	fmov.l		%fpsr,%d0	# get FPSR
	fmov.x		%fp0,FP_SCR1(%a6)	# move Y to memory
	lea.l		FP_SCR1(%a6),%a2	# load a2 with ptr to FP_SCR1
	btst		&9,%d0		# check if INEX2 set
	beq.b		A11_st		# if clear, skip rest
	or.l		&1,8(%a2)	# or in 1 to lsb of mantissa
	fmov.x		FP_SCR1(%a6),%fp0	# write adjusted Y back to fpu


# A11. Restore original FPCR; set size ext.
#      Perform FINT operation in the user's rounding mode.  Keep
#      the size to extended.  The sintdo entry point in the sint
#      routine expects the FPCR value to be in USER_FPCR for
#      mode and precision.  The original FPCR is saved in L_SCR1.

A11_st:
	mov.l		USER_FPCR(%a6),L_SCR1(%a6)	# save it for later
	and.l		&0x00000030,USER_FPCR(%a6)	# set size to ext,
#					;block exceptions


# A12. Calculate YINT = FINT(Y) according to user's rounding mode.
#      The FPSP routine sintd0 is used.  The output is in fp0.
#
# Register usage:
#	Input/Output
#	d0: FPSR with AINEX cleared/FPCR with size set to ext
#	d2: x/x/scratch
#	d3: x/x
#	d4: LEN/Unchanged
#	d5: ICTR:LAMBDA/Unchanged
#	d6: ILOG/Unchanged
#	d7: k-factor/Unchanged
#	a0: ptr for original operand/src ptr for sintdo
#	a1: ptr to PTENxx array/Unchanged
#	a2: ptr to FP_SCR1(a6)/Unchanged
#	a6: temp pointer to FP_SCR1(a6) - orig value saved and restored
#	fp0: Y/YINT
#	fp1: 10^ISCALE/Unchanged
#	fp2: x/x
#	F_SCR1:x/x
#	F_SCR2:Y adjusted for inex/Y with original exponent
#	L_SCR1:x/original USER_FPCR
#	L_SCR2:first word of X packed/Unchanged

A12_st:
	movm.l	&0xc0c0,-(%sp)	# save regs used by sintd0	 {%d0-%d1/%a0-%a1}
	mov.l	L_SCR1(%a6),-(%sp)
	mov.l	L_SCR2(%a6),-(%sp)

	lea.l		FP_SCR1(%a6),%a0	# a0 is ptr to FP_SCR1(a6)
	fmov.x		%fp0,(%a0)	# move Y to memory at FP_SCR1(a6)
	tst.l		L_SCR2(%a6)	# test sign of original operand
	bge.b		do_fint12		# if pos, use Y
	or.l		&0x80000000,(%a0)	# if neg, use -Y
do_fint12:
	mov.l	USER_FPSR(%a6),-(%sp)
#	bsr	sintdo		# sint routine returns int in fp0

	fmov.l	USER_FPCR(%a6),%fpcr
	fmov.l	&0x0,%fpsr			# clear the AEXC bits!!!
##	mov.l		USER_FPCR(%a6),%d0	# ext prec/keep rnd mode
##	andi.l		&0x00000030,%d0
##	fmov.l		%d0,%fpcr
	fint.x		FP_SCR1(%a6),%fp0	# do fint()
	fmov.l	%fpsr,%d0
	or.w	%d0,FPSR_EXCEPT(%a6)
##	fmov.l		&0x0,%fpcr
##	fmov.l		%fpsr,%d0		# don't keep ccodes
##	or.w		%d0,FPSR_EXCEPT(%a6)

	mov.b	(%sp),USER_FPSR(%a6)
	add.l	&4,%sp

	mov.l	(%sp)+,L_SCR2(%a6)
	mov.l	(%sp)+,L_SCR1(%a6)
	movm.l	(%sp)+,&0x303	# restore regs used by sint	 {%d0-%d1/%a0-%a1}

	mov.l	L_SCR2(%a6),FP_SCR1(%a6)	# restore original exponent
	mov.l	L_SCR1(%a6),USER_FPCR(%a6)	# restore user's FPCR

# A13. Check for LEN digits.
#      If the int operation results in more than LEN digits,
#      or less than LEN -1 digits, adjust ILOG and repeat from
#      A6.  This test occurs only on the first pass.  If the
#      result is exactly 10^LEN, decrement ILOG and divide
#      the mantissa by 10.  The calculation of 10^LEN cannot
#      be inexact, since all powers of ten upto 10^27 are exact
#      in extended precision, so the use of a previous power-of-ten
#      table will introduce no error.
#
#
# Register usage:
#	Input/Output
#	d0: FPCR with size set to ext/scratch final = 0
#	d2: x/x
#	d3: x/scratch final = x
#	d4: LEN/LEN adjusted
#	d5: ICTR:LAMBDA/LAMBDA:ICTR
#	d6: ILOG/ILOG adjusted
#	d7: k-factor/Unchanged
#	a0: pointer into memory for packed bcd string formation
#	a1: ptr to PTENxx array/Unchanged
#	a2: ptr to FP_SCR1(a6)/Unchanged
#	fp0: int portion of Y/abs(YINT) adjusted
#	fp1: 10^ISCALE/Unchanged
#	fp2: x/10^LEN
#	F_SCR1:x/x
#	F_SCR2:Y with original exponent/Unchanged
#	L_SCR1:original USER_FPCR/Unchanged
#	L_SCR2:first word of X packed/Unchanged

A13_st:
	swap		%d5		# put ICTR in lower word of d5
	tst.w		%d5		# check if ICTR = 0
	bne		not_zr		# if non-zero, go to second test
#
# Compute 10^(LEN-1)
#
	fmov.s		FONE(%pc),%fp2	# init fp2 to 1.0
	mov.l		%d4,%d0		# put LEN in d0
	subq.l		&1,%d0		# d0 = LEN -1
	clr.l		%d3		# clr table index
l_loop:
	lsr.l		&1,%d0		# shift next bit into carry
	bcc.b		l_next		# if zero, skip the mul
	fmul.x		(%a1,%d3),%fp2	# mul by 10**(d3_bit_no)
l_next:
	add.l		&12,%d3		# inc d3 to next pwrten table entry
	tst.l		%d0		# test if LEN is zero
	bne.b		l_loop		# if not, loop
#
# 10^LEN-1 is computed for this test and A14.  If the input was
# denormalized, check only the case in which YINT > 10^LEN.
#
	tst.b		BINDEC_FLG(%a6)	# check if input was norm
	beq.b		A13_con		# if norm, continue with checking
	fabs.x		%fp0		# take abs of YINT
	bra		test_2
#
# Compare abs(YINT) to 10^(LEN-1) and 10^LEN
#
A13_con:
	fabs.x		%fp0		# take abs of YINT
	fcmp.x		%fp0,%fp2	# compare abs(YINT) with 10^(LEN-1)
	fbge.w		test_2		# if greater, do next test
	subq.l		&1,%d6		# subtract 1 from ILOG
	mov.w		&1,%d5		# set ICTR
	fmov.l		&rm_mode*0x10,%fpcr	# set rmode to RM
	fmul.s		FTEN(%pc),%fp2	# compute 10^LEN
	bra.w		A6_str		# return to A6 and recompute YINT
test_2:
	fmul.s		FTEN(%pc),%fp2	# compute 10^LEN
	fcmp.x		%fp0,%fp2	# compare abs(YINT) with 10^LEN
	fblt.w		A14_st		# if less, all is ok, go to A14
	fbgt.w		fix_ex		# if greater, fix and redo
	fdiv.s		FTEN(%pc),%fp0	# if equal, divide by 10
	addq.l		&1,%d6		# and inc ILOG
	bra.b		A14_st		# and continue elsewhere
fix_ex:
	addq.l		&1,%d6		# increment ILOG by 1
	mov.w		&1,%d5		# set ICTR
	fmov.l		&rm_mode*0x10,%fpcr	# set rmode to RM
	bra.w		A6_str		# return to A6 and recompute YINT
#
# Since ICTR <> 0, we have already been through one adjustment,
# and shouldn't have another; this is to check if abs(YINT) = 10^LEN
# 10^LEN is again computed using whatever table is in a1 since the
# value calculated cannot be inexact.
#
not_zr:
	fmov.s		FONE(%pc),%fp2	# init fp2 to 1.0
	mov.l		%d4,%d0		# put LEN in d0
	clr.l		%d3		# clr table index
z_loop:
	lsr.l		&1,%d0		# shift next bit into carry
	bcc.b		z_next		# if zero, skip the mul
	fmul.x		(%a1,%d3),%fp2	# mul by 10**(d3_bit_no)
z_next:
	add.l		&12,%d3		# inc d3 to next pwrten table entry
	tst.l		%d0		# test if LEN is zero
	bne.b		z_loop		# if not, loop
	fabs.x		%fp0		# get abs(YINT)
	fcmp.x		%fp0,%fp2	# check if abs(YINT) = 10^LEN
	fbneq.w		A14_st		# if not, skip this
	fdiv.s		FTEN(%pc),%fp0	# divide abs(YINT) by 10
	addq.l		&1,%d6		# and inc ILOG by 1
	addq.l		&1,%d4		# and inc LEN
	fmul.s		FTEN(%pc),%fp2	# if LEN++, the get 10^^LEN

# A14. Convert the mantissa to bcd.
#      The binstr routine is used to convert the LEN digit
#      mantissa to bcd in memory.  The input to binstr is
#      to be a fraction; i.e. (mantissa)/10^LEN and adjusted
#      such that the decimal point is to the left of bit 63.
#      The bcd digits are stored in the correct position in
#      the final string area in memory.
#
#
# Register usage:
#	Input/Output
#	d0: x/LEN call to binstr - final is 0
#	d1: x/0
#	d2: x/ms 32-bits of mant of abs(YINT)
#	d3: x/ls 32-bits of mant of abs(YINT)
#	d4: LEN/Unchanged
#	d5: ICTR:LAMBDA/LAMBDA:ICTR
#	d6: ILOG
#	d7: k-factor/Unchanged
#	a0: pointer into memory for packed bcd string formation
#	    /ptr to first mantissa byte in result string
#	a1: ptr to PTENxx array/Unchanged
#	a2: ptr to FP_SCR1(a6)/Unchanged
#	fp0: int portion of Y/abs(YINT) adjusted
#	fp1: 10^ISCALE/Unchanged
#	fp2: 10^LEN/Unchanged
#	F_SCR1:x/Work area for final result
#	F_SCR2:Y with original exponent/Unchanged
#	L_SCR1:original USER_FPCR/Unchanged
#	L_SCR2:first word of X packed/Unchanged

A14_st:
	fmov.l		&rz_mode*0x10,%fpcr	# force rz for conversion
	fdiv.x		%fp2,%fp0	# divide abs(YINT) by 10^LEN
	lea.l		FP_SCR0(%a6),%a0
	fmov.x		%fp0,(%a0)	# move abs(YINT)/10^LEN to memory
	mov.l		4(%a0),%d2	# move 2nd word of FP_RES to d2
	mov.l		8(%a0),%d3	# move 3rd word of FP_RES to d3
	clr.l		4(%a0)		# zero word 2 of FP_RES
	clr.l		8(%a0)		# zero word 3 of FP_RES
	mov.l		(%a0),%d0	# move exponent to d0
	swap		%d0		# put exponent in lower word
	beq.b		no_sft		# if zero, don't shift
	sub.l		&0x3ffd,%d0	# sub bias less 2 to make fract
	tst.l		%d0		# check if > 1
	bgt.b		no_sft		# if so, don't shift
	neg.l		%d0		# make exp positive
m_loop:
	lsr.l		&1,%d2		# shift d2:d3 right, add 0s
	roxr.l		&1,%d3		# the number of places
	dbf.w		%d0,m_loop	# given in d0
no_sft:
	tst.l		%d2		# check for mantissa of zero
	bne.b		no_zr		# if not, go on
	tst.l		%d3		# continue zero check
	beq.b		zer_m		# if zero, go directly to binstr
no_zr:
	clr.l		%d1		# put zero in d1 for addx
	add.l		&0x00000080,%d3	# inc at bit 7
	addx.l		%d1,%d2		# continue inc
	and.l		&0xffffff80,%d3	# strip off lsb not used by 882
zer_m:
	mov.l		%d4,%d0		# put LEN in d0 for binstr call
	addq.l		&3,%a0		# a0 points to M16 byte in result
	bsr		binstr		# call binstr to convert mant


# A15. Convert the exponent to bcd.
#      As in A14 above, the exp is converted to bcd and the
#      digits are stored in the final string.
#
#      Digits are stored in L_SCR1(a6) on return from BINDEC as:
#
#	 32               16 15                0
#	-----------------------------------------
#	|  0 | e3 | e2 | e1 | e4 |  X |  X |  X |
#	-----------------------------------------
#
# And are moved into their proper places in FP_SCR0.  If digit e4
# is non-zero, OPERR is signaled.  In all cases, all 4 digits are
# written as specified in the 881/882 manual for packed decimal.
#
# Register usage:
#	Input/Output
#	d0: x/LEN call to binstr - final is 0
#	d1: x/scratch (0);shift count for final exponent packing
#	d2: x/ms 32-bits of exp fraction/scratch
#	d3: x/ls 32-bits of exp fraction
#	d4: LEN/Unchanged
#	d5: ICTR:LAMBDA/LAMBDA:ICTR
#	d6: ILOG
#	d7: k-factor/Unchanged
#	a0: ptr to result string/ptr to L_SCR1(a6)
#	a1: ptr to PTENxx array/Unchanged
#	a2: ptr to FP_SCR1(a6)/Unchanged
#	fp0: abs(YINT) adjusted/float(ILOG)
#	fp1: 10^ISCALE/Unchanged
#	fp2: 10^LEN/Unchanged
#	F_SCR1:Work area for final result/BCD result
#	F_SCR2:Y with original exponent/ILOG/10^4
#	L_SCR1:original USER_FPCR/Exponent digits on return from binstr
#	L_SCR2:first word of X packed/Unchanged

A15_st:
	tst.b		BINDEC_FLG(%a6)	# check for denorm
	beq.b		not_denorm
	ftest.x		%fp0		# test for zero
	fbeq.w		den_zero	# if zero, use k-factor or 4933
	fmov.l		%d6,%fp0	# float ILOG
	fabs.x		%fp0		# get abs of ILOG
	bra.b		convrt
den_zero:
	tst.l		%d7		# check sign of the k-factor
	blt.b		use_ilog	# if negative, use ILOG
	fmov.s		F4933(%pc),%fp0	# force exponent to 4933
	bra.b		convrt		# do it
use_ilog:
	fmov.l		%d6,%fp0	# float ILOG
	fabs.x		%fp0		# get abs of ILOG
	bra.b		convrt
not_denorm:
	ftest.x		%fp0		# test for zero
	fbneq.w		not_zero	# if zero, force exponent
	fmov.s		FONE(%pc),%fp0	# force exponent to 1
	bra.b		convrt		# do it
not_zero:
	fmov.l		%d6,%fp0	# float ILOG
	fabs.x		%fp0		# get abs of ILOG
convrt:
	fdiv.x		24(%a1),%fp0	# compute ILOG/10^4
	fmov.x		%fp0,FP_SCR1(%a6)	# store fp0 in memory
	mov.l		4(%a2),%d2	# move word 2 to d2
	mov.l		8(%a2),%d3	# move word 3 to d3
	mov.w		(%a2),%d0	# move exp to d0
	beq.b		x_loop_fin	# if zero, skip the shift
	sub.w		&0x3ffd,%d0	# subtract off bias
	neg.w		%d0		# make exp positive
x_loop:
	lsr.l		&1,%d2		# shift d2:d3 right
	roxr.l		&1,%d3		# the number of places
	dbf.w		%d0,x_loop	# given in d0
x_loop_fin:
	clr.l		%d1		# put zero in d1 for addx
	add.l		&0x00000080,%d3	# inc at bit 6
	addx.l		%d1,%d2		# continue inc
	and.l		&0xffffff80,%d3	# strip off lsb not used by 882
	mov.l		&4,%d0		# put 4 in d0 for binstr call
	lea.l		L_SCR1(%a6),%a0	# a0 is ptr to L_SCR1 for exp digits
	bsr		binstr		# call binstr to convert exp
	mov.l		L_SCR1(%a6),%d0	# load L_SCR1 lword to d0
	mov.l		&12,%d1		# use d1 for shift count
	lsr.l		%d1,%d0		# shift d0 right by 12
	bfins		%d0,FP_SCR0(%a6){&4:&12}	# put e3:e2:e1 in FP_SCR0
	lsr.l		%d1,%d0		# shift d0 right by 12
	bfins		%d0,FP_SCR0(%a6){&16:&4}	# put e4 in FP_SCR0
	tst.b		%d0		# check if e4 is zero
	beq.b		A16_st		# if zero, skip rest
	or.l		&opaop_mask,USER_FPSR(%a6)	# set OPERR & AIOP in USER_FPSR


# A16. Write sign bits to final string.
#	   Sigma is bit 31 of initial value; RHO is bit 31 of d6 (ILOG).
#
# Register usage:
#	Input/Output
#	d0: x/scratch - final is x
#	d2: x/x
#	d3: x/x
#	d4: LEN/Unchanged
#	d5: ICTR:LAMBDA/LAMBDA:ICTR
#	d6: ILOG/ILOG adjusted
#	d7: k-factor/Unchanged
#	a0: ptr to L_SCR1(a6)/Unchanged
#	a1: ptr to PTENxx array/Unchanged
#	a2: ptr to FP_SCR1(a6)/Unchanged
#	fp0: float(ILOG)/Unchanged
#	fp1: 10^ISCALE/Unchanged
#	fp2: 10^LEN/Unchanged
#	F_SCR1:BCD result with correct signs
#	F_SCR2:ILOG/10^4
#	L_SCR1:Exponent digits on return from binstr
#	L_SCR2:first word of X packed/Unchanged

A16_st:
	clr.l		%d0		# clr d0 for collection of signs
	and.b		&0x0f,FP_SCR0(%a6)	# clear first nibble of FP_SCR0
	tst.l		L_SCR2(%a6)	# check sign of original mantissa
	bge.b		mant_p		# if pos, don't set SM
	mov.l		&2,%d0		# move 2 in to d0 for SM
mant_p:
	tst.l		%d6		# check sign of ILOG
	bge.b		wr_sgn		# if pos, don't set SE
	addq.l		&1,%d0		# set bit 0 in d0 for SE
wr_sgn:
	bfins		%d0,FP_SCR0(%a6){&0:&2}	# insert SM and SE into FP_SCR0

# Clean up and restore all registers used.

	fmov.l		&0,%fpsr	# clear possible inex2/ainex bits
	fmovm.x		(%sp)+,&0xe0	#  {%fp0-%fp2}
	movm.l		(%sp)+,&0x4fc	#  {%d2-%d7/%a2}
	rts

	global		PTENRN
PTENRN:
	long		0x40020000,0xA0000000,0x00000000	# 10 ^ 1
	long		0x40050000,0xC8000000,0x00000000	# 10 ^ 2
	long		0x400C0000,0x9C400000,0x00000000	# 10 ^ 4
	long		0x40190000,0xBEBC2000,0x00000000	# 10 ^ 8
	long		0x40340000,0x8E1BC9BF,0x04000000	# 10 ^ 16
	long		0x40690000,0x9DC5ADA8,0x2B70B59E	# 10 ^ 32
	long		0x40D30000,0xC2781F49,0xFFCFA6D5	# 10 ^ 64
	long		0x41A80000,0x93BA47C9,0x80E98CE0	# 10 ^ 128
	long		0x43510000,0xAA7EEBFB,0x9DF9DE8E	# 10 ^ 256
	long		0x46A30000,0xE319A0AE,0xA60E91C7	# 10 ^ 512
	long		0x4D480000,0xC9767586,0x81750C17	# 10 ^ 1024
	long		0x5A920000,0x9E8B3B5D,0xC53D5DE5	# 10 ^ 2048
	long		0x75250000,0xC4605202,0x8A20979B	# 10 ^ 4096

	global		PTENRP
PTENRP:
	long		0x40020000,0xA0000000,0x00000000	# 10 ^ 1
	long		0x40050000,0xC8000000,0x00000000	# 10 ^ 2
	long		0x400C0000,0x9C400000,0x00000000	# 10 ^ 4
	long		0x40190000,0xBEBC2000,0x00000000	# 10 ^ 8
	long		0x40340000,0x8E1BC9BF,0x04000000	# 10 ^ 16
	long		0x40690000,0x9DC5ADA8,0x2B70B59E	# 10 ^ 32
	long		0x40D30000,0xC2781F49,0xFFCFA6D6	# 10 ^ 64
	long		0x41A80000,0x93BA47C9,0x80E98CE0	# 10 ^ 128
	long		0x43510000,0xAA7EEBFB,0x9DF9DE8E	# 10 ^ 256
	long		0x46A30000,0xE319A0AE,0xA60E91C7	# 10 ^ 512
	long		0x4D480000,0xC9767586,0x81750C18	# 10 ^ 1024
	long		0x5A920000,0x9E8B3B5D,0xC53D5DE5	# 10 ^ 2048
	long		0x75250000,0xC4605202,0x8A20979B	# 10 ^ 4096

	global		PTENRM
PTENRM:
	long		0x40020000,0xA0000000,0x00000000	# 10 ^ 1
	long		0x40050000,0xC8000000,0x00000000	# 10 ^ 2
	long		0x400C0000,0x9C400000,0x00000000	# 10 ^ 4
	long		0x40190000,0xBEBC2000,0x00000000	# 10 ^ 8
	long		0x40340000,0x8E1BC9BF,0x04000000	# 10 ^ 16
	long		0x40690000,0x9DC5ADA8,0x2B70B59D	# 10 ^ 32
	long		0x40D30000,0xC2781F49,0xFFCFA6D5	# 10 ^ 64
	long		0x41A80000,0x93BA47C9,0x80E98CDF	# 10 ^ 128
	long		0x43510000,0xAA7EEBFB,0x9DF9DE8D	# 10 ^ 256
	long		0x46A30000,0xE319A0AE,0xA60E91C6	# 10 ^ 512
	long		0x4D480000,0xC9767586,0x81750C17	# 10 ^ 1024
	long		0x5A920000,0x9E8B3B5D,0xC53D5DE4	# 10 ^ 2048
	long		0x75250000,0xC4605202,0x8A20979A	# 10 ^ 4096

#########################################################################
# binstr(): Converts a 64-bit binary integer to bcd.			#
#									#
# INPUT *************************************************************** #
#	d2:d3 = 64-bit binary integer					#
#	d0    = desired length (LEN)					#
#	a0    = pointer to start in memory for bcd characters		#
#		(This pointer must point to byte 4 of the first		#
#		 lword of the packed decimal memory string.)		#
#									#
# OUTPUT ************************************************************** #
#	a0 = pointer to LEN bcd digits representing the 64-bit integer.	#
#									#
# ALGORITHM ***********************************************************	#
#	The 64-bit binary is assumed to have a decimal point before	#
#	bit 63.  The fraction is multiplied by 10 using a mul by 2	#
#	shift and a mul by 8 shift.  The bits shifted out of the	#
#	msb form a decimal digit.  This process is iterated until	#
#	LEN digits are formed.						#
#									#
# A1. Init d7 to 1.  D7 is the byte digit counter, and if 1, the	#
#     digit formed will be assumed the least significant.  This is	#
#     to force the first byte formed to have a 0 in the upper 4 bits.	#
#									#
# A2. Beginning of the loop:						#
#     Copy the fraction in d2:d3 to d4:d5.				#
#									#
# A3. Multiply the fraction in d2:d3 by 8 using bit-field		#
#     extracts and shifts.  The three msbs from d2 will go into d1.	#
#									#
# A4. Multiply the fraction in d4:d5 by 2 using shifts.  The msb	#
#     will be collected by the carry.					#
#									#
# A5. Add using the carry the 64-bit quantities in d2:d3 and d4:d5	#
#     into d2:d3.  D1 will contain the bcd digit formed.		#
#									#
# A6. Test d7.  If zero, the digit formed is the ms digit.  If non-	#
#     zero, it is the ls digit.  Put the digit in its place in the	#
#     upper word of d0.  If it is the ls digit, write the word		#
#     from d0 to memory.						#
#									#
# A7. Decrement d6 (LEN counter) and repeat the loop until zero.	#
#									#
#########################################################################

#	Implementation Notes:
#
#	The registers are used as follows:
#
#		d0: LEN counter
#		d1: temp used to form the digit
#		d2: upper 32-bits of fraction for mul by 8
#		d3: lower 32-bits of fraction for mul by 8
#		d4: upper 32-bits of fraction for mul by 2
#		d5: lower 32-bits of fraction for mul by 2
#		d6: temp for bit-field extracts
#		d7: byte digit formation word;digit count {0,1}
#		a0: pointer into memory for packed bcd string formation
#

	global		binstr
binstr:
	movm.l		&0xff00,-(%sp)	#  {%d0-%d7}

#
# A1: Init d7
#
	mov.l		&1,%d7		# init d7 for second digit
	subq.l		&1,%d0		# for dbf d0 would have LEN+1 passes
#
# A2. Copy d2:d3 to d4:d5.  Start loop.
#
loop:
	mov.l		%d2,%d4		# copy the fraction before muls
	mov.l		%d3,%d5		# to d4:d5
#
# A3. Multiply d2:d3 by 8; extract msbs into d1.
#
	bfextu		%d2{&0:&3},%d1	# copy 3 msbs of d2 into d1
	asl.l		&3,%d2		# shift d2 left by 3 places
	bfextu		%d3{&0:&3},%d6	# copy 3 msbs of d3 into d6
	asl.l		&3,%d3		# shift d3 left by 3 places
	or.l		%d6,%d2		# or in msbs from d3 into d2
#
# A4. Multiply d4:d5 by 2; add carry out to d1.
#
	asl.l		&1,%d5		# mul d5 by 2
	roxl.l		&1,%d4		# mul d4 by 2
	swap		%d6		# put 0 in d6 lower word
	addx.w		%d6,%d1		# add in extend from mul by 2
#
# A5. Add mul by 8 to mul by 2.  D1 contains the digit formed.
#
	add.l		%d5,%d3		# add lower 32 bits
	nop				# ERRATA FIX #13 (Rev. 1.2 6/6/90)
	addx.l		%d4,%d2		# add with extend upper 32 bits
	nop				# ERRATA FIX #13 (Rev. 1.2 6/6/90)
	addx.w		%d6,%d1		# add in extend from add to d1
	swap		%d6		# with d6 = 0; put 0 in upper word
#
# A6. Test d7 and branch.
#
	tst.w		%d7		# if zero, store digit & to loop
	beq.b		first_d		# if non-zero, form byte & write
sec_d:
	swap		%d7		# bring first digit to word d7b
	asl.w		&4,%d7		# first digit in upper 4 bits d7b
	add.w		%d1,%d7		# add in ls digit to d7b
	mov.b		%d7,(%a0)+	# store d7b byte in memory
	swap		%d7		# put LEN counter in word d7a
	clr.w		%d7		# set d7a to signal no digits done
	dbf.w		%d0,loop	# do loop some more!
	bra.b		end_bstr	# finished, so exit
first_d:
	swap		%d7		# put digit word in d7b
	mov.w		%d1,%d7		# put new digit in d7b
	swap		%d7		# put LEN counter in word d7a
	addq.w		&1,%d7		# set d7a to signal first digit done
	dbf.w		%d0,loop	# do loop some more!
	swap		%d7		# put last digit in string
	lsl.w		&4,%d7		# move it to upper 4 bits
	mov.b		%d7,(%a0)+	# store it in memory string
#
# Clean up and return with result in fp0.
#
end_bstr:
	movm.l		(%sp)+,&0xff	#  {%d0-%d7}
	rts

#########################################################################
# XDEF ****************************************************************	#
#	facc_in_b(): dmem_read_byte failed				#
#	facc_in_w(): dmem_read_word failed				#
#	facc_in_l(): dmem_read_long failed				#
#	facc_in_d(): dmem_read of dbl prec failed			#
#	facc_in_x(): dmem_read of ext prec failed			#
#									#
#	facc_out_b(): dmem_write_byte failed				#
#	facc_out_w(): dmem_write_word failed				#
#	facc_out_l(): dmem_write_long failed				#
#	facc_out_d(): dmem_write of dbl prec failed			#
#	facc_out_x(): dmem_write of ext prec failed			#
#									#
# XREF ****************************************************************	#
#	_real_access() - exit through access error handler		#
#									#
# INPUT ***************************************************************	#
#	None								#
#									#
# OUTPUT **************************************************************	#
#	None								#
#									#
# ALGORITHM ***********************************************************	#
#	Flow jumps here when an FP data fetch call gets an error	#
# result. This means the operating system wants an access error frame	#
# made out of the current exception stack frame.			#
#	So, we first call restore() which makes sure that any updated	#
# -(an)+ register gets returned to its pre-exception value and then	#
# we change the stack to an access error stack frame.			#
#									#
#########################################################################

facc_in_b:
	movq.l		&0x1,%d0			# one byte
	bsr.w		restore				# fix An

	mov.w		&0x0121,EXC_VOFF(%a6)		# set FSLW
	bra.w		facc_finish

facc_in_w:
	movq.l		&0x2,%d0			# two bytes
	bsr.w		restore				# fix An

	mov.w		&0x0141,EXC_VOFF(%a6)		# set FSLW
	bra.b		facc_finish

facc_in_l:
	movq.l		&0x4,%d0			# four bytes
	bsr.w		restore				# fix An

	mov.w		&0x0101,EXC_VOFF(%a6)		# set FSLW
	bra.b		facc_finish

facc_in_d:
	movq.l		&0x8,%d0			# eight bytes
	bsr.w		restore				# fix An

	mov.w		&0x0161,EXC_VOFF(%a6)		# set FSLW
	bra.b		facc_finish

facc_in_x:
	movq.l		&0xc,%d0			# twelve bytes
	bsr.w		restore				# fix An

	mov.w		&0x0161,EXC_VOFF(%a6)		# set FSLW
	bra.b		facc_finish

################################################################

facc_out_b:
	movq.l		&0x1,%d0			# one byte
	bsr.w		restore				# restore An

	mov.w		&0x00a1,EXC_VOFF(%a6)		# set FSLW
	bra.b		facc_finish

facc_out_w:
	movq.l		&0x2,%d0			# two bytes
	bsr.w		restore				# restore An

	mov.w		&0x00c1,EXC_VOFF(%a6)		# set FSLW
	bra.b		facc_finish

facc_out_l:
	movq.l		&0x4,%d0			# four bytes
	bsr.w		restore				# restore An

	mov.w		&0x0081,EXC_VOFF(%a6)		# set FSLW
	bra.b		facc_finish

facc_out_d:
	movq.l		&0x8,%d0			# eight bytes
	bsr.w		restore				# restore An

	mov.w		&0x00e1,EXC_VOFF(%a6)		# set FSLW
	bra.b		facc_finish

facc_out_x:
	mov.l		&0xc,%d0			# twelve bytes
	bsr.w		restore				# restore An

	mov.w		&0x00e1,EXC_VOFF(%a6)		# set FSLW

# here's where we actually create the access error frame from the
# current exception stack frame.
facc_finish:
	mov.l		USER_FPIAR(%a6),EXC_PC(%a6) # store current PC

	fmovm.x		EXC_FPREGS(%a6),&0xc0	# restore fp0-fp1
	fmovm.l		USER_FPCR(%a6),%fpcr,%fpsr,%fpiar # restore ctrl regs
	movm.l		EXC_DREGS(%a6),&0x0303	# restore d0-d1/a0-a1

	unlk		%a6

	mov.l		(%sp),-(%sp)		# store SR, hi(PC)
	mov.l		0x8(%sp),0x4(%sp)	# store lo(PC)
	mov.l		0xc(%sp),0x8(%sp)	# store EA
	mov.l		&0x00000001,0xc(%sp)	# store FSLW
	mov.w		0x6(%sp),0xc(%sp)	# fix FSLW (size)
	mov.w		&0x4008,0x6(%sp)	# store voff

	btst		&0x5,(%sp)		# supervisor or user mode?
	beq.b		facc_out2		# user
	bset		&0x2,0xd(%sp)		# set supervisor TM bit

facc_out2:
	bra.l		_real_access

##################################################################

# if the effective addressing mode was predecrement or postincrement,
# the emulation has already changed its value to the correct post-
# instruction value. but since we're exiting to the access error
# handler, then AN must be returned to its pre-instruction value.
# we do that here.
restore:
	mov.b		EXC_OPWORD+0x1(%a6),%d1
	andi.b		&0x38,%d1		# extract opmode
	cmpi.b		%d1,&0x18		# postinc?
	beq.w		rest_inc
	cmpi.b		%d1,&0x20		# predec?
	beq.w		rest_dec
	rts

rest_inc:
	mov.b		EXC_OPWORD+0x1(%a6),%d1
	andi.w		&0x0007,%d1		# fetch An

	mov.w		(tbl_rest_inc.b,%pc,%d1.w*2),%d1
	jmp		(tbl_rest_inc.b,%pc,%d1.w*1)

tbl_rest_inc:
	short		ri_a0 - tbl_rest_inc
	short		ri_a1 - tbl_rest_inc
	short		ri_a2 - tbl_rest_inc
	short		ri_a3 - tbl_rest_inc
	short		ri_a4 - tbl_rest_inc
	short		ri_a5 - tbl_rest_inc
	short		ri_a6 - tbl_rest_inc
	short		ri_a7 - tbl_rest_inc

ri_a0:
	sub.l		%d0,EXC_DREGS+0x8(%a6)	# fix stacked a0
	rts
ri_a1:
	sub.l		%d0,EXC_DREGS+0xc(%a6)	# fix stacked a1
	rts
ri_a2:
	sub.l		%d0,%a2			# fix a2
	rts
ri_a3:
	sub.l		%d0,%a3			# fix a3
	rts
ri_a4:
	sub.l		%d0,%a4			# fix a4
	rts
ri_a5:
	sub.l		%d0,%a5			# fix a5
	rts
ri_a6:
	sub.l		%d0,(%a6)		# fix stacked a6
	rts
# if it's a fmove out instruction, we don't have to fix a7
# because we hadn't changed it yet. if it's an opclass two
# instruction (data moved in) and the exception was in supervisor
# mode, then also also wasn't updated. if it was user mode, then
# restore the correct a7 which is in the USP currently.
ri_a7:
	cmpi.b		EXC_VOFF(%a6),&0x30	# move in or out?
	bne.b		ri_a7_done		# out

	btst		&0x5,EXC_SR(%a6)	# user or supervisor?
	bne.b		ri_a7_done		# supervisor
	movc		%usp,%a0		# restore USP
	sub.l		%d0,%a0
	movc		%a0,%usp
ri_a7_done:
	rts

# need to invert adjustment value if the <ea> was predec
rest_dec:
	neg.l		%d0
	bra.b		rest_inc
