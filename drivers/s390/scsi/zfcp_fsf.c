/*
 * This file is part of the zfcp device driver for
 * FCP adapters for IBM System z9 and zSeries.
 *
 * (C) Copyright IBM Corp. 2002, 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "zfcp_ext.h"

static int zfcp_fsf_exchange_config_data_handler(struct zfcp_fsf_req *);
static void zfcp_fsf_exchange_port_data_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_open_port_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_close_port_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_close_physical_port_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_open_unit_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_close_unit_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_send_fcp_command_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_send_fcp_command_task_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_send_fcp_command_task_management_handler(
	struct zfcp_fsf_req *);
static int zfcp_fsf_abort_fcp_command_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_status_read_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_send_ct_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_send_els_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_control_file_handler(struct zfcp_fsf_req *);
static inline int zfcp_fsf_req_sbal_check(
	unsigned long *, struct zfcp_qdio_queue *, int);
static inline int zfcp_use_one_sbal(
	struct scatterlist *, int, struct scatterlist *, int);
static struct zfcp_fsf_req *zfcp_fsf_req_alloc(mempool_t *, int);
static int zfcp_fsf_req_send(struct zfcp_fsf_req *, struct timer_list *);
static int zfcp_fsf_protstatus_eval(struct zfcp_fsf_req *);
static int zfcp_fsf_fsfstatus_eval(struct zfcp_fsf_req *);
static int zfcp_fsf_fsfstatus_qual_eval(struct zfcp_fsf_req *);
static void zfcp_fsf_link_down_info_eval(struct zfcp_adapter *,
	struct fsf_link_down_info *);
static int zfcp_fsf_req_dispatch(struct zfcp_fsf_req *);
static void zfcp_fsf_req_dismiss(struct zfcp_fsf_req *);

/* association between FSF command and FSF QTCB type */
static u32 fsf_qtcb_type[] = {
	[FSF_QTCB_FCP_CMND] =             FSF_IO_COMMAND,
	[FSF_QTCB_ABORT_FCP_CMND] =       FSF_SUPPORT_COMMAND,
	[FSF_QTCB_OPEN_PORT_WITH_DID] =   FSF_SUPPORT_COMMAND,
	[FSF_QTCB_OPEN_LUN] =             FSF_SUPPORT_COMMAND,
	[FSF_QTCB_CLOSE_LUN] =            FSF_SUPPORT_COMMAND,
	[FSF_QTCB_CLOSE_PORT] =           FSF_SUPPORT_COMMAND,
	[FSF_QTCB_CLOSE_PHYSICAL_PORT] =  FSF_SUPPORT_COMMAND,
	[FSF_QTCB_SEND_ELS] =             FSF_SUPPORT_COMMAND,
	[FSF_QTCB_SEND_GENERIC] =         FSF_SUPPORT_COMMAND,
	[FSF_QTCB_EXCHANGE_CONFIG_DATA] = FSF_CONFIG_COMMAND,
	[FSF_QTCB_EXCHANGE_PORT_DATA] =   FSF_PORT_COMMAND,
	[FSF_QTCB_DOWNLOAD_CONTROL_FILE] = FSF_SUPPORT_COMMAND,
	[FSF_QTCB_UPLOAD_CONTROL_FILE] =  FSF_SUPPORT_COMMAND
};

static const char zfcp_act_subtable_type[5][8] = {
	"unknown", "OS", "WWPN", "DID", "LUN"
};

/****************************************************************/
/*************** FSF related Functions  *************************/
/****************************************************************/

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF

/*
 * function:	zfcp_fsf_req_alloc
 *
 * purpose:     Obtains an fsf_req and potentially a qtcb (for all but 
 *              unsolicited requests) via helper functions
 *              Does some initial fsf request set-up.
 *              
 * returns:	pointer to allocated fsf_req if successfull
 *              NULL otherwise
 *
 * locks:       none
 *
 */
static struct zfcp_fsf_req *
zfcp_fsf_req_alloc(mempool_t *pool, int req_flags)
{
	size_t size;
	void *ptr;
	struct zfcp_fsf_req *fsf_req = NULL;

	if (req_flags & ZFCP_REQ_NO_QTCB)
		size = sizeof(struct zfcp_fsf_req);
	else
		size = sizeof(struct zfcp_fsf_req_pool_element);

	if (likely(pool != NULL))
		ptr = mempool_alloc(pool, GFP_ATOMIC);
	else
		ptr = kmalloc(size, GFP_ATOMIC);

	if (unlikely(NULL == ptr))
		goto out;

	memset(ptr, 0, size);

	if (req_flags & ZFCP_REQ_NO_QTCB) {
		fsf_req = (struct zfcp_fsf_req *) ptr;
	} else {
		fsf_req = &((struct zfcp_fsf_req_pool_element *) ptr)->fsf_req;
		fsf_req->qtcb =
			&((struct zfcp_fsf_req_pool_element *) ptr)->qtcb;
	}

	fsf_req->pool = pool;

 out:
	return fsf_req;
}

/*
 * function:	zfcp_fsf_req_free
 *
 * purpose:     Frees the memory of an fsf_req (and potentially a qtcb) or
 *              returns it into the pool via helper functions.
 *
 * returns:     sod all
 *
 * locks:       none
 */
