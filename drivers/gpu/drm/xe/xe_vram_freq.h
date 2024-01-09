/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_VRAM_FREQ_H_
#define _XE_VRAM_FREQ_H_

struct xe_tile;

void xe_vram_freq_sysfs_init(struct xe_tile *tile);

#endif /* _XE_VRAM_FREQ_H_ */
