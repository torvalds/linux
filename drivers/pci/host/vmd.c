// SPDX-License-Identifier: GPL-2.0
/*
 * Volume Management Device driver
 * Copyright (c) 2015, Intel Corporation.
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/srcu.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>

#include <asm/irqdomain.h>
#include <asm/device.h>
#include <asm/msi.h>
#include <asm/msidef.h>

#define VMD_CFGBAR	0
#define VMD_MEMBAR1	2
#define VMD_MEMBAR2	4

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
	char __iomem		*cfgbar;

	int msix_count;
	struct vmd_irq_list	*irqs;

	struct pci_sysdata	sysdata;
	struct resource		resources[3];
	struct irq_domain	*irq_domain;
	struct pci_bus		*bus;

#ifdef CONFIG_X86_DEV_DMA_OPS
	struct dma_map_ops	dma_ops;
	struct dma_domain	dma_domain;
#endif
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

	msg->address_hi = MSI_ADDR_BASE_HI;
	msg->address_lo = MSI_ADDR_BASE_LO |
			  MSI_ADDR_DEST_ID(index_from_irqs(vmd, irq));
	msg->data = 0;
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
	int i, best = 1;
	unsigned long flags;

	if (pci_is_bridge(msi_desc_to_pci_dev(desc)) || vmd->msix_count == 1)
		return &vmd->irqs[0];

	raw_spin_lock_irqsave(&list_lock, flags);
	for (i = 1; i < vmd->msix_count; i++)
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

#ifdef CONFIG_X86_DEV_DMA_OPS
/*
 * VMD replaces the requester ID with its own.  DMA mappings for devices in a
 * VMD domain need to be mapped for the VMD, not the device requiring
 * the mapping.
 */
static struct device *to_vmd_dev(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct vmd_dev *vmd = vmd_from_bus(pdev->bus);

	return &vmd->dev->dev;
}

static const struct dma_map_ops *vmd_dma_ops(struct device *dev)
{
	return get_dma_ops(to_vmd_dev(dev));
}

static void *vmd_alloc(struct device *dev, size_t size, dma_addr_t *addr,
		       gfp_t flag, unsigned long attrs)
{
	return vmd_dma_ops(dev)->alloc(to_vmd_dev(dev), size, addr, flag,
				       attrs);
}

static void vmd_free(struct device *dev, size_t size, void *vaddr,
		     dma_addr_t addr, unsigned long attrs)
{
	return vmd_dma_ops(dev)->free(to_vmd_dev(dev), size, vaddr, addr,
				      attrs);
}

static int vmd_mmap(struct device *dev, struct vm_area_struct *vma,
		    void *cpu_addr, dma_addr_t addr, size_t size,
		    unsigned long attrs)
{
	return vmd_dma_ops(dev)->mmap(to_vmd_dev(dev), vma, cpu_addr, addr,
				      size, attrs);
}

static int vmd_get_sgtable(struct device *dev, struct sg_table *sgt,
			   void *cpu_addr, dma_addr_t addr, size_t size,
			   unsigned long attrs)
{
	return vmd_dma_ops(dev)->get_sgtable(to_vmd_dev(dev), sgt, cpu_addr,
					     addr, size, attrs);
}

static dma_addr_t vmd_map_page(struct device *dev, struct page *page,
			       unsigned long offset, size_t size,
			       enum dma_data_direction dir,
			       unsigned long attrs)
{
	return vmd_dma_ops(dev)->map_page(to_vmd_dev(dev), page, offset, size,
					  dir, attrs);
}

static void vmd_unmap_page(struct device *dev, dma_addr_t addr, size_t size,
			   enum dma_data_direction dir, unsigned long attrs)
{
	vmd_dma_ops(dev)->unmap_page(to_vmd_dev(dev), addr, size, dir, attrs);
}

static int vmd_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		      enum dma_data_direction dir, unsigned long attrs)
{
	return vmd_dma_ops(dev)->map_sg(to_vmd_dev(dev), sg, nents, dir, attrs);
}

static void vmd_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
			 enum dma_data_direction dir, unsigned long attrs)
{
	vmd_dma_ops(dev)->unmap_sg(to_vmd_dev(dev), sg, nents, dir, attrs);
}

static void vmd_sync_single_for_cpu(struct device *dev, dma_addr_t addr,
				    size_t size, enum dma_data_direction dir)
{
	vmd_dma_ops(dev)->sync_single_for_cpu(to_vmd_dev(dev), addr, size, dir);
}

