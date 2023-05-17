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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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

#define MPP_BAT_MSG_DONE		(0x00000001)

struct mpp_bat_msg {
	__u64 flag;
	__u32 fd;
	__s32 ret;
};

#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
const char *mpp_device_name[MPP_DEVICE_BUTT] = {
	[MPP_DEVICE_VDPU1]		= "VDPU1",
	[MPP_DEVICE_VDPU2]		= "VDPU2",
	[MPP_DEVICE_VDPU1_PP]		= "VDPU1_PP",
	[MPP_DEVICE_VDPU2_PP]		= "VDPU2_PP",
	[MPP_DEVICE_AV1DEC]		= "AV1DEC",
	[MPP_DEVICE_HEVC_DEC]		= "HEVC_DEC",
	[MPP_DEVICE_RKVDEC]		= "RKVDEC",
	[MPP_DEVICE_AVSPLUS_DEC]	= "AVSPLUS_DEC",
	[MPP_DEVICE_RKJPEGD]		= "RKJPEGD",
	[MPP_DEVICE_RKVENC]		= "RKVENC",
	[MPP_DEVICE_VEPU1]		= "VEPU1",
	[MPP_DEVICE_VEPU2]		= "VEPU2",
	[MPP_DEVICE_VEPU2_JPEG]		= "VEPU2",
	[MPP_DEVICE_VEPU22]		= "VEPU22",
	[MPP_DEVICE_IEP2]		= "IEP2",
	[MPP_DEVICE_VDPP]		= "VDPP",
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

static void mpp_attach_workqueue(struct mpp_dev *mpp,
				 struct mpp_taskqueue *queue);

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
	unsigned long flags;
	bool flag;

	spin_lock_irqsave(&queue->running_lock, flags);
	flag = !list_empty(&queue->running_list);
	spin_unlock_irqrestore(&queue->running_lock, flags);

	return flag;
}

int mpp_taskqueue_pending_to_run(struct mpp_taskqueue *queue, struct mpp_task *task)
{
	unsigned long flags;

	mutex_lock(&queue->pending_lock);
	spin_lock_irqsave(&queue->running_lock, flags);
	list_move_tail(&task->queue_link, &queue->running_list);
	spin_unlock_irqrestore(&queue->running_lock, flags);

	mutex_unlock(&queue->pending_lock);

	return 0;
}

static struct mpp_task *
mpp_taskqueue_get_running_task(struct mpp_taskqueue *queue)
{
	unsigned long flags;
	struct mpp_task *task = NULL;

	spin_lock_irqsave(&queue->running_lock, flags);
	task = list_first_entry_or_null(&queue->running_list,
					struct mpp_task,
					queue_link);
	spin_unlock_irqrestore(&queue->running_lock, flags);

	return task;
}

static int
mpp_taskqueue_pop_running(struct mpp_taskqueue *queue,
			  struct mpp_task *task)
{
	unsigned long flags;

	if (!task->session || !task->session->mpp)
		return -EINVAL;

	spin_lock_irqsave(&queue->running_lock, flags);
	list_del_init(&task->queue_link);
	spin_unlock_irqrestore(&queue->running_lock, flags);
	kref_put(&task->ref, mpp_free_task);

	return 0;
}

