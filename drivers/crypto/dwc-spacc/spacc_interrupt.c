// SPDX-License-Identifier: GPL-2.0

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include "spacc_core.h"

/* Read the IRQ status register and process as needed */


void spacc_disable_int (struct spacc_device *spacc);

static inline uint32_t _spacc_get_stat_cnt(struct spacc_device *spacc)
{
	u32 fifo;

	if (spacc->config.is_qos)
		fifo = SPACC_FIFO_STAT_STAT_CNT_GET_QOS(readl(spacc->regmap +
					SPACC_REG_FIFO_STAT));
	else
		fifo = SPACC_FIFO_STAT_STAT_CNT_GET(readl(spacc->regmap +
					SPACC_REG_FIFO_STAT));

	return fifo;
}

static int spacc_pop_packets_ex(struct spacc_device *spacc, int *num_popped,
				unsigned long *lock_flag)
{
	int jobs;
	int ret = -EINPROGRESS;
	struct spacc_job *job = NULL;
	u32 cmdstat, swid, spacc_errcode = SPACC_OK;

	*num_popped = 0;

	while ((jobs = _spacc_get_stat_cnt(spacc))) {
		while (jobs-- > 0) {
			/* write the pop register to get the next job */
			writel(1, spacc->regmap + SPACC_REG_STAT_POP);
			cmdstat = readl(spacc->regmap + SPACC_REG_STATUS);

			swid = SPACC_STATUS_SW_ID_GET(cmdstat);

			if (spacc->job_lookup[swid] == SPACC_JOB_IDX_UNUSED) {
				ret = -EIO;
				goto ERR;
			}

			/* find the associated job with popped swid */
			if (swid < 0 || swid >= SPACC_MAX_JOBS)
				job = NULL;
			else
				job = &spacc->job[spacc->job_lookup[swid]];

			if (!job) {
				ret = -EIO;
				goto ERR;
			}

			/* mark job as done */
			job->job_done = 1;
			spacc->job_lookup[swid] = SPACC_JOB_IDX_UNUSED;
			spacc_errcode = SPACC_GET_STATUS_RET_CODE(cmdstat);

			switch (spacc_errcode) {
			case SPACC_ICVFAIL:
				ret = -EBADMSG;
				break;
			case SPACC_MEMERR:
				ret = -EINVAL;
				break;
			case SPACC_BLOCKERR:
				ret = -EINVAL;
				break;
			case SPACC_SECERR:
				ret = -EIO;
				break;
			case SPACC_OK:
				ret = CRYPTO_OK;
				break;
			default:
				pr_debug("Invalid SPAcc Error");
			}

			job->job_err = ret;

			/*
			 * We're done touching the SPAcc hw, so release the
			 * lock across the job callback.  It must be reacquired
			 * before continuing to the next iteration.
			 */

			if (job->cb) {
				spin_unlock_irqrestore(&spacc->lock,
							*lock_flag);
				job->cb(spacc, job->cbdata);
				spin_lock_irqsave(&spacc->lock,
							*lock_flag);
			}

			(*num_popped)++;
		}
	}

	if (!*num_popped)
		pr_debug("   Failed to pop a single job\n");

ERR:
	spacc_process_jb(spacc);

	/* reset the WD timer to the original value*/
	if (spacc->op_mode == SPACC_OP_MODE_WD)
		spacc_set_wd_count(spacc, spacc->config.wd_timer);

	if (*num_popped && spacc->spacc_notify_jobs)
		spacc->spacc_notify_jobs(spacc);

	return ret;
}

int spacc_pop_packets(struct spacc_device *spacc, int *num_popped)
{
	int err;
	unsigned long lock_flag;

	spin_lock_irqsave(&spacc->lock, lock_flag);
	err = spacc_pop_packets_ex(spacc, num_popped, &lock_flag);
	spin_unlock_irqrestore(&spacc->lock, lock_flag);

	return err;
}

