/*
 * zfcp device driver
 *
 * Implementation of FSF commands.
 *
 * Copyright IBM Corporation 2002, 2008
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
static void zfcp_fsf_control_file_handler(struct zfcp_fsf_req *);
static inline int zfcp_fsf_req_sbal_check(
	unsigned long *, struct zfcp_qdio_queue *, int);
static inline int zfcp_use_one_sbal(
	struct scatterlist *, int, struct scatterlist *, int);
static struct zfcp_fsf_req *zfcp_fsf_req_alloc(mempool_t *, int);
static int zfcp_fsf_req_send(struct zfcp_fsf_req *);
static int zfcp_fsf_protstatus_eval(struct zfcp_fsf_req *);
static int zfcp_fsf_fsfstatus_eval(struct zfcp_fsf_req *);
static int zfcp_fsf_fsfstatus_qual_eval(struct zfcp_fsf_req *);
static void zfcp_fsf_link_down_info_eval(struct zfcp_fsf_req *, u8,
	struct fsf_link_down_info *);
static int zfcp_fsf_req_dispatch(struct zfcp_fsf_req *);

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

static void zfcp_act_eval_err(struct zfcp_adapter *adapter, u32 table)
{
	u16 subtable = (table & 0xffff0000) >> 16;
	u16 rule = table & 0xffff;

	if (subtable > 0 &&
	    subtable < ARRAY_SIZE(zfcp_act_subtable_type)) {
		dev_warn(&adapter->ccw_device->dev,
			 "Access denied in subtable %s, rule %d.\n",
			 zfcp_act_subtable_type[subtable], rule);
	}
}

static void zfcp_fsf_access_denied_port(struct zfcp_fsf_req *req,
					struct zfcp_port *port)
{
	struct fsf_qtcb_header *header = &req->qtcb->header;
	dev_warn(&req->adapter->ccw_device->dev,
		 "Access denied, cannot send command to port 0x%016Lx.\n",
		 port->wwpn);
	zfcp_act_eval_err(req->adapter, header->fsf_status_qual.halfword[0]);
	zfcp_act_eval_err(req->adapter, header->fsf_status_qual.halfword[1]);
	zfcp_erp_port_access_denied(port, 55, req);
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

static void zfcp_fsf_access_denied_unit(struct zfcp_fsf_req *req,
					struct zfcp_unit *unit)
{
	struct fsf_qtcb_header *header = &req->qtcb->header;
	dev_warn(&req->adapter->ccw_device->dev,
		 "Access denied for unit 0x%016Lx on port 0x%016Lx.\n",
		 unit->fcp_lun, unit->port->wwpn);
	zfcp_act_eval_err(req->adapter, header->fsf_status_qual.halfword[0]);
	zfcp_act_eval_err(req->adapter, header->fsf_status_qual.halfword[1]);
	zfcp_erp_unit_access_denied(unit, 59, req);
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

static void zfcp_fsf_class_not_supp(struct zfcp_fsf_req *req)
{
	dev_err(&req->adapter->ccw_device->dev,
		"Required FC class not supported by adapter, "
		"shutting down adapter.\n");
	zfcp_erp_adapter_shutdown(req->adapter, 0, 123, req);
	req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

/****************************************************************/
/*************** FSF related Functions  *************************/
/****************************************************************/

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
		size = sizeof(struct zfcp_fsf_req_qtcb);

	if (likely(pool))
		ptr = mempool_alloc(pool, GFP_ATOMIC);
	else {
		if (req_flags & ZFCP_REQ_NO_QTCB)
			ptr = kmalloc(size, GFP_ATOMIC);
		else
			ptr = kmem_cache_alloc(zfcp_data.fsf_req_qtcb_cache,
					       GFP_ATOMIC);
	}

	if (unlikely(!ptr))
		goto out;

	memset(ptr, 0, size);

	if (req_flags & ZFCP_REQ_NO_QTCB) {
		fsf_req = (struct zfcp_fsf_req *) ptr;
	} else {
		fsf_req = &((struct zfcp_fsf_req_qtcb *) ptr)->fsf_req;
		fsf_req->qtcb =	&((struct zfcp_fsf_req_qtcb *) ptr)->qtcb;
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
	if (likely(fsf_req->pool)) {
		mempool_free(fsf_req, fsf_req->pool);
		return;
	}

	if (fsf_req->qtcb) {
		kmem_cache_free(zfcp_data.fsf_req_qtcb_cache, fsf_req);
		return;
	}

	kfree(fsf_req);
}

/*
 * Never ever call this without shutting down the adapter first.
 * Otherwise the adapter would continue using and corrupting s390 storage.
 * Included BUG_ON() call to ensure this is done.
 * ERP is supposed to be the only user of this function.
 */
