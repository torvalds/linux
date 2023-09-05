// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Support PCI/PCIe on PowerNV platforms
 *
 * Copyright 2011 Benjamin Herrenschmidt, IBM Corp.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/crash_dump.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/msi.h>
#include <linux/iommu.h>
#include <linux/rculist.h>
#include <linux/sizes.h>
#include <linux/debugfs.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/msi_bitmap.h>
#include <asm/ppc-pci.h>
#include <asm/opal.h>
#include <asm/iommu.h>
#include <asm/tce.h>
#include <asm/xics.h>
#include <asm/firmware.h>
#include <asm/pnv-pci.h>
#include <asm/mmzone.h>
#include <asm/xive.h>

#include <misc/cxl-base.h>

#include "powernv.h"
#include "pci.h"
#include "../../../../drivers/pci/pci.h"

/* This array is indexed with enum pnv_phb_type */
static const char * const pnv_phb_names[] = { "IODA2", "NPU_OCAPI" };

static void pnv_pci_ioda2_set_bypass(struct pnv_ioda_pe *pe, bool enable);
static void pnv_pci_configure_bus(struct pci_bus *bus);

void pe_level_printk(const struct pnv_ioda_pe *pe, const char *level,
			    const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	char pfix[32];

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (pe->flags & PNV_IODA_PE_DEV)
		strscpy(pfix, dev_name(&pe->pdev->dev), sizeof(pfix));
	else if (pe->flags & (PNV_IODA_PE_BUS | PNV_IODA_PE_BUS_ALL))
		sprintf(pfix, "%04x:%02x     ",
			pci_domain_nr(pe->pbus), pe->pbus->number);
#ifdef CONFIG_PCI_IOV
	else if (pe->flags & PNV_IODA_PE_VF)
		sprintf(pfix, "%04x:%02x:%2x.%d",
			pci_domain_nr(pe->parent_dev->bus),
			(pe->rid & 0xff00) >> 8,
			PCI_SLOT(pe->rid), PCI_FUNC(pe->rid));
#endif /* CONFIG_PCI_IOV*/

	printk("%spci %s: [PE# %.2x] %pV",
	       level, pfix, pe->pe_number, &vaf);

	va_end(args);
}

static bool pnv_iommu_bypass_disabled __read_mostly;
static bool pci_reset_phbs __read_mostly;

static int __init iommu_setup(char *str)
{
	if (!str)
		return -EINVAL;

	while (*str) {
		if (!strncmp(str, "nobypass", 8)) {
			pnv_iommu_bypass_disabled = true;
			pr_info("PowerNV: IOMMU bypass window disabled.\n");
			break;
		}
		str += strcspn(str, ",");
		if (*str == ',')
			str++;
	}

	return 0;
}
early_param("iommu", iommu_setup);

static int __init pci_reset_phbs_setup(char *str)
{
	pci_reset_phbs = true;
	return 0;
}

early_param("ppc_pci_reset_phbs", pci_reset_phbs_setup);

static struct pnv_ioda_pe *pnv_ioda_init_pe(struct pnv_phb *phb, int pe_no)
{
	s64 rc;

	phb->ioda.pe_array[pe_no].phb = phb;
	phb->ioda.pe_array[pe_no].pe_number = pe_no;
	phb->ioda.pe_array[pe_no].dma_setup_done = false;

	/*
	 * Clear the PE frozen state as it might be put into frozen state
	 * in the last PCI remove path. It's not harmful to do so when the
	 * PE is already in unfrozen state.
	 */
	rc = opal_pci_eeh_freeze_clear(phb->opal_id, pe_no,
				       OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);
	if (rc != OPAL_SUCCESS && rc != OPAL_UNSUPPORTED)
		pr_warn("%s: Error %lld unfreezing PHB#%x-PE#%x\n",
			__func__, rc, phb->hose->global_number, pe_no);

	return &phb->ioda.pe_array[pe_no];
}

static void pnv_ioda_reserve_pe(struct pnv_phb *phb, int pe_no)
{
	if (!(pe_no >= 0 && pe_no < phb->ioda.total_pe_num)) {
		pr_warn("%s: Invalid PE %x on PHB#%x\n",
			__func__, pe_no, phb->hose->global_number);
		return;
	}

	mutex_lock(&phb->ioda.pe_alloc_mutex);
	if (test_and_set_bit(pe_no, phb->ioda.pe_alloc))
		pr_debug("%s: PE %x was reserved on PHB#%x\n",
			 __func__, pe_no, phb->hose->global_number);
	mutex_unlock(&phb->ioda.pe_alloc_mutex);

	pnv_ioda_init_pe(phb, pe_no);
}

struct pnv_ioda_pe *pnv_ioda_alloc_pe(struct pnv_phb *phb, int count)
{
	struct pnv_ioda_pe *ret = NULL;
	int run = 0, pe, i;

	mutex_lock(&phb->ioda.pe_alloc_mutex);

	/* scan backwards for a run of @count cleared bits */
	for (pe = phb->ioda.total_pe_num - 1; pe >= 0; pe--) {
		if (test_bit(pe, phb->ioda.pe_alloc)) {
			run = 0;
			continue;
		}

		run++;
		if (run == count)
			break;
	}
	if (run != count)
		goto out;

	for (i = pe; i < pe + count; i++) {
		set_bit(i, phb->ioda.pe_alloc);
		pnv_ioda_init_pe(phb, i);
	}
	ret = &phb->ioda.pe_array[pe];

out:
	mutex_unlock(&phb->ioda.pe_alloc_mutex);
	return ret;
}

void pnv_ioda_free_pe(struct pnv_ioda_pe *pe)
{
	struct pnv_phb *phb = pe->phb;
	unsigned int pe_num = pe->pe_number;

	WARN_ON(pe->pdev);
	memset(pe, 0, sizeof(struct pnv_ioda_pe));

	mutex_lock(&phb->ioda.pe_alloc_mutex);
	clear_bit(pe_num, phb->ioda.pe_alloc);
	mutex_unlock(&phb->ioda.pe_alloc_mutex);
}

/* The default M64 BAR is shared by all PEs */
static int pnv_ioda2_init_m64(struct pnv_phb *phb)
{
	const char *desc;
	struct resource *r;
	s64 rc;

	/* Configure the default M64 BAR */
	rc = opal_pci_set_phb_mem_window(phb->opal_id,
					 OPAL_M64_WINDOW_TYPE,
					 phb->ioda.m64_bar_idx,
					 phb->ioda.m64_base,
					 0, /* unused */
					 phb->ioda.m64_size);
	if (rc != OPAL_SUCCESS) {
		desc = "configuring";
		goto fail;
	}

	/* Enable the default M64 BAR */
	rc = opal_pci_phb_mmio_enable(phb->opal_id,
				      OPAL_M64_WINDOW_TYPE,
				      phb->ioda.m64_bar_idx,
				      OPAL_ENABLE_M64_SPLIT);
	if (rc != OPAL_SUCCESS) {
		desc = "enabling";
		goto fail;
	}

	/*
	 * Exclude the segments for reserved and root bus PE, which
	 * are first or last two PEs.
	 */
	r = &phb->hose->mem_resources[1];
	if (phb->ioda.reserved_pe_idx == 0)
		r->start += (2 * phb->ioda.m64_segsize);
	else if (phb->ioda.reserved_pe_idx == (phb->ioda.total_pe_num - 1))
		r->end -= (2 * phb->ioda.m64_segsize);
	else
		pr_warn("  Cannot strip M64 segment for reserved PE#%x\n",
			phb->ioda.reserved_pe_idx);

	return 0;

fail:
	pr_warn("  Failure %lld %s M64 BAR#%d\n",
		rc, desc, phb->ioda.m64_bar_idx);
	opal_pci_phb_mmio_enable(phb->opal_id,
				 OPAL_M64_WINDOW_TYPE,
				 phb->ioda.m64_bar_idx,
				 OPAL_DISABLE_M64);
	return -EIO;
}

static void pnv_ioda_reserve_dev_m64_pe(struct pci_dev *pdev,
					 unsigned long *pe_bitmap)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(pdev->bus);
	struct resource *r;
	resource_size_t base, sgsz, start, end;
	int segno, i;

	base = phb->ioda.m64_base;
	sgsz = phb->ioda.m64_segsize;
	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		r = &pdev->resource[i];
		if (!r->parent || !pnv_pci_is_m64(phb, r))
			continue;

		start = ALIGN_DOWN(r->start - base, sgsz);
		end = ALIGN(r->end - base, sgsz);
		for (segno = start / sgsz; segno < end / sgsz; segno++) {
			if (pe_bitmap)
				set_bit(segno, pe_bitmap);
			else
				pnv_ioda_reserve_pe(phb, segno);
		}
	}
}

static void pnv_ioda_reserve_m64_pe(struct pci_bus *bus,
				    unsigned long *pe_bitmap,
				    bool all)
{
	struct pci_dev *pdev;

	list_for_each_entry(pdev, &bus->devices, bus_list) {
		pnv_ioda_reserve_dev_m64_pe(pdev, pe_bitmap);

		if (all && pdev->subordinate)
			pnv_ioda_reserve_m64_pe(pdev->subordinate,
						pe_bitmap, all);
	}
}

static struct pnv_ioda_pe *pnv_ioda_pick_m64_pe(struct pci_bus *bus, bool all)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(bus);
	struct pnv_ioda_pe *master_pe, *pe;
	unsigned long size, *pe_alloc;
	int i;

	/* Root bus shouldn't use M64 */
	if (pci_is_root_bus(bus))
		return NULL;

	/* Allocate bitmap */
	size = ALIGN(phb->ioda.total_pe_num / 8, sizeof(unsigned long));
	pe_alloc = kzalloc(size, GFP_KERNEL);
	if (!pe_alloc) {
		pr_warn("%s: Out of memory !\n",
			__func__);
		return NULL;
	}

	/* Figure out reserved PE numbers by the PE */
	pnv_ioda_reserve_m64_pe(bus, pe_alloc, all);

	/*
	 * the current bus might not own M64 window and that's all
	 * contributed by its child buses. For the case, we needn't
	 * pick M64 dependent PE#.
	 */
	if (bitmap_empty(pe_alloc, phb->ioda.total_pe_num)) {
		kfree(pe_alloc);
		return NULL;
	}

	/*
	 * Figure out the master PE and put all slave PEs to master
	 * PE's list to form compound PE.
	 */
	master_pe = NULL;
	i = -1;
	while ((i = find_next_bit(pe_alloc, phb->ioda.total_pe_num, i + 1)) <
		phb->ioda.total_pe_num) {
		pe = &phb->ioda.pe_array[i];

		phb->ioda.m64_segmap[pe->pe_number] = pe->pe_number;
		if (!master_pe) {
			pe->flags |= PNV_IODA_PE_MASTER;
			INIT_LIST_HEAD(&pe->slaves);
			master_pe = pe;
		} else {
			pe->flags |= PNV_IODA_PE_SLAVE;
			pe->master = master_pe;
			list_add_tail(&pe->list, &master_pe->slaves);
		}
	}

	kfree(pe_alloc);
	return master_pe;
}

