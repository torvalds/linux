#ifndef ___ASM_SPARC_PAGE_H
#define ___ASM_SPARC_PAGE_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/page_64.h>
#else
#include <asm/page_32.h>
#endif
#endif
