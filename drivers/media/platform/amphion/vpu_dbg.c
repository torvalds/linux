// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-device.h>
#include <linux/debugfs.h>
#include "vpu.h"
#include "vpu_defs.h"
#include "vpu_core.h"
#include "vpu_helpers.h"
#include "vpu_cmds.h"
#include "vpu_rpc.h"
#include "vpu_v4l2.h"

struct print_buf_desc {
	u32 start_h_phy;
	u32 start_h_vir;
	u32 start_m;
	u32 bytes;
	u32 read;
	u32 write;
	char buffer[];
};

static char *vb2_stat_name[] = {
	[VB2_BUF_STATE_DEQUEUED] = "dequeued",
	[VB2_BUF_STATE_IN_REQUEST] = "in_request",
	[VB2_BUF_STATE_PREPARING] = "preparing",
	[VB2_BUF_STATE_QUEUED] = "queued",
	[VB2_BUF_STATE_ACTIVE] = "active",
	[VB2_BUF_STATE_DONE] = "done",
	[VB2_BUF_STATE_ERROR] = "error",
};

static char *vpu_stat_name[] = {
	[VPU_BUF_STATE_IDLE] = "idle",
	[VPU_BUF_STATE_INUSE] = "inuse",
	[VPU_BUF_STATE_DECODED] = "decoded",
	[VPU_BUF_STATE_READY] = "ready",
	[VPU_BUF_STATE_SKIP] = "skip",
	[VPU_BUF_STATE_ERROR] = "error",
};

static inline const char *to_vpu_stat_name(int state)
{
	if (state <= VPU_BUF_STATE_ERROR)
		return vpu_stat_name[state];
	return "unknown";
}

