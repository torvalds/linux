/*
 * Machine dependent access functions for RTC registers.
 */
#ifndef _ASM_MC146818RTC_H
#define _ASM_MC146818RTC_H


#ifdef CONFIG_ATARI
/* RTC in Atari machines */

#include <asm/atarihw.h>

#define ATARI_RTC_PORT(x)	(TT_RTC_BAS + 2*(x))
#define RTC_ALWAYS_BCD	0

#define CMOS_READ(addr) ({ \
atari_outb_p((addr), ATARI_RTC_PORT(0)); \
atari_inb_p(ATARI_RTC_PORT(1)); \
})
#define CMOS_WRITE(val, addr) ({ \
atari_outb_p((addr), ATARI_RTC_PORT(0)); \
atari_outb_p((val), ATARI_RTC_PORT(1)); \
})
#endif /* CONFIG_ATARI */

#endif /* _ASM_MC146818RTC_H */
