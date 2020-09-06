// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt)     "DMAR-IR: " fmt

#include <linux/interrupt.h>
#include <linux/dmar.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/hpet.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/intel-iommu.h>
#include <linux/acpi.h>
#include <linux/irqdomain.h>
#include <linux/crash_dump.h>
#include <asm/io_apic.h>
#include <asm/apic.h>
#include <asm/smp.h>
#include <asm/cpu.h>
#include <asm/irq_remapping.h>
#include <asm/pci-direct.h>
#include <asm/msidef.h>

#include "../irq_remapping.h"

enum irq_mode {
	IRQ_REMAPPING,
	IRQ_POSTING,
};

struct ioapic_scope {
	struct intel_iommu *iommu;
	unsigned int id;
	unsigned int bus;	/* PCI bus number */
	unsigned int devfn;	/* PCI devfn number */
};

struct hpet_scope {
	struct intel_iommu *iommu;
	u8 id;
	unsigned int bus;
	unsigned int devfn;
};

struct irq_2_iommu {
	struct intel_iommu *iommu;
	u16 irte_index;
	u16 sub_handle;
	u8  irte_mask;
	enum irq_mode mode;
};

struct intel_ir_data {
	struct irq_2_iommu			irq_2_iommu;
	struct irte				irte_entry;
	union {
		struct msi_msg			msi_entry;
	};
};

#define IR_X2APIC_MODE(mode) (mode ? (1 << 11) : 0)
#define IRTE_DEST(dest) ((eim_mode) ? dest : dest << 8)

static int __read_mostly eim_mode;
static struct ioapic_scope ir_ioapic[MAX_IO_APICS];
static struct hpet_scope ir_hpet[MAX_HPET_TBS];

/*
 * Lock ordering:
 * ->dmar_global_lock
 *	->irq_2_ir_lock
 *		->qi->q_lock
 *	->iommu->register_lock
 * Note:
 * intel_irq_remap_ops.{supported,prepare,enable,disable,reenable} are called
 * in single-threaded environment with interrupt disabled, so no need to tabke
 * the dmar_global_lock.
 */
DEFINE_RAW_SPINLOCK(irq_2_ir_lock);
static const struct irq_domain_ops intel_ir_domain_ops;

static void iommu_disable_irq_remapping(struct intel_iommu *iommu);
static int __init parse_ioapics_under_ir(void);

static bool ir_pre_enabled(struct intel_iommu *iommu)
{
	return (iommu->flags & VTD_FLAG_IRQ_REMAP_PRE_ENABLED);
}

static void clear_ir_pre_enabled(struct intel_iommu *iommu)
{
	iommu->flags &= ~VTD_FLAG_IRQ_REMAP_PRE_ENABLED;
}

static void init_ir_status(struct intel_iommu *iommu)
{
	u32 gsts;

	gsts = readl(iommu->reg + DMAR_GSTS_REG);
	if (gsts & DMA_GSTS_IRES)
		iommu->flags |= VTD_FLAG_IRQ_REMAP_PRE_ENABLED;
}

static int alloc_irte(struct intel_iommu *iommu,
		      struct irq_2_iommu *irq_iommu, u16 count)
{
	struct ir_table *table = iommu->ir_table;
	unsigned int mask = 0;
	unsigned long flags;
	int index;

	if (!count || !irq_iommu)
		return -1;

	if (count > 1) {
		count = __roundup_pow_of_two(count);
		mask = ilog2(count);
	}

	if (mask > ecap_max_handle_mask(iommu->ecap)) {
		pr_err("Requested mask %x exceeds the max invalidation handle"
		       " mask value %Lx\n", mask,
		       ecap_max_handle_mask(iommu->ecap));
		return -1;
	}

	raw_spin_lock_irqsave(&irq_2_ir_lock, flags);
	index = bitmap_find_free_region(table->bitmap,
					INTR_REMAP_TABLE_ENTRIES, mask);
	if (index < 0) {
		pr_warn("IR%d: can't allocate an IRTE\n", iommu->seq_id);
	} else {
		irq_iommu->iommu = iommu;
		irq_iommu->irte_index =  index;
		irq_iommu->sub_handle = 0;
		irq_iommu->irte_mask = mask;
		irq_iommu->mode = IRQ_REMAPPING;
	}
	raw_spin_unlock_irqrestore(&irq_2_ir_lock, flags);

	return index;
}

static int qi_flush_iec(struct intel_iommu *iommu, int index, int mask)
{
	struct qi_desc desc;

	desc.qw0 = QI_IEC_IIDEX(index) | QI_IEC_TYPE | QI_IEC_IM(mask)
		   | QI_IEC_SELECTIVE;
	desc.qw1 = 0;
	desc.qw2 = 0;
	desc.qw3 = 0;

	return qi_submit_sync(iommu, &desc, 1, 0);
}

static int modify_irte(struct irq_2_iommu *irq_iommu,
		       struct irte *irte_modified)
{
	struct intel_iommu *iommu;
	unsigned long flags;
	struct irte *irte;
	int rc, index;

	if (!irq_iommu)
		return -1;

	raw_spin_lock_irqsave(&irq_2_ir_lock, flags);

	iommu = irq_iommu->iommu;

	index = irq_iommu->irte_index + irq_iommu->sub_handle;
	irte = &iommu->ir_table->base[index];

#if defined(CONFIG_HAVE_CMPXCHG_DOUBLE)
	if ((irte->pst == 1) || (irte_modified->pst == 1)) {
		bool ret;

		ret = cmpxchg_double(&irte->low, &irte->high,
				     irte->low, irte->high,
				     irte_modified->low, irte_modified->high);
		/*
		 * We use cmpxchg16 to atomically update the 128-bit IRTE,
		 * and it cannot be updated by the hardware or other processors
		 * behind us, so the return value of cmpxchg16 should be the
		 * same as the old value.
		 */
		WARN_ON(!ret);
	} else
#endif
	{
		set_64bit(&irte->low, irte_modified->low);
		set_64bit(&irte->high, irte_modified->high);
	}
	__iommu_flush_cache(iommu, irte, sizeof(*irte));

