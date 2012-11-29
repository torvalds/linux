/*
 * Copyright IBM Corp. 2012
 *
 * Author(s):
 *   Jan Glauber <jang@linux.vnet.ibm.com>
 *
 * The System z PCI code is a rewrite from a prototype by
 * the following people (Kudoz!):
 *   Alexander Schmidt <alexschm@de.ibm.com>
 *   Christoph Raisch <raisch@de.ibm.com>
 *   Hannes Hering <hering2@de.ibm.com>
 *   Hoang-Nam Nguyen <hnguyen@de.ibm.com>
 *   Jan-Bernd Themann <themann@de.ibm.com>
 *   Stefan Roscher <stefan.roscher@de.ibm.com>
 *   Thomas Klein <tklein@de.ibm.com>
 */

#define COMPONENT "zPCI"
#define pr_fmt(fmt) COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/pci.h>
#include <linux/msi.h>

#include <asm/facility.h>
#include <asm/pci_insn.h>

#define DEBUG				/* enable pr_debug */

#define ZPCI_NR_DMA_SPACES		1
#define ZPCI_NR_DEVICES			CONFIG_PCI_NR_FUNCTIONS

/* list of all detected zpci devices */
LIST_HEAD(zpci_list);
DEFINE_MUTEX(zpci_list_lock);

static DECLARE_BITMAP(zpci_domain, ZPCI_NR_DEVICES);
static DEFINE_SPINLOCK(zpci_domain_lock);

/* I/O Map */
static DEFINE_SPINLOCK(zpci_iomap_lock);
static DECLARE_BITMAP(zpci_iomap, ZPCI_IOMAP_MAX_ENTRIES);
struct zpci_iomap_entry *zpci_iomap_start;
EXPORT_SYMBOL_GPL(zpci_iomap_start);

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

/* Store PCI function information block */
static int zpci_store_fib(struct zpci_dev *zdev, u8 *fc)
{
	struct zpci_fib *fib;
	u8 status, cc;

	fib = (void *) get_zeroed_page(GFP_KERNEL);
	if (!fib)
		return -ENOMEM;

	do {
		cc = __stpcifc(zdev->fh, 0, fib, &status);
		if (cc == 2) {
			msleep(ZPCI_INSN_BUSY_DELAY);
			memset(fib, 0, PAGE_SIZE);
		}
	} while (cc == 2);

	if (cc)
		pr_err_once("%s: cc: %u  status: %u\n",
			    __func__, cc, status);

	/* Return PCI function controls */
	*fc = fib->fc;

	free_page((unsigned long) fib);
	return (cc) ? -EIO : 0;
}

#define ZPCI_PCIAS_CFGSPC	15

static int zpci_cfg_load(struct zpci_dev *zdev, int offset, u32 *val, u8 len)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, ZPCI_PCIAS_CFGSPC, len);
	u64 data;
	int rc;

	rc = pcilg_instr(&data, req, offset);
	data = data << ((8 - len) * 8);
	data = le64_to_cpu(data);
	if (!rc)
		*val = (u32) data;
	else
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
	rc = pcistg_instr(data, req, offset);
	return rc;
}

void __devinit pcibios_fixup_bus(struct pci_bus *bus)
{
}

resource_size_t pcibios_align_resource(void *data, const struct resource *res,
				       resource_size_t size,
				       resource_size_t align)
{
	return 0;
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

	if (!zdev || devfn != ZPCI_DEVFN)
		return 0;
	return zpci_cfg_load(zdev, where, val, size);
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where,
		     int size, u32 val)
{
	struct zpci_dev *zdev = get_zdev_by_bus(bus);

	if (!zdev || devfn != ZPCI_DEVFN)
		return 0;
	return zpci_cfg_store(zdev, where, val, size);
}

static struct pci_ops pci_root_ops = {
	.read = pci_read,
	.write = pci_write,
};

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

static void zpci_unmap_resources(struct pci_dev *pdev)
{
	resource_size_t len;
	int i;

	for (i = 0; i < PCI_BAR_COUNT; i++) {
		len = pci_resource_len(pdev, i);
		if (!len)
			continue;
		pci_iounmap(pdev, (void *) pdev->resource[i].start);
	}
};

