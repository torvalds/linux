/*
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2016 SiFive, Inc.
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
#ifndef __ASM_SIGINFO_H
#define __ASM_SIGINFO_H

#define __ARCH_SI_PREAMBLE_SIZE	(__SIZEOF_POINTER__ == 4 ? 12 : 16)

#include <asm-generic/siginfo.h>

#endif