void zfcp_fsf_req_dismiss_all(struct zfcp_adapter *adapter)
{
	struct zfcp_fsf_req *fsf_req, *tmp;
	unsigned long flags;
	LIST_HEAD(remove_queue);
	unsigned int i;

	BUG_ON(atomic_test_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status));
	spin_lock_irqsave(&adapter->req_list_lock, flags);
	for (i = 0; i < REQUEST_LIST_SIZE; i++)
		list_splice_init(&adapter->req_list[i], &remove_queue);
	spin_unlock_irqrestore(&adapter->req_list_lock, flags);

	list_for_each_entry_safe(fsf_req, tmp, &remove_queue, list) {
		list_del(&fsf_req->list);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
		zfcp_fsf_req_complete(fsf_req);
	}
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
		/*
		 * Note: all cleanup handling is done in the callchain of
		 * the function call-chain below.
		 */
		zfcp_fsf_status_read_handler(fsf_req);
		goto out;
	} else {
		del_timer(&fsf_req->timer);
		zfcp_fsf_protstatus_eval(fsf_req);
	}

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
		/*
		 * lock must not be held here since it will be
		 * grabed by the called routine, too
		 */
		zfcp_fsf_req_free(fsf_req);
	} else {
		/* notify initiator waiting for the requests completion */
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
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY; /* only for SCSI cmnds. */
		goto skip_protstatus;
	}

	/* evaluate FSF Protocol Status */
	switch (qtcb->prefix.prot_status) {

	case FSF_PROT_GOOD:
	case FSF_PROT_FSF_STATUS_PRESENTED:
		break;

	case FSF_PROT_QTCB_VERSION_ERROR:
		dev_err(&adapter->ccw_device->dev,
			"The QTCB version requested by zfcp (0x%x) is not "
			"supported by the FCP adapter (lowest supported 0x%x, "
			"highest supported 0x%x).\n",
			ZFCP_QTCB_VERSION, prot_status_qual->word[0],
			prot_status_qual->word[1]);
		zfcp_erp_adapter_shutdown(adapter, 0, 117, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_SEQ_NUMB_ERROR:
		zfcp_erp_adapter_reopen(adapter, 0, 98, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_RETRY;
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_UNSUPP_QTCB_TYPE:
		dev_err(&adapter->ccw_device->dev,
			"Packet header type used by the device driver is "
			"incompatible with that used on the adapter.\n");
		zfcp_erp_adapter_shutdown(adapter, 0, 118, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_HOST_CONNECTION_INITIALIZING:
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		atomic_set_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
				&(adapter->status));
		break;

	case FSF_PROT_DUPLICATE_REQUEST_ID:
		dev_err(&adapter->ccw_device->dev,
			"The request identifier 0x%Lx is ambiguous.\n",
			(unsigned long long)qtcb->bottom.support.req_handle);
		zfcp_erp_adapter_shutdown(adapter, 0, 78, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_LINK_DOWN:
		zfcp_fsf_link_down_info_eval(fsf_req, 37,
					     &prot_status_qual->link_down_info);
		/* FIXME: reopening adapter now? better wait for link up */
		zfcp_erp_adapter_reopen(adapter, 0, 79, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_REEST_QUEUE:
		/* All ports should be marked as ready to run again */
		zfcp_erp_modify_adapter_status(adapter, 28, NULL,
					       ZFCP_STATUS_COMMON_RUNNING,
					       ZFCP_SET);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED
					| ZFCP_STATUS_COMMON_ERP_FAILED,
					99, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_ERROR_STATE:
		zfcp_erp_adapter_reopen(adapter, 0, 100, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_RETRY;
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	default:
		dev_err(&adapter->ccw_device->dev,
			"Transfer protocol status information"
			"provided by the adapter (0x%x) "
			"is not compatible with the device driver.\n",
			qtcb->prefix.prot_status);
		zfcp_erp_adapter_shutdown(adapter, 0, 119, fsf_req);
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
		dev_err(&fsf_req->adapter->ccw_device->dev,
			"Command issued by the device driver (0x%x) is "
			"not known by the adapter.\n",
			fsf_req->qtcb->header.fsf_command);
		zfcp_erp_adapter_shutdown(fsf_req->adapter, 0, 120, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
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
		dev_err(&fsf_req->adapter->ccw_device->dev,
			"No recommendation could be given for a "
			"problem on the adapter.\n");
		zfcp_erp_adapter_shutdown(fsf_req->adapter, 0, 121, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SQ_ULP_PROGRAMMING_ERROR:
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
	case FSF_SQ_NO_RETRY_POSSIBLE:
	case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
		/* dealt with in the respective functions */
		break;
	default:
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}

	return retval;
}

/**
 * zfcp_fsf_link_down_info_eval - evaluate link down information block
 */
static void
zfcp_fsf_link_down_info_eval(struct zfcp_fsf_req *fsf_req, u8 id,
			     struct fsf_link_down_info *link_down)
{
	struct zfcp_adapter *adapter = fsf_req->adapter;

	if (atomic_test_mask(ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED,
	                     &adapter->status))
		return;

	atomic_set_mask(ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED, &adapter->status);

	if (link_down == NULL)
		goto out;

	switch (link_down->error_code) {
	case FSF_PSQ_LINK_NO_LIGHT:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "no light detected.\n");
		break;
	case FSF_PSQ_LINK_WRAP_PLUG:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "wrap plug detected.\n");
		break;
	case FSF_PSQ_LINK_NO_FCP:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "adjacent node on link does not support FCP.\n");
		break;
	case FSF_PSQ_LINK_FIRMWARE_UPDATE:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "firmware update in progress.\n");
		break;
	case FSF_PSQ_LINK_INVALID_WWPN:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "duplicate or invalid WWPN detected.\n");
		break;
	case FSF_PSQ_LINK_NO_NPIV_SUPPORT:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "no support for NPIV by Fabric.\n");
		break;
	case FSF_PSQ_LINK_NO_FCP_RESOURCES:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "out of resource in FCP daughtercard.\n");
		break;
	case FSF_PSQ_LINK_NO_FABRIC_RESOURCES:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "out of resource in Fabric.\n");
		break;
	case FSF_PSQ_LINK_FABRIC_LOGIN_UNABLE:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "The local link is down: "
			 "unable to login to Fabric.\n");
		break;
	case FSF_PSQ_LINK_WWPN_ASSIGNMENT_CORRUPTED:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "WWPN assignment file corrupted on adapter.\n");
		break;
	case FSF_PSQ_LINK_MODE_TABLE_CURRUPTED:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "Mode table corrupted on adapter.\n");
		break;
	case FSF_PSQ_LINK_NO_WWPN_ASSIGNMENT:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "No WWPN for assignment table on adapter.\n");
		break;
	default:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "The local link to adapter is down.\n");
	}

 out:
	zfcp_erp_adapter_failed(adapter, id, fsf_req);
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
	int retval;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_UNSOLICITED_STATUS,
				     req_flags | ZFCP_REQ_NO_QTCB,
				     adapter->pool.fsf_req_status_read,
				     &lock_flags, &fsf_req);
	if (retval < 0)
		goto failed_req_create;

	sbale = zfcp_qdio_sbale_req(fsf_req);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_STATUS;
        sbale[2].flags |= SBAL_FLAGS_LAST_ENTRY;
        fsf_req->sbale_curr = 2;

	retval = -ENOMEM;
	status_buffer =
		mempool_alloc(adapter->pool.data_status_read, GFP_ATOMIC);
	if (!status_buffer)
		goto failed_buf;
	memset(status_buffer, 0, sizeof (struct fsf_status_read_buffer));
	fsf_req->data = (unsigned long) status_buffer;

	/* insert pointer to respective buffer */
	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->addr = (void *) status_buffer;
	sbale->length = sizeof(struct fsf_status_read_buffer);

	retval = zfcp_fsf_req_send(fsf_req);
	if (retval)
		goto failed_req_send;

	goto out;

 failed_req_send:
	mempool_free(status_buffer, adapter->pool.data_status_read);

 failed_buf:
	zfcp_fsf_req_free(fsf_req);
 failed_req_create:
	zfcp_hba_dbf_event_fsf_unsol("fail", adapter, NULL);
 out:
	write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);
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

	if (!port || (port->d_id != (status_buffer->d_id & ZFCP_DID_MASK)))
		goto out;

	switch (status_buffer->status_subtype) {

	case FSF_STATUS_READ_SUB_CLOSE_PHYS_PORT:
		zfcp_erp_port_reopen(port, 0, 101, fsf_req);
		break;

	case FSF_STATUS_READ_SUB_ERROR_PORT:
		zfcp_erp_port_shutdown(port, 0, 122, fsf_req);
		break;
	}
 out:
	return 0;
}

