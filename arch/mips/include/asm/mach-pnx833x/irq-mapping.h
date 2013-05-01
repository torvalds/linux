
/*
 *  irq.h: IRQ mappings for PNX833X.
 *
 *  Copyright 2008 NXP Semiconductors
 *	  Chris Steel <chris.steel@nxp.com>
 *    Daniel Laird <daniel.j.laird@nxp.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASM_MIPS_MACH_PNX833X_IRQ_MAPPING_H
#define __ASM_MIPS_MACH_PNX833X_IRQ_MAPPING_H
/*
 * The "IRQ numbers" are completely virtual.
 *
 * In PNX8330/1, we have 48 interrupt lines, numbered from 1 to 48.
 * Let's use numbers 1..48 for PIC interrupts, number 0 for timer interrupt,
 * numbers 49..64 for (virtual) GPIO interrupts.
 *
 * In PNX8335, we have 57 interrupt lines, numbered from 1 to 57,
 * connected to PIC, which uses core hardware interrupt 2, and also
 * a timer interrupt through hardware interrupt 5.
 * Let's use numbers 1..64 for PIC interrupts, number 0 for timer interrupt,
 * numbers 65..80 for (virtual) GPIO interrupts.
 *
 */
#include <irq.h>

#define PNX833X_TIMER_IRQ				(MIPS_CPU_IRQ_BASE + 7)

/* Interrupts supported by PIC */
#define PNX833X_PIC_I2C0_INT			(PNX833X_PIC_IRQ_BASE +	 1)
#define PNX833X_PIC_I2C1_INT			(PNX833X_PIC_IRQ_BASE +	 2)
#define PNX833X_PIC_UART0_INT			(PNX833X_PIC_IRQ_BASE +	 3)
#define PNX833X_PIC_UART1_INT			(PNX833X_PIC_IRQ_BASE +	 4)
#define PNX833X_PIC_TS_IN0_DV_INT		(PNX833X_PIC_IRQ_BASE +	 5)
#define PNX833X_PIC_TS_IN0_DMA_INT		(PNX833X_PIC_IRQ_BASE +	 6)
#define PNX833X_PIC_GPIO_INT			(PNX833X_PIC_IRQ_BASE +	 7)
#define PNX833X_PIC_AUDIO_DEC_INT		(PNX833X_PIC_IRQ_BASE +	 8)
#define PNX833X_PIC_VIDEO_DEC_INT		(PNX833X_PIC_IRQ_BASE +	 9)
#define PNX833X_PIC_CONFIG_INT			(PNX833X_PIC_IRQ_BASE + 10)
#define PNX833X_PIC_AOI_INT				(PNX833X_PIC_IRQ_BASE + 11)
#define PNX833X_PIC_SYNC_INT			(PNX833X_PIC_IRQ_BASE + 12)
#define PNX8330_PIC_SPU_INT				(PNX833X_PIC_IRQ_BASE + 13)
#define PNX8335_PIC_SATA_INT			(PNX833X_PIC_IRQ_BASE + 13)
#define PNX833X_PIC_OSD_INT				(PNX833X_PIC_IRQ_BASE + 14)
#define PNX833X_PIC_DISP1_INT			(PNX833X_PIC_IRQ_BASE + 15)
#define PNX833X_PIC_DEINTERLACER_INT	(PNX833X_PIC_IRQ_BASE + 16)
#define PNX833X_PIC_DISPLAY2_INT		(PNX833X_PIC_IRQ_BASE + 17)
#define PNX833X_PIC_VC_INT				(PNX833X_PIC_IRQ_BASE + 18)
#define PNX833X_PIC_SC_INT				(PNX833X_PIC_IRQ_BASE + 19)
#define PNX833X_PIC_IDE_INT				(PNX833X_PIC_IRQ_BASE + 20)
#define PNX833X_PIC_IDE_DMA_INT			(PNX833X_PIC_IRQ_BASE + 21)
#define PNX833X_PIC_TS_IN1_DV_INT		(PNX833X_PIC_IRQ_BASE + 22)
#define PNX833X_PIC_TS_IN1_DMA_INT		(PNX833X_PIC_IRQ_BASE + 23)
#define PNX833X_PIC_SGDX_DMA_INT		(PNX833X_PIC_IRQ_BASE + 24)
#define PNX833X_PIC_TS_OUT_INT			(PNX833X_PIC_IRQ_BASE + 25)
#define PNX833X_PIC_IR_INT				(PNX833X_PIC_IRQ_BASE + 26)
#define PNX833X_PIC_VMSP1_INT			(PNX833X_PIC_IRQ_BASE + 27)
#define PNX833X_PIC_VMSP2_INT			(PNX833X_PIC_IRQ_BASE + 28)
#define PNX833X_PIC_PIBC_INT			(PNX833X_PIC_IRQ_BASE + 29)
#define PNX833X_PIC_TS_IN0_TRD_INT		(PNX833X_PIC_IRQ_BASE + 30)
#define PNX833X_PIC_SGDX_TPD_INT		(PNX833X_PIC_IRQ_BASE + 31)
#define PNX833X_PIC_USB_INT				(PNX833X_PIC_IRQ_BASE + 32)
#define PNX833X_PIC_TS_IN1_TRD_INT		(PNX833X_PIC_IRQ_BASE + 33)
#define PNX833X_PIC_CLOCK_INT			(PNX833X_PIC_IRQ_BASE + 34)
#define PNX833X_PIC_SGDX_PARSER_INT		(PNX833X_PIC_IRQ_BASE + 35)
#define PNX833X_PIC_VMSP_DMA_INT		(PNX833X_PIC_IRQ_BASE + 36)

