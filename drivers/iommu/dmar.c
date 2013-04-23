/*
 * Copyright (c) 2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Copyright (C) 2006-2008 Intel Corporation
 * Author: Ashok Raj <ashok.raj@intel.com>
 * Author: Shaohua Li <shaohua.li@intel.com>
 * Author: Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *
 * This file implements early detection/parsing of Remapping Devices
 * reported to OS through BIOS via DMA remapping reporting (DMAR) ACPI
 * tables.
 *
 * These routines are used by both DMA-remapping and Interrupt-remapping
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt /* has to precede printk.h */

#include <linux/pci.h>
#include <linux/dmar.h>
#include <linux/iova.h>
#include <linux/intel-iommu.h>
#include <linux/timer.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/tboot.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <asm/irq_remapping.h>
#include <asm/iommu_table.h>

#include "irq_remapping.h"

/* No locks are needed as DMA remapping hardware unit
 * list is constructed at boot time and hotplug of
 * these units are not supported by the architecture.
 */
LIST_HEAD(dmar_drhd_units);

struct acpi_table_header * __initdata dmar_tbl;
static acpi_size dmar_tbl_size;

static void __init dmar_register_drhd_unit(struct dmar_drhd_unit *drhd)
{
	/*
	 * add INCLUDE_ALL at the tail, so scan the list will find it at
	 * the very end.
	 */
	if (drhd->include_all)
		list_add_tail(&drhd->list, &dmar_drhd_units);
	else
		list_add(&drhd->list, &dmar_drhd_units);
}

static int __init dmar_parse_one_dev_scope(struct acpi_dmar_device_scope *scope,
					   struct pci_dev **dev, u16 segment)
{
	struct pci_bus *bus;
	struct pci_dev *pdev = NULL;
	struct acpi_dmar_pci_path *path;
	int count;

	bus = pci_find_bus(segment, scope->bus);
	path = (struct acpi_dmar_pci_path *)(scope + 1);
	count = (scope->length - sizeof(struct acpi_dmar_device_scope))
		/ sizeof(struct acpi_dmar_pci_path);

	while (count) {
		if (pdev)
			pci_dev_put(pdev);
		/*
		 * Some BIOSes list non-exist devices in DMAR table, just
		 * ignore it
		 */
		if (!bus) {
			pr_warn("Device scope bus [%d] not found\n", scope->bus);
			break;
		}
		pdev = pci_get_slot(bus, PCI_DEVFN(path->dev, path->fn));
		if (!pdev) {
			/* warning will be printed below */
			break;
		}
		path ++;
		count --;
		bus = pdev->subordinate;
	}
	if (!pdev) {
		pr_warn("Device scope device [%04x:%02x:%02x.%02x] not found\n",
			segment, scope->bus, path->dev, path->fn);
		*dev = NULL;
		return 0;
	}
	if ((scope->entry_type == ACPI_DMAR_SCOPE_TYPE_ENDPOINT && \
			pdev->subordinate) || (scope->entry_type == \
			ACPI_DMAR_SCOPE_TYPE_BRIDGE && !pdev->subordinate)) {
		pci_dev_put(pdev);
		pr_warn("Device scope type does not match for %s\n",
			pci_name(pdev));
		return -EINVAL;
	}
	*dev = pdev;
	return 0;
}

int __init dmar_parse_dev_scope(void *start, void *end, int *cnt,
				struct pci_dev ***devices, u16 segment)
{
	struct acpi_dmar_device_scope *scope;
	void * tmp = start;
	int index;
	int ret;

	*cnt = 0;
	while (start < end) {
		scope = start;
		if (scope->entry_type == ACPI_DMAR_SCOPE_TYPE_ENDPOINT ||
		    scope->entry_type == ACPI_DMAR_SCOPE_TYPE_BRIDGE)
			(*cnt)++;
		else if (scope->entry_type != ACPI_DMAR_SCOPE_TYPE_IOAPIC &&
			scope->entry_type != ACPI_DMAR_SCOPE_TYPE_HPET) {
			pr_warn("Unsupported device scope\n");
		}
		start += scope->length;
	}
	if (*cnt == 0)
		return 0;

	*devices = kcalloc(*cnt, sizeof(struct pci_dev *), GFP_KERNEL);
	if (!*devices)
		return -ENOMEM;

	start = tmp;
	index = 0;
	while (start < end) {
		scope = start;
		if (scope->entry_type == ACPI_DMAR_SCOPE_TYPE_ENDPOINT ||
		    scope->entry_type == ACPI_DMAR_SCOPE_TYPE_BRIDGE) {
			ret = dmar_parse_one_dev_scope(scope,
				&(*devices)[index], segment);
			if (ret) {
				kfree(*devices);
				return ret;
			}
			index ++;
		}
		start += scope->length;
	}

	return 0;
}

/**
 * dmar_parse_one_drhd - parses exactly one DMA remapping hardware definition
 * structure which uniquely represent one DMA remapping hardware unit
 * present in the platform
 */
static int __init
dmar_parse_one_drhd(struct acpi_dmar_header *header)
{
	struct acpi_dmar_hardware_unit *drhd;
	struct dmar_drhd_unit *dmaru;
	int ret = 0;

	drhd = (struct acpi_dmar_hardware_unit *)header;
	dmaru = kzalloc(sizeof(*dmaru), GFP_KERNEL);
	if (!dmaru)
		return -ENOMEM;

	dmaru->hdr = header;
	dmaru->reg_base_addr = drhd->address;
	dmaru->segment = drhd->segment;
	dmaru->include_all = drhd->flags & 0x1; /* BIT0: INCLUDE_ALL */

	ret = alloc_iommu(dmaru);
	if (ret) {
		kfree(dmaru);
		return ret;
	}
	dmar_register_drhd_unit(dmaru);
	return 0;
}

