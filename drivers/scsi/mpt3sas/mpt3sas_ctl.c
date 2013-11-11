/*
 * Management Module Support for MPT (Message Passing Technology) based
 * controllers
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_ctl.c
 * Copyright (C) 2012  LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/compat.h>
#include <linux/poll.h>

#include <linux/io.h>
#include <linux/uaccess.h>

#include "mpt3sas_base.h"
#include "mpt3sas_ctl.h"


static struct fasync_struct *async_queue;
static DECLARE_WAIT_QUEUE_HEAD(ctl_poll_wait);


/**
 * enum block_state - blocking state
 * @NON_BLOCKING: non blocking
 * @BLOCKING: blocking
 *
 * These states are for ioctls that need to wait for a response
 * from firmware, so they probably require sleep.
 */
enum block_state {
	NON_BLOCKING,
	BLOCKING,
};

#ifdef CONFIG_SCSI_MPT3SAS_LOGGING
/**
 * _ctl_sas_device_find_by_handle - sas device search
 * @ioc: per adapter object
 * @handle: sas device handle (assigned by firmware)
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for sas_device based on sas_address, then return sas_device
 * object.
 */
static struct _sas_device *
_ctl_sas_device_find_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_device *sas_device, *r;

	r = NULL;
	list_for_each_entry(sas_device, &ioc->sas_device_list, list) {
		if (sas_device->handle != handle)
			continue;
		r = sas_device;
		goto out;
	}

 out:
	return r;
}

/**
 * _ctl_display_some_debug - debug routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @calling_function_name: string pass from calling function
 * @mpi_reply: reply message frame
 * Context: none.
 *
 * Function for displaying debug info helpful when debugging issues
 * in this module.
 */
static void
_ctl_display_some_debug(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	char *calling_function_name, MPI2DefaultReply_t *mpi_reply)
{
	Mpi2ConfigRequest_t *mpi_request;
	char *desc = NULL;

	if (!(ioc->logging_level & MPT_DEBUG_IOCTL))
		return;

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	switch (mpi_request->Function) {
	case MPI2_FUNCTION_SCSI_IO_REQUEST:
	{
		Mpi2SCSIIORequest_t *scsi_request =
		    (Mpi2SCSIIORequest_t *)mpi_request;

		snprintf(ioc->tmp_string, MPT_STRING_LENGTH,
		    "scsi_io, cmd(0x%02x), cdb_len(%d)",
		    scsi_request->CDB.CDB32[0],
		    le16_to_cpu(scsi_request->IoFlags) & 0xF);
		desc = ioc->tmp_string;
		break;
	}
	case MPI2_FUNCTION_SCSI_TASK_MGMT:
		desc = "task_mgmt";
		break;
	case MPI2_FUNCTION_IOC_INIT:
		desc = "ioc_init";
		break;
	case MPI2_FUNCTION_IOC_FACTS:
		desc = "ioc_facts";
		break;
	case MPI2_FUNCTION_CONFIG:
	{
		Mpi2ConfigRequest_t *config_request =
		    (Mpi2ConfigRequest_t *)mpi_request;

		snprintf(ioc->tmp_string, MPT_STRING_LENGTH,
		    "config, type(0x%02x), ext_type(0x%02x), number(%d)",
		    (config_request->Header.PageType &
		     MPI2_CONFIG_PAGETYPE_MASK), config_request->ExtPageType,
		    config_request->Header.PageNumber);
		desc = ioc->tmp_string;
		break;
	}
	case MPI2_FUNCTION_PORT_FACTS:
		desc = "port_facts";
		break;
	case MPI2_FUNCTION_PORT_ENABLE:
		desc = "port_enable";
		break;
	case MPI2_FUNCTION_EVENT_NOTIFICATION:
		desc = "event_notification";
		break;
	case MPI2_FUNCTION_FW_DOWNLOAD:
		desc = "fw_download";
		break;
	case MPI2_FUNCTION_FW_UPLOAD:
		desc = "fw_upload";
		break;
	case MPI2_FUNCTION_RAID_ACTION:
		desc = "raid_action";
		break;
	case MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH:
	{
		Mpi2SCSIIORequest_t *scsi_request =
		    (Mpi2SCSIIORequest_t *)mpi_request;

		snprintf(ioc->tmp_string, MPT_STRING_LENGTH,
		    "raid_pass, cmd(0x%02x), cdb_len(%d)",
		    scsi_request->CDB.CDB32[0],
		    le16_to_cpu(scsi_request->IoFlags) & 0xF);
		desc = ioc->tmp_string;
		break;
	}
	case MPI2_FUNCTION_SAS_IO_UNIT_CONTROL:
		desc = "sas_iounit_cntl";
		break;
	case MPI2_FUNCTION_SATA_PASSTHROUGH:
		desc = "sata_pass";
		break;
	case MPI2_FUNCTION_DIAG_BUFFER_POST:
		desc = "diag_buffer_post";
		break;
	case MPI2_FUNCTION_DIAG_RELEASE:
		desc = "diag_release";
		break;
	case MPI2_FUNCTION_SMP_PASSTHROUGH:
		desc = "smp_passthrough";
		break;
	}

	if (!desc)
		return;

	pr_info(MPT3SAS_FMT "%s: %s, smid(%d)\n",
	    ioc->name, calling_function_name, desc, smid);

	if (!mpi_reply)
		return;

	if (mpi_reply->IOCStatus || mpi_reply->IOCLogInfo)
		pr_info(MPT3SAS_FMT
		    "\tiocstatus(0x%04x), loginfo(0x%08x)\n",
		    ioc->name, le16_to_cpu(mpi_reply->IOCStatus),
		    le32_to_cpu(mpi_reply->IOCLogInfo));

	if (mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST ||
	    mpi_request->Function ==
	    MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH) {
		Mpi2SCSIIOReply_t *scsi_reply =
		    (Mpi2SCSIIOReply_t *)mpi_reply;
		struct _sas_device *sas_device = NULL;
		unsigned long flags;

		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = _ctl_sas_device_find_by_handle(ioc,
		    le16_to_cpu(scsi_reply->DevHandle));
		if (sas_device) {
			pr_warn(MPT3SAS_FMT "\tsas_address(0x%016llx), phy(%d)\n",
				ioc->name, (unsigned long long)
			    sas_device->sas_address, sas_device->phy);
			pr_warn(MPT3SAS_FMT
			    "\tenclosure_logical_id(0x%016llx), slot(%d)\n",
			    ioc->name, (unsigned long long)
			    sas_device->enclosure_logical_id, sas_device->slot);
		}
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		if (scsi_reply->SCSIState || scsi_reply->SCSIStatus)
			pr_info(MPT3SAS_FMT
			    "\tscsi_state(0x%02x), scsi_status"
			    "(0x%02x)\n", ioc->name,
			    scsi_reply->SCSIState,
			    scsi_reply->SCSIStatus);
	}
}

#endif

/**
 * mpt3sas_ctl_done - ctl module completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: none.
 *
 * The callback handler when using ioc->ctl_cb_idx.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
u8
mpt3sas_ctl_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;
	Mpi2SCSIIOReply_t *scsiio_reply;
	const void *sense_data;
	u32 sz;

	if (ioc->ctl_cmds.status == MPT3_CMD_NOT_USED)
		return 1;
	if (ioc->ctl_cmds.smid != smid)
		return 1;
	ioc->ctl_cmds.status |= MPT3_CMD_COMPLETE;
	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply) {
		memcpy(ioc->ctl_cmds.reply, mpi_reply, mpi_reply->MsgLength*4);
		ioc->ctl_cmds.status |= MPT3_CMD_REPLY_VALID;
		/* get sense data */
		if (mpi_reply->Function == MPI2_FUNCTION_SCSI_IO_REQUEST ||
		    mpi_reply->Function ==
		    MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH) {
			scsiio_reply = (Mpi2SCSIIOReply_t *)mpi_reply;
			if (scsiio_reply->SCSIState &
			    MPI2_SCSI_STATE_AUTOSENSE_VALID) {
				sz = min_t(u32, SCSI_SENSE_BUFFERSIZE,
				    le32_to_cpu(scsiio_reply->SenseCount));
				sense_data = mpt3sas_base_get_sense_buffer(ioc,
				    smid);
				memcpy(ioc->ctl_cmds.sense, sense_data, sz);
			}
		}
	}
#ifdef CONFIG_SCSI_MPT3SAS_LOGGING
	_ctl_display_some_debug(ioc, smid, "ctl_done", mpi_reply);
#endif
	ioc->ctl_cmds.status &= ~MPT3_CMD_PENDING;
	complete(&ioc->ctl_cmds.done);
	return 1;
}

/**
 * _ctl_check_event_type - determines when an event needs logging
 * @ioc: per adapter object
 * @event: firmware event
 *
 * The bitmask in ioc->event_type[] indicates which events should be
 * be saved in the driver event_log.  This bitmask is set by application.
 *
 * Returns 1 when event should be captured, or zero means no match.
 */
static int
_ctl_check_event_type(struct MPT3SAS_ADAPTER *ioc, u16 event)
{
	u16 i;
	u32 desired_event;

	if (event >= 128 || !event || !ioc->event_log)
		return 0;

	desired_event = (1 << (event % 32));
	if (!desired_event)
		desired_event = 1;
	i = event / 32;
	return desired_event & ioc->event_type[i];
}

/**
 * mpt3sas_ctl_add_to_event_log - add event
 * @ioc: per adapter object
 * @mpi_reply: reply message frame
 *
 * Return nothing.
 */
void
mpt3sas_ctl_add_to_event_log(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventNotificationReply_t *mpi_reply)
{
	struct MPT3_IOCTL_EVENTS *event_log;
	u16 event;
	int i;
	u32 sz, event_data_sz;
	u8 send_aen = 0;

	if (!ioc->event_log)
		return;

	event = le16_to_cpu(mpi_reply->Event);

	if (_ctl_check_event_type(ioc, event)) {

		/* insert entry into circular event_log */
		i = ioc->event_context % MPT3SAS_CTL_EVENT_LOG_SIZE;
		event_log = ioc->event_log;
		event_log[i].event = event;
		event_log[i].context = ioc->event_context++;

		event_data_sz = le16_to_cpu(mpi_reply->EventDataLength)*4;
		sz = min_t(u32, event_data_sz, MPT3_EVENT_DATA_SIZE);
		memset(event_log[i].data, 0, MPT3_EVENT_DATA_SIZE);
		memcpy(event_log[i].data, mpi_reply->EventData, sz);
		send_aen = 1;
	}

	/* This aen_event_read_flag flag is set until the
	 * application has read the event log.
	 * For MPI2_EVENT_LOG_ENTRY_ADDED, we always notify.
	 */
	if (event == MPI2_EVENT_LOG_ENTRY_ADDED ||
	    (send_aen && !ioc->aen_event_read_flag)) {
		ioc->aen_event_read_flag = 1;
		wake_up_interruptible(&ctl_poll_wait);
		if (async_queue)
			kill_fasync(&async_queue, SIGIO, POLL_IN);
	}
}

/**
 * mpt3sas_ctl_event_callback - firmware event handler (called at ISR time)
 * @ioc: per adapter object
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt.
 *
 * This function merely adds a new work task into ioc->firmware_event_thread.
 * The tasks are worked from _firmware_event_work in user context.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
u8
mpt3sas_ctl_event_callback(struct MPT3SAS_ADAPTER *ioc, u8 msix_index,
	u32 reply)
{
	Mpi2EventNotificationReply_t *mpi_reply;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	mpt3sas_ctl_add_to_event_log(ioc, mpi_reply);
	return 1;
}

/**
 * _ctl_verify_adapter - validates ioc_number passed from application
 * @ioc: per adapter object
 * @iocpp: The ioc pointer is returned in this.
 *
 * Return (-1) means error, else ioc_number.
 */
static int
_ctl_verify_adapter(int ioc_number, struct MPT3SAS_ADAPTER **iocpp)
{
	struct MPT3SAS_ADAPTER *ioc;

	list_for_each_entry(ioc, &mpt3sas_ioc_list, list) {
		if (ioc->id != ioc_number)
			continue;
		*iocpp = ioc;
		return ioc_number;
	}
	*iocpp = NULL;
	return -1;
}

