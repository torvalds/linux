/* 
 * 
 * linux/drivers/s390/scsi/zfcp_erp.c 
 * 
 * FCP adapter driver for IBM eServer zSeries 
 * 
 * (C) Copyright IBM Corp. 2002, 2004
 *
 * Author(s): Martin Peschke <mpeschke@de.ibm.com> 
 *            Raimund Schroeder <raimund.schroeder@de.ibm.com> 
 *            Aron Zeh
 *            Wolfgang Taphorn
 *            Stefan Bader <stefan.bader@de.ibm.com> 
 *            Heiko Carstens <heiko.carstens@de.ibm.com> 
 *            Andreas Herrmann <aherrman@de.ibm.com>
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

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP

#include "zfcp_ext.h"

static int zfcp_erp_adisc(struct zfcp_port *);
static void zfcp_erp_adisc_handler(unsigned long);

static int zfcp_erp_adapter_reopen_internal(struct zfcp_adapter *, int);
static int zfcp_erp_port_forced_reopen_internal(struct zfcp_port *, int);
static int zfcp_erp_port_reopen_internal(struct zfcp_port *, int);
static int zfcp_erp_unit_reopen_internal(struct zfcp_unit *, int);

static int zfcp_erp_port_reopen_all_internal(struct zfcp_adapter *, int);
static int zfcp_erp_unit_reopen_all_internal(struct zfcp_port *, int);

static void zfcp_erp_adapter_block(struct zfcp_adapter *, int);
static void zfcp_erp_adapter_unblock(struct zfcp_adapter *);
static void zfcp_erp_port_block(struct zfcp_port *, int);
static void zfcp_erp_port_unblock(struct zfcp_port *);
static void zfcp_erp_unit_block(struct zfcp_unit *, int);
static void zfcp_erp_unit_unblock(struct zfcp_unit *);

static int zfcp_erp_thread(void *);

static int zfcp_erp_strategy(struct zfcp_erp_action *);

static int zfcp_erp_strategy_do_action(struct zfcp_erp_action *);
static int zfcp_erp_strategy_memwait(struct zfcp_erp_action *);
static int zfcp_erp_strategy_check_target(struct zfcp_erp_action *, int);
static int zfcp_erp_strategy_check_unit(struct zfcp_unit *, int);
static int zfcp_erp_strategy_check_port(struct zfcp_port *, int);
static int zfcp_erp_strategy_check_adapter(struct zfcp_adapter *, int);
static int zfcp_erp_strategy_statechange(int, u32, struct zfcp_adapter *,
					 struct zfcp_port *,
					 struct zfcp_unit *, int);
static inline int zfcp_erp_strategy_statechange_detected(atomic_t *, u32);
static int zfcp_erp_strategy_followup_actions(int, struct zfcp_adapter *,
					      struct zfcp_port *,
					      struct zfcp_unit *, int);
static int zfcp_erp_strategy_check_queues(struct zfcp_adapter *);
static int zfcp_erp_strategy_check_action(struct zfcp_erp_action *, int);

static int zfcp_erp_adapter_strategy(struct zfcp_erp_action *);
static int zfcp_erp_adapter_strategy_generic(struct zfcp_erp_action *, int);
static int zfcp_erp_adapter_strategy_close(struct zfcp_erp_action *);
static int zfcp_erp_adapter_strategy_close_qdio(struct zfcp_erp_action *);
static int zfcp_erp_adapter_strategy_close_fsf(struct zfcp_erp_action *);
static int zfcp_erp_adapter_strategy_open(struct zfcp_erp_action *);
static int zfcp_erp_adapter_strategy_open_qdio(struct zfcp_erp_action *);
static int zfcp_erp_adapter_strategy_open_fsf(struct zfcp_erp_action *);
static int zfcp_erp_adapter_strategy_open_fsf_xconfig(struct zfcp_erp_action *);
static int zfcp_erp_adapter_strategy_open_fsf_xport(struct zfcp_erp_action *);
static int zfcp_erp_adapter_strategy_open_fsf_statusread(
	struct zfcp_erp_action *);

static int zfcp_erp_port_forced_strategy(struct zfcp_erp_action *);
static int zfcp_erp_port_forced_strategy_close(struct zfcp_erp_action *);

static int zfcp_erp_port_strategy(struct zfcp_erp_action *);
static int zfcp_erp_port_strategy_clearstati(struct zfcp_port *);
static int zfcp_erp_port_strategy_close(struct zfcp_erp_action *);
static int zfcp_erp_port_strategy_open(struct zfcp_erp_action *);
static int zfcp_erp_port_strategy_open_nameserver(struct zfcp_erp_action *);
static int zfcp_erp_port_strategy_open_nameserver_wakeup(
	struct zfcp_erp_action *);
static int zfcp_erp_port_strategy_open_common(struct zfcp_erp_action *);
static int zfcp_erp_port_strategy_open_common_lookup(struct zfcp_erp_action *);
static int zfcp_erp_port_strategy_open_port(struct zfcp_erp_action *);

static int zfcp_erp_unit_strategy(struct zfcp_erp_action *);
static int zfcp_erp_unit_strategy_clearstati(struct zfcp_unit *);
static int zfcp_erp_unit_strategy_close(struct zfcp_erp_action *);
static int zfcp_erp_unit_strategy_open(struct zfcp_erp_action *);

static int zfcp_erp_action_dismiss_adapter(struct zfcp_adapter *);
static int zfcp_erp_action_dismiss_port(struct zfcp_port *);
static int zfcp_erp_action_dismiss_unit(struct zfcp_unit *);
static int zfcp_erp_action_dismiss(struct zfcp_erp_action *);

static int zfcp_erp_action_enqueue(int, struct zfcp_adapter *,
				   struct zfcp_port *, struct zfcp_unit *);
static int zfcp_erp_action_dequeue(struct zfcp_erp_action *);
static void zfcp_erp_action_cleanup(int, struct zfcp_adapter *,
				    struct zfcp_port *, struct zfcp_unit *,
				    int);

static void zfcp_erp_action_ready(struct zfcp_erp_action *);
static int  zfcp_erp_action_exists(struct zfcp_erp_action *);

static inline void zfcp_erp_action_to_ready(struct zfcp_erp_action *);
static inline void zfcp_erp_action_to_running(struct zfcp_erp_action *);

static void zfcp_erp_memwait_handler(unsigned long);
static void zfcp_erp_timeout_handler(unsigned long);
static inline void zfcp_erp_timeout_init(struct zfcp_erp_action *);

/**
 * zfcp_fsf_request_timeout_handler - called if a request timed out
 * @data: pointer to adapter for handler function
 *
 * This function needs to be called if requests (ELS, Generic Service,
 * or SCSI commands) exceed a certain time limit. The assumption is
 * that after the time limit the adapter get stuck. So we trigger a reopen of
 * the adapter. This should not be used for error recovery, SCSI abort
 * commands and SCSI requests from SCSI mid-layer.
 */
void
zfcp_fsf_request_timeout_handler(unsigned long data)
{
	struct zfcp_adapter *adapter;

	adapter = (struct zfcp_adapter *) data;

	zfcp_erp_adapter_reopen(adapter, 0);
}

/*
 * function:	zfcp_fsf_scsi_er_timeout_handler
 *
 * purpose:     This function needs to be called whenever a SCSI error recovery
 *              action (abort/reset) does not return.
 *              Re-opening the adapter means that the command can be returned
 *              by zfcp (it is guarranteed that it does not return via the
 *              adapter anymore). The buffer can then be used again.
 *    
 * returns:     sod all
 */
void
zfcp_fsf_scsi_er_timeout_handler(unsigned long data)
{
	struct zfcp_adapter *adapter = (struct zfcp_adapter *) data;

	ZFCP_LOG_NORMAL("warning: SCSI error recovery timed out. "
			"Restarting all operations on the adapter %s\n",
			zfcp_get_busid_by_adapter(adapter));
	debug_text_event(adapter->erp_dbf, 1, "eh_lmem_tout");
	zfcp_erp_adapter_reopen(adapter, 0);

	return;
}

/*
 * function:	
 *
 * purpose:	called if an adapter failed,
 *		initiates adapter recovery which is done
 *		asynchronously
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
int
zfcp_erp_adapter_reopen_internal(struct zfcp_adapter *adapter, int clear_mask)
{
	int retval;

	debug_text_event(adapter->erp_dbf, 5, "a_ro");
	ZFCP_LOG_DEBUG("reopen adapter %s\n",
		       zfcp_get_busid_by_adapter(adapter));

	zfcp_erp_adapter_block(adapter, clear_mask);

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &adapter->status)) {
		ZFCP_LOG_DEBUG("skipped reopen of failed adapter %s\n",
			       zfcp_get_busid_by_adapter(adapter));
		debug_text_event(adapter->erp_dbf, 5, "a_ro_f");
		/* ensure propagation of failed status to new devices */
		zfcp_erp_adapter_failed(adapter);
		retval = -EIO;
		goto out;
	}
	retval = zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_ADAPTER,
					 adapter, NULL, NULL);

 out:
	return retval;
}

/*
 * function:	
 *
 * purpose:	Wrappper for zfcp_erp_adapter_reopen_internal
 *              used to ensure the correct locking
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
int
zfcp_erp_adapter_reopen(struct zfcp_adapter *adapter, int clear_mask)
{
	int retval;
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);
	retval = zfcp_erp_adapter_reopen_internal(adapter, clear_mask);
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
int
zfcp_erp_adapter_shutdown(struct zfcp_adapter *adapter, int clear_mask)
{
	int retval;

	retval = zfcp_erp_adapter_reopen(adapter,
					 ZFCP_STATUS_COMMON_RUNNING |
					 ZFCP_STATUS_COMMON_ERP_FAILED |
					 clear_mask);

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
int
zfcp_erp_port_shutdown(struct zfcp_port *port, int clear_mask)
{
	int retval;

	retval = zfcp_erp_port_reopen(port,
				      ZFCP_STATUS_COMMON_RUNNING |
				      ZFCP_STATUS_COMMON_ERP_FAILED |
				      clear_mask);

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
int
zfcp_erp_unit_shutdown(struct zfcp_unit *unit, int clear_mask)
{
	int retval;

	retval = zfcp_erp_unit_reopen(unit,
				      ZFCP_STATUS_COMMON_RUNNING |
				      ZFCP_STATUS_COMMON_ERP_FAILED |
				      clear_mask);

	return retval;
}


/**
 * zfcp_erp_adisc - send ADISC ELS command
 * @port: port structure
 */
int
zfcp_erp_adisc(struct zfcp_port *port)
{
	struct zfcp_adapter *adapter = port->adapter;
	struct zfcp_send_els *send_els;
	struct zfcp_ls_adisc *adisc;
	void *address = NULL;
	int retval = 0;
	struct timer_list *timer;

	send_els = kmalloc(sizeof(struct zfcp_send_els), GFP_ATOMIC);
	if (send_els == NULL)
		goto nomem;
	memset(send_els, 0, sizeof(*send_els));

	send_els->req = kmalloc(sizeof(struct scatterlist), GFP_ATOMIC);
	if (send_els->req == NULL)
		goto nomem;
	memset(send_els->req, 0, sizeof(*send_els->req));

	send_els->resp = kmalloc(sizeof(struct scatterlist), GFP_ATOMIC);
	if (send_els->resp == NULL)
		goto nomem;
	memset(send_els->resp, 0, sizeof(*send_els->resp));

	address = (void *) get_zeroed_page(GFP_ATOMIC);
	if (address == NULL)
		goto nomem;

	zfcp_address_to_sg(address, send_els->req);
	address += PAGE_SIZE >> 1;
	zfcp_address_to_sg(address, send_els->resp);
	send_els->req_count = send_els->resp_count = 1;

	send_els->adapter = adapter;
	send_els->port = port;
	send_els->d_id = port->d_id;
	send_els->handler = zfcp_erp_adisc_handler;
	send_els->handler_data = (unsigned long) send_els;

	adisc = zfcp_sg_to_address(send_els->req);
	send_els->ls_code = adisc->code = ZFCP_LS_ADISC;

	send_els->req->length = sizeof(struct zfcp_ls_adisc);
	send_els->resp->length = sizeof(struct zfcp_ls_adisc_acc);

	/* acc. to FC-FS, hard_nport_id in ADISC should not be set for ports
	   without FC-AL-2 capability, so we don't set it */
	adisc->wwpn = fc_host_port_name(adapter->scsi_host);
	adisc->wwnn = fc_host_node_name(adapter->scsi_host);
	adisc->nport_id = fc_host_port_id(adapter->scsi_host);
	ZFCP_LOG_INFO("ADISC request from s_id 0x%08x to d_id 0x%08x "
		      "(wwpn=0x%016Lx, wwnn=0x%016Lx, "
		      "hard_nport_id=0x%08x, nport_id=0x%08x)\n",
		      adisc->nport_id, send_els->d_id, (wwn_t) adisc->wwpn,
		      (wwn_t) adisc->wwnn, adisc->hard_nport_id,
		      adisc->nport_id);

	timer = kmalloc(sizeof(struct timer_list), GFP_ATOMIC);
	if (!timer)
		goto nomem;

	init_timer(timer);
	timer->function = zfcp_fsf_request_timeout_handler;
	timer->data = (unsigned long) adapter;
	timer->expires = ZFCP_FSF_REQUEST_TIMEOUT;
	send_els->timer = timer;

	retval = zfcp_fsf_send_els(send_els);
	if (retval != 0) {
		ZFCP_LOG_NORMAL("error: initiation of Send ELS failed for port "
				"0x%08x on adapter %s\n", send_els->d_id,
				zfcp_get_busid_by_adapter(adapter));
		del_timer(send_els->timer);
		goto freemem;
	}

	goto out;

 nomem:
	retval = -ENOMEM;
 freemem:
	if (address != NULL)
		__free_pages(send_els->req->page, 0);
	if (send_els != NULL) {
		kfree(send_els->timer);
		kfree(send_els->req);
		kfree(send_els->resp);
		kfree(send_els);
	}
 out:
	return retval;
}


/**
 * zfcp_erp_adisc_handler - handler for ADISC ELS command
 * @data: pointer to struct zfcp_send_els
 *
 * If ADISC failed (LS_RJT or timed out) forced reopen of the port is triggered.
 */
