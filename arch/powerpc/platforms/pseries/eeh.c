/*
 * eeh.c
 * Copyright (C) 2001 Dave Engebretsen & Todd Inglett IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <asm/eeh.h>
#include <asm/eeh_event.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ppc-pci.h>
#include <asm/rtas.h>

#undef DEBUG

/** Overview:
 *  EEH, or "Extended Error Handling" is a PCI bridge technology for
 *  dealing with PCI bus errors that can't be dealt with within the
 *  usual PCI framework, except by check-stopping the CPU.  Systems
 *  that are designed for high-availability/reliability cannot afford
 *  to crash due to a "mere" PCI error, thus the need for EEH.
 *  An EEH-capable bridge operates by converting a detected error
 *  into a "slot freeze", taking the PCI adapter off-line, making
 *  the slot behave, from the OS'es point of view, as if the slot
 *  were "empty": all reads return 0xff's and all writes are silently
 *  ignored.  EEH slot isolation events can be triggered by parity
 *  errors on the address or data busses (e.g. during posted writes),
 *  which in turn might be caused by low voltage on the bus, dust,
 *  vibration, humidity, radioactivity or plain-old failed hardware.
 *
 *  Note, however, that one of the leading causes of EEH slot
 *  freeze events are buggy device drivers, buggy device microcode,
 *  or buggy device hardware.  This is because any attempt by the
 *  device to bus-master data to a memory address that is not
 *  assigned to the device will trigger a slot freeze.   (The idea
 *  is to prevent devices-gone-wild from corrupting system memory).
 *  Buggy hardware/drivers will have a miserable time co-existing
 *  with EEH.
 *
 *  Ideally, a PCI device driver, when suspecting that an isolation
 *  event has occured (e.g. by reading 0xff's), will then ask EEH
 *  whether this is the case, and then take appropriate steps to
 *  reset the PCI slot, the PCI device, and then resume operations.
 *  However, until that day,  the checking is done here, with the
 *  eeh_check_failure() routine embedded in the MMIO macros.  If
 *  the slot is found to be isolated, an "EEH Event" is synthesized
 *  and sent out for processing.
 */

/* If a device driver keeps reading an MMIO register in an interrupt
 * handler after a slot isolation event has occurred, we assume it
 * is broken and panic.  This sets the threshold for how many read
 * attempts we allow before panicking.
 */
#define EEH_MAX_FAILS	100000

/* Misc forward declaraions */
static void eeh_save_bars(struct pci_dev * pdev, struct pci_dn *pdn);

/* RTAS tokens */
static int ibm_set_eeh_option;
static int ibm_set_slot_reset;
static int ibm_read_slot_reset_state;
static int ibm_read_slot_reset_state2;
static int ibm_slot_error_detail;

int eeh_subsystem_enabled;
EXPORT_SYMBOL(eeh_subsystem_enabled);

/* Lock to avoid races due to multiple reports of an error */
static DEFINE_SPINLOCK(confirm_error_lock);

/* Buffer for reporting slot-error-detail rtas calls */
static unsigned char slot_errbuf[RTAS_ERROR_LOG_MAX];
static DEFINE_SPINLOCK(slot_errbuf_lock);
static int eeh_error_buf_size;

/* System monitoring statistics */
static DEFINE_PER_CPU(unsigned long, no_device);
static DEFINE_PER_CPU(unsigned long, no_dn);
static DEFINE_PER_CPU(unsigned long, no_cfg_addr);
static DEFINE_PER_CPU(unsigned long, ignored_check);
static DEFINE_PER_CPU(unsigned long, total_mmio_ffs);
static DEFINE_PER_CPU(unsigned long, false_positives);
static DEFINE_PER_CPU(unsigned long, ignored_failures);
static DEFINE_PER_CPU(unsigned long, slot_resets);

/**
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
struct pci_io_addr_range
{
	struct rb_node rb_node;
	unsigned long addr_lo;
	unsigned long addr_hi;
	struct pci_dev *pcidev;
	unsigned int flags;
};

static struct pci_io_addr_cache
{
	struct rb_root rb_root;
	spinlock_t piar_lock;
} pci_io_addr_cache_root;

static inline struct pci_dev *__pci_get_device_by_addr(unsigned long addr)
{
	struct rb_node *n = pci_io_addr_cache_root.rb_root.rb_node;

	while (n) {
		struct pci_io_addr_range *piar;
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);

		if (addr < piar->addr_lo) {
			n = n->rb_left;
		} else {
			if (addr > piar->addr_hi) {
				n = n->rb_right;
			} else {
				pci_dev_get(piar->pcidev);
				return piar->pcidev;
			}
		}
	}

	return NULL;
}

/**
 * pci_get_device_by_addr - Get device, given only address
 * @addr: mmio (PIO) phys address or i/o port number
 *
 * Given an mmio phys address, or a port number, find a pci device
 * that implements this address.  Be sure to pci_dev_put the device
 * when finished.  I/O port numbers are assumed to be offset
 * from zero (that is, they do *not* have pci_io_addr added in).
 * It is safe to call this function within an interrupt.
 */
