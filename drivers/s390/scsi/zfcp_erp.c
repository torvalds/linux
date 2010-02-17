/*
 * zfcp device driver
 *
 * Error Recovery Procedures (ERP).
 *
 * Copyright IBM Corporation 2002, 2010
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kthread.h>
#include "zfcp_ext.h"
#include "zfcp_reqlist.h"

#define ZFCP_MAX_ERPS                   3

enum zfcp_erp_act_flags {
	ZFCP_STATUS_ERP_TIMEDOUT	= 0x10000000,
	ZFCP_STATUS_ERP_CLOSE_ONLY	= 0x01000000,
	ZFCP_STATUS_ERP_DISMISSING	= 0x00100000,
	ZFCP_STATUS_ERP_DISMISSED	= 0x00200000,
	ZFCP_STATUS_ERP_LOWMEM		= 0x00400000,
};

enum zfcp_erp_steps {
	ZFCP_ERP_STEP_UNINITIALIZED	= 0x0000,
	ZFCP_ERP_STEP_FSF_XCONFIG	= 0x0001,
	ZFCP_ERP_STEP_PHYS_PORT_CLOSING	= 0x0010,
	ZFCP_ERP_STEP_PORT_CLOSING	= 0x0100,
	ZFCP_ERP_STEP_PORT_OPENING	= 0x0800,
	ZFCP_ERP_STEP_UNIT_CLOSING	= 0x1000,
	ZFCP_ERP_STEP_UNIT_OPENING	= 0x2000,
};

enum zfcp_erp_act_type {
	ZFCP_ERP_ACTION_REOPEN_UNIT        = 1,
	ZFCP_ERP_ACTION_REOPEN_PORT	   = 2,
	ZFCP_ERP_ACTION_REOPEN_PORT_FORCED = 3,
	ZFCP_ERP_ACTION_REOPEN_ADAPTER     = 4,
};

enum zfcp_erp_act_state {
	ZFCP_ERP_ACTION_RUNNING = 1,
	ZFCP_ERP_ACTION_READY   = 2,
};

enum zfcp_erp_act_result {
	ZFCP_ERP_SUCCEEDED = 0,
	ZFCP_ERP_FAILED    = 1,
	ZFCP_ERP_CONTINUES = 2,
	ZFCP_ERP_EXIT      = 3,
	ZFCP_ERP_DISMISSED = 4,
	ZFCP_ERP_NOMEM     = 5,
};

static void zfcp_erp_adapter_block(struct zfcp_adapter *adapter, int mask)
{
	zfcp_erp_modify_adapter_status(adapter, "erablk1", NULL,
				       ZFCP_STATUS_COMMON_UNBLOCKED | mask,
				       ZFCP_CLEAR);
}

static int zfcp_erp_action_exists(struct zfcp_erp_action *act)
{
	struct zfcp_erp_action *curr_act;

	list_for_each_entry(curr_act, &act->adapter->erp_running_head, list)
		if (act == curr_act)
			return ZFCP_ERP_ACTION_RUNNING;
	return 0;
}

static void zfcp_erp_action_ready(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;

	list_move(&act->list, &act->adapter->erp_ready_head);
	zfcp_dbf_rec_action("erardy1", act);
	wake_up(&adapter->erp_ready_wq);
	zfcp_dbf_rec_thread("erardy2", adapter->dbf);
}

static void zfcp_erp_action_dismiss(struct zfcp_erp_action *act)
{
	act->status |= ZFCP_STATUS_ERP_DISMISSED;
	if (zfcp_erp_action_exists(act) == ZFCP_ERP_ACTION_RUNNING)
		zfcp_erp_action_ready(act);
}

static void zfcp_erp_action_dismiss_unit(struct zfcp_unit *unit)
{
	if (atomic_read(&unit->status) & ZFCP_STATUS_COMMON_ERP_INUSE)
		zfcp_erp_action_dismiss(&unit->erp_action);
}

static void zfcp_erp_action_dismiss_port(struct zfcp_port *port)
{
	struct zfcp_unit *unit;

	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_INUSE)
		zfcp_erp_action_dismiss(&port->erp_action);
	else {
		read_lock(&port->unit_list_lock);
		list_for_each_entry(unit, &port->unit_list, list)
			zfcp_erp_action_dismiss_unit(unit);
		read_unlock(&port->unit_list_lock);
	}
}

static void zfcp_erp_action_dismiss_adapter(struct zfcp_adapter *adapter)
{
	struct zfcp_port *port;

	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_ERP_INUSE)
		zfcp_erp_action_dismiss(&adapter->erp_action);
	else {
		read_lock(&adapter->port_list_lock);
		list_for_each_entry(port, &adapter->port_list, list)
		    zfcp_erp_action_dismiss_port(port);
		read_unlock(&adapter->port_list_lock);
	}
}

static int zfcp_erp_required_act(int want, struct zfcp_adapter *adapter,
				 struct zfcp_port *port,
				 struct zfcp_unit *unit)
{
	int need = want;
	int u_status, p_status, a_status;

	switch (want) {
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		u_status = atomic_read(&unit->status);
		if (u_status & ZFCP_STATUS_COMMON_ERP_INUSE)
			return 0;
		p_status = atomic_read(&port->status);
		if (!(p_status & ZFCP_STATUS_COMMON_RUNNING) ||
		      p_status & ZFCP_STATUS_COMMON_ERP_FAILED)
			return 0;
		if (!(p_status & ZFCP_STATUS_COMMON_UNBLOCKED))
			need = ZFCP_ERP_ACTION_REOPEN_PORT;
		/* fall through */
	case ZFCP_ERP_ACTION_REOPEN_PORT:
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		p_status = atomic_read(&port->status);
		if (p_status & ZFCP_STATUS_COMMON_ERP_INUSE)
			return 0;
		a_status = atomic_read(&adapter->status);
		if (!(a_status & ZFCP_STATUS_COMMON_RUNNING) ||
		      a_status & ZFCP_STATUS_COMMON_ERP_FAILED)
			return 0;
		if (!(a_status & ZFCP_STATUS_COMMON_UNBLOCKED))
			need = ZFCP_ERP_ACTION_REOPEN_ADAPTER;
		/* fall through */
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		a_status = atomic_read(&adapter->status);
		if (a_status & ZFCP_STATUS_COMMON_ERP_INUSE)
			return 0;
		if (!(a_status & ZFCP_STATUS_COMMON_RUNNING) &&
		    !(a_status & ZFCP_STATUS_COMMON_OPEN))
			return 0; /* shutdown requested for closed adapter */
	}

	return need;
}

static struct zfcp_erp_action *zfcp_erp_setup_act(int need,
						  struct zfcp_adapter *adapter,
						  struct zfcp_port *port,
						  struct zfcp_unit *unit)
{
	struct zfcp_erp_action *erp_action;
	u32 status = 0;

