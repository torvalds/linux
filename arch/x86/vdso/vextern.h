#ifndef VEXTERN
#include <asm/vsyscall.h>
#define VEXTERN(x) \
	extern typeof(x) *vdso_ ## x __attribute__((visibility("hidden")));
#endif

#define VMAGIC 0xfeedbabeabcdefabUL

/* Any kernel variables used in the vDSO must be exported in the main
   kernel's vmlinux.lds.S/vsyscall.h/proper __section and
   put into vextern.h and be referenced as a pointer with vdso prefix.
   The main kernel later fills in the values.   */

VEXTERN(jiffies)
VEXTERN(vgetcpu_mode)
VEXTERN(vsyscall_gtod_data)
