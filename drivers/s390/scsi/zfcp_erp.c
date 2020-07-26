// SPDX-License-Identifier: GPL-2.0
/*
 * zfcp device driver
 *
 * Error Recovery Procedures (ERP).
 *
 * Copyright IBM Corp. 2002, 2020
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kthread.h>
#include <linux/bug.h>
#include "zfcp_ext.h"
#include "zfcp_reqlist.h"
#include "zfcp_diag.h"

#define ZFCP_MAX_ERPS                   3

enum zfcp_erp_act_flags {
	ZFCP_STATUS_ERP_TIMEDOUT	= 0x10000000,
	ZFCP_STATUS_ERP_CLOSE_ONLY	= 0x01000000,
	ZFCP_STATUS_ERP_DISMISSED	= 0x00200000,
	ZFCP_STATUS_ERP_LOWMEM		= 0x00400000,
	ZFCP_STATUS_ERP_NO_REF		= 0x00800000,
};

/*
 * Eyecatcher pseudo flag to bitwise or-combine with enum zfcp_erp_act_type.
 * Used to indicate that an ERP action could not be set up despite a detected
 * need for some recovery.
 */
#define ZFCP_ERP_ACTION_NONE		0xc0
/*
 * Eyecatcher pseudo flag to bitwise or-combine with enum zfcp_erp_act_type.
 * Used to indicate that ERP not needed because the object has
 * ZFCP_STATUS_COMMON_ERP_FAILED.
 */
#define ZFCP_ERP_ACTION_FAILED		0xe0

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

static bool zfcp_erp_action_is_running(struct zfcp_erp_action *act)
{
	struct zfcp_erp_action *curr_act;

	list_for_each_entry(curr_act, &act->adapter->erp_running_head, list)
		if (act == curr_act)
			return true;
	return false;
}

static void zfcp_erp_action_ready(struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;

	list_move(&act->list, &act->adapter->erp_ready_head);
	zfcp_dbf_rec_run("erardy1", act);
	wake_up(&adapter->erp_ready_wq);
	zfcp_dbf_rec_run("erardy2", act);
}

static void zfcp_erp_action_dismiss(struct zfcp_erp_action *act)
{
	act->status |= ZFCP_STATUS_ERP_DISMISSED;
	if (zfcp_erp_action_is_running(act))
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
	else {
		spin_lock(port->adapter->scsi_host->host_lock);
		__shost_for_each_device(sdev, port->adapter->scsi_host)
			if (sdev_to_zfcp(sdev)->port == port)
				zfcp_erp_action_dismiss_lun(sdev);
		spin_unlock(port->adapter->scsi_host->host_lock);
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

static enum zfcp_erp_act_type zfcp_erp_handle_failed(
	enum zfcp_erp_act_type want, struct zfcp_adapter *adapter,
	struct zfcp_port *port,	struct scsi_device *sdev)
{
	enum zfcp_erp_act_type need = want;
	struct zfcp_scsi_dev *zsdev;

	switch (want) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zsdev = sdev_to_zfcp(sdev);
		if (atomic_read(&zsdev->status) & ZFCP_STATUS_COMMON_ERP_FAILED)
			need = 0;
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_FAILED)
			need = 0;
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if (atomic_read(&port->status) &
		    ZFCP_STATUS_COMMON_ERP_FAILED) {
			need = 0;
			/* ensure propagation of failed status to new devices */
			zfcp_erp_set_port_status(
				port, ZFCP_STATUS_COMMON_ERP_FAILED);
		}
		break;
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (atomic_read(&adapter->status) &
		    ZFCP_STATUS_COMMON_ERP_FAILED) {
			need = 0;
			/* ensure propagation of failed status to new devices */
			zfcp_erp_set_adapter_status(
				adapter, ZFCP_STATUS_COMMON_ERP_FAILED);
		}
		break;
	}

	return need;
}

static enum zfcp_erp_act_type zfcp_erp_required_act(enum zfcp_erp_act_type want,
				 struct zfcp_adapter *adapter,
				 struct zfcp_port *port,
				 struct scsi_device *sdev)
{
	enum zfcp_erp_act_type need = want;
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
		fallthrough;
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		p_status = atomic_read(&port->status);
		if (!(p_status & ZFCP_STATUS_COMMON_OPEN))
			need = ZFCP_ERP_ACTION_REOPEN_PORT;
		fallthrough;
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
		fallthrough;
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

static struct zfcp_erp_action *zfcp_erp_setup_act(enum zfcp_erp_act_type need,
						  u32 act_status,
						  struct zfcp_adapter *adapter,
						  struct zfcp_port *port,
						  struct scsi_device *sdev)
{
	struct zfcp_erp_action *erp_action;
	struct zfcp_scsi_dev *zfcp_sdev;

	if (WARN_ON_ONCE(need != ZFCP_ERP_ACTION_REOPEN_LUN &&
			 need != ZFCP_ERP_ACTION_REOPEN_PORT &&
			 need != ZFCP_ERP_ACTION_REOPEN_PORT_FORCED &&
			 need != ZFCP_ERP_ACTION_REOPEN_ADAPTER))
		return NULL;

	switch (need) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zfcp_sdev = sdev_to_zfcp(sdev);
		if (!(act_status & ZFCP_STATUS_ERP_NO_REF))
			if (scsi_device_get(sdev))
				return NULL;
		atomic_or(ZFCP_STATUS_COMMON_ERP_INUSE,
				&zfcp_sdev->status);
		erp_action = &zfcp_sdev->erp_action;
		WARN_ON_ONCE(erp_action->port != port);
		WARN_ON_ONCE(erp_action->sdev != sdev);
		if (!(atomic_read(&zfcp_sdev->status) &
		      ZFCP_STATUS_COMMON_RUNNING))
			act_status |= ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT:
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		if (!get_device(&port->dev))
			return NULL;
		zfcp_erp_action_dismiss_port(port);
		atomic_or(ZFCP_STATUS_COMMON_ERP_INUSE, &port->status);
		erp_action = &port->erp_action;
		WARN_ON_ONCE(erp_action->port != port);
		WARN_ON_ONCE(erp_action->sdev != NULL);
		if (!(atomic_read(&port->status) & ZFCP_STATUS_COMMON_RUNNING))
			act_status |= ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		kref_get(&adapter->ref);
		zfcp_erp_action_dismiss_adapter(adapter);
		atomic_or(ZFCP_STATUS_COMMON_ERP_INUSE, &adapter->status);
		erp_action = &adapter->erp_action;
		WARN_ON_ONCE(erp_action->port != NULL);
		WARN_ON_ONCE(erp_action->sdev != NULL);
		if (!(atomic_read(&adapter->status) &
		      ZFCP_STATUS_COMMON_RUNNING))
			act_status |= ZFCP_STATUS_ERP_CLOSE_ONLY;
		break;
	}