/**
 * mpt3sas_ctl_reset_handler - reset callback handler (for ctl)
 * @ioc: per adapter object
 * @reset_phase: phase
 *
 * The handler for doing any required cleanup or initialization.
 *
 * The reset phase can be MPT3_IOC_PRE_RESET, MPT3_IOC_AFTER_RESET,
 * MPT3_IOC_DONE_RESET
 */
void
mpt3sas_ctl_reset_handler(struct MPT3SAS_ADAPTER *ioc, int reset_phase)
{
	int i;
	u8 issue_reset;

	switch (reset_phase) {
	case MPT3_IOC_PRE_RESET:
		dtmprintk(ioc, pr_info(MPT3SAS_FMT
			"%s: MPT3_IOC_PRE_RESET\n", ioc->name, __func__));
		for (i = 0; i < MPI2_DIAG_BUF_TYPE_COUNT; i++) {
			if (!(ioc->diag_buffer_status[i] &
			    MPT3_DIAG_BUFFER_IS_REGISTERED))
				continue;
			if ((ioc->diag_buffer_status[i] &
			    MPT3_DIAG_BUFFER_IS_RELEASED))
				continue;
			mpt3sas_send_diag_release(ioc, i, &issue_reset);
		}
		break;
	case MPT3_IOC_AFTER_RESET:
		dtmprintk(ioc, pr_info(MPT3SAS_FMT
			"%s: MPT3_IOC_AFTER_RESET\n", ioc->name, __func__));
		if (ioc->ctl_cmds.status & MPT3_CMD_PENDING) {
			ioc->ctl_cmds.status |= MPT3_CMD_RESET;
			mpt3sas_base_free_smid(ioc, ioc->ctl_cmds.smid);
			complete(&ioc->ctl_cmds.done);
		}
		break;
	case MPT3_IOC_DONE_RESET:
		dtmprintk(ioc, pr_info(MPT3SAS_FMT
			"%s: MPT3_IOC_DONE_RESET\n", ioc->name, __func__));

		for (i = 0; i < MPI2_DIAG_BUF_TYPE_COUNT; i++) {
			if (!(ioc->diag_buffer_status[i] &
			    MPT3_DIAG_BUFFER_IS_REGISTERED))
				continue;
			if ((ioc->diag_buffer_status[i] &
			    MPT3_DIAG_BUFFER_IS_RELEASED))
				continue;
			ioc->diag_buffer_status[i] |=
			    MPT3_DIAG_BUFFER_IS_DIAG_RESET;
		}
		break;
	}
}

/**
 * _ctl_fasync -
 * @fd -
 * @filep -
 * @mode -
 *
 * Called when application request fasyn callback handler.
 */
static int
_ctl_fasync(int fd, struct file *filep, int mode)
{
	return fasync_helper(fd, filep, mode, &async_queue);
}

/**
 * _ctl_poll -
 * @file -
 * @wait -
 *
 */
static unsigned int
_ctl_poll(struct file *filep, poll_table *wait)
{
	struct MPT3SAS_ADAPTER *ioc;

	poll_wait(filep, &ctl_poll_wait, wait);

	list_for_each_entry(ioc, &mpt3sas_ioc_list, list) {
		if (ioc->aen_event_read_flag)
			return POLLIN | POLLRDNORM;
	}
	return 0;
}

/**
 * _ctl_set_task_mid - assign an active smid to tm request
 * @ioc: per adapter object
 * @karg - (struct mpt3_ioctl_command)
 * @tm_request - pointer to mf from user space
 *
 * Returns 0 when an smid if found, else fail.
 * during failure, the reply frame is filled.
 */