	rc = qi_flush_iec(iommu, index, 0);

	/* Update iommu mode according to the IRTE mode */
	irq_iommu->mode = irte->pst ? IRQ_POSTING : IRQ_REMAPPING;
	raw_spin_unlock_irqrestore(&irq_2_ir_lock, flags);

	return rc;
}

static struct intel_iommu *map_hpet_to_ir(u8 hpet_id)
{
	int i;

	for (i = 0; i < MAX_HPET_TBS; i++)
		if (ir_hpet[i].id == hpet_id && ir_hpet[i].iommu)
			return ir_hpet[i].iommu;
	return NULL;
}

static struct intel_iommu *map_ioapic_to_ir(int apic)
{
	int i;

	for (i = 0; i < MAX_IO_APICS; i++)
		if (ir_ioapic[i].id == apic && ir_ioapic[i].iommu)
			return ir_ioapic[i].iommu;
	return NULL;
}

static struct intel_iommu *map_dev_to_ir(struct pci_dev *dev)
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
	index = irq_iommu->irte_index;

	start = iommu->ir_table->base + index;
	end = start + (1 << irq_iommu->irte_mask);

	for (entry = start; entry < end; entry++) {
		set_64bit(&entry->low, 0);
		set_64bit(&entry->high, 0);
	}
	bitmap_release_region(iommu->ir_table->bitmap, index,
			      irq_iommu->irte_mask);

	return qi_flush_iec(iommu, index, irq_iommu->irte_mask);
}

/*
 * source validation type
 */
#define SVT_NO_VERIFY		0x0  /* no verification is required */
#define SVT_VERIFY_SID_SQ	0x1  /* verify using SID and SQ fields */
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
	if (disable_sourceid_checking)
		svt = SVT_NO_VERIFY;
	irte->svt = svt;
	irte->sq = sq;
	irte->sid = sid;
}

/*
 * Set an IRTE to match only the bus number. Interrupt requests that reference
 * this IRTE must have a requester-id whose bus number is between or equal
 * to the start_bus and end_bus arguments.
 */
static void set_irte_verify_bus(struct irte *irte, unsigned int start_bus,
				unsigned int end_bus)
{
	set_irte_sid(irte, SVT_VERIFY_BUS, SQ_ALL_16,
		     (start_bus << 8) | end_bus);
}

static int set_ioapic_sid(struct irte *irte, int apic)
{
	int i;
	u16 sid = 0;

	if (!irte)
		return -1;

	down_read(&dmar_global_lock);
	for (i = 0; i < MAX_IO_APICS; i++) {
		if (ir_ioapic[i].iommu && ir_ioapic[i].id == apic) {
			sid = (ir_ioapic[i].bus << 8) | ir_ioapic[i].devfn;
			break;
		}
	}
	up_read(&dmar_global_lock);

	if (sid == 0) {
		pr_warn("Failed to set source-id of IOAPIC (%d)\n", apic);
		return -1;
	}

	set_irte_sid(irte, SVT_VERIFY_SID_SQ, SQ_ALL_16, sid);

	return 0;
}

static int set_hpet_sid(struct irte *irte, u8 id)
{
	int i;
	u16 sid = 0;

	if (!irte)
		return -1;

	down_read(&dmar_global_lock);
	for (i = 0; i < MAX_HPET_TBS; i++) {
		if (ir_hpet[i].iommu && ir_hpet[i].id == id) {
			sid = (ir_hpet[i].bus << 8) | ir_hpet[i].devfn;
			break;
		}
	}
	up_read(&dmar_global_lock);

	if (sid == 0) {
		pr_warn("Failed to set source-id of HPET block (%d)\n", id);
		return -1;
	}

	/*
	 * Should really use SQ_ALL_16. Some platforms are broken.
	 * While we figure out the right quirks for these broken platforms, use
	 * SQ_13_IGNORE_3 for now.
	 */
	set_irte_sid(irte, SVT_VERIFY_SID_SQ, SQ_13_IGNORE_3, sid);

	return 0;
}

struct set_msi_sid_data {
	struct pci_dev *pdev;
	u16 alias;
	int count;
	int busmatch_count;
};

static int set_msi_sid_cb(struct pci_dev *pdev, u16 alias, void *opaque)
{
	struct set_msi_sid_data *data = opaque;

	if (data->count == 0 || PCI_BUS_NUM(alias) == PCI_BUS_NUM(data->alias))
		data->busmatch_count++;

	data->pdev = pdev;
	data->alias = alias;
	data->count++;

	return 0;
}

static int set_msi_sid(struct irte *irte, struct pci_dev *dev)
{
	struct set_msi_sid_data data;

	if (!irte || !dev)
		return -1;

	data.count = 0;
	data.busmatch_count = 0;
	pci_for_each_dma_alias(dev, set_msi_sid_cb, &data);

	/*
	 * DMA alias provides us with a PCI device and alias.  The only case
	 * where the it will return an alias on a different bus than the
	 * device is the case of a PCIe-to-PCI bridge, where the alias is for
	 * the subordinate bus.  In this case we can only verify the bus.
	 *
	 * If there are multiple aliases, all with the same bus number,
	 * then all we can do is verify the bus. This is typical in NTB
	 * hardware which use proxy IDs where the device will generate traffic
	 * from multiple devfn numbers on the same bus.
	 *
	 * If the alias device is on a different bus than our source device
	 * then we have a topology based alias, use it.
	 *
	 * Otherwise, the alias is for a device DMA quirk and we cannot
	 * assume that MSI uses the same requester ID.  Therefore use the
	 * original device.
	 */
	if (PCI_BUS_NUM(data.alias) != data.pdev->bus->number)
		set_irte_verify_bus(irte, PCI_BUS_NUM(data.alias),
				    dev->bus->number);
	else if (data.count >= 2 && data.busmatch_count == data.count)
		set_irte_verify_bus(irte, dev->bus->number, dev->bus->number);
	else if (data.pdev->bus->number != dev->bus->number)
		set_irte_sid(irte, SVT_VERIFY_SID_SQ, SQ_ALL_16, data.alias);
	else
		set_irte_sid(irte, SVT_VERIFY_SID_SQ, SQ_ALL_16,
			     pci_dev_id(dev));

	return 0;
}