static void __init pnv_ioda_parse_m64_window(struct pnv_phb *phb)
{
	struct pci_controller *hose = phb->hose;
	struct device_node *dn = hose->dn;
	struct resource *res;
	u32 m64_range[2], i;
	const __be32 *r;
	u64 pci_addr;

	if (phb->type != PNV_PHB_IODA2) {
		pr_info("  Not support M64 window\n");
		return;
	}

	if (!firmware_has_feature(FW_FEATURE_OPAL)) {
		pr_info("  Firmware too old to support M64 window\n");
		return;
	}

	r = of_get_property(dn, "ibm,opal-m64-window", NULL);
	if (!r) {
		pr_info("  No <ibm,opal-m64-window> on %pOF\n",
			dn);
		return;
	}

	/*
	 * Find the available M64 BAR range and pickup the last one for
	 * covering the whole 64-bits space. We support only one range.
	 */
	if (of_property_read_u32_array(dn, "ibm,opal-available-m64-ranges",
				       m64_range, 2)) {
		/* In absence of the property, assume 0..15 */
		m64_range[0] = 0;
		m64_range[1] = 16;
	}
	/* We only support 64 bits in our allocator */
	if (m64_range[1] > 63) {
		pr_warn("%s: Limiting M64 range to 63 (from %d) on PHB#%x\n",
			__func__, m64_range[1], phb->hose->global_number);
		m64_range[1] = 63;
	}
	/* Empty range, no m64 */
	if (m64_range[1] <= m64_range[0]) {
		pr_warn("%s: M64 empty, disabling M64 usage on PHB#%x\n",
			__func__, phb->hose->global_number);
		return;
	}

	/* Configure M64 informations */
	res = &hose->mem_resources[1];
	res->name = dn->full_name;
	res->start = of_translate_address(dn, r + 2);
	res->end = res->start + of_read_number(r + 4, 2) - 1;
	res->flags = (IORESOURCE_MEM | IORESOURCE_MEM_64 | IORESOURCE_PREFETCH);
	pci_addr = of_read_number(r, 2);
	hose->mem_offset[1] = res->start - pci_addr;

	phb->ioda.m64_size = resource_size(res);
	phb->ioda.m64_segsize = phb->ioda.m64_size / phb->ioda.total_pe_num;
	phb->ioda.m64_base = pci_addr;

	/* This lines up nicely with the display from processing OF ranges */
	pr_info(" MEM 0x%016llx..0x%016llx -> 0x%016llx (M64 #%d..%d)\n",
		res->start, res->end, pci_addr, m64_range[0],
		m64_range[0] + m64_range[1] - 1);

	/* Mark all M64 used up by default */
	phb->ioda.m64_bar_alloc = (unsigned long)-1;

	/* Use last M64 BAR to cover M64 window */
	m64_range[1]--;
	phb->ioda.m64_bar_idx = m64_range[0] + m64_range[1];

	pr_info(" Using M64 #%d as default window\n", phb->ioda.m64_bar_idx);

	/* Mark remaining ones free */
	for (i = m64_range[0]; i < m64_range[1]; i++)
		clear_bit(i, &phb->ioda.m64_bar_alloc);

	/*
	 * Setup init functions for M64 based on IODA version, IODA3 uses
	 * the IODA2 code.
	 */
	phb->init_m64 = pnv_ioda2_init_m64;
}

static void pnv_ioda_freeze_pe(struct pnv_phb *phb, int pe_no)
{
	struct pnv_ioda_pe *pe = &phb->ioda.pe_array[pe_no];
	struct pnv_ioda_pe *slave;
	s64 rc;

	/* Fetch master PE */
	if (pe->flags & PNV_IODA_PE_SLAVE) {
		pe = pe->master;
		if (WARN_ON(!pe || !(pe->flags & PNV_IODA_PE_MASTER)))
			return;

		pe_no = pe->pe_number;
	}

	/* Freeze master PE */
	rc = opal_pci_eeh_freeze_set(phb->opal_id,
				     pe_no,
				     OPAL_EEH_ACTION_SET_FREEZE_ALL);
	if (rc != OPAL_SUCCESS) {
		pr_warn("%s: Failure %lld freezing PHB#%x-PE#%x\n",
			__func__, rc, phb->hose->global_number, pe_no);
		return;
	}

	/* Freeze slave PEs */
	if (!(pe->flags & PNV_IODA_PE_MASTER))
		return;

	list_for_each_entry(slave, &pe->slaves, list) {
		rc = opal_pci_eeh_freeze_set(phb->opal_id,
					     slave->pe_number,
					     OPAL_EEH_ACTION_SET_FREEZE_ALL);
		if (rc != OPAL_SUCCESS)
			pr_warn("%s: Failure %lld freezing PHB#%x-PE#%x\n",
				__func__, rc, phb->hose->global_number,
				slave->pe_number);
	}
}

static int pnv_ioda_unfreeze_pe(struct pnv_phb *phb, int pe_no, int opt)
{
	struct pnv_ioda_pe *pe, *slave;
	s64 rc;

	/* Find master PE */
	pe = &phb->ioda.pe_array[pe_no];
	if (pe->flags & PNV_IODA_PE_SLAVE) {
		pe = pe->master;
		WARN_ON(!pe || !(pe->flags & PNV_IODA_PE_MASTER));
		pe_no = pe->pe_number;
	}

	/* Clear frozen state for master PE */
	rc = opal_pci_eeh_freeze_clear(phb->opal_id, pe_no, opt);
	if (rc != OPAL_SUCCESS) {
		pr_warn("%s: Failure %lld clear %d on PHB#%x-PE#%x\n",
			__func__, rc, opt, phb->hose->global_number, pe_no);
		return -EIO;
	}

	if (!(pe->flags & PNV_IODA_PE_MASTER))
		return 0;

	/* Clear frozen state for slave PEs */
	list_for_each_entry(slave, &pe->slaves, list) {
		rc = opal_pci_eeh_freeze_clear(phb->opal_id,
					     slave->pe_number,
					     opt);
		if (rc != OPAL_SUCCESS) {
			pr_warn("%s: Failure %lld clear %d on PHB#%x-PE#%x\n",
				__func__, rc, opt, phb->hose->global_number,
				slave->pe_number);
			return -EIO;
		}
	}

	return 0;
}

static int pnv_ioda_get_pe_state(struct pnv_phb *phb, int pe_no)
{
	struct pnv_ioda_pe *slave, *pe;
	u8 fstate = 0, state;
	__be16 pcierr = 0;
	s64 rc;

	/* Sanity check on PE number */
	if (pe_no < 0 || pe_no >= phb->ioda.total_pe_num)
		return OPAL_EEH_STOPPED_PERM_UNAVAIL;

	/*
	 * Fetch the master PE and the PE instance might be
	 * not initialized yet.
	 */
	pe = &phb->ioda.pe_array[pe_no];
	if (pe->flags & PNV_IODA_PE_SLAVE) {
		pe = pe->master;
		WARN_ON(!pe || !(pe->flags & PNV_IODA_PE_MASTER));
		pe_no = pe->pe_number;
	}

	/* Check the master PE */
	rc = opal_pci_eeh_freeze_status(phb->opal_id, pe_no,
					&state, &pcierr, NULL);
	if (rc != OPAL_SUCCESS) {
		pr_warn("%s: Failure %lld getting "
			"PHB#%x-PE#%x state\n",
			__func__, rc,
			phb->hose->global_number, pe_no);
		return OPAL_EEH_STOPPED_TEMP_UNAVAIL;
	}

	/* Check the slave PE */
	if (!(pe->flags & PNV_IODA_PE_MASTER))
		return state;

	list_for_each_entry(slave, &pe->slaves, list) {
		rc = opal_pci_eeh_freeze_status(phb->opal_id,
						slave->pe_number,
						&fstate,
						&pcierr,
						NULL);
		if (rc != OPAL_SUCCESS) {
			pr_warn("%s: Failure %lld getting "
				"PHB#%x-PE#%x state\n",
				__func__, rc,
				phb->hose->global_number, slave->pe_number);
			return OPAL_EEH_STOPPED_TEMP_UNAVAIL;
		}

		/*
		 * Override the result based on the ascending
		 * priority.
		 */
		if (fstate > state)
			state = fstate;
	}

	return state;
}

struct pnv_ioda_pe *pnv_pci_bdfn_to_pe(struct pnv_phb *phb, u16 bdfn)
{
	int pe_number = phb->ioda.pe_rmap[bdfn];

	if (pe_number == IODA_INVALID_PE)
		return NULL;

	return &phb->ioda.pe_array[pe_number];
}

struct pnv_ioda_pe *pnv_ioda_get_pe(struct pci_dev *dev)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(dev->bus);
	struct pci_dn *pdn = pci_get_pdn(dev);

	if (!pdn)
		return NULL;
	if (pdn->pe_number == IODA_INVALID_PE)
		return NULL;
	return &phb->ioda.pe_array[pdn->pe_number];
}

static int pnv_ioda_set_one_peltv(struct pnv_phb *phb,
				  struct pnv_ioda_pe *parent,
				  struct pnv_ioda_pe *child,
				  bool is_add)
{
	const char *desc = is_add ? "adding" : "removing";
	uint8_t op = is_add ? OPAL_ADD_PE_TO_DOMAIN :
			      OPAL_REMOVE_PE_FROM_DOMAIN;
	struct pnv_ioda_pe *slave;
	long rc;

	/* Parent PE affects child PE */
	rc = opal_pci_set_peltv(phb->opal_id, parent->pe_number,
				child->pe_number, op);
	if (rc != OPAL_SUCCESS) {
		pe_warn(child, "OPAL error %ld %s to parent PELTV\n",
			rc, desc);
		return -ENXIO;
	}

	if (!(child->flags & PNV_IODA_PE_MASTER))
		return 0;

	/* Compound case: parent PE affects slave PEs */
	list_for_each_entry(slave, &child->slaves, list) {
		rc = opal_pci_set_peltv(phb->opal_id, parent->pe_number,
					slave->pe_number, op);
		if (rc != OPAL_SUCCESS) {
			pe_warn(slave, "OPAL error %ld %s to parent PELTV\n",
				rc, desc);
			return -ENXIO;
		}
	}

	return 0;
}

static int pnv_ioda_set_peltv(struct pnv_phb *phb,
			      struct pnv_ioda_pe *pe,
			      bool is_add)
{
	struct pnv_ioda_pe *slave;
	struct pci_dev *pdev = NULL;
	int ret;

	/*
	 * Clear PE frozen state. If it's master PE, we need
	 * clear slave PE frozen state as well.
	 */
	if (is_add) {
		opal_pci_eeh_freeze_clear(phb->opal_id, pe->pe_number,
					  OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);
		if (pe->flags & PNV_IODA_PE_MASTER) {
			list_for_each_entry(slave, &pe->slaves, list)
				opal_pci_eeh_freeze_clear(phb->opal_id,
							  slave->pe_number,
							  OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);
		}
	}

	/*
	 * Associate PE in PELT. We need add the PE into the
	 * corresponding PELT-V as well. Otherwise, the error
	 * originated from the PE might contribute to other
	 * PEs.
	 */
	ret = pnv_ioda_set_one_peltv(phb, pe, pe, is_add);
	if (ret)
		return ret;

	/* For compound PEs, any one affects all of them */
	if (pe->flags & PNV_IODA_PE_MASTER) {
		list_for_each_entry(slave, &pe->slaves, list) {
			ret = pnv_ioda_set_one_peltv(phb, slave, pe, is_add);
			if (ret)
				return ret;
		}
	}

	if (pe->flags & (PNV_IODA_PE_BUS_ALL | PNV_IODA_PE_BUS))
		pdev = pe->pbus->self;
	else if (pe->flags & PNV_IODA_PE_DEV)
		pdev = pe->pdev->bus->self;
#ifdef CONFIG_PCI_IOV
	else if (pe->flags & PNV_IODA_PE_VF)
		pdev = pe->parent_dev;
#endif /* CONFIG_PCI_IOV */
	while (pdev) {
		struct pci_dn *pdn = pci_get_pdn(pdev);
		struct pnv_ioda_pe *parent;

		if (pdn && pdn->pe_number != IODA_INVALID_PE) {
			parent = &phb->ioda.pe_array[pdn->pe_number];
			ret = pnv_ioda_set_one_peltv(phb, parent, pe, is_add);
			if (ret)
				return ret;
		}

		pdev = pdev->bus->self;
	}

	return 0;
}

static void pnv_ioda_unset_peltv(struct pnv_phb *phb,
				 struct pnv_ioda_pe *pe,
				 struct pci_dev *parent)
{
	int64_t rc;

	while (parent) {
		struct pci_dn *pdn = pci_get_pdn(parent);

		if (pdn && pdn->pe_number != IODA_INVALID_PE) {
			rc = opal_pci_set_peltv(phb->opal_id, pdn->pe_number,
						pe->pe_number,
						OPAL_REMOVE_PE_FROM_DOMAIN);
			/* XXX What to do in case of error ? */
		}
		parent = parent->bus->self;
	}

	opal_pci_eeh_freeze_clear(phb->opal_id, pe->pe_number,
				  OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);

	/* Disassociate PE in PELT */
	rc = opal_pci_set_peltv(phb->opal_id, pe->pe_number,
				pe->pe_number, OPAL_REMOVE_PE_FROM_DOMAIN);
	if (rc)
		pe_warn(pe, "OPAL error %lld remove self from PELTV\n", rc);
}

