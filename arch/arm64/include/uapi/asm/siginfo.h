/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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
#ifndef __ASM_SIGINFO_H
#define __ASM_SIGINFO_H

#define __ARCH_SI_PREAMBLE_SIZE	(4 * sizeof(int))

#include <asm-generic/siginfo.h>

/*
 * SIGFPE si_codes
 */
#ifdef __KERNEL__
#define FPE_FIXME	0	/* Broken dup of SI_USER */
#endif /* __KERNEL__ */

/*
 * SIGBUS si_codes
 */
#ifdef __KERNEL__
#define BUS_FIXME	0	/* Broken dup of SI_USER */
#endif /* __KERNEL__ */

/*
 * SIGTRAP si_codes
 */
#ifdef __KERNEL__
#define TRAP_FIXME	0	/* Broken dup of SI_USER */
#endif /* __KERNEL__ */

#endif
