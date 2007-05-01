/* NGmemcpy.S: Niagara optimized memcpy.
 *
 * Copyright (C) 2006 David S. Miller (davem@davemloft.net)
 */

#ifdef __KERNEL__
#include <asm/asi.h>
#include <asm/thread_info.h>
#define GLOBAL_SPARE	%g7
#define RESTORE_ASI(TMP)	\
	ldub	[%g6 + TI_CURRENT_DS], TMP;  \
	wr	TMP, 0x0, %asi;
#else
#define GLOBAL_SPARE	%g5
#define RESTORE_ASI(TMP)	\
	wr	%g0, ASI_PNF, %asi
#endif

#ifndef STORE_ASI
#define STORE_ASI	ASI_BLK_INIT_QUAD_LDD_P
#endif

#ifndef EX_LD
#define EX_LD(x)	x
#endif

#ifndef EX_ST
#define EX_ST(x)	x
#endif

#ifndef EX_RETVAL
#define EX_RETVAL(x)	x
#endif

#ifndef LOAD
#ifndef MEMCPY_DEBUG
#define LOAD(type,addr,dest)	type [addr], dest
#else
#define LOAD(type,addr,dest)	type##a [addr] 0x80, dest
#endif
#endif

#ifndef LOAD_TWIN
#define LOAD_TWIN(addr_reg,dest0,dest1)	\
	ldda [addr_reg] ASI_BLK_INIT_QUAD_LDD_P, dest0
#endif

#ifndef STORE
#define STORE(type,src,addr)	type src, [addr]
#endif

#ifndef STORE_INIT
#define STORE_INIT(src,addr)	stxa src, [addr] %asi
#endif

#ifndef FUNC_NAME
#define FUNC_NAME	NGmemcpy
#endif

#ifndef PREAMBLE
#define PREAMBLE
#endif

#ifndef XCC
#define XCC xcc
#endif

	.register	%g2,#scratch
	.register	%g3,#scratch

	.text
	.align		64

	.globl	FUNC_NAME
	.type	FUNC_NAME,#function
