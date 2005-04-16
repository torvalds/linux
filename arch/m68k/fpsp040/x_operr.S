|
|	x_operr.sa 3.5 7/1/91
|
|	fpsp_operr --- FPSP handler for operand error exception
|
|	See 68040 User's Manual pp. 9-44f
|
| Note 1: For trap disabled 040 does the following:
| If the dest is a fp reg, then an extended precision non_signaling
| NAN is stored in the dest reg.  If the dest format is b, w, or l and
| the source op is a NAN, then garbage is stored as the result (actually
| the upper 32 bits of the mantissa are sent to the integer unit). If
| the dest format is integer (b, w, l) and the operr is caused by
| integer overflow, or the source op is inf, then the result stored is
| garbage.
| There are three cases in which operr is incorrectly signaled on the
| 040.  This occurs for move_out of format b, w, or l for the largest
| negative integer (-2^7 for b, -2^15 for w, -2^31 for l).
|
|	  On opclass = 011 fmove.(b,w,l) that causes a conversion
|	  overflow -> OPERR, the exponent in wbte (and fpte) is:
|		byte    56 - (62 - exp)
|		word    48 - (62 - exp)
|		long    32 - (62 - exp)
|
|			where exp = (true exp) - 1
|
|  So, wbtemp and fptemp will contain the following on erroneously
|	  signalled operr:
|			fpts = 1
|			fpte = $4000  (15 bit externally)
|		byte	fptm = $ffffffff ffffff80
|		word	fptm = $ffffffff ffff8000
|		long	fptm = $ffffffff 80000000
|
| Note 2: For trap enabled 040 does the following:
| If the inst is move_out, then same as Note 1.
| If the inst is not move_out, the dest is not modified.
| The exceptional operand is not defined for integer overflow
| during a move_out.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MOTOROLA
|	The copyright notice above does not evidence any
|	actual or intended publication of such source code.

X_OPERR:	|idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

#include "fpsp.h"

	|xref	mem_write
	|xref	real_operr
	|xref	real_inex
	|xref	get_fline
	|xref	fpsp_done
	|xref	reg_dest

	.global	fpsp_operr
fpsp_operr:
|
	link		%a6,#-LOCAL_SIZE
	fsave		-(%a7)
	moveml		%d0-%d1/%a0-%a1,USER_DA(%a6)
	fmovemx	%fp0-%fp3,USER_FP0(%a6)
	fmoveml	%fpcr/%fpsr/%fpiar,USER_FPCR(%a6)

|
| Check if this is an opclass 3 instruction.
|  If so, fall through, else branch to operr_end
|
	btstb	#TFLAG,T_BYTE(%a6)
	beqs	operr_end

