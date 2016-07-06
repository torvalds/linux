/* Copyright (c) 2012 - 2015 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#include <linux/debugfs.h>
#include <linux/skbuff.h>
#include <linux/kthread.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>

#include "visorbus.h"
#include "iochannel.h"

/* The Send and Receive Buffers of the IO Queue may both be full */

#define IOS_ERROR_THRESHOLD	1000
/* MAX_BUF = 6 lines x 10 MAXVHBA x 80 characters
 *         = 4800 bytes ~ 2^13 = 8192 bytes
 */
#define MAX_BUF			8192
#define MAX_PENDING_REQUESTS	(MIN_NUMSIGNALS * 2)
#define VISORHBA_ERROR_COUNT	30
#define VISORHBA_OPEN_MAX	1

static int visorhba_queue_command_lck(struct scsi_cmnd *scsicmd,
				      void (*visorhba_cmnd_done)
					    (struct scsi_cmnd *));
#ifdef DEF_SCSI_QCMD
static DEF_SCSI_QCMD(visorhba_queue_command)
#else
#define visorhba_queue_command visorhba_queue_command_lck
#endif
static int visorhba_probe(struct visor_device *dev);
static void visorhba_remove(struct visor_device *dev);
static int visorhba_pause(struct visor_device *dev,
			  visorbus_state_complete_func complete_func);
static int visorhba_resume(struct visor_device *dev,
			   visorbus_state_complete_func complete_func);

static ssize_t info_debugfs_read(struct file *file, char __user *buf,
				 size_t len, loff_t *offset);
static int set_no_disk_inquiry_result(unsigned char *buf,
				      size_t len, bool is_lun0);
static struct dentry *visorhba_debugfs_dir;
static const struct file_operations debugfs_info_fops = {
	.read = info_debugfs_read,
};

/* GUIDS for HBA channel type supported by this driver */
static struct visor_channeltype_descriptor visorhba_channel_types[] = {
	/* Note that the only channel type we expect to be reported by the
	 * bus driver is the SPAR_VHBA channel.
	 */
	{ SPAR_VHBA_CHANNEL_PROTOCOL_UUID, "sparvhba" },
	{ NULL_UUID_LE, NULL }
};

/* This is used to tell the visor bus driver which types of visor devices
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
MODULE_DEVICE_TABLE(visorbus, visorhba_channel_types);
MODULE_ALIAS("visorbus:" SPAR_VHBA_CHANNEL_PROTOCOL_UUID_STR);

struct visordisk_info {
	u32 valid;
	u32 channel, id, lun;	/* Disk Path */
	atomic_t ios_threshold;
	atomic_t error_count;
	struct visordisk_info *next;
};

struct scsipending {
	struct uiscmdrsp cmdrsp;
	void *sent;		/* The Data being tracked */
	char cmdtype;		/* Type of pointer that is being stored */
};

/* Work Data for dar_work_queue */
struct diskaddremove {
	u8 add;			/* 0-remove, 1-add */
	struct Scsi_Host *shost; /* Scsi Host for this visorhba instance */
	u32 channel, id, lun;	/* Disk Path */
	struct diskaddremove *next;
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
	spinlock_t privlock; /* lock to protect data in devdata */
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
};

struct visorhba_devices_open {
	struct visorhba_devdata *devdata;
};

static struct visorhba_devices_open visorhbas_open[VISORHBA_OPEN_MAX];

#define for_each_vdisk_match(iter, list, match)			  \
	for (iter = &list->head; iter->next; iter = iter->next) \
		if ((iter->channel == match->channel) &&		  \
		    (iter->id == match->id) &&			  \
		    (iter->lun == match->lun))
/**
 *	visor_thread_start - starts a thread for the device
 *	@threadfn: Function the thread starts
 *	@thrcontext: Context to pass to the thread, i.e. devdata
 *	@name: string describing name of thread
 *
 *	Starts a thread for the device.
 *
 *	Return the task_struct * denoting the thread on success,
 *             or NULL on failure
 */