void
zfcp_fsf_req_free(struct zfcp_fsf_req *fsf_req)
{
	if (likely(fsf_req->pool != NULL))
		mempool_free(fsf_req, fsf_req->pool);
	else
		kfree(fsf_req);
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 *
 * note: qdio queues shall be down (no ongoing inbound processing)
 */
int
zfcp_fsf_req_dismiss_all(struct zfcp_adapter *adapter)
{
	struct zfcp_fsf_req *fsf_req, *tmp;
	unsigned long flags;
	LIST_HEAD(remove_queue);

	spin_lock_irqsave(&adapter->fsf_req_list_lock, flags);
	list_splice_init(&adapter->fsf_req_list_head, &remove_queue);
	atomic_set(&adapter->fsf_reqs_active, 0);
	spin_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);

	list_for_each_entry_safe(fsf_req, tmp, &remove_queue, list) {
		list_del(&fsf_req->list);
		zfcp_fsf_req_dismiss(fsf_req);
	}

	return 0;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static void
zfcp_fsf_req_dismiss(struct zfcp_fsf_req *fsf_req)
{
	fsf_req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
	zfcp_fsf_req_complete(fsf_req);
}

/*
 * function:    zfcp_fsf_req_complete
 *
 * purpose:	Updates active counts and timers for openfcp-reqs
 *              May cleanup request after req_eval returns
 *
 * returns:	0 - success
 *		!0 - failure
 *
 * context:	
 */
int
zfcp_fsf_req_complete(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;
	int cleanup;

	if (unlikely(fsf_req->fsf_command == FSF_QTCB_UNSOLICITED_STATUS)) {
		ZFCP_LOG_DEBUG("Status read response received\n");
		/*
		 * Note: all cleanup handling is done in the callchain of
		 * the function call-chain below.
		 */
		zfcp_fsf_status_read_handler(fsf_req);
		goto out;
	} else
		zfcp_fsf_protstatus_eval(fsf_req);

	/*
	 * fsf_req may be deleted due to waking up functions, so 
	 * cleanup is saved here and used later 
	 */
	if (likely(fsf_req->status & ZFCP_STATUS_FSFREQ_CLEANUP))
		cleanup = 1;
	else
		cleanup = 0;

	fsf_req->status |= ZFCP_STATUS_FSFREQ_COMPLETED;

	/* cleanup request if requested by initiator */
	if (likely(cleanup)) {
		ZFCP_LOG_TRACE("removing FSF request %p\n", fsf_req);
		/*
		 * lock must not be held here since it will be
		 * grabed by the called routine, too
		 */
		zfcp_fsf_req_free(fsf_req);
	} else {
		/* notify initiator waiting for the requests completion */
		ZFCP_LOG_TRACE("waking initiator of FSF request %p\n",fsf_req);
		/*
		 * FIXME: Race! We must not access fsf_req here as it might have been
		 * cleaned up already due to the set ZFCP_STATUS_FSFREQ_COMPLETED
		 * flag. It's an improbable case. But, we have the same paranoia for
		 * the cleanup flag already.
		 * Might better be handled using complete()?
		 * (setting the flag and doing wakeup ought to be atomic
		 *  with regard to checking the flag as long as waitqueue is
		 *  part of the to be released structure)
		 */
		wake_up(&fsf_req->completion_wq);
	}

 out:
	return retval;
}

/*
 * function:    zfcp_fsf_protstatus_eval
 *
 * purpose:	evaluates the QTCB of the finished FSF request
 *		and initiates appropriate actions
 *		(usually calling FSF command specific handlers)
 *
 * returns:	
 *
 * context:	
 *
 * locks:
 */
static int
zfcp_fsf_protstatus_eval(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct fsf_qtcb *qtcb = fsf_req->qtcb;
	union fsf_prot_status_qual *prot_status_qual =
		&qtcb->prefix.prot_status_qual;

	zfcp_hba_dbf_event_fsf_response(fsf_req);

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_DISMISSED) {
		ZFCP_LOG_DEBUG("fsf_req 0x%lx has been dismissed\n",
			       (unsigned long) fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY; /* only for SCSI cmnds. */
		goto skip_protstatus;
	}

	/* log additional information provided by FSF (if any) */
	if (unlikely(qtcb->header.log_length)) {
		/* do not trust them ;-) */
		if (qtcb->header.log_start > sizeof(struct fsf_qtcb)) {
			ZFCP_LOG_NORMAL
			    ("bug: ULP (FSF logging) log data starts "
			     "beyond end of packet header. Ignored. "
			     "(start=%i, size=%li)\n",
			     qtcb->header.log_start,
			     sizeof(struct fsf_qtcb));
			goto forget_log;
		}
		if ((size_t) (qtcb->header.log_start + qtcb->header.log_length)
		    > sizeof(struct fsf_qtcb)) {
			ZFCP_LOG_NORMAL("bug: ULP (FSF logging) log data ends "
					"beyond end of packet header. Ignored. "
					"(start=%i, length=%i, size=%li)\n",
					qtcb->header.log_start,
					qtcb->header.log_length,
					sizeof(struct fsf_qtcb));
			goto forget_log;
		}
		ZFCP_LOG_TRACE("ULP log data: \n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE,
			      (char *) qtcb + qtcb->header.log_start,
			      qtcb->header.log_length);
	}
 forget_log:

	/* evaluate FSF Protocol Status */
	switch (qtcb->prefix.prot_status) {

	case FSF_PROT_GOOD:
	case FSF_PROT_FSF_STATUS_PRESENTED:
		break;

	case FSF_PROT_QTCB_VERSION_ERROR:
		ZFCP_LOG_NORMAL("error: The adapter %s contains "
				"microcode of version 0x%x, the device driver "
				"only supports 0x%x. Aborting.\n",
				zfcp_get_busid_by_adapter(adapter),
				prot_status_qual->version_error.fsf_version,
				ZFCP_QTCB_VERSION);
		zfcp_erp_adapter_shutdown(adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_SEQ_NUMB_ERROR:
		ZFCP_LOG_NORMAL("bug: Sequence number mismatch between "
				"driver (0x%x) and adapter %s (0x%x). "
				"Restarting all operations on this adapter.\n",
				qtcb->prefix.req_seq_no,
				zfcp_get_busid_by_adapter(adapter),
				prot_status_qual->sequence_error.exp_req_seq_no);
		zfcp_erp_adapter_reopen(adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_RETRY;
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_UNSUPP_QTCB_TYPE:
		ZFCP_LOG_NORMAL("error: Packet header type used by the "
				"device driver is incompatible with "
				"that used on adapter %s. "
				"Stopping all operations on this adapter.\n",
				zfcp_get_busid_by_adapter(adapter));
		zfcp_erp_adapter_shutdown(adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_HOST_CONNECTION_INITIALIZING:
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		atomic_set_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
				&(adapter->status));
		break;

	case FSF_PROT_DUPLICATE_REQUEST_ID:
			ZFCP_LOG_NORMAL("bug: The request identifier 0x%Lx "
					"to the adapter %s is ambiguous. "
				"Stopping all operations on this adapter.\n",
				*(unsigned long long*)
				(&qtcb->bottom.support.req_handle),
					zfcp_get_busid_by_adapter(adapter));
		zfcp_erp_adapter_shutdown(adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_LINK_DOWN:
		zfcp_fsf_link_down_info_eval(adapter,
					     &prot_status_qual->link_down_info);
		zfcp_erp_adapter_reopen(adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_REEST_QUEUE:
		ZFCP_LOG_NORMAL("The local link to adapter with "
			      "%s was re-plugged. "
			      "Re-starting operations on this adapter.\n",
			      zfcp_get_busid_by_adapter(adapter));
		/* All ports should be marked as ready to run again */
		zfcp_erp_modify_adapter_status(adapter,
					       ZFCP_STATUS_COMMON_RUNNING,
					       ZFCP_SET);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED
					| ZFCP_STATUS_COMMON_ERP_FAILED);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_ERROR_STATE:
		ZFCP_LOG_NORMAL("error: The adapter %s "
				"has entered the error state. "
				"Restarting all operations on this "
				"adapter.\n",
				zfcp_get_busid_by_adapter(adapter));
		zfcp_erp_adapter_reopen(adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_RETRY;
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: Transfer protocol status information "
				"provided by the adapter %s "
				"is not compatible with the device driver. "
				"Stopping all operations on this adapter. "
				"(debug info 0x%x).\n",
				zfcp_get_busid_by_adapter(adapter),
				qtcb->prefix.prot_status);
		zfcp_erp_adapter_shutdown(adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
	}

 skip_protstatus:
	/*
	 * always call specific handlers to give them a chance to do
	 * something meaningful even in error cases
	 */
	zfcp_fsf_fsfstatus_eval(fsf_req);
	return retval;
}

/*
 * function:	zfcp_fsf_fsfstatus_eval
 *
 * purpose:	evaluates FSF status of completed FSF request
 *		and acts accordingly
 *
 * returns:
 */
static int
zfcp_fsf_fsfstatus_eval(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;

	if (unlikely(fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR)) {
		goto skip_fsfstatus;
	}

	/* evaluate FSF Status */
	switch (fsf_req->qtcb->header.fsf_status) {
	case FSF_UNKNOWN_COMMAND:
		ZFCP_LOG_NORMAL("bug: Command issued by the device driver is "
				"not known by the adapter %s "
				"Stopping all operations on this adapter. "
				"(debug info 0x%x).\n",
				zfcp_get_busid_by_adapter(fsf_req->adapter),
				fsf_req->qtcb->header.fsf_command);
		zfcp_erp_adapter_shutdown(fsf_req->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_FCP_RSP_AVAILABLE:
		ZFCP_LOG_DEBUG("FCP Sense data will be presented to the "
			       "SCSI stack.\n");
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		zfcp_fsf_fsfstatus_qual_eval(fsf_req);
		break;
	}

 skip_fsfstatus:
	/*
	 * always call specific handlers to give them a chance to do
	 * something meaningful even in error cases
	 */
	zfcp_fsf_req_dispatch(fsf_req);

	return retval;
}

/*
 * function:	zfcp_fsf_fsfstatus_qual_eval
 *
 * purpose:	evaluates FSF status-qualifier of completed FSF request
 *		and acts accordingly
 *
 * returns:
 */
static int
zfcp_fsf_fsfstatus_qual_eval(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;

	switch (fsf_req->qtcb->header.fsf_status_qual.word[0]) {
	case FSF_SQ_FCP_RSP_AVAILABLE:
		break;
	case FSF_SQ_RETRY_IF_POSSIBLE:
		/* The SCSI-stack may now issue retries or escalate */
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SQ_COMMAND_ABORTED:
		/* Carry the aborted state on to upper layer */
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTED;
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SQ_NO_RECOM:
		ZFCP_LOG_NORMAL("bug: No recommendation could be given for a"
				"problem on the adapter %s "
				"Stopping all operations on this adapter. ",
				zfcp_get_busid_by_adapter(fsf_req->adapter));
		zfcp_erp_adapter_shutdown(fsf_req->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SQ_ULP_PROGRAMMING_ERROR:
		ZFCP_LOG_NORMAL("error: not enough SBALs for data transfer "
				"(adapter %s)\n",
				zfcp_get_busid_by_adapter(fsf_req->adapter));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
	case FSF_SQ_NO_RETRY_POSSIBLE:
	case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
		/* dealt with in the respective functions */
		break;
	default:
		ZFCP_LOG_NORMAL("bug: Additional status info could "
				"not be interpreted properly.\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}

	return retval;
}

/**
 * zfcp_fsf_link_down_info_eval - evaluate link down information block
 */
static void
zfcp_fsf_link_down_info_eval(struct zfcp_adapter *adapter,
			     struct fsf_link_down_info *link_down)
{
	if (atomic_test_mask(ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED,
	                     &adapter->status))
		return;

	atomic_set_mask(ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED, &adapter->status);

	if (link_down == NULL)
		goto out;

	switch (link_down->error_code) {
	case FSF_PSQ_LINK_NO_LIGHT:
		ZFCP_LOG_NORMAL("The local link to adapter %s is down "
				"(no light detected)\n",
				zfcp_get_busid_by_adapter(adapter));
		break;
	case FSF_PSQ_LINK_WRAP_PLUG:
		ZFCP_LOG_NORMAL("The local link to adapter %s is down "
				"(wrap plug detected)\n",
				zfcp_get_busid_by_adapter(adapter));
		break;
	case FSF_PSQ_LINK_NO_FCP:
		ZFCP_LOG_NORMAL("The local link to adapter %s is down "
				"(adjacent node on link does not support FCP)\n",
				zfcp_get_busid_by_adapter(adapter));
		break;
	case FSF_PSQ_LINK_FIRMWARE_UPDATE:
		ZFCP_LOG_NORMAL("The local link to adapter %s is down "
				"(firmware update in progress)\n",
				zfcp_get_busid_by_adapter(adapter));
			break;
	case FSF_PSQ_LINK_INVALID_WWPN:
		ZFCP_LOG_NORMAL("The local link to adapter %s is down "
				"(duplicate or invalid WWPN detected)\n",
				zfcp_get_busid_by_adapter(adapter));
		break;
	case FSF_PSQ_LINK_NO_NPIV_SUPPORT:
		ZFCP_LOG_NORMAL("The local link to adapter %s is down "
				"(no support for NPIV by Fabric)\n",
				zfcp_get_busid_by_adapter(adapter));
		break;
	case FSF_PSQ_LINK_NO_FCP_RESOURCES:
		ZFCP_LOG_NORMAL("The local link to adapter %s is down "
				"(out of resource in FCP daughtercard)\n",
				zfcp_get_busid_by_adapter(adapter));
		break;
	case FSF_PSQ_LINK_NO_FABRIC_RESOURCES:
		ZFCP_LOG_NORMAL("The local link to adapter %s is down "
				"(out of resource in Fabric)\n",
				zfcp_get_busid_by_adapter(adapter));
		break;
	case FSF_PSQ_LINK_FABRIC_LOGIN_UNABLE:
		ZFCP_LOG_NORMAL("The local link to adapter %s is down "
				"(unable to Fabric login)\n",
				zfcp_get_busid_by_adapter(adapter));
		break;
	case FSF_PSQ_LINK_WWPN_ASSIGNMENT_CORRUPTED:
		ZFCP_LOG_NORMAL("WWPN assignment file corrupted on adapter %s\n",
				zfcp_get_busid_by_adapter(adapter));
		break;
	case FSF_PSQ_LINK_MODE_TABLE_CURRUPTED:
		ZFCP_LOG_NORMAL("Mode table corrupted on adapter %s\n",
				zfcp_get_busid_by_adapter(adapter));
		break;
	case FSF_PSQ_LINK_NO_WWPN_ASSIGNMENT:
		ZFCP_LOG_NORMAL("No WWPN for assignment table on adapter %s\n",
				zfcp_get_busid_by_adapter(adapter));
		break;
	default:
		ZFCP_LOG_NORMAL("The local link to adapter %s is down "
				"(warning: unknown reason code %d)\n",
				zfcp_get_busid_by_adapter(adapter),
				link_down->error_code);
	}

	if (adapter->connection_features & FSF_FEATURE_NPIV_MODE)
		ZFCP_LOG_DEBUG("Debug information to link down: "
		               "primary_status=0x%02x "
		               "ioerr_code=0x%02x "
		               "action_code=0x%02x "
		               "reason_code=0x%02x "
		               "explanation_code=0x%02x "
		               "vendor_specific_code=0x%02x\n",
				link_down->primary_status,
				link_down->ioerr_code,
				link_down->action_code,
				link_down->reason_code,
				link_down->explanation_code,
				link_down->vendor_specific_code);

 out:
	zfcp_erp_adapter_failed(adapter);
}

/*
 * function:	zfcp_fsf_req_dispatch
 *
 * purpose:	calls the appropriate command specific handler
 *
 * returns:	
 */
static int
zfcp_fsf_req_dispatch(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_erp_action *erp_action = fsf_req->erp_action;
	struct zfcp_adapter *adapter = fsf_req->adapter;
	int retval = 0;


	switch (fsf_req->fsf_command) {

	case FSF_QTCB_FCP_CMND:
		zfcp_fsf_send_fcp_command_handler(fsf_req);
		break;

	case FSF_QTCB_ABORT_FCP_CMND:
		zfcp_fsf_abort_fcp_command_handler(fsf_req);
		break;

	case FSF_QTCB_SEND_GENERIC:
		zfcp_fsf_send_ct_handler(fsf_req);
		break;

	case FSF_QTCB_OPEN_PORT_WITH_DID:
		zfcp_fsf_open_port_handler(fsf_req);
		break;

	case FSF_QTCB_OPEN_LUN:
		zfcp_fsf_open_unit_handler(fsf_req);
		break;

	case FSF_QTCB_CLOSE_LUN:
		zfcp_fsf_close_unit_handler(fsf_req);
		break;

	case FSF_QTCB_CLOSE_PORT:
		zfcp_fsf_close_port_handler(fsf_req);
		break;

	case FSF_QTCB_CLOSE_PHYSICAL_PORT:
		zfcp_fsf_close_physical_port_handler(fsf_req);
		break;

	case FSF_QTCB_EXCHANGE_CONFIG_DATA:
		zfcp_fsf_exchange_config_data_handler(fsf_req);
		break;

	case FSF_QTCB_EXCHANGE_PORT_DATA:
		zfcp_fsf_exchange_port_data_handler(fsf_req);
		break;

	case FSF_QTCB_SEND_ELS:
		zfcp_fsf_send_els_handler(fsf_req);
		break;

	case FSF_QTCB_DOWNLOAD_CONTROL_FILE:
		zfcp_fsf_control_file_handler(fsf_req);
		break;

	case FSF_QTCB_UPLOAD_CONTROL_FILE:
		zfcp_fsf_control_file_handler(fsf_req);
		break;

	default:
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		ZFCP_LOG_NORMAL("bug: Command issued by the device driver is "
				"not supported by the adapter %s\n",
				zfcp_get_busid_by_adapter(adapter));
		if (fsf_req->fsf_command != fsf_req->qtcb->header.fsf_command)
			ZFCP_LOG_NORMAL
			    ("bug: Command issued by the device driver differs "
			     "from the command returned by the adapter %s "
			     "(debug info 0x%x, 0x%x).\n",
			     zfcp_get_busid_by_adapter(adapter),
			     fsf_req->fsf_command,
			     fsf_req->qtcb->header.fsf_command);
	}

	if (!erp_action)
		return retval;

	zfcp_erp_async_handler(erp_action, 0);

	return retval;
}

/*
 * function:    zfcp_fsf_status_read
 *
 * purpose:	initiates a Status Read command at the specified adapter
 *
 * returns:
 */
int
zfcp_fsf_status_read(struct zfcp_adapter *adapter, int req_flags)
{
	struct zfcp_fsf_req *fsf_req;
	struct fsf_status_read_buffer *status_buffer;
	unsigned long lock_flags;
	volatile struct qdio_buffer_element *sbale;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_UNSOLICITED_STATUS,
				     req_flags | ZFCP_REQ_NO_QTCB,
				     adapter->pool.fsf_req_status_read,
				     &lock_flags, &fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Could not create unsolicited status "
			      "buffer for adapter %s.\n",
			      zfcp_get_busid_by_adapter(adapter));
		goto failed_req_create;
	}

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_STATUS;
        sbale[2].flags |= SBAL_FLAGS_LAST_ENTRY;
        fsf_req->sbale_curr = 2;

	status_buffer =
		mempool_alloc(adapter->pool.data_status_read, GFP_ATOMIC);
	if (!status_buffer) {
		ZFCP_LOG_NORMAL("bug: could not get some buffer\n");
		goto failed_buf;
	}
	memset(status_buffer, 0, sizeof (struct fsf_status_read_buffer));
	fsf_req->data = (unsigned long) status_buffer;

	/* insert pointer to respective buffer */
	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->addr = (void *) status_buffer;
	sbale->length = sizeof(struct fsf_status_read_buffer);

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(fsf_req, NULL);
	if (retval) {
		ZFCP_LOG_DEBUG("error: Could not set-up unsolicited status "
			       "environment.\n");
		goto failed_req_send;
	}

	ZFCP_LOG_TRACE("Status Read request initiated (adapter%s)\n",
		       zfcp_get_busid_by_adapter(adapter));
	goto out;

 failed_req_send:
	mempool_free(status_buffer, adapter->pool.data_status_read);

 failed_buf:
	zfcp_fsf_req_free(fsf_req);
 failed_req_create:
	zfcp_hba_dbf_event_fsf_unsol("fail", adapter, NULL);
 out:
	write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);
	return retval;
}

static int
zfcp_fsf_status_read_port_closed(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_status_read_buffer *status_buffer;
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	unsigned long flags;

	status_buffer = (struct fsf_status_read_buffer *) fsf_req->data;
	adapter = fsf_req->adapter;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	list_for_each_entry(port, &adapter->port_list_head, list)
	    if (port->d_id == (status_buffer->d_id & ZFCP_DID_MASK))
		break;
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	if (!port || (port->d_id != (status_buffer->d_id & ZFCP_DID_MASK))) {
		ZFCP_LOG_NORMAL("bug: Reopen port indication received for"
				"nonexisting port with d_id 0x%08x on "
				"adapter %s. Ignored.\n",
				status_buffer->d_id & ZFCP_DID_MASK,
				zfcp_get_busid_by_adapter(adapter));
		goto out;
	}

	switch (status_buffer->status_subtype) {

	case FSF_STATUS_READ_SUB_CLOSE_PHYS_PORT:
		debug_text_event(adapter->erp_dbf, 3, "unsol_pc_phys:");
		zfcp_erp_port_reopen(port, 0);
		break;

	case FSF_STATUS_READ_SUB_ERROR_PORT:
		debug_text_event(adapter->erp_dbf, 1, "unsol_pc_err:");
		zfcp_erp_port_shutdown(port, 0);
		break;

	default:
		debug_text_event(adapter->erp_dbf, 0, "unsol_unk_sub:");
		debug_exception(adapter->erp_dbf, 0,
				&status_buffer->status_subtype, sizeof (u32));
		ZFCP_LOG_NORMAL("bug: Undefined status subtype received "
				"for a reopen indication on port with "
				"d_id 0x%08x on the adapter %s. "
				"Ignored. (debug info 0x%x)\n",
				status_buffer->d_id,
				zfcp_get_busid_by_adapter(adapter),
				status_buffer->status_subtype);
	}
 out:
	return 0;
}

/*
 * function:    zfcp_fsf_status_read_handler
 *
 * purpose:	is called for finished Open Port command
 *
 * returns:	
 */
static int
zfcp_fsf_status_read_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct fsf_status_read_buffer *status_buffer =
		(struct fsf_status_read_buffer *) fsf_req->data;
	struct fsf_bit_error_payload *fsf_bit_error;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_DISMISSED) {
		zfcp_hba_dbf_event_fsf_unsol("dism", adapter, status_buffer);
		mempool_free(status_buffer, adapter->pool.data_status_read);
		zfcp_fsf_req_free(fsf_req);
		goto out;
	}

	zfcp_hba_dbf_event_fsf_unsol("read", adapter, status_buffer);

	switch (status_buffer->status_type) {

	case FSF_STATUS_READ_PORT_CLOSED:
		zfcp_fsf_status_read_port_closed(fsf_req);
		break;

	case FSF_STATUS_READ_INCOMING_ELS:
		zfcp_fsf_incoming_els(fsf_req);
		break;

	case FSF_STATUS_READ_SENSE_DATA_AVAIL:
		ZFCP_LOG_INFO("unsolicited sense data received (adapter %s)\n",
			      zfcp_get_busid_by_adapter(adapter));
		break;

	case FSF_STATUS_READ_BIT_ERROR_THRESHOLD:
		fsf_bit_error = (struct fsf_bit_error_payload *)
			status_buffer->payload;
		ZFCP_LOG_NORMAL("Warning: bit error threshold data "
		    "received (adapter %s, "
		    "link failures = %i, loss of sync errors = %i, "
		    "loss of signal errors = %i, "
		    "primitive sequence errors = %i, "
		    "invalid transmission word errors = %i, "
		    "CRC errors = %i)\n",
		    zfcp_get_busid_by_adapter(adapter),
		    fsf_bit_error->link_failure_error_count,
		    fsf_bit_error->loss_of_sync_error_count,
		    fsf_bit_error->loss_of_signal_error_count,
		    fsf_bit_error->primitive_sequence_error_count,
		    fsf_bit_error->invalid_transmission_word_error_count,
		    fsf_bit_error->crc_error_count);
		ZFCP_LOG_INFO("Additional bit error threshold data "
		    "(adapter %s, "
		    "primitive sequence event time-outs = %i, "
		    "elastic buffer overrun errors = %i, "
		    "advertised receive buffer-to-buffer credit = %i, "
		    "current receice buffer-to-buffer credit = %i, "
		    "advertised transmit buffer-to-buffer credit = %i, "
		    "current transmit buffer-to-buffer credit = %i)\n",
		    zfcp_get_busid_by_adapter(adapter),
		    fsf_bit_error->primitive_sequence_event_timeout_count,
		    fsf_bit_error->elastic_buffer_overrun_error_count,
		    fsf_bit_error->advertised_receive_b2b_credit,
		    fsf_bit_error->current_receive_b2b_credit,
		    fsf_bit_error->advertised_transmit_b2b_credit,
		    fsf_bit_error->current_transmit_b2b_credit);
		break;

	case FSF_STATUS_READ_LINK_DOWN:
		switch (status_buffer->status_subtype) {
		case FSF_STATUS_READ_SUB_NO_PHYSICAL_LINK:
			ZFCP_LOG_INFO("Physical link to adapter %s is down\n",
				      zfcp_get_busid_by_adapter(adapter));
			zfcp_fsf_link_down_info_eval(adapter,
				(struct fsf_link_down_info *)
				&status_buffer->payload);
			break;
		case FSF_STATUS_READ_SUB_FDISC_FAILED:
			ZFCP_LOG_INFO("Local link to adapter %s is down "
				      "due to failed FDISC login\n",
				      zfcp_get_busid_by_adapter(adapter));
			zfcp_fsf_link_down_info_eval(adapter,
				(struct fsf_link_down_info *)
				&status_buffer->payload);
			break;
		case FSF_STATUS_READ_SUB_FIRMWARE_UPDATE:
			ZFCP_LOG_INFO("Local link to adapter %s is down "
				      "due to firmware update on adapter\n",
				      zfcp_get_busid_by_adapter(adapter));
			zfcp_fsf_link_down_info_eval(adapter, NULL);
			break;
		default:
			ZFCP_LOG_INFO("Local link to adapter %s is down "
				      "due to unknown reason\n",
				      zfcp_get_busid_by_adapter(adapter));
			zfcp_fsf_link_down_info_eval(adapter, NULL);
		};
		break;

	case FSF_STATUS_READ_LINK_UP:
		ZFCP_LOG_NORMAL("Local link to adapter %s was replugged. "
				"Restarting operations on this adapter\n",
				zfcp_get_busid_by_adapter(adapter));
		/* All ports should be marked as ready to run again */
		zfcp_erp_modify_adapter_status(adapter,
					       ZFCP_STATUS_COMMON_RUNNING,
					       ZFCP_SET);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED
					| ZFCP_STATUS_COMMON_ERP_FAILED);
		break;

	case FSF_STATUS_READ_NOTIFICATION_LOST:
		ZFCP_LOG_NORMAL("Unsolicited status notification(s) lost: "
				"adapter %s%s%s%s%s%s%s%s%s\n",
				zfcp_get_busid_by_adapter(adapter),
				(status_buffer->status_subtype &
					FSF_STATUS_READ_SUB_INCOMING_ELS) ?
					", incoming ELS" : "",
				(status_buffer->status_subtype &
					FSF_STATUS_READ_SUB_SENSE_DATA) ?
					", sense data" : "",
				(status_buffer->status_subtype &
					FSF_STATUS_READ_SUB_LINK_STATUS) ?
					", link status change" : "",
				(status_buffer->status_subtype &
					FSF_STATUS_READ_SUB_PORT_CLOSED) ?
					", port close" : "",
				(status_buffer->status_subtype &
					FSF_STATUS_READ_SUB_BIT_ERROR_THRESHOLD) ?
					", bit error exception" : "",
				(status_buffer->status_subtype &
					FSF_STATUS_READ_SUB_ACT_UPDATED) ?
					", ACT update" : "",
				(status_buffer->status_subtype &
					FSF_STATUS_READ_SUB_ACT_HARDENED) ?
					", ACT hardening" : "",
				(status_buffer->status_subtype &
					FSF_STATUS_READ_SUB_FEATURE_UPDATE_ALERT) ?
					", adapter feature change" : "");

		if (status_buffer->status_subtype &
		    FSF_STATUS_READ_SUB_ACT_UPDATED)
			zfcp_erp_adapter_access_changed(adapter);
		break;

	case FSF_STATUS_READ_CFDC_UPDATED:
		ZFCP_LOG_NORMAL("CFDC has been updated on the adapter %s\n",
			      zfcp_get_busid_by_adapter(adapter));
		zfcp_erp_adapter_access_changed(adapter);
		break;

	case FSF_STATUS_READ_CFDC_HARDENED:
		switch (status_buffer->status_subtype) {
		case FSF_STATUS_READ_SUB_CFDC_HARDENED_ON_SE:
			ZFCP_LOG_NORMAL("CFDC of adapter %s saved on SE\n",
				      zfcp_get_busid_by_adapter(adapter));
			break;
		case FSF_STATUS_READ_SUB_CFDC_HARDENED_ON_SE2:
			ZFCP_LOG_NORMAL("CFDC of adapter %s has been copied "
				      "to the secondary SE\n",
				zfcp_get_busid_by_adapter(adapter));
			break;
		default:
			ZFCP_LOG_NORMAL("CFDC of adapter %s has been hardened\n",
				      zfcp_get_busid_by_adapter(adapter));
		}
		break;

	case FSF_STATUS_READ_FEATURE_UPDATE_ALERT:
		debug_text_event(adapter->erp_dbf, 2, "unsol_features:");
		ZFCP_LOG_INFO("List of supported features on adapter %s has "
			      "been changed from 0x%08X to 0x%08X\n",
			      zfcp_get_busid_by_adapter(adapter),
			      *(u32*) (status_buffer->payload + 4),
			      *(u32*) (status_buffer->payload));
		adapter->adapter_features = *(u32*) status_buffer->payload;
		break;

	default:
		ZFCP_LOG_NORMAL("warning: An unsolicited status packet of unknown "
				"type was received (debug info 0x%x)\n",
				status_buffer->status_type);
		ZFCP_LOG_DEBUG("Dump of status_read_buffer %p:\n",
			       status_buffer);
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) status_buffer,
			      sizeof (struct fsf_status_read_buffer));
		break;
	}
	mempool_free(status_buffer, adapter->pool.data_status_read);
	zfcp_fsf_req_free(fsf_req);
	/*
	 * recycle buffer and start new request repeat until outbound
	 * queue is empty or adapter shutdown is requested
	 */
	/*
	 * FIXME(qdio):
	 * we may wait in the req_create for 5s during shutdown, so
	 * qdio_cleanup will have to wait at least that long before returning
	 * with failure to allow us a proper cleanup under all circumstances
	 */
	/*
	 * FIXME:
	 * allocation failure possible? (Is this code needed?)
	 */
	retval = zfcp_fsf_status_read(adapter, 0);
	if (retval < 0) {
		ZFCP_LOG_INFO("Failed to create unsolicited status read "
			      "request for the adapter %s.\n",
			      zfcp_get_busid_by_adapter(adapter));
		/* temporary fix to avoid status read buffer shortage */
		adapter->status_read_failed++;
		if ((ZFCP_STATUS_READS_RECOM - adapter->status_read_failed)
		    < ZFCP_STATUS_READ_FAILED_THRESHOLD) {
			ZFCP_LOG_INFO("restart adapter %s due to status read "
				      "buffer shortage\n",
				      zfcp_get_busid_by_adapter(adapter));
			zfcp_erp_adapter_reopen(adapter, 0);
		}
	}
 out:
	return retval;
}

/*
 * function:    zfcp_fsf_abort_fcp_command
 *
 * purpose:	tells FSF to abort a running SCSI command
 *
 * returns:	address of initiated FSF request
 *		NULL - request could not be initiated
 *
 * FIXME(design): should be watched by a timeout !!! 
 * FIXME(design) shouldn't this be modified to return an int
 *               also...don't know how though
 */
struct zfcp_fsf_req *
zfcp_fsf_abort_fcp_command(unsigned long old_req_id,
			   struct zfcp_adapter *adapter,
			   struct zfcp_unit *unit, int req_flags)
{
	volatile struct qdio_buffer_element *sbale;
	unsigned long lock_flags;
	struct zfcp_fsf_req *fsf_req = NULL;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_ABORT_FCP_CMND,
				     req_flags, adapter->pool.fsf_req_abort,
				     &lock_flags, &fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Failed to create an abort command "
			      "request for lun 0x%016Lx on port 0x%016Lx "
			      "on adapter %s.\n",
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_adapter(adapter));
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	fsf_req->data = (unsigned long) unit;

	/* set handles of unit and its parent port in QTCB */
	fsf_req->qtcb->header.lun_handle = unit->handle;
	fsf_req->qtcb->header.port_handle = unit->port->handle;

	/* set handle of request which should be aborted */
	fsf_req->qtcb->bottom.support.req_handle = (u64) old_req_id;

	/* start QDIO request for this FSF request */

	zfcp_fsf_start_scsi_er_timer(adapter);
	retval = zfcp_fsf_req_send(fsf_req, NULL);
	if (retval) {
		del_timer(&adapter->scsi_er_timer);
		ZFCP_LOG_INFO("error: Failed to send abort command request "
			      "on adapter %s, port 0x%016Lx, unit 0x%016Lx\n",
			      zfcp_get_busid_by_adapter(adapter),
			      unit->port->wwpn, unit->fcp_lun);
		zfcp_fsf_req_free(fsf_req);
		fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_DEBUG("Abort FCP Command request initiated "
		       "(adapter%s, port d_id=0x%08x, "
		       "unit x%016Lx, old_req_id=0x%lx)\n",
		       zfcp_get_busid_by_adapter(adapter),
		       unit->port->d_id,
		       unit->fcp_lun, old_req_id);
 out:
	write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);
	return fsf_req;
}

