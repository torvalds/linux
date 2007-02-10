/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005-2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

/* This file contains the master driver module for use by SGI IOC4 subdrivers.
 *
 * It allocates any resources shared between multiple subdevices, and
 * provides accessor functions (where needed) and the like for those
 * resources.  It also provides a mechanism for the subdevice modules
 * to support loading and unloading.
 *
 * Non-shared resources (e.g. external interrupt A_INT_OUT register page
 * alias, serial port and UART registers) are handled by the subdevice
 * modules themselves.
 *
 * This is all necessary because IOC4 is not implemented as a multi-function
 * PCI device, but an amalgamation of disparate registers for several
 * types of device (ATA, serial, external interrupts).  The normal
 * resource management in the kernel doesn't have quite the right interfaces
 * to handle this situation (e.g. multiple modules can't claim the same
 * PCI ID), thus this IOC4 master module.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ioc4.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <asm/io.h>

/***************
 * Definitions *
 ***************/

/* Tweakable values */

/* PCI bus speed detection/calibration */
#define IOC4_CALIBRATE_COUNT 63		/* Calibration cycle period */
#define IOC4_CALIBRATE_CYCLES 256	/* Average over this many cycles */
#define IOC4_CALIBRATE_DISCARD 2	/* Discard first few cycles */
#define IOC4_CALIBRATE_LOW_MHZ 25	/* Lower bound on bus speed sanity */
#define IOC4_CALIBRATE_HIGH_MHZ 75	/* Upper bound on bus speed sanity */
#define IOC4_CALIBRATE_DEFAULT_MHZ 66	/* Assumed if sanity check fails */

/************************
 * Submodule management *
 ************************/

static DEFINE_MUTEX(ioc4_mutex);

static LIST_HEAD(ioc4_devices);
static LIST_HEAD(ioc4_submodules);

/* Register an IOC4 submodule */
int
ioc4_register_submodule(struct ioc4_submodule *is)
{
	struct ioc4_driver_data *idd;

	mutex_lock(&ioc4_mutex);
	list_add(&is->is_list, &ioc4_submodules);

	/* Initialize submodule for each IOC4 */
	if (!is->is_probe)
		goto out;

	list_for_each_entry(idd, &ioc4_devices, idd_list) {
		if (is->is_probe(idd)) {
			printk(KERN_WARNING
			       "%s: IOC4 submodule %s probe failed "
			       "for pci_dev %s",
			       __FUNCTION__, module_name(is->is_owner),
			       pci_name(idd->idd_pdev));
		}
	}
 out:
	mutex_unlock(&ioc4_mutex);
	return 0;
}

/* Unregister an IOC4 submodule */
void
ioc4_unregister_submodule(struct ioc4_submodule *is)
{
	struct ioc4_driver_data *idd;

	mutex_lock(&ioc4_mutex);
	list_del(&is->is_list);

	/* Remove submodule for each IOC4 */
	if (!is->is_remove)
		goto out;

	list_for_each_entry(idd, &ioc4_devices, idd_list) {
		if (is->is_remove(idd)) {
			printk(KERN_WARNING
			       "%s: IOC4 submodule %s remove failed "
			       "for pci_dev %s.\n",
			       __FUNCTION__, module_name(is->is_owner),
			       pci_name(idd->idd_pdev));
		}
	}
 out:
	mutex_unlock(&ioc4_mutex);
}

/*********************
 * Device management *
 *********************/

#define IOC4_CALIBRATE_LOW_LIMIT \
	(1000*IOC4_EXTINT_COUNT_DIVISOR/IOC4_CALIBRATE_LOW_MHZ)
#define IOC4_CALIBRATE_HIGH_LIMIT \
	(1000*IOC4_EXTINT_COUNT_DIVISOR/IOC4_CALIBRATE_HIGH_MHZ)
#define IOC4_CALIBRATE_DEFAULT \
	(1000*IOC4_EXTINT_COUNT_DIVISOR/IOC4_CALIBRATE_DEFAULT_MHZ)

#define IOC4_CALIBRATE_END \
	(IOC4_CALIBRATE_CYCLES + IOC4_CALIBRATE_DISCARD)

#define IOC4_INT_OUT_MODE_TOGGLE 0x7	/* Toggle INT_OUT every COUNT+1 ticks */

