/* drivers/media/video/s5p-tv/regs-sdo.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * SDO register description file
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SAMSUNG_REGS_SDO_H
#define SAMSUNG_REGS_SDO_H

/*
 * Register part
 */

#define SDO_CLKCON			0x0000
#define SDO_CONFIG			0x0008
#define SDO_VBI				0x0014
#define SDO_DAC				0x003C
#define SDO_CCCON			0x0180
#define SDO_IRQ				0x0280
#define SDO_IRQMASK			0x0284
#define SDO_VERSION			0x03D8

/*
 * Bit definition part
 */

/* SDO Clock Control Register (SDO_CLKCON) */
#define SDO_TVOUT_SW_RESET		(1 << 4)
#define SDO_TVOUT_CLOCK_READY		(1 << 1)
#define SDO_TVOUT_CLOCK_ON		(1 << 0)

/* SDO Video Standard Configuration Register (SDO_CONFIG) */
#define SDO_PROGRESSIVE			(1 << 4)
#define SDO_NTSC_M			0
#define SDO_PAL_M			1
#define SDO_PAL_BGHID			2
#define SDO_PAL_N			3
#define SDO_PAL_NC			4
#define SDO_NTSC_443			8
#define SDO_PAL_60			9
#define SDO_STANDARD_MASK		0xf

/* SDO VBI Configuration Register (SDO_VBI) */
#define SDO_CVBS_WSS_INS		(1 << 14)
#define SDO_CVBS_CLOSED_CAPTION_MASK	(3 << 12)

/* SDO DAC Configuration Register (SDO_DAC) */
#define SDO_POWER_ON_DAC		(1 << 0)

/* SDO Color Compensation On/Off Control (SDO_CCCON) */
#define SDO_COMPENSATION_BHS_ADJ_OFF	(1 << 4)
#define SDO_COMPENSATION_CVBS_COMP_OFF	(1 << 0)

/* SDO Interrupt Request Register (SDO_IRQ) */
#define SDO_VSYNC_IRQ_PEND		(1 << 0)

#endif /* SAMSUNG_REGS_SDO_H */
