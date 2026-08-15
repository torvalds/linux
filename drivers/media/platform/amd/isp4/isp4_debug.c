// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#include "isp4.h"
#include "isp4_debug.h"
#include "isp4_hw_reg.h"
#include "isp4_interface.h"

#define ISP4DBG_FW_LOG_RINGBUF_SIZE (2 * 1024 * 1024)
#define ISP4DBG_MACRO_2_STR(X) #X
#define ISP4DBG_ONE_TIME_LOG_LEN 510

#ifdef CONFIG_DEBUG_FS

void isp_debugfs_create(struct isp4_device *isp_dev)
{
	isp_dev->isp_subdev.debugfs_dir = debugfs_create_dir("amd_isp4", NULL);
	debugfs_create_bool("fw_log_enable", 0644,
			    isp_dev->isp_subdev.debugfs_dir,
			    &isp_dev->isp_subdev.enable_fw_log);
	isp_dev->isp_subdev.fw_log_output =
		devm_kzalloc(isp_dev->isp_subdev.dev,
			     ISP4DBG_FW_LOG_RINGBUF_SIZE + 32,
			     GFP_KERNEL);
}

void isp_debugfs_remove(struct isp4_device *isp_dev)
{
	debugfs_remove_recursive(isp_dev->isp_subdev.debugfs_dir);
	isp_dev->isp_subdev.debugfs_dir = NULL;
}

static u32 isp_fw_fill_rb_log(struct isp4_subdev *isp, void *sys, u32 rb_size)
{
	struct isp4_interface *ispif = &isp->ispif;
	char *buf = isp->fw_log_output;
	struct device *dev = isp->dev;
	u32 rd_ptr, wr_ptr;
	u32 total_cnt = 0;
	u32 offset = 0;
	u32 cnt;

	if (!sys || !rb_size)
		return 0;

	guard(mutex)(&ispif->isp4if_mutex);

	rd_ptr = isp4hw_rreg(isp->mmio, ISP_LOG_RB_RPTR0);
	wr_ptr = isp4hw_rreg(isp->mmio, ISP_LOG_RB_WPTR0);

	do {
		if (wr_ptr > rd_ptr)
			cnt = wr_ptr - rd_ptr;
		else if (wr_ptr < rd_ptr)
			cnt = rb_size - rd_ptr;
		else
			goto quit;

		if (cnt > rb_size) {
			dev_err(dev, "fail bad fw log size %u\n", cnt);
			goto quit;
		}

		memcpy(buf + offset, sys + rd_ptr, cnt);

		offset += cnt;
		total_cnt += cnt;
		rd_ptr = (rd_ptr + cnt) % rb_size;
	} while (rd_ptr < wr_ptr);

	isp4hw_wreg(isp->mmio, ISP_LOG_RB_RPTR0, rd_ptr);

quit:
	return total_cnt;
}

void isp_fw_log_print(struct isp4_subdev *isp)
{
	struct isp4_interface *ispif = &isp->ispif;
	char *fw_log_buf = isp->fw_log_output;
	u32 cnt;

	if (!isp->enable_fw_log || !fw_log_buf)
		return;

	cnt = isp_fw_fill_rb_log(isp, ispif->fw_log_buf->sys_addr,
				 ispif->fw_log_buf->mem_size);

	if (cnt) {
		char temp_ch;
		char *str;
		char *end;
		/* line end */
		char *le;

		str = (char *)fw_log_buf;
		end = ((char *)fw_log_buf + cnt);
		fw_log_buf[cnt] = 0;

		while (str < end) {
			le = strchr(str, 0x0A);
			if ((le && str + ISP4DBG_ONE_TIME_LOG_LEN >= le) ||
			    (!le && str + ISP4DBG_ONE_TIME_LOG_LEN >= end)) {
				if (le)
					*le = 0;

				if (*str != '\0')
					dev_dbg(isp->dev, "%s", str);

				if (le) {
					*le = 0x0A;
					str = le + 1;
				} else {
					break;
				}
			} else {
				u32 tmp_len = ISP4DBG_ONE_TIME_LOG_LEN;

				temp_ch = str[tmp_len];
				str[tmp_len] = 0;
				dev_dbg(isp->dev, "%s", str);
				str[tmp_len] = temp_ch;
				str = &str[tmp_len];
			}
		}
	}
}
#endif

char *isp4dbg_get_buf_src_str(u32 src)
{
	switch (src) {
	case ISP4FW_BUFFER_SOURCE_STREAM:
		return ISP4DBG_MACRO_2_STR(ISP4FW_BUFFER_SOURCE_STREAM);
	default:
		return "Unknown buf source";
	}
}

char *isp4dbg_get_buf_done_str(u32 status)
{
	switch (status) {
	case ISP4FW_BUFFER_STATUS_INVALID:
		return ISP4DBG_MACRO_2_STR(ISP4FW_BUFFER_STATUS_INVALID);
	case ISP4FW_BUFFER_STATUS_SKIPPED:
		return ISP4DBG_MACRO_2_STR(ISP4FW_BUFFER_STATUS_SKIPPED);
	case ISP4FW_BUFFER_STATUS_EXIST:
		return ISP4DBG_MACRO_2_STR(ISP4FW_BUFFER_STATUS_EXIST);
	case ISP4FW_BUFFER_STATUS_DONE:
		return ISP4DBG_MACRO_2_STR(ISP4FW_BUFFER_STATUS_DONE);
	case ISP4FW_BUFFER_STATUS_LACK:
		return ISP4DBG_MACRO_2_STR(ISP4FW_BUFFER_STATUS_LACK);
	case ISP4FW_BUFFER_STATUS_DIRTY:
		return ISP4DBG_MACRO_2_STR(ISP4FW_BUFFER_STATUS_DIRTY);
	case ISP4FW_BUFFER_STATUS_MAX:
		return ISP4DBG_MACRO_2_STR(ISP4FW_BUFFER_STATUS_MAX);
	default:
		return "Unknown Buf Done Status";
	}
}

