// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *	Randy Li, randy.li@rock-chips.com
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/pm_runtime.h>
#include <linux/poll.h>
#include <linux/regmap.h>
#include <linux/rwsem.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <soc/rockchip/pm_domains.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#define MPP_TIMEOUT_DELAY		(2000)

#define MPP_SESSION_MAX_DONE_TASK	(20)

#ifdef CONFIG_COMPAT
struct compat_mpp_request {
	compat_uptr_t req;
	u32 size;
};
#endif

static void mpp_task_try_run(struct work_struct *work_s);

/* task queue schedule */
static int
mpp_taskqueue_push_pending(struct mpp_taskqueue *queue,
			   struct mpp_task *task)
{
	mutex_lock(&queue->list_lock);
	list_add_tail(&task->service_link, &queue->pending);
	mutex_unlock(&queue->list_lock);

	return 0;
}

static struct mpp_task *
mpp_taskqueue_get_pending_task(struct mpp_taskqueue *queue)
{
	struct mpp_task *task = NULL;

	if (!list_empty(&queue->pending)) {
		task = list_first_entry(&queue->pending,
					struct mpp_task,
					service_link);
	}

	return task;
}

static int
mpp_taskqueue_is_running(struct mpp_taskqueue *queue)
{
	return atomic_read(&queue->running);
}

static int
mpp_taskqueue_wait_to_run(struct mpp_taskqueue *queue,
			  struct mpp_task *task)
{
	atomic_inc(&queue->running);
	queue->cur_task = task;
	mutex_lock(&queue->list_lock);
	list_del_init(&task->service_link);
	mutex_unlock(&queue->list_lock);

	return 0;
}

static struct mpp_task *
mpp_taskqueue_get_cur_task(struct mpp_taskqueue *queue)
{
	return queue->cur_task;
}

static int
mpp_taskqueue_done(struct mpp_taskqueue *queue,
		   struct mpp_task *task)
{
	queue->cur_task = NULL;
	atomic_set(&queue->running, 0);

	return 0;
}

static int
mpp_taskqueue_abort(struct mpp_taskqueue *queue,
		    struct mpp_task *task)
{
	if (task) {
		if (queue->cur_task == task)
			queue->cur_task = NULL;
	}
	atomic_set(&queue->running, 0);

	return 0;
}

static int mpp_power_on(struct mpp_dev *mpp)
{
	pm_runtime_get_sync(mpp->dev);
	pm_stay_awake(mpp->dev);

	if (mpp->hw_ops->power_on)
		mpp->hw_ops->power_on(mpp);

	return 0;
}

static int mpp_power_off(struct mpp_dev *mpp)
{
	if (mpp->hw_ops->power_off)
		mpp->hw_ops->power_off(mpp);

	pm_runtime_mark_last_busy(mpp->dev);
	pm_runtime_put_autosuspend(mpp->dev);
	pm_relax(mpp->dev);

	return 0;
}

static void *
mpp_fd_to_mem_region(struct mpp_dev *mpp,
		     struct mpp_dma_session *dma, int fd)
{
	struct mpp_dma_buffer *buffer = NULL;
	struct mpp_mem_region *mem_region = NULL;

	if (fd <= 0 || !dma || !mpp)
		return ERR_PTR(-EINVAL);

	down_read(&mpp->rw_sem);
	buffer = mpp_dma_import_fd(mpp->iommu_info, dma, fd);
	up_read(&mpp->rw_sem);
	if (IS_ERR_OR_NULL(buffer)) {
		mpp_err("can't import dma-buf %d\n", fd);
		return ERR_PTR(-EINVAL);
	}

	mem_region = kzalloc(sizeof(*mem_region), GFP_KERNEL);
	if (!mem_region) {
		down_read(&mpp->rw_sem);
		mpp_dma_release_fd(dma, fd);
		up_read(&mpp->rw_sem);
		return ERR_PTR(-ENOMEM);
	}

	mem_region->hdl = (void *)(long)fd;
	mem_region->iova = buffer->iova;
	mem_region->len = buffer->size;

	return mem_region;
}

static int
mpp_session_push_pending(struct mpp_session *session,
			 struct mpp_task *task)
{
	mutex_lock(&session->list_lock);
	list_add_tail(&task->session_link, &session->pending);
	mutex_unlock(&session->list_lock);

