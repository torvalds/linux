/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Yulgon Kim <Yulgon.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_ARCH_MACH_REGS_USB_HOST_H
#define __ASM_ARCH_MACH_REGS_USB_HOST_H

#define INSNREG00(base)				(base + 0x90)
#define ENA_DMA_INCR				(0xF << 22)
#define ENA_INCR16				(1 << 25)
#define ENA_INCR8				(1 << 24)
#define ENA_INCR4				(1 << 23)
#define ENA_INCRX_ALIGN				(1 << 22)
#define APP_START_CLK				(1 << 21)
#define OHCI_SUSP_LGCY				(1 << 20)
#define MICROFRAME_BASE_VALUE_MASK		(0x1FFF << 1)
#define MICROFRAME_BASE_VALUE_SHIFT		(1)
#define ENA_MICROFRAME_LENGTH_VALUE		(1 << 0)

#define INSNREG01(base, offset)			(base + 0x94)
#define PACKET_BUFFER_THRESHOLDS_OUT_MASK	(0xFFFF << 16)
#define PACKET_BUFFER_THRESHOLDS_OUT_SHIFT	(16)
#define PACKET_BUFFER_THRESHOLDS_IN_MASK	(0xFFFF << 0)
#define PACKET_BUFFER_THRESHOLDS_IN_SHIFT	(0)

#define INSNREG02(base, offset)			(base + 0x98)
#define PACKET_BUFFER_DEPTH_MASK		(0xFFFFFFFF << 0)

#define INSNREG03(base, offset)			(base + 0x9C)
#define TX_TURNAROUD_DELAY_ADD_MASK		(0x7 << 10)
#define TX_TURNAROUD_DELAY_ADD_SHIFT		(10)
#define PERIODIC_FRAME_LIST_FETCH		(1 << 9)
#define TIME_AVAILABLE_OFFSET_MASK		(0xFF << 1)
#define TIME_AVAILABLE_OFFSET_SHIFT		(1)
#define BREAK_MEMORY_TRANSFER			(1 << 0)

#define INSNREG04(base, offset)			(base + 0xA0)

#define INSNREG05(base, offset)			(base + 0xA4)

#define INSNREG06(base, offset)			(base + 0xA8)
#define AHB_ERROR_CAPTURED			(1 << 31)
#define HBURST_VALUE_OF_CONTROL_MASK		(0x7 << 9)
#define HBURST_VALUE_OF_CONTROL_SHIFT		(9)
#define NUMBER_OF_BEATS_EXPECTED_MASK		(0x1F << 4)
#define NUMBER_OF_BEATS_EXPECTED_SHIFT		(4)
#define NUMBER_OF_SUCCESSFULLY_MASK		(0xF << 0)
#define NUMBER_OF_SUCCESSFULLY_SHIFT		(0)

#define INSNREG07(base, offset)			(base + 0xAC)
#define AHB_MASTER_ERROR_ADDRESS_MASK		(0xFFFFFFFF << 0)
#define AHB_MASTER_ERROR_ADDRESS_SHIFT		(0)

#endif /* __ASM_ARCH_MACH_REGS_USB_HOST_H */
