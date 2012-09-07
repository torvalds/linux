/*
 * Copyright IBM Corporation 2001, 2005, 2006
 * Copyright Dave Engebretsen & Todd Inglett 2001
 * Copyright Linas Vepstas 2005, 2006
 * Copyright 2001-2012 IBM Corporation.
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
 *
 * Please address comments and feedback to Linas Vepstas <linas@austin.ibm.com>
 */

#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/of.h>

#include <linux/atomic.h>
#include <asm/eeh.h>
#include <asm/eeh_event.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ppc-pci.h>
#include <asm/rtas.h>


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
 *  event has occurred (e.g. by reading 0xff's), will then ask EEH
 *  whether this is the case, and then take appropriate steps to
 *  reset the PCI slot, the PCI device, and then resume operations.
 *  However, until that day,  the checking is done here, with the
 *  eeh_check_failure() routine embedded in the MMIO macros.  If
 *  the slot is found to be isolated, an "EEH Event" is synthesized
 *  and sent out for processing.
 */

/* If a device driver keeps reading an MMIO register in an interrupt
 * handler after a slot isolation event, it might be broken.
 * This sets the threshold for how many read attempts we allow
 * before printing an error message.
 */
#define EEH_MAX_FAILS	2100000

/* Time to wait for a PCI slot to report status, in milliseconds */
#define PCI_BUS_RESET_WAIT_MSEC (60*1000)

/* Platform dependent EEH operations */
struct eeh_ops *eeh_ops = NULL;

int eeh_subsystem_enabled;
EXPORT_SYMBOL(eeh_subsystem_enabled);

/* Global EEH mutex */
DEFINE_MUTEX(eeh_mutex);

/* Lock to avoid races due to multiple reports of an error */
static DEFINE_RAW_SPINLOCK(confirm_error_lock);

/* Buffer for reporting pci register dumps. Its here in BSS, and
 * not dynamically alloced, so that it ends up in RMO where RTAS
 * can access it.
 */
#define EEH_PCI_REGS_LOG_LEN 4096
static unsigned char pci_regs_buf[EEH_PCI_REGS_LOG_LEN];

/*
 * The struct is used to maintain the EEH global statistic
 * information. Besides, the EEH global statistics will be
 * exported to user space through procfs
 */
struct eeh_stats {
	u64 no_device;		/* PCI device not found		*/
	u64 no_dn;		/* OF node not found		*/
	u64 no_cfg_addr;	/* Config address not found	*/
	u64 ignored_check;	/* EEH check skipped		*/
	u64 total_mmio_ffs;	/* Total EEH checks		*/
	u64 false_positives;	/* Unnecessary EEH checks	*/
	u64 slot_resets;	/* PE reset			*/
};

static struct eeh_stats eeh_stats;

#define IS_BRIDGE(class_code) (((class_code)<<16) == PCI_BASE_CLASS_BRIDGE)

/**
 * eeh_gather_pci_data - Copy assorted PCI config space registers to buff
 * @edev: device to report data for
 * @buf: point to buffer in which to log
 * @len: amount of room in buffer
 *
 * This routine captures assorted PCI configuration space data,
 * and puts them into a buffer for RTAS error logging.
 */
static size_t eeh_gather_pci_data(struct eeh_dev *edev, char * buf, size_t len)
{
	struct device_node *dn = eeh_dev_to_of_node(edev);
	struct pci_dev *dev = eeh_dev_to_pci_dev(edev);
	u32 cfg;
	int cap, i;
	int n = 0;

	n += scnprintf(buf+n, len-n, "%s\n", dn->full_name);
	printk(KERN_WARNING "EEH: of node=%s\n", dn->full_name);

	eeh_ops->read_config(dn, PCI_VENDOR_ID, 4, &cfg);
	n += scnprintf(buf+n, len-n, "dev/vend:%08x\n", cfg);
	printk(KERN_WARNING "EEH: PCI device/vendor: %08x\n", cfg);

	eeh_ops->read_config(dn, PCI_COMMAND, 4, &cfg);
	n += scnprintf(buf+n, len-n, "cmd/stat:%x\n", cfg);
	printk(KERN_WARNING "EEH: PCI cmd/status register: %08x\n", cfg);

	if (!dev) {
		printk(KERN_WARNING "EEH: no PCI device for this of node\n");
		return n;
	}

	/* Gather bridge-specific registers */
	if (dev->class >> 16 == PCI_BASE_CLASS_BRIDGE) {
		eeh_ops->read_config(dn, PCI_SEC_STATUS, 2, &cfg);
		n += scnprintf(buf+n, len-n, "sec stat:%x\n", cfg);
		printk(KERN_WARNING "EEH: Bridge secondary status: %04x\n", cfg);

		eeh_ops->read_config(dn, PCI_BRIDGE_CONTROL, 2, &cfg);
		n += scnprintf(buf+n, len-n, "brdg ctl:%x\n", cfg);
		printk(KERN_WARNING "EEH: Bridge control: %04x\n", cfg);
	}

	/* Dump out the PCI-X command and status regs */
	cap = pci_find_capability(dev, PCI_CAP_ID_PCIX);
	if (cap) {
		eeh_ops->read_config(dn, cap, 4, &cfg);
		n += scnprintf(buf+n, len-n, "pcix-cmd:%x\n", cfg);
		printk(KERN_WARNING "EEH: PCI-X cmd: %08x\n", cfg);

		eeh_ops->read_config(dn, cap+4, 4, &cfg);
		n += scnprintf(buf+n, len-n, "pcix-stat:%x\n", cfg);
		printk(KERN_WARNING "EEH: PCI-X status: %08x\n", cfg);
	}

	/* If PCI-E capable, dump PCI-E cap 10, and the AER */
	cap = pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (cap) {
		n += scnprintf(buf+n, len-n, "pci-e cap10:\n");
		printk(KERN_WARNING
		       "EEH: PCI-E capabilities and status follow:\n");

		for (i=0; i<=8; i++) {
			eeh_ops->read_config(dn, cap+4*i, 4, &cfg);
			n += scnprintf(buf+n, len-n, "%02x:%x\n", 4*i, cfg);
			printk(KERN_WARNING "EEH: PCI-E %02x: %08x\n", i, cfg);
		}

		cap = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
		if (cap) {
			n += scnprintf(buf+n, len-n, "pci-e AER:\n");
			printk(KERN_WARNING
			       "EEH: PCI-E AER capability register set follows:\n");

			for (i=0; i<14; i++) {
				eeh_ops->read_config(dn, cap+4*i, 4, &cfg);
				n += scnprintf(buf+n, len-n, "%02x:%x\n", 4*i, cfg);
				printk(KERN_WARNING "EEH: PCI-E AER %02x: %08x\n", i, cfg);
			}
		}
	}

	/* Gather status on devices under the bridge */
	if (dev->class >> 16 == PCI_BASE_CLASS_BRIDGE) {
		struct device_node *child;

		for_each_child_of_node(dn, child) {
			if (of_node_to_eeh_dev(child))
				n += eeh_gather_pci_data(of_node_to_eeh_dev(child), buf+n, len-n);
		}
	}

	return n;
}

/**
 * eeh_slot_error_detail - Generate combined log including driver log and error log
 * @edev: device to report error log for
 * @severity: temporary or permanent error log
 *
 * This routine should be called to generate the combined log, which
 * is comprised of driver log and error log. The driver log is figured
 * out from the config space of the corresponding PCI device, while
 * the error log is fetched through platform dependent function call.
 */
void eeh_slot_error_detail(struct eeh_dev *edev, int severity)
{
	size_t loglen = 0;
	pci_regs_buf[0] = 0;

	eeh_pci_enable(edev, EEH_OPT_THAW_MMIO);
	eeh_ops->configure_bridge(eeh_dev_to_of_node(edev));
	eeh_restore_bars(edev);
	loglen = eeh_gather_pci_data(edev, pci_regs_buf, EEH_PCI_REGS_LOG_LEN);

	eeh_ops->get_log(eeh_dev_to_of_node(edev), severity, pci_regs_buf, loglen);
}

/**
 * eeh_token_to_phys - Convert EEH address token to phys address
 * @token: I/O token, should be address in the form 0xA....
 *
 * This routine should be called to convert virtual I/O address
 * to physical one.
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
 * eeh_find_device_pe - Retrieve the PE for the given device
 * @dn: device node
 *
 * Return the PE under which this device lies
 */
struct device_node *eeh_find_device_pe(struct device_node *dn)
{
	while (dn->parent && of_node_to_eeh_dev(dn->parent) &&
	       (of_node_to_eeh_dev(dn->parent)->mode & EEH_MODE_SUPPORTED)) {
		dn = dn->parent;
	}
	return dn;
}

/**
 * __eeh_mark_slot - Mark all child devices as failed
 * @parent: parent device
 * @mode_flag: failure flag
 *
 * Mark all devices that are children of this device as failed.
 * Mark the device driver too, so that it can see the failure
 * immediately; this is critical, since some drivers poll
 * status registers in interrupts ... If a driver is polling,
 * and the slot is frozen, then the driver can deadlock in
 * an interrupt context, which is bad.
 */
static void __eeh_mark_slot(struct device_node *parent, int mode_flag)
{
	struct device_node *dn;

	for_each_child_of_node(parent, dn) {
		if (of_node_to_eeh_dev(dn)) {
			/* Mark the pci device driver too */
			struct pci_dev *dev = of_node_to_eeh_dev(dn)->pdev;

			of_node_to_eeh_dev(dn)->mode |= mode_flag;

			if (dev && dev->driver)
				dev->error_state = pci_channel_io_frozen;

			__eeh_mark_slot(dn, mode_flag);
		}
	}
}

/**
 * eeh_mark_slot - Mark the indicated device and its children as failed
 * @dn: parent device
 * @mode_flag: failure flag
 *
 * Mark the indicated device and its child devices as failed.
 * The device drivers are marked as failed as well.
 */
void eeh_mark_slot(struct device_node *dn, int mode_flag)
{
	struct pci_dev *dev;
	dn = eeh_find_device_pe(dn);

	/* Back up one, since config addrs might be shared */
	if (!pcibios_find_pci_bus(dn) && of_node_to_eeh_dev(dn->parent))
		dn = dn->parent;

	of_node_to_eeh_dev(dn)->mode |= mode_flag;

	/* Mark the pci device too */
	dev = of_node_to_eeh_dev(dn)->pdev;
	if (dev)
		dev->error_state = pci_channel_io_frozen;

	__eeh_mark_slot(dn, mode_flag);
}

/**
 * __eeh_clear_slot - Clear failure flag for the child devices
 * @parent: parent device
 * @mode_flag: flag to be cleared
 *
 * Clear failure flag for the child devices.
 */
static void __eeh_clear_slot(struct device_node *parent, int mode_flag)
{
	struct device_node *dn;

	for_each_child_of_node(parent, dn) {
		if (of_node_to_eeh_dev(dn)) {
			of_node_to_eeh_dev(dn)->mode &= ~mode_flag;
			of_node_to_eeh_dev(dn)->check_count = 0;
			__eeh_clear_slot(dn, mode_flag);
		}
	}
}

/**
 * eeh_clear_slot - Clear failure flag for the indicated device and its children
 * @dn: parent device
 * @mode_flag: flag to be cleared
 *
 * Clear failure flag for the indicated device and its children.
 */
void eeh_clear_slot(struct device_node *dn, int mode_flag)
{
	unsigned long flags;
	raw_spin_lock_irqsave(&confirm_error_lock, flags);
	
	dn = eeh_find_device_pe(dn);
	
	/* Back up one, since config addrs might be shared */
	if (!pcibios_find_pci_bus(dn) && of_node_to_eeh_dev(dn->parent))
		dn = dn->parent;

	of_node_to_eeh_dev(dn)->mode &= ~mode_flag;
	of_node_to_eeh_dev(dn)->check_count = 0;
	__eeh_clear_slot(dn, mode_flag);
	raw_spin_unlock_irqrestore(&confirm_error_lock, flags);
}

/**
 * eeh_dn_check_failure - Check if all 1's data is due to EEH slot freeze
 * @dn: device node
 * @dev: pci device, if known
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
	unsigned long flags;
	struct eeh_dev *edev;
	int rc = 0;
	const char *location;

	eeh_stats.total_mmio_ffs++;

	if (!eeh_subsystem_enabled)
		return 0;

	if (!dn) {
		eeh_stats.no_dn++;
		return 0;
	}
	dn = eeh_find_device_pe(dn);
	edev = of_node_to_eeh_dev(dn);

	/* Access to IO BARs might get this far and still not want checking. */
	if (!(edev->mode & EEH_MODE_SUPPORTED) ||
	    edev->mode & EEH_MODE_NOCHECK) {
		eeh_stats.ignored_check++;
		pr_debug("EEH: Ignored check (%x) for %s %s\n",
			edev->mode, eeh_pci_name(dev), dn->full_name);
		return 0;
	}

	if (!edev->config_addr && !edev->pe_config_addr) {
		eeh_stats.no_cfg_addr++;
		return 0;
	}

	/* If we already have a pending isolation event for this
	 * slot, we know it's bad already, we don't need to check.
	 * Do this checking under a lock; as multiple PCI devices
	 * in one slot might report errors simultaneously, and we
	 * only want one error recovery routine running.
	 */
	raw_spin_lock_irqsave(&confirm_error_lock, flags);
	rc = 1;
	if (edev->mode & EEH_MODE_ISOLATED) {
		edev->check_count++;
		if (edev->check_count % EEH_MAX_FAILS == 0) {
			location = of_get_property(dn, "ibm,loc-code", NULL);
			printk(KERN_ERR "EEH: %d reads ignored for recovering device at "
				"location=%s driver=%s pci addr=%s\n",
				edev->check_count, location,
				eeh_driver_name(dev), eeh_pci_name(dev));
			printk(KERN_ERR "EEH: Might be infinite loop in %s driver\n",
				eeh_driver_name(dev));
			dump_stack();
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
	ret = eeh_ops->get_state(dn, NULL);

	/* Note that config-io to empty slots may fail;
	 * they are empty when they don't have children.
	 * We will punt with the following conditions: Failure to get
	 * PE's state, EEH not support and Permanently unavailable
	 * state, PE is in good state.
	 */
	if ((ret < 0) ||
	    (ret == EEH_STATE_NOT_SUPPORT) ||
	    (ret & (EEH_STATE_MMIO_ACTIVE | EEH_STATE_DMA_ACTIVE)) ==
	    (EEH_STATE_MMIO_ACTIVE | EEH_STATE_DMA_ACTIVE)) {
		eeh_stats.false_positives++;
		edev->false_positives ++;
		rc = 0;
		goto dn_unlock;
	}

	eeh_stats.slot_resets++;
 
	/* Avoid repeated reports of this failure, including problems
	 * with other functions on this device, and functions under
	 * bridges.
	 */
	eeh_mark_slot(dn, EEH_MODE_ISOLATED);
	raw_spin_unlock_irqrestore(&confirm_error_lock, flags);

	eeh_send_failure_event(edev);

	/* Most EEH events are due to device driver bugs.  Having
	 * a stack trace will help the device-driver authors figure
	 * out what happened.  So print that out.
	 */
	WARN(1, "EEH: failure detected\n");
	return 1;

dn_unlock:
	raw_spin_unlock_irqrestore(&confirm_error_lock, flags);
	return rc;
}

EXPORT_SYMBOL_GPL(eeh_dn_check_failure);

/**
 * eeh_check_failure - Check if all 1's data is due to EEH slot freeze
 * @token: I/O token, should be address in the form 0xA....
 * @val: value, should be all 1's (XXX why do we need this arg??)
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
	dev = pci_addr_cache_get_device(addr);
	if (!dev) {
		eeh_stats.no_device++;
		return val;
	}

	dn = pci_device_to_OF_node(dev);
	eeh_dn_check_failure(dn, dev);

	pci_dev_put(dev);
	return val;
}

EXPORT_SYMBOL(eeh_check_failure);


/**
 * eeh_pci_enable - Enable MMIO or DMA transfers for this slot
 * @edev: pci device node
 *
 * This routine should be called to reenable frozen MMIO or DMA
 * so that it would work correctly again. It's useful while doing
 * recovery or log collection on the indicated device.
 */
int eeh_pci_enable(struct eeh_dev *edev, int function)
{
	int rc;
	struct device_node *dn = eeh_dev_to_of_node(edev);

	rc = eeh_ops->set_option(dn, function);
	if (rc)
		printk(KERN_WARNING "EEH: Unexpected state change %d, err=%d dn=%s\n",
		        function, rc, dn->full_name);

	rc = eeh_ops->wait_state(dn, PCI_BUS_RESET_WAIT_MSEC);
	if (rc > 0 && (rc & EEH_STATE_MMIO_ENABLED) &&
	   (function == EEH_OPT_THAW_MMIO))
		return 0;

	return rc;
}

/**
 * pcibios_set_pcie_slot_reset - Set PCI-E reset state
 * @dev: pci device struct
 * @state: reset state to enter
 *
 * Return value:
 * 	0 if success
 */
int pcibios_set_pcie_reset_state(struct pci_dev *dev, enum pcie_reset_state state)
{
	struct device_node *dn = pci_device_to_OF_node(dev);

	switch (state) {
	case pcie_deassert_reset:
		eeh_ops->reset(dn, EEH_RESET_DEACTIVATE);
		break;
	case pcie_hot_reset:
		eeh_ops->reset(dn, EEH_RESET_HOT);
		break;
	case pcie_warm_reset:
		eeh_ops->reset(dn, EEH_RESET_FUNDAMENTAL);
		break;
	default:
		return -EINVAL;
	};

	return 0;
}

/**
 * __eeh_set_pe_freset - Check the required reset for child devices
 * @parent: parent device
 * @freset: return value
 *
 * Each device might have its preferred reset type: fundamental or
 * hot reset. The routine is used to collect the information from
 * the child devices so that they could be reset accordingly.
 */
void __eeh_set_pe_freset(struct device_node *parent, unsigned int *freset)
{
	struct device_node *dn;

	for_each_child_of_node(parent, dn) {
		if (of_node_to_eeh_dev(dn)) {
			struct pci_dev *dev = of_node_to_eeh_dev(dn)->pdev;

			if (dev && dev->driver)
				*freset |= dev->needs_freset;

			__eeh_set_pe_freset(dn, freset);
		}
	}
}

/**
 * eeh_set_pe_freset - Check the required reset for the indicated device and its children
 * @dn: parent device
 * @freset: return value
 *
 * Each device might have its preferred reset type: fundamental or
 * hot reset. The routine is used to collected the information for
 * the indicated device and its children so that the bunch of the
 * devices could be reset properly.
 */
void eeh_set_pe_freset(struct device_node *dn, unsigned int *freset)
{
	struct pci_dev *dev;
	dn = eeh_find_device_pe(dn);

	/* Back up one, since config addrs might be shared */
	if (!pcibios_find_pci_bus(dn) && of_node_to_eeh_dev(dn->parent))
		dn = dn->parent;

	dev = of_node_to_eeh_dev(dn)->pdev;
	if (dev)
		*freset |= dev->needs_freset;

	__eeh_set_pe_freset(dn, freset);
}

/**
 * eeh_reset_pe_once - Assert the pci #RST line for 1/4 second
 * @edev: pci device node to be reset.
 *
 * Assert the PCI #RST line for 1/4 second.
 */
static void eeh_reset_pe_once(struct eeh_dev *edev)
{
	unsigned int freset = 0;
	struct device_node *dn = eeh_dev_to_of_node(edev);

	/* Determine type of EEH reset required for
	 * Partitionable Endpoint, a hot-reset (1)
	 * or a fundamental reset (3).
	 * A fundamental reset required by any device under
	 * Partitionable Endpoint trumps hot-reset.
  	 */
	eeh_set_pe_freset(dn, &freset);

	if (freset)
		eeh_ops->reset(dn, EEH_RESET_FUNDAMENTAL);
	else
		eeh_ops->reset(dn, EEH_RESET_HOT);

	/* The PCI bus requires that the reset be held high for at least
	 * a 100 milliseconds. We wait a bit longer 'just in case'.
	 */
#define PCI_BUS_RST_HOLD_TIME_MSEC 250
	msleep(PCI_BUS_RST_HOLD_TIME_MSEC);
	
	/* We might get hit with another EEH freeze as soon as the 
	 * pci slot reset line is dropped. Make sure we don't miss
	 * these, and clear the flag now.
	 */
	eeh_clear_slot(dn, EEH_MODE_ISOLATED);

	eeh_ops->reset(dn, EEH_RESET_DEACTIVATE);

	/* After a PCI slot has been reset, the PCI Express spec requires
	 * a 1.5 second idle time for the bus to stabilize, before starting
	 * up traffic.
	 */
#define PCI_BUS_SETTLE_TIME_MSEC 1800
	msleep(PCI_BUS_SETTLE_TIME_MSEC);
}

/**
 * eeh_reset_pe - Reset the indicated PE
 * @edev: PCI device associated EEH device
 *
 * This routine should be called to reset indicated device, including
 * PE. A PE might include multiple PCI devices and sometimes PCI bridges
 * might be involved as well.
 */
int eeh_reset_pe(struct eeh_dev *edev)
{
	int i, rc;
	struct device_node *dn = eeh_dev_to_of_node(edev);

	/* Take three shots at resetting the bus */
	for (i=0; i<3; i++) {
		eeh_reset_pe_once(edev);

		rc = eeh_ops->wait_state(dn, PCI_BUS_RESET_WAIT_MSEC);
		if (rc == (EEH_STATE_MMIO_ACTIVE | EEH_STATE_DMA_ACTIVE))
			return 0;

		if (rc < 0) {
			printk(KERN_ERR "EEH: unrecoverable slot failure %s\n",
			       dn->full_name);
			return -1;
		}
		printk(KERN_ERR "EEH: bus reset %d failed on slot %s, rc=%d\n",
		       i+1, dn->full_name, rc);
	}

	return -1;
}

/** Save and restore of PCI BARs
 *
 * Although firmware will set up BARs during boot, it doesn't
 * set up device BAR's after a device reset, although it will,
 * if requested, set up bridge configuration. Thus, we need to
 * configure the PCI devices ourselves.  
 */

/**
 * eeh_restore_one_device_bars - Restore the Base Address Registers for one device
 * @edev: PCI device associated EEH device
 *
 * Loads the PCI configuration space base address registers,
 * the expansion ROM base address, the latency timer, and etc.
 * from the saved values in the device node.
 */
static inline void eeh_restore_one_device_bars(struct eeh_dev *edev)
{
	int i;
	u32 cmd;
	struct device_node *dn = eeh_dev_to_of_node(edev);

	if (!edev->phb)
		return;

	for (i=4; i<10; i++) {
		eeh_ops->write_config(dn, i*4, 4, edev->config_space[i]);
	}

	/* 12 == Expansion ROM Address */
	eeh_ops->write_config(dn, 12*4, 4, edev->config_space[12]);

#define BYTE_SWAP(OFF) (8*((OFF)/4)+3-(OFF))
#define SAVED_BYTE(OFF) (((u8 *)(edev->config_space))[BYTE_SWAP(OFF)])

	eeh_ops->write_config(dn, PCI_CACHE_LINE_SIZE, 1,
	            SAVED_BYTE(PCI_CACHE_LINE_SIZE));

	eeh_ops->write_config(dn, PCI_LATENCY_TIMER, 1,
	            SAVED_BYTE(PCI_LATENCY_TIMER));

	/* max latency, min grant, interrupt pin and line */
	eeh_ops->write_config(dn, 15*4, 4, edev->config_space[15]);

	/* Restore PERR & SERR bits, some devices require it,
	 * don't touch the other command bits
	 */
	eeh_ops->read_config(dn, PCI_COMMAND, 4, &cmd);
	if (edev->config_space[1] & PCI_COMMAND_PARITY)
		cmd |= PCI_COMMAND_PARITY;
	else
		cmd &= ~PCI_COMMAND_PARITY;
	if (edev->config_space[1] & PCI_COMMAND_SERR)
		cmd |= PCI_COMMAND_SERR;
	else
		cmd &= ~PCI_COMMAND_SERR;
	eeh_ops->write_config(dn, PCI_COMMAND, 4, cmd);
}

/**
 * eeh_restore_bars - Restore the PCI config space info
 * @edev: EEH device
 *
 * This routine performs a recursive walk to the children
 * of this device as well.
 */
void eeh_restore_bars(struct eeh_dev *edev)
{
	struct device_node *dn;
	if (!edev)
		return;
	
	if ((edev->mode & EEH_MODE_SUPPORTED) && !IS_BRIDGE(edev->class_code))
		eeh_restore_one_device_bars(edev);

	for_each_child_of_node(eeh_dev_to_of_node(edev), dn)
		eeh_restore_bars(of_node_to_eeh_dev(dn));
}

/**
 * eeh_save_bars - Save device bars
 * @edev: PCI device associated EEH device
 *
 * Save the values of the device bars. Unlike the restore
 * routine, this routine is *not* recursive. This is because
 * PCI devices are added individually; but, for the restore,
 * an entire slot is reset at a time.
 */
static void eeh_save_bars(struct eeh_dev *edev)
{
	int i;
	struct device_node *dn;

	if (!edev)
		return;
	dn = eeh_dev_to_of_node(edev);
	
	for (i = 0; i < 16; i++)
		eeh_ops->read_config(dn, i * 4, 4, &edev->config_space[i]);
}

/**
 * eeh_early_enable - Early enable EEH on the indicated device
 * @dn: device node
 * @data: BUID
 *
 * Enable EEH functionality on the specified PCI device. The function
 * is expected to be called before real PCI probing is done. However,
 * the PHBs have been initialized at this point.
 */
static void *eeh_early_enable(struct device_node *dn, void *data)
{
	int ret;
	const u32 *class_code = of_get_property(dn, "class-code", NULL);
	const u32 *vendor_id = of_get_property(dn, "vendor-id", NULL);
	const u32 *device_id = of_get_property(dn, "device-id", NULL);
	const u32 *regs;
	int enable;
	struct eeh_dev *edev = of_node_to_eeh_dev(dn);

	edev->class_code = 0;
	edev->mode = 0;
	edev->check_count = 0;
	edev->freeze_count = 0;
	edev->false_positives = 0;

	if (!of_device_is_available(dn))
		return NULL;

	/* Ignore bad nodes. */
	if (!class_code || !vendor_id || !device_id)
		return NULL;

	/* There is nothing to check on PCI to ISA bridges */
	if (dn->type && !strcmp(dn->type, "isa")) {
		edev->mode |= EEH_MODE_NOCHECK;
		return NULL;
	}
	edev->class_code = *class_code;

	/* Ok... see if this device supports EEH.  Some do, some don't,
	 * and the only way to find out is to check each and every one.
	 */
	regs = of_get_property(dn, "reg", NULL);
	if (regs) {
		/* First register entry is addr (00BBSS00)  */
		/* Try to enable eeh */
		ret = eeh_ops->set_option(dn, EEH_OPT_ENABLE);

		enable = 0;
		if (ret == 0) {
			edev->config_addr = regs[0];

			/* If the newer, better, ibm,get-config-addr-info is supported, 
			 * then use that instead.
			 */
			edev->pe_config_addr = eeh_ops->get_pe_addr(dn);

			/* Some older systems (Power4) allow the
			 * ibm,set-eeh-option call to succeed even on nodes
			 * where EEH is not supported. Verify support
			 * explicitly.
			 */
			ret = eeh_ops->get_state(dn, NULL);
			if (ret > 0 && ret != EEH_STATE_NOT_SUPPORT)
				enable = 1;
		}

		if (enable) {
			eeh_subsystem_enabled = 1;
			edev->mode |= EEH_MODE_SUPPORTED;

			pr_debug("EEH: %s: eeh enabled, config=%x pe_config=%x\n",
				 dn->full_name, edev->config_addr,
				 edev->pe_config_addr);
		} else {

			/* This device doesn't support EEH, but it may have an
			 * EEH parent, in which case we mark it as supported.
			 */
			if (dn->parent && of_node_to_eeh_dev(dn->parent) &&
			    (of_node_to_eeh_dev(dn->parent)->mode & EEH_MODE_SUPPORTED)) {
				/* Parent supports EEH. */
				edev->mode |= EEH_MODE_SUPPORTED;
				edev->config_addr = of_node_to_eeh_dev(dn->parent)->config_addr;
				return NULL;
			}
		}
	} else {
		printk(KERN_WARNING "EEH: %s: unable to get reg property.\n",
		       dn->full_name);
	}

	eeh_save_bars(edev);
	return NULL;
}

/**
 * eeh_ops_register - Register platform dependent EEH operations
 * @ops: platform dependent EEH operations
 *
 * Register the platform dependent EEH operation callback
 * functions. The platform should call this function before
 * any other EEH operations.
 */
int __init eeh_ops_register(struct eeh_ops *ops)
{
	if (!ops->name) {
		pr_warning("%s: Invalid EEH ops name for %p\n",
			__func__, ops);
		return -EINVAL;
	}

	if (eeh_ops && eeh_ops != ops) {
		pr_warning("%s: EEH ops of platform %s already existing (%s)\n",
			__func__, eeh_ops->name, ops->name);
		return -EEXIST;
	}

	eeh_ops = ops;

	return 0;
}

/**
 * eeh_ops_unregister - Unreigster platform dependent EEH operations
 * @name: name of EEH platform operations
 *
 * Unregister the platform dependent EEH operation callback
 * functions.
 */
int __exit eeh_ops_unregister(const char *name)
{
	if (!name || !strlen(name)) {
		pr_warning("%s: Invalid EEH ops name\n",
			__func__);
		return -EINVAL;
	}

	if (eeh_ops && !strcmp(eeh_ops->name, name)) {
		eeh_ops = NULL;
		return 0;
	}

	return -EEXIST;
}

/**
 * eeh_init - EEH initialization
 *
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
static int __init eeh_init(void)
{
	struct pci_controller *hose, *tmp;
	struct device_node *phb;
	int ret;

	/* call platform initialization function */
	if (!eeh_ops) {
		pr_warning("%s: Platform EEH operation not found\n",
			__func__);
		return -EEXIST;
	} else if ((ret = eeh_ops->init())) {
		pr_warning("%s: Failed to call platform init function (%d)\n",
			__func__, ret);
		return ret;
	}

	raw_spin_lock_init(&confirm_error_lock);

	/* Enable EEH for all adapters */
	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		phb = hose->dn;
		traverse_pci_devices(phb, eeh_early_enable, NULL);
	}

	if (eeh_subsystem_enabled)
		printk(KERN_INFO "EEH: PCI Enhanced I/O Error Handling Enabled\n");
	else
		printk(KERN_WARNING "EEH: No capable adapters found\n");

	return ret;
}

