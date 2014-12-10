/*
 * Xen PCI - handle PCI (INTx) and MSI infrastructure calls for PV, HVM and
 * initial domain support. We also handle the DSDT _PRT callbacks for GSI's
 * used in HVM and initial domain mode (PV does not parse ACPI, so it has no
 * concept of GSIs). Under PV we hook under the pnbbios API for IRQs and
 * 0xcf8 PCI configuration read/write.
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 *           Konrad Rzeszutek Wilk <konrad.wilk@oracle.com>
 *           Stefano Stabellini <stefano.stabellini@eu.citrix.com>
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/acpi.h>

#include <linux/io.h>
#include <asm/io_apic.h>
#include <asm/pci_x86.h>

#include <asm/xen/hypervisor.h>

#include <xen/features.h>
#include <xen/events.h>
#include <asm/xen/pci.h>
#include <asm/i8259.h>

static int xen_pcifront_enable_irq(struct pci_dev *dev)
{
	int rc;
	int share = 1;
	int pirq;
	u8 gsi;

	rc = pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &gsi);
	if (rc < 0) {
		dev_warn(&dev->dev, "Xen PCI: failed to read interrupt line: %d\n",
			 rc);
		return rc;
	}
	/* In PV DomU the Xen PCI backend puts the PIRQ in the interrupt line.*/
	pirq = gsi;

	if (gsi < nr_legacy_irqs())
		share = 0;

	rc = xen_bind_pirq_gsi_to_irq(gsi, pirq, share, "pcifront");
	if (rc < 0) {
		dev_warn(&dev->dev, "Xen PCI: failed to bind GSI%d (PIRQ%d) to IRQ: %d\n",
			 gsi, pirq, rc);
		return rc;
	}

	dev->irq = rc;
	dev_info(&dev->dev, "Xen PCI mapped GSI%d to IRQ%d\n", gsi, dev->irq);
	return 0;
}

#ifdef CONFIG_ACPI
static int xen_register_pirq(u32 gsi, int gsi_override, int triggering,
			     bool set_pirq)
{
	int rc, pirq = -1, irq = -1;
	struct physdev_map_pirq map_irq;
	int shareable = 0;
	char *name;

	irq = xen_irq_from_gsi(gsi);
	if (irq > 0)
		return irq;

	if (set_pirq)
		pirq = gsi;

	map_irq.domid = DOMID_SELF;
	map_irq.type = MAP_PIRQ_TYPE_GSI;
	map_irq.index = gsi;
	map_irq.pirq = pirq;

	rc = HYPERVISOR_physdev_op(PHYSDEVOP_map_pirq, &map_irq);
	if (rc) {
		printk(KERN_WARNING "xen map irq failed %d\n", rc);
		return -1;
	}

	if (triggering == ACPI_EDGE_SENSITIVE) {
		shareable = 0;
		name = "ioapic-edge";
	} else {
		shareable = 1;
		name = "ioapic-level";
	}

	if (gsi_override >= 0)
		gsi = gsi_override;

	irq = xen_bind_pirq_gsi_to_irq(gsi, map_irq.pirq, shareable, name);
	if (irq < 0)
		goto out;

	printk(KERN_DEBUG "xen: --> pirq=%d -> irq=%d (gsi=%d)\n", map_irq.pirq, irq, gsi);
out:
	return irq;
}

static int acpi_register_gsi_xen_hvm(struct device *dev, u32 gsi,
				     int trigger, int polarity)
{
	if (!xen_hvm_domain())
		return -1;

	return xen_register_pirq(gsi, -1 /* no GSI override */, trigger,
				 false /* no mapping of GSI to PIRQ */);
}

