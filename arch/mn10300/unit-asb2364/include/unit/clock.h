/* clock.h: unit-specific clocks
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * Modified by Matsushita Electric Industrial Co., Ltd.
 * Modifications:
 *	23-Feb-2007 MEI Add define for watchdog timer.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_UNIT_CLOCK_H
#define _ASM_UNIT_CLOCK_H

#ifndef __ASSEMBLY__

#define MN10300_IOCLK		100000000UL		/* for DDR800 */
/*#define MN10300_IOCLK		83333333UL */		/* for DDR667 */
#define MN10300_IOBCLK		MN10300_IOCLK		/* IOBCLK is equal to IOCLK */

#endif /* !__ASSEMBLY__ */

#define MN10300_WDCLK		27000000UL

#endif /* _ASM_UNIT_CLOCK_H */
