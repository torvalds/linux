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

/*
 * EEH probe mode support. The intention is to support multiple
 * platforms for EEH. Some platforms like pSeries do PCI emunation
 * based on device tree. However, other platforms like powernv probe
 * PCI devices from hardware. The flag is used to distinguish that.
 * In addition, struct eeh_ops::probe would be invoked for particular
 * OF node or PCI device so that the corresponding PE would be created
 * there.
 */
int eeh_probe_mode;

/* Lock to avoid races due to multiple reports of an error */
DEFINE_RAW_SPINLOCK(confirm_error_lock);

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

	return n;
}

/**
 * eeh_slot_error_detail - Generate combined log including driver log and error log
 * @pe: EEH PE
 * @severity: temporary or permanent error log
 *
 * This routine should be called to generate the combined log, which
 * is comprised of driver log and error log. The driver log is figured
 * out from the config space of the corresponding PCI device, while
 * the error log is fetched through platform dependent function call.
 */
void eeh_slot_error_detail(struct eeh_pe *pe, int severity)
{
	size_t loglen = 0;
	struct eeh_dev *edev, *tmp;
	bool valid_cfg_log = true;

	/*
	 * When the PHB is fenced or dead, it's pointless to collect
	 * the data from PCI config space because it should return
	 * 0xFF's. For ER, we still retrieve the data from the PCI
	 * config space.
	 */
	if (eeh_probe_mode_dev() &&
	    (pe->type & EEH_PE_PHB) &&
	    (pe->state & (EEH_PE_ISOLATED | EEH_PE_PHB_DEAD)))
		valid_cfg_log = false;

	if (valid_cfg_log) {
		eeh_pci_enable(pe, EEH_OPT_THAW_MMIO);
		eeh_ops->configure_bridge(pe);
		eeh_pe_restore_bars(pe);

		pci_regs_buf[0] = 0;
		eeh_pe_for_each_dev(pe, edev, tmp) {
			loglen += eeh_gather_pci_data(edev, pci_regs_buf + loglen,
						      EEH_PCI_REGS_LOG_LEN - loglen);
		}
	}

	eeh_ops->get_log(pe, severity, pci_regs_buf, loglen);
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
	int hugepage_shift;

	/*
	 * We won't find hugepages here, iomem
	 */
	ptep = find_linux_pte_or_hugepte(init_mm.pgd, token, &hugepage_shift);
	if (!ptep)
		return token;
	WARN_ON(hugepage_shift);
	pa = pte_pfn(*ptep) << PAGE_SHIFT;

	return pa | (token & (PAGE_SIZE-1));
}

/*
 * On PowerNV platform, we might already have fenced PHB there.
 * For that case, it's meaningless to recover frozen PE. Intead,
 * We have to handle fenced PHB firstly.
 */
static int eeh_phb_check_failure(struct eeh_pe *pe)
{
	struct eeh_pe *phb_pe;
	unsigned long flags;
	int ret;

	if (!eeh_probe_mode_dev())
		return -EPERM;

	/* Find the PHB PE */
	phb_pe = eeh_phb_pe_get(pe->phb);
	if (!phb_pe) {
		pr_warning("%s Can't find PE for PHB#%d\n",
			   __func__, pe->phb->global_number);
		return -EEXIST;
	}

	/* If the PHB has been in problematic state */
	eeh_serialize_lock(&flags);
	if (phb_pe->state & (EEH_PE_ISOLATED | EEH_PE_PHB_DEAD)) {
		ret = 0;
		goto out;
	}

	/* Check PHB state */
	ret = eeh_ops->get_state(phb_pe, NULL);
	if ((ret < 0) ||
	    (ret == EEH_STATE_NOT_SUPPORT) ||
	    (ret & (EEH_STATE_MMIO_ACTIVE | EEH_STATE_DMA_ACTIVE)) ==
	    (EEH_STATE_MMIO_ACTIVE | EEH_STATE_DMA_ACTIVE)) {
		ret = 0;
		goto out;
	}

	/* Isolate the PHB and send event */
	eeh_pe_state_mark(phb_pe, EEH_PE_ISOLATED);
	eeh_serialize_unlock(flags);

	pr_err("EEH: PHB#%x failure detected\n",
		phb_pe->phb->global_number);
	dump_stack();
	eeh_send_failure_event(phb_pe);

	return 1;
out:
	eeh_serialize_unlock(flags);
	return ret;
}