void
zfcp_erp_adisc_handler(unsigned long data)
{
	struct zfcp_send_els *send_els;
	struct zfcp_port *port;
	struct zfcp_adapter *adapter;
	u32 d_id;
	struct zfcp_ls_adisc_acc *adisc;

	send_els = (struct zfcp_send_els *) data;

	del_timer(send_els->timer);

	adapter = send_els->adapter;
	port = send_els->port;
	d_id = send_els->d_id;

	/* request rejected or timed out */
	if (send_els->status != 0) {
		ZFCP_LOG_NORMAL("ELS request rejected/timed out, "
				"force physical port reopen "
				"(adapter %s, port d_id=0x%08x)\n",
				zfcp_get_busid_by_adapter(adapter), d_id);
		debug_text_event(adapter->erp_dbf, 3, "forcreop");
		if (zfcp_erp_port_forced_reopen(port, 0))
			ZFCP_LOG_NORMAL("failed reopen of port "
					"(adapter %s, wwpn=0x%016Lx)\n",
					zfcp_get_busid_by_port(port),
					port->wwpn);
		goto out;
	}

	adisc = zfcp_sg_to_address(send_els->resp);

	ZFCP_LOG_INFO("ADISC response from d_id 0x%08x to s_id "
		      "0x%08x (wwpn=0x%016Lx, wwnn=0x%016Lx, "
		      "hard_nport_id=0x%08x, nport_id=0x%08x)\n",
		      d_id, fc_host_port_id(adapter->scsi_host),
		      (wwn_t) adisc->wwpn, (wwn_t) adisc->wwnn,
		      adisc->hard_nport_id, adisc->nport_id);

	/* set wwnn for port */
	if (port->wwnn == 0)
		port->wwnn = adisc->wwnn;

	if (port->wwpn != adisc->wwpn) {
		ZFCP_LOG_NORMAL("d_id assignment changed, reopening "
				"port (adapter %s, wwpn=0x%016Lx, "
				"adisc_resp_wwpn=0x%016Lx)\n",
				zfcp_get_busid_by_port(port),
				port->wwpn, (wwn_t) adisc->wwpn);
		if (zfcp_erp_port_reopen(port, 0))
			ZFCP_LOG_NORMAL("failed reopen of port "
					"(adapter %s, wwpn=0x%016Lx)\n",
					zfcp_get_busid_by_port(port),
					port->wwpn);
	}

 out:
	zfcp_port_put(port);
	__free_pages(send_els->req->page, 0);
	kfree(send_els->timer);
	kfree(send_els->req);
	kfree(send_els->resp);
	kfree(send_els);
}


/**
 * zfcp_test_link - lightweight link test procedure
 * @port: port to be tested
 *
 * Test status of a link to a remote port using the ELS command ADISC.
 */
int
zfcp_test_link(struct zfcp_port *port)
{
	int retval;

	zfcp_port_get(port);
	retval = zfcp_erp_adisc(port);
	if (retval != 0) {
		zfcp_port_put(port);
		ZFCP_LOG_NORMAL("reopen needed for port 0x%016Lx "
				"on adapter %s\n ", port->wwpn,
				zfcp_get_busid_by_port(port));
		retval = zfcp_erp_port_forced_reopen(port, 0);
		if (retval != 0) {
			ZFCP_LOG_NORMAL("reopen of remote port 0x%016Lx "
					"on adapter %s failed\n", port->wwpn,
					zfcp_get_busid_by_port(port));
			retval = -EPERM;
		}
	}

	return retval;
}


/*
 * function:	
 *
 * purpose:	called if a port failed to be opened normally
 *		initiates Forced Reopen recovery which is done
 *		asynchronously
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
static int
zfcp_erp_port_forced_reopen_internal(struct zfcp_port *port, int clear_mask)
{
	int retval;
	struct zfcp_adapter *adapter = port->adapter;

	debug_text_event(adapter->erp_dbf, 5, "pf_ro");
	debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));

	ZFCP_LOG_DEBUG("forced reopen of port 0x%016Lx on adapter %s\n",
		       port->wwpn, zfcp_get_busid_by_port(port));

	zfcp_erp_port_block(port, clear_mask);

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &port->status)) {
		ZFCP_LOG_DEBUG("skipped forced reopen of failed port 0x%016Lx "
			       "on adapter %s\n", port->wwpn,
			       zfcp_get_busid_by_port(port));
		debug_text_event(adapter->erp_dbf, 5, "pf_ro_f");
		debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));
		retval = -EIO;
		goto out;
	}

	retval = zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_PORT_FORCED,
					 port->adapter, port, NULL);

 out:
	return retval;
}

/*
 * function:	
 *
 * purpose:	Wrappper for zfcp_erp_port_forced_reopen_internal
 *              used to ensure the correct locking
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
int
zfcp_erp_port_forced_reopen(struct zfcp_port *port, int clear_mask)
{
	int retval;
	unsigned long flags;
	struct zfcp_adapter *adapter;

	adapter = port->adapter;
	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);
	retval = zfcp_erp_port_forced_reopen_internal(port, clear_mask);
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	return retval;
}

/*
 * function:	
 *
 * purpose:	called if a port is to be opened
 *		initiates Reopen recovery which is done
 *		asynchronously
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
static int
zfcp_erp_port_reopen_internal(struct zfcp_port *port, int clear_mask)
{
	int retval;
	struct zfcp_adapter *adapter = port->adapter;

	debug_text_event(adapter->erp_dbf, 5, "p_ro");
	debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));

	ZFCP_LOG_DEBUG("reopen of port 0x%016Lx on adapter %s\n",
		       port->wwpn, zfcp_get_busid_by_port(port));

	zfcp_erp_port_block(port, clear_mask);

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &port->status)) {
		ZFCP_LOG_DEBUG("skipped reopen of failed port 0x%016Lx "
			       "on adapter %s\n", port->wwpn,
			       zfcp_get_busid_by_port(port));
		debug_text_event(adapter->erp_dbf, 5, "p_ro_f");
		debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));
		/* ensure propagation of failed status to new devices */
		zfcp_erp_port_failed(port);
		retval = -EIO;
		goto out;
	}

	retval = zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_PORT,
					 port->adapter, port, NULL);

 out:
	return retval;
}

/**
 * zfcp_erp_port_reopen - initiate reopen of a remote port
 * @port: port to be reopened
 * @clear_mask: specifies flags in port status to be cleared
 * Return: 0 on success, < 0 on error
 *
 * This is a wrappper function for zfcp_erp_port_reopen_internal. It ensures
 * correct locking. An error recovery task is initiated to do the reopen.
 * To wait for the completion of the reopen zfcp_erp_wait should be used.
 */
int
zfcp_erp_port_reopen(struct zfcp_port *port, int clear_mask)
{
	int retval;
	unsigned long flags;
	struct zfcp_adapter *adapter = port->adapter;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);
	retval = zfcp_erp_port_reopen_internal(port, clear_mask);
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	return retval;
}

/*
 * function:	
 *
 * purpose:	called if a unit is to be opened
 *		initiates Reopen recovery which is done
 *		asynchronously
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
static int
zfcp_erp_unit_reopen_internal(struct zfcp_unit *unit, int clear_mask)
{
	int retval;
	struct zfcp_adapter *adapter = unit->port->adapter;

	debug_text_event(adapter->erp_dbf, 5, "u_ro");
	debug_event(adapter->erp_dbf, 5, &unit->fcp_lun, sizeof (fcp_lun_t));
	ZFCP_LOG_DEBUG("reopen of unit 0x%016Lx on port 0x%016Lx "
		       "on adapter %s\n", unit->fcp_lun,
		       unit->port->wwpn, zfcp_get_busid_by_unit(unit));

	zfcp_erp_unit_block(unit, clear_mask);

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &unit->status)) {
		ZFCP_LOG_DEBUG("skipped reopen of failed unit 0x%016Lx "
			       "on port 0x%016Lx on adapter %s\n",
			       unit->fcp_lun, unit->port->wwpn,
			       zfcp_get_busid_by_unit(unit));
		debug_text_event(adapter->erp_dbf, 5, "u_ro_f");
		debug_event(adapter->erp_dbf, 5, &unit->fcp_lun,
			    sizeof (fcp_lun_t));
		retval = -EIO;
		goto out;
	}

	retval = zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_UNIT,
					 unit->port->adapter, unit->port, unit);
 out:
	return retval;
}

/**
 * zfcp_erp_unit_reopen - initiate reopen of a unit
 * @unit: unit to be reopened
 * @clear_mask: specifies flags in unit status to be cleared
 * Return: 0 on success, < 0 on error
 *
 * This is a wrappper for zfcp_erp_unit_reopen_internal. It ensures correct
 * locking. An error recovery task is initiated to do the reopen.
 * To wait for the completion of the reopen zfcp_erp_wait should be used.
 */
int
zfcp_erp_unit_reopen(struct zfcp_unit *unit, int clear_mask)
{
	int retval;
	unsigned long flags;
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;

	port = unit->port;
	adapter = port->adapter;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);
	retval = zfcp_erp_unit_reopen_internal(unit, clear_mask);
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	return retval;
}

/*
 * function:	
 *
 * purpose:	disable I/O,
 *		return any open requests and clean them up,
 *		aim: no pending and incoming I/O
 *
 * returns:
 */
static void
zfcp_erp_adapter_block(struct zfcp_adapter *adapter, int clear_mask)
{
	debug_text_event(adapter->erp_dbf, 6, "a_bl");
	zfcp_erp_modify_adapter_status(adapter,
				       ZFCP_STATUS_COMMON_UNBLOCKED |
				       clear_mask, ZFCP_CLEAR);
}

/*
 * function:	
 *
 * purpose:	enable I/O
 *
 * returns:
 */
static void
zfcp_erp_adapter_unblock(struct zfcp_adapter *adapter)
{
	debug_text_event(adapter->erp_dbf, 6, "a_ubl");
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &adapter->status);
}

/*
 * function:	
 *
 * purpose:	disable I/O,
 *		return any open requests and clean them up,
 *		aim: no pending and incoming I/O
 *
 * returns:
 */
static void
zfcp_erp_port_block(struct zfcp_port *port, int clear_mask)
{
	struct zfcp_adapter *adapter = port->adapter;

	debug_text_event(adapter->erp_dbf, 6, "p_bl");
	debug_event(adapter->erp_dbf, 6, &port->wwpn, sizeof (wwn_t));
	zfcp_erp_modify_port_status(port,
				    ZFCP_STATUS_COMMON_UNBLOCKED | clear_mask,
				    ZFCP_CLEAR);
}

/*
 * function:	
 *
 * purpose:	enable I/O
 *
 * returns:
 */
static void
zfcp_erp_port_unblock(struct zfcp_port *port)
{
	struct zfcp_adapter *adapter = port->adapter;

	debug_text_event(adapter->erp_dbf, 6, "p_ubl");
	debug_event(adapter->erp_dbf, 6, &port->wwpn, sizeof (wwn_t));
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &port->status);
}

/*
 * function:	
 *
 * purpose:	disable I/O,
 *		return any open requests and clean them up,
 *		aim: no pending and incoming I/O
 *
 * returns:
 */
static void
zfcp_erp_unit_block(struct zfcp_unit *unit, int clear_mask)
{
	struct zfcp_adapter *adapter = unit->port->adapter;

	debug_text_event(adapter->erp_dbf, 6, "u_bl");
	debug_event(adapter->erp_dbf, 6, &unit->fcp_lun, sizeof (fcp_lun_t));
	zfcp_erp_modify_unit_status(unit,
				    ZFCP_STATUS_COMMON_UNBLOCKED | clear_mask,
				    ZFCP_CLEAR);
}

/*
 * function:	
 *
 * purpose:	enable I/O
 *
 * returns:
 */
static void
zfcp_erp_unit_unblock(struct zfcp_unit *unit)
{
	struct zfcp_adapter *adapter = unit->port->adapter;

	debug_text_event(adapter->erp_dbf, 6, "u_ubl");
	debug_event(adapter->erp_dbf, 6, &unit->fcp_lun, sizeof (fcp_lun_t));
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &unit->status);
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static void
zfcp_erp_action_ready(struct zfcp_erp_action *erp_action)
{
	struct zfcp_adapter *adapter = erp_action->adapter;

	debug_text_event(adapter->erp_dbf, 4, "a_ar");
	debug_event(adapter->erp_dbf, 4, &erp_action->action, sizeof (int));

	zfcp_erp_action_to_ready(erp_action);
	up(&adapter->erp_ready_sem);
}

/*
 * function:	
 *
 * purpose:
 *
 * returns:	<0			erp_action not found in any list
 *		ZFCP_ERP_ACTION_READY	erp_action is in ready list
 *		ZFCP_ERP_ACTION_RUNNING	erp_action is in running list
 *
 * locks:	erp_lock must be held
 */
static int
zfcp_erp_action_exists(struct zfcp_erp_action *erp_action)
{
	int retval = -EINVAL;
	struct list_head *entry;
	struct zfcp_erp_action *entry_erp_action;
	struct zfcp_adapter *adapter = erp_action->adapter;

	/* search in running list */
	list_for_each(entry, &adapter->erp_running_head) {
		entry_erp_action =
		    list_entry(entry, struct zfcp_erp_action, list);
		if (entry_erp_action == erp_action) {
			retval = ZFCP_ERP_ACTION_RUNNING;
			goto out;
		}
	}
	/* search in ready list */
	list_for_each(entry, &adapter->erp_ready_head) {
		entry_erp_action =
		    list_entry(entry, struct zfcp_erp_action, list);
		if (entry_erp_action == erp_action) {
			retval = ZFCP_ERP_ACTION_READY;
			goto out;
		}
	}

 out:
	return retval;
}

/*
 * purpose:	checks current status of action (timed out, dismissed, ...)
 *		and does appropriate preparations (dismiss fsf request, ...)
 *
 * locks:	called under erp_lock (disabled interrupts)
 *
 * returns:	0
 */