static int iommu_load_old_irte(struct intel_iommu *iommu)
{
	struct irte *old_ir_table;
	phys_addr_t irt_phys;
	unsigned int i;
	size_t size;
	u64 irta;

	/* Check whether the old ir-table has the same size as ours */
	irta = dmar_readq(iommu->reg + DMAR_IRTA_REG);
	if ((irta & INTR_REMAP_TABLE_REG_SIZE_MASK)
	     != INTR_REMAP_TABLE_REG_SIZE)
		return -EINVAL;

	irt_phys = irta & VTD_PAGE_MASK;
	size     = INTR_REMAP_TABLE_ENTRIES*sizeof(struct irte);

	/* Map the old IR table */
	old_ir_table = memremap(irt_phys, size, MEMREMAP_WB);
	if (!old_ir_table)
		return -ENOMEM;

	/* Copy data over */
	memcpy(iommu->ir_table->base, old_ir_table, size);

	__iommu_flush_cache(iommu, iommu->ir_table->base, size);

	/*
	 * Now check the table for used entries and mark those as
	 * allocated in the bitmap
	 */
	for (i = 0; i < INTR_REMAP_TABLE_ENTRIES; i++) {
		if (iommu->ir_table->base[i].present)
			bitmap_set(iommu->ir_table->bitmap, i, 1);
	}

	memunmap(old_ir_table);

	return 0;
}


static void iommu_set_irq_remapping(struct intel_iommu *iommu, int mode)
{
	unsigned long flags;
	u64 addr;
	u32 sts;

	addr = virt_to_phys((void *)iommu->ir_table->base);

	raw_spin_lock_irqsave(&iommu->register_lock, flags);

	dmar_writeq(iommu->reg + DMAR_IRTA_REG,
		    (addr) | IR_X2APIC_MODE(mode) | INTR_REMAP_TABLE_REG_SIZE);

	/* Set interrupt-remapping table pointer */
	writel(iommu->gcmd | DMA_GCMD_SIRTP, iommu->reg + DMAR_GCMD_REG);

	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
		      readl, (sts & DMA_GSTS_IRTPS), sts);
	raw_spin_unlock_irqrestore(&iommu->register_lock, flags);

	/*
	 * Global invalidation of interrupt entry cache to make sure the
	 * hardware uses the new irq remapping table.
	 */
	qi_global_iec(iommu);
}

static void iommu_enable_irq_remapping(struct intel_iommu *iommu)
{
	unsigned long flags;
	u32 sts;

	raw_spin_lock_irqsave(&iommu->register_lock, flags);

	/* Enable interrupt-remapping */
	iommu->gcmd |= DMA_GCMD_IRE;
	writel(iommu->gcmd, iommu->reg + DMAR_GCMD_REG);
	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
		      readl, (sts & DMA_GSTS_IRES), sts);

	/* Block compatibility-format MSIs */
	if (sts & DMA_GSTS_CFIS) {
		iommu->gcmd &= ~DMA_GCMD_CFI;
		writel(iommu->gcmd, iommu->reg + DMAR_GCMD_REG);
		IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
			      readl, !(sts & DMA_GSTS_CFIS), sts);
	}

	/*
	 * With CFI clear in the Global Command register, we should be
	 * protected from dangerous (i.e. compatibility) interrupts
	 * regardless of x2apic status.  Check just to be sure.
	 */
	if (sts & DMA_GSTS_CFIS)
		WARN(1, KERN_WARNING
			"Compatibility-format IRQs enabled despite intr remapping;\n"
			"you are vulnerable to IRQ injection.\n");

	raw_spin_unlock_irqrestore(&iommu->register_lock, flags);
}

static int intel_setup_irq_remapping(struct intel_iommu *iommu)
{
	struct ir_table *ir_table;
	struct fwnode_handle *fn;
	unsigned long *bitmap;
	struct page *pages;

	if (iommu->ir_table)
		return 0;

	ir_table = kzalloc(sizeof(struct ir_table), GFP_KERNEL);
	if (!ir_table)
		return -ENOMEM;

	pages = alloc_pages_node(iommu->node, GFP_KERNEL | __GFP_ZERO,
				 INTR_REMAP_PAGE_ORDER);
	if (!pages) {
		pr_err("IR%d: failed to allocate pages of order %d\n",
		       iommu->seq_id, INTR_REMAP_PAGE_ORDER);
		goto out_free_table;
	}

	bitmap = bitmap_zalloc(INTR_REMAP_TABLE_ENTRIES, GFP_ATOMIC);
	if (bitmap == NULL) {
		pr_err("IR%d: failed to allocate bitmap\n", iommu->seq_id);
		goto out_free_pages;
	}

	fn = irq_domain_alloc_named_id_fwnode("INTEL-IR", iommu->seq_id);
	if (!fn)
		goto out_free_bitmap;

	iommu->ir_domain =
		irq_domain_create_hierarchy(arch_get_ir_parent_domain(),
					    0, INTR_REMAP_TABLE_ENTRIES,
					    fn, &intel_ir_domain_ops,
					    iommu);
	if (!iommu->ir_domain) {
		irq_domain_free_fwnode(fn);
		pr_err("IR%d: failed to allocate irqdomain\n", iommu->seq_id);
		goto out_free_bitmap;
	}
	iommu->ir_msi_domain =
		arch_create_remap_msi_irq_domain(iommu->ir_domain,
						 "INTEL-IR-MSI",
						 iommu->seq_id);

	ir_table->base = page_address(pages);
	ir_table->bitmap = bitmap;
	iommu->ir_table = ir_table;

	/*
	 * If the queued invalidation is already initialized,
	 * shouldn't disable it.
	 */
	if (!iommu->qi) {
		/*
		 * Clear previous faults.
		 */
		dmar_fault(-1, iommu);
		dmar_disable_qi(iommu);

		if (dmar_enable_qi(iommu)) {
			pr_err("Failed to enable queued invalidation\n");
			goto out_free_bitmap;
		}
	}

	init_ir_status(iommu);

	if (ir_pre_enabled(iommu)) {
		if (!is_kdump_kernel()) {
			pr_warn("IRQ remapping was enabled on %s but we are not in kdump mode\n",
				iommu->name);
			clear_ir_pre_enabled(iommu);
			iommu_disable_irq_remapping(iommu);
		} else if (iommu_load_old_irte(iommu))
			pr_err("Failed to copy IR table for %s from previous kernel\n",
			       iommu->name);
		else
			pr_info("Copied IR table for %s from previous kernel\n",
				iommu->name);
	}

	iommu_set_irq_remapping(iommu, eim_mode);

	return 0;

out_free_bitmap:
	bitmap_free(bitmap);
out_free_pages:
	__free_pages(pages, INTR_REMAP_PAGE_ORDER);
out_free_table:
	kfree(ir_table);

	iommu->ir_table  = NULL;

	return -ENOMEM;
}

