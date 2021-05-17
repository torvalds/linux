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
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/proc_fs.h>
#include <linux/pm_runtime.h>
#include <linux/poll.h>
#include <linux/regmap.h>
#include <linux/rwsem.h>
#include <linux/mfd/syscon.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/nospec.h>

#include <soc/rockchip/pm_domains.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#define MPP_WORK_TIMEOUT_DELAY		(200)
#define MPP_WAIT_TIMEOUT_DELAY		(2000)

/* Use 'v' as magic number */
#define MPP_IOC_MAGIC		'v'

#define MPP_IOC_CFG_V1	_IOW(MPP_IOC_MAGIC, 1, unsigned int)
#define MPP_IOC_CFG_V2	_IOW(MPP_IOC_MAGIC, 2, unsigned int)

/* input parmater structure for version 1 */
struct mpp_msg_v1 {
	__u32 cmd;
	__u32 flags;
	__u32 size;
	__u32 offset;
	__u64 data_ptr;
};

#ifdef CONFIG_PROC_FS
const char *mpp_device_name[MPP_DEVICE_BUTT] = {
	[MPP_DEVICE_VDPU1]		= "VDPU1",
	[MPP_DEVICE_VDPU2]		= "VDPU2",
	[MPP_DEVICE_VDPU1_PP]		= "VDPU1_PP",
	[MPP_DEVICE_VDPU2_PP]		= "VDPU2_PP",
	[MPP_DEVICE_HEVC_DEC]		= "HEVC_DEC",
	[MPP_DEVICE_RKVDEC]		= "RKVDEC",
	[MPP_DEVICE_AVSPLUS_DEC]	= "AVSPLUS_DEC",
	[MPP_DEVICE_RKVENC]		= "RKVENC",
	[MPP_DEVICE_VEPU1]		= "VEPU1",
	[MPP_DEVICE_VEPU2]		= "VEPU2",
	[MPP_DEVICE_VEPU22]		= "VEPU22",
	[MPP_DEVICE_IEP2]		= "IEP2",
};

const char *enc_info_item_name[ENC_INFO_BUTT] = {
	[ENC_INFO_BASE]		= "null",
	[ENC_INFO_WIDTH]	= "width",
	[ENC_INFO_HEIGHT]	= "height",
	[ENC_INFO_FORMAT]	= "format",
	[ENC_INFO_FPS_IN]	= "fps_in",
	[ENC_INFO_FPS_OUT]	= "fps_out",
	[ENC_INFO_RC_MODE]	= "rc_mode",
	[ENC_INFO_BITRATE]	= "bitrate",
	[ENC_INFO_GOP_SIZE]	= "gop_size",
	[ENC_INFO_FPS_CALC]	= "fps_calc",
	[ENC_INFO_PROFILE]	= "profile",
};

#endif

static void mpp_free_task(struct kref *ref);
static void mpp_attach_workqueue(struct mpp_dev *mpp,
				 struct mpp_taskqueue *queue);

/* task queue schedule */
static int
mpp_taskqueue_push_pending(struct mpp_taskqueue *queue,
			   struct mpp_task *task)
{
	if (!task->session || !task->session->mpp)
		return -EINVAL;

	kref_get(&task->ref);
	mutex_lock(&queue->pending_lock);
	list_add_tail(&task->queue_link, &queue->pending_list);
	mutex_unlock(&queue->pending_lock);

	return 0;
}

static int
mpp_taskqueue_pop_pending(struct mpp_taskqueue *queue,
			  struct mpp_task *task)
{
	if (!task->session || !task->session->mpp)
		return -EINVAL;

	mutex_lock(&queue->pending_lock);
	list_del_init(&task->queue_link);
	mutex_unlock(&queue->pending_lock);
	kref_put(&task->ref, mpp_free_task);

	return 0;
}

static struct mpp_task *
mpp_taskqueue_get_pending_task(struct mpp_taskqueue *queue)
{
	struct mpp_task *task = NULL;

	mutex_lock(&queue->pending_lock);
	task = list_first_entry_or_null(&queue->pending_list,
					struct mpp_task,
					queue_link);
	mutex_unlock(&queue->pending_lock);

	return task;
}

static bool
mpp_taskqueue_is_running(struct mpp_taskqueue *queue)
{
	bool flag;

	mutex_lock(&queue->running_lock);
	flag = !list_empty(&queue->running_list);
	mutex_unlock(&queue->running_lock);

	return flag;
}

static int
mpp_taskqueue_pending_to_run(struct mpp_taskqueue *queue,
			     struct mpp_task *task)
{
	mutex_lock(&queue->pending_lock);
	mutex_lock(&queue->running_lock);
	list_move_tail(&task->queue_link, &queue->running_list);
	mutex_unlock(&queue->running_lock);
	mutex_unlock(&queue->pending_lock);

	return 0;
}

static struct mpp_task *
mpp_taskqueue_get_running_task(struct mpp_taskqueue *queue)
{
	struct mpp_task *task = NULL;

	mutex_lock(&queue->running_lock);
	task = list_first_entry_or_null(&queue->running_list,
					struct mpp_task,
					queue_link);
	mutex_unlock(&queue->running_lock);

	return task;
}

static int
mpp_taskqueue_pop_running(struct mpp_taskqueue *queue,
			  struct mpp_task *task)
{
	if (!task->session || !task->session->mpp)
		return -EINVAL;

	mutex_lock(&queue->running_lock);
	list_del_init(&task->queue_link);
	mutex_unlock(&queue->running_lock);
	kref_put(&task->ref, mpp_free_task);

	return 0;
}

static void
mpp_taskqueue_trigger_work(struct mpp_dev *mpp)
{
	queue_work(mpp->workq, &mpp->work);
}

int mpp_power_on(struct mpp_dev *mpp)
{
	pm_runtime_get_sync(mpp->dev);
	pm_stay_awake(mpp->dev);

	if (mpp->hw_ops->clk_on)
		mpp->hw_ops->clk_on(mpp);

	return 0;
}

int mpp_power_off(struct mpp_dev *mpp)
{
	if (mpp->hw_ops->clk_off)
		mpp->hw_ops->clk_off(mpp);

	pm_relax(mpp->dev);
	if (mpp_taskqueue_get_pending_task(mpp->queue) ||
	    mpp_taskqueue_get_running_task(mpp->queue)) {
		pm_runtime_mark_last_busy(mpp->dev);
		pm_runtime_put_autosuspend(mpp->dev);
	} else {
		pm_runtime_put_sync_suspend(mpp->dev);
	}

	return 0;
}

static int
mpp_session_push_pending(struct mpp_session *session,
			 struct mpp_task *task)
{
	kref_get(&task->ref);
	mutex_lock(&session->pending_lock);
	list_add_tail(&task->pending_link, &session->pending_list);
	mutex_unlock(&session->pending_lock);

	return 0;
}

static int
mpp_session_pop_pending(struct mpp_session *session,
			struct mpp_task *task)
{
	mutex_lock(&session->pending_lock);
	list_del_init(&task->pending_link);
	mutex_unlock(&session->pending_lock);
	kref_put(&task->ref, mpp_free_task);

	return 0;
}

static struct mpp_task *
mpp_session_get_pending_task(struct mpp_session *session)
{
	struct mpp_task *task = NULL;

	mutex_lock(&session->pending_lock);
	task = list_first_entry_or_null(&session->pending_list,
					struct mpp_task,
					pending_link);
	mutex_unlock(&session->pending_lock);

	return task;
}

static int mpp_session_push_done(struct mpp_session *session,
				 struct mpp_task *task)
{
	kref_get(&task->ref);
	mutex_lock(&session->done_lock);
	list_add_tail(&task->done_link, &session->done_list);
	mutex_unlock(&session->done_lock);

	return 0;
}

static int mpp_session_pop_done(struct mpp_session *session,
				struct mpp_task *task)
{
	mutex_lock(&session->done_lock);
	list_del_init(&task->done_link);
	mutex_unlock(&session->done_lock);
	set_bit(TASK_STATE_DONE, &task->state);
	kref_put(&task->ref, mpp_free_task);

