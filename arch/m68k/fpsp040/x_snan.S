|
|	x_snan.sa 3.3 7/1/91
|
| fpsp_snan --- FPSP handler for signalling NAN exception
|
| SNAN for float -> integer conversions (integer conversion of
| an SNAN) is a non-maskable run-time exception.
|
| For trap disabled the 040 does the following:
| If the dest data format is s, d, or x, then the SNAN bit in the NAN
| is set to one and the resulting non-signaling NAN (truncated if
| necessary) is transferred to the dest.  If the dest format is b, w,
| or l, then garbage is written to the dest (actually the upper 32 bits
| of the mantissa are sent to the integer unit).
|
| For trap enabled the 040 does the following:
| If the inst is move_out, then the results are the same as for trap
| disabled with the exception posted.  If the instruction is not move_
| out, the dest. is not modified, and the exception is posted.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

X_SNAN:	|idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

#include "fpsp.h"

	|xref	get_fline
	|xref	mem_write
	|xref	real_snan
	|xref	real_inex
	|xref	fpsp_done
	|xref	reg_dest

	.global	fpsp_snan
fpsp_snan:
	link		%a6,#-LOCAL_SIZE
	fsave		-(%a7)
	moveml		%d0-%d1/%a0-%a1,USER_DA(%a6)
	fmovemx	%fp0-%fp3,USER_FP0(%a6)
	fmoveml	%fpcr/%fpsr/%fpiar,USER_FPCR(%a6)

|
| Check if trap enabled
|
	btstb		#snan_bit,FPCR_ENABLE(%a6)
	bnes		ena		|If enabled, then branch

	bsrl		move_out	|else SNAN disabled
|
| It is possible to have an inex1 exception with the
| snan.  If the inex enable bit is set in the FPCR, and either
| inex2 or inex1 occurred, we must clean up and branch to the
| real inex handler.
|
ck_inex:
	moveb	FPCR_ENABLE(%a6),%d0
	andb	FPSR_EXCEPT(%a6),%d0
	andib	#0x3,%d0
	beq	end_snan
|
| Inexact enabled and reported, and we must take an inexact exception.
|
take_inex:
	moveb		#INEX_VEC,EXC_VEC+1(%a6)
	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore	(%a7)+
	unlk		%a6
	bral		real_inex
|
| SNAN is enabled.  Check if inst is move_out.
| Make any corrections to the 040 output as necessary.
|
ena:
	btstb		#5,CMDREG1B(%a6) |if set, inst is move out
	beq		not_out

	bsrl		move_out

report_snan:
	moveb		(%a7),VER_TMP(%a6)
	cmpib		#VER_40,(%a7)	|test for orig unimp frame
	bnes		ck_rev
	moveql		#13,%d0		|need to zero 14 lwords
	bras		rep_con
ck_rev:
	moveql		#11,%d0		|need to zero 12 lwords
rep_con:
	clrl		(%a7)
loop1:
	clrl		-(%a7)		|clear and dec a7
	dbra		%d0,loop1
	moveb		VER_TMP(%a6),(%a7) |format a busy frame
	moveb		#BUSY_SIZE-4,1(%a7)
	movel		USER_FPSR(%a6),FPSR_SHADOW(%a6)
	orl		#sx_mask,E_BYTE(%a6)
	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore	(%a7)+
	unlk		%a6
	bral		real_snan
|
| Exit snan handler by expanding the unimp frame into a busy frame
|
end_snan:
	bclrb		#E1,E_BYTE(%a6)

	moveb		(%a7),VER_TMP(%a6)
	cmpib		#VER_40,(%a7)	|test for orig unimp frame
	bnes		ck_rev2
	moveql		#13,%d0		|need to zero 14 lwords
	bras		rep_con2
ck_rev2:
	moveql		#11,%d0		|need to zero 12 lwords
rep_con2:
	clrl		(%a7)
loop2:
	clrl		-(%a7)		|clear and dec a7
	dbra		%d0,loop2
	moveb		VER_TMP(%a6),(%a7) |format a busy frame
	moveb		#BUSY_SIZE-4,1(%a7) |write busy size
	movel		USER_FPSR(%a6),FPSR_SHADOW(%a6)
	orl		#sx_mask,E_BYTE(%a6)
	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore	(%a7)+
	unlk		%a6
	bral		fpsp_done