static int vpu_dbg_instance(struct seq_file *s, void *data)
{
	struct vpu_inst *inst = s->private;
	char str[128];
	int num;
	struct vb2_queue *vq;
	int i;

	if (!inst->fh.m2m_ctx)
		return 0;
	num = scnprintf(str, sizeof(str), "[%s]\n", vpu_core_type_desc(inst->type));
	if (seq_write(s, str, num))
		return 0;

	num = scnprintf(str, sizeof(str), "tgig = %d,pid = %d\n", inst->tgid, inst->pid);
	if (seq_write(s, str, num))
		return 0;
	num = scnprintf(str, sizeof(str), "state = %s\n", vpu_codec_state_name(inst->state));
	if (seq_write(s, str, num))
		return 0;
	num = scnprintf(str, sizeof(str),
			"min_buffer_out = %d, min_buffer_cap = %d\n",
			inst->min_buffer_out, inst->min_buffer_cap);
	if (seq_write(s, str, num))
		return 0;

	vq = v4l2_m2m_get_src_vq(inst->fh.m2m_ctx);
	num = scnprintf(str, sizeof(str),
			"output (%2d, %2d): fmt = %c%c%c%c %d x %d, %d;",
			vb2_is_streaming(vq),
			vb2_get_num_buffers(vq),
			inst->out_format.pixfmt,
			inst->out_format.pixfmt >> 8,
			inst->out_format.pixfmt >> 16,
			inst->out_format.pixfmt >> 24,
			inst->out_format.width,
			inst->out_format.height,
			vq->last_buffer_dequeued);
	if (seq_write(s, str, num))
		return 0;
	for (i = 0; i < inst->out_format.mem_planes; i++) {
		num = scnprintf(str, sizeof(str), " %d(%d)",
				vpu_get_fmt_plane_size(&inst->out_format, i),
				inst->out_format.bytesperline[i]);
		if (seq_write(s, str, num))
			return 0;
	}
	if (seq_write(s, "\n", 1))
		return 0;

	vq = v4l2_m2m_get_dst_vq(inst->fh.m2m_ctx);
	num = scnprintf(str, sizeof(str),
			"capture(%2d, %2d): fmt = %c%c%c%c %d x %d, %d;",
			vb2_is_streaming(vq),
			vb2_get_num_buffers(vq),
			inst->cap_format.pixfmt,
			inst->cap_format.pixfmt >> 8,
			inst->cap_format.pixfmt >> 16,
			inst->cap_format.pixfmt >> 24,
			inst->cap_format.width,
			inst->cap_format.height,
			vq->last_buffer_dequeued);
	if (seq_write(s, str, num))
		return 0;
	for (i = 0; i < inst->cap_format.mem_planes; i++) {
		num = scnprintf(str, sizeof(str), " %d(%d)",
				vpu_get_fmt_plane_size(&inst->cap_format, i),
				inst->cap_format.bytesperline[i]);
		if (seq_write(s, str, num))
			return 0;
	}
	if (seq_write(s, "\n", 1))
		return 0;
	num = scnprintf(str, sizeof(str), "crop: (%d, %d) %d x %d\n",
			inst->crop.left,
			inst->crop.top,
			inst->crop.width,
			inst->crop.height);
	if (seq_write(s, str, num))
		return 0;

	vq = v4l2_m2m_get_src_vq(inst->fh.m2m_ctx);
	for (i = 0; i < vb2_get_num_buffers(vq); i++) {
		struct vb2_buffer *vb;
		struct vb2_v4l2_buffer *vbuf;

		vb = vb2_get_buffer(vq, i);
		if (!vb)
			continue;

		if (vb->state == VB2_BUF_STATE_DEQUEUED)
			continue;

		vbuf = to_vb2_v4l2_buffer(vb);

		num = scnprintf(str, sizeof(str),
				"output [%2d] state = %10s, %8s\n",
				i, vb2_stat_name[vb->state],
				to_vpu_stat_name(vpu_get_buffer_state(vbuf)));
		if (seq_write(s, str, num))
			return 0;
	}

	vq = v4l2_m2m_get_dst_vq(inst->fh.m2m_ctx);
	for (i = 0; i < vb2_get_num_buffers(vq); i++) {
		struct vb2_buffer *vb;
		struct vb2_v4l2_buffer *vbuf;

		vb = vb2_get_buffer(vq, i);
		if (!vb)
			continue;

		if (vb->state == VB2_BUF_STATE_DEQUEUED)
			continue;

		vbuf = to_vb2_v4l2_buffer(vb);

		num = scnprintf(str, sizeof(str),
				"capture[%2d] state = %10s, %8s\n",
				i, vb2_stat_name[vb->state],
				to_vpu_stat_name(vpu_get_buffer_state(vbuf)));
		if (seq_write(s, str, num))
			return 0;
	}

	num = scnprintf(str, sizeof(str), "sequence = %d\n", inst->sequence);
	if (seq_write(s, str, num))
		return 0;

	if (inst->use_stream_buffer) {
		num = scnprintf(str, sizeof(str), "stream_buffer = %d / %d, <%pad, 0x%x>\n",
				vpu_helper_get_used_space(inst),
				inst->stream_buffer.length,
				&inst->stream_buffer.phys,
				inst->stream_buffer.length);
		if (seq_write(s, str, num))
			return 0;
	}
	num = scnprintf(str, sizeof(str), "kfifo len = 0x%x\n", kfifo_len(&inst->msg_fifo));
	if (seq_write(s, str, num))
		return 0;

	num = scnprintf(str, sizeof(str), "flow :\n");
	if (seq_write(s, str, num))
		return 0;

	mutex_lock(&inst->core->cmd_lock);
	for (i = 0; i < ARRAY_SIZE(inst->flows); i++) {
		u32 idx = (inst->flow_idx + i) % (ARRAY_SIZE(inst->flows));

		if (!inst->flows[idx])
			continue;
		num = scnprintf(str, sizeof(str), "\t[%s] %s\n",
				inst->flows[idx] >= VPU_MSG_ID_NOOP ? "M" : "C",
				vpu_id_name(inst->flows[idx]));
		if (seq_write(s, str, num)) {
			mutex_unlock(&inst->core->cmd_lock);
			return 0;
		}
	}
	mutex_unlock(&inst->core->cmd_lock);

	i = 0;
	while (true) {
		num = call_vop(inst, get_debug_info, str, sizeof(str), i++);
		if (num <= 0)
			break;
		if (seq_write(s, str, num))
			return 0;
	}

	return 0;
}

