// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2012
 *
 * Author(s):
 *   Jan Glauber <jang@linux.vnet.ibm.com>
 *
 * The System z PCI code is a rewrite from a prototype by
 * the following people (Kudoz!):
 *   Alexander Schmidt
 *   Christoph Raisch
 *   Hannes Hering
 *   Hoang-Nam Nguyen
 *   Jan-Bernd Themann
 *   Stefan Roscher
 *   Thomas Klein
 */

#define KMSG_COMPONENT "zpci"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/jump_label.h>
#include <linux/pci.h>
#include <linux/printk.h>

#include <asm/isc.h>
#include <asm/airq.h>
#include <asm/facility.h>
#include <asm/pci_insn.h>
#include <asm/pci_clp.h>
#include <asm/pci_dma.h>

#include "pci_bus.h"
#include "pci_iov.h"

/* list of all detected zpci devices */
static LIST_HEAD(zpci_list);
static DEFINE_SPINLOCK(zpci_list_lock);

static DECLARE_BITMAP(zpci_domain, ZPCI_DOMAIN_BITMAP_SIZE);
static DEFINE_SPINLOCK(zpci_domain_lock);

#define ZPCI_IOMAP_ENTRIES						\
	min(((unsigned long) ZPCI_NR_DEVICES * PCI_STD_NUM_BARS / 2),	\
	    ZPCI_IOMAP_MAX_ENTRIES)

unsigned int s390_pci_no_rid;

static DEFINE_SPINLOCK(zpci_iomap_lock);
static unsigned long *zpci_iomap_bitmap;
struct zpci_iomap_entry *zpci_iomap_start;
EXPORT_SYMBOL_GPL(zpci_iomap_start);

DEFINE_STATIC_KEY_FALSE(have_mio);

static struct kmem_cache *zdev_fmb_cache;

struct zpci_dev *get_zdev_by_fid(u32 fid)
{
	struct zpci_dev *tmp, *zdev = NULL;

	spin_lock(&zpci_list_lock);
	list_for_each_entry(tmp, &zpci_list, entry) {
		if (tmp->fid == fid) {
			zdev = tmp;
			break;
		}
	}
	spin_unlock(&zpci_list_lock);
	return zdev;
}

void zpci_remove_reserved_devices(void)
{
	struct zpci_dev *tmp, *zdev;
	enum zpci_state state;
	LIST_HEAD(remove);

	spin_lock(&zpci_list_lock);
	list_for_each_entry_safe(zdev, tmp, &zpci_list, entry) {
		if (zdev->state == ZPCI_FN_STATE_STANDBY &&
		    !clp_get_state(zdev->fid, &state) &&
		    state == ZPCI_FN_STATE_RESERVED)
			list_move_tail(&zdev->entry, &remove);
	}
	spin_unlock(&zpci_list_lock);

	list_for_each_entry_safe(zdev, tmp, &remove, entry)
		zpci_zdev_put(zdev);
}

int pci_domain_nr(struct pci_bus *bus)
{
	return ((struct zpci_bus *) bus->sysdata)->domain_nr;
}
EXPORT_SYMBOL_GPL(pci_domain_nr);

int pci_proc_domain(struct pci_bus *bus)
{
	return pci_domain_nr(bus);
}
EXPORT_SYMBOL_GPL(pci_proc_domain);

/* Modify PCI: Register I/O address translation parameters */
int zpci_register_ioat(struct zpci_dev *zdev, u8 dmaas,
		       u64 base, u64 limit, u64 iota)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, dmaas, ZPCI_MOD_FC_REG_IOAT);
	struct zpci_fib fib = {0};
	u8 status;

	WARN_ON_ONCE(iota & 0x3fff);
	fib.pba = base;
	fib.pal = limit;
	fib.iota = iota | ZPCI_IOTA_RTTO_FLAG;
	return zpci_mod_fc(req, &fib, &status) ? -EIO : 0;
}

