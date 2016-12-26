#ifndef _LKL_VMLINUX_LDS_H
#define _LKL_VMLINUX_LDS_H

#ifdef __MINGW32__
#define RODATA_SECTION .rdata
#define RO_AFTER_INIT_DATA
#endif

#include <asm-generic/vmlinux.lds.h>

#endif
