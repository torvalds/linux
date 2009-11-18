#include <linux/interrupt.h>
#include <linux/dmar.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <asm/io_apic.h>
#include <asm/smp.h>
#include <asm/cpu.h>
#include <linux/intel-iommu.h>
#include "intr_remapping.h"
#include <acpi/acpi.h>
#include <asm/pci-direct.h>
#include "pci.h"

static struct ioapic_scope ir_ioapic[MAX_IO_APICS];
static int ir_ioapic_num;
int intr_remapping_enabled;

static int disable_intremap;
static __init int setup_nointremap(char *str)
{
	disable_intremap = 1;
	return 0;
}
early_param("nointremap", setup_nointremap);

struct irq_2_iommu {
	struct intel_iommu *iommu;
	u16 irte_index;
	u16 sub_handle;
	u8  irte_mask;
};

#ifdef CONFIG_GENERIC_HARDIRQS
static struct irq_2_iommu *get_one_free_irq_2_iommu(int node)
{
	struct irq_2_iommu *iommu;

	iommu = kzalloc_node(sizeof(*iommu), GFP_ATOMIC, node);
	printk(KERN_DEBUG "alloc irq_2_iommu on node %d\n", node);

	return iommu;
}

static struct irq_2_iommu *irq_2_iommu(unsigned int irq)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);

	if (WARN_ON_ONCE(!desc))
		return NULL;

	return desc->irq_2_iommu;
}

static struct irq_2_iommu *irq_2_iommu_alloc(unsigned int irq)
{
	struct irq_desc *desc;
	struct irq_2_iommu *irq_iommu;

	desc = irq_to_desc(irq);
	if (!desc) {
		printk(KERN_INFO "can not get irq_desc for %d\n", irq);
		return NULL;
	}

	irq_iommu = desc->irq_2_iommu;

	if (!irq_iommu)
		desc->irq_2_iommu = get_one_free_irq_2_iommu(irq_node(irq));

	return desc->irq_2_iommu;
}

#else /* !CONFIG_SPARSE_IRQ */

static struct irq_2_iommu irq_2_iommuX[NR_IRQS];

static struct irq_2_iommu *irq_2_iommu(unsigned int irq)
{
	if (irq < nr_irqs)
		return &irq_2_iommuX[irq];

	return NULL;
}
static struct irq_2_iommu *irq_2_iommu_alloc(unsigned int irq)
{
	return irq_2_iommu(irq);
}
#endif

static DEFINE_SPINLOCK(irq_2_ir_lock);

static struct irq_2_iommu *valid_irq_2_iommu(unsigned int irq)
{
	struct irq_2_iommu *irq_iommu;

	irq_iommu = irq_2_iommu(irq);

	if (!irq_iommu)
		return NULL;

	if (!irq_iommu->iommu)
		return NULL;

	return irq_iommu;
}

int irq_remapped(int irq)
{
	return valid_irq_2_iommu(irq) != NULL;
}

int get_irte(int irq, struct irte *entry)
{
	int index;
	struct irq_2_iommu *irq_iommu;
	unsigned long flags;

	if (!entry)
		return -1;

	spin_lock_irqsave(&irq_2_ir_lock, flags);
	irq_iommu = valid_irq_2_iommu(irq);
	if (!irq_iommu) {
		spin_unlock_irqrestore(&irq_2_ir_lock, flags);
		return -1;
	}

	index = irq_iommu->irte_index + irq_iommu->sub_handle;
	*entry = *(irq_iommu->iommu->ir_table->base + index);

	spin_unlock_irqrestore(&irq_2_ir_lock, flags);
	return 0;
}

