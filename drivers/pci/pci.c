// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Bus Services, see include/linux/pci.h for further explanation.
 *
 * Copyright 1993 -- 1997 Drew Eckhardt, Frederic Potter,
 * David Mosberger-Tang
 *
 * Copyright 1997 -- 2000 Martin Mares <mj@ucw.cz>
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/log2.h>
#include <linux/logic_pio.h>
#include <linux/pm_wakeup.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/pci_hotplug.h>
#include <linux/vmalloc.h>
#include <asm/dma.h>
#include <linux/aer.h>
#include "pci.h"

DEFINE_MUTEX(pci_slot_mutex);

const char *pci_power_names[] = {
	"error", "D0", "D1", "D2", "D3hot", "D3cold", "unknown",
};
EXPORT_SYMBOL_GPL(pci_power_names);

int isa_dma_bridge_buggy;
EXPORT_SYMBOL(isa_dma_bridge_buggy);

int pci_pci_problems;
EXPORT_SYMBOL(pci_pci_problems);

unsigned int pci_pm_d3hot_delay;

static void pci_pme_list_scan(struct work_struct *work);

static LIST_HEAD(pci_pme_list);
static DEFINE_MUTEX(pci_pme_list_mutex);
static DECLARE_DELAYED_WORK(pci_pme_work, pci_pme_list_scan);

struct pci_pme_device {
	struct list_head list;
	struct pci_dev *dev;
};

#define PME_TIMEOUT 1000 /* How long between PME checks */

static void pci_dev_d3_sleep(struct pci_dev *dev)
{
	unsigned int delay = dev->d3hot_delay;

	if (delay < pci_pm_d3hot_delay)
		delay = pci_pm_d3hot_delay;

	if (delay)
		msleep(delay);
}

#ifdef CONFIG_PCI_DOMAINS
int pci_domains_supported = 1;
#endif

#define DEFAULT_CARDBUS_IO_SIZE		(256)
#define DEFAULT_CARDBUS_MEM_SIZE	(64*1024*1024)
/* pci=cbmemsize=nnM,cbiosize=nn can override this */
unsigned long pci_cardbus_io_size = DEFAULT_CARDBUS_IO_SIZE;
unsigned long pci_cardbus_mem_size = DEFAULT_CARDBUS_MEM_SIZE;

#define DEFAULT_HOTPLUG_IO_SIZE		(256)
#define DEFAULT_HOTPLUG_MMIO_SIZE	(2*1024*1024)
#define DEFAULT_HOTPLUG_MMIO_PREF_SIZE	(2*1024*1024)
/* hpiosize=nn can override this */
unsigned long pci_hotplug_io_size  = DEFAULT_HOTPLUG_IO_SIZE;
/*
 * pci=hpmmiosize=nnM overrides non-prefetchable MMIO size,
 * pci=hpmmioprefsize=nnM overrides prefetchable MMIO size;
 * pci=hpmemsize=nnM overrides both
 */
unsigned long pci_hotplug_mmio_size = DEFAULT_HOTPLUG_MMIO_SIZE;
unsigned long pci_hotplug_mmio_pref_size = DEFAULT_HOTPLUG_MMIO_PREF_SIZE;

#define DEFAULT_HOTPLUG_BUS_SIZE	1
unsigned long pci_hotplug_bus_size = DEFAULT_HOTPLUG_BUS_SIZE;


/* PCIe MPS/MRRS strategy; can be overridden by kernel command-line param */
#ifdef CONFIG_PCIE_BUS_TUNE_OFF
enum pcie_bus_config_types pcie_bus_config = PCIE_BUS_TUNE_OFF;
#elif defined CONFIG_PCIE_BUS_SAFE
enum pcie_bus_config_types pcie_bus_config = PCIE_BUS_SAFE;
#elif defined CONFIG_PCIE_BUS_PERFORMANCE
enum pcie_bus_config_types pcie_bus_config = PCIE_BUS_PERFORMANCE;
#elif defined CONFIG_PCIE_BUS_PEER2PEER
enum pcie_bus_config_types pcie_bus_config = PCIE_BUS_PEER2PEER;
#else
enum pcie_bus_config_types pcie_bus_config = PCIE_BUS_DEFAULT;
#endif

/*
 * The default CLS is used if arch didn't set CLS explicitly and not
 * all pci devices agree on the same value.  Arch can override either
 * the dfl or actual value as it sees fit.  Don't forget this is
 * measured in 32-bit words, not bytes.
 */
u8 pci_dfl_cache_line_size = L1_CACHE_BYTES >> 2;
u8 pci_cache_line_size;

/*
 * If we set up a device for bus mastering, we need to check the latency
 * timer as certain BIOSes forget to set it properly.
 */
unsigned int pcibios_max_latency = 255;

/* If set, the PCIe ARI capability will not be used. */
static bool pcie_ari_disabled;

/* If set, the PCIe ATS capability will not be used. */
static bool pcie_ats_disabled;

/* If set, the PCI config space of each device is printed during boot. */
bool pci_early_dump;

bool pci_ats_disabled(void)
{
	return pcie_ats_disabled;
}
EXPORT_SYMBOL_GPL(pci_ats_disabled);

/* Disable bridge_d3 for all PCIe ports */
static bool pci_bridge_d3_disable;
/* Force bridge_d3 for all PCIe ports */
static bool pci_bridge_d3_force;

static int __init pcie_port_pm_setup(char *str)
{
	if (!strcmp(str, "off"))
		pci_bridge_d3_disable = true;
	else if (!strcmp(str, "force"))
		pci_bridge_d3_force = true;
	return 1;
}
__setup("pcie_port_pm=", pcie_port_pm_setup);

/* Time to wait after a reset for device to become responsive */
#define PCIE_RESET_READY_POLL_MS 60000

/**
 * pci_bus_max_busnr - returns maximum PCI bus number of given bus' children
 * @bus: pointer to PCI bus structure to search
 *
 * Given a PCI bus, returns the highest PCI bus number present in the set
 * including the given PCI bus and its list of child PCI buses.
 */
unsigned char pci_bus_max_busnr(struct pci_bus *bus)
{
	struct pci_bus *tmp;
	unsigned char max, n;

	max = bus->busn_res.end;
	list_for_each_entry(tmp, &bus->children, node) {
		n = pci_bus_max_busnr(tmp);
		if (n > max)
			max = n;
	}
	return max;
}
EXPORT_SYMBOL_GPL(pci_bus_max_busnr);

/**
 * pci_status_get_and_clear_errors - return and clear error bits in PCI_STATUS
 * @pdev: the PCI device
 *
 * Returns error bits set in PCI_STATUS and clears them.
 */
int pci_status_get_and_clear_errors(struct pci_dev *pdev)
{
	u16 status;
	int ret;

	ret = pci_read_config_word(pdev, PCI_STATUS, &status);
	if (ret != PCIBIOS_SUCCESSFUL)
		return -EIO;

	status &= PCI_STATUS_ERROR_BITS;
	if (status)
		pci_write_config_word(pdev, PCI_STATUS, status);

	return status;
}
EXPORT_SYMBOL_GPL(pci_status_get_and_clear_errors);

#ifdef CONFIG_HAS_IOMEM
void __iomem *pci_ioremap_bar(struct pci_dev *pdev, int bar)
{
	struct resource *res = &pdev->resource[bar];

	/*
	 * Make sure the BAR is actually a memory resource, not an IO resource
	 */
	if (res->flags & IORESOURCE_UNSET || !(res->flags & IORESOURCE_MEM)) {
		pci_warn(pdev, "can't ioremap BAR %d: %pR\n", bar, res);
		return NULL;
	}
	return ioremap(res->start, resource_size(res));
}
EXPORT_SYMBOL_GPL(pci_ioremap_bar);

void __iomem *pci_ioremap_wc_bar(struct pci_dev *pdev, int bar)
{
	/*
	 * Make sure the BAR is actually a memory resource, not an IO resource
	 */
	if (!(pci_resource_flags(pdev, bar) & IORESOURCE_MEM)) {
		WARN_ON(1);
		return NULL;
	}
	return ioremap_wc(pci_resource_start(pdev, bar),
			  pci_resource_len(pdev, bar));
}
EXPORT_SYMBOL_GPL(pci_ioremap_wc_bar);
#endif

/**
 * pci_dev_str_match_path - test if a path string matches a device
 * @dev: the PCI device to test
 * @path: string to match the device against
 * @endptr: pointer to the string after the match
 *
 * Test if a string (typically from a kernel parameter) formatted as a
 * path of device/function addresses matches a PCI device. The string must
 * be of the form:
 *
 *   [<domain>:]<bus>:<device>.<func>[/<device>.<func>]*
 *
 * A path for a device can be obtained using 'lspci -t'.  Using a path
 * is more robust against bus renumbering than using only a single bus,
 * device and function address.
 *
 * Returns 1 if the string matches the device, 0 if it does not and
 * a negative error code if it fails to parse the string.
 */
static int pci_dev_str_match_path(struct pci_dev *dev, const char *path,
				  const char **endptr)
{
	int ret;
	int seg, bus, slot, func;
	char *wpath, *p;
	char end;

	*endptr = strchrnul(path, ';');

	wpath = kmemdup_nul(path, *endptr - path, GFP_KERNEL);
	if (!wpath)
		return -ENOMEM;

	while (1) {
		p = strrchr(wpath, '/');
		if (!p)
			break;
		ret = sscanf(p, "/%x.%x%c", &slot, &func, &end);
		if (ret != 2) {
			ret = -EINVAL;
			goto free_and_exit;
		}

		if (dev->devfn != PCI_DEVFN(slot, func)) {
			ret = 0;
			goto free_and_exit;
		}

		/*
		 * Note: we don't need to get a reference to the upstream
		 * bridge because we hold a reference to the top level
		 * device which should hold a reference to the bridge,
		 * and so on.
		 */
		dev = pci_upstream_bridge(dev);
		if (!dev) {
			ret = 0;
			goto free_and_exit;
		}

		*p = 0;
	}

	ret = sscanf(wpath, "%x:%x:%x.%x%c", &seg, &bus, &slot,
		     &func, &end);
	if (ret != 4) {
		seg = 0;
		ret = sscanf(wpath, "%x:%x.%x%c", &bus, &slot, &func, &end);
		if (ret != 3) {
			ret = -EINVAL;
			goto free_and_exit;
		}
	}

	ret = (seg == pci_domain_nr(dev->bus) &&
	       bus == dev->bus->number &&
	       dev->devfn == PCI_DEVFN(slot, func));

free_and_exit:
	kfree(wpath);
	return ret;
}

/**
 * pci_dev_str_match - test if a string matches a device
 * @dev: the PCI device to test
 * @p: string to match the device against
 * @endptr: pointer to the string after the match
 *
 * Test if a string (typically from a kernel parameter) matches a specified
 * PCI device. The string may be of one of the following formats:
 *
 *   [<domain>:]<bus>:<device>.<func>[/<device>.<func>]*
 *   pci:<vendor>:<device>[:<subvendor>:<subdevice>]
 *
 * The first format specifies a PCI bus/device/function address which
 * may change if new hardware is inserted, if motherboard firmware changes,
 * or due to changes caused in kernel parameters. If the domain is
 * left unspecified, it is taken to be 0.  In order to be robust against
 * bus renumbering issues, a path of PCI device/function numbers may be used
 * to address the specific device.  The path for a device can be determined
 * through the use of 'lspci -t'.
 *
 * The second format matches devices using IDs in the configuration
 * space which may match multiple devices in the system. A value of 0
 * for any field will match all devices. (Note: this differs from
 * in-kernel code that uses PCI_ANY_ID which is ~0; this is for
 * legacy reasons and convenience so users don't have to specify
 * FFFFFFFFs on the command line.)
 *
 * Returns 1 if the string matches the device, 0 if it does not and
 * a negative error code if the string cannot be parsed.
 */
static int pci_dev_str_match(struct pci_dev *dev, const char *p,
			     const char **endptr)
{
	int ret;
	int count;
	unsigned short vendor, device, subsystem_vendor, subsystem_device;

	if (strncmp(p, "pci:", 4) == 0) {
		/* PCI vendor/device (subvendor/subdevice) IDs are specified */
		p += 4;
		ret = sscanf(p, "%hx:%hx:%hx:%hx%n", &vendor, &device,
			     &subsystem_vendor, &subsystem_device, &count);
		if (ret != 4) {
			ret = sscanf(p, "%hx:%hx%n", &vendor, &device, &count);
			if (ret != 2)
				return -EINVAL;

			subsystem_vendor = 0;
			subsystem_device = 0;
		}

		p += count;

		if ((!vendor || vendor == dev->vendor) &&
		    (!device || device == dev->device) &&
		    (!subsystem_vendor ||
			    subsystem_vendor == dev->subsystem_vendor) &&
		    (!subsystem_device ||
			    subsystem_device == dev->subsystem_device))
			goto found;
	} else {
		/*
		 * PCI Bus, Device, Function IDs are specified
		 * (optionally, may include a path of devfns following it)
		 */
		ret = pci_dev_str_match_path(dev, p, &p);
		if (ret < 0)
			return ret;
		else if (ret)
			goto found;
	}

	*endptr = p;
	return 0;

found:
	*endptr = p;
	return 1;
}

static u8 __pci_find_next_cap_ttl(struct pci_bus *bus, unsigned int devfn,
				  u8 pos, int cap, int *ttl)
{
	u8 id;
	u16 ent;

	pci_bus_read_config_byte(bus, devfn, pos, &pos);

	while ((*ttl)--) {
		if (pos < 0x40)
			break;
		pos &= ~3;
		pci_bus_read_config_word(bus, devfn, pos, &ent);

		id = ent & 0xff;
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;
		pos = (ent >> 8);
	}
	return 0;
}

static u8 __pci_find_next_cap(struct pci_bus *bus, unsigned int devfn,
			      u8 pos, int cap)
{
	int ttl = PCI_FIND_CAP_TTL;

	return __pci_find_next_cap_ttl(bus, devfn, pos, cap, &ttl);
}

u8 pci_find_next_capability(struct pci_dev *dev, u8 pos, int cap)
{
	return __pci_find_next_cap(dev->bus, dev->devfn,
				   pos + PCI_CAP_LIST_NEXT, cap);
}
EXPORT_SYMBOL_GPL(pci_find_next_capability);

static u8 __pci_bus_find_cap_start(struct pci_bus *bus,
				    unsigned int devfn, u8 hdr_type)
{
	u16 status;

	pci_bus_read_config_word(bus, devfn, PCI_STATUS, &status);
	if (!(status & PCI_STATUS_CAP_LIST))
		return 0;

	switch (hdr_type) {
	case PCI_HEADER_TYPE_NORMAL:
	case PCI_HEADER_TYPE_BRIDGE:
		return PCI_CAPABILITY_LIST;
	case PCI_HEADER_TYPE_CARDBUS:
		return PCI_CB_CAPABILITY_LIST;
	}

	return 0;
}

/**
 * pci_find_capability - query for devices' capabilities
 * @dev: PCI device to query
 * @cap: capability code
 *
 * Tell if a device supports a given PCI capability.
 * Returns the address of the requested capability structure within the
 * device's PCI configuration space or 0 in case the device does not
 * support it.  Possible values for @cap include:
 *
 *  %PCI_CAP_ID_PM           Power Management
 *  %PCI_CAP_ID_AGP          Accelerated Graphics Port
 *  %PCI_CAP_ID_VPD          Vital Product Data
 *  %PCI_CAP_ID_SLOTID       Slot Identification
 *  %PCI_CAP_ID_MSI          Message Signalled Interrupts
 *  %PCI_CAP_ID_CHSWP        CompactPCI HotSwap
 *  %PCI_CAP_ID_PCIX         PCI-X
 *  %PCI_CAP_ID_EXP          PCI Express
 */
u8 pci_find_capability(struct pci_dev *dev, int cap)
{
	u8 pos;

	pos = __pci_bus_find_cap_start(dev->bus, dev->devfn, dev->hdr_type);
	if (pos)
		pos = __pci_find_next_cap(dev->bus, dev->devfn, pos, cap);

	return pos;
}
EXPORT_SYMBOL(pci_find_capability);

/**
 * pci_bus_find_capability - query for devices' capabilities
 * @bus: the PCI bus to query
 * @devfn: PCI device to query
 * @cap: capability code
 *
 * Like pci_find_capability() but works for PCI devices that do not have a
 * pci_dev structure set up yet.
 *
 * Returns the address of the requested capability structure within the
 * device's PCI configuration space or 0 in case the device does not
 * support it.
 */
u8 pci_bus_find_capability(struct pci_bus *bus, unsigned int devfn, int cap)
{
	u8 hdr_type, pos;

	pci_bus_read_config_byte(bus, devfn, PCI_HEADER_TYPE, &hdr_type);

	pos = __pci_bus_find_cap_start(bus, devfn, hdr_type & 0x7f);
	if (pos)
		pos = __pci_find_next_cap(bus, devfn, pos, cap);

	return pos;
}
EXPORT_SYMBOL(pci_bus_find_capability);

/**
 * pci_find_next_ext_capability - Find an extended capability
 * @dev: PCI device to query
 * @start: address at which to start looking (0 to start at beginning of list)
 * @cap: capability code
 *
 * Returns the address of the next matching extended capability structure
 * within the device's PCI configuration space or 0 if the device does
 * not support it.  Some capabilities can occur several times, e.g., the
 * vendor-specific capability, and this provides a way to find them all.
 */
u16 pci_find_next_ext_capability(struct pci_dev *dev, u16 start, int cap)
{
	u32 header;
	int ttl;
	u16 pos = PCI_CFG_SPACE_SIZE;

	/* minimum 8 bytes per capability */
	ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;

	if (dev->cfg_size <= PCI_CFG_SPACE_SIZE)
		return 0;

	if (start)
		pos = start;

	if (pci_read_config_dword(dev, pos, &header) != PCIBIOS_SUCCESSFUL)
		return 0;

	/*
	 * If we have no capabilities, this is indicated by cap ID,
	 * cap version and next pointer all being 0.
	 */
	if (header == 0)
		return 0;

	while (ttl-- > 0) {
		if (PCI_EXT_CAP_ID(header) == cap && pos != start)
			return pos;

		pos = PCI_EXT_CAP_NEXT(header);
		if (pos < PCI_CFG_SPACE_SIZE)
			break;

		if (pci_read_config_dword(dev, pos, &header) != PCIBIOS_SUCCESSFUL)
			break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pci_find_next_ext_capability);

/**
 * pci_find_ext_capability - Find an extended capability
 * @dev: PCI device to query
 * @cap: capability code
 *
 * Returns the address of the requested extended capability structure
 * within the device's PCI configuration space or 0 if the device does
 * not support it.  Possible values for @cap include:
 *
 *  %PCI_EXT_CAP_ID_ERR		Advanced Error Reporting
 *  %PCI_EXT_CAP_ID_VC		Virtual Channel
 *  %PCI_EXT_CAP_ID_DSN		Device Serial Number
 *  %PCI_EXT_CAP_ID_PWR		Power Budgeting
 */
u16 pci_find_ext_capability(struct pci_dev *dev, int cap)
{
	return pci_find_next_ext_capability(dev, 0, cap);
}
EXPORT_SYMBOL_GPL(pci_find_ext_capability);

/**
 * pci_get_dsn - Read and return the 8-byte Device Serial Number
 * @dev: PCI device to query
 *
 * Looks up the PCI_EXT_CAP_ID_DSN and reads the 8 bytes of the Device Serial
 * Number.
 *
 * Returns the DSN, or zero if the capability does not exist.
 */
u64 pci_get_dsn(struct pci_dev *dev)
{
	u32 dword;
	u64 dsn;
	int pos;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_DSN);
	if (!pos)
		return 0;

	/*
	 * The Device Serial Number is two dwords offset 4 bytes from the
	 * capability position. The specification says that the first dword is
	 * the lower half, and the second dword is the upper half.
	 */
	pos += 4;
	pci_read_config_dword(dev, pos, &dword);
	dsn = (u64)dword;
	pci_read_config_dword(dev, pos + 4, &dword);
	dsn |= ((u64)dword) << 32;

	return dsn;
}
EXPORT_SYMBOL_GPL(pci_get_dsn);

static u8 __pci_find_next_ht_cap(struct pci_dev *dev, u8 pos, int ht_cap)
{
	int rc, ttl = PCI_FIND_CAP_TTL;
	u8 cap, mask;

	if (ht_cap == HT_CAPTYPE_SLAVE || ht_cap == HT_CAPTYPE_HOST)
		mask = HT_3BIT_CAP_MASK;
	else
		mask = HT_5BIT_CAP_MASK;

	pos = __pci_find_next_cap_ttl(dev->bus, dev->devfn, pos,
				      PCI_CAP_ID_HT, &ttl);
	while (pos) {
		rc = pci_read_config_byte(dev, pos + 3, &cap);
		if (rc != PCIBIOS_SUCCESSFUL)
			return 0;

		if ((cap & mask) == ht_cap)
			return pos;

		pos = __pci_find_next_cap_ttl(dev->bus, dev->devfn,
					      pos + PCI_CAP_LIST_NEXT,
					      PCI_CAP_ID_HT, &ttl);
	}

	return 0;
}

/**
 * pci_find_next_ht_capability - query a device's HyperTransport capabilities
 * @dev: PCI device to query
 * @pos: Position from which to continue searching
 * @ht_cap: HyperTransport capability code
 *
 * To be used in conjunction with pci_find_ht_capability() to search for
 * all capabilities matching @ht_cap. @pos should always be a value returned
 * from pci_find_ht_capability().
 *
 * NB. To be 100% safe against broken PCI devices, the caller should take
 * steps to avoid an infinite loop.
 */
u8 pci_find_next_ht_capability(struct pci_dev *dev, u8 pos, int ht_cap)
{
	return __pci_find_next_ht_cap(dev, pos + PCI_CAP_LIST_NEXT, ht_cap);
}
EXPORT_SYMBOL_GPL(pci_find_next_ht_capability);

/**
 * pci_find_ht_capability - query a device's HyperTransport capabilities
 * @dev: PCI device to query
 * @ht_cap: HyperTransport capability code
 *
 * Tell if a device supports a given HyperTransport capability.
 * Returns an address within the device's PCI configuration space
 * or 0 in case the device does not support the request capability.
 * The address points to the PCI capability, of type PCI_CAP_ID_HT,
 * which has a HyperTransport capability matching @ht_cap.
 */
u8 pci_find_ht_capability(struct pci_dev *dev, int ht_cap)
{
	u8 pos;

	pos = __pci_bus_find_cap_start(dev->bus, dev->devfn, dev->hdr_type);
	if (pos)
		pos = __pci_find_next_ht_cap(dev, pos, ht_cap);

	return pos;
}
EXPORT_SYMBOL_GPL(pci_find_ht_capability);

/**
 * pci_find_vsec_capability - Find a vendor-specific extended capability
 * @dev: PCI device to query
 * @vendor: Vendor ID for which capability is defined
 * @cap: Vendor-specific capability ID
 *
 * If @dev has Vendor ID @vendor, search for a VSEC capability with
 * VSEC ID @cap. If found, return the capability offset in
 * config space; otherwise return 0.
 */
