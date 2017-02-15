#include <asm/assembler.h>

#if !(defined(CONFIG_CPU_32v3) || defined(CONFIG_CPU_32v4))
#define GENERATE_FIXUPS
#endif

#define SYM(x) x##_thumb

	.macro	usr_ret, reg
	bx	\reg
	.endm

#include "kuser.S"
