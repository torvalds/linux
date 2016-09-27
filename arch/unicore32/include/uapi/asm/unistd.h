/*
 * linux/arch/unicore32/include/asm/unistd.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define __ARCH_WANT_RENAMEAT

#ifndef CONFIG_UNICORE32_OLDABI

/* Use the standard ABI for syscalls. */
#include <asm-generic/unistd.h>
#define __ARCH_WANT_SYS_CLONE

#else

#include <asm/unistd-oldabi.h>

#endif /* CONFIG_UNICORE32_OLDABI */
