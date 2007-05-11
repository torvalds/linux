/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "mlx4.h"

int mlx4_reset(struct mlx4_dev *dev)
{
	void __iomem *reset;
	u32 *hca_header = NULL;
	int pcie_cap;
	u16 devctl;
	u16 linkctl;
	u16 vendor;
	unsigned long end;
	u32 sem;
	int i;
	int err = 0;

#define MLX4_RESET_BASE		0xf0000
#define MLX4_RESET_SIZE		  0x400
#define MLX4_SEM_OFFSET		  0x3fc
#define MLX4_RESET_OFFSET	   0x10
#define MLX4_RESET_VALUE	swab32(1)

#define MLX4_SEM_TIMEOUT_JIFFIES	(10 * HZ)
#define MLX4_RESET_TIMEOUT_JIFFIES	(2 * HZ)

	/*
	 * Reset the chip.  This is somewhat ugly because we have to
	 * save off the PCI header before reset and then restore it
	 * after the chip reboots.  We skip config space offsets 22
	 * and 23 since those have a special meaning.
	 */

	/* Do we need to save off the full 4K PCI Express header?? */
	hca_header = kmalloc(256, GFP_KERNEL);
	if (!hca_header) {
		err = -ENOMEM;
		mlx4_err(dev, "Couldn't allocate memory to save HCA "
			  "PCI header, aborting.\n");
		goto out;
	}

	pcie_cap = pci_find_capability(dev->pdev, PCI_CAP_ID_EXP);

	for (i = 0; i < 64; ++i) {
		if (i == 22 || i == 23)
			continue;
		if (pci_read_config_dword(dev->pdev, i * 4, hca_header + i)) {
			err = -ENODEV;
			mlx4_err(dev, "Couldn't save HCA "
				  "PCI header, aborting.\n");
			goto out;
		}
	}

	reset = ioremap(pci_resource_start(dev->pdev, 0) + MLX4_RESET_BASE,
			MLX4_RESET_SIZE);
	if (!reset) {
		err = -ENOMEM;
		mlx4_err(dev, "Couldn't map HCA reset register, aborting.\n");
		goto out;
	}

	/* grab HW semaphore to lock out flash updates */
	end = jiffies + MLX4_SEM_TIMEOUT_JIFFIES;
	do {
		sem = readl(reset + MLX4_SEM_OFFSET);
		if (!sem)
			break;

		msleep(1);
	} while (time_before(jiffies, end));

	if (sem) {
		mlx4_err(dev, "Failed to obtain HW semaphore, aborting\n");
		err = -EAGAIN;
		iounmap(reset);
		goto out;
	}

	/* actually hit reset */
	writel(MLX4_RESET_VALUE, reset + MLX4_RESET_OFFSET);
	iounmap(reset);

	end = jiffies + MLX4_RESET_TIMEOUT_JIFFIES;
	do {
		if (!pci_read_config_word(dev->pdev, PCI_VENDOR_ID, &vendor) &&
		    vendor != 0xffff)
			break;

		msleep(1);
	} while (time_before(jiffies, end));

	if (vendor == 0xffff) {
		err = -ENODEV;
		mlx4_err(dev, "PCI device did not come back after reset, "
			  "aborting.\n");
		goto out;
	}

	/* Now restore the PCI headers */
	if (pcie_cap) {
		devctl = hca_header[(pcie_cap + PCI_EXP_DEVCTL) / 4];
		if (pci_write_config_word(dev->pdev, pcie_cap + PCI_EXP_DEVCTL,
					   devctl)) {
			err = -ENODEV;
			mlx4_err(dev, "Couldn't restore HCA PCI Express "
				 "Device Control register, aborting.\n");
			goto out;
		}
		linkctl = hca_header[(pcie_cap + PCI_EXP_LNKCTL) / 4];
		if (pci_write_config_word(dev->pdev, pcie_cap + PCI_EXP_LNKCTL,
					   linkctl)) {
			err = -ENODEV;
			mlx4_err(dev, "Couldn't restore HCA PCI Express "
				 "Link control register, aborting.\n");
			goto out;
		}
	}

	for (i = 0; i < 16; ++i) {
		if (i * 4 == PCI_COMMAND)
			continue;

		if (pci_write_config_dword(dev->pdev, i * 4, hca_header[i])) {
			err = -ENODEV;
			mlx4_err(dev, "Couldn't restore HCA reg %x, "
				  "aborting.\n", i);
			goto out;
		}
	}

	if (pci_write_config_dword(dev->pdev, PCI_COMMAND,
				   hca_header[PCI_COMMAND / 4])) {
		err = -ENODEV;
		mlx4_err(dev, "Couldn't restore HCA COMMAND, "
			  "aborting.\n");
		goto out;
	}

out:
	kfree(hca_header);

	return err;
}