/* Modify PCI: Unregister I/O address translation parameters */
int zpci_unregister_ioat(struct zpci_dev *zdev, u8 dmaas)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, dmaas, ZPCI_MOD_FC_DEREG_IOAT);
	struct zpci_fib fib = {0};
	u8 cc, status;

	cc = zpci_mod_fc(req, &fib, &status);
	if (cc == 3) /* Function already gone. */
		cc = 0;
	return cc ? -EIO : 0;
}

/* Modify PCI: Set PCI function measurement parameters */
int zpci_fmb_enable_device(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_SET_MEASURE);
	struct zpci_fib fib = {0};
	u8 cc, status;

	if (zdev->fmb || sizeof(*zdev->fmb) < zdev->fmb_length)
		return -EINVAL;

	zdev->fmb = kmem_cache_zalloc(zdev_fmb_cache, GFP_KERNEL);
	if (!zdev->fmb)
		return -ENOMEM;
	WARN_ON((u64) zdev->fmb & 0xf);

	/* reset software counters */
	atomic64_set(&zdev->allocated_pages, 0);
	atomic64_set(&zdev->mapped_pages, 0);
	atomic64_set(&zdev->unmapped_pages, 0);

	fib.fmb_addr = virt_to_phys(zdev->fmb);
	cc = zpci_mod_fc(req, &fib, &status);
	if (cc) {
		kmem_cache_free(zdev_fmb_cache, zdev->fmb);
		zdev->fmb = NULL;
	}
	return cc ? -EIO : 0;
}

/* Modify PCI: Disable PCI function measurement */
int zpci_fmb_disable_device(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_SET_MEASURE);
	struct zpci_fib fib = {0};
	u8 cc, status;

	if (!zdev->fmb)
		return -EINVAL;

	/* Function measurement is disabled if fmb address is zero */
	cc = zpci_mod_fc(req, &fib, &status);
	if (cc == 3) /* Function already gone. */
		cc = 0;

	if (!cc) {
		kmem_cache_free(zdev_fmb_cache, zdev->fmb);
		zdev->fmb = NULL;
	}
	return cc ? -EIO : 0;
}

static int zpci_cfg_load(struct zpci_dev *zdev, int offset, u32 *val, u8 len)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, ZPCI_PCIAS_CFGSPC, len);
	u64 data;
	int rc;

	rc = __zpci_load(&data, req, offset);
	if (!rc) {
		data = le64_to_cpu((__force __le64) data);
		data >>= (8 - len) * 8;
		*val = (u32) data;
	} else
		*val = 0xffffffff;
	return rc;
}

static int zpci_cfg_store(struct zpci_dev *zdev, int offset, u32 val, u8 len)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, ZPCI_PCIAS_CFGSPC, len);
	u64 data = val;
	int rc;

	data <<= (8 - len) * 8;
	data = (__force u64) cpu_to_le64(data);
	rc = __zpci_store(data, req, offset);
	return rc;
}

resource_size_t pcibios_align_resource(void *data, const struct resource *res,
				       resource_size_t size,
				       resource_size_t align)
{
	return 0;
}

/* combine single writes by using store-block insn */
void __iowrite64_copy(void __iomem *to, const void *from, size_t count)
{
       zpci_memcpy_toio(to, from, count);
}

static void __iomem *__ioremap(phys_addr_t addr, size_t size, pgprot_t prot)
{
	unsigned long offset, vaddr;
	struct vm_struct *area;
	phys_addr_t last_addr;

	last_addr = addr + size - 1;
	if (!size || last_addr < addr)
		return NULL;

	if (!static_branch_unlikely(&have_mio))
		return (void __iomem *) addr;

	offset = addr & ~PAGE_MASK;
	addr &= PAGE_MASK;
	size = PAGE_ALIGN(size + offset);
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;

	vaddr = (unsigned long) area->addr;
	if (ioremap_page_range(vaddr, vaddr + size, addr, prot)) {
		free_vm_area(area);
		return NULL;
	}
	return (void __iomem *) ((unsigned long) area->addr + offset);
}

void __iomem *ioremap_prot(phys_addr_t addr, size_t size, unsigned long prot)
{
	return __ioremap(addr, size, __pgprot(prot));
}
EXPORT_SYMBOL(ioremap_prot);

