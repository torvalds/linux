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

#define COMPONENT "zPCI"
#define pr_fmt(fmt) COMPONENT ": " fmt

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
#define ZPCI_MSI_VEC_BITS		6
#define ZPCI_NR_DEVICES			CONFIG_PCI_NR_FUNCTIONS

/* list of all detected zpci devices */
LIST_HEAD(zpci_list);
EXPORT_SYMBOL_GPL(zpci_list);
DEFINE_MUTEX(zpci_list_lock);
EXPORT_SYMBOL_GPL(zpci_list_lock);

static struct pci_hp_callback_ops *hotplug_ops;

static DECLARE_BITMAP(zpci_domain, ZPCI_NR_DEVICES);
static DEFINE_SPINLOCK(zpci_domain_lock);

struct callback {
	irq_handler_t	handler;
	void		*data;
};

struct zdev_irq_map {
	unsigned long	aibv;		/* AI bit vector */
	int		msi_vecs;	/* consecutive MSI-vectors used */
	int		__unused;
	struct callback	cb[ZPCI_NR_MSI_VECS]; /* callback handler array */
	spinlock_t	lock;		/* protect callbacks against de-reg */
};

struct intr_bucket {
	/* amap of adapters, one bit per dev, corresponds to one irq nr */
	unsigned long	*alloc;
	/* AI summary bit, global page for all devices */
	unsigned long	*aisb;
	/* pointer to aibv and callback data in zdev */
	struct zdev_irq_map *imap[ZPCI_NR_DEVICES];
	/* protects the whole bucket struct */
	spinlock_t	lock;
};

static struct intr_bucket *bucket;

/* Adapter local summary indicator */
static u8 *zpci_irq_si;

static atomic_t irq_retries = ATOMIC_INIT(0);

/* I/O Map */
static DEFINE_SPINLOCK(zpci_iomap_lock);
static DECLARE_BITMAP(zpci_iomap, ZPCI_IOMAP_MAX_ENTRIES);
struct zpci_iomap_entry *zpci_iomap_start;
EXPORT_SYMBOL_GPL(zpci_iomap_start);

/* highest irq summary bit */
static int __read_mostly aisb_max;

static struct kmem_cache *zdev_irq_cache;
static struct kmem_cache *zdev_fmb_cache;

static inline int irq_to_msi_nr(unsigned int irq)
{
	return irq & ZPCI_MSI_MASK;
}

static inline int irq_to_dev_nr(unsigned int irq)
{
	return irq >> ZPCI_MSI_VEC_BITS;
}

static inline struct zdev_irq_map *get_imap(unsigned int irq)
{
	return bucket->imap[irq_to_dev_nr(irq)];
}

struct zpci_dev *get_zdev(struct pci_dev *pdev)
{
	return (struct zpci_dev *) pdev->sysdata;
}

struct zpci_dev *get_zdev_by_fid(u32 fid)
{
	struct zpci_dev *tmp, *zdev = NULL;

	mutex_lock(&zpci_list_lock);
	list_for_each_entry(tmp, &zpci_list, entry) {
		if (tmp->fid == fid) {
			zdev = tmp;
			break;
		}
	}
	mutex_unlock(&zpci_list_lock);
	return zdev;
}

bool zpci_fid_present(u32 fid)
{
	return (get_zdev_by_fid(fid) != NULL) ? true : false;
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
static int zpci_register_airq(struct zpci_dev *zdev, unsigned int aisb,
			      u64 aibv)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_REG_INT);
	struct zpci_fib *fib;
	int rc;

	fib = (void *) get_zeroed_page(GFP_KERNEL);
	if (!fib)
		return -ENOMEM;

	fib->isc = PCI_ISC;
	fib->noi = zdev->irq_map->msi_vecs;
	fib->sum = 1;		/* enable summary notifications */
	fib->aibv = aibv;
	fib->aibvo = 0;		/* every function has its own page */
	fib->aisb = (u64) bucket->aisb + aisb / 8;
	fib->aisbo = aisb & ZPCI_MSI_MASK;

	rc = s390pci_mod_fc(req, fib);
	pr_debug("%s mpcifc returned noi: %d\n", __func__, fib->noi);

	free_page((unsigned long) fib);
	return rc;
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
	struct zpci_fib *fib;
	int rc;

	/* The FIB must be available even if it's not used */
	fib = (void *) get_zeroed_page(GFP_KERNEL);
	if (!fib)
		return -ENOMEM;

	fib->pba = args->base;
	fib->pal = args->limit;
	fib->iota = args->iota;
	fib->fmb_addr = args->fmb_addr;

	rc = s390pci_mod_fc(req, fib);
	free_page((unsigned long) fib);
	return rc;
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
static int zpci_unregister_airq(struct zpci_dev *zdev)
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

	rc = s390pci_load(&data, req, offset);
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
	rc = s390pci_store(data, req, offset);
	return rc;
}

