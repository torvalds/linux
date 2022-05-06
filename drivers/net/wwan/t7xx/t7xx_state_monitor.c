// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "t7xx_hif_cldma.h"
#include "t7xx_mhccif.h"
#include "t7xx_modem_ops.h"
#include "t7xx_pci.h"
#include "t7xx_pcie_mac.h"
#include "t7xx_port_proxy.h"
#include "t7xx_reg.h"
#include "t7xx_state_monitor.h"

#define FSM_DRM_DISABLE_DELAY_MS		200
#define FSM_EVENT_POLL_INTERVAL_MS		20
#define FSM_MD_EX_REC_OK_TIMEOUT_MS		10000
#define FSM_MD_EX_PASS_TIMEOUT_MS		45000
#define FSM_CMD_TIMEOUT_MS			2000

void t7xx_fsm_notifier_register(struct t7xx_modem *md, struct t7xx_fsm_notifier *notifier)
{
	struct t7xx_fsm_ctl *ctl = md->fsm_ctl;
	unsigned long flags;

	spin_lock_irqsave(&ctl->notifier_lock, flags);
	list_add_tail(&notifier->entry, &ctl->notifier_list);
	spin_unlock_irqrestore(&ctl->notifier_lock, flags);
}

void t7xx_fsm_notifier_unregister(struct t7xx_modem *md, struct t7xx_fsm_notifier *notifier)
{
	struct t7xx_fsm_notifier *notifier_cur, *notifier_next;
	struct t7xx_fsm_ctl *ctl = md->fsm_ctl;
	unsigned long flags;

	spin_lock_irqsave(&ctl->notifier_lock, flags);
	list_for_each_entry_safe(notifier_cur, notifier_next, &ctl->notifier_list, entry) {
		if (notifier_cur == notifier)
			list_del(&notifier->entry);
	}
	spin_unlock_irqrestore(&ctl->notifier_lock, flags);
}

static void fsm_state_notify(struct t7xx_modem *md, enum md_state state)
{
	struct t7xx_fsm_ctl *ctl = md->fsm_ctl;
	struct t7xx_fsm_notifier *notifier;
	unsigned long flags;

	spin_lock_irqsave(&ctl->notifier_lock, flags);
	list_for_each_entry(notifier, &ctl->notifier_list, entry) {
		spin_unlock_irqrestore(&ctl->notifier_lock, flags);
		if (notifier->notifier_fn)
			notifier->notifier_fn(state, notifier->data);

		spin_lock_irqsave(&ctl->notifier_lock, flags);
	}
	spin_unlock_irqrestore(&ctl->notifier_lock, flags);
}

void t7xx_fsm_broadcast_state(struct t7xx_fsm_ctl *ctl, enum md_state state)
{
	ctl->md_state = state;

	/* Update to port first, otherwise sending message on HS2 may fail */
	t7xx_port_proxy_md_status_notify(ctl->md->port_prox, state);
	fsm_state_notify(ctl->md, state);
}

static void fsm_finish_command(struct t7xx_fsm_ctl *ctl, struct t7xx_fsm_command *cmd, int result)
{
	if (cmd->flag & FSM_CMD_FLAG_WAIT_FOR_COMPLETION) {
		*cmd->ret = result;
		complete_all(cmd->done);
	}

	kfree(cmd);
}

static void fsm_del_kf_event(struct t7xx_fsm_event *event)
{
	list_del(&event->entry);
	kfree(event);
}

