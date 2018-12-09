/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/linkage.h>

ENTRY (_mcount)
	subi	sp, 20
	stw	a0, (sp, 0)
	stw	a1, (sp, 4)
	stw	a2, (sp, 8)
	stw	a3, (sp, 12)
	stw	lr, (sp, 16)
	mov	a1, lr
	ldw	a0, (sp, 20)
	jsri    csky_mcount
	ldw	a0, (sp, 0)
	ldw	a1, (sp, 4)
	ldw	a2, (sp, 8)
	ldw	a3, (sp, 12)
	ldw	t1, (sp, 16)
	ldw	lr, (sp, 20)
	addi	sp, 24
	jmp	t1
END (_mcount)
