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
#include <asm/io_apic.h>
#include <asm/smp.h>
#include <asm/cpu.h>
#include <asm/irq_remapping.h>
#include <asm/pci-direct.h>
#include <asm/msidef.h>

#include "irq_remapping.h"

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

#define IR_X2APIC_MODE(mode) (mode ? (1 << 11) : 0)
#define IRTE_DEST(dest) ((x2apic_mode) ? dest : dest << 8)

static struct ioapic_scope ir_ioapic[MAX_IO_APICS];
static struct hpet_scope ir_hpet[MAX_HPET_TBS];
static int ir_ioapic_num, ir_hpet_num;

static DEFINE_RAW_SPINLOCK(irq_2_ir_lock);

static struct irq_2_iommu *irq_2_iommu(unsigned int irq)
{
	struct irq_cfg *cfg = irq_get_chip_data(irq);
	return cfg ? &cfg->irq_2_iommu : NULL;
}

int get_irte(int irq, struct irte *entry)
{
	struct irq_2_iommu *irq_iommu = irq_2_iommu(irq);
	unsigned long flags;
	int index;

	if (!entry || !irq_iommu)
		return -1;

	raw_spin_lock_irqsave(&irq_2_ir_lock, flags);

	index = irq_iommu->irte_index + irq_iommu->sub_handle;
	*entry = *(irq_iommu->iommu->ir_table->base + index);

	raw_spin_unlock_irqrestore(&irq_2_ir_lock, flags);
	return 0;
}