u16 pci_find_vsec_capability(struct pci_dev *dev, u16 vendor, int cap)
{
	u16 vsec = 0;
	u32 header;

	if (vendor != dev->vendor)
		return 0;

	while ((vsec = pci_find_next_ext_capability(dev, vsec,
						     PCI_EXT_CAP_ID_VNDR))) {
		if (pci_read_config_dword(dev, vsec + PCI_VNDR_HEADER,
					  &header) == PCIBIOS_SUCCESSFUL &&
		    PCI_VNDR_HEADER_ID(header) == cap)
			return vsec;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pci_find_vsec_capability);

/**
 * pci_find_parent_resource - return resource region of parent bus of given
 *			      region
 * @dev: PCI device structure contains resources to be searched
 * @res: child resource record for which parent is sought
 *
 * For given resource region of given device, return the resource region of
 * parent bus the given region is contained in.
 */
struct resource *pci_find_parent_resource(const struct pci_dev *dev,
					  struct resource *res)
{
	const struct pci_bus *bus = dev->bus;
	struct resource *r;
	int i;

	pci_bus_for_each_resource(bus, r, i) {
		if (!r)
			continue;
		if (resource_contains(r, res)) {

			/*
			 * If the window is prefetchable but the BAR is
			 * not, the allocator made a mistake.
			 */
			if (r->flags & IORESOURCE_PREFETCH &&
			    !(res->flags & IORESOURCE_PREFETCH))
				return NULL;

			/*
			 * If we're below a transparent bridge, there may
			 * be both a positively-decoded aperture and a
			 * subtractively-decoded region that contain the BAR.
			 * We want the positively-decoded one, so this depends
			 * on pci_bus_for_each_resource() giving us those
			 * first.
			 */
			return r;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(pci_find_parent_resource);

/**
 * pci_find_resource - Return matching PCI device resource
 * @dev: PCI device to query
 * @res: Resource to look for
 *
 * Goes over standard PCI resources (BARs) and checks if the given resource
 * is partially or fully contained in any of them. In that case the
 * matching resource is returned, %NULL otherwise.
 */
struct resource *pci_find_resource(struct pci_dev *dev, struct resource *res)
{
	int i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		struct resource *r = &dev->resource[i];

		if (r->start && resource_contains(r, res))
			return r;
	}

	return NULL;
}
EXPORT_SYMBOL(pci_find_resource);

/**
 * pci_wait_for_pending - wait for @mask bit(s) to clear in status word @pos
 * @dev: the PCI device to operate on
 * @pos: config space offset of status word
 * @mask: mask of bit(s) to care about in status word
 *
 * Return 1 when mask bit(s) in status word clear, 0 otherwise.
 */
int pci_wait_for_pending(struct pci_dev *dev, int pos, u16 mask)
{
	int i;

	/* Wait for Transaction Pending bit clean */
	for (i = 0; i < 4; i++) {
		u16 status;
		if (i)
			msleep((1 << (i - 1)) * 100);

		pci_read_config_word(dev, pos, &status);
		if (!(status & mask))
			return 1;
	}

	return 0;
}

static int pci_acs_enable;

/**
 * pci_request_acs - ask for ACS to be enabled if supported
 */
void pci_request_acs(void)
{
	pci_acs_enable = 1;
}

static const char *disable_acs_redir_param;

/**
 * pci_disable_acs_redir - disable ACS redirect capabilities
 * @dev: the PCI device
 *
 * For only devices specified in the disable_acs_redir parameter.
 */
static void pci_disable_acs_redir(struct pci_dev *dev)
{
	int ret = 0;
	const char *p;
	int pos;
	u16 ctrl;

	if (!disable_acs_redir_param)
		return;

	p = disable_acs_redir_param;
	while (*p) {
		ret = pci_dev_str_match(dev, p, &p);
		if (ret < 0) {
			pr_info_once("PCI: Can't parse disable_acs_redir parameter: %s\n",
				     disable_acs_redir_param);

			break;
		} else if (ret == 1) {
			/* Found a match */
			break;
		}

		if (*p != ';' && *p != ',') {
			/* End of param or invalid format */
			break;
		}
		p++;
	}

	if (ret != 1)
		return;

	if (!pci_dev_specific_disable_acs_redir(dev))
		return;

	pos = dev->acs_cap;
	if (!pos) {
		pci_warn(dev, "cannot disable ACS redirect for this hardware as it does not have ACS capabilities\n");
		return;
	}

	pci_read_config_word(dev, pos + PCI_ACS_CTRL, &ctrl);

	/* P2P Request & Completion Redirect */
	ctrl &= ~(PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_EC);

	pci_write_config_word(dev, pos + PCI_ACS_CTRL, ctrl);

	pci_info(dev, "disabled ACS redirect\n");
}

/**
 * pci_std_enable_acs - enable ACS on devices using standard ACS capabilities
 * @dev: the PCI device
 */
static void pci_std_enable_acs(struct pci_dev *dev)
{
	int pos;
	u16 cap;
	u16 ctrl;

	pos = dev->acs_cap;
	if (!pos)
		return;

	pci_read_config_word(dev, pos + PCI_ACS_CAP, &cap);
	pci_read_config_word(dev, pos + PCI_ACS_CTRL, &ctrl);

	/* Source Validation */
	ctrl |= (cap & PCI_ACS_SV);

	/* P2P Request Redirect */
	ctrl |= (cap & PCI_ACS_RR);

	/* P2P Completion Redirect */
	ctrl |= (cap & PCI_ACS_CR);

	/* Upstream Forwarding */
	ctrl |= (cap & PCI_ACS_UF);

	/* Enable Translation Blocking for external devices */
	if (dev->external_facing || dev->untrusted)
		ctrl |= (cap & PCI_ACS_TB);

	pci_write_config_word(dev, pos + PCI_ACS_CTRL, ctrl);
}

/**
 * pci_enable_acs - enable ACS if hardware support it
 * @dev: the PCI device
 */
static void pci_enable_acs(struct pci_dev *dev)
{
	if (!pci_acs_enable)
		goto disable_acs_redir;

	if (!pci_dev_specific_enable_acs(dev))
		goto disable_acs_redir;

	pci_std_enable_acs(dev);

disable_acs_redir:
	/*
	 * Note: pci_disable_acs_redir() must be called even if ACS was not
	 * enabled by the kernel because it may have been enabled by
	 * platform firmware.  So if we are told to disable it, we should
	 * always disable it after setting the kernel's default
	 * preferences.
	 */
	pci_disable_acs_redir(dev);
}

/**
 * pci_restore_bars - restore a device's BAR values (e.g. after wake-up)
 * @dev: PCI device to have its BARs restored
 *
 * Restore the BAR values for a given device, so as to make it
 * accessible by its driver.
 */
static void pci_restore_bars(struct pci_dev *dev)
{
	int i;

	for (i = 0; i < PCI_BRIDGE_RESOURCES; i++)
		pci_update_resource(dev, i);
}

static const struct pci_platform_pm_ops *pci_platform_pm;

int pci_set_platform_pm(const struct pci_platform_pm_ops *ops)
{
	if (!ops->is_manageable || !ops->set_state  || !ops->get_state ||
	    !ops->choose_state  || !ops->set_wakeup || !ops->need_resume)
		return -EINVAL;
	pci_platform_pm = ops;
	return 0;
}

static inline bool platform_pci_power_manageable(struct pci_dev *dev)
{
	return pci_platform_pm ? pci_platform_pm->is_manageable(dev) : false;
}

static inline int platform_pci_set_power_state(struct pci_dev *dev,
					       pci_power_t t)
{
	return pci_platform_pm ? pci_platform_pm->set_state(dev, t) : -ENOSYS;
}

static inline pci_power_t platform_pci_get_power_state(struct pci_dev *dev)
{
	return pci_platform_pm ? pci_platform_pm->get_state(dev) : PCI_UNKNOWN;
}

static inline void platform_pci_refresh_power_state(struct pci_dev *dev)
{
	if (pci_platform_pm && pci_platform_pm->refresh_state)
		pci_platform_pm->refresh_state(dev);
}

static inline pci_power_t platform_pci_choose_state(struct pci_dev *dev)
{
	return pci_platform_pm ?
			pci_platform_pm->choose_state(dev) : PCI_POWER_ERROR;
}

static inline int platform_pci_set_wakeup(struct pci_dev *dev, bool enable)
{
	return pci_platform_pm ?
			pci_platform_pm->set_wakeup(dev, enable) : -ENODEV;
}

static inline bool platform_pci_need_resume(struct pci_dev *dev)
{
	return pci_platform_pm ? pci_platform_pm->need_resume(dev) : false;
}

static inline bool platform_pci_bridge_d3(struct pci_dev *dev)
{
	if (pci_platform_pm && pci_platform_pm->bridge_d3)
		return pci_platform_pm->bridge_d3(dev);
	return false;
}

/**
 * pci_raw_set_power_state - Use PCI PM registers to set the power state of
 *			     given PCI device
 * @dev: PCI device to handle.
 * @state: PCI power state (D0, D1, D2, D3hot) to put the device into.
 *
 * RETURN VALUE:
 * -EINVAL if the requested state is invalid.
 * -EIO if device does not support PCI PM or its PM capabilities register has a
 * wrong version, or device doesn't support the requested state.
 * 0 if device already is in the requested state.
 * 0 if device's power state has been successfully changed.
 */
static int pci_raw_set_power_state(struct pci_dev *dev, pci_power_t state)
{
	u16 pmcsr;
	bool need_restore = false;

	/* Check if we're already there */
	if (dev->current_state == state)
		return 0;

	if (!dev->pm_cap)
		return -EIO;

	if (state < PCI_D0 || state > PCI_D3hot)
		return -EINVAL;

	/*
	 * Validate transition: We can enter D0 from any state, but if
	 * we're already in a low-power state, we can only go deeper.  E.g.,
	 * we can go from D1 to D3, but we can't go directly from D3 to D1;
	 * we'd have to go from D3 to D0, then to D1.
	 */
	if (state != PCI_D0 && dev->current_state <= PCI_D3cold
	    && dev->current_state > state) {
		pci_err(dev, "invalid power transition (from %s to %s)\n",
			pci_power_name(dev->current_state),
			pci_power_name(state));
		return -EINVAL;
	}

	/* Check if this device supports the desired state */
	if ((state == PCI_D1 && !dev->d1_support)
	   || (state == PCI_D2 && !dev->d2_support))
		return -EIO;

	pci_read_config_word(dev, dev->pm_cap + PCI_PM_CTRL, &pmcsr);
	if (pmcsr == (u16) ~0) {
		pci_err(dev, "can't change power state from %s to %s (config space inaccessible)\n",
			pci_power_name(dev->current_state),
			pci_power_name(state));
		return -EIO;
	}

	/*
	 * If we're (effectively) in D3, force entire word to 0.
	 * This doesn't affect PME_Status, disables PME_En, and
	 * sets PowerState to 0.
	 */
	switch (dev->current_state) {
	case PCI_D0:
	case PCI_D1:
	case PCI_D2:
		pmcsr &= ~PCI_PM_CTRL_STATE_MASK;
		pmcsr |= state;
		break;
	case PCI_D3hot:
	case PCI_D3cold:
	case PCI_UNKNOWN: /* Boot-up */
		if ((pmcsr & PCI_PM_CTRL_STATE_MASK) == PCI_D3hot
		 && !(pmcsr & PCI_PM_CTRL_NO_SOFT_RESET))
			need_restore = true;
		fallthrough;	/* force to D0 */
	default:
		pmcsr = 0;
		break;
	}

	/* Enter specified state */
	pci_write_config_word(dev, dev->pm_cap + PCI_PM_CTRL, pmcsr);

	/*
	 * Mandatory power management transition delays; see PCI PM 1.1
	 * 5.6.1 table 18
	 */
	if (state == PCI_D3hot || dev->current_state == PCI_D3hot)
		pci_dev_d3_sleep(dev);
	else if (state == PCI_D2 || dev->current_state == PCI_D2)
		udelay(PCI_PM_D2_DELAY);

	pci_read_config_word(dev, dev->pm_cap + PCI_PM_CTRL, &pmcsr);
	dev->current_state = (pmcsr & PCI_PM_CTRL_STATE_MASK);
	if (dev->current_state != state)
		pci_info_ratelimited(dev, "refused to change power state from %s to %s\n",
			 pci_power_name(dev->current_state),
			 pci_power_name(state));

	/*
	 * According to section 5.4.1 of the "PCI BUS POWER MANAGEMENT
	 * INTERFACE SPECIFICATION, REV. 1.2", a device transitioning
	 * from D3hot to D0 _may_ perform an internal reset, thereby
	 * going to "D0 Uninitialized" rather than "D0 Initialized".
	 * For example, at least some versions of the 3c905B and the
	 * 3c556B exhibit this behaviour.
	 *
	 * At least some laptop BIOSen (e.g. the Thinkpad T21) leave
	 * devices in a D3hot state at boot.  Consequently, we need to
	 * restore at least the BARs so that the device will be
	 * accessible to its driver.
	 */
	if (need_restore)
		pci_restore_bars(dev);

	if (dev->bus->self)
		pcie_aspm_pm_state_change(dev->bus->self);

	return 0;
}

/**
 * pci_update_current_state - Read power state of given device and cache it
 * @dev: PCI device to handle.
 * @state: State to cache in case the device doesn't have the PM capability
 *
 * The power state is read from the PMCSR register, which however is
 * inaccessible in D3cold.  The platform firmware is therefore queried first
 * to detect accessibility of the register.  In case the platform firmware
 * reports an incorrect state or the device isn't power manageable by the
 * platform at all, we try to detect D3cold by testing accessibility of the
 * vendor ID in config space.
 */
void pci_update_current_state(struct pci_dev *dev, pci_power_t state)
{
	if (platform_pci_get_power_state(dev) == PCI_D3cold ||
	    !pci_device_is_present(dev)) {
		dev->current_state = PCI_D3cold;
	} else if (dev->pm_cap) {
		u16 pmcsr;

		pci_read_config_word(dev, dev->pm_cap + PCI_PM_CTRL, &pmcsr);
		dev->current_state = (pmcsr & PCI_PM_CTRL_STATE_MASK);
	} else {
		dev->current_state = state;
	}
}

/**
 * pci_refresh_power_state - Refresh the given device's power state data
 * @dev: Target PCI device.
 *
 * Ask the platform to refresh the devices power state information and invoke
 * pci_update_current_state() to update its current PCI power state.
 */
void pci_refresh_power_state(struct pci_dev *dev)
{
	if (platform_pci_power_manageable(dev))
		platform_pci_refresh_power_state(dev);

	pci_update_current_state(dev, dev->current_state);
}

/**
 * pci_platform_power_transition - Use platform to change device power state
 * @dev: PCI device to handle.
 * @state: State to put the device into.
 */
int pci_platform_power_transition(struct pci_dev *dev, pci_power_t state)
{
	int error;

	if (platform_pci_power_manageable(dev)) {
		error = platform_pci_set_power_state(dev, state);
		if (!error)
			pci_update_current_state(dev, state);
	} else
		error = -ENODEV;

	if (error && !dev->pm_cap) /* Fall back to PCI_D0 */
		dev->current_state = PCI_D0;

	return error;
}
EXPORT_SYMBOL_GPL(pci_platform_power_transition);

static int pci_resume_one(struct pci_dev *pci_dev, void *ign)
{
	pm_request_resume(&pci_dev->dev);
	return 0;
}

/**
 * pci_resume_bus - Walk given bus and runtime resume devices on it
 * @bus: Top bus of the subtree to walk.
 */
void pci_resume_bus(struct pci_bus *bus)
{
	if (bus)
		pci_walk_bus(bus, pci_resume_one, NULL);
}

static int pci_dev_wait(struct pci_dev *dev, char *reset_type, int timeout)
{
	int delay = 1;
	u32 id;

	/*
	 * After reset, the device should not silently discard config
	 * requests, but it may still indicate that it needs more time by
	 * responding to them with CRS completions.  The Root Port will
	 * generally synthesize ~0 data to complete the read (except when
	 * CRS SV is enabled and the read was for the Vendor ID; in that
	 * case it synthesizes 0x0001 data).
	 *
	 * Wait for the device to return a non-CRS completion.  Read the
	 * Command register instead of Vendor ID so we don't have to
	 * contend with the CRS SV value.
	 */
	pci_read_config_dword(dev, PCI_COMMAND, &id);
	while (id == ~0) {
		if (delay > timeout) {
			pci_warn(dev, "not ready %dms after %s; giving up\n",
				 delay - 1, reset_type);
			return -ENOTTY;
		}

		if (delay > 1000)
			pci_info(dev, "not ready %dms after %s; waiting\n",
				 delay - 1, reset_type);

		msleep(delay);
		delay *= 2;
		pci_read_config_dword(dev, PCI_COMMAND, &id);
	}

	if (delay > 1000)
		pci_info(dev, "ready %dms after %s\n", delay - 1,
			 reset_type);

	return 0;
}

/**
 * pci_power_up - Put the given device into D0
 * @dev: PCI device to power up
 */
int pci_power_up(struct pci_dev *dev)
{
	pci_platform_power_transition(dev, PCI_D0);

	/*
	 * Mandatory power management transition delays are handled in
	 * pci_pm_resume_noirq() and pci_pm_runtime_resume() of the
	 * corresponding bridge.
	 */
	if (dev->runtime_d3cold) {
		/*
		 * When powering on a bridge from D3cold, the whole hierarchy
		 * may be powered on into D0uninitialized state, resume them to
		 * give them a chance to suspend again
		 */
		pci_resume_bus(dev->subordinate);
	}

	return pci_raw_set_power_state(dev, PCI_D0);
}

/**
 * __pci_dev_set_current_state - Set current state of a PCI device
 * @dev: Device to handle
 * @data: pointer to state to be set
 */
static int __pci_dev_set_current_state(struct pci_dev *dev, void *data)
{
	pci_power_t state = *(pci_power_t *)data;

	dev->current_state = state;
	return 0;
}

/**
 * pci_bus_set_current_state - Walk given bus and set current state of devices
 * @bus: Top bus of the subtree to walk.
 * @state: state to be set
 */
void pci_bus_set_current_state(struct pci_bus *bus, pci_power_t state)
{
	if (bus)
		pci_walk_bus(bus, __pci_dev_set_current_state, &state);
}

/**
 * pci_set_power_state - Set the power state of a PCI device
 * @dev: PCI device to handle.
 * @state: PCI power state (D0, D1, D2, D3hot) to put the device into.
 *
 * Transition a device to a new power state, using the platform firmware and/or
 * the device's PCI PM registers.
 *
 * RETURN VALUE:
 * -EINVAL if the requested state is invalid.
 * -EIO if device does not support PCI PM or its PM capabilities register has a
 * wrong version, or device doesn't support the requested state.
 * 0 if the transition is to D1 or D2 but D1 and D2 are not supported.
 * 0 if device already is in the requested state.
 * 0 if the transition is to D3 but D3 is not supported.
 * 0 if device's power state has been successfully changed.
 */
int pci_set_power_state(struct pci_dev *dev, pci_power_t state)
{
	int error;

	/* Bound the state we're entering */
	if (state > PCI_D3cold)
		state = PCI_D3cold;
	else if (state < PCI_D0)
		state = PCI_D0;
	else if ((state == PCI_D1 || state == PCI_D2) && pci_no_d1d2(dev))

		/*
		 * If the device or the parent bridge do not support PCI
		 * PM, ignore the request if we're doing anything other
		 * than putting it into D0 (which would only happen on
		 * boot).
		 */
		return 0;

	/* Check if we're already there */
	if (dev->current_state == state)
		return 0;

	if (state == PCI_D0)
		return pci_power_up(dev);

	/*
	 * This device is quirked not to be put into D3, so don't put it in
	 * D3
	 */
	if (state >= PCI_D3hot && (dev->dev_flags & PCI_DEV_FLAGS_NO_D3))
		return 0;

	/*
	 * To put device in D3cold, we put device into D3hot in native
	 * way, then put device into D3cold with platform ops
	 */
	error = pci_raw_set_power_state(dev, state > PCI_D3hot ?
					PCI_D3hot : state);

	if (pci_platform_power_transition(dev, state))
		return error;

	/* Powering off a bridge may power off the whole hierarchy */
	if (state == PCI_D3cold)
		pci_bus_set_current_state(dev->subordinate, PCI_D3cold);

	return 0;
}
EXPORT_SYMBOL(pci_set_power_state);

/**
 * pci_choose_state - Choose the power state of a PCI device
 * @dev: PCI device to be suspended
 * @state: target sleep state for the whole system. This is the value
 *	   that is passed to suspend() function.
 *
 * Returns PCI power state suitable for given device and given system
 * message.
 */
pci_power_t pci_choose_state(struct pci_dev *dev, pm_message_t state)
{
	pci_power_t ret;

	if (!dev->pm_cap)
		return PCI_D0;

	ret = platform_pci_choose_state(dev);
	if (ret != PCI_POWER_ERROR)
		return ret;

	switch (state.event) {
	case PM_EVENT_ON:
		return PCI_D0;
	case PM_EVENT_FREEZE:
	case PM_EVENT_PRETHAW:
		/* REVISIT both freeze and pre-thaw "should" use D0 */
	case PM_EVENT_SUSPEND:
	case PM_EVENT_HIBERNATE:
		return PCI_D3hot;
	default:
		pci_info(dev, "unrecognized suspend event %d\n",
			 state.event);
		BUG();
	}
	return PCI_D0;
}
EXPORT_SYMBOL(pci_choose_state);

#define PCI_EXP_SAVE_REGS	7

static struct pci_cap_saved_state *_pci_find_saved_cap(struct pci_dev *pci_dev,
						       u16 cap, bool extended)
{
	struct pci_cap_saved_state *tmp;

	hlist_for_each_entry(tmp, &pci_dev->saved_cap_space, next) {
		if (tmp->cap.cap_extended == extended && tmp->cap.cap_nr == cap)
			return tmp;
	}
	return NULL;
}

struct pci_cap_saved_state *pci_find_saved_cap(struct pci_dev *dev, char cap)
{
	return _pci_find_saved_cap(dev, cap, false);
}

struct pci_cap_saved_state *pci_find_saved_ext_cap(struct pci_dev *dev, u16 cap)
{
	return _pci_find_saved_cap(dev, cap, true);
}

static int pci_save_pcie_state(struct pci_dev *dev)
{
	int i = 0;
	struct pci_cap_saved_state *save_state;
	u16 *cap;

	if (!pci_is_pcie(dev))
		return 0;

	save_state = pci_find_saved_cap(dev, PCI_CAP_ID_EXP);
	if (!save_state) {
		pci_err(dev, "buffer not found in %s\n", __func__);
		return -ENOMEM;
	}

	cap = (u16 *)&save_state->cap.data[0];
	pcie_capability_read_word(dev, PCI_EXP_DEVCTL, &cap[i++]);
	pcie_capability_read_word(dev, PCI_EXP_LNKCTL, &cap[i++]);
	pcie_capability_read_word(dev, PCI_EXP_SLTCTL, &cap[i++]);
	pcie_capability_read_word(dev, PCI_EXP_RTCTL,  &cap[i++]);
	pcie_capability_read_word(dev, PCI_EXP_DEVCTL2, &cap[i++]);
	pcie_capability_read_word(dev, PCI_EXP_LNKCTL2, &cap[i++]);
	pcie_capability_read_word(dev, PCI_EXP_SLTCTL2, &cap[i++]);

	return 0;
}

static void pci_restore_pcie_state(struct pci_dev *dev)
{
	int i = 0;
	struct pci_cap_saved_state *save_state;
	u16 *cap;

	save_state = pci_find_saved_cap(dev, PCI_CAP_ID_EXP);
	if (!save_state)
		return;

	cap = (u16 *)&save_state->cap.data[0];
	pcie_capability_write_word(dev, PCI_EXP_DEVCTL, cap[i++]);
	pcie_capability_write_word(dev, PCI_EXP_LNKCTL, cap[i++]);
	pcie_capability_write_word(dev, PCI_EXP_SLTCTL, cap[i++]);
	pcie_capability_write_word(dev, PCI_EXP_RTCTL, cap[i++]);
	pcie_capability_write_word(dev, PCI_EXP_DEVCTL2, cap[i++]);
	pcie_capability_write_word(dev, PCI_EXP_LNKCTL2, cap[i++]);
	pcie_capability_write_word(dev, PCI_EXP_SLTCTL2, cap[i++]);
}

static int pci_save_pcix_state(struct pci_dev *dev)
{
	int pos;
	struct pci_cap_saved_state *save_state;

	pos = pci_find_capability(dev, PCI_CAP_ID_PCIX);
	if (!pos)
		return 0;

	save_state = pci_find_saved_cap(dev, PCI_CAP_ID_PCIX);
	if (!save_state) {
		pci_err(dev, "buffer not found in %s\n", __func__);
		return -ENOMEM;
	}

	pci_read_config_word(dev, pos + PCI_X_CMD,
			     (u16 *)save_state->cap.data);

	return 0;
}

static void pci_restore_pcix_state(struct pci_dev *dev)
{
	int i = 0, pos;
	struct pci_cap_saved_state *save_state;
	u16 *cap;

	save_state = pci_find_saved_cap(dev, PCI_CAP_ID_PCIX);
	pos = pci_find_capability(dev, PCI_CAP_ID_PCIX);
	if (!save_state || !pos)
		return;
	cap = (u16 *)&save_state->cap.data[0];

	pci_write_config_word(dev, pos + PCI_X_CMD, cap[i++]);
}

static void pci_save_ltr_state(struct pci_dev *dev)
{
	int ltr;
	struct pci_cap_saved_state *save_state;
	u16 *cap;

	if (!pci_is_pcie(dev))
		return;

	ltr = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_LTR);
	if (!ltr)
		return;

	save_state = pci_find_saved_ext_cap(dev, PCI_EXT_CAP_ID_LTR);
	if (!save_state) {
		pci_err(dev, "no suspend buffer for LTR; ASPM issues possible after resume\n");
		return;
	}

	cap = (u16 *)&save_state->cap.data[0];
	pci_read_config_word(dev, ltr + PCI_LTR_MAX_SNOOP_LAT, cap++);
	pci_read_config_word(dev, ltr + PCI_LTR_MAX_NOSNOOP_LAT, cap++);
}

static void pci_restore_ltr_state(struct pci_dev *dev)
{
	struct pci_cap_saved_state *save_state;
	int ltr;
	u16 *cap;

	save_state = pci_find_saved_ext_cap(dev, PCI_EXT_CAP_ID_LTR);
	ltr = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_LTR);
	if (!save_state || !ltr)
		return;

	cap = (u16 *)&save_state->cap.data[0];
	pci_write_config_word(dev, ltr + PCI_LTR_MAX_SNOOP_LAT, *cap++);
	pci_write_config_word(dev, ltr + PCI_LTR_MAX_NOSNOOP_LAT, *cap++);
}

/**
 * pci_save_state - save the PCI configuration space of a device before
 *		    suspending
 * @dev: PCI device that we're dealing with
 */
int pci_save_state(struct pci_dev *dev)
{
	int i;
	/* XXX: 100% dword access ok here? */
	for (i = 0; i < 16; i++) {
		pci_read_config_dword(dev, i * 4, &dev->saved_config_space[i]);
		pci_dbg(dev, "saving config space at offset %#x (reading %#x)\n",
			i * 4, dev->saved_config_space[i]);
	}
	dev->state_saved = true;

	i = pci_save_pcie_state(dev);
	if (i != 0)
		return i;

	i = pci_save_pcix_state(dev);
	if (i != 0)
		return i;

	pci_save_ltr_state(dev);
	pci_save_dpc_state(dev);
	pci_save_aer_state(dev);
	pci_save_ptm_state(dev);
	return pci_save_vc_state(dev);
}
EXPORT_SYMBOL(pci_save_state);

static void pci_restore_config_dword(struct pci_dev *pdev, int offset,
				     u32 saved_val, int retry, bool force)
{
	u32 val;

	pci_read_config_dword(pdev, offset, &val);
	if (!force && val == saved_val)
		return;

	for (;;) {
		pci_dbg(pdev, "restoring config space at offset %#x (was %#x, writing %#x)\n",
			offset, val, saved_val);
		pci_write_config_dword(pdev, offset, saved_val);
		if (retry-- <= 0)
			return;

		pci_read_config_dword(pdev, offset, &val);
		if (val == saved_val)
			return;

		mdelay(1);
	}
}

static void pci_restore_config_space_range(struct pci_dev *pdev,
					   int start, int end, int retry,
					   bool force)
{
	int index;

	for (index = end; index >= start; index--)
		pci_restore_config_dword(pdev, 4 * index,
					 pdev->saved_config_space[index],
					 retry, force);
}

static void pci_restore_config_space(struct pci_dev *pdev)
{
	if (pdev->hdr_type == PCI_HEADER_TYPE_NORMAL) {
		pci_restore_config_space_range(pdev, 10, 15, 0, false);
		/* Restore BARs before the command register. */
		pci_restore_config_space_range(pdev, 4, 9, 10, false);
		pci_restore_config_space_range(pdev, 0, 3, 0, false);
	} else if (pdev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
		pci_restore_config_space_range(pdev, 12, 15, 0, false);

		/*
		 * Force rewriting of prefetch registers to avoid S3 resume
		 * issues on Intel PCI bridges that occur when these
		 * registers are not explicitly written.
		 */
		pci_restore_config_space_range(pdev, 9, 11, 0, true);
		pci_restore_config_space_range(pdev, 0, 8, 0, false);
	} else {
		pci_restore_config_space_range(pdev, 0, 15, 0, false);
	}
}

static void pci_restore_rebar_state(struct pci_dev *pdev)
{
	unsigned int pos, nbars, i;
	u32 ctrl;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_REBAR);
	if (!pos)
		return;

	pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
	nbars = (ctrl & PCI_REBAR_CTRL_NBAR_MASK) >>
		    PCI_REBAR_CTRL_NBAR_SHIFT;

	for (i = 0; i < nbars; i++, pos += 8) {
		struct resource *res;
		int bar_idx, size;

		pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
		bar_idx = ctrl & PCI_REBAR_CTRL_BAR_IDX;
		res = pdev->resource + bar_idx;
		size = pci_rebar_bytes_to_size(resource_size(res));
		ctrl &= ~PCI_REBAR_CTRL_BAR_SIZE;
		ctrl |= size << PCI_REBAR_CTRL_BAR_SHIFT;
		pci_write_config_dword(pdev, pos + PCI_REBAR_CTRL, ctrl);
	}
}

