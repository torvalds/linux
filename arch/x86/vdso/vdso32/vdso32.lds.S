/*
 * Linker script for 32-bit vDSO.
 * We #include the file to define the layout details.
 * Here we only choose the prelinked virtual address.
 *
 * This file defines the version script giving the user-exported symbols in
 * the DSO.  We can define local symbols here called VDSO* to make their
 * values visible using the asm-x86/vdso.h macros from the kernel proper.
 */

#include <asm/page.h>

#define BUILD_VDSO32
#define VDSO_PRELINK 0

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
	};

	LINUX_2.5 {
	global:
		__kernel_vsyscall;
		__kernel_sigreturn;
		__kernel_rt_sigreturn;
	local: *;
	};
}

/*
 * Symbols we define here called VDSO* get their values into vdso32-syms.h.
 */
VDSO32_vsyscall		= __kernel_vsyscall;
VDSO32_sigreturn	= __kernel_sigreturn;
VDSO32_rt_sigreturn	= __kernel_rt_sigreturn;
VDSO32_clock_gettime	= clock_gettime;
VDSO32_gettimeofday	= gettimeofday;
VDSO32_time		= time;
