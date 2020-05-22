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
#include <linux/pm_runtime.h>
#include <linux/poll.h>
#include <linux/regmap.h>
#include <linux/rwsem.h>
#include <linux/mfd/syscon.h>
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

/* cmd support for version 1 */
#define MPP_CMD_QUERY_SUPPORT_MASK_V1		(0x00000003)
#define MPP_CMD_INIT_SUPPORT_MASK_V1		(0x00000007)
#define MPP_CMD_SEND_SUPPORT_MASK_V1		(0x0000001F)
#define MPP_CMD_POLL_SUPPORT_MASK_V1		(0x00000001)
#define MPP_CMD_CONTROL_SUPPORT_MASK_V1		(0x00000007)

/* input parmater structure for version 1 */
struct mpp_msg_v1 {
	__u32 cmd;
	__u32 flags;
	__u32 size;
	__u32 offset;
	__u64 data_ptr;
};

static void mpp_free_task(struct kref *ref);
static int mpp_dev_reset(struct mpp_dev *mpp);

/* task queue schedule */
static int
mpp_taskqueue_push_pending(struct mpp_taskqueue *queue,
			   struct mpp_task *task)
{
	struct mpp_dev *mpp;

	if (!task->session || !task->session->mpp)
		return -EINVAL;
	mpp = task->session->mpp;

	kref_get(&task->ref);
	atomic_inc(&mpp->task_count);
	mutex_lock(&queue->pending_lock);
	list_add_tail(&task->queue_link, &queue->pending_list);
	mutex_unlock(&queue->pending_lock);

	return 0;
}

static int
mpp_taskqueue_pop_pending(struct mpp_taskqueue *queue,
			  struct mpp_task *task)
{
	struct mpp_dev *mpp;

	if (!task->session || !task->session->mpp)
		return -EINVAL;
	mpp = task->session->mpp;

	mutex_lock(&queue->pending_lock);
	list_del_init(&task->queue_link);
	mutex_unlock(&queue->pending_lock);
	atomic_dec(&mpp->task_count);
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
	struct mpp_dev *mpp;

	if (!task->session || !task->session->mpp)
		return -EINVAL;
	mpp = task->session->mpp;

	mutex_lock(&queue->running_lock);
	list_del_init(&task->queue_link);
	mutex_unlock(&queue->running_lock);
	atomic_dec(&mpp->task_count);
	kref_put(&task->ref, mpp_free_task);

	return 0;
}

static int
mpp_taskqueue_trigger_work(struct mpp_taskqueue *queue,
			   struct workqueue_struct *workq)
{
	mutex_lock(&queue->work_lock);
	queue_work(workq, &queue->work);
	mutex_unlock(&queue->work_lock);

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

static int
mpp_session_push_pending(struct mpp_session *session,
			 struct mpp_task *task)
{
	kref_get(&task->ref);
	atomic_inc(&session->task_count);
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
	atomic_dec(&session->task_count);
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
		       "session=%p, task=%p, state=%08x, abort_request=%d\n",
		       session, task, task->state,
		       atomic_read(&task->abort_request));

	if (!session->mpp) {
		mpp_err("session %p, session->mpp is null.\n", session);
		return;
	}
	mpp = session->mpp;

	if (mpp->dev_ops->free_task)
		mpp->dev_ops->free_task(session, task);
}

