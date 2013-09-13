/* Pass a breakpoint through to Codescape */

#include <asm/tbx.h>

	.text
        .global	___TBIUnExpXXX
        .type   ___TBIUnExpXXX,function
___TBIUnExpXXX:
	TSTT	D0Ar2,#TBICTX_CRIT_BIT	! Result of nestable int call?
	BZ	$LTBINormCase		! UnExpXXX at background level
	MOV	D0Re0,TXMASKI		! Read TXMASKI
	XOR	TXMASKI,D1Re0,D1Re0	! Turn off BGNDHALT handling!
	OR	D0Ar2,D0Ar2,D0Re0	! Preserve bits cleared
$LTBINormCase:
	MSETL 	[A0StP],D0Ar6,D0Ar4,D0Ar2	! Save args on stack
	SETL 	[A0StP++],D0Ar2,D1Ar1	! Init area for returned values
	SWITCH 	#0xC20208		! Total stack frame size 8 Dwords
					!            write back size 2 Dwords
	GETL 	D0Re0,D1Re0,[--A0StP]	! Get result
	SUB 	A0StP,A0StP,#(8*3)	! Recover stack frame
	MOV 	PC,D1RtP
        .size   	___TBIUnExpXXX,.-___TBIUnExpXXX