uint32_t spacc_process_irq(struct spacc_device *spacc)
{
	u32 temp;
	int x, cmd_max;
	unsigned long lock_flag;

	spin_lock_irqsave(&spacc->lock, lock_flag);

	temp = readl(spacc->regmap + SPACC_REG_IRQ_STAT);

	/* clear interrupt pin and run registered callback */
	if (temp & SPACC_IRQ_STAT_STAT) {
		SPACC_IRQ_STAT_CLEAR_STAT(spacc);
		if (spacc->op_mode == SPACC_OP_MODE_IRQ) {
			spacc->config.fifo_cnt <<= 2;
			if (spacc->config.fifo_cnt >=
					spacc->config.stat_fifo_depth)
				spacc->config.fifo_cnt =
					spacc->config.stat_fifo_depth;

			/* update fifo count to allow more stati to pile up*/
			spacc_irq_stat_enable(spacc, spacc->config.fifo_cnt);
			 /* reenable CMD0 empty interrupt*/
			spacc_irq_cmdx_enable(spacc, 0, 0);
		}

		if (spacc->irq_cb_stat)
			spacc->irq_cb_stat(spacc);
	}

	/* Watchdog IRQ */
	if (spacc->op_mode == SPACC_OP_MODE_WD) {
		if (temp & SPACC_IRQ_STAT_STAT_WD) {
			if (++spacc->wdcnt == SPACC_WD_LIMIT) {
				/* this happens when you get too many IRQs that
				 * go unanswered
				 */
				spacc_irq_stat_wd_disable(spacc);
				 /* we set the STAT CNT to 1 so that every job
				  * generates an IRQ now
				  */
				spacc_irq_stat_enable(spacc, 1);
				spacc->op_mode = SPACC_OP_MODE_IRQ;
			} else if (spacc->config.wd_timer < (0xFFFFFFUL >> 4)) {
				/* if the timer isn't too high lets bump it up
				 * a bit so as to give the IRQ a chance to
				 * reply
				 */
				spacc_set_wd_count(spacc,
						   spacc->config.wd_timer << 4);
			}

			SPACC_IRQ_STAT_CLEAR_STAT_WD(spacc);
			if (spacc->irq_cb_stat_wd)
				spacc->irq_cb_stat_wd(spacc);
		}
	}

	if (spacc->op_mode == SPACC_OP_MODE_IRQ) {
		cmd_max = (spacc->config.is_qos ? SPACC_CMDX_MAX_QOS :
				SPACC_CMDX_MAX);
		for (x = 0; x < cmd_max; x++) {
			if (temp & SPACC_IRQ_STAT_CMDX(x)) {
				spacc->config.fifo_cnt = 1;
				/* disable CMD0 interrupt since STAT=1 */
				spacc_irq_cmdx_disable(spacc, x);
				spacc_irq_stat_enable(spacc,
						      spacc->config.fifo_cnt);

				SPACC_IRQ_STAT_CLEAR_CMDX(spacc, x);
				/* run registered callback */
				if (spacc->irq_cb_cmdx)
					spacc->irq_cb_cmdx(spacc, x);
			}
		}
	}

	spin_unlock_irqrestore(&spacc->lock, lock_flag);

	return temp;
}

void spacc_set_wd_count(struct spacc_device *spacc, uint32_t val)
{
	writel(val, spacc->regmap + SPACC_REG_STAT_WD_CTRL);
}

/* cmdx and cmdx_cnt depend on HW config
 * cmdx can be 0, 1 or 2
 * cmdx_cnt must be 2^6 or less
 */
void spacc_irq_cmdx_enable(struct spacc_device *spacc, int cmdx, int cmdx_cnt)
{
	u32 temp;

	/* read the reg, clear the bit range and set the new value */
	temp = readl(spacc->regmap + SPACC_REG_IRQ_CTRL) &
	       (~SPACC_IRQ_CTRL_CMDX_CNT_MASK(cmdx));
	temp |= SPACC_IRQ_CTRL_CMDX_CNT_SET(cmdx, cmdx_cnt);

	writel(temp | SPACC_IRQ_CTRL_CMDX_CNT_SET(cmdx, cmdx_cnt),
	       spacc->regmap + SPACC_REG_IRQ_CTRL);

	writel(readl(spacc->regmap + SPACC_REG_IRQ_EN) | SPACC_IRQ_EN_CMD(cmdx),
	       spacc->regmap + SPACC_REG_IRQ_EN);
}

