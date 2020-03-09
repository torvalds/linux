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

#define pr_fmt(fmt)     "DMAR: " fmt

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
#include <linux/iommu.h>
#include <asm/irq_remapping.h>
#include <asm/iommu_table.h>

#include "irq_remapping.h"

typedef int (*dmar_res_handler_t)(struct acpi_dmar_header *, void *);
struct dmar_res_callback {
	dmar_res_handler_t	cb[ACPI_DMAR_TYPE_RESERVED];
	void			*arg[ACPI_DMAR_TYPE_RESERVED];
	bool			ignore_unhandled;
	bool			print_entry;
};

/*
 * Assumptions:
 * 1) The hotplug framework guarentees that DMAR unit will be hot-added
 *    before IO devices managed by that unit.
 * 2) The hotplug framework guarantees that DMAR unit will be hot-removed
 *    after IO devices managed by that unit.
 * 3) Hotplug events are rare.
 *
 * Locking rules for DMA and interrupt remapping related global data structures:
 * 1) Use dmar_global_lock in process context
 * 2) Use RCU in interrupt context
 */
DECLARE_RWSEM(dmar_global_lock);
LIST_HEAD(dmar_drhd_units);

struct acpi_table_header * __initdata dmar_tbl;
static int dmar_dev_scope_status = 1;
static unsigned long dmar_seq_ids[BITS_TO_LONGS(DMAR_UNITS_SUPPORTED)];

static int alloc_iommu(struct dmar_drhd_unit *drhd);
static void free_iommu(struct intel_iommu *iommu);

extern const struct iommu_ops intel_iommu_ops;

static void dmar_register_drhd_unit(struct dmar_drhd_unit *drhd)
{
	/*
	 * add INCLUDE_ALL at the tail, so scan the list will find it at
	 * the very end.
	 */
	if (drhd->include_all)
		list_add_tail_rcu(&drhd->list, &dmar_drhd_units);
	else
		list_add_rcu(&drhd->list, &dmar_drhd_units);
}

void *dmar_alloc_dev_scope(void *start, void *end, int *cnt)
{
	struct acpi_dmar_device_scope *scope;

	*cnt = 0;
	while (start < end) {
		scope = start;
		if (scope->entry_type == ACPI_DMAR_SCOPE_TYPE_NAMESPACE ||
		    scope->entry_type == ACPI_DMAR_SCOPE_TYPE_ENDPOINT ||
		    scope->entry_type == ACPI_DMAR_SCOPE_TYPE_BRIDGE)
			(*cnt)++;
		else if (scope->entry_type != ACPI_DMAR_SCOPE_TYPE_IOAPIC &&
			scope->entry_type != ACPI_DMAR_SCOPE_TYPE_HPET) {
			pr_warn("Unsupported device scope\n");
		}
		start += scope->length;
	}
	if (*cnt == 0)
		return NULL;

	return kcalloc(*cnt, sizeof(struct dmar_dev_scope), GFP_KERNEL);
}

void dmar_free_dev_scope(struct dmar_dev_scope **devices, int *cnt)
{
	int i;
	struct device *tmp_dev;

	if (*devices && *cnt) {
		for_each_active_dev_scope(*devices, *cnt, i, tmp_dev)
			put_device(tmp_dev);
		kfree(*devices);
	}

	*devices = NULL;
	*cnt = 0;
}

/* Optimize out kzalloc()/kfree() for normal cases */
static char dmar_pci_notify_info_buf[64];

static struct dmar_pci_notify_info *
dmar_alloc_pci_notify_info(struct pci_dev *dev, unsigned long event)
{
	int level = 0;
	size_t size;
	struct pci_dev *tmp;
	struct dmar_pci_notify_info *info;

	BUG_ON(dev->is_virtfn);

	/* Only generate path[] for device addition event */
	if (event == BUS_NOTIFY_ADD_DEVICE)
		for (tmp = dev; tmp; tmp = tmp->bus->self)
			level++;

	size = sizeof(*info) + level * sizeof(info->path[0]);
	if (size <= sizeof(dmar_pci_notify_info_buf)) {
		info = (struct dmar_pci_notify_info *)dmar_pci_notify_info_buf;
	} else {
		info = kzalloc(size, GFP_KERNEL);
		if (!info) {
			pr_warn("Out of memory when allocating notify_info "
				"for %s.\n", pci_name(dev));
			if (dmar_dev_scope_status == 0)
				dmar_dev_scope_status = -ENOMEM;
			return NULL;
		}
	}

	info->event = event;
	info->dev = dev;
	info->seg = pci_domain_nr(dev->bus);
	info->level = level;
	if (event == BUS_NOTIFY_ADD_DEVICE) {
		for (tmp = dev; tmp; tmp = tmp->bus->self) {
			level--;
			info->path[level].bus = tmp->bus->number;
			info->path[level].device = PCI_SLOT(tmp->devfn);
			info->path[level].function = PCI_FUNC(tmp->devfn);
			if (pci_is_root_bus(tmp->bus))
				info->bus = tmp->bus->number;
		}
	}

	return info;
}

static inline void dmar_free_pci_notify_info(struct dmar_pci_notify_info *info)
{
	if ((void *)info != dmar_pci_notify_info_buf)
		kfree(info);
}

static bool dmar_match_pci_path(struct dmar_pci_notify_info *info, int bus,
				struct acpi_dmar_pci_path *path, int count)
{
	int i;

	if (info->bus != bus)
		goto fallback;
	if (info->level != count)
		goto fallback;

	for (i = 0; i < count; i++) {
		if (path[i].device != info->path[i].device ||
		    path[i].function != info->path[i].function)
			goto fallback;
	}

	return true;

fallback:

	if (count != 1)
		return false;

	i = info->level - 1;
	if (bus              == info->path[i].bus &&
	    path[0].device   == info->path[i].device &&
	    path[0].function == info->path[i].function) {
		pr_info(FW_BUG "RMRR entry for device %02x:%02x.%x is broken - applying workaround\n",
			bus, path[0].device, path[0].function);
		return true;
	}

