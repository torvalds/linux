#ifndef __MMU_H
#define __MMU_H

/* Copyright (C) 2002, David McCullough <davidm@snapgear.com> */

typedef struct {
	struct vm_list_struct	*vmlist;
	unsigned long		end_brk;
} mm_context_t;

#endif
