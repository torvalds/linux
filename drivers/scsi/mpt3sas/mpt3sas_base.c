/*
 * This is the Fusion MPT base driver providing common API layer interface
 * for access to MPT (Message Passing Technology) firmware.
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_base.c
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
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <linux/aer.h>


#include "mpt3sas_base.h"

static MPT_CALLBACK	mpt_callbacks[MPT_MAX_CALLBACKS];


#define FAULT_POLLING_INTERVAL 1000 /* in milliseconds */

 /* maximum controller queue depth */
#define MAX_HBA_QUEUE_DEPTH	30000
#define MAX_CHAIN_DEPTH		100000
static int max_queue_depth = -1;
module_param(max_queue_depth, int, 0);
MODULE_PARM_DESC(max_queue_depth, " max controller queue depth ");

static int max_sgl_entries = -1;
module_param(max_sgl_entries, int, 0);
MODULE_PARM_DESC(max_sgl_entries, " max sg entries ");

static int msix_disable = -1;
module_param(msix_disable, int, 0);
MODULE_PARM_DESC(msix_disable, " disable msix routed interrupts (default=0)");

static int smp_affinity_enable = 1;
module_param(smp_affinity_enable, int, S_IRUGO);
MODULE_PARM_DESC(smp_affinity_enable, "SMP affinity feature enable/disbale Default: enable(1)");

static int max_msix_vectors = -1;
module_param(max_msix_vectors, int, 0);
MODULE_PARM_DESC(max_msix_vectors,
	" max msix vectors");

static int mpt3sas_fwfault_debug;
MODULE_PARM_DESC(mpt3sas_fwfault_debug,
	" enable detection of firmware fault and halt firmware - (default=0)");

static int
_base_get_ioc_facts(struct MPT3SAS_ADAPTER *ioc);

/**
 * _scsih_set_fwfault_debug - global setting of ioc->fwfault_debug.
 *
 */
static int
_scsih_set_fwfault_debug(const char *val, struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);
	struct MPT3SAS_ADAPTER *ioc;

	if (ret)
		return ret;

	/* global ioc spinlock to protect controller list on list operations */
	pr_info("setting fwfault_debug(%d)\n", mpt3sas_fwfault_debug);
	spin_lock(&gioc_lock);
	list_for_each_entry(ioc, &mpt3sas_ioc_list, list)
		ioc->fwfault_debug = mpt3sas_fwfault_debug;
	spin_unlock(&gioc_lock);
	return 0;
}
module_param_call(mpt3sas_fwfault_debug, _scsih_set_fwfault_debug,
	param_get_int, &mpt3sas_fwfault_debug, 0644);

/**
 *  mpt3sas_remove_dead_ioc_func - kthread context to remove dead ioc
 * @arg: input argument, used to derive ioc
 *
 * Return 0 if controller is removed from pci subsystem.
 * Return -1 for other case.
 */
static int mpt3sas_remove_dead_ioc_func(void *arg)
{
	struct MPT3SAS_ADAPTER *ioc = (struct MPT3SAS_ADAPTER *)arg;
	struct pci_dev *pdev;

	if ((ioc == NULL))
		return -1;

	pdev = ioc->pdev;
	if ((pdev == NULL))
		return -1;
	pci_stop_and_remove_bus_device_locked(pdev);
	return 0;
}

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
	struct MPT3SAS_ADAPTER *ioc =
	    container_of(work, struct MPT3SAS_ADAPTER, fault_reset_work.work);
	unsigned long	 flags;
	u32 doorbell;
	int rc;
	struct task_struct *p;


	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	if (ioc->shost_recovery || ioc->pci_error_recovery)
		goto rearm_timer;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);

	doorbell = mpt3sas_base_get_iocstate(ioc, 0);
	if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_MASK) {
		pr_err(MPT3SAS_FMT "SAS host is non-operational !!!!\n",
		    ioc->name);

		/* It may be possible that EEH recovery can resolve some of
		 * pci bus failure issues rather removing the dead ioc function
		 * by considering controller is in a non-operational state. So
		 * here priority is given to the EEH recovery. If it doesn't
		 * not resolve this issue, mpt3sas driver will consider this
		 * controller to non-operational state and remove the dead ioc
		 * function.
		 */
		if (ioc->non_operational_loop++ < 5) {
			spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock,
							 flags);
			goto rearm_timer;
		}

		/*
		 * Call _scsih_flush_pending_cmds callback so that we flush all
		 * pending commands back to OS. This call is required to aovid
		 * deadlock at block layer. Dead IOC will fail to do diag reset,
		 * and this call is safe since dead ioc will never return any
		 * command back from HW.
		 */
		ioc->schedule_dead_ioc_flush_running_cmds(ioc);
		/*
		 * Set remove_host flag early since kernel thread will
		 * take some time to execute.
		 */
		ioc->remove_host = 1;
		/*Remove the Dead Host */
		p = kthread_run(mpt3sas_remove_dead_ioc_func, ioc,
		    "%s_dead_ioc_%d", ioc->driver_name, ioc->id);
		if (IS_ERR(p))
			pr_err(MPT3SAS_FMT
			"%s: Running mpt3sas_dead_ioc thread failed !!!!\n",
			ioc->name, __func__);
		else
			pr_err(MPT3SAS_FMT
			"%s: Running mpt3sas_dead_ioc thread success !!!!\n",
			ioc->name, __func__);
		return; /* don't rearm timer */
	}

	ioc->non_operational_loop = 0;

	if ((doorbell & MPI2_IOC_STATE_MASK) != MPI2_IOC_STATE_OPERATIONAL) {
		rc = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
		pr_warn(MPT3SAS_FMT "%s: hard reset: %s\n", ioc->name,
		    __func__, (rc == 0) ? "success" : "failed");
		doorbell = mpt3sas_base_get_iocstate(ioc, 0);
		if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT)
			mpt3sas_base_fault_info(ioc, doorbell &
			    MPI2_DOORBELL_DATA_MASK);
		if (rc && (doorbell & MPI2_IOC_STATE_MASK) !=
		    MPI2_IOC_STATE_OPERATIONAL)
			return; /* don't rearm timer */
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
 * mpt3sas_base_start_watchdog - start the fault_reset_work_q
 * @ioc: per adapter object
 * Context: sleep.
 *
 * Return nothing.
 */
void
mpt3sas_base_start_watchdog(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned long	 flags;

	if (ioc->fault_reset_work_q)
		return;

	/* initialize fault polling */

	INIT_DELAYED_WORK(&ioc->fault_reset_work, _base_fault_reset_work);
	snprintf(ioc->fault_reset_work_q_name,
	    sizeof(ioc->fault_reset_work_q_name), "poll_%s%d_status",
	    ioc->driver_name, ioc->id);
	ioc->fault_reset_work_q =
		create_singlethread_workqueue(ioc->fault_reset_work_q_name);
	if (!ioc->fault_reset_work_q) {
		pr_err(MPT3SAS_FMT "%s: failed (line=%d)\n",
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
 * mpt3sas_base_stop_watchdog - stop the fault_reset_work_q
 * @ioc: per adapter object
 * Context: sleep.
 *
 * Return nothing.
 */
void
mpt3sas_base_stop_watchdog(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned long flags;
	struct workqueue_struct *wq;

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	wq = ioc->fault_reset_work_q;
	ioc->fault_reset_work_q = NULL;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
	if (wq) {
		if (!cancel_delayed_work_sync(&ioc->fault_reset_work))
			flush_workqueue(wq);
		destroy_workqueue(wq);
	}
}

/**
 * mpt3sas_base_fault_info - verbose translation of firmware FAULT code
 * @ioc: per adapter object
 * @fault_code: fault code
 *
 * Return nothing.
 */
void
mpt3sas_base_fault_info(struct MPT3SAS_ADAPTER *ioc , u16 fault_code)
{
	pr_err(MPT3SAS_FMT "fault_state(0x%04x)!\n",
	    ioc->name, fault_code);
}

/**
 * mpt3sas_halt_firmware - halt's mpt controller firmware
 * @ioc: per adapter object
 *
 * For debugging timeout related issues.  Writing 0xCOFFEE00
 * to the doorbell register will halt controller firmware. With
 * the purpose to stop both driver and firmware, the enduser can
 * obtain a ring buffer from controller UART.
 */
void
mpt3sas_halt_firmware(struct MPT3SAS_ADAPTER *ioc)
{
	u32 doorbell;

	if (!ioc->fwfault_debug)
		return;

	dump_stack();

	doorbell = readl(&ioc->chip->Doorbell);
	if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT)
		mpt3sas_base_fault_info(ioc , doorbell);
	else {
		writel(0xC0FFEE00, &ioc->chip->Doorbell);
		pr_err(MPT3SAS_FMT "Firmware is halted due to command timeout\n",
			ioc->name);
	}

	if (ioc->fwfault_debug == 2)
		for (;;)
			;
	else
		panic("panic in %s\n", __func__);
}

/**
 * _base_sas_ioc_info - verbose translation of the ioc status
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @request_hdr: request mf
 *
 * Return nothing.
 */
static void
_base_sas_ioc_info(struct MPT3SAS_ADAPTER *ioc, MPI2DefaultReply_t *mpi_reply,
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
	case MPI2_IOCSTATUS_INSUFFICIENT_POWER:
		desc = "insufficient power";
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

	pr_warn(MPT3SAS_FMT "ioc_status: %s(0x%04x), request(0x%p),(%s)\n",
		ioc->name, desc, ioc_status, request_hdr, func_str);

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
_base_display_event_data(struct MPT3SAS_ADAPTER *ioc,
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
		if (!ioc->hide_ir_msg)
			desc = "IR Operation Status";
		break;
	case MPI2_EVENT_SAS_DISCOVERY:
	{
		Mpi2EventDataSasDiscovery_t *event_data =
		    (Mpi2EventDataSasDiscovery_t *)mpi_reply->EventData;
		pr_info(MPT3SAS_FMT "Discovery: (%s)", ioc->name,
		    (event_data->ReasonCode == MPI2_EVENT_SAS_DISC_RC_STARTED) ?
		    "start" : "stop");
		if (event_data->DiscoveryStatus)
			pr_info("discovery_status(0x%08x)",
			    le32_to_cpu(event_data->DiscoveryStatus));
			pr_info("\n");
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
		if (!ioc->hide_ir_msg)
			desc = "IR Volume";
		break;
	case MPI2_EVENT_IR_PHYSICAL_DISK:
		if (!ioc->hide_ir_msg)
			desc = "IR Physical Disk";
		break;
	case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
		if (!ioc->hide_ir_msg)
			desc = "IR Configuration Change List";
		break;
	case MPI2_EVENT_LOG_ENTRY_ADDED:
		if (!ioc->hide_ir_msg)
			desc = "Log Entry Added";
		break;
	case MPI2_EVENT_TEMP_THRESHOLD:
		desc = "Temperature Threshold";
		break;
	case MPI2_EVENT_ACTIVE_CABLE_EXCEPTION:
		desc = "Active cable exception";
		break;
	}

	if (!desc)
		return;

	pr_info(MPT3SAS_FMT "%s\n", ioc->name, desc);
}

/**
 * _base_sas_log_info - verbose translation of firmware log info
 * @ioc: per adapter object
 * @log_info: log info
 *
 * Return nothing.
 */
static void
_base_sas_log_info(struct MPT3SAS_ADAPTER *ioc , u32 log_info)
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
	if (ioc->ignore_loginfos && (log_info == 0x30050000 || log_info ==
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
		if (!ioc->hide_ir_msg)
			originator_str = "IR";
		else
			originator_str = "WarpDrive";
		break;
	}

	pr_warn(MPT3SAS_FMT
		"log_info(0x%08x): originator(%s), code(0x%02x), sub_code(0x%04x)\n",
		ioc->name, log_info,
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
_base_display_reply_info(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;
	u16 ioc_status;
	u32 loginfo = 0;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (unlikely(!mpi_reply)) {
		pr_err(MPT3SAS_FMT "mpi_reply not valid at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus);

	if ((ioc_status & MPI2_IOCSTATUS_MASK) &&
	    (ioc->logging_level & MPT_DEBUG_REPLY)) {
		_base_sas_ioc_info(ioc , mpi_reply,
		   mpt3sas_base_get_msg_frame(ioc, smid));
	}

	if (ioc_status & MPI2_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE) {
		loginfo = le32_to_cpu(mpi_reply->IOCLogInfo);
		_base_sas_log_info(ioc, loginfo);
	}

	if (ioc_status || loginfo) {
		ioc_status &= MPI2_IOCSTATUS_MASK;
		mpt3sas_trigger_mpi(ioc, ioc_status, loginfo);
	}
}

/**
 * mpt3sas_base_done - base internal command completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
u8
mpt3sas_base_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply && mpi_reply->Function == MPI2_FUNCTION_EVENT_ACK)
		return mpt3sas_check_for_pending_internal_cmds(ioc, smid);

	if (ioc->base_cmds.status == MPT3_CMD_NOT_USED)
		return 1;

	ioc->base_cmds.status |= MPT3_CMD_COMPLETE;
	if (mpi_reply) {
		ioc->base_cmds.status |= MPT3_CMD_REPLY_VALID;
		memcpy(ioc->base_cmds.reply, mpi_reply, mpi_reply->MsgLength*4);
	}
	ioc->base_cmds.status &= ~MPT3_CMD_PENDING;

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
_base_async_event(struct MPT3SAS_ADAPTER *ioc, u8 msix_index, u32 reply)
{
	Mpi2EventNotificationReply_t *mpi_reply;
	Mpi2EventAckRequest_t *ack_request;
	u16 smid;
	struct _event_ack_list *delayed_event_ack;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (!mpi_reply)
		return 1;
	if (mpi_reply->Function != MPI2_FUNCTION_EVENT_NOTIFICATION)
		return 1;

	_base_display_event_data(ioc, mpi_reply);

	if (!(mpi_reply->AckRequired & MPI2_EVENT_NOTIFICATION_ACK_REQUIRED))
		goto out;
	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		delayed_event_ack = kzalloc(sizeof(*delayed_event_ack),
					GFP_ATOMIC);
		if (!delayed_event_ack)
			goto out;
		INIT_LIST_HEAD(&delayed_event_ack->list);
		delayed_event_ack->Event = mpi_reply->Event;
		delayed_event_ack->EventContext = mpi_reply->EventContext;
		list_add_tail(&delayed_event_ack->list,
				&ioc->delayed_event_ack_list);
		dewtprintk(ioc, pr_info(MPT3SAS_FMT
				"DELAYED: EVENT ACK: event (0x%04x)\n",
				ioc->name, le16_to_cpu(mpi_reply->Event)));
		goto out;
	}

	ack_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(ack_request, 0, sizeof(Mpi2EventAckRequest_t));
	ack_request->Function = MPI2_FUNCTION_EVENT_ACK;
	ack_request->Event = mpi_reply->Event;
	ack_request->EventContext = mpi_reply->EventContext;
	ack_request->VF_ID = 0;  /* TODO */
	ack_request->VP_ID = 0;
	ioc->put_smid_default(ioc, smid);

 out:

	/* scsih callback handler */
	mpt3sas_scsih_event_callback(ioc, msix_index, reply);

	/* ctl callback handler */
	mpt3sas_ctl_event_callback(ioc, msix_index, reply);

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
_base_get_cb_idx(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	int i;
	u8 cb_idx;

	if (smid < ioc->hi_priority_smid) {
		i = smid - 1;
		cb_idx = ioc->scsi_lookup[i].cb_idx;
	} else if (smid < ioc->internal_smid) {
		i = smid - ioc->hi_priority_smid;
		cb_idx = ioc->hpr_lookup[i].cb_idx;
	} else if (smid <= ioc->hba_queue_depth) {
		i = smid - ioc->internal_smid;
		cb_idx = ioc->internal_lookup[i].cb_idx;
	} else
		cb_idx = 0xFF;
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
_base_mask_interrupts(struct MPT3SAS_ADAPTER *ioc)
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
_base_unmask_interrupts(struct MPT3SAS_ADAPTER *ioc)
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
	struct adapter_reply_queue *reply_q = bus_id;
	union reply_descriptor rd;
	u32 completed_cmds;
	u8 request_desript_type;
	u16 smid;
	u8 cb_idx;
	u32 reply;
	u8 msix_index = reply_q->msix_index;
	struct MPT3SAS_ADAPTER *ioc = reply_q->ioc;
	Mpi2ReplyDescriptorsUnion_t *rpf;
	u8 rc;

	if (ioc->mask_interrupts)
		return IRQ_NONE;

	if (!atomic_add_unless(&reply_q->busy, 1, 1))
		return IRQ_NONE;

	rpf = &reply_q->reply_post_free[reply_q->reply_post_host_index];
	request_desript_type = rpf->Default.ReplyFlags
	     & MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;
	if (request_desript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED) {
		atomic_dec(&reply_q->busy);
		return IRQ_NONE;
	}

	completed_cmds = 0;
	cb_idx = 0xFF;
	do {
		rd.word = le64_to_cpu(rpf->Words);
		if (rd.u.low == UINT_MAX || rd.u.high == UINT_MAX)
			goto out;
		reply = 0;
		smid = le16_to_cpu(rpf->Default.DescriptorTypeDependent1);
		if (request_desript_type ==
		    MPI25_RPY_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO_SUCCESS ||
		    request_desript_type ==
		    MPI2_RPY_DESCRIPT_FLAGS_SCSI_IO_SUCCESS) {
			cb_idx = _base_get_cb_idx(ioc, smid);
			if ((likely(cb_idx < MPT_MAX_CALLBACKS)) &&
			    (likely(mpt_callbacks[cb_idx] != NULL))) {
				rc = mpt_callbacks[cb_idx](ioc, smid,
				    msix_index, 0);
				if (rc)
					mpt3sas_base_free_smid(ioc, smid);
			}
		} else if (request_desript_type ==
		    MPI2_RPY_DESCRIPT_FLAGS_ADDRESS_REPLY) {
			reply = le32_to_cpu(
			    rpf->AddressReply.ReplyFrameAddress);
			if (reply > ioc->reply_dma_max_address ||
			    reply < ioc->reply_dma_min_address)
				reply = 0;
			if (smid) {
				cb_idx = _base_get_cb_idx(ioc, smid);
				if ((likely(cb_idx < MPT_MAX_CALLBACKS)) &&
				    (likely(mpt_callbacks[cb_idx] != NULL))) {
					rc = mpt_callbacks[cb_idx](ioc, smid,
					    msix_index, reply);
					if (reply)
						_base_display_reply_info(ioc,
						    smid, msix_index, reply);
					if (rc)
						mpt3sas_base_free_smid(ioc,
						    smid);
				}
			} else {
				_base_async_event(ioc, msix_index, reply);
			}

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
		}

		rpf->Words = cpu_to_le64(ULLONG_MAX);
		reply_q->reply_post_host_index =
		    (reply_q->reply_post_host_index ==
		    (ioc->reply_post_queue_depth - 1)) ? 0 :
		    reply_q->reply_post_host_index + 1;
		request_desript_type =
		    reply_q->reply_post_free[reply_q->reply_post_host_index].
		    Default.ReplyFlags & MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;
		completed_cmds++;
		if (request_desript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED)
			goto out;
		if (!reply_q->reply_post_host_index)
			rpf = reply_q->reply_post_free;
		else
			rpf++;
	} while (1);

 out:

	if (!completed_cmds) {
		atomic_dec(&reply_q->busy);
		return IRQ_NONE;
	}

	wmb();
	if (ioc->is_warpdrive) {
		writel(reply_q->reply_post_host_index,
		ioc->reply_post_host_index[msix_index]);
		atomic_dec(&reply_q->busy);
		return IRQ_HANDLED;
	}

	/* Update Reply Post Host Index.
	 * For those HBA's which support combined reply queue feature
	 * 1. Get the correct Supplemental Reply Post Host Index Register.
	 *    i.e. (msix_index / 8)th entry from Supplemental Reply Post Host
	 *    Index Register address bank i.e replyPostRegisterIndex[],
	 * 2. Then update this register with new reply host index value
	 *    in ReplyPostIndex field and the MSIxIndex field with
	 *    msix_index value reduced to a value between 0 and 7,
	 *    using a modulo 8 operation. Since each Supplemental Reply Post
	 *    Host Index Register supports 8 MSI-X vectors.
	 *
	 * For other HBA's just update the Reply Post Host Index register with
	 * new reply host index value in ReplyPostIndex Field and msix_index
	 * value in MSIxIndex field.
	 */
	if (ioc->combined_reply_queue)
		writel(reply_q->reply_post_host_index | ((msix_index  & 7) <<
			MPI2_RPHI_MSIX_INDEX_SHIFT),
			ioc->replyPostRegisterIndex[msix_index/8]);
	else
		writel(reply_q->reply_post_host_index | (msix_index <<
			MPI2_RPHI_MSIX_INDEX_SHIFT),
			&ioc->chip->ReplyPostHostIndex);
	atomic_dec(&reply_q->busy);
	return IRQ_HANDLED;
}

/**
 * _base_is_controller_msix_enabled - is controller support muli-reply queues
 * @ioc: per adapter object
 *
 */
static inline int
_base_is_controller_msix_enabled(struct MPT3SAS_ADAPTER *ioc)
{
	return (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_MSI_X_INDEX) && ioc->msix_enable;
}

/**
 * mpt3sas_base_sync_reply_irqs - flush pending MSIX interrupts
 * @ioc: per adapter object
 * Context: non ISR conext
 *
 * Called when a Task Management request has completed.
 *
 * Return nothing.
 */
void
mpt3sas_base_sync_reply_irqs(struct MPT3SAS_ADAPTER *ioc)
{
	struct adapter_reply_queue *reply_q;

	/* If MSIX capability is turned off
	 * then multi-queues are not enabled
	 */
	if (!_base_is_controller_msix_enabled(ioc))
		return;

	list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {
		if (ioc->shost_recovery || ioc->remove_host ||
				ioc->pci_error_recovery)
			return;
		/* TMs are on msix_index == 0 */
		if (reply_q->msix_index == 0)
			continue;
		synchronize_irq(reply_q->vector);
	}
}

/**
 * mpt3sas_base_release_callback_handler - clear interrupt callback handler
 * @cb_idx: callback index
 *
 * Return nothing.
 */
void
mpt3sas_base_release_callback_handler(u8 cb_idx)
{
	mpt_callbacks[cb_idx] = NULL;
}

/**
 * mpt3sas_base_register_callback_handler - obtain index for the interrupt callback handler
 * @cb_func: callback function
 *
 * Returns cb_func.
 */
u8
mpt3sas_base_register_callback_handler(MPT_CALLBACK cb_func)
{
	u8 cb_idx;

	for (cb_idx = MPT_MAX_CALLBACKS-1; cb_idx; cb_idx--)
		if (mpt_callbacks[cb_idx] == NULL)
			break;

	mpt_callbacks[cb_idx] = cb_func;
	return cb_idx;
}

/**
 * mpt3sas_base_initialize_callback_handler - initialize the interrupt callback handler
 *
 * Return nothing.
 */
void
mpt3sas_base_initialize_callback_handler(void)
{
	u8 cb_idx;

	for (cb_idx = 0; cb_idx < MPT_MAX_CALLBACKS; cb_idx++)
		mpt3sas_base_release_callback_handler(cb_idx);
}


/**
 * _base_build_zero_len_sge - build zero length sg entry
 * @ioc: per adapter object
 * @paddr: virtual address for SGE
 *
 * Create a zero length scatter gather entry to insure the IOCs hardware has
 * something to use if the target device goes brain dead and tries
 * to send data even when none is asked for.
 *
 * Return nothing.
 */
static void
_base_build_zero_len_sge(struct MPT3SAS_ADAPTER *ioc, void *paddr)
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

/**
 * _base_get_chain_buffer_tracker - obtain chain tracker
 * @ioc: per adapter object
 * @smid: smid associated to an IO request
 *
 * Returns chain tracker(from ioc->free_chain_list)
 */
static struct chain_tracker *
_base_get_chain_buffer_tracker(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	struct chain_tracker *chain_req;
	unsigned long flags;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (list_empty(&ioc->free_chain_list)) {
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		dfailprintk(ioc, pr_warn(MPT3SAS_FMT
			"chain buffers not available\n", ioc->name));
		return NULL;
	}
	chain_req = list_entry(ioc->free_chain_list.next,
	    struct chain_tracker, tracker_list);
	list_del_init(&chain_req->tracker_list);
	list_add_tail(&chain_req->tracker_list,
	    &ioc->scsi_lookup[smid - 1].chain_list);
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return chain_req;
}


/**
 * _base_build_sg - build generic sg
 * @ioc: per adapter object
 * @psge: virtual address for SGE
 * @data_out_dma: physical address for WRITES
 * @data_out_sz: data xfer size for WRITES
 * @data_in_dma: physical address for READS
 * @data_in_sz: data xfer size for READS
 *
 * Return nothing.
 */
static void
_base_build_sg(struct MPT3SAS_ADAPTER *ioc, void *psge,
	dma_addr_t data_out_dma, size_t data_out_sz, dma_addr_t data_in_dma,
	size_t data_in_sz)
{
	u32 sgl_flags;

	if (!data_out_sz && !data_in_sz) {
		_base_build_zero_len_sge(ioc, psge);
		return;
	}

	if (data_out_sz && data_in_sz) {
		/* WRITE sgel first */
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_END_OF_BUFFER | MPI2_SGE_FLAGS_HOST_TO_IOC);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_out_sz, data_out_dma);

		/* incr sgel */
		psge += ioc->sge_size;

		/* READ sgel last */
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_END_OF_LIST);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_in_sz, data_in_dma);
	} else if (data_out_sz) /* WRITE */ {
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_END_OF_LIST | MPI2_SGE_FLAGS_HOST_TO_IOC);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_out_sz, data_out_dma);
	} else if (data_in_sz) /* READ */ {
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_END_OF_LIST);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_in_sz, data_in_dma);
	}
}