	return false;
}

/* Return: > 0 if match found, 0 if no match found, < 0 if error happens */
int dmar_insert_dev_scope(struct dmar_pci_notify_info *info,
			  void *start, void*end, u16 segment,
			  struct dmar_dev_scope *devices,
			  int devices_cnt)
{
	int i, level;
	struct device *tmp, *dev = &info->dev->dev;
	struct acpi_dmar_device_scope *scope;
	struct acpi_dmar_pci_path *path;

	if (segment != info->seg)
		return 0;

	for (; start < end; start += scope->length) {
		scope = start;
		if (scope->entry_type != ACPI_DMAR_SCOPE_TYPE_ENDPOINT &&
		    scope->entry_type != ACPI_DMAR_SCOPE_TYPE_BRIDGE)
			continue;

		path = (struct acpi_dmar_pci_path *)(scope + 1);
		level = (scope->length - sizeof(*scope)) / sizeof(*path);
		if (!dmar_match_pci_path(info, scope->bus, path, level))
			continue;

		/*
		 * We expect devices with endpoint scope to have normal PCI
		 * headers, and devices with bridge scope to have bridge PCI
		 * headers.  However PCI NTB devices may be listed in the
		 * DMAR table with bridge scope, even though they have a
		 * normal PCI header.  NTB devices are identified by class
		 * "BRIDGE_OTHER" (0680h) - we don't declare a socpe mismatch
		 * for this special case.
		 */
		if ((scope->entry_type == ACPI_DMAR_SCOPE_TYPE_ENDPOINT &&
		     info->dev->hdr_type != PCI_HEADER_TYPE_NORMAL) ||
		    (scope->entry_type == ACPI_DMAR_SCOPE_TYPE_BRIDGE &&
		     (info->dev->hdr_type == PCI_HEADER_TYPE_NORMAL &&
		      info->dev->class >> 8 != PCI_CLASS_BRIDGE_OTHER))) {
			pr_warn("Device scope type does not match for %s\n",
				pci_name(info->dev));
			return -EINVAL;
		}

		for_each_dev_scope(devices, devices_cnt, i, tmp)
			if (tmp == NULL) {
				devices[i].bus = info->dev->bus->number;
				devices[i].devfn = info->dev->devfn;
				rcu_assign_pointer(devices[i].dev,
						   get_device(dev));
				return 1;
			}
		BUG_ON(i >= devices_cnt);
	}

	return 0;
}

int dmar_remove_dev_scope(struct dmar_pci_notify_info *info, u16 segment,
			  struct dmar_dev_scope *devices, int count)
{
	int index;
	struct device *tmp;

	if (info->seg != segment)
		return 0;

	for_each_active_dev_scope(devices, count, index, tmp)
		if (tmp == &info->dev->dev) {
			RCU_INIT_POINTER(devices[index].dev, NULL);
			synchronize_rcu();
			put_device(tmp);
			return 1;
		}

	return 0;
}

static int dmar_pci_bus_add_dev(struct dmar_pci_notify_info *info)
{
	int ret = 0;
	struct dmar_drhd_unit *dmaru;
	struct acpi_dmar_hardware_unit *drhd;

	for_each_drhd_unit(dmaru) {
		if (dmaru->include_all)
			continue;

		drhd = container_of(dmaru->hdr,
				    struct acpi_dmar_hardware_unit, header);
		ret = dmar_insert_dev_scope(info, (void *)(drhd + 1),
				((void *)drhd) + drhd->header.length,
				dmaru->segment,
				dmaru->devices, dmaru->devices_cnt);
		if (ret)
			break;
	}
	if (ret >= 0)
		ret = dmar_iommu_notify_scope_dev(info);
	if (ret < 0 && dmar_dev_scope_status == 0)
		dmar_dev_scope_status = ret;

	return ret;
}

static void  dmar_pci_bus_del_dev(struct dmar_pci_notify_info *info)
{
	struct dmar_drhd_unit *dmaru;

	for_each_drhd_unit(dmaru)
		if (dmar_remove_dev_scope(info, dmaru->segment,
			dmaru->devices, dmaru->devices_cnt))
			break;
	dmar_iommu_notify_scope_dev(info);
}

static int dmar_pci_bus_notifier(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct pci_dev *pdev = to_pci_dev(data);
	struct dmar_pci_notify_info *info;

	/* Only care about add/remove events for physical functions.
	 * For VFs we actually do the lookup based on the corresponding
	 * PF in device_to_iommu() anyway. */
	if (pdev->is_virtfn)
		return NOTIFY_DONE;
	if (action != BUS_NOTIFY_ADD_DEVICE &&
	    action != BUS_NOTIFY_REMOVED_DEVICE)
		return NOTIFY_DONE;

	info = dmar_alloc_pci_notify_info(pdev, action);
	if (!info)
		return NOTIFY_DONE;

	down_write(&dmar_global_lock);
	if (action == BUS_NOTIFY_ADD_DEVICE)
		dmar_pci_bus_add_dev(info);
	else if (action == BUS_NOTIFY_REMOVED_DEVICE)
		dmar_pci_bus_del_dev(info);
	up_write(&dmar_global_lock);

	dmar_free_pci_notify_info(info);

	return NOTIFY_OK;
}

static struct notifier_block dmar_pci_bus_nb = {
	.notifier_call = dmar_pci_bus_notifier,
	.priority = INT_MIN,
};

static struct dmar_drhd_unit *
dmar_find_dmaru(struct acpi_dmar_hardware_unit *drhd)
{
	struct dmar_drhd_unit *dmaru;

	list_for_each_entry_rcu(dmaru, &dmar_drhd_units, list)
		if (dmaru->segment == drhd->segment &&
		    dmaru->reg_base_addr == drhd->address)
			return dmaru;

	return NULL;
}

/**
 * dmar_parse_one_drhd - parses exactly one DMA remapping hardware definition
 * structure which uniquely represent one DMA remapping hardware unit
 * present in the platform
 */