static int vpu_dbg_core(struct seq_file *s, void *data)
{
	struct vpu_core *core = s->private;
	struct vpu_shared_addr *iface = core->iface;
	char str[128];
	int num;

	num = scnprintf(str, sizeof(str), "[%s]\n", vpu_core_type_desc(core->type));
	if (seq_write(s, str, num))
		return 0;

	num = scnprintf(str, sizeof(str), "boot_region  = <%pad, 0x%x>\n",
			&core->fw.phys, core->fw.length);
	if (seq_write(s, str, num))
		return 0;
	num = scnprintf(str, sizeof(str), "rpc_region   = <%pad, 0x%x> used = 0x%x\n",
			&core->rpc.phys, core->rpc.length, core->rpc.bytesused);
	if (seq_write(s, str, num))
		return 0;
	num = scnprintf(str, sizeof(str), "fwlog_region = <%pad, 0x%x>\n",
			&core->log.phys, core->log.length);
	if (seq_write(s, str, num))
		return 0;

	num = scnprintf(str, sizeof(str), "power %s\n",
			vpu_iface_get_power_state(core) ? "on" : "off");
	if (seq_write(s, str, num))
		return 0;
	num = scnprintf(str, sizeof(str), "state = %d\n", core->state);
	if (seq_write(s, str, num))
		return 0;
	if (core->state == VPU_CORE_DEINIT)
		return 0;
	num = scnprintf(str, sizeof(str), "fw version = %d.%d.%d\n",
			(core->fw_version >> 16) & 0xff,
			(core->fw_version >> 8) & 0xff,
			core->fw_version & 0xff);
	if (seq_write(s, str, num))
		return 0;
	num = scnprintf(str, sizeof(str), "instances = %d/%d (0x%02lx), %d\n",
			hweight32(core->instance_mask),
			core->supported_instance_count,
			core->instance_mask,
			core->request_count);
	if (seq_write(s, str, num))
		return 0;
	num = scnprintf(str, sizeof(str), "kfifo len = 0x%x\n", kfifo_len(&core->msg_fifo));
	if (seq_write(s, str, num))
		return 0;
	num = scnprintf(str, sizeof(str),
			"cmd_buf:[0x%x, 0x%x], wptr = 0x%x, rptr = 0x%x\n",
			iface->cmd_desc->start,
			iface->cmd_desc->end,
			iface->cmd_desc->wptr,
			iface->cmd_desc->rptr);
	if (seq_write(s, str, num))
		return 0;
	num = scnprintf(str, sizeof(str),
			"msg_buf:[0x%x, 0x%x], wptr = 0x%x, rptr = 0x%x\n",
			iface->msg_desc->start,
			iface->msg_desc->end,
			iface->msg_desc->wptr,
			iface->msg_desc->rptr);
	if (seq_write(s, str, num))
		return 0;

	return 0;
}

static int vpu_dbg_fwlog(struct seq_file *s, void *data)
{
	struct vpu_core *core = s->private;
	struct print_buf_desc *print_buf;
	int length;
	u32 rptr;
	u32 wptr;
	int ret = 0;

	if (!core->log.virt || core->state == VPU_CORE_DEINIT)
		return 0;

	print_buf = core->log.virt;
	rptr = print_buf->read;
	wptr = print_buf->write;

	if (rptr == wptr)
		return 0;
	else if (rptr < wptr)
		length = wptr - rptr;
	else
		length = print_buf->bytes + wptr - rptr;

	if (s->count + length >= s->size) {
		s->count = s->size;
		return 0;
	}

	if (rptr + length >= print_buf->bytes) {
		int num = print_buf->bytes - rptr;

		if (seq_write(s, print_buf->buffer + rptr, num))
			ret = -1;
		length -= num;
		rptr = 0;
	}

	if (length) {
		if (seq_write(s, print_buf->buffer + rptr, length))
			ret = -1;
		rptr += length;
	}
	if (!ret)
		print_buf->read = rptr;

	return 0;
}

static int vpu_dbg_inst_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, vpu_dbg_instance, inode->i_private);
}