static int
zfcp_erp_strategy_check_fsfreq(struct zfcp_erp_action *erp_action)
{
	int retval = 0;
	struct zfcp_fsf_req *fsf_req = NULL;
	struct zfcp_adapter *adapter = erp_action->adapter;

	if (erp_action->fsf_req) {
		/* take lock to ensure that request is not being deleted meanwhile */
		spin_lock(&adapter->fsf_req_list_lock);
		/* check whether fsf req does still exist */
		list_for_each_entry(fsf_req, &adapter->fsf_req_list_head, list)
		    if (fsf_req == erp_action->fsf_req)
			break;
		if (fsf_req && (fsf_req->erp_action == erp_action)) {
			/* fsf_req still exists */
			debug_text_event(adapter->erp_dbf, 3, "a_ca_req");
			debug_event(adapter->erp_dbf, 3, &fsf_req,
				    sizeof (unsigned long));
			/* dismiss fsf_req of timed out or dismissed erp_action */
			if (erp_action->status & (ZFCP_STATUS_ERP_DISMISSED |
						  ZFCP_STATUS_ERP_TIMEDOUT)) {
				debug_text_event(adapter->erp_dbf, 3,
						 "a_ca_disreq");
				fsf_req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
			}
			if (erp_action->status & ZFCP_STATUS_ERP_TIMEDOUT) {
				ZFCP_LOG_NORMAL("error: erp step timed out "
						"(action=%d, fsf_req=%p)\n ",
						erp_action->action,
						erp_action->fsf_req);
			}
			/*
			 * If fsf_req is neither dismissed nor completed
			 * then keep it running asynchronously and don't mess
			 * with the association of erp_action and fsf_req.
			 */
			if (fsf_req->status & (ZFCP_STATUS_FSFREQ_COMPLETED |
					       ZFCP_STATUS_FSFREQ_DISMISSED)) {
				/* forget about association between fsf_req
				   and erp_action */
				fsf_req->erp_action = NULL;
				erp_action->fsf_req = NULL;
			}
		} else {
			debug_text_event(adapter->erp_dbf, 3, "a_ca_gonereq");
			/*
			 * even if this fsf_req has gone, forget about
			 * association between erp_action and fsf_req
			 */
			erp_action->fsf_req = NULL;
		}
		spin_unlock(&adapter->fsf_req_list_lock);
	} else
		debug_text_event(adapter->erp_dbf, 3, "a_ca_noreq");

	return retval;
}

/*
 * purpose:	generic handler for asynchronous events related to erp_action events
 *		(normal completion, time-out, dismissing, retry after
 *		low memory condition)
 *
 * note:	deletion of timer is not required (e.g. in case of a time-out),
 *		but a second try does no harm,
 *		we leave it in here to allow for greater simplification
 *
 * returns:	0 - there was an action to handle
 *		!0 - otherwise
 */
static int
zfcp_erp_async_handler_nolock(struct zfcp_erp_action *erp_action,
			      unsigned long set_mask)
{
	int retval;
	struct zfcp_adapter *adapter = erp_action->adapter;

	if (zfcp_erp_action_exists(erp_action) == ZFCP_ERP_ACTION_RUNNING) {
		debug_text_event(adapter->erp_dbf, 2, "a_asyh_ex");
		debug_event(adapter->erp_dbf, 2, &erp_action->action,
			    sizeof (int));
		if (!(set_mask & ZFCP_STATUS_ERP_TIMEDOUT))
			del_timer(&erp_action->timer);
		erp_action->status |= set_mask;
		zfcp_erp_action_ready(erp_action);
		retval = 0;
	} else {
		/* action is ready or gone - nothing to do */
		debug_text_event(adapter->erp_dbf, 3, "a_asyh_gone");
		debug_event(adapter->erp_dbf, 3, &erp_action->action,
			    sizeof (int));
		retval = 1;
	}

	return retval;
}

/*
 * purpose:	generic handler for asynchronous events related to erp_action
 *               events	(normal completion, time-out, dismissing, retry after
 *		low memory condition)
 *
 * note:	deletion of timer is not required (e.g. in case of a time-out),
 *		but a second try does no harm,
 *		we leave it in here to allow for greater simplification
 *
 * returns:	0 - there was an action to handle
 *		!0 - otherwise
 */
int
zfcp_erp_async_handler(struct zfcp_erp_action *erp_action,
		       unsigned long set_mask)
{
	struct zfcp_adapter *adapter = erp_action->adapter;
	unsigned long flags;
	int retval;

	write_lock_irqsave(&adapter->erp_lock, flags);
	retval = zfcp_erp_async_handler_nolock(erp_action, set_mask);
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	return retval;
}

/*
 * purpose:	is called for erp_action which was slept waiting for
 *		memory becoming avaliable,
 *		will trigger that this action will be continued
 */
static void
zfcp_erp_memwait_handler(unsigned long data)
{
	struct zfcp_erp_action *erp_action = (struct zfcp_erp_action *) data;
	struct zfcp_adapter *adapter = erp_action->adapter;

	debug_text_event(adapter->erp_dbf, 2, "a_mwh");
	debug_event(adapter->erp_dbf, 2, &erp_action->action, sizeof (int));

	zfcp_erp_async_handler(erp_action, 0);
}

/*
 * purpose:	is called if an asynchronous erp step timed out,
 *		action gets an appropriate flag and will be processed
 *		accordingly
 */
static void
zfcp_erp_timeout_handler(unsigned long data)
{
	struct zfcp_erp_action *erp_action = (struct zfcp_erp_action *) data;
	struct zfcp_adapter *adapter = erp_action->adapter;

	debug_text_event(adapter->erp_dbf, 2, "a_th");
	debug_event(adapter->erp_dbf, 2, &erp_action->action, sizeof (int));

	zfcp_erp_async_handler(erp_action, ZFCP_STATUS_ERP_TIMEDOUT);
}

/*
 * purpose:	is called for an erp_action which needs to be ended
 *		though not being done,
 *		this is usually required if an higher is generated,
 *		action gets an appropriate flag and will be processed
 *		accordingly
 *
 * locks:	erp_lock held (thus we need to call another handler variant)
 */
static int
zfcp_erp_action_dismiss(struct zfcp_erp_action *erp_action)
{
	struct zfcp_adapter *adapter = erp_action->adapter;

	debug_text_event(adapter->erp_dbf, 2, "a_adis");
	debug_event(adapter->erp_dbf, 2, &erp_action->action, sizeof (int));

	zfcp_erp_async_handler_nolock(erp_action, ZFCP_STATUS_ERP_DISMISSED);

	return 0;
}

int
zfcp_erp_thread_setup(struct zfcp_adapter *adapter)
{
	int retval = 0;

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP, &adapter->status);

	retval = kernel_thread(zfcp_erp_thread, adapter, SIGCHLD);
	if (retval < 0) {
		ZFCP_LOG_NORMAL("error: creation of erp thread failed for "
				"adapter %s\n",
				zfcp_get_busid_by_adapter(adapter));
		debug_text_event(adapter->erp_dbf, 5, "a_thset_fail");
	} else {
		wait_event(adapter->erp_thread_wqh,
			   atomic_test_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP,
					    &adapter->status));
		debug_text_event(adapter->erp_dbf, 5, "a_thset_ok");
	}

	return (retval < 0);
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 *
 * context:	process (i.e. proc-fs or rmmod/insmod)
 *
 * note:	The caller of this routine ensures that the specified
 *		adapter has been shut down and that this operation
 *		has been completed. Thus, there are no pending erp_actions
 *		which would need to be handled here.
 */
int
zfcp_erp_thread_kill(struct zfcp_adapter *adapter)
{
	int retval = 0;

	atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_KILL, &adapter->status);
	up(&adapter->erp_ready_sem);

	wait_event(adapter->erp_thread_wqh,
		   !atomic_test_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP,
				     &adapter->status));

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_KILL,
			  &adapter->status);

	debug_text_event(adapter->erp_dbf, 5, "a_thki_ok");

	return retval;
}

/*
 * purpose:	is run as a kernel thread,
 *		goes through list of error recovery actions of associated adapter
 *		and delegates single action to execution
 *
 * returns:	0
 */
static int
zfcp_erp_thread(void *data)
{
	struct zfcp_adapter *adapter = (struct zfcp_adapter *) data;
	struct list_head *next;
	struct zfcp_erp_action *erp_action;
	unsigned long flags;

	daemonize("zfcperp%s", zfcp_get_busid_by_adapter(adapter));
	/* Block all signals */
	siginitsetinv(&current->blocked, 0);
	atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP, &adapter->status);
	debug_text_event(adapter->erp_dbf, 5, "a_th_run");
	wake_up(&adapter->erp_thread_wqh);

	while (!atomic_test_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_KILL,
				 &adapter->status)) {

		write_lock_irqsave(&adapter->erp_lock, flags);
		next = adapter->erp_ready_head.prev;
		write_unlock_irqrestore(&adapter->erp_lock, flags);

		if (next != &adapter->erp_ready_head) {
			erp_action =
			    list_entry(next, struct zfcp_erp_action, list);
			/*
			 * process action (incl. [re]moving it
			 * from 'ready' queue)
			 */
			zfcp_erp_strategy(erp_action);
		}

		/*
		 * sleep as long as there is nothing to do, i.e.
		 * no action in 'ready' queue to be processed and
		 * thread is not to be killed
		 */
		down_interruptible(&adapter->erp_ready_sem);
		debug_text_event(adapter->erp_dbf, 5, "a_th_woken");
	}

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP, &adapter->status);
	debug_text_event(adapter->erp_dbf, 5, "a_th_stop");
	wake_up(&adapter->erp_thread_wqh);

	return 0;
}

/*
 * function:	
 *
 * purpose:	drives single error recovery action and schedules higher and
 *		subordinate actions, if necessary
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_SUCCEEDED	- action finished successfully (deqd)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully (deqd)
 *		ZFCP_ERP_EXIT		- action finished (dequeued), offline
 *		ZFCP_ERP_DISMISSED	- action canceled (dequeued)
 */
static int
zfcp_erp_strategy(struct zfcp_erp_action *erp_action)
{
	int retval = 0;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_port *port = erp_action->port;
	struct zfcp_unit *unit = erp_action->unit;
	int action = erp_action->action;
	u32 status = erp_action->status;
	unsigned long flags;

	/* serialise dismissing, timing out, moving, enqueueing */
	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);

	/* dequeue dismissed action and leave, if required */
	retval = zfcp_erp_strategy_check_action(erp_action, retval);
	if (retval == ZFCP_ERP_DISMISSED) {
		debug_text_event(adapter->erp_dbf, 4, "a_st_dis1");
		goto unlock;
	}

	/*
	 * move action to 'running' queue before processing it
	 * (to avoid a race condition regarding moving the
	 * action to the 'running' queue and back)
	 */
	zfcp_erp_action_to_running(erp_action);

	/*
	 * try to process action as far as possible,
	 * no lock to allow for blocking operations (kmalloc, qdio, ...),
	 * afterwards the lock is required again for the following reasons:
	 * - dequeueing of finished action and enqueueing of
	 *   follow-up actions must be atomic so that any other
	 *   reopen-routine does not believe there is nothing to do
	 *   and that it is safe to enqueue something else,
	 * - we want to force any control thread which is dismissing
	 *   actions to finish this before we decide about
	 *   necessary steps to be taken here further
	 */
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
	retval = zfcp_erp_strategy_do_action(erp_action);
	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);

	/*
	 * check for dismissed status again to avoid follow-up actions,
	 * failing of targets and so on for dismissed actions
	 */
	retval = zfcp_erp_strategy_check_action(erp_action, retval);

	switch (retval) {
	case ZFCP_ERP_DISMISSED:
		/* leave since this action has ridden to its ancestors */
		debug_text_event(adapter->erp_dbf, 6, "a_st_dis2");
		goto unlock;
	case ZFCP_ERP_NOMEM:
		/* no memory to continue immediately, let it sleep */
		if (!(erp_action->status & ZFCP_STATUS_ERP_LOWMEM)) {
			++adapter->erp_low_mem_count;
			erp_action->status |= ZFCP_STATUS_ERP_LOWMEM;
		}
		/* This condition is true if there is no memory available
		   for any erp_action on this adapter. This implies that there
		   are no elements in the memory pool(s) left for erp_actions.
		   This might happen if an erp_action that used a memory pool
		   element was timed out.
		 */
		if (adapter->erp_total_count == adapter->erp_low_mem_count) {
			debug_text_event(adapter->erp_dbf, 3, "a_st_lowmem");
			ZFCP_LOG_NORMAL("error: no mempool elements available, "
					"restarting I/O on adapter %s "
					"to free mempool\n",
					zfcp_get_busid_by_adapter(adapter));
			zfcp_erp_adapter_reopen_internal(adapter, 0);
		} else {
		debug_text_event(adapter->erp_dbf, 2, "a_st_memw");
		retval = zfcp_erp_strategy_memwait(erp_action);
		}
		goto unlock;
	case ZFCP_ERP_CONTINUES:
		/* leave since this action runs asynchronously */
		debug_text_event(adapter->erp_dbf, 6, "a_st_cont");
		if (erp_action->status & ZFCP_STATUS_ERP_LOWMEM) {
			--adapter->erp_low_mem_count;
			erp_action->status &= ~ZFCP_STATUS_ERP_LOWMEM;
		}
		goto unlock;
	}
	/* ok, finished action (whatever its result is) */

	/* check for unrecoverable targets */
	retval = zfcp_erp_strategy_check_target(erp_action, retval);

	/* action must be dequeued (here to allow for further ones) */
	zfcp_erp_action_dequeue(erp_action);

	/*
	 * put this target through the erp mill again if someone has
	 * requested to change the status of a target being online 
	 * to offline or the other way around
	 * (old retval is preserved if nothing has to be done here)
	 */
	retval = zfcp_erp_strategy_statechange(action, status, adapter,
					       port, unit, retval);

	/*
	 * leave if target is in permanent error state or if
	 * action is repeated in order to process state change
	 */
	if (retval == ZFCP_ERP_EXIT) {
		debug_text_event(adapter->erp_dbf, 2, "a_st_exit");
		goto unlock;
	}

	/* trigger follow up actions */
	zfcp_erp_strategy_followup_actions(action, adapter, port, unit, retval);

 unlock:
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
	
	if (retval != ZFCP_ERP_CONTINUES)
		zfcp_erp_action_cleanup(action, adapter, port, unit, retval);

	/*
	 * a few tasks remain when the erp queues are empty
	 * (don't do that if the last action evaluated was dismissed
	 * since this clearly indicates that there is more to come) :
	 * - close the name server port if it is open yet
	 *   (enqueues another [probably] final action)
	 * - otherwise, wake up whoever wants to be woken when we are
	 *   done with erp
	 */
	if (retval != ZFCP_ERP_DISMISSED)
		zfcp_erp_strategy_check_queues(adapter);

	debug_text_event(adapter->erp_dbf, 6, "a_st_done");

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_DISMISSED	- if action has been dismissed
 *		retval			- otherwise
 */
