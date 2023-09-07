// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "vpu.h"
#include "vpu_defs.h"
#include "vpu_core.h"
#include "vpu_rpc.h"
#include "vpu_helpers.h"

int vpu_helper_find_in_array_u8(const u8 *array, u32 size, u32 x)
{
	int i;

	for (i = 0; i < size; i++) {
		if (array[i] == x)
			return i;
	}

	return 0;
}

bool vpu_helper_check_type(struct vpu_inst *inst, u32 type)
{
	const struct vpu_format *pfmt;

	for (pfmt = inst->formats; pfmt->pixfmt; pfmt++) {
		if (!vpu_iface_check_format(inst, pfmt->pixfmt))
			continue;
		if (pfmt->type == type)
			return true;
	}

	return false;
}

const struct vpu_format *vpu_helper_find_format(struct vpu_inst *inst, u32 type, u32 pixelfmt)
{
	const struct vpu_format *pfmt;

	if (!inst || !inst->formats)
		return NULL;

	if (!vpu_iface_check_format(inst, pixelfmt))
		return NULL;

	for (pfmt = inst->formats; pfmt->pixfmt; pfmt++) {
		if (pfmt->pixfmt == pixelfmt && (!type || type == pfmt->type))
			return pfmt;
	}

	return NULL;
}

const struct vpu_format *vpu_helper_find_sibling(struct vpu_inst *inst, u32 type, u32 pixelfmt)
{
	const struct vpu_format *fmt;
	const struct vpu_format *sibling;

	fmt = vpu_helper_find_format(inst, type, pixelfmt);
	if (!fmt || !fmt->sibling)
		return NULL;

	sibling = vpu_helper_find_format(inst, type, fmt->sibling);
	if (!sibling || sibling->sibling != fmt->pixfmt ||
	    sibling->comp_planes != fmt->comp_planes)
		return NULL;

	return sibling;
}

bool vpu_helper_match_format(struct vpu_inst *inst, u32 type, u32 fmta, u32 fmtb)
{
	const struct vpu_format *sibling;

	if (fmta == fmtb)
		return true;

	sibling = vpu_helper_find_sibling(inst, type, fmta);
	if (sibling && sibling->pixfmt == fmtb)
		return true;
	return false;
}

const struct vpu_format *vpu_helper_enum_format(struct vpu_inst *inst, u32 type, int index)
{
	const struct vpu_format *pfmt;
	int i = 0;

	if (!inst || !inst->formats)
		return NULL;

	for (pfmt = inst->formats; pfmt->pixfmt; pfmt++) {
		if (!vpu_iface_check_format(inst, pfmt->pixfmt))
			continue;

		if (pfmt->type == type) {
			if (index == i)
				return pfmt;
			i++;
		}
	}

	return NULL;
}

u32 vpu_helper_valid_frame_width(struct vpu_inst *inst, u32 width)
{
	const struct vpu_core_resources *res;

	if (!inst)
		return width;

	res = vpu_get_resource(inst);
	if (!res)
		return width;
	if (res->max_width)
		width = clamp(width, res->min_width, res->max_width);
	if (res->step_width)
		width = ALIGN(width, res->step_width);

	return width;
}

u32 vpu_helper_valid_frame_height(struct vpu_inst *inst, u32 height)
{
	const struct vpu_core_resources *res;

	if (!inst)
		return height;

	res = vpu_get_resource(inst);
	if (!res)
		return height;
	if (res->max_height)
		height = clamp(height, res->min_height, res->max_height);
	if (res->step_height)
		height = ALIGN(height, res->step_height);

	return height;
}

static u32 get_nv12_plane_size(u32 width, u32 height, int plane_no,
			       u32 stride, u32 interlaced, u32 *pbl)
{
	u32 bytesperline;
	u32 size = 0;

	bytesperline = width;
	if (pbl)
		bytesperline = max(bytesperline, *pbl);
	bytesperline = ALIGN(bytesperline, stride);
	height = ALIGN(height, 2);
	if (plane_no == 0)
		size = bytesperline * height;
	else if (plane_no == 1)
		size = bytesperline * height >> 1;
	if (pbl)
		*pbl = bytesperline;

	return size;
}

static u32 get_tiled_8l128_plane_size(u32 fmt, u32 width, u32 height, int plane_no,
				      u32 stride, u32 interlaced, u32 *pbl)
{
	u32 ws = 3;
	u32 hs = 7;
	u32 bitdepth = 8;
	u32 bytesperline;
	u32 size = 0;

	if (interlaced)
		hs++;
	if (fmt == V4L2_PIX_FMT_NV12M_10BE_8L128 || fmt == V4L2_PIX_FMT_NV12_10BE_8L128)
		bitdepth = 10;
	bytesperline = DIV_ROUND_UP(width * bitdepth, BITS_PER_BYTE);
	if (pbl)
		bytesperline = max(bytesperline, *pbl);
	bytesperline = ALIGN(bytesperline, 1 << ws);
	bytesperline = ALIGN(bytesperline, stride);
	height = ALIGN(height, 1 << hs);
	if (plane_no == 0)
		size = bytesperline * height;
	else if (plane_no == 1)
		size = (bytesperline * ALIGN(height, 1 << (hs + 1))) >> 1;
	if (pbl)
		*pbl = bytesperline;

	return size;
}

