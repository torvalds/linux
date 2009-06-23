/* Intel 7 core  Memory Controller kernel module (Nehalem)
 *
 * This file may be distributed under the terms of the
 * GNU General Public License version 2 only.
 *
 * Copyright (c) 2009 by:
 *	 Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * Red Hat Inc. http://www.redhat.com
 *
 * Forked and adapted from the i5400_edac driver
 *
 * Based on the following public Intel datasheets:
 * Intel Core i7 Processor Extreme Edition and Intel Core i7 Processor
 * Datasheet, Volume 2:
 *	http://download.intel.com/design/processor/datashts/320835.pdf
 * Intel Xeon Processor 5500 Series Datasheet Volume 2
 *	http://www.intel.com/Assets/PDF/datasheet/321322.pdf
 * also available at:
 * 	http://www.arrownac.com/manufacturers/intel/s/nehalem/5500-datasheet-v2.pdf
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/edac.h>
#include <linux/mmzone.h>

#include "edac_core.h"


/*
 * Alter this version for the module when modifications are made
 */
#define I7CORE_REVISION    " Ver: 1.0.0 " __DATE__
#define EDAC_MOD_STR      "i7core_edac"

/* HACK: temporary, just to enable all logs, for now */
#undef debugf0
#define debugf0(fmt, arg...)  edac_printk(KERN_INFO, "i7core", fmt, ##arg)

/*
 * Debug macros
 */
