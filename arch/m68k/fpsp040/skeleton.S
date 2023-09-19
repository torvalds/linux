|
|	skeleton.sa 3.2 4/26/91
|
|	This file contains code that is system dependent and will
|	need to be modified to install the FPSP.
|
|	Each entry point for exception 'xxxx' begins with a 'jmp fpsp_xxxx'.
|	Put any target system specific handling that must be done immediately
|	before the jump instruction.  If there no handling necessary, then
|	the 'fpsp_xxxx' handler entry point should be placed in the exception
|	table so that the 'jmp' can be eliminated. If the FPSP determines that the
|	exception is one that must be reported then there will be a
|	return from the package by a 'jmp real_xxxx'.  At that point
|	the machine state will be identical to the state before
|	the FPSP was entered.  In particular, whatever condition
|	that caused the exception will still be pending when the FPSP
|	package returns.  Thus, there will be system specific code
|	to handle the exception.
|
|	If the exception was completely handled by the package, then
|	the return will be via a 'jmp fpsp_done'.  Unless there is
|	OS specific work to be done (such as handling a context switch or
|	interrupt) the user program can be resumed via 'rte'.
|
|	In the following skeleton code, some typical 'real_xxxx' handling
|	code is shown.  This code may need to be moved to an appropriate
|	place in the target system, or rewritten.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

|
|	Modified for Linux-1.3.x by Jes Sorensen (jds@kom.auc.dk)
|

#include <linux/linkage.h>
#include <asm/entry.h>
#include <asm/asm-offsets.h>

|SKELETON	idnt    2,1 | Motorola 040 Floating Point Software Package

	|section 15
|
|	The following counters are used for standalone testing
|

	|section 8

#include "fpsp.h"

	|xref	b1238_fix

|
|	Divide by Zero exception
|
|	All dz exceptions are 'real', hence no fpsp_dz entry point.
|
	.global	dz
	.global	real_dz
dz:
real_dz:
	link		%a6,#-LOCAL_SIZE
	fsave		-(%sp)
	bclrb		#E1,E_BYTE(%a6)
	frestore	(%sp)+
	unlk		%a6

	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@-		| stack frame pointer argument
	bsrl	trap_c
	addql	#4,%sp
	bral	ret_from_exception

|
|	Inexact exception
|
|	All inexact exceptions are real, but the 'real' handler
|	will probably want to clear the pending exception.
|	The provided code will clear the E3 exception (if pending),
|	otherwise clear the E1 exception.  The frestore is not really
|	necessary for E1 exceptions.
|
| Code following the 'inex' label is to handle bug #1232.  In this
| bug, if an E1 snan, ovfl, or unfl occurred, and the process was
| swapped out before taking the exception, the exception taken on
| return was inex, rather than the correct exception.  The snan, ovfl,
| and unfl exception to be taken must not have been enabled.  The
| fix is to check for E1, and the existence of one of snan, ovfl,
| or unfl bits set in the fpsr.  If any of these are set, branch
| to the appropriate  handler for the exception in the fpsr.  Note
| that this fix is only for d43b parts, and is skipped if the
| version number is not $40.
|
|
	.global	real_inex
	.global	inex
inex:
	link		%a6,#-LOCAL_SIZE
	fsave		-(%sp)
	cmpib		#VER_40,(%sp)		|test version number
	bnes		not_fmt40
	fmovel		%fpsr,-(%sp)
	btstb		#E1,E_BYTE(%a6)		|test for E1 set
	beqs		not_b1232
	btstb		#snan_bit,2(%sp) |test for snan
	beq		inex_ckofl
	addl		#4,%sp
	frestore	(%sp)+
	unlk		%a6
	bra		snan
inex_ckofl:
	btstb		#ovfl_bit,2(%sp) |test for ovfl
	beq		inex_ckufl
	addl		#4,%sp
	frestore	(%sp)+
	unlk		%a6
	bra		ovfl
inex_ckufl:
	btstb		#unfl_bit,2(%sp) |test for unfl
	beq		not_b1232
	addl		#4,%sp
	frestore	(%sp)+
	unlk		%a6
	bra		unfl

|
| We do not have the bug 1232 case.  Clean up the stack and call
| real_inex.
|
not_b1232:
	addl		#4,%sp
	frestore	(%sp)+
	unlk		%a6

real_inex:

	link		%a6,#-LOCAL_SIZE
	fsave		-(%sp)
not_fmt40:
	bclrb		#E3,E_BYTE(%a6)		|clear and test E3 flag
	beqs		inex_cke1
