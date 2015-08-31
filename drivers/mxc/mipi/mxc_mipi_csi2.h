/*
 * Copyright (C) 2011-2014 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __MXC_MIPI_CSI2_H__
#define __MXC_MIPI_CSI2_H__

#ifdef DEBUG
#define mipi_dbg(fmt, ...)	\
	printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define mipi_dbg(fmt, ...)
#endif

/* driver private data */
struct mipi_csi2_info {
	bool		mipi_en;
	int		ipu_id;
	unsigned int	csi_id;
	unsigned int	v_channel;
	unsigned int	lanes;
	unsigned int	datatype;
	struct clk	*cfg_clk;
	struct clk	*dphy_clk;
	struct clk	*pixel_clk;
	void __iomem	*mipi_csi2_base;
	struct platform_device	*pdev;

	struct mutex mutex_lock;
};

#endif