static void vmd_sync_single_for_device(struct device *dev, dma_addr_t addr,
				       size_t size, enum dma_data_direction dir)
{
	vmd_dma_ops(dev)->sync_single_for_device(to_vmd_dev(dev), addr, size,
						 dir);
}

static void vmd_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
				int nents, enum dma_data_direction dir)
{
	vmd_dma_ops(dev)->sync_sg_for_cpu(to_vmd_dev(dev), sg, nents, dir);
}

static void vmd_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
				   int nents, enum dma_data_direction dir)
{
	vmd_dma_ops(dev)->sync_sg_for_device(to_vmd_dev(dev), sg, nents, dir);
}

static int vmd_mapping_error(struct device *dev, dma_addr_t addr)
{
	return vmd_dma_ops(dev)->mapping_error(to_vmd_dev(dev), addr);
}

static int vmd_dma_supported(struct device *dev, u64 mask)
{
	return vmd_dma_ops(dev)->dma_supported(to_vmd_dev(dev), mask);
}

#ifdef ARCH_HAS_DMA_GET_REQUIRED_MASK
static u64 vmd_get_required_mask(struct device *dev)
{
	return vmd_dma_ops(dev)->get_required_mask(to_vmd_dev(dev));
}
#endif

static void vmd_teardown_dma_ops(struct vmd_dev *vmd)
{
	struct dma_domain *domain = &vmd->dma_domain;

	if (get_dma_ops(&vmd->dev->dev))
		del_dma_domain(domain);
}

#define ASSIGN_VMD_DMA_OPS(source, dest, fn)	\
	do {					\
		if (source->fn)			\
			dest->fn = vmd_##fn;	\
	} while (0)

static void vmd_setup_dma_ops(struct vmd_dev *vmd)
{
	const struct dma_map_ops *source = get_dma_ops(&vmd->dev->dev);
	struct dma_map_ops *dest = &vmd->dma_ops;
	struct dma_domain *domain = &vmd->dma_domain;

	domain->domain_nr = vmd->sysdata.domain;
	domain->dma_ops = dest;

	if (!source)
		return;
	ASSIGN_VMD_DMA_OPS(source, dest, alloc);
	ASSIGN_VMD_DMA_OPS(source, dest, free);
	ASSIGN_VMD_DMA_OPS(source, dest, mmap);
	ASSIGN_VMD_DMA_OPS(source, dest, get_sgtable);
	ASSIGN_VMD_DMA_OPS(source, dest, map_page);
	ASSIGN_VMD_DMA_OPS(source, dest, unmap_page);
	ASSIGN_VMD_DMA_OPS(source, dest, map_sg);
	ASSIGN_VMD_DMA_OPS(source, dest, unmap_sg);
	ASSIGN_VMD_DMA_OPS(source, dest, sync_single_for_cpu);
	ASSIGN_VMD_DMA_OPS(source, dest, sync_single_for_device);
	ASSIGN_VMD_DMA_OPS(source, dest, sync_sg_for_cpu);
	ASSIGN_VMD_DMA_OPS(source, dest, sync_sg_for_device);
	ASSIGN_VMD_DMA_OPS(source, dest, mapping_error);
	ASSIGN_VMD_DMA_OPS(source, dest, dma_supported);
#ifdef ARCH_HAS_DMA_GET_REQUIRED_MASK
	ASSIGN_VMD_DMA_OPS(source, dest, get_required_mask);
#endif
	add_dma_domain(domain);
}
#undef ASSIGN_VMD_DMA_OPS
#else
static void vmd_teardown_dma_ops(struct vmd_dev *vmd) {}
static void vmd_setup_dma_ops(struct vmd_dev *vmd) {}
#endif

static char __iomem *vmd_cfg_addr(struct vmd_dev *vmd, struct pci_bus *bus,
				  unsigned int devfn, int reg, int len)
{
	char __iomem *addr = vmd->cfgbar +
			     (bus->number << 20) + (devfn << 12) + reg;

	if ((addr - vmd->cfgbar) + len >=
	    resource_size(&vmd->dev->resource[VMD_CFGBAR]))
		return NULL;

	return addr;
}

/*
 * CPU may deadlock if config space is not serialized on some versions of this
 * hardware, so all config space access is done under a spinlock.
 */
