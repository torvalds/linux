// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2012-2016, 2018-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <mali_kbase.h>
#include <linux/spinlock.h>
#include <mali_kbase_hwaccess_jm.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)

static bool kbase_is_job_fault_event_pending(struct kbase_device *kbdev)
{
	struct list_head *event_list = &kbdev->job_fault_event_list;
	unsigned long    flags;
	bool             ret;

	spin_lock_irqsave(&kbdev->job_fault_event_lock, flags);
	ret = !list_empty(event_list);
	spin_unlock_irqrestore(&kbdev->job_fault_event_lock, flags);

	return ret;
}

static void kbase_ctx_remove_pending_event(struct kbase_context *kctx)
{
	struct list_head *event_list = &kctx->kbdev->job_fault_event_list;
	struct base_job_fault_event *event;
	unsigned long flags;

	spin_lock_irqsave(&kctx->kbdev->job_fault_event_lock, flags);
	list_for_each_entry(event, event_list, head) {
		if (event->katom->kctx == kctx) {
			list_del(&event->head);
			spin_unlock_irqrestore(&kctx->kbdev->job_fault_event_lock, flags);

			wake_up(&kctx->kbdev->job_fault_resume_wq);
			flush_work(&event->job_fault_work);

			/* job_fault_event_list can only have a single atom for
			 * each context.
			 */
			return;
		}
	}
	spin_unlock_irqrestore(&kctx->kbdev->job_fault_event_lock, flags);
}

static bool kbase_ctx_has_no_event_pending(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	struct list_head *event_list = &kctx->kbdev->job_fault_event_list;
	struct base_job_fault_event *event;
	unsigned long               flags;

	spin_lock_irqsave(&kbdev->job_fault_event_lock, flags);
	if (list_empty(event_list)) {
		spin_unlock_irqrestore(&kbdev->job_fault_event_lock, flags);
		return true;
	}
	list_for_each_entry(event, event_list, head) {
		if (event->katom->kctx == kctx) {
			spin_unlock_irqrestore(&kbdev->job_fault_event_lock,
					flags);
			return false;
		}
	}
	spin_unlock_irqrestore(&kbdev->job_fault_event_lock, flags);
	return true;
}

static int wait_for_job_fault(struct kbase_device *kbdev)
{
#if KERNEL_VERSION(4, 7, 0) <= LINUX_VERSION_CODE && \
	KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE
	int ret = wait_event_interruptible_timeout(kbdev->job_fault_wq,
			kbase_is_job_fault_event_pending(kbdev),
			msecs_to_jiffies(2000));
	if (ret == 0)
		return -EAGAIN;
	else if (ret > 0)
		return 0;
	else
		return ret;
#else
	return wait_event_interruptible(kbdev->job_fault_wq,
			kbase_is_job_fault_event_pending(kbdev));
#endif
}

/* wait until the fault happen and copy the event */
static int kbase_job_fault_event_wait(struct kbase_device *kbdev,
		struct base_job_fault_event *event)
{
	struct list_head            *event_list = &kbdev->job_fault_event_list;
	struct base_job_fault_event *event_in;
	unsigned long               flags;

	spin_lock_irqsave(&kbdev->job_fault_event_lock, flags);
	while (list_empty(event_list)) {
		int err;

		spin_unlock_irqrestore(&kbdev->job_fault_event_lock, flags);

		err = wait_for_job_fault(kbdev);
		if (err)
			return err;

		spin_lock_irqsave(&kbdev->job_fault_event_lock, flags);
	}

	event_in = list_entry(event_list->next,
			struct base_job_fault_event, head);
	event->event_code = event_in->event_code;
	event->katom = event_in->katom;

	spin_unlock_irqrestore(&kbdev->job_fault_event_lock, flags);

	return 0;

}

/* remove the event from the queue */
static struct base_job_fault_event *kbase_job_fault_event_dequeue(
		struct kbase_device *kbdev, struct list_head *event_list)
{
	struct base_job_fault_event *event;

	event = list_entry(event_list->next,
			struct base_job_fault_event, head);
	list_del(event_list->next);

	return event;

}

/* Remove all the following atoms after the failed atom in the same context
 * Call the postponed bottom half of job done.
 * Then, this context could be rescheduled.
 */
