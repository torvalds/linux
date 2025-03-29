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
#include <linux/lockdep.h>
#include <linux/list_sort.h>

#include <asm/machine.h>
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

/* AEN structures that must be preserved over KVM module re-insertion */
union zpci_sic_iib *zpci_aipb;
EXPORT_SYMBOL_GPL(zpci_aipb);
struct airq_iv *zpci_aif_sbv;
EXPORT_SYMBOL_GPL(zpci_aif_sbv);

struct zpci_dev *get_zdev_by_fid(u32 fid)
{
	struct zpci_dev *tmp, *zdev = NULL;

	spin_lock(&zpci_list_lock);
	list_for_each_entry(tmp, &zpci_list, entry) {
		if (tmp->fid == fid) {
			zdev = tmp;
			zpci_zdev_get(zdev);
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
		zpci_device_reserved(zdev);
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
		       u64 base, u64 limit, u64 iota, u8 *status)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, dmaas, ZPCI_MOD_FC_REG_IOAT);
	struct zpci_fib fib = {0};
	u8 cc;

	fib.pba = base;
	/* Work around off by one in ISM virt device */
	if (zdev->pft == PCI_FUNC_TYPE_ISM && limit > base)
		fib.pal = limit + (1 << 12);
	else
		fib.pal = limit;
	fib.iota = iota;
	fib.gd = zdev->gisa;
	cc = zpci_mod_fc(req, &fib, status);
	if (cc)
		zpci_dbg(3, "reg ioat fid:%x, cc:%d, status:%d\n", zdev->fid, cc, *status);
	return cc;
}
EXPORT_SYMBOL_GPL(zpci_register_ioat);

/* Modify PCI: Unregister I/O address translation parameters */
int zpci_unregister_ioat(struct zpci_dev *zdev, u8 dmaas)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, dmaas, ZPCI_MOD_FC_DEREG_IOAT);
	struct zpci_fib fib = {0};
	u8 cc, status;

	fib.gd = zdev->gisa;

	cc = zpci_mod_fc(req, &fib, &status);
	if (cc)
		zpci_dbg(3, "unreg ioat fid:%x, cc:%d, status:%d\n", zdev->fid, cc, status);
	return cc;
}

/* Modify PCI: Set PCI function measurement parameters */
int zpci_fmb_enable_device(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_SET_MEASURE);
	struct zpci_iommu_ctrs *ctrs;
	struct zpci_fib fib = {0};
	unsigned long flags;
	u8 cc, status;

	if (zdev->fmb || sizeof(*zdev->fmb) < zdev->fmb_length)
		return -EINVAL;

	zdev->fmb = kmem_cache_zalloc(zdev_fmb_cache, GFP_KERNEL);
	if (!zdev->fmb)
		return -ENOMEM;
	WARN_ON((u64) zdev->fmb & 0xf);

	/* reset software counters */
	spin_lock_irqsave(&zdev->dom_lock, flags);
	ctrs = zpci_get_iommu_ctrs(zdev);
	if (ctrs) {
		atomic64_set(&ctrs->mapped_pages, 0);
		atomic64_set(&ctrs->unmapped_pages, 0);
		atomic64_set(&ctrs->global_rpcits, 0);
		atomic64_set(&ctrs->sync_map_rpcits, 0);
		atomic64_set(&ctrs->sync_rpcits, 0);
	}
	spin_unlock_irqrestore(&zdev->dom_lock, flags);


	fib.fmb_addr = virt_to_phys(zdev->fmb);
	fib.gd = zdev->gisa;
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

	fib.gd = zdev->gisa;

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

void __iomem *ioremap_prot(phys_addr_t phys_addr, size_t size,
			   unsigned long prot)
{
	/*
	 * When PCI MIO instructions are unavailable the "physical" address
	 * encodes a hint for accessing the PCI memory space it represents.
	 * Just pass it unchanged such that ioread/iowrite can decode it.
	 */
	if (!static_branch_unlikely(&have_mio))
		return (void __iomem *)phys_addr;

	return generic_ioremap_prot(phys_addr, size, __pgprot(prot));
}
EXPORT_SYMBOL(ioremap_prot);