static void fsm_flush_event_cmd_qs(struct t7xx_fsm_ctl *ctl)
{
	struct device *dev = &ctl->md->t7xx_dev->pdev->dev;
	struct t7xx_fsm_event *event, *evt_next;
	struct t7xx_fsm_command *cmd, *cmd_next;
	unsigned long flags;

	spin_lock_irqsave(&ctl->command_lock, flags);
	list_for_each_entry_safe(cmd, cmd_next, &ctl->command_queue, entry) {
		dev_warn(dev, "Unhandled command %d\n", cmd->cmd_id);
		list_del(&cmd->entry);
		fsm_finish_command(ctl, cmd, -EINVAL);
	}
	spin_unlock_irqrestore(&ctl->command_lock, flags);

	spin_lock_irqsave(&ctl->event_lock, flags);
	list_for_each_entry_safe(event, evt_next, &ctl->event_queue, entry) {
		dev_warn(dev, "Unhandled event %d\n", event->event_id);
		fsm_del_kf_event(event);
	}
	spin_unlock_irqrestore(&ctl->event_lock, flags);
}

static void fsm_wait_for_event(struct t7xx_fsm_ctl *ctl, enum t7xx_fsm_event_state event_expected,
			       enum t7xx_fsm_event_state event_ignore, int retries)
{
	struct t7xx_fsm_event *event;
	bool event_received = false;
	unsigned long flags;
	int cnt = 0;

	while (cnt++ < retries && !event_received) {
		bool sleep_required = true;

		if (kthread_should_stop())
			return;

		spin_lock_irqsave(&ctl->event_lock, flags);
		event = list_first_entry_or_null(&ctl->event_queue, struct t7xx_fsm_event, entry);
		if (event) {
			event_received = event->event_id == event_expected;
			if (event_received || event->event_id == event_ignore) {
				fsm_del_kf_event(event);
				sleep_required = false;
			}
		}
		spin_unlock_irqrestore(&ctl->event_lock, flags);

		if (sleep_required)
			msleep(FSM_EVENT_POLL_INTERVAL_MS);
	}
}

static void fsm_routine_exception(struct t7xx_fsm_ctl *ctl, struct t7xx_fsm_command *cmd,
				  enum t7xx_ex_reason reason)
{
	struct device *dev = &ctl->md->t7xx_dev->pdev->dev;

	if (ctl->curr_state != FSM_STATE_READY && ctl->curr_state != FSM_STATE_STARTING) {
		if (cmd)
			fsm_finish_command(ctl, cmd, -EINVAL);

		return;
	}

	ctl->curr_state = FSM_STATE_EXCEPTION;

	switch (reason) {
	case EXCEPTION_HS_TIMEOUT:
		dev_err(dev, "Boot Handshake failure\n");
		break;

	case EXCEPTION_EVENT:
		dev_err(dev, "Exception event\n");
		t7xx_fsm_broadcast_state(ctl, MD_STATE_EXCEPTION);
		t7xx_md_exception_handshake(ctl->md);

		fsm_wait_for_event(ctl, FSM_EVENT_MD_EX_REC_OK, FSM_EVENT_MD_EX,
				   FSM_MD_EX_REC_OK_TIMEOUT_MS / FSM_EVENT_POLL_INTERVAL_MS);
		fsm_wait_for_event(ctl, FSM_EVENT_MD_EX_PASS, FSM_EVENT_INVALID,
				   FSM_MD_EX_PASS_TIMEOUT_MS / FSM_EVENT_POLL_INTERVAL_MS);
		break;

	default:
		dev_err(dev, "Exception %d\n", reason);
		break;
	}

	if (cmd)
		fsm_finish_command(ctl, cmd, 0);
}

static int fsm_stopped_handler(struct t7xx_fsm_ctl *ctl)
{
	ctl->curr_state = FSM_STATE_STOPPED;

	t7xx_fsm_broadcast_state(ctl, MD_STATE_STOPPED);
	return t7xx_md_reset(ctl->md->t7xx_dev);
}

static void fsm_routine_stopped(struct t7xx_fsm_ctl *ctl, struct t7xx_fsm_command *cmd)
{
	if (ctl->curr_state == FSM_STATE_STOPPED) {
		fsm_finish_command(ctl, cmd, -EINVAL);
		return;
	}

	fsm_finish_command(ctl, cmd, fsm_stopped_handler(ctl));
}