int alloc_irte(struct intel_iommu *iommu, int irq, u16 count)
{
	struct ir_table *table = iommu->ir_table;
	struct irq_2_iommu *irq_iommu;
	u16 index, start_index;
	unsigned int mask = 0;
	unsigned long flags;
	int i;

	if (!count)
		return -1;

#ifndef CONFIG_SPARSE_IRQ
	/* protect irq_2_iommu_alloc later */
	if (irq >= nr_irqs)
		return -1;
#endif

	/*
	 * start the IRTE search from index 0.
	 */
	index = start_index = 0;

	if (count > 1) {
		count = __roundup_pow_of_two(count);
		mask = ilog2(count);
	}

	if (mask > ecap_max_handle_mask(iommu->ecap)) {
		printk(KERN_ERR
		       "Requested mask %x exceeds the max invalidation handle"
		       " mask value %Lx\n", mask,
		       ecap_max_handle_mask(iommu->ecap));
		return -1;
	}

	spin_lock_irqsave(&irq_2_ir_lock, flags);
	do {
		for (i = index; i < index + count; i++)
			if  (table->base[i].present)
				break;
		/* empty index found */
		if (i == index + count)
			break;

		index = (index + count) % INTR_REMAP_TABLE_ENTRIES;

		if (index == start_index) {
			spin_unlock_irqrestore(&irq_2_ir_lock, flags);
			printk(KERN_ERR "can't allocate an IRTE\n");
			return -1;
		}
	} while (1);

	for (i = index; i < index + count; i++)
		table->base[i].present = 1;

	irq_iommu = irq_2_iommu_alloc(irq);
	if (!irq_iommu) {
		spin_unlock_irqrestore(&irq_2_ir_lock, flags);
		printk(KERN_ERR "can't allocate irq_2_iommu\n");
		return -1;
	}

	irq_iommu->iommu = iommu;
	irq_iommu->irte_index =  index;
	irq_iommu->sub_handle = 0;
	irq_iommu->irte_mask = mask;

	spin_unlock_irqrestore(&irq_2_ir_lock, flags);

	return index;
}

static int qi_flush_iec(struct intel_iommu *iommu, int index, int mask)
{
	struct qi_desc desc;

	desc.low = QI_IEC_IIDEX(index) | QI_IEC_TYPE | QI_IEC_IM(mask)
		   | QI_IEC_SELECTIVE;
	desc.high = 0;

	return qi_submit_sync(&desc, iommu);
}

int map_irq_to_irte_handle(int irq, u16 *sub_handle)
{
	int index;
	struct irq_2_iommu *irq_iommu;
	unsigned long flags;

	spin_lock_irqsave(&irq_2_ir_lock, flags);
	irq_iommu = valid_irq_2_iommu(irq);
	if (!irq_iommu) {
		spin_unlock_irqrestore(&irq_2_ir_lock, flags);
		return -1;
	}

	*sub_handle = irq_iommu->sub_handle;
	index = irq_iommu->irte_index;
	spin_unlock_irqrestore(&irq_2_ir_lock, flags);
	return index;
}

int set_irte_irq(int irq, struct intel_iommu *iommu, u16 index, u16 subhandle)
{
	struct irq_2_iommu *irq_iommu;
	unsigned long flags;

	spin_lock_irqsave(&irq_2_ir_lock, flags);

	irq_iommu = irq_2_iommu_alloc(irq);

	if (!irq_iommu) {
		spin_unlock_irqrestore(&irq_2_ir_lock, flags);
		printk(KERN_ERR "can't allocate irq_2_iommu\n");
		return -1;
	}

	irq_iommu->iommu = iommu;
	irq_iommu->irte_index = index;
	irq_iommu->sub_handle = subhandle;
	irq_iommu->irte_mask = 0;

	spin_unlock_irqrestore(&irq_2_ir_lock, flags);

	return 0;
}

int clear_irte_irq(int irq, struct intel_iommu *iommu, u16 index)
{
	struct irq_2_iommu *irq_iommu;
	unsigned long flags;

	spin_lock_irqsave(&irq_2_ir_lock, flags);
	irq_iommu = valid_irq_2_iommu(irq);
	if (!irq_iommu) {
		spin_unlock_irqrestore(&irq_2_ir_lock, flags);
		return -1;
	}

	irq_iommu->iommu = NULL;
	irq_iommu->irte_index = 0;
	irq_iommu->sub_handle = 0;
	irq_2_iommu(irq)->irte_mask = 0;

	spin_unlock_irqrestore(&irq_2_ir_lock, flags);

	return 0;
}

