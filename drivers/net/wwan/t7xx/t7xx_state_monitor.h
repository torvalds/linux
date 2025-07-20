/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *
 * Contributors:
 *  Eliot Lee <eliot.lee@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#ifndef __T7XX_MONITOR_H__
#define __T7XX_MONITOR_H__

#include <linux/bits.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "t7xx_modem_ops.h"

enum t7xx_fsm_state {
	FSM_STATE_INIT,
	FSM_STATE_PRE_START,
	FSM_STATE_STARTING,
	FSM_STATE_READY,
	FSM_STATE_EXCEPTION,
	FSM_STATE_STOPPING,
	FSM_STATE_STOPPED,
};

enum t7xx_fsm_event_state {
	FSM_EVENT_INVALID,
	FSM_EVENT_MD_HS2,
	FSM_EVENT_AP_HS2,
	FSM_EVENT_MD_EX,
	FSM_EVENT_MD_EX_REC_OK,
	FSM_EVENT_MD_EX_PASS,
	FSM_EVENT_MD_HS2_EXIT,
	FSM_EVENT_AP_HS2_EXIT,
	FSM_EVENT_MAX
};

enum t7xx_fsm_cmd_state {
	FSM_CMD_INVALID,
	FSM_CMD_START,
	FSM_CMD_EXCEPTION,
	FSM_CMD_PRE_STOP,
	FSM_CMD_STOP,
};

enum t7xx_ex_reason {
	EXCEPTION_HS_TIMEOUT,
	EXCEPTION_EVENT,
};

enum t7xx_md_irq_type {
	MD_IRQ_WDT,
	MD_IRQ_CCIF_EX,
	MD_IRQ_PORT_ENUM,
};

enum md_state {
	MD_STATE_INVALID,
	MD_STATE_WAITING_FOR_HS1,
	MD_STATE_WAITING_FOR_HS2,
	MD_STATE_READY,
	MD_STATE_EXCEPTION,
	MD_STATE_WAITING_TO_STOP,
	MD_STATE_STOPPED,
};

#define FSM_CMD_FLAG_WAIT_FOR_COMPLETION	BIT(0)
#define FSM_CMD_FLAG_FLIGHT_MODE		BIT(1)
#define FSM_CMD_FLAG_IN_INTERRUPT		BIT(2)
#define FSM_CMD_EX_REASON			GENMASK(23, 16)

struct t7xx_fsm_ctl {
	struct t7xx_modem	*md;
	enum md_state		md_state;
	unsigned int		curr_state;
	struct list_head	command_queue;
	struct list_head	event_queue;
	wait_queue_head_t	command_wq;
	wait_queue_head_t	event_wq;
	wait_queue_head_t	async_hk_wq;
	spinlock_t		event_lock;		/* Protects event queue */
	spinlock_t		command_lock;		/* Protects command queue */
	struct task_struct	*fsm_thread;
	bool			exp_flg;
	spinlock_t		notifier_lock;		/* Protects notifier list */
	struct list_head	notifier_list;
	u32			status;			/* Device boot stage */
};

struct t7xx_fsm_event {
	struct list_head	entry;
	enum t7xx_fsm_event_state event_id;
	unsigned int		length;
	unsigned char		data[] __counted_by(length);
};

struct t7xx_fsm_command {
	struct list_head	entry;
	enum t7xx_fsm_cmd_state	cmd_id;
	unsigned int		flag;
	struct completion	done;
	int			result;
	struct kref		refcnt;
};

struct t7xx_fsm_notifier {
	struct list_head	entry;
	int (*notifier_fn)(enum md_state state, void *data);
	void			*data;
};

int t7xx_fsm_append_cmd(struct t7xx_fsm_ctl *ctl, enum t7xx_fsm_cmd_state cmd_id,
			unsigned int flag);
int t7xx_fsm_append_event(struct t7xx_fsm_ctl *ctl, enum t7xx_fsm_event_state event_id,
			  unsigned char *data, unsigned int length);
void t7xx_fsm_clr_event(struct t7xx_fsm_ctl *ctl, enum t7xx_fsm_event_state event_id);
void t7xx_fsm_broadcast_state(struct t7xx_fsm_ctl *ctl, enum md_state state);
void t7xx_fsm_reset(struct t7xx_modem *md);
int t7xx_fsm_init(struct t7xx_modem *md);
void t7xx_fsm_uninit(struct t7xx_modem *md);
int t7xx_fsm_recv_md_intr(struct t7xx_fsm_ctl *ctl, enum t7xx_md_irq_type type);
enum md_state t7xx_fsm_get_md_state(struct t7xx_fsm_ctl *ctl);
unsigned int t7xx_fsm_get_ctl_state(struct t7xx_fsm_ctl *ctl);
void t7xx_fsm_notifier_register(struct t7xx_modem *md, struct t7xx_fsm_notifier *notifier);
void t7xx_fsm_notifier_unregister(struct t7xx_modem *md, struct t7xx_fsm_notifier *notifier);

#endif /* __T7XX_MONITOR_H__ */
