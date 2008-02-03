/*
 * Intel I/OAT DMA Linux driver
 * Copyright(c) 2007 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/dca.h>

/* either a kernel change is needed, or we need something like this in kernel */
#ifndef CONFIG_SMP
#include <asm/smp.h>
#undef cpu_physical_id
#define cpu_physical_id(cpu) (cpuid_ebx(1) >> 24)
#endif

#include "ioatdma.h"
#include "ioatdma_registers.h"

/*
 * Bit 16 of a tag map entry is the "valid" bit, if it is set then bits 0:15
 * contain the bit number of the APIC ID to map into the DCA tag.  If the valid
 * bit is not set, then the value must be 0 or 1 and defines the bit in the tag.
 */
#define DCA_TAG_MAP_VALID 0x80

/*
 * "Legacy" DCA systems do not implement the DCA register set in the
 * I/OAT device.  Software needs direct support for their tag mappings.
 */

#define APICID_BIT(x)		(DCA_TAG_MAP_VALID | (x))
#define IOAT_TAG_MAP_LEN	8

static u8 ioat_tag_map_BNB[IOAT_TAG_MAP_LEN] = {
	1, APICID_BIT(1), APICID_BIT(2), APICID_BIT(2), };
static u8 ioat_tag_map_SCNB[IOAT_TAG_MAP_LEN] = {
	1, APICID_BIT(1), APICID_BIT(2), APICID_BIT(2), };
static u8 ioat_tag_map_CNB[IOAT_TAG_MAP_LEN] = {
	1, APICID_BIT(1), APICID_BIT(3), APICID_BIT(4), APICID_BIT(2), };
static u8 ioat_tag_map_UNISYS[IOAT_TAG_MAP_LEN] = { 0 };

/* pack PCI B/D/F into a u16 */
static inline u16 dcaid_from_pcidev(struct pci_dev *pci)
{
	return (pci->bus->number << 8) | pci->devfn;
}

static int dca_enabled_in_bios(struct pci_dev *pdev)
{
	/* CPUID level 9 returns DCA configuration */
	/* Bit 0 indicates DCA enabled by the BIOS */
	unsigned long cpuid_level_9;
	int res;

	cpuid_level_9 = cpuid_eax(9);
	res = test_bit(0, &cpuid_level_9);
	if (!res)
		dev_err(&pdev->dev, "DCA is disabled in BIOS\n");

	return res;
}

static int system_has_dca_enabled(struct pci_dev *pdev)
{
	if (boot_cpu_has(X86_FEATURE_DCA))
		return dca_enabled_in_bios(pdev);

	dev_err(&pdev->dev, "boot cpu doesn't have X86_FEATURE_DCA\n");
	return 0;
}

struct ioat_dca_slot {
	struct pci_dev *pdev;	/* requester device */
	u16 rid;		/* requester id, as used by IOAT */
};

#define IOAT_DCA_MAX_REQ 6

struct ioat_dca_priv {
	void __iomem		*iobase;
	void			*dca_base;
	int			 max_requesters;
	int			 requester_count;
	u8			 tag_map[IOAT_TAG_MAP_LEN];
	struct ioat_dca_slot 	 req_slots[0];
};

/* 5000 series chipset DCA Port Requester ID Table Entry Format
 * [15:8]	PCI-Express Bus Number
 * [7:3]	PCI-Express Device Number
 * [2:0]	PCI-Express Function Number
 *
 * 5000 series chipset DCA control register format
 * [7:1]	Reserved (0)
 * [0]		Ignore Function Number
 */

static int ioat_dca_add_requester(struct dca_provider *dca, struct device *dev)
{
	struct ioat_dca_priv *ioatdca = dca_priv(dca);
	struct pci_dev *pdev;
	int i;
	u16 id;

	/* This implementation only supports PCI-Express */
	if (dev->bus != &pci_bus_type)
		return -ENODEV;
	pdev = to_pci_dev(dev);
	id = dcaid_from_pcidev(pdev);

	if (ioatdca->requester_count == ioatdca->max_requesters)
		return -ENODEV;

	for (i = 0; i < ioatdca->max_requesters; i++) {
		if (ioatdca->req_slots[i].pdev == NULL) {
			/* found an empty slot */
			ioatdca->requester_count++;
			ioatdca->req_slots[i].pdev = pdev;
			ioatdca->req_slots[i].rid = id;
			writew(id, ioatdca->dca_base + (i * 4));
			/* make sure the ignore function bit is off */
			writeb(0, ioatdca->dca_base + (i * 4) + 2);
			return i;
		}
	}
	/* Error, ioatdma->requester_count is out of whack */
	return -EFAULT;
}