core_initcall_sync(eeh_init);

/**
 * eeh_add_device_early - Enable EEH for the indicated device_node
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
static void eeh_add_device_early(struct device_node *dn)
{
	struct pci_controller *phb;

	if (!dn || !of_node_to_eeh_dev(dn))
		return;
	phb = of_node_to_eeh_dev(dn)->phb;

	/* USB Bus children of PCI devices will not have BUID's */
	if (NULL == phb || 0 == phb->buid)
		return;

	eeh_early_enable(dn, NULL);
}

/**
 * eeh_add_device_tree_early - Enable EEH for the indicated device
 * @dn: device node
 *
 * This routine must be used to perform EEH initialization for the
 * indicated PCI device that was added after system boot (e.g.
 * hotplug, dlpar).
 */
void eeh_add_device_tree_early(struct device_node *dn)
{
	struct device_node *sib;

	for_each_child_of_node(dn, sib)
		eeh_add_device_tree_early(sib);
	eeh_add_device_early(dn);
}
EXPORT_SYMBOL_GPL(eeh_add_device_tree_early);

/**
 * eeh_add_device_late - Perform EEH initialization for the indicated pci device
 * @dev: pci device for which to set up EEH
 *
 * This routine must be used to complete EEH initialization for PCI
 * devices that were added after system boot (e.g. hotplug, dlpar).
 */