static struct pci_dev *pci_get_device_by_addr(unsigned long addr)
{
	struct pci_dev *dev;
	unsigned long flags;

	spin_lock_irqsave(&pci_io_addr_cache_root.piar_lock, flags);
	dev = __pci_get_device_by_addr(addr);
	spin_unlock_irqrestore(&pci_io_addr_cache_root.piar_lock, flags);
	return dev;
}

#ifdef DEBUG
/*
 * Handy-dandy debug print routine, does nothing more
 * than print out the contents of our addr cache.
 */
static void pci_addr_cache_print(struct pci_io_addr_cache *cache)
{
	struct rb_node *n;
	int cnt = 0;

	n = rb_first(&cache->rb_root);
	while (n) {
		struct pci_io_addr_range *piar;
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);
		printk(KERN_DEBUG "PCI: %s addr range %d [%lx-%lx]: %s\n",
		       (piar->flags & IORESOURCE_IO) ? "i/o" : "mem", cnt,
		       piar->addr_lo, piar->addr_hi, pci_name(piar->pcidev));
		cnt++;
		n = rb_next(n);
	}
}
#endif

/* Insert address range into the rb tree. */
static struct pci_io_addr_range *
pci_addr_cache_insert(struct pci_dev *dev, unsigned long alo,
		      unsigned long ahi, unsigned int flags)
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
				printk(KERN_WARNING "PIAR: overlapping address range\n");
			}
			return piar;
		}
	}
	piar = (struct pci_io_addr_range *)kmalloc(sizeof(struct pci_io_addr_range), GFP_ATOMIC);
	if (!piar)
		return NULL;

	piar->addr_lo = alo;
	piar->addr_hi = ahi;
	piar->pcidev = dev;
	piar->flags = flags;

#ifdef DEBUG
	printk(KERN_DEBUG "PIAR: insert range=[%lx:%lx] dev=%s\n",
	                  alo, ahi, pci_name (dev));
#endif

	rb_link_node(&piar->rb_node, parent, p);
	rb_insert_color(&piar->rb_node, &pci_io_addr_cache_root.rb_root);

	return piar;
}

static void __pci_addr_cache_insert_device(struct pci_dev *dev)
{
	struct device_node *dn;
	struct pci_dn *pdn;
	int i;
	int inserted = 0;

	dn = pci_device_to_OF_node(dev);
	if (!dn) {
		printk(KERN_WARNING "PCI: no pci dn found for dev=%s\n", pci_name(dev));
		return;
	}

	/* Skip any devices for which EEH is not enabled. */
	pdn = PCI_DN(dn);
	if (!(pdn->eeh_mode & EEH_MODE_SUPPORTED) ||
	    pdn->eeh_mode & EEH_MODE_NOCHECK) {
#ifdef DEBUG
		printk(KERN_INFO "PCI: skip building address cache for=%s - %s\n",
		       pci_name(dev), pdn->node->full_name);
#endif
		return;
	}

	/* The cache holds a reference to the device... */
	pci_dev_get(dev);

	/* Walk resources on this device, poke them into the tree */
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		unsigned long start = pci_resource_start(dev,i);
		unsigned long end = pci_resource_end(dev,i);
		unsigned int flags = pci_resource_flags(dev,i);

		/* We are interested only bus addresses, not dma or other stuff */
		if (0 == (flags & (IORESOURCE_IO | IORESOURCE_MEM)))
			continue;
		if (start == 0 || ~start == 0 || end == 0 || ~end == 0)
			 continue;
		pci_addr_cache_insert(dev, start, end, flags);
		inserted = 1;
	}

	/* If there was nothing to add, the cache has no reference... */
	if (!inserted)
		pci_dev_put(dev);
}

/**
 * pci_addr_cache_insert_device - Add a device to the address cache
 * @dev: PCI device whose I/O addresses we are interested in.
 *
 * In order to support the fast lookup of devices based on addresses,
 * we maintain a cache of devices that can be quickly searched.
 * This routine adds a device to that cache.
 */
static void pci_addr_cache_insert_device(struct pci_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pci_io_addr_cache_root.piar_lock, flags);
	__pci_addr_cache_insert_device(dev);
	spin_unlock_irqrestore(&pci_io_addr_cache_root.piar_lock, flags);
}

static inline void __pci_addr_cache_remove_device(struct pci_dev *dev)
{
	struct rb_node *n;
	int removed = 0;

restart:
	n = rb_first(&pci_io_addr_cache_root.rb_root);
	while (n) {
		struct pci_io_addr_range *piar;
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);

		if (piar->pcidev == dev) {
			rb_erase(n, &pci_io_addr_cache_root.rb_root);
			removed = 1;
			kfree(piar);
			goto restart;
		}
		n = rb_next(n);
	}

	/* The cache no longer holds its reference to this device... */
	if (removed)
		pci_dev_put(dev);
}