void enable_irq(unsigned int irq)
{
	struct msi_desc *msi = irq_get_msi_desc(irq);

	zpci_msi_set_mask_bits(msi, 1, 0);
}
EXPORT_SYMBOL_GPL(enable_irq);

void disable_irq(unsigned int irq)
{
	struct msi_desc *msi = irq_get_msi_desc(irq);

	zpci_msi_set_mask_bits(msi, 1, 1);
}
EXPORT_SYMBOL_GPL(disable_irq);

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
void __iomem *pci_iomap(struct pci_dev *pdev, int bar, unsigned long max)
{
	struct zpci_dev *zdev =	get_zdev(pdev);
	u64 addr;
	int idx;

	if ((bar & 7) != bar)
		return NULL;

	idx = zdev->bars[bar].map_idx;
	spin_lock(&zpci_iomap_lock);
	zpci_iomap_start[idx].fh = zdev->fh;
	zpci_iomap_start[idx].bar = bar;
	spin_unlock(&zpci_iomap_lock);

	addr = ZPCI_IOMAP_ADDR_BASE | ((u64) idx << 48);
	return (void __iomem *) addr;
}
EXPORT_SYMBOL_GPL(pci_iomap);

void pci_iounmap(struct pci_dev *pdev, void __iomem *addr)
{
	unsigned int idx;

	idx = (((__force u64) addr) & ~ZPCI_IOMAP_ADDR_BASE) >> 48;
	spin_lock(&zpci_iomap_lock);
	zpci_iomap_start[idx].fh = 0;
	zpci_iomap_start[idx].bar = 0;
	spin_unlock(&zpci_iomap_lock);
}
EXPORT_SYMBOL_GPL(pci_iounmap);

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

/* store the last handled bit to implement fair scheduling of devices */
static DEFINE_PER_CPU(unsigned long, next_sbit);

static void zpci_irq_handler(void *dont, void *need)
{
	unsigned long sbit, mbit, last = 0, start = __get_cpu_var(next_sbit);
	int rescan = 0, max = aisb_max;
	struct zdev_irq_map *imap;

	inc_irq_stat(IRQIO_PCI);
	sbit = start;

scan:
	/* find summary_bit */
	for_each_set_bit_left_cont(sbit, bucket->aisb, max) {
		clear_bit(63 - (sbit & 63), bucket->aisb + (sbit >> 6));
		last = sbit;

		/* find vector bit */
		imap = bucket->imap[sbit];
		for_each_set_bit_left(mbit, &imap->aibv, imap->msi_vecs) {
			inc_irq_stat(IRQIO_MSI);
			clear_bit(63 - mbit, &imap->aibv);

			spin_lock(&imap->lock);
			if (imap->cb[mbit].handler)
				imap->cb[mbit].handler(mbit,
					imap->cb[mbit].data);
			spin_unlock(&imap->lock);
		}
	}

	if (rescan)
		goto out;

	/* scan the skipped bits */
	if (start > 0) {
		sbit = 0;
		max = start;
		start = 0;
		goto scan;
	}

	/* enable interrupts again */
	set_irq_ctrl(SIC_IRQ_MODE_SINGLE, NULL, PCI_ISC);

	/* check again to not lose initiative */
	rmb();
	max = aisb_max;
	sbit = find_first_bit_left(bucket->aisb, max);
	if (sbit != max) {
		atomic_inc(&irq_retries);
		rescan++;
		goto scan;
	}
out:
	/* store next device bit to scan */
	__get_cpu_var(next_sbit) = (++last >= aisb_max) ? 0 : last;
}