	return 0;
}

static void mpp_free_task(struct kref *ref)
{
	struct mpp_dev *mpp;
	struct mpp_session *session;
	struct mpp_task *task = container_of(ref, struct mpp_task, ref);

	if (!task->session) {
		mpp_err("task %p, task->session is null.\n", task);
		return;
	}
	session = task->session;

	mpp_debug_func(DEBUG_TASK_INFO,
		       "session=%p, task=%p, state=0x%lx, abort_request=%d\n",
		       session, task, task->state,
		       atomic_read(&task->abort_request));
	if (!session->mpp) {
		mpp_err("session %p, session->mpp is null.\n", session);
		return;
	}
	mpp = session->mpp;

	if (mpp->dev_ops->free_task)
		mpp->dev_ops->free_task(session, task);
	/* Decrease reference count */
	atomic_dec(&session->task_count);
	atomic_dec(&mpp->task_count);
}

static void mpp_task_timeout_work(struct work_struct *work_s)
{
	struct mpp_dev *mpp;
	struct mpp_session *session;
	struct mpp_task *task = container_of(to_delayed_work(work_s),
					     struct mpp_task,
					     timeout_work);

	if (test_and_set_bit(TASK_STATE_HANDLE, &task->state)) {
		mpp_err("task has been handled\n");
		return;
	}

	mpp_err("task %p processing time out!\n", task);
	if (!task->session) {
		mpp_err("task %p, task->session is null.\n", task);
		return;
	}
	session = task->session;

	if (!session->mpp) {
		mpp_err("session %p, session->mpp is null.\n", session);
		return;
	}
	mpp = session->mpp;

	/* hardware maybe dead, reset it */
	mpp_reset_up_read(mpp->reset_group);
	mpp_dev_reset(mpp);
	mpp_power_off(mpp);

	mpp_session_push_done(session, task);
	/* Wake up the GET thread */
	wake_up(&session->wait);

	/* remove task from taskqueue running list */
	set_bit(TASK_STATE_TIMEOUT, &task->state);
	mpp_taskqueue_pop_running(mpp->queue, task);
}

static int mpp_process_task(struct mpp_session *session,
			    struct mpp_task_msgs *msgs)
{
	struct mpp_task *task = NULL;
	struct mpp_dev *mpp = session->mpp;

	if (!mpp) {
		mpp_err("pid %d not find clinet %d\n",
			session->pid, session->device_type);
		return -EINVAL;
	}

	if (mpp->dev_ops->alloc_task)
		task = mpp->dev_ops->alloc_task(session, msgs);
	if (!task) {
		mpp_err("alloc_task failed.\n");
		return -ENOMEM;
	}
	kref_init(&task->ref);
	atomic_set(&task->abort_request, 0);
	task->task_index = atomic_fetch_inc(&mpp->task_index);
	INIT_DELAYED_WORK(&task->timeout_work, mpp_task_timeout_work);

	if (mpp->auto_freq_en && mpp->hw_ops->get_freq)
		mpp->hw_ops->get_freq(mpp, task);

	/*
	 * Push task to session should be in front of push task to queue.
	 * Otherwise, when mpp_task_finish finish and worker_thread call
	 * mpp_task_try_run, it may be get a task who has push in queue but
	 * not in session, cause some errors.
	 */
	atomic_inc(&session->task_count);
	mpp_session_push_pending(session, task);
	/* push current task to queue */
	atomic_inc(&mpp->task_count);
	mpp_taskqueue_push_pending(mpp->queue, task);
	set_bit(TASK_STATE_PENDING, &task->state);
	/* trigger current queue to run task */
	mpp_taskqueue_trigger_work(mpp);
	kref_put(&task->ref, mpp_free_task);

	return 0;
}

struct reset_control *
mpp_reset_control_get(struct mpp_dev *mpp, enum MPP_RESET_TYPE type, const char *name)
{
	int index;
	struct reset_control *rst = NULL;
	char shared_name[32] = "shared_";
	struct mpp_reset_group *group;

	/* check reset whether belone to device alone */
	index = of_property_match_string(mpp->dev->of_node, "reset-names", name);
	if (index >= 0) {
		rst = devm_reset_control_get(mpp->dev, name);
		mpp_safe_unreset(rst);

		return rst;
	}

	/* check reset whether is shared */
	strncat(shared_name, name,
		sizeof(shared_name) - strlen(shared_name) - 1);
	index = of_property_match_string(mpp->dev->of_node,
					 "reset-names", shared_name);
	if (index < 0) {
		dev_err(mpp->dev, "%s is not found!\n", shared_name);
		return NULL;
	}

	if (!mpp->reset_group) {
		dev_err(mpp->dev, "reset group is empty!\n");
		return NULL;
	}
	group = mpp->reset_group;

	down_write(&group->rw_sem);
	rst = group->resets[type];
	if (!rst) {
		rst = devm_reset_control_get(mpp->dev, shared_name);
		mpp_safe_unreset(rst);
		group->resets[type] = rst;
		group->queue = mpp->queue;
	}
	/* if reset not in the same queue, it means different device
	 * may reset in the same time, then rw_sem_on should set true.
	 */
	group->rw_sem_on |= (group->queue != mpp->queue) ? true : false;
	dev_info(mpp->dev, "reset_group->rw_sem_on=%d\n", group->rw_sem_on);
	up_write(&group->rw_sem);

	return rst;
}

int mpp_dev_reset(struct mpp_dev *mpp)
{
	dev_info(mpp->dev, "resetting...\n");

	/*
	 * before running, we have to switch grf ctrl bit to ensure
	 * working in current hardware
	 */
	if (mpp->hw_ops->set_grf)
		mpp->hw_ops->set_grf(mpp);
	else
		mpp_set_grf(mpp->grf_info);

	if (mpp->auto_freq_en && mpp->hw_ops->reduce_freq)
		mpp->hw_ops->reduce_freq(mpp);
	/* FIXME lock resource lock of the other devices in combo */
	mpp_iommu_down_write(mpp->iommu_info);
	mpp_reset_down_write(mpp->reset_group);
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
	mpp_iommu_refresh(mpp->iommu_info, mpp->dev);

	mpp_iommu_attach(mpp->iommu_info);
	mpp_reset_up_write(mpp->reset_group);
	mpp_iommu_up_write(mpp->iommu_info);

	dev_info(mpp->dev, "reset done\n");

	return 0;
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
	if (mpp->hw_ops->set_grf) {
		ret = mpp->hw_ops->set_grf(mpp);
		if (ret) {
			dev_err(mpp->dev, "set grf failed\n");
			return ret;
		}
	} else {
		mpp_set_grf(mpp->grf_info);
	}
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

	if (mpp->auto_freq_en && mpp->hw_ops->set_freq)
		mpp->hw_ops->set_freq(mpp, task);
	/*
	 * TODO: Lock the reader locker of the device resource lock here,
	 * release at the finish operation
	 */
	mpp_reset_down_read(mpp->reset_group);

	set_bit(TASK_STATE_START, &task->state);
	schedule_delayed_work(&task->timeout_work,
			      msecs_to_jiffies(MPP_WORK_TIMEOUT_DELAY));
	if (mpp->dev_ops->run)
		mpp->dev_ops->run(mpp, task);

	mpp_debug_leave();

	return 0;
}

