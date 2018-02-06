/* SPDX-License-Identifier: GPL-2.0 */
/* Cragganmore 6410 shared definitions
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *	Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#ifndef MACH_CRAG6410_H
#define MACH_CRAG6410_H

#include <mach/gpio-samsung.h>

#define GLENFARCLAS_PMIC_IRQ_BASE	IRQ_BOARD_START
#define BANFF_PMIC_IRQ_BASE		(IRQ_BOARD_START + 64)

#define PCA935X_GPIO_BASE		GPIO_BOARD_START
#define CODEC_GPIO_BASE			(GPIO_BOARD_START + 8)
#define GLENFARCLAS_PMIC_GPIO_BASE	(GPIO_BOARD_START + 32)
#define BANFF_PMIC_GPIO_BASE		(GPIO_BOARD_START + 64)
#define MMGPIO_GPIO_BASE		(GPIO_BOARD_START + 96)

#endif
