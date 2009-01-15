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

#include <linux/pci.h>
#include <linux/dmar.h>
#include <linux/iova.h>
#include <linux/intel-iommu.h>
#include <linux/timer.h>

#undef PREFIX
#define PREFIX "DMAR:"

/* No locks are needed as DMA remapping hardware unit
 * list is constructed at boot time and hotplug of
 * these units are not supported by the architecture.
 */
LIST_HEAD(dmar_drhd_units);

static struct acpi_table_header * __initdata dmar_tbl;

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
			printk(KERN_WARNING
			PREFIX "Device scope bus [%d] not found\n",
			scope->bus);
			break;
		}
		pdev = pci_get_slot(bus, PCI_DEVFN(path->dev, path->fn));
		if (!pdev) {
			printk(KERN_WARNING PREFIX
			"Device scope device [%04x:%02x:%02x.%02x] not found\n",
				segment, bus->number, path->dev, path->fn);
			break;
		}
		path ++;
		count --;
		bus = pdev->subordinate;
	}
	if (!pdev) {
		printk(KERN_WARNING PREFIX
		"Device scope device [%04x:%02x:%02x.%02x] not found\n",
		segment, scope->bus, path->dev, path->fn);
		*dev = NULL;
		return 0;
	}
	if ((scope->entry_type == ACPI_DMAR_SCOPE_TYPE_ENDPOINT && \
			pdev->subordinate) || (scope->entry_type == \
			ACPI_DMAR_SCOPE_TYPE_BRIDGE && !pdev->subordinate)) {
		pci_dev_put(pdev);
		printk(KERN_WARNING PREFIX
			"Device scope type does not match for %s\n",
			 pci_name(pdev));
		return -EINVAL;
	}
	*dev = pdev;
	return 0;
}

