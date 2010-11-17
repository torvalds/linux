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
	ZFCP_STATUS_ERP_NO_REF		= 0x00800000,
};

enum zfcp_erp_steps {
	ZFCP_ERP_STEP_UNINITIALIZED	= 0x0000,
	ZFCP_ERP_STEP_FSF_XCONFIG	= 0x0001,
	ZFCP_ERP_STEP_PHYS_PORT_CLOSING	= 0x0010,
	ZFCP_ERP_STEP_PORT_CLOSING	= 0x0100,
	ZFCP_ERP_STEP_PORT_OPENING	= 0x0800,
	ZFCP_ERP_STEP_LUN_CLOSING	= 0x1000,
	ZFCP_ERP_STEP_LUN_OPENING	= 0x2000,
};

enum zfcp_erp_act_type {
	ZFCP_ERP_ACTION_REOPEN_LUN         = 1,
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
	zfcp_erp_clear_adapter_status(adapter,
				       ZFCP_STATUS_COMMON_UNBLOCKED | mask);
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

static void zfcp_erp_action_dismiss_lun(struct scsi_device *sdev)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_ERP_INUSE)
		zfcp_erp_action_dismiss(&zfcp_sdev->erp_action);
}

static void zfcp_erp_action_dismiss_port(struct zfcp_port *port)
{
	struct scsi_device *sdev;

	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_INUSE)
		zfcp_erp_action_dismiss(&port->erp_action);
	else
		shost_for_each_device(sdev, port->adapter->scsi_host)
			if (sdev_to_zfcp(sdev)->port == port)
				zfcp_erp_action_dismiss_lun(sdev);
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
				 struct scsi_device *sdev)
{
	int need = want;
	int l_status, p_status, a_status;
	struct zfcp_scsi_dev *zfcp_sdev;

	switch (want) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zfcp_sdev = sdev_to_zfcp(sdev);
		l_status = atomic_read(&zfcp_sdev->status);
		if (l_status & ZFCP_STATUS_COMMON_ERP_INUSE)
			return 0;
		p_status = atomic_read(&port->status);
		if (!(p_status & ZFCP_STATUS_COMMON_RUNNING) ||
		      p_status & ZFCP_STATUS_COMMON_ERP_FAILED)
			return 0;
		if (!(p_status & ZFCP_STATUS_COMMON_UNBLOCKED))
			need = ZFCP_ERP_ACTION_REOPEN_PORT;
		/* fall through */
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		p_status = atomic_read(&port->status);
		if (!(p_status & ZFCP_STATUS_COMMON_OPEN))
			need = ZFCP_ERP_ACTION_REOPEN_PORT;
		/* fall through */
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		p_status = atomic_read(&port->status);
		if (p_status & ZFCP_STATUS_COMMON_ERP_INUSE)
			return 0;
		a_status = atomic_read(&adapter->status);
		if (!(a_status & ZFCP_STATUS_COMMON_RUNNING) ||
		      a_status & ZFCP_STATUS_COMMON_ERP_FAILED)
			return 0;
		if (p_status & ZFCP_STATUS_COMMON_NOESC)
			return need;
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

static struct zfcp_erp_action *zfcp_erp_setup_act(int need, u32 act_status,
						  struct zfcp_adapter *adapter,
						  struct zfcp_port *port,
						  struct scsi_device *sdev)
{
	struct zfcp_erp_action *erp_action;
	struct zfcp_scsi_dev *zfcp_sdev;

	switch (need) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zfcp_sdev = sdev_to_zfcp(sdev);
		if (!(act_status & ZFCP_STATUS_ERP_NO_REF))
			if (scsi_device_get(sdev))
				return NULL;
		atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE,
				&zfcp_sdev->status);
		erp_action = &zfcp_sdev->erp_action;
		if (!(atomic_read(&zfcp_sdev->status) &
		      ZFCP_STATUS_COMMON_RUNNING))
			act_status |= ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT:
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		if (!get_device(&port->dev))
			return NULL;
		zfcp_erp_action_dismiss_port(port);
		atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &port->status);
		erp_action = &port->erp_action;
		if (!(atomic_read(&port->status) & ZFCP_STATUS_COMMON_RUNNING))
			act_status |= ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		kref_get(&adapter->ref);
		zfcp_erp_action_dismiss_adapter(adapter);
		atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &adapter->status);
		erp_action = &adapter->erp_action;
		if (!(atomic_read(&adapter->status) &
		      ZFCP_STATUS_COMMON_RUNNING))
			act_status |= ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	default:
		return NULL;
	}

	memset(erp_action, 0, sizeof(struct zfcp_erp_action));
	erp_action->adapter = adapter;
	erp_action->port = port;
	erp_action->sdev = sdev;
	erp_action->action = need;
	erp_action->status = act_status;

	return erp_action;
}