static void eeh_add_device_late(struct pci_dev *dev)
{
	struct device_node *dn;
	struct eeh_dev *edev;

	if (!dev || !eeh_subsystem_enabled)
		return;

	pr_debug("EEH: Adding device %s\n", pci_name(dev));

	dn = pci_device_to_OF_node(dev);
	edev = of_node_to_eeh_dev(dn);
	if (edev->pdev == dev) {
		pr_debug("EEH: Already referenced !\n");
		return;
	}
	WARN_ON(edev->pdev);

	pci_dev_get(dev);
	edev->pdev = dev;
	dev->dev.archdata.edev = edev;

	pci_addr_cache_insert_device(dev);
	eeh_sysfs_add_device(dev);
}

/**
 * eeh_add_device_tree_late - Perform EEH initialization for the indicated PCI bus
 * @bus: PCI bus
 *
 * This routine must be used to perform EEH initialization for PCI
 * devices which are attached to the indicated PCI bus. The PCI bus
 * is added after system boot through hotplug or dlpar.
 */
void eeh_add_device_tree_late(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
 		eeh_add_device_late(dev);
 		if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
 			struct pci_bus *subbus = dev->subordinate;
 			if (subbus)
 				eeh_add_device_tree_late(subbus);
 		}
	}
}
EXPORT_SYMBOL_GPL(eeh_add_device_tree_late);

