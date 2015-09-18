/*
 * vivid-radio-common.h - common radio rx/tx support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _VIVID_RADIO_COMMON_H_
#define _VIVID_RADIO_COMMON_H_

/* The supported radio frequency ranges in kHz */
#define FM_FREQ_RANGE_LOW       (64000U * 16U)
#define FM_FREQ_RANGE_HIGH      (108000U * 16U)
#define AM_FREQ_RANGE_LOW       (520U * 16U)
#define AM_FREQ_RANGE_HIGH      (1710U * 16U)
#define SW_FREQ_RANGE_LOW       (2300U * 16U)
#define SW_FREQ_RANGE_HIGH      (26100U * 16U)

enum { BAND_FM, BAND_AM, BAND_SW, TOT_BANDS };

extern const struct v4l2_frequency_band vivid_radio_bands[TOT_BANDS];

int vivid_radio_g_frequency(struct file *file, const unsigned *freq, struct v4l2_frequency *vf);
int vivid_radio_s_frequency(struct file *file, unsigned *freq, const struct v4l2_frequency *vf);

void vivid_radio_rds_init(struct vivid_dev *dev);

#endif
