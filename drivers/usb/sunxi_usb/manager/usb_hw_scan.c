/*
 * drivers/usb/sunxi_usb/manager/usb_hw_scan.c
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
#include  "usb_manager.h"
#include  "usb_hw_scan.h"
#include  "usb_msg_center.h"

static struct usb_scan_info g_usb_scan_info;

void (*__usb_hw_scan) (struct usb_scan_info *);


/*
*******************************************************************************
*                     __get_pin_data
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
static __u32 get_pin_data(u32 id_hdle)
{
    return gpio_read_one_pin_value(id_hdle, NULL);
}

/*
*********************************************************************
*                     PIODataIn_debounce
*
* Description:
* 	过滤PIO的毛刺
* 	取10次，如果10次相同，则认为无抖动，取任意一次的值返回
* 	如果10次有一次不相同，则本次读取无效
*
* Arguments:
*    phdle  :  input.
*    value  :  output.  读回来的PIO的值
*
* Returns:
*    返回是否有变化
*
* note:
*    无
*
*********************************************************************
*/
static __u32 PIODataIn_debounce(__hdle phdle, __u32 *value)
{
    __u32 retry  = 0;
    __u32 time   = 10;
	__u32 temp1  = 0;
	__u32 cnt    = 0;
	__u32 change = 0;	/* 是否有抖动? */

    /* 取 10 次PIO的状态，如果10次的值都一样，说明本次读操作有效，
       否则，认为本次读操作失败。
    */
    if(phdle){
        retry = time;
		while(retry--){
			temp1 = get_pin_data(phdle);
			if(temp1){
				cnt++;
			}
		}

        /* 10 次都为0，或者都为1 */
		if((cnt == time)||(cnt == 0)){
		    change = 0;
		}
	    else{
	        change = 1;
	    }
	}else{
		change = 1;
	}

	if(!change){
		*value = temp1;
	}

	DMSG_DBG_MANAGER("phdle = %x, cnt = %x, change= %d, temp1 = %x\n", phdle, cnt, change, temp1);

	return change;
}

/*
*******************************************************************************
*                     get_id_state
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
static u32 get_id_state(struct usb_scan_info *info)
{
	enum usb_id_state id_state = USB_DEVICE_MODE;
	__u32 pin_data = 0;

	if(info->id_hdle){
		if(!PIODataIn_debounce(info->id_hdle, &pin_data)){
			if(pin_data){
				id_state = USB_DEVICE_MODE;
			}else{
				id_state = USB_HOST_MODE;
			}

			info->id_old_state = id_state;
		}else{
			id_state = info->id_old_state;
		}
	}

    return id_state;
}

/*
*******************************************************************************
*                     get_detect_vbus_state
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
static u32 get_detect_vbus_state(struct usb_scan_info *info)
{
	enum usb_det_vbus_state det_vbus_state = USB_DET_VBUS_INVALID;
	__u32 pin_data = 0;

	if(info->det_vbus_hdle){
		if(!PIODataIn_debounce(info->det_vbus_hdle, &pin_data)){
			if(pin_data){
				det_vbus_state = USB_DET_VBUS_VALID;
			}else{
				det_vbus_state = USB_DET_VBUS_INVALID;
			}

			info->det_vbus_old_state = det_vbus_state;
		}else{
			det_vbus_state = info->det_vbus_old_state;
		}
	}

    return det_vbus_state;
}

/*
*******************************************************************************
*                     do_vbus0_id0
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
static void do_vbus0_id0(struct usb_scan_info *info)
{
	enum usb_role role = USB_ROLE_NULL;

	role = get_usb_role();
	info->device_insmod_delay = 0;

	switch(role){
		case USB_ROLE_NULL:
			/* delay for vbus is stably */
			if(info->host_insmod_delay < USB_SCAN_INSMOD_HOST_DRIVER_DELAY){
				info->host_insmod_delay++;
				break;
			}
			info->host_insmod_delay = 0;

			/* insmod usb host */
			hw_insmod_usb_host();
		break;

		case USB_ROLE_HOST:
			/* nothing to do */
		break;

		case USB_ROLE_DEVICE:
			/* rmmod usb device */
			hw_rmmod_usb_device();
		break;

		default:
			DMSG_PANIC("ERR: unkown usb role(%d)\n", role);
	}

	return;
}

