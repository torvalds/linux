/*
 * This module provides common API to set Diagnostic trigger for MPT
 * (Message Passing Technology) based controllers
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_trigger_diag.c
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

/**
 * _mpt3sas_raise_sigio - notifiy app
 * @ioc: per adapter object
 * @event_data: ?
 */
static void
_mpt3sas_raise_sigio(struct MPT3SAS_ADAPTER *ioc,
	struct SL_WH_TRIGGERS_EVENT_DATA_T *event_data)
{
	Mpi2EventNotificationReply_t *mpi_reply;
	u16 sz, event_data_sz;
	unsigned long flags;

	dTriggerDiagPrintk(ioc, ioc_info(ioc, "%s: enter\n", __func__));

	sz = offsetof(Mpi2EventNotificationReply_t, EventData) +
	    sizeof(struct SL_WH_TRIGGERS_EVENT_DATA_T) + 4;
	mpi_reply = kzalloc(sz, GFP_KERNEL);
	if (!mpi_reply)
		goto out;
	mpi_reply->Event = cpu_to_le16(MPI3_EVENT_DIAGNOSTIC_TRIGGER_FIRED);
	event_data_sz = (sizeof(struct SL_WH_TRIGGERS_EVENT_DATA_T) + 4) / 4;
	mpi_reply->EventDataLength = cpu_to_le16(event_data_sz);
	memcpy(&mpi_reply->EventData, event_data,
	    sizeof(struct SL_WH_TRIGGERS_EVENT_DATA_T));
	dTriggerDiagPrintk(ioc,
			   ioc_info(ioc, "%s: add to driver event log\n",
				    __func__));
	mpt3sas_ctl_add_to_event_log(ioc, mpi_reply);
	kfree(mpi_reply);
 out:

	/* clearing the diag_trigger_active flag */
	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);
	dTriggerDiagPrintk(ioc,
			   ioc_info(ioc, "%s: clearing diag_trigger_active flag\n",
				    __func__));
	ioc->diag_trigger_active = 0;
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);

	dTriggerDiagPrintk(ioc, ioc_info(ioc, "%s: exit\n",
					 __func__));
}

/**
 * mpt3sas_process_trigger_data - process the event data for the trigger
 * @ioc: per adapter object
 * @event_data: ?
 */
void
mpt3sas_process_trigger_data(struct MPT3SAS_ADAPTER *ioc,
	struct SL_WH_TRIGGERS_EVENT_DATA_T *event_data)
{
	u8 issue_reset = 0;
	u32 *trig_data = (u32 *)&event_data->u.master;

	dTriggerDiagPrintk(ioc, ioc_info(ioc, "%s: enter\n", __func__));

	/* release the diag buffer trace */
	if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_RELEASED) == 0) {
		/*
		 * add a log message so that user knows which event caused
		 * the release
		 */
		ioc_info(ioc,
		    "%s: Releasing the trace buffer. Trigger_Type 0x%08x, Data[0] 0x%08x, Data[1] 0x%08x\n",
		    __func__, event_data->trigger_type,
		    trig_data[0], trig_data[1]);
		mpt3sas_send_diag_release(ioc, MPI2_DIAG_BUF_TYPE_TRACE,
		    &issue_reset);
	}

	_mpt3sas_raise_sigio(ioc, event_data);

	dTriggerDiagPrintk(ioc, ioc_info(ioc, "%s: exit\n",
					 __func__));
}

/**
 * mpt3sas_trigger_master - Master trigger handler
 * @ioc: per adapter object
 * @trigger_bitmask:
 *
 */
