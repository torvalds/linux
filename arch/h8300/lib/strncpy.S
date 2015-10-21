;;; strncpy.S

#include <asm/linkage.h>

	.text
.global strncpy_from_user

;;; long strncpy_from_user(void *to, void *from, size_t n)
strncpy_from_user:
	mov.l	er2,er2
	bne	1f
	sub.l	er0,er0
	rts
1:
	mov.l	er4,@-sp
	sub.l	er3,er3
2:
	mov.b	@er1+,r4l
	mov.b	r4l,@er0
	adds	#1,er0
	beq	3f
	inc.l	#1,er3
	dec.l	#1,er2
	bne	2b
3:
	dec.l	#1,er2
4:
	mov.b	r4l,@er0
	adds	#1,er0
	dec.l	#1,er2
	bne	4b
	mov.l	er3,er0
	mov.l	@sp+,er4
	rts
