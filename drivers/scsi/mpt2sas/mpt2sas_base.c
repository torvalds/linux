/*
 * This is the Fusion MPT base driver providing common API layer interface
 * for access to MPT (Message Passing Technology) firmware.
 *
 * This code is based on drivers/scsi/mpt2sas/mpt2_base.c
 * Copyright (C) 2007-2010  LSI Corporation
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

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/sort.h>
#include <linux/io.h>
#include <linux/time.h>
#include <linux/aer.h>

#include "mpt2sas_base.h"

static MPT_CALLBACK	mpt_callbacks[MPT_MAX_CALLBACKS];

#define FAULT_POLLING_INTERVAL 1000 /* in milliseconds */

static int max_queue_depth = -1;
module_param(max_queue_depth, int, 0);
MODULE_PARM_DESC(max_queue_depth, " max controller queue depth ");

static int max_sgl_entries = -1;
module_param(max_sgl_entries, int, 0);
MODULE_PARM_DESC(max_sgl_entries, " max sg entries ");

static int msix_disable = -1;
module_param(msix_disable, int, 0);
MODULE_PARM_DESC(msix_disable, " disable msix routed interrupts (default=0)");

static int missing_delay[2] = {-1, -1};
module_param_array(missing_delay, int, NULL, 0);
MODULE_PARM_DESC(missing_delay, " device missing delay , io missing delay");

/* diag_buffer_enable is bitwise
 * bit 0 set = TRACE
 * bit 1 set = SNAPSHOT
 * bit 2 set = EXTENDED
 *
 * Either bit can be set, or both
 */
static int diag_buffer_enable;
module_param(diag_buffer_enable, int, 0);
MODULE_PARM_DESC(diag_buffer_enable, " post diag buffers "
    "(TRACE=1/SNAPSHOT=2/EXTENDED=4/default=0)");

int mpt2sas_fwfault_debug;
MODULE_PARM_DESC(mpt2sas_fwfault_debug, " enable detection of firmware fault "
    "and halt firmware - (default=0)");

static int disable_discovery = -1;
module_param(disable_discovery, int, 0);
MODULE_PARM_DESC(disable_discovery, " disable discovery ");

/**
 * _scsih_set_fwfault_debug - global setting of ioc->fwfault_debug.
 *
 */
static int
_scsih_set_fwfault_debug(const char *val, struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);
	struct MPT2SAS_ADAPTER *ioc;

	if (ret)
		return ret;

	printk(KERN_INFO "setting fwfault_debug(%d)\n", mpt2sas_fwfault_debug);
	list_for_each_entry(ioc, &mpt2sas_ioc_list, list)
		ioc->fwfault_debug = mpt2sas_fwfault_debug;
	return 0;
}
module_param_call(mpt2sas_fwfault_debug, _scsih_set_fwfault_debug,
    param_get_int, &mpt2sas_fwfault_debug, 0644);

/**
 * _base_fault_reset_work - workq handling ioc fault conditions
 * @work: input argument, used to derive ioc
 * Context: sleep.
 *
 * Return nothing.
 */
static void
_base_fault_reset_work(struct work_struct *work)
{
	struct MPT2SAS_ADAPTER *ioc =
	    container_of(work, struct MPT2SAS_ADAPTER, fault_reset_work.work);
	unsigned long	 flags;
	u32 doorbell;
	int rc;

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	if (ioc->shost_recovery)
		goto rearm_timer;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);

	doorbell = mpt2sas_base_get_iocstate(ioc, 0);
	if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		rc = mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP,
		    FORCE_BIG_HAMMER);
		printk(MPT2SAS_WARN_FMT "%s: hard reset: %s\n", ioc->name,
		    __func__, (rc == 0) ? "success" : "failed");
		doorbell = mpt2sas_base_get_iocstate(ioc, 0);
		if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT)
			mpt2sas_base_fault_info(ioc, doorbell &
			    MPI2_DOORBELL_DATA_MASK);
	}

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
 rearm_timer:
	if (ioc->fault_reset_work_q)
		queue_delayed_work(ioc->fault_reset_work_q,
		    &ioc->fault_reset_work,
		    msecs_to_jiffies(FAULT_POLLING_INTERVAL));
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
}

/**
 * mpt2sas_base_start_watchdog - start the fault_reset_work_q
 * @ioc: per adapter object
 * Context: sleep.
 *
 * Return nothing.
 */
void
mpt2sas_base_start_watchdog(struct MPT2SAS_ADAPTER *ioc)
{
	unsigned long	 flags;

	if (ioc->fault_reset_work_q)
		return;

	/* initialize fault polling */
	INIT_DELAYED_WORK(&ioc->fault_reset_work, _base_fault_reset_work);
	snprintf(ioc->fault_reset_work_q_name,
	    sizeof(ioc->fault_reset_work_q_name), "poll_%d_status", ioc->id);
	ioc->fault_reset_work_q =
		create_singlethread_workqueue(ioc->fault_reset_work_q_name);
	if (!ioc->fault_reset_work_q) {
		printk(MPT2SAS_ERR_FMT "%s: failed (line=%d)\n",
		    ioc->name, __func__, __LINE__);
			return;
	}
	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	if (ioc->fault_reset_work_q)
		queue_delayed_work(ioc->fault_reset_work_q,
		    &ioc->fault_reset_work,
		    msecs_to_jiffies(FAULT_POLLING_INTERVAL));
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
}

/**
 * mpt2sas_base_stop_watchdog - stop the fault_reset_work_q
 * @ioc: per adapter object
 * Context: sleep.
 *
 * Return nothing.
 */
void
mpt2sas_base_stop_watchdog(struct MPT2SAS_ADAPTER *ioc)
{
	unsigned long	 flags;
	struct workqueue_struct *wq;

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	wq = ioc->fault_reset_work_q;
	ioc->fault_reset_work_q = NULL;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
	if (wq) {
		if (!cancel_delayed_work(&ioc->fault_reset_work))
			flush_workqueue(wq);
		destroy_workqueue(wq);
	}
}

/**
 * mpt2sas_base_fault_info - verbose translation of firmware FAULT code
 * @ioc: per adapter object
 * @fault_code: fault code
 *
 * Return nothing.
 */
void
mpt2sas_base_fault_info(struct MPT2SAS_ADAPTER *ioc , u16 fault_code)
{
	printk(MPT2SAS_ERR_FMT "fault_state(0x%04x)!\n",
	    ioc->name, fault_code);
}

/**
 * mpt2sas_halt_firmware - halt's mpt controller firmware
 * @ioc: per adapter object
 *
 * For debugging timeout related issues.  Writing 0xCOFFEE00
 * to the doorbell register will halt controller firmware. With
 * the purpose to stop both driver and firmware, the enduser can
 * obtain a ring buffer from controller UART.
 */
void
mpt2sas_halt_firmware(struct MPT2SAS_ADAPTER *ioc)
{
	u32 doorbell;

	if (!ioc->fwfault_debug)
		return;

	dump_stack();

	doorbell = readl(&ioc->chip->Doorbell);
	if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT)
		mpt2sas_base_fault_info(ioc , doorbell);
	else {
		writel(0xC0FFEE00, &ioc->chip->Doorbell);
		printk(MPT2SAS_ERR_FMT "Firmware is halted due to command "
		    "timeout\n", ioc->name);
	}

	panic("panic in %s\n", __func__);
}

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _base_sas_ioc_info - verbose translation of the ioc status
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @request_hdr: request mf
 *
 * Return nothing.
 */
static void
_base_sas_ioc_info(struct MPT2SAS_ADAPTER *ioc, MPI2DefaultReply_t *mpi_reply,
     MPI2RequestHeader_t *request_hdr)
{
	u16 ioc_status = le16_to_cpu(mpi_reply->IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	char *desc = NULL;
	u16 frame_sz;
	char *func_str = NULL;

	/* SCSI_IO, RAID_PASS are handled from _scsih_scsi_ioc_info */
	if (request_hdr->Function == MPI2_FUNCTION_SCSI_IO_REQUEST ||
	    request_hdr->Function == MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH ||
	    request_hdr->Function == MPI2_FUNCTION_EVENT_NOTIFICATION)
		return;

	if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
		return;

	switch (ioc_status) {

/****************************************************************************
*  Common IOCStatus values for all replies
****************************************************************************/

	case MPI2_IOCSTATUS_INVALID_FUNCTION:
		desc = "invalid function";
		break;
	case MPI2_IOCSTATUS_BUSY:
		desc = "busy";
		break;
	case MPI2_IOCSTATUS_INVALID_SGL:
		desc = "invalid sgl";
		break;
	case MPI2_IOCSTATUS_INTERNAL_ERROR:
		desc = "internal error";
		break;
	case MPI2_IOCSTATUS_INVALID_VPID:
		desc = "invalid vpid";
		break;
	case MPI2_IOCSTATUS_INSUFFICIENT_RESOURCES:
		desc = "insufficient resources";
		break;
	case MPI2_IOCSTATUS_INVALID_FIELD:
		desc = "invalid field";
		break;
	case MPI2_IOCSTATUS_INVALID_STATE:
		desc = "invalid state";
		break;
	case MPI2_IOCSTATUS_OP_STATE_NOT_SUPPORTED:
		desc = "op state not supported";
		break;

/****************************************************************************
*  Config IOCStatus values
****************************************************************************/

	case MPI2_IOCSTATUS_CONFIG_INVALID_ACTION:
		desc = "config invalid action";
		break;
	case MPI2_IOCSTATUS_CONFIG_INVALID_TYPE:
		desc = "config invalid type";
		break;
	case MPI2_IOCSTATUS_CONFIG_INVALID_PAGE:
		desc = "config invalid page";
		break;
	case MPI2_IOCSTATUS_CONFIG_INVALID_DATA:
		desc = "config invalid data";
		break;
	case MPI2_IOCSTATUS_CONFIG_NO_DEFAULTS:
		desc = "config no defaults";
		break;
	case MPI2_IOCSTATUS_CONFIG_CANT_COMMIT:
		desc = "config cant commit";
		break;

/****************************************************************************
*  SCSI IO Reply
****************************************************************************/

	case MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR:
	case MPI2_IOCSTATUS_SCSI_INVALID_DEVHANDLE:
	case MPI2_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
	case MPI2_IOCSTATUS_SCSI_DATA_OVERRUN:
	case MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN:
	case MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR:
	case MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR:
	case MPI2_IOCSTATUS_SCSI_TASK_TERMINATED:
	case MPI2_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
	case MPI2_IOCSTATUS_SCSI_TASK_MGMT_FAILED:
	case MPI2_IOCSTATUS_SCSI_IOC_TERMINATED:
	case MPI2_IOCSTATUS_SCSI_EXT_TERMINATED:
		break;

/****************************************************************************
*  For use by SCSI Initiator and SCSI Target end-to-end data protection
****************************************************************************/

	case MPI2_IOCSTATUS_EEDP_GUARD_ERROR:
		desc = "eedp guard error";
		break;
	case MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR:
		desc = "eedp ref tag error";
		break;
	case MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR:
		desc = "eedp app tag error";
		break;

/****************************************************************************
*  SCSI Target values
****************************************************************************/

	case MPI2_IOCSTATUS_TARGET_INVALID_IO_INDEX:
		desc = "target invalid io index";
		break;
	case MPI2_IOCSTATUS_TARGET_ABORTED:
		desc = "target aborted";
		break;
	case MPI2_IOCSTATUS_TARGET_NO_CONN_RETRYABLE:
		desc = "target no conn retryable";
		break;
	case MPI2_IOCSTATUS_TARGET_NO_CONNECTION:
		desc = "target no connection";
		break;
	case MPI2_IOCSTATUS_TARGET_XFER_COUNT_MISMATCH:
		desc = "target xfer count mismatch";
		break;
	case MPI2_IOCSTATUS_TARGET_DATA_OFFSET_ERROR:
		desc = "target data offset error";
		break;
	case MPI2_IOCSTATUS_TARGET_TOO_MUCH_WRITE_DATA:
		desc = "target too much write data";
		break;
	case MPI2_IOCSTATUS_TARGET_IU_TOO_SHORT:
		desc = "target iu too short";
		break;
	case MPI2_IOCSTATUS_TARGET_ACK_NAK_TIMEOUT:
		desc = "target ack nak timeout";
		break;
	case MPI2_IOCSTATUS_TARGET_NAK_RECEIVED:
		desc = "target nak received";
		break;

/****************************************************************************
*  Serial Attached SCSI values
****************************************************************************/

	case MPI2_IOCSTATUS_SAS_SMP_REQUEST_FAILED:
		desc = "smp request failed";
		break;
	case MPI2_IOCSTATUS_SAS_SMP_DATA_OVERRUN:
		desc = "smp data overrun";
		break;

/****************************************************************************
*  Diagnostic Buffer Post / Diagnostic Release values
****************************************************************************/

	case MPI2_IOCSTATUS_DIAGNOSTIC_RELEASED:
		desc = "diagnostic released";
		break;
	default:
		break;
	}

	if (!desc)
		return;

	switch (request_hdr->Function) {
	case MPI2_FUNCTION_CONFIG:
		frame_sz = sizeof(Mpi2ConfigRequest_t) + ioc->sge_size;
		func_str = "config_page";
		break;
	case MPI2_FUNCTION_SCSI_TASK_MGMT:
		frame_sz = sizeof(Mpi2SCSITaskManagementRequest_t);
		func_str = "task_mgmt";
		break;
	case MPI2_FUNCTION_SAS_IO_UNIT_CONTROL:
		frame_sz = sizeof(Mpi2SasIoUnitControlRequest_t);
		func_str = "sas_iounit_ctl";
		break;
	case MPI2_FUNCTION_SCSI_ENCLOSURE_PROCESSOR:
		frame_sz = sizeof(Mpi2SepRequest_t);
		func_str = "enclosure";
		break;
	case MPI2_FUNCTION_IOC_INIT:
		frame_sz = sizeof(Mpi2IOCInitRequest_t);
		func_str = "ioc_init";
		break;
	case MPI2_FUNCTION_PORT_ENABLE:
		frame_sz = sizeof(Mpi2PortEnableRequest_t);
		func_str = "port_enable";
		break;
	case MPI2_FUNCTION_SMP_PASSTHROUGH:
		frame_sz = sizeof(Mpi2SmpPassthroughRequest_t) + ioc->sge_size;
		func_str = "smp_passthru";
		break;
	default:
		frame_sz = 32;
		func_str = "unknown";
		break;
	}

	printk(MPT2SAS_WARN_FMT "ioc_status: %s(0x%04x), request(0x%p),"
	    " (%s)\n", ioc->name, desc, ioc_status, request_hdr, func_str);

	_debug_dump_mf(request_hdr, frame_sz/4);
}

/**
 * _base_display_event_data - verbose translation of firmware asyn events
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 *
 * Return nothing.
 */
static void
_base_display_event_data(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventNotificationReply_t *mpi_reply)
{
	char *desc = NULL;
	u16 event;

	if (!(ioc->logging_level & MPT_DEBUG_EVENTS))
		return;

	event = le16_to_cpu(mpi_reply->Event);

	switch (event) {
	case MPI2_EVENT_LOG_DATA:
		desc = "Log Data";
		break;
	case MPI2_EVENT_STATE_CHANGE:
		desc = "Status Change";
		break;
	case MPI2_EVENT_HARD_RESET_RECEIVED:
		desc = "Hard Reset Received";
		break;
	case MPI2_EVENT_EVENT_CHANGE:
		desc = "Event Change";
		break;
	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
		desc = "Device Status Change";
		break;
	case MPI2_EVENT_IR_OPERATION_STATUS:
		desc = "IR Operation Status";
		break;
	case MPI2_EVENT_SAS_DISCOVERY:
	{
		Mpi2EventDataSasDiscovery_t *event_data =
		    (Mpi2EventDataSasDiscovery_t *)mpi_reply->EventData;
		printk(MPT2SAS_INFO_FMT "Discovery: (%s)", ioc->name,
		    (event_data->ReasonCode == MPI2_EVENT_SAS_DISC_RC_STARTED) ?
		    "start" : "stop");
		if (event_data->DiscoveryStatus)
			printk("discovery_status(0x%08x)",
			    le32_to_cpu(event_data->DiscoveryStatus));
		printk("\n");
		return;
	}
	case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
		desc = "SAS Broadcast Primitive";
		break;
	case MPI2_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE:
		desc = "SAS Init Device Status Change";
		break;
	case MPI2_EVENT_SAS_INIT_TABLE_OVERFLOW:
		desc = "SAS Init Table Overflow";
		break;
	case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		desc = "SAS Topology Change List";
		break;
	case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
		desc = "SAS Enclosure Device Status Change";
		break;
	case MPI2_EVENT_IR_VOLUME:
		desc = "IR Volume";
		break;
	case MPI2_EVENT_IR_PHYSICAL_DISK:
		desc = "IR Physical Disk";
		break;
	case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
		desc = "IR Configuration Change List";
		break;
	case MPI2_EVENT_LOG_ENTRY_ADDED:
		desc = "Log Entry Added";
		break;
	}

	if (!desc)
		return;

	printk(MPT2SAS_INFO_FMT "%s\n", ioc->name, desc);
}
#endif

/**
 * _base_sas_log_info - verbose translation of firmware log info
 * @ioc: per adapter object
 * @log_info: log info
 *
 * Return nothing.
 */
static void
_base_sas_log_info(struct MPT2SAS_ADAPTER *ioc , u32 log_info)
{
	union loginfo_type {
		u32	loginfo;
		struct {
			u32	subcode:16;
			u32	code:8;
			u32	originator:4;
			u32	bus_type:4;
		} dw;
	};
	union loginfo_type sas_loginfo;
	char *originator_str = NULL;

	sas_loginfo.loginfo = log_info;
	if (sas_loginfo.dw.bus_type != 3 /*SAS*/)
		return;

	/* each nexus loss loginfo */
	if (log_info == 0x31170000)
		return;

	/* eat the loginfos associated with task aborts */
	if (ioc->ignore_loginfos && (log_info == 30050000 || log_info ==
	    0x31140000 || log_info == 0x31130000))
		return;

	switch (sas_loginfo.dw.originator) {
	case 0:
		originator_str = "IOP";
		break;
	case 1:
		originator_str = "PL";
		break;
	case 2:
		originator_str = "IR";
		break;
	}

	printk(MPT2SAS_WARN_FMT "log_info(0x%08x): originator(%s), "
	    "code(0x%02x), sub_code(0x%04x)\n", ioc->name, log_info,
	     originator_str, sas_loginfo.dw.code,
	     sas_loginfo.dw.subcode);
}

/**
 * _base_display_reply_info -
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Return nothing.
 */
static void
_base_display_reply_info(struct MPT2SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
    u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;
	u16 ioc_status;

	mpi_reply = mpt2sas_base_get_reply_virt_addr(ioc, reply);
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus);
#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if ((ioc_status & MPI2_IOCSTATUS_MASK) &&
	    (ioc->logging_level & MPT_DEBUG_REPLY)) {
		_base_sas_ioc_info(ioc , mpi_reply,
		   mpt2sas_base_get_msg_frame(ioc, smid));
	}
