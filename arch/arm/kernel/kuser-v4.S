#include <asm/assembler.h>

#define GENERATE_FIXUPS

#define SYM(x) x##_v4

	.macro	usr_ret, reg
	mov	pc, \reg
	.endm

#include "kuser.S"
