// SPDX-License-Identifier: GPL-2.0-only
#include <asm/host_ops.h>
#include <asm/kasan.h>

int lkl_init(struct lkl_host_operations *ops)
{
	lkl_ops = ops;

	return kasan_init();
}

void lkl_cleanup(void)
{
	if (kasan_cleanup() < 0)
		lkl_printf("kasan: failed to cleanup\n");
}

