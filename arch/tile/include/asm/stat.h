#ifdef CONFIG_COMPAT
#define __ARCH_WANT_STAT64	/* Used for compat_sys_stat64() etc. */
#endif
#include <asm-generic/stat.h>