static int __init dmar_parse_dev(struct dmar_drhd_unit *dmaru)
{
	struct acpi_dmar_hardware_unit *drhd;
	int ret = 0;

	drhd = (struct acpi_dmar_hardware_unit *) dmaru->hdr;

	if (dmaru->include_all)
		return 0;

	ret = dmar_parse_dev_scope((void *)(drhd + 1),
				((void *)drhd) + drhd->header.length,
				&dmaru->devices_cnt, &dmaru->devices,
				drhd->segment);
	if (ret) {
		list_del(&dmaru->list);
		kfree(dmaru);
	}
	return ret;
}

#ifdef CONFIG_ACPI_NUMA
static int __init
dmar_parse_one_rhsa(struct acpi_dmar_header *header)
{
	struct acpi_dmar_rhsa *rhsa;
	struct dmar_drhd_unit *drhd;

	rhsa = (struct acpi_dmar_rhsa *)header;
	for_each_drhd_unit(drhd) {
		if (drhd->reg_base_addr == rhsa->base_address) {
			int node = acpi_map_pxm_to_node(rhsa->proximity_domain);

			if (!node_online(node))
				node = -1;
			drhd->iommu->node = node;
			return 0;
		}
	}
	WARN_TAINT(
		1, TAINT_FIRMWARE_WORKAROUND,
		"Your BIOS is broken; RHSA refers to non-existent DMAR unit at %llx\n"
		"BIOS vendor: %s; Ver: %s; Product Version: %s\n",
		drhd->reg_base_addr,
		dmi_get_system_info(DMI_BIOS_VENDOR),
		dmi_get_system_info(DMI_BIOS_VERSION),
		dmi_get_system_info(DMI_PRODUCT_VERSION));

	return 0;
}
#endif

static void __init
dmar_table_print_dmar_entry(struct acpi_dmar_header *header)
{
	struct acpi_dmar_hardware_unit *drhd;
	struct acpi_dmar_reserved_memory *rmrr;
	struct acpi_dmar_atsr *atsr;
	struct acpi_dmar_rhsa *rhsa;

	switch (header->type) {
	case ACPI_DMAR_TYPE_HARDWARE_UNIT:
		drhd = container_of(header, struct acpi_dmar_hardware_unit,
				    header);
		pr_info("DRHD base: %#016Lx flags: %#x\n",
			(unsigned long long)drhd->address, drhd->flags);
		break;
	case ACPI_DMAR_TYPE_RESERVED_MEMORY:
		rmrr = container_of(header, struct acpi_dmar_reserved_memory,
				    header);
		pr_info("RMRR base: %#016Lx end: %#016Lx\n",
			(unsigned long long)rmrr->base_address,
			(unsigned long long)rmrr->end_address);
		break;
	case ACPI_DMAR_TYPE_ATSR:
		atsr = container_of(header, struct acpi_dmar_atsr, header);
		pr_info("ATSR flags: %#x\n", atsr->flags);
		break;
	case ACPI_DMAR_HARDWARE_AFFINITY:
		rhsa = container_of(header, struct acpi_dmar_rhsa, header);
		pr_info("RHSA base: %#016Lx proximity domain: %#x\n",
		       (unsigned long long)rhsa->base_address,
		       rhsa->proximity_domain);
		break;
	}
}

/**
 * dmar_table_detect - checks to see if the platform supports DMAR devices
 */
static int __init dmar_table_detect(void)
{
	acpi_status status = AE_OK;

	/* if we could find DMAR table, then there are DMAR devices */
	status = acpi_get_table_with_size(ACPI_SIG_DMAR, 0,
				(struct acpi_table_header **)&dmar_tbl,
				&dmar_tbl_size);

	if (ACPI_SUCCESS(status) && !dmar_tbl) {
		pr_warn("Unable to map DMAR\n");
		status = AE_NOT_FOUND;
	}

	return (ACPI_SUCCESS(status) ? 1 : 0);
}

/**
 * parse_dmar_table - parses the DMA reporting table
 */
