// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/export.h>
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
#include <asm/xen/cpuid.h>
#include <asm/apic.h>
#include <asm/acpi.h>
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
static int xen_register_pirq(u32 gsi, int triggering, bool set_pirq)
{
	int rc, pirq = -1, irq;
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

	return xen_register_pirq(gsi, trigger,
				 false /* no mapping of GSI to PIRQ */);
}

#ifdef CONFIG_XEN_DOM0
static int xen_register_gsi(u32 gsi, int triggering, int polarity)
{
	int rc, irq;
	struct physdev_setup_gsi setup_gsi;

	if (!xen_pv_domain())
		return -1;

	printk(KERN_DEBUG "xen: registering gsi %u triggering %d polarity %d\n",
			gsi, triggering, polarity);

	irq = xen_register_pirq(gsi, triggering, true);

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
	return xen_register_gsi(gsi, trigger, polarity);
}
#endif
#endif

#if defined(CONFIG_PCI_MSI)
#include <linux/msi.h>

struct xen_pci_frontend_ops *xen_pci_frontend;
EXPORT_SYMBOL_GPL(xen_pci_frontend);

struct xen_msi_ops {
	int (*setup_msi_irqs)(struct pci_dev *dev, int nvec, int type);
	void (*teardown_msi_irqs)(struct pci_dev *dev);
};

static struct xen_msi_ops xen_msi_ops __ro_after_init;

static int xen_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	int irq, ret, i;
	struct msi_desc *msidesc;
	int *v;

	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	v = kcalloc(max(1, nvec), sizeof(int), GFP_KERNEL);
	if (!v)
		return -ENOMEM;

	if (type == PCI_CAP_ID_MSIX)
		ret = xen_pci_frontend_enable_msix(dev, v, nvec);
	else
		ret = xen_pci_frontend_enable_msi(dev, v);
	if (ret)
		goto error;
	i = 0;
	for_each_pci_msi_entry(msidesc, dev) {
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
	if (ret == -ENOSYS)
		dev_err(&dev->dev, "Xen PCI frontend has not registered MSI/MSI-X support!\n");
	else if (ret)
		dev_err(&dev->dev, "Xen PCI frontend error: %d!\n", ret);
free:
	kfree(v);
	return ret;
}

static void xen_msi_compose_msg(struct pci_dev *pdev, unsigned int pirq,
		struct msi_msg *msg)
{
	/*
	 * We set vector == 0 to tell the hypervisor we don't care about
	 * it, but we want a pirq setup instead.  We use the dest_id fields
	 * to pass the pirq that we want.
	 */
	memset(msg, 0, sizeof(*msg));
	msg->address_hi = X86_MSI_BASE_ADDRESS_HIGH;
	msg->arch_addr_hi.destid_8_31 = pirq >> 8;
	msg->arch_addr_lo.destid_0_7 = pirq & 0xFF;
	msg->arch_addr_lo.base_address = X86_MSI_BASE_ADDRESS_LOW;
	msg->arch_data.delivery_mode = APIC_DELIVERY_MODE_EXTINT;
}

static int xen_hvm_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	int irq, pirq;
	struct msi_desc *msidesc;
	struct msi_msg msg;

	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	for_each_pci_msi_entry(msidesc, dev) {
		pirq = xen_allocate_pirq_msi(dev, msidesc);
		if (pirq < 0) {
			irq = -ENODEV;
			goto error;
		}
		xen_msi_compose_msg(dev, pirq, &msg);
		__pci_write_msi_msg(msidesc, &msg);
		dev_dbg(&dev->dev, "xen: msi bound to pirq=%d\n", pirq);
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
	dev_err(&dev->dev, "Failed to create MSI%s! ret=%d!\n",
		type == PCI_CAP_ID_MSI ? "" : "-X", irq);
	return irq;
}

#ifdef CONFIG_XEN_DOM0
static bool __read_mostly pci_seg_supported = true;

static int xen_initdom_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	int ret = 0;
	struct msi_desc *msidesc;

	for_each_pci_msi_entry(msidesc, dev) {
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
			unsigned long flags;
			u32 table_offset, bir;

			pos = dev->msix_cap;
			pci_read_config_dword(dev, pos + PCI_MSIX_TABLE,
					      &table_offset);
			bir = (u8)(table_offset & PCI_MSIX_TABLE_BIR);
			flags = pci_resource_flags(dev, bir);
			if (!flags || (flags & IORESOURCE_UNSET))
				return -EINVAL;

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
#else /* CONFIG_XEN_DOM0 */
#define xen_initdom_setup_msi_irqs	NULL
#define xen_initdom_restore_msi_irqs	NULL
#endif /* !CONFIG_XEN_DOM0 */

static void xen_teardown_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *msidesc;
	int i;

	for_each_pci_msi_entry(msidesc, dev) {
		if (msidesc->irq) {
			for (i = 0; i < msidesc->nvec_used; i++)
				xen_destroy_irq(msidesc->irq + i);
		}
	}
}

static void xen_pv_teardown_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *msidesc = first_pci_msi_entry(dev);

	if (msidesc->msi_attrib.is_msix)
		xen_pci_frontend_disable_msix(dev);
	else
		xen_pci_frontend_disable_msi(dev);

	xen_teardown_msi_irqs(dev);
}

static int xen_msi_domain_alloc_irqs(struct irq_domain *domain,
				     struct device *dev,  int nvec)
{
	int type;

