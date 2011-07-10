/*
 * Copyright(c) 2009 Ian Molton <spyro@f2s.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/mfd/tmio.h>

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