|
| Move_out
|
move_out:
	movel		EXC_EA(%a6),%a0	|get <ea> from exc frame

	bfextu		CMDREG1B(%a6){#3:#3},%d0 |move rx field to d0{2:0}
	cmpil		#0,%d0		|check for long
	beqs		sto_long	|branch if move_out long

	cmpil		#4,%d0		|check for word
	beqs		sto_word	|branch if move_out word

	cmpil		#6,%d0		|check for byte
	beqs		sto_byte	|branch if move_out byte

|
| Not byte, word or long
|
	rts
|
| Get the 32 most significant bits of etemp mantissa
|
sto_long:
	movel		ETEMP_HI(%a6),%d1
	movel		#4,%d0		|load byte count
|
| Set signalling nan bit
|
	bsetl		#30,%d1
|
| Store to the users destination address
|
	tstl		%a0		|check if <ea> is 0
	beqs		wrt_dn		|destination is a data register

	movel		%d1,-(%a7)	|move the snan onto the stack
	movel		%a0,%a1		|load dest addr into a1
	movel		%a7,%a0		|load src addr of snan into a0
	bsrl		mem_write	|write snan to user memory
	movel		(%a7)+,%d1	|clear off stack
	rts
|
| Get the 16 most significant bits of etemp mantissa
|
sto_word:
	movel		ETEMP_HI(%a6),%d1
	movel		#2,%d0		|load byte count
|
| Set signalling nan bit
|
	bsetl		#30,%d1
|
| Store to the users destination address
|
	tstl		%a0		|check if <ea> is 0
	beqs		wrt_dn		|destination is a data register

	movel		%d1,-(%a7)	|move the snan onto the stack
	movel		%a0,%a1		|load dest addr into a1
	movel		%a7,%a0		|point to low word
	bsrl		mem_write	|write snan to user memory
	movel		(%a7)+,%d1	|clear off stack
	rts
|
| Get the 8 most significant bits of etemp mantissa
|
sto_byte:
	movel		ETEMP_HI(%a6),%d1
	movel		#1,%d0		|load byte count
|
| Set signalling nan bit
|
	bsetl		#30,%d1
|
| Store to the users destination address
|
	tstl		%a0		|check if <ea> is 0
	beqs		wrt_dn		|destination is a data register
	movel		%d1,-(%a7)	|move the snan onto the stack
	movel		%a0,%a1		|load dest addr into a1
	movel		%a7,%a0		|point to source byte
	bsrl		mem_write	|write snan to user memory
	movel		(%a7)+,%d1	|clear off stack
	rts

|
|	wrt_dn --- write to a data register
|
|	We get here with D1 containing the data to write and D0 the
|	number of bytes to write: 1=byte,2=word,4=long.
|
wrt_dn:
	movel		%d1,L_SCR1(%a6)	|data
	movel		%d0,-(%a7)	|size
	bsrl		get_fline	|returns fline word in d0
	movel		%d0,%d1
	andil		#0x7,%d1		|d1 now holds register number
	movel		(%sp)+,%d0	|get original size
	cmpil		#4,%d0
	beqs		wrt_long
	cmpil		#2,%d0
	bnes		wrt_byte
wrt_word:
	orl		#0x8,%d1
	bral		reg_dest
wrt_long:
	orl		#0x10,%d1
	bral		reg_dest
wrt_byte:
	bral		reg_dest
|
| Check if it is a src nan or dst nan
|
not_out:
	movel		DTAG(%a6),%d0
	bfextu		%d0{#0:#3},%d0	|isolate dtag in lsbs

	cmpib		#3,%d0		|check for nan in destination
	bnes		issrc		|destination nan has priority
dst_nan:
	btstb		#6,FPTEMP_HI(%a6) |check if dest nan is an snan
	bnes		issrc		|no, so check source for snan
	movew		FPTEMP_EX(%a6),%d0
	bras		cont
issrc:
	movew		ETEMP_EX(%a6),%d0
cont:
	btstl		#15,%d0		|test for sign of snan
	beqs		clr_neg
	bsetb		#neg_bit,FPSR_CC(%a6)
	bra		report_snan
clr_neg:
	bclrb		#neg_bit,FPSR_CC(%a6)
	bra		report_snan

	|end
