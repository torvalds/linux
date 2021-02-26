// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#ifndef EPD_LUT_H
#define EPD_LUT_H

enum epd_lut_type {
	WF_TYPE_RESET	= 1,
	WF_TYPE_GRAY16	= 2,
	WF_TYPE_GRAY4	= 3,
	WF_TYPE_GRAY2	= 4,
	WF_TYPE_AUTO	= 5,
	WF_TYPE_A2	= 6,
	WF_TYPE_GC16	= 7,
	WF_TYPE_GL16	= 8,
	WF_TYPE_GLR16	= 9,
	WF_TYPE_GLD16	= 10,
	WF_TYPE_GCC16	= 11,
	WF_TYPE_MAX	= 12,
};

enum pvi_wf_mode {
	PVI_WF_RESET	= 0,
	PVI_WF_DU	= 1,
	PVI_WF_DU4	= 2,
	PVI_WF_GC16	= 3,
	PVI_WF_GL16	= 4,
	PVI_WF_GLR16	= 5,
	PVI_WF_GLD16	= 6,
	PVI_WF_A2	= 7,
	PVI_WF_GCC16	= 8,
	PVI_WF_MAX,
};

struct epd_lut_data {
	unsigned int frame_num;
	unsigned int *data;
	u8 *wf_table;
};

/*
 * EPD LUT module export symbols
 */
int epd_lut_from_mem_init(void *waveform);
int epd_lut_from_file_init(struct device *dev, void *waveform, int size);
const char *epd_lut_get_wf_version(void);
int epd_lut_get(struct epd_lut_data *output, enum epd_lut_type lut_type, int temperture);

/*
 * PVI Waveform Interfaces
 */
int pvi_wf_input(void *waveform_file);
const char *pvi_wf_get_version(void);
int pvi_wf_get_lut(struct epd_lut_data *output, enum epd_lut_type lut_type, int temperture);

/*
 * RKF Waveform Interfaces
 */
int rkf_wf_input(void *waveform_file);
const char *rkf_wf_get_version(void);
int rkf_wf_get_lut(struct epd_lut_data *output, enum epd_lut_type lut_type, int temperture);
#endif