#ifdef CONFIG_XEN_DOM0
static int xen_register_gsi(u32 gsi, int gsi_override, int triggering, int polarity)
{
	int rc, irq;
	struct physdev_setup_gsi setup_gsi;

	if (!xen_pv_domain())
		return -1;

	printk(KERN_DEBUG "xen: registering gsi %u triggering %d polarity %d\n",
			gsi, triggering, polarity);

	irq = xen_register_pirq(gsi, gsi_override, triggering, true);

	setup_gsi.gsi = gsi;
	setup_gsi.triggering = (triggering == ACPI_EDGE_SENSITIVE ? 0 : 1);
	setup_gsi.polarity = (polarity == ACPI_ACTIVE_HIGH ? 0 : 1);

	rc = HYPERVISOR_physdev_op(PHYSDEVOP_setup_gsi, &setup_gsi);
	if (rc == -EEXIST)
		printk(KERN_INFO "Already setup the GSI :%d\n", gsi);
	else if (rc) {
		printk(KERN_ERR "Failed to setup GSI :%d, err_code:%d\n",
				gsi, rc);
	}

	return irq;
}

static int acpi_register_gsi_xen(struct device *dev, u32 gsi,
				 int trigger, int polarity)
{
	return xen_register_gsi(gsi, -1 /* no GSI override */, trigger, polarity);
}
#endif
#endif

#if defined(CONFIG_PCI_MSI)
#include <linux/msi.h>
#include <asm/msidef.h>

struct xen_pci_frontend_ops *xen_pci_frontend;
EXPORT_SYMBOL_GPL(xen_pci_frontend);

static int xen_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	int irq, ret, i;
	struct msi_desc *msidesc;
	int *v;

	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	v = kzalloc(sizeof(int) * max(1, nvec), GFP_KERNEL);
	if (!v)
		return -ENOMEM;

	if (type == PCI_CAP_ID_MSIX)
		ret = xen_pci_frontend_enable_msix(dev, v, nvec);
	else
		ret = xen_pci_frontend_enable_msi(dev, v);
	if (ret)
		goto error;
	i = 0;
	list_for_each_entry(msidesc, &dev->msi_list, list) {
		irq = xen_bind_pirq_msi_to_irq(dev, msidesc, v[i],
					       (type == PCI_CAP_ID_MSI) ? nvec : 1,
					       (type == PCI_CAP_ID_MSIX) ?
					       "pcifront-msi-x" :
					       "pcifront-msi",
						DOMID_SELF);
		if (irq < 0) {
			ret = irq;
			goto free;
		}
		i++;
	}
	kfree(v);
	return 0;

error:
	dev_err(&dev->dev, "Xen PCI frontend has not registered MSI/MSI-X support!\n");
free:
	kfree(v);
	return ret;
}

#define XEN_PIRQ_MSI_DATA  (MSI_DATA_TRIGGER_EDGE | \
		MSI_DATA_LEVEL_ASSERT | (3 << 8) | MSI_DATA_VECTOR(0))

static void xen_msi_compose_msg(struct pci_dev *pdev, unsigned int pirq,
		struct msi_msg *msg)
{
	/* We set vector == 0 to tell the hypervisor we don't care about it,
	 * but we want a pirq setup instead.
	 * We use the dest_id field to pass the pirq that we want. */
	msg->address_hi = MSI_ADDR_BASE_HI | MSI_ADDR_EXT_DEST_ID(pirq);
	msg->address_lo =
		MSI_ADDR_BASE_LO |
		MSI_ADDR_DEST_MODE_PHYSICAL |
		MSI_ADDR_REDIRECTION_CPU |
		MSI_ADDR_DEST_ID(pirq);

	msg->data = XEN_PIRQ_MSI_DATA;
}

static int xen_hvm_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	int irq, pirq;
	struct msi_desc *msidesc;
	struct msi_msg msg;

	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	list_for_each_entry(msidesc, &dev->msi_list, list) {
		__pci_read_msi_msg(msidesc, &msg);
		pirq = MSI_ADDR_EXT_DEST_ID(msg.address_hi) |
			((msg.address_lo >> MSI_ADDR_DEST_ID_SHIFT) & 0xff);
		if (msg.data != XEN_PIRQ_MSI_DATA ||
		    xen_irq_from_pirq(pirq) < 0) {
			pirq = xen_allocate_pirq_msi(dev, msidesc);
			if (pirq < 0) {
				irq = -ENODEV;
				goto error;
			}
			xen_msi_compose_msg(dev, pirq, &msg);
			__pci_write_msi_msg(msidesc, &msg);
			dev_dbg(&dev->dev, "xen: msi bound to pirq=%d\n", pirq);
		} else {
			dev_dbg(&dev->dev,
				"xen: msi already bound to pirq=%d\n", pirq);
		}
		irq = xen_bind_pirq_msi_to_irq(dev, msidesc, pirq,
					       (type == PCI_CAP_ID_MSI) ? nvec : 1,
					       (type == PCI_CAP_ID_MSIX) ?
					       "msi-x" : "msi",
					       DOMID_SELF);
		if (irq < 0)
			goto error;
		dev_dbg(&dev->dev,
			"xen: msi --> pirq=%d --> irq=%d\n", pirq, irq);
	}
	return 0;

