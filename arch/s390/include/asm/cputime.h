/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright IBM Corp. 2004
 *
 *  Author: Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _S390_CPUTIME_H
#define _S390_CPUTIME_H

#include <linux/types.h>
#include <asm/timex.h>

/*
 * Convert cputime to nanoseconds.
 */
#define cputime_to_nsecs(cputime) tod_to_ns(cputime)

void account_idle_time_irq(void);

#endif /* _S390_CPUTIME_H */
