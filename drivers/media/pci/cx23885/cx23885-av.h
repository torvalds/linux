/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for the Conexant CX23885/7/8 PCIe bridge
 *
 *  AV device support routines - non-input, non-vl42_subdev routines
 *
 *  Copyright (C) 2010  Andy Walls <awalls@md.metrocast.net>
 */

#ifndef _CX23885_AV_H_
#define _CX23885_AV_H_
void cx23885_av_work_handler(struct work_struct *work);
#endif