/* IEEE format sgls */

/**
 * _base_add_sg_single_ieee - add sg element for IEEE format
 * @paddr: virtual address for SGE
 * @flags: SGE flags
 * @chain_offset: number of 128 byte elements from start of segment
 * @length: data transfer length
 * @dma_addr: Physical address
 *
 * Return nothing.
 */
static void
_base_add_sg_single_ieee(void *paddr, u8 flags, u8 chain_offset, u32 length,
	dma_addr_t dma_addr)
{
	Mpi25IeeeSgeChain64_t *sgel = paddr;

	sgel->Flags = flags;
	sgel->NextChainOffset = chain_offset;
	sgel->Length = cpu_to_le32(length);
	sgel->Address = cpu_to_le64(dma_addr);
}

/**
 * _base_build_zero_len_sge_ieee - build zero length sg entry for IEEE format
 * @ioc: per adapter object
 * @paddr: virtual address for SGE
 *
 * Create a zero length scatter gather entry to insure the IOCs hardware has
 * something to use if the target device goes brain dead and tries
 * to send data even when none is asked for.
 *
 * Return nothing.
 */
static void
_base_build_zero_len_sge_ieee(struct MPT3SAS_ADAPTER *ioc, void *paddr)
{
	u8 sgl_flags = (MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR |
		MPI25_IEEE_SGE_FLAGS_END_OF_LIST);

	_base_add_sg_single_ieee(paddr, sgl_flags, 0, 0, -1);
}

/**
 * _base_build_sg_scmd - main sg creation routine
 * @ioc: per adapter object
 * @scmd: scsi command
 * @smid: system request message index
 * Context: none.
 *
 * The main routine that builds scatter gather table from a given
 * scsi request sent via the .queuecommand main handler.
 *
 * Returns 0 success, anything else error
 */
static int
_base_build_sg_scmd(struct MPT3SAS_ADAPTER *ioc,
		struct scsi_cmnd *scmd, u16 smid)
{
	Mpi2SCSIIORequest_t *mpi_request;
	dma_addr_t chain_dma;
	struct scatterlist *sg_scmd;
	void *sg_local, *chain;
	u32 chain_offset;
	u32 chain_length;
	u32 chain_flags;
	int sges_left;
	u32 sges_in_segment;
	u32 sgl_flags;
	u32 sgl_flags_last_element;
	u32 sgl_flags_end_buffer;
	struct chain_tracker *chain_req;

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);

	/* init scatter gather flags */
	sgl_flags = MPI2_SGE_FLAGS_SIMPLE_ELEMENT;
	if (scmd->sc_data_direction == DMA_TO_DEVICE)
		sgl_flags |= MPI2_SGE_FLAGS_HOST_TO_IOC;
	sgl_flags_last_element = (sgl_flags | MPI2_SGE_FLAGS_LAST_ELEMENT)
	    << MPI2_SGE_FLAGS_SHIFT;
	sgl_flags_end_buffer = (sgl_flags | MPI2_SGE_FLAGS_LAST_ELEMENT |
	    MPI2_SGE_FLAGS_END_OF_BUFFER | MPI2_SGE_FLAGS_END_OF_LIST)
	    << MPI2_SGE_FLAGS_SHIFT;
	sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;

	sg_scmd = scsi_sglist(scmd);
	sges_left = scsi_dma_map(scmd);
	if (sges_left < 0) {
		sdev_printk(KERN_ERR, scmd->device,
		 "pci_map_sg failed: request for %d bytes!\n",
		 scsi_bufflen(scmd));
		return -ENOMEM;
	}

	sg_local = &mpi_request->SGL;
	sges_in_segment = ioc->max_sges_in_main_message;
	if (sges_left <= sges_in_segment)
		goto fill_in_last_segment;

	mpi_request->ChainOffset = (offsetof(Mpi2SCSIIORequest_t, SGL) +
	    (sges_in_segment * ioc->sge_size))/4;

	/* fill in main message segment when there is a chain following */
	while (sges_in_segment) {
		if (sges_in_segment == 1)
			ioc->base_add_sg_single(sg_local,
			    sgl_flags_last_element | sg_dma_len(sg_scmd),
			    sg_dma_address(sg_scmd));
		else
			ioc->base_add_sg_single(sg_local, sgl_flags |
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		sg_scmd = sg_next(sg_scmd);
		sg_local += ioc->sge_size;
		sges_left--;
		sges_in_segment--;
	}

	/* initializing the chain flags and pointers */
	chain_flags = MPI2_SGE_FLAGS_CHAIN_ELEMENT << MPI2_SGE_FLAGS_SHIFT;
	chain_req = _base_get_chain_buffer_tracker(ioc, smid);
	if (!chain_req)
		return -1;
	chain = chain_req->chain_buffer;
	chain_dma = chain_req->chain_buffer_dma;
	do {
		sges_in_segment = (sges_left <=
		    ioc->max_sges_in_chain_message) ? sges_left :
		    ioc->max_sges_in_chain_message;
		chain_offset = (sges_left == sges_in_segment) ?
		    0 : (sges_in_segment * ioc->sge_size)/4;
		chain_length = sges_in_segment * ioc->sge_size;
		if (chain_offset) {
			chain_offset = chain_offset <<
			    MPI2_SGE_CHAIN_OFFSET_SHIFT;
			chain_length += ioc->sge_size;
		}
		ioc->base_add_sg_single(sg_local, chain_flags | chain_offset |
		    chain_length, chain_dma);
		sg_local = chain;
		if (!chain_offset)
			goto fill_in_last_segment;

		/* fill in chain segments */
		while (sges_in_segment) {
			if (sges_in_segment == 1)
				ioc->base_add_sg_single(sg_local,
				    sgl_flags_last_element |
				    sg_dma_len(sg_scmd),
				    sg_dma_address(sg_scmd));
			else
				ioc->base_add_sg_single(sg_local, sgl_flags |
				    sg_dma_len(sg_scmd),
				    sg_dma_address(sg_scmd));
			sg_scmd = sg_next(sg_scmd);
			sg_local += ioc->sge_size;
			sges_left--;
			sges_in_segment--;
		}

		chain_req = _base_get_chain_buffer_tracker(ioc, smid);
		if (!chain_req)
			return -1;
		chain = chain_req->chain_buffer;
		chain_dma = chain_req->chain_buffer_dma;
	} while (1);


 fill_in_last_segment:

	/* fill the last segment */
	while (sges_left) {
		if (sges_left == 1)
			ioc->base_add_sg_single(sg_local, sgl_flags_end_buffer |
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		else
			ioc->base_add_sg_single(sg_local, sgl_flags |
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		sg_scmd = sg_next(sg_scmd);
		sg_local += ioc->sge_size;
		sges_left--;
	}

	return 0;
}

/**
 * _base_build_sg_scmd_ieee - main sg creation routine for IEEE format
 * @ioc: per adapter object
 * @scmd: scsi command
 * @smid: system request message index
 * Context: none.
 *
 * The main routine that builds scatter gather table from a given
 * scsi request sent via the .queuecommand main handler.
 *
 * Returns 0 success, anything else error
 */
static int
_base_build_sg_scmd_ieee(struct MPT3SAS_ADAPTER *ioc,
	struct scsi_cmnd *scmd, u16 smid)
{
	Mpi2SCSIIORequest_t *mpi_request;
	dma_addr_t chain_dma;
	struct scatterlist *sg_scmd;
	void *sg_local, *chain;
	u32 chain_offset;
	u32 chain_length;
	int sges_left;
	u32 sges_in_segment;
	u8 simple_sgl_flags;
	u8 simple_sgl_flags_last;
	u8 chain_sgl_flags;
	struct chain_tracker *chain_req;

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);

	/* init scatter gather flags */
	simple_sgl_flags = MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;
	simple_sgl_flags_last = simple_sgl_flags |
	    MPI25_IEEE_SGE_FLAGS_END_OF_LIST;
	chain_sgl_flags = MPI2_IEEE_SGE_FLAGS_CHAIN_ELEMENT |
	    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;

	sg_scmd = scsi_sglist(scmd);
	sges_left = scsi_dma_map(scmd);
	if (sges_left < 0) {
		sdev_printk(KERN_ERR, scmd->device,
			"pci_map_sg failed: request for %d bytes!\n",
			scsi_bufflen(scmd));
		return -ENOMEM;
	}

	sg_local = &mpi_request->SGL;
	sges_in_segment = (ioc->request_sz -
	    offsetof(Mpi2SCSIIORequest_t, SGL))/ioc->sge_size_ieee;
	if (sges_left <= sges_in_segment)
		goto fill_in_last_segment;

	mpi_request->ChainOffset = (sges_in_segment - 1 /* chain element */) +
	    (offsetof(Mpi2SCSIIORequest_t, SGL)/ioc->sge_size_ieee);

	/* fill in main message segment when there is a chain following */
	while (sges_in_segment > 1) {
		_base_add_sg_single_ieee(sg_local, simple_sgl_flags, 0,
		    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		sg_scmd = sg_next(sg_scmd);
		sg_local += ioc->sge_size_ieee;
		sges_left--;
		sges_in_segment--;
	}

	/* initializing the pointers */
	chain_req = _base_get_chain_buffer_tracker(ioc, smid);
	if (!chain_req)
		return -1;
	chain = chain_req->chain_buffer;
	chain_dma = chain_req->chain_buffer_dma;
	do {
		sges_in_segment = (sges_left <=
		    ioc->max_sges_in_chain_message) ? sges_left :
		    ioc->max_sges_in_chain_message;
		chain_offset = (sges_left == sges_in_segment) ?
		    0 : sges_in_segment;
		chain_length = sges_in_segment * ioc->sge_size_ieee;
		if (chain_offset)
			chain_length += ioc->sge_size_ieee;
		_base_add_sg_single_ieee(sg_local, chain_sgl_flags,
		    chain_offset, chain_length, chain_dma);

		sg_local = chain;
		if (!chain_offset)
			goto fill_in_last_segment;

		/* fill in chain segments */
		while (sges_in_segment) {
			_base_add_sg_single_ieee(sg_local, simple_sgl_flags, 0,
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
			sg_scmd = sg_next(sg_scmd);
			sg_local += ioc->sge_size_ieee;
			sges_left--;
			sges_in_segment--;
		}

		chain_req = _base_get_chain_buffer_tracker(ioc, smid);
		if (!chain_req)
			return -1;
		chain = chain_req->chain_buffer;
		chain_dma = chain_req->chain_buffer_dma;
	} while (1);


 fill_in_last_segment:

	/* fill the last segment */
	while (sges_left > 0) {
		if (sges_left == 1)
			_base_add_sg_single_ieee(sg_local,
			    simple_sgl_flags_last, 0, sg_dma_len(sg_scmd),
			    sg_dma_address(sg_scmd));
		else
			_base_add_sg_single_ieee(sg_local, simple_sgl_flags, 0,
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		sg_scmd = sg_next(sg_scmd);
		sg_local += ioc->sge_size_ieee;
		sges_left--;
	}

	return 0;
}

/**
 * _base_build_sg_ieee - build generic sg for IEEE format
 * @ioc: per adapter object
 * @psge: virtual address for SGE
 * @data_out_dma: physical address for WRITES
 * @data_out_sz: data xfer size for WRITES
 * @data_in_dma: physical address for READS
 * @data_in_sz: data xfer size for READS
 *
 * Return nothing.
 */
static void
_base_build_sg_ieee(struct MPT3SAS_ADAPTER *ioc, void *psge,
	dma_addr_t data_out_dma, size_t data_out_sz, dma_addr_t data_in_dma,
	size_t data_in_sz)
{
	u8 sgl_flags;

	if (!data_out_sz && !data_in_sz) {
		_base_build_zero_len_sge_ieee(ioc, psge);
		return;
	}

	if (data_out_sz && data_in_sz) {
		/* WRITE sgel first */
		sgl_flags = MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;
		_base_add_sg_single_ieee(psge, sgl_flags, 0, data_out_sz,
		    data_out_dma);

		/* incr sgel */
		psge += ioc->sge_size_ieee;

		/* READ sgel last */
		sgl_flags |= MPI25_IEEE_SGE_FLAGS_END_OF_LIST;
		_base_add_sg_single_ieee(psge, sgl_flags, 0, data_in_sz,
		    data_in_dma);
	} else if (data_out_sz) /* WRITE */ {
		sgl_flags = MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI25_IEEE_SGE_FLAGS_END_OF_LIST |
		    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;
		_base_add_sg_single_ieee(psge, sgl_flags, 0, data_out_sz,
		    data_out_dma);
	} else if (data_in_sz) /* READ */ {
		sgl_flags = MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI25_IEEE_SGE_FLAGS_END_OF_LIST |
		    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;
		_base_add_sg_single_ieee(psge, sgl_flags, 0, data_in_sz,
		    data_in_dma);
	}
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
_base_config_dma_addressing(struct MPT3SAS_ADAPTER *ioc, struct pci_dev *pdev)
{
	struct sysinfo s;
	u64 consistent_dma_mask;

	if (ioc->dma_mask)
		consistent_dma_mask = DMA_BIT_MASK(64);
	else
		consistent_dma_mask = DMA_BIT_MASK(32);

	if (sizeof(dma_addr_t) > 4) {
		const uint64_t required_mask =
		    dma_get_required_mask(&pdev->dev);
		if ((required_mask > DMA_BIT_MASK(32)) &&
		    !pci_set_dma_mask(pdev, DMA_BIT_MASK(64)) &&
		    !pci_set_consistent_dma_mask(pdev, consistent_dma_mask)) {
			ioc->base_add_sg_single = &_base_add_sg_single_64;
			ioc->sge_size = sizeof(Mpi2SGESimple64_t);
			ioc->dma_mask = 64;
			goto out;
		}
	}

	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))
	    && !pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32))) {
		ioc->base_add_sg_single = &_base_add_sg_single_32;
		ioc->sge_size = sizeof(Mpi2SGESimple32_t);
		ioc->dma_mask = 32;
	} else
		return -ENODEV;

 out:
	si_meminfo(&s);
	pr_info(MPT3SAS_FMT
		"%d BIT PCI BUS DMA ADDRESSING SUPPORTED, total mem (%ld kB)\n",
		ioc->name, ioc->dma_mask, convert_to_kb(s.totalram));

	return 0;
}

static int
_base_change_consistent_dma_mask(struct MPT3SAS_ADAPTER *ioc,
				      struct pci_dev *pdev)
{
	if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))) {
		if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)))
			return -ENODEV;
	}
	return 0;
}