void __iomem *ioremap(phys_addr_t addr, size_t size)
{
	return __ioremap(addr, size, PAGE_KERNEL);
}
EXPORT_SYMBOL(ioremap);

void __iomem *ioremap_wc(phys_addr_t addr, size_t size)
{
	return __ioremap(addr, size, pgprot_writecombine(PAGE_KERNEL));
}
EXPORT_SYMBOL(ioremap_wc);

void __iomem *ioremap_wt(phys_addr_t addr, size_t size)
{
	return __ioremap(addr, size, pgprot_writethrough(PAGE_KERNEL));
}
EXPORT_SYMBOL(ioremap_wt);

void iounmap(volatile void __iomem *addr)
{
	if (static_branch_likely(&have_mio))
		vunmap((__force void *) ((unsigned long) addr & PAGE_MASK));
}
EXPORT_SYMBOL(iounmap);

/* Create a virtual mapping cookie for a PCI BAR */
static void __iomem *pci_iomap_range_fh(struct pci_dev *pdev, int bar,
					unsigned long offset, unsigned long max)
{
	struct zpci_dev *zdev =	to_zpci(pdev);
	int idx;

	idx = zdev->bars[bar].map_idx;
	spin_lock(&zpci_iomap_lock);
	/* Detect overrun */
	WARN_ON(!++zpci_iomap_start[idx].count);
	zpci_iomap_start[idx].fh = zdev->fh;
	zpci_iomap_start[idx].bar = bar;
	spin_unlock(&zpci_iomap_lock);

	return (void __iomem *) ZPCI_ADDR(idx) + offset;
}

static void __iomem *pci_iomap_range_mio(struct pci_dev *pdev, int bar,
					 unsigned long offset,
					 unsigned long max)
{
	unsigned long barsize = pci_resource_len(pdev, bar);
	struct zpci_dev *zdev = to_zpci(pdev);
	void __iomem *iova;

	iova = ioremap((unsigned long) zdev->bars[bar].mio_wt, barsize);
	return iova ? iova + offset : iova;
}

void __iomem *pci_iomap_range(struct pci_dev *pdev, int bar,
			      unsigned long offset, unsigned long max)
{
	if (bar >= PCI_STD_NUM_BARS || !pci_resource_len(pdev, bar))
		return NULL;

	if (static_branch_likely(&have_mio))
		return pci_iomap_range_mio(pdev, bar, offset, max);
	else
		return pci_iomap_range_fh(pdev, bar, offset, max);
}
EXPORT_SYMBOL(pci_iomap_range);

void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	return pci_iomap_range(dev, bar, 0, maxlen);
}
EXPORT_SYMBOL(pci_iomap);

static void __iomem *pci_iomap_wc_range_mio(struct pci_dev *pdev, int bar,
					    unsigned long offset, unsigned long max)
{
	unsigned long barsize = pci_resource_len(pdev, bar);
	struct zpci_dev *zdev = to_zpci(pdev);
	void __iomem *iova;

	iova = ioremap((unsigned long) zdev->bars[bar].mio_wb, barsize);
	return iova ? iova + offset : iova;
}

void __iomem *pci_iomap_wc_range(struct pci_dev *pdev, int bar,
				 unsigned long offset, unsigned long max)
{
	if (bar >= PCI_STD_NUM_BARS || !pci_resource_len(pdev, bar))
		return NULL;

	if (static_branch_likely(&have_mio))
		return pci_iomap_wc_range_mio(pdev, bar, offset, max);
	else
		return pci_iomap_range_fh(pdev, bar, offset, max);
}
EXPORT_SYMBOL(pci_iomap_wc_range);

void __iomem *pci_iomap_wc(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	return pci_iomap_wc_range(dev, bar, 0, maxlen);
}
EXPORT_SYMBOL(pci_iomap_wc);

static void pci_iounmap_fh(struct pci_dev *pdev, void __iomem *addr)
{
	unsigned int idx = ZPCI_IDX(addr);

	spin_lock(&zpci_iomap_lock);
	/* Detect underrun */
	WARN_ON(!zpci_iomap_start[idx].count);
	if (!--zpci_iomap_start[idx].count) {
		zpci_iomap_start[idx].fh = 0;
		zpci_iomap_start[idx].bar = 0;
	}
	spin_unlock(&zpci_iomap_lock);
}

