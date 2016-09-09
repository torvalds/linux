/*
 * AMD Cryptographic Coprocessor (CCP) driver
 *
 * Copyright (C) 2013,2016 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/hw_random.h>
#include <linux/cpu.h>
#ifdef CONFIG_X86
#include <asm/cpu_device_id.h>
#endif
#include <linux/ccp.h>

#include "ccp-dev.h"

MODULE_AUTHOR("Tom Lendacky <thomas.lendacky@amd.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");
MODULE_DESCRIPTION("AMD Cryptographic Coprocessor driver");

struct ccp_tasklet_data {
	struct completion completion;
	struct ccp_cmd *cmd;
};

/* List of CCPs, CCP count, read-write access lock, and access functions
 *
 * Lock structure: get ccp_unit_lock for reading whenever we need to
 * examine the CCP list. While holding it for reading we can acquire
 * the RR lock to update the round-robin next-CCP pointer. The unit lock
 * must be acquired before the RR lock.
 *
 * If the unit-lock is acquired for writing, we have total control over
 * the list, so there's no value in getting the RR lock.
 */
static DEFINE_RWLOCK(ccp_unit_lock);
static LIST_HEAD(ccp_units);

/* Round-robin counter */
static DEFINE_SPINLOCK(ccp_rr_lock);
static struct ccp_device *ccp_rr;

/* Ever-increasing value to produce unique unit numbers */
static atomic_t ccp_unit_ordinal;
unsigned int ccp_increment_unit_ordinal(void)
{
	return atomic_inc_return(&ccp_unit_ordinal);
}

/**
 * ccp_add_device - add a CCP device to the list
 *
 * @ccp: ccp_device struct pointer
 *
 * Put this CCP on the unit list, which makes it available
 * for use.
 *
 * Returns zero if a CCP device is present, -ENODEV otherwise.
 */
void ccp_add_device(struct ccp_device *ccp)
{
	unsigned long flags;

	write_lock_irqsave(&ccp_unit_lock, flags);
	list_add_tail(&ccp->entry, &ccp_units);
	if (!ccp_rr)
		/* We already have the list lock (we're first) so this
		 * pointer can't change on us. Set its initial value.
		 */
		ccp_rr = ccp;
	write_unlock_irqrestore(&ccp_unit_lock, flags);
}

/**
 * ccp_del_device - remove a CCP device from the list
 *
 * @ccp: ccp_device struct pointer
 *
 * Remove this unit from the list of devices. If the next device
 * up for use is this one, adjust the pointer. If this is the last
 * device, NULL the pointer.
 */
void ccp_del_device(struct ccp_device *ccp)
{
	unsigned long flags;

	write_lock_irqsave(&ccp_unit_lock, flags);
	if (ccp_rr == ccp) {
		/* ccp_unit_lock is read/write; any read access
		 * will be suspended while we make changes to the
		 * list and RR pointer.
		 */
		if (list_is_last(&ccp_rr->entry, &ccp_units))
			ccp_rr = list_first_entry(&ccp_units, struct ccp_device,
						  entry);
		else
			ccp_rr = list_next_entry(ccp_rr, entry);
	}
	list_del(&ccp->entry);
	if (list_empty(&ccp_units))
		ccp_rr = NULL;
	write_unlock_irqrestore(&ccp_unit_lock, flags);
}

static struct ccp_device *ccp_get_device(void)
{
	unsigned long flags;
	struct ccp_device *dp = NULL;

	/* We round-robin through the unit list.
	 * The (ccp_rr) pointer refers to the next unit to use.
	 */
	read_lock_irqsave(&ccp_unit_lock, flags);
	if (!list_empty(&ccp_units)) {
		spin_lock(&ccp_rr_lock);
		dp = ccp_rr;
		if (list_is_last(&ccp_rr->entry, &ccp_units))
			ccp_rr = list_first_entry(&ccp_units, struct ccp_device,
						  entry);
		else
			ccp_rr = list_next_entry(ccp_rr, entry);
		spin_unlock(&ccp_rr_lock);
	}
	read_unlock_irqrestore(&ccp_unit_lock, flags);

	return dp;
}

/**
 * ccp_present - check if a CCP device is present
 *
 * Returns zero if a CCP device is present, -ENODEV otherwise.
 */
int ccp_present(void)
{
	unsigned long flags;
	int ret;

	read_lock_irqsave(&ccp_unit_lock, flags);
	ret = list_empty(&ccp_units);
	read_unlock_irqrestore(&ccp_unit_lock, flags);

	return ret ? -ENODEV : 0;
}
EXPORT_SYMBOL_GPL(ccp_present);

