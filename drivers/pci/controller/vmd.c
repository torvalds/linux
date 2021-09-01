// SPDX-License-Identifier: GPL-2.0
/*
 * Volume Management Device driver
 * Copyright (c) 2015, Intel Corporation.
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/pci-ecam.h>
#include <linux/srcu.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>

#include <asm/irqdomain.h>
#include <asm/device.h>
#include <asm/msi.h>

#define VMD_CFGBAR	0
#define VMD_MEMBAR1	2
#define VMD_MEMBAR2	4

#define PCI_REG_VMCAP		0x40
#define BUS_RESTRICT_CAP(vmcap)	(vmcap & 0x1)
#define PCI_REG_VMCONFIG	0x44
#define BUS_RESTRICT_CFG(vmcfg)	((vmcfg >> 8) & 0x3)
#define VMCONFIG_MSI_REMAP	0x2
#define PCI_REG_VMLOCK		0x70
#define MB2_SHADOW_EN(vmlock)	(vmlock & 0x2)

#define MB2_SHADOW_OFFSET	0x2000
#define MB2_SHADOW_SIZE		16

enum vmd_features {
	/*
	 * Device may contain registers which hint the physical location of the
	 * membars, in order to allow proper address translation during
	 * resource assignment to enable guest virtualization
	 */
	VMD_FEAT_HAS_MEMBAR_SHADOW		= (1 << 0),

	/*
	 * Device may provide root port configuration information which limits
	 * bus numbering
	 */
	VMD_FEAT_HAS_BUS_RESTRICTIONS		= (1 << 1),

	/*
	 * Device contains physical location shadow registers in
	 * vendor-specific capability space
	 */
	VMD_FEAT_HAS_MEMBAR_SHADOW_VSCAP	= (1 << 2),

	/*
	 * Device may use MSI-X vector 0 for software triggering and will not
	 * be used for MSI remapping
	 */
	VMD_FEAT_OFFSET_FIRST_VECTOR		= (1 << 3),

	/*
	 * Device can bypass remapping MSI-X transactions into its MSI-X table,
	 * avoiding the requirement of a VMD MSI domain for child device
	 * interrupt handling.
	 */
	VMD_FEAT_CAN_BYPASS_MSI_REMAP		= (1 << 4),
};

static DEFINE_IDA(vmd_instance_ida);

/*
 * Lock for manipulating VMD IRQ lists.
 */
static DEFINE_RAW_SPINLOCK(list_lock);

/**
 * struct vmd_irq - private data to map driver IRQ to the VMD shared vector
 * @node:	list item for parent traversal.
 * @irq:	back pointer to parent.
 * @enabled:	true if driver enabled IRQ
 * @virq:	the virtual IRQ value provided to the requesting driver.
 *
 * Every MSI/MSI-X IRQ requested for a device in a VMD domain will be mapped to
 * a VMD IRQ using this structure.
 */
struct vmd_irq {
	struct list_head	node;
	struct vmd_irq_list	*irq;
	bool			enabled;
	unsigned int		virq;
};

/**
 * struct vmd_irq_list - list of driver requested IRQs mapping to a VMD vector
 * @irq_list:	the list of irq's the VMD one demuxes to.
 * @srcu:	SRCU struct for local synchronization.
 * @count:	number of child IRQs assigned to this vector; used to track
 *		sharing.
 */
struct vmd_irq_list {
	struct list_head	irq_list;
	struct srcu_struct	srcu;
	unsigned int		count;
};

struct vmd_dev {
	struct pci_dev		*dev;

	spinlock_t		cfg_lock;
	void __iomem		*cfgbar;

	int msix_count;
	struct vmd_irq_list	*irqs;

	struct pci_sysdata	sysdata;
	struct resource		resources[3];
	struct irq_domain	*irq_domain;
	struct pci_bus		*bus;
	u8			busn_start;
	u8			first_vec;
	char			*name;
	int			instance;
};

static inline struct vmd_dev *vmd_from_bus(struct pci_bus *bus)
{
	return container_of(bus->sysdata, struct vmd_dev, sysdata);
}

