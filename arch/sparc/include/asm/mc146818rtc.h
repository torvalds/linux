#ifndef ___ASM_SPARC_MC146818RTC_H
#define ___ASM_SPARC_MC146818RTC_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/mc146818rtc_64.h>
#else
#include <asm/mc146818rtc_32.h>
#endif
#endif
