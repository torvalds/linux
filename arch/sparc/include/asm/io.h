#ifndef ___ASM_SPARC_IO_H
#define ___ASM_SPARC_IO_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/io_64.h>
#else
#include <asm/io_32.h>
#endif
#endif