void iounmap(volatile void __iomem *addr)
{
	if (static_branch_likely(&have_mio))
		generic_iounmap(addr);
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
	struct zpci_dev *zdev = zdev_from_bus(bus, devfn);

	return (zdev) ? zpci_cfg_load(zdev, where, val, size) : -ENODEV;
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where,
		     int size, u32 val)
{
	struct zpci_dev *zdev = zdev_from_bus(bus, devfn);

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

static void zpci_do_update_iomap_fh(struct zpci_dev *zdev, u32 fh)
{
	int bar, idx;

	spin_lock(&zpci_iomap_lock);
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (!zdev->bars[bar].size)
			continue;
		idx = zdev->bars[bar].map_idx;
		if (!zpci_iomap_start[idx].count)
			continue;
		WRITE_ONCE(zpci_iomap_start[idx].fh, zdev->fh);
	}
	spin_unlock(&zpci_iomap_lock);
}

void zpci_update_fh(struct zpci_dev *zdev, u32 fh)
{
	if (!fh || zdev->fh == fh)
		return;

	zdev->fh = fh;
	if (zpci_use_mio(zdev))
		return;
	if (zdev->has_resources && zdev_enabled(zdev))
		zpci_do_update_iomap_fh(zdev, fh);
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

int zpci_setup_bus_resources(struct zpci_dev *zdev)
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
	}
	zdev->has_resources = 1;

	return 0;
}

static void zpci_cleanup_bus_resources(struct zpci_dev *zdev)
{
	struct resource *res;
	int i;

	pci_lock_rescan_remove();
	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		res = zdev->bars[i].res;
		if (!res)
			continue;

		release_resource(res);
		pci_bus_remove_resource(zdev->zbus->bus, res);
		zpci_free_iomap(zdev, zdev->bars[i].map_idx);
		zdev->bars[i].res = NULL;
		kfree(res);
	}
	zdev->has_resources = 0;
	pci_unlock_rescan_remove();
}

int pcibios_device_add(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = to_zpci(pdev);
	struct resource *res;
	int i;

	/* The pdev has a reference to the zdev via its bus */
	zpci_zdev_get(zdev);
	if (pdev->is_physfn)
		pdev->no_vf_scan = 1;

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
	u32 fh = zdev->fh;
	int rc = 0;

	if (clp_enable_fh(zdev, &fh, ZPCI_NR_DMA_SPACES))
		rc = -EIO;
	else
		zpci_update_fh(zdev, fh);
	return rc;
}
EXPORT_SYMBOL_GPL(zpci_enable_device);

int zpci_reenable_device(struct zpci_dev *zdev)
{
	u8 status;
	int rc;

	rc = zpci_enable_device(zdev);
	if (rc)
		return rc;

	rc = zpci_iommu_register_ioat(zdev, &status);
	if (rc)
		zpci_disable_device(zdev);

	return rc;
}
EXPORT_SYMBOL_GPL(zpci_reenable_device);

