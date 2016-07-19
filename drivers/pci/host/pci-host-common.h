/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#ifndef _PCI_HOST_COMMON_H
#define _PCI_HOST_COMMON_H

#include <linux/kernel.h>
#include <linux/platform_device.h>

struct gen_pci_cfg_bus_ops {
	u32 bus_shift;
	struct pci_ops ops;
};

struct gen_pci_cfg_windows {
	struct resource				res;
	struct resource				*bus_range;
	void __iomem				**win;

	struct gen_pci_cfg_bus_ops		*ops;
};

struct gen_pci {
	struct pci_host_bridge			host;
	struct gen_pci_cfg_windows		cfg;
	struct list_head			resources;
};

int pci_host_common_probe(struct platform_device *pdev,
			  struct gen_pci *pci);

#endif /* _PCI_HOST_COMMON_H */