static int zfcp_erp_action_enqueue(int want, struct zfcp_adapter *adapter,
				   struct zfcp_port *port,
				   struct scsi_device *sdev,
				   char *id, void *ref, u32 act_status)
{
	int retval = 1, need;
	struct zfcp_erp_action *act = NULL;

	if (!adapter->erp_thread)
		return -EIO;

	need = zfcp_erp_required_act(want, adapter, port, sdev);
	if (!need)
		goto out;

	act = zfcp_erp_setup_act(need, act_status, adapter, port, sdev);
	if (!act)
		goto out;
	atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_PENDING, &adapter->status);
	++adapter->erp_total_count;
	list_add_tail(&act->list, &adapter->erp_ready_head);
	wake_up(&adapter->erp_ready_wq);
	zfcp_dbf_rec_thread("eracte1", adapter->dbf);
	retval = 0;
 out:
	zfcp_dbf_rec_trigger(id, ref, want, need, act, adapter, port, sdev);
	return retval;
}

static int _zfcp_erp_adapter_reopen(struct zfcp_adapter *adapter,
				    int clear_mask, char *id, void *ref)
{
	zfcp_erp_adapter_block(adapter, clear_mask);
	zfcp_scsi_schedule_rports_block(adapter);

	/* ensure propagation of failed status to new devices */
	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_set_adapter_status(adapter,
					    ZFCP_STATUS_COMMON_ERP_FAILED);
		return -EIO;
	}
	return zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_ADAPTER,
				       adapter, NULL, NULL, id, ref, 0);
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
		zfcp_erp_set_adapter_status(adapter,
					    ZFCP_STATUS_COMMON_ERP_FAILED);
	else
		zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_ADAPTER, adapter,
					NULL, NULL, id, ref, 0);
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

static void zfcp_erp_port_block(struct zfcp_port *port, int clear)
{
	zfcp_erp_clear_port_status(port,
				    ZFCP_STATUS_COMMON_UNBLOCKED | clear);
}

static void _zfcp_erp_port_forced_reopen(struct zfcp_port *port,
					 int clear, char *id, void *ref)
{
	zfcp_erp_port_block(port, clear);
	zfcp_scsi_schedule_rport_block(port);

	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_FAILED)
		return;

	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_PORT_FORCED,
				port->adapter, port, NULL, id, ref, 0);
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
		zfcp_erp_set_port_status(port, ZFCP_STATUS_COMMON_ERP_FAILED);
		return -EIO;
	}

	return zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_PORT,
				       port->adapter, port, NULL, id, ref, 0);
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

static void zfcp_erp_lun_block(struct scsi_device *sdev, int clear_mask)
{
	zfcp_erp_clear_lun_status(sdev,
				  ZFCP_STATUS_COMMON_UNBLOCKED | clear_mask);
}

static void _zfcp_erp_lun_reopen(struct scsi_device *sdev, int clear, char *id,
				 void *ref, u32 act_status)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_adapter *adapter = zfcp_sdev->port->adapter;

	zfcp_erp_lun_block(sdev, clear);

	if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_ERP_FAILED)
		return;

	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_LUN, adapter,
				zfcp_sdev->port, sdev, id, ref, act_status);
}

/**
 * zfcp_erp_lun_reopen - initiate reopen of a LUN
 * @sdev: SCSI device / LUN to be reopened
 * @clear_mask: specifies flags in LUN status to be cleared
 * Return: 0 on success, < 0 on error
 */
