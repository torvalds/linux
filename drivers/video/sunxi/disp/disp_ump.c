/*
 * Copyright (C) 2012 Henrik Nordstrom <henrik@henriknordstrom.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef CONFIG_FB_SUNXI_UMP
#error This file should not be built without UMP enabled
#endif

#include <ump/ump_kernel_interface_ref_drv.h>

#include "drv_disp_i.h"
#include "dev_disp.h"
#include "dev_fb.h"

static int _disp_get_ump_secure_id(struct fb_info *info, fb_info_t *g_fbi,
				   unsigned long arg, int buf)
{
	u32 __user *psecureid = (u32 __user *) arg;
	int buf_len = info->fix.smem_len;
	ump_secure_id secure_id;

	if (info->var.yres * 2 == info->var.yres_virtual)
		buf_len = buf_len >> 1;	/* divide by two */
	else
		__wrn("UMP: Double-buffering not enabled");

	if (!g_fbi->ump_wrapped_buffer[info->node][buf]) {
		ump_dd_physical_block ump_memory_description;

		ump_memory_description.addr =
			info->fix.smem_start + (buf_len * buf);
		ump_memory_description.size = buf_len;
		g_fbi->ump_wrapped_buffer[info->node][buf] =
			ump_dd_handle_create_from_phys_blocks
			(&ump_memory_description, 1);
	}
	secure_id = ump_dd_secure_id_get(g_fbi->
					 ump_wrapped_buffer[info->node][buf]);
	return put_user((unsigned int)secure_id, psecureid);
}

static int __init disp_ump_module_init(void)
{
	int ret = 0;

	disp_get_ump_secure_id = _disp_get_ump_secure_id;

	return ret;
}

static void __exit disp_ump_module_exit(void)
{
	disp_get_ump_secure_id = NULL;
}

module_init(disp_ump_module_init);
module_exit(disp_ump_module_exit);

MODULE_AUTHOR("Henrik Nordstrom <henrik@henriknordstrom.net>");
MODULE_DESCRIPTION("sunxi display driver MALI UMP module glue");
MODULE_LICENSE("GPL");