void
mpt3sas_trigger_master(struct MPT3SAS_ADAPTER *ioc, u32 trigger_bitmask)
{
	struct SL_WH_TRIGGERS_EVENT_DATA_T event_data;
	unsigned long flags;
	u8 found_match = 0;

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);

	if (trigger_bitmask & MASTER_TRIGGER_FW_FAULT ||
	    trigger_bitmask & MASTER_TRIGGER_ADAPTER_RESET)
		goto by_pass_checks;

	/* check to see if trace buffers are currently registered */
	if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
		return;
	}

	/* check to see if trace buffers are currently released */
	if (ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_RELEASED) {
		spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
		return;
	}

 by_pass_checks:

	dTriggerDiagPrintk(ioc,
			   ioc_info(ioc, "%s: enter - trigger_bitmask = 0x%08x\n",
				    __func__, trigger_bitmask));

	/* don't send trigger if an trigger is currently active */
	if (ioc->diag_trigger_active) {
		spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
		goto out;
	}

	/* check for the trigger condition */
	if (ioc->diag_trigger_master.MasterData & trigger_bitmask) {
		found_match = 1;
		ioc->diag_trigger_active = 1;
		dTriggerDiagPrintk(ioc,
				   ioc_info(ioc, "%s: setting diag_trigger_active flag\n",
					    __func__));
	}
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);

	if (!found_match)
		goto out;

	memset(&event_data, 0, sizeof(struct SL_WH_TRIGGERS_EVENT_DATA_T));
	event_data.trigger_type = MPT3SAS_TRIGGER_MASTER;
	event_data.u.master.MasterData = trigger_bitmask;

	if (trigger_bitmask & MASTER_TRIGGER_FW_FAULT ||
	    trigger_bitmask & MASTER_TRIGGER_ADAPTER_RESET)
		_mpt3sas_raise_sigio(ioc, &event_data);
	else
		mpt3sas_send_trigger_data_event(ioc, &event_data);

 out:
	dTriggerDiagPrintk(ioc, ioc_info(ioc, "%s: exit\n",
					 __func__));
}

/**
 * mpt3sas_trigger_event - Event trigger handler
 * @ioc: per adapter object
 * @event: ?
 * @log_entry_qualifier: ?
 *
 */
void
mpt3sas_trigger_event(struct MPT3SAS_ADAPTER *ioc, u16 event,
	u16 log_entry_qualifier)
{
	struct SL_WH_TRIGGERS_EVENT_DATA_T event_data;
	struct SL_WH_EVENT_TRIGGER_T *event_trigger;
	int i;
	unsigned long flags;
	u8 found_match;

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);

	/* check to see if trace buffers are currently registered */
	if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
		return;
	}

	/* check to see if trace buffers are currently released */
	if (ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_RELEASED) {
		spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
		return;
	}

	dTriggerDiagPrintk(ioc,
			   ioc_info(ioc, "%s: enter - event = 0x%04x, log_entry_qualifier = 0x%04x\n",
				    __func__, event, log_entry_qualifier));

	/* don't send trigger if an trigger is currently active */
	if (ioc->diag_trigger_active) {
		spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
		goto out;
	}

	/* check for the trigger condition */
	event_trigger = ioc->diag_trigger_event.EventTriggerEntry;
	for (i = 0 , found_match = 0; i < ioc->diag_trigger_event.ValidEntries
	    && !found_match; i++, event_trigger++) {
		if (event_trigger->EventValue != event)
			continue;
		if (event == MPI2_EVENT_LOG_ENTRY_ADDED) {
			if (event_trigger->LogEntryQualifier ==
			    log_entry_qualifier)
				found_match = 1;
			continue;
		}
		found_match = 1;
		ioc->diag_trigger_active = 1;
		dTriggerDiagPrintk(ioc,
				   ioc_info(ioc, "%s: setting diag_trigger_active flag\n",
					    __func__));
	}
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);

	if (!found_match)
		goto out;

	dTriggerDiagPrintk(ioc,
			   ioc_info(ioc, "%s: setting diag_trigger_active flag\n",
				    __func__));
	memset(&event_data, 0, sizeof(struct SL_WH_TRIGGERS_EVENT_DATA_T));
	event_data.trigger_type = MPT3SAS_TRIGGER_EVENT;
	event_data.u.event.EventValue = event;
	event_data.u.event.LogEntryQualifier = log_entry_qualifier;
	mpt3sas_send_trigger_data_event(ioc, &event_data);
 out:
	dTriggerDiagPrintk(ioc, ioc_info(ioc, "%s: exit\n",
					 __func__));
}

/**
 * mpt3sas_trigger_scsi - SCSI trigger handler
 * @ioc: per adapter object
 * @sense_key: ?
 * @asc: ?
 * @ascq: ?
 *
 */