struct zpci_dev *zpci_alloc_device(void)
{
	struct zpci_dev *zdev;

	/* Alloc memory for our private pci device data */
	zdev = kzalloc(sizeof(*zdev), GFP_KERNEL);
	if (!zdev)
		return ERR_PTR(-ENOMEM);
	return zdev;
}

void zpci_free_device(struct zpci_dev *zdev)
{
	kfree(zdev);
}

/* Called on removal of pci_dev, leaves zpci and bus device */
static void zpci_remove_device(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = get_zdev(pdev);

	dev_info(&pdev->dev, "Removing device %u\n", zdev->domain);
	zdev->state = ZPCI_FN_STATE_CONFIGURED;
	zpci_unmap_resources(pdev);
	list_del(&zdev->entry);		/* can be called from init */
	zdev->pdev = NULL;
}

static void zpci_scan_devices(void)
{
	struct zpci_dev *zdev;

	mutex_lock(&zpci_list_lock);
	list_for_each_entry(zdev, &zpci_list, entry)
		if (zdev->state == ZPCI_FN_STATE_CONFIGURED)
			zpci_scan_device(zdev);
	mutex_unlock(&zpci_list_lock);
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

void pcibios_disable_device(struct pci_dev *pdev)
{
	zpci_remove_device(pdev);
	pdev->sysdata = NULL;
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

static int zpci_create_device_bus(struct zpci_dev *zdev)
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

	zdev->bus = pci_create_root_bus(NULL, ZPCI_BUS_NR, &pci_root_ops,
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

int zpci_create_device(struct zpci_dev *zdev)
{
	int rc;

	rc = zpci_alloc_domain(zdev);
	if (rc)
		goto out;

	rc = zpci_create_device_bus(zdev);
	if (rc)
		goto out_bus;

	mutex_lock(&zpci_list_lock);
	list_add_tail(&zdev->entry, &zpci_list);
	mutex_unlock(&zpci_list_lock);

	if (zdev->state == ZPCI_FN_STATE_STANDBY)
		return 0;

	return 0;

out_bus:
	zpci_free_domain(zdev);
out:
	return rc;
}

void zpci_stop_device(struct zpci_dev *zdev)
{
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

	zpci_map_resources(zdev);
	pci_bus_add_devices(zdev->bus);

	/* now that pdev was added to the bus mark it as used */
	zdev->state = ZPCI_FN_STATE_ONLINE;
	return 0;

out:
	return -EIO;
}
EXPORT_SYMBOL_GPL(zpci_scan_device);

static inline int barsize(u8 size)
{
	return (size) ? (1 << size) >> 10 : 0;
}

static int zpci_mem_init(void)
{
	/* TODO: use realloc */
	zpci_iomap_start = kzalloc(ZPCI_IOMAP_MAX_ENTRIES * sizeof(*zpci_iomap_start),
				   GFP_KERNEL);
	if (!zpci_iomap_start)
		goto error_zdev;
	return 0;

error_zdev:
	return -ENOMEM;
}

static void zpci_mem_exit(void)
{
	kfree(zpci_iomap_start);
}

unsigned int pci_probe = 1;
EXPORT_SYMBOL_GPL(pci_probe);

char * __init pcibios_setup(char *str)
{
	if (!strcmp(str, "off")) {
		pci_probe = 0;
		return NULL;
	}
	return str;
}

static int __init pci_base_init(void)
{
	int rc;

	if (!pci_probe)
		return 0;

	if (!test_facility(2) || !test_facility(69)
	    || !test_facility(71) || !test_facility(72))
		return 0;

	pr_info("Probing PCI hardware: PCI:%d  SID:%d  AEN:%d\n",
		test_facility(69), test_facility(70),
		test_facility(71));

	rc = zpci_mem_init();
	if (rc)
		goto out_mem;

	zpci_scan_devices();
	return 0;

	zpci_mem_exit();
out_mem:
	return rc;
}
subsys_initcall(pci_base_init);
