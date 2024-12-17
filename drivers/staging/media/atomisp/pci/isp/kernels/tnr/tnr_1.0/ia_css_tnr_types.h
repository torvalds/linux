/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_TNR_TYPES_H
#define __IA_CSS_TNR_TYPES_H

/* @file
* CSS-API header file for Temporal Noise Reduction (TNR) parameters.
*/

/* Temporal Noise Reduction (TNR) configuration.
 *
 *  When difference between current frame and previous frame is less than or
 *  equal to threshold, TNR works and current frame is mixed
 *  with previous frame.
 *  When difference between current frame and previous frame is greater
 *  than threshold, we judge motion is detected. Then, TNR does not work and
 *  current frame is outputted as it is.
 *  Therefore, when threshold_y and threshold_uv are set as 0, TNR can be disabled.
 *
 *  ISP block: TNR1
 *  ISP1: TNR1 is used.
 *  ISP2: TNR1 is used.
 */

struct ia_css_tnr_config {
	ia_css_u0_16 gain; /** Interpolation ratio of current frame
				and previous frame.
				gain=0.0 -> previous frame is outputted.
				gain=1.0 -> current frame is outputted.
				u0.16, [0,65535],
			default 32768(0.5), ineffective 65535(almost 1.0) */
	ia_css_u0_16 threshold_y; /** Threshold to enable interpolation of Y.
				If difference between current frame and
				previous frame is greater than threshold_y,
				TNR for Y is disabled.
				u0.16, [0,65535], default/ineffective 0 */
	ia_css_u0_16 threshold_uv; /** Threshold to enable interpolation of
				U/V.
				If difference between current frame and
				previous frame is greater than threshold_uv,
				TNR for UV is disabled.
				u0.16, [0,65535], default/ineffective 0 */
};

#endif /* __IA_CSS_TNR_TYPES_H */