/**
 * _base_check_enable_msix - checks MSIX capabable.
 * @ioc: per adapter object
 *
 * Check to see if card is capable of MSIX, and set number
 * of available msix vectors
 */
static int
_base_check_enable_msix(struct MPT3SAS_ADAPTER *ioc)
{
	int base;
	u16 message_control;

	/* Check whether controller SAS2008 B0 controller,
	 * if it is SAS2008 B0 controller use IO-APIC instead of MSIX
	 */
	if (ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2008 &&
	    ioc->pdev->revision == SAS2_PCI_DEVICE_B0_REVISION) {
		return -EINVAL;
	}

	base = pci_find_capability(ioc->pdev, PCI_CAP_ID_MSIX);
	if (!base) {
		dfailprintk(ioc, pr_info(MPT3SAS_FMT "msix not supported\n",
			ioc->name));
		return -EINVAL;
	}

	/* get msix vector count */
	/* NUMA_IO not supported for older controllers */
	if (ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2004 ||
	    ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2008 ||
	    ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2108_1 ||
	    ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2108_2 ||
	    ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2108_3 ||
	    ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2116_1 ||
	    ioc->pdev->device == MPI2_MFGPAGE_DEVID_SAS2116_2)
		ioc->msix_vector_count = 1;
	else {
		pci_read_config_word(ioc->pdev, base + 2, &message_control);
		ioc->msix_vector_count = (message_control & 0x3FF) + 1;
	}
	dinitprintk(ioc, pr_info(MPT3SAS_FMT
		"msix is supported, vector_count(%d)\n",
		ioc->name, ioc->msix_vector_count));
	return 0;
}

/**
 * _base_free_irq - free irq
 * @ioc: per adapter object
 *
 * Freeing respective reply_queue from the list.
 */
static void
_base_free_irq(struct MPT3SAS_ADAPTER *ioc)
{
	struct adapter_reply_queue *reply_q, *next;

	if (list_empty(&ioc->reply_queue_list))
		return;

	list_for_each_entry_safe(reply_q, next, &ioc->reply_queue_list, list) {
		list_del(&reply_q->list);
		if (smp_affinity_enable) {
			irq_set_affinity_hint(reply_q->vector, NULL);
			free_cpumask_var(reply_q->affinity_hint);
		}
		free_irq(reply_q->vector, reply_q);
		kfree(reply_q);
	}
}

/**
 * _base_request_irq - request irq
 * @ioc: per adapter object
 * @index: msix index into vector table
 * @vector: irq vector
 *
 * Inserting respective reply_queue into the list.
 */
static int
_base_request_irq(struct MPT3SAS_ADAPTER *ioc, u8 index, u32 vector)
{
	struct adapter_reply_queue *reply_q;
	int r;

	reply_q =  kzalloc(sizeof(struct adapter_reply_queue), GFP_KERNEL);
	if (!reply_q) {
		pr_err(MPT3SAS_FMT "unable to allocate memory %d!\n",
		    ioc->name, (int)sizeof(struct adapter_reply_queue));
		return -ENOMEM;
	}
	reply_q->ioc = ioc;
	reply_q->msix_index = index;
	reply_q->vector = vector;

	if (smp_affinity_enable) {
		if (!zalloc_cpumask_var(&reply_q->affinity_hint, GFP_KERNEL)) {
			kfree(reply_q);
			return -ENOMEM;
		}
	}

	atomic_set(&reply_q->busy, 0);
	if (ioc->msix_enable)
		snprintf(reply_q->name, MPT_NAME_LENGTH, "%s%d-msix%d",
		    ioc->driver_name, ioc->id, index);
	else
		snprintf(reply_q->name, MPT_NAME_LENGTH, "%s%d",
		    ioc->driver_name, ioc->id);
	r = request_irq(vector, _base_interrupt, IRQF_SHARED, reply_q->name,
	    reply_q);
	if (r) {
		pr_err(MPT3SAS_FMT "unable to allocate interrupt %d!\n",
		    reply_q->name, vector);
		free_cpumask_var(reply_q->affinity_hint);
		kfree(reply_q);
		return -EBUSY;
	}

	INIT_LIST_HEAD(&reply_q->list);
	list_add_tail(&reply_q->list, &ioc->reply_queue_list);
	return 0;
}

/**
 * _base_assign_reply_queues - assigning msix index for each cpu
 * @ioc: per adapter object
 *
 * The enduser would need to set the affinity via /proc/irq/#/smp_affinity
 *
 * It would nice if we could call irq_set_affinity, however it is not
 * an exported symbol
 */
static void
_base_assign_reply_queues(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned int cpu, nr_cpus, nr_msix, index = 0;
	struct adapter_reply_queue *reply_q;

	if (!_base_is_controller_msix_enabled(ioc))
		return;

	memset(ioc->cpu_msix_table, 0, ioc->cpu_msix_table_sz);

	nr_cpus = num_online_cpus();
	nr_msix = ioc->reply_queue_count = min(ioc->reply_queue_count,
					       ioc->facts.MaxMSIxVectors);
	if (!nr_msix)
		return;

	cpu = cpumask_first(cpu_online_mask);

	list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {

		unsigned int i, group = nr_cpus / nr_msix;

		if (cpu >= nr_cpus)
			break;

		if (index < nr_cpus % nr_msix)
			group++;

		for (i = 0 ; i < group ; i++) {
			ioc->cpu_msix_table[cpu] = index;
			if (smp_affinity_enable)
				cpumask_or(reply_q->affinity_hint,
				   reply_q->affinity_hint, get_cpu_mask(cpu));
			cpu = cpumask_next(cpu, cpu_online_mask);
		}
		if (smp_affinity_enable)
			if (irq_set_affinity_hint(reply_q->vector,
					   reply_q->affinity_hint))
				dinitprintk(ioc, pr_info(MPT3SAS_FMT
				 "Err setting affinity hint to irq vector %d\n",
				 ioc->name, reply_q->vector));
		index++;
	}
}

/**
 * _base_disable_msix - disables msix
 * @ioc: per adapter object
 *
 */
static void
_base_disable_msix(struct MPT3SAS_ADAPTER *ioc)
{
	if (!ioc->msix_enable)
		return;
	pci_disable_msix(ioc->pdev);
	ioc->msix_enable = 0;
}

/**
 * _base_enable_msix - enables msix, failback to io_apic
 * @ioc: per adapter object
 *
 */
static int
_base_enable_msix(struct MPT3SAS_ADAPTER *ioc)
{
	struct msix_entry *entries, *a;
	int r;
	int i, local_max_msix_vectors;
	u8 try_msix = 0;

	if (msix_disable == -1 || msix_disable == 0)
		try_msix = 1;

	if (!try_msix)
		goto try_ioapic;

	if (_base_check_enable_msix(ioc) != 0)
		goto try_ioapic;

	ioc->reply_queue_count = min_t(int, ioc->cpu_count,
	    ioc->msix_vector_count);

	printk(MPT3SAS_FMT "MSI-X vectors supported: %d, no of cores"
	  ": %d, max_msix_vectors: %d\n", ioc->name, ioc->msix_vector_count,
	  ioc->cpu_count, max_msix_vectors);

	if (!ioc->rdpq_array_enable && max_msix_vectors == -1)
		local_max_msix_vectors = 8;
	else
		local_max_msix_vectors = max_msix_vectors;

	if (local_max_msix_vectors > 0) {
		ioc->reply_queue_count = min_t(int, local_max_msix_vectors,
			ioc->reply_queue_count);
		ioc->msix_vector_count = ioc->reply_queue_count;
	} else if (local_max_msix_vectors == 0)
		goto try_ioapic;

	if (ioc->msix_vector_count < ioc->cpu_count)
		smp_affinity_enable = 0;

	entries = kcalloc(ioc->reply_queue_count, sizeof(struct msix_entry),
	    GFP_KERNEL);
	if (!entries) {
		dfailprintk(ioc, pr_info(MPT3SAS_FMT
			"kcalloc failed @ at %s:%d/%s() !!!\n",
			ioc->name, __FILE__, __LINE__, __func__));
		goto try_ioapic;
	}

	for (i = 0, a = entries; i < ioc->reply_queue_count; i++, a++)
		a->entry = i;

	r = pci_enable_msix_exact(ioc->pdev, entries, ioc->reply_queue_count);
	if (r) {
		dfailprintk(ioc, pr_info(MPT3SAS_FMT
			"pci_enable_msix_exact failed (r=%d) !!!\n",
			ioc->name, r));
		kfree(entries);
		goto try_ioapic;
	}

	ioc->msix_enable = 1;
	for (i = 0, a = entries; i < ioc->reply_queue_count; i++, a++) {
		r = _base_request_irq(ioc, i, a->vector);
		if (r) {
			_base_free_irq(ioc);
			_base_disable_msix(ioc);
			kfree(entries);
			goto try_ioapic;
		}
	}

	kfree(entries);
	return 0;

/* failback to io_apic interrupt routing */
 try_ioapic:

	ioc->reply_queue_count = 1;
	r = _base_request_irq(ioc, 0, ioc->pdev->irq);

	return r;
}

/**
 * mpt3sas_base_unmap_resources - free controller resources
 * @ioc: per adapter object
 */
static void
mpt3sas_base_unmap_resources(struct MPT3SAS_ADAPTER *ioc)
{
	struct pci_dev *pdev = ioc->pdev;

	dexitprintk(ioc, printk(MPT3SAS_FMT "%s\n",
		ioc->name, __func__));

	_base_free_irq(ioc);
	_base_disable_msix(ioc);

	if (ioc->combined_reply_queue) {
		kfree(ioc->replyPostRegisterIndex);
		ioc->replyPostRegisterIndex = NULL;
	}

	if (ioc->chip_phys) {
		iounmap(ioc->chip);
		ioc->chip_phys = 0;
	}

	if (pci_is_enabled(pdev)) {
		pci_release_selected_regions(ioc->pdev, ioc->bars);
		pci_disable_pcie_error_reporting(pdev);
		pci_disable_device(pdev);
	}
}

/**
 * mpt3sas_base_map_resources - map in controller resources (io/irq/memap)
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt3sas_base_map_resources(struct MPT3SAS_ADAPTER *ioc)
{
	struct pci_dev *pdev = ioc->pdev;
	u32 memap_sz;
	u32 pio_sz;
	int i, r = 0;
	u64 pio_chip = 0;
	u64 chip_phys = 0;
	struct adapter_reply_queue *reply_q;

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n",
	    ioc->name, __func__));

	ioc->bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if (pci_enable_device_mem(pdev)) {
		pr_warn(MPT3SAS_FMT "pci_enable_device_mem: failed\n",
			ioc->name);
		ioc->bars = 0;
		return -ENODEV;
	}


	if (pci_request_selected_regions(pdev, ioc->bars,
	    ioc->driver_name)) {
		pr_warn(MPT3SAS_FMT "pci_request_selected_regions: failed\n",
			ioc->name);
		ioc->bars = 0;
		r = -ENODEV;
		goto out_fail;
	}

/* AER (Advanced Error Reporting) hooks */
	pci_enable_pcie_error_reporting(pdev);

	pci_set_master(pdev);


	if (_base_config_dma_addressing(ioc, pdev) != 0) {
		pr_warn(MPT3SAS_FMT "no suitable DMA mask for %s\n",
		    ioc->name, pci_name(pdev));
		r = -ENODEV;
		goto out_fail;
	}

	for (i = 0, memap_sz = 0, pio_sz = 0; (i < DEVICE_COUNT_RESOURCE) &&
	     (!memap_sz || !pio_sz); i++) {
		if (pci_resource_flags(pdev, i) & IORESOURCE_IO) {
			if (pio_sz)
				continue;
			pio_chip = (u64)pci_resource_start(pdev, i);
			pio_sz = pci_resource_len(pdev, i);
		} else if (pci_resource_flags(pdev, i) & IORESOURCE_MEM) {
			if (memap_sz)
				continue;
			ioc->chip_phys = pci_resource_start(pdev, i);
			chip_phys = (u64)ioc->chip_phys;
			memap_sz = pci_resource_len(pdev, i);
			ioc->chip = ioremap(ioc->chip_phys, memap_sz);
		}
	}

	if (ioc->chip == NULL) {
		pr_err(MPT3SAS_FMT "unable to map adapter memory! "
			" or resource not found\n", ioc->name);
		r = -EINVAL;
		goto out_fail;
	}

	_base_mask_interrupts(ioc);

	r = _base_get_ioc_facts(ioc);
	if (r)
		goto out_fail;

	if (!ioc->rdpq_array_enable_assigned) {
		ioc->rdpq_array_enable = ioc->rdpq_array_capable;
		ioc->rdpq_array_enable_assigned = 1;
	}

	r = _base_enable_msix(ioc);
	if (r)
		goto out_fail;

	/* Use the Combined reply queue feature only for SAS3 C0 & higher
	 * revision HBAs and also only when reply queue count is greater than 8
	 */
	if (ioc->combined_reply_queue && ioc->reply_queue_count > 8) {
		/* Determine the Supplemental Reply Post Host Index Registers
		 * Addresse. Supplemental Reply Post Host Index Registers
		 * starts at offset MPI25_SUP_REPLY_POST_HOST_INDEX_OFFSET and
		 * each register is at offset bytes of
		 * MPT3_SUP_REPLY_POST_HOST_INDEX_REG_OFFSET from previous one.
		 */
		ioc->replyPostRegisterIndex = kcalloc(
		     ioc->combined_reply_index_count,
		     sizeof(resource_size_t *), GFP_KERNEL);
		if (!ioc->replyPostRegisterIndex) {
			dfailprintk(ioc, printk(MPT3SAS_FMT
			"allocation for reply Post Register Index failed!!!\n",
								   ioc->name));
			r = -ENOMEM;
			goto out_fail;
		}

		for (i = 0; i < ioc->combined_reply_index_count; i++) {
			ioc->replyPostRegisterIndex[i] = (resource_size_t *)
			     ((u8 *)&ioc->chip->Doorbell +
			     MPI25_SUP_REPLY_POST_HOST_INDEX_OFFSET +
			     (i * MPT3_SUP_REPLY_POST_HOST_INDEX_REG_OFFSET));
		}
	} else
		ioc->combined_reply_queue = 0;

	if (ioc->is_warpdrive) {
		ioc->reply_post_host_index[0] = (resource_size_t __iomem *)
		    &ioc->chip->ReplyPostHostIndex;

		for (i = 1; i < ioc->cpu_msix_table_sz; i++)
			ioc->reply_post_host_index[i] =
			(resource_size_t __iomem *)
			((u8 __iomem *)&ioc->chip->Doorbell + (0x4000 + ((i - 1)
			* 4)));
	}

	list_for_each_entry(reply_q, &ioc->reply_queue_list, list)
		pr_info(MPT3SAS_FMT "%s: IRQ %d\n",
		    reply_q->name,  ((ioc->msix_enable) ? "PCI-MSI-X enabled" :
		    "IO-APIC enabled"), reply_q->vector);

	pr_info(MPT3SAS_FMT "iomem(0x%016llx), mapped(0x%p), size(%d)\n",
	    ioc->name, (unsigned long long)chip_phys, ioc->chip, memap_sz);
	pr_info(MPT3SAS_FMT "ioport(0x%016llx), size(%d)\n",
	    ioc->name, (unsigned long long)pio_chip, pio_sz);

	/* Save PCI configuration state for recovery from PCI AER/EEH errors */
	pci_save_state(pdev);
	return 0;

 out_fail:
	mpt3sas_base_unmap_resources(ioc);
	return r;
}

/**
 * mpt3sas_base_get_msg_frame - obtain request mf pointer
 * @ioc: per adapter object
 * @smid: system request message index(smid zero is invalid)
 *
 * Returns virt pointer to message frame.
 */
void *
mpt3sas_base_get_msg_frame(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return (void *)(ioc->request + (smid * ioc->request_sz));
}

/**
 * mpt3sas_base_get_sense_buffer - obtain a sense buffer virt addr
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns virt pointer to sense buffer.
 */
void *
mpt3sas_base_get_sense_buffer(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return (void *)(ioc->sense + ((smid - 1) * SCSI_SENSE_BUFFERSIZE));
}

/**
 * mpt3sas_base_get_sense_buffer_dma - obtain a sense buffer dma addr
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns phys pointer to the low 32bit address of the sense buffer.
 */
__le32
mpt3sas_base_get_sense_buffer_dma(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return cpu_to_le32(ioc->sense_dma + ((smid - 1) *
	    SCSI_SENSE_BUFFERSIZE));
}

/**
 * mpt3sas_base_get_reply_virt_addr - obtain reply frames virt address
 * @ioc: per adapter object
 * @phys_addr: lower 32 physical addr of the reply
 *
 * Converts 32bit lower physical addr into a virt address.
 */
void *
mpt3sas_base_get_reply_virt_addr(struct MPT3SAS_ADAPTER *ioc, u32 phys_addr)
{
	if (!phys_addr)
		return NULL;
	return ioc->reply + (phys_addr - (u32)ioc->reply_dma);
}

static inline u8
_base_get_msix_index(struct MPT3SAS_ADAPTER *ioc)
{
	return ioc->cpu_msix_table[raw_smp_processor_id()];
}

/**
 * mpt3sas_base_get_smid - obtain a free smid from internal queue
 * @ioc: per adapter object
 * @cb_idx: callback index
 *
 * Returns smid (zero is invalid)
 */
