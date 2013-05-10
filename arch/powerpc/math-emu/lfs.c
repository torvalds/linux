#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include <asm/sfp-machine.h>
#include <math-emu/soft-fp.h>
#include <math-emu/double.h>
#include <math-emu/single.h>

int
lfs(void *frD, void *ea)
{
	FP_DECL_D(R);
	FP_DECL_S(A);
	FP_DECL_EX;
	float f;

#ifdef DEBUG
	printk("%s: D %p, ea %p\n", __func__, frD, ea);
#endif

	if (copy_from_user(&f, ea, sizeof(float)))
		return -EFAULT;

	FP_UNPACK_S(A, f);

#ifdef DEBUG
	printk("A: %ld %lu %ld (%ld) [%08lx]\n", A_s, A_f, A_e, A_c,
	       *(unsigned long *)&f);
#endif

	FP_CONV(D, S, 2, 1, R, A);

#ifdef DEBUG
	printk("R: %ld %lu %lu %ld (%ld)\n", R_s, R_f1, R_f0, R_e, R_c);
#endif

	if (R_c == FP_CLS_NAN) {
		R_e = _FP_EXPMAX_D;
		_FP_PACK_RAW_2_P(D, frD, R);
	} else {
		__FP_PACK_D(frD, R);
	}

	return 0;
}
