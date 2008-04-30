/*****************************************************************************
 *
 *     Author: Xilinx, Inc.
 *
 *     This program is free software; you can redistribute it and/or modify it
 *     under the terms of the GNU General Public License as published by the
 *     Free Software Foundation; either version 2 of the License, or (at your
 *     option) any later version.
 *
 *     XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
 *     AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
 *     SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
 *     OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
 *     APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
 *     THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
 *     AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
 *     FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
 *     WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
 *     IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
 *     REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
 *     INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE.
 *
 *     Xilinx products are not intended for use in life support appliances,
 *     devices, or systems. Use in such applications is expressly prohibited.
 *
 *     (c) Copyright 2003-2008 Xilinx Inc.
 *     All rights reserved.
 *
 *     You should have received a copy of the GNU General Public License along
 *     with this program; if not, write to the Free Software Foundation, Inc.,
 *     675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

#ifndef XILINX_BUFFER_ICAP_H_	/* prevent circular inclusions */
#define XILINX_BUFFER_ICAP_H_	/* by using protection macros */

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/version.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include "xilinx_hwicap.h"

/* Loads a partial bitstream from system memory. */
int buffer_icap_set_configuration(struct hwicap_drvdata *drvdata, u32 *data,
			     u32 Size);

/* Loads a partial bitstream from system memory. */
int buffer_icap_get_configuration(struct hwicap_drvdata *drvdata, u32 *data,
			     u32 Size);

u32 buffer_icap_get_status(struct hwicap_drvdata *drvdata);
void buffer_icap_reset(struct hwicap_drvdata *drvdata);

#endif