	return 0;
}

static int mpp_session_push_done(struct mpp_task *task)
{
	struct mpp_session *session = NULL;

	session = task->session;

	mutex_lock(&session->list_lock);
	list_del_init(&task->session_link);
	mutex_unlock(&session->list_lock);

	kfifo_in(&session->done_fifo, &task, 1);
	wake_up(&session->wait);

	return 0;
}

static struct mpp_task *
mpp_session_pull_done(struct mpp_session *session)
{
	struct mpp_task *task = NULL;

	if (kfifo_out(&session->done_fifo, &task, 1))
		return task;
	return NULL;
}

static struct mpp_task *
mpp_alloc_task(struct mpp_dev *mpp,
	       struct mpp_session *session,
	       void __user *src, u32 size)
{
	struct mpp_task *task = NULL;

	if (mpp->dev_ops->alloc_task)
		task = mpp->dev_ops->alloc_task(session, src, size);

	if (task && mpp->hw_ops->get_freq)
		mpp->hw_ops->get_freq(mpp, task);

	return task;
}

static int
mpp_free_task(struct mpp_session *session,
	      struct mpp_task *task)
{
	struct mpp_dev *mpp = session->mpp;

	if (mpp->dev_ops->free_task)
		mpp->dev_ops->free_task(session, task);
	return 0;
}

static int mpp_refresh_pm_runtime(struct device *dev)
{
	struct device_link *link;

	rcu_read_lock();

	list_for_each_entry_rcu(link, &dev->links.suppliers, c_node)
		pm_runtime_put_sync(link->supplier);

	list_for_each_entry_rcu(link, &dev->links.suppliers, c_node)
		pm_runtime_get_sync(link->supplier);

	rcu_read_unlock();

	return 0;
}

static int mpp_dev_reset(struct mpp_dev *mpp)
{
	dev_info(mpp->dev, "resetting...\n");

	/*
	 * before running, we have to switch grf ctrl bit to ensure
	 * working in current hardware
	 */
	mpp_set_grf(mpp->grf_info);

	if (mpp->hw_ops->reduce_freq)
		mpp->hw_ops->reduce_freq(mpp);
	/* FIXME lock resource lock of the other devices in combo */
	down_write(&mpp->rw_sem);
	atomic_set(&mpp->reset_request, 0);
	mpp_iommu_detach(mpp->iommu_info);

	rockchip_save_qos(mpp->dev);
	if (mpp->hw_ops->reset)
		mpp->hw_ops->reset(mpp);
	rockchip_restore_qos(mpp->dev);

	/* Note: if the domain does not change, iommu attach will be return
	 * as an empty operation. Therefore, force to close and then open,
	 * will be update the domain. In this way, domain can really attach.
	 */
	mpp_refresh_pm_runtime(mpp->dev);

	mpp_iommu_attach(mpp->iommu_info);
	up_write(&mpp->rw_sem);

	dev_info(mpp->dev, "reset done\n");

	return 0;
}

static int mpp_dev_abort(struct mpp_dev *mpp)
{
	int ret;

	mpp_debug_enter();

	mpp_dev_reset(mpp);
	/* destroy the current task after hardware reset */
	ret = mpp_taskqueue_is_running(mpp->queue);
	if (ret) {
		struct mpp_task *task = NULL;

		task = mpp_taskqueue_get_cur_task(mpp->queue);
		mutex_lock(&task->session->list_lock);
		list_del_init(&task->session_link);
		mutex_unlock(&task->session->list_lock);
		mpp_taskqueue_abort(mpp->queue, task);
		atomic_dec(&task->session->task_running);
		mpp_free_task(task->session, task);
	} else {
		mpp_taskqueue_abort(mpp->queue, NULL);
	}

	mpp_debug_leave();

	return ret;
}

static int mpp_task_run(struct mpp_dev *mpp,
			struct mpp_task *task)
{
	int ret;

	mpp_debug_enter();

	/*
	 * before running, we have to switch grf ctrl bit to ensure
	 * working in current hardware
	 */
	mpp_set_grf(mpp->grf_info);
	/*
	 * for iommu share hardware, should attach to ensure
	 * working in current device
	 */
	ret = mpp_iommu_attach(mpp->iommu_info);
	if (ret) {
		dev_err(mpp->dev, "mpp_iommu_attach failed\n");
		return -ENODATA;
	}