	if (WARN_ON_ONCE(!dev_is_pci(dev)))
		return -EINVAL;

	if (first_msi_entry(dev)->msi_attrib.is_msix)
		type = PCI_CAP_ID_MSIX;
	else
		type = PCI_CAP_ID_MSI;

	return xen_msi_ops.setup_msi_irqs(to_pci_dev(dev), nvec, type);
}

static void xen_msi_domain_free_irqs(struct irq_domain *domain,
				     struct device *dev)
{
	if (WARN_ON_ONCE(!dev_is_pci(dev)))
		return;

	xen_msi_ops.teardown_msi_irqs(to_pci_dev(dev));
}

static struct msi_domain_ops xen_pci_msi_domain_ops = {
	.domain_alloc_irqs	= xen_msi_domain_alloc_irqs,
	.domain_free_irqs	= xen_msi_domain_free_irqs,
};

static struct msi_domain_info xen_pci_msi_domain_info = {
	.ops			= &xen_pci_msi_domain_ops,
};

/*
 * This irq domain is a blatant violation of the irq domain design, but
 * distangling XEN into real irq domains is not a job for mere mortals with
 * limited XENology. But it's the least dangerous way for a mere mortal to
 * get rid of the arch_*_msi_irqs() hackery in order to store the irq
 * domain pointer in struct device. This irq domain wrappery allows to do
 * that without breaking XEN terminally.
 */
static __init struct irq_domain *xen_create_pci_msi_domain(void)
{
	struct irq_domain *d = NULL;
	struct fwnode_handle *fn;

	fn = irq_domain_alloc_named_fwnode("XEN-MSI");
	if (fn)
		d = msi_create_irq_domain(fn, &xen_pci_msi_domain_info, NULL);

	/* FIXME: No idea how to survive if this fails */
	BUG_ON(!d);

	return d;
}

static __init void xen_setup_pci_msi(void)
{
	if (xen_pv_domain()) {
		if (xen_initial_domain()) {
			xen_msi_ops.setup_msi_irqs = xen_initdom_setup_msi_irqs;
			x86_msi.restore_msi_irqs = xen_initdom_restore_msi_irqs;
		} else {
			xen_msi_ops.setup_msi_irqs = xen_setup_msi_irqs;
		}
		xen_msi_ops.teardown_msi_irqs = xen_pv_teardown_msi_irqs;
		pci_msi_ignore_mask = 1;
	} else if (xen_hvm_domain()) {
		xen_msi_ops.setup_msi_irqs = xen_hvm_setup_msi_irqs;
		xen_msi_ops.teardown_msi_irqs = xen_teardown_msi_irqs;
	} else {
		WARN_ON_ONCE(1);
		return;
	}

	/*
	 * Override the PCI/MSI irq domain init function. No point
	 * in allocating the native domain and never use it.
	 */
	x86_init.irqs.create_pci_msi_domain = xen_create_pci_msi_domain;
}

#else /* CONFIG_PCI_MSI */
static inline void xen_setup_pci_msi(void) { }
#endif /* CONFIG_PCI_MSI */

int __init pci_xen_init(void)
{
	if (!xen_pv_domain() || xen_initial_domain())
		return -ENODEV;

	printk(KERN_INFO "PCI: setting up Xen PCI frontend stub\n");

	pcibios_set_cache_line_size();

	pcibios_enable_irq = xen_pcifront_enable_irq;
	pcibios_disable_irq = NULL;

	/* Keep ACPI out of the picture */
	acpi_noirq_set();

	xen_setup_pci_msi();
	return 0;
}

#ifdef CONFIG_PCI_MSI
static void __init xen_hvm_msi_init(void)
{
	if (!disable_apic) {
		/*
		 * If hardware supports (x2)APIC virtualization (as indicated
		 * by hypervisor's leaf 4) then we don't need to use pirqs/
		 * event channels for MSI handling and instead use regular
		 * APIC processing
		 */
		uint32_t eax = cpuid_eax(xen_cpuid_base() + 4);

		if (((eax & XEN_HVM_CPUID_X2APIC_VIRT) && x2apic_mode) ||
		    ((eax & XEN_HVM_CPUID_APIC_ACCESS_VIRT) && boot_cpu_has(X86_FEATURE_APIC)))
			return;
	}
	xen_setup_pci_msi();
}
#endif

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
	__acpi_unregister_gsi = NULL;
#endif

#ifdef CONFIG_PCI_MSI
	/*
	 * We need to wait until after x2apic is initialized
	 * before we can set MSI IRQ ops.
	 */
	x86_platform.apic_post_init = xen_hvm_msi_init;
#endif
	return 0;
}

#ifdef CONFIG_XEN_DOM0
int __init pci_xen_initial_domain(void)
{
	int irq;

	xen_setup_pci_msi();
	__acpi_register_gsi = acpi_register_gsi_xen;
	__acpi_unregister_gsi = NULL;
	/*
	 * Pre-allocate the legacy IRQs.  Use NR_LEGACY_IRQS here
	 * because we don't have a PIC and thus nr_legacy_irqs() is zero.
	 */
	for (irq = 0; irq < NR_IRQS_LEGACY; irq++) {
		int trigger, polarity;

		if (acpi_get_override_irq(irq, &trigger, &polarity) == -1)
			continue;

		xen_register_pirq(irq,
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
