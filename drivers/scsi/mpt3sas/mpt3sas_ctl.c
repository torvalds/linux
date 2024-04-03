/*
 * Management Module Support for MPT (Message Passing Technology) based
 * controllers
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_ctl.c
 * Copyright (C) 2012-2014  LSI Corporation
 * Copyright (C) 2013-2014 Avago Technologies
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
	case MPI2_FUNCTION_TOOLBOX:
		desc = "toolbox";
		break;
	case MPI2_FUNCTION_NVME_ENCAPSULATED:
		desc = "nvme_encapsulated";
		break;
	}

	if (!desc)
		return;

	ioc_info(ioc, "%s: %s, smid(%d)\n", calling_function_name, desc, smid);

	if (!mpi_reply)
		return;

	if (mpi_reply->IOCStatus || mpi_reply->IOCLogInfo)
		ioc_info(ioc, "\tiocstatus(0x%04x), loginfo(0x%08x)\n",
			 le16_to_cpu(mpi_reply->IOCStatus),
			 le32_to_cpu(mpi_reply->IOCLogInfo));

	if (mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST ||
	    mpi_request->Function ==
	    MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH) {
		Mpi2SCSIIOReply_t *scsi_reply =
		    (Mpi2SCSIIOReply_t *)mpi_reply;
		struct _sas_device *sas_device = NULL;
		struct _pcie_device *pcie_device = NULL;

		sas_device = mpt3sas_get_sdev_by_handle(ioc,
		    le16_to_cpu(scsi_reply->DevHandle));
		if (sas_device) {
			ioc_warn(ioc, "\tsas_address(0x%016llx), phy(%d)\n",
				 (u64)sas_device->sas_address,
				 sas_device->phy);
			ioc_warn(ioc, "\tenclosure_logical_id(0x%016llx), slot(%d)\n",
				 (u64)sas_device->enclosure_logical_id,
				 sas_device->slot);
			sas_device_put(sas_device);
		}
		if (!sas_device) {
			pcie_device = mpt3sas_get_pdev_by_handle(ioc,
				le16_to_cpu(scsi_reply->DevHandle));
			if (pcie_device) {
				ioc_warn(ioc, "\tWWID(0x%016llx), port(%d)\n",
					 (unsigned long long)pcie_device->wwid,
					 pcie_device->port_num);
				if (pcie_device->enclosure_handle != 0)
					ioc_warn(ioc, "\tenclosure_logical_id(0x%016llx), slot(%d)\n",
						 (u64)pcie_device->enclosure_logical_id,
						 pcie_device->slot);
				pcie_device_put(pcie_device);
			}
		}
		if (scsi_reply->SCSIState || scsi_reply->SCSIStatus)
			ioc_info(ioc, "\tscsi_state(0x%02x), scsi_status(0x%02x)\n",
				 scsi_reply->SCSIState,
				 scsi_reply->SCSIStatus);
	}
}

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
 * Return: 1 meaning mf should be freed from _base_interrupt
 *         0 means the mf is freed from this function.
 */
u8
mpt3sas_ctl_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;
	Mpi2SCSIIOReply_t *scsiio_reply;
	Mpi26NVMeEncapsulatedErrorReply_t *nvme_error_reply;
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
		/*
		 * Get Error Response data for NVMe device. The ctl_cmds.sense
		 * buffer is used to store the Error Response data.
		 */
		if (mpi_reply->Function == MPI2_FUNCTION_NVME_ENCAPSULATED) {
			nvme_error_reply =
			    (Mpi26NVMeEncapsulatedErrorReply_t *)mpi_reply;
			sz = min_t(u32, NVME_ERROR_RESPONSE_SIZE,
			    le16_to_cpu(nvme_error_reply->ErrorResponseCount));
			sense_data = mpt3sas_base_get_sense_buffer(ioc, smid);
			memcpy(ioc->ctl_cmds.sense, sense_data, sz);
		}
	}

	_ctl_display_some_debug(ioc, smid, "ctl_done", mpi_reply);
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
 * Return: 1 when event should be captured, or zero means no match.
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
 * Return: 1 meaning mf should be freed from _base_interrupt
 *         0 means the mf is freed from this function.
 */
u8
mpt3sas_ctl_event_callback(struct MPT3SAS_ADAPTER *ioc, u8 msix_index,
	u32 reply)
{
	Mpi2EventNotificationReply_t *mpi_reply;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply)
		mpt3sas_ctl_add_to_event_log(ioc, mpi_reply);
	return 1;
}

/**
 * _ctl_verify_adapter - validates ioc_number passed from application
 * @ioc_number: ?
 * @iocpp: The ioc pointer is returned in this.
 * @mpi_version: will be MPI2_VERSION for mpt2ctl ioctl device &
 * MPI25_VERSION | MPI26_VERSION for mpt3ctl ioctl device.
 *
 * Return: (-1) means error, else ioc_number.
 */
static int
_ctl_verify_adapter(int ioc_number, struct MPT3SAS_ADAPTER **iocpp,
							int mpi_version)
{
	struct MPT3SAS_ADAPTER *ioc;
	int version = 0;
	/* global ioc lock to protect controller on list operations */
	spin_lock(&gioc_lock);
	list_for_each_entry(ioc, &mpt3sas_ioc_list, list) {
		if (ioc->id != ioc_number)
			continue;
		/* Check whether this ioctl command is from right
		 * ioctl device or not, if not continue the search.
		 */
		version = ioc->hba_mpi_version_belonged;
		/* MPI25_VERSION and MPI26_VERSION uses same ioctl
		 * device.
		 */
		if (mpi_version == (MPI25_VERSION | MPI26_VERSION)) {
			if ((version == MPI25_VERSION) ||
				(version == MPI26_VERSION))
				goto out;
			else
				continue;
		} else {
			if (version != mpi_version)
				continue;
		}
out:
		spin_unlock(&gioc_lock);
		*iocpp = ioc;
		return ioc_number;
	}
	spin_unlock(&gioc_lock);
	*iocpp = NULL;
	return -1;
}

/**
 * mpt3sas_ctl_pre_reset_handler - reset callback handler (for ctl)
 * @ioc: per adapter object
 *
 * The handler for doing any required cleanup or initialization.
 */
void mpt3sas_ctl_pre_reset_handler(struct MPT3SAS_ADAPTER *ioc)
{
	int i;
	u8 issue_reset;

	dtmprintk(ioc, ioc_info(ioc, "%s: MPT3_IOC_PRE_RESET\n", __func__));
	for (i = 0; i < MPI2_DIAG_BUF_TYPE_COUNT; i++) {
		if (!(ioc->diag_buffer_status[i] &
		      MPT3_DIAG_BUFFER_IS_REGISTERED))
			continue;
		if ((ioc->diag_buffer_status[i] &
		     MPT3_DIAG_BUFFER_IS_RELEASED))
			continue;

		/*
		 * add a log message to indicate the release
		 */
		ioc_info(ioc,
		    "%s: Releasing the trace buffer due to adapter reset.",
		    __func__);
		ioc->htb_rel.buffer_rel_condition =
		    MPT3_DIAG_BUFFER_REL_TRIGGER;
		mpt3sas_send_diag_release(ioc, i, &issue_reset);
	}
}

/**
 * mpt3sas_ctl_clear_outstanding_ioctls - clears outstanding ioctl cmd.
 * @ioc: per adapter object
 *
 * The handler for doing any required cleanup or initialization.
 */
void mpt3sas_ctl_clear_outstanding_ioctls(struct MPT3SAS_ADAPTER *ioc)
{
	dtmprintk(ioc,
	    ioc_info(ioc, "%s: clear outstanding ioctl cmd\n", __func__));
	if (ioc->ctl_cmds.status & MPT3_CMD_PENDING) {
		ioc->ctl_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->ctl_cmds.smid);
		complete(&ioc->ctl_cmds.done);
	}
}

/**
 * mpt3sas_ctl_reset_done_handler - reset callback handler (for ctl)
 * @ioc: per adapter object
 *
 * The handler for doing any required cleanup or initialization.
 */
void mpt3sas_ctl_reset_done_handler(struct MPT3SAS_ADAPTER *ioc)
{
	int i;

	dtmprintk(ioc, ioc_info(ioc, "%s: MPT3_IOC_DONE_RESET\n", __func__));

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
}

/**
 * _ctl_fasync -
 * @fd: ?
 * @filep: ?
 * @mode: ?
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
 * @filep: ?
 * @wait: ?
 *
 */
static __poll_t
_ctl_poll(struct file *filep, poll_table *wait)
{
	struct MPT3SAS_ADAPTER *ioc;

	poll_wait(filep, &ctl_poll_wait, wait);

	/* global ioc lock to protect controller on list operations */
	spin_lock(&gioc_lock);
	list_for_each_entry(ioc, &mpt3sas_ioc_list, list) {
		if (ioc->aen_event_read_flag) {
			spin_unlock(&gioc_lock);
			return EPOLLIN | EPOLLRDNORM;
		}
	}
	spin_unlock(&gioc_lock);
	return 0;
}

/**
 * _ctl_set_task_mid - assign an active smid to tm request
 * @ioc: per adapter object
 * @karg: (struct mpt3_ioctl_command)
 * @tm_request: pointer to mf from user space
 *
 * Return: 0 when an smid if found, else fail.
 * during failure, the reply frame is filled.
 */
