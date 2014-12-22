/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 * Authors: Slava Grigorev <slava.grigorev@amd.com>
 */

#include <drm/drmP.h>
#include "radeon.h"

void r600_audio_enable(struct radeon_device *rdev, struct r600_audio_pin *pin,
		u8 enable_mask);
void dce6_audio_enable(struct radeon_device *rdev, struct r600_audio_pin *pin,
		u8 enable_mask);

static const u32 pin_offsets[7] =
{
	(0x5e00 - 0x5e00),
	(0x5e18 - 0x5e00),
	(0x5e30 - 0x5e00),
	(0x5e48 - 0x5e00),
	(0x5e60 - 0x5e00),
	(0x5e78 - 0x5e00),
	(0x5e90 - 0x5e00),
};

static int radeon_audio_chipset_supported(struct radeon_device *rdev)
{
	return ASIC_IS_DCE2(rdev) && !ASIC_IS_NODCE(rdev);
}

int radeon_audio_init(struct radeon_device *rdev)
{
	int i;

	if (!radeon_audio || !radeon_audio_chipset_supported(rdev))
		return 0;

	rdev->audio.enabled = true;

	if (ASIC_IS_DCE83(rdev))		/* KB: 2 streams, 3 endpoints */
		rdev->audio.num_pins = 3;
	else if (ASIC_IS_DCE81(rdev))	/* KV: 4 streams, 7 endpoints */
		rdev->audio.num_pins = 7;
	else if (ASIC_IS_DCE8(rdev))	/* BN/HW: 6 streams, 7 endpoints */
		rdev->audio.num_pins = 7;
	else if (ASIC_IS_DCE64(rdev))	/* OL: 2 streams, 2 endpoints */
		rdev->audio.num_pins = 2;
	else if (ASIC_IS_DCE61(rdev))	/* TN: 4 streams, 6 endpoints */
		rdev->audio.num_pins = 6;
	else if (ASIC_IS_DCE6(rdev))	/* SI: 6 streams, 6 endpoints */
		rdev->audio.num_pins = 6;
	else
		rdev->audio.num_pins = 1;

	for (i = 0; i < rdev->audio.num_pins; i++) {
		rdev->audio.pin[i].channels = -1;
		rdev->audio.pin[i].rate = -1;
		rdev->audio.pin[i].bits_per_sample = -1;
		rdev->audio.pin[i].status_bits = 0;
		rdev->audio.pin[i].category_code = 0;
		rdev->audio.pin[i].connected = false;
		rdev->audio.pin[i].offset = pin_offsets[i];
		rdev->audio.pin[i].id = i;
		/* disable audio.  it will be set up later */
		if (ASIC_IS_DCE6(rdev))
			dce6_audio_enable(rdev, &rdev->audio.pin[i], false);
		else
			r600_audio_enable(rdev, &rdev->audio.pin[i], false);
	}

	return 0;
}
