// SPDX-License-Identifier: GPL-2.0
/*
 *    PA-RISC assembly string functions
 *
 *    Copyright (C) 2019 Helge Deller <deller@gmx.de>
 */

#include <asm/assembly.h>
#include <linux/linkage.h>

	.section .text.hot
	.level PA_ASM_LEVEL

	t0 = r20
	t1 = r21
	t2 = r22

ENTRY_CFI(strlen, frame=0,no_calls)
	or,COND(<>) arg0,r0,ret0
	b,l,n	.Lstrlen_null_ptr,r0
	depwi	0,31,2,ret0
	cmpb,COND(<>) arg0,ret0,.Lstrlen_not_aligned
	ldw,ma	4(ret0),t0
	cmpib,tr 0,r0,.Lstrlen_loop
	uxor,nbz r0,t0,r0
.Lstrlen_not_aligned:
	uaddcm	arg0,ret0,t1
	shladd	t1,3,r0,t1
	mtsar	t1
	depwi	-1,%sar,32,t0
	uxor,nbz r0,t0,r0
.Lstrlen_loop:
	b,l,n	.Lstrlen_end_loop,r0
	ldw,ma	4(ret0),t0
	cmpib,tr 0,r0,.Lstrlen_loop
	uxor,nbz r0,t0,r0
.Lstrlen_end_loop:
	extrw,u,<> t0,7,8,r0
	addib,tr,n -3,ret0,.Lstrlen_out
	extrw,u,<> t0,15,8,r0
	addib,tr,n -2,ret0,.Lstrlen_out
	extrw,u,<> t0,23,8,r0
	addi	-1,ret0,ret0
.Lstrlen_out:
	bv r0(rp)
	uaddcm ret0,arg0,ret0
.Lstrlen_null_ptr:
	bv,n r0(rp)
ENDPROC_CFI(strlen)


ENTRY_CFI(strcpy, frame=0,no_calls)
	ldb	0(arg1),t0
	stb	t0,0(arg0)
	ldo	0(arg0),ret0
	ldo	1(arg1),t1
	cmpb,=	r0,t0,2f
	ldo	1(arg0),t2
1:	ldb	0(t1),arg1
	stb	arg1,0(t2)
	ldo	1(t1),t1
	cmpb,<> r0,arg1,1b
	ldo	1(t2),t2
2:	bv,n	r0(rp)
ENDPROC_CFI(strcpy)


ENTRY_CFI(strncpy, frame=0,no_calls)
	ldb	0(arg1),t0
	stb	t0,0(arg0)
	ldo	1(arg1),t1
	ldo	0(arg0),ret0
	cmpb,=	r0,t0,2f
	ldo	1(arg0),arg1
1:	ldo	-1(arg2),arg2
	cmpb,COND(=),n r0,arg2,2f
	ldb	0(t1),arg0
	stb	arg0,0(arg1)
	ldo	1(t1),t1
	cmpb,<> r0,arg0,1b
	ldo	1(arg1),arg1
2:	bv,n	r0(rp)
ENDPROC_CFI(strncpy)


ENTRY_CFI(strcat, frame=0,no_calls)
	ldb	0(arg0),t0
	cmpb,=	t0,r0,2f
	ldo	0(arg0),ret0
	ldo	1(arg0),arg0
1:	ldb	0(arg0),t1
	cmpb,<>,n r0,t1,1b
	ldo	1(arg0),arg0
2:	ldb	0(arg1),t2
	stb	t2,0(arg0)
	ldo	1(arg0),arg0
	ldb	0(arg1),t0
	cmpb,<>	r0,t0,2b
	ldo	1(arg1),arg1
	bv,n	r0(rp)
ENDPROC_CFI(strcat)


ENTRY_CFI(memset, frame=0,no_calls)
	copy	arg0,ret0
	cmpb,COND(=) r0,arg0,4f
	copy	arg0,t2
	cmpb,COND(=) r0,arg2,4f
	ldo	-1(arg2),arg3
	subi	-1,arg3,t0
	subi	0,t0,t1
	cmpiclr,COND(>=) 0,t1,arg2
	ldo	-1(t1),arg2
	extru arg2,31,2,arg0
2:	stb	arg1,0(t2)
	ldo	1(t2),t2
	addib,>= -1,arg0,2b
	ldo	-1(arg3),arg3
	cmpiclr,COND(<=) 4,arg2,r0
	b,l,n	4f,r0
#ifdef CONFIG_64BIT
	depd,*	r0,63,2,arg2
#else
	depw	r0,31,2,arg2
#endif
	ldo	1(t2),t2
3:	stb	arg1,-1(t2)
	stb	arg1,0(t2)
	stb	arg1,1(t2)
	stb	arg1,2(t2)
	addib,COND(>) -4,arg2,3b
	ldo	4(t2),t2
4:	bv,n	r0(rp)
ENDPROC_CFI(memset)

	.end