static inline unsigned int index_from_irqs(struct vmd_dev *vmd,
					   struct vmd_irq_list *irqs)
{
	return irqs - vmd->irqs;
}

/*
 * Drivers managing a device in a VMD domain allocate their own IRQs as before,
 * but the MSI entry for the hardware it's driving will be programmed with a
 * destination ID for the VMD MSI-X table.  The VMD muxes interrupts in its
 * domain into one of its own, and the VMD driver de-muxes these for the
 * handlers sharing that VMD IRQ.  The vmd irq_domain provides the operations
 * and irq_chip to set this up.
 */
static void vmd_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct vmd_irq *vmdirq = data->chip_data;
	struct vmd_irq_list *irq = vmdirq->irq;
	struct vmd_dev *vmd = irq_data_get_irq_handler_data(data);

	memset(msg, 0, sizeof(*msg));
	msg->address_hi = X86_MSI_BASE_ADDRESS_HIGH;
	msg->arch_addr_lo.base_address = X86_MSI_BASE_ADDRESS_LOW;
	msg->arch_addr_lo.destid_0_7 = index_from_irqs(vmd, irq);
}

/*
 * We rely on MSI_FLAG_USE_DEF_CHIP_OPS to set the IRQ mask/unmask ops.
 */
static void vmd_irq_enable(struct irq_data *data)
{
	struct vmd_irq *vmdirq = data->chip_data;
	unsigned long flags;

	raw_spin_lock_irqsave(&list_lock, flags);
	WARN_ON(vmdirq->enabled);
	list_add_tail_rcu(&vmdirq->node, &vmdirq->irq->irq_list);
	vmdirq->enabled = true;
	raw_spin_unlock_irqrestore(&list_lock, flags);

	data->chip->irq_unmask(data);
}

static void vmd_irq_disable(struct irq_data *data)
{
	struct vmd_irq *vmdirq = data->chip_data;
	unsigned long flags;

	data->chip->irq_mask(data);

	raw_spin_lock_irqsave(&list_lock, flags);
	if (vmdirq->enabled) {
		list_del_rcu(&vmdirq->node);
		vmdirq->enabled = false;
	}
	raw_spin_unlock_irqrestore(&list_lock, flags);
}

/*
 * XXX: Stubbed until we develop acceptable way to not create conflicts with
 * other devices sharing the same vector.
 */
static int vmd_irq_set_affinity(struct irq_data *data,
				const struct cpumask *dest, bool force)
{
	return -EINVAL;
}

static struct irq_chip vmd_msi_controller = {
	.name			= "VMD-MSI",
	.irq_enable		= vmd_irq_enable,
	.irq_disable		= vmd_irq_disable,
	.irq_compose_msi_msg	= vmd_compose_msi_msg,
	.irq_set_affinity	= vmd_irq_set_affinity,
};

static irq_hw_number_t vmd_get_hwirq(struct msi_domain_info *info,
				     msi_alloc_info_t *arg)
{
	return 0;
}

/*
 * XXX: We can be even smarter selecting the best IRQ once we solve the
 * affinity problem.
 */
static struct vmd_irq_list *vmd_next_irq(struct vmd_dev *vmd, struct msi_desc *desc)
{
	unsigned long flags;
	int i, best;

	if (vmd->msix_count == 1 + vmd->first_vec)
		return &vmd->irqs[vmd->first_vec];

	/*
	 * White list for fast-interrupt handlers. All others will share the
	 * "slow" interrupt vector.
	 */
	switch (msi_desc_to_pci_dev(desc)->class) {
	case PCI_CLASS_STORAGE_EXPRESS:
		break;
	default:
		return &vmd->irqs[vmd->first_vec];
	}

	raw_spin_lock_irqsave(&list_lock, flags);
	best = vmd->first_vec + 1;
	for (i = best; i < vmd->msix_count; i++)
		if (vmd->irqs[i].count < vmd->irqs[best].count)
			best = i;
	vmd->irqs[best].count++;
	raw_spin_unlock_irqrestore(&list_lock, flags);

	return &vmd->irqs[best];
}