int modify_irte(int irq, struct irte *irte_modified)
{
	int rc;
	int index;
	struct irte *irte;
	struct intel_iommu *iommu;
	struct irq_2_iommu *irq_iommu;
	unsigned long flags;

	spin_lock_irqsave(&irq_2_ir_lock, flags);
	irq_iommu = valid_irq_2_iommu(irq);
	if (!irq_iommu) {
		spin_unlock_irqrestore(&irq_2_ir_lock, flags);
		return -1;
	}

	iommu = irq_iommu->iommu;

	index = irq_iommu->irte_index + irq_iommu->sub_handle;
	irte = &iommu->ir_table->base[index];

	set_64bit((unsigned long *)&irte->low, irte_modified->low);
	set_64bit((unsigned long *)&irte->high, irte_modified->high);
	__iommu_flush_cache(iommu, irte, sizeof(*irte));

	rc = qi_flush_iec(iommu, index, 0);
	spin_unlock_irqrestore(&irq_2_ir_lock, flags);

	return rc;
}

int flush_irte(int irq)
{
	int rc;
	int index;
	struct intel_iommu *iommu;
	struct irq_2_iommu *irq_iommu;
	unsigned long flags;

	spin_lock_irqsave(&irq_2_ir_lock, flags);
	irq_iommu = valid_irq_2_iommu(irq);
	if (!irq_iommu) {
		spin_unlock_irqrestore(&irq_2_ir_lock, flags);
		return -1;
	}

	iommu = irq_iommu->iommu;

	index = irq_iommu->irte_index + irq_iommu->sub_handle;

	rc = qi_flush_iec(iommu, index, irq_iommu->irte_mask);
	spin_unlock_irqrestore(&irq_2_ir_lock, flags);

	return rc;
}

struct intel_iommu *map_ioapic_to_ir(int apic)
{
	int i;

	for (i = 0; i < MAX_IO_APICS; i++)
		if (ir_ioapic[i].id == apic)
			return ir_ioapic[i].iommu;
	return NULL;
}

struct intel_iommu *map_dev_to_ir(struct pci_dev *dev)
{
	struct dmar_drhd_unit *drhd;

	drhd = dmar_find_matched_drhd_unit(dev);
	if (!drhd)
		return NULL;

	return drhd->iommu;
}

static int clear_entries(struct irq_2_iommu *irq_iommu)
{
	struct irte *start, *entry, *end;
	struct intel_iommu *iommu;
	int index;

	if (irq_iommu->sub_handle)
		return 0;

	iommu = irq_iommu->iommu;
	index = irq_iommu->irte_index + irq_iommu->sub_handle;

	start = iommu->ir_table->base + index;
	end = start + (1 << irq_iommu->irte_mask);

	for (entry = start; entry < end; entry++) {
		set_64bit((unsigned long *)&entry->low, 0);
		set_64bit((unsigned long *)&entry->high, 0);
	}

	return qi_flush_iec(iommu, index, irq_iommu->irte_mask);
}

int free_irte(int irq)
{
	int rc = 0;
	struct irq_2_iommu *irq_iommu;
	unsigned long flags;

	spin_lock_irqsave(&irq_2_ir_lock, flags);
	irq_iommu = valid_irq_2_iommu(irq);
	if (!irq_iommu) {
		spin_unlock_irqrestore(&irq_2_ir_lock, flags);
		return -1;
	}

	rc = clear_entries(irq_iommu);

	irq_iommu->iommu = NULL;
	irq_iommu->irte_index = 0;
	irq_iommu->sub_handle = 0;
	irq_iommu->irte_mask = 0;

	spin_unlock_irqrestore(&irq_2_ir_lock, flags);

	return rc;
}

/*
 * source validation type
 */
#define SVT_NO_VERIFY		0x0  /* no verification is required */
#define SVT_VERIFY_SID_SQ	0x1  /* verify using SID and SQ fiels */
#define SVT_VERIFY_BUS		0x2  /* verify bus of request-id */