#endif
	if (ioc_status & MPI2_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE)
		_base_sas_log_info(ioc, le32_to_cpu(mpi_reply->IOCLogInfo));
}

/**
 * mpt2sas_base_done - base internal command completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
u8
mpt2sas_base_done(struct MPT2SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
    u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	mpi_reply = mpt2sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply && mpi_reply->Function == MPI2_FUNCTION_EVENT_ACK)
		return 1;

	if (ioc->base_cmds.status == MPT2_CMD_NOT_USED)
		return 1;

	ioc->base_cmds.status |= MPT2_CMD_COMPLETE;
	if (mpi_reply) {
		ioc->base_cmds.status |= MPT2_CMD_REPLY_VALID;
		memcpy(ioc->base_cmds.reply, mpi_reply, mpi_reply->MsgLength*4);
	}
	ioc->base_cmds.status &= ~MPT2_CMD_PENDING;
	complete(&ioc->base_cmds.done);
	return 1;
}

/**
 * _base_async_event - main callback handler for firmware asyn events
 * @ioc: per adapter object
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_base_async_event(struct MPT2SAS_ADAPTER *ioc, u8 msix_index, u32 reply)
{
	Mpi2EventNotificationReply_t *mpi_reply;
	Mpi2EventAckRequest_t *ack_request;
	u16 smid;

	mpi_reply = mpt2sas_base_get_reply_virt_addr(ioc, reply);
	if (!mpi_reply)
		return 1;
	if (mpi_reply->Function != MPI2_FUNCTION_EVENT_NOTIFICATION)
		return 1;
#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	_base_display_event_data(ioc, mpi_reply);
#endif
	if (!(mpi_reply->AckRequired & MPI2_EVENT_NOTIFICATION_ACK_REQUIRED))
		goto out;
	smid = mpt2sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		goto out;
	}

	ack_request = mpt2sas_base_get_msg_frame(ioc, smid);
	memset(ack_request, 0, sizeof(Mpi2EventAckRequest_t));
	ack_request->Function = MPI2_FUNCTION_EVENT_ACK;
	ack_request->Event = mpi_reply->Event;
	ack_request->EventContext = mpi_reply->EventContext;
	ack_request->VF_ID = 0;  /* TODO */
	ack_request->VP_ID = 0;
	mpt2sas_base_put_smid_default(ioc, smid);

 out:

	/* scsih callback handler */
	mpt2sas_scsih_event_callback(ioc, msix_index, reply);

	/* ctl callback handler */
	mpt2sas_ctl_event_callback(ioc, msix_index, reply);

	return 1;
}

/**
 * _base_get_cb_idx - obtain the callback index
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return callback index.
 */
static u8
_base_get_cb_idx(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	int i;
	u8 cb_idx = 0xFF;

	if (smid >= ioc->hi_priority_smid) {
		if (smid < ioc->internal_smid) {
			i = smid - ioc->hi_priority_smid;
			cb_idx = ioc->hpr_lookup[i].cb_idx;
		} else if (smid <= ioc->hba_queue_depth)  {
			i = smid - ioc->internal_smid;
			cb_idx = ioc->internal_lookup[i].cb_idx;
		}
	} else {
		i = smid - 1;
		cb_idx = ioc->scsi_lookup[i].cb_idx;
	}
	return cb_idx;
}

/**
 * _base_mask_interrupts - disable interrupts
 * @ioc: per adapter object
 *
 * Disabling ResetIRQ, Reply and Doorbell Interrupts
 *
 * Return nothing.
 */
static void
_base_mask_interrupts(struct MPT2SAS_ADAPTER *ioc)
{
	u32 him_register;

	ioc->mask_interrupts = 1;
	him_register = readl(&ioc->chip->HostInterruptMask);
	him_register |= MPI2_HIM_DIM + MPI2_HIM_RIM + MPI2_HIM_RESET_IRQ_MASK;
	writel(him_register, &ioc->chip->HostInterruptMask);
	readl(&ioc->chip->HostInterruptMask);
}

/**
 * _base_unmask_interrupts - enable interrupts
 * @ioc: per adapter object
 *
 * Enabling only Reply Interrupts
 *
 * Return nothing.
 */
static void
_base_unmask_interrupts(struct MPT2SAS_ADAPTER *ioc)
{
	u32 him_register;

	him_register = readl(&ioc->chip->HostInterruptMask);
	him_register &= ~MPI2_HIM_RIM;
	writel(him_register, &ioc->chip->HostInterruptMask);
	ioc->mask_interrupts = 0;
}

union reply_descriptor {
	u64 word;
	struct {
		u32 low;
		u32 high;
	} u;
};

/**
 * _base_interrupt - MPT adapter (IOC) specific interrupt handler.
 * @irq: irq number (not used)
 * @bus_id: bus identifier cookie == pointer to MPT_ADAPTER structure
 * @r: pt_regs pointer (not used)
 *
 * Return IRQ_HANDLE if processed, else IRQ_NONE.
 */
static irqreturn_t
_base_interrupt(int irq, void *bus_id)
{
	union reply_descriptor rd;
	u32 completed_cmds;
	u8 request_desript_type;
	u16 smid;
	u8 cb_idx;
	u32 reply;
	u8 msix_index;
	struct MPT2SAS_ADAPTER *ioc = bus_id;
	Mpi2ReplyDescriptorsUnion_t *rpf;
	u8 rc;

	if (ioc->mask_interrupts)
		return IRQ_NONE;

	rpf = &ioc->reply_post_free[ioc->reply_post_host_index];
	request_desript_type = rpf->Default.ReplyFlags
	     & MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;
	if (request_desript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED)
		return IRQ_NONE;

	completed_cmds = 0;
	cb_idx = 0xFF;
	do {
		rd.word = rpf->Words;
		if (rd.u.low == UINT_MAX || rd.u.high == UINT_MAX)
			goto out;
		reply = 0;
		cb_idx = 0xFF;
		smid = le16_to_cpu(rpf->Default.DescriptorTypeDependent1);
		msix_index = rpf->Default.MSIxIndex;
		if (request_desript_type ==
		    MPI2_RPY_DESCRIPT_FLAGS_ADDRESS_REPLY) {
			reply = le32_to_cpu
				(rpf->AddressReply.ReplyFrameAddress);
			if (reply > ioc->reply_dma_max_address ||
			    reply < ioc->reply_dma_min_address)
				reply = 0;
		} else if (request_desript_type ==
		    MPI2_RPY_DESCRIPT_FLAGS_TARGET_COMMAND_BUFFER)
			goto next;
		else if (request_desript_type ==
		    MPI2_RPY_DESCRIPT_FLAGS_TARGETASSIST_SUCCESS)
			goto next;
		if (smid)
			cb_idx = _base_get_cb_idx(ioc, smid);
		if (smid && cb_idx != 0xFF) {
			rc = mpt_callbacks[cb_idx](ioc, smid, msix_index,
			    reply);
			if (reply)
				_base_display_reply_info(ioc, smid, msix_index,
				    reply);
			if (rc)
				mpt2sas_base_free_smid(ioc, smid);
		}
		if (!smid)
			_base_async_event(ioc, msix_index, reply);

		/* reply free queue handling */
		if (reply) {
			ioc->reply_free_host_index =
			    (ioc->reply_free_host_index ==
			    (ioc->reply_free_queue_depth - 1)) ?
			    0 : ioc->reply_free_host_index + 1;
			ioc->reply_free[ioc->reply_free_host_index] =
			    cpu_to_le32(reply);
			wmb();
			writel(ioc->reply_free_host_index,
			    &ioc->chip->ReplyFreeHostIndex);
		}

 next:

		rpf->Words = ULLONG_MAX;
		ioc->reply_post_host_index = (ioc->reply_post_host_index ==
		    (ioc->reply_post_queue_depth - 1)) ? 0 :
		    ioc->reply_post_host_index + 1;
		request_desript_type =
		    ioc->reply_post_free[ioc->reply_post_host_index].Default.
		    ReplyFlags & MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;
		completed_cmds++;
		if (request_desript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED)
			goto out;
		if (!ioc->reply_post_host_index)
			rpf = ioc->reply_post_free;
		else
			rpf++;
	} while (1);

 out:

	if (!completed_cmds)
		return IRQ_NONE;

	wmb();
	writel(ioc->reply_post_host_index, &ioc->chip->ReplyPostHostIndex);
	return IRQ_HANDLED;
}

/**
 * mpt2sas_base_release_callback_handler - clear interupt callback handler
 * @cb_idx: callback index
 *
 * Return nothing.
 */
void
mpt2sas_base_release_callback_handler(u8 cb_idx)
{
	mpt_callbacks[cb_idx] = NULL;
}

/**
 * mpt2sas_base_register_callback_handler - obtain index for the interrupt callback handler
 * @cb_func: callback function
 *
 * Returns cb_func.
 */
u8
mpt2sas_base_register_callback_handler(MPT_CALLBACK cb_func)
{
	u8 cb_idx;

	for (cb_idx = MPT_MAX_CALLBACKS-1; cb_idx; cb_idx--)
		if (mpt_callbacks[cb_idx] == NULL)
			break;

	mpt_callbacks[cb_idx] = cb_func;
	return cb_idx;
}

/**
 * mpt2sas_base_initialize_callback_handler - initialize the interrupt callback handler
 *
 * Return nothing.
 */
void
mpt2sas_base_initialize_callback_handler(void)
{
	u8 cb_idx;

	for (cb_idx = 0; cb_idx < MPT_MAX_CALLBACKS; cb_idx++)
		mpt2sas_base_release_callback_handler(cb_idx);
}

/**
 * mpt2sas_base_build_zero_len_sge - build zero length sg entry
 * @ioc: per adapter object
 * @paddr: virtual address for SGE
 *
 * Create a zero length scatter gather entry to insure the IOCs hardware has
 * something to use if the target device goes brain dead and tries
 * to send data even when none is asked for.
 *
 * Return nothing.
 */
void
mpt2sas_base_build_zero_len_sge(struct MPT2SAS_ADAPTER *ioc, void *paddr)
{
	u32 flags_length = (u32)((MPI2_SGE_FLAGS_LAST_ELEMENT |
	    MPI2_SGE_FLAGS_END_OF_BUFFER | MPI2_SGE_FLAGS_END_OF_LIST |
	    MPI2_SGE_FLAGS_SIMPLE_ELEMENT) <<
	    MPI2_SGE_FLAGS_SHIFT);
	ioc->base_add_sg_single(paddr, flags_length, -1);
}

/**
 * _base_add_sg_single_32 - Place a simple 32 bit SGE at address pAddr.
 * @paddr: virtual address for SGE
 * @flags_length: SGE flags and data transfer length
 * @dma_addr: Physical address
 *
 * Return nothing.
 */
static void
_base_add_sg_single_32(void *paddr, u32 flags_length, dma_addr_t dma_addr)
{
	Mpi2SGESimple32_t *sgel = paddr;

	flags_length |= (MPI2_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI2_SGE_FLAGS_SYSTEM_ADDRESS) << MPI2_SGE_FLAGS_SHIFT;
	sgel->FlagsLength = cpu_to_le32(flags_length);
	sgel->Address = cpu_to_le32(dma_addr);
}


/**
 * _base_add_sg_single_64 - Place a simple 64 bit SGE at address pAddr.
 * @paddr: virtual address for SGE
 * @flags_length: SGE flags and data transfer length
 * @dma_addr: Physical address
 *
 * Return nothing.
 */
static void
_base_add_sg_single_64(void *paddr, u32 flags_length, dma_addr_t dma_addr)
{
	Mpi2SGESimple64_t *sgel = paddr;

	flags_length |= (MPI2_SGE_FLAGS_64_BIT_ADDRESSING |
	    MPI2_SGE_FLAGS_SYSTEM_ADDRESS) << MPI2_SGE_FLAGS_SHIFT;
	sgel->FlagsLength = cpu_to_le32(flags_length);
	sgel->Address = cpu_to_le64(dma_addr);
}

#define convert_to_kb(x) ((x) << (PAGE_SHIFT - 10))

/**
 * _base_config_dma_addressing - set dma addressing
 * @ioc: per adapter object
 * @pdev: PCI device struct
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_config_dma_addressing(struct MPT2SAS_ADAPTER *ioc, struct pci_dev *pdev)
{
	struct sysinfo s;
	char *desc = NULL;

	if (sizeof(dma_addr_t) > 4) {
		const uint64_t required_mask =
		    dma_get_required_mask(&pdev->dev);
		if ((required_mask > DMA_BIT_MASK(32)) && !pci_set_dma_mask(pdev,
		    DMA_BIT_MASK(64)) && !pci_set_consistent_dma_mask(pdev,
		    DMA_BIT_MASK(64))) {
			ioc->base_add_sg_single = &_base_add_sg_single_64;
			ioc->sge_size = sizeof(Mpi2SGESimple64_t);
			desc = "64";
			goto out;
		}
	}

	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))
	    && !pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32))) {
		ioc->base_add_sg_single = &_base_add_sg_single_32;
		ioc->sge_size = sizeof(Mpi2SGESimple32_t);
		desc = "32";
	} else
		return -ENODEV;

 out:
	si_meminfo(&s);
	printk(MPT2SAS_INFO_FMT "%s BIT PCI BUS DMA ADDRESSING SUPPORTED, "
	    "total mem (%ld kB)\n", ioc->name, desc, convert_to_kb(s.totalram));

	return 0;
}

/**
 * _base_save_msix_table - backup msix vector table
 * @ioc: per adapter object
 *
 * This address an errata where diag reset clears out the table
 */
static void
_base_save_msix_table(struct MPT2SAS_ADAPTER *ioc)
{
	int i;

	if (!ioc->msix_enable || ioc->msix_table_backup == NULL)
		return;

	for (i = 0; i < ioc->msix_vector_count; i++)
		ioc->msix_table_backup[i] = ioc->msix_table[i];
}

/**
 * _base_restore_msix_table - this restores the msix vector table
 * @ioc: per adapter object
 *
 */
static void
_base_restore_msix_table(struct MPT2SAS_ADAPTER *ioc)
{
	int i;

	if (!ioc->msix_enable || ioc->msix_table_backup == NULL)
		return;

	for (i = 0; i < ioc->msix_vector_count; i++)
		ioc->msix_table[i] = ioc->msix_table_backup[i];
}

/**
 * _base_check_enable_msix - checks MSIX capabable.
 * @ioc: per adapter object
 *
 * Check to see if card is capable of MSIX, and set number
 * of avaliable msix vectors
 */
static int
_base_check_enable_msix(struct MPT2SAS_ADAPTER *ioc)
{
	int base;
	u16 message_control;
	u32 msix_table_offset;

	base = pci_find_capability(ioc->pdev, PCI_CAP_ID_MSIX);
	if (!base) {
		dfailprintk(ioc, printk(MPT2SAS_INFO_FMT "msix not "
		    "supported\n", ioc->name));
		return -EINVAL;
	}

	/* get msix vector count */
	pci_read_config_word(ioc->pdev, base + 2, &message_control);
	ioc->msix_vector_count = (message_control & 0x3FF) + 1;

	/* get msix table  */
	pci_read_config_dword(ioc->pdev, base + 4, &msix_table_offset);
	msix_table_offset &= 0xFFFFFFF8;
	ioc->msix_table = (u32 *)((void *)ioc->chip + msix_table_offset);

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "msix is supported, "
	    "vector_count(%d), table_offset(0x%08x), table(%p)\n", ioc->name,
	    ioc->msix_vector_count, msix_table_offset, ioc->msix_table));
	return 0;
}