/*
*******************************************************************************
*                     do_vbus0_id1
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
static void do_vbus0_id1(struct usb_scan_info *info)
{
	enum usb_role role = USB_ROLE_NULL;

	role = get_usb_role();
	info->device_insmod_delay = 0;
	info->host_insmod_delay   = 0;

	switch(role){
		case USB_ROLE_NULL:
			/* nothing to do */
		break;

		case USB_ROLE_HOST:
			hw_rmmod_usb_host();
		break;

		case USB_ROLE_DEVICE:
			hw_rmmod_usb_device();
		break;

		default:
			DMSG_PANIC("ERR: unkown usb role(%d)\n", role);
	}

	return;
}

/*
*******************************************************************************
*                     do_vbus1_id0
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
static void do_vbus1_id0(struct usb_scan_info *info)
{
	enum usb_role role = USB_ROLE_NULL;

	role = get_usb_role();
	info->device_insmod_delay = 0;

	switch(role){
		case USB_ROLE_NULL:
			/* delay for vbus is stably */
			if(info->host_insmod_delay < USB_SCAN_INSMOD_HOST_DRIVER_DELAY){
				info->host_insmod_delay++;
				break;
			}
			info->host_insmod_delay = 0;

			hw_insmod_usb_host();
		break;

		case USB_ROLE_HOST:
			/* nothing to do */
		break;

		case USB_ROLE_DEVICE:
			hw_rmmod_usb_device();
		break;

		default:
			DMSG_PANIC("ERR: unkown usb role(%d)\n", role);
	}

	return;
}

/*
*******************************************************************************
*                     do_vbus1_id1
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
static void do_vbus1_id1(struct usb_scan_info *info)
{
	enum usb_role role = USB_ROLE_NULL;

	role = get_usb_role();
	info->host_insmod_delay = 0;

	switch(role){
		case USB_ROLE_NULL:
			/* delay for vbus is stably */
			if(info->device_insmod_delay < USB_SCAN_INSMOD_DEVICE_DRIVER_DELAY){
				info->device_insmod_delay++;
				break;
			}

			info->device_insmod_delay = 0;
			hw_insmod_usb_device();
		break;

		case USB_ROLE_HOST:
			hw_rmmod_usb_host();
		break;

		case USB_ROLE_DEVICE:
			/* nothing to do */
		break;

		default:
			DMSG_PANIC("ERR: unkown usb role(%d)\n", role);
	}

	return;
}

