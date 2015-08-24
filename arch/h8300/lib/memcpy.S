;;; memcpy.S

#include <asm/linkage.h>

#if defined(CONFIG_CPU_H8300H)
	.h8300h
#endif
#if defined(CONFIG_CPU_H8S)
	.h8300s
#endif
	.text
.global memcpy

;;; void *memcpy(void *to, void *from, size_t n)
memcpy:
	mov.l	er2,er2
	bne	1f
	rts
1:
	;; address check
	bld	#0,r0l
	bxor	#0,r1l
	bcs	4f
	mov.l	er4,@-sp
	mov.l	er0,@-sp
	btst	#0,r0l
	beq	1f
	;; (aligned even) odd address
	mov.b	@er1,r3l
	mov.b	r3l,@er0
	adds	#1,er1
	adds	#1,er0
	dec.l	#1,er2
	beq	3f
1:
	;; n < sizeof(unsigned long) check
	sub.l	er4,er4
	adds	#4,er4		; loop count check value
	cmp.l	er4,er2
	blo	2f
	;; unsigned long copy
1:
	mov.l	@er1,er3
	mov.l	er3,@er0
	adds	#4,er0
	adds	#4,er1
	subs	#4,er2
	cmp.l	er4,er2
	bcc	1b
	;; rest
2:
	mov.l	er2,er2
	beq	3f
1:
	mov.b	@er1,r3l
	mov.b	r3l,@er0
	adds	#1,er1
	adds	#1,er0
	dec.l	#1,er2
	bne	1b
3:
	mov.l	@sp+,er0
	mov.l	@sp+,er4
	rts

	;; odd <- even / even <- odd
4:
	mov.l	er4,er3
	mov.l	er2,er4
	mov.l	er5,er2
	mov.l	er1,er5
	mov.l	er6,er1
	mov.l	er0,er6
1:
	eepmov.w
	mov.w	r4,r4
	bne	1b
	dec.w	#1,e4
	bpl	1b
	mov.l	er1,er6
	mov.l	er2,er5
	mov.l	er3,er4
	rts

	.end
