/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PROM_H
#define __PROM_H

#include <linux/spinlock.h>
#include <asm/prom.h>

void of_console_init(void);

extern unsigned int prom_early_allocated;

#endif /* __PROM_H */
