|
|	gen_except.sa 3.7 1/16/92
|
|	gen_except --- FPSP routine to detect reportable exceptions
|
|	This routine compares the exception enable byte of the
|	user_fpcr on the stack with the exception status byte
|	of the user_fpsr.
|
|	Any routine which may report an exceptions must load
|	the stack frame in memory with the exceptional operand(s).
|
|	Priority for exceptions is:
|
|	Highest:	bsun
|			snan
|			operr
|			ovfl
|			unfl
|			dz
|			inex2
|	Lowest:		inex1
|
|	Note: The IEEE standard specifies that inex2 is to be
|	reported if ovfl occurs and the ovfl enable bit is not
|	set but the inex2 enable bit is.
|
|
|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

GEN_EXCEPT:    |idnt    2,1 | Motorola 040 Floating Point Software Package

	|section 8

#include "fpsp.h"

	|xref	real_trace
	|xref	fpsp_done
	|xref	fpsp_fmt_error

exc_tbl:
	.long	bsun_exc
	.long	commonE1
	.long	commonE1
	.long	ovfl_unfl
	.long	ovfl_unfl
	.long	commonE1
	.long	commonE3
	.long	commonE3
	.long	no_match

	.global	gen_except
gen_except:
	cmpib	#IDLE_SIZE-4,1(%a7)	|test for idle frame
	beq	do_check		|go handle idle frame
	cmpib	#UNIMP_40_SIZE-4,1(%a7)	|test for orig unimp frame
	beqs	unimp_x			|go handle unimp frame
	cmpib	#UNIMP_41_SIZE-4,1(%a7)	|test for rev unimp frame
	beqs	unimp_x			|go handle unimp frame
	cmpib	#BUSY_SIZE-4,1(%a7)	|if size <> $60, fmt error
	bnel	fpsp_fmt_error
	leal	BUSY_SIZE+LOCAL_SIZE(%a7),%a1 |init a1 so fpsp.h
