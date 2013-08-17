/*
 * locore.S
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>

	.text

/*
 * Simple general exception handling routine. This one is used for the
 * Memory sizing routine for pmax machines. HK
 */

NESTED(genexcept_early, 0, sp)
	.set	noat
	.set	noreorder

	mfc0	k0, CP0_STATUS
	la	k1, mem_err

	sw	k0, 0(k1)

	mfc0	k0, CP0_EPC
	nop
	addiu	k0, 4		# skip the causing instruction
	jr	k0
	 rfe
END(genexcept_early)