/**
 * pci_addr_cache_remove_device - remove pci device from addr cache
 * @dev: device to remove
 *
 * Remove a device from the addr-cache tree.
 * This is potentially expensive, since it will walk
 * the tree multiple times (once per resource).
 * But so what; device removal doesn't need to be that fast.
 */
static void pci_addr_cache_remove_device(struct pci_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pci_io_addr_cache_root.piar_lock, flags);
	__pci_addr_cache_remove_device(dev);
	spin_unlock_irqrestore(&pci_io_addr_cache_root.piar_lock, flags);
}

/**
 * pci_addr_cache_build - Build a cache of I/O addresses
 *
 * Build a cache of pci i/o addresses.  This cache will be used to
 * find the pci device that corresponds to a given address.
 * This routine scans all pci busses to build the cache.
 * Must be run late in boot process, after the pci controllers
 * have been scaned for devices (after all device resources are known).
 */
void __init pci_addr_cache_build(void)
{
	struct device_node *dn;
	struct pci_dev *dev = NULL;

	if (!eeh_subsystem_enabled)
		return;

	spin_lock_init(&pci_io_addr_cache_root.piar_lock);

	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		/* Ignore PCI bridges ( XXX why ??) */
		if ((dev->class >> 16) == PCI_BASE_CLASS_BRIDGE) {
			continue;
		}
		pci_addr_cache_insert_device(dev);

		/* Save the BAR's; firmware doesn't restore these after EEH reset */
		dn = pci_device_to_OF_node(dev);
		eeh_save_bars(dev, PCI_DN(dn));
	}

#ifdef DEBUG
	/* Verify tree built up above, echo back the list of addrs. */
	pci_addr_cache_print(&pci_io_addr_cache_root);
#endif
}

/* --------------------------------------------------------------- */
/* Above lies the PCI Address Cache. Below lies the EEH event infrastructure */

void eeh_slot_error_detail (struct pci_dn *pdn, int severity)
{
	unsigned long flags;
	int rc;

	/* Log the error with the rtas logger */
	spin_lock_irqsave(&slot_errbuf_lock, flags);
	memset(slot_errbuf, 0, eeh_error_buf_size);

	rc = rtas_call(ibm_slot_error_detail,
	               8, 1, NULL, pdn->eeh_config_addr,
	               BUID_HI(pdn->phb->buid),
	               BUID_LO(pdn->phb->buid), NULL, 0,
	               virt_to_phys(slot_errbuf),
	               eeh_error_buf_size,
	               severity);

	if (rc == 0)
		log_error(slot_errbuf, ERR_TYPE_RTAS_LOG, 0);
	spin_unlock_irqrestore(&slot_errbuf_lock, flags);
}

/**
 * read_slot_reset_state - Read the reset state of a device node's slot
 * @dn: device node to read
 * @rets: array to return results in
 */
static int read_slot_reset_state(struct pci_dn *pdn, int rets[])
{
	int token, outputs;

	if (ibm_read_slot_reset_state2 != RTAS_UNKNOWN_SERVICE) {
		token = ibm_read_slot_reset_state2;
		outputs = 4;
	} else {
		token = ibm_read_slot_reset_state;
		rets[2] = 0; /* fake PE Unavailable info */
		outputs = 3;
	}

	return rtas_call(token, 3, outputs, rets, pdn->eeh_config_addr,
			 BUID_HI(pdn->phb->buid), BUID_LO(pdn->phb->buid));
}

/**
 * eeh_token_to_phys - convert EEH address token to phys address
 * @token i/o token, should be address in the form 0xA....
 */
static inline unsigned long eeh_token_to_phys(unsigned long token)
{
	pte_t *ptep;
	unsigned long pa;

	ptep = find_linux_pte(init_mm.pgd, token);
	if (!ptep)
		return token;
	pa = pte_pfn(*ptep) << PAGE_SHIFT;

	return pa | (token & (PAGE_SIZE-1));
}

/** 
 * Return the "partitionable endpoint" (pe) under which this device lies
 */
static struct device_node * find_device_pe(struct device_node *dn)
{
	while ((dn->parent) && PCI_DN(dn->parent) &&
	      (PCI_DN(dn->parent)->eeh_mode & EEH_MODE_SUPPORTED)) {
		dn = dn->parent;
	}
	return dn;
}

/** Mark all devices that are peers of this device as failed.
 *  Mark the device driver too, so that it can see the failure
 *  immediately; this is critical, since some drivers poll
 *  status registers in interrupts ... If a driver is polling,
 *  and the slot is frozen, then the driver can deadlock in
 *  an interrupt context, which is bad.
 */

static void __eeh_mark_slot (struct device_node *dn, int mode_flag)
{
	while (dn) {
		if (PCI_DN(dn)) {
			PCI_DN(dn)->eeh_mode |= mode_flag;

			if (dn->child)
				__eeh_mark_slot (dn->child, mode_flag);
		}
		dn = dn->sibling;
	}
}

