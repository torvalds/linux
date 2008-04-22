#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include "soft-fp.h"

int
mtfsf(unsigned int FM, u32 *frB)
{
	u32 mask;

	if (FM == 0)
		return 0;

	if (FM == 0xff)
		mask = 0x9fffffff;
	else {
		mask = 0;
		if (FM & (1 << 0))
			mask |= 0x90000000;
		if (FM & (1 << 1))
			mask |= 0x0f000000;
		if (FM & (1 << 2))
			mask |= 0x00f00000;
		if (FM & (1 << 3))
			mask |= 0x000f0000;
		if (FM & (1 << 4))
			mask |= 0x0000f000;
		if (FM & (1 << 5))
			mask |= 0x00000f00;
		if (FM & (1 << 6))
			mask |= 0x000000f0;
		if (FM & (1 << 7))
			mask |= 0x0000000f;
	}

	__FPU_FPSCR &= ~(mask);
	__FPU_FPSCR |= (frB[1] & mask);

#ifdef DEBUG
	printk("%s: %02x %p: %08lx\n", __func__, FM, frB, __FPU_FPSCR);
#endif

	return 0;
}
