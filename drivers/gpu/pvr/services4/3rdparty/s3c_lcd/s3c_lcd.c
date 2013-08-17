/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 		Samsung Electronics System LSI. modify
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>

#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
//#include <asm/hardware.h>

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"

#include "s3c_lcd.h"

MODULE_LICENSE("GPL");

static int __init S3cLcdBridgeInit(void)
{
	if(s3c_displayclass_init())
		return -1;
	return 0;
}

static void __exit S3cLcdBridgeExit (void)
{
	s3c_displayclass_deinit();
}

late_initcall(S3cLcdBridgeInit);
module_exit(S3cLcdBridgeExit);