/*
 * function:    zfcp_fsf_abort_fcp_command_handler
 *
 * purpose:	is called for finished Abort FCP Command request
 *
 * returns:	
 */
static int
zfcp_fsf_abort_fcp_command_handler(struct zfcp_fsf_req *new_fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_unit *unit;
	unsigned char status_qual =
	    new_fsf_req->qtcb->header.fsf_status_qual.word[0];

	del_timer(&new_fsf_req->adapter->scsi_er_timer);

	if (new_fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* do not set ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED */
		goto skip_fsfstatus;
	}

	unit = (struct zfcp_unit *) new_fsf_req->data;

	/* evaluate FSF status in QTCB */
	switch (new_fsf_req->qtcb->header.fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		if (status_qual >> 4 != status_qual % 0xf) {
			debug_text_event(new_fsf_req->adapter->erp_dbf, 3,
					 "fsf_s_phand_nv0");
			/*
			 * In this case a command that was sent prior to a port
			 * reopen was aborted (handles are different). This is
			 * fine.
			 */
		} else {
			ZFCP_LOG_INFO("Temporary port identifier 0x%x for "
				      "port 0x%016Lx on adapter %s invalid. "
				      "This may happen occasionally.\n",
				      unit->port->handle,
				      unit->port->wwpn,
				      zfcp_get_busid_by_unit(unit));
			ZFCP_LOG_INFO("status qualifier:\n");
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_INFO,
				      (char *) &new_fsf_req->qtcb->header.
				      fsf_status_qual,
				      sizeof (union fsf_status_qual));
			/* Let's hope this sorts out the mess */
			debug_text_event(new_fsf_req->adapter->erp_dbf, 1,
					 "fsf_s_phand_nv1");
			zfcp_erp_adapter_reopen(unit->port->adapter, 0);
			new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		}
		break;

	case FSF_LUN_HANDLE_NOT_VALID:
		if (status_qual >> 4 != status_qual % 0xf) {
			/* 2 */
			debug_text_event(new_fsf_req->adapter->erp_dbf, 3,
					 "fsf_s_lhand_nv0");
			/*
			 * In this case a command that was sent prior to a unit
			 * reopen was aborted (handles are different).
			 * This is fine.
			 */
		} else {
			ZFCP_LOG_INFO
			    ("Warning: Temporary LUN identifier 0x%x of LUN "
			     "0x%016Lx on port 0x%016Lx on adapter %s is "
			     "invalid. This may happen in rare cases. "
			     "Trying to re-establish link.\n",
			     unit->handle,
			     unit->fcp_lun,
			     unit->port->wwpn,
			     zfcp_get_busid_by_unit(unit));
			ZFCP_LOG_DEBUG("Status qualifier data:\n");
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
				      (char *) &new_fsf_req->qtcb->header.
				      fsf_status_qual,
				      sizeof (union fsf_status_qual));
			/* Let's hope this sorts out the mess */
			debug_text_event(new_fsf_req->adapter->erp_dbf, 1,
					 "fsf_s_lhand_nv1");
			zfcp_erp_port_reopen(unit->port, 0);
			new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		}
		break;

	case FSF_FCP_COMMAND_DOES_NOT_EXIST:
		retval = 0;
		debug_text_event(new_fsf_req->adapter->erp_dbf, 3,
				 "fsf_s_no_exist");
		new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED;
		break;

	case FSF_PORT_BOXED:
		ZFCP_LOG_INFO("Remote port 0x%016Lx on adapter %s needs to "
			      "be reopened\n", unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit));
		debug_text_event(new_fsf_req->adapter->erp_dbf, 2,
				 "fsf_s_pboxed");
		zfcp_erp_port_boxed(unit->port);
		new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
		    | ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_LUN_BOXED:
                ZFCP_LOG_INFO(
                        "unit 0x%016Lx on port 0x%016Lx on adapter %s needs "
                        "to be reopened\n",
                        unit->fcp_lun, unit->port->wwpn,
                        zfcp_get_busid_by_unit(unit));
                debug_text_event(new_fsf_req->adapter->erp_dbf, 1, "fsf_s_lboxed");
		zfcp_erp_unit_boxed(unit);
                new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
                        | ZFCP_STATUS_FSFREQ_RETRY;
                break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (new_fsf_req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			debug_text_event(new_fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
			zfcp_test_link(unit->port);
			new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* SCSI stack will escalate */
			debug_text_event(new_fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			ZFCP_LOG_NORMAL
			    ("bug: Wrong status qualifier 0x%x arrived.\n",
			     new_fsf_req->qtcb->header.fsf_status_qual.word[0]);
			debug_text_event(new_fsf_req->adapter->erp_dbf, 0,
					 "fsf_sq_inval:");
			debug_exception(new_fsf_req->adapter->erp_dbf, 0,
					&new_fsf_req->qtcb->header.
					fsf_status_qual.word[0], sizeof (u32));
			break;
		}
		break;

	case FSF_GOOD:
		retval = 0;
		new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				new_fsf_req->qtcb->header.fsf_status);
		debug_text_event(new_fsf_req->adapter->erp_dbf, 0,
				 "fsf_s_inval:");
		debug_exception(new_fsf_req->adapter->erp_dbf, 0,
				&new_fsf_req->qtcb->header.fsf_status,
				sizeof (u32));
		break;
	}
 skip_fsfstatus:
	return retval;
}

/**
 * zfcp_use_one_sbal - checks whether req buffer and resp bother each fit into
 *	one SBALE
 * Two scatter-gather lists are passed, one for the reqeust and one for the
 * response.
 */
static inline int
zfcp_use_one_sbal(struct scatterlist *req, int req_count,
                  struct scatterlist *resp, int resp_count)
{
        return ((req_count == 1) &&
		(resp_count == 1) &&
                (((unsigned long) zfcp_sg_to_address(&req[0]) &
		  PAGE_MASK) ==
		 ((unsigned long) (zfcp_sg_to_address(&req[0]) +
				   req[0].length - 1) & PAGE_MASK)) &&
                (((unsigned long) zfcp_sg_to_address(&resp[0]) &
		  PAGE_MASK) ==
                 ((unsigned long) (zfcp_sg_to_address(&resp[0]) +
				   resp[0].length - 1) & PAGE_MASK)));
}

/**
 * zfcp_fsf_send_ct - initiate a Generic Service request (FC-GS)
 * @ct: pointer to struct zfcp_send_ct which conatins all needed data for
 *	the request
 * @pool: pointer to memory pool, if non-null this pool is used to allocate
 *	a struct zfcp_fsf_req
 * @erp_action: pointer to erp_action, if non-null the Generic Service request
 *	is sent within error recovery
 */
int
zfcp_fsf_send_ct(struct zfcp_send_ct *ct, mempool_t *pool,
		 struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_port *port;
	struct zfcp_adapter *adapter;
        struct zfcp_fsf_req *fsf_req;
        unsigned long lock_flags;
        int bytes;
	int ret = 0;

	port = ct->port;
	adapter = port->adapter;

	ret = zfcp_fsf_req_create(adapter, FSF_QTCB_SEND_GENERIC,
				  ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				  pool, &lock_flags, &fsf_req);
	if (ret < 0) {
                ZFCP_LOG_INFO("error: Could not create CT request (FC-GS) for "
			      "adapter: %s\n",
			      zfcp_get_busid_by_adapter(adapter));
		goto failed_req;
	}

        if (erp_action != NULL) {
                erp_action->fsf_req = fsf_req;
                fsf_req->erp_action = erp_action;
        }

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
        if (zfcp_use_one_sbal(ct->req, ct->req_count,
                              ct->resp, ct->resp_count)){
                /* both request buffer and response buffer
                   fit into one sbale each */
                sbale[0].flags |= SBAL_FLAGS0_TYPE_WRITE_READ;
                sbale[2].addr = zfcp_sg_to_address(&ct->req[0]);
                sbale[2].length = ct->req[0].length;
                sbale[3].addr = zfcp_sg_to_address(&ct->resp[0]);
                sbale[3].length = ct->resp[0].length;
                sbale[3].flags |= SBAL_FLAGS_LAST_ENTRY;
	} else if (adapter->adapter_features &
                   FSF_FEATURE_ELS_CT_CHAINED_SBALS) {
                /* try to use chained SBALs */
                bytes = zfcp_qdio_sbals_from_sg(fsf_req,
                                                SBAL_FLAGS0_TYPE_WRITE_READ,
                                                ct->req, ct->req_count,
                                                ZFCP_MAX_SBALS_PER_CT_REQ);
                if (bytes <= 0) {
                        ZFCP_LOG_INFO("error: creation of CT request failed "
				      "on adapter %s\n",
				      zfcp_get_busid_by_adapter(adapter));
                        if (bytes == 0)
                                ret = -ENOMEM;
                        else
                                ret = bytes;

                        goto failed_send;
                }
                fsf_req->qtcb->bottom.support.req_buf_length = bytes;
                fsf_req->sbale_curr = ZFCP_LAST_SBALE_PER_SBAL;
                bytes = zfcp_qdio_sbals_from_sg(fsf_req,
                                                SBAL_FLAGS0_TYPE_WRITE_READ,
                                                ct->resp, ct->resp_count,
                                                ZFCP_MAX_SBALS_PER_CT_REQ);
                if (bytes <= 0) {
                        ZFCP_LOG_INFO("error: creation of CT request failed "
				      "on adapter %s\n",
				      zfcp_get_busid_by_adapter(adapter));
                        if (bytes == 0)
                                ret = -ENOMEM;
                        else
                                ret = bytes;

                        goto failed_send;
                }
                fsf_req->qtcb->bottom.support.resp_buf_length = bytes;
        } else {
                /* reject send generic request */
		ZFCP_LOG_INFO(
			"error: microcode does not support chained SBALs,"
                        "CT request too big (adapter %s)\n",
			zfcp_get_busid_by_adapter(adapter));
                ret = -EOPNOTSUPP;
                goto failed_send;
        }

	/* settings in QTCB */
	fsf_req->qtcb->header.port_handle = port->handle;
	fsf_req->qtcb->bottom.support.service_class =
		ZFCP_FC_SERVICE_CLASS_DEFAULT;
	fsf_req->qtcb->bottom.support.timeout = ct->timeout;
        fsf_req->data = (unsigned long) ct;

	zfcp_san_dbf_event_ct_request(fsf_req);

	/* start QDIO request for this FSF request */
	ret = zfcp_fsf_req_send(fsf_req, ct->timer);
	if (ret) {
		ZFCP_LOG_DEBUG("error: initiation of CT request failed "
			       "(adapter %s, port 0x%016Lx)\n",
			       zfcp_get_busid_by_adapter(adapter), port->wwpn);
		goto failed_send;
	}

	ZFCP_LOG_DEBUG("CT request initiated (adapter %s, port 0x%016Lx)\n",
		       zfcp_get_busid_by_adapter(adapter), port->wwpn);
	goto out;

 failed_send:
	zfcp_fsf_req_free(fsf_req);
        if (erp_action != NULL) {
                erp_action->fsf_req = NULL;
        }
 failed_req:
 out:
        write_unlock_irqrestore(&adapter->request_queue.queue_lock,
				lock_flags);
	return ret;
}

/**
 * zfcp_fsf_send_ct_handler - handler for Generic Service requests
 * @fsf_req: pointer to struct zfcp_fsf_req
 *
 * Data specific for the Generic Service request is passed using
 * fsf_req->data. There we find the pointer to struct zfcp_send_ct.
 * Usually a specific handler for the CT request is called which is
 * found in this structure.
 */
static int
zfcp_fsf_send_ct_handler(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_port *port;
	struct zfcp_adapter *adapter;
	struct zfcp_send_ct *send_ct;
	struct fsf_qtcb_header *header;
	struct fsf_qtcb_bottom_support *bottom;
	int retval = -EINVAL;
	u16 subtable, rule, counter;

	adapter = fsf_req->adapter;
	send_ct = (struct zfcp_send_ct *) fsf_req->data;
	port = send_ct->port;
	header = &fsf_req->qtcb->header;
	bottom = &fsf_req->qtcb->bottom.support;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto skip_fsfstatus;

	/* evaluate FSF status in QTCB */
	switch (header->fsf_status) {

        case FSF_GOOD:
		zfcp_san_dbf_event_ct_response(fsf_req);
                retval = 0;
		break;

        case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		ZFCP_LOG_INFO("error: adapter %s does not support fc "
			      "class %d.\n",
			      zfcp_get_busid_by_port(port),
			      ZFCP_FC_SERVICE_CLASS_DEFAULT);
		/* stop operation for this adapter */
		debug_text_exception(adapter->erp_dbf, 0, "fsf_s_class_nsup");
		zfcp_erp_adapter_shutdown(adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_ADAPTER_STATUS_AVAILABLE:
                switch (header->fsf_status_qual.word[0]){
                case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			/* reopening link to port */
			debug_text_event(adapter->erp_dbf, 1, "fsf_sq_ltest");
			zfcp_test_link(port);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
                case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* ERP strategy will escalate */
			debug_text_event(adapter->erp_dbf, 1, "fsf_sq_ulp");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
                default:
			ZFCP_LOG_INFO("bug: Wrong status qualifier 0x%x "
				      "arrived.\n",
				      header->fsf_status_qual.word[0]);
			break;
                }
                break;

	case FSF_ACCESS_DENIED:
		ZFCP_LOG_NORMAL("access denied, cannot send generic service "
				"command (adapter %s, port d_id=0x%08x)\n",
				zfcp_get_busid_by_port(port), port->d_id);
		for (counter = 0; counter < 2; counter++) {
			subtable = header->fsf_status_qual.halfword[counter * 2];
			rule = header->fsf_status_qual.halfword[counter * 2 + 1];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
       				ZFCP_LOG_INFO("Access denied (%s rule %d)\n",
					zfcp_act_subtable_type[subtable], rule);
				break;
			}
		}
		debug_text_event(adapter->erp_dbf, 1, "fsf_s_access");
		zfcp_erp_port_access_denied(port);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_GENERIC_COMMAND_REJECTED:
		ZFCP_LOG_INFO("generic service command rejected "
			      "(adapter %s, port d_id=0x%08x)\n",
			      zfcp_get_busid_by_port(port), port->d_id);
		ZFCP_LOG_INFO("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_INFO,
			      (char *) &header->fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(adapter->erp_dbf, 1, "fsf_s_gcom_rej");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_PORT_HANDLE_NOT_VALID:
		ZFCP_LOG_DEBUG("Temporary port identifier 0x%x for port "
			       "0x%016Lx on adapter %s invalid. This may "
			       "happen occasionally.\n", port->handle,
			       port->wwpn, zfcp_get_busid_by_port(port));
		ZFCP_LOG_INFO("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_INFO,
			      (char *) &header->fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(adapter->erp_dbf, 1, "fsf_s_phandle_nv");
		zfcp_erp_adapter_reopen(adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_PORT_BOXED:
		ZFCP_LOG_INFO("port needs to be reopened "
			      "(adapter %s, port d_id=0x%08x)\n",
			      zfcp_get_busid_by_port(port), port->d_id);
		debug_text_event(adapter->erp_dbf, 2, "fsf_s_pboxed");
		zfcp_erp_port_boxed(port);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
		    | ZFCP_STATUS_FSFREQ_RETRY;
		break;

	/* following states should never occure, all cases avoided
	   in zfcp_fsf_send_ct - but who knows ... */
	case FSF_PAYLOAD_SIZE_MISMATCH:
		ZFCP_LOG_INFO("payload size mismatch (adapter: %s, "
			      "req_buf_length=%d, resp_buf_length=%d)\n",
			      zfcp_get_busid_by_adapter(adapter),
			      bottom->req_buf_length, bottom->resp_buf_length);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_REQUEST_SIZE_TOO_LARGE:
		ZFCP_LOG_INFO("request size too large (adapter: %s, "
			      "req_buf_length=%d)\n",
			      zfcp_get_busid_by_adapter(adapter),
			      bottom->req_buf_length);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_RESPONSE_SIZE_TOO_LARGE:
		ZFCP_LOG_INFO("response size too large (adapter: %s, "
			      "resp_buf_length=%d)\n",
			      zfcp_get_busid_by_adapter(adapter),
			      bottom->resp_buf_length);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SBAL_MISMATCH:
		ZFCP_LOG_INFO("SBAL mismatch (adapter: %s, req_buf_length=%d, "
			      "resp_buf_length=%d)\n",
			      zfcp_get_busid_by_adapter(adapter),
			      bottom->req_buf_length, bottom->resp_buf_length);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

       default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n", header->fsf_status);
		debug_text_event(adapter->erp_dbf, 0, "fsf_sq_inval:");
		debug_exception(adapter->erp_dbf, 0,
				&header->fsf_status_qual.word[0], sizeof (u32));
		break;
	}

skip_fsfstatus:
	send_ct->status = retval;

	if (send_ct->handler != NULL)
		send_ct->handler(send_ct->handler_data);

	return retval;
}

/**
 * zfcp_fsf_send_els - initiate an ELS command (FC-FS)
 * @els: pointer to struct zfcp_send_els which contains all needed data for
 *	the command.
 */
int
zfcp_fsf_send_els(struct zfcp_send_els *els)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *fsf_req;
	u32 d_id;
	struct zfcp_adapter *adapter;
	unsigned long lock_flags;
        int bytes;
	int ret = 0;

	d_id = els->d_id;
	adapter = els->adapter;

        ret = zfcp_fsf_req_create(adapter, FSF_QTCB_SEND_ELS,
				  ZFCP_REQ_AUTO_CLEANUP,
				  NULL, &lock_flags, &fsf_req);
	if (ret < 0) {
                ZFCP_LOG_INFO("error: creation of ELS request failed "
			      "(adapter %s, port d_id: 0x%08x)\n",
                              zfcp_get_busid_by_adapter(adapter), d_id);
                goto failed_req;
	}

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
        if (zfcp_use_one_sbal(els->req, els->req_count,
                              els->resp, els->resp_count)){
                /* both request buffer and response buffer
                   fit into one sbale each */
                sbale[0].flags |= SBAL_FLAGS0_TYPE_WRITE_READ;
                sbale[2].addr = zfcp_sg_to_address(&els->req[0]);
                sbale[2].length = els->req[0].length;
                sbale[3].addr = zfcp_sg_to_address(&els->resp[0]);
                sbale[3].length = els->resp[0].length;
                sbale[3].flags |= SBAL_FLAGS_LAST_ENTRY;
	} else if (adapter->adapter_features &
                   FSF_FEATURE_ELS_CT_CHAINED_SBALS) {
                /* try to use chained SBALs */
                bytes = zfcp_qdio_sbals_from_sg(fsf_req,
                                                SBAL_FLAGS0_TYPE_WRITE_READ,
                                                els->req, els->req_count,
                                                ZFCP_MAX_SBALS_PER_ELS_REQ);
                if (bytes <= 0) {
                        ZFCP_LOG_INFO("error: creation of ELS request failed "
				      "(adapter %s, port d_id: 0x%08x)\n",
				      zfcp_get_busid_by_adapter(adapter), d_id);
                        if (bytes == 0) {
                                ret = -ENOMEM;
                        } else {
                                ret = bytes;
                        }
                        goto failed_send;
                }
                fsf_req->qtcb->bottom.support.req_buf_length = bytes;
                fsf_req->sbale_curr = ZFCP_LAST_SBALE_PER_SBAL;
                bytes = zfcp_qdio_sbals_from_sg(fsf_req,
                                                SBAL_FLAGS0_TYPE_WRITE_READ,
                                                els->resp, els->resp_count,
                                                ZFCP_MAX_SBALS_PER_ELS_REQ);
                if (bytes <= 0) {
                        ZFCP_LOG_INFO("error: creation of ELS request failed "
				      "(adapter %s, port d_id: 0x%08x)\n",
				      zfcp_get_busid_by_adapter(adapter), d_id);
                        if (bytes == 0) {
                                ret = -ENOMEM;
                        } else {
                                ret = bytes;
                        }
                        goto failed_send;
                }
                fsf_req->qtcb->bottom.support.resp_buf_length = bytes;
        } else {
                /* reject request */
		ZFCP_LOG_INFO("error: microcode does not support chained SBALs"
                              ", ELS request too big (adapter %s, "
			      "port d_id: 0x%08x)\n",
			      zfcp_get_busid_by_adapter(adapter), d_id);
                ret = -EOPNOTSUPP;
                goto failed_send;
        }

	/* settings in QTCB */
	fsf_req->qtcb->bottom.support.d_id = d_id;
	fsf_req->qtcb->bottom.support.service_class =
		ZFCP_FC_SERVICE_CLASS_DEFAULT;
	fsf_req->qtcb->bottom.support.timeout = ZFCP_ELS_TIMEOUT;
	fsf_req->data = (unsigned long) els;

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);

	zfcp_san_dbf_event_els_request(fsf_req);

	/* start QDIO request for this FSF request */
	ret = zfcp_fsf_req_send(fsf_req, els->timer);
	if (ret) {
		ZFCP_LOG_DEBUG("error: initiation of ELS request failed "
			       "(adapter %s, port d_id: 0x%08x)\n",
			       zfcp_get_busid_by_adapter(adapter), d_id);
		goto failed_send;
	}

	ZFCP_LOG_DEBUG("ELS request initiated (adapter %s, port d_id: "
		       "0x%08x)\n", zfcp_get_busid_by_adapter(adapter), d_id);
	goto out;

 failed_send:
	zfcp_fsf_req_free(fsf_req);

 failed_req:
 out:
	write_unlock_irqrestore(&adapter->request_queue.queue_lock,
				lock_flags);

        return ret;
}

/**
 * zfcp_fsf_send_els_handler - handler for ELS commands
 * @fsf_req: pointer to struct zfcp_fsf_req
 *
 * Data specific for the ELS command is passed using
 * fsf_req->data. There we find the pointer to struct zfcp_send_els.
 * Usually a specific handler for the ELS command is called which is
 * found in this structure.
 */
static int zfcp_fsf_send_els_handler(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	u32 d_id;
	struct fsf_qtcb_header *header;
	struct fsf_qtcb_bottom_support *bottom;
	struct zfcp_send_els *send_els;
	int retval = -EINVAL;
	u16 subtable, rule, counter;

	send_els = (struct zfcp_send_els *) fsf_req->data;
	adapter = send_els->adapter;
	port = send_els->port;
	d_id = send_els->d_id;
	header = &fsf_req->qtcb->header;
	bottom = &fsf_req->qtcb->bottom.support;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto skip_fsfstatus;

	switch (header->fsf_status) {

	case FSF_GOOD:
		zfcp_san_dbf_event_els_response(fsf_req);
		retval = 0;
		break;

	case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		ZFCP_LOG_INFO("error: adapter %s does not support fc "
			      "class %d.\n",
			      zfcp_get_busid_by_adapter(adapter),
			      ZFCP_FC_SERVICE_CLASS_DEFAULT);
		/* stop operation for this adapter */
		debug_text_exception(adapter->erp_dbf, 0, "fsf_s_class_nsup");
		zfcp_erp_adapter_shutdown(adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]){
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			debug_text_event(adapter->erp_dbf, 1, "fsf_sq_ltest");
			if (port && (send_els->ls_code != ZFCP_LS_ADISC))
				zfcp_test_link(port);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			debug_text_event(adapter->erp_dbf, 1, "fsf_sq_ulp");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			retval =
			  zfcp_handle_els_rjt(header->fsf_status_qual.word[1],
					      (struct zfcp_ls_rjt_par *)
					      &header->fsf_status_qual.word[2]);
			break;
		case FSF_SQ_RETRY_IF_POSSIBLE:
			debug_text_event(adapter->erp_dbf, 1, "fsf_sq_retry");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			ZFCP_LOG_INFO("bug: Wrong status qualifier 0x%x\n",
				      header->fsf_status_qual.word[0]);
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_INFO,
				(char*)header->fsf_status_qual.word, 16);
		}
		break;

	case FSF_ELS_COMMAND_REJECTED:
		ZFCP_LOG_INFO("ELS has been rejected because command filter "
			      "prohibited sending "
			      "(adapter: %s, port d_id: 0x%08x)\n",
			      zfcp_get_busid_by_adapter(adapter), d_id);

		break;

	case FSF_PAYLOAD_SIZE_MISMATCH:
		ZFCP_LOG_INFO(
			"ELS request size and ELS response size must be either "
			"both 0, or both greater than 0 "
			"(adapter: %s, req_buf_length=%d resp_buf_length=%d)\n",
			zfcp_get_busid_by_adapter(adapter),
			bottom->req_buf_length,
			bottom->resp_buf_length);
		break;

	case FSF_REQUEST_SIZE_TOO_LARGE:
		ZFCP_LOG_INFO(
			"Length of the ELS request buffer, "
			"specified in QTCB bottom, "
			"exceeds the size of the buffers "
			"that have been allocated for ELS request data "
			"(adapter: %s, req_buf_length=%d)\n",
			zfcp_get_busid_by_adapter(adapter),
			bottom->req_buf_length);
		break;

	case FSF_RESPONSE_SIZE_TOO_LARGE:
		ZFCP_LOG_INFO(
			"Length of the ELS response buffer, "
			"specified in QTCB bottom, "
			"exceeds the size of the buffers "
			"that have been allocated for ELS response data "
			"(adapter: %s, resp_buf_length=%d)\n",
			zfcp_get_busid_by_adapter(adapter),
			bottom->resp_buf_length);
		break;

	case FSF_SBAL_MISMATCH:
		/* should never occure, avoided in zfcp_fsf_send_els */
		ZFCP_LOG_INFO("SBAL mismatch (adapter: %s, req_buf_length=%d, "
			      "resp_buf_length=%d)\n",
			      zfcp_get_busid_by_adapter(adapter),
			      bottom->req_buf_length, bottom->resp_buf_length);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ACCESS_DENIED:
		ZFCP_LOG_NORMAL("access denied, cannot send ELS command "
				"(adapter %s, port d_id=0x%08x)\n",
				zfcp_get_busid_by_adapter(adapter), d_id);
		for (counter = 0; counter < 2; counter++) {
			subtable = header->fsf_status_qual.halfword[counter * 2];
			rule = header->fsf_status_qual.halfword[counter * 2 + 1];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
				ZFCP_LOG_INFO("Access denied (%s rule %d)\n",
					zfcp_act_subtable_type[subtable], rule);
				break;
			}
		}
		debug_text_event(adapter->erp_dbf, 1, "fsf_s_access");
		if (port != NULL)
			zfcp_erp_port_access_denied(port);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	default:
		ZFCP_LOG_NORMAL(
			"bug: An unknown FSF Status was presented "
			"(adapter: %s, fsf_status=0x%08x)\n",
			zfcp_get_busid_by_adapter(adapter),
			header->fsf_status);
		debug_text_event(adapter->erp_dbf, 0, "fsf_sq_inval");
		debug_exception(adapter->erp_dbf, 0,
			&header->fsf_status_qual.word[0], sizeof(u32));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}

skip_fsfstatus:
	send_els->status = retval;

	if (send_els->handler != 0)
		send_els->handler(send_els->handler_data);

	return retval;
}

int
zfcp_fsf_exchange_config_data(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	unsigned long lock_flags;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_EXCHANGE_CONFIG_DATA,
				     ZFCP_REQ_AUTO_CLEANUP,
				     erp_action->adapter->pool.fsf_req_erp,
				     &lock_flags, &(erp_action->fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Could not create exchange configuration "
			      "data request for adapter %s.\n",
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(erp_action->fsf_req,
                                    erp_action->fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	erp_action->fsf_req->erp_action = erp_action;
	erp_action->fsf_req->qtcb->bottom.config.feature_selection =
			FSF_FEATURE_CFDC |
			FSF_FEATURE_LUN_SHARING |
			FSF_FEATURE_NOTIFICATION_LOST |
			FSF_FEATURE_UPDATE_ALERT;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO
		    ("error: Could not send exchange configuration data "
		     "command on the adapter %s\n",
		     zfcp_get_busid_by_adapter(erp_action->adapter));
		zfcp_fsf_req_free(erp_action->fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_DEBUG("exchange configuration data request initiated "
		       "(adapter %s)\n",
		       zfcp_get_busid_by_adapter(erp_action->adapter));

 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/**
 * zfcp_fsf_exchange_config_evaluate
 * @fsf_req: fsf_req which belongs to xchg config data request
 * @xchg_ok: specifies if xchg config data was incomplete or complete (0/1)
 *
 * returns: -EIO on error, 0 otherwise
 */
static int
zfcp_fsf_exchange_config_evaluate(struct zfcp_fsf_req *fsf_req, int xchg_ok)
{
	struct fsf_qtcb_bottom_config *bottom;
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct Scsi_Host *shost = adapter->scsi_host;

	bottom = &fsf_req->qtcb->bottom.config;
	ZFCP_LOG_DEBUG("low/high QTCB version 0x%x/0x%x of FSF\n",
		       bottom->low_qtcb_version, bottom->high_qtcb_version);
	adapter->fsf_lic_version = bottom->lic_version;
	adapter->adapter_features = bottom->adapter_features;
	adapter->connection_features = bottom->connection_features;
	adapter->peer_wwpn = 0;
	adapter->peer_wwnn = 0;
	adapter->peer_d_id = 0;

	if (xchg_ok) {
		fc_host_node_name(shost) = bottom->nport_serv_param.wwnn;
		fc_host_port_name(shost) = bottom->nport_serv_param.wwpn;
		fc_host_port_id(shost) = bottom->s_id & ZFCP_DID_MASK;
		fc_host_speed(shost) = bottom->fc_link_speed;
		fc_host_supported_classes(shost) = FC_COS_CLASS2 | FC_COS_CLASS3;
		adapter->hydra_version = bottom->adapter_type;
		if (fc_host_permanent_port_name(shost) == -1)
			fc_host_permanent_port_name(shost) =
				fc_host_port_name(shost);
		if (bottom->fc_topology == FSF_TOPO_P2P) {
			adapter->peer_d_id = bottom->peer_d_id & ZFCP_DID_MASK;
			adapter->peer_wwpn = bottom->plogi_payload.wwpn;
			adapter->peer_wwnn = bottom->plogi_payload.wwnn;
			fc_host_port_type(shost) = FC_PORTTYPE_PTP;
		} else if (bottom->fc_topology == FSF_TOPO_FABRIC)
			fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
		else if (bottom->fc_topology == FSF_TOPO_AL)
			fc_host_port_type(shost) = FC_PORTTYPE_NLPORT;
		else
			fc_host_port_type(shost) = FC_PORTTYPE_UNKNOWN;
	} else {
		fc_host_node_name(shost) = 0;
		fc_host_port_name(shost) = 0;
		fc_host_port_id(shost) = 0;
		fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
		fc_host_port_type(shost) = FC_PORTTYPE_UNKNOWN;
		adapter->hydra_version = 0;
	}

	if (adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT) {
		adapter->hardware_version = bottom->hardware_version;
		memcpy(fc_host_serial_number(shost), bottom->serial_number,
		       min(FC_SERIAL_NUMBER_SIZE, 17));
		EBCASC(fc_host_serial_number(shost),
		       min(FC_SERIAL_NUMBER_SIZE, 17));
	}

	ZFCP_LOG_NORMAL("The adapter %s reported the following characteristics:\n"
			"WWNN 0x%016Lx, "
			"WWPN 0x%016Lx, "
			"S_ID 0x%08x,\n"
			"adapter version 0x%x, "
			"LIC version 0x%x, "
			"FC link speed %d Gb/s\n",
			zfcp_get_busid_by_adapter(adapter),
			(wwn_t) fc_host_node_name(shost),
			(wwn_t) fc_host_port_name(shost),
			fc_host_port_id(shost),
			adapter->hydra_version,
			adapter->fsf_lic_version,
			fc_host_speed(shost));
	if (ZFCP_QTCB_VERSION < bottom->low_qtcb_version) {
		ZFCP_LOG_NORMAL("error: the adapter %s "
				"only supports newer control block "
				"versions in comparison to this device "
				"driver (try updated device driver)\n",
				zfcp_get_busid_by_adapter(adapter));
		debug_text_event(adapter->erp_dbf, 0, "low_qtcb_ver");
		zfcp_erp_adapter_shutdown(adapter, 0);
		return -EIO;
	}
	if (ZFCP_QTCB_VERSION > bottom->high_qtcb_version) {
		ZFCP_LOG_NORMAL("error: the adapter %s "
				"only supports older control block "
				"versions than this device driver uses"
				"(consider a microcode upgrade)\n",
				zfcp_get_busid_by_adapter(adapter));
		debug_text_event(adapter->erp_dbf, 0, "high_qtcb_ver");
		zfcp_erp_adapter_shutdown(adapter, 0);
		return -EIO;
	}
	return 0;
}

/*
 * function:    zfcp_fsf_exchange_config_data_handler
 *
 * purpose:     is called for finished Exchange Configuration Data command
 *
 * returns:
 */
static int
zfcp_fsf_exchange_config_data_handler(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_qtcb_bottom_config *bottom;
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct fsf_qtcb *qtcb = fsf_req->qtcb;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return -EIO;

	switch (qtcb->header.fsf_status) {

	case FSF_GOOD:
		if (zfcp_fsf_exchange_config_evaluate(fsf_req, 1))
			return -EIO;

		switch (fc_host_port_type(adapter->scsi_host)) {
		case FC_PORTTYPE_PTP:
			ZFCP_LOG_NORMAL("Point-to-Point fibrechannel "
					"configuration detected at adapter %s\n"
					"Peer WWNN 0x%016llx, "
					"peer WWPN 0x%016llx, "
					"peer d_id 0x%06x\n",
					zfcp_get_busid_by_adapter(adapter),
					adapter->peer_wwnn,
					adapter->peer_wwpn,
					adapter->peer_d_id);
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "top-p-to-p");
			break;
		case FC_PORTTYPE_NLPORT:
			ZFCP_LOG_NORMAL("error: Arbitrated loop fibrechannel "
					"topology detected at adapter %s "
					"unsupported, shutting down adapter\n",
					zfcp_get_busid_by_adapter(adapter));
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "top-al");
			zfcp_erp_adapter_shutdown(adapter, 0);
			return -EIO;
		case FC_PORTTYPE_NPORT:
			ZFCP_LOG_NORMAL("Switched fabric fibrechannel "
				      "network detected at adapter %s.\n",
				      zfcp_get_busid_by_adapter(adapter));
			break;
		default:
			ZFCP_LOG_NORMAL("bug: The fibrechannel topology "
					"reported by the exchange "
					"configuration command for "
					"the adapter %s is not "
					"of a type known to the zfcp "
					"driver, shutting down adapter\n",
					zfcp_get_busid_by_adapter(adapter));
			debug_text_exception(fsf_req->adapter->erp_dbf, 0,
					     "unknown-topo");
			zfcp_erp_adapter_shutdown(adapter, 0);
			return -EIO;
		}
		bottom = &qtcb->bottom.config;
		if (bottom->max_qtcb_size < sizeof(struct fsf_qtcb)) {
			ZFCP_LOG_NORMAL("bug: Maximum QTCB size (%d bytes) "
					"allowed by the adapter %s "
					"is lower than the minimum "
					"required by the driver (%ld bytes).\n",
					bottom->max_qtcb_size,
					zfcp_get_busid_by_adapter(adapter),
					sizeof(struct fsf_qtcb));
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "qtcb-size");
			debug_event(fsf_req->adapter->erp_dbf, 0,
				    &bottom->max_qtcb_size, sizeof (u32));
			zfcp_erp_adapter_shutdown(adapter, 0);
			return -EIO;
		}
		atomic_set_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK,
				&adapter->status);
		break;
	case FSF_EXCHANGE_CONFIG_DATA_INCOMPLETE:
		debug_text_event(adapter->erp_dbf, 0, "xchg-inco");

		if (zfcp_fsf_exchange_config_evaluate(fsf_req, 0))
			return -EIO;

		atomic_set_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK, &adapter->status);

		zfcp_fsf_link_down_info_eval(adapter,
			&qtcb->header.fsf_status_qual.link_down_info);
		break;
	default:
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf-stat-ng");
		debug_event(fsf_req->adapter->erp_dbf, 0,
			    &fsf_req->qtcb->header.fsf_status, sizeof (u32));
		zfcp_erp_adapter_shutdown(adapter, 0);
		return -EIO;
	}
	return 0;
}

/**
 * zfcp_fsf_exchange_port_data - request information about local port
 * @erp_action: ERP action for the adapter for which port data is requested
 * @adapter: for which port data is requested
 * @data: response to exchange port data request
 */
int
zfcp_fsf_exchange_port_data(struct zfcp_erp_action *erp_action,
			    struct zfcp_adapter *adapter,
			    struct fsf_qtcb_bottom_port *data)
{
	volatile struct qdio_buffer_element *sbale;
	int retval = 0;
	unsigned long lock_flags;
        struct zfcp_fsf_req *fsf_req;
	struct timer_list *timer;

	if (!(adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT)) {
		ZFCP_LOG_INFO("error: exchange port data "
                              "command not supported by adapter %s\n",
			      zfcp_get_busid_by_adapter(adapter));
                return -EOPNOTSUPP;
        }

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_EXCHANGE_PORT_DATA,
				     erp_action ? ZFCP_REQ_AUTO_CLEANUP : 0,
				     0, &lock_flags, &fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of resources. Could not create an "
                              "exchange port data request for"
                              "the adapter %s.\n",
			      zfcp_get_busid_by_adapter(adapter));
		write_unlock_irqrestore(&adapter->request_queue.queue_lock,
					lock_flags);
		return retval;
	}

	if (data)
		fsf_req->data = (unsigned long) data;

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	if (erp_action) {
		erp_action->fsf_req = fsf_req;
		fsf_req->erp_action = erp_action;
		timer = &erp_action->timer;
	} else {
		timer = kmalloc(sizeof(struct timer_list), GFP_ATOMIC);
		if (!timer) {
			write_unlock_irqrestore(&adapter->request_queue.queue_lock,
						lock_flags);
			zfcp_fsf_req_free(fsf_req);
			return -ENOMEM;
		}
		init_timer(timer);
		timer->function = zfcp_fsf_request_timeout_handler;
		timer->data = (unsigned long) adapter;
		timer->expires = ZFCP_FSF_REQUEST_TIMEOUT;
	}

	retval = zfcp_fsf_req_send(fsf_req, timer);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send an exchange port data "
                              "command on the adapter %s\n",
			      zfcp_get_busid_by_adapter(adapter));
		zfcp_fsf_req_free(fsf_req);
		if (erp_action)
			erp_action->fsf_req = NULL;
		else
			kfree(timer);
		write_unlock_irqrestore(&adapter->request_queue.queue_lock,
					lock_flags);
		return retval;
	}

	write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);

	if (!erp_action) {
		wait_event(fsf_req->completion_wq,
			   fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);
		del_timer_sync(timer);
		zfcp_fsf_req_free(fsf_req);
		kfree(timer);
	}
	return retval;
}

/**
 * zfcp_fsf_exchange_port_evaluate
 * @fsf_req: fsf_req which belongs to xchg port data request
 * @xchg_ok: specifies if xchg port data was incomplete or complete (0/1)
 */