static void fsm_routine_stopping(struct t7xx_fsm_ctl *ctl, struct t7xx_fsm_command *cmd)
{
	struct t7xx_pci_dev *t7xx_dev;
	struct cldma_ctrl *md_ctrl;
	int err;

	if (ctl->curr_state == FSM_STATE_STOPPED || ctl->curr_state == FSM_STATE_STOPPING) {
		fsm_finish_command(ctl, cmd, -EINVAL);
		return;
	}

	md_ctrl = ctl->md->md_ctrl[CLDMA_ID_MD];
	t7xx_dev = ctl->md->t7xx_dev;

	ctl->curr_state = FSM_STATE_STOPPING;
	t7xx_fsm_broadcast_state(ctl, MD_STATE_WAITING_TO_STOP);
	t7xx_cldma_stop(md_ctrl);

	if (!ctl->md->rgu_irq_asserted) {
		t7xx_mhccif_h2d_swint_trigger(t7xx_dev, H2D_CH_DRM_DISABLE_AP);
		/* Wait for the DRM disable to take effect */
		msleep(FSM_DRM_DISABLE_DELAY_MS);

		err = t7xx_acpi_fldr_func(t7xx_dev);
		if (err)
			t7xx_mhccif_h2d_swint_trigger(t7xx_dev, H2D_CH_DEVICE_RESET);
	}

	fsm_finish_command(ctl, cmd, fsm_stopped_handler(ctl));
}

static void t7xx_fsm_broadcast_ready_state(struct t7xx_fsm_ctl *ctl)
{
	if (ctl->md_state != MD_STATE_WAITING_FOR_HS2)
		return;

	ctl->md_state = MD_STATE_READY;

	fsm_state_notify(ctl->md, MD_STATE_READY);
	t7xx_port_proxy_md_status_notify(ctl->md->port_prox, MD_STATE_READY);
}

static void fsm_routine_ready(struct t7xx_fsm_ctl *ctl)
{
	struct t7xx_modem *md = ctl->md;

	ctl->curr_state = FSM_STATE_READY;
	t7xx_fsm_broadcast_ready_state(ctl);
	t7xx_md_event_notify(md, FSM_READY);
}

static int fsm_routine_starting(struct t7xx_fsm_ctl *ctl)
{
	struct t7xx_modem *md = ctl->md;
	struct device *dev;

	ctl->curr_state = FSM_STATE_STARTING;

	t7xx_fsm_broadcast_state(ctl, MD_STATE_WAITING_FOR_HS1);
	t7xx_md_event_notify(md, FSM_START);

	wait_event_interruptible_timeout(ctl->async_hk_wq, md->core_md.ready || ctl->exp_flg,
					 HZ * 60);
	dev = &md->t7xx_dev->pdev->dev;

	if (ctl->exp_flg)
		dev_err(dev, "MD exception is captured during handshake\n");

	if (!md->core_md.ready) {
		dev_err(dev, "MD handshake timeout\n");
		if (md->core_md.handshake_ongoing)
			t7xx_fsm_append_event(ctl, FSM_EVENT_MD_HS2_EXIT, NULL, 0);

		fsm_routine_exception(ctl, NULL, EXCEPTION_HS_TIMEOUT);
		return -ETIMEDOUT;
	}

	fsm_routine_ready(ctl);
	return 0;
}

static void fsm_routine_start(struct t7xx_fsm_ctl *ctl, struct t7xx_fsm_command *cmd)
{
	struct t7xx_modem *md = ctl->md;
	u32 dev_status;
	int ret;

	if (!md)
		return;

	if (ctl->curr_state != FSM_STATE_INIT && ctl->curr_state != FSM_STATE_PRE_START &&
	    ctl->curr_state != FSM_STATE_STOPPED) {
		fsm_finish_command(ctl, cmd, -EINVAL);
		return;
	}

	ctl->curr_state = FSM_STATE_PRE_START;
	t7xx_md_event_notify(md, FSM_PRE_START);

	ret = read_poll_timeout(ioread32, dev_status,
				(dev_status & MISC_STAGE_MASK) == LINUX_STAGE, 20000, 2000000,
				false, IREG_BASE(md->t7xx_dev) + T7XX_PCIE_MISC_DEV_STATUS);
	if (ret) {
		struct device *dev = &md->t7xx_dev->pdev->dev;

		fsm_finish_command(ctl, cmd, -ETIMEDOUT);
		dev_err(dev, "Invalid device status 0x%lx\n", dev_status & MISC_STAGE_MASK);
		return;
	}

	t7xx_cldma_hif_hw_init(md->md_ctrl[CLDMA_ID_MD]);
	fsm_finish_command(ctl, cmd, fsm_routine_starting(ctl));
}