static int
_ctl_set_task_mid(struct MPT3SAS_ADAPTER *ioc, struct mpt3_ioctl_command *karg,
	Mpi2SCSITaskManagementRequest_t *tm_request)
{
	bool found = false;
	u16 smid;
	u16 handle;
	struct scsi_cmnd *scmd;
	struct MPT3SAS_DEVICE *priv_data;
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
	for (smid = ioc->scsiio_depth; smid && !found; smid--) {
		struct scsiio_tracker *st;
		__le16 task_mid;

		scmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
		if (!scmd)
			continue;
		if (lun != scmd->device->lun)
			continue;
		priv_data = scmd->device->hostdata;
		if (priv_data->sas_target == NULL)
			continue;
		if (priv_data->sas_target->handle != handle)
			continue;
		st = scsi_cmd_priv(scmd);

		/*
		 * If the given TaskMID from the user space is zero, then the
		 * first outstanding smid will be picked up.  Otherwise,
		 * targeted smid will be the one.
		 */
		task_mid = cpu_to_le16(st->smid);
		if (!tm_request->TaskMID)
			tm_request->TaskMID = task_mid;
		found = tm_request->TaskMID == task_mid;
	}

	if (!found) {
		dctlprintk(ioc,
			   ioc_info(ioc, "%s: handle(0x%04x), lun(%d), no active mid!!\n",
				    desc, le16_to_cpu(tm_request->DevHandle),
				    lun));
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

	dctlprintk(ioc,
		   ioc_info(ioc, "%s: handle(0x%04x), lun(%d), task_mid(%d)\n",
			    desc, le16_to_cpu(tm_request->DevHandle), lun,
			    le16_to_cpu(tm_request->TaskMID)));
	return 0;
}

/**
 * _ctl_do_mpt_command - main handler for MPT3COMMAND opcode
 * @ioc: per adapter object
 * @karg: (struct mpt3_ioctl_command)
 * @mf: pointer to mf in user space
 */
static long
_ctl_do_mpt_command(struct MPT3SAS_ADAPTER *ioc, struct mpt3_ioctl_command karg,
	void __user *mf)
{
	MPI2RequestHeader_t *mpi_request = NULL, *request;
	MPI2DefaultReply_t *mpi_reply;
	Mpi26NVMeEncapsulatedRequest_t *nvme_encap_request = NULL;
	struct _pcie_device *pcie_device = NULL;
	u16 smid;
	unsigned long timeout;
	u8 issue_reset;
	u32 sz, sz_arg;
	void *psge;
	void *data_out = NULL;
	dma_addr_t data_out_dma = 0;
	size_t data_out_sz = 0;
	void *data_in = NULL;
	dma_addr_t data_in_dma = 0;
	size_t data_in_sz = 0;
	long ret;
	u16 device_handle = MPT3SAS_INVALID_DEVICE_HANDLE;

	issue_reset = 0;

	if (ioc->ctl_cmds.status != MPT3_CMD_NOT_USED) {
		ioc_err(ioc, "%s: ctl_cmd in use\n", __func__);
		ret = -EAGAIN;
		goto out;
	}

	ret = mpt3sas_wait_for_ioc(ioc,	IOC_OPERATIONAL_WAIT_COUNT);
	if (ret)
		goto out;

	mpi_request = kzalloc(ioc->request_sz, GFP_KERNEL);
	if (!mpi_request) {
		ioc_err(ioc, "%s: failed obtaining a memory for mpi_request\n",
			__func__);
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
			ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
			ret = -EAGAIN;
			goto out;
		}
	} else {
		/* Use first reserved smid for passthrough ioctls */
		smid = ioc->scsiio_depth - INTERNAL_SCSIIO_CMDS_COUNT + 1;
	}

	ret = 0;
	ioc->ctl_cmds.status = MPT3_CMD_PENDING;
	memset(ioc->ctl_cmds.reply, 0, ioc->reply_sz);
	request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(request, 0, ioc->request_sz);
	memcpy(request, mpi_request, karg.data_sge_offset*4);
	ioc->ctl_cmds.smid = smid;
	data_out_sz = karg.data_out_size;
	data_in_sz = karg.data_in_size;

	if (mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST ||
	    mpi_request->Function == MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH ||
	    mpi_request->Function == MPI2_FUNCTION_SCSI_TASK_MGMT ||
	    mpi_request->Function == MPI2_FUNCTION_SATA_PASSTHROUGH ||
	    mpi_request->Function == MPI2_FUNCTION_NVME_ENCAPSULATED) {

		device_handle = le16_to_cpu(mpi_request->FunctionDependent1);
		if (!device_handle || (device_handle >
		    ioc->facts.MaxDevHandle)) {
			ret = -EINVAL;
			mpt3sas_base_free_smid(ioc, smid);
			goto out;
		}
	}

	/* obtain dma-able memory for data transfer */
	if (data_out_sz) /* WRITE */ {
		data_out = dma_alloc_coherent(&ioc->pdev->dev, data_out_sz,
				&data_out_dma, GFP_KERNEL);
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
		data_in = dma_alloc_coherent(&ioc->pdev->dev, data_in_sz,
				&data_in_dma, GFP_KERNEL);
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
	_ctl_display_some_debug(ioc, smid, "ctl_request", NULL);

	init_completion(&ioc->ctl_cmds.done);
	switch (mpi_request->Function) {
	case MPI2_FUNCTION_NVME_ENCAPSULATED:
	{
		nvme_encap_request = (Mpi26NVMeEncapsulatedRequest_t *)request;
		if (!ioc->pcie_sg_lookup) {
			dtmprintk(ioc, ioc_info(ioc,
			    "HBA doesn't support NVMe. Rejecting NVMe Encapsulated request.\n"
			    ));

			if (ioc->logging_level & MPT_DEBUG_TM)
				_debug_dump_mf(nvme_encap_request,
				    ioc->request_sz/4);
			mpt3sas_base_free_smid(ioc, smid);
			ret = -EINVAL;
			goto out;
		}
		/*
		 * Get the Physical Address of the sense buffer.
		 * Use Error Response buffer address field to hold the sense
		 * buffer address.
		 * Clear the internal sense buffer, which will potentially hold
		 * the Completion Queue Entry on return, or 0 if no Entry.
		 * Build the PRPs and set direction bits.
		 * Send the request.
		 */
		nvme_encap_request->ErrorResponseBaseAddress =
		    cpu_to_le64(ioc->sense_dma & 0xFFFFFFFF00000000UL);
		nvme_encap_request->ErrorResponseBaseAddress |=
		   cpu_to_le64(le32_to_cpu(
		   mpt3sas_base_get_sense_buffer_dma(ioc, smid)));
		nvme_encap_request->ErrorResponseAllocationLength =
					cpu_to_le16(NVME_ERROR_RESPONSE_SIZE);
		memset(ioc->ctl_cmds.sense, 0, NVME_ERROR_RESPONSE_SIZE);
		ioc->build_nvme_prp(ioc, smid, nvme_encap_request,
		    data_out_dma, data_out_sz, data_in_dma, data_in_sz);
		if (test_bit(device_handle, ioc->device_remove_in_progress)) {
			dtmprintk(ioc,
				  ioc_info(ioc, "handle(0x%04x): ioctl failed due to device removal in progress\n",
					   device_handle));
			mpt3sas_base_free_smid(ioc, smid);
			ret = -EINVAL;
			goto out;
		}
		mpt3sas_base_put_smid_nvme_encap(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_SCSI_IO_REQUEST:
	case MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH:
	{
		Mpi2SCSIIORequest_t *scsiio_request =
		    (Mpi2SCSIIORequest_t *)request;
		scsiio_request->SenseBufferLength = SCSI_SENSE_BUFFERSIZE;
		scsiio_request->SenseBufferLowAddress =
		    mpt3sas_base_get_sense_buffer_dma(ioc, smid);
		memset(ioc->ctl_cmds.sense, 0, SCSI_SENSE_BUFFERSIZE);
		if (test_bit(device_handle, ioc->device_remove_in_progress)) {
			dtmprintk(ioc,
				  ioc_info(ioc, "handle(0x%04x) :ioctl failed due to device removal in progress\n",
					   device_handle));
			mpt3sas_base_free_smid(ioc, smid);
			ret = -EINVAL;
			goto out;
		}
		ioc->build_sg(ioc, psge, data_out_dma, data_out_sz,
		    data_in_dma, data_in_sz);
		if (mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST)
			ioc->put_smid_scsi_io(ioc, smid, device_handle);
		else
			ioc->put_smid_default(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_SCSI_TASK_MGMT:
	{
		Mpi2SCSITaskManagementRequest_t *tm_request =
		    (Mpi2SCSITaskManagementRequest_t *)request;

		dtmprintk(ioc,
			  ioc_info(ioc, "TASK_MGMT: handle(0x%04x), task_type(0x%02x)\n",
				   le16_to_cpu(tm_request->DevHandle),
				   tm_request->TaskType));
		ioc->got_task_abort_from_ioctl = 1;
		if (tm_request->TaskType ==
		    MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK ||
		    tm_request->TaskType ==
		    MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK) {
			if (_ctl_set_task_mid(ioc, &karg, tm_request)) {
				mpt3sas_base_free_smid(ioc, smid);
				ioc->got_task_abort_from_ioctl = 0;
				goto out;
			}
		}
		ioc->got_task_abort_from_ioctl = 0;

		if (test_bit(device_handle, ioc->device_remove_in_progress)) {
			dtmprintk(ioc,
				  ioc_info(ioc, "handle(0x%04x) :ioctl failed due to device removal in progress\n",
					   device_handle));
			mpt3sas_base_free_smid(ioc, smid);
			ret = -EINVAL;
			goto out;
		}
		mpt3sas_scsih_set_tm_flag(ioc, le16_to_cpu(
		    tm_request->DevHandle));
		ioc->build_sg_mpi(ioc, psge, data_out_dma, data_out_sz,
		    data_in_dma, data_in_sz);
		ioc->put_smid_hi_priority(ioc, smid, 0);
		break;
	}
	case MPI2_FUNCTION_SMP_PASSTHROUGH:
	{
		Mpi2SmpPassthroughRequest_t *smp_request =
		    (Mpi2SmpPassthroughRequest_t *)mpi_request;
		u8 *data;

		if (!ioc->multipath_on_hba) {
			/* ioc determines which port to use */
			smp_request->PhysicalPort = 0xFF;
		}
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
		ioc->put_smid_default(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_SATA_PASSTHROUGH:
	{
		if (test_bit(device_handle, ioc->device_remove_in_progress)) {
			dtmprintk(ioc,
				  ioc_info(ioc, "handle(0x%04x) :ioctl failed due to device removal in progress\n",
					   device_handle));
			mpt3sas_base_free_smid(ioc, smid);
			ret = -EINVAL;
			goto out;
		}
		ioc->build_sg(ioc, psge, data_out_dma, data_out_sz, data_in_dma,
		    data_in_sz);
		ioc->put_smid_default(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_FW_DOWNLOAD:
	{
		if (ioc->pdev->vendor == MPI2_MFGPAGE_VENDORID_ATTO) {
			ioc_info(ioc, "Firmware download not supported for ATTO HBA.\n");
			ret = -EPERM;
			break;
		}
		fallthrough;
	}
	case MPI2_FUNCTION_FW_UPLOAD:
	{
		ioc->build_sg(ioc, psge, data_out_dma, data_out_sz, data_in_dma,
		    data_in_sz);
		ioc->put_smid_default(ioc, smid);
		break;
	}
	case MPI2_FUNCTION_TOOLBOX:
	{
		Mpi2ToolboxCleanRequest_t *toolbox_request =
			(Mpi2ToolboxCleanRequest_t *)mpi_request;

		if ((toolbox_request->Tool == MPI2_TOOLBOX_DIAGNOSTIC_CLI_TOOL)
		    || (toolbox_request->Tool ==
		    MPI26_TOOLBOX_BACKEND_PCIE_LANE_MARGIN))
			ioc->build_sg(ioc, psge, data_out_dma, data_out_sz,
				data_in_dma, data_in_sz);
		else if (toolbox_request->Tool ==
				MPI2_TOOLBOX_MEMORY_MOVE_TOOL) {
			Mpi2ToolboxMemMoveRequest_t *mem_move_request =
					(Mpi2ToolboxMemMoveRequest_t *)request;
			Mpi2SGESimple64_t tmp, *src = NULL, *dst = NULL;

			ioc->build_sg_mpi(ioc, psge, data_out_dma,
					data_out_sz, data_in_dma, data_in_sz);
			if (data_out_sz && !data_in_sz) {
				dst =
				    (Mpi2SGESimple64_t *)&mem_move_request->SGL;
				src = (void *)dst + ioc->sge_size;

				memcpy(&tmp, src, ioc->sge_size);
				memcpy(src, dst, ioc->sge_size);
				memcpy(dst, &tmp, ioc->sge_size);
			}
			if (ioc->logging_level & MPT_DEBUG_TM) {
				ioc_info(ioc,
				  "Mpi2ToolboxMemMoveRequest_t request msg\n");
				_debug_dump_mf(mem_move_request,
							ioc->request_sz/4);
			}
		} else
			ioc->build_sg_mpi(ioc, psge, data_out_dma, data_out_sz,
			    data_in_dma, data_in_sz);
		ioc->put_smid_default(ioc, smid);
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
		fallthrough;
	default:
		ioc->build_sg_mpi(ioc, psge, data_out_dma, data_out_sz,
		    data_in_dma, data_in_sz);
		ioc->put_smid_default(ioc, smid);
		break;
	}

	if (karg.timeout < MPT3_IOCTL_DEFAULT_TIMEOUT)
		timeout = MPT3_IOCTL_DEFAULT_TIMEOUT;
	else
		timeout = karg.timeout;
	wait_for_completion_timeout(&ioc->ctl_cmds.done, timeout*HZ);
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
		mpt3sas_check_cmd_timeout(ioc,
		    ioc->ctl_cmds.status, mpi_request,
		    karg.data_sge_offset, issue_reset);
		goto issue_host_reset;
	}

	mpi_reply = ioc->ctl_cmds.reply;

	if (mpi_reply->Function == MPI2_FUNCTION_SCSI_TASK_MGMT &&
	    (ioc->logging_level & MPT_DEBUG_TM)) {
		Mpi2SCSITaskManagementReply_t *tm_reply =
		    (Mpi2SCSITaskManagementReply_t *)mpi_reply;

		ioc_info(ioc, "TASK_MGMT: IOCStatus(0x%04x), IOCLogInfo(0x%08x), TerminationCount(0x%08x)\n",
			 le16_to_cpu(tm_reply->IOCStatus),
			 le32_to_cpu(tm_reply->IOCLogInfo),
			 le32_to_cpu(tm_reply->TerminationCount));
	}

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

	/* copy out sense/NVMe Error Response to user */
	if (karg.max_sense_bytes && (mpi_request->Function ==
	    MPI2_FUNCTION_SCSI_IO_REQUEST || mpi_request->Function ==
	    MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH || mpi_request->Function ==
	    MPI2_FUNCTION_NVME_ENCAPSULATED)) {
		if (karg.sense_data_ptr == NULL) {
			ioc_info(ioc, "Response buffer provided by application is NULL; Response data will not be returned\n");
			goto out;
		}
		sz_arg = (mpi_request->Function ==
		MPI2_FUNCTION_NVME_ENCAPSULATED) ? NVME_ERROR_RESPONSE_SIZE :
							SCSI_SENSE_BUFFERSIZE;
		sz = min_t(u32, karg.max_sense_bytes, sz_arg);
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
			ioc_info(ioc, "issue target reset: handle = (0x%04x)\n",
				 le16_to_cpu(mpi_request->FunctionDependent1));
			mpt3sas_halt_firmware(ioc);
			pcie_device = mpt3sas_get_pdev_by_handle(ioc,
				le16_to_cpu(mpi_request->FunctionDependent1));
			if (pcie_device && (!ioc->tm_custom_handling) &&
			    (!(mpt3sas_scsih_is_pcie_scsi_device(
			    pcie_device->device_info))))
				mpt3sas_scsih_issue_locked_tm(ioc,
				  le16_to_cpu(mpi_request->FunctionDependent1),
				  0, 0, 0,
				  MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, 0,
				  0, pcie_device->reset_timeout,
			MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE);
			else
				mpt3sas_scsih_issue_locked_tm(ioc,
				  le16_to_cpu(mpi_request->FunctionDependent1),
				  0, 0, 0,
				  MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, 0,
				  0, 30, MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET);
		} else
			mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	}

 out:
	if (pcie_device)
		pcie_device_put(pcie_device);

	/* free memory associated with sg buffers */
	if (data_in)
		dma_free_coherent(&ioc->pdev->dev, data_in_sz, data_in,
		    data_in_dma);

	if (data_out)
		dma_free_coherent(&ioc->pdev->dev, data_out_sz, data_out,
		    data_out_dma);

	kfree(mpi_request);
	ioc->ctl_cmds.status = MPT3_CMD_NOT_USED;
	return ret;
}

/**
 * _ctl_getiocinfo - main handler for MPT3IOCINFO opcode
 * @ioc: per adapter object
 * @arg: user space buffer containing ioctl content
 */
static long
_ctl_getiocinfo(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_ioctl_iocinfo karg;

	dctlprintk(ioc, ioc_info(ioc, "%s: enter\n",
				 __func__));

	memset(&karg, 0 , sizeof(karg));
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
	strcpy(karg.driver_version, ioc->driver_name);
	strcat(karg.driver_version, "-");
	switch  (ioc->hba_mpi_version_belonged) {
	case MPI2_VERSION:
		if (ioc->is_warpdrive)
			karg.adapter_type = MPT2_IOCTL_INTERFACE_SAS2_SSS6200;
		else
			karg.adapter_type = MPT2_IOCTL_INTERFACE_SAS2;
		strcat(karg.driver_version, MPT2SAS_DRIVER_VERSION);
		break;
	case MPI25_VERSION:
	case MPI26_VERSION:
		if (ioc->is_gen35_ioc)
			karg.adapter_type = MPT3_IOCTL_INTERFACE_SAS35;
		else
			karg.adapter_type = MPT3_IOCTL_INTERFACE_SAS3;
		strcat(karg.driver_version, MPT3SAS_DRIVER_VERSION);
		break;
	}
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
 * @arg: user space buffer containing ioctl content
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

	dctlprintk(ioc, ioc_info(ioc, "%s: enter\n",
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
 * @arg: user space buffer containing ioctl content
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

	dctlprintk(ioc, ioc_info(ioc, "%s: enter\n",
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
 * @arg: user space buffer containing ioctl content
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

	dctlprintk(ioc, ioc_info(ioc, "%s: enter\n",
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
 * @arg: user space buffer containing ioctl content
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

	dctlprintk(ioc, ioc_info(ioc, "%s: enter\n",
				 __func__));

	ioc->reset_from_user = 1;
	retval = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	ioc_info(ioc,
	    "Ioctl: host reset: %s\n", ((!retval) ? "SUCCESS" : "FAILED"));
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
 * _ctl_btdh_search_pcie_device - searching for pcie device
 * @ioc: per adapter object
 * @btdh: btdh ioctl payload
 */
static int
_ctl_btdh_search_pcie_device(struct MPT3SAS_ADAPTER *ioc,
	struct mpt3_ioctl_btdh_mapping *btdh)
{
	struct _pcie_device *pcie_device;
	unsigned long flags;
	int rc = 0;

	if (list_empty(&ioc->pcie_device_list))
		return rc;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	list_for_each_entry(pcie_device, &ioc->pcie_device_list, list) {
		if (btdh->bus == 0xFFFFFFFF && btdh->id == 0xFFFFFFFF &&
			   btdh->handle == pcie_device->handle) {
			btdh->bus = pcie_device->channel;
			btdh->id = pcie_device->id;
			rc = 1;
			goto out;
		} else if (btdh->bus == pcie_device->channel && btdh->id ==
			   pcie_device->id && btdh->handle == 0xFFFF) {
			btdh->handle = pcie_device->handle;
			rc = 1;
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
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
 * @arg: user space buffer containing ioctl content
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

	dctlprintk(ioc, ioc_info(ioc, "%s\n",
				 __func__));

	rc = _ctl_btdh_search_sas_device(ioc, &karg);
	if (!rc)
		rc = _ctl_btdh_search_pcie_device(ioc, &karg);
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
 * _ctl_diag_get_bufftype - return diag buffer type
 *              either TRACE, SNAPSHOT, or EXTENDED
 * @ioc: per adapter object
 * @unique_id: specifies the unique_id for the buffer
 *
 * returns MPT3_DIAG_UID_NOT_FOUND if the id not found
 */
static u8
_ctl_diag_get_bufftype(struct MPT3SAS_ADAPTER *ioc, u32 unique_id)
{
	u8  index;

	for (index = 0; index < MPI2_DIAG_BUF_TYPE_COUNT; index++) {
		if (ioc->unique_id[index] == unique_id)
			return index;
	}

	return MPT3_DIAG_UID_NOT_FOUND;
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
	u16 smid;
	u16 ioc_status;
	u32 ioc_state;
	u8 issue_reset = 0;

	dctlprintk(ioc, ioc_info(ioc, "%s\n",
				 __func__));

	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		ioc_err(ioc, "%s: failed due to ioc not operational\n",
			__func__);
		rc = -EAGAIN;
		goto out;
	}

	if (ioc->ctl_cmds.status != MPT3_CMD_NOT_USED) {
		ioc_err(ioc, "%s: ctl_cmd in use\n", __func__);
		rc = -EAGAIN;
		goto out;
	}

	buffer_type = diag_register->buffer_type;
	if (!_ctl_diag_capability(ioc, buffer_type)) {
		ioc_err(ioc, "%s: doesn't have capability for buffer_type(0x%02x)\n",
			__func__, buffer_type);
		return -EPERM;
	}

	if (diag_register->unique_id == 0) {
		ioc_err(ioc,
		    "%s: Invalid UID(0x%08x), buffer_type(0x%02x)\n", __func__,
		    diag_register->unique_id, buffer_type);
		return -EINVAL;
	}

	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_APP_OWNED) &&
	    !(ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_RELEASED)) {
		ioc_err(ioc,
		    "%s: buffer_type(0x%02x) is already registered by application with UID(0x%08x)\n",
		    __func__, buffer_type, ioc->unique_id[buffer_type]);
		return -EINVAL;
	}

	if (ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) {
		/*
		 * If driver posts buffer initially, then an application wants
		 * to Register that buffer (own it) without Releasing first,
		 * the application Register command MUST have the same buffer
		 * type and size in the Register command (obtained from the
		 * Query command). Otherwise that Register command will be
		 * failed. If the application has released the buffer but wants
		 * to re-register it, it should be allowed as long as the
		 * Unique-Id/Size match.
		 */

		if (ioc->unique_id[buffer_type] == MPT3DIAGBUFFUNIQUEID &&
		    ioc->diag_buffer_sz[buffer_type] ==
		    diag_register->requested_buffer_size) {

			if (!(ioc->diag_buffer_status[buffer_type] &
			     MPT3_DIAG_BUFFER_IS_RELEASED)) {
				dctlprintk(ioc, ioc_info(ioc,
				    "%s: diag_buffer (%d) ownership changed. old-ID(0x%08x), new-ID(0x%08x)\n",
				    __func__, buffer_type,
				    ioc->unique_id[buffer_type],
				    diag_register->unique_id));

				/*
				 * Application wants to own the buffer with
				 * the same size.
				 */
				ioc->unique_id[buffer_type] =
				    diag_register->unique_id;
				rc = 0; /* success */
				goto out;
			}
		} else if (ioc->unique_id[buffer_type] !=
		    MPT3DIAGBUFFUNIQUEID) {
			if (ioc->unique_id[buffer_type] !=
			    diag_register->unique_id ||
			    ioc->diag_buffer_sz[buffer_type] !=
			    diag_register->requested_buffer_size ||
			    !(ioc->diag_buffer_status[buffer_type] &
			    MPT3_DIAG_BUFFER_IS_RELEASED)) {
				ioc_err(ioc,
				    "%s: already has a registered buffer for buffer_type(0x%02x)\n",
				    __func__, buffer_type);
				return -EINVAL;
			}
		} else {
			ioc_err(ioc, "%s: already has a registered buffer for buffer_type(0x%02x)\n",
			    __func__, buffer_type);
			return -EINVAL;
		}
	} else if (ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_DRIVER_ALLOCATED) {

		if (ioc->unique_id[buffer_type] != MPT3DIAGBUFFUNIQUEID ||
		    ioc->diag_buffer_sz[buffer_type] !=
		    diag_register->requested_buffer_size) {

			ioc_err(ioc,
			    "%s: already a buffer is allocated for buffer_type(0x%02x) of size %d bytes, so please try registering again with same size\n",
			     __func__, buffer_type,
			    ioc->diag_buffer_sz[buffer_type]);
			return -EINVAL;
		}
	}

	if (diag_register->requested_buffer_size % 4)  {
		ioc_err(ioc, "%s: the requested_buffer_size is not 4 byte aligned\n",
			__func__);
		return -EINVAL;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->ctl_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->ctl_cmds.status = MPT3_CMD_PENDING;
	memset(ioc->ctl_cmds.reply, 0, ioc->reply_sz);
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, ioc->request_sz);
	ioc->ctl_cmds.smid = smid;

	request_data = ioc->diag_buffer[buffer_type];
	request_data_sz = diag_register->requested_buffer_size;
	ioc->unique_id[buffer_type] = diag_register->unique_id;
	/* Reset ioc variables used for additional query commands */
	ioc->reset_from_user = 0;
	memset(&ioc->htb_rel, 0, sizeof(struct htb_rel_query));
	ioc->diag_buffer_status[buffer_type] &=
	    MPT3_DIAG_BUFFER_IS_DRIVER_ALLOCATED;
	memcpy(ioc->product_specific[buffer_type],
	    diag_register->product_specific, MPT3_PRODUCT_SPECIFIC_DWORDS);
	ioc->diagnostic_flags[buffer_type] = diag_register->diagnostic_flags;

	if (request_data) {
		request_data_dma = ioc->diag_buffer_dma[buffer_type];
		if (request_data_sz != ioc->diag_buffer_sz[buffer_type]) {
			dma_free_coherent(&ioc->pdev->dev,
					ioc->diag_buffer_sz[buffer_type],
					request_data, request_data_dma);
			request_data = NULL;
		}
	}

	if (request_data == NULL) {
		ioc->diag_buffer_sz[buffer_type] = 0;
		ioc->diag_buffer_dma[buffer_type] = 0;
		request_data = dma_alloc_coherent(&ioc->pdev->dev,
				request_data_sz, &request_data_dma, GFP_KERNEL);
		if (request_data == NULL) {
			ioc_err(ioc, "%s: failed allocating memory for diag buffers, requested size(%d)\n",
				__func__, request_data_sz);
			mpt3sas_base_free_smid(ioc, smid);
			rc = -ENOMEM;
			goto out;
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

	dctlprintk(ioc,
		   ioc_info(ioc, "%s: diag_buffer(0x%p), dma(0x%llx), sz(%d)\n",
			    __func__, request_data,
			    (unsigned long long)request_data_dma,
			    le32_to_cpu(mpi_request->BufferLength)));

	for (i = 0; i < MPT3_PRODUCT_SPECIFIC_DWORDS; i++)
		mpi_request->ProductSpecific[i] =
			cpu_to_le32(ioc->product_specific[buffer_type][i]);

	init_completion(&ioc->ctl_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->ctl_cmds.done,
	    MPT3_IOCTL_DEFAULT_TIMEOUT*HZ);

	if (!(ioc->ctl_cmds.status & MPT3_CMD_COMPLETE)) {
		mpt3sas_check_cmd_timeout(ioc,
		    ioc->ctl_cmds.status, mpi_request,
		    sizeof(Mpi2DiagBufferPostRequest_t)/4, issue_reset);
		goto issue_host_reset;
	}

	/* process the completed Reply Message Frame */
	if ((ioc->ctl_cmds.status & MPT3_CMD_REPLY_VALID) == 0) {
		ioc_err(ioc, "%s: no reply message\n", __func__);
		rc = -EFAULT;
		goto out;
	}

	mpi_reply = ioc->ctl_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;

	if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
		ioc->diag_buffer_status[buffer_type] |=
			MPT3_DIAG_BUFFER_IS_REGISTERED;
		dctlprintk(ioc, ioc_info(ioc, "%s: success\n", __func__));
	} else {
		ioc_info(ioc, "%s: ioc_status(0x%04x) log_info(0x%08x)\n",
			 __func__,
			 ioc_status, le32_to_cpu(mpi_reply->IOCLogInfo));
		rc = -EFAULT;
	}

 issue_host_reset:
	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);

 out:

	if (rc && request_data) {
		dma_free_coherent(&ioc->pdev->dev, request_data_sz,
		    request_data, request_data_dma);
		ioc->diag_buffer[buffer_type] = NULL;
		ioc->diag_buffer_status[buffer_type] &=
		    ~MPT3_DIAG_BUFFER_IS_DRIVER_ALLOCATED;
	}

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
	u32 ret_val;
	u32 trace_buff_size = ioc->manu_pg11.HostTraceBufferMaxSizeKB<<10;
	u32 min_trace_buff_size = 0;
	u32 decr_trace_buff_size = 0;

	memset(&diag_register, 0, sizeof(struct mpt3_diag_register));

	if (bits_to_register & 1) {
		ioc_info(ioc, "registering trace buffer support\n");
		ioc->diag_trigger_master.MasterData =
		    (MASTER_TRIGGER_FW_FAULT + MASTER_TRIGGER_ADAPTER_RESET);
		diag_register.buffer_type = MPI2_DIAG_BUF_TYPE_TRACE;
		diag_register.unique_id =
		    (ioc->hba_mpi_version_belonged == MPI2_VERSION) ?
		    (MPT2DIAGBUFFUNIQUEID):(MPT3DIAGBUFFUNIQUEID);

		if (trace_buff_size != 0) {
			diag_register.requested_buffer_size = trace_buff_size;
			min_trace_buff_size =
			    ioc->manu_pg11.HostTraceBufferMinSizeKB<<10;
			decr_trace_buff_size =
			    ioc->manu_pg11.HostTraceBufferDecrementSizeKB<<10;

			if (min_trace_buff_size > trace_buff_size) {
				/* The buff size is not set correctly */
				ioc_err(ioc,
				    "Min Trace Buff size (%d KB) greater than Max Trace Buff size (%d KB)\n",
				     min_trace_buff_size>>10,
				     trace_buff_size>>10);
				ioc_err(ioc,
				    "Using zero Min Trace Buff Size\n");
				min_trace_buff_size = 0;
			}

			if (decr_trace_buff_size == 0) {
				/*
				 * retry the min size if decrement
				 * is not available.
				 */
				decr_trace_buff_size =
				    trace_buff_size - min_trace_buff_size;
			}
		} else {
			/* register for 2MB buffers  */
			diag_register.requested_buffer_size = 2 * (1024 * 1024);
		}

		do {
			ret_val = _ctl_diag_register_2(ioc,  &diag_register);

			if (ret_val == -ENOMEM && min_trace_buff_size &&
			    (trace_buff_size - decr_trace_buff_size) >=
			    min_trace_buff_size) {
				/* adjust the buffer size */
				trace_buff_size -= decr_trace_buff_size;
				diag_register.requested_buffer_size =
				    trace_buff_size;
			} else
				break;
		} while (true);

		if (ret_val == -ENOMEM)
			ioc_err(ioc,
			    "Cannot allocate trace buffer memory. Last memory tried = %d KB\n",
			    diag_register.requested_buffer_size>>10);
		else if (ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE]
		    & MPT3_DIAG_BUFFER_IS_REGISTERED) {
			ioc_info(ioc, "Trace buffer memory %d KB allocated\n",
			    diag_register.requested_buffer_size>>10);
			if (ioc->hba_mpi_version_belonged != MPI2_VERSION)
				ioc->diag_buffer_status[
				    MPI2_DIAG_BUF_TYPE_TRACE] |=
				    MPT3_DIAG_BUFFER_IS_DRIVER_ALLOCATED;
		}
	}

	if (bits_to_register & 2) {
		ioc_info(ioc, "registering snapshot buffer support\n");
		diag_register.buffer_type = MPI2_DIAG_BUF_TYPE_SNAPSHOT;
		/* register for 2MB buffers  */
		diag_register.requested_buffer_size = 2 * (1024 * 1024);
		diag_register.unique_id = 0x7075901;
		_ctl_diag_register_2(ioc,  &diag_register);
	}

	if (bits_to_register & 4) {
		ioc_info(ioc, "registering extended buffer support\n");
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
 * @arg: user space buffer containing ioctl content
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

	if (!rc && (ioc->diag_buffer_status[karg.buffer_type] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED))
		ioc->diag_buffer_status[karg.buffer_type] |=
		    MPT3_DIAG_BUFFER_IS_APP_OWNED;

	return rc;
}

/**
 * _ctl_diag_unregister - application unregister with driver
 * @ioc: per adapter object
 * @arg: user space buffer containing ioctl content
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

	dctlprintk(ioc, ioc_info(ioc, "%s\n",
				 __func__));

	buffer_type = _ctl_diag_get_bufftype(ioc, karg.unique_id);
	if (buffer_type == MPT3_DIAG_UID_NOT_FOUND) {
		ioc_err(ioc, "%s: buffer with unique_id(0x%08x) not found\n",
		    __func__, karg.unique_id);
		return -EINVAL;
	}

	if (!_ctl_diag_capability(ioc, buffer_type)) {
		ioc_err(ioc, "%s: doesn't have capability for buffer_type(0x%02x)\n",
			__func__, buffer_type);
		return -EPERM;
	}

	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		ioc_err(ioc, "%s: buffer_type(0x%02x) is not registered\n",
			__func__, buffer_type);
		return -EINVAL;
	}
	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_RELEASED) == 0) {
		ioc_err(ioc, "%s: buffer_type(0x%02x) has not been released\n",
			__func__, buffer_type);
		return -EINVAL;
	}

	if (karg.unique_id != ioc->unique_id[buffer_type]) {
		ioc_err(ioc, "%s: unique_id(0x%08x) is not registered\n",
			__func__, karg.unique_id);
		return -EINVAL;
	}

	request_data = ioc->diag_buffer[buffer_type];
	if (!request_data) {
		ioc_err(ioc, "%s: doesn't have memory allocated for buffer_type(0x%02x)\n",
			__func__, buffer_type);
		return -ENOMEM;
	}

	if (ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_DRIVER_ALLOCATED) {
		ioc->unique_id[buffer_type] = MPT3DIAGBUFFUNIQUEID;
		ioc->diag_buffer_status[buffer_type] &=
		    ~MPT3_DIAG_BUFFER_IS_APP_OWNED;
		ioc->diag_buffer_status[buffer_type] &=
		    ~MPT3_DIAG_BUFFER_IS_REGISTERED;
	} else {
		request_data_sz = ioc->diag_buffer_sz[buffer_type];
		request_data_dma = ioc->diag_buffer_dma[buffer_type];
		dma_free_coherent(&ioc->pdev->dev, request_data_sz,
				request_data, request_data_dma);
		ioc->diag_buffer[buffer_type] = NULL;
		ioc->diag_buffer_status[buffer_type] = 0;
	}
	return 0;
}

/**
 * _ctl_diag_query - query relevant info associated with diag buffers
 * @ioc: per adapter object
 * @arg: user space buffer containing ioctl content
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

	dctlprintk(ioc, ioc_info(ioc, "%s\n",
				 __func__));

	karg.application_flags = 0;
	buffer_type = karg.buffer_type;

	if (!_ctl_diag_capability(ioc, buffer_type)) {
		ioc_err(ioc, "%s: doesn't have capability for buffer_type(0x%02x)\n",
			__func__, buffer_type);
		return -EPERM;
	}

	if (!(ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_DRIVER_ALLOCATED)) {
		if ((ioc->diag_buffer_status[buffer_type] &
		    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
			ioc_err(ioc, "%s: buffer_type(0x%02x) is not registered\n",
				__func__, buffer_type);
			return -EINVAL;
		}
	}

	if (karg.unique_id) {
		if (karg.unique_id != ioc->unique_id[buffer_type]) {
			ioc_err(ioc, "%s: unique_id(0x%08x) is not registered\n",
				__func__, karg.unique_id);
			return -EINVAL;
		}
	}

	request_data = ioc->diag_buffer[buffer_type];
	if (!request_data) {
		ioc_err(ioc, "%s: doesn't have buffer for buffer_type(0x%02x)\n",
			__func__, buffer_type);
		return -ENOMEM;
	}

	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED))
		karg.application_flags |= MPT3_APP_FLAGS_BUFFER_VALID;

	if (!(ioc->diag_buffer_status[buffer_type] &
	     MPT3_DIAG_BUFFER_IS_RELEASED))
		karg.application_flags |= MPT3_APP_FLAGS_FW_BUFFER_ACCESS;

	if (!(ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_DRIVER_ALLOCATED))
		karg.application_flags |= MPT3_APP_FLAGS_DYNAMIC_BUFFER_ALLOC;

	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_APP_OWNED))
		karg.application_flags |= MPT3_APP_FLAGS_APP_OWNED;

	for (i = 0; i < MPT3_PRODUCT_SPECIFIC_DWORDS; i++)
		karg.product_specific[i] =
		    ioc->product_specific[buffer_type][i];

	karg.total_buffer_size = ioc->diag_buffer_sz[buffer_type];
	karg.driver_added_buffer_size = 0;
	karg.unique_id = ioc->unique_id[buffer_type];
	karg.diagnostic_flags = ioc->diagnostic_flags[buffer_type];

	if (copy_to_user(arg, &karg, sizeof(struct mpt3_diag_query))) {
		ioc_err(ioc, "%s: unable to write mpt3_diag_query data @ %p\n",
			__func__, arg);
		return -EFAULT;
	}
	return 0;
}

/**
 * mpt3sas_send_diag_release - Diag Release Message
 * @ioc: per adapter object
 * @buffer_type: specifies either TRACE, SNAPSHOT, or EXTENDED
 * @issue_reset: specifies whether host reset is required.
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
	u8 reset_needed = 0;

	dctlprintk(ioc, ioc_info(ioc, "%s\n",
				 __func__));

	rc = 0;
	*issue_reset = 0;


	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		if (ioc->diag_buffer_status[buffer_type] &
		    MPT3_DIAG_BUFFER_IS_REGISTERED)
			ioc->diag_buffer_status[buffer_type] |=
			    MPT3_DIAG_BUFFER_IS_RELEASED;
		dctlprintk(ioc,
			   ioc_info(ioc, "%s: skipping due to FAULT state\n",
				    __func__));
		rc = -EAGAIN;
		goto out;
	}

	if (ioc->ctl_cmds.status != MPT3_CMD_NOT_USED) {
		ioc_err(ioc, "%s: ctl_cmd in use\n", __func__);
		rc = -EAGAIN;
		goto out;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->ctl_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		rc = -EAGAIN;
		goto out;
	}

	ioc->ctl_cmds.status = MPT3_CMD_PENDING;
	memset(ioc->ctl_cmds.reply, 0, ioc->reply_sz);
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, ioc->request_sz);
	ioc->ctl_cmds.smid = smid;

	mpi_request->Function = MPI2_FUNCTION_DIAG_RELEASE;
	mpi_request->BufferType = buffer_type;
	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;

	init_completion(&ioc->ctl_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->ctl_cmds.done,
	    MPT3_IOCTL_DEFAULT_TIMEOUT*HZ);

	if (!(ioc->ctl_cmds.status & MPT3_CMD_COMPLETE)) {
		mpt3sas_check_cmd_timeout(ioc,
		    ioc->ctl_cmds.status, mpi_request,
		    sizeof(Mpi2DiagReleaseRequest_t)/4, reset_needed);
		*issue_reset = reset_needed;
		rc = -EFAULT;
		goto out;
	}

	/* process the completed Reply Message Frame */
	if ((ioc->ctl_cmds.status & MPT3_CMD_REPLY_VALID) == 0) {
		ioc_err(ioc, "%s: no reply message\n", __func__);
		rc = -EFAULT;
		goto out;
	}

	mpi_reply = ioc->ctl_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;

	if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
		ioc->diag_buffer_status[buffer_type] |=
		    MPT3_DIAG_BUFFER_IS_RELEASED;
		dctlprintk(ioc, ioc_info(ioc, "%s: success\n", __func__));
	} else {
		ioc_info(ioc, "%s: ioc_status(0x%04x) log_info(0x%08x)\n",
			 __func__,
			 ioc_status, le32_to_cpu(mpi_reply->IOCLogInfo));
		rc = -EFAULT;
	}

 out:
	ioc->ctl_cmds.status = MPT3_CMD_NOT_USED;
	return rc;
}

/**
 * _ctl_diag_release - request to send Diag Release Message to firmware
 * @ioc: ?
 * @arg: user space buffer containing ioctl content
 *
 * This allows ownership of the specified buffer to returned to the driver,
 * allowing an application to read the buffer without fear that firmware is
 * overwriting information in the buffer.
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

	dctlprintk(ioc, ioc_info(ioc, "%s\n",
				 __func__));

	buffer_type = _ctl_diag_get_bufftype(ioc, karg.unique_id);
	if (buffer_type == MPT3_DIAG_UID_NOT_FOUND) {
		ioc_err(ioc, "%s: buffer with unique_id(0x%08x) not found\n",
		    __func__, karg.unique_id);
		return -EINVAL;
	}

	if (!_ctl_diag_capability(ioc, buffer_type)) {
		ioc_err(ioc, "%s: doesn't have capability for buffer_type(0x%02x)\n",
			__func__, buffer_type);
		return -EPERM;
	}

	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		ioc_err(ioc, "%s: buffer_type(0x%02x) is not registered\n",
			__func__, buffer_type);
		return -EINVAL;
	}

	if (karg.unique_id != ioc->unique_id[buffer_type]) {
		ioc_err(ioc, "%s: unique_id(0x%08x) is not registered\n",
			__func__, karg.unique_id);
		return -EINVAL;
	}

	if (ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_RELEASED) {
		ioc_err(ioc, "%s: buffer_type(0x%02x) is already released\n",
			__func__, buffer_type);
		return -EINVAL;
	}

	request_data = ioc->diag_buffer[buffer_type];

	if (!request_data) {
		ioc_err(ioc, "%s: doesn't have memory allocated for buffer_type(0x%02x)\n",
			__func__, buffer_type);
		return -ENOMEM;
	}

	/* buffers were released by due to host reset */
	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_DIAG_RESET)) {
		ioc->diag_buffer_status[buffer_type] |=
		    MPT3_DIAG_BUFFER_IS_RELEASED;
		ioc->diag_buffer_status[buffer_type] &=
		    ~MPT3_DIAG_BUFFER_IS_DIAG_RESET;
		ioc_err(ioc, "%s: buffer_type(0x%02x) was released due to host reset\n",
			__func__, buffer_type);
		return 0;
	}

	rc = mpt3sas_send_diag_release(ioc, buffer_type, &issue_reset);

	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);

	return rc;
}

/**
 * _ctl_diag_read_buffer - request for copy of the diag buffer
 * @ioc: per adapter object
 * @arg: user space buffer containing ioctl content
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
	unsigned long request_size, copy_size;
	u16 smid;
	u16 ioc_status;
	u8 issue_reset = 0;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	dctlprintk(ioc, ioc_info(ioc, "%s\n",
				 __func__));

	buffer_type = _ctl_diag_get_bufftype(ioc, karg.unique_id);
	if (buffer_type == MPT3_DIAG_UID_NOT_FOUND) {
		ioc_err(ioc, "%s: buffer with unique_id(0x%08x) not found\n",
		    __func__, karg.unique_id);
		return -EINVAL;
	}

	if (!_ctl_diag_capability(ioc, buffer_type)) {
		ioc_err(ioc, "%s: doesn't have capability for buffer_type(0x%02x)\n",
			__func__, buffer_type);
		return -EPERM;
	}

	if (karg.unique_id != ioc->unique_id[buffer_type]) {
		ioc_err(ioc, "%s: unique_id(0x%08x) is not registered\n",
			__func__, karg.unique_id);
		return -EINVAL;
	}

	request_data = ioc->diag_buffer[buffer_type];
	if (!request_data) {
		ioc_err(ioc, "%s: doesn't have buffer for buffer_type(0x%02x)\n",
			__func__, buffer_type);
		return -ENOMEM;
	}

	request_size = ioc->diag_buffer_sz[buffer_type];

	if ((karg.starting_offset % 4) || (karg.bytes_to_read % 4)) {
		ioc_err(ioc, "%s: either the starting_offset or bytes_to_read are not 4 byte aligned\n",
			__func__);
		return -EINVAL;
	}

	if (karg.starting_offset > request_size)
		return -EINVAL;

	diag_data = (void *)(request_data + karg.starting_offset);
	dctlprintk(ioc,
		   ioc_info(ioc, "%s: diag_buffer(%p), offset(%d), sz(%d)\n",
			    __func__, diag_data, karg.starting_offset,
			    karg.bytes_to_read));

	/* Truncate data on requests that are too large */
	if ((diag_data + karg.bytes_to_read < diag_data) ||
	    (diag_data + karg.bytes_to_read > request_data + request_size))
		copy_size = request_size - karg.starting_offset;
	else
		copy_size = karg.bytes_to_read;

	if (copy_to_user((void __user *)uarg->diagnostic_data,
	    diag_data, copy_size)) {
		ioc_err(ioc, "%s: Unable to write mpt_diag_read_buffer_t data @ %p\n",
			__func__, diag_data);
		return -EFAULT;
	}

	if ((karg.flags & MPT3_FLAGS_REREGISTER) == 0)
		return 0;

	dctlprintk(ioc,
		   ioc_info(ioc, "%s: Reregister buffer_type(0x%02x)\n",
			    __func__, buffer_type));
	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_RELEASED) == 0) {
		dctlprintk(ioc,
			   ioc_info(ioc, "%s: buffer_type(0x%02x) is still registered\n",
				    __func__, buffer_type));
		return 0;
	}
	/* Get a free request frame and save the message context.
	*/

	if (ioc->ctl_cmds.status != MPT3_CMD_NOT_USED) {
		ioc_err(ioc, "%s: ctl_cmd in use\n", __func__);
		rc = -EAGAIN;
		goto out;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->ctl_cb_idx);
	if (!smid) {
		ioc_err(ioc, "%s: failed obtaining a smid\n", __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->ctl_cmds.status = MPT3_CMD_PENDING;
	memset(ioc->ctl_cmds.reply, 0, ioc->reply_sz);
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, ioc->request_sz);
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
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->ctl_cmds.done,
	    MPT3_IOCTL_DEFAULT_TIMEOUT*HZ);

	if (!(ioc->ctl_cmds.status & MPT3_CMD_COMPLETE)) {
		mpt3sas_check_cmd_timeout(ioc,
		    ioc->ctl_cmds.status, mpi_request,
		    sizeof(Mpi2DiagBufferPostRequest_t)/4, issue_reset);
		goto issue_host_reset;
	}

	/* process the completed Reply Message Frame */
	if ((ioc->ctl_cmds.status & MPT3_CMD_REPLY_VALID) == 0) {
		ioc_err(ioc, "%s: no reply message\n", __func__);
		rc = -EFAULT;
		goto out;
	}

	mpi_reply = ioc->ctl_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;

	if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
		ioc->diag_buffer_status[buffer_type] |=
		    MPT3_DIAG_BUFFER_IS_REGISTERED;
		ioc->diag_buffer_status[buffer_type] &=
		    ~MPT3_DIAG_BUFFER_IS_RELEASED;
		dctlprintk(ioc, ioc_info(ioc, "%s: success\n", __func__));
	} else {
		ioc_info(ioc, "%s: ioc_status(0x%04x) log_info(0x%08x)\n",
			 __func__, ioc_status,
			 le32_to_cpu(mpi_reply->IOCLogInfo));
		rc = -EFAULT;
	}

 issue_host_reset:
	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);

 out:

	ioc->ctl_cmds.status = MPT3_CMD_NOT_USED;
	return rc;
}

/**
 * _ctl_addnl_diag_query - query relevant info associated with diag buffers
 * @ioc: per adapter object
 * @arg: user space buffer containing ioctl content
 *
 * The application will send only unique_id.  Driver will
 * inspect unique_id first, if valid, fill the details related to cause
 * for diag buffer release.
 */
static long
_ctl_addnl_diag_query(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	struct mpt3_addnl_diag_query karg;
	u32 buffer_type = 0;

	if (copy_from_user(&karg, arg, sizeof(karg))) {
		pr_err("%s: failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -EFAULT;
	}
	dctlprintk(ioc, ioc_info(ioc, "%s\n",  __func__));
	if (karg.unique_id == 0) {
		ioc_err(ioc, "%s: unique_id is(0x%08x)\n",
		    __func__, karg.unique_id);
		return -EPERM;
	}
	buffer_type = _ctl_diag_get_bufftype(ioc, karg.unique_id);
	if (buffer_type == MPT3_DIAG_UID_NOT_FOUND) {
		ioc_err(ioc, "%s: buffer with unique_id(0x%08x) not found\n",
		    __func__, karg.unique_id);
		return -EPERM;
	}
	memset(&karg.rel_query, 0, sizeof(karg.rel_query));
	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		ioc_info(ioc, "%s: buffer_type(0x%02x) is not registered\n",
		    __func__, buffer_type);
		goto out;
	}
	if ((ioc->diag_buffer_status[buffer_type] &
	    MPT3_DIAG_BUFFER_IS_RELEASED) == 0) {
		ioc_err(ioc, "%s: buffer_type(0x%02x) is not released\n",
		    __func__, buffer_type);
		return -EPERM;
	}
	memcpy(&karg.rel_query, &ioc->htb_rel, sizeof(karg.rel_query));
out:
	if (copy_to_user(arg, &karg, sizeof(struct mpt3_addnl_diag_query))) {
		ioc_err(ioc, "%s: unable to write mpt3_addnl_diag_query data @ %p\n",
		    __func__, arg);
		return -EFAULT;
	}
	return 0;
}

/**
 * _ctl_enable_diag_sbr_reload - enable sbr reload bit
 * @ioc: per adapter object
 * @arg: user space buffer containing ioctl content
 *
 * Enable the SBR reload bit
 */
static int
_ctl_enable_diag_sbr_reload(struct MPT3SAS_ADAPTER *ioc, void __user *arg)
{
	u32 ioc_state, host_diagnostic;

	if (ioc->shost_recovery ||
	    ioc->pci_error_recovery || ioc->is_driver_loading ||
	    ioc->remove_host)
		return -EAGAIN;

	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);

	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL)
		return -EFAULT;

	host_diagnostic = ioc->base_readl(&ioc->chip->HostDiagnostic);

	if (host_diagnostic & MPI2_DIAG_SBR_RELOAD)
		return 0;

	if (mutex_trylock(&ioc->hostdiag_unlock_mutex)) {
		if (mpt3sas_base_unlock_and_get_host_diagnostic(ioc, &host_diagnostic)) {
			mutex_unlock(&ioc->hostdiag_unlock_mutex);
				return -EFAULT;
		}
	} else
		return -EAGAIN;

	host_diagnostic |= MPI2_DIAG_SBR_RELOAD;
	writel(host_diagnostic, &ioc->chip->HostDiagnostic);
	host_diagnostic = ioc->base_readl(&ioc->chip->HostDiagnostic);
	mpt3sas_base_lock_host_diagnostic(ioc);
	mutex_unlock(&ioc->hostdiag_unlock_mutex);

	if (!(host_diagnostic & MPI2_DIAG_SBR_RELOAD)) {
		ioc_err(ioc, "%s: Failed to set Diag SBR Reload Bit\n", __func__);
		return -EFAULT;
	}

	ioc_info(ioc, "%s: Successfully set the Diag SBR Reload Bit\n", __func__);
	return 0;
}

#ifdef CONFIG_COMPAT
/**
 * _ctl_compat_mpt_command - convert 32bit pointers to 64bit.
 * @ioc: per adapter object
 * @cmd: ioctl opcode
 * @arg: (struct mpt3_ioctl_command32)
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
 * @file:  (struct file)
 * @cmd:  ioctl opcode
 * @arg:  user space data buffer
 * @compat:  handles 32 bit applications in 64bit os
 * @mpi_version: will be MPI2_VERSION for mpt2ctl ioctl device &
 * MPI25_VERSION | MPI26_VERSION for mpt3ctl ioctl device.
 */
static long
_ctl_ioctl_main(struct file *file, unsigned int cmd, void __user *arg,
	u8 compat, u16 mpi_version)
{
	struct MPT3SAS_ADAPTER *ioc;
	struct mpt3_ioctl_header ioctl_header;
	enum block_state state;
	long ret = -ENOIOCTLCMD;

	/* get IOCTL header */
	if (copy_from_user(&ioctl_header, (char __user *)arg,
	    sizeof(struct mpt3_ioctl_header))) {
		pr_err("failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	if (_ctl_verify_adapter(ioctl_header.ioc_number,
				&ioc, mpi_version) == -1 || !ioc)
		return -ENODEV;

	/* pci_access_mutex lock acquired by ioctl path */
	mutex_lock(&ioc->pci_access_mutex);

	if (ioc->shost_recovery || ioc->pci_error_recovery ||
	    ioc->is_driver_loading || ioc->remove_host) {
		ret = -EAGAIN;
		goto out_unlock_pciaccess;
	}

	state = (file->f_flags & O_NONBLOCK) ? NON_BLOCKING : BLOCKING;
	if (state == NON_BLOCKING) {
		if (!mutex_trylock(&ioc->ctl_cmds.mutex)) {
			ret = -EAGAIN;
			goto out_unlock_pciaccess;
		}
	} else if (mutex_lock_interruptible(&ioc->ctl_cmds.mutex)) {
		ret = -ERESTARTSYS;
		goto out_unlock_pciaccess;
	}


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

		if (karg.hdr.ioc_number != ioctl_header.ioc_number) {
			ret = -EINVAL;
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
	case MPT3ADDNLDIAGQUERY:
		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_addnl_diag_query))
			ret = _ctl_addnl_diag_query(ioc, arg);
		break;
	case MPT3ENABLEDIAGSBRRELOAD:
		if (_IOC_SIZE(cmd) == sizeof(struct mpt3_enable_diag_sbr_reload))
			ret = _ctl_enable_diag_sbr_reload(ioc, arg);
		break;
	default:
		dctlprintk(ioc,
			   ioc_info(ioc, "unsupported ioctl opcode(0x%08x)\n",
				    cmd));
		break;
	}

	mutex_unlock(&ioc->ctl_cmds.mutex);
out_unlock_pciaccess:
	mutex_unlock(&ioc->pci_access_mutex);
	return ret;
}

