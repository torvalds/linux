/*
 *
 *			Linux MegaRAID device driver
 *
 * Copyright (c) 2003-2004  LSI Logic Corporation.
 *
 *	   This program is free software; you can redistribute it and/or
 *	   modify it under the terms of the GNU General Public License
 *	   as published by the Free Software Foundation; either version
 *	   2 of the License, or (at your option) any later version.
 *
 * FILE		: megaraid_mbox.c
 * Version	: v2.20.5.1 (Nov 16 2006)
 *
 * Authors:
 * 	Atul Mukker		<Atul.Mukker@lsi.com>
 * 	Sreenivas Bagalkote	<Sreenivas.Bagalkote@lsi.com>
 * 	Manoj Jose		<Manoj.Jose@lsi.com>
 * 	Seokmann Ju
 *
 * List of supported controllers
 *
 * OEM	Product Name			VID	DID	SSVID	SSID
 * ---	------------			---	---	----	----
 * Dell PERC3/QC			101E	1960	1028	0471
 * Dell PERC3/DC			101E	1960	1028	0493
 * Dell PERC3/SC			101E	1960	1028	0475
 * Dell PERC3/Di			1028	1960	1028	0123
 * Dell PERC4/SC			1000	1960	1028	0520
 * Dell PERC4/DC			1000	1960	1028	0518
 * Dell PERC4/QC			1000	0407	1028	0531
 * Dell PERC4/Di			1028	000F	1028	014A
 * Dell PERC 4e/Si			1028	0013	1028	016c
 * Dell PERC 4e/Di			1028	0013	1028	016d
 * Dell PERC 4e/Di			1028	0013	1028	016e
 * Dell PERC 4e/Di			1028	0013	1028	016f
 * Dell PERC 4e/Di			1028	0013	1028	0170
 * Dell PERC 4e/DC			1000	0408	1028	0002
 * Dell PERC 4e/SC			1000	0408	1028	0001
 *
 *
 * LSI MegaRAID SCSI 320-0		1000	1960	1000	A520
 * LSI MegaRAID SCSI 320-1		1000	1960	1000	0520
 * LSI MegaRAID SCSI 320-2		1000	1960	1000	0518
 * LSI MegaRAID SCSI 320-0X		1000	0407	1000	0530
 * LSI MegaRAID SCSI 320-2X		1000	0407	1000	0532
 * LSI MegaRAID SCSI 320-4X		1000	0407	1000	0531
 * LSI MegaRAID SCSI 320-1E		1000	0408	1000	0001
 * LSI MegaRAID SCSI 320-2E		1000	0408	1000	0002
 * LSI MegaRAID SATA 150-4		1000	1960	1000	4523
 * LSI MegaRAID SATA 150-6		1000	1960	1000	0523
 * LSI MegaRAID SATA 300-4X		1000	0409	1000	3004
 * LSI MegaRAID SATA 300-8X		1000	0409	1000	3008
 *
 * INTEL RAID Controller SRCU42X	1000	0407	8086	0532
 * INTEL RAID Controller SRCS16		1000	1960	8086	0523
 * INTEL RAID Controller SRCU42E	1000	0408	8086	0002
 * INTEL RAID Controller SRCZCRX	1000	0407	8086	0530
 * INTEL RAID Controller SRCS28X	1000	0409	8086	3008
 * INTEL RAID Controller SROMBU42E	1000	0408	8086	3431
 * INTEL RAID Controller SROMBU42E	1000	0408	8086	3499
 * INTEL RAID Controller SRCU51L	1000	1960	8086	0520
 *
 * FSC	MegaRAID PCI Express ROMB	1000	0408	1734	1065
 *
 * ACER	MegaRAID ROMB-2E		1000	0408	1025	004D
 *
 * NEC	MegaRAID PCI Express ROMB	1000	0408	1033	8287
 *
 * For history of changes, see Documentation/ChangeLog.megaraid
 */

#include "megaraid_mbox.h"

static int megaraid_init(void);
static void megaraid_exit(void);

static int megaraid_probe_one(struct pci_dev*, const struct pci_device_id *);
static void megaraid_detach_one(struct pci_dev *);
static void megaraid_mbox_shutdown(struct pci_dev *);

static int megaraid_io_attach(adapter_t *);
static void megaraid_io_detach(adapter_t *);

static int megaraid_init_mbox(adapter_t *);
static void megaraid_fini_mbox(adapter_t *);

static int megaraid_alloc_cmd_packets(adapter_t *);
static void megaraid_free_cmd_packets(adapter_t *);

static int megaraid_mbox_setup_dma_pools(adapter_t *);
static void megaraid_mbox_teardown_dma_pools(adapter_t *);

static int megaraid_sysfs_alloc_resources(adapter_t *);
static void megaraid_sysfs_free_resources(adapter_t *);

static int megaraid_abort_handler(struct scsi_cmnd *);
static int megaraid_reset_handler(struct scsi_cmnd *);

static int mbox_post_sync_cmd(adapter_t *, uint8_t []);
static int mbox_post_sync_cmd_fast(adapter_t *, uint8_t []);
static int megaraid_busywait_mbox(mraid_device_t *);
static int megaraid_mbox_product_info(adapter_t *);
static int megaraid_mbox_extended_cdb(adapter_t *);
static int megaraid_mbox_support_ha(adapter_t *, uint16_t *);
static int megaraid_mbox_support_random_del(adapter_t *);
static int megaraid_mbox_get_max_sg(adapter_t *);
static void megaraid_mbox_enum_raid_scsi(adapter_t *);
static void megaraid_mbox_flush_cache(adapter_t *);
static int megaraid_mbox_fire_sync_cmd(adapter_t *);

static void megaraid_mbox_display_scb(adapter_t *, scb_t *);
static void megaraid_mbox_setup_device_map(adapter_t *);

static int megaraid_queue_command(struct scsi_cmnd *,
		void (*)(struct scsi_cmnd *));
static scb_t *megaraid_mbox_build_cmd(adapter_t *, struct scsi_cmnd *, int *);
static void megaraid_mbox_runpendq(adapter_t *, scb_t *);
static void megaraid_mbox_prepare_pthru(adapter_t *, scb_t *,
		struct scsi_cmnd *);
static void megaraid_mbox_prepare_epthru(adapter_t *, scb_t *,
		struct scsi_cmnd *);

static irqreturn_t megaraid_isr(int, void *);

static void megaraid_mbox_dpc(unsigned long);

static ssize_t megaraid_sysfs_show_app_hndl(struct class_device *, char *);
static ssize_t megaraid_sysfs_show_ldnum(struct device *, struct device_attribute *attr, char *);

static int megaraid_cmm_register(adapter_t *);
static int megaraid_cmm_unregister(adapter_t *);
static int megaraid_mbox_mm_handler(unsigned long, uioc_t *, uint32_t);
static int megaraid_mbox_mm_command(adapter_t *, uioc_t *);
static void megaraid_mbox_mm_done(adapter_t *, scb_t *);
static int gather_hbainfo(adapter_t *, mraid_hba_info_t *);
static int wait_till_fw_empty(adapter_t *);



MODULE_AUTHOR("megaraidlinux@lsi.com");
MODULE_DESCRIPTION("LSI Logic MegaRAID Mailbox Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(MEGARAID_VERSION);

/*
 * ### modules parameters for driver ###
 */

/*
 * Set to enable driver to expose unconfigured disk to kernel
 */
static int megaraid_expose_unconf_disks = 0;
module_param_named(unconf_disks, megaraid_expose_unconf_disks, int, 0);
MODULE_PARM_DESC(unconf_disks,
	"Set to expose unconfigured disks to kernel (default=0)");

/*
 * driver wait time if the adapter's mailbox is busy
 */
static unsigned int max_mbox_busy_wait = MBOX_BUSY_WAIT;
module_param_named(busy_wait, max_mbox_busy_wait, int, 0);
MODULE_PARM_DESC(busy_wait,
	"Max wait for mailbox in microseconds if busy (default=10)");

/*
 * number of sectors per IO command
 */
static unsigned int megaraid_max_sectors = MBOX_MAX_SECTORS;
module_param_named(max_sectors, megaraid_max_sectors, int, 0);
MODULE_PARM_DESC(max_sectors,
	"Maximum number of sectors per IO command (default=128)");

/*
 * number of commands per logical unit
 */
static unsigned int megaraid_cmd_per_lun = MBOX_DEF_CMD_PER_LUN;
module_param_named(cmd_per_lun, megaraid_cmd_per_lun, int, 0);
MODULE_PARM_DESC(cmd_per_lun,
	"Maximum number of commands per logical unit (default=64)");


/*
 * Fast driver load option, skip scanning for physical devices during load.
 * This would result in non-disk devices being skipped during driver load
 * time. These can be later added though, using /proc/scsi/scsi
 */
static unsigned int megaraid_fast_load = 0;
module_param_named(fast_load, megaraid_fast_load, int, 0);
MODULE_PARM_DESC(fast_load,
	"Faster loading of the driver, skips physical devices! (default=0)");


/*
 * mraid_debug level - threshold for amount of information to be displayed by
 * the driver. This level can be changed through modules parameters, ioctl or
 * sysfs/proc interface. By default, print the announcement messages only.
 */
int mraid_debug_level = CL_ANN;
module_param_named(debug_level, mraid_debug_level, int, 0);
MODULE_PARM_DESC(debug_level, "Debug level for driver (default=0)");

/*
 * ### global data ###
 */
static uint8_t megaraid_mbox_version[8] =
	{ 0x02, 0x20, 0x04, 0x06, 3, 7, 20, 5 };


/*
 * PCI table for all supported controllers.
 */
static struct pci_device_id pci_id_table_g[] =  {
	{
		PCI_VENDOR_ID_DELL,
		PCI_DEVICE_ID_PERC4_DI_DISCOVERY,
		PCI_VENDOR_ID_DELL,
		PCI_SUBSYS_ID_PERC4_DI_DISCOVERY,
	},
	{
		PCI_VENDOR_ID_LSI_LOGIC,
		PCI_DEVICE_ID_PERC4_SC,
		PCI_VENDOR_ID_DELL,
		PCI_SUBSYS_ID_PERC4_SC,
	},
	{
		PCI_VENDOR_ID_LSI_LOGIC,
		PCI_DEVICE_ID_PERC4_DC,
		PCI_VENDOR_ID_DELL,
		PCI_SUBSYS_ID_PERC4_DC,
	},
	{
		PCI_VENDOR_ID_LSI_LOGIC,
		PCI_DEVICE_ID_VERDE,
		PCI_ANY_ID,
		PCI_ANY_ID,
	},
	{
		PCI_VENDOR_ID_DELL,
		PCI_DEVICE_ID_PERC4_DI_EVERGLADES,
		PCI_VENDOR_ID_DELL,
		PCI_SUBSYS_ID_PERC4_DI_EVERGLADES,
	},
	{
		PCI_VENDOR_ID_DELL,
		PCI_DEVICE_ID_PERC4E_SI_BIGBEND,
		PCI_VENDOR_ID_DELL,
		PCI_SUBSYS_ID_PERC4E_SI_BIGBEND,
	},
	{
		PCI_VENDOR_ID_DELL,
		PCI_DEVICE_ID_PERC4E_DI_KOBUK,
		PCI_VENDOR_ID_DELL,
		PCI_SUBSYS_ID_PERC4E_DI_KOBUK,
	},
	{
		PCI_VENDOR_ID_DELL,
		PCI_DEVICE_ID_PERC4E_DI_CORVETTE,
		PCI_VENDOR_ID_DELL,
		PCI_SUBSYS_ID_PERC4E_DI_CORVETTE,
	},
	{
		PCI_VENDOR_ID_DELL,
		PCI_DEVICE_ID_PERC4E_DI_EXPEDITION,
		PCI_VENDOR_ID_DELL,
		PCI_SUBSYS_ID_PERC4E_DI_EXPEDITION,
	},
	{
		PCI_VENDOR_ID_DELL,
		PCI_DEVICE_ID_PERC4E_DI_GUADALUPE,
		PCI_VENDOR_ID_DELL,
		PCI_SUBSYS_ID_PERC4E_DI_GUADALUPE,
	},
	{
		PCI_VENDOR_ID_LSI_LOGIC,
		PCI_DEVICE_ID_DOBSON,
		PCI_ANY_ID,
		PCI_ANY_ID,
	},
	{
		PCI_VENDOR_ID_AMI,
		PCI_DEVICE_ID_AMI_MEGARAID3,
		PCI_ANY_ID,
		PCI_ANY_ID,
	},
	{
		PCI_VENDOR_ID_LSI_LOGIC,
		PCI_DEVICE_ID_AMI_MEGARAID3,
		PCI_ANY_ID,
		PCI_ANY_ID,
	},
	{
		PCI_VENDOR_ID_LSI_LOGIC,
		PCI_DEVICE_ID_LINDSAY,
		PCI_ANY_ID,
		PCI_ANY_ID,
	},
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, pci_id_table_g);


static struct pci_driver megaraid_pci_driver = {
	.name		= "megaraid",
	.id_table	= pci_id_table_g,
	.probe		= megaraid_probe_one,
	.remove		= __devexit_p(megaraid_detach_one),
	.shutdown	= megaraid_mbox_shutdown,
};



// definitions for the device attributes for exporting logical drive number
// for a scsi address (Host, Channel, Id, Lun)

CLASS_DEVICE_ATTR(megaraid_mbox_app_hndl, S_IRUSR, megaraid_sysfs_show_app_hndl,
		NULL);

// Host template initializer for megaraid mbox sysfs device attributes
static struct class_device_attribute *megaraid_shost_attrs[] = {
	&class_device_attr_megaraid_mbox_app_hndl,
	NULL,
};


DEVICE_ATTR(megaraid_mbox_ld, S_IRUSR, megaraid_sysfs_show_ldnum, NULL);

// Host template initializer for megaraid mbox sysfs device attributes
static struct device_attribute *megaraid_sdev_attrs[] = {
	&dev_attr_megaraid_mbox_ld,
	NULL,
};

/**
 * megaraid_change_queue_depth - Change the device's queue depth
 * @sdev:	scsi device struct
 * @qdepth:	depth to set
 *
 * Return value:
 * 	actual depth set
 */
static int megaraid_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	if (qdepth > MBOX_MAX_SCSI_CMDS)
		qdepth = MBOX_MAX_SCSI_CMDS;
	scsi_adjust_queue_depth(sdev, 0, qdepth);
	return sdev->queue_depth;
}

/*
 * Scsi host template for megaraid unified driver
 */
static struct scsi_host_template megaraid_template_g = {
	.module				= THIS_MODULE,
	.name				= "LSI Logic MegaRAID driver",
	.proc_name			= "megaraid",
	.queuecommand			= megaraid_queue_command,
	.eh_abort_handler		= megaraid_abort_handler,
	.eh_device_reset_handler	= megaraid_reset_handler,
	.eh_bus_reset_handler		= megaraid_reset_handler,
	.eh_host_reset_handler		= megaraid_reset_handler,
	.change_queue_depth		= megaraid_change_queue_depth,
	.use_clustering			= ENABLE_CLUSTERING,
	.sdev_attrs			= megaraid_sdev_attrs,
	.shost_attrs			= megaraid_shost_attrs,
};


/**
 * megaraid_init - module load hook
 *
 * We register ourselves as hotplug enabled module and let PCI subsystem
 * discover our adapters.
 */
static int __init
megaraid_init(void)
{
	int	rval;

	// Announce the driver version
	con_log(CL_ANN, (KERN_INFO "megaraid: %s %s\n", MEGARAID_VERSION,
		MEGARAID_EXT_VERSION));

	// check validity of module parameters
	if (megaraid_cmd_per_lun > MBOX_MAX_SCSI_CMDS) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid mailbox: max commands per lun reset to %d\n",
			MBOX_MAX_SCSI_CMDS));

		megaraid_cmd_per_lun = MBOX_MAX_SCSI_CMDS;
	}


	// register as a PCI hot-plug driver module
	rval = pci_register_driver(&megaraid_pci_driver);
	if (rval < 0) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: could not register hotplug support.\n"));
	}

	return rval;
}


/**
 * megaraid_exit - driver unload entry point
 *
 * We simply unwrap the megaraid_init routine here.
 */
static void __exit
megaraid_exit(void)
{
	con_log(CL_DLEVEL1, (KERN_NOTICE "megaraid: unloading framework\n"));

	// unregister as PCI hotplug driver
	pci_unregister_driver(&megaraid_pci_driver);

	return;
}


/**
 * megaraid_probe_one - PCI hotplug entry point
 * @pdev	: handle to this controller's PCI configuration space
 * @id		: pci device id of the class of controllers
 *
 * This routine should be called whenever a new adapter is detected by the
 * PCI hotplug susbsystem.
 */
static int __devinit
megaraid_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	adapter_t	*adapter;


	// detected a new controller
	con_log(CL_ANN, (KERN_INFO
		"megaraid: probe new device %#4.04x:%#4.04x:%#4.04x:%#4.04x: ",
		pdev->vendor, pdev->device, pdev->subsystem_vendor,
		pdev->subsystem_device));

	con_log(CL_ANN, ("bus %d:slot %d:func %d\n", pdev->bus->number,
		PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn)));

	if (pci_enable_device(pdev)) {
		con_log(CL_ANN, (KERN_WARNING
				"megaraid: pci_enable_device failed\n"));

		return -ENODEV;
	}

	// Enable bus-mastering on this controller
	pci_set_master(pdev);

	// Allocate the per driver initialization structure
	adapter = kzalloc(sizeof(adapter_t), GFP_KERNEL);

	if (adapter == NULL) {
		con_log(CL_ANN, (KERN_WARNING
		"megaraid: out of memory, %s %d.\n", __FUNCTION__, __LINE__));

		goto out_probe_one;
	}


	// set up PCI related soft state and other pre-known parameters
	adapter->unique_id	= pdev->bus->number << 8 | pdev->devfn;
	adapter->irq		= pdev->irq;
	adapter->pdev		= pdev;

	atomic_set(&adapter->being_detached, 0);

	// Setup the default DMA mask. This would be changed later on
	// depending on hardware capabilities
	if (pci_set_dma_mask(adapter->pdev, DMA_32BIT_MASK) != 0) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: pci_set_dma_mask failed:%d\n", __LINE__));

		goto out_free_adapter;
	}


	// Initialize the synchronization lock for kernel and LLD
	spin_lock_init(&adapter->lock);

	// Initialize the command queues: the list of free SCBs and the list
	// of pending SCBs.
	INIT_LIST_HEAD(&adapter->kscb_pool);
	spin_lock_init(SCSI_FREE_LIST_LOCK(adapter));

	INIT_LIST_HEAD(&adapter->pend_list);
	spin_lock_init(PENDING_LIST_LOCK(adapter));

	INIT_LIST_HEAD(&adapter->completed_list);
	spin_lock_init(COMPLETED_LIST_LOCK(adapter));


	// Start the mailbox based controller
	if (megaraid_init_mbox(adapter) != 0) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: maibox adapter did not initialize\n"));

		goto out_free_adapter;
	}

	// Register with LSI Common Management Module
	if (megaraid_cmm_register(adapter) != 0) {

		con_log(CL_ANN, (KERN_WARNING
		"megaraid: could not register with management module\n"));

		goto out_fini_mbox;
	}

	// setup adapter handle in PCI soft state
	pci_set_drvdata(pdev, adapter);

	// attach with scsi mid-layer
	if (megaraid_io_attach(adapter) != 0) {

		con_log(CL_ANN, (KERN_WARNING "megaraid: io attach failed\n"));

		goto out_cmm_unreg;
	}

	return 0;

out_cmm_unreg:
	pci_set_drvdata(pdev, NULL);
	megaraid_cmm_unregister(adapter);
out_fini_mbox:
	megaraid_fini_mbox(adapter);
out_free_adapter:
	kfree(adapter);
out_probe_one:
	pci_disable_device(pdev);

	return -ENODEV;
}


/**
 * megaraid_detach_one - release framework resources and call LLD release routine
 * @pdev	: handle for our PCI cofiguration space
 *
 * This routine is called during driver unload. We free all the allocated
 * resources and call the corresponding LLD so that it can also release all
 * its resources.
 *
 * This routine is also called from the PCI hotplug system.
 */
static void
megaraid_detach_one(struct pci_dev *pdev)
{
	adapter_t		*adapter;
	struct Scsi_Host	*host;


	// Start a rollback on this adapter
	adapter = pci_get_drvdata(pdev);

	if (!adapter) {
		con_log(CL_ANN, (KERN_CRIT
		"megaraid: Invalid detach on %#4.04x:%#4.04x:%#4.04x:%#4.04x\n",
			pdev->vendor, pdev->device, pdev->subsystem_vendor,
			pdev->subsystem_device));

		return;
	}
	else {
		con_log(CL_ANN, (KERN_NOTICE
		"megaraid: detaching device %#4.04x:%#4.04x:%#4.04x:%#4.04x\n",
			pdev->vendor, pdev->device, pdev->subsystem_vendor,
			pdev->subsystem_device));
	}


	host = adapter->host;

	// do not allow any more requests from the management module for this
	// adapter.
	// FIXME: How do we account for the request which might still be
	// pending with us?
	atomic_set(&adapter->being_detached, 1);

	// detach from the IO sub-system
	megaraid_io_detach(adapter);

	// reset the device state in the PCI structure. We check this
	// condition when we enter here. If the device state is NULL,
	// that would mean the device has already been removed
	pci_set_drvdata(pdev, NULL);

	// Unregister from common management module
	//
	// FIXME: this must return success or failure for conditions if there
	// is a command pending with LLD or not.
	megaraid_cmm_unregister(adapter);

	// finalize the mailbox based controller and release all resources
	megaraid_fini_mbox(adapter);

	kfree(adapter);

	scsi_host_put(host);

	pci_disable_device(pdev);

	return;
}


/**
 * megaraid_mbox_shutdown - PCI shutdown for megaraid HBA
 * @pdev		: generic driver model device
 *
 * Shutdown notification, perform flush cache.
 */
static void
megaraid_mbox_shutdown(struct pci_dev *pdev)
{
	adapter_t		*adapter = pci_get_drvdata(pdev);
	static int		counter;

	if (!adapter) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: null device in shutdown\n"));
		return;
	}

	// flush caches now
	con_log(CL_ANN, (KERN_INFO "megaraid: flushing adapter %d...",
		counter++));

	megaraid_mbox_flush_cache(adapter);

	con_log(CL_ANN, ("done\n"));
}


/**
 * megaraid_io_attach - attach a device with the IO subsystem
 * @adapter		: controller's soft state
 *
 * Attach this device with the IO subsystem.
 */
static int
megaraid_io_attach(adapter_t *adapter)
{
	struct Scsi_Host	*host;

	// Initialize SCSI Host structure
	host = scsi_host_alloc(&megaraid_template_g, 8);
	if (!host) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid mbox: scsi_register failed\n"));

		return -1;
	}

	SCSIHOST2ADAP(host)	= (caddr_t)adapter;
	adapter->host		= host;

	host->irq		= adapter->irq;
	host->unique_id		= adapter->unique_id;
	host->can_queue		= adapter->max_cmds;
	host->this_id		= adapter->init_id;
	host->sg_tablesize	= adapter->sglen;
	host->max_sectors	= adapter->max_sectors;
	host->cmd_per_lun	= adapter->cmd_per_lun;
	host->max_channel	= adapter->max_channel;
	host->max_id		= adapter->max_target;
	host->max_lun		= adapter->max_lun;


	// notify mid-layer about the new controller
	if (scsi_add_host(host, &adapter->pdev->dev)) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid mbox: scsi_add_host failed\n"));

		scsi_host_put(host);

		return -1;
	}

	scsi_scan_host(host);

	return 0;
}


/**
 * megaraid_io_detach - detach a device from the IO subsystem
 * @adapter		: controller's soft state
 *
 * Detach this device from the IO subsystem.
 */
static void
megaraid_io_detach(adapter_t *adapter)
{
	struct Scsi_Host	*host;

	con_log(CL_DLEVEL1, (KERN_INFO "megaraid: io detach\n"));

	host = adapter->host;

	scsi_remove_host(host);

	return;
}


/*
 * START: Mailbox Low Level Driver
 *
 * This is section specific to the single mailbox based controllers
 */

/**
 * megaraid_init_mbox - initialize controller
 * @adapter		: our soft state
 *
 * - Allocate 16-byte aligned mailbox memory for firmware handshake
 * - Allocate controller's memory resources
 * - Find out all initialization data
 * - Allocate memory required for all the commands
 * - Use internal library of FW routines, build up complete soft state
 */
static int __devinit
megaraid_init_mbox(adapter_t *adapter)
{
	struct pci_dev		*pdev;
	mraid_device_t		*raid_dev;
	int			i;
	uint32_t		magic64;


	adapter->ito	= MBOX_TIMEOUT;
	pdev		= adapter->pdev;

	/*
	 * Allocate and initialize the init data structure for mailbox
	 * controllers
	 */
	raid_dev = kzalloc(sizeof(mraid_device_t), GFP_KERNEL);
	if (raid_dev == NULL) return -1;


	/*
	 * Attach the adapter soft state to raid device soft state
	 */
	adapter->raid_device	= (caddr_t)raid_dev;
	raid_dev->fast_load	= megaraid_fast_load;


	// our baseport
	raid_dev->baseport = pci_resource_start(pdev, 0);

	if (pci_request_regions(pdev, "MegaRAID: LSI Logic Corporation") != 0) {

		con_log(CL_ANN, (KERN_WARNING
				"megaraid: mem region busy\n"));

		goto out_free_raid_dev;
	}

	raid_dev->baseaddr = ioremap_nocache(raid_dev->baseport, 128);

	if (!raid_dev->baseaddr) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: could not map hba memory\n") );

		goto out_release_regions;
	}

	/* initialize the mutual exclusion lock for the mailbox */
	spin_lock_init(&raid_dev->mailbox_lock);

	/* allocate memory required for commands */
	if (megaraid_alloc_cmd_packets(adapter) != 0)
		goto out_iounmap;

	/*
	 * Issue SYNC cmd to flush the pending cmds in the adapter
	 * and initialize its internal state
	 */

	if (megaraid_mbox_fire_sync_cmd(adapter))
		con_log(CL_ANN, ("megaraid: sync cmd failed\n"));

	/*
	 * Setup the rest of the soft state using the library of
	 * FW routines
	 */

	/* request IRQ and register the interrupt service routine */
	if (request_irq(adapter->irq, megaraid_isr, IRQF_SHARED, "megaraid",
		adapter)) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: Couldn't register IRQ %d!\n", adapter->irq));
		goto out_alloc_cmds;

	}

	// Product info
	if (megaraid_mbox_product_info(adapter) != 0)
		goto out_free_irq;

	// Do we support extended CDBs
	adapter->max_cdb_sz = 10;
	if (megaraid_mbox_extended_cdb(adapter) == 0) {
		adapter->max_cdb_sz = 16;
	}

	/*
	 * Do we support cluster environment, if we do, what is the initiator
	 * id.
	 * NOTE: In a non-cluster aware firmware environment, the LLD should
	 * return 7 as initiator id.
	 */
	adapter->ha		= 0;
	adapter->init_id	= -1;
	if (megaraid_mbox_support_ha(adapter, &adapter->init_id) == 0) {
		adapter->ha = 1;
	}

	/*
	 * Prepare the device ids array to have the mapping between the kernel
	 * device address and megaraid device address.
	 * We export the physical devices on their actual addresses. The
	 * logical drives are exported on a virtual SCSI channel
	 */
	megaraid_mbox_setup_device_map(adapter);

	// If the firmware supports random deletion, update the device id map
	if (megaraid_mbox_support_random_del(adapter)) {

		// Change the logical drives numbers in device_ids array one
		// slot in device_ids is reserved for target id, that's why
		// "<=" below
		for (i = 0; i <= MAX_LOGICAL_DRIVES_40LD; i++) {
			adapter->device_ids[adapter->max_channel][i] += 0x80;
		}
		adapter->device_ids[adapter->max_channel][adapter->init_id] =
			0xFF;

		raid_dev->random_del_supported = 1;
	}

	/*
	 * find out the maximum number of scatter-gather elements supported by
	 * this firmware
	 */
	adapter->sglen = megaraid_mbox_get_max_sg(adapter);

	// enumerate RAID and SCSI channels so that all devices on SCSI
	// channels can later be exported, including disk devices
	megaraid_mbox_enum_raid_scsi(adapter);

	/*
	 * Other parameters required by upper layer
	 *
	 * maximum number of sectors per IO command
	 */
	adapter->max_sectors = megaraid_max_sectors;

	/*
	 * number of queued commands per LUN.
	 */
	adapter->cmd_per_lun = megaraid_cmd_per_lun;

	/*
	 * Allocate resources required to issue FW calls, when sysfs is
	 * accessed
	 */
	if (megaraid_sysfs_alloc_resources(adapter) != 0)
		goto out_free_irq;

	// Set the DMA mask to 64-bit. All supported controllers as capable of
	// DMA in this range
	pci_read_config_dword(adapter->pdev, PCI_CONF_AMISIG64, &magic64);

	if (((magic64 == HBA_SIGNATURE_64_BIT) &&
		((adapter->pdev->subsystem_device !=
		PCI_SUBSYS_ID_MEGARAID_SATA_150_6) &&
		(adapter->pdev->subsystem_device !=
		PCI_SUBSYS_ID_MEGARAID_SATA_150_4))) ||
		(adapter->pdev->vendor == PCI_VENDOR_ID_LSI_LOGIC &&
		adapter->pdev->device == PCI_DEVICE_ID_VERDE) ||
		(adapter->pdev->vendor == PCI_VENDOR_ID_LSI_LOGIC &&
		adapter->pdev->device == PCI_DEVICE_ID_DOBSON) ||
		(adapter->pdev->vendor == PCI_VENDOR_ID_LSI_LOGIC &&
		adapter->pdev->device == PCI_DEVICE_ID_LINDSAY) ||
		(adapter->pdev->vendor == PCI_VENDOR_ID_DELL &&
		adapter->pdev->device == PCI_DEVICE_ID_PERC4_DI_EVERGLADES) ||
		(adapter->pdev->vendor == PCI_VENDOR_ID_DELL &&
		adapter->pdev->device == PCI_DEVICE_ID_PERC4E_DI_KOBUK)) {
		if (pci_set_dma_mask(adapter->pdev, DMA_64BIT_MASK)) {
			con_log(CL_ANN, (KERN_WARNING
				"megaraid: DMA mask for 64-bit failed\n"));

			if (pci_set_dma_mask (adapter->pdev, DMA_32BIT_MASK)) {
				con_log(CL_ANN, (KERN_WARNING
					"megaraid: 32-bit DMA mask failed\n"));
				goto out_free_sysfs_res;
			}
		}
	}

	// setup tasklet for DPC
	tasklet_init(&adapter->dpc_h, megaraid_mbox_dpc,
			(unsigned long)adapter);

	con_log(CL_DLEVEL1, (KERN_INFO
		"megaraid mbox hba successfully initialized\n"));

	return 0;

out_free_sysfs_res:
	megaraid_sysfs_free_resources(adapter);
out_free_irq:
	free_irq(adapter->irq, adapter);
out_alloc_cmds:
	megaraid_free_cmd_packets(adapter);
out_iounmap:
	iounmap(raid_dev->baseaddr);
out_release_regions:
	pci_release_regions(pdev);
out_free_raid_dev:
	kfree(raid_dev);

	return -1;
}


/**
 * megaraid_fini_mbox - undo controller initialization
 * @adapter		: our soft state
 */
static void
megaraid_fini_mbox(adapter_t *adapter)
{
	mraid_device_t *raid_dev = ADAP2RAIDDEV(adapter);

	// flush all caches
	megaraid_mbox_flush_cache(adapter);

	tasklet_kill(&adapter->dpc_h);

	megaraid_sysfs_free_resources(adapter);

	megaraid_free_cmd_packets(adapter);

	free_irq(adapter->irq, adapter);

	iounmap(raid_dev->baseaddr);

	pci_release_regions(adapter->pdev);

	kfree(raid_dev);

	return;
}


/**
 * megaraid_alloc_cmd_packets - allocate shared mailbox
 * @adapter		: soft state of the raid controller
 *
 * Allocate and align the shared mailbox. This maibox is used to issue
 * all the commands. For IO based controllers, the mailbox is also regsitered
 * with the FW. Allocate memory for all commands as well.
 * This is our big allocator.
 */
static int
megaraid_alloc_cmd_packets(adapter_t *adapter)
{
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	struct pci_dev		*pdev;
	unsigned long		align;
	scb_t			*scb;
	mbox_ccb_t		*ccb;
	struct mraid_pci_blk	*epthru_pci_blk;
	struct mraid_pci_blk	*sg_pci_blk;
	struct mraid_pci_blk	*mbox_pci_blk;
	int			i;

	pdev = adapter->pdev;

	/*
	 * Setup the mailbox
	 * Allocate the common 16-byte aligned memory for the handshake
	 * mailbox.
	 */
	raid_dev->una_mbox64 = pci_alloc_consistent(adapter->pdev,
			sizeof(mbox64_t), &raid_dev->una_mbox64_dma);

	if (!raid_dev->una_mbox64) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));
		return -1;
	}
	memset(raid_dev->una_mbox64, 0, sizeof(mbox64_t));

	/*
	 * Align the mailbox at 16-byte boundary
	 */
	raid_dev->mbox	= &raid_dev->una_mbox64->mbox32;

	raid_dev->mbox	= (mbox_t *)((((unsigned long)raid_dev->mbox) + 15) &
				(~0UL ^ 0xFUL));

	raid_dev->mbox64 = (mbox64_t *)(((unsigned long)raid_dev->mbox) - 8);

	align = ((void *)raid_dev->mbox -
			((void *)&raid_dev->una_mbox64->mbox32));

	raid_dev->mbox_dma = (unsigned long)raid_dev->una_mbox64_dma + 8 +
			align;

	// Allocate memory for commands issued internally
	adapter->ibuf = pci_alloc_consistent(pdev, MBOX_IBUF_SIZE,
				&adapter->ibuf_dma_h);
	if (!adapter->ibuf) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));

		goto out_free_common_mbox;
	}
	memset(adapter->ibuf, 0, MBOX_IBUF_SIZE);

	// Allocate memory for our SCSI Command Blocks and their associated
	// memory

	/*
	 * Allocate memory for the base list of scb. Later allocate memory for
	 * CCBs and embedded components of each CCB and point the pointers in
	 * scb to the allocated components
	 * NOTE: The code to allocate SCB will be duplicated in all the LLD
	 * since the calling routine does not yet know the number of available
	 * commands.
	 */
	adapter->kscb_list = kcalloc(MBOX_MAX_SCSI_CMDS, sizeof(scb_t), GFP_KERNEL);

	if (adapter->kscb_list == NULL) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));
		goto out_free_ibuf;
	}

	// memory allocation for our command packets
	if (megaraid_mbox_setup_dma_pools(adapter) != 0) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));
		goto out_free_scb_list;
	}

	// Adjust the scb pointers and link in the free pool
	epthru_pci_blk	= raid_dev->epthru_pool;
	sg_pci_blk	= raid_dev->sg_pool;
	mbox_pci_blk	= raid_dev->mbox_pool;

	for (i = 0; i < MBOX_MAX_SCSI_CMDS; i++) {
		scb			= adapter->kscb_list + i;
		ccb			= raid_dev->ccb_list + i;

		ccb->mbox	= (mbox_t *)(mbox_pci_blk[i].vaddr + 16);
		ccb->raw_mbox	= (uint8_t *)ccb->mbox;
		ccb->mbox64	= (mbox64_t *)(mbox_pci_blk[i].vaddr + 8);
		ccb->mbox_dma_h	= (unsigned long)mbox_pci_blk[i].dma_addr + 16;

		// make sure the mailbox is aligned properly
		if (ccb->mbox_dma_h & 0x0F) {
			con_log(CL_ANN, (KERN_CRIT
				"megaraid mbox: not aligned on 16-bytes\n"));

			goto out_teardown_dma_pools;
		}

		ccb->epthru		= (mraid_epassthru_t *)
						epthru_pci_blk[i].vaddr;
		ccb->epthru_dma_h	= epthru_pci_blk[i].dma_addr;
		ccb->pthru		= (mraid_passthru_t *)ccb->epthru;
		ccb->pthru_dma_h	= ccb->epthru_dma_h;


		ccb->sgl64		= (mbox_sgl64 *)sg_pci_blk[i].vaddr;
		ccb->sgl_dma_h		= sg_pci_blk[i].dma_addr;
		ccb->sgl32		= (mbox_sgl32 *)ccb->sgl64;

		scb->ccb		= (caddr_t)ccb;
		scb->gp			= 0;

		scb->sno		= i;	// command index

		scb->scp		= NULL;
		scb->state		= SCB_FREE;
		scb->dma_direction	= PCI_DMA_NONE;
		scb->dma_type		= MRAID_DMA_NONE;
		scb->dev_channel	= -1;
		scb->dev_target		= -1;

		// put scb in the free pool
		list_add_tail(&scb->list, &adapter->kscb_pool);
	}

	return 0;

out_teardown_dma_pools:
	megaraid_mbox_teardown_dma_pools(adapter);
out_free_scb_list:
	kfree(adapter->kscb_list);
out_free_ibuf:
	pci_free_consistent(pdev, MBOX_IBUF_SIZE, (void *)adapter->ibuf,
		adapter->ibuf_dma_h);
out_free_common_mbox:
	pci_free_consistent(adapter->pdev, sizeof(mbox64_t),
		(caddr_t)raid_dev->una_mbox64, raid_dev->una_mbox64_dma);

	return -1;
}


/**
 * megaraid_free_cmd_packets - free memory
 * @adapter		: soft state of the raid controller
 *
 * Release memory resources allocated for commands.
 */
static void
megaraid_free_cmd_packets(adapter_t *adapter)
{
	mraid_device_t *raid_dev = ADAP2RAIDDEV(adapter);

	megaraid_mbox_teardown_dma_pools(adapter);

	kfree(adapter->kscb_list);

	pci_free_consistent(adapter->pdev, MBOX_IBUF_SIZE,
		(void *)adapter->ibuf, adapter->ibuf_dma_h);

	pci_free_consistent(adapter->pdev, sizeof(mbox64_t),
		(caddr_t)raid_dev->una_mbox64, raid_dev->una_mbox64_dma);
	return;
}


/**
 * megaraid_mbox_setup_dma_pools - setup dma pool for command packets
 * @adapter		: HBA soft state
 *
 * Setup the dma pools for mailbox, passthru and extended passthru structures,
 * and scatter-gather lists.
 */
static int
megaraid_mbox_setup_dma_pools(adapter_t *adapter)
{
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	struct mraid_pci_blk	*epthru_pci_blk;
	struct mraid_pci_blk	*sg_pci_blk;
	struct mraid_pci_blk	*mbox_pci_blk;
	int			i;



	// Allocate memory for 16-bytes aligned mailboxes
	raid_dev->mbox_pool_handle = pci_pool_create("megaraid mbox pool",
						adapter->pdev,
						sizeof(mbox64_t) + 16,
						16, 0);

	if (raid_dev->mbox_pool_handle == NULL) {
		goto fail_setup_dma_pool;
	}

	mbox_pci_blk = raid_dev->mbox_pool;
	for (i = 0; i < MBOX_MAX_SCSI_CMDS; i++) {
		mbox_pci_blk[i].vaddr = pci_pool_alloc(
						raid_dev->mbox_pool_handle,
						GFP_KERNEL,
						&mbox_pci_blk[i].dma_addr);
		if (!mbox_pci_blk[i].vaddr) {
			goto fail_setup_dma_pool;
		}
	}

	/*
	 * Allocate memory for each embedded passthru strucuture pointer
	 * Request for a 128 bytes aligned structure for each passthru command
	 * structure
	 * Since passthru and extended passthru commands are exclusive, they
	 * share common memory pool. Passthru structures piggyback on memory
	 * allocted to extended passthru since passthru is smaller of the two
	 */
	raid_dev->epthru_pool_handle = pci_pool_create("megaraid mbox pthru",
			adapter->pdev, sizeof(mraid_epassthru_t), 128, 0);

	if (raid_dev->epthru_pool_handle == NULL) {
		goto fail_setup_dma_pool;
	}

	epthru_pci_blk = raid_dev->epthru_pool;
	for (i = 0; i < MBOX_MAX_SCSI_CMDS; i++) {
		epthru_pci_blk[i].vaddr = pci_pool_alloc(
						raid_dev->epthru_pool_handle,
						GFP_KERNEL,
						&epthru_pci_blk[i].dma_addr);
		if (!epthru_pci_blk[i].vaddr) {
			goto fail_setup_dma_pool;
		}
	}


	// Allocate memory for each scatter-gather list. Request for 512 bytes
	// alignment for each sg list
	raid_dev->sg_pool_handle = pci_pool_create("megaraid mbox sg",
					adapter->pdev,
					sizeof(mbox_sgl64) * MBOX_MAX_SG_SIZE,
					512, 0);

	if (raid_dev->sg_pool_handle == NULL) {
		goto fail_setup_dma_pool;
	}

	sg_pci_blk = raid_dev->sg_pool;
	for (i = 0; i < MBOX_MAX_SCSI_CMDS; i++) {
		sg_pci_blk[i].vaddr = pci_pool_alloc(
						raid_dev->sg_pool_handle,
						GFP_KERNEL,
						&sg_pci_blk[i].dma_addr);
		if (!sg_pci_blk[i].vaddr) {
			goto fail_setup_dma_pool;
		}
	}

	return 0;

fail_setup_dma_pool:
	megaraid_mbox_teardown_dma_pools(adapter);
	return -1;
}


/**
 * megaraid_mbox_teardown_dma_pools - teardown dma pools for command packets
 * @adapter		: HBA soft state
 *
 * Teardown the dma pool for mailbox, passthru and extended passthru
 * structures, and scatter-gather lists.
 */
static void
megaraid_mbox_teardown_dma_pools(adapter_t *adapter)
{
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	struct mraid_pci_blk	*epthru_pci_blk;
	struct mraid_pci_blk	*sg_pci_blk;
	struct mraid_pci_blk	*mbox_pci_blk;
	int			i;


	sg_pci_blk = raid_dev->sg_pool;
	for (i = 0; i < MBOX_MAX_SCSI_CMDS && sg_pci_blk[i].vaddr; i++) {
		pci_pool_free(raid_dev->sg_pool_handle, sg_pci_blk[i].vaddr,
			sg_pci_blk[i].dma_addr);
	}
	if (raid_dev->sg_pool_handle)
		pci_pool_destroy(raid_dev->sg_pool_handle);


	epthru_pci_blk = raid_dev->epthru_pool;
	for (i = 0; i < MBOX_MAX_SCSI_CMDS && epthru_pci_blk[i].vaddr; i++) {
		pci_pool_free(raid_dev->epthru_pool_handle,
			epthru_pci_blk[i].vaddr, epthru_pci_blk[i].dma_addr);
	}
	if (raid_dev->epthru_pool_handle)
		pci_pool_destroy(raid_dev->epthru_pool_handle);


	mbox_pci_blk = raid_dev->mbox_pool;
	for (i = 0; i < MBOX_MAX_SCSI_CMDS && mbox_pci_blk[i].vaddr; i++) {
		pci_pool_free(raid_dev->mbox_pool_handle,
			mbox_pci_blk[i].vaddr, mbox_pci_blk[i].dma_addr);
	}
	if (raid_dev->mbox_pool_handle)
		pci_pool_destroy(raid_dev->mbox_pool_handle);

	return;
}


/**
 * megaraid_alloc_scb - detach and return a scb from the free list
 * @adapter	: controller's soft state
 * @scp		: pointer to the scsi command to be executed
 *
 * Return the scb from the head of the free list. %NULL if there are none
 * available.
 */
static scb_t *
megaraid_alloc_scb(adapter_t *adapter, struct scsi_cmnd *scp)
{
	struct list_head	*head = &adapter->kscb_pool;
	scb_t			*scb = NULL;
	unsigned long		flags;

	// detach scb from free pool
	spin_lock_irqsave(SCSI_FREE_LIST_LOCK(adapter), flags);

	if (list_empty(head)) {
		spin_unlock_irqrestore(SCSI_FREE_LIST_LOCK(adapter), flags);
		return NULL;
	}

	scb = list_entry(head->next, scb_t, list);
	list_del_init(&scb->list);

	spin_unlock_irqrestore(SCSI_FREE_LIST_LOCK(adapter), flags);

	scb->state	= SCB_ACTIVE;
	scb->scp	= scp;
	scb->dma_type	= MRAID_DMA_NONE;

	return scb;
}


/**
 * megaraid_dealloc_scb - return the scb to the free pool
 * @adapter	: controller's soft state
 * @scb		: scb to be freed
 *
 * Return the scb back to the free list of scbs. The caller must 'flush' the
 * SCB before calling us. E.g., performing pci_unamp and/or pci_sync etc.
 * NOTE NOTE: Make sure the scb is not on any list before calling this
 * routine.
 */
static inline void
megaraid_dealloc_scb(adapter_t *adapter, scb_t *scb)
{
	unsigned long		flags;

	// put scb in the free pool
	scb->state	= SCB_FREE;
	scb->scp	= NULL;
	spin_lock_irqsave(SCSI_FREE_LIST_LOCK(adapter), flags);

	list_add(&scb->list, &adapter->kscb_pool);

	spin_unlock_irqrestore(SCSI_FREE_LIST_LOCK(adapter), flags);

	return;
}


/**
 * megaraid_mbox_mksgl - make the scatter-gather list
 * @adapter	: controller's soft state
 * @scb		: scsi control block
 *
 * Prepare the scatter-gather list.
 */
static int
megaraid_mbox_mksgl(adapter_t *adapter, scb_t *scb)
{
	struct scatterlist	*sgl;
	mbox_ccb_t		*ccb;
	struct scsi_cmnd	*scp;
	int			sgcnt;
	int			i;


	scp	= scb->scp;
	ccb	= (mbox_ccb_t *)scb->ccb;

	sgcnt = scsi_dma_map(scp);
	BUG_ON(sgcnt < 0 || sgcnt > adapter->sglen);

	// no mapping required if no data to be transferred
	if (!sgcnt)
		return 0;

	scb->dma_type = MRAID_DMA_WSG;

	scsi_for_each_sg(scp, sgl, sgcnt, i) {
		ccb->sgl64[i].address	= sg_dma_address(sgl);
		ccb->sgl64[i].length	= sg_dma_len(sgl);
	}

	// Return count of SG nodes
	return sgcnt;
}


/**
 * mbox_post_cmd - issue a mailbox command
 * @adapter	: controller's soft state
 * @scb		: command to be issued
 *
 * Post the command to the controller if mailbox is available.
 */
static int
mbox_post_cmd(adapter_t *adapter, scb_t *scb)
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	mbox64_t	*mbox64;
	mbox_t		*mbox;
	mbox_ccb_t	*ccb;
	unsigned long	flags;
	unsigned int	i = 0;


	ccb	= (mbox_ccb_t *)scb->ccb;
	mbox	= raid_dev->mbox;
	mbox64	= raid_dev->mbox64;

	/*
	 * Check for busy mailbox. If it is, return failure - the caller
	 * should retry later.
	 */
	spin_lock_irqsave(MAILBOX_LOCK(raid_dev), flags);

	if (unlikely(mbox->busy)) {
		do {
			udelay(1);
			i++;
			rmb();
		} while(mbox->busy && (i < max_mbox_busy_wait));

		if (mbox->busy) {

			spin_unlock_irqrestore(MAILBOX_LOCK(raid_dev), flags);

			return -1;
		}
	}


	// Copy this command's mailbox data into "adapter's" mailbox
	memcpy((caddr_t)mbox64, (caddr_t)ccb->mbox64, 22);
	mbox->cmdid = scb->sno;

	adapter->outstanding_cmds++;

	if (scb->dma_direction == PCI_DMA_TODEVICE)
		pci_dma_sync_sg_for_device(adapter->pdev,
					   scsi_sglist(scb->scp),
					   scsi_sg_count(scb->scp),
					   PCI_DMA_TODEVICE);

	mbox->busy	= 1;	// Set busy
	mbox->poll	= 0;
	mbox->ack	= 0;
	wmb();

	WRINDOOR(raid_dev, raid_dev->mbox_dma | 0x1);

	spin_unlock_irqrestore(MAILBOX_LOCK(raid_dev), flags);

	return 0;
}


