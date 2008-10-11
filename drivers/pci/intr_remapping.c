#include <linux/dmar.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <asm/io_apic.h>
#include "intel-iommu.h"
#include "intr_remapping.h"

static struct ioapic_scope ir_ioapic[MAX_IO_APICS];
static int ir_ioapic_num;
int intr_remapping_enabled;

static struct {
	struct intel_iommu *iommu;
	u16 irte_index;
	u16 sub_handle;
	u8  irte_mask;
} irq_2_iommu[NR_IRQS];

static DEFINE_SPINLOCK(irq_2_ir_lock);

int irq_remapped(int irq)
{
	if (irq > NR_IRQS)
		return 0;

	if (!irq_2_iommu[irq].iommu)
		return 0;

	return 1;
}

int get_irte(int irq, struct irte *entry)
{
	int index;

	if (!entry || irq > NR_IRQS)
		return -1;

	spin_lock(&irq_2_ir_lock);
	if (!irq_2_iommu[irq].iommu) {
		spin_unlock(&irq_2_ir_lock);
		return -1;
	}

	index = irq_2_iommu[irq].irte_index + irq_2_iommu[irq].sub_handle;
	*entry = *(irq_2_iommu[irq].iommu->ir_table->base + index);

	spin_unlock(&irq_2_ir_lock);
	return 0;
}

int alloc_irte(struct intel_iommu *iommu, int irq, u16 count)
{
	struct ir_table *table = iommu->ir_table;
	u16 index, start_index;
	unsigned int mask = 0;
	int i;

	if (!count)
		return -1;

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

	spin_lock(&irq_2_ir_lock);
	do {
		for (i = index; i < index + count; i++)
			if  (table->base[i].present)
				break;
		/* empty index found */
		if (i == index + count)
			break;

		index = (index + count) % INTR_REMAP_TABLE_ENTRIES;

		if (index == start_index) {
			spin_unlock(&irq_2_ir_lock);
			printk(KERN_ERR "can't allocate an IRTE\n");
			return -1;
		}
	} while (1);

	for (i = index; i < index + count; i++)
		table->base[i].present = 1;

	irq_2_iommu[irq].iommu = iommu;
	irq_2_iommu[irq].irte_index =  index;
	irq_2_iommu[irq].sub_handle = 0;
	irq_2_iommu[irq].irte_mask = mask;

	spin_unlock(&irq_2_ir_lock);

	return index;
}

static void qi_flush_iec(struct intel_iommu *iommu, int index, int mask)
{
	struct qi_desc desc;

	desc.low = QI_IEC_IIDEX(index) | QI_IEC_TYPE | QI_IEC_IM(mask)
		   | QI_IEC_SELECTIVE;
	desc.high = 0;

	qi_submit_sync(&desc, iommu);
}

int map_irq_to_irte_handle(int irq, u16 *sub_handle)
{
	int index;

	spin_lock(&irq_2_ir_lock);
	if (irq >= NR_IRQS || !irq_2_iommu[irq].iommu) {
		spin_unlock(&irq_2_ir_lock);
		return -1;
	}

	*sub_handle = irq_2_iommu[irq].sub_handle;
	index = irq_2_iommu[irq].irte_index;
	spin_unlock(&irq_2_ir_lock);
	return index;
}

int set_irte_irq(int irq, struct intel_iommu *iommu, u16 index, u16 subhandle)
{
	spin_lock(&irq_2_ir_lock);
	if (irq >= NR_IRQS || irq_2_iommu[irq].iommu) {
		spin_unlock(&irq_2_ir_lock);
		return -1;
	}

	irq_2_iommu[irq].iommu = iommu;
	irq_2_iommu[irq].irte_index = index;
	irq_2_iommu[irq].sub_handle = subhandle;
	irq_2_iommu[irq].irte_mask = 0;

	spin_unlock(&irq_2_ir_lock);

	return 0;
}

int clear_irte_irq(int irq, struct intel_iommu *iommu, u16 index)
{
	spin_lock(&irq_2_ir_lock);
	if (irq >= NR_IRQS || !irq_2_iommu[irq].iommu) {
		spin_unlock(&irq_2_ir_lock);
		return -1;
	}

	irq_2_iommu[irq].iommu = NULL;
	irq_2_iommu[irq].irte_index = 0;
	irq_2_iommu[irq].sub_handle = 0;
	irq_2_iommu[irq].irte_mask = 0;

	spin_unlock(&irq_2_ir_lock);

	return 0;
}