/**
 * _ctl_ioctl - mpt3ctl main ioctl entry point (unlocked)
 * @file: (struct file)
 * @cmd: ioctl opcode
 * @arg: ?
 */
static long
_ctl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret;

	/* pass MPI25_VERSION | MPI26_VERSION value,
	 * to indicate that this ioctl cmd
	 * came from mpt3ctl ioctl device.
	 */
	ret = _ctl_ioctl_main(file, cmd, (void __user *)arg, 0,
		MPI25_VERSION | MPI26_VERSION);
	return ret;
}

/**
 * _ctl_mpt2_ioctl - mpt2ctl main ioctl entry point (unlocked)
 * @file: (struct file)
 * @cmd: ioctl opcode
 * @arg: ?
 */
static long
_ctl_mpt2_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret;

	/* pass MPI2_VERSION value, to indicate that this ioctl cmd
	 * came from mpt2ctl ioctl device.
	 */
	ret = _ctl_ioctl_main(file, cmd, (void __user *)arg, 0, MPI2_VERSION);
	return ret;
}
#ifdef CONFIG_COMPAT
/**
 * _ctl_ioctl_compat - main ioctl entry point (compat)
 * @file: ?
 * @cmd: ?
 * @arg: ?
 *
 * This routine handles 32 bit applications in 64bit os.
 */
