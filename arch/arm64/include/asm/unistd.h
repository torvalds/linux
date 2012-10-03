/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#if !defined(__ASM_UNISTD_H) || defined(__SYSCALL)
#define __ASM_UNISTD_H

#ifndef __SYSCALL_COMPAT
#include <asm-generic/unistd.h>
#endif

#if defined(__KERNEL__) && defined(CONFIG_COMPAT)
#include <asm/unistd32.h>
#endif

#endif /* __ASM_UNISTD_H */
