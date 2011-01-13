#ifndef _ASM_X86_TIME_H
#define _ASM_X86_TIME_H

extern void hpet_time_init(void);

#include <asm/mc146818rtc.h>

extern void time_init(void);

#endif /* _ASM_X86_TIME_H */