/* Determines external interrupt output clock period of the PCI bus an
 * IOC4 is attached to.  This value can be used to determine the PCI
 * bus speed.
 *
 * IOC4 has a design feature that various internal timers are derived from
 * the PCI bus clock.  This causes IOC4 device drivers to need to take the
 * bus speed into account when setting various register values (e.g. INT_OUT
 * register COUNT field, UART divisors, etc).  Since this information is
 * needed by several subdrivers, it is determined by the main IOC4 driver,
 * even though the following code utilizes external interrupt registers
 * to perform the speed calculation.
 */
static void
ioc4_clock_calibrate(struct ioc4_driver_data *idd)
{
	union ioc4_int_out int_out;
	union ioc4_gpcr gpcr;
	unsigned int state, last_state = 1;
	struct timespec start_ts, end_ts;
	uint64_t start, end, period;
	unsigned int count = 0;

	/* Enable output */
	gpcr.raw = 0;
	gpcr.fields.dir = IOC4_GPCR_DIR_0;
	gpcr.fields.int_out_en = 1;
	writel(gpcr.raw, &idd->idd_misc_regs->gpcr_s.raw);

	/* Reset to power-on state */
	writel(0, &idd->idd_misc_regs->int_out.raw);
	mmiowb();

	/* Set up square wave */
	int_out.raw = 0;
	int_out.fields.count = IOC4_CALIBRATE_COUNT;
	int_out.fields.mode = IOC4_INT_OUT_MODE_TOGGLE;
	int_out.fields.diag = 0;
	writel(int_out.raw, &idd->idd_misc_regs->int_out.raw);
	mmiowb();

	/* Check square wave period averaged over some number of cycles */
	do {
		int_out.raw = readl(&idd->idd_misc_regs->int_out.raw);
		state = int_out.fields.int_out;
		if (!last_state && state) {
			count++;
			if (count == IOC4_CALIBRATE_END) {
				ktime_get_ts(&end_ts);
				break;
			} else if (count == IOC4_CALIBRATE_DISCARD)
				ktime_get_ts(&start_ts);
		}
		last_state = state;
	} while (1);

	/* Calculation rearranged to preserve intermediate precision.
	 * Logically:
	 * 1. "end - start" gives us the measurement period over all
	 *    the square wave cycles.
	 * 2. Divide by number of square wave cycles to get the period
	 *    of a square wave cycle.
	 * 3. Divide by 2*(int_out.fields.count+1), which is the formula
	 *    by which the IOC4 generates the square wave, to get the
	 *    period of an IOC4 INT_OUT count.
	 */
	end = end_ts.tv_sec * NSEC_PER_SEC + end_ts.tv_nsec;
	start = start_ts.tv_sec * NSEC_PER_SEC + start_ts.tv_nsec;
	period = (end - start) /
		(IOC4_CALIBRATE_CYCLES * 2 * (IOC4_CALIBRATE_COUNT + 1));

	/* Bounds check the result. */
	if (period > IOC4_CALIBRATE_LOW_LIMIT ||
	    period < IOC4_CALIBRATE_HIGH_LIMIT) {
		printk(KERN_INFO
		       "IOC4 %s: Clock calibration failed.  Assuming"
		       "PCI clock is %d ns.\n",
		       pci_name(idd->idd_pdev),
		       IOC4_CALIBRATE_DEFAULT / IOC4_EXTINT_COUNT_DIVISOR);
		period = IOC4_CALIBRATE_DEFAULT;
	} else {
		u64 ns = period;

		do_div(ns, IOC4_EXTINT_COUNT_DIVISOR);
		printk(KERN_DEBUG
		       "IOC4 %s: PCI clock is %llu ns.\n",
		       pci_name(idd->idd_pdev), (unsigned long long)ns);
	}

	/* Remember results.  We store the extint clock period rather
	 * than the PCI clock period so that greater precision is
	 * retained.  Divide by IOC4_EXTINT_COUNT_DIVISOR to get
	 * PCI clock period.
	 */
	idd->count_period = period;
}

/* There are three variants of IOC4 cards: IO9, IO10, and PCI-RT.
 * Each brings out different combinations of IOC4 signals, thus.
 * the IOC4 subdrivers need to know to which we're attached.
 *
 * We look for the presence of a SCSI (IO9) or SATA (IO10) controller
 * on the same PCI bus at slot number 3 to differentiate IO9 from IO10.
 * If neither is present, it's a PCI-RT.
 */
