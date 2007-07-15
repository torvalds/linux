/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */
#include <linux/moduleparam.h>

#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>

#include "ql4_def.h"
#include "ql4_version.h"
#include "ql4_glbl.h"
#include "ql4_dbg.h"
#include "ql4_inline.h"

/*
 * Driver version
 */
static char qla4xxx_version_str[40];

/*
 * SRB allocation cache
 */
static struct kmem_cache *srb_cachep;

/*
 * Module parameter information and variables
 */
int ql4xdiscoverywait = 60;
module_param(ql4xdiscoverywait, int, S_IRUGO | S_IRUSR);
MODULE_PARM_DESC(ql4xdiscoverywait, "Discovery wait time");
int ql4xdontresethba = 0;
module_param(ql4xdontresethba, int, S_IRUGO | S_IRUSR);
MODULE_PARM_DESC(ql4xdontresethba,
		 "Dont reset the HBA when the driver gets 0x8002 AEN "
		 " default it will reset hba :0"
		 " set to 1 to avoid resetting HBA");

int ql4xextended_error_logging = 0; /* 0 = off, 1 = log errors */
module_param(ql4xextended_error_logging, int, S_IRUGO | S_IRUSR);
MODULE_PARM_DESC(ql4xextended_error_logging,
		 "Option to enable extended error logging, "
		 "Default is 0 - no logging, 1 - debug logging");

int ql4_mod_unload = 0;

/*
 * SCSI host template entry points
 */
static void qla4xxx_config_dma_addressing(struct scsi_qla_host *ha);

/*
 * iSCSI template entry points
 */
static int qla4xxx_tgt_dscvr(struct Scsi_Host *shost,
			     enum iscsi_tgt_dscvr type, uint32_t enable,
			     struct sockaddr *dst_addr);
static int qla4xxx_conn_get_param(struct iscsi_cls_conn *conn,
				  enum iscsi_param param, char *buf);
static int qla4xxx_sess_get_param(struct iscsi_cls_session *sess,
				  enum iscsi_param param, char *buf);
static int qla4xxx_host_get_param(struct Scsi_Host *shost,
				  enum iscsi_host_param param, char *buf);
static void qla4xxx_conn_stop(struct iscsi_cls_conn *conn, int flag);
static int qla4xxx_conn_start(struct iscsi_cls_conn *conn);
static void qla4xxx_recovery_timedout(struct iscsi_cls_session *session);

/*
 * SCSI host template entry points
 */
static int qla4xxx_queuecommand(struct scsi_cmnd *cmd,
				void (*done) (struct scsi_cmnd *));
static int qla4xxx_eh_device_reset(struct scsi_cmnd *cmd);
static int qla4xxx_eh_host_reset(struct scsi_cmnd *cmd);
static int qla4xxx_slave_alloc(struct scsi_device *device);
static int qla4xxx_slave_configure(struct scsi_device *device);
static void qla4xxx_slave_destroy(struct scsi_device *sdev);

static struct scsi_host_template qla4xxx_driver_template = {
	.module			= THIS_MODULE,
	.name			= DRIVER_NAME,
	.proc_name		= DRIVER_NAME,
	.queuecommand		= qla4xxx_queuecommand,

	.eh_device_reset_handler = qla4xxx_eh_device_reset,
	.eh_host_reset_handler	= qla4xxx_eh_host_reset,

	.slave_configure	= qla4xxx_slave_configure,
	.slave_alloc		= qla4xxx_slave_alloc,
	.slave_destroy		= qla4xxx_slave_destroy,

	.this_id		= -1,
	.cmd_per_lun		= 3,
	.use_clustering		= ENABLE_CLUSTERING,
	.sg_tablesize		= SG_ALL,

	.max_sectors		= 0xFFFF,
};

static struct iscsi_transport qla4xxx_iscsi_transport = {
	.owner			= THIS_MODULE,
	.name			= DRIVER_NAME,
	.caps			= CAP_FW_DB | CAP_SENDTARGETS_OFFLOAD |
				  CAP_DATA_PATH_OFFLOAD,
	.param_mask		= ISCSI_CONN_PORT | ISCSI_CONN_ADDRESS |
				  ISCSI_TARGET_NAME | ISCSI_TPGT,
	.host_param_mask	= ISCSI_HOST_HWADDRESS |
				  ISCSI_HOST_IPADDRESS |
				  ISCSI_HOST_INITIATOR_NAME,
	.sessiondata_size	= sizeof(struct ddb_entry),
	.host_template		= &qla4xxx_driver_template,

	.tgt_dscvr		= qla4xxx_tgt_dscvr,
	.get_conn_param		= qla4xxx_conn_get_param,
	.get_session_param	= qla4xxx_sess_get_param,
	.get_host_param		= qla4xxx_host_get_param,
	.start_conn		= qla4xxx_conn_start,
	.stop_conn		= qla4xxx_conn_stop,
	.session_recovery_timedout = qla4xxx_recovery_timedout,
};

static struct scsi_transport_template *qla4xxx_scsi_transport;

static void qla4xxx_recovery_timedout(struct iscsi_cls_session *session)
{
	struct ddb_entry *ddb_entry = session->dd_data;
	struct scsi_qla_host *ha = ddb_entry->ha;

	DEBUG2(printk("scsi%ld: %s: index [%d] port down retry count of (%d) "
		      "secs exhausted, marking device DEAD.\n", ha->host_no,
		      __func__, ddb_entry->fw_ddb_index,
		      ha->port_down_retry_count));

	atomic_set(&ddb_entry->state, DDB_STATE_DEAD);

	DEBUG2(printk("scsi%ld: %s: scheduling dpc routine - dpc flags = "
		      "0x%lx\n", ha->host_no, __func__, ha->dpc_flags));
	queue_work(ha->dpc_thread, &ha->dpc_work);
}

static int qla4xxx_conn_start(struct iscsi_cls_conn *conn)
{
	struct iscsi_cls_session *session;
	struct ddb_entry *ddb_entry;

	session = iscsi_dev_to_session(conn->dev.parent);
	ddb_entry = session->dd_data;

	DEBUG2(printk("scsi%ld: %s: index [%d] starting conn\n",
		      ddb_entry->ha->host_no, __func__,
		      ddb_entry->fw_ddb_index));
	iscsi_unblock_session(session);
	return 0;
}

static void qla4xxx_conn_stop(struct iscsi_cls_conn *conn, int flag)
{
	struct iscsi_cls_session *session;
	struct ddb_entry *ddb_entry;

	session = iscsi_dev_to_session(conn->dev.parent);
	ddb_entry = session->dd_data;

	DEBUG2(printk("scsi%ld: %s: index [%d] stopping conn\n",
		      ddb_entry->ha->host_no, __func__,
		      ddb_entry->fw_ddb_index));
	if (flag == STOP_CONN_RECOVER)
		iscsi_block_session(session);
	else
		printk(KERN_ERR "iscsi: invalid stop flag %d\n", flag);
}

static ssize_t format_addr(char *buf, const unsigned char *addr, int len)
{
	int i;
	char *cp = buf;

	for (i = 0; i < len; i++)
		cp += sprintf(cp, "%02x%c", addr[i],
			      i == (len - 1) ? '\n' : ':');
	return cp - buf;
}


static int qla4xxx_host_get_param(struct Scsi_Host *shost,
				  enum iscsi_host_param param, char *buf)
{
	struct scsi_qla_host *ha = to_qla_host(shost);
	int len;