#if defined(CONFIG_SOC_PNX8335)
#define PNX8335_PIC_MIU_INT					(PNX833X_PIC_IRQ_BASE + 37)
#define PNX8335_PIC_AVCHIP_IRQ_INT			(PNX833X_PIC_IRQ_BASE + 38)
#define PNX8335_PIC_SYNC_HD_INT				(PNX833X_PIC_IRQ_BASE + 39)
#define PNX8335_PIC_DISP_HD_INT				(PNX833X_PIC_IRQ_BASE + 40)
#define PNX8335_PIC_DISP_SCALER_INT			(PNX833X_PIC_IRQ_BASE + 41)
#define PNX8335_PIC_OSD_HD1_INT				(PNX833X_PIC_IRQ_BASE + 42)
#define PNX8335_PIC_DTL_WRITER_Y_INT		(PNX833X_PIC_IRQ_BASE + 43)
#define PNX8335_PIC_DTL_WRITER_C_INT		(PNX833X_PIC_IRQ_BASE + 44)
#define PNX8335_PIC_DTL_EMULATOR_Y_IR_INT	(PNX833X_PIC_IRQ_BASE + 45)
#define PNX8335_PIC_DTL_EMULATOR_C_IR_INT	(PNX833X_PIC_IRQ_BASE + 46)
#define PNX8335_PIC_DENC_TTX_INT			(PNX833X_PIC_IRQ_BASE + 47)
#define PNX8335_PIC_MMI_SIF0_INT			(PNX833X_PIC_IRQ_BASE + 48)
#define PNX8335_PIC_MMI_SIF1_INT			(PNX833X_PIC_IRQ_BASE + 49)
#define PNX8335_PIC_MMI_CDMMU_INT			(PNX833X_PIC_IRQ_BASE + 50)
#define PNX8335_PIC_PIBCS_INT				(PNX833X_PIC_IRQ_BASE + 51)
#define PNX8335_PIC_ETHERNET_INT			(PNX833X_PIC_IRQ_BASE + 52)
#define PNX8335_PIC_VMSP1_0_INT				(PNX833X_PIC_IRQ_BASE + 53)
#define PNX8335_PIC_VMSP1_1_INT				(PNX833X_PIC_IRQ_BASE + 54)
#define PNX8335_PIC_VMSP1_DMA_INT			(PNX833X_PIC_IRQ_BASE + 55)
#define PNX8335_PIC_TDGR_DE_INT				(PNX833X_PIC_IRQ_BASE + 56)
#define PNX8335_PIC_IR1_IRQ_INT				(PNX833X_PIC_IRQ_BASE + 57)
#endif

/* GPIO interrupts */
#define PNX833X_GPIO_0_INT			(PNX833X_GPIO_IRQ_BASE +  0)
#define PNX833X_GPIO_1_INT			(PNX833X_GPIO_IRQ_BASE +  1)
#define PNX833X_GPIO_2_INT			(PNX833X_GPIO_IRQ_BASE +  2)
#define PNX833X_GPIO_3_INT			(PNX833X_GPIO_IRQ_BASE +  3)
#define PNX833X_GPIO_4_INT			(PNX833X_GPIO_IRQ_BASE +  4)
#define PNX833X_GPIO_5_INT			(PNX833X_GPIO_IRQ_BASE +  5)
#define PNX833X_GPIO_6_INT			(PNX833X_GPIO_IRQ_BASE +  6)
#define PNX833X_GPIO_7_INT			(PNX833X_GPIO_IRQ_BASE +  7)
#define PNX833X_GPIO_8_INT			(PNX833X_GPIO_IRQ_BASE +  8)
#define PNX833X_GPIO_9_INT			(PNX833X_GPIO_IRQ_BASE +  9)
#define PNX833X_GPIO_10_INT			(PNX833X_GPIO_IRQ_BASE + 10)
#define PNX833X_GPIO_11_INT			(PNX833X_GPIO_IRQ_BASE + 11)
#define PNX833X_GPIO_12_INT			(PNX833X_GPIO_IRQ_BASE + 12)
#define PNX833X_GPIO_13_INT			(PNX833X_GPIO_IRQ_BASE + 13)
#define PNX833X_GPIO_14_INT			(PNX833X_GPIO_IRQ_BASE + 14)
#define PNX833X_GPIO_15_INT			(PNX833X_GPIO_IRQ_BASE + 15)

#endif
