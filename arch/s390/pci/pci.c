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
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <linux/pci.h>
#include <linux/msi.h>

#include <asm/isc.h>
#include <asm/airq.h>
#include <asm/facility.h>
#include <asm/pci_insn.h>
#include <asm/pci_clp.h>
#include <asm/pci_dma.h>

#define DEBUG				/* enable pr_debug */

#define	SIC_IRQ_MODE_ALL		0
#define	SIC_IRQ_MODE_SINGLE		1

#define ZPCI_NR_DMA_SPACES		1
#define ZPCI_NR_DEVICES			CONFIG_PCI_NR_FUNCTIONS

/* list of all detected zpci devices */
static LIST_HEAD(zpci_list);
static DEFINE_SPINLOCK(zpci_list_lock);

static struct irq_chip zpci_irq_chip = {
	.name = "zPCI",
	.irq_unmask = pci_msi_unmask_irq,
	.irq_mask = pci_msi_mask_irq,
};

static DECLARE_BITMAP(zpci_domain, ZPCI_NR_DEVICES);
static DEFINE_SPINLOCK(zpci_domain_lock);

static struct airq_iv *zpci_aisb_iv;
static struct airq_iv *zpci_aibv[ZPCI_NR_DEVICES];

/* Adapter interrupt definitions */
static void zpci_irq_handler(struct airq_struct *airq);

static struct airq_struct zpci_airq = {
	.handler = zpci_irq_handler,
	.isc = PCI_ISC,
};

/* I/O Map */
static DEFINE_SPINLOCK(zpci_iomap_lock);
static DECLARE_BITMAP(zpci_iomap, ZPCI_IOMAP_MAX_ENTRIES);
struct zpci_iomap_entry *zpci_iomap_start;
EXPORT_SYMBOL_GPL(zpci_iomap_start);

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

static struct zpci_dev *get_zdev_by_bus(struct pci_bus *bus)
{
	return (bus && bus->sysdata) ? (struct zpci_dev *) bus->sysdata : NULL;
}

int pci_domain_nr(struct pci_bus *bus)
{
	return ((struct zpci_dev *) bus->sysdata)->domain;
}
EXPORT_SYMBOL_GPL(pci_domain_nr);

int pci_proc_domain(struct pci_bus *bus)
{
	return pci_domain_nr(bus);
}
EXPORT_SYMBOL_GPL(pci_proc_domain);

/* Modify PCI: Register adapter interruptions */
static int zpci_set_airq(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_REG_INT);
	struct zpci_fib fib = {0};

	fib.isc = PCI_ISC;
	fib.sum = 1;		/* enable summary notifications */
	fib.noi = airq_iv_end(zdev->aibv);
	fib.aibv = (unsigned long) zdev->aibv->vector;
	fib.aibvo = 0;		/* each zdev has its own interrupt vector */
	fib.aisb = (unsigned long) zpci_aisb_iv->vector + (zdev->aisb/64)*8;
	fib.aisbo = zdev->aisb & 63;

	return zpci_mod_fc(req, &fib);
}

struct mod_pci_args {
	u64 base;
	u64 limit;
	u64 iota;
	u64 fmb_addr;
};

static int mod_pci(struct zpci_dev *zdev, int fn, u8 dmaas, struct mod_pci_args *args)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, dmaas, fn);
	struct zpci_fib fib = {0};

	fib.pba = args->base;
	fib.pal = args->limit;
	fib.iota = args->iota;
	fib.fmb_addr = args->fmb_addr;

	return zpci_mod_fc(req, &fib);
}

/* Modify PCI: Register I/O address translation parameters */
int zpci_register_ioat(struct zpci_dev *zdev, u8 dmaas,
		       u64 base, u64 limit, u64 iota)
{
	struct mod_pci_args args = { base, limit, iota, 0 };

	WARN_ON_ONCE(iota & 0x3fff);
	args.iota |= ZPCI_IOTA_RTTO_FLAG;
	return mod_pci(zdev, ZPCI_MOD_FC_REG_IOAT, dmaas, &args);
}