void eeh_mark_slot (struct device_node *dn, int mode_flag)
{
	dn = find_device_pe (dn);
	PCI_DN(dn)->eeh_mode |= mode_flag;
	__eeh_mark_slot (dn->child, mode_flag);
}

static void __eeh_clear_slot (struct device_node *dn, int mode_flag)
{
	while (dn) {
		if (PCI_DN(dn)) {
			PCI_DN(dn)->eeh_mode &= ~mode_flag;
			PCI_DN(dn)->eeh_check_count = 0;
			if (dn->child)
				__eeh_clear_slot (dn->child, mode_flag);
		}
		dn = dn->sibling;
	}
}

void eeh_clear_slot (struct device_node *dn, int mode_flag)
{
	unsigned long flags;
	spin_lock_irqsave(&confirm_error_lock, flags);
	dn = find_device_pe (dn);
	PCI_DN(dn)->eeh_mode &= ~mode_flag;
	PCI_DN(dn)->eeh_check_count = 0;
	__eeh_clear_slot (dn->child, mode_flag);
	spin_unlock_irqrestore(&confirm_error_lock, flags);
}

/**
 * eeh_dn_check_failure - check if all 1's data is due to EEH slot freeze
 * @dn device node
 * @dev pci device, if known
 *
 * Check for an EEH failure for the given device node.  Call this
 * routine if the result of a read was all 0xff's and you want to
 * find out if this is due to an EEH slot freeze.  This routine
 * will query firmware for the EEH status.
 *
 * Returns 0 if there has not been an EEH error; otherwise returns
 * a non-zero value and queues up a slot isolation event notification.
 *
 * It is safe to call this routine in an interrupt context.
 */
int eeh_dn_check_failure(struct device_node *dn, struct pci_dev *dev)
{
	int ret;
	int rets[3];
	unsigned long flags;
	struct pci_dn *pdn;
	int rc = 0;

	__get_cpu_var(total_mmio_ffs)++;

	if (!eeh_subsystem_enabled)
		return 0;

	if (!dn) {
		__get_cpu_var(no_dn)++;
		return 0;
	}
	pdn = PCI_DN(dn);

	/* Access to IO BARs might get this far and still not want checking. */
	if (!(pdn->eeh_mode & EEH_MODE_SUPPORTED) ||
	    pdn->eeh_mode & EEH_MODE_NOCHECK) {
		__get_cpu_var(ignored_check)++;
#ifdef DEBUG
		printk ("EEH:ignored check (%x) for %s %s\n", 
		        pdn->eeh_mode, pci_name (dev), dn->full_name);
#endif
		return 0;
	}

	if (!pdn->eeh_config_addr) {
		__get_cpu_var(no_cfg_addr)++;
		return 0;
	}

	/* If we already have a pending isolation event for this
	 * slot, we know it's bad already, we don't need to check.
	 * Do this checking under a lock; as multiple PCI devices
	 * in one slot might report errors simultaneously, and we
	 * only want one error recovery routine running.
	 */
	spin_lock_irqsave(&confirm_error_lock, flags);
	rc = 1;
	if (pdn->eeh_mode & EEH_MODE_ISOLATED) {
		pdn->eeh_check_count ++;
		if (pdn->eeh_check_count >= EEH_MAX_FAILS) {
			printk (KERN_ERR "EEH: Device driver ignored %d bad reads, panicing\n",
			        pdn->eeh_check_count);
			dump_stack();
			
			/* re-read the slot reset state */
			if (read_slot_reset_state(pdn, rets) != 0)
				rets[0] = -1;	/* reset state unknown */

			/* If we are here, then we hit an infinite loop. Stop. */
			panic("EEH: MMIO halt (%d) on device:%s\n", rets[0], pci_name(dev));
		}
		goto dn_unlock;
	}

	/*
	 * Now test for an EEH failure.  This is VERY expensive.
	 * Note that the eeh_config_addr may be a parent device
	 * in the case of a device behind a bridge, or it may be
	 * function zero of a multi-function device.
	 * In any case they must share a common PHB.
	 */
	ret = read_slot_reset_state(pdn, rets);

	/* If the call to firmware failed, punt */
	if (ret != 0) {
		printk(KERN_WARNING "EEH: read_slot_reset_state() failed; rc=%d dn=%s\n",
		       ret, dn->full_name);
		__get_cpu_var(false_positives)++;
		rc = 0;
		goto dn_unlock;
	}

	/* If EEH is not supported on this device, punt. */
	if (rets[1] != 1) {
		printk(KERN_WARNING "EEH: event on unsupported device, rc=%d dn=%s\n",
		       ret, dn->full_name);
		__get_cpu_var(false_positives)++;
		rc = 0;
		goto dn_unlock;
	}

	/* If not the kind of error we know about, punt. */
	if (rets[0] != 2 && rets[0] != 4 && rets[0] != 5) {
		__get_cpu_var(false_positives)++;
		rc = 0;
		goto dn_unlock;
	}

	/* Note that config-io to empty slots may fail;
	 * we recognize empty because they don't have children. */
	if ((rets[0] == 5) && (dn->child == NULL)) {
		__get_cpu_var(false_positives)++;
		rc = 0;
		goto dn_unlock;
	}

	__get_cpu_var(slot_resets)++;
 
	/* Avoid repeated reports of this failure, including problems
	 * with other functions on this device, and functions under
	 * bridges. */
	eeh_mark_slot (dn, EEH_MODE_ISOLATED);
	spin_unlock_irqrestore(&confirm_error_lock, flags);

	eeh_send_failure_event (dn, dev, rets[0], rets[2]);
	
	/* Most EEH events are due to device driver bugs.  Having
	 * a stack trace will help the device-driver authors figure
	 * out what happened.  So print that out. */
	if (rets[0] != 5) dump_stack();
	return 1;

dn_unlock:
	spin_unlock_irqrestore(&confirm_error_lock, flags);
	return rc;
}