int pnv_ioda_deconfigure_pe(struct pnv_phb *phb, struct pnv_ioda_pe *pe)
{
	struct pci_dev *parent;
	uint8_t bcomp, dcomp, fcomp;
	int64_t rc;
	long rid_end, rid;

	/* Currently, we just deconfigure VF PE. Bus PE will always there.*/
	if (pe->pbus) {
		int count;

		dcomp = OPAL_IGNORE_RID_DEVICE_NUMBER;
		fcomp = OPAL_IGNORE_RID_FUNCTION_NUMBER;
		parent = pe->pbus->self;
		if (pe->flags & PNV_IODA_PE_BUS_ALL)
			count = resource_size(&pe->pbus->busn_res);
		else
			count = 1;

		switch(count) {
		case  1: bcomp = OpalPciBusAll;         break;
		case  2: bcomp = OpalPciBus7Bits;       break;
		case  4: bcomp = OpalPciBus6Bits;       break;
		case  8: bcomp = OpalPciBus5Bits;       break;
		case 16: bcomp = OpalPciBus4Bits;       break;
		case 32: bcomp = OpalPciBus3Bits;       break;
		default:
			dev_err(&pe->pbus->dev, "Number of subordinate buses %d unsupported\n",
			        count);
			/* Do an exact match only */
			bcomp = OpalPciBusAll;
		}
		rid_end = pe->rid + (count << 8);
	} else {
#ifdef CONFIG_PCI_IOV
		if (pe->flags & PNV_IODA_PE_VF)
			parent = pe->parent_dev;
		else
#endif
			parent = pe->pdev->bus->self;
		bcomp = OpalPciBusAll;
		dcomp = OPAL_COMPARE_RID_DEVICE_NUMBER;
		fcomp = OPAL_COMPARE_RID_FUNCTION_NUMBER;
		rid_end = pe->rid + 1;
	}

	/* Clear the reverse map */
	for (rid = pe->rid; rid < rid_end; rid++)
		phb->ioda.pe_rmap[rid] = IODA_INVALID_PE;

	/*
	 * Release from all parents PELT-V. NPUs don't have a PELTV
	 * table
	 */
	if (phb->type != PNV_PHB_NPU_OCAPI)
		pnv_ioda_unset_peltv(phb, pe, parent);

	rc = opal_pci_set_pe(phb->opal_id, pe->pe_number, pe->rid,
			     bcomp, dcomp, fcomp, OPAL_UNMAP_PE);
	if (rc)
		pe_err(pe, "OPAL error %lld trying to setup PELT table\n", rc);

	pe->pbus = NULL;
	pe->pdev = NULL;
#ifdef CONFIG_PCI_IOV
	pe->parent_dev = NULL;
#endif

	return 0;
}

int pnv_ioda_configure_pe(struct pnv_phb *phb, struct pnv_ioda_pe *pe)
{
	uint8_t bcomp, dcomp, fcomp;
	long rc, rid_end, rid;

	/* Bus validation ? */
	if (pe->pbus) {
		int count;

		dcomp = OPAL_IGNORE_RID_DEVICE_NUMBER;
		fcomp = OPAL_IGNORE_RID_FUNCTION_NUMBER;
		if (pe->flags & PNV_IODA_PE_BUS_ALL)
			count = resource_size(&pe->pbus->busn_res);
		else
			count = 1;

		switch(count) {
		case  1: bcomp = OpalPciBusAll;		break;
		case  2: bcomp = OpalPciBus7Bits;	break;
		case  4: bcomp = OpalPciBus6Bits;	break;
		case  8: bcomp = OpalPciBus5Bits;	break;
		case 16: bcomp = OpalPciBus4Bits;	break;
		case 32: bcomp = OpalPciBus3Bits;	break;
		default:
			dev_err(&pe->pbus->dev, "Number of subordinate buses %d unsupported\n",
			        count);
			/* Do an exact match only */
			bcomp = OpalPciBusAll;
		}
		rid_end = pe->rid + (count << 8);
	} else {
		bcomp = OpalPciBusAll;
		dcomp = OPAL_COMPARE_RID_DEVICE_NUMBER;
		fcomp = OPAL_COMPARE_RID_FUNCTION_NUMBER;
		rid_end = pe->rid + 1;
	}

	/*
	 * Associate PE in PELT. We need add the PE into the
	 * corresponding PELT-V as well. Otherwise, the error
	 * originated from the PE might contribute to other
	 * PEs.
	 */
	rc = opal_pci_set_pe(phb->opal_id, pe->pe_number, pe->rid,
			     bcomp, dcomp, fcomp, OPAL_MAP_PE);
	if (rc) {
		pe_err(pe, "OPAL error %ld trying to setup PELT table\n", rc);
		return -ENXIO;
	}

	/*
	 * Configure PELTV. NPUs don't have a PELTV table so skip
	 * configuration on them.
	 */
	if (phb->type != PNV_PHB_NPU_OCAPI)
		pnv_ioda_set_peltv(phb, pe, true);

	/* Setup reverse map */
	for (rid = pe->rid; rid < rid_end; rid++)
		phb->ioda.pe_rmap[rid] = pe->pe_number;

	pe->mve_number = 0;

	return 0;
}

static struct pnv_ioda_pe *pnv_ioda_setup_dev_PE(struct pci_dev *dev)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(dev->bus);
	struct pci_dn *pdn = pci_get_pdn(dev);
	struct pnv_ioda_pe *pe;

	if (!pdn) {
		pr_err("%s: Device tree node not associated properly\n",
			   pci_name(dev));
		return NULL;
	}
	if (pdn->pe_number != IODA_INVALID_PE)
		return NULL;

	pe = pnv_ioda_alloc_pe(phb, 1);
	if (!pe) {
		pr_warn("%s: Not enough PE# available, disabling device\n",
			pci_name(dev));
		return NULL;
	}

	/* NOTE: We don't get a reference for the pointer in the PE
	 * data structure, both the device and PE structures should be
	 * destroyed at the same time.
	 *
	 * At some point we want to remove the PDN completely anyways
	 */
	pdn->pe_number = pe->pe_number;
	pe->flags = PNV_IODA_PE_DEV;
	pe->pdev = dev;
	pe->pbus = NULL;
	pe->mve_number = -1;
	pe->rid = dev->bus->number << 8 | pdn->devfn;
	pe->device_count++;

	pe_info(pe, "Associated device to PE\n");

	if (pnv_ioda_configure_pe(phb, pe)) {
		/* XXX What do we do here ? */
		pnv_ioda_free_pe(pe);
		pdn->pe_number = IODA_INVALID_PE;
		pe->pdev = NULL;
		return NULL;
	}

	/* Put PE to the list */
	mutex_lock(&phb->ioda.pe_list_mutex);
	list_add_tail(&pe->list, &phb->ioda.pe_list);
	mutex_unlock(&phb->ioda.pe_list_mutex);
	return pe;
}

/*
 * There're 2 types of PCI bus sensitive PEs: One that is compromised of
 * single PCI bus. Another one that contains the primary PCI bus and its
 * subordinate PCI devices and buses. The second type of PE is normally
 * orgiriated by PCIe-to-PCI bridge or PLX switch downstream ports.
 */
static struct pnv_ioda_pe *pnv_ioda_setup_bus_PE(struct pci_bus *bus, bool all)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(bus);
	struct pnv_ioda_pe *pe = NULL;
	unsigned int pe_num;

	/*
	 * In partial hotplug case, the PE instance might be still alive.
	 * We should reuse it instead of allocating a new one.
	 */
	pe_num = phb->ioda.pe_rmap[bus->number << 8];
	if (WARN_ON(pe_num != IODA_INVALID_PE)) {
		pe = &phb->ioda.pe_array[pe_num];
		return NULL;
	}

	/* PE number for root bus should have been reserved */
	if (pci_is_root_bus(bus))
		pe = &phb->ioda.pe_array[phb->ioda.root_pe_idx];

	/* Check if PE is determined by M64 */
	if (!pe)
		pe = pnv_ioda_pick_m64_pe(bus, all);

	/* The PE number isn't pinned by M64 */
	if (!pe)
		pe = pnv_ioda_alloc_pe(phb, 1);

	if (!pe) {
		pr_warn("%s: Not enough PE# available for PCI bus %04x:%02x\n",
			__func__, pci_domain_nr(bus), bus->number);
		return NULL;
	}

	pe->flags |= (all ? PNV_IODA_PE_BUS_ALL : PNV_IODA_PE_BUS);
	pe->pbus = bus;
	pe->pdev = NULL;
	pe->mve_number = -1;
	pe->rid = bus->busn_res.start << 8;

	if (all)
		pe_info(pe, "Secondary bus %pad..%pad associated with PE#%x\n",
			&bus->busn_res.start, &bus->busn_res.end,
			pe->pe_number);
	else
		pe_info(pe, "Secondary bus %pad associated with PE#%x\n",
			&bus->busn_res.start, pe->pe_number);

	if (pnv_ioda_configure_pe(phb, pe)) {
		/* XXX What do we do here ? */
		pnv_ioda_free_pe(pe);
		pe->pbus = NULL;
		return NULL;
	}

	/* Put PE to the list */
	list_add_tail(&pe->list, &phb->ioda.pe_list);

	return pe;
}

static void pnv_pci_ioda_dma_dev_setup(struct pci_dev *pdev)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(pdev->bus);
	struct pci_dn *pdn = pci_get_pdn(pdev);
	struct pnv_ioda_pe *pe;

	/* Check if the BDFN for this device is associated with a PE yet */
	pe = pnv_pci_bdfn_to_pe(phb, pdev->devfn | (pdev->bus->number << 8));
	if (!pe) {
		/* VF PEs should be pre-configured in pnv_pci_sriov_enable() */
		if (WARN_ON(pdev->is_virtfn))
			return;

		pnv_pci_configure_bus(pdev->bus);
		pe = pnv_pci_bdfn_to_pe(phb, pdev->devfn | (pdev->bus->number << 8));
		pci_info(pdev, "Configured PE#%x\n", pe ? pe->pe_number : 0xfffff);


		/*
		 * If we can't setup the IODA PE something has gone horribly
		 * wrong and we can't enable DMA for the device.
		 */
		if (WARN_ON(!pe))
			return;
	} else {
		pci_info(pdev, "Added to existing PE#%x\n", pe->pe_number);
	}

	/*
	 * We assume that bridges *probably* don't need to do any DMA so we can
	 * skip allocating a TCE table, etc unless we get a non-bridge device.
	 */
	if (!pe->dma_setup_done && !pci_is_bridge(pdev)) {
		switch (phb->type) {
		case PNV_PHB_IODA2:
			pnv_pci_ioda2_setup_dma_pe(phb, pe);
			break;
		default:
			pr_warn("%s: No DMA for PHB#%x (type %d)\n",
				__func__, phb->hose->global_number, phb->type);
		}
	}

	if (pdn)
		pdn->pe_number = pe->pe_number;
	pe->device_count++;

	WARN_ON(get_dma_ops(&pdev->dev) != &dma_iommu_ops);
	pdev->dev.archdata.dma_offset = pe->tce_bypass_base;
	set_iommu_table_base(&pdev->dev, pe->table_group.tables[0]);

	/* PEs with a DMA weight of zero won't have a group */
	if (pe->table_group.group)
		iommu_add_device(&pe->table_group, &pdev->dev);
}

/*
 * Reconfigure TVE#0 to be usable as 64-bit DMA space.
 *
 * The first 4GB of virtual memory for a PE is reserved for 32-bit accesses.
 * Devices can only access more than that if bit 59 of the PCI address is set
 * by hardware, which indicates TVE#1 should be used instead of TVE#0.
 * Many PCI devices are not capable of addressing that many bits, and as a
 * result are limited to the 4GB of virtual memory made available to 32-bit
 * devices in TVE#0.
 *
 * In order to work around this, reconfigure TVE#0 to be suitable for 64-bit
 * devices by configuring the virtual memory past the first 4GB inaccessible
 * by 64-bit DMAs.  This should only be used by devices that want more than
 * 4GB, and only on PEs that have no 32-bit devices.
 *
 * Currently this will only work on PHB3 (POWER8).
 */
static int pnv_pci_ioda_dma_64bit_bypass(struct pnv_ioda_pe *pe)
{
	u64 window_size, table_size, tce_count, addr;
	struct page *table_pages;
	u64 tce_order = 28; /* 256MB TCEs */
	__be64 *tces;
	s64 rc;

	/*
	 * Window size needs to be a power of two, but needs to account for
	 * shifting memory by the 4GB offset required to skip 32bit space.
	 */
	window_size = roundup_pow_of_two(memory_hotplug_max() + (1ULL << 32));
	tce_count = window_size >> tce_order;
	table_size = tce_count << 3;

	if (table_size < PAGE_SIZE)
		table_size = PAGE_SIZE;

	table_pages = alloc_pages_node(pe->phb->hose->node, GFP_KERNEL,
				       get_order(table_size));
	if (!table_pages)
		goto err;

	tces = page_address(table_pages);
	if (!tces)
		goto err;

	memset(tces, 0, table_size);

	for (addr = 0; addr < memory_hotplug_max(); addr += (1 << tce_order)) {
		tces[(addr + (1ULL << 32)) >> tce_order] =
			cpu_to_be64(addr | TCE_PCI_READ | TCE_PCI_WRITE);
	}

	rc = opal_pci_map_pe_dma_window(pe->phb->opal_id,
					pe->pe_number,
					/* reconfigure window 0 */
					(pe->pe_number << 1) + 0,
					1,
					__pa(tces),
					table_size,
					1 << tce_order);
	if (rc == OPAL_SUCCESS) {
		pe_info(pe, "Using 64-bit DMA iommu bypass (through TVE#0)\n");
		return 0;
	}
err:
	pe_err(pe, "Error configuring 64-bit DMA bypass\n");
	return -EIO;
}