static int
_ctl_set_task_mid(struct MPT3SAS_ADAPTER *ioc, struct mpt3_ioctl_command *karg,
	Mpi2SCSITaskManagementRequest_t *tm_request)
{
	u8 found = 0;
	u16 i;
	u16 handle;
	struct scsi_cmnd *scmd;
	struct MPT3SAS_DEVICE *priv_data;
	unsigned long flags;
	Mpi2SCSITaskManagementReply_t *tm_reply;
	u32 sz;
	u32 lun;
	char *desc = NULL;

	if (tm_request->TaskType == MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK)
		desc = "abort_task";
	else if (tm_request->TaskType == MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK)
		desc = "query_task";
	else
		return 0;

	lun = scsilun_to_int((struct scsi_lun *)tm_request->LUN);

	handle = le16_to_cpu(tm_request->DevHandle);
	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	for (i = ioc->scsiio_depth; i && !found; i--) {
		scmd = ioc->scsi_lookup[i - 1].scmd;
		if (scmd == NULL || scmd->device == NULL ||
		    scmd->device->hostdata == NULL)
			continue;
		if (lun != scmd->device->lun)
			continue;
		priv_data = scmd->device->hostdata;
		if (priv_data->sas_target == NULL)
			continue;
		if (priv_data->sas_target->handle != handle)
			continue;
		tm_request->TaskMID = cpu_to_le16(ioc->scsi_lookup[i - 1].smid);
		found = 1;
	}
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

	if (!found) {
		dctlprintk(ioc, pr_info(MPT3SAS_FMT
			"%s: handle(0x%04x), lun(%d), no active mid!!\n",
			ioc->name,
		    desc, le16_to_cpu(tm_request->DevHandle), lun));
		tm_reply = ioc->ctl_cmds.reply;
		tm_reply->DevHandle = tm_request->DevHandle;
		tm_reply->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
		tm_reply->TaskType = tm_request->TaskType;
		tm_reply->MsgLength = sizeof(Mpi2SCSITaskManagementReply_t)/4;
		tm_reply->VP_ID = tm_request->VP_ID;
		tm_reply->VF_ID = tm_request->VF_ID;
		sz = min_t(u32, karg->max_reply_bytes, ioc->reply_sz);
		if (copy_to_user(karg->reply_frame_buf_ptr, ioc->ctl_cmds.reply,
		    sz))
			pr_err("failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
		return 1;
	}

	dctlprintk(ioc, pr_info(MPT3SAS_FMT
		"%s: handle(0x%04x), lun(%d), task_mid(%d)\n", ioc->name,
	    desc, le16_to_cpu(tm_request->DevHandle), lun,
	     le16_to_cpu(tm_request->TaskMID)));
	return 0;
}

/**
 * _ctl_do_mpt_command - main handler for MPT3COMMAND opcode
 * @ioc: per adapter object
 * @karg - (struct mpt3_ioctl_command)
 * @mf - pointer to mf in user space
 */
static long
_ctl_do_mpt_command(struct MPT3SAS_ADAPTER *ioc, struct mpt3_ioctl_command karg,
	void __user *mf)
{
	MPI2RequestHeader_t *mpi_request = NULL, *request;
	MPI2DefaultReply_t *mpi_reply;
	u32 ioc_state;
	u16 ioc_status;
	u16 smid;
	unsigned long timeout, timeleft;
	u8 issue_reset;
	u32 sz;
	void *psge;
	void *data_out = NULL;
	dma_addr_t data_out_dma = 0;
	size_t data_out_sz = 0;
	void *data_in = NULL;
	dma_addr_t data_in_dma = 0;
	size_t data_in_sz = 0;
	long ret;
	u16 wait_state_count;

	issue_reset = 0;

	if (ioc->ctl_cmds.status != MPT3_CMD_NOT_USED) {
		pr_err(MPT3SAS_FMT "%s: ctl_cmd in use\n",
		    ioc->name, __func__);
		ret = -EAGAIN;
		goto out;
	}

	wait_state_count = 0;
	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	while (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		if (wait_state_count++ == 10) {
			pr_err(MPT3SAS_FMT
			    "%s: failed due to ioc not operational\n",
			    ioc->name, __func__);
			ret = -EFAULT;
			goto out;
		}
		ssleep(1);
		ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
		pr_info(MPT3SAS_FMT
			"%s: waiting for operational state(count=%d)\n",
			ioc->name,
		    __func__, wait_state_count);
	}
	if (wait_state_count)
		pr_info(MPT3SAS_FMT "%s: ioc is operational\n",
		    ioc->name, __func__);

	mpi_request = kzalloc(ioc->request_sz, GFP_KERNEL);
	if (!mpi_request) {
		pr_err(MPT3SAS_FMT
			"%s: failed obtaining a memory for mpi_request\n",
			ioc->name, __func__);
		ret = -ENOMEM;
		goto out;
	}

	/* Check for overflow and wraparound */
	if (karg.data_sge_offset * 4 > ioc->request_sz ||
	    karg.data_sge_offset > (UINT_MAX / 4)) {
		ret = -EINVAL;
		goto out;
	}

	/* copy in request message frame from user */
	if (copy_from_user(mpi_request, mf, karg.data_sge_offset*4)) {
		pr_err("failure at %s:%d/%s()!\n", __FILE__, __LINE__,
		    __func__);
		ret = -EFAULT;
		goto out;
	}

	if (mpi_request->Function == MPI2_FUNCTION_SCSI_TASK_MGMT) {
		smid = mpt3sas_base_get_smid_hpr(ioc, ioc->ctl_cb_idx);
		if (!smid) {
			pr_err(MPT3SAS_FMT "%s: failed obtaining a smid\n",
			    ioc->name, __func__);
			ret = -EAGAIN;
			goto out;
		}
	} else {

		smid = mpt3sas_base_get_smid_scsiio(ioc, ioc->ctl_cb_idx, NULL);
		if (!smid) {
			pr_err(MPT3SAS_FMT "%s: failed obtaining a smid\n",
			    ioc->name, __func__);
			ret = -EAGAIN;
			goto out;
		}
	}

	ret = 0;
	ioc->ctl_cmds.status = MPT3_CMD_PENDING;
	memset(ioc->ctl_cmds.reply, 0, ioc->reply_sz);
	request = mpt3sas_base_get_msg_frame(ioc, smid);
	memcpy(request, mpi_request, karg.data_sge_offset*4);
	ioc->ctl_cmds.smid = smid;
	data_out_sz = karg.data_out_size;
	data_in_sz = karg.data_in_size;

	if (mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST ||
	    mpi_request->Function == MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH) {
		if (!le16_to_cpu(mpi_request->FunctionDependent1) ||
		    le16_to_cpu(mpi_request->FunctionDependent1) >
		    ioc->facts.MaxDevHandle) {
			ret = -EINVAL;
			mpt3sas_base_free_smid(ioc, smid);
			goto out;
		}
	}

	/* obtain dma-able memory for data transfer */
	if (data_out_sz) /* WRITE */ {
		data_out = pci_alloc_consistent(ioc->pdev, data_out_sz,
		    &data_out_dma);
		if (!data_out) {
			pr_err("failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			ret = -ENOMEM;
			mpt3sas_base_free_smid(ioc, smid);
			goto out;
		}
		if (copy_from_user(data_out, karg.data_out_buf_ptr,
			data_out_sz)) {
			pr_err("failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			ret =  -EFAULT;
			mpt3sas_base_free_smid(ioc, smid);
			goto out;
		}
	}

	if (data_in_sz) /* READ */ {
		data_in = pci_alloc_consistent(ioc->pdev, data_in_sz,
		    &data_in_dma);
		if (!data_in) {
			pr_err("failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			ret = -ENOMEM;
			mpt3sas_base_free_smid(ioc, smid);
			goto out;
		}
	}

	psge = (void *)request + (karg.data_sge_offset*4);

	/* send command to firmware */
#ifdef CONFIG_SCSI_MPT3SAS_LOGGING
	_ctl_display_some_debug(ioc, smid, "ctl_request", NULL);
#endif

	init_completion(&ioc->ctl_cmds.done);
	switch (mpi_request->Function) {
	case MPI2_FUNCTION_SCSI_IO_REQUEST:
	case MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH:
	{
		Mpi2SCSIIORequest_t *scsiio_request =
		    (Mpi2SCSIIORequest_t *)request;
		scsiio_request->SenseBufferLength = SCSI_SENSE_BUFFERSIZE;
		scsiio_request->SenseBufferLowAddress =
		    mpt3sas_base_get_sense_buffer_dma(ioc, smid);
		memset(ioc->ctl_cmds.sense, 0, SCSI_SENSE_BUFFERSIZE);
		ioc->build_sg(ioc, psge, data_out_dma, data_out_sz,
		    data_in_dma, data_in_sz);

		if (mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST)
			mpt3sas_base_put_smid_scsi_io(ioc, smid,
			    le16_to_cpu(mpi_request->FunctionDependent1));
		else
			mpt3sas_base_put_smid_default(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_SCSI_TASK_MGMT:
	{
		Mpi2SCSITaskManagementRequest_t *tm_request =
		    (Mpi2SCSITaskManagementRequest_t *)request;

		dtmprintk(ioc, pr_info(MPT3SAS_FMT
			"TASK_MGMT: handle(0x%04x), task_type(0x%02x)\n",
			ioc->name,
		    le16_to_cpu(tm_request->DevHandle), tm_request->TaskType));

		if (tm_request->TaskType ==
		    MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK ||
		    tm_request->TaskType ==
		    MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK) {
			if (_ctl_set_task_mid(ioc, &karg, tm_request)) {
				mpt3sas_base_free_smid(ioc, smid);
				goto out;
			}
		}

		mpt3sas_scsih_set_tm_flag(ioc, le16_to_cpu(
		    tm_request->DevHandle));
		ioc->build_sg_mpi(ioc, psge, data_out_dma, data_out_sz,
		    data_in_dma, data_in_sz);
		mpt3sas_base_put_smid_hi_priority(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_SMP_PASSTHROUGH:
	{
		Mpi2SmpPassthroughRequest_t *smp_request =
		    (Mpi2SmpPassthroughRequest_t *)mpi_request;
		u8 *data;

		/* ioc determines which port to use */
		smp_request->PhysicalPort = 0xFF;
		if (smp_request->PassthroughFlags &
		    MPI2_SMP_PT_REQ_PT_FLAGS_IMMEDIATE)
			data = (u8 *)&smp_request->SGL;
		else {
			if (unlikely(data_out == NULL)) {
				pr_err("failure at %s:%d/%s()!\n",
				    __FILE__, __LINE__, __func__);
				mpt3sas_base_free_smid(ioc, smid);
				ret = -EINVAL;
				goto out;
			}
			data = data_out;
		}

		if (data[1] == 0x91 && (data[10] == 1 || data[10] == 2)) {
			ioc->ioc_link_reset_in_progress = 1;
			ioc->ignore_loginfos = 1;
		}
		ioc->build_sg(ioc, psge, data_out_dma, data_out_sz, data_in_dma,
		    data_in_sz);
		mpt3sas_base_put_smid_default(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_SATA_PASSTHROUGH:
	case MPI2_FUNCTION_FW_DOWNLOAD:
	case MPI2_FUNCTION_FW_UPLOAD:
	{
		ioc->build_sg(ioc, psge, data_out_dma, data_out_sz, data_in_dma,
		    data_in_sz);
		mpt3sas_base_put_smid_default(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_TOOLBOX:
	{
		Mpi2ToolboxCleanRequest_t *toolbox_request =
			(Mpi2ToolboxCleanRequest_t *)mpi_request;

		if (toolbox_request->Tool == MPI2_TOOLBOX_DIAGNOSTIC_CLI_TOOL) {
			ioc->build_sg(ioc, psge, data_out_dma, data_out_sz,
				data_in_dma, data_in_sz);
		} else {
			ioc->build_sg_mpi(ioc, psge, data_out_dma, data_out_sz,
				data_in_dma, data_in_sz);
		}
		mpt3sas_base_put_smid_default(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_SAS_IO_UNIT_CONTROL:
	{
		Mpi2SasIoUnitControlRequest_t *sasiounit_request =
		    (Mpi2SasIoUnitControlRequest_t *)mpi_request;

		if (sasiounit_request->Operation == MPI2_SAS_OP_PHY_HARD_RESET
		    || sasiounit_request->Operation ==
		    MPI2_SAS_OP_PHY_LINK_RESET) {
			ioc->ioc_link_reset_in_progress = 1;
			ioc->ignore_loginfos = 1;
		}
		/* drop to default case for posting the request */
	}
	default:
		ioc->build_sg_mpi(ioc, psge, data_out_dma, data_out_sz,
		    data_in_dma, data_in_sz);
		mpt3sas_base_put_smid_default(ioc, smid);
		break;
	}

	if (karg.timeout < MPT3_IOCTL_DEFAULT_TIMEOUT)
		timeout = MPT3_IOCTL_DEFAULT_TIMEOUT;
	else
		timeout = karg.timeout;
	timeleft = wait_for_completion_timeout(&ioc->ctl_cmds.done,
	    timeout*HZ);
	if (mpi_request->Function == MPI2_FUNCTION_SCSI_TASK_MGMT) {
		Mpi2SCSITaskManagementRequest_t *tm_request =
		    (Mpi2SCSITaskManagementRequest_t *)mpi_request;
		mpt3sas_scsih_clear_tm_flag(ioc, le16_to_cpu(
		    tm_request->DevHandle));
		mpt3sas_trigger_master(ioc, MASTER_TRIGGER_TASK_MANAGMENT);
	} else if ((mpi_request->Function == MPI2_FUNCTION_SMP_PASSTHROUGH ||
	    mpi_request->Function == MPI2_FUNCTION_SAS_IO_UNIT_CONTROL) &&
		ioc->ioc_link_reset_in_progress) {
		ioc->ioc_link_reset_in_progress = 0;
		ioc->ignore_loginfos = 0;
	}
	if (!(ioc->ctl_cmds.status & MPT3_CMD_COMPLETE)) {
		pr_err(MPT3SAS_FMT "%s: timeout\n", ioc->name,
		    __func__);
		_debug_dump_mf(mpi_request, karg.data_sge_offset);
		if (!(ioc->ctl_cmds.status & MPT3_CMD_RESET))
			issue_reset = 1;
		goto issue_host_reset;
	}

	mpi_reply = ioc->ctl_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;

#ifdef CONFIG_SCSI_MPT3SAS_LOGGING
	if (mpi_reply->Function == MPI2_FUNCTION_SCSI_TASK_MGMT &&
	    (ioc->logging_level & MPT_DEBUG_TM)) {
		Mpi2SCSITaskManagementReply_t *tm_reply =
		    (Mpi2SCSITaskManagementReply_t *)mpi_reply;

		pr_info(MPT3SAS_FMT "TASK_MGMT: " \
		    "IOCStatus(0x%04x), IOCLogInfo(0x%08x), "
		    "TerminationCount(0x%08x)\n", ioc->name,
		    le16_to_cpu(tm_reply->IOCStatus),
		    le32_to_cpu(tm_reply->IOCLogInfo),
		    le32_to_cpu(tm_reply->TerminationCount));
	}
#endif
	/* copy out xdata to user */
	if (data_in_sz) {
		if (copy_to_user(karg.data_in_buf_ptr, data_in,
		    data_in_sz)) {
			pr_err("failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			ret = -ENODATA;
			goto out;
		}
	}

	/* copy out reply message frame to user */
	if (karg.max_reply_bytes) {
		sz = min_t(u32, karg.max_reply_bytes, ioc->reply_sz);
		if (copy_to_user(karg.reply_frame_buf_ptr, ioc->ctl_cmds.reply,
		    sz)) {
			pr_err("failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			ret = -ENODATA;
			goto out;
		}
	}

	/* copy out sense to user */
	if (karg.max_sense_bytes && (mpi_request->Function ==
	    MPI2_FUNCTION_SCSI_IO_REQUEST || mpi_request->Function ==
	    MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH)) {
		sz = min_t(u32, karg.max_sense_bytes, SCSI_SENSE_BUFFERSIZE);
		if (copy_to_user(karg.sense_data_ptr, ioc->ctl_cmds.sense,
		    sz)) {
			pr_err("failure at %s:%d/%s()!\n", __FILE__,
			    __LINE__, __func__);
			ret = -ENODATA;
			goto out;
		}
	}

 issue_host_reset:
	if (issue_reset) {
		ret = -ENODATA;
		if ((mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST ||
		    mpi_request->Function ==
		    MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH ||
		    mpi_request->Function == MPI2_FUNCTION_SATA_PASSTHROUGH)) {
			pr_info(MPT3SAS_FMT "issue target reset: handle = (0x%04x)\n",
				ioc->name,
				le16_to_cpu(mpi_request->FunctionDependent1));
			mpt3sas_halt_firmware(ioc);
			mpt3sas_scsih_issue_tm(ioc,
			    le16_to_cpu(mpi_request->FunctionDependent1), 0, 0,
			    0, MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, 0, 30,
			    0, TM_MUTEX_ON);
		} else
			mpt3sas_base_hard_reset_handler(ioc, CAN_SLEEP,
			    FORCE_BIG_HAMMER);
	}

 out:

	/* free memory associated with sg buffers */
	if (data_in)
		pci_free_consistent(ioc->pdev, data_in_sz, data_in,
		    data_in_dma);

	if (data_out)
		pci_free_consistent(ioc->pdev, data_out_sz, data_out,
		    data_out_dma);

	kfree(mpi_request);
	ioc->ctl_cmds.status = MPT3_CMD_NOT_USED;
	return ret;
}

/**
 * _ctl_getiocinfo - main handler for MPT3IOCINFO opcode
 * @ioc: per adapter object
 * @arg - user space buffer containing ioctl content
 */
static long
_ctl_getiocinfo(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_ioctl_iocinfo karg;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s: enter\n", ioc->name,
	    __func__));

	memset(&karg, 0 , sizeof(karg));
	karg.adapter_type = MPT3_IOCTL_INTERFACE_SAS3;
	if (ioc->pfacts)
		karg.port_number = ioc->pfacts[0].PortNumber;
	karg.hw_rev = ioc->pdev->revision;
	karg.pci_id = ioc->pdev->device;
	karg.subsystem_device = ioc->pdev->subsystem_device;
	karg.subsystem_vendor = ioc->pdev->subsystem_vendor;
	karg.pci_information.u.bits.bus = ioc->pdev->bus->number;
	karg.pci_information.u.bits.device = PCI_SLOT(ioc->pdev->devfn);
	karg.pci_information.u.bits.function = PCI_FUNC(ioc->pdev->devfn);
	karg.pci_information.segment_id = pci_domain_nr(ioc->pdev->bus);
	karg.firmware_version = ioc->facts.FWVersion.Word;
	strcpy(karg.driver_version, MPT3SAS_DRIVER_NAME);
	strcat(karg.driver_version, "-");
	strcat(karg.driver_version, MPT3SAS_DRIVER_VERSION);
	karg.bios_version = le32_to_cpu(ioc->bios_pg3.BiosVersion);

	if (copy_to_user(arg, &karg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}
	return 0;
}

/**
 * _ctl_eventquery - main handler for MPT3EVENTQUERY opcode
 * @ioc: per adapter object
 * @arg - user space buffer containing ioctl content
 */
static long
_ctl_eventquery(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_ioctl_eventquery karg;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s: enter\n", ioc->name,
	    __func__));

	karg.event_entries = MPT3SAS_CTL_EVENT_LOG_SIZE;
	memcpy(karg.event_types, ioc->event_type,
	    MPI2_EVENT_NOTIFY_EVENTMASK_WORDS * sizeof(u32));

	if (copy_to_user(arg, &karg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}
	return 0;
}

/**
 * _ctl_eventenable - main handler for MPT3EVENTENABLE opcode
 * @ioc: per adapter object
 * @arg - user space buffer containing ioctl content
 */
static long
_ctl_eventenable(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_ioctl_eventenable karg;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s: enter\n", ioc->name,
	    __func__));

	memcpy(ioc->event_type, karg.event_types,
	    MPI2_EVENT_NOTIFY_EVENTMASK_WORDS * sizeof(u32));
	mpt3sas_base_validate_event_type(ioc, ioc->event_type);

	if (ioc->event_log)
		return 0;
	/* initialize event_log */
	ioc->event_context = 0;
	ioc->aen_event_read_flag = 0;
	ioc->event_log = kcalloc(MPT3SAS_CTL_EVENT_LOG_SIZE,
	    sizeof(struct MPT3_IOCTL_EVENTS), GFP_KERNEL);
	if (!ioc->event_log) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -ENOMEM;
	}
	return 0;
}

/**
 * _ctl_eventreport - main handler for MPT3EVENTREPORT opcode
 * @ioc: per adapter object
 * @arg - user space buffer containing ioctl content
 */
static long
_ctl_eventreport(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_ioctl_eventreport karg;
	u32 number_bytes, max_events, max;
	struct mpt3_ioctl_eventreport __user *uarg = arg;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s: enter\n", ioc->name,
	    __func__));

	number_bytes = karg.hdr.max_data_size -
	    sizeof(struct mpt3_ioctl_header);
	max_events = number_bytes/sizeof(struct MPT3_IOCTL_EVENTS);
	max = min_t(u32, MPT3SAS_CTL_EVENT_LOG_SIZE, max_events);

	/* If fewer than 1 event is requested, there must have
	 * been some type of error.
	 */
	if (!max || !ioc->event_log)
		return -ENODATA;

	number_bytes = max * sizeof(struct MPT3_IOCTL_EVENTS);
	if (copy_to_user(uarg->event_data, ioc->event_log, number_bytes)) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	/* reset flag so SIGIO can restart */
	ioc->aen_event_read_flag = 0;
	return 0;
}

/**
 * _ctl_do_reset - main handler for MPT3HARDRESET opcode
 * @ioc: per adapter object
 * @arg - user space buffer containing ioctl content
 */
static long
_ctl_do_reset(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_ioctl_diag_reset karg;
	int retval;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	if (ioc->shost_recovery || ioc->pci_error_recovery ||
	    ioc->is_driver_loading)
		return -EAGAIN;

	dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s: enter\n", ioc->name,
	    __func__));

	retval = mpt3sas_base_hard_reset_handler(ioc, CAN_SLEEP,
	    FORCE_BIG_HAMMER);
	pr_info(MPT3SAS_FMT "host reset: %s\n",
	    ioc->name, ((!retval) ? "SUCCESS" : "FAILED"));
	return 0;
}

/**
 * _ctl_btdh_search_sas_device - searching for sas device
 * @ioc: per adapter object
 * @btdh: btdh ioctl payload
 */
static int
_ctl_btdh_search_sas_device(struct MPT3SAS_ADAPTER *ioc,
	struct mpt3_ioctl_btdh_mapping *btdh)
{
	struct _sas_device *sas_device;
	unsigned long flags;
	int rc = 0;

	if (list_empty(&ioc->sas_device_list))
		return rc;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_for_each_entry(sas_device, &ioc->sas_device_list, list) {
		if (btdh->bus == 0xFFFFFFFF && btdh->id == 0xFFFFFFFF &&
		    btdh->handle == sas_device->handle) {
			btdh->bus = sas_device->channel;
			btdh->id = sas_device->id;
			rc = 1;
			goto out;
		} else if (btdh->bus == sas_device->channel && btdh->id ==
		    sas_device->id && btdh->handle == 0xFFFF) {
			btdh->handle = sas_device->handle;
			rc = 1;
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	return rc;
}

/**
 * _ctl_btdh_search_raid_device - searching for raid device
 * @ioc: per adapter object
 * @btdh: btdh ioctl payload
 */
static int
_ctl_btdh_search_raid_device(struct MPT3SAS_ADAPTER *ioc,
	struct mpt3_ioctl_btdh_mapping *btdh)
{
	struct _raid_device *raid_device;
	unsigned long flags;
	int rc = 0;

	if (list_empty(&ioc->raid_device_list))
		return rc;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (btdh->bus == 0xFFFFFFFF && btdh->id == 0xFFFFFFFF &&
		    btdh->handle == raid_device->handle) {
			btdh->bus = raid_device->channel;
			btdh->id = raid_device->id;
			rc = 1;
			goto out;
		} else if (btdh->bus == raid_device->channel && btdh->id ==
		    raid_device->id && btdh->handle == 0xFFFF) {
			btdh->handle = raid_device->handle;
			rc = 1;
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	return rc;
}

/**
 * _ctl_btdh_mapping - main handler for MPT3BTDHMAPPING opcode
 * @ioc: per adapter object
 * @arg - user space buffer containing ioctl content
 */
static long
_ctl_btdh_mapping(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_ioctl_btdh_mapping karg;
	int rc;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	rc = _ctl_btdh_search_sas_device(ioc, &karg);
	if (!rc)
		_ctl_btdh_search_raid_device(ioc, &karg);

	if (copy_to_user(arg, &karg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}
	return 0;
}

/**
 * _ctl_diag_capability - return diag buffer capability
 * @ioc: per adapter object
 * @buffer_type: specifies either TRACE, SNAPSHOT, or EXTENDED
 *
 * returns 1 when diag buffer support is enabled in firmware
 */
static u8
_ctl_diag_capability(struct MPT3SAS_ADAPTER *ioc, u8 buffer_type)
{
	u8 rc = 0;

	switch (buffer_type) {
	case MPI2_DIAG_BUF_TYPE_TRACE:
		if (ioc->facts.IOCCapabilities &
		    MPI2_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER)
			rc = 1;
		break;
	case MPI2_DIAG_BUF_TYPE_SNAPSHOT:
		if (ioc->facts.IOCCapabilities &
		    MPI2_IOCFACTS_CAPABILITY_SNAPSHOT_BUFFER)
			rc = 1;
		break;
	case MPI2_DIAG_BUF_TYPE_EXTENDED:
		if (ioc->facts.IOCCapabilities &
		    MPI2_IOCFACTS_CAPABILITY_EXTENDED_BUFFER)
			rc = 1;
	}

	return rc;
}


/**
 * _ctl_diag_register_2 - wrapper for registering diag buffer support
 * @ioc: per adapter object
 * @diag_register: the diag_register struct passed in from user space
 *
 */
static long
_ctl_diag_register_2(struct MPT3SAS_ADAPTER *ioc,
	struct mpt3_diag_register *diag_register)
{
	int rc, i;
	void *request_data = NULL;
	dma_addr_t request_data_dma;
	u32 request_data_sz = 0;
	Mpi2DiagBufferPostRequest_t *mpi_request;
	Mpi2DiagBufferPostReply_t *mpi_reply;
	u8 buffer_type;
	unsigned long timeleft;
	u16 smid;
	u16 ioc_status;
	u32 ioc_state;
	u8 issue_reset = 0;

	dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		pr_err(MPT3SAS_FMT
		    "%s: failed due to ioc not operational\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	if (ioc->ctl_cmds.status != MPT3_CMD_NOT_USED) {
		pr_err(MPT3SAS_FMT "%s: ctl_cmd in use\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	buffer_type = diag_register->buffer_type;
	if (!_ctl_diag_capability(ioc, buffer_type)) {
		pr_err(MPT3SAS_FMT
			"%s: doesn't have capability for buffer_type(0x%02x)\n",
			ioc->name, __func__, buffer_type);
		return -EPERM;
	}

	if (ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) {
		pr_err(MPT3SAS_FMT
			"%s: already has a registered buffer for buffer_type(0x%02x)\n",
			ioc->name, __func__,
		    buffer_type);
		return -EINVAL;
	}

	if (diag_register->requested_buffer_size % 4)  {
		pr_err(MPT3SAS_FMT
			"%s: the requested_buffer_size is not 4 byte aligned\n",
			ioc->name, __func__);
		return -EINVAL;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->ctl_cb_idx);
	if (!smid) {
		pr_err(MPT3SAS_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->ctl_cmds.status = MPT3_CMD_PENDING;
	memset(ioc->ctl_cmds.reply, 0, ioc->reply_sz);
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->ctl_cmds.smid = smid;

	request_data = ioc->diag_buffer[buffer_type];
	request_data_sz = diag_register->requested_buffer_size;
	ioc->unique_id[buffer_type] = diag_register->unique_id;
	ioc->diag_buffer_status[buffer_type] = 0;
	memcpy(ioc->product_specific[buffer_type],
	    diag_register->product_specific, MPT3_PRODUCT_SPECIFIC_DWORDS);
	ioc->diagnostic_flags[buffer_type] = diag_register->diagnostic_flags;

	if (request_data) {
		request_data_dma = ioc->diag_buffer_dma[buffer_type];
		if (request_data_sz != ioc->diag_buffer_sz[buffer_type]) {
			pci_free_consistent(ioc->pdev,
			    ioc->diag_buffer_sz[buffer_type],
			    request_data, request_data_dma);
			request_data = NULL;
		}
	}

	if (request_data == NULL) {
		ioc->diag_buffer_sz[buffer_type] = 0;
		ioc->diag_buffer_dma[buffer_type] = 0;
		request_data = pci_alloc_consistent(
			ioc->pdev, request_data_sz, &request_data_dma);
		if (request_data == NULL) {
			pr_err(MPT3SAS_FMT "%s: failed allocating memory" \
			    " for diag buffers, requested size(%d)\n",
			    ioc->name, __func__, request_data_sz);
			mpt3sas_base_free_smid(ioc, smid);
			return -ENOMEM;
		}
		ioc->diag_buffer[buffer_type] = request_data;
		ioc->diag_buffer_sz[buffer_type] = request_data_sz;
		ioc->diag_buffer_dma[buffer_type] = request_data_dma;
	}

	mpi_request->Function = MPI2_FUNCTION_DIAG_BUFFER_POST;
	mpi_request->BufferType = diag_register->buffer_type;
	mpi_request->Flags = cpu_to_le32(diag_register->diagnostic_flags);
	mpi_request->BufferAddress = cpu_to_le64(request_data_dma);
	mpi_request->BufferLength = cpu_to_le32(request_data_sz);
	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;

	dctlprintk(ioc, pr_info(MPT3SAS_FMT
		"%s: diag_buffer(0x%p), dma(0x%llx), sz(%d)\n",
		ioc->name, __func__, request_data,
	    (unsigned long long)request_data_dma,
	    le32_to_cpu(mpi_request->BufferLength)));

	for (i = 0; i < MPT3_PRODUCT_SPECIFIC_DWORDS; i++)
		mpi_request->ProductSpecific[i] =
			cpu_to_le32(ioc->product_specific[buffer_type][i]);

	init_completion(&ioc->ctl_cmds.done);
	mpt3sas_base_put_smid_default(ioc, smid);
	timeleft = wait_for_completion_timeout(&ioc->ctl_cmds.done,
	    MPT3_IOCTL_DEFAULT_TIMEOUT*HZ);

	if (!(ioc->ctl_cmds.status & MPT3_CMD_COMPLETE)) {
		pr_err(MPT3SAS_FMT "%s: timeout\n", ioc->name,
		    __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2DiagBufferPostRequest_t)/4);
		if (!(ioc->ctl_cmds.status & MPT3_CMD_RESET))
			issue_reset = 1;
		goto issue_host_reset;
	}

	/* process the completed Reply Message Frame */
	if ((ioc->ctl_cmds.status & MPT3_CMD_REPLY_VALID) == 0) {
		pr_err(MPT3SAS_FMT "%s: no reply message\n",
		    ioc->name, __func__);
		rc = -EFAULT;
		goto out;
	}

	mpi_reply = ioc->ctl_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;

	if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
		ioc->diag_buffer_status[buffer_type] |=
			MPT3_DIAG_BUFFER_IS_REGISTERED;
		dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s: success\n",
		    ioc->name, __func__));
	} else {
		pr_info(MPT3SAS_FMT
			"%s: ioc_status(0x%04x) log_info(0x%08x)\n",
			ioc->name, __func__,
		    ioc_status, le32_to_cpu(mpi_reply->IOCLogInfo));
		rc = -EFAULT;
	}

 issue_host_reset:
	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, CAN_SLEEP,
		    FORCE_BIG_HAMMER);

 out:

	if (rc && request_data)
		pci_free_consistent(ioc->pdev, request_data_sz,
		    request_data, request_data_dma);

	ioc->ctl_cmds.status = MPT3_CMD_NOT_USED;
	return rc;
}

/**
 * mpt3sas_enable_diag_buffer - enabling diag_buffers support driver load time
 * @ioc: per adapter object
 * @bits_to_register: bitwise field where trace is bit 0, and snapshot is bit 1
 *
 * This is called when command line option diag_buffer_enable is enabled
 * at driver load time.
 */
void
mpt3sas_enable_diag_buffer(struct MPT3SAS_ADAPTER *ioc, u8 bits_to_register)
{
	struct mpt3_diag_register diag_register;

	memset(&diag_register, 0, sizeof(struct mpt3_diag_register));

	if (bits_to_register & 1) {
		pr_info(MPT3SAS_FMT "registering trace buffer support\n",
		    ioc->name);
		ioc->diag_trigger_master.MasterData =
		    (MASTER_TRIGGER_FW_FAULT + MASTER_TRIGGER_ADAPTER_RESET);
		diag_register.buffer_type = MPI2_DIAG_BUF_TYPE_TRACE;
		/* register for 2MB buffers  */
		diag_register.requested_buffer_size = 2 * (1024 * 1024);
		diag_register.unique_id = 0x7075900;
		_ctl_diag_register_2(ioc,  &diag_register);
	}

	if (bits_to_register & 2) {
		pr_info(MPT3SAS_FMT "registering snapshot buffer support\n",
		    ioc->name);
		diag_register.buffer_type = MPI2_DIAG_BUF_TYPE_SNAPSHOT;
		/* register for 2MB buffers  */
		diag_register.requested_buffer_size = 2 * (1024 * 1024);
		diag_register.unique_id = 0x7075901;
		_ctl_diag_register_2(ioc,  &diag_register);
	}

	if (bits_to_register & 4) {
		pr_info(MPT3SAS_FMT "registering extended buffer support\n",
		    ioc->name);
		diag_register.buffer_type = MPI2_DIAG_BUF_TYPE_EXTENDED;
		/* register for 2MB buffers  */
		diag_register.requested_buffer_size = 2 * (1024 * 1024);
		diag_register.unique_id = 0x7075901;
		_ctl_diag_register_2(ioc,  &diag_register);
	}
}

/**
 * _ctl_diag_register - application register with driver
 * @ioc: per adapter object
 * @arg - user space buffer containing ioctl content
 *
 * This will allow the driver to setup any required buffers that will be
 * needed by firmware to communicate with the driver.
 */
static long
_ctl_diag_register(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_diag_register karg;
	long rc;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	rc = _ctl_diag_register_2(ioc, &karg);
	return rc;
}

/**
 * _ctl_diag_unregister - application unregister with driver
 * @ioc: per adapter object
 * @arg - user space buffer containing ioctl content
 *
 * This will allow the driver to cleanup any memory allocated for diag
 * messages and to free up any resources.
 */
static long
_ctl_diag_unregister(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_diag_unregister karg;
	void *request_data;
	dma_addr_t request_data_dma;
	u32 request_data_sz;
	u8 buffer_type;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	buffer_type = karg.unique_id & 0x000000ff;
	if (!_ctl_diag_capability(ioc, buffer_type)) {
		pr_err(MPT3SAS_FMT
			"%s: doesn't have capability for buffer_type(0x%02x)\n",
			ioc->name, __func__, buffer_type);
		return -EPERM;
	}

	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		pr_err(MPT3SAS_FMT
			"%s: buffer_type(0x%02x) is not registered\n",
			ioc->name, __func__, buffer_type);
		return -EINVAL;
	}
	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_RELEASED) == 0) {
		pr_err(MPT3SAS_FMT
			"%s: buffer_type(0x%02x) has not been released\n",
			ioc->name, __func__, buffer_type);
		return -EINVAL;
	}

	if (karg.unique_id != ioc->unique_id[buffer_type]) {
		pr_err(MPT3SAS_FMT
			"%s: unique_id(0x%08x) is not registered\n",
			ioc->name, __func__, karg.unique_id);
		return -EINVAL;
	}

	request_data = ioc->diag_buffer[buffer_type];
	if (!request_data) {
		pr_err(MPT3SAS_FMT
			"%s: doesn't have memory allocated for buffer_type(0x%02x)\n",
			ioc->name, __func__, buffer_type);
		return -ENOMEM;
	}

	request_data_sz = ioc->diag_buffer_sz[buffer_type];
	request_data_dma = ioc->diag_buffer_dma[buffer_type];
	pci_free_consistent(ioc->pdev, request_data_sz,
	    request_data, request_data_dma);
	ioc->diag_buffer[buffer_type] = NULL;
	ioc->diag_buffer_status[buffer_type] = 0;
	return 0;
}

/**
 * _ctl_diag_query - query relevant info associated with diag buffers
 * @ioc: per adapter object
 * @arg - user space buffer containing ioctl content
 *
 * The application will send only buffer_type and unique_id.  Driver will
 * inspect unique_id first, if valid, fill in all the info.  If unique_id is
 * 0x00, the driver will return info specified by Buffer Type.
 */
static long
_ctl_diag_query(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_diag_query karg;
	void *request_data;
	int i;
	u8 buffer_type;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	karg.application_flags = 0;
	buffer_type = karg.buffer_type;

	if (!_ctl_diag_capability(ioc, buffer_type)) {
		pr_err(MPT3SAS_FMT
			"%s: doesn't have capability for buffer_type(0x%02x)\n",
			ioc->name, __func__, buffer_type);
		return -EPERM;
	}

	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		pr_err(MPT3SAS_FMT
			"%s: buffer_type(0x%02x) is not registered\n",
			ioc->name, __func__, buffer_type);
		return -EINVAL;
	}

	if (karg.unique_id & 0xffffff00) {
		if (karg.unique_id != ioc->unique_id[buffer_type]) {
			pr_err(MPT3SAS_FMT
				"%s: unique_id(0x%08x) is not registered\n",
				ioc->name, __func__, karg.unique_id);
			return -EINVAL;
		}
	}

	request_data = ioc->diag_buffer[buffer_type];
	if (!request_data) {
		pr_err(MPT3SAS_FMT
			"%s: doesn't have buffer for buffer_type(0x%02x)\n",
			ioc->name, __func__, buffer_type);
		return -ENOMEM;
	}

	if (ioc->diag_buffer_status[buffer_type] & MPT3_DIAG_BUFFER_IS_RELEASED)
		karg.application_flags = (MPT3_APP_FLAGS_APP_OWNED |
		    MPT3_APP_FLAGS_BUFFER_VALID);
	else
		karg.application_flags = (MPT3_APP_FLAGS_APP_OWNED |
		    MPT3_APP_FLAGS_BUFFER_VALID |
		    MPT3_APP_FLAGS_FW_BUFFER_ACCESS);

	for (i = 0; i < MPT3_PRODUCT_SPECIFIC_DWORDS; i++)
		karg.product_specific[i] =
		    ioc->product_specific[buffer_type][i];

	karg.total_buffer_size = ioc->diag_buffer_sz[buffer_type];
	karg.driver_added_buffer_size = 0;
	karg.unique_id = ioc->unique_id[buffer_type];
	karg.diagnostic_flags = ioc->diagnostic_flags[buffer_type];

	if (copy_to_user(arg, &karg, sizeof(struct mpt3_diag_query))) {
		pr_err(MPT3SAS_FMT
			"%s: unable to write mpt3_diag_query data @ %p\n",
			ioc->name, __func__, arg);
		return -EFAULT;
	}
	return 0;
}

/**
 * mpt3sas_send_diag_release - Diag Release Message
 * @ioc: per adapter object
 * @buffer_type - specifies either TRACE, SNAPSHOT, or EXTENDED
 * @issue_reset - specifies whether host reset is required.
 *
 */
int
mpt3sas_send_diag_release(struct MPT3SAS_ADAPTER *ioc, u8 buffer_type,
	u8 *issue_reset)
{
	Mpi2DiagReleaseRequest_t *mpi_request;
	Mpi2DiagReleaseReply_t *mpi_reply;
	u16 smid;
	u16 ioc_status;
	u32 ioc_state;
	int rc;
	unsigned long timeleft;

	dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	rc = 0;
	*issue_reset = 0;

	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		if (ioc->diag_buffer_status[buffer_type] &
		    MPT3_DIAG_BUFFER_IS_REGISTERED)
			ioc->diag_buffer_status[buffer_type] |=
			    MPT3_DIAG_BUFFER_IS_RELEASED;
		dctlprintk(ioc, pr_info(MPT3SAS_FMT
			"%s: skipping due to FAULT state\n", ioc->name,
		    __func__));
		rc = -EAGAIN;
		goto out;
	}

	if (ioc->ctl_cmds.status != MPT3_CMD_NOT_USED) {
		pr_err(MPT3SAS_FMT "%s: ctl_cmd in use\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->ctl_cb_idx);
	if (!smid) {
		pr_err(MPT3SAS_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	ioc->ctl_cmds.status = MPT3_CMD_PENDING;
	memset(ioc->ctl_cmds.reply, 0, ioc->reply_sz);
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->ctl_cmds.smid = smid;

	mpi_request->Function = MPI2_FUNCTION_DIAG_RELEASE;
	mpi_request->BufferType = buffer_type;
	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;

	init_completion(&ioc->ctl_cmds.done);
	mpt3sas_base_put_smid_default(ioc, smid);
	timeleft = wait_for_completion_timeout(&ioc->ctl_cmds.done,
	    MPT3_IOCTL_DEFAULT_TIMEOUT*HZ);

	if (!(ioc->ctl_cmds.status & MPT3_CMD_COMPLETE)) {
		pr_err(MPT3SAS_FMT "%s: timeout\n", ioc->name,
		    __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2DiagReleaseRequest_t)/4);
		if (!(ioc->ctl_cmds.status & MPT3_CMD_RESET))
			*issue_reset = 1;
		rc = -EFAULT;
		goto out;
	}

	/* process the completed Reply Message Frame */
	if ((ioc->ctl_cmds.status & MPT3_CMD_REPLY_VALID) == 0) {
		pr_err(MPT3SAS_FMT "%s: no reply message\n",
		    ioc->name, __func__);
		rc = -EFAULT;
		goto out;
	}

	mpi_reply = ioc->ctl_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;

	if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
		ioc->diag_buffer_status[buffer_type] |=
		    MPT3_DIAG_BUFFER_IS_RELEASED;
		dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s: success\n",
		    ioc->name, __func__));
	} else {
		pr_info(MPT3SAS_FMT
			"%s: ioc_status(0x%04x) log_info(0x%08x)\n",
			ioc->name, __func__,
		    ioc_status, le32_to_cpu(mpi_reply->IOCLogInfo));
		rc = -EFAULT;
	}

 out:
	ioc->ctl_cmds.status = MPT3_CMD_NOT_USED;
	return rc;
}

/**
 * _ctl_diag_release - request to send Diag Release Message to firmware
 * @arg - user space buffer containing ioctl content
 *
 * This allows ownership of the specified buffer to returned to the driver,
 * allowing an application to read the buffer without fear that firmware is
 * overwritting information in the buffer.
 */
static long
_ctl_diag_release(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_diag_release karg;
	void *request_data;
	int rc;
	u8 buffer_type;
	u8 issue_reset = 0;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	buffer_type = karg.unique_id & 0x000000ff;
	if (!_ctl_diag_capability(ioc, buffer_type)) {
		pr_err(MPT3SAS_FMT
			"%s: doesn't have capability for buffer_type(0x%02x)\n",
			ioc->name, __func__, buffer_type);
		return -EPERM;
	}

	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		pr_err(MPT3SAS_FMT
			"%s: buffer_type(0x%02x) is not registered\n",
			ioc->name, __func__, buffer_type);
		return -EINVAL;
	}

	if (karg.unique_id != ioc->unique_id[buffer_type]) {
		pr_err(MPT3SAS_FMT
			"%s: unique_id(0x%08x) is not registered\n",
			ioc->name, __func__, karg.unique_id);
		return -EINVAL;
	}

	if (ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_RELEASED) {
		pr_err(MPT3SAS_FMT
			"%s: buffer_type(0x%02x) is already released\n",
			ioc->name, __func__,
		    buffer_type);
		return 0;
	}

	request_data = ioc->diag_buffer[buffer_type];

	if (!request_data) {
		pr_err(MPT3SAS_FMT
			"%s: doesn't have memory allocated for buffer_type(0x%02x)\n",
			ioc->name, __func__, buffer_type);
		return -ENOMEM;
	}

	/* buffers were released by due to host reset */
	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_DIAG_RESET)) {
		ioc->diag_buffer_status[buffer_type] |=
		    MPT3_DIAG_BUFFER_IS_RELEASED;
		ioc->diag_buffer_status[buffer_type] &=
		    ~MPT3_DIAG_BUFFER_IS_DIAG_RESET;
		pr_err(MPT3SAS_FMT
			"%s: buffer_type(0x%02x) was released due to host reset\n",
			ioc->name, __func__, buffer_type);
		return 0;
	}

	rc = mpt3sas_send_diag_release(ioc, buffer_type, &issue_reset);

	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, CAN_SLEEP,
		    FORCE_BIG_HAMMER);

	return rc;
}

/**
 * _ctl_diag_read_buffer - request for copy of the diag buffer
 * @ioc: per adapter object
 * @arg - user space buffer containing ioctl content
 */
static long
_ctl_diag_read_buffer(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_diag_read_buffer karg;
	struct mpt3_diag_read_buffer __user *uarg = arg;
	void *request_data, *diag_data;
	Mpi2DiagBufferPostRequest_t *mpi_request;
	Mpi2DiagBufferPostReply_t *mpi_reply;
	int rc, i;
	u8 buffer_type;
	unsigned long timeleft, request_size, copy_size;
	u16 smid;
	u16 ioc_status;
	u8 issue_reset = 0;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	buffer_type = karg.unique_id & 0x000000ff;
	if (!_ctl_diag_capability(ioc, buffer_type)) {
		pr_err(MPT3SAS_FMT
			"%s: doesn't have capability for buffer_type(0x%02x)\n",
			ioc->name, __func__, buffer_type);
		return -EPERM;
	}

	if (karg.unique_id != ioc->unique_id[buffer_type]) {
		pr_err(MPT3SAS_FMT
			"%s: unique_id(0x%08x) is not registered\n",
			ioc->name, __func__, karg.unique_id);
		return -EINVAL;
	}

	request_data = ioc->diag_buffer[buffer_type];
	if (!request_data) {
		pr_err(MPT3SAS_FMT
			"%s: doesn't have buffer for buffer_type(0x%02x)\n",
			ioc->name, __func__, buffer_type);
		return -ENOMEM;
	}

	request_size = ioc->diag_buffer_sz[buffer_type];

	if ((karg.starting_offset % 4) || (karg.bytes_to_read % 4)) {
		pr_err(MPT3SAS_FMT "%s: either the starting_offset " \
		    "or bytes_to_read are not 4 byte aligned\n", ioc->name,
		    __func__);
		return -EINVAL;
	}

	if (karg.starting_offset > request_size)
		return -EINVAL;

	diag_data = (void *)(request_data + karg.starting_offset);
	dctlprintk(ioc, pr_info(MPT3SAS_FMT
		"%s: diag_buffer(%p), offset(%d), sz(%d)\n",
		ioc->name, __func__,
	    diag_data, karg.starting_offset, karg.bytes_to_read));

	/* Truncate data on requests that are too large */
	if ((diag_data + karg.bytes_to_read < diag_data) ||
	    (diag_data + karg.bytes_to_read > request_data + request_size))
		copy_size = request_size - karg.starting_offset;
	else
		copy_size = karg.bytes_to_read;

	if (copy_to_user((void __user *)uarg->diagnostic_data,
	    diag_data, copy_size)) {
		pr_err(MPT3SAS_FMT
			"%s: Unable to write mpt_diag_read_buffer_t data @ %p\n",
			ioc->name, __func__, diag_data);
		return -EFAULT;
	}

	if ((karg.flags & MPT3_FLAGS_REREGISTER) == 0)
		return 0;

	dctlprintk(ioc, pr_info(MPT3SAS_FMT
		"%s: Reregister buffer_type(0x%02x)\n",
		ioc->name, __func__, buffer_type));
	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_RELEASED) == 0) {
		dctlprintk(ioc, pr_info(MPT3SAS_FMT
			"%s: buffer_type(0x%02x) is still registered\n",
			ioc->name, __func__, buffer_type));
		return 0;
	}
	/* Get a free request frame and save the message context.
	*/

	if (ioc->ctl_cmds.status != MPT3_CMD_NOT_USED) {
		pr_err(MPT3SAS_FMT "%s: ctl_cmd in use\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->ctl_cb_idx);
	if (!smid) {
		pr_err(MPT3SAS_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->ctl_cmds.status = MPT3_CMD_PENDING;
	memset(ioc->ctl_cmds.reply, 0, ioc->reply_sz);
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->ctl_cmds.smid = smid;

	mpi_request->Function = MPI2_FUNCTION_DIAG_BUFFER_POST;
	mpi_request->BufferType = buffer_type;
	mpi_request->BufferLength =
	    cpu_to_le32(ioc->diag_buffer_sz[buffer_type]);
	mpi_request->BufferAddress =
	    cpu_to_le64(ioc->diag_buffer_dma[buffer_type]);
	for (i = 0; i < MPT3_PRODUCT_SPECIFIC_DWORDS; i++)
		mpi_request->ProductSpecific[i] =
			cpu_to_le32(ioc->product_specific[buffer_type][i]);
	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;

	init_completion(&ioc->ctl_cmds.done);
	mpt3sas_base_put_smid_default(ioc, smid);
	timeleft = wait_for_completion_timeout(&ioc->ctl_cmds.done,
	    MPT3_IOCTL_DEFAULT_TIMEOUT*HZ);

	if (!(ioc->ctl_cmds.status & MPT3_CMD_COMPLETE)) {
		pr_err(MPT3SAS_FMT "%s: timeout\n", ioc->name,
		    __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2DiagBufferPostRequest_t)/4);
		if (!(ioc->ctl_cmds.status & MPT3_CMD_RESET))
			issue_reset = 1;
		goto issue_host_reset;
	}

	/* process the completed Reply Message Frame */
	if ((ioc->ctl_cmds.status & MPT3_CMD_REPLY_VALID) == 0) {
		pr_err(MPT3SAS_FMT "%s: no reply message\n",
		    ioc->name, __func__);
		rc = -EFAULT;
		goto out;
	}

	mpi_reply = ioc->ctl_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;

	if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
		ioc->diag_buffer_status[buffer_type] |=
		    MPT3_DIAG_BUFFER_IS_REGISTERED;
		dctlprintk(ioc, pr_info(MPT3SAS_FMT "%s: success\n",
		    ioc->name, __func__));
	} else {
		pr_info(MPT3SAS_FMT
			"%s: ioc_status(0x%04x) log_info(0x%08x)\n",
			ioc->name, __func__,
		    ioc_status, le32_to_cpu(mpi_reply->IOCLogInfo));
		rc = -EFAULT;
	}

 issue_host_reset:
	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, CAN_SLEEP,
		    FORCE_BIG_HAMMER);

 out:

	ioc->ctl_cmds.status = MPT3_CMD_NOT_USED;
	return rc;
}



