/*
 * Copyright Altera Corporation (C) 2013. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef __ASM_NIOS2_SYSCALLS_H
#define __ASM_NIOS2_SYSCALLS_H

int sys_cacheflush(unsigned long addr, unsigned long len,
				unsigned int op);

#include <asm-generic/syscalls.h>

#endif /* __ASM_NIOS2_SYSCALLS_H */
