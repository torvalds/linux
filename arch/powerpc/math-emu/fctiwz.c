#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include <asm/sfp-machine.h>
#include <math-emu/soft-fp.h>
#include <math-emu/double.h>

int
fctiwz(u32 *frD, void *frB)
{
	FP_DECL_D(B);
	FP_DECL_EX;
	u32 fpscr;
	unsigned int r;

	fpscr = __FPU_FPSCR;
	__FPU_FPSCR &= ~(3);
	__FPU_FPSCR |= FP_RND_ZERO;

	FP_UNPACK_DP(B, frB);
	FP_TO_INT_D(r, B, 32, 1);
	frD[1] = r;

	__FPU_FPSCR = fpscr;

#ifdef DEBUG
	printk("%s: D %p, B %p: ", __func__, frD, frB);
	dump_double(frD);
	printk("\n");
#endif

	return 0;
}