static ssize_t vpu_dbg_inst_write(struct file *file,
				  const char __user *user_buf, size_t size, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct vpu_inst *inst = s->private;

	vpu_session_debug(inst);

	return size;
}

static ssize_t vpu_dbg_core_write(struct file *file,
				  const char __user *user_buf, size_t size, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct vpu_core *core = s->private;

	pm_runtime_resume_and_get(core->dev);
	mutex_lock(&core->lock);
	if (vpu_iface_get_power_state(core) && !core->request_count) {
		dev_info(core->dev, "reset\n");
		if (!vpu_core_sw_reset(core)) {
			vpu_core_set_state(core, VPU_CORE_ACTIVE);
			core->hang_mask = 0;
		}
	}
	mutex_unlock(&core->lock);
	pm_runtime_put_sync(core->dev);

	return size;
}

static int vpu_dbg_core_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, vpu_dbg_core, inode->i_private);
}

static int vpu_dbg_fwlog_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, vpu_dbg_fwlog, inode->i_private);
}

static const struct file_operations vpu_dbg_inst_fops = {
	.owner = THIS_MODULE,
	.open = vpu_dbg_inst_open,
	.release = single_release,
	.read = seq_read,
	.write = vpu_dbg_inst_write,
};

static const struct file_operations vpu_dbg_core_fops = {
	.owner = THIS_MODULE,
	.open = vpu_dbg_core_open,
	.release = single_release,
	.read = seq_read,
	.write = vpu_dbg_core_write,
};

static const struct file_operations vpu_dbg_fwlog_fops = {
	.owner = THIS_MODULE,
	.open = vpu_dbg_fwlog_open,
	.release = single_release,
	.read = seq_read,
};

int vpu_inst_create_dbgfs_file(struct vpu_inst *inst)
{
	struct vpu_dev *vpu;
	char name[64];

	if (!inst || !inst->core || !inst->core->vpu)
		return -EINVAL;

	vpu = inst->core->vpu;
	if (!vpu->debugfs)
		return -EINVAL;

	if (inst->debugfs)
		return 0;

	scnprintf(name, sizeof(name), "instance.%d.%d", inst->core->id, inst->id);
	inst->debugfs = debugfs_create_file((const char *)name,
					    VERIFY_OCTAL_PERMISSIONS(0644),
					    vpu->debugfs,
					    inst,
					    &vpu_dbg_inst_fops);

	return 0;
}

int vpu_inst_remove_dbgfs_file(struct vpu_inst *inst)
{
	if (!inst)
		return 0;

	debugfs_remove(inst->debugfs);
	inst->debugfs = NULL;

	return 0;
}

int vpu_core_create_dbgfs_file(struct vpu_core *core)
{
	struct vpu_dev *vpu;
	char name[64];

	if (!core || !core->vpu)
		return -EINVAL;

	vpu = core->vpu;
	if (!vpu->debugfs)
		return -EINVAL;

	if (!core->debugfs) {
		scnprintf(name, sizeof(name), "core.%d", core->id);
		core->debugfs = debugfs_create_file((const char *)name,
						    VERIFY_OCTAL_PERMISSIONS(0644),
						    vpu->debugfs,
						    core,
						    &vpu_dbg_core_fops);
	}
	if (!core->debugfs_fwlog) {
		scnprintf(name, sizeof(name), "fwlog.%d", core->id);
		core->debugfs_fwlog = debugfs_create_file((const char *)name,
							  VERIFY_OCTAL_PERMISSIONS(0444),
							  vpu->debugfs,
							  core,
							  &vpu_dbg_fwlog_fops);
	}

	return 0;
}

int vpu_core_remove_dbgfs_file(struct vpu_core *core)
{
	if (!core)
		return 0;
	debugfs_remove(core->debugfs);
	core->debugfs = NULL;
	debugfs_remove(core->debugfs_fwlog);
	core->debugfs_fwlog = NULL;

	return 0;
}

void vpu_inst_record_flow(struct vpu_inst *inst, u32 flow)
{
	if (!inst)
		return;

	inst->flows[inst->flow_idx] = flow;
	inst->flow_idx = (inst->flow_idx + 1) % (ARRAY_SIZE(inst->flows));
}