	WARN_ON_ONCE(erp_action->adapter != adapter);
	memset(&erp_action->list, 0, sizeof(erp_action->list));
	memset(&erp_action->timer, 0, sizeof(erp_action->timer));
	erp_action->step = ZFCP_ERP_STEP_UNINITIALIZED;
	erp_action->fsf_req_id = 0;
	erp_action->type = need;
	erp_action->status = act_status;

	return erp_action;
}

static void zfcp_erp_action_enqueue(enum zfcp_erp_act_type want,
				    struct zfcp_adapter *adapter,
				    struct zfcp_port *port,
				    struct scsi_device *sdev,
				    char *dbftag, u32 act_status)
{
	enum zfcp_erp_act_type need;
	struct zfcp_erp_action *act;

	need = zfcp_erp_handle_failed(want, adapter, port, sdev);
	if (!need) {
		need = ZFCP_ERP_ACTION_FAILED; /* marker for trace */
		goto out;
	}

	if (!adapter->erp_thread) {
		need = ZFCP_ERP_ACTION_NONE; /* marker for trace */
		goto out;
	}

	need = zfcp_erp_required_act(want, adapter, port, sdev);
	if (!need)
		goto out;

	act = zfcp_erp_setup_act(need, act_status, adapter, port, sdev);
	if (!act) {
		need |= ZFCP_ERP_ACTION_NONE; /* marker for trace */
		goto out;
	}
	atomic_or(ZFCP_STATUS_ADAPTER_ERP_PENDING, &adapter->status);
	++adapter->erp_total_count;
	list_add_tail(&act->list, &adapter->erp_ready_head);
	wake_up(&adapter->erp_ready_wq);
 out:
	zfcp_dbf_rec_trig(dbftag, adapter, port, sdev, want, need);
}

void zfcp_erp_port_forced_no_port_dbf(char *dbftag,
				      struct zfcp_adapter *adapter,
				      u64 port_name, u32 port_id)
{
	unsigned long flags;
	static /* don't waste stack */ struct zfcp_port tmpport;

	write_lock_irqsave(&adapter->erp_lock, flags);
	/* Stand-in zfcp port with fields just good enough for
	 * zfcp_dbf_rec_trig() and zfcp_dbf_set_common().
	 * Under lock because tmpport is static.
	 */
	atomic_set(&tmpport.status, -1); /* unknown */
	tmpport.wwpn = port_name;
	tmpport.d_id = port_id;
	zfcp_dbf_rec_trig(dbftag, adapter, &tmpport, NULL,
			  ZFCP_ERP_ACTION_REOPEN_PORT_FORCED,
			  ZFCP_ERP_ACTION_NONE);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

static void _zfcp_erp_adapter_reopen(struct zfcp_adapter *adapter,
				    int clear_mask, char *dbftag)
{
	zfcp_erp_adapter_block(adapter, clear_mask);
	zfcp_scsi_schedule_rports_block(adapter);

	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_ADAPTER,
				adapter, NULL, NULL, dbftag, 0);
}

/**
 * zfcp_erp_adapter_reopen - Reopen adapter.
 * @adapter: Adapter to reopen.
 * @clear: Status flags to clear.
 * @dbftag: Tag for debug trace event.
 */
void zfcp_erp_adapter_reopen(struct zfcp_adapter *adapter, int clear,
			     char *dbftag)
{
	unsigned long flags;

	zfcp_erp_adapter_block(adapter, clear);
	zfcp_scsi_schedule_rports_block(adapter);

	write_lock_irqsave(&adapter->erp_lock, flags);
	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_ADAPTER, adapter,
				NULL, NULL, dbftag, 0);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

/**
 * zfcp_erp_adapter_shutdown - Shutdown adapter.
 * @adapter: Adapter to shut down.
 * @clear: Status flags to clear.
 * @dbftag: Tag for debug trace event.
 */
void zfcp_erp_adapter_shutdown(struct zfcp_adapter *adapter, int clear,
			       char *dbftag)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_adapter_reopen(adapter, clear | flags, dbftag);
}

/**
 * zfcp_erp_port_shutdown - Shutdown port
 * @port: Port to shut down.
 * @clear: Status flags to clear.
 * @dbftag: Tag for debug trace event.
 */
void zfcp_erp_port_shutdown(struct zfcp_port *port, int clear, char *dbftag)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_port_reopen(port, clear | flags, dbftag);
}

static void zfcp_erp_port_block(struct zfcp_port *port, int clear)
{
	zfcp_erp_clear_port_status(port,
				    ZFCP_STATUS_COMMON_UNBLOCKED | clear);
}

static void _zfcp_erp_port_forced_reopen(struct zfcp_port *port, int clear,
					 char *dbftag)
{
	zfcp_erp_port_block(port, clear);
	zfcp_scsi_schedule_rport_block(port);

	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_PORT_FORCED,
				port->adapter, port, NULL, dbftag, 0);
}

/**
 * zfcp_erp_port_forced_reopen - Forced close of port and open again
 * @port: Port to force close and to reopen.
 * @clear: Status flags to clear.
 * @dbftag: Tag for debug trace event.
 */
void zfcp_erp_port_forced_reopen(struct zfcp_port *port, int clear,
				 char *dbftag)
{
	unsigned long flags;
	struct zfcp_adapter *adapter = port->adapter;

	write_lock_irqsave(&adapter->erp_lock, flags);
	_zfcp_erp_port_forced_reopen(port, clear, dbftag);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

static void _zfcp_erp_port_reopen(struct zfcp_port *port, int clear,
				  char *dbftag)
{
	zfcp_erp_port_block(port, clear);
	zfcp_scsi_schedule_rport_block(port);

	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_PORT,
				port->adapter, port, NULL, dbftag, 0);
}

