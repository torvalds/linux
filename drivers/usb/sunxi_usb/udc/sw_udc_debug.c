/*
 * drivers/usb/sunxi_usb/udc/sw_udc_debug.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen <javen@allwinnertech.com>
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include  "sw_udc_config.h"
#include  "sw_udc_board.h"
#include  "sw_udc_debug.h"

/*
*******************************************************************************
*                     print_list_node
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void print_list_node(struct sw_udc_ep *ep, char *str)
{
#ifdef SW_UDC_DEBUG
//#if 1
	struct sw_udc_request	*req = NULL;
	unsigned long		flags = 0;

	local_irq_save(flags);

	DMSG_INFO("---------------ep%d: %s-------------\n", ep->num, str);
	list_for_each_entry (req, &ep->queue, queue) {
		DMSG_INFO("print_list_node: ep(0x%p, %d), req(0x%p, 0x%p, %d, %d)\n\n",
			         ep, ep->num,
			         req, &(req->req), req->req.length, req->req.actual);
	}
	DMSG_INFO("-------------------------------------\n");

	local_irq_restore(flags);

	return;
#endif
}


