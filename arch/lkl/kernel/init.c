// SPDX-License-Identifier: GPL-2.0-only
#include <asm/host_ops.h>
#include <asm/kasan.h>

int lkl_init(struct lkl_host_operations *ops)
{
	if ((IS_ENABLED(CONFIG_LKL_HOST_MEMCPY) && !ops->memcpy)
	 || (IS_ENABLED(CONFIG_LKL_HOST_MEMSET) && !ops->memset)
	 || (IS_ENABLED(CONFIG_LKL_HOST_MEMMOVE) && !ops->memmove)) {
		lkl_printf("unexpected NULL lkl_host_ops member\n");
		return -1;
	}

	lkl_ops = ops;

	return kasan_init();
}

void lkl_cleanup(void)
{
	if (kasan_cleanup() < 0)
		lkl_printf("kasan: failed to cleanup\n");
}