static void
mpp_taskqueue_trigger_work(struct mpp_dev *mpp)
{
	kthread_queue_work(&mpp->queue->worker, &mpp->work);
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

static void task_msgs_reset(struct mpp_task_msgs *msgs)
{
	list_del_init(&msgs->list);

	msgs->flags = 0;
	msgs->req_cnt = 0;
	msgs->set_cnt = 0;
	msgs->poll_cnt = 0;
}

static void task_msgs_init(struct mpp_task_msgs *msgs, struct mpp_session *session)
{
	INIT_LIST_HEAD(&msgs->list);

	msgs->session = session;
	msgs->queue = NULL;
	msgs->task = NULL;
	msgs->mpp = NULL;

	msgs->ext_fd = -1;

	task_msgs_reset(msgs);
}

static struct mpp_task_msgs *get_task_msgs(struct mpp_session *session)
{
	unsigned long flags;
	struct mpp_task_msgs *msgs;

	spin_lock_irqsave(&session->lock_msgs, flags);
	msgs = list_first_entry_or_null(&session->list_msgs_idle,
					struct mpp_task_msgs, list_session);
	if (msgs) {
		list_move_tail(&msgs->list_session, &session->list_msgs);
		spin_unlock_irqrestore(&session->lock_msgs, flags);

		return msgs;
	}
	spin_unlock_irqrestore(&session->lock_msgs, flags);

	msgs = kzalloc(sizeof(*msgs), GFP_KERNEL);
	task_msgs_init(msgs, session);
	INIT_LIST_HEAD(&msgs->list_session);

	spin_lock_irqsave(&session->lock_msgs, flags);
	list_move_tail(&msgs->list_session, &session->list_msgs);
	session->msgs_cnt++;
	spin_unlock_irqrestore(&session->lock_msgs, flags);

	mpp_debug_func(DEBUG_TASK_INFO, "session %d:%d msgs cnt %d\n",
		       session->pid, session->index, session->msgs_cnt);

	return msgs;
}

static void put_task_msgs(struct mpp_task_msgs *msgs)
{
	struct mpp_session *session = msgs->session;
	unsigned long flags;

	if (!session) {
		pr_err("invalid msgs without session\n");
		return;
	}

	if (msgs->ext_fd >= 0) {
		fdput(msgs->f);
		msgs->ext_fd = -1;
	}

	task_msgs_reset(msgs);

	spin_lock_irqsave(&session->lock_msgs, flags);
	list_move_tail(&msgs->list_session, &session->list_msgs_idle);
	spin_unlock_irqrestore(&session->lock_msgs, flags);
}

static void clear_task_msgs(struct mpp_session *session)
{
	struct mpp_task_msgs *msgs, *n;
	LIST_HEAD(list_to_free);
	unsigned long flags;

	spin_lock_irqsave(&session->lock_msgs, flags);

	list_for_each_entry_safe(msgs, n, &session->list_msgs, list_session)
		list_move_tail(&msgs->list_session, &list_to_free);

	list_for_each_entry_safe(msgs, n, &session->list_msgs_idle, list_session)
		list_move_tail(&msgs->list_session, &list_to_free);

	spin_unlock_irqrestore(&session->lock_msgs, flags);

	list_for_each_entry_safe(msgs, n, &list_to_free, list_session)
		kfree(msgs);
}

static void mpp_session_clear_pending(struct mpp_session *session)
{
	struct mpp_task *task = NULL, *n;

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
}

void mpp_session_cleanup_detach(struct mpp_taskqueue *queue, struct kthread_work *work)
{
	struct mpp_session *session, *n;

	if (!atomic_read(&queue->detach_count))
		return;

	mutex_lock(&queue->session_lock);
	list_for_each_entry_safe(session, n, &queue->session_detach, session_link) {
		s32 task_count = atomic_read(&session->task_count);

		if (!task_count) {
			list_del_init(&session->session_link);
			atomic_dec(&queue->detach_count);
		}

		mutex_unlock(&queue->session_lock);

		if (task_count) {
			mpp_dbg_session("session %d:%d not finished %d task cnt %d\n",
					session->device_type, session->index,
					atomic_read(&queue->detach_count), task_count);

			mpp_session_clear_pending(session);
		} else {
			mpp_dbg_session("queue detach %d\n",
					atomic_read(&queue->detach_count));

			mpp_session_deinit(session);
		}

		mutex_lock(&queue->session_lock);
	}
	mutex_unlock(&queue->session_lock);

	if (atomic_read(&queue->detach_count)) {
		mpp_dbg_session("queue detach %d again\n",
				atomic_read(&queue->detach_count));

		kthread_queue_work(&queue->worker, work);
	}
}

static struct mpp_session *mpp_session_init(void)
{
	struct mpp_session *session = kzalloc(sizeof(*session), GFP_KERNEL);

	if (!session)
		return NULL;

	session->pid = current->pid;

	mutex_init(&session->pending_lock);
	INIT_LIST_HEAD(&session->pending_list);
	INIT_LIST_HEAD(&session->service_link);
	INIT_LIST_HEAD(&session->session_link);

	atomic_set(&session->task_count, 0);
	atomic_set(&session->release_request, 0);

	INIT_LIST_HEAD(&session->list_msgs);
	INIT_LIST_HEAD(&session->list_msgs_idle);
	spin_lock_init(&session->lock_msgs);

	mpp_dbg_session("session %p init\n", session);
	return session;
}

static void mpp_session_deinit_default(struct mpp_session *session)
{
	if (session->mpp) {
		struct mpp_dev *mpp = session->mpp;

		if (mpp->dev_ops->free_session)
			mpp->dev_ops->free_session(session);

		mpp_session_clear_pending(session);

		if (session->dma) {
			mpp_iommu_down_read(mpp->iommu_info);
			mpp_dma_session_destroy(session->dma);
			mpp_iommu_up_read(mpp->iommu_info);
			session->dma = NULL;
		}
	}

	if (session->srv) {
		struct mpp_service *srv = session->srv;

		mutex_lock(&srv->session_lock);
		list_del_init(&session->service_link);
		mutex_unlock(&srv->session_lock);
	}

	list_del_init(&session->session_link);
}

void mpp_session_deinit(struct mpp_session *session)
{
	mpp_dbg_session("session %d:%d task %d deinit\n", session->pid,
			session->index, atomic_read(&session->task_count));

	if (likely(session->deinit))
		session->deinit(session);
	else
		pr_err("invalid NULL session deinit function\n");

	clear_task_msgs(session);

	kfree(session);
}

static void mpp_session_attach_workqueue(struct mpp_session *session,
					 struct mpp_taskqueue *queue)
{
	mpp_dbg_session("session %d:%d attach\n", session->pid, session->index);
	mutex_lock(&queue->session_lock);
	list_add_tail(&session->session_link, &queue->session_attach);
	mutex_unlock(&queue->session_lock);
}

static void mpp_session_detach_workqueue(struct mpp_session *session)
{
	struct mpp_taskqueue *queue;
	struct mpp_dev *mpp;

	if (!session->mpp || !session->mpp->queue)
		return;

	mpp_dbg_session("session %d:%d detach\n", session->pid, session->index);
	mpp = session->mpp;
	queue = mpp->queue;

	mutex_lock(&queue->session_lock);
	list_del_init(&session->session_link);
	list_add_tail(&session->session_link, &queue->session_detach);
	atomic_inc(&queue->detach_count);
	mutex_unlock(&queue->session_lock);

	mpp_taskqueue_trigger_work(mpp);
}

static int
mpp_session_push_pending(struct mpp_session *session,
			 struct mpp_task *task)
{
	kref_get(&task->ref);
	mutex_lock(&session->pending_lock);
	if (session->srv->timing_en) {
		task->on_pending = ktime_get();
		set_bit(TASK_TIMING_PENDING, &task->state);
	}
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

void mpp_free_task(struct kref *ref)
{
	struct mpp_dev *mpp;
	struct mpp_session *session;
	struct mpp_task *task = container_of(ref, struct mpp_task, ref);

	if (!task->session) {
		mpp_err("task %p, task->session is null.\n", task);
		return;
	}
	session = task->session;

	mpp_debug_func(DEBUG_TASK_INFO, "task %d:%d free state 0x%lx abort %d\n",
		       session->index, task->task_id, task->state,
		       atomic_read(&task->abort_request));

	mpp = mpp_get_task_used_device(task, session);
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

	if (!task->session) {
		mpp_err("task %p, task->session is null.\n", task);
		return;
	}

	session = task->session;
	mpp_err("task %d:%d:%d processing time out!\n", session->pid,
		session->index, task->task_id);

	if (!session->mpp) {
		mpp_err("session %d:%d, session mpp is null.\n", session->pid,
			session->index);
		return;
	}

	mpp_task_dump_timing(task, ktime_us_delta(ktime_get(), task->on_create));

	mpp = mpp_get_task_used_device(task, session);

	/* disable core irq */
	disable_irq(mpp->irq);
	/* disable mmu irq */
	if (mpp->iommu_info && mpp->iommu_info->got_irq)
		disable_irq(mpp->iommu_info->irq);

	/* hardware maybe dead, reset it */
	mpp_reset_up_read(mpp->reset_group);
	mpp_dev_reset(mpp);
	mpp_power_off(mpp);

	set_bit(TASK_STATE_TIMEOUT, &task->state);
	set_bit(TASK_STATE_DONE, &task->state);
	/* Wake up the GET thread */
	wake_up(&task->wait);

	/* remove task from taskqueue running list */
	mpp_taskqueue_pop_running(mpp->queue, task);

	/* enable core irq */
	enable_irq(mpp->irq);
	/* enable mmu irq */
	if (mpp->iommu_info && mpp->iommu_info->got_irq)
		enable_irq(mpp->iommu_info->irq);

	mpp_taskqueue_trigger_work(mpp);
}

static int mpp_process_task_default(struct mpp_session *session,
				    struct mpp_task_msgs *msgs)
{
	struct mpp_task *task = NULL;
	struct mpp_dev *mpp = session->mpp;
	u32 timing_en;
	ktime_t on_create;

	if (unlikely(!mpp)) {
		mpp_err("pid %d client %d found invalid process function\n",
			session->pid, session->device_type);
		return -EINVAL;
	}

	timing_en = session->srv->timing_en;
	if (timing_en)
		on_create = ktime_get();

	if (mpp->dev_ops->alloc_task)
		task = mpp->dev_ops->alloc_task(session, msgs);
	if (!task) {
		mpp_err("alloc_task failed.\n");
		return -ENOMEM;
	}

	if (timing_en) {
		task->on_create_end = ktime_get();
		task->on_create = on_create;
		set_bit(TASK_TIMING_CREATE_END, &task->state);
		set_bit(TASK_TIMING_CREATE, &task->state);
	}

	/* ensure current device */
	mpp = mpp_get_task_used_device(task, session);

	kref_init(&task->ref);
	init_waitqueue_head(&task->wait);
	atomic_set(&task->abort_request, 0);
	task->task_index = atomic_fetch_inc(&mpp->task_index);
	task->task_id = atomic_fetch_inc(&mpp->queue->task_id);
	INIT_DELAYED_WORK(&task->timeout_work, mpp_task_timeout_work);

	if (mpp->auto_freq_en && mpp->hw_ops->get_freq)
		mpp->hw_ops->get_freq(mpp, task);

	msgs->queue = mpp->queue;
	msgs->task = task;
	msgs->mpp = mpp;

	/*
	 * Push task to session should be in front of push task to queue.
	 * Otherwise, when mpp_task_finish finish and worker_thread call
	 * task worker, it may be get a task who has push in queue but
	 * not in session, cause some errors.
	 */
	atomic_inc(&session->task_count);
	mpp_session_push_pending(session, task);

	return 0;
}

static int mpp_process_task(struct mpp_session *session,
			    struct mpp_task_msgs *msgs)
{
	if (likely(session->process_task))
		return session->process_task(session, msgs);

	pr_err("invalid NULL process task function\n");
	return -EINVAL;
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

	if (mpp->hw_ops->reset)
		mpp->hw_ops->reset(mpp);

	/* Note: if the domain does not change, iommu attach will be return
	 * as an empty operation. Therefore, force to close and then open,
	 * will be update the domain. In this way, domain can really attach.
	 */
	mpp_iommu_refresh(mpp->iommu_info, mpp->dev);

	mpp_reset_up_write(mpp->reset_group);
	mpp_iommu_up_write(mpp->iommu_info);

	dev_info(mpp->dev, "reset done\n");

	return 0;
}

void mpp_task_run_begin(struct mpp_task *task, u32 timing_en, u32 timeout)
{
	preempt_disable();

	set_bit(TASK_STATE_START, &task->state);

	mpp_time_record(task);
	schedule_delayed_work(&task->timeout_work, msecs_to_jiffies(timeout));

	if (timing_en) {
		task->on_sched_timeout = ktime_get();
		set_bit(TASK_TIMING_TO_SCHED, &task->state);
	}
}

void mpp_task_run_end(struct mpp_task *task, u32 timing_en)
{
	if (timing_en) {
		task->on_run_end = ktime_get();
		set_bit(TASK_TIMING_RUN_END, &task->state);
	}

#ifdef MODULE
	preempt_enable();
#else
	preempt_enable_no_resched();
#endif
}

static int mpp_task_run(struct mpp_dev *mpp,
			struct mpp_task *task)
{
	int ret;
	u32 timing_en;

	mpp_debug_enter();

	timing_en = mpp->srv->timing_en;
	if (timing_en) {
		task->on_run = ktime_get();
		set_bit(TASK_TIMING_RUN, &task->state);
	}

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
	 * Lock the reader locker of the device resource lock here,
	 * release at the finish operation
	 */
	mpp_reset_down_read(mpp->reset_group);

	/*
	 * for iommu share hardware, should attach to ensure
	 * working in current device
	 */
	ret = mpp_iommu_attach(mpp->iommu_info);
	if (ret) {
		dev_err(mpp->dev, "mpp_iommu_attach failed\n");
		mpp_reset_up_read(mpp->reset_group);
		return -ENODATA;
	}

	mpp_power_on(mpp);
	mpp_debug_func(DEBUG_TASK_INFO, "pid %d run %s\n",
		       task->session->pid, dev_name(mpp->dev));

	if (mpp->auto_freq_en && mpp->hw_ops->set_freq)
		mpp->hw_ops->set_freq(mpp, task);

	mpp_iommu_dev_activate(mpp->iommu_info, mpp);
	if (mpp->dev_ops->run)
		mpp->dev_ops->run(mpp, task);

	mpp_debug_leave();

	return 0;
}

static void mpp_task_worker_default(struct kthread_work *work_s)
{
	struct mpp_task *task;
	struct mpp_dev *mpp = container_of(work_s, struct mpp_dev, work);
	struct mpp_taskqueue *queue = mpp->queue;

	mpp_debug_enter();

again:
	task = mpp_taskqueue_get_pending_task(queue);
	if (!task)
		goto done;

	/* if task timeout and aborted, remove it */
	if (atomic_read(&task->abort_request) > 0) {
		mpp_taskqueue_pop_pending(queue, task);
		goto again;
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
		struct mpp_dev *task_mpp = mpp_get_task_used_device(task, task->session);

		atomic_inc(&task_mpp->task_count);
		mpp_taskqueue_pending_to_run(queue, task);
		set_bit(TASK_STATE_RUNNING, &task->state);
		if (mpp_task_run(task_mpp, task))
			mpp_taskqueue_pop_running(queue, task);
		else
			goto again;
	}

done:
	mpp_session_cleanup_detach(queue, work_s);
}

static int mpp_wait_result_default(struct mpp_session *session,
				   struct mpp_task_msgs *msgs)
{
	int ret;
	struct mpp_task *task;
	struct mpp_dev *mpp;

	task = mpp_session_get_pending_task(session);
	if (!task) {
		mpp_err("session %d:%d pending list is empty!\n",
			session->pid, session->index);
		return -EIO;
	}
	mpp = mpp_get_task_used_device(task, session);

	ret = wait_event_interruptible(task->wait, test_bit(TASK_STATE_DONE, &task->state));
	if (ret == -ERESTARTSYS)
		mpp_err("wait task break by signal\n");

	if (mpp->dev_ops->result)
		ret = mpp->dev_ops->result(mpp, task, msgs);
	mpp_debug_func(DEBUG_TASK_INFO, "wait done session %d:%d count %d task %d state %lx\n",
		       session->device_type, session->index, atomic_read(&session->task_count),
		       task->task_index, task->state);

	mpp_session_pop_pending(session, task);

	return ret;
}

static int mpp_wait_result(struct mpp_session *session,
			   struct mpp_task_msgs *msgs)
{
	if (likely(session->wait_result))
		return session->wait_result(session, msgs);

	pr_err("invalid NULL wait result function\n");
	return -EINVAL;
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
		return -ENODEV;
	}

	mpp->srv = platform_get_drvdata(pdev);
	platform_device_put(pdev);
	if (!mpp->srv) {
		dev_err(dev, "failed attach service\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(dev->of_node,
				   "rockchip,taskqueue-node", &taskqueue_node);
	if (ret) {
		dev_err(dev, "failed to get taskqueue-node\n");
		return ret;
	} else if (taskqueue_node >= mpp->srv->taskqueue_cnt) {
		dev_err(dev, "taskqueue-node %d must less than %d\n",
			taskqueue_node, mpp->srv->taskqueue_cnt);
		return -ENODEV;
	}
	/* set taskqueue according dtsi */
	queue = mpp->srv->task_queues[taskqueue_node];
	if (!queue) {
		dev_err(dev, "taskqueue attach to invalid node %d\n",
			taskqueue_node);
		return -ENODEV;
	}
	mpp_attach_workqueue(mpp, queue);

	ret = of_property_read_u32(dev->of_node,
				   "rockchip,resetgroup-node", &reset_group_node);
	if (!ret) {
		/* set resetgroup according dtsi */
		if (reset_group_node >= mpp->srv->reset_group_cnt) {
			dev_err(dev, "resetgroup-node %d must less than %d\n",
				reset_group_node, mpp->srv->reset_group_cnt);
			return -ENODEV;
		} else {
			mpp->reset_group = mpp->srv->reset_groups[reset_group_node];
			if (!mpp->reset_group->queue)
				mpp->reset_group->queue = queue;
			if (mpp->reset_group->queue != mpp->queue)
				mpp->reset_group->rw_sem_on = true;
		}
	}

	return 0;
}

struct mpp_taskqueue *mpp_taskqueue_init(struct device *dev)
{
	struct mpp_taskqueue *queue = devm_kzalloc(dev, sizeof(*queue),
						   GFP_KERNEL);
	if (!queue)
		return NULL;

	mutex_init(&queue->session_lock);
	mutex_init(&queue->pending_lock);
	spin_lock_init(&queue->running_lock);
	mutex_init(&queue->mmu_lock);
	mutex_init(&queue->dev_lock);
	INIT_LIST_HEAD(&queue->session_attach);
	INIT_LIST_HEAD(&queue->session_detach);
	INIT_LIST_HEAD(&queue->pending_list);
	INIT_LIST_HEAD(&queue->running_list);
	INIT_LIST_HEAD(&queue->mmu_list);
	INIT_LIST_HEAD(&queue->dev_list);

	/* default taskqueue has max 16 task capacity */
	queue->task_capacity = MPP_MAX_TASK_CAPACITY;
	atomic_set(&queue->reset_request, 0);
	atomic_set(&queue->detach_count, 0);
	atomic_set(&queue->task_id, 0);
	queue->dev_active_flags = 0;

	return queue;
}

static void mpp_attach_workqueue(struct mpp_dev *mpp,
				 struct mpp_taskqueue *queue)
{
	s32 core_id;

	INIT_LIST_HEAD(&mpp->queue_link);

	mutex_lock(&queue->dev_lock);

	if (mpp->core_id >= 0)
		core_id = mpp->core_id;
	else
		core_id = queue->core_count;

	if (core_id < 0 || core_id >= MPP_MAX_CORE_NUM) {
		dev_err(mpp->dev, "invalid core id %d\n", core_id);
		goto done;
	}

	/*
	 * multi devices with no multicores share one queue,
	 * the core_id is default value 0.
	 */
	if (queue->cores[core_id]) {
		if (queue->cores[core_id] == mpp)
			goto done;

		core_id = queue->core_count;
	}

	queue->cores[core_id] = mpp;
	queue->core_count++;

	set_bit(core_id, &queue->core_idle);
	list_add_tail(&mpp->queue_link, &queue->dev_list);
	if (queue->core_id_max < (u32)core_id)
		queue->core_id_max = (u32)core_id;

	mpp->core_id = core_id;
	mpp->queue = queue;

	mpp_dbg_core("%s attach queue as core %d\n",
			dev_name(mpp->dev), mpp->core_id);

	if (queue->task_capacity > mpp->task_capacity)
		queue->task_capacity = mpp->task_capacity;

done:
	mutex_unlock(&queue->dev_lock);
}

static void mpp_detach_workqueue(struct mpp_dev *mpp)
{
	struct mpp_taskqueue *queue = mpp->queue;

	if (queue) {
		mutex_lock(&queue->dev_lock);

		queue->cores[mpp->core_id] = NULL;
		queue->core_count--;

		clear_bit(mpp->core_id, &queue->core_idle);
		list_del_init(&mpp->queue_link);

		mpp->queue = NULL;

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

	mpp_debug(DEBUG_IOCTL, "cmd %x process\n", req->cmd);

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
		if (mpp->dev_ops) {
			if (mpp->dev_ops->process_task)
				session->process_task =
					mpp->dev_ops->process_task;

			if (mpp->dev_ops->wait_result)
				session->wait_result =
					mpp->dev_ops->wait_result;

			if (mpp->dev_ops->deinit)
				session->deinit = mpp->dev_ops->deinit;
		}
		session->index = atomic_fetch_inc(&mpp->session_index);
		if (mpp->dev_ops && mpp->dev_ops->init_session) {
			ret = mpp->dev_ops->init_session(session);
			if (ret)
				return ret;
		}

		mpp_session_attach_workqueue(session, mpp->queue);
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
		msgs->poll_req = NULL;
	} break;
	case MPP_CMD_POLL_HW_IRQ: {
		if (msgs->poll_cnt || msgs->poll_req)
			mpp_err("Do NOT poll hw irq when previous call not return\n");

		msgs->flags |= req->flags;
		msgs->poll_cnt++;

		if (req->size && req->data) {
			if (!msgs->poll_req)
				msgs->poll_req = req;
		} else {
			msgs->poll_req = NULL;
		}
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

			mpp_session_clear_pending(session);
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
			mpp_err("pid %d not find client %d\n",
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

static void task_msgs_add(struct mpp_task_msgs *msgs, struct list_head *head)
{
	struct mpp_session *session = msgs->session;
	int ret = 0;

	/* process each task */
	if (msgs->set_cnt) {
		/* NOTE: update msg_flags for fd over 1024 */
		session->msg_flags = msgs->flags;
		ret = mpp_process_task(session, msgs);
	}

	if (!ret) {
		INIT_LIST_HEAD(&msgs->list);
		list_add_tail(&msgs->list, head);
	} else {
		put_task_msgs(msgs);
	}
}

static int mpp_collect_msgs(struct list_head *head, struct mpp_session *session,
			    unsigned int cmd, void __user *msg)
{
	struct mpp_msg_v1 msg_v1;
	struct mpp_request *req;
	struct mpp_task_msgs *msgs = NULL;
	int last = 1;
	int ret;

	if (cmd != MPP_IOC_CFG_V1) {
		mpp_err("unknown ioctl cmd %x\n", cmd);
		return -EINVAL;
	}

next:
	/* first, parse to fixed struct */
	if (copy_from_user(&msg_v1, msg, sizeof(msg_v1)))
		return -EFAULT;

	msg += sizeof(msg_v1);

	mpp_debug(DEBUG_IOCTL, "cmd %x collect flags %08x, size %d, offset %x\n",
		  msg_v1.cmd, msg_v1.flags, msg_v1.size, msg_v1.offset);

	if (mpp_check_cmd_v1(msg_v1.cmd)) {
		mpp_err("mpp cmd %x is not supported.\n", msg_v1.cmd);
		return -EFAULT;
	}

	if (msg_v1.flags & MPP_FLAGS_MULTI_MSG)
		last = (msg_v1.flags & MPP_FLAGS_LAST_MSG) ? 1 : 0;
	else
		last = 1;

	/* check cmd for change msgs session */
	if (msg_v1.cmd == MPP_CMD_SET_SESSION_FD) {
		struct mpp_bat_msg bat_msg;
		struct mpp_bat_msg __user *usr_cmd;
		struct fd f;

		/* try session switch here */
		usr_cmd = (struct mpp_bat_msg __user *)(unsigned long)msg_v1.data_ptr;

		if (copy_from_user(&bat_msg, usr_cmd, sizeof(bat_msg)))
			return -EFAULT;

		/* skip finished message */
		if (bat_msg.flag & MPP_BAT_MSG_DONE)
			goto session_switch_done;

		f = fdget(bat_msg.fd);
		if (!f.file) {
			int ret = -EBADF;

			mpp_err("fd %d get session failed\n", bat_msg.fd);

			if (copy_to_user(&usr_cmd->ret, &ret, sizeof(usr_cmd->ret)))
				mpp_err("copy_to_user failed.\n");
			goto session_switch_done;
		}

		/* NOTE: add previous ready task to queue and drop empty task */
		if (msgs) {
			if (msgs->req_cnt)
				task_msgs_add(msgs, head);
			else
				put_task_msgs(msgs);

			msgs = NULL;
		}

		/* switch session */
		session = f.file->private_data;
		msgs = get_task_msgs(session);

		if (f.file->private_data == session)
			msgs->ext_fd = bat_msg.fd;

		msgs->f = f;

		mpp_debug(DEBUG_IOCTL, "fd %d, session %d msg_cnt %d\n",
				bat_msg.fd, session->index, session->msgs_cnt);

session_switch_done:
		/* session id should NOT be the last message */
		if (last)
			return 0;

		goto next;
	}

	if (!msgs)
		msgs = get_task_msgs(session);

	if (!msgs) {
		pr_err("session %d:%d failed to get task msgs",
		       session->pid, session->index);
		return -EINVAL;
	}

	if (msgs->req_cnt >= MPP_MAX_MSG_NUM) {
		mpp_err("session %d message count %d more than %d.\n",
			session->index, msgs->req_cnt, MPP_MAX_MSG_NUM);
		return -EINVAL;
	}

	req = &msgs->reqs[msgs->req_cnt++];
	req->cmd = msg_v1.cmd;
	req->flags = msg_v1.flags;
	req->size = msg_v1.size;
	req->offset = msg_v1.offset;
	req->data = (void __user *)(unsigned long)msg_v1.data_ptr;

	ret = mpp_process_request(session, session->srv, req, msgs);
	if (ret) {
		mpp_err("session %d process cmd %x ret %d\n",
			session->index, req->cmd, ret);
		return ret;
	}

	if (!last)
		goto next;

	task_msgs_add(msgs, head);
	msgs = NULL;

	return 0;
}

static void mpp_msgs_trigger(struct list_head *msgs_list)
{
	struct mpp_task_msgs *msgs, *n;
	struct mpp_dev *mpp_prev = NULL;
	struct mpp_taskqueue *queue_prev = NULL;

	/* push task to queue */
	list_for_each_entry_safe(msgs, n, msgs_list, list) {
		struct mpp_dev *mpp;
		struct mpp_task *task;
		struct mpp_taskqueue *queue;

		if (!msgs->set_cnt || !msgs->queue)
			continue;

		mpp = msgs->mpp;
		task = msgs->task;
		queue = msgs->queue;

		if (queue_prev != queue) {
			if (queue_prev && mpp_prev) {
				mutex_unlock(&queue_prev->pending_lock);
				mpp_taskqueue_trigger_work(mpp_prev);
			}

			if (queue)
				mutex_lock(&queue->pending_lock);

			mpp_prev = mpp;
			queue_prev = queue;
		}

		if (test_bit(TASK_STATE_ABORT, &task->state))
			pr_info("try to trigger abort task %d\n", task->task_id);

		set_bit(TASK_STATE_PENDING, &task->state);
		list_add_tail(&task->queue_link, &queue->pending_list);
	}

	if (mpp_prev && queue_prev) {
		mutex_unlock(&queue_prev->pending_lock);
		mpp_taskqueue_trigger_work(mpp_prev);
	}
}

static void mpp_msgs_wait(struct list_head *msgs_list)
{
	struct mpp_task_msgs *msgs, *n;

	/* poll and release each task */
	list_for_each_entry_safe(msgs, n, msgs_list, list) {
		struct mpp_session *session = msgs->session;

		if (msgs->poll_cnt) {
			int ret = mpp_wait_result(session, msgs);

			if (ret) {
				mpp_err("session %d wait result ret %d\n",
					session->index, ret);
			}
		}

		put_task_msgs(msgs);

	}
}

static long mpp_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct mpp_service *srv;
	struct mpp_session *session = (struct mpp_session *)filp->private_data;
	struct list_head msgs_list;
	int ret = 0;

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

	INIT_LIST_HEAD(&msgs_list);

	ret = mpp_collect_msgs(&msgs_list, session, cmd, (void __user *)arg);
	if (ret)
		mpp_err("collect msgs failed %d\n", ret);

	mpp_msgs_trigger(&msgs_list);

	mpp_msgs_wait(&msgs_list);

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

	session = mpp_session_init();
	if (!session)
		return -ENOMEM;

	session->srv = srv;

	if (session->srv) {
		mutex_lock(&srv->session_lock);
		list_add_tail(&session->service_link, &srv->session_list);
		mutex_unlock(&srv->session_lock);
	}
	session->process_task = mpp_process_task_default;
	session->wait_result = mpp_wait_result_default;
	session->deinit = mpp_session_deinit_default;
	filp->private_data = (void *)session;

	mpp_debug_leave();

	return nonseekable_open(inode, filp);
}

static int mpp_dev_release(struct inode *inode, struct file *filp)
{
	struct mpp_session *session = filp->private_data;

	mpp_debug_enter();

	if (!session) {
		mpp_err("session is null\n");
		return -EINVAL;
	}

	/* wait for task all done */
	atomic_inc(&session->release_request);

	if (session->mpp || atomic_read(&session->task_count))
		mpp_session_detach_workqueue(session);
	else
		mpp_session_deinit(session);

	filp->private_data = NULL;

	mpp_debug_leave();
	return 0;
}

const struct file_operations rockchip_mpp_fops = {
	.open		= mpp_dev_open,
	.release	= mpp_dev_release,
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
		mem_region->is_dup = true;
	} else {
		mpp_iommu_down_read(mpp->iommu_info);
		buffer = mpp_dma_import_fd(mpp->iommu_info, dma, fd);
		mpp_iommu_up_read(mpp->iommu_info);
		if (IS_ERR(buffer)) {
			mpp_err("can't import dma-buf %d\n", fd);
			return ERR_CAST(buffer);
		}

		mem_region->hdl = buffer;
		mem_region->iova = buffer->iova;
		mem_region->len = buffer->size;
		mem_region->fd = fd;
		mem_region->is_dup = false;
	}
	task->mem_count++;
	INIT_LIST_HEAD(&mem_region->reg_link);
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
		struct mpp_dev *mpp = mpp_get_task_used_device(task, session);
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

int mpp_task_init(struct mpp_session *session, struct mpp_task *task)
{
	INIT_LIST_HEAD(&task->pending_link);
	INIT_LIST_HEAD(&task->queue_link);
	INIT_LIST_HEAD(&task->mem_region_list);
	task->state = 0;
	task->mem_count = 0;
	task->session = session;

	return 0;
}

int mpp_task_finish(struct mpp_session *session,
		    struct mpp_task *task)
{
	struct mpp_dev *mpp = mpp_get_task_used_device(task, session);

	if (mpp->dev_ops->finish)
		mpp->dev_ops->finish(mpp, task);

	mpp_reset_up_read(mpp->reset_group);
	if (atomic_read(&mpp->reset_request) > 0)
		mpp_dev_reset(mpp);
	mpp_power_off(mpp);

	set_bit(TASK_STATE_FINISH, &task->state);
	set_bit(TASK_STATE_DONE, &task->state);

	if (session->srv->timing_en) {
		s64 time_diff;

		task->on_finish = ktime_get();
		set_bit(TASK_TIMING_FINISH, &task->state);

		time_diff = ktime_us_delta(task->on_finish, task->on_create);

		if (mpp->timing_check && time_diff > (s64)mpp->timing_check)
			mpp_task_dump_timing(task, time_diff);
	}

	/* Wake up the GET thread */
	wake_up(&task->wait);
	mpp_taskqueue_pop_running(mpp->queue, task);

	return 0;
}

int mpp_task_finalize(struct mpp_session *session,
		      struct mpp_task *task)
{
	struct mpp_mem_region *mem_region = NULL, *n;
	struct mpp_dev *mpp = mpp_get_task_used_device(task, session);

	/* release memory region attach to this registers table. */
	list_for_each_entry_safe(mem_region, n,
				 &task->mem_region_list,
				 reg_link) {
		if (!mem_region->is_dup) {
			mpp_iommu_down_read(mpp->iommu_info);
			mpp_dma_release(session->dma, mem_region->hdl);
			mpp_iommu_up_read(mpp->iommu_info);
		}
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

	mpp_err("--- dump task %d mem region ---\n", task->task_index);
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

int mpp_task_dump_hw_reg(struct mpp_dev *mpp)
{
	u32 i;
	u32 s = mpp->var->hw_info->reg_start;
	u32 e = mpp->var->hw_info->reg_end;

	mpp_err("--- dump hardware register ---\n");
	for (i = s; i <= e; i++) {
		u32 reg = i * sizeof(u32);

		mpp_err("reg[%03d]: %04x: 0x%08x\n",
				i, reg, readl_relaxed(mpp->reg_base + reg));
	}

	return 0;
}

void mpp_reg_show(struct mpp_dev *mpp, u32 offset)
{
	if (!mpp)
		return;

	dev_err(mpp->dev, "reg[%03d]: %04x: 0x%08x\n",
		offset >> 2, offset, mpp_read_relaxed(mpp, offset));
}

void mpp_reg_show_range(struct mpp_dev *mpp, u32 start, u32 end)
{
	u32 offset;

	if (!mpp)
		return;

	for (offset = start; offset < end; offset += sizeof(u32))
		mpp_reg_show(mpp, offset);
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
	/* read flag for pum idle request */
	mpp->skip_idle = device_property_read_bool(dev, "rockchip,skip-pmu-idle-request");

	/* read link table capacity */
	ret = of_property_read_u32(np, "rockchip,task-capacity",
				   &mpp->task_capacity);
	if (ret)
		mpp->task_capacity = 1;

	mpp->dev = dev;
	mpp->hw_ops = mpp->var->hw_ops;
	mpp->dev_ops = mpp->var->dev_ops;

	/* Get and attach to service */
	ret = mpp_attach_service(mpp, dev);
	if (ret) {
		dev_err(dev, "failed to attach service\n");
		return -ENODEV;
	}

	/* power domain autosuspend delay 2s */
	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);

	kthread_init_work(&mpp->work, mpp_task_worker_default);

	atomic_set(&mpp->reset_request, 0);
	atomic_set(&mpp->session_index, 0);
	atomic_set(&mpp->task_count, 0);
	atomic_set(&mpp->task_index, 0);

	device_init_wakeup(dev, true);
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
	mpp->io_base = res->start;

	/*
	 * TODO: here or at the device itself, some device does not
	 * have the iommu, maybe in the device is better.
	 */
	mpp->iommu_info = mpp_iommu_probe(dev);
	if (IS_ERR(mpp->iommu_info)) {
		dev_err(dev, "failed to attach iommu\n");
		mpp->iommu_info = NULL;
	}
	if (mpp->hw_ops->init) {
		ret = mpp->hw_ops->init(mpp);
		if (ret)
			goto failed;
	}

	/* read hardware id */
	if (hw_info->reg_id >= 0) {
		pm_runtime_get_sync(dev);
		if (mpp->hw_ops->clk_on)
			mpp->hw_ops->clk_on(mpp);

		hw_info->hw_id = mpp_read(mpp, hw_info->reg_id * sizeof(u32));
		if (mpp->hw_ops->clk_off)
			mpp->hw_ops->clk_off(mpp);
		pm_runtime_put_sync(dev);
	}

	return ret;
failed:
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
	mpp_detach_workqueue(mpp);
	device_init_wakeup(mpp->dev, false);
	pm_runtime_disable(mpp->dev);

	return 0;
}

void mpp_dev_shutdown(struct platform_device *pdev)
{
	int ret;
	int val;
	struct device *dev = &pdev->dev;
	struct mpp_dev *mpp = dev_get_drvdata(dev);

	dev_info(dev, "shutdown device\n");

	atomic_inc(&mpp->srv->shutdown_request);
	ret = readx_poll_timeout(atomic_read,
				 &mpp->task_count,
				 val, val == 0, 20000, 200000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "wait total %d running time out\n",
			atomic_read(&mpp->task_count));
	else
		dev_info(dev, "shutdown success\n");
}

int mpp_dev_register_srv(struct mpp_dev *mpp, struct mpp_service *srv)
{
	enum MPP_DEVICE_TYPE device_type = mpp->var->device_type;

	srv->sub_devices[device_type] = mpp;
	set_bit(device_type, &srv->hw_support);

	return 0;
}

irqreturn_t mpp_dev_irq(int irq, void *param)
{
	struct mpp_dev *mpp = param;
	struct mpp_task *task = mpp->cur_task;
	irqreturn_t irq_ret = IRQ_NONE;
	u32 timing_en = mpp->srv->timing_en;

	if (task && timing_en) {
		task->on_irq = ktime_get();
		set_bit(TASK_TIMING_IRQ, &task->state);
	}

	if (mpp->dev_ops->irq)
		irq_ret = mpp->dev_ops->irq(mpp);

	if (task) {
		if (irq_ret == IRQ_WAKE_THREAD) {
			/* if wait or delayed work timeout, abort request will turn on,
			 * isr should not to response, and handle it in delayed work
			 */
			if (test_and_set_bit(TASK_STATE_HANDLE, &task->state)) {
				mpp_err("error, task has been handled, irq_status %08x\n",
					mpp->irq_status);
				irq_ret = IRQ_HANDLED;
				goto done;
			}
			if (timing_en) {
				task->on_cancel_timeout = ktime_get();
				set_bit(TASK_TIMING_TO_CANCEL, &task->state);
			}
			cancel_delayed_work(&task->timeout_work);
			/* normal condition, set state and wake up isr thread */
			set_bit(TASK_STATE_IRQ, &task->state);
		}

		if (irq_ret == IRQ_WAKE_THREAD)
			mpp_iommu_dev_deactivate(mpp->iommu_info, mpp);
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
	struct mpp_task *task = mpp->cur_task;

	if (task && mpp->srv->timing_en) {
		task->on_isr = ktime_get();
		set_bit(TASK_TIMING_ISR, &task->state);
	}

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
	if (mpp_debug_unlikely(DEBUG_TIMING) && task) {
		task->start = ktime_get();
		task->part = task->start;
	}

	return 0;
}

int mpp_time_part_diff(struct mpp_task *task)
{
	if (mpp_debug_unlikely(DEBUG_TIMING)) {
		ktime_t end;
		struct mpp_dev *mpp = mpp_get_task_used_device(task, task->session);

		end = ktime_get();
		mpp_debug(DEBUG_PART_TIMING, "%s:%d session %d:%d part time: %lld us\n",
			dev_name(mpp->dev), task->core_id, task->session->pid,
			task->session->index, ktime_us_delta(end, task->part));
		task->part = end;
	}

	return 0;
}

int mpp_time_diff(struct mpp_task *task)
{
	if (mpp_debug_unlikely(DEBUG_TIMING)) {
		ktime_t end;
		struct mpp_dev *mpp = mpp_get_task_used_device(task, task->session);

		end = ktime_get();
		mpp_debug(DEBUG_TIMING, "%s:%d session %d:%d time: %lld us\n",
			dev_name(mpp->dev), task->core_id, task->session->pid,
			task->session->index, ktime_us_delta(end, task->start));
	}

	return 0;
}

int mpp_time_diff_with_hw_time(struct mpp_task *task, u32 clk_hz)
{
	if (mpp_debug_unlikely(DEBUG_TIMING)) {
		ktime_t end;
		struct mpp_dev *mpp = mpp_get_task_used_device(task, task->session);

		end = ktime_get();

		if (clk_hz)
			mpp_debug(DEBUG_TIMING, "%s:%d session %d:%d time: %lld us hw %d us\n",
				dev_name(mpp->dev), task->core_id, task->session->pid,
				task->session->index, ktime_us_delta(end, task->start),
				task->hw_cycles / (clk_hz / 1000000));
		else
			mpp_debug(DEBUG_TIMING, "%s:%d session %d:%d time: %lld us\n",
				dev_name(mpp->dev), task->core_id, task->session->pid,
				task->session->index, ktime_us_delta(end, task->start));
	}

	return 0;
}

#define LOG_TIMING(state, id, stage, time, base) \
	do { \
		if (test_bit(id, &state)) \
			pr_info("timing: %-14s : %lld us\n", stage, ktime_us_delta(time, base)); \
		else \
			pr_info("timing: %-14s : invalid\n", stage); \
	} while (0)

void mpp_task_dump_timing(struct mpp_task *task, s64 time_diff)
{
	ktime_t s = task->on_create;
	unsigned long state = task->state;

	pr_info("task %d dump timing at %lld us:", task->task_id, time_diff);

	pr_info("timing: %-14s : %lld us\n", "create", ktime_to_us(s));
	LOG_TIMING(state, TASK_TIMING_CREATE_END, "create end",     task->on_create_end, s);
	LOG_TIMING(state, TASK_TIMING_PENDING,    "pending",        task->on_pending, s);
	LOG_TIMING(state, TASK_TIMING_RUN,        "run",            task->on_run, s);
	LOG_TIMING(state, TASK_TIMING_TO_SCHED,   "timeout start",  task->on_sched_timeout, s);
	LOG_TIMING(state, TASK_TIMING_RUN_END,    "run end",        task->on_run_end, s);
	LOG_TIMING(state, TASK_TIMING_IRQ,        "irq",            task->on_irq, s);
	LOG_TIMING(state, TASK_TIMING_TO_CANCEL,  "timeout cancel", task->on_cancel_timeout, s);
	LOG_TIMING(state, TASK_TIMING_ISR,        "isr",            task->on_isr, s);
	LOG_TIMING(state, TASK_TIMING_FINISH,     "finish",         task->on_finish, s);
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
		clk_info->real_rate_hz = clk_get_rate(clk_info->clk);
	}

	return 0;
}

#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
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

void mpp_procfs_create_common(struct proc_dir_entry *parent, struct mpp_dev *mpp)
{
	mpp_procfs_create_u32("disable_work", 0644, parent, &mpp->disable);
	mpp_procfs_create_u32("timing_check", 0644, parent, &mpp->timing_check);
}
#endif
