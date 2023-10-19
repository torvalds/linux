/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-radio-rx.h - radio receiver support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _VIVID_RADIO_RX_H_
#define _VIVID_RADIO_RX_H_

ssize_t vivid_radio_rx_read(struct file *, char __user *, size_t, loff_t *);
__poll_t vivid_radio_rx_poll(struct file *file, struct poll_table_struct *wait);

int vivid_radio_rx_enum_freq_bands(struct file *file, void *fh, struct v4l2_frequency_band *band);
int vivid_radio_rx_s_hw_freq_seek(struct file *file, void *fh, const struct v4l2_hw_freq_seek *a);
int vivid_radio_rx_g_tuner(struct file *file, void *fh, struct v4l2_tuner *vt);
int vivid_radio_rx_s_tuner(struct file *file, void *fh, const struct v4l2_tuner *vt);

#endif