static void intel_teardown_irq_remapping(struct intel_iommu *iommu)
{
	struct fwnode_handle *fn;

	if (iommu && iommu->ir_table) {
		if (iommu->ir_msi_domain) {
			fn = iommu->ir_msi_domain->fwnode;

			irq_domain_remove(iommu->ir_msi_domain);
			irq_domain_free_fwnode(fn);
			iommu->ir_msi_domain = NULL;
		}
		if (iommu->ir_domain) {
			fn = iommu->ir_domain->fwnode;

			irq_domain_remove(iommu->ir_domain);
			irq_domain_free_fwnode(fn);
			iommu->ir_domain = NULL;
		}
		free_pages((unsigned long)iommu->ir_table->base,
			   INTR_REMAP_PAGE_ORDER);
		bitmap_free(iommu->ir_table->bitmap);
		kfree(iommu->ir_table);
		iommu->ir_table = NULL;
	}
}

/*
 * Disable Interrupt Remapping.
 */
static void iommu_disable_irq_remapping(struct intel_iommu *iommu)
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

	raw_spin_lock_irqsave(&iommu->register_lock, flags);

	sts = readl(iommu->reg + DMAR_GSTS_REG);
	if (!(sts & DMA_GSTS_IRES))
		goto end;

	iommu->gcmd &= ~DMA_GCMD_IRE;
	writel(iommu->gcmd, iommu->reg + DMAR_GCMD_REG);

	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
		      readl, !(sts & DMA_GSTS_IRES), sts);

end:
	raw_spin_unlock_irqrestore(&iommu->register_lock, flags);
}

static int __init dmar_x2apic_optout(void)
{
	struct acpi_table_dmar *dmar;
	dmar = (struct acpi_table_dmar *)dmar_tbl;
	if (!dmar || no_x2apic_optout)
		return 0;
	return dmar->flags & DMAR_X2APIC_OPT_OUT;
}

static void __init intel_cleanup_irq_remapping(void)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu;

	for_each_iommu(iommu, drhd) {
		if (ecap_ir_support(iommu->ecap)) {
			iommu_disable_irq_remapping(iommu);
			intel_teardown_irq_remapping(iommu);
		}
	}

	if (x2apic_supported())
		pr_warn("Failed to enable irq remapping. You are vulnerable to irq-injection attacks.\n");
}

static int __init intel_prepare_irq_remapping(void)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu;
	int eim = 0;

	if (irq_remap_broken) {
		pr_warn("This system BIOS has enabled interrupt remapping\n"
			"on a chipset that contains an erratum making that\n"
			"feature unstable.  To maintain system stability\n"
			"interrupt remapping is being disabled.  Please\n"
			"contact your BIOS vendor for an update\n");
		add_taint(TAINT_FIRMWARE_WORKAROUND, LOCKDEP_STILL_OK);
		return -ENODEV;
	}

	if (dmar_table_init() < 0)
		return -ENODEV;

	if (!dmar_ir_support())
		return -ENODEV;

	if (parse_ioapics_under_ir()) {
		pr_info("Not enabling interrupt remapping\n");
		goto error;
	}

	/* First make sure all IOMMUs support IRQ remapping */
	for_each_iommu(iommu, drhd)
		if (!ecap_ir_support(iommu->ecap))
			goto error;

	/* Detect remapping mode: lapic or x2apic */
	if (x2apic_supported()) {
		eim = !dmar_x2apic_optout();
		if (!eim) {
			pr_info("x2apic is disabled because BIOS sets x2apic opt out bit.");
			pr_info("Use 'intremap=no_x2apic_optout' to override the BIOS setting.\n");
		}
	}

	for_each_iommu(iommu, drhd) {
		if (eim && !ecap_eim_support(iommu->ecap)) {
			pr_info("%s does not support EIM\n", iommu->name);
			eim = 0;
		}
	}

	eim_mode = eim;
	if (eim)
		pr_info("Queued invalidation will be enabled to support x2apic and Intr-remapping.\n");

	/* Do the initializations early */
	for_each_iommu(iommu, drhd) {
		if (intel_setup_irq_remapping(iommu)) {
			pr_err("Failed to setup irq remapping for %s\n",
			       iommu->name);
			goto error;
		}
	}

	return 0;

error:
	intel_cleanup_irq_remapping();
	return -ENODEV;
}

/*
 * Set Posted-Interrupts capability.
 */
