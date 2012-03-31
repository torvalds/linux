#ifndef __MACH_SRAM_H
#define __MACH_SRAM_H

#include <plat/sram.h>

#define SRAM_LOOPS_PER_USEC	24
#define SRAM_LOOP(loops)	do { unsigned int i = loops; barrier(); while (--i) barrier(); } while (0)
/* delay on slow mode */
#define sram_udelay(usecs)	SRAM_LOOP((usecs)*SRAM_LOOPS_PER_USEC)

#endif