static int vmd_msi_init(struct irq_domain *domain, struct msi_domain_info *info,
			unsigned int virq, irq_hw_number_t hwirq,
			msi_alloc_info_t *arg)
{
	struct msi_desc *desc = arg->desc;
	struct vmd_dev *vmd = vmd_from_bus(msi_desc_to_pci_dev(desc)->bus);
	struct vmd_irq *vmdirq = kzalloc(sizeof(*vmdirq), GFP_KERNEL);
	unsigned int index, vector;

	if (!vmdirq)
		return -ENOMEM;

	INIT_LIST_HEAD(&vmdirq->node);
	vmdirq->irq = vmd_next_irq(vmd, desc);
	vmdirq->virq = virq;
	index = index_from_irqs(vmd, vmdirq->irq);
	vector = pci_irq_vector(vmd->dev, index);

	irq_domain_set_info(domain, virq, vector, info->chip, vmdirq,
			    handle_untracked_irq, vmd, NULL);
	return 0;
}

static void vmd_msi_free(struct irq_domain *domain,
			struct msi_domain_info *info, unsigned int virq)
{
	struct vmd_irq *vmdirq = irq_get_chip_data(virq);
	unsigned long flags;

	synchronize_srcu(&vmdirq->irq->srcu);

	/* XXX: Potential optimization to rebalance */
	raw_spin_lock_irqsave(&list_lock, flags);
	vmdirq->irq->count--;
	raw_spin_unlock_irqrestore(&list_lock, flags);

	kfree(vmdirq);
}

static int vmd_msi_prepare(struct irq_domain *domain, struct device *dev,
			   int nvec, msi_alloc_info_t *arg)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct vmd_dev *vmd = vmd_from_bus(pdev->bus);

	if (nvec > vmd->msix_count)
		return vmd->msix_count;

	memset(arg, 0, sizeof(*arg));
	return 0;
}

static void vmd_set_desc(msi_alloc_info_t *arg, struct msi_desc *desc)
{
	arg->desc = desc;
}

static struct msi_domain_ops vmd_msi_domain_ops = {
	.get_hwirq	= vmd_get_hwirq,
	.msi_init	= vmd_msi_init,
	.msi_free	= vmd_msi_free,
	.msi_prepare	= vmd_msi_prepare,
	.set_desc	= vmd_set_desc,
};

static struct msi_domain_info vmd_msi_domain_info = {
	.flags		= MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
			  MSI_FLAG_PCI_MSIX,
	.ops		= &vmd_msi_domain_ops,
	.chip		= &vmd_msi_controller,
};

static void vmd_set_msi_remapping(struct vmd_dev *vmd, bool enable)
{
	u16 reg;

	pci_read_config_word(vmd->dev, PCI_REG_VMCONFIG, &reg);
	reg = enable ? (reg & ~VMCONFIG_MSI_REMAP) :
		       (reg | VMCONFIG_MSI_REMAP);
	pci_write_config_word(vmd->dev, PCI_REG_VMCONFIG, reg);
}

static int vmd_create_irq_domain(struct vmd_dev *vmd)
{
	struct fwnode_handle *fn;

	fn = irq_domain_alloc_named_id_fwnode("VMD-MSI", vmd->sysdata.domain);
	if (!fn)
		return -ENODEV;

	vmd->irq_domain = pci_msi_create_irq_domain(fn, &vmd_msi_domain_info, NULL);
	if (!vmd->irq_domain) {
		irq_domain_free_fwnode(fn);
		return -ENODEV;
	}

	return 0;
}

static void vmd_remove_irq_domain(struct vmd_dev *vmd)
{
	/*
	 * Some production BIOS won't enable remapping between soft reboots.
	 * Ensure remapping is restored before unloading the driver.
	 */
	if (!vmd->msix_count)
		vmd_set_msi_remapping(vmd, true);

	if (vmd->irq_domain) {
		struct fwnode_handle *fn = vmd->irq_domain->fwnode;

		irq_domain_remove(vmd->irq_domain);
		irq_domain_free_fwnode(fn);
	}
}

