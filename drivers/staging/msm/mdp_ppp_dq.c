/* Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "mdp.h"

static boolean mdp_ppp_intr_flag = FALSE;
static boolean mdp_ppp_busy_flag = FALSE;

/* Queue to keep track of the completed jobs for cleaning */
static LIST_HEAD(mdp_ppp_djob_clnrq);
static DEFINE_SPINLOCK(mdp_ppp_djob_clnrq_lock);

/* Worker to cleanup Display Jobs */
static struct workqueue_struct *mdp_ppp_djob_clnr;

/* Display Queue (DQ) for MDP PPP Block */
static LIST_HEAD(mdp_ppp_dq);
static DEFINE_SPINLOCK(mdp_ppp_dq_lock);

/* Current Display Job for MDP PPP */
static struct mdp_ppp_djob *curr_djob;

/* Track ret code for the last opeartion */
static int mdp_ppp_ret_code;

inline int mdp_ppp_get_ret_code(void)
{
	return mdp_ppp_ret_code;
}

/* Push <Reg, Val> pair into DQ (if available) to later
 * program the MDP PPP Block */
inline void mdp_ppp_outdw(uint32_t addr, uint32_t data)
{
	if (curr_djob) {

		/* get the last node of the list. */
		struct mdp_ppp_roi_cmd_set *node =
			list_entry(curr_djob->roi_cmd_list.prev,
				struct mdp_ppp_roi_cmd_set, node);

		/* If a node is already full, create a new one and add it to
		 * the list (roi_cmd_list).
		 */
		if (node->ncmds == MDP_PPP_ROI_NODE_SIZE) {
			node = kmalloc(sizeof(struct mdp_ppp_roi_cmd_set),
				GFP_KERNEL);
			if (!node) {
				printk(KERN_ERR
					"MDP_PPP: not enough memory.\n");
				mdp_ppp_ret_code = -EINVAL;
				return;
			}

			/* no ROI commands initially */
			node->ncmds = 0;

			/* add one node to roi_cmd_list. */
			list_add_tail(&node->node, &curr_djob->roi_cmd_list);
		}

		/* register ROI commands */
		node->cmd[node->ncmds].reg = addr;
		node->cmd[node->ncmds].val = data;
		node->ncmds++;
	} else
		/* program MDP PPP block now */
		outpdw((addr), (data));
}

/* Initialize DQ */
inline void mdp_ppp_dq_init(void)
{
	mdp_ppp_djob_clnr = create_singlethread_workqueue("MDPDJobClnrThrd");
}

/* Release resources of a job (DJob). */
static void mdp_ppp_del_djob(struct mdp_ppp_djob *job)
{
	struct mdp_ppp_roi_cmd_set *node, *tmp;

	/* release mem */
	mdp_ppp_put_img(job->p_src_file, job->p_dst_file);

	/* release roi_cmd_list */
	list_for_each_entry_safe(node, tmp, &job->roi_cmd_list, node) {
		list_del(&node->node);
		kfree(node);
	}

	/* release job struct */
	kfree(job);
}

/* Worker thread to reclaim resources once a display job is done */
static void mdp_ppp_djob_cleaner(struct work_struct *work)
{
	struct mdp_ppp_djob *job;

	MDP_PPP_DEBUG_MSG("mdp ppp display job cleaner started \n");

	/* cleanup display job */
	job = container_of(work, struct mdp_ppp_djob, cleaner.work);
	if (likely(work && job))
		mdp_ppp_del_djob(job);
}