static int __init
parse_dmar_table(void)
{
	struct acpi_table_dmar *dmar;
	struct acpi_dmar_header *entry_header;
	int ret = 0;

	/*
	 * Do it again, earlier dmar_tbl mapping could be mapped with
	 * fixed map.
	 */
	dmar_table_detect();

	/*
	 * ACPI tables may not be DMA protected by tboot, so use DMAR copy
	 * SINIT saved in SinitMleData in TXT heap (which is DMA protected)
	 */
	dmar_tbl = tboot_get_dmar_table(dmar_tbl);

	dmar = (struct acpi_table_dmar *)dmar_tbl;
	if (!dmar)
		return -ENODEV;

	if (dmar->width < PAGE_SHIFT - 1) {
		pr_warn("Invalid DMAR haw\n");
		return -EINVAL;
	}

	pr_info("Host address width %d\n", dmar->width + 1);

	entry_header = (struct acpi_dmar_header *)(dmar + 1);
	while (((unsigned long)entry_header) <
			(((unsigned long)dmar) + dmar_tbl->length)) {
		/* Avoid looping forever on bad ACPI tables */
		if (entry_header->length == 0) {
			pr_warn("Invalid 0-length structure\n");
			ret = -EINVAL;
			break;
		}

		dmar_table_print_dmar_entry(entry_header);

		switch (entry_header->type) {
		case ACPI_DMAR_TYPE_HARDWARE_UNIT:
			ret = dmar_parse_one_drhd(entry_header);
			break;
		case ACPI_DMAR_TYPE_RESERVED_MEMORY:
			ret = dmar_parse_one_rmrr(entry_header);
			break;
		case ACPI_DMAR_TYPE_ATSR:
			ret = dmar_parse_one_atsr(entry_header);
			break;
		case ACPI_DMAR_HARDWARE_AFFINITY:
#ifdef CONFIG_ACPI_NUMA
			ret = dmar_parse_one_rhsa(entry_header);
#endif
			break;
		default:
			pr_warn("Unknown DMAR structure type %d\n",
				entry_header->type);
			ret = 0; /* for forward compatibility */
			break;
		}
		if (ret)
			break;

		entry_header = ((void *)entry_header + entry_header->length);
	}
	return ret;
}

static int dmar_pci_device_match(struct pci_dev *devices[], int cnt,
			  struct pci_dev *dev)
{
	int index;

	while (dev) {
		for (index = 0; index < cnt; index++)
			if (dev == devices[index])
				return 1;

		/* Check our parent */
		dev = dev->bus->self;
	}

	return 0;
}

struct dmar_drhd_unit *
dmar_find_matched_drhd_unit(struct pci_dev *dev)
{
	struct dmar_drhd_unit *dmaru = NULL;
	struct acpi_dmar_hardware_unit *drhd;

	dev = pci_physfn(dev);

	list_for_each_entry(dmaru, &dmar_drhd_units, list) {
		drhd = container_of(dmaru->hdr,
				    struct acpi_dmar_hardware_unit,
				    header);

		if (dmaru->include_all &&
		    drhd->segment == pci_domain_nr(dev->bus))
			return dmaru;

		if (dmar_pci_device_match(dmaru->devices,
					  dmaru->devices_cnt, dev))
			return dmaru;
	}

	return NULL;
}

int __init dmar_dev_scope_init(void)
{
	static int dmar_dev_scope_initialized;
	struct dmar_drhd_unit *drhd, *drhd_n;
	int ret = -ENODEV;

	if (dmar_dev_scope_initialized)
		return dmar_dev_scope_initialized;

	if (list_empty(&dmar_drhd_units))
		goto fail;

	list_for_each_entry_safe(drhd, drhd_n, &dmar_drhd_units, list) {
		ret = dmar_parse_dev(drhd);
		if (ret)
			goto fail;
	}

	ret = dmar_parse_rmrr_atsr_dev();
	if (ret)
		goto fail;

	dmar_dev_scope_initialized = 1;
	return 0;

fail:
	dmar_dev_scope_initialized = ret;
	return ret;
}


int __init dmar_table_init(void)
{
	static int dmar_table_initialized;
	int ret;

	if (dmar_table_initialized)
		return 0;

	dmar_table_initialized = 1;

	ret = parse_dmar_table();
	if (ret) {
		if (ret != -ENODEV)
			pr_info("parse DMAR table failure.\n");
		return ret;
	}

	if (list_empty(&dmar_drhd_units)) {
		pr_info("No DMAR devices found\n");
		return -ENODEV;
	}

	return 0;
}

static void warn_invalid_dmar(u64 addr, const char *message)
{
	WARN_TAINT_ONCE(
		1, TAINT_FIRMWARE_WORKAROUND,
		"Your BIOS is broken; DMAR reported at address %llx%s!\n"
		"BIOS vendor: %s; Ver: %s; Product Version: %s\n",
		addr, message,
		dmi_get_system_info(DMI_BIOS_VENDOR),
		dmi_get_system_info(DMI_BIOS_VERSION),
		dmi_get_system_info(DMI_PRODUCT_VERSION));
}

int __init check_zero_address(void)
{
	struct acpi_table_dmar *dmar;
	struct acpi_dmar_header *entry_header;
	struct acpi_dmar_hardware_unit *drhd;

	dmar = (struct acpi_table_dmar *)dmar_tbl;
	entry_header = (struct acpi_dmar_header *)(dmar + 1);

	while (((unsigned long)entry_header) <
			(((unsigned long)dmar) + dmar_tbl->length)) {
		/* Avoid looping forever on bad ACPI tables */
		if (entry_header->length == 0) {
			pr_warn("Invalid 0-length structure\n");
			return 0;
		}

		if (entry_header->type == ACPI_DMAR_TYPE_HARDWARE_UNIT) {
			void __iomem *addr;
			u64 cap, ecap;

			drhd = (void *)entry_header;
			if (!drhd->address) {
				warn_invalid_dmar(0, "");
				goto failed;
			}

			addr = early_ioremap(drhd->address, VTD_PAGE_SIZE);
			if (!addr ) {
				printk("IOMMU: can't validate: %llx\n", drhd->address);
				goto failed;
			}
			cap = dmar_readq(addr + DMAR_CAP_REG);
			ecap = dmar_readq(addr + DMAR_ECAP_REG);
			early_iounmap(addr, VTD_PAGE_SIZE);
			if (cap == (uint64_t)-1 && ecap == (uint64_t)-1) {
				warn_invalid_dmar(drhd->address,
						  " returns all ones");
				goto failed;
			}
		}

		entry_header = ((void *)entry_header + entry_header->length);
	}
	return 1;

failed:
	return 0;
}