static void __iomem *vmd_cfg_addr(struct vmd_dev *vmd, struct pci_bus *bus,
				  unsigned int devfn, int reg, int len)
{
	unsigned int busnr_ecam = bus->number - vmd->busn_start;
	u32 offset = PCIE_ECAM_OFFSET(busnr_ecam, devfn, reg);

	if (offset + len >= resource_size(&vmd->dev->resource[VMD_CFGBAR]))
		return NULL;

	return vmd->cfgbar + offset;
}

/*
 * CPU may deadlock if config space is not serialized on some versions of this
 * hardware, so all config space access is done under a spinlock.
 */
static int vmd_pci_read(struct pci_bus *bus, unsigned int devfn, int reg,
			int len, u32 *value)
{
	struct vmd_dev *vmd = vmd_from_bus(bus);
	void __iomem *addr = vmd_cfg_addr(vmd, bus, devfn, reg, len);
	unsigned long flags;
	int ret = 0;

	if (!addr)
		return -EFAULT;

	spin_lock_irqsave(&vmd->cfg_lock, flags);
	switch (len) {
	case 1:
		*value = readb(addr);
		break;
	case 2:
		*value = readw(addr);
		break;
	case 4:
		*value = readl(addr);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&vmd->cfg_lock, flags);
	return ret;
}

/*
 * VMD h/w converts non-posted config writes to posted memory writes. The
 * read-back in this function forces the completion so it returns only after
 * the config space was written, as expected.
 */
static int vmd_pci_write(struct pci_bus *bus, unsigned int devfn, int reg,
			 int len, u32 value)
{
	struct vmd_dev *vmd = vmd_from_bus(bus);
	void __iomem *addr = vmd_cfg_addr(vmd, bus, devfn, reg, len);
	unsigned long flags;
	int ret = 0;

	if (!addr)
		return -EFAULT;

	spin_lock_irqsave(&vmd->cfg_lock, flags);
	switch (len) {
	case 1:
		writeb(value, addr);
		readb(addr);
		break;
	case 2:
		writew(value, addr);
		readw(addr);
		break;
	case 4:
		writel(value, addr);
		readl(addr);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&vmd->cfg_lock, flags);
	return ret;
}

static struct pci_ops vmd_ops = {
	.read		= vmd_pci_read,
	.write		= vmd_pci_write,
};

#ifdef CONFIG_ACPI
static struct acpi_device *vmd_acpi_find_companion(struct pci_dev *pci_dev)
{
	struct pci_host_bridge *bridge;
	u32 busnr, addr;

	if (pci_dev->bus->ops != &vmd_ops)
		return NULL;

	bridge = pci_find_host_bridge(pci_dev->bus);
	busnr = pci_dev->bus->number - bridge->bus->number;
	/*
	 * The address computation below is only applicable to relative bus
	 * numbers below 32.
	 */
	if (busnr > 31)
		return NULL;

	addr = (busnr << 24) | ((u32)pci_dev->devfn << 16) | 0x8000FFFFU;

	dev_dbg(&pci_dev->dev, "Looking for ACPI companion (address 0x%x)\n",
		addr);

	return acpi_find_child_device(ACPI_COMPANION(bridge->dev.parent), addr,
				      false);
}

static bool hook_installed;

static void vmd_acpi_begin(void)
{
	if (pci_acpi_set_companion_lookup_hook(vmd_acpi_find_companion))
		return;

	hook_installed = true;
}

static void vmd_acpi_end(void)
{
	if (!hook_installed)
		return;

	pci_acpi_clear_companion_lookup_hook();
	hook_installed = false;
}
#else
static inline void vmd_acpi_begin(void) { }
static inline void vmd_acpi_end(void) { }
#endif /* CONFIG_ACPI */

static void vmd_attach_resources(struct vmd_dev *vmd)
{
	vmd->dev->resource[VMD_MEMBAR1].child = &vmd->resources[1];
	vmd->dev->resource[VMD_MEMBAR2].child = &vmd->resources[2];
}

static void vmd_detach_resources(struct vmd_dev *vmd)
{
	vmd->dev->resource[VMD_MEMBAR1].child = NULL;
	vmd->dev->resource[VMD_MEMBAR2].child = NULL;
}

