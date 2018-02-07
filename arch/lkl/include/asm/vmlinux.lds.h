#ifndef _LKL_VMLINUX_LDS_H
#define _LKL_VMLINUX_LDS_H

/* we encode our own __ro_after_init section */
#define RO_AFTER_INIT_DATA

#ifdef __MINGW32__
#define RODATA_SECTION .rdata
#endif

#include <asm-generic/vmlinux.lds.h>

#endif