static void mpp_task_timeout_work(struct work_struct *work_s)
{
	struct mpp_dev *mpp;
	struct mpp_session *session;
	struct mpp_task *task = container_of(to_delayed_work(work_s),
					     struct mpp_task,
					     timeout_work);

	mpp_err("task %p processing timed out!\n", task);

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
	up_read(&mpp->reset_group->rw_sem);
	mpp_dev_reset(mpp);
	mpp_power_off(mpp);

	/* remove task from taskqueue running list */
	task->state = TASK_STATE_TIMEOUT;
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
	INIT_DELAYED_WORK(&task->timeout_work, mpp_task_timeout_work);

	if (mpp->hw_ops->get_freq)
		mpp->hw_ops->get_freq(mpp, task);

	/*
	 * Push task to session should be in front of push task to queue.
	 * Otherwise, when mpp_task_finish finish and worker_thread call
	 * mpp_task_try_run, it may be get a task who has push in queue but
	 * not in session, cause some errors.
	 */
	mpp_session_push_pending(session, task);
	/* push current task to queue */
	mpp_taskqueue_push_pending(mpp->queue, task);
	task->state = TASK_STATE_PENDING;
	/* trigger current queue to run task */
	mpp_taskqueue_trigger_work(mpp->queue, mpp->workq);
	kref_put(&task->ref, mpp_free_task);

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

struct reset_control *
mpp_reset_control_get(struct mpp_dev *mpp, const char *name)
{
	struct mpp_reset_clk *rst_clk = NULL, *n;
	struct reset_control *clk = NULL;
	struct device *dev = mpp->dev;
	char shared_name[30] = "shared_";
	int index = 0;
	int clk_cnt = 0, i = 0;
	static const char * const clk_list[] = {
		"video_a", "video_h",
		"niu_a", "niu_h",
		"video_cabac", "video_core",
		"video_hevc_cabac",
	};

	if (!name) {
		mpp_err("error: reset clk name is null\n");
		return NULL;
	}

	clk_cnt = ARRAY_SIZE(clk_list);
	for (i = 0; i < clk_cnt; i++) {
		if (!strcmp(clk_list[i], name))
			break;
	}
	if (i == clk_cnt) {
		mpp_err("error: reset clk name is error\n");
		return NULL;
	}

	if (!dev) {
		mpp_err("error: dev is null\n");
		return NULL;
	}

	/* not shared reset clk */
	index = of_property_match_string(dev->of_node, "reset-names", name);
	if (index >= 0) {
		clk = devm_reset_control_get(dev, name);
		return clk;
	}

	/* shared reset clk */
	strncat(shared_name, name,
		sizeof(shared_name) - strlen(shared_name) - 1);
	index = of_property_match_string(dev->of_node, "reset-names",
					 shared_name);
	if (index >= 0) {
		down_write(&mpp->reset_group->rw_sem);
		list_for_each_entry_safe(rst_clk, n,
					 &mpp->reset_group->clk, link) {
			if (!strcmp(rst_clk->name, name)) {
				clk = rst_clk->clk;
				break;
			}
		}
		if (!clk) {
			rst_clk = devm_kzalloc(dev, sizeof(*rst_clk),
					       GFP_KERNEL);
			strncpy(rst_clk->name, name, sizeof(rst_clk->name));
			rst_clk->clk = devm_reset_control_get(dev,
							      shared_name);
			if (IS_ERR_OR_NULL(rst_clk->clk))
				goto fail;

			INIT_LIST_HEAD(&rst_clk->link);
			list_add_tail(&rst_clk->link, &mpp->reset_group->clk);
			clk = rst_clk->clk;
		}
		up_write(&mpp->reset_group->rw_sem);
	}

	return clk;
fail:
	devm_kfree(dev, rst_clk);
	up_write(&mpp->reset_group->rw_sem);
	return NULL;
}

static int mpp_dev_reset(struct mpp_dev *mpp)
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

	if (mpp->hw_ops->reduce_freq)
		mpp->hw_ops->reduce_freq(mpp);
	/* FIXME lock resource lock of the other devices in combo */
	down_write(&mpp->iommu_info->rw_sem);
	down_write(&mpp->reset_group->rw_sem);
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
	up_write(&mpp->reset_group->rw_sem);
	up_write(&mpp->iommu_info->rw_sem);

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

	if (mpp->hw_ops->set_freq)
		mpp->hw_ops->set_freq(mpp, task);
	/*
	 * TODO: Lock the reader locker of the device resource lock here,
	 * release at the finish operation
	 */
	down_read(&mpp->reset_group->rw_sem);

	task->state = TASK_STATE_START;
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
	struct mpp_dev *mpp;
	struct mpp_taskqueue *queue = container_of(work_s,
						   struct mpp_taskqueue,
						   work);

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
		task->state = TASK_STATE_RUNNING;
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
		} else {
			mpp_err("session %p task %p, not found in done list!\n",
				session, task);
			ret = -EIO;
		}
	} else {
		atomic_inc(&task->abort_request);
		mpp_err("timeout, pid %d session %p count %d cur_task %p.\n",
			session->pid, session,
			atomic_read(&session->task_count), task);
		/* if twice and return timeout, otherwise, re-wait */
		if (atomic_read(&task->abort_request) > 1) {
			mpp_err("session %p, task %p abort wait twice!\n",
				session, task);
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

	of_property_read_u32(dev->of_node,
			     "rockchip,taskqueue-node", &taskqueue_node);
	if (taskqueue_node >= mpp->srv->taskqueue_cnt) {
		dev_err(dev, "rockchip,taskqueue-node %d must less than %d\n",
			taskqueue_node, mpp->srv->taskqueue_cnt);
		ret = -ENODEV;
		goto err_put_pdev;
	}

	of_property_read_u32(dev->of_node,
			     "rockchip,resetgroup-node", &reset_group_node);
	if (reset_group_node >= mpp->srv->reset_group_cnt) {
		dev_err(dev, "rockchip,resetgroup-node %d must less than %d\n",
			reset_group_node, mpp->srv->reset_group_cnt);
		ret = -ENODEV;
		goto err_put_pdev;
	}

	device_lock(&pdev->dev);
	/* register current device to mpp service */
	mpp->srv->sub_devices[mpp->var->device_type] = mpp;
	/* set taskqueue which set in dtsi */
	mpp->queue = mpp->srv->task_queues[taskqueue_node];
	/* set resetgroup which set in dtsi */
	mpp->reset_group = mpp->srv->reset_groups[reset_group_node];
	mpp->srv->hw_support |= BIT(mpp->var->device_type);
	device_unlock(&pdev->dev);

	return 0;

err_put_pdev:
	platform_device_put(pdev);

	return ret;
}

int mpp_taskqueue_init(struct mpp_taskqueue *queue,
		       struct mpp_service *srv)
{
	mutex_init(&queue->work_lock);
	mutex_init(&queue->pending_lock);
	mutex_init(&queue->running_lock);
	mutex_init(&queue->mmu_lock);
	INIT_LIST_HEAD(&queue->pending_list);
	INIT_LIST_HEAD(&queue->running_list);
	INIT_LIST_HEAD(&queue->mmu_list);
	INIT_WORK(&queue->work, mpp_task_try_run);

	queue->srv = srv;

	return 0;
}

int mpp_reset_group_init(struct mpp_reset_group *group,
			 struct mpp_service *srv)
{
	init_rwsem(&group->rw_sem);
	INIT_LIST_HEAD(&group->clk);

	return 0;
}

static int mpp_check_cmd_v1(__u32 cmd)
{
	int ret;
	__u64 mask = 0;

	if (cmd >= MPP_CMD_CONTROL_BASE)
		mask = MPP_CMD_CONTROL_SUPPORT_MASK_V1;
	else if (cmd >= MPP_CMD_POLL_BASE)
		mask = MPP_CMD_POLL_SUPPORT_MASK_V1;
	else if (cmd >= MPP_CMD_SEND_BASE)
		mask = MPP_CMD_SEND_SUPPORT_MASK_V1;
	else if (cmd >= MPP_CMD_INIT_BASE)
		mask = MPP_CMD_INIT_SUPPORT_MASK_V1;
	else
		mask = MPP_CMD_QUERY_SUPPORT_MASK_V1;

	cmd &= 0x3F;
	ret = ((mask >> cmd) & 0x1) ? 0 : (-EINVAL);

	return ret;
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

		mpp = session->mpp;
		if (!mpp)
			return -EINVAL;
		hw_info = mpp->var->hw_info;
		mpp_debug(DEBUG_IOCTL, "hw_id %08x\n", hw_info->hw_id);
		if (put_user(hw_info->hw_id, (u32 __user *)req->data))
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
		session->dma = mpp_dma_session_create(mpp->dev);
		session->dma->max_buffers = mpp->session_max_buffers;
		session->mpp = mpp;
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
	case MPP_CMD_SET_REG_ADDR_OFFSET: {
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
					 val, val == 0, 1000, 50000);
		if (ret == -ETIMEDOUT) {
			mpp_err("wait task running time out\n");
		} else {
			mpp = session->mpp;
			if (!mpp)
				return -EINVAL;

			mpp_session_clear(mpp, session);
			down_write(&mpp->iommu_info->rw_sem);
			ret = mpp_dma_session_destroy(session->dma);
			up_write(&mpp->iommu_info->rw_sem);
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

			down_read(&mpp->iommu_info->rw_sem);
			buffer = mpp_dma_import_fd(mpp->iommu_info,
						   session->dma, fd);
			up_read(&mpp->iommu_info->rw_sem);
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

		mpp_err("unknown mpp ioctl cmd %x\n", req->cmd);
		return -ENOIOCTLCMD;
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

	mutex_init(&session->reg_lock);
	mutex_init(&session->pending_lock);
	mutex_init(&session->done_lock);
	INIT_LIST_HEAD(&session->pending_list);
	INIT_LIST_HEAD(&session->done_list);

	init_waitqueue_head(&session->wait);
	atomic_set(&session->task_count, 0);
	atomic_set(&session->release_request, 0);

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
				 val, val == 0, 1000, 50000);
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

		down_read(&mpp->iommu_info->rw_sem);
		mpp_dma_session_destroy(session->dma);
		up_read(&mpp->iommu_info->rw_sem);
	}

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
	struct mpp_mem_region *mem_region = NULL;
	struct mpp_dma_buffer *buffer = NULL;
	struct mpp_dev *mpp = task->session->mpp;
	struct mpp_dma_session *dma = task->session->dma;

	if (fd <= 0 || !dma || !mpp)
		return ERR_PTR(-EINVAL);

	mem_region = kzalloc(sizeof(*mem_region), GFP_KERNEL);
	if (!mem_region)
		return ERR_PTR(-ENOMEM);

	down_read(&mpp->iommu_info->rw_sem);
	buffer = mpp_dma_import_fd(mpp->iommu_info, dma, fd);
	up_read(&mpp->iommu_info->rw_sem);
	if (IS_ERR_OR_NULL(buffer)) {
		mpp_err("can't import dma-buf %d\n", fd);
		goto fail;
	}

	mem_region->hdl = buffer;
	mem_region->iova = buffer->iova;
	mem_region->len = buffer->size;

	INIT_LIST_HEAD(&mem_region->reg_link);
	mutex_lock(&task->session->reg_lock);
	list_add_tail(&mem_region->reg_link, &task->mem_region_list);
	mutex_unlock(&task->session->reg_lock);

	return mem_region;
fail:
	kfree(mem_region);
	return ERR_PTR(-ENOMEM);
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
		struct mpp_mem_region *mem_region = NULL;
		int usr_fd = reg[tbl[i]] & 0x3FF;
		int offset = reg[tbl[i]] >> 10;

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

	task->session = session;

	return 0;
}

