/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/cpumask.h>

#include <asm/sn/addrs.h>
#include <asm/sn/intr.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/nodepda.h>

#include "msi.h"

struct sn_msi_info {
	u64 pci_addr;
	struct sn_irq_info *sn_irq_info;
};

static struct sn_msi_info *sn_msi_info;

static void
sn_msi_teardown(unsigned int vector)
{
	nasid_t nasid;
	int widget;
	struct pci_dev *pdev;
	struct pcidev_info *sn_pdev;
	struct sn_irq_info *sn_irq_info;
	struct pcibus_bussoft *bussoft;
	struct sn_pcibus_provider *provider;

	sn_irq_info = sn_msi_info[vector].sn_irq_info;
	if (sn_irq_info == NULL || sn_irq_info->irq_int_bit >= 0)
		return;

	sn_pdev = (struct pcidev_info *)sn_irq_info->irq_pciioinfo;
	pdev = sn_pdev->pdi_linux_pcidev;
	provider = SN_PCIDEV_BUSPROVIDER(pdev);

	(*provider->dma_unmap)(pdev,
			       sn_msi_info[vector].pci_addr,
			       PCI_DMA_FROMDEVICE);
	sn_msi_info[vector].pci_addr = 0;

	bussoft = SN_PCIDEV_BUSSOFT(pdev);
	nasid = NASID_GET(bussoft->bs_base);
	widget = (nasid & 1) ?
			TIO_SWIN_WIDGETNUM(bussoft->bs_base) :
			SWIN_WIDGETNUM(bussoft->bs_base);

	sn_intr_free(nasid, widget, sn_irq_info);
	sn_msi_info[vector].sn_irq_info = NULL;

	return;
}

int
sn_msi_setup(struct pci_dev *pdev, unsigned int vector,
	     u32 *addr_hi, u32 *addr_lo, u32 *data)
{
	int widget;
	int status;
	nasid_t nasid;
	u64 bus_addr;
	struct sn_irq_info *sn_irq_info;
	struct pcibus_bussoft *bussoft = SN_PCIDEV_BUSSOFT(pdev);
	struct sn_pcibus_provider *provider = SN_PCIDEV_BUSPROVIDER(pdev);

	if (bussoft == NULL)
		return -EINVAL;

	if (provider == NULL || provider->dma_map_consistent == NULL)
		return -EINVAL;

	/*
	 * Set up the vector plumbing.  Let the prom (via sn_intr_alloc)
	 * decide which cpu to direct this msi at by default.
	 */

	nasid = NASID_GET(bussoft->bs_base);
	widget = (nasid & 1) ?
			TIO_SWIN_WIDGETNUM(bussoft->bs_base) :
			SWIN_WIDGETNUM(bussoft->bs_base);

	sn_irq_info = kzalloc(sizeof(struct sn_irq_info), GFP_KERNEL);
	if (! sn_irq_info)
		return -ENOMEM;

	status = sn_intr_alloc(nasid, widget, sn_irq_info, vector, -1, -1);
	if (status) {
		kfree(sn_irq_info);
		return -ENOMEM;
	}

	sn_irq_info->irq_int_bit = -1;		/* mark this as an MSI irq */
	sn_irq_fixup(pdev, sn_irq_info);

	/* Prom probably should fill these in, but doesn't ... */
	sn_irq_info->irq_bridge_type = bussoft->bs_asic_type;
	sn_irq_info->irq_bridge = (void *)bussoft->bs_base;

	/*
	 * Map the xio address into bus space
	 */
	bus_addr = (*provider->dma_map_consistent)(pdev,
					sn_irq_info->irq_xtalkaddr,
					sizeof(sn_irq_info->irq_xtalkaddr),
					SN_DMA_MSI|SN_DMA_ADDR_XIO);
	if (! bus_addr) {
		sn_intr_free(nasid, widget, sn_irq_info);
		kfree(sn_irq_info);
		return -ENOMEM;
	}

	sn_msi_info[vector].sn_irq_info = sn_irq_info;
	sn_msi_info[vector].pci_addr = bus_addr;

	*addr_hi = (u32)(bus_addr >> 32);
	*addr_lo = (u32)(bus_addr & 0x00000000ffffffff);

	/*
	 * In the SN platform, bit 16 is a "send vector" bit which
	 * must be present in order to move the vector through the system.
	 */
	*data = 0x100 + (unsigned int)vector;

#ifdef CONFIG_SMP
	set_irq_affinity_info((vector & 0xff), sn_irq_info->irq_cpuid, 0);
#endif

	return 0;
}

static void
sn_msi_target(unsigned int vector, unsigned int cpu,
	      u32 *addr_hi, u32 *addr_lo)
{
	int slice;
	nasid_t nasid;
	u64 bus_addr;
	struct pci_dev *pdev;
	struct pcidev_info *sn_pdev;
	struct sn_irq_info *sn_irq_info;
	struct sn_irq_info *new_irq_info;
	struct sn_pcibus_provider *provider;

	sn_irq_info = sn_msi_info[vector].sn_irq_info;
	if (sn_irq_info == NULL || sn_irq_info->irq_int_bit >= 0)
		return;

	/*
	 * Release XIO resources for the old MSI PCI address
	 */

        sn_pdev = (struct pcidev_info *)sn_irq_info->irq_pciioinfo;
	pdev = sn_pdev->pdi_linux_pcidev;
	provider = SN_PCIDEV_BUSPROVIDER(pdev);

	bus_addr = (u64)(*addr_hi) << 32 | (u64)(*addr_lo);
	(*provider->dma_unmap)(pdev, bus_addr, PCI_DMA_FROMDEVICE);
	sn_msi_info[vector].pci_addr = 0;

	nasid = cpuid_to_nasid(cpu);
	slice = cpuid_to_slice(cpu);

	new_irq_info = sn_retarget_vector(sn_irq_info, nasid, slice);
	sn_msi_info[vector].sn_irq_info = new_irq_info;
	if (new_irq_info == NULL)
		return;

	/*
	 * Map the xio address into bus space
	 */

	bus_addr = (*provider->dma_map_consistent)(pdev,
					new_irq_info->irq_xtalkaddr,
					sizeof(new_irq_info->irq_xtalkaddr),
					SN_DMA_MSI|SN_DMA_ADDR_XIO);

	sn_msi_info[vector].pci_addr = bus_addr;
	*addr_hi = (u32)(bus_addr >> 32);
	*addr_lo = (u32)(bus_addr & 0x00000000ffffffff);
}

struct msi_ops sn_msi_ops = {
	.setup = sn_msi_setup,
	.teardown = sn_msi_teardown,
#ifdef CONFIG_SMP
	.target = sn_msi_target,
#endif
};

int
sn_msi_init(void)
{
	sn_msi_info =
		kzalloc(sizeof(struct sn_msi_info) * NR_VECTORS, GFP_KERNEL);
	if (! sn_msi_info)
		return -ENOMEM;

	msi_register(&sn_msi_ops);
	return 0;
}