int __init detect_intel_iommu(void)
{
	int ret;

	ret = dmar_table_detect();
	if (ret)
		ret = check_zero_address();
	{
		struct acpi_table_dmar *dmar;

		dmar = (struct acpi_table_dmar *) dmar_tbl;

		if (ret && irq_remapping_enabled && cpu_has_x2apic &&
		    dmar->flags & 0x1)
			pr_info("Queued invalidation will be enabled to support x2apic and Intr-remapping.\n");

		if (ret && !no_iommu && !iommu_detected && !dmar_disabled) {
			iommu_detected = 1;
			/* Make sure ACS will be enabled */
			pci_request_acs();
		}

#ifdef CONFIG_X86
		if (ret)
			x86_init.iommu.iommu_init = intel_iommu_init;
#endif
	}
	early_acpi_os_unmap_memory(dmar_tbl, dmar_tbl_size);
	dmar_tbl = NULL;

	return ret ? 1 : -ENODEV;
}


static void unmap_iommu(struct intel_iommu *iommu)
{
	iounmap(iommu->reg);
	release_mem_region(iommu->reg_phys, iommu->reg_size);
}

/**
 * map_iommu: map the iommu's registers
 * @iommu: the iommu to map
 * @phys_addr: the physical address of the base resgister
 *
 * Memory map the iommu's registers.  Start w/ a single page, and
 * possibly expand if that turns out to be insufficent.
 */
static int map_iommu(struct intel_iommu *iommu, u64 phys_addr)
{
	int map_size, err=0;

	iommu->reg_phys = phys_addr;
	iommu->reg_size = VTD_PAGE_SIZE;

	if (!request_mem_region(iommu->reg_phys, iommu->reg_size, iommu->name)) {
		pr_err("IOMMU: can't reserve memory\n");
		err = -EBUSY;
		goto out;
	}

	iommu->reg = ioremap(iommu->reg_phys, iommu->reg_size);
	if (!iommu->reg) {
		pr_err("IOMMU: can't map the region\n");
		err = -ENOMEM;
		goto release;
	}

	iommu->cap = dmar_readq(iommu->reg + DMAR_CAP_REG);
	iommu->ecap = dmar_readq(iommu->reg + DMAR_ECAP_REG);

	if (iommu->cap == (uint64_t)-1 && iommu->ecap == (uint64_t)-1) {
		err = -EINVAL;
		warn_invalid_dmar(phys_addr, " returns all ones");
		goto unmap;
	}

	/* the registers might be more than one page */
	map_size = max_t(int, ecap_max_iotlb_offset(iommu->ecap),
			 cap_max_fault_reg_offset(iommu->cap));
	map_size = VTD_PAGE_ALIGN(map_size);
	if (map_size > iommu->reg_size) {
		iounmap(iommu->reg);
		release_mem_region(iommu->reg_phys, iommu->reg_size);
		iommu->reg_size = map_size;
		if (!request_mem_region(iommu->reg_phys, iommu->reg_size,
					iommu->name)) {
			pr_err("IOMMU: can't reserve memory\n");
			err = -EBUSY;
			goto out;
		}
		iommu->reg = ioremap(iommu->reg_phys, iommu->reg_size);
		if (!iommu->reg) {
			pr_err("IOMMU: can't map the region\n");
			err = -ENOMEM;
			goto release;
		}
	}
	err = 0;
	goto out;

unmap:
	iounmap(iommu->reg);
release:
	release_mem_region(iommu->reg_phys, iommu->reg_size);
out:
	return err;
}

