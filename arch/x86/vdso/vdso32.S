#include <linux/init.h>

__INITDATA

	.globl vdso32_default_start, vdso32_default_end
vdso32_default_start:
#ifdef CONFIG_X86_32
	.incbin "arch/x86/vdso/vdso32-int80.so"
#else
	.incbin "arch/x86/vdso/vdso32-syscall.so"
#endif
vdso32_default_end:

	.globl vdso32_sysenter_start, vdso32_sysenter_end
vdso32_sysenter_start:
	.incbin "arch/x86/vdso/vdso32-sysenter.so"
vdso32_sysenter_end:

__FINIT
