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

/* For debugging crashes, userspace can:
 *
 *   tail -f /sys/kernel/debug/dri/<minor>/rd > logfile.rd
 *
 * To log the cmdstream in a format that is understood by freedreno/cffdump
 * utility.  By comparing the last successfully completed fence #, to the
 * cmdstream for the next fence, you can narrow down which process and submit
 * caused the gpu crash/lockup.
 *
 * This bypasses drm_debugfs_create_files() mainly because we need to use
 * our own fops for a bit more control.  In particular, we don't want to
 * do anything if userspace doesn't have the debugfs file open.
 */

#ifdef CONFIG_DEBUG_FS

#include <linux/kfifo.h>
#include <linux/debugfs.h>
#include <linux/circ_buf.h>
#include <linux/wait.h>

#include "msm_drv.h"
#include "msm_gpu.h"
#include "msm_gem.h"

enum rd_sect_type {
	RD_NONE,
	RD_TEST,       /* ascii text */
	RD_CMD,        /* ascii text */
	RD_GPUADDR,    /* u32 gpuaddr, u32 size */
	RD_CONTEXT,    /* raw dump */
	RD_CMDSTREAM,  /* raw dump */
	RD_CMDSTREAM_ADDR, /* gpu addr of cmdstream */
	RD_PARAM,      /* u32 param_type, u32 param_val, u32 bitlen */
	RD_FLUSH,      /* empty, clear previous params */
	RD_PROGRAM,    /* shader program, raw dump */
	RD_VERT_SHADER,
	RD_FRAG_SHADER,
	RD_BUFFER_CONTENTS,
	RD_GPU_ID,
};

#define BUF_SZ 512  /* should be power of 2 */

/* space used: */
#define circ_count(circ) \
	(CIRC_CNT((circ)->head, (circ)->tail, BUF_SZ))
#define circ_count_to_end(circ) \
	(CIRC_CNT_TO_END((circ)->head, (circ)->tail, BUF_SZ))
/* space available: */
#define circ_space(circ) \
	(CIRC_SPACE((circ)->head, (circ)->tail, BUF_SZ))
#define circ_space_to_end(circ) \
	(CIRC_SPACE_TO_END((circ)->head, (circ)->tail, BUF_SZ))

struct msm_rd_state {
	struct drm_device *dev;

	bool open;

	struct dentry *ent;
	struct drm_info_node *node;

	/* current submit to read out: */
	struct msm_gem_submit *submit;

	/* fifo access is synchronized on the producer side by
	 * struct_mutex held by submit code (otherwise we could
	 * end up w/ cmds logged in different order than they
	 * were executed).  And read_lock synchronizes the reads
	 */
	struct mutex read_lock;

	wait_queue_head_t fifo_event;
	struct circ_buf fifo;

	char buf[BUF_SZ];
};

static void rd_write(struct msm_rd_state *rd, const void *buf, int sz)
{
	struct circ_buf *fifo = &rd->fifo;
	const char *ptr = buf;

	while (sz > 0) {
		char *fptr = &fifo->buf[fifo->head];
		int n;

		wait_event(rd->fifo_event, circ_space(&rd->fifo) > 0);

		n = min(sz, circ_space_to_end(&rd->fifo));
		memcpy(fptr, ptr, n);

		fifo->head = (fifo->head + n) & (BUF_SZ - 1);
		sz  -= n;
		ptr += n;

		wake_up_all(&rd->fifo_event);
	}
}

static void rd_write_section(struct msm_rd_state *rd,
		enum rd_sect_type type, const void *buf, int sz)
{
	rd_write(rd, &type, 4);
	rd_write(rd, &sz, 4);
	rd_write(rd, buf, sz);
}

static ssize_t rd_read(struct file *file, char __user *buf,
		size_t sz, loff_t *ppos)
{
	struct msm_rd_state *rd = file->private_data;
	struct circ_buf *fifo = &rd->fifo;
	const char *fptr = &fifo->buf[fifo->tail];
	int n = 0, ret = 0;

	mutex_lock(&rd->read_lock);

	ret = wait_event_interruptible(rd->fifo_event,
			circ_count(&rd->fifo) > 0);
	if (ret)
		goto out;

	n = min_t(int, sz, circ_count_to_end(&rd->fifo));
	ret = copy_to_user(buf, fptr, n);
	if (ret)
		goto out;

	fifo->tail = (fifo->tail + n) & (BUF_SZ - 1);
	*ppos += n;

	wake_up_all(&rd->fifo_event);

out:
	mutex_unlock(&rd->read_lock);
	if (ret)
		return ret;
	return n;
}