	switch (param) {
	case ISCSI_HOST_PARAM_HWADDRESS:
		len = format_addr(buf, ha->my_mac, MAC_ADDR_LEN);
		break;
	case ISCSI_HOST_PARAM_IPADDRESS:
		len = sprintf(buf, "%d.%d.%d.%d\n", ha->ip_address[0],
			      ha->ip_address[1], ha->ip_address[2],
			      ha->ip_address[3]);
		break;
	case ISCSI_HOST_PARAM_INITIATOR_NAME:
		len = sprintf(buf, "%s\n", ha->name_string);
		break;
	default:
		return -ENOSYS;
	}

	return len;
}

static int qla4xxx_sess_get_param(struct iscsi_cls_session *sess,
				  enum iscsi_param param, char *buf)
{
	struct ddb_entry *ddb_entry = sess->dd_data;
	int len;

	switch (param) {
	case ISCSI_PARAM_TARGET_NAME:
		len = snprintf(buf, PAGE_SIZE - 1, "%s\n",
			       ddb_entry->iscsi_name);
		break;
	case ISCSI_PARAM_TPGT:
		len = sprintf(buf, "%u\n", ddb_entry->tpgt);
		break;
	default:
		return -ENOSYS;
	}

	return len;
}

static int qla4xxx_conn_get_param(struct iscsi_cls_conn *conn,
				  enum iscsi_param param, char *buf)
{
	struct iscsi_cls_session *session;
	struct ddb_entry *ddb_entry;
	int len;

	session = iscsi_dev_to_session(conn->dev.parent);
	ddb_entry = session->dd_data;

	switch (param) {
	case ISCSI_PARAM_CONN_PORT:
		len = sprintf(buf, "%hu\n", ddb_entry->port);
		break;
	case ISCSI_PARAM_CONN_ADDRESS:
		/* TODO: what are the ipv6 bits */
		len = sprintf(buf, "%u.%u.%u.%u\n",
			      NIPQUAD(ddb_entry->ip_addr));
		break;
	default:
		return -ENOSYS;
	}

	return len;
}

static int qla4xxx_tgt_dscvr(struct Scsi_Host *shost,
			     enum iscsi_tgt_dscvr type, uint32_t enable,
			     struct sockaddr *dst_addr)
{
	struct scsi_qla_host *ha;
	struct sockaddr_in *addr;
	struct sockaddr_in6 *addr6;
	int ret = 0;

	ha = (struct scsi_qla_host *) shost->hostdata;

	switch (type) {
	case ISCSI_TGT_DSCVR_SEND_TARGETS:
		if (dst_addr->sa_family == AF_INET) {
			addr = (struct sockaddr_in *)dst_addr;
			if (qla4xxx_send_tgts(ha, (char *)&addr->sin_addr,
					      addr->sin_port) != QLA_SUCCESS)
				ret = -EIO;
		} else if (dst_addr->sa_family == AF_INET6) {
			/*
			 * TODO: fix qla4xxx_send_tgts
			 */
			addr6 = (struct sockaddr_in6 *)dst_addr;
			if (qla4xxx_send_tgts(ha, (char *)&addr6->sin6_addr,
					      addr6->sin6_port) != QLA_SUCCESS)
				ret = -EIO;
		} else
			ret = -ENOSYS;
		break;
	default:
		ret = -ENOSYS;
	}
	return ret;
}

void qla4xxx_destroy_sess(struct ddb_entry *ddb_entry)
{
	if (!ddb_entry->sess)
		return;

	if (ddb_entry->conn) {
		iscsi_if_destroy_session_done(ddb_entry->conn);
		iscsi_destroy_conn(ddb_entry->conn);
		iscsi_remove_session(ddb_entry->sess);
	}
	iscsi_free_session(ddb_entry->sess);
}

int qla4xxx_add_sess(struct ddb_entry *ddb_entry)
{
	int err;

	err = iscsi_add_session(ddb_entry->sess, ddb_entry->fw_ddb_index);
	if (err) {
		DEBUG2(printk(KERN_ERR "Could not add session.\n"));
		return err;
	}

	ddb_entry->conn = iscsi_create_conn(ddb_entry->sess, 0);
	if (!ddb_entry->conn) {
		iscsi_remove_session(ddb_entry->sess);
		DEBUG2(printk(KERN_ERR "Could not add connection.\n"));
		return -ENOMEM;
	}

	ddb_entry->sess->recovery_tmo = ddb_entry->ha->port_down_retry_count;
	iscsi_if_create_session_done(ddb_entry->conn);
	return 0;
}

struct ddb_entry *qla4xxx_alloc_sess(struct scsi_qla_host *ha)
{
	struct ddb_entry *ddb_entry;
	struct iscsi_cls_session *sess;

	sess = iscsi_alloc_session(ha->host, &qla4xxx_iscsi_transport);
	if (!sess)
		return NULL;

	ddb_entry = sess->dd_data;
	memset(ddb_entry, 0, sizeof(*ddb_entry));
	ddb_entry->ha = ha;
	ddb_entry->sess = sess;
	return ddb_entry;
}

/*
 * Timer routines
 */

static void qla4xxx_start_timer(struct scsi_qla_host *ha, void *func,
				unsigned long interval)
{
	DEBUG(printk("scsi: %s: Starting timer thread for adapter %d\n",
		     __func__, ha->host->host_no));
	init_timer(&ha->timer);
	ha->timer.expires = jiffies + interval * HZ;
	ha->timer.data = (unsigned long)ha;
	ha->timer.function = (void (*)(unsigned long))func;
	add_timer(&ha->timer);
	ha->timer_active = 1;
}

static void qla4xxx_stop_timer(struct scsi_qla_host *ha)
{
	del_timer_sync(&ha->timer);
	ha->timer_active = 0;
}

/***
 * qla4xxx_mark_device_missing - mark a device as missing.
 * @ha: Pointer to host adapter structure.
 * @ddb_entry: Pointer to device database entry
 *
 * This routine marks a device missing and resets the relogin retry count.
 **/
void qla4xxx_mark_device_missing(struct scsi_qla_host *ha,
				 struct ddb_entry *ddb_entry)
{
	atomic_set(&ddb_entry->state, DDB_STATE_MISSING);
	DEBUG3(printk("scsi%d:%d:%d: index [%d] marked MISSING\n",
		      ha->host_no, ddb_entry->bus, ddb_entry->target,
		      ddb_entry->fw_ddb_index));
	iscsi_conn_error(ddb_entry->conn, ISCSI_ERR_CONN_FAILED);
}

static struct srb* qla4xxx_get_new_srb(struct scsi_qla_host *ha,
				       struct ddb_entry *ddb_entry,
				       struct scsi_cmnd *cmd,
				       void (*done)(struct scsi_cmnd *))
{
	struct srb *srb;

	srb = mempool_alloc(ha->srb_mempool, GFP_ATOMIC);
	if (!srb)
		return srb;

	atomic_set(&srb->ref_count, 1);
	srb->ha = ha;
	srb->ddb = ddb_entry;
	srb->cmd = cmd;
	srb->flags = 0;
	cmd->SCp.ptr = (void *)srb;
	cmd->scsi_done = done;

	return srb;
}

static void qla4xxx_srb_free_dma(struct scsi_qla_host *ha, struct srb *srb)
{
	struct scsi_cmnd *cmd = srb->cmd;

	if (srb->flags & SRB_DMA_VALID) {
		scsi_dma_unmap(cmd);
		srb->flags &= ~SRB_DMA_VALID;
	}
	cmd->SCp.ptr = NULL;
}

void qla4xxx_srb_compl(struct scsi_qla_host *ha, struct srb *srb)
{
	struct scsi_cmnd *cmd = srb->cmd;

	qla4xxx_srb_free_dma(ha, srb);

	mempool_free(srb, ha->srb_mempool);

	cmd->scsi_done(cmd);
}

/**
 * qla4xxx_queuecommand - scsi layer issues scsi command to driver.
 * @cmd: Pointer to Linux's SCSI command structure
 * @done_fn: Function that the driver calls to notify the SCSI mid-layer
 *	that the command has been processed.
 *
 * Remarks:
 * This routine is invoked by Linux to send a SCSI command to the driver.
 * The mid-level driver tries to ensure that queuecommand never gets
 * invoked concurrently with itself or the interrupt handler (although
 * the interrupt handler may call this routine as part of request-
 * completion handling).   Unfortunely, it sometimes calls the scheduler
 * in interrupt context which is a big NO! NO!.
 **/
static int qla4xxx_queuecommand(struct scsi_cmnd *cmd,
				void (*done)(struct scsi_cmnd *))
{
	struct scsi_qla_host *ha = to_qla_host(cmd->device->host);
	struct ddb_entry *ddb_entry = cmd->device->hostdata;
	struct srb *srb;
	int rval;

	if (atomic_read(&ddb_entry->state) != DDB_STATE_ONLINE) {
		if (atomic_read(&ddb_entry->state) == DDB_STATE_DEAD) {
			cmd->result = DID_NO_CONNECT << 16;
			goto qc_fail_command;
		}
		goto qc_host_busy;
	}

	if (test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags))
		goto qc_host_busy;

	spin_unlock_irq(ha->host->host_lock);

	srb = qla4xxx_get_new_srb(ha, ddb_entry, cmd, done);
	if (!srb)
		goto qc_host_busy_lock;

	rval = qla4xxx_send_command_to_isp(ha, srb);
	if (rval != QLA_SUCCESS)
		goto qc_host_busy_free_sp;

	spin_lock_irq(ha->host->host_lock);
	return 0;

qc_host_busy_free_sp:
	qla4xxx_srb_free_dma(ha, srb);
	mempool_free(srb, ha->srb_mempool);

qc_host_busy_lock:
	spin_lock_irq(ha->host->host_lock);

qc_host_busy:
	return SCSI_MLQUEUE_HOST_BUSY;

qc_fail_command:
	done(cmd);

	return 0;
}

/**
 * qla4xxx_mem_free - frees memory allocated to adapter
 * @ha: Pointer to host adapter structure.
 *
 * Frees memory previously allocated by qla4xxx_mem_alloc
 **/
static void qla4xxx_mem_free(struct scsi_qla_host *ha)
{
	if (ha->queues)
		dma_free_coherent(&ha->pdev->dev, ha->queues_len, ha->queues,
				  ha->queues_dma);

	ha->queues_len = 0;
	ha->queues = NULL;
	ha->queues_dma = 0;
	ha->request_ring = NULL;
	ha->request_dma = 0;
	ha->response_ring = NULL;
	ha->response_dma = 0;
	ha->shadow_regs = NULL;
	ha->shadow_regs_dma = 0;

	/* Free srb pool. */
	if (ha->srb_mempool)
		mempool_destroy(ha->srb_mempool);

	ha->srb_mempool = NULL;

	/* release io space registers  */
	if (ha->reg)
		iounmap(ha->reg);
	pci_release_regions(ha->pdev);
}

/**
 * qla4xxx_mem_alloc - allocates memory for use by adapter.
 * @ha: Pointer to host adapter structure
 *
 * Allocates DMA memory for request and response queues. Also allocates memory
 * for srbs.
 **/
static int qla4xxx_mem_alloc(struct scsi_qla_host *ha)
{
	unsigned long align;

	/* Allocate contiguous block of DMA memory for queues. */
	ha->queues_len = ((REQUEST_QUEUE_DEPTH * QUEUE_SIZE) +
			  (RESPONSE_QUEUE_DEPTH * QUEUE_SIZE) +
			  sizeof(struct shadow_regs) +
			  MEM_ALIGN_VALUE +
			  (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
	ha->queues = dma_alloc_coherent(&ha->pdev->dev, ha->queues_len,
					&ha->queues_dma, GFP_KERNEL);
	if (ha->queues == NULL) {
		dev_warn(&ha->pdev->dev,
			"Memory Allocation failed - queues.\n");

		goto mem_alloc_error_exit;
	}
	memset(ha->queues, 0, ha->queues_len);

	/*
	 * As per RISC alignment requirements -- the bus-address must be a
	 * multiple of the request-ring size (in bytes).
	 */
	align = 0;
	if ((unsigned long)ha->queues_dma & (MEM_ALIGN_VALUE - 1))
		align = MEM_ALIGN_VALUE - ((unsigned long)ha->queues_dma &
					   (MEM_ALIGN_VALUE - 1));

	/* Update request and response queue pointers. */
	ha->request_dma = ha->queues_dma + align;
	ha->request_ring = (struct queue_entry *) (ha->queues + align);
	ha->response_dma = ha->queues_dma + align +
		(REQUEST_QUEUE_DEPTH * QUEUE_SIZE);
	ha->response_ring = (struct queue_entry *) (ha->queues + align +
						    (REQUEST_QUEUE_DEPTH *
						     QUEUE_SIZE));
	ha->shadow_regs_dma = ha->queues_dma + align +
		(REQUEST_QUEUE_DEPTH * QUEUE_SIZE) +
		(RESPONSE_QUEUE_DEPTH * QUEUE_SIZE);
	ha->shadow_regs = (struct shadow_regs *) (ha->queues + align +
						  (REQUEST_QUEUE_DEPTH *
						   QUEUE_SIZE) +
						  (RESPONSE_QUEUE_DEPTH *
						   QUEUE_SIZE));

	/* Allocate memory for srb pool. */
	ha->srb_mempool = mempool_create(SRB_MIN_REQ, mempool_alloc_slab,
					 mempool_free_slab, srb_cachep);
	if (ha->srb_mempool == NULL) {
		dev_warn(&ha->pdev->dev,
			"Memory Allocation failed - SRB Pool.\n");

		goto mem_alloc_error_exit;
	}

	return QLA_SUCCESS;

mem_alloc_error_exit:
	qla4xxx_mem_free(ha);
	return QLA_ERROR;
}

/**
 * qla4xxx_timer - checks every second for work to do.
 * @ha: Pointer to host adapter structure.
 **/
static void qla4xxx_timer(struct scsi_qla_host *ha)
{
	struct ddb_entry *ddb_entry, *dtemp;
	int start_dpc = 0;

	/* Search for relogin's to time-out and port down retry. */
	list_for_each_entry_safe(ddb_entry, dtemp, &ha->ddb_list, list) {
		/* Count down time between sending relogins */
		if (adapter_up(ha) &&
		    !test_bit(DF_RELOGIN, &ddb_entry->flags) &&
		    atomic_read(&ddb_entry->state) != DDB_STATE_ONLINE) {
			if (atomic_read(&ddb_entry->retry_relogin_timer) !=
			    INVALID_ENTRY) {
				if (atomic_read(&ddb_entry->retry_relogin_timer)
				    		== 0) {
					atomic_set(&ddb_entry->
						retry_relogin_timer,
						INVALID_ENTRY);
					set_bit(DPC_RELOGIN_DEVICE,
						&ha->dpc_flags);
					set_bit(DF_RELOGIN, &ddb_entry->flags);
					DEBUG2(printk("scsi%ld: %s: index [%d]"
						      " login device\n",
						      ha->host_no, __func__,
						      ddb_entry->fw_ddb_index));
				} else
					atomic_dec(&ddb_entry->
							retry_relogin_timer);
			}
		}

		/* Wait for relogin to timeout */
		if (atomic_read(&ddb_entry->relogin_timer) &&
		    (atomic_dec_and_test(&ddb_entry->relogin_timer) != 0)) {
			/*
			 * If the relogin times out and the device is
			 * still NOT ONLINE then try and relogin again.
			 */
			if (atomic_read(&ddb_entry->state) !=
			    DDB_STATE_ONLINE &&
			    ddb_entry->fw_ddb_device_state ==
			    DDB_DS_SESSION_FAILED) {
				/* Reset retry relogin timer */
				atomic_inc(&ddb_entry->relogin_retry_count);
				DEBUG2(printk("scsi%ld: index[%d] relogin"
					      " timed out-retrying"
					      " relogin (%d)\n",
					      ha->host_no,
					      ddb_entry->fw_ddb_index,
					      atomic_read(&ddb_entry->
							  relogin_retry_count))
					);
				start_dpc++;
				DEBUG(printk("scsi%ld:%d:%d: index [%d] "
					     "initate relogin after"
					     " %d seconds\n",
					     ha->host_no, ddb_entry->bus,
					     ddb_entry->target,
					     ddb_entry->fw_ddb_index,
					     ddb_entry->default_time2wait + 4)
					);

				atomic_set(&ddb_entry->retry_relogin_timer,
					   ddb_entry->default_time2wait + 4);
			}
		}
	}

	/* Check for heartbeat interval. */
	if (ha->firmware_options & FWOPT_HEARTBEAT_ENABLE &&
	    ha->heartbeat_interval != 0) {
		ha->seconds_since_last_heartbeat++;
		if (ha->seconds_since_last_heartbeat >
		    ha->heartbeat_interval + 2)
			set_bit(DPC_RESET_HA, &ha->dpc_flags);
	}


	/* Wakeup the dpc routine for this adapter, if needed. */
	if ((start_dpc ||
	     test_bit(DPC_RESET_HA, &ha->dpc_flags) ||
	     test_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags) ||
	     test_bit(DPC_RELOGIN_DEVICE, &ha->dpc_flags) ||
	     test_bit(DPC_RESET_HA_DESTROY_DDB_LIST, &ha->dpc_flags) ||
	     test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags) ||
	     test_bit(DPC_GET_DHCP_IP_ADDR, &ha->dpc_flags) ||
	     test_bit(DPC_AEN, &ha->dpc_flags)) &&
	     ha->dpc_thread) {
		DEBUG2(printk("scsi%ld: %s: scheduling dpc routine"
			      " - dpc flags = 0x%lx\n",
			      ha->host_no, __func__, ha->dpc_flags));
		queue_work(ha->dpc_thread, &ha->dpc_work);
	}

	/* Reschedule timer thread to call us back in one second */
	mod_timer(&ha->timer, jiffies + HZ);

	DEBUG2(ha->seconds_since_last_intr++);
}

/**
 * qla4xxx_cmd_wait - waits for all outstanding commands to complete
 * @ha: Pointer to host adapter structure.
 *
 * This routine stalls the driver until all outstanding commands are returned.
 * Caller must release the Hardware Lock prior to calling this routine.
 **/
static int qla4xxx_cmd_wait(struct scsi_qla_host *ha)
{
	uint32_t index = 0;
	int stat = QLA_SUCCESS;
	unsigned long flags;
	struct scsi_cmnd *cmd;
	int wait_cnt = WAIT_CMD_TOV;	/*
					 * Initialized for 30 seconds as we
					 * expect all commands to retuned
					 * ASAP.
					 */

	while (wait_cnt) {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		/* Find a command that hasn't completed. */
		for (index = 0; index < ha->host->can_queue; index++) {
			cmd = scsi_host_find_tag(ha->host, index);
			if (cmd != NULL)
				break;
		}
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		/* If No Commands are pending, wait is complete */
		if (index == ha->host->can_queue) {
			break;
		}

		/* If we timed out on waiting for commands to come back
		 * return ERROR.
		 */
		wait_cnt--;
		if (wait_cnt == 0)
			stat = QLA_ERROR;
		else {
			msleep(1000);
		}
	}			/* End of While (wait_cnt) */

	return stat;
}

void qla4xxx_hw_reset(struct scsi_qla_host *ha)
{
	uint32_t ctrl_status;
	unsigned long flags = 0;

	DEBUG2(printk(KERN_ERR "scsi%ld: %s\n", ha->host_no, __func__));

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/*
	 * If the SCSI Reset Interrupt bit is set, clear it.
	 * Otherwise, the Soft Reset won't work.
	 */
	ctrl_status = readw(&ha->reg->ctrl_status);
	if ((ctrl_status & CSR_SCSI_RESET_INTR) != 0)
		writel(set_rmask(CSR_SCSI_RESET_INTR), &ha->reg->ctrl_status);

	/* Issue Soft Reset */
	writel(set_rmask(CSR_SOFT_RESET), &ha->reg->ctrl_status);
	readl(&ha->reg->ctrl_status);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

/**
 * qla4xxx_soft_reset - performs soft reset.
 * @ha: Pointer to host adapter structure.
 **/
int qla4xxx_soft_reset(struct scsi_qla_host *ha)
{
	uint32_t max_wait_time;
	unsigned long flags = 0;
	int status = QLA_ERROR;
	uint32_t ctrl_status;

	qla4xxx_hw_reset(ha);

	/* Wait until the Network Reset Intr bit is cleared */
	max_wait_time = RESET_INTR_TOV;
	do {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		ctrl_status = readw(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		if ((ctrl_status & CSR_NET_RESET_INTR) == 0)
			break;

		msleep(1000);
	} while ((--max_wait_time));

	if ((ctrl_status & CSR_NET_RESET_INTR) != 0) {
		DEBUG2(printk(KERN_WARNING
			      "scsi%ld: Network Reset Intr not cleared by "
			      "Network function, clearing it now!\n",
			      ha->host_no));
		spin_lock_irqsave(&ha->hardware_lock, flags);
		writel(set_rmask(CSR_NET_RESET_INTR), &ha->reg->ctrl_status);
		readl(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}

	/* Wait until the firmware tells us the Soft Reset is done */
	max_wait_time = SOFT_RESET_TOV;
	do {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		ctrl_status = readw(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		if ((ctrl_status & CSR_SOFT_RESET) == 0) {
			status = QLA_SUCCESS;
			break;
		}

		msleep(1000);
	} while ((--max_wait_time));

	/*
	 * Also, make sure that the SCSI Reset Interrupt bit has been cleared
	 * after the soft reset has taken place.
	 */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	ctrl_status = readw(&ha->reg->ctrl_status);
	if ((ctrl_status & CSR_SCSI_RESET_INTR) != 0) {
		writel(set_rmask(CSR_SCSI_RESET_INTR), &ha->reg->ctrl_status);
		readl(&ha->reg->ctrl_status);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* If soft reset fails then most probably the bios on other
	 * function is also enabled.
	 * Since the initialization is sequential the other fn
	 * wont be able to acknowledge the soft reset.
	 * Issue a force soft reset to workaround this scenario.
	 */
	if (max_wait_time == 0) {
		/* Issue Force Soft Reset */
		spin_lock_irqsave(&ha->hardware_lock, flags);
		writel(set_rmask(CSR_FORCE_SOFT_RESET), &ha->reg->ctrl_status);
		readl(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		/* Wait until the firmware tells us the Soft Reset is done */
		max_wait_time = SOFT_RESET_TOV;
		do {
			spin_lock_irqsave(&ha->hardware_lock, flags);
			ctrl_status = readw(&ha->reg->ctrl_status);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

			if ((ctrl_status & CSR_FORCE_SOFT_RESET) == 0) {
				status = QLA_SUCCESS;
				break;
			}

			msleep(1000);
		} while ((--max_wait_time));
	}

	return status;
}

/**
 * qla4xxx_flush_active_srbs - returns all outstanding i/o requests to O.S.
 * @ha: Pointer to host adapter structure.
 *
 * This routine is called just prior to a HARD RESET to return all
 * outstanding commands back to the Operating System.
 * Caller should make sure that the following locks are released
 * before this calling routine: Hardware lock, and io_request_lock.
 **/
static void qla4xxx_flush_active_srbs(struct scsi_qla_host *ha)
{
	struct srb *srb;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (i = 0; i < ha->host->can_queue; i++) {
		srb = qla4xxx_del_from_active_array(ha, i);
		if (srb != NULL) {
			srb->cmd->result = DID_RESET << 16;
			qla4xxx_srb_compl(ha, srb);
		}
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

}

/**
 * qla4xxx_recover_adapter - recovers adapter after a fatal error
 * @ha: Pointer to host adapter structure.
 * @renew_ddb_list: Indicates what to do with the adapter's ddb list
 *	after adapter recovery has completed.
 *	0=preserve ddb list, 1=destroy and rebuild ddb list
 **/
static int qla4xxx_recover_adapter(struct scsi_qla_host *ha,
				uint8_t renew_ddb_list)
{
	int status;

	/* Stall incoming I/O until we are done */
	clear_bit(AF_ONLINE, &ha->flags);
	DEBUG2(printk("scsi%ld: %s calling qla4xxx_cmd_wait\n", ha->host_no,
		      __func__));

	/* Wait for outstanding commands to complete.
	 * Stalls the driver for max 30 secs
	 */
	status = qla4xxx_cmd_wait(ha);

	qla4xxx_disable_intrs(ha);

	/* Flush any pending ddb changed AENs */
	qla4xxx_process_aen(ha, FLUSH_DDB_CHANGED_AENS);

	/* Reset the firmware.	If successful, function
	 * returns with ISP interrupts enabled.
	 */
	if (status == QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s - Performing soft reset..\n",
			      ha->host_no, __func__));
		qla4xxx_flush_active_srbs(ha);
		if (ql4xxx_lock_drvr_wait(ha) == QLA_SUCCESS)
			status = qla4xxx_soft_reset(ha);
		else
			status = QLA_ERROR;
	}

	/* Flush any pending ddb changed AENs */
	qla4xxx_process_aen(ha, FLUSH_DDB_CHANGED_AENS);

	/* Re-initialize firmware. If successful, function returns
	 * with ISP interrupts enabled */
	if (status == QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s - Initializing adapter..\n",
			      ha->host_no, __func__));

		/* If successful, AF_ONLINE flag set in
		 * qla4xxx_initialize_adapter */
		status = qla4xxx_initialize_adapter(ha, renew_ddb_list);
	}

	/* Failed adapter initialization?
	 * Retry reset_ha only if invoked via DPC (DPC_RESET_HA) */
	if ((test_bit(AF_ONLINE, &ha->flags) == 0) &&
	    (test_bit(DPC_RESET_HA, &ha->dpc_flags))) {
		/* Adapter initialization failed, see if we can retry
		 * resetting the ha */
		if (!test_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags)) {
			ha->retry_reset_ha_cnt = MAX_RESET_HA_RETRIES;
			DEBUG2(printk("scsi%ld: recover adapter - retrying "
				      "(%d) more times\n", ha->host_no,
				      ha->retry_reset_ha_cnt));
			set_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags);
			status = QLA_ERROR;
		} else {
			if (ha->retry_reset_ha_cnt > 0) {
				/* Schedule another Reset HA--DPC will retry */
				ha->retry_reset_ha_cnt--;
				DEBUG2(printk("scsi%ld: recover adapter - "
					      "retry remaining %d\n",
					      ha->host_no,
					      ha->retry_reset_ha_cnt));
				status = QLA_ERROR;
			}

			if (ha->retry_reset_ha_cnt == 0) {
				/* Recover adapter retries have been exhausted.
				 * Adapter DEAD */
				DEBUG2(printk("scsi%ld: recover adapter "
					      "failed - board disabled\n",
					      ha->host_no));
				qla4xxx_flush_active_srbs(ha);
				clear_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags);
				clear_bit(DPC_RESET_HA, &ha->dpc_flags);
				clear_bit(DPC_RESET_HA_DESTROY_DDB_LIST,
					  &ha->dpc_flags);
				status = QLA_ERROR;
			}
		}
	} else {
		clear_bit(DPC_RESET_HA, &ha->dpc_flags);
		clear_bit(DPC_RESET_HA_DESTROY_DDB_LIST, &ha->dpc_flags);
		clear_bit(DPC_RETRY_RESET_HA, &ha->dpc_flags);
	}

	ha->adapter_error_count++;

	if (status == QLA_SUCCESS)
		qla4xxx_enable_intrs(ha);

	DEBUG2(printk("scsi%ld: recover adapter .. DONE\n", ha->host_no));
	return status;
}

/**
 * qla4xxx_do_dpc - dpc routine
 * @data: in our case pointer to adapter structure
 *
 * This routine is a task that is schedule by the interrupt handler
 * to perform the background processing for interrupts.  We put it
 * on a task queue that is consumed whenever the scheduler runs; that's
 * so you can do anything (i.e. put the process to sleep etc).  In fact,
 * the mid-level tries to sleep when it reaches the driver threshold
 * "host->can_queue". This can cause a panic if we were in our interrupt code.
 **/
static void qla4xxx_do_dpc(struct work_struct *work)
{
	struct scsi_qla_host *ha =
		container_of(work, struct scsi_qla_host, dpc_work);
	struct ddb_entry *ddb_entry, *dtemp;
	int status = QLA_ERROR;

	DEBUG2(printk("scsi%ld: %s: DPC handler waking up."
		"flags = 0x%08lx, dpc_flags = 0x%08lx ctrl_stat = 0x%08x\n",
		ha->host_no, __func__, ha->flags, ha->dpc_flags,
		readw(&ha->reg->ctrl_status)));

	/* Initialization not yet finished. Don't do anything yet. */
	if (!test_bit(AF_INIT_DONE, &ha->flags))
		return;

	if (adapter_up(ha) ||
	    test_bit(DPC_RESET_HA, &ha->dpc_flags) ||
	    test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags) ||
	    test_bit(DPC_RESET_HA_DESTROY_DDB_LIST, &ha->dpc_flags)) {
		if (test_bit(DPC_RESET_HA_DESTROY_DDB_LIST, &ha->dpc_flags) ||
			test_bit(DPC_RESET_HA, &ha->dpc_flags))
			qla4xxx_recover_adapter(ha, PRESERVE_DDB_LIST);

		if (test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags)) {
			uint8_t wait_time = RESET_INTR_TOV;

			while ((readw(&ha->reg->ctrl_status) &
				(CSR_SOFT_RESET | CSR_FORCE_SOFT_RESET)) != 0) {
				if (--wait_time == 0)
					break;
				msleep(1000);
			}
			if (wait_time == 0)
				DEBUG2(printk("scsi%ld: %s: SR|FSR "
					      "bit not cleared-- resetting\n",
					      ha->host_no, __func__));
			qla4xxx_flush_active_srbs(ha);
			if (ql4xxx_lock_drvr_wait(ha) == QLA_SUCCESS) {
				qla4xxx_process_aen(ha, FLUSH_DDB_CHANGED_AENS);
				status = qla4xxx_initialize_adapter(ha,
						PRESERVE_DDB_LIST);
			}
			clear_bit(DPC_RESET_HA_INTR, &ha->dpc_flags);
			if (status == QLA_SUCCESS)
				qla4xxx_enable_intrs(ha);
		}
	}

	/* ---- process AEN? --- */
	if (test_and_clear_bit(DPC_AEN, &ha->dpc_flags))
		qla4xxx_process_aen(ha, PROCESS_ALL_AENS);

	/* ---- Get DHCP IP Address? --- */
	if (test_and_clear_bit(DPC_GET_DHCP_IP_ADDR, &ha->dpc_flags))
		qla4xxx_get_dhcp_ip_address(ha);

	/* ---- relogin device? --- */
	if (adapter_up(ha) &&
	    test_and_clear_bit(DPC_RELOGIN_DEVICE, &ha->dpc_flags)) {
		list_for_each_entry_safe(ddb_entry, dtemp,
					 &ha->ddb_list, list) {
			if (test_and_clear_bit(DF_RELOGIN, &ddb_entry->flags) &&
			    atomic_read(&ddb_entry->state) != DDB_STATE_ONLINE)
				qla4xxx_relogin_device(ha, ddb_entry);

			/*
			 * If mbx cmd times out there is no point
			 * in continuing further.
			 * With large no of targets this can hang
			 * the system.
			 */
			if (test_bit(DPC_RESET_HA, &ha->dpc_flags)) {
				printk(KERN_WARNING "scsi%ld: %s: "
				       "need to reset hba\n",
				       ha->host_no, __func__);
				break;
			}
		}
	}
}

/**
 * qla4xxx_free_adapter - release the adapter
 * @ha: pointer to adapter structure
 **/
static void qla4xxx_free_adapter(struct scsi_qla_host *ha)
{

	if (test_bit(AF_INTERRUPTS_ON, &ha->flags)) {
		/* Turn-off interrupts on the card. */
		qla4xxx_disable_intrs(ha);
	}

	/* Kill the kernel thread for this host */
	if (ha->dpc_thread)
		destroy_workqueue(ha->dpc_thread);

	/* Issue Soft Reset to put firmware in unknown state */
	if (ql4xxx_lock_drvr_wait(ha) == QLA_SUCCESS)
		qla4xxx_hw_reset(ha);

	/* Remove timer thread, if present */
	if (ha->timer_active)
		qla4xxx_stop_timer(ha);

	/* Detach interrupts */
	if (test_and_clear_bit(AF_IRQ_ATTACHED, &ha->flags))
		free_irq(ha->pdev->irq, ha);

	/* free extra memory */
	qla4xxx_mem_free(ha);

	pci_disable_device(ha->pdev);

}

/***
 * qla4xxx_iospace_config - maps registers
 * @ha: pointer to adapter structure
 *
 * This routines maps HBA's registers from the pci address space
 * into the kernel virtual address space for memory mapped i/o.
 **/
static int qla4xxx_iospace_config(struct scsi_qla_host *ha)
{
	unsigned long pio, pio_len, pio_flags;
	unsigned long mmio, mmio_len, mmio_flags;

	pio = pci_resource_start(ha->pdev, 0);
	pio_len = pci_resource_len(ha->pdev, 0);
	pio_flags = pci_resource_flags(ha->pdev, 0);
	if (pio_flags & IORESOURCE_IO) {
		if (pio_len < MIN_IOBASE_LEN) {
			dev_warn(&ha->pdev->dev,
				"Invalid PCI I/O region size\n");
			pio = 0;
		}
	} else {
		dev_warn(&ha->pdev->dev, "region #0 not a PIO resource\n");
		pio = 0;
	}

	/* Use MMIO operations for all accesses. */
	mmio = pci_resource_start(ha->pdev, 1);
	mmio_len = pci_resource_len(ha->pdev, 1);
	mmio_flags = pci_resource_flags(ha->pdev, 1);

	if (!(mmio_flags & IORESOURCE_MEM)) {
		dev_err(&ha->pdev->dev,
			"region #0 not an MMIO resource, aborting\n");

		goto iospace_error_exit;
	}
	if (mmio_len < MIN_IOBASE_LEN) {
		dev_err(&ha->pdev->dev,
			"Invalid PCI mem region size, aborting\n");
		goto iospace_error_exit;
	}

	if (pci_request_regions(ha->pdev, DRIVER_NAME)) {
		dev_warn(&ha->pdev->dev,
			"Failed to reserve PIO/MMIO regions\n");

		goto iospace_error_exit;
	}

	ha->pio_address = pio;
	ha->pio_length = pio_len;
	ha->reg = ioremap(mmio, MIN_IOBASE_LEN);
	if (!ha->reg) {
		dev_err(&ha->pdev->dev,
			"cannot remap MMIO, aborting\n");

		goto iospace_error_exit;
	}

	return 0;

iospace_error_exit:
	return -ENOMEM;
}

/**
 * qla4xxx_probe_adapter - callback function to probe HBA
 * @pdev: pointer to pci_dev structure
 * @pci_device_id: pointer to pci_device entry
 *
 * This routine will probe for Qlogic 4xxx iSCSI host adapters.
 * It returns zero if successful. It also initializes all data necessary for
 * the driver.
 **/
