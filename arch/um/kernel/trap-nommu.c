// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <shared/kern_util.h>

int handle_page_fault(unsigned long address, unsigned long ip,
		      int is_write, int is_user, int *code_out)
{
	/* everything that's valid is already mapped in NOMMU */
	*code_out = SEGV_MAPERR;
	return -EFAULT;
}