static int fsm_main_thread(void *data)
{
	struct t7xx_fsm_ctl *ctl = data;
	struct t7xx_fsm_command *cmd;
	unsigned long flags;

	while (!kthread_should_stop()) {
		if (wait_event_interruptible(ctl->command_wq, !list_empty(&ctl->command_queue) ||
					     kthread_should_stop()))
			continue;

		if (kthread_should_stop())
			break;

		spin_lock_irqsave(&ctl->command_lock, flags);
		cmd = list_first_entry(&ctl->command_queue, struct t7xx_fsm_command, entry);
		list_del(&cmd->entry);
		spin_unlock_irqrestore(&ctl->command_lock, flags);

		switch (cmd->cmd_id) {
		case FSM_CMD_START:
			fsm_routine_start(ctl, cmd);
			break;

		case FSM_CMD_EXCEPTION:
			fsm_routine_exception(ctl, cmd, FIELD_GET(FSM_CMD_EX_REASON, cmd->flag));
			break;

		case FSM_CMD_PRE_STOP:
			fsm_routine_stopping(ctl, cmd);
			break;

		case FSM_CMD_STOP:
			fsm_routine_stopped(ctl, cmd);
			break;

		default:
			fsm_finish_command(ctl, cmd, -EINVAL);
			fsm_flush_event_cmd_qs(ctl);
			break;
		}
	}

	return 0;
}

