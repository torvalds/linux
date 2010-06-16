/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>

#include "msm_fb.h"
#include "mddihost.h"
#include "mddihosti.h"

#include <linux/clk.h>
#include <mach/clk.h>

extern struct semaphore mddi_host_mutex;
static boolean mddi_host_ext_powered = FALSE;

void mddi_host_start_ext_display(void)
{
	down(&mddi_host_mutex);

	if (!mddi_host_ext_powered) {
		mddi_host_init(MDDI_HOST_EXT);

		mddi_host_ext_powered = TRUE;
	}

	up(&mddi_host_mutex);
}

void mddi_host_stop_ext_display(void)
{
	down(&mddi_host_mutex);

	if (mddi_host_ext_powered) {
		mddi_host_powerdown(MDDI_HOST_EXT);

		mddi_host_ext_powered = FALSE;
	}

	up(&mddi_host_mutex);
}