/**
 * _base_disable_msix - disables msix
 * @ioc: per adapter object
 *
 */
static void
_base_disable_msix(struct MPT2SAS_ADAPTER *ioc)
{
	if (ioc->msix_enable) {
		pci_disable_msix(ioc->pdev);
		kfree(ioc->msix_table_backup);
		ioc->msix_table_backup = NULL;
		ioc->msix_enable = 0;
	}
}

/**
 * _base_enable_msix - enables msix, failback to io_apic
 * @ioc: per adapter object
 *
 */
static int
_base_enable_msix(struct MPT2SAS_ADAPTER *ioc)
{
	struct msix_entry entries;
	int r;
	u8 try_msix = 0;

	if (msix_disable == -1 || msix_disable == 0)
		try_msix = 1;

	if (!try_msix)
		goto try_ioapic;

	if (_base_check_enable_msix(ioc) != 0)
		goto try_ioapic;

	ioc->msix_table_backup = kcalloc(ioc->msix_vector_count,
	    sizeof(u32), GFP_KERNEL);
	if (!ioc->msix_table_backup) {
		dfailprintk(ioc, printk(MPT2SAS_INFO_FMT "allocation for "
		    "msix_table_backup failed!!!\n", ioc->name));
		goto try_ioapic;
	}

	memset(&entries, 0, sizeof(struct msix_entry));
	r = pci_enable_msix(ioc->pdev, &entries, 1);
	if (r) {
		dfailprintk(ioc, printk(MPT2SAS_INFO_FMT "pci_enable_msix "
		    "failed (r=%d) !!!\n", ioc->name, r));
		goto try_ioapic;
	}

	r = request_irq(entries.vector, _base_interrupt, IRQF_SHARED,
	    ioc->name, ioc);
	if (r) {
		dfailprintk(ioc, printk(MPT2SAS_INFO_FMT "unable to allocate "
		    "interrupt %d !!!\n", ioc->name, entries.vector));
		pci_disable_msix(ioc->pdev);
		goto try_ioapic;
	}

	ioc->pci_irq = entries.vector;
	ioc->msix_enable = 1;
	return 0;

/* failback to io_apic interrupt routing */
 try_ioapic:

	r = request_irq(ioc->pdev->irq, _base_interrupt, IRQF_SHARED,
	    ioc->name, ioc);
	if (r) {
		printk(MPT2SAS_ERR_FMT "unable to allocate interrupt %d!\n",
		    ioc->name, ioc->pdev->irq);
		r = -EBUSY;
		goto out_fail;
	}

	ioc->pci_irq = ioc->pdev->irq;
	return 0;

 out_fail:
	return r;
}

/**
 * mpt2sas_base_map_resources - map in controller resources (io/irq/memap)
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_base_map_resources(struct MPT2SAS_ADAPTER *ioc)
{
	struct pci_dev *pdev = ioc->pdev;
	u32 memap_sz;
	u32 pio_sz;
	int i, r = 0;
	u64 pio_chip = 0;
	u64 chip_phys = 0;

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n",
	    ioc->name, __func__));

	ioc->bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if (pci_enable_device_mem(pdev)) {
		printk(MPT2SAS_WARN_FMT "pci_enable_device_mem: "
		    "failed\n", ioc->name);
		return -ENODEV;
	}


	if (pci_request_selected_regions(pdev, ioc->bars,
	    MPT2SAS_DRIVER_NAME)) {
		printk(MPT2SAS_WARN_FMT "pci_request_selected_regions: "
		    "failed\n", ioc->name);
		r = -ENODEV;
		goto out_fail;
	}

	/* AER (Advanced Error Reporting) hooks */
	pci_enable_pcie_error_reporting(pdev);

	pci_set_master(pdev);

	if (_base_config_dma_addressing(ioc, pdev) != 0) {
		printk(MPT2SAS_WARN_FMT "no suitable DMA mask for %s\n",
		    ioc->name, pci_name(pdev));
		r = -ENODEV;
		goto out_fail;
	}

	for (i = 0, memap_sz = 0, pio_sz = 0 ; i < DEVICE_COUNT_RESOURCE; i++) {
		if (pci_resource_flags(pdev, i) & IORESOURCE_IO) {
			if (pio_sz)
				continue;
			pio_chip = (u64)pci_resource_start(pdev, i);
			pio_sz = pci_resource_len(pdev, i);
		} else {
			if (memap_sz)
				continue;
			/* verify memory resource is valid before using */
			if (pci_resource_flags(pdev, i) & IORESOURCE_MEM) {
				ioc->chip_phys = pci_resource_start(pdev, i);
				chip_phys = (u64)ioc->chip_phys;
				memap_sz = pci_resource_len(pdev, i);
				ioc->chip = ioremap(ioc->chip_phys, memap_sz);
				if (ioc->chip == NULL) {
					printk(MPT2SAS_ERR_FMT "unable to map "
					    "adapter memory!\n", ioc->name);
					r = -EINVAL;
					goto out_fail;
				}
			}
		}
	}

	_base_mask_interrupts(ioc);
	r = _base_enable_msix(ioc);
	if (r)
		goto out_fail;

	printk(MPT2SAS_INFO_FMT "%s: IRQ %d\n",
	    ioc->name,  ((ioc->msix_enable) ? "PCI-MSI-X enabled" :
	    "IO-APIC enabled"), ioc->pci_irq);
	printk(MPT2SAS_INFO_FMT "iomem(0x%016llx), mapped(0x%p), size(%d)\n",
	    ioc->name, (unsigned long long)chip_phys, ioc->chip, memap_sz);
	printk(MPT2SAS_INFO_FMT "ioport(0x%016llx), size(%d)\n",
	    ioc->name, (unsigned long long)pio_chip, pio_sz);

	/* Save PCI configuration state for recovery from PCI AER/EEH errors */
	pci_save_state(pdev);

	return 0;

 out_fail:
	if (ioc->chip_phys)
		iounmap(ioc->chip);
	ioc->chip_phys = 0;
	ioc->pci_irq = -1;
	pci_release_selected_regions(ioc->pdev, ioc->bars);
	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);
	return r;
}

/**
 * mpt2sas_base_get_msg_frame - obtain request mf pointer
 * @ioc: per adapter object
 * @smid: system request message index(smid zero is invalid)
 *
 * Returns virt pointer to message frame.
 */
void *
mpt2sas_base_get_msg_frame(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	return (void *)(ioc->request + (smid * ioc->request_sz));
}

/**
 * mpt2sas_base_get_sense_buffer - obtain a sense buffer assigned to a mf request
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns virt pointer to sense buffer.
 */
void *
mpt2sas_base_get_sense_buffer(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	return (void *)(ioc->sense + ((smid - 1) * SCSI_SENSE_BUFFERSIZE));
}

/**
 * mpt2sas_base_get_sense_buffer_dma - obtain a sense buffer assigned to a mf request
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns phys pointer to the low 32bit address of the sense buffer.
 */
__le32
mpt2sas_base_get_sense_buffer_dma(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	return cpu_to_le32(ioc->sense_dma +
			((smid - 1) * SCSI_SENSE_BUFFERSIZE));
}

/**
 * mpt2sas_base_get_reply_virt_addr - obtain reply frames virt address
 * @ioc: per adapter object
 * @phys_addr: lower 32 physical addr of the reply
 *
 * Converts 32bit lower physical addr into a virt address.
 */
void *
mpt2sas_base_get_reply_virt_addr(struct MPT2SAS_ADAPTER *ioc, u32 phys_addr)
{
	if (!phys_addr)
		return NULL;
	return ioc->reply + (phys_addr - (u32)ioc->reply_dma);
}

/**
 * mpt2sas_base_get_smid - obtain a free smid from internal queue
 * @ioc: per adapter object
 * @cb_idx: callback index
 *
 * Returns smid (zero is invalid)
 */
u16
mpt2sas_base_get_smid(struct MPT2SAS_ADAPTER *ioc, u8 cb_idx)
{
	unsigned long flags;
	struct request_tracker *request;
	u16 smid;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (list_empty(&ioc->internal_free_list)) {
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		printk(MPT2SAS_ERR_FMT "%s: smid not available\n",
		    ioc->name, __func__);
		return 0;
	}

	request = list_entry(ioc->internal_free_list.next,
	    struct request_tracker, tracker_list);
	request->cb_idx = cb_idx;
	smid = request->smid;
	list_del(&request->tracker_list);
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return smid;
}

/**
 * mpt2sas_base_get_smid_scsiio - obtain a free smid from scsiio queue
 * @ioc: per adapter object
 * @cb_idx: callback index
 * @scmd: pointer to scsi command object
 *
 * Returns smid (zero is invalid)
 */
u16
mpt2sas_base_get_smid_scsiio(struct MPT2SAS_ADAPTER *ioc, u8 cb_idx,
    struct scsi_cmnd *scmd)
{
	unsigned long flags;
	struct request_tracker *request;
	u16 smid;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (list_empty(&ioc->free_list)) {
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		printk(MPT2SAS_ERR_FMT "%s: smid not available\n",
		    ioc->name, __func__);
		return 0;
	}

	request = list_entry(ioc->free_list.next,
	    struct request_tracker, tracker_list);
	request->scmd = scmd;
	request->cb_idx = cb_idx;
	smid = request->smid;
	list_del(&request->tracker_list);
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return smid;
}

/**
 * mpt2sas_base_get_smid_hpr - obtain a free smid from hi-priority queue
 * @ioc: per adapter object
 * @cb_idx: callback index
 *
 * Returns smid (zero is invalid)
 */
u16
mpt2sas_base_get_smid_hpr(struct MPT2SAS_ADAPTER *ioc, u8 cb_idx)
{
	unsigned long flags;
	struct request_tracker *request;
	u16 smid;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (list_empty(&ioc->hpr_free_list)) {
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		return 0;
	}

	request = list_entry(ioc->hpr_free_list.next,
	    struct request_tracker, tracker_list);
	request->cb_idx = cb_idx;
	smid = request->smid;
	list_del(&request->tracker_list);
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return smid;
}


/**
 * mpt2sas_base_free_smid - put smid back on free_list
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return nothing.
 */
void
mpt2sas_base_free_smid(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	unsigned long flags;
	int i;
	struct chain_tracker *chain_req, *next;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (smid >= ioc->hi_priority_smid) {
		if (smid < ioc->internal_smid) {
			/* hi-priority */
			i = smid - ioc->hi_priority_smid;
			ioc->hpr_lookup[i].cb_idx = 0xFF;
			list_add_tail(&ioc->hpr_lookup[i].tracker_list,
			    &ioc->hpr_free_list);
		} else {
			/* internal queue */
			i = smid - ioc->internal_smid;
			ioc->internal_lookup[i].cb_idx = 0xFF;
			list_add_tail(&ioc->internal_lookup[i].tracker_list,
			    &ioc->internal_free_list);
		}
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		return;
	}

	/* scsiio queue */
	i = smid - 1;
	if (!list_empty(&ioc->scsi_lookup[i].chain_list)) {
		list_for_each_entry_safe(chain_req, next,
		    &ioc->scsi_lookup[i].chain_list, tracker_list) {
			list_del_init(&chain_req->tracker_list);
			list_add_tail(&chain_req->tracker_list,
			    &ioc->free_chain_list);
		}
	}
	ioc->scsi_lookup[i].cb_idx = 0xFF;
	ioc->scsi_lookup[i].scmd = NULL;
	list_add_tail(&ioc->scsi_lookup[i].tracker_list,
	    &ioc->free_list);
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

	/*
	 * See _wait_for_commands_to_complete() call with regards to this code.
	 */
	if (ioc->shost_recovery && ioc->pending_io_count) {
		if (ioc->pending_io_count == 1)
			wake_up(&ioc->reset_wq);
		ioc->pending_io_count--;
	}
}

/**
 * _base_writeq - 64 bit write to MMIO
 * @ioc: per adapter object
 * @b: data payload
 * @addr: address in MMIO space
 * @writeq_lock: spin lock
 *
 * Glue for handling an atomic 64 bit word to MMIO. This special handling takes
 * care of 32 bit environment where its not quarenteed to send the entire word
 * in one transfer.
 */
#ifndef writeq
static inline void _base_writeq(__u64 b, volatile void __iomem *addr,
    spinlock_t *writeq_lock)
{
	unsigned long flags;
	__u64 data_out = cpu_to_le64(b);

	spin_lock_irqsave(writeq_lock, flags);
	writel((u32)(data_out), addr);
	writel((u32)(data_out >> 32), (addr + 4));
	spin_unlock_irqrestore(writeq_lock, flags);
}
#else
static inline void _base_writeq(__u64 b, volatile void __iomem *addr,
    spinlock_t *writeq_lock)
{
	writeq(cpu_to_le64(b), addr);
}
#endif

/**
 * mpt2sas_base_put_smid_scsi_io - send SCSI_IO request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @handle: device handle
 *
 * Return nothing.
 */
void
mpt2sas_base_put_smid_scsi_io(struct MPT2SAS_ADAPTER *ioc, u16 smid, u16 handle)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;


	descriptor.SCSIIO.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO;
	descriptor.SCSIIO.MSIxIndex = 0; /* TODO */
	descriptor.SCSIIO.SMID = cpu_to_le16(smid);
	descriptor.SCSIIO.DevHandle = cpu_to_le16(handle);
	descriptor.SCSIIO.LMID = 0;
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}


/**
 * mpt2sas_base_put_smid_hi_priority - send Task Managment request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return nothing.
 */