void spacc_irq_cmdx_disable(struct spacc_device *spacc, int cmdx)
{
	writel(readl(spacc->regmap + SPACC_REG_IRQ_EN) &
	       (~SPACC_IRQ_EN_CMD(cmdx)), spacc->regmap + SPACC_REG_IRQ_EN);
}

void spacc_irq_stat_enable(struct spacc_device *spacc, int stat_cnt)
{
	u32 temp;

	temp = readl(spacc->regmap + SPACC_REG_IRQ_CTRL);
	if (spacc->config.is_qos) {
		temp &= (~SPACC_IRQ_CTRL_STAT_CNT_MASK_QOS);
		temp |= SPACC_IRQ_CTRL_STAT_CNT_SET_QOS(stat_cnt);
	} else {
		temp &= (~SPACC_IRQ_CTRL_STAT_CNT_MASK);
		temp |= SPACC_IRQ_CTRL_STAT_CNT_SET(stat_cnt);
	}

	writel(temp, spacc->regmap + SPACC_REG_IRQ_CTRL);
	writel(readl(spacc->regmap + SPACC_REG_IRQ_EN) | SPACC_IRQ_EN_STAT,
	       spacc->regmap + SPACC_REG_IRQ_EN);
}

void spacc_irq_stat_disable(struct spacc_device *spacc)
{
	writel(readl(spacc->regmap + SPACC_REG_IRQ_EN) & (~SPACC_IRQ_EN_STAT),
	       spacc->regmap + SPACC_REG_IRQ_EN);
}

void spacc_irq_stat_wd_enable(struct spacc_device *spacc)
{
	writel(readl(spacc->regmap + SPACC_REG_IRQ_EN) | SPACC_IRQ_EN_STAT_WD,
	       spacc->regmap + SPACC_REG_IRQ_EN);
}

void spacc_irq_stat_wd_disable(struct spacc_device *spacc)
{
	writel(readl(spacc->regmap + SPACC_REG_IRQ_EN) &
	       (~SPACC_IRQ_EN_STAT_WD), spacc->regmap + SPACC_REG_IRQ_EN);
}

void spacc_irq_glbl_enable(struct spacc_device *spacc)
{
	writel(readl(spacc->regmap + SPACC_REG_IRQ_EN) | SPACC_IRQ_EN_GLBL,
	       spacc->regmap + SPACC_REG_IRQ_EN);
}

void spacc_irq_glbl_disable(struct spacc_device *spacc)
{
	writel(readl(spacc->regmap + SPACC_REG_IRQ_EN) & (~SPACC_IRQ_EN_GLBL),
	       spacc->regmap + SPACC_REG_IRQ_EN);
}

void spacc_disable_int (struct spacc_device *spacc)
{
	writel(0, spacc->regmap + SPACC_REG_IRQ_EN);
}

/* a function to run callbacks in the IRQ handler */
irqreturn_t spacc_irq_handler(int irq, void *dev)
{
	struct spacc_priv *priv = platform_get_drvdata(to_platform_device(dev));
	struct spacc_device *spacc = &priv->spacc;

	if (spacc->config.oldtimer != spacc->config.timer) {
		priv->spacc.config.wd_timer = spacc->config.timer;
		spacc_set_wd_count(&priv->spacc, priv->spacc.config.wd_timer);
		spacc->config.oldtimer = spacc->config.timer;
	}

	/* check irq flags and process as required */
	if (!spacc_process_irq(spacc))
		return IRQ_NONE;

	return IRQ_HANDLED;
}
