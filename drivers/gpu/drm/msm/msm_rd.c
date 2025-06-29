// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

/* For debugging crashes, userspace can:
 *
 *   tail -f /sys/kernel/debug/dri/<minor>/rd > logfile.rd
 *
 * to log the cmdstream in a format that is understood by freedreno/cffdump
 * utility.  By comparing the last successfully completed fence #, to the
 * cmdstream for the next fence, you can narrow down which process and submit
 * caused the gpu crash/lockup.
 *
 * Additionally:
 *
 *   tail -f /sys/kernel/debug/dri/<minor>/hangrd > logfile.rd
 *
 * will capture just the cmdstream from submits which triggered a GPU hang.
 *
 * This bypasses drm_debugfs_create_files() mainly because we need to use
 * our own fops for a bit more control.  In particular, we don't want to
 * do anything if userspace doesn't have the debugfs file open.
 *
 * The module-param "rd_full", which defaults to false, enables snapshotting
 * all (non-written) buffers in the submit, rather than just cmdstream bo's.
 * This is useful to capture the contents of (for example) vbo's or textures,
 * or shader programs (if not emitted inline in cmdstream).
 */

#include <linux/circ_buf.h>
#include <linux/debugfs.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include <drm/drm_file.h>

#include "msm_drv.h"
#include "msm_gpu.h"
#include "msm_gem.h"

bool rd_full = false;
MODULE_PARM_DESC(rd_full, "If true, $debugfs/.../rd will snapshot all buffer contents");
module_param_named(rd_full, rd_full, bool, 0600);

#ifdef CONFIG_DEBUG_FS

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
	RD_CHIP_ID,
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

	/* fifo access is synchronized on the producer side by
	 * write_lock.  And read_lock synchronizes the reads
	 */
	struct mutex read_lock, write_lock;

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

		wait_event(rd->fifo_event, circ_space(&rd->fifo) > 0 || !rd->open);
		if (!rd->open)
			return;

		/* Note that smp_load_acquire() is not strictly required
		 * as CIRC_SPACE_TO_END() does not access the tail more
		 * than once.
		 */
		n = min(sz, circ_space_to_end(&rd->fifo));
		memcpy(fptr, ptr, n);

		smp_store_release(&fifo->head, (fifo->head + n) & (BUF_SZ - 1));
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

	/* Note that smp_load_acquire() is not strictly required
	 * as CIRC_CNT_TO_END() does not access the head more than
	 * once.
	 */
	n = min_t(int, sz, circ_count_to_end(&rd->fifo));
	if (copy_to_user(buf, fptr, n)) {
		ret = -EFAULT;
		goto out;
	}

	smp_store_release(&fifo->tail, (fifo->tail + n) & (BUF_SZ - 1));
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
	uint32_t zero = 0;
	int ret = 0;

	if (!gpu)
		return -ENODEV;

	mutex_lock(&gpu->lock);

	if (rd->open) {
		ret = -EBUSY;
		goto out;
	}

	file->private_data = rd;
	rd->open = true;

	/* Reset fifo to clear any previously unread data: */
	rd->fifo.head = rd->fifo.tail = 0;

	/* the parsing tools need to know gpu-id to know which
	 * register database to load.
	 *
	 * Note: These particular params do not require a context
	 */
	gpu->funcs->get_param(gpu, NULL, MSM_PARAM_GPU_ID, &val, &zero);
	gpu_id = val;

	rd_write_section(rd, RD_GPU_ID, &gpu_id, sizeof(gpu_id));

	gpu->funcs->get_param(gpu, NULL, MSM_PARAM_CHIP_ID, &val, &zero);
	rd_write_section(rd, RD_CHIP_ID, &val, sizeof(val));

out:
	mutex_unlock(&gpu->lock);
	return ret;
}

static int rd_release(struct inode *inode, struct file *file)
{
	struct msm_rd_state *rd = inode->i_private;

	rd->open = false;
	wake_up_all(&rd->fifo_event);

	return 0;
}


static const struct file_operations rd_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = rd_open,
	.read = rd_read,
	.release = rd_release,
};


static void rd_cleanup(struct msm_rd_state *rd)
{
	if (!rd)
		return;

	mutex_destroy(&rd->read_lock);
	mutex_destroy(&rd->write_lock);
	kfree(rd);
}

static struct msm_rd_state *rd_init(struct drm_minor *minor, const char *name)
{
	struct msm_rd_state *rd;

	rd = kzalloc(sizeof(*rd), GFP_KERNEL);
	if (!rd)
		return ERR_PTR(-ENOMEM);

	rd->dev = minor->dev;
	rd->fifo.buf = rd->buf;

	mutex_init(&rd->read_lock);
	mutex_init(&rd->write_lock);

	init_waitqueue_head(&rd->fifo_event);

	debugfs_create_file(name, S_IFREG | S_IRUGO, minor->debugfs_root, rd,
			    &rd_debugfs_fops);