/**
 * zfcp_erp_port_reopen - trigger remote port recovery
 * @port: port to recover
 * @clear: flags in port status to be cleared
 * @dbftag: Tag for debug trace event.
 */
void zfcp_erp_port_reopen(struct zfcp_port *port, int clear, char *dbftag)
{
	unsigned long flags;
	struct zfcp_adapter *adapter = port->adapter;

	write_lock_irqsave(&adapter->erp_lock, flags);
	_zfcp_erp_port_reopen(port, clear, dbftag);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

static void zfcp_erp_lun_block(struct scsi_device *sdev, int clear_mask)
{
	zfcp_erp_clear_lun_status(sdev,
				  ZFCP_STATUS_COMMON_UNBLOCKED | clear_mask);
}

static void _zfcp_erp_lun_reopen(struct scsi_device *sdev, int clear,
				 char *dbftag, u32 act_status)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_adapter *adapter = zfcp_sdev->port->adapter;

	zfcp_erp_lun_block(sdev, clear);

	zfcp_erp_action_enqueue(ZFCP_ERP_ACTION_REOPEN_LUN, adapter,
				zfcp_sdev->port, sdev, dbftag, act_status);
}

/**
 * zfcp_erp_lun_reopen - initiate reopen of a LUN
 * @sdev: SCSI device / LUN to be reopened
 * @clear: specifies flags in LUN status to be cleared
 * @dbftag: Tag for debug trace event.
 *
 * Return: 0 on success, < 0 on error
 */
void zfcp_erp_lun_reopen(struct scsi_device *sdev, int clear, char *dbftag)
{
	unsigned long flags;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_port *port = zfcp_sdev->port;
	struct zfcp_adapter *adapter = port->adapter;

	write_lock_irqsave(&adapter->erp_lock, flags);
	_zfcp_erp_lun_reopen(sdev, clear, dbftag, 0);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

/**
 * zfcp_erp_lun_shutdown - Shutdown LUN
 * @sdev: SCSI device / LUN to shut down.
 * @clear: Status flags to clear.
 * @dbftag: Tag for debug trace event.
 */
void zfcp_erp_lun_shutdown(struct scsi_device *sdev, int clear, char *dbftag)
{
	int flags = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;
	zfcp_erp_lun_reopen(sdev, clear | flags, dbftag);
}

/**
 * zfcp_erp_lun_shutdown_wait - Shutdown LUN and wait for erp completion
 * @sdev: SCSI device / LUN to shut down.
 * @dbftag: Tag for debug trace event.
 *
 * Do not acquire a reference for the LUN when creating the ERP
 * action. It is safe, because this function waits for the ERP to
 * complete first. This allows to shutdown the LUN, even when the SCSI
 * device is in the state SDEV_DEL when scsi_device_get will fail.
 */
void zfcp_erp_lun_shutdown_wait(struct scsi_device *sdev, char *dbftag)
{
	unsigned long flags;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_port *port = zfcp_sdev->port;
	struct zfcp_adapter *adapter = port->adapter;
	int clear = ZFCP_STATUS_COMMON_RUNNING | ZFCP_STATUS_COMMON_ERP_FAILED;

	write_lock_irqsave(&adapter->erp_lock, flags);
	_zfcp_erp_lun_reopen(sdev, clear, dbftag, ZFCP_STATUS_ERP_NO_REF);
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	zfcp_erp_wait(adapter);
}

static int zfcp_erp_status_change_set(unsigned long mask, atomic_t *status)
{
	return (atomic_read(status) ^ mask) & mask;
}

static void zfcp_erp_adapter_unblock(struct zfcp_adapter *adapter)
{
	if (zfcp_erp_status_change_set(ZFCP_STATUS_COMMON_UNBLOCKED,
				       &adapter->status))
		zfcp_dbf_rec_run("eraubl1", &adapter->erp_action);
	atomic_or(ZFCP_STATUS_COMMON_UNBLOCKED, &adapter->status);
}

static void zfcp_erp_port_unblock(struct zfcp_port *port)
{
	if (zfcp_erp_status_change_set(ZFCP_STATUS_COMMON_UNBLOCKED,
				       &port->status))
		zfcp_dbf_rec_run("erpubl1", &port->erp_action);
	atomic_or(ZFCP_STATUS_COMMON_UNBLOCKED, &port->status);
}

static void zfcp_erp_lun_unblock(struct scsi_device *sdev)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	if (zfcp_erp_status_change_set(ZFCP_STATUS_COMMON_UNBLOCKED,
				       &zfcp_sdev->status))
		zfcp_dbf_rec_run("erlubl1", &sdev_to_zfcp(sdev)->erp_action);
	atomic_or(ZFCP_STATUS_COMMON_UNBLOCKED, &zfcp_sdev->status);
}

static void zfcp_erp_action_to_running(struct zfcp_erp_action *erp_action)
{
	list_move(&erp_action->list, &erp_action->adapter->erp_running_head);
	zfcp_dbf_rec_run("erator1", erp_action);
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
			zfcp_dbf_rec_run("erscf_1", act);
			/* lock-free concurrent access with
			 * zfcp_erp_timeout_handler()
			 */
			WRITE_ONCE(req->erp_action, NULL);
		}
		if (act->status & ZFCP_STATUS_ERP_TIMEDOUT)
			zfcp_dbf_rec_run("erscf_2", act);
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
	if (zfcp_erp_action_is_running(erp_action)) {
		erp_action->status |= set_mask;
		zfcp_erp_action_ready(erp_action);
	}
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

/**
 * zfcp_erp_timeout_handler - Trigger ERP action from timed out ERP request
 * @t: timer list entry embedded in zfcp FSF request
 */
void zfcp_erp_timeout_handler(struct timer_list *t)
{
	struct zfcp_fsf_req *fsf_req = from_timer(fsf_req, t, timer);
	struct zfcp_erp_action *act;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_DISMISSED)
		return;
	/* lock-free concurrent access with zfcp_erp_strategy_check_fsfreq() */
	act = READ_ONCE(fsf_req->erp_action);
	if (!act)
		return;
	zfcp_erp_notify(act, ZFCP_STATUS_ERP_TIMEDOUT);
}

