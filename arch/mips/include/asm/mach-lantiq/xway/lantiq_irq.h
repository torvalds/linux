/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
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

#define LTQ_ASC_TIR(x)		(INT_NUM_IM3_IRL0 + (x * 8))
#define LTQ_ASC_RIR(x)		(INT_NUM_IM3_IRL0 + (x * 8) + 1)
#define LTQ_ASC_EIR(x)		(INT_NUM_IM3_IRL0 + (x * 8) + 2)

#define LTQ_ASC_ASE_TIR		INT_NUM_IM2_IRL0
#define LTQ_ASC_ASE_RIR		(INT_NUM_IM2_IRL0 + 2)
#define LTQ_ASC_ASE_EIR		(INT_NUM_IM2_IRL0 + 3)

#define LTQ_SSC_TIR		(INT_NUM_IM0_IRL0 + 15)
#define LTQ_SSC_RIR		(INT_NUM_IM0_IRL0 + 14)
#define LTQ_SSC_EIR		(INT_NUM_IM0_IRL0 + 16)

#define LTQ_MEI_DYING_GASP_INT	(INT_NUM_IM1_IRL0 + 21)
#define LTQ_MEI_INT		(INT_NUM_IM1_IRL0 + 23)

#define LTQ_TIMER6_INT		(INT_NUM_IM1_IRL0 + 23)
#define LTQ_USB_INT		(INT_NUM_IM1_IRL0 + 22)
#define LTQ_USB_OC_INT		(INT_NUM_IM4_IRL0 + 23)

#define MIPS_CPU_TIMER_IRQ		7

#define LTQ_DMA_CH0_INT		(INT_NUM_IM2_IRL0)
#define LTQ_DMA_CH1_INT		(INT_NUM_IM2_IRL0 + 1)
#define LTQ_DMA_CH2_INT		(INT_NUM_IM2_IRL0 + 2)
#define LTQ_DMA_CH3_INT		(INT_NUM_IM2_IRL0 + 3)
#define LTQ_DMA_CH4_INT		(INT_NUM_IM2_IRL0 + 4)
#define LTQ_DMA_CH5_INT		(INT_NUM_IM2_IRL0 + 5)
#define LTQ_DMA_CH6_INT		(INT_NUM_IM2_IRL0 + 6)
#define LTQ_DMA_CH7_INT		(INT_NUM_IM2_IRL0 + 7)
#define LTQ_DMA_CH8_INT		(INT_NUM_IM2_IRL0 + 8)
#define LTQ_DMA_CH9_INT		(INT_NUM_IM2_IRL0 + 9)
#define LTQ_DMA_CH10_INT	(INT_NUM_IM2_IRL0 + 10)
#define LTQ_DMA_CH11_INT	(INT_NUM_IM2_IRL0 + 11)
#define LTQ_DMA_CH12_INT	(INT_NUM_IM2_IRL0 + 25)
#define LTQ_DMA_CH13_INT	(INT_NUM_IM2_IRL0 + 26)
#define LTQ_DMA_CH14_INT	(INT_NUM_IM2_IRL0 + 27)
#define LTQ_DMA_CH15_INT	(INT_NUM_IM2_IRL0 + 28)
#define LTQ_DMA_CH16_INT	(INT_NUM_IM2_IRL0 + 29)
#define LTQ_DMA_CH17_INT	(INT_NUM_IM2_IRL0 + 30)
#define LTQ_DMA_CH18_INT	(INT_NUM_IM2_IRL0 + 16)
#define LTQ_DMA_CH19_INT	(INT_NUM_IM2_IRL0 + 21)

#define LTQ_PPE_MBOX_INT	(INT_NUM_IM2_IRL0 + 24)

#define INT_NUM_IM4_IRL14	(INT_NUM_IM4_IRL0 + 14)

#endif
