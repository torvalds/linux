/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005 Jeff Dike (jdike@karaya.com)
 */

#ifndef __MM_ID_H
#define __MM_ID_H

struct mm_id {
	int pid;
	unsigned long stack;
	int syscall_data_len;
};

void __switch_mm(struct mm_id *mm_idp);

#endif