static inline void set_irq_posting_cap(void)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu;

	if (!disable_irq_post) {
		/*
		 * If IRTE is in posted format, the 'pda' field goes across the
		 * 64-bit boundary, we need use cmpxchg16b to atomically update
		 * it. We only expose posted-interrupt when X86_FEATURE_CX16
		 * is supported. Actually, hardware platforms supporting PI
		 * should have X86_FEATURE_CX16 support, this has been confirmed
		 * with Intel hardware guys.
		 */
		if (boot_cpu_has(X86_FEATURE_CX16))
			intel_irq_remap_ops.capability |= 1 << IRQ_POSTING_CAP;

		for_each_iommu(iommu, drhd)
			if (!cap_pi_support(iommu->cap)) {
				intel_irq_remap_ops.capability &=
						~(1 << IRQ_POSTING_CAP);
				break;
			}
	}
}

static int __init intel_enable_irq_remapping(void)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu;
	bool setup = false;

	/*
	 * Setup Interrupt-remapping for all the DRHD's now.
	 */
	for_each_iommu(iommu, drhd) {
		if (!ir_pre_enabled(iommu))
			iommu_enable_irq_remapping(iommu);
		setup = true;
	}

	if (!setup)
		goto error;

	irq_remapping_enabled = 1;

	set_irq_posting_cap();

	pr_info("Enabled IRQ remapping in %s mode\n", eim_mode ? "x2apic" : "xapic");

	return eim_mode ? IRQ_REMAP_X2APIC_MODE : IRQ_REMAP_XAPIC_MODE;

error:
	intel_cleanup_irq_remapping();
	return -1;
}

static int ir_parse_one_hpet_scope(struct acpi_dmar_device_scope *scope,
				   struct intel_iommu *iommu,
				   struct acpi_dmar_hardware_unit *drhd)
{
	struct acpi_dmar_pci_path *path;
	u8 bus;
	int count, free = -1;

	bus = scope->bus;
	path = (struct acpi_dmar_pci_path *)(scope + 1);
	count = (scope->length - sizeof(struct acpi_dmar_device_scope))
		/ sizeof(struct acpi_dmar_pci_path);

	while (--count > 0) {
		/*
		 * Access PCI directly due to the PCI
		 * subsystem isn't initialized yet.
		 */
		bus = read_pci_config_byte(bus, path->device, path->function,
					   PCI_SECONDARY_BUS);
		path++;
	}

	for (count = 0; count < MAX_HPET_TBS; count++) {
		if (ir_hpet[count].iommu == iommu &&
		    ir_hpet[count].id == scope->enumeration_id)
			return 0;
		else if (ir_hpet[count].iommu == NULL && free == -1)
			free = count;
	}
	if (free == -1) {
		pr_warn("Exceeded Max HPET blocks\n");
		return -ENOSPC;
	}

	ir_hpet[free].iommu = iommu;
	ir_hpet[free].id    = scope->enumeration_id;
	ir_hpet[free].bus   = bus;
	ir_hpet[free].devfn = PCI_DEVFN(path->device, path->function);
	pr_info("HPET id %d under DRHD base 0x%Lx\n",
		scope->enumeration_id, drhd->address);

	return 0;
}

static int ir_parse_one_ioapic_scope(struct acpi_dmar_device_scope *scope,
				     struct intel_iommu *iommu,
				     struct acpi_dmar_hardware_unit *drhd)
{
	struct acpi_dmar_pci_path *path;
	u8 bus;
	int count, free = -1;

	bus = scope->bus;
	path = (struct acpi_dmar_pci_path *)(scope + 1);
	count = (scope->length - sizeof(struct acpi_dmar_device_scope))
		/ sizeof(struct acpi_dmar_pci_path);

	while (--count > 0) {
		/*
		 * Access PCI directly due to the PCI
		 * subsystem isn't initialized yet.
		 */
		bus = read_pci_config_byte(bus, path->device, path->function,
					   PCI_SECONDARY_BUS);
		path++;
	}

	for (count = 0; count < MAX_IO_APICS; count++) {
		if (ir_ioapic[count].iommu == iommu &&
		    ir_ioapic[count].id == scope->enumeration_id)
			return 0;
		else if (ir_ioapic[count].iommu == NULL && free == -1)
			free = count;
	}
	if (free == -1) {
		pr_warn("Exceeded Max IO APICS\n");
		return -ENOSPC;
	}

	ir_ioapic[free].bus   = bus;
	ir_ioapic[free].devfn = PCI_DEVFN(path->device, path->function);
	ir_ioapic[free].iommu = iommu;
	ir_ioapic[free].id    = scope->enumeration_id;
	pr_info("IOAPIC id %d under DRHD base  0x%Lx IOMMU %d\n",
		scope->enumeration_id, drhd->address, iommu->seq_id);

	return 0;
}

static int ir_parse_ioapic_hpet_scope(struct acpi_dmar_header *header,
				      struct intel_iommu *iommu)
{
	int ret = 0;
	struct acpi_dmar_hardware_unit *drhd;
	struct acpi_dmar_device_scope *scope;
	void *start, *end;

	drhd = (struct acpi_dmar_hardware_unit *)header;
	start = (void *)(drhd + 1);
	end = ((void *)drhd) + header->length;

	while (start < end && ret == 0) {
		scope = start;
		if (scope->entry_type == ACPI_DMAR_SCOPE_TYPE_IOAPIC)
			ret = ir_parse_one_ioapic_scope(scope, iommu, drhd);
		else if (scope->entry_type == ACPI_DMAR_SCOPE_TYPE_HPET)
			ret = ir_parse_one_hpet_scope(scope, iommu, drhd);
		start += scope->length;
	}

	return ret;
}

static void ir_remove_ioapic_hpet_scope(struct intel_iommu *iommu)
{
	int i;

	for (i = 0; i < MAX_HPET_TBS; i++)
		if (ir_hpet[i].iommu == iommu)
			ir_hpet[i].iommu = NULL;

	for (i = 0; i < MAX_IO_APICS; i++)
		if (ir_ioapic[i].iommu == iommu)
			ir_ioapic[i].iommu = NULL;
}

/*
 * Finds the assocaition between IOAPIC's and its Interrupt-remapping
 * hardware unit.
 */
