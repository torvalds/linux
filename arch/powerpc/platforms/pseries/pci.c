// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2001 Dave Engebretsen, IBM Corporation
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * pSeries specific routines for PCI.
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>

#include <asm/eeh.h>
#include <asm/pci-bridge.h>
#include <asm/ppc-pci.h>
#include <asm/pci.h>
#include "pseries.h"

#if 0
void pcibios_name_device(struct pci_dev *dev)
{
	struct device_node *dn;

	/*
	 * Add IBM loc code (slot) as a prefix to the device names for service
	 */
	dn = pci_device_to_OF_node(dev);
	if (dn) {
		const char *loc_code = of_get_property(dn, "ibm,loc-code",
				NULL);
		if (loc_code) {
			int loc_len = strlen(loc_code);
			if (loc_len < sizeof(dev->dev.name)) {
				memmove(dev->dev.name+loc_len+1, dev->dev.name,
					sizeof(dev->dev.name)-loc_len-1);
				memcpy(dev->dev.name, loc_code, loc_len);
				dev->dev.name[loc_len] = ' ';
				dev->dev.name[sizeof(dev->dev.name)-1] = '\0';
			}
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, pcibios_name_device);
#endif

#ifdef CONFIG_PCI_IOV
#define MAX_VFS_FOR_MAP_PE 256
struct pe_map_bar_entry {
	__be64     bar;       /* Input:  Virtual Function BAR */
	__be16     rid;       /* Input:  Virtual Function Router ID */
	__be16     pe_num;    /* Output: Virtual Function PE Number */
	__be32     reserved;  /* Reserved Space */
};

static int pseries_send_map_pe(struct pci_dev *pdev, u16 num_vfs,
			       struct pe_map_bar_entry *vf_pe_array)
{
	struct pci_dn *pdn;
	int rc;
	unsigned long buid, addr;
	int ibm_map_pes = rtas_token("ibm,open-sriov-map-pe-number");

	if (ibm_map_pes == RTAS_UNKNOWN_SERVICE)
		return -EINVAL;

	pdn = pci_get_pdn(pdev);
	addr = rtas_config_addr(pdn->busno, pdn->devfn, 0);
	buid = pdn->phb->buid;
	spin_lock(&rtas_data_buf_lock);
	memcpy(rtas_data_buf, vf_pe_array,
	       RTAS_DATA_BUF_SIZE);
	rc = rtas_call(ibm_map_pes, 5, 1, NULL, addr,
		       BUID_HI(buid), BUID_LO(buid),
		       rtas_data_buf,
		       num_vfs * sizeof(struct pe_map_bar_entry));
	memcpy(vf_pe_array, rtas_data_buf, RTAS_DATA_BUF_SIZE);
	spin_unlock(&rtas_data_buf_lock);

	if (rc)
		dev_err(&pdev->dev,
			"%s: Failed to associate pes PE#%lx, rc=%x\n",
			__func__,  addr, rc);

	return rc;
}

static void pseries_set_pe_num(struct pci_dev *pdev, u16 vf_index, __be16 pe_num)
{
	struct pci_dn *pdn;

	pdn = pci_get_pdn(pdev);
	pdn->pe_num_map[vf_index] = be16_to_cpu(pe_num);
	dev_dbg(&pdev->dev, "VF %04x:%02x:%02x.%x associated with PE#%x\n",
		pci_domain_nr(pdev->bus),
		pdev->bus->number,
		PCI_SLOT(pci_iov_virtfn_devfn(pdev, vf_index)),
		PCI_FUNC(pci_iov_virtfn_devfn(pdev, vf_index)),
		pdn->pe_num_map[vf_index]);
}

static int pseries_associate_pes(struct pci_dev *pdev, u16 num_vfs)
{
	struct pci_dn *pdn;
	int i, rc, vf_index;
	struct pe_map_bar_entry *vf_pe_array;
	struct resource *res;
	u64 size;

	vf_pe_array = kzalloc(RTAS_DATA_BUF_SIZE, GFP_KERNEL);
	if (!vf_pe_array)
		return -ENOMEM;

	pdn = pci_get_pdn(pdev);
	/* create firmware structure to associate pes */
	for (vf_index = 0; vf_index < num_vfs; vf_index++) {
		pdn->pe_num_map[vf_index] = IODA_INVALID_PE;
		for (i = 0; i < PCI_SRIOV_NUM_BARS; i++) {
			res = &pdev->resource[i + PCI_IOV_RESOURCES];
			if (!res->parent)
				continue;
			size = pcibios_iov_resource_alignment(pdev, i +
					PCI_IOV_RESOURCES);
			vf_pe_array[vf_index].bar =
				cpu_to_be64(res->start + size * vf_index);
			vf_pe_array[vf_index].rid =
				cpu_to_be16((pci_iov_virtfn_bus(pdev, vf_index)
					    << 8) | pci_iov_virtfn_devfn(pdev,
					    vf_index));
			vf_pe_array[vf_index].pe_num =
				cpu_to_be16(IODA_INVALID_PE);
		}
	}

	rc = pseries_send_map_pe(pdev, num_vfs, vf_pe_array);
	/* Only zero is success */
	if (!rc)
		for (vf_index = 0; vf_index < num_vfs; vf_index++)
			pseries_set_pe_num(pdev, vf_index,
					   vf_pe_array[vf_index].pe_num);

	kfree(vf_pe_array);
	return rc;
}

static int pseries_pci_sriov_enable(struct pci_dev *pdev, u16 num_vfs)
{
	struct pci_dn         *pdn;
	int                    rc;
	const int *max_vfs;
	int max_config_vfs;
	struct device_node *dn = pci_device_to_OF_node(pdev);

	max_vfs = of_get_property(dn, "ibm,number-of-configurable-vfs", NULL);

	if (!max_vfs)
		return -EINVAL;

	/* First integer stores max config */
	max_config_vfs = of_read_number(&max_vfs[0], 1);
	if (max_config_vfs < num_vfs && num_vfs > MAX_VFS_FOR_MAP_PE) {
		dev_err(&pdev->dev,
			"Num VFs %x > %x Configurable VFs\n",
			num_vfs, (num_vfs > MAX_VFS_FOR_MAP_PE) ?
			MAX_VFS_FOR_MAP_PE : max_config_vfs);
		return -EINVAL;
	}

	pdn = pci_get_pdn(pdev);
	pdn->pe_num_map = kmalloc_array(num_vfs,
					sizeof(*pdn->pe_num_map),
					GFP_KERNEL);
	if (!pdn->pe_num_map)
		return -ENOMEM;

	rc = pseries_associate_pes(pdev, num_vfs);

	/* Anything other than zero is failure */
	if (rc) {
		dev_err(&pdev->dev, "Failure to enable sriov: %x\n", rc);
		kfree(pdn->pe_num_map);
	} else {
		pci_vf_drivers_autoprobe(pdev, false);
	}

	return rc;
}

static int pseries_pcibios_sriov_enable(struct pci_dev *pdev, u16 num_vfs)
{
	/* Allocate PCI data */
	add_sriov_vf_pdns(pdev);
	return pseries_pci_sriov_enable(pdev, num_vfs);
}

static int pseries_pcibios_sriov_disable(struct pci_dev *pdev)
{
	struct pci_dn         *pdn;

	pdn = pci_get_pdn(pdev);
	/* Releasing pe_num_map */
	kfree(pdn->pe_num_map);
	/* Release PCI data */
	remove_sriov_vf_pdns(pdev);
	pci_vf_drivers_autoprobe(pdev, true);
	return 0;
}
#endif

static void __init pSeries_request_regions(void)
{
	if (!isa_io_base)
		return;

	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
}

void __init pSeries_final_fixup(void)
{
	pSeries_request_regions();

	eeh_show_enabled();

#ifdef CONFIG_PCI_IOV
	ppc_md.pcibios_sriov_enable = pseries_pcibios_sriov_enable;
	ppc_md.pcibios_sriov_disable = pseries_pcibios_sriov_disable;
#endif
}

/*
 * Assume the winbond 82c105 is the IDE controller on a
 * p610/p615/p630. We should probably be more careful in case
 * someone tries to plug in a similar adapter.
 */
static void fixup_winbond_82c105(struct pci_dev* dev)
{
	int i;
	unsigned int reg;

	if (!machine_is(pseries))
		return;

	printk("Using INTC for W82c105 IDE controller.\n");
	pci_read_config_dword(dev, 0x40, &reg);
	/* Enable LEGIRQ to use INTC instead of ISA interrupts */
	pci_write_config_dword(dev, 0x40, reg | (1<<11));

	for (i = 0; i < DEVICE_COUNT_RESOURCE; ++i) {
		/* zap the 2nd function of the winbond chip */
		if (dev->resource[i].flags & IORESOURCE_IO
		    && dev->bus->number == 0 && dev->devfn == 0x81)
			dev->resource[i].flags &= ~IORESOURCE_IO;
		if (dev->resource[i].start == 0 && dev->resource[i].end) {
			dev->resource[i].flags = 0;
			dev->resource[i].end = 0;
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_WINBOND, PCI_DEVICE_ID_WINBOND_82C105,
			 fixup_winbond_82c105);

static enum pci_bus_speed prop_to_pci_speed(u32 prop)
{
	switch (prop) {
	case 0x01:
		return PCIE_SPEED_2_5GT;
	case 0x02:
		return PCIE_SPEED_5_0GT;
	case 0x04:
		return PCIE_SPEED_8_0GT;
	case 0x08:
		return PCIE_SPEED_16_0GT;
	case 0x10:
		return PCIE_SPEED_32_0GT;
	default:
		pr_debug("Unexpected PCI link speed property value\n");
		return PCI_SPEED_UNKNOWN;
	}
}

int pseries_root_bridge_prepare(struct pci_host_bridge *bridge)
{
	struct device_node *dn, *pdn;
	struct pci_bus *bus;
	u32 pcie_link_speed_stats[2];
	int rc;

	bus = bridge->bus;

	/* Rely on the pcibios_free_controller_deferred() callback. */
	pci_set_host_bridge_release(bridge, pcibios_free_controller_deferred,
					(void *) pci_bus_to_host(bus));

	dn = pcibios_get_phb_of_node(bus);
	if (!dn)
		return 0;

	for (pdn = dn; pdn != NULL; pdn = of_get_next_parent(pdn)) {
		rc = of_property_read_u32_array(pdn,
				"ibm,pcie-link-speed-stats",
				&pcie_link_speed_stats[0], 2);
		if (!rc)
			break;
	}

	of_node_put(pdn);

	if (rc) {
		pr_debug("no ibm,pcie-link-speed-stats property\n");
		return 0;
	}

	bus->max_bus_speed = prop_to_pci_speed(pcie_link_speed_stats[0]);
	bus->cur_bus_speed = prop_to_pci_speed(pcie_link_speed_stats[1]);
	return 0;
}
