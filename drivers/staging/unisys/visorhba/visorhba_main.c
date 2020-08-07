// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2012 - 2015 UNISYS CORPORATION
 * All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/kthread.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/visorbus.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>

#include "iochannel.h"

/* The Send and Receive Buffers of the IO Queue may both be full */

#define IOS_ERROR_THRESHOLD  1000
#define MAX_PENDING_REQUESTS (MIN_NUMSIGNALS * 2)
#define VISORHBA_ERROR_COUNT 30

static struct dentry *visorhba_debugfs_dir;

/* GUIDS for HBA channel type supported by this driver */
static struct visor_channeltype_descriptor visorhba_channel_types[] = {
	/* Note that the only channel type we expect to be reported by the
	 * bus driver is the VISOR_VHBA channel.
	 */
	{ VISOR_VHBA_CHANNEL_GUID, "sparvhba", sizeof(struct channel_header),
	  VISOR_VHBA_CHANNEL_VERSIONID },
	{}
};

MODULE_DEVICE_TABLE(visorbus, visorhba_channel_types);
MODULE_ALIAS("visorbus:" VISOR_VHBA_CHANNEL_GUID_STR);

struct visordisk_info {
	struct scsi_device *sdev;
	u32 valid;
	atomic_t ios_threshold;
	atomic_t error_count;
	struct visordisk_info *next;
};

struct scsipending {
	struct uiscmdrsp cmdrsp;
	/* The Data being tracked */
	void *sent;
	/* Type of pointer that is being stored */
	char cmdtype;
};

/* Each scsi_host has a host_data area that contains this struct. */
struct visorhba_devdata {
	struct Scsi_Host *scsihost;
	struct visor_device *dev;
	struct list_head dev_info_list;
	/* Tracks the requests that have been forwarded to
	 * the IOVM and haven't returned yet
	 */
	struct scsipending pending[MAX_PENDING_REQUESTS];
	/* Start search for next pending free slot here */
	unsigned int nextinsert;
	/* lock to protect data in devdata */
	spinlock_t privlock;
	bool serverdown;
	bool serverchangingstate;
	unsigned long long acquire_failed_cnt;
	unsigned long long interrupts_rcvd;
	unsigned long long interrupts_notme;
	unsigned long long interrupts_disabled;
	u64 __iomem *flags_addr;
	atomic_t interrupt_rcvd;
	wait_queue_head_t rsp_queue;
	struct visordisk_info head;
	unsigned int max_buff_len;
	int devnum;
	struct task_struct *thread;
	int thread_wait_ms;

	/*
	 * allows us to pass int handles back-and-forth between us and
	 * iovm, instead of raw pointers
	 */
	struct idr idr;

	struct dentry *debugfs_dir;
	struct dentry *debugfs_info;
};

struct visorhba_devices_open {
	struct visorhba_devdata *devdata;
};

/*
 * visor_thread_start - Starts a thread for the device
 * @threadfn:   Function the thread starts
 * @thrcontext: Context to pass to the thread, i.e. devdata
 * @name:	String describing name of thread
 *
 * Starts a thread for the device.
 *
 * Return: The task_struct * denoting the thread on success,
 *	   or NULL on failure
 */
static struct task_struct *visor_thread_start(int (*threadfn)(void *),
					      void *thrcontext, char *name)
{
	struct task_struct *task;

	task = kthread_run(threadfn, thrcontext, "%s", name);
	if (IS_ERR(task)) {
		pr_err("visorbus failed to start thread\n");
		return NULL;
	}
	return task;
}

/*
 * visor_thread_stop - Stops the thread if it is running
 * @task: Description of process to stop
 */
static void visor_thread_stop(struct task_struct *task)
{
	kthread_stop(task);
}

/*
 * add_scsipending_entry - Save off io command that is pending in
 *			   Service Partition
 * @devdata: Pointer to devdata
 * @cmdtype: Specifies the type of command pending
 * @new:     The command to be saved
 *
 * Saves off the io command that is being handled by the Service
 * Partition so that it can be handled when it completes. If new is
 * NULL it is assumed the entry refers only to the cmdrsp.
 *
 * Return: Insert_location where entry was added on success,
 *	   -EBUSY if it can't
 */
static int add_scsipending_entry(struct visorhba_devdata *devdata,
				 char cmdtype, void *new)
{
	unsigned long flags;
	struct scsipending *entry;
	int insert_location;

	spin_lock_irqsave(&devdata->privlock, flags);
	insert_location = devdata->nextinsert;
	while (devdata->pending[insert_location].sent) {
		insert_location = (insert_location + 1) % MAX_PENDING_REQUESTS;
		if (insert_location == (int)devdata->nextinsert) {
			spin_unlock_irqrestore(&devdata->privlock, flags);
			return -EBUSY;
		}
	}

	entry = &devdata->pending[insert_location];
	memset(&entry->cmdrsp, 0, sizeof(entry->cmdrsp));
	entry->cmdtype = cmdtype;
	if (new)
		entry->sent = new;
	/* wants to send cmdrsp */
	else
		entry->sent = &entry->cmdrsp;
	devdata->nextinsert = (insert_location + 1) % MAX_PENDING_REQUESTS;
	spin_unlock_irqrestore(&devdata->privlock, flags);

	return insert_location;
}