u16
mpt3sas_base_get_smid(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx)
{
	unsigned long flags;
	struct request_tracker *request;
	u16 smid;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (list_empty(&ioc->internal_free_list)) {
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		pr_err(MPT3SAS_FMT "%s: smid not available\n",
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
 * mpt3sas_base_get_smid_scsiio - obtain a free smid from scsiio queue
 * @ioc: per adapter object
 * @cb_idx: callback index
 * @scmd: pointer to scsi command object
 *
 * Returns smid (zero is invalid)
 */
u16
mpt3sas_base_get_smid_scsiio(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx,
	struct scsi_cmnd *scmd)
{
	unsigned long flags;
	struct scsiio_tracker *request;
	u16 smid;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (list_empty(&ioc->free_list)) {
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		pr_err(MPT3SAS_FMT "%s: smid not available\n",
		    ioc->name, __func__);
		return 0;
	}

	request = list_entry(ioc->free_list.next,
	    struct scsiio_tracker, tracker_list);
	request->scmd = scmd;
	request->cb_idx = cb_idx;
	smid = request->smid;
	request->msix_io = _base_get_msix_index(ioc);
	list_del(&request->tracker_list);
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return smid;
}

/**
 * mpt3sas_base_get_smid_hpr - obtain a free smid from hi-priority queue
 * @ioc: per adapter object
 * @cb_idx: callback index
 *
 * Returns smid (zero is invalid)
 */
u16
mpt3sas_base_get_smid_hpr(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx)
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
 * mpt3sas_base_free_smid - put smid back on free_list
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return nothing.
 */
void
mpt3sas_base_free_smid(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	unsigned long flags;
	int i;
	struct chain_tracker *chain_req, *next;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (smid < ioc->hi_priority_smid) {
		/* scsiio queue */
		i = smid - 1;
		if (!list_empty(&ioc->scsi_lookup[i].chain_list)) {
			list_for_each_entry_safe(chain_req, next,
			    &ioc->scsi_lookup[i].chain_list, tracker_list) {
				list_del_init(&chain_req->tracker_list);
				list_add(&chain_req->tracker_list,
				    &ioc->free_chain_list);
			}
		}
		ioc->scsi_lookup[i].cb_idx = 0xFF;
		ioc->scsi_lookup[i].scmd = NULL;
		ioc->scsi_lookup[i].direct_io = 0;
		list_add(&ioc->scsi_lookup[i].tracker_list, &ioc->free_list);
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

		/*
		 * See _wait_for_commands_to_complete() call with regards
		 * to this code.
		 */
		if (ioc->shost_recovery && ioc->pending_io_count) {
			if (ioc->pending_io_count == 1)
				wake_up(&ioc->reset_wq);
			ioc->pending_io_count--;
		}
		return;
	} else if (smid < ioc->internal_smid) {
		/* hi-priority */
		i = smid - ioc->hi_priority_smid;
		ioc->hpr_lookup[i].cb_idx = 0xFF;
		list_add(&ioc->hpr_lookup[i].tracker_list, &ioc->hpr_free_list);
	} else if (smid <= ioc->hba_queue_depth) {
		/* internal queue */
		i = smid - ioc->internal_smid;
		ioc->internal_lookup[i].cb_idx = 0xFF;
		list_add(&ioc->internal_lookup[i].tracker_list,
		    &ioc->internal_free_list);
	}
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
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
#if defined(writeq) && defined(CONFIG_64BIT)
static inline void
_base_writeq(__u64 b, volatile void __iomem *addr, spinlock_t *writeq_lock)
{
	writeq(cpu_to_le64(b), addr);
}
#else
static inline void
_base_writeq(__u64 b, volatile void __iomem *addr, spinlock_t *writeq_lock)
{
	unsigned long flags;
	__u64 data_out = cpu_to_le64(b);

	spin_lock_irqsave(writeq_lock, flags);
	writel((u32)(data_out), addr);
	writel((u32)(data_out >> 32), (addr + 4));
	spin_unlock_irqrestore(writeq_lock, flags);
}
#endif

/**
 * _base_put_smid_scsi_io - send SCSI_IO request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @handle: device handle
 *
 * Return nothing.
 */
static void
_base_put_smid_scsi_io(struct MPT3SAS_ADAPTER *ioc, u16 smid, u16 handle)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;


	descriptor.SCSIIO.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO;
	descriptor.SCSIIO.MSIxIndex =  _base_get_msix_index(ioc);
	descriptor.SCSIIO.SMID = cpu_to_le16(smid);
	descriptor.SCSIIO.DevHandle = cpu_to_le16(handle);
	descriptor.SCSIIO.LMID = 0;
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * _base_put_smid_fast_path - send fast path request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @handle: device handle
 *
 * Return nothing.
 */
static void
_base_put_smid_fast_path(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 handle)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;

	descriptor.SCSIIO.RequestFlags =
	    MPI25_REQ_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO;
	descriptor.SCSIIO.MSIxIndex = _base_get_msix_index(ioc);
	descriptor.SCSIIO.SMID = cpu_to_le16(smid);
	descriptor.SCSIIO.DevHandle = cpu_to_le16(handle);
	descriptor.SCSIIO.LMID = 0;
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * _base_put_smid_hi_priority - send Task Management request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_task: msix_task will be same as msix of IO incase of task abort else 0.
 * Return nothing.
 */
static void
_base_put_smid_hi_priority(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 msix_task)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;

	descriptor.HighPriority.RequestFlags =
	    MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY;
	descriptor.HighPriority.MSIxIndex =  msix_task;
	descriptor.HighPriority.SMID = cpu_to_le16(smid);
	descriptor.HighPriority.LMID = 0;
	descriptor.HighPriority.Reserved1 = 0;
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * _base_put_smid_default - Default, primarily used for config pages
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return nothing.
 */
static void
_base_put_smid_default(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;

	descriptor.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	descriptor.Default.MSIxIndex =  _base_get_msix_index(ioc);
	descriptor.Default.SMID = cpu_to_le16(smid);
	descriptor.Default.LMID = 0;
	descriptor.Default.DescriptorTypeDependent = 0;
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
* _base_put_smid_scsi_io_atomic - send SCSI_IO request to firmware using
*   Atomic Request Descriptor
* @ioc: per adapter object
* @smid: system request message index
* @handle: device handle, unused in this function, for function type match
*
* Return nothing.
*/
static void
_base_put_smid_scsi_io_atomic(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 handle)
{
	Mpi26AtomicRequestDescriptor_t descriptor;
	u32 *request = (u32 *)&descriptor;

	descriptor.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO;
	descriptor.MSIxIndex = _base_get_msix_index(ioc);
	descriptor.SMID = cpu_to_le16(smid);

	writel(cpu_to_le32(*request), &ioc->chip->AtomicRequestDescriptorPost);
}

/**
 * _base_put_smid_fast_path_atomic - send fast path request to firmware
 * using Atomic Request Descriptor
 * @ioc: per adapter object
 * @smid: system request message index
 * @handle: device handle, unused in this function, for function type match
 * Return nothing
 */
static void
_base_put_smid_fast_path_atomic(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 handle)
{
	Mpi26AtomicRequestDescriptor_t descriptor;
	u32 *request = (u32 *)&descriptor;

	descriptor.RequestFlags = MPI25_REQ_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO;
	descriptor.MSIxIndex = _base_get_msix_index(ioc);
	descriptor.SMID = cpu_to_le16(smid);

	writel(cpu_to_le32(*request), &ioc->chip->AtomicRequestDescriptorPost);
}

/**
 * _base_put_smid_hi_priority_atomic - send Task Management request to
 * firmware using Atomic Request Descriptor
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_task: msix_task will be same as msix of IO incase of task abort else 0
 *
 * Return nothing.
 */
static void
_base_put_smid_hi_priority_atomic(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 msix_task)
{
	Mpi26AtomicRequestDescriptor_t descriptor;
	u32 *request = (u32 *)&descriptor;

	descriptor.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY;
	descriptor.MSIxIndex = msix_task;
	descriptor.SMID = cpu_to_le16(smid);

	writel(cpu_to_le32(*request), &ioc->chip->AtomicRequestDescriptorPost);
}

/**
 * _base_put_smid_default - Default, primarily used for config pages
 * use Atomic Request Descriptor
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return nothing.
 */
static void
_base_put_smid_default_atomic(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	Mpi26AtomicRequestDescriptor_t descriptor;
	u32 *request = (u32 *)&descriptor;

	descriptor.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	descriptor.MSIxIndex = _base_get_msix_index(ioc);
	descriptor.SMID = cpu_to_le16(smid);

	writel(cpu_to_le32(*request), &ioc->chip->AtomicRequestDescriptorPost);
}

/**
 * _base_display_OEMs_branding - Display branding string
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_base_display_OEMs_branding(struct MPT3SAS_ADAPTER *ioc)
{
	if (ioc->pdev->subsystem_vendor != PCI_VENDOR_ID_INTEL)
		return;

	switch (ioc->pdev->subsystem_vendor) {
	case PCI_VENDOR_ID_INTEL:
		switch (ioc->pdev->device) {
		case MPI2_MFGPAGE_DEVID_SAS2008:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_INTEL_RMS2LL080_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS2LL080_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS2LL040_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS2LL040_BRANDING);
				break;
			case MPT2SAS_INTEL_SSD910_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_SSD910_BRANDING);
				break;
			default:
				pr_info(MPT3SAS_FMT
				 "Intel(R) Controller: Subsystem ID: 0x%X\n",
				 ioc->name, ioc->pdev->subsystem_device);
				break;
			}
		case MPI2_MFGPAGE_DEVID_SAS2308_2:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_INTEL_RS25GB008_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RS25GB008_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25JB080_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS25JB080_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25JB040_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS25JB040_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25KB080_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS25KB080_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25KB040_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS25KB040_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25LB040_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS25LB040_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25LB080_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS25LB080_BRANDING);
				break;
			default:
				pr_info(MPT3SAS_FMT
				 "Intel(R) Controller: Subsystem ID: 0x%X\n",
				 ioc->name, ioc->pdev->subsystem_device);
				break;
			}
		case MPI25_MFGPAGE_DEVID_SAS3008:
			switch (ioc->pdev->subsystem_device) {
			case MPT3SAS_INTEL_RMS3JC080_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
					MPT3SAS_INTEL_RMS3JC080_BRANDING);
				break;

			case MPT3SAS_INTEL_RS3GC008_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
					MPT3SAS_INTEL_RS3GC008_BRANDING);
				break;
			case MPT3SAS_INTEL_RS3FC044_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
					MPT3SAS_INTEL_RS3FC044_BRANDING);
				break;
			case MPT3SAS_INTEL_RS3UC080_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
					MPT3SAS_INTEL_RS3UC080_BRANDING);
				break;
			default:
				pr_info(MPT3SAS_FMT
				 "Intel(R) Controller: Subsystem ID: 0x%X\n",
				 ioc->name, ioc->pdev->subsystem_device);
				break;
			}
			break;
		default:
			pr_info(MPT3SAS_FMT
			 "Intel(R) Controller: Subsystem ID: 0x%X\n",
			 ioc->name, ioc->pdev->subsystem_device);
			break;
		}
		break;
	case PCI_VENDOR_ID_DELL:
		switch (ioc->pdev->device) {
		case MPI2_MFGPAGE_DEVID_SAS2008:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_DELL_6GBPS_SAS_HBA_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				 MPT2SAS_DELL_6GBPS_SAS_HBA_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_ADAPTER_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				 MPT2SAS_DELL_PERC_H200_ADAPTER_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_INTEGRATED_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				 MPT2SAS_DELL_PERC_H200_INTEGRATED_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_MODULAR_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				 MPT2SAS_DELL_PERC_H200_MODULAR_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_EMBEDDED_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				 MPT2SAS_DELL_PERC_H200_EMBEDDED_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				 MPT2SAS_DELL_PERC_H200_BRANDING);
				break;
			case MPT2SAS_DELL_6GBPS_SAS_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				 MPT2SAS_DELL_6GBPS_SAS_BRANDING);
				break;
			default:
				pr_info(MPT3SAS_FMT
				   "Dell 6Gbps HBA: Subsystem ID: 0x%X\n",
				   ioc->name, ioc->pdev->subsystem_device);
				break;
			}
			break;
		case MPI25_MFGPAGE_DEVID_SAS3008:
			switch (ioc->pdev->subsystem_device) {
			case MPT3SAS_DELL_12G_HBA_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
					MPT3SAS_DELL_12G_HBA_BRANDING);
				break;
			default:
				pr_info(MPT3SAS_FMT
				   "Dell 12Gbps HBA: Subsystem ID: 0x%X\n",
				   ioc->name, ioc->pdev->subsystem_device);
				break;
			}
			break;
		default:
			pr_info(MPT3SAS_FMT
			   "Dell HBA: Subsystem ID: 0x%X\n", ioc->name,
			   ioc->pdev->subsystem_device);
			break;
		}
		break;
	case PCI_VENDOR_ID_CISCO:
		switch (ioc->pdev->device) {
		case MPI25_MFGPAGE_DEVID_SAS3008:
			switch (ioc->pdev->subsystem_device) {
			case MPT3SAS_CISCO_12G_8E_HBA_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
					MPT3SAS_CISCO_12G_8E_HBA_BRANDING);
				break;
			case MPT3SAS_CISCO_12G_8I_HBA_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
					MPT3SAS_CISCO_12G_8I_HBA_BRANDING);
				break;
			case MPT3SAS_CISCO_12G_AVILA_HBA_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
					MPT3SAS_CISCO_12G_AVILA_HBA_BRANDING);
				break;
			default:
				pr_info(MPT3SAS_FMT
				  "Cisco 12Gbps SAS HBA: Subsystem ID: 0x%X\n",
				  ioc->name, ioc->pdev->subsystem_device);
				break;
			}
			break;
		case MPI25_MFGPAGE_DEVID_SAS3108_1:
			switch (ioc->pdev->subsystem_device) {
			case MPT3SAS_CISCO_12G_AVILA_HBA_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				MPT3SAS_CISCO_12G_AVILA_HBA_BRANDING);
				break;
			case MPT3SAS_CISCO_12G_COLUSA_MEZZANINE_HBA_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				MPT3SAS_CISCO_12G_COLUSA_MEZZANINE_HBA_BRANDING
				);
				break;
			default:
				pr_info(MPT3SAS_FMT
				 "Cisco 12Gbps SAS HBA: Subsystem ID: 0x%X\n",
				 ioc->name, ioc->pdev->subsystem_device);
				break;
			}
			break;
		default:
			pr_info(MPT3SAS_FMT
			   "Cisco SAS HBA: Subsystem ID: 0x%X\n",
			   ioc->name, ioc->pdev->subsystem_device);
			break;
		}
		break;
	case MPT2SAS_HP_3PAR_SSVID:
		switch (ioc->pdev->device) {
		case MPI2_MFGPAGE_DEVID_SAS2004:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_HP_DAUGHTER_2_4_INTERNAL_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_HP_DAUGHTER_2_4_INTERNAL_BRANDING);
				break;
			default:
				pr_info(MPT3SAS_FMT
				   "HP 6Gbps SAS HBA: Subsystem ID: 0x%X\n",
				   ioc->name, ioc->pdev->subsystem_device);
				break;
			}
		case MPI2_MFGPAGE_DEVID_SAS2308_2:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_HP_2_4_INTERNAL_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_HP_2_4_INTERNAL_BRANDING);
				break;
			case MPT2SAS_HP_2_4_EXTERNAL_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_HP_2_4_EXTERNAL_BRANDING);
				break;
			case MPT2SAS_HP_1_4_INTERNAL_1_4_EXTERNAL_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				 MPT2SAS_HP_1_4_INTERNAL_1_4_EXTERNAL_BRANDING);
				break;
			case MPT2SAS_HP_EMBEDDED_2_4_INTERNAL_SSDID:
				pr_info(MPT3SAS_FMT "%s\n", ioc->name,
				    MPT2SAS_HP_EMBEDDED_2_4_INTERNAL_BRANDING);
				break;
			default:
				pr_info(MPT3SAS_FMT
				   "HP 6Gbps SAS HBA: Subsystem ID: 0x%X\n",
				   ioc->name, ioc->pdev->subsystem_device);
				break;
			}
		default:
			pr_info(MPT3SAS_FMT
			   "HP SAS HBA: Subsystem ID: 0x%X\n",
			   ioc->name, ioc->pdev->subsystem_device);
			break;
		}
	default:
		break;
	}
}

/**
 * _base_display_ioc_capabilities - Disply IOC's capabilities.
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_base_display_ioc_capabilities(struct MPT3SAS_ADAPTER *ioc)
{
	int i = 0;
	char desc[16];
	u32 iounit_pg1_flags;
	u32 bios_version;

	bios_version = le32_to_cpu(ioc->bios_pg3.BiosVersion);
	strncpy(desc, ioc->manu_pg0.ChipName, 16);
	pr_info(MPT3SAS_FMT "%s: FWVersion(%02d.%02d.%02d.%02d), "\
	   "ChipRevision(0x%02x), BiosVersion(%02d.%02d.%02d.%02d)\n",
	    ioc->name, desc,
	   (ioc->facts.FWVersion.Word & 0xFF000000) >> 24,
	   (ioc->facts.FWVersion.Word & 0x00FF0000) >> 16,
	   (ioc->facts.FWVersion.Word & 0x0000FF00) >> 8,
	   ioc->facts.FWVersion.Word & 0x000000FF,
	   ioc->pdev->revision,
	   (bios_version & 0xFF000000) >> 24,
	   (bios_version & 0x00FF0000) >> 16,
	   (bios_version & 0x0000FF00) >> 8,
	    bios_version & 0x000000FF);

	_base_display_OEMs_branding(ioc);

	pr_info(MPT3SAS_FMT "Protocol=(", ioc->name);

	if (ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_SCSI_INITIATOR) {
		pr_info("Initiator");
		i++;
	}

	if (ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_SCSI_TARGET) {
		pr_info("%sTarget", i ? "," : "");
		i++;
	}

	i = 0;
	pr_info("), ");
	pr_info("Capabilities=(");

	if (!ioc->hide_ir_msg) {
		if (ioc->facts.IOCCapabilities &
		    MPI2_IOCFACTS_CAPABILITY_INTEGRATED_RAID) {
			pr_info("Raid");
			i++;
		}
	}

	if (ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_TLR) {
		pr_info("%sTLR", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_MULTICAST) {
		pr_info("%sMulticast", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_BIDIRECTIONAL_TARGET) {
		pr_info("%sBIDI Target", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_EEDP) {
		pr_info("%sEEDP", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_SNAPSHOT_BUFFER) {
		pr_info("%sSnapshot Buffer", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER) {
		pr_info("%sDiag Trace Buffer", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_EXTENDED_BUFFER) {
		pr_info("%sDiag Extended Buffer", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_TASK_SET_FULL_HANDLING) {
		pr_info("%sTask Set Full", i ? "," : "");
		i++;
	}

	iounit_pg1_flags = le32_to_cpu(ioc->iounit_pg1.Flags);
	if (!(iounit_pg1_flags & MPI2_IOUNITPAGE1_NATIVE_COMMAND_Q_DISABLE)) {
		pr_info("%sNCQ", i ? "," : "");
		i++;
	}

	pr_info(")\n");
}

/**
 * mpt3sas_base_update_missing_delay - change the missing delay timers
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
void
mpt3sas_base_update_missing_delay(struct MPT3SAS_ADAPTER *ioc,
	u16 device_missing_delay, u8 io_missing_delay)
{
	u16 dmd, dmd_new, dmd_orignal;
	u8 io_missing_delay_original;
	u16 sz;
	Mpi2SasIOUnitPage1_t *sas_iounit_pg1 = NULL;
	Mpi2ConfigReply_t mpi_reply;
	u8 num_phys = 0;
	u16 ioc_status;

	mpt3sas_config_get_number_hba_phys(ioc, &num_phys);
	if (!num_phys)
		return;

	sz = offsetof(Mpi2SasIOUnitPage1_t, PhyData) + (num_phys *
	    sizeof(Mpi2SasIOUnit1PhyData_t));
	sas_iounit_pg1 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg1) {
		pr_err(MPT3SAS_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	if ((mpt3sas_config_get_sas_iounit_pg1(ioc, &mpi_reply,
	    sas_iounit_pg1, sz))) {
		pr_err(MPT3SAS_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		pr_err(MPT3SAS_FMT "failure at %s:%d/%s()!\n",
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

	if (!mpt3sas_config_set_sas_iounit_pg1(ioc, &mpi_reply, sas_iounit_pg1,
	    sz)) {
		if (dmd & MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16)
			dmd_new = (dmd &
			    MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16;
		else
			dmd_new =
		    dmd & MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK;
		pr_info(MPT3SAS_FMT "device_missing_delay: old(%d), new(%d)\n",
			ioc->name, dmd_orignal, dmd_new);
		pr_info(MPT3SAS_FMT "ioc_missing_delay: old(%d), new(%d)\n",
			ioc->name, io_missing_delay_original,
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
_base_static_config_pages(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2ConfigReply_t mpi_reply;
	u32 iounit_pg1_flags;

	mpt3sas_config_get_manufacturing_pg0(ioc, &mpi_reply, &ioc->manu_pg0);
	if (ioc->ir_firmware)
		mpt3sas_config_get_manufacturing_pg10(ioc, &mpi_reply,
		    &ioc->manu_pg10);

	/*
	 * Ensure correct T10 PI operation if vendor left EEDPTagMode
	 * flag unset in NVDATA.
	 */
	mpt3sas_config_get_manufacturing_pg11(ioc, &mpi_reply, &ioc->manu_pg11);
	if (ioc->manu_pg11.EEDPTagMode == 0) {
		pr_err("%s: overriding NVDATA EEDPTagMode setting\n",
		    ioc->name);
		ioc->manu_pg11.EEDPTagMode &= ~0x3;
		ioc->manu_pg11.EEDPTagMode |= 0x1;
		mpt3sas_config_set_manufacturing_pg11(ioc, &mpi_reply,
		    &ioc->manu_pg11);
	}

	mpt3sas_config_get_bios_pg2(ioc, &mpi_reply, &ioc->bios_pg2);
	mpt3sas_config_get_bios_pg3(ioc, &mpi_reply, &ioc->bios_pg3);
	mpt3sas_config_get_ioc_pg8(ioc, &mpi_reply, &ioc->ioc_pg8);
	mpt3sas_config_get_iounit_pg0(ioc, &mpi_reply, &ioc->iounit_pg0);
	mpt3sas_config_get_iounit_pg1(ioc, &mpi_reply, &ioc->iounit_pg1);
	mpt3sas_config_get_iounit_pg8(ioc, &mpi_reply, &ioc->iounit_pg8);
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
	mpt3sas_config_set_iounit_pg1(ioc, &mpi_reply, &ioc->iounit_pg1);

	if (ioc->iounit_pg8.NumSensors)
		ioc->temp_sensors_count = ioc->iounit_pg8.NumSensors;
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
_base_release_memory_pools(struct MPT3SAS_ADAPTER *ioc)
{
	int i = 0;
	struct reply_post_struct *rps;

	dexitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	if (ioc->request) {
		pci_free_consistent(ioc->pdev, ioc->request_dma_sz,
		    ioc->request,  ioc->request_dma);
		dexitprintk(ioc, pr_info(MPT3SAS_FMT
			"request_pool(0x%p): free\n",
			ioc->name, ioc->request));
		ioc->request = NULL;
	}

	if (ioc->sense) {
		pci_pool_free(ioc->sense_dma_pool, ioc->sense, ioc->sense_dma);
		if (ioc->sense_dma_pool)
			pci_pool_destroy(ioc->sense_dma_pool);
		dexitprintk(ioc, pr_info(MPT3SAS_FMT
			"sense_pool(0x%p): free\n",
			ioc->name, ioc->sense));
		ioc->sense = NULL;
	}

	if (ioc->reply) {
		pci_pool_free(ioc->reply_dma_pool, ioc->reply, ioc->reply_dma);
		if (ioc->reply_dma_pool)
			pci_pool_destroy(ioc->reply_dma_pool);
		dexitprintk(ioc, pr_info(MPT3SAS_FMT
			"reply_pool(0x%p): free\n",
			ioc->name, ioc->reply));
		ioc->reply = NULL;
	}

	if (ioc->reply_free) {
		pci_pool_free(ioc->reply_free_dma_pool, ioc->reply_free,
		    ioc->reply_free_dma);
		if (ioc->reply_free_dma_pool)
			pci_pool_destroy(ioc->reply_free_dma_pool);
		dexitprintk(ioc, pr_info(MPT3SAS_FMT
			"reply_free_pool(0x%p): free\n",
			ioc->name, ioc->reply_free));
		ioc->reply_free = NULL;
	}

	if (ioc->reply_post) {
		do {
			rps = &ioc->reply_post[i];
			if (rps->reply_post_free) {
				pci_pool_free(
				    ioc->reply_post_free_dma_pool,
				    rps->reply_post_free,
				    rps->reply_post_free_dma);
				dexitprintk(ioc, pr_info(MPT3SAS_FMT
				    "reply_post_free_pool(0x%p): free\n",
				    ioc->name, rps->reply_post_free));
				rps->reply_post_free = NULL;
			}
		} while (ioc->rdpq_array_enable &&
			   (++i < ioc->reply_queue_count));

		if (ioc->reply_post_free_dma_pool)
			pci_pool_destroy(ioc->reply_post_free_dma_pool);
		kfree(ioc->reply_post);
	}

	if (ioc->config_page) {
		dexitprintk(ioc, pr_info(MPT3SAS_FMT
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
		free_pages((ulong)ioc->chain_lookup, ioc->chain_pages);
		ioc->chain_lookup = NULL;
	}
}

/**
 * _base_allocate_memory_pools - allocate start of day memory pools
 * @ioc: per adapter object
 *
 * Returns 0 success, anything else error
 */
static int
_base_allocate_memory_pools(struct MPT3SAS_ADAPTER *ioc)
{
	struct mpt3sas_facts *facts;
	u16 max_sge_elements;
	u16 chains_needed_per_io;
	u32 sz, total_sz, reply_post_free_sz;
	u32 retry_sz;
	u16 max_request_credit;
	unsigned short sg_tablesize;
	u16 sge_size;
	int i;

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));


	retry_sz = 0;
	facts = &ioc->facts;

	/* command line tunables for max sgl entries */
	if (max_sgl_entries != -1)
		sg_tablesize = max_sgl_entries;
	else {
		if (ioc->hba_mpi_version_belonged == MPI2_VERSION)
			sg_tablesize = MPT2SAS_SG_DEPTH;
		else
			sg_tablesize = MPT3SAS_SG_DEPTH;
	}

	if (sg_tablesize < MPT_MIN_PHYS_SEGMENTS)
		sg_tablesize = MPT_MIN_PHYS_SEGMENTS;
	else if (sg_tablesize > MPT_MAX_PHYS_SEGMENTS) {
		sg_tablesize = min_t(unsigned short, sg_tablesize,
				      SG_MAX_SEGMENTS);
		pr_warn(MPT3SAS_FMT
		 "sg_tablesize(%u) is bigger than kernel"
		 " defined SG_CHUNK_SIZE(%u)\n", ioc->name,
		 sg_tablesize, MPT_MAX_PHYS_SEGMENTS);
	}
	ioc->shost->sg_tablesize = sg_tablesize;

	ioc->internal_depth = min_t(int, (facts->HighPriorityCredit + (5)),
		(facts->RequestCredit / 4));
	if (ioc->internal_depth < INTERNAL_CMDS_COUNT) {
		if (facts->RequestCredit <= (INTERNAL_CMDS_COUNT +
				INTERNAL_SCSIIO_CMDS_COUNT)) {
			pr_err(MPT3SAS_FMT "IOC doesn't have enough Request \
			    Credits, it has just %d number of credits\n",
			    ioc->name, facts->RequestCredit);
			return -ENOMEM;
		}
		ioc->internal_depth = 10;
	}

	ioc->hi_priority_depth = ioc->internal_depth - (5);
	/* command line tunables  for max controller queue depth */
	if (max_queue_depth != -1 && max_queue_depth != 0) {
		max_request_credit = min_t(u16, max_queue_depth +
			ioc->internal_depth, facts->RequestCredit);
		if (max_request_credit > MAX_HBA_QUEUE_DEPTH)
			max_request_credit =  MAX_HBA_QUEUE_DEPTH;
	} else
		max_request_credit = min_t(u16, facts->RequestCredit,
		    MAX_HBA_QUEUE_DEPTH);

	/* Firmware maintains additional facts->HighPriorityCredit number of
	 * credits for HiPriprity Request messages, so hba queue depth will be
	 * sum of max_request_credit and high priority queue depth.
	 */
	ioc->hba_queue_depth = max_request_credit + ioc->hi_priority_depth;

	/* request frame size */
	ioc->request_sz = facts->IOCRequestFrameSize * 4;

	/* reply frame size */
	ioc->reply_sz = facts->ReplyFrameSize * 4;

	/* chain segment size */
	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		if (facts->IOCMaxChainSegmentSize)
			ioc->chain_segment_sz =
					facts->IOCMaxChainSegmentSize *
					MAX_CHAIN_ELEMT_SZ;
		else
		/* set to 128 bytes size if IOCMaxChainSegmentSize is zero */
			ioc->chain_segment_sz = DEFAULT_NUM_FWCHAIN_ELEMTS *
						    MAX_CHAIN_ELEMT_SZ;
	} else
		ioc->chain_segment_sz = ioc->request_sz;

	/* calculate the max scatter element size */
	sge_size = max_t(u16, ioc->sge_size, ioc->sge_size_ieee);

 retry_allocation:
	total_sz = 0;
	/* calculate number of sg elements left over in the 1st frame */
	max_sge_elements = ioc->request_sz - ((sizeof(Mpi2SCSIIORequest_t) -
	    sizeof(Mpi2SGEIOUnion_t)) + sge_size);
	ioc->max_sges_in_main_message = max_sge_elements/sge_size;

	/* now do the same for a chain buffer */
	max_sge_elements = ioc->chain_segment_sz - sge_size;
	ioc->max_sges_in_chain_message = max_sge_elements/sge_size;

	/*
	 *  MPT3SAS_SG_DEPTH = CONFIG_FUSION_MAX_SGE
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

	/* reply free queue sizing - taking into account for 64 FW events */
	ioc->reply_free_queue_depth = ioc->hba_queue_depth + 64;

	/* calculate reply descriptor post queue depth */
	ioc->reply_post_queue_depth = ioc->hba_queue_depth +
				ioc->reply_free_queue_depth +  1 ;
	/* align the reply post queue on the next 16 count boundary */
	if (ioc->reply_post_queue_depth % 16)
		ioc->reply_post_queue_depth += 16 -
		(ioc->reply_post_queue_depth % 16);

	if (ioc->reply_post_queue_depth >
	    facts->MaxReplyDescriptorPostQueueDepth) {
		ioc->reply_post_queue_depth =
				facts->MaxReplyDescriptorPostQueueDepth -
		    (facts->MaxReplyDescriptorPostQueueDepth % 16);
		ioc->hba_queue_depth =
				((ioc->reply_post_queue_depth - 64) / 2) - 1;
		ioc->reply_free_queue_depth = ioc->hba_queue_depth + 64;
	}

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "scatter gather: " \
	    "sge_in_main_msg(%d), sge_per_chain(%d), sge_per_io(%d), "
	    "chains_per_io(%d)\n", ioc->name, ioc->max_sges_in_main_message,
	    ioc->max_sges_in_chain_message, ioc->shost->sg_tablesize,
	    ioc->chains_needed_per_io));

	/* reply post queue, 16 byte align */
	reply_post_free_sz = ioc->reply_post_queue_depth *
	    sizeof(Mpi2DefaultReplyDescriptor_t);

	sz = reply_post_free_sz;
	if (_base_is_controller_msix_enabled(ioc) && !ioc->rdpq_array_enable)
		sz *= ioc->reply_queue_count;

	ioc->reply_post = kcalloc((ioc->rdpq_array_enable) ?
	    (ioc->reply_queue_count):1,
	    sizeof(struct reply_post_struct), GFP_KERNEL);

	if (!ioc->reply_post) {
		pr_err(MPT3SAS_FMT "reply_post_free pool: kcalloc failed\n",
			ioc->name);
		goto out;
	}
	ioc->reply_post_free_dma_pool = pci_pool_create("reply_post_free pool",
	    ioc->pdev, sz, 16, 0);
	if (!ioc->reply_post_free_dma_pool) {
		pr_err(MPT3SAS_FMT
		 "reply_post_free pool: pci_pool_create failed\n",
		 ioc->name);
		goto out;
	}
	i = 0;
	do {
		ioc->reply_post[i].reply_post_free =
		    pci_pool_alloc(ioc->reply_post_free_dma_pool,
		    GFP_KERNEL,
		    &ioc->reply_post[i].reply_post_free_dma);
		if (!ioc->reply_post[i].reply_post_free) {
			pr_err(MPT3SAS_FMT
			"reply_post_free pool: pci_pool_alloc failed\n",
			ioc->name);
			goto out;
		}
		memset(ioc->reply_post[i].reply_post_free, 0, sz);
		dinitprintk(ioc, pr_info(MPT3SAS_FMT
		    "reply post free pool (0x%p): depth(%d),"
		    "element_size(%d), pool_size(%d kB)\n", ioc->name,
		    ioc->reply_post[i].reply_post_free,
		    ioc->reply_post_queue_depth, 8, sz/1024));
		dinitprintk(ioc, pr_info(MPT3SAS_FMT
		    "reply_post_free_dma = (0x%llx)\n", ioc->name,
		    (unsigned long long)
		    ioc->reply_post[i].reply_post_free_dma));
		total_sz += sz;
	} while (ioc->rdpq_array_enable && (++i < ioc->reply_queue_count));

	if (ioc->dma_mask == 64) {
		if (_base_change_consistent_dma_mask(ioc, ioc->pdev) != 0) {
			pr_warn(MPT3SAS_FMT
			    "no suitable consistent DMA mask for %s\n",
			    ioc->name, pci_name(ioc->pdev));
			goto out;
		}
	}

	ioc->scsiio_depth = ioc->hba_queue_depth -
	    ioc->hi_priority_depth - ioc->internal_depth;

	/* set the scsi host can_queue depth
	 * with some internal commands that could be outstanding
	 */
	ioc->shost->can_queue = ioc->scsiio_depth - INTERNAL_SCSIIO_CMDS_COUNT;
	dinitprintk(ioc, pr_info(MPT3SAS_FMT
		"scsi host: can_queue depth (%d)\n",
		ioc->name, ioc->shost->can_queue));


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
		pr_err(MPT3SAS_FMT "request pool: pci_alloc_consistent " \
		    "failed: hba_depth(%d), chains_per_io(%d), frame_sz(%d), "
		    "total(%d kB)\n", ioc->name, ioc->hba_queue_depth,
		    ioc->chains_needed_per_io, ioc->request_sz, sz/1024);
		if (ioc->scsiio_depth < MPT3SAS_SAS_QUEUE_DEPTH)
			goto out;
		retry_sz = 64;
		ioc->hba_queue_depth -= retry_sz;
		_base_release_memory_pools(ioc);
		goto retry_allocation;
	}

	if (retry_sz)
		pr_err(MPT3SAS_FMT "request pool: pci_alloc_consistent " \
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

	dinitprintk(ioc, pr_info(MPT3SAS_FMT
		"request pool(0x%p): depth(%d), frame_size(%d), pool_size(%d kB)\n",
		ioc->name, ioc->request, ioc->hba_queue_depth, ioc->request_sz,
	    (ioc->hba_queue_depth * ioc->request_sz)/1024));

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "request pool: dma(0x%llx)\n",
	    ioc->name, (unsigned long long) ioc->request_dma));
	total_sz += sz;

	sz = ioc->scsiio_depth * sizeof(struct scsiio_tracker);
	ioc->scsi_lookup_pages = get_order(sz);
	ioc->scsi_lookup = (struct scsiio_tracker *)__get_free_pages(
	    GFP_KERNEL, ioc->scsi_lookup_pages);
	if (!ioc->scsi_lookup) {
		pr_err(MPT3SAS_FMT "scsi_lookup: get_free_pages failed, sz(%d)\n",
			ioc->name, (int)sz);
		goto out;
	}

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "scsiio(0x%p): depth(%d)\n",
		ioc->name, ioc->request, ioc->scsiio_depth));

	ioc->chain_depth = min_t(u32, ioc->chain_depth, MAX_CHAIN_DEPTH);
	sz = ioc->chain_depth * sizeof(struct chain_tracker);
	ioc->chain_pages = get_order(sz);
	ioc->chain_lookup = (struct chain_tracker *)__get_free_pages(
	    GFP_KERNEL, ioc->chain_pages);
	if (!ioc->chain_lookup) {
		pr_err(MPT3SAS_FMT "chain_lookup: __get_free_pages failed\n",
			ioc->name);
		goto out;
	}
	ioc->chain_dma_pool = pci_pool_create("chain pool", ioc->pdev,
	    ioc->chain_segment_sz, 16, 0);
	if (!ioc->chain_dma_pool) {
		pr_err(MPT3SAS_FMT "chain_dma_pool: pci_pool_create failed\n",
			ioc->name);
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
		total_sz += ioc->chain_segment_sz;
	}
 chain_done:
	dinitprintk(ioc, pr_info(MPT3SAS_FMT
		"chain pool depth(%d), frame_size(%d), pool_size(%d kB)\n",
		ioc->name, ioc->chain_depth, ioc->chain_segment_sz,
		((ioc->chain_depth *  ioc->chain_segment_sz))/1024));

	/* initialize hi-priority queue smid's */
	ioc->hpr_lookup = kcalloc(ioc->hi_priority_depth,
	    sizeof(struct request_tracker), GFP_KERNEL);
	if (!ioc->hpr_lookup) {
		pr_err(MPT3SAS_FMT "hpr_lookup: kcalloc failed\n",
		    ioc->name);
		goto out;
	}
	ioc->hi_priority_smid = ioc->scsiio_depth + 1;
	dinitprintk(ioc, pr_info(MPT3SAS_FMT
		"hi_priority(0x%p): depth(%d), start smid(%d)\n",
		ioc->name, ioc->hi_priority,
	    ioc->hi_priority_depth, ioc->hi_priority_smid));

	/* initialize internal queue smid's */
	ioc->internal_lookup = kcalloc(ioc->internal_depth,
	    sizeof(struct request_tracker), GFP_KERNEL);
	if (!ioc->internal_lookup) {
		pr_err(MPT3SAS_FMT "internal_lookup: kcalloc failed\n",
		    ioc->name);
		goto out;
	}
	ioc->internal_smid = ioc->hi_priority_smid + ioc->hi_priority_depth;
	dinitprintk(ioc, pr_info(MPT3SAS_FMT
		"internal(0x%p): depth(%d), start smid(%d)\n",
		ioc->name, ioc->internal,
	    ioc->internal_depth, ioc->internal_smid));

	/* sense buffers, 4 byte align */
	sz = ioc->scsiio_depth * SCSI_SENSE_BUFFERSIZE;
	ioc->sense_dma_pool = pci_pool_create("sense pool", ioc->pdev, sz, 4,
	    0);
	if (!ioc->sense_dma_pool) {
		pr_err(MPT3SAS_FMT "sense pool: pci_pool_create failed\n",
		    ioc->name);
		goto out;
	}
	ioc->sense = pci_pool_alloc(ioc->sense_dma_pool , GFP_KERNEL,
	    &ioc->sense_dma);
	if (!ioc->sense) {
		pr_err(MPT3SAS_FMT "sense pool: pci_pool_alloc failed\n",
		    ioc->name);
		goto out;
	}
	dinitprintk(ioc, pr_info(MPT3SAS_FMT
	    "sense pool(0x%p): depth(%d), element_size(%d), pool_size"
	    "(%d kB)\n", ioc->name, ioc->sense, ioc->scsiio_depth,
	    SCSI_SENSE_BUFFERSIZE, sz/1024));
	dinitprintk(ioc, pr_info(MPT3SAS_FMT "sense_dma(0x%llx)\n",
	    ioc->name, (unsigned long long)ioc->sense_dma));
	total_sz += sz;

	/* reply pool, 4 byte align */
	sz = ioc->reply_free_queue_depth * ioc->reply_sz;
	ioc->reply_dma_pool = pci_pool_create("reply pool", ioc->pdev, sz, 4,
	    0);
	if (!ioc->reply_dma_pool) {
		pr_err(MPT3SAS_FMT "reply pool: pci_pool_create failed\n",
		    ioc->name);
		goto out;
	}
	ioc->reply = pci_pool_alloc(ioc->reply_dma_pool , GFP_KERNEL,
	    &ioc->reply_dma);
	if (!ioc->reply) {
		pr_err(MPT3SAS_FMT "reply pool: pci_pool_alloc failed\n",
		    ioc->name);
		goto out;
	}
	ioc->reply_dma_min_address = (u32)(ioc->reply_dma);
	ioc->reply_dma_max_address = (u32)(ioc->reply_dma) + sz;
	dinitprintk(ioc, pr_info(MPT3SAS_FMT
		"reply pool(0x%p): depth(%d), frame_size(%d), pool_size(%d kB)\n",
		ioc->name, ioc->reply,
	    ioc->reply_free_queue_depth, ioc->reply_sz, sz/1024));
	dinitprintk(ioc, pr_info(MPT3SAS_FMT "reply_dma(0x%llx)\n",
	    ioc->name, (unsigned long long)ioc->reply_dma));
	total_sz += sz;

	/* reply free queue, 16 byte align */
	sz = ioc->reply_free_queue_depth * 4;
	ioc->reply_free_dma_pool = pci_pool_create("reply_free pool",
	    ioc->pdev, sz, 16, 0);
	if (!ioc->reply_free_dma_pool) {
		pr_err(MPT3SAS_FMT "reply_free pool: pci_pool_create failed\n",
			ioc->name);
		goto out;
	}
	ioc->reply_free = pci_pool_alloc(ioc->reply_free_dma_pool , GFP_KERNEL,
	    &ioc->reply_free_dma);
	if (!ioc->reply_free) {
		pr_err(MPT3SAS_FMT "reply_free pool: pci_pool_alloc failed\n",
			ioc->name);
		goto out;
	}
	memset(ioc->reply_free, 0, sz);
	dinitprintk(ioc, pr_info(MPT3SAS_FMT "reply_free pool(0x%p): " \
	    "depth(%d), element_size(%d), pool_size(%d kB)\n", ioc->name,
	    ioc->reply_free, ioc->reply_free_queue_depth, 4, sz/1024));
	dinitprintk(ioc, pr_info(MPT3SAS_FMT
		"reply_free_dma (0x%llx)\n",
		ioc->name, (unsigned long long)ioc->reply_free_dma));
	total_sz += sz;

	ioc->config_page_sz = 512;
	ioc->config_page = pci_alloc_consistent(ioc->pdev,
	    ioc->config_page_sz, &ioc->config_page_dma);
	if (!ioc->config_page) {
		pr_err(MPT3SAS_FMT
			"config page: pci_pool_alloc failed\n",
			ioc->name);
		goto out;
	}
	dinitprintk(ioc, pr_info(MPT3SAS_FMT
		"config page(0x%p): size(%d)\n",
		ioc->name, ioc->config_page, ioc->config_page_sz));
	dinitprintk(ioc, pr_info(MPT3SAS_FMT "config_page_dma(0x%llx)\n",
		ioc->name, (unsigned long long)ioc->config_page_dma));
	total_sz += ioc->config_page_sz;

	pr_info(MPT3SAS_FMT "Allocated physical memory: size(%d kB)\n",
	    ioc->name, total_sz/1024);
	pr_info(MPT3SAS_FMT
		"Current Controller Queue Depth(%d),Max Controller Queue Depth(%d)\n",
	    ioc->name, ioc->shost->can_queue, facts->RequestCredit);
	pr_info(MPT3SAS_FMT "Scatter Gather Elements per IO(%d)\n",
	    ioc->name, ioc->shost->sg_tablesize);
	return 0;

 out:
	return -ENOMEM;
}

/**
 * mpt3sas_base_get_iocstate - Get the current state of a MPT adapter.
 * @ioc: Pointer to MPT_ADAPTER structure
 * @cooked: Request raw or cooked IOC state
 *
 * Returns all IOC Doorbell register bits if cooked==0, else just the
 * Doorbell bits in MPI_IOC_STATE_MASK.
 */
u32
mpt3sas_base_get_iocstate(struct MPT3SAS_ADAPTER *ioc, int cooked)
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
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_wait_on_iocstate(struct MPT3SAS_ADAPTER *ioc, u32 ioc_state, int timeout)
{
	u32 count, cntdn;
	u32 current_state;

	count = 0;
	cntdn = 1000 * timeout;
	do {
		current_state = mpt3sas_base_get_iocstate(ioc, 1);
		if (current_state == ioc_state)
			return 0;
		if (count && current_state == MPI2_IOC_STATE_FAULT)
			break;

		usleep_range(1000, 1500);
		count++;
	} while (--cntdn);

	return current_state;
}

/**
 * _base_wait_for_doorbell_int - waiting for controller interrupt(generated by
 * a write to the doorbell)
 * @ioc: per adapter object
 * @timeout: timeout in second
 *
 * Returns 0 for success, non-zero for failure.
 *
 * Notes: MPI2_HIS_IOC2SYS_DB_STATUS - set to one when IOC writes to doorbell.
 */