static unsigned int
ioc4_variant(struct ioc4_driver_data *idd)
{
	struct pci_dev *pdev = NULL;
	int found = 0;

	/* IO9: Look for a QLogic ISP 12160 at the same bus and slot 3. */
	do {
		pdev = pci_get_device(PCI_VENDOR_ID_QLOGIC,
				      PCI_DEVICE_ID_QLOGIC_ISP12160, pdev);
		if (pdev &&
		    idd->idd_pdev->bus->number == pdev->bus->number &&
		    3 == PCI_SLOT(pdev->devfn))
			found = 1;
		pci_dev_put(pdev);
	} while (pdev && !found);
	if (NULL != pdev)
		return IOC4_VARIANT_IO9;

	/* IO10: Look for a Vitesse VSC 7174 at the same bus and slot 3. */
	pdev = NULL;
	do {
		pdev = pci_get_device(PCI_VENDOR_ID_VITESSE,
				      PCI_DEVICE_ID_VITESSE_VSC7174, pdev);
		if (pdev &&
		    idd->idd_pdev->bus->number == pdev->bus->number &&
		    3 == PCI_SLOT(pdev->devfn))
			found = 1;
		pci_dev_put(pdev);
	} while (pdev && !found);
	if (NULL != pdev)
		return IOC4_VARIANT_IO10;

	/* PCI-RT: No SCSI/SATA controller will be present */
	return IOC4_VARIANT_PCI_RT;
}

/* Adds a new instance of an IOC4 card */
static int
ioc4_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
	struct ioc4_driver_data *idd;
	struct ioc4_submodule *is;
	uint32_t pcmd;
	int ret;

	/* Enable IOC4 and take ownership of it */
	if ((ret = pci_enable_device(pdev))) {
		printk(KERN_WARNING
		       "%s: Failed to enable IOC4 device for pci_dev %s.\n",
		       __FUNCTION__, pci_name(pdev));
		goto out;
	}
	pci_set_master(pdev);

	/* Set up per-IOC4 data */
	idd = kmalloc(sizeof(struct ioc4_driver_data), GFP_KERNEL);
	if (!idd) {
		printk(KERN_WARNING
		       "%s: Failed to allocate IOC4 data for pci_dev %s.\n",
		       __FUNCTION__, pci_name(pdev));
		ret = -ENODEV;
		goto out_idd;
	}
	idd->idd_pdev = pdev;
	idd->idd_pci_id = pci_id;

	/* Map IOC4 misc registers.  These are shared between subdevices
	 * so the main IOC4 module manages them.
	 */
	idd->idd_bar0 = pci_resource_start(idd->idd_pdev, 0);
	if (!idd->idd_bar0) {
		printk(KERN_WARNING
		       "%s: Unable to find IOC4 misc resource "
		       "for pci_dev %s.\n",
		       __FUNCTION__, pci_name(idd->idd_pdev));
		ret = -ENODEV;
		goto out_pci;
	}
	if (!request_mem_region(idd->idd_bar0, sizeof(struct ioc4_misc_regs),
			    "ioc4_misc")) {
		printk(KERN_WARNING
		       "%s: Unable to request IOC4 misc region "
		       "for pci_dev %s.\n",
		       __FUNCTION__, pci_name(idd->idd_pdev));
		ret = -ENODEV;
		goto out_pci;
	}
	idd->idd_misc_regs = ioremap(idd->idd_bar0,
				     sizeof(struct ioc4_misc_regs));
	if (!idd->idd_misc_regs) {
		printk(KERN_WARNING
		       "%s: Unable to remap IOC4 misc region "
		       "for pci_dev %s.\n",
		       __FUNCTION__, pci_name(idd->idd_pdev));
		ret = -ENODEV;
		goto out_misc_region;
	}

	/* Failsafe portion of per-IOC4 initialization */

	/* Detect card variant */
	idd->idd_variant = ioc4_variant(idd);
	printk(KERN_INFO "IOC4 %s: %s card detected.\n", pci_name(pdev),
	       idd->idd_variant == IOC4_VARIANT_IO9 ? "IO9" :
	       idd->idd_variant == IOC4_VARIANT_PCI_RT ? "PCI-RT" :
	       idd->idd_variant == IOC4_VARIANT_IO10 ? "IO10" : "unknown");

	/* Initialize IOC4 */
	pci_read_config_dword(idd->idd_pdev, PCI_COMMAND, &pcmd);
	pci_write_config_dword(idd->idd_pdev, PCI_COMMAND,
			       pcmd | PCI_COMMAND_PARITY | PCI_COMMAND_SERR);

	/* Determine PCI clock */
	ioc4_clock_calibrate(idd);

	/* Disable/clear all interrupts.  Need to do this here lest
	 * one submodule request the shared IOC4 IRQ, but interrupt
	 * is generated by a different subdevice.
	 */
	/* Disable */
	writel(~0, &idd->idd_misc_regs->other_iec.raw);
	writel(~0, &idd->idd_misc_regs->sio_iec);
	/* Clear (i.e. acknowledge) */
	writel(~0, &idd->idd_misc_regs->other_ir.raw);
	writel(~0, &idd->idd_misc_regs->sio_ir);

	/* Track PCI-device specific data */
	idd->idd_serial_data = NULL;
	pci_set_drvdata(idd->idd_pdev, idd);

	mutex_lock(&ioc4_mutex);
	list_add_tail(&idd->idd_list, &ioc4_devices);

	/* Add this IOC4 to all submodules */
	list_for_each_entry(is, &ioc4_submodules, is_list) {
		if (is->is_probe && is->is_probe(idd)) {
			printk(KERN_WARNING
			       "%s: IOC4 submodule 0x%s probe failed "
			       "for pci_dev %s.\n",
			       __FUNCTION__, module_name(is->is_owner),
			       pci_name(idd->idd_pdev));
		}
	}
	mutex_unlock(&ioc4_mutex);

	return 0;

