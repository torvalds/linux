#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include "soft-fp.h"

int
mtfsfi(unsigned int crfD, unsigned int IMM)
{
	u32 mask = 0xf;

	if (!crfD)
		mask = 9;

	__FPU_FPSCR &= ~(mask << ((7 - crfD) << 2));
	__FPU_FPSCR |= (IMM & 0xf) << ((7 - crfD) << 2);

#ifdef DEBUG
	printk("%s: %d %x: %08lx\n", __FUNCTION__, crfD, IMM, __FPU_FPSCR);
#endif

	return 0;
}