|					;equates will work
| Fix up the new busy frame with entries from the unimp frame
|
	movel	ETEMP_EX(%a6),ETEMP_EX(%a1) |copy etemp from unimp
	movel	ETEMP_HI(%a6),ETEMP_HI(%a1) |frame to busy frame
	movel	ETEMP_LO(%a6),ETEMP_LO(%a1)
	movel	CMDREG1B(%a6),CMDREG1B(%a1) |set inst in frame to unimp
	movel	CMDREG1B(%a6),%d0		|fix cmd1b to make it
	andl	#0x03c30000,%d0		|work for cmd3b
	bfextu	CMDREG1B(%a6){#13:#1},%d1	|extract bit 2
	lsll	#5,%d1
	swap	%d1
	orl	%d1,%d0			|put it in the right place
	bfextu	CMDREG1B(%a6){#10:#3},%d1	|extract bit 3,4,5
	lsll	#2,%d1
	swap	%d1
	orl	%d1,%d0			|put them in the right place
	movel	%d0,CMDREG3B(%a1)		|in the busy frame
|
| Or in the FPSR from the emulation with the USER_FPSR on the stack.
|
	fmovel	%FPSR,%d0
	orl	%d0,USER_FPSR(%a6)
	movel	USER_FPSR(%a6),FPSR_SHADOW(%a1) |set exc bits
	orl	#sx_mask,E_BYTE(%a1)
	bra	do_clean

|
| Frame is an unimp frame possible resulting from an fmove <ea>,fp0
| that caused an exception
|
| a1 is modified to point into the new frame allowing fpsp equates
| to be valid.
|
unimp_x:
	cmpib	#UNIMP_40_SIZE-4,1(%a7)	|test for orig unimp frame
	bnes	test_rev
	leal	UNIMP_40_SIZE+LOCAL_SIZE(%a7),%a1
	bras	unimp_con
test_rev:
	cmpib	#UNIMP_41_SIZE-4,1(%a7)	|test for rev unimp frame
	bnel	fpsp_fmt_error		|if not $28 or $30
	leal	UNIMP_41_SIZE+LOCAL_SIZE(%a7),%a1

unimp_con:
|
| Fix up the new unimp frame with entries from the old unimp frame
|
	movel	CMDREG1B(%a6),CMDREG1B(%a1) |set inst in frame to unimp
|
| Or in the FPSR from the emulation with the USER_FPSR on the stack.
|
	fmovel	%FPSR,%d0
	orl	%d0,USER_FPSR(%a6)
	bra	do_clean

|
| Frame is idle, so check for exceptions reported through
| USER_FPSR and set the unimp frame accordingly.
| A7 must be incremented to the point before the
| idle fsave vector to the unimp vector.
|

do_check:
	addl	#4,%a7			|point A7 back to unimp frame
|
| Or in the FPSR from the emulation with the USER_FPSR on the stack.
|
	fmovel	%FPSR,%d0
	orl	%d0,USER_FPSR(%a6)
|
| On a busy frame, we must clear the nmnexc bits.
|
	cmpib	#BUSY_SIZE-4,1(%a7)	|check frame type
	bnes	check_fr		|if busy, clr nmnexc
	clrw	NMNEXC(%a6)		|clr nmnexc & nmcexc
	btstb	#5,CMDREG1B(%a6)		|test for fmove out
	bnes	frame_com
	movel	USER_FPSR(%a6),FPSR_SHADOW(%a6) |set exc bits
	orl	#sx_mask,E_BYTE(%a6)
	bras	frame_com
check_fr:
	cmpb	#UNIMP_40_SIZE-4,1(%a7)
	beqs	frame_com
	clrw	NMNEXC(%a6)
frame_com:
	moveb	FPCR_ENABLE(%a6),%d0	|get fpcr enable byte
	andb	FPSR_EXCEPT(%a6),%d0	|and in the fpsr exc byte
	bfffo	%d0{#24:#8},%d1		|test for first set bit
	leal	exc_tbl,%a0		|load jmp table address
	subib	#24,%d1			|normalize bit offset to 0-8
	movel	(%a0,%d1.w*4),%a0		|load routine address based
|					;based on first enabled exc
	jmp	(%a0)			|jump to routine
|
| Bsun is not possible in unimp or unsupp
|
bsun_exc:
	bra	do_clean
|
| The typical work to be done to the unimp frame to report an
| exception is to set the E1/E3 byte and clr the U flag.
| commonE1 does this for E1 exceptions, which are snan,
| operr, and dz.  commonE3 does this for E3 exceptions, which
| are inex2 and inex1, and also clears the E1 exception bit
| left over from the unimp exception.
|
commonE1:
	bsetb	#E1,E_BYTE(%a6)		|set E1 flag
	bra	commonE			|go clean and exit

commonE3:
	tstb	UFLG_TMP(%a6)		|test flag for unsup/unimp state
	bnes	unsE3
uniE3:
	bsetb	#E3,E_BYTE(%a6)		|set E3 flag
	bclrb	#E1,E_BYTE(%a6)		|clr E1 from unimp
	bra	commonE

unsE3:
	tstb	RES_FLG(%a6)
	bnes	unsE3_0
unsE3_1:
	bsetb	#E3,E_BYTE(%a6)		|set E3 flag
unsE3_0:
	bclrb	#E1,E_BYTE(%a6)		|clr E1 flag
	movel	CMDREG1B(%a6),%d0
	andl	#0x03c30000,%d0		|work for cmd3b
	bfextu	CMDREG1B(%a6){#13:#1},%d1	|extract bit 2
	lsll	#5,%d1
	swap	%d1
	orl	%d1,%d0			|put it in the right place
	bfextu	CMDREG1B(%a6){#10:#3},%d1	|extract bit 3,4,5
	lsll	#2,%d1
	swap	%d1
	orl	%d1,%d0			|put them in the right place
	movel	%d0,CMDREG3B(%a6)		|in the busy frame

commonE:
	bclrb	#UFLAG,T_BYTE(%a6)	|clr U flag from unimp
	bra	do_clean		|go clean and exit
|
| No bits in the enable byte match existing exceptions.  Check for
| the case of the ovfl exc without the ovfl enabled, but with
| inex2 enabled.
|
no_match:
	btstb	#inex2_bit,FPCR_ENABLE(%a6) |check for ovfl/inex2 case
	beqs	no_exc			|if clear, exit
	btstb	#ovfl_bit,FPSR_EXCEPT(%a6) |now check ovfl
	beqs	no_exc			|if clear, exit
	bras	ovfl_unfl		|go to unfl_ovfl to determine if
|					;it is an unsupp or unimp exc

| No exceptions are to be reported.  If the instruction was
| unimplemented, no FPU restore is necessary.  If it was
| unsupported, we must perform the restore.
no_exc:
	tstb	UFLG_TMP(%a6)	|test flag for unsupp/unimp state
	beqs	uni_no_exc
uns_no_exc:
	tstb	RES_FLG(%a6)	|check if frestore is needed
	bne	do_clean	|if clear, no frestore needed
uni_no_exc:
	moveml	USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx USER_FP0(%a6),%fp0-%fp3
	fmoveml USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	unlk	%a6
	bra	finish_up
|
| Unsupported Data Type Handler:
| Ovfl:
|   An fmoveout that results in an overflow is reported this way.
| Unfl:
|   An fmoveout that results in an underflow is reported this way.
|
| Unimplemented Instruction Handler:
| Ovfl:
|   Only scosh, setox, ssinh, stwotox, and scale can set overflow in
|   this manner.
| Unfl:
|   Stwotox, setox, and scale can set underflow in this manner.
|   Any of the other Library Routines such that f(x)=x in which
|   x is an extended denorm can report an underflow exception.
|   It is the responsibility of the exception-causing exception
|   to make sure that WBTEMP is correct.
|
|   The exceptional operand is in FP_SCR1.
|
ovfl_unfl:
	tstb	UFLG_TMP(%a6)	|test flag for unsupp/unimp state
	beqs	ofuf_con
|
| The caller was from an unsupported data type trap.  Test if the
| caller set CU_ONLY.  If so, the exceptional operand is expected in
| FPTEMP, rather than WBTEMP.
|
	tstb	CU_ONLY(%a6)		|test if inst is cu-only
	beq	unsE3
|	move.w	#$fe,CU_SAVEPC(%a6)
	clrb	CU_SAVEPC(%a6)
	bsetb	#E1,E_BYTE(%a6)		|set E1 exception flag
	movew	ETEMP_EX(%a6),FPTEMP_EX(%a6)
	movel	ETEMP_HI(%a6),FPTEMP_HI(%a6)
	movel	ETEMP_LO(%a6),FPTEMP_LO(%a6)
	bsetb	#fptemp15_bit,DTAG(%a6)	|set fpte15
	bclrb	#UFLAG,T_BYTE(%a6)	|clr U flag from unimp
	bra	do_clean		|go clean and exit

ofuf_con:
	moveb	(%a7),VER_TMP(%a6)	|save version number
	cmpib	#BUSY_SIZE-4,1(%a7)	|check for busy frame
	beqs	busy_fr			|if unimp, grow to busy
	cmpib	#VER_40,(%a7)		|test for orig unimp frame
	bnes	try_41			|if not, test for rev frame
	moveql	#13,%d0			|need to zero 14 lwords
	bras	ofuf_fin
try_41:
	cmpib	#VER_41,(%a7)		|test for rev unimp frame
	bnel	fpsp_fmt_error		|if neither, exit with error
	moveql	#11,%d0			|need to zero 12 lwords

ofuf_fin:
	clrl	(%a7)
loop1:
	clrl	-(%a7)			|clear and dec a7
	dbra	%d0,loop1
	moveb	VER_TMP(%a6),(%a7)
	moveb	#BUSY_SIZE-4,1(%a7)		|write busy fmt word.
busy_fr:
	movel	FP_SCR1(%a6),WBTEMP_EX(%a6)	|write
	movel	FP_SCR1+4(%a6),WBTEMP_HI(%a6)	|exceptional op to
	movel	FP_SCR1+8(%a6),WBTEMP_LO(%a6)	|wbtemp
	bsetb	#E3,E_BYTE(%a6)			|set E3 flag
	bclrb	#E1,E_BYTE(%a6)			|make sure E1 is clear
	bclrb	#UFLAG,T_BYTE(%a6)		|clr U flag
	movel	USER_FPSR(%a6),FPSR_SHADOW(%a6)
	orl	#sx_mask,E_BYTE(%a6)
	movel	CMDREG1B(%a6),%d0		|fix cmd1b to make it
	andl	#0x03c30000,%d0		|work for cmd3b
	bfextu	CMDREG1B(%a6){#13:#1},%d1	|extract bit 2
	lsll	#5,%d1
	swap	%d1
	orl	%d1,%d0			|put it in the right place
	bfextu	CMDREG1B(%a6){#10:#3},%d1	|extract bit 3,4,5
	lsll	#2,%d1
	swap	%d1
	orl	%d1,%d0			|put them in the right place
	movel	%d0,CMDREG3B(%a6)		|in the busy frame

|
| Check if the frame to be restored is busy or unimp.
|** NOTE *** Bug fix for errata (0d43b #3)
| If the frame is unimp, we must create a busy frame to
| fix the bug with the nmnexc bits in cases in which they
| are set by a previous instruction and not cleared by
| the save. The frame will be unimp only if the final
| instruction in an emulation routine caused the exception
| by doing an fmove <ea>,fp0.  The exception operand, in
| internal format, is in fptemp.
|
do_clean:
	cmpib	#UNIMP_40_SIZE-4,1(%a7)
	bnes	do_con
	moveql	#13,%d0			|in orig, need to zero 14 lwords
	bras	do_build
do_con:
	cmpib	#UNIMP_41_SIZE-4,1(%a7)
	bnes	do_restore		|frame must be busy
	moveql	#11,%d0			|in rev, need to zero 12 lwords

do_build:
	moveb	(%a7),VER_TMP(%a6)
	clrl	(%a7)
loop2:
	clrl	-(%a7)			|clear and dec a7
	dbra	%d0,loop2
|
| Use a1 as pointer into new frame.  a6 is not correct if an unimp or
| busy frame was created as the result of an exception on the final
| instruction of an emulation routine.
|
| We need to set the nmcexc bits if the exception is E1. Otherwise,
| the exc taken will be inex2.
|
	leal	BUSY_SIZE+LOCAL_SIZE(%a7),%a1	|init a1 for new frame
	moveb	VER_TMP(%a6),(%a7)	|write busy fmt word
	moveb	#BUSY_SIZE-4,1(%a7)
	movel	FP_SCR1(%a6),WBTEMP_EX(%a1)	|write
	movel	FP_SCR1+4(%a6),WBTEMP_HI(%a1)	|exceptional op to
	movel	FP_SCR1+8(%a6),WBTEMP_LO(%a1)	|wbtemp
|	btst.b	#E1,E_BYTE(%a1)
|	beq.b	do_restore
	bfextu	USER_FPSR(%a6){#17:#4},%d0	|get snan/operr/ovfl/unfl bits
	bfins	%d0,NMCEXC(%a1){#4:#4}	|and insert them in nmcexc
	movel	USER_FPSR(%a6),FPSR_SHADOW(%a1) |set exc bits
	orl	#sx_mask,E_BYTE(%a1)

do_restore:
	moveml	USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx USER_FP0(%a6),%fp0-%fp3
	fmoveml USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore (%a7)+
	tstb	RES_FLG(%a6)	|RES_FLG indicates a "continuation" frame
	beq	cont
	bsr	bug1384
cont:
	unlk	%a6
|
| If trace mode enabled, then go to trace handler.  This handler
| cannot have any fp instructions.  If there are fp inst's and an
| exception has been restored into the machine then the exception
| will occur upon execution of the fp inst.  This is not desirable
| in the kernel (supervisor mode).  See MC68040 manual Section 9.3.8.
|
finish_up:
	btstb	#7,(%a7)		|test T1 in SR
	bnes	g_trace
	btstb	#6,(%a7)		|test T0 in SR
	bnes	g_trace
	bral	fpsp_done
|
| Change integer stack to look like trace stack
| The address of the instruction that caused the
| exception is already in the integer stack (is
| the same as the saved friar)
|
| If the current frame is already a 6-word stack then all
| that needs to be done is to change the vector# to TRACE.
| If the frame is only a 4-word stack (meaning we got here
| on an Unsupported data type exception), then we need to grow
| the stack an extra 2 words and get the FPIAR from the FPU.
|
g_trace:
	bftst	EXC_VEC-4(%sp){#0:#4}
	bne	g_easy

	subw	#4,%sp		| make room
	movel	4(%sp),(%sp)
	movel	8(%sp),4(%sp)
	subw	#BUSY_SIZE,%sp
	fsave	(%sp)
	fmovel	%fpiar,BUSY_SIZE+EXC_EA-4(%sp)
	frestore (%sp)
	addw	#BUSY_SIZE,%sp

g_easy:
	movew	#TRACE_VEC,EXC_VEC-4(%a7)
	bral	real_trace
|
|  This is a work-around for hardware bug 1384.
|
bug1384:
	link	%a5,#0
	fsave	-(%sp)
	cmpib	#0x41,(%sp)	| check for correct frame
	beq	frame_41
	bgt	nofix		| if more advanced mask, do nada

frame_40:
	tstb	1(%sp)		| check to see if idle
	bne	notidle
idle40:
	clrl	(%sp)		| get rid of old fsave frame
        movel  %d1,USER_D1(%a6)  | save d1
	movew	#8,%d1		| place unimp frame instead
loop40:	clrl	-(%sp)
	dbra	%d1,loop40
        movel  USER_D1(%a6),%d1  | restore d1
	movel	#0x40280000,-(%sp)
	frestore (%sp)+
	unlk	%a5
	rts

frame_41:
	tstb	1(%sp)		| check to see if idle
	bne	notidle
idle41:
	clrl	(%sp)		| get rid of old fsave frame
        movel  %d1,USER_D1(%a6)  | save d1
	movew	#10,%d1		| place unimp frame instead
loop41:	clrl	-(%sp)
	dbra	%d1,loop41
        movel  USER_D1(%a6),%d1  | restore d1
	movel	#0x41300000,-(%sp)
	frestore (%sp)+
	unlk	%a5
	rts

notidle:
	bclrb	#etemp15_bit,-40(%a5)
	frestore (%sp)+
	unlk	%a5
	rts

nofix:
	frestore (%sp)+
	unlk	%a5
	rts

	|end
