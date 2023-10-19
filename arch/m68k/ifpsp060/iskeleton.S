|~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
|MOTOROLA MICROPROCESSOR & MEMORY TECHNOLOGY GROUP
|M68000 Hi-Performance Microprocessor Division
|M68060 Software Package
|Production Release P1.00 -- October 10, 1994
|
|M68060 Software Package Copyright © 1993, 1994 Motorola Inc.  All rights reserved.
|
|THE SOFTWARE is provided on an "AS IS" basis and without warranty.
|To the maximum extent permitted by applicable law,
|MOTOROLA DISCLAIMS ALL WARRANTIES WHETHER EXPRESS OR IMPLIED,
|INCLUDING IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
|and any warranty against infringement with regard to the SOFTWARE
|(INCLUDING ANY MODIFIED VERSIONS THEREOF) and any accompanying written materials.
|
|To the maximum extent permitted by applicable law,
|IN NO EVENT SHALL MOTOROLA BE LIABLE FOR ANY DAMAGES WHATSOEVER
|(INCLUDING WITHOUT LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS,
|BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR OTHER PECUNIARY LOSS)
|ARISING OF THE USE OR INABILITY TO USE THE SOFTWARE.
|Motorola assumes no responsibility for the maintenance and support of the SOFTWARE.
|
|You are hereby granted a copyright license to use, modify, and distribute the SOFTWARE
|so long as this entire notice is retained without alteration in any modified and/or
|redistributed versions, and that such modified versions are clearly identified as such.
|No licenses are granted by implication, estoppel or otherwise under any patents
|or trademarks of Motorola, Inc.
|~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
| iskeleton.s
|
| This file contains:
|	(1) example "Call-out"s
|	(2) example package entry code
|	(3) example "Call-out" table
|

#include <linux/linkage.h>
#include <asm/entry.h>
#include <asm/asm-offsets.h>


|################################
| (1) EXAMPLE CALL-OUTS		#
|				#
| _060_isp_done()		#
| _060_real_chk()		#
| _060_real_divbyzero()		#
|				#
| _060_real_cas()		#
| _060_real_cas2()		#
| _060_real_lock_page()		#
| _060_real_unlock_page()	#
|################################

|
| _060_isp_done():
|
| This is and example main exit point for the Unimplemented Integer
| Instruction exception handler. For a normal exit, the
| _isp_unimp() branches to here so that the operating system
| can do any clean-up desired. The stack frame is the
| Unimplemented Integer Instruction stack frame with
| the PC pointing to the instruction following the instruction
| just emulated.
| To simply continue execution at the next instruction, just
| do an "rte".
|
| Linux/68k: If returning to user space, check for needed reselections.

	.global		_060_isp_done
_060_isp_done:
	btst	#0x5,%sp@		| supervisor bit set in saved SR?
	beq	.Lnotkern
	rte
.Lnotkern:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	| deliver signals, reschedule etc..
	jra	ret_from_exception

|
| _060_real_chk():
|
| This is an alternate exit point for the Unimplemented Integer
| Instruction exception handler. If the instruction was a "chk2"
| and the operand was out of bounds, then _isp_unimp() creates
| a CHK exception stack frame from the Unimplemented Integer Instrcution
| stack frame and branches to this routine.
|
| Linux/68k: commented out test for tracing

	.global		_060_real_chk
_060_real_chk:
|	tst.b		(%sp)			| is tracing enabled?
|	bpls		real_chk_end		| no

|
|	    CHK FRAME		   TRACE FRAME
|	*****************	*****************
|	*   Current PC	*	*   Current PC	*
|	*****************	*****************
|	* 0x2 *  0x018	*	* 0x2 *  0x024	*
|	*****************	*****************
|	*     Next	*	*     Next	*
|	*      PC	*	*      PC	*
|	*****************	*****************
|	*      SR	*	*      SR	*
|	*****************	*****************
|
|	move.b		#0x24,0x7(%sp)		| set trace vecno
|	bral		_060_real_trace

