/* Copyright (c) 2008-2010, Code Aurora Forum. All rights reserved.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/clk.h>
#include <mach/hardware.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>

#include "mdp.h"
#include "msm_fb.h"
#ifdef CONFIG_FB_MSM_MDP40
#include "mdp4.h"
#endif

static struct clk *mdp_clk;
static struct clk *mdp_pclk;

struct completion mdp_ppp_comp;
struct semaphore mdp_ppp_mutex;
struct semaphore mdp_pipe_ctrl_mutex;

unsigned long mdp_timer_duration = (HZ);   /* 1 sec */
/* unsigned long mdp_mdp_timer_duration=0; */

boolean mdp_ppp_waiting = FALSE;
uint32 mdp_tv_underflow_cnt;
uint32 mdp_lcdc_underflow_cnt;

boolean mdp_current_clk_on = FALSE;
boolean mdp_is_in_isr = FALSE;

/*
 * legacy mdp_in_processing is only for DMA2-MDDI
 * this applies to DMA2 block only
 */
uint32 mdp_in_processing = FALSE;

#ifdef CONFIG_FB_MSM_MDP40
uint32 mdp_intr_mask = MDP4_ANY_INTR_MASK;
#else
uint32 mdp_intr_mask = MDP_ANY_INTR_MASK;
#endif

MDP_BLOCK_TYPE mdp_debug[MDP_MAX_BLOCK];

int32 mdp_block_power_cnt[MDP_MAX_BLOCK];

spinlock_t mdp_spin_lock;
struct workqueue_struct *mdp_dma_wq;	/*mdp dma wq */
struct workqueue_struct *mdp_vsync_wq;	/*mdp vsync wq */

static struct workqueue_struct *mdp_pipe_ctrl_wq; /* mdp mdp pipe ctrl wq */
static struct delayed_work mdp_pipe_ctrl_worker;

#ifdef CONFIG_FB_MSM_MDP40
struct mdp_dma_data dma2_data;
struct mdp_dma_data dma_s_data;
struct mdp_dma_data dma_e_data;
#else
static struct mdp_dma_data dma2_data;
static struct mdp_dma_data dma_s_data;
static struct mdp_dma_data dma_e_data;
#endif
static struct mdp_dma_data dma3_data;

extern ktime_t mdp_dma2_last_update_time;

extern uint32 mdp_dma2_update_time_in_usec;
extern int mdp_lcd_rd_cnt_offset_slow;
extern int mdp_lcd_rd_cnt_offset_fast;
extern int mdp_usec_diff_threshold;

#ifdef CONFIG_FB_MSM_LCDC
extern int mdp_lcdc_pclk_clk_rate;
extern int mdp_lcdc_pad_pclk_clk_rate;
extern int first_pixel_start_x;
extern int first_pixel_start_y;
#endif

#ifdef MSM_FB_ENABLE_DBGFS
struct dentry *mdp_dir;
#endif

#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
static int mdp_suspend(struct platform_device *pdev, pm_message_t state);
#else
#define mdp_suspend NULL
#endif

struct timeval mdp_dma2_timeval;
struct timeval mdp_ppp_timeval;

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend early_suspend;
#endif

#ifndef CONFIG_FB_MSM_MDP22
DEFINE_MUTEX(mdp_lut_push_sem);
static int mdp_lut_i;
static int mdp_lut_hw_update(struct fb_cmap *cmap)
{
	int i;
	u16 *c[3];
	u16 r, g, b;

	c[0] = cmap->green;
	c[1] = cmap->blue;
	c[2] = cmap->red;

	for (i = 0; i < cmap->len; i++) {
		if (copy_from_user(&r, cmap->red++, sizeof(r)) ||
		    copy_from_user(&g, cmap->green++, sizeof(g)) ||
		    copy_from_user(&b, cmap->blue++, sizeof(b)))
			return -EFAULT;

#ifdef CONFIG_FB_MSM_MDP40
		MDP_OUTP(MDP_BASE + 0x94800 +
#else
		MDP_OUTP(MDP_BASE + 0x93800 +
#endif
			(0x400*mdp_lut_i) + cmap->start*4 + i*4,
				((g & 0xff) |
				 ((b & 0xff) << 8) |
				 ((r & 0xff) << 16)));
	}

	return 0;
}

static int mdp_lut_push;
static int mdp_lut_push_i;
static int mdp_lut_update_nonlcdc(struct fb_info *info, struct fb_cmap *cmap)
{
	int ret;

	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	ret = mdp_lut_hw_update(cmap);
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);

	if (ret)
		return ret;

	mutex_lock(&mdp_lut_push_sem);
	mdp_lut_push = 1;
	mdp_lut_push_i = mdp_lut_i;
	mutex_unlock(&mdp_lut_push_sem);

	mdp_lut_i = (mdp_lut_i + 1)%2;

	return 0;
}

