/*
 * arch/arm/mach-sa1100/include/mach/nanoengine.h
 *
 * This file contains the hardware specific definitions for nanoEngine.
 * Only include this file from SA1100-specific files.
 *
 * Copyright (C) 2010 Marcelo Roberto Jimenez <mroberto@cpti.cetuc.puc-rio.br>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __ASM_ARCH_NANOENGINE_H
#define __ASM_ARCH_NANOENGINE_H

#define GPIO_PC_READY0	GPIO_GPIO(11) /* ready for socket 0 (active high)*/
#define GPIO_PC_READY1	GPIO_GPIO(12) /* ready for socket 1 (active high) */
#define GPIO_PC_CD0	GPIO_GPIO(13) /* detect for socket 0 (active low) */
#define GPIO_PC_CD1	GPIO_GPIO(14) /* detect for socket 1 (active low) */
#define GPIO_PC_RESET0	GPIO_GPIO(15) /* reset socket 0 */
#define GPIO_PC_RESET1	GPIO_GPIO(16) /* reset socket 1 */

#define NANOENGINE_IRQ_GPIO_PC_READY0	IRQ_GPIO11
#define NANOENGINE_IRQ_GPIO_PC_READY1	IRQ_GPIO12
#define NANOENGINE_IRQ_GPIO_PC_CD0	IRQ_GPIO13
#define NANOENGINE_IRQ_GPIO_PC_CD1	IRQ_GPIO14

#endif

