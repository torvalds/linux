/*
 * FPGA Framework Transport
 *
 *  Copyright (C) 2013 Altera Corporation
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
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/fpga.h>

static u32 fpga_reg_readl(struct fpga_mgr_transport *transp, u32 reg_offset)
{
	return readl(transp->fpga_base_addr + reg_offset);
}

static void fpga_reg_writel(struct fpga_mgr_transport *transp, u32 reg_offset,
			    u32 value)
{
	writel(value, transp->fpga_base_addr + reg_offset);
}

static u32 fpga_reg_raw_readl(struct fpga_mgr_transport *transp, u32 reg_offset)
{
	return __raw_readl(transp->fpga_base_addr + reg_offset);
}

static void fpga_reg_raw_writel(struct fpga_mgr_transport *transp, u32 reg_offset,
			    u32 value)
{
	__raw_writel(value, transp->fpga_base_addr + reg_offset);
}

static u32 fpga_data_readl(struct fpga_mgr_transport *transp)
{
	return readl(transp->fpga_data_addr);
}

static void fpga_data_writel(struct fpga_mgr_transport *transp, u32 value)
{
	writel(value, transp->fpga_data_addr);
}

int fpga_attach_mmio_transport(struct fpga_manager *mgr)
{
	struct device_node *np = mgr->np;
	struct fpga_mgr_transport *transp;
	static void __iomem *fpga_base_addr;
	static void __iomem *fpga_data_addr;
	int ret = 0;

	fpga_base_addr = of_iomap(np, 0);
	if (!fpga_base_addr) {
		dev_err(mgr->parent,
			"Need to specify fpga manager register address.\n");
		return -EINVAL;
	}

	fpga_data_addr = of_iomap(np, 1);
	if (!fpga_data_addr) {
		dev_err(mgr->parent,
			"Need to specify fpga manager data address.\n");
		ret = -ENOMEM;
		goto err_baddr;
	}

	transp = kzalloc(sizeof(struct fpga_mgr_transport), GFP_KERNEL);
	if (!transp) {
		dev_err(mgr->parent, "Could not allocate transport\n");
		ret = -ENOMEM;
		goto err_daddr;
	}

	transp->fpga_base_addr = fpga_base_addr;
	transp->fpga_data_addr = fpga_data_addr;

	transp->reg_readl = fpga_reg_readl;
	transp->reg_writel = fpga_reg_writel;
	transp->reg_raw_readl = fpga_reg_raw_readl;
	transp->reg_raw_writel = fpga_reg_raw_writel;
	transp->data_writel = fpga_data_writel;
	transp->data_readl = fpga_data_readl;
	strlcpy(transp->type, "mmio", sizeof(transp->type));

	transp->mgr = mgr;
	mgr->transp = transp;

	return 0;

err_daddr:
	iounmap(fpga_data_addr);
err_baddr:
	iounmap(fpga_base_addr);
	return ret;
}
EXPORT_SYMBOL(fpga_attach_mmio_transport);

void fpga_detach_mmio_transport(struct fpga_manager *mgr)
{
	struct fpga_mgr_transport *transp = mgr->transp;

	iounmap(transp->fpga_base_addr);
	iounmap(transp->fpga_data_addr);
	kfree(transp);
}
EXPORT_SYMBOL(fpga_detach_mmio_transport);
