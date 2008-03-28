#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include "soft-fp.h"
#include "double.h"
#include "single.h"

int
frsp(void *frD, void *frB)
{
	FP_DECL_D(B);

#ifdef DEBUG
	printk("%s: D %p, B %p\n", __func__, frD, frB);
#endif

	__FP_UNPACK_D(B, frB);

#ifdef DEBUG
	printk("B: %ld %lu %lu %ld (%ld)\n", B_s, B_f1, B_f0, B_e, B_c);
#endif

	return __FP_PACK_DS(frD, B);
}
