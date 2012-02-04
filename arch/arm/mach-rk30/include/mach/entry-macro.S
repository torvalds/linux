#include <mach/io.h>

#define HAVE_GET_IRQNR_PREAMBLE
#include <asm/hardware/entry-macro-gic.S>

	.macro	disable_fiq
        .endm

	.macro	get_irqnr_preamble, base, tmp
	ldr	\base, =RK30_GICC_BASE
	.endm

	.macro	arch_ret_to_user, tmp1, tmp2
        .endm