static void mpp_task_try_run(struct work_struct *work_s)
{
	struct mpp_task *task;
	struct mpp_dev *mpp = container_of(work_s, struct mpp_dev, work);
	struct mpp_taskqueue *queue = mpp->queue;

	mpp_debug_enter();

	task = mpp_taskqueue_get_pending_task(queue);
	if (!task)
		goto done;

	/* if task timeout and aborted, remove it */
	if (atomic_read(&task->abort_request) > 0) {
		mpp_taskqueue_pop_pending(queue, task);
		goto done;
	}

	/* get device for current task */
	mpp = task->session->mpp;

	/*
	 * In the link table mode, the prepare function of the device
	 * will check whether I can insert a new task into device.
	 * If the device supports the task status query(like the HEVC
	 * encoder), it can report whether the device is busy.
	 * If the device does not support multiple task or task status
	 * query, leave this job to mpp service.
	 */
	if (mpp->dev_ops->prepare)
		task = mpp->dev_ops->prepare(mpp, task);
	else if (mpp_taskqueue_is_running(queue))
		task = NULL;

	/*
	 * FIXME if the hardware supports task query, but we still need to lock
	 * the running list and lock the mpp service in the current state.
	 */
	/* Push a pending task to running queue */
	if (task) {
		mpp_taskqueue_pending_to_run(queue, task);
		set_bit(TASK_STATE_RUNNING, &task->state);
		if (mpp_task_run(mpp, task))
			mpp_taskqueue_pop_running(mpp->queue, task);
	}

done:
	mpp_debug_leave();
}

static int mpp_session_clear(struct mpp_dev *mpp,
			     struct mpp_session *session)
{
	struct mpp_task *task = NULL, *n;

	/* clear session done list */
	mutex_lock(&session->done_lock);
	list_for_each_entry_safe(task, n,
				 &session->done_list,
				 done_link) {
		list_del_init(&task->done_link);
		kref_put(&task->ref, mpp_free_task);
	}
	mutex_unlock(&session->done_lock);

	/* clear session pending list */
	mutex_lock(&session->pending_lock);
	list_for_each_entry_safe(task, n,
				 &session->pending_list,
				 pending_link) {
		/* abort task in taskqueue */
		atomic_inc(&task->abort_request);
		list_del_init(&task->pending_link);
		kref_put(&task->ref, mpp_free_task);
	}
	mutex_unlock(&session->pending_lock);

	return 0;
}

static int mpp_wait_result(struct mpp_session *session,
			   struct mpp_task_msgs *msgs)
{
	int ret;
	struct mpp_task *task;
	struct mpp_dev *mpp = session->mpp;

	if (!mpp) {
		mpp_err("pid %d not find clinet %d\n",
			session->pid, session->device_type);
		return -EINVAL;
	}

	ret = wait_event_timeout(session->wait,
				 !list_empty(&session->done_list),
				 msecs_to_jiffies(MPP_WAIT_TIMEOUT_DELAY));

	task = mpp_session_get_pending_task(session);
	if (!task) {
		mpp_err("session %p pending list is empty!\n", session);
		return -EIO;
	}

	if (ret > 0) {
		u32 task_found = 0;
		struct mpp_task *loop = NULL, *n;

		/* find task in session done list */
		mutex_lock(&session->done_lock);
		list_for_each_entry_safe(loop, n,
					 &session->done_list,
					 done_link) {
			if (loop == task) {
				task_found = 1;
				break;
			}
		}
		mutex_unlock(&session->done_lock);
		if (task_found) {
			if (mpp->dev_ops->result)
				ret = mpp->dev_ops->result(mpp, task, msgs);
			mpp_session_pop_done(session, task);

			if (test_bit(TASK_STATE_TIMEOUT, &task->state))
				ret = -ETIMEDOUT;
		} else {
			mpp_err("session %p task %p, not found in done list!\n",
				session, task);
			ret = -EIO;
		}
	} else {
		atomic_inc(&task->abort_request);
		mpp_err("timeout, pid %d session %p:%d count %d cur_task %p index %d.\n",
			session->pid, session, session->index,
			atomic_read(&session->task_count), task,
			task->task_index);
		/* if twice and return timeout, otherwise, re-wait */
		if (atomic_read(&task->abort_request) > 1) {
			mpp_err("session %p:%d, task %p index %d abort wait twice!\n",
				session, session->index,
				task, task->task_index);
			ret = -ETIMEDOUT;
		} else {
			return mpp_wait_result(session, msgs);
		}
	}

	mpp_debug_func(DEBUG_TASK_INFO,
		       "kref_read=%d, ret=%d\n", kref_read(&task->ref), ret);
	mpp_session_pop_pending(session, task);

	return ret;
}

static int mpp_attach_service(struct mpp_dev *mpp, struct device *dev)
{
	u32 taskqueue_node = 0;
	u32 reset_group_node = 0;
	struct device_node *np = NULL;
	struct platform_device *pdev = NULL;
	struct mpp_taskqueue *queue = NULL;
	int ret = 0;

	np = of_parse_phandle(dev->of_node, "rockchip,srv", 0);
	if (!np || !of_device_is_available(np)) {
		dev_err(dev, "failed to get the mpp service node\n");
		return -ENODEV;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		dev_err(dev, "failed to get mpp service from node\n");
		ret = -ENODEV;
		goto err_put_pdev;
	}

	mpp->pdev_srv = pdev;
	mpp->srv = platform_get_drvdata(pdev);
	if (!mpp->srv) {
		dev_err(&pdev->dev, "failed attach service\n");
		ret = -EINVAL;
		goto err_put_pdev;
	}

	ret = of_property_read_u32(dev->of_node,
				   "rockchip,taskqueue-node", &taskqueue_node);
	if (taskqueue_node >= mpp->srv->taskqueue_cnt) {
		dev_err(dev, "taskqueue-node %d must less than %d\n",
			taskqueue_node, mpp->srv->taskqueue_cnt);
		ret = -ENODEV;
		goto err_put_pdev;
	}

	if (ret) {
		/* if device not set, then alloc one */
		queue = mpp_taskqueue_init(dev);
		if (!queue)
			goto err_put_pdev;
	} else {
		/* set taskqueue according dtsi */
		queue = mpp->srv->task_queues[taskqueue_node];
		if (!queue) {
			dev_err(dev, "taskqueue attach to invalid node %d\n",
				taskqueue_node);
			ret = -ENODEV;
			goto err_put_pdev;
		}
	}
	mpp_attach_workqueue(mpp, queue);

	ret = of_property_read_u32(dev->of_node,
				   "rockchip,resetgroup-node", &reset_group_node);
	if (!ret) {
		/* set resetgroup according dtsi */
		if (reset_group_node >= mpp->srv->reset_group_cnt) {
			dev_err(dev, "resetgroup-node %d must less than %d\n",
				reset_group_node, mpp->srv->reset_group_cnt);
			ret = -ENODEV;
			goto err_put_pdev;
		} else {
			mpp->reset_group = mpp->srv->reset_groups[reset_group_node];
		}
	}

	/* register current device to mpp service */
	mpp->srv->sub_devices[mpp->var->device_type] = mpp;
	set_bit(mpp->var->device_type, &mpp->srv->hw_support);

	return 0;

err_put_pdev:
	platform_device_put(pdev);

	return ret;
}

struct mpp_taskqueue *mpp_taskqueue_init(struct device *dev)
{
	struct mpp_taskqueue *queue = devm_kzalloc(dev, sizeof(*queue),
						   GFP_KERNEL);
	if (!queue)
		return NULL;

	mutex_init(&queue->pending_lock);
	mutex_init(&queue->running_lock);
	mutex_init(&queue->mmu_lock);
	mutex_init(&queue->dev_lock);
	INIT_LIST_HEAD(&queue->pending_list);
	INIT_LIST_HEAD(&queue->running_list);
	INIT_LIST_HEAD(&queue->mmu_list);
	INIT_LIST_HEAD(&queue->dev_list);
	/* default taskqueue has max 16 task capacity */
	queue->task_capacity = MPP_MAX_TASK_CAPACITY;

	return queue;
}

static void mpp_attach_workqueue(struct mpp_dev *mpp,
				 struct mpp_taskqueue *queue)
{
	mpp->queue = queue;
	INIT_LIST_HEAD(&mpp->queue_link);
	mutex_lock(&queue->dev_lock);
	list_add_tail(&mpp->queue_link, &queue->dev_list);
	if (queue->task_capacity > mpp->task_capacity)
		queue->task_capacity = mpp->task_capacity;
	mutex_unlock(&queue->dev_lock);
}