static void
zfcp_fsf_exchange_port_evaluate(struct zfcp_fsf_req *fsf_req, int xchg_ok)
{
	struct zfcp_adapter *adapter;
	struct fsf_qtcb *qtcb;
	struct fsf_qtcb_bottom_port *bottom, *data;
	struct Scsi_Host *shost;

	adapter = fsf_req->adapter;
	qtcb = fsf_req->qtcb;
	bottom = &qtcb->bottom.port;
	shost = adapter->scsi_host;

	data = (struct fsf_qtcb_bottom_port*) fsf_req->data;
	if (data)
		memcpy(data, bottom, sizeof(struct fsf_qtcb_bottom_port));

	if (adapter->connection_features & FSF_FEATURE_NPIV_MODE)
		fc_host_permanent_port_name(shost) = bottom->wwpn;
	else
		fc_host_permanent_port_name(shost) = fc_host_port_name(shost);
	fc_host_maxframe_size(shost) = bottom->maximum_frame_size;
	fc_host_supported_speeds(shost) = bottom->supported_speed;
}

/**
 * zfcp_fsf_exchange_port_data_handler - handler for exchange_port_data request
 * @fsf_req: pointer to struct zfcp_fsf_req
 */
static void
zfcp_fsf_exchange_port_data_handler(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_adapter *adapter;
	struct fsf_qtcb *qtcb;

	adapter = fsf_req->adapter;
	qtcb = fsf_req->qtcb;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR)
		return;

	switch (qtcb->header.fsf_status) {
        case FSF_GOOD:
		zfcp_fsf_exchange_port_evaluate(fsf_req, 1);
		atomic_set_mask(ZFCP_STATUS_ADAPTER_XPORT_OK, &adapter->status);
		break;
	case FSF_EXCHANGE_CONFIG_DATA_INCOMPLETE:
		zfcp_fsf_exchange_port_evaluate(fsf_req, 0);
		atomic_set_mask(ZFCP_STATUS_ADAPTER_XPORT_OK, &adapter->status);
		zfcp_fsf_link_down_info_eval(adapter,
			&qtcb->header.fsf_status_qual.link_down_info);
                break;
        default:
		debug_text_event(adapter->erp_dbf, 0, "xchg-port-ng");
		debug_event(adapter->erp_dbf, 0,
			    &fsf_req->qtcb->header.fsf_status, sizeof(u32));
	}
}


/*
 * function:    zfcp_fsf_open_port
 *
 * purpose:	
 *
 * returns:	address of initiated FSF request
 *		NULL - request could not be initiated 
 */
int
zfcp_fsf_open_port(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	unsigned long lock_flags;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_OPEN_PORT_WITH_DID,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     erp_action->adapter->pool.fsf_req_erp,
				     &lock_flags, &(erp_action->fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Could not create open port request "
			      "for port 0x%016Lx on adapter %s.\n",
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(erp_action->fsf_req,
                                    erp_action->fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	erp_action->fsf_req->qtcb->bottom.support.d_id = erp_action->port->d_id;
	atomic_set_mask(ZFCP_STATUS_COMMON_OPENING, &erp_action->port->status);
	erp_action->fsf_req->data = (unsigned long) erp_action->port;
	erp_action->fsf_req->erp_action = erp_action;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send open port request for "
			      "port 0x%016Lx on adapter %s.\n",
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		zfcp_fsf_req_free(erp_action->fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_DEBUG("open port request initiated "
		       "(adapter %s,  port 0x%016Lx)\n",
		       zfcp_get_busid_by_adapter(erp_action->adapter),
		       erp_action->port->wwpn);
 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/*
 * function:    zfcp_fsf_open_port_handler
 *
 * purpose:	is called for finished Open Port command
 *
 * returns:	
 */
static int
zfcp_fsf_open_port_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_port *port;
	struct fsf_plogi *plogi;
	struct fsf_qtcb_header *header;
	u16 subtable, rule, counter;

	port = (struct zfcp_port *) fsf_req->data;
	header = &fsf_req->qtcb->header;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change port status in our bookkeeping */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (header->fsf_status) {

	case FSF_PORT_ALREADY_OPEN:
		ZFCP_LOG_NORMAL("bug: remote port 0x%016Lx on adapter %s "
				"is already open.\n",
				port->wwpn, zfcp_get_busid_by_port(port));
		debug_text_exception(fsf_req->adapter->erp_dbf, 0,
				     "fsf_s_popen");
		/*
		 * This is a bug, however operation should continue normally
		 * if it is simply ignored
		 */
		break;

	case FSF_ACCESS_DENIED:
		ZFCP_LOG_NORMAL("Access denied, cannot open port 0x%016Lx "
				"on adapter %s\n",
				port->wwpn, zfcp_get_busid_by_port(port));
		for (counter = 0; counter < 2; counter++) {
			subtable = header->fsf_status_qual.halfword[counter * 2];
			rule = header->fsf_status_qual.halfword[counter * 2 + 1];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
				ZFCP_LOG_INFO("Access denied (%s rule %d)\n",
					zfcp_act_subtable_type[subtable], rule);
				break;
			}
		}
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_access");
		zfcp_erp_port_access_denied(port);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED:
		ZFCP_LOG_INFO("error: The FSF adapter is out of resources. "
			      "The remote port 0x%016Lx on adapter %s "
			      "could not be opened. Disabling it.\n",
			      port->wwpn, zfcp_get_busid_by_port(port));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_max_ports");
		zfcp_erp_port_failed(port);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
			/* ERP strategy will escalate */
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* ERP strategy will escalate */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_NO_RETRY_POSSIBLE:
			ZFCP_LOG_NORMAL("The remote port 0x%016Lx on "
					"adapter %s could not be opened. "
					"Disabling it.\n",
					port->wwpn,
					zfcp_get_busid_by_port(port));
			debug_text_exception(fsf_req->adapter->erp_dbf, 0,
					     "fsf_sq_no_retry");
			zfcp_erp_port_failed(port);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			ZFCP_LOG_NORMAL
			    ("bug: Wrong status qualifier 0x%x arrived.\n",
			     header->fsf_status_qual.word[0]);
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "fsf_sq_inval:");
			debug_exception(
				fsf_req->adapter->erp_dbf, 0,
				&header->fsf_status_qual.word[0],
				sizeof (u32));
			break;
		}
		break;

	case FSF_GOOD:
		/* save port handle assigned by FSF */
		port->handle = header->port_handle;
		ZFCP_LOG_INFO("The remote port 0x%016Lx via adapter %s "
			      "was opened, it's port handle is 0x%x\n",
			      port->wwpn, zfcp_get_busid_by_port(port),
			      port->handle);
		/* mark port as open */
		atomic_set_mask(ZFCP_STATUS_COMMON_OPEN |
				ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		atomic_clear_mask(ZFCP_STATUS_COMMON_ACCESS_DENIED |
		                  ZFCP_STATUS_COMMON_ACCESS_BOXED,
		                  &port->status);
		retval = 0;
		/* check whether D_ID has changed during open */
		/*
		 * FIXME: This check is not airtight, as the FCP channel does
		 * not monitor closures of target port connections caused on
		 * the remote side. Thus, they might miss out on invalidating
		 * locally cached WWPNs (and other N_Port parameters) of gone
		 * target ports. So, our heroic attempt to make things safe
		 * could be undermined by 'open port' response data tagged with
		 * obsolete WWPNs. Another reason to monitor potential
		 * connection closures ourself at least (by interpreting
		 * incoming ELS' and unsolicited status). It just crosses my
		 * mind that one should be able to cross-check by means of
		 * another GID_PN straight after a port has been opened.
		 * Alternately, an ADISC/PDISC ELS should suffice, as well.
		 */
		plogi = (struct fsf_plogi *) fsf_req->qtcb->bottom.support.els;
		if (!atomic_test_mask(ZFCP_STATUS_PORT_NO_WWPN, &port->status))
		{
			if (fsf_req->qtcb->bottom.support.els1_length <
			    sizeof (struct fsf_plogi)) {
				ZFCP_LOG_INFO(
					"warning: insufficient length of "
					"PLOGI payload (%i)\n",
					fsf_req->qtcb->bottom.support.els1_length);
				debug_text_event(fsf_req->adapter->erp_dbf, 0,
						 "fsf_s_short_plogi:");
				/* skip sanity check and assume wwpn is ok */
			} else {
				if (plogi->serv_param.wwpn != port->wwpn) {
					ZFCP_LOG_INFO("warning: d_id of port "
						      "0x%016Lx changed during "
						      "open\n", port->wwpn);
					debug_text_event(
						fsf_req->adapter->erp_dbf, 0,
						"fsf_s_did_change:");
					atomic_clear_mask(
						ZFCP_STATUS_PORT_DID_DID,
						&port->status);
				} else {
					port->wwnn = plogi->serv_param.wwnn;
					zfcp_plogi_evaluate(port, plogi);
				}
			}
		}
		break;

	case FSF_UNKNOWN_OP_SUBTYPE:
		/* should never occure, subtype not set in zfcp_fsf_open_port */
		ZFCP_LOG_INFO("unknown operation subtype (adapter: %s, "
			      "op_subtype=0x%x)\n",
			      zfcp_get_busid_by_port(port),
			      fsf_req->qtcb->bottom.support.operation_subtype);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				header->fsf_status);
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&header->fsf_status, sizeof (u32));
		break;
	}

 skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING, &port->status);
	return retval;
}

/*
 * function:    zfcp_fsf_close_port
 *
 * purpose:     submit FSF command "close port"
 *
 * returns:     address of initiated FSF request
 *              NULL - request could not be initiated
 */
int
zfcp_fsf_close_port(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	unsigned long lock_flags;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_CLOSE_PORT,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     erp_action->adapter->pool.fsf_req_erp,
				     &lock_flags, &(erp_action->fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Could not create a close port request "
			      "for port 0x%016Lx on adapter %s.\n",
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(erp_action->fsf_req,
                                    erp_action->fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING, &erp_action->port->status);
	erp_action->fsf_req->data = (unsigned long) erp_action->port;
	erp_action->fsf_req->erp_action = erp_action;
	erp_action->fsf_req->qtcb->header.port_handle =
	    erp_action->port->handle;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send a close port request for "
			      "port 0x%016Lx on adapter %s.\n",
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		zfcp_fsf_req_free(erp_action->fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_TRACE("close port request initiated "
		       "(adapter %s, port 0x%016Lx)\n",
		       zfcp_get_busid_by_adapter(erp_action->adapter),
		       erp_action->port->wwpn);
 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/*
 * function:    zfcp_fsf_close_port_handler
 *
 * purpose:     is called for finished Close Port FSF command
 *
 * returns:
 */
static int
zfcp_fsf_close_port_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_port *port;

	port = (struct zfcp_port *) fsf_req->data;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change port status in our bookkeeping */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		ZFCP_LOG_INFO("Temporary port identifier 0x%x for port "
			      "0x%016Lx on adapter %s invalid. This may happen "
			      "occasionally.\n", port->handle,
			      port->wwpn, zfcp_get_busid_by_port(port));
		ZFCP_LOG_DEBUG("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_phand_nv");
		zfcp_erp_adapter_reopen(port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		/* Note: FSF has actually closed the port in this case.
		 * The status code is just daft. Fingers crossed for a change
		 */
		retval = 0;
		break;

	case FSF_GOOD:
		ZFCP_LOG_TRACE("remote port 0x016%Lx on adapter %s closed, "
			       "port handle 0x%x\n", port->wwpn,
			       zfcp_get_busid_by_port(port), port->handle);
		zfcp_erp_modify_port_status(port,
					    ZFCP_STATUS_COMMON_OPEN,
					    ZFCP_CLEAR);
		retval = 0;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				fsf_req->qtcb->header.fsf_status);
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status,
				sizeof (u32));
		break;
	}

 skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_CLOSING, &port->status);
	return retval;
}

/*
 * function:    zfcp_fsf_close_physical_port
 *
 * purpose:     submit FSF command "close physical port"
 *
 * returns:     address of initiated FSF request
 *              NULL - request could not be initiated
 */
int
zfcp_fsf_close_physical_port(struct zfcp_erp_action *erp_action)
{
	int retval = 0;
	unsigned long lock_flags;
	volatile struct qdio_buffer_element *sbale;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_CLOSE_PHYSICAL_PORT,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     erp_action->adapter->pool.fsf_req_erp,
				     &lock_flags, &erp_action->fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Could not create close physical port "
			      "request (adapter %s, port 0x%016Lx)\n",
			      zfcp_get_busid_by_adapter(erp_action->adapter),
			      erp_action->port->wwpn);

		goto out;
	}

	sbale = zfcp_qdio_sbale_req(erp_action->fsf_req,
				    erp_action->fsf_req->sbal_curr, 0);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	/* mark port as being closed */
	atomic_set_mask(ZFCP_STATUS_PORT_PHYS_CLOSING,
			&erp_action->port->status);
	/* save a pointer to this port */
	erp_action->fsf_req->data = (unsigned long) erp_action->port;
	/* port to be closed */
	erp_action->fsf_req->qtcb->header.port_handle =
	    erp_action->port->handle;
	erp_action->fsf_req->erp_action = erp_action;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send close physical port "
			      "request (adapter %s, port 0x%016Lx)\n",
			      zfcp_get_busid_by_adapter(erp_action->adapter),
			      erp_action->port->wwpn);
		zfcp_fsf_req_free(erp_action->fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_TRACE("close physical port request initiated "
		       "(adapter %s, port 0x%016Lx)\n",
		       zfcp_get_busid_by_adapter(erp_action->adapter),
		       erp_action->port->wwpn);
 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/*
 * function:    zfcp_fsf_close_physical_port_handler
 *
 * purpose:     is called for finished Close Physical Port FSF command
 *
 * returns:
 */
static int
zfcp_fsf_close_physical_port_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_port *port;
	struct zfcp_unit *unit;
	struct fsf_qtcb_header *header;
	u16 subtable, rule, counter;

	port = (struct zfcp_port *) fsf_req->data;
	header = &fsf_req->qtcb->header;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change port status in our bookkeeping */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (header->fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		ZFCP_LOG_INFO("Temporary port identifier 0x%x invalid"
			      "(adapter %s, port 0x%016Lx). "
			      "This may happen occasionally.\n",
			      port->handle,
			      zfcp_get_busid_by_port(port),
			      port->wwpn);
		ZFCP_LOG_DEBUG("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &header->fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_phand_nv");
		zfcp_erp_adapter_reopen(port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ACCESS_DENIED:
		ZFCP_LOG_NORMAL("Access denied, cannot close "
				"physical port 0x%016Lx on adapter %s\n",
				port->wwpn, zfcp_get_busid_by_port(port));
		for (counter = 0; counter < 2; counter++) {
			subtable = header->fsf_status_qual.halfword[counter * 2];
			rule = header->fsf_status_qual.halfword[counter * 2 + 1];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
	       			ZFCP_LOG_INFO("Access denied (%s rule %d)\n",
					zfcp_act_subtable_type[subtable], rule);
				break;
			}
		}
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_access");
		zfcp_erp_port_access_denied(port);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PORT_BOXED:
		ZFCP_LOG_DEBUG("The remote port 0x%016Lx on adapter "
			       "%s needs to be reopened but it was attempted "
			       "to close it physically.\n",
			       port->wwpn,
			       zfcp_get_busid_by_port(port));
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_pboxed");
		zfcp_erp_port_boxed(port);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
			/* This will now be escalated by ERP */
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* ERP strategy will escalate */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			ZFCP_LOG_NORMAL
			    ("bug: Wrong status qualifier 0x%x arrived.\n",
			     header->fsf_status_qual.word[0]);
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "fsf_sq_inval:");
			debug_exception(
				fsf_req->adapter->erp_dbf, 0,
				&header->fsf_status_qual.word[0], sizeof (u32));
			break;
		}
		break;

	case FSF_GOOD:
		ZFCP_LOG_DEBUG("Remote port 0x%016Lx via adapter %s "
			       "physically closed, port handle 0x%x\n",
			       port->wwpn,
			       zfcp_get_busid_by_port(port), port->handle);
		/* can't use generic zfcp_erp_modify_port_status because
		 * ZFCP_STATUS_COMMON_OPEN must not be reset for the port
		 */
		atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		list_for_each_entry(unit, &port->unit_list_head, list)
		    atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);
		retval = 0;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				header->fsf_status);
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&header->fsf_status, sizeof (u32));
		break;
	}

 skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_CLOSING, &port->status);
	return retval;
}

/*
 * function:    zfcp_fsf_open_unit
 *
 * purpose:
 *
 * returns:
 *
 * assumptions:	This routine does not check whether the associated
 *		remote port has already been opened. This should be
 *		done by calling routines. Otherwise some status
 *		may be presented by FSF
 */