static int mdp_lut_update_lcdc(struct fb_info *info, struct fb_cmap *cmap)
{
	int ret;

	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	ret = mdp_lut_hw_update(cmap);

	if (ret) {
		mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
		return ret;
	}

	MDP_OUTP(MDP_BASE + 0x90070, (mdp_lut_i << 10) | 0x17);
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
	mdp_lut_i = (mdp_lut_i + 1)%2;

	return 0;
}

#define MDP_HIST_MAX_BIN 32
static __u32 mdp_hist_r[MDP_HIST_MAX_BIN];
static __u32 mdp_hist_g[MDP_HIST_MAX_BIN];
static __u32 mdp_hist_b[MDP_HIST_MAX_BIN];

#ifdef CONFIG_FB_MSM_MDP40
struct mdp_histogram mdp_hist;
struct completion mdp_hist_comp;
#else
static struct mdp_histogram mdp_hist;
static struct completion mdp_hist_comp;
#endif

static int mdp_do_histogram(struct fb_info *info, struct mdp_histogram *hist)
{
	int ret = 0;

	if (!hist->frame_cnt || (hist->bin_cnt == 0) ||
				 (hist->bin_cnt > MDP_HIST_MAX_BIN))
		return -EINVAL;

	INIT_COMPLETION(mdp_hist_comp);

	mdp_hist.bin_cnt = hist->bin_cnt;
	mdp_hist.r = (hist->r) ? mdp_hist_r : 0;
	mdp_hist.g = (hist->g) ? mdp_hist_g : 0;
	mdp_hist.b = (hist->b) ? mdp_hist_b : 0;

#ifdef CONFIG_FB_MSM_MDP40
	MDP_OUTP(MDP_BASE + 0x95004, hist->frame_cnt);
	MDP_OUTP(MDP_BASE + 0x95000, 1);
#else
	MDP_OUTP(MDP_BASE + 0x94004, hist->frame_cnt);
	MDP_OUTP(MDP_BASE + 0x94000, 1);
#endif
	wait_for_completion_killable(&mdp_hist_comp);

	if (hist->r) {
		ret = copy_to_user(hist->r, mdp_hist.r, hist->bin_cnt*4);
		if (ret)
			goto hist_err;
	}
	if (hist->g) {
		ret = copy_to_user(hist->g, mdp_hist.g, hist->bin_cnt*4);
		if (ret)
			goto hist_err;
	}
	if (hist->b) {
		ret = copy_to_user(hist->b, mdp_hist.b, hist->bin_cnt*4);
		if (ret)
			goto hist_err;
	}
	return 0;

hist_err:
	printk(KERN_ERR "%s: invalid hist buffer\n", __func__);
	return ret;
}
#endif

/* Returns < 0 on error, 0 on timeout, or > 0 on successful wait */

int mdp_ppp_pipe_wait(void)
{
	int ret = 1;

	/* wait 5 seconds for the operation to complete before declaring
	the MDP hung */

	if (mdp_ppp_waiting == TRUE) {
		ret = wait_for_completion_interruptible_timeout(&mdp_ppp_comp,
								5 * HZ);

		if (!ret)
			printk(KERN_ERR "%s: Timed out waiting for the MDP.\n",
					__func__);
	}

	return ret;
}

static DEFINE_SPINLOCK(mdp_lock);
static int mdp_irq_mask;
static int mdp_irq_enabled;

void mdp_enable_irq(uint32 term)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&mdp_lock, irq_flags);
	if (mdp_irq_mask & term) {
		printk(KERN_ERR "MDP IRQ term-0x%x is already set\n", term);
	} else {
		mdp_irq_mask |= term;
		if (mdp_irq_mask && !mdp_irq_enabled) {
			mdp_irq_enabled = 1;
			enable_irq(INT_MDP);
		}
	}
	spin_unlock_irqrestore(&mdp_lock, irq_flags);
}

void mdp_disable_irq(uint32 term)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&mdp_lock, irq_flags);
	if (!(mdp_irq_mask & term)) {
		printk(KERN_ERR "MDP IRQ term-0x%x is not set\n", term);
	} else {
		mdp_irq_mask &= ~term;
		if (!mdp_irq_mask && mdp_irq_enabled) {
			mdp_irq_enabled = 0;
			disable_irq(INT_MDP);
		}
	}
	spin_unlock_irqrestore(&mdp_lock, irq_flags);
}

