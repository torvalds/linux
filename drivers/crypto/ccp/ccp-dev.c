/*
 * AMD Cryptographic Coprocessor (CCP) driver
 *
 * Copyright (C) 2013 Advanced Micro Devices, Inc.
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


static struct ccp_device *ccp_dev;
static inline struct ccp_device *ccp_get_device(void)
{
	return ccp_dev;
}

static inline void ccp_add_device(struct ccp_device *ccp)
{
	ccp_dev = ccp;
}

static inline void ccp_del_device(struct ccp_device *ccp)
{
	ccp_dev = NULL;
}

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

static int ccp_cmd_queue_thread(void *data)
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

static int ccp_trng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	struct ccp_device *ccp = container_of(rng, struct ccp_device, hwrng);
	u32 trng_value;
	int len = min_t(int, sizeof(trng_value), max);

	/*
	 * Locking is provided by the caller so we can update device
	 * hwrng-related fields safely
	 */
	trng_value = ioread32(ccp->io_regs + TRNG_OUT_REG);
	if (!trng_value) {
		/* Zero is returned if not data is available or if a
		 * bad-entropy error is present. Assume an error if
		 * we exceed TRNG_RETRIES reads of zero.
		 */
		if (ccp->hwrng_retries++ > TRNG_RETRIES)
			return -EIO;

		return 0;
	}

	/* Reset the counter and save the rng value */
	ccp->hwrng_retries = 0;
	memcpy(data, &trng_value, len);

	return len;
}

/**
 * ccp_alloc_struct - allocate and initialize the ccp_device struct
 *
 * @dev: device struct of the CCP
 */
struct ccp_device *ccp_alloc_struct(struct device *dev)
{
	struct ccp_device *ccp;

	ccp = kzalloc(sizeof(*ccp), GFP_KERNEL);
	if (ccp == NULL) {
		dev_err(dev, "unable to allocate device struct\n");
		return NULL;
	}
	ccp->dev = dev;

	INIT_LIST_HEAD(&ccp->cmd);
	INIT_LIST_HEAD(&ccp->backlog);

	spin_lock_init(&ccp->cmd_lock);
	mutex_init(&ccp->req_mutex);
	mutex_init(&ccp->ksb_mutex);
	ccp->ksb_count = KSB_COUNT;
	ccp->ksb_start = 0;

	return ccp;
}

/**
 * ccp_init - initialize the CCP device
 *
 * @ccp: ccp_device struct
 */