static bool pnv_pci_ioda_iommu_bypass_supported(struct pci_dev *pdev,
		u64 dma_mask)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(pdev->bus);
	struct pci_dn *pdn = pci_get_pdn(pdev);
	struct pnv_ioda_pe *pe;

	if (WARN_ON(!pdn || pdn->pe_number == IODA_INVALID_PE))
		return false;

	pe = &phb->ioda.pe_array[pdn->pe_number];
	if (pe->tce_bypass_enabled) {
		u64 top = pe->tce_bypass_base + memblock_end_of_DRAM() - 1;
		if (dma_mask >= top)
			return true;
	}

	/*
	 * If the device can't set the TCE bypass bit but still wants
	 * to access 4GB or more, on PHB3 we can reconfigure TVE#0 to
	 * bypass the 32-bit region and be usable for 64-bit DMAs.
	 * The device needs to be able to address all of this space.
	 */
	if (dma_mask >> 32 &&
	    dma_mask > (memory_hotplug_max() + (1ULL << 32)) &&
	    /* pe->pdev should be set if it's a single device, pe->pbus if not */
	    (pe->device_count == 1 || !pe->pbus) &&
	    phb->model == PNV_PHB_MODEL_PHB3) {
		/* Configure the bypass mode */
		s64 rc = pnv_pci_ioda_dma_64bit_bypass(pe);
		if (rc)
			return false;
		/* 4GB offset bypasses 32-bit space */
		pdev->dev.archdata.dma_offset = (1ULL << 32);
		return true;
	}

	return false;
}

static inline __be64 __iomem *pnv_ioda_get_inval_reg(struct pnv_phb *phb)
{
	return phb->regs + 0x210;
}

#ifdef CONFIG_IOMMU_API
/* Common for IODA1 and IODA2 */
static int pnv_ioda_tce_xchg_no_kill(struct iommu_table *tbl, long index,
		unsigned long *hpa, enum dma_data_direction *direction)
{
	return pnv_tce_xchg(tbl, index, hpa, direction);
}
#endif

#define PHB3_TCE_KILL_INVAL_ALL		PPC_BIT(0)
#define PHB3_TCE_KILL_INVAL_PE		PPC_BIT(1)
#define PHB3_TCE_KILL_INVAL_ONE		PPC_BIT(2)

static inline void pnv_pci_phb3_tce_invalidate_pe(struct pnv_ioda_pe *pe)
{
	/* 01xb - invalidate TCEs that match the specified PE# */
	__be64 __iomem *invalidate = pnv_ioda_get_inval_reg(pe->phb);
	unsigned long val = PHB3_TCE_KILL_INVAL_PE | (pe->pe_number & 0xFF);

	mb(); /* Ensure above stores are visible */
	__raw_writeq_be(val, invalidate);
}

static void pnv_pci_phb3_tce_invalidate(struct pnv_ioda_pe *pe,
					unsigned shift, unsigned long index,
					unsigned long npages)
{
	__be64 __iomem *invalidate = pnv_ioda_get_inval_reg(pe->phb);
	unsigned long start, end, inc;

	/* We'll invalidate DMA address in PE scope */
	start = PHB3_TCE_KILL_INVAL_ONE;
	start |= (pe->pe_number & 0xFF);
	end = start;

	/* Figure out the start, end and step */
	start |= (index << shift);
	end |= ((index + npages - 1) << shift);
	inc = (0x1ull << shift);
	mb();

	while (start <= end) {
		__raw_writeq_be(start, invalidate);
		start += inc;
	}
}

static inline void pnv_pci_ioda2_tce_invalidate_pe(struct pnv_ioda_pe *pe)
{
	struct pnv_phb *phb = pe->phb;

	if (phb->model == PNV_PHB_MODEL_PHB3 && phb->regs)
		pnv_pci_phb3_tce_invalidate_pe(pe);
	else
		opal_pci_tce_kill(phb->opal_id, OPAL_PCI_TCE_KILL_PE,
				  pe->pe_number, 0, 0, 0);
}

static void pnv_pci_ioda2_tce_invalidate(struct iommu_table *tbl,
		unsigned long index, unsigned long npages)
{
	struct iommu_table_group_link *tgl;

	list_for_each_entry_lockless(tgl, &tbl->it_group_list, next) {
		struct pnv_ioda_pe *pe = container_of(tgl->table_group,
				struct pnv_ioda_pe, table_group);
		struct pnv_phb *phb = pe->phb;
		unsigned int shift = tbl->it_page_shift;

		if (phb->model == PNV_PHB_MODEL_PHB3 && phb->regs)
			pnv_pci_phb3_tce_invalidate(pe, shift,
						    index, npages);
		else
			opal_pci_tce_kill(phb->opal_id,
					  OPAL_PCI_TCE_KILL_PAGES,
					  pe->pe_number, 1u << shift,
					  index << shift, npages);
	}
}

static int pnv_ioda2_tce_build(struct iommu_table *tbl, long index,
		long npages, unsigned long uaddr,
		enum dma_data_direction direction,
		unsigned long attrs)
{
	int ret = pnv_tce_build(tbl, index, npages, uaddr, direction,
			attrs);

	if (!ret)
		pnv_pci_ioda2_tce_invalidate(tbl, index, npages);

	return ret;
}

static void pnv_ioda2_tce_free(struct iommu_table *tbl, long index,
		long npages)
{
	pnv_tce_free(tbl, index, npages);

	pnv_pci_ioda2_tce_invalidate(tbl, index, npages);
}

static struct iommu_table_ops pnv_ioda2_iommu_ops = {
	.set = pnv_ioda2_tce_build,
#ifdef CONFIG_IOMMU_API
	.xchg_no_kill = pnv_ioda_tce_xchg_no_kill,
	.tce_kill = pnv_pci_ioda2_tce_invalidate,
	.useraddrptr = pnv_tce_useraddrptr,
#endif
	.clear = pnv_ioda2_tce_free,
	.get = pnv_tce_get,
	.free = pnv_pci_ioda2_table_free_pages,
};

static long pnv_pci_ioda2_set_window(struct iommu_table_group *table_group,
		int num, struct iommu_table *tbl)
{
	struct pnv_ioda_pe *pe = container_of(table_group, struct pnv_ioda_pe,
			table_group);
	struct pnv_phb *phb = pe->phb;
	int64_t rc;
	const unsigned long size = tbl->it_indirect_levels ?
			tbl->it_level_size : tbl->it_size;
	const __u64 start_addr = tbl->it_offset << tbl->it_page_shift;
	const __u64 win_size = tbl->it_size << tbl->it_page_shift;

	pe_info(pe, "Setting up window#%d %llx..%llx pg=%lx\n",
		num, start_addr, start_addr + win_size - 1,
		IOMMU_PAGE_SIZE(tbl));

	/*
	 * Map TCE table through TVT. The TVE index is the PE number
	 * shifted by 1 bit for 32-bits DMA space.
	 */
	rc = opal_pci_map_pe_dma_window(phb->opal_id,
			pe->pe_number,
			(pe->pe_number << 1) + num,
			tbl->it_indirect_levels + 1,
			__pa(tbl->it_base),
			size << 3,
			IOMMU_PAGE_SIZE(tbl));
	if (rc) {
		pe_err(pe, "Failed to configure TCE table, err %lld\n", rc);
		return rc;
	}

	pnv_pci_link_table_and_group(phb->hose->node, num,
			tbl, &pe->table_group);
	pnv_pci_ioda2_tce_invalidate_pe(pe);

	return 0;
}

static void pnv_pci_ioda2_set_bypass(struct pnv_ioda_pe *pe, bool enable)
{
	uint16_t window_id = (pe->pe_number << 1 ) + 1;
	int64_t rc;

	pe_info(pe, "%sabling 64-bit DMA bypass\n", enable ? "En" : "Dis");
	if (enable) {
		phys_addr_t top = memblock_end_of_DRAM();

		top = roundup_pow_of_two(top);
		rc = opal_pci_map_pe_dma_window_real(pe->phb->opal_id,
						     pe->pe_number,
						     window_id,
						     pe->tce_bypass_base,
						     top);
	} else {
		rc = opal_pci_map_pe_dma_window_real(pe->phb->opal_id,
						     pe->pe_number,
						     window_id,
						     pe->tce_bypass_base,
						     0);
	}
	if (rc)
		pe_err(pe, "OPAL error %lld configuring bypass window\n", rc);
	else
		pe->tce_bypass_enabled = enable;
}

static long pnv_pci_ioda2_create_table(struct iommu_table_group *table_group,
		int num, __u32 page_shift, __u64 window_size, __u32 levels,
		bool alloc_userspace_copy, struct iommu_table **ptbl)
{
	struct pnv_ioda_pe *pe = container_of(table_group, struct pnv_ioda_pe,
			table_group);
	int nid = pe->phb->hose->node;
	__u64 bus_offset = num ? pe->tce_bypass_base : table_group->tce32_start;
	long ret;
	struct iommu_table *tbl;

	tbl = pnv_pci_table_alloc(nid);
	if (!tbl)
		return -ENOMEM;

	tbl->it_ops = &pnv_ioda2_iommu_ops;

	ret = pnv_pci_ioda2_table_alloc_pages(nid,
			bus_offset, page_shift, window_size,
			levels, alloc_userspace_copy, tbl);
	if (ret) {
		iommu_tce_table_put(tbl);
		return ret;
	}

	*ptbl = tbl;

	return 0;
}

static long pnv_pci_ioda2_setup_default_config(struct pnv_ioda_pe *pe)
{
	struct iommu_table *tbl = NULL;
	long rc;
	unsigned long res_start, res_end;

	/*
	 * crashkernel= specifies the kdump kernel's maximum memory at
	 * some offset and there is no guaranteed the result is a power
	 * of 2, which will cause errors later.
	 */
	const u64 max_memory = __rounddown_pow_of_two(memory_hotplug_max());

	/*
	 * In memory constrained environments, e.g. kdump kernel, the
	 * DMA window can be larger than available memory, which will
	 * cause errors later.
	 */
	const u64 maxblock = 1UL << (PAGE_SHIFT + MAX_ORDER);

	/*
	 * We create the default window as big as we can. The constraint is
	 * the max order of allocation possible. The TCE table is likely to
	 * end up being multilevel and with on-demand allocation in place,
	 * the initial use is not going to be huge as the default window aims
	 * to support crippled devices (i.e. not fully 64bit DMAble) only.
	 */
	/* iommu_table::it_map uses 1 bit per IOMMU page, hence 8 */
	const u64 window_size = min((maxblock * 8) << PAGE_SHIFT, max_memory);
	/* Each TCE level cannot exceed maxblock so go multilevel if needed */
	unsigned long tces_order = ilog2(window_size >> PAGE_SHIFT);
	unsigned long tcelevel_order = ilog2(maxblock >> 3);
	unsigned int levels = tces_order / tcelevel_order;

	if (tces_order % tcelevel_order)
		levels += 1;
	/*
	 * We try to stick to default levels (which is >1 at the moment) in
	 * order to save memory by relying on on-demain TCE level allocation.
	 */
	levels = max_t(unsigned int, levels, POWERNV_IOMMU_DEFAULT_LEVELS);

	rc = pnv_pci_ioda2_create_table(&pe->table_group, 0, PAGE_SHIFT,
			window_size, levels, false, &tbl);
	if (rc) {
		pe_err(pe, "Failed to create 32-bit TCE table, err %ld",
				rc);
		return rc;
	}

	/* We use top part of 32bit space for MMIO so exclude it from DMA */
	res_start = 0;
	res_end = 0;
	if (window_size > pe->phb->ioda.m32_pci_base) {
		res_start = pe->phb->ioda.m32_pci_base >> tbl->it_page_shift;
		res_end = min(window_size, SZ_4G) >> tbl->it_page_shift;
	}

	tbl->it_index = (pe->phb->hose->global_number << 16) | pe->pe_number;
	if (iommu_init_table(tbl, pe->phb->hose->node, res_start, res_end))
		rc = pnv_pci_ioda2_set_window(&pe->table_group, 0, tbl);
	else
		rc = -ENOMEM;
	if (rc) {
		pe_err(pe, "Failed to configure 32-bit TCE table, err %ld\n", rc);
		iommu_tce_table_put(tbl);
		tbl = NULL; /* This clears iommu_table_base below */
	}
	if (!pnv_iommu_bypass_disabled)
		pnv_pci_ioda2_set_bypass(pe, true);

	/*
	 * Set table base for the case of IOMMU DMA use. Usually this is done
	 * from dma_dev_setup() which is not called when a device is returned
	 * from VFIO so do it here.
	 */
	if (pe->pdev)
		set_iommu_table_base(&pe->pdev->dev, tbl);

	return 0;
}

