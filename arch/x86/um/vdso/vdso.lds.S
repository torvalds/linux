/*
 * Linker script for 64-bit vDSO.
 * We #include the file to define the layout details.
 * Here we only choose the prelinked virtual address.
 *
 * This file defines the version script giving the user-exported symbols in
 * the DSO.  We can define local symbols here called VDSO* to make their
 * values visible using the asm-x86/vdso.h macros from the kernel proper.
 */

#define VDSO_PRELINK 0xffffffffff700000
#include "vdso-layout.lds.S"

/*
 * This controls what userland symbols we export from the vDSO.
 */
VERSION {
	LINUX_2.6 {
	global:
		clock_gettime;
		__vdso_clock_gettime;
		gettimeofday;
		__vdso_gettimeofday;
		getcpu;
		__vdso_getcpu;
		time;
		__vdso_time;
	local: *;
	};
}

VDSO64_PRELINK = VDSO_PRELINK;