int ccp_init(struct ccp_device *ccp)
{
	struct device *dev = ccp->dev;
	struct ccp_cmd_queue *cmd_q;
	struct dma_pool *dma_pool;
	char dma_pool_name[MAX_DMAPOOL_NAME_LEN];
	unsigned int qmr, qim, i;
	int ret;

	/* Find available queues */
	qim = 0;
	qmr = ioread32(ccp->io_regs + Q_MASK_REG);
	for (i = 0; i < MAX_HW_QUEUES; i++) {
		if (!(qmr & (1 << i)))
			continue;

		/* Allocate a dma pool for this queue */
		snprintf(dma_pool_name, sizeof(dma_pool_name), "ccp_q%d", i);
		dma_pool = dma_pool_create(dma_pool_name, dev,
					   CCP_DMAPOOL_MAX_SIZE,
					   CCP_DMAPOOL_ALIGN, 0);
		if (!dma_pool) {
			dev_err(dev, "unable to allocate dma pool\n");
			ret = -ENOMEM;
			goto e_pool;
		}

		cmd_q = &ccp->cmd_q[ccp->cmd_q_count];
		ccp->cmd_q_count++;

		cmd_q->ccp = ccp;
		cmd_q->id = i;
		cmd_q->dma_pool = dma_pool;

		/* Reserve 2 KSB regions for the queue */
		cmd_q->ksb_key = KSB_START + ccp->ksb_start++;
		cmd_q->ksb_ctx = KSB_START + ccp->ksb_start++;
		ccp->ksb_count -= 2;

		/* Preset some register values and masks that are queue
		 * number dependent
		 */
		cmd_q->reg_status = ccp->io_regs + CMD_Q_STATUS_BASE +
				    (CMD_Q_STATUS_INCR * i);
		cmd_q->reg_int_status = ccp->io_regs + CMD_Q_INT_STATUS_BASE +
					(CMD_Q_STATUS_INCR * i);
		cmd_q->int_ok = 1 << (i * 2);
		cmd_q->int_err = 1 << ((i * 2) + 1);

		cmd_q->free_slots = CMD_Q_DEPTH(ioread32(cmd_q->reg_status));

		init_waitqueue_head(&cmd_q->int_queue);

		/* Build queue interrupt mask (two interrupts per queue) */
		qim |= cmd_q->int_ok | cmd_q->int_err;

#ifdef CONFIG_ARM64
		/* For arm64 set the recommended queue cache settings */
		iowrite32(CACHE_WB_NO_ALLOC, ccp->io_regs + CMD_Q_CACHE_BASE +
			  (CMD_Q_CACHE_INC * i));
#endif

		dev_dbg(dev, "queue #%u available\n", i);
	}
	if (ccp->cmd_q_count == 0) {
		dev_notice(dev, "no command queues available\n");
		ret = -EIO;
		goto e_pool;
	}
	dev_notice(dev, "%u command queues available\n", ccp->cmd_q_count);

	/* Disable and clear interrupts until ready */
	iowrite32(0x00, ccp->io_regs + IRQ_MASK_REG);
	for (i = 0; i < ccp->cmd_q_count; i++) {
		cmd_q = &ccp->cmd_q[i];

		ioread32(cmd_q->reg_int_status);
		ioread32(cmd_q->reg_status);
	}
	iowrite32(qim, ccp->io_regs + IRQ_STATUS_REG);

	/* Request an irq */
	ret = ccp->get_irq(ccp);
	if (ret) {
		dev_err(dev, "unable to allocate an IRQ\n");
		goto e_pool;
	}

	/* Initialize the queues used to wait for KSB space and suspend */
	init_waitqueue_head(&ccp->ksb_queue);
	init_waitqueue_head(&ccp->suspend_queue);

	/* Create a kthread for each queue */
	for (i = 0; i < ccp->cmd_q_count; i++) {
		struct task_struct *kthread;

		cmd_q = &ccp->cmd_q[i];

		kthread = kthread_create(ccp_cmd_queue_thread, cmd_q,
					 "ccp-q%u", cmd_q->id);
		if (IS_ERR(kthread)) {
			dev_err(dev, "error creating queue thread (%ld)\n",
				PTR_ERR(kthread));
			ret = PTR_ERR(kthread);
			goto e_kthread;
		}

		cmd_q->kthread = kthread;
		wake_up_process(kthread);
	}

	/* Register the RNG */
	ccp->hwrng.name = "ccp-rng";
	ccp->hwrng.read = ccp_trng_read;
	ret = hwrng_register(&ccp->hwrng);
	if (ret) {
		dev_err(dev, "error registering hwrng (%d)\n", ret);
		goto e_kthread;
	}

	/* Make the device struct available before enabling interrupts */
	ccp_add_device(ccp);

	/* Enable interrupts */
	iowrite32(qim, ccp->io_regs + IRQ_MASK_REG);

	return 0;

e_kthread:
	for (i = 0; i < ccp->cmd_q_count; i++)
		if (ccp->cmd_q[i].kthread)
			kthread_stop(ccp->cmd_q[i].kthread);

	ccp->free_irq(ccp);

e_pool:
	for (i = 0; i < ccp->cmd_q_count; i++)
		dma_pool_destroy(ccp->cmd_q[i].dma_pool);

	return ret;
}

/**
 * ccp_destroy - tear down the CCP device
 *
 * @ccp: ccp_device struct
 */