#ifdef CONFIG_COMPAT
/**
 * _ctl_compat_mpt_command - convert 32bit pointers to 64bit.
 * @ioc: per adapter object
 * @cmd - ioctl opcode
 * @arg - (struct mpt3_ioctl_command32)
 *
 * MPT3COMMAND32 - Handle 32bit applications running on 64bit os.
 */
static long
_ctl_compat_mpt_command(struct MPT3SAS_ADAPTER *ioc, unsigned cmd,
	void __user *arg)
{
	struct mpt3_ioctl_command32 karg32;
	struct mpt3_ioctl_command32 __user *uarg;
	struct mpt3_ioctl_command karg;

	if (_IOC_SIZE(cmd) != sizeof(struct mpt3_ioctl_command32))
		return -EINVAL;

	uarg = (struct mpt3_ioctl_command32 __user *) arg;

	if (copy_from_user(&karg32, (char __user *)arg, sizeof(karg32))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	memset(&karg, 0, sizeof(struct mpt3_ioctl_command));
	karg.hdr.ioc_number = karg32.hdr.ioc_number;
	karg.hdr.port_number = karg32.hdr.port_number;
	karg.hdr.max_data_size = karg32.hdr.max_data_size;
	karg.timeout = karg32.timeout;
	karg.max_reply_bytes = karg32.max_reply_bytes;
	karg.data_in_size = karg32.data_in_size;
	karg.data_out_size = karg32.data_out_size;
	karg.max_sense_bytes = karg32.max_sense_bytes;
	karg.data_sge_offset = karg32.data_sge_offset;
	karg.reply_frame_buf_ptr = compat_ptr(karg32.reply_frame_buf_ptr);
	karg.data_in_buf_ptr = compat_ptr(karg32.data_in_buf_ptr);
	karg.data_out_buf_ptr = compat_ptr(karg32.data_out_buf_ptr);
	karg.sense_data_ptr = compat_ptr(karg32.sense_data_ptr);
	return _ctl_do_mpt_command(ioc, karg, &uarg->mf);
}
#endif

/**
 * _ctl_ioctl_main - main ioctl entry point
 * @file - (struct file)
 * @cmd - ioctl opcode
 * @arg -
 * compat - handles 32 bit applications in 64bit os
 */
static long
_ctl_ioctl_main(struct file *file, unsigned int cmd, void __user *arg,
	u8 compat)
{
	struct MPT3SAS_ADAPTER *ioc;
	struct mpt3_ioctl_header ioctl_header;
	enum block_state state;
	long ret = -EINVAL;

	/* get IOCTL header */
	if (copy_from_user(&ioctl_header, (char __user *)arg,
	    sizeof(struct mpt3_ioctl_header))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	if (_ctl_verify_adapter(ioctl_header.ioc_number, &ioc) == -1 || !ioc)
		return -ENODEV;

	if (ioc->shost_recovery || ioc->pci_error_recovery ||
	    ioc->is_driver_loading)
		return -EAGAIN;

	state = (file->f_flags & O_NONBLOCK) ? NON_BLOCKING : BLOCKING;
	if (state == NON_BLOCKING) {
		if (!mutex_trylock(&ioc->ctl_cmds.mutex))
			return -EAGAIN;
	} else if (mutex_lock_interruptible(&ioc->ctl_cmds.mutex))
		return -ERESTARTSYS;


	switch (cmd) {
	case MPT3IOCINFO:
		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_ioctl_iocinfo))
			ret = _ctl_getiocinfo(ioc, arg);
		break;
#ifdef CONFIG_COMPAT
	case MPT3COMMAND32:
#endif
	case MPT3COMMAND:
	{
		struct mpt3_ioctl_command __user *uarg;
		struct mpt3_ioctl_command karg;

#ifdef CONFIG_COMPAT
		if (compat) {
			ret = _ctl_compat_mpt_command(ioc, cmd, arg);
			break;
		}
#endif
		if (copy_from_user(&karg, arg, sizeof(karg))) {
			pr_err("failure at %s:%d/%s()!\n",
			    __FILE__, __LINE__, __func__);
			ret = -EFAULT;
			break;
		}

		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_ioctl_command)) {
			uarg = arg;
			ret = _ctl_do_mpt_command(ioc, karg, &uarg->mf);
		}
		break;
	}
	case MPT3EVENTQUERY:
		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_ioctl_eventquery))
			ret = _ctl_eventquery(ioc, arg);
		break;
	case MPT3EVENTENABLE:
		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_ioctl_eventenable))
			ret = _ctl_eventenable(ioc, arg);
		break;
	case MPT3EVENTREPORT:
		ret = _ctl_eventreport(ioc, arg);
		break;
	case MPT3HARDRESET:
		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_ioctl_diag_reset))
			ret = _ctl_do_reset(ioc, arg);
		break;
	case MPT3BTDHMAPPING:
		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_ioctl_btdh_mapping))
			ret = _ctl_btdh_mapping(ioc, arg);
		break;
	case MPT3DIAGREGISTER:
		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_diag_register))
			ret = _ctl_diag_register(ioc, arg);
		break;
	case MPT3DIAGUNREGISTER:
		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_diag_unregister))
			ret = _ctl_diag_unregister(ioc, arg);
		break;
	case MPT3DIAGQUERY:
		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_diag_query))
			ret = _ctl_diag_query(ioc, arg);
		break;
	case MPT3DIAGRELEASE:
		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_diag_release))
			ret = _ctl_diag_release(ioc, arg);
		break;
	case MPT3DIAGREADBUFFER:
		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_diag_read_buffer))
			ret = _ctl_diag_read_buffer(ioc, arg);
		break;
	default:
		dctlprintk(ioc, pr_info(MPT3SAS_FMT
		    "unsupported ioctl opcode(0x%08x)\n", ioc->name, cmd));
		break;
	}

	mutex_unlock(&ioc->ctl_cmds.mutex);
	return ret;
}