static int
_base_diag_reset(struct MPT3SAS_ADAPTER *ioc);

static int
_base_wait_for_doorbell_int(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 cntdn, count;
	u32 int_status;

	count = 0;
	cntdn = 1000 * timeout;
	do {
		int_status = readl(&ioc->chip->HostInterruptStatus);
		if (int_status & MPI2_HIS_IOC2SYS_DB_STATUS) {
			dhsprintk(ioc, pr_info(MPT3SAS_FMT
				"%s: successful count(%d), timeout(%d)\n",
				ioc->name, __func__, count, timeout));
			return 0;
		}

		usleep_range(1000, 1500);
		count++;
	} while (--cntdn);

	pr_err(MPT3SAS_FMT
		"%s: failed due to timeout count(%d), int_status(%x)!\n",
		ioc->name, __func__, count, int_status);
	return -EFAULT;
}

static int
_base_spin_on_doorbell_int(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 cntdn, count;
	u32 int_status;

	count = 0;
	cntdn = 2000 * timeout;
	do {
		int_status = readl(&ioc->chip->HostInterruptStatus);
		if (int_status & MPI2_HIS_IOC2SYS_DB_STATUS) {
			dhsprintk(ioc, pr_info(MPT3SAS_FMT
				"%s: successful count(%d), timeout(%d)\n",
				ioc->name, __func__, count, timeout));
			return 0;
		}

		udelay(500);
		count++;
	} while (--cntdn);

	pr_err(MPT3SAS_FMT
		"%s: failed due to timeout count(%d), int_status(%x)!\n",
		ioc->name, __func__, count, int_status);
	return -EFAULT;

}

/**
 * _base_wait_for_doorbell_ack - waiting for controller to read the doorbell.
 * @ioc: per adapter object
 * @timeout: timeout in second
 *
 * Returns 0 for success, non-zero for failure.
 *
 * Notes: MPI2_HIS_SYS2IOC_DB_STATUS - set to one when host writes to
 * doorbell.
 */
static int
_base_wait_for_doorbell_ack(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 cntdn, count;
	u32 int_status;
	u32 doorbell;

	count = 0;
	cntdn = 1000 * timeout;
	do {
		int_status = readl(&ioc->chip->HostInterruptStatus);
		if (!(int_status & MPI2_HIS_SYS2IOC_DB_STATUS)) {
			dhsprintk(ioc, pr_info(MPT3SAS_FMT
				"%s: successful count(%d), timeout(%d)\n",
				ioc->name, __func__, count, timeout));
			return 0;
		} else if (int_status & MPI2_HIS_IOC2SYS_DB_STATUS) {
			doorbell = readl(&ioc->chip->Doorbell);
			if ((doorbell & MPI2_IOC_STATE_MASK) ==
			    MPI2_IOC_STATE_FAULT) {
				mpt3sas_base_fault_info(ioc , doorbell);
				return -EFAULT;
			}
		} else if (int_status == 0xFFFFFFFF)
			goto out;

		usleep_range(1000, 1500);
		count++;
	} while (--cntdn);

 out:
	pr_err(MPT3SAS_FMT
	 "%s: failed due to timeout count(%d), int_status(%x)!\n",
	 ioc->name, __func__, count, int_status);
	return -EFAULT;
}

/**
 * _base_wait_for_doorbell_not_used - waiting for doorbell to not be in use
 * @ioc: per adapter object
 * @timeout: timeout in second
 *
 * Returns 0 for success, non-zero for failure.
 *
 */
static int
_base_wait_for_doorbell_not_used(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 cntdn, count;
	u32 doorbell_reg;

	count = 0;
	cntdn = 1000 * timeout;
	do {
		doorbell_reg = readl(&ioc->chip->Doorbell);
		if (!(doorbell_reg & MPI2_DOORBELL_USED)) {
			dhsprintk(ioc, pr_info(MPT3SAS_FMT
				"%s: successful count(%d), timeout(%d)\n",
				ioc->name, __func__, count, timeout));
			return 0;
		}

		usleep_range(1000, 1500);
		count++;
	} while (--cntdn);

	pr_err(MPT3SAS_FMT
		"%s: failed due to timeout count(%d), doorbell_reg(%x)!\n",
		ioc->name, __func__, count, doorbell_reg);
	return -EFAULT;
}

/**
 * _base_send_ioc_reset - send doorbell reset
 * @ioc: per adapter object
 * @reset_type: currently only supports: MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET
 * @timeout: timeout in second
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_send_ioc_reset(struct MPT3SAS_ADAPTER *ioc, u8 reset_type, int timeout)
{
	u32 ioc_state;
	int r = 0;

	if (reset_type != MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET) {
		pr_err(MPT3SAS_FMT "%s: unknown reset_type\n",
		    ioc->name, __func__);
		return -EFAULT;
	}

	if (!(ioc->facts.IOCCapabilities &
	   MPI2_IOCFACTS_CAPABILITY_EVENT_REPLAY))
		return -EFAULT;

	pr_info(MPT3SAS_FMT "sending message unit reset !!\n", ioc->name);

	writel(reset_type << MPI2_DOORBELL_FUNCTION_SHIFT,
	    &ioc->chip->Doorbell);
	if ((_base_wait_for_doorbell_ack(ioc, 15))) {
		r = -EFAULT;
		goto out;
	}
	ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_READY, timeout);
	if (ioc_state) {
		pr_err(MPT3SAS_FMT
			"%s: failed going to ready state (ioc_state=0x%x)\n",
			ioc->name, __func__, ioc_state);
		r = -EFAULT;
		goto out;
	}
 out:
	pr_info(MPT3SAS_FMT "message unit reset: %s\n",
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
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_handshake_req_reply_wait(struct MPT3SAS_ADAPTER *ioc, int request_bytes,
	u32 *request, int reply_bytes, u16 *reply, int timeout)
{
	MPI2DefaultReply_t *default_reply = (MPI2DefaultReply_t *)reply;
	int i;
	u8 failed;
	__le32 *mfp;

	/* make sure doorbell is not in use */
	if ((readl(&ioc->chip->Doorbell) & MPI2_DOORBELL_USED)) {
		pr_err(MPT3SAS_FMT
			"doorbell is in use (line=%d)\n",
			ioc->name, __LINE__);
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

	if ((_base_spin_on_doorbell_int(ioc, 5))) {
		pr_err(MPT3SAS_FMT
			"doorbell handshake int failed (line=%d)\n",
			ioc->name, __LINE__);
		return -EFAULT;
	}
	writel(0, &ioc->chip->HostInterruptStatus);

	if ((_base_wait_for_doorbell_ack(ioc, 5))) {
		pr_err(MPT3SAS_FMT
			"doorbell handshake ack failed (line=%d)\n",
			ioc->name, __LINE__);
		return -EFAULT;
	}

	/* send message 32-bits at a time */
	for (i = 0, failed = 0; i < request_bytes/4 && !failed; i++) {
		writel(cpu_to_le32(request[i]), &ioc->chip->Doorbell);
		if ((_base_wait_for_doorbell_ack(ioc, 5)))
			failed = 1;
	}

	if (failed) {
		pr_err(MPT3SAS_FMT
			"doorbell handshake sending request failed (line=%d)\n",
			ioc->name, __LINE__);
		return -EFAULT;
	}

	/* now wait for the reply */
	if ((_base_wait_for_doorbell_int(ioc, timeout))) {
		pr_err(MPT3SAS_FMT
			"doorbell handshake int failed (line=%d)\n",
			ioc->name, __LINE__);
		return -EFAULT;
	}

	/* read the first two 16-bits, it gives the total length of the reply */
	reply[0] = le16_to_cpu(readl(&ioc->chip->Doorbell)
	    & MPI2_DOORBELL_DATA_MASK);
	writel(0, &ioc->chip->HostInterruptStatus);
	if ((_base_wait_for_doorbell_int(ioc, 5))) {
		pr_err(MPT3SAS_FMT
			"doorbell handshake int failed (line=%d)\n",
			ioc->name, __LINE__);
		return -EFAULT;
	}
	reply[1] = le16_to_cpu(readl(&ioc->chip->Doorbell)
	    & MPI2_DOORBELL_DATA_MASK);
	writel(0, &ioc->chip->HostInterruptStatus);

	for (i = 2; i < default_reply->MsgLength * 2; i++)  {
		if ((_base_wait_for_doorbell_int(ioc, 5))) {
			pr_err(MPT3SAS_FMT
				"doorbell handshake int failed (line=%d)\n",
				ioc->name, __LINE__);
			return -EFAULT;
		}
		if (i >=  reply_bytes/2) /* overflow case */
			readl(&ioc->chip->Doorbell);
		else
			reply[i] = le16_to_cpu(readl(&ioc->chip->Doorbell)
			    & MPI2_DOORBELL_DATA_MASK);
		writel(0, &ioc->chip->HostInterruptStatus);
	}

	_base_wait_for_doorbell_int(ioc, 5);
	if (_base_wait_for_doorbell_not_used(ioc, 5) != 0) {
		dhsprintk(ioc, pr_info(MPT3SAS_FMT
			"doorbell is in use (line=%d)\n", ioc->name, __LINE__));
	}
	writel(0, &ioc->chip->HostInterruptStatus);

	if (ioc->logging_level & MPT_DEBUG_INIT) {
		mfp = (__le32 *)reply;
		pr_info("\toffset:data\n");
		for (i = 0; i < reply_bytes/4; i++)
			pr_info("\t[0x%02x]:%08x\n", i*4,
			    le32_to_cpu(mfp[i]));
	}
	return 0;
}

/**
 * mpt3sas_base_sas_iounit_control - send sas iounit control to FW
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
mpt3sas_base_sas_iounit_control(struct MPT3SAS_ADAPTER *ioc,
	Mpi2SasIoUnitControlReply_t *mpi_reply,
	Mpi2SasIoUnitControlRequest_t *mpi_request)
{
	u16 smid;
	u32 ioc_state;
	bool issue_reset = false;
	int rc;
	void *request;
	u16 wait_state_count;

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	mutex_lock(&ioc->base_cmds.mutex);

	if (ioc->base_cmds.status != MPT3_CMD_NOT_USED) {
		pr_err(MPT3SAS_FMT "%s: base_cmd in use\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	wait_state_count = 0;
	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	while (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		if (wait_state_count++ == 10) {
			pr_err(MPT3SAS_FMT
			    "%s: failed due to ioc not operational\n",
			    ioc->name, __func__);
			rc = -EFAULT;
			goto out;
		}
		ssleep(1);
		ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
		pr_info(MPT3SAS_FMT
			"%s: waiting for operational state(count=%d)\n",
			ioc->name, __func__, wait_state_count);
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		pr_err(MPT3SAS_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->base_cmds.status = MPT3_CMD_PENDING;
	request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memcpy(request, mpi_request, sizeof(Mpi2SasIoUnitControlRequest_t));
	if (mpi_request->Operation == MPI2_SAS_OP_PHY_HARD_RESET ||
	    mpi_request->Operation == MPI2_SAS_OP_PHY_LINK_RESET)
		ioc->ioc_link_reset_in_progress = 1;
	init_completion(&ioc->base_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->base_cmds.done,
	    msecs_to_jiffies(10000));
	if ((mpi_request->Operation == MPI2_SAS_OP_PHY_HARD_RESET ||
	    mpi_request->Operation == MPI2_SAS_OP_PHY_LINK_RESET) &&
	    ioc->ioc_link_reset_in_progress)
		ioc->ioc_link_reset_in_progress = 0;
	if (!(ioc->base_cmds.status & MPT3_CMD_COMPLETE)) {
		pr_err(MPT3SAS_FMT "%s: timeout\n",
		    ioc->name, __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2SasIoUnitControlRequest_t)/4);
		if (!(ioc->base_cmds.status & MPT3_CMD_RESET))
			issue_reset = true;
		goto issue_host_reset;
	}
	if (ioc->base_cmds.status & MPT3_CMD_REPLY_VALID)
		memcpy(mpi_reply, ioc->base_cmds.reply,
		    sizeof(Mpi2SasIoUnitControlReply_t));
	else
		memset(mpi_reply, 0, sizeof(Mpi2SasIoUnitControlReply_t));
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	goto out;

 issue_host_reset:
	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	rc = -EFAULT;
 out:
	mutex_unlock(&ioc->base_cmds.mutex);
	return rc;
}

/**
 * mpt3sas_base_scsi_enclosure_processor - sending request to sep device
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
mpt3sas_base_scsi_enclosure_processor(struct MPT3SAS_ADAPTER *ioc,
	Mpi2SepReply_t *mpi_reply, Mpi2SepRequest_t *mpi_request)
{
	u16 smid;
	u32 ioc_state;
	bool issue_reset = false;
	int rc;
	void *request;
	u16 wait_state_count;

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	mutex_lock(&ioc->base_cmds.mutex);

	if (ioc->base_cmds.status != MPT3_CMD_NOT_USED) {
		pr_err(MPT3SAS_FMT "%s: base_cmd in use\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	wait_state_count = 0;
	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	while (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		if (wait_state_count++ == 10) {
			pr_err(MPT3SAS_FMT
			    "%s: failed due to ioc not operational\n",
			    ioc->name, __func__);
			rc = -EFAULT;
			goto out;
		}
		ssleep(1);
		ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
		pr_info(MPT3SAS_FMT
			"%s: waiting for operational state(count=%d)\n",
			ioc->name,
		    __func__, wait_state_count);
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		pr_err(MPT3SAS_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->base_cmds.status = MPT3_CMD_PENDING;
	request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memcpy(request, mpi_request, sizeof(Mpi2SepReply_t));
	init_completion(&ioc->base_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->base_cmds.done,
	    msecs_to_jiffies(10000));
	if (!(ioc->base_cmds.status & MPT3_CMD_COMPLETE)) {
		pr_err(MPT3SAS_FMT "%s: timeout\n",
		    ioc->name, __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2SepRequest_t)/4);
		if (!(ioc->base_cmds.status & MPT3_CMD_RESET))
			issue_reset = false;
		goto issue_host_reset;
	}
	if (ioc->base_cmds.status & MPT3_CMD_REPLY_VALID)
		memcpy(mpi_reply, ioc->base_cmds.reply,
		    sizeof(Mpi2SepReply_t));
	else
		memset(mpi_reply, 0, sizeof(Mpi2SepReply_t));
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	goto out;

 issue_host_reset:
	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	rc = -EFAULT;
 out:
	mutex_unlock(&ioc->base_cmds.mutex);
	return rc;
}

/**
 * _base_get_port_facts - obtain port facts reply and save in ioc
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_get_port_facts(struct MPT3SAS_ADAPTER *ioc, int port)
{
	Mpi2PortFactsRequest_t mpi_request;
	Mpi2PortFactsReply_t mpi_reply;
	struct mpt3sas_port_facts *pfacts;
	int mpi_reply_sz, mpi_request_sz, r;

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	mpi_reply_sz = sizeof(Mpi2PortFactsReply_t);
	mpi_request_sz = sizeof(Mpi2PortFactsRequest_t);
	memset(&mpi_request, 0, mpi_request_sz);
	mpi_request.Function = MPI2_FUNCTION_PORT_FACTS;
	mpi_request.PortNumber = port;
	r = _base_handshake_req_reply_wait(ioc, mpi_request_sz,
	    (u32 *)&mpi_request, mpi_reply_sz, (u16 *)&mpi_reply, 5);

	if (r != 0) {
		pr_err(MPT3SAS_FMT "%s: handshake failed (r=%d)\n",
		    ioc->name, __func__, r);
		return r;
	}

	pfacts = &ioc->pfacts[port];
	memset(pfacts, 0, sizeof(struct mpt3sas_port_facts));
	pfacts->PortNumber = mpi_reply.PortNumber;
	pfacts->VP_ID = mpi_reply.VP_ID;
	pfacts->VF_ID = mpi_reply.VF_ID;
	pfacts->MaxPostedCmdBuffers =
	    le16_to_cpu(mpi_reply.MaxPostedCmdBuffers);

	return 0;
}

/**
 * _base_wait_for_iocstate - Wait until the card is in READY or OPERATIONAL
 * @ioc: per adapter object
 * @timeout:
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_wait_for_iocstate(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 ioc_state;
	int rc;

	dinitprintk(ioc, printk(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	if (ioc->pci_error_recovery) {
		dfailprintk(ioc, printk(MPT3SAS_FMT
		    "%s: host in pci error recovery\n", ioc->name, __func__));
		return -EFAULT;
	}

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	dhsprintk(ioc, printk(MPT3SAS_FMT "%s: ioc_state(0x%08x)\n",
	    ioc->name, __func__, ioc_state));

	if (((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_READY) ||
	    (ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_OPERATIONAL)
		return 0;

	if (ioc_state & MPI2_DOORBELL_USED) {
		dhsprintk(ioc, printk(MPT3SAS_FMT
		    "unexpected doorbell active!\n", ioc->name));
		goto issue_diag_reset;
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt3sas_base_fault_info(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		goto issue_diag_reset;
	}

	ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_READY, timeout);
	if (ioc_state) {
		dfailprintk(ioc, printk(MPT3SAS_FMT
		    "%s: failed going to ready state (ioc_state=0x%x)\n",
		    ioc->name, __func__, ioc_state));
		return -EFAULT;
	}

 issue_diag_reset:
	rc = _base_diag_reset(ioc);
	return rc;
}

/**
 * _base_get_ioc_facts - obtain ioc facts reply and save in ioc
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_get_ioc_facts(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2IOCFactsRequest_t mpi_request;
	Mpi2IOCFactsReply_t mpi_reply;
	struct mpt3sas_facts *facts;
	int mpi_reply_sz, mpi_request_sz, r;

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	r = _base_wait_for_iocstate(ioc, 10);
	if (r) {
		dfailprintk(ioc, printk(MPT3SAS_FMT
		    "%s: failed getting to correct state\n",
		    ioc->name, __func__));
		return r;
	}
	mpi_reply_sz = sizeof(Mpi2IOCFactsReply_t);
	mpi_request_sz = sizeof(Mpi2IOCFactsRequest_t);
	memset(&mpi_request, 0, mpi_request_sz);
	mpi_request.Function = MPI2_FUNCTION_IOC_FACTS;
	r = _base_handshake_req_reply_wait(ioc, mpi_request_sz,
	    (u32 *)&mpi_request, mpi_reply_sz, (u16 *)&mpi_reply, 5);

	if (r != 0) {
		pr_err(MPT3SAS_FMT "%s: handshake failed (r=%d)\n",
		    ioc->name, __func__, r);
		return r;
	}

	facts = &ioc->facts;
	memset(facts, 0, sizeof(struct mpt3sas_facts));
	facts->MsgVersion = le16_to_cpu(mpi_reply.MsgVersion);
	facts->HeaderVersion = le16_to_cpu(mpi_reply.HeaderVersion);
	facts->VP_ID = mpi_reply.VP_ID;
	facts->VF_ID = mpi_reply.VF_ID;
	facts->IOCExceptions = le16_to_cpu(mpi_reply.IOCExceptions);
	facts->MaxChainDepth = mpi_reply.MaxChainDepth;
	facts->WhoInit = mpi_reply.WhoInit;
	facts->NumberOfPorts = mpi_reply.NumberOfPorts;
	facts->MaxMSIxVectors = mpi_reply.MaxMSIxVectors;
	facts->RequestCredit = le16_to_cpu(mpi_reply.RequestCredit);
	facts->MaxReplyDescriptorPostQueueDepth =
	    le16_to_cpu(mpi_reply.MaxReplyDescriptorPostQueueDepth);
	facts->ProductID = le16_to_cpu(mpi_reply.ProductID);
	facts->IOCCapabilities = le32_to_cpu(mpi_reply.IOCCapabilities);
	if ((facts->IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_INTEGRATED_RAID))
		ioc->ir_firmware = 1;
	if ((facts->IOCCapabilities &
	      MPI2_IOCFACTS_CAPABILITY_RDPQ_ARRAY_CAPABLE))
		ioc->rdpq_array_capable = 1;
	if (facts->IOCCapabilities & MPI26_IOCFACTS_CAPABILITY_ATOMIC_REQ)
		ioc->atomic_desc_capable = 1;
	facts->FWVersion.Word = le32_to_cpu(mpi_reply.FWVersion.Word);
	facts->IOCRequestFrameSize =
	    le16_to_cpu(mpi_reply.IOCRequestFrameSize);
	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		facts->IOCMaxChainSegmentSize =
			le16_to_cpu(mpi_reply.IOCMaxChainSegmentSize);
	}
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

	dinitprintk(ioc, pr_info(MPT3SAS_FMT
		"hba queue depth(%d), max chains per io(%d)\n",
		ioc->name, facts->RequestCredit,
	    facts->MaxChainDepth));
	dinitprintk(ioc, pr_info(MPT3SAS_FMT
		"request frame size(%d), reply frame size(%d)\n", ioc->name,
	    facts->IOCRequestFrameSize * 4, facts->ReplyFrameSize * 4));
	return 0;
}

/**
 * _base_send_ioc_init - send ioc_init to firmware
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_send_ioc_init(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2IOCInitRequest_t mpi_request;
	Mpi2IOCInitReply_t mpi_reply;
	int i, r = 0;
	ktime_t current_time;
	u16 ioc_status;
	u32 reply_post_free_array_sz = 0;
	Mpi2IOCInitRDPQArrayEntry *reply_post_free_array = NULL;
	dma_addr_t reply_post_free_array_dma;

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	memset(&mpi_request, 0, sizeof(Mpi2IOCInitRequest_t));
	mpi_request.Function = MPI2_FUNCTION_IOC_INIT;
	mpi_request.WhoInit = MPI2_WHOINIT_HOST_DRIVER;
	mpi_request.VF_ID = 0; /* TODO */
	mpi_request.VP_ID = 0;
	mpi_request.MsgVersion = cpu_to_le16(ioc->hba_mpi_version_belonged);
	mpi_request.HeaderVersion = cpu_to_le16(MPI2_HEADER_VERSION);

	if (_base_is_controller_msix_enabled(ioc))
		mpi_request.HostMSIxVectors = ioc->reply_queue_count;
	mpi_request.SystemRequestFrameSize = cpu_to_le16(ioc->request_sz/4);
	mpi_request.ReplyDescriptorPostQueueDepth =
	    cpu_to_le16(ioc->reply_post_queue_depth);
	mpi_request.ReplyFreeQueueDepth =
	    cpu_to_le16(ioc->reply_free_queue_depth);

	mpi_request.SenseBufferAddressHigh =
	    cpu_to_le32((u64)ioc->sense_dma >> 32);
	mpi_request.SystemReplyAddressHigh =
	    cpu_to_le32((u64)ioc->reply_dma >> 32);
	mpi_request.SystemRequestFrameBaseAddress =
	    cpu_to_le64((u64)ioc->request_dma);
	mpi_request.ReplyFreeQueueAddress =
	    cpu_to_le64((u64)ioc->reply_free_dma);

	if (ioc->rdpq_array_enable) {
		reply_post_free_array_sz = ioc->reply_queue_count *
		    sizeof(Mpi2IOCInitRDPQArrayEntry);
		reply_post_free_array = pci_alloc_consistent(ioc->pdev,
			reply_post_free_array_sz, &reply_post_free_array_dma);
		if (!reply_post_free_array) {
			pr_err(MPT3SAS_FMT
			"reply_post_free_array: pci_alloc_consistent failed\n",
			ioc->name);
			r = -ENOMEM;
			goto out;
		}
		memset(reply_post_free_array, 0, reply_post_free_array_sz);
		for (i = 0; i < ioc->reply_queue_count; i++)
			reply_post_free_array[i].RDPQBaseAddress =
			    cpu_to_le64(
				(u64)ioc->reply_post[i].reply_post_free_dma);
		mpi_request.MsgFlags = MPI2_IOCINIT_MSGFLAG_RDPQ_ARRAY_MODE;
		mpi_request.ReplyDescriptorPostQueueAddress =
		    cpu_to_le64((u64)reply_post_free_array_dma);
	} else {
		mpi_request.ReplyDescriptorPostQueueAddress =
		    cpu_to_le64((u64)ioc->reply_post[0].reply_post_free_dma);
	}

	/* This time stamp specifies number of milliseconds
	 * since epoch ~ midnight January 1, 1970.
	 */
	current_time = ktime_get_real();
	mpi_request.TimeStamp = cpu_to_le64(ktime_to_ms(current_time));

	if (ioc->logging_level & MPT_DEBUG_INIT) {
		__le32 *mfp;
		int i;

		mfp = (__le32 *)&mpi_request;
		pr_info("\toffset:data\n");
		for (i = 0; i < sizeof(Mpi2IOCInitRequest_t)/4; i++)
			pr_info("\t[0x%02x]:%08x\n", i*4,
			    le32_to_cpu(mfp[i]));
	}

	r = _base_handshake_req_reply_wait(ioc,
	    sizeof(Mpi2IOCInitRequest_t), (u32 *)&mpi_request,
	    sizeof(Mpi2IOCInitReply_t), (u16 *)&mpi_reply, 10);

	if (r != 0) {
		pr_err(MPT3SAS_FMT "%s: handshake failed (r=%d)\n",
		    ioc->name, __func__, r);
		goto out;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS ||
	    mpi_reply.IOCLogInfo) {
		pr_err(MPT3SAS_FMT "%s: failed\n", ioc->name, __func__);
		r = -EIO;
	}

