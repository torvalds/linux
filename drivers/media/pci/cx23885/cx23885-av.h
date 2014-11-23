/*
 *  Driver for the Conexant CX23885/7/8 PCIe bridge
 *
 *  AV device support routines - non-input, non-vl42_subdev routines
 *
 *  Copyright (C) 2010  Andy Walls <awalls@md.metrocast.net>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef _CX23885_AV_H_
#define _CX23885_AV_H_
void cx23885_av_work_handler(struct work_struct *work);
#endif
