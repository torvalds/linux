// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Broadcom MPI3 Storage Controllers
 *
 * Copyright (C) 2017-2021 Broadcom Inc.
 *  (mailto: mpi3mr-linuxdrv.pdl@broadcom.com)
 *
 */

#include "mpi3mr.h"

/* global driver scop variables */
LIST_HEAD(mrioc_list);
DEFINE_SPINLOCK(mrioc_list_lock);
static int mrioc_ids;
static int warn_non_secure_ctlr;

MODULE_AUTHOR(MPI3MR_DRIVER_AUTHOR);
MODULE_DESCRIPTION(MPI3MR_DRIVER_DESC);
MODULE_LICENSE(MPI3MR_DRIVER_LICENSE);
MODULE_VERSION(MPI3MR_DRIVER_VERSION);

/* Module parameters*/
int logging_level;
module_param(logging_level, int, 0);
MODULE_PARM_DESC(logging_level,
	" bits for enabling additional logging info (default=0)");

/**
 * mpi3mr_map_queues - Map queues callback handler
 * @shost: SCSI host reference
 *
 * Call the blk_mq_pci_map_queues with from which operational
 * queue the mapping has to be done
 *
 * Return: return of blk_mq_pci_map_queues
 */
static int mpi3mr_map_queues(struct Scsi_Host *shost)
{
	struct mpi3mr_ioc *mrioc = shost_priv(shost);

	return blk_mq_pci_map_queues(&shost->tag_set.map[HCTX_TYPE_DEFAULT],
	    mrioc->pdev, 0);
}

/**
 * mpi3mr_slave_destroy - Slave destroy callback handler
 * @sdev: SCSI device reference
 *
 * Cleanup and free per device(lun) private data.
 *
 * Return: Nothing.
 */
static void mpi3mr_slave_destroy(struct scsi_device *sdev)
{
}

/**
 * mpi3mr_target_destroy - Target destroy callback handler
 * @starget: SCSI target reference
 *
 * Cleanup and free per target private data.
 *
 * Return: Nothing.
 */
static void mpi3mr_target_destroy(struct scsi_target *starget)
{
}

/**
 * mpi3mr_slave_configure - Slave configure callback handler
 * @sdev: SCSI device reference
 *
 * Configure queue depth, max hardware sectors and virt boundary
 * as required
 *
 * Return: 0 always.
 */
static int mpi3mr_slave_configure(struct scsi_device *sdev)
{
	int retval = 0;
	return retval;
}

/**
 * mpi3mr_slave_alloc -Slave alloc callback handler
 * @sdev: SCSI device reference
 *
 * Allocate per device(lun) private data and initialize it.
 *
 * Return: 0 on success -ENOMEM on memory allocation failure.
 */
static int mpi3mr_slave_alloc(struct scsi_device *sdev)
{
	int retval = 0;
	return retval;
}

/**
 * mpi3mr_target_alloc - Target alloc callback handler
 * @starget: SCSI target reference
 *
 * Allocate per target private data and initialize it.
 *
 * Return: 0 on success -ENOMEM on memory allocation failure.
 */
static int mpi3mr_target_alloc(struct scsi_target *starget)
{
	int retval = -ENODEV;
	return retval;
}

/**
 * mpi3mr_qcmd - I/O request despatcher
 * @shost: SCSI Host reference
 * @scmd: SCSI Command reference
 *
 * Issues the SCSI Command as an MPI3 request.
 *
 * Return: 0 on successful queueing of the request or if the
 *         request is completed with failure.
 *         SCSI_MLQUEUE_DEVICE_BUSY when the device is busy.
 *         SCSI_MLQUEUE_HOST_BUSY when the host queue is full.
 */
static int mpi3mr_qcmd(struct Scsi_Host *shost,
	struct scsi_cmnd *scmd)
{
	int retval = 0;

	scmd->result = DID_NO_CONNECT << 16;
	scmd->scsi_done(scmd);
	return retval;
}

static struct scsi_host_template mpi3mr_driver_template = {
	.module				= THIS_MODULE,
	.name				= "MPI3 Storage Controller",
	.proc_name			= MPI3MR_DRIVER_NAME,
	.queuecommand			= mpi3mr_qcmd,
	.target_alloc			= mpi3mr_target_alloc,
	.slave_alloc			= mpi3mr_slave_alloc,
	.slave_configure		= mpi3mr_slave_configure,
	.target_destroy			= mpi3mr_target_destroy,
	.slave_destroy			= mpi3mr_slave_destroy,
	.map_queues			= mpi3mr_map_queues,
	.no_write_same			= 1,
	.can_queue			= 1,
	.this_id			= -1,
	.sg_tablesize			= MPI3MR_SG_DEPTH,
	/* max xfer supported is 1M (2K in 512 byte sized sectors)
	 */
	.max_sectors			= 2048,
	.cmd_per_lun			= MPI3MR_MAX_CMDS_LUN,
	.track_queue_depth		= 1,
	.cmd_size			= sizeof(struct scmd_priv),
};

/**
 * mpi3mr_init_drv_cmd - Initialize internal command tracker
 * @cmdptr: Internal command tracker
 * @host_tag: Host tag used for the specific command
 *
 * Initialize the internal command tracker structure with
 * specified host tag.
 *
 * Return: Nothing.
 */
static inline void mpi3mr_init_drv_cmd(struct mpi3mr_drv_cmd *cmdptr,
	u16 host_tag)
{
	mutex_init(&cmdptr->mutex);
	cmdptr->reply = NULL;
	cmdptr->state = MPI3MR_CMD_NOTUSED;
	cmdptr->dev_handle = MPI3MR_INVALID_DEV_HANDLE;
	cmdptr->host_tag = host_tag;
}