static int __init parse_ioapics_under_ir(void)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu;
	bool ir_supported = false;
	int ioapic_idx;

	for_each_iommu(iommu, drhd) {
		int ret;

		if (!ecap_ir_support(iommu->ecap))
			continue;

		ret = ir_parse_ioapic_hpet_scope(drhd->hdr, iommu);
		if (ret)
			return ret;

		ir_supported = true;
	}

	if (!ir_supported)
		return -ENODEV;

	for (ioapic_idx = 0; ioapic_idx < nr_ioapics; ioapic_idx++) {
		int ioapic_id = mpc_ioapic_id(ioapic_idx);
		if (!map_ioapic_to_ir(ioapic_id)) {
			pr_err(FW_BUG "ioapic %d has no mapping iommu, "
			       "interrupt remapping will be disabled\n",
			       ioapic_id);
			return -1;
		}
	}

	return 0;
}

static int __init ir_dev_scope_init(void)
{
	int ret;

	if (!irq_remapping_enabled)
		return 0;

	down_write(&dmar_global_lock);
	ret = dmar_dev_scope_init();
	up_write(&dmar_global_lock);

	return ret;
}
rootfs_initcall(ir_dev_scope_init);

static void disable_irq_remapping(void)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu = NULL;

	/*
	 * Disable Interrupt-remapping for all the DRHD's now.
	 */
	for_each_iommu(iommu, drhd) {
		if (!ecap_ir_support(iommu->ecap))
			continue;

		iommu_disable_irq_remapping(iommu);
	}

	/*
	 * Clear Posted-Interrupts capability.
	 */
	if (!disable_irq_post)
		intel_irq_remap_ops.capability &= ~(1 << IRQ_POSTING_CAP);
}

static int reenable_irq_remapping(int eim)
{
	struct dmar_drhd_unit *drhd;
	bool setup = false;
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
		iommu_set_irq_remapping(iommu, eim);
		iommu_enable_irq_remapping(iommu);
		setup = true;
	}

	if (!setup)
		goto error;

	set_irq_posting_cap();

	return 0;

error:
	/*
	 * handle error condition gracefully here!
	 */
	return -1;
}

static void prepare_irte(struct irte *irte, int vector, unsigned int dest)
{
	memset(irte, 0, sizeof(*irte));

	irte->present = 1;
	irte->dst_mode = apic->irq_dest_mode;
	/*
	 * Trigger mode in the IRTE will always be edge, and for IO-APIC, the
	 * actual level or edge trigger will be setup in the IO-APIC
	 * RTE. This will help simplify level triggered irq migration.
	 * For more details, see the comments (in io_apic.c) explainig IO-APIC
	 * irq migration in the presence of interrupt-remapping.
	*/
	irte->trigger_mode = 0;
	irte->dlvry_mode = apic->irq_delivery_mode;
	irte->vector = vector;
	irte->dest_id = IRTE_DEST(dest);
	irte->redir_hint = 1;
}

static struct irq_domain *intel_get_ir_irq_domain(struct irq_alloc_info *info)
{
	struct intel_iommu *iommu = NULL;

	if (!info)
		return NULL;

	switch (info->type) {
	case X86_IRQ_ALLOC_TYPE_IOAPIC:
		iommu = map_ioapic_to_ir(info->ioapic_id);
		break;
	case X86_IRQ_ALLOC_TYPE_HPET:
		iommu = map_hpet_to_ir(info->hpet_id);
		break;
	case X86_IRQ_ALLOC_TYPE_MSI:
	case X86_IRQ_ALLOC_TYPE_MSIX:
		iommu = map_dev_to_ir(info->msi_dev);
		break;
	default:
		BUG_ON(1);
		break;
	}

	return iommu ? iommu->ir_domain : NULL;
}

static struct irq_domain *intel_get_irq_domain(struct irq_alloc_info *info)
{
	struct intel_iommu *iommu;

	if (!info)
		return NULL;

	switch (info->type) {
	case X86_IRQ_ALLOC_TYPE_MSI:
	case X86_IRQ_ALLOC_TYPE_MSIX:
		iommu = map_dev_to_ir(info->msi_dev);
		if (iommu)
			return iommu->ir_msi_domain;
		break;
	default:
		break;
	}

	return NULL;
}

struct irq_remap_ops intel_irq_remap_ops = {
	.prepare		= intel_prepare_irq_remapping,
	.enable			= intel_enable_irq_remapping,
	.disable		= disable_irq_remapping,
	.reenable		= reenable_irq_remapping,
	.enable_faulting	= enable_drhd_fault_handling,
	.get_ir_irq_domain	= intel_get_ir_irq_domain,
	.get_irq_domain		= intel_get_irq_domain,
};

static void intel_ir_reconfigure_irte(struct irq_data *irqd, bool force)
{
	struct intel_ir_data *ir_data = irqd->chip_data;
	struct irte *irte = &ir_data->irte_entry;
	struct irq_cfg *cfg = irqd_cfg(irqd);

	/*
	 * Atomically updates the IRTE with the new destination, vector
	 * and flushes the interrupt entry cache.
	 */
	irte->vector = cfg->vector;
	irte->dest_id = IRTE_DEST(cfg->dest_apicid);

	/* Update the hardware only if the interrupt is in remapped mode. */
	if (force || ir_data->irq_2_iommu.mode == IRQ_REMAPPING)
		modify_irte(&ir_data->irq_2_iommu, irte);
}

/*
 * Migrate the IO-APIC irq in the presence of intr-remapping.
 *
 * For both level and edge triggered, irq migration is a simple atomic
 * update(of vector and cpu destination) of IRTE and flush the hardware cache.
 *
 * For level triggered, we eliminate the io-apic RTE modification (with the
 * updated vector information), by using a virtual vector (io-apic pin number).
 * Real vector that is used for interrupting cpu will be coming from
 * the interrupt-remapping table entry.
 *
 * As the migration is a simple atomic update of IRTE, the same mechanism
 * is used to migrate MSI irq's in the presence of interrupt-remapping.
 */