/**
 * megaraid_queue_command - generic queue entry point for all LLDs
 * @scp		: pointer to the scsi command to be executed
 * @done	: callback routine to be called after the cmd has be completed
 *
 * Queue entry point for mailbox based controllers.
 */
static int
megaraid_queue_command(struct scsi_cmnd *scp, void (*done)(struct scsi_cmnd *))
{
	adapter_t	*adapter;
	scb_t		*scb;
	int		if_busy;

	adapter		= SCP2ADAPTER(scp);
	scp->scsi_done	= done;
	scp->result	= 0;

	/*
	 * Allocate and build a SCB request
	 * if_busy flag will be set if megaraid_mbox_build_cmd() command could
	 * not allocate scb. We will return non-zero status in that case.
	 * NOTE: scb can be null even though certain commands completed
	 * successfully, e.g., MODE_SENSE and TEST_UNIT_READY, it would
	 * return 0 in that case, and we would do the callback right away.
	 */
	if_busy	= 0;
	scb = megaraid_mbox_build_cmd(adapter, scp, &if_busy);
	if (!scb) {	// command already completed
		done(scp);
		return 0;
	}

	megaraid_mbox_runpendq(adapter, scb);
	return if_busy;
}

/**
 * megaraid_mbox_build_cmd - transform the mid-layer scsi commands
 * @adapter	: controller's soft state
 * @scp		: mid-layer scsi command pointer
 * @busy	: set if request could not be completed because of lack of
 *		resources
 *
 * Transform the mid-layer scsi command to megaraid firmware lingua.
 * Convert the command issued by mid-layer to format understood by megaraid
 * firmware. We also complete certain commands without sending them to firmware.
 */
static scb_t *
megaraid_mbox_build_cmd(adapter_t *adapter, struct scsi_cmnd *scp, int *busy)
{
	mraid_device_t		*rdev = ADAP2RAIDDEV(adapter);
	int			channel;
	int			target;
	int			islogical;
	mbox_ccb_t		*ccb;
	mraid_passthru_t	*pthru;
	mbox64_t		*mbox64;
	mbox_t			*mbox;
	scb_t			*scb;
	char			skip[] = "skipping";
	char			scan[] = "scanning";
	char			*ss;


	/*
	 * Get the appropriate device map for the device this command is
	 * intended for
	 */
	MRAID_GET_DEVICE_MAP(adapter, scp, channel, target, islogical);

	/*
	 * Logical drive commands
	 */
	if (islogical) {
		switch (scp->cmnd[0]) {
		case TEST_UNIT_READY:
			/*
			 * Do we support clustering and is the support enabled
			 * If no, return success always
			 */
			if (!adapter->ha) {
				scp->result = (DID_OK << 16);
				return NULL;
			}

			if (!(scb = megaraid_alloc_scb(adapter, scp))) {
				scp->result = (DID_ERROR << 16);
				*busy = 1;
				return NULL;
			}

			scb->dma_direction	= scp->sc_data_direction;
			scb->dev_channel	= 0xFF;
			scb->dev_target		= target;
			ccb			= (mbox_ccb_t *)scb->ccb;

			/*
			 * The command id will be provided by the command
			 * issuance routine
			 */
			ccb->raw_mbox[0]	= CLUSTER_CMD;
			ccb->raw_mbox[2]	= RESERVATION_STATUS;
			ccb->raw_mbox[3]	= target;

			return scb;

		case MODE_SENSE:
		{
			struct scatterlist	*sgl;
			caddr_t			vaddr;

			sgl = scsi_sglist(scp);
			if (sg_page(sgl)) {
				vaddr = (caddr_t) sg_virt(&sgl[0]);

				memset(vaddr, 0, scp->cmnd[4]);
			}
			else {
				con_log(CL_ANN, (KERN_WARNING
						 "megaraid mailbox: invalid sg:%d\n",
						 __LINE__));
			}
		}
		scp->result = (DID_OK << 16);
		return NULL;

		case INQUIRY:
			/*
			 * Display the channel scan for logical drives
			 * Do not display scan for a channel if already done.
			 */
			if (!(rdev->last_disp & (1L << SCP2CHANNEL(scp)))) {

				con_log(CL_ANN, (KERN_INFO
					"scsi[%d]: scanning scsi channel %d",
					adapter->host->host_no,
					SCP2CHANNEL(scp)));

				con_log(CL_ANN, (
					" [virtual] for logical drives\n"));

				rdev->last_disp |= (1L << SCP2CHANNEL(scp));
			}

			if (scp->cmnd[1] & MEGA_SCSI_INQ_EVPD) {
				scp->sense_buffer[0] = 0x70;
				scp->sense_buffer[2] = ILLEGAL_REQUEST;
				scp->sense_buffer[12] = MEGA_INVALID_FIELD_IN_CDB;
				scp->result = CHECK_CONDITION << 1;
				return NULL;
			}

			/* Fall through */

		case READ_CAPACITY:
			/*
			 * Do not allow LUN > 0 for logical drives and
			 * requests for more than 40 logical drives
			 */
			if (SCP2LUN(scp)) {
				scp->result = (DID_BAD_TARGET << 16);
				return NULL;
			}
			if ((target % 0x80) >= MAX_LOGICAL_DRIVES_40LD) {
				scp->result = (DID_BAD_TARGET << 16);
				return NULL;
			}


			/* Allocate a SCB and initialize passthru */
			if (!(scb = megaraid_alloc_scb(adapter, scp))) {
				scp->result = (DID_ERROR << 16);
				*busy = 1;
				return NULL;
			}

			ccb			= (mbox_ccb_t *)scb->ccb;
			scb->dev_channel	= 0xFF;
			scb->dev_target		= target;
			pthru			= ccb->pthru;
			mbox			= ccb->mbox;
			mbox64			= ccb->mbox64;

			pthru->timeout		= 0;
			pthru->ars		= 1;
			pthru->reqsenselen	= 14;
			pthru->islogical	= 1;
			pthru->logdrv		= target;
			pthru->cdblen		= scp->cmd_len;
			memcpy(pthru->cdb, scp->cmnd, scp->cmd_len);

			mbox->cmd		= MBOXCMD_PASSTHRU64;
			scb->dma_direction	= scp->sc_data_direction;

			pthru->dataxferlen	= scsi_bufflen(scp);
			pthru->dataxferaddr	= ccb->sgl_dma_h;
			pthru->numsge		= megaraid_mbox_mksgl(adapter,
							scb);

			mbox->xferaddr		= 0xFFFFFFFF;
			mbox64->xferaddr_lo	= (uint32_t )ccb->pthru_dma_h;
			mbox64->xferaddr_hi	= 0;

			return scb;

		case READ_6:
		case WRITE_6:
		case READ_10:
		case WRITE_10:
		case READ_12:
		case WRITE_12:

			/*
			 * Allocate a SCB and initialize mailbox
			 */
			if (!(scb = megaraid_alloc_scb(adapter, scp))) {
				scp->result = (DID_ERROR << 16);
				*busy = 1;
				return NULL;
			}
			ccb			= (mbox_ccb_t *)scb->ccb;
			scb->dev_channel	= 0xFF;
			scb->dev_target		= target;
			mbox			= ccb->mbox;
			mbox64			= ccb->mbox64;
			mbox->logdrv		= target;

			/*
			 * A little HACK: 2nd bit is zero for all scsi read
			 * commands and is set for all scsi write commands
			 */
			mbox->cmd = (scp->cmnd[0] & 0x02) ?  MBOXCMD_LWRITE64:
					MBOXCMD_LREAD64 ;

			/*
			 * 6-byte READ(0x08) or WRITE(0x0A) cdb
			 */
			if (scp->cmd_len == 6) {
				mbox->numsectors = (uint32_t)scp->cmnd[4];
				mbox->lba =
					((uint32_t)scp->cmnd[1] << 16)	|
					((uint32_t)scp->cmnd[2] << 8)	|
					(uint32_t)scp->cmnd[3];

				mbox->lba &= 0x1FFFFF;
			}

			/*
			 * 10-byte READ(0x28) or WRITE(0x2A) cdb
			 */
			else if (scp->cmd_len == 10) {
				mbox->numsectors =
					(uint32_t)scp->cmnd[8] |
					((uint32_t)scp->cmnd[7] << 8);
				mbox->lba =
					((uint32_t)scp->cmnd[2] << 24) |
					((uint32_t)scp->cmnd[3] << 16) |
					((uint32_t)scp->cmnd[4] << 8) |
					(uint32_t)scp->cmnd[5];
			}

			/*
			 * 12-byte READ(0xA8) or WRITE(0xAA) cdb
			 */
			else if (scp->cmd_len == 12) {
				mbox->lba =
					((uint32_t)scp->cmnd[2] << 24) |
					((uint32_t)scp->cmnd[3] << 16) |
					((uint32_t)scp->cmnd[4] << 8) |
					(uint32_t)scp->cmnd[5];

				mbox->numsectors =
					((uint32_t)scp->cmnd[6] << 24) |
					((uint32_t)scp->cmnd[7] << 16) |
					((uint32_t)scp->cmnd[8] << 8) |
					(uint32_t)scp->cmnd[9];
			}
			else {
				con_log(CL_ANN, (KERN_WARNING
					"megaraid: unsupported CDB length\n"));

				megaraid_dealloc_scb(adapter, scb);

				scp->result = (DID_ERROR << 16);
				return NULL;
			}

			scb->dma_direction = scp->sc_data_direction;

			// Calculate Scatter-Gather info
			mbox64->xferaddr_lo	= (uint32_t )ccb->sgl_dma_h;
			mbox->numsge		= megaraid_mbox_mksgl(adapter,
							scb);
			mbox->xferaddr		= 0xFFFFFFFF;
			mbox64->xferaddr_hi	= 0;

			return scb;

		case RESERVE:
		case RELEASE:
			/*
			 * Do we support clustering and is the support enabled
			 */
			if (!adapter->ha) {
				scp->result = (DID_BAD_TARGET << 16);
				return NULL;
			}

			/*
			 * Allocate a SCB and initialize mailbox
			 */
			if (!(scb = megaraid_alloc_scb(adapter, scp))) {
				scp->result = (DID_ERROR << 16);
				*busy = 1;
				return NULL;
			}

			ccb			= (mbox_ccb_t *)scb->ccb;
			scb->dev_channel	= 0xFF;
			scb->dev_target		= target;
			ccb->raw_mbox[0]	= CLUSTER_CMD;
			ccb->raw_mbox[2]	=  (scp->cmnd[0] == RESERVE) ?
						RESERVE_LD : RELEASE_LD;

			ccb->raw_mbox[3]	= target;
			scb->dma_direction	= scp->sc_data_direction;

			return scb;

		default:
			scp->result = (DID_BAD_TARGET << 16);
			return NULL;
		}
	}
	else { // Passthru device commands

		// Do not allow access to target id > 15 or LUN > 7
		if (target > 15 || SCP2LUN(scp) > 7) {
			scp->result = (DID_BAD_TARGET << 16);
			return NULL;
		}

		// if fast load option was set and scan for last device is
		// over, reset the fast_load flag so that during a possible
		// next scan, devices can be made available
		if (rdev->fast_load && (target == 15) &&
			(SCP2CHANNEL(scp) == adapter->max_channel -1)) {

			con_log(CL_ANN, (KERN_INFO
			"megaraid[%d]: physical device scan re-enabled\n",
				adapter->host->host_no));
			rdev->fast_load = 0;
		}

		/*
		 * Display the channel scan for physical devices
		 */
		if (!(rdev->last_disp & (1L << SCP2CHANNEL(scp)))) {

			ss = rdev->fast_load ? skip : scan;

			con_log(CL_ANN, (KERN_INFO
				"scsi[%d]: %s scsi channel %d [Phy %d]",
				adapter->host->host_no, ss, SCP2CHANNEL(scp),
				channel));

			con_log(CL_ANN, (
				" for non-raid devices\n"));

			rdev->last_disp |= (1L << SCP2CHANNEL(scp));
		}

		// disable channel sweep if fast load option given
		if (rdev->fast_load) {
			scp->result = (DID_BAD_TARGET << 16);
			return NULL;
		}

		// Allocate a SCB and initialize passthru
		if (!(scb = megaraid_alloc_scb(adapter, scp))) {
			scp->result = (DID_ERROR << 16);
			*busy = 1;
			return NULL;
		}

		ccb			= (mbox_ccb_t *)scb->ccb;
		scb->dev_channel	= channel;
		scb->dev_target		= target;
		scb->dma_direction	= scp->sc_data_direction;
		mbox			= ccb->mbox;
		mbox64			= ccb->mbox64;

		// Does this firmware support extended CDBs
		if (adapter->max_cdb_sz == 16) {
			mbox->cmd		= MBOXCMD_EXTPTHRU;

			megaraid_mbox_prepare_epthru(adapter, scb, scp);

			mbox64->xferaddr_lo	= (uint32_t)ccb->epthru_dma_h;
			mbox64->xferaddr_hi	= 0;
			mbox->xferaddr		= 0xFFFFFFFF;
		}
		else {
			mbox->cmd = MBOXCMD_PASSTHRU64;

			megaraid_mbox_prepare_pthru(adapter, scb, scp);

			mbox64->xferaddr_lo	= (uint32_t)ccb->pthru_dma_h;
			mbox64->xferaddr_hi	= 0;
			mbox->xferaddr		= 0xFFFFFFFF;
		}
		return scb;
	}

	// NOT REACHED
}


/**
 * megaraid_mbox_runpendq - execute commands queued in the pending queue
 * @adapter	: controller's soft state
 * @scb_q	: SCB to be queued in the pending list
 *
 * Scan the pending list for commands which are not yet issued and try to
 * post to the controller. The SCB can be a null pointer, which would indicate
 * no SCB to be queue, just try to execute the ones in the pending list.
 *
 * NOTE: We do not actually traverse the pending list. The SCBs are plucked
 * out from the head of the pending list. If it is successfully issued, the
 * next SCB is at the head now.
 */
static void
megaraid_mbox_runpendq(adapter_t *adapter, scb_t *scb_q)
{
	scb_t			*scb;
	unsigned long		flags;

	spin_lock_irqsave(PENDING_LIST_LOCK(adapter), flags);

	if (scb_q) {
		scb_q->state = SCB_PENDQ;
		list_add_tail(&scb_q->list, &adapter->pend_list);
	}

	// if the adapter in not in quiescent mode, post the commands to FW
	if (adapter->quiescent) {
		spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter), flags);
		return;
	}

	while (!list_empty(&adapter->pend_list)) {

		assert_spin_locked(PENDING_LIST_LOCK(adapter));

		scb = list_entry(adapter->pend_list.next, scb_t, list);

		// remove the scb from the pending list and try to
		// issue. If we are unable to issue it, put back in
		// the pending list and return

		list_del_init(&scb->list);

		spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter), flags);

		// if mailbox was busy, return SCB back to pending
		// list. Make sure to add at the head, since that's
		// where it would have been removed from

		scb->state = SCB_ISSUED;

		if (mbox_post_cmd(adapter, scb) != 0) {

			spin_lock_irqsave(PENDING_LIST_LOCK(adapter), flags);

			scb->state = SCB_PENDQ;

			list_add(&scb->list, &adapter->pend_list);

			spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter),
				flags);

			return;
		}

		spin_lock_irqsave(PENDING_LIST_LOCK(adapter), flags);
	}

	spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter), flags);


	return;
}


