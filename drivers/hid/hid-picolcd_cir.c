/***************************************************************************
 *   Copyright (C) 2010-2012 by Bruno Prémont <bonbons@linux-vserver.org>  *
 *                                                                         *
 *   Based on Logitech G13 driver (v0.4)                                   *
 *     Copyright (C) 2009 by Rick L. Vinyard, Jr. <rvinyard@cs.nmsu.edu>   *
 *                                                                         *
 *   This program is free software: you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation, version 2 of the License.               *
 *                                                                         *
 *   This driver is distributed in the hope that it will be useful, but    *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   General Public License for more details.                              *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this software. If not see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#include <linux/hid.h>
#include <linux/hid-debug.h>
#include <linux/input.h>
#include "hid-ids.h"
#include "usbhid/usbhid.h"
#include <linux/usb.h>

#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/backlight.h>
#include <linux/lcd.h>

#include <linux/leds.h>

#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include <linux/completion.h>
#include <linux/uaccess.h>
#include <linux/module.h>

#include "hid-picolcd.h"


int picolcd_raw_cir(struct picolcd_data *data,
		struct hid_report *report, u8 *raw_data, int size)
{
	/* Need understanding of CIR data format to implement ... */
	return 1;
}

/* initialize CIR input device */
int picolcd_init_cir(struct picolcd_data *data, struct hid_report *report)
{
	/* support not implemented yet */
	return 0;
}

void picolcd_exit_cir(struct picolcd_data *data)
{
}