	switch (need) {
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		if (!get_device(&unit->dev))
			return NULL;
		atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &unit->status);
		erp_action = &unit->erp_action;
		if (!(atomic_read(&unit->status) & ZFCP_STATUS_COMMON_RUNNING))
			status = ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT:
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		if (!get_device(&port->dev))
			return NULL;
		zfcp_erp_action_dismiss_port(port);
		atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &port->status);
		erp_action = &port->erp_action;
		if (!(atomic_read(&port->status) & ZFCP_STATUS_COMMON_RUNNING))
			status = ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		kref_get(&adapter->ref);
		zfcp_erp_action_dismiss_adapter(adapter);
		atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &adapter->status);
		erp_action = &adapter->erp_action;
		if (!(atomic_read(&adapter->status) &
		      ZFCP_STATUS_COMMON_RUNNING))
			status = ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	default:
		return NULL;
	}

	memset(erp_action, 0, sizeof(struct zfcp_erp_action));
	erp_action->adapter = adapter;
	erp_action->port = port;
	erp_action->unit = unit;
	erp_action->action = need;
	erp_action->status = status;

	return erp_action;
}

static int zfcp_erp_action_enqueue(int want, struct zfcp_adapter *adapter,
				   struct zfcp_port *port,
				   struct zfcp_unit *unit, char *id, void *ref)
{
	int retval = 1, need;
	struct zfcp_erp_action *act = NULL;

	if (!adapter->erp_thread)
		return -EIO;

	need = zfcp_erp_required_act(want, adapter, port, unit);
	if (!need)
		goto out;

	atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_PENDING, &adapter->status);
	act = zfcp_erp_setup_act(need, adapter, port, unit);
	if (!act)
		goto out;
	++adapter->erp_total_count;
	list_add_tail(&act->list, &adapter->erp_ready_head);
	wake_up(&adapter->erp_ready_wq);
	zfcp_dbf_rec_thread("eracte1", adapter->dbf);
	retval = 0;
 out:
	zfcp_dbf_rec_trigger(id, ref, want, need, act, adapter, port, unit);
	return retval;
}

static int _zfcp_erp_adapter_reopen(struct zfcp_adapter *adapter,
				    int clear_mask, char *id, void *ref)
{
	zfcp_erp_adapter_block(adapter, clear_mask);
	zfcp_scsi_schedule_rports_block(adapter);

	/* ensure propagation of failed status to new devices */
	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_adapter_failed(adapter, "erareo1", NULL);
		return -EIO;
	}
	return zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_ADAPTER,
				       adapter, NULL, NULL, id, ref);
}

/**
 * zfcp_erp_adapter_reopen - Reopen adapter.
 * @adapter: Adapter to reopen.
 * @clear: Status flags to clear.
 * @id: Id for debug trace event.
 * @ref: Reference for debug trace event.
 */
void zfcp_erp_adapter_reopen(struct zfcp_adapter *adapter, int clear,
			     char *id, void *ref)
{
	unsigned long flags;

	zfcp_erp_adapter_block(adapter, clear);
	zfcp_scsi_schedule_rports_block(adapter);

	write_lock_irqsave(&adapter->erp_lock, flags);
	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_ERP_FAILED)
		zfcp_erp_adapter_failed(adapter, "erareo1", NULL);
	else
		zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_ADAPTER, adapter,
					NULL, NULL, id, ref);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

/**
 * zfcp_erp_adapter_shutdown - Shutdown adapter.
 * @adapter: Adapter to shut down.
 * @clear: Status flags to clear.
 * @id: Id for debug trace event.
 * @ref: Reference for debug trace event.
 */
void zfcp_erp_adapter_shutdown(struct zfcp_adapter *adapter, int clear,
			       char *id, void *ref)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_adapter_reopen(adapter, clear | flags, id, ref);
}

/**
 * zfcp_erp_port_shutdown - Shutdown port
 * @port: Port to shut down.
 * @clear: Status flags to clear.
 * @id: Id for debug trace event.
 * @ref: Reference for debug trace event.
 */
void zfcp_erp_port_shutdown(struct zfcp_port *port, int clear, char *id,
			    void *ref)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_port_reopen(port, clear | flags, id, ref);
}

/**
 * zfcp_erp_unit_shutdown - Shutdown unit
 * @unit: Unit to shut down.
 * @clear: Status flags to clear.
 * @id: Id for debug trace event.
 * @ref: Reference for debug trace event.
 */
void zfcp_erp_unit_shutdown(struct zfcp_unit *unit, int clear, char *id,
			    void *ref)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_unit_reopen(unit, clear | flags, id, ref);
}

static void zfcp_erp_port_block(struct zfcp_port *port, int clear)
{
	zfcp_erp_modify_port_status(port, "erpblk1", NULL,
				    ZFCP_STATUS_COMMON_UNBLOCKED | clear,
				    ZFCP_CLEAR);
}

static void _zfcp_erp_port_forced_reopen(struct zfcp_port *port,
					 int clear, char *id, void *ref)
{
	zfcp_erp_port_block(port, clear);
	zfcp_scsi_schedule_rport_block(port);

	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_FAILED)
		return;

	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_PORT_FORCED,
				port->adapter, port, NULL, id, ref);
}

/**
 * zfcp_erp_port_forced_reopen - Forced close of port and open again
 * @port: Port to force close and to reopen.
 * @id: Id for debug trace event.
 * @ref: Reference for debug trace event.
 */
void zfcp_erp_port_forced_reopen(struct zfcp_port *port, int clear, char *id,
				 void *ref)
{
	unsigned long flags;
	struct zfcp_adapter *adapter = port->adapter;

	write_lock_irqsave(&adapter->erp_lock, flags);
	_zfcp_erp_port_forced_reopen(port, clear, id, ref);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

static int _zfcp_erp_port_reopen(struct zfcp_port *port, int clear, char *id,
				 void *ref)
{
	zfcp_erp_port_block(port, clear);
	zfcp_scsi_schedule_rport_block(port);

	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		/* ensure propagation of failed status to new devices */
		zfcp_erp_port_failed(port, "erpreo1", NULL);
		return -EIO;
	}

	return zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_PORT,
				       port->adapter, port, NULL, id, ref);
}

/**
 * zfcp_erp_port_reopen - trigger remote port recovery
 * @port: port to recover
 * @clear_mask: flags in port status to be cleared
 *
 * Returns 0 if recovery has been triggered, < 0 if not.
 */
int zfcp_erp_port_reopen(struct zfcp_port *port, int clear, char *id, void *ref)
{
	int retval;
	unsigned long flags;
	struct zfcp_adapter *adapter = port->adapter;

	write_lock_irqsave(&adapter->erp_lock, flags);
	retval = _zfcp_erp_port_reopen(port, clear, id, ref);
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	return retval;
}

