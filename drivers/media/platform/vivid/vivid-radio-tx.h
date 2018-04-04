/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-radio-tx.h - radio transmitter support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _VIVID_RADIO_TX_H_
#define _VIVID_RADIO_TX_H_

ssize_t vivid_radio_tx_write(struct file *, const char __user *, size_t, loff_t *);
__poll_t vivid_radio_tx_poll(struct file *file, struct poll_table_struct *wait);

int vidioc_g_modulator(struct file *file, void *fh, struct v4l2_modulator *a);
int vidioc_s_modulator(struct file *file, void *fh, const struct v4l2_modulator *a);

#endif