static int __devinit qla4xxx_probe_adapter(struct pci_dev *pdev,
					   const struct pci_device_id *ent)
{
	int ret = -ENODEV, status;
	struct Scsi_Host *host;
	struct scsi_qla_host *ha;
	struct ddb_entry *ddb_entry, *ddbtemp;
	uint8_t init_retry_count = 0;
	char buf[34];

	if (pci_enable_device(pdev))
		return -1;

	host = scsi_host_alloc(&qla4xxx_driver_template, sizeof(*ha));
	if (host == NULL) {
		printk(KERN_WARNING
		       "qla4xxx: Couldn't allocate host from scsi layer!\n");
		goto probe_disable_device;
	}

	/* Clear our data area */
	ha = (struct scsi_qla_host *) host->hostdata;
	memset(ha, 0, sizeof(*ha));

	/* Save the information from PCI BIOS.	*/
	ha->pdev = pdev;
	ha->host = host;
	ha->host_no = host->host_no;

	/* Configure PCI I/O space. */
	ret = qla4xxx_iospace_config(ha);
	if (ret)
		goto probe_failed;

	dev_info(&ha->pdev->dev, "Found an ISP%04x, irq %d, iobase 0x%p\n",
		   pdev->device, pdev->irq, ha->reg);

	qla4xxx_config_dma_addressing(ha);

	/* Initialize lists and spinlocks. */
	INIT_LIST_HEAD(&ha->ddb_list);
	INIT_LIST_HEAD(&ha->free_srb_q);

	mutex_init(&ha->mbox_sem);

	spin_lock_init(&ha->hardware_lock);

	/* Allocate dma buffers */
	if (qla4xxx_mem_alloc(ha)) {
		dev_warn(&ha->pdev->dev,
			   "[ERROR] Failed to allocate memory for adapter\n");

		ret = -ENOMEM;
		goto probe_failed;
	}

	/*
	 * Initialize the Host adapter request/response queues and
	 * firmware
	 * NOTE: interrupts enabled upon successful completion
	 */
	status = qla4xxx_initialize_adapter(ha, REBUILD_DDB_LIST);
	while (status == QLA_ERROR && init_retry_count++ < MAX_INIT_RETRIES) {
		DEBUG2(printk("scsi: %s: retrying adapter initialization "
			      "(%d)\n", __func__, init_retry_count));
		qla4xxx_soft_reset(ha);
		status = qla4xxx_initialize_adapter(ha, REBUILD_DDB_LIST);
	}
	if (status == QLA_ERROR) {
		dev_warn(&ha->pdev->dev, "Failed to initialize adapter\n");

		ret = -ENODEV;
		goto probe_failed;
	}

	host->cmd_per_lun = 3;
	host->max_channel = 0;
	host->max_lun = MAX_LUNS - 1;
	host->max_id = MAX_TARGETS;
	host->max_cmd_len = IOCB_MAX_CDB_LEN;
	host->can_queue = MAX_SRBS ;
	host->transportt = qla4xxx_scsi_transport;

        ret = scsi_init_shared_tag_map(host, MAX_SRBS);
        if (ret) {
                dev_warn(&ha->pdev->dev, "scsi_init_shared_tag_map failed");
                goto probe_failed;
        }

	/* Startup the kernel thread for this host adapter. */
	DEBUG2(printk("scsi: %s: Starting kernel thread for "
		      "qla4xxx_dpc\n", __func__));
	sprintf(buf, "qla4xxx_%lu_dpc", ha->host_no);
	ha->dpc_thread = create_singlethread_workqueue(buf);
	if (!ha->dpc_thread) {
		dev_warn(&ha->pdev->dev, "Unable to start DPC thread!\n");
		ret = -ENODEV;
		goto probe_failed;
	}
	INIT_WORK(&ha->dpc_work, qla4xxx_do_dpc);

	ret = request_irq(pdev->irq, qla4xxx_intr_handler,
			  IRQF_DISABLED | IRQF_SHARED, "qla4xxx", ha);
	if (ret) {
		dev_warn(&ha->pdev->dev, "Failed to reserve interrupt %d"
			" already in use.\n", pdev->irq);
		goto probe_failed;
	}
	set_bit(AF_IRQ_ATTACHED, &ha->flags);
	host->irq = pdev->irq;
	DEBUG(printk("scsi%d: irq %d attached\n", ha->host_no, ha->pdev->irq));

	qla4xxx_enable_intrs(ha);

	/* Start timer thread. */
	qla4xxx_start_timer(ha, qla4xxx_timer, 1);

	set_bit(AF_INIT_DONE, &ha->flags);

	pci_set_drvdata(pdev, ha);

	ret = scsi_add_host(host, &pdev->dev);
	if (ret)
		goto probe_failed;

	/* Update transport device information for all devices. */
	list_for_each_entry_safe(ddb_entry, ddbtemp, &ha->ddb_list, list) {
		if (ddb_entry->fw_ddb_device_state == DDB_DS_SESSION_ACTIVE)
			if (qla4xxx_add_sess(ddb_entry))
				goto remove_host;
	}

	printk(KERN_INFO
	       " QLogic iSCSI HBA Driver version: %s\n"
	       "  QLogic ISP%04x @ %s, host#=%ld, fw=%02d.%02d.%02d.%02d\n",
	       qla4xxx_version_str, ha->pdev->device, pci_name(ha->pdev),
	       ha->host_no, ha->firmware_version[0], ha->firmware_version[1],
	       ha->patch_number, ha->build_number);

	return 0;

remove_host:
	qla4xxx_free_ddb_list(ha);
	scsi_remove_host(host);

probe_failed:
	qla4xxx_free_adapter(ha);
	scsi_host_put(ha->host);

probe_disable_device:
	pci_disable_device(pdev);

	return ret;
}

/**
 * qla4xxx_remove_adapter - calback function to remove adapter.
 * @pci_dev: PCI device pointer
 **/
static void __devexit qla4xxx_remove_adapter(struct pci_dev *pdev)
{
	struct scsi_qla_host *ha;

	ha = pci_get_drvdata(pdev);

	qla4xxx_disable_intrs(ha);

	while (test_bit(DPC_RESET_HA_INTR, &ha->dpc_flags))
		ssleep(1);

	/* remove devs from iscsi_sessions to scsi_devices */
	qla4xxx_free_ddb_list(ha);

	scsi_remove_host(ha->host);

	qla4xxx_free_adapter(ha);

	scsi_host_put(ha->host);

	pci_set_drvdata(pdev, NULL);
}

/**
 * qla4xxx_config_dma_addressing() - Configure OS DMA addressing method.
 * @ha: HA context
 *
 * At exit, the @ha's flags.enable_64bit_addressing set to indicated
 * supported addressing method.
 */
static void qla4xxx_config_dma_addressing(struct scsi_qla_host *ha)
{
	int retval;

	/* Update our PCI device dma_mask for full 64 bit mask */
	if (pci_set_dma_mask(ha->pdev, DMA_64BIT_MASK) == 0) {
		if (pci_set_consistent_dma_mask(ha->pdev, DMA_64BIT_MASK)) {
			dev_dbg(&ha->pdev->dev,
				  "Failed to set 64 bit PCI consistent mask; "
				   "using 32 bit.\n");
			retval = pci_set_consistent_dma_mask(ha->pdev,
							     DMA_32BIT_MASK);
		}
	} else
		retval = pci_set_dma_mask(ha->pdev, DMA_32BIT_MASK);
}

static int qla4xxx_slave_alloc(struct scsi_device *sdev)
{
	struct iscsi_cls_session *sess = starget_to_session(sdev->sdev_target);
	struct ddb_entry *ddb = sess->dd_data;

	sdev->hostdata = ddb;
	sdev->tagged_supported = 1;
	scsi_activate_tcq(sdev, sdev->host->can_queue);
	return 0;
}

static int qla4xxx_slave_configure(struct scsi_device *sdev)
{
	sdev->tagged_supported = 1;
	return 0;
}

static void qla4xxx_slave_destroy(struct scsi_device *sdev)
{
	scsi_deactivate_tcq(sdev, 1);
}

/**
 * qla4xxx_del_from_active_array - returns an active srb
 * @ha: Pointer to host adapter structure.
 * @index: index into to the active_array
 *
 * This routine removes and returns the srb at the specified index
 **/
struct srb * qla4xxx_del_from_active_array(struct scsi_qla_host *ha, uint32_t index)
{
	struct srb *srb = NULL;
	struct scsi_cmnd *cmd;

	if (!(cmd = scsi_host_find_tag(ha->host, index)))
		return srb;

	if (!(srb = (struct srb *)cmd->host_scribble))
		return srb;

	/* update counters */
	if (srb->flags & SRB_DMA_VALID) {
		ha->req_q_count += srb->iocb_cnt;
		ha->iocb_cnt -= srb->iocb_cnt;
		if (srb->cmd)
			srb->cmd->host_scribble = NULL;
	}
	return srb;
}

/**
 * qla4xxx_eh_wait_on_command - waits for command to be returned by firmware
 * @ha: actual ha whose done queue will contain the comd returned by firmware.
 * @cmd: Scsi Command to wait on.
 *
 * This routine waits for the command to be returned by the Firmware
 * for some max time.
 **/
static int qla4xxx_eh_wait_on_command(struct scsi_qla_host *ha,
				      struct scsi_cmnd *cmd)
{
	int done = 0;
	struct srb *rp;
	uint32_t max_wait_time = EH_WAIT_CMD_TOV;