static long
_ctl_ioctl_compat(struct file *file, unsigned cmd, unsigned long arg)
{
	long ret;

	ret = _ctl_ioctl_main(file, cmd, (void __user *)arg, 1,
		MPI25_VERSION | MPI26_VERSION);
	return ret;
}

/**
 * _ctl_mpt2_ioctl_compat - main ioctl entry point (compat)
 * @file: ?
 * @cmd: ?
 * @arg: ?
 *
 * This routine handles 32 bit applications in 64bit os.
 */
static long
_ctl_mpt2_ioctl_compat(struct file *file, unsigned cmd, unsigned long arg)
{
	long ret;

	ret = _ctl_ioctl_main(file, cmd, (void __user *)arg, 1, MPI2_VERSION);
	return ret;
}
#endif

/* scsi host attributes */
/**
 * version_fw_show - firmware version
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
version_fw_show(struct device *cdev, struct device_attribute *attr,
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
static DEVICE_ATTR_RO(version_fw);

/**
 * version_bios_show - bios version
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
version_bios_show(struct device *cdev, struct device_attribute *attr,
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
static DEVICE_ATTR_RO(version_bios);

/**
 * version_mpi_show - MPI (message passing interface) version
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
version_mpi_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%03x.%02x\n",
	    ioc->facts.MsgVersion, ioc->facts.HeaderVersion >> 8);
}
static DEVICE_ATTR_RO(version_mpi);

/**
 * version_product_show - product name
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
version_product_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, 16, "%s\n", ioc->manu_pg0.ChipName);
}
static DEVICE_ATTR_RO(version_product);

/**
 * version_nvdata_persistent_show - ndvata persistent version
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
version_nvdata_persistent_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%08xh\n",
	    le32_to_cpu(ioc->iounit_pg0.NvdataVersionPersistent.Word));
}
static DEVICE_ATTR_RO(version_nvdata_persistent);

/**
 * version_nvdata_default_show - nvdata default version
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
version_nvdata_default_show(struct device *cdev, struct device_attribute
	*attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%08xh\n",
	    le32_to_cpu(ioc->iounit_pg0.NvdataVersionDefault.Word));
}
static DEVICE_ATTR_RO(version_nvdata_default);

/**
 * board_name_show - board name
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
board_name_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, 16, "%s\n", ioc->manu_pg0.BoardName);
}
static DEVICE_ATTR_RO(board_name);

/**
 * board_assembly_show - board assembly name
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
board_assembly_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, 16, "%s\n", ioc->manu_pg0.BoardAssembly);
}
static DEVICE_ATTR_RO(board_assembly);

/**
 * board_tracer_show - board tracer number
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
board_tracer_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, 16, "%s\n", ioc->manu_pg0.BoardTracerNumber);
}
static DEVICE_ATTR_RO(board_tracer);

/**
 * io_delay_show - io missing delay
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * This is for firmware implemention for deboucing device
 * removal events.
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
io_delay_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%02d\n", ioc->io_missing_delay);
}
static DEVICE_ATTR_RO(io_delay);

/**
 * device_delay_show - device missing delay
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * This is for firmware implemention for deboucing device
 * removal events.
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
device_delay_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%02d\n", ioc->device_missing_delay);
}
static DEVICE_ATTR_RO(device_delay);

/**
 * fw_queue_depth_show - global credits
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * This is firmware queue depth limit
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
fw_queue_depth_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%02d\n", ioc->facts.RequestCredit);
}
static DEVICE_ATTR_RO(fw_queue_depth);

/**
 * host_sas_address_show - sas address
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * This is the controller sas address
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
host_sas_address_show(struct device *cdev, struct device_attribute *attr,
	char *buf)

{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "0x%016llx\n",
	    (unsigned long long)ioc->sas_hba.sas_address);
}
static DEVICE_ATTR_RO(host_sas_address);

/**
 * logging_level_show - logging level
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
logging_level_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%08xh\n", ioc->logging_level);
}
static ssize_t
logging_level_store(struct device *cdev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	int val = 0;

	if (sscanf(buf, "%x", &val) != 1)
		return -EINVAL;

	ioc->logging_level = val;
	ioc_info(ioc, "logging_level=%08xh\n",
		 ioc->logging_level);
	return strlen(buf);
}
static DEVICE_ATTR_RW(logging_level);

/**
 * fwfault_debug_show - show/store fwfault_debug
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * mpt3sas_fwfault_debug is command line option
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
fwfault_debug_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%d\n", ioc->fwfault_debug);
}
static ssize_t
fwfault_debug_store(struct device *cdev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	int val = 0;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	ioc->fwfault_debug = val;
	ioc_info(ioc, "fwfault_debug=%d\n",
		 ioc->fwfault_debug);
	return strlen(buf);
}
static DEVICE_ATTR_RW(fwfault_debug);

/**
 * ioc_reset_count_show - ioc reset count
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * This is firmware queue depth limit
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
ioc_reset_count_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%d\n", ioc->ioc_reset_count);
}
static DEVICE_ATTR_RO(ioc_reset_count);

/**
 * reply_queue_count_show - number of reply queues
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * This is number of reply queues
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
reply_queue_count_show(struct device *cdev,
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
static DEVICE_ATTR_RO(reply_queue_count);

/**
 * BRM_status_show - Backup Rail Monitor Status
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * This is number of reply queues
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
BRM_status_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	Mpi2IOUnitPage3_t io_unit_pg3;
	Mpi2ConfigReply_t mpi_reply;
	u16 backup_rail_monitor_status = 0;
	u16 ioc_status;
	int sz;
	ssize_t rc = 0;

	if (!ioc->is_warpdrive) {
		ioc_err(ioc, "%s: BRM attribute is only for warpdrive\n",
			__func__);
		return 0;
	}
	/* pci_access_mutex lock acquired by sysfs show path */
	mutex_lock(&ioc->pci_access_mutex);
	if (ioc->pci_error_recovery || ioc->remove_host)
		goto out;

	sz = sizeof(io_unit_pg3);
	memset(&io_unit_pg3, 0, sz);

	if (mpt3sas_config_get_iounit_pg3(ioc, &mpi_reply, &io_unit_pg3, sz) !=
	    0) {
		ioc_err(ioc, "%s: failed reading iounit_pg3\n",
			__func__);
		rc = -EINVAL;
		goto out;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		ioc_err(ioc, "%s: iounit_pg3 failed with ioc_status(0x%04x)\n",
			__func__, ioc_status);
		rc = -EINVAL;
		goto out;
	}

	if (io_unit_pg3.GPIOCount < 25) {
		ioc_err(ioc, "%s: iounit_pg3.GPIOCount less than 25 entries, detected (%d) entries\n",
			__func__, io_unit_pg3.GPIOCount);
		rc = -EINVAL;
		goto out;
	}

	/* BRM status is in bit zero of GPIOVal[24] */
	backup_rail_monitor_status = le16_to_cpu(io_unit_pg3.GPIOVal[24]);
	rc = snprintf(buf, PAGE_SIZE, "%d\n", (backup_rail_monitor_status & 1));

 out:
	mutex_unlock(&ioc->pci_access_mutex);
	return rc;
}
static DEVICE_ATTR_RO(BRM_status);

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
 * host_trace_buffer_size_show - host buffer size (trace only)
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
host_trace_buffer_size_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	u32 size = 0;
	struct DIAG_BUFFER_START *request_data;

	if (!ioc->diag_buffer[MPI2_DIAG_BUF_TYPE_TRACE]) {
		ioc_err(ioc, "%s: host_trace_buffer is not registered\n",
			__func__);
		return 0;
	}

	if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		ioc_err(ioc, "%s: host_trace_buffer is not registered\n",
			__func__);
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
static DEVICE_ATTR_RO(host_trace_buffer_size);