static int
intel_ir_set_affinity(struct irq_data *data, const struct cpumask *mask,
		      bool force)
{
	struct irq_data *parent = data->parent_data;
	struct irq_cfg *cfg = irqd_cfg(data);
	int ret;

	ret = parent->chip->irq_set_affinity(parent, mask, force);
	if (ret < 0 || ret == IRQ_SET_MASK_OK_DONE)
		return ret;

	intel_ir_reconfigure_irte(data, false);
	/*
	 * After this point, all the interrupts will start arriving
	 * at the new destination. So, time to cleanup the previous
	 * vector allocation.
	 */
	send_cleanup_vector(cfg);

	return IRQ_SET_MASK_OK_DONE;
}

static void intel_ir_compose_msi_msg(struct irq_data *irq_data,
				     struct msi_msg *msg)
{
	struct intel_ir_data *ir_data = irq_data->chip_data;

	*msg = ir_data->msi_entry;
}

static int intel_ir_set_vcpu_affinity(struct irq_data *data, void *info)
{
	struct intel_ir_data *ir_data = data->chip_data;
	struct vcpu_data *vcpu_pi_info = info;

	/* stop posting interrupts, back to remapping mode */
	if (!vcpu_pi_info) {
		modify_irte(&ir_data->irq_2_iommu, &ir_data->irte_entry);
	} else {
		struct irte irte_pi;

		/*
		 * We are not caching the posted interrupt entry. We
		 * copy the data from the remapped entry and modify
		 * the fields which are relevant for posted mode. The
		 * cached remapped entry is used for switching back to
		 * remapped mode.
		 */
		memset(&irte_pi, 0, sizeof(irte_pi));
		dmar_copy_shared_irte(&irte_pi, &ir_data->irte_entry);

		/* Update the posted mode fields */
		irte_pi.p_pst = 1;
		irte_pi.p_urgent = 0;
		irte_pi.p_vector = vcpu_pi_info->vector;
		irte_pi.pda_l = (vcpu_pi_info->pi_desc_addr >>
				(32 - PDA_LOW_BIT)) & ~(-1UL << PDA_LOW_BIT);
		irte_pi.pda_h = (vcpu_pi_info->pi_desc_addr >> 32) &
				~(-1UL << PDA_HIGH_BIT);

		modify_irte(&ir_data->irq_2_iommu, &irte_pi);
	}

	return 0;
}

static struct irq_chip intel_ir_chip = {
	.name			= "INTEL-IR",
	.irq_ack		= apic_ack_irq,
	.irq_set_affinity	= intel_ir_set_affinity,
	.irq_compose_msi_msg	= intel_ir_compose_msi_msg,
	.irq_set_vcpu_affinity	= intel_ir_set_vcpu_affinity,
};

static void intel_irq_remapping_prepare_irte(struct intel_ir_data *data,
					     struct irq_cfg *irq_cfg,
					     struct irq_alloc_info *info,
					     int index, int sub_handle)
{
	struct IR_IO_APIC_route_entry *entry;
	struct irte *irte = &data->irte_entry;
	struct msi_msg *msg = &data->msi_entry;

	prepare_irte(irte, irq_cfg->vector, irq_cfg->dest_apicid);
	switch (info->type) {
	case X86_IRQ_ALLOC_TYPE_IOAPIC:
		/* Set source-id of interrupt request */
		set_ioapic_sid(irte, info->ioapic_id);
		apic_printk(APIC_VERBOSE, KERN_DEBUG "IOAPIC[%d]: Set IRTE entry (P:%d FPD:%d Dst_Mode:%d Redir_hint:%d Trig_Mode:%d Dlvry_Mode:%X Avail:%X Vector:%02X Dest:%08X SID:%04X SQ:%X SVT:%X)\n",
			info->ioapic_id, irte->present, irte->fpd,
			irte->dst_mode, irte->redir_hint,
			irte->trigger_mode, irte->dlvry_mode,
			irte->avail, irte->vector, irte->dest_id,
			irte->sid, irte->sq, irte->svt);

		entry = (struct IR_IO_APIC_route_entry *)info->ioapic_entry;
		info->ioapic_entry = NULL;
		memset(entry, 0, sizeof(*entry));
		entry->index2	= (index >> 15) & 0x1;
		entry->zero	= 0;
		entry->format	= 1;
		entry->index	= (index & 0x7fff);
		/*
		 * IO-APIC RTE will be configured with virtual vector.
		 * irq handler will do the explicit EOI to the io-apic.
		 */
		entry->vector	= info->ioapic_pin;
		entry->mask	= 0;			/* enable IRQ */
		entry->trigger	= info->ioapic_trigger;
		entry->polarity	= info->ioapic_polarity;
		if (info->ioapic_trigger)
			entry->mask = 1; /* Mask level triggered irqs. */
		break;

	case X86_IRQ_ALLOC_TYPE_HPET:
	case X86_IRQ_ALLOC_TYPE_MSI:
	case X86_IRQ_ALLOC_TYPE_MSIX:
		if (info->type == X86_IRQ_ALLOC_TYPE_HPET)
			set_hpet_sid(irte, info->hpet_id);
		else
			set_msi_sid(irte, info->msi_dev);

		msg->address_hi = MSI_ADDR_BASE_HI;
		msg->data = sub_handle;
		msg->address_lo = MSI_ADDR_BASE_LO | MSI_ADDR_IR_EXT_INT |
				  MSI_ADDR_IR_SHV |
				  MSI_ADDR_IR_INDEX1(index) |
				  MSI_ADDR_IR_INDEX2(index);
		break;

	default:
		BUG_ON(1);
		break;
	}
}

static void intel_free_irq_resources(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *irq_data;
	struct intel_ir_data *data;
	struct irq_2_iommu *irq_iommu;
	unsigned long flags;
	int i;
	for (i = 0; i < nr_irqs; i++) {
		irq_data = irq_domain_get_irq_data(domain, virq  + i);
		if (irq_data && irq_data->chip_data) {
			data = irq_data->chip_data;
			irq_iommu = &data->irq_2_iommu;
			raw_spin_lock_irqsave(&irq_2_ir_lock, flags);
			clear_entries(irq_iommu);
			raw_spin_unlock_irqrestore(&irq_2_ir_lock, flags);
			irq_domain_reset_irq_data(irq_data);
			kfree(data);
		}
	}
}

