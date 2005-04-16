#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include "soft-fp.h"

int
mtfsb0(int crbD)
{
	if ((crbD != 1) && (crbD != 2))
		__FPU_FPSCR &= ~(1 << (31 - crbD));

#ifdef DEBUG
	printk("%s: %d %08lx\n", __FUNCTION__, crbD, __FPU_FPSCR);
#endif

	return 0;
}
