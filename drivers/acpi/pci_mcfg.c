// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Broadcom
 *	Author: Jayachandran C <jchandra@broadcom.com>
 * Copyright (C) 2016 Semihalf
 * 	Author: Tomasz Nowicki <tn@semihalf.com>
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/pci-ecam.h>

/* Structure to hold entries from the MCFG table */
struct mcfg_entry {
	struct list_head	list;
	phys_addr_t		addr;
	u16			segment;
	u8			bus_start;
	u8			bus_end;
};

#ifdef CONFIG_PCI_QUIRKS
struct mcfg_fixup {
	char oem_id[ACPI_OEM_ID_SIZE + 1];
	char oem_table_id[ACPI_OEM_TABLE_ID_SIZE + 1];
	u32 oem_revision;
	u16 segment;
	struct resource bus_range;
	const struct pci_ecam_ops *ops;
	struct resource cfgres;
};

#define MCFG_BUS_RANGE(start, end)	DEFINE_RES_NAMED((start),	\
						((end) - (start) + 1),	\
						NULL, IORESOURCE_BUS)
#define MCFG_BUS_ANY			MCFG_BUS_RANGE(0x0, 0xff)

static struct mcfg_fixup mcfg_quirks[] = {
/*	{ OEM_ID, OEM_TABLE_ID, REV, SEGMENT, BUS_RANGE, ops, cfgres }, */

#define AL_ECAM(table_id, rev, seg, ops) \
	{ "AMAZON", table_id, rev, seg, MCFG_BUS_ANY, ops }

	AL_ECAM("GRAVITON", 0, 0, &al_pcie_ops),
	AL_ECAM("GRAVITON", 0, 1, &al_pcie_ops),
	AL_ECAM("GRAVITON", 0, 2, &al_pcie_ops),
	AL_ECAM("GRAVITON", 0, 3, &al_pcie_ops),
	AL_ECAM("GRAVITON", 0, 4, &al_pcie_ops),
	AL_ECAM("GRAVITON", 0, 5, &al_pcie_ops),
	AL_ECAM("GRAVITON", 0, 6, &al_pcie_ops),
	AL_ECAM("GRAVITON", 0, 7, &al_pcie_ops),

#define QCOM_ECAM32(seg) \
	{ "QCOM  ", "QDF2432 ", 1, seg, MCFG_BUS_ANY, &pci_32b_ops }

	QCOM_ECAM32(0),
	QCOM_ECAM32(1),
	QCOM_ECAM32(2),
	QCOM_ECAM32(3),
	QCOM_ECAM32(4),
	QCOM_ECAM32(5),
	QCOM_ECAM32(6),
	QCOM_ECAM32(7),

#define HISI_QUAD_DOM(table_id, seg, ops) \
	{ "HISI  ", table_id, 0, (seg) + 0, MCFG_BUS_ANY, ops }, \
	{ "HISI  ", table_id, 0, (seg) + 1, MCFG_BUS_ANY, ops }, \
	{ "HISI  ", table_id, 0, (seg) + 2, MCFG_BUS_ANY, ops }, \
	{ "HISI  ", table_id, 0, (seg) + 3, MCFG_BUS_ANY, ops }

	HISI_QUAD_DOM("HIP05   ",  0, &hisi_pcie_ops),
	HISI_QUAD_DOM("HIP06   ",  0, &hisi_pcie_ops),
	HISI_QUAD_DOM("HIP07   ",  0, &hisi_pcie_ops),
	HISI_QUAD_DOM("HIP07   ",  4, &hisi_pcie_ops),
	HISI_QUAD_DOM("HIP07   ",  8, &hisi_pcie_ops),
	HISI_QUAD_DOM("HIP07   ", 12, &hisi_pcie_ops),

#define THUNDER_PEM_RES(addr, node) \
	DEFINE_RES_MEM((addr) + ((u64) (node) << 44), 0x39 * SZ_16M)

#define THUNDER_PEM_QUIRK(rev, node) \
	{ "CAVIUM", "THUNDERX", rev, 4 + (10 * (node)), MCFG_BUS_ANY,	    \
	  &thunder_pem_ecam_ops, THUNDER_PEM_RES(0x88001f000000UL, node) },  \
	{ "CAVIUM", "THUNDERX", rev, 5 + (10 * (node)), MCFG_BUS_ANY,	    \
	  &thunder_pem_ecam_ops, THUNDER_PEM_RES(0x884057000000UL, node) },  \
	{ "CAVIUM", "THUNDERX", rev, 6 + (10 * (node)), MCFG_BUS_ANY,	    \
	  &thunder_pem_ecam_ops, THUNDER_PEM_RES(0x88808f000000UL, node) },  \
	{ "CAVIUM", "THUNDERX", rev, 7 + (10 * (node)), MCFG_BUS_ANY,	    \
	  &thunder_pem_ecam_ops, THUNDER_PEM_RES(0x89001f000000UL, node) },  \
	{ "CAVIUM", "THUNDERX", rev, 8 + (10 * (node)), MCFG_BUS_ANY,	    \
	  &thunder_pem_ecam_ops, THUNDER_PEM_RES(0x894057000000UL, node) },  \
	{ "CAVIUM", "THUNDERX", rev, 9 + (10 * (node)), MCFG_BUS_ANY,	    \
	  &thunder_pem_ecam_ops, THUNDER_PEM_RES(0x89808f000000UL, node) }

#define THUNDER_ECAM_QUIRK(rev, seg)					\
	{ "CAVIUM", "THUNDERX", rev, seg, MCFG_BUS_ANY,			\
	&pci_thunder_ecam_ops }