/*
 * del_scsipending_ent - Removes an entry from the pending array
 * @devdata: Device holding the pending array
 * @del:     Entry to remove
 *
 * Removes the entry pointed at by del and returns it.
 *
 * Return: The scsipending entry pointed to on success, NULL on failure
 */
static void *del_scsipending_ent(struct visorhba_devdata *devdata, int del)
{
	unsigned long flags;
	void *sent;

	if (del >= MAX_PENDING_REQUESTS)
		return NULL;

	spin_lock_irqsave(&devdata->privlock, flags);
	sent = devdata->pending[del].sent;
	devdata->pending[del].cmdtype = 0;
	devdata->pending[del].sent = NULL;
	spin_unlock_irqrestore(&devdata->privlock, flags);

	return sent;
}

/*
 * get_scsipending_cmdrsp - Return the cmdrsp stored in a pending entry
 * @ddata: Device holding the pending array
 * @ent:   Entry that stores the cmdrsp
 *
 * Each scsipending entry has a cmdrsp in it. The cmdrsp is only valid
 * if the "sent" field is not NULL.
 *
 * Return: A pointer to the cmdrsp, NULL on failure
 */
static struct uiscmdrsp *get_scsipending_cmdrsp(struct visorhba_devdata *ddata,
						int ent)
{
	if (ddata->pending[ent].sent)
		return &ddata->pending[ent].cmdrsp;

	return NULL;
}

/*
 * simple_idr_get - Associate a provided pointer with an int value
 *		    1 <= value <= INT_MAX, and return this int value;
 *		    the pointer value can be obtained later by passing
 *		    this int value to idr_find()
 * @idrtable: The data object maintaining the pointer<-->int mappings
 * @p:	      The pointer value to be remembered
 * @lock:     A spinlock used when exclusive access to idrtable is needed
 *
 * Return: The id number mapped to pointer 'p', 0 on failure
 */
static unsigned int simple_idr_get(struct idr *idrtable, void *p,
				   spinlock_t *lock)
{
	int id;
	unsigned long flags;

	idr_preload(GFP_KERNEL);
	spin_lock_irqsave(lock, flags);
	id = idr_alloc(idrtable, p, 1, INT_MAX, GFP_NOWAIT);
	spin_unlock_irqrestore(lock, flags);
	idr_preload_end();
	/* failure */
	if (id < 0)
		return 0;
	/* idr_alloc() guarantees > 0 */
	return (unsigned int)(id);
}

/*
 * setup_scsitaskmgmt_handles - Stash the necessary handles so that the
 *				completion processing logic for a taskmgmt
 *				cmd will be able to find who to wake up
 *				and where to stash the result
 * @idrtable: The data object maintaining the pointer<-->int mappings
 * @lock:     A spinlock used when exclusive access to idrtable is needed
 * @cmdrsp:   Response from the IOVM
 * @event:    The event handle to associate with an id
 * @result:   The location to place the result of the event handle into
 */
static void setup_scsitaskmgmt_handles(struct idr *idrtable, spinlock_t *lock,
				       struct uiscmdrsp *cmdrsp,
				       wait_queue_head_t *event, int *result)
{
	/* specify the event that has to be triggered when this */
	/* cmd is complete */
	cmdrsp->scsitaskmgmt.notify_handle =
		simple_idr_get(idrtable, event, lock);
	cmdrsp->scsitaskmgmt.notifyresult_handle =
		simple_idr_get(idrtable, result, lock);
}

/*
 * cleanup_scsitaskmgmt_handles - Forget handles created by
 *				  setup_scsitaskmgmt_handles()
 * @idrtable: The data object maintaining the pointer<-->int mappings
 * @cmdrsp:   Response from the IOVM
 */
static void cleanup_scsitaskmgmt_handles(struct idr *idrtable,
					 struct uiscmdrsp *cmdrsp)
{
	if (cmdrsp->scsitaskmgmt.notify_handle)
		idr_remove(idrtable, cmdrsp->scsitaskmgmt.notify_handle);
	if (cmdrsp->scsitaskmgmt.notifyresult_handle)
		idr_remove(idrtable, cmdrsp->scsitaskmgmt.notifyresult_handle);
}

/*
 * forward_taskmgmt_command - Send taskmegmt command to the Service
 *			      Partition
 * @tasktype: Type of taskmgmt command
 * @scsidev:  Scsidev that issued command
 *
 * Create a cmdrsp packet and send it to the Service Partition
 * that will service this request.
 *
 * Return: Int representing whether command was queued successfully or not
 */
static int forward_taskmgmt_command(enum task_mgmt_types tasktype,
				    struct scsi_device *scsidev)
{
	struct uiscmdrsp *cmdrsp;
	struct visorhba_devdata *devdata =
		(struct visorhba_devdata *)scsidev->host->hostdata;
	int notifyresult = 0xffff;
	wait_queue_head_t notifyevent;
	int scsicmd_id;

	if (devdata->serverdown || devdata->serverchangingstate)
		return FAILED;

	scsicmd_id = add_scsipending_entry(devdata, CMD_SCSITASKMGMT_TYPE,
					   NULL);
	if (scsicmd_id < 0)
		return FAILED;

	cmdrsp = get_scsipending_cmdrsp(devdata, scsicmd_id);

	init_waitqueue_head(&notifyevent);

	/* issue TASK_MGMT_ABORT_TASK */
	cmdrsp->cmdtype = CMD_SCSITASKMGMT_TYPE;
	setup_scsitaskmgmt_handles(&devdata->idr, &devdata->privlock, cmdrsp,
				   &notifyevent, &notifyresult);

	/* save destination */
	cmdrsp->scsitaskmgmt.tasktype = tasktype;
	cmdrsp->scsitaskmgmt.vdest.channel = scsidev->channel;
	cmdrsp->scsitaskmgmt.vdest.id = scsidev->id;
	cmdrsp->scsitaskmgmt.vdest.lun = scsidev->lun;
	cmdrsp->scsitaskmgmt.handle = scsicmd_id;

	dev_dbg(&scsidev->sdev_gendev,
		"visorhba: initiating type=%d taskmgmt command\n", tasktype);
	if (visorchannel_signalinsert(devdata->dev->visorchannel,
				      IOCHAN_TO_IOPART,
				      cmdrsp))
		goto err_del_scsipending_ent;

	/* It can take the Service Partition up to 35 seconds to complete
	 * an IO in some cases, so wait 45 seconds and error out
	 */
	if (!wait_event_timeout(notifyevent, notifyresult != 0xffff,
				msecs_to_jiffies(45000)))
		goto err_del_scsipending_ent;

	dev_dbg(&scsidev->sdev_gendev,
		"visorhba: taskmgmt type=%d success; result=0x%x\n",
		 tasktype, notifyresult);
	cleanup_scsitaskmgmt_handles(&devdata->idr, cmdrsp);
	return SUCCESS;

err_del_scsipending_ent:
	dev_dbg(&scsidev->sdev_gendev,
		"visorhba: taskmgmt type=%d not executed\n", tasktype);
	del_scsipending_ent(devdata, scsicmd_id);
	cleanup_scsitaskmgmt_handles(&devdata->idr, cmdrsp);
	return FAILED;
}

/*
 * visorhba_abort_handler - Send TASK_MGMT_ABORT_TASK
 * @scsicmd: The scsicmd that needs aborted
 *
 * Return: SUCCESS if inserted, FAILED otherwise
 */
static int visorhba_abort_handler(struct scsi_cmnd *scsicmd)
{
	/* issue TASK_MGMT_ABORT_TASK */
	struct scsi_device *scsidev;
	struct visordisk_info *vdisk;
	int rtn;

	scsidev = scsicmd->device;
	vdisk = scsidev->hostdata;
	if (atomic_read(&vdisk->error_count) < VISORHBA_ERROR_COUNT)
		atomic_inc(&vdisk->error_count);
	else
		atomic_set(&vdisk->ios_threshold, IOS_ERROR_THRESHOLD);
	rtn = forward_taskmgmt_command(TASK_MGMT_ABORT_TASK, scsidev);
	if (rtn == SUCCESS) {
		scsicmd->result = DID_ABORT << 16;
		scsicmd->scsi_done(scsicmd);
	}
	return rtn;
}

/*
 * visorhba_device_reset_handler - Send TASK_MGMT_LUN_RESET
 * @scsicmd: The scsicmd that needs aborted
 *
 * Return: SUCCESS if inserted, FAILED otherwise
 */
static int visorhba_device_reset_handler(struct scsi_cmnd *scsicmd)
{
	/* issue TASK_MGMT_LUN_RESET */
	struct scsi_device *scsidev;
	struct visordisk_info *vdisk;
	int rtn;

	scsidev = scsicmd->device;
	vdisk = scsidev->hostdata;
	if (atomic_read(&vdisk->error_count) < VISORHBA_ERROR_COUNT)
		atomic_inc(&vdisk->error_count);
	else
		atomic_set(&vdisk->ios_threshold, IOS_ERROR_THRESHOLD);
	rtn = forward_taskmgmt_command(TASK_MGMT_LUN_RESET, scsidev);
	if (rtn == SUCCESS) {
		scsicmd->result = DID_RESET << 16;
		scsicmd->scsi_done(scsicmd);
	}
	return rtn;
}

/*
 * visorhba_bus_reset_handler - Send TASK_MGMT_TARGET_RESET for each
 *				target on the bus
 * @scsicmd: The scsicmd that needs aborted
 *
 * Return: SUCCESS if inserted, FAILED otherwise
 */
static int visorhba_bus_reset_handler(struct scsi_cmnd *scsicmd)
{
	struct scsi_device *scsidev;
	struct visordisk_info *vdisk;
	int rtn;

	scsidev = scsicmd->device;
	shost_for_each_device(scsidev, scsidev->host) {
		vdisk = scsidev->hostdata;
		if (atomic_read(&vdisk->error_count) < VISORHBA_ERROR_COUNT)
			atomic_inc(&vdisk->error_count);
		else
			atomic_set(&vdisk->ios_threshold, IOS_ERROR_THRESHOLD);
	}
	rtn = forward_taskmgmt_command(TASK_MGMT_BUS_RESET, scsidev);
	if (rtn == SUCCESS) {
		scsicmd->result = DID_RESET << 16;
		scsicmd->scsi_done(scsicmd);
	}
	return rtn;
}

/*
 * visorhba_host_reset_handler - Not supported
 * @scsicmd: The scsicmd that needs to be aborted
 *
 * Return: Not supported, return SUCCESS
 */
static int visorhba_host_reset_handler(struct scsi_cmnd *scsicmd)
{
	/* issue TASK_MGMT_TARGET_RESET for each target on each bus for host */
	return SUCCESS;
}

/*
 * visorhba_get_info - Get information about SCSI device
 * @shp: Scsi host that is requesting information
 *
 * Return: String with visorhba information
 */
static const char *visorhba_get_info(struct Scsi_Host *shp)
{
	/* Return version string */
	return "visorhba";
}

/*
 * dma_data_dir_linux_to_spar - convert dma_data_direction value to
 *				Unisys-specific equivalent
 * @d: dma direction value to convert
 *
 * Returns the Unisys-specific dma direction value corresponding to @d
 */
static u32 dma_data_dir_linux_to_spar(enum dma_data_direction d)
{
	switch (d) {
	case DMA_BIDIRECTIONAL:
		return UIS_DMA_BIDIRECTIONAL;
	case DMA_TO_DEVICE:
		return UIS_DMA_TO_DEVICE;
	case DMA_FROM_DEVICE:
		return UIS_DMA_FROM_DEVICE;
	case DMA_NONE:
		return UIS_DMA_NONE;
	default:
		return UIS_DMA_NONE;
	}
}

/*
 * visorhba_queue_command_lck - Queues command to the Service Partition
 * @scsicmd:		Command to be queued
 * @vsiorhba_cmnd_done: Done command to call when scsicmd is returned
 *
 * Queues to scsicmd to the ServicePartition after converting it to a
 * uiscmdrsp structure.
 *
 * Return: 0 if successfully queued to the Service Partition, otherwise
 *	   error code
 */
static int visorhba_queue_command_lck(struct scsi_cmnd *scsicmd,
				      void (*visorhba_cmnd_done)
					   (struct scsi_cmnd *))
{
	struct uiscmdrsp *cmdrsp;
	struct scsi_device *scsidev = scsicmd->device;
	int insert_location;
	unsigned char *cdb = scsicmd->cmnd;
	struct Scsi_Host *scsihost = scsidev->host;
	unsigned int i;
	struct visorhba_devdata *devdata =
		(struct visorhba_devdata *)scsihost->hostdata;
	struct scatterlist *sg = NULL;
	struct scatterlist *sglist = NULL;

	if (devdata->serverdown || devdata->serverchangingstate)
		return SCSI_MLQUEUE_DEVICE_BUSY;

	insert_location = add_scsipending_entry(devdata, CMD_SCSI_TYPE,
						(void *)scsicmd);
	if (insert_location < 0)
		return SCSI_MLQUEUE_DEVICE_BUSY;

	cmdrsp = get_scsipending_cmdrsp(devdata, insert_location);
	cmdrsp->cmdtype = CMD_SCSI_TYPE;
	/* save the pending insertion location. Deletion from pending
	 * will return the scsicmd pointer for completion
	 */
	cmdrsp->scsi.handle = insert_location;

	/* save done function that we have call when cmd is complete */
	scsicmd->scsi_done = visorhba_cmnd_done;
	/* save destination */
	cmdrsp->scsi.vdest.channel = scsidev->channel;
	cmdrsp->scsi.vdest.id = scsidev->id;
	cmdrsp->scsi.vdest.lun = scsidev->lun;
	/* save datadir */
	cmdrsp->scsi.data_dir =
		dma_data_dir_linux_to_spar(scsicmd->sc_data_direction);
	memcpy(cmdrsp->scsi.cmnd, cdb, MAX_CMND_SIZE);
	cmdrsp->scsi.bufflen = scsi_bufflen(scsicmd);

	/* keep track of the max buffer length so far. */
	if (cmdrsp->scsi.bufflen > devdata->max_buff_len)
		devdata->max_buff_len = cmdrsp->scsi.bufflen;

	if (scsi_sg_count(scsicmd) > MAX_PHYS_INFO)
		goto err_del_scsipending_ent;

	/* convert buffer to phys information  */
	/* buffer is scatterlist - copy it out */
	sglist = scsi_sglist(scsicmd);

	for_each_sg(sglist, sg, scsi_sg_count(scsicmd), i) {
		cmdrsp->scsi.gpi_list[i].address = sg_phys(sg);
		cmdrsp->scsi.gpi_list[i].length = sg->length;
	}
	cmdrsp->scsi.guest_phys_entries = scsi_sg_count(scsicmd);

	if (visorchannel_signalinsert(devdata->dev->visorchannel,
				      IOCHAN_TO_IOPART,
				      cmdrsp))
		/* queue must be full and we aren't going to wait */
		goto err_del_scsipending_ent;

	return 0;

err_del_scsipending_ent:
	del_scsipending_ent(devdata, insert_location);
	return SCSI_MLQUEUE_DEVICE_BUSY;
}

#ifdef DEF_SCSI_QCMD
static DEF_SCSI_QCMD(visorhba_queue_command)
#else
#define visorhba_queue_command visorhba_queue_command_lck
#endif

/*
 * visorhba_slave_alloc - Called when new disk is discovered
 * @scsidev: New disk
 *
 * Create a new visordisk_info structure and add it to our
 * list of vdisks.
 *
 * Return: 0 on success, -ENOMEM on failure.
 */
