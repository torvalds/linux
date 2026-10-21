/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copy the decompressed kernel onto START_ADDR, clear its bss and jump to
 * its entry point. See relocate_and_jump() in misc.c for why this cannot
 * run from the stub itself. It is position independent (no data or
 * rodata references, no gp) and clobbers only its argument registers and
 * $1, so misc.c can copy it anywhere and call it there.
 */

	.set noreorder

	.globl	trampoline_start
	.globl	trampoline_end
	.ent	trampoline_start
trampoline_start:
	/* $16 = dest, $17 = src, $18 = p_filesz, $19 = p_memsz, $20 = entry */

	/*
	 * dest and src are both page aligned, and everything above
	 * p_filesz is bss, so round both lengths up and work in quadwords
	 * only, using instructions every Alpha implements.
	 */
	addq	$18,7,$18
	bic	$18,7,$18
	addq	$19,7,$19
	bic	$19,7,$19
	subq	$19,$18,$19		/* bytes to zero after the copy */

1:	beq	$18,2f
	ldq	$1,0($17)
	stq	$1,0($16)
	addq	$17,8,$17
	addq	$16,8,$16
	subq	$18,8,$18
	br	$31,1b

2:	beq	$19,3f
	stq	$31,0($16)
	addq	$16,8,$16
	subq	$19,8,$19
	br	$31,2b

3:	imb
	jmp	$31,($20)
	.end trampoline_start
trampoline_end:
