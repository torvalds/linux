/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"
#include "include/signal_types.h"

bool dc_is_hdmi_signal(enum signal_type signal)
{
	return (signal == SIGNAL_TYPE_HDMI_TYPE_A);
}

bool dc_is_dp_sst_signal(enum signal_type signal)
{
	return (signal == SIGNAL_TYPE_DISPLAY_PORT ||
		signal == SIGNAL_TYPE_EDP);
}

bool dc_is_dp_signal(enum signal_type signal)
{
	return (signal == SIGNAL_TYPE_DISPLAY_PORT ||
		signal == SIGNAL_TYPE_EDP ||
		signal == SIGNAL_TYPE_DISPLAY_PORT_MST);
}

bool dc_is_dp_external_signal(enum signal_type signal)
{
	return (signal == SIGNAL_TYPE_DISPLAY_PORT ||
		signal == SIGNAL_TYPE_DISPLAY_PORT_MST);
}

bool dc_is_analog_signal(enum signal_type signal)
{
	switch (signal) {
	case SIGNAL_TYPE_RGB:
		return true;
	break;
	default:
		return false;
	}
}

bool dc_is_embedded_signal(enum signal_type signal)
{
	return (signal == SIGNAL_TYPE_EDP || signal == SIGNAL_TYPE_LVDS);
}

bool dc_is_dvi_signal(enum signal_type signal)
{
	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		return true;
	break;
	default:
		return false;
	}
}

bool dc_is_dvi_single_link_signal(enum signal_type signal)
{
	return (signal == SIGNAL_TYPE_DVI_SINGLE_LINK);
}

bool dc_is_dual_link_signal(enum signal_type signal)
{
	return (signal == SIGNAL_TYPE_DVI_DUAL_LINK);
}

bool dc_is_audio_capable_signal(enum signal_type signal)
{
	return (signal == SIGNAL_TYPE_DISPLAY_PORT ||
		signal == SIGNAL_TYPE_DISPLAY_PORT_MST ||
		dc_is_hdmi_signal(signal) ||
		signal == SIGNAL_TYPE_WIRELESS);
}

/*
 * @brief
 * Returns whether the signal is compatible
 * with other digital encoder signal types.
 * This is true for DVI, LVDS, and HDMI signal types.
 */
bool dc_is_digital_encoder_compatible_signal(enum signal_type signal)
{
	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
	case SIGNAL_TYPE_LVDS:
		return true;
	default:
		return false;
	}
}
