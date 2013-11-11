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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#include "cx23885.h"
#include "cx23885-av.h"

void cx23885_av_work_handler(struct work_struct *work)
{
	struct cx23885_dev *dev =
			   container_of(work, struct cx23885_dev, cx25840_work);
	bool handled;

	v4l2_subdev_call(dev->sd_cx25840, core, interrupt_service_routine,
			 PCI_MSK_AV_CORE, &handled);
	cx23885_irq_enable(dev, PCI_MSK_AV_CORE);
}
