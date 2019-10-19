/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for the Conexant CX23885/7/8 PCIe bridge
 *
 *  Copyright (C) 2010  Andy Walls <awalls@md.metrocast.net>
 */

#ifndef _CX23885_VIDEO_H_
#define _CX23885_VIDEO_H_
int cx23885_flatiron_write(struct cx23885_dev *dev, u8 reg, u8 data);
u8 cx23885_flatiron_read(struct cx23885_dev *dev, u8 reg);
#endif