static int ioat_dca_remove_requester(struct dca_provider *dca,
				     struct device *dev)
{
	struct ioat_dca_priv *ioatdca = dca_priv(dca);
	struct pci_dev *pdev;
	int i;

	/* This implementation only supports PCI-Express */
	if (dev->bus != &pci_bus_type)
		return -ENODEV;
	pdev = to_pci_dev(dev);

	for (i = 0; i < ioatdca->max_requesters; i++) {
		if (ioatdca->req_slots[i].pdev == pdev) {
			writew(0, ioatdca->dca_base + (i * 4));
			ioatdca->req_slots[i].pdev = NULL;
			ioatdca->req_slots[i].rid = 0;
			ioatdca->requester_count--;
			return i;
		}
	}
	return -ENODEV;
}

static u8 ioat_dca_get_tag(struct dca_provider *dca, int cpu)
{
	struct ioat_dca_priv *ioatdca = dca_priv(dca);
	int i, apic_id, bit, value;
	u8 entry, tag;

	tag = 0;
	apic_id = cpu_physical_id(cpu);

	for (i = 0; i < IOAT_TAG_MAP_LEN; i++) {
		entry = ioatdca->tag_map[i];
		if (entry & DCA_TAG_MAP_VALID) {
			bit = entry & ~DCA_TAG_MAP_VALID;
			value = (apic_id & (1 << bit)) ? 1 : 0;
		} else {
			value = entry ? 1 : 0;
		}
		tag |= (value << i);
	}
	return tag;
}

static struct dca_ops ioat_dca_ops = {
	.add_requester		= ioat_dca_add_requester,
	.remove_requester	= ioat_dca_remove_requester,
	.get_tag		= ioat_dca_get_tag,
};


struct dca_provider *ioat_dca_init(struct pci_dev *pdev, void __iomem *iobase)
{
	struct dca_provider *dca;
	struct ioat_dca_priv *ioatdca;
	u8 *tag_map = NULL;
	int i;
	int err;

	if (!system_has_dca_enabled(pdev))
		return NULL;

	/* I/OAT v1 systems must have a known tag_map to support DCA */
	switch (pdev->vendor) {
	case PCI_VENDOR_ID_INTEL:
		switch (pdev->device) {
		case PCI_DEVICE_ID_INTEL_IOAT:
			tag_map = ioat_tag_map_BNB;
			break;
		case PCI_DEVICE_ID_INTEL_IOAT_CNB:
			tag_map = ioat_tag_map_CNB;
			break;
		case PCI_DEVICE_ID_INTEL_IOAT_SCNB:
			tag_map = ioat_tag_map_SCNB;
			break;
		}
		break;
	case PCI_VENDOR_ID_UNISYS:
		switch (pdev->device) {
		case PCI_DEVICE_ID_UNISYS_DMA_DIRECTOR:
			tag_map = ioat_tag_map_UNISYS;
			break;
		}
		break;
	}
	if (tag_map == NULL)
		return NULL;

	dca = alloc_dca_provider(&ioat_dca_ops,
			sizeof(*ioatdca) +
			(sizeof(struct ioat_dca_slot) * IOAT_DCA_MAX_REQ));
	if (!dca)
		return NULL;

	ioatdca = dca_priv(dca);
	ioatdca->max_requesters = IOAT_DCA_MAX_REQ;

	ioatdca->dca_base = iobase + 0x54;

	/* copy over the APIC ID to DCA tag mapping */
	for (i = 0; i < IOAT_TAG_MAP_LEN; i++)
		ioatdca->tag_map[i] = tag_map[i];

	err = register_dca_provider(dca, &pdev->dev);
	if (err) {
		free_dca_provider(dca);
		return NULL;
	}

	return dca;
}


static int ioat2_dca_add_requester(struct dca_provider *dca, struct device *dev)
{
	struct ioat_dca_priv *ioatdca = dca_priv(dca);
	struct pci_dev *pdev;
	int i;
	u16 id;
	u16 global_req_table;

	/* This implementation only supports PCI-Express */
	if (dev->bus != &pci_bus_type)
		return -ENODEV;
	pdev = to_pci_dev(dev);
	id = dcaid_from_pcidev(pdev);

	if (ioatdca->requester_count == ioatdca->max_requesters)
		return -ENODEV;

	for (i = 0; i < ioatdca->max_requesters; i++) {
		if (ioatdca->req_slots[i].pdev == NULL) {
			/* found an empty slot */
			ioatdca->requester_count++;
			ioatdca->req_slots[i].pdev = pdev;
			ioatdca->req_slots[i].rid = id;
			global_req_table =
			      readw(ioatdca->dca_base + IOAT_DCA_GREQID_OFFSET);
			writel(id | IOAT_DCA_GREQID_VALID,
			       ioatdca->iobase + global_req_table + (i * 4));
			return i;
		}
	}
	/* Error, ioatdma->requester_count is out of whack */
	return -EFAULT;
}

