/*
 * Copyright (C) 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.section	".text.ftrace_trampoline", "ax"
	.align		3
0:	.quad		0
__ftrace_trampoline:
	ldr		x16, 0b
	br		x16
ENDPROC(__ftrace_trampoline)