static void mpp_detach_workqueue(struct mpp_dev *mpp)
{
	struct mpp_taskqueue *queue = mpp->queue;

	if (queue) {
		mutex_lock(&queue->dev_lock);
		list_del_init(&mpp->queue_link);
		mutex_unlock(&queue->dev_lock);
	}
}

static int mpp_check_cmd_v1(__u32 cmd)
{
	bool found;

	found = (cmd < MPP_CMD_QUERY_BUTT) ? true : false;
	found = (cmd >= MPP_CMD_INIT_BASE && cmd < MPP_CMD_INIT_BUTT) ? true : found;
	found = (cmd >= MPP_CMD_SEND_BASE && cmd < MPP_CMD_SEND_BUTT) ? true : found;
	found = (cmd >= MPP_CMD_POLL_BASE && cmd < MPP_CMD_POLL_BUTT) ? true : found;
	found = (cmd >= MPP_CMD_CONTROL_BASE && cmd < MPP_CMD_CONTROL_BUTT) ? true : found;

	return found ? 0 : -EINVAL;
}

static int mpp_parse_msg_v1(struct mpp_msg_v1 *msg,
			    struct mpp_request *req)
{
	int ret = 0;

	req->cmd = msg->cmd;
	req->flags = msg->flags;
	req->size = msg->size;
	req->offset = msg->offset;
	req->data = (void __user *)(unsigned long)msg->data_ptr;

	mpp_debug(DEBUG_IOCTL, "cmd %x, flags %08x, size %d, offset %x\n",
		  req->cmd, req->flags, req->size, req->offset);

	ret = mpp_check_cmd_v1(req->cmd);
	if (ret)
		mpp_err("mpp cmd %x is not supproted.\n", req->cmd);

	return ret;
}

static inline int mpp_msg_is_last(struct mpp_request *req)
{
	int flag;

	if (req->flags & MPP_FLAGS_MULTI_MSG)
		flag = (req->flags & MPP_FLAGS_LAST_MSG) ? 1 : 0;
	else
		flag = 1;

	return flag;
}

static __u32 mpp_get_cmd_butt(__u32 cmd)
{
	__u32 mask = 0;

	switch (cmd) {
	case MPP_CMD_QUERY_BASE:
		mask = MPP_CMD_QUERY_BUTT;
		break;
	case MPP_CMD_INIT_BASE:
		mask = MPP_CMD_INIT_BUTT;
		break;

	case MPP_CMD_SEND_BASE:
		mask = MPP_CMD_SEND_BUTT;
		break;
	case MPP_CMD_POLL_BASE:
		mask = MPP_CMD_POLL_BUTT;
		break;
	case MPP_CMD_CONTROL_BASE:
		mask = MPP_CMD_CONTROL_BUTT;
		break;
	default:
		mpp_err("unknown dev cmd 0x%x\n", cmd);
		break;
	}

	return mask;
}

static int mpp_process_request(struct mpp_session *session,
			       struct mpp_service *srv,
			       struct mpp_request *req,
			       struct mpp_task_msgs *msgs)
{
	int ret;
	struct mpp_dev *mpp;

	mpp_debug(DEBUG_IOCTL, "req->cmd %x\n", req->cmd);
	switch (req->cmd) {
	case MPP_CMD_QUERY_HW_SUPPORT: {
		u32 hw_support = srv->hw_support;

		mpp_debug(DEBUG_IOCTL, "hw_support %08x\n", hw_support);
		if (put_user(hw_support, (u32 __user *)req->data))
			return -EFAULT;
	} break;
	case MPP_CMD_QUERY_HW_ID: {
		struct mpp_hw_info *hw_info;

		mpp = NULL;
		if (session && session->mpp) {
			mpp = session->mpp;
		} else {
			u32 client_type;

			if (get_user(client_type, (u32 __user *)req->data))
				return -EFAULT;

			mpp_debug(DEBUG_IOCTL, "client %d\n", client_type);
			client_type = array_index_nospec(client_type, MPP_DEVICE_BUTT);
			if (test_bit(client_type, &srv->hw_support))
				mpp = srv->sub_devices[client_type];
		}
		if (!mpp)
			return -EINVAL;
		hw_info = mpp->var->hw_info;
		mpp_debug(DEBUG_IOCTL, "hw_id %08x\n", hw_info->hw_id);
		if (put_user(hw_info->hw_id, (u32 __user *)req->data))
			return -EFAULT;
	} break;
	case MPP_CMD_QUERY_CMD_SUPPORT: {
		__u32 cmd = 0;

		if (get_user(cmd, (u32 __user *)req->data))
			return -EINVAL;

		if (put_user(mpp_get_cmd_butt(cmd), (u32 __user *)req->data))
			return -EFAULT;
	} break;
	case MPP_CMD_INIT_CLIENT_TYPE: {
		u32 client_type;

		if (get_user(client_type, (u32 __user *)req->data))
			return -EFAULT;

		mpp_debug(DEBUG_IOCTL, "client %d\n", client_type);
		if (client_type >= MPP_DEVICE_BUTT) {
			mpp_err("client_type must less than %d\n",
				MPP_DEVICE_BUTT);
			return -EINVAL;
		}
		client_type = array_index_nospec(client_type, MPP_DEVICE_BUTT);
		mpp = srv->sub_devices[client_type];
		if (!mpp)
			return -EINVAL;
		session->device_type = (enum MPP_DEVICE_TYPE)client_type;
		session->dma = mpp_dma_session_create(mpp->dev, mpp->session_max_buffers);
		session->mpp = mpp;
		session->index = atomic_fetch_inc(&mpp->session_index);
		if (mpp->dev_ops->init_session) {
			ret = mpp->dev_ops->init_session(session);
			if (ret)
				return ret;
		}
	} break;
	case MPP_CMD_INIT_DRIVER_DATA: {
		u32 val;

		mpp = session->mpp;
		if (!mpp)
			return -EINVAL;
		if (get_user(val, (u32 __user *)req->data))
			return -EFAULT;
		if (mpp->grf_info->grf)
			regmap_write(mpp->grf_info->grf, 0x5d8, val);
	} break;
	case MPP_CMD_INIT_TRANS_TABLE: {
		if (session && req->size) {
			int trans_tbl_size = sizeof(session->trans_table);

			if (req->size > trans_tbl_size) {
				mpp_err("init table size %d more than %d\n",
					req->size, trans_tbl_size);
				return -ENOMEM;
			}

			if (copy_from_user(session->trans_table,
					   req->data, req->size)) {
				mpp_err("copy_from_user failed\n");
				return -EINVAL;
			}
			session->trans_count =
				req->size / sizeof(session->trans_table[0]);
		}
	} break;
	case MPP_CMD_SET_REG_WRITE:
	case MPP_CMD_SET_REG_READ:
	case MPP_CMD_SET_REG_ADDR_OFFSET:
	case MPP_CMD_SET_RCB_INFO: {
		msgs->flags |= req->flags;
		msgs->set_cnt++;
	} break;
	case MPP_CMD_POLL_HW_FINISH: {
		msgs->flags |= req->flags;
		msgs->poll_cnt++;
	} break;
	case MPP_CMD_RESET_SESSION: {
		int ret;
		int val;

		ret = readx_poll_timeout(atomic_read,
					 &session->task_count,
					 val, val == 0, 1000, 500000);
		if (ret == -ETIMEDOUT) {
			mpp_err("wait task running time out\n");
		} else {
			mpp = session->mpp;
			if (!mpp)
				return -EINVAL;

			mpp_session_clear(mpp, session);
			mpp_iommu_down_write(mpp->iommu_info);
			ret = mpp_dma_session_destroy(session->dma);
			mpp_iommu_up_write(mpp->iommu_info);
		}
		return ret;
	} break;
	case MPP_CMD_TRANS_FD_TO_IOVA: {
		u32 i;
		u32 count;
		u32 data[MPP_MAX_REG_TRANS_NUM];

		mpp = session->mpp;
		if (!mpp)
			return -EINVAL;

		if (req->size <= 0 ||
		    req->size > sizeof(data))
			return -EINVAL;

		memset(data, 0, sizeof(data));
		if (copy_from_user(data, req->data, req->size)) {
			mpp_err("copy_from_user failed.\n");
			return -EINVAL;
		}
		count = req->size / sizeof(u32);
		for (i = 0; i < count; i++) {
			struct mpp_dma_buffer *buffer;
			int fd = data[i];

			mpp_iommu_down_read(mpp->iommu_info);
			buffer = mpp_dma_import_fd(mpp->iommu_info,
						   session->dma, fd);
			mpp_iommu_up_read(mpp->iommu_info);
			if (IS_ERR_OR_NULL(buffer)) {
				mpp_err("can not import fd %d\n", fd);
				return -EINVAL;
			}
			data[i] = (u32)buffer->iova;
			mpp_debug(DEBUG_IOMMU, "fd %d => iova %08x\n",
				  fd, data[i]);
		}
		if (copy_to_user(req->data, data, req->size)) {
			mpp_err("copy_to_user failed.\n");
			return -EINVAL;
		}
	} break;
	case MPP_CMD_RELEASE_FD: {
		u32 i;
		int ret;
		u32 count;
		u32 data[MPP_MAX_REG_TRANS_NUM];

		if (req->size <= 0 ||
		    req->size > sizeof(data))
			return -EINVAL;

		memset(data, 0, sizeof(data));
		if (copy_from_user(data, req->data, req->size)) {
			mpp_err("copy_from_user failed.\n");
			return -EINVAL;
		}
		count = req->size / sizeof(u32);
		for (i = 0; i < count; i++) {
			ret = mpp_dma_release_fd(session->dma, data[i]);
			if (ret) {
				mpp_err("release fd %d failed.\n", data[i]);
				return ret;
			}
		}
	} break;
	default: {
		mpp = session->mpp;
		if (!mpp) {
			mpp_err("pid %d not find clinet %d\n",
				session->pid, session->device_type);
			return -EINVAL;
		}
		if (mpp->dev_ops->ioctl)
			return mpp->dev_ops->ioctl(session, req);

		mpp_debug(DEBUG_IOCTL, "unknown mpp ioctl cmd %x\n", req->cmd);
	} break;
	}