static int visorhba_slave_alloc(struct scsi_device *scsidev)
{
	/* this is called by the midlayer before scan for new devices --
	 * LLD can alloc any struct & do init if needed.
	 */
	struct visordisk_info *vdisk;
	struct visorhba_devdata *devdata;
	struct Scsi_Host *scsihost = (struct Scsi_Host *)scsidev->host;

	/* already allocated return success */
	if (scsidev->hostdata)
		return 0;

	/* even though we errored, treat as success */
	devdata = (struct visorhba_devdata *)scsihost->hostdata;
	if (!devdata)
		return 0;

	vdisk = kzalloc(sizeof(*vdisk), GFP_ATOMIC);
	if (!vdisk)
		return -ENOMEM;

	vdisk->sdev = scsidev;
	scsidev->hostdata = vdisk;
	return 0;
}

/*
 * visorhba_slave_destroy - Disk is going away, clean up resources.
 * @scsidev: Scsi device to destroy
 */
static void visorhba_slave_destroy(struct scsi_device *scsidev)
{
	/* midlevel calls this after device has been quiesced and
	 * before it is to be deleted.
	 */
	struct visordisk_info *vdisk;

	vdisk = scsidev->hostdata;
	scsidev->hostdata = NULL;
	kfree(vdisk);
}

static struct scsi_host_template visorhba_driver_template = {
	.name = "Unisys Visor HBA",
	.info = visorhba_get_info,
	.queuecommand = visorhba_queue_command,
	.eh_abort_handler = visorhba_abort_handler,
	.eh_device_reset_handler = visorhba_device_reset_handler,
	.eh_bus_reset_handler = visorhba_bus_reset_handler,
	.eh_host_reset_handler = visorhba_host_reset_handler,
	.shost_attrs = NULL,
#define visorhba_MAX_CMNDS 128
	.can_queue = visorhba_MAX_CMNDS,
	.sg_tablesize = 64,
	.this_id = -1,
	.slave_alloc = visorhba_slave_alloc,
	.slave_destroy = visorhba_slave_destroy,
};

/*
 * info_debugfs_show - Debugfs interface to dump visorhba states
 * @seq: The sequence file to write information to
 * @v:   Unused, but needed for use with seq file single_open invocation
 *
 * Presents a file in the debugfs tree named: /visorhba/vbus<x>:dev<y>/info.
 *
 * Return: SUCCESS
 */
