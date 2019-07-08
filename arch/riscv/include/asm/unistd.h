/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

/*
 * There is explicitly no include guard here because this file is expected to
 * be included multiple times.
 */

#define __ARCH_WANT_SYS_CLONE

#include <uapi/asm/unistd.h>

#define NR_syscalls (__NR_syscalls)