static long pnv_pci_ioda2_unset_window(struct iommu_table_group *table_group,
		int num)
{
	struct pnv_ioda_pe *pe = container_of(table_group, struct pnv_ioda_pe,
			table_group);
	struct pnv_phb *phb = pe->phb;
	long ret;

	pe_info(pe, "Removing DMA window #%d\n", num);

	ret = opal_pci_map_pe_dma_window(phb->opal_id, pe->pe_number,
			(pe->pe_number << 1) + num,
			0/* levels */, 0/* table address */,
			0/* table size */, 0/* page size */);
	if (ret)
		pe_warn(pe, "Unmapping failed, ret = %ld\n", ret);
	else
		pnv_pci_ioda2_tce_invalidate_pe(pe);

	pnv_pci_unlink_table_and_group(table_group->tables[num], table_group);

	return ret;
}

#ifdef CONFIG_IOMMU_API
unsigned long pnv_pci_ioda2_get_table_size(__u32 page_shift,
		__u64 window_size, __u32 levels)
{
	unsigned long bytes = 0;
	const unsigned window_shift = ilog2(window_size);
	unsigned entries_shift = window_shift - page_shift;
	unsigned table_shift = entries_shift + 3;
	unsigned long tce_table_size = max(0x1000UL, 1UL << table_shift);
	unsigned long direct_table_size;

	if (!levels || (levels > POWERNV_IOMMU_MAX_LEVELS) ||
			!is_power_of_2(window_size))
		return 0;

	/* Calculate a direct table size from window_size and levels */
	entries_shift = (entries_shift + levels - 1) / levels;
	table_shift = entries_shift + 3;
	table_shift = max_t(unsigned, table_shift, PAGE_SHIFT);
	direct_table_size =  1UL << table_shift;

	for ( ; levels; --levels) {
		bytes += ALIGN(tce_table_size, direct_table_size);

		tce_table_size /= direct_table_size;
		tce_table_size <<= 3;
		tce_table_size = max_t(unsigned long,
				tce_table_size, direct_table_size);
	}

	return bytes + bytes; /* one for HW table, one for userspace copy */
}

static long pnv_pci_ioda2_create_table_userspace(
		struct iommu_table_group *table_group,
		int num, __u32 page_shift, __u64 window_size, __u32 levels,
		struct iommu_table **ptbl)
{
	long ret = pnv_pci_ioda2_create_table(table_group,
			num, page_shift, window_size, levels, true, ptbl);

	if (!ret)
		(*ptbl)->it_allocated_size = pnv_pci_ioda2_get_table_size(
				page_shift, window_size, levels);
	return ret;
}

static void pnv_ioda_setup_bus_dma(struct pnv_ioda_pe *pe, struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		set_iommu_table_base(&dev->dev, pe->table_group.tables[0]);
		dev->dev.archdata.dma_offset = pe->tce_bypass_base;

		if ((pe->flags & PNV_IODA_PE_BUS_ALL) && dev->subordinate)
			pnv_ioda_setup_bus_dma(pe, dev->subordinate);
	}
}

static long pnv_ioda2_take_ownership(struct iommu_table_group *table_group)
{
	struct pnv_ioda_pe *pe = container_of(table_group, struct pnv_ioda_pe,
						table_group);
	/* Store @tbl as pnv_pci_ioda2_unset_window() resets it */
	struct iommu_table *tbl = pe->table_group.tables[0];

	/*
	 * iommu_ops transfers the ownership per a device and we mode
	 * the group ownership with the first device in the group.
	 */
	if (!tbl)
		return 0;

	pnv_pci_ioda2_set_bypass(pe, false);
	pnv_pci_ioda2_unset_window(&pe->table_group, 0);
	if (pe->pbus)
		pnv_ioda_setup_bus_dma(pe, pe->pbus);
	else if (pe->pdev)
		set_iommu_table_base(&pe->pdev->dev, NULL);
	iommu_tce_table_put(tbl);

	return 0;
}

static void pnv_ioda2_release_ownership(struct iommu_table_group *table_group)
{
	struct pnv_ioda_pe *pe = container_of(table_group, struct pnv_ioda_pe,
						table_group);

	/* See the comment about iommu_ops above */
	if (pe->table_group.tables[0])
		return;
	pnv_pci_ioda2_setup_default_config(pe);
	if (pe->pbus)
		pnv_ioda_setup_bus_dma(pe, pe->pbus);
}

static struct iommu_table_group_ops pnv_pci_ioda2_ops = {
	.get_table_size = pnv_pci_ioda2_get_table_size,
	.create_table = pnv_pci_ioda2_create_table_userspace,
	.set_window = pnv_pci_ioda2_set_window,
	.unset_window = pnv_pci_ioda2_unset_window,
	.take_ownership = pnv_ioda2_take_ownership,
	.release_ownership = pnv_ioda2_release_ownership,
};
#endif

void pnv_pci_ioda2_setup_dma_pe(struct pnv_phb *phb,
				struct pnv_ioda_pe *pe)
{
	int64_t rc;

	/* TVE #1 is selected by PCI address bit 59 */
	pe->tce_bypass_base = 1ull << 59;

	/* The PE will reserve all possible 32-bits space */
	pe_info(pe, "Setting up 32-bit TCE table at 0..%08x\n",
		phb->ioda.m32_pci_base);

	/* Setup linux iommu table */
	pe->table_group.tce32_start = 0;
	pe->table_group.tce32_size = phb->ioda.m32_pci_base;
	pe->table_group.max_dynamic_windows_supported =
			IOMMU_TABLE_GROUP_MAX_TABLES;
	pe->table_group.max_levels = POWERNV_IOMMU_MAX_LEVELS;
	pe->table_group.pgsizes = pnv_ioda_parse_tce_sizes(phb);

	rc = pnv_pci_ioda2_setup_default_config(pe);
	if (rc)
		return;

#ifdef CONFIG_IOMMU_API
	pe->table_group.ops = &pnv_pci_ioda2_ops;
	iommu_register_group(&pe->table_group, phb->hose->global_number,
			     pe->pe_number);
#endif
	pe->dma_setup_done = true;
}

/*
 * Called from KVM in real mode to EOI passthru interrupts. The ICP
 * EOI is handled directly in KVM in kvmppc_deliver_irq_passthru().
 *
 * The IRQ data is mapped in the PCI-MSI domain and the EOI OPAL call
 * needs an HW IRQ number mapped in the XICS IRQ domain. The HW IRQ
 * numbers of the in-the-middle MSI domain are vector numbers and it's
 * good enough for OPAL. Use that.
 */
int64_t pnv_opal_pci_msi_eoi(struct irq_data *d)
{
	struct pci_controller *hose = irq_data_get_irq_chip_data(d->parent_data);
	struct pnv_phb *phb = hose->private_data;

	return opal_pci_msi_eoi(phb->opal_id, d->parent_data->hwirq);
}

/*
 * The IRQ data is mapped in the XICS domain, with OPAL HW IRQ numbers
 */
static void pnv_ioda2_msi_eoi(struct irq_data *d)
{
	int64_t rc;
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);
	struct pci_controller *hose = irq_data_get_irq_chip_data(d);
	struct pnv_phb *phb = hose->private_data;

	rc = opal_pci_msi_eoi(phb->opal_id, hw_irq);
	WARN_ON_ONCE(rc);

	icp_native_eoi(d);
}

/* P8/CXL only */
void pnv_set_msi_irq_chip(struct pnv_phb *phb, unsigned int virq)
{
	struct irq_data *idata;
	struct irq_chip *ichip;

	/* The MSI EOI OPAL call is only needed on PHB3 */
	if (phb->model != PNV_PHB_MODEL_PHB3)
		return;

	if (!phb->ioda.irq_chip_init) {
		/*
		 * First time we setup an MSI IRQ, we need to setup the
		 * corresponding IRQ chip to route correctly.
		 */
		idata = irq_get_irq_data(virq);
		ichip = irq_data_get_irq_chip(idata);
		phb->ioda.irq_chip_init = 1;
		phb->ioda.irq_chip = *ichip;
		phb->ioda.irq_chip.irq_eoi = pnv_ioda2_msi_eoi;
	}
	irq_set_chip(virq, &phb->ioda.irq_chip);
	irq_set_chip_data(virq, phb->hose);
}

static struct irq_chip pnv_pci_msi_irq_chip;

/*
 * Returns true iff chip is something that we could call
 * pnv_opal_pci_msi_eoi for.
 */
bool is_pnv_opal_msi(struct irq_chip *chip)
{
	return chip == &pnv_pci_msi_irq_chip;
}
EXPORT_SYMBOL_GPL(is_pnv_opal_msi);

static int __pnv_pci_ioda_msi_setup(struct pnv_phb *phb, struct pci_dev *dev,
				    unsigned int xive_num,
				    unsigned int is_64, struct msi_msg *msg)
{
	struct pnv_ioda_pe *pe = pnv_ioda_get_pe(dev);
	__be32 data;
	int rc;

	dev_dbg(&dev->dev, "%s: setup %s-bit MSI for vector #%d\n", __func__,
		is_64 ? "64" : "32", xive_num);

	/* No PE assigned ? bail out ... no MSI for you ! */
	if (pe == NULL)
		return -ENXIO;

	/* Check if we have an MVE */
	if (pe->mve_number < 0)
		return -ENXIO;

	/* Force 32-bit MSI on some broken devices */
	if (dev->no_64bit_msi)
		is_64 = 0;

	/* Assign XIVE to PE */
	rc = opal_pci_set_xive_pe(phb->opal_id, pe->pe_number, xive_num);
	if (rc) {
		pr_warn("%s: OPAL error %d setting XIVE %d PE\n",
			pci_name(dev), rc, xive_num);
		return -EIO;
	}

	if (is_64) {
		__be64 addr64;

		rc = opal_get_msi_64(phb->opal_id, pe->mve_number, xive_num, 1,
				     &addr64, &data);
		if (rc) {
			pr_warn("%s: OPAL error %d getting 64-bit MSI data\n",
				pci_name(dev), rc);
			return -EIO;
		}
		msg->address_hi = be64_to_cpu(addr64) >> 32;
		msg->address_lo = be64_to_cpu(addr64) & 0xfffffffful;
	} else {
		__be32 addr32;

		rc = opal_get_msi_32(phb->opal_id, pe->mve_number, xive_num, 1,
				     &addr32, &data);
		if (rc) {
			pr_warn("%s: OPAL error %d getting 32-bit MSI data\n",
				pci_name(dev), rc);
			return -EIO;
		}
		msg->address_hi = 0;
		msg->address_lo = be32_to_cpu(addr32);
	}
	msg->data = be32_to_cpu(data);

	return 0;
}

/*
 * The msi_free() op is called before irq_domain_free_irqs_top() when
 * the handler data is still available. Use that to clear the XIVE
 * controller.
 */
static void pnv_msi_ops_msi_free(struct irq_domain *domain,
				 struct msi_domain_info *info,
				 unsigned int irq)
{
	if (xive_enabled())
		xive_irq_free_data(irq);
}

static struct msi_domain_ops pnv_pci_msi_domain_ops = {
	.msi_free	= pnv_msi_ops_msi_free,
};

static void pnv_msi_shutdown(struct irq_data *d)
{
	d = d->parent_data;
	if (d->chip->irq_shutdown)
		d->chip->irq_shutdown(d);
}