/**
 * eeh_dev_check_failure - Check if all 1's data is due to EEH slot freeze
 * @edev: eeh device
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
int eeh_dev_check_failure(struct eeh_dev *edev)
{
	int ret;
	unsigned long flags;
	struct device_node *dn;
	struct pci_dev *dev;
	struct eeh_pe *pe;
	int rc = 0;
	const char *location;

	eeh_stats.total_mmio_ffs++;

	if (!eeh_subsystem_enabled)
		return 0;

	if (!edev) {
		eeh_stats.no_dn++;
		return 0;
	}
	dn = eeh_dev_to_of_node(edev);
	dev = eeh_dev_to_pci_dev(edev);
	pe = edev->pe;

	/* Access to IO BARs might get this far and still not want checking. */
	if (!pe) {
		eeh_stats.ignored_check++;
		pr_debug("EEH: Ignored check for %s %s\n",
			eeh_pci_name(dev), dn->full_name);
		return 0;
	}

	if (!pe->addr && !pe->config_addr) {
		eeh_stats.no_cfg_addr++;
		return 0;
	}

	/*
	 * On PowerNV platform, we might already have fenced PHB
	 * there and we need take care of that firstly.
	 */
	ret = eeh_phb_check_failure(pe);
	if (ret > 0)
		return ret;

	/* If we already have a pending isolation event for this
	 * slot, we know it's bad already, we don't need to check.
	 * Do this checking under a lock; as multiple PCI devices
	 * in one slot might report errors simultaneously, and we
	 * only want one error recovery routine running.
	 */
	eeh_serialize_lock(&flags);
	rc = 1;
	if (pe->state & EEH_PE_ISOLATED) {
		pe->check_count++;
		if (pe->check_count % EEH_MAX_FAILS == 0) {
			location = of_get_property(dn, "ibm,loc-code", NULL);
			printk(KERN_ERR "EEH: %d reads ignored for recovering device at "
				"location=%s driver=%s pci addr=%s\n",
				pe->check_count, location,
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
	ret = eeh_ops->get_state(pe, NULL);

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
		pe->false_positives++;
		rc = 0;
		goto dn_unlock;
	}

	eeh_stats.slot_resets++;

	/* Avoid repeated reports of this failure, including problems
	 * with other functions on this device, and functions under
	 * bridges.
	 */
	eeh_pe_state_mark(pe, EEH_PE_ISOLATED);
	eeh_serialize_unlock(flags);

	/* Most EEH events are due to device driver bugs.  Having
	 * a stack trace will help the device-driver authors figure
	 * out what happened.  So print that out.
	 */
	pr_err("EEH: Frozen PE#%x detected on PHB#%x\n",
		pe->addr, pe->phb->global_number);
	dump_stack();

	eeh_send_failure_event(pe);

	return 1;

dn_unlock:
	eeh_serialize_unlock(flags);
	return rc;
}

EXPORT_SYMBOL_GPL(eeh_dev_check_failure);

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
	struct eeh_dev *edev;

	/* Finding the phys addr + pci device; this is pretty quick. */
	addr = eeh_token_to_phys((unsigned long __force) token);
	edev = eeh_addr_cache_get_dev(addr);
	if (!edev) {
		eeh_stats.no_device++;
		return val;
	}

	eeh_dev_check_failure(edev);
	return val;
}

EXPORT_SYMBOL(eeh_check_failure);


/**
 * eeh_pci_enable - Enable MMIO or DMA transfers for this slot
 * @pe: EEH PE
 *
 * This routine should be called to reenable frozen MMIO or DMA
 * so that it would work correctly again. It's useful while doing
 * recovery or log collection on the indicated device.
 */
int eeh_pci_enable(struct eeh_pe *pe, int function)
{
	int rc;

	rc = eeh_ops->set_option(pe, function);
	if (rc)
		pr_warning("%s: Unexpected state change %d on PHB#%d-PE#%x, err=%d\n",
			__func__, function, pe->phb->global_number, pe->addr, rc);

	rc = eeh_ops->wait_state(pe, PCI_BUS_RESET_WAIT_MSEC);
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
	struct eeh_dev *edev = pci_dev_to_eeh_dev(dev);
	struct eeh_pe *pe = edev->pe;

	if (!pe) {
		pr_err("%s: No PE found on PCI device %s\n",
			__func__, pci_name(dev));
		return -EINVAL;
	}

	switch (state) {
	case pcie_deassert_reset:
		eeh_ops->reset(pe, EEH_RESET_DEACTIVATE);
		break;
	case pcie_hot_reset:
		eeh_ops->reset(pe, EEH_RESET_HOT);
		break;
	case pcie_warm_reset:
		eeh_ops->reset(pe, EEH_RESET_FUNDAMENTAL);
		break;
	default:
		return -EINVAL;
	};

	return 0;
}