static void zfcp_erp_memwait_handler(struct timer_list *t)
{
	struct zfcp_erp_action *act = from_timer(act, t, timer);

	zfcp_erp_notify(act, 0);
}

static void zfcp_erp_strategy_memwait(struct zfcp_erp_action *erp_action)
{
	timer_setup(&erp_action->timer, zfcp_erp_memwait_handler, 0);
	erp_action->timer.expires = jiffies + HZ;
	add_timer(&erp_action->timer);
}

void zfcp_erp_port_forced_reopen_all(struct zfcp_adapter *adapter,
				     int clear, char *dbftag)
{
	unsigned long flags;
	struct zfcp_port *port;

	write_lock_irqsave(&adapter->erp_lock, flags);
	read_lock(&adapter->port_list_lock);
	list_for_each_entry(port, &adapter->port_list, list)
		_zfcp_erp_port_forced_reopen(port, clear, dbftag);
	read_unlock(&adapter->port_list_lock);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

static void _zfcp_erp_port_reopen_all(struct zfcp_adapter *adapter,
				      int clear, char *dbftag)
{
	struct zfcp_port *port;

	read_lock(&adapter->port_list_lock);
	list_for_each_entry(port, &adapter->port_list, list)
		_zfcp_erp_port_reopen(port, clear, dbftag);
	read_unlock(&adapter->port_list_lock);
}

static void _zfcp_erp_lun_reopen_all(struct zfcp_port *port, int clear,
				     char *dbftag)
{
	struct scsi_device *sdev;

	spin_lock(port->adapter->scsi_host->host_lock);
	__shost_for_each_device(sdev, port->adapter->scsi_host)
		if (sdev_to_zfcp(sdev)->port == port)
			_zfcp_erp_lun_reopen(sdev, clear, dbftag, 0);
	spin_unlock(port->adapter->scsi_host->host_lock);
}

static void zfcp_erp_strategy_followup_failed(struct zfcp_erp_action *act)
{
	switch (act->type) {
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		_zfcp_erp_adapter_reopen(act->adapter, 0, "ersff_1");
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		_zfcp_erp_port_forced_reopen(act->port, 0, "ersff_2");
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		_zfcp_erp_port_reopen(act->port, 0, "ersff_3");
		break;
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		_zfcp_erp_lun_reopen(act->sdev, 0, "ersff_4", 0);
		break;
	}
}

static void zfcp_erp_strategy_followup_success(struct zfcp_erp_action *act)
{
	switch (act->type) {
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		_zfcp_erp_port_reopen_all(act->adapter, 0, "ersfs_1");
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		_zfcp_erp_port_reopen(act->port, 0, "ersfs_2");
		break;
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		_zfcp_erp_lun_reopen_all(act->port, 0, "ersfs_3");
		break;
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		/* NOP */
		break;
	}
}

static void zfcp_erp_wakeup(struct zfcp_adapter *adapter)
{
	unsigned long flags;

	read_lock_irqsave(&adapter->erp_lock, flags);
	if (list_empty(&adapter->erp_ready_head) &&
	    list_empty(&adapter->erp_running_head)) {
			atomic_andnot(ZFCP_STATUS_ADAPTER_ERP_PENDING,
					  &adapter->status);
			wake_up(&adapter->erp_done_wqh);
	}
	read_unlock_irqrestore(&adapter->erp_lock, flags);
}

static void zfcp_erp_enqueue_ptp_port(struct zfcp_adapter *adapter)
{
	struct zfcp_port *port;
	port = zfcp_port_enqueue(adapter, adapter->peer_wwpn, 0,
				 adapter->peer_d_id);
	if (IS_ERR(port)) /* error or port already attached */
		return;
	zfcp_erp_port_reopen(port, 0, "ereptp1");
}

static enum zfcp_erp_act_result zfcp_erp_adapter_strat_fsf_xconf(
	struct zfcp_erp_action *erp_action)
{
	int retries;
	int sleep = 1;
	struct zfcp_adapter *adapter = erp_action->adapter;

	atomic_andnot(ZFCP_STATUS_ADAPTER_XCONFIG_OK, &adapter->status);

	for (retries = 7; retries; retries--) {
		atomic_andnot(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
				  &adapter->status);
		write_lock_irq(&adapter->erp_lock);
		zfcp_erp_action_to_running(erp_action);
		write_unlock_irq(&adapter->erp_lock);
		if (zfcp_fsf_exchange_config_data(erp_action)) {
			atomic_andnot(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
					  &adapter->status);
			return ZFCP_ERP_FAILED;
		}

		wait_event(adapter->erp_ready_wq,
			   !list_empty(&adapter->erp_ready_head));
		if (erp_action->status & ZFCP_STATUS_ERP_TIMEDOUT)
			break;

		if (!(atomic_read(&adapter->status) &
		      ZFCP_STATUS_ADAPTER_HOST_CON_INIT))
			break;

		ssleep(sleep);
		sleep *= 2;
	}

	atomic_andnot(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
			  &adapter->status);

	if (!(atomic_read(&adapter->status) & ZFCP_STATUS_ADAPTER_XCONFIG_OK))
		return ZFCP_ERP_FAILED;

	return ZFCP_ERP_SUCCEEDED;
}

static void
zfcp_erp_adapter_strategy_open_ptp_port(struct zfcp_adapter *const adapter)
{
	if (fc_host_port_type(adapter->scsi_host) == FC_PORTTYPE_PTP)
		zfcp_erp_enqueue_ptp_port(adapter);
}

static enum zfcp_erp_act_result zfcp_erp_adapter_strategy_open_fsf_xport(
	struct zfcp_erp_action *act)
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

	zfcp_dbf_rec_run("erasox1", act);
	wait_event(adapter->erp_ready_wq,
		   !list_empty(&adapter->erp_ready_head));
	zfcp_dbf_rec_run("erasox2", act);
	if (act->status & ZFCP_STATUS_ERP_TIMEDOUT)
		return ZFCP_ERP_FAILED;

	return ZFCP_ERP_SUCCEEDED;
}