real_chk_end:
	bral		trap			| jump to trap handler

|
| _060_real_divbyzero:
|
| This is an alternate exit point for the Unimplemented Integer
| Instruction exception handler isp_unimp(). If the instruction is a 64-bit
| integer divide where the source operand is a zero, then the _isp_unimp()
| creates a Divide-by-zero exception stack frame from the Unimplemented
| Integer Instruction stack frame and branches to this routine.
|
| Remember that a trace exception may be pending. The code below performs
| no action associated with the "chk" exception. If tracing is enabled,
| then it create a Trace exception stack frame from the "chk" exception
| stack frame and branches to the _real_trace() entry point.
|
| Linux/68k: commented out test for tracing

	.global		_060_real_divbyzero
_060_real_divbyzero:
|	tst.b		(%sp)			| is tracing enabled?
|	bpls		real_divbyzero_end	| no

|
|	 DIVBYZERO FRAME	   TRACE FRAME
|	*****************	*****************
|	*   Current PC	*	*   Current PC	*
|	*****************	*****************
|	* 0x2 *  0x014	*	* 0x2 *  0x024	*
|	*****************	*****************
|	*     Next	*	*     Next	*
|	*      PC	*	*      PC	*
|	*****************	*****************
|	*      SR	*	*      SR	*
|	*****************	*****************
|
|	move.b		#0x24,0x7(%sp)		| set trace vecno
|	bral		_060_real_trace

real_divbyzero_end:
	bral		trap			| jump to trap handler

|##########################

|
| _060_real_cas():
|
| Entry point for the selected cas emulation code implementation.
| If the implementation provided by the 68060ISP is sufficient,
| then this routine simply re-enters the package through _isp_cas.
|
	.global		_060_real_cas
_060_real_cas:
	bral		_I_CALL_TOP+0x80+0x08

|
| _060_real_cas2():
|
| Entry point for the selected cas2 emulation code implementation.
| If the implementation provided by the 68060ISP is sufficient,
| then this routine simply re-enters the package through _isp_cas2.
|
	.global		_060_real_cas2
_060_real_cas2:
	bral		_I_CALL_TOP+0x80+0x10

|
| _060_lock_page():
|
| Entry point for the operating system`s routine to "lock" a page
| from being paged out. This routine is needed by the cas/cas2
| algorithms so that no page faults occur within the "core" code
| region. Note: the routine must lock two pages if the operand
| spans two pages.
| NOTE: THE ROUTINE SHOULD RETURN AN FSLW VALUE IN D0 ON FAILURE
| SO THAT THE 060SP CAN CREATE A PROPER ACCESS ERROR FRAME.
| Arguments:
|	a0 = operand address
|	d0 = `xxxxxxff -> supervisor; `xxxxxx00 -> user
|	d1 = `xxxxxxff -> longword; `xxxxxx00 -> word
| Expected outputs:
|	d0 = 0 -> success; non-zero -> failure
|
| Linux/m68k: Make sure the page is properly paged in, so we use
| plpaw and handle any exception here. The kernel must not be
| preempted until _060_unlock_page(), so that the page stays mapped.
|
	.global		_060_real_lock_page
_060_real_lock_page:
	move.l	%d2,-(%sp)
	| load sfc/dfc
	tst.b	%d0
	jne	1f
	moveq	#1,%d0
	jra	2f
1:	moveq	#5,%d0
2:	movec.l	%dfc,%d2
	movec.l	%d0,%dfc
	movec.l	%d0,%sfc

	clr.l	%d0
	| prefetch address
	.chip	68060
	move.l	%a0,%a1
1:	plpaw	(%a1)
	addq.w	#1,%a0
	tst.b	%d1
	jeq	2f
	addq.w	#2,%a0
2:	plpaw	(%a0)
3:	.chip	68k

	| restore sfc/dfc
	movec.l	%d2,%dfc
	movec.l	%d2,%sfc
	move.l	(%sp)+,%d2
	rts

.section __ex_table,"a"
	.align	4
	.long	1b,11f
	.long	2b,21f
.previous
.section .fixup,"ax"
	.even
11:	move.l	#0x020003c0,%d0
	or.l	%d2,%d0
	swap	%d0
	jra	3b
21:	move.l	#0x02000bc0,%d0
	or.l	%d2,%d0
	swap	%d0
	jra	3b
.previous

|
| _060_unlock_page():
|
| Entry point for the operating system`s routine to "unlock" a
| page that has been "locked" previously with _real_lock_page.
| Note: the routine must unlock two pages if the operand spans
| two pages.
| Arguments:
|	a0 = operand address
|	d0 = `xxxxxxff -> supervisor; `xxxxxx00 -> user
|	d1 = `xxxxxxff -> longword; `xxxxxx00 -> word
|
| Linux/m68k: perhaps reenable preemption here...

	.global		_060_real_unlock_page
