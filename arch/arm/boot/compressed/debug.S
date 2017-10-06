#include <linux/linkage.h>
#include <asm/assembler.h>

#ifndef CONFIG_DEBUG_SEMIHOSTING

#include CONFIG_DEBUG_LL_INCLUDE

ENTRY(putc)
	addruart r1, r2, r3
	waituart r3, r1
	senduart r0, r1
	busyuart r3, r1
	mov	 pc, lr
ENDPROC(putc)

#else

ENTRY(putc)
	adr	r1, 1f
	ldmia	r1, {r2, r3}
	add	r2, r2, r1
	ldr	r1, [r2, r3]
	strb	r0, [r1]
	mov	r0, #0x03		@ SYS_WRITEC
   ARM(	svc	#0x123456	)
#ifdef CONFIG_CPU_V7M
 THUMB(	bkpt	#0xab		)
#else
 THUMB(	svc	#0xab		)
#endif
	mov	pc, lr
	.align	2
1:	.word	_GLOBAL_OFFSET_TABLE_ - .
	.word	semi_writec_buf(GOT)
ENDPROC(putc)

	.bss
	.global	semi_writec_buf
	.type   semi_writec_buf, %object
semi_writec_buf:
	.space	4
	.size	semi_writec_buf, 4

#endif
