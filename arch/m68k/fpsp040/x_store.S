|
|	x_store.sa 3.2 1/24/91
|
|	store --- store operand to memory or register
|
|	Used by underflow and overflow handlers.
|
|	a6 = points to fp value to be stored.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MOTOROLA
|	The copyright notice above does not evidence any
|	actual or intended publication of such source code.

X_STORE:	|idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

fpreg_mask:
	.byte	0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01

#include "fpsp.h"

	|xref	mem_write
	|xref	get_fline
	|xref	g_opcls
	|xref	g_dfmtou
	|xref	reg_dest

	.global	dest_ext
	.global	dest_dbl
	.global	dest_sgl

	.global	store
store:
	btstb	#E3,E_BYTE(%a6)
	beqs	E1_sto
E3_sto:
	movel	CMDREG3B(%a6),%d0
	bfextu	%d0{#6:#3},%d0		|isolate dest. reg from cmdreg3b
sto_fp:
	lea	fpreg_mask,%a1
	moveb	(%a1,%d0.w),%d0		|convert reg# to dynamic register mask
	tstb	LOCAL_SGN(%a0)
	beqs	is_pos
	bsetb	#sign_bit,LOCAL_EX(%a0)
is_pos:
	fmovemx (%a0),%d0		|move to correct register
|
|	if fp0-fp3 is being modified, we must put a copy
|	in the USER_FPn variable on the stack because all exception
|	handlers restore fp0-fp3 from there.
|
	cmpb	#0x80,%d0
	bnes	not_fp0
	fmovemx %fp0-%fp0,USER_FP0(%a6)
	rts
not_fp0:
	cmpb	#0x40,%d0
	bnes	not_fp1
	fmovemx %fp1-%fp1,USER_FP1(%a6)
	rts
not_fp1:
	cmpb	#0x20,%d0
	bnes	not_fp2
	fmovemx %fp2-%fp2,USER_FP2(%a6)
	rts
not_fp2:
	cmpb	#0x10,%d0
	bnes	not_fp3
	fmovemx %fp3-%fp3,USER_FP3(%a6)
	rts
not_fp3:
	rts

E1_sto:
	bsrl	g_opcls		|returns opclass in d0
	cmpib	#3,%d0
	beq	opc011		|branch if opclass 3
	movel	CMDREG1B(%a6),%d0
	bfextu	%d0{#6:#3},%d0	|extract destination register
	bras	sto_fp

opc011:
	bsrl	g_dfmtou	|returns dest format in d0
|				;ext=00, sgl=01, dbl=10
	movel	%a0,%a1		|save source addr in a1
	movel	EXC_EA(%a6),%a0	|get the address
	cmpil	#0,%d0		|if dest format is extended
	beq	dest_ext	|then branch
	cmpil	#1,%d0		|if dest format is single
	beq	dest_sgl	|then branch
|
|	fall through to dest_dbl
|

|
|	dest_dbl --- write double precision value to user space
|
|Input
|	a0 -> destination address
|	a1 -> source in extended precision
|Output
|	a0 -> destroyed
|	a1 -> destroyed
|	d0 -> 0
|
|Changes extended precision to double precision.
| Note: no attempt is made to round the extended value to double.
|	dbl_sign = ext_sign
|	dbl_exp = ext_exp - $3fff(ext bias) + $7ff(dbl bias)
|	get rid of ext integer bit
|	dbl_mant = ext_mant{62:12}
|
|		---------------   ---------------    ---------------
|  extended ->  |s|    exp    |   |1| ms mant   |    | ls mant     |
|		---------------   ---------------    ---------------
|		 95	    64    63 62	      32      31     11	  0
|				     |			     |
|				     |			     |
|				     |			     |
|			             v			     v
|			      ---------------   ---------------
|  double   ->		      |s|exp| mant  |   |  mant       |
|			      ---------------   ---------------
|			      63     51   32   31	       0
|
dest_dbl:
	clrl	%d0		|clear d0
	movew	LOCAL_EX(%a1),%d0	|get exponent
	subw	#0x3fff,%d0	|subtract extended precision bias
	cmpw	#0x4000,%d0	|check if inf
	beqs	inf		|if so, special case
	addw	#0x3ff,%d0	|add double precision bias
	swap	%d0		|d0 now in upper word
	lsll	#4,%d0		|d0 now in proper place for dbl prec exp
	tstb	LOCAL_SGN(%a1)
	beqs	get_mant	|if positive, go process mantissa
	bsetl	#31,%d0		|if negative, put in sign information
|				; before continuing
	bras	get_mant	|go process mantissa
inf:
	movel	#0x7ff00000,%d0	|load dbl inf exponent
	clrl	LOCAL_HI(%a1)	|clear msb
	tstb	LOCAL_SGN(%a1)
	beqs	dbl_inf		|if positive, go ahead and write it
	bsetl	#31,%d0		|if negative put in sign information
dbl_inf:
	movel	%d0,LOCAL_EX(%a1)	|put the new exp back on the stack
	bras	dbl_wrt
get_mant:
	movel	LOCAL_HI(%a1),%d1	|get ms mantissa
	bfextu	%d1{#1:#20},%d1	|get upper 20 bits of ms
	orl	%d1,%d0		|put these bits in ms word of double
	movel	%d0,LOCAL_EX(%a1)	|put the new exp back on the stack
	movel	LOCAL_HI(%a1),%d1	|get ms mantissa
	movel	#21,%d0		|load shift count
	lsll	%d0,%d1		|put lower 11 bits in upper bits
	movel	%d1,LOCAL_HI(%a1)	|build lower lword in memory
	movel	LOCAL_LO(%a1),%d1	|get ls mantissa
	bfextu	%d1{#0:#21},%d0	|get ls 21 bits of double
	orl	%d0,LOCAL_HI(%a1)	|put them in double result
dbl_wrt:
	movel	#0x8,%d0		|byte count for double precision number
	exg	%a0,%a1		|a0=supervisor source, a1=user dest
	bsrl	mem_write	|move the number to the user's memory
	rts
|
|	dest_sgl --- write single precision value to user space
|
|Input
|	a0 -> destination address
|	a1 -> source in extended precision
|
|Output
|	a0 -> destroyed
|	a1 -> destroyed
|	d0 -> 0
|
|Changes extended precision to single precision.
|	sgl_sign = ext_sign
|	sgl_exp = ext_exp - $3fff(ext bias) + $7f(sgl bias)
|	get rid of ext integer bit
|	sgl_mant = ext_mant{62:12}
|
|		---------------   ---------------    ---------------
|  extended ->  |s|    exp    |   |1| ms mant   |    | ls mant     |
|		---------------   ---------------    ---------------
|		 95	    64    63 62	   40 32      31     12	  0
|				     |	   |
|				     |	   |
|				     |	   |
|			             v     v
|			      ---------------
|  single   ->		      |s|exp| mant  |
|			      ---------------
|			      31     22     0
|
dest_sgl:
	clrl	%d0
	movew	LOCAL_EX(%a1),%d0	|get exponent
	subw	#0x3fff,%d0	|subtract extended precision bias
	cmpw	#0x4000,%d0	|check if inf
	beqs	sinf		|if so, special case
	addw	#0x7f,%d0		|add single precision bias
	swap	%d0		|put exp in upper word of d0
	lsll	#7,%d0		|shift it into single exp bits
	tstb	LOCAL_SGN(%a1)
	beqs	get_sman	|if positive, continue
	bsetl	#31,%d0		|if negative, put in sign first
	bras	get_sman	|get mantissa
sinf:
	movel	#0x7f800000,%d0	|load single inf exp to d0
	tstb	LOCAL_SGN(%a1)
	beqs	sgl_wrt		|if positive, continue
	bsetl	#31,%d0		|if negative, put in sign info
	bras	sgl_wrt

get_sman:
	movel	LOCAL_HI(%a1),%d1	|get ms mantissa
	bfextu	%d1{#1:#23},%d1	|get upper 23 bits of ms
	orl	%d1,%d0		|put these bits in ms word of single

sgl_wrt:
	movel	%d0,L_SCR1(%a6)	|put the new exp back on the stack
	movel	#0x4,%d0		|byte count for single precision number
	tstl	%a0		|users destination address
	beqs	sgl_Dn		|destination is a data register
	exg	%a0,%a1		|a0=supervisor source, a1=user dest
	leal	L_SCR1(%a6),%a0	|point a0 to data
	bsrl	mem_write	|move the number to the user's memory
	rts
sgl_Dn:
	bsrl	get_fline	|returns fline word in d0
	andw	#0x7,%d0		|isolate register number
	movel	%d0,%d1		|d1 has size:reg formatted for reg_dest
	orl	#0x10,%d1		|reg_dest wants size added to reg#
	bral	reg_dest	|size is X, rts in reg_dest will
|				;return to caller of dest_sgl

dest_ext:
	tstb	LOCAL_SGN(%a1)	|put back sign into exponent word
	beqs	dstx_cont
	bsetb	#sign_bit,LOCAL_EX(%a1)
dstx_cont:
	clrb	LOCAL_SGN(%a1)	|clear out the sign byte

	movel	#0x0c,%d0		|byte count for extended number
	exg	%a0,%a1		|a0=supervisor source, a1=user dest
	bsrl	mem_write	|move the number to the user's memory
	rts

	|end