static int ioat2_dca_remove_requester(struct dca_provider *dca,
				      struct device *dev)
{
	struct ioat_dca_priv *ioatdca = dca_priv(dca);
	struct pci_dev *pdev;
	int i;
	u16 global_req_table;

	/* This implementation only supports PCI-Express */
	if (dev->bus != &pci_bus_type)
		return -ENODEV;
	pdev = to_pci_dev(dev);

	for (i = 0; i < ioatdca->max_requesters; i++) {
		if (ioatdca->req_slots[i].pdev == pdev) {
			global_req_table =
			      readw(ioatdca->dca_base + IOAT_DCA_GREQID_OFFSET);
			writel(0, ioatdca->iobase + global_req_table + (i * 4));
			ioatdca->req_slots[i].pdev = NULL;
			ioatdca->req_slots[i].rid = 0;
			ioatdca->requester_count--;
			return i;
		}
	}
	return -ENODEV;
}

static u8 ioat2_dca_get_tag(struct dca_provider *dca, int cpu)
{
	u8 tag;

	tag = ioat_dca_get_tag(dca, cpu);
	tag = (~tag) & 0x1F;
	return tag;
}

static struct dca_ops ioat2_dca_ops = {
	.add_requester		= ioat2_dca_add_requester,
	.remove_requester	= ioat2_dca_remove_requester,
	.get_tag		= ioat2_dca_get_tag,
};

static int ioat2_dca_count_dca_slots(void *iobase, u16 dca_offset)
{
	int slots = 0;
	u32 req;
	u16 global_req_table;

	global_req_table = readw(iobase + dca_offset + IOAT_DCA_GREQID_OFFSET);
	if (global_req_table == 0)
		return 0;
	do {
		req = readl(iobase + global_req_table + (slots * sizeof(u32)));
		slots++;
	} while ((req & IOAT_DCA_GREQID_LASTID) == 0);

	return slots;
}

struct dca_provider *ioat2_dca_init(struct pci_dev *pdev, void __iomem *iobase)
{
	struct dca_provider *dca;
	struct ioat_dca_priv *ioatdca;
	int slots;
	int i;
	int err;
	u32 tag_map;
	u16 dca_offset;
	u16 csi_fsb_control;
	u16 pcie_control;
	u8 bit;

	if (!system_has_dca_enabled(pdev))
		return NULL;

	dca_offset = readw(iobase + IOAT_DCAOFFSET_OFFSET);
	if (dca_offset == 0)
		return NULL;

	slots = ioat2_dca_count_dca_slots(iobase, dca_offset);
	if (slots == 0)
		return NULL;

	dca = alloc_dca_provider(&ioat2_dca_ops,
				 sizeof(*ioatdca)
				      + (sizeof(struct ioat_dca_slot) * slots));
	if (!dca)
		return NULL;

	ioatdca = dca_priv(dca);
	ioatdca->iobase = iobase;
	ioatdca->dca_base = iobase + dca_offset;
	ioatdca->max_requesters = slots;

	/* some bios might not know to turn these on */
	csi_fsb_control = readw(ioatdca->dca_base + IOAT_FSB_CAP_ENABLE_OFFSET);
	if ((csi_fsb_control & IOAT_FSB_CAP_ENABLE_PREFETCH) == 0) {
		csi_fsb_control |= IOAT_FSB_CAP_ENABLE_PREFETCH;
		writew(csi_fsb_control,
		       ioatdca->dca_base + IOAT_FSB_CAP_ENABLE_OFFSET);
	}
	pcie_control = readw(ioatdca->dca_base + IOAT_PCI_CAP_ENABLE_OFFSET);
	if ((pcie_control & IOAT_PCI_CAP_ENABLE_MEMWR) == 0) {
		pcie_control |= IOAT_PCI_CAP_ENABLE_MEMWR;
		writew(pcie_control,
		       ioatdca->dca_base + IOAT_PCI_CAP_ENABLE_OFFSET);
	}


	/* TODO version, compatibility and configuration checks */

	/* copy out the APIC to DCA tag map */
	tag_map = readl(ioatdca->dca_base + IOAT_APICID_TAG_MAP_OFFSET);
	for (i = 0; i < 5; i++) {
		bit = (tag_map >> (4 * i)) & 0x0f;
		if (bit < 8)
			ioatdca->tag_map[i] = bit | DCA_TAG_MAP_VALID;
		else
			ioatdca->tag_map[i] = 0;
	}

	err = register_dca_provider(dca, &pdev->dev);
	if (err) {
		free_dca_provider(dca);
		return NULL;
	}

	return dca;
}