int mpp_task_finish(struct mpp_session *session,
		    struct mpp_task *task)
{
	struct mpp_dev *mpp = session->mpp;

	if (mpp->dev_ops->finish)
		mpp->dev_ops->finish(mpp, task);

	up_read(&mpp->reset_group->rw_sem);
	if (atomic_read(&mpp->reset_request) > 0)
		mpp_dev_reset(mpp);
	mpp_power_off(mpp);

	if (!atomic_read(&task->abort_request)) {
		mpp_session_push_done(session, task);
		/* Wake up the GET thread */
		wake_up(&session->wait);
	}
	task->state = TASK_STATE_DONE;
	mpp_taskqueue_pop_running(mpp->queue, task);

	return 0;
}

int mpp_task_finalize(struct mpp_session *session,
		      struct mpp_task *task)
{
	struct mpp_dev *mpp = NULL;
	struct mpp_mem_region *mem_region = NULL, *n;

	mpp = session->mpp;
	mutex_lock(&session->reg_lock);
	/* release memory region attach to this registers table. */
	list_for_each_entry_safe(mem_region, n,
				 &task->mem_region_list,
				 reg_link) {
		down_read(&mpp->iommu_info->rw_sem);
		mpp_dma_release(session->dma, mem_region->hdl);
		up_read(&mpp->iommu_info->rw_sem);
		list_del_init(&mem_region->reg_link);
		kfree(mem_region);
	}
	mutex_unlock(&session->reg_lock);
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
	u32 i, s, e;