	/* SoC pass2.x */
	THUNDER_PEM_QUIRK(1, 0),
	THUNDER_PEM_QUIRK(1, 1),
	THUNDER_ECAM_QUIRK(1, 10),

	/* SoC pass1.x */
	THUNDER_PEM_QUIRK(2, 0),	/* off-chip devices */
	THUNDER_PEM_QUIRK(2, 1),	/* off-chip devices */
	THUNDER_ECAM_QUIRK(2,  0),
	THUNDER_ECAM_QUIRK(2,  1),
	THUNDER_ECAM_QUIRK(2,  2),
	THUNDER_ECAM_QUIRK(2,  3),
	THUNDER_ECAM_QUIRK(2, 10),
	THUNDER_ECAM_QUIRK(2, 11),
	THUNDER_ECAM_QUIRK(2, 12),
	THUNDER_ECAM_QUIRK(2, 13),

#define XGENE_V1_ECAM_MCFG(rev, seg) \
	{"APM   ", "XGENE   ", rev, seg, MCFG_BUS_ANY, \
		&xgene_v1_pcie_ecam_ops }

#define XGENE_V2_ECAM_MCFG(rev, seg) \
	{"APM   ", "XGENE   ", rev, seg, MCFG_BUS_ANY, \
		&xgene_v2_pcie_ecam_ops }

	/* X-Gene SoC with v1 PCIe controller */
	XGENE_V1_ECAM_MCFG(1, 0),
	XGENE_V1_ECAM_MCFG(1, 1),
	XGENE_V1_ECAM_MCFG(1, 2),
	XGENE_V1_ECAM_MCFG(1, 3),
	XGENE_V1_ECAM_MCFG(1, 4),
	XGENE_V1_ECAM_MCFG(2, 0),
	XGENE_V1_ECAM_MCFG(2, 1),
	XGENE_V1_ECAM_MCFG(2, 2),
	XGENE_V1_ECAM_MCFG(2, 3),
	XGENE_V1_ECAM_MCFG(2, 4),
	/* X-Gene SoC with v2.1 PCIe controller */
	XGENE_V2_ECAM_MCFG(3, 0),
	XGENE_V2_ECAM_MCFG(3, 1),
	/* X-Gene SoC with v2.2 PCIe controller */
	XGENE_V2_ECAM_MCFG(4, 0),
	XGENE_V2_ECAM_MCFG(4, 1),
	XGENE_V2_ECAM_MCFG(4, 2),

#define ALTRA_ECAM_QUIRK(rev, seg) \
	{ "Ampere", "Altra   ", rev, seg, MCFG_BUS_ANY, &pci_32b_read_ops }

	ALTRA_ECAM_QUIRK(1, 0),
	ALTRA_ECAM_QUIRK(1, 1),
	ALTRA_ECAM_QUIRK(1, 2),
	ALTRA_ECAM_QUIRK(1, 3),
	ALTRA_ECAM_QUIRK(1, 4),
	ALTRA_ECAM_QUIRK(1, 5),
	ALTRA_ECAM_QUIRK(1, 6),
	ALTRA_ECAM_QUIRK(1, 7),
	ALTRA_ECAM_QUIRK(1, 8),
	ALTRA_ECAM_QUIRK(1, 9),
	ALTRA_ECAM_QUIRK(1, 10),
	ALTRA_ECAM_QUIRK(1, 11),
	ALTRA_ECAM_QUIRK(1, 12),
	ALTRA_ECAM_QUIRK(1, 13),
	ALTRA_ECAM_QUIRK(1, 14),
	ALTRA_ECAM_QUIRK(1, 15),
};

static char mcfg_oem_id[ACPI_OEM_ID_SIZE];
static char mcfg_oem_table_id[ACPI_OEM_TABLE_ID_SIZE];
static u32 mcfg_oem_revision;