	return 0;
}

static long mpp_dev_ioctl(struct file *filp,
			  unsigned int cmd,
			  unsigned long arg)
{
	int ret = 0;
	struct mpp_service *srv;
	void __user *msg;
	struct mpp_request *req;
	struct mpp_task_msgs task_msgs;
	struct mpp_session *session =
		(struct mpp_session *)filp->private_data;

	mpp_debug_enter();

	if (!session || !session->srv) {
		mpp_err("session %p\n", session);
		return -EINVAL;
	}
	srv = session->srv;
	if (atomic_read(&session->release_request) > 0) {
		mpp_debug(DEBUG_IOCTL, "release session had request\n");
		return -EBUSY;
	}
	if (atomic_read(&srv->shutdown_request) > 0) {
		mpp_debug(DEBUG_IOCTL, "shutdown had request\n");
		return -EBUSY;
	}

	msg = (void __user *)arg;
	memset(&task_msgs, 0, sizeof(task_msgs));
	do {
		req = &task_msgs.reqs[task_msgs.req_cnt];
		/* first, parse to fixed struct */
		switch (cmd) {
		case MPP_IOC_CFG_V1: {
			struct mpp_msg_v1 msg_v1;

			memset(&msg_v1, 0, sizeof(msg_v1));
			if (copy_from_user(&msg_v1, msg, sizeof(msg_v1)))
				return -EFAULT;
			ret = mpp_parse_msg_v1(&msg_v1, req);
			if (ret)
				return -EFAULT;

			msg += sizeof(msg_v1);
		} break;
		default:
			mpp_err("unknown ioctl cmd %x\n", cmd);
			return -EINVAL;
		}
		task_msgs.req_cnt++;
		/* check loop times */
		if (task_msgs.req_cnt > MPP_MAX_MSG_NUM) {
			mpp_err("fail, message count %d more than %d.\n",
				task_msgs.req_cnt, MPP_MAX_MSG_NUM);
			return -EINVAL;
		}
		/* second, process request */
		ret = mpp_process_request(session, srv, req, &task_msgs);
		if (ret)
			return -EFAULT;
		/* last, process task message */
		if (mpp_msg_is_last(req)) {
			session->msg_flags = task_msgs.flags;
			if (task_msgs.set_cnt > 0) {
				ret = mpp_process_task(session, &task_msgs);
				if (ret)
					return ret;
			}
			if (task_msgs.poll_cnt > 0) {
				ret = mpp_wait_result(session, &task_msgs);
				if (ret)
					return ret;
			}
		}
	} while (!mpp_msg_is_last(req));

	mpp_debug_leave();

	return ret;
}

static int mpp_dev_open(struct inode *inode, struct file *filp)
{
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

	mutex_init(&session->pending_lock);
	mutex_init(&session->done_lock);
	INIT_LIST_HEAD(&session->pending_list);
	INIT_LIST_HEAD(&session->done_list);
	INIT_LIST_HEAD(&session->session_link);

	init_waitqueue_head(&session->wait);
	atomic_set(&session->task_count, 0);
	atomic_set(&session->release_request, 0);

	mutex_lock(&srv->session_lock);
	list_add_tail(&session->session_link, &srv->session_list);
	mutex_unlock(&srv->session_lock);
	filp->private_data = (void *)session;

	mpp_debug_leave();

	return nonseekable_open(inode, filp);
}

static int mpp_dev_release(struct inode *inode, struct file *filp)
{
	int ret;
	int val;
	struct mpp_dev *mpp;
	struct mpp_session *session = filp->private_data;

	mpp_debug_enter();

	if (!session) {
		mpp_err("session is null\n");
		return -EINVAL;
	}

	/* wait for task all done */
	atomic_inc(&session->release_request);
	ret = readx_poll_timeout(atomic_read,
				 &session->task_count,
				 val, val == 0, 1000, 500000);
	if (ret == -ETIMEDOUT)
		mpp_err("session %p, wait task count %d time out\n",
			session, atomic_read(&session->task_count));
	wake_up(&session->wait);

	/* release device resource */
	mpp = session->mpp;
	if (mpp) {
		if (mpp->dev_ops->free_session)
			mpp->dev_ops->free_session(session);

		/* remove this filp from the asynchronusly notified filp's */
		mpp_session_clear(mpp, session);

		mpp_iommu_down_read(mpp->iommu_info);
		mpp_dma_session_destroy(session->dma);
		mpp_iommu_up_read(mpp->iommu_info);
	}
	mutex_lock(&session->srv->session_lock);
	list_del_init(&session->session_link);
	mutex_unlock(&session->srv->session_lock);

	kfree(session);
	filp->private_data = NULL;

	mpp_debug_leave();
	return 0;
}

static unsigned int
mpp_dev_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	struct mpp_session *session =
		(struct mpp_session *)filp->private_data;

	poll_wait(filp, &session->wait, wait);
	if (!list_empty(&session->done_list))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

const struct file_operations rockchip_mpp_fops = {
	.open		= mpp_dev_open,
	.release	= mpp_dev_release,
	.poll		= mpp_dev_poll,
	.unlocked_ioctl = mpp_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = mpp_dev_ioctl,
#endif
};