EXPORT_SYMBOL_GPL(eeh_dn_check_failure);

/**
 * eeh_check_failure - check if all 1's data is due to EEH slot freeze
 * @token i/o token, should be address in the form 0xA....
 * @val value, should be all 1's (XXX why do we need this arg??)
 *
 * Check for an EEH failure at the given token address.  Call this
 * routine if the result of a read was all 0xff's and you want to
 * find out if this is due to an EEH slot freeze event.  This routine
 * will query firmware for the EEH status.
 *
 * Note this routine is safe to call in an interrupt context.
 */
unsigned long eeh_check_failure(const volatile void __iomem *token, unsigned long val)
{
	unsigned long addr;
	struct pci_dev *dev;
	struct device_node *dn;

	/* Finding the phys addr + pci device; this is pretty quick. */
	addr = eeh_token_to_phys((unsigned long __force) token);
	dev = pci_get_device_by_addr(addr);
	if (!dev) {
		__get_cpu_var(no_device)++;
		return val;
	}

	dn = pci_device_to_OF_node(dev);
	eeh_dn_check_failure (dn, dev);

	pci_dev_put(dev);
	return val;
}

EXPORT_SYMBOL(eeh_check_failure);

/* ------------------------------------------------------------- */
/* The code below deals with error recovery */

/** Return negative value if a permanent error, else return
 * a number of milliseconds to wait until the PCI slot is
 * ready to be used.
 */
static int
eeh_slot_availability(struct pci_dn *pdn)
{
	int rc;
	int rets[3];

	rc = read_slot_reset_state(pdn, rets);

	if (rc) return rc;

	if (rets[1] == 0) return -1;  /* EEH is not supported */
	if (rets[0] == 0)  return 0;  /* Oll Korrect */
	if (rets[0] == 5) {
		if (rets[2] == 0) return -1; /* permanently unavailable */
		return rets[2]; /* number of millisecs to wait */
	}
	return -1;
}

/** rtas_pci_slot_reset raises/lowers the pci #RST line
 *  state: 1/0 to raise/lower the #RST
 *
 * Clear the EEH-frozen condition on a slot.  This routine
 * asserts the PCI #RST line if the 'state' argument is '1',
 * and drops the #RST line if 'state is '0'.  This routine is
 * safe to call in an interrupt context.
 *
 */

static void
rtas_pci_slot_reset(struct pci_dn *pdn, int state)
{
	int rc;

	BUG_ON (pdn==NULL); 

	if (!pdn->phb) {
		printk (KERN_WARNING "EEH: in slot reset, device node %s has no phb\n",
		        pdn->node->full_name);
		return;
	}

	rc = rtas_call(ibm_set_slot_reset,4,1, NULL,
	               pdn->eeh_config_addr,
	               BUID_HI(pdn->phb->buid),
	               BUID_LO(pdn->phb->buid),
	               state);
	if (rc) {
		printk (KERN_WARNING "EEH: Unable to reset the failed slot, (%d) #RST=%d dn=%s\n", 
		        rc, state, pdn->node->full_name);
		return;
	}
}

/** rtas_set_slot_reset -- assert the pci #RST line for 1/4 second
 *  dn -- device node to be reset.
 */

void
rtas_set_slot_reset(struct pci_dn *pdn)
{
	int i, rc;

	rtas_pci_slot_reset (pdn, 1);

	/* The PCI bus requires that the reset be held high for at least
	 * a 100 milliseconds. We wait a bit longer 'just in case'.  */

#define PCI_BUS_RST_HOLD_TIME_MSEC 250
	msleep (PCI_BUS_RST_HOLD_TIME_MSEC);
	
	/* We might get hit with another EEH freeze as soon as the 
	 * pci slot reset line is dropped. Make sure we don't miss
	 * these, and clear the flag now. */
	eeh_clear_slot (pdn->node, EEH_MODE_ISOLATED);

	rtas_pci_slot_reset (pdn, 0);

	/* After a PCI slot has been reset, the PCI Express spec requires
	 * a 1.5 second idle time for the bus to stabilize, before starting
	 * up traffic. */
#define PCI_BUS_SETTLE_TIME_MSEC 1800
	msleep (PCI_BUS_SETTLE_TIME_MSEC);

	/* Now double check with the firmware to make sure the device is
	 * ready to be used; if not, wait for recovery. */
	for (i=0; i<10; i++) {
		rc = eeh_slot_availability (pdn);
		if (rc <= 0) break;

		msleep (rc+100);
	}
}

