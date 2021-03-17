/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-kthread-cap.h - video/vbi capture thread support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _VIVID_KTHREAD_CAP_H_
#define _VIVID_KTHREAD_CAP_H_

int vivid_start_generating_vid_cap(struct vivid_dev *dev, bool *pstreaming);
void vivid_stop_generating_vid_cap(struct vivid_dev *dev, bool *pstreaming);

#endif