/* Modify PCI: Unregister I/O address translation parameters */
int zpci_unregister_ioat(struct zpci_dev *zdev, u8 dmaas)
{
	struct mod_pci_args args = { 0, 0, 0, 0 };

	return mod_pci(zdev, ZPCI_MOD_FC_DEREG_IOAT, dmaas, &args);
}

/* Modify PCI: Unregister adapter interruptions */
static int zpci_clear_airq(struct zpci_dev *zdev)
{
	struct mod_pci_args args = { 0, 0, 0, 0 };

	return mod_pci(zdev, ZPCI_MOD_FC_DEREG_INT, 0, &args);
}

/* Modify PCI: Set PCI function measurement parameters */
int zpci_fmb_enable_device(struct zpci_dev *zdev)
{
	struct mod_pci_args args = { 0, 0, 0, 0 };

	if (zdev->fmb)
		return -EINVAL;

	zdev->fmb = kmem_cache_zalloc(zdev_fmb_cache, GFP_KERNEL);
	if (!zdev->fmb)
		return -ENOMEM;
	WARN_ON((u64) zdev->fmb & 0xf);

	/* reset software counters */
	atomic64_set(&zdev->allocated_pages, 0);
	atomic64_set(&zdev->mapped_pages, 0);
	atomic64_set(&zdev->unmapped_pages, 0);

	args.fmb_addr = virt_to_phys(zdev->fmb);
	return mod_pci(zdev, ZPCI_MOD_FC_SET_MEASURE, 0, &args);
}

/* Modify PCI: Disable PCI function measurement */
int zpci_fmb_disable_device(struct zpci_dev *zdev)
{
	struct mod_pci_args args = { 0, 0, 0, 0 };
	int rc;

	if (!zdev->fmb)
		return -EINVAL;

	/* Function measurement is disabled if fmb address is zero */
	rc = mod_pci(zdev, ZPCI_MOD_FC_SET_MEASURE, 0, &args);

	kmem_cache_free(zdev_fmb_cache, zdev->fmb);
	zdev->fmb = NULL;
	return rc;
}

#define ZPCI_PCIAS_CFGSPC	15

static int zpci_cfg_load(struct zpci_dev *zdev, int offset, u32 *val, u8 len)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, ZPCI_PCIAS_CFGSPC, len);
	u64 data;
	int rc;

	rc = zpci_load(&data, req, offset);
	if (!rc) {
		data = data << ((8 - len) * 8);
		data = le64_to_cpu(data);
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

	data = cpu_to_le64(data);
	data = data >> ((8 - len) * 8);
	rc = zpci_store(data, req, offset);
	return rc;
}

void pcibios_fixup_bus(struct pci_bus *bus)
{
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

/* Create a virtual mapping cookie for a PCI BAR */
void __iomem *pci_iomap_range(struct pci_dev *pdev,
			      int bar,
			      unsigned long offset,
			      unsigned long max)
{
	struct zpci_dev *zdev =	to_zpci(pdev);
	u64 addr;
	int idx;

	if ((bar & 7) != bar)
		return NULL;

	idx = zdev->bars[bar].map_idx;
	spin_lock(&zpci_iomap_lock);
	if (zpci_iomap_start[idx].count++) {
		BUG_ON(zpci_iomap_start[idx].fh != zdev->fh ||
		       zpci_iomap_start[idx].bar != bar);
	} else {
		zpci_iomap_start[idx].fh = zdev->fh;
		zpci_iomap_start[idx].bar = bar;
	}
	/* Detect overrun */
	BUG_ON(!zpci_iomap_start[idx].count);
	spin_unlock(&zpci_iomap_lock);

	addr = ZPCI_IOMAP_ADDR_BASE | ((u64) idx << 48);
	return (void __iomem *) addr + offset;
}
EXPORT_SYMBOL(pci_iomap_range);

void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	return pci_iomap_range(dev, bar, 0, maxlen);
}
EXPORT_SYMBOL(pci_iomap);

