/*
 *  Copyright (C) 2012 Altera Corporation <www.altera.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>

#define SOCFPGA_OSC1_CLK	10000000
#define SOCFPGA_MPU_CLK		800000000
#define SOCFPGA_MAIN_QSPI_CLK		432000000
#define SOCFPGA_MAIN_NAND_SDMMC_CLK	250000000
#define SOCFPGA_S2F_USR_CLK		125000000

void __init socfpga_init_clocks(void)
{
	struct clk *clk;

	clk = clk_register_fixed_rate(NULL, "osc1_clk", NULL, CLK_IS_ROOT, SOCFPGA_OSC1_CLK);
	clk_register_clkdev(clk, "osc1_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "mpu_clk", NULL, CLK_IS_ROOT, SOCFPGA_MPU_CLK);
	clk_register_clkdev(clk, "mpu_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "main_clk", NULL, CLK_IS_ROOT, SOCFPGA_MPU_CLK/2);
	clk_register_clkdev(clk, "main_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "dbg_base_clk", NULL, CLK_IS_ROOT, SOCFPGA_MPU_CLK/2);
	clk_register_clkdev(clk, "dbg_base_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "main_qspi_clk", NULL, CLK_IS_ROOT, SOCFPGA_MAIN_QSPI_CLK);
	clk_register_clkdev(clk, "main_qspi_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "main_nand_sdmmc_clk", NULL, CLK_IS_ROOT, SOCFPGA_MAIN_NAND_SDMMC_CLK);
	clk_register_clkdev(clk, "main_nand_sdmmc_clk", NULL);

	clk = clk_register_fixed_rate(NULL, "s2f_usr_clk", NULL, CLK_IS_ROOT, SOCFPGA_S2F_USR_CLK);
	clk_register_clkdev(clk, "s2f_usr_clk", NULL);
}
