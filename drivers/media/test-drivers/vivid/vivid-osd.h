/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-osd.h - output overlay support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _VIVID_OSD_H_
#define _VIVID_OSD_H_

int vivid_fb_init(struct vivid_dev *dev);
void vivid_fb_deinit(struct vivid_dev *dev);
void vivid_fb_clear(struct vivid_dev *dev);
unsigned int vivid_fb_green_bits(struct vivid_dev *dev);

#endif
