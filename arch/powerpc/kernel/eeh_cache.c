// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PCI address cache; allows the lookup of PCI devices based on I/O address
 *
 * Copyright IBM Corporation 2004
 * Copyright Linas Vepstas <linas@austin.ibm.com> 2004
 */

#include <linux/list.h>
#include <linux/pci.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <asm/pci-bridge.h>
#include <asm/debugfs.h>
#include <asm/ppc-pci.h>


/**
 * DOC: Overview
 *
 * The pci address cache subsystem.  This subsystem places
 * PCI device address resources into a red-black tree, sorted
 * according to the address range, so that given only an i/o
 * address, the corresponding PCI device can be **quickly**
 * found. It is safe to perform an address lookup in an interrupt
 * context; this ability is an important feature.
 *
 * Currently, the only customer of this code is the EEH subsystem;
 * thus, this code has been somewhat tailored to suit EEH better.
 * In particular, the cache does *not* hold the addresses of devices
 * for which EEH is not enabled.
 *
 * (Implementation Note: The RB tree seems to be better/faster
 * than any hash algo I could think of for this problem, even
 * with the penalty of slow pointer chases for d-cache misses).
 */

struct pci_io_addr_range {
	struct rb_node rb_node;
	resource_size_t addr_lo;
	resource_size_t addr_hi;
	struct eeh_dev *edev;
	struct pci_dev *pcidev;
	unsigned long flags;
};

static struct pci_io_addr_cache {
	struct rb_root rb_root;
	spinlock_t piar_lock;
} pci_io_addr_cache_root;

static inline struct eeh_dev *__eeh_addr_cache_get_device(unsigned long addr)
{
	struct rb_node *n = pci_io_addr_cache_root.rb_root.rb_node;

	while (n) {
		struct pci_io_addr_range *piar;
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);

		if (addr < piar->addr_lo)
			n = n->rb_left;
		else if (addr > piar->addr_hi)
			n = n->rb_right;
		else
			return piar->edev;
	}

	return NULL;
}

/**
 * eeh_addr_cache_get_dev - Get device, given only address
 * @addr: mmio (PIO) phys address or i/o port number
 *
 * Given an mmio phys address, or a port number, find a pci device
 * that implements this address.  I/O port numbers are assumed to be offset
 * from zero (that is, they do *not* have pci_io_addr added in).
 * It is safe to call this function within an interrupt.
 */
struct eeh_dev *eeh_addr_cache_get_dev(unsigned long addr)
{
	struct eeh_dev *edev;
	unsigned long flags;

	spin_lock_irqsave(&pci_io_addr_cache_root.piar_lock, flags);
	edev = __eeh_addr_cache_get_device(addr);
	spin_unlock_irqrestore(&pci_io_addr_cache_root.piar_lock, flags);
	return edev;
}

#ifdef DEBUG
/*
 * Handy-dandy debug print routine, does nothing more
 * than print out the contents of our addr cache.
 */
static void eeh_addr_cache_print(struct pci_io_addr_cache *cache)
{
	struct rb_node *n;
	int cnt = 0;

	n = rb_first(&cache->rb_root);
	while (n) {
		struct pci_io_addr_range *piar;
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);
		pr_info("PCI: %s addr range %d [%pap-%pap]: %s\n",
		       (piar->flags & IORESOURCE_IO) ? "i/o" : "mem", cnt,
		       &piar->addr_lo, &piar->addr_hi, pci_name(piar->pcidev));
		cnt++;
		n = rb_next(n);
	}
}
#endif

/* Insert address range into the rb tree. */
static struct pci_io_addr_range *
eeh_addr_cache_insert(struct pci_dev *dev, resource_size_t alo,
		      resource_size_t ahi, unsigned long flags)
{
	struct rb_node **p = &pci_io_addr_cache_root.rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct pci_io_addr_range *piar;

	/* Walk tree, find a place to insert into tree */
	while (*p) {
		parent = *p;
		piar = rb_entry(parent, struct pci_io_addr_range, rb_node);
		if (ahi < piar->addr_lo) {
			p = &parent->rb_left;
		} else if (alo > piar->addr_hi) {
			p = &parent->rb_right;
		} else {
			if (dev != piar->pcidev ||
			    alo != piar->addr_lo || ahi != piar->addr_hi) {
				pr_warn("PIAR: overlapping address range\n");
			}
			return piar;
		}
	}
	piar = kzalloc(sizeof(struct pci_io_addr_range), GFP_ATOMIC);
	if (!piar)
		return NULL;

	piar->addr_lo = alo;
	piar->addr_hi = ahi;
	piar->edev = pci_dev_to_eeh_dev(dev);
	piar->pcidev = dev;
	piar->flags = flags;

	eeh_edev_dbg(piar->edev, "PIAR: insert range=[%pap:%pap]\n",
		 &alo, &ahi);

	rb_link_node(&piar->rb_node, parent, p);
	rb_insert_color(&piar->rb_node, &pci_io_addr_cache_root.rb_root);

	return piar;
}