/**
 * _ctl_ioctl - main ioctl entry point (unlocked)
 * @file - (struct file)
 * @cmd - ioctl opcode
 * @arg -
 */
static long
_ctl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret;

	ret = _ctl_ioctl_main(file, cmd, (void __user *)arg, 0);
	return ret;
}

#ifdef CONFIG_COMPAT
/**
 * _ctl_ioctl_compat - main ioctl entry point (compat)
 * @file -
 * @cmd -
 * @arg -
 *
 * This routine handles 32 bit applications in 64bit os.
 */
static long
_ctl_ioctl_compat(struct file *file, unsigned cmd, unsigned long arg)
{
	long ret;

	ret = _ctl_ioctl_main(file, cmd, (void __user *)arg, 1);
	return ret;
}
#endif

/* scsi host attributes */
/**
 * _ctl_version_fw_show - firmware version
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_version_fw_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%02d.%02d.%02d.%02d\n",
	    (ioc->facts.FWVersion.Word & 0xFF000000) >> 24,
	    (ioc->facts.FWVersion.Word & 0x00FF0000) >> 16,
	    (ioc->facts.FWVersion.Word & 0x0000FF00) >> 8,
	    ioc->facts.FWVersion.Word & 0x000000FF);
}
static DEVICE_ATTR(version_fw, S_IRUGO, _ctl_version_fw_show, NULL);

/**
 * _ctl_version_bios_show - bios version
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_version_bios_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	u32 version = le32_to_cpu(ioc->bios_pg3.BiosVersion);

	return snprintf(buf, PAGE_SIZE, "%02d.%02d.%02d.%02d\n",
	    (version & 0xFF000000) >> 24,
	    (version & 0x00FF0000) >> 16,
	    (version & 0x0000FF00) >> 8,
	    version & 0x000000FF);
}
static DEVICE_ATTR(version_bios, S_IRUGO, _ctl_version_bios_show, NULL);

/**
 * _ctl_version_mpi_show - MPI (message passing interface) version
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_version_mpi_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%03x.%02x\n",
	    ioc->facts.MsgVersion, ioc->facts.HeaderVersion >> 8);
}
static DEVICE_ATTR(version_mpi, S_IRUGO, _ctl_version_mpi_show, NULL);

/**
 * _ctl_version_product_show - product name
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_version_product_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, 16, "%s\n", ioc->manu_pg0.ChipName);
}
static DEVICE_ATTR(version_product, S_IRUGO, _ctl_version_product_show, NULL);

/**
 * _ctl_version_nvdata_persistent_show - ndvata persistent version
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_version_nvdata_persistent_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%08xh\n",
	    le32_to_cpu(ioc->iounit_pg0.NvdataVersionPersistent.Word));
}
static DEVICE_ATTR(version_nvdata_persistent, S_IRUGO,
	_ctl_version_nvdata_persistent_show, NULL);

/**
 * _ctl_version_nvdata_default_show - nvdata default version
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_version_nvdata_default_show(struct device *cdev, struct device_attribute
	*attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%08xh\n",
	    le32_to_cpu(ioc->iounit_pg0.NvdataVersionDefault.Word));
}
static DEVICE_ATTR(version_nvdata_default, S_IRUGO,
	_ctl_version_nvdata_default_show, NULL);

/**
 * _ctl_board_name_show - board name
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_board_name_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, 16, "%s\n", ioc->manu_pg0.BoardName);
}
static DEVICE_ATTR(board_name, S_IRUGO, _ctl_board_name_show, NULL);

/**
 * _ctl_board_assembly_show - board assembly name
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_board_assembly_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, 16, "%s\n", ioc->manu_pg0.BoardAssembly);
}
static DEVICE_ATTR(board_assembly, S_IRUGO, _ctl_board_assembly_show, NULL);

/**
 * _ctl_board_tracer_show - board tracer number
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_board_tracer_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, 16, "%s\n", ioc->manu_pg0.BoardTracerNumber);
}
static DEVICE_ATTR(board_tracer, S_IRUGO, _ctl_board_tracer_show, NULL);

/**
 * _ctl_io_delay_show - io missing delay
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * This is for firmware implemention for deboucing device
 * removal events.
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_io_delay_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%02d\n", ioc->io_missing_delay);
}
static DEVICE_ATTR(io_delay, S_IRUGO, _ctl_io_delay_show, NULL);

/**
 * _ctl_device_delay_show - device missing delay
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * This is for firmware implemention for deboucing device
 * removal events.
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_device_delay_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%02d\n", ioc->device_missing_delay);
}
static DEVICE_ATTR(device_delay, S_IRUGO, _ctl_device_delay_show, NULL);

/**
 * _ctl_fw_queue_depth_show - global credits
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * This is firmware queue depth limit
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_fw_queue_depth_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%02d\n", ioc->facts.RequestCredit);
}
static DEVICE_ATTR(fw_queue_depth, S_IRUGO, _ctl_fw_queue_depth_show, NULL);

/**
 * _ctl_sas_address_show - sas address
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * This is the controller sas address
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_host_sas_address_show(struct device *cdev, struct device_attribute *attr,
	char *buf)

{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "0x%016llx\n",
	    (unsigned long long)ioc->sas_hba.sas_address);
}
static DEVICE_ATTR(host_sas_address, S_IRUGO,
	_ctl_host_sas_address_show, NULL);

/**
 * _ctl_logging_level_show - logging level
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
_ctl_logging_level_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%08xh\n", ioc->logging_level);
}
static ssize_t
_ctl_logging_level_store(struct device *cdev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	int val = 0;

	if (sscanf(buf, "%x", &val) != 1)
		return -EINVAL;

	ioc->logging_level = val;
	pr_info(MPT3SAS_FMT "logging_level=%08xh\n", ioc->name,
	    ioc->logging_level);
	return strlen(buf);
}
static DEVICE_ATTR(logging_level, S_IRUGO | S_IWUSR, _ctl_logging_level_show,
	_ctl_logging_level_store);

/**
 * _ctl_fwfault_debug_show - show/store fwfault_debug
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * mpt3sas_fwfault_debug is command line option
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
_ctl_fwfault_debug_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%d\n", ioc->fwfault_debug);
}
static ssize_t
_ctl_fwfault_debug_store(struct device *cdev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	int val = 0;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	ioc->fwfault_debug = val;
	pr_info(MPT3SAS_FMT "fwfault_debug=%d\n", ioc->name,
	    ioc->fwfault_debug);
	return strlen(buf);
}
static DEVICE_ATTR(fwfault_debug, S_IRUGO | S_IWUSR,
	_ctl_fwfault_debug_show, _ctl_fwfault_debug_store);

/**
 * _ctl_ioc_reset_count_show - ioc reset count
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * This is firmware queue depth limit
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_ioc_reset_count_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%d\n", ioc->ioc_reset_count);
}
static DEVICE_ATTR(ioc_reset_count, S_IRUGO, _ctl_ioc_reset_count_show, NULL);

/**
 * _ctl_ioc_reply_queue_count_show - number of reply queues
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * This is number of reply queues
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_ioc_reply_queue_count_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	u8 reply_queue_count;
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	if ((ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_MSI_X_INDEX) && ioc->msix_enable)
		reply_queue_count = ioc->reply_queue_count;
	else
		reply_queue_count = 1;

	return snprintf(buf, PAGE_SIZE, "%d\n", reply_queue_count);
}
static DEVICE_ATTR(reply_queue_count, S_IRUGO, _ctl_ioc_reply_queue_count_show,
	NULL);

struct DIAG_BUFFER_START {
	__le32	Size;
	__le32	DiagVersion;
	u8	BufferType;
	u8	Reserved[3];
	__le32	Reserved1;
	__le32	Reserved2;
	__le32	Reserved3;
};

/**
 * _ctl_host_trace_buffer_size_show - host buffer size (trace only)
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_host_trace_buffer_size_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	u32 size = 0;
	struct DIAG_BUFFER_START *request_data;

	if (!ioc->diag_buffer[MPI2_DIAG_BUF_TYPE_TRACE]) {
		pr_err(MPT3SAS_FMT
			"%s: host_trace_buffer is not registered\n",
			ioc->name, __func__);
		return 0;
	}

	if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		pr_err(MPT3SAS_FMT
			"%s: host_trace_buffer is not registered\n",
			ioc->name, __func__);
		return 0;
	}

	request_data = (struct DIAG_BUFFER_START *)
	    ioc->diag_buffer[MPI2_DIAG_BUF_TYPE_TRACE];
	if ((le32_to_cpu(request_data->DiagVersion) == 0x00000000 ||
	    le32_to_cpu(request_data->DiagVersion) == 0x01000000 ||
	    le32_to_cpu(request_data->DiagVersion) == 0x01010000) &&
	    le32_to_cpu(request_data->Reserved3) == 0x4742444c)
		size = le32_to_cpu(request_data->Size);

	ioc->ring_buffer_sz = size;
	return snprintf(buf, PAGE_SIZE, "%d\n", size);
}
static DEVICE_ATTR(host_trace_buffer_size, S_IRUGO,
	_ctl_host_trace_buffer_size_show, NULL);

/**
 * _ctl_host_trace_buffer_show - firmware ring buffer (trace only)
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 *
 * You will only be able to read 4k bytes of ring buffer at a time.
 * In order to read beyond 4k bytes, you will have to write out the
 * offset to the same attribute, it will move the pointer.
 */
