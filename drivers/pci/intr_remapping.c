#include <linux/dmar.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/pci.h>
#include <asm/io_apic.h>
#include "intel-iommu.h"
#include "intr_remapping.h"

static struct ioapic_scope ir_ioapic[MAX_IO_APICS];
static int ir_ioapic_num;
int intr_remapping_enabled;

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