void
mpt2sas_base_put_smid_hi_priority(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;

	descriptor.HighPriority.RequestFlags =
	    MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY;
	descriptor.HighPriority.MSIxIndex = 0; /* TODO */
	descriptor.HighPriority.SMID = cpu_to_le16(smid);
	descriptor.HighPriority.LMID = 0;
	descriptor.HighPriority.Reserved1 = 0;
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * mpt2sas_base_put_smid_default - Default, primarily used for config pages
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return nothing.
 */
void
mpt2sas_base_put_smid_default(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;

	descriptor.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	descriptor.Default.MSIxIndex = 0; /* TODO */
	descriptor.Default.SMID = cpu_to_le16(smid);
	descriptor.Default.LMID = 0;
	descriptor.Default.DescriptorTypeDependent = 0;
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * mpt2sas_base_put_smid_target_assist - send Target Assist/Status to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @io_index: value used to track the IO
 *
 * Return nothing.
 */
void
mpt2sas_base_put_smid_target_assist(struct MPT2SAS_ADAPTER *ioc, u16 smid,
    u16 io_index)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;

	descriptor.SCSITarget.RequestFlags =
	    MPI2_REQ_DESCRIPT_FLAGS_SCSI_TARGET;
	descriptor.SCSITarget.MSIxIndex = 0; /* TODO */
	descriptor.SCSITarget.SMID = cpu_to_le16(smid);
	descriptor.SCSITarget.LMID = 0;
	descriptor.SCSITarget.IoIndex = cpu_to_le16(io_index);
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * _base_display_dell_branding - Disply branding string
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_base_display_dell_branding(struct MPT2SAS_ADAPTER *ioc)
{
	char dell_branding[MPT2SAS_DELL_BRANDING_SIZE];

	if (ioc->pdev->subsystem_vendor != PCI_VENDOR_ID_DELL)
		return;

	memset(dell_branding, 0, MPT2SAS_DELL_BRANDING_SIZE);
	switch (ioc->pdev->subsystem_device) {
	case MPT2SAS_DELL_6GBPS_SAS_HBA_SSDID:
		strncpy(dell_branding, MPT2SAS_DELL_6GBPS_SAS_HBA_BRANDING,
		    MPT2SAS_DELL_BRANDING_SIZE - 1);
		break;
	case MPT2SAS_DELL_PERC_H200_ADAPTER_SSDID:
		strncpy(dell_branding, MPT2SAS_DELL_PERC_H200_ADAPTER_BRANDING,
		    MPT2SAS_DELL_BRANDING_SIZE - 1);
		break;
	case MPT2SAS_DELL_PERC_H200_INTEGRATED_SSDID:
		strncpy(dell_branding,
		    MPT2SAS_DELL_PERC_H200_INTEGRATED_BRANDING,
		    MPT2SAS_DELL_BRANDING_SIZE - 1);
		break;
	case MPT2SAS_DELL_PERC_H200_MODULAR_SSDID:
		strncpy(dell_branding,
		    MPT2SAS_DELL_PERC_H200_MODULAR_BRANDING,
		    MPT2SAS_DELL_BRANDING_SIZE - 1);
		break;
	case MPT2SAS_DELL_PERC_H200_EMBEDDED_SSDID:
		strncpy(dell_branding,
		    MPT2SAS_DELL_PERC_H200_EMBEDDED_BRANDING,
		    MPT2SAS_DELL_BRANDING_SIZE - 1);
		break;
	case MPT2SAS_DELL_PERC_H200_SSDID:
		strncpy(dell_branding, MPT2SAS_DELL_PERC_H200_BRANDING,
		    MPT2SAS_DELL_BRANDING_SIZE - 1);
		break;
	case MPT2SAS_DELL_6GBPS_SAS_SSDID:
		strncpy(dell_branding, MPT2SAS_DELL_6GBPS_SAS_BRANDING,
		    MPT2SAS_DELL_BRANDING_SIZE - 1);
		break;
	default:
		sprintf(dell_branding, "0x%4X", ioc->pdev->subsystem_device);
		break;
	}

	printk(MPT2SAS_INFO_FMT "%s: Vendor(0x%04X), Device(0x%04X),"
	    " SSVID(0x%04X), SSDID(0x%04X)\n", ioc->name, dell_branding,
	    ioc->pdev->vendor, ioc->pdev->device, ioc->pdev->subsystem_vendor,
	    ioc->pdev->subsystem_device);
}

/**
 * _base_display_ioc_capabilities - Disply IOC's capabilities.
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_base_display_ioc_capabilities(struct MPT2SAS_ADAPTER *ioc)
{
	int i = 0;
	char desc[16];
	u8 revision;
	u32 iounit_pg1_flags;

	pci_read_config_byte(ioc->pdev, PCI_CLASS_REVISION, &revision);
	strncpy(desc, ioc->manu_pg0.ChipName, 16);
	printk(MPT2SAS_INFO_FMT "%s: FWVersion(%02d.%02d.%02d.%02d), "
	   "ChipRevision(0x%02x), BiosVersion(%02d.%02d.%02d.%02d)\n",
	    ioc->name, desc,
	   (ioc->facts.FWVersion.Word & 0xFF000000) >> 24,
	   (ioc->facts.FWVersion.Word & 0x00FF0000) >> 16,
	   (ioc->facts.FWVersion.Word & 0x0000FF00) >> 8,
	   ioc->facts.FWVersion.Word & 0x000000FF,
	   revision,
	   (ioc->bios_pg3.BiosVersion & 0xFF000000) >> 24,
	   (ioc->bios_pg3.BiosVersion & 0x00FF0000) >> 16,
	   (ioc->bios_pg3.BiosVersion & 0x0000FF00) >> 8,
	    ioc->bios_pg3.BiosVersion & 0x000000FF);

	_base_display_dell_branding(ioc);

	printk(MPT2SAS_INFO_FMT "Protocol=(", ioc->name);

	if (ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_SCSI_INITIATOR) {
		printk("Initiator");
		i++;
	}

	if (ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_SCSI_TARGET) {
		printk("%sTarget", i ? "," : "");
		i++;
	}

	i = 0;
	printk("), ");
	printk("Capabilities=(");

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_INTEGRATED_RAID) {
		printk("Raid");
		i++;
	}

	if (ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_TLR) {
		printk("%sTLR", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_MULTICAST) {
		printk("%sMulticast", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_BIDIRECTIONAL_TARGET) {
		printk("%sBIDI Target", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_EEDP) {
		printk("%sEEDP", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_SNAPSHOT_BUFFER) {
		printk("%sSnapshot Buffer", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER) {
		printk("%sDiag Trace Buffer", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_EXTENDED_BUFFER) {
		printk(KERN_INFO "%sDiag Extended Buffer", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_TASK_SET_FULL_HANDLING) {
		printk("%sTask Set Full", i ? "," : "");
		i++;
	}

	iounit_pg1_flags = le32_to_cpu(ioc->iounit_pg1.Flags);
	if (!(iounit_pg1_flags & MPI2_IOUNITPAGE1_NATIVE_COMMAND_Q_DISABLE)) {
		printk("%sNCQ", i ? "," : "");
		i++;
	}

	printk(")\n");
}

/**
 * _base_update_missing_delay - change the missing delay timers
 * @ioc: per adapter object
 * @device_missing_delay: amount of time till device is reported missing
 * @io_missing_delay: interval IO is returned when there is a missing device
 *
 * Return nothing.
 *
 * Passed on the command line, this function will modify the device missing
 * delay, as well as the io missing delay. This should be called at driver
 * load time.
 */
static void
_base_update_missing_delay(struct MPT2SAS_ADAPTER *ioc,
	u16 device_missing_delay, u8 io_missing_delay)
{
	u16 dmd, dmd_new, dmd_orignal;
	u8 io_missing_delay_original;
	u16 sz;
	Mpi2SasIOUnitPage1_t *sas_iounit_pg1 = NULL;
	Mpi2ConfigReply_t mpi_reply;
	u8 num_phys = 0;
	u16 ioc_status;

	mpt2sas_config_get_number_hba_phys(ioc, &num_phys);
	if (!num_phys)
		return;

	sz = offsetof(Mpi2SasIOUnitPage1_t, PhyData) + (num_phys *
	    sizeof(Mpi2SasIOUnit1PhyData_t));
	sas_iounit_pg1 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg1) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	if ((mpt2sas_config_get_sas_iounit_pg1(ioc, &mpi_reply,
	    sas_iounit_pg1, sz))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}

	/* device missing delay */
	dmd = sas_iounit_pg1->ReportDeviceMissingDelay;
	if (dmd & MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16)
		dmd = (dmd & MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16;
	else
		dmd = dmd & MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK;
	dmd_orignal = dmd;
	if (device_missing_delay > 0x7F) {
		dmd = (device_missing_delay > 0x7F0) ? 0x7F0 :
		    device_missing_delay;
		dmd = dmd / 16;
		dmd |= MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16;
	} else
		dmd = device_missing_delay;
	sas_iounit_pg1->ReportDeviceMissingDelay = dmd;

	/* io missing delay */
	io_missing_delay_original = sas_iounit_pg1->IODeviceMissingDelay;
	sas_iounit_pg1->IODeviceMissingDelay = io_missing_delay;

	if (!mpt2sas_config_set_sas_iounit_pg1(ioc, &mpi_reply, sas_iounit_pg1,
	    sz)) {
		if (dmd & MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16)
			dmd_new = (dmd &
			    MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16;
		else
			dmd_new =
		    dmd & MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK;
		printk(MPT2SAS_INFO_FMT "device_missing_delay: old(%d), "
		    "new(%d)\n", ioc->name, dmd_orignal, dmd_new);
		printk(MPT2SAS_INFO_FMT "ioc_missing_delay: old(%d), "
		    "new(%d)\n", ioc->name, io_missing_delay_original,
		    io_missing_delay);
		ioc->device_missing_delay = dmd_new;
		ioc->io_missing_delay = io_missing_delay;
	}

out:
	kfree(sas_iounit_pg1);
}

/**
 * _base_static_config_pages - static start of day config pages
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_base_static_config_pages(struct MPT2SAS_ADAPTER *ioc)
{
	Mpi2ConfigReply_t mpi_reply;
	u32 iounit_pg1_flags;

	mpt2sas_config_get_manufacturing_pg0(ioc, &mpi_reply, &ioc->manu_pg0);
	if (ioc->ir_firmware)
		mpt2sas_config_get_manufacturing_pg10(ioc, &mpi_reply,
		    &ioc->manu_pg10);
	mpt2sas_config_get_bios_pg2(ioc, &mpi_reply, &ioc->bios_pg2);
	mpt2sas_config_get_bios_pg3(ioc, &mpi_reply, &ioc->bios_pg3);
	mpt2sas_config_get_ioc_pg8(ioc, &mpi_reply, &ioc->ioc_pg8);
	mpt2sas_config_get_iounit_pg0(ioc, &mpi_reply, &ioc->iounit_pg0);
	mpt2sas_config_get_iounit_pg1(ioc, &mpi_reply, &ioc->iounit_pg1);
	_base_display_ioc_capabilities(ioc);

	/*
	 * Enable task_set_full handling in iounit_pg1 when the
	 * facts capabilities indicate that its supported.
	 */
	iounit_pg1_flags = le32_to_cpu(ioc->iounit_pg1.Flags);
	if ((ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_TASK_SET_FULL_HANDLING))
		iounit_pg1_flags &=
		    ~MPI2_IOUNITPAGE1_DISABLE_TASK_SET_FULL_HANDLING;
	else
		iounit_pg1_flags |=
		    MPI2_IOUNITPAGE1_DISABLE_TASK_SET_FULL_HANDLING;
	ioc->iounit_pg1.Flags = cpu_to_le32(iounit_pg1_flags);
	mpt2sas_config_set_iounit_pg1(ioc, &mpi_reply, &ioc->iounit_pg1);

}

/**
 * _base_release_memory_pools - release memory
 * @ioc: per adapter object
 *
 * Free memory allocated from _base_allocate_memory_pools.
 *
 * Return nothing.
 */
static void
_base_release_memory_pools(struct MPT2SAS_ADAPTER *ioc)
{
	int i;

	dexitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	if (ioc->request) {
		pci_free_consistent(ioc->pdev, ioc->request_dma_sz,
		    ioc->request,  ioc->request_dma);
		dexitprintk(ioc, printk(MPT2SAS_INFO_FMT "request_pool(0x%p)"
		    ": free\n", ioc->name, ioc->request));
		ioc->request = NULL;
	}

	if (ioc->sense) {
		pci_pool_free(ioc->sense_dma_pool, ioc->sense, ioc->sense_dma);
		if (ioc->sense_dma_pool)
			pci_pool_destroy(ioc->sense_dma_pool);
		dexitprintk(ioc, printk(MPT2SAS_INFO_FMT "sense_pool(0x%p)"
		    ": free\n", ioc->name, ioc->sense));
		ioc->sense = NULL;
	}

	if (ioc->reply) {
		pci_pool_free(ioc->reply_dma_pool, ioc->reply, ioc->reply_dma);
		if (ioc->reply_dma_pool)
			pci_pool_destroy(ioc->reply_dma_pool);
		dexitprintk(ioc, printk(MPT2SAS_INFO_FMT "reply_pool(0x%p)"
		     ": free\n", ioc->name, ioc->reply));
		ioc->reply = NULL;
	}

	if (ioc->reply_free) {
		pci_pool_free(ioc->reply_free_dma_pool, ioc->reply_free,
		    ioc->reply_free_dma);
		if (ioc->reply_free_dma_pool)
			pci_pool_destroy(ioc->reply_free_dma_pool);
		dexitprintk(ioc, printk(MPT2SAS_INFO_FMT "reply_free_pool"
		    "(0x%p): free\n", ioc->name, ioc->reply_free));
		ioc->reply_free = NULL;
	}

	if (ioc->reply_post_free) {
		pci_pool_free(ioc->reply_post_free_dma_pool,
		    ioc->reply_post_free, ioc->reply_post_free_dma);
		if (ioc->reply_post_free_dma_pool)
			pci_pool_destroy(ioc->reply_post_free_dma_pool);
		dexitprintk(ioc, printk(MPT2SAS_INFO_FMT
		    "reply_post_free_pool(0x%p): free\n", ioc->name,
		    ioc->reply_post_free));
		ioc->reply_post_free = NULL;
	}

	if (ioc->config_page) {
		dexitprintk(ioc, printk(MPT2SAS_INFO_FMT
		    "config_page(0x%p): free\n", ioc->name,
		    ioc->config_page));
		pci_free_consistent(ioc->pdev, ioc->config_page_sz,
		    ioc->config_page, ioc->config_page_dma);
	}

	if (ioc->scsi_lookup) {
		free_pages((ulong)ioc->scsi_lookup, ioc->scsi_lookup_pages);
		ioc->scsi_lookup = NULL;
	}
	kfree(ioc->hpr_lookup);
	kfree(ioc->internal_lookup);
	if (ioc->chain_lookup) {
		for (i = 0; i < ioc->chain_depth; i++) {
			if (ioc->chain_lookup[i].chain_buffer)
				pci_pool_free(ioc->chain_dma_pool,
				    ioc->chain_lookup[i].chain_buffer,
				    ioc->chain_lookup[i].chain_buffer_dma);
		}
		if (ioc->chain_dma_pool)
			pci_pool_destroy(ioc->chain_dma_pool);
	}
	if (ioc->chain_lookup) {
		free_pages((ulong)ioc->chain_lookup, ioc->chain_pages);
		ioc->chain_lookup = NULL;
	}
}


/**
 * _base_allocate_memory_pools - allocate start of day memory pools
 * @ioc: per adapter object
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 success, anything else error
 */
static int
_base_allocate_memory_pools(struct MPT2SAS_ADAPTER *ioc,  int sleep_flag)
{
	Mpi2IOCFactsReply_t *facts;
	u32 queue_size, queue_diff;
	u16 max_sge_elements;
	u16 num_of_reply_frames;
	u16 chains_needed_per_io;
	u32 sz, total_sz;
	u32 retry_sz;
	u16 max_request_credit;
	int i;

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	retry_sz = 0;
	facts = &ioc->facts;

	/* command line tunables  for max sgl entries */
	if (max_sgl_entries != -1) {
		ioc->shost->sg_tablesize = (max_sgl_entries <
		    MPT2SAS_SG_DEPTH) ? max_sgl_entries :
		    MPT2SAS_SG_DEPTH;
	} else {
		ioc->shost->sg_tablesize = MPT2SAS_SG_DEPTH;
	}

	/* command line tunables  for max controller queue depth */
	if (max_queue_depth != -1)
		max_request_credit = (max_queue_depth < facts->RequestCredit)
		    ? max_queue_depth : facts->RequestCredit;
	else
		max_request_credit = facts->RequestCredit;

	ioc->hba_queue_depth = max_request_credit;
	ioc->hi_priority_depth = facts->HighPriorityCredit;
	ioc->internal_depth = ioc->hi_priority_depth + 5;

	/* request frame size */
	ioc->request_sz = facts->IOCRequestFrameSize * 4;

	/* reply frame size */
	ioc->reply_sz = facts->ReplyFrameSize * 4;

 retry_allocation:
	total_sz = 0;
	/* calculate number of sg elements left over in the 1st frame */
	max_sge_elements = ioc->request_sz - ((sizeof(Mpi2SCSIIORequest_t) -
	    sizeof(Mpi2SGEIOUnion_t)) + ioc->sge_size);
	ioc->max_sges_in_main_message = max_sge_elements/ioc->sge_size;

	/* now do the same for a chain buffer */
	max_sge_elements = ioc->request_sz - ioc->sge_size;
	ioc->max_sges_in_chain_message = max_sge_elements/ioc->sge_size;

	ioc->chain_offset_value_for_main_message =
	    ((sizeof(Mpi2SCSIIORequest_t) - sizeof(Mpi2SGEIOUnion_t)) +
	     (ioc->max_sges_in_chain_message * ioc->sge_size)) / 4;

	/*
	 *  MPT2SAS_SG_DEPTH = CONFIG_FUSION_MAX_SGE
	 */
	chains_needed_per_io = ((ioc->shost->sg_tablesize -
	   ioc->max_sges_in_main_message)/ioc->max_sges_in_chain_message)
	    + 1;
	if (chains_needed_per_io > facts->MaxChainDepth) {
		chains_needed_per_io = facts->MaxChainDepth;
		ioc->shost->sg_tablesize = min_t(u16,
		ioc->max_sges_in_main_message + (ioc->max_sges_in_chain_message
		* chains_needed_per_io), ioc->shost->sg_tablesize);
	}
	ioc->chains_needed_per_io = chains_needed_per_io;

	/* reply free queue sizing - taking into account for events */
	num_of_reply_frames = ioc->hba_queue_depth + 32;

	/* number of replies frames can't be a multiple of 16 */
	/* decrease number of reply frames by 1 */
	if (!(num_of_reply_frames % 16))
		num_of_reply_frames--;

	/* calculate number of reply free queue entries
	 *  (must be multiple of 16)
	 */

	/* (we know reply_free_queue_depth is not a multiple of 16) */
	queue_size = num_of_reply_frames;
	queue_size += 16 - (queue_size % 16);
	ioc->reply_free_queue_depth = queue_size;

	/* reply descriptor post queue sizing */
	/* this size should be the number of request frames + number of reply
	 * frames
	 */

	queue_size = ioc->hba_queue_depth + num_of_reply_frames + 1;
	/* round up to 16 byte boundary */
	if (queue_size % 16)
		queue_size += 16 - (queue_size % 16);

	/* check against IOC maximum reply post queue depth */
	if (queue_size > facts->MaxReplyDescriptorPostQueueDepth) {
		queue_diff = queue_size -
		    facts->MaxReplyDescriptorPostQueueDepth;

		/* round queue_diff up to multiple of 16 */
		if (queue_diff % 16)
			queue_diff += 16 - (queue_diff % 16);

		/* adjust hba_queue_depth, reply_free_queue_depth,
		 * and queue_size
		 */
		ioc->hba_queue_depth -= (queue_diff / 2);
		ioc->reply_free_queue_depth -= (queue_diff / 2);
		queue_size = facts->MaxReplyDescriptorPostQueueDepth;
	}
	ioc->reply_post_queue_depth = queue_size;

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "scatter gather: "
	    "sge_in_main_msg(%d), sge_per_chain(%d), sge_per_io(%d), "
	    "chains_per_io(%d)\n", ioc->name, ioc->max_sges_in_main_message,
	    ioc->max_sges_in_chain_message, ioc->shost->sg_tablesize,
	    ioc->chains_needed_per_io));

	ioc->scsiio_depth = ioc->hba_queue_depth -
	    ioc->hi_priority_depth - ioc->internal_depth;

	/* set the scsi host can_queue depth
	 * with some internal commands that could be outstanding
	 */
	ioc->shost->can_queue = ioc->scsiio_depth - (2);
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "scsi host: "
	    "can_queue depth (%d)\n", ioc->name, ioc->shost->can_queue));

	/* contiguous pool for request and chains, 16 byte align, one extra "
	 * "frame for smid=0
	 */
	ioc->chain_depth = ioc->chains_needed_per_io * ioc->scsiio_depth;
	sz = ((ioc->scsiio_depth + 1) * ioc->request_sz);

	/* hi-priority queue */
	sz += (ioc->hi_priority_depth * ioc->request_sz);

	/* internal queue */
	sz += (ioc->internal_depth * ioc->request_sz);

	ioc->request_dma_sz = sz;
	ioc->request = pci_alloc_consistent(ioc->pdev, sz, &ioc->request_dma);
	if (!ioc->request) {
		printk(MPT2SAS_ERR_FMT "request pool: pci_alloc_consistent "
		    "failed: hba_depth(%d), chains_per_io(%d), frame_sz(%d), "
		    "total(%d kB)\n", ioc->name, ioc->hba_queue_depth,
		    ioc->chains_needed_per_io, ioc->request_sz, sz/1024);
		if (ioc->scsiio_depth < MPT2SAS_SAS_QUEUE_DEPTH)
			goto out;
		retry_sz += 64;
		ioc->hba_queue_depth = max_request_credit - retry_sz;
		goto retry_allocation;
	}

	if (retry_sz)
		printk(MPT2SAS_ERR_FMT "request pool: pci_alloc_consistent "
		    "succeed: hba_depth(%d), chains_per_io(%d), frame_sz(%d), "
		    "total(%d kb)\n", ioc->name, ioc->hba_queue_depth,
		    ioc->chains_needed_per_io, ioc->request_sz, sz/1024);


	/* hi-priority queue */
	ioc->hi_priority = ioc->request + ((ioc->scsiio_depth + 1) *
	    ioc->request_sz);
	ioc->hi_priority_dma = ioc->request_dma + ((ioc->scsiio_depth + 1) *
	    ioc->request_sz);

	/* internal queue */
	ioc->internal = ioc->hi_priority + (ioc->hi_priority_depth *
	    ioc->request_sz);
	ioc->internal_dma = ioc->hi_priority_dma + (ioc->hi_priority_depth *
	    ioc->request_sz);


	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "request pool(0x%p): "
	    "depth(%d), frame_size(%d), pool_size(%d kB)\n", ioc->name,
	    ioc->request, ioc->hba_queue_depth, ioc->request_sz,
	    (ioc->hba_queue_depth * ioc->request_sz)/1024));
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "request pool: dma(0x%llx)\n",
	    ioc->name, (unsigned long long) ioc->request_dma));
	total_sz += sz;

	sz = ioc->scsiio_depth * sizeof(struct request_tracker);
	ioc->scsi_lookup_pages = get_order(sz);
	ioc->scsi_lookup = (struct request_tracker *)__get_free_pages(
	    GFP_KERNEL, ioc->scsi_lookup_pages);
	if (!ioc->scsi_lookup) {
		printk(MPT2SAS_ERR_FMT "scsi_lookup: get_free_pages failed, "
		    "sz(%d)\n", ioc->name, (int)sz);
		goto out;
	}

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "scsiio(0x%p): "
	    "depth(%d)\n", ioc->name, ioc->request,
	    ioc->scsiio_depth));

	/* loop till the allocation succeeds */
	do {
		sz = ioc->chain_depth * sizeof(struct chain_tracker);
		ioc->chain_pages = get_order(sz);
		ioc->chain_lookup = (struct chain_tracker *)__get_free_pages(
		    GFP_KERNEL, ioc->chain_pages);
		if (ioc->chain_lookup == NULL)
			ioc->chain_depth -= 100;
	} while (ioc->chain_lookup == NULL);
	ioc->chain_dma_pool = pci_pool_create("chain pool", ioc->pdev,
	    ioc->request_sz, 16, 0);
	if (!ioc->chain_dma_pool) {
		printk(MPT2SAS_ERR_FMT "chain_dma_pool: pci_pool_create "
		    "failed\n", ioc->name);
		goto out;
	}
	for (i = 0; i < ioc->chain_depth; i++) {
		ioc->chain_lookup[i].chain_buffer = pci_pool_alloc(
		    ioc->chain_dma_pool , GFP_KERNEL,
		    &ioc->chain_lookup[i].chain_buffer_dma);
		if (!ioc->chain_lookup[i].chain_buffer) {
			ioc->chain_depth = i;
			goto chain_done;
		}
		total_sz += ioc->request_sz;
	}
chain_done:
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "chain pool depth"
	    "(%d), frame_size(%d), pool_size(%d kB)\n", ioc->name,
	    ioc->chain_depth, ioc->request_sz, ((ioc->chain_depth *
	    ioc->request_sz))/1024));

	/* initialize hi-priority queue smid's */
	ioc->hpr_lookup = kcalloc(ioc->hi_priority_depth,
	    sizeof(struct request_tracker), GFP_KERNEL);
	if (!ioc->hpr_lookup) {
		printk(MPT2SAS_ERR_FMT "hpr_lookup: kcalloc failed\n",
		    ioc->name);
		goto out;
	}
	ioc->hi_priority_smid = ioc->scsiio_depth + 1;
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "hi_priority(0x%p): "
	    "depth(%d), start smid(%d)\n", ioc->name, ioc->hi_priority,
	    ioc->hi_priority_depth, ioc->hi_priority_smid));

	/* initialize internal queue smid's */
	ioc->internal_lookup = kcalloc(ioc->internal_depth,
	    sizeof(struct request_tracker), GFP_KERNEL);
	if (!ioc->internal_lookup) {
		printk(MPT2SAS_ERR_FMT "internal_lookup: kcalloc failed\n",
		    ioc->name);
		goto out;
	}
	ioc->internal_smid = ioc->hi_priority_smid + ioc->hi_priority_depth;
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "internal(0x%p): "
	    "depth(%d), start smid(%d)\n", ioc->name, ioc->internal,
	     ioc->internal_depth, ioc->internal_smid));

	/* sense buffers, 4 byte align */
	sz = ioc->scsiio_depth * SCSI_SENSE_BUFFERSIZE;
	ioc->sense_dma_pool = pci_pool_create("sense pool", ioc->pdev, sz, 4,
	    0);
	if (!ioc->sense_dma_pool) {
		printk(MPT2SAS_ERR_FMT "sense pool: pci_pool_create failed\n",
		    ioc->name);
		goto out;
	}
	ioc->sense = pci_pool_alloc(ioc->sense_dma_pool , GFP_KERNEL,
	    &ioc->sense_dma);
	if (!ioc->sense) {
		printk(MPT2SAS_ERR_FMT "sense pool: pci_pool_alloc failed\n",
		    ioc->name);
		goto out;
	}
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT
	    "sense pool(0x%p): depth(%d), element_size(%d), pool_size"
	    "(%d kB)\n", ioc->name, ioc->sense, ioc->scsiio_depth,
	    SCSI_SENSE_BUFFERSIZE, sz/1024));
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "sense_dma(0x%llx)\n",
	    ioc->name, (unsigned long long)ioc->sense_dma));
	total_sz += sz;

	/* reply pool, 4 byte align */
	sz = ioc->reply_free_queue_depth * ioc->reply_sz;
	ioc->reply_dma_pool = pci_pool_create("reply pool", ioc->pdev, sz, 4,
	    0);
	if (!ioc->reply_dma_pool) {
		printk(MPT2SAS_ERR_FMT "reply pool: pci_pool_create failed\n",
		    ioc->name);
		goto out;
	}
	ioc->reply = pci_pool_alloc(ioc->reply_dma_pool , GFP_KERNEL,
	    &ioc->reply_dma);
	if (!ioc->reply) {
		printk(MPT2SAS_ERR_FMT "reply pool: pci_pool_alloc failed\n",
		    ioc->name);
		goto out;
	}
	ioc->reply_dma_min_address = (u32)(ioc->reply_dma);
	ioc->reply_dma_max_address = (u32)(ioc->reply_dma) + sz;
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "reply pool(0x%p): depth"
	    "(%d), frame_size(%d), pool_size(%d kB)\n", ioc->name, ioc->reply,
	    ioc->reply_free_queue_depth, ioc->reply_sz, sz/1024));
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "reply_dma(0x%llx)\n",
	    ioc->name, (unsigned long long)ioc->reply_dma));
	total_sz += sz;

	/* reply free queue, 16 byte align */
	sz = ioc->reply_free_queue_depth * 4;
	ioc->reply_free_dma_pool = pci_pool_create("reply_free pool",
	    ioc->pdev, sz, 16, 0);
	if (!ioc->reply_free_dma_pool) {
		printk(MPT2SAS_ERR_FMT "reply_free pool: pci_pool_create "
		    "failed\n", ioc->name);
		goto out;
	}
	ioc->reply_free = pci_pool_alloc(ioc->reply_free_dma_pool , GFP_KERNEL,
	    &ioc->reply_free_dma);
	if (!ioc->reply_free) {
		printk(MPT2SAS_ERR_FMT "reply_free pool: pci_pool_alloc "
		    "failed\n", ioc->name);
		goto out;
	}
	memset(ioc->reply_free, 0, sz);
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "reply_free pool(0x%p): "
	    "depth(%d), element_size(%d), pool_size(%d kB)\n", ioc->name,
	    ioc->reply_free, ioc->reply_free_queue_depth, 4, sz/1024));
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "reply_free_dma"
	    "(0x%llx)\n", ioc->name, (unsigned long long)ioc->reply_free_dma));
	total_sz += sz;

	/* reply post queue, 16 byte align */
	sz = ioc->reply_post_queue_depth * sizeof(Mpi2DefaultReplyDescriptor_t);
	ioc->reply_post_free_dma_pool = pci_pool_create("reply_post_free pool",
	    ioc->pdev, sz, 16, 0);
	if (!ioc->reply_post_free_dma_pool) {
		printk(MPT2SAS_ERR_FMT "reply_post_free pool: pci_pool_create "
		    "failed\n", ioc->name);
		goto out;
	}
	ioc->reply_post_free = pci_pool_alloc(ioc->reply_post_free_dma_pool ,
	    GFP_KERNEL, &ioc->reply_post_free_dma);
	if (!ioc->reply_post_free) {
		printk(MPT2SAS_ERR_FMT "reply_post_free pool: pci_pool_alloc "
		    "failed\n", ioc->name);
		goto out;
	}
	memset(ioc->reply_post_free, 0, sz);
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "reply post free pool"
	    "(0x%p): depth(%d), element_size(%d), pool_size(%d kB)\n",
	    ioc->name, ioc->reply_post_free, ioc->reply_post_queue_depth, 8,
	    sz/1024));
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "reply_post_free_dma = "
	    "(0x%llx)\n", ioc->name, (unsigned long long)
	    ioc->reply_post_free_dma));
	total_sz += sz;

	ioc->config_page_sz = 512;
	ioc->config_page = pci_alloc_consistent(ioc->pdev,
	    ioc->config_page_sz, &ioc->config_page_dma);
	if (!ioc->config_page) {
		printk(MPT2SAS_ERR_FMT "config page: pci_pool_alloc "
		    "failed\n", ioc->name);
		goto out;
	}
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "config page(0x%p): size"
	    "(%d)\n", ioc->name, ioc->config_page, ioc->config_page_sz));
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "config_page_dma"
	    "(0x%llx)\n", ioc->name, (unsigned long long)ioc->config_page_dma));
	total_sz += ioc->config_page_sz;

	printk(MPT2SAS_INFO_FMT "Allocated physical memory: size(%d kB)\n",
	    ioc->name, total_sz/1024);
	printk(MPT2SAS_INFO_FMT "Current Controller Queue Depth(%d), "
	    "Max Controller Queue Depth(%d)\n",
	    ioc->name, ioc->shost->can_queue, facts->RequestCredit);
	printk(MPT2SAS_INFO_FMT "Scatter Gather Elements per IO(%d)\n",
	    ioc->name, ioc->shost->sg_tablesize);
	return 0;

 out:
	return -ENOMEM;
}


/**
 * mpt2sas_base_get_iocstate - Get the current state of a MPT adapter.
 * @ioc: Pointer to MPT_ADAPTER structure
 * @cooked: Request raw or cooked IOC state
 *
 * Returns all IOC Doorbell register bits if cooked==0, else just the
 * Doorbell bits in MPI_IOC_STATE_MASK.
 */
u32
mpt2sas_base_get_iocstate(struct MPT2SAS_ADAPTER *ioc, int cooked)
{
	u32 s, sc;

	s = readl(&ioc->chip->Doorbell);
	sc = s & MPI2_IOC_STATE_MASK;
	return cooked ? sc : s;
}

/**
 * _base_wait_on_iocstate - waiting on a particular ioc state
 * @ioc_state: controller state { READY, OPERATIONAL, or RESET }
 * @timeout: timeout in second
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_wait_on_iocstate(struct MPT2SAS_ADAPTER *ioc, u32 ioc_state, int timeout,
    int sleep_flag)
{
	u32 count, cntdn;
	u32 current_state;

	count = 0;
	cntdn = (sleep_flag == CAN_SLEEP) ? 1000*timeout : 2000*timeout;
	do {
		current_state = mpt2sas_base_get_iocstate(ioc, 1);
		if (current_state == ioc_state)
			return 0;
		if (count && current_state == MPI2_IOC_STATE_FAULT)
			break;
		if (sleep_flag == CAN_SLEEP)
			msleep(1);
		else
			udelay(500);
		count++;
	} while (--cntdn);

	return current_state;
}

/**
 * _base_wait_for_doorbell_int - waiting for controller interrupt(generated by
 * a write to the doorbell)
 * @ioc: per adapter object
 * @timeout: timeout in second
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 *
 * Notes: MPI2_HIS_IOC2SYS_DB_STATUS - set to one when IOC writes to doorbell.
 */
static int
_base_wait_for_doorbell_int(struct MPT2SAS_ADAPTER *ioc, int timeout,
    int sleep_flag)
{
	u32 cntdn, count;
	u32 int_status;

	count = 0;
	cntdn = (sleep_flag == CAN_SLEEP) ? 1000*timeout : 2000*timeout;
	do {
		int_status = readl(&ioc->chip->HostInterruptStatus);
		if (int_status & MPI2_HIS_IOC2SYS_DB_STATUS) {
			dhsprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: "
			    "successfull count(%d), timeout(%d)\n", ioc->name,
			    __func__, count, timeout));
			return 0;
		}
		if (sleep_flag == CAN_SLEEP)
			msleep(1);
		else
			udelay(500);
		count++;
	} while (--cntdn);

	printk(MPT2SAS_ERR_FMT "%s: failed due to timeout count(%d), "
	    "int_status(%x)!\n", ioc->name, __func__, count, int_status);
	return -EFAULT;
}

/**
 * _base_wait_for_doorbell_ack - waiting for controller to read the doorbell.
 * @ioc: per adapter object
 * @timeout: timeout in second
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 *
 * Notes: MPI2_HIS_SYS2IOC_DB_STATUS - set to one when host writes to
 * doorbell.
 */
static int
_base_wait_for_doorbell_ack(struct MPT2SAS_ADAPTER *ioc, int timeout,
    int sleep_flag)
{
	u32 cntdn, count;
	u32 int_status;
	u32 doorbell;

	count = 0;
	cntdn = (sleep_flag == CAN_SLEEP) ? 1000*timeout : 2000*timeout;
	do {
		int_status = readl(&ioc->chip->HostInterruptStatus);
		if (!(int_status & MPI2_HIS_SYS2IOC_DB_STATUS)) {
			dhsprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: "
			    "successfull count(%d), timeout(%d)\n", ioc->name,
			    __func__, count, timeout));
			return 0;
		} else if (int_status & MPI2_HIS_IOC2SYS_DB_STATUS) {
			doorbell = readl(&ioc->chip->Doorbell);
			if ((doorbell & MPI2_IOC_STATE_MASK) ==
			    MPI2_IOC_STATE_FAULT) {
				mpt2sas_base_fault_info(ioc , doorbell);
				return -EFAULT;
			}
		} else if (int_status == 0xFFFFFFFF)
			goto out;

		if (sleep_flag == CAN_SLEEP)
			msleep(1);
		else
			udelay(500);
		count++;
	} while (--cntdn);

 out:
	printk(MPT2SAS_ERR_FMT "%s: failed due to timeout count(%d), "
	    "int_status(%x)!\n", ioc->name, __func__, count, int_status);
	return -EFAULT;
}

/**
 * _base_wait_for_doorbell_not_used - waiting for doorbell to not be in use
 * @ioc: per adapter object
 * @timeout: timeout in second
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 *
 */
