#ifndef ___ASM_SPARC_FLOPPY_H
#define ___ASM_SPARC_FLOPPY_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/floppy_64.h>
#else
#include <asm/floppy_32.h>
#endif
#endif