/*
 * source-id qualifier
 */
#define SQ_ALL_16	0x0  /* verify all 16 bits of request-id */
#define SQ_13_IGNORE_1	0x1  /* verify most significant 13 bits, ignore
			      * the third least significant bit
			      */
#define SQ_13_IGNORE_2	0x2  /* verify most significant 13 bits, ignore
			      * the second and third least significant bits
			      */
#define SQ_13_IGNORE_3	0x3  /* verify most significant 13 bits, ignore
			      * the least three significant bits
			      */

/*
 * set SVT, SQ and SID fields of irte to verify
 * source ids of interrupt requests
 */
static void set_irte_sid(struct irte *irte, unsigned int svt,
			 unsigned int sq, unsigned int sid)
{
	irte->svt = svt;
	irte->sq = sq;
	irte->sid = sid;
}

int set_ioapic_sid(struct irte *irte, int apic)
{
	int i;
	u16 sid = 0;

	if (!irte)
		return -1;

	for (i = 0; i < MAX_IO_APICS; i++) {
		if (ir_ioapic[i].id == apic) {
			sid = (ir_ioapic[i].bus << 8) | ir_ioapic[i].devfn;
			break;
		}
	}

	if (sid == 0) {
		pr_warning("Failed to set source-id of IOAPIC (%d)\n", apic);
		return -1;
	}

	set_irte_sid(irte, 1, 0, sid);

	return 0;
}

int set_msi_sid(struct irte *irte, struct pci_dev *dev)
{
	struct pci_dev *bridge;

	if (!irte || !dev)
		return -1;

	/* PCIe device or Root Complex integrated PCI device */
	if (dev->is_pcie || !dev->bus->parent) {
		set_irte_sid(irte, SVT_VERIFY_SID_SQ, SQ_ALL_16,
			     (dev->bus->number << 8) | dev->devfn);
		return 0;
	}

	bridge = pci_find_upstream_pcie_bridge(dev);
	if (bridge) {
		if (bridge->is_pcie) /* this is a PCIE-to-PCI/PCIX bridge */
			set_irte_sid(irte, SVT_VERIFY_BUS, SQ_ALL_16,
				(bridge->bus->number << 8) | dev->bus->number);
		else /* this is a legacy PCI bridge */
			set_irte_sid(irte, SVT_VERIFY_SID_SQ, SQ_ALL_16,
				(bridge->bus->number << 8) | bridge->devfn);
	}

	return 0;
}

static void iommu_set_intr_remapping(struct intel_iommu *iommu, int mode)
{
	u64 addr;
	u32 sts;
	unsigned long flags;

	addr = virt_to_phys((void *)iommu->ir_table->base);

	spin_lock_irqsave(&iommu->register_lock, flags);

	dmar_writeq(iommu->reg + DMAR_IRTA_REG,
		    (addr) | IR_X2APIC_MODE(mode) | INTR_REMAP_TABLE_REG_SIZE);

	/* Set interrupt-remapping table pointer */
	iommu->gcmd |= DMA_GCMD_SIRTP;
	writel(iommu->gcmd, iommu->reg + DMAR_GCMD_REG);

	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
		      readl, (sts & DMA_GSTS_IRTPS), sts);
	spin_unlock_irqrestore(&iommu->register_lock, flags);

	/*
	 * global invalidation of interrupt entry cache before enabling
	 * interrupt-remapping.
	 */
	qi_global_iec(iommu);

	spin_lock_irqsave(&iommu->register_lock, flags);

	/* Enable interrupt-remapping */
	iommu->gcmd |= DMA_GCMD_IRE;
	writel(iommu->gcmd, iommu->reg + DMAR_GCMD_REG);

	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
		      readl, (sts & DMA_GSTS_IRES), sts);

	spin_unlock_irqrestore(&iommu->register_lock, flags);
}


