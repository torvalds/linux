#ifndef __ASM_SH_SECTIONS_H
#define __ASM_SH_SECTIONS_H

#include <asm-generic/sections.h>

extern void __nosave_begin, __nosave_end;
extern long __machvec_start, __machvec_end;
extern char __uncached_start, __uncached_end;
extern char _ebss[];
extern char __start_eh_frame[], __stop_eh_frame[];

#endif /* __ASM_SH_SECTIONS_H */