int alloc_iommu(struct dmar_drhd_unit *drhd)
{
	struct intel_iommu *iommu;
	u32 ver;
	static int iommu_allocated = 0;
	int agaw = 0;
	int msagaw = 0;
	int err;

	if (!drhd->reg_base_addr) {
		warn_invalid_dmar(0, "");
		return -EINVAL;
	}

	iommu = kzalloc(sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return -ENOMEM;

	iommu->seq_id = iommu_allocated++;
	sprintf (iommu->name, "dmar%d", iommu->seq_id);

	err = map_iommu(iommu, drhd->reg_base_addr);
	if (err) {
		pr_err("IOMMU: failed to map %s\n", iommu->name);
		goto error;
	}

	err = -EINVAL;
	agaw = iommu_calculate_agaw(iommu);
	if (agaw < 0) {
		pr_err("Cannot get a valid agaw for iommu (seq_id = %d)\n",
			iommu->seq_id);
		goto err_unmap;
	}
	msagaw = iommu_calculate_max_sagaw(iommu);
	if (msagaw < 0) {
		pr_err("Cannot get a valid max agaw for iommu (seq_id = %d)\n",
			iommu->seq_id);
		goto err_unmap;
	}
	iommu->agaw = agaw;
	iommu->msagaw = msagaw;

	iommu->node = -1;

	ver = readl(iommu->reg + DMAR_VER_REG);
	pr_info("IOMMU %d: reg_base_addr %llx ver %d:%d cap %llx ecap %llx\n",
		iommu->seq_id,
		(unsigned long long)drhd->reg_base_addr,
		DMAR_VER_MAJOR(ver), DMAR_VER_MINOR(ver),
		(unsigned long long)iommu->cap,
		(unsigned long long)iommu->ecap);

	raw_spin_lock_init(&iommu->register_lock);

	drhd->iommu = iommu;
	return 0;

 err_unmap:
	unmap_iommu(iommu);
 error:
	kfree(iommu);
	return err;
}

void free_iommu(struct intel_iommu *iommu)
{
	if (!iommu)
		return;

	free_dmar_iommu(iommu);

	if (iommu->reg)
		unmap_iommu(iommu);

	kfree(iommu);
}

/*
 * Reclaim all the submitted descriptors which have completed its work.
 */
static inline void reclaim_free_desc(struct q_inval *qi)
{
	while (qi->desc_status[qi->free_tail] == QI_DONE ||
	       qi->desc_status[qi->free_tail] == QI_ABORT) {
		qi->desc_status[qi->free_tail] = QI_FREE;
		qi->free_tail = (qi->free_tail + 1) % QI_LENGTH;
		qi->free_cnt++;
	}
}

static int qi_check_fault(struct intel_iommu *iommu, int index)
{
	u32 fault;
	int head, tail;
	struct q_inval *qi = iommu->qi;
	int wait_index = (index + 1) % QI_LENGTH;

	if (qi->desc_status[wait_index] == QI_ABORT)
		return -EAGAIN;

	fault = readl(iommu->reg + DMAR_FSTS_REG);

	/*
	 * If IQE happens, the head points to the descriptor associated
	 * with the error. No new descriptors are fetched until the IQE
	 * is cleared.
	 */
	if (fault & DMA_FSTS_IQE) {
		head = readl(iommu->reg + DMAR_IQH_REG);
		if ((head >> DMAR_IQ_SHIFT) == index) {
			pr_err("VT-d detected invalid descriptor: "
				"low=%llx, high=%llx\n",
				(unsigned long long)qi->desc[index].low,
				(unsigned long long)qi->desc[index].high);
			memcpy(&qi->desc[index], &qi->desc[wait_index],
					sizeof(struct qi_desc));
			__iommu_flush_cache(iommu, &qi->desc[index],
					sizeof(struct qi_desc));
			writel(DMA_FSTS_IQE, iommu->reg + DMAR_FSTS_REG);
			return -EINVAL;
		}
	}

	/*
	 * If ITE happens, all pending wait_desc commands are aborted.
	 * No new descriptors are fetched until the ITE is cleared.
	 */
	if (fault & DMA_FSTS_ITE) {
		head = readl(iommu->reg + DMAR_IQH_REG);
		head = ((head >> DMAR_IQ_SHIFT) - 1 + QI_LENGTH) % QI_LENGTH;
		head |= 1;
		tail = readl(iommu->reg + DMAR_IQT_REG);
		tail = ((tail >> DMAR_IQ_SHIFT) - 1 + QI_LENGTH) % QI_LENGTH;

		writel(DMA_FSTS_ITE, iommu->reg + DMAR_FSTS_REG);

		do {
			if (qi->desc_status[head] == QI_IN_USE)
				qi->desc_status[head] = QI_ABORT;
			head = (head - 2 + QI_LENGTH) % QI_LENGTH;
		} while (head != tail);

		if (qi->desc_status[wait_index] == QI_ABORT)
			return -EAGAIN;
	}

	if (fault & DMA_FSTS_ICE)
		writel(DMA_FSTS_ICE, iommu->reg + DMAR_FSTS_REG);

	return 0;
}

/*
 * Submit the queued invalidation descriptor to the remapping
 * hardware unit and wait for its completion.
 */
int qi_submit_sync(struct qi_desc *desc, struct intel_iommu *iommu)
{
	int rc;
	struct q_inval *qi = iommu->qi;
	struct qi_desc *hw, wait_desc;
	int wait_index, index;
	unsigned long flags;

	if (!qi)
		return 0;

	hw = qi->desc;

restart:
	rc = 0;

	raw_spin_lock_irqsave(&qi->q_lock, flags);
	while (qi->free_cnt < 3) {
		raw_spin_unlock_irqrestore(&qi->q_lock, flags);
		cpu_relax();
		raw_spin_lock_irqsave(&qi->q_lock, flags);
	}

	index = qi->free_head;
	wait_index = (index + 1) % QI_LENGTH;

	qi->desc_status[index] = qi->desc_status[wait_index] = QI_IN_USE;

	hw[index] = *desc;

	wait_desc.low = QI_IWD_STATUS_DATA(QI_DONE) |
			QI_IWD_STATUS_WRITE | QI_IWD_TYPE;
	wait_desc.high = virt_to_phys(&qi->desc_status[wait_index]);

	hw[wait_index] = wait_desc;

	__iommu_flush_cache(iommu, &hw[index], sizeof(struct qi_desc));
	__iommu_flush_cache(iommu, &hw[wait_index], sizeof(struct qi_desc));

	qi->free_head = (qi->free_head + 2) % QI_LENGTH;
	qi->free_cnt -= 2;

	/*
	 * update the HW tail register indicating the presence of
	 * new descriptors.
	 */
	writel(qi->free_head << DMAR_IQ_SHIFT, iommu->reg + DMAR_IQT_REG);

	while (qi->desc_status[wait_index] != QI_DONE) {
		/*
		 * We will leave the interrupts disabled, to prevent interrupt
		 * context to queue another cmd while a cmd is already submitted
		 * and waiting for completion on this cpu. This is to avoid
		 * a deadlock where the interrupt context can wait indefinitely
		 * for free slots in the queue.
		 */
		rc = qi_check_fault(iommu, index);
		if (rc)
			break;

		raw_spin_unlock(&qi->q_lock);
		cpu_relax();
		raw_spin_lock(&qi->q_lock);
	}

	qi->desc_status[index] = QI_DONE;

	reclaim_free_desc(qi);
	raw_spin_unlock_irqrestore(&qi->q_lock, flags);

	if (rc == -EAGAIN)
		goto restart;

	return rc;
}

/*
 * Flush the global interrupt entry cache.
 */
void qi_global_iec(struct intel_iommu *iommu)
{
	struct qi_desc desc;

	desc.low = QI_IEC_TYPE;
	desc.high = 0;

	/* should never fail */
	qi_submit_sync(&desc, iommu);
}

void qi_flush_context(struct intel_iommu *iommu, u16 did, u16 sid, u8 fm,
		      u64 type)
{
	struct qi_desc desc;

	desc.low = QI_CC_FM(fm) | QI_CC_SID(sid) | QI_CC_DID(did)
			| QI_CC_GRAN(type) | QI_CC_TYPE;
	desc.high = 0;

	qi_submit_sync(&desc, iommu);
}

void qi_flush_iotlb(struct intel_iommu *iommu, u16 did, u64 addr,
		    unsigned int size_order, u64 type)
{
	u8 dw = 0, dr = 0;

	struct qi_desc desc;
	int ih = 0;

	if (cap_write_drain(iommu->cap))
		dw = 1;

	if (cap_read_drain(iommu->cap))
		dr = 1;

	desc.low = QI_IOTLB_DID(did) | QI_IOTLB_DR(dr) | QI_IOTLB_DW(dw)
		| QI_IOTLB_GRAN(type) | QI_IOTLB_TYPE;
	desc.high = QI_IOTLB_ADDR(addr) | QI_IOTLB_IH(ih)
		| QI_IOTLB_AM(size_order);

	qi_submit_sync(&desc, iommu);
}

void qi_flush_dev_iotlb(struct intel_iommu *iommu, u16 sid, u16 qdep,
			u64 addr, unsigned mask)
{
	struct qi_desc desc;

	if (mask) {
		BUG_ON(addr & ((1 << (VTD_PAGE_SHIFT + mask)) - 1));
		addr |= (1 << (VTD_PAGE_SHIFT + mask - 1)) - 1;
		desc.high = QI_DEV_IOTLB_ADDR(addr) | QI_DEV_IOTLB_SIZE;
	} else
		desc.high = QI_DEV_IOTLB_ADDR(addr);

	if (qdep >= QI_DEV_IOTLB_MAX_INVS)
		qdep = 0;

	desc.low = QI_DEV_IOTLB_SID(sid) | QI_DEV_IOTLB_QDEP(qdep) |
		   QI_DIOTLB_TYPE;

	qi_submit_sync(&desc, iommu);
}

/*
 * Disable Queued Invalidation interface.
 */
void dmar_disable_qi(struct intel_iommu *iommu)
{
	unsigned long flags;
	u32 sts;
	cycles_t start_time = get_cycles();

	if (!ecap_qis(iommu->ecap))
		return;

	raw_spin_lock_irqsave(&iommu->register_lock, flags);

	sts =  dmar_readq(iommu->reg + DMAR_GSTS_REG);
	if (!(sts & DMA_GSTS_QIES))
		goto end;

	/*
	 * Give a chance to HW to complete the pending invalidation requests.
	 */
	while ((readl(iommu->reg + DMAR_IQT_REG) !=
		readl(iommu->reg + DMAR_IQH_REG)) &&
		(DMAR_OPERATION_TIMEOUT > (get_cycles() - start_time)))
		cpu_relax();

	iommu->gcmd &= ~DMA_GCMD_QIE;
	writel(iommu->gcmd, iommu->reg + DMAR_GCMD_REG);

	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG, readl,
		      !(sts & DMA_GSTS_QIES), sts);
end:
	raw_spin_unlock_irqrestore(&iommu->register_lock, flags);
}