static int pci_mcfg_quirk_matches(struct mcfg_fixup *f, u16 segment,
				  struct resource *bus_range)
{
	if (!memcmp(f->oem_id, mcfg_oem_id, ACPI_OEM_ID_SIZE) &&
	    !memcmp(f->oem_table_id, mcfg_oem_table_id,
		    ACPI_OEM_TABLE_ID_SIZE) &&
	    f->oem_revision == mcfg_oem_revision &&
	    f->segment == segment &&
	    resource_contains(&f->bus_range, bus_range))
		return 1;

	return 0;
}
#endif

static void pci_mcfg_apply_quirks(struct acpi_pci_root *root,
				  struct resource *cfgres,
				  const struct pci_ecam_ops **ecam_ops)
{
#ifdef CONFIG_PCI_QUIRKS
	u16 segment = root->segment;
	struct resource *bus_range = &root->secondary;
	struct mcfg_fixup *f;
	int i;

	for (i = 0, f = mcfg_quirks; i < ARRAY_SIZE(mcfg_quirks); i++, f++) {
		if (pci_mcfg_quirk_matches(f, segment, bus_range)) {
			if (f->cfgres.start)
				*cfgres = f->cfgres;
			if (f->ops)
				*ecam_ops =  f->ops;
			dev_info(&root->device->dev, "MCFG quirk: ECAM at %pR for %pR with %ps\n",
				 cfgres, bus_range, *ecam_ops);
			return;
		}
	}
#endif
}

/* List to save MCFG entries */
static LIST_HEAD(pci_mcfg_list);

int pci_mcfg_lookup(struct acpi_pci_root *root, struct resource *cfgres,
		    const struct pci_ecam_ops **ecam_ops)
{
	const struct pci_ecam_ops *ops = &pci_generic_ecam_ops;
	struct resource *bus_res = &root->secondary;
	u16 seg = root->segment;
	struct mcfg_entry *e;
	struct resource res;

	/* Use address from _CBA if present, otherwise lookup MCFG */
	if (root->mcfg_addr)
		goto skip_lookup;

	/*
	 * We expect the range in bus_res in the coverage of MCFG bus range.
	 */
	list_for_each_entry(e, &pci_mcfg_list, list) {
		if (e->segment == seg && e->bus_start <= bus_res->start &&
		    e->bus_end >= bus_res->end) {
			root->mcfg_addr = e->addr;
		}

	}

skip_lookup:
	memset(&res, 0, sizeof(res));
	if (root->mcfg_addr) {
		res.start = root->mcfg_addr + (bus_res->start << 20);
		res.end = res.start + (resource_size(bus_res) << 20) - 1;
		res.flags = IORESOURCE_MEM;
	}

	/*
	 * Allow quirks to override default ECAM ops and CFG resource
	 * range.  This may even fabricate a CFG resource range in case
	 * MCFG does not have it.  Invalid CFG start address means MCFG
	 * firmware bug or we need another quirk in array.
	 */
	pci_mcfg_apply_quirks(root, &res, &ops);
	if (!res.start)
		return -ENXIO;

	*cfgres = res;
	*ecam_ops = ops;
	return 0;
}

static __init int pci_mcfg_parse(struct acpi_table_header *header)
{
	struct acpi_table_mcfg *mcfg;
	struct acpi_mcfg_allocation *mptr;
	struct mcfg_entry *e, *arr;
	int i, n;

	if (header->length < sizeof(struct acpi_table_mcfg))
		return -EINVAL;

	n = (header->length - sizeof(struct acpi_table_mcfg)) /
					sizeof(struct acpi_mcfg_allocation);
	mcfg = (struct acpi_table_mcfg *)header;
	mptr = (struct acpi_mcfg_allocation *) &mcfg[1];

	arr = kcalloc(n, sizeof(*arr), GFP_KERNEL);
	if (!arr)
		return -ENOMEM;

	for (i = 0, e = arr; i < n; i++, mptr++, e++) {
		e->segment = mptr->pci_segment;
		e->addr =  mptr->address;
		e->bus_start = mptr->start_bus_number;
		e->bus_end = mptr->end_bus_number;
		list_add(&e->list, &pci_mcfg_list);
	}

#ifdef CONFIG_PCI_QUIRKS
	/* Save MCFG IDs and revision for quirks matching */
	memcpy(mcfg_oem_id, header->oem_id, ACPI_OEM_ID_SIZE);
	memcpy(mcfg_oem_table_id, header->oem_table_id, ACPI_OEM_TABLE_ID_SIZE);
	mcfg_oem_revision = header->oem_revision;
#endif

	pr_info("MCFG table detected, %d entries\n", n);
	return 0;
}

/* Interface called by ACPI - parse and save MCFG table */
void __init pci_mmcfg_late_init(void)
{
	int err = acpi_table_parse(ACPI_SIG_MCFG, pci_mcfg_parse);
	if (err)
		pr_debug("Failed to parse MCFG (%d)\n", err);
}