	do {
		/* Checking to see if its returned to OS */
		rp = (struct srb *) cmd->SCp.ptr;
		if (rp == NULL) {
			done++;
			break;
		}

		msleep(2000);
	} while (max_wait_time--);

	return done;
}

/**
 * qla4xxx_wait_for_hba_online - waits for HBA to come online
 * @ha: Pointer to host adapter structure
 **/
static int qla4xxx_wait_for_hba_online(struct scsi_qla_host *ha)
{
	unsigned long wait_online;

	wait_online = jiffies + (30 * HZ);
	while (time_before(jiffies, wait_online)) {

		if (adapter_up(ha))
			return QLA_SUCCESS;
		else if (ha->retry_reset_ha_cnt == 0)
			return QLA_ERROR;

		msleep(2000);
	}

	return QLA_ERROR;
}

/**
 * qla4xxx_eh_wait_for_active_target_commands - wait for active cmds to finish.
 * @ha: pointer to to HBA
 * @t: target id
 * @l: lun id
 *
 * This function waits for all outstanding commands to a lun to complete. It
 * returns 0 if all pending commands are returned and 1 otherwise.
 **/
static int qla4xxx_eh_wait_for_active_target_commands(struct scsi_qla_host *ha,
						 int t, int l)
{
	int cnt;
	int status = 0;
	struct scsi_cmnd *cmd;

	/*
	 * Waiting for all commands for the designated target in the active
	 * array
	 */
	for (cnt = 0; cnt < ha->host->can_queue; cnt++) {
		cmd = scsi_host_find_tag(ha->host, cnt);
		if (cmd && cmd->device->id == t && cmd->device->lun == l) {
			if (!qla4xxx_eh_wait_on_command(ha, cmd)) {
				status++;
				break;
			}
		}
	}
	return status;
}

/**
 * qla4xxx_eh_device_reset - callback for target reset.
 * @cmd: Pointer to Linux's SCSI command structure
 *
 * This routine is called by the Linux OS to reset all luns on the
 * specified target.
 **/
static int qla4xxx_eh_device_reset(struct scsi_cmnd *cmd)
{
	struct scsi_qla_host *ha = to_qla_host(cmd->device->host);
	struct ddb_entry *ddb_entry = cmd->device->hostdata;
	struct srb *sp;
	int ret = FAILED, stat;

	sp = (struct srb *) cmd->SCp.ptr;
	if (!sp || !ddb_entry)
		return ret;

	dev_info(&ha->pdev->dev,
		   "scsi%ld:%d:%d:%d: DEVICE RESET ISSUED.\n", ha->host_no,
		   cmd->device->channel, cmd->device->id, cmd->device->lun);

	DEBUG2(printk(KERN_INFO
		      "scsi%ld: DEVICE_RESET cmd=%p jiffies = 0x%lx, to=%x,"
		      "dpc_flags=%lx, status=%x allowed=%d\n", ha->host_no,
		      cmd, jiffies, cmd->timeout_per_command / HZ,
		      ha->dpc_flags, cmd->result, cmd->allowed));

	/* FIXME: wait for hba to go online */
	stat = qla4xxx_reset_lun(ha, ddb_entry, cmd->device->lun);
	if (stat != QLA_SUCCESS) {
		dev_info(&ha->pdev->dev, "DEVICE RESET FAILED. %d\n", stat);
		goto eh_dev_reset_done;
	}

	/* Send marker. */
	ha->marker_needed = 1;

	/*
	 * If we are coming down the EH path, wait for all commands to complete
	 * for the device.
	 */
	if (cmd->device->host->shost_state == SHOST_RECOVERY) {
		if (qla4xxx_eh_wait_for_active_target_commands(ha,
							  cmd->device->id,
							  cmd->device->lun)){
			dev_info(&ha->pdev->dev,
				   "DEVICE RESET FAILED - waiting for "
				   "commands.\n");
			goto eh_dev_reset_done;
		}
	}

	dev_info(&ha->pdev->dev,
		   "scsi(%ld:%d:%d:%d): DEVICE RESET SUCCEEDED.\n",
		   ha->host_no, cmd->device->channel, cmd->device->id,
		   cmd->device->lun);

	ret = SUCCESS;

eh_dev_reset_done:

	return ret;
}

/**
 * qla4xxx_eh_host_reset - kernel callback
 * @cmd: Pointer to Linux's SCSI command structure
 *
 * This routine is invoked by the Linux kernel to perform fatal error
 * recovery on the specified adapter.
 **/
static int qla4xxx_eh_host_reset(struct scsi_cmnd *cmd)
{
	int return_status = FAILED;
	struct scsi_qla_host *ha;

	ha = (struct scsi_qla_host *) cmd->device->host->hostdata;

	dev_info(&ha->pdev->dev,
		   "scsi(%ld:%d:%d:%d): ADAPTER RESET ISSUED.\n", ha->host_no,
		   cmd->device->channel, cmd->device->id, cmd->device->lun);

	if (qla4xxx_wait_for_hba_online(ha) != QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld:%d: %s: Unable to reset host.  Adapter "
			      "DEAD.\n", ha->host_no, cmd->device->channel,
			      __func__));

		return FAILED;
	}

	if (qla4xxx_recover_adapter(ha, PRESERVE_DDB_LIST) == QLA_SUCCESS) {
		return_status = SUCCESS;
	}

	dev_info(&ha->pdev->dev, "HOST RESET %s.\n",
		   return_status == FAILED ? "FAILED" : "SUCCEDED");

	return return_status;
}


static struct pci_device_id qla4xxx_pci_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP4010,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP4022,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP4032,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{0, 0},
};
MODULE_DEVICE_TABLE(pci, qla4xxx_pci_tbl);

static struct pci_driver qla4xxx_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= qla4xxx_pci_tbl,
	.probe		= qla4xxx_probe_adapter,
	.remove		= qla4xxx_remove_adapter,
};

static int __init qla4xxx_module_init(void)
{
	int ret;

	/* Allocate cache for SRBs. */
	srb_cachep = kmem_cache_create("qla4xxx_srbs", sizeof(struct srb), 0,
				       SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (srb_cachep == NULL) {
		printk(KERN_ERR
		       "%s: Unable to allocate SRB cache..."
		       "Failing load!\n", DRIVER_NAME);
		ret = -ENOMEM;
		goto no_srp_cache;
	}

	/* Derive version string. */
	strcpy(qla4xxx_version_str, QLA4XXX_DRIVER_VERSION);
	if (ql4xextended_error_logging)
		strcat(qla4xxx_version_str, "-debug");

	qla4xxx_scsi_transport =
		iscsi_register_transport(&qla4xxx_iscsi_transport);
	if (!qla4xxx_scsi_transport){
		ret = -ENODEV;
		goto release_srb_cache;
	}

	ret = pci_register_driver(&qla4xxx_pci_driver);
	if (ret)
		goto unregister_transport;

	printk(KERN_INFO "QLogic iSCSI HBA Driver\n");
	return 0;

unregister_transport:
	iscsi_unregister_transport(&qla4xxx_iscsi_transport);
release_srb_cache:
	kmem_cache_destroy(srb_cachep);
no_srp_cache:
	return ret;
}

static void __exit qla4xxx_module_exit(void)
{
	ql4_mod_unload = 1;
	pci_unregister_driver(&qla4xxx_pci_driver);
	iscsi_unregister_transport(&qla4xxx_iscsi_transport);
	kmem_cache_destroy(srb_cachep);
}

module_init(qla4xxx_module_init);
module_exit(qla4xxx_module_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic iSCSI HBA Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(QLA4XXX_DRIVER_VERSION);
