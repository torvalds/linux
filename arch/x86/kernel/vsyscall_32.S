#include <linux/init.h>

__INITDATA

	.globl vsyscall_int80_start, vsyscall_int80_end
vsyscall_int80_start:
	.incbin "arch/x86/kernel/vsyscall-int80_32.so"
vsyscall_int80_end:

	.globl vsyscall_sysenter_start, vsyscall_sysenter_end
vsyscall_sysenter_start:
	.incbin "arch/x86/kernel/vsyscall-sysenter_32.so"
vsyscall_sysenter_end:

__FINIT
