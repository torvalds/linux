/* memset.S */

#include <asm/linkage.h>

#if defined(__H8300H__) 
	.h8300h
#endif
#if defined(__H8300S__) 
	.h8300s
#endif
	.text

.global	_memset

;;void *memset(*ptr, int c, size_t count)
;; ptr = er0
;; c   = er1(r1l)
;; count = er2
_memset:
	btst	#0,r0l
	beq	2f

	;; odd address
1:
	mov.b	r1l,@er0
	adds	#1,er0
	dec.l	#1,er2
	beq	6f

	;; even address
2:
	mov.l	er2,er3
	cmp.l	#4,er2
	blo	4f
	;; count>=4 -> count/4
#if defined(__H8300H__)
	shlr.l	er2
	shlr.l	er2
#endif
#if defined(__H8300S__)
	shlr.l	#2,er2
#endif
	;; byte -> long
	mov.b	r1l,r1h
	mov.w	r1,e1
3:
	mov.l	er1,@er0
	adds	#4,er0
	dec.l	#1,er2
	bne	3b
4:
	;; count % 4
	and.b	#3,r3l
	beq	6f
5:
	mov.b	r1l,@er0
	adds	#1,er0
	dec.b	r3l
	bne	5b
6:
	rts