/* Create a new Display Job (DJob) */
inline struct mdp_ppp_djob *mdp_ppp_new_djob(void)
{
	struct mdp_ppp_djob *job;
	struct mdp_ppp_roi_cmd_set *node;

	/* create a new djob */
	job = kmalloc(sizeof(struct mdp_ppp_djob), GFP_KERNEL);
	if (!job)
		return NULL;

	/* add the first node to curr_djob->roi_cmd_list */
	node = kmalloc(sizeof(struct mdp_ppp_roi_cmd_set), GFP_KERNEL);
	if (!node) {
		kfree(job);
		return NULL;
	}

	/* make this current djob container to keep track of the curr djob not
	 * used in the async path i.e. no sync needed
	 *
	 * Should not contain any references from the past djob
	 */
	BUG_ON(curr_djob);
	curr_djob = job;
	INIT_LIST_HEAD(&curr_djob->roi_cmd_list);

	/* no ROI commands initially */
	node->ncmds = 0;
	INIT_LIST_HEAD(&node->node);
	list_add_tail(&node->node, &curr_djob->roi_cmd_list);

	/* register this djob with the djob cleaner
	 * initializes 'work' data struct
	 */
	INIT_DELAYED_WORK(&curr_djob->cleaner, mdp_ppp_djob_cleaner);
	INIT_LIST_HEAD(&curr_djob->entry);

	curr_djob->p_src_file = 0;
	curr_djob->p_dst_file = 0;

	return job;
}

/* Undo the effect of mdp_ppp_new_djob() */
inline void mdp_ppp_clear_curr_djob(void)
{
	if (likely(curr_djob)) {
		mdp_ppp_del_djob(curr_djob);
		curr_djob = NULL;
	}
}

/* Cleanup dirty djobs */
static void mdp_ppp_flush_dirty_djobs(void *cond)
{
	unsigned long flags;
	struct mdp_ppp_djob *job;

	/* Flush the jobs from the djob clnr queue */
	while (cond && test_bit(0, (unsigned long *)cond)) {

		/* Until we are done with the cleanup queue */
		spin_lock_irqsave(&mdp_ppp_djob_clnrq_lock, flags);
		if (list_empty(&mdp_ppp_djob_clnrq)) {
			spin_unlock_irqrestore(&mdp_ppp_djob_clnrq_lock, flags);
			break;
		}

		MDP_PPP_DEBUG_MSG("flushing djobs ... loop \n");

		/* Retrieve the job that needs to be cleaned */
		job = list_entry(mdp_ppp_djob_clnrq.next,
				struct mdp_ppp_djob, entry);
		list_del_init(&job->entry);
		spin_unlock_irqrestore(&mdp_ppp_djob_clnrq_lock, flags);

		/* Keep mem state coherent */
		msm_fb_ensure_mem_coherency_after_dma(job->info, &job->req, 1);

		/* Schedule jobs for cleanup
		 * A separate worker thread does this */
		queue_delayed_work(mdp_ppp_djob_clnr, &job->cleaner,
			mdp_timer_duration);
	}
}

/* If MDP PPP engine is busy, wait until it is available again */
void mdp_ppp_wait(void)
{
	unsigned long flags;
	int cond = 1;

	/* keep flushing dirty djobs as long as MDP PPP engine is busy */
	mdp_ppp_flush_dirty_djobs(&mdp_ppp_busy_flag);

	/* block if MDP PPP engine is still busy */
	spin_lock_irqsave(&mdp_ppp_dq_lock, flags);
	if (test_bit(0, (unsigned long *)&mdp_ppp_busy_flag)) {

		/* prepare for the wakeup event */
		test_and_set_bit(0, (unsigned long *)&mdp_ppp_waiting);
		INIT_COMPLETION(mdp_ppp_comp);
		spin_unlock_irqrestore(&mdp_ppp_dq_lock, flags);

		/* block uninterruptibly until available */
		MDP_PPP_DEBUG_MSG("waiting for mdp... \n");
		wait_for_completion_killable(&mdp_ppp_comp);

		/* if MDP PPP engine is still free,
		 * disable INT_MDP if enabled
		 */
		spin_lock_irqsave(&mdp_ppp_dq_lock, flags);
		if (!test_bit(0, (unsigned long *)&mdp_ppp_busy_flag) &&
		test_and_clear_bit(0, (unsigned long *)&mdp_ppp_intr_flag))
			mdp_disable_irq(MDP_PPP_TERM);
	}
	spin_unlock_irqrestore(&mdp_ppp_dq_lock, flags);

	/* flush remaining dirty djobs, if any */
	mdp_ppp_flush_dirty_djobs(&cond);
}

