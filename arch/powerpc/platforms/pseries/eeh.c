/*
 * eeh.c
 * Copyright IBM Corporation 2001, 2005, 2006
 * Copyright Dave Engebretsen & Todd Inglett 2001
 * Copyright Linas Vepstas 2005, 2006
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
#include <linux/init.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/of.h>

#include <asm/atomic.h>
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

/* RTAS tokens */
static int ibm_set_eeh_option;
static int ibm_set_slot_reset;
static int ibm_read_slot_reset_state;
static int ibm_read_slot_reset_state2;
static int ibm_slot_error_detail;
static int ibm_get_config_addr_info;
static int ibm_get_config_addr_info2;
static int ibm_configure_bridge;
static int ibm_configure_pe;

int eeh_subsystem_enabled;
EXPORT_SYMBOL(eeh_subsystem_enabled);

/* Lock to avoid races due to multiple reports of an error */
static DEFINE_RAW_SPINLOCK(confirm_error_lock);

/* Buffer for reporting slot-error-detail rtas calls. Its here
 * in BSS, and not dynamically alloced, so that it ends up in
 * RMO where RTAS can access it.
 */
static unsigned char slot_errbuf[RTAS_ERROR_LOG_MAX];
static DEFINE_SPINLOCK(slot_errbuf_lock);
static int eeh_error_buf_size;

/* Buffer for reporting pci register dumps. Its here in BSS, and
 * not dynamically alloced, so that it ends up in RMO where RTAS
 * can access it.
 */
#define EEH_PCI_REGS_LOG_LEN 4096
static unsigned char pci_regs_buf[EEH_PCI_REGS_LOG_LEN];

/* System monitoring statistics */
static unsigned long no_device;
static unsigned long no_dn;
static unsigned long no_cfg_addr;
static unsigned long ignored_check;
static unsigned long total_mmio_ffs;
static unsigned long false_positives;
static unsigned long slot_resets;

#define IS_BRIDGE(class_code) (((class_code)<<16) == PCI_BASE_CLASS_BRIDGE)

/* --------------------------------------------------------------- */
/* Below lies the EEH event infrastructure */

static void rtas_slot_error_detail(struct pci_dn *pdn, int severity,
                                   char *driver_log, size_t loglen)
{
	int config_addr;
	unsigned long flags;
	int rc;

	/* Log the error with the rtas logger */
	spin_lock_irqsave(&slot_errbuf_lock, flags);
	memset(slot_errbuf, 0, eeh_error_buf_size);

	/* Use PE configuration address, if present */
	config_addr = pdn->eeh_config_addr;
	if (pdn->eeh_pe_config_addr)
		config_addr = pdn->eeh_pe_config_addr;

	rc = rtas_call(ibm_slot_error_detail,
	               8, 1, NULL, config_addr,
	               BUID_HI(pdn->phb->buid),
	               BUID_LO(pdn->phb->buid),
	               virt_to_phys(driver_log), loglen,
	               virt_to_phys(slot_errbuf),
	               eeh_error_buf_size,
	               severity);

	if (rc == 0)
		log_error(slot_errbuf, ERR_TYPE_RTAS_LOG, 0);
	spin_unlock_irqrestore(&slot_errbuf_lock, flags);
}

/**
 * gather_pci_data - copy assorted PCI config space registers to buff
 * @pdn: device to report data for
 * @buf: point to buffer in which to log
 * @len: amount of room in buffer
 *
 * This routine captures assorted PCI configuration space data,
 * and puts them into a buffer for RTAS error logging.
 */
