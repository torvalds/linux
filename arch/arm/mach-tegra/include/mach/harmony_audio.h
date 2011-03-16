/*
 * arch/arm/mach-tegra/include/mach/harmony_audio.h
 *
 * Copyright 2011 NVIDIA, Inc.
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

struct harmony_audio_platform_data {
	int gpio_spkr_en;
	int gpio_hp_det;
	int gpio_int_mic_en;
	int gpio_ext_mic_en;
};