static int setup_intr_remapping(struct intel_iommu *iommu, int mode)
{
	struct ir_table *ir_table;
	struct page *pages;

	ir_table = iommu->ir_table = kzalloc(sizeof(struct ir_table),
					     GFP_ATOMIC);

	if (!iommu->ir_table)
		return -ENOMEM;

	pages = alloc_pages(GFP_ATOMIC | __GFP_ZERO, INTR_REMAP_PAGE_ORDER);

	if (!pages) {
		printk(KERN_ERR "failed to allocate pages of order %d\n",
		       INTR_REMAP_PAGE_ORDER);
		kfree(iommu->ir_table);
		return -ENOMEM;
	}

	ir_table->base = page_address(pages);

	iommu_set_intr_remapping(iommu, mode);
	return 0;
}

/*
 * Disable Interrupt Remapping.
 */
static void iommu_disable_intr_remapping(struct intel_iommu *iommu)
{
	unsigned long flags;
	u32 sts;

	if (!ecap_ir_support(iommu->ecap))
		return;

	/*
	 * global invalidation of interrupt entry cache before disabling
	 * interrupt-remapping.
	 */
	qi_global_iec(iommu);

	spin_lock_irqsave(&iommu->register_lock, flags);

	sts = dmar_readq(iommu->reg + DMAR_GSTS_REG);
	if (!(sts & DMA_GSTS_IRES))
		goto end;

	iommu->gcmd &= ~DMA_GCMD_IRE;
	writel(iommu->gcmd, iommu->reg + DMAR_GCMD_REG);

	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
		      readl, !(sts & DMA_GSTS_IRES), sts);

end:
	spin_unlock_irqrestore(&iommu->register_lock, flags);
}

int __init intr_remapping_supported(void)
{
	struct dmar_drhd_unit *drhd;

	if (disable_intremap)
		return 0;

	if (!dmar_ir_support())
		return 0;

	for_each_drhd_unit(drhd) {
		struct intel_iommu *iommu = drhd->iommu;

		if (!ecap_ir_support(iommu->ecap))
			return 0;
	}

	return 1;
}

int __init enable_intr_remapping(int eim)
{
	struct dmar_drhd_unit *drhd;
	int setup = 0;

	if (parse_ioapics_under_ir() != 1) {
		printk(KERN_INFO "Not enable interrupt remapping\n");
		return -1;
	}

	for_each_drhd_unit(drhd) {
		struct intel_iommu *iommu = drhd->iommu;

		/*
		 * If the queued invalidation is already initialized,
		 * shouldn't disable it.
		 */
		if (iommu->qi)
			continue;

		/*
		 * Clear previous faults.
		 */
		dmar_fault(-1, iommu);

		/*
		 * Disable intr remapping and queued invalidation, if already
		 * enabled prior to OS handover.
		 */
		iommu_disable_intr_remapping(iommu);

		dmar_disable_qi(iommu);
	}

	/*
	 * check for the Interrupt-remapping support
	 */
	for_each_drhd_unit(drhd) {
		struct intel_iommu *iommu = drhd->iommu;

		if (!ecap_ir_support(iommu->ecap))
			continue;

		if (eim && !ecap_eim_support(iommu->ecap)) {
			printk(KERN_INFO "DRHD %Lx: EIM not supported by DRHD, "
			       " ecap %Lx\n", drhd->reg_base_addr, iommu->ecap);
			return -1;
		}
	}

	/*
	 * Enable queued invalidation for all the DRHD's.
	 */
	for_each_drhd_unit(drhd) {
		int ret;
		struct intel_iommu *iommu = drhd->iommu;
		ret = dmar_enable_qi(iommu);

		if (ret) {
			printk(KERN_ERR "DRHD %Lx: failed to enable queued, "
			       " invalidation, ecap %Lx, ret %d\n",
			       drhd->reg_base_addr, iommu->ecap, ret);
			return -1;
		}
	}

	/*
	 * Setup Interrupt-remapping for all the DRHD's now.
	 */
	for_each_drhd_unit(drhd) {
		struct intel_iommu *iommu = drhd->iommu;

		if (!ecap_ir_support(iommu->ecap))
			continue;

		if (setup_intr_remapping(iommu, eim))
			goto error;

		setup = 1;
	}

	if (!setup)
		goto error;

	intr_remapping_enabled = 1;

	return 0;

error:
	/*
	 * handle error condition gracefully here!
	 */
	return -1;
}