static ssize_t
_ctl_host_trace_buffer_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	void *request_data;
	u32 size;

	if (!ioc->diag_buffer[MPI2_DIAG_BUF_TYPE_TRACE]) {
		pr_err(MPT3SAS_FMT
			"%s: host_trace_buffer is not registered\n",
			ioc->name, __func__);
		return 0;
	}

	if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		pr_err(MPT3SAS_FMT
			"%s: host_trace_buffer is not registered\n",
			ioc->name, __func__);
		return 0;
	}

	if (ioc->ring_buffer_offset > ioc->ring_buffer_sz)
		return 0;

	size = ioc->ring_buffer_sz - ioc->ring_buffer_offset;
	size = (size >= PAGE_SIZE) ? (PAGE_SIZE - 1) : size;
	request_data = ioc->diag_buffer[0] + ioc->ring_buffer_offset;
	memcpy(buf, request_data, size);
	return size;
}

static ssize_t
_ctl_host_trace_buffer_store(struct device *cdev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	int val = 0;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	ioc->ring_buffer_offset = val;
	return strlen(buf);
}
static DEVICE_ATTR(host_trace_buffer, S_IRUGO | S_IWUSR,
	_ctl_host_trace_buffer_show, _ctl_host_trace_buffer_store);


/*****************************************/