static struct task_struct *visor_thread_start
(int (*threadfn)(void *), void *thrcontext, char *name)
{
	struct task_struct *task;

	task = kthread_run(threadfn, thrcontext, "%s", name);
	if (IS_ERR(task)) {
		pr_err("visorbus failed to start thread\n");
		return NULL;
	}
	return task;
}

/**
 *      visor_thread_stop - stops the thread if it is running
 */
static void visor_thread_stop(struct task_struct *task)
{
	if (!task)
		return;  /* no thread running */
	kthread_stop(task);
}

/**
 *	add_scsipending_entry - save off io command that is pending in
 *				Service Partition
 *	@devdata: Pointer to devdata
 *	@cmdtype: Specifies the type of command pending
 *	@new:	The command to be saved
 *
 *	Saves off the io command that is being handled by the Service
 *	Partition so that it can be handled when it completes. If new is
 *	NULL it is assumed the entry refers only to the cmdrsp.
 *	Returns insert_location where entry was added,
 *	SCSI_MLQUEUE_DEVICE_BUSY if it can't
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
			return -1;
		}
	}

	entry = &devdata->pending[insert_location];
	memset(&entry->cmdrsp, 0, sizeof(entry->cmdrsp));
	entry->cmdtype = cmdtype;
	if (new)
		entry->sent = new;
	else /* wants to send cmdrsp */
		entry->sent = &entry->cmdrsp;
	devdata->nextinsert = (insert_location + 1) % MAX_PENDING_REQUESTS;
	spin_unlock_irqrestore(&devdata->privlock, flags);

	return insert_location;
}

/**
 *	del_scsipending_enty - removes an entry from the pending array
 *	@devdata: Device holding the pending array
 *	@del: Entry to remove
 *
 *	Removes the entry pointed at by del and returns it.
 *	Returns the scsipending entry pointed at
 */
static void *del_scsipending_ent(struct visorhba_devdata *devdata,
				 int del)
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

/**
 *	get_scsipending_cmdrsp - return the cmdrsp stored in a pending entry
 *	#ddata: Device holding the pending array
 *	@ent: Entry that stores the cmdrsp
 *
 *	Each scsipending entry has a cmdrsp in it. The cmdrsp is only valid
 *	if the "sent" field is not NULL
 *	Returns a pointer to the cmdrsp.
 */
static struct uiscmdrsp *get_scsipending_cmdrsp(struct visorhba_devdata *ddata,
						int ent)
{
	if (ddata->pending[ent].sent)
		return &ddata->pending[ent].cmdrsp;

	return NULL;
}

/**
 *	forward_taskmgmt_command - send taskmegmt command to the Service
 *				   Partition
 *	@tasktype: Type of taskmgmt command
 *	@scsidev: Scsidev that issued command
 *
 *	Create a cmdrsp packet and send it to the Serivce Partition
 *	that will service this request.
 *	Returns whether the command was queued successfully or not.
 */
static int forward_taskmgmt_command(enum task_mgmt_types tasktype,
				    struct scsi_cmnd *scsicmd)
{
	struct uiscmdrsp *cmdrsp;
	struct scsi_device *scsidev = scsicmd->device;
	struct visorhba_devdata *devdata =
		(struct visorhba_devdata *)scsidev->host->hostdata;
	int notifyresult = 0xffff;
	wait_queue_head_t notifyevent;
	int scsicmd_id = 0;

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
	/* specify the event that has to be triggered when this */
	/* cmd is complete */
	cmdrsp->scsitaskmgmt.notify_handle = (u64)&notifyevent;
	cmdrsp->scsitaskmgmt.notifyresult_handle = (u64)&notifyresult;

	/* save destination */
	cmdrsp->scsitaskmgmt.tasktype = tasktype;
	cmdrsp->scsitaskmgmt.vdest.channel = scsidev->channel;
	cmdrsp->scsitaskmgmt.vdest.id = scsidev->id;
	cmdrsp->scsitaskmgmt.vdest.lun = scsidev->lun;
	cmdrsp->scsitaskmgmt.handle = scsicmd_id;

	if (!visorchannel_signalinsert(devdata->dev->visorchannel,
				       IOCHAN_TO_IOPART,
				       cmdrsp))
		goto err_del_scsipending_ent;