void ccp_destroy(struct ccp_device *ccp)
{
	struct ccp_cmd_queue *cmd_q;
	struct ccp_cmd *cmd;
	unsigned int qim, i;

	/* Remove general access to the device struct */
	ccp_del_device(ccp);

	/* Unregister the RNG */
	hwrng_unregister(&ccp->hwrng);

	/* Stop the queue kthreads */
	for (i = 0; i < ccp->cmd_q_count; i++)
		if (ccp->cmd_q[i].kthread)
			kthread_stop(ccp->cmd_q[i].kthread);

	/* Build queue interrupt mask (two interrupt masks per queue) */
	qim = 0;
	for (i = 0; i < ccp->cmd_q_count; i++) {
		cmd_q = &ccp->cmd_q[i];
		qim |= cmd_q->int_ok | cmd_q->int_err;
	}

	/* Disable and clear interrupts */
	iowrite32(0x00, ccp->io_regs + IRQ_MASK_REG);
	for (i = 0; i < ccp->cmd_q_count; i++) {
		cmd_q = &ccp->cmd_q[i];

		ioread32(cmd_q->reg_int_status);
		ioread32(cmd_q->reg_status);
	}
	iowrite32(qim, ccp->io_regs + IRQ_STATUS_REG);

	ccp->free_irq(ccp);

	for (i = 0; i < ccp->cmd_q_count; i++)
		dma_pool_destroy(ccp->cmd_q[i].dma_pool);

	/* Flush the cmd and backlog queue */
	while (!list_empty(&ccp->cmd)) {
		/* Invoke the callback directly with an error code */
		cmd = list_first_entry(&ccp->cmd, struct ccp_cmd, entry);
		list_del(&cmd->entry);
		cmd->callback(cmd->data, -ENODEV);
	}
	while (!list_empty(&ccp->backlog)) {
		/* Invoke the callback directly with an error code */
		cmd = list_first_entry(&ccp->backlog, struct ccp_cmd, entry);
		list_del(&cmd->entry);
		cmd->callback(cmd->data, -ENODEV);
	}
}

/**
 * ccp_irq_handler - handle interrupts generated by the CCP device
 *
 * @irq: the irq associated with the interrupt
 * @data: the data value supplied when the irq was created
 */
irqreturn_t ccp_irq_handler(int irq, void *data)
{
	struct device *dev = data;
	struct ccp_device *ccp = dev_get_drvdata(dev);
	struct ccp_cmd_queue *cmd_q;
	u32 q_int, status;
	unsigned int i;

	status = ioread32(ccp->io_regs + IRQ_STATUS_REG);

	for (i = 0; i < ccp->cmd_q_count; i++) {
		cmd_q = &ccp->cmd_q[i];

		q_int = status & (cmd_q->int_ok | cmd_q->int_err);
		if (q_int) {
			cmd_q->int_status = status;
			cmd_q->q_status = ioread32(cmd_q->reg_status);
			cmd_q->q_int_status = ioread32(cmd_q->reg_int_status);

			/* On error, only save the first error value */
			if ((q_int & cmd_q->int_err) && !cmd_q->cmd_error)
				cmd_q->cmd_error = CMD_Q_ERROR(cmd_q->q_status);

			cmd_q->int_rcvd = 1;

			/* Acknowledge the interrupt and wake the kthread */
			iowrite32(q_int, ccp->io_regs + IRQ_STATUS_REG);
			wake_up_interruptible(&cmd_q->int_queue);
		}
	}

	return IRQ_HANDLED;
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

#ifdef CONFIG_X86
static const struct x86_cpu_id ccp_support[] = {
	{ X86_VENDOR_AMD, 22, },
};
#endif

static int __init ccp_mod_init(void)
{
#ifdef CONFIG_X86
	struct cpuinfo_x86 *cpuinfo = &boot_cpu_data;
	int ret;

	if (!x86_match_cpu(ccp_support))
		return -ENODEV;

	switch (cpuinfo->x86) {
	case 22:
		if ((cpuinfo->x86_model < 48) || (cpuinfo->x86_model > 63))
			return -ENODEV;

		ret = ccp_pci_init();
		if (ret)
			return ret;

		/* Don't leave the driver loaded if init failed */
		if (!ccp_get_device()) {
			ccp_pci_exit();
			return -ENODEV;
		}

		return 0;

		break;
	}
#endif

#ifdef CONFIG_ARM64
	int ret;

	ret = ccp_platform_init();
	if (ret)
		return ret;

	/* Don't leave the driver loaded if init failed */
	if (!ccp_get_device()) {
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
	struct cpuinfo_x86 *cpuinfo = &boot_cpu_data;

	switch (cpuinfo->x86) {
	case 22:
		ccp_pci_exit();
		break;
	}
#endif

#ifdef CONFIG_ARM64
	ccp_platform_exit();
#endif
}

module_init(ccp_mod_init);
module_exit(ccp_mod_exit);