/**
 * pci_restore_state - Restore the saved state of a PCI device
 * @dev: PCI device that we're dealing with
 */
void pci_restore_state(struct pci_dev *dev)
{
	if (!dev->state_saved)
		return;

	/*
	 * Restore max latencies (in the LTR capability) before enabling
	 * LTR itself (in the PCIe capability).
	 */
	pci_restore_ltr_state(dev);

	pci_restore_pcie_state(dev);
	pci_restore_pasid_state(dev);
	pci_restore_pri_state(dev);
	pci_restore_ats_state(dev);
	pci_restore_vc_state(dev);
	pci_restore_rebar_state(dev);
	pci_restore_dpc_state(dev);
	pci_restore_ptm_state(dev);

	pci_aer_clear_status(dev);
	pci_restore_aer_state(dev);

	pci_restore_config_space(dev);

	pci_restore_pcix_state(dev);
	pci_restore_msi_state(dev);

	/* Restore ACS and IOV configuration state */
	pci_enable_acs(dev);
	pci_restore_iov_state(dev);

	dev->state_saved = false;
}
EXPORT_SYMBOL(pci_restore_state);

struct pci_saved_state {
	u32 config_space[16];
	struct pci_cap_saved_data cap[];
};

/**
 * pci_store_saved_state - Allocate and return an opaque struct containing
 *			   the device saved state.
 * @dev: PCI device that we're dealing with
 *
 * Return NULL if no state or error.
 */
struct pci_saved_state *pci_store_saved_state(struct pci_dev *dev)
{
	struct pci_saved_state *state;
	struct pci_cap_saved_state *tmp;
	struct pci_cap_saved_data *cap;
	size_t size;

	if (!dev->state_saved)
		return NULL;

	size = sizeof(*state) + sizeof(struct pci_cap_saved_data);

	hlist_for_each_entry(tmp, &dev->saved_cap_space, next)
		size += sizeof(struct pci_cap_saved_data) + tmp->cap.size;

	state = kzalloc(size, GFP_KERNEL);
	if (!state)
		return NULL;

	memcpy(state->config_space, dev->saved_config_space,
	       sizeof(state->config_space));

	cap = state->cap;
	hlist_for_each_entry(tmp, &dev->saved_cap_space, next) {
		size_t len = sizeof(struct pci_cap_saved_data) + tmp->cap.size;
		memcpy(cap, &tmp->cap, len);
		cap = (struct pci_cap_saved_data *)((u8 *)cap + len);
	}
	/* Empty cap_save terminates list */

	return state;
}
EXPORT_SYMBOL_GPL(pci_store_saved_state);

/**
 * pci_load_saved_state - Reload the provided save state into struct pci_dev.
 * @dev: PCI device that we're dealing with
 * @state: Saved state returned from pci_store_saved_state()
 */
int pci_load_saved_state(struct pci_dev *dev,
			 struct pci_saved_state *state)
{
	struct pci_cap_saved_data *cap;

	dev->state_saved = false;

	if (!state)
		return 0;

	memcpy(dev->saved_config_space, state->config_space,
	       sizeof(state->config_space));

	cap = state->cap;
	while (cap->size) {
		struct pci_cap_saved_state *tmp;

		tmp = _pci_find_saved_cap(dev, cap->cap_nr, cap->cap_extended);
		if (!tmp || tmp->cap.size != cap->size)
			return -EINVAL;

		memcpy(tmp->cap.data, cap->data, tmp->cap.size);
		cap = (struct pci_cap_saved_data *)((u8 *)cap +
		       sizeof(struct pci_cap_saved_data) + cap->size);
	}

	dev->state_saved = true;
	return 0;
}
EXPORT_SYMBOL_GPL(pci_load_saved_state);

/**
 * pci_load_and_free_saved_state - Reload the save state pointed to by state,
 *				   and free the memory allocated for it.
 * @dev: PCI device that we're dealing with
 * @state: Pointer to saved state returned from pci_store_saved_state()
 */
int pci_load_and_free_saved_state(struct pci_dev *dev,
				  struct pci_saved_state **state)
{
	int ret = pci_load_saved_state(dev, *state);
	kfree(*state);
	*state = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(pci_load_and_free_saved_state);

int __weak pcibios_enable_device(struct pci_dev *dev, int bars)
{
	return pci_enable_resources(dev, bars);
}

static int do_pci_enable_device(struct pci_dev *dev, int bars)
{
	int err;
	struct pci_dev *bridge;
	u16 cmd;
	u8 pin;

	err = pci_set_power_state(dev, PCI_D0);
	if (err < 0 && err != -EIO)
		return err;

	bridge = pci_upstream_bridge(dev);
	if (bridge)
		pcie_aspm_powersave_config_link(bridge);

	err = pcibios_enable_device(dev, bars);
	if (err < 0)
		return err;
	pci_fixup_device(pci_fixup_enable, dev);

	if (dev->msi_enabled || dev->msix_enabled)
		return 0;

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
	if (pin) {
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		if (cmd & PCI_COMMAND_INTX_DISABLE)
			pci_write_config_word(dev, PCI_COMMAND,
					      cmd & ~PCI_COMMAND_INTX_DISABLE);
	}

	return 0;
}

/**
 * pci_reenable_device - Resume abandoned device
 * @dev: PCI device to be resumed
 *
 * NOTE: This function is a backend of pci_default_resume() and is not supposed
 * to be called by normal code, write proper resume handler and use it instead.
 */
int pci_reenable_device(struct pci_dev *dev)
{
	if (pci_is_enabled(dev))
		return do_pci_enable_device(dev, (1 << PCI_NUM_RESOURCES) - 1);
	return 0;
}
EXPORT_SYMBOL(pci_reenable_device);

static void pci_enable_bridge(struct pci_dev *dev)
{
	struct pci_dev *bridge;
	int retval;

	bridge = pci_upstream_bridge(dev);
	if (bridge)
		pci_enable_bridge(bridge);

	if (pci_is_enabled(dev)) {
		if (!dev->is_busmaster)
			pci_set_master(dev);
		return;
	}

	retval = pci_enable_device(dev);
	if (retval)
		pci_err(dev, "Error enabling bridge (%d), continuing\n",
			retval);
	pci_set_master(dev);
}

static int pci_enable_device_flags(struct pci_dev *dev, unsigned long flags)
{
	struct pci_dev *bridge;
	int err;
	int i, bars = 0;

	/*
	 * Power state could be unknown at this point, either due to a fresh
	 * boot or a device removal call.  So get the current power state
	 * so that things like MSI message writing will behave as expected
	 * (e.g. if the device really is in D0 at enable time).
	 */
	if (dev->pm_cap) {
		u16 pmcsr;
		pci_read_config_word(dev, dev->pm_cap + PCI_PM_CTRL, &pmcsr);
		dev->current_state = (pmcsr & PCI_PM_CTRL_STATE_MASK);
	}

	if (atomic_inc_return(&dev->enable_cnt) > 1)
		return 0;		/* already enabled */

	bridge = pci_upstream_bridge(dev);
	if (bridge)
		pci_enable_bridge(bridge);

	/* only skip sriov related */
	for (i = 0; i <= PCI_ROM_RESOURCE; i++)
		if (dev->resource[i].flags & flags)
			bars |= (1 << i);
	for (i = PCI_BRIDGE_RESOURCES; i < DEVICE_COUNT_RESOURCE; i++)
		if (dev->resource[i].flags & flags)
			bars |= (1 << i);

	err = do_pci_enable_device(dev, bars);
	if (err < 0)
		atomic_dec(&dev->enable_cnt);
	return err;
}

/**
 * pci_enable_device_io - Initialize a device for use with IO space
 * @dev: PCI device to be initialized
 *
 * Initialize device before it's used by a driver. Ask low-level code
 * to enable I/O resources. Wake up the device if it was suspended.
 * Beware, this function can fail.
 */
int pci_enable_device_io(struct pci_dev *dev)
{
	return pci_enable_device_flags(dev, IORESOURCE_IO);
}
EXPORT_SYMBOL(pci_enable_device_io);

/**
 * pci_enable_device_mem - Initialize a device for use with Memory space
 * @dev: PCI device to be initialized
 *
 * Initialize device before it's used by a driver. Ask low-level code
 * to enable Memory resources. Wake up the device if it was suspended.
 * Beware, this function can fail.
 */
int pci_enable_device_mem(struct pci_dev *dev)
{
	return pci_enable_device_flags(dev, IORESOURCE_MEM);
}
EXPORT_SYMBOL(pci_enable_device_mem);

/**
 * pci_enable_device - Initialize device before it's used by a driver.
 * @dev: PCI device to be initialized
 *
 * Initialize device before it's used by a driver. Ask low-level code
 * to enable I/O and memory. Wake up the device if it was suspended.
 * Beware, this function can fail.
 *
 * Note we don't actually enable the device many times if we call
 * this function repeatedly (we just increment the count).
 */
int pci_enable_device(struct pci_dev *dev)
{
	return pci_enable_device_flags(dev, IORESOURCE_MEM | IORESOURCE_IO);
}
EXPORT_SYMBOL(pci_enable_device);

/*
 * Managed PCI resources.  This manages device on/off, INTx/MSI/MSI-X
 * on/off and BAR regions.  pci_dev itself records MSI/MSI-X status, so
 * there's no need to track it separately.  pci_devres is initialized
 * when a device is enabled using managed PCI device enable interface.
 */
struct pci_devres {
	unsigned int enabled:1;
	unsigned int pinned:1;
	unsigned int orig_intx:1;
	unsigned int restore_intx:1;
	unsigned int mwi:1;
	u32 region_mask;
};

static void pcim_release(struct device *gendev, void *res)
{
	struct pci_dev *dev = to_pci_dev(gendev);
	struct pci_devres *this = res;
	int i;

	if (dev->msi_enabled)
		pci_disable_msi(dev);
	if (dev->msix_enabled)
		pci_disable_msix(dev);

	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++)
		if (this->region_mask & (1 << i))
			pci_release_region(dev, i);

	if (this->mwi)
		pci_clear_mwi(dev);

	if (this->restore_intx)
		pci_intx(dev, this->orig_intx);

	if (this->enabled && !this->pinned)
		pci_disable_device(dev);
}

static struct pci_devres *get_pci_dr(struct pci_dev *pdev)
{
	struct pci_devres *dr, *new_dr;

	dr = devres_find(&pdev->dev, pcim_release, NULL, NULL);
	if (dr)
		return dr;

	new_dr = devres_alloc(pcim_release, sizeof(*new_dr), GFP_KERNEL);
	if (!new_dr)
		return NULL;
	return devres_get(&pdev->dev, new_dr, NULL, NULL);
}

static struct pci_devres *find_pci_dr(struct pci_dev *pdev)
{
	if (pci_is_managed(pdev))
		return devres_find(&pdev->dev, pcim_release, NULL, NULL);
	return NULL;
}

/**
 * pcim_enable_device - Managed pci_enable_device()
 * @pdev: PCI device to be initialized
 *
 * Managed pci_enable_device().
 */
int pcim_enable_device(struct pci_dev *pdev)
{
	struct pci_devres *dr;
	int rc;

	dr = get_pci_dr(pdev);
	if (unlikely(!dr))
		return -ENOMEM;
	if (dr->enabled)
		return 0;

	rc = pci_enable_device(pdev);
	if (!rc) {
		pdev->is_managed = 1;
		dr->enabled = 1;
	}
	return rc;
}
EXPORT_SYMBOL(pcim_enable_device);

/**
 * pcim_pin_device - Pin managed PCI device
 * @pdev: PCI device to pin
 *
 * Pin managed PCI device @pdev.  Pinned device won't be disabled on
 * driver detach.  @pdev must have been enabled with
 * pcim_enable_device().
 */
void pcim_pin_device(struct pci_dev *pdev)
{
	struct pci_devres *dr;

	dr = find_pci_dr(pdev);
	WARN_ON(!dr || !dr->enabled);
	if (dr)
		dr->pinned = 1;
}
EXPORT_SYMBOL(pcim_pin_device);

/*
 * pcibios_add_device - provide arch specific hooks when adding device dev
 * @dev: the PCI device being added
 *
 * Permits the platform to provide architecture specific functionality when
 * devices are added. This is the default implementation. Architecture
 * implementations can override this.
 */
int __weak pcibios_add_device(struct pci_dev *dev)
{
	return 0;
}

/**
 * pcibios_release_device - provide arch specific hooks when releasing
 *			    device dev
 * @dev: the PCI device being released
 *
 * Permits the platform to provide architecture specific functionality when
 * devices are released. This is the default implementation. Architecture
 * implementations can override this.
 */
void __weak pcibios_release_device(struct pci_dev *dev) {}

/**
 * pcibios_disable_device - disable arch specific PCI resources for device dev
 * @dev: the PCI device to disable
 *
 * Disables architecture specific PCI resources for the device. This
 * is the default implementation. Architecture implementations can
 * override this.
 */
void __weak pcibios_disable_device(struct pci_dev *dev) {}

/**
 * pcibios_penalize_isa_irq - penalize an ISA IRQ
 * @irq: ISA IRQ to penalize
 * @active: IRQ active or not
 *
 * Permits the platform to provide architecture-specific functionality when
 * penalizing ISA IRQs. This is the default implementation. Architecture
 * implementations can override this.
 */
void __weak pcibios_penalize_isa_irq(int irq, int active) {}

static void do_pci_disable_device(struct pci_dev *dev)
{
	u16 pci_command;

	pci_read_config_word(dev, PCI_COMMAND, &pci_command);
	if (pci_command & PCI_COMMAND_MASTER) {
		pci_command &= ~PCI_COMMAND_MASTER;
		pci_write_config_word(dev, PCI_COMMAND, pci_command);
	}

	pcibios_disable_device(dev);
}

/**
 * pci_disable_enabled_device - Disable device without updating enable_cnt
 * @dev: PCI device to disable
 *
 * NOTE: This function is a backend of PCI power management routines and is
 * not supposed to be called drivers.
 */
void pci_disable_enabled_device(struct pci_dev *dev)
{
	if (pci_is_enabled(dev))
		do_pci_disable_device(dev);
}

/**
 * pci_disable_device - Disable PCI device after use
 * @dev: PCI device to be disabled
 *
 * Signal to the system that the PCI device is not in use by the system
 * anymore.  This only involves disabling PCI bus-mastering, if active.
 *
 * Note we don't actually disable the device until all callers of
 * pci_enable_device() have called pci_disable_device().
 */
void pci_disable_device(struct pci_dev *dev)
{
	struct pci_devres *dr;

	dr = find_pci_dr(dev);
	if (dr)
		dr->enabled = 0;

	dev_WARN_ONCE(&dev->dev, atomic_read(&dev->enable_cnt) <= 0,
		      "disabling already-disabled device");

	if (atomic_dec_return(&dev->enable_cnt) != 0)
		return;

	do_pci_disable_device(dev);

	dev->is_busmaster = 0;
}
EXPORT_SYMBOL(pci_disable_device);

/**
 * pcibios_set_pcie_reset_state - set reset state for device dev
 * @dev: the PCIe device reset
 * @state: Reset state to enter into
 *
 * Set the PCIe reset state for the device. This is the default
 * implementation. Architecture implementations can override this.
 */
int __weak pcibios_set_pcie_reset_state(struct pci_dev *dev,
					enum pcie_reset_state state)
{
	return -EINVAL;
}

/**
 * pci_set_pcie_reset_state - set reset state for device dev
 * @dev: the PCIe device reset
 * @state: Reset state to enter into
 *
 * Sets the PCI reset state for the device.
 */
int pci_set_pcie_reset_state(struct pci_dev *dev, enum pcie_reset_state state)
{
	return pcibios_set_pcie_reset_state(dev, state);
}
EXPORT_SYMBOL_GPL(pci_set_pcie_reset_state);

void pcie_clear_device_status(struct pci_dev *dev)
{
	u16 sta;

	pcie_capability_read_word(dev, PCI_EXP_DEVSTA, &sta);
	pcie_capability_write_word(dev, PCI_EXP_DEVSTA, sta);
}

/**
 * pcie_clear_root_pme_status - Clear root port PME interrupt status.
 * @dev: PCIe root port or event collector.
 */
void pcie_clear_root_pme_status(struct pci_dev *dev)
{
	pcie_capability_set_dword(dev, PCI_EXP_RTSTA, PCI_EXP_RTSTA_PME);
}

/**
 * pci_check_pme_status - Check if given device has generated PME.
 * @dev: Device to check.
 *
 * Check the PME status of the device and if set, clear it and clear PME enable
 * (if set).  Return 'true' if PME status and PME enable were both set or
 * 'false' otherwise.
 */
bool pci_check_pme_status(struct pci_dev *dev)
{
	int pmcsr_pos;
	u16 pmcsr;
	bool ret = false;

	if (!dev->pm_cap)
		return false;

	pmcsr_pos = dev->pm_cap + PCI_PM_CTRL;
	pci_read_config_word(dev, pmcsr_pos, &pmcsr);
	if (!(pmcsr & PCI_PM_CTRL_PME_STATUS))
		return false;

	/* Clear PME status. */
	pmcsr |= PCI_PM_CTRL_PME_STATUS;
	if (pmcsr & PCI_PM_CTRL_PME_ENABLE) {
		/* Disable PME to avoid interrupt flood. */
		pmcsr &= ~PCI_PM_CTRL_PME_ENABLE;
		ret = true;
	}

	pci_write_config_word(dev, pmcsr_pos, pmcsr);

	return ret;
}

/**
 * pci_pme_wakeup - Wake up a PCI device if its PME Status bit is set.
 * @dev: Device to handle.
 * @pme_poll_reset: Whether or not to reset the device's pme_poll flag.
 *
 * Check if @dev has generated PME and queue a resume request for it in that
 * case.
 */
static int pci_pme_wakeup(struct pci_dev *dev, void *pme_poll_reset)
{
	if (pme_poll_reset && dev->pme_poll)
		dev->pme_poll = false;

	if (pci_check_pme_status(dev)) {
		pci_wakeup_event(dev);
		pm_request_resume(&dev->dev);
	}
	return 0;
}

/**
 * pci_pme_wakeup_bus - Walk given bus and wake up devices on it, if necessary.
 * @bus: Top bus of the subtree to walk.
 */
void pci_pme_wakeup_bus(struct pci_bus *bus)
{
	if (bus)
		pci_walk_bus(bus, pci_pme_wakeup, (void *)true);
}


/**
 * pci_pme_capable - check the capability of PCI device to generate PME#
 * @dev: PCI device to handle.
 * @state: PCI state from which device will issue PME#.
 */
bool pci_pme_capable(struct pci_dev *dev, pci_power_t state)
{
	if (!dev->pm_cap)
		return false;

	return !!(dev->pme_support & (1 << state));
}
EXPORT_SYMBOL(pci_pme_capable);

static void pci_pme_list_scan(struct work_struct *work)
{
	struct pci_pme_device *pme_dev, *n;

	mutex_lock(&pci_pme_list_mutex);
	list_for_each_entry_safe(pme_dev, n, &pci_pme_list, list) {
		if (pme_dev->dev->pme_poll) {
			struct pci_dev *bridge;

			bridge = pme_dev->dev->bus->self;
			/*
			 * If bridge is in low power state, the
			 * configuration space of subordinate devices
			 * may be not accessible
			 */
			if (bridge && bridge->current_state != PCI_D0)
				continue;
			/*
			 * If the device is in D3cold it should not be
			 * polled either.
			 */
			if (pme_dev->dev->current_state == PCI_D3cold)
				continue;

			pci_pme_wakeup(pme_dev->dev, NULL);
		} else {
			list_del(&pme_dev->list);
			kfree(pme_dev);
		}
	}
	if (!list_empty(&pci_pme_list))
		queue_delayed_work(system_freezable_wq, &pci_pme_work,
				   msecs_to_jiffies(PME_TIMEOUT));
	mutex_unlock(&pci_pme_list_mutex);
}

static void __pci_pme_active(struct pci_dev *dev, bool enable)
{
	u16 pmcsr;

	if (!dev->pme_support)
		return;

	pci_read_config_word(dev, dev->pm_cap + PCI_PM_CTRL, &pmcsr);
	/* Clear PME_Status by writing 1 to it and enable PME# */
	pmcsr |= PCI_PM_CTRL_PME_STATUS | PCI_PM_CTRL_PME_ENABLE;
	if (!enable)
		pmcsr &= ~PCI_PM_CTRL_PME_ENABLE;

	pci_write_config_word(dev, dev->pm_cap + PCI_PM_CTRL, pmcsr);
}

/**
 * pci_pme_restore - Restore PME configuration after config space restore.
 * @dev: PCI device to update.
 */
void pci_pme_restore(struct pci_dev *dev)
{
	u16 pmcsr;

	if (!dev->pme_support)
		return;

	pci_read_config_word(dev, dev->pm_cap + PCI_PM_CTRL, &pmcsr);
	if (dev->wakeup_prepared) {
		pmcsr |= PCI_PM_CTRL_PME_ENABLE;
		pmcsr &= ~PCI_PM_CTRL_PME_STATUS;
	} else {
		pmcsr &= ~PCI_PM_CTRL_PME_ENABLE;
		pmcsr |= PCI_PM_CTRL_PME_STATUS;
	}
	pci_write_config_word(dev, dev->pm_cap + PCI_PM_CTRL, pmcsr);
}

/**
 * pci_pme_active - enable or disable PCI device's PME# function
 * @dev: PCI device to handle.
 * @enable: 'true' to enable PME# generation; 'false' to disable it.
 *
 * The caller must verify that the device is capable of generating PME# before
 * calling this function with @enable equal to 'true'.
 */
void pci_pme_active(struct pci_dev *dev, bool enable)
{
	__pci_pme_active(dev, enable);

	/*
	 * PCI (as opposed to PCIe) PME requires that the device have
	 * its PME# line hooked up correctly. Not all hardware vendors
	 * do this, so the PME never gets delivered and the device
	 * remains asleep. The easiest way around this is to
	 * periodically walk the list of suspended devices and check
	 * whether any have their PME flag set. The assumption is that
	 * we'll wake up often enough anyway that this won't be a huge
	 * hit, and the power savings from the devices will still be a
	 * win.
	 *
	 * Although PCIe uses in-band PME message instead of PME# line
	 * to report PME, PME does not work for some PCIe devices in
	 * reality.  For example, there are devices that set their PME
	 * status bits, but don't really bother to send a PME message;
	 * there are PCI Express Root Ports that don't bother to
	 * trigger interrupts when they receive PME messages from the
	 * devices below.  So PME poll is used for PCIe devices too.
	 */

	if (dev->pme_poll) {
		struct pci_pme_device *pme_dev;
		if (enable) {
			pme_dev = kmalloc(sizeof(struct pci_pme_device),
					  GFP_KERNEL);
			if (!pme_dev) {
				pci_warn(dev, "can't enable PME#\n");
				return;
			}
			pme_dev->dev = dev;
			mutex_lock(&pci_pme_list_mutex);
			list_add(&pme_dev->list, &pci_pme_list);
			if (list_is_singular(&pci_pme_list))
				queue_delayed_work(system_freezable_wq,
						   &pci_pme_work,
						   msecs_to_jiffies(PME_TIMEOUT));
			mutex_unlock(&pci_pme_list_mutex);
		} else {
			mutex_lock(&pci_pme_list_mutex);
			list_for_each_entry(pme_dev, &pci_pme_list, list) {
				if (pme_dev->dev == dev) {
					list_del(&pme_dev->list);
					kfree(pme_dev);
					break;
				}
			}
			mutex_unlock(&pci_pme_list_mutex);
		}
	}

	pci_dbg(dev, "PME# %s\n", enable ? "enabled" : "disabled");
}
EXPORT_SYMBOL(pci_pme_active);

/**
 * __pci_enable_wake - enable PCI device as wakeup event source
 * @dev: PCI device affected
 * @state: PCI state from which device will issue wakeup events
 * @enable: True to enable event generation; false to disable
 *
 * This enables the device as a wakeup event source, or disables it.
 * When such events involves platform-specific hooks, those hooks are
 * called automatically by this routine.
 *
 * Devices with legacy power management (no standard PCI PM capabilities)
 * always require such platform hooks.
 *
 * RETURN VALUE:
 * 0 is returned on success
 * -EINVAL is returned if device is not supposed to wake up the system
 * Error code depending on the platform is returned if both the platform and
 * the native mechanism fail to enable the generation of wake-up events
 */
static int __pci_enable_wake(struct pci_dev *dev, pci_power_t state, bool enable)
{
	int ret = 0;

	/*
	 * Bridges that are not power-manageable directly only signal
	 * wakeup on behalf of subordinate devices which is set up
	 * elsewhere, so skip them. However, bridges that are
	 * power-manageable may signal wakeup for themselves (for example,
	 * on a hotplug event) and they need to be covered here.
	 */
	if (!pci_power_manageable(dev))
		return 0;

	/* Don't do the same thing twice in a row for one device. */
	if (!!enable == !!dev->wakeup_prepared)
		return 0;

	/*
	 * According to "PCI System Architecture" 4th ed. by Tom Shanley & Don
	 * Anderson we should be doing PME# wake enable followed by ACPI wake
	 * enable.  To disable wake-up we call the platform first, for symmetry.
	 */

	if (enable) {
		int error;

		if (pci_pme_capable(dev, state))
			pci_pme_active(dev, true);
		else
			ret = 1;
		error = platform_pci_set_wakeup(dev, true);
		if (ret)
			ret = error;
		if (!ret)
			dev->wakeup_prepared = true;
	} else {
		platform_pci_set_wakeup(dev, false);
		pci_pme_active(dev, false);
		dev->wakeup_prepared = false;
	}

	return ret;
}

