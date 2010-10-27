/* timex.h: MN2WS0038 architecture timer specifications
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_UNIT_TIMEX_H
#define _ASM_UNIT_TIMEX_H

#ifndef __ASSEMBLY__
#include <linux/irq.h>
#endif /* __ASSEMBLY__ */

#include <asm/timer-regs.h>
#include <unit/clock.h>
#include <asm/param.h>

/*
 * jiffies counter specifications
 */

#define	TMJCBR_MAX		0xffffff	/* 24bit */
#define	TMJCBC			TMTBC

#define	TMJCMD			TMTMD
#define	TMJCBR			TMTBR
#define	TMJCIRQ			TMTIRQ
#define	TMJCICR			TMTICR

#ifndef __ASSEMBLY__

#define MN10300_SRC_IOBCLK	MN10300_IOBCLK

#ifndef HZ
# error HZ undeclared.
#endif /* !HZ */

#define MN10300_JCCLK		(MN10300_SRC_IOBCLK)
#define MN10300_TSCCLK		(MN10300_SRC_IOBCLK)

#define MN10300_JC_PER_HZ	((MN10300_JCCLK + HZ / 2) / HZ)
#define MN10300_TSC_PER_HZ	((MN10300_TSCCLK + HZ / 2) / HZ)

/* Check bit width of MTM interval value that sets base register */
#if (MN10300_JC_PER_HZ - 1) > TMJCBR_MAX
# error MTM tick timer interval value is overflow.
#endif


static inline void startup_jiffies_counter(void)
{
	u32 sync;

	TMJCBR = MN10300_JC_PER_HZ - 1;
	sync = TMJCBR;

	TMJCMD = TMTMD_TMTLDE;
	TMJCMD = TMTMD_TMTCNE;
	sync = TMJCMD;

	TMJCICR |= GxICR_ENABLE | GxICR_DETECT | GxICR_REQUEST;
	sync = TMJCICR;
}

static inline void shutdown_jiffies_counter(void)
{
}

#endif /* !__ASSEMBLY__ */


/*
 * timestamp counter specifications
 */

#define	TMTSCBR_MAX	0xffffffff
#define	TMTSCMD		TMSMD
#define	TMTSCBR		TMSBR
#define	TMTSCBC		TMSBC
#define	TMTSCICR	TMSICR

#ifndef __ASSEMBLY__

static inline void startup_timestamp_counter(void)
{
	u32 sync;

	/* set up TMS(Timestamp) 32bit timer register to count real time
	 * - count down from 4Gig-1 to 0 and wrap at IOBCLK rate
	 */

	TMTSCBR = TMTSCBR_MAX;
	sync = TMTSCBR;

	TMTSCICR = 0;
	sync = TMTSCICR;

	TMTSCMD = TMTMD_TMTLDE;
	TMTSCMD = TMTMD_TMTCNE;
	sync = TMTSCMD;
}

static inline void shutdown_timestamp_counter(void)
{
	TMTSCMD = 0;
}

/*
 * we use a cascaded pair of 16-bit down-counting timers to count I/O
 * clock cycles for the purposes of time keeping
 */
typedef unsigned long cycles_t;

static inline cycles_t read_timestamp_counter(void)
{
	return (cycles_t)TMTSCBC;
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_UNIT_TIMEX_H */