#define i7core_printk(level, fmt, arg...)			\
	edac_printk(level, "i7core", fmt, ##arg)

#define i7core_mc_printk(mci, level, fmt, arg...)		\
	edac_mc_chipset_printk(mci, level, "i7core", fmt, ##arg)

/*
 * i7core Memory Controller Registers
 */

	/* OFFSETS for Device 3 Function 0 */

#define MC_CONTROL	0x48
#define MC_STATUS	0x4c
#define MC_MAX_DOD	0x64

	/* OFFSETS for Devices 4,5 and 6 Function 0 */

#define MC_CHANNEL_ADDR_MATCH	0xf0

#define MC_MASK_DIMM	(1 << 41)
#define MC_MASK_RANK	(1 << 40)
#define MC_MASK_BANK	(1 << 39)
#define MC_MASK_PAGE	(1 << 38)
#define MC_MASK_COL	(1 << 37)

/*
 * i7core structs
 */

#define NUM_CHANS 3
#define NUM_FUNCS 1

struct i7core_info {
	u32	mc_control;
	u32	mc_status;
	u32	max_dod;
};

struct i7core_pvt {
	struct pci_dev		*pci_mcr;	/* Dev 3:0 */
	struct pci_dev		*pci_ch[NUM_CHANS][NUM_FUNCS];
	struct i7core_info	info;
};

/* Device name and register DID (Device ID) */
struct i7core_dev_info {
	const char *ctl_name;	/* name for this device */
	u16 fsb_mapping_errors;	/* DID for the branchmap,control */
};

static int chan_pci_ids[NUM_CHANS] = {
	PCI_DEVICE_ID_INTEL_I7_MC_CH0_CTRL,	/* Dev 4 */
	PCI_DEVICE_ID_INTEL_I7_MC_CH1_CTRL,	/* Dev 5 */
	PCI_DEVICE_ID_INTEL_I7_MC_CH2_CTRL,	/* Dev 6 */
};

/* Table of devices attributes supported by this driver */
static const struct i7core_dev_info i7core_devs[] = {
	{
		.ctl_name = "i7 Core",
		.fsb_mapping_errors = PCI_DEVICE_ID_INTEL_I7_MCR,
	},
};

static struct edac_pci_ctl_info *i7core_pci;

/****************************************************************************
			Anciliary status routines
 ****************************************************************************/

	/* MC_CONTROL bits */
#define CH2_ACTIVE(pvt)		((pvt)->info.mc_control & 1 << 10)
#define CH1_ACTIVE(pvt)		((pvt)->info.mc_control & 1 << 9)
#define CH0_ACTIVE(pvt)		((pvt)->info.mc_control & 1 << 8)
#define ECCx8(pvt)		((pvt)->info.mc_control & 1 << 1)

	/* MC_STATUS bits */
#define ECC_ENABLED(pvt)	((pvt)->info.mc_status & 1 << 3)
#define CH2_DISABLED(pvt)	((pvt)->info.mc_status & 1 << 2)
#define CH1_DISABLED(pvt)	((pvt)->info.mc_status & 1 << 1)
#define CH0_DISABLED(pvt)	((pvt)->info.mc_status & 1 << 0)

	/* MC_MAX_DOD read functions */
static inline int maxnumdimms(struct i7core_pvt *pvt)
{
	return (pvt->info.max_dod & 0x3) + 1;
}

static inline int maxnumrank(struct i7core_pvt *pvt)
{
	static int ranks[4] = { 1, 2, 4, -EINVAL };

	return ranks[(pvt->info.max_dod >> 2) & 0x3];
}

static inline int maxnumbank(struct i7core_pvt *pvt)
{
	static int banks[4] = { 4, 8, 16, -EINVAL };

	return banks[(pvt->info.max_dod >> 4) & 0x3];
}

static inline int maxnumrow(struct i7core_pvt *pvt)
{
	static int rows[8] = {
		1 << 12, 1 << 13, 1 << 14, 1 << 15,
		1 << 16, -EINVAL, -EINVAL, -EINVAL,
	};

	return rows[((pvt->info.max_dod >> 6) & 0x7)];
}

static inline int maxnumcol(struct i7core_pvt *pvt)
{
	static int cols[8] = {
		1 << 10, 1 << 11, 1 << 12, -EINVAL,
	};
	return cols[((pvt->info.max_dod >> 9) & 0x3) << 12];
}

/****************************************************************************
			Memory check routines
 ****************************************************************************/
static int get_dimm_config(struct mem_ctl_info *mci)
{
	struct i7core_pvt *pvt = mci->pvt_info;

	pci_read_config_dword(pvt->pci_mcr, MC_CONTROL, &pvt->info.mc_control);
	pci_read_config_dword(pvt->pci_mcr, MC_STATUS, &pvt->info.mc_status);
	pci_read_config_dword(pvt->pci_mcr, MC_MAX_DOD, &pvt->info.max_dod);

	debugf0("Channels  active [%c][%c][%c] - enabled [%c][%c][%c]\n",
		CH0_ACTIVE(pvt)?'0':'-',
		CH1_ACTIVE(pvt)?'1':'-',
		CH2_ACTIVE(pvt)?'2':'-',
		CH0_DISABLED(pvt)?'-':'0',
		CH1_DISABLED(pvt)?'-':'1',
		CH2_DISABLED(pvt)?'-':'2');

	if (ECC_ENABLED(pvt))
		debugf0("ECC enabled with x%d SDCC\n", ECCx8(pvt)?8:4);
	else
		debugf0("ECC disabled\n");

	/* FIXME: need to handle the error codes */
	debugf0("DOD Maximum limits: DIMMS: %d, %d-ranked, %d-banked\n",
		maxnumdimms(pvt), maxnumrank(pvt), maxnumbank(pvt));
	debugf0("DOD Maximum rows x colums = 0x%x x 0x%x\n",
		maxnumrow(pvt), maxnumcol(pvt));

	return 0;
}

/****************************************************************************
	Device initialization routines: put/get, init/exit
 ****************************************************************************/

/*
 *	i7core_put_devices	'put' all the devices that we have
 *				reserved via 'get'
 */
static void i7core_put_devices(struct mem_ctl_info *mci)
{
	struct i7core_pvt *pvt = mci->pvt_info;
	int i, n;

	pci_dev_put(pvt->pci_mcr);

	/* Release all PCI device functions at MTR channel controllers */
	for (i = 0; i < NUM_CHANS; i++)
		for (n = 0; n < NUM_FUNCS; n++)
			pci_dev_put(pvt->pci_ch[i][n]);
}

/*
 *	i7core_get_devices	Find and perform 'get' operation on the MCH's
 *			device/functions we want to reference for this driver
 *
 *			Need to 'get' device 16 func 1 and func 2
 */
static int i7core_get_devices(struct mem_ctl_info *mci, int dev_idx)
{
	struct i7core_pvt *pvt;
	struct pci_dev *pdev;
	int i, n, func;

	pvt = mci->pvt_info;
	memset(pvt, 0, sizeof(*pvt));

	pdev = pci_get_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_I7_MCR,
			      NULL);
	if (!pdev) {
		i7core_printk(KERN_ERR,
			"Couldn't get PCI ID %04x:%04x function 0\n",
			PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_I7_MCR);
		return -ENODEV;
	}
	pvt->pci_mcr=pdev;

	/* Get dimm basic config */
	get_dimm_config(mci);

	/* Retrieve all needed functions at MTR channel controllers */
	for (i = 0; i < NUM_CHANS; i++) {
		pdev = NULL;
		for (n = 0; n < NUM_FUNCS; n++) {
			pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
					      chan_pci_ids[i], pdev);
			if (!pdev) {
				/* End of list, leave */
				i7core_printk(KERN_ERR,
					"Device not found: PCI ID %04x:%04x "
					"found only %d functions "
					"(broken BIOS?)\n",
					PCI_VENDOR_ID_INTEL,
					chan_pci_ids[i], n);
				i7core_put_devices(mci);
				return -ENODEV;
			}
			func = PCI_FUNC(pdev->devfn);
			pvt->pci_ch[i][func] = pdev;
		}
	}
	i7core_printk(KERN_INFO, "Driver loaded.\n");

	return 0;
}

/*
 *	i7core_probe	Probe for ONE instance of device to see if it is
 *			present.
 *	return:
 *		0 for FOUND a device
 *		< 0 for error code
 */
