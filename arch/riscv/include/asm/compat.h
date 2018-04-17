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
#ifndef __ASM_COMPAT_H
#define __ASM_COMPAT_H
#ifdef CONFIG_COMPAT

#if defined(CONFIG_64BIT)
#define COMPAT_UTS_MACHINE "riscv64\0\0"
#elif defined(CONFIG_32BIT)
#define COMPAT_UTS_MACHINE "riscv32\0\0"
#else
#error "Unknown RISC-V base ISA"
#endif

#endif /*CONFIG_COMPAT*/
#endif /*__ASM_COMPAT_H*/