static int dmar_parse_one_drhd(struct acpi_dmar_header *header, void *arg)
{
	struct acpi_dmar_hardware_unit *drhd;
	struct dmar_drhd_unit *dmaru;
	int ret;

	drhd = (struct acpi_dmar_hardware_unit *)header;
	dmaru = dmar_find_dmaru(drhd);
	if (dmaru)
		goto out;

	dmaru = kzalloc(sizeof(*dmaru) + header->length, GFP_KERNEL);
	if (!dmaru)
		return -ENOMEM;

	/*
	 * If header is allocated from slab by ACPI _DSM method, we need to
	 * copy the content because the memory buffer will be freed on return.
	 */
	dmaru->hdr = (void *)(dmaru + 1);
	memcpy(dmaru->hdr, header, header->length);
	dmaru->reg_base_addr = drhd->address;
	dmaru->segment = drhd->segment;
	dmaru->include_all = drhd->flags & 0x1; /* BIT0: INCLUDE_ALL */
	dmaru->devices = dmar_alloc_dev_scope((void *)(drhd + 1),
					      ((void *)drhd) + drhd->header.length,
					      &dmaru->devices_cnt);
	if (dmaru->devices_cnt && dmaru->devices == NULL) {
		kfree(dmaru);
		return -ENOMEM;
	}

	ret = alloc_iommu(dmaru);
	if (ret) {
		dmar_free_dev_scope(&dmaru->devices,
				    &dmaru->devices_cnt);
		kfree(dmaru);
		return ret;
	}
	dmar_register_drhd_unit(dmaru);

out:
	if (arg)
		(*(int *)arg)++;

	return 0;
}

static void dmar_free_drhd(struct dmar_drhd_unit *dmaru)
{
	if (dmaru->devices && dmaru->devices_cnt)
		dmar_free_dev_scope(&dmaru->devices, &dmaru->devices_cnt);
	if (dmaru->iommu)
		free_iommu(dmaru->iommu);
	kfree(dmaru);
}

static int __init dmar_parse_one_andd(struct acpi_dmar_header *header,
				      void *arg)
{
	struct acpi_dmar_andd *andd = (void *)header;

	/* Check for NUL termination within the designated length */
	if (strnlen(andd->device_name, header->length - 8) == header->length - 8) {
		pr_warn(FW_BUG
			   "Your BIOS is broken; ANDD object name is not NUL-terminated\n"
			   "BIOS vendor: %s; Ver: %s; Product Version: %s\n",
			   dmi_get_system_info(DMI_BIOS_VENDOR),
			   dmi_get_system_info(DMI_BIOS_VERSION),
			   dmi_get_system_info(DMI_PRODUCT_VERSION));
		add_taint(TAINT_FIRMWARE_WORKAROUND, LOCKDEP_STILL_OK);
		return -EINVAL;
	}
	pr_info("ANDD device: %x name: %s\n", andd->device_number,
		andd->device_name);

	return 0;
}

#ifdef CONFIG_ACPI_NUMA
static int dmar_parse_one_rhsa(struct acpi_dmar_header *header, void *arg)
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
	pr_warn(FW_BUG
		"Your BIOS is broken; RHSA refers to non-existent DMAR unit at %llx\n"
		"BIOS vendor: %s; Ver: %s; Product Version: %s\n",
		drhd->reg_base_addr,
		dmi_get_system_info(DMI_BIOS_VENDOR),
		dmi_get_system_info(DMI_BIOS_VERSION),
		dmi_get_system_info(DMI_PRODUCT_VERSION));
	add_taint(TAINT_FIRMWARE_WORKAROUND, LOCKDEP_STILL_OK);

	return 0;
}
#else
#define	dmar_parse_one_rhsa		dmar_res_noop
#endif

static void
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
	case ACPI_DMAR_TYPE_ROOT_ATS:
		atsr = container_of(header, struct acpi_dmar_atsr, header);
		pr_info("ATSR flags: %#x\n", atsr->flags);
		break;
	case ACPI_DMAR_TYPE_HARDWARE_AFFINITY:
		rhsa = container_of(header, struct acpi_dmar_rhsa, header);
		pr_info("RHSA base: %#016Lx proximity domain: %#x\n",
		       (unsigned long long)rhsa->base_address,
		       rhsa->proximity_domain);
		break;
	case ACPI_DMAR_TYPE_NAMESPACE:
		/* We don't print this here because we need to sanity-check
		   it first. So print it in dmar_parse_one_andd() instead. */
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
	status = acpi_get_table(ACPI_SIG_DMAR, 0, &dmar_tbl);

	if (ACPI_SUCCESS(status) && !dmar_tbl) {
		pr_warn("Unable to map DMAR\n");
		status = AE_NOT_FOUND;
	}

	return ACPI_SUCCESS(status) ? 0 : -ENOENT;
}

static int dmar_walk_remapping_entries(struct acpi_dmar_header *start,
				       size_t len, struct dmar_res_callback *cb)
{
	struct acpi_dmar_header *iter, *next;
	struct acpi_dmar_header *end = ((void *)start) + len;

	for (iter = start; iter < end; iter = next) {
		next = (void *)iter + iter->length;
		if (iter->length == 0) {
			/* Avoid looping forever on bad ACPI tables */
			pr_debug(FW_BUG "Invalid 0-length structure\n");
			break;
		} else if (next > end) {
			/* Avoid passing table end */
			pr_warn(FW_BUG "Record passes table end\n");
			return -EINVAL;
		}

		if (cb->print_entry)
			dmar_table_print_dmar_entry(iter);

		if (iter->type >= ACPI_DMAR_TYPE_RESERVED) {
			/* continue for forward compatibility */
			pr_debug("Unknown DMAR structure type %d\n",
				 iter->type);
		} else if (cb->cb[iter->type]) {
			int ret;

			ret = cb->cb[iter->type](iter, cb->arg[iter->type]);
			if (ret)
				return ret;
		} else if (!cb->ignore_unhandled) {
			pr_warn("No handler for DMAR structure type %d\n",
				iter->type);
			return -EINVAL;
		}
	}

	return 0;
}