char *isp4dbg_get_img_fmt_str(int fmt /* enum isp4fw_image_format * */)
{
	switch (fmt) {
	case ISP4FW_IMAGE_FORMAT_NV12:
		return "NV12";
	case ISP4FW_IMAGE_FORMAT_YUV422INTERLEAVED:
		return "YUV422INTERLEAVED";
	default:
		return "unknown fmt";
	}
}

void isp4dbg_show_bufmeta_info(struct device *dev, char *pre,
			       void *in, void *orig_buf)
{
	struct isp4fw_buffer_meta_info *p;
	struct isp4if_img_buf_info *orig;

	if (!in)
		return;

	if (!pre)
		pre = "";

	p = in;
	orig = orig_buf;

	dev_dbg(dev, "%s(%s) en:%d,stat:%s(%u),src:%s\n", pre,
		isp4dbg_get_img_fmt_str(p->image_prop.image_format),
		p->enabled, isp4dbg_get_buf_done_str(p->status), p->status,
		isp4dbg_get_buf_src_str(p->source));

	dev_dbg(dev, "%p,0x%llx(%u) %p,0x%llx(%u) %p,0x%llx(%u)\n",
		orig->planes[0].sys_addr, orig->planes[0].mc_addr,
		orig->planes[0].len, orig->planes[1].sys_addr,
		orig->planes[1].mc_addr, orig->planes[1].len,
		orig->planes[2].sys_addr, orig->planes[2].mc_addr,
		orig->planes[2].len);
}

char *isp4dbg_get_buf_type(u32 type)
{
	/* enum isp4fw_buffer_type */
	switch (type) {
	case ISP4FW_BUFFER_TYPE_PREVIEW:
		return ISP4DBG_MACRO_2_STR(ISP4FW_BUFFER_TYPE_PREVIEW);
	case ISP4FW_BUFFER_TYPE_META_INFO:
		return ISP4DBG_MACRO_2_STR(ISP4FW_BUFFER_TYPE_META_INFO);
	case ISP4FW_BUFFER_TYPE_MEM_POOL:
		return ISP4DBG_MACRO_2_STR(ISP4FW_BUFFER_TYPE_MEM_POOL);
	default:
		return "unknown type";
	}
}

char *isp4dbg_get_cmd_str(u32 cmd)
{
	switch (cmd) {
	case ISP4FW_CMD_ID_START_STREAM:
		return ISP4DBG_MACRO_2_STR(ISP4FW_CMD_ID_START_STREAM);
	case ISP4FW_CMD_ID_STOP_STREAM:
		return ISP4DBG_MACRO_2_STR(ISP4FW_CMD_ID_STOP_STREAM);
	case ISP4FW_CMD_ID_SEND_BUFFER:
		return ISP4DBG_MACRO_2_STR(ISP4FW_CMD_ID_SEND_BUFFER);
	case ISP4FW_CMD_ID_SET_STREAM_CONFIG:
		return ISP4DBG_MACRO_2_STR(ISP4FW_CMD_ID_SET_STREAM_CONFIG);
	case ISP4FW_CMD_ID_SET_OUT_CHAN_PROP:
		return ISP4DBG_MACRO_2_STR(ISP4FW_CMD_ID_SET_OUT_CHAN_PROP);
	case ISP4FW_CMD_ID_ENABLE_OUT_CHAN:
		return ISP4DBG_MACRO_2_STR(ISP4FW_CMD_ID_ENABLE_OUT_CHAN);
	default:
		return "unknown cmd";
	}
}

char *isp4dbg_get_resp_str(u32 cmd)
{
	switch (cmd) {
	case ISP4FW_RESP_ID_CMD_DONE:
		return ISP4DBG_MACRO_2_STR(ISP4FW_RESP_ID_CMD_DONE);
	case ISP4FW_RESP_ID_NOTI_FRAME_DONE:
		return ISP4DBG_MACRO_2_STR(ISP4FW_RESP_ID_NOTI_FRAME_DONE);
	default:
		return "unknown respid";
	}
}

char *isp4dbg_get_if_stream_str(u32 stream /* enum fw_cmd_resp_stream_id */)
{
	switch (stream) {
	case ISP4IF_STREAM_ID_GLOBAL:
		return "STREAM_GLOBAL";
	case ISP4IF_STREAM_ID_1:
		return "STREAM1";
	default:
		return "unknown streamID";
	}
}

char *isp4dbg_get_out_ch_str(int ch /* enum isp4fw_pipe_out_ch */)
{
	switch ((enum isp4fw_pipe_out_ch)ch) {
	case ISP4FW_ISP_PIPE_OUT_CH_PREVIEW:
		return "prev";
	default:
		return "unknown channel";
	}
}