int modify_irte(int irq, struct irte *irte_modified)
{
	int index;
	struct irte *irte;
	struct intel_iommu *iommu;

	spin_lock(&irq_2_ir_lock);
	if (irq >= NR_IRQS || !irq_2_iommu[irq].iommu) {
		spin_unlock(&irq_2_ir_lock);
		return -1;
	}

	iommu = irq_2_iommu[irq].iommu;

	index = irq_2_iommu[irq].irte_index + irq_2_iommu[irq].sub_handle;
	irte = &iommu->ir_table->base[index];

	set_64bit((unsigned long *)irte, irte_modified->low | (1 << 1));
	__iommu_flush_cache(iommu, irte, sizeof(*irte));

	qi_flush_iec(iommu, index, 0);

	spin_unlock(&irq_2_ir_lock);
	return 0;
}

int flush_irte(int irq)
{
	int index;
	struct intel_iommu *iommu;

	spin_lock(&irq_2_ir_lock);
	if (irq >= NR_IRQS || !irq_2_iommu[irq].iommu) {
		spin_unlock(&irq_2_ir_lock);
		return -1;
	}

	iommu = irq_2_iommu[irq].iommu;

	index = irq_2_iommu[irq].irte_index + irq_2_iommu[irq].sub_handle;

	qi_flush_iec(iommu, index, irq_2_iommu[irq].irte_mask);
	spin_unlock(&irq_2_ir_lock);

	return 0;
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

int free_irte(int irq)
{
	int index, i;
	struct irte *irte;
	struct intel_iommu *iommu;

	spin_lock(&irq_2_ir_lock);
	if (irq >= NR_IRQS || !irq_2_iommu[irq].iommu) {
		spin_unlock(&irq_2_ir_lock);
		return -1;
	}

	iommu = irq_2_iommu[irq].iommu;

	index = irq_2_iommu[irq].irte_index + irq_2_iommu[irq].sub_handle;
	irte = &iommu->ir_table->base[index];

	if (!irq_2_iommu[irq].sub_handle) {
		for (i = 0; i < (1 << irq_2_iommu[irq].irte_mask); i++)
			set_64bit((unsigned long *)irte, 0);
		qi_flush_iec(iommu, index, irq_2_iommu[irq].irte_mask);
	}

	irq_2_iommu[irq].iommu = NULL;
	irq_2_iommu[irq].irte_index = 0;
	irq_2_iommu[irq].sub_handle = 0;
	irq_2_iommu[irq].irte_mask = 0;

	spin_unlock(&irq_2_ir_lock);

	return 0;
}

static void iommu_set_intr_remapping(struct intel_iommu *iommu, int mode)
{
	u64 addr;
	u32 cmd, sts;
	unsigned long flags;

	addr = virt_to_phys((void *)iommu->ir_table->base);

	spin_lock_irqsave(&iommu->register_lock, flags);

	dmar_writeq(iommu->reg + DMAR_IRTA_REG,
		    (addr) | IR_X2APIC_MODE(mode) | INTR_REMAP_TABLE_REG_SIZE);

	/* Set interrupt-remapping table pointer */
	cmd = iommu->gcmd | DMA_GCMD_SIRTP;
	writel(cmd, iommu->reg + DMAR_GCMD_REG);

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
	cmd = iommu->gcmd | DMA_GCMD_IRE;
	iommu->gcmd |= DMA_GCMD_IRE;
	writel(cmd, iommu->reg + DMAR_GCMD_REG);

	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
		      readl, (sts & DMA_GSTS_IRES), sts);

	spin_unlock_irqrestore(&iommu->register_lock, flags);
}


static int setup_intr_remapping(struct intel_iommu *iommu, int mode)
{
	struct ir_table *ir_table;
	struct page *pages;

	ir_table = iommu->ir_table = kzalloc(sizeof(struct ir_table),
					     GFP_KERNEL);

	if (!iommu->ir_table)
		return -ENOMEM;

	pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, INTR_REMAP_PAGE_ORDER);

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

int __init enable_intr_remapping(int eim)
{
	struct dmar_drhd_unit *drhd;
	int setup = 0;

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

			ir_ioapic[ir_ioapic_num].iommu = iommu;
			ir_ioapic[ir_ioapic_num].id = scope->enumeration_id;
			ir_ioapic_num++;
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
