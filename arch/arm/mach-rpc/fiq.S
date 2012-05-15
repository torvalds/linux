#include <linux/linkage.h>
#include <asm/assembler.h>
#include <mach/hardware.h>
#include <mach/entry-macro.S>

	.text

	.global	rpc_default_fiq_end
ENTRY(rpc_default_fiq_start)
	mov	r12, #ioc_base_high
	.if	ioc_base_low
	orr	r12, r12, #ioc_base_low
	.endif
	strb	r12, [r12, #0x38]	@ Disable FIQ register
	subs	pc, lr, #4
rpc_default_fiq_end:
