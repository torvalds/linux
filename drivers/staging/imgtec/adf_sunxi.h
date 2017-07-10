/* -*- mode: c; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* vi: set ts=8 sw=8 sts=8: */
/*************************************************************************/ /*!
@File           adf_sunxi.h
@Codingstyle    LinuxKernel
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef _ADF_SUNXI_
#define _ADF_SUNXI_

extern struct ion_device *idev;

#include <video/drv_display.h>
#define DISPLAY_INTERNAL 0
#define DISPLAY_HDMI 1
#define DISPLAY_EDP 2

struct setup_dispc_data {
	int			layer_num[3];
	disp_layer_info		layer_info[3][4];
	void			*hConfigData;
};

struct disp_composer_ops {
	int (*get_screen_width)(u32 screen_id);
	int (*get_screen_height)(u32 screen_id);
	int (*get_output_type)(u32 screen_id);
	int (*hdmi_enable)(u32 screen_id);
	int (*hdmi_disable)(u32 screen_id);
	int (*hdmi_set_mode)(u32 screen_id,  disp_tv_mode mode);
	int (*hdmi_get_mode)(u32 screen_id);
	int (*hdmi_check_support_mode)(u32 screen_id,  u8 mode);
	int (*is_support_scaler_layer)(unsigned int screen_id,
		unsigned int src_w, unsigned int src_h, unsigned int out_w,
		unsigned int out_h);
	int (*dispc_gralloc_queue)(struct setup_dispc_data *psDispcData);
	int (*set_retire_callback)(void (*retire_fn)(void));
	int (*vsync_enable)(u32 screen_id, bool enable);
	int (*vsync_callback)(void *user_data, void (*cb_fn)(void *user_data,
		u32 screen_id));
	int (*hotplug_enable)(u32 screen_id, bool enable);
	int (*hotplug_callback)(u32 screen_id, void *user_data,
		hdmi_hotplug_callback_function cb_fn);
	int (*hotplug_state)(u32 screen_id);

};
extern int disp_get_composer_ops(struct disp_composer_ops *ops);

#endif