static void __eeh_addr_cache_insert_dev(struct pci_dev *dev)
{
	struct eeh_dev *edev;
	int i;

	edev = pci_dev_to_eeh_dev(dev);
	if (!edev) {
		pr_warn("PCI: no EEH dev found for %s\n",
			pci_name(dev));
		return;
	}

	/* Skip any devices for which EEH is not enabled. */
	if (!edev->pe) {
		dev_dbg(&dev->dev, "EEH: Skip building address cache\n");
		return;
	}

	/*
	 * Walk resources on this device, poke the first 7 (6 normal BAR and 1
	 * ROM BAR) into the tree.
	 */
	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		resource_size_t start = pci_resource_start(dev,i);
		resource_size_t end = pci_resource_end(dev,i);
		unsigned long flags = pci_resource_flags(dev,i);

		/* We are interested only bus addresses, not dma or other stuff */
		if (0 == (flags & (IORESOURCE_IO | IORESOURCE_MEM)))
			continue;
		if (start == 0 || ~start == 0 || end == 0 || ~end == 0)
			 continue;
		eeh_addr_cache_insert(dev, start, end, flags);
	}
}

/**
 * eeh_addr_cache_insert_dev - Add a device to the address cache
 * @dev: PCI device whose I/O addresses we are interested in.
 *
 * In order to support the fast lookup of devices based on addresses,
 * we maintain a cache of devices that can be quickly searched.
 * This routine adds a device to that cache.
 */
void eeh_addr_cache_insert_dev(struct pci_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pci_io_addr_cache_root.piar_lock, flags);
	__eeh_addr_cache_insert_dev(dev);
	spin_unlock_irqrestore(&pci_io_addr_cache_root.piar_lock, flags);
}

static inline void __eeh_addr_cache_rmv_dev(struct pci_dev *dev)
{
	struct rb_node *n;

restart:
	n = rb_first(&pci_io_addr_cache_root.rb_root);
	while (n) {
		struct pci_io_addr_range *piar;
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);

		if (piar->pcidev == dev) {
			eeh_edev_dbg(piar->edev, "PIAR: remove range=[%pap:%pap]\n",
				 &piar->addr_lo, &piar->addr_hi);
			rb_erase(n, &pci_io_addr_cache_root.rb_root);
			kfree(piar);
			goto restart;
		}
		n = rb_next(n);
	}
}

/**
 * eeh_addr_cache_rmv_dev - remove pci device from addr cache
 * @dev: device to remove
 *
 * Remove a device from the addr-cache tree.
 * This is potentially expensive, since it will walk
 * the tree multiple times (once per resource).
 * But so what; device removal doesn't need to be that fast.
 */
void eeh_addr_cache_rmv_dev(struct pci_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pci_io_addr_cache_root.piar_lock, flags);
	__eeh_addr_cache_rmv_dev(dev);
	spin_unlock_irqrestore(&pci_io_addr_cache_root.piar_lock, flags);
}

/**
 * eeh_addr_cache_init - Initialize a cache of I/O addresses
 *
 * Initialize a cache of pci i/o addresses.  This cache will be used to
 * find the pci device that corresponds to a given address.
 */
void eeh_addr_cache_init(void)
{
	spin_lock_init(&pci_io_addr_cache_root.piar_lock);
}

static int eeh_addr_cache_show(struct seq_file *s, void *v)
{
	struct pci_io_addr_range *piar;
	struct rb_node *n;

	spin_lock(&pci_io_addr_cache_root.piar_lock);
	for (n = rb_first(&pci_io_addr_cache_root.rb_root); n; n = rb_next(n)) {
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);

		seq_printf(s, "%s addr range [%pap-%pap]: %s\n",
		       (piar->flags & IORESOURCE_IO) ? "i/o" : "mem",
		       &piar->addr_lo, &piar->addr_hi, pci_name(piar->pcidev));
	}
	spin_unlock(&pci_io_addr_cache_root.piar_lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(eeh_addr_cache);

void eeh_cache_debugfs_init(void)
{
	debugfs_create_file_unsafe("eeh_address_cache", 0400,
			powerpc_debugfs_root, NULL,
			&eeh_addr_cache_fops);
}