/*
 * Enable queued invalidation.
 */
static void __dmar_enable_qi(struct intel_iommu *iommu)
{
	u32 sts;
	unsigned long flags;
	struct q_inval *qi = iommu->qi;

	qi->free_head = qi->free_tail = 0;
	qi->free_cnt = QI_LENGTH;

	raw_spin_lock_irqsave(&iommu->register_lock, flags);

	/* write zero to the tail reg */
	writel(0, iommu->reg + DMAR_IQT_REG);

	dmar_writeq(iommu->reg + DMAR_IQA_REG, virt_to_phys(qi->desc));

	iommu->gcmd |= DMA_GCMD_QIE;
	writel(iommu->gcmd, iommu->reg + DMAR_GCMD_REG);

	/* Make sure hardware complete it */
	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG, readl, (sts & DMA_GSTS_QIES), sts);

	raw_spin_unlock_irqrestore(&iommu->register_lock, flags);
}

/*
 * Enable Queued Invalidation interface. This is a must to support
 * interrupt-remapping. Also used by DMA-remapping, which replaces
 * register based IOTLB invalidation.
 */
int dmar_enable_qi(struct intel_iommu *iommu)
{
	struct q_inval *qi;
	struct page *desc_page;

	if (!ecap_qis(iommu->ecap))
		return -ENOENT;

	/*
	 * queued invalidation is already setup and enabled.
	 */
	if (iommu->qi)
		return 0;

	iommu->qi = kmalloc(sizeof(*qi), GFP_ATOMIC);
	if (!iommu->qi)
		return -ENOMEM;

	qi = iommu->qi;


	desc_page = alloc_pages_node(iommu->node, GFP_ATOMIC | __GFP_ZERO, 0);
	if (!desc_page) {
		kfree(qi);
		iommu->qi = 0;
		return -ENOMEM;
	}

	qi->desc = page_address(desc_page);

	qi->desc_status = kzalloc(QI_LENGTH * sizeof(int), GFP_ATOMIC);
	if (!qi->desc_status) {
		free_page((unsigned long) qi->desc);
		kfree(qi);
		iommu->qi = 0;
		return -ENOMEM;
	}

	qi->free_head = qi->free_tail = 0;
	qi->free_cnt = QI_LENGTH;

	raw_spin_lock_init(&qi->q_lock);

	__dmar_enable_qi(iommu);

	return 0;
}