/**
 * eeh_remove_device - Undo EEH setup for the indicated pci device
 * @dev: pci device to be removed
 *
 * This routine should be called when a device is removed from
 * a running system (e.g. by hotplug or dlpar).  It unregisters
 * the PCI device from the EEH subsystem.  I/O errors affecting
 * this device will no longer be detected after this call; thus,
 * i/o errors affecting this slot may leave this device unusable.
 */
static void eeh_remove_device(struct pci_dev *dev)
{
	struct eeh_dev *edev;

	if (!dev || !eeh_subsystem_enabled)
		return;
	edev = pci_dev_to_eeh_dev(dev);

	/* Unregister the device with the EEH/PCI address search system */
	pr_debug("EEH: Removing device %s\n", pci_name(dev));

	if (!edev || !edev->pdev) {
		pr_debug("EEH: Not referenced !\n");
		return;
	}
	edev->pdev = NULL;
	dev->dev.archdata.edev = NULL;
	pci_dev_put(dev);

	pci_addr_cache_remove_device(dev);
	eeh_sysfs_remove_device(dev);
}

/**
 * eeh_remove_bus_device - Undo EEH setup for the indicated PCI device
 * @dev: PCI device
 *
 * This routine must be called when a device is removed from the
 * running system through hotplug or dlpar. The corresponding
 * PCI address cache will be removed.
 */