static int __init dmar_parse_dev_scope(void *start, void *end, int *cnt,
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
		else
			printk(KERN_WARNING PREFIX
				"Unsupported device scope\n");
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

	dmaru = kzalloc(sizeof(*dmaru), GFP_KERNEL);
	if (!dmaru)
		return -ENOMEM;

	dmaru->hdr = header;
	drhd = (struct acpi_dmar_hardware_unit *)header;
	dmaru->reg_base_addr = drhd->address;
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

#ifdef CONFIG_DMAR
LIST_HEAD(dmar_rmrr_units);

static void __init dmar_register_rmrr_unit(struct dmar_rmrr_unit *rmrr)
{
	list_add(&rmrr->list, &dmar_rmrr_units);
}


static int __init
dmar_parse_one_rmrr(struct acpi_dmar_header *header)
{
	struct acpi_dmar_reserved_memory *rmrr;
	struct dmar_rmrr_unit *rmrru;

	rmrru = kzalloc(sizeof(*rmrru), GFP_KERNEL);
	if (!rmrru)
		return -ENOMEM;

	rmrru->hdr = header;
	rmrr = (struct acpi_dmar_reserved_memory *)header;
	rmrru->base_address = rmrr->base_address;
	rmrru->end_address = rmrr->end_address;

	dmar_register_rmrr_unit(rmrru);
	return 0;
}

static int __init
rmrr_parse_dev(struct dmar_rmrr_unit *rmrru)
{
	struct acpi_dmar_reserved_memory *rmrr;
	int ret;

	rmrr = (struct acpi_dmar_reserved_memory *) rmrru->hdr;
	ret = dmar_parse_dev_scope((void *)(rmrr + 1),
		((void *)rmrr) + rmrr->header.length,
		&rmrru->devices_cnt, &rmrru->devices, rmrr->segment);

	if (ret || (rmrru->devices_cnt == 0)) {
		list_del(&rmrru->list);
		kfree(rmrru);
	}
	return ret;
}
#endif

static void __init
dmar_table_print_dmar_entry(struct acpi_dmar_header *header)
{
	struct acpi_dmar_hardware_unit *drhd;
	struct acpi_dmar_reserved_memory *rmrr;

	switch (header->type) {
	case ACPI_DMAR_TYPE_HARDWARE_UNIT:
		drhd = (struct acpi_dmar_hardware_unit *)header;
		printk (KERN_INFO PREFIX
			"DRHD (flags: 0x%08x)base: 0x%016Lx\n",
			drhd->flags, (unsigned long long)drhd->address);
		break;
	case ACPI_DMAR_TYPE_RESERVED_MEMORY:
		rmrr = (struct acpi_dmar_reserved_memory *)header;

		printk (KERN_INFO PREFIX
			"RMRR base: 0x%016Lx end: 0x%016Lx\n",
			(unsigned long long)rmrr->base_address,
			(unsigned long long)rmrr->end_address);
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
	status = acpi_get_table(ACPI_SIG_DMAR, 0,
				(struct acpi_table_header **)&dmar_tbl);

	if (ACPI_SUCCESS(status) && !dmar_tbl) {
		printk (KERN_WARNING PREFIX "Unable to map DMAR\n");
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

	dmar = (struct acpi_table_dmar *)dmar_tbl;
	if (!dmar)
		return -ENODEV;

	if (dmar->width < PAGE_SHIFT - 1) {
		printk(KERN_WARNING PREFIX "Invalid DMAR haw\n");
		return -EINVAL;
	}

	printk (KERN_INFO PREFIX "Host address width %d\n",
		dmar->width + 1);

	entry_header = (struct acpi_dmar_header *)(dmar + 1);
	while (((unsigned long)entry_header) <
			(((unsigned long)dmar) + dmar_tbl->length)) {
		dmar_table_print_dmar_entry(entry_header);

		switch (entry_header->type) {
		case ACPI_DMAR_TYPE_HARDWARE_UNIT:
			ret = dmar_parse_one_drhd(entry_header);
			break;
		case ACPI_DMAR_TYPE_RESERVED_MEMORY:
#ifdef CONFIG_DMAR
			ret = dmar_parse_one_rmrr(entry_header);
#endif
			break;
		default:
			printk(KERN_WARNING PREFIX
				"Unknown DMAR structure type\n");
			ret = 0; /* for forward compatibility */
			break;
		}
		if (ret)
			break;

		entry_header = ((void *)entry_header + entry_header->length);
	}
	return ret;
}

int dmar_pci_device_match(struct pci_dev *devices[], int cnt,
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
	struct dmar_drhd_unit *drhd, *drhd_n;
	int ret = -ENODEV;

	list_for_each_entry_safe(drhd, drhd_n, &dmar_drhd_units, list) {
		ret = dmar_parse_dev(drhd);
		if (ret)
			return ret;
	}

#ifdef CONFIG_DMAR
	{
		struct dmar_rmrr_unit *rmrr, *rmrr_n;
		list_for_each_entry_safe(rmrr, rmrr_n, &dmar_rmrr_units, list) {
			ret = rmrr_parse_dev(rmrr);
			if (ret)
				return ret;
		}
	}
#endif

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
			printk(KERN_INFO PREFIX "parse DMAR table failure.\n");
		return ret;
	}

	if (list_empty(&dmar_drhd_units)) {
		printk(KERN_INFO PREFIX "No DMAR devices found\n");
		return -ENODEV;
	}

#ifdef CONFIG_DMAR
	if (list_empty(&dmar_rmrr_units))
		printk(KERN_INFO PREFIX "No RMRR found\n");
#endif

#ifdef CONFIG_INTR_REMAP
	parse_ioapics_under_ir();
#endif
	return 0;
}

void __init detect_intel_iommu(void)
{
	int ret;

	ret = dmar_table_detect();

	{
#ifdef CONFIG_INTR_REMAP
		struct acpi_table_dmar *dmar;
		/*
		 * for now we will disable dma-remapping when interrupt
		 * remapping is enabled.
		 * When support for queued invalidation for IOTLB invalidation
		 * is added, we will not need this any more.
		 */
		dmar = (struct acpi_table_dmar *) dmar_tbl;
		if (ret && cpu_has_x2apic && dmar->flags & 0x1)
			printk(KERN_INFO
			       "Queued invalidation will be enabled to support "
			       "x2apic and Intr-remapping.\n");
#endif
#ifdef CONFIG_DMAR
		if (ret && !no_iommu && !iommu_detected && !swiotlb &&
		    !dmar_disabled)
			iommu_detected = 1;
#endif
	}
	dmar_tbl = NULL;
}


int alloc_iommu(struct dmar_drhd_unit *drhd)
{
	struct intel_iommu *iommu;
	int map_size;
	u32 ver;
	static int iommu_allocated = 0;
	int agaw;

	iommu = kzalloc(sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return -ENOMEM;

	iommu->seq_id = iommu_allocated++;

	iommu->reg = ioremap(drhd->reg_base_addr, VTD_PAGE_SIZE);
	if (!iommu->reg) {
		printk(KERN_ERR "IOMMU: can't map the region\n");
		goto error;
	}
	iommu->cap = dmar_readq(iommu->reg + DMAR_CAP_REG);
	iommu->ecap = dmar_readq(iommu->reg + DMAR_ECAP_REG);

	agaw = iommu_calculate_agaw(iommu);
	if (agaw < 0) {
		printk(KERN_ERR
			"Cannot get a valid agaw for iommu (seq_id = %d)\n",
			iommu->seq_id);
		goto error;
	}
	iommu->agaw = agaw;

	/* the registers might be more than one page */
	map_size = max_t(int, ecap_max_iotlb_offset(iommu->ecap),
		cap_max_fault_reg_offset(iommu->cap));
	map_size = VTD_PAGE_ALIGN(map_size);
	if (map_size > VTD_PAGE_SIZE) {
		iounmap(iommu->reg);
		iommu->reg = ioremap(drhd->reg_base_addr, map_size);
		if (!iommu->reg) {
			printk(KERN_ERR "IOMMU: can't map the region\n");
			goto error;
		}
	}

	ver = readl(iommu->reg + DMAR_VER_REG);
	pr_debug("IOMMU %llx: ver %d:%d cap %llx ecap %llx\n",
		(unsigned long long)drhd->reg_base_addr,
		DMAR_VER_MAJOR(ver), DMAR_VER_MINOR(ver),
		(unsigned long long)iommu->cap,
		(unsigned long long)iommu->ecap);

	spin_lock_init(&iommu->register_lock);

	drhd->iommu = iommu;
	return 0;
error:
	kfree(iommu);
	return -1;
}

void free_iommu(struct intel_iommu *iommu)
{
	if (!iommu)
		return;

#ifdef CONFIG_DMAR
	free_dmar_iommu(iommu);
#endif

	if (iommu->reg)
		iounmap(iommu->reg);
	kfree(iommu);
}

/*
 * Reclaim all the submitted descriptors which have completed its work.
 */
static inline void reclaim_free_desc(struct q_inval *qi)
{
	while (qi->desc_status[qi->free_tail] == QI_DONE) {
		qi->desc_status[qi->free_tail] = QI_FREE;
		qi->free_tail = (qi->free_tail + 1) % QI_LENGTH;
		qi->free_cnt++;
	}
}

/*
 * Submit the queued invalidation descriptor to the remapping
 * hardware unit and wait for its completion.
 */
void qi_submit_sync(struct qi_desc *desc, struct intel_iommu *iommu)
{
	struct q_inval *qi = iommu->qi;
	struct qi_desc *hw, wait_desc;
	int wait_index, index;
	unsigned long flags;

	if (!qi)
		return;

	hw = qi->desc;

	spin_lock_irqsave(&qi->q_lock, flags);
	while (qi->free_cnt < 3) {
		spin_unlock_irqrestore(&qi->q_lock, flags);
		cpu_relax();
		spin_lock_irqsave(&qi->q_lock, flags);
	}

	index = qi->free_head;
	wait_index = (index + 1) % QI_LENGTH;

	qi->desc_status[index] = qi->desc_status[wait_index] = QI_IN_USE;

	hw[index] = *desc;

	wait_desc.low = QI_IWD_STATUS_DATA(2) | QI_IWD_STATUS_WRITE | QI_IWD_TYPE;
	wait_desc.high = virt_to_phys(&qi->desc_status[wait_index]);

	hw[wait_index] = wait_desc;

	__iommu_flush_cache(iommu, &hw[index], sizeof(struct qi_desc));
	__iommu_flush_cache(iommu, &hw[wait_index], sizeof(struct qi_desc));

	qi->free_head = (qi->free_head + 2) % QI_LENGTH;
	qi->free_cnt -= 2;

	spin_lock(&iommu->register_lock);
	/*
	 * update the HW tail register indicating the presence of
	 * new descriptors.
	 */
	writel(qi->free_head << 4, iommu->reg + DMAR_IQT_REG);
	spin_unlock(&iommu->register_lock);

	while (qi->desc_status[wait_index] != QI_DONE) {
		/*
		 * We will leave the interrupts disabled, to prevent interrupt
		 * context to queue another cmd while a cmd is already submitted
		 * and waiting for completion on this cpu. This is to avoid
		 * a deadlock where the interrupt context can wait indefinitely
		 * for free slots in the queue.
		 */
		spin_unlock(&qi->q_lock);
		cpu_relax();
		spin_lock(&qi->q_lock);
	}

	qi->desc_status[index] = QI_DONE;

	reclaim_free_desc(qi);
	spin_unlock_irqrestore(&qi->q_lock, flags);
}

/*
 * Flush the global interrupt entry cache.
 */
void qi_global_iec(struct intel_iommu *iommu)
{
	struct qi_desc desc;

	desc.low = QI_IEC_TYPE;
	desc.high = 0;

	qi_submit_sync(&desc, iommu);
}

int qi_flush_context(struct intel_iommu *iommu, u16 did, u16 sid, u8 fm,
		     u64 type, int non_present_entry_flush)
{

	struct qi_desc desc;

	if (non_present_entry_flush) {
		if (!cap_caching_mode(iommu->cap))
			return 1;
		else
			did = 0;
	}

	desc.low = QI_CC_FM(fm) | QI_CC_SID(sid) | QI_CC_DID(did)
			| QI_CC_GRAN(type) | QI_CC_TYPE;
	desc.high = 0;

	qi_submit_sync(&desc, iommu);

	return 0;

}

int qi_flush_iotlb(struct intel_iommu *iommu, u16 did, u64 addr,
		   unsigned int size_order, u64 type,
		   int non_present_entry_flush)
{
	u8 dw = 0, dr = 0;

	struct qi_desc desc;
	int ih = 0;

	if (non_present_entry_flush) {
		if (!cap_caching_mode(iommu->cap))
			return 1;
		else
			did = 0;
	}

	if (cap_write_drain(iommu->cap))
		dw = 1;

	if (cap_read_drain(iommu->cap))
		dr = 1;

	desc.low = QI_IOTLB_DID(did) | QI_IOTLB_DR(dr) | QI_IOTLB_DW(dw)
		| QI_IOTLB_GRAN(type) | QI_IOTLB_TYPE;
	desc.high = QI_IOTLB_ADDR(addr) | QI_IOTLB_IH(ih)
		| QI_IOTLB_AM(size_order);

	qi_submit_sync(&desc, iommu);

	return 0;

}

/*
 * Enable Queued Invalidation interface. This is a must to support
 * interrupt-remapping. Also used by DMA-remapping, which replaces
 * register based IOTLB invalidation.
 */
int dmar_enable_qi(struct intel_iommu *iommu)
{
	u32 cmd, sts;
	unsigned long flags;
	struct q_inval *qi;

	if (!ecap_qis(iommu->ecap))
		return -ENOENT;

	/*
	 * queued invalidation is already setup and enabled.
	 */
	if (iommu->qi)
		return 0;

	iommu->qi = kmalloc(sizeof(*qi), GFP_KERNEL);
	if (!iommu->qi)
		return -ENOMEM;

	qi = iommu->qi;

	qi->desc = (void *)(get_zeroed_page(GFP_KERNEL));
	if (!qi->desc) {
		kfree(qi);
		iommu->qi = 0;
		return -ENOMEM;
	}

	qi->desc_status = kmalloc(QI_LENGTH * sizeof(int), GFP_KERNEL);
	if (!qi->desc_status) {
		free_page((unsigned long) qi->desc);
		kfree(qi);
		iommu->qi = 0;
		return -ENOMEM;
	}

	qi->free_head = qi->free_tail = 0;
	qi->free_cnt = QI_LENGTH;

	spin_lock_init(&qi->q_lock);

	spin_lock_irqsave(&iommu->register_lock, flags);
	/* write zero to the tail reg */
	writel(0, iommu->reg + DMAR_IQT_REG);

	dmar_writeq(iommu->reg + DMAR_IQA_REG, virt_to_phys(qi->desc));

	cmd = iommu->gcmd | DMA_GCMD_QIE;
	iommu->gcmd |= DMA_GCMD_QIE;
	writel(cmd, iommu->reg + DMAR_GCMD_REG);

	/* Make sure hardware complete it */
	IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG, readl, (sts & DMA_GSTS_QIES), sts);
	spin_unlock_irqrestore(&iommu->register_lock, flags);

	return 0;
}