/**
 * eeh_set_pe_freset - Check the required reset for the indicated device
 * @data: EEH device
 * @flag: return value
 *
 * Each device might have its preferred reset type: fundamental or
 * hot reset. The routine is used to collected the information for
 * the indicated device and its children so that the bunch of the
 * devices could be reset properly.
 */
static void *eeh_set_dev_freset(void *data, void *flag)
{
	struct pci_dev *dev;
	unsigned int *freset = (unsigned int *)flag;
	struct eeh_dev *edev = (struct eeh_dev *)data;

	dev = eeh_dev_to_pci_dev(edev);
	if (dev)
		*freset |= dev->needs_freset;

	return NULL;
}

/**
 * eeh_reset_pe_once - Assert the pci #RST line for 1/4 second
 * @pe: EEH PE
 *
 * Assert the PCI #RST line for 1/4 second.
 */
static void eeh_reset_pe_once(struct eeh_pe *pe)
{
	unsigned int freset = 0;

	/* Determine type of EEH reset required for
	 * Partitionable Endpoint, a hot-reset (1)
	 * or a fundamental reset (3).
	 * A fundamental reset required by any device under
	 * Partitionable Endpoint trumps hot-reset.
	 */
	eeh_pe_dev_traverse(pe, eeh_set_dev_freset, &freset);

	if (freset)
		eeh_ops->reset(pe, EEH_RESET_FUNDAMENTAL);
	else
		eeh_ops->reset(pe, EEH_RESET_HOT);

	/* The PCI bus requires that the reset be held high for at least
	 * a 100 milliseconds. We wait a bit longer 'just in case'.
	 */
#define PCI_BUS_RST_HOLD_TIME_MSEC 250
	msleep(PCI_BUS_RST_HOLD_TIME_MSEC);

	/* We might get hit with another EEH freeze as soon as the
	 * pci slot reset line is dropped. Make sure we don't miss
	 * these, and clear the flag now.
	 */
	eeh_pe_state_clear(pe, EEH_PE_ISOLATED);

	eeh_ops->reset(pe, EEH_RESET_DEACTIVATE);

	/* After a PCI slot has been reset, the PCI Express spec requires
	 * a 1.5 second idle time for the bus to stabilize, before starting
	 * up traffic.
	 */
#define PCI_BUS_SETTLE_TIME_MSEC 1800
	msleep(PCI_BUS_SETTLE_TIME_MSEC);
}

/**
 * eeh_reset_pe - Reset the indicated PE
 * @pe: EEH PE
 *
 * This routine should be called to reset indicated device, including
 * PE. A PE might include multiple PCI devices and sometimes PCI bridges
 * might be involved as well.
 */