static int
zfcp_erp_strategy_check_action(struct zfcp_erp_action *erp_action, int retval)
{
	struct zfcp_adapter *adapter = erp_action->adapter;

	zfcp_erp_strategy_check_fsfreq(erp_action);

	debug_event(adapter->erp_dbf, 5, &erp_action->action, sizeof (int));
	if (erp_action->status & ZFCP_STATUS_ERP_DISMISSED) {
		debug_text_event(adapter->erp_dbf, 3, "a_stcd_dis");
		zfcp_erp_action_dequeue(erp_action);
		retval = ZFCP_ERP_DISMISSED;
	} else
		debug_text_event(adapter->erp_dbf, 5, "a_stcd_nodis");

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int
zfcp_erp_strategy_do_action(struct zfcp_erp_action *erp_action)
{
	int retval = ZFCP_ERP_FAILED;
	struct zfcp_adapter *adapter = erp_action->adapter;

	/*
	 * try to execute/continue action as far as possible,
	 * note: no lock in subsequent strategy routines
	 * (this allows these routine to call schedule, e.g.
	 * kmalloc with such flags or qdio_initialize & friends)
	 * Note: in case of timeout, the seperate strategies will fail
	 * anyhow. No need for a special action. Even worse, a nameserver
	 * failure would not wake up waiting ports without the call.
	 */
	switch (erp_action->action) {

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		retval = zfcp_erp_adapter_strategy(erp_action);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		retval = zfcp_erp_port_forced_strategy(erp_action);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT:
		retval = zfcp_erp_port_strategy(erp_action);
		break;

	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		retval = zfcp_erp_unit_strategy(erp_action);
		break;

	default:
		debug_text_exception(adapter->erp_dbf, 1, "a_stda_bug");
		debug_event(adapter->erp_dbf, 1, &erp_action->action,
			    sizeof (int));
		ZFCP_LOG_NORMAL("bug: unknown erp action requested on "
				"adapter %s (action=%d)\n",
				zfcp_get_busid_by_adapter(erp_action->adapter),
				erp_action->action);
	}

	return retval;
}

/*
 * function:	
 *
 * purpose:	triggers retry of this action after a certain amount of time
 *		by means of timer provided by erp_action
 *
 * returns:	ZFCP_ERP_CONTINUES - erp_action sleeps in erp running queue
 */
static int
zfcp_erp_strategy_memwait(struct zfcp_erp_action *erp_action)
{
	int retval = ZFCP_ERP_CONTINUES;
	struct zfcp_adapter *adapter = erp_action->adapter;

	debug_text_event(adapter->erp_dbf, 6, "a_mwinit");
	debug_event(adapter->erp_dbf, 6, &erp_action->action, sizeof (int));
	init_timer(&erp_action->timer);
	erp_action->timer.function = zfcp_erp_memwait_handler;
	erp_action->timer.data = (unsigned long) erp_action;
	erp_action->timer.expires = jiffies + ZFCP_ERP_MEMWAIT_TIMEOUT;
	add_timer(&erp_action->timer);

	return retval;
}

/* 
 * function:    zfcp_erp_adapter_failed
 *
 * purpose:     sets the adapter and all underlying devices to ERP_FAILED
 *
 */
void
zfcp_erp_adapter_failed(struct zfcp_adapter *adapter)
{
	zfcp_erp_modify_adapter_status(adapter,
				       ZFCP_STATUS_COMMON_ERP_FAILED, ZFCP_SET);
	ZFCP_LOG_NORMAL("adapter erp failed on adapter %s\n",
			zfcp_get_busid_by_adapter(adapter));
	debug_text_event(adapter->erp_dbf, 2, "a_afail");
}

/* 
 * function:    zfcp_erp_port_failed
 *
 * purpose:     sets the port and all underlying devices to ERP_FAILED
 *
 */
void
zfcp_erp_port_failed(struct zfcp_port *port)
{
	zfcp_erp_modify_port_status(port,
				    ZFCP_STATUS_COMMON_ERP_FAILED, ZFCP_SET);

	if (atomic_test_mask(ZFCP_STATUS_PORT_WKA, &port->status))
		ZFCP_LOG_NORMAL("port erp failed (adapter %s, "
				"port d_id=0x%08x)\n",
				zfcp_get_busid_by_port(port), port->d_id);
	else
		ZFCP_LOG_NORMAL("port erp failed (adapter %s, wwpn=0x%016Lx)\n",
				zfcp_get_busid_by_port(port), port->wwpn);

	debug_text_event(port->adapter->erp_dbf, 2, "p_pfail");
	debug_event(port->adapter->erp_dbf, 2, &port->wwpn, sizeof (wwn_t));
}

/* 
 * function:    zfcp_erp_unit_failed
 *
 * purpose:     sets the unit to ERP_FAILED
 *
 */
void
zfcp_erp_unit_failed(struct zfcp_unit *unit)
{
	zfcp_erp_modify_unit_status(unit,
				    ZFCP_STATUS_COMMON_ERP_FAILED, ZFCP_SET);

	ZFCP_LOG_NORMAL("unit erp failed on unit 0x%016Lx on port 0x%016Lx "
			" on adapter %s\n", unit->fcp_lun,
			unit->port->wwpn, zfcp_get_busid_by_unit(unit));
	debug_text_event(unit->port->adapter->erp_dbf, 2, "u_ufail");
	debug_event(unit->port->adapter->erp_dbf, 2,
		    &unit->fcp_lun, sizeof (fcp_lun_t));
}

/*
 * function:	zfcp_erp_strategy_check_target
 *
 * purpose:	increments the erp action count on the device currently in
 *              recovery if the action failed or resets the count in case of
 *              success. If a maximum count is exceeded the device is marked
 *              as ERP_FAILED.
 *		The 'blocked' state of a target which has been recovered
 *              successfully is reset.
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (not considered)
 *		ZFCP_ERP_SUCCEEDED	- action finished successfully 
 *		ZFCP_ERP_EXIT		- action failed and will not continue
 */
static int
zfcp_erp_strategy_check_target(struct zfcp_erp_action *erp_action, int result)
{
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_port *port = erp_action->port;
	struct zfcp_unit *unit = erp_action->unit;

	debug_text_event(adapter->erp_dbf, 5, "a_stct_norm");
	debug_event(adapter->erp_dbf, 5, &erp_action->action, sizeof (int));
	debug_event(adapter->erp_dbf, 5, &result, sizeof (int));

	switch (erp_action->action) {

	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		result = zfcp_erp_strategy_check_unit(unit, result);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		result = zfcp_erp_strategy_check_port(port, result);
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		result = zfcp_erp_strategy_check_adapter(adapter, result);
		break;
	}

	return result;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int
zfcp_erp_strategy_statechange(int action,
			      u32 status,
			      struct zfcp_adapter *adapter,
			      struct zfcp_port *port,
			      struct zfcp_unit *unit, int retval)
{
	debug_text_event(adapter->erp_dbf, 3, "a_stsc");
	debug_event(adapter->erp_dbf, 3, &action, sizeof (int));

	switch (action) {

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (zfcp_erp_strategy_statechange_detected(&adapter->status,
							   status)) {
			zfcp_erp_adapter_reopen_internal(adapter, ZFCP_STATUS_COMMON_ERP_FAILED);
			retval = ZFCP_ERP_EXIT;
		}
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if (zfcp_erp_strategy_statechange_detected(&port->status,
							   status)) {
			zfcp_erp_port_reopen_internal(port, ZFCP_STATUS_COMMON_ERP_FAILED);
			retval = ZFCP_ERP_EXIT;
		}
		break;

	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		if (zfcp_erp_strategy_statechange_detected(&unit->status,
							   status)) {
			zfcp_erp_unit_reopen_internal(unit, ZFCP_STATUS_COMMON_ERP_FAILED);
			retval = ZFCP_ERP_EXIT;
		}
		break;
	}

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static inline int
zfcp_erp_strategy_statechange_detected(atomic_t * target_status, u32 erp_status)
{
	return
	    /* take it online */
	    (atomic_test_mask(ZFCP_STATUS_COMMON_RUNNING, target_status) &&
	     (ZFCP_STATUS_ERP_CLOSE_ONLY & erp_status)) ||
	    /* take it offline */
	    (!atomic_test_mask(ZFCP_STATUS_COMMON_RUNNING, target_status) &&
	     !(ZFCP_STATUS_ERP_CLOSE_ONLY & erp_status));
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int
zfcp_erp_strategy_check_unit(struct zfcp_unit *unit, int result)
{
	debug_text_event(unit->port->adapter->erp_dbf, 5, "u_stct");
	debug_event(unit->port->adapter->erp_dbf, 5, &unit->fcp_lun,
		    sizeof (fcp_lun_t));

	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&unit->erp_counter, 0);
		zfcp_erp_unit_unblock(unit);
		break;
	case ZFCP_ERP_FAILED :
		atomic_inc(&unit->erp_counter);
		if (atomic_read(&unit->erp_counter) > ZFCP_MAX_ERPS)
			zfcp_erp_unit_failed(unit);
		break;
	case ZFCP_ERP_EXIT :
		/* nothing */
		break;
	}

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &unit->status)) {
		zfcp_erp_unit_block(unit, 0); /* for ZFCP_ERP_SUCCEEDED */
		result = ZFCP_ERP_EXIT;
	}

	return result;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int
zfcp_erp_strategy_check_port(struct zfcp_port *port, int result)
{
	debug_text_event(port->adapter->erp_dbf, 5, "p_stct");
	debug_event(port->adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));

	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&port->erp_counter, 0);
		zfcp_erp_port_unblock(port);
		break;
	case ZFCP_ERP_FAILED :
		atomic_inc(&port->erp_counter);
		if (atomic_read(&port->erp_counter) > ZFCP_MAX_ERPS)
			zfcp_erp_port_failed(port);
		break;
	case ZFCP_ERP_EXIT :
		/* nothing */
		break;
	}

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &port->status)) {
		zfcp_erp_port_block(port, 0); /* for ZFCP_ERP_SUCCEEDED */
		result = ZFCP_ERP_EXIT;
	}

	return result;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int
zfcp_erp_strategy_check_adapter(struct zfcp_adapter *adapter, int result)
{
	debug_text_event(adapter->erp_dbf, 5, "a_stct");

	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&adapter->erp_counter, 0);
		zfcp_erp_adapter_unblock(adapter);
		break;
	case ZFCP_ERP_FAILED :
		atomic_inc(&adapter->erp_counter);
		if (atomic_read(&adapter->erp_counter) > ZFCP_MAX_ERPS)
			zfcp_erp_adapter_failed(adapter);
		break;
	case ZFCP_ERP_EXIT :
		/* nothing */
		break;
	}

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &adapter->status)) {
		zfcp_erp_adapter_block(adapter, 0); /* for ZFCP_ERP_SUCCEEDED */
		result = ZFCP_ERP_EXIT;
	}

	return result;
}

/*
 * function:	
 *
 * purpose:	remaining things in good cases,
 *		escalation in bad cases
 *
 * returns:
 */
static int
zfcp_erp_strategy_followup_actions(int action,
				   struct zfcp_adapter *adapter,
				   struct zfcp_port *port,
				   struct zfcp_unit *unit, int status)
{
	debug_text_event(adapter->erp_dbf, 5, "a_stfol");
	debug_event(adapter->erp_dbf, 5, &action, sizeof (int));