static void zfcp_fsf_bit_error_threshold(struct zfcp_fsf_req *req)
{
	struct zfcp_adapter *adapter = req->adapter;
	struct fsf_status_read_buffer *buf =
		(struct fsf_status_read_buffer *) req->data;
	struct fsf_bit_error_payload *err =
		(struct fsf_bit_error_payload *) buf->payload;
	dev_warn(&adapter->ccw_device->dev,
		 "Warning: bit error threshold data "
		 "received for the adapter: "
		 "link failures = %i, loss of sync errors = %i, "
		 "loss of signal errors = %i, "
		 "primitive sequence errors = %i, "
		 "invalid transmission word errors = %i, "
		 "CRC errors = %i).\n",
		 err->link_failure_error_count,
		 err->loss_of_sync_error_count,
		 err->loss_of_signal_error_count,
		 err->primitive_sequence_error_count,
		 err->invalid_transmission_word_error_count,
		 err->crc_error_count);
	dev_warn(&adapter->ccw_device->dev,
		 "Additional bit error threshold data of the adapter: "
		 "primitive sequence event time-outs = %i, "
		 "elastic buffer overrun errors = %i, "
		 "advertised receive buffer-to-buffer credit = %i, "
		 "current receice buffer-to-buffer credit = %i, "
		 "advertised transmit buffer-to-buffer credit = %i, "
		 "current transmit buffer-to-buffer credit = %i).\n",
		 err->primitive_sequence_event_timeout_count,
		 err->elastic_buffer_overrun_error_count,
		 err->advertised_receive_b2b_credit,
		 err->current_receive_b2b_credit,
		 err->advertised_transmit_b2b_credit,
		 err->current_transmit_b2b_credit);
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
		zfcp_fc_incoming_els(fsf_req);
		break;

	case FSF_STATUS_READ_SENSE_DATA_AVAIL:
		break;

	case FSF_STATUS_READ_BIT_ERROR_THRESHOLD:
		zfcp_fsf_bit_error_threshold(fsf_req);
		break;

	case FSF_STATUS_READ_LINK_DOWN:
		switch (status_buffer->status_subtype) {
		case FSF_STATUS_READ_SUB_NO_PHYSICAL_LINK:
			dev_warn(&adapter->ccw_device->dev,
				 "Physical link is down.\n");
			zfcp_fsf_link_down_info_eval(fsf_req, 38,
				(struct fsf_link_down_info *)
				&status_buffer->payload);
			break;
		case FSF_STATUS_READ_SUB_FDISC_FAILED:
			dev_warn(&adapter->ccw_device->dev,
				 "Local link is down "
				 "due to failed FDISC login.\n");
			zfcp_fsf_link_down_info_eval(fsf_req, 39,
				(struct fsf_link_down_info *)
				&status_buffer->payload);
			break;
		case FSF_STATUS_READ_SUB_FIRMWARE_UPDATE:
			dev_warn(&adapter->ccw_device->dev,
				 "Local link is down "
				 "due to firmware update on adapter.\n");
			zfcp_fsf_link_down_info_eval(fsf_req, 40, NULL);
			break;
		default:
			dev_warn(&adapter->ccw_device->dev,
				 "Local link is down.\n");
			zfcp_fsf_link_down_info_eval(fsf_req, 41, NULL);
		};
		break;

	case FSF_STATUS_READ_LINK_UP:
		dev_info(&adapter->ccw_device->dev,
			 "Local link was replugged.\n");
		/* All ports should be marked as ready to run again */
		zfcp_erp_modify_adapter_status(adapter, 30, NULL,
					       ZFCP_STATUS_COMMON_RUNNING,
					       ZFCP_SET);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED
					| ZFCP_STATUS_COMMON_ERP_FAILED,
					102, fsf_req);
		break;

	case FSF_STATUS_READ_NOTIFICATION_LOST:
		if (status_buffer->status_subtype &
		    FSF_STATUS_READ_SUB_ACT_UPDATED)
			zfcp_erp_adapter_access_changed(adapter, 135, fsf_req);
		break;

	case FSF_STATUS_READ_CFDC_UPDATED:
		zfcp_erp_adapter_access_changed(adapter, 136, fsf_req);
		break;

	case FSF_STATUS_READ_FEATURE_UPDATE_ALERT:
		adapter->adapter_features = *(u32*) status_buffer->payload;
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

	atomic_inc(&adapter->stat_miss);
	schedule_work(&adapter->stat_work);
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
	struct zfcp_fsf_req *fsf_req = NULL;
	unsigned long lock_flags;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_ABORT_FCP_CMND,
				     req_flags, adapter->pool.fsf_req_abort,
				     &lock_flags, &fsf_req);
	if (retval < 0)
		goto out;

	if (unlikely(!atomic_test_mask(ZFCP_STATUS_COMMON_UNBLOCKED,
			&unit->status)))
		goto unit_blocked;

	sbale = zfcp_qdio_sbale_req(fsf_req);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	fsf_req->data = (unsigned long) unit;

	/* set handles of unit and its parent port in QTCB */
	fsf_req->qtcb->header.lun_handle = unit->handle;
	fsf_req->qtcb->header.port_handle = unit->port->handle;

	/* set handle of request which should be aborted */
	fsf_req->qtcb->bottom.support.req_handle = (u64) old_req_id;

	zfcp_fsf_start_timer(fsf_req, ZFCP_SCSI_ER_TIMEOUT);
	retval = zfcp_fsf_req_send(fsf_req);
	if (!retval)
		goto out;

 unit_blocked:
		zfcp_fsf_req_free(fsf_req);
		fsf_req = NULL;

 out:
	write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);
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
	union fsf_status_qual *fsf_stat_qual =
		&new_fsf_req->qtcb->header.fsf_status_qual;

	if (new_fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* do not set ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED */
		goto skip_fsfstatus;
	}

	unit = (struct zfcp_unit *) new_fsf_req->data;

	/* evaluate FSF status in QTCB */
	switch (new_fsf_req->qtcb->header.fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		if (fsf_stat_qual->word[0] != fsf_stat_qual->word[1]) {
			/*
			 * In this case a command that was sent prior to a port
			 * reopen was aborted (handles are different). This is
			 * fine.
			 */
		} else {
			/* Let's hope this sorts out the mess */
			zfcp_erp_adapter_reopen(unit->port->adapter, 0, 104,
						new_fsf_req);
			new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		}
		break;

	case FSF_LUN_HANDLE_NOT_VALID:
		if (fsf_stat_qual->word[0] != fsf_stat_qual->word[1]) {
			/*
			 * In this case a command that was sent prior to a unit
			 * reopen was aborted (handles are different).
			 * This is fine.
			 */
		} else {
			/* Let's hope this sorts out the mess */
			zfcp_erp_port_reopen(unit->port, 0, 105, new_fsf_req);
			new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		}
		break;

	case FSF_FCP_COMMAND_DOES_NOT_EXIST:
		retval = 0;
		new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED;
		break;

	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(unit->port, 47, new_fsf_req);
		new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
		    | ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_LUN_BOXED:
		zfcp_erp_unit_boxed(unit, 48, new_fsf_req);
                new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
                        | ZFCP_STATUS_FSFREQ_RETRY;
                break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (new_fsf_req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			zfcp_test_link(unit->port);
			new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* SCSI stack will escalate */
			new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;

	case FSF_GOOD:
		retval = 0;
		new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED;
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
	if (ret < 0)
		goto failed_req;

	sbale = zfcp_qdio_sbale_req(fsf_req);
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
						ct->req,
                                                ZFCP_MAX_SBALS_PER_CT_REQ);
                if (bytes <= 0) {
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
						ct->resp,
                                                ZFCP_MAX_SBALS_PER_CT_REQ);
                if (bytes <= 0) {
                        if (bytes == 0)
                                ret = -ENOMEM;
                        else
                                ret = bytes;

                        goto failed_send;
                }
                fsf_req->qtcb->bottom.support.resp_buf_length = bytes;
        } else {
                /* reject send generic request */
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

	if (erp_action) {
		erp_action->fsf_req = fsf_req;
		fsf_req->erp_action = erp_action;
		zfcp_erp_start_timer(fsf_req);
	} else
		zfcp_fsf_start_timer(fsf_req, ZFCP_FSF_REQUEST_TIMEOUT);

	ret = zfcp_fsf_req_send(fsf_req);
	if (ret)
		goto failed_send;

	goto out;

 failed_send:
	zfcp_fsf_req_free(fsf_req);
        if (erp_action != NULL) {
                erp_action->fsf_req = NULL;
        }
 failed_req:
 out:
	write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);
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
		zfcp_fsf_class_not_supp(fsf_req);
		break;

        case FSF_ADAPTER_STATUS_AVAILABLE:
                switch (header->fsf_status_qual.word[0]){
                case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			/* reopening link to port */
			zfcp_test_link(port);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
                case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* ERP strategy will escalate */
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
                }
                break;

	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_port(fsf_req, port);
		break;

        case FSF_GENERIC_COMMAND_REJECTED:
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(adapter, 0, 106, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(port, 49, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
		    | ZFCP_STATUS_FSFREQ_RETRY;
		break;

	/* following states should never occure, all cases avoided
	   in zfcp_fsf_send_ct - but who knows ... */
	case FSF_PAYLOAD_SIZE_MISMATCH:
	case FSF_REQUEST_SIZE_TOO_LARGE:
	case FSF_RESPONSE_SIZE_TOO_LARGE:
	case FSF_SBAL_MISMATCH:
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

       default:
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
	if (ret < 0)
                goto failed_req;

	if (unlikely(!atomic_test_mask(ZFCP_STATUS_COMMON_UNBLOCKED,
			&els->port->status))) {
		ret = -EBUSY;
		goto port_blocked;
	}

	sbale = zfcp_qdio_sbale_req(fsf_req);
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
						els->req,
                                                ZFCP_MAX_SBALS_PER_ELS_REQ);
                if (bytes <= 0) {
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
						els->resp,
                                                ZFCP_MAX_SBALS_PER_ELS_REQ);
                if (bytes <= 0) {
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
                ret = -EOPNOTSUPP;
                goto failed_send;
        }

	/* settings in QTCB */
	fsf_req->qtcb->bottom.support.d_id = d_id;
	fsf_req->qtcb->bottom.support.service_class =
		ZFCP_FC_SERVICE_CLASS_DEFAULT;
	fsf_req->qtcb->bottom.support.timeout = ZFCP_ELS_TIMEOUT;
	fsf_req->data = (unsigned long) els;

	sbale = zfcp_qdio_sbale_req(fsf_req);

	zfcp_san_dbf_event_els_request(fsf_req);

	zfcp_fsf_start_timer(fsf_req, ZFCP_FSF_REQUEST_TIMEOUT);
	ret = zfcp_fsf_req_send(fsf_req);
	if (ret)
		goto failed_send;

	goto out;

 port_blocked:
 failed_send:
	zfcp_fsf_req_free(fsf_req);

 failed_req:
 out:
	write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);

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
		zfcp_fsf_class_not_supp(fsf_req);
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]){
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			if (port && (send_els->ls_code != ZFCP_LS_ADISC))
				zfcp_test_link(port);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_RETRY_IF_POSSIBLE:
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;

	case FSF_ELS_COMMAND_REJECTED:
	case FSF_PAYLOAD_SIZE_MISMATCH:
	case FSF_REQUEST_SIZE_TOO_LARGE:
	case FSF_RESPONSE_SIZE_TOO_LARGE:
		break;

	case FSF_SBAL_MISMATCH:
		/* should never occure, avoided in zfcp_fsf_send_els */
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_port(fsf_req, port);
		break;

	default:
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}

