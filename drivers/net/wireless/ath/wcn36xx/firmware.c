// SPDX-License-Identifier: GPL-2.0-only

#include "wcn36xx.h"
#include "firmware.h"

void wcn36xx_firmware_set_feat_caps(u32 *bitmap,
				    enum wcn36xx_firmware_feat_caps cap)
{
	int arr_idx, bit_idx;

	if (cap < 0 || cap > 127) {
		wcn36xx_warn("error cap idx %d\n", cap);
		return;
	}

	arr_idx = cap / 32;
	bit_idx = cap % 32;
	bitmap[arr_idx] |= (1 << bit_idx);
}

int wcn36xx_firmware_get_feat_caps(u32 *bitmap,
				   enum wcn36xx_firmware_feat_caps cap)
{
	int arr_idx, bit_idx;

	if (cap < 0 || cap > 127) {
		wcn36xx_warn("error cap idx %d\n", cap);
		return -EINVAL;
	}

	arr_idx = cap / 32;
	bit_idx = cap % 32;

	return (bitmap[arr_idx] & (1 << bit_idx)) ? 1 : 0;
}

void wcn36xx_firmware_clear_feat_caps(u32 *bitmap,
				      enum wcn36xx_firmware_feat_caps cap)
{
	int arr_idx, bit_idx;

	if (cap < 0 || cap > 127) {
		wcn36xx_warn("error cap idx %d\n", cap);
		return;
	}

	arr_idx = cap / 32;
	bit_idx = cap % 32;
	bitmap[arr_idx] &= ~(1 << bit_idx);
}