void zfcp_erp_lun_reopen(struct scsi_device *sdev, int clear, char *id,
			 void *ref)
{
	unsigned long flags;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_port *port = zfcp_sdev->port;
	struct zfcp_adapter *adapter = port->adapter;

	write_lock_irqsave(&adapter->erp_lock, flags);
	_zfcp_erp_lun_reopen(sdev, clear, id, ref, 0);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

/**
 * zfcp_erp_lun_shutdown - Shutdown LUN
 * @sdev: SCSI device / LUN to shut down.
 * @clear: Status flags to clear.
 * @id: Id for debug trace event.
 * @ref: Reference for debug trace event.
 */
void zfcp_erp_lun_shutdown(struct scsi_device *sdev, int clear, char *id,
			   void *ref)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_lun_reopen(sdev, clear | flags, id, ref);
}

/**
 * zfcp_erp_lun_shutdown_wait - Shutdown LUN and wait for erp completion
 * @sdev: SCSI device / LUN to shut down.
 * @id: Id for debug trace event.
 *
 * Do not acquire a reference for the LUN when creating the ERP
 * action. It is safe, because this function waits for the ERP to
 * complete first. This allows to shutdown the LUN, even when the SCSI
 * device is in the state SDEV_DEL when scsi_device_get will fail.
 */
void zfcp_erp_lun_shutdown_wait(struct scsi_device *sdev, char *id)
{
	unsigned long flags;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_port *port = zfcp_sdev->port;
	struct zfcp_adapter *adapter = port->adapter;
	int clear = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;

	write_lock_irqsave(&adapter->erp_lock, flags);
	_zfcp_erp_lun_reopen(sdev, clear, id, NULL, ZFCP_STATUS_ERP_NO_REF);
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	zfcp_erp_wait(adapter);
}

static int status_change_set(unsigned long mask, atomic_t *status)
{
	return (atomic_read(status) ^ mask) & mask;
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

static void zfcp_erp_lun_unblock(struct scsi_device *sdev)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	if (status_change_set(ZFCP_STATUS_COMMON_UNBLOCKED, &zfcp_sdev->status))
		zfcp_dbf_rec_lun("erlubl1", NULL, sdev);
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &zfcp_sdev->status);
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

static void _zfcp_erp_lun_reopen_all(struct zfcp_port *port, int clear,
				     char *id, void *ref)
{
	struct scsi_device *sdev;

	shost_for_each_device(sdev, port->adapter->scsi_host)
		if (sdev_to_zfcp(sdev)->port == port)
			_zfcp_erp_lun_reopen(sdev, clear, id, ref, 0);
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
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		_zfcp_erp_lun_reopen(act->sdev, 0, "ersff_4", NULL, 0);
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
		_zfcp_erp_lun_reopen_all(act->port, 0, "ersfs_3", NULL);
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

	if (mempool_resize(act->adapter->pool.status_read_data,
			   act->adapter->stat_read_buf_num, GFP_KERNEL))
		return ZFCP_ERP_FAILED;

	if (mempool_resize(act->adapter->pool.status_read_req,
			   act->adapter->stat_read_buf_num, GFP_KERNEL))
		return ZFCP_ERP_FAILED;

	atomic_set(&act->adapter->stat_miss, act->adapter->stat_read_buf_num);
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
	/* all ports and LUNs are closed */
	zfcp_erp_clear_adapter_status(adapter, ZFCP_STATUS_COMMON_OPEN);

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
		zfcp_erp_set_port_status(port, ZFCP_STATUS_COMMON_ERP_FAILED);
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
			return ZFCP_ERP_FAILED;
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

static void zfcp_erp_lun_strategy_clearstati(struct scsi_device *sdev)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	atomic_clear_mask(ZFCP_STATUS_COMMON_ACCESS_DENIED |
			  ZFCP_STATUS_LUN_SHARED | ZFCP_STATUS_LUN_READONLY,
			  &zfcp_sdev->status);
}