static void pnv_msi_mask(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void pnv_msi_unmask(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static struct irq_chip pnv_pci_msi_irq_chip = {
	.name		= "PNV-PCI-MSI",
	.irq_shutdown	= pnv_msi_shutdown,
	.irq_mask	= pnv_msi_mask,
	.irq_unmask	= pnv_msi_unmask,
	.irq_eoi	= irq_chip_eoi_parent,
};

static struct msi_domain_info pnv_msi_domain_info = {
	.flags = (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_MULTI_PCI_MSI  | MSI_FLAG_PCI_MSIX),
	.ops   = &pnv_pci_msi_domain_ops,
	.chip  = &pnv_pci_msi_irq_chip,
};

static void pnv_msi_compose_msg(struct irq_data *d, struct msi_msg *msg)
{
	struct msi_desc *entry = irq_data_get_msi_desc(d);
	struct pci_dev *pdev = msi_desc_to_pci_dev(entry);
	struct pci_controller *hose = irq_data_get_irq_chip_data(d);
	struct pnv_phb *phb = hose->private_data;
	int rc;

	rc = __pnv_pci_ioda_msi_setup(phb, pdev, d->hwirq,
				      entry->pci.msi_attrib.is_64, msg);
	if (rc)
		dev_err(&pdev->dev, "Failed to setup %s-bit MSI #%ld : %d\n",
			entry->pci.msi_attrib.is_64 ? "64" : "32", d->hwirq, rc);
}

/*
 * The IRQ data is mapped in the MSI domain in which HW IRQ numbers
 * correspond to vector numbers.
 */
static void pnv_msi_eoi(struct irq_data *d)
{
	struct pci_controller *hose = irq_data_get_irq_chip_data(d);
	struct pnv_phb *phb = hose->private_data;

	if (phb->model == PNV_PHB_MODEL_PHB3) {
		/*
		 * The EOI OPAL call takes an OPAL HW IRQ number but
		 * since it is translated into a vector number in
		 * OPAL, use that directly.
		 */
		WARN_ON_ONCE(opal_pci_msi_eoi(phb->opal_id, d->hwirq));
	}

	irq_chip_eoi_parent(d);
}

static struct irq_chip pnv_msi_irq_chip = {
	.name			= "PNV-MSI",
	.irq_shutdown		= pnv_msi_shutdown,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= pnv_msi_eoi,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_compose_msi_msg	= pnv_msi_compose_msg,
};

static int pnv_irq_parent_domain_alloc(struct irq_domain *domain,
				       unsigned int virq, int hwirq)
{
	struct irq_fwspec parent_fwspec;
	int ret;

	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param_count = 2;
	parent_fwspec.param[0] = hwirq;
	parent_fwspec.param[1] = IRQ_TYPE_EDGE_RISING;

	ret = irq_domain_alloc_irqs_parent(domain, virq, 1, &parent_fwspec);
	if (ret)
		return ret;

	return 0;
}

static int pnv_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *arg)
{
	struct pci_controller *hose = domain->host_data;
	struct pnv_phb *phb = hose->private_data;
	msi_alloc_info_t *info = arg;
	struct pci_dev *pdev = msi_desc_to_pci_dev(info->desc);
	int hwirq;
	int i, ret;

	hwirq = msi_bitmap_alloc_hwirqs(&phb->msi_bmp, nr_irqs);
	if (hwirq < 0) {
		dev_warn(&pdev->dev, "failed to find a free MSI\n");
		return -ENOSPC;
	}

	dev_dbg(&pdev->dev, "%s bridge %pOF %d/%x #%d\n", __func__,
		hose->dn, virq, hwirq, nr_irqs);

	for (i = 0; i < nr_irqs; i++) {
		ret = pnv_irq_parent_domain_alloc(domain, virq + i,
						  phb->msi_base + hwirq + i);
		if (ret)
			goto out;

		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &pnv_msi_irq_chip, hose);
	}

	return 0;

out:
	irq_domain_free_irqs_parent(domain, virq, i - 1);
	msi_bitmap_free_hwirqs(&phb->msi_bmp, hwirq, nr_irqs);
	return ret;
}

static void pnv_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct pci_controller *hose = irq_data_get_irq_chip_data(d);
	struct pnv_phb *phb = hose->private_data;

	pr_debug("%s bridge %pOF %d/%lx #%d\n", __func__, hose->dn,
		 virq, d->hwirq, nr_irqs);

	msi_bitmap_free_hwirqs(&phb->msi_bmp, d->hwirq, nr_irqs);
	/* XIVE domain is cleared through ->msi_free() */
}

static const struct irq_domain_ops pnv_irq_domain_ops = {
	.alloc  = pnv_irq_domain_alloc,
	.free   = pnv_irq_domain_free,
};

static int __init pnv_msi_allocate_domains(struct pci_controller *hose, unsigned int count)
{
	struct pnv_phb *phb = hose->private_data;
	struct irq_domain *parent = irq_get_default_host();

	hose->fwnode = irq_domain_alloc_named_id_fwnode("PNV-MSI", phb->opal_id);
	if (!hose->fwnode)
		return -ENOMEM;

	hose->dev_domain = irq_domain_create_hierarchy(parent, 0, count,
						       hose->fwnode,
						       &pnv_irq_domain_ops, hose);
	if (!hose->dev_domain) {
		pr_err("PCI: failed to create IRQ domain bridge %pOF (domain %d)\n",
		       hose->dn, hose->global_number);
		irq_domain_free_fwnode(hose->fwnode);
		return -ENOMEM;
	}

	hose->msi_domain = pci_msi_create_irq_domain(of_node_to_fwnode(hose->dn),
						     &pnv_msi_domain_info,
						     hose->dev_domain);
	if (!hose->msi_domain) {
		pr_err("PCI: failed to create MSI IRQ domain bridge %pOF (domain %d)\n",
		       hose->dn, hose->global_number);
		irq_domain_free_fwnode(hose->fwnode);
		irq_domain_remove(hose->dev_domain);
		return -ENOMEM;
	}

	return 0;
}

static void __init pnv_pci_init_ioda_msis(struct pnv_phb *phb)
{
	unsigned int count;
	const __be32 *prop = of_get_property(phb->hose->dn,
					     "ibm,opal-msi-ranges", NULL);
	if (!prop) {
		/* BML Fallback */
		prop = of_get_property(phb->hose->dn, "msi-ranges", NULL);
	}
	if (!prop)
		return;

	phb->msi_base = be32_to_cpup(prop);
	count = be32_to_cpup(prop + 1);
	if (msi_bitmap_alloc(&phb->msi_bmp, count, phb->hose->dn)) {
		pr_err("PCI %d: Failed to allocate MSI bitmap !\n",
		       phb->hose->global_number);
		return;
	}

	pr_info("  Allocated bitmap for %d MSIs (base IRQ 0x%x)\n",
		count, phb->msi_base);

	pnv_msi_allocate_domains(phb->hose, count);
}

static void pnv_ioda_setup_pe_res(struct pnv_ioda_pe *pe,
				  struct resource *res)
{
	struct pnv_phb *phb = pe->phb;
	struct pci_bus_region region;
	int index;
	int64_t rc;

	if (!res || !res->flags || res->start > res->end ||
	    res->flags & IORESOURCE_UNSET)
		return;

	if (res->flags & IORESOURCE_IO) {
		region.start = res->start - phb->ioda.io_pci_base;
		region.end   = res->end - phb->ioda.io_pci_base;
		index = region.start / phb->ioda.io_segsize;

		while (index < phb->ioda.total_pe_num &&
		       region.start <= region.end) {
			phb->ioda.io_segmap[index] = pe->pe_number;
			rc = opal_pci_map_pe_mmio_window(phb->opal_id,
				pe->pe_number, OPAL_IO_WINDOW_TYPE, 0, index);
			if (rc != OPAL_SUCCESS) {
				pr_err("%s: Error %lld mapping IO segment#%d to PE#%x\n",
				       __func__, rc, index, pe->pe_number);
				break;
			}

			region.start += phb->ioda.io_segsize;
			index++;
		}
	} else if ((res->flags & IORESOURCE_MEM) &&
		   !pnv_pci_is_m64(phb, res)) {
		region.start = res->start -
			       phb->hose->mem_offset[0] -
			       phb->ioda.m32_pci_base;
		region.end   = res->end -
			       phb->hose->mem_offset[0] -
			       phb->ioda.m32_pci_base;
		index = region.start / phb->ioda.m32_segsize;

		while (index < phb->ioda.total_pe_num &&
		       region.start <= region.end) {
			phb->ioda.m32_segmap[index] = pe->pe_number;
			rc = opal_pci_map_pe_mmio_window(phb->opal_id,
				pe->pe_number, OPAL_M32_WINDOW_TYPE, 0, index);
			if (rc != OPAL_SUCCESS) {
				pr_err("%s: Error %lld mapping M32 segment#%d to PE#%x",
				       __func__, rc, index, pe->pe_number);
				break;
			}

			region.start += phb->ioda.m32_segsize;
			index++;
		}
	}
}

/*
 * This function is supposed to be called on basis of PE from top
 * to bottom style. So the I/O or MMIO segment assigned to
 * parent PE could be overridden by its child PEs if necessary.
 */
static void pnv_ioda_setup_pe_seg(struct pnv_ioda_pe *pe)
{
	struct pci_dev *pdev;
	int i;

	/*
	 * NOTE: We only care PCI bus based PE for now. For PCI
	 * device based PE, for example SRIOV sensitive VF should
	 * be figured out later.
	 */
	BUG_ON(!(pe->flags & (PNV_IODA_PE_BUS | PNV_IODA_PE_BUS_ALL)));

	list_for_each_entry(pdev, &pe->pbus->devices, bus_list) {
		for (i = 0; i <= PCI_ROM_RESOURCE; i++)
			pnv_ioda_setup_pe_res(pe, &pdev->resource[i]);

		/*
		 * If the PE contains all subordinate PCI buses, the
		 * windows of the child bridges should be mapped to
		 * the PE as well.
		 */
		if (!(pe->flags & PNV_IODA_PE_BUS_ALL) || !pci_is_bridge(pdev))
			continue;
		for (i = 0; i < PCI_BRIDGE_RESOURCE_NUM; i++)
			pnv_ioda_setup_pe_res(pe,
				&pdev->resource[PCI_BRIDGE_RESOURCES + i]);
	}
}

#ifdef CONFIG_DEBUG_FS
static int pnv_pci_diag_data_set(void *data, u64 val)
{
	struct pnv_phb *phb = data;
	s64 ret;

	/* Retrieve the diag data from firmware */
	ret = opal_pci_get_phb_diag_data2(phb->opal_id, phb->diag_data,
					  phb->diag_data_size);
	if (ret != OPAL_SUCCESS)
		return -EIO;

	/* Print the diag data to the kernel log */
	pnv_pci_dump_phb_diag_data(phb->hose, phb->diag_data);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(pnv_pci_diag_data_fops, NULL, pnv_pci_diag_data_set,
			 "%llu\n");

static int pnv_pci_ioda_pe_dump(void *data, u64 val)
{
	struct pnv_phb *phb = data;
	int pe_num;

	for (pe_num = 0; pe_num < phb->ioda.total_pe_num; pe_num++) {
		struct pnv_ioda_pe *pe = &phb->ioda.pe_array[pe_num];

		if (!test_bit(pe_num, phb->ioda.pe_alloc))
			continue;

		pe_warn(pe, "rid: %04x dev count: %2d flags: %s%s%s%s%s%s\n",
			pe->rid, pe->device_count,
			(pe->flags & PNV_IODA_PE_DEV) ? "dev " : "",
			(pe->flags & PNV_IODA_PE_BUS) ? "bus " : "",
			(pe->flags & PNV_IODA_PE_BUS_ALL) ? "all " : "",
			(pe->flags & PNV_IODA_PE_MASTER) ? "master " : "",
			(pe->flags & PNV_IODA_PE_SLAVE) ? "slave " : "",
			(pe->flags & PNV_IODA_PE_VF) ? "vf " : "");
	}

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(pnv_pci_ioda_pe_dump_fops, NULL,
			 pnv_pci_ioda_pe_dump, "%llu\n");

#endif /* CONFIG_DEBUG_FS */

static void pnv_pci_ioda_create_dbgfs(void)
{
#ifdef CONFIG_DEBUG_FS
	struct pci_controller *hose, *tmp;
	struct pnv_phb *phb;
	char name[16];

	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		phb = hose->private_data;

		sprintf(name, "PCI%04x", hose->global_number);
		phb->dbgfs = debugfs_create_dir(name, arch_debugfs_dir);

		debugfs_create_file_unsafe("dump_diag_regs", 0200, phb->dbgfs,
					   phb, &pnv_pci_diag_data_fops);
		debugfs_create_file_unsafe("dump_ioda_pe_state", 0200, phb->dbgfs,
					   phb, &pnv_pci_ioda_pe_dump_fops);
	}
#endif /* CONFIG_DEBUG_FS */
}

static void pnv_pci_enable_bridge(struct pci_bus *bus)
{
	struct pci_dev *dev = bus->self;
	struct pci_bus *child;

	/* Empty bus ? bail */
	if (list_empty(&bus->devices))
		return;

	/*
	 * If there's a bridge associated with that bus enable it. This works
	 * around races in the generic code if the enabling is done during
	 * parallel probing. This can be removed once those races have been
	 * fixed.
	 */
	if (dev) {
		int rc = pci_enable_device(dev);
		if (rc)
			pci_err(dev, "Error enabling bridge (%d)\n", rc);
		pci_set_master(dev);
	}

	/* Perform the same to child busses */
	list_for_each_entry(child, &bus->children, node)
		pnv_pci_enable_bridge(child);
}

static void pnv_pci_enable_bridges(void)
{
	struct pci_controller *hose;

	list_for_each_entry(hose, &hose_list, list_node)
		pnv_pci_enable_bridge(hose->bus);
}

static void pnv_pci_ioda_fixup(void)
{
	pnv_pci_ioda_create_dbgfs();

	pnv_pci_enable_bridges();

#ifdef CONFIG_EEH
	pnv_eeh_post_init();
#endif
}

/*
 * Returns the alignment for I/O or memory windows for P2P
 * bridges. That actually depends on how PEs are segmented.
 * For now, we return I/O or M32 segment size for PE sensitive
 * P2P bridges. Otherwise, the default values (4KiB for I/O,
 * 1MiB for memory) will be returned.
 *
 * The current PCI bus might be put into one PE, which was
 * create against the parent PCI bridge. For that case, we
 * needn't enlarge the alignment so that we can save some
 * resources.
 */
static resource_size_t pnv_pci_window_alignment(struct pci_bus *bus,
						unsigned long type)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(bus);
	int num_pci_bridges = 0;
	struct pci_dev *bridge;

	bridge = bus->self;
	while (bridge) {
		if (pci_pcie_type(bridge) == PCI_EXP_TYPE_PCI_BRIDGE) {
			num_pci_bridges++;
			if (num_pci_bridges >= 2)
				return 1;
		}

		bridge = bridge->bus->self;
	}

	/*
	 * We fall back to M32 if M64 isn't supported. We enforce the M64
	 * alignment for any 64-bit resource, PCIe doesn't care and
	 * bridges only do 64-bit prefetchable anyway.
	 */
	if (phb->ioda.m64_segsize && pnv_pci_is_m64_flags(type))
		return phb->ioda.m64_segsize;
	if (type & IORESOURCE_MEM)
		return phb->ioda.m32_segsize;

	return phb->ioda.io_segsize;
}