|
| Clear dirty bit on dest resister in the frame before branching
| to b1238_fix.
|
	moveml		%d0/%d1,USER_DA(%a6)
	bfextu		CMDREG1B(%a6){#6:#3},%d0		|get dest reg no
	bclrb		%d0,FPR_DIRTY_BITS(%a6)	|clr dest dirty bit
	bsrl		b1238_fix		|test for bug1238 case
	moveml		USER_DA(%a6),%d0/%d1
	bras		inex_done
inex_cke1:
	bclrb		#E1,E_BYTE(%a6)
inex_done:
	frestore	(%sp)+
	unlk		%a6

	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@-		| stack frame pointer argument
	bsrl	trap_c
	addql	#4,%sp
	bral	ret_from_exception

|
|	Overflow exception
|
	|xref	fpsp_ovfl
	.global	real_ovfl
	.global	ovfl
ovfl:
	jmp	fpsp_ovfl
real_ovfl:

	link		%a6,#-LOCAL_SIZE
	fsave		-(%sp)
	bclrb		#E3,E_BYTE(%a6)		|clear and test E3 flag
	bnes		ovfl_done
	bclrb		#E1,E_BYTE(%a6)
ovfl_done:
	frestore	(%sp)+
	unlk		%a6

	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@-		| stack frame pointer argument
	bsrl	trap_c
	addql	#4,%sp
	bral	ret_from_exception

|
|	Underflow exception
|
	|xref	fpsp_unfl
	.global	real_unfl
	.global	unfl
unfl:
	jmp	fpsp_unfl
real_unfl:

	link		%a6,#-LOCAL_SIZE
	fsave		-(%sp)
	bclrb		#E3,E_BYTE(%a6)		|clear and test E3 flag
	bnes		unfl_done
	bclrb		#E1,E_BYTE(%a6)
unfl_done:
	frestore	(%sp)+
	unlk		%a6

	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@-		| stack frame pointer argument
	bsrl	trap_c
	addql	#4,%sp
	bral	ret_from_exception

|
|	Signalling NAN exception
|
	|xref	fpsp_snan
	.global	real_snan
	.global	snan
snan:
	jmp	fpsp_snan
real_snan:
	link		%a6,#-LOCAL_SIZE
	fsave		-(%sp)
	bclrb		#E1,E_BYTE(%a6)	|snan is always an E1 exception
	frestore	(%sp)+
	unlk		%a6

	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@-		| stack frame pointer argument
	bsrl	trap_c
	addql	#4,%sp
	bral	ret_from_exception

|
|	Operand Error exception
|
	|xref	fpsp_operr
	.global	real_operr
	.global	operr
operr:
	jmp	fpsp_operr
real_operr:
	link		%a6,#-LOCAL_SIZE
	fsave		-(%sp)
	bclrb		#E1,E_BYTE(%a6)	|operr is always an E1 exception
	frestore	(%sp)+
	unlk		%a6

	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@-		| stack frame pointer argument
	bsrl	trap_c
	addql	#4,%sp
	bral	ret_from_exception


|
|	BSUN exception
|
|	This sample handler simply clears the nan bit in the FPSR.
|
	|xref	fpsp_bsun
	.global	real_bsun
	.global	bsun
bsun:
	jmp	fpsp_bsun
real_bsun:
	link		%a6,#-LOCAL_SIZE
	fsave		-(%sp)
	bclrb		#E1,E_BYTE(%a6)	|bsun is always an E1 exception
	fmovel		%FPSR,-(%sp)
	bclrb		#nan_bit,(%sp)
	fmovel		(%sp)+,%FPSR
	frestore	(%sp)+
	unlk		%a6

	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@-		| stack frame pointer argument
	bsrl	trap_c
	addql	#4,%sp
	bral	ret_from_exception

|
|	F-line exception
|
|	A 'real' F-line exception is one that the FPSP isn't supposed to
|	handle. E.g. an instruction with a co-processor ID that is not 1.
|
|
	|xref	fpsp_fline
	.global	real_fline
	.global	fline
fline:
	jmp	fpsp_fline
real_fline:

	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@-		| stack frame pointer argument
	bsrl	trap_c
	addql	#4,%sp
	bral	ret_from_exception

|
|	Unsupported data type exception
|
	|xref	fpsp_unsupp
	.global	real_unsupp
	.global	unsupp
unsupp:
	jmp	fpsp_unsupp
real_unsupp:
	link		%a6,#-LOCAL_SIZE
	fsave		-(%sp)
	bclrb		#E1,E_BYTE(%a6)	|unsupp is always an E1 exception
	frestore	(%sp)+
	unlk		%a6

	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@-		| stack frame pointer argument
	bsrl	trap_c
	addql	#4,%sp
	bral	ret_from_exception

|
|	Trace exception
|
	.global	real_trace
real_trace:
	|
	bral	trap

|
|	fpsp_fmt_error --- exit point for frame format error
|
|	The fpu stack frame does not match the frames existing
|	or planned at the time of this writing.  The fpsp is
|	unable to handle frame sizes not in the following
|	version:size pairs:
|
|	{4060, 4160} - busy frame
|	{4028, 4130} - unimp frame
|	{4000, 4100} - idle frame
|
|	This entry point simply holds an f-line illegal value.
|	Replace this with a call to your kernel panic code or
|	code to handle future revisions of the fpu.
|
	.global	fpsp_fmt_error
fpsp_fmt_error:

	.long	0xf27f0000	|f-line illegal

|
|	fpsp_done --- FPSP exit point
|
|	The exception has been handled by the package and we are ready
|	to return to user mode, but there may be OS specific code
|	to execute before we do.  If there is, do it now.
|
|

	.global	fpsp_done
fpsp_done:
	btst	#0x5,%sp@		| supervisor bit set in saved SR?
	beq	.Lnotkern
	rte
.Lnotkern:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	| deliver signals, reschedule etc..
	jra	ret_from_exception

|
|	mem_write --- write to user or supervisor address space
|
| Writes to memory while in supervisor mode.  copyout accomplishes
| this via a 'moves' instruction.  copyout is a UNIX SVR3 (and later) function.
| If you don't have copyout, use the local copy of the function below.
|
|	a0 - supervisor source address
|	a1 - user destination address
|	d0 - number of bytes to write (maximum count is 12)
|
| The supervisor source address is guaranteed to point into the supervisor
| stack.  The result is that a UNIX
| process is allowed to sleep as a consequence of a page fault during
| copyout.  The probability of a page fault is exceedingly small because
| the 68040 always reads the destination address and thus the page
| faults should have already been handled.
|
| If the EXC_SR shows that the exception was from supervisor space,
| then just do a dumb (and slow) memory move.  In a UNIX environment
| there shouldn't be any supervisor mode floating point exceptions.
|
	.global	mem_write
mem_write:
	btstb	#5,EXC_SR(%a6)	|check for supervisor state
	beqs	user_write
super_write:
	moveb	(%a0)+,(%a1)+
	subql	#1,%d0
	bnes	super_write
	rts
user_write:
	movel	%d1,-(%sp)	|preserve d1 just in case
	movel	%d0,-(%sp)
	movel	%a1,-(%sp)
	movel	%a0,-(%sp)
	jsr		copyout
	addw	#12,%sp
	movel	(%sp)+,%d1
	rts
|
|	mem_read --- read from user or supervisor address space
|
| Reads from memory while in supervisor mode.  copyin accomplishes
| this via a 'moves' instruction.  copyin is a UNIX SVR3 (and later) function.
| If you don't have copyin, use the local copy of the function below.
|
| The FPSP calls mem_read to read the original F-line instruction in order
| to extract the data register number when the 'Dn' addressing mode is
| used.
|
|Input:
|	a0 - user source address
|	a1 - supervisor destination address
|	d0 - number of bytes to read (maximum count is 12)
|
| Like mem_write, mem_read always reads with a supervisor
| destination address on the supervisor stack.  Also like mem_write,
| the EXC_SR is checked and a simple memory copy is done if reading
| from supervisor space is indicated.
|
	.global	mem_read
mem_read:
	btstb	#5,EXC_SR(%a6)	|check for supervisor state
	beqs	user_read
super_read:
	moveb	(%a0)+,(%a1)+
	subql	#1,%d0
	bnes	super_read
	rts
user_read:
	movel	%d1,-(%sp)	|preserve d1 just in case
	movel	%d0,-(%sp)
	movel	%a1,-(%sp)
	movel	%a0,-(%sp)
	jsr	copyin
	addw	#12,%sp
	movel	(%sp)+,%d1
	rts

|
| Use these routines if your kernel doesn't have copyout/copyin equivalents.
| Assumes that D0/D1/A0/A1 are scratch registers. copyout overwrites DFC,
| and copyin overwrites SFC.
|
copyout:
	movel	4(%sp),%a0	| source
	movel	8(%sp),%a1	| destination
	movel	12(%sp),%d0	| count
	subl	#1,%d0		| dec count by 1 for dbra
	movel	#1,%d1

|	DFC is already set
|	movec	%d1,%DFC		| set dfc for user data space
moreout:
	moveb	(%a0)+,%d1	| fetch supervisor byte
out_ea:
	movesb	%d1,(%a1)+	| write user byte
	dbf	%d0,moreout
	rts

copyin:
	movel	4(%sp),%a0	| source
	movel	8(%sp),%a1	| destination
	movel	12(%sp),%d0	| count
	subl	#1,%d0		| dec count by 1 for dbra
	movel	#1,%d1
|	SFC is already set
|	movec	%d1,%SFC		| set sfc for user space
morein:
in_ea:
	movesb	(%a0)+,%d1	| fetch user byte
	moveb	%d1,(%a1)+	| write supervisor byte
	dbf	%d0,morein
	rts

	.section .fixup,"ax"
	.even
1:
	jbra	fpsp040_die

	.section __ex_table,"a"
	.align	4

	.long	in_ea,1b
	.long	out_ea,1b

	|end
