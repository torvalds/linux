#include <linux/pci.h>
#include <linux/usb.h>
#include <linux/usb/ehci_def.h>
#include <linux/usb/hcd.h>
#include <asm/xen/hypercall.h>
#include <xen/interface/physdev.h>
#include <xen/xen.h>

static int xen_dbgp_op(struct usb_hcd *hcd, int op)
{
#ifdef CONFIG_PCI
	const struct device *ctrlr = hcd_to_bus(hcd)->controller;
#endif
	struct physdev_dbgp_op dbgp;

	if (!xen_initial_domain())
		return 0;

	dbgp.op = op;

#ifdef CONFIG_PCI
	if (ctrlr->bus == &pci_bus_type) {
		const struct pci_dev *pdev = to_pci_dev(ctrlr);

		dbgp.u.pci.seg = pci_domain_nr(pdev->bus);
		dbgp.u.pci.bus = pdev->bus->number;
		dbgp.u.pci.devfn = pdev->devfn;
		dbgp.bus = PHYSDEVOP_DBGP_BUS_PCI;
	} else
#endif
		dbgp.bus = PHYSDEVOP_DBGP_BUS_UNKNOWN;

	return HYPERVISOR_physdev_op(PHYSDEVOP_dbgp_op, &dbgp);
}

int xen_dbgp_reset_prep(struct usb_hcd *hcd)
{
	return xen_dbgp_op(hcd, PHYSDEVOP_DBGP_RESET_PREPARE);
}

int xen_dbgp_external_startup(struct usb_hcd *hcd)
{
	return xen_dbgp_op(hcd, PHYSDEVOP_DBGP_RESET_DONE);
}

#ifndef CONFIG_EARLY_PRINTK_DBGP
#include <linux/export.h>
EXPORT_SYMBOL_GPL(xen_dbgp_reset_prep);
EXPORT_SYMBOL_GPL(xen_dbgp_external_startup);
#endif