/**
 * megaraid_mbox_prepare_pthru - prepare a command for physical devices
 * @adapter	: pointer to controller's soft state
 * @scb		: scsi control block
 * @scp		: scsi command from the mid-layer
 *
 * Prepare a command for the scsi physical devices.
 */
static void
megaraid_mbox_prepare_pthru(adapter_t *adapter, scb_t *scb,
		struct scsi_cmnd *scp)
{
	mbox_ccb_t		*ccb;
	mraid_passthru_t	*pthru;
	uint8_t			channel;
	uint8_t			target;

	ccb	= (mbox_ccb_t *)scb->ccb;
	pthru	= ccb->pthru;
	channel	= scb->dev_channel;
	target	= scb->dev_target;

	// 0=6sec, 1=60sec, 2=10min, 3=3hrs, 4=NO timeout
	pthru->timeout		= 4;	
	pthru->ars		= 1;
	pthru->islogical	= 0;
	pthru->channel		= 0;
	pthru->target		= (channel << 4) | target;
	pthru->logdrv		= SCP2LUN(scp);
	pthru->reqsenselen	= 14;
	pthru->cdblen		= scp->cmd_len;

	memcpy(pthru->cdb, scp->cmnd, scp->cmd_len);

	if (scsi_bufflen(scp)) {
		pthru->dataxferlen	= scsi_bufflen(scp);
		pthru->dataxferaddr	= ccb->sgl_dma_h;
		pthru->numsge		= megaraid_mbox_mksgl(adapter, scb);
	}
	else {
		pthru->dataxferaddr	= 0;
		pthru->dataxferlen	= 0;
		pthru->numsge		= 0;
	}
	return;
}


/**
 * megaraid_mbox_prepare_epthru - prepare a command for physical devices
 * @adapter	: pointer to controller's soft state
 * @scb		: scsi control block
 * @scp		: scsi command from the mid-layer
 *
 * Prepare a command for the scsi physical devices. This rountine prepares
 * commands for devices which can take extended CDBs (>10 bytes).
 */
static void
megaraid_mbox_prepare_epthru(adapter_t *adapter, scb_t *scb,
		struct scsi_cmnd *scp)
{
	mbox_ccb_t		*ccb;
	mraid_epassthru_t	*epthru;
	uint8_t			channel;
	uint8_t			target;

	ccb	= (mbox_ccb_t *)scb->ccb;
	epthru	= ccb->epthru;
	channel	= scb->dev_channel;
	target	= scb->dev_target;

	// 0=6sec, 1=60sec, 2=10min, 3=3hrs, 4=NO timeout
	epthru->timeout		= 4;	
	epthru->ars		= 1;
	epthru->islogical	= 0;
	epthru->channel		= 0;
	epthru->target		= (channel << 4) | target;
	epthru->logdrv		= SCP2LUN(scp);
	epthru->reqsenselen	= 14;
	epthru->cdblen		= scp->cmd_len;

	memcpy(epthru->cdb, scp->cmnd, scp->cmd_len);

	if (scsi_bufflen(scp)) {
		epthru->dataxferlen	= scsi_bufflen(scp);
		epthru->dataxferaddr	= ccb->sgl_dma_h;
		epthru->numsge		= megaraid_mbox_mksgl(adapter, scb);
	}
	else {
		epthru->dataxferaddr	= 0;
		epthru->dataxferlen	= 0;
		epthru->numsge		= 0;
	}
	return;
}


/**
 * megaraid_ack_sequence - interrupt ack sequence for memory mapped HBAs
 * @adapter	: controller's soft state
 *
 * Interrupt acknowledgement sequence for memory mapped HBAs. Find out the
 * completed command and put them on the completed list for later processing.
 *
 * Returns:	1 if the interrupt is valid, 0 otherwise
 */
static int
megaraid_ack_sequence(adapter_t *adapter)
{
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	mbox_t			*mbox;
	scb_t			*scb;
	uint8_t			nstatus;
	uint8_t			completed[MBOX_MAX_FIRMWARE_STATUS];
	struct list_head	clist;
	int			handled;
	uint32_t		dword;
	unsigned long		flags;
	int			i, j;


	mbox	= raid_dev->mbox;

	// move the SCBs from the firmware completed array to our local list
	INIT_LIST_HEAD(&clist);

	// loop till F/W has more commands for us to complete
	handled = 0;
	spin_lock_irqsave(MAILBOX_LOCK(raid_dev), flags);
	do {
		/*
		 * Check if a valid interrupt is pending. If found, force the
		 * interrupt line low.
		 */
		dword = RDOUTDOOR(raid_dev);
		if (dword != 0x10001234) break;

		handled = 1;

		WROUTDOOR(raid_dev, 0x10001234);

		nstatus = 0;
		// wait for valid numstatus to post
		for (i = 0; i < 0xFFFFF; i++) {
			if (mbox->numstatus != 0xFF) {
				nstatus = mbox->numstatus;
				break;
			}
			rmb();
		}
		mbox->numstatus = 0xFF;

		adapter->outstanding_cmds -= nstatus;

		for (i = 0; i < nstatus; i++) {

			// wait for valid command index to post
			for (j = 0; j < 0xFFFFF; j++) {
				if (mbox->completed[i] != 0xFF) break;
				rmb();
			}
			completed[i]		= mbox->completed[i];
			mbox->completed[i]	= 0xFF;

			if (completed[i] == 0xFF) {
				con_log(CL_ANN, (KERN_CRIT
				"megaraid: command posting timed out\n"));

				BUG();
				continue;
			}

			// Get SCB associated with this command id
			if (completed[i] >= MBOX_MAX_SCSI_CMDS) {
				// a cmm command
				scb = adapter->uscb_list + (completed[i] -
						MBOX_MAX_SCSI_CMDS);
			}
			else {
				// an os command
				scb = adapter->kscb_list + completed[i];
			}

			scb->status = mbox->status;
			list_add_tail(&scb->list, &clist);
		}

		// Acknowledge interrupt
		WRINDOOR(raid_dev, 0x02);

	} while(1);

	spin_unlock_irqrestore(MAILBOX_LOCK(raid_dev), flags);


	// put the completed commands in the completed list. DPC would
	// complete these commands later
	spin_lock_irqsave(COMPLETED_LIST_LOCK(adapter), flags);

	list_splice(&clist, &adapter->completed_list);

	spin_unlock_irqrestore(COMPLETED_LIST_LOCK(adapter), flags);


	// schedule the DPC if there is some work for it
	if (handled)
		tasklet_schedule(&adapter->dpc_h);

	return handled;
}


/**
 * megaraid_isr - isr for memory based mailbox based controllers
 * @irq		: irq
 * @devp	: pointer to our soft state
 *
 * Interrupt service routine for memory-mapped mailbox controllers.
 */
static irqreturn_t
megaraid_isr(int irq, void *devp)
{
	adapter_t	*adapter = devp;
	int		handled;

	handled = megaraid_ack_sequence(adapter);

	/* Loop through any pending requests */
	if (!adapter->quiescent) {
		megaraid_mbox_runpendq(adapter, NULL);
	}

	return IRQ_RETVAL(handled);
}


/**
 * megaraid_mbox_sync_scb - sync kernel buffers
 * @adapter	: controller's soft state
 * @scb		: pointer to the resource packet
 *
 * DMA sync if required.
 */
static void
megaraid_mbox_sync_scb(adapter_t *adapter, scb_t *scb)
{
	mbox_ccb_t	*ccb;

	ccb	= (mbox_ccb_t *)scb->ccb;

	if (scb->dma_direction == PCI_DMA_FROMDEVICE)
		pci_dma_sync_sg_for_cpu(adapter->pdev,
					scsi_sglist(scb->scp),
					scsi_sg_count(scb->scp),
					PCI_DMA_FROMDEVICE);

	scsi_dma_unmap(scb->scp);
	return;
}


/**
 * megaraid_mbox_dpc - the tasklet to complete the commands from completed list
 * @devp	: pointer to HBA soft state
 *
 * Pick up the commands from the completed list and send back to the owners.
 * This is a reentrant function and does not assume any locks are held while
 * it is being called.
 */
static void
megaraid_mbox_dpc(unsigned long devp)
{
	adapter_t		*adapter = (adapter_t *)devp;
	mraid_device_t		*raid_dev;
	struct list_head	clist;
	struct scatterlist	*sgl;
	scb_t			*scb;
	scb_t			*tmp;
	struct scsi_cmnd	*scp;
	mraid_passthru_t	*pthru;
	mraid_epassthru_t	*epthru;
	mbox_ccb_t		*ccb;
	int			islogical;
	int			pdev_index;
	int			pdev_state;
	mbox_t			*mbox;
	unsigned long		flags;
	uint8_t			c;
	int			status;
	uioc_t			*kioc;


	if (!adapter) return;

	raid_dev = ADAP2RAIDDEV(adapter);

	// move the SCBs from the completed list to our local list
	INIT_LIST_HEAD(&clist);

	spin_lock_irqsave(COMPLETED_LIST_LOCK(adapter), flags);

	list_splice_init(&adapter->completed_list, &clist);

	spin_unlock_irqrestore(COMPLETED_LIST_LOCK(adapter), flags);


	list_for_each_entry_safe(scb, tmp, &clist, list) {

		status		= scb->status;
		scp		= scb->scp;
		ccb		= (mbox_ccb_t *)scb->ccb;
		pthru		= ccb->pthru;
		epthru		= ccb->epthru;
		mbox		= ccb->mbox;

		// Make sure f/w has completed a valid command
		if (scb->state != SCB_ISSUED) {
			con_log(CL_ANN, (KERN_CRIT
			"megaraid critical err: invalid command %d:%d:%p\n",
				scb->sno, scb->state, scp));
			BUG();
			continue;	// Must never happen!
		}

		// check for the management command and complete it right away
		if (scb->sno >= MBOX_MAX_SCSI_CMDS) {
			scb->state	= SCB_FREE;
			scb->status	= status;

			// remove from local clist
			list_del_init(&scb->list);

			kioc			= (uioc_t *)scb->gp;
			kioc->status		= 0;

			megaraid_mbox_mm_done(adapter, scb);

			continue;
		}

		// Was an abort issued for this command earlier
		if (scb->state & SCB_ABORT) {
			con_log(CL_ANN, (KERN_NOTICE
			"megaraid: aborted cmd %lx[%x] completed\n",
				scp->serial_number, scb->sno));
		}

		/*
		 * If the inquiry came of a disk drive which is not part of
		 * any RAID array, expose it to the kernel. For this to be
		 * enabled, user must set the "megaraid_expose_unconf_disks"
		 * flag to 1 by specifying it on module parameter list.
		 * This would enable data migration off drives from other
		 * configurations.
		 */
		islogical = MRAID_IS_LOGICAL(adapter, scp);
		if (scp->cmnd[0] == INQUIRY && status == 0 && islogical == 0
				&& IS_RAID_CH(raid_dev, scb->dev_channel)) {

			sgl = scsi_sglist(scp);
			if (sg_page(sgl)) {
				c = *(unsigned char *) sg_virt(&sgl[0]);
			} else {
				con_log(CL_ANN, (KERN_WARNING
						 "megaraid mailbox: invalid sg:%d\n",
						 __LINE__));
				c = 0;
			}

			if ((c & 0x1F ) == TYPE_DISK) {
				pdev_index = (scb->dev_channel * 16) +
					scb->dev_target;
				pdev_state =
					raid_dev->pdrv_state[pdev_index] & 0x0F;

				if (pdev_state == PDRV_ONLINE		||
					pdev_state == PDRV_FAILED	||
					pdev_state == PDRV_RBLD		||
					pdev_state == PDRV_HOTSPARE	||
					megaraid_expose_unconf_disks == 0) {

					status = 0xF0;
				}
			}
		}

		// Convert MegaRAID status to Linux error code
		switch (status) {

		case 0x00:

			scp->result = (DID_OK << 16);
			break;

		case 0x02:

			/* set sense_buffer and result fields */
			if (mbox->cmd == MBOXCMD_PASSTHRU ||
				mbox->cmd == MBOXCMD_PASSTHRU64) {

				memcpy(scp->sense_buffer, pthru->reqsensearea,
						14);

				scp->result = DRIVER_SENSE << 24 |
					DID_OK << 16 | CHECK_CONDITION << 1;
			}
			else {
				if (mbox->cmd == MBOXCMD_EXTPTHRU) {

					memcpy(scp->sense_buffer,
						epthru->reqsensearea, 14);

					scp->result = DRIVER_SENSE << 24 |
						DID_OK << 16 |
						CHECK_CONDITION << 1;
				} else {
					scp->sense_buffer[0] = 0x70;
					scp->sense_buffer[2] = ABORTED_COMMAND;
					scp->result = CHECK_CONDITION << 1;
				}
			}
			break;

		case 0x08:

			scp->result = DID_BUS_BUSY << 16 | status;
			break;

		default:

			/*
			 * If TEST_UNIT_READY fails, we know RESERVATION_STATUS
			 * failed
			 */
			if (scp->cmnd[0] == TEST_UNIT_READY) {
				scp->result = DID_ERROR << 16 |
					RESERVATION_CONFLICT << 1;
			}
			else
			/*
			 * Error code returned is 1 if Reserve or Release
			 * failed or the input parameter is invalid
			 */
			if (status == 1 && (scp->cmnd[0] == RESERVE ||
					 scp->cmnd[0] == RELEASE)) {

				scp->result = DID_ERROR << 16 |
					RESERVATION_CONFLICT << 1;
			}
			else {
				scp->result = DID_BAD_TARGET << 16 | status;
			}
		}

		// print a debug message for all failed commands
		if (status) {
			megaraid_mbox_display_scb(adapter, scb);
		}

		// Free our internal resources and call the mid-layer callback
		// routine
		megaraid_mbox_sync_scb(adapter, scb);

		// remove from local clist
		list_del_init(&scb->list);

		// put back in free list
		megaraid_dealloc_scb(adapter, scb);

		// send the scsi packet back to kernel
		scp->scsi_done(scp);
	}

	return;
}


/**
 * megaraid_abort_handler - abort the scsi command
 * @scp		: command to be aborted
 *
 * Abort a previous SCSI request. Only commands on the pending list can be
 * aborted. All the commands issued to the F/W must complete.
 **/