void pci_iounmap(struct pci_dev *pdev, void __iomem *addr)
{
	unsigned int idx;

	idx = (((__force u64) addr) & ~ZPCI_IOMAP_ADDR_BASE) >> 48;
	spin_lock(&zpci_iomap_lock);
	/* Detect underrun */
	BUG_ON(!zpci_iomap_start[idx].count);
	if (!--zpci_iomap_start[idx].count) {
		zpci_iomap_start[idx].fh = 0;
		zpci_iomap_start[idx].bar = 0;
	}
	spin_unlock(&zpci_iomap_lock);
}
EXPORT_SYMBOL(pci_iounmap);

static int pci_read(struct pci_bus *bus, unsigned int devfn, int where,
		    int size, u32 *val)
{
	struct zpci_dev *zdev = get_zdev_by_bus(bus);
	int ret;

	if (!zdev || devfn != ZPCI_DEVFN)
		ret = -ENODEV;
	else
		ret = zpci_cfg_load(zdev, where, val, size);

	return ret;
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where,
		     int size, u32 val)
{
	struct zpci_dev *zdev = get_zdev_by_bus(bus);
	int ret;

	if (!zdev || devfn != ZPCI_DEVFN)
		ret = -ENODEV;
	else
		ret = zpci_cfg_store(zdev, where, val, size);

	return ret;
}

static struct pci_ops pci_root_ops = {
	.read = pci_read,
	.write = pci_write,
};

static void zpci_irq_handler(struct airq_struct *airq)
{
	unsigned long si, ai;
	struct airq_iv *aibv;
	int irqs_on = 0;

	inc_irq_stat(IRQIO_PCI);
	for (si = 0;;) {
		/* Scan adapter summary indicator bit vector */
		si = airq_iv_scan(zpci_aisb_iv, si, airq_iv_end(zpci_aisb_iv));
		if (si == -1UL) {
			if (irqs_on++)
				/* End of second scan with interrupts on. */
				break;
			/* First scan complete, reenable interrupts. */
			if (zpci_set_irq_ctrl(SIC_IRQ_MODE_SINGLE, NULL, PCI_ISC))
				break;
			si = 0;
			continue;
		}

		/* Scan the adapter interrupt vector for this device. */
		aibv = zpci_aibv[si];
		for (ai = 0;;) {
			ai = airq_iv_scan(aibv, ai, airq_iv_end(aibv));
			if (ai == -1UL)
				break;
			inc_irq_stat(IRQIO_MSI);
			airq_iv_lock(aibv, ai);
			generic_handle_irq(airq_iv_get_data(aibv, ai));
			airq_iv_unlock(aibv, ai);
		}
	}
}

int arch_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	struct zpci_dev *zdev = to_zpci(pdev);
	unsigned int hwirq, msi_vecs;
	unsigned long aisb;
	struct msi_desc *msi;
	struct msi_msg msg;
	int rc, irq;

	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;
	msi_vecs = min_t(unsigned int, nvec, zdev->max_msi);

	/* Allocate adapter summary indicator bit */
	rc = -EIO;
	aisb = airq_iv_alloc_bit(zpci_aisb_iv);
	if (aisb == -1UL)
		goto out;
	zdev->aisb = aisb;

	/* Create adapter interrupt vector */
	rc = -ENOMEM;
	zdev->aibv = airq_iv_create(msi_vecs, AIRQ_IV_DATA | AIRQ_IV_BITLOCK);
	if (!zdev->aibv)
		goto out_si;

	/* Wire up shortcut pointer */
	zpci_aibv[aisb] = zdev->aibv;

	/* Request MSI interrupts */
	hwirq = 0;
	for_each_pci_msi_entry(msi, pdev) {
		rc = -EIO;
		if (hwirq >= msi_vecs)
			break;
		irq = irq_alloc_desc(0);	/* Alloc irq on node 0 */
		if (irq < 0)
			goto out_msi;
		rc = irq_set_msi_desc(irq, msi);
		if (rc)
			goto out_msi;
		irq_set_chip_and_handler(irq, &zpci_irq_chip,
					 handle_simple_irq);
		msg.data = hwirq;
		msg.address_lo = zdev->msi_addr & 0xffffffff;
		msg.address_hi = zdev->msi_addr >> 32;
		pci_write_msi_msg(irq, &msg);
		airq_iv_set_data(zdev->aibv, hwirq, irq);
		hwirq++;
	}

	/* Enable adapter interrupts */
	rc = zpci_set_airq(zdev);
	if (rc)
		goto out_msi;

	return (msi_vecs == nvec) ? 0 : msi_vecs;

