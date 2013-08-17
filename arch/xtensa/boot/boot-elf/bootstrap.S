
#include <asm/bootparam.h>


/* ResetVector
 */
	.section        .ResetVector.text, "ax"
	.global         _ResetVector
_ResetVector:
	_j reset
	.align 4
RomInitAddr:
	.word 0xd0001000
RomBootParam:
	.word _bootparam
reset:
	l32r    a0, RomInitAddr
	l32r	a2, RomBootParam
	movi	a3, 0
	movi	a4, 0
	jx      a0

	.align 4
	.section .bootstrap.data, "aw"

	.globl _bootparam
_bootparam:
	.short	BP_TAG_FIRST
	.short	4
	.long	BP_VERSION
	.short	BP_TAG_LAST
	.short	0
	.long	0