static int
megaraid_abort_handler(struct scsi_cmnd *scp)
{
	adapter_t		*adapter;
	mraid_device_t		*raid_dev;
	scb_t			*scb;
	scb_t			*tmp;
	int			found;
	unsigned long		flags;
	int			i;


	adapter		= SCP2ADAPTER(scp);
	raid_dev	= ADAP2RAIDDEV(adapter);

	con_log(CL_ANN, (KERN_WARNING
		"megaraid: aborting-%ld cmd=%x <c=%d t=%d l=%d>\n",
		scp->serial_number, scp->cmnd[0], SCP2CHANNEL(scp),
		SCP2TARGET(scp), SCP2LUN(scp)));

	// If FW has stopped responding, simply return failure
	if (raid_dev->hw_error) {
		con_log(CL_ANN, (KERN_NOTICE
			"megaraid: hw error, not aborting\n"));
		return FAILED;
	}

	// There might a race here, where the command was completed by the
	// firmware and now it is on the completed list. Before we could
	// complete the command to the kernel in dpc, the abort came.
	// Find out if this is the case to avoid the race.
	scb = NULL;
	spin_lock_irqsave(COMPLETED_LIST_LOCK(adapter), flags);
	list_for_each_entry_safe(scb, tmp, &adapter->completed_list, list) {

		if (scb->scp == scp) {	// Found command

			list_del_init(&scb->list);	// from completed list

			con_log(CL_ANN, (KERN_WARNING
			"megaraid: %ld:%d[%d:%d], abort from completed list\n",
				scp->serial_number, scb->sno,
				scb->dev_channel, scb->dev_target));

			scp->result = (DID_ABORT << 16);
			scp->scsi_done(scp);

			megaraid_dealloc_scb(adapter, scb);

			spin_unlock_irqrestore(COMPLETED_LIST_LOCK(adapter),
				flags);

			return SUCCESS;
		}
	}
	spin_unlock_irqrestore(COMPLETED_LIST_LOCK(adapter), flags);


	// Find out if this command is still on the pending list. If it is and
	// was never issued, abort and return success. If the command is owned
	// by the firmware, we must wait for it to complete by the FW.
	spin_lock_irqsave(PENDING_LIST_LOCK(adapter), flags);
	list_for_each_entry_safe(scb, tmp, &adapter->pend_list, list) {

		if (scb->scp == scp) {	// Found command

			list_del_init(&scb->list);	// from pending list

			ASSERT(!(scb->state & SCB_ISSUED));

			con_log(CL_ANN, (KERN_WARNING
				"megaraid abort: %ld[%d:%d], driver owner\n",
				scp->serial_number, scb->dev_channel,
				scb->dev_target));

			scp->result = (DID_ABORT << 16);
			scp->scsi_done(scp);

			megaraid_dealloc_scb(adapter, scb);

			spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter),
				flags);

			return SUCCESS;
		}
	}
	spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter), flags);


	// Check do we even own this command, in which case this would be
	// owned by the firmware. The only way to locate the FW scb is to
	// traverse through the list of all SCB, since driver does not
	// maintain these SCBs on any list
	found = 0;
	spin_lock_irq(&adapter->lock);
	for (i = 0; i < MBOX_MAX_SCSI_CMDS; i++) {
		scb = adapter->kscb_list + i;

		if (scb->scp == scp) {

			found = 1;

			if (!(scb->state & SCB_ISSUED)) {
				con_log(CL_ANN, (KERN_WARNING
				"megaraid abort: %ld%d[%d:%d], invalid state\n",
				scp->serial_number, scb->sno, scb->dev_channel,
				scb->dev_target));
				BUG();
			}
			else {
				con_log(CL_ANN, (KERN_WARNING
				"megaraid abort: %ld:%d[%d:%d], fw owner\n",
				scp->serial_number, scb->sno, scb->dev_channel,
				scb->dev_target));
			}
		}
	}
	spin_unlock_irq(&adapter->lock);

	if (!found) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid abort: scsi cmd:%ld, do now own\n",
			scp->serial_number));

		// FIXME: Should there be a callback for this command?
		return SUCCESS;
	}

	// We cannot actually abort a command owned by firmware, return
	// failure and wait for reset. In host reset handler, we will find out
	// if the HBA is still live
	return FAILED;
}

/**
 * megaraid_reset_handler - device reset hadler for mailbox based driver
 * @scp		: reference command
 *
 * Reset handler for the mailbox based controller. First try to find out if
 * the FW is still live, in which case the outstanding commands counter mut go
 * down to 0. If that happens, also issue the reservation reset command to
 * relinquish (possible) reservations on the logical drives connected to this
 * host.
 **/
static int
megaraid_reset_handler(struct scsi_cmnd *scp)
{
	adapter_t	*adapter;
	scb_t		*scb;
	scb_t		*tmp;
	mraid_device_t	*raid_dev;
	unsigned long	flags;
	uint8_t		raw_mbox[sizeof(mbox_t)];
	int		rval;
	int		recovery_window;
	int		recovering;
	int		i;
	uioc_t		*kioc;

	adapter		= SCP2ADAPTER(scp);
	raid_dev	= ADAP2RAIDDEV(adapter);

	// return failure if adapter is not responding
	if (raid_dev->hw_error) {
		con_log(CL_ANN, (KERN_NOTICE
			"megaraid: hw error, cannot reset\n"));
		return FAILED;
	}


	// Under exceptional conditions, FW can take up to 3 minutes to
	// complete command processing. Wait for additional 2 minutes for the
	// pending commands counter to go down to 0. If it doesn't, let the
	// controller be marked offline
	// Also, reset all the commands currently owned by the driver
	spin_lock_irqsave(PENDING_LIST_LOCK(adapter), flags);
	list_for_each_entry_safe(scb, tmp, &adapter->pend_list, list) {
		list_del_init(&scb->list);	// from pending list

		if (scb->sno >= MBOX_MAX_SCSI_CMDS) {
			con_log(CL_ANN, (KERN_WARNING
			"megaraid: IOCTL packet with %d[%d:%d] being reset\n",
			scb->sno, scb->dev_channel, scb->dev_target));

			scb->status = -1;

			kioc			= (uioc_t *)scb->gp;
			kioc->status		= -EFAULT;

			megaraid_mbox_mm_done(adapter, scb);
		} else {
			if (scb->scp == scp) {	// Found command
				con_log(CL_ANN, (KERN_WARNING
					"megaraid: %ld:%d[%d:%d], reset from pending list\n",
					scp->serial_number, scb->sno,
					scb->dev_channel, scb->dev_target));
			} else {
				con_log(CL_ANN, (KERN_WARNING
				"megaraid: IO packet with %d[%d:%d] being reset\n",
				scb->sno, scb->dev_channel, scb->dev_target));
			}

			scb->scp->result = (DID_RESET << 16);
			scb->scp->scsi_done(scb->scp);

			megaraid_dealloc_scb(adapter, scb);
		}
	}
	spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter), flags);

	if (adapter->outstanding_cmds) {
		con_log(CL_ANN, (KERN_NOTICE
			"megaraid: %d outstanding commands. Max wait %d sec\n",
			adapter->outstanding_cmds,
			(MBOX_RESET_WAIT + MBOX_RESET_EXT_WAIT)));
	}

	recovery_window = MBOX_RESET_WAIT + MBOX_RESET_EXT_WAIT;

	recovering = adapter->outstanding_cmds;

	for (i = 0; i < recovery_window; i++) {

		megaraid_ack_sequence(adapter);

		// print a message once every 5 seconds only
		if (!(i % 5)) {
			con_log(CL_ANN, (
			"megaraid mbox: Wait for %d commands to complete:%d\n",
				adapter->outstanding_cmds,
				(MBOX_RESET_WAIT + MBOX_RESET_EXT_WAIT) - i));
		}

		// bailout if no recovery happended in reset time
		if (adapter->outstanding_cmds == 0) {
			break;
		}

		msleep(1000);
	}

	spin_lock(&adapter->lock);

	// If still outstanding commands, bail out
	if (adapter->outstanding_cmds) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid mbox: critical hardware error!\n"));

		raid_dev->hw_error = 1;

		rval = FAILED;
		goto out;
	}
	else {
		con_log(CL_ANN, (KERN_NOTICE
		"megaraid mbox: reset sequence completed sucessfully\n"));
	}


	// If the controller supports clustering, reset reservations
	if (!adapter->ha) {
		rval = SUCCESS;
		goto out;
	}

	// clear reservations if any
	raw_mbox[0] = CLUSTER_CMD;
	raw_mbox[2] = RESET_RESERVATIONS;

	rval = SUCCESS;
	if (mbox_post_sync_cmd_fast(adapter, raw_mbox) == 0) {
		con_log(CL_ANN,
			(KERN_INFO "megaraid: reservation reset\n"));
	}
	else {
		rval = FAILED;
		con_log(CL_ANN, (KERN_WARNING
				"megaraid: reservation reset failed\n"));
	}

 out:
	spin_unlock_irq(&adapter->lock);
	return rval;
}

/*
 * START: internal commands library
 *
 * This section of the driver has the common routine used by the driver and
 * also has all the FW routines
 */

/**
 * mbox_post_sync_cmd() - blocking command to the mailbox based controllers
 * @adapter	: controller's soft state
 * @raw_mbox	: the mailbox
 *
 * Issue a scb in synchronous and non-interrupt mode for mailbox based
 * controllers.
 */
static int
mbox_post_sync_cmd(adapter_t *adapter, uint8_t raw_mbox[])
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	mbox64_t	*mbox64;
	mbox_t		*mbox;
	uint8_t		status;
	int		i;


	mbox64	= raid_dev->mbox64;
	mbox	= raid_dev->mbox;

	/*
	 * Wait until mailbox is free
	 */
	if (megaraid_busywait_mbox(raid_dev) != 0)
		goto blocked_mailbox;

	/*
	 * Copy mailbox data into host structure
	 */
	memcpy((caddr_t)mbox, (caddr_t)raw_mbox, 16);
	mbox->cmdid		= 0xFE;
	mbox->busy		= 1;
	mbox->poll		= 0;
	mbox->ack		= 0;
	mbox->numstatus		= 0xFF;
	mbox->status		= 0xFF;

	wmb();
	WRINDOOR(raid_dev, raid_dev->mbox_dma | 0x1);

	// wait for maximum 1 second for status to post. If the status is not
	// available within 1 second, assume FW is initializing and wait
	// for an extended amount of time
	if (mbox->numstatus == 0xFF) {	// status not yet available
		udelay(25);

		for (i = 0; mbox->numstatus == 0xFF && i < 1000; i++) {
			rmb();
			msleep(1);
		}


		if (i == 1000) {
			con_log(CL_ANN, (KERN_NOTICE
				"megaraid mailbox: wait for FW to boot      "));

			for (i = 0; (mbox->numstatus == 0xFF) &&
					(i < MBOX_RESET_WAIT); i++) {
				rmb();
				con_log(CL_ANN, ("\b\b\b\b\b[%03d]",
							MBOX_RESET_WAIT - i));
				msleep(1000);
			}

			if (i == MBOX_RESET_WAIT) {

				con_log(CL_ANN, (
				"\nmegaraid mailbox: status not available\n"));

				return -1;
			}
			con_log(CL_ANN, ("\b\b\b\b\b[ok] \n"));
		}
	}

	// wait for maximum 1 second for poll semaphore
	if (mbox->poll != 0x77) {
		udelay(25);

		for (i = 0; (mbox->poll != 0x77) && (i < 1000); i++) {
			rmb();
			msleep(1);
		}

		if (i == 1000) {
			con_log(CL_ANN, (KERN_WARNING
			"megaraid mailbox: could not get poll semaphore\n"));
			return -1;
		}
	}

	WRINDOOR(raid_dev, raid_dev->mbox_dma | 0x2);
	wmb();

	// wait for maximum 1 second for acknowledgement
	if (RDINDOOR(raid_dev) & 0x2) {
		udelay(25);

		for (i = 0; (RDINDOOR(raid_dev) & 0x2) && (i < 1000); i++) {
			rmb();
			msleep(1);
		}

		if (i == 1000) {
			con_log(CL_ANN, (KERN_WARNING
				"megaraid mailbox: could not acknowledge\n"));
			return -1;
		}
	}
	mbox->poll	= 0;
	mbox->ack	= 0x77;

	status = mbox->status;

	// invalidate the completed command id array. After command
	// completion, firmware would write the valid id.
	mbox->numstatus	= 0xFF;
	mbox->status	= 0xFF;
	for (i = 0; i < MBOX_MAX_FIRMWARE_STATUS; i++) {
		mbox->completed[i] = 0xFF;
	}

	return status;

blocked_mailbox:

	con_log(CL_ANN, (KERN_WARNING "megaraid: blocked mailbox\n") );
	return -1;
}


/**
 * mbox_post_sync_cmd_fast - blocking command to the mailbox based controllers
 * @adapter	: controller's soft state
 * @raw_mbox	: the mailbox
 *
 * Issue a scb in synchronous and non-interrupt mode for mailbox based
 * controllers. This is a faster version of the synchronous command and
 * therefore can be called in interrupt-context as well.
 */
static int
mbox_post_sync_cmd_fast(adapter_t *adapter, uint8_t raw_mbox[])
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	mbox_t		*mbox;
	long		i;


	mbox	= raid_dev->mbox;

	// return immediately if the mailbox is busy
	if (mbox->busy) return -1;

	// Copy mailbox data into host structure
	memcpy((caddr_t)mbox, (caddr_t)raw_mbox, 14);
	mbox->cmdid		= 0xFE;
	mbox->busy		= 1;
	mbox->poll		= 0;
	mbox->ack		= 0;
	mbox->numstatus		= 0xFF;
	mbox->status		= 0xFF;

	wmb();
	WRINDOOR(raid_dev, raid_dev->mbox_dma | 0x1);

	for (i = 0; i < MBOX_SYNC_WAIT_CNT; i++) {
		if (mbox->numstatus != 0xFF) break;
		rmb();
		udelay(MBOX_SYNC_DELAY_200);
	}

	if (i == MBOX_SYNC_WAIT_CNT) {
		// We may need to re-calibrate the counter
		con_log(CL_ANN, (KERN_CRIT
			"megaraid: fast sync command timed out\n"));
	}

	WRINDOOR(raid_dev, raid_dev->mbox_dma | 0x2);
	wmb();

	return mbox->status;
}


/**
 * megaraid_busywait_mbox() - Wait until the controller's mailbox is available
 * @raid_dev	: RAID device (HBA) soft state
 *
 * Wait until the controller's mailbox is available to accept more commands.
 * Wait for at most 1 second.
 */
static int
megaraid_busywait_mbox(mraid_device_t *raid_dev)
{
	mbox_t	*mbox = raid_dev->mbox;
	int	i = 0;

	if (mbox->busy) {
		udelay(25);
		for (i = 0; mbox->busy && i < 1000; i++)
			msleep(1);
	}

	if (i < 1000) return 0;
	else return -1;
}


/**
 * megaraid_mbox_product_info - some static information about the controller
 * @adapter	: our soft state
 *
 * Issue commands to the controller to grab some parameters required by our
 * caller.
 */
static int
megaraid_mbox_product_info(adapter_t *adapter)
{
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	mbox_t			*mbox;
	uint8_t			raw_mbox[sizeof(mbox_t)];
	mraid_pinfo_t		*pinfo;
	dma_addr_t		pinfo_dma_h;
	mraid_inquiry3_t	*mraid_inq3;
	int			i;


	memset((caddr_t)raw_mbox, 0, sizeof(raw_mbox));
	mbox = (mbox_t *)raw_mbox;

	/*
	 * Issue an ENQUIRY3 command to find out certain adapter parameters,
	 * e.g., max channels, max commands etc.
	 */
	pinfo = pci_alloc_consistent(adapter->pdev, sizeof(mraid_pinfo_t),
			&pinfo_dma_h);

	if (pinfo == NULL) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));

		return -1;
	}
	memset(pinfo, 0, sizeof(mraid_pinfo_t));

	mbox->xferaddr = (uint32_t)adapter->ibuf_dma_h;
	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	raw_mbox[0] = FC_NEW_CONFIG;
	raw_mbox[2] = NC_SUBOP_ENQUIRY3;
	raw_mbox[3] = ENQ3_GET_SOLICITED_FULL;

	// Issue the command
	if (mbox_post_sync_cmd(adapter, raw_mbox) != 0) {

		con_log(CL_ANN, (KERN_WARNING "megaraid: Inquiry3 failed\n"));

		pci_free_consistent(adapter->pdev, sizeof(mraid_pinfo_t),
			pinfo, pinfo_dma_h);

		return -1;
	}

	/*
	 * Collect information about state of each physical drive
	 * attached to the controller. We will expose all the disks
	 * which are not part of RAID
	 */
	mraid_inq3 = (mraid_inquiry3_t *)adapter->ibuf;
	for (i = 0; i < MBOX_MAX_PHYSICAL_DRIVES; i++) {
		raid_dev->pdrv_state[i] = mraid_inq3->pdrv_state[i];
	}

	/*
	 * Get product info for information like number of channels,
	 * maximum commands supported.
	 */
	memset((caddr_t)raw_mbox, 0, sizeof(raw_mbox));
	mbox->xferaddr = (uint32_t)pinfo_dma_h;

	raw_mbox[0] = FC_NEW_CONFIG;
	raw_mbox[2] = NC_SUBOP_PRODUCT_INFO;

	if (mbox_post_sync_cmd(adapter, raw_mbox) != 0) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: product info failed\n"));

		pci_free_consistent(adapter->pdev, sizeof(mraid_pinfo_t),
			pinfo, pinfo_dma_h);

		return -1;
	}

	/*
	 * Setup some parameters for host, as required by our caller
	 */
	adapter->max_channel = pinfo->nchannels;

	/*
	 * we will export all the logical drives on a single channel.
	 * Add 1 since inquires do not come for inititor ID
	 */
	adapter->max_target	= MAX_LOGICAL_DRIVES_40LD + 1;
	adapter->max_lun	= 8;	// up to 8 LUNs for non-disk devices

	/*
	 * These are the maximum outstanding commands for the scsi-layer
	 */
	adapter->max_cmds	= MBOX_MAX_SCSI_CMDS;

	memset(adapter->fw_version, 0, VERSION_SIZE);
	memset(adapter->bios_version, 0, VERSION_SIZE);

	memcpy(adapter->fw_version, pinfo->fw_version, 4);
	adapter->fw_version[4] = 0;

	memcpy(adapter->bios_version, pinfo->bios_version, 4);
	adapter->bios_version[4] = 0;

	con_log(CL_ANN, (KERN_NOTICE
		"megaraid: fw version:[%s] bios version:[%s]\n",
		adapter->fw_version, adapter->bios_version));

	pci_free_consistent(adapter->pdev, sizeof(mraid_pinfo_t), pinfo,
			pinfo_dma_h);

	return 0;
}