/*
 * VMD domains start at 0x10000 to not clash with ACPI _SEG domains.
 * Per ACPI r6.0, sec 6.5.6,  _SEG returns an integer, of which the lower
 * 16 bits are the PCI Segment Group (domain) number.  Other bits are
 * currently reserved.
 */
static int vmd_find_free_domain(void)
{
	int domain = 0xffff;
	struct pci_bus *bus = NULL;

	while ((bus = pci_find_next_bus(bus)) != NULL)
		domain = max_t(int, domain, pci_domain_nr(bus));
	return domain + 1;
}

static int vmd_get_phys_offsets(struct vmd_dev *vmd, bool native_hint,
				resource_size_t *offset1,
				resource_size_t *offset2)
{
	struct pci_dev *dev = vmd->dev;
	u64 phys1, phys2;

	if (native_hint) {
		u32 vmlock;
		int ret;

		ret = pci_read_config_dword(dev, PCI_REG_VMLOCK, &vmlock);
		if (ret || vmlock == ~0)
			return -ENODEV;

		if (MB2_SHADOW_EN(vmlock)) {
			void __iomem *membar2;

			membar2 = pci_iomap(dev, VMD_MEMBAR2, 0);
			if (!membar2)
				return -ENOMEM;
			phys1 = readq(membar2 + MB2_SHADOW_OFFSET);
			phys2 = readq(membar2 + MB2_SHADOW_OFFSET + 8);
			pci_iounmap(dev, membar2);
		} else
			return 0;
	} else {
		/* Hypervisor-Emulated Vendor-Specific Capability */
		int pos = pci_find_capability(dev, PCI_CAP_ID_VNDR);
		u32 reg, regu;

		pci_read_config_dword(dev, pos + 4, &reg);

		/* "SHDW" */
		if (pos && reg == 0x53484457) {
			pci_read_config_dword(dev, pos + 8, &reg);
			pci_read_config_dword(dev, pos + 12, &regu);
			phys1 = (u64) regu << 32 | reg;

			pci_read_config_dword(dev, pos + 16, &reg);
			pci_read_config_dword(dev, pos + 20, &regu);
			phys2 = (u64) regu << 32 | reg;
		} else
			return 0;
	}

	*offset1 = dev->resource[VMD_MEMBAR1].start -
			(phys1 & PCI_BASE_ADDRESS_MEM_MASK);
	*offset2 = dev->resource[VMD_MEMBAR2].start -
			(phys2 & PCI_BASE_ADDRESS_MEM_MASK);

	return 0;
}

static int vmd_get_bus_number_start(struct vmd_dev *vmd)
{
	struct pci_dev *dev = vmd->dev;
	u16 reg;

	pci_read_config_word(dev, PCI_REG_VMCAP, &reg);
	if (BUS_RESTRICT_CAP(reg)) {
		pci_read_config_word(dev, PCI_REG_VMCONFIG, &reg);

		switch (BUS_RESTRICT_CFG(reg)) {
		case 0:
			vmd->busn_start = 0;
			break;
		case 1:
			vmd->busn_start = 128;
			break;
		case 2:
			vmd->busn_start = 224;
			break;
		default:
			pci_err(dev, "Unknown Bus Offset Setting (%d)\n",
				BUS_RESTRICT_CFG(reg));
			return -ENODEV;
		}
	}

	return 0;
}

static irqreturn_t vmd_irq(int irq, void *data)
{
	struct vmd_irq_list *irqs = data;
	struct vmd_irq *vmdirq;
	int idx;

	idx = srcu_read_lock(&irqs->srcu);
	list_for_each_entry_rcu(vmdirq, &irqs->irq_list, node)
		generic_handle_irq(vmdirq->virq);
	srcu_read_unlock(&irqs->srcu, idx);

	return IRQ_HANDLED;
}

