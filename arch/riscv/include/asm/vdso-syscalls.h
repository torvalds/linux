/*
 * Copyright (C) 2017 SiFive
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

#ifndef _ASM_RISCV_VDSO_SYSCALLS_H
#define _ASM_RISCV_VDSO_SYSCALLS_H

#ifdef CONFIG_SMP

/* These syscalls are only used by the vDSO and are not in the uapi. */
#define __NR_riscv_flush_icache (__NR_arch_specific_syscall + 15)
__SYSCALL(__NR_riscv_flush_icache, sys_riscv_flush_icache)

#endif

#endif /* _ASM_RISCV_VDSO_H */