static int rd_open(struct inode *inode, struct file *file)
{
	struct msm_rd_state *rd = inode->i_private;
	struct drm_device *dev = rd->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;
	uint64_t val;
	uint32_t gpu_id;
	int ret = 0;

	mutex_lock(&dev->struct_mutex);

	if (rd->open || !gpu) {
		ret = -EBUSY;
		goto out;
	}

	file->private_data = rd;
	rd->open = true;

	/* the parsing tools need to know gpu-id to know which
	 * register database to load.
	 */
	gpu->funcs->get_param(gpu, MSM_PARAM_GPU_ID, &val);
	gpu_id = val;

	rd_write_section(rd, RD_GPU_ID, &gpu_id, sizeof(gpu_id));

out:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static int rd_release(struct inode *inode, struct file *file)
{
	struct msm_rd_state *rd = inode->i_private;
	rd->open = false;
	return 0;
}


static const struct file_operations rd_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = rd_open,
	.read = rd_read,
	.llseek = no_llseek,
	.release = rd_release,
};

int msm_rd_debugfs_init(struct drm_minor *minor)
{
	struct msm_drm_private *priv = minor->dev->dev_private;
	struct msm_rd_state *rd;

	/* only create on first minor: */
	if (priv->rd)
		return 0;

	rd = kzalloc(sizeof(*rd), GFP_KERNEL);
	if (!rd)
		return -ENOMEM;

	rd->dev = minor->dev;
	rd->fifo.buf = rd->buf;

	mutex_init(&rd->read_lock);
	priv->rd = rd;

	init_waitqueue_head(&rd->fifo_event);

	rd->node = kzalloc(sizeof(*rd->node), GFP_KERNEL);
	if (!rd->node)
		goto fail;

	rd->ent = debugfs_create_file("rd", S_IFREG | S_IRUGO,
			minor->debugfs_root, rd, &rd_debugfs_fops);
	if (!rd->ent) {
		DRM_ERROR("Cannot create /sys/kernel/debug/dri/%s/rd\n",
				minor->debugfs_root->d_name.name);
		goto fail;
	}

	rd->node->minor = minor;
	rd->node->dent  = rd->ent;
	rd->node->info_ent = NULL;

	mutex_lock(&minor->debugfs_lock);
	list_add(&rd->node->list, &minor->debugfs_list);
	mutex_unlock(&minor->debugfs_lock);

	return 0;

fail:
	msm_rd_debugfs_cleanup(minor);
	return -1;
}

void msm_rd_debugfs_cleanup(struct drm_minor *minor)
{
	struct msm_drm_private *priv = minor->dev->dev_private;
	struct msm_rd_state *rd = priv->rd;

	if (!rd)
		return;

	priv->rd = NULL;

	debugfs_remove(rd->ent);

	if (rd->node) {
		mutex_lock(&minor->debugfs_lock);
		list_del(&rd->node->list);
		mutex_unlock(&minor->debugfs_lock);
		kfree(rd->node);
	}

	mutex_destroy(&rd->read_lock);

	kfree(rd);
}

/* called under struct_mutex */
void msm_rd_dump_submit(struct msm_gem_submit *submit)
{
	struct drm_device *dev = submit->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_rd_state *rd = priv->rd;
	char msg[128];
	int i, n;

	if (!rd->open)
		return;

	/* writing into fifo is serialized by caller, and
	 * rd->read_lock is used to serialize the reads
	 */
	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	n = snprintf(msg, sizeof(msg), "%.*s/%d: fence=%u",
			TASK_COMM_LEN, current->comm, task_pid_nr(current),
			submit->fence->seqno);

	rd_write_section(rd, RD_CMD, msg, ALIGN(n, 4));

	/* could be nice to have an option (module-param?) to snapshot
	 * all the bo's associated with the submit.  Handy to see vtx
	 * buffers, etc.  For now just the cmdstream bo's is enough.
	 */

	for (i = 0; i < submit->nr_cmds; i++) {
		uint32_t idx  = submit->cmd[i].idx;
		uint32_t iova = submit->cmd[i].iova;
		uint32_t szd  = submit->cmd[i].size; /* in dwords */
		struct msm_gem_object *obj = submit->bos[idx].obj;
		const char *buf = msm_gem_vaddr_locked(&obj->base);

		if (IS_ERR(buf))
			continue;

		buf += iova - submit->bos[idx].iova;

		rd_write_section(rd, RD_GPUADDR,
				(uint32_t[2]){ iova, szd * 4 }, 8);
		rd_write_section(rd, RD_BUFFER_CONTENTS,
				buf, szd * 4);

		switch (submit->cmd[i].type) {
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
			/* ignore IB-targets, we've logged the buffer, the
			 * parser tool will follow the IB based on the logged
			 * buffer/gpuaddr, so nothing more to do.
			 */
			break;
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
		case MSM_SUBMIT_CMD_BUF:
			rd_write_section(rd, RD_CMDSTREAM_ADDR,
					(uint32_t[2]){ iova, szd }, 8);
			break;
		}
	}
}
#endif
