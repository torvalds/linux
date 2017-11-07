/*
 * Copyright(c) 2009 Ian Molton <spyro@f2s.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/mfd/tmio.h>

#define CNF_CMD     0x04
#define CNF_CTL_BASE   0x10
#define CNF_INT_PIN  0x3d
#define CNF_STOP_CLK_CTL 0x40
#define CNF_GCLK_CTL 0x41
#define CNF_SD_CLK_MODE 0x42
#define CNF_PIN_STATUS 0x44
#define CNF_PWR_CTL_1 0x48
#define CNF_PWR_CTL_2 0x49
#define CNF_PWR_CTL_3 0x4a
#define CNF_CARD_DETECT_MODE 0x4c
#define CNF_SD_SLOT 0x50
#define CNF_EXT_GCLK_CTL_1 0xf0
#define CNF_EXT_GCLK_CTL_2 0xf1
#define CNF_EXT_GCLK_CTL_3 0xf9
#define CNF_SD_LED_EN_1 0xfa
#define CNF_SD_LED_EN_2 0xfe

#define   SDCREN 0x2   /* Enable access to MMC CTL regs. (flag in COMMAND_REG)*/

int tmio_core_mmc_enable(void __iomem *cnf, int shift, unsigned long base)
{
	/* Enable the MMC/SD Control registers */
	sd_config_write16(cnf, shift, CNF_CMD, SDCREN);
	sd_config_write32(cnf, shift, CNF_CTL_BASE, base & 0xfffe);

	/* Disable SD power during suspend */
	sd_config_write8(cnf, shift, CNF_PWR_CTL_3, 0x01);

	/* The below is required but why? FIXME */
	sd_config_write8(cnf, shift, CNF_STOP_CLK_CTL, 0x1f);

	/* Power down SD bus */
	sd_config_write8(cnf, shift, CNF_PWR_CTL_2, 0x00);

	return 0;
}
EXPORT_SYMBOL(tmio_core_mmc_enable);

int tmio_core_mmc_resume(void __iomem *cnf, int shift, unsigned long base)
{

	/* Enable the MMC/SD Control registers */
	sd_config_write16(cnf, shift, CNF_CMD, SDCREN);
	sd_config_write32(cnf, shift, CNF_CTL_BASE, base & 0xfffe);

	return 0;
}
EXPORT_SYMBOL(tmio_core_mmc_resume);

void tmio_core_mmc_pwr(void __iomem *cnf, int shift, int state)
{
	sd_config_write8(cnf, shift, CNF_PWR_CTL_2, state ? 0x02 : 0x00);
}
EXPORT_SYMBOL(tmio_core_mmc_pwr);

void tmio_core_mmc_clk_div(void __iomem *cnf, int shift, int state)
{
	sd_config_write8(cnf, shift, CNF_SD_CLK_MODE, state ? 1 : 0);
}
EXPORT_SYMBOL(tmio_core_mmc_clk_div);

