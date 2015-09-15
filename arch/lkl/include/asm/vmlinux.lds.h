#ifndef _LKL_VMLINUX_LDS_H
#define _LKL_VMLINUX_LDS_H

#ifdef __MINGW32__
#define VMLINUX_SYMBOL(sym) _##sym
#define RODATA_SECTION .rdata
#endif

#include <asm-generic/vmlinux.lds.h>

#ifndef RODATA_SECTION
#define RODATA_SECTION .rodata
#endif

#endif