static inline int dmar_walk_dmar_table(struct acpi_table_dmar *dmar,
				       struct dmar_res_callback *cb)
{
	return dmar_walk_remapping_entries((void *)(dmar + 1),
			dmar->header.length - sizeof(*dmar), cb);
}

/**
 * parse_dmar_table - parses the DMA reporting table
 */
static int __init
parse_dmar_table(void)
{
	struct acpi_table_dmar *dmar;
	int drhd_count = 0;
	int ret;
	struct dmar_res_callback cb = {
		.print_entry = true,
		.ignore_unhandled = true,
		.arg[ACPI_DMAR_TYPE_HARDWARE_UNIT] = &drhd_count,
		.cb[ACPI_DMAR_TYPE_HARDWARE_UNIT] = &dmar_parse_one_drhd,
		.cb[ACPI_DMAR_TYPE_RESERVED_MEMORY] = &dmar_parse_one_rmrr,
		.cb[ACPI_DMAR_TYPE_ROOT_ATS] = &dmar_parse_one_atsr,
		.cb[ACPI_DMAR_TYPE_HARDWARE_AFFINITY] = &dmar_parse_one_rhsa,
		.cb[ACPI_DMAR_TYPE_NAMESPACE] = &dmar_parse_one_andd,
	};

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
	ret = dmar_walk_dmar_table(dmar, &cb);
	if (ret == 0 && drhd_count == 0)
		pr_warn(FW_BUG "No DRHD structure found in DMAR table\n");

	return ret;
}

static int dmar_pci_device_match(struct dmar_dev_scope devices[],
				 int cnt, struct pci_dev *dev)
{
	int index;
	struct device *tmp;

	while (dev) {
		for_each_active_dev_scope(devices, cnt, index, tmp)
			if (dev_is_pci(tmp) && dev == to_pci_dev(tmp))
				return 1;

		/* Check our parent */
		dev = dev->bus->self;
	}

	return 0;
}

struct dmar_drhd_unit *
dmar_find_matched_drhd_unit(struct pci_dev *dev)
{
	struct dmar_drhd_unit *dmaru;
	struct acpi_dmar_hardware_unit *drhd;

	dev = pci_physfn(dev);

	rcu_read_lock();
	for_each_drhd_unit(dmaru) {
		drhd = container_of(dmaru->hdr,
				    struct acpi_dmar_hardware_unit,
				    header);

		if (dmaru->include_all &&
		    drhd->segment == pci_domain_nr(dev->bus))
			goto out;

		if (dmar_pci_device_match(dmaru->devices,
					  dmaru->devices_cnt, dev))
			goto out;
	}
	dmaru = NULL;
out:
	rcu_read_unlock();

	return dmaru;
}

static void __init dmar_acpi_insert_dev_scope(u8 device_number,
					      struct acpi_device *adev)
{
	struct dmar_drhd_unit *dmaru;
	struct acpi_dmar_hardware_unit *drhd;
	struct acpi_dmar_device_scope *scope;
	struct device *tmp;
	int i;
	struct acpi_dmar_pci_path *path;

	for_each_drhd_unit(dmaru) {
		drhd = container_of(dmaru->hdr,
				    struct acpi_dmar_hardware_unit,
				    header);

		for (scope = (void *)(drhd + 1);
		     (unsigned long)scope < ((unsigned long)drhd) + drhd->header.length;
		     scope = ((void *)scope) + scope->length) {
			if (scope->entry_type != ACPI_DMAR_SCOPE_TYPE_NAMESPACE)
				continue;
			if (scope->enumeration_id != device_number)
				continue;

			path = (void *)(scope + 1);
			pr_info("ACPI device \"%s\" under DMAR at %llx as %02x:%02x.%d\n",
				dev_name(&adev->dev), dmaru->reg_base_addr,
				scope->bus, path->device, path->function);
			for_each_dev_scope(dmaru->devices, dmaru->devices_cnt, i, tmp)
				if (tmp == NULL) {
					dmaru->devices[i].bus = scope->bus;
					dmaru->devices[i].devfn = PCI_DEVFN(path->device,
									    path->function);
					rcu_assign_pointer(dmaru->devices[i].dev,
							   get_device(&adev->dev));
					return;
				}
			BUG_ON(i >= dmaru->devices_cnt);
		}
	}
	pr_warn("No IOMMU scope found for ANDD enumeration ID %d (%s)\n",
		device_number, dev_name(&adev->dev));
}

static int __init dmar_acpi_dev_scope_init(void)
{
	struct acpi_dmar_andd *andd;

	if (dmar_tbl == NULL)
		return -ENODEV;

	for (andd = (void *)dmar_tbl + sizeof(struct acpi_table_dmar);
	     ((unsigned long)andd) < ((unsigned long)dmar_tbl) + dmar_tbl->length;
	     andd = ((void *)andd) + andd->header.length) {
		if (andd->header.type == ACPI_DMAR_TYPE_NAMESPACE) {
			acpi_handle h;
			struct acpi_device *adev;

			if (!ACPI_SUCCESS(acpi_get_handle(ACPI_ROOT_OBJECT,
							  andd->device_name,
							  &h))) {
				pr_err("Failed to find handle for ACPI object %s\n",
				       andd->device_name);
				continue;
			}
			if (acpi_bus_get_device(h, &adev)) {
				pr_err("Failed to get device for ACPI object %s\n",
				       andd->device_name);
				continue;
			}
			dmar_acpi_insert_dev_scope(andd->device_number, adev);
		}
	}
	return 0;
}

int __init dmar_dev_scope_init(void)
{
	struct pci_dev *dev = NULL;
	struct dmar_pci_notify_info *info;

	if (dmar_dev_scope_status != 1)
		return dmar_dev_scope_status;

	if (list_empty(&dmar_drhd_units)) {
		dmar_dev_scope_status = -ENODEV;
	} else {
		dmar_dev_scope_status = 0;

		dmar_acpi_dev_scope_init();

		for_each_pci_dev(dev) {
			if (dev->is_virtfn)
				continue;

			info = dmar_alloc_pci_notify_info(dev,
					BUS_NOTIFY_ADD_DEVICE);
			if (!info) {
				return dmar_dev_scope_status;
			} else {
				dmar_pci_bus_add_dev(info);
				dmar_free_pci_notify_info(info);
			}
		}
	}

	return dmar_dev_scope_status;
}

