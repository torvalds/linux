/*
 *
 * arch/arm/mach-u300/padmux.c
 *
 *
 * Copyright (C) 2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * U300 PADMUX functions
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 *
 */
#include <linux/io.h>
#include <linux/err.h>
#include <mach/u300-regs.h>
#include <mach/syscon.h>

#include "padmux.h"

/* Set the PAD MUX to route the MMC reader correctly to GPIO0. */
void pmx_set_mission_mode_mmc(void)
{
	u16 val;

	val = readw(U300_SYSCON_VBASE + U300_SYSCON_PMC1LR);
	val &= ~U300_SYSCON_PMC1LR_MMCSD_MASK;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_PMC1LR);
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_PMC1HR);
	val &= ~U300_SYSCON_PMC1HR_APP_GPIO_1_MASK;
	val |= U300_SYSCON_PMC1HR_APP_GPIO_1_MMC;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_PMC1HR);
}

void pmx_set_mission_mode_spi(void)
{
	u16 val;

	/* Set up padmuxing so the SPI port and its chipselects are active */
	val = readw(U300_SYSCON_VBASE + U300_SYSCON_PMC1HR);
	/*
	 * Activate the SPI port (disable the use of these pins for generic
	 * GPIO, DSP, AAIF
	 */
	val &= ~U300_SYSCON_PMC1HR_APP_SPI_2_MASK;
	val |= U300_SYSCON_PMC1HR_APP_SPI_2_SPI;
	/*
	 * Use GPIO pin SPI CS1 for CS1 actually (it can be used for other
	 * things also)
	 */
	val &= ~U300_SYSCON_PMC1HR_APP_SPI_CS_1_MASK;
	val |= U300_SYSCON_PMC1HR_APP_SPI_CS_1_SPI;
	/*
	 * Use GPIO pin SPI CS2 for CS2 actually (it can be used for other
	 * things also)
	 */
	val &= ~U300_SYSCON_PMC1HR_APP_SPI_CS_2_MASK;
	val |= U300_SYSCON_PMC1HR_APP_SPI_CS_2_SPI;
	writew(val, U300_SYSCON_VBASE + U300_SYSCON_PMC1HR);
}