int zpci_disable_device(struct zpci_dev *zdev)
{
	u32 fh = zdev->fh;
	int cc, rc = 0;

	cc = clp_disable_fh(zdev, &fh);
	if (!cc) {
		zpci_update_fh(zdev, fh);
	} else if (cc == CLP_RC_SETPCIFN_ALRDY) {
		pr_info("Disabling PCI function %08x had no effect as it was already disabled\n",
			zdev->fid);
		/* Function is already disabled - update handle */
		rc = clp_refresh_fh(zdev->fid, &fh);
		if (!rc) {
			zpci_update_fh(zdev, fh);
			rc = -EINVAL;
		}
	} else {
		rc = -EIO;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(zpci_disable_device);

/**
 * zpci_hot_reset_device - perform a reset of the given zPCI function
 * @zdev: the slot which should be reset
 *
 * Performs a low level reset of the zPCI function. The reset is low level in
 * the sense that the zPCI function can be reset without detaching it from the
 * common PCI subsystem. The reset may be performed while under control of
 * either DMA or IOMMU APIs in which case the existing DMA/IOMMU translation
 * table is reinstated at the end of the reset.
 *
 * After the reset the functions internal state is reset to an initial state
 * equivalent to its state during boot when first probing a driver.
 * Consequently after reset the PCI function requires re-initialization via the
 * common PCI code including re-enabling IRQs via pci_alloc_irq_vectors()
 * and enabling the function via e.g. pci_enable_device_flags(). The caller
 * must guard against concurrent reset attempts.
 *
 * In most cases this function should not be called directly but through
 * pci_reset_function() or pci_reset_bus() which handle the save/restore and
 * locking - asserted by lockdep.
 *
 * Return: 0 on success and an error value otherwise
 */
int zpci_hot_reset_device(struct zpci_dev *zdev)
{
	int rc;

	lockdep_assert_held(&zdev->state_lock);
	zpci_dbg(3, "rst fid:%x, fh:%x\n", zdev->fid, zdev->fh);
	if (zdev_enabled(zdev)) {
		/* Disables device access, DMAs and IRQs (reset state) */
		rc = zpci_disable_device(zdev);
		/*
		 * Due to a z/VM vs LPAR inconsistency in the error state the
		 * FH may indicate an enabled device but disable says the
		 * device is already disabled don't treat it as an error here.
		 */
		if (rc == -EINVAL)
			rc = 0;
		if (rc)
			return rc;
	}

	rc = zpci_reenable_device(zdev);

	return rc;
}

/**
 * zpci_create_device() - Create a new zpci_dev and add it to the zbus
 * @fid: Function ID of the device to be created
 * @fh: Current Function Handle of the device to be created
 * @state: Initial state after creation either Standby or Configured
 *
 * Allocates a new struct zpci_dev and queries the platform for its details.
 * If successful the device can subsequently be added to the zPCI subsystem
 * using zpci_add_device().
 *
 * Returns: the zdev on success or an error pointer otherwise
 */
struct zpci_dev *zpci_create_device(u32 fid, u32 fh, enum zpci_state state)
{
	struct zpci_dev *zdev;
	int rc;

	zdev = kzalloc(sizeof(*zdev), GFP_KERNEL);
	if (!zdev)
		return ERR_PTR(-ENOMEM);

	/* FID and Function Handle are the static/dynamic identifiers */
	zdev->fid = fid;
	zdev->fh = fh;

	/* Query function properties and update zdev */
	rc = clp_query_pci_fn(zdev);
	if (rc)
		goto error;
	zdev->state =  state;

	mutex_init(&zdev->state_lock);
	mutex_init(&zdev->fmb_lock);
	mutex_init(&zdev->kzdev_lock);

	return zdev;

error:
	zpci_dbg(0, "crt fid:%x, rc:%d\n", fid, rc);
	kfree(zdev);
	return ERR_PTR(rc);
}

/**
 * zpci_add_device() - Add a previously created zPCI device to the zPCI subsystem
 * @zdev: The zPCI device to be added
 *
 * A struct zpci_dev is added to the zPCI subsystem and to a virtual PCI bus creating
 * a new one as necessary. A hotplug slot is created and events start to be handled.
 * If successful from this point on zpci_zdev_get() and zpci_zdev_put() must be used.
 * If adding the struct zpci_dev fails the device was not added and should be freed.
 *
 * Return: 0 on success, or an error code otherwise
 */
int zpci_add_device(struct zpci_dev *zdev)
{
	int rc;

	zpci_dbg(1, "add fid:%x, fh:%x, c:%d\n", zdev->fid, zdev->fh, zdev->state);
	rc = zpci_init_iommu(zdev);
	if (rc)
		goto error;

	rc = zpci_bus_device_register(zdev, &pci_root_ops);
	if (rc)
		goto error_destroy_iommu;

	kref_init(&zdev->kref);
	spin_lock(&zpci_list_lock);
	list_add_tail(&zdev->entry, &zpci_list);
	spin_unlock(&zpci_list_lock);
	return 0;

error_destroy_iommu:
	zpci_destroy_iommu(zdev);
error:
	zpci_dbg(0, "add fid:%x, rc:%d\n", zdev->fid, rc);
	return rc;
}

bool zpci_is_device_configured(struct zpci_dev *zdev)
{
	enum zpci_state state = zdev->state;

	return state != ZPCI_FN_STATE_RESERVED &&
		state != ZPCI_FN_STATE_STANDBY;
}

/**
 * zpci_scan_configured_device() - Scan a freshly configured zpci_dev
 * @zdev: The zpci_dev to be configured
 * @fh: The general function handle supplied by the platform
 *
 * Given a device in the configuration state Configured, enables, scans and
 * adds it to the common code PCI subsystem if possible. If any failure occurs,
 * the zpci_dev is left disabled.
 *
 * Return: 0 on success, or an error code otherwise
 */
int zpci_scan_configured_device(struct zpci_dev *zdev, u32 fh)
{
	zpci_update_fh(zdev, fh);
	return zpci_bus_scan_device(zdev);
}

/**
 * zpci_deconfigure_device() - Deconfigure a zpci_dev
 * @zdev: The zpci_dev to configure
 *
 * Deconfigure a zPCI function that is currently configured and possibly known
 * to the common code PCI subsystem.
 * If any failure occurs the device is left as is.
 *
 * Return: 0 on success, or an error code otherwise
 */
int zpci_deconfigure_device(struct zpci_dev *zdev)
{
	int rc;

	lockdep_assert_held(&zdev->state_lock);
	if (zdev->state != ZPCI_FN_STATE_CONFIGURED)
		return 0;

	if (zdev->zbus->bus)
		zpci_bus_remove_device(zdev, false);

	if (zdev_enabled(zdev)) {
		rc = zpci_disable_device(zdev);
		if (rc)
			return rc;
	}

	rc = sclp_pci_deconfigure(zdev->fid);
	zpci_dbg(3, "deconf fid:%x, rc:%d\n", zdev->fid, rc);
	if (rc)
		return rc;
	zdev->state = ZPCI_FN_STATE_STANDBY;

	return 0;
}

/**
 * zpci_device_reserved() - Mark device as reserved
 * @zdev: the zpci_dev that was reserved
 *
 * Handle the case that a given zPCI function was reserved by another system.
 * After a call to this function the zpci_dev can not be found via
 * get_zdev_by_fid() anymore but may still be accessible via existing
 * references though it will not be functional anymore.
 */
void zpci_device_reserved(struct zpci_dev *zdev)
{
	/*
	 * Remove device from zpci_list as it is going away. This also
	 * makes sure we ignore subsequent zPCI events for this device.
	 */
	spin_lock(&zpci_list_lock);
	list_del(&zdev->entry);
	spin_unlock(&zpci_list_lock);
	zdev->state = ZPCI_FN_STATE_RESERVED;
	zpci_dbg(3, "rsv fid:%x\n", zdev->fid);
	zpci_zdev_put(zdev);
}

void zpci_release_device(struct kref *kref)
{
	struct zpci_dev *zdev = container_of(kref, struct zpci_dev, kref);

	WARN_ON(zdev->state != ZPCI_FN_STATE_RESERVED);

	if (zdev->zbus->bus)
		zpci_bus_remove_device(zdev, false);

	if (zdev_enabled(zdev))
		zpci_disable_device(zdev);

	if (zdev->has_hp_slot)
		zpci_exit_slot(zdev);

	if (zdev->has_resources)
		zpci_cleanup_bus_resources(zdev);

	zpci_bus_device_unregister(zdev);
	zpci_destroy_iommu(zdev);
	zpci_dbg(3, "rem fid:%x\n", zdev->fid);
	kfree_rcu(zdev, rcu);
}

int zpci_report_error(struct pci_dev *pdev,
		      struct zpci_report_error_header *report)
{
	struct zpci_dev *zdev = to_zpci(pdev);

	return sclp_pci_report(report, zdev->fh, zdev->fid);
}
EXPORT_SYMBOL(zpci_report_error);

/**
 * zpci_clear_error_state() - Clears the zPCI error state of the device
 * @zdev: The zdev for which the zPCI error state should be reset
 *
 * Clear the zPCI error state of the device. If clearing the zPCI error state
 * fails the device is left in the error state. In this case it may make sense
 * to call zpci_io_perm_failure() on the associated pdev if it exists.
 *
 * Returns: 0 on success, -EIO otherwise
 */
int zpci_clear_error_state(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_RESET_ERROR);
	struct zpci_fib fib = {0};
	u8 status;
	int cc;

	cc = zpci_mod_fc(req, &fib, &status);
	if (cc) {
		zpci_dbg(3, "ces fid:%x, cc:%d, status:%x\n", zdev->fid, cc, status);
		return -EIO;
	}

	return 0;
}

/**
 * zpci_reset_load_store_blocked() - Re-enables L/S from error state
 * @zdev: The zdev for which to unblock load/store access
 *
 * Re-enables load/store access for a PCI function in the error state while
 * keeping DMA blocked. In this state drivers can poke MMIO space to determine
 * if error recovery is possible while catching any rogue DMA access from the
 * device.
 *
 * Returns: 0 on success, -EIO otherwise
 */
int zpci_reset_load_store_blocked(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_RESET_BLOCK);
	struct zpci_fib fib = {0};
	u8 status;
	int cc;

	cc = zpci_mod_fc(req, &fib, &status);
	if (cc) {
		zpci_dbg(3, "rls fid:%x, cc:%d, status:%x\n", zdev->fid, cc, status);
		return -EIO;
	}

	return 0;
}

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
		clear_machine_feature(MFEATURE_PCI_MIO);
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

