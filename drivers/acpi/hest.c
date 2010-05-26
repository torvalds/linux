#include <linux/acpi.h>
#include <linux/pci.h>

#define PREFIX "ACPI: "

static inline unsigned long parse_acpi_hest_ia_machine_check(struct acpi_hest_ia_machine_check *p)
{
	return sizeof(*p) +
		(sizeof(struct acpi_hest_ia_error_bank) * p->num_hardware_banks);
}

static inline unsigned long parse_acpi_hest_ia_corrected(struct acpi_hest_ia_corrected *p)
{
	return sizeof(*p) +
		(sizeof(struct acpi_hest_ia_error_bank) * p->num_hardware_banks);
}

static inline unsigned long parse_acpi_hest_ia_nmi(struct acpi_hest_ia_nmi *p)
{
	return sizeof(*p);
}

static inline unsigned long parse_acpi_hest_generic(struct acpi_hest_generic *p)
{
	return sizeof(*p);
}

static inline unsigned int hest_match_pci(struct acpi_hest_aer_common *p, struct pci_dev *pci)
{
	return	(0           == pci_domain_nr(pci->bus) &&
		 p->bus      == pci->bus->number &&
		 p->device   == PCI_SLOT(pci->devfn) &&
		 p->function == PCI_FUNC(pci->devfn));
}

static unsigned long parse_acpi_hest_aer(void *hdr, int type, struct pci_dev *pci, int *firmware_first)
{
	struct acpi_hest_aer_common *p = hdr + sizeof(struct acpi_hest_header);
	unsigned long rc=0;
	u8 pcie_type = 0;
	u8 bridge = 0;
	switch (type) {
	case ACPI_HEST_TYPE_AER_ROOT_PORT:
		rc = sizeof(struct acpi_hest_aer_root);
		pcie_type = PCI_EXP_TYPE_ROOT_PORT;
		break;
	case ACPI_HEST_TYPE_AER_ENDPOINT:
		rc = sizeof(struct acpi_hest_aer);
		pcie_type = PCI_EXP_TYPE_ENDPOINT;
		break;
	case ACPI_HEST_TYPE_AER_BRIDGE:
		rc = sizeof(struct acpi_hest_aer_bridge);
		if ((pci->class >> 16) == PCI_BASE_CLASS_BRIDGE)
			bridge = 1;
		break;
	}

	if (p->flags & ACPI_HEST_GLOBAL) {
		if ((pci->is_pcie && (pci->pcie_type == pcie_type)) || bridge)
			*firmware_first = !!(p->flags & ACPI_HEST_FIRMWARE_FIRST);
	}
	else
		if (hest_match_pci(p, pci))
			*firmware_first = !!(p->flags & ACPI_HEST_FIRMWARE_FIRST);
	return rc;
}

static int acpi_hest_firmware_first(struct acpi_table_header *stdheader, struct pci_dev *pci)
{
	struct acpi_table_hest *hest = (struct acpi_table_hest *)stdheader;
	void *p = (void *)hest + sizeof(*hest); /* defined by the ACPI 4.0 spec */
	struct acpi_hest_header *hdr = p;

	int i;
	int firmware_first = 0;
	static unsigned char printed_unused = 0;
	static unsigned char printed_reserved = 0;

	for (i=0, hdr=p; p < (((void *)hest) + hest->header.length) && i < hest->error_source_count; i++) {
		switch (hdr->type) {
		case ACPI_HEST_TYPE_IA32_CHECK:
			p += parse_acpi_hest_ia_machine_check(p);
			break;
		case ACPI_HEST_TYPE_IA32_CORRECTED_CHECK:
			p += parse_acpi_hest_ia_corrected(p);
			break;
		case ACPI_HEST_TYPE_IA32_NMI:
			p += parse_acpi_hest_ia_nmi(p);
			break;
		/* These three should never appear */
		case ACPI_HEST_TYPE_NOT_USED3:
		case ACPI_HEST_TYPE_NOT_USED4:
		case ACPI_HEST_TYPE_NOT_USED5:
			if (!printed_unused) {
				printk(KERN_DEBUG PREFIX
				       "HEST Error Source list contains an obsolete type (%d).\n", hdr->type);
				printed_unused = 1;
			}
			break;
		case ACPI_HEST_TYPE_AER_ROOT_PORT:
		case ACPI_HEST_TYPE_AER_ENDPOINT:
		case ACPI_HEST_TYPE_AER_BRIDGE:
			p += parse_acpi_hest_aer(p, hdr->type, pci, &firmware_first);
			break;
		case ACPI_HEST_TYPE_GENERIC_ERROR:
			p += parse_acpi_hest_generic(p);
			break;
		/* These should never appear either */
		case ACPI_HEST_TYPE_RESERVED:
		default:
			if (!printed_reserved) {
				printk(KERN_DEBUG PREFIX
				       "HEST Error Source list contains a reserved type (%d).\n", hdr->type);
				printed_reserved = 1;
			}
			break;
		}
	}
	return firmware_first;
}

int acpi_hest_firmware_first_pci(struct pci_dev *pci)
{
	acpi_status status = AE_NOT_FOUND;
	struct acpi_table_header *hest = NULL;

	if (acpi_disabled)
		return 0;

	status = acpi_get_table(ACPI_SIG_HEST, 1, &hest);

	if (ACPI_SUCCESS(status)) {
		if (acpi_hest_firmware_first(hest, pci)) {
			return 1;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(acpi_hest_firmware_first_pci);