/**
 * pci_enable_wake - change wakeup settings for a PCI device
 * @pci_dev: Target device
 * @state: PCI state from which device will issue wakeup events
 * @enable: Whether or not to enable event generation
 *
 * If @enable is set, check device_may_wakeup() for the device before calling
 * __pci_enable_wake() for it.
 */
int pci_enable_wake(struct pci_dev *pci_dev, pci_power_t state, bool enable)
{
	if (enable && !device_may_wakeup(&pci_dev->dev))
		return -EINVAL;

	return __pci_enable_wake(pci_dev, state, enable);
}
EXPORT_SYMBOL(pci_enable_wake);

/**
 * pci_wake_from_d3 - enable/disable device to wake up from D3_hot or D3_cold
 * @dev: PCI device to prepare
 * @enable: True to enable wake-up event generation; false to disable
 *
 * Many drivers want the device to wake up the system from D3_hot or D3_cold
 * and this function allows them to set that up cleanly - pci_enable_wake()
 * should not be called twice in a row to enable wake-up due to PCI PM vs ACPI
 * ordering constraints.
 *
 * This function only returns error code if the device is not allowed to wake
 * up the system from sleep or it is not capable of generating PME# from both
 * D3_hot and D3_cold and the platform is unable to enable wake-up power for it.
 */
int pci_wake_from_d3(struct pci_dev *dev, bool enable)
{
	return pci_pme_capable(dev, PCI_D3cold) ?
			pci_enable_wake(dev, PCI_D3cold, enable) :
			pci_enable_wake(dev, PCI_D3hot, enable);
}
EXPORT_SYMBOL(pci_wake_from_d3);

/**
 * pci_target_state - find an appropriate low power state for a given PCI dev
 * @dev: PCI device
 * @wakeup: Whether or not wakeup functionality will be enabled for the device.
 *
 * Use underlying platform code to find a supported low power state for @dev.
 * If the platform can't manage @dev, return the deepest state from which it
 * can generate wake events, based on any available PME info.
 */
static pci_power_t pci_target_state(struct pci_dev *dev, bool wakeup)
{
	pci_power_t target_state = PCI_D3hot;

	if (platform_pci_power_manageable(dev)) {
		/*
		 * Call the platform to find the target state for the device.
		 */
		pci_power_t state = platform_pci_choose_state(dev);

		switch (state) {
		case PCI_POWER_ERROR:
		case PCI_UNKNOWN:
			break;
		case PCI_D1:
		case PCI_D2:
			if (pci_no_d1d2(dev))
				break;
			fallthrough;
		default:
			target_state = state;
		}

		return target_state;
	}

	if (!dev->pm_cap)
		target_state = PCI_D0;

	/*
	 * If the device is in D3cold even though it's not power-manageable by
	 * the platform, it may have been powered down by non-standard means.
	 * Best to let it slumber.
	 */
	if (dev->current_state == PCI_D3cold)
		target_state = PCI_D3cold;

	if (wakeup) {
		/*
		 * Find the deepest state from which the device can generate
		 * PME#.
		 */
		if (dev->pme_support) {
			while (target_state
			      && !(dev->pme_support & (1 << target_state)))
				target_state--;
		}
	}

	return target_state;
}

/**
 * pci_prepare_to_sleep - prepare PCI device for system-wide transition
 *			  into a sleep state
 * @dev: Device to handle.
 *
 * Choose the power state appropriate for the device depending on whether
 * it can wake up the system and/or is power manageable by the platform
 * (PCI_D3hot is the default) and put the device into that state.
 */
int pci_prepare_to_sleep(struct pci_dev *dev)
{
	bool wakeup = device_may_wakeup(&dev->dev);
	pci_power_t target_state = pci_target_state(dev, wakeup);
	int error;

	if (target_state == PCI_POWER_ERROR)
		return -EIO;

	/*
	 * There are systems (for example, Intel mobile chips since Coffee
	 * Lake) where the power drawn while suspended can be significantly
	 * reduced by disabling PTM on PCIe root ports as this allows the
	 * port to enter a lower-power PM state and the SoC to reach a
	 * lower-power idle state as a whole.
	 */
	if (pci_pcie_type(dev) == PCI_EXP_TYPE_ROOT_PORT)
		pci_disable_ptm(dev);

	pci_enable_wake(dev, target_state, wakeup);

	error = pci_set_power_state(dev, target_state);

	if (error) {
		pci_enable_wake(dev, target_state, false);
		pci_restore_ptm_state(dev);
	}

	return error;
}
EXPORT_SYMBOL(pci_prepare_to_sleep);

/**
 * pci_back_from_sleep - turn PCI device on during system-wide transition
 *			 into working state
 * @dev: Device to handle.
 *
 * Disable device's system wake-up capability and put it into D0.
 */
int pci_back_from_sleep(struct pci_dev *dev)
{
	pci_enable_wake(dev, PCI_D0, false);
	return pci_set_power_state(dev, PCI_D0);
}
EXPORT_SYMBOL(pci_back_from_sleep);

/**
 * pci_finish_runtime_suspend - Carry out PCI-specific part of runtime suspend.
 * @dev: PCI device being suspended.
 *
 * Prepare @dev to generate wake-up events at run time and put it into a low
 * power state.
 */
int pci_finish_runtime_suspend(struct pci_dev *dev)
{
	pci_power_t target_state;
	int error;

	target_state = pci_target_state(dev, device_can_wakeup(&dev->dev));
	if (target_state == PCI_POWER_ERROR)
		return -EIO;

	dev->runtime_d3cold = target_state == PCI_D3cold;

	/*
	 * There are systems (for example, Intel mobile chips since Coffee
	 * Lake) where the power drawn while suspended can be significantly
	 * reduced by disabling PTM on PCIe root ports as this allows the
	 * port to enter a lower-power PM state and the SoC to reach a
	 * lower-power idle state as a whole.
	 */
	if (pci_pcie_type(dev) == PCI_EXP_TYPE_ROOT_PORT)
		pci_disable_ptm(dev);

	__pci_enable_wake(dev, target_state, pci_dev_run_wake(dev));

	error = pci_set_power_state(dev, target_state);

	if (error) {
		pci_enable_wake(dev, target_state, false);
		pci_restore_ptm_state(dev);
		dev->runtime_d3cold = false;
	}

	return error;
}

/**
 * pci_dev_run_wake - Check if device can generate run-time wake-up events.
 * @dev: Device to check.
 *
 * Return true if the device itself is capable of generating wake-up events
 * (through the platform or using the native PCIe PME) or if the device supports
 * PME and one of its upstream bridges can generate wake-up events.
 */
bool pci_dev_run_wake(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->bus;

	if (!dev->pme_support)
		return false;

	/* PME-capable in principle, but not from the target power state */
	if (!pci_pme_capable(dev, pci_target_state(dev, true)))
		return false;

	if (device_can_wakeup(&dev->dev))
		return true;

	while (bus->parent) {
		struct pci_dev *bridge = bus->self;

		if (device_can_wakeup(&bridge->dev))
			return true;

		bus = bus->parent;
	}

	/* We have reached the root bus. */
	if (bus->bridge)
		return device_can_wakeup(bus->bridge);

	return false;
}
EXPORT_SYMBOL_GPL(pci_dev_run_wake);

/**
 * pci_dev_need_resume - Check if it is necessary to resume the device.
 * @pci_dev: Device to check.
 *
 * Return 'true' if the device is not runtime-suspended or it has to be
 * reconfigured due to wakeup settings difference between system and runtime
 * suspend, or the current power state of it is not suitable for the upcoming
 * (system-wide) transition.
 */
bool pci_dev_need_resume(struct pci_dev *pci_dev)
{
	struct device *dev = &pci_dev->dev;
	pci_power_t target_state;

	if (!pm_runtime_suspended(dev) || platform_pci_need_resume(pci_dev))
		return true;

	target_state = pci_target_state(pci_dev, device_may_wakeup(dev));

	/*
	 * If the earlier platform check has not triggered, D3cold is just power
	 * removal on top of D3hot, so no need to resume the device in that
	 * case.
	 */
	return target_state != pci_dev->current_state &&
		target_state != PCI_D3cold &&
		pci_dev->current_state != PCI_D3hot;
}

/**
 * pci_dev_adjust_pme - Adjust PME setting for a suspended device.
 * @pci_dev: Device to check.
 *
 * If the device is suspended and it is not configured for system wakeup,
 * disable PME for it to prevent it from waking up the system unnecessarily.
 *
 * Note that if the device's power state is D3cold and the platform check in
 * pci_dev_need_resume() has not triggered, the device's configuration need not
 * be changed.
 */
void pci_dev_adjust_pme(struct pci_dev *pci_dev)
{
	struct device *dev = &pci_dev->dev;

	spin_lock_irq(&dev->power.lock);

	if (pm_runtime_suspended(dev) && !device_may_wakeup(dev) &&
	    pci_dev->current_state < PCI_D3cold)
		__pci_pme_active(pci_dev, false);

	spin_unlock_irq(&dev->power.lock);
}

/**
 * pci_dev_complete_resume - Finalize resume from system sleep for a device.
 * @pci_dev: Device to handle.
 *
 * If the device is runtime suspended and wakeup-capable, enable PME for it as
 * it might have been disabled during the prepare phase of system suspend if
 * the device was not configured for system wakeup.
 */
void pci_dev_complete_resume(struct pci_dev *pci_dev)
{
	struct device *dev = &pci_dev->dev;

	if (!pci_dev_run_wake(pci_dev))
		return;

	spin_lock_irq(&dev->power.lock);

	if (pm_runtime_suspended(dev) && pci_dev->current_state < PCI_D3cold)
		__pci_pme_active(pci_dev, true);

	spin_unlock_irq(&dev->power.lock);
}

void pci_config_pm_runtime_get(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *parent = dev->parent;

	if (parent)
		pm_runtime_get_sync(parent);
	pm_runtime_get_noresume(dev);
	/*
	 * pdev->current_state is set to PCI_D3cold during suspending,
	 * so wait until suspending completes
	 */
	pm_runtime_barrier(dev);
	/*
	 * Only need to resume devices in D3cold, because config
	 * registers are still accessible for devices suspended but
	 * not in D3cold.
	 */
	if (pdev->current_state == PCI_D3cold)
		pm_runtime_resume(dev);
}

void pci_config_pm_runtime_put(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *parent = dev->parent;

	pm_runtime_put(dev);
	if (parent)
		pm_runtime_put_sync(parent);
}

static const struct dmi_system_id bridge_d3_blacklist[] = {
#ifdef CONFIG_X86
	{
		/*
		 * Gigabyte X299 root port is not marked as hotplug capable
		 * which allows Linux to power manage it.  However, this
		 * confuses the BIOS SMI handler so don't power manage root
		 * ports on that system.
		 */
		.ident = "X299 DESIGNARE EX-CF",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Gigabyte Technology Co., Ltd."),
			DMI_MATCH(DMI_BOARD_NAME, "X299 DESIGNARE EX-CF"),
		},
	},
#endif
	{ }
};

/**
 * pci_bridge_d3_possible - Is it possible to put the bridge into D3
 * @bridge: Bridge to check
 *
 * This function checks if it is possible to move the bridge to D3.
 * Currently we only allow D3 for recent enough PCIe ports and Thunderbolt.
 */
bool pci_bridge_d3_possible(struct pci_dev *bridge)
{
	if (!pci_is_pcie(bridge))
		return false;

	switch (pci_pcie_type(bridge)) {
	case PCI_EXP_TYPE_ROOT_PORT:
	case PCI_EXP_TYPE_UPSTREAM:
	case PCI_EXP_TYPE_DOWNSTREAM:
		if (pci_bridge_d3_disable)
			return false;

		/*
		 * Hotplug ports handled by firmware in System Management Mode
		 * may not be put into D3 by the OS (Thunderbolt on non-Macs).
		 */
		if (bridge->is_hotplug_bridge && !pciehp_is_native(bridge))
			return false;

		if (pci_bridge_d3_force)
			return true;

		/* Even the oldest 2010 Thunderbolt controller supports D3. */
		if (bridge->is_thunderbolt)
			return true;

		/* Platform might know better if the bridge supports D3 */
		if (platform_pci_bridge_d3(bridge))
			return true;

		/*
		 * Hotplug ports handled natively by the OS were not validated
		 * by vendors for runtime D3 at least until 2018 because there
		 * was no OS support.
		 */
		if (bridge->is_hotplug_bridge)
			return false;

		if (dmi_check_system(bridge_d3_blacklist))
			return false;

		/*
		 * It should be safe to put PCIe ports from 2015 or newer
		 * to D3.
		 */
		if (dmi_get_bios_year() >= 2015)
			return true;
		break;
	}

	return false;
}

static int pci_dev_check_d3cold(struct pci_dev *dev, void *data)
{
	bool *d3cold_ok = data;

	if (/* The device needs to be allowed to go D3cold ... */
	    dev->no_d3cold || !dev->d3cold_allowed ||

	    /* ... and if it is wakeup capable to do so from D3cold. */
	    (device_may_wakeup(&dev->dev) &&
	     !pci_pme_capable(dev, PCI_D3cold)) ||

	    /* If it is a bridge it must be allowed to go to D3. */
	    !pci_power_manageable(dev))

		*d3cold_ok = false;

	return !*d3cold_ok;
}

/*
 * pci_bridge_d3_update - Update bridge D3 capabilities
 * @dev: PCI device which is changed
 *
 * Update upstream bridge PM capabilities accordingly depending on if the
 * device PM configuration was changed or the device is being removed.  The
 * change is also propagated upstream.
 */
void pci_bridge_d3_update(struct pci_dev *dev)
{
	bool remove = !device_is_registered(&dev->dev);
	struct pci_dev *bridge;
	bool d3cold_ok = true;

	bridge = pci_upstream_bridge(dev);
	if (!bridge || !pci_bridge_d3_possible(bridge))
		return;

	/*
	 * If D3 is currently allowed for the bridge, removing one of its
	 * children won't change that.
	 */
	if (remove && bridge->bridge_d3)
		return;

	/*
	 * If D3 is currently allowed for the bridge and a child is added or
	 * changed, disallowance of D3 can only be caused by that child, so
	 * we only need to check that single device, not any of its siblings.
	 *
	 * If D3 is currently not allowed for the bridge, checking the device
	 * first may allow us to skip checking its siblings.
	 */
	if (!remove)
		pci_dev_check_d3cold(dev, &d3cold_ok);

	/*
	 * If D3 is currently not allowed for the bridge, this may be caused
	 * either by the device being changed/removed or any of its siblings,
	 * so we need to go through all children to find out if one of them
	 * continues to block D3.
	 */
	if (d3cold_ok && !bridge->bridge_d3)
		pci_walk_bus(bridge->subordinate, pci_dev_check_d3cold,
			     &d3cold_ok);

	if (bridge->bridge_d3 != d3cold_ok) {
		bridge->bridge_d3 = d3cold_ok;
		/* Propagate change to upstream bridges */
		pci_bridge_d3_update(bridge);
	}
}

/**
 * pci_d3cold_enable - Enable D3cold for device
 * @dev: PCI device to handle
 *
 * This function can be used in drivers to enable D3cold from the device
 * they handle.  It also updates upstream PCI bridge PM capabilities
 * accordingly.
 */
void pci_d3cold_enable(struct pci_dev *dev)
{
	if (dev->no_d3cold) {
		dev->no_d3cold = false;
		pci_bridge_d3_update(dev);
	}
}
EXPORT_SYMBOL_GPL(pci_d3cold_enable);

/**
 * pci_d3cold_disable - Disable D3cold for device
 * @dev: PCI device to handle
 *
 * This function can be used in drivers to disable D3cold from the device
 * they handle.  It also updates upstream PCI bridge PM capabilities
 * accordingly.
 */
void pci_d3cold_disable(struct pci_dev *dev)
{
	if (!dev->no_d3cold) {
		dev->no_d3cold = true;
		pci_bridge_d3_update(dev);
	}
}
EXPORT_SYMBOL_GPL(pci_d3cold_disable);

/**
 * pci_pm_init - Initialize PM functions of given PCI device
 * @dev: PCI device to handle.
 */
void pci_pm_init(struct pci_dev *dev)
{
	int pm;
	u16 status;
	u16 pmc;

	pm_runtime_forbid(&dev->dev);
	pm_runtime_set_active(&dev->dev);
	pm_runtime_enable(&dev->dev);
	device_enable_async_suspend(&dev->dev);
	dev->wakeup_prepared = false;

	dev->pm_cap = 0;
	dev->pme_support = 0;

	/* find PCI PM capability in list */
	pm = pci_find_capability(dev, PCI_CAP_ID_PM);
	if (!pm)
		return;
	/* Check device's ability to generate PME# */
	pci_read_config_word(dev, pm + PCI_PM_PMC, &pmc);

	if ((pmc & PCI_PM_CAP_VER_MASK) > 3) {
		pci_err(dev, "unsupported PM cap regs version (%u)\n",
			pmc & PCI_PM_CAP_VER_MASK);
		return;
	}

	dev->pm_cap = pm;
	dev->d3hot_delay = PCI_PM_D3HOT_WAIT;
	dev->d3cold_delay = PCI_PM_D3COLD_WAIT;
	dev->bridge_d3 = pci_bridge_d3_possible(dev);
	dev->d3cold_allowed = true;

	dev->d1_support = false;
	dev->d2_support = false;
	if (!pci_no_d1d2(dev)) {
		if (pmc & PCI_PM_CAP_D1)
			dev->d1_support = true;
		if (pmc & PCI_PM_CAP_D2)
			dev->d2_support = true;

		if (dev->d1_support || dev->d2_support)
			pci_info(dev, "supports%s%s\n",
				   dev->d1_support ? " D1" : "",
				   dev->d2_support ? " D2" : "");
	}

	pmc &= PCI_PM_CAP_PME_MASK;
	if (pmc) {
		pci_info(dev, "PME# supported from%s%s%s%s%s\n",
			 (pmc & PCI_PM_CAP_PME_D0) ? " D0" : "",
			 (pmc & PCI_PM_CAP_PME_D1) ? " D1" : "",
			 (pmc & PCI_PM_CAP_PME_D2) ? " D2" : "",
			 (pmc & PCI_PM_CAP_PME_D3hot) ? " D3hot" : "",
			 (pmc & PCI_PM_CAP_PME_D3cold) ? " D3cold" : "");
		dev->pme_support = pmc >> PCI_PM_CAP_PME_SHIFT;
		dev->pme_poll = true;
		/*
		 * Make device's PM flags reflect the wake-up capability, but
		 * let the user space enable it to wake up the system as needed.
		 */
		device_set_wakeup_capable(&dev->dev, true);
		/* Disable the PME# generation functionality */
		pci_pme_active(dev, false);
	}

	pci_read_config_word(dev, PCI_STATUS, &status);
	if (status & PCI_STATUS_IMM_READY)
		dev->imm_ready = 1;
}

static unsigned long pci_ea_flags(struct pci_dev *dev, u8 prop)
{
	unsigned long flags = IORESOURCE_PCI_FIXED | IORESOURCE_PCI_EA_BEI;

	switch (prop) {
	case PCI_EA_P_MEM:
	case PCI_EA_P_VF_MEM:
		flags |= IORESOURCE_MEM;
		break;
	case PCI_EA_P_MEM_PREFETCH:
	case PCI_EA_P_VF_MEM_PREFETCH:
		flags |= IORESOURCE_MEM | IORESOURCE_PREFETCH;
		break;
	case PCI_EA_P_IO:
		flags |= IORESOURCE_IO;
		break;
	default:
		return 0;
	}

	return flags;
}

static struct resource *pci_ea_get_resource(struct pci_dev *dev, u8 bei,
					    u8 prop)
{
	if (bei <= PCI_EA_BEI_BAR5 && prop <= PCI_EA_P_IO)
		return &dev->resource[bei];
#ifdef CONFIG_PCI_IOV
	else if (bei >= PCI_EA_BEI_VF_BAR0 && bei <= PCI_EA_BEI_VF_BAR5 &&
		 (prop == PCI_EA_P_VF_MEM || prop == PCI_EA_P_VF_MEM_PREFETCH))
		return &dev->resource[PCI_IOV_RESOURCES +
				      bei - PCI_EA_BEI_VF_BAR0];
#endif
	else if (bei == PCI_EA_BEI_ROM)
		return &dev->resource[PCI_ROM_RESOURCE];
	else
		return NULL;
}

/* Read an Enhanced Allocation (EA) entry */
static int pci_ea_read(struct pci_dev *dev, int offset)
{
	struct resource *res;
	int ent_size, ent_offset = offset;
	resource_size_t start, end;
	unsigned long flags;
	u32 dw0, bei, base, max_offset;
	u8 prop;
	bool support_64 = (sizeof(resource_size_t) >= 8);

	pci_read_config_dword(dev, ent_offset, &dw0);
	ent_offset += 4;

	/* Entry size field indicates DWORDs after 1st */
	ent_size = ((dw0 & PCI_EA_ES) + 1) << 2;

	if (!(dw0 & PCI_EA_ENABLE)) /* Entry not enabled */
		goto out;

	bei = (dw0 & PCI_EA_BEI) >> 4;
	prop = (dw0 & PCI_EA_PP) >> 8;

	/*
	 * If the Property is in the reserved range, try the Secondary
	 * Property instead.
	 */
	if (prop > PCI_EA_P_BRIDGE_IO && prop < PCI_EA_P_MEM_RESERVED)
		prop = (dw0 & PCI_EA_SP) >> 16;
	if (prop > PCI_EA_P_BRIDGE_IO)
		goto out;

	res = pci_ea_get_resource(dev, bei, prop);
	if (!res) {
		pci_err(dev, "Unsupported EA entry BEI: %u\n", bei);
		goto out;
	}

	flags = pci_ea_flags(dev, prop);
	if (!flags) {
		pci_err(dev, "Unsupported EA properties: %#x\n", prop);
		goto out;
	}

	/* Read Base */
	pci_read_config_dword(dev, ent_offset, &base);
	start = (base & PCI_EA_FIELD_MASK);
	ent_offset += 4;

	/* Read MaxOffset */
	pci_read_config_dword(dev, ent_offset, &max_offset);
	ent_offset += 4;

	/* Read Base MSBs (if 64-bit entry) */
	if (base & PCI_EA_IS_64) {
		u32 base_upper;

		pci_read_config_dword(dev, ent_offset, &base_upper);
		ent_offset += 4;

		flags |= IORESOURCE_MEM_64;

		/* entry starts above 32-bit boundary, can't use */
		if (!support_64 && base_upper)
			goto out;

		if (support_64)
			start |= ((u64)base_upper << 32);
	}

	end = start + (max_offset | 0x03);

	/* Read MaxOffset MSBs (if 64-bit entry) */
	if (max_offset & PCI_EA_IS_64) {
		u32 max_offset_upper;

		pci_read_config_dword(dev, ent_offset, &max_offset_upper);
		ent_offset += 4;

		flags |= IORESOURCE_MEM_64;

		/* entry too big, can't use */
		if (!support_64 && max_offset_upper)
			goto out;

		if (support_64)
			end += ((u64)max_offset_upper << 32);
	}

	if (end < start) {
		pci_err(dev, "EA Entry crosses address boundary\n");
		goto out;
	}

	if (ent_size != ent_offset - offset) {
		pci_err(dev, "EA Entry Size (%d) does not match length read (%d)\n",
			ent_size, ent_offset - offset);
		goto out;
	}

	res->name = pci_name(dev);
	res->start = start;
	res->end = end;
	res->flags = flags;

	if (bei <= PCI_EA_BEI_BAR5)
		pci_info(dev, "BAR %d: %pR (from Enhanced Allocation, properties %#02x)\n",
			   bei, res, prop);
	else if (bei == PCI_EA_BEI_ROM)
		pci_info(dev, "ROM: %pR (from Enhanced Allocation, properties %#02x)\n",
			   res, prop);
	else if (bei >= PCI_EA_BEI_VF_BAR0 && bei <= PCI_EA_BEI_VF_BAR5)
		pci_info(dev, "VF BAR %d: %pR (from Enhanced Allocation, properties %#02x)\n",
			   bei - PCI_EA_BEI_VF_BAR0, res, prop);
	else
		pci_info(dev, "BEI %d res: %pR (from Enhanced Allocation, properties %#02x)\n",
			   bei, res, prop);

out:
	return offset + ent_size;
}

/* Enhanced Allocation Initialization */
void pci_ea_init(struct pci_dev *dev)
{
	int ea;
	u8 num_ent;
	int offset;
	int i;

	/* find PCI EA capability in list */
	ea = pci_find_capability(dev, PCI_CAP_ID_EA);
	if (!ea)
		return;

	/* determine the number of entries */
	pci_bus_read_config_byte(dev->bus, dev->devfn, ea + PCI_EA_NUM_ENT,
					&num_ent);
	num_ent &= PCI_EA_NUM_ENT_MASK;

	offset = ea + PCI_EA_FIRST_ENT;

	/* Skip DWORD 2 for type 1 functions */
	if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE)
		offset += 4;

	/* parse each EA entry */
	for (i = 0; i < num_ent; ++i)
		offset = pci_ea_read(dev, offset);
}

static void pci_add_saved_cap(struct pci_dev *pci_dev,
	struct pci_cap_saved_state *new_cap)
{
	hlist_add_head(&new_cap->next, &pci_dev->saved_cap_space);
}

/**
 * _pci_add_cap_save_buffer - allocate buffer for saving given
 *			      capability registers
 * @dev: the PCI device
 * @cap: the capability to allocate the buffer for
 * @extended: Standard or Extended capability ID
 * @size: requested size of the buffer
 */
static int _pci_add_cap_save_buffer(struct pci_dev *dev, u16 cap,
				    bool extended, unsigned int size)
{
	int pos;
	struct pci_cap_saved_state *save_state;

	if (extended)
		pos = pci_find_ext_capability(dev, cap);
	else
		pos = pci_find_capability(dev, cap);

	if (!pos)
		return 0;

	save_state = kzalloc(sizeof(*save_state) + size, GFP_KERNEL);
	if (!save_state)
		return -ENOMEM;

	save_state->cap.cap_nr = cap;
	save_state->cap.cap_extended = extended;
	save_state->cap.size = size;
	pci_add_saved_cap(dev, save_state);

	return 0;
}