error:
	dev_err(&dev->dev,
		"Xen PCI frontend has not registered MSI/MSI-X support!\n");
	return irq;
}

#ifdef CONFIG_XEN_DOM0
static bool __read_mostly pci_seg_supported = true;

static int xen_initdom_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	int ret = 0;
	struct msi_desc *msidesc;

	list_for_each_entry(msidesc, &dev->msi_list, list) {
		struct physdev_map_pirq map_irq;
		domid_t domid;

		domid = ret = xen_find_device_domain_owner(dev);
		/* N.B. Casting int's -ENODEV to uint16_t results in 0xFFED,
		 * hence check ret value for < 0. */
		if (ret < 0)
			domid = DOMID_SELF;

		memset(&map_irq, 0, sizeof(map_irq));
		map_irq.domid = domid;
		map_irq.type = MAP_PIRQ_TYPE_MSI_SEG;
		map_irq.index = -1;
		map_irq.pirq = -1;
		map_irq.bus = dev->bus->number |
			      (pci_domain_nr(dev->bus) << 16);
		map_irq.devfn = dev->devfn;

		if (type == PCI_CAP_ID_MSI && nvec > 1) {
			map_irq.type = MAP_PIRQ_TYPE_MULTI_MSI;
			map_irq.entry_nr = nvec;
		} else if (type == PCI_CAP_ID_MSIX) {
			int pos;
			u32 table_offset, bir;

			pos = dev->msix_cap;
			pci_read_config_dword(dev, pos + PCI_MSIX_TABLE,
					      &table_offset);
			bir = (u8)(table_offset & PCI_MSIX_TABLE_BIR);

			map_irq.table_base = pci_resource_start(dev, bir);
			map_irq.entry_nr = msidesc->msi_attrib.entry_nr;
		}

		ret = -EINVAL;
		if (pci_seg_supported)
			ret = HYPERVISOR_physdev_op(PHYSDEVOP_map_pirq,
						    &map_irq);
		if (type == PCI_CAP_ID_MSI && nvec > 1 && ret) {
			/*
			 * If MAP_PIRQ_TYPE_MULTI_MSI is not available
			 * there's nothing else we can do in this case.
			 * Just set ret > 0 so driver can retry with
			 * single MSI.
			 */
			ret = 1;
			goto out;
		}
		if (ret == -EINVAL && !pci_domain_nr(dev->bus)) {
			map_irq.type = MAP_PIRQ_TYPE_MSI;
			map_irq.index = -1;
			map_irq.pirq = -1;
			map_irq.bus = dev->bus->number;
			ret = HYPERVISOR_physdev_op(PHYSDEVOP_map_pirq,
						    &map_irq);
			if (ret != -EINVAL)
				pci_seg_supported = false;
		}
		if (ret) {
			dev_warn(&dev->dev, "xen map irq failed %d for %d domain\n",
				 ret, domid);
			goto out;
		}

		ret = xen_bind_pirq_msi_to_irq(dev, msidesc, map_irq.pirq,
		                               (type == PCI_CAP_ID_MSI) ? nvec : 1,
		                               (type == PCI_CAP_ID_MSIX) ? "msi-x" : "msi",
		                               domid);
		if (ret < 0)
			goto out;
	}
	ret = 0;
out:
	return ret;
}

