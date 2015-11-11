/*
 * Scsi Host Layer for MPT (Message Passing Technology) based controllers
 *
 * Copyright (C) 2012-2014  LSI Corporation
 * Copyright (C) 2013-2015 Avago Technologies
 *  (mailto: MPT-FusionLinux.pdl@avagotech.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * NO WARRANTY
 * THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 * LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 * solely responsible for determining the appropriateness of using and
 * distributing the Program and assumes all risks associated with its
 * exercise of rights under this Agreement, including but not limited to
 * the risks and costs of program errors, damage to or loss of data,
 * programs or equipment, and unavailability or interruption of operations.

 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

 * You should have received a copy of the GNU General Public License
 * along with this program.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/raid_class.h>

#include "mpt3sas_base.h"
#include "mpt3sas_ctl.h"

MODULE_AUTHOR(MPT3SAS_AUTHOR);
MODULE_DESCRIPTION(MPT2SAS_DESCRIPTION);
MODULE_LICENSE("GPL");
MODULE_VERSION(MPT2SAS_DRIVER_VERSION);

/* shost template */
static struct scsi_host_template mpt2sas_driver_template = {
	.module				= THIS_MODULE,
	.name				= "Fusion MPT SAS Host",
	.proc_name			= MPT2SAS_DRIVER_NAME,
	.queuecommand			= scsih_qcmd,
	.target_alloc			= scsih_target_alloc,
	.slave_alloc			= scsih_slave_alloc,
	.slave_configure		= scsih_slave_configure,
	.target_destroy			= scsih_target_destroy,
	.slave_destroy			= scsih_slave_destroy,
	.scan_finished			= scsih_scan_finished,
	.scan_start			= scsih_scan_start,
	.change_queue_depth		= scsih_change_queue_depth,
	.eh_abort_handler		= scsih_abort,
	.eh_device_reset_handler	= scsih_dev_reset,
	.eh_target_reset_handler	= scsih_target_reset,
	.eh_host_reset_handler		= scsih_host_reset,
	.bios_param			= scsih_bios_param,
	.can_queue			= 1,
	.this_id			= -1,
	.sg_tablesize			= MPT2SAS_SG_DEPTH,
	.max_sectors			= 32767,
	.cmd_per_lun			= 7,
	.use_clustering			= ENABLE_CLUSTERING,
	.shost_attrs			= mpt3sas_host_attrs,
	.sdev_attrs			= mpt3sas_dev_attrs,
	.track_queue_depth		= 1,
};

/* raid transport support */
static struct raid_function_template mpt2sas_raid_functions = {
	.cookie		= &mpt2sas_driver_template,
	.is_raid	= scsih_is_raid,
	.get_resync	= scsih_get_resync,
	.get_state	= scsih_get_state,
};

/*
 * The pci device ids are defined in mpi/mpi2_cnfg.h.
 */
static const struct pci_device_id mpt2sas_pci_table[] = {
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2004,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Falcon ~ 2008*/
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2008,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Liberator ~ 2108 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_3,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Meteor ~ 2116 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2116_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2116_2,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Thunderbolt ~ 2208 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_3,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_4,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_5,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_6,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Mustang ~ 2308 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_3,
		PCI_ANY_ID, PCI_ANY_ID },
	/* SSS6200 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SSS6200,
		PCI_ANY_ID, PCI_ANY_ID },
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, mpt2sas_pci_table);

static const struct file_operations mpt2sas_ctl_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ctl_ioctl,
	.poll = ctl_poll,
	.fasync = ctl_fasync,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ctl_ioctl_compat,
#endif
	.llseek = noop_llseek,
};

static struct miscdevice mpt2sas_ctl_dev = {
	.minor  = MPT2SAS_MINOR,
	.name   = MPT2SAS_DEV_NAME,
	.fops   = &mpt2sas_ctl_fops,
};

/**
 * mpt2sas_ctl_init - main entry point for ctl.
 *
 */
void
mpt2sas_ctl_init(void)
{
	ctl_init();
	if (misc_register(&mpt2sas_ctl_dev) < 0)
		pr_err("%s can't register misc device [minor=%d]\n",
		    MPT2SAS_DRIVER_NAME, MPT2SAS_MINOR);
}

/**
 * mpt2sas_ctl_exit - exit point for ctl
 *
 */
void
mpt2sas_ctl_exit(void)
{
	ctl_exit();
	misc_deregister(&mpt2sas_ctl_dev);
}

/**
 * _mpt2sas_probe - attach and add scsi host
 * @pdev: PCI device struct
 * @id: pci device id
 *
 * Returns 0 success, anything else error.
 */
static int
_mpt2sas_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct Scsi_Host *shost;
	int rv;

	shost = scsi_host_alloc(&mpt2sas_driver_template,
			    sizeof(struct MPT3SAS_ADAPTER));
	if (!shost)
		return -ENODEV;

	sprintf(driver_name, "%s", MPT2SAS_DRIVER_NAME);
	rv = scsih_probe(pdev, shost);
	return rv;
}

static struct pci_error_handlers _mpt2sas_err_handler = {
	.error_detected = scsih_pci_error_detected,
	.mmio_enabled = scsih_pci_mmio_enabled,
	.slot_reset =   scsih_pci_slot_reset,
	.resume =       scsih_pci_resume,
};

static struct pci_driver mpt2sas_driver = {
	.name		= MPT2SAS_DRIVER_NAME,
	.id_table	= mpt2sas_pci_table,
	.probe		= _mpt2sas_probe,
	.remove		= scsih_remove,
	.shutdown	= scsih_shutdown,
	.err_handler	= &_mpt2sas_err_handler,
#ifdef CONFIG_PM
	.suspend	= scsih_suspend,
	.resume		= scsih_resume,
#endif
};

/**
 * _mpt2sas_init - main entry point for this driver.
 *
 * Returns 0 success, anything else error.
 */
static int __init
_mpt2sas_init(void)
{
	int error;

	pr_info("%s version %s loaded\n", MPT2SAS_DRIVER_NAME,
					MPT2SAS_DRIVER_VERSION);

	mpt3sas_transport_template =
	    sas_attach_transport(&mpt3sas_transport_functions);
	if (!mpt3sas_transport_template)
		return -ENODEV;

	mpt3sas_raid_template = raid_class_attach(&mpt2sas_raid_functions);
	if (!mpt3sas_raid_template) {
		sas_release_transport(mpt3sas_transport_template);
		return -ENODEV;
	}

	error = scsih_init();
	if (error) {
		scsih_exit();
		return error;
	}

	mpt2sas_ctl_init();

	error = pci_register_driver(&mpt2sas_driver);
	if (error)
		scsih_exit();

	return error;
}

/**
 * _mpt2sas_exit - exit point for this driver (when it is a module).
 *
 */
static void __exit
_mpt2sas_exit(void)
{
	pr_info("mpt2sas version %s unloading\n",
				MPT2SAS_DRIVER_VERSION);

	pci_unregister_driver(&mpt2sas_driver);

	mpt2sas_ctl_exit();

	scsih_exit();
}

module_init(_mpt2sas_init);
module_exit(_mpt2sas_exit);
