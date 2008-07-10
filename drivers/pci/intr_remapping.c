#include <linux/dmar.h>
#include <asm/io_apic.h>
#include "intel-iommu.h"
#include "intr_remapping.h"

static struct ioapic_scope ir_ioapic[MAX_IO_APICS];
static int ir_ioapic_num;

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
