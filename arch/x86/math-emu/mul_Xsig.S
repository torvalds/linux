/*---------------------------------------------------------------------------+
 |  mul_Xsig.S                                                               |
 |                                                                           |
 | Multiply a 12 byte fixed point number by another fixed point number.      |
 |                                                                           |
 | Copyright (C) 1992,1994,1995                                              |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail billm@jacobi.maths.monash.edu.au |
 |                                                                           |
 | Call from C as:                                                           |
 |   void mul32_Xsig(Xsig *x, unsigned b)                                    |
 |                                                                           |
 |   void mul64_Xsig(Xsig *x, unsigned long long *b)                         |
 |                                                                           |
 |   void mul_Xsig_Xsig(Xsig *x, unsigned *b)                                |
 |                                                                           |
 | The result is neither rounded nor normalized, and the ls bit or so may    |
 | be wrong.                                                                 |
 |                                                                           |
 +---------------------------------------------------------------------------*/
	.file	"mul_Xsig.S"


#include "fpu_emu.h"

.text
ENTRY(mul32_Xsig)
	pushl %ebp
	movl %esp,%ebp
	subl $16,%esp
	pushl %esi

	movl PARAM1,%esi
	movl PARAM2,%ecx

	xor %eax,%eax
	movl %eax,-4(%ebp)
	movl %eax,-8(%ebp)

	movl (%esi),%eax        /* lsl of Xsig */
	mull %ecx		/* msl of b */
	movl %edx,-12(%ebp)

	movl 4(%esi),%eax	/* midl of Xsig */
	mull %ecx		/* msl of b */
	addl %eax,-12(%ebp)
	adcl %edx,-8(%ebp)
	adcl $0,-4(%ebp)

	movl 8(%esi),%eax	/* msl of Xsig */
	mull %ecx		/* msl of b */
	addl %eax,-8(%ebp)
	adcl %edx,-4(%ebp)

	movl -12(%ebp),%eax
	movl %eax,(%esi)
	movl -8(%ebp),%eax
	movl %eax,4(%esi)
	movl -4(%ebp),%eax
	movl %eax,8(%esi)

	popl %esi
	leave
	ret
ENDPROC(mul32_Xsig)


ENTRY(mul64_Xsig)
	pushl %ebp
	movl %esp,%ebp
	subl $16,%esp
	pushl %esi

	movl PARAM1,%esi
	movl PARAM2,%ecx

	xor %eax,%eax
	movl %eax,-4(%ebp)
	movl %eax,-8(%ebp)

	movl (%esi),%eax        /* lsl of Xsig */
	mull 4(%ecx)		/* msl of b */
	movl %edx,-12(%ebp)

	movl 4(%esi),%eax	/* midl of Xsig */
	mull (%ecx)		/* lsl of b */
	addl %edx,-12(%ebp)
	adcl $0,-8(%ebp)
	adcl $0,-4(%ebp)

	movl 4(%esi),%eax	/* midl of Xsig */
	mull 4(%ecx)		/* msl of b */
	addl %eax,-12(%ebp)
	adcl %edx,-8(%ebp)
	adcl $0,-4(%ebp)

	movl 8(%esi),%eax	/* msl of Xsig */
	mull (%ecx)		/* lsl of b */
	addl %eax,-12(%ebp)
	adcl %edx,-8(%ebp)
	adcl $0,-4(%ebp)

	movl 8(%esi),%eax	/* msl of Xsig */
	mull 4(%ecx)		/* msl of b */
	addl %eax,-8(%ebp)
	adcl %edx,-4(%ebp)

	movl -12(%ebp),%eax
	movl %eax,(%esi)
	movl -8(%ebp),%eax
	movl %eax,4(%esi)
	movl -4(%ebp),%eax
	movl %eax,8(%esi)

	popl %esi
	leave
	ret
ENDPROC(mul64_Xsig)



ENTRY(mul_Xsig_Xsig)
	pushl %ebp
	movl %esp,%ebp
	subl $16,%esp
	pushl %esi

	movl PARAM1,%esi
	movl PARAM2,%ecx

	xor %eax,%eax
	movl %eax,-4(%ebp)
	movl %eax,-8(%ebp)

	movl (%esi),%eax        /* lsl of Xsig */
	mull 8(%ecx)		/* msl of b */
	movl %edx,-12(%ebp)

	movl 4(%esi),%eax	/* midl of Xsig */
	mull 4(%ecx)		/* midl of b */
	addl %edx,-12(%ebp)
	adcl $0,-8(%ebp)
	adcl $0,-4(%ebp)

	movl 8(%esi),%eax	/* msl of Xsig */
	mull (%ecx)		/* lsl of b */
	addl %edx,-12(%ebp)
	adcl $0,-8(%ebp)
	adcl $0,-4(%ebp)

	movl 4(%esi),%eax	/* midl of Xsig */
	mull 8(%ecx)		/* msl of b */
	addl %eax,-12(%ebp)
	adcl %edx,-8(%ebp)
	adcl $0,-4(%ebp)

	movl 8(%esi),%eax	/* msl of Xsig */
	mull 4(%ecx)		/* midl of b */
	addl %eax,-12(%ebp)
	adcl %edx,-8(%ebp)
	adcl $0,-4(%ebp)

	movl 8(%esi),%eax	/* msl of Xsig */
	mull 8(%ecx)		/* msl of b */
	addl %eax,-8(%ebp)
	adcl %edx,-4(%ebp)

	movl -12(%ebp),%edx
	movl %edx,(%esi)
	movl -8(%ebp),%edx
	movl %edx,4(%esi)
	movl -4(%ebp),%edx
	movl %edx,8(%esi)

	popl %esi
	leave
	ret
ENDPROC(mul_Xsig_Xsig)