	/* initiate follow-up actions depending on success of finished action */
	switch (action) {

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (status == ZFCP_ERP_SUCCEEDED)
			zfcp_erp_port_reopen_all_internal(adapter, 0);
		else
			zfcp_erp_adapter_reopen_internal(adapter, 0);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		if (status == ZFCP_ERP_SUCCEEDED)
			zfcp_erp_port_reopen_internal(port, 0);
		else
			zfcp_erp_adapter_reopen_internal(adapter, 0);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if (status == ZFCP_ERP_SUCCEEDED)
			zfcp_erp_unit_reopen_all_internal(port, 0);
		else
			zfcp_erp_port_forced_reopen_internal(port, 0);
		break;

	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		if (status == ZFCP_ERP_SUCCEEDED) ;	/* no further action */
		else
			zfcp_erp_port_reopen_internal(unit->port, 0);
		break;
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
static int
zfcp_erp_strategy_check_queues(struct zfcp_adapter *adapter)
{
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	read_lock(&adapter->erp_lock);
	if (list_empty(&adapter->erp_ready_head) &&
	    list_empty(&adapter->erp_running_head)) {
			debug_text_event(adapter->erp_dbf, 4, "a_cq_wake");
			atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_PENDING,
					  &adapter->status);
			wake_up(&adapter->erp_done_wqh);
	} else
		debug_text_event(adapter->erp_dbf, 5, "a_cq_notempty");
	read_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	return 0;
}

/**
 * zfcp_erp_wait - wait for completion of error recovery on an adapter
 * @adapter: adapter for which to wait for completion of its error recovery
 * Return: 0
 */
int
zfcp_erp_wait(struct zfcp_adapter *adapter)
{
	int retval = 0;

	wait_event(adapter->erp_done_wqh,
		   !atomic_test_mask(ZFCP_STATUS_ADAPTER_ERP_PENDING,
				     &adapter->status));

	return retval;
}

/*
 * function:	zfcp_erp_modify_adapter_status
 *
 * purpose:	
 *
 */
void
zfcp_erp_modify_adapter_status(struct zfcp_adapter *adapter,
			       u32 mask, int set_or_clear)
{
	struct zfcp_port *port;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;

	if (set_or_clear == ZFCP_SET) {
		atomic_set_mask(mask, &adapter->status);
		debug_text_event(adapter->erp_dbf, 3, "a_mod_as_s");
	} else {
		atomic_clear_mask(mask, &adapter->status);
		if (mask & ZFCP_STATUS_COMMON_ERP_FAILED)
			atomic_set(&adapter->erp_counter, 0);
		debug_text_event(adapter->erp_dbf, 3, "a_mod_as_c");
	}
	debug_event(adapter->erp_dbf, 3, &mask, sizeof (u32));

	/* Deal with all underlying devices, only pass common_mask */
	if (common_mask)
		list_for_each_entry(port, &adapter->port_list_head, list)
		    zfcp_erp_modify_port_status(port, common_mask,
						set_or_clear);
}

/*
 * function:	zfcp_erp_modify_port_status
 *
 * purpose:	sets the port and all underlying devices to ERP_FAILED
 *
 */
void
zfcp_erp_modify_port_status(struct zfcp_port *port, u32 mask, int set_or_clear)
{
	struct zfcp_unit *unit;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;

	if (set_or_clear == ZFCP_SET) {
		atomic_set_mask(mask, &port->status);
		debug_text_event(port->adapter->erp_dbf, 3, "p_mod_ps_s");
	} else {
		atomic_clear_mask(mask, &port->status);
		if (mask & ZFCP_STATUS_COMMON_ERP_FAILED)
			atomic_set(&port->erp_counter, 0);
		debug_text_event(port->adapter->erp_dbf, 3, "p_mod_ps_c");
	}
	debug_event(port->adapter->erp_dbf, 3, &port->wwpn, sizeof (wwn_t));
	debug_event(port->adapter->erp_dbf, 3, &mask, sizeof (u32));

	/* Modify status of all underlying devices, only pass common mask */
	if (common_mask)
		list_for_each_entry(unit, &port->unit_list_head, list)
		    zfcp_erp_modify_unit_status(unit, common_mask,
						set_or_clear);
}

/*
 * function:	zfcp_erp_modify_unit_status
 *
 * purpose:	sets the unit to ERP_FAILED
 *
 */
void
zfcp_erp_modify_unit_status(struct zfcp_unit *unit, u32 mask, int set_or_clear)
{
	if (set_or_clear == ZFCP_SET) {
		atomic_set_mask(mask, &unit->status);
		debug_text_event(unit->port->adapter->erp_dbf, 3, "u_mod_us_s");
	} else {
		atomic_clear_mask(mask, &unit->status);
		if (mask & ZFCP_STATUS_COMMON_ERP_FAILED) {
			atomic_set(&unit->erp_counter, 0);
		}
		debug_text_event(unit->port->adapter->erp_dbf, 3, "u_mod_us_c");
	}
	debug_event(unit->port->adapter->erp_dbf, 3, &unit->fcp_lun,
		    sizeof (fcp_lun_t));
	debug_event(unit->port->adapter->erp_dbf, 3, &mask, sizeof (u32));
}

/*
 * function:	
 *
 * purpose:	Wrappper for zfcp_erp_port_reopen_all_internal
 *              used to ensure the correct locking
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
int
zfcp_erp_port_reopen_all(struct zfcp_adapter *adapter, int clear_mask)
{
	int retval;
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	write_lock(&adapter->erp_lock);
	retval = zfcp_erp_port_reopen_all_internal(adapter, clear_mask);
	write_unlock(&adapter->erp_lock);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	FIXME
 */
static int
zfcp_erp_port_reopen_all_internal(struct zfcp_adapter *adapter, int clear_mask)
{
	int retval = 0;
	struct zfcp_port *port;

	list_for_each_entry(port, &adapter->port_list_head, list)
		if (!atomic_test_mask(ZFCP_STATUS_PORT_WKA, &port->status))
			zfcp_erp_port_reopen_internal(port, clear_mask);

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	FIXME
 */
static int
zfcp_erp_unit_reopen_all_internal(struct zfcp_port *port, int clear_mask)
{
	int retval = 0;
	struct zfcp_unit *unit;

	list_for_each_entry(unit, &port->unit_list_head, list)
	    zfcp_erp_unit_reopen_internal(unit, clear_mask);

	return retval;
}

/*
 * function:	
 *
 * purpose:	this routine executes the 'Reopen Adapter' action
 *		(the entire action is processed synchronously, since
 *		there are no actions which might be run concurrently
 *		per definition)
 *
 * returns:	ZFCP_ERP_SUCCEEDED	- action finished successfully
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int
zfcp_erp_adapter_strategy(struct zfcp_erp_action *erp_action)
{
	int retval;
	struct zfcp_adapter *adapter = erp_action->adapter;

	retval = zfcp_erp_adapter_strategy_close(erp_action);
	if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
		retval = ZFCP_ERP_EXIT;
	else
		retval = zfcp_erp_adapter_strategy_open(erp_action);

	debug_text_event(adapter->erp_dbf, 3, "a_ast/ret");
	debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof (int));
	debug_event(adapter->erp_dbf, 3, &retval, sizeof (int));

	if (retval == ZFCP_ERP_FAILED) {
		ZFCP_LOG_INFO("Waiting to allow the adapter %s "
			      "to recover itself\n",
			      zfcp_get_busid_by_adapter(adapter));
		msleep(jiffies_to_msecs(ZFCP_TYPE2_RECOVERY_TIME));
	}

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_SUCCEEDED      - action finished successfully
 *              ZFCP_ERP_FAILED         - action finished unsuccessfully
 */
static int
zfcp_erp_adapter_strategy_close(struct zfcp_erp_action *erp_action)
{
	int retval;

	atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING,
			&erp_action->adapter->status);
	retval = zfcp_erp_adapter_strategy_generic(erp_action, 1);
	atomic_clear_mask(ZFCP_STATUS_COMMON_CLOSING,
			  &erp_action->adapter->status);

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_SUCCEEDED      - action finished successfully
 *              ZFCP_ERP_FAILED         - action finished unsuccessfully
 */
static int
zfcp_erp_adapter_strategy_open(struct zfcp_erp_action *erp_action)
{
	int retval;

	atomic_set_mask(ZFCP_STATUS_COMMON_OPENING,
			&erp_action->adapter->status);
	retval = zfcp_erp_adapter_strategy_generic(erp_action, 0);
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING,
			  &erp_action->adapter->status);

	return retval;
}

/*
 * function:    zfcp_register_adapter
 *
 * purpose:	allocate the irq associated with this devno and register
 *		the FSF adapter with the SCSI stack
 *
 * returns:	
 */
static int
zfcp_erp_adapter_strategy_generic(struct zfcp_erp_action *erp_action, int close)
{
	int retval = ZFCP_ERP_SUCCEEDED;

	if (close)
		goto close_only;

	retval = zfcp_erp_adapter_strategy_open_qdio(erp_action);
	if (retval != ZFCP_ERP_SUCCEEDED)
		goto failed_qdio;

	retval = zfcp_erp_adapter_strategy_open_fsf(erp_action);
	if (retval != ZFCP_ERP_SUCCEEDED)
		goto failed_openfcp;

	atomic_set_mask(ZFCP_STATUS_COMMON_OPEN, &erp_action->adapter->status);
	goto out;

 close_only:
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN,
			  &erp_action->adapter->status);

 failed_openfcp:
	zfcp_erp_adapter_strategy_close_qdio(erp_action);
	zfcp_erp_adapter_strategy_close_fsf(erp_action);
 failed_qdio:
 out:
	return retval;
}

/*
 * function:    zfcp_qdio_init
 *
 * purpose:	setup QDIO operation for specified adapter
 *
 * returns:	0 - successful setup
 *		!0 - failed setup
 */
int
zfcp_erp_adapter_strategy_open_qdio(struct zfcp_erp_action *erp_action)
{
	int retval;
	int i;
	volatile struct qdio_buffer_element *sbale;
	struct zfcp_adapter *adapter = erp_action->adapter;

	if (atomic_test_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status)) {
		ZFCP_LOG_NORMAL("bug: second attempt to set up QDIO on "
				"adapter %s\n",
				zfcp_get_busid_by_adapter(adapter));
		goto failed_sanity;
	}

	if (qdio_establish(&adapter->qdio_init_data) != 0) {
		ZFCP_LOG_INFO("error: establishment of QDIO queues failed "
			      "on adapter %s\n",
			      zfcp_get_busid_by_adapter(adapter));
		goto failed_qdio_establish;
	}
	debug_text_event(adapter->erp_dbf, 3, "qdio_est");

	if (qdio_activate(adapter->ccw_device, 0) != 0) {
		ZFCP_LOG_INFO("error: activation of QDIO queues failed "
			      "on adapter %s\n",
			      zfcp_get_busid_by_adapter(adapter));
		goto failed_qdio_activate;
	}
	debug_text_event(adapter->erp_dbf, 3, "qdio_act");

	/*
	 * put buffers into response queue,
	 */
	for (i = 0; i < QDIO_MAX_BUFFERS_PER_Q; i++) {
		sbale = &(adapter->response_queue.buffer[i]->element[0]);
		sbale->length = 0;
		sbale->flags = SBAL_FLAGS_LAST_ENTRY;
		sbale->addr = 0;
	}

	ZFCP_LOG_TRACE("calling do_QDIO on adapter %s (flags=0x%x, "
		       "queue_no=%i, index_in_queue=%i, count=%i)\n",
		       zfcp_get_busid_by_adapter(adapter),
		       QDIO_FLAG_SYNC_INPUT, 0, 0, QDIO_MAX_BUFFERS_PER_Q);

	retval = do_QDIO(adapter->ccw_device,
			 QDIO_FLAG_SYNC_INPUT,
			 0, 0, QDIO_MAX_BUFFERS_PER_Q, NULL);

	if (retval) {
		ZFCP_LOG_NORMAL("bug: setup of QDIO failed (retval=%d)\n",
				retval);
		goto failed_do_qdio;
	} else {
		adapter->response_queue.free_index = 0;
		atomic_set(&adapter->response_queue.free_count, 0);
		ZFCP_LOG_DEBUG("%i buffers successfully enqueued to "
			       "response queue\n", QDIO_MAX_BUFFERS_PER_Q);
	}
	/* set index of first avalable SBALS / number of available SBALS */
	adapter->request_queue.free_index = 0;
	atomic_set(&adapter->request_queue.free_count, QDIO_MAX_BUFFERS_PER_Q);
	adapter->request_queue.distance_from_int = 0;

	/* initialize waitqueue used to wait for free SBALs in requests queue */
	init_waitqueue_head(&adapter->request_wq);

	/* ok, we did it - skip all cleanups for different failures */
	atomic_set_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status);
	retval = ZFCP_ERP_SUCCEEDED;
	goto out;

 failed_do_qdio:
	/* NOP */

 failed_qdio_activate:
	debug_text_event(adapter->erp_dbf, 3, "qdio_down1a");
	while (qdio_shutdown(adapter->ccw_device,
			     QDIO_FLAG_CLEANUP_USING_CLEAR) == -EINPROGRESS)
		msleep(1000);
	debug_text_event(adapter->erp_dbf, 3, "qdio_down1b");

 failed_qdio_establish:
 failed_sanity:
	retval = ZFCP_ERP_FAILED;

 out:
	return retval;
}

/*
 * function:    zfcp_qdio_cleanup
 *
 * purpose:	cleans up QDIO operation for the specified adapter
 *
 * returns:	0 - successful cleanup
 *		!0 - failed cleanup
 */
int
zfcp_erp_adapter_strategy_close_qdio(struct zfcp_erp_action *erp_action)
{
	int retval = ZFCP_ERP_SUCCEEDED;
	int first_used;
	int used_count;
	struct zfcp_adapter *adapter = erp_action->adapter;

	if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status)) {
		ZFCP_LOG_DEBUG("error: attempt to shut down inactive QDIO "
			       "queues on adapter %s\n",
			       zfcp_get_busid_by_adapter(adapter));
		retval = ZFCP_ERP_FAILED;
		goto out;
	}

	/*
	 * Get queue_lock and clear QDIOUP flag. Thus it's guaranteed that
	 * do_QDIO won't be called while qdio_shutdown is in progress.
	 */

	write_lock_irq(&adapter->request_queue.queue_lock);
	atomic_clear_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status);
	write_unlock_irq(&adapter->request_queue.queue_lock);

	debug_text_event(adapter->erp_dbf, 3, "qdio_down2a");
	while (qdio_shutdown(adapter->ccw_device,
			     QDIO_FLAG_CLEANUP_USING_CLEAR) == -EINPROGRESS)
		msleep(1000);
	debug_text_event(adapter->erp_dbf, 3, "qdio_down2b");

	/*
	 * First we had to stop QDIO operation.
	 * Now it is safe to take the following actions.
	 */

	/* Cleanup only necessary when there are unacknowledged buffers */
	if (atomic_read(&adapter->request_queue.free_count)
	    < QDIO_MAX_BUFFERS_PER_Q) {
		first_used = (adapter->request_queue.free_index +
			      atomic_read(&adapter->request_queue.free_count))
			% QDIO_MAX_BUFFERS_PER_Q;
		used_count = QDIO_MAX_BUFFERS_PER_Q -
			atomic_read(&adapter->request_queue.free_count);
		zfcp_qdio_zero_sbals(adapter->request_queue.buffer,
				     first_used, used_count);
	}
	adapter->response_queue.free_index = 0;
	atomic_set(&adapter->response_queue.free_count, 0);
	adapter->request_queue.free_index = 0;
	atomic_set(&adapter->request_queue.free_count, 0);
	adapter->request_queue.distance_from_int = 0;
 out:
	return retval;
}

static int
zfcp_erp_adapter_strategy_open_fsf(struct zfcp_erp_action *erp_action)
{
	int retval;

	if ((atomic_test_mask(ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED,
			      &erp_action->adapter->status)) &&
	    (erp_action->adapter->adapter_features &
	     FSF_FEATURE_HBAAPI_MANAGEMENT)) {
		zfcp_erp_adapter_strategy_open_fsf_xport(erp_action);
		atomic_set(&erp_action->adapter->erp_counter, 0);
		return ZFCP_ERP_FAILED;
	}

	retval = zfcp_erp_adapter_strategy_open_fsf_xconfig(erp_action);
	if (retval == ZFCP_ERP_FAILED)
		return ZFCP_ERP_FAILED;

	retval = zfcp_erp_adapter_strategy_open_fsf_xport(erp_action);
	if (retval == ZFCP_ERP_FAILED)
		return ZFCP_ERP_FAILED;

	return zfcp_erp_adapter_strategy_open_fsf_statusread(erp_action);
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int
zfcp_erp_adapter_strategy_open_fsf_xconfig(struct zfcp_erp_action *erp_action)
{
	int retval = ZFCP_ERP_SUCCEEDED;
	int retries;
	int sleep = ZFCP_EXCHANGE_CONFIG_DATA_FIRST_SLEEP;
	struct zfcp_adapter *adapter = erp_action->adapter;

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK, &adapter->status);

	for (retries = ZFCP_EXCHANGE_CONFIG_DATA_RETRIES; retries; retries--) {
		atomic_clear_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
				  &adapter->status);
		ZFCP_LOG_DEBUG("Doing exchange config data\n");
		write_lock(&adapter->erp_lock);
		zfcp_erp_action_to_running(erp_action);
		write_unlock(&adapter->erp_lock);
		zfcp_erp_timeout_init(erp_action);
		if (zfcp_fsf_exchange_config_data(erp_action)) {
			retval = ZFCP_ERP_FAILED;
			debug_text_event(adapter->erp_dbf, 5, "a_fstx_xf");
			ZFCP_LOG_INFO("error:  initiation of exchange of "
				      "configuration data failed for "
				      "adapter %s\n",
				      zfcp_get_busid_by_adapter(adapter));
			break;
		}
		debug_text_event(adapter->erp_dbf, 6, "a_fstx_xok");
		ZFCP_LOG_DEBUG("Xchange underway\n");

		/*
		 * Why this works:
		 * Both the normal completion handler as well as the timeout
		 * handler will do an 'up' when the 'exchange config data'
		 * request completes or times out. Thus, the signal to go on
		 * won't be lost utilizing this semaphore.
		 * Furthermore, this 'adapter_reopen' action is
		 * guaranteed to be the only action being there (highest action
		 * which prevents other actions from being created).
		 * Resulting from that, the wake signal recognized here
		 * _must_ be the one belonging to the 'exchange config
		 * data' request.
		 */
		down(&adapter->erp_ready_sem);
		if (erp_action->status & ZFCP_STATUS_ERP_TIMEDOUT) {
			ZFCP_LOG_INFO("error: exchange of configuration data "
				      "for adapter %s timed out\n",
				      zfcp_get_busid_by_adapter(adapter));
			break;
		}

		if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
				     &adapter->status))
			break;

		ZFCP_LOG_DEBUG("host connection still initialising... "
			       "waiting and retrying...\n");
		/* sleep a little bit before retry */
		msleep(jiffies_to_msecs(sleep));
		sleep *= 2;
	}

	if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK,
			      &adapter->status)) {
		ZFCP_LOG_INFO("error: exchange of configuration data for "
			      "adapter %s failed\n",
			      zfcp_get_busid_by_adapter(adapter));
		retval = ZFCP_ERP_FAILED;
	}

	return retval;
}

