/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/types.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/cpumask.h>
#include <linux/msi.h>

#include <asm/sn/addrs.h>
#include <asm/sn/intr.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/nodepda.h>

struct sn_msi_info {
	u64 pci_addr;
	struct sn_irq_info *sn_irq_info;
};

static struct sn_msi_info sn_msi_info[NR_IRQS];

static struct irq_chip sn_msi_chip;

void sn_teardown_msi_irq(unsigned int irq)
{
	nasid_t nasid;
	int widget;
	struct pci_dev *pdev;
	struct pcidev_info *sn_pdev;
	struct sn_irq_info *sn_irq_info;
	struct pcibus_bussoft *bussoft;
	struct sn_pcibus_provider *provider;

	sn_irq_info = sn_msi_info[irq].sn_irq_info;
	if (sn_irq_info == NULL || sn_irq_info->irq_int_bit >= 0)
		return;

	sn_pdev = (struct pcidev_info *)sn_irq_info->irq_pciioinfo;
	pdev = sn_pdev->pdi_linux_pcidev;
	provider = SN_PCIDEV_BUSPROVIDER(pdev);

	(*provider->dma_unmap)(pdev,
			       sn_msi_info[irq].pci_addr,
			       PCI_DMA_FROMDEVICE);
	sn_msi_info[irq].pci_addr = 0;

	bussoft = SN_PCIDEV_BUSSOFT(pdev);
	nasid = NASID_GET(bussoft->bs_base);
	widget = (nasid & 1) ?
			TIO_SWIN_WIDGETNUM(bussoft->bs_base) :
			SWIN_WIDGETNUM(bussoft->bs_base);

	sn_intr_free(nasid, widget, sn_irq_info);
	sn_msi_info[irq].sn_irq_info = NULL;

	destroy_irq(irq);
}

int sn_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *entry)
{
	struct msi_msg msg;
	int widget;
	int status;
	nasid_t nasid;
	u64 bus_addr;
	struct sn_irq_info *sn_irq_info;
	struct pcibus_bussoft *bussoft = SN_PCIDEV_BUSSOFT(pdev);
	struct sn_pcibus_provider *provider = SN_PCIDEV_BUSPROVIDER(pdev);
	int irq;

	if (!entry->msi_attrib.is_64)
		return -EINVAL;

	if (bussoft == NULL)
		return -EINVAL;

	if (provider == NULL || provider->dma_map_consistent == NULL)
		return -EINVAL;

	irq = create_irq();
	if (irq < 0)
		return irq;

	/*
	 * Set up the vector plumbing.  Let the prom (via sn_intr_alloc)
	 * decide which cpu to direct this msi at by default.
	 */

	nasid = NASID_GET(bussoft->bs_base);
	widget = (nasid & 1) ?
			TIO_SWIN_WIDGETNUM(bussoft->bs_base) :
			SWIN_WIDGETNUM(bussoft->bs_base);

	sn_irq_info = kzalloc(sizeof(struct sn_irq_info), GFP_KERNEL);
	if (! sn_irq_info) {
		destroy_irq(irq);
		return -ENOMEM;
	}

	status = sn_intr_alloc(nasid, widget, sn_irq_info, irq, -1, -1);
	if (status) {
		kfree(sn_irq_info);
		destroy_irq(irq);
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
		destroy_irq(irq);
		return -ENOMEM;
	}

	sn_msi_info[irq].sn_irq_info = sn_irq_info;
	sn_msi_info[irq].pci_addr = bus_addr;

	msg.address_hi = (u32)(bus_addr >> 32);
	msg.address_lo = (u32)(bus_addr & 0x00000000ffffffff);

	/*
	 * In the SN platform, bit 16 is a "send vector" bit which
	 * must be present in order to move the vector through the system.
	 */
	msg.data = 0x100 + irq;

	set_irq_msi(irq, entry);
	write_msi_msg(irq, &msg);
	set_irq_chip_and_handler(irq, &sn_msi_chip, handle_edge_irq);

	return 0;
}

#ifdef CONFIG_SMP
static void sn_set_msi_irq_affinity(unsigned int irq,
				    const struct cpumask *cpu_mask)
{
	struct msi_msg msg;
	int slice;
	nasid_t nasid;
	u64 bus_addr;
	struct pci_dev *pdev;
	struct pcidev_info *sn_pdev;
	struct sn_irq_info *sn_irq_info;
	struct sn_irq_info *new_irq_info;
	struct sn_pcibus_provider *provider;
	unsigned int cpu;

	cpu = cpumask_first(cpu_mask);
	sn_irq_info = sn_msi_info[irq].sn_irq_info;
	if (sn_irq_info == NULL || sn_irq_info->irq_int_bit >= 0)
		return;

	/*
	 * Release XIO resources for the old MSI PCI address
	 */

	read_msi_msg(irq, &msg);
        sn_pdev = (struct pcidev_info *)sn_irq_info->irq_pciioinfo;
	pdev = sn_pdev->pdi_linux_pcidev;
	provider = SN_PCIDEV_BUSPROVIDER(pdev);

	bus_addr = (u64)(msg.address_hi) << 32 | (u64)(msg.address_lo);
	(*provider->dma_unmap)(pdev, bus_addr, PCI_DMA_FROMDEVICE);
	sn_msi_info[irq].pci_addr = 0;

	nasid = cpuid_to_nasid(cpu);
	slice = cpuid_to_slice(cpu);

	new_irq_info = sn_retarget_vector(sn_irq_info, nasid, slice);
	sn_msi_info[irq].sn_irq_info = new_irq_info;
	if (new_irq_info == NULL)
		return;

	/*
	 * Map the xio address into bus space
	 */

	bus_addr = (*provider->dma_map_consistent)(pdev,
					new_irq_info->irq_xtalkaddr,
					sizeof(new_irq_info->irq_xtalkaddr),
					SN_DMA_MSI|SN_DMA_ADDR_XIO);

	sn_msi_info[irq].pci_addr = bus_addr;
	msg.address_hi = (u32)(bus_addr >> 32);
	msg.address_lo = (u32)(bus_addr & 0x00000000ffffffff);

	write_msi_msg(irq, &msg);
	cpumask_copy(irq_desc[irq].affinity, cpu_mask);
}
#endif /* CONFIG_SMP */

static void sn_ack_msi_irq(unsigned int irq)
{
	move_native_irq(irq);
	ia64_eoi();
}

static int sn_msi_retrigger_irq(unsigned int irq)
{
	unsigned int vector = irq;
	ia64_resend_irq(vector);

	return 1;
}

static struct irq_chip sn_msi_chip = {
	.name		= "PCI-MSI",
	.mask		= mask_msi_irq,
	.unmask		= unmask_msi_irq,
	.ack		= sn_ack_msi_irq,
#ifdef CONFIG_SMP
	.set_affinity	= sn_set_msi_irq_affinity,
#endif
	.retrigger	= sn_msi_retrigger_irq,
};