static void pci_iounmap_mio(struct pci_dev *pdev, void __iomem *addr)
{
	iounmap(addr);
}

void pci_iounmap(struct pci_dev *pdev, void __iomem *addr)
{
	if (static_branch_likely(&have_mio))
		pci_iounmap_mio(pdev, addr);
	else
		pci_iounmap_fh(pdev, addr);
}
EXPORT_SYMBOL(pci_iounmap);

static int pci_read(struct pci_bus *bus, unsigned int devfn, int where,
		    int size, u32 *val)
{
	struct zpci_dev *zdev = get_zdev_by_bus(bus, devfn);

	return (zdev) ? zpci_cfg_load(zdev, where, val, size) : -ENODEV;
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where,
		     int size, u32 val)
{
	struct zpci_dev *zdev = get_zdev_by_bus(bus, devfn);

	return (zdev) ? zpci_cfg_store(zdev, where, val, size) : -ENODEV;
}

static struct pci_ops pci_root_ops = {
	.read = pci_read,
	.write = pci_write,
};

static void zpci_map_resources(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = to_zpci(pdev);
	resource_size_t len;
	int i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		len = pci_resource_len(pdev, i);
		if (!len)
			continue;

		if (zpci_use_mio(zdev))
			pdev->resource[i].start =
				(resource_size_t __force) zdev->bars[i].mio_wt;
		else
			pdev->resource[i].start = (resource_size_t __force)
				pci_iomap_range_fh(pdev, i, 0, 0);
		pdev->resource[i].end = pdev->resource[i].start + len - 1;
	}

	zpci_iov_map_resources(pdev);
}

static void zpci_unmap_resources(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = to_zpci(pdev);
	resource_size_t len;
	int i;

	if (zpci_use_mio(zdev))
		return;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		len = pci_resource_len(pdev, i);
		if (!len)
			continue;
		pci_iounmap_fh(pdev, (void __iomem __force *)
			       pdev->resource[i].start);
	}
}

static int zpci_alloc_iomap(struct zpci_dev *zdev)
{
	unsigned long entry;

	spin_lock(&zpci_iomap_lock);
	entry = find_first_zero_bit(zpci_iomap_bitmap, ZPCI_IOMAP_ENTRIES);
	if (entry == ZPCI_IOMAP_ENTRIES) {
		spin_unlock(&zpci_iomap_lock);
		return -ENOSPC;
	}
	set_bit(entry, zpci_iomap_bitmap);
	spin_unlock(&zpci_iomap_lock);
	return entry;
}

static void zpci_free_iomap(struct zpci_dev *zdev, int entry)
{
	spin_lock(&zpci_iomap_lock);
	memset(&zpci_iomap_start[entry], 0, sizeof(struct zpci_iomap_entry));
	clear_bit(entry, zpci_iomap_bitmap);
	spin_unlock(&zpci_iomap_lock);
}

static struct resource *__alloc_res(struct zpci_dev *zdev, unsigned long start,
				    unsigned long size, unsigned long flags)
{
	struct resource *r;

	r = kzalloc(sizeof(*r), GFP_KERNEL);
	if (!r)
		return NULL;

	r->start = start;
	r->end = r->start + size - 1;
	r->flags = flags;
	r->name = zdev->res_name;

	if (request_resource(&iomem_resource, r)) {
		kfree(r);
		return NULL;
	}
	return r;
}

int zpci_setup_bus_resources(struct zpci_dev *zdev,
			     struct list_head *resources)
{
	unsigned long addr, size, flags;
	struct resource *res;
	int i, entry;

	snprintf(zdev->res_name, sizeof(zdev->res_name),
		 "PCI Bus %04x:%02x", zdev->uid, ZPCI_BUS_NR);

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (!zdev->bars[i].size)
			continue;
		entry = zpci_alloc_iomap(zdev);
		if (entry < 0)
			return entry;
		zdev->bars[i].map_idx = entry;