static int intel_irq_remapping_alloc(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs,
				     void *arg)
{
	struct intel_iommu *iommu = domain->host_data;
	struct irq_alloc_info *info = arg;
	struct intel_ir_data *data, *ird;
	struct irq_data *irq_data;
	struct irq_cfg *irq_cfg;
	int i, ret, index;

	if (!info || !iommu)
		return -EINVAL;
	if (nr_irqs > 1 && info->type != X86_IRQ_ALLOC_TYPE_MSI &&
	    info->type != X86_IRQ_ALLOC_TYPE_MSIX)
		return -EINVAL;

	/*
	 * With IRQ remapping enabled, don't need contiguous CPU vectors
	 * to support multiple MSI interrupts.
	 */
	if (info->type == X86_IRQ_ALLOC_TYPE_MSI)
		info->flags &= ~X86_IRQ_ALLOC_CONTIGUOUS_VECTORS;

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, arg);
	if (ret < 0)
		return ret;

	ret = -ENOMEM;
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		goto out_free_parent;

	down_read(&dmar_global_lock);
	index = alloc_irte(iommu, &data->irq_2_iommu, nr_irqs);
	up_read(&dmar_global_lock);
	if (index < 0) {
		pr_warn("Failed to allocate IRTE\n");
		kfree(data);
		goto out_free_parent;
	}

	for (i = 0; i < nr_irqs; i++) {
		irq_data = irq_domain_get_irq_data(domain, virq + i);
		irq_cfg = irqd_cfg(irq_data);
		if (!irq_data || !irq_cfg) {
			ret = -EINVAL;
			goto out_free_data;
		}

		if (i > 0) {
			ird = kzalloc(sizeof(*ird), GFP_KERNEL);
			if (!ird)
				goto out_free_data;
			/* Initialize the common data */
			ird->irq_2_iommu = data->irq_2_iommu;
			ird->irq_2_iommu.sub_handle = i;
		} else {
			ird = data;
		}

		irq_data->hwirq = (index << 16) + i;
		irq_data->chip_data = ird;
		irq_data->chip = &intel_ir_chip;
		intel_irq_remapping_prepare_irte(ird, irq_cfg, info, index, i);
		irq_set_status_flags(virq + i, IRQ_MOVE_PCNTXT);
	}
	return 0;

out_free_data:
	intel_free_irq_resources(domain, virq, i);
out_free_parent:
	irq_domain_free_irqs_common(domain, virq, nr_irqs);
	return ret;
}

static void intel_irq_remapping_free(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs)
{
	intel_free_irq_resources(domain, virq, nr_irqs);
	irq_domain_free_irqs_common(domain, virq, nr_irqs);
}

static int intel_irq_remapping_activate(struct irq_domain *domain,
					struct irq_data *irq_data, bool reserve)
{
	intel_ir_reconfigure_irte(irq_data, true);
	return 0;
}

static void intel_irq_remapping_deactivate(struct irq_domain *domain,
					   struct irq_data *irq_data)
{
	struct intel_ir_data *data = irq_data->chip_data;
	struct irte entry;

	memset(&entry, 0, sizeof(entry));
	modify_irte(&data->irq_2_iommu, &entry);
}

static const struct irq_domain_ops intel_ir_domain_ops = {
	.alloc = intel_irq_remapping_alloc,
	.free = intel_irq_remapping_free,
	.activate = intel_irq_remapping_activate,
	.deactivate = intel_irq_remapping_deactivate,
};

/*
 * Support of Interrupt Remapping Unit Hotplug
 */
static int dmar_ir_add(struct dmar_drhd_unit *dmaru, struct intel_iommu *iommu)
{
	int ret;
	int eim = x2apic_enabled();

	if (eim && !ecap_eim_support(iommu->ecap)) {
		pr_info("DRHD %Lx: EIM not supported by DRHD, ecap %Lx\n",
			iommu->reg_phys, iommu->ecap);
		return -ENODEV;
	}

	if (ir_parse_ioapic_hpet_scope(dmaru->hdr, iommu)) {
		pr_warn("DRHD %Lx: failed to parse managed IOAPIC/HPET\n",
			iommu->reg_phys);
		return -ENODEV;
	}

	/* TODO: check all IOAPICs are covered by IOMMU */

	/* Setup Interrupt-remapping now. */
	ret = intel_setup_irq_remapping(iommu);
	if (ret) {
		pr_err("Failed to setup irq remapping for %s\n",
		       iommu->name);
		intel_teardown_irq_remapping(iommu);
		ir_remove_ioapic_hpet_scope(iommu);
	} else {
		iommu_enable_irq_remapping(iommu);
	}

	return ret;
}

int dmar_ir_hotplug(struct dmar_drhd_unit *dmaru, bool insert)
{
	int ret = 0;
	struct intel_iommu *iommu = dmaru->iommu;

	if (!irq_remapping_enabled)
		return 0;
	if (iommu == NULL)
		return -EINVAL;
	if (!ecap_ir_support(iommu->ecap))
		return 0;
	if (irq_remapping_cap(IRQ_POSTING_CAP) &&
	    !cap_pi_support(iommu->cap))
		return -EBUSY;

	if (insert) {
		if (!iommu->ir_table)
			ret = dmar_ir_add(dmaru, iommu);
	} else {
		if (iommu->ir_table) {
			if (!bitmap_empty(iommu->ir_table->bitmap,
					  INTR_REMAP_TABLE_ENTRIES)) {
				ret = -EBUSY;
			} else {
				iommu_disable_irq_remapping(iommu);
				intel_teardown_irq_remapping(iommu);
				ir_remove_ioapic_hpet_scope(iommu);
			}
		}
	}

	return ret;
}