/**
 * megaraid_mbox_extended_cdb - check for support for extended CDBs
 * @adapter	: soft state for the controller
 *
 * This routine check whether the controller in question supports extended
 * ( > 10 bytes ) CDBs.
 */
static int
megaraid_mbox_extended_cdb(adapter_t *adapter)
{
	mbox_t		*mbox;
	uint8_t		raw_mbox[sizeof(mbox_t)];
	int		rval;

	mbox = (mbox_t *)raw_mbox;

	memset((caddr_t)raw_mbox, 0, sizeof(raw_mbox));
	mbox->xferaddr	= (uint32_t)adapter->ibuf_dma_h;

	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	raw_mbox[0] = MAIN_MISC_OPCODE;
	raw_mbox[2] = SUPPORT_EXT_CDB;

	/*
	 * Issue the command
	 */
	rval = 0;
	if (mbox_post_sync_cmd(adapter, raw_mbox) != 0) {
		rval = -1;
	}

	return rval;
}


/**
 * megaraid_mbox_support_ha - Do we support clustering
 * @adapter	: soft state for the controller
 * @init_id	: ID of the initiator
 *
 * Determine if the firmware supports clustering and the ID of the initiator.
 */
static int
megaraid_mbox_support_ha(adapter_t *adapter, uint16_t *init_id)
{
	mbox_t		*mbox;
	uint8_t		raw_mbox[sizeof(mbox_t)];
	int		rval;


	mbox = (mbox_t *)raw_mbox;

	memset((caddr_t)raw_mbox, 0, sizeof(raw_mbox));

	mbox->xferaddr = (uint32_t)adapter->ibuf_dma_h;

	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	raw_mbox[0] = GET_TARGET_ID;

	// Issue the command
	*init_id = 7;
	rval =  -1;
	if (mbox_post_sync_cmd(adapter, raw_mbox) == 0) {

		*init_id = *(uint8_t *)adapter->ibuf;

		con_log(CL_ANN, (KERN_INFO
			"megaraid: cluster firmware, initiator ID: %d\n",
			*init_id));

		rval =  0;
	}

	return rval;
}


/**
 * megaraid_mbox_support_random_del - Do we support random deletion
 * @adapter	: soft state for the controller
 *
 * Determine if the firmware supports random deletion.
 * Return:	1 is operation supported, 0 otherwise
 */
static int
megaraid_mbox_support_random_del(adapter_t *adapter)
{
	mbox_t		*mbox;
	uint8_t		raw_mbox[sizeof(mbox_t)];
	int		rval;


	mbox = (mbox_t *)raw_mbox;

	memset((caddr_t)raw_mbox, 0, sizeof(mbox_t));

	raw_mbox[0] = FC_DEL_LOGDRV;
	raw_mbox[2] = OP_SUP_DEL_LOGDRV;

	// Issue the command
	rval = 0;
	if (mbox_post_sync_cmd(adapter, raw_mbox) == 0) {

		con_log(CL_DLEVEL1, ("megaraid: supports random deletion\n"));

		rval =  1;
	}

	return rval;
}


/**
 * megaraid_mbox_get_max_sg - maximum sg elements supported by the firmware
 * @adapter	: soft state for the controller
 *
 * Find out the maximum number of scatter-gather elements supported by the
 * firmware.
 */
static int
megaraid_mbox_get_max_sg(adapter_t *adapter)
{
	mbox_t		*mbox;
	uint8_t		raw_mbox[sizeof(mbox_t)];
	int		nsg;


	mbox = (mbox_t *)raw_mbox;

	memset((caddr_t)raw_mbox, 0, sizeof(mbox_t));

	mbox->xferaddr = (uint32_t)adapter->ibuf_dma_h;

	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	raw_mbox[0] = MAIN_MISC_OPCODE;
	raw_mbox[2] = GET_MAX_SG_SUPPORT;

	// Issue the command
	if (mbox_post_sync_cmd(adapter, raw_mbox) == 0) {
		nsg =  *(uint8_t *)adapter->ibuf;
	}
	else {
		nsg =  MBOX_DEFAULT_SG_SIZE;
	}

	if (nsg > MBOX_MAX_SG_SIZE) nsg = MBOX_MAX_SG_SIZE;

	return nsg;
}


/**
 * megaraid_mbox_enum_raid_scsi - enumerate the RAID and SCSI channels
 * @adapter	: soft state for the controller
 *
 * Enumerate the RAID and SCSI channels for ROMB platforms so that channels
 * can be exported as regular SCSI channels.
 */
static void
megaraid_mbox_enum_raid_scsi(adapter_t *adapter)
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	mbox_t		*mbox;
	uint8_t		raw_mbox[sizeof(mbox_t)];


	mbox = (mbox_t *)raw_mbox;

	memset((caddr_t)raw_mbox, 0, sizeof(mbox_t));

	mbox->xferaddr = (uint32_t)adapter->ibuf_dma_h;

	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	raw_mbox[0] = CHNL_CLASS;
	raw_mbox[2] = GET_CHNL_CLASS;

	// Issue the command. If the command fails, all channels are RAID
	// channels
	raid_dev->channel_class = 0xFF;
	if (mbox_post_sync_cmd(adapter, raw_mbox) == 0) {
		raid_dev->channel_class =  *(uint8_t *)adapter->ibuf;
	}

	return;
}


/**
 * megaraid_mbox_flush_cache - flush adapter and disks cache
 * @adapter		: soft state for the controller
 *
 * Flush adapter cache followed by disks cache.
 */
static void
megaraid_mbox_flush_cache(adapter_t *adapter)
{
	mbox_t	*mbox;
	uint8_t	raw_mbox[sizeof(mbox_t)];


	mbox = (mbox_t *)raw_mbox;

	memset((caddr_t)raw_mbox, 0, sizeof(mbox_t));

	raw_mbox[0] = FLUSH_ADAPTER;

	if (mbox_post_sync_cmd(adapter, raw_mbox) != 0) {
		con_log(CL_ANN, ("megaraid: flush adapter failed\n"));
	}

	raw_mbox[0] = FLUSH_SYSTEM;

	if (mbox_post_sync_cmd(adapter, raw_mbox) != 0) {
		con_log(CL_ANN, ("megaraid: flush disks cache failed\n"));
	}

	return;
}


/**
 * megaraid_mbox_fire_sync_cmd - fire the sync cmd
 * @adapter		: soft state for the controller
 *
 * Clears the pending cmds in FW and reinits its RAID structs.
 */
static int
megaraid_mbox_fire_sync_cmd(adapter_t *adapter)
{
	mbox_t	*mbox;
	uint8_t	raw_mbox[sizeof(mbox_t)];
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	mbox64_t *mbox64;
	int	status = 0;
	int i;
	uint32_t dword;

	mbox = (mbox_t *)raw_mbox;

	memset((caddr_t)raw_mbox, 0, sizeof(mbox_t));

	raw_mbox[0] = 0xFF;

	mbox64	= raid_dev->mbox64;
	mbox	= raid_dev->mbox;

	/* Wait until mailbox is free */
	if (megaraid_busywait_mbox(raid_dev) != 0) {
		status = 1;
		goto blocked_mailbox;
	}

	/* Copy mailbox data into host structure */
	memcpy((caddr_t)mbox, (caddr_t)raw_mbox, 16);
	mbox->cmdid		= 0xFE;
	mbox->busy		= 1;
	mbox->poll		= 0;
	mbox->ack		= 0;
	mbox->numstatus		= 0;
	mbox->status		= 0;

	wmb();
	WRINDOOR(raid_dev, raid_dev->mbox_dma | 0x1);

	/* Wait for maximum 1 min for status to post.
	 * If the Firmware SUPPORTS the ABOVE COMMAND,
	 * mbox->cmd will be set to 0
	 * else
	 * the firmware will reject the command with
	 * mbox->numstatus set to 1
	 */

	i = 0;
	status = 0;
	while (!mbox->numstatus && mbox->cmd == 0xFF) {
		rmb();
		msleep(1);
		i++;
		if (i > 1000 * 60) {
			status = 1;
			break;
		}
	}
	if (mbox->numstatus == 1)
		status = 1; /*cmd not supported*/

	/* Check for interrupt line */
	dword = RDOUTDOOR(raid_dev);
	WROUTDOOR(raid_dev, dword);
	WRINDOOR(raid_dev,2);

	return status;

blocked_mailbox:
	con_log(CL_ANN, (KERN_WARNING "megaraid: blocked mailbox\n"));
	return status;
}

/**
 * megaraid_mbox_display_scb - display SCB information, mostly debug purposes
 * @adapter		: controller's soft state
 * @scb			: SCB to be displayed
 * @level		: debug level for console print
 *
 * Diplay information about the given SCB iff the current debug level is
 * verbose.
 */
static void
megaraid_mbox_display_scb(adapter_t *adapter, scb_t *scb)
{
	mbox_ccb_t		*ccb;
	struct scsi_cmnd	*scp;
	mbox_t			*mbox;
	int			level;
	int			i;


	ccb	= (mbox_ccb_t *)scb->ccb;
	scp	= scb->scp;
	mbox	= ccb->mbox;

	level = CL_DLEVEL3;

	con_log(level, (KERN_NOTICE
		"megaraid mailbox: status:%#x cmd:%#x id:%#x ", scb->status,
		mbox->cmd, scb->sno));

	con_log(level, ("sec:%#x lba:%#x addr:%#x ld:%d sg:%d\n",
		mbox->numsectors, mbox->lba, mbox->xferaddr, mbox->logdrv,
		mbox->numsge));

	if (!scp) return;

	con_log(level, (KERN_NOTICE "scsi cmnd: "));

	for (i = 0; i < scp->cmd_len; i++) {
		con_log(level, ("%#2.02x ", scp->cmnd[i]));
	}

	con_log(level, ("\n"));

	return;
}


/**
 * megaraid_mbox_setup_device_map - manage device ids
 * @adapter	: Driver's soft state
 *
 * Manange the device ids to have an appropraite mapping between the kernel
 * scsi addresses and megaraid scsi and logical drive addresses. We export
 * scsi devices on their actual addresses, whereas the logical drives are
 * exported on a virtual scsi channel.
 */
static void
megaraid_mbox_setup_device_map(adapter_t *adapter)
{
	uint8_t		c;
	uint8_t		t;

	/*
	 * First fill the values on the logical drive channel
	 */
	for (t = 0; t < LSI_MAX_LOGICAL_DRIVES_64LD; t++)
		adapter->device_ids[adapter->max_channel][t] =
			(t < adapter->init_id) ?  t : t - 1;

	adapter->device_ids[adapter->max_channel][adapter->init_id] = 0xFF;

	/*
	 * Fill the values on the physical devices channels
	 */
	for (c = 0; c < adapter->max_channel; c++)
		for (t = 0; t < LSI_MAX_LOGICAL_DRIVES_64LD; t++)
			adapter->device_ids[c][t] = (c << 8) | t;
}


/*
 * END: internal commands library
 */

/*
 * START: Interface for the common management module
 *
 * This is the module, which interfaces with the common management module to
 * provide support for ioctl and sysfs
 */

/**
 * megaraid_cmm_register - register with the management module
 * @adapter		: HBA soft state
 *
 * Register with the management module, which allows applications to issue
 * ioctl calls to the drivers. This interface is used by the management module
 * to setup sysfs support as well.
 */
static int
megaraid_cmm_register(adapter_t *adapter)
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	mraid_mmadp_t	adp;
	scb_t		*scb;
	mbox_ccb_t	*ccb;
	int		rval;
	int		i;

	// Allocate memory for the base list of scb for management module.
	adapter->uscb_list = kcalloc(MBOX_MAX_USER_CMDS, sizeof(scb_t), GFP_KERNEL);

	if (adapter->uscb_list == NULL) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));
		return -1;
	}


	// Initialize the synchronization parameters for resources for
	// commands for management module
	INIT_LIST_HEAD(&adapter->uscb_pool);

	spin_lock_init(USER_FREE_LIST_LOCK(adapter));



	// link all the packets. Note, CCB for commands, coming from the
	// commom management module, mailbox physical address are already
	// setup by it. We just need placeholder for that in our local command
	// control blocks
	for (i = 0; i < MBOX_MAX_USER_CMDS; i++) {

		scb			= adapter->uscb_list + i;
		ccb			= raid_dev->uccb_list + i;

		scb->ccb		= (caddr_t)ccb;
		ccb->mbox64		= raid_dev->umbox64 + i;
		ccb->mbox		= &ccb->mbox64->mbox32;
		ccb->raw_mbox		= (uint8_t *)ccb->mbox;

		scb->gp			= 0;

		// COMMAND ID 0 - (MBOX_MAX_SCSI_CMDS-1) ARE RESERVED FOR
		// COMMANDS COMING FROM IO SUBSYSTEM (MID-LAYER)
		scb->sno		= i + MBOX_MAX_SCSI_CMDS;

		scb->scp		= NULL;
		scb->state		= SCB_FREE;
		scb->dma_direction	= PCI_DMA_NONE;
		scb->dma_type		= MRAID_DMA_NONE;
		scb->dev_channel	= -1;
		scb->dev_target		= -1;

		// put scb in the free pool
		list_add_tail(&scb->list, &adapter->uscb_pool);
	}

	adp.unique_id		= adapter->unique_id;
	adp.drvr_type		= DRVRTYPE_MBOX;
	adp.drvr_data		= (unsigned long)adapter;
	adp.pdev		= adapter->pdev;
	adp.issue_uioc		= megaraid_mbox_mm_handler;
	adp.timeout		= MBOX_RESET_WAIT + MBOX_RESET_EXT_WAIT;
	adp.max_kioc		= MBOX_MAX_USER_CMDS;

	if ((rval = mraid_mm_register_adp(&adp)) != 0) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid mbox: did not register with CMM\n"));

		kfree(adapter->uscb_list);
	}

	return rval;
}


/**
 * megaraid_cmm_unregister - un-register with the management module
 * @adapter		: HBA soft state
 *
 * Un-register with the management module.
 * FIXME: mgmt module must return failure for unregister if it has pending
 * commands in LLD.
 */
static int
megaraid_cmm_unregister(adapter_t *adapter)
{
	kfree(adapter->uscb_list);
	mraid_mm_unregister_adp(adapter->unique_id);
	return 0;
}


/**
 * megaraid_mbox_mm_handler - interface for CMM to issue commands to LLD
 * @drvr_data		: LLD specific data
 * @kioc		: CMM interface packet
 * @action		: command action
 *
 * This routine is invoked whenever the Common Management Module (CMM) has a
 * command for us. The 'action' parameter specifies if this is a new command
 * or otherwise.
 */