/* iommu interrupt handling. Most stuff are MSI-like. */

enum faulttype {
	DMA_REMAP,
	INTR_REMAP,
	UNKNOWN,
};

static const char *dma_remap_fault_reasons[] =
{
	"Software",
	"Present bit in root entry is clear",
	"Present bit in context entry is clear",
	"Invalid context entry",
	"Access beyond MGAW",
	"PTE Write access is not set",
	"PTE Read access is not set",
	"Next page table ptr is invalid",
	"Root table address invalid",
	"Context table ptr is invalid",
	"non-zero reserved fields in RTP",
	"non-zero reserved fields in CTP",
	"non-zero reserved fields in PTE",
	"PCE for translation request specifies blocking",
};

static const char *irq_remap_fault_reasons[] =
{
	"Detected reserved fields in the decoded interrupt-remapped request",
	"Interrupt index exceeded the interrupt-remapping table size",
	"Present field in the IRTE entry is clear",
	"Error accessing interrupt-remapping table pointed by IRTA_REG",
	"Detected reserved fields in the IRTE entry",
	"Blocked a compatibility format interrupt request",
	"Blocked an interrupt request due to source-id verification failure",
};

#define MAX_FAULT_REASON_IDX 	(ARRAY_SIZE(fault_reason_strings) - 1)

const char *dmar_get_fault_reason(u8 fault_reason, int *fault_type)
{
	if (fault_reason >= 0x20 && (fault_reason - 0x20 <
					ARRAY_SIZE(irq_remap_fault_reasons))) {
		*fault_type = INTR_REMAP;
		return irq_remap_fault_reasons[fault_reason - 0x20];
	} else if (fault_reason < ARRAY_SIZE(dma_remap_fault_reasons)) {
		*fault_type = DMA_REMAP;
		return dma_remap_fault_reasons[fault_reason];
	} else {
		*fault_type = UNKNOWN;
		return "Unknown";
	}
}

void dmar_msi_unmask(struct irq_data *data)
{
	struct intel_iommu *iommu = irq_data_get_irq_handler_data(data);
	unsigned long flag;

	/* unmask it */
	raw_spin_lock_irqsave(&iommu->register_lock, flag);
	writel(0, iommu->reg + DMAR_FECTL_REG);
	/* Read a reg to force flush the post write */
	readl(iommu->reg + DMAR_FECTL_REG);
	raw_spin_unlock_irqrestore(&iommu->register_lock, flag);
}

void dmar_msi_mask(struct irq_data *data)
{
	unsigned long flag;
	struct intel_iommu *iommu = irq_data_get_irq_handler_data(data);

	/* mask it */
	raw_spin_lock_irqsave(&iommu->register_lock, flag);
	writel(DMA_FECTL_IM, iommu->reg + DMAR_FECTL_REG);
	/* Read a reg to force flush the post write */
	readl(iommu->reg + DMAR_FECTL_REG);
	raw_spin_unlock_irqrestore(&iommu->register_lock, flag);
}

void dmar_msi_write(int irq, struct msi_msg *msg)
{
	struct intel_iommu *iommu = irq_get_handler_data(irq);
	unsigned long flag;

	raw_spin_lock_irqsave(&iommu->register_lock, flag);
	writel(msg->data, iommu->reg + DMAR_FEDATA_REG);
	writel(msg->address_lo, iommu->reg + DMAR_FEADDR_REG);
	writel(msg->address_hi, iommu->reg + DMAR_FEUADDR_REG);
	raw_spin_unlock_irqrestore(&iommu->register_lock, flag);
}

void dmar_msi_read(int irq, struct msi_msg *msg)
{
	struct intel_iommu *iommu = irq_get_handler_data(irq);
	unsigned long flag;

	raw_spin_lock_irqsave(&iommu->register_lock, flag);
	msg->data = readl(iommu->reg + DMAR_FEDATA_REG);
	msg->address_lo = readl(iommu->reg + DMAR_FEADDR_REG);
	msg->address_hi = readl(iommu->reg + DMAR_FEUADDR_REG);
	raw_spin_unlock_irqrestore(&iommu->register_lock, flag);
}

