/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */
/* In compat mode, we use sys_llseek() for compat_sys_llseek(). */
#ifdef CONFIG_COMPAT
#define __ARCH_WANT_SYS_LLSEEK
#endif
#define __ARCH_WANT_SYS_NEWFSTATAT
#include <uapi/asm/unistd.h>