/* msi_vecs - number of requested interrupts, 0 place function to error state */
static int zpci_setup_msi(struct pci_dev *pdev, int msi_vecs)
{
	struct zpci_dev *zdev = get_zdev(pdev);
	unsigned int aisb, msi_nr;
	struct msi_desc *msi;
	int rc;

	/* store the number of used MSI vectors */
	zdev->irq_map->msi_vecs = min(msi_vecs, ZPCI_NR_MSI_VECS);

	spin_lock(&bucket->lock);
	aisb = find_first_zero_bit(bucket->alloc, PAGE_SIZE);
	/* alloc map exhausted? */
	if (aisb == PAGE_SIZE) {
		spin_unlock(&bucket->lock);
		return -EIO;
	}
	set_bit(aisb, bucket->alloc);
	spin_unlock(&bucket->lock);

	zdev->aisb = aisb;
	if (aisb + 1 > aisb_max)
		aisb_max = aisb + 1;

	/* wire up IRQ shortcut pointer */
	bucket->imap[zdev->aisb] = zdev->irq_map;
	pr_debug("%s: imap[%u] linked to %p\n", __func__, zdev->aisb, zdev->irq_map);

	/* TODO: irq number 0 wont be found if we return less than requested MSIs.
	 * ignore it for now and fix in common code.
	 */
	msi_nr = aisb << ZPCI_MSI_VEC_BITS;

	list_for_each_entry(msi, &pdev->msi_list, list) {
		rc = zpci_setup_msi_irq(zdev, msi, msi_nr,
					  aisb << ZPCI_MSI_VEC_BITS);
		if (rc)
			return rc;
		msi_nr++;
	}

	rc = zpci_register_airq(zdev, aisb, (u64) &zdev->irq_map->aibv);
	if (rc) {
		clear_bit(aisb, bucket->alloc);
		dev_err(&pdev->dev, "register MSI failed with: %d\n", rc);
		return rc;
	}
	return (zdev->irq_map->msi_vecs == msi_vecs) ?
		0 : zdev->irq_map->msi_vecs;
}

static void zpci_teardown_msi(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = get_zdev(pdev);
	struct msi_desc *msi;
	int aisb, rc;

	rc = zpci_unregister_airq(zdev);
	if (rc) {
		dev_err(&pdev->dev, "deregister MSI failed with: %d\n", rc);
		return;
	}

	msi = list_first_entry(&pdev->msi_list, struct msi_desc, list);
	aisb = irq_to_dev_nr(msi->irq);

	list_for_each_entry(msi, &pdev->msi_list, list)
		zpci_teardown_msi_irq(zdev, msi);

	clear_bit(aisb, bucket->alloc);
	if (aisb + 1 == aisb_max)
		aisb_max--;
}

int arch_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	pr_debug("%s: requesting %d MSI-X interrupts...", __func__, nvec);
	if (type != PCI_CAP_ID_MSIX && type != PCI_CAP_ID_MSI)
		return -EINVAL;
	return zpci_setup_msi(pdev, nvec);
}

void arch_teardown_msi_irqs(struct pci_dev *pdev)
{
	pr_info("%s: on pdev: %p\n", __func__, pdev);
	zpci_teardown_msi(pdev);
}

static void zpci_map_resources(struct zpci_dev *zdev)
{
	struct pci_dev *pdev = zdev->pdev;
	resource_size_t len;
	int i;

	for (i = 0; i < PCI_BAR_COUNT; i++) {
		len = pci_resource_len(pdev, i);
		if (!len)
			continue;
		pdev->resource[i].start = (resource_size_t) pci_iomap(pdev, i, 0);
		pdev->resource[i].end = pdev->resource[i].start + len - 1;
		pr_debug("BAR%i: -> start: %Lx  end: %Lx\n",
			i, pdev->resource[i].start, pdev->resource[i].end);
	}
};

struct zpci_dev *zpci_alloc_device(void)
{
	struct zpci_dev *zdev;