int pci_add_cap_save_buffer(struct pci_dev *dev, char cap, unsigned int size)
{
	return _pci_add_cap_save_buffer(dev, cap, false, size);
}

int pci_add_ext_cap_save_buffer(struct pci_dev *dev, u16 cap, unsigned int size)
{
	return _pci_add_cap_save_buffer(dev, cap, true, size);
}

/**
 * pci_allocate_cap_save_buffers - allocate buffers for saving capabilities
 * @dev: the PCI device
 */
void pci_allocate_cap_save_buffers(struct pci_dev *dev)
{
	int error;

	error = pci_add_cap_save_buffer(dev, PCI_CAP_ID_EXP,
					PCI_EXP_SAVE_REGS * sizeof(u16));
	if (error)
		pci_err(dev, "unable to preallocate PCI Express save buffer\n");

	error = pci_add_cap_save_buffer(dev, PCI_CAP_ID_PCIX, sizeof(u16));
	if (error)
		pci_err(dev, "unable to preallocate PCI-X save buffer\n");

	error = pci_add_ext_cap_save_buffer(dev, PCI_EXT_CAP_ID_LTR,
					    2 * sizeof(u16));
	if (error)
		pci_err(dev, "unable to allocate suspend buffer for LTR\n");

	pci_allocate_vc_save_buffers(dev);
}

void pci_free_cap_save_buffers(struct pci_dev *dev)
{
	struct pci_cap_saved_state *tmp;
	struct hlist_node *n;

	hlist_for_each_entry_safe(tmp, n, &dev->saved_cap_space, next)
		kfree(tmp);
}

/**
 * pci_configure_ari - enable or disable ARI forwarding
 * @dev: the PCI device
 *
 * If @dev and its upstream bridge both support ARI, enable ARI in the
 * bridge.  Otherwise, disable ARI in the bridge.
 */
void pci_configure_ari(struct pci_dev *dev)
{
	u32 cap;
	struct pci_dev *bridge;

	if (pcie_ari_disabled || !pci_is_pcie(dev) || dev->devfn)
		return;

	bridge = dev->bus->self;
	if (!bridge)
		return;

	pcie_capability_read_dword(bridge, PCI_EXP_DEVCAP2, &cap);
	if (!(cap & PCI_EXP_DEVCAP2_ARI))
		return;

	if (pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ARI)) {
		pcie_capability_set_word(bridge, PCI_EXP_DEVCTL2,
					 PCI_EXP_DEVCTL2_ARI);
		bridge->ari_enabled = 1;
	} else {
		pcie_capability_clear_word(bridge, PCI_EXP_DEVCTL2,
					   PCI_EXP_DEVCTL2_ARI);
		bridge->ari_enabled = 0;
	}
}

static bool pci_acs_flags_enabled(struct pci_dev *pdev, u16 acs_flags)
{
	int pos;
	u16 cap, ctrl;

	pos = pdev->acs_cap;
	if (!pos)
		return false;

	/*
	 * Except for egress control, capabilities are either required
	 * or only required if controllable.  Features missing from the
	 * capability field can therefore be assumed as hard-wired enabled.
	 */
	pci_read_config_word(pdev, pos + PCI_ACS_CAP, &cap);
	acs_flags &= (cap | PCI_ACS_EC);

	pci_read_config_word(pdev, pos + PCI_ACS_CTRL, &ctrl);
	return (ctrl & acs_flags) == acs_flags;
}

/**
 * pci_acs_enabled - test ACS against required flags for a given device
 * @pdev: device to test
 * @acs_flags: required PCI ACS flags
 *
 * Return true if the device supports the provided flags.  Automatically
 * filters out flags that are not implemented on multifunction devices.
 *
 * Note that this interface checks the effective ACS capabilities of the
 * device rather than the actual capabilities.  For instance, most single
 * function endpoints are not required to support ACS because they have no
 * opportunity for peer-to-peer access.  We therefore return 'true'
 * regardless of whether the device exposes an ACS capability.  This makes
 * it much easier for callers of this function to ignore the actual type
 * or topology of the device when testing ACS support.
 */
bool pci_acs_enabled(struct pci_dev *pdev, u16 acs_flags)
{
	int ret;

	ret = pci_dev_specific_acs_enabled(pdev, acs_flags);
	if (ret >= 0)
		return ret > 0;

	/*
	 * Conventional PCI and PCI-X devices never support ACS, either
	 * effectively or actually.  The shared bus topology implies that
	 * any device on the bus can receive or snoop DMA.
	 */
	if (!pci_is_pcie(pdev))
		return false;

	switch (pci_pcie_type(pdev)) {
	/*
	 * PCI/X-to-PCIe bridges are not specifically mentioned by the spec,
	 * but since their primary interface is PCI/X, we conservatively
	 * handle them as we would a non-PCIe device.
	 */
	case PCI_EXP_TYPE_PCIE_BRIDGE:
	/*
	 * PCIe 3.0, 6.12.1 excludes ACS on these devices.  "ACS is never
	 * applicable... must never implement an ACS Extended Capability...".
	 * This seems arbitrary, but we take a conservative interpretation
	 * of this statement.
	 */
	case PCI_EXP_TYPE_PCI_BRIDGE:
	case PCI_EXP_TYPE_RC_EC:
		return false;
	/*
	 * PCIe 3.0, 6.12.1.1 specifies that downstream and root ports should
	 * implement ACS in order to indicate their peer-to-peer capabilities,
	 * regardless of whether they are single- or multi-function devices.
	 */
	case PCI_EXP_TYPE_DOWNSTREAM:
	case PCI_EXP_TYPE_ROOT_PORT:
		return pci_acs_flags_enabled(pdev, acs_flags);
	/*
	 * PCIe 3.0, 6.12.1.2 specifies ACS capabilities that should be
	 * implemented by the remaining PCIe types to indicate peer-to-peer
	 * capabilities, but only when they are part of a multifunction
	 * device.  The footnote for section 6.12 indicates the specific
	 * PCIe types included here.
	 */
	case PCI_EXP_TYPE_ENDPOINT:
	case PCI_EXP_TYPE_UPSTREAM:
	case PCI_EXP_TYPE_LEG_END:
	case PCI_EXP_TYPE_RC_END:
		if (!pdev->multifunction)
			break;

		return pci_acs_flags_enabled(pdev, acs_flags);
	}

	/*
	 * PCIe 3.0, 6.12.1.3 specifies no ACS capabilities are applicable
	 * to single function devices with the exception of downstream ports.
	 */
	return true;
}

/**
 * pci_acs_path_enabled - test ACS flags from start to end in a hierarchy
 * @start: starting downstream device
 * @end: ending upstream device or NULL to search to the root bus
 * @acs_flags: required flags
 *
 * Walk up a device tree from start to end testing PCI ACS support.  If
 * any step along the way does not support the required flags, return false.
 */
bool pci_acs_path_enabled(struct pci_dev *start,
			  struct pci_dev *end, u16 acs_flags)
{
	struct pci_dev *pdev, *parent = start;

	do {
		pdev = parent;

		if (!pci_acs_enabled(pdev, acs_flags))
			return false;

		if (pci_is_root_bus(pdev->bus))
			return (end == NULL);

		parent = pdev->bus->self;
	} while (pdev != end);

	return true;
}

/**
 * pci_acs_init - Initialize ACS if hardware supports it
 * @dev: the PCI device
 */
void pci_acs_init(struct pci_dev *dev)
{
	dev->acs_cap = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ACS);

	/*
	 * Attempt to enable ACS regardless of capability because some Root
	 * Ports (e.g. those quirked with *_intel_pch_acs_*) do not have
	 * the standard ACS capability but still support ACS via those
	 * quirks.
	 */
	pci_enable_acs(dev);
}

/**
 * pci_rebar_find_pos - find position of resize ctrl reg for BAR
 * @pdev: PCI device
 * @bar: BAR to find
 *
 * Helper to find the position of the ctrl register for a BAR.
 * Returns -ENOTSUPP if resizable BARs are not supported at all.
 * Returns -ENOENT if no ctrl register for the BAR could be found.
 */
static int pci_rebar_find_pos(struct pci_dev *pdev, int bar)
{
	unsigned int pos, nbars, i;
	u32 ctrl;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_REBAR);
	if (!pos)
		return -ENOTSUPP;

	pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
	nbars = (ctrl & PCI_REBAR_CTRL_NBAR_MASK) >>
		    PCI_REBAR_CTRL_NBAR_SHIFT;

	for (i = 0; i < nbars; i++, pos += 8) {
		int bar_idx;

		pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
		bar_idx = ctrl & PCI_REBAR_CTRL_BAR_IDX;
		if (bar_idx == bar)
			return pos;
	}

	return -ENOENT;
}

/**
 * pci_rebar_get_possible_sizes - get possible sizes for BAR
 * @pdev: PCI device
 * @bar: BAR to query
 *
 * Get the possible sizes of a resizable BAR as bitmask defined in the spec
 * (bit 0=1MB, bit 19=512GB). Returns 0 if BAR isn't resizable.
 */
u32 pci_rebar_get_possible_sizes(struct pci_dev *pdev, int bar)
{
	int pos;
	u32 cap;

	pos = pci_rebar_find_pos(pdev, bar);
	if (pos < 0)
		return 0;

	pci_read_config_dword(pdev, pos + PCI_REBAR_CAP, &cap);
	cap &= PCI_REBAR_CAP_SIZES;

	/* Sapphire RX 5600 XT Pulse has an invalid cap dword for BAR 0 */
	if (pdev->vendor == PCI_VENDOR_ID_ATI && pdev->device == 0x731f &&
	    bar == 0 && cap == 0x7000)
		cap = 0x3f000;

	return cap >> 4;
}
EXPORT_SYMBOL(pci_rebar_get_possible_sizes);

/**
 * pci_rebar_get_current_size - get the current size of a BAR
 * @pdev: PCI device
 * @bar: BAR to set size to
 *
 * Read the size of a BAR from the resizable BAR config.
 * Returns size if found or negative error code.
 */
int pci_rebar_get_current_size(struct pci_dev *pdev, int bar)
{
	int pos;
	u32 ctrl;

	pos = pci_rebar_find_pos(pdev, bar);
	if (pos < 0)
		return pos;

	pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
	return (ctrl & PCI_REBAR_CTRL_BAR_SIZE) >> PCI_REBAR_CTRL_BAR_SHIFT;
}

/**
 * pci_rebar_set_size - set a new size for a BAR
 * @pdev: PCI device
 * @bar: BAR to set size to
 * @size: new size as defined in the spec (0=1MB, 19=512GB)
 *
 * Set the new size of a BAR as defined in the spec.
 * Returns zero if resizing was successful, error code otherwise.
 */
int pci_rebar_set_size(struct pci_dev *pdev, int bar, int size)
{
	int pos;
	u32 ctrl;

	pos = pci_rebar_find_pos(pdev, bar);
	if (pos < 0)
		return pos;

	pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
	ctrl &= ~PCI_REBAR_CTRL_BAR_SIZE;
	ctrl |= size << PCI_REBAR_CTRL_BAR_SHIFT;
	pci_write_config_dword(pdev, pos + PCI_REBAR_CTRL, ctrl);
	return 0;
}

/**
 * pci_enable_atomic_ops_to_root - enable AtomicOp requests to root port
 * @dev: the PCI device
 * @cap_mask: mask of desired AtomicOp sizes, including one or more of:
 *	PCI_EXP_DEVCAP2_ATOMIC_COMP32
 *	PCI_EXP_DEVCAP2_ATOMIC_COMP64
 *	PCI_EXP_DEVCAP2_ATOMIC_COMP128
 *
 * Return 0 if all upstream bridges support AtomicOp routing, egress
 * blocking is disabled on all upstream ports, and the root port supports
 * the requested completion capabilities (32-bit, 64-bit and/or 128-bit
 * AtomicOp completion), or negative otherwise.
 */
int pci_enable_atomic_ops_to_root(struct pci_dev *dev, u32 cap_mask)
{
	struct pci_bus *bus = dev->bus;
	struct pci_dev *bridge;
	u32 cap, ctl2;

	if (!pci_is_pcie(dev))
		return -EINVAL;

	/*
	 * Per PCIe r4.0, sec 6.15, endpoints and root ports may be
	 * AtomicOp requesters.  For now, we only support endpoints as
	 * requesters and root ports as completers.  No endpoints as
	 * completers, and no peer-to-peer.
	 */

	switch (pci_pcie_type(dev)) {
	case PCI_EXP_TYPE_ENDPOINT:
	case PCI_EXP_TYPE_LEG_END:
	case PCI_EXP_TYPE_RC_END:
		break;
	default:
		return -EINVAL;
	}

	while (bus->parent) {
		bridge = bus->self;

		pcie_capability_read_dword(bridge, PCI_EXP_DEVCAP2, &cap);

		switch (pci_pcie_type(bridge)) {
		/* Ensure switch ports support AtomicOp routing */
		case PCI_EXP_TYPE_UPSTREAM:
		case PCI_EXP_TYPE_DOWNSTREAM:
			if (!(cap & PCI_EXP_DEVCAP2_ATOMIC_ROUTE))
				return -EINVAL;
			break;

		/* Ensure root port supports all the sizes we care about */
		case PCI_EXP_TYPE_ROOT_PORT:
			if ((cap & cap_mask) != cap_mask)
				return -EINVAL;
			break;
		}

		/* Ensure upstream ports don't block AtomicOps on egress */
		if (pci_pcie_type(bridge) == PCI_EXP_TYPE_UPSTREAM) {
			pcie_capability_read_dword(bridge, PCI_EXP_DEVCTL2,
						   &ctl2);
			if (ctl2 & PCI_EXP_DEVCTL2_ATOMIC_EGRESS_BLOCK)
				return -EINVAL;
		}

		bus = bus->parent;
	}

	pcie_capability_set_word(dev, PCI_EXP_DEVCTL2,
				 PCI_EXP_DEVCTL2_ATOMIC_REQ);
	return 0;
}
EXPORT_SYMBOL(pci_enable_atomic_ops_to_root);

/**
 * pci_swizzle_interrupt_pin - swizzle INTx for device behind bridge
 * @dev: the PCI device
 * @pin: the INTx pin (1=INTA, 2=INTB, 3=INTC, 4=INTD)
 *
 * Perform INTx swizzling for a device behind one level of bridge.  This is
 * required by section 9.1 of the PCI-to-PCI bridge specification for devices
 * behind bridges on add-in cards.  For devices with ARI enabled, the slot
 * number is always 0 (see the Implementation Note in section 2.2.8.1 of
 * the PCI Express Base Specification, Revision 2.1)
 */
u8 pci_swizzle_interrupt_pin(const struct pci_dev *dev, u8 pin)
{
	int slot;

	if (pci_ari_enabled(dev->bus))
		slot = 0;
	else
		slot = PCI_SLOT(dev->devfn);

	return (((pin - 1) + slot) % 4) + 1;
}

int pci_get_interrupt_pin(struct pci_dev *dev, struct pci_dev **bridge)
{
	u8 pin;

	pin = dev->pin;
	if (!pin)
		return -1;

	while (!pci_is_root_bus(dev->bus)) {
		pin = pci_swizzle_interrupt_pin(dev, pin);
		dev = dev->bus->self;
	}
	*bridge = dev;
	return pin;
}

/**
 * pci_common_swizzle - swizzle INTx all the way to root bridge
 * @dev: the PCI device
 * @pinp: pointer to the INTx pin value (1=INTA, 2=INTB, 3=INTD, 4=INTD)
 *
 * Perform INTx swizzling for a device.  This traverses through all PCI-to-PCI
 * bridges all the way up to a PCI root bus.
 */
u8 pci_common_swizzle(struct pci_dev *dev, u8 *pinp)
{
	u8 pin = *pinp;

	while (!pci_is_root_bus(dev->bus)) {
		pin = pci_swizzle_interrupt_pin(dev, pin);
		dev = dev->bus->self;
	}
	*pinp = pin;
	return PCI_SLOT(dev->devfn);
}
EXPORT_SYMBOL_GPL(pci_common_swizzle);

/**
 * pci_release_region - Release a PCI bar
 * @pdev: PCI device whose resources were previously reserved by
 *	  pci_request_region()
 * @bar: BAR to release
 *
 * Releases the PCI I/O and memory resources previously reserved by a
 * successful call to pci_request_region().  Call this function only
 * after all use of the PCI regions has ceased.
 */
void pci_release_region(struct pci_dev *pdev, int bar)
{
	struct pci_devres *dr;

	if (pci_resource_len(pdev, bar) == 0)
		return;
	if (pci_resource_flags(pdev, bar) & IORESOURCE_IO)
		release_region(pci_resource_start(pdev, bar),
				pci_resource_len(pdev, bar));
	else if (pci_resource_flags(pdev, bar) & IORESOURCE_MEM)
		release_mem_region(pci_resource_start(pdev, bar),
				pci_resource_len(pdev, bar));

	dr = find_pci_dr(pdev);
	if (dr)
		dr->region_mask &= ~(1 << bar);
}
EXPORT_SYMBOL(pci_release_region);

/**
 * __pci_request_region - Reserved PCI I/O and memory resource
 * @pdev: PCI device whose resources are to be reserved
 * @bar: BAR to be reserved
 * @res_name: Name to be associated with resource.
 * @exclusive: whether the region access is exclusive or not
 *
 * Mark the PCI region associated with PCI device @pdev BAR @bar as
 * being reserved by owner @res_name.  Do not access any
 * address inside the PCI regions unless this call returns
 * successfully.
 *
 * If @exclusive is set, then the region is marked so that userspace
 * is explicitly not allowed to map the resource via /dev/mem or
 * sysfs MMIO access.
 *
 * Returns 0 on success, or %EBUSY on error.  A warning
 * message is also printed on failure.
 */
static int __pci_request_region(struct pci_dev *pdev, int bar,
				const char *res_name, int exclusive)
{
	struct pci_devres *dr;

	if (pci_resource_len(pdev, bar) == 0)
		return 0;

	if (pci_resource_flags(pdev, bar) & IORESOURCE_IO) {
		if (!request_region(pci_resource_start(pdev, bar),
			    pci_resource_len(pdev, bar), res_name))
			goto err_out;
	} else if (pci_resource_flags(pdev, bar) & IORESOURCE_MEM) {
		if (!__request_mem_region(pci_resource_start(pdev, bar),
					pci_resource_len(pdev, bar), res_name,
					exclusive))
			goto err_out;
	}

	dr = find_pci_dr(pdev);
	if (dr)
		dr->region_mask |= 1 << bar;

	return 0;

err_out:
	pci_warn(pdev, "BAR %d: can't reserve %pR\n", bar,
		 &pdev->resource[bar]);
	return -EBUSY;
}

/**
 * pci_request_region - Reserve PCI I/O and memory resource
 * @pdev: PCI device whose resources are to be reserved
 * @bar: BAR to be reserved
 * @res_name: Name to be associated with resource
 *
 * Mark the PCI region associated with PCI device @pdev BAR @bar as
 * being reserved by owner @res_name.  Do not access any
 * address inside the PCI regions unless this call returns
 * successfully.
 *
 * Returns 0 on success, or %EBUSY on error.  A warning
 * message is also printed on failure.
 */
int pci_request_region(struct pci_dev *pdev, int bar, const char *res_name)
{
	return __pci_request_region(pdev, bar, res_name, 0);
}
EXPORT_SYMBOL(pci_request_region);

/**
 * pci_release_selected_regions - Release selected PCI I/O and memory resources
 * @pdev: PCI device whose resources were previously reserved
 * @bars: Bitmask of BARs to be released
 *
 * Release selected PCI I/O and memory resources previously reserved.
 * Call this function only after all use of the PCI regions has ceased.
 */
void pci_release_selected_regions(struct pci_dev *pdev, int bars)
{
	int i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++)
		if (bars & (1 << i))
			pci_release_region(pdev, i);
}
EXPORT_SYMBOL(pci_release_selected_regions);

static int __pci_request_selected_regions(struct pci_dev *pdev, int bars,
					  const char *res_name, int excl)
{
	int i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++)
		if (bars & (1 << i))
			if (__pci_request_region(pdev, i, res_name, excl))
				goto err_out;
	return 0;

err_out:
	while (--i >= 0)
		if (bars & (1 << i))
			pci_release_region(pdev, i);

	return -EBUSY;
}


/**
 * pci_request_selected_regions - Reserve selected PCI I/O and memory resources
 * @pdev: PCI device whose resources are to be reserved
 * @bars: Bitmask of BARs to be requested
 * @res_name: Name to be associated with resource
 */
int pci_request_selected_regions(struct pci_dev *pdev, int bars,
				 const char *res_name)
{
	return __pci_request_selected_regions(pdev, bars, res_name, 0);
}
EXPORT_SYMBOL(pci_request_selected_regions);

int pci_request_selected_regions_exclusive(struct pci_dev *pdev, int bars,
					   const char *res_name)
{
	return __pci_request_selected_regions(pdev, bars, res_name,
			IORESOURCE_EXCLUSIVE);
}
EXPORT_SYMBOL(pci_request_selected_regions_exclusive);

/**
 * pci_release_regions - Release reserved PCI I/O and memory resources
 * @pdev: PCI device whose resources were previously reserved by
 *	  pci_request_regions()
 *
 * Releases all PCI I/O and memory resources previously reserved by a
 * successful call to pci_request_regions().  Call this function only
 * after all use of the PCI regions has ceased.
 */

void pci_release_regions(struct pci_dev *pdev)
{
	pci_release_selected_regions(pdev, (1 << PCI_STD_NUM_BARS) - 1);
}
EXPORT_SYMBOL(pci_release_regions);

/**
 * pci_request_regions - Reserve PCI I/O and memory resources
 * @pdev: PCI device whose resources are to be reserved
 * @res_name: Name to be associated with resource.
 *
 * Mark all PCI regions associated with PCI device @pdev as
 * being reserved by owner @res_name.  Do not access any
 * address inside the PCI regions unless this call returns
 * successfully.
 *
 * Returns 0 on success, or %EBUSY on error.  A warning
 * message is also printed on failure.
 */
int pci_request_regions(struct pci_dev *pdev, const char *res_name)
{
	return pci_request_selected_regions(pdev,
			((1 << PCI_STD_NUM_BARS) - 1), res_name);
}
EXPORT_SYMBOL(pci_request_regions);

/**
 * pci_request_regions_exclusive - Reserve PCI I/O and memory resources
 * @pdev: PCI device whose resources are to be reserved
 * @res_name: Name to be associated with resource.
 *
 * Mark all PCI regions associated with PCI device @pdev as being reserved
 * by owner @res_name.  Do not access any address inside the PCI regions
 * unless this call returns successfully.
 *
 * pci_request_regions_exclusive() will mark the region so that /dev/mem
 * and the sysfs MMIO access will not be allowed.
 *
 * Returns 0 on success, or %EBUSY on error.  A warning message is also
 * printed on failure.
 */
int pci_request_regions_exclusive(struct pci_dev *pdev, const char *res_name)
{
	return pci_request_selected_regions_exclusive(pdev,
				((1 << PCI_STD_NUM_BARS) - 1), res_name);
}
EXPORT_SYMBOL(pci_request_regions_exclusive);

/*
 * Record the PCI IO range (expressed as CPU physical address + size).
 * Return a negative value if an error has occurred, zero otherwise
 */
int pci_register_io_range(struct fwnode_handle *fwnode, phys_addr_t addr,
			resource_size_t	size)
{
	int ret = 0;
#ifdef PCI_IOBASE
	struct logic_pio_hwaddr *range;

	if (!size || addr + size < addr)
		return -EINVAL;

	range = kzalloc(sizeof(*range), GFP_ATOMIC);
	if (!range)
		return -ENOMEM;

	range->fwnode = fwnode;
	range->size = size;
	range->hw_start = addr;
	range->flags = LOGIC_PIO_CPU_MMIO;

	ret = logic_pio_register_range(range);
	if (ret)
		kfree(range);

	/* Ignore duplicates due to deferred probing */
	if (ret == -EEXIST)
		ret = 0;
#endif

	return ret;
}

phys_addr_t pci_pio_to_address(unsigned long pio)
{
	phys_addr_t address = (phys_addr_t)OF_BAD_ADDR;

#ifdef PCI_IOBASE
	if (pio >= MMIO_UPPER_LIMIT)
		return address;

	address = logic_pio_to_hwaddr(pio);
#endif

	return address;
}
EXPORT_SYMBOL_GPL(pci_pio_to_address);

unsigned long __weak pci_address_to_pio(phys_addr_t address)
{
#ifdef PCI_IOBASE
	return logic_pio_trans_cpuaddr(address);
#else
	if (address > IO_SPACE_LIMIT)
		return (unsigned long)-1;

	return (unsigned long) address;
#endif
}

/**
 * pci_remap_iospace - Remap the memory mapped I/O space
 * @res: Resource describing the I/O space
 * @phys_addr: physical address of range to be mapped
 *
 * Remap the memory mapped I/O space described by the @res and the CPU
 * physical address @phys_addr into virtual address space.  Only
 * architectures that have memory mapped IO functions defined (and the
 * PCI_IOBASE value defined) should call this function.
 */
int pci_remap_iospace(const struct resource *res, phys_addr_t phys_addr)
{
#if defined(PCI_IOBASE) && defined(CONFIG_MMU)
	unsigned long vaddr = (unsigned long)PCI_IOBASE + res->start;

	if (!(res->flags & IORESOURCE_IO))
		return -EINVAL;

	if (res->end > IO_SPACE_LIMIT)
		return -EINVAL;

	return ioremap_page_range(vaddr, vaddr + resource_size(res), phys_addr,
				  pgprot_device(PAGE_KERNEL));
#else
	/*
	 * This architecture does not have memory mapped I/O space,
	 * so this function should never be called
	 */
	WARN_ONCE(1, "This architecture does not support memory mapped I/O\n");
	return -ENODEV;
#endif
}
EXPORT_SYMBOL(pci_remap_iospace);

/**
 * pci_unmap_iospace - Unmap the memory mapped I/O space
 * @res: resource to be unmapped
 *
 * Unmap the CPU virtual address @res from virtual address space.  Only
 * architectures that have memory mapped IO functions defined (and the
 * PCI_IOBASE value defined) should call this function.
 */
void pci_unmap_iospace(struct resource *res)
{
#if defined(PCI_IOBASE) && defined(CONFIG_MMU)
	unsigned long vaddr = (unsigned long)PCI_IOBASE + res->start;

	vunmap_range(vaddr, vaddr + resource_size(res));
#endif
}
EXPORT_SYMBOL(pci_unmap_iospace);