		/* only MMIO is supported */
		flags = IORESOURCE_MEM;
		if (zdev->bars[i].val & 8)
			flags |= IORESOURCE_PREFETCH;
		if (zdev->bars[i].val & 4)
			flags |= IORESOURCE_MEM_64;

		if (zpci_use_mio(zdev))
			addr = (unsigned long) zdev->bars[i].mio_wt;
		else
			addr = ZPCI_ADDR(entry);
		size = 1UL << zdev->bars[i].size;

		res = __alloc_res(zdev, addr, size, flags);
		if (!res) {
			zpci_free_iomap(zdev, entry);
			return -ENOMEM;
		}
		zdev->bars[i].res = res;
		pci_add_resource(resources, res);
	}

	return 0;
}

static void zpci_cleanup_bus_resources(struct zpci_dev *zdev)
{
	int i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (!zdev->bars[i].size || !zdev->bars[i].res)
			continue;

		zpci_free_iomap(zdev, zdev->bars[i].map_idx);
		release_resource(zdev->bars[i].res);
		kfree(zdev->bars[i].res);
	}
}

int pcibios_add_device(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = to_zpci(pdev);
	struct resource *res;
	int i;

	/* The pdev has a reference to the zdev via its bus */
	zpci_zdev_get(zdev);
	if (pdev->is_physfn)
		pdev->no_vf_scan = 1;

	pdev->dev.groups = zpci_attr_groups;
	pdev->dev.dma_ops = &s390_pci_dma_ops;
	zpci_map_resources(pdev);

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		res = &pdev->resource[i];
		if (res->parent || !res->flags)
			continue;
		pci_claim_resource(pdev, i);
	}

	return 0;
}

void pcibios_release_device(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = to_zpci(pdev);

	zpci_unmap_resources(pdev);
	zpci_zdev_put(zdev);
}

int pcibios_enable_device(struct pci_dev *pdev, int mask)
{
	struct zpci_dev *zdev = to_zpci(pdev);

	zpci_debug_init_device(zdev, dev_name(&pdev->dev));
	zpci_fmb_enable_device(zdev);

	return pci_enable_resources(pdev, mask);
}

void pcibios_disable_device(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = to_zpci(pdev);

	zpci_fmb_disable_device(zdev);
	zpci_debug_exit_device(zdev);
}

static int __zpci_register_domain(int domain)
{
	spin_lock(&zpci_domain_lock);
	if (test_bit(domain, zpci_domain)) {
		spin_unlock(&zpci_domain_lock);
		pr_err("Domain %04x is already assigned\n", domain);
		return -EEXIST;
	}
	set_bit(domain, zpci_domain);
	spin_unlock(&zpci_domain_lock);
	return domain;
}

static int __zpci_alloc_domain(void)
{
	int domain;

	spin_lock(&zpci_domain_lock);
	/*
	 * We can always auto allocate domains below ZPCI_NR_DEVICES.
	 * There is either a free domain or we have reached the maximum in
	 * which case we would have bailed earlier.
	 */
	domain = find_first_zero_bit(zpci_domain, ZPCI_NR_DEVICES);
	set_bit(domain, zpci_domain);
	spin_unlock(&zpci_domain_lock);
	return domain;
}

int zpci_alloc_domain(int domain)
{
	if (zpci_unique_uid) {
		if (domain)
			return __zpci_register_domain(domain);
		pr_warn("UID checking was active but no UID is provided: switching to automatic domain allocation\n");
		update_uid_checking(false);
	}
	return __zpci_alloc_domain();
}

void zpci_free_domain(int domain)
{
	spin_lock(&zpci_domain_lock);
	clear_bit(domain, zpci_domain);
	spin_unlock(&zpci_domain_lock);
}


int zpci_enable_device(struct zpci_dev *zdev)
{
	int rc;

	if (clp_enable_fh(zdev, ZPCI_NR_DMA_SPACES)) {
		rc = -EIO;
		goto out;
	}

	rc = zpci_dma_init_device(zdev);
	if (rc)
		goto out_dma;

	zdev->state = ZPCI_FN_STATE_ONLINE;
	return 0;

out_dma:
	clp_disable_fh(zdev);
out:
	return rc;
}
EXPORT_SYMBOL_GPL(zpci_enable_device);