static void xen_initdom_restore_msi_irqs(struct pci_dev *dev)
{
	int ret = 0;

	if (pci_seg_supported) {
		struct physdev_pci_device restore_ext;

		restore_ext.seg = pci_domain_nr(dev->bus);
		restore_ext.bus = dev->bus->number;
		restore_ext.devfn = dev->devfn;
		ret = HYPERVISOR_physdev_op(PHYSDEVOP_restore_msi_ext,
					&restore_ext);
		if (ret == -ENOSYS)
			pci_seg_supported = false;
		WARN(ret && ret != -ENOSYS, "restore_msi_ext -> %d\n", ret);
	}
	if (!pci_seg_supported) {
		struct physdev_restore_msi restore;

		restore.bus = dev->bus->number;
		restore.devfn = dev->devfn;
		ret = HYPERVISOR_physdev_op(PHYSDEVOP_restore_msi, &restore);
		WARN(ret && ret != -ENOSYS, "restore_msi -> %d\n", ret);
	}
}
#endif

static void xen_teardown_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *msidesc;

	msidesc = list_entry(dev->msi_list.next, struct msi_desc, list);
	if (msidesc->msi_attrib.is_msix)
		xen_pci_frontend_disable_msix(dev);
	else
		xen_pci_frontend_disable_msi(dev);

	/* Free the IRQ's and the msidesc using the generic code. */
	default_teardown_msi_irqs(dev);
}

static void xen_teardown_msi_irq(unsigned int irq)
{
	xen_destroy_irq(irq);
}

#endif

int __init pci_xen_init(void)
{
	if (!xen_pv_domain() || xen_initial_domain())
		return -ENODEV;

	printk(KERN_INFO "PCI: setting up Xen PCI frontend stub\n");

	pcibios_set_cache_line_size();

	pcibios_enable_irq = xen_pcifront_enable_irq;
	pcibios_disable_irq = NULL;

#ifdef CONFIG_ACPI
	/* Keep ACPI out of the picture */
	acpi_noirq = 1;
#endif

#ifdef CONFIG_PCI_MSI
	x86_msi.setup_msi_irqs = xen_setup_msi_irqs;
	x86_msi.teardown_msi_irq = xen_teardown_msi_irq;
	x86_msi.teardown_msi_irqs = xen_teardown_msi_irqs;
	pci_msi_ignore_mask = 1;
#endif
	return 0;
}

int __init pci_xen_hvm_init(void)
{
	if (!xen_have_vector_callback || !xen_feature(XENFEAT_hvm_pirqs))
		return 0;

#ifdef CONFIG_ACPI
	/*
	 * We don't want to change the actual ACPI delivery model,
	 * just how GSIs get registered.
	 */
	__acpi_register_gsi = acpi_register_gsi_xen_hvm;
#endif

#ifdef CONFIG_PCI_MSI
	x86_msi.setup_msi_irqs = xen_hvm_setup_msi_irqs;
	x86_msi.teardown_msi_irq = xen_teardown_msi_irq;
#endif
	return 0;
}

#ifdef CONFIG_XEN_DOM0
static __init void xen_setup_acpi_sci(void)
{
	int rc;
	int trigger, polarity;
	int gsi = acpi_sci_override_gsi;
	int irq = -1;
	int gsi_override = -1;

	if (!gsi)
		return;

	rc = acpi_get_override_irq(gsi, &trigger, &polarity);
	if (rc) {
		printk(KERN_WARNING "xen: acpi_get_override_irq failed for acpi"
				" sci, rc=%d\n", rc);
		return;
	}
	trigger = trigger ? ACPI_LEVEL_SENSITIVE : ACPI_EDGE_SENSITIVE;
	polarity = polarity ? ACPI_ACTIVE_LOW : ACPI_ACTIVE_HIGH;

	printk(KERN_INFO "xen: sci override: global_irq=%d trigger=%d "
			"polarity=%d\n", gsi, trigger, polarity);

	/* Before we bind the GSI to a Linux IRQ, check whether
	 * we need to override it with bus_irq (IRQ) value. Usually for
	 * IRQs below IRQ_LEGACY_IRQ this holds IRQ == GSI, as so:
	 *  ACPI: INT_SRC_OVR (bus 0 bus_irq 9 global_irq 9 low level)
	 * but there are oddballs where the IRQ != GSI:
	 *  ACPI: INT_SRC_OVR (bus 0 bus_irq 9 global_irq 20 low level)
	 * which ends up being: gsi_to_irq[9] == 20
	 * (which is what acpi_gsi_to_irq ends up calling when starting the
	 * the ACPI interpreter and keels over since IRQ 9 has not been
	 * setup as we had setup IRQ 20 for it).
	 */
	if (acpi_gsi_to_irq(gsi, &irq) == 0) {
		/* Use the provided value if it's valid. */
		if (irq >= 0)
			gsi_override = irq;
	}

	gsi = xen_register_gsi(gsi, gsi_override, trigger, polarity);
	printk(KERN_INFO "xen: acpi sci %d\n", gsi);

	return;
}