	mpp_power_on(mpp);
	mpp_time_record(task);
	mpp_debug(DEBUG_TASK_INFO, "pid %d, start hw %s\n",
		  task->session->pid, dev_name(mpp->dev));

	if (mpp->hw_ops->set_freq)
		mpp->hw_ops->set_freq(mpp, task);
	/*
	 * TODO: Lock the reader locker of the device resource lock here,
	 * release at the finish operation
	 */
	if (mpp->dev_ops->run)
		mpp->dev_ops->run(mpp, task);

	mpp_debug_leave();

	return 0;
}

static void mpp_task_try_run(struct work_struct *work_s)
{
	int ret;
	struct mpp_task *task;
	struct mpp_dev *mpp;
	struct mpp_taskqueue *queue = container_of(work_s,
						   struct mpp_taskqueue,
						   work);

	mpp_debug_enter();

	ret = mpp_taskqueue_is_running(queue);
	if (ret)
		goto done;

	task = mpp_taskqueue_get_pending_task(queue);
	if (!task)
		goto done;
	/* get device for current task */
	mpp = task->session->mpp;
	if (atomic_read(&mpp->reset_request) > 0)
		goto done;
	/*
	 * In the link table mode, the prepare function of the device
	 * will check whether I can insert a new task into device.
	 * If the device supports the task status query(like the HEVC
	 * encoder), it can report whether the device is busy.
	 * If the device does not support multiple task or task status
	 * query, leave this job to mpp service.
	 */
	if (mpp->dev_ops->prepare)
		mpp->dev_ops->prepare(mpp, task);
	/*
	 * FIXME if the hardware supports task query, but we still need to lock
	 * the running list and lock the mpp service in the current state.
	 */
	mpp_taskqueue_wait_to_run(mpp->queue, task);
	/* Push a pending task to running queue */
	mpp_task_run(mpp, task);
done:
	mpp_debug_leave();
}

static int mpp_session_clear(struct mpp_dev *mpp,
			     struct mpp_session *session)
{
	struct mpp_task *task, *n, *task_done;

	list_for_each_entry_safe(task, n,
				 &session->pending,
				 session_link) {
		mutex_lock(&task->session->list_lock);
		list_del_init(&task->session_link);
		mutex_unlock(&task->session->list_lock);
		mpp_free_task(session, task);
	}

	while (kfifo_out(&session->done_fifo, &task_done, 1))
		mpp_free_task(session, task_done);

	return 0;
}

static int mpp_task_result(struct mpp_dev *mpp,
			   struct mpp_task *task,
			   u32 __user *dst, u32 size)
{
	mpp_debug_enter();

	if (!mpp || !task)
		return -EINVAL;

	if (mpp->dev_ops->result)
		mpp->dev_ops->result(mpp, task, dst, size);

	mpp_free_task(task->session, task);

	mpp_debug_leave();

	return 0;
}

static int mpp_wait_result(struct mpp_session *session,
			   struct mpp_dev *mpp,
			   struct mpp_request req)
{
	int ret;
	struct mpp_task *task;

	ret = wait_event_timeout(session->wait,
				 !kfifo_is_empty(&session->done_fifo),
				 msecs_to_jiffies(MPP_TIMEOUT_DELAY));
	if (ret > 0) {
		ret = 0;
		task = mpp_session_pull_done(session);
		mpp_task_result(mpp, task, req.req, req.size);
	} else {
		mpp_err("error: pid %d wait %d task done timeout\n",
			session->pid, atomic_read(&session->task_running));
		ret = -ETIMEDOUT;
		mutex_lock(&mpp->reset_lock);
		mpp_dev_abort(mpp);
		mutex_unlock(&mpp->reset_lock);
	}
	atomic_dec(&mpp->total_running);
	mpp_power_off(mpp);

	return ret;
}