static void devm_pci_unmap_iospace(struct device *dev, void *ptr)
{
	struct resource **res = ptr;

	pci_unmap_iospace(*res);
}

/**
 * devm_pci_remap_iospace - Managed pci_remap_iospace()
 * @dev: Generic device to remap IO address for
 * @res: Resource describing the I/O space
 * @phys_addr: physical address of range to be mapped
 *
 * Managed pci_remap_iospace().  Map is automatically unmapped on driver
 * detach.
 */
int devm_pci_remap_iospace(struct device *dev, const struct resource *res,
			   phys_addr_t phys_addr)
{
	const struct resource **ptr;
	int error;

	ptr = devres_alloc(devm_pci_unmap_iospace, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	error = pci_remap_iospace(res, phys_addr);
	if (error) {
		devres_free(ptr);
	} else	{
		*ptr = res;
		devres_add(dev, ptr);
	}

	return error;
}
EXPORT_SYMBOL(devm_pci_remap_iospace);

/**
 * devm_pci_remap_cfgspace - Managed pci_remap_cfgspace()
 * @dev: Generic device to remap IO address for
 * @offset: Resource address to map
 * @size: Size of map
 *
 * Managed pci_remap_cfgspace().  Map is automatically unmapped on driver
 * detach.
 */
void __iomem *devm_pci_remap_cfgspace(struct device *dev,
				      resource_size_t offset,
				      resource_size_t size)
{
	void __iomem **ptr, *addr;

	ptr = devres_alloc(devm_ioremap_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	addr = pci_remap_cfgspace(offset, size);
	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}
EXPORT_SYMBOL(devm_pci_remap_cfgspace);

/**
 * devm_pci_remap_cfg_resource - check, request region and ioremap cfg resource
 * @dev: generic device to handle the resource for
 * @res: configuration space resource to be handled
 *
 * Checks that a resource is a valid memory region, requests the memory
 * region and ioremaps with pci_remap_cfgspace() API that ensures the
 * proper PCI configuration space memory attributes are guaranteed.
 *
 * All operations are managed and will be undone on driver detach.
 *
 * Returns a pointer to the remapped memory or an ERR_PTR() encoded error code
 * on failure. Usage example::
 *
 *	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
 *	base = devm_pci_remap_cfg_resource(&pdev->dev, res);
 *	if (IS_ERR(base))
 *		return PTR_ERR(base);
 */
void __iomem *devm_pci_remap_cfg_resource(struct device *dev,
					  struct resource *res)
{
	resource_size_t size;
	const char *name;
	void __iomem *dest_ptr;

	BUG_ON(!dev);

	if (!res || resource_type(res) != IORESOURCE_MEM) {
		dev_err(dev, "invalid resource\n");
		return IOMEM_ERR_PTR(-EINVAL);
	}

	size = resource_size(res);

	if (res->name)
		name = devm_kasprintf(dev, GFP_KERNEL, "%s %s", dev_name(dev),
				      res->name);
	else
		name = devm_kstrdup(dev, dev_name(dev), GFP_KERNEL);
	if (!name)
		return IOMEM_ERR_PTR(-ENOMEM);

	if (!devm_request_mem_region(dev, res->start, size, name)) {
		dev_err(dev, "can't request region for resource %pR\n", res);
		return IOMEM_ERR_PTR(-EBUSY);
	}

	dest_ptr = devm_pci_remap_cfgspace(dev, res->start, size);
	if (!dest_ptr) {
		dev_err(dev, "ioremap failed for resource %pR\n", res);
		devm_release_mem_region(dev, res->start, size);
		dest_ptr = IOMEM_ERR_PTR(-ENOMEM);
	}

	return dest_ptr;
}
EXPORT_SYMBOL(devm_pci_remap_cfg_resource);

static void __pci_set_master(struct pci_dev *dev, bool enable)
{
	u16 old_cmd, cmd;

	pci_read_config_word(dev, PCI_COMMAND, &old_cmd);
	if (enable)
		cmd = old_cmd | PCI_COMMAND_MASTER;
	else
		cmd = old_cmd & ~PCI_COMMAND_MASTER;
	if (cmd != old_cmd) {
		pci_dbg(dev, "%s bus mastering\n",
			enable ? "enabling" : "disabling");
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	dev->is_busmaster = enable;
}

/**
 * pcibios_setup - process "pci=" kernel boot arguments
 * @str: string used to pass in "pci=" kernel boot arguments
 *
 * Process kernel boot arguments.  This is the default implementation.
 * Architecture specific implementations can override this as necessary.
 */
char * __weak __init pcibios_setup(char *str)
{
	return str;
}

/**
 * pcibios_set_master - enable PCI bus-mastering for device dev
 * @dev: the PCI device to enable
 *
 * Enables PCI bus-mastering for the device.  This is the default
 * implementation.  Architecture specific implementations can override
 * this if necessary.
 */
void __weak pcibios_set_master(struct pci_dev *dev)
{
	u8 lat;

	/* The latency timer doesn't apply to PCIe (either Type 0 or Type 1) */
	if (pci_is_pcie(dev))
		return;

	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	if (lat < 16)
		lat = (64 <= pcibios_max_latency) ? 64 : pcibios_max_latency;
	else if (lat > pcibios_max_latency)
		lat = pcibios_max_latency;
	else
		return;

	pci_write_config_byte(dev, PCI_LATENCY_TIMER, lat);
}

/**
 * pci_set_master - enables bus-mastering for device dev
 * @dev: the PCI device to enable
 *
 * Enables bus-mastering on the device and calls pcibios_set_master()
 * to do the needed arch specific settings.
 */
void pci_set_master(struct pci_dev *dev)
{
	__pci_set_master(dev, true);
	pcibios_set_master(dev);
}
EXPORT_SYMBOL(pci_set_master);

/**
 * pci_clear_master - disables bus-mastering for device dev
 * @dev: the PCI device to disable
 */
void pci_clear_master(struct pci_dev *dev)
{
	__pci_set_master(dev, false);
}
EXPORT_SYMBOL(pci_clear_master);

/**
 * pci_set_cacheline_size - ensure the CACHE_LINE_SIZE register is programmed
 * @dev: the PCI device for which MWI is to be enabled
 *
 * Helper function for pci_set_mwi.
 * Originally copied from drivers/net/acenic.c.
 * Copyright 1998-2001 by Jes Sorensen, <jes@trained-monkey.org>.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int pci_set_cacheline_size(struct pci_dev *dev)
{
	u8 cacheline_size;

	if (!pci_cache_line_size)
		return -EINVAL;

	/* Validate current setting: the PCI_CACHE_LINE_SIZE must be
	   equal to or multiple of the right value. */
	pci_read_config_byte(dev, PCI_CACHE_LINE_SIZE, &cacheline_size);
	if (cacheline_size >= pci_cache_line_size &&
	    (cacheline_size % pci_cache_line_size) == 0)
		return 0;

	/* Write the correct value. */
	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, pci_cache_line_size);
	/* Read it back. */
	pci_read_config_byte(dev, PCI_CACHE_LINE_SIZE, &cacheline_size);
	if (cacheline_size == pci_cache_line_size)
		return 0;

	pci_dbg(dev, "cache line size of %d is not supported\n",
		   pci_cache_line_size << 2);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(pci_set_cacheline_size);

/**
 * pci_set_mwi - enables memory-write-invalidate PCI transaction
 * @dev: the PCI device for which MWI is enabled
 *
 * Enables the Memory-Write-Invalidate transaction in %PCI_COMMAND.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int pci_set_mwi(struct pci_dev *dev)
{
#ifdef PCI_DISABLE_MWI
	return 0;
#else
	int rc;
	u16 cmd;

	rc = pci_set_cacheline_size(dev);
	if (rc)
		return rc;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (!(cmd & PCI_COMMAND_INVALIDATE)) {
		pci_dbg(dev, "enabling Mem-Wr-Inval\n");
		cmd |= PCI_COMMAND_INVALIDATE;
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
#endif
}
EXPORT_SYMBOL(pci_set_mwi);

/**
 * pcim_set_mwi - a device-managed pci_set_mwi()
 * @dev: the PCI device for which MWI is enabled
 *
 * Managed pci_set_mwi().
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int pcim_set_mwi(struct pci_dev *dev)
{
	struct pci_devres *dr;

	dr = find_pci_dr(dev);
	if (!dr)
		return -ENOMEM;

	dr->mwi = 1;
	return pci_set_mwi(dev);
}
EXPORT_SYMBOL(pcim_set_mwi);

/**
 * pci_try_set_mwi - enables memory-write-invalidate PCI transaction
 * @dev: the PCI device for which MWI is enabled
 *
 * Enables the Memory-Write-Invalidate transaction in %PCI_COMMAND.
 * Callers are not required to check the return value.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int pci_try_set_mwi(struct pci_dev *dev)
{
#ifdef PCI_DISABLE_MWI
	return 0;
#else
	return pci_set_mwi(dev);
#endif
}
EXPORT_SYMBOL(pci_try_set_mwi);

/**
 * pci_clear_mwi - disables Memory-Write-Invalidate for device dev
 * @dev: the PCI device to disable
 *
 * Disables PCI Memory-Write-Invalidate transaction on the device
 */
void pci_clear_mwi(struct pci_dev *dev)
{
#ifndef PCI_DISABLE_MWI
	u16 cmd;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (cmd & PCI_COMMAND_INVALIDATE) {
		cmd &= ~PCI_COMMAND_INVALIDATE;
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
#endif
}
EXPORT_SYMBOL(pci_clear_mwi);

/**
 * pci_disable_parity - disable parity checking for device
 * @dev: the PCI device to operate on
 *
 * Disable parity checking for device @dev
 */
void pci_disable_parity(struct pci_dev *dev)
{
	u16 cmd;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (cmd & PCI_COMMAND_PARITY) {
		cmd &= ~PCI_COMMAND_PARITY;
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
}

/**
 * pci_intx - enables/disables PCI INTx for device dev
 * @pdev: the PCI device to operate on
 * @enable: boolean: whether to enable or disable PCI INTx
 *
 * Enables/disables PCI INTx for device @pdev
 */
void pci_intx(struct pci_dev *pdev, int enable)
{
	u16 pci_command, new;

	pci_read_config_word(pdev, PCI_COMMAND, &pci_command);

	if (enable)
		new = pci_command & ~PCI_COMMAND_INTX_DISABLE;
	else
		new = pci_command | PCI_COMMAND_INTX_DISABLE;

	if (new != pci_command) {
		struct pci_devres *dr;

		pci_write_config_word(pdev, PCI_COMMAND, new);

		dr = find_pci_dr(pdev);
		if (dr && !dr->restore_intx) {
			dr->restore_intx = 1;
			dr->orig_intx = !enable;
		}
	}
}
EXPORT_SYMBOL_GPL(pci_intx);

static bool pci_check_and_set_intx_mask(struct pci_dev *dev, bool mask)
{
	struct pci_bus *bus = dev->bus;
	bool mask_updated = true;
	u32 cmd_status_dword;
	u16 origcmd, newcmd;
	unsigned long flags;
	bool irq_pending;

	/*
	 * We do a single dword read to retrieve both command and status.
	 * Document assumptions that make this possible.
	 */
	BUILD_BUG_ON(PCI_COMMAND % 4);
	BUILD_BUG_ON(PCI_COMMAND + 2 != PCI_STATUS);

	raw_spin_lock_irqsave(&pci_lock, flags);

	bus->ops->read(bus, dev->devfn, PCI_COMMAND, 4, &cmd_status_dword);

	irq_pending = (cmd_status_dword >> 16) & PCI_STATUS_INTERRUPT;

	/*
	 * Check interrupt status register to see whether our device
	 * triggered the interrupt (when masking) or the next IRQ is
	 * already pending (when unmasking).
	 */
	if (mask != irq_pending) {
		mask_updated = false;
		goto done;
	}

	origcmd = cmd_status_dword;
	newcmd = origcmd & ~PCI_COMMAND_INTX_DISABLE;
	if (mask)
		newcmd |= PCI_COMMAND_INTX_DISABLE;
	if (newcmd != origcmd)
		bus->ops->write(bus, dev->devfn, PCI_COMMAND, 2, newcmd);

done:
	raw_spin_unlock_irqrestore(&pci_lock, flags);

	return mask_updated;
}

/**
 * pci_check_and_mask_intx - mask INTx on pending interrupt
 * @dev: the PCI device to operate on
 *
 * Check if the device dev has its INTx line asserted, mask it and return
 * true in that case. False is returned if no interrupt was pending.
 */
bool pci_check_and_mask_intx(struct pci_dev *dev)
{
	return pci_check_and_set_intx_mask(dev, true);
}
EXPORT_SYMBOL_GPL(pci_check_and_mask_intx);

/**
 * pci_check_and_unmask_intx - unmask INTx if no interrupt is pending
 * @dev: the PCI device to operate on
 *
 * Check if the device dev has its INTx line asserted, unmask it if not and
 * return true. False is returned and the mask remains active if there was
 * still an interrupt pending.
 */
bool pci_check_and_unmask_intx(struct pci_dev *dev)
{
	return pci_check_and_set_intx_mask(dev, false);
}
EXPORT_SYMBOL_GPL(pci_check_and_unmask_intx);

/**
 * pci_wait_for_pending_transaction - wait for pending transaction
 * @dev: the PCI device to operate on
 *
 * Return 0 if transaction is pending 1 otherwise.
 */
int pci_wait_for_pending_transaction(struct pci_dev *dev)
{
	if (!pci_is_pcie(dev))
		return 1;

	return pci_wait_for_pending(dev, pci_pcie_cap(dev) + PCI_EXP_DEVSTA,
				    PCI_EXP_DEVSTA_TRPND);
}
EXPORT_SYMBOL(pci_wait_for_pending_transaction);

/**
 * pcie_has_flr - check if a device supports function level resets
 * @dev: device to check
 *
 * Returns true if the device advertises support for PCIe function level
 * resets.
 */
bool pcie_has_flr(struct pci_dev *dev)
{
	u32 cap;

	if (dev->dev_flags & PCI_DEV_FLAGS_NO_FLR_RESET)
		return false;

	pcie_capability_read_dword(dev, PCI_EXP_DEVCAP, &cap);
	return cap & PCI_EXP_DEVCAP_FLR;
}
EXPORT_SYMBOL_GPL(pcie_has_flr);

/**
 * pcie_flr - initiate a PCIe function level reset
 * @dev: device to reset
 *
 * Initiate a function level reset on @dev.  The caller should ensure the
 * device supports FLR before calling this function, e.g. by using the
 * pcie_has_flr() helper.
 */
int pcie_flr(struct pci_dev *dev)
{
	if (!pci_wait_for_pending_transaction(dev))
		pci_err(dev, "timed out waiting for pending transaction; performing function level reset anyway\n");

	pcie_capability_set_word(dev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_BCR_FLR);

	if (dev->imm_ready)
		return 0;

	/*
	 * Per PCIe r4.0, sec 6.6.2, a device must complete an FLR within
	 * 100ms, but may silently discard requests while the FLR is in
	 * progress.  Wait 100ms before trying to access the device.
	 */
	msleep(100);

	return pci_dev_wait(dev, "FLR", PCIE_RESET_READY_POLL_MS);
}
EXPORT_SYMBOL_GPL(pcie_flr);

static int pci_af_flr(struct pci_dev *dev, int probe)
{
	int pos;
	u8 cap;

	pos = pci_find_capability(dev, PCI_CAP_ID_AF);
	if (!pos)
		return -ENOTTY;

	if (dev->dev_flags & PCI_DEV_FLAGS_NO_FLR_RESET)
		return -ENOTTY;

	pci_read_config_byte(dev, pos + PCI_AF_CAP, &cap);
	if (!(cap & PCI_AF_CAP_TP) || !(cap & PCI_AF_CAP_FLR))
		return -ENOTTY;

	if (probe)
		return 0;

	/*
	 * Wait for Transaction Pending bit to clear.  A word-aligned test
	 * is used, so we use the control offset rather than status and shift
	 * the test bit to match.
	 */
	if (!pci_wait_for_pending(dev, pos + PCI_AF_CTRL,
				 PCI_AF_STATUS_TP << 8))
		pci_err(dev, "timed out waiting for pending transaction; performing AF function level reset anyway\n");

	pci_write_config_byte(dev, pos + PCI_AF_CTRL, PCI_AF_CTRL_FLR);

	if (dev->imm_ready)
		return 0;

	/*
	 * Per Advanced Capabilities for Conventional PCI ECN, 13 April 2006,
	 * updated 27 July 2006; a device must complete an FLR within
	 * 100ms, but may silently discard requests while the FLR is in
	 * progress.  Wait 100ms before trying to access the device.
	 */
	msleep(100);

	return pci_dev_wait(dev, "AF_FLR", PCIE_RESET_READY_POLL_MS);
}

/**
 * pci_pm_reset - Put device into PCI_D3 and back into PCI_D0.
 * @dev: Device to reset.
 * @probe: If set, only check if the device can be reset this way.
 *
 * If @dev supports native PCI PM and its PCI_PM_CTRL_NO_SOFT_RESET flag is
 * unset, it will be reinitialized internally when going from PCI_D3hot to
 * PCI_D0.  If that's the case and the device is not in a low-power state
 * already, force it into PCI_D3hot and back to PCI_D0, causing it to be reset.
 *
 * NOTE: This causes the caller to sleep for twice the device power transition
 * cooldown period, which for the D0->D3hot and D3hot->D0 transitions is 10 ms
 * by default (i.e. unless the @dev's d3hot_delay field has a different value).
 * Moreover, only devices in D0 can be reset by this function.
 */
static int pci_pm_reset(struct pci_dev *dev, int probe)
{
	u16 csr;

	if (!dev->pm_cap || dev->dev_flags & PCI_DEV_FLAGS_NO_PM_RESET)
		return -ENOTTY;

	pci_read_config_word(dev, dev->pm_cap + PCI_PM_CTRL, &csr);
	if (csr & PCI_PM_CTRL_NO_SOFT_RESET)
		return -ENOTTY;

	if (probe)
		return 0;

	if (dev->current_state != PCI_D0)
		return -EINVAL;

	csr &= ~PCI_PM_CTRL_STATE_MASK;
	csr |= PCI_D3hot;
	pci_write_config_word(dev, dev->pm_cap + PCI_PM_CTRL, csr);
	pci_dev_d3_sleep(dev);

	csr &= ~PCI_PM_CTRL_STATE_MASK;
	csr |= PCI_D0;
	pci_write_config_word(dev, dev->pm_cap + PCI_PM_CTRL, csr);
	pci_dev_d3_sleep(dev);

	return pci_dev_wait(dev, "PM D3hot->D0", PCIE_RESET_READY_POLL_MS);
}

/**
 * pcie_wait_for_link_delay - Wait until link is active or inactive
 * @pdev: Bridge device
 * @active: waiting for active or inactive?
 * @delay: Delay to wait after link has become active (in ms)
 *
 * Use this to wait till link becomes active or inactive.
 */
static bool pcie_wait_for_link_delay(struct pci_dev *pdev, bool active,
				     int delay)
{
	int timeout = 1000;
	bool ret;
	u16 lnk_status;

	/*
	 * Some controllers might not implement link active reporting. In this
	 * case, we wait for 1000 ms + any delay requested by the caller.
	 */
	if (!pdev->link_active_reporting) {
		msleep(timeout + delay);
		return true;
	}

	/*
	 * PCIe r4.0 sec 6.6.1, a component must enter LTSSM Detect within 20ms,
	 * after which we should expect an link active if the reset was
	 * successful. If so, software must wait a minimum 100ms before sending
	 * configuration requests to devices downstream this port.
	 *
	 * If the link fails to activate, either the device was physically
	 * removed or the link is permanently failed.
	 */
	if (active)
		msleep(20);
	for (;;) {
		pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnk_status);
		ret = !!(lnk_status & PCI_EXP_LNKSTA_DLLLA);
		if (ret == active)
			break;
		if (timeout <= 0)
			break;
		msleep(10);
		timeout -= 10;
	}
	if (active && ret)
		msleep(delay);

	return ret == active;
}

/**
 * pcie_wait_for_link - Wait until link is active or inactive
 * @pdev: Bridge device
 * @active: waiting for active or inactive?
 *
 * Use this to wait till link becomes active or inactive.
 */
bool pcie_wait_for_link(struct pci_dev *pdev, bool active)
{
	return pcie_wait_for_link_delay(pdev, active, 100);
}

/*
 * Find maximum D3cold delay required by all the devices on the bus.  The
 * spec says 100 ms, but firmware can lower it and we allow drivers to
 * increase it as well.
 *
 * Called with @pci_bus_sem locked for reading.
 */
static int pci_bus_max_d3cold_delay(const struct pci_bus *bus)
{
	const struct pci_dev *pdev;
	int min_delay = 100;
	int max_delay = 0;

	list_for_each_entry(pdev, &bus->devices, bus_list) {
		if (pdev->d3cold_delay < min_delay)
			min_delay = pdev->d3cold_delay;
		if (pdev->d3cold_delay > max_delay)
			max_delay = pdev->d3cold_delay;
	}

	return max(min_delay, max_delay);
}

/**
 * pci_bridge_wait_for_secondary_bus - Wait for secondary bus to be accessible
 * @dev: PCI bridge
 *
 * Handle necessary delays before access to the devices on the secondary
 * side of the bridge are permitted after D3cold to D0 transition.
 *
 * For PCIe this means the delays in PCIe 5.0 section 6.6.1. For
 * conventional PCI it means Tpvrh + Trhfa specified in PCI 3.0 section
 * 4.3.2.
 */
void pci_bridge_wait_for_secondary_bus(struct pci_dev *dev)
{
	struct pci_dev *child;
	int delay;

	if (pci_dev_is_disconnected(dev))
		return;

	if (!pci_is_bridge(dev) || !dev->bridge_d3)
		return;

	down_read(&pci_bus_sem);

	/*
	 * We only deal with devices that are present currently on the bus.
	 * For any hot-added devices the access delay is handled in pciehp
	 * board_added(). In case of ACPI hotplug the firmware is expected
	 * to configure the devices before OS is notified.
	 */
	if (!dev->subordinate || list_empty(&dev->subordinate->devices)) {
		up_read(&pci_bus_sem);
		return;
	}

	/* Take d3cold_delay requirements into account */
	delay = pci_bus_max_d3cold_delay(dev->subordinate);
	if (!delay) {
		up_read(&pci_bus_sem);
		return;
	}

	child = list_first_entry(&dev->subordinate->devices, struct pci_dev,
				 bus_list);
	up_read(&pci_bus_sem);

	/*
	 * Conventional PCI and PCI-X we need to wait Tpvrh + Trhfa before
	 * accessing the device after reset (that is 1000 ms + 100 ms). In
	 * practice this should not be needed because we don't do power
	 * management for them (see pci_bridge_d3_possible()).
	 */
	if (!pci_is_pcie(dev)) {
		pci_dbg(dev, "waiting %d ms for secondary bus\n", 1000 + delay);
		msleep(1000 + delay);
		return;
	}

	/*
	 * For PCIe downstream and root ports that do not support speeds
	 * greater than 5 GT/s need to wait minimum 100 ms. For higher
	 * speeds (gen3) we need to wait first for the data link layer to
	 * become active.
	 *
	 * However, 100 ms is the minimum and the PCIe spec says the
	 * software must allow at least 1s before it can determine that the
	 * device that did not respond is a broken device. There is
	 * evidence that 100 ms is not always enough, for example certain
	 * Titan Ridge xHCI controller does not always respond to
	 * configuration requests if we only wait for 100 ms (see
	 * https://bugzilla.kernel.org/show_bug.cgi?id=203885).
	 *
	 * Therefore we wait for 100 ms and check for the device presence.
	 * If it is still not present give it an additional 100 ms.
	 */
	if (!pcie_downstream_port(dev))
		return;

	if (pcie_get_speed_cap(dev) <= PCIE_SPEED_5_0GT) {
		pci_dbg(dev, "waiting %d ms for downstream link\n", delay);
		msleep(delay);
	} else {
		pci_dbg(dev, "waiting %d ms for downstream link, after activation\n",
			delay);
		if (!pcie_wait_for_link_delay(dev, true, delay)) {
			/* Did not train, no need to wait any further */
			pci_info(dev, "Data Link Layer Link Active not set in 1000 msec\n");
			return;
		}
	}

	if (!pci_device_is_present(child)) {
		pci_dbg(child, "waiting additional %d ms to become accessible\n", delay);
		msleep(delay);
	}
}

void pci_reset_secondary_bus(struct pci_dev *dev)
{
	u16 ctrl;

	pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &ctrl);
	ctrl |= PCI_BRIDGE_CTL_BUS_RESET;
	pci_write_config_word(dev, PCI_BRIDGE_CONTROL, ctrl);

	/*
	 * PCI spec v3.0 7.6.4.2 requires minimum Trst of 1ms.  Double
	 * this to 2ms to ensure that we meet the minimum requirement.
	 */
	msleep(2);

	ctrl &= ~PCI_BRIDGE_CTL_BUS_RESET;
	pci_write_config_word(dev, PCI_BRIDGE_CONTROL, ctrl);

	/*
	 * Trhfa for conventional PCI is 2^25 clock cycles.
	 * Assuming a minimum 33MHz clock this results in a 1s
	 * delay before we can consider subordinate devices to
	 * be re-initialized.  PCIe has some ways to shorten this,
	 * but we don't make use of them yet.
	 */
	ssleep(1);
}

void __weak pcibios_reset_secondary_bus(struct pci_dev *dev)
{
	pci_reset_secondary_bus(dev);
}

/**
 * pci_bridge_secondary_bus_reset - Reset the secondary bus on a PCI bridge.
 * @dev: Bridge device
 *
 * Use the bridge control register to assert reset on the secondary bus.
 * Devices on the secondary bus are left in power-on state.
 */
int pci_bridge_secondary_bus_reset(struct pci_dev *dev)
{
	pcibios_reset_secondary_bus(dev);

	return pci_dev_wait(dev, "bus reset", PCIE_RESET_READY_POLL_MS);
}
EXPORT_SYMBOL_GPL(pci_bridge_secondary_bus_reset);

static int pci_parent_bus_reset(struct pci_dev *dev, int probe)
{
	struct pci_dev *pdev;

	if (pci_is_root_bus(dev->bus) || dev->subordinate ||
	    !dev->bus->self || dev->dev_flags & PCI_DEV_FLAGS_NO_BUS_RESET)
		return -ENOTTY;

	list_for_each_entry(pdev, &dev->bus->devices, bus_list)
		if (pdev != dev)
			return -ENOTTY;

	if (probe)
		return 0;

	return pci_bridge_secondary_bus_reset(dev->bus->self);
}

static int pci_reset_hotplug_slot(struct hotplug_slot *hotplug, int probe)
{
	int rc = -ENOTTY;

	if (!hotplug || !try_module_get(hotplug->owner))
		return rc;

	if (hotplug->ops->reset_slot)
		rc = hotplug->ops->reset_slot(hotplug, probe);

	module_put(hotplug->owner);

	return rc;
}

static int pci_dev_reset_slot_function(struct pci_dev *dev, int probe)
{
	if (dev->multifunction || dev->subordinate || !dev->slot ||
	    dev->dev_flags & PCI_DEV_FLAGS_NO_BUS_RESET)
		return -ENOTTY;

	return pci_reset_hotplug_slot(dev->slot->hotplug, probe);
}

static int pci_reset_bus_function(struct pci_dev *dev, int probe)
{
	int rc;

	rc = pci_dev_reset_slot_function(dev, probe);
	if (rc != -ENOTTY)
		return rc;
	return pci_parent_bus_reset(dev, probe);
}

static void pci_dev_lock(struct pci_dev *dev)
{
	pci_cfg_access_lock(dev);
	/* block PM suspend, driver probe, etc. */
	device_lock(&dev->dev);
}

/* Return 1 on successful lock, 0 on contention */
int pci_dev_trylock(struct pci_dev *dev)
{
	if (pci_cfg_access_trylock(dev)) {
		if (device_trylock(&dev->dev))
			return 1;
		pci_cfg_access_unlock(dev);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pci_dev_trylock);

void pci_dev_unlock(struct pci_dev *dev)
{
	device_unlock(&dev->dev);
	pci_cfg_access_unlock(dev);
}
EXPORT_SYMBOL_GPL(pci_dev_unlock);

static void pci_dev_save_and_disable(struct pci_dev *dev)
{
	const struct pci_error_handlers *err_handler =
			dev->driver ? dev->driver->err_handler : NULL;

	/*
	 * dev->driver->err_handler->reset_prepare() is protected against
	 * races with ->remove() by the device lock, which must be held by
	 * the caller.
	 */
	if (err_handler && err_handler->reset_prepare)
		err_handler->reset_prepare(dev);

	/*
	 * Wake-up device prior to save.  PM registers default to D0 after
	 * reset and a simple register restore doesn't reliably return
	 * to a non-D0 state anyway.
	 */
	pci_set_power_state(dev, PCI_D0);

	pci_save_state(dev);
	/*
	 * Disable the device by clearing the Command register, except for
	 * INTx-disable which is set.  This not only disables MMIO and I/O port
	 * BARs, but also prevents the device from being Bus Master, preventing
	 * DMA from the device including MSI/MSI-X interrupts.  For PCI 2.3
	 * compliant devices, INTx-disable prevents legacy interrupts.
	 */
	pci_write_config_word(dev, PCI_COMMAND, PCI_COMMAND_INTX_DISABLE);
}

static void pci_dev_restore(struct pci_dev *dev)
{
	const struct pci_error_handlers *err_handler =
			dev->driver ? dev->driver->err_handler : NULL;

	pci_restore_state(dev);

	/*
	 * dev->driver->err_handler->reset_done() is protected against
	 * races with ->remove() by the device lock, which must be held by
	 * the caller.
	 */
	if (err_handler && err_handler->reset_done)
		err_handler->reset_done(dev);
}

/**
 * __pci_reset_function_locked - reset a PCI device function while holding
 * the @dev mutex lock.
 * @dev: PCI device to reset
 *
 * Some devices allow an individual function to be reset without affecting
 * other functions in the same device.  The PCI device must be responsive
 * to PCI config space in order to use this function.
 *
 * The device function is presumed to be unused and the caller is holding
 * the device mutex lock when this function is called.
 *
 * Resetting the device will make the contents of PCI configuration space
 * random, so any caller of this must be prepared to reinitialise the
 * device including MSI, bus mastering, BARs, decoding IO and memory spaces,
 * etc.
 *
 * Returns 0 if the device function was successfully reset or negative if the
 * device doesn't support resetting a single function.
 */
int __pci_reset_function_locked(struct pci_dev *dev)
{
	int rc;

	might_sleep();

	/*
	 * A reset method returns -ENOTTY if it doesn't support this device
	 * and we should try the next method.
	 *
	 * If it returns 0 (success), we're finished.  If it returns any
	 * other error, we're also finished: this indicates that further
	 * reset mechanisms might be broken on the device.
	 */
	rc = pci_dev_specific_reset(dev, 0);
	if (rc != -ENOTTY)
		return rc;
	if (pcie_has_flr(dev)) {
		rc = pcie_flr(dev);
		if (rc != -ENOTTY)
			return rc;
	}
	rc = pci_af_flr(dev, 0);
	if (rc != -ENOTTY)
		return rc;
	rc = pci_pm_reset(dev, 0);
	if (rc != -ENOTTY)
		return rc;
	return pci_reset_bus_function(dev, 0);
}
EXPORT_SYMBOL_GPL(__pci_reset_function_locked);

/**
 * pci_probe_reset_function - check whether the device can be safely reset
 * @dev: PCI device to reset
 *
 * Some devices allow an individual function to be reset without affecting
 * other functions in the same device.  The PCI device must be responsive
 * to PCI config space in order to use this function.
 *
 * Returns 0 if the device function can be reset or negative if the
 * device doesn't support resetting a single function.
 */
int pci_probe_reset_function(struct pci_dev *dev)
{
	int rc;

	might_sleep();

	rc = pci_dev_specific_reset(dev, 1);
	if (rc != -ENOTTY)
		return rc;
	if (pcie_has_flr(dev))
		return 0;
	rc = pci_af_flr(dev, 1);
	if (rc != -ENOTTY)
		return rc;
	rc = pci_pm_reset(dev, 1);
	if (rc != -ENOTTY)
		return rc;

	return pci_reset_bus_function(dev, 1);
}

/**
 * pci_reset_function - quiesce and reset a PCI device function
 * @dev: PCI device to reset
 *
 * Some devices allow an individual function to be reset without affecting
 * other functions in the same device.  The PCI device must be responsive
 * to PCI config space in order to use this function.
 *
 * This function does not just reset the PCI portion of a device, but
 * clears all the state associated with the device.  This function differs
 * from __pci_reset_function_locked() in that it saves and restores device state
 * over the reset and takes the PCI device lock.
 *
 * Returns 0 if the device function was successfully reset or negative if the
 * device doesn't support resetting a single function.
 */
int pci_reset_function(struct pci_dev *dev)
{
	int rc;

	if (!dev->reset_fn)
		return -ENOTTY;

	pci_dev_lock(dev);
	pci_dev_save_and_disable(dev);

	rc = __pci_reset_function_locked(dev);

	pci_dev_restore(dev);
	pci_dev_unlock(dev);

	return rc;
}
EXPORT_SYMBOL_GPL(pci_reset_function);

/**
 * pci_reset_function_locked - quiesce and reset a PCI device function
 * @dev: PCI device to reset
 *
 * Some devices allow an individual function to be reset without affecting
 * other functions in the same device.  The PCI device must be responsive
 * to PCI config space in order to use this function.
 *
 * This function does not just reset the PCI portion of a device, but
 * clears all the state associated with the device.  This function differs
 * from __pci_reset_function_locked() in that it saves and restores device state
 * over the reset.  It also differs from pci_reset_function() in that it
 * requires the PCI device lock to be held.
 *
 * Returns 0 if the device function was successfully reset or negative if the
 * device doesn't support resetting a single function.
 */
int pci_reset_function_locked(struct pci_dev *dev)
{
	int rc;

	if (!dev->reset_fn)
		return -ENOTTY;

	pci_dev_save_and_disable(dev);

	rc = __pci_reset_function_locked(dev);

	pci_dev_restore(dev);

	return rc;
}
EXPORT_SYMBOL_GPL(pci_reset_function_locked);

/**
 * pci_try_reset_function - quiesce and reset a PCI device function
 * @dev: PCI device to reset
 *
 * Same as above, except return -EAGAIN if unable to lock device.
 */
int pci_try_reset_function(struct pci_dev *dev)
{
	int rc;

	if (!dev->reset_fn)
		return -ENOTTY;

	if (!pci_dev_trylock(dev))
		return -EAGAIN;

	pci_dev_save_and_disable(dev);
	rc = __pci_reset_function_locked(dev);
	pci_dev_restore(dev);
	pci_dev_unlock(dev);

	return rc;
}
EXPORT_SYMBOL_GPL(pci_try_reset_function);

/* Do any devices on or below this bus prevent a bus reset? */
static bool pci_bus_resetable(struct pci_bus *bus)
{
	struct pci_dev *dev;


	if (bus->self && (bus->self->dev_flags & PCI_DEV_FLAGS_NO_BUS_RESET))
		return false;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (dev->dev_flags & PCI_DEV_FLAGS_NO_BUS_RESET ||
		    (dev->subordinate && !pci_bus_resetable(dev->subordinate)))
			return false;
	}

	return true;
}

/* Lock devices from the top of the tree down */
static void pci_bus_lock(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		pci_dev_lock(dev);
		if (dev->subordinate)
			pci_bus_lock(dev->subordinate);
	}
}

/* Unlock devices from the bottom of the tree up */
static void pci_bus_unlock(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (dev->subordinate)
			pci_bus_unlock(dev->subordinate);
		pci_dev_unlock(dev);
	}
}

