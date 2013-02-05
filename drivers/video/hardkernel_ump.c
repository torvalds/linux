/*
 * Copyright (C) 2012 Henrik Nordstrom <henrik@henriknordstrom.net>
 * Copyright (C) 2013 Mauro Ribeiro <mauro.ribeiro@hardkernel.com>
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

#include "linux/kernel.h"
#include "linux/mm.h"
#include <linux/uaccess.h>
#include <asm/memory.h>   
#include <linux/unistd.h> 
#include "linux/semaphore.h"
#include <linux/vmalloc.h>  
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/sched.h> /* wake_up_process() */
#include <linux/kthread.h> /* kthread_create(), kthread_run() */
#include <linux/err.h> /* IS_ERR(), PTR_ERR() */
#include <linux/delay.h>
#include <linux/platform_device.h>
#include "asm-generic/int-ll64.h" 
#include <linux/errno.h>
#include <linux/slab.h> 
#include <linux/delay.h>
#include <linux/init.h> 
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>  
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <video/hardkernel_ump.h>

#include "linux/ump_kernel_linux.h"
#include "common/ump_kernel_memory_backend.h"
#include <ump/ump_kernel_interface_ref_drv.h>

int (*disp_get_ump_secure_id) (struct fb_info *info, unsigned long arg, int buf);
EXPORT_SYMBOL(disp_get_ump_secure_id);

static int _disp_get_ump_secure_id(struct fb_info *info, unsigned long arg, int buf) {

	int buf_len = info->fix.smem_len;

	if (info->var.yres * 2 == info->var.yres_virtual)
		buf_len = buf_len >> 1;	
	else
		pr_warn("HardkernelUMP: Double buffer disabled!\n");
	
	u32 __user *psecureid = (u32 __user *) arg;
	ump_secure_id secure_id;
	ump_dd_physical_block ump_memory_description;
	ump_dd_handle ump_wrapped_buffer;

	ump_memory_description.addr = info->fix.smem_start + (buf_len * buf);
	ump_memory_description.size = info->fix.smem_len;
	                                
	if(buf > 0) { 
		ump_memory_description.addr += (buf_len * (buf - 1));
		ump_memory_description.size = buf_len;
	} 
	
	ump_wrapped_buffer = ump_dd_handle_create_from_phys_blocks(&ump_memory_description, 1);

	secure_id = ump_dd_secure_id_get(ump_wrapped_buffer);
	
	return put_user((unsigned int)secure_id, psecureid);
}

static int __init disp_ump_module_init(void) {
	int ret = 0;

	disp_get_ump_secure_id = _disp_get_ump_secure_id;
	pr_emerg("Hardkernel UMP: Loaded!\n");

	return ret;
}

static void __exit disp_ump_module_exit(void) {
	disp_get_ump_secure_id = NULL;
}

module_init(disp_ump_module_init);
module_exit(disp_ump_module_exit);

MODULE_AUTHOR("Henrik Nordstrom <henrik@henriknordstrom.net>");
MODULE_AUTHOR("Mauro Ribeiro <mauro.ribeiro@hardkernel.com>");
MODULE_DESCRIPTION("Hardkernel UMP Glue Driver");
MODULE_LICENSE("GPL");