static int
megaraid_mbox_mm_handler(unsigned long drvr_data, uioc_t *kioc, uint32_t action)
{
	adapter_t *adapter;

	if (action != IOCTL_ISSUE) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: unsupported management action:%#2x\n",
			action));
		return (-ENOTSUPP);
	}

	adapter = (adapter_t *)drvr_data;

	// make sure this adapter is not being detached right now.
	if (atomic_read(&adapter->being_detached)) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: reject management request, detaching\n"));
		return (-ENODEV);
	}

	switch (kioc->opcode) {

	case GET_ADAP_INFO:

		kioc->status =  gather_hbainfo(adapter, (mraid_hba_info_t *)
					(unsigned long)kioc->buf_vaddr);

		kioc->done(kioc);

		return kioc->status;

	case MBOX_CMD:

		return megaraid_mbox_mm_command(adapter, kioc);

	default:
		kioc->status = (-EINVAL);
		kioc->done(kioc);
		return (-EINVAL);
	}

	return 0;	// not reached
}

/**
 * megaraid_mbox_mm_command - issues commands routed through CMM
 * @adapter		: HBA soft state
 * @kioc		: management command packet
 *
 * Issues commands, which are routed through the management module.
 */
static int
megaraid_mbox_mm_command(adapter_t *adapter, uioc_t *kioc)
{
	struct list_head	*head = &adapter->uscb_pool;
	mbox64_t		*mbox64;
	uint8_t			*raw_mbox;
	scb_t			*scb;
	mbox_ccb_t		*ccb;
	unsigned long		flags;

	// detach one scb from free pool
	spin_lock_irqsave(USER_FREE_LIST_LOCK(adapter), flags);

	if (list_empty(head)) {	// should never happen because of CMM

		con_log(CL_ANN, (KERN_WARNING
			"megaraid mbox: bug in cmm handler, lost resources\n"));

		spin_unlock_irqrestore(USER_FREE_LIST_LOCK(adapter), flags);

		return (-EINVAL);
	}

	scb = list_entry(head->next, scb_t, list);
	list_del_init(&scb->list);

	spin_unlock_irqrestore(USER_FREE_LIST_LOCK(adapter), flags);

	scb->state		= SCB_ACTIVE;
	scb->dma_type		= MRAID_DMA_NONE;
	scb->dma_direction	= PCI_DMA_NONE;

	ccb		= (mbox_ccb_t *)scb->ccb;
	mbox64		= (mbox64_t *)(unsigned long)kioc->cmdbuf;
	raw_mbox	= (uint8_t *)&mbox64->mbox32;

	memcpy(ccb->mbox64, mbox64, sizeof(mbox64_t));

	scb->gp		= (unsigned long)kioc;

	/*
	 * If it is a logdrv random delete operation, we have to wait till
	 * there are no outstanding cmds at the fw and then issue it directly
	 */
	if (raw_mbox[0] == FC_DEL_LOGDRV && raw_mbox[2] == OP_DEL_LOGDRV) {

		if (wait_till_fw_empty(adapter)) {
			con_log(CL_ANN, (KERN_NOTICE
				"megaraid mbox: LD delete, timed out\n"));

			kioc->status = -ETIME;

			scb->status = -1;

			megaraid_mbox_mm_done(adapter, scb);

			return (-ETIME);
		}

		INIT_LIST_HEAD(&scb->list);

		scb->state = SCB_ISSUED;
		if (mbox_post_cmd(adapter, scb) != 0) {

			con_log(CL_ANN, (KERN_NOTICE
				"megaraid mbox: LD delete, mailbox busy\n"));

			kioc->status = -EBUSY;

			scb->status = -1;

			megaraid_mbox_mm_done(adapter, scb);

			return (-EBUSY);
		}

		return 0;
	}

	// put the command on the pending list and execute
	megaraid_mbox_runpendq(adapter, scb);

	return 0;
}


static int
wait_till_fw_empty(adapter_t *adapter)
{
	unsigned long	flags = 0;
	int		i;


	/*
	 * Set the quiescent flag to stop issuing cmds to FW.
	 */
	spin_lock_irqsave(&adapter->lock, flags);
	adapter->quiescent++;
	spin_unlock_irqrestore(&adapter->lock, flags);

	/*
	 * Wait till there are no more cmds outstanding at FW. Try for at most
	 * 60 seconds
	 */
	for (i = 0; i < 60 && adapter->outstanding_cmds; i++) {
		con_log(CL_DLEVEL1, (KERN_INFO
			"megaraid: FW has %d pending commands\n",
			adapter->outstanding_cmds));

		msleep(1000);
	}

	return adapter->outstanding_cmds;
}


/**
 * megaraid_mbox_mm_done - callback for CMM commands
 * @adapter	: HBA soft state
 * @scb		: completed command
 *
 * Callback routine for internal commands originated from the management
 * module.
 */
static void
megaraid_mbox_mm_done(adapter_t *adapter, scb_t *scb)
{
	uioc_t			*kioc;
	mbox64_t		*mbox64;
	uint8_t			*raw_mbox;
	unsigned long		flags;

	kioc			= (uioc_t *)scb->gp;
	mbox64			= (mbox64_t *)(unsigned long)kioc->cmdbuf;
	mbox64->mbox32.status	= scb->status;
	raw_mbox		= (uint8_t *)&mbox64->mbox32;


	// put scb in the free pool
	scb->state	= SCB_FREE;
	scb->scp	= NULL;

	spin_lock_irqsave(USER_FREE_LIST_LOCK(adapter), flags);

	list_add(&scb->list, &adapter->uscb_pool);

	spin_unlock_irqrestore(USER_FREE_LIST_LOCK(adapter), flags);

	// if a delete logical drive operation succeeded, restart the
	// controller
	if (raw_mbox[0] == FC_DEL_LOGDRV && raw_mbox[2] == OP_DEL_LOGDRV) {

		adapter->quiescent--;

		megaraid_mbox_runpendq(adapter, NULL);
	}

	kioc->done(kioc);

	return;
}


/**
 * gather_hbainfo - HBA characteristics for the applications
 * @adapter		: HBA soft state
 * @hinfo		: pointer to the caller's host info strucuture
 */
static int
gather_hbainfo(adapter_t *adapter, mraid_hba_info_t *hinfo)
{
	uint8_t	dmajor;

	dmajor			= megaraid_mbox_version[0];

	hinfo->pci_vendor_id	= adapter->pdev->vendor;
	hinfo->pci_device_id	= adapter->pdev->device;
	hinfo->subsys_vendor_id	= adapter->pdev->subsystem_vendor;
	hinfo->subsys_device_id	= adapter->pdev->subsystem_device;

	hinfo->pci_bus		= adapter->pdev->bus->number;
	hinfo->pci_dev_fn	= adapter->pdev->devfn;
	hinfo->pci_slot		= PCI_SLOT(adapter->pdev->devfn);
	hinfo->irq		= adapter->host->irq;
	hinfo->baseport		= ADAP2RAIDDEV(adapter)->baseport;

	hinfo->unique_id	= (hinfo->pci_bus << 8) | adapter->pdev->devfn;
	hinfo->host_no		= adapter->host->host_no;

	return 0;
}

/*
 * END: Interface for the common management module
 */



/**
 * megaraid_sysfs_alloc_resources - allocate sysfs related resources
 * @adapter	: controller's soft state
 *
 * Allocate packets required to issue FW calls whenever the sysfs attributes
 * are read. These attributes would require up-to-date information from the
 * FW. Also set up resources for mutual exclusion to share these resources and
 * the wait queue.
 *
 * Return 0 on success.
 * Return -ERROR_CODE on failure.
 */
static int
megaraid_sysfs_alloc_resources(adapter_t *adapter)
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	int		rval = 0;

	raid_dev->sysfs_uioc = kmalloc(sizeof(uioc_t), GFP_KERNEL);

	raid_dev->sysfs_mbox64 = kmalloc(sizeof(mbox64_t), GFP_KERNEL);

	raid_dev->sysfs_buffer = pci_alloc_consistent(adapter->pdev,
			PAGE_SIZE, &raid_dev->sysfs_buffer_dma);

	if (!raid_dev->sysfs_uioc || !raid_dev->sysfs_mbox64 ||
		!raid_dev->sysfs_buffer) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));

		rval = -ENOMEM;

		megaraid_sysfs_free_resources(adapter);
	}

	mutex_init(&raid_dev->sysfs_mtx);

	init_waitqueue_head(&raid_dev->sysfs_wait_q);

	return rval;
}


/**
 * megaraid_sysfs_free_resources - free sysfs related resources
 * @adapter	: controller's soft state
 *
 * Free packets allocated for sysfs FW commands
 */
static void
megaraid_sysfs_free_resources(adapter_t *adapter)
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);

	kfree(raid_dev->sysfs_uioc);
	kfree(raid_dev->sysfs_mbox64);

	if (raid_dev->sysfs_buffer) {
		pci_free_consistent(adapter->pdev, PAGE_SIZE,
			raid_dev->sysfs_buffer, raid_dev->sysfs_buffer_dma);
	}
}


/**
 * megaraid_sysfs_get_ldmap_done - callback for get ldmap
 * @uioc	: completed packet
 *
 * Callback routine called in the ISR/tasklet context for get ldmap call
 */
static void
megaraid_sysfs_get_ldmap_done(uioc_t *uioc)
{
	adapter_t	*adapter = (adapter_t *)uioc->buf_vaddr;
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);

	uioc->status = 0;

	wake_up(&raid_dev->sysfs_wait_q);
}


/**
 * megaraid_sysfs_get_ldmap_timeout - timeout handling for get ldmap
 * @data	: timed out packet
 *
 * Timeout routine to recover and return to application, in case the adapter
 * has stopped responding. A timeout of 60 seconds for this command seems like
 * a good value.
 */
static void
megaraid_sysfs_get_ldmap_timeout(unsigned long data)
{
	uioc_t		*uioc = (uioc_t *)data;
	adapter_t	*adapter = (adapter_t *)uioc->buf_vaddr;
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);

	uioc->status = -ETIME;

	wake_up(&raid_dev->sysfs_wait_q);
}


/**
 * megaraid_sysfs_get_ldmap - get update logical drive map
 * @adapter	: controller's soft state
 *
 * This routine will be called whenever user reads the logical drive
 * attributes, go get the current logical drive mapping table from the
 * firmware. We use the management API's to issue commands to the controller.
 *
 * NOTE: The commands issuance functionality is not generalized and
 * implemented in context of "get ld map" command only. If required, the
 * command issuance logical can be trivially pulled out and implemented as a
 * standalone libary. For now, this should suffice since there is no other
 * user of this interface.
 *
 * Return 0 on success.
 * Return -1 on failure.
 */
static int
megaraid_sysfs_get_ldmap(adapter_t *adapter)
{
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	uioc_t			*uioc;
	mbox64_t		*mbox64;
	mbox_t			*mbox;
	char			*raw_mbox;
	struct timer_list	sysfs_timer;
	struct timer_list	*timerp;
	caddr_t			ldmap;
	int			rval = 0;

	/*
	 * Allow only one read at a time to go through the sysfs attributes
	 */
	mutex_lock(&raid_dev->sysfs_mtx);

	uioc	= raid_dev->sysfs_uioc;
	mbox64	= raid_dev->sysfs_mbox64;
	ldmap	= raid_dev->sysfs_buffer;

	memset(uioc, 0, sizeof(uioc_t));
	memset(mbox64, 0, sizeof(mbox64_t));
	memset(ldmap, 0, sizeof(raid_dev->curr_ldmap));

	mbox		= &mbox64->mbox32;
	raw_mbox	= (char *)mbox;
	uioc->cmdbuf    = (uint64_t)(unsigned long)mbox64;
	uioc->buf_vaddr	= (caddr_t)adapter;
	uioc->status	= -ENODATA;
	uioc->done	= megaraid_sysfs_get_ldmap_done;

	/*
	 * Prepare the mailbox packet to get the current logical drive mapping
	 * table
	 */
	mbox->xferaddr = (uint32_t)raid_dev->sysfs_buffer_dma;

	raw_mbox[0] = FC_DEL_LOGDRV;
	raw_mbox[2] = OP_GET_LDID_MAP;

	/*
	 * Setup a timer to recover from a non-responding controller
	 */
	timerp	= &sysfs_timer;
	init_timer(timerp);

	timerp->function	= megaraid_sysfs_get_ldmap_timeout;
	timerp->data		= (unsigned long)uioc;
	timerp->expires		= jiffies + 60 * HZ;

	add_timer(timerp);

	/*
	 * Send the command to the firmware
	 */
	rval = megaraid_mbox_mm_command(adapter, uioc);

	if (rval == 0) {	// command successfully issued
		wait_event(raid_dev->sysfs_wait_q, (uioc->status != -ENODATA));

		/*
		 * Check if the command timed out
		 */
		if (uioc->status == -ETIME) {
			con_log(CL_ANN, (KERN_NOTICE
				"megaraid: sysfs get ld map timed out\n"));

			rval = -ETIME;
		}
		else {
			rval = mbox->status;
		}

		if (rval == 0) {
			memcpy(raid_dev->curr_ldmap, ldmap,
				sizeof(raid_dev->curr_ldmap));
		}
		else {
			con_log(CL_ANN, (KERN_NOTICE
				"megaraid: get ld map failed with %x\n", rval));
		}
	}
	else {
		con_log(CL_ANN, (KERN_NOTICE
			"megaraid: could not issue ldmap command:%x\n", rval));
	}


	del_timer_sync(timerp);

	mutex_unlock(&raid_dev->sysfs_mtx);

	return rval;
}


/**
 * megaraid_sysfs_show_app_hndl - display application handle for this adapter
 * @cdev	: class device object representation for the host
 * @buf		: buffer to send data to
 *
 * Display the handle used by the applications while executing management
 * tasks on the adapter. We invoke a management module API to get the adapter
 * handle, since we do not interface with applications directly.
 */
static ssize_t
megaraid_sysfs_show_app_hndl(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	adapter_t	*adapter = (adapter_t *)SCSIHOST2ADAP(shost);
	uint32_t	app_hndl;

	app_hndl = mraid_mm_adapter_app_handle(adapter->unique_id);

	return snprintf(buf, 8, "%u\n", app_hndl);
}


/**
 * megaraid_sysfs_show_ldnum - display the logical drive number for this device
 * @dev		: device object representation for the scsi device
 * @attr	: device attribute to show
 * @buf		: buffer to send data to
 *
 * Display the logical drive number for the device in question, if it a valid
 * logical drive. For physical devices, "-1" is returned.
 *
 * The logical drive number is displayed in following format:
 *
 * <SCSI ID> <LD NUM> <LD STICKY ID> <APP ADAPTER HANDLE>
 *
 *   <int>     <int>       <int>            <int>
 */
static ssize_t
megaraid_sysfs_show_ldnum(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	adapter_t	*adapter = (adapter_t *)SCSIHOST2ADAP(sdev->host);
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	int		scsi_id = -1;
	int		logical_drv = -1;
	int		ldid_map = -1;
	uint32_t	app_hndl = 0;
	int		mapped_sdev_id;
	int		rval;
	int		i;

	if (raid_dev->random_del_supported &&
			MRAID_IS_LOGICAL_SDEV(adapter, sdev)) {

		rval = megaraid_sysfs_get_ldmap(adapter);
		if (rval == 0) {

			for (i = 0; i < MAX_LOGICAL_DRIVES_40LD; i++) {

				mapped_sdev_id = sdev->id;

				if (sdev->id > adapter->init_id) {
					mapped_sdev_id -= 1;
				}

				if (raid_dev->curr_ldmap[i] == mapped_sdev_id) {

					scsi_id = sdev->id;

					logical_drv = i;

					ldid_map = raid_dev->curr_ldmap[i];

					app_hndl = mraid_mm_adapter_app_handle(
							adapter->unique_id);

					break;
				}
			}
		}
		else {
			con_log(CL_ANN, (KERN_NOTICE
				"megaraid: sysfs get ld map failed: %x\n",
				rval));
		}
	}

	return snprintf(buf, 36, "%d %d %d %d\n", scsi_id, logical_drv,
			ldid_map, app_hndl);
}


/*
 * END: Mailbox Low Level Driver
 */
module_init(megaraid_init);
module_exit(megaraid_exit);

/* vim: set ts=8 sw=8 tw=78 ai si: */