/**
 * mpi3mr_probe - PCI probe callback
 * @pdev: PCI device instance
 * @id: PCI device ID details
 *
 * controller initialization routine. Checks the security status
 * of the controller and if it is invalid or tampered return the
 * probe without initializing the controller. Otherwise,
 * allocate per adapter instance through shost_priv and
 * initialize controller specific data structures, initializae
 * the controller hardware, add shost to the SCSI subsystem.
 *
 * Return: 0 on success, non-zero on failure.
 */

static int
mpi3mr_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mpi3mr_ioc *mrioc = NULL;
	struct Scsi_Host *shost = NULL;
	int retval = 0;

	shost = scsi_host_alloc(&mpi3mr_driver_template,
	    sizeof(struct mpi3mr_ioc));
	if (!shost) {
		retval = -ENODEV;
		goto shost_failed;
	}

	mrioc = shost_priv(shost);
	mrioc->id = mrioc_ids++;
	sprintf(mrioc->driver_name, "%s", MPI3MR_DRIVER_NAME);
	sprintf(mrioc->name, "%s%d", mrioc->driver_name, mrioc->id);
	INIT_LIST_HEAD(&mrioc->list);
	spin_lock(&mrioc_list_lock);
	list_add_tail(&mrioc->list, &mrioc_list);
	spin_unlock(&mrioc_list_lock);

	spin_lock_init(&mrioc->admin_req_lock);
	spin_lock_init(&mrioc->reply_free_queue_lock);
	spin_lock_init(&mrioc->sbq_lock);

	mpi3mr_init_drv_cmd(&mrioc->init_cmds, MPI3MR_HOSTTAG_INITCMDS);

	mrioc->logging_level = logging_level;
	mrioc->shost = shost;
	mrioc->pdev = pdev;

	/* init shost parameters */
	shost->max_cmd_len = MPI3MR_MAX_CDB_LENGTH;
	shost->max_lun = -1;
	shost->unique_id = mrioc->id;

	shost->max_channel = 1;
	shost->max_id = 0xFFFFFFFF;

	mrioc->is_driver_loading = 1;
	if (mpi3mr_init_ioc(mrioc)) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		retval = -ENODEV;
		goto out_iocinit_failed;
	}

	shost->nr_hw_queues = mrioc->num_op_reply_q;
	shost->can_queue = mrioc->max_host_ios;
	shost->sg_tablesize = MPI3MR_SG_DEPTH;
	shost->max_id = mrioc->facts.max_perids;

	retval = scsi_add_host(shost, &pdev->dev);
	if (retval) {
		ioc_err(mrioc, "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		goto addhost_failed;
	}

	scsi_scan_host(shost);
	return retval;

addhost_failed:
	mpi3mr_cleanup_ioc(mrioc);
out_iocinit_failed:
	spin_lock(&mrioc_list_lock);
	list_del(&mrioc->list);
	spin_unlock(&mrioc_list_lock);
	scsi_host_put(shost);
shost_failed:
	return retval;
}

/**
 * mpi3mr_remove - PCI remove callback
 * @pdev: PCI device instance
 *
 * Free up all memory and resources associated with the
 * controllerand target devices, unregister the shost.
 *
 * Return: Nothing.
 */
static void mpi3mr_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct mpi3mr_ioc *mrioc;

	mrioc = shost_priv(shost);
	while (mrioc->reset_in_progress || mrioc->is_driver_loading)
		ssleep(1);

	scsi_remove_host(shost);

	mpi3mr_cleanup_ioc(mrioc);

	spin_lock(&mrioc_list_lock);
	list_del(&mrioc->list);
	spin_unlock(&mrioc_list_lock);

	scsi_host_put(shost);
}

/**
 * mpi3mr_shutdown - PCI shutdown callback
 * @pdev: PCI device instance
 *
 * Free up all memory and resources associated with the
 * controller
 *
 * Return: Nothing.
 */
static void mpi3mr_shutdown(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct mpi3mr_ioc *mrioc;

	if (!shost)
		return;

	mrioc = shost_priv(shost);
	while (mrioc->reset_in_progress || mrioc->is_driver_loading)
		ssleep(1);

	mpi3mr_cleanup_ioc(mrioc);
}

static const struct pci_device_id mpi3mr_pci_id_table[] = {
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_LSI_LOGIC, 0x00A5,
		    PCI_ANY_ID, PCI_ANY_ID)
	},
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, mpi3mr_pci_id_table);

static struct pci_driver mpi3mr_pci_driver = {
	.name = MPI3MR_DRIVER_NAME,
	.id_table = mpi3mr_pci_id_table,
	.probe = mpi3mr_probe,
	.remove = mpi3mr_remove,
	.shutdown = mpi3mr_shutdown,
};

static int __init mpi3mr_init(void)
{
	int ret_val;

	pr_info("Loading %s version %s\n", MPI3MR_DRIVER_NAME,
	    MPI3MR_DRIVER_VERSION);

	ret_val = pci_register_driver(&mpi3mr_pci_driver);

	return ret_val;
}

static void __exit mpi3mr_exit(void)
{
	if (warn_non_secure_ctlr)
		pr_warn(
		    "Unloading %s version %s while managing a non secure controller\n",
		    MPI3MR_DRIVER_NAME, MPI3MR_DRIVER_VERSION);
	else
		pr_info("Unloading %s version %s\n", MPI3MR_DRIVER_NAME,
		    MPI3MR_DRIVER_VERSION);

	pci_unregister_driver(&mpi3mr_pci_driver);
}

module_init(mpi3mr_init);
module_exit(mpi3mr_exit);
