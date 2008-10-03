#ifndef ___ASM_SPARC_IPCBUF_H
#define ___ASM_SPARC_IPCBUF_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/ipcbuf_64.h>
#else
#include <asm/ipcbuf_32.h>
#endif
#endif