static void zfcp_erp_unit_block(struct zfcp_unit *unit, int clear_mask)
{
	zfcp_erp_modify_unit_status(unit, "erublk1", NULL,
				    ZFCP_STATUS_COMMON_UNBLOCKED | clear_mask,
				    ZFCP_CLEAR);
}

static void _zfcp_erp_unit_reopen(struct zfcp_unit *unit, int clear, char *id,
				  void *ref)
{
	struct zfcp_adapter *adapter = unit->port->adapter;

	zfcp_erp_unit_block(unit, clear);

	if (atomic_read(&unit->status) & ZFCP_STATUS_COMMON_ERP_FAILED)
		return;

	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_UNIT,
				adapter, unit->port, unit, id, ref);
}

/**
 * zfcp_erp_unit_reopen - initiate reopen of a unit
 * @unit: unit to be reopened
 * @clear_mask: specifies flags in unit status to be cleared
 * Return: 0 on success, < 0 on error
 */
void zfcp_erp_unit_reopen(struct zfcp_unit *unit, int clear, char *id,
			  void *ref)
{
	unsigned long flags;
	struct zfcp_port *port = unit->port;
	struct zfcp_adapter *adapter = port->adapter;

	write_lock_irqsave(&adapter->erp_lock, flags);
	_zfcp_erp_unit_reopen(unit, clear, id, ref);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

static int status_change_set(unsigned long mask, atomic_t *status)
{
	return (atomic_read(status) ^ mask) & mask;
}

static int status_change_clear(unsigned long mask, atomic_t *status)
{
	return atomic_read(status) & mask;
}

static void zfcp_erp_adapter_unblock(struct zfcp_adapter *adapter)
{
	if (status_change_set(ZFCP_STATUS_COMMON_UNBLOCKED, &adapter->status))
		zfcp_dbf_rec_adapter("eraubl1", NULL, adapter->dbf);
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &adapter->status);
}

static void zfcp_erp_port_unblock(struct zfcp_port *port)
{
	if (status_change_set(ZFCP_STATUS_COMMON_UNBLOCKED, &port->status))
		zfcp_dbf_rec_port("erpubl1", NULL, port);
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &port->status);
}

static void zfcp_erp_unit_unblock(struct zfcp_unit *unit)
{
	if (status_change_set(ZFCP_STATUS_COMMON_UNBLOCKED, &unit->status))
		zfcp_dbf_rec_unit("eruubl1", NULL, unit);
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &unit->status);
}

static void zfcp_erp_action_to_running(struct zfcp_erp_action *erp_action)
{
	list_move(&erp_action->list, &erp_action->adapter->erp_running_head);
	zfcp_dbf_rec_action("erator1", erp_action);
}

static void zfcp_erp_strategy_check_fsfreq(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;
	struct zfcp_fsf_req *req;

	if (!act->fsf_req_id)
		return;

	spin_lock(&adapter->req_list->lock);
	req = _zfcp_reqlist_find(adapter->req_list, act->fsf_req_id);
	if (req && req->erp_action == act) {
		if (act->status & (ZFCP_STATUS_ERP_DISMISSED |
				   ZFCP_STATUS_ERP_TIMEDOUT)) {
			req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
			zfcp_dbf_rec_action("erscf_1", act);
			req->erp_action = NULL;
		}
		if (act->status & ZFCP_STATUS_ERP_TIMEDOUT)
			zfcp_dbf_rec_action("erscf_2", act);
		if (req->status & ZFCP_STATUS_FSFREQ_DISMISSED)
			act->fsf_req_id = 0;
	} else
		act->fsf_req_id = 0;
	spin_unlock(&adapter->req_list->lock);
}

/**
 * zfcp_erp_notify - Trigger ERP action.
 * @erp_action: ERP action to continue.
 * @set_mask: ERP action status flags to set.
 */
void zfcp_erp_notify(struct zfcp_erp_action *erp_action, unsigned long set_mask)
{
	struct zfcp_adapter *adapter = erp_action->adapter;
	unsigned long flags;

	write_lock_irqsave(&adapter->erp_lock, flags);
	if (zfcp_erp_action_exists(erp_action) == ZFCP_ERP_ACTION_RUNNING) {
		erp_action->status |= set_mask;
		zfcp_erp_action_ready(erp_action);
	}
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

/**
 * zfcp_erp_timeout_handler - Trigger ERP action from timed out ERP request
 * @data: ERP action (from timer data)
 */
void zfcp_erp_timeout_handler(unsigned long data)
{
	struct zfcp_erp_action *act = (struct zfcp_erp_action *) data;
	zfcp_erp_notify(act, ZFCP_STATUS_ERP_TIMEDOUT);
}

static void zfcp_erp_memwait_handler(unsigned long data)
{
	zfcp_erp_notify((struct zfcp_erp_action *)data, 0);
}

static void zfcp_erp_strategy_memwait(struct zfcp_erp_action *erp_action)
{
	init_timer(&erp_action->timer);
	erp_action->timer.function = zfcp_erp_memwait_handler;
	erp_action->timer.data = (unsigned long) erp_action;
	erp_action->timer.expires = jiffies + HZ;
	add_timer(&erp_action->timer);
}

static void _zfcp_erp_port_reopen_all(struct zfcp_adapter *adapter,
				      int clear, char *id, void *ref)
{
	struct zfcp_port *port;

	read_lock(&adapter->port_list_lock);
	list_for_each_entry(port, &adapter->port_list, list)
		_zfcp_erp_port_reopen(port, clear, id, ref);
	read_unlock(&adapter->port_list_lock);
}

static void _zfcp_erp_unit_reopen_all(struct zfcp_port *port, int clear,
				      char *id, void *ref)
{
	struct zfcp_unit *unit;

	read_lock(&port->unit_list_lock);
	list_for_each_entry(unit, &port->unit_list, list)
		_zfcp_erp_unit_reopen(unit, clear, id, ref);
	read_unlock(&port->unit_list_lock);
}

static void zfcp_erp_strategy_followup_failed(struct zfcp_erp_action *act)
{
	switch (act->action) {
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		_zfcp_erp_adapter_reopen(act->adapter, 0, "ersff_1", NULL);
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		_zfcp_erp_port_forced_reopen(act->port, 0, "ersff_2", NULL);
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		_zfcp_erp_port_reopen(act->port, 0, "ersff_3", NULL);
		break;
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		_zfcp_erp_unit_reopen(act->unit, 0, "ersff_4", NULL);
		break;
	}
}

static void zfcp_erp_strategy_followup_success(struct zfcp_erp_action *act)
{
	switch (act->action) {
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		_zfcp_erp_port_reopen_all(act->adapter, 0, "ersfs_1", NULL);
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		_zfcp_erp_port_reopen(act->port, 0, "ersfs_2", NULL);
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		_zfcp_erp_unit_reopen_all(act->port, 0, "ersfs_3", NULL);
		break;
	}
}

static void zfcp_erp_wakeup(struct zfcp_adapter *adapter)
{
	unsigned long flags;

	read_lock_irqsave(&adapter->erp_lock, flags);
	if (list_empty(&adapter->erp_ready_head) &&
	    list_empty(&adapter->erp_running_head)) {
			atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_PENDING,
					  &adapter->status);
			wake_up(&adapter->erp_done_wqh);
	}
	read_unlock_irqrestore(&adapter->erp_lock, flags);
}