FUNC_NAME:	/* %o0=dst, %o1=src, %o2=len */
	srlx		%o2, 31, %g2
	cmp		%g2, 0
	tne		%xcc, 5
	PREAMBLE
	mov		%o0, GLOBAL_SPARE
	cmp		%o2, 0
	be,pn		%XCC, 85f
	 or		%o0, %o1, %o3
	cmp		%o2, 16
	blu,a,pn	%XCC, 80f
	 or		%o3, %o2, %o3

	/* 2 blocks (128 bytes) is the minimum we can do the block
	 * copy with.  We need to ensure that we'll iterate at least
	 * once in the block copy loop.  At worst we'll need to align
	 * the destination to a 64-byte boundary which can chew up
	 * to (64 - 1) bytes from the length before we perform the
	 * block copy loop.
	 */
	cmp		%o2, (2 * 64)
	blu,pt		%XCC, 70f
	 andcc		%o3, 0x7, %g0

	/* %o0:	dst
	 * %o1:	src
	 * %o2:	len  (known to be >= 128)
	 *
	 * The block copy loops will use %o4/%o5,%g2/%g3 as
	 * temporaries while copying the data.
	 */

	LOAD(prefetch, %o1, #one_read)
	wr		%g0, STORE_ASI, %asi

	/* Align destination on 64-byte boundary.  */
	andcc		%o0, (64 - 1), %o4
	be,pt		%XCC, 2f
	 sub		%o4, 64, %o4
	sub		%g0, %o4, %o4	! bytes to align dst
	sub		%o2, %o4, %o2
1:	subcc		%o4, 1, %o4
	EX_LD(LOAD(ldub, %o1, %g1))
	EX_ST(STORE(stb, %g1, %o0))
	add		%o1, 1, %o1
	bne,pt		%XCC, 1b
	add		%o0, 1, %o0

	/* If the source is on a 16-byte boundary we can do
	 * the direct block copy loop.  If it is 8-byte aligned
	 * we can do the 16-byte loads offset by -8 bytes and the
	 * init stores offset by one register.
	 *
	 * If the source is not even 8-byte aligned, we need to do
	 * shifting and masking (basically integer faligndata).
	 *
	 * The careful bit with init stores is that if we store
	 * to any part of the cache line we have to store the whole
	 * cacheline else we can end up with corrupt L2 cache line
	 * contents.  Since the loop works on 64-bytes of 64-byte
	 * aligned store data at a time, this is easy to ensure.
	 */
2:
	andcc		%o1, (16 - 1), %o4
	andn		%o2, (64 - 1), %g1	! block copy loop iterator
	sub		%o2, %g1, %o2		! final sub-block copy bytes
	be,pt		%XCC, 50f
	 cmp		%o4, 8
	be,a,pt		%XCC, 10f
	 sub		%o1, 0x8, %o1

	/* Neither 8-byte nor 16-byte aligned, shift and mask.  */
	mov		%g1, %o4
	and		%o1, 0x7, %g1
	sll		%g1, 3, %g1
	mov		64, %o3
	andn		%o1, 0x7, %o1
	EX_LD(LOAD(ldx, %o1, %g2))
	sub		%o3, %g1, %o3
	sllx		%g2, %g1, %g2

#define SWIVEL_ONE_DWORD(SRC, TMP1, TMP2, PRE_VAL, PRE_SHIFT, POST_SHIFT, DST)\
	EX_LD(LOAD(ldx, SRC, TMP1)); \
	srlx		TMP1, PRE_SHIFT, TMP2; \
	or		TMP2, PRE_VAL, TMP2; \
	EX_ST(STORE_INIT(TMP2, DST)); \
	sllx		TMP1, POST_SHIFT, PRE_VAL;

1:	add		%o1, 0x8, %o1
	SWIVEL_ONE_DWORD(%o1, %g3, %o5, %g2, %o3, %g1, %o0 + 0x00)
	add		%o1, 0x8, %o1
	SWIVEL_ONE_DWORD(%o1, %g3, %o5, %g2, %o3, %g1, %o0 + 0x08)
	add		%o1, 0x8, %o1
	SWIVEL_ONE_DWORD(%o1, %g3, %o5, %g2, %o3, %g1, %o0 + 0x10)
	add		%o1, 0x8, %o1
	SWIVEL_ONE_DWORD(%o1, %g3, %o5, %g2, %o3, %g1, %o0 + 0x18)
	add		%o1, 32, %o1
	LOAD(prefetch, %o1, #one_read)
	sub		%o1, 32 - 8, %o1
	SWIVEL_ONE_DWORD(%o1, %g3, %o5, %g2, %o3, %g1, %o0 + 0x20)
	add		%o1, 8, %o1
	SWIVEL_ONE_DWORD(%o1, %g3, %o5, %g2, %o3, %g1, %o0 + 0x28)
	add		%o1, 8, %o1
	SWIVEL_ONE_DWORD(%o1, %g3, %o5, %g2, %o3, %g1, %o0 + 0x30)
	add		%o1, 8, %o1
	SWIVEL_ONE_DWORD(%o1, %g3, %o5, %g2, %o3, %g1, %o0 + 0x38)
	subcc		%o4, 64, %o4
	bne,pt		%XCC, 1b
	 add		%o0, 64, %o0

#undef SWIVEL_ONE_DWORD

	srl		%g1, 3, %g1
	ba,pt		%XCC, 60f
	 add		%o1, %g1, %o1

10:	/* Destination is 64-byte aligned, source was only 8-byte
	 * aligned but it has been subtracted by 8 and we perform
	 * one twin load ahead, then add 8 back into source when
	 * we finish the loop.
	 */
	EX_LD(LOAD_TWIN(%o1, %o4, %o5))
1:	add		%o1, 16, %o1
	EX_LD(LOAD_TWIN(%o1, %g2, %g3))
	add		%o1, 16 + 32, %o1
	LOAD(prefetch, %o1, #one_read)
	sub		%o1, 32, %o1
	EX_ST(STORE_INIT(%o5, %o0 + 0x00))	! initializes cache line
	EX_ST(STORE_INIT(%g2, %o0 + 0x08))
	EX_LD(LOAD_TWIN(%o1, %o4, %o5))
	add		%o1, 16, %o1
	EX_ST(STORE_INIT(%g3, %o0 + 0x10))
	EX_ST(STORE_INIT(%o4, %o0 + 0x18))
	EX_LD(LOAD_TWIN(%o1, %g2, %g3))
	add		%o1, 16, %o1
	EX_ST(STORE_INIT(%o5, %o0 + 0x20))
	EX_ST(STORE_INIT(%g2, %o0 + 0x28))
	EX_LD(LOAD_TWIN(%o1, %o4, %o5))
	EX_ST(STORE_INIT(%g3, %o0 + 0x30))
	EX_ST(STORE_INIT(%o4, %o0 + 0x38))
	subcc		%g1, 64, %g1
	bne,pt		%XCC, 1b
	 add		%o0, 64, %o0

	ba,pt		%XCC, 60f
	 add		%o1, 0x8, %o1

50:	/* Destination is 64-byte aligned, and source is 16-byte
	 * aligned.
	 */
1:	EX_LD(LOAD_TWIN(%o1, %o4, %o5))
	add	%o1, 16, %o1
	EX_LD(LOAD_TWIN(%o1, %g2, %g3))
	add	%o1, 16 + 32, %o1
	LOAD(prefetch, %o1, #one_read)
	sub	%o1, 32, %o1
	EX_ST(STORE_INIT(%o4, %o0 + 0x00))	! initializes cache line
	EX_ST(STORE_INIT(%o5, %o0 + 0x08))
	EX_LD(LOAD_TWIN(%o1, %o4, %o5))
	add	%o1, 16, %o1
	EX_ST(STORE_INIT(%g2, %o0 + 0x10))
	EX_ST(STORE_INIT(%g3, %o0 + 0x18))
	EX_LD(LOAD_TWIN(%o1, %g2, %g3))
	add	%o1, 16, %o1
	EX_ST(STORE_INIT(%o4, %o0 + 0x20))
	EX_ST(STORE_INIT(%o5, %o0 + 0x28))
	EX_ST(STORE_INIT(%g2, %o0 + 0x30))
	EX_ST(STORE_INIT(%g3, %o0 + 0x38))
	subcc	%g1, 64, %g1
	bne,pt	%XCC, 1b
	 add	%o0, 64, %o0
	/* fall through */

60:	
	membar		#Sync

	/* %o2 contains any final bytes still needed to be copied
	 * over. If anything is left, we copy it one byte at a time.
	 */
	RESTORE_ASI(%o3)
	brz,pt		%o2, 85f
	 sub		%o0, %o1, %o3
	ba,a,pt		%XCC, 90f

	.align		64
70: /* 16 < len <= 64 */
	bne,pn		%XCC, 75f
	 sub		%o0, %o1, %o3

72:
	andn		%o2, 0xf, %o4
	and		%o2, 0xf, %o2
1:	subcc		%o4, 0x10, %o4
	EX_LD(LOAD(ldx, %o1, %o5))
	add		%o1, 0x08, %o1
	EX_LD(LOAD(ldx, %o1, %g1))
	sub		%o1, 0x08, %o1
	EX_ST(STORE(stx, %o5, %o1 + %o3))
	add		%o1, 0x8, %o1
	EX_ST(STORE(stx, %g1, %o1 + %o3))
	bgu,pt		%XCC, 1b
	 add		%o1, 0x8, %o1
73:	andcc		%o2, 0x8, %g0
	be,pt		%XCC, 1f
	 nop
	sub		%o2, 0x8, %o2
	EX_LD(LOAD(ldx, %o1, %o5))
	EX_ST(STORE(stx, %o5, %o1 + %o3))
	add		%o1, 0x8, %o1
1:	andcc		%o2, 0x4, %g0
	be,pt		%XCC, 1f
	 nop
	sub		%o2, 0x4, %o2
	EX_LD(LOAD(lduw, %o1, %o5))
	EX_ST(STORE(stw, %o5, %o1 + %o3))
	add		%o1, 0x4, %o1
1:	cmp		%o2, 0
	be,pt		%XCC, 85f
	 nop
	ba,pt		%xcc, 90f
	 nop

75:
	andcc		%o0, 0x7, %g1
	sub		%g1, 0x8, %g1
	be,pn		%icc, 2f
	 sub		%g0, %g1, %g1
	sub		%o2, %g1, %o2

1:	subcc		%g1, 1, %g1
	EX_LD(LOAD(ldub, %o1, %o5))
	EX_ST(STORE(stb, %o5, %o1 + %o3))
	bgu,pt		%icc, 1b
	 add		%o1, 1, %o1

2:	add		%o1, %o3, %o0
	andcc		%o1, 0x7, %g1
	bne,pt		%icc, 8f
	 sll		%g1, 3, %g1

	cmp		%o2, 16
	bgeu,pt		%icc, 72b
	 nop
	ba,a,pt		%xcc, 73b

8:	mov		64, %o3
	andn		%o1, 0x7, %o1
	EX_LD(LOAD(ldx, %o1, %g2))
	sub		%o3, %g1, %o3
	andn		%o2, 0x7, %o4
	sllx		%g2, %g1, %g2
1:	add		%o1, 0x8, %o1
	EX_LD(LOAD(ldx, %o1, %g3))
	subcc		%o4, 0x8, %o4
	srlx		%g3, %o3, %o5
	or		%o5, %g2, %o5
	EX_ST(STORE(stx, %o5, %o0))
	add		%o0, 0x8, %o0
	bgu,pt		%icc, 1b
	 sllx		%g3, %g1, %g2

	srl		%g1, 3, %g1
	andcc		%o2, 0x7, %o2
	be,pn		%icc, 85f
	 add		%o1, %g1, %o1
	ba,pt		%xcc, 90f
	 sub		%o0, %o1, %o3

	.align		64
80: /* 0 < len <= 16 */
	andcc		%o3, 0x3, %g0
	bne,pn		%XCC, 90f
	 sub		%o0, %o1, %o3

1:
	subcc		%o2, 4, %o2
	EX_LD(LOAD(lduw, %o1, %g1))
	EX_ST(STORE(stw, %g1, %o1 + %o3))
	bgu,pt		%XCC, 1b
	 add		%o1, 4, %o1

85:	retl
	 mov		EX_RETVAL(GLOBAL_SPARE), %o0

	.align		32
90:
	subcc		%o2, 1, %o2
	EX_LD(LOAD(ldub, %o1, %g1))
	EX_ST(STORE(stb, %g1, %o1 + %o3))
	bgu,pt		%XCC, 90b
	 add		%o1, 1, %o1
	retl
	 mov		EX_RETVAL(GLOBAL_SPARE), %o0

	.size		FUNC_NAME, .-FUNC_NAME
