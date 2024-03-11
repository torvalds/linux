/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __IP32_COMMON_H
#define __IP32_COMMON_H

#include <linux/init.h>
#include <linux/interrupt.h>

void __init crime_init(void);
irqreturn_t crime_memerr_intr(int irq, void *dev_id);
irqreturn_t crime_cpuerr_intr(int irq, void *dev_id);
void __init ip32_be_init(void);
void ip32_prepare_poweroff(void);

#endif /* __IP32_COMMON_H */