void mdp_disable_irq_nolock(uint32 term)
{

	if (!(mdp_irq_mask & term)) {
		printk(KERN_ERR "MDP IRQ term-0x%x is not set\n", term);
	} else {
		mdp_irq_mask &= ~term;
		if (!mdp_irq_mask && mdp_irq_enabled) {
			mdp_irq_enabled = 0;
			disable_irq(INT_MDP);
		}
	}
}

void mdp_pipe_kickoff(uint32 term, struct msm_fb_data_type *mfd)
{

	dmb();	/* memory barrier */

	/* kick off PPP engine */
	if (term == MDP_PPP_TERM) {
		if (mdp_debug[MDP_PPP_BLOCK])
			jiffies_to_timeval(jiffies, &mdp_ppp_timeval);

		/* let's turn on PPP block */
		mdp_pipe_ctrl(MDP_PPP_BLOCK, MDP_BLOCK_POWER_ON, FALSE);

		mdp_enable_irq(term);
		INIT_COMPLETION(mdp_ppp_comp);
		mdp_ppp_waiting = TRUE;
		outpdw(MDP_BASE + 0x30, 0x1000);
		wait_for_completion_killable(&mdp_ppp_comp);
		mdp_disable_irq(term);

		if (mdp_debug[MDP_PPP_BLOCK]) {
			struct timeval now;

			jiffies_to_timeval(jiffies, &now);
			mdp_ppp_timeval.tv_usec =
			    now.tv_usec - mdp_ppp_timeval.tv_usec;
			MSM_FB_INFO("MDP-PPP: %d\n",
				    (int)mdp_ppp_timeval.tv_usec);
		}
	} else if (term == MDP_DMA2_TERM) {
		if (mdp_debug[MDP_DMA2_BLOCK]) {
			MSM_FB_INFO("MDP-DMA2: %d\n",
				    (int)mdp_dma2_timeval.tv_usec);
			jiffies_to_timeval(jiffies, &mdp_dma2_timeval);
		}
		/* DMA update timestamp */
		mdp_dma2_last_update_time = ktime_get_real();
		/* let's turn on DMA2 block */
#if 0
		mdp_pipe_ctrl(MDP_DMA2_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
#endif
#ifdef CONFIG_FB_MSM_MDP22
		outpdw(MDP_CMD_DEBUG_ACCESS_BASE + 0x0044, 0x0);/* start DMA */
#else
		if (mdp_lut_push) {
			mutex_lock(&mdp_lut_push_sem);
			mdp_lut_push = 0;
			MDP_OUTP(MDP_BASE + 0x90070,
					(mdp_lut_push_i << 10) | 0x17);
			mutex_unlock(&mdp_lut_push_sem);
		}
#ifdef CONFIG_FB_MSM_MDP40
		outpdw(MDP_BASE + 0x000c, 0x0);	/* start DMA */
#else
		outpdw(MDP_BASE + 0x0044, 0x0);	/* start DMA */
#endif
#endif
#ifdef CONFIG_FB_MSM_MDP40
	} else if (term == MDP_DMA_S_TERM) {
		mdp_pipe_ctrl(MDP_DMA_S_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		outpdw(MDP_BASE + 0x0010, 0x0);	/* start DMA */
	} else if (term == MDP_DMA_E_TERM) {
		mdp_pipe_ctrl(MDP_DMA_E_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		outpdw(MDP_BASE + 0x0014, 0x0);	/* start DMA */
	} else if (term == MDP_OVERLAY0_TERM) {
		mdp_pipe_ctrl(MDP_OVERLAY0_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		outpdw(MDP_BASE + 0x0004, 0);
	} else if (term == MDP_OVERLAY1_TERM) {
		mdp_pipe_ctrl(MDP_OVERLAY1_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		outpdw(MDP_BASE + 0x0008, 0);
	}
#else
	} else if (term == MDP_DMA_S_TERM) {
		mdp_pipe_ctrl(MDP_DMA_S_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		outpdw(MDP_BASE + 0x0048, 0x0);	/* start DMA */
	}
#endif
}

static void mdp_pipe_ctrl_workqueue_handler(struct work_struct *work)
{
	mdp_pipe_ctrl(MDP_MASTER_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
}

void mdp_pipe_ctrl(MDP_BLOCK_TYPE block, MDP_BLOCK_POWER_STATE state,
		   boolean isr)
{
	boolean mdp_all_blocks_off = TRUE;
	int i;
	unsigned long flag;

	spin_lock_irqsave(&mdp_spin_lock, flag);
	if (MDP_BLOCK_POWER_ON == state) {
		mdp_block_power_cnt[block]++;

		if (MDP_DMA2_BLOCK == block)
			mdp_in_processing = TRUE;
	} else {
		mdp_block_power_cnt[block]--;

		if (mdp_block_power_cnt[block] < 0) {
			/*
			* Master has to serve a request to power off MDP always
			* It also has a timer to power off.  So, in case of
			* timer expires first and DMA2 finishes later,
			* master has to power off two times
			* There shouldn't be multiple power-off request for
			* other blocks
			*/
			if (block != MDP_MASTER_BLOCK) {
				MSM_FB_INFO("mdp_block_power_cnt[block=%d] \
				multiple power-off request\n", block);
			}
			mdp_block_power_cnt[block] = 0;
		}

		if (MDP_DMA2_BLOCK == block)
			mdp_in_processing = FALSE;
	}
	spin_unlock_irqrestore(&mdp_spin_lock, flag);

	/*
	 * If it's in isr, we send our request to workqueue.
	 * Otherwise, processing happens in the current context
	 */
	if (isr) {
		/* checking all blocks power state */
		for (i = 0; i < MDP_MAX_BLOCK; i++) {
			if (mdp_block_power_cnt[i] > 0)
				mdp_all_blocks_off = FALSE;
		}

		if ((mdp_all_blocks_off) && (mdp_current_clk_on)) {
			/* send workqueue to turn off mdp power */
			queue_delayed_work(mdp_pipe_ctrl_wq,
					   &mdp_pipe_ctrl_worker,
					   mdp_timer_duration);
		}
	} else {
		down(&mdp_pipe_ctrl_mutex);
		/* checking all blocks power state */
		for (i = 0; i < MDP_MAX_BLOCK; i++) {
			if (mdp_block_power_cnt[i] > 0)
				mdp_all_blocks_off = FALSE;
		}

		/*
		 * find out whether a delayable work item is currently
		 * pending
		 */

		if (delayed_work_pending(&mdp_pipe_ctrl_worker)) {
			/*
			 * try to cancel the current work if it fails to
			 * stop (which means del_timer can't delete it
			 * from the list, it's about to expire and run),
			 * we have to let it run. queue_delayed_work won't
			 * accept the next job which is same as
			 * queue_delayed_work(mdp_timer_duration = 0)
			 */
			cancel_delayed_work(&mdp_pipe_ctrl_worker);
		}

		if ((mdp_all_blocks_off) && (mdp_current_clk_on)) {
			if (block == MDP_MASTER_BLOCK) {
				mdp_current_clk_on = FALSE;
				/* turn off MDP clks */
				if (mdp_clk != NULL) {
					clk_disable(mdp_clk);
					MSM_FB_DEBUG("MDP CLK OFF\n");
				}
				if (mdp_pclk != NULL) {
					clk_disable(mdp_pclk);
					MSM_FB_DEBUG("MDP PCLK OFF\n");
				}
			} else {
				/* send workqueue to turn off mdp power */
				queue_delayed_work(mdp_pipe_ctrl_wq,
						   &mdp_pipe_ctrl_worker,
						   mdp_timer_duration);
			}
		} else if ((!mdp_all_blocks_off) && (!mdp_current_clk_on)) {
			mdp_current_clk_on = TRUE;
			/* turn on MDP clks */
			if (mdp_clk != NULL) {
				clk_enable(mdp_clk);
				MSM_FB_DEBUG("MDP CLK ON\n");
			}
			if (mdp_pclk != NULL) {
				clk_enable(mdp_pclk);
				MSM_FB_DEBUG("MDP PCLK ON\n");
			}
		}
		up(&mdp_pipe_ctrl_mutex);
	}
}

#ifndef CONFIG_FB_MSM_MDP40
irqreturn_t mdp_isr(int irq, void *ptr)
{
	uint32 mdp_interrupt = 0;
	struct mdp_dma_data *dma;

	mdp_is_in_isr = TRUE;
	do {
		mdp_interrupt = inp32(MDP_INTR_STATUS);
		outp32(MDP_INTR_CLEAR, mdp_interrupt);

		mdp_interrupt &= mdp_intr_mask;

		if (mdp_interrupt & TV_ENC_UNDERRUN) {
			mdp_interrupt &= ~(TV_ENC_UNDERRUN);
			mdp_tv_underflow_cnt++;
		}

		if (!mdp_interrupt)
			break;

		/* DMA3 TV-Out Start */
		if (mdp_interrupt & TV_OUT_DMA3_START) {
			/* let's disable TV out interrupt */
			mdp_intr_mask &= ~TV_OUT_DMA3_START;
			outp32(MDP_INTR_ENABLE, mdp_intr_mask);

			dma = &dma3_data;
			if (dma->waiting) {
				dma->waiting = FALSE;
				complete(&dma->comp);
			}
		}
#ifndef CONFIG_FB_MSM_MDP22
		if (mdp_interrupt & MDP_HIST_DONE) {
			outp32(MDP_BASE + 0x94018, 0x3);
			outp32(MDP_INTR_CLEAR, MDP_HIST_DONE);
			if (mdp_hist.r)
				memcpy(mdp_hist.r, MDP_BASE + 0x94100,
						mdp_hist.bin_cnt*4);
			if (mdp_hist.g)
				memcpy(mdp_hist.g, MDP_BASE + 0x94200,
						mdp_hist.bin_cnt*4);
			if (mdp_hist.b)
				memcpy(mdp_hist.b, MDP_BASE + 0x94300,
						mdp_hist.bin_cnt*4);
			complete(&mdp_hist_comp);
		}

		/* LCDC UnderFlow */
		if (mdp_interrupt & LCDC_UNDERFLOW) {
			mdp_lcdc_underflow_cnt++;
		}
		/* LCDC Frame Start */
		if (mdp_interrupt & LCDC_FRAME_START) {
			/* let's disable LCDC interrupt */
			mdp_intr_mask &= ~LCDC_FRAME_START;
			outp32(MDP_INTR_ENABLE, mdp_intr_mask);

			dma = &dma2_data;
			if (dma->waiting) {
				dma->waiting = FALSE;
				complete(&dma->comp);
			}
		}

		/* DMA2 LCD-Out Complete */
		if (mdp_interrupt & MDP_DMA_S_DONE) {
			dma = &dma_s_data;
			dma->busy = FALSE;
			mdp_pipe_ctrl(MDP_DMA_S_BLOCK, MDP_BLOCK_POWER_OFF,
				      TRUE);
			complete(&dma->comp);
		}
#endif

		/* DMA2 LCD-Out Complete */
		if (mdp_interrupt & MDP_DMA_P_DONE) {
			struct timeval now;
			ktime_t now_k;

			now_k = ktime_get_real();
			mdp_dma2_last_update_time.tv.sec =
			    now_k.tv.sec - mdp_dma2_last_update_time.tv.sec;
			mdp_dma2_last_update_time.tv.nsec =
			    now_k.tv.nsec - mdp_dma2_last_update_time.tv.nsec;

			if (mdp_debug[MDP_DMA2_BLOCK]) {
				jiffies_to_timeval(jiffies, &now);
				mdp_dma2_timeval.tv_usec =
				    now.tv_usec - mdp_dma2_timeval.tv_usec;
			}

			dma = &dma2_data;
			dma->busy = FALSE;
			mdp_pipe_ctrl(MDP_DMA2_BLOCK, MDP_BLOCK_POWER_OFF,
				      TRUE);
			complete(&dma->comp);
		}
		/* PPP Complete */
		if (mdp_interrupt & MDP_PPP_DONE) {
#ifdef CONFIG_MDP_PPP_ASYNC_OP
			mdp_ppp_djob_done();
#else
			mdp_pipe_ctrl(MDP_PPP_BLOCK,
				MDP_BLOCK_POWER_OFF, TRUE);
			if (mdp_ppp_waiting) {
				mdp_ppp_waiting = FALSE;
				complete(&mdp_ppp_comp);
			}
#endif
		}
	} while (1);

	mdp_is_in_isr = FALSE;

	return IRQ_HANDLED;
}
#endif

static void mdp_drv_init(void)
{
	int i;

	for (i = 0; i < MDP_MAX_BLOCK; i++) {
		mdp_debug[i] = 0;
	}

	/* initialize spin lock and workqueue */
	spin_lock_init(&mdp_spin_lock);
	mdp_dma_wq = create_singlethread_workqueue("mdp_dma_wq");
	mdp_vsync_wq = create_singlethread_workqueue("mdp_vsync_wq");
	mdp_pipe_ctrl_wq = create_singlethread_workqueue("mdp_pipe_ctrl_wq");
	INIT_DELAYED_WORK(&mdp_pipe_ctrl_worker,
			  mdp_pipe_ctrl_workqueue_handler);
#ifdef CONFIG_MDP_PPP_ASYNC_OP
	mdp_ppp_dq_init();
#endif

	/* initialize semaphore */
	init_completion(&mdp_ppp_comp);
	init_MUTEX(&mdp_ppp_mutex);
	init_MUTEX(&mdp_pipe_ctrl_mutex);

	dma2_data.busy = FALSE;
	dma2_data.waiting = FALSE;
	init_completion(&dma2_data.comp);
	init_MUTEX(&dma2_data.mutex);
	mutex_init(&dma2_data.ov_mutex);

	dma3_data.busy = FALSE;
	dma3_data.waiting = FALSE;
	init_completion(&dma3_data.comp);
	init_MUTEX(&dma3_data.mutex);

	dma_s_data.busy = FALSE;
	dma_s_data.waiting = FALSE;
	init_completion(&dma_s_data.comp);
	init_MUTEX(&dma_s_data.mutex);

	dma_e_data.busy = FALSE;
	dma_e_data.waiting = FALSE;
	init_completion(&dma_e_data.comp);

#ifndef CONFIG_FB_MSM_MDP22
	init_completion(&mdp_hist_comp);
#endif

	/* initializing mdp power block counter to 0 */
	for (i = 0; i < MDP_MAX_BLOCK; i++) {
		mdp_block_power_cnt[i] = 0;
	}

#ifdef MSM_FB_ENABLE_DBGFS
	{
		struct dentry *root;
		char sub_name[] = "mdp";

		root = msm_fb_get_debugfs_root();
		if (root != NULL) {
			mdp_dir = debugfs_create_dir(sub_name, root);

			if (mdp_dir) {
				msm_fb_debugfs_file_create(mdp_dir,
					"dma2_update_time_in_usec",
					(u32 *) &mdp_dma2_update_time_in_usec);
				msm_fb_debugfs_file_create(mdp_dir,
					"vs_rdcnt_slow",
					(u32 *) &mdp_lcd_rd_cnt_offset_slow);
				msm_fb_debugfs_file_create(mdp_dir,
					"vs_rdcnt_fast",
					(u32 *) &mdp_lcd_rd_cnt_offset_fast);
				msm_fb_debugfs_file_create(mdp_dir,
					"mdp_usec_diff_threshold",
					(u32 *) &mdp_usec_diff_threshold);
				msm_fb_debugfs_file_create(mdp_dir,
					"mdp_current_clk_on",
					(u32 *) &mdp_current_clk_on);
#ifdef CONFIG_FB_MSM_LCDC
				msm_fb_debugfs_file_create(mdp_dir,
					"lcdc_start_x",
					(u32 *) &first_pixel_start_x);
				msm_fb_debugfs_file_create(mdp_dir,
					"lcdc_start_y",
					(u32 *) &first_pixel_start_y);
				msm_fb_debugfs_file_create(mdp_dir,
					"mdp_lcdc_pclk_clk_rate",
					(u32 *) &mdp_lcdc_pclk_clk_rate);
				msm_fb_debugfs_file_create(mdp_dir,
					"mdp_lcdc_pad_pclk_clk_rate",
					(u32 *) &mdp_lcdc_pad_pclk_clk_rate);
#endif
			}
		}
	}
#endif
}

static int mdp_probe(struct platform_device *pdev);
static int mdp_remove(struct platform_device *pdev);

static struct platform_driver mdp_driver = {
	.probe = mdp_probe,
	.remove = mdp_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend = mdp_suspend,
	.resume = NULL,
#endif
	.shutdown = NULL,
	.driver = {
		/*
		 * Driver name must match the device name added in
		 * platform.c.
		 */
		.name = "mdp",
	},
};

static int mdp_off(struct platform_device *pdev)
{
	int ret = 0;

#ifdef MDP_HW_VSYNC
	struct msm_fb_data_type *mfd = platform_get_drvdata(pdev);
#endif

	ret = panel_next_off(pdev);

#ifdef MDP_HW_VSYNC
	mdp_hw_vsync_clk_disable(mfd);
#endif

	return ret;
}

static int mdp_on(struct platform_device *pdev)
{
#ifdef MDP_HW_VSYNC
	struct msm_fb_data_type *mfd = platform_get_drvdata(pdev);
#endif

	int ret = 0;

#ifdef MDP_HW_VSYNC
	mdp_hw_vsync_clk_enable(mfd);
#endif

	ret = panel_next_on(pdev);

	return ret;
}

static int mdp_irq_clk_setup(void)
{
	int ret;

#ifdef CONFIG_FB_MSM_MDP40
	ret = request_irq(INT_MDP, mdp4_isr, IRQF_DISABLED, "MDP", 0);
#else
	ret = request_irq(INT_MDP, mdp_isr, IRQF_DISABLED, "MDP", 0);
#endif
	if (ret) {
		printk(KERN_ERR "mdp request_irq() failed!\n");
		return ret;
	}
	disable_irq(INT_MDP);

	mdp_clk = clk_get(NULL, "mdp_clk");

	if (IS_ERR(mdp_clk)) {
		ret = PTR_ERR(mdp_clk);
		printk(KERN_ERR "can't get mdp_clk error:%d!\n", ret);
		free_irq(INT_MDP, 0);
		return ret;
	}

	mdp_pclk = clk_get(NULL, "mdp_pclk");
	if (IS_ERR(mdp_pclk))
		mdp_pclk = NULL;


#ifdef CONFIG_FB_MSM_MDP40
	/*
	 * mdp_clk should greater than mdp_pclk always
	 */
	clk_set_rate(mdp_clk, 122880000); /* 122.88 Mhz */
	printk(KERN_INFO "mdp_clk: mdp_clk=%d mdp_pclk=%d\n",
		(int)clk_get_rate(mdp_clk), (int)clk_get_rate(mdp_pclk));
#endif

	return 0;
}

static struct platform_device *pdev_list[MSM_FB_MAX_DEV_LIST];
static int pdev_list_cnt;
static int mdp_resource_initialized;
static struct msm_panel_common_pdata *mdp_pdata;

static int mdp_probe(struct platform_device *pdev)
{
	struct platform_device *msm_fb_dev = NULL;
	struct msm_fb_data_type *mfd;
	struct msm_fb_panel_data *pdata = NULL;
	int rc;
	resource_size_t  size ;
#ifdef CONFIG_FB_MSM_MDP40
	int intf, if_no;
#else
	unsigned long flag;
#endif

	if ((pdev->id == 0) && (pdev->num_resources > 0)) {
		mdp_pdata = pdev->dev.platform_data;

		size =  resource_size(&pdev->resource[0]);
		msm_mdp_base = ioremap(pdev->resource[0].start, size);

		MSM_FB_INFO("MDP HW Base phy_Address = 0x%x virt = 0x%x\n",
			(int)pdev->resource[0].start, (int)msm_mdp_base);

		if (unlikely(!msm_mdp_base))
			return -ENOMEM;

		printk("irq clk setup\n");
		rc = mdp_irq_clk_setup();
		printk("irq clk setup done\n");
		if (rc)
			return rc;

		/* initializing mdp hw */
#ifdef CONFIG_FB_MSM_MDP40
		mdp4_hw_init();
#else
		mdp_hw_init();
#endif

		mdp_resource_initialized = 1;
		return 0;
	}

	if (!mdp_resource_initialized)
		return -EPERM;

	mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	if (pdev_list_cnt >= MSM_FB_MAX_DEV_LIST)
		return -ENOMEM;

	msm_fb_dev = platform_device_alloc("msm_fb", pdev->id);
	if (!msm_fb_dev)
		return -ENOMEM;

	/* link to the latest pdev */
	mfd->pdev = msm_fb_dev;

	/* add panel data */
	if (platform_device_add_data
	    (msm_fb_dev, pdev->dev.platform_data,
	     sizeof(struct msm_fb_panel_data))) {
		printk(KERN_ERR "mdp_probe: platform_device_add_data failed!\n");
		rc = -ENOMEM;
		goto mdp_probe_err;
	}
	/* data chain */
	pdata = msm_fb_dev->dev.platform_data;
	pdata->on = mdp_on;
	pdata->off = mdp_off;
	pdata->next = pdev;

	switch (mfd->panel.type) {
	case EXT_MDDI_PANEL:
	case MDDI_PANEL:
	case EBI2_PANEL:
		INIT_WORK(&mfd->dma_update_worker,
			  mdp_lcd_update_workqueue_handler);
		INIT_WORK(&mfd->vsync_resync_worker,
			  mdp_vsync_resync_workqueue_handler);
		mfd->hw_refresh = FALSE;

		if (mfd->panel.type == EXT_MDDI_PANEL) {
			/* 15 fps -> 66 msec */
			mfd->refresh_timer_duration = (66 * HZ / 1000);
		} else {
			/* 24 fps -> 42 msec */
			mfd->refresh_timer_duration = (42 * HZ / 1000);
		}

#ifdef CONFIG_FB_MSM_MDP22
		mfd->dma_fnc = mdp_dma2_update;
		mfd->dma = &dma2_data;
#else
		if (mfd->panel_info.pdest == DISPLAY_1) {
#ifdef CONFIG_FB_MSM_OVERLAY
			mfd->dma_fnc = mdp4_mddi_overlay;
#else
			mfd->dma_fnc = mdp_dma2_update;
#endif
			mfd->dma = &dma2_data;
			mfd->lut_update = mdp_lut_update_nonlcdc;
			mfd->do_histogram = mdp_do_histogram;
		} else {
			mfd->dma_fnc = mdp_dma_s_update;
			mfd->dma = &dma_s_data;
		}
#endif
		if (mdp_pdata)
			mfd->vsync_gpio = mdp_pdata->gpio;
		else
			mfd->vsync_gpio = -1;

#ifdef CONFIG_FB_MSM_MDP40
		if (mfd->panel.type == EBI2_PANEL)
			intf = EBI2_INTF;
		else
			intf = MDDI_INTF;

		if (mfd->panel_info.pdest == DISPLAY_1)
			if_no = PRIMARY_INTF_SEL;
		else
			if_no = SECONDARY_INTF_SEL;

		mdp4_display_intf_sel(if_no, intf);
#endif
		mdp_config_vsync(mfd);
		break;

	case HDMI_PANEL:
	case LCDC_PANEL:
		pdata->on = mdp_lcdc_on;
		pdata->off = mdp_lcdc_off;
		mfd->hw_refresh = TRUE;
		mfd->cursor_update = mdp_hw_cursor_update;
#ifndef CONFIG_FB_MSM_MDP22
		mfd->lut_update = mdp_lut_update_lcdc;
		mfd->do_histogram = mdp_do_histogram;
#endif
#ifdef CONFIG_FB_MSM_OVERLAY
		mfd->dma_fnc = mdp4_lcdc_overlay;
#else
		mfd->dma_fnc = mdp_lcdc_update;
#endif

#ifdef CONFIG_FB_MSM_MDP40
		if (mfd->panel.type == HDMI_PANEL) {
			mfd->dma = &dma_e_data;
			mdp4_display_intf_sel(EXTERNAL_INTF_SEL, LCDC_RGB_INTF);
		} else {
			mfd->dma = &dma2_data;
			mdp4_display_intf_sel(PRIMARY_INTF_SEL, LCDC_RGB_INTF);
		}
#else
		mfd->dma = &dma2_data;
		spin_lock_irqsave(&mdp_spin_lock, flag);
		mdp_intr_mask &= ~MDP_DMA_P_DONE;
		outp32(MDP_INTR_ENABLE, mdp_intr_mask);
		spin_unlock_irqrestore(&mdp_spin_lock, flag);
#endif
		break;

	case TV_PANEL:
		pdata->on = mdp_dma3_on;
		pdata->off = mdp_dma3_off;
		mfd->hw_refresh = TRUE;
		mfd->dma_fnc = mdp_dma3_update;
		mfd->dma = &dma3_data;
		break;

	default:
		printk(KERN_ERR "mdp_probe: unknown device type!\n");
		rc = -ENODEV;
		goto mdp_probe_err;
	}

	/* set driver data */
	platform_set_drvdata(msm_fb_dev, mfd);

	rc = platform_device_add(msm_fb_dev);
	if (rc) {
		goto mdp_probe_err;
	}

	pdev_list[pdev_list_cnt++] = pdev;
	return 0;

      mdp_probe_err:
	platform_device_put(msm_fb_dev);
	return rc;
}

static void mdp_suspend_sub(void)
{
	/* cancel pipe ctrl worker */
	cancel_delayed_work(&mdp_pipe_ctrl_worker);

	/* for workder can't be cancelled... */
	flush_workqueue(mdp_pipe_ctrl_wq);

	/* let's wait for PPP completion */
	while (mdp_block_power_cnt[MDP_PPP_BLOCK] > 0) ;

	/* try to power down */
	mdp_pipe_ctrl(MDP_MASTER_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
}

#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
static int mdp_suspend(struct platform_device *pdev, pm_message_t state)
{
	mdp_suspend_sub();
	return 0;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mdp_early_suspend(struct early_suspend *h)
{
	mdp_suspend_sub();
}
#endif

static int mdp_remove(struct platform_device *pdev)
{
	iounmap(msm_mdp_base);
	return 0;
}

static int mdp_register_driver(void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	early_suspend.suspend = mdp_early_suspend;
	register_early_suspend(&early_suspend);
#endif

	return platform_driver_register(&mdp_driver);
}

static int __init mdp_driver_init(void)
{
	int ret;

	mdp_drv_init();

	ret = mdp_register_driver();
	if (ret) {
		printk(KERN_ERR "mdp_register_driver() failed!\n");
		return ret;
	}

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_FB_MSM_MDP40)
	mdp4_debugfs_init();
#endif

	return 0;

}

module_init(mdp_driver_init);
