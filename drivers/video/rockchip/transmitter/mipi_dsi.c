/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
 * drivers/video/display/transmitter/mipi_dsi.c
 * author: hhb@rock-chips.com
 * create date: 2013-01-17
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef CONFIG_MIPI_DSI
#include <common.h>
#endif
#ifdef CONFIG_RK_3288_DSI_UBOOT
#include <asm/io.h>
#include <errno.h>
#include <malloc.h>
#include <fdtdec.h>
#include <errno.h>
#include <asm/io.h>
#include <asm/arch/rkplat.h>
#include <lcd.h>
#include "mipi_dsi.h"
#else
#include <linux/module.h>
#include <linux/init.h>
#include <asm/system.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/rk_fb.h>
#include <linux/rk_screen.h>
#include <linux/ktime.h>

#include "mipi_dsi.h"
#endif

#define MAX_DSI_CHIPS 5

/*
*			 Driver Version Note
*
*v1.0 : this driver is a top level architecture of mipi dsi driver;
*v1.1 : add struct mipi_dsi_screen
*v1.2 : add id argument to identify different dsi 
*v1.3 : fix send commad's methods  
*/
#define MIPI_DSI_VERSION_AND_TIME  "mipi_dsi v1.3 2014-04-17"
#ifdef CONFIG_RK_3288_DSI_UBOOT
#define	printk(x...)	printf(x)
#endif


static struct mipi_dsi_ops *dsi_ops[MAX_DSI_CHIPS] = {NULL};
//static struct mipi_dsi_ops *cur_dsi_ops;

int register_dsi_ops(unsigned int id, struct mipi_dsi_ops *ops) {

	//int i = 0;
	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	dsi_ops[id] = ops;
	return 0;
}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(register_dsi_ops);
#endif

int del_dsi_ops(struct mipi_dsi_ops *ops) {

	int i = 0;

	for(i = 0; i < MAX_DSI_CHIPS; i++) {
		if(dsi_ops[i] == ops) {
			dsi_ops[i] = NULL;
			break;	
		}	
	}

	if(i == MAX_DSI_CHIPS) {
		printk("dsi ops not found\n");
		return -1;
	}
	return 0;	
}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(del_dsi_ops);
#endif

int dsi_probe_current_chip(unsigned int id) {
	int ret = 0;
	struct mipi_dsi_ops *ops = NULL;

	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];
	if(!ops)
		return -EINVAL;

	id = ops->get_id(ops->dsi);
	if(id == ops->id) {
		printk("load mipi dsi chip:%s id:%08x\n", ops->name, ops->id);
		printk("%s\n", MIPI_DSI_VERSION_AND_TIME);
	} else {
		printk("mipi dsi chip is not found, read id:%08x, but %08x is correct\n", id, ops->id);
		ret = -1;
	}

	return ret;
}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_probe_current_chip);
#endif
int dsi_power_up(unsigned int id) {

	struct mipi_dsi_ops *ops = NULL;

	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];

	if(!ops)
		return -EINVAL;
	if(ops->power_up)
		ops->power_up(ops->dsi);
	return 0;
}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_power_up);
#endif

int dsi_power_off(unsigned int id) {

	struct mipi_dsi_ops *ops = NULL;

	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];

	if(!ops)
		return -EINVAL;

	if(ops->power_down)
		ops->power_down(ops->dsi);

	return 0;
}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_power_off);
#endif
int dsi_set_regs(unsigned int id, void *array, u32 n) {

	struct mipi_dsi_ops *ops = NULL;

	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];

	if(!ops)
		return -EINVAL;

	if(ops->dsi_set_regs)
		ops->dsi_set_regs(ops->dsi, array, n);

	return 0;
}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_set_regs);
#endif
int dsi_init(unsigned int id, u32 n) {

	struct mipi_dsi_ops *ops = NULL;

	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];

	if(!ops)
		return -EINVAL;

	if(ops->dsi_init)
		ops->dsi_init(ops->dsi, n);

	return 0;
}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_init);
#endif
int dsi_enable_video_mode(unsigned int id, u32 enable) {

	struct mipi_dsi_ops *ops = NULL;

	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];

	if(!ops)
		return -EINVAL;

	if(ops->dsi_enable_video_mode)
		ops->dsi_enable_video_mode(ops->dsi, enable);

	return 0;

}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_enable_video_mode);
#endif
int dsi_enable_command_mode(unsigned int id, u32 enable) {

	struct mipi_dsi_ops *ops = NULL;

	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];

	if(!ops)
		return -EINVAL;

	if(ops->dsi_enable_command_mode)
		ops->dsi_enable_command_mode(ops->dsi, enable);

	return 0;

}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_enable_command_mode);
#endif
int dsi_enable_hs_clk(unsigned int id, u32 enable) {

	struct mipi_dsi_ops *ops = NULL;

	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];

	if(!ops)
		return -EINVAL;

	if(ops->dsi_enable_hs_clk)
		ops->dsi_enable_hs_clk(ops->dsi, enable);

	return 0;

}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_enable_hs_clk);
#endif
int dsi_is_active(unsigned int id) {

	struct mipi_dsi_ops *ops = NULL;

	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];

	if(!ops)
		return -EINVAL;

	if(ops->dsi_is_active)
		return ops->dsi_is_active(ops->dsi);
	else
		return -1;
}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_is_active);
#endif
int dsi_is_enable(unsigned int id, u32 enable){

    struct mipi_dsi_ops *ops = NULL;

	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];

	if(!ops)
		return -EINVAL;

	if(ops->dsi_is_enable)
		ops->dsi_is_enable(ops->dsi, enable);

	return 0;
	
}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_is_enable);
#endif
int dsi_send_dcs_packet(unsigned int id, unsigned char *packet, u32 n) {

	struct mipi_dsi_ops *ops = NULL;

    //printk("dsi_send_dcs_packet-------id=%d\n",id);
	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];

	if(!ops)
		return -EINVAL;

	if(ops->dsi_send_dcs_packet)
		ops->dsi_send_dcs_packet(ops->dsi, packet, n);
	return 0;
}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_send_dcs_packet);
#endif

int dsi_read_dcs_packet(unsigned int id, unsigned char *packet, u32 n) {

	struct mipi_dsi_ops *ops = NULL;

	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];

	if(!ops)
		return -EINVAL;

	if(ops->dsi_read_dcs_packet)
		ops->dsi_read_dcs_packet(ops->dsi, packet, n);
	return 0;
}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_read_dcs_packet);
#endif

int dsi_send_packet(unsigned int id, unsigned char *packet, u32 n) {

	struct mipi_dsi_ops *ops = NULL;

	if(id > (MAX_DSI_CHIPS - 1))
		return -EINVAL;

	ops = dsi_ops[id];

	if(!ops)
		return -EINVAL;

	if(ops->dsi_send_packet)
		ops->dsi_send_packet(ops->dsi, packet, n);
		
	return 0;
}
#ifdef CONFIG_MIPI_DSI
EXPORT_SYMBOL(dsi_send_packet);
#endif