	if (!task)
		return -EIO;

	mpp_err("--- dump register ---\n");
	if (task->reg) {
		s = task->hw_info->reg_start;
		e = task->hw_info->reg_end;
		for (i = s; i <= e; i++) {
			u32 reg = i * sizeof(u32);

			mpp_err("reg[%03d]: %04x: 0x%08x\n",
				i, reg, task->reg[i]);
		}
	}

	return 0;
}

static int mpp_iommu_handle(struct iommu_domain *iommu,
			    struct device *iommu_dev,
			    unsigned long iova,
			    int status, void *arg)
{
	u32 i, s, e;
	struct mpp_dev *mpp = (struct mpp_dev *)arg;
	struct mpp_task *task = mpp_taskqueue_get_running_task(mpp->queue);

	dev_err(mpp->dev, "fault addr 0x%08lx status %x\n", iova, status);

	if (!task)
		return -EIO;

	mpp_task_dump_mem_region(mpp, task);

	s = task->hw_info->reg_start;
	e = task->hw_info->reg_end;
	mpp_err("--- dump register ---\n");
	for (i = s; i <= e; i++) {
		u32 reg = i * sizeof(u32);

		mpp_err("reg[%03d]: %04x: 0x%08x\n",
			i, reg, readl_relaxed(mpp->reg_base + reg));
	}

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
	atomic_set(&mpp->task_count, 0);

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
		if (mpp->hw_ops->power_on)
			mpp->hw_ops->power_on(mpp);

		hw_info->hw_id = mpp_read(mpp, hw_info->reg_id);
		if (mpp->hw_ops->power_off)
			mpp->hw_ops->power_off(mpp);
	}

	pm_runtime_put_sync(dev);

	return ret;