static int zfcp_erp_adapter_strategy_open_qdio(struct zfcp_erp_action *act)
{
	struct zfcp_qdio *qdio = act->adapter->qdio;

	if (zfcp_qdio_open(qdio))
		return ZFCP_ERP_FAILED;
	init_waitqueue_head(&qdio->req_q_wq);
	atomic_set_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &act->adapter->status);
	return ZFCP_ERP_SUCCEEDED;
}

static void zfcp_erp_enqueue_ptp_port(struct zfcp_adapter *adapter)
{
	struct zfcp_port *port;
	port = zfcp_port_enqueue(adapter, adapter->peer_wwpn, 0,
				 adapter->peer_d_id);
	if (IS_ERR(port)) /* error or port already attached */
		return;
	_zfcp_erp_port_reopen(port, 0, "ereptp1", NULL);
}

static int zfcp_erp_adapter_strat_fsf_xconf(struct zfcp_erp_action *erp_action)
{
	int retries;
	int sleep = 1;
	struct zfcp_adapter *adapter = erp_action->adapter;

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK, &adapter->status);

	for (retries = 7; retries; retries--) {
		atomic_clear_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
				  &adapter->status);
		write_lock_irq(&adapter->erp_lock);
		zfcp_erp_action_to_running(erp_action);
		write_unlock_irq(&adapter->erp_lock);
		if (zfcp_fsf_exchange_config_data(erp_action)) {
			atomic_clear_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
					  &adapter->status);
			return ZFCP_ERP_FAILED;
		}

		zfcp_dbf_rec_thread_lock("erasfx1", adapter->dbf);
		wait_event(adapter->erp_ready_wq,
			   !list_empty(&adapter->erp_ready_head));
		zfcp_dbf_rec_thread_lock("erasfx2", adapter->dbf);
		if (erp_action->status & ZFCP_STATUS_ERP_TIMEDOUT)
			break;

		if (!(atomic_read(&adapter->status) &
		      ZFCP_STATUS_ADAPTER_HOST_CON_INIT))
			break;

		ssleep(sleep);
		sleep *= 2;
	}

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
			  &adapter->status);

	if (!(atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_XCONFIG_OK))
		return ZFCP_ERP_FAILED;

	if (fc_host_port_type(adapter->scsi_host) == FC_PORTTYPE_PTP)
		zfcp_erp_enqueue_ptp_port(adapter);

	return ZFCP_ERP_SUCCEEDED;
}

static int zfcp_erp_adapter_strategy_open_fsf_xport(struct zfcp_erp_action *act)
{
	int ret;
	struct zfcp_adapter *adapter = act->adapter;

	write_lock_irq(&adapter->erp_lock);
	zfcp_erp_action_to_running(act);
	write_unlock_irq(&adapter->erp_lock);

	ret = zfcp_fsf_exchange_port_data(act);
	if (ret == -EOPNOTSUPP)
		return ZFCP_ERP_SUCCEEDED;
	if (ret)
		return ZFCP_ERP_FAILED;

	zfcp_dbf_rec_thread_lock("erasox1", adapter->dbf);
	wait_event(adapter->erp_ready_wq,
		   !list_empty(&adapter->erp_ready_head));
	zfcp_dbf_rec_thread_lock("erasox2", adapter->dbf);
	if (act->status & ZFCP_STATUS_ERP_TIMEDOUT)
		return ZFCP_ERP_FAILED;

	return ZFCP_ERP_SUCCEEDED;
}

static int zfcp_erp_adapter_strategy_open_fsf(struct zfcp_erp_action *act)
{
	if (zfcp_erp_adapter_strat_fsf_xconf(act) == ZFCP_ERP_FAILED)
		return ZFCP_ERP_FAILED;

	if (zfcp_erp_adapter_strategy_open_fsf_xport(act) == ZFCP_ERP_FAILED)
		return ZFCP_ERP_FAILED;

	atomic_set(&act->adapter->stat_miss, 16);
	if (zfcp_status_read_refill(act->adapter))
		return ZFCP_ERP_FAILED;

	return ZFCP_ERP_SUCCEEDED;
}

static void zfcp_erp_adapter_strategy_close(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;

	/* close queues to ensure that buffers are not accessed by adapter */
	zfcp_qdio_close(adapter->qdio);
	zfcp_fsf_req_dismiss_all(adapter);
	adapter->fsf_req_seq_no = 0;
	zfcp_fc_wka_ports_force_offline(adapter->gs);
	/* all ports and units are closed */
	zfcp_erp_modify_adapter_status(adapter, "erascl1", NULL,
				       ZFCP_STATUS_COMMON_OPEN, ZFCP_CLEAR);

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK |
			  ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED, &adapter->status);
}

static int zfcp_erp_adapter_strategy_open(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;

	if (zfcp_erp_adapter_strategy_open_qdio(act)) {
		atomic_clear_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK |
				  ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED,
				  &adapter->status);
		return ZFCP_ERP_FAILED;
	}

	if (zfcp_erp_adapter_strategy_open_fsf(act)) {
		zfcp_erp_adapter_strategy_close(act);
		return ZFCP_ERP_FAILED;
	}

	atomic_set_mask(ZFCP_STATUS_COMMON_OPEN, &adapter->status);

	return ZFCP_ERP_SUCCEEDED;
}

static int zfcp_erp_adapter_strategy(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;

	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_OPEN) {
		zfcp_erp_adapter_strategy_close(act);
		if (act->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
			return ZFCP_ERP_EXIT;
	}

	if (zfcp_erp_adapter_strategy_open(act)) {
		ssleep(8);
		return ZFCP_ERP_FAILED;
	}

	return ZFCP_ERP_SUCCEEDED;
}

static int zfcp_erp_port_forced_strategy_close(struct zfcp_erp_action *act)
{
	int retval;

	retval = zfcp_fsf_close_physical_port(act);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	act->step = ZFCP_ERP_STEP_PHYS_PORT_CLOSING;
	if (retval)
		return ZFCP_ERP_FAILED;

	return ZFCP_ERP_CONTINUES;
}