struct mpp_mem_region *
mpp_task_attach_fd(struct mpp_task *task, int fd)
{
	struct mpp_mem_region *mem_region = NULL, *loop = NULL, *n;
	struct mpp_dma_buffer *buffer = NULL;
	struct mpp_dev *mpp = task->session->mpp;
	struct mpp_dma_session *dma = task->session->dma;
	u32 mem_num = ARRAY_SIZE(task->mem_regions);
	bool found = false;

	if (fd <= 0 || !dma || !mpp)
		return ERR_PTR(-EINVAL);

	if (task->mem_count > mem_num) {
		mpp_err("mem_count %d must less than %d\n", task->mem_count, mem_num);
		return ERR_PTR(-ENOMEM);
	}

	/* find fd whether had import */
	list_for_each_entry_safe_reverse(loop, n, &task->mem_region_list, reg_link) {
		if (loop->fd == fd) {
			found = true;
			break;
		}
	}

	mem_region = &task->mem_regions[task->mem_count];
	if (found) {
		memcpy(mem_region, loop, sizeof(*loop));
		buffer = mem_region->hdl;
		kref_get(&buffer->ref);
	} else {
		down_read(&mpp->iommu_info->rw_sem);
		buffer = mpp_dma_import_fd(mpp->iommu_info, dma, fd);
		up_read(&mpp->iommu_info->rw_sem);
		if (IS_ERR_OR_NULL(buffer)) {
			mpp_err("can't import dma-buf %d\n", fd);
			return ERR_PTR(-ENOMEM);
		}

		mem_region->hdl = buffer;
		mem_region->iova = buffer->iova;
		mem_region->len = buffer->size;
		mem_region->fd = fd;
	}
	task->mem_count++;
	list_add_tail(&mem_region->reg_link, &task->mem_region_list);

	return mem_region;
}

int mpp_translate_reg_address(struct mpp_session *session,
			      struct mpp_task *task, int fmt,
			      u32 *reg, struct reg_offset_info *off_inf)
{
	int i;
	int cnt;
	const u16 *tbl;

	mpp_debug_enter();

	if (session->trans_count > 0) {
		cnt = session->trans_count;
		tbl = session->trans_table;
	} else {
		struct mpp_dev *mpp = session->mpp;
		struct mpp_trans_info *trans_info = mpp->var->trans_info;

		cnt = trans_info[fmt].count;
		tbl = trans_info[fmt].table;
	}

	for (i = 0; i < cnt; i++) {
		int usr_fd;
		u32 offset;
		struct mpp_mem_region *mem_region = NULL;

		if (session->msg_flags & MPP_FLAGS_REG_NO_OFFSET) {
			usr_fd = reg[tbl[i]];
			offset = 0;
		} else {
			usr_fd = reg[tbl[i]] & 0x3ff;
			offset = reg[tbl[i]] >> 10;
		}

		if (usr_fd == 0)
			continue;

		mem_region = mpp_task_attach_fd(task, usr_fd);
		if (IS_ERR(mem_region)) {
			mpp_err("reg[%3d]: 0x%08x fd %d failed\n",
				tbl[i], reg[tbl[i]], usr_fd);
			return PTR_ERR(mem_region);
		}
		mpp_debug(DEBUG_IOMMU,
			  "reg[%3d]: %d => %pad, offset %10d, size %lx\n",
			  tbl[i], usr_fd, &mem_region->iova,
			  offset, mem_region->len);
		mem_region->reg_idx = tbl[i];
		reg[tbl[i]] = mem_region->iova + offset;
	}

	mpp_debug_leave();

	return 0;
}

int mpp_check_req(struct mpp_request *req, int base,
		  int max_size, u32 off_s, u32 off_e)
{
	int req_off;

	if (req->offset < base) {
		mpp_err("error: base %x, offset %x\n",
			base, req->offset);
		return -EINVAL;
	}
	req_off = req->offset - base;
	if ((req_off + req->size) < off_s) {
		mpp_err("error: req_off %x, req_size %x, off_s %x\n",
			req_off, req->size, off_s);
		return -EINVAL;
	}
	if (max_size < off_e) {
		mpp_err("error: off_e %x, max_size %x\n",
			off_e, max_size);
		return -EINVAL;
	}
	if (req_off > max_size) {
		mpp_err("error: req_off %x, max_size %x\n",
			req_off, max_size);
		return -EINVAL;
	}
	if ((req_off + req->size) > max_size) {
		mpp_err("error: req_off %x, req_size %x, max_size %x\n",
			req_off, req->size, max_size);
		req->size = req_off + req->size - max_size;
	}

	return 0;
}

int mpp_extract_reg_offset_info(struct reg_offset_info *off_inf,
				struct mpp_request *req)
{
	int max_size = ARRAY_SIZE(off_inf->elem);
	int cnt = req->size / sizeof(off_inf->elem[0]);

	if ((cnt + off_inf->cnt) > max_size) {
		mpp_err("count %d, total %d, max_size %d\n",
			cnt, off_inf->cnt, max_size);
		return -EINVAL;
	}
	if (copy_from_user(&off_inf->elem[off_inf->cnt],
			   req->data, req->size)) {
		mpp_err("copy_from_user failed\n");
		return -EINVAL;
	}
	off_inf->cnt += cnt;

	return 0;
}

int mpp_query_reg_offset_info(struct reg_offset_info *off_inf,
			      u32 index)
{
	mpp_debug_enter();
	if (off_inf) {
		int i;

		for (i = 0; i < off_inf->cnt; i++) {
			if (off_inf->elem[i].index == index)
				return off_inf->elem[i].offset;
		}
	}
	mpp_debug_leave();

	return 0;
}

int mpp_translate_reg_offset_info(struct mpp_task *task,
				  struct reg_offset_info *off_inf,
				  u32 *reg)
{
	mpp_debug_enter();

	if (off_inf) {
		int i;

		for (i = 0; i < off_inf->cnt; i++) {
			mpp_debug(DEBUG_IOMMU, "reg[%d] + offset %d\n",
				  off_inf->elem[i].index,
				  off_inf->elem[i].offset);
			reg[off_inf->elem[i].index] += off_inf->elem[i].offset;
		}
	}
	mpp_debug_leave();

	return 0;
}

int mpp_task_init(struct mpp_session *session,
		  struct mpp_task *task)
{
	INIT_LIST_HEAD(&task->pending_link);
	INIT_LIST_HEAD(&task->queue_link);
	INIT_LIST_HEAD(&task->mem_region_list);
	task->mem_count = 0;
	task->session = session;

	return 0;
}

int mpp_task_finish(struct mpp_session *session,
		    struct mpp_task *task)
{
	struct mpp_dev *mpp = session->mpp;

	if (mpp->dev_ops->finish)
		mpp->dev_ops->finish(mpp, task);

	mpp_reset_up_read(mpp->reset_group);
	if (atomic_read(&mpp->reset_request) > 0)
		mpp_dev_reset(mpp);
	mpp_power_off(mpp);

	if (!atomic_read(&task->abort_request)) {
		mpp_session_push_done(session, task);
		/* Wake up the GET thread */
		wake_up(&session->wait);
	}
	set_bit(TASK_STATE_FINISH, &task->state);
	mpp_taskqueue_pop_running(mpp->queue, task);

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
				 reg_link) {
		mpp_iommu_down_read(mpp->iommu_info);
		mpp_dma_release(session->dma, mem_region->hdl);
		mpp_iommu_up_read(mpp->iommu_info);
		list_del_init(&mem_region->reg_link);
	}

	return 0;
}

int mpp_task_dump_mem_region(struct mpp_dev *mpp,
			     struct mpp_task *task)
{
	struct mpp_mem_region *mem = NULL, *n;

	if (!task)
		return -EIO;

	mpp_err("--- dump mem region ---\n");
	if (!list_empty(&task->mem_region_list)) {
		list_for_each_entry_safe(mem, n,
					 &task->mem_region_list,
					 reg_link) {
			mpp_err("reg[%3d]: %pad, size %lx\n",
				mem->reg_idx, &mem->iova, mem->len);
		}
	} else {
		dev_err(mpp->dev, "no memory region mapped\n");
	}

	return 0;
}

int mpp_task_dump_reg(struct mpp_dev *mpp,
		      struct mpp_task *task)
{
	if (!task)
		return -EIO;

	if (mpp_debug_unlikely(DEBUG_DUMP_ERR_REG)) {
		mpp_err("--- dump task register ---\n");
		if (task->reg) {
			u32 i;
			u32 s = task->hw_info->reg_start;
			u32 e = task->hw_info->reg_end;

			for (i = s; i <= e; i++) {
				u32 reg = i * sizeof(u32);

				mpp_err("reg[%03d]: %04x: 0x%08x\n",
					i, reg, task->reg[i]);
			}
		}
	}