int zpci_disable_device(struct zpci_dev *zdev)
{
	zpci_dma_exit_device(zdev);
	/*
	 * The zPCI function may already be disabled by the platform, this is
	 * detected in clp_disable_fh() which becomes a no-op.
	 */
	return clp_disable_fh(zdev) ? -EIO : 0;
}
EXPORT_SYMBOL_GPL(zpci_disable_device);

/* zpci_remove_device - Removes the given zdev from the PCI core
 * @zdev: the zdev to be removed from the PCI core
 * @set_error: if true the device's error state is set to permanent failure
 *
 * Sets a zPCI device to a configured but offline state; the zPCI
 * device is still accessible through its hotplug slot and the zPCI
 * API but is removed from the common code PCI bus, making it
 * no longer available to drivers.
 */
void zpci_remove_device(struct zpci_dev *zdev, bool set_error)
{
	struct zpci_bus *zbus = zdev->zbus;
	struct pci_dev *pdev;

	if (!zdev->zbus->bus)
		return;

	pdev = pci_get_slot(zbus->bus, zdev->devfn);
	if (pdev) {
		if (set_error)
			pdev->error_state = pci_channel_io_perm_failure;
		if (pdev->is_virtfn) {
			zpci_iov_remove_virtfn(pdev, zdev->vfn);
			/* balance pci_get_slot */
			pci_dev_put(pdev);
			return;
		}
		pci_stop_and_remove_bus_device_locked(pdev);
		/* balance pci_get_slot */
		pci_dev_put(pdev);
	}
}

/**
 * zpci_create_device() - Create a new zpci_dev and add it to the zbus
 * @fid: Function ID of the device to be created
 * @fh: Current Function Handle of the device to be created
 * @state: Initial state after creation either Standby or Configured
 *
 * Creates a new zpci device and adds it to its, possibly newly created, zbus
 * as well as zpci_list.
 *
 * Returns: 0 on success, an error value otherwise
 */
int zpci_create_device(u32 fid, u32 fh, enum zpci_state state)
{
	struct zpci_dev *zdev;
	int rc;

	zpci_dbg(3, "add fid:%x, fh:%x, c:%d\n", fid, fh, state);
	zdev = kzalloc(sizeof(*zdev), GFP_KERNEL);
	if (!zdev)
		return -ENOMEM;

	/* FID and Function Handle are the static/dynamic identifiers */
	zdev->fid = fid;
	zdev->fh = fh;

	/* Query function properties and update zdev */
	rc = clp_query_pci_fn(zdev);
	if (rc)
		goto error;
	zdev->state =  state;

	kref_init(&zdev->kref);
	mutex_init(&zdev->lock);

	rc = zpci_init_iommu(zdev);
	if (rc)
		goto error;

	if (zdev->state == ZPCI_FN_STATE_CONFIGURED) {
		rc = zpci_enable_device(zdev);
		if (rc)
			goto error_destroy_iommu;
	}

	rc = zpci_bus_device_register(zdev, &pci_root_ops);
	if (rc)
		goto error_disable;

	spin_lock(&zpci_list_lock);
	list_add_tail(&zdev->entry, &zpci_list);
	spin_unlock(&zpci_list_lock);

	return 0;

error_disable:
	if (zdev->state == ZPCI_FN_STATE_ONLINE)
		zpci_disable_device(zdev);
error_destroy_iommu:
	zpci_destroy_iommu(zdev);
error:
	zpci_dbg(0, "add fid:%x, rc:%d\n", fid, rc);
	kfree(zdev);
	return rc;
}

