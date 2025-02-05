/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_ERR_H
#define __IA_CSS_ERR_H

/* @file
 * This file contains possible return values for most
 * functions in the CSS-API.
 */

/* FW warnings. This enum contains a value for each warning that
 * the SP FW could indicate potential performance issue
 */
enum ia_css_fw_warning {
	IA_CSS_FW_WARNING_NONE,
	IA_CSS_FW_WARNING_ISYS_QUEUE_FULL, /* < CSS system delayed because of insufficient space in the ISys queue.
		This warning can be avoided by de-queuing ISYS buffers more timely. */
	IA_CSS_FW_WARNING_PSYS_QUEUE_FULL, /* < CSS system delayed because of insufficient space in the PSys queue.
		This warning can be avoided by de-queuing PSYS buffers more timely. */
	IA_CSS_FW_WARNING_CIRCBUF_ALL_LOCKED, /* < CSS system delayed because of insufficient available buffers.
		This warning can be avoided by unlocking locked frame-buffers more timely. */
	IA_CSS_FW_WARNING_EXP_ID_LOCKED, /* < Exposure ID skipped because the frame associated to it was still locked.
		This warning can be avoided by unlocking locked frame-buffers more timely. */
	IA_CSS_FW_WARNING_TAG_EXP_ID_FAILED, /* < Exposure ID cannot be found on the circular buffer.
		This warning can be avoided by unlocking locked frame-buffers more timely. */
	IA_CSS_FW_WARNING_FRAME_PARAM_MISMATCH, /* < Frame and param pair mismatched in tagger.
		This warning can be avoided by providing a param set for each frame. */
};

#endif /* __IA_CSS_ERR_H */