out:
	if (reply_post_free_array)
		pci_free_consistent(ioc->pdev, reply_post_free_array_sz,
				    reply_post_free_array,
				    reply_post_free_array_dma);
	return r;
}

/**
 * mpt3sas_port_enable_done - command completion routine for port enable
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
u8
mpt3sas_port_enable_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;
	u16 ioc_status;

	if (ioc->port_enable_cmds.status == MPT3_CMD_NOT_USED)
		return 1;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (!mpi_reply)
		return 1;

	if (mpi_reply->Function != MPI2_FUNCTION_PORT_ENABLE)
		return 1;

	ioc->port_enable_cmds.status &= ~MPT3_CMD_PENDING;
	ioc->port_enable_cmds.status |= MPT3_CMD_COMPLETE;
	ioc->port_enable_cmds.status |= MPT3_CMD_REPLY_VALID;
	memcpy(ioc->port_enable_cmds.reply, mpi_reply, mpi_reply->MsgLength*4);
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		ioc->port_enable_failed = 1;

	if (ioc->is_driver_loading) {
		if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
			mpt3sas_port_enable_complete(ioc);
			return 1;
		} else {
			ioc->start_scan_failed = ioc_status;
			ioc->start_scan = 0;
			return 1;
		}
	}
	complete(&ioc->port_enable_cmds.done);
	return 1;
}

/**
 * _base_send_port_enable - send port_enable(discovery stuff) to firmware
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_send_port_enable(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2PortEnableRequest_t *mpi_request;
	Mpi2PortEnableReply_t *mpi_reply;
	int r = 0;
	u16 smid;
	u16 ioc_status;

	pr_info(MPT3SAS_FMT "sending port enable !!\n", ioc->name);

	if (ioc->port_enable_cmds.status & MPT3_CMD_PENDING) {
		pr_err(MPT3SAS_FMT "%s: internal command already in use\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->port_enable_cb_idx);
	if (!smid) {
		pr_err(MPT3SAS_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	ioc->port_enable_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->port_enable_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2PortEnableRequest_t));
	mpi_request->Function = MPI2_FUNCTION_PORT_ENABLE;

	init_completion(&ioc->port_enable_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->port_enable_cmds.done, 300*HZ);
	if (!(ioc->port_enable_cmds.status & MPT3_CMD_COMPLETE)) {
		pr_err(MPT3SAS_FMT "%s: timeout\n",
		    ioc->name, __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2PortEnableRequest_t)/4);
		if (ioc->port_enable_cmds.status & MPT3_CMD_RESET)
			r = -EFAULT;
		else
			r = -ETIME;
		goto out;
	}

	mpi_reply = ioc->port_enable_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		pr_err(MPT3SAS_FMT "%s: failed with (ioc_status=0x%08x)\n",
		    ioc->name, __func__, ioc_status);
		r = -EFAULT;
		goto out;
	}

 out:
	ioc->port_enable_cmds.status = MPT3_CMD_NOT_USED;
	pr_info(MPT3SAS_FMT "port enable: %s\n", ioc->name, ((r == 0) ?
	    "SUCCESS" : "FAILED"));
	return r;
}

/**
 * mpt3sas_port_enable - initiate firmware discovery (don't wait for reply)
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt3sas_port_enable(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2PortEnableRequest_t *mpi_request;
	u16 smid;

	pr_info(MPT3SAS_FMT "sending port enable !!\n", ioc->name);

	if (ioc->port_enable_cmds.status & MPT3_CMD_PENDING) {
		pr_err(MPT3SAS_FMT "%s: internal command already in use\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->port_enable_cb_idx);
	if (!smid) {
		pr_err(MPT3SAS_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	ioc->port_enable_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->port_enable_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2PortEnableRequest_t));
	mpi_request->Function = MPI2_FUNCTION_PORT_ENABLE;

	ioc->put_smid_default(ioc, smid);
	return 0;
}

/**
 * _base_determine_wait_on_discovery - desposition
 * @ioc: per adapter object
 *
 * Decide whether to wait on discovery to complete. Used to either
 * locate boot device, or report volumes ahead of physical devices.
 *
 * Returns 1 for wait, 0 for don't wait
 */
static int
_base_determine_wait_on_discovery(struct MPT3SAS_ADAPTER *ioc)
{
	/* We wait for discovery to complete if IR firmware is loaded.
	 * The sas topology events arrive before PD events, so we need time to
	 * turn on the bit in ioc->pd_handles to indicate PD
	 * Also, it maybe required to report Volumes ahead of physical
	 * devices when MPI2_IOCPAGE8_IRFLAGS_LOW_VOLUME_MAPPING is set.
	 */
	if (ioc->ir_firmware)
		return 1;

	/* if no Bios, then we don't need to wait */
	if (!ioc->bios_pg3.BiosVersion)
		return 0;

	/* Bios is present, then we drop down here.
	 *
	 * If there any entries in the Bios Page 2, then we wait
	 * for discovery to complete.
	 */

	/* Current Boot Device */
	if ((ioc->bios_pg2.CurrentBootDeviceForm &
	    MPI2_BIOSPAGE2_FORM_MASK) ==
	    MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED &&
	/* Request Boot Device */
	   (ioc->bios_pg2.ReqBootDeviceForm &
	    MPI2_BIOSPAGE2_FORM_MASK) ==
	    MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED &&
	/* Alternate Request Boot Device */
	   (ioc->bios_pg2.ReqAltBootDeviceForm &
	    MPI2_BIOSPAGE2_FORM_MASK) ==
	    MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED)
		return 0;

	return 1;
}

/**
 * _base_unmask_events - turn on notification for this event
 * @ioc: per adapter object
 * @event: firmware event
 *
 * The mask is stored in ioc->event_masks.
 */
static void
_base_unmask_events(struct MPT3SAS_ADAPTER *ioc, u16 event)
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
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_event_notification(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2EventNotificationRequest_t *mpi_request;
	u16 smid;
	int r = 0;
	int i;

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	if (ioc->base_cmds.status & MPT3_CMD_PENDING) {
		pr_err(MPT3SAS_FMT "%s: internal command already in use\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		pr_err(MPT3SAS_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}
	ioc->base_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2EventNotificationRequest_t));
	mpi_request->Function = MPI2_FUNCTION_EVENT_NOTIFICATION;
	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;
	for (i = 0; i < MPI2_EVENT_NOTIFY_EVENTMASK_WORDS; i++)
		mpi_request->EventMasks[i] =
		    cpu_to_le32(ioc->event_masks[i]);
	init_completion(&ioc->base_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->base_cmds.done, 30*HZ);
	if (!(ioc->base_cmds.status & MPT3_CMD_COMPLETE)) {
		pr_err(MPT3SAS_FMT "%s: timeout\n",
		    ioc->name, __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2EventNotificationRequest_t)/4);
		if (ioc->base_cmds.status & MPT3_CMD_RESET)
			r = -EFAULT;
		else
			r = -ETIME;
	} else
		dinitprintk(ioc, pr_info(MPT3SAS_FMT "%s: complete\n",
		    ioc->name, __func__));
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	return r;
}

/**
 * mpt3sas_base_validate_event_type - validating event types
 * @ioc: per adapter object
 * @event: firmware event
 *
 * This will turn on firmware event notification when application
 * ask for that event. We don't mask events that are already enabled.
 */
void
mpt3sas_base_validate_event_type(struct MPT3SAS_ADAPTER *ioc, u32 *event_type)
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
	_base_event_notification(ioc);
	mutex_unlock(&ioc->base_cmds.mutex);
}

/**
 * _base_diag_reset - the "big hammer" start of day reset
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_diag_reset(struct MPT3SAS_ADAPTER *ioc)
{
	u32 host_diagnostic;
	u32 ioc_state;
	u32 count;
	u32 hcb_size;

	pr_info(MPT3SAS_FMT "sending diag reset !!\n", ioc->name);

	drsprintk(ioc, pr_info(MPT3SAS_FMT "clear interrupts\n",
	    ioc->name));

	count = 0;
	do {
		/* Write magic sequence to WriteSequence register
		 * Loop until in diagnostic mode
		 */
		drsprintk(ioc, pr_info(MPT3SAS_FMT
			"write magic sequence\n", ioc->name));
		writel(MPI2_WRSEQ_FLUSH_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_1ST_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_2ND_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_3RD_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_4TH_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_5TH_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_6TH_KEY_VALUE, &ioc->chip->WriteSequence);

		/* wait 100 msec */
		msleep(100);

		if (count++ > 20)
			goto out;

		host_diagnostic = readl(&ioc->chip->HostDiagnostic);
		drsprintk(ioc, pr_info(MPT3SAS_FMT
			"wrote magic sequence: count(%d), host_diagnostic(0x%08x)\n",
		    ioc->name, count, host_diagnostic));

	} while ((host_diagnostic & MPI2_DIAG_DIAG_WRITE_ENABLE) == 0);

	hcb_size = readl(&ioc->chip->HCBSize);

	drsprintk(ioc, pr_info(MPT3SAS_FMT "diag reset: issued\n",
	    ioc->name));
	writel(host_diagnostic | MPI2_DIAG_RESET_ADAPTER,
	     &ioc->chip->HostDiagnostic);

	/*This delay allows the chip PCIe hardware time to finish reset tasks*/
	msleep(MPI2_HARD_RESET_PCIE_FIRST_READ_DELAY_MICRO_SEC/1000);

	/* Approximately 300 second max wait */
	for (count = 0; count < (300000000 /
		MPI2_HARD_RESET_PCIE_SECOND_READ_DELAY_MICRO_SEC); count++) {

		host_diagnostic = readl(&ioc->chip->HostDiagnostic);

		if (host_diagnostic == 0xFFFFFFFF)
			goto out;
		if (!(host_diagnostic & MPI2_DIAG_RESET_ADAPTER))
			break;

		msleep(MPI2_HARD_RESET_PCIE_SECOND_READ_DELAY_MICRO_SEC / 1000);
	}

	if (host_diagnostic & MPI2_DIAG_HCB_MODE) {

		drsprintk(ioc, pr_info(MPT3SAS_FMT
		"restart the adapter assuming the HCB Address points to good F/W\n",
		    ioc->name));
		host_diagnostic &= ~MPI2_DIAG_BOOT_DEVICE_SELECT_MASK;
		host_diagnostic |= MPI2_DIAG_BOOT_DEVICE_SELECT_HCDW;
		writel(host_diagnostic, &ioc->chip->HostDiagnostic);

		drsprintk(ioc, pr_info(MPT3SAS_FMT
		    "re-enable the HCDW\n", ioc->name));
		writel(hcb_size | MPI2_HCB_SIZE_HCB_ENABLE,
		    &ioc->chip->HCBSize);
	}

	drsprintk(ioc, pr_info(MPT3SAS_FMT "restart the adapter\n",
	    ioc->name));
	writel(host_diagnostic & ~MPI2_DIAG_HOLD_IOC_RESET,
	    &ioc->chip->HostDiagnostic);

	drsprintk(ioc, pr_info(MPT3SAS_FMT
		"disable writes to the diagnostic register\n", ioc->name));
	writel(MPI2_WRSEQ_FLUSH_KEY_VALUE, &ioc->chip->WriteSequence);

	drsprintk(ioc, pr_info(MPT3SAS_FMT
		"Wait for FW to go to the READY state\n", ioc->name));
	ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_READY, 20);
	if (ioc_state) {
		pr_err(MPT3SAS_FMT
			"%s: failed going to ready state (ioc_state=0x%x)\n",
			ioc->name, __func__, ioc_state);
		goto out;
	}

	pr_info(MPT3SAS_FMT "diag reset: SUCCESS\n", ioc->name);
	return 0;

 out:
	pr_err(MPT3SAS_FMT "diag reset: FAILED\n", ioc->name);
	return -EFAULT;
}

/**
 * _base_make_ioc_ready - put controller in READY state
 * @ioc: per adapter object
 * @type: FORCE_BIG_HAMMER or SOFT_RESET
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_make_ioc_ready(struct MPT3SAS_ADAPTER *ioc, enum reset_type type)
{
	u32 ioc_state;
	int rc;
	int count;

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	if (ioc->pci_error_recovery)
		return 0;

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	dhsprintk(ioc, pr_info(MPT3SAS_FMT "%s: ioc_state(0x%08x)\n",
	    ioc->name, __func__, ioc_state));

	/* if in RESET state, it should move to READY state shortly */
	count = 0;
	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_RESET) {
		while ((ioc_state & MPI2_IOC_STATE_MASK) !=
		    MPI2_IOC_STATE_READY) {
			if (count++ == 10) {
				pr_err(MPT3SAS_FMT
					"%s: failed going to ready state (ioc_state=0x%x)\n",
				    ioc->name, __func__, ioc_state);
				return -EFAULT;
			}
			ssleep(1);
			ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
		}
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_READY)
		return 0;

	if (ioc_state & MPI2_DOORBELL_USED) {
		dhsprintk(ioc, pr_info(MPT3SAS_FMT
			"unexpected doorbell active!\n",
			ioc->name));
		goto issue_diag_reset;
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt3sas_base_fault_info(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		goto issue_diag_reset;
	}

	if (type == FORCE_BIG_HAMMER)
		goto issue_diag_reset;

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_OPERATIONAL)
		if (!(_base_send_ioc_reset(ioc,
		    MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET, 15))) {
			return 0;
	}

 issue_diag_reset:
	rc = _base_diag_reset(ioc);
	return rc;
}

