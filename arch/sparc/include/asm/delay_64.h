/* SPDX-License-Identifier: GPL-2.0 */
/* delay.h: Linux delay routines on sparc64.
 *
 * Copyright (C) 1996, 2004, 2007 David S. Miller (davem@davemloft.net).
 */

#ifndef _SPARC64_DELAY_H
#define _SPARC64_DELAY_H

#ifndef __ASSEMBLER__

void __delay(unsigned long loops);
void udelay(unsigned long usecs);
#define mdelay(n)	udelay((n) * 1000)

#endif /* !__ASSEMBLER__ */

#endif /* _SPARC64_DELAY_H */