static int dmar_fault_do_one(struct intel_iommu *iommu, int type,
		u8 fault_reason, u16 source_id, unsigned long long addr)
{
	const char *reason;
	int fault_type;

	reason = dmar_get_fault_reason(fault_reason, &fault_type);

	if (fault_type == INTR_REMAP)
		pr_err("INTR-REMAP: Request device [[%02x:%02x.%d] "
		       "fault index %llx\n"
			"INTR-REMAP:[fault reason %02d] %s\n",
			(source_id >> 8), PCI_SLOT(source_id & 0xFF),
			PCI_FUNC(source_id & 0xFF), addr >> 48,
			fault_reason, reason);
	else
		pr_err("DMAR:[%s] Request device [%02x:%02x.%d] "
		       "fault addr %llx \n"
		       "DMAR:[fault reason %02d] %s\n",
		       (type ? "DMA Read" : "DMA Write"),
		       (source_id >> 8), PCI_SLOT(source_id & 0xFF),
		       PCI_FUNC(source_id & 0xFF), addr, fault_reason, reason);
	return 0;
}

#define PRIMARY_FAULT_REG_LEN (16)
irqreturn_t dmar_fault(int irq, void *dev_id)
{
	struct intel_iommu *iommu = dev_id;
	int reg, fault_index;
	u32 fault_status;
	unsigned long flag;

	raw_spin_lock_irqsave(&iommu->register_lock, flag);
	fault_status = readl(iommu->reg + DMAR_FSTS_REG);
	if (fault_status)
		pr_err("DRHD: handling fault status reg %x\n", fault_status);

	/* TBD: ignore advanced fault log currently */
	if (!(fault_status & DMA_FSTS_PPF))
		goto clear_rest;

	fault_index = dma_fsts_fault_record_index(fault_status);
	reg = cap_fault_reg_offset(iommu->cap);
	while (1) {
		u8 fault_reason;
		u16 source_id;
		u64 guest_addr;
		int type;
		u32 data;

		/* highest 32 bits */
		data = readl(iommu->reg + reg +
				fault_index * PRIMARY_FAULT_REG_LEN + 12);
		if (!(data & DMA_FRCD_F))
			break;

		fault_reason = dma_frcd_fault_reason(data);
		type = dma_frcd_type(data);

		data = readl(iommu->reg + reg +
				fault_index * PRIMARY_FAULT_REG_LEN + 8);
		source_id = dma_frcd_source_id(data);

		guest_addr = dmar_readq(iommu->reg + reg +
				fault_index * PRIMARY_FAULT_REG_LEN);
		guest_addr = dma_frcd_page_addr(guest_addr);
		/* clear the fault */
		writel(DMA_FRCD_F, iommu->reg + reg +
			fault_index * PRIMARY_FAULT_REG_LEN + 12);

		raw_spin_unlock_irqrestore(&iommu->register_lock, flag);

		dmar_fault_do_one(iommu, type, fault_reason,
				source_id, guest_addr);

		fault_index++;
		if (fault_index >= cap_num_fault_regs(iommu->cap))
			fault_index = 0;
		raw_spin_lock_irqsave(&iommu->register_lock, flag);
	}
clear_rest:
	/* clear all the other faults */
	fault_status = readl(iommu->reg + DMAR_FSTS_REG);
	writel(fault_status, iommu->reg + DMAR_FSTS_REG);

	raw_spin_unlock_irqrestore(&iommu->register_lock, flag);
	return IRQ_HANDLED;
}

int dmar_set_interrupt(struct intel_iommu *iommu)
{
	int irq, ret;

	/*
	 * Check if the fault interrupt is already initialized.
	 */
	if (iommu->irq)
		return 0;

	irq = create_irq();
	if (!irq) {
		pr_err("IOMMU: no free vectors\n");
		return -EINVAL;
	}

	irq_set_handler_data(irq, iommu);
	iommu->irq = irq;

	ret = arch_setup_dmar_msi(irq);
	if (ret) {
		irq_set_handler_data(irq, NULL);
		iommu->irq = 0;
		destroy_irq(irq);
		return ret;
	}

	ret = request_irq(irq, dmar_fault, IRQF_NO_THREAD, iommu->name, iommu);
	if (ret)
		pr_err("IOMMU: can't request irq\n");
	return ret;
}

int __init enable_drhd_fault_handling(void)
{
	struct dmar_drhd_unit *drhd;

	/*
	 * Enable fault control interrupt.
	 */
	for_each_drhd_unit(drhd) {
		int ret;
		struct intel_iommu *iommu = drhd->iommu;
		ret = dmar_set_interrupt(iommu);

		if (ret) {
			pr_err("DRHD %Lx: failed to enable fault, interrupt, ret %d\n",
			       (unsigned long long)drhd->reg_base_addr, ret);
			return -1;
		}

		/*
		 * Clear any previous faults.
		 */
		dmar_fault(iommu->irq, iommu);
	}

	return 0;
}

/*
 * Re-enable Queued Invalidation interface.
 */
int dmar_reenable_qi(struct intel_iommu *iommu)
{
	if (!ecap_qis(iommu->ecap))
		return -ENOENT;

	if (!iommu->qi)
		return -ENOENT;

	/*
	 * First disable queued invalidation.
	 */
	dmar_disable_qi(iommu);
	/*
	 * Then enable queued invalidation again. Since there is no pending
	 * invalidation requests now, it's safe to re-enable queued
	 * invalidation.
	 */
	__dmar_enable_qi(iommu);

	return 0;
}

/*
 * Check interrupt remapping support in DMAR table description.
 */
int __init dmar_ir_support(void)
{
	struct acpi_table_dmar *dmar;
	dmar = (struct acpi_table_dmar *)dmar_tbl;
	if (!dmar)
		return 0;
	return dmar->flags & 0x1;
}
IOMMU_INIT_POST(detect_intel_iommu);
