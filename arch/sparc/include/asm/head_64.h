#ifndef _SPARC64_HEAD_H
#define _SPARC64_HEAD_H

#include <asm/pstate.h>

	/* wrpr	%g0, val, %gl */
#define SET_GL(val)	\
	.word	0xa1902000 | val

	/* rdpr %gl, %gN */
#define GET_GL_GLOBAL(N)	\
	.word	0x81540000 | (N << 25)

#define KERNBASE	0x400000

#define	PTREGS_OFF	(STACK_BIAS + STACKFRAME_SZ)

#define	RTRAP_PSTATE		(PSTATE_TSO|PSTATE_PEF|PSTATE_PRIV|PSTATE_IE)
#define	RTRAP_PSTATE_IRQOFF	(PSTATE_TSO|PSTATE_PEF|PSTATE_PRIV)
#define RTRAP_PSTATE_AG_IRQOFF	(PSTATE_TSO|PSTATE_PEF|PSTATE_PRIV|PSTATE_AG)

#define __CHEETAH_ID	0x003e0014
#define __JALAPENO_ID	0x003e0016
#define __SERRANO_ID	0x003e0022

#define CHEETAH_MANUF		0x003e
#define CHEETAH_IMPL		0x0014 /* Ultra-III   */
#define CHEETAH_PLUS_IMPL	0x0015 /* Ultra-III+  */
#define JALAPENO_IMPL		0x0016 /* Ultra-IIIi  */
#define JAGUAR_IMPL		0x0018 /* Ultra-IV    */
#define PANTHER_IMPL		0x0019 /* Ultra-IV+   */
#define SERRANO_IMPL		0x0022 /* Ultra-IIIi+ */

#define BRANCH_IF_SUN4V(tmp1,label)		\
	sethi	%hi(is_sun4v), %tmp1;		\
	lduw	[%tmp1 + %lo(is_sun4v)], %tmp1; \
	brnz,pn	%tmp1, label;			\
	 nop

#define BRANCH_IF_CHEETAH_BASE(tmp1,tmp2,label)	\
	rdpr	%ver, %tmp1;			\
	sethi	%hi(__CHEETAH_ID), %tmp2;	\
	srlx	%tmp1, 32, %tmp1;		\
	or	%tmp2, %lo(__CHEETAH_ID), %tmp2;\
	cmp	%tmp1, %tmp2;			\
	be,pn	%icc, label;			\
	 nop;

#define BRANCH_IF_JALAPENO(tmp1,tmp2,label)	\
	rdpr	%ver, %tmp1;			\
	sethi	%hi(__JALAPENO_ID), %tmp2;	\
	srlx	%tmp1, 32, %tmp1;		\
	or	%tmp2, %lo(__JALAPENO_ID), %tmp2;\
	cmp	%tmp1, %tmp2;			\
	be,pn	%icc, label;			\
	 nop;

#define BRANCH_IF_CHEETAH_PLUS_OR_FOLLOWON(tmp1,tmp2,label)	\
	rdpr	%ver, %tmp1;			\
	srlx	%tmp1, (32 + 16), %tmp2;	\
	cmp	%tmp2, CHEETAH_MANUF;		\
	bne,pt	%xcc, 99f;			\
	 sllx	%tmp1, 16, %tmp1;		\
	srlx	%tmp1, (32 + 16), %tmp2;	\
	cmp	%tmp2, CHEETAH_PLUS_IMPL;	\
	bgeu,pt	%xcc, label;			\
99:	 nop;

#define BRANCH_IF_ANY_CHEETAH(tmp1,tmp2,label)	\
	rdpr	%ver, %tmp1;			\
	srlx	%tmp1, (32 + 16), %tmp2;	\
	cmp	%tmp2, CHEETAH_MANUF;		\
	bne,pt	%xcc, 99f;			\
	 sllx	%tmp1, 16, %tmp1;		\
	srlx	%tmp1, (32 + 16), %tmp2;	\
	cmp	%tmp2, CHEETAH_IMPL;		\
	bgeu,pt	%xcc, label;			\
99:	 nop;

#endif /* !(_SPARC64_HEAD_H) */
