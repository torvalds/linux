/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Christoph Hellwig (hch@lst.de)
 * Copyright (C) 1999, 2000, 04 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <asm/sn/addrs.h>
#include <asm/sn/types.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/hub.h>
#include <asm/sn/ioc3.h>
#include <asm/pci/bridge.h>

#ifdef CONFIG_NUMA
int pcibus_to_node(struct pci_bus *bus)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);

	return bc->nasid;
}
EXPORT_SYMBOL(pcibus_to_node);
#endif /* CONFIG_NUMA */

static void ip29_fixup_phy(struct pci_dev *dev)
{
	int nasid = pcibus_to_node(dev->bus);
	u32 sid;

	if (nasid != 1)
		return; /* only needed on second module */

	/* enable ethernet PHY on IP29 systemboard */
	pci_read_config_dword(dev, PCI_SUBSYSTEM_VENDOR_ID, &sid);
	if (sid == (PCI_VENDOR_ID_SGI | (IOC3_SUBSYS_IP29_SYSBOARD) << 16))
		REMOTE_HUB_S(nasid, MD_LED0, 0x09);
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC3,
			ip29_fixup_phy);