skip_fsfstatus:
	send_els->status = retval;

	if (send_els->handler)
		send_els->handler(send_els->handler_data);

	return retval;
}

int
zfcp_fsf_exchange_config_data(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *fsf_req;
	struct zfcp_adapter *adapter = erp_action->adapter;
	unsigned long lock_flags;
	int retval;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter,
				     FSF_QTCB_EXCHANGE_CONFIG_DATA,
				     ZFCP_REQ_AUTO_CLEANUP,
				     adapter->pool.fsf_req_erp,
				     &lock_flags, &fsf_req);
	if (retval) {
		write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);
		return retval;
	}

	sbale = zfcp_qdio_sbale_req(fsf_req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	fsf_req->qtcb->bottom.config.feature_selection =
			FSF_FEATURE_CFDC |
			FSF_FEATURE_LUN_SHARING |
			FSF_FEATURE_NOTIFICATION_LOST |
			FSF_FEATURE_UPDATE_ALERT;
	fsf_req->erp_action = erp_action;
	erp_action->fsf_req = fsf_req;

	zfcp_erp_start_timer(fsf_req);
	retval = zfcp_fsf_req_send(fsf_req);
	write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);
	if (retval) {
		zfcp_fsf_req_free(fsf_req);
		erp_action->fsf_req = NULL;
	}

	return retval;
}

int
zfcp_fsf_exchange_config_data_sync(struct zfcp_adapter *adapter,
				struct fsf_qtcb_bottom_config *data)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *fsf_req;
	unsigned long lock_flags;
	int retval;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_EXCHANGE_CONFIG_DATA,
				     ZFCP_WAIT_FOR_SBAL, NULL, &lock_flags,
				     &fsf_req);
	if (retval) {
		write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);
		return retval;
	}

	sbale = zfcp_qdio_sbale_req(fsf_req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	fsf_req->qtcb->bottom.config.feature_selection =
			FSF_FEATURE_CFDC |
			FSF_FEATURE_LUN_SHARING |
			FSF_FEATURE_NOTIFICATION_LOST |
			FSF_FEATURE_UPDATE_ALERT;

	if (data)
		fsf_req->data = (unsigned long) data;

	zfcp_fsf_start_timer(fsf_req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(fsf_req);
	write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);
	if (!retval)
		wait_event(fsf_req->completion_wq,
			   fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	zfcp_fsf_req_free(fsf_req);

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
	adapter->fsf_lic_version = bottom->lic_version;
	adapter->adapter_features = bottom->adapter_features;
	adapter->connection_features = bottom->connection_features;
	adapter->peer_wwpn = 0;
	adapter->peer_wwnn = 0;
	adapter->peer_d_id = 0;

	if (xchg_ok) {

		if (fsf_req->data)
			memcpy((struct fsf_qtcb_bottom_config *) fsf_req->data,
				bottom, sizeof (struct fsf_qtcb_bottom_config));

		fc_host_node_name(shost) = bottom->nport_serv_param.wwnn;
		fc_host_port_name(shost) = bottom->nport_serv_param.wwpn;
		fc_host_port_id(shost) = bottom->s_id & ZFCP_DID_MASK;
		fc_host_speed(shost) = bottom->fc_link_speed;
		fc_host_supported_classes(shost) =
				FC_COS_CLASS2 | FC_COS_CLASS3;
		adapter->hydra_version = bottom->adapter_type;
		adapter->timer_ticks = bottom->timer_interval;
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

	if (ZFCP_QTCB_VERSION < bottom->low_qtcb_version) {
		dev_err(&adapter->ccw_device->dev,
			"The adapter only supports newer control block "
			"versions, try updated device driver.\n");
		zfcp_erp_adapter_shutdown(adapter, 0, 125, fsf_req);
		return -EIO;
	}
	if (ZFCP_QTCB_VERSION > bottom->high_qtcb_version) {
		dev_err(&adapter->ccw_device->dev,
			"The adapter only supports older control block "
			"versions, consider a microcode upgrade.\n");
		zfcp_erp_adapter_shutdown(adapter, 0, 126, fsf_req);
		return -EIO;
	}
	return 0;
}

/**
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
			if (fsf_req->erp_action)
				dev_info(&adapter->ccw_device->dev,
					 "Point-to-Point fibrechannel "
					 "configuration detected.\n");
			break;
		case FC_PORTTYPE_NLPORT:
			dev_err(&adapter->ccw_device->dev,
				"Unsupported arbitrated loop fibrechannel "
				"topology detected, shutting down adapter\n");
			zfcp_erp_adapter_shutdown(adapter, 0, 127, fsf_req);
			return -EIO;
		case FC_PORTTYPE_NPORT:
			if (fsf_req->erp_action)
				dev_info(&adapter->ccw_device->dev,
					 "Switched fabric fibrechannel "
					 "network detected.\n");
			break;
		default:
			dev_err(&adapter->ccw_device->dev,
				"The fibrechannel topology reported by the "
				"adapter is not known by the zfcp driver, "
				"shutting down adapter.\n");
			zfcp_erp_adapter_shutdown(adapter, 0, 128, fsf_req);
			return -EIO;
		}
		bottom = &qtcb->bottom.config;
		if (bottom->max_qtcb_size < sizeof(struct fsf_qtcb)) {
			dev_err(&adapter->ccw_device->dev,
				"Maximum QTCB size (%d bytes) allowed by "
				"the adapter is lower than the minimum "
				"required by the driver (%ld bytes).\n",
				bottom->max_qtcb_size, sizeof(struct fsf_qtcb));
			zfcp_erp_adapter_shutdown(adapter, 0, 129, fsf_req);
			return -EIO;
		}
		atomic_set_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK,
				&adapter->status);
		break;
	case FSF_EXCHANGE_CONFIG_DATA_INCOMPLETE:
		if (zfcp_fsf_exchange_config_evaluate(fsf_req, 0))
			return -EIO;

		atomic_set_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK,
				&adapter->status);

		zfcp_fsf_link_down_info_eval(fsf_req, 42,
			&qtcb->header.fsf_status_qual.link_down_info);
		break;
	default:
		zfcp_erp_adapter_shutdown(adapter, 0, 130, fsf_req);
		return -EIO;
	}
	return 0;
}

/**
 * zfcp_fsf_exchange_port_data - request information about local port
 * @erp_action: ERP action for the adapter for which port data is requested
 */
int
zfcp_fsf_exchange_port_data(struct zfcp_erp_action *erp_action)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *fsf_req;
	struct zfcp_adapter *adapter = erp_action->adapter;
	unsigned long lock_flags;
	int retval;

	if (!(adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT))
		return -EOPNOTSUPP;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_EXCHANGE_PORT_DATA,
				     ZFCP_REQ_AUTO_CLEANUP,
				     adapter->pool.fsf_req_erp,
				     &lock_flags, &fsf_req);
	if (retval) {
		write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);
		return retval;
	}

	sbale = zfcp_qdio_sbale_req(fsf_req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	erp_action->fsf_req = fsf_req;
	fsf_req->erp_action = erp_action;
	zfcp_erp_start_timer(fsf_req);

	retval = zfcp_fsf_req_send(fsf_req);
	write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);

	if (retval) {
		zfcp_fsf_req_free(fsf_req);
		erp_action->fsf_req = NULL;
	}
	return retval;
}