int __init pci_xen_initial_domain(void)
{
	int irq;

#ifdef CONFIG_PCI_MSI
	x86_msi.setup_msi_irqs = xen_initdom_setup_msi_irqs;
	x86_msi.teardown_msi_irq = xen_teardown_msi_irq;
	x86_msi.restore_msi_irqs = xen_initdom_restore_msi_irqs;
	pci_msi_ignore_mask = 1;
#endif
	xen_setup_acpi_sci();
	__acpi_register_gsi = acpi_register_gsi_xen;
	/* Pre-allocate legacy irqs */
	for (irq = 0; irq < nr_legacy_irqs(); irq++) {
		int trigger, polarity;

		if (acpi_get_override_irq(irq, &trigger, &polarity) == -1)
			continue;

		xen_register_pirq(irq, -1 /* no GSI override */,
			trigger ? ACPI_LEVEL_SENSITIVE : ACPI_EDGE_SENSITIVE,
			true /* Map GSI to PIRQ */);
	}
	if (0 == nr_ioapics) {
		for (irq = 0; irq < nr_legacy_irqs(); irq++)
			xen_bind_pirq_gsi_to_irq(irq, irq, 0, "xt-pic");
	}
	return 0;
}

struct xen_device_domain_owner {
	domid_t domain;
	struct pci_dev *dev;
	struct list_head list;
};

static DEFINE_SPINLOCK(dev_domain_list_spinlock);
static struct list_head dev_domain_list = LIST_HEAD_INIT(dev_domain_list);

static struct xen_device_domain_owner *find_device(struct pci_dev *dev)
{
	struct xen_device_domain_owner *owner;

	list_for_each_entry(owner, &dev_domain_list, list) {
		if (owner->dev == dev)
			return owner;
	}
	return NULL;
}

int xen_find_device_domain_owner(struct pci_dev *dev)
{
	struct xen_device_domain_owner *owner;
	int domain = -ENODEV;

	spin_lock(&dev_domain_list_spinlock);
	owner = find_device(dev);
	if (owner)
		domain = owner->domain;
	spin_unlock(&dev_domain_list_spinlock);
	return domain;
}
EXPORT_SYMBOL_GPL(xen_find_device_domain_owner);

int xen_register_device_domain_owner(struct pci_dev *dev, uint16_t domain)
{
	struct xen_device_domain_owner *owner;

	owner = kzalloc(sizeof(struct xen_device_domain_owner), GFP_KERNEL);
	if (!owner)
		return -ENODEV;

	spin_lock(&dev_domain_list_spinlock);
	if (find_device(dev)) {
		spin_unlock(&dev_domain_list_spinlock);
		kfree(owner);
		return -EEXIST;
	}
	owner->domain = domain;
	owner->dev = dev;
	list_add_tail(&owner->list, &dev_domain_list);
	spin_unlock(&dev_domain_list_spinlock);
	return 0;
}
EXPORT_SYMBOL_GPL(xen_register_device_domain_owner);

int xen_unregister_device_domain_owner(struct pci_dev *dev)
{
	struct xen_device_domain_owner *owner;

	spin_lock(&dev_domain_list_spinlock);
	owner = find_device(dev);
	if (!owner) {
		spin_unlock(&dev_domain_list_spinlock);
		return -ENODEV;
	}
	list_del(&owner->list);
	spin_unlock(&dev_domain_list_spinlock);
	kfree(owner);
	return 0;
}
EXPORT_SYMBOL_GPL(xen_unregister_device_domain_owner);
#endif
