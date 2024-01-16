|
|	x_fline.sa 3.3 1/10/91
|
|	fpsp_fline --- FPSP handler for fline exception
|
|	First determine if the exception is one of the unimplemented
|	floating point instructions.  If so, let fpsp_unimp handle it.
|	Next, determine if the instruction is an fmovecr with a non-zero
|	<ea> field.  If so, handle here and return.  Otherwise, it
|	must be a real F-line exception.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

X_FLINE:	|idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

#include "fpsp.h"

	|xref	real_fline
	|xref	fpsp_unimp
	|xref	uni_2
	|xref	mem_read
	|xref	fpsp_fmt_error

	.global	fpsp_fline
fpsp_fline:
|
|	check for unimplemented vector first.  Use EXC_VEC-4 because
|	the equate is valid only after a 'link a6' has pushed one more
|	long onto the stack.
|
	cmpw	#UNIMP_VEC,EXC_VEC-4(%a7)
	beql	fpsp_unimp

|
|	fmovecr with non-zero <ea> handling here
|
	subl	#4,%a7		|4 accounts for 2-word difference
|				;between six word frame (unimp) and
|				;four word frame
	link	%a6,#-LOCAL_SIZE
	fsave	-(%a7)
	moveml	%d0-%d1/%a0-%a1,USER_DA(%a6)
	moveal	EXC_PC+4(%a6),%a0	|get address of fline instruction
	leal	L_SCR1(%a6),%a1	|use L_SCR1 as scratch
	movel	#4,%d0
	addl	#4,%a6		|to offset the sub.l #4,a7 above so that
|				;a6 can point correctly to the stack frame
|				;before branching to mem_read
	bsrl	mem_read
	subl	#4,%a6
	movel	L_SCR1(%a6),%d0	|d0 contains the fline and command word
	bfextu	%d0{#4:#3},%d1	|extract coprocessor id
	cmpib	#1,%d1		|check if cpid=1
	bne	not_mvcr	|exit if not
	bfextu	%d0{#16:#6},%d1
	cmpib	#0x17,%d1		|check if it is an FMOVECR encoding
	bne	not_mvcr
|				;if an FMOVECR instruction, fix stack
|				;and go to FPSP_UNIMP
fix_stack:
	cmpib	#VER_40,(%a7)	|test for orig unimp frame
	bnes	ck_rev
	subl	#UNIMP_40_SIZE-4,%a7 |emulate an orig fsave
	moveb	#VER_40,(%a7)
	moveb	#UNIMP_40_SIZE-4,1(%a7)
	clrw	2(%a7)
	bras	fix_con
ck_rev:
	cmpib	#VER_41,(%a7)	|test for rev unimp frame
	bnel	fpsp_fmt_error	|if not $40 or $41, exit with error
	subl	#UNIMP_41_SIZE-4,%a7 |emulate a rev fsave
	moveb	#VER_41,(%a7)
	moveb	#UNIMP_41_SIZE-4,1(%a7)
	clrw	2(%a7)
fix_con:
	movew	EXC_SR+4(%a6),EXC_SR(%a6) |move stacked sr to new position
	movel	EXC_PC+4(%a6),EXC_PC(%a6) |move stacked pc to new position
	fmovel	EXC_PC(%a6),%FPIAR |point FPIAR to fline inst
	movel	#4,%d1
	addl	%d1,EXC_PC(%a6)	|increment stacked pc value to next inst
	movew	#0x202c,EXC_VEC(%a6) |reformat vector to unimp
	clrl	EXC_EA(%a6)	|clear the EXC_EA field
	movew	%d0,CMDREG1B(%a6) |move the lower word into CMDREG1B
	clrl	E_BYTE(%a6)
	bsetb	#UFLAG,T_BYTE(%a6)
	moveml	USER_DA(%a6),%d0-%d1/%a0-%a1 |restore data registers
	bral	uni_2

not_mvcr:
	moveml	USER_DA(%a6),%d0-%d1/%a0-%a1 |restore data registers
	frestore (%a7)+
	unlk	%a6
	addl	#4,%a7
	bral	real_fline

	|end