|
| If the destination size is B,W,or L, the operr must be
| handled here.
|
	movel	CMDREG1B(%a6),%d0
	bfextu	%d0{#3:#3},%d0	|0=long, 4=word, 6=byte
	cmpib	#0,%d0		|determine size; check long
	beq	operr_long
	cmpib	#4,%d0		|check word
	beq	operr_word
	cmpib	#6,%d0		|check byte
	beq	operr_byte

|
| The size is not B,W,or L, so the operr is handled by the
| kernel handler.  Set the operr bits and clean up, leaving
| only the integer exception frame on the stack, and the
| fpu in the original exceptional state.
|
operr_end:
	bsetb		#operr_bit,FPSR_EXCEPT(%a6)
	bsetb		#aiop_bit,FPSR_AEXCEPT(%a6)

	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore	(%a7)+
	unlk		%a6
	bral		real_operr

operr_long:
	moveql	#4,%d1		|write size to d1
	moveb	STAG(%a6),%d0	|test stag for nan
	andib	#0xe0,%d0		|clr all but tag
	cmpib	#0x60,%d0		|check for nan
	beq	operr_nan
	cmpil	#0x80000000,FPTEMP_LO(%a6) |test if ls lword is special
	bnes	chklerr		|if not equal, check for incorrect operr
	bsr	check_upper	|check if exp and ms mant are special
	tstl	%d0
	bnes	chklerr		|if d0 is true, check for incorrect operr
	movel	#0x80000000,%d0	|store special case result
	bsr	operr_store
	bra	not_enabled	|clean and exit
|
|	CHECK FOR INCORRECTLY GENERATED OPERR EXCEPTION HERE
|
chklerr:
	movew	FPTEMP_EX(%a6),%d0
	andw	#0x7FFF,%d0	|ignore sign bit
	cmpw	#0x3FFE,%d0	|this is the only possible exponent value
	bnes	chklerr2
fixlong:
	movel	FPTEMP_LO(%a6),%d0
	bsr	operr_store
	bra	not_enabled
chklerr2:
	movew	FPTEMP_EX(%a6),%d0
	andw	#0x7FFF,%d0	|ignore sign bit
	cmpw	#0x4000,%d0
	bcc	store_max	|exponent out of range

	movel	FPTEMP_LO(%a6),%d0
	andl	#0x7FFF0000,%d0	|look for all 1's on bits 30-16
	cmpl	#0x7FFF0000,%d0
	beqs	fixlong

	tstl	FPTEMP_LO(%a6)
	bpls	chklepos
	cmpl	#0xFFFFFFFF,FPTEMP_HI(%a6)
	beqs	fixlong
	bra	store_max
chklepos:
	tstl	FPTEMP_HI(%a6)
	beqs	fixlong
	bra	store_max

operr_word:
	moveql	#2,%d1		|write size to d1
	moveb	STAG(%a6),%d0	|test stag for nan
	andib	#0xe0,%d0		|clr all but tag
	cmpib	#0x60,%d0		|check for nan
	beq	operr_nan
	cmpil	#0xffff8000,FPTEMP_LO(%a6) |test if ls lword is special
	bnes	chkwerr		|if not equal, check for incorrect operr
	bsr	check_upper	|check if exp and ms mant are special
	tstl	%d0
	bnes	chkwerr		|if d0 is true, check for incorrect operr
	movel	#0x80000000,%d0	|store special case result
	bsr	operr_store
	bra	not_enabled	|clean and exit
|
|	CHECK FOR INCORRECTLY GENERATED OPERR EXCEPTION HERE
|
chkwerr:
	movew	FPTEMP_EX(%a6),%d0
	andw	#0x7FFF,%d0	|ignore sign bit
	cmpw	#0x3FFE,%d0	|this is the only possible exponent value
	bnes	store_max
	movel	FPTEMP_LO(%a6),%d0
	swap	%d0
	bsr	operr_store
	bra	not_enabled

operr_byte:
	moveql	#1,%d1		|write size to d1
	moveb	STAG(%a6),%d0	|test stag for nan
	andib	#0xe0,%d0		|clr all but tag
	cmpib	#0x60,%d0		|check for nan
	beqs	operr_nan
	cmpil	#0xffffff80,FPTEMP_LO(%a6) |test if ls lword is special
	bnes	chkberr		|if not equal, check for incorrect operr
	bsr	check_upper	|check if exp and ms mant are special
	tstl	%d0
	bnes	chkberr		|if d0 is true, check for incorrect operr
	movel	#0x80000000,%d0	|store special case result
	bsr	operr_store
	bra	not_enabled	|clean and exit
|
|	CHECK FOR INCORRECTLY GENERATED OPERR EXCEPTION HERE
|
chkberr:
	movew	FPTEMP_EX(%a6),%d0
	andw	#0x7FFF,%d0	|ignore sign bit
	cmpw	#0x3FFE,%d0	|this is the only possible exponent value
	bnes	store_max
	movel	FPTEMP_LO(%a6),%d0
	asll	#8,%d0
	swap	%d0
	bsr	operr_store
	bra	not_enabled

|
| This operr condition is not of the special case.  Set operr
| and aiop and write the portion of the nan to memory for the
| given size.
|
operr_nan:
	orl	#opaop_mask,USER_FPSR(%a6) |set operr & aiop

	movel	ETEMP_HI(%a6),%d0	|output will be from upper 32 bits
	bsr	operr_store
	bra	end_operr
|
| Store_max loads the max pos or negative for the size, sets
| the operr and aiop bits, and clears inex and ainex, incorrectly
| set by the 040.
|
store_max:
	orl	#opaop_mask,USER_FPSR(%a6) |set operr & aiop
	bclrb	#inex2_bit,FPSR_EXCEPT(%a6)
	bclrb	#ainex_bit,FPSR_AEXCEPT(%a6)
	fmovel	#0,%FPSR

	tstw	FPTEMP_EX(%a6)	|check sign
	blts	load_neg
	movel	#0x7fffffff,%d0
	bsr	operr_store
	bra	end_operr
load_neg:
	movel	#0x80000000,%d0
	bsr	operr_store
	bra	end_operr

|
| This routine stores the data in d0, for the given size in d1,
| to memory or data register as required.  A read of the fline
| is required to determine the destination.
|
operr_store:
	movel	%d0,L_SCR1(%a6)	|move write data to L_SCR1
	movel	%d1,-(%a7)	|save register size
	bsrl	get_fline	|fline returned in d0
	movel	(%a7)+,%d1
	bftst	%d0{#26:#3}		|if mode is zero, dest is Dn
	bnes	dest_mem
|
| Destination is Dn.  Get register number from d0. Data is on
| the stack at (a7). D1 has size: 1=byte,2=word,4=long/single
|
	andil	#7,%d0		|isolate register number
	cmpil	#4,%d1
	beqs	op_long		|the most frequent case
	cmpil	#2,%d1
	bnes	op_con
	orl	#8,%d0
	bras	op_con
op_long:
	orl	#0x10,%d0
op_con:
	movel	%d0,%d1		|format size:reg for reg_dest
	bral	reg_dest	|call to reg_dest returns to caller
|				;of operr_store
|
| Destination is memory.  Get <ea> from integer exception frame
| and call mem_write.
|
dest_mem:
	leal	L_SCR1(%a6),%a0	|put ptr to write data in a0
	movel	EXC_EA(%a6),%a1	|put user destination address in a1
	movel	%d1,%d0		|put size in d0
	bsrl	mem_write
	rts
|
| Check the exponent for $c000 and the upper 32 bits of the
| mantissa for $ffffffff.  If both are true, return d0 clr
| and store the lower n bits of the least lword of FPTEMP
| to d0 for write out.  If not, it is a real operr, and set d0.
|
check_upper:
	cmpil	#0xffffffff,FPTEMP_HI(%a6) |check if first byte is all 1's
	bnes	true_operr	|if not all 1's then was true operr
	cmpiw	#0xc000,FPTEMP_EX(%a6) |check if incorrectly signalled
	beqs	not_true_operr	|branch if not true operr
	cmpiw	#0xbfff,FPTEMP_EX(%a6) |check if incorrectly signalled
	beqs	not_true_operr	|branch if not true operr
true_operr:
	movel	#1,%d0		|signal real operr
	rts
not_true_operr:
	clrl	%d0		|signal no real operr
	rts

|
| End_operr tests for operr enabled.  If not, it cleans up the stack
| and does an rte.  If enabled, it cleans up the stack and branches
| to the kernel operr handler with only the integer exception
| frame on the stack and the fpu in the original exceptional state
| with correct data written to the destination.
|
end_operr:
	btstb		#operr_bit,FPCR_ENABLE(%a6)
	beqs		not_enabled
enabled:
	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore	(%a7)+
	unlk		%a6
	bral		real_operr

not_enabled:
|
| It is possible to have either inex2 or inex1 exceptions with the
| operr.  If the inex enable bit is set in the FPCR, and either
| inex2 or inex1 occurred, we must clean up and branch to the
| real inex handler.
|
ck_inex:
	moveb	FPCR_ENABLE(%a6),%d0
	andb	FPSR_EXCEPT(%a6),%d0
	andib	#0x3,%d0
	beq	operr_exit
|
| Inexact enabled and reported, and we must take an inexact exception.
|
take_inex:
	moveb		#INEX_VEC,EXC_VEC+1(%a6)
	movel		USER_FPSR(%a6),FPSR_SHADOW(%a6)
	orl		#sx_mask,E_BYTE(%a6)
	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	frestore	(%a7)+
	unlk		%a6
	bral		real_inex
|
| Since operr is only an E1 exception, there is no need to frestore
| any state back to the fpu.
|
operr_exit:
	moveml		USER_DA(%a6),%d0-%d1/%a0-%a1
	fmovemx	USER_FP0(%a6),%fp0-%fp3
	fmoveml	USER_FPCR(%a6),%fpcr/%fpsr/%fpiar
	unlk		%a6
	bral		fpsp_done

	|end
