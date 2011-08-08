#include <linux/init.h>

__INITDATA

	.globl vdso_start, vdso_end
vdso_start:
	.incbin "arch/um/sys-x86_64/vdso/vdso.so"
vdso_end:

__FINIT
