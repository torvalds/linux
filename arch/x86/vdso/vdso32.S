#include <linux/init.h>

__INITDATA

	.globl vdso32_int80_start, vdso32_int80_end
vdso32_int80_start:
	.incbin "arch/x86/vdso/vdso32-int80.so"
vdso32_int80_end:

	.globl vdso32_syscall_start, vdso32_syscall_end
vdso32_syscall_start:
#ifdef CONFIG_COMPAT
	.incbin "arch/x86/vdso/vdso32-syscall.so"
#endif
vdso32_syscall_end:

	.globl vdso32_sysenter_start, vdso32_sysenter_end
vdso32_sysenter_start:
	.incbin "arch/x86/vdso/vdso32-sysenter.so"
vdso32_sysenter_end:

__FINIT