out_misc_region:
	release_mem_region(idd->idd_bar0, sizeof(struct ioc4_misc_regs));
out_pci:
	kfree(idd);
out_idd:
	pci_disable_device(pdev);
out:
	return ret;
}

/* Removes a particular instance of an IOC4 card. */
static void
ioc4_remove(struct pci_dev *pdev)
{
	struct ioc4_submodule *is;
	struct ioc4_driver_data *idd;

	idd = pci_get_drvdata(pdev);

	/* Remove this IOC4 from all submodules */
	mutex_lock(&ioc4_mutex);
	list_for_each_entry(is, &ioc4_submodules, is_list) {
		if (is->is_remove && is->is_remove(idd)) {
			printk(KERN_WARNING
			       "%s: IOC4 submodule 0x%s remove failed "
			       "for pci_dev %s.\n",
			       __FUNCTION__, module_name(is->is_owner),
			       pci_name(idd->idd_pdev));
		}
	}
	mutex_unlock(&ioc4_mutex);

	/* Release resources */
	iounmap(idd->idd_misc_regs);
	if (!idd->idd_bar0) {
		printk(KERN_WARNING
		       "%s: Unable to get IOC4 misc mapping for pci_dev %s. "
		       "Device removal may be incomplete.\n",
		       __FUNCTION__, pci_name(idd->idd_pdev));
	}
	release_mem_region(idd->idd_bar0, sizeof(struct ioc4_misc_regs));

	/* Disable IOC4 and relinquish */
	pci_disable_device(pdev);

	/* Remove and free driver data */
	mutex_lock(&ioc4_mutex);
	list_del(&idd->idd_list);
	mutex_unlock(&ioc4_mutex);
	kfree(idd);
}

static struct pci_device_id ioc4_id_table[] = {
	{PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC4, PCI_ANY_ID,
	 PCI_ANY_ID, 0x0b4000, 0xFFFFFF},
	{0}
};

static struct pci_driver ioc4_driver = {
	.name = "IOC4",
	.id_table = ioc4_id_table,
	.probe = ioc4_probe,
	.remove = ioc4_remove,
};

MODULE_DEVICE_TABLE(pci, ioc4_id_table);

/*********************
 * Module management *
 *********************/

/* Module load */
static int __devinit
ioc4_init(void)
{
	return pci_register_driver(&ioc4_driver);
}

/* Module unload */
static void __devexit
ioc4_exit(void)
{
	pci_unregister_driver(&ioc4_driver);
}

module_init(ioc4_init);
module_exit(ioc4_exit);

MODULE_AUTHOR("Brent Casavant - Silicon Graphics, Inc. <bcasavan@sgi.com>");
MODULE_DESCRIPTION("PCI driver master module for SGI IOC4 Base-IO Card");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(ioc4_register_submodule);
EXPORT_SYMBOL(ioc4_unregister_submodule);