static int mpp_attach_service(struct mpp_dev *mpp, struct device *dev)
{
	u32 node = 0;
	struct device_node *np = NULL;
	struct platform_device *pdev = NULL;

	of_property_read_u32(dev->of_node,
			     "rockchip,taskqueue-node", &node);
	if (node >= MPP_DEVICE_BUTT) {
		dev_err(dev, "rockchip,taskqueue-node %d must less than %d\n",
			node, MPP_DEVICE_BUTT);
		return -ENODEV;
	}

	np = of_parse_phandle(dev->of_node, "rockchip,srv", 0);
	if (!np || !of_device_is_available(np)) {
		dev_err(dev, "failed to get the mpp service node\n");
		return -ENODEV;
	}

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		of_node_put(np);
		dev_err(dev, "failed to get mpp service from node\n");
		return -ENODEV;
	}

	device_lock(&pdev->dev);
	mpp->srv = platform_get_drvdata(pdev);
	if (mpp->srv) {
		/* register current device to mpp service */
		mpp->srv->sub_devices[mpp->var->device_type] = mpp;
		/* set taskqueue which set in dtsi */
		mpp->queue = mpp->srv->task_queues[node];
	} else {
		dev_err(&pdev->dev, "failed attach service\n");
		return -EINVAL;
	}
	device_unlock(&pdev->dev);

	put_device(&pdev->dev);
	of_node_put(np);

	return 0;
}

int mpp_taskqueue_init(struct mpp_taskqueue *queue,
		       struct mpp_service *srv)
{
	mutex_init(&queue->lock);
	mutex_init(&queue->list_lock);
	atomic_set(&queue->running, 0);
	INIT_LIST_HEAD(&queue->pending);
	INIT_WORK(&queue->work, mpp_task_try_run);

	queue->srv = srv;
	queue->cur_task = NULL;

	return 0;
}

long mpp_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct mpp_dev *mpp;
	struct mpp_service *srv;
	struct mpp_session *session =
		(struct mpp_session *)filp->private_data;

	mpp_debug_enter();
	if (!session)
		return -EINVAL;

	srv = session->srv;
	if (!srv)
		return -EINVAL;

	if (atomic_read(&srv->shutdown_request) > 0)
		return -EBUSY;

	mpp_debug(DEBUG_IOCTL, "cmd=%x\n", cmd);
	switch (cmd) {
	case MPP_IOC_SET_CLIENT_TYPE:
		session->device_type = (enum MPP_DEVICE_TYPE)(arg);
		mpp_debug(DEBUG_IOCTL, "pid %d set client type %d\n",
			  session->pid, session->device_type);

		mpp = srv->sub_devices[session->device_type];
		if (IS_ERR_OR_NULL(mpp)) {
			mpp_err("pid %d set client type %d failed\n",
				session->pid, session->device_type);
			return -EINVAL;
		}
		session->dma = mpp_dma_session_create(mpp->dev);
		session->dma->max_buffers = mpp->session_max_buffers;
		if (mpp->dev_ops->init_session)
			mpp->dev_ops->init_session(mpp);
		session->mpp = mpp;
		break;
	case MPP_IOC_SET_REG: {
		struct mpp_request req;
		struct mpp_task *task;

		mpp = session->mpp;
		if (IS_ERR_OR_NULL(mpp)) {
			mpp_err("pid %d not find clinet %d\n",
				session->pid, session->device_type);
			return -EINVAL;
		}
		mpp_debug(DEBUG_IOCTL, "pid %d set reg, client type %d\n",
			  session->pid, session->device_type);
		if (copy_from_user(&req, (void __user *)arg,
				   sizeof(req))) {
			mpp_err("error: set reg copy_from_user failed\n");
			return -EFAULT;
		}
		task = mpp_alloc_task(mpp, session,
				      (void __user *)req.req,
				      req.size);
		if (IS_ERR_OR_NULL(task))
			return -EFAULT;
		mpp_taskqueue_push_pending(mpp->queue, task);
		mpp_session_push_pending(session, task);
		atomic_inc(&session->task_running);
		atomic_inc(&mpp->total_running);
		/* trigger current queue to run task */
		mutex_lock(&mpp->queue->lock);
		queue_work(mpp->workq, &mpp->queue->work);
		mutex_unlock(&mpp->queue->lock);
	} break;
	case MPP_IOC_GET_REG: {
		struct mpp_request req;

		mpp = session->mpp;
		if (IS_ERR_OR_NULL(mpp)) {
			mpp_err("pid %d not find clinet %d\n",
				session->pid, session->device_type);
			return -EINVAL;
		}
		mpp_debug(DEBUG_IOCTL, "pid %d get reg, client type %d\n",
			  session->pid, session->device_type);
		if (copy_from_user(&req, (void __user *)arg,
				   sizeof(req))) {
			mpp_err("get reg copy_from_user failed\n");
			return -EFAULT;
		}

		return mpp_wait_result(session, mpp, req);
	} break;
	case MPP_IOC_PROBE_IOMMU_STATUS: {
		int iommu_enable = 1;

		mpp_debug(DEBUG_IOCTL, "MPP_IOC_PROBE_IOMMU_STATUS\n");

		if (put_user(iommu_enable, ((u32 __user *)arg))) {
			mpp_err("iommu status copy_to_user fail\n");
			return -EFAULT;
		}
		break;
	}
	case MPP_IOC_SET_DRIVER_DATA: {
		u32 val;

		mpp = session->mpp;
		if (IS_ERR_OR_NULL(mpp)) {
			mpp_err("pid %d not find clinet %d\n",
				session->pid, session->device_type);
			return -EINVAL;
		}
		if (copy_from_user(&val, (void __user *)arg,
				   sizeof(val))) {
			mpp_err("MPP_IOC_SET_DRIVER_DATA copy_from_user fail\n");
			return -EFAULT;
		}

		if (mpp->grf_info->grf)
			regmap_write(mpp->grf_info->grf, 0x5d8, val);
	} break;
	default: {
		mpp = session->mpp;
		if (IS_ERR_OR_NULL(mpp)) {
			mpp_err("pid %d not find clinet %d\n",
				session->pid, session->device_type);
			return -EINVAL;
		}
		if (mpp->dev_ops->ioctl)
			return mpp->dev_ops->ioctl(session, cmd, arg);

		mpp_err("unknown mpp ioctl cmd %x\n", cmd);
		return -ENOIOCTLCMD;
	} break;
	}

	mpp_debug_leave();

	return 0;
}

