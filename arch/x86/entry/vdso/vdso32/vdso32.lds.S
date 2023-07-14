/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linker script for 32-bit vDSO.
 * We #include the file to define the layout details.
 *
 * This file defines the version script giving the user-exported symbols in
 * the DSO.
 */

#include <asm/page.h>

#define BUILD_VDSO32

#include "../vdso-layout.lds.S"

/* The ELF entry point can be used to set the AT_SYSINFO value.  */
ENTRY(__kernel_vsyscall);

/*
 * This controls what userland symbols we export from the vDSO.
 */
VERSION
{
	LINUX_2.6 {
	global:
		__vdso_clock_gettime;
		__vdso_gettimeofday;
		__vdso_time;
		__vdso_clock_getres;
		__vdso_clock_gettime64;
		__vdso_getcpu;
	};

	LINUX_2.5 {
	global:
		__kernel_vsyscall;
		__kernel_sigreturn;
		__kernel_rt_sigreturn;
	local: *;
	};
}