static enum zfcp_erp_act_result
zfcp_erp_adapter_strategy_alloc_shost(struct zfcp_adapter *const adapter)
{
	struct zfcp_diag_adapter_config_data *const config_data =
		&adapter->diagnostics->config_data;
	struct zfcp_diag_adapter_port_data *const port_data =
		&adapter->diagnostics->port_data;
	unsigned long flags;
	int rc;

	rc = zfcp_scsi_adapter_register(adapter);
	if (rc == -EEXIST)
		return ZFCP_ERP_SUCCEEDED;
	else if (rc)
		return ZFCP_ERP_FAILED;

	/*
	 * We allocated the shost for the first time. Before it was NULL,
	 * and so we deferred all updates in the xconf- and xport-data
	 * handlers. We need to make up for that now, and make all the updates
	 * that would have been done before.
	 *
	 * We can be sure that xconf- and xport-data succeeded, because
	 * otherwise this function is not called. But they might have been
	 * incomplete.
	 */

	spin_lock_irqsave(&config_data->header.access_lock, flags);
	zfcp_scsi_shost_update_config_data(adapter, &config_data->data,
					   !!config_data->header.incomplete);
	spin_unlock_irqrestore(&config_data->header.access_lock, flags);

	if (adapter->adapter_features & FSF_FEATURE_HBAAPI_MANAGEMENT) {
		spin_lock_irqsave(&port_data->header.access_lock, flags);
		zfcp_scsi_shost_update_port_data(adapter, &port_data->data);
		spin_unlock_irqrestore(&port_data->header.access_lock, flags);
	}

	/*
	 * There is a remote possibility that the 'Exchange Port Data' request
	 * reports a different connectivity status than 'Exchange Config Data'.
	 * But any change to the connectivity status of the local optic that
	 * happens after the initial xconf request is expected to be reported
	 * to us, as soon as we post Status Read Buffers to the FCP channel
	 * firmware after this function. So any resulting inconsistency will
	 * only be momentary.
	 */
	if (config_data->header.incomplete)
		zfcp_fsf_fc_host_link_down(adapter);

	return ZFCP_ERP_SUCCEEDED;
}

static enum zfcp_erp_act_result zfcp_erp_adapter_strategy_open_fsf(
	struct zfcp_erp_action *act)
{
	if (zfcp_erp_adapter_strat_fsf_xconf(act) == ZFCP_ERP_FAILED)
		return ZFCP_ERP_FAILED;

	if (zfcp_erp_adapter_strategy_open_fsf_xport(act) == ZFCP_ERP_FAILED)
		return ZFCP_ERP_FAILED;

	if (zfcp_erp_adapter_strategy_alloc_shost(act->adapter) ==
	    ZFCP_ERP_FAILED)
		return ZFCP_ERP_FAILED;

	zfcp_erp_adapter_strategy_open_ptp_port(act->adapter);

	if (mempool_resize(act->adapter->pool.sr_data,
			   act->adapter->stat_read_buf_num))
		return ZFCP_ERP_FAILED;

	if (mempool_resize(act->adapter->pool.status_read_req,
			   act->adapter->stat_read_buf_num))
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

	atomic_andnot(ZFCP_STATUS_ADAPTER_XCONFIG_OK |
			  ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED, &adapter->status);
}

static enum zfcp_erp_act_result zfcp_erp_adapter_strategy_open(
	struct zfcp_erp_action *act)
{
	struct zfcp_adapter *adapter = act->adapter;

	if (zfcp_qdio_open(adapter->qdio)) {
		atomic_andnot(ZFCP_STATUS_ADAPTER_XCONFIG_OK |
				  ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED,
				  &adapter->status);
		return ZFCP_ERP_FAILED;
	}

	if (zfcp_erp_adapter_strategy_open_fsf(act)) {
		zfcp_erp_adapter_strategy_close(act);
		return ZFCP_ERP_FAILED;
	}

	atomic_or(ZFCP_STATUS_COMMON_OPEN, &adapter->status);

	return ZFCP_ERP_SUCCEEDED;
}

static enum zfcp_erp_act_result zfcp_erp_adapter_strategy(
	struct zfcp_erp_action *act)
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

static enum zfcp_erp_act_result zfcp_erp_port_forced_strategy_close(
	struct zfcp_erp_action *act)
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

static enum zfcp_erp_act_result zfcp_erp_port_forced_strategy(
	struct zfcp_erp_action *erp_action)
{
	struct zfcp_port *port = erp_action->port;
	int status = atomic_read(&port->status);

	switch (erp_action->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
		if ((status & ZFCP_STATUS_PORT_PHYS_OPEN) &&
		    (status & ZFCP_STATUS_COMMON_OPEN))
			return zfcp_erp_port_forced_strategy_close(erp_action);
		else
			return ZFCP_ERP_FAILED;

	case ZFCP_ERP_STEP_PHYS_PORT_CLOSING:
		if (!(status & ZFCP_STATUS_PORT_PHYS_OPEN))
			return ZFCP_ERP_SUCCEEDED;
		break;
	case ZFCP_ERP_STEP_PORT_CLOSING:
	case ZFCP_ERP_STEP_PORT_OPENING:
	case ZFCP_ERP_STEP_LUN_CLOSING:
	case ZFCP_ERP_STEP_LUN_OPENING:
		/* NOP */
		break;
	}
	return ZFCP_ERP_FAILED;
}

static enum zfcp_erp_act_result zfcp_erp_port_strategy_close(
	struct zfcp_erp_action *erp_action)
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

static enum zfcp_erp_act_result zfcp_erp_port_strategy_open_port(
	struct zfcp_erp_action *erp_action)
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

static enum zfcp_erp_act_result zfcp_erp_port_strategy_open_common(
	struct zfcp_erp_action *act)
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
		/* no early return otherwise, continue after switch case */
		break;
	case ZFCP_ERP_STEP_LUN_CLOSING:
	case ZFCP_ERP_STEP_LUN_OPENING:
		/* NOP */
		break;
	}
	return ZFCP_ERP_FAILED;
}

static enum zfcp_erp_act_result zfcp_erp_port_strategy(
	struct zfcp_erp_action *erp_action)
{
	struct zfcp_port *port = erp_action->port;
	int p_status = atomic_read(&port->status);

	if ((p_status & ZFCP_STATUS_COMMON_NOESC) &&
	    !(p_status & ZFCP_STATUS_COMMON_OPEN))
		goto close_init_done;