#ifdef CONFIG_COMPAT
#define MPP_IOC_SET_CLIENT_TYPE32          _IOW(MPP_IOC_MAGIC, 1, u32)
#define MPP_IOC_GET_HW_FUSE_STATUS32       _IOW(MPP_IOC_MAGIC, 2, \
						compat_ulong_t)
#define MPP_IOC_SET_REG32                  _IOW(MPP_IOC_MAGIC, 3, \
						compat_ulong_t)
#define MPP_IOC_GET_REG32                  _IOW(MPP_IOC_MAGIC, 4, \
						compat_ulong_t)
#define MPP_IOC_PROBE_IOMMU_STATUS32       _IOR(MPP_IOC_MAGIC, 5, u32)
#define MPP_IOC_SET_DRIVER_DATA32          _IOW(MPP_IOC_MAGIC, 64, u32)

static long native_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	long ret = -ENOIOCTLCMD;

	if (file->f_op->unlocked_ioctl)
		ret = file->f_op->unlocked_ioctl(file, cmd, arg);

	return ret;
}

long mpp_dev_compat_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct mpp_request req;
	void __user *up = compat_ptr(arg);
	int compatible_arg = 1;
	long err = 0;

	mpp_debug_enter();
	mpp_debug(DEBUG_IOCTL, "cmd %x, MPP_IOC_SET_CLIENT_TYPE32 %x\n",
		  cmd, (u32)MPP_IOC_SET_CLIENT_TYPE32);
	/* First, convert the command. */
	switch (cmd) {
	case MPP_IOC_SET_CLIENT_TYPE32:
		cmd = MPP_IOC_SET_CLIENT_TYPE;
		break;
	case MPP_IOC_GET_HW_FUSE_STATUS32:
		cmd = MPP_IOC_GET_HW_FUSE_STATUS;
		break;
	case MPP_IOC_SET_REG32:
		cmd = MPP_IOC_SET_REG;
		break;
	case MPP_IOC_GET_REG32:
		cmd = MPP_IOC_GET_REG;
		break;
	case MPP_IOC_PROBE_IOMMU_STATUS32:
		cmd = MPP_IOC_PROBE_IOMMU_STATUS;
		break;
	case MPP_IOC_SET_DRIVER_DATA32:
		cmd = MPP_IOC_SET_DRIVER_DATA;
		break;
	default:
		break;
	}
	switch (cmd) {
	case MPP_IOC_SET_REG:
	case MPP_IOC_GET_REG:
	case MPP_IOC_GET_HW_FUSE_STATUS: {
		compat_uptr_t req_ptr;
		struct compat_mpp_request __user *req32 = NULL;

		req32 = (struct compat_mpp_request __user *)up;
		memset(&req, 0, sizeof(req));

		if (get_user(req_ptr, &req32->req) ||
		    get_user(req.size, &req32->size)) {
			mpp_err("compat get hw status copy_from_user failed\n");
			return -EFAULT;
		}
		req.req = compat_ptr(req_ptr);
		compatible_arg = 0;
	} break;
	default:
		break;
	}

	if (compatible_arg) {
		err = native_ioctl(file, cmd, (unsigned long)up);
	} else {
		mm_segment_t old_fs = get_fs();

		set_fs(KERNEL_DS);
		err = native_ioctl(file, cmd, (unsigned long)&req);
		set_fs(old_fs);
	}

	mpp_debug_leave();
	return err;
}
#endif

