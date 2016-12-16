/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <john@phrozen.org>
 */

#ifndef _LANTIQ_XWAY_IRQ_H__
#define _LANTIQ_XWAY_IRQ_H__

#define INT_NUM_IRQ0		8
#define INT_NUM_IM0_IRL0	(INT_NUM_IRQ0 + 0)
#define INT_NUM_IM1_IRL0	(INT_NUM_IRQ0 + 32)
#define INT_NUM_IM2_IRL0	(INT_NUM_IRQ0 + 64)
#define INT_NUM_IM3_IRL0	(INT_NUM_IRQ0 + 96)
#define INT_NUM_IM4_IRL0	(INT_NUM_IRQ0 + 128)
#define INT_NUM_IM_OFFSET	(INT_NUM_IM1_IRL0 - INT_NUM_IM0_IRL0)

#define LTQ_DMA_CH0_INT		(INT_NUM_IM2_IRL0)

#define MIPS_CPU_TIMER_IRQ	7

#define MAX_IM			5

#endif
