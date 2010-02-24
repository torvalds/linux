/*
 * atomic64_t for 386/486
 *
 * Copyright © 2010  Luca Barbieri
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/linkage.h>
#include <asm/alternative-asm.h>
#include <asm/dwarf2.h>

/* if you want SMP support, implement these with real spinlocks */
.macro LOCK reg
	pushfl
	CFI_ADJUST_CFA_OFFSET 4
	cli
.endm

.macro UNLOCK reg
	popfl
	CFI_ADJUST_CFA_OFFSET -4
.endm

.macro BEGIN func reg
$v = \reg

ENTRY(atomic64_\func\()_386)
	CFI_STARTPROC
	LOCK $v

.macro RETURN
	UNLOCK $v
	ret
.endm

.macro END_
	CFI_ENDPROC
ENDPROC(atomic64_\func\()_386)
.purgem RETURN
.purgem END_
.purgem END
.endm

.macro END
RETURN
END_
.endm
.endm

BEGIN read %ecx
	movl  ($v), %eax
	movl 4($v), %edx
END

BEGIN set %esi
	movl %ebx,  ($v)
	movl %ecx, 4($v)
END

BEGIN xchg %esi
	movl  ($v), %eax
	movl 4($v), %edx
	movl %ebx,  ($v)
	movl %ecx, 4($v)
END

BEGIN add %ecx
	addl %eax,  ($v)
	adcl %edx, 4($v)
END

BEGIN add_return %ecx
	addl  ($v), %eax
	adcl 4($v), %edx
	movl %eax,  ($v)
	movl %edx, 4($v)
END

BEGIN sub %ecx
	subl %eax,  ($v)
	sbbl %edx, 4($v)
END

BEGIN sub_return %ecx
	negl %edx
	negl %eax
	sbbl $0, %edx
	addl  ($v), %eax
	adcl 4($v), %edx
	movl %eax,  ($v)
	movl %edx, 4($v)
END

BEGIN inc %esi
	addl $1,  ($v)
	adcl $0, 4($v)
END

BEGIN inc_return %esi
	movl  ($v), %eax
	movl 4($v), %edx
	addl $1, %eax
	adcl $0, %edx
	movl %eax,  ($v)
	movl %edx, 4($v)
END

BEGIN dec %esi
	subl $1,  ($v)
	sbbl $0, 4($v)
END

BEGIN dec_return %esi
	movl  ($v), %eax
	movl 4($v), %edx
	subl $1, %eax
	sbbl $0, %edx
	movl %eax,  ($v)
	movl %edx, 4($v)
END

BEGIN add_unless %ecx
	addl %eax, %esi
	adcl %edx, %edi
	addl  ($v), %eax
	adcl 4($v), %edx
	cmpl %eax, %esi
	je 3f
1:
	movl %eax,  ($v)
	movl %edx, 4($v)
	xorl %eax, %eax
2:
RETURN
3:
	cmpl %edx, %edi
	jne 1b
	movl $1, %eax
	jmp 2b
END_

BEGIN inc_not_zero %esi
	movl  ($v), %eax
	movl 4($v), %edx
	testl %eax, %eax
	je 3f
1:
	addl $1, %eax
	adcl $0, %edx
	movl %eax,  ($v)
	movl %edx, 4($v)
	xorl %eax, %eax
2:
RETURN
3:
	testl %edx, %edx
	jne 1b
	movl $1, %eax
	jmp 2b
END_

BEGIN dec_if_positive %esi
	movl  ($v), %eax
	movl 4($v), %edx
	subl $1, %eax
	sbbl $0, %edx
	js 1f
	movl %eax,  ($v)
	movl %edx, 4($v)
1:
END