int eeh_reset_pe(struct eeh_pe *pe)
{
	int flags = (EEH_STATE_MMIO_ACTIVE | EEH_STATE_DMA_ACTIVE);
	int i, rc;

	/* Take three shots at resetting the bus */
	for (i=0; i<3; i++) {
		eeh_reset_pe_once(pe);

		rc = eeh_ops->wait_state(pe, PCI_BUS_RESET_WAIT_MSEC);
		if ((rc & flags) == flags)
			return 0;

		if (rc < 0) {
			pr_err("%s: Unrecoverable slot failure on PHB#%d-PE#%x",
				__func__, pe->phb->global_number, pe->addr);
			return -1;
		}
		pr_err("EEH: bus reset %d failed on PHB#%d-PE#%x, rc=%d\n",
			i+1, pe->phb->global_number, pe->addr, rc);
	}

	return -1;
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
void eeh_save_bars(struct eeh_dev *edev)
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
int eeh_init(void)
{
	struct pci_controller *hose, *tmp;
	struct device_node *phb;
	static int cnt = 0;
	int ret = 0;

	/*
	 * We have to delay the initialization on PowerNV after
	 * the PCI hierarchy tree has been built because the PEs
	 * are figured out based on PCI devices instead of device
	 * tree nodes
	 */
	if (machine_is(powernv) && cnt++ <= 0)
		return ret;

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

	/* Initialize EEH event */
	ret = eeh_event_init();
	if (ret)
		return ret;

	/* Enable EEH for all adapters */
	if (eeh_probe_mode_devtree()) {
		list_for_each_entry_safe(hose, tmp,
			&hose_list, list_node) {
			phb = hose->dn;
			traverse_pci_devices(phb, eeh_ops->of_probe, NULL);
		}
	} else if (eeh_probe_mode_dev()) {
		list_for_each_entry_safe(hose, tmp,
			&hose_list, list_node)
			pci_walk_bus(hose->bus, eeh_ops->dev_probe, NULL);
	} else {
		pr_warning("%s: Invalid probe mode %d\n",
			   __func__, eeh_probe_mode);
		return -EINVAL;
	}

	/*
	 * Call platform post-initialization. Actually, It's good chance
	 * to inform platform that EEH is ready to supply service if the
	 * I/O cache stuff has been built up.
	 */
	if (eeh_ops->post_init) {
		ret = eeh_ops->post_init();
		if (ret)
			return ret;
	}

	if (eeh_subsystem_enabled)
		pr_info("EEH: PCI Enhanced I/O Error Handling Enabled\n");
	else
		pr_warning("EEH: No capable adapters found\n");

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
void eeh_add_device_early(struct device_node *dn)
{
	struct pci_controller *phb;

	/*
	 * If we're doing EEH probe based on PCI device, we
	 * would delay the probe until late stage because
	 * the PCI device isn't available this moment.
	 */
	if (!eeh_probe_mode_devtree())
		return;

	if (!of_node_to_eeh_dev(dn))
		return;
	phb = of_node_to_eeh_dev(dn)->phb;

	/* USB Bus children of PCI devices will not have BUID's */
	if (NULL == phb || 0 == phb->buid)
		return;

	eeh_ops->of_probe(dn, NULL);
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
void eeh_add_device_late(struct pci_dev *dev)
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

	/*
	 * The EEH cache might not be removed correctly because of
	 * unbalanced kref to the device during unplug time, which
	 * relies on pcibios_release_device(). So we have to remove
	 * that here explicitly.
	 */
	if (edev->pdev) {
		eeh_rmv_from_parent_pe(edev);
		eeh_addr_cache_rmv_dev(edev->pdev);
		eeh_sysfs_remove_device(edev->pdev);
		edev->mode &= ~EEH_DEV_SYSFS;

		edev->pdev = NULL;
		dev->dev.archdata.edev = NULL;
	}

	edev->pdev = dev;
	dev->dev.archdata.edev = edev;

	/*
	 * We have to do the EEH probe here because the PCI device
	 * hasn't been created yet in the early stage.
	 */
	if (eeh_probe_mode_dev())
		eeh_ops->dev_probe(dev, NULL);

	eeh_addr_cache_insert_dev(dev);
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
 * eeh_add_sysfs_files - Add EEH sysfs files for the indicated PCI bus
 * @bus: PCI bus
 *
 * This routine must be used to add EEH sysfs files for PCI
 * devices which are attached to the indicated PCI bus. The PCI bus
 * is added after system boot through hotplug or dlpar.
 */
void eeh_add_sysfs_files(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		eeh_sysfs_add_device(dev);
		if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
			struct pci_bus *subbus = dev->subordinate;
			if (subbus)
				eeh_add_sysfs_files(subbus);
		}
	}
}
EXPORT_SYMBOL_GPL(eeh_add_sysfs_files);

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
void eeh_remove_device(struct pci_dev *dev)
{
	struct eeh_dev *edev;

	if (!dev || !eeh_subsystem_enabled)
		return;
	edev = pci_dev_to_eeh_dev(dev);

	/* Unregister the device with the EEH/PCI address search system */
	pr_debug("EEH: Removing device %s\n", pci_name(dev));

	if (!edev || !edev->pdev || !edev->pe) {
		pr_debug("EEH: Not referenced !\n");
		return;
	}

	/*
	 * During the hotplug for EEH error recovery, we need the EEH
	 * device attached to the parent PE in order for BAR restore
	 * a bit later. So we keep it for BAR restore and remove it
	 * from the parent PE during the BAR resotre.
	 */
	edev->pdev = NULL;
	dev->dev.archdata.edev = NULL;
	if (!(edev->pe->state & EEH_PE_KEEP))
		eeh_rmv_from_parent_pe(edev);
	else
		edev->mode |= EEH_DEV_DISCONNECTED;

	eeh_addr_cache_rmv_dev(dev);
	eeh_sysfs_remove_device(dev);
	edev->mode &= ~EEH_DEV_SYSFS;
}

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
	if (machine_is(pseries) || machine_is(powernv))
		proc_create("powerpc/eeh", 0, NULL, &proc_eeh_operations);
	return 0;
}
__initcall(eeh_init_proc);
