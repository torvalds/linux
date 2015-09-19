#ifndef _ASM_X86_DELAY_H
#define _ASM_X86_DELAY_H

#include <asm-generic/delay.h>

void use_tsc_delay(void);
void use_mwaitx_delay(void);

#endif /* _ASM_X86_DELAY_H */