static int
_base_wait_for_doorbell_not_used(struct MPT2SAS_ADAPTER *ioc, int timeout,
    int sleep_flag)
{
	u32 cntdn, count;
	u32 doorbell_reg;

	count = 0;
	cntdn = (sleep_flag == CAN_SLEEP) ? 1000*timeout : 2000*timeout;
	do {
		doorbell_reg = readl(&ioc->chip->Doorbell);
		if (!(doorbell_reg & MPI2_DOORBELL_USED)) {
			dhsprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: "
			    "successfull count(%d), timeout(%d)\n", ioc->name,
			    __func__, count, timeout));
			return 0;
		}
		if (sleep_flag == CAN_SLEEP)
			msleep(1);
		else
			udelay(500);
		count++;
	} while (--cntdn);

	printk(MPT2SAS_ERR_FMT "%s: failed due to timeout count(%d), "
	    "doorbell_reg(%x)!\n", ioc->name, __func__, count, doorbell_reg);
	return -EFAULT;
}

/**
 * _base_send_ioc_reset - send doorbell reset
 * @ioc: per adapter object
 * @reset_type: currently only supports: MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET
 * @timeout: timeout in second
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_send_ioc_reset(struct MPT2SAS_ADAPTER *ioc, u8 reset_type, int timeout,
    int sleep_flag)
{
	u32 ioc_state;
	int r = 0;

	if (reset_type != MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET) {
		printk(MPT2SAS_ERR_FMT "%s: unknown reset_type\n",
		    ioc->name, __func__);
		return -EFAULT;
	}

	if (!(ioc->facts.IOCCapabilities &
	   MPI2_IOCFACTS_CAPABILITY_EVENT_REPLAY))
		return -EFAULT;

	printk(MPT2SAS_INFO_FMT "sending message unit reset !!\n", ioc->name);

	writel(reset_type << MPI2_DOORBELL_FUNCTION_SHIFT,
	    &ioc->chip->Doorbell);
	if ((_base_wait_for_doorbell_ack(ioc, 15, sleep_flag))) {
		r = -EFAULT;
		goto out;
	}
	ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_READY,
	    timeout, sleep_flag);
	if (ioc_state) {
		printk(MPT2SAS_ERR_FMT "%s: failed going to ready state "
		    " (ioc_state=0x%x)\n", ioc->name, __func__, ioc_state);
		r = -EFAULT;
		goto out;
	}
 out:
	printk(MPT2SAS_INFO_FMT "message unit reset: %s\n",
	    ioc->name, ((r == 0) ? "SUCCESS" : "FAILED"));
	return r;
}

/**
 * _base_handshake_req_reply_wait - send request thru doorbell interface
 * @ioc: per adapter object
 * @request_bytes: request length
 * @request: pointer having request payload
 * @reply_bytes: reply length
 * @reply: pointer to reply payload
 * @timeout: timeout in second
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_handshake_req_reply_wait(struct MPT2SAS_ADAPTER *ioc, int request_bytes,
    u32 *request, int reply_bytes, u16 *reply, int timeout, int sleep_flag)
{
	MPI2DefaultReply_t *default_reply = (MPI2DefaultReply_t *)reply;
	int i;
	u8 failed;
	u16 dummy;
	u32 *mfp;

	/* make sure doorbell is not in use */
	if ((readl(&ioc->chip->Doorbell) & MPI2_DOORBELL_USED)) {
		printk(MPT2SAS_ERR_FMT "doorbell is in use "
		    " (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}

	/* clear pending doorbell interrupts from previous state changes */
	if (readl(&ioc->chip->HostInterruptStatus) &
	    MPI2_HIS_IOC2SYS_DB_STATUS)
		writel(0, &ioc->chip->HostInterruptStatus);

	/* send message to ioc */
	writel(((MPI2_FUNCTION_HANDSHAKE<<MPI2_DOORBELL_FUNCTION_SHIFT) |
	    ((request_bytes/4)<<MPI2_DOORBELL_ADD_DWORDS_SHIFT)),
	    &ioc->chip->Doorbell);

	if ((_base_wait_for_doorbell_int(ioc, 5, NO_SLEEP))) {
		printk(MPT2SAS_ERR_FMT "doorbell handshake "
		   "int failed (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}
	writel(0, &ioc->chip->HostInterruptStatus);

	if ((_base_wait_for_doorbell_ack(ioc, 5, sleep_flag))) {
		printk(MPT2SAS_ERR_FMT "doorbell handshake "
		    "ack failed (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}

	/* send message 32-bits at a time */
	for (i = 0, failed = 0; i < request_bytes/4 && !failed; i++) {
		writel(cpu_to_le32(request[i]), &ioc->chip->Doorbell);
		if ((_base_wait_for_doorbell_ack(ioc, 5, sleep_flag)))
			failed = 1;
	}

	if (failed) {
		printk(MPT2SAS_ERR_FMT "doorbell handshake "
		    "sending request failed (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}

	/* now wait for the reply */
	if ((_base_wait_for_doorbell_int(ioc, timeout, sleep_flag))) {
		printk(MPT2SAS_ERR_FMT "doorbell handshake "
		   "int failed (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}

	/* read the first two 16-bits, it gives the total length of the reply */
	reply[0] = le16_to_cpu(readl(&ioc->chip->Doorbell)
	    & MPI2_DOORBELL_DATA_MASK);
	writel(0, &ioc->chip->HostInterruptStatus);
	if ((_base_wait_for_doorbell_int(ioc, 5, sleep_flag))) {
		printk(MPT2SAS_ERR_FMT "doorbell handshake "
		   "int failed (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}
	reply[1] = le16_to_cpu(readl(&ioc->chip->Doorbell)
	    & MPI2_DOORBELL_DATA_MASK);
	writel(0, &ioc->chip->HostInterruptStatus);

	for (i = 2; i < default_reply->MsgLength * 2; i++)  {
		if ((_base_wait_for_doorbell_int(ioc, 5, sleep_flag))) {
			printk(MPT2SAS_ERR_FMT "doorbell "
			    "handshake int failed (line=%d)\n", ioc->name,
			    __LINE__);
			return -EFAULT;
		}
		if (i >=  reply_bytes/2) /* overflow case */
			dummy = readl(&ioc->chip->Doorbell);
		else
			reply[i] = le16_to_cpu(readl(&ioc->chip->Doorbell)
			    & MPI2_DOORBELL_DATA_MASK);
		writel(0, &ioc->chip->HostInterruptStatus);
	}

	_base_wait_for_doorbell_int(ioc, 5, sleep_flag);
	if (_base_wait_for_doorbell_not_used(ioc, 5, sleep_flag) != 0) {
		dhsprintk(ioc, printk(MPT2SAS_INFO_FMT "doorbell is in use "
		    " (line=%d)\n", ioc->name, __LINE__));
	}
	writel(0, &ioc->chip->HostInterruptStatus);

	if (ioc->logging_level & MPT_DEBUG_INIT) {
		mfp = (u32 *)reply;
		printk(KERN_INFO "\toffset:data\n");
		for (i = 0; i < reply_bytes/4; i++)
			printk(KERN_INFO "\t[0x%02x]:%08x\n", i*4,
			    le32_to_cpu(mfp[i]));
	}
	return 0;
}

/**
 * mpt2sas_base_sas_iounit_control - send sas iounit control to FW
 * @ioc: per adapter object
 * @mpi_reply: the reply payload from FW
 * @mpi_request: the request payload sent to FW
 *
 * The SAS IO Unit Control Request message allows the host to perform low-level
 * operations, such as resets on the PHYs of the IO Unit, also allows the host
 * to obtain the IOC assigned device handles for a device if it has other
 * identifying information about the device, in addition allows the host to
 * remove IOC resources associated with the device.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_base_sas_iounit_control(struct MPT2SAS_ADAPTER *ioc,
    Mpi2SasIoUnitControlReply_t *mpi_reply,
    Mpi2SasIoUnitControlRequest_t *mpi_request)
{
	u16 smid;
	u32 ioc_state;
	unsigned long timeleft;
	u8 issue_reset;
	int rc;
	void *request;
	u16 wait_state_count;

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	mutex_lock(&ioc->base_cmds.mutex);

	if (ioc->base_cmds.status != MPT2_CMD_NOT_USED) {
		printk(MPT2SAS_ERR_FMT "%s: base_cmd in use\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	wait_state_count = 0;
	ioc_state = mpt2sas_base_get_iocstate(ioc, 1);
	while (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		if (wait_state_count++ == 10) {
			printk(MPT2SAS_ERR_FMT
			    "%s: failed due to ioc not operational\n",
			    ioc->name, __func__);
			rc = -EFAULT;
			goto out;
		}
		ssleep(1);
		ioc_state = mpt2sas_base_get_iocstate(ioc, 1);
		printk(MPT2SAS_INFO_FMT "%s: waiting for "
		    "operational state(count=%d)\n", ioc->name,
		    __func__, wait_state_count);
	}

	smid = mpt2sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->base_cmds.status = MPT2_CMD_PENDING;
	request = mpt2sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memcpy(request, mpi_request, sizeof(Mpi2SasIoUnitControlRequest_t));
	if (mpi_request->Operation == MPI2_SAS_OP_PHY_HARD_RESET ||
	    mpi_request->Operation == MPI2_SAS_OP_PHY_LINK_RESET)
		ioc->ioc_link_reset_in_progress = 1;
	mpt2sas_base_put_smid_default(ioc, smid);
	init_completion(&ioc->base_cmds.done);
	timeleft = wait_for_completion_timeout(&ioc->base_cmds.done,
	    msecs_to_jiffies(10000));
	if ((mpi_request->Operation == MPI2_SAS_OP_PHY_HARD_RESET ||
	    mpi_request->Operation == MPI2_SAS_OP_PHY_LINK_RESET) &&
	    ioc->ioc_link_reset_in_progress)
		ioc->ioc_link_reset_in_progress = 0;
	if (!(ioc->base_cmds.status & MPT2_CMD_COMPLETE)) {
		printk(MPT2SAS_ERR_FMT "%s: timeout\n",
		    ioc->name, __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2SasIoUnitControlRequest_t)/4);
		if (!(ioc->base_cmds.status & MPT2_CMD_RESET))
			issue_reset = 1;
		goto issue_host_reset;
	}
	if (ioc->base_cmds.status & MPT2_CMD_REPLY_VALID)
		memcpy(mpi_reply, ioc->base_cmds.reply,
		    sizeof(Mpi2SasIoUnitControlReply_t));
	else
		memset(mpi_reply, 0, sizeof(Mpi2SasIoUnitControlReply_t));
	ioc->base_cmds.status = MPT2_CMD_NOT_USED;
	goto out;

 issue_host_reset:
	if (issue_reset)
		mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP,
		    FORCE_BIG_HAMMER);
	ioc->base_cmds.status = MPT2_CMD_NOT_USED;
	rc = -EFAULT;
 out:
	mutex_unlock(&ioc->base_cmds.mutex);
	return rc;
}


/**
 * mpt2sas_base_scsi_enclosure_processor - sending request to sep device
 * @ioc: per adapter object
 * @mpi_reply: the reply payload from FW
 * @mpi_request: the request payload sent to FW
 *
 * The SCSI Enclosure Processor request message causes the IOC to
 * communicate with SES devices to control LED status signals.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_base_scsi_enclosure_processor(struct MPT2SAS_ADAPTER *ioc,
    Mpi2SepReply_t *mpi_reply, Mpi2SepRequest_t *mpi_request)
{
	u16 smid;
	u32 ioc_state;
	unsigned long timeleft;
	u8 issue_reset;
	int rc;
	void *request;
	u16 wait_state_count;

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	mutex_lock(&ioc->base_cmds.mutex);

	if (ioc->base_cmds.status != MPT2_CMD_NOT_USED) {
		printk(MPT2SAS_ERR_FMT "%s: base_cmd in use\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	wait_state_count = 0;
	ioc_state = mpt2sas_base_get_iocstate(ioc, 1);
	while (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		if (wait_state_count++ == 10) {
			printk(MPT2SAS_ERR_FMT
			    "%s: failed due to ioc not operational\n",
			    ioc->name, __func__);
			rc = -EFAULT;
			goto out;
		}
		ssleep(1);
		ioc_state = mpt2sas_base_get_iocstate(ioc, 1);
		printk(MPT2SAS_INFO_FMT "%s: waiting for "
		    "operational state(count=%d)\n", ioc->name,
		    __func__, wait_state_count);
	}

	smid = mpt2sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->base_cmds.status = MPT2_CMD_PENDING;
	request = mpt2sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memcpy(request, mpi_request, sizeof(Mpi2SepReply_t));
	mpt2sas_base_put_smid_default(ioc, smid);
	init_completion(&ioc->base_cmds.done);
	timeleft = wait_for_completion_timeout(&ioc->base_cmds.done,
	    msecs_to_jiffies(10000));
	if (!(ioc->base_cmds.status & MPT2_CMD_COMPLETE)) {
		printk(MPT2SAS_ERR_FMT "%s: timeout\n",
		    ioc->name, __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2SepRequest_t)/4);
		if (!(ioc->base_cmds.status & MPT2_CMD_RESET))
			issue_reset = 1;
		goto issue_host_reset;
	}
	if (ioc->base_cmds.status & MPT2_CMD_REPLY_VALID)
		memcpy(mpi_reply, ioc->base_cmds.reply,
		    sizeof(Mpi2SepReply_t));
	else
		memset(mpi_reply, 0, sizeof(Mpi2SepReply_t));
	ioc->base_cmds.status = MPT2_CMD_NOT_USED;
	goto out;

 issue_host_reset:
	if (issue_reset)
		mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP,
		    FORCE_BIG_HAMMER);
	ioc->base_cmds.status = MPT2_CMD_NOT_USED;
	rc = -EFAULT;
 out:
	mutex_unlock(&ioc->base_cmds.mutex);
	return rc;
}

/**
 * _base_get_port_facts - obtain port facts reply and save in ioc
 * @ioc: per adapter object
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_get_port_facts(struct MPT2SAS_ADAPTER *ioc, int port, int sleep_flag)
{
	Mpi2PortFactsRequest_t mpi_request;
	Mpi2PortFactsReply_t mpi_reply, *pfacts;
	int mpi_reply_sz, mpi_request_sz, r;

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	mpi_reply_sz = sizeof(Mpi2PortFactsReply_t);
	mpi_request_sz = sizeof(Mpi2PortFactsRequest_t);
	memset(&mpi_request, 0, mpi_request_sz);
	mpi_request.Function = MPI2_FUNCTION_PORT_FACTS;
	mpi_request.PortNumber = port;
	r = _base_handshake_req_reply_wait(ioc, mpi_request_sz,
	    (u32 *)&mpi_request, mpi_reply_sz, (u16 *)&mpi_reply, 5, CAN_SLEEP);

	if (r != 0) {
		printk(MPT2SAS_ERR_FMT "%s: handshake failed (r=%d)\n",
		    ioc->name, __func__, r);
		return r;
	}

	pfacts = &ioc->pfacts[port];
	memset(pfacts, 0, sizeof(Mpi2PortFactsReply_t));
	pfacts->PortNumber = mpi_reply.PortNumber;
	pfacts->VP_ID = mpi_reply.VP_ID;
	pfacts->VF_ID = mpi_reply.VF_ID;
	pfacts->MaxPostedCmdBuffers =
	    le16_to_cpu(mpi_reply.MaxPostedCmdBuffers);

	return 0;
}

/**
 * _base_get_ioc_facts - obtain ioc facts reply and save in ioc
 * @ioc: per adapter object
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_get_ioc_facts(struct MPT2SAS_ADAPTER *ioc, int sleep_flag)
{
	Mpi2IOCFactsRequest_t mpi_request;
	Mpi2IOCFactsReply_t mpi_reply, *facts;
	int mpi_reply_sz, mpi_request_sz, r;

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	mpi_reply_sz = sizeof(Mpi2IOCFactsReply_t);
	mpi_request_sz = sizeof(Mpi2IOCFactsRequest_t);
	memset(&mpi_request, 0, mpi_request_sz);
	mpi_request.Function = MPI2_FUNCTION_IOC_FACTS;
	r = _base_handshake_req_reply_wait(ioc, mpi_request_sz,
	    (u32 *)&mpi_request, mpi_reply_sz, (u16 *)&mpi_reply, 5, CAN_SLEEP);

	if (r != 0) {
		printk(MPT2SAS_ERR_FMT "%s: handshake failed (r=%d)\n",
		    ioc->name, __func__, r);
		return r;
	}

	facts = &ioc->facts;
	memset(facts, 0, sizeof(Mpi2IOCFactsReply_t));
	facts->MsgVersion = le16_to_cpu(mpi_reply.MsgVersion);
	facts->HeaderVersion = le16_to_cpu(mpi_reply.HeaderVersion);
	facts->VP_ID = mpi_reply.VP_ID;
	facts->VF_ID = mpi_reply.VF_ID;
	facts->IOCExceptions = le16_to_cpu(mpi_reply.IOCExceptions);
	facts->MaxChainDepth = mpi_reply.MaxChainDepth;
	facts->WhoInit = mpi_reply.WhoInit;
	facts->NumberOfPorts = mpi_reply.NumberOfPorts;
	facts->RequestCredit = le16_to_cpu(mpi_reply.RequestCredit);
	facts->MaxReplyDescriptorPostQueueDepth =
	    le16_to_cpu(mpi_reply.MaxReplyDescriptorPostQueueDepth);
	facts->ProductID = le16_to_cpu(mpi_reply.ProductID);
	facts->IOCCapabilities = le32_to_cpu(mpi_reply.IOCCapabilities);
	if ((facts->IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_INTEGRATED_RAID))
		ioc->ir_firmware = 1;
	facts->FWVersion.Word = le32_to_cpu(mpi_reply.FWVersion.Word);
	facts->IOCRequestFrameSize =
	    le16_to_cpu(mpi_reply.IOCRequestFrameSize);
	facts->MaxInitiators = le16_to_cpu(mpi_reply.MaxInitiators);
	facts->MaxTargets = le16_to_cpu(mpi_reply.MaxTargets);
	ioc->shost->max_id = -1;
	facts->MaxSasExpanders = le16_to_cpu(mpi_reply.MaxSasExpanders);
	facts->MaxEnclosures = le16_to_cpu(mpi_reply.MaxEnclosures);
	facts->ProtocolFlags = le16_to_cpu(mpi_reply.ProtocolFlags);
	facts->HighPriorityCredit =
	    le16_to_cpu(mpi_reply.HighPriorityCredit);
	facts->ReplyFrameSize = mpi_reply.ReplyFrameSize;
	facts->MaxDevHandle = le16_to_cpu(mpi_reply.MaxDevHandle);

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "hba queue depth(%d), "
	    "max chains per io(%d)\n", ioc->name, facts->RequestCredit,
	    facts->MaxChainDepth));
	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "request frame size(%d), "
	    "reply frame size(%d)\n", ioc->name,
	    facts->IOCRequestFrameSize * 4, facts->ReplyFrameSize * 4));
	return 0;
}

/**
 * _base_send_ioc_init - send ioc_init to firmware
 * @ioc: per adapter object
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_send_ioc_init(struct MPT2SAS_ADAPTER *ioc, int sleep_flag)
{
	Mpi2IOCInitRequest_t mpi_request;
	Mpi2IOCInitReply_t mpi_reply;
	int r;
	struct timeval current_time;
	u16 ioc_status;

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	memset(&mpi_request, 0, sizeof(Mpi2IOCInitRequest_t));
	mpi_request.Function = MPI2_FUNCTION_IOC_INIT;
	mpi_request.WhoInit = MPI2_WHOINIT_HOST_DRIVER;
	mpi_request.VF_ID = 0; /* TODO */
	mpi_request.VP_ID = 0;
	mpi_request.MsgVersion = cpu_to_le16(MPI2_VERSION);
	mpi_request.HeaderVersion = cpu_to_le16(MPI2_HEADER_VERSION);

	/* In MPI Revision I (0xA), the SystemReplyFrameSize(offset 0x18) was
	 * removed and made reserved.  For those with older firmware will need
	 * this fix. It was decided that the Reply and Request frame sizes are
	 * the same.
	 */
	if ((ioc->facts.HeaderVersion >> 8) < 0xA) {
		mpi_request.Reserved7 = cpu_to_le16(ioc->reply_sz);
/*		mpi_request.SystemReplyFrameSize =
 *		 cpu_to_le16(ioc->reply_sz);
 */
	}

	mpi_request.SystemRequestFrameSize = cpu_to_le16(ioc->request_sz/4);
	mpi_request.ReplyDescriptorPostQueueDepth =
	    cpu_to_le16(ioc->reply_post_queue_depth);
	mpi_request.ReplyFreeQueueDepth =
	    cpu_to_le16(ioc->reply_free_queue_depth);

#if BITS_PER_LONG > 32
	mpi_request.SenseBufferAddressHigh =
	    cpu_to_le32(ioc->sense_dma >> 32);
	mpi_request.SystemReplyAddressHigh =
	    cpu_to_le32(ioc->reply_dma >> 32);
	mpi_request.SystemRequestFrameBaseAddress =
	    cpu_to_le64(ioc->request_dma);
	mpi_request.ReplyFreeQueueAddress =
	    cpu_to_le64(ioc->reply_free_dma);
	mpi_request.ReplyDescriptorPostQueueAddress =
	    cpu_to_le64(ioc->reply_post_free_dma);
#else
	mpi_request.SystemRequestFrameBaseAddress =
	    cpu_to_le32(ioc->request_dma);
	mpi_request.ReplyFreeQueueAddress =
	    cpu_to_le32(ioc->reply_free_dma);
	mpi_request.ReplyDescriptorPostQueueAddress =
	    cpu_to_le32(ioc->reply_post_free_dma);
#endif

	/* This time stamp specifies number of milliseconds
	 * since epoch ~ midnight January 1, 1970.
	 */
	do_gettimeofday(&current_time);
	mpi_request.TimeStamp = cpu_to_le64((u64)current_time.tv_sec * 1000 +
	    (current_time.tv_usec / 1000));

	if (ioc->logging_level & MPT_DEBUG_INIT) {
		u32 *mfp;
		int i;

		mfp = (u32 *)&mpi_request;
		printk(KERN_INFO "\toffset:data\n");
		for (i = 0; i < sizeof(Mpi2IOCInitRequest_t)/4; i++)
			printk(KERN_INFO "\t[0x%02x]:%08x\n", i*4,
			    le32_to_cpu(mfp[i]));
	}

	r = _base_handshake_req_reply_wait(ioc,
	    sizeof(Mpi2IOCInitRequest_t), (u32 *)&mpi_request,
	    sizeof(Mpi2IOCInitReply_t), (u16 *)&mpi_reply, 10,
	    sleep_flag);

	if (r != 0) {
		printk(MPT2SAS_ERR_FMT "%s: handshake failed (r=%d)\n",
		    ioc->name, __func__, r);
		return r;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS ||
	    mpi_reply.IOCLogInfo) {
		printk(MPT2SAS_ERR_FMT "%s: failed\n", ioc->name, __func__);
		r = -EIO;
	}

	return 0;
}

/**
 * _base_send_port_enable - send port_enable(discovery stuff) to firmware
 * @ioc: per adapter object
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_send_port_enable(struct MPT2SAS_ADAPTER *ioc, int sleep_flag)
{
	Mpi2PortEnableRequest_t *mpi_request;
	u32 ioc_state;
	unsigned long timeleft;
	int r = 0;
	u16 smid;

	printk(MPT2SAS_INFO_FMT "sending port enable !!\n", ioc->name);

	if (ioc->base_cmds.status & MPT2_CMD_PENDING) {
		printk(MPT2SAS_ERR_FMT "%s: internal command already in use\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	smid = mpt2sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	ioc->base_cmds.status = MPT2_CMD_PENDING;
	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2PortEnableRequest_t));
	mpi_request->Function = MPI2_FUNCTION_PORT_ENABLE;
	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;

	mpt2sas_base_put_smid_default(ioc, smid);
	init_completion(&ioc->base_cmds.done);
	timeleft = wait_for_completion_timeout(&ioc->base_cmds.done,
	    300*HZ);
	if (!(ioc->base_cmds.status & MPT2_CMD_COMPLETE)) {
		printk(MPT2SAS_ERR_FMT "%s: timeout\n",
		    ioc->name, __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2PortEnableRequest_t)/4);
		if (ioc->base_cmds.status & MPT2_CMD_RESET)
			r = -EFAULT;
		else
			r = -ETIME;
		goto out;
	} else
		dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: complete\n",
		    ioc->name, __func__));

	ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_OPERATIONAL,
	    60, sleep_flag);
	if (ioc_state) {
		printk(MPT2SAS_ERR_FMT "%s: failed going to operational state "
		    " (ioc_state=0x%x)\n", ioc->name, __func__, ioc_state);
		r = -EFAULT;
	}
 out:
	ioc->base_cmds.status = MPT2_CMD_NOT_USED;
	printk(MPT2SAS_INFO_FMT "port enable: %s\n",
	    ioc->name, ((r == 0) ? "SUCCESS" : "FAILED"));
	return r;
}

/**
 * _base_unmask_events - turn on notification for this event
 * @ioc: per adapter object
 * @event: firmware event
 *
 * The mask is stored in ioc->event_masks.
 */
static void
_base_unmask_events(struct MPT2SAS_ADAPTER *ioc, u16 event)
{
	u32 desired_event;

	if (event >= 128)
		return;

	desired_event = (1 << (event % 32));

	if (event < 32)
		ioc->event_masks[0] &= ~desired_event;
	else if (event < 64)
		ioc->event_masks[1] &= ~desired_event;
	else if (event < 96)
		ioc->event_masks[2] &= ~desired_event;
	else if (event < 128)
		ioc->event_masks[3] &= ~desired_event;
}

/**
 * _base_event_notification - send event notification
 * @ioc: per adapter object
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_event_notification(struct MPT2SAS_ADAPTER *ioc, int sleep_flag)
{
	Mpi2EventNotificationRequest_t *mpi_request;
	unsigned long timeleft;
	u16 smid;
	int r = 0;
	int i;

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	if (ioc->base_cmds.status & MPT2_CMD_PENDING) {
		printk(MPT2SAS_ERR_FMT "%s: internal command already in use\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	smid = mpt2sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}
	ioc->base_cmds.status = MPT2_CMD_PENDING;
	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2EventNotificationRequest_t));
	mpi_request->Function = MPI2_FUNCTION_EVENT_NOTIFICATION;
	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;
	for (i = 0; i < MPI2_EVENT_NOTIFY_EVENTMASK_WORDS; i++)
		mpi_request->EventMasks[i] =
		    cpu_to_le32(ioc->event_masks[i]);
	mpt2sas_base_put_smid_default(ioc, smid);
	init_completion(&ioc->base_cmds.done);
	timeleft = wait_for_completion_timeout(&ioc->base_cmds.done, 30*HZ);
	if (!(ioc->base_cmds.status & MPT2_CMD_COMPLETE)) {
		printk(MPT2SAS_ERR_FMT "%s: timeout\n",
		    ioc->name, __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2EventNotificationRequest_t)/4);
		if (ioc->base_cmds.status & MPT2_CMD_RESET)
			r = -EFAULT;
		else
			r = -ETIME;
	} else
		dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: complete\n",
		    ioc->name, __func__));
	ioc->base_cmds.status = MPT2_CMD_NOT_USED;
	return r;
}

/**
 * mpt2sas_base_validate_event_type - validating event types
 * @ioc: per adapter object
 * @event: firmware event
 *
 * This will turn on firmware event notification when application
 * ask for that event. We don't mask events that are already enabled.
 */
void
mpt2sas_base_validate_event_type(struct MPT2SAS_ADAPTER *ioc, u32 *event_type)
{
	int i, j;
	u32 event_mask, desired_event;
	u8 send_update_to_fw;

	for (i = 0, send_update_to_fw = 0; i <
	    MPI2_EVENT_NOTIFY_EVENTMASK_WORDS; i++) {
		event_mask = ~event_type[i];
		desired_event = 1;
		for (j = 0; j < 32; j++) {
			if (!(event_mask & desired_event) &&
			    (ioc->event_masks[i] & desired_event)) {
				ioc->event_masks[i] &= ~desired_event;
				send_update_to_fw = 1;
			}
			desired_event = (desired_event << 1);
		}
	}

	if (!send_update_to_fw)
		return;

	mutex_lock(&ioc->base_cmds.mutex);
	_base_event_notification(ioc, CAN_SLEEP);
	mutex_unlock(&ioc->base_cmds.mutex);
}

/**
 * _base_diag_reset - the "big hammer" start of day reset
 * @ioc: per adapter object
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_diag_reset(struct MPT2SAS_ADAPTER *ioc, int sleep_flag)
{
	u32 host_diagnostic;
	u32 ioc_state;
	u32 count;
	u32 hcb_size;

	printk(MPT2SAS_INFO_FMT "sending diag reset !!\n", ioc->name);

	_base_save_msix_table(ioc);

	drsprintk(ioc, printk(MPT2SAS_INFO_FMT "clear interrupts\n",
	    ioc->name));

	count = 0;
	do {
		/* Write magic sequence to WriteSequence register
		 * Loop until in diagnostic mode
		 */
		drsprintk(ioc, printk(MPT2SAS_INFO_FMT "write magic "
		    "sequence\n", ioc->name));
		writel(MPI2_WRSEQ_FLUSH_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_1ST_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_2ND_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_3RD_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_4TH_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_5TH_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_6TH_KEY_VALUE, &ioc->chip->WriteSequence);

		/* wait 100 msec */
		if (sleep_flag == CAN_SLEEP)
			msleep(100);
		else
			mdelay(100);

		if (count++ > 20)
			goto out;

		host_diagnostic = readl(&ioc->chip->HostDiagnostic);
		drsprintk(ioc, printk(MPT2SAS_INFO_FMT "wrote magic "
		    "sequence: count(%d), host_diagnostic(0x%08x)\n",
		    ioc->name, count, host_diagnostic));

	} while ((host_diagnostic & MPI2_DIAG_DIAG_WRITE_ENABLE) == 0);

	hcb_size = readl(&ioc->chip->HCBSize);

	drsprintk(ioc, printk(MPT2SAS_INFO_FMT "diag reset: issued\n",
	    ioc->name));
	writel(host_diagnostic | MPI2_DIAG_RESET_ADAPTER,
	     &ioc->chip->HostDiagnostic);

	/* don't access any registers for 50 milliseconds */
	msleep(50);

	/* 300 second max wait */
	for (count = 0; count < 3000000 ; count++) {

		host_diagnostic = readl(&ioc->chip->HostDiagnostic);

		if (host_diagnostic == 0xFFFFFFFF)
			goto out;
		if (!(host_diagnostic & MPI2_DIAG_RESET_ADAPTER))
			break;

		/* wait 100 msec */
		if (sleep_flag == CAN_SLEEP)
			msleep(1);
		else
			mdelay(1);
	}

	if (host_diagnostic & MPI2_DIAG_HCB_MODE) {

		drsprintk(ioc, printk(MPT2SAS_INFO_FMT "restart the adapter "
		    "assuming the HCB Address points to good F/W\n",
		    ioc->name));
		host_diagnostic &= ~MPI2_DIAG_BOOT_DEVICE_SELECT_MASK;
		host_diagnostic |= MPI2_DIAG_BOOT_DEVICE_SELECT_HCDW;
		writel(host_diagnostic, &ioc->chip->HostDiagnostic);

		drsprintk(ioc, printk(MPT2SAS_INFO_FMT
		    "re-enable the HCDW\n", ioc->name));
		writel(hcb_size | MPI2_HCB_SIZE_HCB_ENABLE,
		    &ioc->chip->HCBSize);
	}

	drsprintk(ioc, printk(MPT2SAS_INFO_FMT "restart the adapter\n",
	    ioc->name));
	writel(host_diagnostic & ~MPI2_DIAG_HOLD_IOC_RESET,
	    &ioc->chip->HostDiagnostic);

	drsprintk(ioc, printk(MPT2SAS_INFO_FMT "disable writes to the "
	    "diagnostic register\n", ioc->name));
	writel(MPI2_WRSEQ_FLUSH_KEY_VALUE, &ioc->chip->WriteSequence);

	drsprintk(ioc, printk(MPT2SAS_INFO_FMT "Wait for FW to go to the "
	    "READY state\n", ioc->name));
	ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_READY, 20,
	    sleep_flag);
	if (ioc_state) {
		printk(MPT2SAS_ERR_FMT "%s: failed going to ready state "
		    " (ioc_state=0x%x)\n", ioc->name, __func__, ioc_state);
		goto out;
	}

	_base_restore_msix_table(ioc);
	printk(MPT2SAS_INFO_FMT "diag reset: SUCCESS\n", ioc->name);
	return 0;

 out:
	printk(MPT2SAS_ERR_FMT "diag reset: FAILED\n", ioc->name);
	return -EFAULT;
}

/**
 * _base_make_ioc_ready - put controller in READY state
 * @ioc: per adapter object
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 * @type: FORCE_BIG_HAMMER or SOFT_RESET
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_make_ioc_ready(struct MPT2SAS_ADAPTER *ioc, int sleep_flag,
    enum reset_type type)
{
	u32 ioc_state;
	int rc;

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	if (ioc->pci_error_recovery)
		return 0;

	ioc_state = mpt2sas_base_get_iocstate(ioc, 0);
	dhsprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: ioc_state(0x%08x)\n",
	    ioc->name, __func__, ioc_state));

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_READY)
		return 0;

	if (ioc_state & MPI2_DOORBELL_USED) {
		dhsprintk(ioc, printk(MPT2SAS_INFO_FMT "unexpected doorbell "
		    "active!\n", ioc->name));
		goto issue_diag_reset;
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt2sas_base_fault_info(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		goto issue_diag_reset;
	}

	if (type == FORCE_BIG_HAMMER)
		goto issue_diag_reset;

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_OPERATIONAL)
		if (!(_base_send_ioc_reset(ioc,
		    MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET, 15, CAN_SLEEP))) {
			ioc->ioc_reset_count++;
			return 0;
	}

 issue_diag_reset:
	rc = _base_diag_reset(ioc, CAN_SLEEP);
	ioc->ioc_reset_count++;
	return rc;
}

/**
 * _base_make_ioc_operational - put controller in OPERATIONAL state
 * @ioc: per adapter object
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_make_ioc_operational(struct MPT2SAS_ADAPTER *ioc, int sleep_flag)
{
	int r, i;
	unsigned long	flags;
	u32 reply_address;
	u16 smid;
	struct _tr_list *delayed_tr, *delayed_tr_next;

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	/* clean the delayed target reset list */
	list_for_each_entry_safe(delayed_tr, delayed_tr_next,
	    &ioc->delayed_tr_list, list) {
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
	}

	list_for_each_entry_safe(delayed_tr, delayed_tr_next,
	    &ioc->delayed_tr_volume_list, list) {
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
	}

	/* initialize the scsi lookup free list */
	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	INIT_LIST_HEAD(&ioc->free_list);
	smid = 1;
	for (i = 0; i < ioc->scsiio_depth; i++, smid++) {
		INIT_LIST_HEAD(&ioc->scsi_lookup[i].chain_list);
		ioc->scsi_lookup[i].cb_idx = 0xFF;
		ioc->scsi_lookup[i].smid = smid;
		ioc->scsi_lookup[i].scmd = NULL;
		list_add_tail(&ioc->scsi_lookup[i].tracker_list,
		    &ioc->free_list);
	}

	/* hi-priority queue */
	INIT_LIST_HEAD(&ioc->hpr_free_list);
	smid = ioc->hi_priority_smid;
	for (i = 0; i < ioc->hi_priority_depth; i++, smid++) {
		ioc->hpr_lookup[i].cb_idx = 0xFF;
		ioc->hpr_lookup[i].smid = smid;
		list_add_tail(&ioc->hpr_lookup[i].tracker_list,
		    &ioc->hpr_free_list);
	}

	/* internal queue */
	INIT_LIST_HEAD(&ioc->internal_free_list);
	smid = ioc->internal_smid;
	for (i = 0; i < ioc->internal_depth; i++, smid++) {
		ioc->internal_lookup[i].cb_idx = 0xFF;
		ioc->internal_lookup[i].smid = smid;
		list_add_tail(&ioc->internal_lookup[i].tracker_list,
		    &ioc->internal_free_list);
	}

	/* chain pool */
	INIT_LIST_HEAD(&ioc->free_chain_list);
	for (i = 0; i < ioc->chain_depth; i++)
		list_add_tail(&ioc->chain_lookup[i].tracker_list,
		    &ioc->free_chain_list);

	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

	/* initialize Reply Free Queue */
	for (i = 0, reply_address = (u32)ioc->reply_dma ;
	    i < ioc->reply_free_queue_depth ; i++, reply_address +=
	    ioc->reply_sz)
		ioc->reply_free[i] = cpu_to_le32(reply_address);

	/* initialize Reply Post Free Queue */
	for (i = 0; i < ioc->reply_post_queue_depth; i++)
		ioc->reply_post_free[i].Words = ULLONG_MAX;

	r = _base_send_ioc_init(ioc, sleep_flag);
	if (r)
		return r;

	/* initialize the index's */
	ioc->reply_free_host_index = ioc->reply_free_queue_depth - 1;
	ioc->reply_post_host_index = 0;
	writel(ioc->reply_free_host_index, &ioc->chip->ReplyFreeHostIndex);
	writel(0, &ioc->chip->ReplyPostHostIndex);

	_base_unmask_interrupts(ioc);
	r = _base_event_notification(ioc, sleep_flag);
	if (r)
		return r;

	if (sleep_flag == CAN_SLEEP)
		_base_static_config_pages(ioc);

	if (ioc->wait_for_port_enable_to_complete) {
		if (diag_buffer_enable != 0)
			mpt2sas_enable_diag_buffer(ioc, diag_buffer_enable);
		if (disable_discovery > 0)
			return r;
	}

	r = _base_send_port_enable(ioc, sleep_flag);
	if (r)
		return r;

	return r;
}

/**
 * mpt2sas_base_free_resources - free resources controller resources (io/irq/memap)
 * @ioc: per adapter object
 *
 * Return nothing.
 */
void
mpt2sas_base_free_resources(struct MPT2SAS_ADAPTER *ioc)
{
	struct pci_dev *pdev = ioc->pdev;

	dexitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	_base_mask_interrupts(ioc);
	ioc->shost_recovery = 1;
	_base_make_ioc_ready(ioc, CAN_SLEEP, SOFT_RESET);
	ioc->shost_recovery = 0;
	if (ioc->pci_irq) {
		synchronize_irq(pdev->irq);
		free_irq(ioc->pci_irq, ioc);
	}
	_base_disable_msix(ioc);
	if (ioc->chip_phys)
		iounmap(ioc->chip);
	ioc->pci_irq = -1;
	ioc->chip_phys = 0;
	pci_release_selected_regions(ioc->pdev, ioc->bars);
	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);
	return;
}

/**
 * mpt2sas_base_attach - attach controller instance
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_base_attach(struct MPT2SAS_ADAPTER *ioc)
{
	int r, i;

	dinitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	r = mpt2sas_base_map_resources(ioc);
	if (r)
		return r;

	pci_set_drvdata(ioc->pdev, ioc->shost);
	r = _base_get_ioc_facts(ioc, CAN_SLEEP);
	if (r)
		goto out_free_resources;

	r = _base_make_ioc_ready(ioc, CAN_SLEEP, SOFT_RESET);
	if (r)
		goto out_free_resources;

	ioc->pfacts = kcalloc(ioc->facts.NumberOfPorts,
	    sizeof(Mpi2PortFactsReply_t), GFP_KERNEL);
	if (!ioc->pfacts) {
		r = -ENOMEM;
		goto out_free_resources;
	}

	for (i = 0 ; i < ioc->facts.NumberOfPorts; i++) {
		r = _base_get_port_facts(ioc, i, CAN_SLEEP);
		if (r)
			goto out_free_resources;
	}

	r = _base_allocate_memory_pools(ioc, CAN_SLEEP);
	if (r)
		goto out_free_resources;

	init_waitqueue_head(&ioc->reset_wq);

	/* allocate memory pd handle bitmask list */
	ioc->pd_handles_sz = (ioc->facts.MaxDevHandle / 8);
	if (ioc->facts.MaxDevHandle % 8)
		ioc->pd_handles_sz++;
	ioc->pd_handles = kzalloc(ioc->pd_handles_sz,
	    GFP_KERNEL);
	if (!ioc->pd_handles) {
		r = -ENOMEM;
		goto out_free_resources;
	}

	ioc->fwfault_debug = mpt2sas_fwfault_debug;

	/* base internal command bits */
	mutex_init(&ioc->base_cmds.mutex);
	ioc->base_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->base_cmds.status = MPT2_CMD_NOT_USED;

	/* transport internal command bits */
	ioc->transport_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->transport_cmds.status = MPT2_CMD_NOT_USED;
	mutex_init(&ioc->transport_cmds.mutex);

	/* scsih internal command bits */
	ioc->scsih_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->scsih_cmds.status = MPT2_CMD_NOT_USED;
	mutex_init(&ioc->scsih_cmds.mutex);

	/* task management internal command bits */
	ioc->tm_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->tm_cmds.status = MPT2_CMD_NOT_USED;
	mutex_init(&ioc->tm_cmds.mutex);

	/* config page internal command bits */
	ioc->config_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->config_cmds.status = MPT2_CMD_NOT_USED;
	mutex_init(&ioc->config_cmds.mutex);

	/* ctl module internal command bits */
	ioc->ctl_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->ctl_cmds.sense = kzalloc(SCSI_SENSE_BUFFERSIZE, GFP_KERNEL);
	ioc->ctl_cmds.status = MPT2_CMD_NOT_USED;
	mutex_init(&ioc->ctl_cmds.mutex);

	if (!ioc->base_cmds.reply || !ioc->transport_cmds.reply ||
	    !ioc->scsih_cmds.reply || !ioc->tm_cmds.reply ||
	    !ioc->config_cmds.reply || !ioc->ctl_cmds.reply ||
	    !ioc->ctl_cmds.sense) {
		r = -ENOMEM;
		goto out_free_resources;
	}

	if (!ioc->base_cmds.reply || !ioc->transport_cmds.reply ||
	    !ioc->scsih_cmds.reply || !ioc->tm_cmds.reply ||
	    !ioc->config_cmds.reply || !ioc->ctl_cmds.reply) {
		r = -ENOMEM;
		goto out_free_resources;
	}

	init_completion(&ioc->shost_recovery_done);

	for (i = 0; i < MPI2_EVENT_NOTIFY_EVENTMASK_WORDS; i++)
		ioc->event_masks[i] = -1;

	/* here we enable the events we care about */
	_base_unmask_events(ioc, MPI2_EVENT_SAS_DISCOVERY);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_BROADCAST_PRIMITIVE);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE);
	_base_unmask_events(ioc, MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST);
	_base_unmask_events(ioc, MPI2_EVENT_IR_VOLUME);
	_base_unmask_events(ioc, MPI2_EVENT_IR_PHYSICAL_DISK);
	_base_unmask_events(ioc, MPI2_EVENT_IR_OPERATION_STATUS);
	_base_unmask_events(ioc, MPI2_EVENT_LOG_ENTRY_ADDED);
	r = _base_make_ioc_operational(ioc, CAN_SLEEP);
	if (r)
		goto out_free_resources;

	if (missing_delay[0] != -1 && missing_delay[1] != -1)
		_base_update_missing_delay(ioc, missing_delay[0],
		    missing_delay[1]);

	mpt2sas_base_start_watchdog(ioc);
	return 0;

 out_free_resources:

	ioc->remove_host = 1;
	mpt2sas_base_free_resources(ioc);
	_base_release_memory_pools(ioc);
	pci_set_drvdata(ioc->pdev, NULL);
	kfree(ioc->pd_handles);
	kfree(ioc->tm_cmds.reply);
	kfree(ioc->transport_cmds.reply);
	kfree(ioc->scsih_cmds.reply);
	kfree(ioc->config_cmds.reply);
	kfree(ioc->base_cmds.reply);
	kfree(ioc->ctl_cmds.reply);
	kfree(ioc->ctl_cmds.sense);
	kfree(ioc->pfacts);
	ioc->ctl_cmds.reply = NULL;
	ioc->base_cmds.reply = NULL;
	ioc->tm_cmds.reply = NULL;
	ioc->scsih_cmds.reply = NULL;
	ioc->transport_cmds.reply = NULL;
	ioc->config_cmds.reply = NULL;
	ioc->pfacts = NULL;
	return r;
}


/**
 * mpt2sas_base_detach - remove controller instance
 * @ioc: per adapter object
 *
 * Return nothing.
 */
void
mpt2sas_base_detach(struct MPT2SAS_ADAPTER *ioc)
{

	dexitprintk(ioc, printk(MPT2SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	mpt2sas_base_stop_watchdog(ioc);
	mpt2sas_base_free_resources(ioc);
	_base_release_memory_pools(ioc);
	pci_set_drvdata(ioc->pdev, NULL);
	kfree(ioc->pd_handles);
	kfree(ioc->pfacts);
	kfree(ioc->ctl_cmds.reply);
	kfree(ioc->ctl_cmds.sense);
	kfree(ioc->base_cmds.reply);
	kfree(ioc->tm_cmds.reply);
	kfree(ioc->transport_cmds.reply);
	kfree(ioc->scsih_cmds.reply);
	kfree(ioc->config_cmds.reply);
}

/**
 * _base_reset_handler - reset callback handler (for base)
 * @ioc: per adapter object
 * @reset_phase: phase
 *
 * The handler for doing any required cleanup or initialization.
 *
 * The reset phase can be MPT2_IOC_PRE_RESET, MPT2_IOC_AFTER_RESET,
 * MPT2_IOC_DONE_RESET
 *
 * Return nothing.
 */
static void
_base_reset_handler(struct MPT2SAS_ADAPTER *ioc, int reset_phase)
{
	mpt2sas_scsih_reset_handler(ioc, reset_phase);
	mpt2sas_ctl_reset_handler(ioc, reset_phase);
	switch (reset_phase) {
	case MPT2_IOC_PRE_RESET:
		dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: "
		    "MPT2_IOC_PRE_RESET\n", ioc->name, __func__));
		break;
	case MPT2_IOC_AFTER_RESET:
		dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: "
		    "MPT2_IOC_AFTER_RESET\n", ioc->name, __func__));
		if (ioc->transport_cmds.status & MPT2_CMD_PENDING) {
			ioc->transport_cmds.status |= MPT2_CMD_RESET;
			mpt2sas_base_free_smid(ioc, ioc->transport_cmds.smid);
			complete(&ioc->transport_cmds.done);
		}
		if (ioc->base_cmds.status & MPT2_CMD_PENDING) {
			ioc->base_cmds.status |= MPT2_CMD_RESET;
			mpt2sas_base_free_smid(ioc, ioc->base_cmds.smid);
			complete(&ioc->base_cmds.done);
		}
		if (ioc->config_cmds.status & MPT2_CMD_PENDING) {
			ioc->config_cmds.status |= MPT2_CMD_RESET;
			mpt2sas_base_free_smid(ioc, ioc->config_cmds.smid);
			ioc->config_cmds.smid = USHRT_MAX;
			complete(&ioc->config_cmds.done);
		}
		break;
	case MPT2_IOC_DONE_RESET:
		dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: "
		    "MPT2_IOC_DONE_RESET\n", ioc->name, __func__));
		break;
	}
}