static int vmd_alloc_irqs(struct vmd_dev *vmd)
{
	struct pci_dev *dev = vmd->dev;
	int i, err;

	vmd->msix_count = pci_msix_vec_count(dev);
	if (vmd->msix_count < 0)
		return -ENODEV;

	vmd->msix_count = pci_alloc_irq_vectors(dev, vmd->first_vec + 1,
						vmd->msix_count, PCI_IRQ_MSIX);
	if (vmd->msix_count < 0)
		return vmd->msix_count;

	vmd->irqs = devm_kcalloc(&dev->dev, vmd->msix_count, sizeof(*vmd->irqs),
				 GFP_KERNEL);
	if (!vmd->irqs)
		return -ENOMEM;

	for (i = 0; i < vmd->msix_count; i++) {
		err = init_srcu_struct(&vmd->irqs[i].srcu);
		if (err)
			return err;

		INIT_LIST_HEAD(&vmd->irqs[i].irq_list);
		err = devm_request_irq(&dev->dev, pci_irq_vector(dev, i),
				       vmd_irq, IRQF_NO_THREAD,
				       vmd->name, &vmd->irqs[i]);
		if (err)
			return err;
	}

	return 0;
}

static int vmd_enable_domain(struct vmd_dev *vmd, unsigned long features)
{
	struct pci_sysdata *sd = &vmd->sysdata;
	struct resource *res;
	u32 upper_bits;
	unsigned long flags;
	LIST_HEAD(resources);
	resource_size_t offset[2] = {0};
	resource_size_t membar2_offset = 0x2000;
	struct pci_bus *child;
	int ret;

	/*
	 * Shadow registers may exist in certain VMD device ids which allow
	 * guests to correctly assign host physical addresses to the root ports
	 * and child devices. These registers will either return the host value
	 * or 0, depending on an enable bit in the VMD device.
	 */
	if (features & VMD_FEAT_HAS_MEMBAR_SHADOW) {
		membar2_offset = MB2_SHADOW_OFFSET + MB2_SHADOW_SIZE;
		ret = vmd_get_phys_offsets(vmd, true, &offset[0], &offset[1]);
		if (ret)
			return ret;
	} else if (features & VMD_FEAT_HAS_MEMBAR_SHADOW_VSCAP) {
		ret = vmd_get_phys_offsets(vmd, false, &offset[0], &offset[1]);
		if (ret)
			return ret;
	}

	/*
	 * Certain VMD devices may have a root port configuration option which
	 * limits the bus range to between 0-127, 128-255, or 224-255
	 */
	if (features & VMD_FEAT_HAS_BUS_RESTRICTIONS) {
		ret = vmd_get_bus_number_start(vmd);
		if (ret)
			return ret;
	}

	res = &vmd->dev->resource[VMD_CFGBAR];
	vmd->resources[0] = (struct resource) {
		.name  = "VMD CFGBAR",
		.start = vmd->busn_start,
		.end   = vmd->busn_start + (resource_size(res) >> 20) - 1,
		.flags = IORESOURCE_BUS | IORESOURCE_PCI_FIXED,
	};

	/*
	 * If the window is below 4GB, clear IORESOURCE_MEM_64 so we can
	 * put 32-bit resources in the window.
	 *
	 * There's no hardware reason why a 64-bit window *couldn't*
	 * contain a 32-bit resource, but pbus_size_mem() computes the
	 * bridge window size assuming a 64-bit window will contain no
	 * 32-bit resources.  __pci_assign_resource() enforces that
	 * artificial restriction to make sure everything will fit.
	 *
	 * The only way we could use a 64-bit non-prefetchable MEMBAR is
	 * if its address is <4GB so that we can convert it to a 32-bit
	 * resource.  To be visible to the host OS, all VMD endpoints must
	 * be initially configured by platform BIOS, which includes setting
	 * up these resources.  We can assume the device is configured
	 * according to the platform needs.
	 */
	res = &vmd->dev->resource[VMD_MEMBAR1];
	upper_bits = upper_32_bits(res->end);
	flags = res->flags & ~IORESOURCE_SIZEALIGN;
	if (!upper_bits)
		flags &= ~IORESOURCE_MEM_64;
	vmd->resources[1] = (struct resource) {
		.name  = "VMD MEMBAR1",
		.start = res->start,
		.end   = res->end,
		.flags = flags,
		.parent = res,
	};

	res = &vmd->dev->resource[VMD_MEMBAR2];
	upper_bits = upper_32_bits(res->end);
	flags = res->flags & ~IORESOURCE_SIZEALIGN;
	if (!upper_bits)
		flags &= ~IORESOURCE_MEM_64;
	vmd->resources[2] = (struct resource) {
		.name  = "VMD MEMBAR2",
		.start = res->start + membar2_offset,
		.end   = res->end,
		.flags = flags,
		.parent = res,
	};

	sd->vmd_dev = vmd->dev;
	sd->domain = vmd_find_free_domain();
	if (sd->domain < 0)
		return sd->domain;

	sd->node = pcibus_to_node(vmd->dev->bus);

	/*
	 * Currently MSI remapping must be enabled in guest passthrough mode
	 * due to some missing interrupt remapping plumbing. This is probably
	 * acceptable because the guest is usually CPU-limited and MSI
	 * remapping doesn't become a performance bottleneck.
	 */
	if (iommu_capable(vmd->dev->dev.bus, IOMMU_CAP_INTR_REMAP) ||
	    !(features & VMD_FEAT_CAN_BYPASS_MSI_REMAP) ||
	    offset[0] || offset[1]) {
		ret = vmd_alloc_irqs(vmd);
		if (ret)
			return ret;

		vmd_set_msi_remapping(vmd, true);

		ret = vmd_create_irq_domain(vmd);
		if (ret)
			return ret;

		/*
		 * Override the IRQ domain bus token so the domain can be
		 * distinguished from a regular PCI/MSI domain.
		 */
		irq_domain_update_bus_token(vmd->irq_domain, DOMAIN_BUS_VMD_MSI);
	} else {
		vmd_set_msi_remapping(vmd, false);
	}

	pci_add_resource(&resources, &vmd->resources[0]);
	pci_add_resource_offset(&resources, &vmd->resources[1], offset[0]);
	pci_add_resource_offset(&resources, &vmd->resources[2], offset[1]);

	vmd->bus = pci_create_root_bus(&vmd->dev->dev, vmd->busn_start,
				       &vmd_ops, sd, &resources);
	if (!vmd->bus) {
		pci_free_resource_list(&resources);
		vmd_remove_irq_domain(vmd);
		return -ENODEV;
	}

	vmd_attach_resources(vmd);
	if (vmd->irq_domain)
		dev_set_msi_domain(&vmd->bus->dev, vmd->irq_domain);

	vmd_acpi_begin();

	pci_scan_child_bus(vmd->bus);
	pci_assign_unassigned_bus_resources(vmd->bus);

	/*
	 * VMD root buses are virtual and don't return true on pci_is_pcie()
	 * and will fail pcie_bus_configure_settings() early. It can instead be
	 * run on each of the real root ports.
	 */
	list_for_each_entry(child, &vmd->bus->children, node)
		pcie_bus_configure_settings(child);

	pci_bus_add_devices(vmd->bus);

	vmd_acpi_end();

	WARN(sysfs_create_link(&vmd->dev->dev.kobj, &vmd->bus->dev.kobj,
			       "domain"), "Can't create symlink to domain\n");
	return 0;
}