static void zfcp_erp_port_strategy_clearstati(struct zfcp_port *port)
{
	atomic_clear_mask(ZFCP_STATUS_COMMON_ACCESS_DENIED, &port->status);
}

static int zfcp_erp_port_forced_strategy(struct zfcp_erp_action *erp_action)
{
	struct zfcp_port *port = erp_action->port;
	int status = atomic_read(&port->status);

	switch (erp_action->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
		zfcp_erp_port_strategy_clearstati(port);
		if ((status & ZFCP_STATUS_PORT_PHYS_OPEN) &&
		    (status & ZFCP_STATUS_COMMON_OPEN))
			return zfcp_erp_port_forced_strategy_close(erp_action);
		else
			return ZFCP_ERP_FAILED;

	case ZFCP_ERP_STEP_PHYS_PORT_CLOSING:
		if (!(status & ZFCP_STATUS_PORT_PHYS_OPEN))
			return ZFCP_ERP_SUCCEEDED;
	}
	return ZFCP_ERP_FAILED;
}

static int zfcp_erp_port_strategy_close(struct zfcp_erp_action *erp_action)
{
	int retval;

	retval = zfcp_fsf_close_port(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_PORT_CLOSING;
	if (retval)
		return ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static int zfcp_erp_port_strategy_open_port(struct zfcp_erp_action *erp_action)
{
	int retval;

	retval = zfcp_fsf_open_port(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_PORT_OPENING;
	if (retval)
		return ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static int zfcp_erp_open_ptp_port(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;
	struct zfcp_port *port = act->port;

	if (port->wwpn != adapter->peer_wwpn) {
		zfcp_erp_port_failed(port, "eroptp1", NULL);
		return ZFCP_ERP_FAILED;
	}
	port->d_id = adapter->peer_d_id;
	return zfcp_erp_port_strategy_open_port(act);
}

static int zfcp_erp_port_strategy_open_common(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;
	struct zfcp_port *port = act->port;
	int p_status = atomic_read(&port->status);

	switch (act->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
	case ZFCP_ERP_STEP_PHYS_PORT_CLOSING:
	case ZFCP_ERP_STEP_PORT_CLOSING:
		if (fc_host_port_type(adapter->scsi_host) == FC_PORTTYPE_PTP)
			return zfcp_erp_open_ptp_port(act);
		if (!port->d_id) {
			zfcp_fc_trigger_did_lookup(port);
			return ZFCP_ERP_EXIT;
		}
		return zfcp_erp_port_strategy_open_port(act);

	case ZFCP_ERP_STEP_PORT_OPENING:
		/* D_ID might have changed during open */
		if (p_status & ZFCP_STATUS_COMMON_OPEN) {
			if (!port->d_id) {
				zfcp_fc_trigger_did_lookup(port);
				return ZFCP_ERP_EXIT;
			}
			return ZFCP_ERP_SUCCEEDED;
		}
		if (port->d_id && !(p_status & ZFCP_STATUS_COMMON_NOESC)) {
			port->d_id = 0;
			_zfcp_erp_port_reopen(port, 0, "erpsoc1", NULL);
			return ZFCP_ERP_EXIT;
		}
		/* fall through otherwise */
	}
	return ZFCP_ERP_FAILED;
}

static int zfcp_erp_port_strategy(struct zfcp_erp_action *erp_action)
{
	struct zfcp_port *port = erp_action->port;
	int p_status = atomic_read(&port->status);

	if ((p_status & ZFCP_STATUS_COMMON_NOESC) &&
	    !(p_status & ZFCP_STATUS_COMMON_OPEN))
		goto close_init_done;

	switch (erp_action->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
		zfcp_erp_port_strategy_clearstati(port);
		if (p_status & ZFCP_STATUS_COMMON_OPEN)
			return zfcp_erp_port_strategy_close(erp_action);
		break;

	case ZFCP_ERP_STEP_PORT_CLOSING:
		if (p_status & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_FAILED;
		break;
	}

close_init_done:
	if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
		return ZFCP_ERP_EXIT;

	return zfcp_erp_port_strategy_open_common(erp_action);
}

static void zfcp_erp_unit_strategy_clearstati(struct zfcp_unit *unit)
{
	atomic_clear_mask(ZFCP_STATUS_COMMON_ACCESS_DENIED |
			  ZFCP_STATUS_UNIT_SHARED |
			  ZFCP_STATUS_UNIT_READONLY,
			  &unit->status);
}

static int zfcp_erp_unit_strategy_close(struct zfcp_erp_action *erp_action)
{
	int retval = zfcp_fsf_close_unit(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_UNIT_CLOSING;
	if (retval)
		return ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static int zfcp_erp_unit_strategy_open(struct zfcp_erp_action *erp_action)
{
	int retval = zfcp_fsf_open_unit(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_UNIT_OPENING;
	if (retval)
		return  ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static int zfcp_erp_unit_strategy(struct zfcp_erp_action *erp_action)
{
	struct zfcp_unit *unit = erp_action->unit;

	switch (erp_action->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
		zfcp_erp_unit_strategy_clearstati(unit);
		if (atomic_read(&unit->status) & ZFCP_STATUS_COMMON_OPEN)
			return zfcp_erp_unit_strategy_close(erp_action);
		/* already closed, fall through */
	case ZFCP_ERP_STEP_UNIT_CLOSING:
		if (atomic_read(&unit->status) & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_FAILED;
		if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
			return ZFCP_ERP_EXIT;
		return zfcp_erp_unit_strategy_open(erp_action);

	case ZFCP_ERP_STEP_UNIT_OPENING:
		if (atomic_read(&unit->status) & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_SUCCEEDED;
	}
	return ZFCP_ERP_FAILED;
}

static int zfcp_erp_strategy_check_unit(struct zfcp_unit *unit, int result)
{
	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&unit->erp_counter, 0);
		zfcp_erp_unit_unblock(unit);
		break;
	case ZFCP_ERP_FAILED :
		atomic_inc(&unit->erp_counter);
		if (atomic_read(&unit->erp_counter) > ZFCP_MAX_ERPS) {
			dev_err(&unit->port->adapter->ccw_device->dev,
				"ERP failed for unit 0x%016Lx on "
				"port 0x%016Lx\n",
				(unsigned long long)unit->fcp_lun,
				(unsigned long long)unit->port->wwpn);
			zfcp_erp_unit_failed(unit, "erusck1", NULL);
		}
		break;
	}

	if (atomic_read(&unit->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_unit_block(unit, 0);
		result = ZFCP_ERP_EXIT;
	}
	return result;
}

static int zfcp_erp_strategy_check_port(struct zfcp_port *port, int result)
{
	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&port->erp_counter, 0);
		zfcp_erp_port_unblock(port);
		break;

	case ZFCP_ERP_FAILED :
		if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_NOESC) {
			zfcp_erp_port_block(port, 0);
			result = ZFCP_ERP_EXIT;
		}
		atomic_inc(&port->erp_counter);
		if (atomic_read(&port->erp_counter) > ZFCP_MAX_ERPS) {
			dev_err(&port->adapter->ccw_device->dev,
				"ERP failed for remote port 0x%016Lx\n",
				(unsigned long long)port->wwpn);
			zfcp_erp_port_failed(port, "erpsck1", NULL);
		}
		break;
	}

	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_port_block(port, 0);
		result = ZFCP_ERP_EXIT;
	}
	return result;
}

static int zfcp_erp_strategy_check_adapter(struct zfcp_adapter *adapter,
					   int result)
{
	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&adapter->erp_counter, 0);
		zfcp_erp_adapter_unblock(adapter);
		break;

	case ZFCP_ERP_FAILED :
		atomic_inc(&adapter->erp_counter);
		if (atomic_read(&adapter->erp_counter) > ZFCP_MAX_ERPS) {
			dev_err(&adapter->ccw_device->dev,
				"ERP cannot recover an error "
				"on the FCP device\n");
			zfcp_erp_adapter_failed(adapter, "erasck1", NULL);
		}
		break;
	}

	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_adapter_block(adapter, 0);
		result = ZFCP_ERP_EXIT;
	}
	return result;
}

static int zfcp_erp_strategy_check_target(struct zfcp_erp_action *erp_action,
					  int result)
{
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_port *port = erp_action->port;
	struct zfcp_unit *unit = erp_action->unit;

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

static int zfcp_erp_strat_change_det(atomic_t *target_status, u32 erp_status)
{
	int status = atomic_read(target_status);

	if ((status & ZFCP_STATUS_COMMON_RUNNING) &&
	    (erp_status & ZFCP_STATUS_ERP_CLOSE_ONLY))
		return 1; /* take it online */

	if (!(status & ZFCP_STATUS_COMMON_RUNNING) &&
	    !(erp_status & ZFCP_STATUS_ERP_CLOSE_ONLY))
		return 1; /* take it offline */

	return 0;
}

static int zfcp_erp_strategy_statechange(struct zfcp_erp_action *act, int ret)
{
	int action = act->action;
	struct zfcp_adapter *adapter = act->adapter;
	struct zfcp_port *port = act->port;
	struct zfcp_unit *unit = act->unit;
	u32 erp_status = act->status;

	switch (action) {
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (zfcp_erp_strat_change_det(&adapter->status, erp_status)) {
			_zfcp_erp_adapter_reopen(adapter,
						 ZFCP_STATUS_COMMON_ERP_FAILED,
						 "ersscg1", NULL);
			return ZFCP_ERP_EXIT;
		}
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if (zfcp_erp_strat_change_det(&port->status, erp_status)) {
			_zfcp_erp_port_reopen(port,
					      ZFCP_STATUS_COMMON_ERP_FAILED,
					      "ersscg2", NULL);
			return ZFCP_ERP_EXIT;
		}
		break;

	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		if (zfcp_erp_strat_change_det(&unit->status, erp_status)) {
			_zfcp_erp_unit_reopen(unit,
					      ZFCP_STATUS_COMMON_ERP_FAILED,
					      "ersscg3", NULL);
			return ZFCP_ERP_EXIT;
		}
		break;
	}
	return ret;
}

static void zfcp_erp_action_dequeue(struct zfcp_erp_action *erp_action)
{
	struct zfcp_adapter *adapter = erp_action->adapter;

	adapter->erp_total_count--;
	if (erp_action->status & ZFCP_STATUS_ERP_LOWMEM) {
		adapter->erp_low_mem_count--;
		erp_action->status &= ~ZFCP_STATUS_ERP_LOWMEM;
	}

	list_del(&erp_action->list);
	zfcp_dbf_rec_action("eractd1", erp_action);

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
	}
}

static void zfcp_erp_action_cleanup(struct zfcp_erp_action *act, int result)
{
	struct zfcp_adapter *adapter = act->adapter;
	struct zfcp_port *port = act->port;
	struct zfcp_unit *unit = act->unit;

	switch (act->action) {
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		if ((result == ZFCP_ERP_SUCCEEDED) && !unit->device) {
			get_device(&unit->dev);
			if (scsi_queue_work(unit->port->adapter->scsi_host,
					    &unit->scsi_work) <= 0)
				put_device(&unit->dev);
		}
		put_device(&unit->dev);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if (result == ZFCP_ERP_SUCCEEDED)
			zfcp_scsi_schedule_rport_register(port);
		put_device(&port->dev);
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (result == ZFCP_ERP_SUCCEEDED) {
			register_service_level(&adapter->service_level);
			queue_work(adapter->work_queue, &adapter->scan_work);
		} else
			unregister_service_level(&adapter->service_level);
		kref_put(&adapter->ref, zfcp_adapter_release);
		break;
	}
}

static int zfcp_erp_strategy_do_action(struct zfcp_erp_action *erp_action)
{
	switch (erp_action->action) {
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		return zfcp_erp_adapter_strategy(erp_action);
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		return zfcp_erp_port_forced_strategy(erp_action);
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		return zfcp_erp_port_strategy(erp_action);
	case ZFCP_ERP_ACTION_REOPEN_UNIT:
		return zfcp_erp_unit_strategy(erp_action);
	}
	return ZFCP_ERP_FAILED;
}

static int zfcp_erp_strategy(struct zfcp_erp_action *erp_action)
{
	int retval;
	unsigned long flags;
	struct zfcp_adapter *adapter = erp_action->adapter;

	kref_get(&adapter->ref);

	write_lock_irqsave(&adapter->erp_lock, flags);
	zfcp_erp_strategy_check_fsfreq(erp_action);

	if (erp_action->status & ZFCP_STATUS_ERP_DISMISSED) {
		zfcp_erp_action_dequeue(erp_action);
		retval = ZFCP_ERP_DISMISSED;
		goto unlock;
	}

	zfcp_erp_action_to_running(erp_action);

	/* no lock to allow for blocking operations */
	write_unlock_irqrestore(&adapter->erp_lock, flags);
	retval = zfcp_erp_strategy_do_action(erp_action);
	write_lock_irqsave(&adapter->erp_lock, flags);

	if (erp_action->status & ZFCP_STATUS_ERP_DISMISSED)
		retval = ZFCP_ERP_CONTINUES;

	switch (retval) {
	case ZFCP_ERP_NOMEM:
		if (!(erp_action->status & ZFCP_STATUS_ERP_LOWMEM)) {
			++adapter->erp_low_mem_count;
			erp_action->status |= ZFCP_STATUS_ERP_LOWMEM;
		}
		if (adapter->erp_total_count == adapter->erp_low_mem_count)
			_zfcp_erp_adapter_reopen(adapter, 0, "erstgy1", NULL);
		else {
			zfcp_erp_strategy_memwait(erp_action);
			retval = ZFCP_ERP_CONTINUES;
		}
		goto unlock;

	case ZFCP_ERP_CONTINUES:
		if (erp_action->status & ZFCP_STATUS_ERP_LOWMEM) {
			--adapter->erp_low_mem_count;
			erp_action->status &= ~ZFCP_STATUS_ERP_LOWMEM;
		}
		goto unlock;
	}

	retval = zfcp_erp_strategy_check_target(erp_action, retval);
	zfcp_erp_action_dequeue(erp_action);
	retval = zfcp_erp_strategy_statechange(erp_action, retval);
	if (retval == ZFCP_ERP_EXIT)
		goto unlock;
	if (retval == ZFCP_ERP_SUCCEEDED)
		zfcp_erp_strategy_followup_success(erp_action);
	if (retval == ZFCP_ERP_FAILED)
		zfcp_erp_strategy_followup_failed(erp_action);

 unlock:
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	if (retval != ZFCP_ERP_CONTINUES)
		zfcp_erp_action_cleanup(erp_action, retval);

	kref_put(&adapter->ref, zfcp_adapter_release);
	return retval;
}

static int zfcp_erp_thread(void *data)
{
	struct zfcp_adapter *adapter = (struct zfcp_adapter *) data;
	struct list_head *next;
	struct zfcp_erp_action *act;
	unsigned long flags;

	for (;;) {
		zfcp_dbf_rec_thread_lock("erthrd1", adapter->dbf);
		wait_event_interruptible(adapter->erp_ready_wq,
			   !list_empty(&adapter->erp_ready_head) ||
			   kthread_should_stop());
		zfcp_dbf_rec_thread_lock("erthrd2", adapter->dbf);

		if (kthread_should_stop())
			break;

		write_lock_irqsave(&adapter->erp_lock, flags);
		next = adapter->erp_ready_head.next;
		write_unlock_irqrestore(&adapter->erp_lock, flags);

		if (next != &adapter->erp_ready_head) {
			act = list_entry(next, struct zfcp_erp_action, list);

			/* there is more to come after dismission, no notify */
			if (zfcp_erp_strategy(act) != ZFCP_ERP_DISMISSED)
				zfcp_erp_wakeup(adapter);
		}
	}

	return 0;
}

/**
 * zfcp_erp_thread_setup - Start ERP thread for adapter
 * @adapter: Adapter to start the ERP thread for
 *
 * Returns 0 on success or error code from kernel_thread()
 */
int zfcp_erp_thread_setup(struct zfcp_adapter *adapter)
{
	struct task_struct *thread;

	thread = kthread_run(zfcp_erp_thread, adapter, "zfcperp%s",
			     dev_name(&adapter->ccw_device->dev));
	if (IS_ERR(thread)) {
		dev_err(&adapter->ccw_device->dev,
			"Creating an ERP thread for the FCP device failed.\n");
		return PTR_ERR(thread);
	}

	adapter->erp_thread = thread;
	return 0;
}

/**
 * zfcp_erp_thread_kill - Stop ERP thread.
 * @adapter: Adapter where the ERP thread should be stopped.
 *
 * The caller of this routine ensures that the specified adapter has
 * been shut down and that this operation has been completed. Thus,
 * there are no pending erp_actions which would need to be handled
 * here.
 */
void zfcp_erp_thread_kill(struct zfcp_adapter *adapter)
{
	kthread_stop(adapter->erp_thread);
	adapter->erp_thread = NULL;
	WARN_ON(!list_empty(&adapter->erp_ready_head));
	WARN_ON(!list_empty(&adapter->erp_running_head));
}

/**
 * zfcp_erp_adapter_failed - Set adapter status to failed.
 * @adapter: Failed adapter.
 * @id: Event id for debug trace.
 * @ref: Reference for debug trace.
 */
void zfcp_erp_adapter_failed(struct zfcp_adapter *adapter, char *id, void *ref)
{
	zfcp_erp_modify_adapter_status(adapter, id, ref,
				       ZFCP_STATUS_COMMON_ERP_FAILED, ZFCP_SET);
}

/**
 * zfcp_erp_port_failed - Set port status to failed.
 * @port: Failed port.
 * @id: Event id for debug trace.
 * @ref: Reference for debug trace.
 */
void zfcp_erp_port_failed(struct zfcp_port *port, char *id, void *ref)
{
	zfcp_erp_modify_port_status(port, id, ref,
				    ZFCP_STATUS_COMMON_ERP_FAILED, ZFCP_SET);
}

/**
 * zfcp_erp_unit_failed - Set unit status to failed.
 * @unit: Failed unit.
 * @id: Event id for debug trace.
 * @ref: Reference for debug trace.
 */
void zfcp_erp_unit_failed(struct zfcp_unit *unit, char *id, void *ref)
{
	zfcp_erp_modify_unit_status(unit, id, ref,
				    ZFCP_STATUS_COMMON_ERP_FAILED, ZFCP_SET);
}

/**
 * zfcp_erp_wait - wait for completion of error recovery on an adapter
 * @adapter: adapter for which to wait for completion of its error recovery
 */
void zfcp_erp_wait(struct zfcp_adapter *adapter)
{
	wait_event(adapter->erp_done_wqh,
		   !(atomic_read(&adapter->status) &
			ZFCP_STATUS_ADAPTER_ERP_PENDING));
}

/**
 * zfcp_erp_modify_adapter_status - change adapter status bits
 * @adapter: adapter to change the status
 * @id: id for the debug trace
 * @ref: reference for the debug trace
 * @mask: status bits to change
 * @set_or_clear: ZFCP_SET or ZFCP_CLEAR
 *
 * Changes in common status bits are propagated to attached ports and units.
 */
void zfcp_erp_modify_adapter_status(struct zfcp_adapter *adapter, char *id,
				    void *ref, u32 mask, int set_or_clear)
{
	struct zfcp_port *port;
	unsigned long flags;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;

	if (set_or_clear == ZFCP_SET) {
		if (status_change_set(mask, &adapter->status))
			zfcp_dbf_rec_adapter(id, ref, adapter->dbf);
		atomic_set_mask(mask, &adapter->status);
	} else {
		if (status_change_clear(mask, &adapter->status))
			zfcp_dbf_rec_adapter(id, ref, adapter->dbf);
		atomic_clear_mask(mask, &adapter->status);
		if (mask & ZFCP_STATUS_COMMON_ERP_FAILED)
			atomic_set(&adapter->erp_counter, 0);
	}

	if (common_mask) {
		read_lock_irqsave(&adapter->port_list_lock, flags);
		list_for_each_entry(port, &adapter->port_list, list)
			zfcp_erp_modify_port_status(port, id, ref, common_mask,
						    set_or_clear);
		read_unlock_irqrestore(&adapter->port_list_lock, flags);
	}
}

/**
 * zfcp_erp_modify_port_status - change port status bits
 * @port: port to change the status bits
 * @id: id for the debug trace
 * @ref: reference for the debug trace
 * @mask: status bits to change
 * @set_or_clear: ZFCP_SET or ZFCP_CLEAR
 *
 * Changes in common status bits are propagated to attached units.
 */
void zfcp_erp_modify_port_status(struct zfcp_port *port, char *id, void *ref,
				 u32 mask, int set_or_clear)
{
	struct zfcp_unit *unit;
	unsigned long flags;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;

	if (set_or_clear == ZFCP_SET) {
		if (status_change_set(mask, &port->status))
			zfcp_dbf_rec_port(id, ref, port);
		atomic_set_mask(mask, &port->status);
	} else {
		if (status_change_clear(mask, &port->status))
			zfcp_dbf_rec_port(id, ref, port);
		atomic_clear_mask(mask, &port->status);
		if (mask & ZFCP_STATUS_COMMON_ERP_FAILED)
			atomic_set(&port->erp_counter, 0);
	}

	if (common_mask) {
		read_lock_irqsave(&port->unit_list_lock, flags);
		list_for_each_entry(unit, &port->unit_list, list)
			zfcp_erp_modify_unit_status(unit, id, ref, common_mask,
						    set_or_clear);
		read_unlock_irqrestore(&port->unit_list_lock, flags);
	}
}

/**
 * zfcp_erp_modify_unit_status - change unit status bits
 * @unit: unit to change the status bits
 * @id: id for the debug trace
 * @ref: reference for the debug trace
 * @mask: status bits to change
 * @set_or_clear: ZFCP_SET or ZFCP_CLEAR
 */
void zfcp_erp_modify_unit_status(struct zfcp_unit *unit, char *id, void *ref,
				 u32 mask, int set_or_clear)
{
	if (set_or_clear == ZFCP_SET) {
		if (status_change_set(mask, &unit->status))
			zfcp_dbf_rec_unit(id, ref, unit);
		atomic_set_mask(mask, &unit->status);
	} else {
		if (status_change_clear(mask, &unit->status))
			zfcp_dbf_rec_unit(id, ref, unit);
		atomic_clear_mask(mask, &unit->status);
		if (mask & ZFCP_STATUS_COMMON_ERP_FAILED) {
			atomic_set(&unit->erp_counter, 0);
		}
	}
}

/**
 * zfcp_erp_port_boxed - Mark port as "boxed" and start ERP
 * @port: The "boxed" port.
 * @id: The debug trace id.
 * @id: Reference for the debug trace.
 */
void zfcp_erp_port_boxed(struct zfcp_port *port, char *id, void *ref)
{
	zfcp_erp_modify_port_status(port, id, ref,
				    ZFCP_STATUS_COMMON_ACCESS_BOXED, ZFCP_SET);
	zfcp_erp_port_reopen(port, ZFCP_STATUS_COMMON_ERP_FAILED, id, ref);
}

/**
 * zfcp_erp_unit_boxed - Mark unit as "boxed" and start ERP
 * @port: The "boxed" unit.
 * @id: The debug trace id.
 * @id: Reference for the debug trace.
 */
void zfcp_erp_unit_boxed(struct zfcp_unit *unit, char *id, void *ref)
{
	zfcp_erp_modify_unit_status(unit, id, ref,
				    ZFCP_STATUS_COMMON_ACCESS_BOXED, ZFCP_SET);
	zfcp_erp_unit_reopen(unit, ZFCP_STATUS_COMMON_ERP_FAILED, id, ref);
}

/**
 * zfcp_erp_port_access_denied - Adapter denied access to port.
 * @port: port where access has been denied
 * @id: id for debug trace
 * @ref: reference for debug trace
 *
 * Since the adapter has denied access, stop using the port and the
 * attached units.
 */
void zfcp_erp_port_access_denied(struct zfcp_port *port, char *id, void *ref)
{
	zfcp_erp_modify_port_status(port, id, ref,
				    ZFCP_STATUS_COMMON_ERP_FAILED |
				    ZFCP_STATUS_COMMON_ACCESS_DENIED, ZFCP_SET);
}

/**
 * zfcp_erp_unit_access_denied - Adapter denied access to unit.
 * @unit: unit where access has been denied
 * @id: id for debug trace
 * @ref: reference for debug trace
 *
 * Since the adapter has denied access, stop using the unit.
 */
void zfcp_erp_unit_access_denied(struct zfcp_unit *unit, char *id, void *ref)
{
	zfcp_erp_modify_unit_status(unit, id, ref,
				    ZFCP_STATUS_COMMON_ERP_FAILED |
				    ZFCP_STATUS_COMMON_ACCESS_DENIED, ZFCP_SET);
}

static void zfcp_erp_unit_access_changed(struct zfcp_unit *unit, char *id,
					 void *ref)
{
	int status = atomic_read(&unit->status);
	if (!(status & (ZFCP_STATUS_COMMON_ACCESS_DENIED |
			ZFCP_STATUS_COMMON_ACCESS_BOXED)))
		return;

	zfcp_erp_unit_reopen(unit, ZFCP_STATUS_COMMON_ERP_FAILED, id, ref);
}

static void zfcp_erp_port_access_changed(struct zfcp_port *port, char *id,
					 void *ref)
{
	struct zfcp_unit *unit;
	unsigned long flags;
	int status = atomic_read(&port->status);

	if (!(status & (ZFCP_STATUS_COMMON_ACCESS_DENIED |
			ZFCP_STATUS_COMMON_ACCESS_BOXED))) {
		read_lock_irqsave(&port->unit_list_lock, flags);
		list_for_each_entry(unit, &port->unit_list, list)
				    zfcp_erp_unit_access_changed(unit, id, ref);
		read_unlock_irqrestore(&port->unit_list_lock, flags);
		return;
	}

	zfcp_erp_port_reopen(port, ZFCP_STATUS_COMMON_ERP_FAILED, id, ref);
}

/**
 * zfcp_erp_adapter_access_changed - Process change in adapter ACT
 * @adapter: Adapter where the Access Control Table (ACT) changed
 * @id: Id for debug trace
 * @ref: Reference for debug trace
 */
void zfcp_erp_adapter_access_changed(struct zfcp_adapter *adapter, char *id,
				     void *ref)
{
	unsigned long flags;
	struct zfcp_port *port;

	if (adapter->connection_features & FSF_FEATURE_NPIV_MODE)
		return;

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list)
		zfcp_erp_port_access_changed(port, id, ref);
	read_unlock_irqrestore(&adapter->port_list_lock, flags);
}