static int zpci_cmp_rid(void *priv, const struct list_head *a,
			const struct list_head *b)
{
	struct zpci_dev *za = container_of(a, struct zpci_dev, entry);
	struct zpci_dev *zb = container_of(b, struct zpci_dev, entry);

	/*
	 * PCI functions without RID available maintain original order
	 * between themselves but sort before those with RID.
	 */
	if (za->rid == zb->rid)
		return za->rid_available > zb->rid_available;
	/*
	 * PCI functions with RID sort by RID ascending.
	 */
	return za->rid > zb->rid;
}

static void zpci_add_devices(struct list_head *scan_list)
{
	struct zpci_dev *zdev, *tmp;

	list_sort(NULL, scan_list, &zpci_cmp_rid);
	list_for_each_entry_safe(zdev, tmp, scan_list, entry) {
		list_del_init(&zdev->entry);
		if (zpci_add_device(zdev))
			kfree(zdev);
	}
}

int zpci_scan_devices(void)
{
	LIST_HEAD(scan_list);
	int rc;

	rc = clp_scan_pci_devices(&scan_list);
	if (rc)
		return rc;

	zpci_add_devices(&scan_list);
	zpci_bus_scan_busses();
	return 0;
}

static int __init pci_base_init(void)
{
	int rc;

	if (!s390_pci_probe)
		return 0;

	if (!test_facility(69) || !test_facility(71)) {
		pr_info("PCI is not supported because CPU facilities 69 or 71 are not available\n");
		return 0;
	}

	if (test_machine_feature(MFEATURE_PCI_MIO)) {
		static_branch_enable(&have_mio);
		system_ctl_set_bit(2, CR2_MIO_ADDRESSING_BIT);
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

	rc = zpci_scan_devices();
	if (rc)
		goto out_find;

	s390_pci_initialized = 1;
	return 0;

out_find:
	zpci_irq_exit();
out_irq:
	zpci_mem_exit();
out_mem:
	zpci_debug_exit();
out:
	return rc;
}
subsys_initcall_sync(pci_base_init);