/**
 * _wait_for_commands_to_complete - reset controller
 * @ioc: Pointer to MPT_ADAPTER structure
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 *
 * This function waiting(3s) for all pending commands to complete
 * prior to putting controller in reset.
 */
static void
_wait_for_commands_to_complete(struct MPT2SAS_ADAPTER *ioc, int sleep_flag)
{
	u32 ioc_state;
	unsigned long flags;
	u16 i;

	ioc->pending_io_count = 0;
	if (sleep_flag != CAN_SLEEP)
		return;

	ioc_state = mpt2sas_base_get_iocstate(ioc, 0);
	if ((ioc_state & MPI2_IOC_STATE_MASK) != MPI2_IOC_STATE_OPERATIONAL)
		return;

	/* pending command count */
	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	for (i = 0; i < ioc->scsiio_depth; i++)
		if (ioc->scsi_lookup[i].cb_idx != 0xFF)
			ioc->pending_io_count++;
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

	if (!ioc->pending_io_count)
		return;

	/* wait for pending commands to complete */
	wait_event_timeout(ioc->reset_wq, ioc->pending_io_count == 0, 10 * HZ);
}

/**
 * mpt2sas_base_hard_reset_handler - reset controller
 * @ioc: Pointer to MPT_ADAPTER structure
 * @sleep_flag: CAN_SLEEP or NO_SLEEP
 * @type: FORCE_BIG_HAMMER or SOFT_RESET
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt2sas_base_hard_reset_handler(struct MPT2SAS_ADAPTER *ioc, int sleep_flag,
    enum reset_type type)
{
	int r;
	unsigned long flags;
	u8 pe_complete = ioc->wait_for_port_enable_to_complete;

	dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: enter\n", ioc->name,
	    __func__));

	if (ioc->pci_error_recovery) {
		printk(MPT2SAS_ERR_FMT "%s: pci error recovery reset\n",
		    ioc->name, __func__);
		r = 0;
		goto out;
	}

	if (mpt2sas_fwfault_debug)
		mpt2sas_halt_firmware(ioc);

	/* TODO - What we really should be doing is pulling
	 * out all the code associated with NO_SLEEP; its never used.
	 * That is legacy code from mpt fusion driver, ported over.
	 * I will leave this BUG_ON here for now till its been resolved.
	 */
	BUG_ON(sleep_flag == NO_SLEEP);

	/* wait for an active reset in progress to complete */
	if (!mutex_trylock(&ioc->reset_in_progress_mutex)) {
		do {
			ssleep(1);
		} while (ioc->shost_recovery == 1);
		dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: exit\n", ioc->name,
		    __func__));
		return ioc->ioc_reset_in_progress_status;
	}

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	ioc->shost_recovery = 1;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);

	_base_reset_handler(ioc, MPT2_IOC_PRE_RESET);
	_wait_for_commands_to_complete(ioc, sleep_flag);
	_base_mask_interrupts(ioc);
	r = _base_make_ioc_ready(ioc, sleep_flag, type);
	if (r)
		goto out;
	_base_reset_handler(ioc, MPT2_IOC_AFTER_RESET);

	/* If this hard reset is called while port enable is active, then
	 * there is no reason to call make_ioc_operational
	 */
	if (pe_complete) {
		r = -EFAULT;
		goto out;
	}
	r = _base_make_ioc_operational(ioc, sleep_flag);
	if (!r)
		_base_reset_handler(ioc, MPT2_IOC_DONE_RESET);
 out:
	dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: %s\n",
	    ioc->name, __func__, ((r == 0) ? "SUCCESS" : "FAILED")));

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	ioc->ioc_reset_in_progress_status = r;
	ioc->shost_recovery = 0;
	complete(&ioc->shost_recovery_done);
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
	mutex_unlock(&ioc->reset_in_progress_mutex);

	dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: exit\n", ioc->name,
	    __func__));
	return r;
}