static int
zfcp_erp_adapter_strategy_open_fsf_xport(struct zfcp_erp_action *erp_action)
{
	int ret;
	int retries;
	int sleep;
	struct zfcp_adapter *adapter = erp_action->adapter;

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_XPORT_OK, &adapter->status);

	retries = 0;
	do {
		write_lock(&adapter->erp_lock);
		zfcp_erp_action_to_running(erp_action);
		write_unlock(&adapter->erp_lock);
		zfcp_erp_timeout_init(erp_action);
		ret = zfcp_fsf_exchange_port_data(erp_action, adapter, NULL);
		if (ret == -EOPNOTSUPP) {
			debug_text_event(adapter->erp_dbf, 3, "a_xport_notsupp");
			return ZFCP_ERP_SUCCEEDED;
		} else if (ret) {
			debug_text_event(adapter->erp_dbf, 3, "a_xport_failed");
			return ZFCP_ERP_FAILED;
		}
		debug_text_event(adapter->erp_dbf, 6, "a_xport_ok");

		down(&adapter->erp_ready_sem);
		if (erp_action->status & ZFCP_STATUS_ERP_TIMEDOUT) {
			ZFCP_LOG_INFO("error: exchange of port data "
				      "for adapter %s timed out\n",
				      zfcp_get_busid_by_adapter(adapter));
			break;
		}
		if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED,
				      &adapter->status))
			break;

		if (retries < ZFCP_EXCHANGE_PORT_DATA_SHORT_RETRIES) {
			sleep = ZFCP_EXCHANGE_PORT_DATA_SHORT_SLEEP;
			retries++;
		} else
			sleep = ZFCP_EXCHANGE_PORT_DATA_LONG_SLEEP;
		schedule_timeout(sleep);
	} while (1);

	return ZFCP_ERP_SUCCEEDED;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int
zfcp_erp_adapter_strategy_open_fsf_statusread(struct zfcp_erp_action
					      *erp_action)
{
	int retval = ZFCP_ERP_SUCCEEDED;
	int temp_ret;
	struct zfcp_adapter *adapter = erp_action->adapter;
	int i;

	adapter->status_read_failed = 0;
	for (i = 0; i < ZFCP_STATUS_READS_RECOM; i++) {
		temp_ret = zfcp_fsf_status_read(adapter, ZFCP_WAIT_FOR_SBAL);
		if (temp_ret < 0) {
			ZFCP_LOG_INFO("error: set-up of unsolicited status "
				      "notification failed on adapter %s\n",
				      zfcp_get_busid_by_adapter(adapter));
			retval = ZFCP_ERP_FAILED;
			i--;
			break;
		}
	}

	return retval;
}

/*
 * function:    zfcp_fsf_cleanup
 *
 * purpose:	cleanup FSF operation for specified adapter
 *
 * returns:	0 - FSF operation successfully cleaned up
 *		!0 - failed to cleanup FSF operation for this adapter
 */
static int
zfcp_erp_adapter_strategy_close_fsf(struct zfcp_erp_action *erp_action)
{
	int retval = ZFCP_ERP_SUCCEEDED;
	struct zfcp_adapter *adapter = erp_action->adapter;

	/*
	 * wake waiting initiators of requests,
	 * return SCSI commands (with error status),
	 * clean up all requests (synchronously)
	 */
	zfcp_fsf_req_dismiss_all(adapter);
	/* reset FSF request sequence number */
	adapter->fsf_req_seq_no = 0;
	/* all ports and units are closed */
	zfcp_erp_modify_adapter_status(adapter,
				       ZFCP_STATUS_COMMON_OPEN, ZFCP_CLEAR);

	return retval;
}

/*
 * function:	
 *
 * purpose:	this routine executes the 'Reopen Physical Port' action
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_SUCCEEDED	- action finished successfully
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int
zfcp_erp_port_forced_strategy(struct zfcp_erp_action *erp_action)
{
	int retval = ZFCP_ERP_FAILED;
	struct zfcp_port *port = erp_action->port;
	struct zfcp_adapter *adapter = erp_action->adapter;

	switch (erp_action->step) {

		/*
		 * FIXME:
		 * the ULP spec. begs for waiting for oustanding commands
		 */
	case ZFCP_ERP_STEP_UNINITIALIZED:
		zfcp_erp_port_strategy_clearstati(port);
		/*
		 * it would be sufficient to test only the normal open flag
		 * since the phys. open flag cannot be set if the normal
		 * open flag is unset - however, this is for readabilty ...
		 */
		if (atomic_test_mask((ZFCP_STATUS_PORT_PHYS_OPEN |
				      ZFCP_STATUS_COMMON_OPEN),
			             &port->status)) {
			ZFCP_LOG_DEBUG("port 0x%016Lx is open -> trying "
				       "close physical\n", port->wwpn);
			retval =
			    zfcp_erp_port_forced_strategy_close(erp_action);
		} else
			retval = ZFCP_ERP_FAILED;
		break;

	case ZFCP_ERP_STEP_PHYS_PORT_CLOSING:
		if (atomic_test_mask(ZFCP_STATUS_PORT_PHYS_OPEN,
				     &port->status)) {
			ZFCP_LOG_DEBUG("close physical failed for port "
				       "0x%016Lx\n", port->wwpn);
			retval = ZFCP_ERP_FAILED;
		} else
			retval = ZFCP_ERP_SUCCEEDED;
		break;
	}

	debug_text_event(adapter->erp_dbf, 3, "p_pfst/ret");
	debug_event(adapter->erp_dbf, 3, &port->wwpn, sizeof (wwn_t));
	debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof (int));
	debug_event(adapter->erp_dbf, 3, &retval, sizeof (int));

	return retval;
}

/*
 * function:	
 *
 * purpose:	this routine executes the 'Reopen Port' action
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_SUCCEEDED	- action finished successfully
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int
zfcp_erp_port_strategy(struct zfcp_erp_action *erp_action)
{
	int retval = ZFCP_ERP_FAILED;
	struct zfcp_port *port = erp_action->port;
	struct zfcp_adapter *adapter = erp_action->adapter;

	switch (erp_action->step) {

		/*
		 * FIXME:
		 * the ULP spec. begs for waiting for oustanding commands
		 */
	case ZFCP_ERP_STEP_UNINITIALIZED:
		zfcp_erp_port_strategy_clearstati(port);
		if (atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &port->status)) {
			ZFCP_LOG_DEBUG("port 0x%016Lx is open -> trying "
				       "close\n", port->wwpn);
			retval = zfcp_erp_port_strategy_close(erp_action);
			goto out;
		}		/* else it's already closed, open it */
		break;

	case ZFCP_ERP_STEP_PORT_CLOSING:
		if (atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &port->status)) {
			ZFCP_LOG_DEBUG("close failed for port 0x%016Lx\n",
				       port->wwpn);
			retval = ZFCP_ERP_FAILED;
			goto out;
		}		/* else it's closed now, open it */
		break;
	}
	if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
		retval = ZFCP_ERP_EXIT;
	else
		retval = zfcp_erp_port_strategy_open(erp_action);

 out:
	debug_text_event(adapter->erp_dbf, 3, "p_pst/ret");
	debug_event(adapter->erp_dbf, 3, &port->wwpn, sizeof (wwn_t));
	debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof (int));
	debug_event(adapter->erp_dbf, 3, &retval, sizeof (int));

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int
zfcp_erp_port_strategy_open(struct zfcp_erp_action *erp_action)
{
	int retval;

	if (atomic_test_mask(ZFCP_STATUS_PORT_WKA,
			     &erp_action->port->status))
		retval = zfcp_erp_port_strategy_open_nameserver(erp_action);
	else
		retval = zfcp_erp_port_strategy_open_common(erp_action);

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 *
 * FIXME(design):	currently only prepared for fabric (nameserver!)
 */
static int
zfcp_erp_port_strategy_open_common(struct zfcp_erp_action *erp_action)
{
	int retval = 0;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_port *port = erp_action->port;

	switch (erp_action->step) {

	case ZFCP_ERP_STEP_UNINITIALIZED:
	case ZFCP_ERP_STEP_PHYS_PORT_CLOSING:
	case ZFCP_ERP_STEP_PORT_CLOSING:
		if (fc_host_port_type(adapter->scsi_host) == FC_PORTTYPE_PTP) {
			if (port->wwpn != adapter->peer_wwpn) {
				ZFCP_LOG_NORMAL("Failed to open port 0x%016Lx "
						"on adapter %s.\nPeer WWPN "
						"0x%016Lx does not match\n",
						port->wwpn,
						zfcp_get_busid_by_adapter(adapter),
						adapter->peer_wwpn);
				zfcp_erp_port_failed(port);
				retval = ZFCP_ERP_FAILED;
				break;
			}
			port->d_id = adapter->peer_d_id;
			atomic_set_mask(ZFCP_STATUS_PORT_DID_DID, &port->status);
			retval = zfcp_erp_port_strategy_open_port(erp_action);
			break;
		}
		if (!(adapter->nameserver_port)) {
			retval = zfcp_nameserver_enqueue(adapter);
			if (retval != 0) {
				ZFCP_LOG_NORMAL("error: nameserver port "
						"unavailable for adapter %s\n",
						zfcp_get_busid_by_adapter(adapter));
				retval = ZFCP_ERP_FAILED;
				break;
			}
		}
		if (!atomic_test_mask(ZFCP_STATUS_COMMON_UNBLOCKED,
				      &adapter->nameserver_port->status)) {
			ZFCP_LOG_DEBUG("nameserver port is not open -> open "
				       "nameserver port\n");
			/* nameserver port may live again */
			atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING,
					&adapter->nameserver_port->status);
			if (zfcp_erp_port_reopen(adapter->nameserver_port, 0)
			    >= 0) {
				erp_action->step =
					ZFCP_ERP_STEP_NAMESERVER_OPEN;
				retval = ZFCP_ERP_CONTINUES;
			} else
				retval = ZFCP_ERP_FAILED;
			break;
		}
		/* else nameserver port is already open, fall through */
	case ZFCP_ERP_STEP_NAMESERVER_OPEN:
		if (!atomic_test_mask(ZFCP_STATUS_COMMON_OPEN,
				      &adapter->nameserver_port->status)) {
			ZFCP_LOG_DEBUG("open failed for nameserver port\n");
			retval = ZFCP_ERP_FAILED;
		} else {
			ZFCP_LOG_DEBUG("nameserver port is open -> "
				       "nameserver look-up for port 0x%016Lx\n",
				       port->wwpn);
			retval = zfcp_erp_port_strategy_open_common_lookup
				(erp_action);
		}
		break;

	case ZFCP_ERP_STEP_NAMESERVER_LOOKUP:
		if (!atomic_test_mask(ZFCP_STATUS_PORT_DID_DID, &port->status)) {
			if (atomic_test_mask
			    (ZFCP_STATUS_PORT_INVALID_WWPN, &port->status)) {
				ZFCP_LOG_DEBUG("nameserver look-up failed "
					       "for port 0x%016Lx "
					       "(misconfigured WWPN?)\n",
					       port->wwpn);
				zfcp_erp_port_failed(port);
				retval = ZFCP_ERP_EXIT;
			} else {
				ZFCP_LOG_DEBUG("nameserver look-up failed for "
					       "port 0x%016Lx\n", port->wwpn);
				retval = ZFCP_ERP_FAILED;
			}
		} else {
			ZFCP_LOG_DEBUG("port 0x%016Lx has d_id=0x%08x -> "
				       "trying open\n", port->wwpn, port->d_id);
			retval = zfcp_erp_port_strategy_open_port(erp_action);
		}
		break;

	case ZFCP_ERP_STEP_PORT_OPENING:
		/* D_ID might have changed during open */
		if (atomic_test_mask((ZFCP_STATUS_COMMON_OPEN |
				      ZFCP_STATUS_PORT_DID_DID),
				     &port->status)) {
			ZFCP_LOG_DEBUG("port 0x%016Lx is open\n", port->wwpn);
			retval = ZFCP_ERP_SUCCEEDED;
		} else {
			ZFCP_LOG_DEBUG("open failed for port 0x%016Lx\n",
				       port->wwpn);
			retval = ZFCP_ERP_FAILED;
		}
		break;

	default:
		ZFCP_LOG_NORMAL("bug: unknown erp step 0x%08x\n",
				erp_action->step);
		retval = ZFCP_ERP_FAILED;
	}

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int
zfcp_erp_port_strategy_open_nameserver(struct zfcp_erp_action *erp_action)
{
	int retval;
	struct zfcp_port *port = erp_action->port;

	switch (erp_action->step) {

	case ZFCP_ERP_STEP_UNINITIALIZED:
	case ZFCP_ERP_STEP_PHYS_PORT_CLOSING:
	case ZFCP_ERP_STEP_PORT_CLOSING:
		ZFCP_LOG_DEBUG("port 0x%016Lx has d_id=0x%08x -> trying open\n",
			       port->wwpn, port->d_id);
		retval = zfcp_erp_port_strategy_open_port(erp_action);
		break;

	case ZFCP_ERP_STEP_PORT_OPENING:
		if (atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &port->status)) {
			ZFCP_LOG_DEBUG("WKA port is open\n");
			retval = ZFCP_ERP_SUCCEEDED;
		} else {
			ZFCP_LOG_DEBUG("open failed for WKA port\n");
			retval = ZFCP_ERP_FAILED;
		}
		/* this is needed anyway (dont care for retval of wakeup) */
		ZFCP_LOG_DEBUG("continue other open port operations\n");
		zfcp_erp_port_strategy_open_nameserver_wakeup(erp_action);
		break;

	default:
		ZFCP_LOG_NORMAL("bug: unknown erp step 0x%08x\n",
				erp_action->step);
		retval = ZFCP_ERP_FAILED;
	}

	return retval;
}

/*
 * function:	
 *
 * purpose:	makes the erp thread continue with reopen (physical) port
 *		actions which have been paused until the name server port
 *		is opened (or failed)
 *
 * returns:	0	(a kind of void retval, its not used)
 */