void eeh_remove_bus_device(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->subordinate;
	struct pci_dev *child, *tmp;

	eeh_remove_device(dev);

	if (bus && dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
		list_for_each_entry_safe(child, tmp, &bus->devices, bus_list)
			 eeh_remove_bus_device(child);
	}
}
EXPORT_SYMBOL_GPL(eeh_remove_bus_device);

static int proc_eeh_show(struct seq_file *m, void *v)
{
	if (0 == eeh_subsystem_enabled) {
		seq_printf(m, "EEH Subsystem is globally disabled\n");
		seq_printf(m, "eeh_total_mmio_ffs=%llu\n", eeh_stats.total_mmio_ffs);
	} else {
		seq_printf(m, "EEH Subsystem is enabled\n");
		seq_printf(m,
				"no device=%llu\n"
				"no device node=%llu\n"
				"no config address=%llu\n"
				"check not wanted=%llu\n"
				"eeh_total_mmio_ffs=%llu\n"
				"eeh_false_positives=%llu\n"
				"eeh_slot_resets=%llu\n",
				eeh_stats.no_device,
				eeh_stats.no_dn,
				eeh_stats.no_cfg_addr,
				eeh_stats.ignored_check,
				eeh_stats.total_mmio_ffs,
				eeh_stats.false_positives,
				eeh_stats.slot_resets);
	}

	return 0;
}

static int proc_eeh_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_eeh_show, NULL);
}

static const struct file_operations proc_eeh_operations = {
	.open      = proc_eeh_open,
	.read      = seq_read,
	.llseek    = seq_lseek,
	.release   = single_release,
};

static int __init eeh_init_proc(void)
{
	if (machine_is(pseries))
		proc_create("powerpc/eeh", 0, NULL, &proc_eeh_operations);
	return 0;
}
__initcall(eeh_init_proc);