static u32 get_default_plane_size(u32 width, u32 height, int plane_no,
				  u32 stride, u32 interlaced, u32 *pbl)
{
	u32 bytesperline;
	u32 size = 0;

	bytesperline = width;
	if (pbl)
		bytesperline = max(bytesperline, *pbl);
	bytesperline = ALIGN(bytesperline, stride);
	if (plane_no == 0)
		size = bytesperline * height;
	if (pbl)
		*pbl = bytesperline;

	return size;
}

u32 vpu_helper_get_plane_size(u32 fmt, u32 w, u32 h, int plane_no,
			      u32 stride, u32 interlaced, u32 *pbl)
{
	switch (fmt) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12M:
		return get_nv12_plane_size(w, h, plane_no, stride, interlaced, pbl);
	case V4L2_PIX_FMT_NV12_8L128:
	case V4L2_PIX_FMT_NV12M_8L128:
	case V4L2_PIX_FMT_NV12_10BE_8L128:
	case V4L2_PIX_FMT_NV12M_10BE_8L128:
		return get_tiled_8l128_plane_size(fmt, w, h, plane_no, stride, interlaced, pbl);
	default:
		return get_default_plane_size(w, h, plane_no, stride, interlaced, pbl);
	}
}

int vpu_helper_copy_from_stream_buffer(struct vpu_buffer *stream_buffer,
				       u32 *rptr, u32 size, void *dst)
{
	u32 offset;
	u32 start;
	u32 end;
	void *virt;

	if (!stream_buffer || !rptr || !dst)
		return -EINVAL;

	if (!size)
		return 0;

	offset = *rptr;
	start = stream_buffer->phys;
	end = start + stream_buffer->length;
	virt = stream_buffer->virt;

	if (offset < start || offset > end)
		return -EINVAL;

	if (offset + size <= end) {
		memcpy(dst, virt + (offset - start), size);
	} else {
		memcpy(dst, virt + (offset - start), end - offset);
		memcpy(dst + end - offset, virt, size + offset - end);
	}

	*rptr = vpu_helper_step_walk(stream_buffer, offset, size);

	return 0;
}

int vpu_helper_copy_to_stream_buffer(struct vpu_buffer *stream_buffer,
				     u32 *wptr, u32 size, void *src)
{
	u32 offset;
	u32 start;
	u32 end;
	void *virt;

	if (!stream_buffer || !wptr || !src)
		return -EINVAL;

	if (!size)
		return 0;

	offset = *wptr;
	start = stream_buffer->phys;
	end = start + stream_buffer->length;
	virt = stream_buffer->virt;
	if (offset < start || offset > end)
		return -EINVAL;

	if (offset + size <= end) {
		memcpy(virt + (offset - start), src, size);
	} else {
		memcpy(virt + (offset - start), src, end - offset);
		memcpy(virt, src + end - offset, size + offset - end);
	}

	*wptr = vpu_helper_step_walk(stream_buffer, offset, size);

	return 0;
}

int vpu_helper_memset_stream_buffer(struct vpu_buffer *stream_buffer,
				    u32 *wptr, u8 val, u32 size)
{
	u32 offset;
	u32 start;
	u32 end;
	void *virt;

	if (!stream_buffer || !wptr)
		return -EINVAL;

	if (!size)
		return 0;

	offset = *wptr;
	start = stream_buffer->phys;
	end = start + stream_buffer->length;
	virt = stream_buffer->virt;
	if (offset < start || offset > end)
		return -EINVAL;

	if (offset + size <= end) {
		memset(virt + (offset - start), val, size);
	} else {
		memset(virt + (offset - start), val, end - offset);
		memset(virt, val, size + offset - end);
	}

	offset += size;
	if (offset >= end)
		offset -= stream_buffer->length;

	*wptr = offset;

	return 0;
}

u32 vpu_helper_get_free_space(struct vpu_inst *inst)
{
	struct vpu_rpc_buffer_desc desc;

	if (vpu_iface_get_stream_buffer_desc(inst, &desc))
		return 0;

	if (desc.rptr > desc.wptr)
		return desc.rptr - desc.wptr;
	else if (desc.rptr < desc.wptr)
		return (desc.end - desc.start + desc.rptr - desc.wptr);
	else
		return desc.end - desc.start;
}

u32 vpu_helper_get_used_space(struct vpu_inst *inst)
{
	struct vpu_rpc_buffer_desc desc;

	if (vpu_iface_get_stream_buffer_desc(inst, &desc))
		return 0;

	if (desc.wptr > desc.rptr)
		return desc.wptr - desc.rptr;
	else if (desc.wptr < desc.rptr)
		return (desc.end - desc.start + desc.wptr - desc.rptr);
	else
		return 0;
}