/**
 * _ctl_host_trace_buffer_enable_show - firmware ring buffer (trace only)
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 *
 * This is a mechnism to post/release host_trace_buffers
 */
static ssize_t
_ctl_host_trace_buffer_enable_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	if ((!ioc->diag_buffer[MPI2_DIAG_BUF_TYPE_TRACE]) ||
	   ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0))
		return snprintf(buf, PAGE_SIZE, "off\n");
	else if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_RELEASED))
		return snprintf(buf, PAGE_SIZE, "release\n");
	else
		return snprintf(buf, PAGE_SIZE, "post\n");
}

static ssize_t
_ctl_host_trace_buffer_enable_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	char str[10] = "";
	struct mpt3_diag_register diag_register;
	u8 issue_reset = 0;

	/* don't allow post/release occurr while recovery is active */
	if (ioc->shost_recovery || ioc->remove_host ||
	    ioc->pci_error_recovery || ioc->is_driver_loading)
		return -EBUSY;

	if (sscanf(buf, "%9s", str) != 1)
		return -EINVAL;

	if (!strcmp(str, "post")) {
		/* exit out if host buffers are already posted */
		if ((ioc->diag_buffer[MPI2_DIAG_BUF_TYPE_TRACE]) &&
		    (ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
		    MPT3_DIAG_BUFFER_IS_REGISTERED) &&
		    ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
		    MPT3_DIAG_BUFFER_IS_RELEASED) == 0))
			goto out;
		memset(&diag_register, 0, sizeof(struct mpt3_diag_register));
		pr_info(MPT3SAS_FMT "posting host trace buffers\n",
		    ioc->name);
		diag_register.buffer_type = MPI2_DIAG_BUF_TYPE_TRACE;
		diag_register.requested_buffer_size = (1024 * 1024);
		diag_register.unique_id = 0x7075900;
		ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] = 0;
		_ctl_diag_register_2(ioc,  &diag_register);
	} else if (!strcmp(str, "release")) {
		/* exit out if host buffers are already released */
		if (!ioc->diag_buffer[MPI2_DIAG_BUF_TYPE_TRACE])
			goto out;
		if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
		    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0)
			goto out;
		if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
		    MPT3_DIAG_BUFFER_IS_RELEASED))
			goto out;
		pr_info(MPT3SAS_FMT "releasing host trace buffer\n",
		    ioc->name);
		mpt3sas_send_diag_release(ioc, MPI2_DIAG_BUF_TYPE_TRACE,
		    &issue_reset);
	}

 out:
	return strlen(buf);
}
static DEVICE_ATTR(host_trace_buffer_enable, S_IRUGO | S_IWUSR,
	_ctl_host_trace_buffer_enable_show,
	_ctl_host_trace_buffer_enable_store);

