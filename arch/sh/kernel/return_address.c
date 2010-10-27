/*
 * arch/sh/kernel/return_address.c
 *
 * Copyright (C) 2009  Matt Fleming
 * Copyright (C) 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/dwarf.h>

#ifdef CONFIG_DWARF_UNWINDER

void *return_address(unsigned int depth)
{
	struct dwarf_frame *frame;
	unsigned long ra;
	int i;

	for (i = 0, frame = NULL, ra = 0; i <= depth; i++) {
		struct dwarf_frame *tmp;

		tmp = dwarf_unwind_stack(ra, frame);
		if (!tmp)
			return NULL;

		if (frame)
			dwarf_free_frame(frame);

		frame = tmp;

		if (!frame || !frame->return_addr)
			break;

		ra = frame->return_addr;
	}

	/* Failed to unwind the stack to the specified depth. */
	WARN_ON(i != depth + 1);

	if (frame)
		dwarf_free_frame(frame);

	return (void *)ra;
}

#else

void *return_address(unsigned int depth)
{
	return NULL;
}

#endif

EXPORT_SYMBOL_GPL(return_address);
