/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 */

#ifndef __ASM_JZ4740_CLOCK_H__
#define __ASM_JZ4740_CLOCK_H__

enum jz4740_wait_mode {
	JZ4740_WAIT_MODE_IDLE,
	JZ4740_WAIT_MODE_SLEEP,
};

void jz4740_clock_set_wait_mode(enum jz4740_wait_mode mode);

void jz4740_clock_suspend(void);
void jz4740_clock_resume(void);

void jz4740_clock_udc_enable_auto_suspend(void);
void jz4740_clock_udc_disable_auto_suspend(void);

#endif
