#ifndef ___ASM_SPARC_SIGNAL_H
#define ___ASM_SPARC_SIGNAL_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/signal_64.h>
#else
#include <asm/signal_32.h>
#endif
#endif
