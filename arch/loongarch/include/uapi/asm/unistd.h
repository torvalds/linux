/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#include <asm/bitsperlong.h>

#if __BITS_PER_LONG == 32
#include <asm/unistd_32.h>
#else
#include <asm/unistd_64.h>
#endif