out_msi:
	for_each_pci_msi_entry(msi, pdev) {
		if (hwirq-- == 0)
			break;
		irq_set_msi_desc(msi->irq, NULL);
		irq_free_desc(msi->irq);
		msi->msg.address_lo = 0;
		msi->msg.address_hi = 0;
		msi->msg.data = 0;
		msi->irq = 0;
	}
	zpci_aibv[aisb] = NULL;
	airq_iv_release(zdev->aibv);
out_si:
	airq_iv_free_bit(zpci_aisb_iv, aisb);
out:
	return rc;
}

void arch_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = to_zpci(pdev);
	struct msi_desc *msi;
	int rc;

	/* Disable adapter interrupts */
	rc = zpci_clear_airq(zdev);
	if (rc)
		return;

	/* Release MSI interrupts */
	for_each_pci_msi_entry(msi, pdev) {
		if (msi->msi_attrib.is_msix)
			__pci_msix_desc_mask_irq(msi, 1);
		else
			__pci_msi_desc_mask_irq(msi, 1, 1);
		irq_set_msi_desc(msi->irq, NULL);
		irq_free_desc(msi->irq);
		msi->msg.address_lo = 0;
		msi->msg.address_hi = 0;
		msi->msg.data = 0;
		msi->irq = 0;
	}

	zpci_aibv[zdev->aisb] = NULL;
	airq_iv_release(zdev->aibv);
	airq_iv_free_bit(zpci_aisb_iv, zdev->aisb);
}

static void zpci_map_resources(struct pci_dev *pdev)
{
	resource_size_t len;
	int i;

	for (i = 0; i < PCI_BAR_COUNT; i++) {
		len = pci_resource_len(pdev, i);
		if (!len)
			continue;
		pdev->resource[i].start =
			(resource_size_t __force) pci_iomap(pdev, i, 0);
		pdev->resource[i].end = pdev->resource[i].start + len - 1;
	}
}

static void zpci_unmap_resources(struct pci_dev *pdev)
{
	resource_size_t len;
	int i;

	for (i = 0; i < PCI_BAR_COUNT; i++) {
		len = pci_resource_len(pdev, i);
		if (!len)
			continue;
		pci_iounmap(pdev, (void __iomem __force *)
			    pdev->resource[i].start);
	}
}

static int __init zpci_irq_init(void)
{
	int rc;

	rc = register_adapter_interrupt(&zpci_airq);
	if (rc)
		goto out;
	/* Set summary to 1 to be called every time for the ISC. */
	*zpci_airq.lsi_ptr = 1;

	rc = -ENOMEM;
	zpci_aisb_iv = airq_iv_create(ZPCI_NR_DEVICES, AIRQ_IV_ALLOC);
	if (!zpci_aisb_iv)
		goto out_airq;

	zpci_set_irq_ctrl(SIC_IRQ_MODE_SINGLE, NULL, PCI_ISC);
	return 0;

out_airq:
	unregister_adapter_interrupt(&zpci_airq);
out:
	return rc;
}

static void zpci_irq_exit(void)
{
	airq_iv_release(zpci_aisb_iv);
	unregister_adapter_interrupt(&zpci_airq);
}

static int zpci_alloc_iomap(struct zpci_dev *zdev)
{
	int entry;

	spin_lock(&zpci_iomap_lock);
	entry = find_first_zero_bit(zpci_iomap, ZPCI_IOMAP_MAX_ENTRIES);
	if (entry == ZPCI_IOMAP_MAX_ENTRIES) {
		spin_unlock(&zpci_iomap_lock);
		return -ENOSPC;
	}
	set_bit(entry, zpci_iomap);
	spin_unlock(&zpci_iomap_lock);
	return entry;
}

