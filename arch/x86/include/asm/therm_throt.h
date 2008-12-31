#ifndef _ASM_X86_THERM_THROT_H
#define _ASM_X86_THERM_THROT_H

#include <asm/atomic.h>

extern atomic_t therm_throt_en;
int therm_throt_process(int curr);

#endif /* _ASM_X86_THERM_THROT_H */