static int vmd_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	unsigned long features = (unsigned long) id->driver_data;
	struct vmd_dev *vmd;
	int err;

	if (resource_size(&dev->resource[VMD_CFGBAR]) < (1 << 20))
		return -ENOMEM;

	vmd = devm_kzalloc(&dev->dev, sizeof(*vmd), GFP_KERNEL);
	if (!vmd)
		return -ENOMEM;

	vmd->dev = dev;
	vmd->instance = ida_simple_get(&vmd_instance_ida, 0, 0, GFP_KERNEL);
	if (vmd->instance < 0)
		return vmd->instance;

	vmd->name = kasprintf(GFP_KERNEL, "vmd%d", vmd->instance);
	if (!vmd->name) {
		err = -ENOMEM;
		goto out_release_instance;
	}

	err = pcim_enable_device(dev);
	if (err < 0)
		goto out_release_instance;

	vmd->cfgbar = pcim_iomap(dev, VMD_CFGBAR, 0);
	if (!vmd->cfgbar) {
		err = -ENOMEM;
		goto out_release_instance;
	}

	pci_set_master(dev);
	if (dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(64)) &&
	    dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(32))) {
		err = -ENODEV;
		goto out_release_instance;
	}

	if (features & VMD_FEAT_OFFSET_FIRST_VECTOR)
		vmd->first_vec = 1;

	spin_lock_init(&vmd->cfg_lock);
	pci_set_drvdata(dev, vmd);
	err = vmd_enable_domain(vmd, features);
	if (err)
		goto out_release_instance;

	dev_info(&vmd->dev->dev, "Bound to PCI domain %04x\n",
		 vmd->sysdata.domain);
	return 0;

 out_release_instance:
	ida_simple_remove(&vmd_instance_ida, vmd->instance);
	kfree(vmd->name);
	return err;
}