static void zpci_free_iomap(struct zpci_dev *zdev, int entry)
{
	spin_lock(&zpci_iomap_lock);
	memset(&zpci_iomap_start[entry], 0, sizeof(struct zpci_iomap_entry));
	clear_bit(entry, zpci_iomap);
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

static int zpci_setup_bus_resources(struct zpci_dev *zdev,
				    struct list_head *resources)
{
	unsigned long addr, size, flags;
	struct resource *res;
	int i, entry;

	snprintf(zdev->res_name, sizeof(zdev->res_name),
		 "PCI Bus %04x:%02x", zdev->domain, ZPCI_BUS_NR);

	for (i = 0; i < PCI_BAR_COUNT; i++) {
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

		addr = ZPCI_IOMAP_ADDR_BASE + ((u64) entry << 48);

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

	for (i = 0; i < PCI_BAR_COUNT; i++) {
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

	zdev->pdev = pdev;
	pdev->dev.groups = zpci_attr_groups;
	zpci_map_resources(pdev);

	for (i = 0; i < PCI_BAR_COUNT; i++) {
		res = &pdev->resource[i];
		if (res->parent || !res->flags)
			continue;
		pci_claim_resource(pdev, i);
	}

	return 0;
}

void pcibios_release_device(struct pci_dev *pdev)
{
	zpci_unmap_resources(pdev);
}

int pcibios_enable_device(struct pci_dev *pdev, int mask)
{
	struct zpci_dev *zdev = to_zpci(pdev);

	zdev->pdev = pdev;
	zpci_debug_init_device(zdev);
	zpci_fmb_enable_device(zdev);

	return pci_enable_resources(pdev, mask);
}

void pcibios_disable_device(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = to_zpci(pdev);

	zpci_fmb_disable_device(zdev);
	zpci_debug_exit_device(zdev);
	zdev->pdev = NULL;
}

#ifdef CONFIG_HIBERNATE_CALLBACKS
static int zpci_restore(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct zpci_dev *zdev = to_zpci(pdev);
	int ret = 0;

	if (zdev->state != ZPCI_FN_STATE_ONLINE)
		goto out;

	ret = clp_enable_fh(zdev, ZPCI_NR_DMA_SPACES);
	if (ret)
		goto out;

	zpci_map_resources(pdev);
	zpci_register_ioat(zdev, 0, zdev->start_dma, zdev->end_dma,
			   (u64) zdev->dma_table);

out:
	return ret;
}

static int zpci_freeze(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct zpci_dev *zdev = to_zpci(pdev);

	if (zdev->state != ZPCI_FN_STATE_ONLINE)
		return 0;

	zpci_unregister_ioat(zdev, 0);
	zpci_unmap_resources(pdev);
	return clp_disable_fh(zdev);
}

struct dev_pm_ops pcibios_pm_ops = {
	.thaw_noirq = zpci_restore,
	.freeze_noirq = zpci_freeze,
	.restore_noirq = zpci_restore,
	.poweroff_noirq = zpci_freeze,
};
#endif /* CONFIG_HIBERNATE_CALLBACKS */

static int zpci_alloc_domain(struct zpci_dev *zdev)
{
	spin_lock(&zpci_domain_lock);
	zdev->domain = find_first_zero_bit(zpci_domain, ZPCI_NR_DEVICES);
	if (zdev->domain == ZPCI_NR_DEVICES) {
		spin_unlock(&zpci_domain_lock);
		return -ENOSPC;
	}
	set_bit(zdev->domain, zpci_domain);
	spin_unlock(&zpci_domain_lock);
	return 0;
}

static void zpci_free_domain(struct zpci_dev *zdev)
{
	spin_lock(&zpci_domain_lock);
	clear_bit(zdev->domain, zpci_domain);
	spin_unlock(&zpci_domain_lock);
}

void pcibios_remove_bus(struct pci_bus *bus)
{
	struct zpci_dev *zdev = get_zdev_by_bus(bus);

	zpci_exit_slot(zdev);
	zpci_cleanup_bus_resources(zdev);
	zpci_free_domain(zdev);

	spin_lock(&zpci_list_lock);
	list_del(&zdev->entry);
	spin_unlock(&zpci_list_lock);

	kfree(zdev);
}

static int zpci_scan_bus(struct zpci_dev *zdev)
{
	LIST_HEAD(resources);
	int ret;

	ret = zpci_setup_bus_resources(zdev, &resources);
	if (ret)
		goto error;

	zdev->bus = pci_scan_root_bus(NULL, ZPCI_BUS_NR, &pci_root_ops,
				      zdev, &resources);
	if (!zdev->bus) {
		ret = -EIO;
		goto error;
	}
	zdev->bus->max_bus_speed = zdev->max_bus_speed;
	pci_bus_add_devices(zdev->bus);
	return 0;

error:
	zpci_cleanup_bus_resources(zdev);
	pci_free_resource_list(&resources);
	return ret;
}

int zpci_enable_device(struct zpci_dev *zdev)
{
	int rc;

	rc = clp_enable_fh(zdev, ZPCI_NR_DMA_SPACES);
	if (rc)
		goto out;

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
	return clp_disable_fh(zdev);
}
EXPORT_SYMBOL_GPL(zpci_disable_device);

int zpci_create_device(struct zpci_dev *zdev)
{
	int rc;

	rc = zpci_alloc_domain(zdev);
	if (rc)
		goto out;

	mutex_init(&zdev->lock);
	if (zdev->state == ZPCI_FN_STATE_CONFIGURED) {
		rc = zpci_enable_device(zdev);
		if (rc)
			goto out_free;
	}
	rc = zpci_scan_bus(zdev);
	if (rc)
		goto out_disable;

	spin_lock(&zpci_list_lock);
	list_add_tail(&zdev->entry, &zpci_list);
	spin_unlock(&zpci_list_lock);

	zpci_init_slot(zdev);

	return 0;

out_disable:
	if (zdev->state == ZPCI_FN_STATE_ONLINE)
		zpci_disable_device(zdev);
out_free:
	zpci_free_domain(zdev);
out:
	return rc;
}

void zpci_stop_device(struct zpci_dev *zdev)
{
	zpci_dma_exit_device(zdev);
	/*
	 * Note: SCLP disables fh via set-pci-fn so don't
	 * do that here.
	 */
}
EXPORT_SYMBOL_GPL(zpci_stop_device);

static inline int barsize(u8 size)
{
	return (size) ? (1 << size) >> 10 : 0;
}

static int zpci_mem_init(void)
{
	BUILD_BUG_ON(!is_power_of_2(__alignof__(struct zpci_fmb)) ||
		     __alignof__(struct zpci_fmb) < sizeof(struct zpci_fmb));

	zdev_fmb_cache = kmem_cache_create("PCI_FMB_cache", sizeof(struct zpci_fmb),
					   __alignof__(struct zpci_fmb), 0, NULL);
	if (!zdev_fmb_cache)
		goto error_zdev;

	/* TODO: use realloc */
	zpci_iomap_start = kzalloc(ZPCI_IOMAP_MAX_ENTRIES * sizeof(*zpci_iomap_start),
				   GFP_KERNEL);
	if (!zpci_iomap_start)
		goto error_iomap;
	return 0;

error_iomap:
	kmem_cache_destroy(zdev_fmb_cache);
error_zdev:
	return -ENOMEM;
}

static void zpci_mem_exit(void)
{
	kfree(zpci_iomap_start);
	kmem_cache_destroy(zdev_fmb_cache);
}

static unsigned int s390_pci_probe = 1;
static unsigned int s390_pci_initialized;

char * __init pcibios_setup(char *str)
{
	if (!strcmp(str, "off")) {
		s390_pci_probe = 0;
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

void zpci_rescan(void)
{
	if (zpci_is_enabled())
		clp_rescan_pci_devices_simple();
}