_060_real_unlock_page:
	clr.l		%d0
	rts

|###########################################################################

|#################################
| (2) EXAMPLE PACKAGE ENTRY CODE #
|#################################

	.global		_060_isp_unimp
_060_isp_unimp:
	bral		_I_CALL_TOP+0x80+0x00

	.global		_060_isp_cas
_060_isp_cas:
	bral		_I_CALL_TOP+0x80+0x08

	.global		_060_isp_cas2
_060_isp_cas2:
	bral		_I_CALL_TOP+0x80+0x10

	.global		_060_isp_cas_finish
_060_isp_cas_finish:
	bra.l		_I_CALL_TOP+0x80+0x18

	.global		_060_isp_cas2_finish
_060_isp_cas2_finish:
	bral		_I_CALL_TOP+0x80+0x20

	.global		_060_isp_cas_inrange
_060_isp_cas_inrange:
	bral		_I_CALL_TOP+0x80+0x28

	.global		_060_isp_cas_terminate
_060_isp_cas_terminate:
	bral		_I_CALL_TOP+0x80+0x30

	.global		_060_isp_cas_restart
_060_isp_cas_restart:
	bral		_I_CALL_TOP+0x80+0x38

|###########################################################################

|###############################
| (3) EXAMPLE CALL-OUT SECTION #
|###############################

| The size of this section MUST be 128 bytes!!!

_I_CALL_TOP:
	.long	_060_real_chk		- _I_CALL_TOP
	.long	_060_real_divbyzero	- _I_CALL_TOP
	.long	_060_real_trace		- _I_CALL_TOP
	.long	_060_real_access	- _I_CALL_TOP
	.long	_060_isp_done		- _I_CALL_TOP

	.long	_060_real_cas		- _I_CALL_TOP
	.long	_060_real_cas2		- _I_CALL_TOP
	.long	_060_real_lock_page	- _I_CALL_TOP
	.long	_060_real_unlock_page	- _I_CALL_TOP

	.long	0x00000000, 0x00000000, 0x00000000, 0x00000000
	.long	0x00000000, 0x00000000, 0x00000000

	.long	_060_imem_read		- _I_CALL_TOP
	.long	_060_dmem_read		- _I_CALL_TOP
	.long	_060_dmem_write		- _I_CALL_TOP
	.long	_060_imem_read_word	- _I_CALL_TOP
	.long	_060_imem_read_long	- _I_CALL_TOP
	.long	_060_dmem_read_byte	- _I_CALL_TOP
	.long	_060_dmem_read_word	- _I_CALL_TOP
	.long	_060_dmem_read_long	- _I_CALL_TOP
	.long	_060_dmem_write_byte	- _I_CALL_TOP
	.long	_060_dmem_write_word	- _I_CALL_TOP
	.long	_060_dmem_write_long	- _I_CALL_TOP

	.long	0x00000000
	.long	0x00000000, 0x00000000, 0x00000000, 0x00000000

|###########################################################################

| 060 INTEGER KERNEL PACKAGE MUST GO HERE!!!
#include "isp.sa"