/**
 * host_trace_buffer_show - firmware ring buffer (trace only)
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 *
 * You will only be able to read 4k bytes of ring buffer at a time.
 * In order to read beyond 4k bytes, you will have to write out the
 * offset to the same attribute, it will move the pointer.
 */
static ssize_t
host_trace_buffer_show(struct device *cdev, struct device_attribute *attr,
	char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	void *request_data;
	u32 size;

	if (!ioc->diag_buffer[MPI2_DIAG_BUF_TYPE_TRACE]) {
		ioc_err(ioc, "%s: host_trace_buffer is not registered\n",
			__func__);
		return 0;
	}

	if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		ioc_err(ioc, "%s: host_trace_buffer is not registered\n",
			__func__);
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
host_trace_buffer_store(struct device *cdev, struct device_attribute *attr,
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
static DEVICE_ATTR_RW(host_trace_buffer);


/*****************************************/

/**
 * host_trace_buffer_enable_show - firmware ring buffer (trace only)
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 *
 * This is a mechnism to post/release host_trace_buffers
 */
static ssize_t
host_trace_buffer_enable_show(struct device *cdev,
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
host_trace_buffer_enable_store(struct device *cdev,
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
		ioc_info(ioc, "posting host trace buffers\n");
		diag_register.buffer_type = MPI2_DIAG_BUF_TYPE_TRACE;

		if (ioc->manu_pg11.HostTraceBufferMaxSizeKB != 0 &&
		    ioc->diag_buffer_sz[MPI2_DIAG_BUF_TYPE_TRACE] != 0) {
			/* post the same buffer allocated previously */
			diag_register.requested_buffer_size =
			    ioc->diag_buffer_sz[MPI2_DIAG_BUF_TYPE_TRACE];
		} else {
			/*
			 * Free the diag buffer memory which was previously
			 * allocated by an application.
			 */
			if ((ioc->diag_buffer_sz[MPI2_DIAG_BUF_TYPE_TRACE] != 0)
			    &&
			    (ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
			    MPT3_DIAG_BUFFER_IS_APP_OWNED)) {
				dma_free_coherent(&ioc->pdev->dev,
						  ioc->diag_buffer_sz[MPI2_DIAG_BUF_TYPE_TRACE],
						  ioc->diag_buffer[MPI2_DIAG_BUF_TYPE_TRACE],
						  ioc->diag_buffer_dma[MPI2_DIAG_BUF_TYPE_TRACE]);
				ioc->diag_buffer[MPI2_DIAG_BUF_TYPE_TRACE] =
				    NULL;
			}

			diag_register.requested_buffer_size = (1024 * 1024);
		}

		diag_register.unique_id =
		    (ioc->hba_mpi_version_belonged == MPI2_VERSION) ?
		    (MPT2DIAGBUFFUNIQUEID):(MPT3DIAGBUFFUNIQUEID);
		ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] = 0;
		_ctl_diag_register_2(ioc,  &diag_register);
		if (ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
		    MPT3_DIAG_BUFFER_IS_REGISTERED) {
			ioc_info(ioc,
			    "Trace buffer %d KB allocated through sysfs\n",
			    diag_register.requested_buffer_size>>10);
			if (ioc->hba_mpi_version_belonged != MPI2_VERSION)
				ioc->diag_buffer_status[
				    MPI2_DIAG_BUF_TYPE_TRACE] |=
				    MPT3_DIAG_BUFFER_IS_DRIVER_ALLOCATED;
		}
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
		ioc_info(ioc, "releasing host trace buffer\n");
		ioc->htb_rel.buffer_rel_condition = MPT3_DIAG_BUFFER_REL_SYSFS;
		mpt3sas_send_diag_release(ioc, MPI2_DIAG_BUF_TYPE_TRACE,
		    &issue_reset);
	}

 out:
	return strlen(buf);
}
static DEVICE_ATTR_RW(host_trace_buffer_enable);

/*********** diagnostic trigger suppport *********************************/

/**
 * diag_trigger_master_show - show the diag_trigger_master attribute
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
diag_trigger_master_show(struct device *cdev,
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
 * diag_trigger_master_store - store the diag_trigger_master attribute
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 * @count: ?
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
diag_trigger_master_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)

{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	struct SL_WH_MASTER_TRIGGER_T *master_tg;
	unsigned long flags;
	ssize_t rc;
	bool set = 1;

	rc = min(sizeof(struct SL_WH_MASTER_TRIGGER_T), count);

	if (ioc->supports_trigger_pages) {
		master_tg = kzalloc(sizeof(struct SL_WH_MASTER_TRIGGER_T),
		    GFP_KERNEL);
		if (!master_tg)
			return -ENOMEM;

		memcpy(master_tg, buf, rc);
		if (!master_tg->MasterData)
			set = 0;
		if (mpt3sas_config_update_driver_trigger_pg1(ioc, master_tg,
		    set)) {
			kfree(master_tg);
			return -EFAULT;
		}
		kfree(master_tg);
	}

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);
	memset(&ioc->diag_trigger_master, 0,
	    sizeof(struct SL_WH_MASTER_TRIGGER_T));
	memcpy(&ioc->diag_trigger_master, buf, rc);
	ioc->diag_trigger_master.MasterData |=
	    (MASTER_TRIGGER_FW_FAULT + MASTER_TRIGGER_ADAPTER_RESET);
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
	return rc;
}
static DEVICE_ATTR_RW(diag_trigger_master);


/**
 * diag_trigger_event_show - show the diag_trigger_event attribute
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
diag_trigger_event_show(struct device *cdev,
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
 * diag_trigger_event_store - store the diag_trigger_event attribute
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 * @count: ?
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
diag_trigger_event_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)

{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	struct SL_WH_EVENT_TRIGGERS_T *event_tg;
	unsigned long flags;
	ssize_t sz;
	bool set = 1;

	sz = min(sizeof(struct SL_WH_EVENT_TRIGGERS_T), count);
	if (ioc->supports_trigger_pages) {
		event_tg = kzalloc(sizeof(struct SL_WH_EVENT_TRIGGERS_T),
		    GFP_KERNEL);
		if (!event_tg)
			return -ENOMEM;

		memcpy(event_tg, buf, sz);
		if (!event_tg->ValidEntries)
			set = 0;
		if (mpt3sas_config_update_driver_trigger_pg2(ioc, event_tg,
		    set)) {
			kfree(event_tg);
			return -EFAULT;
		}
		kfree(event_tg);
	}

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);

	memset(&ioc->diag_trigger_event, 0,
	    sizeof(struct SL_WH_EVENT_TRIGGERS_T));
	memcpy(&ioc->diag_trigger_event, buf, sz);
	if (ioc->diag_trigger_event.ValidEntries > NUM_VALID_ENTRIES)
		ioc->diag_trigger_event.ValidEntries = NUM_VALID_ENTRIES;
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
	return sz;
}
static DEVICE_ATTR_RW(diag_trigger_event);


/**
 * diag_trigger_scsi_show - show the diag_trigger_scsi attribute
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
diag_trigger_scsi_show(struct device *cdev,
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
 * diag_trigger_scsi_store - store the diag_trigger_scsi attribute
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 * @count: ?
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
diag_trigger_scsi_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	struct SL_WH_SCSI_TRIGGERS_T *scsi_tg;
	unsigned long flags;
	ssize_t sz;
	bool set = 1;

	sz = min(sizeof(struct SL_WH_SCSI_TRIGGERS_T), count);
	if (ioc->supports_trigger_pages) {
		scsi_tg = kzalloc(sizeof(struct SL_WH_SCSI_TRIGGERS_T),
		    GFP_KERNEL);
		if (!scsi_tg)
			return -ENOMEM;

		memcpy(scsi_tg, buf, sz);
		if (!scsi_tg->ValidEntries)
			set = 0;
		if (mpt3sas_config_update_driver_trigger_pg3(ioc, scsi_tg,
		    set)) {
			kfree(scsi_tg);
			return -EFAULT;
		}
		kfree(scsi_tg);
	}

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);

	memset(&ioc->diag_trigger_scsi, 0, sizeof(ioc->diag_trigger_scsi));
	memcpy(&ioc->diag_trigger_scsi, buf, sz);
	if (ioc->diag_trigger_scsi.ValidEntries > NUM_VALID_ENTRIES)
		ioc->diag_trigger_scsi.ValidEntries = NUM_VALID_ENTRIES;
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
	return sz;
}
static DEVICE_ATTR_RW(diag_trigger_scsi);


/**
 * diag_trigger_mpi_show - show the diag_trigger_mpi attribute
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
diag_trigger_mpi_show(struct device *cdev,
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
 * diag_trigger_mpi_store - store the diag_trigger_mpi attribute
 * @cdev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 * @count: ?
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t
diag_trigger_mpi_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	struct SL_WH_MPI_TRIGGERS_T *mpi_tg;
	unsigned long flags;
	ssize_t sz;
	bool set = 1;

	sz = min(sizeof(struct SL_WH_MPI_TRIGGERS_T), count);
	if (ioc->supports_trigger_pages) {
		mpi_tg = kzalloc(sizeof(struct SL_WH_MPI_TRIGGERS_T),
		    GFP_KERNEL);
		if (!mpi_tg)
			return -ENOMEM;

		memcpy(mpi_tg, buf, sz);
		if (!mpi_tg->ValidEntries)
			set = 0;
		if (mpt3sas_config_update_driver_trigger_pg4(ioc, mpi_tg,
		    set)) {
			kfree(mpi_tg);
			return -EFAULT;
		}
		kfree(mpi_tg);
	}

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);
	memset(&ioc->diag_trigger_mpi, 0,
	    sizeof(ioc->diag_trigger_mpi));
	memcpy(&ioc->diag_trigger_mpi, buf, sz);
	if (ioc->diag_trigger_mpi.ValidEntries > NUM_VALID_ENTRIES)
		ioc->diag_trigger_mpi.ValidEntries = NUM_VALID_ENTRIES;
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
	return sz;
}

static DEVICE_ATTR_RW(diag_trigger_mpi);

/*********** diagnostic trigger suppport *** END ****************************/

/*****************************************/

/**
 * drv_support_bitmap_show - driver supported feature bitmap
 * @cdev: pointer to embedded class device
 * @attr: unused
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
drv_support_bitmap_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "0x%08x\n", ioc->drv_support_bitmap);
}
static DEVICE_ATTR_RO(drv_support_bitmap);

/**
 * enable_sdev_max_qd_show - display whether sdev max qd is enabled/disabled
 * @cdev: pointer to embedded class device
 * @attr: unused
 * @buf: the buffer returned
 *
 * A sysfs read/write shost attribute. This attribute is used to set the
 * targets queue depth to HBA IO queue depth if this attribute is enabled.
 */
static ssize_t
enable_sdev_max_qd_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%d\n", ioc->enable_sdev_max_qd);
}