/*
 * We are updating root port or the upstream port of the
 * bridge behind the root port with PHB's windows in order
 * to accommodate the changes on required resources during
 * PCI (slot) hotplug, which is connected to either root
 * port or the downstream ports of PCIe switch behind the
 * root port.
 */
static void pnv_pci_fixup_bridge_resources(struct pci_bus *bus,
					   unsigned long type)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct pnv_phb *phb = hose->private_data;
	struct pci_dev *bridge = bus->self;
	struct resource *r, *w;
	bool msi_region = false;
	int i;

	/* Check if we need apply fixup to the bridge's windows */
	if (!pci_is_root_bus(bridge->bus) &&
	    !pci_is_root_bus(bridge->bus->self->bus))
		return;

	/* Fixup the resources */
	for (i = 0; i < PCI_BRIDGE_RESOURCE_NUM; i++) {
		r = &bridge->resource[PCI_BRIDGE_RESOURCES + i];
		if (!r->flags || !r->parent)
			continue;

		w = NULL;
		if (r->flags & type & IORESOURCE_IO)
			w = &hose->io_resource;
		else if (pnv_pci_is_m64(phb, r) &&
			 (type & IORESOURCE_PREFETCH) &&
			 phb->ioda.m64_segsize)
			w = &hose->mem_resources[1];
		else if (r->flags & type & IORESOURCE_MEM) {
			w = &hose->mem_resources[0];
			msi_region = true;
		}

		r->start = w->start;
		r->end = w->end;

		/* The 64KB 32-bits MSI region shouldn't be included in
		 * the 32-bits bridge window. Otherwise, we can see strange
		 * issues. One of them is EEH error observed on Garrison.
		 *
		 * Exclude top 1MB region which is the minimal alignment of
		 * 32-bits bridge window.
		 */
		if (msi_region) {
			r->end += 0x10000;
			r->end -= 0x100000;
		}
	}
}

static void pnv_pci_configure_bus(struct pci_bus *bus)
{
	struct pci_dev *bridge = bus->self;
	struct pnv_ioda_pe *pe;
	bool all = (bridge && pci_pcie_type(bridge) == PCI_EXP_TYPE_PCI_BRIDGE);

	dev_info(&bus->dev, "Configuring PE for bus\n");

	/* Don't assign PE to PCI bus, which doesn't have subordinate devices */
	if (WARN_ON(list_empty(&bus->devices)))
		return;

	/* Reserve PEs according to used M64 resources */
	pnv_ioda_reserve_m64_pe(bus, NULL, all);

	/*
	 * Assign PE. We might run here because of partial hotplug.
	 * For the case, we just pick up the existing PE and should
	 * not allocate resources again.
	 */
	pe = pnv_ioda_setup_bus_PE(bus, all);
	if (!pe)
		return;

	pnv_ioda_setup_pe_seg(pe);
}

static resource_size_t pnv_pci_default_alignment(void)
{
	return PAGE_SIZE;
}

/* Prevent enabling devices for which we couldn't properly
 * assign a PE
 */
static bool pnv_pci_enable_device_hook(struct pci_dev *dev)
{
	struct pci_dn *pdn;

	pdn = pci_get_pdn(dev);
	if (!pdn || pdn->pe_number == IODA_INVALID_PE) {
		pci_err(dev, "pci_enable_device() blocked, no PE assigned.\n");
		return false;
	}

	return true;
}

static bool pnv_ocapi_enable_device_hook(struct pci_dev *dev)
{
	struct pci_dn *pdn;
	struct pnv_ioda_pe *pe;

	pdn = pci_get_pdn(dev);
	if (!pdn)
		return false;

	if (pdn->pe_number == IODA_INVALID_PE) {
		pe = pnv_ioda_setup_dev_PE(dev);
		if (!pe)
			return false;
	}
	return true;
}

void pnv_pci_ioda2_release_pe_dma(struct pnv_ioda_pe *pe)
{
	struct iommu_table *tbl = pe->table_group.tables[0];
	int64_t rc;

	if (!pe->dma_setup_done)
		return;

	rc = pnv_pci_ioda2_unset_window(&pe->table_group, 0);
	if (rc)
		pe_warn(pe, "OPAL error %lld release DMA window\n", rc);

	pnv_pci_ioda2_set_bypass(pe, false);
	if (pe->table_group.group) {
		iommu_group_put(pe->table_group.group);
		WARN_ON(pe->table_group.group);
	}

	iommu_tce_table_put(tbl);
}

static void pnv_ioda_free_pe_seg(struct pnv_ioda_pe *pe,
				 unsigned short win,
				 unsigned int *map)
{
	struct pnv_phb *phb = pe->phb;
	int idx;
	int64_t rc;

	for (idx = 0; idx < phb->ioda.total_pe_num; idx++) {
		if (map[idx] != pe->pe_number)
			continue;

		rc = opal_pci_map_pe_mmio_window(phb->opal_id,
				phb->ioda.reserved_pe_idx, win, 0, idx);

		if (rc != OPAL_SUCCESS)
			pe_warn(pe, "Error %lld unmapping (%d) segment#%d\n",
				rc, win, idx);

		map[idx] = IODA_INVALID_PE;
	}
}

static void pnv_ioda_release_pe_seg(struct pnv_ioda_pe *pe)
{
	struct pnv_phb *phb = pe->phb;

	if (phb->type == PNV_PHB_IODA2) {
		pnv_ioda_free_pe_seg(pe, OPAL_M32_WINDOW_TYPE,
				     phb->ioda.m32_segmap);
	}
}

static void pnv_ioda_release_pe(struct pnv_ioda_pe *pe)
{
	struct pnv_phb *phb = pe->phb;
	struct pnv_ioda_pe *slave, *tmp;

	pe_info(pe, "Releasing PE\n");

	mutex_lock(&phb->ioda.pe_list_mutex);
	list_del(&pe->list);
	mutex_unlock(&phb->ioda.pe_list_mutex);

	switch (phb->type) {
	case PNV_PHB_IODA2:
		pnv_pci_ioda2_release_pe_dma(pe);
		break;
	case PNV_PHB_NPU_OCAPI:
		break;
	default:
		WARN_ON(1);
	}

	pnv_ioda_release_pe_seg(pe);
	pnv_ioda_deconfigure_pe(pe->phb, pe);

	/* Release slave PEs in the compound PE */
	if (pe->flags & PNV_IODA_PE_MASTER) {
		list_for_each_entry_safe(slave, tmp, &pe->slaves, list) {
			list_del(&slave->list);
			pnv_ioda_free_pe(slave);
		}
	}

	/*
	 * The PE for root bus can be removed because of hotplug in EEH
	 * recovery for fenced PHB error. We need to mark the PE dead so
	 * that it can be populated again in PCI hot add path. The PE
	 * shouldn't be destroyed as it's the global reserved resource.
	 */
	if (phb->ioda.root_pe_idx == pe->pe_number)
		return;

	pnv_ioda_free_pe(pe);
}

static void pnv_pci_release_device(struct pci_dev *pdev)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(pdev->bus);
	struct pci_dn *pdn = pci_get_pdn(pdev);
	struct pnv_ioda_pe *pe;

	/* The VF PE state is torn down when sriov_disable() is called */
	if (pdev->is_virtfn)
		return;

	if (!pdn || pdn->pe_number == IODA_INVALID_PE)
		return;

#ifdef CONFIG_PCI_IOV
	/*
	 * FIXME: Try move this to sriov_disable(). It's here since we allocate
	 * the iov state at probe time since we need to fiddle with the IOV
	 * resources.
	 */
	if (pdev->is_physfn)
		kfree(pdev->dev.archdata.iov_data);
#endif

	/*
	 * PCI hotplug can happen as part of EEH error recovery. The @pdn
	 * isn't removed and added afterwards in this scenario. We should
	 * set the PE number in @pdn to an invalid one. Otherwise, the PE's
	 * device count is decreased on removing devices while failing to
	 * be increased on adding devices. It leads to unbalanced PE's device
	 * count and eventually make normal PCI hotplug path broken.
	 */
	pe = &phb->ioda.pe_array[pdn->pe_number];
	pdn->pe_number = IODA_INVALID_PE;

	WARN_ON(--pe->device_count < 0);
	if (pe->device_count == 0)
		pnv_ioda_release_pe(pe);
}

static void pnv_pci_ioda_shutdown(struct pci_controller *hose)
{
	struct pnv_phb *phb = hose->private_data;

	opal_pci_reset(phb->opal_id, OPAL_RESET_PCI_IODA_TABLE,
		       OPAL_ASSERT_RESET);
}

static void pnv_pci_ioda_dma_bus_setup(struct pci_bus *bus)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(bus);
	struct pnv_ioda_pe *pe;

	list_for_each_entry(pe, &phb->ioda.pe_list, list) {
		if (!(pe->flags & (PNV_IODA_PE_BUS | PNV_IODA_PE_BUS_ALL)))
			continue;

		if (!pe->pbus)
			continue;

		if (bus->number == ((pe->rid >> 8) & 0xFF)) {
			pe->pbus = bus;
			break;
		}
	}
}

#ifdef CONFIG_IOMMU_API
static struct iommu_group *pnv_pci_device_group(struct pci_controller *hose,
						struct pci_dev *pdev)
{
	struct pnv_phb *phb = hose->private_data;
	struct pnv_ioda_pe *pe;

	if (WARN_ON(!phb))
		return ERR_PTR(-ENODEV);

	pe = pnv_pci_bdfn_to_pe(phb, pdev->devfn | (pdev->bus->number << 8));
	if (!pe)
		return ERR_PTR(-ENODEV);

	if (!pe->table_group.group)
		return ERR_PTR(-ENODEV);

	return iommu_group_ref_get(pe->table_group.group);
}
#endif

