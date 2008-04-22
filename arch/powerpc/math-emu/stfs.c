#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include "soft-fp.h"
#include "double.h"
#include "single.h"

int
stfs(void *frS, void *ea)
{
	FP_DECL_D(A);
	FP_DECL_S(R);
	float f;
	int err;

#ifdef DEBUG
	printk("%s: S %p, ea %p\n", __func__, frS, ea);
#endif

	__FP_UNPACK_D(A, frS);

#ifdef DEBUG
	printk("A: %ld %lu %lu %ld (%ld)\n", A_s, A_f1, A_f0, A_e, A_c);
#endif

	FP_CONV(S, D, 1, 2, R, A);

#ifdef DEBUG
	printk("R: %ld %lu %ld (%ld)\n", R_s, R_f, R_e, R_c);
#endif

	err = _FP_PACK_CANONICAL(S, 1, R);
	if (!err || !__FPU_TRAP_P(err)) {
		__FP_PACK_RAW_1(S, &f, R);
		if (copy_to_user(ea, &f, sizeof(float)))
			return -EFAULT;
	}

	return err;
}
