	! Copyright 2005,2006,2007,2009 Imagination Technologies

#include <linux/init.h>
#include <generated/asm-offsets.h>
#undef __exit

	__HEAD
	! Setup the stack and get going into _metag_start_kernel
	.global	__start
	.type   __start,function
__start:
	! D1Ar1 contains pTBI (ISTAT)
	! D0Ar2 contains pTBI
	! D1Ar3 contains __pTBISegs
	! D0Ar4 contains kernel arglist pointer

	MOVT    D0Re0,#HI(___pTBIs)
	ADD     D0Re0,D0Re0,#LO(___pTBIs)
	SETL    [D0Re0],D0Ar2,D1Ar1
	MOVT    D0Re0,#HI(___pTBISegs)
	ADD     D0Re0,D0Re0,#LO(___pTBISegs)
	SETD    [D0Re0],D1Ar3
	MOV	A0FrP,#0
	MOV	D0Re0,#0
	MOV	D1Re0,#0
	MOV	D1Ar3,#0
	MOV	D1Ar1,D0Ar4			!Store kernel boot params
	MOV	D1Ar5,#0
	MOV	D0Ar6,#0
#ifdef CONFIG_METAG_DSP
	MOV	D0.8,#0
#endif
	MOVT    A0StP,#HI(_init_thread_union)
	ADD	A0StP,A0StP,#LO(_init_thread_union)
	ADD     A0StP,A0StP,#THREAD_INFO_SIZE
	MOVT	D1RtP,#HI(_metag_start_kernel)
	CALL	D1RtP,#LO(_metag_start_kernel)
	.size	__start,.-__start

	!! Needed by TBX
	.global	__exit
	.type   __exit,function
__exit:
	XOR     TXENABLE,D0Re0,D0Re0
	.size	__exit,.-__exit
