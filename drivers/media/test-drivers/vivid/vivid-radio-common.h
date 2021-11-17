/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-radio-common.h - common radio rx/tx support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
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
