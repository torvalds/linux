// SPDX-License-Identifier: GPL-2.0-only
#include <asm/host_ops.h>

int lkl_init(struct lkl_host_operations *ops)
{
	lkl_ops = ops;
}

void lkl_cleanup(void)
{
}

