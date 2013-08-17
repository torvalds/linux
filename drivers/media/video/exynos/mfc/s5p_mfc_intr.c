/*
 * drivers/media/video/exynos/mfc/s5p_mfc_intr.c
 *
 * C file for Samsung MFC (Multi Function Codec - FIMV) driver
 * This file contains functions used to wait for command completion.
 *
 * Kamil Debski, Copyright (c) 2010 Samsung Electronics
 * http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/io.h>

#include "s5p_mfc_common.h"

#include "s5p_mfc_intr.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_pm.h"

#define wait_condition(x, c) (x->int_cond &&		\
		(R2H_BIT(x->int_type) & r2h_bits(c)))
#define is_err_cond(x)	((x->int_cond) && (x->int_type == S5P_FIMV_R2H_CMD_ERR_RET))
int s5p_mfc_wait_for_done_dev(struct s5p_mfc_dev *dev, int command)
{
	int ret;

	ret = wait_event_timeout(dev->queue,
			wait_condition(dev, command),
			msecs_to_jiffies(MFC_INT_TIMEOUT));
	if (ret == 0) {
		mfc_err("Interrupt (dev->int_type:%d, command:%d) timed out.\n",
							dev->int_type, command);
		return 1;
	} else if (ret == -ERESTARTSYS) {
		mfc_err("Interrupted by a signal.\n");
		return 1;
	}
	mfc_debug(1, "Finished waiting (dev->int_type:%d, command: %d).\n",
							dev->int_type, command);
	return 0;
}

void s5p_mfc_clean_dev_int_flags(struct s5p_mfc_dev *dev)
{
	dev->int_cond = 0;
	dev->int_type = 0;
	dev->int_err = 0;
}

int s5p_mfc_wait_for_done_ctx(struct s5p_mfc_ctx *ctx,
				    int command, int interrupt)
{
	int ret;

	if (interrupt) {
		ret = wait_event_interruptible_timeout(ctx->queue,
				wait_condition(ctx, command),
				msecs_to_jiffies(MFC_INT_TIMEOUT));
	} else {
		ret = wait_event_timeout(ctx->queue,
				wait_condition(ctx, command),
				msecs_to_jiffies(MFC_INT_TIMEOUT));
	}
	if (ret == 0) {
		mfc_err("Interrupt (ctx->int_type:%d, command:%d) timed out.\n",
							ctx->int_type, command);
		return 1;
	} else if (ret == -ERESTARTSYS) {
		mfc_err("Interrupted by a signal.\n");
		return 1;
	} else if (ret > 0) {
		if (is_err_cond(ctx)) {
			mfc_err("Finished (ctx->int_type:%d, command: %d).\n",
					ctx->int_type, command);
			mfc_err("But error (ctx->int_err:%d).\n", ctx->int_err);
			return -1;
		}
	}
	mfc_debug(1, "Finished waiting (ctx->int_type:%d, command: %d).\n",
							ctx->int_type, command);
	return 0;
}

void s5p_mfc_clean_ctx_int_flags(struct s5p_mfc_ctx *ctx)
{
	ctx->int_cond = 0;
	ctx->int_type = 0;
	ctx->int_err = 0;
}

void s5p_mfc_cleanup_timeout(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;

	spin_lock_irq(&dev->condlock);
	clear_bit(ctx->num, &dev->ctx_work_bits);
	spin_unlock_irq(&dev->condlock);
	if (test_and_clear_bit(ctx->num, &dev->hw_lock))
		s5p_mfc_clock_off();

	s5p_mfc_try_run(dev);
}