	return 0;
}

int mpp_task_dump_hw_reg(struct mpp_dev *mpp, struct mpp_task *task)
{
	if (!task)
		return -EIO;

	if (mpp_debug_unlikely(DEBUG_DUMP_ERR_REG)) {
		u32 i;
		u32 s = task->hw_info->reg_start;
		u32 e = task->hw_info->reg_end;

		mpp_err("--- dump hardware register ---\n");
		for (i = s; i <= e; i++) {
			u32 reg = i * sizeof(u32);

			mpp_err("reg[%03d]: %04x: 0x%08x\n",
				i, reg, readl_relaxed(mpp->reg_base + reg));
		}
	}

	return 0;
}

static int mpp_iommu_handle(struct iommu_domain *iommu,
			    struct device *iommu_dev,
			    unsigned long iova,
			    int status, void *arg)
{
	struct mpp_dev *mpp = (struct mpp_dev *)arg;
	struct mpp_task *task = mpp_taskqueue_get_running_task(mpp->queue);

	dev_err(mpp->dev, "fault addr 0x%08lx status %x\n", iova, status);

	if (!task)
		return -EIO;

	mpp_task_dump_mem_region(mpp, task);
	mpp_task_dump_hw_reg(mpp, task);

	if (mpp->iommu_info->hdl)
		mpp->iommu_info->hdl(iommu, iommu_dev, iova, status, arg);

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
	struct mpp_hw_info *hw_info = mpp->var->hw_info;

	/* Get disable auto frequent flag from dtsi */
	mpp->auto_freq_en = !device_property_read_bool(dev, "rockchip,disable-auto-freq");
	/* read link table capacity */
	ret = of_property_read_u32(np, "rockchip,task-capacity",
				   &mpp->task_capacity);
	if (ret) {
		mpp->task_capacity = 1;
		INIT_WORK(&mpp->work, mpp_task_try_run);
	} else {
		INIT_WORK(&mpp->work, NULL);
	}

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

	atomic_set(&mpp->reset_request, 0);
	atomic_set(&mpp->session_index, 0);
	atomic_set(&mpp->task_count, 0);
	atomic_set(&mpp->task_index, 0);

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
	/*
	 * Tips: here can not use function devm_ioremap_resource. The resion is
	 * that hevc and vdpu map the same register address region in rk3368.
	 * However, devm_ioremap_resource will call function
	 * devm_request_mem_region to check region. Thus, use function
	 * devm_ioremap can avoid it.
	 */
	mpp->reg_base = devm_ioremap(dev, res->start, resource_size(res));
	if (!mpp->reg_base) {
		dev_err(dev, "ioremap failed for resource %pR\n", res);
		ret = -ENOMEM;
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
	if (mpp->hw_ops->init) {
		ret = mpp->hw_ops->init(mpp);
		if (ret)
			goto failed_init;
	}
	/* set iommu fault handler */
	if (!IS_ERR(mpp->iommu_info))
		iommu_set_fault_handler(mpp->iommu_info->domain,
					mpp_iommu_handle, mpp);

	/* read hardware id */
	if (hw_info->reg_id >= 0) {
		if (mpp->hw_ops->clk_on)
			mpp->hw_ops->clk_on(mpp);

		hw_info->hw_id = mpp_read(mpp, hw_info->reg_id);
		if (mpp->hw_ops->clk_off)
			mpp->hw_ops->clk_off(mpp);
	}

	pm_runtime_put_sync(dev);

	return ret;
failed_init:
	pm_runtime_put_sync(dev);
failed:
	destroy_workqueue(mpp->workq);
	mpp_detach_workqueue(mpp);
	device_init_wakeup(dev, false);
	pm_runtime_disable(dev);

	return ret;
}

int mpp_dev_remove(struct mpp_dev *mpp)
{
	if (mpp->hw_ops->exit)
		mpp->hw_ops->exit(mpp);

	mpp_iommu_remove(mpp->iommu_info);
	platform_device_put(mpp->pdev_srv);

	if (mpp->workq) {
		destroy_workqueue(mpp->workq);
		mpp->workq = NULL;
	}

	mpp_detach_workqueue(mpp);
	device_init_wakeup(mpp->dev, false);
	pm_runtime_disable(mpp->dev);

	return 0;
}

irqreturn_t mpp_dev_irq(int irq, void *param)
{
	struct mpp_dev *mpp = param;
	struct mpp_task *task = mpp->cur_task;
	irqreturn_t irq_ret = IRQ_NONE;

	if (mpp->dev_ops->irq)
		irq_ret = mpp->dev_ops->irq(mpp);

	if (task) {
		if (irq_ret != IRQ_NONE) {
			/* if wait or delayed work timeout, abort request will turn on,
			 * isr should not to response, and handle it in delayed work
			 */
			if (test_and_set_bit(TASK_STATE_HANDLE, &task->state)) {
				mpp_err("error, task has been handled, irq_status %08x\n",
					mpp->irq_status);
				irq_ret = IRQ_HANDLED;
				goto done;
			}
			cancel_delayed_work(&task->timeout_work);
			/* normal condition, set state and wake up isr thread */
			set_bit(TASK_STATE_IRQ, &task->state);
		}
	} else {
		mpp_debug(DEBUG_IRQ_CHECK, "error, task is null\n");
	}
done:
	return irq_ret;
}

irqreturn_t mpp_dev_isr_sched(int irq, void *param)
{
	irqreturn_t ret = IRQ_NONE;
	struct mpp_dev *mpp = param;

	if (mpp->auto_freq_en &&
	    mpp->hw_ops->reduce_freq &&
	    list_empty(&mpp->queue->pending_list))
		mpp->hw_ops->reduce_freq(mpp);

	if (mpp->dev_ops->isr)
		ret = mpp->dev_ops->isr(mpp);

	/* trigger current queue to run next task */
	mpp_taskqueue_trigger_work(mpp);

	return ret;
}

#define MPP_GRF_VAL_MASK	0xFFFF

u32 mpp_get_grf(struct mpp_grf_info *grf_info)
{
	u32 val = 0;

	if (grf_info && grf_info->grf && grf_info->val)
		regmap_read(grf_info->grf, grf_info->offset, &val);

	return (val & MPP_GRF_VAL_MASK);
}

bool mpp_grf_is_changed(struct mpp_grf_info *grf_info)
{
	bool changed = false;

	if (grf_info && grf_info->grf && grf_info->val) {
		u32 grf_status = mpp_get_grf(grf_info);
		u32 grf_val = grf_info->val & MPP_GRF_VAL_MASK;

		changed = (grf_status == grf_val) ? false : true;
	}

	return changed;
}

int mpp_set_grf(struct mpp_grf_info *grf_info)
{
	if (grf_info && grf_info->grf && grf_info->val)
		regmap_write(grf_info->grf, grf_info->offset, grf_info->val);

	return 0;
}

int mpp_time_record(struct mpp_task *task)
{
	if (mpp_debug_unlikely(DEBUG_TIMING) && task)
		ktime_get_real_ts64(&task->start);

	return 0;
}

int mpp_time_diff(struct mpp_task *task)
{
	struct timespec64 end;
	struct mpp_dev *mpp = task->session->mpp;

	ktime_get_real_ts64(&end);
	mpp_debug(DEBUG_TIMING, "%s: pid: %d, session: %p, time: %lld us\n",
		  dev_name(mpp->dev), task->session->pid, task->session,
		  (end.tv_sec  - task->start.tv_sec)  * 1000000 +
		  (end.tv_nsec - task->start.tv_nsec)/1000);

	return 0;
}

int mpp_write_req(struct mpp_dev *mpp, u32 *regs,
		  u32 start_idx, u32 end_idx, u32 en_idx)
{
	int i;

	for (i = start_idx; i < end_idx; i++) {
		if (i == en_idx)
			continue;
		mpp_write_relaxed(mpp, i * sizeof(u32), regs[i]);
	}

	return 0;
}

int mpp_read_req(struct mpp_dev *mpp, u32 *regs,
		 u32 start_idx, u32 end_idx)
{
	int i;

	for (i = start_idx; i < end_idx; i++)
		regs[i] = mpp_read_relaxed(mpp, i * sizeof(u32));

	return 0;
}

int mpp_get_clk_info(struct mpp_dev *mpp,
		     struct mpp_clk_info *clk_info,
		     const char *name)
{
	int index = of_property_match_string(mpp->dev->of_node,
					     "clock-names", name);

	if (index < 0)
		return -EINVAL;

	clk_info->clk = devm_clk_get(mpp->dev, name);
	of_property_read_u32_index(mpp->dev->of_node,
				   "rockchip,normal-rates",
				   index,
				   &clk_info->normal_rate_hz);
	of_property_read_u32_index(mpp->dev->of_node,
				   "rockchip,advanced-rates",
				   index,
				   &clk_info->advanced_rate_hz);

	return 0;
}

int mpp_set_clk_info_rate_hz(struct mpp_clk_info *clk_info,
			     enum MPP_CLOCK_MODE mode,
			     unsigned long val)
{
	if (!clk_info->clk || !val)
		return 0;

	switch (mode) {
	case CLK_MODE_DEBUG:
		clk_info->debug_rate_hz = val;
	break;
	case CLK_MODE_REDUCE:
		clk_info->reduce_rate_hz = val;
	break;
	case CLK_MODE_NORMAL:
		clk_info->normal_rate_hz = val;
	break;
	case CLK_MODE_ADVANCED:
		clk_info->advanced_rate_hz = val;
	break;
	case CLK_MODE_DEFAULT:
		clk_info->default_rate_hz = val;
	break;
	default:
		mpp_err("error mode %d\n", mode);
	break;
	}

	return 0;
}

#define MPP_REDUCE_RATE_HZ (50 * MHZ)

unsigned long mpp_get_clk_info_rate_hz(struct mpp_clk_info *clk_info,
				       enum MPP_CLOCK_MODE mode)
{
	unsigned long clk_rate_hz = 0;

	if (!clk_info->clk)
		return 0;

	if (clk_info->debug_rate_hz)
		return clk_info->debug_rate_hz;

	switch (mode) {
	case CLK_MODE_REDUCE: {
		if (clk_info->reduce_rate_hz)
			clk_rate_hz = clk_info->reduce_rate_hz;
		else
			clk_rate_hz = MPP_REDUCE_RATE_HZ;
	} break;
	case CLK_MODE_NORMAL: {
		if (clk_info->normal_rate_hz)
			clk_rate_hz = clk_info->normal_rate_hz;
		else
			clk_rate_hz = clk_info->default_rate_hz;
	} break;
	case CLK_MODE_ADVANCED: {
		if (clk_info->advanced_rate_hz)
			clk_rate_hz = clk_info->advanced_rate_hz;
		else if (clk_info->normal_rate_hz)
			clk_rate_hz = clk_info->normal_rate_hz;
		else
			clk_rate_hz = clk_info->default_rate_hz;
	} break;
	case CLK_MODE_DEFAULT:
	default: {
		clk_rate_hz = clk_info->default_rate_hz;
	} break;
	}

	return clk_rate_hz;
}

int mpp_clk_set_rate(struct mpp_clk_info *clk_info,
		     enum MPP_CLOCK_MODE mode)
{
	unsigned long clk_rate_hz;

	if (!clk_info->clk)
		return -EINVAL;

	clk_rate_hz = mpp_get_clk_info_rate_hz(clk_info, mode);
	if (clk_rate_hz) {
		clk_info->used_rate_hz = clk_rate_hz;
		clk_set_rate(clk_info->clk, clk_rate_hz);
	}

	return 0;
}

#ifdef CONFIG_PROC_FS
static int fops_show_u32(struct seq_file *file, void *v)
{
	u32 *val = file->private;

	seq_printf(file, "%d\n", *val);

	return 0;
}

static int fops_open_u32(struct inode *inode, struct file *file)
{
	return single_open(file, fops_show_u32, PDE_DATA(inode));
}

static ssize_t fops_write_u32(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	int rc;
	struct seq_file *priv = file->private_data;

	rc = kstrtou32_from_user(buf, count, 0, priv->private);
	if (rc)
		return rc;

	return count;
}

static const struct proc_ops procfs_fops_u32 = {
	.proc_open = fops_open_u32,
	.proc_read = seq_read,
	.proc_release = single_release,
	.proc_write = fops_write_u32,
};

struct proc_dir_entry *
mpp_procfs_create_u32(const char *name, umode_t mode,
		      struct proc_dir_entry *parent, void *data)
{
	return proc_create_data(name, mode, parent, &procfs_fops_u32, data);
}
#endif

int px30_workaround_combo_init(struct mpp_dev *mpp)
{
	struct mpp_rk_iommu *iommu = NULL, *loop = NULL, *n;
	struct platform_device *pdev = mpp->iommu_info->pdev;

	/* find whether exist in iommu link */
	list_for_each_entry_safe(loop, n, &mpp->queue->mmu_list, link) {
		if (loop->base_addr[0] == pdev->resource[0].start) {
			iommu = loop;
			break;
		}
	}
	/* if not exist, add it */
	if (!iommu) {
		int i;
		struct resource *res;
		void __iomem *base;

		iommu = devm_kzalloc(mpp->srv->dev, sizeof(*iommu), GFP_KERNEL);
		for (i = 0; i < pdev->num_resources; i++) {
			res = platform_get_resource(pdev, IORESOURCE_MEM, i);
			if (!res)
				continue;
			base = devm_ioremap(&pdev->dev,
					    res->start, resource_size(res));
			if (IS_ERR(base))
				continue;
			iommu->base_addr[i] = res->start;
			iommu->bases[i] = base;
			iommu->mmu_num++;
		}
		iommu->grf_val = mpp->grf_info->val & MPP_GRF_VAL_MASK;
		if (mpp->hw_ops->clk_on)
			mpp->hw_ops->clk_on(mpp);
		iommu->dte_addr =  mpp_iommu_get_dte_addr(iommu);
		if (mpp->hw_ops->clk_off)
			mpp->hw_ops->clk_off(mpp);
		INIT_LIST_HEAD(&iommu->link);
		mutex_lock(&mpp->queue->mmu_lock);
		list_add_tail(&iommu->link, &mpp->queue->mmu_list);
		mutex_unlock(&mpp->queue->mmu_lock);
	}
	mpp->iommu_info->iommu = iommu;

	return 0;
}

int px30_workaround_combo_switch_grf(struct mpp_dev *mpp)
{
	int ret = 0;
	u32 curr_val;
	u32 next_val;
	bool pd_is_on;
	struct mpp_rk_iommu *loop = NULL, *n;

	if (!mpp->grf_info->grf || !mpp->grf_info->val)
		return 0;

	curr_val = mpp_get_grf(mpp->grf_info);
	next_val = mpp->grf_info->val & MPP_GRF_VAL_MASK;
	if (curr_val == next_val)
		return 0;

	pd_is_on = rockchip_pmu_pd_is_on(mpp->dev);
	if (!pd_is_on)
		rockchip_pmu_pd_on(mpp->dev);
	mpp->hw_ops->clk_on(mpp);

	list_for_each_entry_safe(loop, n, &mpp->queue->mmu_list, link) {
		/* update iommu parameters */
		if (loop->grf_val == curr_val)
			loop->is_paged = mpp_iommu_is_paged(loop);
		/* disable all iommu */
		mpp_iommu_disable(loop);
	}
	mpp_set_grf(mpp->grf_info);
	/* enable current iommu */
	ret = mpp_iommu_enable(mpp->iommu_info->iommu);

	mpp->hw_ops->clk_off(mpp);
	if (!pd_is_on)
		rockchip_pmu_pd_off(mpp->dev);

	return ret;
}