/**
 * zfcp_fsf_exchange_port_data_sync - request information about local port
 * and wait until information is ready
 */
int
zfcp_fsf_exchange_port_data_sync(struct zfcp_adapter *adapter,
				struct fsf_qtcb_bottom_port *data)
{
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *fsf_req;
	unsigned long lock_flags;
	int retval;

	if (!(adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT))
		return -EOPNOTSUPP;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_EXCHANGE_PORT_DATA,
				0, NULL, &lock_flags, &fsf_req);
	if (retval) {
		write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);
		return retval;
	}

	if (data)
		fsf_req->data = (unsigned long) data;

	sbale = zfcp_qdio_sbale_req(fsf_req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	zfcp_fsf_start_timer(fsf_req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(fsf_req);
	write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);

	if (!retval)
		wait_event(fsf_req->completion_wq,
			   fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	zfcp_fsf_req_free(fsf_req);

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
	struct fsf_qtcb_bottom_port *bottom;
	struct Scsi_Host *shost;

	adapter = fsf_req->adapter;
	bottom = &fsf_req->qtcb->bottom.port;
	shost = adapter->scsi_host;

	if (fsf_req->data)
		memcpy((struct fsf_qtcb_bottom_port*) fsf_req->data, bottom,
			sizeof(struct fsf_qtcb_bottom_port));

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
		zfcp_fsf_link_down_info_eval(fsf_req, 43,
			&qtcb->header.fsf_status_qual.link_down_info);
                break;
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
	struct zfcp_fsf_req *fsf_req;
	unsigned long lock_flags;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_OPEN_PORT_WITH_DID,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     erp_action->adapter->pool.fsf_req_erp,
				     &lock_flags, &fsf_req);
	if (retval < 0)
		goto out;

	sbale = zfcp_qdio_sbale_req(fsf_req);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	fsf_req->qtcb->bottom.support.d_id = erp_action->port->d_id;
	atomic_set_mask(ZFCP_STATUS_COMMON_OPENING, &erp_action->port->status);
	fsf_req->data = (unsigned long) erp_action->port;
	fsf_req->erp_action = erp_action;
	erp_action->fsf_req = fsf_req;

	zfcp_erp_start_timer(fsf_req);
	retval = zfcp_fsf_req_send(fsf_req);
	if (retval) {
		zfcp_fsf_req_free(fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

 out:
	write_unlock_irqrestore(&erp_action->adapter->req_q.lock, lock_flags);
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

	port = (struct zfcp_port *) fsf_req->data;
	header = &fsf_req->qtcb->header;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change port status in our bookkeeping */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (header->fsf_status) {

	case FSF_PORT_ALREADY_OPEN:
		/*
		 * This is a bug, however operation should continue normally
		 * if it is simply ignored
		 */
		break;

	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_port(fsf_req, port);
		break;

	case FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "The adapter is out of resources. The remote port "
			 "0x%016Lx could not be opened, disabling it.\n",
			 port->wwpn);
		zfcp_erp_port_failed(port, 31, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			/* ERP strategy will escalate */
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* ERP strategy will escalate */
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_NO_RETRY_POSSIBLE:
			dev_warn(&fsf_req->adapter->ccw_device->dev,
				 "The remote port 0x%016Lx could not be "
				 "opened. Disabling it.\n", port->wwpn);
			zfcp_erp_port_failed(port, 32, fsf_req);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			break;
		}
		break;

	case FSF_GOOD:
		/* save port handle assigned by FSF */
		port->handle = header->port_handle;
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
				/* skip sanity check and assume wwpn is ok */
			} else {
				if (plogi->serv_param.wwpn != port->wwpn) {
					atomic_clear_mask(
						ZFCP_STATUS_PORT_DID_DID,
						&port->status);
				} else {
					port->wwnn = plogi->serv_param.wwnn;
					zfcp_fc_plogi_evaluate(port, plogi);
				}
			}
		}
		break;

	case FSF_UNKNOWN_OP_SUBTYPE:
		/* should never occure, subtype not set in zfcp_fsf_open_port */
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	default:
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
	struct zfcp_fsf_req *fsf_req;
	unsigned long lock_flags;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_CLOSE_PORT,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     erp_action->adapter->pool.fsf_req_erp,
				     &lock_flags, &fsf_req);
	if (retval < 0)
		goto out;

	sbale = zfcp_qdio_sbale_req(fsf_req);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING, &erp_action->port->status);
	fsf_req->data = (unsigned long) erp_action->port;
	fsf_req->erp_action = erp_action;
	fsf_req->qtcb->header.port_handle = erp_action->port->handle;
	fsf_req->erp_action = erp_action;
	erp_action->fsf_req = fsf_req;

	zfcp_erp_start_timer(fsf_req);
	retval = zfcp_fsf_req_send(fsf_req);
	if (retval) {
		zfcp_fsf_req_free(fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

 out:
	write_unlock_irqrestore(&erp_action->adapter->req_q.lock, lock_flags);
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
		zfcp_erp_adapter_reopen(port->adapter, 0, 107, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		/* Note: FSF has actually closed the port in this case.
		 * The status code is just daft. Fingers crossed for a change
		 */
		retval = 0;
		break;

	case FSF_GOOD:
		zfcp_erp_modify_port_status(port, 33, fsf_req,
					    ZFCP_STATUS_COMMON_OPEN,
					    ZFCP_CLEAR);
		retval = 0;
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
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_fsf_req *fsf_req;
	unsigned long lock_flags;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_CLOSE_PHYSICAL_PORT,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     erp_action->adapter->pool.fsf_req_erp,
				     &lock_flags, &fsf_req);
	if (retval < 0)
		goto out;

	sbale = zfcp_qdio_sbale_req(fsf_req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	/* mark port as being closed */
	atomic_set_mask(ZFCP_STATUS_PORT_PHYS_CLOSING,
			&erp_action->port->status);
	/* save a pointer to this port */
	fsf_req->data = (unsigned long) erp_action->port;
	fsf_req->qtcb->header.port_handle = erp_action->port->handle;
	fsf_req->erp_action = erp_action;
	erp_action->fsf_req = fsf_req;

	zfcp_erp_start_timer(fsf_req);
	retval = zfcp_fsf_req_send(fsf_req);
	if (retval) {
		zfcp_fsf_req_free(fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

 out:
	write_unlock_irqrestore(&erp_action->adapter->req_q.lock, lock_flags);
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

	port = (struct zfcp_port *) fsf_req->data;
	header = &fsf_req->qtcb->header;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change port status in our bookkeeping */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (header->fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(port->adapter, 0, 108, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_port(fsf_req, port);
		break;

	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(port, 50, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY;

		/* can't use generic zfcp_erp_modify_port_status because
		 * ZFCP_STATUS_COMMON_OPEN must not be reset for the port */
		atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		list_for_each_entry(unit, &port->unit_list_head, list)
			atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN,
					  &unit->status);
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			/* This will now be escalated by ERP */
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* ERP strategy will escalate */
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;

	case FSF_GOOD:
		/* can't use generic zfcp_erp_modify_port_status because
		 * ZFCP_STATUS_COMMON_OPEN must not be reset for the port
		 */
		atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		list_for_each_entry(unit, &port->unit_list_head, list)
		    atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);
		retval = 0;
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
	struct zfcp_fsf_req *fsf_req;
	unsigned long lock_flags;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_OPEN_LUN,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     erp_action->adapter->pool.fsf_req_erp,
				     &lock_flags, &fsf_req);
	if (retval < 0)
		goto out;

	sbale = zfcp_qdio_sbale_req(fsf_req);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	fsf_req->qtcb->header.port_handle = erp_action->port->handle;
	fsf_req->qtcb->bottom.support.fcp_lun =	erp_action->unit->fcp_lun;
	if (!(erp_action->adapter->connection_features & FSF_FEATURE_NPIV_MODE))
		fsf_req->qtcb->bottom.support.option =
			FSF_OPEN_LUN_SUPPRESS_BOXING;
	atomic_set_mask(ZFCP_STATUS_COMMON_OPENING, &erp_action->unit->status);
	fsf_req->data = (unsigned long) erp_action->unit;
	fsf_req->erp_action = erp_action;
	erp_action->fsf_req = fsf_req;

	zfcp_erp_start_timer(fsf_req);
	retval = zfcp_fsf_req_send(erp_action->fsf_req);
	if (retval) {
		zfcp_fsf_req_free(fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}
 out:
	write_unlock_irqrestore(&erp_action->adapter->req_q.lock, lock_flags);
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
			  ZFCP_STATUS_COMMON_ACCESS_BOXED |
			  ZFCP_STATUS_UNIT_SHARED |
			  ZFCP_STATUS_UNIT_READONLY,
			  &unit->status);

	/* evaluate FSF status in QTCB */
	switch (header->fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		zfcp_erp_adapter_reopen(unit->port->adapter, 0, 109, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_LUN_ALREADY_OPEN:
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_unit(fsf_req, unit);
		atomic_clear_mask(ZFCP_STATUS_UNIT_SHARED, &unit->status);
		atomic_clear_mask(ZFCP_STATUS_UNIT_READONLY, &unit->status);
		break;

	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(unit->port, 51, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_LUN_SHARING_VIOLATION:
		if (header->fsf_status_qual.word[0] != 0) {
			dev_warn(&adapter->ccw_device->dev,
				 "FCP-LUN 0x%Lx at the remote port "
				 "with WWPN 0x%Lx "
				 "connected to the adapter "
				 "is already in use in LPAR%d, CSS%d.\n",
				 unit->fcp_lun,
				 unit->port->wwpn,
				 queue_designator->hla,
				 queue_designator->cssid);
		} else
			zfcp_act_eval_err(adapter,
					  header->fsf_status_qual.word[2]);
		zfcp_erp_unit_access_denied(unit, 60, fsf_req);
		atomic_clear_mask(ZFCP_STATUS_UNIT_SHARED, &unit->status);
		atomic_clear_mask(ZFCP_STATUS_UNIT_READONLY, &unit->status);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_MAXIMUM_NUMBER_OF_LUNS_EXCEEDED:
		dev_warn(&fsf_req->adapter->ccw_device->dev,
			 "The adapter ran out of resources. There is no "
			 "handle available for unit 0x%016Lx on port 0x%016Lx.",
			 unit->fcp_lun, unit->port->wwpn);
		zfcp_erp_unit_failed(unit, 34, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			/* Re-establish link to port */
			zfcp_test_link(unit->port);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* ERP strategy will escalate */
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		}
		break;

	case FSF_INVALID_COMMAND_OPTION:
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EINVAL;
		break;

	case FSF_GOOD:
		/* save LUN handle assigned by FSF */
		unit->handle = header->lun_handle;
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
				dev_info(&fsf_req->adapter->ccw_device->dev,
					 "Read-only access for unit 0x%016Lx "
					 "on port 0x%016Lx.\n",
					 unit->fcp_lun, unit->port->wwpn);
        		}

        		if (exclusive && !readwrite) {
				dev_err(&fsf_req->adapter->ccw_device->dev,
					"Exclusive access of read-only unit "
					"0x%016Lx on port 0x%016Lx not "
					"supported, disabling unit.\n",
					unit->fcp_lun, unit->port->wwpn);
				zfcp_erp_unit_failed(unit, 35, fsf_req);
				fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
				zfcp_erp_unit_shutdown(unit, 0, 80, fsf_req);
        		} else if (!exclusive && readwrite) {
				dev_err(&fsf_req->adapter->ccw_device->dev,
					"Shared access of read-write unit "
					"0x%016Lx on port 0x%016Lx not "
					"supported, disabling unit.\n",
					unit->fcp_lun, unit->port->wwpn);
				zfcp_erp_unit_failed(unit, 36, fsf_req);
				fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
				zfcp_erp_unit_shutdown(unit, 0, 81, fsf_req);
        		}
		}

		retval = 0;
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
	struct zfcp_fsf_req *fsf_req;
	unsigned long lock_flags;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_CLOSE_LUN,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     erp_action->adapter->pool.fsf_req_erp,
				     &lock_flags, &fsf_req);
	if (retval < 0)
		goto out;

	sbale = zfcp_qdio_sbale_req(fsf_req);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	fsf_req->qtcb->header.port_handle = erp_action->port->handle;
	fsf_req->qtcb->header.lun_handle = erp_action->unit->handle;
	atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING, &erp_action->unit->status);
	fsf_req->data = (unsigned long) erp_action->unit;
	fsf_req->erp_action = erp_action;
	erp_action->fsf_req = fsf_req;

	zfcp_erp_start_timer(fsf_req);
	retval = zfcp_fsf_req_send(erp_action->fsf_req);
	if (retval) {
		zfcp_fsf_req_free(fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

 out:
	write_unlock_irqrestore(&erp_action->adapter->req_q.lock, lock_flags);
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
		zfcp_erp_adapter_reopen(unit->port->adapter, 0, 110, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_LUN_HANDLE_NOT_VALID:
		zfcp_erp_port_reopen(unit->port, 0, 111, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(unit->port, 52, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (fsf_req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			/* re-establish link to port */
			zfcp_test_link(unit->port);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* ERP strategy will escalate */
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			break;
		}
		break;

	case FSF_GOOD:
		/* mark unit as closed */
		atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);
		retval = 0;
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
			       int use_timer, int req_flags)
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
	if (unlikely(retval < 0))
		goto failed_req_create;

	if (unlikely(!atomic_test_mask(ZFCP_STATUS_COMMON_UNBLOCKED,
			&unit->status))) {
		retval = -EBUSY;
		goto unit_blocked;
	}

	zfcp_unit_get(unit);
	fsf_req->unit = unit;

	/* associate FSF request with SCSI request (for look up on abort) */
	scsi_cmnd->host_scribble = (unsigned char *) fsf_req->req_id;

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
	if (unlikely(scsi_cmnd->cmd_len > FCP_CDB_LENGTH))
		fcp_cmnd_iu->add_fcp_cdb_length
		    = (scsi_cmnd->cmd_len - FCP_CDB_LENGTH) >> 2;
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
	real_bytes = zfcp_qdio_sbals_from_sg(fsf_req, sbtype,
					     scsi_sglist(scsi_cmnd),
					     ZFCP_MAX_SBALS_PER_REQ);
	if (unlikely(real_bytes < 0)) {
		if (fsf_req->sbal_number < ZFCP_MAX_SBALS_PER_REQ)
			retval = -EIO;
		else {
			dev_err(&adapter->ccw_device->dev,
				"SCSI request too large. "
				"Shutting down unit 0x%016Lx on port "
				"0x%016Lx.\n", unit->fcp_lun,
				unit->port->wwpn);
			zfcp_erp_unit_shutdown(unit, 0, 131, fsf_req);
			retval = -EINVAL;
		}
		goto no_fit;
	}

	/* set length of FCP data length in FCP_CMND IU in QTCB */
	zfcp_set_fcp_dl(fcp_cmnd_iu, real_bytes);

	if (use_timer)
		zfcp_fsf_start_timer(fsf_req, ZFCP_FSF_REQUEST_TIMEOUT);

	retval = zfcp_fsf_req_send(fsf_req);
	if (unlikely(retval < 0))
		goto send_failed;

	goto success;

 send_failed:
 no_fit:
 failed_scsi_cmnd:
	zfcp_unit_put(unit);
 unit_blocked:
	zfcp_fsf_req_free(fsf_req);
	fsf_req = NULL;
	scsi_cmnd->host_scribble = NULL;
 success:
 failed_req_create:
	write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);
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
	if (retval < 0)
		goto out;

	if (unlikely(!atomic_test_mask(ZFCP_STATUS_COMMON_UNBLOCKED,
			&unit->status)))
		goto unit_blocked;

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

	sbale = zfcp_qdio_sbale_req(fsf_req);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_WRITE;
	sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	/* set FCP related fields in FCP_CMND IU in QTCB */
	fcp_cmnd_iu = (struct fcp_cmnd_iu *)
		&(fsf_req->qtcb->bottom.io.fcp_cmnd);
	fcp_cmnd_iu->fcp_lun = unit->fcp_lun;
	fcp_cmnd_iu->task_management_flags = tm_flags;

	zfcp_fsf_start_timer(fsf_req, ZFCP_SCSI_ER_TIMEOUT);
	retval = zfcp_fsf_req_send(fsf_req);
	if (!retval)
		goto out;

 unit_blocked:
	zfcp_fsf_req_free(fsf_req);
	fsf_req = NULL;

 out:
	write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);
	return fsf_req;
}

static void zfcp_fsf_update_lat(struct fsf_latency_record *lat_rec, u32 lat)
{
	lat_rec->sum += lat;
	if (lat_rec->min > lat)
		lat_rec->min = lat;
	if (lat_rec->max < lat)
		lat_rec->max = lat;
}

static void zfcp_fsf_req_latency(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_qual_latency_info *lat_inf;
	struct latency_cont *lat;
	struct zfcp_unit *unit;
	unsigned long flags;

	lat_inf = &fsf_req->qtcb->prefix.prot_status_qual.latency_info;
	unit = fsf_req->unit;

	switch (fsf_req->qtcb->bottom.io.data_direction) {
	case FSF_DATADIR_READ:
		lat = &unit->latencies.read;
		break;
	case FSF_DATADIR_WRITE:
		lat = &unit->latencies.write;
		break;
	case FSF_DATADIR_CMND:
		lat = &unit->latencies.cmd;
		break;
	default:
		return;
	}

	spin_lock_irqsave(&unit->latencies.lock, flags);
	zfcp_fsf_update_lat(&lat->channel, lat_inf->channel_lat);
	zfcp_fsf_update_lat(&lat->fabric, lat_inf->fabric_lat);
	lat->counter++;
	spin_unlock_irqrestore(&unit->latencies.lock, flags);
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
		zfcp_erp_adapter_reopen(unit->port->adapter, 0, 112, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_LUN_HANDLE_NOT_VALID:
		zfcp_erp_port_reopen(unit->port, 0, 113, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_HANDLE_MISMATCH:
		zfcp_erp_adapter_reopen(unit->port->adapter, 0, 114, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		zfcp_fsf_class_not_supp(fsf_req);
		break;

	case FSF_FCPLUN_NOT_VALID:
		zfcp_erp_port_reopen(unit->port, 0, 115, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ACCESS_DENIED:
		zfcp_fsf_access_denied_unit(fsf_req, unit);
		break;

	case FSF_DIRECTION_INDICATOR_NOT_VALID:
		dev_err(&fsf_req->adapter->ccw_device->dev,
			"Invalid data direction (%d) given for unit 0x%016Lx "
			"on port 0x%016Lx, shutting down adapter.\n",
			fsf_req->qtcb->bottom.io.data_direction,
			unit->fcp_lun, unit->port->wwpn);
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0, 133, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_CMND_LENGTH_NOT_VALID:
		dev_err(&fsf_req->adapter->ccw_device->dev,
			"An invalid control-data-block length field (%d) "
			"was found in a command for unit 0x%016Lx on port "
			"0x%016Lx. Shutting down adapter.\n",
			fsf_req->qtcb->bottom.io.fcp_cmnd_length,
			unit->fcp_lun, unit->port->wwpn);
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0, 134, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PORT_BOXED:
		zfcp_erp_port_boxed(unit->port, 53, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_LUN_BOXED:
		zfcp_erp_unit_boxed(unit, 54, fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
			| ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		switch (header->fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			/* re-establish link to port */
 			zfcp_test_link(unit->port);
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* FIXME(hw) need proper specs for proper action */
			/* let scsi stack deal with retries and escalation */
			break;
		}
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_GOOD:
		break;

	case FSF_FCP_RSP_AVAILABLE:
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
	u32 sns_len;
	char *fcp_rsp_info = zfcp_get_fcp_rsp_info_ptr(fcp_rsp_iu);
	unsigned long flags;

	read_lock_irqsave(&fsf_req->adapter->abort_lock, flags);
	scpnt = (struct scsi_cmnd *) fsf_req->data;
	if (unlikely(!scpnt))
		goto out;

	if (unlikely(fsf_req->status & ZFCP_STATUS_FSFREQ_ABORTED)) {
		/* FIXME: (design) mid-layer should handle DID_ABORT like
		 *        DID_SOFT_ERROR by retrying the request for devices
		 *        that allow retries.
		 */
		set_host_byte(&scpnt->result, DID_SOFT_ERROR);
		set_driver_byte(&scpnt->result, SUGGEST_RETRY);
		goto skip_fsfstatus;
	}

	if (unlikely(fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR)) {
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

	if (fsf_req->adapter->adapter_features & FSF_FEATURE_MEASUREMENT_DATA)
		zfcp_fsf_req_latency(fsf_req);

	/* check FCP_RSP_INFO */
	if (unlikely(fcp_rsp_iu->validity.bits.fcp_rsp_len_valid)) {
		switch (fcp_rsp_info[3]) {
		case RSP_CODE_GOOD:
			/* ok, continue */
			set_host_byte(&scpnt->result, DID_OK);
			break;
		case RSP_CODE_LENGTH_MISMATCH:
			/* hardware bug */
			set_host_byte(&scpnt->result, DID_ERROR);
			goto skip_fsfstatus;
		case RSP_CODE_FIELD_INVALID:
			/* driver or hardware bug */
			set_host_byte(&scpnt->result, DID_ERROR);
			goto skip_fsfstatus;
		case RSP_CODE_RO_MISMATCH:
			/* hardware bug */
			set_host_byte(&scpnt->result, DID_ERROR);
			goto skip_fsfstatus;
		default:
			/* invalid FCP response code */
			set_host_byte(&scpnt->result, DID_ERROR);
			goto skip_fsfstatus;
		}
	}

	/* check for sense data */
	if (unlikely(fcp_rsp_iu->validity.bits.fcp_sns_len_valid)) {
		sns_len = FSF_FCP_RSP_SIZE -
		    sizeof (struct fcp_rsp_iu) + fcp_rsp_iu->fcp_rsp_len;
		sns_len = min(sns_len, (u32) SCSI_SENSE_BUFFERSIZE);
		sns_len = min(sns_len, fcp_rsp_iu->fcp_sns_len);

		memcpy(scpnt->sense_buffer,
		       zfcp_get_fcp_sns_info_ptr(fcp_rsp_iu), sns_len);
	}

	/* check for underrun */
	if (unlikely(fcp_rsp_iu->validity.bits.fcp_resid_under)) {
		scsi_set_resid(scpnt, fcp_rsp_iu->fcp_resid);
		if (scsi_bufflen(scpnt) - scsi_get_resid(scpnt) <
		    scpnt->underflow)
			set_host_byte(&scpnt->result, DID_ERROR);
	}

 skip_fsfstatus:
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

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		fsf_req->status |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
		goto skip_fsfstatus;
	}

	/* check FCP_RSP_INFO */
	switch (fcp_rsp_info[3]) {
	case RSP_CODE_GOOD:
		/* ok, continue */
		break;
	case RSP_CODE_TASKMAN_UNSUPP:
		fsf_req->status |= ZFCP_STATUS_FSFREQ_TMFUNCNOTSUPP;
		break;
	case RSP_CODE_TASKMAN_FAILED:
		fsf_req->status |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
		break;
	default:
		/* invalid FCP response code */
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
struct zfcp_fsf_req *zfcp_fsf_control_file(struct zfcp_adapter *adapter,
					   struct zfcp_fsf_cfdc *fsf_cfdc)
{
	struct zfcp_fsf_req *fsf_req;
	struct fsf_qtcb_bottom_support *bottom;
	volatile struct qdio_buffer_element *sbale;
	unsigned long lock_flags;
	int direction;
	int retval;
	int bytes;

	if (!(adapter->adapter_features & FSF_FEATURE_CFDC))
		return ERR_PTR(-EOPNOTSUPP);

	switch (fsf_cfdc->command) {
	case FSF_QTCB_DOWNLOAD_CONTROL_FILE:
		direction = SBAL_FLAGS0_TYPE_WRITE;
		break;
	case FSF_QTCB_UPLOAD_CONTROL_FILE:
		direction = SBAL_FLAGS0_TYPE_READ;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	retval = zfcp_fsf_req_create(adapter, fsf_cfdc->command,
				     ZFCP_WAIT_FOR_SBAL,
				     NULL, &lock_flags, &fsf_req);
	if (retval < 0) {
		retval = -EPERM;
		goto unlock_queue_lock;
	}

	sbale = zfcp_qdio_sbale_req(fsf_req);
	sbale[0].flags |= direction;

	bottom = &fsf_req->qtcb->bottom.support;
	bottom->operation_subtype = FSF_CFDC_OPERATION_SUBTYPE;
	bottom->option = fsf_cfdc->option;

	bytes = zfcp_qdio_sbals_from_sg(fsf_req, direction,
					fsf_cfdc->sg,
					ZFCP_MAX_SBALS_PER_REQ);
	if (bytes != ZFCP_CFDC_MAX_SIZE) {
		retval = -ENOMEM;
		goto free_fsf_req;
	}

	zfcp_fsf_start_timer(fsf_req, ZFCP_FSF_REQUEST_TIMEOUT);
	retval = zfcp_fsf_req_send(fsf_req);
	if (retval < 0) {
		retval = -EPERM;
		goto free_fsf_req;
	}
	write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);

	wait_event(fsf_req->completion_wq,
	           fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);

	return fsf_req;

 free_fsf_req:
	zfcp_fsf_req_free(fsf_req);
 unlock_queue_lock:
	write_unlock_irqrestore(&adapter->req_q.lock, lock_flags);
	return ERR_PTR(retval);
}

static void zfcp_fsf_control_file_handler(struct zfcp_fsf_req *fsf_req)
{
	if (fsf_req->qtcb->header.fsf_status != FSF_GOOD)
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
}

static inline int
zfcp_fsf_req_sbal_check(unsigned long *flags,
			struct zfcp_qdio_queue *queue, int needed)
{
	write_lock_irqsave(&queue->lock, *flags);
	if (likely(atomic_read(&queue->count) >= needed))
		return 1;
	write_unlock_irqrestore(&queue->lock, *flags);
	return 0;
}

/*
 * set qtcb pointer in fsf_req and initialize QTCB
 */
static void
zfcp_fsf_req_qtcb_init(struct zfcp_fsf_req *fsf_req)
{
	if (likely(fsf_req->qtcb != NULL)) {
		fsf_req->qtcb->prefix.req_seq_no =
			fsf_req->adapter->fsf_req_seq_no;
		fsf_req->qtcb->prefix.req_id = fsf_req->req_id;
		fsf_req->qtcb->prefix.ulp_info = ZFCP_ULP_INFO_VERSION;
		fsf_req->qtcb->prefix.qtcb_type =
			fsf_qtcb_type[fsf_req->fsf_command];
		fsf_req->qtcb->prefix.qtcb_version = ZFCP_QTCB_VERSION;
		fsf_req->qtcb->header.req_handle = fsf_req->req_id;
		fsf_req->qtcb->header.fsf_command = fsf_req->fsf_command;
	}
}

/**
 * zfcp_fsf_req_sbal_get - try to get one SBAL in the request queue
 * @adapter: adapter for which request queue is examined
 * @req_flags: flags indicating whether to wait for needed SBAL or not
 * @lock_flags: lock_flags if queue_lock is taken
 * Return: 0 on success, otherwise -EIO, or -ERESTARTSYS
 * Locks: lock adapter->req_q->lock on success
 */
static int
zfcp_fsf_req_sbal_get(struct zfcp_adapter *adapter, int req_flags,
		      unsigned long *lock_flags)
{
        long ret;
	struct zfcp_qdio_queue *req_q = &adapter->req_q;

        if (unlikely(req_flags & ZFCP_WAIT_FOR_SBAL)) {
                ret = wait_event_interruptible_timeout(adapter->request_wq,
			zfcp_fsf_req_sbal_check(lock_flags, req_q, 1),
						       ZFCP_SBAL_TIMEOUT);
		if (ret < 0)
			return ret;
		if (!ret)
			return -EIO;
	} else if (!zfcp_fsf_req_sbal_check(lock_flags, req_q, 1))
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
	struct zfcp_qdio_queue *req_q = &adapter->req_q;

	/* allocate new FSF request */
	fsf_req = zfcp_fsf_req_alloc(pool, req_flags);
	if (unlikely(!fsf_req)) {
		ret = -ENOMEM;
		goto failed_fsf_req;
	}

	fsf_req->adapter = adapter;
	fsf_req->fsf_command = fsf_cmd;
	INIT_LIST_HEAD(&fsf_req->list);
	init_timer(&fsf_req->timer);

	/* initialize waitqueue which may be used to wait on
	   this request completion */
	init_waitqueue_head(&fsf_req->completion_wq);

        ret = zfcp_fsf_req_sbal_get(adapter, req_flags, lock_flags);
        if (ret < 0)
                goto failed_sbals;

	/* this is serialized (we are holding req_queue-lock of adapter) */
	if (adapter->req_no == 0)
		adapter->req_no++;
	fsf_req->req_id = adapter->req_no++;

	zfcp_fsf_req_qtcb_init(fsf_req);

	/*
	 * We hold queue_lock here. Check if QDIOUP is set and let request fail
	 * if it is not set (see also *_open_qdio and *_close_qdio).
	 */

	if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status)) {
		write_unlock_irqrestore(&req_q->lock, *lock_flags);
		ret = -EIO;
		goto failed_sbals;
	}

	if (fsf_req->qtcb) {
		fsf_req->seq_no = adapter->fsf_req_seq_no;
		fsf_req->qtcb->prefix.req_seq_no = adapter->fsf_req_seq_no;
	}
	fsf_req->sbal_number = 1;
	fsf_req->sbal_first = req_q->first;
	fsf_req->sbal_last = req_q->first;
        fsf_req->sbale_curr = 1;

	if (likely(req_flags & ZFCP_REQ_AUTO_CLEANUP)) {
		fsf_req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;
	}

	sbale = zfcp_qdio_sbale_req(fsf_req);

	/* setup common SBALE fields */
	sbale[0].addr = (void *) fsf_req->req_id;
	sbale[0].flags |= SBAL_FLAGS0_COMMAND;
	if (likely(fsf_req->qtcb != NULL)) {
		sbale[1].addr = (void *) fsf_req->qtcb;
		sbale[1].length = sizeof(struct fsf_qtcb);
	}

	goto success;

 failed_sbals:
/* dequeue new FSF request previously enqueued */
	zfcp_fsf_req_free(fsf_req);
	fsf_req = NULL;

 failed_fsf_req:
	write_lock_irqsave(&req_q->lock, *lock_flags);
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
static int zfcp_fsf_req_send(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_adapter *adapter;
	struct zfcp_qdio_queue *req_q;
	volatile struct qdio_buffer_element *sbale;
	int inc_seq_no;
	int retval = 0;

	adapter = fsf_req->adapter;
	req_q = &adapter->req_q;

	sbale = zfcp_qdio_sbale_req(fsf_req);

	/* put allocated FSF request into hash table */
	spin_lock(&adapter->req_list_lock);
	zfcp_reqlist_add(adapter, fsf_req);
	spin_unlock(&adapter->req_list_lock);

	inc_seq_no = (fsf_req->qtcb != NULL);

	fsf_req->issued = get_clock();

	retval = zfcp_qdio_send(fsf_req);

	if (unlikely(retval)) {
		/* Queues are down..... */
		del_timer(&fsf_req->timer);
		spin_lock(&adapter->req_list_lock);
		zfcp_reqlist_remove(adapter, fsf_req);
		spin_unlock(&adapter->req_list_lock);
		/* undo changes in request queue made for this request */
		atomic_add(fsf_req->sbal_number, &req_q->count);
		req_q->first -= fsf_req->sbal_number;
		req_q->first += QDIO_MAX_BUFFERS_PER_Q;
		req_q->first %= QDIO_MAX_BUFFERS_PER_Q;
		zfcp_erp_adapter_reopen(adapter, 0, 116, fsf_req);
		retval = -EIO;
	} else {
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
	}
	return retval;
}
