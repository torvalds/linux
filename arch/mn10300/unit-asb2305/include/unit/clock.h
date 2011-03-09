/* ASB2305-specific clocks
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_UNIT_CLOCK_H
#define _ASM_UNIT_CLOCK_H

#ifndef __ASSEMBLY__

#define MN10300_IOCLK		33333333UL
/* #define MN10300_IOBCLK	66666666UL */

#endif /* !__ASSEMBLY__ */

#define MN10300_WDCLK		MN10300_IOCLK

#endif /* _ASM_UNIT_CLOCK_H */