void __init dmar_register_bus_notifier(void)
{
	bus_register_notifier(&pci_bus_type, &dmar_pci_bus_nb);
}


int __init dmar_table_init(void)
{
	static int dmar_table_initialized;
	int ret;

	if (dmar_table_initialized == 0) {
		ret = parse_dmar_table();
		if (ret < 0) {
			if (ret != -ENODEV)
				pr_info("Parse DMAR table failure.\n");
		} else  if (list_empty(&dmar_drhd_units)) {
			pr_info("No DMAR devices found\n");
			ret = -ENODEV;
		}

		if (ret < 0)
			dmar_table_initialized = ret;
		else
			dmar_table_initialized = 1;
	}

	return dmar_table_initialized < 0 ? dmar_table_initialized : 0;
}

static void warn_invalid_dmar(u64 addr, const char *message)
{
	pr_warn_once(FW_BUG
		"Your BIOS is broken; DMAR reported at address %llx%s!\n"
		"BIOS vendor: %s; Ver: %s; Product Version: %s\n",
		addr, message,
		dmi_get_system_info(DMI_BIOS_VENDOR),
		dmi_get_system_info(DMI_BIOS_VERSION),
		dmi_get_system_info(DMI_PRODUCT_VERSION));
	add_taint(TAINT_FIRMWARE_WORKAROUND, LOCKDEP_STILL_OK);
}

static int __ref
dmar_validate_one_drhd(struct acpi_dmar_header *entry, void *arg)
{
	struct acpi_dmar_hardware_unit *drhd;
	void __iomem *addr;
	u64 cap, ecap;

	drhd = (void *)entry;
	if (!drhd->address) {
		warn_invalid_dmar(0, "");
		return -EINVAL;
	}

	if (arg)
		addr = ioremap(drhd->address, VTD_PAGE_SIZE);
	else
		addr = early_ioremap(drhd->address, VTD_PAGE_SIZE);
	if (!addr) {
		pr_warn("Can't validate DRHD address: %llx\n", drhd->address);
		return -EINVAL;
	}

	cap = dmar_readq(addr + DMAR_CAP_REG);
	ecap = dmar_readq(addr + DMAR_ECAP_REG);

	if (arg)
		iounmap(addr);
	else
		early_iounmap(addr, VTD_PAGE_SIZE);

	if (cap == (uint64_t)-1 && ecap == (uint64_t)-1) {
		warn_invalid_dmar(drhd->address, " returns all ones");
		return -EINVAL;
	}

	return 0;
}