void zpci_release_device(struct kref *kref)
{
	struct zpci_dev *zdev = container_of(kref, struct zpci_dev, kref);

	if (zdev->zbus->bus)
		zpci_remove_device(zdev, false);

	switch (zdev->state) {
	case ZPCI_FN_STATE_ONLINE:
	case ZPCI_FN_STATE_CONFIGURED:
		zpci_disable_device(zdev);
		fallthrough;
	case ZPCI_FN_STATE_STANDBY:
		if (zdev->has_hp_slot)
			zpci_exit_slot(zdev);
		zpci_cleanup_bus_resources(zdev);
		zpci_bus_device_unregister(zdev);
		zpci_destroy_iommu(zdev);
		fallthrough;
	default:
		break;
	}

	spin_lock(&zpci_list_lock);
	list_del(&zdev->entry);
	spin_unlock(&zpci_list_lock);
	zpci_dbg(3, "rem fid:%x\n", zdev->fid);
	kfree(zdev);
}

int zpci_report_error(struct pci_dev *pdev,
		      struct zpci_report_error_header *report)
{
	struct zpci_dev *zdev = to_zpci(pdev);

	return sclp_pci_report(report, zdev->fh, zdev->fid);
}
EXPORT_SYMBOL(zpci_report_error);

static int zpci_mem_init(void)
{
	BUILD_BUG_ON(!is_power_of_2(__alignof__(struct zpci_fmb)) ||
		     __alignof__(struct zpci_fmb) < sizeof(struct zpci_fmb));

	zdev_fmb_cache = kmem_cache_create("PCI_FMB_cache", sizeof(struct zpci_fmb),
					   __alignof__(struct zpci_fmb), 0, NULL);
	if (!zdev_fmb_cache)
		goto error_fmb;

	zpci_iomap_start = kcalloc(ZPCI_IOMAP_ENTRIES,
				   sizeof(*zpci_iomap_start), GFP_KERNEL);
	if (!zpci_iomap_start)
		goto error_iomap;

	zpci_iomap_bitmap = kcalloc(BITS_TO_LONGS(ZPCI_IOMAP_ENTRIES),
				    sizeof(*zpci_iomap_bitmap), GFP_KERNEL);
	if (!zpci_iomap_bitmap)
		goto error_iomap_bitmap;

	if (static_branch_likely(&have_mio))
		clp_setup_writeback_mio();

	return 0;
error_iomap_bitmap:
	kfree(zpci_iomap_start);
error_iomap:
	kmem_cache_destroy(zdev_fmb_cache);
error_fmb:
	return -ENOMEM;
}

static void zpci_mem_exit(void)
{
	kfree(zpci_iomap_bitmap);
	kfree(zpci_iomap_start);
	kmem_cache_destroy(zdev_fmb_cache);
}

static unsigned int s390_pci_probe __initdata = 1;
unsigned int s390_pci_force_floating __initdata;
static unsigned int s390_pci_initialized;

char * __init pcibios_setup(char *str)
{
	if (!strcmp(str, "off")) {
		s390_pci_probe = 0;
		return NULL;
	}
	if (!strcmp(str, "nomio")) {
		S390_lowcore.machine_flags &= ~MACHINE_FLAG_PCI_MIO;
		return NULL;
	}
	if (!strcmp(str, "force_floating")) {
		s390_pci_force_floating = 1;
		return NULL;
	}
	if (!strcmp(str, "norid")) {
		s390_pci_no_rid = 1;
		return NULL;
	}
	return str;
}

bool zpci_is_enabled(void)
{
	return s390_pci_initialized;
}

static int __init pci_base_init(void)
{
	int rc;

	if (!s390_pci_probe)
		return 0;

	if (!test_facility(69) || !test_facility(71))
		return 0;

	if (MACHINE_HAS_PCI_MIO) {
		static_branch_enable(&have_mio);
		ctl_set_bit(2, 5);
	}

	rc = zpci_debug_init();
	if (rc)
		goto out;

	rc = zpci_mem_init();
	if (rc)
		goto out_mem;

	rc = zpci_irq_init();
	if (rc)
		goto out_irq;

	rc = zpci_dma_init();
	if (rc)
		goto out_dma;

	rc = clp_scan_pci_devices();
	if (rc)
		goto out_find;

	s390_pci_initialized = 1;
	return 0;

out_find:
	zpci_dma_exit();
out_dma:
	zpci_irq_exit();
out_irq:
	zpci_mem_exit();
out_mem:
	zpci_debug_exit();
out:
	return rc;
}
subsys_initcall_sync(pci_base_init);