void
mpt3sas_trigger_scsi(struct MPT3SAS_ADAPTER *ioc, u8 sense_key, u8 asc,
	u8 ascq)
{
	struct SL_WH_TRIGGERS_EVENT_DATA_T event_data;
	struct SL_WH_SCSI_TRIGGER_T *scsi_trigger;
	int i;
	unsigned long flags;
	u8 found_match;

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);

	/* check to see if trace buffers are currently registered */
	if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
		return;
	}

	/* check to see if trace buffers are currently released */
	if (ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_RELEASED) {
		spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
		return;
	}

	dTriggerDiagPrintk(ioc,
			   ioc_info(ioc, "%s: enter - sense_key = 0x%02x, asc = 0x%02x, ascq = 0x%02x\n",
				    __func__, sense_key, asc, ascq));

	/* don't send trigger if an trigger is currently active */
	if (ioc->diag_trigger_active) {
		spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
		goto out;
	}

	/* check for the trigger condition */
	scsi_trigger = ioc->diag_trigger_scsi.SCSITriggerEntry;
	for (i = 0 , found_match = 0; i < ioc->diag_trigger_scsi.ValidEntries
	    && !found_match; i++, scsi_trigger++) {
		if (scsi_trigger->SenseKey != sense_key)
			continue;
		if (!(scsi_trigger->ASC == 0xFF || scsi_trigger->ASC == asc))
			continue;
		if (!(scsi_trigger->ASCQ == 0xFF || scsi_trigger->ASCQ == ascq))
			continue;
		found_match = 1;
		ioc->diag_trigger_active = 1;
	}
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);

	if (!found_match)
		goto out;

	dTriggerDiagPrintk(ioc,
			   ioc_info(ioc, "%s: setting diag_trigger_active flag\n",
				    __func__));
	memset(&event_data, 0, sizeof(struct SL_WH_TRIGGERS_EVENT_DATA_T));
	event_data.trigger_type = MPT3SAS_TRIGGER_SCSI;
	event_data.u.scsi.SenseKey = sense_key;
	event_data.u.scsi.ASC = asc;
	event_data.u.scsi.ASCQ = ascq;
	mpt3sas_send_trigger_data_event(ioc, &event_data);
 out:
	dTriggerDiagPrintk(ioc, ioc_info(ioc, "%s: exit\n",
					 __func__));
}

/**
 * mpt3sas_trigger_mpi - MPI trigger handler
 * @ioc: per adapter object
 * @ioc_status: ?
 * @loginfo: ?
 *
 */
void
mpt3sas_trigger_mpi(struct MPT3SAS_ADAPTER *ioc, u16 ioc_status, u32 loginfo)
{
	struct SL_WH_TRIGGERS_EVENT_DATA_T event_data;
	struct SL_WH_MPI_TRIGGER_T *mpi_trigger;
	int i;
	unsigned long flags;
	u8 found_match;

	spin_lock_irqsave(&ioc->diag_trigger_lock, flags);

	/* check to see if trace buffers are currently registered */
	if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) == 0) {
		spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
		return;
	}

	/* check to see if trace buffers are currently released */
	if (ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_RELEASED) {
		spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
		return;
	}

	dTriggerDiagPrintk(ioc,
			   ioc_info(ioc, "%s: enter - ioc_status = 0x%04x, loginfo = 0x%08x\n",
				    __func__, ioc_status, loginfo));

	/* don't send trigger if an trigger is currently active */
	if (ioc->diag_trigger_active) {
		spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);
		goto out;
	}

	/* check for the trigger condition */
	mpi_trigger = ioc->diag_trigger_mpi.MPITriggerEntry;
	for (i = 0 , found_match = 0; i < ioc->diag_trigger_mpi.ValidEntries
	    && !found_match; i++, mpi_trigger++) {
		if (mpi_trigger->IOCStatus != ioc_status)
			continue;
		if (!(mpi_trigger->IocLogInfo == 0xFFFFFFFF ||
		    mpi_trigger->IocLogInfo == loginfo))
			continue;
		found_match = 1;
		ioc->diag_trigger_active = 1;
	}
	spin_unlock_irqrestore(&ioc->diag_trigger_lock, flags);

	if (!found_match)
		goto out;

	dTriggerDiagPrintk(ioc,
			   ioc_info(ioc, "%s: setting diag_trigger_active flag\n",
				    __func__));
	memset(&event_data, 0, sizeof(struct SL_WH_TRIGGERS_EVENT_DATA_T));
	event_data.trigger_type = MPT3SAS_TRIGGER_MPI;
	event_data.u.mpi.IOCStatus = ioc_status;
	event_data.u.mpi.IocLogInfo = loginfo;
	mpt3sas_send_trigger_data_event(ioc, &event_data);
 out:
	dTriggerDiagPrintk(ioc, ioc_info(ioc, "%s: exit\n",
					 __func__));
}