int
zfcp_fsf_open_unit(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	unsigned long lock_flags;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_OPEN_LUN,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     erp_action->adapter->pool.fsf_req_erp,
				     &lock_flags, &(erp_action->fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Could not create open unit request for "
			      "unit 0x%016Lx on port 0x%016Lx on adapter %s.\n",
			      erp_action->unit->fcp_lun,
			      erp_action->unit->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(erp_action->fsf_req,
                                    erp_action->fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	erp_action->fsf_req->qtcb->header.port_handle =
		erp_action->port->handle;
	erp_action->fsf_req->qtcb->bottom.support.fcp_lun =
		erp_action->unit->fcp_lun;
	if (!(erp_action->adapter->connection_features & FSF_FEATURE_NPIV_MODE))
		erp_action->fsf_req->qtcb->bottom.support.option =
			FSF_OPEN_LUN_SUPPRESS_BOXING;
	atomic_set_mask(ZFCP_STATUS_COMMON_OPENING, &erp_action->unit->status);
	erp_action->fsf_req->data = (unsigned long) erp_action->unit;
	erp_action->fsf_req->erp_action = erp_action;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send an open unit request "
			      "on the adapter %s, port 0x%016Lx for "
			      "unit 0x%016Lx\n",
			      zfcp_get_busid_by_adapter(erp_action->adapter),
			      erp_action->port->wwpn,
			      erp_action->unit->fcp_lun);
		zfcp_fsf_req_free(erp_action->fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_TRACE("Open LUN request initiated (adapter %s, "
		       "port 0x%016Lx, unit 0x%016Lx)\n",
		       zfcp_get_busid_by_adapter(erp_action->adapter),
		       erp_action->port->wwpn, erp_action->unit->fcp_lun);
 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/*
 * function:    zfcp_fsf_open_unit_handler
 *
 * purpose:	is called for finished Open LUN command
 *
 * returns:	
 */
static int
zfcp_fsf_open_unit_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_adapter *adapter;
	struct zfcp_unit *unit;
	struct fsf_qtcb_header *header;
	struct fsf_qtcb_bottom_support *bottom;
	struct fsf_queue_designator *queue_designator;
	u16 subtable, rule, counter;
	int exclusive, readwrite;

	unit = (struct zfcp_unit *) fsf_req->data;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change unit status in our bookkeeping */
		goto skip_fsfstatus;
	}

	adapter = fsf_req->adapter;
	header = &fsf_req->qtcb->header;
	bottom = &fsf_req->qtcb->bottom.support;
	queue_designator = &header->fsf_status_qual.fsf_queue_designator;

	atomic_clear_mask(ZFCP_STATUS_COMMON_ACCESS_DENIED |
			  ZFCP_STATUS_UNIT_SHARED |
			  ZFCP_STATUS_UNIT_READONLY,
			  &unit->status);

	/* evaluate FSF status in QTCB */
	switch (header->fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		ZFCP_LOG_INFO("Temporary port identifier 0x%x "
			      "for port 0x%016Lx on adapter %s invalid "
			      "This may happen occasionally\n",
			      unit->port->handle,
			      unit->port->wwpn, zfcp_get_busid_by_unit(unit));
		ZFCP_LOG_DEBUG("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &header->fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(adapter->erp_dbf, 1, "fsf_s_ph_nv");
		zfcp_erp_adapter_reopen(unit->port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_LUN_ALREADY_OPEN:
		ZFCP_LOG_NORMAL("bug: Attempted to open unit 0x%016Lx on "
				"remote port 0x%016Lx on adapter %s twice.\n",
				unit->fcp_lun,
				unit->port->wwpn, zfcp_get_busid_by_unit(unit));
		debug_text_exception(adapter->erp_dbf, 0,
				     "fsf_s_uopen");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ACCESS_DENIED:
		ZFCP_LOG_NORMAL("Access denied, cannot open unit 0x%016Lx on "
				"remote port 0x%016Lx on adapter %s\n",
				unit->fcp_lun, unit->port->wwpn,
				zfcp_get_busid_by_unit(unit));
		for (counter = 0; counter < 2; counter++) {
			subtable = header->fsf_status_qual.halfword[counter * 2];
			rule = header->fsf_status_qual.halfword[counter * 2 + 1];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
				ZFCP_LOG_INFO("Access denied (%s rule %d)\n",
					zfcp_act_subtable_type[subtable], rule);
				break;
			}
		}
		debug_text_event(adapter->erp_dbf, 1, "fsf_s_access");
		zfcp_erp_unit_access_denied(unit);
		atomic_clear_mask(ZFCP_STATUS_UNIT_SHARED, &unit->status);
                atomic_clear_mask(ZFCP_STATUS_UNIT_READONLY, &unit->status);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PORT_BOXED:
		ZFCP_LOG_DEBUG("The remote port 0x%016Lx on adapter %s "
			       "needs to be reopened\n",
			       unit->port->wwpn, zfcp_get_busid_by_unit(unit));
		debug_text_event(adapter->erp_dbf, 2, "fsf_s_pboxed");
		zfcp_erp_port_boxed(unit->port);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_LUN_SHARING_VIOLATION:
		if (header->fsf_status_qual.word[0] != 0) {
			ZFCP_LOG_NORMAL("FCP-LUN 0x%Lx at the remote port "
					"with WWPN 0x%Lx "
					"connected to the adapter %s "
					"is already in use in LPAR%d, CSS%d\n",
					unit->fcp_lun,
					unit->port->wwpn,
					zfcp_get_busid_by_unit(unit),
					queue_designator->hla,
					queue_designator->cssid);
		} else {
			subtable = header->fsf_status_qual.halfword[4];
			rule = header->fsf_status_qual.halfword[5];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
				ZFCP_LOG_NORMAL("Access to FCP-LUN 0x%Lx at the "
						"remote port with WWPN 0x%Lx "
						"connected to the adapter %s "
						"is denied (%s rule %d)\n",
						unit->fcp_lun,
						unit->port->wwpn,
						zfcp_get_busid_by_unit(unit),
						zfcp_act_subtable_type[subtable],
						rule);
				break;
			}
		}
		ZFCP_LOG_DEBUG("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &header->fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(adapter->erp_dbf, 2,
				 "fsf_s_l_sh_vio");
		zfcp_erp_unit_access_denied(unit);
		atomic_clear_mask(ZFCP_STATUS_UNIT_SHARED, &unit->status);
		atomic_clear_mask(ZFCP_STATUS_UNIT_READONLY, &unit->status);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_MAXIMUM_NUMBER_OF_LUNS_EXCEEDED:
		ZFCP_LOG_INFO("error: The adapter ran out of resources. "
			      "There is no handle (temporary port identifier) "
			      "available for unit 0x%016Lx on port 0x%016Lx "
			      "on adapter %s\n",
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit));
		debug_text_event(adapter->erp_dbf, 1,
				 "fsf_s_max_units");
		zfcp_erp_unit_failed(unit);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			/* Re-establish link to port */
			debug_text_event(adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
			zfcp_test_link(unit->port);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* ERP strategy will escalate */
			debug_text_event(adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			ZFCP_LOG_NORMAL
			    ("bug: Wrong status qualifier 0x%x arrived.\n",
			     header->fsf_status_qual.word[0]);
			debug_text_event(adapter->erp_dbf, 0,
					 "fsf_sq_inval:");
			debug_exception(adapter->erp_dbf, 0,
					&header->fsf_status_qual.word[0],
				sizeof (u32));
		}
		break;

	case FSF_INVALID_COMMAND_OPTION:
		ZFCP_LOG_NORMAL(
			"Invalid option 0x%x has been specified "
			"in QTCB bottom sent to the adapter %s\n",
			bottom->option,
			zfcp_get_busid_by_adapter(adapter));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EINVAL;
		break;

	case FSF_GOOD:
		/* save LUN handle assigned by FSF */
		unit->handle = header->lun_handle;
		ZFCP_LOG_TRACE("unit 0x%016Lx on remote port 0x%016Lx on "
			       "adapter %s opened, port handle 0x%x\n",
			       unit->fcp_lun,
			       unit->port->wwpn,
			       zfcp_get_busid_by_unit(unit),
			       unit->handle);
		/* mark unit as open */
		atomic_set_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);

		if (!(adapter->connection_features & FSF_FEATURE_NPIV_MODE) &&
		    (adapter->adapter_features & FSF_FEATURE_LUN_SHARING) &&
		    (adapter->ccw_device->id.dev_model != ZFCP_DEVICE_MODEL_PRIV)) {
			exclusive = (bottom->lun_access_info &
					FSF_UNIT_ACCESS_EXCLUSIVE);
			readwrite = (bottom->lun_access_info &
					FSF_UNIT_ACCESS_OUTBOUND_TRANSFER);

			if (!exclusive)
		                atomic_set_mask(ZFCP_STATUS_UNIT_SHARED,
						&unit->status);

			if (!readwrite) {
                		atomic_set_mask(ZFCP_STATUS_UNIT_READONLY,
						&unit->status);
                		ZFCP_LOG_NORMAL("read-only access for unit "
						"(adapter %s, wwpn=0x%016Lx, "
						"fcp_lun=0x%016Lx)\n",
						zfcp_get_busid_by_unit(unit),
						unit->port->wwpn,
						unit->fcp_lun);
        		}

        		if (exclusive && !readwrite) {
                		ZFCP_LOG_NORMAL("exclusive access of read-only "
						"unit not supported\n");
				zfcp_erp_unit_failed(unit);
				fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
				zfcp_erp_unit_shutdown(unit, 0);
        		} else if (!exclusive && readwrite) {
                		ZFCP_LOG_NORMAL("shared access of read-write "
						"unit not supported\n");
                		zfcp_erp_unit_failed(unit);
				fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
				zfcp_erp_unit_shutdown(unit, 0);
        		}
		}

		retval = 0;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				header->fsf_status);
		debug_text_event(adapter->erp_dbf, 0, "fsf_s_inval:");
		debug_exception(adapter->erp_dbf, 0,
				&header->fsf_status, sizeof (u32));
		break;
	}

 skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING, &unit->status);
	return retval;
}

/*
 * function:    zfcp_fsf_close_unit
 *
 * purpose:
 *
 * returns:	address of fsf_req - request successfully initiated
 *		NULL - 
 *
 * assumptions: This routine does not check whether the associated
 *              remote port/lun has already been opened. This should be
 *              done by calling routines. Otherwise some status
 *              may be presented by FSF
 */
int
zfcp_fsf_close_unit(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	unsigned long lock_flags;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_CLOSE_LUN,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     erp_action->adapter->pool.fsf_req_erp,
				     &lock_flags, &(erp_action->fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Could not create close unit request for "
			      "unit 0x%016Lx on port 0x%016Lx on adapter %s.\n",
			      erp_action->unit->fcp_lun,
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(erp_action->fsf_req,
                                    erp_action->fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	erp_action->fsf_req->qtcb->header.port_handle =
	    erp_action->port->handle;
	erp_action->fsf_req->qtcb->header.lun_handle = erp_action->unit->handle;
	atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING, &erp_action->unit->status);
	erp_action->fsf_req->data = (unsigned long) erp_action->unit;
	erp_action->fsf_req->erp_action = erp_action;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send a close unit request for "
			      "unit 0x%016Lx on port 0x%016Lx onadapter %s.\n",
			      erp_action->unit->fcp_lun,
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		zfcp_fsf_req_free(erp_action->fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_TRACE("Close LUN request initiated (adapter %s, "
		       "port 0x%016Lx, unit 0x%016Lx)\n",
		       zfcp_get_busid_by_adapter(erp_action->adapter),
		       erp_action->port->wwpn, erp_action->unit->fcp_lun);
 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/*
 * function:    zfcp_fsf_close_unit_handler
 *
 * purpose:     is called for finished Close LUN FSF command
 *
 * returns:
 */
static int
zfcp_fsf_close_unit_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_unit *unit;

	unit = (struct zfcp_unit *) fsf_req->data;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change unit status in our bookkeeping */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		ZFCP_LOG_INFO("Temporary port identifier 0x%x for port "
			      "0x%016Lx on adapter %s invalid. This may "
			      "happen in rare circumstances\n",
			      unit->port->handle,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit));
		ZFCP_LOG_DEBUG("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_phand_nv");
		zfcp_erp_adapter_reopen(unit->port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_LUN_HANDLE_NOT_VALID:
		ZFCP_LOG_INFO("Temporary LUN identifier 0x%x of unit "
			      "0x%016Lx on port 0x%016Lx on adapter %s is "
			      "invalid. This may happen occasionally.\n",
			      unit->handle,
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit));
		ZFCP_LOG_DEBUG("Status qualifier data:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_lhand_nv");
		zfcp_erp_port_reopen(unit->port, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PORT_BOXED:
		ZFCP_LOG_DEBUG("The remote port 0x%016Lx on adapter %s "
			       "needs to be reopened\n",
			       unit->port->wwpn,
			       zfcp_get_busid_by_unit(unit));
		debug_text_event(fsf_req->adapter->erp_dbf, 2, "fsf_s_pboxed");
		zfcp_erp_port_boxed(unit->port);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (fsf_req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			/* re-establish link to port */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
			zfcp_test_link(unit->port);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* ERP strategy will escalate */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			ZFCP_LOG_NORMAL
			    ("bug: Wrong status qualifier 0x%x arrived.\n",
			     fsf_req->qtcb->header.fsf_status_qual.word[0]);
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "fsf_sq_inval:");
			debug_exception(
				fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status_qual.word[0],
				sizeof (u32));
			break;
		}
		break;

	case FSF_GOOD:
		ZFCP_LOG_TRACE("unit 0x%016Lx on port 0x%016Lx on adapter %s "
			       "closed, port handle 0x%x\n",
			       unit->fcp_lun,
			       unit->port->wwpn,
			       zfcp_get_busid_by_unit(unit),
			       unit->handle);
		/* mark unit as closed */
		atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);
		retval = 0;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				fsf_req->qtcb->header.fsf_status);
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status,
				sizeof (u32));
		break;
	}

 skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_CLOSING, &unit->status);
	return retval;
}

/**
 * zfcp_fsf_send_fcp_command_task - initiate an FCP command (for a SCSI command)
 * @adapter: adapter where scsi command is issued
 * @unit: unit where command is sent to
 * @scsi_cmnd: scsi command to be sent
 * @timer: timer to be started when request is initiated
 * @req_flags: flags for fsf_request
 */
int
zfcp_fsf_send_fcp_command_task(struct zfcp_adapter *adapter,
			       struct zfcp_unit *unit,
			       struct scsi_cmnd * scsi_cmnd,
			       struct timer_list *timer, int req_flags)
{
	struct zfcp_fsf_req *fsf_req = NULL;
	struct fcp_cmnd_iu *fcp_cmnd_iu;
	unsigned int sbtype;
	unsigned long lock_flags;
	int real_bytes = 0;
	int retval = 0;
	int mask;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_FCP_CMND, req_flags,
				     adapter->pool.fsf_req_scsi,
				     &lock_flags, &fsf_req);
	if (unlikely(retval < 0)) {
		ZFCP_LOG_DEBUG("error: Could not create FCP command request "
			       "for unit 0x%016Lx on port 0x%016Lx on "
			       "adapter %s\n",
			       unit->fcp_lun,
			       unit->port->wwpn,
			       zfcp_get_busid_by_adapter(adapter));
		goto failed_req_create;
	}

	zfcp_unit_get(unit);
	fsf_req->unit = unit;

	/* associate FSF request with SCSI request (for look up on abort) */
	scsi_cmnd->host_scribble = (char *) fsf_req;

	/* associate SCSI command with FSF request */
	fsf_req->data = (unsigned long) scsi_cmnd;

	/* set handles of unit and its parent port in QTCB */
	fsf_req->qtcb->header.lun_handle = unit->handle;
	fsf_req->qtcb->header.port_handle = unit->port->handle;

	/* FSF does not define the structure of the FCP_CMND IU */
	fcp_cmnd_iu = (struct fcp_cmnd_iu *)
	    &(fsf_req->qtcb->bottom.io.fcp_cmnd);

	/*
	 * set depending on data direction:
	 *      data direction bits in SBALE (SB Type)
	 *      data direction bits in QTCB
	 *      data direction bits in FCP_CMND IU
	 */
	switch (scsi_cmnd->sc_data_direction) {
	case DMA_NONE:
		fsf_req->qtcb->bottom.io.data_direction = FSF_DATADIR_CMND;
		/*
		 * FIXME(qdio):
		 * what is the correct type for commands
		 * without 'real' data buffers?
		 */
		sbtype = SBAL_FLAGS0_TYPE_READ;
		break;
	case DMA_FROM_DEVICE:
		fsf_req->qtcb->bottom.io.data_direction = FSF_DATADIR_READ;
		sbtype = SBAL_FLAGS0_TYPE_READ;
		fcp_cmnd_iu->rddata = 1;
		break;
	case DMA_TO_DEVICE:
		fsf_req->qtcb->bottom.io.data_direction = FSF_DATADIR_WRITE;
		sbtype = SBAL_FLAGS0_TYPE_WRITE;
		fcp_cmnd_iu->wddata = 1;
		break;
	case DMA_BIDIRECTIONAL:
	default:
		/*
		 * dummy, catch this condition earlier
		 * in zfcp_scsi_queuecommand
		 */
		goto failed_scsi_cmnd;
	}

	/* set FC service class in QTCB (3 per default) */
	fsf_req->qtcb->bottom.io.service_class = ZFCP_FC_SERVICE_CLASS_DEFAULT;

	/* set FCP_LUN in FCP_CMND IU in QTCB */
	fcp_cmnd_iu->fcp_lun = unit->fcp_lun;

	mask = ZFCP_STATUS_UNIT_READONLY | ZFCP_STATUS_UNIT_SHARED;

	/* set task attributes in FCP_CMND IU in QTCB */
	if (likely((scsi_cmnd->device->simple_tags) ||
		   (atomic_test_mask(mask, &unit->status))))
		fcp_cmnd_iu->task_attribute = SIMPLE_Q;
	else
		fcp_cmnd_iu->task_attribute = UNTAGGED;

	/* set additional length of FCP_CDB in FCP_CMND IU in QTCB, if needed */
	if (unlikely(scsi_cmnd->cmd_len > FCP_CDB_LENGTH)) {
		fcp_cmnd_iu->add_fcp_cdb_length
		    = (scsi_cmnd->cmd_len - FCP_CDB_LENGTH) >> 2;
		ZFCP_LOG_TRACE("SCSI CDB length is 0x%x, "
			       "additional FCP_CDB length is 0x%x "
			       "(shifted right 2 bits)\n",
			       scsi_cmnd->cmd_len,
			       fcp_cmnd_iu->add_fcp_cdb_length);
	}
	/*
	 * copy SCSI CDB (including additional length, if any) to
	 * FCP_CDB in FCP_CMND IU in QTCB
	 */
	memcpy(fcp_cmnd_iu->fcp_cdb, scsi_cmnd->cmnd, scsi_cmnd->cmd_len);

	/* FCP CMND IU length in QTCB */
	fsf_req->qtcb->bottom.io.fcp_cmnd_length =
		sizeof (struct fcp_cmnd_iu) +
		fcp_cmnd_iu->add_fcp_cdb_length + sizeof (fcp_dl_t);

	/* generate SBALEs from data buffer */
	real_bytes = zfcp_qdio_sbals_from_scsicmnd(fsf_req, sbtype, scsi_cmnd);
	if (unlikely(real_bytes < 0)) {
		if (fsf_req->sbal_number < ZFCP_MAX_SBALS_PER_REQ) {
			ZFCP_LOG_DEBUG(
				"Data did not fit into available buffer(s), "
			       "waiting for more...\n");
		retval = -EIO;
	} else {
		ZFCP_LOG_NORMAL("error: No truncation implemented but "
				"required. Shutting down unit "
				"(adapter %s, port 0x%016Lx, "
				"unit 0x%016Lx)\n",
				zfcp_get_busid_by_unit(unit),
				unit->port->wwpn,
				unit->fcp_lun);
		zfcp_erp_unit_shutdown(unit, 0);
		retval = -EINVAL;
		}
		goto no_fit;
	}

	/* set length of FCP data length in FCP_CMND IU in QTCB */
	zfcp_set_fcp_dl(fcp_cmnd_iu, real_bytes);

	ZFCP_LOG_DEBUG("Sending SCSI command:\n");
	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
		      (char *) scsi_cmnd->cmnd, scsi_cmnd->cmd_len);

	/*
	 * start QDIO request for this FSF request
	 *  covered by an SBALE)
	 */
	retval = zfcp_fsf_req_send(fsf_req, timer);
	if (unlikely(retval < 0)) {
		ZFCP_LOG_INFO("error: Could not send FCP command request "
			      "on adapter %s, port 0x%016Lx, unit 0x%016Lx\n",
			      zfcp_get_busid_by_adapter(adapter),
			      unit->port->wwpn,
			      unit->fcp_lun);
		goto send_failed;
	}

	ZFCP_LOG_TRACE("Send FCP Command initiated (adapter %s, "
		       "port 0x%016Lx, unit 0x%016Lx)\n",
		       zfcp_get_busid_by_adapter(adapter),
		       unit->port->wwpn,
		       unit->fcp_lun);
	goto success;

 send_failed:
 no_fit:
 failed_scsi_cmnd:
	zfcp_unit_put(unit);
	zfcp_fsf_req_free(fsf_req);
	fsf_req = NULL;
	scsi_cmnd->host_scribble = NULL;
 success:
 failed_req_create:
	write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);
	return retval;
}