static void vmd_cleanup_srcu(struct vmd_dev *vmd)
{
	int i;

	for (i = 0; i < vmd->msix_count; i++)
		cleanup_srcu_struct(&vmd->irqs[i].srcu);
}

static void vmd_remove(struct pci_dev *dev)
{
	struct vmd_dev *vmd = pci_get_drvdata(dev);

	sysfs_remove_link(&vmd->dev->dev.kobj, "domain");
	pci_stop_root_bus(vmd->bus);
	pci_remove_root_bus(vmd->bus);
	vmd_cleanup_srcu(vmd);
	vmd_detach_resources(vmd);
	vmd_remove_irq_domain(vmd);
	ida_simple_remove(&vmd_instance_ida, vmd->instance);
	kfree(vmd->name);
}

#ifdef CONFIG_PM_SLEEP
static int vmd_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct vmd_dev *vmd = pci_get_drvdata(pdev);
	int i;

	for (i = 0; i < vmd->msix_count; i++)
		devm_free_irq(dev, pci_irq_vector(pdev, i), &vmd->irqs[i]);

	return 0;
}

static int vmd_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct vmd_dev *vmd = pci_get_drvdata(pdev);
	int err, i;

	for (i = 0; i < vmd->msix_count; i++) {
		err = devm_request_irq(dev, pci_irq_vector(pdev, i),
				       vmd_irq, IRQF_NO_THREAD,
				       vmd->name, &vmd->irqs[i]);
		if (err)
			return err;
	}

	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(vmd_dev_pm_ops, vmd_suspend, vmd_resume);

static const struct pci_device_id vmd_ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_VMD_201D),
		.driver_data = VMD_FEAT_HAS_MEMBAR_SHADOW_VSCAP,},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_VMD_28C0),
		.driver_data = VMD_FEAT_HAS_MEMBAR_SHADOW |
				VMD_FEAT_HAS_BUS_RESTRICTIONS |
				VMD_FEAT_CAN_BYPASS_MSI_REMAP,},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x467f),
		.driver_data = VMD_FEAT_HAS_MEMBAR_SHADOW_VSCAP |
				VMD_FEAT_HAS_BUS_RESTRICTIONS |
				VMD_FEAT_OFFSET_FIRST_VECTOR,},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x4c3d),
		.driver_data = VMD_FEAT_HAS_MEMBAR_SHADOW_VSCAP |
				VMD_FEAT_HAS_BUS_RESTRICTIONS |
				VMD_FEAT_OFFSET_FIRST_VECTOR,},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_VMD_9A0B),
		.driver_data = VMD_FEAT_HAS_MEMBAR_SHADOW_VSCAP |
				VMD_FEAT_HAS_BUS_RESTRICTIONS |
				VMD_FEAT_OFFSET_FIRST_VECTOR,},
	{0,}
};
MODULE_DEVICE_TABLE(pci, vmd_ids);

static struct pci_driver vmd_drv = {
	.name		= "vmd",
	.id_table	= vmd_ids,
	.probe		= vmd_probe,
	.remove		= vmd_remove,
	.driver		= {
		.pm	= &vmd_dev_pm_ops,
	},
};
module_pci_driver(vmd_drv);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.6");