/*********** diagnostic trigger suppport *********************************/

/**
 * _ctl_diag_trigger_master_show - show the diag_trigger_master attribute
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
_ctl_diag_trigger_master_show(struct device *cdev,
	struct device_attribute *attr, char *buf)

{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	unsigned long flags;
	ssize_t rc;

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);
	rc = sizeof(struct SL_WH_MASTER_TRIGGER_T);
	memcpy(buf, &ioc->diag_trigger_master, rc);
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
	return rc;
}

/**
 * _ctl_diag_trigger_master_store - store the diag_trigger_master attribute
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
_ctl_diag_trigger_master_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)

{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	unsigned long flags;
	ssize_t rc;

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);
	rc = min(sizeof(struct SL_WH_MASTER_TRIGGER_T), count);
	memset(&ioc->diag_trigger_master, 0,
	    sizeof(struct SL_WH_MASTER_TRIGGER_T));
	memcpy(&ioc->diag_trigger_master, buf, rc);
	ioc->diag_trigger_master.MasterData |=
	    (MASTER_TRIGGER_FW_FAULT + MASTER_TRIGGER_ADAPTER_RESET);
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
	return rc;
}
static DEVICE_ATTR(diag_trigger_master, S_IRUGO | S_IWUSR,
	_ctl_diag_trigger_master_show, _ctl_diag_trigger_master_store);


/**
 * _ctl_diag_trigger_event_show - show the diag_trigger_event attribute
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
_ctl_diag_trigger_event_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	unsigned long flags;
	ssize_t rc;

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);
	rc = sizeof(struct SL_WH_EVENT_TRIGGERS_T);
	memcpy(buf, &ioc->diag_trigger_event, rc);
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
	return rc;
}

/**
 * _ctl_diag_trigger_event_store - store the diag_trigger_event attribute
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
_ctl_diag_trigger_event_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)

{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	unsigned long flags;
	ssize_t sz;

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);
	sz = min(sizeof(struct SL_WH_EVENT_TRIGGERS_T), count);
	memset(&ioc->diag_trigger_event, 0,
	    sizeof(struct SL_WH_EVENT_TRIGGERS_T));
	memcpy(&ioc->diag_trigger_event, buf, sz);
	if (ioc->diag_trigger_event.ValidEntries > NUM_VALID_ENTRIES)
		ioc->diag_trigger_event.ValidEntries = NUM_VALID_ENTRIES;
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
	return sz;
}
static DEVICE_ATTR(diag_trigger_event, S_IRUGO | S_IWUSR,
	_ctl_diag_trigger_event_show, _ctl_diag_trigger_event_store);


/**
 * _ctl_diag_trigger_scsi_show - show the diag_trigger_scsi attribute
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
_ctl_diag_trigger_scsi_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	unsigned long flags;
	ssize_t rc;

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);
	rc = sizeof(struct SL_WH_SCSI_TRIGGERS_T);
	memcpy(buf, &ioc->diag_trigger_scsi, rc);
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
	return rc;
}

/**
 * _ctl_diag_trigger_scsi_store - store the diag_trigger_scsi attribute
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
_ctl_diag_trigger_scsi_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	unsigned long flags;
	ssize_t sz;

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);
	sz = min(sizeof(struct SL_WH_SCSI_TRIGGERS_T), count);
	memset(&ioc->diag_trigger_scsi, 0,
	    sizeof(struct SL_WH_EVENT_TRIGGERS_T));
	memcpy(&ioc->diag_trigger_scsi, buf, sz);
	if (ioc->diag_trigger_scsi.ValidEntries > NUM_VALID_ENTRIES)
		ioc->diag_trigger_scsi.ValidEntries = NUM_VALID_ENTRIES;
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
	return sz;
}
static DEVICE_ATTR(diag_trigger_scsi, S_IRUGO | S_IWUSR,
	_ctl_diag_trigger_scsi_show, _ctl_diag_trigger_scsi_store);


/**
 * _ctl_diag_trigger_scsi_show - show the diag_trigger_mpi attribute
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
_ctl_diag_trigger_mpi_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	unsigned long flags;
	ssize_t rc;

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);
	rc = sizeof(struct SL_WH_MPI_TRIGGERS_T);
	memcpy(buf, &ioc->diag_trigger_mpi, rc);
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
	return rc;
}

/**
 * _ctl_diag_trigger_mpi_store - store the diag_trigger_mpi attribute
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
_ctl_diag_trigger_mpi_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	unsigned long flags;
	ssize_t sz;

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);
	sz = min(sizeof(struct SL_WH_MPI_TRIGGERS_T), count);
	memset(&ioc->diag_trigger_mpi, 0,
	    sizeof(ioc->diag_trigger_mpi));
	memcpy(&ioc->diag_trigger_mpi, buf, sz);
	if (ioc->diag_trigger_mpi.ValidEntries > NUM_VALID_ENTRIES)
		ioc->diag_trigger_mpi.ValidEntries = NUM_VALID_ENTRIES;
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
	return sz;
}

static DEVICE_ATTR(diag_trigger_mpi, S_IRUGO | S_IWUSR,
	_ctl_diag_trigger_mpi_show, _ctl_diag_trigger_mpi_store);

/*********** diagnostic trigger suppport *** END ****************************/



/*****************************************/

struct device_attribute *mpt3sas_host_attrs[] = {
	&dev_attr_version_fw,
	&dev_attr_version_bios,
	&dev_attr_version_mpi,
	&dev_attr_version_product,
	&dev_attr_version_nvdata_persistent,
	&dev_attr_version_nvdata_default,
	&dev_attr_board_name,
	&dev_attr_board_assembly,
	&dev_attr_board_tracer,
	&dev_attr_io_delay,
	&dev_attr_device_delay,
	&dev_attr_logging_level,
	&dev_attr_fwfault_debug,
	&dev_attr_fw_queue_depth,
	&dev_attr_host_sas_address,
	&dev_attr_ioc_reset_count,
	&dev_attr_host_trace_buffer_size,
	&dev_attr_host_trace_buffer,
	&dev_attr_host_trace_buffer_enable,
	&dev_attr_reply_queue_count,
	&dev_attr_diag_trigger_master,
	&dev_attr_diag_trigger_event,
	&dev_attr_diag_trigger_scsi,
	&dev_attr_diag_trigger_mpi,
	NULL,
};

/* device attributes */

/**
 * _ctl_device_sas_address_show - sas address
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * This is the sas address for the target
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_device_sas_address_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT3SAS_DEVICE *sas_device_priv_data = sdev->hostdata;

	return snprintf(buf, PAGE_SIZE, "0x%016llx\n",
	    (unsigned long long)sas_device_priv_data->sas_target->sas_address);
}
static DEVICE_ATTR(sas_address, S_IRUGO, _ctl_device_sas_address_show, NULL);

/**
 * _ctl_device_handle_show - device handle
 * @cdev - pointer to embedded class device
 * @buf - the buffer returned
 *
 * This is the firmware assigned device handle
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
_ctl_device_handle_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT3SAS_DEVICE *sas_device_priv_data = sdev->hostdata;

	return snprintf(buf, PAGE_SIZE, "0x%04x\n",
	    sas_device_priv_data->sas_target->handle);
}
static DEVICE_ATTR(sas_device_handle, S_IRUGO, _ctl_device_handle_show, NULL);

struct device_attribute *mpt3sas_dev_attrs[] = {
	&dev_attr_sas_address,
	&dev_attr_sas_device_handle,
	NULL,
};

static const struct file_operations ctl_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = _ctl_ioctl,
	.poll = _ctl_poll,
	.fasync = _ctl_fasync,
#ifdef CONFIG_COMPAT
	.compat_ioctl = _ctl_ioctl_compat,
#endif
};

static struct miscdevice ctl_dev = {
	.minor  = MPT3SAS_MINOR,
	.name   = MPT3SAS_DEV_NAME,
	.fops   = &ctl_fops,
};

/**
 * mpt3sas_ctl_init - main entry point for ctl.
 *
 */
void
mpt3sas_ctl_init(void)
{
	async_queue = NULL;
	if (misc_register(&ctl_dev) < 0)
		pr_err("%s can't register misc device [minor=%d]\n",
		    MPT3SAS_DRIVER_NAME, MPT3SAS_MINOR);

	init_waitqueue_head(&ctl_poll_wait);
}

/**
 * mpt3sas_ctl_exit - exit point for ctl
 *
 */
void
mpt3sas_ctl_exit(void)
{
	struct MPT3SAS_ADAPTER *ioc;
	int i;

	list_for_each_entry(ioc, &mpt3sas_ioc_list, list) {

		/* free memory associated to diag buffers */
		for (i = 0; i < MPI2_DIAG_BUF_TYPE_COUNT; i++) {
			if (!ioc->diag_buffer[i])
				continue;
			if (!(ioc->diag_buffer_status[i] &
			    MPT3_DIAG_BUFFER_IS_REGISTERED))
				continue;
			if ((ioc->diag_buffer_status[i] &
			    MPT3_DIAG_BUFFER_IS_RELEASED))
				continue;
			pci_free_consistent(ioc->pdev, ioc->diag_buffer_sz[i],
			ioc->diag_buffer[i], ioc->diag_buffer_dma[i]);
			ioc->diag_buffer[i] = NULL;
			ioc->diag_buffer_status[i] = 0;
		}

		kfree(ioc->event_log);
	}
	misc_deregister(&ctl_dev);
}