/**
 * enable_sdev_max_qd_store - Enable/disable sdev max qd
 * @cdev: pointer to embedded class device
 * @attr: unused
 * @buf: the buffer returned
 * @count: unused
 *
 * A sysfs read/write shost attribute. This attribute is used to set the
 * targets queue depth to HBA IO queue depth if this attribute is enabled.
 * If this attribute is disabled then targets will have corresponding default
 * queue depth.
 */
static ssize_t
enable_sdev_max_qd_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	int val = 0;
	struct scsi_device *sdev;
	struct _raid_device *raid_device;
	int qdepth;

	if (kstrtoint(buf, 0, &val) != 0)
		return -EINVAL;

	switch (val) {
	case 0:
		ioc->enable_sdev_max_qd = 0;
		shost_for_each_device(sdev, ioc->shost) {
			sas_device_priv_data = sdev->hostdata;
			if (!sas_device_priv_data)
				continue;
			sas_target_priv_data = sas_device_priv_data->sas_target;
			if (!sas_target_priv_data)
				continue;

			if (sas_target_priv_data->flags &
			    MPT_TARGET_FLAGS_VOLUME) {
				raid_device =
				    mpt3sas_raid_device_find_by_handle(ioc,
				    sas_target_priv_data->handle);

				switch (raid_device->volume_type) {
				case MPI2_RAID_VOL_TYPE_RAID0:
					if (raid_device->device_info &
					    MPI2_SAS_DEVICE_INFO_SSP_TARGET)
						qdepth =
						    MPT3SAS_SAS_QUEUE_DEPTH;
					else
						qdepth =
						    MPT3SAS_SATA_QUEUE_DEPTH;
					break;
				case MPI2_RAID_VOL_TYPE_RAID1E:
				case MPI2_RAID_VOL_TYPE_RAID1:
				case MPI2_RAID_VOL_TYPE_RAID10:
				case MPI2_RAID_VOL_TYPE_UNKNOWN:
				default:
					qdepth = MPT3SAS_RAID_QUEUE_DEPTH;
				}
			} else if (sas_target_priv_data->flags &
			    MPT_TARGET_FLAGS_PCIE_DEVICE)
				qdepth = ioc->max_nvme_qd;
			else
				qdepth = (sas_target_priv_data->sas_dev->port_type > 1) ?
				    ioc->max_wideport_qd : ioc->max_narrowport_qd;

			mpt3sas_scsih_change_queue_depth(sdev, qdepth);
		}
		break;
	case 1:
		ioc->enable_sdev_max_qd = 1;
		shost_for_each_device(sdev, ioc->shost)
			mpt3sas_scsih_change_queue_depth(sdev,
			    shost->can_queue);
		break;
	default:
		return -EINVAL;
	}

	return strlen(buf);
}
static DEVICE_ATTR_RW(enable_sdev_max_qd);