static const struct pci_controller_ops pnv_pci_ioda_controller_ops = {
	.dma_dev_setup		= pnv_pci_ioda_dma_dev_setup,
	.dma_bus_setup		= pnv_pci_ioda_dma_bus_setup,
	.iommu_bypass_supported	= pnv_pci_ioda_iommu_bypass_supported,
	.enable_device_hook	= pnv_pci_enable_device_hook,
	.release_device		= pnv_pci_release_device,
	.window_alignment	= pnv_pci_window_alignment,
	.setup_bridge		= pnv_pci_fixup_bridge_resources,
	.reset_secondary_bus	= pnv_pci_reset_secondary_bus,
	.shutdown		= pnv_pci_ioda_shutdown,
#ifdef CONFIG_IOMMU_API
	.device_group		= pnv_pci_device_group,
#endif
};

static const struct pci_controller_ops pnv_npu_ocapi_ioda_controller_ops = {
	.enable_device_hook	= pnv_ocapi_enable_device_hook,
	.release_device		= pnv_pci_release_device,
	.window_alignment	= pnv_pci_window_alignment,
	.reset_secondary_bus	= pnv_pci_reset_secondary_bus,
	.shutdown		= pnv_pci_ioda_shutdown,
};

static void __init pnv_pci_init_ioda_phb(struct device_node *np,
					 u64 hub_id, int ioda_type)
{
	struct pci_controller *hose;
	struct pnv_phb *phb;
	unsigned long size, m64map_off, m32map_off, pemap_off;
	struct pnv_ioda_pe *root_pe;
	struct resource r;
	const __be64 *prop64;
	const __be32 *prop32;
	int len;
	unsigned int segno;
	u64 phb_id;
	void *aux;
	long rc;

	if (!of_device_is_available(np))
		return;

	pr_info("Initializing %s PHB (%pOF)\n",	pnv_phb_names[ioda_type], np);

	prop64 = of_get_property(np, "ibm,opal-phbid", NULL);
	if (!prop64) {
		pr_err("  Missing \"ibm,opal-phbid\" property !\n");
		return;
	}
	phb_id = be64_to_cpup(prop64);
	pr_debug("  PHB-ID  : 0x%016llx\n", phb_id);

	phb = kzalloc(sizeof(*phb), GFP_KERNEL);
	if (!phb)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      sizeof(*phb));

	/* Allocate PCI controller */
	phb->hose = hose = pcibios_alloc_controller(np);
	if (!phb->hose) {
		pr_err("  Can't allocate PCI controller for %pOF\n",
		       np);
		memblock_free(phb, sizeof(struct pnv_phb));
		return;
	}

	spin_lock_init(&phb->lock);
	prop32 = of_get_property(np, "bus-range", &len);
	if (prop32 && len == 8) {
		hose->first_busno = be32_to_cpu(prop32[0]);
		hose->last_busno = be32_to_cpu(prop32[1]);
	} else {
		pr_warn("  Broken <bus-range> on %pOF\n", np);
		hose->first_busno = 0;
		hose->last_busno = 0xff;
	}
	hose->private_data = phb;
	phb->hub_id = hub_id;
	phb->opal_id = phb_id;
	phb->type = ioda_type;
	mutex_init(&phb->ioda.pe_alloc_mutex);

	/* Detect specific models for error handling */
	if (of_device_is_compatible(np, "ibm,p7ioc-pciex"))
		phb->model = PNV_PHB_MODEL_P7IOC;
	else if (of_device_is_compatible(np, "ibm,power8-pciex"))
		phb->model = PNV_PHB_MODEL_PHB3;
	else
		phb->model = PNV_PHB_MODEL_UNKNOWN;

	/* Initialize diagnostic data buffer */
	prop32 = of_get_property(np, "ibm,phb-diag-data-size", NULL);
	if (prop32)
		phb->diag_data_size = be32_to_cpup(prop32);
	else
		phb->diag_data_size = PNV_PCI_DIAG_BUF_SIZE;

	phb->diag_data = kzalloc(phb->diag_data_size, GFP_KERNEL);
	if (!phb->diag_data)
		panic("%s: Failed to allocate %u bytes\n", __func__,
		      phb->diag_data_size);

	/* Parse 32-bit and IO ranges (if any) */
	pci_process_bridge_OF_ranges(hose, np, !hose->global_number);

	/* Get registers */
	if (!of_address_to_resource(np, 0, &r)) {
		phb->regs_phys = r.start;
		phb->regs = ioremap(r.start, resource_size(&r));
		if (phb->regs == NULL)
			pr_err("  Failed to map registers !\n");
	}

	/* Initialize more IODA stuff */
	phb->ioda.total_pe_num = 1;
	prop32 = of_get_property(np, "ibm,opal-num-pes", NULL);
	if (prop32)
		phb->ioda.total_pe_num = be32_to_cpup(prop32);
	prop32 = of_get_property(np, "ibm,opal-reserved-pe", NULL);
	if (prop32)
		phb->ioda.reserved_pe_idx = be32_to_cpup(prop32);

	/* Invalidate RID to PE# mapping */
	for (segno = 0; segno < ARRAY_SIZE(phb->ioda.pe_rmap); segno++)
		phb->ioda.pe_rmap[segno] = IODA_INVALID_PE;

	/* Parse 64-bit MMIO range */
	pnv_ioda_parse_m64_window(phb);

	phb->ioda.m32_size = resource_size(&hose->mem_resources[0]);
	/* FW Has already off top 64k of M32 space (MSI space) */
	phb->ioda.m32_size += 0x10000;

	phb->ioda.m32_segsize = phb->ioda.m32_size / phb->ioda.total_pe_num;
	phb->ioda.m32_pci_base = hose->mem_resources[0].start - hose->mem_offset[0];
	phb->ioda.io_size = hose->pci_io_size;
	phb->ioda.io_segsize = phb->ioda.io_size / phb->ioda.total_pe_num;
	phb->ioda.io_pci_base = 0; /* XXX calculate this ? */

	/* Allocate aux data & arrays. We don't have IO ports on PHB3 */
	size = ALIGN(max_t(unsigned, phb->ioda.total_pe_num, 8) / 8,
			sizeof(unsigned long));
	m64map_off = size;
	size += phb->ioda.total_pe_num * sizeof(phb->ioda.m64_segmap[0]);
	m32map_off = size;
	size += phb->ioda.total_pe_num * sizeof(phb->ioda.m32_segmap[0]);
	pemap_off = size;
	size += phb->ioda.total_pe_num * sizeof(struct pnv_ioda_pe);
	aux = kzalloc(size, GFP_KERNEL);
	if (!aux)
		panic("%s: Failed to allocate %lu bytes\n", __func__, size);

	phb->ioda.pe_alloc = aux;
	phb->ioda.m64_segmap = aux + m64map_off;
	phb->ioda.m32_segmap = aux + m32map_off;
	for (segno = 0; segno < phb->ioda.total_pe_num; segno++) {
		phb->ioda.m64_segmap[segno] = IODA_INVALID_PE;
		phb->ioda.m32_segmap[segno] = IODA_INVALID_PE;
	}
	phb->ioda.pe_array = aux + pemap_off;

	/*
	 * Choose PE number for root bus, which shouldn't have
	 * M64 resources consumed by its child devices. To pick
	 * the PE number adjacent to the reserved one if possible.
	 */
	pnv_ioda_reserve_pe(phb, phb->ioda.reserved_pe_idx);
	if (phb->ioda.reserved_pe_idx == 0) {
		phb->ioda.root_pe_idx = 1;
		pnv_ioda_reserve_pe(phb, phb->ioda.root_pe_idx);
	} else if (phb->ioda.reserved_pe_idx == (phb->ioda.total_pe_num - 1)) {
		phb->ioda.root_pe_idx = phb->ioda.reserved_pe_idx - 1;
		pnv_ioda_reserve_pe(phb, phb->ioda.root_pe_idx);
	} else {
		/* otherwise just allocate one */
		root_pe = pnv_ioda_alloc_pe(phb, 1);
		phb->ioda.root_pe_idx = root_pe->pe_number;
	}

	INIT_LIST_HEAD(&phb->ioda.pe_list);
	mutex_init(&phb->ioda.pe_list_mutex);

#if 0 /* We should really do that ... */
	rc = opal_pci_set_phb_mem_window(opal->phb_id,
					 window_type,
					 window_num,
					 starting_real_address,
					 starting_pci_address,
					 segment_size);
#endif

	pr_info("  %03d (%03d) PE's M32: 0x%x [segment=0x%x]\n",
		phb->ioda.total_pe_num, phb->ioda.reserved_pe_idx,
		phb->ioda.m32_size, phb->ioda.m32_segsize);
	if (phb->ioda.m64_size)
		pr_info("                 M64: 0x%lx [segment=0x%lx]\n",
			phb->ioda.m64_size, phb->ioda.m64_segsize);
	if (phb->ioda.io_size)
		pr_info("                  IO: 0x%x [segment=0x%x]\n",
			phb->ioda.io_size, phb->ioda.io_segsize);


	phb->hose->ops = &pnv_pci_ops;
	phb->get_pe_state = pnv_ioda_get_pe_state;
	phb->freeze_pe = pnv_ioda_freeze_pe;
	phb->unfreeze_pe = pnv_ioda_unfreeze_pe;

	/* Setup MSI support */
	pnv_pci_init_ioda_msis(phb);

	/*
	 * We pass the PCI probe flag PCI_REASSIGN_ALL_RSRC here
	 * to let the PCI core do resource assignment. It's supposed
	 * that the PCI core will do correct I/O and MMIO alignment
	 * for the P2P bridge bars so that each PCI bus (excluding
	 * the child P2P bridges) can form individual PE.
	 */
	ppc_md.pcibios_fixup = pnv_pci_ioda_fixup;

	switch (phb->type) {
	case PNV_PHB_NPU_OCAPI:
		hose->controller_ops = pnv_npu_ocapi_ioda_controller_ops;
		break;
	default:
		hose->controller_ops = pnv_pci_ioda_controller_ops;
	}

	ppc_md.pcibios_default_alignment = pnv_pci_default_alignment;

#ifdef CONFIG_PCI_IOV
	ppc_md.pcibios_fixup_sriov = pnv_pci_ioda_fixup_iov;
	ppc_md.pcibios_iov_resource_alignment = pnv_pci_iov_resource_alignment;
	ppc_md.pcibios_sriov_enable = pnv_pcibios_sriov_enable;
	ppc_md.pcibios_sriov_disable = pnv_pcibios_sriov_disable;
#endif

	pci_add_flags(PCI_REASSIGN_ALL_RSRC);

	/* Reset IODA tables to a clean state */
	rc = opal_pci_reset(phb_id, OPAL_RESET_PCI_IODA_TABLE, OPAL_ASSERT_RESET);
	if (rc)
		pr_warn("  OPAL Error %ld performing IODA table reset !\n", rc);

	/*
	 * If we're running in kdump kernel, the previous kernel never
	 * shutdown PCI devices correctly. We already got IODA table
	 * cleaned out. So we have to issue PHB reset to stop all PCI
	 * transactions from previous kernel. The ppc_pci_reset_phbs
	 * kernel parameter will force this reset too. Additionally,
	 * if the IODA reset above failed then use a bigger hammer.
	 * This can happen if we get a PHB fatal error in very early
	 * boot.
	 */
	if (is_kdump_kernel() || pci_reset_phbs || rc) {
		pr_info("  Issue PHB reset ...\n");
		pnv_eeh_phb_reset(hose, EEH_RESET_FUNDAMENTAL);
		pnv_eeh_phb_reset(hose, EEH_RESET_DEACTIVATE);
	}

	/* Remove M64 resource if we can't configure it successfully */
	if (!phb->init_m64 || phb->init_m64(phb))
		hose->mem_resources[1].flags = 0;

	/* create pci_dn's for DT nodes under this PHB */
	pci_devs_phb_init_dynamic(hose);
}

void __init pnv_pci_init_ioda2_phb(struct device_node *np)
{
	pnv_pci_init_ioda_phb(np, 0, PNV_PHB_IODA2);
}

void __init pnv_pci_init_npu2_opencapi_phb(struct device_node *np)
{
	pnv_pci_init_ioda_phb(np, 0, PNV_PHB_NPU_OCAPI);
}

static void pnv_npu2_opencapi_cfg_size_fixup(struct pci_dev *dev)
{
	struct pnv_phb *phb = pci_bus_to_pnvhb(dev->bus);

	if (!machine_is(powernv))
		return;

	if (phb->type == PNV_PHB_NPU_OCAPI)
		dev->cfg_size = PCI_CFG_SPACE_EXP_SIZE;
}
DECLARE_PCI_FIXUP_EARLY(PCI_ANY_ID, PCI_ANY_ID, pnv_npu2_opencapi_cfg_size_fixup);
