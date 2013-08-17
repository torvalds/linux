/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MACH_EXYNOS_BOARD_SMDK4X12_H
#define __MACH_EXYNOS_BOARD_SMDK4X12_H

#define SMDK4X12_REV_0_0        0x0
#define SMDK4X12_REV_0_1        0x1

void exynos4_smdk4x12_mmc_init(void);
void exynos4_smdk4x12_audio_init(void);
void exynos4_smdk4x12_display_init(void);
void exynos4_smdk4x12_usb_init(void);
void exynos4_smdk4x12_media_init(void);
void exynos4_smdk4x12_power_init(void);

int exynos4_smdk4x12_get_revision(void);

#endif