/* Return 1 on successful lock, 0 on contention */
static int pci_bus_trylock(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (!pci_dev_trylock(dev))
			goto unlock;
		if (dev->subordinate) {
			if (!pci_bus_trylock(dev->subordinate)) {
				pci_dev_unlock(dev);
				goto unlock;
			}
		}
	}
	return 1;

unlock:
	list_for_each_entry_continue_reverse(dev, &bus->devices, bus_list) {
		if (dev->subordinate)
			pci_bus_unlock(dev->subordinate);
		pci_dev_unlock(dev);
	}
	return 0;
}

/* Do any devices on or below this slot prevent a bus reset? */
static bool pci_slot_resetable(struct pci_slot *slot)
{
	struct pci_dev *dev;

	if (slot->bus->self &&
	    (slot->bus->self->dev_flags & PCI_DEV_FLAGS_NO_BUS_RESET))
		return false;

	list_for_each_entry(dev, &slot->bus->devices, bus_list) {
		if (!dev->slot || dev->slot != slot)
			continue;
		if (dev->dev_flags & PCI_DEV_FLAGS_NO_BUS_RESET ||
		    (dev->subordinate && !pci_bus_resetable(dev->subordinate)))
			return false;
	}

	return true;
}

/* Lock devices from the top of the tree down */
static void pci_slot_lock(struct pci_slot *slot)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &slot->bus->devices, bus_list) {
		if (!dev->slot || dev->slot != slot)
			continue;
		pci_dev_lock(dev);
		if (dev->subordinate)
			pci_bus_lock(dev->subordinate);
	}
}

/* Unlock devices from the bottom of the tree up */
static void pci_slot_unlock(struct pci_slot *slot)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &slot->bus->devices, bus_list) {
		if (!dev->slot || dev->slot != slot)
			continue;
		if (dev->subordinate)
			pci_bus_unlock(dev->subordinate);
		pci_dev_unlock(dev);
	}
}

/* Return 1 on successful lock, 0 on contention */
static int pci_slot_trylock(struct pci_slot *slot)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &slot->bus->devices, bus_list) {
		if (!dev->slot || dev->slot != slot)
			continue;
		if (!pci_dev_trylock(dev))
			goto unlock;
		if (dev->subordinate) {
			if (!pci_bus_trylock(dev->subordinate)) {
				pci_dev_unlock(dev);
				goto unlock;
			}
		}
	}
	return 1;

unlock:
	list_for_each_entry_continue_reverse(dev,
					     &slot->bus->devices, bus_list) {
		if (!dev->slot || dev->slot != slot)
			continue;
		if (dev->subordinate)
			pci_bus_unlock(dev->subordinate);
		pci_dev_unlock(dev);
	}
	return 0;
}

/*
 * Save and disable devices from the top of the tree down while holding
 * the @dev mutex lock for the entire tree.
 */
static void pci_bus_save_and_disable_locked(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		pci_dev_save_and_disable(dev);
		if (dev->subordinate)
			pci_bus_save_and_disable_locked(dev->subordinate);
	}
}

/*
 * Restore devices from top of the tree down while holding @dev mutex lock
 * for the entire tree.  Parent bridges need to be restored before we can
 * get to subordinate devices.
 */
static void pci_bus_restore_locked(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		pci_dev_restore(dev);
		if (dev->subordinate)
			pci_bus_restore_locked(dev->subordinate);
	}
}

/*
 * Save and disable devices from the top of the tree down while holding
 * the @dev mutex lock for the entire tree.
 */
static void pci_slot_save_and_disable_locked(struct pci_slot *slot)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &slot->bus->devices, bus_list) {
		if (!dev->slot || dev->slot != slot)
			continue;
		pci_dev_save_and_disable(dev);
		if (dev->subordinate)
			pci_bus_save_and_disable_locked(dev->subordinate);
	}
}

/*
 * Restore devices from top of the tree down while holding @dev mutex lock
 * for the entire tree.  Parent bridges need to be restored before we can
 * get to subordinate devices.
 */
static void pci_slot_restore_locked(struct pci_slot *slot)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &slot->bus->devices, bus_list) {
		if (!dev->slot || dev->slot != slot)
			continue;
		pci_dev_restore(dev);
		if (dev->subordinate)
			pci_bus_restore_locked(dev->subordinate);
	}
}

static int pci_slot_reset(struct pci_slot *slot, int probe)
{
	int rc;

	if (!slot || !pci_slot_resetable(slot))
		return -ENOTTY;

	if (!probe)
		pci_slot_lock(slot);

	might_sleep();

	rc = pci_reset_hotplug_slot(slot->hotplug, probe);

	if (!probe)
		pci_slot_unlock(slot);

	return rc;
}

/**
 * pci_probe_reset_slot - probe whether a PCI slot can be reset
 * @slot: PCI slot to probe
 *
 * Return 0 if slot can be reset, negative if a slot reset is not supported.
 */
int pci_probe_reset_slot(struct pci_slot *slot)
{
	return pci_slot_reset(slot, 1);
}
EXPORT_SYMBOL_GPL(pci_probe_reset_slot);

/**
 * __pci_reset_slot - Try to reset a PCI slot
 * @slot: PCI slot to reset
 *
 * A PCI bus may host multiple slots, each slot may support a reset mechanism
 * independent of other slots.  For instance, some slots may support slot power
 * control.  In the case of a 1:1 bus to slot architecture, this function may
 * wrap the bus reset to avoid spurious slot related events such as hotplug.
 * Generally a slot reset should be attempted before a bus reset.  All of the
 * function of the slot and any subordinate buses behind the slot are reset
 * through this function.  PCI config space of all devices in the slot and
 * behind the slot is saved before and restored after reset.
 *
 * Same as above except return -EAGAIN if the slot cannot be locked
 */
static int __pci_reset_slot(struct pci_slot *slot)
{
	int rc;

	rc = pci_slot_reset(slot, 1);
	if (rc)
		return rc;

	if (pci_slot_trylock(slot)) {
		pci_slot_save_and_disable_locked(slot);
		might_sleep();
		rc = pci_reset_hotplug_slot(slot->hotplug, 0);
		pci_slot_restore_locked(slot);
		pci_slot_unlock(slot);
	} else
		rc = -EAGAIN;

	return rc;
}

static int pci_bus_reset(struct pci_bus *bus, int probe)
{
	int ret;

	if (!bus->self || !pci_bus_resetable(bus))
		return -ENOTTY;

	if (probe)
		return 0;

	pci_bus_lock(bus);

	might_sleep();

	ret = pci_bridge_secondary_bus_reset(bus->self);

	pci_bus_unlock(bus);

	return ret;
}

/**
 * pci_bus_error_reset - reset the bridge's subordinate bus
 * @bridge: The parent device that connects to the bus to reset
 *
 * This function will first try to reset the slots on this bus if the method is
 * available. If slot reset fails or is not available, this will fall back to a
 * secondary bus reset.
 */
int pci_bus_error_reset(struct pci_dev *bridge)
{
	struct pci_bus *bus = bridge->subordinate;
	struct pci_slot *slot;

	if (!bus)
		return -ENOTTY;

	mutex_lock(&pci_slot_mutex);
	if (list_empty(&bus->slots))
		goto bus_reset;

	list_for_each_entry(slot, &bus->slots, list)
		if (pci_probe_reset_slot(slot))
			goto bus_reset;

	list_for_each_entry(slot, &bus->slots, list)
		if (pci_slot_reset(slot, 0))
			goto bus_reset;

	mutex_unlock(&pci_slot_mutex);
	return 0;
bus_reset:
	mutex_unlock(&pci_slot_mutex);
	return pci_bus_reset(bridge->subordinate, 0);
}

/**
 * pci_probe_reset_bus - probe whether a PCI bus can be reset
 * @bus: PCI bus to probe
 *
 * Return 0 if bus can be reset, negative if a bus reset is not supported.
 */
int pci_probe_reset_bus(struct pci_bus *bus)
{
	return pci_bus_reset(bus, 1);
}
EXPORT_SYMBOL_GPL(pci_probe_reset_bus);

/**
 * __pci_reset_bus - Try to reset a PCI bus
 * @bus: top level PCI bus to reset
 *
 * Same as above except return -EAGAIN if the bus cannot be locked
 */
static int __pci_reset_bus(struct pci_bus *bus)
{
	int rc;

	rc = pci_bus_reset(bus, 1);
	if (rc)
		return rc;

	if (pci_bus_trylock(bus)) {
		pci_bus_save_and_disable_locked(bus);
		might_sleep();
		rc = pci_bridge_secondary_bus_reset(bus->self);
		pci_bus_restore_locked(bus);
		pci_bus_unlock(bus);
	} else
		rc = -EAGAIN;

	return rc;
}

/**
 * pci_reset_bus - Try to reset a PCI bus
 * @pdev: top level PCI device to reset via slot/bus
 *
 * Same as above except return -EAGAIN if the bus cannot be locked
 */
int pci_reset_bus(struct pci_dev *pdev)
{
	return (!pci_probe_reset_slot(pdev->slot)) ?
	    __pci_reset_slot(pdev->slot) : __pci_reset_bus(pdev->bus);
}
EXPORT_SYMBOL_GPL(pci_reset_bus);

/**
 * pcix_get_max_mmrbc - get PCI-X maximum designed memory read byte count
 * @dev: PCI device to query
 *
 * Returns mmrbc: maximum designed memory read count in bytes or
 * appropriate error value.
 */
int pcix_get_max_mmrbc(struct pci_dev *dev)
{
	int cap;
	u32 stat;

	cap = pci_find_capability(dev, PCI_CAP_ID_PCIX);
	if (!cap)
		return -EINVAL;

	if (pci_read_config_dword(dev, cap + PCI_X_STATUS, &stat))
		return -EINVAL;

	return 512 << ((stat & PCI_X_STATUS_MAX_READ) >> 21);
}
EXPORT_SYMBOL(pcix_get_max_mmrbc);

/**
 * pcix_get_mmrbc - get PCI-X maximum memory read byte count
 * @dev: PCI device to query
 *
 * Returns mmrbc: maximum memory read count in bytes or appropriate error
 * value.
 */
int pcix_get_mmrbc(struct pci_dev *dev)
{
	int cap;
	u16 cmd;

	cap = pci_find_capability(dev, PCI_CAP_ID_PCIX);
	if (!cap)
		return -EINVAL;

	if (pci_read_config_word(dev, cap + PCI_X_CMD, &cmd))
		return -EINVAL;

	return 512 << ((cmd & PCI_X_CMD_MAX_READ) >> 2);
}
EXPORT_SYMBOL(pcix_get_mmrbc);

/**
 * pcix_set_mmrbc - set PCI-X maximum memory read byte count
 * @dev: PCI device to query
 * @mmrbc: maximum memory read count in bytes
 *    valid values are 512, 1024, 2048, 4096
 *
 * If possible sets maximum memory read byte count, some bridges have errata
 * that prevent this.
 */
int pcix_set_mmrbc(struct pci_dev *dev, int mmrbc)
{
	int cap;
	u32 stat, v, o;
	u16 cmd;

	if (mmrbc < 512 || mmrbc > 4096 || !is_power_of_2(mmrbc))
		return -EINVAL;

	v = ffs(mmrbc) - 10;

	cap = pci_find_capability(dev, PCI_CAP_ID_PCIX);
	if (!cap)
		return -EINVAL;

	if (pci_read_config_dword(dev, cap + PCI_X_STATUS, &stat))
		return -EINVAL;

	if (v > (stat & PCI_X_STATUS_MAX_READ) >> 21)
		return -E2BIG;

	if (pci_read_config_word(dev, cap + PCI_X_CMD, &cmd))
		return -EINVAL;

	o = (cmd & PCI_X_CMD_MAX_READ) >> 2;
	if (o != v) {
		if (v > o && (dev->bus->bus_flags & PCI_BUS_FLAGS_NO_MMRBC))
			return -EIO;

		cmd &= ~PCI_X_CMD_MAX_READ;
		cmd |= v << 2;
		if (pci_write_config_word(dev, cap + PCI_X_CMD, cmd))
			return -EIO;
	}
	return 0;
}
EXPORT_SYMBOL(pcix_set_mmrbc);

/**
 * pcie_get_readrq - get PCI Express read request size
 * @dev: PCI device to query
 *
 * Returns maximum memory read request in bytes or appropriate error value.
 */
int pcie_get_readrq(struct pci_dev *dev)
{
	u16 ctl;

	pcie_capability_read_word(dev, PCI_EXP_DEVCTL, &ctl);

	return 128 << ((ctl & PCI_EXP_DEVCTL_READRQ) >> 12);
}
EXPORT_SYMBOL(pcie_get_readrq);

/**
 * pcie_set_readrq - set PCI Express maximum memory read request
 * @dev: PCI device to query
 * @rq: maximum memory read count in bytes
 *    valid values are 128, 256, 512, 1024, 2048, 4096
 *
 * If possible sets maximum memory read request in bytes
 */
int pcie_set_readrq(struct pci_dev *dev, int rq)
{
	u16 v;
	int ret;

	if (rq < 128 || rq > 4096 || !is_power_of_2(rq))
		return -EINVAL;

	/*
	 * If using the "performance" PCIe config, we clamp the read rq
	 * size to the max packet size to keep the host bridge from
	 * generating requests larger than we can cope with.
	 */
	if (pcie_bus_config == PCIE_BUS_PERFORMANCE) {
		int mps = pcie_get_mps(dev);

		if (mps < rq)
			rq = mps;
	}

	v = (ffs(rq) - 8) << 12;

	ret = pcie_capability_clear_and_set_word(dev, PCI_EXP_DEVCTL,
						  PCI_EXP_DEVCTL_READRQ, v);

	return pcibios_err_to_errno(ret);
}
EXPORT_SYMBOL(pcie_set_readrq);

/**
 * pcie_get_mps - get PCI Express maximum payload size
 * @dev: PCI device to query
 *
 * Returns maximum payload size in bytes
 */
int pcie_get_mps(struct pci_dev *dev)
{
	u16 ctl;

	pcie_capability_read_word(dev, PCI_EXP_DEVCTL, &ctl);

	return 128 << ((ctl & PCI_EXP_DEVCTL_PAYLOAD) >> 5);
}
EXPORT_SYMBOL(pcie_get_mps);

/**
 * pcie_set_mps - set PCI Express maximum payload size
 * @dev: PCI device to query
 * @mps: maximum payload size in bytes
 *    valid values are 128, 256, 512, 1024, 2048, 4096
 *
 * If possible sets maximum payload size
 */
int pcie_set_mps(struct pci_dev *dev, int mps)
{
	u16 v;
	int ret;

	if (mps < 128 || mps > 4096 || !is_power_of_2(mps))
		return -EINVAL;

	v = ffs(mps) - 8;
	if (v > dev->pcie_mpss)
		return -EINVAL;
	v <<= 5;

	ret = pcie_capability_clear_and_set_word(dev, PCI_EXP_DEVCTL,
						  PCI_EXP_DEVCTL_PAYLOAD, v);

	return pcibios_err_to_errno(ret);
}
EXPORT_SYMBOL(pcie_set_mps);

/**
 * pcie_bandwidth_available - determine minimum link settings of a PCIe
 *			      device and its bandwidth limitation
 * @dev: PCI device to query
 * @limiting_dev: storage for device causing the bandwidth limitation
 * @speed: storage for speed of limiting device
 * @width: storage for width of limiting device
 *
 * Walk up the PCI device chain and find the point where the minimum
 * bandwidth is available.  Return the bandwidth available there and (if
 * limiting_dev, speed, and width pointers are supplied) information about
 * that point.  The bandwidth returned is in Mb/s, i.e., megabits/second of
 * raw bandwidth.
 */
u32 pcie_bandwidth_available(struct pci_dev *dev, struct pci_dev **limiting_dev,
			     enum pci_bus_speed *speed,
			     enum pcie_link_width *width)
{
	u16 lnksta;
	enum pci_bus_speed next_speed;
	enum pcie_link_width next_width;
	u32 bw, next_bw;

	if (speed)
		*speed = PCI_SPEED_UNKNOWN;
	if (width)
		*width = PCIE_LNK_WIDTH_UNKNOWN;

	bw = 0;

	while (dev) {
		pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &lnksta);

		next_speed = pcie_link_speed[lnksta & PCI_EXP_LNKSTA_CLS];
		next_width = (lnksta & PCI_EXP_LNKSTA_NLW) >>
			PCI_EXP_LNKSTA_NLW_SHIFT;

		next_bw = next_width * PCIE_SPEED2MBS_ENC(next_speed);

		/* Check if current device limits the total bandwidth */
		if (!bw || next_bw <= bw) {
			bw = next_bw;

			if (limiting_dev)
				*limiting_dev = dev;
			if (speed)
				*speed = next_speed;
			if (width)
				*width = next_width;
		}