static int
zfcp_erp_port_strategy_open_nameserver_wakeup(struct zfcp_erp_action
					      *ns_erp_action)
{
	int retval = 0;
	unsigned long flags;
	struct zfcp_adapter *adapter = ns_erp_action->adapter;
	struct zfcp_erp_action *erp_action, *tmp;

	read_lock_irqsave(&adapter->erp_lock, flags);
	list_for_each_entry_safe(erp_action, tmp, &adapter->erp_running_head,
				 list) {
		debug_text_event(adapter->erp_dbf, 4, "p_pstnsw_n");
		debug_event(adapter->erp_dbf, 4, &erp_action->port->wwpn,
			    sizeof (wwn_t));
		if (erp_action->step == ZFCP_ERP_STEP_NAMESERVER_OPEN) {
			debug_text_event(adapter->erp_dbf, 3, "p_pstnsw_w");
			debug_event(adapter->erp_dbf, 3,
				    &erp_action->port->wwpn, sizeof (wwn_t));
			if (atomic_test_mask(
				    ZFCP_STATUS_COMMON_ERP_FAILED,
				    &adapter->nameserver_port->status))
				zfcp_erp_port_failed(erp_action->port);
			zfcp_erp_action_ready(erp_action);
		}
	}
	read_unlock_irqrestore(&adapter->erp_lock, flags);

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int
zfcp_erp_port_forced_strategy_close(struct zfcp_erp_action *erp_action)
{
	int retval;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_port *port = erp_action->port;

	zfcp_erp_timeout_init(erp_action);
	retval = zfcp_fsf_close_physical_port(erp_action);
	if (retval == -ENOMEM) {
		debug_text_event(adapter->erp_dbf, 5, "o_pfstc_nomem");
		debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));
		retval = ZFCP_ERP_NOMEM;
		goto out;
	}
	erp_action->step = ZFCP_ERP_STEP_PHYS_PORT_CLOSING;
	if (retval != 0) {
		debug_text_event(adapter->erp_dbf, 5, "o_pfstc_cpf");
		debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));
		/* could not send 'open', fail */
		retval = ZFCP_ERP_FAILED;
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 6, "o_pfstc_cpok");
	debug_event(adapter->erp_dbf, 6, &port->wwpn, sizeof (wwn_t));
	retval = ZFCP_ERP_CONTINUES;
 out:
	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int
zfcp_erp_port_strategy_clearstati(struct zfcp_port *port)
{
	int retval = 0;
	struct zfcp_adapter *adapter = port->adapter;

	debug_text_event(adapter->erp_dbf, 5, "p_pstclst");
	debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));

	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING |
			  ZFCP_STATUS_COMMON_CLOSING |
			  ZFCP_STATUS_COMMON_ACCESS_DENIED |
			  ZFCP_STATUS_PORT_DID_DID |
			  ZFCP_STATUS_PORT_PHYS_CLOSING |
			  ZFCP_STATUS_PORT_INVALID_WWPN,
			  &port->status);
	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int
zfcp_erp_port_strategy_close(struct zfcp_erp_action *erp_action)
{
	int retval;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_port *port = erp_action->port;

	zfcp_erp_timeout_init(erp_action);
	retval = zfcp_fsf_close_port(erp_action);
	if (retval == -ENOMEM) {
		debug_text_event(adapter->erp_dbf, 5, "p_pstc_nomem");
		debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));
		retval = ZFCP_ERP_NOMEM;
		goto out;
	}
	erp_action->step = ZFCP_ERP_STEP_PORT_CLOSING;
	if (retval != 0) {
		debug_text_event(adapter->erp_dbf, 5, "p_pstc_cpf");
		debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));
		/* could not send 'close', fail */
		retval = ZFCP_ERP_FAILED;
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 6, "p_pstc_cpok");
	debug_event(adapter->erp_dbf, 6, &port->wwpn, sizeof (wwn_t));
	retval = ZFCP_ERP_CONTINUES;
 out:
	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int
zfcp_erp_port_strategy_open_port(struct zfcp_erp_action *erp_action)
{
	int retval;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_port *port = erp_action->port;

	zfcp_erp_timeout_init(erp_action);
	retval = zfcp_fsf_open_port(erp_action);
	if (retval == -ENOMEM) {
		debug_text_event(adapter->erp_dbf, 5, "p_psto_nomem");
		debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));
		retval = ZFCP_ERP_NOMEM;
		goto out;
	}
	erp_action->step = ZFCP_ERP_STEP_PORT_OPENING;
	if (retval != 0) {
		debug_text_event(adapter->erp_dbf, 5, "p_psto_opf");
		debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));
		/* could not send 'open', fail */
		retval = ZFCP_ERP_FAILED;
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 6, "p_psto_opok");
	debug_event(adapter->erp_dbf, 6, &port->wwpn, sizeof (wwn_t));
	retval = ZFCP_ERP_CONTINUES;
 out:
	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int
zfcp_erp_port_strategy_open_common_lookup(struct zfcp_erp_action *erp_action)
{
	int retval;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_port *port = erp_action->port;

	zfcp_erp_timeout_init(erp_action);
	retval = zfcp_ns_gid_pn_request(erp_action);
	if (retval == -ENOMEM) {
		debug_text_event(adapter->erp_dbf, 5, "p_pstn_nomem");
		debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));
		retval = ZFCP_ERP_NOMEM;
		goto out;
	}
	erp_action->step = ZFCP_ERP_STEP_NAMESERVER_LOOKUP;
	if (retval != 0) {
		debug_text_event(adapter->erp_dbf, 5, "p_pstn_ref");
		debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));
		/* could not send nameserver request, fail */
		retval = ZFCP_ERP_FAILED;
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 6, "p_pstn_reok");
	debug_event(adapter->erp_dbf, 6, &port->wwpn, sizeof (wwn_t));
	retval = ZFCP_ERP_CONTINUES;
 out:
	return retval;
}

/*
 * function:	
 *
 * purpose:	this routine executes the 'Reopen Unit' action
 *		currently no retries
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_SUCCEEDED	- action finished successfully
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int
zfcp_erp_unit_strategy(struct zfcp_erp_action *erp_action)
{
	int retval = ZFCP_ERP_FAILED;
	struct zfcp_unit *unit = erp_action->unit;
	struct zfcp_adapter *adapter = erp_action->adapter;

	switch (erp_action->step) {

		/*
		 * FIXME:
		 * the ULP spec. begs for waiting for oustanding commands
		 */
	case ZFCP_ERP_STEP_UNINITIALIZED:
		zfcp_erp_unit_strategy_clearstati(unit);
		if (atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status)) {
			ZFCP_LOG_DEBUG("unit 0x%016Lx is open -> "
				       "trying close\n", unit->fcp_lun);
			retval = zfcp_erp_unit_strategy_close(erp_action);
			break;
		}
		/* else it's already closed, fall through */
	case ZFCP_ERP_STEP_UNIT_CLOSING:
		if (atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status)) {
			ZFCP_LOG_DEBUG("close failed for unit 0x%016Lx\n",
				       unit->fcp_lun);
			retval = ZFCP_ERP_FAILED;
		} else {
			if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
				retval = ZFCP_ERP_EXIT;
			else {
				ZFCP_LOG_DEBUG("unit 0x%016Lx is not open -> "
					       "trying open\n", unit->fcp_lun);
				retval =
				    zfcp_erp_unit_strategy_open(erp_action);
			}
		}
		break;

	case ZFCP_ERP_STEP_UNIT_OPENING:
		if (atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status)) {
			ZFCP_LOG_DEBUG("unit 0x%016Lx is open\n",
				       unit->fcp_lun);
			retval = ZFCP_ERP_SUCCEEDED;
		} else {
			ZFCP_LOG_DEBUG("open failed for unit 0x%016Lx\n",
				       unit->fcp_lun);
			retval = ZFCP_ERP_FAILED;
		}
		break;
	}

	debug_text_event(adapter->erp_dbf, 3, "u_ust/ret");
	debug_event(adapter->erp_dbf, 3, &unit->fcp_lun, sizeof (fcp_lun_t));
	debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof (int));
	debug_event(adapter->erp_dbf, 3, &retval, sizeof (int));
	return retval;
}

/*
 * function:
 *
 * purpose:
 *
 * returns:
 */
static int
zfcp_erp_unit_strategy_clearstati(struct zfcp_unit *unit)
{
	int retval = 0;
	struct zfcp_adapter *adapter = unit->port->adapter;

	debug_text_event(adapter->erp_dbf, 5, "u_ustclst");
	debug_event(adapter->erp_dbf, 5, &unit->fcp_lun, sizeof (fcp_lun_t));

	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING |
			  ZFCP_STATUS_COMMON_CLOSING |
			  ZFCP_STATUS_COMMON_ACCESS_DENIED |
			  ZFCP_STATUS_UNIT_SHARED |
			  ZFCP_STATUS_UNIT_READONLY,
			  &unit->status);

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int
zfcp_erp_unit_strategy_close(struct zfcp_erp_action *erp_action)
{
	int retval;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_unit *unit = erp_action->unit;

	zfcp_erp_timeout_init(erp_action);
	retval = zfcp_fsf_close_unit(erp_action);
	if (retval == -ENOMEM) {
		debug_text_event(adapter->erp_dbf, 5, "u_ustc_nomem");
		debug_event(adapter->erp_dbf, 5, &unit->fcp_lun,
			    sizeof (fcp_lun_t));
		retval = ZFCP_ERP_NOMEM;
		goto out;
	}
	erp_action->step = ZFCP_ERP_STEP_UNIT_CLOSING;
	if (retval != 0) {
		debug_text_event(adapter->erp_dbf, 5, "u_ustc_cuf");
		debug_event(adapter->erp_dbf, 5, &unit->fcp_lun,
			    sizeof (fcp_lun_t));
		/* could not send 'close', fail */
		retval = ZFCP_ERP_FAILED;
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 6, "u_ustc_cuok");
	debug_event(adapter->erp_dbf, 6, &unit->fcp_lun, sizeof (fcp_lun_t));
	retval = ZFCP_ERP_CONTINUES;

 out:
	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int
zfcp_erp_unit_strategy_open(struct zfcp_erp_action *erp_action)
{
	int retval;
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_unit *unit = erp_action->unit;

	zfcp_erp_timeout_init(erp_action);
	retval = zfcp_fsf_open_unit(erp_action);
	if (retval == -ENOMEM) {
		debug_text_event(adapter->erp_dbf, 5, "u_usto_nomem");
		debug_event(adapter->erp_dbf, 5, &unit->fcp_lun,
			    sizeof (fcp_lun_t));
		retval = ZFCP_ERP_NOMEM;
		goto out;
	}
	erp_action->step = ZFCP_ERP_STEP_UNIT_OPENING;
	if (retval != 0) {
		debug_text_event(adapter->erp_dbf, 5, "u_usto_ouf");
		debug_event(adapter->erp_dbf, 5, &unit->fcp_lun,
			    sizeof (fcp_lun_t));
		/* could not send 'open', fail */
		retval = ZFCP_ERP_FAILED;
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 6, "u_usto_ouok");
	debug_event(adapter->erp_dbf, 6, &unit->fcp_lun, sizeof (fcp_lun_t));
	retval = ZFCP_ERP_CONTINUES;
 out:
	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static inline void
zfcp_erp_timeout_init(struct zfcp_erp_action *erp_action)
{
	init_timer(&erp_action->timer);
	erp_action->timer.function = zfcp_erp_timeout_handler;
	erp_action->timer.data = (unsigned long) erp_action;
	/* jiffies will be added in zfcp_fsf_req_send */
	erp_action->timer.expires = ZFCP_ERP_FSFREQ_TIMEOUT;
}

/*
 * function:	
 *
 * purpose:	enqueue the specified error recovery action, if needed
 *
 * returns:
 */
static int
zfcp_erp_action_enqueue(int action,
			struct zfcp_adapter *adapter,
			struct zfcp_port *port, struct zfcp_unit *unit)
{
	int retval = 1;
	struct zfcp_erp_action *erp_action = NULL;
	int stronger_action = 0;
	u32 status = 0;

	/*
	 * We need some rules here which check whether we really need
	 * this action or whether we should just drop it.
	 * E.g. if there is a unfinished 'Reopen Port' request then we drop a
	 * 'Reopen Unit' request for an associated unit since we can't
	 * satisfy this request now. A 'Reopen Port' action will trigger
	 * 'Reopen Unit' actions when it completes.
	 * Thus, there are only actions in the queue which can immediately be
	 * executed. This makes the processing of the action queue more
	 * efficient.
	 */

	if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP,
			      &adapter->status))
		return -EIO;

	debug_event(adapter->erp_dbf, 4, &action, sizeof (int));
	/* check whether we really need this */
	switch (action) {
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		if (atomic_test_mask
		    (ZFCP_STATUS_COMMON_ERP_INUSE, &unit->status)) {
			debug_text_event(adapter->erp_dbf, 4, "u_actenq_drp");
			debug_event(adapter->erp_dbf, 4, &port->wwpn,
				    sizeof (wwn_t));
			debug_event(adapter->erp_dbf, 4, &unit->fcp_lun,
				    sizeof (fcp_lun_t));
			goto out;
		}
		if (!atomic_test_mask
		    (ZFCP_STATUS_COMMON_RUNNING, &port->status) ||
		    atomic_test_mask
		    (ZFCP_STATUS_COMMON_ERP_FAILED, &port->status)) {
			goto out;
		}
		if (!atomic_test_mask
		    (ZFCP_STATUS_COMMON_UNBLOCKED, &port->status)) {
			stronger_action = ZFCP_ERP_ACTION_REOPEN_PORT;
			unit = NULL;
		}
		/* fall through !!! */

	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if (atomic_test_mask
		    (ZFCP_STATUS_COMMON_ERP_INUSE, &port->status)) {
			debug_text_event(adapter->erp_dbf, 4, "p_actenq_drp");
			debug_event(adapter->erp_dbf, 4, &port->wwpn,
				    sizeof (wwn_t));
			goto out;
		}
		/* fall through !!! */

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE,
				     &port->status)) {
			if (port->erp_action.action !=
			    ZFCP_ERP_ACTION_REOPEN_PORT_FORCED) {
				ZFCP_LOG_INFO("dropped erp action %i (port "
					      "0x%016Lx, action in use: %i)\n",
					      action, port->wwpn,
					      port->erp_action.action);
				debug_text_event(adapter->erp_dbf, 4,
						 "pf_actenq_drp");
			} else 
				debug_text_event(adapter->erp_dbf, 4,
						 "pf_actenq_drpcp");
			debug_event(adapter->erp_dbf, 4, &port->wwpn,
				    sizeof (wwn_t));
			goto out;
		}
		if (!atomic_test_mask
		    (ZFCP_STATUS_COMMON_RUNNING, &adapter->status) ||
		    atomic_test_mask
		    (ZFCP_STATUS_COMMON_ERP_FAILED, &adapter->status)) {
			goto out;
		}
		if (!atomic_test_mask
		    (ZFCP_STATUS_COMMON_UNBLOCKED, &adapter->status)) {
			stronger_action = ZFCP_ERP_ACTION_REOPEN_ADAPTER;
			port = NULL;
		}
		/* fall through !!! */

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (atomic_test_mask
		    (ZFCP_STATUS_COMMON_ERP_INUSE, &adapter->status)) {
			debug_text_event(adapter->erp_dbf, 4, "a_actenq_drp");
			goto out;
		}
		break;

	default:
		debug_text_exception(adapter->erp_dbf, 1, "a_actenq_bug");
		debug_event(adapter->erp_dbf, 1, &action, sizeof (int));
		ZFCP_LOG_NORMAL("bug: unknown erp action requested "
				"on adapter %s (action=%d)\n",
				zfcp_get_busid_by_adapter(adapter), action);
		goto out;
	}

	/* check whether we need something stronger first */
	if (stronger_action) {
		debug_text_event(adapter->erp_dbf, 4, "a_actenq_str");
		debug_event(adapter->erp_dbf, 4, &stronger_action,
			    sizeof (int));
		ZFCP_LOG_DEBUG("stronger erp action %d needed before "
			       "erp action %d on adapter %s\n",
			       stronger_action, action,
			       zfcp_get_busid_by_adapter(adapter));
		action = stronger_action;
	}

	/* mark adapter to have some error recovery pending */
	atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_PENDING, &adapter->status);

	/* setup error recovery action */
	switch (action) {

	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		zfcp_unit_get(unit);
		atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &unit->status);
		erp_action = &unit->erp_action;
		if (!atomic_test_mask
		    (ZFCP_STATUS_COMMON_RUNNING, &unit->status))
			status = ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT:
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		zfcp_port_get(port);
		zfcp_erp_action_dismiss_port(port);
		atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &port->status);
		erp_action = &port->erp_action;
		if (!atomic_test_mask
		    (ZFCP_STATUS_COMMON_RUNNING, &port->status))
			status = ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		zfcp_adapter_get(adapter);
		zfcp_erp_action_dismiss_adapter(adapter);
		atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &adapter->status);
		erp_action = &adapter->erp_action;
		if (!atomic_test_mask
		    (ZFCP_STATUS_COMMON_RUNNING, &adapter->status))
			status = ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;
	}

	debug_text_event(adapter->erp_dbf, 4, "a_actenq");

	memset(erp_action, 0, sizeof (struct zfcp_erp_action));
	erp_action->adapter = adapter;
	erp_action->port = port;
	erp_action->unit = unit;
	erp_action->action = action;
	erp_action->status = status;

	++adapter->erp_total_count;

	/* finally put it into 'ready' queue and kick erp thread */
	list_add(&erp_action->list, &adapter->erp_ready_head);
	up(&adapter->erp_ready_sem);
	retval = 0;
 out:
	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int
