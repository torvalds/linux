/*
 * Common header file for blackfin family of processors.
 *
 */

#ifndef _BLACKFIN_H_
#define _BLACKFIN_H_

#define LO(con32) ((con32) & 0xFFFF)
#define lo(con32) ((con32) & 0xFFFF)
#define HI(con32) (((con32) >> 16) & 0xFFFF)
#define hi(con32) (((con32) >> 16) & 0xFFFF)

#include <mach/anomaly.h>

#ifndef __ASSEMBLY__

/* SSYNC implementation for C file */
static inline void SSYNC(void)
{
	int _tmp;
	if (ANOMALY_05000312)
		__asm__ __volatile__(
			"cli %0;"
			"nop;"
			"nop;"
			"ssync;"
			"sti %0;"
			: "=d" (_tmp)
		);
	else if (ANOMALY_05000244)
		__asm__ __volatile__(
			"nop;"
			"nop;"
			"nop;"
			"ssync;"
		);
	else
		__asm__ __volatile__("ssync;");
}

/* CSYNC implementation for C file */
static inline void CSYNC(void)
{
	int _tmp;
	if (ANOMALY_05000312)
		__asm__ __volatile__(
			"cli %0;"
			"nop;"
			"nop;"
			"csync;"
			"sti %0;"
			: "=d" (_tmp)
		);
	else if (ANOMALY_05000244)
		__asm__ __volatile__(
			"nop;"
			"nop;"
			"nop;"
			"csync;"
		);
	else
		__asm__ __volatile__("csync;");
}

#else  /* __ASSEMBLY__ */

/* SSYNC & CSYNC implementations for assembly files */

#define ssync(x) SSYNC(x)
#define csync(x) CSYNC(x)

#if ANOMALY_05000312
#define SSYNC(scratch) cli scratch; nop; nop; SSYNC; sti scratch;
#define CSYNC(scratch) cli scratch; nop; nop; CSYNC; sti scratch;

#elif ANOMALY_05000244
#define SSYNC(scratch) nop; nop; nop; SSYNC;
#define CSYNC(scratch) nop; nop; nop; CSYNC;

#else
#define SSYNC(scratch) SSYNC;
#define CSYNC(scratch) CSYNC;

#endif /* ANOMALY_05000312 & ANOMALY_05000244 handling */

#endif /* __ASSEMBLY__ */

#include <mach/blackfin.h>
#include <asm/bfin-global.h>

#endif				/* _BLACKFIN_H_ */