int t7xx_fsm_append_cmd(struct t7xx_fsm_ctl *ctl, enum t7xx_fsm_cmd_state cmd_id, unsigned int flag)
{
	DECLARE_COMPLETION_ONSTACK(done);
	struct t7xx_fsm_command *cmd;
	unsigned long flags;
	int ret;

	cmd = kzalloc(sizeof(*cmd), flag & FSM_CMD_FLAG_IN_INTERRUPT ? GFP_ATOMIC : GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	INIT_LIST_HEAD(&cmd->entry);
	cmd->cmd_id = cmd_id;
	cmd->flag = flag;
	if (flag & FSM_CMD_FLAG_WAIT_FOR_COMPLETION) {
		cmd->done = &done;
		cmd->ret = &ret;
	}

	spin_lock_irqsave(&ctl->command_lock, flags);
	list_add_tail(&cmd->entry, &ctl->command_queue);
	spin_unlock_irqrestore(&ctl->command_lock, flags);

	wake_up(&ctl->command_wq);

	if (flag & FSM_CMD_FLAG_WAIT_FOR_COMPLETION) {
		unsigned long wait_ret;

		wait_ret = wait_for_completion_timeout(&done,
						       msecs_to_jiffies(FSM_CMD_TIMEOUT_MS));
		if (!wait_ret)
			return -ETIMEDOUT;

		return ret;
	}

	return 0;
}

int t7xx_fsm_append_event(struct t7xx_fsm_ctl *ctl, enum t7xx_fsm_event_state event_id,
			  unsigned char *data, unsigned int length)
{
	struct device *dev = &ctl->md->t7xx_dev->pdev->dev;
	struct t7xx_fsm_event *event;
	unsigned long flags;

	if (event_id <= FSM_EVENT_INVALID || event_id >= FSM_EVENT_MAX) {
		dev_err(dev, "Invalid event %d\n", event_id);
		return -EINVAL;
	}

	event = kmalloc(sizeof(*event) + length, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	INIT_LIST_HEAD(&event->entry);
	event->event_id = event_id;
	event->length = length;

	if (data && length)
		memcpy(event->data, data, length);

	spin_lock_irqsave(&ctl->event_lock, flags);
	list_add_tail(&event->entry, &ctl->event_queue);
	spin_unlock_irqrestore(&ctl->event_lock, flags);

	wake_up_all(&ctl->event_wq);
	return 0;
}

void t7xx_fsm_clr_event(struct t7xx_fsm_ctl *ctl, enum t7xx_fsm_event_state event_id)
{
	struct t7xx_fsm_event *event, *evt_next;
	unsigned long flags;

	spin_lock_irqsave(&ctl->event_lock, flags);
	list_for_each_entry_safe(event, evt_next, &ctl->event_queue, entry) {
		if (event->event_id == event_id)
			fsm_del_kf_event(event);
	}
	spin_unlock_irqrestore(&ctl->event_lock, flags);
}

enum md_state t7xx_fsm_get_md_state(struct t7xx_fsm_ctl *ctl)
{
	if (ctl)
		return ctl->md_state;

	return MD_STATE_INVALID;
}

unsigned int t7xx_fsm_get_ctl_state(struct t7xx_fsm_ctl *ctl)
{
	if (ctl)
		return ctl->curr_state;

	return FSM_STATE_STOPPED;
}

int t7xx_fsm_recv_md_intr(struct t7xx_fsm_ctl *ctl, enum t7xx_md_irq_type type)
{
	unsigned int cmd_flags = FSM_CMD_FLAG_IN_INTERRUPT;

	if (type == MD_IRQ_PORT_ENUM) {
		return t7xx_fsm_append_cmd(ctl, FSM_CMD_START, cmd_flags);
	} else if (type == MD_IRQ_CCIF_EX) {
		ctl->exp_flg = true;
		wake_up(&ctl->async_hk_wq);
		cmd_flags |= FIELD_PREP(FSM_CMD_EX_REASON, EXCEPTION_EVENT);
		return t7xx_fsm_append_cmd(ctl, FSM_CMD_EXCEPTION, cmd_flags);
	}

	return -EINVAL;
}

void t7xx_fsm_reset(struct t7xx_modem *md)
{
	struct t7xx_fsm_ctl *ctl = md->fsm_ctl;

	fsm_flush_event_cmd_qs(ctl);
	ctl->curr_state = FSM_STATE_STOPPED;
	ctl->exp_flg = false;
}

int t7xx_fsm_init(struct t7xx_modem *md)
{
	struct device *dev = &md->t7xx_dev->pdev->dev;
	struct t7xx_fsm_ctl *ctl;

	ctl = devm_kzalloc(dev, sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;

	md->fsm_ctl = ctl;
	ctl->md = md;
	ctl->curr_state = FSM_STATE_INIT;
	INIT_LIST_HEAD(&ctl->command_queue);
	INIT_LIST_HEAD(&ctl->event_queue);
	init_waitqueue_head(&ctl->async_hk_wq);
	init_waitqueue_head(&ctl->event_wq);
	INIT_LIST_HEAD(&ctl->notifier_list);
	init_waitqueue_head(&ctl->command_wq);
	spin_lock_init(&ctl->event_lock);
	spin_lock_init(&ctl->command_lock);
	ctl->exp_flg = false;
	spin_lock_init(&ctl->notifier_lock);

	ctl->fsm_thread = kthread_run(fsm_main_thread, ctl, "t7xx_fsm");
	return PTR_ERR_OR_ZERO(ctl->fsm_thread);
}

void t7xx_fsm_uninit(struct t7xx_modem *md)
{
	struct t7xx_fsm_ctl *ctl = md->fsm_ctl;

	if (!ctl)
		return;

	if (ctl->fsm_thread)
		kthread_stop(ctl->fsm_thread);

	fsm_flush_event_cmd_qs(ctl);
}
