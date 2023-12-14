// SPDX-License-Identifier: GPL-2.0
#ifdef CONFIG_UCLINUX // FIXME(mreis): We cannot use CONFIG_M68KCLASSIC because coldfire is also contained in setup_classic. so classic is not strictly true
#include "setup_uclinux.c"
#else
#include "setup_classic.c"
#endif

#if IS_ENABLED(CONFIG_INPUT_M68K_BEEP)
void (*mach_beep)(unsigned int, unsigned int);
EXPORT_SYMBOL(mach_beep);
#endif