int mpp_dev_open(struct inode *inode, struct file *filp)
{
	int ret;
	struct mpp_session *session = NULL;
	struct mpp_service *srv = container_of(inode->i_cdev,
					       struct mpp_service,
					       mpp_cdev);
	mpp_debug_enter();

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session)
		return -ENOMEM;

	session->srv = srv;
	session->pid = current->pid;

	mutex_init(&session->list_lock);
	INIT_LIST_HEAD(&session->pending);
	init_waitqueue_head(&session->wait);
	ret = kfifo_alloc(&session->done_fifo,
			  MPP_SESSION_MAX_DONE_TASK,
			  GFP_KERNEL);
	if (ret < 0) {
		ret = -ENOMEM;
		goto failed_kfifo;
	}

	atomic_set(&session->task_running, 0);
	filp->private_data = (void *)session;

	mpp_debug_leave();

	return nonseekable_open(inode, filp);

failed_kfifo:
	kfree(session);
	return ret;
}

int mpp_dev_release(struct inode *inode, struct file *filp)
{
	int task_running;
	struct mpp_dev *mpp;
	struct mpp_session *session = filp->private_data;

	mpp_debug_enter();

	if (!session) {
		mpp_err("session is null\n");
		return -EINVAL;
	}

	task_running = atomic_read(&session->task_running);
	if (task_running) {
		mpp_err("session %d still has %d task running when closing\n",
			session->pid, task_running);
		msleep(50);
	}
	wake_up(&session->wait);

	/* release device resource */
	mpp = session->mpp;
	if (mpp) {
		if (mpp->dev_ops->release_session)
			mpp->dev_ops->release_session(session);

		/* remove this filp from the asynchronusly notified filp's */
		mpp_session_clear(mpp, session);

		down_read(&mpp->rw_sem);
		mpp_dma_session_destroy(session->dma);
		up_read(&mpp->rw_sem);
	}

	kfifo_free(&session->done_fifo);
	kfree(session);
	filp->private_data = NULL;

	mpp_debug_leave();
	return 0;
}

unsigned int mpp_dev_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	struct mpp_session *session =
		(struct mpp_session *)filp->private_data;

	poll_wait(filp, &session->wait, wait);
	if (kfifo_len(&session->done_fifo))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

struct mpp_mem_region *
mpp_task_attach_fd(struct mpp_task *task, int fd)
{
	struct mpp_mem_region *mem_region = NULL;

	mem_region = mpp_fd_to_mem_region(task->session->mpp,
					  task->session->dma, fd);
	if (IS_ERR(mem_region))
		return mem_region;

	INIT_LIST_HEAD(&mem_region->reg_lnk);
	mutex_lock(&task->session->list_lock);
	list_add_tail(&mem_region->reg_lnk, &task->mem_region_list);
	mutex_unlock(&task->session->list_lock);

	return mem_region;
}