static int zfcp_erp_lun_strategy_close(struct zfcp_erp_action *erp_action)
{
	int retval = zfcp_fsf_close_lun(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_LUN_CLOSING;
	if (retval)
		return ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static int zfcp_erp_lun_strategy_open(struct zfcp_erp_action *erp_action)
{
	int retval = zfcp_fsf_open_lun(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_LUN_OPENING;
	if (retval)
		return  ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static int zfcp_erp_lun_strategy(struct zfcp_erp_action *erp_action)
{
	struct scsi_device *sdev = erp_action->sdev;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	switch (erp_action->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
		zfcp_erp_lun_strategy_clearstati(sdev);
		if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_OPEN)
			return zfcp_erp_lun_strategy_close(erp_action);
		/* already closed, fall through */
	case ZFCP_ERP_STEP_LUN_CLOSING:
		if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_FAILED;
		if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
			return ZFCP_ERP_EXIT;
		return zfcp_erp_lun_strategy_open(erp_action);

	case ZFCP_ERP_STEP_LUN_OPENING:
		if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_SUCCEEDED;
	}
	return ZFCP_ERP_FAILED;
}

static int zfcp_erp_strategy_check_lun(struct scsi_device *sdev, int result)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&zfcp_sdev->erp_counter, 0);
		zfcp_erp_lun_unblock(sdev);
		break;
	case ZFCP_ERP_FAILED :
		atomic_inc(&zfcp_sdev->erp_counter);
		if (atomic_read(&zfcp_sdev->erp_counter) > ZFCP_MAX_ERPS) {
			dev_err(&zfcp_sdev->port->adapter->ccw_device->dev,
				"ERP failed for LUN 0x%016Lx on "
				"port 0x%016Lx\n",
				(unsigned long long)zfcp_scsi_dev_lun(sdev),
				(unsigned long long)zfcp_sdev->port->wwpn);
			zfcp_erp_set_lun_status(sdev,
						ZFCP_STATUS_COMMON_ERP_FAILED);
		}
		break;
	}

	if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_lun_block(sdev, 0);
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
			zfcp_erp_set_port_status(port,
					 ZFCP_STATUS_COMMON_ERP_FAILED);
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
			zfcp_erp_set_adapter_status(adapter,
					    ZFCP_STATUS_COMMON_ERP_FAILED);
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
	struct scsi_device *sdev = erp_action->sdev;

	switch (erp_action->action) {

	case ZFCP_ERP_ACTION_REOPEN_LUN:
		result = zfcp_erp_strategy_check_lun(sdev, result);
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
	struct scsi_device *sdev = act->sdev;
	struct zfcp_scsi_dev *zfcp_sdev;
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

	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zfcp_sdev = sdev_to_zfcp(sdev);
		if (zfcp_erp_strat_change_det(&zfcp_sdev->status, erp_status)) {
			_zfcp_erp_lun_reopen(sdev,
					     ZFCP_STATUS_COMMON_ERP_FAILED,
					     "ersscg3", NULL, 0);
			return ZFCP_ERP_EXIT;
		}
		break;
	}
	return ret;
}