	/* It can take the Service Partition up to 35 seconds to complete
	 * an IO in some cases, so wait 45 seconds and error out
	 */
	if (!wait_event_timeout(notifyevent, notifyresult != 0xffff,
				msecs_to_jiffies(45000)))
		goto err_del_scsipending_ent;

	if (tasktype == TASK_MGMT_ABORT_TASK)
		scsicmd->result = DID_ABORT << 16;
	else
		scsicmd->result = DID_RESET << 16;

	scsicmd->scsi_done(scsicmd);

	return SUCCESS;

err_del_scsipending_ent:
	del_scsipending_ent(devdata, scsicmd_id);
	return FAILED;
}

/**
 *	visorhba_abort_handler - Send TASK_MGMT_ABORT_TASK
 *	@scsicmd: The scsicmd that needs aborted
 *
 *	Returns SUCCESS if inserted, failure otherwise
 *
 */
static int visorhba_abort_handler(struct scsi_cmnd *scsicmd)
{
	/* issue TASK_MGMT_ABORT_TASK */
	struct scsi_device *scsidev;
	struct visordisk_info *vdisk;
	struct visorhba_devdata *devdata;

	scsidev = scsicmd->device;
	devdata = (struct visorhba_devdata *)scsidev->host->hostdata;
	for_each_vdisk_match(vdisk, devdata, scsidev) {
		if (atomic_read(&vdisk->error_count) < VISORHBA_ERROR_COUNT)
			atomic_inc(&vdisk->error_count);
		else
			atomic_set(&vdisk->ios_threshold, IOS_ERROR_THRESHOLD);
	}
	return forward_taskmgmt_command(TASK_MGMT_ABORT_TASK, scsicmd);
}

/**
 *	visorhba_device_reset_handler - Send TASK_MGMT_LUN_RESET
 *	@scsicmd: The scsicmd that needs aborted
 *
 *	Returns SUCCESS if inserted, failure otherwise
 */
static int visorhba_device_reset_handler(struct scsi_cmnd *scsicmd)
{
	/* issue TASK_MGMT_LUN_RESET */
	struct scsi_device *scsidev;
	struct visordisk_info *vdisk;
	struct visorhba_devdata *devdata;

	scsidev = scsicmd->device;
	devdata = (struct visorhba_devdata *)scsidev->host->hostdata;
	for_each_vdisk_match(vdisk, devdata, scsidev) {
		if (atomic_read(&vdisk->error_count) < VISORHBA_ERROR_COUNT)
			atomic_inc(&vdisk->error_count);
		else
			atomic_set(&vdisk->ios_threshold, IOS_ERROR_THRESHOLD);
	}
	return forward_taskmgmt_command(TASK_MGMT_LUN_RESET, scsicmd);
}

/**
 *	visorhba_bus_reset_handler - Send TASK_MGMT_TARGET_RESET for each
 *				     target on the bus
 *	@scsicmd: The scsicmd that needs aborted
 *
 *	Returns SUCCESS
 */
static int visorhba_bus_reset_handler(struct scsi_cmnd *scsicmd)
{
	struct scsi_device *scsidev;
	struct visordisk_info *vdisk;
	struct visorhba_devdata *devdata;

	scsidev = scsicmd->device;
	devdata = (struct visorhba_devdata *)scsidev->host->hostdata;
	for_each_vdisk_match(vdisk, devdata, scsidev) {
		if (atomic_read(&vdisk->error_count) < VISORHBA_ERROR_COUNT)
			atomic_inc(&vdisk->error_count);
		else
			atomic_set(&vdisk->ios_threshold, IOS_ERROR_THRESHOLD);
	}
	return forward_taskmgmt_command(TASK_MGMT_BUS_RESET, scsicmd);
}

/**
 *	visorhba_host_reset_handler - Not supported
 *	@scsicmd: The scsicmd that needs aborted
 *
 *	Not supported, return SUCCESS
 *	Returns SUCCESS
 */
static int
visorhba_host_reset_handler(struct scsi_cmnd *scsicmd)
{
	/* issue TASK_MGMT_TARGET_RESET for each target on each bus for host */
	return SUCCESS;
}

