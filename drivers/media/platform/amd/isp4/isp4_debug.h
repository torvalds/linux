/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#ifndef _ISP4_DEBUG_H_
#define _ISP4_DEBUG_H_

#include <linux/dev_printk.h>
#include <linux/printk.h>

#include "isp4_subdev.h"

#ifdef CONFIG_DEBUG_FS
struct isp4_device;

void isp_debugfs_create(struct isp4_device *isp_dev);
void isp_debugfs_remove(struct isp4_device *isp_dev);
void isp_fw_log_print(struct isp4_subdev *isp);

#else

/* to avoid checkpatch warning */
#define isp_debugfs_create(cam) ((void)(cam))
#define isp_debugfs_remove(cam) ((void)(cam))
#define isp_fw_log_print(isp) ((void)(isp))

#endif /* CONFIG_DEBUG_FS */

void isp4dbg_show_bufmeta_info(struct device *dev, char *pre, void *p,
			       void *orig_buf /* struct sys_img_buf_handle */);
char *isp4dbg_get_img_fmt_str(int fmt /* enum _image_format_t */);
char *isp4dbg_get_out_ch_str(int ch /* enum _isp_pipe_out_ch_t */);
char *isp4dbg_get_cmd_str(u32 cmd);
char *isp4dbg_get_buf_type(u32 type);/* enum _buffer_type_t */
char *isp4dbg_get_resp_str(u32 resp);
char *isp4dbg_get_buf_src_str(u32 src);
char *isp4dbg_get_buf_done_str(u32 status);
char *isp4dbg_get_if_stream_str(u32 stream);

#endif /* _ISP4_DEBUG_H_ */
