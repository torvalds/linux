#ifndef __MACH_SRAM_H
#define __MACH_SRAM_H

#include <plat/sram.h>

#define SRAM_LOOPS_PER_USEC	24
#define SRAM_LOOP(loops)	do { unsigned int i = (loops); if (i < 7) i = 7; barrier(); asm volatile(".align 4; 1: subs %0, %0, #1; bne 1b;" : "+r" (i)); } while (0)
/* delay on slow mode */
#define sram_udelay(usecs)	SRAM_LOOP((usecs)*SRAM_LOOPS_PER_USEC)
/* delay on deep slow mode */
#define sram_32k_udelay(usecs)	SRAM_LOOP(((usecs)*SRAM_LOOPS_PER_USEC)/(24000000/32768))

#endif