/**
 * _base_make_ioc_operational - put controller in OPERATIONAL state
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_make_ioc_operational(struct MPT3SAS_ADAPTER *ioc)
{
	int r, i, index;
	unsigned long	flags;
	u32 reply_address;
	u16 smid;
	struct _tr_list *delayed_tr, *delayed_tr_next;
	struct _sc_list *delayed_sc, *delayed_sc_next;
	struct _event_ack_list *delayed_event_ack, *delayed_event_ack_next;
	u8 hide_flag;
	struct adapter_reply_queue *reply_q;
	Mpi2ReplyDescriptorsUnion_t *reply_post_free_contig;

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
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

	list_for_each_entry_safe(delayed_sc, delayed_sc_next,
	    &ioc->delayed_sc_list, list) {
		list_del(&delayed_sc->list);
		kfree(delayed_sc);
	}

	list_for_each_entry_safe(delayed_event_ack, delayed_event_ack_next,
	    &ioc->delayed_event_ack_list, list) {
		list_del(&delayed_event_ack->list);
		kfree(delayed_event_ack);
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
		ioc->scsi_lookup[i].direct_io = 0;
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

	/* initialize reply queues */
	if (ioc->is_driver_loading)
		_base_assign_reply_queues(ioc);

	/* initialize Reply Post Free Queue */
	index = 0;
	reply_post_free_contig = ioc->reply_post[0].reply_post_free;
	list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {
		/*
		 * If RDPQ is enabled, switch to the next allocation.
		 * Otherwise advance within the contiguous region.
		 */
		if (ioc->rdpq_array_enable) {
			reply_q->reply_post_free =
				ioc->reply_post[index++].reply_post_free;
		} else {
			reply_q->reply_post_free = reply_post_free_contig;
			reply_post_free_contig += ioc->reply_post_queue_depth;
		}

		reply_q->reply_post_host_index = 0;
		for (i = 0; i < ioc->reply_post_queue_depth; i++)
			reply_q->reply_post_free[i].Words =
			    cpu_to_le64(ULLONG_MAX);
		if (!_base_is_controller_msix_enabled(ioc))
			goto skip_init_reply_post_free_queue;
	}
 skip_init_reply_post_free_queue:

	r = _base_send_ioc_init(ioc);
	if (r)
		return r;

	/* initialize reply free host index */
	ioc->reply_free_host_index = ioc->reply_free_queue_depth - 1;
	writel(ioc->reply_free_host_index, &ioc->chip->ReplyFreeHostIndex);

	/* initialize reply post host index */
	list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {
		if (ioc->combined_reply_queue)
			writel((reply_q->msix_index & 7)<<
			   MPI2_RPHI_MSIX_INDEX_SHIFT,
			   ioc->replyPostRegisterIndex[reply_q->msix_index/8]);
		else
			writel(reply_q->msix_index <<
				MPI2_RPHI_MSIX_INDEX_SHIFT,
				&ioc->chip->ReplyPostHostIndex);

		if (!_base_is_controller_msix_enabled(ioc))
			goto skip_init_reply_post_host_index;
	}

 skip_init_reply_post_host_index:

	_base_unmask_interrupts(ioc);
	r = _base_event_notification(ioc);
	if (r)
		return r;

	_base_static_config_pages(ioc);

	if (ioc->is_driver_loading) {

		if (ioc->is_warpdrive && ioc->manu_pg10.OEMIdentifier
		    == 0x80) {
			hide_flag = (u8) (
			    le32_to_cpu(ioc->manu_pg10.OEMSpecificFlags0) &
			    MFG_PAGE10_HIDE_SSDS_MASK);
			if (hide_flag != MFG_PAGE10_HIDE_SSDS_MASK)
				ioc->mfg_pg10_hide_flag = hide_flag;
		}

		ioc->wait_for_discovery_to_complete =
		    _base_determine_wait_on_discovery(ioc);

		return r; /* scan_start and scan_finished support */
	}

	r = _base_send_port_enable(ioc);
	if (r)
		return r;

	return r;
}

/**
 * mpt3sas_base_free_resources - free resources controller resources
 * @ioc: per adapter object
 *
 * Return nothing.
 */
void
mpt3sas_base_free_resources(struct MPT3SAS_ADAPTER *ioc)
{
	dexitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	/* synchronizing freeing resource with pci_access_mutex lock */
	mutex_lock(&ioc->pci_access_mutex);
	if (ioc->chip_phys && ioc->chip) {
		_base_mask_interrupts(ioc);
		ioc->shost_recovery = 1;
		_base_make_ioc_ready(ioc, SOFT_RESET);
		ioc->shost_recovery = 0;
	}

	mpt3sas_base_unmap_resources(ioc);
	mutex_unlock(&ioc->pci_access_mutex);
	return;
}

/**
 * mpt3sas_base_attach - attach controller instance
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt3sas_base_attach(struct MPT3SAS_ADAPTER *ioc)
{
	int r, i;
	int cpu_id, last_cpu_id = 0;

	dinitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	/* setup cpu_msix_table */
	ioc->cpu_count = num_online_cpus();
	for_each_online_cpu(cpu_id)
		last_cpu_id = cpu_id;
	ioc->cpu_msix_table_sz = last_cpu_id + 1;
	ioc->cpu_msix_table = kzalloc(ioc->cpu_msix_table_sz, GFP_KERNEL);
	ioc->reply_queue_count = 1;
	if (!ioc->cpu_msix_table) {
		dfailprintk(ioc, pr_info(MPT3SAS_FMT
			"allocation for cpu_msix_table failed!!!\n",
			ioc->name));
		r = -ENOMEM;
		goto out_free_resources;
	}

	if (ioc->is_warpdrive) {
		ioc->reply_post_host_index = kcalloc(ioc->cpu_msix_table_sz,
		    sizeof(resource_size_t *), GFP_KERNEL);
		if (!ioc->reply_post_host_index) {
			dfailprintk(ioc, pr_info(MPT3SAS_FMT "allocation "
				"for cpu_msix_table failed!!!\n", ioc->name));
			r = -ENOMEM;
			goto out_free_resources;
		}
	}

	ioc->rdpq_array_enable_assigned = 0;
	ioc->dma_mask = 0;
	r = mpt3sas_base_map_resources(ioc);
	if (r)
		goto out_free_resources;

	pci_set_drvdata(ioc->pdev, ioc->shost);
	r = _base_get_ioc_facts(ioc);
	if (r)
		goto out_free_resources;

	switch (ioc->hba_mpi_version_belonged) {
	case MPI2_VERSION:
		ioc->build_sg_scmd = &_base_build_sg_scmd;
		ioc->build_sg = &_base_build_sg;
		ioc->build_zero_len_sge = &_base_build_zero_len_sge;
		break;
	case MPI25_VERSION:
	case MPI26_VERSION:
		/*
		 * In SAS3.0,
		 * SCSI_IO, SMP_PASSTHRU, SATA_PASSTHRU, Target Assist, and
		 * Target Status - all require the IEEE formated scatter gather
		 * elements.
		 */
		ioc->build_sg_scmd = &_base_build_sg_scmd_ieee;
		ioc->build_sg = &_base_build_sg_ieee;
		ioc->build_zero_len_sge = &_base_build_zero_len_sge_ieee;
		ioc->sge_size_ieee = sizeof(Mpi2IeeeSgeSimple64_t);

		break;
	}

	if (ioc->atomic_desc_capable) {
		ioc->put_smid_default = &_base_put_smid_default_atomic;
		ioc->put_smid_scsi_io = &_base_put_smid_scsi_io_atomic;
		ioc->put_smid_fast_path = &_base_put_smid_fast_path_atomic;
		ioc->put_smid_hi_priority = &_base_put_smid_hi_priority_atomic;
	} else {
		ioc->put_smid_default = &_base_put_smid_default;
		ioc->put_smid_scsi_io = &_base_put_smid_scsi_io;
		ioc->put_smid_fast_path = &_base_put_smid_fast_path;
		ioc->put_smid_hi_priority = &_base_put_smid_hi_priority;
	}


	/*
	 * These function pointers for other requests that don't
	 * the require IEEE scatter gather elements.
	 *
	 * For example Configuration Pages and SAS IOUNIT Control don't.
	 */
	ioc->build_sg_mpi = &_base_build_sg;
	ioc->build_zero_len_sge_mpi = &_base_build_zero_len_sge;

	r = _base_make_ioc_ready(ioc, SOFT_RESET);
	if (r)
		goto out_free_resources;

	ioc->pfacts = kcalloc(ioc->facts.NumberOfPorts,
	    sizeof(struct mpt3sas_port_facts), GFP_KERNEL);
	if (!ioc->pfacts) {
		r = -ENOMEM;
		goto out_free_resources;
	}

	for (i = 0 ; i < ioc->facts.NumberOfPorts; i++) {
		r = _base_get_port_facts(ioc, i);
		if (r)
			goto out_free_resources;
	}

	r = _base_allocate_memory_pools(ioc);
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
	ioc->blocking_handles = kzalloc(ioc->pd_handles_sz,
	    GFP_KERNEL);
	if (!ioc->blocking_handles) {
		r = -ENOMEM;
		goto out_free_resources;
	}

	/* allocate memory for pending OS device add list */
	ioc->pend_os_device_add_sz = (ioc->facts.MaxDevHandle / 8);
	if (ioc->facts.MaxDevHandle % 8)
		ioc->pend_os_device_add_sz++;
	ioc->pend_os_device_add = kzalloc(ioc->pend_os_device_add_sz,
	    GFP_KERNEL);
	if (!ioc->pend_os_device_add)
		goto out_free_resources;

	ioc->device_remove_in_progress_sz = ioc->pend_os_device_add_sz;
	ioc->device_remove_in_progress =
		kzalloc(ioc->device_remove_in_progress_sz, GFP_KERNEL);
	if (!ioc->device_remove_in_progress)
		goto out_free_resources;

	ioc->fwfault_debug = mpt3sas_fwfault_debug;

	/* base internal command bits */
	mutex_init(&ioc->base_cmds.mutex);
	ioc->base_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;

	/* port_enable command bits */
	ioc->port_enable_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->port_enable_cmds.status = MPT3_CMD_NOT_USED;

	/* transport internal command bits */
	ioc->transport_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->transport_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->transport_cmds.mutex);

	/* scsih internal command bits */
	ioc->scsih_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->scsih_cmds.mutex);

	/* task management internal command bits */
	ioc->tm_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->tm_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->tm_cmds.mutex);

	/* config page internal command bits */
	ioc->config_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->config_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->config_cmds.mutex);

	/* ctl module internal command bits */
	ioc->ctl_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->ctl_cmds.sense = kzalloc(SCSI_SENSE_BUFFERSIZE, GFP_KERNEL);
	ioc->ctl_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->ctl_cmds.mutex);

	if (!ioc->base_cmds.reply || !ioc->transport_cmds.reply ||
	    !ioc->scsih_cmds.reply || !ioc->tm_cmds.reply ||
	    !ioc->config_cmds.reply || !ioc->ctl_cmds.reply ||
	    !ioc->ctl_cmds.sense) {
		r = -ENOMEM;
		goto out_free_resources;
	}

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
	_base_unmask_events(ioc, MPI2_EVENT_TEMP_THRESHOLD);
	if (ioc->hba_mpi_version_belonged == MPI26_VERSION)
		_base_unmask_events(ioc, MPI2_EVENT_ACTIVE_CABLE_EXCEPTION);

	r = _base_make_ioc_operational(ioc);
	if (r)
		goto out_free_resources;

	ioc->non_operational_loop = 0;
	return 0;

 out_free_resources:

	ioc->remove_host = 1;

	mpt3sas_base_free_resources(ioc);
	_base_release_memory_pools(ioc);
	pci_set_drvdata(ioc->pdev, NULL);
	kfree(ioc->cpu_msix_table);
	if (ioc->is_warpdrive)
		kfree(ioc->reply_post_host_index);
	kfree(ioc->pd_handles);
	kfree(ioc->blocking_handles);
	kfree(ioc->device_remove_in_progress);
	kfree(ioc->pend_os_device_add);
	kfree(ioc->tm_cmds.reply);
	kfree(ioc->transport_cmds.reply);
	kfree(ioc->scsih_cmds.reply);
	kfree(ioc->config_cmds.reply);
	kfree(ioc->base_cmds.reply);
	kfree(ioc->port_enable_cmds.reply);
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
 * mpt3sas_base_detach - remove controller instance
 * @ioc: per adapter object
 *
 * Return nothing.
 */
void
mpt3sas_base_detach(struct MPT3SAS_ADAPTER *ioc)
{
	dexitprintk(ioc, pr_info(MPT3SAS_FMT "%s\n", ioc->name,
	    __func__));

	mpt3sas_base_stop_watchdog(ioc);
	mpt3sas_base_free_resources(ioc);
	_base_release_memory_pools(ioc);
	pci_set_drvdata(ioc->pdev, NULL);
	kfree(ioc->cpu_msix_table);
	if (ioc->is_warpdrive)
		kfree(ioc->reply_post_host_index);
	kfree(ioc->pd_handles);
	kfree(ioc->blocking_handles);
	kfree(ioc->device_remove_in_progress);
	kfree(ioc->pend_os_device_add);
	kfree(ioc->pfacts);
	kfree(ioc->ctl_cmds.reply);
	kfree(ioc->ctl_cmds.sense);
	kfree(ioc->base_cmds.reply);
	kfree(ioc->port_enable_cmds.reply);
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
 * The reset phase can be MPT3_IOC_PRE_RESET, MPT3_IOC_AFTER_RESET,
 * MPT3_IOC_DONE_RESET
 *
 * Return nothing.
 */
static void
_base_reset_handler(struct MPT3SAS_ADAPTER *ioc, int reset_phase)
{
	mpt3sas_scsih_reset_handler(ioc, reset_phase);
	mpt3sas_ctl_reset_handler(ioc, reset_phase);
	switch (reset_phase) {
	case MPT3_IOC_PRE_RESET:
		dtmprintk(ioc, pr_info(MPT3SAS_FMT
		"%s: MPT3_IOC_PRE_RESET\n", ioc->name, __func__));
		break;
	case MPT3_IOC_AFTER_RESET:
		dtmprintk(ioc, pr_info(MPT3SAS_FMT
		"%s: MPT3_IOC_AFTER_RESET\n", ioc->name, __func__));
		if (ioc->transport_cmds.status & MPT3_CMD_PENDING) {
			ioc->transport_cmds.status |= MPT3_CMD_RESET;
			mpt3sas_base_free_smid(ioc, ioc->transport_cmds.smid);
			complete(&ioc->transport_cmds.done);
		}
		if (ioc->base_cmds.status & MPT3_CMD_PENDING) {
			ioc->base_cmds.status |= MPT3_CMD_RESET;
			mpt3sas_base_free_smid(ioc, ioc->base_cmds.smid);
			complete(&ioc->base_cmds.done);
		}
		if (ioc->port_enable_cmds.status & MPT3_CMD_PENDING) {
			ioc->port_enable_failed = 1;
			ioc->port_enable_cmds.status |= MPT3_CMD_RESET;
			mpt3sas_base_free_smid(ioc, ioc->port_enable_cmds.smid);
			if (ioc->is_driver_loading) {
				ioc->start_scan_failed =
				    MPI2_IOCSTATUS_INTERNAL_ERROR;
				ioc->start_scan = 0;
				ioc->port_enable_cmds.status =
				    MPT3_CMD_NOT_USED;
			} else
				complete(&ioc->port_enable_cmds.done);
		}
		if (ioc->config_cmds.status & MPT3_CMD_PENDING) {
			ioc->config_cmds.status |= MPT3_CMD_RESET;
			mpt3sas_base_free_smid(ioc, ioc->config_cmds.smid);
			ioc->config_cmds.smid = USHRT_MAX;
			complete(&ioc->config_cmds.done);
		}
		break;
	case MPT3_IOC_DONE_RESET:
		dtmprintk(ioc, pr_info(MPT3SAS_FMT
			"%s: MPT3_IOC_DONE_RESET\n", ioc->name, __func__));
		break;
	}
}

/**
 * _wait_for_commands_to_complete - reset controller
 * @ioc: Pointer to MPT_ADAPTER structure
 *
 * This function waiting(3s) for all pending commands to complete
 * prior to putting controller in reset.
 */
static void
_wait_for_commands_to_complete(struct MPT3SAS_ADAPTER *ioc)
{
	u32 ioc_state;
	unsigned long flags;
	u16 i;

	ioc->pending_io_count = 0;

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
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
 * mpt3sas_base_hard_reset_handler - reset controller
 * @ioc: Pointer to MPT_ADAPTER structure
 * @type: FORCE_BIG_HAMMER or SOFT_RESET
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt3sas_base_hard_reset_handler(struct MPT3SAS_ADAPTER *ioc,
	enum reset_type type)
{
	int r;
	unsigned long flags;
	u32 ioc_state;
	u8 is_fault = 0, is_trigger = 0;

	dtmprintk(ioc, pr_info(MPT3SAS_FMT "%s: enter\n", ioc->name,
	    __func__));

	if (ioc->pci_error_recovery) {
		pr_err(MPT3SAS_FMT "%s: pci error recovery reset\n",
		    ioc->name, __func__);
		r = 0;
		goto out_unlocked;
	}

	if (mpt3sas_fwfault_debug)
		mpt3sas_halt_firmware(ioc);

	/* wait for an active reset in progress to complete */
	if (!mutex_trylock(&ioc->reset_in_progress_mutex)) {
		do {
			ssleep(1);
		} while (ioc->shost_recovery == 1);
		dtmprintk(ioc, pr_info(MPT3SAS_FMT "%s: exit\n", ioc->name,
		    __func__));
		return ioc->ioc_reset_in_progress_status;
	}

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	ioc->shost_recovery = 1;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);

	if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) &&
	    (!(ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_RELEASED))) {
		is_trigger = 1;
		ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
		if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT)
			is_fault = 1;
	}
	_base_reset_handler(ioc, MPT3_IOC_PRE_RESET);
	_wait_for_commands_to_complete(ioc);
	_base_mask_interrupts(ioc);
	r = _base_make_ioc_ready(ioc, type);
	if (r)
		goto out;
	_base_reset_handler(ioc, MPT3_IOC_AFTER_RESET);

	/* If this hard reset is called while port enable is active, then
	 * there is no reason to call make_ioc_operational
	 */
	if (ioc->is_driver_loading && ioc->port_enable_failed) {
		ioc->remove_host = 1;
		r = -EFAULT;
		goto out;
	}
	r = _base_get_ioc_facts(ioc);
	if (r)
		goto out;

	if (ioc->rdpq_array_enable && !ioc->rdpq_array_capable)
		panic("%s: Issue occurred with flashing controller firmware."
		      "Please reboot the system and ensure that the correct"
		      " firmware version is running\n", ioc->name);

	r = _base_make_ioc_operational(ioc);
	if (!r)
		_base_reset_handler(ioc, MPT3_IOC_DONE_RESET);

 out:
	dtmprintk(ioc, pr_info(MPT3SAS_FMT "%s: %s\n",
	    ioc->name, __func__, ((r == 0) ? "SUCCESS" : "FAILED")));

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	ioc->ioc_reset_in_progress_status = r;
	ioc->shost_recovery = 0;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
	ioc->ioc_reset_count++;
	mutex_unlock(&ioc->reset_in_progress_mutex);

 out_unlocked:
	if ((r == 0) && is_trigger) {
		if (is_fault)
			mpt3sas_trigger_master(ioc, MASTER_TRIGGER_FW_FAULT);
		else
			mpt3sas_trigger_master(ioc,
			    MASTER_TRIGGER_ADAPTER_RESET);
	}
	dtmprintk(ioc, pr_info(MPT3SAS_FMT "%s: exit\n", ioc->name,
	    __func__));
	return r;
}