struct zfcp_fsf_req *
zfcp_fsf_send_fcp_command_task_management(struct zfcp_adapter *adapter,
					  struct zfcp_unit *unit,
					  u8 tm_flags, int req_flags)
{
	struct zfcp_fsf_req *fsf_req = NULL;
	int retval = 0;
	struct fcp_cmnd_iu *fcp_cmnd_iu;
	unsigned long lock_flags;
	volatile struct qdio_buffer_element *sbale;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_FCP_CMND, req_flags,
				     adapter->pool.fsf_req_scsi,
				     &lock_flags, &fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Could not create FCP command (task "
			      "management) request for adapter %s, port "
			      " 0x%016Lx, unit 0x%016Lx.\n",
			      zfcp_get_busid_by_adapter(adapter),
			      unit->port->wwpn, unit->fcp_lun);
		goto out;
	}

	/*
	 * Used to decide on proper handler in the return path,
	 * could be either zfcp_fsf_send_fcp_command_task_handler or
	 * zfcp_fsf_send_fcp_command_task_management_handler */

	fsf_req->status |= ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT;

	/*
	 * hold a pointer to the unit being target of this
	 * task management request
	 */
	fsf_req->data = (unsigned long) unit;

	/* set FSF related fields in QTCB */
	fsf_req->qtcb->header.lun_handle = unit->handle;
	fsf_req->qtcb->header.port_handle = unit->port->handle;
	fsf_req->qtcb->bottom.io.data_direction = FSF_DATADIR_CMND;
	fsf_req->qtcb->bottom.io.service_class = ZFCP_FC_SERVICE_CLASS_DEFAULT;
	fsf_req->qtcb->bottom.io.fcp_cmnd_length =
		sizeof (struct fcp_cmnd_iu) + sizeof (fcp_dl_t);

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_WRITE;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	/* set FCP related fields in FCP_CMND IU in QTCB */
	fcp_cmnd_iu = (struct fcp_cmnd_iu *)
		&(fsf_req->qtcb->bottom.io.fcp_cmnd);
	fcp_cmnd_iu->fcp_lun = unit->fcp_lun;
	fcp_cmnd_iu->task_management_flags = tm_flags;

	/* start QDIO request for this FSF request */
	zfcp_fsf_start_scsi_er_timer(adapter);
	retval = zfcp_fsf_req_send(fsf_req, NULL);
	if (retval) {
		del_timer(&adapter->scsi_er_timer);
		ZFCP_LOG_INFO("error: Could not send an FCP-command (task "
			      "management) on adapter %s, port 0x%016Lx for "
			      "unit LUN 0x%016Lx\n",
			      zfcp_get_busid_by_adapter(adapter),
			      unit->port->wwpn,
			      unit->fcp_lun);
		zfcp_fsf_req_free(fsf_req);
		fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_TRACE("Send FCP Command (task management function) initiated "
		       "(adapter %s, port 0x%016Lx, unit 0x%016Lx, "
		       "tm_flags=0x%x)\n",
		       zfcp_get_busid_by_adapter(adapter),
		       unit->port->wwpn,
		       unit->fcp_lun,
		       tm_flags);
 out:
	write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);
	return fsf_req;
}

/*
 * function:    zfcp_fsf_send_fcp_command_handler
 *
 * purpose:	is called for finished Send FCP Command
 *
 * returns:	
 */
static int
zfcp_fsf_send_fcp_command_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_unit *unit;
	struct fsf_qtcb_header *header;
	u16 subtable, rule, counter;

	header = &fsf_req->qtcb->header;

	if (unlikely(fsf_req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT))
		unit = (struct zfcp_unit *) fsf_req->data;
	else
		unit = fsf_req->unit;

	if (unlikely(fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR)) {
		/* go directly to calls of special handlers */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (header->fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		ZFCP_LOG_INFO("Temporary port identifier 0x%x for port "
			      "0x%016Lx on adapter %s invalid\n",
			      unit->port->handle,
			      unit->port->wwpn, zfcp_get_busid_by_unit(unit));
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &header->fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_phand_nv");
		zfcp_erp_adapter_reopen(unit->port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_LUN_HANDLE_NOT_VALID:
		ZFCP_LOG_INFO("Temporary LUN identifier 0x%x for unit "
			      "0x%016Lx on port 0x%016Lx on adapter %s is "
			      "invalid. This may happen occasionally.\n",
			      unit->handle,
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit));
		ZFCP_LOG_NORMAL("Status qualifier data:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) &header->fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_uhand_nv");
		zfcp_erp_port_reopen(unit->port, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_HANDLE_MISMATCH:
		ZFCP_LOG_NORMAL("bug: The port handle 0x%x has changed "
				"unexpectedly. (adapter %s, port 0x%016Lx, "
				"unit 0x%016Lx)\n",
				unit->port->handle,
				zfcp_get_busid_by_unit(unit),
				unit->port->wwpn,
				unit->fcp_lun);
		ZFCP_LOG_NORMAL("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) &header->fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_hand_mis");
		zfcp_erp_adapter_reopen(unit->port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		ZFCP_LOG_INFO("error: adapter %s does not support fc "
			      "class %d.\n",
			      zfcp_get_busid_by_unit(unit),
			      ZFCP_FC_SERVICE_CLASS_DEFAULT);
		/* stop operation for this adapter */
		debug_text_exception(fsf_req->adapter->erp_dbf, 0,
				     "fsf_s_class_nsup");
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_FCPLUN_NOT_VALID:
		ZFCP_LOG_NORMAL("bug: unit 0x%016Lx on port 0x%016Lx on "
				"adapter %s does not have correct unit "
				"handle 0x%x\n",
				unit->fcp_lun,
				unit->port->wwpn,
				zfcp_get_busid_by_unit(unit),
				unit->handle);
		ZFCP_LOG_DEBUG("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &header->fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_fcp_lun_nv");
		zfcp_erp_port_reopen(unit->port, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ACCESS_DENIED:
		ZFCP_LOG_NORMAL("Access denied, cannot send FCP command to "
				"unit 0x%016Lx on port 0x%016Lx on "
				"adapter %s\n",	unit->fcp_lun, unit->port->wwpn,
				zfcp_get_busid_by_unit(unit));
		for (counter = 0; counter < 2; counter++) {
			subtable = header->fsf_status_qual.halfword[counter * 2];
			rule = header->fsf_status_qual.halfword[counter * 2 + 1];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
				ZFCP_LOG_INFO("Access denied (%s rule %d)\n",
					zfcp_act_subtable_type[subtable], rule);
				break;
			}
		}
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_access");
		zfcp_erp_unit_access_denied(unit);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_DIRECTION_INDICATOR_NOT_VALID:
		ZFCP_LOG_INFO("bug: Invalid data direction given for unit "
			      "0x%016Lx on port 0x%016Lx on adapter %s "
			      "(debug info %d)\n",
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit),
			      fsf_req->qtcb->bottom.io.data_direction);
		/* stop operation for this adapter */
		debug_text_event(fsf_req->adapter->erp_dbf, 0,
				 "fsf_s_dir_ind_nv");
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_CMND_LENGTH_NOT_VALID:
		ZFCP_LOG_NORMAL
		    ("bug: An invalid control-data-block length field "
		     "was found in a command for unit 0x%016Lx on port "
		     "0x%016Lx on adapter %s " "(debug info %d)\n",
		     unit->fcp_lun, unit->port->wwpn,
		     zfcp_get_busid_by_unit(unit),
		     fsf_req->qtcb->bottom.io.fcp_cmnd_length);
		/* stop operation for this adapter */
		debug_text_event(fsf_req->adapter->erp_dbf, 0,
				 "fsf_s_cmd_len_nv");
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PORT_BOXED:
		ZFCP_LOG_DEBUG("The remote port 0x%016Lx on adapter %s "
			       "needs to be reopened\n",
			       unit->port->wwpn, zfcp_get_busid_by_unit(unit));
		debug_text_event(fsf_req->adapter->erp_dbf, 2, "fsf_s_pboxed");
		zfcp_erp_port_boxed(unit->port);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_LUN_BOXED:
		ZFCP_LOG_NORMAL("unit needs to be reopened (adapter %s, "
				"wwpn=0x%016Lx, fcp_lun=0x%016Lx)\n",
				zfcp_get_busid_by_unit(unit),
				unit->port->wwpn, unit->fcp_lun);
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_lboxed");
		zfcp_erp_unit_boxed(unit);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
			| ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			/* re-establish link to port */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
 			zfcp_test_link(unit->port);
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* FIXME(hw) need proper specs for proper action */
			/* let scsi stack deal with retries and escalation */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			break;
		default:
			ZFCP_LOG_NORMAL
 			    ("Unknown status qualifier 0x%x arrived.\n",
			     header->fsf_status_qual.word[0]);
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "fsf_sq_inval:");
			debug_exception(fsf_req->adapter->erp_dbf, 0,
					&header->fsf_status_qual.word[0],
					sizeof(u32));
			break;
		}
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_GOOD:
		break;

	case FSF_FCP_RSP_AVAILABLE:
		break;

	default:
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&header->fsf_status, sizeof(u32));
		break;
	}

 skip_fsfstatus:
	if (fsf_req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT) {
		retval =
		    zfcp_fsf_send_fcp_command_task_management_handler(fsf_req);
	} else {
		retval = zfcp_fsf_send_fcp_command_task_handler(fsf_req);
		fsf_req->unit = NULL;
		zfcp_unit_put(unit);
	}
	return retval;
}

/*
 * function:    zfcp_fsf_send_fcp_command_task_handler
 *
 * purpose:	evaluates FCP_RSP IU
 *
 * returns:	
 */
static int
zfcp_fsf_send_fcp_command_task_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;
	struct scsi_cmnd *scpnt;
	struct fcp_rsp_iu *fcp_rsp_iu = (struct fcp_rsp_iu *)
	    &(fsf_req->qtcb->bottom.io.fcp_rsp);
	struct fcp_cmnd_iu *fcp_cmnd_iu = (struct fcp_cmnd_iu *)
	    &(fsf_req->qtcb->bottom.io.fcp_cmnd);
	u32 sns_len;
	char *fcp_rsp_info = zfcp_get_fcp_rsp_info_ptr(fcp_rsp_iu);
	unsigned long flags;
	struct zfcp_unit *unit = fsf_req->unit;

	read_lock_irqsave(&fsf_req->adapter->abort_lock, flags);
	scpnt = (struct scsi_cmnd *) fsf_req->data;
	if (unlikely(!scpnt)) {
		ZFCP_LOG_DEBUG
		    ("Command with fsf_req %p is not associated to "
		     "a scsi command anymore. Aborted?\n", fsf_req);
		goto out;
	}
	if (unlikely(fsf_req->status & ZFCP_STATUS_FSFREQ_ABORTED)) {
		/* FIXME: (design) mid-layer should handle DID_ABORT like
		 *        DID_SOFT_ERROR by retrying the request for devices
		 *        that allow retries.
		 */
		ZFCP_LOG_DEBUG("Setting DID_SOFT_ERROR and SUGGEST_RETRY\n");
		set_host_byte(&scpnt->result, DID_SOFT_ERROR);
		set_driver_byte(&scpnt->result, SUGGEST_RETRY);
		goto skip_fsfstatus;
	}

	if (unlikely(fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR)) {
		ZFCP_LOG_DEBUG("Setting DID_ERROR\n");
		set_host_byte(&scpnt->result, DID_ERROR);
		goto skip_fsfstatus;
	}

	/* set message byte of result in SCSI command */
	scpnt->result |= COMMAND_COMPLETE << 8;

	/*
	 * copy SCSI status code of FCP_STATUS of FCP_RSP IU to status byte
	 * of result in SCSI command
	 */
	scpnt->result |= fcp_rsp_iu->scsi_status;
	if (unlikely(fcp_rsp_iu->scsi_status)) {
		/* DEBUG */
		ZFCP_LOG_DEBUG("status for SCSI Command:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      scpnt->cmnd, scpnt->cmd_len);
		ZFCP_LOG_DEBUG("SCSI status code 0x%x\n",
				fcp_rsp_iu->scsi_status);
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (void *) fcp_rsp_iu, sizeof (struct fcp_rsp_iu));
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      zfcp_get_fcp_sns_info_ptr(fcp_rsp_iu),
			      fcp_rsp_iu->fcp_sns_len);
	}

	/* check FCP_RSP_INFO */
	if (unlikely(fcp_rsp_iu->validity.bits.fcp_rsp_len_valid)) {
		ZFCP_LOG_DEBUG("rsp_len is valid\n");
		switch (fcp_rsp_info[3]) {
		case RSP_CODE_GOOD:
			/* ok, continue */
			ZFCP_LOG_TRACE("no failure or Task Management "
				       "Function complete\n");
			set_host_byte(&scpnt->result, DID_OK);
			break;
		case RSP_CODE_LENGTH_MISMATCH:
			/* hardware bug */
			ZFCP_LOG_NORMAL("bug: FCP response code indictates "
					"that the fibrechannel protocol data "
					"length differs from the burst length. "
					"The problem occured on unit 0x%016Lx "
					"on port 0x%016Lx on adapter %s",
					unit->fcp_lun,
					unit->port->wwpn,
					zfcp_get_busid_by_unit(unit));
			/* dump SCSI CDB as prepared by zfcp */
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
				      (char *) &fsf_req->qtcb->
				      bottom.io.fcp_cmnd, FSF_FCP_CMND_SIZE);
			set_host_byte(&scpnt->result, DID_ERROR);
			goto skip_fsfstatus;
		case RSP_CODE_FIELD_INVALID:
			/* driver or hardware bug */
			ZFCP_LOG_NORMAL("bug: FCP response code indictates "
					"that the fibrechannel protocol data "
					"fields were incorrectly set up. "
					"The problem occured on the unit "
					"0x%016Lx on port 0x%016Lx on "
					"adapter %s",
					unit->fcp_lun,
					unit->port->wwpn,
					zfcp_get_busid_by_unit(unit));
			/* dump SCSI CDB as prepared by zfcp */
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
				      (char *) &fsf_req->qtcb->
				      bottom.io.fcp_cmnd, FSF_FCP_CMND_SIZE);
			set_host_byte(&scpnt->result, DID_ERROR);
			goto skip_fsfstatus;
		case RSP_CODE_RO_MISMATCH:
			/* hardware bug */
			ZFCP_LOG_NORMAL("bug: The FCP response code indicates "
					"that conflicting  values for the "
					"fibrechannel payload offset from the "
					"header were found. "
					"The problem occured on unit 0x%016Lx "
					"on port 0x%016Lx on adapter %s.\n",
					unit->fcp_lun,
					unit->port->wwpn,
					zfcp_get_busid_by_unit(unit));
			/* dump SCSI CDB as prepared by zfcp */
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
				      (char *) &fsf_req->qtcb->
				      bottom.io.fcp_cmnd, FSF_FCP_CMND_SIZE);
			set_host_byte(&scpnt->result, DID_ERROR);
			goto skip_fsfstatus;
		default:
			ZFCP_LOG_NORMAL("bug: An invalid FCP response "
					"code was detected for a command. "
					"The problem occured on the unit "
					"0x%016Lx on port 0x%016Lx on "
					"adapter %s (debug info 0x%x)\n",
					unit->fcp_lun,
					unit->port->wwpn,
					zfcp_get_busid_by_unit(unit),
					fcp_rsp_info[3]);
			/* dump SCSI CDB as prepared by zfcp */
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
				      (char *) &fsf_req->qtcb->
				      bottom.io.fcp_cmnd, FSF_FCP_CMND_SIZE);
			set_host_byte(&scpnt->result, DID_ERROR);
			goto skip_fsfstatus;
		}
	}

	/* check for sense data */
	if (unlikely(fcp_rsp_iu->validity.bits.fcp_sns_len_valid)) {
		sns_len = FSF_FCP_RSP_SIZE -
		    sizeof (struct fcp_rsp_iu) + fcp_rsp_iu->fcp_rsp_len;
		ZFCP_LOG_TRACE("room for %i bytes sense data in QTCB\n",
			       sns_len);
		sns_len = min(sns_len, (u32) SCSI_SENSE_BUFFERSIZE);
		ZFCP_LOG_TRACE("room for %i bytes sense data in SCSI command\n",
			       SCSI_SENSE_BUFFERSIZE);
		sns_len = min(sns_len, fcp_rsp_iu->fcp_sns_len);
		ZFCP_LOG_TRACE("scpnt->result =0x%x, command was:\n",
			       scpnt->result);
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE,
			      (void *) &scpnt->cmnd, scpnt->cmd_len);

		ZFCP_LOG_TRACE("%i bytes sense data provided by FCP\n",
			       fcp_rsp_iu->fcp_sns_len);
		memcpy(&scpnt->sense_buffer,
		       zfcp_get_fcp_sns_info_ptr(fcp_rsp_iu), sns_len);
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE,
			      (void *) &scpnt->sense_buffer, sns_len);
	}

	/* check for overrun */
	if (unlikely(fcp_rsp_iu->validity.bits.fcp_resid_over)) {
		ZFCP_LOG_INFO("A data overrun was detected for a command. "
			      "unit 0x%016Lx, port 0x%016Lx, adapter %s. "
			      "The response data length is "
			      "%d, the original length was %d.\n",
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit),
			      fcp_rsp_iu->fcp_resid,
			      (int) zfcp_get_fcp_dl(fcp_cmnd_iu));
	}

	/* check for underrun */
	if (unlikely(fcp_rsp_iu->validity.bits.fcp_resid_under)) {
		ZFCP_LOG_INFO("A data underrun was detected for a command. "
			      "unit 0x%016Lx, port 0x%016Lx, adapter %s. "
			      "The response data length is "
			      "%d, the original length was %d.\n",
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit),
			      fcp_rsp_iu->fcp_resid,
			      (int) zfcp_get_fcp_dl(fcp_cmnd_iu));

		scpnt->resid = fcp_rsp_iu->fcp_resid;
		if (scpnt->request_bufflen - scpnt->resid < scpnt->underflow)
			set_host_byte(&scpnt->result, DID_ERROR);
	}

 skip_fsfstatus:
	ZFCP_LOG_DEBUG("scpnt->result =0x%x\n", scpnt->result);

	if (scpnt->result != 0)
		zfcp_scsi_dbf_event_result("erro", 3, fsf_req->adapter, scpnt, fsf_req);
	else if (scpnt->retries > 0)
		zfcp_scsi_dbf_event_result("retr", 4, fsf_req->adapter, scpnt, fsf_req);
	else
		zfcp_scsi_dbf_event_result("norm", 6, fsf_req->adapter, scpnt, fsf_req);

	/* cleanup pointer (need this especially for abort) */
	scpnt->host_scribble = NULL;

	/* always call back */
	(scpnt->scsi_done) (scpnt);

	/*
	 * We must hold this lock until scsi_done has been called.
	 * Otherwise we may call scsi_done after abort regarding this
	 * command has completed.
	 * Note: scsi_done must not block!
	 */
 out:
	read_unlock_irqrestore(&fsf_req->adapter->abort_lock, flags);
	return retval;
}

/*
 * function:    zfcp_fsf_send_fcp_command_task_management_handler
 *
 * purpose:	evaluates FCP_RSP IU
 *
 * returns:	
 */
static int
zfcp_fsf_send_fcp_command_task_management_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;
	struct fcp_rsp_iu *fcp_rsp_iu = (struct fcp_rsp_iu *)
	    &(fsf_req->qtcb->bottom.io.fcp_rsp);
	char *fcp_rsp_info = zfcp_get_fcp_rsp_info_ptr(fcp_rsp_iu);
	struct zfcp_unit *unit = (struct zfcp_unit *) fsf_req->data;

	del_timer(&fsf_req->adapter->scsi_er_timer);
	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		fsf_req->status |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
		goto skip_fsfstatus;
	}

	/* check FCP_RSP_INFO */
	switch (fcp_rsp_info[3]) {
	case RSP_CODE_GOOD:
		/* ok, continue */
		ZFCP_LOG_DEBUG("no failure or Task Management "
			       "Function complete\n");
		break;
	case RSP_CODE_TASKMAN_UNSUPP:
		ZFCP_LOG_NORMAL("bug: A reuested task management function "
				"is not supported on the target device "
				"unit 0x%016Lx, port 0x%016Lx, adapter %s\n ",
				unit->fcp_lun,
				unit->port->wwpn,
				zfcp_get_busid_by_unit(unit));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_TMFUNCNOTSUPP;
		break;
	case RSP_CODE_TASKMAN_FAILED:
		ZFCP_LOG_NORMAL("bug: A reuested task management function "
				"failed to complete successfully. "
				"unit 0x%016Lx, port 0x%016Lx, adapter %s.\n",
				unit->fcp_lun,
				unit->port->wwpn,
				zfcp_get_busid_by_unit(unit));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
		break;
	default:
		ZFCP_LOG_NORMAL("bug: An invalid FCP response "
				"code was detected for a command. "
				"unit 0x%016Lx, port 0x%016Lx, adapter %s "
				"(debug info 0x%x)\n",
				unit->fcp_lun,
				unit->port->wwpn,
				zfcp_get_busid_by_unit(unit),
				fcp_rsp_info[3]);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
	}

      skip_fsfstatus:
	return retval;
}