static int alloc_irte(struct intel_iommu *iommu, int irq, u16 count)
{
	struct ir_table *table = iommu->ir_table;
	struct irq_2_iommu *irq_iommu = irq_2_iommu(irq);
	struct irq_cfg *cfg = irq_get_chip_data(irq);
	u16 index, start_index;
	unsigned int mask = 0;
	unsigned long flags;
	int i;

	if (!count || !irq_iommu)
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

	raw_spin_lock_irqsave(&irq_2_ir_lock, flags);
	do {
		for (i = index; i < index + count; i++)
			if  (table->base[i].present)
				break;
		/* empty index found */
		if (i == index + count)
			break;

		index = (index + count) % INTR_REMAP_TABLE_ENTRIES;

		if (index == start_index) {
			raw_spin_unlock_irqrestore(&irq_2_ir_lock, flags);
			printk(KERN_ERR "can't allocate an IRTE\n");
			return -1;
		}
	} while (1);

	for (i = index; i < index + count; i++)
		table->base[i].present = 1;

	cfg->remapped = 1;
	irq_iommu->iommu = iommu;
	irq_iommu->irte_index =  index;
	irq_iommu->sub_handle = 0;
	irq_iommu->irte_mask = mask;

	raw_spin_unlock_irqrestore(&irq_2_ir_lock, flags);

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

static int map_irq_to_irte_handle(int irq, u16 *sub_handle)
{
	struct irq_2_iommu *irq_iommu = irq_2_iommu(irq);
	unsigned long flags;
	int index;

	if (!irq_iommu)
		return -1;

	raw_spin_lock_irqsave(&irq_2_ir_lock, flags);
	*sub_handle = irq_iommu->sub_handle;
	index = irq_iommu->irte_index;
	raw_spin_unlock_irqrestore(&irq_2_ir_lock, flags);
	return index;
}

static int set_irte_irq(int irq, struct intel_iommu *iommu, u16 index, u16 subhandle)
{
	struct irq_2_iommu *irq_iommu = irq_2_iommu(irq);
	struct irq_cfg *cfg = irq_get_chip_data(irq);
	unsigned long flags;

	if (!irq_iommu)
		return -1;

	raw_spin_lock_irqsave(&irq_2_ir_lock, flags);

	cfg->remapped = 1;
	irq_iommu->iommu = iommu;
	irq_iommu->irte_index = index;
	irq_iommu->sub_handle = subhandle;
	irq_iommu->irte_mask = 0;

	raw_spin_unlock_irqrestore(&irq_2_ir_lock, flags);

	return 0;
}

static int modify_irte(int irq, struct irte *irte_modified)
{
	struct irq_2_iommu *irq_iommu = irq_2_iommu(irq);
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

	set_64bit(&irte->low, irte_modified->low);
	set_64bit(&irte->high, irte_modified->high);
	__iommu_flush_cache(iommu, irte, sizeof(*irte));

	rc = qi_flush_iec(iommu, index, 0);
	raw_spin_unlock_irqrestore(&irq_2_ir_lock, flags);

	return rc;
}

static struct intel_iommu *map_hpet_to_ir(u8 hpet_id)
{
	int i;

	for (i = 0; i < MAX_HPET_TBS; i++)
		if (ir_hpet[i].id == hpet_id)
			return ir_hpet[i].iommu;
	return NULL;
}

static struct intel_iommu *map_ioapic_to_ir(int apic)
{
	int i;

	for (i = 0; i < MAX_IO_APICS; i++)
		if (ir_ioapic[i].id == apic)
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
	index = irq_iommu->irte_index + irq_iommu->sub_handle;

	start = iommu->ir_table->base + index;
	end = start + (1 << irq_iommu->irte_mask);

	for (entry = start; entry < end; entry++) {
		set_64bit(&entry->low, 0);
		set_64bit(&entry->high, 0);
	}

	return qi_flush_iec(iommu, index, irq_iommu->irte_mask);
}

static int free_irte(int irq)
{
	struct irq_2_iommu *irq_iommu = irq_2_iommu(irq);
	unsigned long flags;
	int rc;

	if (!irq_iommu)
		return -1;

	raw_spin_lock_irqsave(&irq_2_ir_lock, flags);

	rc = clear_entries(irq_iommu);

	irq_iommu->iommu = NULL;
	irq_iommu->irte_index = 0;
	irq_iommu->sub_handle = 0;
	irq_iommu->irte_mask = 0;

	raw_spin_unlock_irqrestore(&irq_2_ir_lock, flags);

	return rc;
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

static int set_ioapic_sid(struct irte *irte, int apic)
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

static int set_hpet_sid(struct irte *irte, u8 id)
{
	int i;
	u16 sid = 0;

	if (!irte)
		return -1;

	for (i = 0; i < MAX_HPET_TBS; i++) {
		if (ir_hpet[i].id == id) {
			sid = (ir_hpet[i].bus << 8) | ir_hpet[i].devfn;
			break;
		}
	}

	if (sid == 0) {
		pr_warning("Failed to set source-id of HPET block (%d)\n", id);
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

static int set_msi_sid(struct irte *irte, struct pci_dev *dev)
{
	struct pci_dev *bridge;

	if (!irte || !dev)
		return -1;

	/* PCIe device or Root Complex integrated PCI device */
	if (pci_is_pcie(dev) || !dev->bus->parent) {
		set_irte_sid(irte, SVT_VERIFY_SID_SQ, SQ_ALL_16,
			     (dev->bus->number << 8) | dev->devfn);
		return 0;
	}

	bridge = pci_find_upstream_pcie_bridge(dev);
	if (bridge) {
		if (pci_is_pcie(bridge))/* this is a PCIe-to-PCI/PCIX bridge */
			set_irte_sid(irte, SVT_VERIFY_BUS, SQ_ALL_16,
				(bridge->bus->number << 8) | dev->bus->number);
		else /* this is a legacy PCI bridge */
			set_irte_sid(irte, SVT_VERIFY_SID_SQ, SQ_ALL_16,
				(bridge->bus->number << 8) | bridge->devfn);
	}

	return 0;
}

static void iommu_set_irq_remapping(struct intel_iommu *iommu, int mode)
{
	u64 addr;
	u32 sts;
	unsigned long flags;

	addr = virt_to_phys((void *)iommu->ir_table->base);

	raw_spin_lock_irqsave(&iommu->register_lock, flags);

	dmar_writeq(iommu->reg + DMAR_IRTA_REG,
		    (addr) | IR_X2APIC_MODE(mode) | INTR_REMAP_TABLE_REG_SIZE);

	/* Set interrupt-remapping table pointer */
	iommu->gcmd |= DMA_GCMD_SIRTP;
	writel(iommu->gcmd, iommu->reg + DMAR_GCMD_REG);

	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
		      readl, (sts & DMA_GSTS_IRTPS), sts);
	raw_spin_unlock_irqrestore(&iommu->register_lock, flags);

	/*
	 * global invalidation of interrupt entry cache before enabling
	 * interrupt-remapping.
	 */
	qi_global_iec(iommu);

	raw_spin_lock_irqsave(&iommu->register_lock, flags);

	/* Enable interrupt-remapping */
	iommu->gcmd |= DMA_GCMD_IRE;
	iommu->gcmd &= ~DMA_GCMD_CFI;  /* Block compatibility-format MSIs */
	writel(iommu->gcmd, iommu->reg + DMAR_GCMD_REG);

	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG,
		      readl, (sts & DMA_GSTS_IRES), sts);

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


static int intel_setup_irq_remapping(struct intel_iommu *iommu, int mode)
{
	struct ir_table *ir_table;
	struct page *pages;

	ir_table = iommu->ir_table = kzalloc(sizeof(struct ir_table),
					     GFP_ATOMIC);

	if (!iommu->ir_table)
		return -ENOMEM;

	pages = alloc_pages_node(iommu->node, GFP_ATOMIC | __GFP_ZERO,
				 INTR_REMAP_PAGE_ORDER);

	if (!pages) {
		printk(KERN_ERR "failed to allocate pages of order %d\n",
		       INTR_REMAP_PAGE_ORDER);
		kfree(iommu->ir_table);
		return -ENOMEM;
	}

	ir_table->base = page_address(pages);

	iommu_set_irq_remapping(iommu, mode);
	return 0;
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

	sts = dmar_readq(iommu->reg + DMAR_GSTS_REG);
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

static int __init intel_irq_remapping_supported(void)
{
	struct dmar_drhd_unit *drhd;

	if (disable_irq_remap)
		return 0;
	if (irq_remap_broken) {
		printk(KERN_WARNING
			"This system BIOS has enabled interrupt remapping\n"
			"on a chipset that contains an erratum making that\n"
			"feature unstable.  To maintain system stability\n"
			"interrupt remapping is being disabled.  Please\n"
			"contact your BIOS vendor for an update\n");
		add_taint(TAINT_FIRMWARE_WORKAROUND, LOCKDEP_STILL_OK);
		disable_irq_remap = 1;
		return 0;
	}

	if (!dmar_ir_support())
		return 0;

	for_each_drhd_unit(drhd) {
		struct intel_iommu *iommu = drhd->iommu;

		if (!ecap_ir_support(iommu->ecap))
			return 0;
	}

	return 1;
}

static int __init intel_enable_irq_remapping(void)
{
	struct dmar_drhd_unit *drhd;
	bool x2apic_present;
	int setup = 0;
	int eim = 0;

	x2apic_present = x2apic_supported();

	if (parse_ioapics_under_ir() != 1) {
		printk(KERN_INFO "Not enable interrupt remapping\n");
		goto error;
	}

	if (x2apic_present) {
		eim = !dmar_x2apic_optout();
		if (!eim)
			printk(KERN_WARNING
				"Your BIOS is broken and requested that x2apic be disabled.\n"
				"This will slightly decrease performance.\n"
				"Use 'intremap=no_x2apic_optout' to override BIOS request.\n");
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
		iommu_disable_irq_remapping(iommu);

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
			goto error;
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
			goto error;
		}
	}

	/*
	 * Setup Interrupt-remapping for all the DRHD's now.
	 */
	for_each_drhd_unit(drhd) {
		struct intel_iommu *iommu = drhd->iommu;

		if (!ecap_ir_support(iommu->ecap))
			continue;

		if (intel_setup_irq_remapping(iommu, eim))
			goto error;

		setup = 1;
	}

	if (!setup)
		goto error;

	irq_remapping_enabled = 1;

	/*
	 * VT-d has a different layout for IO-APIC entries when
	 * interrupt remapping is enabled. So it needs a special routine
	 * to print IO-APIC entries for debugging purposes too.
	 */
	x86_io_apic_ops.print_entries = intel_ir_io_apic_print_entries;

	pr_info("Enabled IRQ remapping in %s mode\n", eim ? "x2apic" : "xapic");

	return eim ? IRQ_REMAP_X2APIC_MODE : IRQ_REMAP_XAPIC_MODE;

error:
	/*
	 * handle error condition gracefully here!
	 */

	if (x2apic_present)
		pr_warn("Failed to enable irq remapping.  You are vulnerable to irq-injection attacks.\n");

	return -1;
}

static void ir_parse_one_hpet_scope(struct acpi_dmar_device_scope *scope,
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
		bus = read_pci_config_byte(bus, path->device, path->function,
					   PCI_SECONDARY_BUS);
		path++;
	}
	ir_hpet[ir_hpet_num].bus   = bus;
	ir_hpet[ir_hpet_num].devfn = PCI_DEVFN(path->device, path->function);
	ir_hpet[ir_hpet_num].iommu = iommu;
	ir_hpet[ir_hpet_num].id    = scope->enumeration_id;
	ir_hpet_num++;
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
		bus = read_pci_config_byte(bus, path->device, path->function,
					   PCI_SECONDARY_BUS);
		path++;
	}

	ir_ioapic[ir_ioapic_num].bus   = bus;
	ir_ioapic[ir_ioapic_num].devfn = PCI_DEVFN(path->device, path->function);
	ir_ioapic[ir_ioapic_num].iommu = iommu;
	ir_ioapic[ir_ioapic_num].id    = scope->enumeration_id;
	ir_ioapic_num++;
}

static int ir_parse_ioapic_hpet_scope(struct acpi_dmar_header *header,
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

			printk(KERN_INFO "IOAPIC id %d under DRHD base "
			       " 0x%Lx IOMMU %d\n", scope->enumeration_id,
			       drhd->address, iommu->seq_id);

			ir_parse_one_ioapic_scope(scope, iommu);
		} else if (scope->entry_type == ACPI_DMAR_SCOPE_TYPE_HPET) {
			if (ir_hpet_num == MAX_HPET_TBS) {
				printk(KERN_WARNING "Exceeded Max HPET blocks\n");
				return -1;
			}

			printk(KERN_INFO "HPET id %d under DRHD base"
			       " 0x%Lx\n", scope->enumeration_id,
			       drhd->address);

			ir_parse_one_hpet_scope(scope, iommu);
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
	int ioapic_idx;

	for_each_drhd_unit(drhd) {
		struct intel_iommu *iommu = drhd->iommu;

		if (ecap_ir_support(iommu->ecap)) {
			if (ir_parse_ioapic_hpet_scope(drhd->hdr, iommu))
				return -1;

			ir_supported = 1;
		}
	}

	if (!ir_supported)
		return 0;

	for (ioapic_idx = 0; ioapic_idx < nr_ioapics; ioapic_idx++) {
		int ioapic_id = mpc_ioapic_id(ioapic_idx);
		if (!map_ioapic_to_ir(ioapic_id)) {
			pr_err(FW_BUG "ioapic %d has no mapping iommu, "
			       "interrupt remapping will be disabled\n",
			       ioapic_id);
			return -1;
		}
	}

	return 1;
}

int __init ir_dev_scope_init(void)
{
	if (!irq_remapping_enabled)
		return 0;

	return dmar_dev_scope_init();
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
}

static int reenable_irq_remapping(int eim)
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
		iommu_set_irq_remapping(iommu, eim);
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

static void prepare_irte(struct irte *irte, int vector,
			 unsigned int dest)
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

static int intel_setup_ioapic_entry(int irq,
				    struct IO_APIC_route_entry *route_entry,
				    unsigned int destination, int vector,
				    struct io_apic_irq_attr *attr)
{
	int ioapic_id = mpc_ioapic_id(attr->ioapic);
	struct intel_iommu *iommu = map_ioapic_to_ir(ioapic_id);
	struct IR_IO_APIC_route_entry *entry;
	struct irte irte;
	int index;

	if (!iommu) {
		pr_warn("No mapping iommu for ioapic %d\n", ioapic_id);
		return -ENODEV;
	}

	entry = (struct IR_IO_APIC_route_entry *)route_entry;

	index = alloc_irte(iommu, irq, 1);
	if (index < 0) {
		pr_warn("Failed to allocate IRTE for ioapic %d\n", ioapic_id);
		return -ENOMEM;
	}

	prepare_irte(&irte, vector, destination);

	/* Set source-id of interrupt request */
	set_ioapic_sid(&irte, ioapic_id);

	modify_irte(irq, &irte);

	apic_printk(APIC_VERBOSE, KERN_DEBUG "IOAPIC[%d]: "
		"Set IRTE entry (P:%d FPD:%d Dst_Mode:%d "
		"Redir_hint:%d Trig_Mode:%d Dlvry_Mode:%X "
		"Avail:%X Vector:%02X Dest:%08X "
		"SID:%04X SQ:%X SVT:%X)\n",
		attr->ioapic, irte.present, irte.fpd, irte.dst_mode,
		irte.redir_hint, irte.trigger_mode, irte.dlvry_mode,
		irte.avail, irte.vector, irte.dest_id,
		irte.sid, irte.sq, irte.svt);

	memset(entry, 0, sizeof(*entry));

	entry->index2	= (index >> 15) & 0x1;
	entry->zero	= 0;
	entry->format	= 1;
	entry->index	= (index & 0x7fff);
	/*
	 * IO-APIC RTE will be configured with virtual vector.
	 * irq handler will do the explicit EOI to the io-apic.
	 */
	entry->vector	= attr->ioapic_pin;
	entry->mask	= 0;			/* enable IRQ */
	entry->trigger	= attr->trigger;
	entry->polarity	= attr->polarity;

	/* Mask level triggered irqs.
	 * Use IRQ_DELAYED_DISABLE for edge triggered irqs.
	 */
	if (attr->trigger)
		entry->mask = 1;

	return 0;
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
intel_ioapic_set_affinity(struct irq_data *data, const struct cpumask *mask,
			  bool force)
{
	struct irq_cfg *cfg = data->chip_data;
	unsigned int dest, irq = data->irq;
	struct irte irte;
	int err;

	if (!config_enabled(CONFIG_SMP))
		return -EINVAL;

	if (!cpumask_intersects(mask, cpu_online_mask))
		return -EINVAL;

	if (get_irte(irq, &irte))
		return -EBUSY;

	err = assign_irq_vector(irq, cfg, mask);
	if (err)
		return err;

	err = apic->cpu_mask_to_apicid_and(cfg->domain, mask, &dest);
	if (err) {
		if (assign_irq_vector(irq, cfg, data->affinity))
			pr_err("Failed to recover vector for irq %d\n", irq);
		return err;
	}

	irte.vector = cfg->vector;
	irte.dest_id = IRTE_DEST(dest);

	/*
	 * Atomically updates the IRTE with the new destination, vector
	 * and flushes the interrupt entry cache.
	 */
	modify_irte(irq, &irte);

	/*
	 * After this point, all the interrupts will start arriving
	 * at the new destination. So, time to cleanup the previous
	 * vector allocation.
	 */
	if (cfg->move_in_progress)
		send_cleanup_vector(cfg);

	cpumask_copy(data->affinity, mask);
	return 0;
}

static void intel_compose_msi_msg(struct pci_dev *pdev,
				  unsigned int irq, unsigned int dest,
				  struct msi_msg *msg, u8 hpet_id)
{
	struct irq_cfg *cfg;
	struct irte irte;
	u16 sub_handle = 0;
	int ir_index;

	cfg = irq_get_chip_data(irq);

	ir_index = map_irq_to_irte_handle(irq, &sub_handle);
	BUG_ON(ir_index == -1);

	prepare_irte(&irte, cfg->vector, dest);

	/* Set source-id of interrupt request */
	if (pdev)
		set_msi_sid(&irte, pdev);
	else
		set_hpet_sid(&irte, hpet_id);

	modify_irte(irq, &irte);

	msg->address_hi = MSI_ADDR_BASE_HI;
	msg->data = sub_handle;
	msg->address_lo = MSI_ADDR_BASE_LO | MSI_ADDR_IR_EXT_INT |
			  MSI_ADDR_IR_SHV |
			  MSI_ADDR_IR_INDEX1(ir_index) |
			  MSI_ADDR_IR_INDEX2(ir_index);
}

/*
 * Map the PCI dev to the corresponding remapping hardware unit
 * and allocate 'nvec' consecutive interrupt-remapping table entries
 * in it.
 */
static int intel_msi_alloc_irq(struct pci_dev *dev, int irq, int nvec)
{
	struct intel_iommu *iommu;
	int index;

	iommu = map_dev_to_ir(dev);
	if (!iommu) {
		printk(KERN_ERR
		       "Unable to map PCI %s to iommu\n", pci_name(dev));
		return -ENOENT;
	}

	index = alloc_irte(iommu, irq, nvec);
	if (index < 0) {
		printk(KERN_ERR
		       "Unable to allocate %d IRTE for PCI %s\n", nvec,
		       pci_name(dev));
		return -ENOSPC;
	}
	return index;
}

static int intel_msi_setup_irq(struct pci_dev *pdev, unsigned int irq,
			       int index, int sub_handle)
{
	struct intel_iommu *iommu;

	iommu = map_dev_to_ir(pdev);
	if (!iommu)
		return -ENOENT;
	/*
	 * setup the mapping between the irq and the IRTE
	 * base index, the sub_handle pointing to the
	 * appropriate interrupt remap table entry.
	 */
	set_irte_irq(irq, iommu, index, sub_handle);

	return 0;
}

static int intel_setup_hpet_msi(unsigned int irq, unsigned int id)
{
	struct intel_iommu *iommu = map_hpet_to_ir(id);
	int index;

	if (!iommu)
		return -1;

	index = alloc_irte(iommu, irq, 1);
	if (index < 0)
		return -1;

	return 0;
}

struct irq_remap_ops intel_irq_remap_ops = {
	.supported		= intel_irq_remapping_supported,
	.prepare		= dmar_table_init,
	.enable			= intel_enable_irq_remapping,
	.disable		= disable_irq_remapping,
	.reenable		= reenable_irq_remapping,
	.enable_faulting	= enable_drhd_fault_handling,
	.setup_ioapic_entry	= intel_setup_ioapic_entry,
	.set_affinity		= intel_ioapic_set_affinity,
	.free_irq		= free_irte,
	.compose_msi_msg	= intel_compose_msi_msg,
	.msi_alloc_irq		= intel_msi_alloc_irq,
	.msi_setup_irq		= intel_msi_setup_irq,
	.setup_hpet_msi		= intel_setup_hpet_msi,
};