static int vmd_pci_read(struct pci_bus *bus, unsigned int devfn, int reg,
			int len, u32 *value)
{
	struct vmd_dev *vmd = vmd_from_bus(bus);
	char __iomem *addr = vmd_cfg_addr(vmd, bus, devfn, reg, len);
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
	char __iomem *addr = vmd_cfg_addr(vmd, bus, devfn, reg, len);
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

static int vmd_enable_domain(struct vmd_dev *vmd)
{
	struct pci_sysdata *sd = &vmd->sysdata;
	struct fwnode_handle *fn;
	struct resource *res;
	u32 upper_bits;
	unsigned long flags;
	LIST_HEAD(resources);

	res = &vmd->dev->resource[VMD_CFGBAR];
	vmd->resources[0] = (struct resource) {
		.name  = "VMD CFGBAR",
		.start = 0,
		.end   = (resource_size(res) >> 20) - 1,
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
	 * The only way we could use a 64-bit non-prefechable MEMBAR is
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
		.start = res->start + 0x2000,
		.end   = res->end,
		.flags = flags,
		.parent = res,
	};

	sd->vmd_domain = true;
	sd->domain = vmd_find_free_domain();
	if (sd->domain < 0)
		return sd->domain;

	sd->node = pcibus_to_node(vmd->dev->bus);

	fn = irq_domain_alloc_named_id_fwnode("VMD-MSI", vmd->sysdata.domain);
	if (!fn)
		return -ENODEV;

	vmd->irq_domain = pci_msi_create_irq_domain(fn, &vmd_msi_domain_info,
						    x86_vector_domain);
	irq_domain_free_fwnode(fn);
	if (!vmd->irq_domain)
		return -ENODEV;

	pci_add_resource(&resources, &vmd->resources[0]);
	pci_add_resource(&resources, &vmd->resources[1]);
	pci_add_resource(&resources, &vmd->resources[2]);
	vmd->bus = pci_create_root_bus(&vmd->dev->dev, 0, &vmd_ops, sd,
				       &resources);
	if (!vmd->bus) {
		pci_free_resource_list(&resources);
		irq_domain_remove(vmd->irq_domain);
		return -ENODEV;
	}

	vmd_attach_resources(vmd);
	vmd_setup_dma_ops(vmd);
	dev_set_msi_domain(&vmd->bus->dev, vmd->irq_domain);
	pci_rescan_bus(vmd->bus);

	WARN(sysfs_create_link(&vmd->dev->dev.kobj, &vmd->bus->dev.kobj,
			       "domain"), "Can't create symlink to domain\n");
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

static int vmd_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct vmd_dev *vmd;
	int i, err;

	if (resource_size(&dev->resource[VMD_CFGBAR]) < (1 << 20))
		return -ENOMEM;

	vmd = devm_kzalloc(&dev->dev, sizeof(*vmd), GFP_KERNEL);
	if (!vmd)
		return -ENOMEM;

	vmd->dev = dev;
	err = pcim_enable_device(dev);
	if (err < 0)
		return err;

	vmd->cfgbar = pcim_iomap(dev, VMD_CFGBAR, 0);
	if (!vmd->cfgbar)
		return -ENOMEM;

	pci_set_master(dev);
	if (dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(64)) &&
	    dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(32)))
		return -ENODEV;

	vmd->msix_count = pci_msix_vec_count(dev);
	if (vmd->msix_count < 0)
		return -ENODEV;

	vmd->msix_count = pci_alloc_irq_vectors(dev, 1, vmd->msix_count,
					PCI_IRQ_MSIX);
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
				       "vmd", &vmd->irqs[i]);
		if (err)
			return err;
	}

	spin_lock_init(&vmd->cfg_lock);
	pci_set_drvdata(dev, vmd);
	err = vmd_enable_domain(vmd);
	if (err)
		return err;

	dev_info(&vmd->dev->dev, "Bound to PCI domain %04x\n",
		 vmd->sysdata.domain);
	return 0;
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

	vmd_detach_resources(vmd);
	sysfs_remove_link(&vmd->dev->dev.kobj, "domain");
	pci_stop_root_bus(vmd->bus);
	pci_remove_root_bus(vmd->bus);
	vmd_cleanup_srcu(vmd);
	vmd_teardown_dma_ops(vmd);
	irq_domain_remove(vmd->irq_domain);
}

#ifdef CONFIG_PM_SLEEP
static int vmd_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct vmd_dev *vmd = pci_get_drvdata(pdev);
	int i;

	for (i = 0; i < vmd->msix_count; i++)
                devm_free_irq(dev, pci_irq_vector(pdev, i), &vmd->irqs[i]);

	pci_save_state(pdev);
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
				       "vmd", &vmd->irqs[i]);
		if (err)
			return err;
	}

	pci_restore_state(pdev);
	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(vmd_dev_pm_ops, vmd_suspend, vmd_resume);

static const struct pci_device_id vmd_ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x201d),},
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