/*
 * function:    zfcp_fsf_control_file
 *
 * purpose:     Initiator of the control file upload/download FSF requests
 *
 * returns:     0           - FSF request is successfuly created and queued
 *              -EOPNOTSUPP - The FCP adapter does not have Control File support
 *              -EINVAL     - Invalid direction specified
 *              -ENOMEM     - Insufficient memory
 *              -EPERM      - Cannot create FSF request or place it in QDIO queue
 */
int
zfcp_fsf_control_file(struct zfcp_adapter *adapter,
                      struct zfcp_fsf_req **fsf_req_ptr,
                      u32 fsf_command,
                      u32 option,
                      struct zfcp_sg_list *sg_list)
{
	struct zfcp_fsf_req *fsf_req;
	struct fsf_qtcb_bottom_support *bottom;
	volatile struct qdio_buffer_element *sbale;
	struct timer_list *timer;
	unsigned long lock_flags;
	int req_flags = 0;
	int direction;
	int retval = 0;

	if (!(adapter->adapter_features & FSF_FEATURE_CFDC)) {
		ZFCP_LOG_INFO("cfdc not supported (adapter %s)\n",
			      zfcp_get_busid_by_adapter(adapter));
		retval = -EOPNOTSUPP;
		goto out;
	}

	switch (fsf_command) {

	case FSF_QTCB_DOWNLOAD_CONTROL_FILE:
		direction = SBAL_FLAGS0_TYPE_WRITE;
		if ((option != FSF_CFDC_OPTION_FULL_ACCESS) &&
		    (option != FSF_CFDC_OPTION_RESTRICTED_ACCESS))
			req_flags = ZFCP_WAIT_FOR_SBAL;
		break;

	case FSF_QTCB_UPLOAD_CONTROL_FILE:
		direction = SBAL_FLAGS0_TYPE_READ;
		break;

	default:
		ZFCP_LOG_INFO("Invalid FSF command code 0x%08x\n", fsf_command);
		retval = -EINVAL;
		goto out;
	}

	timer = kmalloc(sizeof(struct timer_list), GFP_KERNEL);
	if (!timer) {
		retval = -ENOMEM;
		goto out;
 	}

	retval = zfcp_fsf_req_create(adapter, fsf_command, req_flags,
				     NULL, &lock_flags, &fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Could not create FSF request for the "
			      "adapter %s\n",
			zfcp_get_busid_by_adapter(adapter));
		retval = -EPERM;
		goto unlock_queue_lock;
	}

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
	sbale[0].flags |= direction;

	bottom = &fsf_req->qtcb->bottom.support;
	bottom->operation_subtype = FSF_CFDC_OPERATION_SUBTYPE;
	bottom->option = option;

	if (sg_list->count > 0) {
		int bytes;

		bytes = zfcp_qdio_sbals_from_sg(fsf_req, direction,
						sg_list->sg, sg_list->count,
						ZFCP_MAX_SBALS_PER_REQ);
                if (bytes != ZFCP_CFDC_MAX_CONTROL_FILE_SIZE) {
			ZFCP_LOG_INFO(
				"error: Could not create sufficient number of "
				"SBALS for an FSF request to the adapter %s\n",
				zfcp_get_busid_by_adapter(adapter));
			retval = -ENOMEM;
			goto free_fsf_req;
		}
	} else
		sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	init_timer(timer);
	timer->function = zfcp_fsf_request_timeout_handler;
	timer->data = (unsigned long) adapter;
	timer->expires = ZFCP_FSF_REQUEST_TIMEOUT;

	retval = zfcp_fsf_req_send(fsf_req, timer);
	if (retval < 0) {
		ZFCP_LOG_INFO("initiation of cfdc up/download failed"
			      "(adapter %s)\n",
			      zfcp_get_busid_by_adapter(adapter));
		retval = -EPERM;
		goto free_fsf_req;
	}
	write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);

	ZFCP_LOG_NORMAL("Control file %s FSF request has been sent to the "
			"adapter %s\n",
			fsf_command == FSF_QTCB_DOWNLOAD_CONTROL_FILE ?
			"download" : "upload",
			zfcp_get_busid_by_adapter(adapter));

	wait_event(fsf_req->completion_wq,
	           fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	*fsf_req_ptr = fsf_req;
	del_timer_sync(timer);
	goto free_timer;

 free_fsf_req:
	zfcp_fsf_req_free(fsf_req);
 unlock_queue_lock:
	write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);
 free_timer:
	kfree(timer);
 out:
	return retval;
}


/*
 * function:    zfcp_fsf_control_file_handler
 *
 * purpose:     Handler of the control file upload/download FSF requests
 *
 * returns:     0       - FSF request successfuly processed
 *              -EAGAIN - Operation has to be repeated because of a temporary problem
 *              -EACCES - There is no permission to execute an operation
 *              -EPERM  - The control file is not in a right format
 *              -EIO    - There is a problem with the FCP adapter
 *              -EINVAL - Invalid operation
 *              -EFAULT - User space memory I/O operation fault
 */
static int
zfcp_fsf_control_file_handler(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct fsf_qtcb_header *header = &fsf_req->qtcb->header;
	struct fsf_qtcb_bottom_support *bottom = &fsf_req->qtcb->bottom.support;
	int retval = 0;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		retval = -EINVAL;
		goto skip_fsfstatus;
	}

	switch (header->fsf_status) {

	case FSF_GOOD:
		ZFCP_LOG_NORMAL(
			"The FSF request has been successfully completed "
			"on the adapter %s\n",
			zfcp_get_busid_by_adapter(adapter));
		break;

	case FSF_OPERATION_PARTIALLY_SUCCESSFUL:
		if (bottom->operation_subtype == FSF_CFDC_OPERATION_SUBTYPE) {
			switch (header->fsf_status_qual.word[0]) {

			case FSF_SQ_CFDC_HARDENED_ON_SE:
				ZFCP_LOG_NORMAL(
					"CFDC on the adapter %s has being "
					"hardened on primary and secondary SE\n",
					zfcp_get_busid_by_adapter(adapter));
				break;

			case FSF_SQ_CFDC_COULD_NOT_HARDEN_ON_SE:
				ZFCP_LOG_NORMAL(
					"CFDC of the adapter %s could not "
					"be saved on the SE\n",
					zfcp_get_busid_by_adapter(adapter));
				break;

			case FSF_SQ_CFDC_COULD_NOT_HARDEN_ON_SE2:
				ZFCP_LOG_NORMAL(
					"CFDC of the adapter %s could not "
					"be copied to the secondary SE\n",
					zfcp_get_busid_by_adapter(adapter));
				break;

			default:
				ZFCP_LOG_NORMAL(
					"CFDC could not be hardened "
					"on the adapter %s\n",
					zfcp_get_busid_by_adapter(adapter));
			}
		}
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EAGAIN;
		break;

	case FSF_AUTHORIZATION_FAILURE:
		ZFCP_LOG_NORMAL(
			"Adapter %s does not accept privileged commands\n",
			zfcp_get_busid_by_adapter(adapter));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EACCES;
		break;

	case FSF_CFDC_ERROR_DETECTED:
		ZFCP_LOG_NORMAL(
			"Error at position %d in the CFDC, "
			"CFDC is discarded by the adapter %s\n",
			header->fsf_status_qual.word[0],
			zfcp_get_busid_by_adapter(adapter));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EPERM;
		break;

	case FSF_CONTROL_FILE_UPDATE_ERROR:
		ZFCP_LOG_NORMAL(
			"Adapter %s cannot harden the control file, "
			"file is discarded\n",
			zfcp_get_busid_by_adapter(adapter));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EIO;
		break;

	case FSF_CONTROL_FILE_TOO_LARGE:
		ZFCP_LOG_NORMAL(
			"Control file is too large, file is discarded "
			"by the adapter %s\n",
			zfcp_get_busid_by_adapter(adapter));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EIO;
		break;

	case FSF_ACCESS_CONFLICT_DETECTED:
		if (bottom->operation_subtype == FSF_CFDC_OPERATION_SUBTYPE)
			ZFCP_LOG_NORMAL(
				"CFDC has been discarded by the adapter %s, "
				"because activation would impact "
				"%d active connection(s)\n",
				zfcp_get_busid_by_adapter(adapter),
				header->fsf_status_qual.word[0]);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EIO;
		break;

	case FSF_CONFLICTS_OVERRULED:
		if (bottom->operation_subtype == FSF_CFDC_OPERATION_SUBTYPE)
			ZFCP_LOG_NORMAL(
				"CFDC has been activated on the adapter %s, "
				"but activation has impacted "
				"%d active connection(s)\n",
				zfcp_get_busid_by_adapter(adapter),
				header->fsf_status_qual.word[0]);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EIO;
		break;

	case FSF_UNKNOWN_OP_SUBTYPE:
		ZFCP_LOG_NORMAL("unknown operation subtype (adapter: %s, "
				"op_subtype=0x%x)\n",
				zfcp_get_busid_by_adapter(adapter),
				bottom->operation_subtype);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EINVAL;
		break;

	case FSF_INVALID_COMMAND_OPTION:
		ZFCP_LOG_NORMAL(
			"Invalid option 0x%x has been specified "
			"in QTCB bottom sent to the adapter %s\n",
			bottom->option,
			zfcp_get_busid_by_adapter(adapter));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EINVAL;
		break;

	default:
		ZFCP_LOG_NORMAL(
			"bug: An unknown/unexpected FSF status 0x%08x "
			"was presented on the adapter %s\n",
			header->fsf_status,
			zfcp_get_busid_by_adapter(adapter));
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_sq_inval");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
			&header->fsf_status_qual.word[0], sizeof(u32));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EINVAL;
		break;
	}

skip_fsfstatus:
	return retval;
}

static inline int
zfcp_fsf_req_sbal_check(unsigned long *flags,
			struct zfcp_qdio_queue *queue, int needed)
{
	write_lock_irqsave(&queue->queue_lock, *flags);
	if (likely(atomic_read(&queue->free_count) >= needed))
		return 1;
	write_unlock_irqrestore(&queue->queue_lock, *flags);
	return 0;
}

/*
 * set qtcb pointer in fsf_req and initialize QTCB
 */
static inline void
zfcp_fsf_req_qtcb_init(struct zfcp_fsf_req *fsf_req)
{
	if (likely(fsf_req->qtcb != NULL)) {
		fsf_req->qtcb->prefix.req_seq_no = fsf_req->adapter->fsf_req_seq_no;
		fsf_req->qtcb->prefix.req_id = (unsigned long)fsf_req;
		fsf_req->qtcb->prefix.ulp_info = ZFCP_ULP_INFO_VERSION;
		fsf_req->qtcb->prefix.qtcb_type = fsf_qtcb_type[fsf_req->fsf_command];
		fsf_req->qtcb->prefix.qtcb_version = ZFCP_QTCB_VERSION;
		fsf_req->qtcb->header.req_handle = (unsigned long)fsf_req;
		fsf_req->qtcb->header.fsf_command = fsf_req->fsf_command;
	}
}

/**
 * zfcp_fsf_req_sbal_get - try to get one SBAL in the request queue
 * @adapter: adapter for which request queue is examined
 * @req_flags: flags indicating whether to wait for needed SBAL or not
 * @lock_flags: lock_flags if queue_lock is taken
 * Return: 0 on success, otherwise -EIO, or -ERESTARTSYS
 * Locks: lock adapter->request_queue->queue_lock on success
 */
static int
zfcp_fsf_req_sbal_get(struct zfcp_adapter *adapter, int req_flags,
		      unsigned long *lock_flags)
{
        long ret;
        struct zfcp_qdio_queue *req_queue = &adapter->request_queue;

        if (unlikely(req_flags & ZFCP_WAIT_FOR_SBAL)) {
                ret = wait_event_interruptible_timeout(adapter->request_wq,
			zfcp_fsf_req_sbal_check(lock_flags, req_queue, 1),
						       ZFCP_SBAL_TIMEOUT);
		if (ret < 0)
			return ret;
		if (!ret)
			return -EIO;
        } else if (!zfcp_fsf_req_sbal_check(lock_flags, req_queue, 1))
                return -EIO;

        return 0;
}

/*
 * function:    zfcp_fsf_req_create
 *
 * purpose:	create an FSF request at the specified adapter and
 *		setup common fields
 *
 * returns:	-ENOMEM if there was insufficient memory for a request
 *              -EIO if no qdio buffers could be allocate to the request
 *              -EINVAL/-EPERM on bug conditions in req_dequeue
 *              0 in success
 *
 * note:        The created request is returned by reference.
 *
 * locks:	lock of concerned request queue must not be held,
 *		but is held on completion (write, irqsave)
 */
int
zfcp_fsf_req_create(struct zfcp_adapter *adapter, u32 fsf_cmd, int req_flags,
		    mempool_t *pool, unsigned long *lock_flags,
		    struct zfcp_fsf_req **fsf_req_p)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *fsf_req = NULL;
	int ret = 0;
	struct zfcp_qdio_queue *req_queue = &adapter->request_queue;

	/* allocate new FSF request */
	fsf_req = zfcp_fsf_req_alloc(pool, req_flags);
	if (unlikely(NULL == fsf_req)) {
		ZFCP_LOG_DEBUG("error: Could not put an FSF request into"
			       "the outbound (send) queue.\n");
		ret = -ENOMEM;
		goto failed_fsf_req;
	}

	fsf_req->adapter = adapter;
	fsf_req->fsf_command = fsf_cmd;

        zfcp_fsf_req_qtcb_init(fsf_req);

	/* initialize waitqueue which may be used to wait on 
	   this request completion */
	init_waitqueue_head(&fsf_req->completion_wq);

        ret = zfcp_fsf_req_sbal_get(adapter, req_flags, lock_flags);
        if(ret < 0) {
                goto failed_sbals;
	}

	/*
	 * We hold queue_lock here. Check if QDIOUP is set and let request fail
	 * if it is not set (see also *_open_qdio and *_close_qdio).
	 */

	if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status)) {
		write_unlock_irqrestore(&req_queue->queue_lock, *lock_flags);
		ret = -EIO;
		goto failed_sbals;
	}

	if (fsf_req->qtcb) {
		fsf_req->seq_no = adapter->fsf_req_seq_no;
		fsf_req->qtcb->prefix.req_seq_no = adapter->fsf_req_seq_no;
	}
	fsf_req->sbal_number = 1;
	fsf_req->sbal_first = req_queue->free_index;
	fsf_req->sbal_curr = req_queue->free_index;
        fsf_req->sbale_curr = 1;

	if (likely(req_flags & ZFCP_REQ_AUTO_CLEANUP)) {
		fsf_req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	}

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);

	/* setup common SBALE fields */
	sbale[0].addr = fsf_req;
	sbale[0].flags |= SBAL_FLAGS0_COMMAND;
	if (likely(fsf_req->qtcb != NULL)) {
		sbale[1].addr = (void *) fsf_req->qtcb;
		sbale[1].length = sizeof(struct fsf_qtcb);
	}

	ZFCP_LOG_TRACE("got %i free BUFFERs starting at index %i\n",
                       fsf_req->sbal_number, fsf_req->sbal_first);

	goto success;

 failed_sbals:
/* dequeue new FSF request previously enqueued */
	zfcp_fsf_req_free(fsf_req);
	fsf_req = NULL;

 failed_fsf_req:
	write_lock_irqsave(&req_queue->queue_lock, *lock_flags);
 success:
	*fsf_req_p = fsf_req;
	return ret;
}

/*
 * function:    zfcp_fsf_req_send
 *
 * purpose:	start transfer of FSF request via QDIO
 *
 * returns:	0 - request transfer succesfully started
 *		!0 - start of request transfer failed
 */
static int
zfcp_fsf_req_send(struct zfcp_fsf_req *fsf_req, struct timer_list *timer)
{
	struct zfcp_adapter *adapter;
	struct zfcp_qdio_queue *req_queue;
	volatile struct qdio_buffer_element *sbale;
	int inc_seq_no;
	int new_distance_from_int;
	unsigned long flags;
	int retval = 0;

	adapter = fsf_req->adapter;
	req_queue = &adapter->request_queue,


	/* FIXME(debug): remove it later */
	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_first, 0);
	ZFCP_LOG_DEBUG("SBALE0 flags=0x%x\n", sbale[0].flags);
	ZFCP_LOG_TRACE("HEX DUMP OF SBALE1 PAYLOAD:\n");
	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE, (char *) sbale[1].addr,
		      sbale[1].length);

	/* put allocated FSF request at list tail */
	spin_lock_irqsave(&adapter->fsf_req_list_lock, flags);
	list_add_tail(&fsf_req->list, &adapter->fsf_req_list_head);
	spin_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);

	inc_seq_no = (fsf_req->qtcb != NULL);

	/* figure out expiration time of timeout and start timeout */
	if (unlikely(timer)) {
		timer->expires += jiffies;
		add_timer(timer);
	}

	ZFCP_LOG_TRACE("request queue of adapter %s: "
		       "next free SBAL is %i, %i free SBALs\n",
		       zfcp_get_busid_by_adapter(adapter),
		       req_queue->free_index,
		       atomic_read(&req_queue->free_count));

	ZFCP_LOG_DEBUG("calling do_QDIO adapter %s, flags=0x%x, queue_no=%i, "
		       "index_in_queue=%i, count=%i, buffers=%p\n",
		       zfcp_get_busid_by_adapter(adapter),
		       QDIO_FLAG_SYNC_OUTPUT,
		       0, fsf_req->sbal_first, fsf_req->sbal_number,
		       &req_queue->buffer[fsf_req->sbal_first]);

	/*
	 * adjust the number of free SBALs in request queue as well as
	 * position of first one
	 */
	atomic_sub(fsf_req->sbal_number, &req_queue->free_count);
	ZFCP_LOG_TRACE("free_count=%d\n", atomic_read(&req_queue->free_count));
	req_queue->free_index += fsf_req->sbal_number;	  /* increase */
	req_queue->free_index %= QDIO_MAX_BUFFERS_PER_Q;  /* wrap if needed */
	new_distance_from_int = zfcp_qdio_determine_pci(req_queue, fsf_req);

	fsf_req->issued = get_clock();

	retval = do_QDIO(adapter->ccw_device,
			 QDIO_FLAG_SYNC_OUTPUT,
			 0, fsf_req->sbal_first, fsf_req->sbal_number, NULL);

	if (unlikely(retval)) {
		/* Queues are down..... */
		retval = -EIO;
		/*
		 * FIXME(potential race):
		 * timer might be expired (absolutely unlikely)
		 */
		if (timer)
			del_timer(timer);
		spin_lock_irqsave(&adapter->fsf_req_list_lock, flags);
		list_del(&fsf_req->list);
		spin_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);
		/*
		 * adjust the number of free SBALs in request queue as well as
		 * position of first one
		 */
		zfcp_qdio_zero_sbals(req_queue->buffer,
				     fsf_req->sbal_first, fsf_req->sbal_number);
		atomic_add(fsf_req->sbal_number, &req_queue->free_count);
		req_queue->free_index -= fsf_req->sbal_number;	 /* increase */
		req_queue->free_index += QDIO_MAX_BUFFERS_PER_Q;
		req_queue->free_index %= QDIO_MAX_BUFFERS_PER_Q; /* wrap */
		ZFCP_LOG_DEBUG
			("error: do_QDIO failed. Buffers could not be enqueued "
			 "to request queue.\n");
	} else {
		req_queue->distance_from_int = new_distance_from_int;
		/*
		 * increase FSF sequence counter -
		 * this must only be done for request successfully enqueued to
		 * QDIO this rejected requests may be cleaned up by calling
		 * routines  resulting in missing sequence counter values
		 * otherwise,
		 */

		/* Don't increase for unsolicited status */
		if (inc_seq_no)
			adapter->fsf_req_seq_no++;

		/* count FSF requests pending */
		atomic_inc(&adapter->fsf_reqs_active);
	}
	return retval;
}

#undef ZFCP_LOG_AREA