int __init detect_intel_iommu(void)
{
	int ret;
	struct dmar_res_callback validate_drhd_cb = {
		.cb[ACPI_DMAR_TYPE_HARDWARE_UNIT] = &dmar_validate_one_drhd,
		.ignore_unhandled = true,
	};

	down_write(&dmar_global_lock);
	ret = dmar_table_detect();
	if (!ret)
		ret = dmar_walk_dmar_table((struct acpi_table_dmar *)dmar_tbl,
					   &validate_drhd_cb);
	if (!ret && !no_iommu && !iommu_detected && !dmar_disabled) {
		iommu_detected = 1;
		/* Make sure ACS will be enabled */
		pci_request_acs();
	}

#ifdef CONFIG_X86
	if (!ret)
		x86_init.iommu.iommu_init = intel_iommu_init;
#endif

	if (dmar_tbl) {
		acpi_put_table(dmar_tbl);
		dmar_tbl = NULL;
	}
	up_write(&dmar_global_lock);

	return ret ? ret : 1;
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
		pr_err("Can't reserve memory\n");
		err = -EBUSY;
		goto out;
	}

	iommu->reg = ioremap(iommu->reg_phys, iommu->reg_size);
	if (!iommu->reg) {
		pr_err("Can't map the region\n");
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
			pr_err("Can't reserve memory\n");
			err = -EBUSY;
			goto out;
		}
		iommu->reg = ioremap(iommu->reg_phys, iommu->reg_size);
		if (!iommu->reg) {
			pr_err("Can't map the region\n");
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

static int dmar_alloc_seq_id(struct intel_iommu *iommu)
{
	iommu->seq_id = find_first_zero_bit(dmar_seq_ids,
					    DMAR_UNITS_SUPPORTED);
	if (iommu->seq_id >= DMAR_UNITS_SUPPORTED) {
		iommu->seq_id = -1;
	} else {
		set_bit(iommu->seq_id, dmar_seq_ids);
		sprintf(iommu->name, "dmar%d", iommu->seq_id);
	}

	return iommu->seq_id;
}

static void dmar_free_seq_id(struct intel_iommu *iommu)
{
	if (iommu->seq_id >= 0) {
		clear_bit(iommu->seq_id, dmar_seq_ids);
		iommu->seq_id = -1;
	}
}

static int alloc_iommu(struct dmar_drhd_unit *drhd)
{
	struct intel_iommu *iommu;
	u32 ver, sts;
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

	if (dmar_alloc_seq_id(iommu) < 0) {
		pr_err("Failed to allocate seq_id\n");
		err = -ENOSPC;
		goto error;
	}

	err = map_iommu(iommu, drhd->reg_base_addr);
	if (err) {
		pr_err("Failed to map %s\n", iommu->name);
		goto error_free_seq_id;
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
	iommu->segment = drhd->segment;

	iommu->node = -1;

	ver = readl(iommu->reg + DMAR_VER_REG);
	pr_info("%s: reg_base_addr %llx ver %d:%d cap %llx ecap %llx\n",
		iommu->name,
		(unsigned long long)drhd->reg_base_addr,
		DMAR_VER_MAJOR(ver), DMAR_VER_MINOR(ver),
		(unsigned long long)iommu->cap,
		(unsigned long long)iommu->ecap);

	/* Reflect status in gcmd */
	sts = readl(iommu->reg + DMAR_GSTS_REG);
	if (sts & DMA_GSTS_IRES)
		iommu->gcmd |= DMA_GCMD_IRE;
	if (sts & DMA_GSTS_TES)
		iommu->gcmd |= DMA_GCMD_TE;
	if (sts & DMA_GSTS_QIES)
		iommu->gcmd |= DMA_GCMD_QIE;

	raw_spin_lock_init(&iommu->register_lock);

	if (intel_iommu_enabled) {
		err = iommu_device_sysfs_add(&iommu->iommu, NULL,
					     intel_iommu_groups,
					     "%s", iommu->name);
		if (err)
			goto err_unmap;

		iommu_device_set_ops(&iommu->iommu, &intel_iommu_ops);

		err = iommu_device_register(&iommu->iommu);
		if (err)
			goto err_unmap;
	}

	drhd->iommu = iommu;

	return 0;

err_unmap:
	unmap_iommu(iommu);
error_free_seq_id:
	dmar_free_seq_id(iommu);
error:
	kfree(iommu);
	return err;
}

static void free_iommu(struct intel_iommu *iommu)
{
	if (intel_iommu_enabled) {
		iommu_device_unregister(&iommu->iommu);
		iommu_device_sysfs_remove(&iommu->iommu);
	}

	if (iommu->irq) {
		if (iommu->pr_irq) {
			free_irq(iommu->pr_irq, iommu);
			dmar_free_hwirq(iommu->pr_irq);
			iommu->pr_irq = 0;
		}
		free_irq(iommu->irq, iommu);
		dmar_free_hwirq(iommu->irq);
		iommu->irq = 0;
	}

	if (iommu->qi) {
		free_page((unsigned long)iommu->qi->desc);
		kfree(iommu->qi->desc_status);
		kfree(iommu->qi);
	}

	if (iommu->reg)
		unmap_iommu(iommu);

	dmar_free_seq_id(iommu);
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

void qi_flush_dev_iotlb(struct intel_iommu *iommu, u16 sid, u16 pfsid,
			u16 qdep, u64 addr, unsigned mask)
{
	struct qi_desc desc;

	if (mask) {
		addr |= (1ULL << (VTD_PAGE_SHIFT + mask - 1)) - 1;
		desc.high = QI_DEV_IOTLB_ADDR(addr) | QI_DEV_IOTLB_SIZE;
	} else
		desc.high = QI_DEV_IOTLB_ADDR(addr);

	if (qdep >= QI_DEV_IOTLB_MAX_INVS)
		qdep = 0;

	desc.low = QI_DEV_IOTLB_SID(sid) | QI_DEV_IOTLB_QDEP(qdep) |
		   QI_DIOTLB_TYPE | QI_DEV_IOTLB_PFSID(pfsid);

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

	sts =  readl(iommu->reg + DMAR_GSTS_REG);
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
		iommu->qi = NULL;
		return -ENOMEM;
	}

	qi->desc = page_address(desc_page);

	qi->desc_status = kcalloc(QI_LENGTH, sizeof(int), GFP_ATOMIC);
	if (!qi->desc_status) {
		free_page((unsigned long) qi->desc);
		kfree(qi);
		iommu->qi = NULL;
		return -ENOMEM;
	}

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

static const char *dmar_get_fault_reason(u8 fault_reason, int *fault_type)
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


static inline int dmar_msi_reg(struct intel_iommu *iommu, int irq)
{
	if (iommu->irq == irq)
		return DMAR_FECTL_REG;
	else if (iommu->pr_irq == irq)
		return DMAR_PECTL_REG;
	else
		BUG();
}

void dmar_msi_unmask(struct irq_data *data)
{
	struct intel_iommu *iommu = irq_data_get_irq_handler_data(data);
	int reg = dmar_msi_reg(iommu, data->irq);
	unsigned long flag;

	/* unmask it */
	raw_spin_lock_irqsave(&iommu->register_lock, flag);
	writel(0, iommu->reg + reg);
	/* Read a reg to force flush the post write */
	readl(iommu->reg + reg);
	raw_spin_unlock_irqrestore(&iommu->register_lock, flag);
}

void dmar_msi_mask(struct irq_data *data)
{
	struct intel_iommu *iommu = irq_data_get_irq_handler_data(data);
	int reg = dmar_msi_reg(iommu, data->irq);
	unsigned long flag;

	/* mask it */
	raw_spin_lock_irqsave(&iommu->register_lock, flag);
	writel(DMA_FECTL_IM, iommu->reg + reg);
	/* Read a reg to force flush the post write */
	readl(iommu->reg + reg);
	raw_spin_unlock_irqrestore(&iommu->register_lock, flag);
}

void dmar_msi_write(int irq, struct msi_msg *msg)
{
	struct intel_iommu *iommu = irq_get_handler_data(irq);
	int reg = dmar_msi_reg(iommu, irq);
	unsigned long flag;

	raw_spin_lock_irqsave(&iommu->register_lock, flag);
	writel(msg->data, iommu->reg + reg + 4);
	writel(msg->address_lo, iommu->reg + reg + 8);
	writel(msg->address_hi, iommu->reg + reg + 12);
	raw_spin_unlock_irqrestore(&iommu->register_lock, flag);
}

void dmar_msi_read(int irq, struct msi_msg *msg)
{
	struct intel_iommu *iommu = irq_get_handler_data(irq);
	int reg = dmar_msi_reg(iommu, irq);
	unsigned long flag;

	raw_spin_lock_irqsave(&iommu->register_lock, flag);
	msg->data = readl(iommu->reg + reg + 4);
	msg->address_lo = readl(iommu->reg + reg + 8);
	msg->address_hi = readl(iommu->reg + reg + 12);
	raw_spin_unlock_irqrestore(&iommu->register_lock, flag);
}

static int dmar_fault_do_one(struct intel_iommu *iommu, int type,
		u8 fault_reason, u16 source_id, unsigned long long addr)
{
	const char *reason;
	int fault_type;

	reason = dmar_get_fault_reason(fault_reason, &fault_type);

	if (fault_type == INTR_REMAP)
		pr_err("[INTR-REMAP] Request device [%02x:%02x.%d] fault index %llx [fault reason %02d] %s\n",
			source_id >> 8, PCI_SLOT(source_id & 0xFF),
			PCI_FUNC(source_id & 0xFF), addr >> 48,
			fault_reason, reason);
	else
		pr_err("[%s] Request device [%02x:%02x.%d] fault addr %llx [fault reason %02d] %s\n",
		       type ? "DMA Read" : "DMA Write",
		       source_id >> 8, PCI_SLOT(source_id & 0xFF),
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
	static DEFINE_RATELIMIT_STATE(rs,
				      DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	raw_spin_lock_irqsave(&iommu->register_lock, flag);
	fault_status = readl(iommu->reg + DMAR_FSTS_REG);
	if (fault_status && __ratelimit(&rs))
		pr_err("DRHD: handling fault status reg %x\n", fault_status);

	/* TBD: ignore advanced fault log currently */
	if (!(fault_status & DMA_FSTS_PPF))
		goto unlock_exit;

	fault_index = dma_fsts_fault_record_index(fault_status);
	reg = cap_fault_reg_offset(iommu->cap);
	while (1) {
		/* Disable printing, simply clear the fault when ratelimited */
		bool ratelimited = !__ratelimit(&rs);
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

		if (!ratelimited) {
			fault_reason = dma_frcd_fault_reason(data);
			type = dma_frcd_type(data);

			data = readl(iommu->reg + reg +
				     fault_index * PRIMARY_FAULT_REG_LEN + 8);
			source_id = dma_frcd_source_id(data);

			guest_addr = dmar_readq(iommu->reg + reg +
					fault_index * PRIMARY_FAULT_REG_LEN);
			guest_addr = dma_frcd_page_addr(guest_addr);
		}

		/* clear the fault */
		writel(DMA_FRCD_F, iommu->reg + reg +
			fault_index * PRIMARY_FAULT_REG_LEN + 12);

		raw_spin_unlock_irqrestore(&iommu->register_lock, flag);

		if (!ratelimited)
			dmar_fault_do_one(iommu, type, fault_reason,
					  source_id, guest_addr);

		fault_index++;
		if (fault_index >= cap_num_fault_regs(iommu->cap))
			fault_index = 0;
		raw_spin_lock_irqsave(&iommu->register_lock, flag);
	}

	writel(DMA_FSTS_PFO | DMA_FSTS_PPF | DMA_FSTS_PRO,
	       iommu->reg + DMAR_FSTS_REG);

unlock_exit:
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

	irq = dmar_alloc_hwirq(iommu->seq_id, iommu->node, iommu);
	if (irq > 0) {
		iommu->irq = irq;
	} else {
		pr_err("No free IRQ vectors\n");
		return -EINVAL;
	}

	ret = request_irq(irq, dmar_fault, IRQF_NO_THREAD, iommu->name, iommu);
	if (ret)
		pr_err("Can't request irq\n");
	return ret;
}

int __init enable_drhd_fault_handling(void)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu;

	/*
	 * Enable fault control interrupt.
	 */
	for_each_iommu(iommu, drhd) {
		u32 fault_status;
		int ret = dmar_set_interrupt(iommu);

		if (ret) {
			pr_err("DRHD %Lx: failed to enable fault, interrupt, ret %d\n",
			       (unsigned long long)drhd->reg_base_addr, ret);
			return -1;
		}

		/*
		 * Clear any previous faults.
		 */
		dmar_fault(iommu->irq, iommu);
		fault_status = readl(iommu->reg + DMAR_FSTS_REG);
		writel(fault_status, iommu->reg + DMAR_FSTS_REG);
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

/* Check whether DMAR units are in use */
static inline bool dmar_in_use(void)
{
	return irq_remapping_enabled || intel_iommu_enabled;
}

static int __init dmar_free_unused_resources(void)
{
	struct dmar_drhd_unit *dmaru, *dmaru_n;

	if (dmar_in_use())
		return 0;

	if (dmar_dev_scope_status != 1 && !list_empty(&dmar_drhd_units))
		bus_unregister_notifier(&pci_bus_type, &dmar_pci_bus_nb);

	down_write(&dmar_global_lock);
	list_for_each_entry_safe(dmaru, dmaru_n, &dmar_drhd_units, list) {
		list_del(&dmaru->list);
		dmar_free_drhd(dmaru);
	}
	up_write(&dmar_global_lock);

	return 0;
}

late_initcall(dmar_free_unused_resources);
IOMMU_INIT_POST(detect_intel_iommu);

/*
 * DMAR Hotplug Support
 * For more details, please refer to Intel(R) Virtualization Technology
 * for Directed-IO Architecture Specifiction, Rev 2.2, Section 8.8
 * "Remapping Hardware Unit Hot Plug".
 */
static guid_t dmar_hp_guid =
	GUID_INIT(0xD8C1A3A6, 0xBE9B, 0x4C9B,
		  0x91, 0xBF, 0xC3, 0xCB, 0x81, 0xFC, 0x5D, 0xAF);

/*
 * Currently there's only one revision and BIOS will not check the revision id,
 * so use 0 for safety.
 */
#define	DMAR_DSM_REV_ID			0
#define	DMAR_DSM_FUNC_DRHD		1
#define	DMAR_DSM_FUNC_ATSR		2
#define	DMAR_DSM_FUNC_RHSA		3

static inline bool dmar_detect_dsm(acpi_handle handle, int func)
{
	return acpi_check_dsm(handle, &dmar_hp_guid, DMAR_DSM_REV_ID, 1 << func);
}

static int dmar_walk_dsm_resource(acpi_handle handle, int func,
				  dmar_res_handler_t handler, void *arg)
{
	int ret = -ENODEV;
	union acpi_object *obj;
	struct acpi_dmar_header *start;
	struct dmar_res_callback callback;
	static int res_type[] = {
		[DMAR_DSM_FUNC_DRHD] = ACPI_DMAR_TYPE_HARDWARE_UNIT,
		[DMAR_DSM_FUNC_ATSR] = ACPI_DMAR_TYPE_ROOT_ATS,
		[DMAR_DSM_FUNC_RHSA] = ACPI_DMAR_TYPE_HARDWARE_AFFINITY,
	};

	if (!dmar_detect_dsm(handle, func))
		return 0;

	obj = acpi_evaluate_dsm_typed(handle, &dmar_hp_guid, DMAR_DSM_REV_ID,
				      func, NULL, ACPI_TYPE_BUFFER);
	if (!obj)
		return -ENODEV;

	memset(&callback, 0, sizeof(callback));
	callback.cb[res_type[func]] = handler;
	callback.arg[res_type[func]] = arg;
	start = (struct acpi_dmar_header *)obj->buffer.pointer;
	ret = dmar_walk_remapping_entries(start, obj->buffer.length, &callback);

	ACPI_FREE(obj);

	return ret;
}

static int dmar_hp_add_drhd(struct acpi_dmar_header *header, void *arg)
{
	int ret;
	struct dmar_drhd_unit *dmaru;

	dmaru = dmar_find_dmaru((struct acpi_dmar_hardware_unit *)header);
	if (!dmaru)
		return -ENODEV;

	ret = dmar_ir_hotplug(dmaru, true);
	if (ret == 0)
		ret = dmar_iommu_hotplug(dmaru, true);

	return ret;
}

static int dmar_hp_remove_drhd(struct acpi_dmar_header *header, void *arg)
{
	int i, ret;
	struct device *dev;
	struct dmar_drhd_unit *dmaru;

	dmaru = dmar_find_dmaru((struct acpi_dmar_hardware_unit *)header);
	if (!dmaru)
		return 0;

	/*
	 * All PCI devices managed by this unit should have been destroyed.
	 */
	if (!dmaru->include_all && dmaru->devices && dmaru->devices_cnt) {
		for_each_active_dev_scope(dmaru->devices,
					  dmaru->devices_cnt, i, dev)
			return -EBUSY;
	}

	ret = dmar_ir_hotplug(dmaru, false);
	if (ret == 0)
		ret = dmar_iommu_hotplug(dmaru, false);

	return ret;
}

static int dmar_hp_release_drhd(struct acpi_dmar_header *header, void *arg)
{
	struct dmar_drhd_unit *dmaru;

	dmaru = dmar_find_dmaru((struct acpi_dmar_hardware_unit *)header);
	if (dmaru) {
		list_del_rcu(&dmaru->list);
		synchronize_rcu();
		dmar_free_drhd(dmaru);
	}

	return 0;
}

static int dmar_hotplug_insert(acpi_handle handle)
{
	int ret;
	int drhd_count = 0;

	ret = dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_DRHD,
				     &dmar_validate_one_drhd, (void *)1);
	if (ret)
		goto out;

	ret = dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_DRHD,
				     &dmar_parse_one_drhd, (void *)&drhd_count);
	if (ret == 0 && drhd_count == 0) {
		pr_warn(FW_BUG "No DRHD structures in buffer returned by _DSM method\n");
		goto out;
	} else if (ret) {
		goto release_drhd;
	}

	ret = dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_RHSA,
				     &dmar_parse_one_rhsa, NULL);
	if (ret)
		goto release_drhd;

	ret = dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_ATSR,
				     &dmar_parse_one_atsr, NULL);
	if (ret)
		goto release_atsr;

	ret = dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_DRHD,
				     &dmar_hp_add_drhd, NULL);
	if (!ret)
		return 0;

	dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_DRHD,
			       &dmar_hp_remove_drhd, NULL);
release_atsr:
	dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_ATSR,
			       &dmar_release_one_atsr, NULL);
