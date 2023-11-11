/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021 Cadence Design Systems Inc. */

#ifndef _XTENSA_PLATFORM_ISS_SIMCALL_GDBIO_H
#define _XTENSA_PLATFORM_ISS_SIMCALL_GDBIO_H

/*
 *  System call like services offered by the GDBIO host.
 */

#define SYS_open	-2
#define SYS_close	-3
#define SYS_read	-4
#define SYS_write	-5
#define SYS_lseek	-6

static int errno;

static inline int __simc(int a, int b, int c, int d)
{
	register int a1 asm("a2") = a;
	register int b1 asm("a6") = b;
	register int c1 asm("a3") = c;
	register int d1 asm("a4") = d;
	__asm__ __volatile__ (
			"break 1, 14\n"
			: "+r"(a1), "+r"(c1)
			: "r"(b1), "r"(d1)
			: "memory");
	errno = c1;
	return a1;
}

#endif /* _XTENSA_PLATFORM_ISS_SIMCALL_GDBIO_H */