	switch (erp_action->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
		if (p_status & ZFCP_STATUS_COMMON_OPEN)
			return zfcp_erp_port_strategy_close(erp_action);
		break;

	case ZFCP_ERP_STEP_PORT_CLOSING:
		if (p_status & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_FAILED;
		break;
	case ZFCP_ERP_STEP_PHYS_PORT_CLOSING:
	case ZFCP_ERP_STEP_PORT_OPENING:
	case ZFCP_ERP_STEP_LUN_CLOSING:
	case ZFCP_ERP_STEP_LUN_OPENING:
		/* NOP */
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

	atomic_andnot(ZFCP_STATUS_COMMON_ACCESS_DENIED,
			  &zfcp_sdev->status);
}

static enum zfcp_erp_act_result zfcp_erp_lun_strategy_close(
	struct zfcp_erp_action *erp_action)
{
	int retval = zfcp_fsf_close_lun(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_LUN_CLOSING;
	if (retval)
		return ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static enum zfcp_erp_act_result zfcp_erp_lun_strategy_open(
	struct zfcp_erp_action *erp_action)
{
	int retval = zfcp_fsf_open_lun(erp_action);
	if (retval == -ENOMEM)
		return ZFCP_ERP_NOMEM;
	erp_action->step = ZFCP_ERP_STEP_LUN_OPENING;
	if (retval)
		return  ZFCP_ERP_FAILED;
	return ZFCP_ERP_CONTINUES;
}

static enum zfcp_erp_act_result zfcp_erp_lun_strategy(
	struct zfcp_erp_action *erp_action)
{
	struct scsi_device *sdev = erp_action->sdev;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	switch (erp_action->step) {
	case ZFCP_ERP_STEP_UNINITIALIZED:
		zfcp_erp_lun_strategy_clearstati(sdev);
		if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_OPEN)
			return zfcp_erp_lun_strategy_close(erp_action);
		/* already closed */
		fallthrough;
	case ZFCP_ERP_STEP_LUN_CLOSING:
		if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_FAILED;
		if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
			return ZFCP_ERP_EXIT;
		return zfcp_erp_lun_strategy_open(erp_action);

	case ZFCP_ERP_STEP_LUN_OPENING:
		if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_OPEN)
			return ZFCP_ERP_SUCCEEDED;
		break;
	case ZFCP_ERP_STEP_PHYS_PORT_CLOSING:
	case ZFCP_ERP_STEP_PORT_CLOSING:
	case ZFCP_ERP_STEP_PORT_OPENING:
		/* NOP */
		break;
	}
	return ZFCP_ERP_FAILED;
}

static enum zfcp_erp_act_result zfcp_erp_strategy_check_lun(
	struct scsi_device *sdev, enum zfcp_erp_act_result result)
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
	case ZFCP_ERP_CONTINUES:
	case ZFCP_ERP_EXIT:
	case ZFCP_ERP_DISMISSED:
	case ZFCP_ERP_NOMEM:
		/* NOP */
		break;
	}

	if (atomic_read(&zfcp_sdev->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_lun_block(sdev, 0);
		result = ZFCP_ERP_EXIT;
	}
	return result;
}

static enum zfcp_erp_act_result zfcp_erp_strategy_check_port(
	struct zfcp_port *port, enum zfcp_erp_act_result result)
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
	case ZFCP_ERP_CONTINUES:
	case ZFCP_ERP_EXIT:
	case ZFCP_ERP_DISMISSED:
	case ZFCP_ERP_NOMEM:
		/* NOP */
		break;
	}

	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_port_block(port, 0);
		result = ZFCP_ERP_EXIT;
	}
	return result;
}

static enum zfcp_erp_act_result zfcp_erp_strategy_check_adapter(
	struct zfcp_adapter *adapter, enum zfcp_erp_act_result result)
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
	case ZFCP_ERP_CONTINUES:
	case ZFCP_ERP_EXIT:
	case ZFCP_ERP_DISMISSED:
	case ZFCP_ERP_NOMEM:
		/* NOP */
		break;
	}

	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_ERP_FAILED) {
		zfcp_erp_adapter_block(adapter, 0);
		result = ZFCP_ERP_EXIT;
	}
	return result;
}

static enum zfcp_erp_act_result zfcp_erp_strategy_check_target(
	struct zfcp_erp_action *erp_action, enum zfcp_erp_act_result result)
{
	struct zfcp_adapter *adapter = erp_action->adapter;
	struct zfcp_port *port = erp_action->port;
	struct scsi_device *sdev = erp_action->sdev;