int mpp_translate_reg_address(struct mpp_dev *mpp,
			      struct mpp_task *task,
			      int fmt, u32 *reg)
{
	struct mpp_trans_info *trans_info = mpp->var->trans_info;
	const u8 *tbl = trans_info[fmt].table;
	int size = trans_info[fmt].count;
	int i;

	mpp_debug_enter();
	for (i = 0; i < size; i++) {
		struct mpp_mem_region *mem_region = NULL;
		int usr_fd = reg[tbl[i]] & 0x3FF;
		int offset = reg[tbl[i]] >> 10;

		if (usr_fd == 0)
			continue;

		mem_region = mpp_task_attach_fd(task, usr_fd);
		if (IS_ERR(mem_region)) {
			mpp_debug(DEBUG_IOMMU, "reg[%3d]: %08x failed\n",
				  tbl[i], reg[tbl[i]]);
			return PTR_ERR(mem_region);
		}

		mem_region->reg_idx = tbl[i];
		mpp_debug(DEBUG_IOMMU, "reg[%3d]: %3d => %pad + offset %10d\n",
			  tbl[i], usr_fd, &mem_region->iova, offset);
		reg[tbl[i]] = mem_region->iova + offset;
	}

	mpp_debug_leave();

	return 0;
}

int mpp_translate_extra_info(struct mpp_task *task,
			     struct extra_info_for_iommu *ext_inf,
			     u32 *reg)
{
	mpp_debug_enter();
	if (ext_inf) {
		int i;

		if (ext_inf->magic != EXTRA_INFO_MAGIC)
			return -EINVAL;

		for (i = 0; i < ext_inf->cnt; i++) {
			mpp_debug(DEBUG_IOMMU, "reg[%d] + offset %d\n",
				  ext_inf->elem[i].index,
				  ext_inf->elem[i].offset);
			reg[ext_inf->elem[i].index] += ext_inf->elem[i].offset;
		}
	}
	mpp_debug_leave();

	return 0;
}

int mpp_task_init(struct mpp_session *session,
		  struct mpp_task *task)
{
	INIT_LIST_HEAD(&task->session_link);
	INIT_LIST_HEAD(&task->service_link);
	INIT_LIST_HEAD(&task->mem_region_list);

	task->session = session;

	return 0;
}

int mpp_task_finish(struct mpp_session *session,
		    struct mpp_task *task)
{
	struct mpp_dev *mpp = session->mpp;

	mutex_lock(&mpp->queue->lock);

	if (mpp->dev_ops->finish)
		mpp->dev_ops->finish(mpp, task);
	atomic_dec(&task->session->task_running);

	/* FIXME lock resource lock of combo device */
	mutex_lock(&mpp->reset_lock);
	if (atomic_read(&mpp->reset_request) > 0)
		mpp_dev_reset(mpp);
	mutex_unlock(&mpp->reset_lock);

	mpp_taskqueue_done(mpp->queue, task);
	/* Wake up the GET thread */
	mpp_session_push_done(task);
	/* trigger current queue to run next task */
	queue_work(mpp->workq, &mpp->queue->work);

	mutex_unlock(&mpp->queue->lock);

	return 0;
}

int mpp_task_finalize(struct mpp_session *session,
		      struct mpp_task *task)
{
	struct mpp_dev *mpp = NULL;
	struct mpp_mem_region *mem_region = NULL, *n;

	mpp = session->mpp;
	/* release memory region attach to this registers table. */
	list_for_each_entry_safe(mem_region, n,
				 &task->mem_region_list,
				 reg_lnk) {
		down_read(&mpp->rw_sem);
		mpp_dma_release_fd(session->dma, (long)mem_region->hdl);
		up_read(&mpp->rw_sem);
		mutex_lock(&task->session->list_lock);
		list_del_init(&mem_region->reg_lnk);
		mutex_unlock(&task->session->list_lock);
		kfree(mem_region);
	}

	return 0;
}

/* The device will do more probing work after this */
int mpp_dev_probe(struct mpp_dev *mpp,
		  struct platform_device *pdev)
{
	int ret;
	struct resource *res = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	/* Get and attach to service */
	ret = mpp_attach_service(mpp, dev);
	if (ret) {
		dev_err(dev, "failed to attach service\n");
		return -ENODEV;
	}

	mpp->workq = create_singlethread_workqueue(np->name);
	if (!mpp->workq) {
		dev_err(dev, "failed to create workqueue\n");
		return -ENOMEM;
	}