/*
*******************************************************************************
*                     get_vbus_id_state
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
static __u32 get_vbus_id_state(struct usb_scan_info *info)
{
    u32 state = 0;

	if(get_id_state(info) == USB_DEVICE_MODE){
		x_set_bit(state, 0);
	}

	if(get_detect_vbus_state(info) == USB_DET_VBUS_VALID){
		x_set_bit(state, 1);
	}

	return state;
}

/*
*******************************************************************************
*                     vbus_id_hw_scan
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
static void vbus_id_hw_scan(struct usb_scan_info *info)
{
	__u32 vbus_id_state = 0;

	vbus_id_state = get_vbus_id_state(info);

	DMSG_DBG_MANAGER("vbus_id=%d, role=%d\n", vbus_id_state, get_usb_role());

	switch(vbus_id_state){
		case  0x00:
			do_vbus0_id0(info);
		break;

		case  0x01:
			do_vbus0_id1(info);
		break;

		case  0x02:
			do_vbus1_id0(info);
		break;

		case  0x03:
			do_vbus1_id1(info);
		break;

		default:
			DMSG_PANIC("ERR: vbus_id_hw_scan: unkown vbus_id_state(0x%x)\n", vbus_id_state);
	}

	return ;
}

/*
*******************************************************************************
*                     usb_hw_scan
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
static void null_hw_scan(struct usb_scan_info *info)
{
	DMSG_DBG_MANAGER("null_hw_scan\n");

	return;
}

/*
*******************************************************************************
*                     usb_hw_scan
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
void usb_hw_scan(struct usb_cfg *cfg)
{
    __usb_hw_scan(&g_usb_scan_info);
}

/*
*******************************************************************************
*                     usb_hw_scan_init
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
__s32 usb_hw_scan_init(struct usb_cfg *cfg)
{
	struct usb_scan_info *scan_info = &g_usb_scan_info;
	struct usb_port_info *port_info = NULL;
	__s32 ret = 0;

	memset(scan_info, 0, sizeof(struct usb_scan_info));
	scan_info->cfg 					= cfg;
	scan_info->id_old_state 		= USB_DEVICE_MODE;
	scan_info->det_vbus_old_state 	= USB_DET_VBUS_INVALID;

	port_info =&(cfg->port[0]);
	switch(port_info->port_type){
		case USB_PORT_TYPE_DEVICE:
			__usb_hw_scan = null_hw_scan;
		break;

		case USB_PORT_TYPE_HOST:
			__usb_hw_scan = null_hw_scan;
		break;

		case USB_PORT_TYPE_OTG:
		{
			switch(port_info->detect_type){
				case USB_DETECT_TYPE_DP_DM:
					__usb_hw_scan = null_hw_scan;
				break;

				case USB_DETECT_TYPE_VBUS_ID:
				{
					__u32 need_pull_pio = 1;

					if((port_info->id.valid == 0) || (port_info->det_vbus.valid == 0)){
						DMSG_PANIC("ERR: usb detect tpye is vbus/id, but id(%d)/vbus(%d) is invalid\n",
							       port_info->id.valid, port_info->det_vbus.valid);
						ret = -1;
						goto failed;
					}

                    /* 如果id和vbus的pin相同, 就不需要拉pio了 */
					if(port_info->id.gpio_set.port_num == port_info->det_vbus.gpio_set.port_num){
						need_pull_pio = 0;
					}

					/* request id gpio */
					switch(port_info->id.group_type){
						case GPIO_GROUP_TYPE_PIO:
							/* request gpio */
							scan_info->id_hdle = sunxi_gpio_request_array(&port_info->id.gpio_set, 1);
							if(scan_info->id_hdle == 0){
								DMSG_PANIC("ERR: id gpio_request failed\n");
								ret = -1;
								goto failed;
							}

							/* set config, input */
							gpio_set_one_pin_io_status(scan_info->id_hdle, 0, NULL);

							/* reserved is pull up */
							if(need_pull_pio){
								gpio_set_one_pin_pull(scan_info->id_hdle, 1, NULL);
							}
						break;

						case GPIO_GROUP_TYPE_POWER:
							/* not support */
						break;

						default:
							DMSG_PANIC("ERR: unkown id gpio group type(%d)\n", port_info->id.group_type);
							ret = -1;
							goto failed;
					}

					/* request det_vbus gpio */
					switch(port_info->det_vbus.group_type){
						case GPIO_GROUP_TYPE_PIO:
							/* request gpio */
							scan_info->det_vbus_hdle = sunxi_gpio_request_array(&port_info->det_vbus.gpio_set, 1);
							if(scan_info->det_vbus_hdle == 0){
								DMSG_PANIC("ERR: det_vbus gpio_request failed\n");
								ret = -1;
								goto failed;
							}

							/* set config, input */
							gpio_set_one_pin_io_status(scan_info->det_vbus_hdle, 0, NULL);

							/* reserved is disable */
							if(need_pull_pio){
								gpio_set_one_pin_pull(scan_info->det_vbus_hdle, 0, NULL);
							}
						break;

						case GPIO_GROUP_TYPE_POWER:
							/* not support */
						break;

						default:
							DMSG_PANIC("ERR: unkown det_vbus gpio group type(%d)\n", port_info->det_vbus.group_type);
							ret = -1;
							goto failed;
					}

					__usb_hw_scan = vbus_id_hw_scan;
				}
				break;

				default:
					DMSG_PANIC("ERR: unkown detect_type(%d)\n", port_info->detect_type);
					ret = -1;
					goto failed;
			}
		}
		break;

		default:
			DMSG_PANIC("ERR: unkown port_type(%d)\n", cfg->port[0].port_type);
			ret = -1;
			goto failed;
	}

	return 0;

failed:
	if(scan_info->id_hdle){
		gpio_release(scan_info->id_hdle, 0);
		scan_info->id_hdle = 0;
	}

	if(scan_info->det_vbus_hdle){
		gpio_release(scan_info->det_vbus_hdle, 0);
		scan_info->det_vbus_hdle = 0;
	}

	__usb_hw_scan = null_hw_scan;

	return ret;
}

/*
*******************************************************************************
*                     usb_hw_scan_exit
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
__s32 usb_hw_scan_exit(struct usb_cfg *cfg)
{
	struct usb_scan_info *scan_info = &g_usb_scan_info;

	if(scan_info->id_hdle){
		gpio_release(scan_info->id_hdle, 0);
		scan_info->id_hdle = 0;
	}

	if(scan_info->det_vbus_hdle){
		gpio_release(scan_info->det_vbus_hdle, 0);
		scan_info->det_vbus_hdle = 0;
	}

	return 0;
}