/**
 *	visorhba_get_info
 *	@shp: Scsi host that is requesting information
 *
 *	Returns string with info
 */
static const char *visorhba_get_info(struct Scsi_Host *shp)
{
	/* Return version string */
	return "visorhba";
}

/**
 *	visorhba_queue_command_lck -- queues command to the Service Partition
 *	@scsicmd: Command to be queued
 *	@vsiorhba_cmnd_done: Done command to call when scsicmd is returned
 *
 *	Queues to scsicmd to the ServicePartition after converting it to a
 *	uiscmdrsp structure.
 *
 *	Returns success if queued to the Service Partition, otherwise
 *	failure.
 */
static int
visorhba_queue_command_lck(struct scsi_cmnd *scsicmd,
			   void (*visorhba_cmnd_done)(struct scsi_cmnd *))
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
	cmdrsp->scsi.data_dir = scsicmd->sc_data_direction;
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

	if (!visorchannel_signalinsert(devdata->dev->visorchannel,
				       IOCHAN_TO_IOPART,
				       cmdrsp))
		/* queue must be full and we aren't going to wait */
		goto err_del_scsipending_ent;

	return 0;

err_del_scsipending_ent:
	del_scsipending_ent(devdata, insert_location);
	return SCSI_MLQUEUE_DEVICE_BUSY;
}

/**
 *	visorhba_slave_alloc - called when new disk is discovered
 *	@scsidev: New disk
 *
 *	Create a new visordisk_info structure and add it to our
 *	list of vdisks.
 *
 *	Returns success when created, otherwise error.
 */
static int visorhba_slave_alloc(struct scsi_device *scsidev)
{
	/* this is called by the midlayer before scan for new devices --
	 * LLD can alloc any struct & do init if needed.
	 */
	struct visordisk_info *vdisk;
	struct visordisk_info *tmpvdisk;
	struct visorhba_devdata *devdata;
	struct Scsi_Host *scsihost = (struct Scsi_Host *)scsidev->host;

	devdata = (struct visorhba_devdata *)scsihost->hostdata;
	if (!devdata)
		return 0; /* even though we errored, treat as success */

	for_each_vdisk_match(vdisk, devdata, scsidev)
		return 0; /* already allocated return success */

	tmpvdisk = kzalloc(sizeof(*tmpvdisk), GFP_ATOMIC);
	if (!tmpvdisk)
		return -ENOMEM;

	tmpvdisk->channel = scsidev->channel;
	tmpvdisk->id = scsidev->id;
	tmpvdisk->lun = scsidev->lun;
	vdisk->next = tmpvdisk;
	return 0;
}

/**
 *	visorhba_slave_destroy - disk is going away
 *	@scsidev: scsi device going away
 *
 *	Disk is going away, clean up resources.
 *	Returns void.
 */
static void visorhba_slave_destroy(struct scsi_device *scsidev)
{
	/* midlevel calls this after device has been quiesced and
	 * before it is to be deleted.
	 */
	struct visordisk_info *vdisk, *delvdisk;
	struct visorhba_devdata *devdata;
	struct Scsi_Host *scsihost = (struct Scsi_Host *)scsidev->host;

	devdata = (struct visorhba_devdata *)scsihost->hostdata;
	for_each_vdisk_match(vdisk, devdata, scsidev) {
		delvdisk = vdisk->next;
		vdisk->next = delvdisk->next;
		kfree(delvdisk);
		return;
	}
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
	.use_clustering = ENABLE_CLUSTERING,
};

/**
 *	info_debugfs_read - debugfs interface to dump visorhba states
 *	@file: Debug file
 *	@buf: buffer to send back to user
 *	@len: len that can be written to buf
 *	@offset: offset into buf
 *
 *	Dumps information about the visorhba driver and devices
 *	TODO: Make this per vhba
 *	Returns bytes_read
 */
static ssize_t info_debugfs_read(struct file *file, char __user *buf,
				 size_t len, loff_t *offset)
{
	ssize_t bytes_read = 0;
	int str_pos = 0;
	u64 phys_flags_addr;
	int i;
	struct visorhba_devdata *devdata;
	char *vbuf;

	if (len > MAX_BUF)
		len = MAX_BUF;
	vbuf = kzalloc(len, GFP_KERNEL);
	if (!vbuf)
		return -ENOMEM;

	for (i = 0; i < VISORHBA_OPEN_MAX; i++) {
		if (!visorhbas_open[i].devdata)
			continue;

		devdata = visorhbas_open[i].devdata;

		str_pos += scnprintf(vbuf + str_pos,
				len - str_pos, "max_buff_len:%u\n",
				devdata->max_buff_len);

		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				"\ninterrupts_rcvd = %llu, interrupts_disabled = %llu\n",
				devdata->interrupts_rcvd,
				devdata->interrupts_disabled);
		str_pos += scnprintf(vbuf + str_pos,
				len - str_pos, "\ninterrupts_notme = %llu,\n",
				devdata->interrupts_notme);
		phys_flags_addr = virt_to_phys((__force  void *)
					       devdata->flags_addr);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos,
				"flags_addr = %p, phys_flags_addr=0x%016llx, FeatureFlags=%llu\n",
				devdata->flags_addr, phys_flags_addr,
				(__le64)readq(devdata->flags_addr));
		str_pos += scnprintf(vbuf + str_pos,
			len - str_pos, "acquire_failed_cnt:%llu\n",
			devdata->acquire_failed_cnt);
		str_pos += scnprintf(vbuf + str_pos, len - str_pos, "\n");
	}

	bytes_read = simple_read_from_buffer(buf, len, offset, vbuf, str_pos);
	kfree(vbuf);
	return bytes_read;
}

/**
 *	visorhba_serverdown_complete - Called when we are done cleaning up
 *				       from serverdown
 *	@work: work structure for this serverdown request
 *
 *	Called when we are done cleanning up from serverdown, stop processing
 *	queue, fail pending IOs.
 *	Returns void when finished cleaning up
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
			cmdrsp->scsitaskmgmt.notifyresult_handle
							= TASK_MGMT_FAILED;
			wake_up_all((wait_queue_head_t *)
				    cmdrsp->scsitaskmgmt.notify_handle);
			break;
		case CMD_VDISKMGMT_TYPE:
			cmdrsp = pendingdel->sent;
			cmdrsp->vdiskmgmt.notifyresult_handle
							= VDISK_MGMT_FAILED;
			wake_up_all((wait_queue_head_t *)
				    cmdrsp->vdiskmgmt.notify_handle);
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

/**
 *	visorhba_serverdown - Got notified that the IOVM is down
 *	@devdata: visorhba that is being serviced by downed IOVM.
 *
 *	Something happened to the IOVM, return immediately and
 *	schedule work cleanup work.
 *	Return SUCCESS or EINVAL
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

/**
 *	do_scsi_linuxstat - scsi command returned linuxstat
 *	@cmdrsp: response from IOVM
 *	@scsicmd: Command issued.
 *
 *	Don't log errors for disk-not-present inquiries
 *	Returns void
 */
static void
do_scsi_linuxstat(struct uiscmdrsp *cmdrsp, struct scsi_cmnd *scsicmd)
{
	struct visorhba_devdata *devdata;
	struct visordisk_info *vdisk;
	struct scsi_device *scsidev;

	scsidev = scsicmd->device;
	memcpy(scsicmd->sense_buffer, cmdrsp->scsi.sensebuf, MAX_SENSE_SIZE);

	/* Do not log errors for disk-not-present inquiries */
	if ((cmdrsp->scsi.cmnd[0] == INQUIRY) &&
	    (host_byte(cmdrsp->scsi.linuxstat) == DID_NO_CONNECT) &&
	    (cmdrsp->scsi.addlstat == ADDL_SEL_TIMEOUT))
		return;
	/* Okay see what our error_count is here.... */
	devdata = (struct visorhba_devdata *)scsidev->host->hostdata;
	for_each_vdisk_match(vdisk, devdata, scsidev) {
		if (atomic_read(&vdisk->error_count) < VISORHBA_ERROR_COUNT) {
			atomic_inc(&vdisk->error_count);
			atomic_set(&vdisk->ios_threshold, IOS_ERROR_THRESHOLD);
		}
	}
}

static int set_no_disk_inquiry_result(unsigned char *buf,
				      size_t len, bool is_lun0)
{
	if (!buf || len < NO_DISK_INQUIRY_RESULT_LEN)
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

/**
 *	do_scsi_nolinuxstat - scsi command didn't have linuxstat
 *	@cmdrsp: response from IOVM
 *	@scsicmd: Command issued.
 *
 *	Handle response when no linuxstat was returned
 *	Returns void
 */
static void
do_scsi_nolinuxstat(struct uiscmdrsp *cmdrsp, struct scsi_cmnd *scsicmd)
{
	struct scsi_device *scsidev;
	unsigned char buf[36];
	struct scatterlist *sg;
	unsigned int i;
	char *this_page;
	char *this_page_orig;
	int bufind = 0;
	struct visordisk_info *vdisk;
	struct visorhba_devdata *devdata;

	scsidev = scsicmd->device;
	if ((cmdrsp->scsi.cmnd[0] == INQUIRY) &&
	    (cmdrsp->scsi.bufflen >= MIN_INQUIRY_RESULT_LEN)) {
		if (cmdrsp->scsi.no_disk_result == 0)
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
			return;
		}

		sg = scsi_sglist(scsicmd);
		for (i = 0; i < scsi_sg_count(scsicmd); i++) {
			this_page_orig = kmap_atomic(sg_page(sg + i));
			this_page = (void *)((unsigned long)this_page_orig |
					     sg[i].offset);
			memcpy(this_page, buf + bufind, sg[i].length);
			kunmap_atomic(this_page_orig);
		}
	} else {
		devdata = (struct visorhba_devdata *)scsidev->host->hostdata;
		for_each_vdisk_match(vdisk, devdata, scsidev) {
			if (atomic_read(&vdisk->ios_threshold) > 0) {
				atomic_dec(&vdisk->ios_threshold);
				if (atomic_read(&vdisk->ios_threshold) == 0)
					atomic_set(&vdisk->error_count, 0);
			}
		}
	}
}

/**
 *	complete_scsi_command - complete a scsi command
 *	@uiscmdrsp: Response from Service Partition
 *	@scsicmd: The scsi command
 *
 *	Response returned by the Service Partition, finish it and send
 *	completion to the scsi midlayer.
 *	Returns void.
 */
static void
complete_scsi_command(struct uiscmdrsp *cmdrsp, struct scsi_cmnd *scsicmd)
{
	/* take what we need out of cmdrsp and complete the scsicmd */
	scsicmd->result = cmdrsp->scsi.linuxstat;
	if (cmdrsp->scsi.linuxstat)
		do_scsi_linuxstat(cmdrsp, scsicmd);
	else
		do_scsi_nolinuxstat(cmdrsp, scsicmd);

	scsicmd->scsi_done(scsicmd);
}

/* DELETE VDISK TASK MGMT COMMANDS */
static inline void complete_vdiskmgmt_command(struct uiscmdrsp *cmdrsp)
{
	/* copy the result of the taskmgmt and
	 * wake up the error handler that is waiting for this
	 */
	cmdrsp->vdiskmgmt.notifyresult_handle = cmdrsp->vdiskmgmt.result;
	wake_up_all((wait_queue_head_t *)cmdrsp->vdiskmgmt.notify_handle);
}

/**
 *	complete_taskmgmt_command - complete task management
 *	@cmdrsp: Response from the IOVM
 *
 *	Service Partition returned the result of the task management
 *	command. Wake up anyone waiting for it.
 *	Returns void
 */
static inline void complete_taskmgmt_command(struct uiscmdrsp *cmdrsp)
{
	/* copy the result of the taskgmgt and
	 * wake up the error handler that is waiting for this
	 */
	cmdrsp->vdiskmgmt.notifyresult_handle = cmdrsp->vdiskmgmt.result;
	wake_up_all((wait_queue_head_t *)cmdrsp->scsitaskmgmt.notify_handle);
}

static struct work_struct dar_work_queue;
static struct diskaddremove *dar_work_queue_head;
static spinlock_t dar_work_queue_lock; /* Lock to protet dar_work_queue_head */
static unsigned short dar_work_queue_sched;

/**
 *	queue_disk_add_remove - IOSP has sent us a add/remove request
 *	@dar: disk add/remove request
 *
 *	Queue the work needed to add/remove a disk.
 *	Returns void
 */
static inline void queue_disk_add_remove(struct diskaddremove *dar)
{
	unsigned long flags;

	spin_lock_irqsave(&dar_work_queue_lock, flags);
	if (!dar_work_queue_head) {
		dar_work_queue_head = dar;
		dar->next = NULL;
	} else {
		dar->next = dar_work_queue_head;
		dar_work_queue_head = dar;
	}
	if (!dar_work_queue_sched) {
		schedule_work(&dar_work_queue);
		dar_work_queue_sched = 1;
	}
	spin_unlock_irqrestore(&dar_work_queue_lock, flags);
}

/**
 *	process_disk_notify - IOSP has sent a process disk notify event
 *	@shost: Scsi hot
 *	@cmdrsp: Response from the IOSP
 *
 *	Queue it to the work queue.
 *	Return void.
 */
static void process_disk_notify(struct Scsi_Host *shost,
				struct uiscmdrsp *cmdrsp)
{
	struct diskaddremove *dar;

	dar = kzalloc(sizeof(*dar), GFP_ATOMIC);
	if (!dar)
		return;

	dar->add = cmdrsp->disknotify.add;
	dar->shost = shost;
	dar->channel = cmdrsp->disknotify.channel;
	dar->id = cmdrsp->disknotify.id;
	dar->lun = cmdrsp->disknotify.lun;
	queue_disk_add_remove(dar);
}

/**
 *	drain_queue - pull responses out of iochannel
 *	@cmdrsp: Response from the IOSP
 *	@devdata: device that owns this iochannel
 *
 *	Pulls responses out of the iochannel and process the responses.
 *	Restuns void
 */
static void
drain_queue(struct uiscmdrsp *cmdrsp, struct visorhba_devdata *devdata)
{
	struct scsi_cmnd *scsicmd;
	struct Scsi_Host *shost = devdata->scsihost;

	while (1) {
		if (!visorchannel_signalremove(devdata->dev->visorchannel,
					       IOCHAN_FROM_IOPART,
					       cmdrsp))
			break; /* queue empty */

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
			complete_taskmgmt_command(cmdrsp);
		} else if (cmdrsp->cmdtype == CMD_NOTIFYGUEST_TYPE) {
			/* The vHba pointer has no meaning in a
			 * guest partition. Let's be safe and set it
			 * to NULL now. Do not use it here!
			 */
			cmdrsp->disknotify.v_hba = NULL;
			process_disk_notify(shost, cmdrsp);
		} else if (cmdrsp->cmdtype == CMD_VDISKMGMT_TYPE) {
			if (!del_scsipending_ent(devdata,
						 cmdrsp->vdiskmgmt.handle))
				break;
			complete_vdiskmgmt_command(cmdrsp);
		}
		/* cmdrsp is now available for resuse */
	}
}

/**
 *	process_incoming_rsps - Process responses from IOSP
 *	@v: void pointer to visorhba_devdata
 *
 *	Main function for the thread that processes the responses
 *	from the IO Service Partition. When the queue is empty, wait
 *	to check to see if it is full again.
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

/**
 *	visorhba_pause - function to handle visorbus pause messages
 *	@dev: device that is pausing.
 *	@complete_func: function to call when finished
 *
 *	Something has happened to the IO Service Partition that is
 *	handling this device. Quiet this device and reset commands
 *	so that the Service Partition can be corrected.
 *	Returns SUCCESS
 */
static int visorhba_pause(struct visor_device *dev,
			  visorbus_state_complete_func complete_func)
{
	struct visorhba_devdata *devdata = dev_get_drvdata(&dev->device);

	visorhba_serverdown(devdata);
	complete_func(dev, 0);
	return 0;
}