static void ir_parse_one_ioapic_scope(struct acpi_dmar_device_scope *scope,
				      struct intel_iommu *iommu)
{
	struct acpi_dmar_pci_path *path;
	u8 bus;
	int count;

	bus = scope->bus;
	path = (struct acpi_dmar_pci_path *)(scope + 1);
	count = (scope->length - sizeof(struct acpi_dmar_device_scope))
		/ sizeof(struct acpi_dmar_pci_path);

	while (--count > 0) {
		/*
		 * Access PCI directly due to the PCI
		 * subsystem isn't initialized yet.
		 */
		bus = read_pci_config_byte(bus, path->dev, path->fn,
					   PCI_SECONDARY_BUS);
		path++;
	}

	ir_ioapic[ir_ioapic_num].bus   = bus;
	ir_ioapic[ir_ioapic_num].devfn = PCI_DEVFN(path->dev, path->fn);
	ir_ioapic[ir_ioapic_num].iommu = iommu;
	ir_ioapic[ir_ioapic_num].id    = scope->enumeration_id;
	ir_ioapic_num++;
}

static int ir_parse_ioapic_scope(struct acpi_dmar_header *header,
				 struct intel_iommu *iommu)
{
	struct acpi_dmar_hardware_unit *drhd;
	struct acpi_dmar_device_scope *scope;
	void *start, *end;

	drhd = (struct acpi_dmar_hardware_unit *)header;

	start = (void *)(drhd + 1);
	end = ((void *)drhd) + header->length;

	while (start < end) {
		scope = start;
		if (scope->entry_type == ACPI_DMAR_SCOPE_TYPE_IOAPIC) {
			if (ir_ioapic_num == MAX_IO_APICS) {
				printk(KERN_WARNING "Exceeded Max IO APICS\n");
				return -1;
			}

			printk(KERN_INFO "IOAPIC id %d under DRHD base"
			       " 0x%Lx\n", scope->enumeration_id,
			       drhd->address);

			ir_parse_one_ioapic_scope(scope, iommu);
		}
		start += scope->length;
	}

	return 0;
}

/*
 * Finds the assocaition between IOAPIC's and its Interrupt-remapping
 * hardware unit.
 */
int __init parse_ioapics_under_ir(void)
{
	struct dmar_drhd_unit *drhd;
	int ir_supported = 0;

	for_each_drhd_unit(drhd) {
		struct intel_iommu *iommu = drhd->iommu;

		if (ecap_ir_support(iommu->ecap)) {
			if (ir_parse_ioapic_scope(drhd->hdr, iommu))
				return -1;

			ir_supported = 1;
		}
	}

	if (ir_supported && ir_ioapic_num != nr_ioapics) {
		printk(KERN_WARNING
		       "Not all IO-APIC's listed under remapping hardware\n");
		return -1;
	}

	return ir_supported;
}

void disable_intr_remapping(void)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu = NULL;

	/*
	 * Disable Interrupt-remapping for all the DRHD's now.
	 */
	for_each_iommu(iommu, drhd) {
		if (!ecap_ir_support(iommu->ecap))
			continue;

		iommu_disable_intr_remapping(iommu);
	}
}

int reenable_intr_remapping(int eim)
{
	struct dmar_drhd_unit *drhd;
	int setup = 0;
	struct intel_iommu *iommu = NULL;

	for_each_iommu(iommu, drhd)
		if (iommu->qi)
			dmar_reenable_qi(iommu);

	/*
	 * Setup Interrupt-remapping for all the DRHD's now.
	 */
	for_each_iommu(iommu, drhd) {
		if (!ecap_ir_support(iommu->ecap))
			continue;

		/* Set up interrupt remapping for iommu.*/
		iommu_set_intr_remapping(iommu, eim);
		setup = 1;
	}

	if (!setup)
		goto error;

	return 0;

error:
	/*
	 * handle error condition gracefully here!
	 */
	return -1;
}

