//for 1008M
#define STANDBY_COEFFICIENT (17)

    .text
    .globl standby_delay
standby_delay:
	push    {r0-r3}
	ldr  	r1, =STANDBY_COEFFICIENT
	mov	r3, r0
loop2:
	subs    r0, r0, #1
	bhi	loop2
	mov	r0, r3
	subs	r1, r1, #1
	bhi	loop2
	
	pop     {r0-r3}
	mov     pc, lr

    .text
    .globl standby_delay_cycle
standby_delay_cycle:
loop3:
	subs    r0, r0, #1
	bhi	loop3
	mov     pc, lr
	
	