static size_t gather_pci_data(struct pci_dn *pdn, char * buf, size_t len)
{
	struct pci_dev *dev = pdn->pcidev;
	u32 cfg;
	int cap, i;
	int n = 0;

	n += scnprintf(buf+n, len-n, "%s\n", pdn->node->full_name);
	printk(KERN_WARNING "EEH: of node=%s\n", pdn->node->full_name);

	rtas_read_config(pdn, PCI_VENDOR_ID, 4, &cfg);
	n += scnprintf(buf+n, len-n, "dev/vend:%08x\n", cfg);
	printk(KERN_WARNING "EEH: PCI device/vendor: %08x\n", cfg);

	rtas_read_config(pdn, PCI_COMMAND, 4, &cfg);
	n += scnprintf(buf+n, len-n, "cmd/stat:%x\n", cfg);
	printk(KERN_WARNING "EEH: PCI cmd/status register: %08x\n", cfg);

	if (!dev) {
		printk(KERN_WARNING "EEH: no PCI device for this of node\n");
		return n;
	}

	/* Gather bridge-specific registers */
	if (dev->class >> 16 == PCI_BASE_CLASS_BRIDGE) {
		rtas_read_config(pdn, PCI_SEC_STATUS, 2, &cfg);
		n += scnprintf(buf+n, len-n, "sec stat:%x\n", cfg);
		printk(KERN_WARNING "EEH: Bridge secondary status: %04x\n", cfg);

		rtas_read_config(pdn, PCI_BRIDGE_CONTROL, 2, &cfg);
		n += scnprintf(buf+n, len-n, "brdg ctl:%x\n", cfg);
		printk(KERN_WARNING "EEH: Bridge control: %04x\n", cfg);
	}

	/* Dump out the PCI-X command and status regs */
	cap = pci_find_capability(dev, PCI_CAP_ID_PCIX);
	if (cap) {
		rtas_read_config(pdn, cap, 4, &cfg);
		n += scnprintf(buf+n, len-n, "pcix-cmd:%x\n", cfg);
		printk(KERN_WARNING "EEH: PCI-X cmd: %08x\n", cfg);

		rtas_read_config(pdn, cap+4, 4, &cfg);
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
			rtas_read_config(pdn, cap+4*i, 4, &cfg);
			n += scnprintf(buf+n, len-n, "%02x:%x\n", 4*i, cfg);
			printk(KERN_WARNING "EEH: PCI-E %02x: %08x\n", i, cfg);
		}

		cap = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
		if (cap) {
			n += scnprintf(buf+n, len-n, "pci-e AER:\n");
			printk(KERN_WARNING
			       "EEH: PCI-E AER capability register set follows:\n");

			for (i=0; i<14; i++) {
				rtas_read_config(pdn, cap+4*i, 4, &cfg);
				n += scnprintf(buf+n, len-n, "%02x:%x\n", 4*i, cfg);
				printk(KERN_WARNING "EEH: PCI-E AER %02x: %08x\n", i, cfg);
			}
		}
	}

	/* Gather status on devices under the bridge */
	if (dev->class >> 16 == PCI_BASE_CLASS_BRIDGE) {
		struct device_node *dn;

		for_each_child_of_node(pdn->node, dn) {
			pdn = PCI_DN(dn);
			if (pdn)
				n += gather_pci_data(pdn, buf+n, len-n);
		}
	}

	return n;
}

void eeh_slot_error_detail(struct pci_dn *pdn, int severity)
{
	size_t loglen = 0;
	pci_regs_buf[0] = 0;

	rtas_pci_enable(pdn, EEH_THAW_MMIO);
	rtas_configure_bridge(pdn);
	eeh_restore_bars(pdn);
	loglen = gather_pci_data(pdn, pci_regs_buf, EEH_PCI_REGS_LOG_LEN);

	rtas_slot_error_detail(pdn, severity, pci_regs_buf, loglen);
}

/**
 * read_slot_reset_state - Read the reset state of a device node's slot
 * @dn: device node to read
 * @rets: array to return results in
 */