	return rd;
}

int msm_rd_debugfs_init(struct drm_minor *minor)
{
	struct msm_drm_private *priv = minor->dev->dev_private;
	struct msm_rd_state *rd;
	int ret;

	if (!priv->gpu_pdev)
		return 0;

	/* only create on first minor: */
	if (priv->rd)
		return 0;

	rd = rd_init(minor, "rd");
	if (IS_ERR(rd)) {
		ret = PTR_ERR(rd);
		goto fail;
	}

	priv->rd = rd;

	rd = rd_init(minor, "hangrd");
	if (IS_ERR(rd)) {
		ret = PTR_ERR(rd);
		goto fail;
	}

	priv->hangrd = rd;

	return 0;

fail:
	msm_rd_debugfs_cleanup(priv);
	return ret;
}

void msm_rd_debugfs_cleanup(struct msm_drm_private *priv)
{
	rd_cleanup(priv->rd);
	priv->rd = NULL;

	rd_cleanup(priv->hangrd);
	priv->hangrd = NULL;
}

static void snapshot_buf(struct msm_rd_state *rd, struct drm_gem_object *obj,
			 uint64_t iova, bool full, size_t offset, size_t size)
{
	const char *buf;

	/*
	 * Always write the GPUADDR header so can get a complete list of all the
	 * buffers in the cmd
	 */
	rd_write_section(rd, RD_GPUADDR,
			(uint32_t[3]){ iova, size, iova >> 32 }, 12);

	if (!full)
		return;

	buf = msm_gem_get_vaddr_active(obj);
	if (IS_ERR(buf))
		return;

	buf += offset;

	rd_write_section(rd, RD_BUFFER_CONTENTS, buf, size);

	msm_gem_put_vaddr_locked(obj);
}

/* called under gpu->lock */
void msm_rd_dump_submit(struct msm_rd_state *rd, struct msm_gem_submit *submit,
		const char *fmt, ...)
{
	extern bool rd_full;
	struct task_struct *task;
	char msg[256];
	int i, n;

	if (!rd->open)
		return;

	mutex_lock(&rd->write_lock);

	if (fmt) {
		va_list args;

		va_start(args, fmt);
		n = vscnprintf(msg, sizeof(msg), fmt, args);
		va_end(args);

		rd_write_section(rd, RD_CMD, msg, ALIGN(n, 4));
	}

	rcu_read_lock();
	task = pid_task(submit->pid, PIDTYPE_PID);
	if (task) {
		n = scnprintf(msg, sizeof(msg), "%.*s/%d: fence=%u",
				TASK_COMM_LEN, task->comm,
				pid_nr(submit->pid), submit->seqno);
	} else {
		n = scnprintf(msg, sizeof(msg), "???/%d: fence=%u",
				pid_nr(submit->pid), submit->seqno);
	}
	rcu_read_unlock();

	rd_write_section(rd, RD_CMD, msg, ALIGN(n, 4));

	if (msm_context_is_vmbind(submit->queue->ctx)) {
		struct drm_gpuva *vma;

		drm_gpuvm_resv_assert_held(submit->vm);

		drm_gpuvm_for_each_va (vma, submit->vm) {
			bool dump = rd_full || (vma->flags & MSM_VMA_DUMP);

			/* Skip MAP_NULL/PRR VMAs: */
			if (!vma->gem.obj)
				continue;

			snapshot_buf(rd, vma->gem.obj, vma->va.addr, dump,
				     vma->gem.offset, vma->va.range);
		}

	} else {
		for (i = 0; i < submit->nr_bos; i++) {
			struct drm_gem_object *obj = submit->bos[i].obj;
			bool dump = rd_full || (submit->bos[i].flags & MSM_SUBMIT_BO_DUMP);

			snapshot_buf(rd, obj, submit->bos[i].iova, dump, 0, obj->size);
		}

		for (i = 0; i < submit->nr_cmds; i++) {
			uint32_t szd  = submit->cmd[i].size; /* in dwords */
			int idx = submit->cmd[i].idx;
			bool dump = rd_full || (submit->bos[idx].flags & MSM_SUBMIT_BO_DUMP);

			/* snapshot cmdstream bo's (if we haven't already): */
			if (!dump) {
				struct drm_gem_object *obj = submit->bos[idx].obj;
				size_t offset = submit->cmd[i].iova - submit->bos[idx].iova;

				snapshot_buf(rd, obj, submit->cmd[i].iova, true,
					offset, szd * 4);
			}
		}
	}

	for (i = 0; i < submit->nr_cmds; i++) {
		uint64_t iova = submit->cmd[i].iova;
		uint32_t szd  = submit->cmd[i].size; /* in dwords */

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
				(uint32_t[3]){ iova, szd, iova >> 32 }, 12);
			break;
		}
	}

	mutex_unlock(&rd->write_lock);
}
#endif