zfcp_erp_action_dequeue(struct zfcp_erp_action *erp_action)
{
	int retval = 0;
	struct zfcp_adapter *adapter = erp_action->adapter;

	--adapter->erp_total_count;
	if (erp_action->status & ZFCP_STATUS_ERP_LOWMEM) {
		--adapter->erp_low_mem_count;
		erp_action->status &= ~ZFCP_STATUS_ERP_LOWMEM;
	}

	debug_text_event(adapter->erp_dbf, 4, "a_actdeq");
	debug_event(adapter->erp_dbf, 4, &erp_action->action, sizeof (int));
	list_del(&erp_action->list);
	switch (erp_action->action) {
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		atomic_clear_mask(ZFCP_STATUS_COMMON_ERP_INUSE,
				  &erp_action->unit->status);
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		atomic_clear_mask(ZFCP_STATUS_COMMON_ERP_INUSE,
				  &erp_action->port->status);
		break;
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		atomic_clear_mask(ZFCP_STATUS_COMMON_ERP_INUSE,
				  &erp_action->adapter->status);
		break;
	default:
		/* bug */
		break;
	}
	return retval;
}

/**
 * zfcp_erp_action_cleanup
 *
 * Register unit with scsi stack if appropriate and fix reference counts.
 * Note: Temporary units are not registered with scsi stack.
 */
static void
zfcp_erp_action_cleanup(int action, struct zfcp_adapter *adapter,
			struct zfcp_port *port, struct zfcp_unit *unit,
			int result)
{
	switch (action) {
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		if ((result == ZFCP_ERP_SUCCEEDED)
		    && (!atomic_test_mask(ZFCP_STATUS_UNIT_TEMPORARY,
					  &unit->status))
		    && !unit->device
		    && port->rport)
 			scsi_add_device(port->adapter->scsi_host, 0,
 					port->rport->scsi_target_id,
					unit->scsi_lun);
		zfcp_unit_put(unit);
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if ((result == ZFCP_ERP_SUCCEEDED)
		    && !atomic_test_mask(ZFCP_STATUS_PORT_NO_WWPN,
					 &port->status)
		    && !port->rport) {
			struct fc_rport_identifiers ids;
			ids.node_name = port->wwnn;
			ids.port_name = port->wwpn;
			ids.port_id = port->d_id;
			ids.roles = FC_RPORT_ROLE_FCP_TARGET;
			port->rport =
				fc_remote_port_add(adapter->scsi_host, 0, &ids);
			if (!port->rport)
				ZFCP_LOG_NORMAL("failed registration of rport"
						"(adapter %s, wwpn=0x%016Lx)\n",
						zfcp_get_busid_by_port(port),
						port->wwpn);
		}
		zfcp_port_put(port);
		break;
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		zfcp_adapter_put(adapter);
		break;
	default:
		break;
	}
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:	FIXME
 */
static int
zfcp_erp_action_dismiss_adapter(struct zfcp_adapter *adapter)
{
	int retval = 0;
	struct zfcp_port *port;

	debug_text_event(adapter->erp_dbf, 5, "a_actab");
	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &adapter->status))
		zfcp_erp_action_dismiss(&adapter->erp_action);
	else
		list_for_each_entry(port, &adapter->port_list_head, list)
		    zfcp_erp_action_dismiss_port(port);

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	FIXME
 */
static int
zfcp_erp_action_dismiss_port(struct zfcp_port *port)
{
	int retval = 0;
	struct zfcp_unit *unit;
	struct zfcp_adapter *adapter = port->adapter;

	debug_text_event(adapter->erp_dbf, 5, "p_actab");
	debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof (wwn_t));
	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &port->status))
		zfcp_erp_action_dismiss(&port->erp_action);
	else
		list_for_each_entry(unit, &port->unit_list_head, list)
		    zfcp_erp_action_dismiss_unit(unit);

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:	FIXME
 */
static int
zfcp_erp_action_dismiss_unit(struct zfcp_unit *unit)
{
	int retval = 0;
	struct zfcp_adapter *adapter = unit->port->adapter;

	debug_text_event(adapter->erp_dbf, 5, "u_actab");
	debug_event(adapter->erp_dbf, 5, &unit->fcp_lun, sizeof (fcp_lun_t));
	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &unit->status))
		zfcp_erp_action_dismiss(&unit->erp_action);

	return retval;
}

/*
 * function:	
 *
 * purpose:	moves erp_action to 'erp running list'
 *
 * returns:
 */
static inline void
zfcp_erp_action_to_running(struct zfcp_erp_action *erp_action)
{
	struct zfcp_adapter *adapter = erp_action->adapter;

	debug_text_event(adapter->erp_dbf, 6, "a_toru");
	debug_event(adapter->erp_dbf, 6, &erp_action->action, sizeof (int));
	list_move(&erp_action->list, &erp_action->adapter->erp_running_head);
}

/*
 * function:	
 *
 * purpose:	moves erp_action to 'erp ready list'
 *
 * returns:
 */
static inline void
zfcp_erp_action_to_ready(struct zfcp_erp_action *erp_action)
{
	struct zfcp_adapter *adapter = erp_action->adapter;

	debug_text_event(adapter->erp_dbf, 6, "a_tore");
	debug_event(adapter->erp_dbf, 6, &erp_action->action, sizeof (int));
	list_move(&erp_action->list, &erp_action->adapter->erp_ready_head);
}

/*
 * function:	zfcp_erp_port_boxed
 *
 * purpose:
 */
void
zfcp_erp_port_boxed(struct zfcp_port *port)
{
	struct zfcp_adapter *adapter = port->adapter;
	unsigned long flags;

	debug_text_event(adapter->erp_dbf, 3, "p_access_boxed");
	debug_event(adapter->erp_dbf, 3, &port->wwpn, sizeof(wwn_t));
	read_lock_irqsave(&zfcp_data.config_lock, flags);
	zfcp_erp_modify_port_status(port,
			ZFCP_STATUS_COMMON_ACCESS_BOXED,
			ZFCP_SET);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
	zfcp_erp_port_reopen(port, ZFCP_STATUS_COMMON_ERP_FAILED);
}

/*
 * function:	zfcp_erp_unit_boxed
 *
 * purpose:
 */
void
zfcp_erp_unit_boxed(struct zfcp_unit *unit)
{
	struct zfcp_adapter *adapter = unit->port->adapter;

	debug_text_event(adapter->erp_dbf, 3, "u_access_boxed");
	debug_event(adapter->erp_dbf, 3, &unit->fcp_lun, sizeof(fcp_lun_t));
	zfcp_erp_modify_unit_status(unit,
			ZFCP_STATUS_COMMON_ACCESS_BOXED,
			ZFCP_SET);
	zfcp_erp_unit_reopen(unit, ZFCP_STATUS_COMMON_ERP_FAILED);
}

/*
 * function:	zfcp_erp_port_access_denied
 *
 * purpose:
 */
void
zfcp_erp_port_access_denied(struct zfcp_port *port)
{
	struct zfcp_adapter *adapter = port->adapter;
	unsigned long flags;

	debug_text_event(adapter->erp_dbf, 3, "p_access_denied");
	debug_event(adapter->erp_dbf, 3, &port->wwpn, sizeof(wwn_t));
	read_lock_irqsave(&zfcp_data.config_lock, flags);
	zfcp_erp_modify_port_status(port,
			ZFCP_STATUS_COMMON_ERP_FAILED |
			ZFCP_STATUS_COMMON_ACCESS_DENIED,
			ZFCP_SET);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
}

/*
 * function:	zfcp_erp_unit_access_denied
 *
 * purpose:
 */
void
zfcp_erp_unit_access_denied(struct zfcp_unit *unit)
{
	struct zfcp_adapter *adapter = unit->port->adapter;

	debug_text_event(adapter->erp_dbf, 3, "u_access_denied");
	debug_event(adapter->erp_dbf, 3, &unit->fcp_lun, sizeof(fcp_lun_t));
	zfcp_erp_modify_unit_status(unit,
			ZFCP_STATUS_COMMON_ERP_FAILED |
			ZFCP_STATUS_COMMON_ACCESS_DENIED,
			ZFCP_SET);
}

/*
 * function:	zfcp_erp_adapter_access_changed
 *
 * purpose:
 */
void
zfcp_erp_adapter_access_changed(struct zfcp_adapter *adapter)
{
	struct zfcp_port *port;
	unsigned long flags;

	if (adapter->connection_features & FSF_FEATURE_NPIV_MODE)
		return;

	debug_text_event(adapter->erp_dbf, 3, "a_access_recover");
	debug_event(adapter->erp_dbf, 3, &adapter->name, 8);

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	if (adapter->nameserver_port)
		zfcp_erp_port_access_changed(adapter->nameserver_port);
	list_for_each_entry(port, &adapter->port_list_head, list)
		if (port != adapter->nameserver_port)
			zfcp_erp_port_access_changed(port);
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
}

/*
 * function:	zfcp_erp_port_access_changed
 *
 * purpose:
 */
void
zfcp_erp_port_access_changed(struct zfcp_port *port)
{
	struct zfcp_adapter *adapter = port->adapter;
	struct zfcp_unit *unit;

	debug_text_event(adapter->erp_dbf, 3, "p_access_recover");
	debug_event(adapter->erp_dbf, 3, &port->wwpn, sizeof(wwn_t));

	if (!atomic_test_mask(ZFCP_STATUS_COMMON_ACCESS_DENIED,
			      &port->status) &&
	    !atomic_test_mask(ZFCP_STATUS_COMMON_ACCESS_BOXED,
			      &port->status)) {
		if (!atomic_test_mask(ZFCP_STATUS_PORT_WKA, &port->status))
			list_for_each_entry(unit, &port->unit_list_head, list)
				zfcp_erp_unit_access_changed(unit);
		return;
	}

	ZFCP_LOG_NORMAL("reopen of port 0x%016Lx on adapter %s "
			"(due to ACT update)\n",
			port->wwpn, zfcp_get_busid_by_adapter(adapter));
	if (zfcp_erp_port_reopen(port, ZFCP_STATUS_COMMON_ERP_FAILED) != 0)
		ZFCP_LOG_NORMAL("failed reopen of port"
				"(adapter %s, wwpn=0x%016Lx)\n",
				zfcp_get_busid_by_adapter(adapter), port->wwpn);
}

/*
 * function:	zfcp_erp_unit_access_changed
 *
 * purpose:
 */
void
zfcp_erp_unit_access_changed(struct zfcp_unit *unit)
{
	struct zfcp_adapter *adapter = unit->port->adapter;

	debug_text_event(adapter->erp_dbf, 3, "u_access_recover");
	debug_event(adapter->erp_dbf, 3, &unit->fcp_lun, sizeof(fcp_lun_t));

	if (!atomic_test_mask(ZFCP_STATUS_COMMON_ACCESS_DENIED,
			      &unit->status) &&
	    !atomic_test_mask(ZFCP_STATUS_COMMON_ACCESS_BOXED,
			      &unit->status))
		return;

	ZFCP_LOG_NORMAL("reopen of unit 0x%016Lx on port 0x%016Lx "
			" on adapter %s (due to ACT update)\n",
			unit->fcp_lun, unit->port->wwpn,
			zfcp_get_busid_by_adapter(adapter));
	if (zfcp_erp_unit_reopen(unit, ZFCP_STATUS_COMMON_ERP_FAILED) != 0)
		ZFCP_LOG_NORMAL("failed reopen of unit (adapter %s, "
				"wwpn=0x%016Lx, fcp_lun=0x%016Lx)\n",
				zfcp_get_busid_by_adapter(adapter),
				unit->port->wwpn, unit->fcp_lun);
}

#undef ZFCP_LOG_AREA