/* ------------------------------------------------------- */
/** Save and restore of PCI BARs
 *
 * Although firmware will set up BARs during boot, it doesn't
 * set up device BAR's after a device reset, although it will,
 * if requested, set up bridge configuration. Thus, we need to
 * configure the PCI devices ourselves.  
 */

/**
 * __restore_bars - Restore the Base Address Registers
 * Loads the PCI configuration space base address registers,
 * the expansion ROM base address, the latency timer, and etc.
 * from the saved values in the device node.
 */
static inline void __restore_bars (struct pci_dn *pdn)
{
	int i;

	if (NULL==pdn->phb) return;
	for (i=4; i<10; i++) {
		rtas_write_config(pdn, i*4, 4, pdn->config_space[i]);
	}

	/* 12 == Expansion ROM Address */
	rtas_write_config(pdn, 12*4, 4, pdn->config_space[12]);

#define BYTE_SWAP(OFF) (8*((OFF)/4)+3-(OFF))
#define SAVED_BYTE(OFF) (((u8 *)(pdn->config_space))[BYTE_SWAP(OFF)])

	rtas_write_config (pdn, PCI_CACHE_LINE_SIZE, 1,
	            SAVED_BYTE(PCI_CACHE_LINE_SIZE));

	rtas_write_config (pdn, PCI_LATENCY_TIMER, 1,
	            SAVED_BYTE(PCI_LATENCY_TIMER));

	/* max latency, min grant, interrupt pin and line */
	rtas_write_config(pdn, 15*4, 4, pdn->config_space[15]);
}

/**
 * eeh_restore_bars - restore the PCI config space info
 *
 * This routine performs a recursive walk to the children
 * of this device as well.
 */
void eeh_restore_bars(struct pci_dn *pdn)
{
	struct device_node *dn;
	if (!pdn) 
		return;
	
	if (! pdn->eeh_is_bridge)
		__restore_bars (pdn);

	dn = pdn->node->child;
	while (dn) {
		eeh_restore_bars (PCI_DN(dn));
		dn = dn->sibling;
	}
}

/**
 * eeh_save_bars - save device bars
 *
 * Save the values of the device bars. Unlike the restore
 * routine, this routine is *not* recursive. This is because
 * PCI devices are added individuallly; but, for the restore,
 * an entire slot is reset at a time.
 */
static void eeh_save_bars(struct pci_dev * pdev, struct pci_dn *pdn)
{
	int i;

	if (!pdev || !pdn )
		return;
	
	for (i = 0; i < 16; i++)
		pci_read_config_dword(pdev, i * 4, &pdn->config_space[i]);

	if (pdev->hdr_type == PCI_HEADER_TYPE_BRIDGE)
		pdn->eeh_is_bridge = 1;
}

void
rtas_configure_bridge(struct pci_dn *pdn)
{
	int token = rtas_token ("ibm,configure-bridge");
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return;
	rc = rtas_call(token,3,1, NULL,
	               pdn->eeh_config_addr,
	               BUID_HI(pdn->phb->buid),
	               BUID_LO(pdn->phb->buid));
	if (rc) {
		printk (KERN_WARNING "EEH: Unable to configure device bridge (%d) for %s\n",
		        rc, pdn->node->full_name);
	}
}

/* ------------------------------------------------------------- */
/* The code below deals with enabling EEH for devices during  the
 * early boot sequence.  EEH must be enabled before any PCI probing
 * can be done.
 */

#define EEH_ENABLE 1

struct eeh_early_enable_info {
	unsigned int buid_hi;
	unsigned int buid_lo;
};