static int read_slot_reset_state(struct pci_dn *pdn, int rets[])
{
	int token, outputs;
	int config_addr;

	if (ibm_read_slot_reset_state2 != RTAS_UNKNOWN_SERVICE) {
		token = ibm_read_slot_reset_state2;
		outputs = 4;
	} else {
		token = ibm_read_slot_reset_state;
		rets[2] = 0; /* fake PE Unavailable info */
		outputs = 3;
	}

	/* Use PE configuration address, if present */
	config_addr = pdn->eeh_config_addr;
	if (pdn->eeh_pe_config_addr)
		config_addr = pdn->eeh_pe_config_addr;

	return rtas_call(token, 3, outputs, rets, config_addr,
			 BUID_HI(pdn->phb->buid), BUID_LO(pdn->phb->buid));
}

/**
 * eeh_wait_for_slot_status - returns error status of slot
 * @pdn pci device node
 * @max_wait_msecs maximum number to millisecs to wait
 *
 * Return negative value if a permanent error, else return
 * Partition Endpoint (PE) status value.
 *
 * If @max_wait_msecs is positive, then this routine will
 * sleep until a valid status can be obtained, or until
 * the max allowed wait time is exceeded, in which case
 * a -2 is returned.
 */
int
eeh_wait_for_slot_status(struct pci_dn *pdn, int max_wait_msecs)
{
	int rc;
	int rets[3];
	int mwait;

	while (1) {
		rc = read_slot_reset_state(pdn, rets);
		if (rc) return rc;
		if (rets[1] == 0) return -1;  /* EEH is not supported */

		if (rets[0] != 5) return rets[0]; /* return actual status */

		if (rets[2] == 0) return -1; /* permanently unavailable */

		if (max_wait_msecs <= 0) break;

		mwait = rets[2];
		if (mwait <= 0) {
			printk (KERN_WARNING
			        "EEH: Firmware returned bad wait value=%d\n", mwait);
			mwait = 1000;
		} else if (mwait > 300*1000) {
			printk (KERN_WARNING
			        "EEH: Firmware is taking too long, time=%d\n", mwait);
			mwait = 300*1000;
		}
		max_wait_msecs -= mwait;
		msleep (mwait);
	}

	printk(KERN_WARNING "EEH: Timed out waiting for slot status\n");
	return -2;
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
struct device_node * find_device_pe(struct device_node *dn)
{
	while ((dn->parent) && PCI_DN(dn->parent) &&
	      (PCI_DN(dn->parent)->eeh_mode & EEH_MODE_SUPPORTED)) {
		dn = dn->parent;
	}
	return dn;
}

/** Mark all devices that are children of this device as failed.
 *  Mark the device driver too, so that it can see the failure
 *  immediately; this is critical, since some drivers poll
 *  status registers in interrupts ... If a driver is polling,
 *  and the slot is frozen, then the driver can deadlock in
 *  an interrupt context, which is bad.
 */

static void __eeh_mark_slot(struct device_node *parent, int mode_flag)
{
	struct device_node *dn;

	for_each_child_of_node(parent, dn) {
		if (PCI_DN(dn)) {
			/* Mark the pci device driver too */
			struct pci_dev *dev = PCI_DN(dn)->pcidev;

			PCI_DN(dn)->eeh_mode |= mode_flag;

			if (dev && dev->driver)
				dev->error_state = pci_channel_io_frozen;

			__eeh_mark_slot(dn, mode_flag);
		}
	}
}

void eeh_mark_slot (struct device_node *dn, int mode_flag)
{
	struct pci_dev *dev;
	dn = find_device_pe (dn);

	/* Back up one, since config addrs might be shared */
	if (!pcibios_find_pci_bus(dn) && PCI_DN(dn->parent))
		dn = dn->parent;

	PCI_DN(dn)->eeh_mode |= mode_flag;

	/* Mark the pci device too */
	dev = PCI_DN(dn)->pcidev;
	if (dev)
		dev->error_state = pci_channel_io_frozen;

	__eeh_mark_slot(dn, mode_flag);
}

static void __eeh_clear_slot(struct device_node *parent, int mode_flag)
{
	struct device_node *dn;

	for_each_child_of_node(parent, dn) {
		if (PCI_DN(dn)) {
			PCI_DN(dn)->eeh_mode &= ~mode_flag;
			PCI_DN(dn)->eeh_check_count = 0;
			__eeh_clear_slot(dn, mode_flag);
		}
	}
}

void eeh_clear_slot (struct device_node *dn, int mode_flag)
{
	unsigned long flags;
	raw_spin_lock_irqsave(&confirm_error_lock, flags);
	
	dn = find_device_pe (dn);
	
	/* Back up one, since config addrs might be shared */
	if (!pcibios_find_pci_bus(dn) && PCI_DN(dn->parent))
		dn = dn->parent;

	PCI_DN(dn)->eeh_mode &= ~mode_flag;
	PCI_DN(dn)->eeh_check_count = 0;
	__eeh_clear_slot(dn, mode_flag);
	raw_spin_unlock_irqrestore(&confirm_error_lock, flags);
}

void __eeh_set_pe_freset(struct device_node *parent, unsigned int *freset)
{
	struct device_node *dn;

	for_each_child_of_node(parent, dn) {
		if (PCI_DN(dn)) {

			struct pci_dev *dev = PCI_DN(dn)->pcidev;

			if (dev && dev->driver)
				*freset |= dev->needs_freset;

			__eeh_set_pe_freset(dn, freset);
		}
	}
}

void eeh_set_pe_freset(struct device_node *dn, unsigned int *freset)
{
	struct pci_dev *dev;
	dn = find_device_pe(dn);

	/* Back up one, since config addrs might be shared */
	if (!pcibios_find_pci_bus(dn) && PCI_DN(dn->parent))
		dn = dn->parent;

	dev = PCI_DN(dn)->pcidev;
	if (dev)
		*freset |= dev->needs_freset;

	__eeh_set_pe_freset(dn, freset);
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
	const char *location;

	total_mmio_ffs++;

	if (!eeh_subsystem_enabled)
		return 0;

	if (!dn) {
		no_dn++;
		return 0;
	}
	dn = find_device_pe(dn);
	pdn = PCI_DN(dn);

	/* Access to IO BARs might get this far and still not want checking. */
	if (!(pdn->eeh_mode & EEH_MODE_SUPPORTED) ||
	    pdn->eeh_mode & EEH_MODE_NOCHECK) {
		ignored_check++;
		pr_debug("EEH: Ignored check (%x) for %s %s\n",
			 pdn->eeh_mode, eeh_pci_name(dev), dn->full_name);
		return 0;
	}

	if (!pdn->eeh_config_addr && !pdn->eeh_pe_config_addr) {
		no_cfg_addr++;
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
	if (pdn->eeh_mode & EEH_MODE_ISOLATED) {
		pdn->eeh_check_count ++;
		if (pdn->eeh_check_count % EEH_MAX_FAILS == 0) {
			location = of_get_property(dn, "ibm,loc-code", NULL);
			printk (KERN_ERR "EEH: %d reads ignored for recovering device at "
				"location=%s driver=%s pci addr=%s\n",
				pdn->eeh_check_count, location,
				dev->driver->name, eeh_pci_name(dev));
			printk (KERN_ERR "EEH: Might be infinite loop in %s driver\n",
				dev->driver->name);
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
	ret = read_slot_reset_state(pdn, rets);

	/* If the call to firmware failed, punt */
	if (ret != 0) {
		printk(KERN_WARNING "EEH: read_slot_reset_state() failed; rc=%d dn=%s\n",
		       ret, dn->full_name);
		false_positives++;
		pdn->eeh_false_positives ++;
		rc = 0;
		goto dn_unlock;
	}

	/* Note that config-io to empty slots may fail;
	 * they are empty when they don't have children. */
	if ((rets[0] == 5) && (rets[2] == 0) && (dn->child == NULL)) {
		false_positives++;
		pdn->eeh_false_positives ++;
		rc = 0;
		goto dn_unlock;
	}

	/* If EEH is not supported on this device, punt. */
	if (rets[1] != 1) {
		printk(KERN_WARNING "EEH: event on unsupported device, rc=%d dn=%s\n",
		       ret, dn->full_name);
		false_positives++;
		pdn->eeh_false_positives ++;
		rc = 0;
		goto dn_unlock;
	}

	/* If not the kind of error we know about, punt. */
	if (rets[0] != 1 && rets[0] != 2 && rets[0] != 4 && rets[0] != 5) {
		false_positives++;
		pdn->eeh_false_positives ++;
		rc = 0;
		goto dn_unlock;
	}

	slot_resets++;
 
	/* Avoid repeated reports of this failure, including problems
	 * with other functions on this device, and functions under
	 * bridges. */
	eeh_mark_slot (dn, EEH_MODE_ISOLATED);
	raw_spin_unlock_irqrestore(&confirm_error_lock, flags);

	eeh_send_failure_event (dn, dev);

	/* Most EEH events are due to device driver bugs.  Having
	 * a stack trace will help the device-driver authors figure
	 * out what happened.  So print that out. */
	dump_stack();
	return 1;

dn_unlock:
	raw_spin_unlock_irqrestore(&confirm_error_lock, flags);
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
		no_device++;
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

/**
 * rtas_pci_enable - enable MMIO or DMA transfers for this slot
 * @pdn pci device node
 */

int
rtas_pci_enable(struct pci_dn *pdn, int function)
{
	int config_addr;
	int rc;

	/* Use PE configuration address, if present */
	config_addr = pdn->eeh_config_addr;
	if (pdn->eeh_pe_config_addr)
		config_addr = pdn->eeh_pe_config_addr;

	rc = rtas_call(ibm_set_eeh_option, 4, 1, NULL,
	               config_addr,
	               BUID_HI(pdn->phb->buid),
	               BUID_LO(pdn->phb->buid),
		            function);

	if (rc)
		printk(KERN_WARNING "EEH: Unexpected state change %d, err=%d dn=%s\n",
		        function, rc, pdn->node->full_name);

	rc = eeh_wait_for_slot_status (pdn, PCI_BUS_RESET_WAIT_MSEC);
	if ((rc == 4) && (function == EEH_THAW_MMIO))
		return 0;

	return rc;
}

/**
 * rtas_pci_slot_reset - raises/lowers the pci #RST line
 * @pdn pci device node
 * @state: 1/0 to raise/lower the #RST
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
	int config_addr;
	int rc;

	BUG_ON (pdn==NULL); 

	if (!pdn->phb) {
		printk (KERN_WARNING "EEH: in slot reset, device node %s has no phb\n",
		        pdn->node->full_name);
		return;
	}

	/* Use PE configuration address, if present */
	config_addr = pdn->eeh_config_addr;
	if (pdn->eeh_pe_config_addr)
		config_addr = pdn->eeh_pe_config_addr;

	rc = rtas_call(ibm_set_slot_reset, 4, 1, NULL,
	               config_addr,
	               BUID_HI(pdn->phb->buid),
	               BUID_LO(pdn->phb->buid),
	               state);

	/* Fundamental-reset not supported on this PE, try hot-reset */
	if (rc == -8 && state == 3) {
		rc = rtas_call(ibm_set_slot_reset, 4, 1, NULL,
			       config_addr,
			       BUID_HI(pdn->phb->buid),
			       BUID_LO(pdn->phb->buid), 1);
		if (rc)
			printk(KERN_WARNING
				"EEH: Unable to reset the failed slot,"
				" #RST=%d dn=%s\n",
				rc, pdn->node->full_name);
	}
}

/**
 * pcibios_set_pcie_slot_reset - Set PCI-E reset state
 * @dev:	pci device struct
 * @state:	reset state to enter
 *
 * Return value:
 * 	0 if success
 **/
int pcibios_set_pcie_reset_state(struct pci_dev *dev, enum pcie_reset_state state)
{
	struct device_node *dn = pci_device_to_OF_node(dev);
	struct pci_dn *pdn = PCI_DN(dn);

	switch (state) {
	case pcie_deassert_reset:
		rtas_pci_slot_reset(pdn, 0);
		break;
	case pcie_hot_reset:
		rtas_pci_slot_reset(pdn, 1);
		break;
	case pcie_warm_reset:
		rtas_pci_slot_reset(pdn, 3);
		break;
	default:
		return -EINVAL;
	};

	return 0;
}

/**
 * rtas_set_slot_reset -- assert the pci #RST line for 1/4 second
 * @pdn: pci device node to be reset.
 */

static void __rtas_set_slot_reset(struct pci_dn *pdn)
{
	unsigned int freset = 0;

	/* Determine type of EEH reset required for
	 * Partitionable Endpoint, a hot-reset (1)
	 * or a fundamental reset (3).
	 * A fundamental reset required by any device under
	 * Partitionable Endpoint trumps hot-reset.
  	 */
	eeh_set_pe_freset(pdn->node, &freset);

	if (freset)
		rtas_pci_slot_reset(pdn, 3);
	else
		rtas_pci_slot_reset(pdn, 1);

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
}

int rtas_set_slot_reset(struct pci_dn *pdn)
{
	int i, rc;

	/* Take three shots at resetting the bus */
	for (i=0; i<3; i++) {
		__rtas_set_slot_reset(pdn);

		rc = eeh_wait_for_slot_status(pdn, PCI_BUS_RESET_WAIT_MSEC);
		if (rc == 0)
			return 0;

		if (rc < 0) {
			printk(KERN_ERR "EEH: unrecoverable slot failure %s\n",
			       pdn->node->full_name);
			return -1;
		}
		printk(KERN_ERR "EEH: bus reset %d failed on slot %s, rc=%d\n",
		       i+1, pdn->node->full_name, rc);
	}

	return -1;
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
 * @pdn: pci device node
 *
 * Loads the PCI configuration space base address registers,
 * the expansion ROM base address, the latency timer, and etc.
 * from the saved values in the device node.
 */
static inline void __restore_bars (struct pci_dn *pdn)
{
	int i;
	u32 cmd;

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

	/* Restore PERR & SERR bits, some devices require it,
	   don't touch the other command bits */
	rtas_read_config(pdn, PCI_COMMAND, 4, &cmd);
	if (pdn->config_space[1] & PCI_COMMAND_PARITY)
		cmd |= PCI_COMMAND_PARITY;
	else
		cmd &= ~PCI_COMMAND_PARITY;
	if (pdn->config_space[1] & PCI_COMMAND_SERR)
		cmd |= PCI_COMMAND_SERR;
	else
		cmd &= ~PCI_COMMAND_SERR;
	rtas_write_config(pdn, PCI_COMMAND, 4, cmd);
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
	
	if ((pdn->eeh_mode & EEH_MODE_SUPPORTED) && !IS_BRIDGE(pdn->class_code))
		__restore_bars (pdn);

	for_each_child_of_node(pdn->node, dn)
		eeh_restore_bars (PCI_DN(dn));
}

/**
 * eeh_save_bars - save device bars
 *
 * Save the values of the device bars. Unlike the restore
 * routine, this routine is *not* recursive. This is because
 * PCI devices are added individually; but, for the restore,
 * an entire slot is reset at a time.
 */
static void eeh_save_bars(struct pci_dn *pdn)
{
	int i;

	if (!pdn )
		return;
	
	for (i = 0; i < 16; i++)
		rtas_read_config(pdn, i * 4, 4, &pdn->config_space[i]);
}

void
rtas_configure_bridge(struct pci_dn *pdn)
{
	int config_addr;
	int rc;
	int token;

	/* Use PE configuration address, if present */
	config_addr = pdn->eeh_config_addr;
	if (pdn->eeh_pe_config_addr)
		config_addr = pdn->eeh_pe_config_addr;

	/* Use new configure-pe function, if supported */
	if (ibm_configure_pe != RTAS_UNKNOWN_SERVICE)
		token = ibm_configure_pe;
	else
		token = ibm_configure_bridge;

	rc = rtas_call(token, 3, 1, NULL,
	               config_addr,
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

static int get_pe_addr (int config_addr,
                        struct eeh_early_enable_info *info)
{
	unsigned int rets[3];
	int ret;

	/* Use latest config-addr token on power6 */
	if (ibm_get_config_addr_info2 != RTAS_UNKNOWN_SERVICE) {
		/* Make sure we have a PE in hand */
		ret = rtas_call (ibm_get_config_addr_info2, 4, 2, rets,
			config_addr, info->buid_hi, info->buid_lo, 1);
		if (ret || (rets[0]==0))
			return 0;

		ret = rtas_call (ibm_get_config_addr_info2, 4, 2, rets,
			config_addr, info->buid_hi, info->buid_lo, 0);
		if (ret)
			return 0;
		return rets[0];
	}

	/* Use older config-addr token on power5 */
	if (ibm_get_config_addr_info != RTAS_UNKNOWN_SERVICE) {
		ret = rtas_call (ibm_get_config_addr_info, 4, 2, rets,
			config_addr, info->buid_hi, info->buid_lo, 0);
		if (ret)
			return 0;
		return rets[0];
	}
	return 0;
}

/* Enable eeh for the given device node. */
static void *early_enable_eeh(struct device_node *dn, void *data)
{
	unsigned int rets[3];
	struct eeh_early_enable_info *info = data;
	int ret;
	const u32 *class_code = of_get_property(dn, "class-code", NULL);
	const u32 *vendor_id = of_get_property(dn, "vendor-id", NULL);
	const u32 *device_id = of_get_property(dn, "device-id", NULL);
	const u32 *regs;
	int enable;
	struct pci_dn *pdn = PCI_DN(dn);

	pdn->class_code = 0;
	pdn->eeh_mode = 0;
	pdn->eeh_check_count = 0;
	pdn->eeh_freeze_count = 0;
	pdn->eeh_false_positives = 0;

	if (!of_device_is_available(dn))
		return NULL;

	/* Ignore bad nodes. */
	if (!class_code || !vendor_id || !device_id)
		return NULL;

	/* There is nothing to check on PCI to ISA bridges */
	if (dn->type && !strcmp(dn->type, "isa")) {
		pdn->eeh_mode |= EEH_MODE_NOCHECK;
		return NULL;
	}
	pdn->class_code = *class_code;

	/* Ok... see if this device supports EEH.  Some do, some don't,
	 * and the only way to find out is to check each and every one. */
	regs = of_get_property(dn, "reg", NULL);
	if (regs) {
		/* First register entry is addr (00BBSS00)  */
		/* Try to enable eeh */
		ret = rtas_call(ibm_set_eeh_option, 4, 1, NULL,
		                regs[0], info->buid_hi, info->buid_lo,
		                EEH_ENABLE);

		enable = 0;
		if (ret == 0) {
			pdn->eeh_config_addr = regs[0];

			/* If the newer, better, ibm,get-config-addr-info is supported, 
			 * then use that instead. */
			pdn->eeh_pe_config_addr = get_pe_addr(pdn->eeh_config_addr, info);

			/* Some older systems (Power4) allow the
			 * ibm,set-eeh-option call to succeed even on nodes
			 * where EEH is not supported. Verify support
			 * explicitly. */
			ret = read_slot_reset_state(pdn, rets);
			if ((ret == 0) && (rets[1] == 1))
				enable = 1;
		}

		if (enable) {
			eeh_subsystem_enabled = 1;
			pdn->eeh_mode |= EEH_MODE_SUPPORTED;

			pr_debug("EEH: %s: eeh enabled, config=%x pe_config=%x\n",
				 dn->full_name, pdn->eeh_config_addr,
				 pdn->eeh_pe_config_addr);
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

	eeh_save_bars(pdn);
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

	raw_spin_lock_init(&confirm_error_lock);
	spin_lock_init(&slot_errbuf_lock);

	np = of_find_node_by_path("/rtas");
	if (np == NULL)
		return;

	ibm_set_eeh_option = rtas_token("ibm,set-eeh-option");
	ibm_set_slot_reset = rtas_token("ibm,set-slot-reset");
	ibm_read_slot_reset_state2 = rtas_token("ibm,read-slot-reset-state2");
	ibm_read_slot_reset_state = rtas_token("ibm,read-slot-reset-state");
	ibm_slot_error_detail = rtas_token("ibm,slot-error-detail");
	ibm_get_config_addr_info = rtas_token("ibm,get-config-addr-info");
	ibm_get_config_addr_info2 = rtas_token("ibm,get-config-addr-info2");
	ibm_configure_bridge = rtas_token ("ibm,configure-bridge");
	ibm_configure_pe = rtas_token("ibm,configure-pe");

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
static void eeh_add_device_early(struct device_node *dn)
{
	struct pci_controller *phb;
	struct eeh_early_enable_info info;

	if (!dn || !PCI_DN(dn))
		return;
	phb = PCI_DN(dn)->phb;

	/* USB Bus children of PCI devices will not have BUID's */
	if (NULL == phb || 0 == phb->buid)
		return;

	info.buid_hi = BUID_HI(phb->buid);
	info.buid_lo = BUID_LO(phb->buid);
	early_enable_eeh(dn, &info);
}

void eeh_add_device_tree_early(struct device_node *dn)
{
	struct device_node *sib;

	for_each_child_of_node(dn, sib)
		eeh_add_device_tree_early(sib);
	eeh_add_device_early(dn);
}
EXPORT_SYMBOL_GPL(eeh_add_device_tree_early);

/**
 * eeh_add_device_late - perform EEH initialization for the indicated pci device
 * @dev: pci device for which to set up EEH
 *
 * This routine must be used to complete EEH initialization for PCI
 * devices that were added after system boot (e.g. hotplug, dlpar).
 */
static void eeh_add_device_late(struct pci_dev *dev)
{
	struct device_node *dn;
	struct pci_dn *pdn;

	if (!dev || !eeh_subsystem_enabled)
		return;

	pr_debug("EEH: Adding device %s\n", pci_name(dev));

	dn = pci_device_to_OF_node(dev);
	pdn = PCI_DN(dn);
	if (pdn->pcidev == dev) {
		pr_debug("EEH: Already referenced !\n");
		return;
	}
	WARN_ON(pdn->pcidev);

	pci_dev_get (dev);
	pdn->pcidev = dev;

	pci_addr_cache_insert_device(dev);
	eeh_sysfs_add_device(dev);
}

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
 * eeh_remove_device - undo EEH setup for the indicated pci device
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
	struct device_node *dn;
	if (!dev || !eeh_subsystem_enabled)
		return;

	/* Unregister the device with the EEH/PCI address search system */
	pr_debug("EEH: Removing device %s\n", pci_name(dev));

	dn = pci_device_to_OF_node(dev);
	if (PCI_DN(dn)->pcidev == NULL) {
		pr_debug("EEH: Not referenced !\n");
		return;
	}
	PCI_DN(dn)->pcidev = NULL;
	pci_dev_put (dev);

	pci_addr_cache_remove_device(dev);
	eeh_sysfs_remove_device(dev);
}

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
		seq_printf(m, "eeh_total_mmio_ffs=%ld\n", total_mmio_ffs);
	} else {
		seq_printf(m, "EEH Subsystem is enabled\n");
		seq_printf(m,
				"no device=%ld\n"
				"no device node=%ld\n"
				"no config address=%ld\n"
				"check not wanted=%ld\n"
				"eeh_total_mmio_ffs=%ld\n"
				"eeh_false_positives=%ld\n"
				"eeh_slot_resets=%ld\n",
				no_device, no_dn, no_cfg_addr, 
				ignored_check, total_mmio_ffs, 
				false_positives,
				slot_resets);
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