	/* Alloc memory for our private pci device data */
	zdev = kzalloc(sizeof(*zdev), GFP_KERNEL);
	if (!zdev)
		return ERR_PTR(-ENOMEM);

	/* Alloc aibv & callback space */
	zdev->irq_map = kmem_cache_zalloc(zdev_irq_cache, GFP_KERNEL);
	if (!zdev->irq_map)
		goto error;
	WARN_ON((u64) zdev->irq_map & 0xff);
	return zdev;

error:
	kfree(zdev);
	return ERR_PTR(-ENOMEM);
}

void zpci_free_device(struct zpci_dev *zdev)
{
	kmem_cache_free(zdev_irq_cache, zdev->irq_map);
	kfree(zdev);
}

/*
 * Too late for any s390 specific setup, since interrupts must be set up
 * already which requires DMA setup too and the pci scan will access the
 * config space, which only works if the function handle is enabled.
 */
int pcibios_enable_device(struct pci_dev *pdev, int mask)
{
	struct resource *res;
	u16 cmd;
	int i;

	pci_read_config_word(pdev, PCI_COMMAND, &cmd);

	for (i = 0; i < PCI_BAR_COUNT; i++) {
		res = &pdev->resource[i];

		if (res->flags & IORESOURCE_IO)
			return -EINVAL;

		if (res->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	pci_write_config_word(pdev, PCI_COMMAND, cmd);
	return 0;
}

int pcibios_add_platform_entries(struct pci_dev *pdev)
{
	return zpci_sysfs_add_device(&pdev->dev);
}

int zpci_request_irq(unsigned int irq, irq_handler_t handler, void *data)
{
	int msi_nr = irq_to_msi_nr(irq);
	struct zdev_irq_map *imap;
	struct msi_desc *msi;

	msi = irq_get_msi_desc(irq);
	if (!msi)
		return -EIO;

	imap = get_imap(irq);
	spin_lock_init(&imap->lock);

	pr_debug("%s: register handler for IRQ:MSI %d:%d\n", __func__, irq >> 6, msi_nr);
	imap->cb[msi_nr].handler = handler;
	imap->cb[msi_nr].data = data;

	/*
	 * The generic MSI code returns with the interrupt disabled on the
	 * card, using the MSI mask bits. Firmware doesn't appear to unmask
	 * at that level, so we do it here by hand.
	 */
	zpci_msi_set_mask_bits(msi, 1, 0);
	return 0;
}

void zpci_free_irq(unsigned int irq)
{
	struct zdev_irq_map *imap = get_imap(irq);
	int msi_nr = irq_to_msi_nr(irq);
	unsigned long flags;

	pr_debug("%s: for irq: %d\n", __func__, irq);

	spin_lock_irqsave(&imap->lock, flags);
	imap->cb[msi_nr].handler = NULL;
	imap->cb[msi_nr].data = NULL;
	spin_unlock_irqrestore(&imap->lock, flags);
}

int request_irq(unsigned int irq, irq_handler_t handler,
		unsigned long irqflags, const char *devname, void *dev_id)
{
	pr_debug("%s: irq: %d  handler: %p  flags: %lx  dev: %s\n",
		__func__, irq, handler, irqflags, devname);

	return zpci_request_irq(irq, handler, dev_id);
}
EXPORT_SYMBOL_GPL(request_irq);

void free_irq(unsigned int irq, void *dev_id)
{
	zpci_free_irq(irq);
}
EXPORT_SYMBOL_GPL(free_irq);

static int __init zpci_irq_init(void)
{
	int cpu, rc;

	bucket = kzalloc(sizeof(*bucket), GFP_KERNEL);
	if (!bucket)
		return -ENOMEM;

	bucket->aisb = (unsigned long *) get_zeroed_page(GFP_KERNEL);
	if (!bucket->aisb) {
		rc = -ENOMEM;
		goto out_aisb;
	}

	bucket->alloc = (unsigned long *) get_zeroed_page(GFP_KERNEL);
	if (!bucket->alloc) {
		rc = -ENOMEM;
		goto out_alloc;
	}

	isc_register(PCI_ISC);
	zpci_irq_si = s390_register_adapter_interrupt(&zpci_irq_handler, NULL, PCI_ISC);
	if (IS_ERR(zpci_irq_si)) {
		rc = PTR_ERR(zpci_irq_si);
		zpci_irq_si = NULL;
		goto out_ai;
	}

	for_each_online_cpu(cpu)
		per_cpu(next_sbit, cpu) = 0;

	spin_lock_init(&bucket->lock);
	/* set summary to 1 to be called every time for the ISC */
	*zpci_irq_si = 1;
	set_irq_ctrl(SIC_IRQ_MODE_SINGLE, NULL, PCI_ISC);
	return 0;

out_ai:
	isc_unregister(PCI_ISC);
	free_page((unsigned long) bucket->alloc);
out_alloc:
	free_page((unsigned long) bucket->aisb);
out_aisb:
	kfree(bucket);
	return rc;
}

static void zpci_irq_exit(void)
{
	free_page((unsigned long) bucket->alloc);
	free_page((unsigned long) bucket->aisb);
	s390_unregister_adapter_interrupt(zpci_irq_si, PCI_ISC);
	isc_unregister(PCI_ISC);
	kfree(bucket);
}

void zpci_debug_info(struct zpci_dev *zdev, struct seq_file *m)
{
	if (!zdev)
		return;

	seq_printf(m, "global irq retries: %u\n", atomic_read(&irq_retries));
	seq_printf(m, "aibv[0]:%016lx  aibv[1]:%016lx  aisb:%016lx\n",
		   get_imap(0)->aibv, get_imap(1)->aibv, *bucket->aisb);
}

static struct resource *zpci_alloc_bus_resource(unsigned long start, unsigned long size,
						unsigned long flags, int domain)
{
	struct resource *r;
	char *name;
	int rc;

	r = kzalloc(sizeof(*r), GFP_KERNEL);
	if (!r)
		return ERR_PTR(-ENOMEM);
	r->start = start;
	r->end = r->start + size - 1;
	r->flags = flags;
	r->parent = &iomem_resource;
	name = kmalloc(18, GFP_KERNEL);
	if (!name) {
		kfree(r);
		return ERR_PTR(-ENOMEM);
	}
	sprintf(name, "PCI Bus: %04x:%02x", domain, ZPCI_BUS_NR);
	r->name = name;

	rc = request_resource(&iomem_resource, r);
	if (rc)
		pr_debug("request resource %pR failed\n", r);
	return r;
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

int pcibios_add_device(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = get_zdev(pdev);

	zdev->pdev = pdev;
	zpci_debug_init_device(zdev);
	zpci_fmb_enable_device(zdev);
	zpci_map_resources(zdev);

	return 0;
}

static int zpci_scan_bus(struct zpci_dev *zdev)
{
	struct resource *res;
	LIST_HEAD(resources);
	int i;

	/* allocate mapping entry for each used bar */
	for (i = 0; i < PCI_BAR_COUNT; i++) {
		unsigned long addr, size, flags;
		int entry;

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

		res = zpci_alloc_bus_resource(addr, size, flags, zdev->domain);
		if (IS_ERR(res)) {
			zpci_free_iomap(zdev, entry);
			return PTR_ERR(res);
		}
		pci_add_resource(&resources, res);
	}

	zdev->bus = pci_scan_root_bus(NULL, ZPCI_BUS_NR, &pci_root_ops,
				      zdev, &resources);
	if (!zdev->bus)
		return -EIO;

	zdev->bus->max_bus_speed = zdev->max_bus_speed;
	return 0;
}

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

int zpci_enable_device(struct zpci_dev *zdev)
{
	int rc;

	rc = clp_enable_fh(zdev, ZPCI_NR_DMA_SPACES);
	if (rc)
		goto out;
	pr_info("Enabled fh: 0x%x fid: 0x%x\n", zdev->fh, zdev->fid);

	rc = zpci_dma_init_device(zdev);
	if (rc)
		goto out_dma;
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

	if (zdev->state == ZPCI_FN_STATE_CONFIGURED) {
		rc = zpci_enable_device(zdev);
		if (rc)
			goto out_free;

		zdev->state = ZPCI_FN_STATE_ONLINE;
	}
	rc = zpci_scan_bus(zdev);
	if (rc)
		goto out_disable;

	mutex_lock(&zpci_list_lock);
	list_add_tail(&zdev->entry, &zpci_list);
	if (hotplug_ops)
		hotplug_ops->create_slot(zdev);
	mutex_unlock(&zpci_list_lock);

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

int zpci_scan_device(struct zpci_dev *zdev)
{
	zdev->pdev = pci_scan_single_device(zdev->bus, ZPCI_DEVFN);
	if (!zdev->pdev) {
		pr_err("pci_scan_single_device failed for fid: 0x%x\n",
			zdev->fid);
		goto out;
	}

	pci_bus_add_devices(zdev->bus);

	return 0;
out:
	zpci_dma_exit_device(zdev);
	clp_disable_fh(zdev);
	return -EIO;
}
EXPORT_SYMBOL_GPL(zpci_scan_device);

static inline int barsize(u8 size)
{
	return (size) ? (1 << size) >> 10 : 0;
}

static int zpci_mem_init(void)
{
	zdev_irq_cache = kmem_cache_create("PCI_IRQ_cache", sizeof(struct zdev_irq_map),
				L1_CACHE_BYTES, SLAB_HWCACHE_ALIGN, NULL);
	if (!zdev_irq_cache)
		goto error_zdev;

	zdev_fmb_cache = kmem_cache_create("PCI_FMB_cache", sizeof(struct zpci_fmb),
				16, 0, NULL);
	if (!zdev_fmb_cache)
		goto error_fmb;

	/* TODO: use realloc */
	zpci_iomap_start = kzalloc(ZPCI_IOMAP_MAX_ENTRIES * sizeof(*zpci_iomap_start),
				   GFP_KERNEL);
	if (!zpci_iomap_start)
		goto error_iomap;
	return 0;

error_iomap:
	kmem_cache_destroy(zdev_fmb_cache);
error_fmb:
	kmem_cache_destroy(zdev_irq_cache);
error_zdev:
	return -ENOMEM;
}

static void zpci_mem_exit(void)
{
	kfree(zpci_iomap_start);
	kmem_cache_destroy(zdev_irq_cache);
	kmem_cache_destroy(zdev_fmb_cache);
}

void zpci_register_hp_ops(struct pci_hp_callback_ops *ops)
{
	mutex_lock(&zpci_list_lock);
	hotplug_ops = ops;
	mutex_unlock(&zpci_list_lock);
}
EXPORT_SYMBOL_GPL(zpci_register_hp_ops);

void zpci_deregister_hp_ops(void)
{
	mutex_lock(&zpci_list_lock);
	hotplug_ops = NULL;
	mutex_unlock(&zpci_list_lock);
}
EXPORT_SYMBOL_GPL(zpci_deregister_hp_ops);

unsigned int s390_pci_probe;
EXPORT_SYMBOL_GPL(s390_pci_probe);

char * __init pcibios_setup(char *str)
{
	if (!strcmp(str, "on")) {
		s390_pci_probe = 1;
		return NULL;
	}
	return str;
}

static int __init pci_base_init(void)
{
	int rc;

	if (!s390_pci_probe)
		return 0;

	if (!test_facility(2) || !test_facility(69)
	    || !test_facility(71) || !test_facility(72))
		return 0;

	pr_info("Probing PCI hardware: PCI:%d  SID:%d  AEN:%d\n",
		test_facility(69), test_facility(70),
		test_facility(71));

	rc = zpci_debug_init();
	if (rc)
		return rc;

	rc = zpci_mem_init();
	if (rc)
		goto out_mem;

	rc = zpci_msihash_init();
	if (rc)
		goto out_hash;

	rc = zpci_irq_init();
	if (rc)
		goto out_irq;

	rc = zpci_dma_init();
	if (rc)
		goto out_dma;

	rc = clp_find_pci_devices();
	if (rc)
		goto out_find;

	return 0;

out_find:
	zpci_dma_exit();
out_dma:
	zpci_irq_exit();
out_irq:
	zpci_msihash_exit();
out_hash:
	zpci_mem_exit();
out_mem:
	zpci_debug_exit();
	return rc;
}
subsys_initcall(pci_base_init);
