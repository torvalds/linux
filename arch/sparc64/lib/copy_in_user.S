/* copy_in_user.S: Copy from userspace to userspace.
 *
 * Copyright (C) 1999, 2000, 2004 David S. Miller (davem@redhat.com)
 */

#include <asm/asi.h>

#define XCC xcc

#define EX(x,y)			\
98:	x,y;			\
	.section .fixup;	\
	.align 4;		\
99:	retl;			\
	 mov 1, %o0;		\
	.section __ex_table,"a";\
	.align 4;		\
	.word 98b, 99b;		\
	.text;			\
	.align 4;

	.register	%g2,#scratch
	.register	%g3,#scratch

	.text
	.align	32

	/* Don't try to get too fancy here, just nice and
	 * simple.  This is predominantly used for well aligned
	 * small copies in the compat layer.  It is also used
	 * to copy register windows around during thread cloning.
	 */

	.globl		___copy_in_user
	.type		___copy_in_user,#function
___copy_in_user:	/* %o0=dst, %o1=src, %o2=len */
	/* Writing to %asi is _expensive_ so we hardcode it.
	 * Reading %asi to check for KERNEL_DS is comparatively
	 * cheap.
	 */
	rd		%asi, %g1
	cmp		%g1, ASI_AIUS
	bne,pn		%icc, memcpy_user_stub
	 nop

	cmp		%o2, 0
	be,pn		%XCC, 85f
	 or		%o0, %o1, %o3
	cmp		%o2, 16
	bleu,a,pn	%XCC, 80f
	 or		%o3, %o2, %o3

	/* 16 < len <= 64 */
	andcc		%o3, 0x7, %g0
	bne,pn		%XCC, 90f
	 sub		%o0, %o1, %o3

	andn		%o2, 0x7, %o4
	and		%o2, 0x7, %o2
1:	subcc		%o4, 0x8, %o4
	EX(ldxa [%o1] %asi, %o5)
	EX(stxa %o5, [%o1 + %o3] ASI_AIUS)
	bgu,pt		%XCC, 1b
	 add		%o1, 0x8, %o1
	andcc		%o2, 0x4, %g0
	be,pt		%XCC, 1f
	 nop
	sub		%o2, 0x4, %o2
	EX(lduwa [%o1] %asi, %o5)
	EX(stwa %o5, [%o1 + %o3] ASI_AIUS)
	add		%o1, 0x4, %o1
1:	cmp		%o2, 0
	be,pt		%XCC, 85f
	 nop
	ba,pt		%xcc, 90f
	 nop

80:	/* 0 < len <= 16 */
	andcc		%o3, 0x3, %g0
	bne,pn		%XCC, 90f
	 sub		%o0, %o1, %o3

82:
	subcc		%o2, 4, %o2
	EX(lduwa [%o1] %asi, %g1)
	EX(stwa %g1, [%o1 + %o3] ASI_AIUS)
	bgu,pt		%XCC, 82b
	 add		%o1, 4, %o1

85:	retl
	 clr		%o0

	.align	32
90:
	subcc		%o2, 1, %o2
	EX(lduba [%o1] %asi, %g1)
	EX(stba %g1, [%o1 + %o3] ASI_AIUS)
	bgu,pt		%XCC, 90b
	 add		%o1, 1, %o1
	retl
	 clr		%o0

	.size		___copy_in_user, .-___copy_in_user

	/* Act like copy_{to,in}_user(), ie. return zero instead
	 * of original destination pointer.  This is invoked when
	 * copy_{to,in}_user() finds that %asi is kernel space.
	 */
	.globl		memcpy_user_stub
	.type		memcpy_user_stub,#function
memcpy_user_stub:
	save		%sp, -192, %sp
	mov		%i0, %o0
	mov		%i1, %o1
	call		memcpy
	 mov		%i2, %o2
	ret
	 restore	%g0, %g0, %o0
	.size		memcpy_user_stub, .-memcpy_user_stub