/* Enable eeh for the given device node. */
static void *early_enable_eeh(struct device_node *dn, void *data)
{
	struct eeh_early_enable_info *info = data;
	int ret;
	char *status = get_property(dn, "status", NULL);
	u32 *class_code = (u32 *)get_property(dn, "class-code", NULL);
	u32 *vendor_id = (u32 *)get_property(dn, "vendor-id", NULL);
	u32 *device_id = (u32 *)get_property(dn, "device-id", NULL);
	u32 *regs;
	int enable;
	struct pci_dn *pdn = PCI_DN(dn);

	pdn->eeh_mode = 0;
	pdn->eeh_check_count = 0;
	pdn->eeh_freeze_count = 0;

	if (status && strcmp(status, "ok") != 0)
		return NULL;	/* ignore devices with bad status */

	/* Ignore bad nodes. */
	if (!class_code || !vendor_id || !device_id)
		return NULL;

	/* There is nothing to check on PCI to ISA bridges */
	if (dn->type && !strcmp(dn->type, "isa")) {
		pdn->eeh_mode |= EEH_MODE_NOCHECK;
		return NULL;
	}

	/*
	 * Now decide if we are going to "Disable" EEH checking
	 * for this device.  We still run with the EEH hardware active,
	 * but we won't be checking for ff's.  This means a driver
	 * could return bad data (very bad!), an interrupt handler could
	 * hang waiting on status bits that won't change, etc.
	 * But there are a few cases like display devices that make sense.
	 */
	enable = 1;	/* i.e. we will do checking */
	if ((*class_code >> 16) == PCI_BASE_CLASS_DISPLAY)
		enable = 0;

	if (!enable)
		pdn->eeh_mode |= EEH_MODE_NOCHECK;

	/* Ok... see if this device supports EEH.  Some do, some don't,
	 * and the only way to find out is to check each and every one. */
	regs = (u32 *)get_property(dn, "reg", NULL);
	if (regs) {
		/* First register entry is addr (00BBSS00)  */
		/* Try to enable eeh */
		ret = rtas_call(ibm_set_eeh_option, 4, 1, NULL,
		                regs[0], info->buid_hi, info->buid_lo,
		                EEH_ENABLE);

		if (ret == 0) {
			eeh_subsystem_enabled = 1;
			pdn->eeh_mode |= EEH_MODE_SUPPORTED;
			pdn->eeh_config_addr = regs[0];
#ifdef DEBUG
			printk(KERN_DEBUG "EEH: %s: eeh enabled\n", dn->full_name);
#endif
		} else {

			/* This device doesn't support EEH, but it may have an
			 * EEH parent, in which case we mark it as supported. */
			if (dn->parent && PCI_DN(dn->parent)
			    && (PCI_DN(dn->parent)->eeh_mode & EEH_MODE_SUPPORTED)) {
				/* Parent supports EEH. */
				pdn->eeh_mode |= EEH_MODE_SUPPORTED;
				pdn->eeh_config_addr = PCI_DN(dn->parent)->eeh_config_addr;
				return NULL;
			}
		}
	} else {
		printk(KERN_WARNING "EEH: %s: unable to get reg property.\n",
		       dn->full_name);
	}

	return NULL;
}

/*
 * Initialize EEH by trying to enable it for all of the adapters in the system.
 * As a side effect we can determine here if eeh is supported at all.
 * Note that we leave EEH on so failed config cycles won't cause a machine
 * check.  If a user turns off EEH for a particular adapter they are really
 * telling Linux to ignore errors.  Some hardware (e.g. POWER5) won't
 * grant access to a slot if EEH isn't enabled, and so we always enable
 * EEH for all slots/all devices.
 *
 * The eeh-force-off option disables EEH checking globally, for all slots.
 * Even if force-off is set, the EEH hardware is still enabled, so that
 * newer systems can boot.
 */
void __init eeh_init(void)
{
	struct device_node *phb, *np;
	struct eeh_early_enable_info info;

	spin_lock_init(&confirm_error_lock);
	spin_lock_init(&slot_errbuf_lock);

	np = of_find_node_by_path("/rtas");
	if (np == NULL)
		return;

	ibm_set_eeh_option = rtas_token("ibm,set-eeh-option");
	ibm_set_slot_reset = rtas_token("ibm,set-slot-reset");
	ibm_read_slot_reset_state2 = rtas_token("ibm,read-slot-reset-state2");
	ibm_read_slot_reset_state = rtas_token("ibm,read-slot-reset-state");
	ibm_slot_error_detail = rtas_token("ibm,slot-error-detail");

	if (ibm_set_eeh_option == RTAS_UNKNOWN_SERVICE)
		return;

	eeh_error_buf_size = rtas_token("rtas-error-log-max");
	if (eeh_error_buf_size == RTAS_UNKNOWN_SERVICE) {
		eeh_error_buf_size = 1024;
	}
	if (eeh_error_buf_size > RTAS_ERROR_LOG_MAX) {
		printk(KERN_WARNING "EEH: rtas-error-log-max is bigger than allocated "
		      "buffer ! (%d vs %d)", eeh_error_buf_size, RTAS_ERROR_LOG_MAX);
		eeh_error_buf_size = RTAS_ERROR_LOG_MAX;
	}

	/* Enable EEH for all adapters.  Note that eeh requires buid's */
	for (phb = of_find_node_by_name(NULL, "pci"); phb;
	     phb = of_find_node_by_name(phb, "pci")) {
		unsigned long buid;

		buid = get_phb_buid(phb);
		if (buid == 0 || PCI_DN(phb) == NULL)
			continue;

		info.buid_lo = BUID_LO(buid);
		info.buid_hi = BUID_HI(buid);
		traverse_pci_devices(phb, early_enable_eeh, &info);
	}

	if (eeh_subsystem_enabled)
		printk(KERN_INFO "EEH: PCI Enhanced I/O Error Handling Enabled\n");
	else
		printk(KERN_WARNING "EEH: No capable adapters found\n");
}

