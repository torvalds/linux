/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  DS1287 timer functions.
 *
 *  Copyright (C) 2008  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#ifndef __ASM_DS1287_H
#define __ASM_DS1287_H

extern int ds1287_timer_state(void);
extern int ds1287_set_base_clock(unsigned int hz);
extern int ds1287_clockevent_init(int irq);

#endif