static void kbase_job_fault_resume_event_cleanup(struct kbase_context *kctx)
{
	struct list_head *event_list = &kctx->job_fault_resume_event_list;

	while (!list_empty(event_list)) {
		struct base_job_fault_event *event;

		event = kbase_job_fault_event_dequeue(kctx->kbdev,
				&kctx->job_fault_resume_event_list);
		kbase_jd_done_worker(&event->katom->work);
	}

}

static void kbase_job_fault_resume_worker(struct work_struct *data)
{
	struct base_job_fault_event *event = container_of(data,
			struct base_job_fault_event, job_fault_work);
	struct kbase_context *kctx;
	struct kbase_jd_atom *katom;

	katom = event->katom;
	kctx = katom->kctx;

	dev_info(kctx->kbdev->dev, "Job dumping wait\n");

	/* When it was waked up, it need to check if queue is empty or the
	 * failed atom belongs to different context. If yes, wake up. Both
	 * of them mean the failed job has been dumped. Please note, it
	 * should never happen that the job_fault_event_list has the two
	 * atoms belong to the same context.
	 */
	wait_event(kctx->kbdev->job_fault_resume_wq,
			 kbase_ctx_has_no_event_pending(kctx));

	atomic_set(&kctx->job_fault_count, 0);
	kbase_jd_done_worker(&katom->work);

	/* In case the following atoms were scheduled during failed job dump
	 * the job_done_worker was held. We need to rerun it after the dump
	 * was finished
	 */
	kbase_job_fault_resume_event_cleanup(kctx);

	dev_info(kctx->kbdev->dev, "Job dumping finish, resume scheduler\n");
}

static struct base_job_fault_event *kbase_job_fault_event_queue(
		struct list_head *event_list,
		struct kbase_jd_atom *atom,
		u32 completion_code)
{
	struct base_job_fault_event *event;

	event = &atom->fault_event;

	event->katom = atom;
	event->event_code = completion_code;

	list_add_tail(&event->head, event_list);

	return event;

}

static void kbase_job_fault_event_post(struct kbase_device *kbdev,
		struct kbase_jd_atom *katom, u32 completion_code)
{
	struct base_job_fault_event *event;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->job_fault_event_lock, flags);
	event = kbase_job_fault_event_queue(&kbdev->job_fault_event_list,
				katom, completion_code);
	spin_unlock_irqrestore(&kbdev->job_fault_event_lock, flags);

	wake_up_interruptible(&kbdev->job_fault_wq);

	INIT_WORK(&event->job_fault_work, kbase_job_fault_resume_worker);
	queue_work(kbdev->job_fault_resume_workq, &event->job_fault_work);

	dev_info(katom->kctx->kbdev->dev, "Job fault happen, start dump: %d_%d",
			katom->kctx->tgid, katom->kctx->id);

}

/*
 * This function will process the job fault
 * Get the register copy
 * Send the failed job dump event
 * Create a Wait queue to wait until the job dump finish
 */

bool kbase_debug_job_fault_process(struct kbase_jd_atom *katom,
		u32 completion_code)
{
	struct kbase_context *kctx = katom->kctx;

	/* Check if dumping is in the process
	 * only one atom of each context can be dumped at the same time
	 * If the atom belongs to different context, it can be dumped
	 */
	if (atomic_read(&kctx->job_fault_count) > 0) {
		kbase_job_fault_event_queue(
				&kctx->job_fault_resume_event_list,
				katom, completion_code);
		dev_info(kctx->kbdev->dev, "queue:%d\n",
				kbase_jd_atom_id(kctx, katom));
		return true;
	}

	if (kbase_ctx_flag(kctx, KCTX_DYING))
		return false;

	if (atomic_read(&kctx->kbdev->job_fault_debug) > 0) {

		if (completion_code != BASE_JD_EVENT_DONE) {

			if (kbase_job_fault_get_reg_snapshot(kctx) == false) {
				dev_warn(kctx->kbdev->dev, "get reg dump failed\n");
				return false;
			}

			kbase_job_fault_event_post(kctx->kbdev, katom,
					completion_code);
			atomic_inc(&kctx->job_fault_count);
			dev_info(kctx->kbdev->dev, "post:%d\n",
					kbase_jd_atom_id(kctx, katom));
			return true;

		}
	}
	return false;

}

