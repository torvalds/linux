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

#ifndef __DAL_AUDIO_H__
#define __DAL_AUDIO_H__

#include "audio_types.h"

struct audio;

struct audio_funcs {

	bool (*endpoint_valid)(struct audio *audio);

	void (*hw_init)(struct audio *audio);

	void (*az_enable)(struct audio *audio);

	void (*az_disable)(struct audio *audio);

	void (*az_configure)(struct audio *audio,
		enum signal_type signal,
		const struct audio_crtc_info *crtc_info,
		const struct audio_info *audio_info);

	void (*wall_dto_setup)(struct audio *audio,
		enum signal_type signal,
		const struct audio_crtc_info *crtc_info,
		const struct audio_pll_info *pll_info);

	void (*destroy)(struct audio **audio);
};

struct audio {
	const struct audio_funcs *funcs;
	struct dc_context *ctx;
	unsigned int inst;
	bool enabled;
};

#endif  /* __DAL_AUDIO__ */