static int info_debugfs_show(struct seq_file *seq, void *v)
{
	struct visorhba_devdata *devdata = seq->private;

	seq_printf(seq, "max_buff_len = %u\n", devdata->max_buff_len);
	seq_printf(seq, "interrupts_rcvd = %llu\n", devdata->interrupts_rcvd);
	seq_printf(seq, "interrupts_disabled = %llu\n",
		   devdata->interrupts_disabled);
	seq_printf(seq, "interrupts_notme = %llu\n",
		   devdata->interrupts_notme);
	seq_printf(seq, "flags_addr = %p\n", devdata->flags_addr);
	if (devdata->flags_addr) {
		u64 phys_flags_addr =
			virt_to_phys((__force  void *)devdata->flags_addr);
		seq_printf(seq, "phys_flags_addr = 0x%016llx\n",
			   phys_flags_addr);
		seq_printf(seq, "FeatureFlags = %llu\n",
			   (u64)readq(devdata->flags_addr));
	}
	seq_printf(seq, "acquire_failed_cnt = %llu\n",
		   devdata->acquire_failed_cnt);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(info_debugfs);

/*
 * complete_taskmgmt_command - Complete task management
 * @idrtable: The data object maintaining the pointer<-->int mappings
 * @cmdrsp:   Response from the IOVM
 * @result:   The result of the task management command
 *
 * Service Partition returned the result of the task management
 * command. Wake up anyone waiting for it.
 */
static void complete_taskmgmt_command(struct idr *idrtable,
				      struct uiscmdrsp *cmdrsp, int result)
{
	wait_queue_head_t *wq =
		idr_find(idrtable, cmdrsp->scsitaskmgmt.notify_handle);
	int *scsi_result_ptr =
		idr_find(idrtable, cmdrsp->scsitaskmgmt.notifyresult_handle);
	if (unlikely(!(wq && scsi_result_ptr))) {
		pr_err("visorhba: no completion context; cmd will time out\n");
		return;
	}

	/* copy the result of the taskmgmt and
	 * wake up the error handler that is waiting for this
	 */
	pr_debug("visorhba: notifying initiator with result=0x%x\n", result);
	*scsi_result_ptr = result;
	wake_up_all(wq);
}

/*
 * visorhba_serverdown_complete - Called when we are done cleaning up
 *				  from serverdown
 * @devdata: Visorhba instance on which to complete serverdown
 *
 * Called when we are done cleanning up from serverdown, stop processing
 * queue, fail pending IOs.
 */
static void visorhba_serverdown_complete(struct visorhba_devdata *devdata)
{
	int i;
	struct scsipending *pendingdel = NULL;
	struct scsi_cmnd *scsicmd = NULL;
	struct uiscmdrsp *cmdrsp;
	unsigned long flags;

	/* Stop using the IOVM response queue (queue should be drained
	 * by the end)
	 */
	visor_thread_stop(devdata->thread);

	/* Fail commands that weren't completed */
	spin_lock_irqsave(&devdata->privlock, flags);
	for (i = 0; i < MAX_PENDING_REQUESTS; i++) {
		pendingdel = &devdata->pending[i];
		switch (pendingdel->cmdtype) {
		case CMD_SCSI_TYPE:
			scsicmd = pendingdel->sent;
			scsicmd->result = DID_RESET << 16;
			if (scsicmd->scsi_done)
				scsicmd->scsi_done(scsicmd);
			break;
		case CMD_SCSITASKMGMT_TYPE:
			cmdrsp = pendingdel->sent;
			complete_taskmgmt_command(&devdata->idr, cmdrsp,
						  TASK_MGMT_FAILED);
			break;
		default:
			break;
		}
		pendingdel->cmdtype = 0;
		pendingdel->sent = NULL;
	}
	spin_unlock_irqrestore(&devdata->privlock, flags);

	devdata->serverdown = true;
	devdata->serverchangingstate = false;
}

/*
 * visorhba_serverdown - Got notified that the IOVM is down
 * @devdata: Visorhba that is being serviced by downed IOVM
 *
 * Something happened to the IOVM, return immediately and
 * schedule cleanup work.
 *
 * Return: 0 on success, -EINVAL on failure
 */
static int visorhba_serverdown(struct visorhba_devdata *devdata)
{
	if (!devdata->serverdown && !devdata->serverchangingstate) {
		devdata->serverchangingstate = true;
		visorhba_serverdown_complete(devdata);
	} else if (devdata->serverchangingstate) {
		return -EINVAL;
	}
	return 0;
}

/*
 * do_scsi_linuxstat - Scsi command returned linuxstat
 * @cmdrsp:  Response from IOVM
 * @scsicmd: Command issued
 *
 * Don't log errors for disk-not-present inquiries.
 */
static void do_scsi_linuxstat(struct uiscmdrsp *cmdrsp,
			      struct scsi_cmnd *scsicmd)
{
	struct visordisk_info *vdisk;
	struct scsi_device *scsidev;

	scsidev = scsicmd->device;
	memcpy(scsicmd->sense_buffer, cmdrsp->scsi.sensebuf, MAX_SENSE_SIZE);

	/* Do not log errors for disk-not-present inquiries */
	if (cmdrsp->scsi.cmnd[0] == INQUIRY &&
	    (host_byte(cmdrsp->scsi.linuxstat) == DID_NO_CONNECT) &&
	    cmdrsp->scsi.addlstat == ADDL_SEL_TIMEOUT)
		return;
	/* Okay see what our error_count is here.... */
	vdisk = scsidev->hostdata;
	if (atomic_read(&vdisk->error_count) < VISORHBA_ERROR_COUNT) {
		atomic_inc(&vdisk->error_count);
		atomic_set(&vdisk->ios_threshold, IOS_ERROR_THRESHOLD);
	}
}

static int set_no_disk_inquiry_result(unsigned char *buf, size_t len,
				      bool is_lun0)
{
	if (len < NO_DISK_INQUIRY_RESULT_LEN)
		return -EINVAL;
	memset(buf, 0, NO_DISK_INQUIRY_RESULT_LEN);
	buf[2] = SCSI_SPC2_VER;
	if (is_lun0) {
		buf[0] = DEV_DISK_CAPABLE_NOT_PRESENT;
		buf[3] = DEV_HISUPPORT;
	} else {
		buf[0] = DEV_NOT_CAPABLE;
	}
	buf[4] = NO_DISK_INQUIRY_RESULT_LEN - 5;
	strncpy(buf + 8, "DELLPSEUDO DEVICE .", NO_DISK_INQUIRY_RESULT_LEN - 8);
	return 0;
}

/*
 * do_scsi_nolinuxstat - Scsi command didn't have linuxstat
 * @cmdrsp:  Response from IOVM
 * @scsicmd: Command issued
 *
 * Handle response when no linuxstat was returned.
 */
static void do_scsi_nolinuxstat(struct uiscmdrsp *cmdrsp,
				struct scsi_cmnd *scsicmd)
{
	struct scsi_device *scsidev;
	unsigned char *buf;
	struct scatterlist *sg;
	unsigned int i;
	char *this_page;
	char *this_page_orig;
	int bufind = 0;
	struct visordisk_info *vdisk;

	scsidev = scsicmd->device;
	if (cmdrsp->scsi.cmnd[0] == INQUIRY &&
	    cmdrsp->scsi.bufflen >= MIN_INQUIRY_RESULT_LEN) {
		if (cmdrsp->scsi.no_disk_result == 0)
			return;

		buf = kzalloc(36, GFP_KERNEL);
		if (!buf)
			return;

		/* Linux scsi code wants a device at Lun 0
		 * to issue report luns, but we don't want
		 * a disk there so we'll present a processor
		 * there.
		 */
		set_no_disk_inquiry_result(buf, (size_t)cmdrsp->scsi.bufflen,
					   scsidev->lun == 0);

		if (scsi_sg_count(scsicmd) == 0) {
			memcpy(scsi_sglist(scsicmd), buf,
			       cmdrsp->scsi.bufflen);
			kfree(buf);
			return;
		}

		scsi_for_each_sg(scsicmd, sg, scsi_sg_count(scsicmd), i) {
			this_page_orig = kmap_atomic(sg_page(sg));
			this_page = (void *)((unsigned long)this_page_orig |
					     sg->offset);
			memcpy(this_page, buf + bufind, sg->length);
			kunmap_atomic(this_page_orig);
		}
		kfree(buf);
	} else {
		vdisk = scsidev->hostdata;
		if (atomic_read(&vdisk->ios_threshold) > 0) {
			atomic_dec(&vdisk->ios_threshold);
			if (atomic_read(&vdisk->ios_threshold) == 0)
				atomic_set(&vdisk->error_count, 0);
		}
	}
}

/*
 * complete_scsi_command - Complete a scsi command
 * @uiscmdrsp: Response from Service Partition
 * @scsicmd:   The scsi command
 *
 * Response was returned by the Service Partition. Finish it and send
 * completion to the scsi midlayer.
 */
static void complete_scsi_command(struct uiscmdrsp *cmdrsp,
				  struct scsi_cmnd *scsicmd)
{
	/* take what we need out of cmdrsp and complete the scsicmd */
	scsicmd->result = cmdrsp->scsi.linuxstat;
	if (cmdrsp->scsi.linuxstat)
		do_scsi_linuxstat(cmdrsp, scsicmd);
	else
		do_scsi_nolinuxstat(cmdrsp, scsicmd);

	scsicmd->scsi_done(scsicmd);
}

/*
 * drain_queue - Pull responses out of iochannel
 * @cmdrsp:  Response from the IOSP
 * @devdata: Device that owns this iochannel
 *
 * Pulls responses out of the iochannel and process the responses.
 */
static void drain_queue(struct uiscmdrsp *cmdrsp,
			struct visorhba_devdata *devdata)
{
	struct scsi_cmnd *scsicmd;

	while (1) {
		/* queue empty */
		if (visorchannel_signalremove(devdata->dev->visorchannel,
					      IOCHAN_FROM_IOPART,
					      cmdrsp))
			break;
		if (cmdrsp->cmdtype == CMD_SCSI_TYPE) {
			/* scsicmd location is returned by the
			 * deletion
			 */
			scsicmd = del_scsipending_ent(devdata,
						      cmdrsp->scsi.handle);
			if (!scsicmd)
				break;
			/* complete the orig cmd */
			complete_scsi_command(cmdrsp, scsicmd);
		} else if (cmdrsp->cmdtype == CMD_SCSITASKMGMT_TYPE) {
			if (!del_scsipending_ent(devdata,
						 cmdrsp->scsitaskmgmt.handle))
				break;
			complete_taskmgmt_command(&devdata->idr, cmdrsp,
						  cmdrsp->scsitaskmgmt.result);
		} else if (cmdrsp->cmdtype == CMD_NOTIFYGUEST_TYPE)
			dev_err_once(&devdata->dev->device,
				     "ignoring unsupported NOTIFYGUEST\n");
		/* cmdrsp is now available for re-use */
	}
}

/*
 * process_incoming_rsps - Process responses from IOSP
 * @v:  Void pointer to visorhba_devdata
 *
 * Main function for the thread that processes the responses
 * from the IO Service Partition. When the queue is empty, wait
 * to check to see if it is full again.
 *
 * Return: 0 on success, -ENOMEM on failure
 */
static int process_incoming_rsps(void *v)
{
	struct visorhba_devdata *devdata = v;
	struct uiscmdrsp *cmdrsp = NULL;
	const int size = sizeof(*cmdrsp);

	cmdrsp = kmalloc(size, GFP_ATOMIC);
	if (!cmdrsp)
		return -ENOMEM;

	while (1) {
		if (kthread_should_stop())
			break;
		wait_event_interruptible_timeout(
			devdata->rsp_queue, (atomic_read(
					     &devdata->interrupt_rcvd) == 1),
				msecs_to_jiffies(devdata->thread_wait_ms));
		/* drain queue */
		drain_queue(cmdrsp, devdata);
	}
	kfree(cmdrsp);
	return 0;
}

/*
 * visorhba_pause - Function to handle visorbus pause messages
 * @dev:	   Device that is pausing
 * @complete_func: Function to call when finished
 *
 * Something has happened to the IO Service Partition that is
 * handling this device. Quiet this device and reset commands
 * so that the Service Partition can be corrected.
 *
 * Return: SUCCESS
 */
static int visorhba_pause(struct visor_device *dev,
			  visorbus_state_complete_func complete_func)
{
	struct visorhba_devdata *devdata = dev_get_drvdata(&dev->device);

	visorhba_serverdown(devdata);
	complete_func(dev, 0);
	return 0;
}

/*
 * visorhba_resume - Function called when the IO Service Partition is back
 * @dev:	   Device that is pausing
 * @complete_func: Function to call when finished
 *
 * Yay! The IO Service Partition is back, the channel has been wiped
 * so lets re-establish connection and start processing responses.
 *
 * Return: 0 on success, -EINVAL on failure
 */
static int visorhba_resume(struct visor_device *dev,
			   visorbus_state_complete_func complete_func)
{
	struct visorhba_devdata *devdata;

	devdata = dev_get_drvdata(&dev->device);
	if (!devdata)
		return -EINVAL;

	if (devdata->serverdown && !devdata->serverchangingstate)
		devdata->serverchangingstate = true;

	devdata->thread = visor_thread_start(process_incoming_rsps, devdata,
					     "vhba_incming");
	devdata->serverdown = false;
	devdata->serverchangingstate = false;

	return 0;
}

/*
 * visorhba_probe - Device has been discovered; do acquire
 * @dev: visor_device that was discovered
 *
 * A new HBA was discovered; do the initial connections of it.
 *
 * Return: 0 on success, otherwise error code
 */
static int visorhba_probe(struct visor_device *dev)
{
	struct Scsi_Host *scsihost;
	struct vhba_config_max max;
	struct visorhba_devdata *devdata = NULL;
	int err, channel_offset;
	u64 features;

	scsihost = scsi_host_alloc(&visorhba_driver_template,
				   sizeof(*devdata));
	if (!scsihost)
		return -ENODEV;

	channel_offset = offsetof(struct visor_io_channel, vhba.max);
	err = visorbus_read_channel(dev, channel_offset, &max,
				    sizeof(struct vhba_config_max));
	if (err < 0)
		goto err_scsi_host_put;

	scsihost->max_id = (unsigned int)max.max_id;
	scsihost->max_lun = (unsigned int)max.max_lun;
	scsihost->cmd_per_lun = (unsigned int)max.cmd_per_lun;
	scsihost->max_sectors =
	    (unsigned short)(max.max_io_size >> 9);
	scsihost->sg_tablesize =
	    (unsigned short)(max.max_io_size / PAGE_SIZE);
	if (scsihost->sg_tablesize > MAX_PHYS_INFO)
		scsihost->sg_tablesize = MAX_PHYS_INFO;
	err = scsi_add_host(scsihost, &dev->device);
	if (err < 0)
		goto err_scsi_host_put;

	devdata = (struct visorhba_devdata *)scsihost->hostdata;
	devdata->dev = dev;
	dev_set_drvdata(&dev->device, devdata);

	devdata->debugfs_dir = debugfs_create_dir(dev_name(&dev->device),
						  visorhba_debugfs_dir);
	if (!devdata->debugfs_dir) {
		err = -ENOMEM;
		goto err_scsi_remove_host;
	}
	devdata->debugfs_info =
		debugfs_create_file("info", 0440,
				    devdata->debugfs_dir, devdata,
				    &info_debugfs_fops);
	if (!devdata->debugfs_info) {
		err = -ENOMEM;
		goto err_debugfs_dir;
	}

	init_waitqueue_head(&devdata->rsp_queue);
	spin_lock_init(&devdata->privlock);
	devdata->serverdown = false;
	devdata->serverchangingstate = false;
	devdata->scsihost = scsihost;

	channel_offset = offsetof(struct visor_io_channel,
				  channel_header.features);
	err = visorbus_read_channel(dev, channel_offset, &features, 8);
	if (err)
		goto err_debugfs_info;
	features |= VISOR_CHANNEL_IS_POLLING;
	err = visorbus_write_channel(dev, channel_offset, &features, 8);
	if (err)
		goto err_debugfs_info;

	idr_init(&devdata->idr);

	devdata->thread_wait_ms = 2;
	devdata->thread = visor_thread_start(process_incoming_rsps, devdata,
					     "vhba_incoming");

	scsi_scan_host(scsihost);

	return 0;

err_debugfs_info:
	debugfs_remove(devdata->debugfs_info);

err_debugfs_dir:
	debugfs_remove_recursive(devdata->debugfs_dir);

err_scsi_remove_host:
	scsi_remove_host(scsihost);

err_scsi_host_put:
	scsi_host_put(scsihost);
	return err;
}

/*
 * visorhba_remove - Remove a visorhba device
 * @dev: Device to remove
 *
 * Removes the visorhba device.
 */
static void visorhba_remove(struct visor_device *dev)
{
	struct visorhba_devdata *devdata = dev_get_drvdata(&dev->device);
	struct Scsi_Host *scsihost = NULL;

	if (!devdata)
		return;

	scsihost = devdata->scsihost;
	visor_thread_stop(devdata->thread);
	scsi_remove_host(scsihost);
	scsi_host_put(scsihost);

	idr_destroy(&devdata->idr);

	dev_set_drvdata(&dev->device, NULL);
	debugfs_remove(devdata->debugfs_info);
	debugfs_remove_recursive(devdata->debugfs_dir);
}

/* This is used to tell the visorbus driver which types of visor devices
 * we support, and what functions to call when a visor device that we support
 * is attached or removed.
 */
static struct visor_driver visorhba_driver = {
	.name = "visorhba",
	.owner = THIS_MODULE,
	.channel_types = visorhba_channel_types,
	.probe = visorhba_probe,
	.remove = visorhba_remove,
	.pause = visorhba_pause,
	.resume = visorhba_resume,
	.channel_interrupt = NULL,
};

/*
 * visorhba_init - Driver init routine
 *
 * Initialize the visorhba driver and register it with visorbus
 * to handle s-Par virtual host bus adapter.
 *
 * Return: 0 on success, error code otherwise
 */
static int visorhba_init(void)
{
	int rc;

	visorhba_debugfs_dir = debugfs_create_dir("visorhba", NULL);
	if (!visorhba_debugfs_dir)
		return -ENOMEM;

	rc = visorbus_register_visor_driver(&visorhba_driver);
	if (rc)
		goto cleanup_debugfs;

	return 0;

cleanup_debugfs:
	debugfs_remove_recursive(visorhba_debugfs_dir);

	return rc;
}

/*
 * visorhba_exit - Driver exit routine
 *
 * Unregister driver from the bus and free up memory.
 */
static void visorhba_exit(void)
{
	visorbus_unregister_visor_driver(&visorhba_driver);
	debugfs_remove_recursive(visorhba_debugfs_dir);
}

module_init(visorhba_init);
module_exit(visorhba_exit);

MODULE_AUTHOR("Unisys");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("s-Par HBA driver for virtual SCSI host busses");