	switch (erp_action->type) {

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

static enum zfcp_erp_act_result zfcp_erp_strategy_statechange(
	struct zfcp_erp_action *act, enum zfcp_erp_act_result result)
{
	enum zfcp_erp_act_type type = act->type;
	struct zfcp_adapter *adapter = act->adapter;
	struct zfcp_port *port = act->port;
	struct scsi_device *sdev = act->sdev;
	struct zfcp_scsi_dev *zfcp_sdev;
	u32 erp_status = act->status;

	switch (type) {
	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (zfcp_erp_strat_change_det(&adapter->status, erp_status)) {
			_zfcp_erp_adapter_reopen(adapter,
						 ZFCP_STATUS_COMMON_ERP_FAILED,
						 "ersscg1");
			return ZFCP_ERP_EXIT;
		}
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		if (zfcp_erp_strat_change_det(&port->status, erp_status)) {
			_zfcp_erp_port_reopen(port,
					      ZFCP_STATUS_COMMON_ERP_FAILED,
					      "ersscg2");
			return ZFCP_ERP_EXIT;
		}
		break;

	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zfcp_sdev = sdev_to_zfcp(sdev);
		if (zfcp_erp_strat_change_det(&zfcp_sdev->status, erp_status)) {
			_zfcp_erp_lun_reopen(sdev,
					     ZFCP_STATUS_COMMON_ERP_FAILED,
					     "ersscg3", 0);
			return ZFCP_ERP_EXIT;
		}
		break;
	}
	return result;
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
	zfcp_dbf_rec_run("eractd1", erp_action);

	switch (erp_action->type) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		zfcp_sdev = sdev_to_zfcp(erp_action->sdev);
		atomic_andnot(ZFCP_STATUS_COMMON_ERP_INUSE,
				  &zfcp_sdev->status);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
	case ZFCP_ERP_ACTION_REOPEN_PORT:
		atomic_andnot(ZFCP_STATUS_COMMON_ERP_INUSE,
				  &erp_action->port->status);
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		atomic_andnot(ZFCP_STATUS_COMMON_ERP_INUSE,
				  &erp_action->adapter->status);
		break;
	}
}

/**
 * zfcp_erp_try_rport_unblock - unblock rport if no more/new recovery
 * @port: zfcp_port whose fc_rport we should try to unblock
 */
static void zfcp_erp_try_rport_unblock(struct zfcp_port *port)
{
	unsigned long flags;
	struct zfcp_adapter *adapter = port->adapter;
	int port_status;
	struct Scsi_Host *shost = adapter->scsi_host;
	struct scsi_device *sdev;

	write_lock_irqsave(&adapter->erp_lock, flags);
	port_status = atomic_read(&port->status);
	if ((port_status & ZFCP_STATUS_COMMON_UNBLOCKED)    == 0 ||
	    (port_status & (ZFCP_STATUS_COMMON_ERP_INUSE |
			    ZFCP_STATUS_COMMON_ERP_FAILED)) != 0) {
		/* new ERP of severity >= port triggered elsewhere meanwhile or
		 * local link down (adapter erp_failed but not clear unblock)
		 */
		zfcp_dbf_rec_run_lvl(4, "ertru_p", &port->erp_action);
		write_unlock_irqrestore(&adapter->erp_lock, flags);
		return;
	}
	spin_lock(shost->host_lock);
	__shost_for_each_device(sdev, shost) {
		struct zfcp_scsi_dev *zsdev = sdev_to_zfcp(sdev);
		int lun_status;

		if (sdev->sdev_state == SDEV_DEL ||
		    sdev->sdev_state == SDEV_CANCEL)
			continue;
		if (zsdev->port != port)
			continue;
		/* LUN under port of interest */
		lun_status = atomic_read(&zsdev->status);
		if ((lun_status & ZFCP_STATUS_COMMON_ERP_FAILED) != 0)
			continue; /* unblock rport despite failed LUNs */
		/* LUN recovery not given up yet [maybe follow-up pending] */
		if ((lun_status & ZFCP_STATUS_COMMON_UNBLOCKED) == 0 ||
		    (lun_status & ZFCP_STATUS_COMMON_ERP_INUSE) != 0) {
			/* LUN blocked:
			 * not yet unblocked [LUN recovery pending]
			 * or meanwhile blocked [new LUN recovery triggered]
			 */
			zfcp_dbf_rec_run_lvl(4, "ertru_l", &zsdev->erp_action);
			spin_unlock(shost->host_lock);
			write_unlock_irqrestore(&adapter->erp_lock, flags);
			return;
		}
	}
	/* now port has no child or all children have completed recovery,
	 * and no ERP of severity >= port was meanwhile triggered elsewhere
	 */
	zfcp_scsi_schedule_rport_register(port);
	spin_unlock(shost->host_lock);
	write_unlock_irqrestore(&adapter->erp_lock, flags);
}

static void zfcp_erp_action_cleanup(struct zfcp_erp_action *act,
				    enum zfcp_erp_act_result result)
{
	struct zfcp_adapter *adapter = act->adapter;
	struct zfcp_port *port = act->port;
	struct scsi_device *sdev = act->sdev;