/**
 *	visorhba_resume - function called when the IO Service Partition is back
 *	@dev: device that is pausing.
 *	@complete_func: function to call when finished
 *
 *	Yay! The IO Service Partition is back, the channel has been wiped
 *	so lets re-establish connection and start processing responses.
 *	Returns 0 on success, error on failure.
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

/**
 *	visorhba_probe - device has been discovered, do acquire
 *	@dev: visor_device that was discovered
 *
 *	A new HBA was discovered, do the initial connections of it.
 *	Return 0 on success, otherwise error.
 */
static int visorhba_probe(struct visor_device *dev)
{
	struct Scsi_Host *scsihost;
	struct vhba_config_max max;
	struct visorhba_devdata *devdata = NULL;
	int i, err, channel_offset;
	u64 features;

	scsihost = scsi_host_alloc(&visorhba_driver_template,
				   sizeof(*devdata));
	if (!scsihost)
		return -ENODEV;

	channel_offset = offsetof(struct spar_io_channel_protocol,
				  vhba.max);
	err = visorbus_read_channel(dev, channel_offset, &max,
				    sizeof(struct vhba_config_max));
	if (err < 0)
		goto err_scsi_host_put;

	scsihost->max_id = (unsigned)max.max_id;
	scsihost->max_lun = (unsigned)max.max_lun;
	scsihost->cmd_per_lun = (unsigned)max.cmd_per_lun;
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
	for (i = 0; i < VISORHBA_OPEN_MAX; i++) {
		if (!visorhbas_open[i].devdata) {
			visorhbas_open[i].devdata = devdata;
			break;
		}
	}

	devdata->dev = dev;
	dev_set_drvdata(&dev->device, devdata);

	init_waitqueue_head(&devdata->rsp_queue);
	spin_lock_init(&devdata->privlock);
	devdata->serverdown = false;
	devdata->serverchangingstate = false;
	devdata->scsihost = scsihost;

	channel_offset = offsetof(struct spar_io_channel_protocol,
				  channel_header.features);
	err = visorbus_read_channel(dev, channel_offset, &features, 8);
	if (err)
		goto err_scsi_remove_host;
	features |= ULTRA_IO_CHANNEL_IS_POLLING;
	err = visorbus_write_channel(dev, channel_offset, &features, 8);
	if (err)
		goto err_scsi_remove_host;

	devdata->thread_wait_ms = 2;
	devdata->thread = visor_thread_start(process_incoming_rsps, devdata,
					     "vhba_incoming");

	scsi_scan_host(scsihost);

	return 0;

err_scsi_remove_host:
	scsi_remove_host(scsihost);

err_scsi_host_put:
	scsi_host_put(scsihost);
	return err;
}

/**
 *	visorhba_remove - remove a visorhba device
 *	@dev: Device to remove
 *
 *	Removes the visorhba device.
 *	Returns void.
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

	dev_set_drvdata(&dev->device, NULL);
}

/**
 *	visorhba_init		- driver init routine
 *
 *	Initialize the visorhba driver and register it with visorbus
 *	to handle s-Par virtual host bus adapter.
 */
static int visorhba_init(void)
{
	struct dentry *ret;
	int rc = -ENOMEM;

	visorhba_debugfs_dir = debugfs_create_dir("visorhba", NULL);
	if (!visorhba_debugfs_dir)
		return -ENOMEM;

	ret = debugfs_create_file("info", S_IRUSR, visorhba_debugfs_dir, NULL,
				  &debugfs_info_fops);

	if (!ret) {
		rc = -EIO;
		goto cleanup_debugfs;
	}

	rc = visorbus_register_visor_driver(&visorhba_driver);
	if (rc)
		goto cleanup_debugfs;

	return rc;

cleanup_debugfs:
	debugfs_remove_recursive(visorhba_debugfs_dir);

	return rc;
}

/**
 *	visorhba_cleanup	- driver exit routine
 *
 *	Unregister driver from the bus and free up memory.
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
MODULE_DESCRIPTION("s-Par hba driver");
