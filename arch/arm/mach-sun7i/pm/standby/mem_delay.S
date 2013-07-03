#include "./pm_config.h"

#define ENABLE_SUPER_STANDBY

#ifdef ENABLE_SUPER_STANDBY
#define STANDBY_COEFFICIENT (17)
#else
#define STANDBY_COEFFICIENT (1)
#endif

    .text
    .globl mem_delay
mem_delay:
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
	
