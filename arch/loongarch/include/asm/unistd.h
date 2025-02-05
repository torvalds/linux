/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <uapi/asm/unistd.h>

#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_SYS_CLONE

#define NR_syscalls (__NR_syscalls)
