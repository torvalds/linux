/*
 * Copyright (C) 2015  ARM Limited
 * Author: Dave Martin <Dave.Martin@arm.com>
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

#ifndef _ARCH_ARM64_ASM__H
#define _ARCH_ARM64_ASM__H

#include <linux/stringify.h>

#include <asm/asm-.h>

#define ___FLAGS(flags)				\
	asm volatile (__stringify(ASM__FLAGS(flags)));

#define () do {					\
	___FLAGS(0);					\
	unreachable();					\
} while (0)

#define __WARN_FLAGS(flags) ___FLAGS(FLAG_WARNING|(flags))

#define HAVE_ARCH_

#include <asm-generic/.h>

#endif /* ! _ARCH_ARM64_ASM__H */
