	.file	"wm_shrx.S"
/*---------------------------------------------------------------------------+
 |  wm_shrx.S                                                                |
 |                                                                           |
 | 64 bit right shift functions                                              |
 |                                                                           |
 | Copyright (C) 1992,1995                                                   |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail billm@jacobi.maths.monash.edu.au |
 |                                                                           |
 | Call from C as:                                                           |
 |   unsigned FPU_shrx(void *arg1, unsigned arg2)                            |
 | and                                                                       |
 |   unsigned FPU_shrxs(void *arg1, unsigned arg2)                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "fpu_emu.h"

.text
/*---------------------------------------------------------------------------+
 |   unsigned FPU_shrx(void *arg1, unsigned arg2)                            |
 |                                                                           |
 |   Extended shift right function.                                          |
 |   Fastest for small shifts.                                               |
 |   Shifts the 64 bit quantity pointed to by the first arg (arg1)           |
 |   right by the number of bits specified by the second arg (arg2).         |
 |   Forms a 96 bit quantity from the 64 bit arg and eax:                    |
 |                [  64 bit arg ][ eax ]                                     |
 |            shift right  --------->                                        |
 |   The eax register is initialized to 0 before the shifting.               |
 |   Results returned in the 64 bit arg and eax.                             |
 +---------------------------------------------------------------------------*/

ENTRY(FPU_shrx)
	push	%ebp
	movl	%esp,%ebp
	pushl	%esi
	movl	PARAM2,%ecx
	movl	PARAM1,%esi
	cmpl	$32,%ecx	/* shrd only works for 0..31 bits */
	jnc	L_more_than_31

/* less than 32 bits */
	pushl	%ebx
	movl	(%esi),%ebx	/* lsl */
	movl	4(%esi),%edx	/* msl */
	xorl	%eax,%eax	/* extension */
	shrd	%cl,%ebx,%eax
	shrd	%cl,%edx,%ebx
	shr	%cl,%edx
	movl	%ebx,(%esi)
	movl	%edx,4(%esi)
	popl	%ebx
	popl	%esi
	leave
	ret

L_more_than_31:
	cmpl	$64,%ecx
	jnc	L_more_than_63

	subb	$32,%cl
	movl	(%esi),%eax	/* lsl */
	movl	4(%esi),%edx	/* msl */
	shrd	%cl,%edx,%eax
	shr	%cl,%edx
	movl	%edx,(%esi)
	movl	$0,4(%esi)
	popl	%esi
	leave
	ret

L_more_than_63:
	cmpl	$96,%ecx
	jnc	L_more_than_95

	subb	$64,%cl
	movl	4(%esi),%eax	/* msl */
	shr	%cl,%eax
	xorl	%edx,%edx
	movl	%edx,(%esi)
	movl	%edx,4(%esi)
	popl	%esi
	leave
	ret

L_more_than_95:
	xorl	%eax,%eax
	movl	%eax,(%esi)
	movl	%eax,4(%esi)
	popl	%esi
	leave
	ret


/*---------------------------------------------------------------------------+
 |   unsigned FPU_shrxs(void *arg1, unsigned arg2)                           |
 |                                                                           |
 |   Extended shift right function (optimized for small floating point       |
 |   integers).                                                              |
 |   Shifts the 64 bit quantity pointed to by the first arg (arg1)           |
 |   right by the number of bits specified by the second arg (arg2).         |
 |   Forms a 96 bit quantity from the 64 bit arg and eax:                    |
 |                [  64 bit arg ][ eax ]                                     |
 |            shift right  --------->                                        |
 |   The eax register is initialized to 0 before the shifting.               |
 |   The lower 8 bits of eax are lost and replaced by a flag which is        |
 |   set (to 0x01) if any bit, apart from the first one, is set in the       |
 |   part which has been shifted out of the arg.                             |
 |   Results returned in the 64 bit arg and eax.                             |
 +---------------------------------------------------------------------------*/
ENTRY(FPU_shrxs)
	push	%ebp
	movl	%esp,%ebp
	pushl	%esi
	pushl	%ebx
	movl	PARAM2,%ecx
	movl	PARAM1,%esi
	cmpl	$64,%ecx	/* shrd only works for 0..31 bits */
	jnc	Ls_more_than_63

	cmpl	$32,%ecx	/* shrd only works for 0..31 bits */
	jc	Ls_less_than_32

/* We got here without jumps by assuming that the most common requirement
   is for small integers */
/* Shift by [32..63] bits */
	subb	$32,%cl
	movl	(%esi),%eax	/* lsl */
	movl	4(%esi),%edx	/* msl */
	xorl	%ebx,%ebx
	shrd	%cl,%eax,%ebx
	shrd	%cl,%edx,%eax
	shr	%cl,%edx
	orl	%ebx,%ebx		/* test these 32 bits */
	setne	%bl
	test	$0x7fffffff,%eax	/* and 31 bits here */
	setne	%bh
	orw	%bx,%bx			/* Any of the 63 bit set ? */
	setne	%al
	movl	%edx,(%esi)
	movl	$0,4(%esi)
	popl	%ebx
	popl	%esi
	leave
	ret

/* Shift by [0..31] bits */
Ls_less_than_32:
	movl	(%esi),%ebx	/* lsl */
	movl	4(%esi),%edx	/* msl */
	xorl	%eax,%eax	/* extension */
	shrd	%cl,%ebx,%eax
	shrd	%cl,%edx,%ebx
	shr	%cl,%edx
	test	$0x7fffffff,%eax	/* only need to look at eax here */
	setne	%al
	movl	%ebx,(%esi)
	movl	%edx,4(%esi)
	popl	%ebx
	popl	%esi
	leave
	ret

/* Shift by [64..95] bits */
Ls_more_than_63:
	cmpl	$96,%ecx
	jnc	Ls_more_than_95

	subb	$64,%cl
	movl	(%esi),%ebx	/* lsl */
	movl	4(%esi),%eax	/* msl */
	xorl	%edx,%edx	/* extension */
	shrd	%cl,%ebx,%edx
	shrd	%cl,%eax,%ebx
	shr	%cl,%eax
	orl	%ebx,%edx
	setne	%bl
	test	$0x7fffffff,%eax	/* only need to look at eax here */
	setne	%bh
	orw	%bx,%bx
	setne	%al
	xorl	%edx,%edx
	movl	%edx,(%esi)	/* set to zero */
	movl	%edx,4(%esi)	/* set to zero */
	popl	%ebx
	popl	%esi
	leave
	ret

Ls_more_than_95:
/* Shift by [96..inf) bits */
	xorl	%eax,%eax
	movl	(%esi),%ebx
	orl	4(%esi),%ebx
	setne	%al
	xorl	%ebx,%ebx
	movl	%ebx,(%esi)
	movl	%ebx,4(%esi)
	popl	%ebx
	popl	%esi
	leave
	ret