/* Program MDP PPP block to process this ROI */
static void mdp_ppp_process_roi(struct list_head *roi_cmd_list)
{

	/* program PPP engine with registered ROI commands */
	struct mdp_ppp_roi_cmd_set *node;
	list_for_each_entry(node, roi_cmd_list, node) {
		int i = 0;
		for (; i < node->ncmds; i++) {
			MDP_PPP_DEBUG_MSG("%d: reg: 0x%x val: 0x%x \n",
					i, node->cmd[i].reg, node->cmd[i].val);
			outpdw(node->cmd[i].reg, node->cmd[i].val);
		}
	}

	/* kickoff MDP PPP engine */
	MDP_PPP_DEBUG_MSG("kicking off mdp \n");
	outpdw(MDP_BASE + 0x30, 0x1000);
}

/* Submit this display job to MDP PPP engine */
static void mdp_ppp_dispatch_djob(struct mdp_ppp_djob *job)
{
	/* enable INT_MDP if disabled */
	if (!test_and_set_bit(0, (unsigned long *)&mdp_ppp_intr_flag))
		mdp_enable_irq(MDP_PPP_TERM);

	/* turn on PPP and CMD blocks */
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	mdp_pipe_ctrl(MDP_PPP_BLOCK, MDP_BLOCK_POWER_ON, FALSE);

	/* process this ROI */
	mdp_ppp_process_roi(&job->roi_cmd_list);
}

/* Enqueue this display job to be cleaned up later in "mdp_ppp_djob_done" */
static inline void mdp_ppp_enqueue_djob(struct mdp_ppp_djob *job)
{
	unsigned long flags;

	spin_lock_irqsave(&mdp_ppp_dq_lock, flags);
	list_add_tail(&job->entry, &mdp_ppp_dq);
	spin_unlock_irqrestore(&mdp_ppp_dq_lock, flags);
}

/* First enqueue display job for cleanup and dispatch immediately
 * if MDP PPP engine is free */
void mdp_ppp_process_curr_djob(void)
{
	/* enqueue djob */
	mdp_ppp_enqueue_djob(curr_djob);

	/* dispatch now if MDP PPP engine is free */
	if (!test_and_set_bit(0, (unsigned long *)&mdp_ppp_busy_flag))
		mdp_ppp_dispatch_djob(curr_djob);

	/* done with the current djob */
	curr_djob = NULL;
}

/* Called from mdp_isr - cleanup finished job and start with next
 * if available else set MDP PPP engine free */
void mdp_ppp_djob_done(void)
{
	struct mdp_ppp_djob *curr, *next;
	unsigned long flags;

	/* dequeue current */
	spin_lock_irqsave(&mdp_ppp_dq_lock, flags);
	curr = list_entry(mdp_ppp_dq.next, struct mdp_ppp_djob, entry);
	list_del_init(&curr->entry);
	spin_unlock_irqrestore(&mdp_ppp_dq_lock, flags);

	/* cleanup current - enqueue in the djob clnr queue */
	spin_lock_irqsave(&mdp_ppp_djob_clnrq_lock, flags);
	list_add_tail(&curr->entry, &mdp_ppp_djob_clnrq);
	spin_unlock_irqrestore(&mdp_ppp_djob_clnrq_lock, flags);

	/* grab next pending */
	spin_lock_irqsave(&mdp_ppp_dq_lock, flags);
	if (!list_empty(&mdp_ppp_dq)) {
		next = list_entry(mdp_ppp_dq.next, struct mdp_ppp_djob,
			entry);
		spin_unlock_irqrestore(&mdp_ppp_dq_lock, flags);

		/* process next in the queue */
		mdp_ppp_process_roi(&next->roi_cmd_list);
	} else {
		/* no pending display job */
		spin_unlock_irqrestore(&mdp_ppp_dq_lock, flags);

		/* turn off PPP and CMD blocks - "in_isr" is TRUE */
		mdp_pipe_ctrl(MDP_PPP_BLOCK, MDP_BLOCK_POWER_OFF, TRUE);
		mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, TRUE);

		/* notify if waiting */
		if (test_and_clear_bit(0, (unsigned long *)&mdp_ppp_waiting))
			complete(&mdp_ppp_comp);

		/* set free */
		test_and_clear_bit(0, (unsigned long *)&mdp_ppp_busy_flag);
	}
}
