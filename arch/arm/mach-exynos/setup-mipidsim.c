/* linux/arch/arm/mach-exynos/setup-mipidsim.c
 *
 * Copyright (c) 2011 Samsung Electronics
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * ERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * A 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include <mach/map.h>
#include <mach/regs-clock.h>

#include <plat/dsim.h>
#include <plat/clock.h>
#include <plat/regs-mipidsim.h>

#define S5P_MIPI_M_RESETN 4

static int s5p_dsim_enable_d_phy(struct mipi_dsim_device *dsim,
		unsigned int enable)
{
	unsigned int reg;
#if defined(CONFIG_ARCH_EXYNOS5)
	reg = readl(S5P_MIPI_DPHY_CONTROL(1)) & ~(1 << 0);
	reg |= (enable << 0);
	writel(reg, S5P_MIPI_DPHY_CONTROL(1));
#else
	reg = readl(S5P_MIPI_DPHY_CONTROL(0)) & ~(1 << 0);
	reg |= (enable << 0);
	writel(reg, S5P_MIPI_DPHY_CONTROL(0));
#endif
	return 0;
}

static int s5p_dsim_enable_dsi_master(struct mipi_dsim_device *dsim,
	unsigned int enable)
{
	unsigned int reg;
#if defined(CONFIG_ARCH_EXYNOS5)
	reg = readl(S5P_MIPI_DPHY_CONTROL(1)) & ~(1 << 2);
	reg |= (enable << 2);
	writel(reg, S5P_MIPI_DPHY_CONTROL(1));
#else
	reg = readl(S5P_MIPI_DPHY_CONTROL(0)) & ~(1 << 2);
	reg |= (enable << 2);
	writel(reg, S5P_MIPI_DPHY_CONTROL(0));
#endif
	return 0;
}

int s5p_dsim_part_reset(struct mipi_dsim_device *dsim)
{
#if defined(CONFIG_ARCH_EXYNOS5)
	if (dsim->id == 0)
		writel(S5P_MIPI_M_RESETN, S5P_MIPI_DPHY_CONTROL(1));
#else
	if (dsim->id == 0)
		writel(S5P_MIPI_M_RESETN, S5P_MIPI_DPHY_CONTROL(0));
#endif
	return 0;
}

int s5p_dsim_init_d_phy(struct mipi_dsim_device *dsim, unsigned int enable)
{
	/**
	 * DPHY and aster block must be enabled at the system initialization
	 * step before data access from/to DPHY begins.
	 */
	s5p_dsim_enable_d_phy(dsim, enable);

	s5p_dsim_enable_dsi_master(dsim, enable);
	return 0;
}