static int debug_job_fault_show(struct seq_file *m, void *v)
{
	struct kbase_device *kbdev = m->private;
	struct base_job_fault_event *event = (struct base_job_fault_event *)v;
	struct kbase_context *kctx = event->katom->kctx;
	int i;

	dev_info(kbdev->dev, "debug job fault seq show:%d_%d, %d",
			kctx->tgid, kctx->id, event->reg_offset);

	if (kctx->reg_dump == NULL) {
		dev_warn(kbdev->dev, "reg dump is NULL");
		return -1;
	}

	if (kctx->reg_dump[event->reg_offset] ==
			REGISTER_DUMP_TERMINATION_FLAG) {
		/* Return the error here to stop the read. And the
		 * following next() will not be called. The stop can
		 * get the real event resource and release it
		 */
		return -1;
	}

	if (event->reg_offset == 0)
		seq_printf(m, "%d_%d\n", kctx->tgid, kctx->id);

	for (i = 0; i < 50; i++) {
		if (kctx->reg_dump[event->reg_offset] ==
				REGISTER_DUMP_TERMINATION_FLAG) {
			break;
		}
		seq_printf(m, "%08x: %08x\n",
				kctx->reg_dump[event->reg_offset],
				kctx->reg_dump[1+event->reg_offset]);
		event->reg_offset += 2;

	}


	return 0;
}
static void *debug_job_fault_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct kbase_device *kbdev = m->private;
	struct base_job_fault_event *event = (struct base_job_fault_event *)v;

	dev_info(kbdev->dev, "debug job fault seq next:%d, %d",
			event->reg_offset, (int)*pos);

	return event;
}

static void *debug_job_fault_start(struct seq_file *m, loff_t *pos)
{
	struct kbase_device *kbdev = m->private;
	struct base_job_fault_event *event;

	dev_info(kbdev->dev, "fault job seq start:%d", (int)*pos);

	/* The condition is trick here. It needs make sure the
	 * fault hasn't happened and the dumping hasn't been started,
	 * or the dumping has finished
	 */
	if (*pos == 0) {
		event = kmalloc(sizeof(*event), GFP_KERNEL);
		if (!event)
			return NULL;
		event->reg_offset = 0;
		if (kbase_job_fault_event_wait(kbdev, event)) {
			kfree(event);
			return NULL;
		}

		/* The cache flush workaround is called in bottom half of
		 * job done but we delayed it. Now we should clean cache
		 * earlier. Then the GPU memory dump should be correct.
		 */
		kbase_backend_cache_clean(kbdev, event->katom);
	} else
		return NULL;

	return event;
}

static void debug_job_fault_stop(struct seq_file *m, void *v)
{
	struct kbase_device *kbdev = m->private;

	/* here we wake up the kbase_jd_done_worker after stop, it needs
	 * get the memory dump before the register dump in debug daemon,
	 * otherwise, the memory dump may be incorrect.
	 */

	if (v != NULL) {
		kfree(v);
		dev_info(kbdev->dev, "debug job fault seq stop stage 1");

	} else {
		unsigned long flags;

		spin_lock_irqsave(&kbdev->job_fault_event_lock, flags);
		if (!list_empty(&kbdev->job_fault_event_list)) {
			kbase_job_fault_event_dequeue(kbdev,
				&kbdev->job_fault_event_list);
			wake_up(&kbdev->job_fault_resume_wq);
		}
		spin_unlock_irqrestore(&kbdev->job_fault_event_lock, flags);
		dev_info(kbdev->dev, "debug job fault seq stop stage 2");
	}

}

static const struct seq_operations ops = {
	.start = debug_job_fault_start,
	.next = debug_job_fault_next,
	.stop = debug_job_fault_stop,
	.show = debug_job_fault_show,
};

static int debug_job_fault_open(struct inode *in, struct file *file)
{
	struct kbase_device *kbdev = in->i_private;

	if (atomic_cmpxchg(&kbdev->job_fault_debug, 0, 1) == 1) {
		dev_warn(kbdev->dev, "debug job fault is busy, only a single client is allowed");
		return -EBUSY;
	}

	seq_open(file, &ops);

	((struct seq_file *)file->private_data)->private = kbdev;
	dev_info(kbdev->dev, "debug job fault seq open");


	return 0;

}