/**
 * ccp_version - get the version of the CCP device
 *
 * Returns the version from the first unit on the list;
 * otherwise a zero if no CCP device is present
 */
unsigned int ccp_version(void)
{
	struct ccp_device *dp;
	unsigned long flags;
	int ret = 0;

	read_lock_irqsave(&ccp_unit_lock, flags);
	if (!list_empty(&ccp_units)) {
		dp = list_first_entry(&ccp_units, struct ccp_device, entry);
		ret = dp->vdata->version;
	}
	read_unlock_irqrestore(&ccp_unit_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(ccp_version);

/**
 * ccp_enqueue_cmd - queue an operation for processing by the CCP
 *
 * @cmd: ccp_cmd struct to be processed
 *
 * Queue a cmd to be processed by the CCP. If queueing the cmd
 * would exceed the defined length of the cmd queue the cmd will
 * only be queued if the CCP_CMD_MAY_BACKLOG flag is set and will
 * result in a return code of -EBUSY.
 *
 * The callback routine specified in the ccp_cmd struct will be
 * called to notify the caller of completion (if the cmd was not
 * backlogged) or advancement out of the backlog. If the cmd has
 * advanced out of the backlog the "err" value of the callback
 * will be -EINPROGRESS. Any other "err" value during callback is
 * the result of the operation.
 *
 * The cmd has been successfully queued if:
 *   the return code is -EINPROGRESS or
 *   the return code is -EBUSY and CCP_CMD_MAY_BACKLOG flag is set
 */
int ccp_enqueue_cmd(struct ccp_cmd *cmd)
{
	struct ccp_device *ccp = ccp_get_device();
	unsigned long flags;
	unsigned int i;
	int ret;

	if (!ccp)
		return -ENODEV;

	/* Caller must supply a callback routine */
	if (!cmd->callback)
		return -EINVAL;

	cmd->ccp = ccp;

	spin_lock_irqsave(&ccp->cmd_lock, flags);

	i = ccp->cmd_q_count;

	if (ccp->cmd_count >= MAX_CMD_QLEN) {
		ret = -EBUSY;
		if (cmd->flags & CCP_CMD_MAY_BACKLOG)
			list_add_tail(&cmd->entry, &ccp->backlog);
	} else {
		ret = -EINPROGRESS;
		ccp->cmd_count++;
		list_add_tail(&cmd->entry, &ccp->cmd);

		/* Find an idle queue */
		if (!ccp->suspending) {
			for (i = 0; i < ccp->cmd_q_count; i++) {
				if (ccp->cmd_q[i].active)
					continue;

				break;
			}
		}
	}

	spin_unlock_irqrestore(&ccp->cmd_lock, flags);

	/* If we found an idle queue, wake it up */
	if (i < ccp->cmd_q_count)
		wake_up_process(ccp->cmd_q[i].kthread);

	return ret;
}
EXPORT_SYMBOL_GPL(ccp_enqueue_cmd);

static void ccp_do_cmd_backlog(struct work_struct *work)
{
	struct ccp_cmd *cmd = container_of(work, struct ccp_cmd, work);
	struct ccp_device *ccp = cmd->ccp;
	unsigned long flags;
	unsigned int i;

	cmd->callback(cmd->data, -EINPROGRESS);

	spin_lock_irqsave(&ccp->cmd_lock, flags);

	ccp->cmd_count++;
	list_add_tail(&cmd->entry, &ccp->cmd);

	/* Find an idle queue */
	for (i = 0; i < ccp->cmd_q_count; i++) {
		if (ccp->cmd_q[i].active)
			continue;

		break;
	}

	spin_unlock_irqrestore(&ccp->cmd_lock, flags);

	/* If we found an idle queue, wake it up */
	if (i < ccp->cmd_q_count)
		wake_up_process(ccp->cmd_q[i].kthread);
}

static struct ccp_cmd *ccp_dequeue_cmd(struct ccp_cmd_queue *cmd_q)
{
	struct ccp_device *ccp = cmd_q->ccp;
	struct ccp_cmd *cmd = NULL;
	struct ccp_cmd *backlog = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ccp->cmd_lock, flags);

	cmd_q->active = 0;

	if (ccp->suspending) {
		cmd_q->suspended = 1;

		spin_unlock_irqrestore(&ccp->cmd_lock, flags);
		wake_up_interruptible(&ccp->suspend_queue);

		return NULL;
	}

	if (ccp->cmd_count) {
		cmd_q->active = 1;

		cmd = list_first_entry(&ccp->cmd, struct ccp_cmd, entry);
		list_del(&cmd->entry);

		ccp->cmd_count--;
	}

	if (!list_empty(&ccp->backlog)) {
		backlog = list_first_entry(&ccp->backlog, struct ccp_cmd,
					   entry);
		list_del(&backlog->entry);
	}

	spin_unlock_irqrestore(&ccp->cmd_lock, flags);

	if (backlog) {
		INIT_WORK(&backlog->work, ccp_do_cmd_backlog);
		schedule_work(&backlog->work);
	}

	return cmd;
}

static void ccp_do_cmd_complete(unsigned long data)
{
	struct ccp_tasklet_data *tdata = (struct ccp_tasklet_data *)data;
	struct ccp_cmd *cmd = tdata->cmd;

	cmd->callback(cmd->data, cmd->ret);
	complete(&tdata->completion);
}

/**
 * ccp_cmd_queue_thread - create a kernel thread to manage a CCP queue
 *
 * @data: thread-specific data
 */
int ccp_cmd_queue_thread(void *data)
{
	struct ccp_cmd_queue *cmd_q = (struct ccp_cmd_queue *)data;
	struct ccp_cmd *cmd;
	struct ccp_tasklet_data tdata;
	struct tasklet_struct tasklet;

	tasklet_init(&tasklet, ccp_do_cmd_complete, (unsigned long)&tdata);

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();

		set_current_state(TASK_INTERRUPTIBLE);

		cmd = ccp_dequeue_cmd(cmd_q);
		if (!cmd)
			continue;

		__set_current_state(TASK_RUNNING);

		/* Execute the command */
		cmd->ret = ccp_run_cmd(cmd_q, cmd);

		/* Schedule the completion callback */
		tdata.cmd = cmd;
		init_completion(&tdata.completion);
		tasklet_schedule(&tasklet);
		wait_for_completion(&tdata.completion);
	}

	__set_current_state(TASK_RUNNING);

	return 0;
}