static struct attribute *mpt3sas_host_attrs[] = {
	&dev_attr_version_fw.attr,
	&dev_attr_version_bios.attr,
	&dev_attr_version_mpi.attr,
	&dev_attr_version_product.attr,
	&dev_attr_version_nvdata_persistent.attr,
	&dev_attr_version_nvdata_default.attr,
	&dev_attr_board_name.attr,
	&dev_attr_board_assembly.attr,
	&dev_attr_board_tracer.attr,
	&dev_attr_io_delay.attr,
	&dev_attr_device_delay.attr,
	&dev_attr_logging_level.attr,
	&dev_attr_fwfault_debug.attr,
	&dev_attr_fw_queue_depth.attr,
	&dev_attr_host_sas_address.attr,
	&dev_attr_ioc_reset_count.attr,
	&dev_attr_host_trace_buffer_size.attr,
	&dev_attr_host_trace_buffer.attr,
	&dev_attr_host_trace_buffer_enable.attr,
	&dev_attr_reply_queue_count.attr,
	&dev_attr_diag_trigger_master.attr,
	&dev_attr_diag_trigger_event.attr,
	&dev_attr_diag_trigger_scsi.attr,
	&dev_attr_diag_trigger_mpi.attr,
	&dev_attr_drv_support_bitmap.attr,
	&dev_attr_BRM_status.attr,
	&dev_attr_enable_sdev_max_qd.attr,
	NULL,
};

