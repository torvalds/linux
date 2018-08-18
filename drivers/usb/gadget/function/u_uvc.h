// SPDX-License-Identifier: GPL-2.0
/*
 * u_uvc.h
 *
 * Utility definitions for the uvc function
 *
 * Copyright (c) 2013-2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 */

#ifndef U_UVC_H
#define U_UVC_H

#include <linux/mutex.h>
#include <linux/usb/composite.h>
#include <linux/usb/video.h>

#define fi_to_f_uvc_opts(f)	container_of(f, struct f_uvc_opts, func_inst)

struct f_uvc_opts {
	struct usb_function_instance			func_inst;
	unsigned int					streaming_interval;
	unsigned int					streaming_maxpacket;
	unsigned int					streaming_maxburst;

	/*
	 * Control descriptors array pointers for full-/high-speed and
	 * super-speed. They point by default to the uvc_fs_control_cls and
	 * uvc_ss_control_cls arrays respectively. Legacy gadgets must
	 * override them in their gadget bind callback.
	 */
	const struct uvc_descriptor_header * const	*fs_control;
	const struct uvc_descriptor_header * const	*ss_control;

	/*
	 * Streaming descriptors array pointers for full-speed, high-speed and
	 * super-speed. They will point to the uvc_[fhs]s_streaming_cls arrays
	 * for configfs-based gadgets. Legacy gadgets must initialize them in
	 * their gadget bind callback.
	 */
	const struct uvc_descriptor_header * const	*fs_streaming;
	const struct uvc_descriptor_header * const	*hs_streaming;
	const struct uvc_descriptor_header * const	*ss_streaming;

	/* Default control descriptors for configfs-based gadgets. */
	struct uvc_camera_terminal_descriptor		uvc_camera_terminal;
	struct uvc_processing_unit_descriptor		uvc_processing;
	struct uvc_output_terminal_descriptor		uvc_output_terminal;
	struct uvc_color_matching_descriptor		uvc_color_matching;

	/*
	 * Control descriptors pointers arrays for full-/high-speed and
	 * super-speed. The first element is a configurable control header
	 * descriptor, the other elements point to the fixed default control
	 * descriptors. Used by configfs only, must not be touched by legacy
	 * gadgets.
	 */
	struct uvc_descriptor_header			*uvc_fs_control_cls[5];
	struct uvc_descriptor_header			*uvc_ss_control_cls[5];

	/*
	 * Streaming descriptors for full-speed, high-speed and super-speed.
	 * Used by configfs only, must not be touched by legacy gadgets. The
	 * arrays are allocated at runtime as the number of descriptors isn't
	 * known in advance.
	 */
	struct uvc_descriptor_header			**uvc_fs_streaming_cls;
	struct uvc_descriptor_header			**uvc_hs_streaming_cls;
	struct uvc_descriptor_header			**uvc_ss_streaming_cls;

	/*
	 * Read/write access to configfs attributes is handled by configfs.
	 *
	 * This lock protects the descriptors from concurrent access by
	 * read/write and symlink creation/removal.
	 */
	struct mutex			lock;
	int				refcnt;
};

#endif /* U_UVC_H */