static int __devinit i7core_probe(struct pci_dev *pdev,
				  const struct pci_device_id *id)
{
	struct mem_ctl_info *mci;
	struct i7core_pvt *pvt;
	int rc;
	int num_channels;
	int num_csrows;
	int num_dimms_per_channel;
	int dev_idx = id->driver_data;

	if (dev_idx >= ARRAY_SIZE(i7core_devs))
		return -EINVAL;

	/* wake up device */
	rc = pci_enable_device(pdev);
	if (rc == -EIO)
		return rc;

	debugf0("MC: " __FILE__ ": %s(), pdev bus %u dev=0x%x fn=0x%x\n",
		__func__,
		pdev->bus->number,
		PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

	/* We only are looking for func 0 of the set */
	if (PCI_FUNC(pdev->devfn) != 0)
		return -ENODEV;

	num_channels = NUM_CHANS;

	/* FIXME: FAKE data, since we currently don't now how to get this */
	num_dimms_per_channel = 4;
	num_csrows = num_dimms_per_channel;

	/* allocate a new MC control structure */
	mci = edac_mc_alloc(sizeof(*pvt), num_csrows, num_channels, 0);
	if (mci == NULL)
		return -ENOMEM;

	debugf0("MC: " __FILE__ ": %s(): mci = %p\n", __func__, mci);

	mci->dev = &pdev->dev;	/* record ptr  to the generic device */
	dev_set_drvdata(mci->dev, mci);

	pvt = mci->pvt_info;
//	pvt->system_address = pdev;	/* Record this device in our private */
//	pvt->maxch = num_channels;
//	pvt->maxdimmperch = num_dimms_per_channel;

	/* 'get' the pci devices we want to reserve for our use */
	if (i7core_get_devices(mci, dev_idx))
		goto fail0;

	mci->mc_idx = 0;
	mci->mtype_cap = MEM_FLAG_FB_DDR2;	/* FIXME: it uses DDR3 */
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = "i7core_edac.c";
	mci->mod_ver = I7CORE_REVISION;
	mci->ctl_name = i7core_devs[dev_idx].ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->ctl_page_to_phys = NULL;

	/* add this new MC control structure to EDAC's list of MCs */
	if (edac_mc_add_mc(mci)) {
		debugf0("MC: " __FILE__
			": %s(): failed edac_mc_add_mc()\n", __func__);
		/* FIXME: perhaps some code should go here that disables error
		 * reporting if we just enabled it
		 */
		goto fail1;
	}

	/* allocating generic PCI control info */
	i7core_pci = edac_pci_create_generic_ctl(&pdev->dev, EDAC_MOD_STR);
	if (!i7core_pci) {
		printk(KERN_WARNING
			"%s(): Unable to create PCI control\n",
			__func__);
		printk(KERN_WARNING
			"%s(): PCI error report via EDAC not setup\n",
			__func__);
	}

	return 0;

fail1:
	i7core_put_devices(mci);

fail0:
	edac_mc_free(mci);
	return -ENODEV;
}

/*
 *	i7core_remove	destructor for one instance of device
 *
 */
static void __devexit i7core_remove(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;

	debugf0(__FILE__ ": %s()\n", __func__);

	if (i7core_pci)
		edac_pci_release_generic_ctl(i7core_pci);

	mci = edac_mc_del_mc(&pdev->dev);
	if (!mci)
		return;

	/* retrieve references to resources, and free those resources */
	i7core_put_devices(mci);

	edac_mc_free(mci);
}

/*
 *	pci_device_id	table for which devices we are looking for
 *
 *	The "E500P" device is the first device supported.
 */
static const struct pci_device_id i7core_pci_tbl[] __devinitdata = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_I7_MCR)},
	{0,}			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, i7core_pci_tbl);

/*
 *	i7core_driver	pci_driver structure for this module
 *
 */
static struct pci_driver i7core_driver = {
	.name     = "i7core_edac",
	.probe    = i7core_probe,
	.remove   = __devexit_p(i7core_remove),
	.id_table = i7core_pci_tbl,
};

/*
 *	i7core_init		Module entry function
 *			Try to initialize this module for its devices
 */
static int __init i7core_init(void)
{
	int pci_rc;

	debugf2("MC: " __FILE__ ": %s()\n", __func__);

	/* Ensure that the OPSTATE is set correctly for POLL or NMI */
	opstate_init();

	pci_rc = pci_register_driver(&i7core_driver);

	return (pci_rc < 0) ? pci_rc : 0;
}

/*
 *	i7core_exit()	Module exit function
 *			Unregister the driver
 */
static void __exit i7core_exit(void)
{
	debugf2("MC: " __FILE__ ": %s()\n", __func__);
	pci_unregister_driver(&i7core_driver);
}

module_init(i7core_init);
module_exit(i7core_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_AUTHOR("Red Hat Inc. (http://www.redhat.com)");
MODULE_DESCRIPTION("MC Driver for Intel i7 Core memory controllers - "
		   I7CORE_REVISION);

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");