int vpu_helper_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vpu_inst *inst = ctrl_to_inst(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		ctrl->val = inst->min_buffer_cap;
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:
		ctrl->val = inst->min_buffer_out;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int vpu_helper_find_startcode(struct vpu_buffer *stream_buffer,
			      u32 pixelformat, u32 offset, u32 bytesused)
{
	u32 start_code;
	int start_code_size;
	u32 val = 0;
	int i;
	int ret = -EINVAL;

	if (!stream_buffer || !stream_buffer->virt)
		return -EINVAL;

	switch (pixelformat) {
	case V4L2_PIX_FMT_H264:
		start_code_size = 4;
		start_code = 0x00000001;
		break;
	default:
		return 0;
	}

	for (i = 0; i < bytesused; i++) {
		val = (val << 8) | vpu_helper_read_byte(stream_buffer, offset + i);
		if (i < start_code_size - 1)
			continue;
		if (val == start_code) {
			ret = i + 1 - start_code_size;
			break;
		}
	}

	return ret;
}

int vpu_find_dst_by_src(struct vpu_pair *pairs, u32 cnt, u32 src)
{
	u32 i;

	if (!pairs || !cnt)
		return -EINVAL;

	for (i = 0; i < cnt; i++) {
		if (pairs[i].src == src)
			return pairs[i].dst;
	}

	return -EINVAL;
}

int vpu_find_src_by_dst(struct vpu_pair *pairs, u32 cnt, u32 dst)
{
	u32 i;

	if (!pairs || !cnt)
		return -EINVAL;

	for (i = 0; i < cnt; i++) {
		if (pairs[i].dst == dst)
			return pairs[i].src;
	}

	return -EINVAL;
}

const char *vpu_id_name(u32 id)
{
	switch (id) {
	case VPU_CMD_ID_NOOP: return "noop";
	case VPU_CMD_ID_CONFIGURE_CODEC: return "configure codec";
	case VPU_CMD_ID_START: return "start";
	case VPU_CMD_ID_STOP: return "stop";
	case VPU_CMD_ID_ABORT: return "abort";
	case VPU_CMD_ID_RST_BUF: return "reset buf";
	case VPU_CMD_ID_SNAPSHOT: return "snapshot";
	case VPU_CMD_ID_FIRM_RESET: return "reset firmware";
	case VPU_CMD_ID_UPDATE_PARAMETER: return "update parameter";
	case VPU_CMD_ID_FRAME_ENCODE: return "encode frame";
	case VPU_CMD_ID_SKIP: return "skip";
	case VPU_CMD_ID_FS_ALLOC: return "alloc fb";
	case VPU_CMD_ID_FS_RELEASE: return "release fb";
	case VPU_CMD_ID_TIMESTAMP: return "timestamp";
	case VPU_CMD_ID_DEBUG: return "debug";
	case VPU_MSG_ID_RESET_DONE: return "reset done";
	case VPU_MSG_ID_START_DONE: return "start done";
	case VPU_MSG_ID_STOP_DONE: return "stop done";
	case VPU_MSG_ID_ABORT_DONE: return "abort done";
	case VPU_MSG_ID_BUF_RST: return "buf reset done";
	case VPU_MSG_ID_MEM_REQUEST: return "mem request";
	case VPU_MSG_ID_PARAM_UPD_DONE: return "param upd done";
	case VPU_MSG_ID_FRAME_INPUT_DONE: return "frame input done";
	case VPU_MSG_ID_ENC_DONE: return "encode done";
	case VPU_MSG_ID_DEC_DONE: return "frame display";
	case VPU_MSG_ID_FRAME_REQ: return "fb request";
	case VPU_MSG_ID_FRAME_RELEASE: return "fb release";
	case VPU_MSG_ID_SEQ_HDR_FOUND: return "seq hdr found";
	case VPU_MSG_ID_RES_CHANGE: return "resolution change";
	case VPU_MSG_ID_PIC_HDR_FOUND: return "pic hdr found";
	case VPU_MSG_ID_PIC_DECODED: return "picture decoded";
	case VPU_MSG_ID_PIC_EOS: return "eos";
	case VPU_MSG_ID_FIFO_LOW: return "fifo low";
	case VPU_MSG_ID_BS_ERROR: return "bs error";
	case VPU_MSG_ID_UNSUPPORTED: return "unsupported";
	case VPU_MSG_ID_FIRMWARE_XCPT: return "exception";
	case VPU_MSG_ID_PIC_SKIPPED: return "skipped";
	case VPU_MSG_ID_DBG_MSG: return "debug msg";
	}
	return "<unknown>";
}

const char *vpu_codec_state_name(enum vpu_codec_state state)
{
	switch (state) {
	case VPU_CODEC_STATE_DEINIT: return "initialization";
	case VPU_CODEC_STATE_CONFIGURED: return "configured";
	case VPU_CODEC_STATE_START: return "start";
	case VPU_CODEC_STATE_STARTED: return "started";
	case VPU_CODEC_STATE_ACTIVE: return "active";
	case VPU_CODEC_STATE_SEEK: return "seek";
	case VPU_CODEC_STATE_STOP: return "stop";
	case VPU_CODEC_STATE_DRAIN: return "drain";
	case VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE: return "resolution change";
	}
	return "<unknown>";
}