static void zfcp_erp_action_dequeue(struct zfcp_erp_action *erp_action)
{
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_scsi_dev *zfcp_sdev;

	adapter->erp_total_count--;
	if (erp_action->status & ZFCP_STATUS_ERP_LOWMEM) {
		adapter->erp_low_mem_count--;
		erp_action->status &= ~ZFCP_STATUS_ERP_LOWMEM;
	}

	list_del(&erp_action->list);
	zfcp_dbf_rec_action("eractd1", erp_action);

	switch (erp_action->action) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zfcp_sdev = sdev_to_zfcp(erp_action->sdev);
		atomic_clear_mask(ZFCP_STATUS_COMMON_ERP_INUSE,
				  &zfcp_sdev->status);
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
	struct scsi_device *sdev = act->sdev;

	switch (act->action) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		if (!(act->status & ZFCP_STATUS_ERP_NO_REF))
			scsi_device_put(sdev);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if (result == ZFCP_ERP_SUCCEEDED)
			zfcp_scsi_schedule_rport_register(port);
		/* fall through */
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
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
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		return zfcp_erp_lun_strategy(erp_action);
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

	if (erp_action->status & ZFCP_STATUS_ERP_TIMEDOUT) {
		retval = ZFCP_ERP_FAILED;
		goto check_target;
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

check_target:
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
 * zfcp_erp_set_adapter_status - set adapter status bits
 * @adapter: adapter to change the status
 * @mask: status bits to change
 *
 * Changes in common status bits are propagated to attached ports and LUNs.
 */
void zfcp_erp_set_adapter_status(struct zfcp_adapter *adapter, u32 mask)
{
	struct zfcp_port *port;
	struct scsi_device *sdev;
	unsigned long flags;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;

	atomic_set_mask(mask, &adapter->status);

	if (!common_mask)
		return;

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list)
		atomic_set_mask(common_mask, &port->status);
	read_unlock_irqrestore(&adapter->port_list_lock, flags);

	shost_for_each_device(sdev, adapter->scsi_host)
		atomic_set_mask(common_mask, &sdev_to_zfcp(sdev)->status);
}

/**
 * zfcp_erp_clear_adapter_status - clear adapter status bits
 * @adapter: adapter to change the status
 * @mask: status bits to change
 *
 * Changes in common status bits are propagated to attached ports and LUNs.
 */
void zfcp_erp_clear_adapter_status(struct zfcp_adapter *adapter, u32 mask)
{
	struct zfcp_port *port;
	struct scsi_device *sdev;
	unsigned long flags;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;
	u32 clear_counter = mask & ZFCP_STATUS_COMMON_ERP_FAILED;

	atomic_clear_mask(mask, &adapter->status);

	if (!common_mask)
		return;

	if (clear_counter)
		atomic_set(&adapter->erp_counter, 0);

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list) {
		atomic_clear_mask(common_mask, &port->status);
		if (clear_counter)
			atomic_set(&port->erp_counter, 0);
	}
	read_unlock_irqrestore(&adapter->port_list_lock, flags);

	shost_for_each_device(sdev, adapter->scsi_host) {
		atomic_clear_mask(common_mask, &sdev_to_zfcp(sdev)->status);
		if (clear_counter)
			atomic_set(&sdev_to_zfcp(sdev)->erp_counter, 0);
	}
}

/**
 * zfcp_erp_set_port_status - set port status bits
 * @port: port to change the status
 * @mask: status bits to change
 *
 * Changes in common status bits are propagated to attached LUNs.
 */
void zfcp_erp_set_port_status(struct zfcp_port *port, u32 mask)
{
	struct scsi_device *sdev;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;

	atomic_set_mask(mask, &port->status);

	if (!common_mask)
		return;

	shost_for_each_device(sdev, port->adapter->scsi_host)
		if (sdev_to_zfcp(sdev)->port == port)
			atomic_set_mask(common_mask,
					&sdev_to_zfcp(sdev)->status);
}

/**
 * zfcp_erp_clear_port_status - clear port status bits
 * @port: adapter to change the status
 * @mask: status bits to change
 *
 * Changes in common status bits are propagated to attached LUNs.
 */
void zfcp_erp_clear_port_status(struct zfcp_port *port, u32 mask)
{
	struct scsi_device *sdev;
	u32 common_mask = mask & ZFCP_COMMON_FLAGS;
	u32 clear_counter = mask & ZFCP_STATUS_COMMON_ERP_FAILED;

	atomic_clear_mask(mask, &port->status);

	if (!common_mask)
		return;

	if (clear_counter)
		atomic_set(&port->erp_counter, 0);

	shost_for_each_device(sdev, port->adapter->scsi_host)
		if (sdev_to_zfcp(sdev)->port == port) {
			atomic_clear_mask(common_mask,
					  &sdev_to_zfcp(sdev)->status);
			if (clear_counter)
				atomic_set(&sdev_to_zfcp(sdev)->erp_counter, 0);
		}
}

/**
 * zfcp_erp_set_lun_status - set lun status bits
 * @sdev: SCSI device / lun to set the status bits
 * @mask: status bits to change
 */
void zfcp_erp_set_lun_status(struct scsi_device *sdev, u32 mask)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	atomic_set_mask(mask, &zfcp_sdev->status);
}

/**
 * zfcp_erp_clear_lun_status - clear lun status bits
 * @sdev: SCSi device / lun to clear the status bits
 * @mask: status bits to change
 */
void zfcp_erp_clear_lun_status(struct scsi_device *sdev, u32 mask)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	atomic_clear_mask(mask, &zfcp_sdev->status);

	if (mask & ZFCP_STATUS_COMMON_ERP_FAILED)
		atomic_set(&zfcp_sdev->erp_counter, 0);
}

