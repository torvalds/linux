// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2011-2017, The Linux Foundation
 */

#include <linux/errno.h>
#include "slimbus.h"

/**
 * slim_ctrl_clk_pause() - Called by slimbus controller to enter/exit
 *			   'clock pause'
 * @ctrl: controller requesting bus to be paused or woken up
 * @wakeup: Wakeup this controller from clock pause.
 * @restart: Restart time value per spec used for clock pause. This value
 *	isn't used when controller is to be woken up.
 *
 * Slimbus specification needs this sequence to turn-off clocks for the bus.
 * The sequence involves sending 3 broadcast messages (reconfiguration
 * sequence) to inform all devices on the bus.
 * To exit clock-pause, controller typically wakes up active framer device.
 * This API executes clock pause reconfiguration sequence if wakeup is false.
 * If wakeup is true, controller's wakeup is called.
 * For entering clock-pause, -EBUSY is returned if a message txn in pending.
 */
int slim_ctrl_clk_pause(struct slim_controller *ctrl, bool wakeup, u8 restart)
{
	int i, ret = 0;
	unsigned long flags;
	struct slim_sched *sched = &ctrl->sched;
	struct slim_val_inf msg = {0, 0, NULL, NULL};

	DEFINE_SLIM_BCAST_TXN(txn, SLIM_MSG_MC_BEGIN_RECONFIGURATION,
				3, SLIM_LA_MANAGER, &msg);

	if (wakeup == false && restart > SLIM_CLK_UNSPECIFIED)
		return -EINVAL;

	mutex_lock(&sched->m_reconf);
	if (wakeup) {
		if (sched->clk_state == SLIM_CLK_ACTIVE) {
			mutex_unlock(&sched->m_reconf);
			return 0;
		}

		/*
		 * Fine-tune calculation based on clock gear,
		 * message-bandwidth after bandwidth management
		 */
		ret = wait_for_completion_timeout(&sched->pause_comp,
				msecs_to_jiffies(100));
		if (!ret) {
			mutex_unlock(&sched->m_reconf);
			pr_err("Previous clock pause did not finish");
			return -ETIMEDOUT;
		}
		ret = 0;

		/*
		 * Slimbus framework will call controller wakeup
		 * Controller should make sure that it sets active framer
		 * out of clock pause
		 */
		if (sched->clk_state == SLIM_CLK_PAUSED && ctrl->wakeup)
			ret = ctrl->wakeup(ctrl);
		if (!ret)
			sched->clk_state = SLIM_CLK_ACTIVE;
		mutex_unlock(&sched->m_reconf);

		return ret;
	}

	/* already paused */
	if (ctrl->sched.clk_state == SLIM_CLK_PAUSED) {
		mutex_unlock(&sched->m_reconf);
		return 0;
	}

	spin_lock_irqsave(&ctrl->txn_lock, flags);
	for (i = 0; i < SLIM_MAX_TIDS; i++) {
		/* Pending response for a message */
		if (idr_find(&ctrl->tid_idr, i)) {
			spin_unlock_irqrestore(&ctrl->txn_lock, flags);
			mutex_unlock(&sched->m_reconf);
			return -EBUSY;
		}
	}
	spin_unlock_irqrestore(&ctrl->txn_lock, flags);

	sched->clk_state = SLIM_CLK_ENTERING_PAUSE;

	/* clock pause sequence */
	ret = slim_do_transfer(ctrl, &txn);
	if (ret)
		goto clk_pause_ret;

	txn.mc = SLIM_MSG_MC_NEXT_PAUSE_CLOCK;
	txn.rl = 4;
	msg.num_bytes = 1;
	msg.wbuf = &restart;
	ret = slim_do_transfer(ctrl, &txn);
	if (ret)
		goto clk_pause_ret;

	txn.mc = SLIM_MSG_MC_RECONFIGURE_NOW;
	txn.rl = 3;
	msg.num_bytes = 1;
	msg.wbuf = NULL;
	ret = slim_do_transfer(ctrl, &txn);

clk_pause_ret:
	if (ret) {
		sched->clk_state = SLIM_CLK_ACTIVE;
	} else {
		sched->clk_state = SLIM_CLK_PAUSED;
		complete(&sched->pause_comp);
	}
	mutex_unlock(&sched->m_reconf);

	return ret;
}
EXPORT_SYMBOL_GPL(slim_ctrl_clk_pause);
