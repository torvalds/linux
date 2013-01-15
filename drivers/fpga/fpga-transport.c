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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fpga.h>

int fpga_mgr_attach_transport(struct fpga_manager *mgr)
{
	struct device_node *np = mgr->np;
	const char *string;
	int ret;

	ret = of_property_read_string(np, "transport", &string);
	if (ret) {
		dev_err(mgr->parent,
			"Transport not specified for fpga manager %s\n",
			mgr->name);
		return -ENODEV;
	}

	if (strcmp(string, "mmio") == 0)
		ret = fpga_attach_mmio_transport(mgr);
	else {
		dev_err(mgr->parent,
			"Invalid transport specified for fpga manager %s (%s)\n",
			mgr->name, string);
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL(fpga_mgr_attach_transport);

void fpga_mgr_detach_transport(struct fpga_manager *mgr)
{
	struct fpga_mgr_transport *transp = mgr->transp;
	const char *type = transp->type;

	if (!strncmp(type, "mmio", strlen(type)))
		fpga_detach_mmio_transport(mgr);
	else
		BUG();
}
EXPORT_SYMBOL(fpga_mgr_detach_transport);