		dev = pci_upstream_bridge(dev);
	}

	return bw;
}
EXPORT_SYMBOL(pcie_bandwidth_available);

/**
 * pcie_get_speed_cap - query for the PCI device's link speed capability
 * @dev: PCI device to query
 *
 * Query the PCI device speed capability.  Return the maximum link speed
 * supported by the device.
 */
enum pci_bus_speed pcie_get_speed_cap(struct pci_dev *dev)
{
	u32 lnkcap2, lnkcap;

	/*
	 * Link Capabilities 2 was added in PCIe r3.0, sec 7.8.18.  The
	 * implementation note there recommends using the Supported Link
	 * Speeds Vector in Link Capabilities 2 when supported.
	 *
	 * Without Link Capabilities 2, i.e., prior to PCIe r3.0, software
	 * should use the Supported Link Speeds field in Link Capabilities,
	 * where only 2.5 GT/s and 5.0 GT/s speeds were defined.
	 */
	pcie_capability_read_dword(dev, PCI_EXP_LNKCAP2, &lnkcap2);

	/* PCIe r3.0-compliant */
	if (lnkcap2)
		return PCIE_LNKCAP2_SLS2SPEED(lnkcap2);

	pcie_capability_read_dword(dev, PCI_EXP_LNKCAP, &lnkcap);
	if ((lnkcap & PCI_EXP_LNKCAP_SLS) == PCI_EXP_LNKCAP_SLS_5_0GB)
		return PCIE_SPEED_5_0GT;
	else if ((lnkcap & PCI_EXP_LNKCAP_SLS) == PCI_EXP_LNKCAP_SLS_2_5GB)
		return PCIE_SPEED_2_5GT;

	return PCI_SPEED_UNKNOWN;
}
EXPORT_SYMBOL(pcie_get_speed_cap);

/**
 * pcie_get_width_cap - query for the PCI device's link width capability
 * @dev: PCI device to query
 *
 * Query the PCI device width capability.  Return the maximum link width
 * supported by the device.
 */
enum pcie_link_width pcie_get_width_cap(struct pci_dev *dev)
{
	u32 lnkcap;

	pcie_capability_read_dword(dev, PCI_EXP_LNKCAP, &lnkcap);
	if (lnkcap)
		return (lnkcap & PCI_EXP_LNKCAP_MLW) >> 4;

	return PCIE_LNK_WIDTH_UNKNOWN;
}
EXPORT_SYMBOL(pcie_get_width_cap);

/**
 * pcie_bandwidth_capable - calculate a PCI device's link bandwidth capability
 * @dev: PCI device
 * @speed: storage for link speed
 * @width: storage for link width
 *
 * Calculate a PCI device's link bandwidth by querying for its link speed
 * and width, multiplying them, and applying encoding overhead.  The result
 * is in Mb/s, i.e., megabits/second of raw bandwidth.
 */
u32 pcie_bandwidth_capable(struct pci_dev *dev, enum pci_bus_speed *speed,
			   enum pcie_link_width *width)
{
	*speed = pcie_get_speed_cap(dev);
	*width = pcie_get_width_cap(dev);

	if (*speed == PCI_SPEED_UNKNOWN || *width == PCIE_LNK_WIDTH_UNKNOWN)
		return 0;

	return *width * PCIE_SPEED2MBS_ENC(*speed);
}

/**
 * __pcie_print_link_status - Report the PCI device's link speed and width
 * @dev: PCI device to query
 * @verbose: Print info even when enough bandwidth is available
 *
 * If the available bandwidth at the device is less than the device is
 * capable of, report the device's maximum possible bandwidth and the
 * upstream link that limits its performance.  If @verbose, always print
 * the available bandwidth, even if the device isn't constrained.
 */
void __pcie_print_link_status(struct pci_dev *dev, bool verbose)
{
	enum pcie_link_width width, width_cap;
	enum pci_bus_speed speed, speed_cap;
	struct pci_dev *limiting_dev = NULL;
	u32 bw_avail, bw_cap;

	bw_cap = pcie_bandwidth_capable(dev, &speed_cap, &width_cap);
	bw_avail = pcie_bandwidth_available(dev, &limiting_dev, &speed, &width);

	if (bw_avail >= bw_cap && verbose)
		pci_info(dev, "%u.%03u Gb/s available PCIe bandwidth (%s x%d link)\n",
			 bw_cap / 1000, bw_cap % 1000,
			 pci_speed_string(speed_cap), width_cap);
	else if (bw_avail < bw_cap)
		pci_info(dev, "%u.%03u Gb/s available PCIe bandwidth, limited by %s x%d link at %s (capable of %u.%03u Gb/s with %s x%d link)\n",
			 bw_avail / 1000, bw_avail % 1000,
			 pci_speed_string(speed), width,
			 limiting_dev ? pci_name(limiting_dev) : "<unknown>",
			 bw_cap / 1000, bw_cap % 1000,
			 pci_speed_string(speed_cap), width_cap);
}

/**
 * pcie_print_link_status - Report the PCI device's link speed and width
 * @dev: PCI device to query
 *
 * Report the available bandwidth at the device.
 */
void pcie_print_link_status(struct pci_dev *dev)
{
	__pcie_print_link_status(dev, true);
}
EXPORT_SYMBOL(pcie_print_link_status);

/**
 * pci_select_bars - Make BAR mask from the type of resource
 * @dev: the PCI device for which BAR mask is made
 * @flags: resource type mask to be selected
 *
 * This helper routine makes bar mask from the type of resource.
 */
int pci_select_bars(struct pci_dev *dev, unsigned long flags)
{
	int i, bars = 0;
	for (i = 0; i < PCI_NUM_RESOURCES; i++)
		if (pci_resource_flags(dev, i) & flags)
			bars |= (1 << i);
	return bars;
}
EXPORT_SYMBOL(pci_select_bars);

/* Some architectures require additional programming to enable VGA */
static arch_set_vga_state_t arch_set_vga_state;

void __init pci_register_set_vga_state(arch_set_vga_state_t func)
{
	arch_set_vga_state = func;	/* NULL disables */
}

static int pci_set_vga_state_arch(struct pci_dev *dev, bool decode,
				  unsigned int command_bits, u32 flags)
{
	if (arch_set_vga_state)
		return arch_set_vga_state(dev, decode, command_bits,
						flags);
	return 0;
}

/**
 * pci_set_vga_state - set VGA decode state on device and parents if requested
 * @dev: the PCI device
 * @decode: true = enable decoding, false = disable decoding
 * @command_bits: PCI_COMMAND_IO and/or PCI_COMMAND_MEMORY
 * @flags: traverse ancestors and change bridges
 * CHANGE_BRIDGE_ONLY / CHANGE_BRIDGE
 */
int pci_set_vga_state(struct pci_dev *dev, bool decode,
		      unsigned int command_bits, u32 flags)
{
	struct pci_bus *bus;
	struct pci_dev *bridge;
	u16 cmd;
	int rc;

	WARN_ON((flags & PCI_VGA_STATE_CHANGE_DECODES) && (command_bits & ~(PCI_COMMAND_IO|PCI_COMMAND_MEMORY)));

	/* ARCH specific VGA enables */
	rc = pci_set_vga_state_arch(dev, decode, command_bits, flags);
	if (rc)
		return rc;

	if (flags & PCI_VGA_STATE_CHANGE_DECODES) {
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		if (decode)
			cmd |= command_bits;
		else
			cmd &= ~command_bits;
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}

	if (!(flags & PCI_VGA_STATE_CHANGE_BRIDGE))
		return 0;

	bus = dev->bus;
	while (bus) {
		bridge = bus->self;
		if (bridge) {
			pci_read_config_word(bridge, PCI_BRIDGE_CONTROL,
					     &cmd);
			if (decode)
				cmd |= PCI_BRIDGE_CTL_VGA;
			else
				cmd &= ~PCI_BRIDGE_CTL_VGA;
			pci_write_config_word(bridge, PCI_BRIDGE_CONTROL,
					      cmd);
		}
		bus = bus->parent;
	}
	return 0;
}

#ifdef CONFIG_ACPI
bool pci_pr3_present(struct pci_dev *pdev)
{
	struct acpi_device *adev;

	if (acpi_disabled)
		return false;

	adev = ACPI_COMPANION(&pdev->dev);
	if (!adev)
		return false;

	return adev->power.flags.power_resources &&
		acpi_has_method(adev->handle, "_PR3");
}
EXPORT_SYMBOL_GPL(pci_pr3_present);
#endif

/**
 * pci_add_dma_alias - Add a DMA devfn alias for a device
 * @dev: the PCI device for which alias is added
 * @devfn_from: alias slot and function
 * @nr_devfns: number of subsequent devfns to alias
 *
 * This helper encodes an 8-bit devfn as a bit number in dma_alias_mask
 * which is used to program permissible bus-devfn source addresses for DMA
 * requests in an IOMMU.  These aliases factor into IOMMU group creation
 * and are useful for devices generating DMA requests beyond or different
 * from their logical bus-devfn.  Examples include device quirks where the
 * device simply uses the wrong devfn, as well as non-transparent bridges
 * where the alias may be a proxy for devices in another domain.
 *
 * IOMMU group creation is performed during device discovery or addition,
 * prior to any potential DMA mapping and therefore prior to driver probing
 * (especially for userspace assigned devices where IOMMU group definition
 * cannot be left as a userspace activity).  DMA aliases should therefore
 * be configured via quirks, such as the PCI fixup header quirk.
 */
void pci_add_dma_alias(struct pci_dev *dev, u8 devfn_from, unsigned nr_devfns)
{
	int devfn_to;

	nr_devfns = min(nr_devfns, (unsigned) MAX_NR_DEVFNS - devfn_from);
	devfn_to = devfn_from + nr_devfns - 1;

	if (!dev->dma_alias_mask)
		dev->dma_alias_mask = bitmap_zalloc(MAX_NR_DEVFNS, GFP_KERNEL);
	if (!dev->dma_alias_mask) {
		pci_warn(dev, "Unable to allocate DMA alias mask\n");
		return;
	}

	bitmap_set(dev->dma_alias_mask, devfn_from, nr_devfns);

	if (nr_devfns == 1)
		pci_info(dev, "Enabling fixed DMA alias to %02x.%d\n",
				PCI_SLOT(devfn_from), PCI_FUNC(devfn_from));
	else if (nr_devfns > 1)
		pci_info(dev, "Enabling fixed DMA alias for devfn range from %02x.%d to %02x.%d\n",
				PCI_SLOT(devfn_from), PCI_FUNC(devfn_from),
				PCI_SLOT(devfn_to), PCI_FUNC(devfn_to));
}

bool pci_devs_are_dma_aliases(struct pci_dev *dev1, struct pci_dev *dev2)
{
	return (dev1->dma_alias_mask &&
		test_bit(dev2->devfn, dev1->dma_alias_mask)) ||
	       (dev2->dma_alias_mask &&
		test_bit(dev1->devfn, dev2->dma_alias_mask)) ||
	       pci_real_dma_dev(dev1) == dev2 ||
	       pci_real_dma_dev(dev2) == dev1;
}

bool pci_device_is_present(struct pci_dev *pdev)
{
	u32 v;

	if (pci_dev_is_disconnected(pdev))
		return false;
	return pci_bus_read_dev_vendor_id(pdev->bus, pdev->devfn, &v, 0);
}
EXPORT_SYMBOL_GPL(pci_device_is_present);

void pci_ignore_hotplug(struct pci_dev *dev)
{
	struct pci_dev *bridge = dev->bus->self;

	dev->ignore_hotplug = 1;
	/* Propagate the "ignore hotplug" setting to the parent bridge. */
	if (bridge)
		bridge->ignore_hotplug = 1;
}
EXPORT_SYMBOL_GPL(pci_ignore_hotplug);

/**
 * pci_real_dma_dev - Get PCI DMA device for PCI device
 * @dev: the PCI device that may have a PCI DMA alias
 *
 * Permits the platform to provide architecture-specific functionality to
 * devices needing to alias DMA to another PCI device on another PCI bus. If
 * the PCI device is on the same bus, it is recommended to use
 * pci_add_dma_alias(). This is the default implementation. Architecture
 * implementations can override this.
 */
struct pci_dev __weak *pci_real_dma_dev(struct pci_dev *dev)
{
	return dev;
}

resource_size_t __weak pcibios_default_alignment(void)
{
	return 0;
}

/*
 * Arches that don't want to expose struct resource to userland as-is in
 * sysfs and /proc can implement their own pci_resource_to_user().
 */
void __weak pci_resource_to_user(const struct pci_dev *dev, int bar,
				 const struct resource *rsrc,
				 resource_size_t *start, resource_size_t *end)
{
	*start = rsrc->start;
	*end = rsrc->end;
}

static char *resource_alignment_param;
static DEFINE_SPINLOCK(resource_alignment_lock);

/**
 * pci_specified_resource_alignment - get resource alignment specified by user.
 * @dev: the PCI device to get
 * @resize: whether or not to change resources' size when reassigning alignment
 *
 * RETURNS: Resource alignment if it is specified.
 *          Zero if it is not specified.
 */
static resource_size_t pci_specified_resource_alignment(struct pci_dev *dev,
							bool *resize)
{
	int align_order, count;
	resource_size_t align = pcibios_default_alignment();
	const char *p;
	int ret;

	spin_lock(&resource_alignment_lock);
	p = resource_alignment_param;
	if (!p || !*p)
		goto out;
	if (pci_has_flag(PCI_PROBE_ONLY)) {
		align = 0;
		pr_info_once("PCI: Ignoring requested alignments (PCI_PROBE_ONLY)\n");
		goto out;
	}

	while (*p) {
		count = 0;
		if (sscanf(p, "%d%n", &align_order, &count) == 1 &&
		    p[count] == '@') {
			p += count + 1;
			if (align_order > 63) {
				pr_err("PCI: Invalid requested alignment (order %d)\n",
				       align_order);
				align_order = PAGE_SHIFT;
			}
		} else {
			align_order = PAGE_SHIFT;
		}

		ret = pci_dev_str_match(dev, p, &p);
		if (ret == 1) {
			*resize = true;
			align = 1ULL << align_order;
			break;
		} else if (ret < 0) {
			pr_err("PCI: Can't parse resource_alignment parameter: %s\n",
			       p);
			break;
		}

		if (*p != ';' && *p != ',') {
			/* End of param or invalid format */
			break;
		}
		p++;
	}
out:
	spin_unlock(&resource_alignment_lock);
	return align;
}

static void pci_request_resource_alignment(struct pci_dev *dev, int bar,
					   resource_size_t align, bool resize)
{
	struct resource *r = &dev->resource[bar];
	resource_size_t size;

	if (!(r->flags & IORESOURCE_MEM))
		return;

	if (r->flags & IORESOURCE_PCI_FIXED) {
		pci_info(dev, "BAR%d %pR: ignoring requested alignment %#llx\n",
			 bar, r, (unsigned long long)align);
		return;
	}

	size = resource_size(r);
	if (size >= align)
		return;

	/*
	 * Increase the alignment of the resource.  There are two ways we
	 * can do this:
	 *
	 * 1) Increase the size of the resource.  BARs are aligned on their
	 *    size, so when we reallocate space for this resource, we'll
	 *    allocate it with the larger alignment.  This also prevents
	 *    assignment of any other BARs inside the alignment region, so
	 *    if we're requesting page alignment, this means no other BARs
	 *    will share the page.
	 *
	 *    The disadvantage is that this makes the resource larger than
	 *    the hardware BAR, which may break drivers that compute things
	 *    based on the resource size, e.g., to find registers at a
	 *    fixed offset before the end of the BAR.
	 *
	 * 2) Retain the resource size, but use IORESOURCE_STARTALIGN and
	 *    set r->start to the desired alignment.  By itself this
	 *    doesn't prevent other BARs being put inside the alignment
	 *    region, but if we realign *every* resource of every device in
	 *    the system, none of them will share an alignment region.
	 *
	 * When the user has requested alignment for only some devices via
	 * the "pci=resource_alignment" argument, "resize" is true and we
	 * use the first method.  Otherwise we assume we're aligning all
	 * devices and we use the second.
	 */

	pci_info(dev, "BAR%d %pR: requesting alignment to %#llx\n",
		 bar, r, (unsigned long long)align);

	if (resize) {
		r->start = 0;
		r->end = align - 1;
	} else {
		r->flags &= ~IORESOURCE_SIZEALIGN;
		r->flags |= IORESOURCE_STARTALIGN;
		r->start = align;
		r->end = r->start + size - 1;
	}
	r->flags |= IORESOURCE_UNSET;
}

/*
 * This function disables memory decoding and releases memory resources
 * of the device specified by kernel's boot parameter 'pci=resource_alignment='.
 * It also rounds up size to specified alignment.
 * Later on, the kernel will assign page-aligned memory resource back
 * to the device.
 */
void pci_reassigndev_resource_alignment(struct pci_dev *dev)
{
	int i;
	struct resource *r;
	resource_size_t align;
	u16 command;
	bool resize = false;

	/*
	 * VF BARs are read-only zero according to SR-IOV spec r1.1, sec
	 * 3.4.1.11.  Their resources are allocated from the space
	 * described by the VF BARx register in the PF's SR-IOV capability.
	 * We can't influence their alignment here.
	 */
	if (dev->is_virtfn)
		return;

	/* check if specified PCI is target device to reassign */
	align = pci_specified_resource_alignment(dev, &resize);
	if (!align)
		return;

	if (dev->hdr_type == PCI_HEADER_TYPE_NORMAL &&
	    (dev->class >> 8) == PCI_CLASS_BRIDGE_HOST) {
		pci_warn(dev, "Can't reassign resources to host bridge\n");
		return;
	}

	pci_read_config_word(dev, PCI_COMMAND, &command);
	command &= ~PCI_COMMAND_MEMORY;
	pci_write_config_word(dev, PCI_COMMAND, command);

	for (i = 0; i <= PCI_ROM_RESOURCE; i++)
		pci_request_resource_alignment(dev, i, align, resize);

	/*
	 * Need to disable bridge's resource window,
	 * to enable the kernel to reassign new resource
	 * window later on.
	 */
	if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
		for (i = PCI_BRIDGE_RESOURCES; i < PCI_NUM_RESOURCES; i++) {
			r = &dev->resource[i];
			if (!(r->flags & IORESOURCE_MEM))
				continue;
			r->flags |= IORESOURCE_UNSET;
			r->end = resource_size(r) - 1;
			r->start = 0;
		}
		pci_disable_bridge_window(dev);
	}
}

static ssize_t resource_alignment_show(struct bus_type *bus, char *buf)
{
	size_t count = 0;

	spin_lock(&resource_alignment_lock);
	if (resource_alignment_param)
		count = sysfs_emit(buf, "%s\n", resource_alignment_param);
	spin_unlock(&resource_alignment_lock);

	return count;
}

static ssize_t resource_alignment_store(struct bus_type *bus,
					const char *buf, size_t count)
{
	char *param, *old, *end;

	if (count >= (PAGE_SIZE - 1))
		return -EINVAL;

	param = kstrndup(buf, count, GFP_KERNEL);
	if (!param)
		return -ENOMEM;

	end = strchr(param, '\n');
	if (end)
		*end = '\0';

	spin_lock(&resource_alignment_lock);
	old = resource_alignment_param;
	if (strlen(param)) {
		resource_alignment_param = param;
	} else {
		kfree(param);
		resource_alignment_param = NULL;
	}
	spin_unlock(&resource_alignment_lock);

	kfree(old);

	return count;
}

static BUS_ATTR_RW(resource_alignment);

static int __init pci_resource_alignment_sysfs_init(void)
{
	return bus_create_file(&pci_bus_type,
					&bus_attr_resource_alignment);
}
late_initcall(pci_resource_alignment_sysfs_init);

static void pci_no_domains(void)
{
#ifdef CONFIG_PCI_DOMAINS
	pci_domains_supported = 0;
#endif
}

#ifdef CONFIG_PCI_DOMAINS_GENERIC
static atomic_t __domain_nr = ATOMIC_INIT(-1);

static int pci_get_new_domain_nr(void)
{
	return atomic_inc_return(&__domain_nr);
}

static int of_pci_bus_find_domain_nr(struct device *parent)
{
	static int use_dt_domains = -1;
	int domain = -1;

	if (parent)
		domain = of_get_pci_domain_nr(parent->of_node);

	/*
	 * Check DT domain and use_dt_domains values.
	 *
	 * If DT domain property is valid (domain >= 0) and
	 * use_dt_domains != 0, the DT assignment is valid since this means
	 * we have not previously allocated a domain number by using
	 * pci_get_new_domain_nr(); we should also update use_dt_domains to
	 * 1, to indicate that we have just assigned a domain number from
	 * DT.
	 *
	 * If DT domain property value is not valid (ie domain < 0), and we
	 * have not previously assigned a domain number from DT
	 * (use_dt_domains != 1) we should assign a domain number by
	 * using the:
	 *
	 * pci_get_new_domain_nr()
	 *
	 * API and update the use_dt_domains value to keep track of method we
	 * are using to assign domain numbers (use_dt_domains = 0).
	 *
	 * All other combinations imply we have a platform that is trying
	 * to mix domain numbers obtained from DT and pci_get_new_domain_nr(),
	 * which is a recipe for domain mishandling and it is prevented by
	 * invalidating the domain value (domain = -1) and printing a
	 * corresponding error.
	 */
	if (domain >= 0 && use_dt_domains) {
		use_dt_domains = 1;
	} else if (domain < 0 && use_dt_domains != 1) {
		use_dt_domains = 0;
		domain = pci_get_new_domain_nr();
	} else {
		if (parent)
			pr_err("Node %pOF has ", parent->of_node);
		pr_err("Inconsistent \"linux,pci-domain\" property in DT\n");
		domain = -1;
	}

	return domain;
}

int pci_bus_find_domain_nr(struct pci_bus *bus, struct device *parent)
{
	return acpi_disabled ? of_pci_bus_find_domain_nr(parent) :
			       acpi_pci_bus_find_domain_nr(bus);
}
#endif

/**
 * pci_ext_cfg_avail - can we access extended PCI config space?
 *
 * Returns 1 if we can access PCI extended config space (offsets
 * greater than 0xff). This is the default implementation. Architecture
 * implementations can override this.
 */
int __weak pci_ext_cfg_avail(void)
{
	return 1;
}

void __weak pci_fixup_cardbus(struct pci_bus *bus)
{
}
EXPORT_SYMBOL(pci_fixup_cardbus);

static int __init pci_setup(char *str)
{
	while (str) {
		char *k = strchr(str, ',');
		if (k)
			*k++ = 0;
		if (*str && (str = pcibios_setup(str)) && *str) {
			if (!strcmp(str, "nomsi")) {
				pci_no_msi();
			} else if (!strncmp(str, "noats", 5)) {
				pr_info("PCIe: ATS is disabled\n");
				pcie_ats_disabled = true;
			} else if (!strcmp(str, "noaer")) {
				pci_no_aer();
			} else if (!strcmp(str, "earlydump")) {
				pci_early_dump = true;
			} else if (!strncmp(str, "realloc=", 8)) {
				pci_realloc_get_opt(str + 8);
			} else if (!strncmp(str, "realloc", 7)) {
				pci_realloc_get_opt("on");
			} else if (!strcmp(str, "nodomains")) {
				pci_no_domains();
			} else if (!strncmp(str, "noari", 5)) {
				pcie_ari_disabled = true;
			} else if (!strncmp(str, "cbiosize=", 9)) {
				pci_cardbus_io_size = memparse(str + 9, &str);
			} else if (!strncmp(str, "cbmemsize=", 10)) {
				pci_cardbus_mem_size = memparse(str + 10, &str);
			} else if (!strncmp(str, "resource_alignment=", 19)) {
				resource_alignment_param = str + 19;
			} else if (!strncmp(str, "ecrc=", 5)) {
				pcie_ecrc_get_policy(str + 5);
			} else if (!strncmp(str, "hpiosize=", 9)) {
				pci_hotplug_io_size = memparse(str + 9, &str);
			} else if (!strncmp(str, "hpmmiosize=", 11)) {
				pci_hotplug_mmio_size = memparse(str + 11, &str);
			} else if (!strncmp(str, "hpmmioprefsize=", 15)) {
				pci_hotplug_mmio_pref_size = memparse(str + 15, &str);
			} else if (!strncmp(str, "hpmemsize=", 10)) {
				pci_hotplug_mmio_size = memparse(str + 10, &str);
				pci_hotplug_mmio_pref_size = pci_hotplug_mmio_size;
			} else if (!strncmp(str, "hpbussize=", 10)) {
				pci_hotplug_bus_size =
					simple_strtoul(str + 10, &str, 0);
				if (pci_hotplug_bus_size > 0xff)
					pci_hotplug_bus_size = DEFAULT_HOTPLUG_BUS_SIZE;
			} else if (!strncmp(str, "pcie_bus_tune_off", 17)) {
				pcie_bus_config = PCIE_BUS_TUNE_OFF;
			} else if (!strncmp(str, "pcie_bus_safe", 13)) {
				pcie_bus_config = PCIE_BUS_SAFE;
			} else if (!strncmp(str, "pcie_bus_perf", 13)) {
				pcie_bus_config = PCIE_BUS_PERFORMANCE;
			} else if (!strncmp(str, "pcie_bus_peer2peer", 18)) {
				pcie_bus_config = PCIE_BUS_PEER2PEER;
			} else if (!strncmp(str, "pcie_scan_all", 13)) {
				pci_add_flags(PCI_SCAN_ALL_PCIE_DEVS);
			} else if (!strncmp(str, "disable_acs_redir=", 18)) {
				disable_acs_redir_param = str + 18;
			} else {
				pr_err("PCI: Unknown option `%s'\n", str);
			}
		}
		str = k;
	}
	return 0;
}
early_param("pci", pci_setup);

/*
 * 'resource_alignment_param' and 'disable_acs_redir_param' are initialized
 * in pci_setup(), above, to point to data in the __initdata section which
 * will be freed after the init sequence is complete. We can't allocate memory
 * in pci_setup() because some architectures do not have any memory allocation
 * service available during an early_param() call. So we allocate memory and
 * copy the variable here before the init section is freed.
 *
 */
static int __init pci_realloc_setup_params(void)
{
	resource_alignment_param = kstrdup(resource_alignment_param,
					   GFP_KERNEL);
	disable_acs_redir_param = kstrdup(disable_acs_redir_param, GFP_KERNEL);

	return 0;
}
pure_initcall(pci_realloc_setup_params);
