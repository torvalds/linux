/*
 * drivers/usb/sunxi_usb/manager/usbc0_platform.c
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

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <mach/irqs.h>

#include  "../include/sw_usb_config.h"

//---------------------------------------------------------------
//  device 信息描述
//---------------------------------------------------------------
static struct sw_udc_mach_info sw_udc_cfg;

//static u64 sw_udc_dmamask = 0xffffffffUL;

static struct platform_device sw_udc_device = {
	.name				= "sw_usb_udc",
	.id					= -1,

	.dev = {
//		.dma_mask			= &sw_udc_dmamask,
//		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &sw_udc_cfg,
	},
};

//---------------------------------------------------------------
//  host 信息描述
//---------------------------------------------------------------
static struct sw_hcd_eps_bits sw_hcd_eps[] = {
	{ "ep1_tx", 8, },
	{ "ep1_rx", 8, },
	{ "ep2_tx", 8, },
	{ "ep2_rx", 8, },
	{ "ep3_tx", 8, },
	{ "ep3_rx", 8, },
	{ "ep4_tx", 8, },
	{ "ep4_rx", 8, },
	{ "ep5_tx", 8, },
	{ "ep5_rx", 8, },
};

static struct sw_hcd_config sw_hcd_config = {
	.multipoint		= 1,
	.dyn_fifo		= 1,
	.soft_con		= 1,
	.dma			= 0,

	.num_eps		= USBC_MAX_EP_NUM,
	.dma_channels	= 0,
	.ram_size		= USBC0_MAX_FIFO_SIZE,
	.eps_bits	    = sw_hcd_eps,
};

static struct sw_hcd_platform_data sw_hcd_plat = {
	.mode		= SW_HCD_HOST,
	.config		= &sw_hcd_config,
};

//static u64 sw_hcd_dmamask = DMA_BIT_MASK(32);

static struct platform_device sw_hcd_device = {
	.name				= "sw_hcd_host0",
	.id					= -1,

	.dev = {
//		.dma_mask			= &sw_hcd_dmamask,
//		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &sw_hcd_plat,
	},
};

/*
*******************************************************************************
*                     usbc0_platform_device_init
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
__s32 usbc0_platform_device_init(struct usb_port_info *port_info)
{
    /* device */
    sw_udc_cfg.port_info = port_info;
    sw_udc_cfg.usbc_base = SW_VA_USB0_IO_BASE;

    /* host */
    sw_hcd_config.port_info = port_info;

    switch(port_info->port_type){
        case USB_PORT_TYPE_DEVICE:
            platform_device_register(&sw_udc_device);
            device_enable_async_suspend(&sw_udc_device.dev);
        break;

        case USB_PORT_TYPE_HOST:
            platform_device_register(&sw_hcd_device);
            device_enable_async_suspend(&sw_hcd_device.dev);
        break;

        case USB_PORT_TYPE_OTG:
            platform_device_register(&sw_udc_device);
            device_enable_async_suspend(&sw_udc_device.dev);
            platform_device_register(&sw_hcd_device);
            device_enable_async_suspend(&sw_hcd_device.dev);
        break;

        default:
            DMSG_PANIC("ERR: unkown port_type(%d)\n", port_info->port_type);
    }

    return 0;
}

/*
*******************************************************************************
*                     usbc0_platform_device_exit
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
__s32 usbc0_platform_device_exit(struct usb_port_info *info)
{
    switch(info->port_type){
        case USB_PORT_TYPE_DEVICE:
            platform_device_unregister(&sw_udc_device);
        break;

        case USB_PORT_TYPE_HOST:
            platform_device_unregister(&sw_hcd_device);
        break;

        case USB_PORT_TYPE_OTG:
            platform_device_unregister(&sw_udc_device);
            platform_device_unregister(&sw_hcd_device);
        break;

        default:
            DMSG_PANIC("ERR: unkown port_type(%d)\n", info->port_type);
    }

    return 0;
}


