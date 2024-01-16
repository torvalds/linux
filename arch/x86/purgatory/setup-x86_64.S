/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * purgatory:  setup code
 *
 * Copyright (C) 2003,2004  Eric Biederman (ebiederm@xmission.com)
 * Copyright (C) 2014 Red Hat Inc.
 *
 * This code has been taken from kexec-tools.
 */
#include <linux/linkage.h>
#include <asm/purgatory.h>

	.text
	.balign 16
	.code64

SYM_CODE_START(purgatory_start)
	/* Load a gdt so I know what the segment registers are */
	lgdt	gdt(%rip)

	/* load the data segments */
	movl	$0x18, %eax	/* data segment */
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %ss
	movl	%eax, %fs
	movl	%eax, %gs

	/* Setup a stack */
	leaq	lstack_end(%rip), %rsp

	/* Call the C code */
	call purgatory
	jmp	entry64
SYM_CODE_END(purgatory_start)

	.section ".rodata"
	.balign 16
SYM_DATA_START_LOCAL(gdt)
	/* 0x00 unusable segment
	 * 0x08 unused
	 * so use them as the gdt ptr
	 */
	.word	gdt_end - gdt - 1
	.quad	gdt
	.word	0, 0, 0

	/* 0x10 4GB flat code segment */
	.word	0xFFFF, 0x0000, 0x9A00, 0x00AF

	/* 0x18 4GB flat data segment */
	.word	0xFFFF, 0x0000, 0x9200, 0x00CF
SYM_DATA_END_LABEL(gdt, SYM_L_LOCAL, gdt_end)

	.bss
	.balign 4096
SYM_DATA_START_LOCAL(lstack)
	.skip 4096
SYM_DATA_END_LABEL(lstack, SYM_L_LOCAL, lstack_end)