failed_init:
	pm_runtime_put_sync(dev);
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
	platform_device_put(mpp->pdev_srv);

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
	    list_empty(&mpp->queue->pending_list))
		mpp->hw_ops->reduce_freq(mpp);

	if (mpp->cur_task) {
		mpp->cur_task->state = TASK_STATE_IRQ;
		cancel_delayed_work(&mpp->cur_task->timeout_work);
	}
	if (mpp->dev_ops->isr)
		ret = mpp->dev_ops->isr(mpp);

	/* trigger current queue to run next task */
	mpp_taskqueue_trigger_work(mpp->queue, mpp->workq);

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

#define MPP_GRF_VAL_MASK	0xFFFF

u32 mpp_get_grf(struct mpp_grf_info *grf_info)
{
	u32 val = 0;

	if (grf_info->grf && grf_info->val)
		regmap_read(grf_info->grf,
			    grf_info->offset,
			    &val);

	return (val & MPP_GRF_VAL_MASK);
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
	mpp_debug(DEBUG_TIMING, "%s: pid: %d, session: %p, time: %ld us\n",
		  dev_name(mpp->dev), task->session->pid, task->session,
		  (end.tv_sec  - task->start.tv_sec)  * 1000000 +
		  (end.tv_usec - task->start.tv_usec));

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
		iommu->dte_addr =  mpp_iommu_get_dte_addr(iommu);
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
	mpp->hw_ops->power_on(mpp);

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

	mpp->hw_ops->power_off(mpp);
	if (!pd_is_on)
		rockchip_pmu_pd_off(mpp->dev);

	return ret;
}