release_drhd:
	dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_DRHD,
			       &dmar_hp_release_drhd, NULL);
out:
	return ret;
}

static int dmar_hotplug_remove(acpi_handle handle)
{
	int ret;

	ret = dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_ATSR,
				     &dmar_check_one_atsr, NULL);
	if (ret)
		return ret;

	ret = dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_DRHD,
				     &dmar_hp_remove_drhd, NULL);
	if (ret == 0) {
		WARN_ON(dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_ATSR,
					       &dmar_release_one_atsr, NULL));
		WARN_ON(dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_DRHD,
					       &dmar_hp_release_drhd, NULL));
	} else {
		dmar_walk_dsm_resource(handle, DMAR_DSM_FUNC_DRHD,
				       &dmar_hp_add_drhd, NULL);
	}

	return ret;
}

static acpi_status dmar_get_dsm_handle(acpi_handle handle, u32 lvl,
				       void *context, void **retval)
{
	acpi_handle *phdl = retval;

	if (dmar_detect_dsm(handle, DMAR_DSM_FUNC_DRHD)) {
		*phdl = handle;
		return AE_CTRL_TERMINATE;
	}

	return AE_OK;
}

static int dmar_device_hotplug(acpi_handle handle, bool insert)
{
	int ret;
	acpi_handle tmp = NULL;
	acpi_status status;

	if (!dmar_in_use())
		return 0;

	if (dmar_detect_dsm(handle, DMAR_DSM_FUNC_DRHD)) {
		tmp = handle;
	} else {
		status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle,
					     ACPI_UINT32_MAX,
					     dmar_get_dsm_handle,
					     NULL, NULL, &tmp);
		if (ACPI_FAILURE(status)) {
			pr_warn("Failed to locate _DSM method.\n");
			return -ENXIO;
		}
	}
	if (tmp == NULL)
		return 0;

	down_write(&dmar_global_lock);
	if (insert)
		ret = dmar_hotplug_insert(tmp);
	else
		ret = dmar_hotplug_remove(tmp);
	up_write(&dmar_global_lock);

	return ret;
}

int dmar_device_add(acpi_handle handle)
{
	return dmar_device_hotplug(handle, true);
}

int dmar_device_remove(acpi_handle handle)
{
	return dmar_device_hotplug(handle, false);
}
