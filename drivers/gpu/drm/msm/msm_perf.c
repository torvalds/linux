/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* For profiling, userspace can:
 *
 *   tail -f /sys/kernel/debug/dri/<minor>/gpu
 *
 * This will enable performance counters/profiling to track the busy time
 * and any gpu specific performance counters that are supported.
 */

#ifdef CONFIG_DEBUG_FS

#include <linux/debugfs.h>

#include "msm_drv.h"
#include "msm_gpu.h"

struct msm_perf_state {
	struct drm_device *dev;

	bool open;
	int cnt;
	struct mutex read_lock;

	char buf[256];
	int buftot, bufpos;

	unsigned long next_jiffies;
};

#define SAMPLE_TIME (HZ/4)

/* wait for next sample time: */
static int wait_sample(struct msm_perf_state *perf)
{
	unsigned long start_jiffies = jiffies;

	if (time_after(perf->next_jiffies, start_jiffies)) {
		unsigned long remaining_jiffies =
			perf->next_jiffies - start_jiffies;
		int ret = schedule_timeout_interruptible(remaining_jiffies);
		if (ret > 0) {
			/* interrupted */
			return -ERESTARTSYS;
		}
	}
	perf->next_jiffies += SAMPLE_TIME;
	return 0;
}

static int refill_buf(struct msm_perf_state *perf)
{
	struct msm_drm_private *priv = perf->dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;
	char *ptr = perf->buf;
	int rem = sizeof(perf->buf);
	int i, n;

	if ((perf->cnt++ % 32) == 0) {
		/* Header line: */
		n = snprintf(ptr, rem, "%%BUSY");
		ptr += n;
		rem -= n;

		for (i = 0; i < gpu->num_perfcntrs; i++) {
			const struct msm_gpu_perfcntr *perfcntr = &gpu->perfcntrs[i];
			n = snprintf(ptr, rem, "\t%s", perfcntr->name);
			ptr += n;
			rem -= n;
		}
	} else {
		/* Sample line: */
		uint32_t activetime = 0, totaltime = 0;
		uint32_t cntrs[5];
		uint32_t val;
		int ret;

		/* sleep until next sample time: */
		ret = wait_sample(perf);
		if (ret)
			return ret;

		ret = msm_gpu_perfcntr_sample(gpu, &activetime, &totaltime,
				ARRAY_SIZE(cntrs), cntrs);
		if (ret < 0)
			return ret;

		val = totaltime ? 1000 * activetime / totaltime : 0;
		n = snprintf(ptr, rem, "%3d.%d%%", val / 10, val % 10);
		ptr += n;
		rem -= n;

		for (i = 0; i < ret; i++) {
			/* cycle counters (I think).. convert to MHz.. */
			val = cntrs[i] / 10000;
			n = snprintf(ptr, rem, "\t%5d.%02d",
					val / 100, val % 100);
			ptr += n;
			rem -= n;
		}
	}

	n = snprintf(ptr, rem, "\n");
	ptr += n;
	rem -= n;

	perf->bufpos = 0;
	perf->buftot = ptr - perf->buf;

	return 0;
}

static ssize_t perf_read(struct file *file, char __user *buf,
		size_t sz, loff_t *ppos)
{
	struct msm_perf_state *perf = file->private_data;
	int n = 0, ret = 0;

	mutex_lock(&perf->read_lock);

	if (perf->bufpos >= perf->buftot) {
		ret = refill_buf(perf);
		if (ret)
			goto out;
	}

	n = min((int)sz, perf->buftot - perf->bufpos);
	if (copy_to_user(buf, &perf->buf[perf->bufpos], n)) {
		ret = -EFAULT;
		goto out;
	}

	perf->bufpos += n;
	*ppos += n;

out:
	mutex_unlock(&perf->read_lock);
	if (ret)
		return ret;
	return n;
}

static int perf_open(struct inode *inode, struct file *file)
{
	struct msm_perf_state *perf = inode->i_private;
	struct drm_device *dev = perf->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;
	int ret = 0;

	mutex_lock(&dev->struct_mutex);

	if (perf->open || !gpu) {
		ret = -EBUSY;
		goto out;
	}

	file->private_data = perf;
	perf->open = true;
	perf->cnt = 0;
	perf->buftot = 0;
	perf->bufpos = 0;
	msm_gpu_perfcntr_start(gpu);
	perf->next_jiffies = jiffies + SAMPLE_TIME;

out:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static int perf_release(struct inode *inode, struct file *file)
{
	struct msm_perf_state *perf = inode->i_private;
	struct msm_drm_private *priv = perf->dev->dev_private;
	msm_gpu_perfcntr_stop(priv->gpu);
	perf->open = false;
	return 0;
}


static const struct file_operations perf_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = perf_open,
	.read = perf_read,
	.llseek = no_llseek,
	.release = perf_release,
};

int msm_perf_debugfs_init(struct drm_minor *minor)
{
	struct msm_drm_private *priv = minor->dev->dev_private;
	struct msm_perf_state *perf;
	struct dentry *ent;

	/* only create on first minor: */
	if (priv->perf)
		return 0;

	perf = kzalloc(sizeof(*perf), GFP_KERNEL);
	if (!perf)
		return -ENOMEM;

	perf->dev = minor->dev;

	mutex_init(&perf->read_lock);
	priv->perf = perf;

	ent = debugfs_create_file("perf", S_IFREG | S_IRUGO,
			minor->debugfs_root, perf, &perf_debugfs_fops);
	if (!ent) {
		DRM_ERROR("Cannot create /sys/kernel/debug/dri/%pd/perf\n",
				minor->debugfs_root);
		goto fail;
	}

	return 0;

fail:
	msm_perf_debugfs_cleanup(priv);
	return -1;
}

void msm_perf_debugfs_cleanup(struct msm_drm_private *priv)
{
	struct msm_perf_state *perf = priv->perf;

	if (!perf)
		return;

	priv->perf = NULL;

	mutex_destroy(&perf->read_lock);

	kfree(perf);
}

#endif
