// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include <linux/jiffies.h>
#include <linux/minmax.h>

#include "errors.h"
#include "time-utils.h"
#include "uds-threads.h"

int uds_init_cond(struct cond_var *cv)
{
	init_waitqueue_head(&cv->wait_queue);
	return UDS_SUCCESS;
}

int uds_signal_cond(struct cond_var *cv)
{
	wake_up(&cv->wait_queue);
	return UDS_SUCCESS;
}

int uds_broadcast_cond(struct cond_var *cv)
{
	wake_up_all(&cv->wait_queue);
	return UDS_SUCCESS;
}

int uds_wait_cond(struct cond_var *cv, struct mutex *mutex)
{
	DEFINE_WAIT(__wait);

	prepare_to_wait(&cv->wait_queue, &__wait, TASK_IDLE);
	uds_unlock_mutex(mutex);
	schedule();
	finish_wait(&cv->wait_queue, &__wait);
	uds_lock_mutex(mutex);
	return UDS_SUCCESS;
}

int uds_destroy_cond(struct cond_var *cv)
{
	return UDS_SUCCESS;
}