static int debug_job_fault_release(struct inode *in, struct file *file)
{
	struct kbase_device *kbdev = in->i_private;
	struct list_head *event_list = &kbdev->job_fault_event_list;
	unsigned long    flags;

	seq_release(in, file);

	spin_lock_irqsave(&kbdev->job_fault_event_lock, flags);

	/* Disable job fault dumping. This will let kbase run jobs as normal,
	 * without blocking waiting for a job_fault client to read failed jobs.
	 *
	 * After this a new client may open the file, and may re-enable job
	 * fault dumping, but the job_fault_event_lock we hold here will block
	 * that from interfering until after we've completed the cleanup.
	 */
	atomic_dec(&kbdev->job_fault_debug);

	/* Clean the unprocessed job fault. After that, all the suspended
	 * contexts could be rescheduled. Remove all the failed atoms that
	 * belong to different contexts Resume all the contexts that were
	 * suspend due to failed job.
	 */
	while (!list_empty(event_list)) {
		kbase_job_fault_event_dequeue(kbdev, event_list);
		spin_unlock_irqrestore(&kbdev->job_fault_event_lock, flags);
		wake_up(&kbdev->job_fault_resume_wq);
		spin_lock_irqsave(&kbdev->job_fault_event_lock, flags);
	}

	spin_unlock_irqrestore(&kbdev->job_fault_event_lock, flags);

	dev_info(kbdev->dev, "debug job fault seq close");

	return 0;
}

static const struct file_operations kbasep_debug_job_fault_fops = {
	.owner = THIS_MODULE,
	.open = debug_job_fault_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = debug_job_fault_release,
};

/*
 *  Initialize debugfs entry for job fault dump
 */
void kbase_debug_job_fault_debugfs_init(struct kbase_device *kbdev)
{
	debugfs_create_file("job_fault", 0400,
			kbdev->mali_debugfs_directory, kbdev,
			&kbasep_debug_job_fault_fops);
}


int kbase_debug_job_fault_dev_init(struct kbase_device *kbdev)
{

	INIT_LIST_HEAD(&kbdev->job_fault_event_list);

	init_waitqueue_head(&(kbdev->job_fault_wq));
	init_waitqueue_head(&(kbdev->job_fault_resume_wq));
	spin_lock_init(&kbdev->job_fault_event_lock);

	kbdev->job_fault_resume_workq = alloc_workqueue(
			"kbase_job_fault_resume_work_queue", WQ_MEM_RECLAIM, 1);
	if (!kbdev->job_fault_resume_workq)
		return -ENOMEM;

	atomic_set(&kbdev->job_fault_debug, 0);

	return 0;
}

/*
 * Release the relevant resource per device
 */
void kbase_debug_job_fault_dev_term(struct kbase_device *kbdev)
{
	destroy_workqueue(kbdev->job_fault_resume_workq);
}


/*
 *  Initialize the relevant data structure per context
 */
int kbase_debug_job_fault_context_init(struct kbase_context *kctx)
{

	/* We need allocate double size register range
	 * Because this memory will keep the register address and value
	 */
	kctx->reg_dump = vmalloc(0x4000 * 2);
	if (kctx->reg_dump != NULL) {
		if (kbase_debug_job_fault_reg_snapshot_init(kctx, 0x4000) ==
		    false) {
			vfree(kctx->reg_dump);
			kctx->reg_dump = NULL;
		}
		INIT_LIST_HEAD(&kctx->job_fault_resume_event_list);
		atomic_set(&kctx->job_fault_count, 0);
	}

	return 0;
}

/*
 *  release the relevant resource per context
 */
void kbase_debug_job_fault_context_term(struct kbase_context *kctx)
{
	vfree(kctx->reg_dump);
}

void kbase_debug_job_fault_kctx_unblock(struct kbase_context *kctx)
{
	WARN_ON(!kbase_ctx_flag(kctx, KCTX_DYING));

	/* Return early if the job fault part of the kbase_device is not
	 * initialized yet. An error can happen during the device probe after
	 * the privileged Kbase context was created for the HW counter dumping
	 * but before the job fault part is initialized.
	 */
	if (!kctx->kbdev->job_fault_resume_workq)
		return;

	kbase_ctx_remove_pending_event(kctx);
}

#else /* CONFIG_DEBUG_FS */

int kbase_debug_job_fault_dev_init(struct kbase_device *kbdev)
{
	return 0;
}

void kbase_debug_job_fault_dev_term(struct kbase_device *kbdev)
{
}

#endif /* CONFIG_DEBUG_FS */
