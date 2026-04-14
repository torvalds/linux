/*
 * Linker script for sparc32 vDSO
 * We #include the file to define the layout details.
 *
 * This file defines the version script giving the user-exported symbols in
 * the DSO.
 */

#define	BUILD_VDSO32
#include "../vdso-layout.lds.S"

/*
 * This controls what userland symbols we export from the vDSO.
 */
VERSION {
	LINUX_2.6 {
	global:
		clock_gettime;
		__vdso_clock_gettime;
		clock_gettime64;
		__vdso_clock_gettime64;
		gettimeofday;
		__vdso_gettimeofday;
	local: *;
	};
}