/**
 * eeh_add_device_early - enable EEH for the indicated device_node
 * @dn: device node for which to set up EEH
 *
 * This routine must be used to perform EEH initialization for PCI
 * devices that were added after system boot (e.g. hotplug, dlpar).
 * This routine must be called before any i/o is performed to the
 * adapter (inluding any config-space i/o).
 * Whether this actually enables EEH or not for this device depends
 * on the CEC architecture, type of the device, on earlier boot
 * command-line arguments & etc.
 */
void eeh_add_device_early(struct device_node *dn)
{
	struct pci_controller *phb;
	struct eeh_early_enable_info info;

	if (!dn || !PCI_DN(dn))
		return;
	phb = PCI_DN(dn)->phb;
	if (NULL == phb || 0 == phb->buid) {
		printk(KERN_WARNING "EEH: Expected buid but found none for %s\n",
		       dn->full_name);
		dump_stack();
		return;
	}

	info.buid_hi = BUID_HI(phb->buid);
	info.buid_lo = BUID_LO(phb->buid);
	early_enable_eeh(dn, &info);
}
EXPORT_SYMBOL_GPL(eeh_add_device_early);

/**
 * eeh_add_device_late - perform EEH initialization for the indicated pci device
 * @dev: pci device for which to set up EEH
 *
 * This routine must be used to complete EEH initialization for PCI
 * devices that were added after system boot (e.g. hotplug, dlpar).
 */
void eeh_add_device_late(struct pci_dev *dev)
{
	struct device_node *dn;
	struct pci_dn *pdn;

	if (!dev || !eeh_subsystem_enabled)
		return;

#ifdef DEBUG
	printk(KERN_DEBUG "EEH: adding device %s\n", pci_name(dev));
#endif

	pci_dev_get (dev);
	dn = pci_device_to_OF_node(dev);
	pdn = PCI_DN(dn);
	pdn->pcidev = dev;

	pci_addr_cache_insert_device (dev);
	eeh_save_bars(dev, pdn);
}
EXPORT_SYMBOL_GPL(eeh_add_device_late);

/**
 * eeh_remove_device - undo EEH setup for the indicated pci device
 * @dev: pci device to be removed
 *
 * This routine should be when a device is removed from a running
 * system (e.g. by hotplug or dlpar).
 */
void eeh_remove_device(struct pci_dev *dev)
{
	struct device_node *dn;
	if (!dev || !eeh_subsystem_enabled)
		return;

	/* Unregister the device with the EEH/PCI address search system */
#ifdef DEBUG
	printk(KERN_DEBUG "EEH: remove device %s\n", pci_name(dev));
#endif
	pci_addr_cache_remove_device(dev);

	dn = pci_device_to_OF_node(dev);
	PCI_DN(dn)->pcidev = NULL;
	pci_dev_put (dev);
}
EXPORT_SYMBOL_GPL(eeh_remove_device);

static int proc_eeh_show(struct seq_file *m, void *v)
{
	unsigned int cpu;
	unsigned long ffs = 0, positives = 0, failures = 0;
	unsigned long resets = 0;
	unsigned long no_dev = 0, no_dn = 0, no_cfg = 0, no_check = 0;

	for_each_cpu(cpu) {
		ffs += per_cpu(total_mmio_ffs, cpu);
		positives += per_cpu(false_positives, cpu);
		failures += per_cpu(ignored_failures, cpu);
		resets += per_cpu(slot_resets, cpu);
		no_dev += per_cpu(no_device, cpu);
		no_dn += per_cpu(no_dn, cpu);
		no_cfg += per_cpu(no_cfg_addr, cpu);
		no_check += per_cpu(ignored_check, cpu);
	}

	if (0 == eeh_subsystem_enabled) {
		seq_printf(m, "EEH Subsystem is globally disabled\n");
		seq_printf(m, "eeh_total_mmio_ffs=%ld\n", ffs);
	} else {
		seq_printf(m, "EEH Subsystem is enabled\n");
		seq_printf(m,
				"no device=%ld\n"
				"no device node=%ld\n"
				"no config address=%ld\n"
				"check not wanted=%ld\n"
				"eeh_total_mmio_ffs=%ld\n"
				"eeh_false_positives=%ld\n"
				"eeh_ignored_failures=%ld\n"
				"eeh_slot_resets=%ld\n",
				no_dev, no_dn, no_cfg, no_check,
				ffs, positives, failures, resets);
	}

	return 0;
}

static int proc_eeh_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_eeh_show, NULL);
}

static struct file_operations proc_eeh_operations = {
	.open      = proc_eeh_open,
	.read      = seq_read,
	.llseek    = seq_lseek,
	.release   = single_release,
};

static int __init eeh_init_proc(void)
{
	struct proc_dir_entry *e;

	if (platform_is_pseries()) {
		e = create_proc_entry("ppc64/eeh", 0, NULL);
		if (e)
			e->proc_fops = &proc_eeh_operations;
	}

	return 0;
}
__initcall(eeh_init_proc);