static const struct attribute_group mpt3sas_host_attr_group = {
	.attrs = mpt3sas_host_attrs
};

const struct attribute_group *mpt3sas_host_groups[] = {
	&mpt3sas_host_attr_group,
	NULL
};

/* device attributes */

/**
 * sas_address_show - sas address
 * @dev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * This is the sas address for the target
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
sas_address_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT3SAS_DEVICE *sas_device_priv_data = sdev->hostdata;

	return snprintf(buf, PAGE_SIZE, "0x%016llx\n",
	    (unsigned long long)sas_device_priv_data->sas_target->sas_address);
}
static DEVICE_ATTR_RO(sas_address);

/**
 * sas_device_handle_show - device handle
 * @dev: pointer to embedded class device
 * @attr: ?
 * @buf: the buffer returned
 *
 * This is the firmware assigned device handle
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t
sas_device_handle_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT3SAS_DEVICE *sas_device_priv_data = sdev->hostdata;

	return snprintf(buf, PAGE_SIZE, "0x%04x\n",
	    sas_device_priv_data->sas_target->handle);
}
static DEVICE_ATTR_RO(sas_device_handle);

/**
 * sas_ncq_prio_supported_show - Indicate if device supports NCQ priority
 * @dev: pointer to embedded device
 * @attr: sas_ncq_prio_supported attribute descriptor
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' sdev attribute, only works with SATA
 */
static ssize_t
sas_ncq_prio_supported_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	return sysfs_emit(buf, "%d\n", scsih_ncq_prio_supp(sdev));
}
static DEVICE_ATTR_RO(sas_ncq_prio_supported);

/**
 * sas_ncq_prio_enable_show - send prioritized io commands to device
 * @dev: pointer to embedded device
 * @attr: ?
 * @buf: the buffer returned
 *
 * A sysfs 'read/write' sdev attribute, only works with SATA
 */
static ssize_t
sas_ncq_prio_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT3SAS_DEVICE *sas_device_priv_data = sdev->hostdata;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			sas_device_priv_data->ncq_prio_enable);
}

static ssize_t
sas_ncq_prio_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT3SAS_DEVICE *sas_device_priv_data = sdev->hostdata;
	bool ncq_prio_enable = 0;

	if (kstrtobool(buf, &ncq_prio_enable))
		return -EINVAL;

	if (!scsih_ncq_prio_supp(sdev))
		return -EINVAL;

	sas_device_priv_data->ncq_prio_enable = ncq_prio_enable;
	return strlen(buf);
}
static DEVICE_ATTR_RW(sas_ncq_prio_enable);

static struct attribute *mpt3sas_dev_attrs[] = {
	&dev_attr_sas_address.attr,
	&dev_attr_sas_device_handle.attr,
	&dev_attr_sas_ncq_prio_supported.attr,
	&dev_attr_sas_ncq_prio_enable.attr,
	NULL,
};

static const struct attribute_group mpt3sas_dev_attr_group = {
	.attrs = mpt3sas_dev_attrs
};

const struct attribute_group *mpt3sas_dev_groups[] = {
	&mpt3sas_dev_attr_group,
	NULL
};

/* file operations table for mpt3ctl device */
static const struct file_operations ctl_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = _ctl_ioctl,
	.poll = _ctl_poll,
	.fasync = _ctl_fasync,
#ifdef CONFIG_COMPAT
	.compat_ioctl = _ctl_ioctl_compat,
#endif
};

/* file operations table for mpt2ctl device */
static const struct file_operations ctl_gen2_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = _ctl_mpt2_ioctl,
	.poll = _ctl_poll,
	.fasync = _ctl_fasync,
#ifdef CONFIG_COMPAT
	.compat_ioctl = _ctl_mpt2_ioctl_compat,
#endif
};

static struct miscdevice ctl_dev = {
	.minor  = MPT3SAS_MINOR,
	.name   = MPT3SAS_DEV_NAME,
	.fops   = &ctl_fops,
};

static struct miscdevice gen2_ctl_dev = {
	.minor  = MPT2SAS_MINOR,
	.name   = MPT2SAS_DEV_NAME,
	.fops   = &ctl_gen2_fops,
};

/**
 * mpt3sas_ctl_init - main entry point for ctl.
 * @hbas_to_enumerate: ?
 */
void
mpt3sas_ctl_init(ushort hbas_to_enumerate)
{
	async_queue = NULL;

	/* Don't register mpt3ctl ioctl device if
	 * hbas_to_enumarate is one.
	 */
	if (hbas_to_enumerate != 1)
		if (misc_register(&ctl_dev) < 0)
			pr_err("%s can't register misc device [minor=%d]\n",
			    MPT3SAS_DRIVER_NAME, MPT3SAS_MINOR);

	/* Don't register mpt3ctl ioctl device if
	 * hbas_to_enumarate is two.
	 */
	if (hbas_to_enumerate != 2)
		if (misc_register(&gen2_ctl_dev) < 0)
			pr_err("%s can't register misc device [minor=%d]\n",
			    MPT2SAS_DRIVER_NAME, MPT2SAS_MINOR);

	init_waitqueue_head(&ctl_poll_wait);
}

/**
 * mpt3sas_ctl_release - release dma for ctl
 * @ioc: per adapter object
 */
void
mpt3sas_ctl_release(struct MPT3SAS_ADAPTER *ioc)
{
	int i;

	/* free memory associated to diag buffers */
	for (i = 0; i < MPI2_DIAG_BUF_TYPE_COUNT; i++) {
		if (!ioc->diag_buffer[i])
			continue;
		dma_free_coherent(&ioc->pdev->dev,
				  ioc->diag_buffer_sz[i],
				  ioc->diag_buffer[i],
				  ioc->diag_buffer_dma[i]);
		ioc->diag_buffer[i] = NULL;
		ioc->diag_buffer_status[i] = 0;
	}

	kfree(ioc->event_log);
}

/**
 * mpt3sas_ctl_exit - exit point for ctl
 * @hbas_to_enumerate: ?
 */
void
mpt3sas_ctl_exit(ushort hbas_to_enumerate)
{

	if (hbas_to_enumerate != 1)
		misc_deregister(&ctl_dev);
	if (hbas_to_enumerate != 2)
		misc_deregister(&gen2_ctl_dev);
}
