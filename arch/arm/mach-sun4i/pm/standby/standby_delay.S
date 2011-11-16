
    .text
    .globl standby_delay
standby_delay:
	subs    r0, r0, #1
	bhi	    standby_delay
	mov     pc, lr