	switch (act->type) {
	case ZFCP_ERP_ACTION_REOPEN_LUN:
		if (!(act->status & ZFCP_STATUS_ERP_NO_REF))
			scsi_device_put(sdev);
		zfcp_erp_try_rport_unblock(port);
		break;

	case ZFCP_ERP_ACTION_REOPEN_PORT:
		/* This switch case might also happen after a forced reopen
		 * was successfully done and thus overwritten with a new
		 * non-forced reopen at `ersfs_2'. In this case, we must not
		 * do the clean-up of the non-forced version.
		 */
		if (act->step != ZFCP_ERP_STEP_UNINITIALIZED)
			if (result == ZFCP_ERP_SUCCEEDED)
				zfcp_erp_try_rport_unblock(port);
		fallthrough;
	case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED:
		put_device(&port->dev);
		break;

	case ZFCP_ERP_ACTION_REOPEN_ADAPTER:
		if (result == ZFCP_ERP_SUCCEEDED) {
			register_service_level(&adapter->service_level);
			zfcp_fc_conditional_port_scan(adapter);
			queue_work(adapter->work_queue, &adapter->ns_up_work);
		} else
			unregister_service_level(&adapter->service_level);

		kref_put(&adapter->ref, zfcp_adapter_release);
		break;
	}
}

static enum zfcp_erp_act_result zfcp_erp_strategy_do_action(
	struct zfcp_erp_action *erp_action)
{
	switch (erp_action->type) {
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

static enum zfcp_erp_act_result zfcp_erp_strategy(
	struct zfcp_erp_action *erp_action)
{
	enum zfcp_erp_act_result result;
	unsigned long flags;
	struct zfcp_adapter *adapter = erp_action->adapter;

	kref_get(&adapter->ref);

	write_lock_irqsave(&adapter->erp_lock, flags);
	zfcp_erp_strategy_check_fsfreq(erp_action);

	if (erp_action->status & ZFCP_STATUS_ERP_DISMISSED) {
		zfcp_erp_action_dequeue(erp_action);
		result = ZFCP_ERP_DISMISSED;
		goto unlock;
	}

	if (erp_action->status & ZFCP_STATUS_ERP_TIMEDOUT) {
		result = ZFCP_ERP_FAILED;
		goto check_target;
	}

	zfcp_erp_action_to_running(erp_action);

	/* no lock to allow for blocking operations */
	write_unlock_irqrestore(&adapter->erp_lock, flags);
	result = zfcp_erp_strategy_do_action(erp_action);
	write_lock_irqsave(&adapter->erp_lock, flags);

	if (erp_action->status & ZFCP_STATUS_ERP_DISMISSED)
		result = ZFCP_ERP_CONTINUES;

	switch (result) {
	case ZFCP_ERP_NOMEM:
		if (!(erp_action->status & ZFCP_STATUS_ERP_LOWMEM)) {
			++adapter->erp_low_mem_count;
			erp_action->status |= ZFCP_STATUS_ERP_LOWMEM;
		}
		if (adapter->erp_total_count == adapter->erp_low_mem_count)
			_zfcp_erp_adapter_reopen(adapter, 0, "erstgy1");
		else {
			zfcp_erp_strategy_memwait(erp_action);
			result = ZFCP_ERP_CONTINUES;
		}
		goto unlock;

	case ZFCP_ERP_CONTINUES:
		if (erp_action->status & ZFCP_STATUS_ERP_LOWMEM) {
			--adapter->erp_low_mem_count;
			erp_action->status &= ~ZFCP_STATUS_ERP_LOWMEM;
		}
		goto unlock;
	case ZFCP_ERP_SUCCEEDED:
	case ZFCP_ERP_FAILED:
	case ZFCP_ERP_EXIT:
	case ZFCP_ERP_DISMISSED:
		/* NOP */
		break;
	}

check_target:
	result = zfcp_erp_strategy_check_target(erp_action, result);
	zfcp_erp_action_dequeue(erp_action);
	result = zfcp_erp_strategy_statechange(erp_action, result);
	if (result == ZFCP_ERP_EXIT)
		goto unlock;
	if (result == ZFCP_ERP_SUCCEEDED)
		zfcp_erp_strategy_followup_success(erp_action);
	if (result == ZFCP_ERP_FAILED)
		zfcp_erp_strategy_followup_failed(erp_action);

 unlock:
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	if (result != ZFCP_ERP_CONTINUES)
		zfcp_erp_action_cleanup(erp_action, result);

	kref_put(&adapter->ref, zfcp_adapter_release);
	return result;
}

static int zfcp_erp_thread(void *data)
{
	struct zfcp_adapter *adapter = (struct zfcp_adapter *) data;
	struct list_head *next;
	struct zfcp_erp_action *act;
	unsigned long flags;

	for (;;) {
		wait_event_interruptible(adapter->erp_ready_wq,
			   !list_empty(&adapter->erp_ready_head) ||
			   kthread_should_stop());

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
 * Return: 0 on success, or error code from kthread_run().
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

	atomic_or(mask, &adapter->status);

	if (!common_mask)
		return;

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list)
		atomic_or(common_mask, &port->status);
	read_unlock_irqrestore(&adapter->port_list_lock, flags);

	/*
	 * if `scsi_host` is missing, xconfig/xport data has never completed
	 * yet, so we can't access it, but there are also no SDEVs yet
	 */
	if (adapter->scsi_host == NULL)
		return;

	spin_lock_irqsave(adapter->scsi_host->host_lock, flags);
	__shost_for_each_device(sdev, adapter->scsi_host)
		atomic_or(common_mask, &sdev_to_zfcp(sdev)->status);
	spin_unlock_irqrestore(adapter->scsi_host->host_lock, flags);
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

	atomic_andnot(mask, &adapter->status);

	if (!common_mask)
		return;

	if (clear_counter)
		atomic_set(&adapter->erp_counter, 0);

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list) {
		atomic_andnot(common_mask, &port->status);
		if (clear_counter)
			atomic_set(&port->erp_counter, 0);
	}
	read_unlock_irqrestore(&adapter->port_list_lock, flags);

	/*
	 * if `scsi_host` is missing, xconfig/xport data has never completed
	 * yet, so we can't access it, but there are also no SDEVs yet
	 */
	if (adapter->scsi_host == NULL)
		return;

	spin_lock_irqsave(adapter->scsi_host->host_lock, flags);
	__shost_for_each_device(sdev, adapter->scsi_host) {
		atomic_andnot(common_mask, &sdev_to_zfcp(sdev)->status);
		if (clear_counter)
			atomic_set(&sdev_to_zfcp(sdev)->erp_counter, 0);
	}
	spin_unlock_irqrestore(adapter->scsi_host->host_lock, flags);
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
	unsigned long flags;

	atomic_or(mask, &port->status);

	if (!common_mask)
		return;

	spin_lock_irqsave(port->adapter->scsi_host->host_lock, flags);
	__shost_for_each_device(sdev, port->adapter->scsi_host)
		if (sdev_to_zfcp(sdev)->port == port)
			atomic_or(common_mask,
					&sdev_to_zfcp(sdev)->status);
	spin_unlock_irqrestore(port->adapter->scsi_host->host_lock, flags);
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
	unsigned long flags;

	atomic_andnot(mask, &port->status);

	if (!common_mask)
		return;

	if (clear_counter)
		atomic_set(&port->erp_counter, 0);

	spin_lock_irqsave(port->adapter->scsi_host->host_lock, flags);
	__shost_for_each_device(sdev, port->adapter->scsi_host)
		if (sdev_to_zfcp(sdev)->port == port) {
			atomic_andnot(common_mask,
					  &sdev_to_zfcp(sdev)->status);
			if (clear_counter)
				atomic_set(&sdev_to_zfcp(sdev)->erp_counter, 0);
		}
	spin_unlock_irqrestore(port->adapter->scsi_host->host_lock, flags);
}

/**
 * zfcp_erp_set_lun_status - set lun status bits
 * @sdev: SCSI device / lun to set the status bits
 * @mask: status bits to change
 */
void zfcp_erp_set_lun_status(struct scsi_device *sdev, u32 mask)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	atomic_or(mask, &zfcp_sdev->status);
}

/**
 * zfcp_erp_clear_lun_status - clear lun status bits
 * @sdev: SCSi device / lun to clear the status bits
 * @mask: status bits to change
 */
void zfcp_erp_clear_lun_status(struct scsi_device *sdev, u32 mask)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	atomic_andnot(mask, &zfcp_sdev->status);

	if (mask & ZFCP_STATUS_COMMON_ERP_FAILED)
		atomic_set(&zfcp_sdev->erp_counter, 0);
}

/**
 * zfcp_erp_adapter_reset_sync() - Really reopen adapter and wait.
 * @adapter: Pointer to zfcp_adapter to reopen.
 * @dbftag: Trace tag string of length %ZFCP_DBF_TAG_LEN.
 */
void zfcp_erp_adapter_reset_sync(struct zfcp_adapter *adapter, char *dbftag)
{
	zfcp_erp_set_adapter_status(adapter, ZFCP_STATUS_COMMON_RUNNING);
	zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED, dbftag);
	zfcp_erp_wait(adapter);
}