/**
 * ccp_alloc_struct - allocate and initialize the ccp_device struct
 *
 * @dev: device struct of the CCP
 */
struct ccp_device *ccp_alloc_struct(struct device *dev)
{
	struct ccp_device *ccp;

	ccp = devm_kzalloc(dev, sizeof(*ccp), GFP_KERNEL);
	if (!ccp)
		return NULL;
	ccp->dev = dev;

	INIT_LIST_HEAD(&ccp->cmd);
	INIT_LIST_HEAD(&ccp->backlog);

	spin_lock_init(&ccp->cmd_lock);
	mutex_init(&ccp->req_mutex);
	mutex_init(&ccp->ksb_mutex);
	ccp->ksb_count = KSB_COUNT;
	ccp->ksb_start = 0;

	ccp->ord = ccp_increment_unit_ordinal();
	snprintf(ccp->name, MAX_CCP_NAME_LEN, "ccp-%u", ccp->ord);
	snprintf(ccp->rngname, MAX_CCP_NAME_LEN, "ccp-%u-rng", ccp->ord);

	return ccp;
}

#ifdef CONFIG_PM
bool ccp_queues_suspended(struct ccp_device *ccp)
{
	unsigned int suspended = 0;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&ccp->cmd_lock, flags);

	for (i = 0; i < ccp->cmd_q_count; i++)
		if (ccp->cmd_q[i].suspended)
			suspended++;

	spin_unlock_irqrestore(&ccp->cmd_lock, flags);

	return ccp->cmd_q_count == suspended;
}
#endif

static int __init ccp_mod_init(void)
{
#ifdef CONFIG_X86
	int ret;

	ret = ccp_pci_init();
	if (ret)
		return ret;

	/* Don't leave the driver loaded if init failed */
	if (ccp_present() != 0) {
		ccp_pci_exit();
		return -ENODEV;
	}

	return 0;
#endif

#ifdef CONFIG_ARM64
	int ret;

	ret = ccp_platform_init();
	if (ret)
		return ret;

	/* Don't leave the driver loaded if init failed */
	if (ccp_present() != 0) {
		ccp_platform_exit();
		return -ENODEV;
	}

	return 0;
#endif

	return -ENODEV;
}

static void __exit ccp_mod_exit(void)
{
#ifdef CONFIG_X86
	ccp_pci_exit();
#endif

#ifdef CONFIG_ARM64
	ccp_platform_exit();
#endif
}

module_init(ccp_mod_init);
module_exit(ccp_mod_exit);
