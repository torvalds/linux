#include <asm/vdso.h>

DEFINE_VDSO_IMAGE(vdso32_int80, "arch/x86/vdso/vdso32-int80.so")

#ifdef CONFIG_COMPAT
DEFINE_VDSO_IMAGE(vdso32_syscall, "arch/x86/vdso/vdso32-syscall.so")
#endif

DEFINE_VDSO_IMAGE(vdso32_sysenter, "arch/x86/vdso/vdso32-sysenter.so")
