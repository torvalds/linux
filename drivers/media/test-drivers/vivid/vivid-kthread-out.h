/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-kthread-out.h - video/vbi output thread support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _VIVID_KTHREAD_OUT_H_
#define _VIVID_KTHREAD_OUT_H_

int vivid_start_generating_vid_out(struct vivid_dev *dev, bool *pstreaming);
void vivid_stop_generating_vid_out(struct vivid_dev *dev, bool *pstreaming);

#endif