	mpp->dev = dev;
	mpp->hw_ops = mpp->var->hw_ops;
	mpp->dev_ops = mpp->var->dev_ops;

	init_rwsem(&mpp->rw_sem);
	mutex_init(&mpp->reset_lock);
	atomic_set(&mpp->reset_request, 0);
	atomic_set(&mpp->total_running, 0);

	device_init_wakeup(dev, true);
	/* power domain autosuspend delay 2s */
	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	mpp->irq = platform_get_irq(pdev, 0);
	if (mpp->irq < 0) {
		dev_err(dev, "No interrupt resource found\n");
		ret = -ENODEV;
		goto failed;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		ret = -ENODEV;
		goto failed;
	}
	mpp->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(mpp->reg_base)) {
		ret = PTR_ERR(mpp->reg_base);
		goto failed;
	}

	pm_runtime_get_sync(dev);
	/*
	 * TODO: here or at the device itself, some device does not
	 * have the iommu, maybe in the device is better.
	 */
	mpp->iommu_info = mpp_iommu_probe(dev);
	if (IS_ERR(mpp->iommu_info)) {
		dev_err(dev, "failed to attach iommu: %ld\n",
			PTR_ERR(mpp->iommu_info));
	}
	if (mpp->hw_ops->init)
		mpp->hw_ops->init(mpp);
	pm_runtime_put_sync(dev);

	return 0;

failed:
	destroy_workqueue(mpp->workq);
	device_init_wakeup(dev, false);
	pm_runtime_disable(dev);

	return ret;
}

int mpp_dev_remove(struct mpp_dev *mpp)
{
	if (mpp->hw_ops->exit)
		mpp->hw_ops->exit(mpp);

	mpp_iommu_remove(mpp->iommu_info);

	if (mpp->workq) {
		destroy_workqueue(mpp->workq);
		mpp->workq = NULL;
	}

	device_init_wakeup(mpp->dev, false);
	pm_runtime_disable(mpp->dev);

	return 0;
}

irqreturn_t mpp_dev_irq(int irq, void *param)
{
	irqreturn_t ret = IRQ_NONE;
	struct mpp_dev *mpp = param;

	if (mpp->dev_ops->irq)
		ret = mpp->dev_ops->irq(mpp);

	return ret;
}

irqreturn_t mpp_dev_isr_sched(int irq, void *param)
{
	irqreturn_t ret = IRQ_NONE;
	struct mpp_dev *mpp = param;

	if (mpp->hw_ops->reduce_freq &&
	    list_empty(&mpp->queue->pending))
		mpp->hw_ops->reduce_freq(mpp);

	if (mpp->dev_ops->isr)
		ret = mpp->dev_ops->isr(mpp);

	return ret;
}

int mpp_safe_reset(struct reset_control *rst)
{
	if (rst)
		reset_control_assert(rst);
	return 0;
}

int mpp_safe_unreset(struct reset_control *rst)
{
	if (rst)
		reset_control_deassert(rst);
	return 0;
}

int mpp_set_grf(struct mpp_grf_info *grf_info)
{
	if (grf_info->grf && grf_info->val)
		regmap_write(grf_info->grf,
			     grf_info->offset,
			     grf_info->val);

	return 0;
}

int mpp_time_record(struct mpp_task *task)
{
	if (mpp_debug_unlikely(DEBUG_TIMING) && task)
		do_gettimeofday(&task->start);

	return 0;
}

int mpp_time_diff(struct mpp_task *task)
{
	struct timeval end;
	struct mpp_dev *mpp = task->session->mpp;

	do_gettimeofday(&end);
	mpp_debug(DEBUG_TIMING, "%s: pid:%d time: %ld ms\n",
		  dev_name(mpp->dev), task->session->pid,
		  (end.tv_sec  - task->start.tv_sec)  * 1000 +
		  (end.tv_usec - task->start.tv_usec) / 1000);

	return 0;
}

int mpp_dump_reg(u32 *regs, u32 start_idx, u32 end_idx)
{
	u32 i;

	if (mpp_debug_unlikely(DEBUG_DUMP_ERR_REG)) {
		pr_info("Dumping registers: %p\n", regs);

		for (i = start_idx; i < end_idx; i++)
			pr_info("reg[%03d]: %08x\n", i, regs[i]);
	}

	return 0;
}
