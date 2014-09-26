/*
 * Copyright (C) 2014 ROCKCHIP, Inc.
 * drivers/video/rockchip/screen/lcd_mipi.c
 * author: libing@rock-chips.com
 * create date: 2014-04-10
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef CONFIG_LCD_MIPI
#include <common.h>
#endif
#ifdef CONFIG_LCD_MIPI
#include "../transmitter/mipi_dsi.h"
#include <linux/delay.h>
#endif
#ifdef CONFIG_RK_3288_DSI_UBOOT
#include <common.h>
#include <asm/io.h>
#include <errno.h>
#include <malloc.h>
#include <fdtdec.h>
#include <errno.h>
#include <asm/io.h>
#include <asm/arch/rkplat.h>
#include <lcd.h>
#include "../transmitter/mipi_dsi.h"
#endif

#ifdef CONFIG_RK_3288_DSI_UBOOT
#define	MIPI_SCREEN_DBG(x...)	//printf(x)
#elif defined CONFIG_LCD_MIPI
#define	MIPI_SCREEN_DBG(x...)	//printk(KERN_ERR x)
#else
#define	MIPI_SCREEN_DBG(x...)  
#endif
#ifdef CONFIG_RK_3288_DSI_UBOOT
DECLARE_GLOBAL_DATA_PTR;
#define msleep(a) udelay(a * 1000)
#define	printk(x...)	//printf(x)
#endif
static struct mipi_screen *gmipi_screen;

static void rk_mipi_screen_pwr_disable(struct mipi_screen *screen)
{   
	if(screen->lcd_en_gpio != INVALID_GPIO){
		gpio_direction_output(screen->lcd_en_gpio, !screen->lcd_en_atv_val);
		msleep(screen->lcd_en_delay);
	}
	else{
		MIPI_SCREEN_DBG("lcd_en_gpio is null");
	}
	
	if(screen->lcd_rst_gpio != INVALID_GPIO){

		gpio_direction_output(screen->lcd_rst_gpio, !screen->lcd_rst_atv_val);
		msleep(screen->lcd_rst_delay);
	}
	else{
		MIPI_SCREEN_DBG("lcd_rst_gpio is null");
	}    
}

static void rk_mipi_screen_pwr_enable(struct mipi_screen *screen)
{   

	if(screen->lcd_en_gpio != INVALID_GPIO){
		gpio_direction_output(screen->lcd_en_gpio, !screen->lcd_en_atv_val);
		msleep(screen->lcd_en_delay);
		gpio_direction_output(screen->lcd_en_gpio, screen->lcd_en_atv_val);
		msleep(screen->lcd_en_delay);
	}
	else
		MIPI_SCREEN_DBG("lcd_en_gpio is null\n");
	
	if(screen->lcd_rst_gpio != INVALID_GPIO){
		gpio_direction_output(screen->lcd_rst_gpio, !screen->lcd_rst_atv_val);
		msleep (screen->lcd_rst_delay);
		gpio_direction_output(screen->lcd_rst_gpio, screen->lcd_rst_atv_val);
		msleep(screen->lcd_rst_delay);
	}
	else
		MIPI_SCREEN_DBG("lcd_rst_gpio is null\n");
}

static void rk_mipi_screen_cmd_init(struct mipi_screen *screen)
{
	u8 len, i;
	u8 *cmds;
	struct list_head *screen_pos;
	struct mipi_dcs_cmd_ctr_list  *dcs_cmd;
#ifdef CONFIG_RK_3288_DSI_UBOOT
	cmds = calloc(1,0x400);
	if(!cmds) {
		printf("request cmds fail!\n");
		return;
	}
#endif

#ifdef CONFIG_LCD_MIPI
	cmds = kmalloc(0x400, GFP_KERNEL);
	if(!cmds) {
		printk("request cmds fail!\n");
		return ;
	}
#endif
		
	list_for_each(screen_pos, &screen->cmdlist_head){
	
		dcs_cmd = list_entry(screen_pos, struct mipi_dcs_cmd_ctr_list, list);
		len = dcs_cmd->dcs_cmd.cmd_len + 1;
		
		for( i = 1; i < len ; i++){
			cmds[i] = dcs_cmd->dcs_cmd.cmds[i-1];
		}
		MIPI_SCREEN_DBG("dcs_cmd.name:%s\n",dcs_cmd->dcs_cmd.name);
		if(dcs_cmd->dcs_cmd.type == LPDT){
			cmds[0] = LPDT;
			if(dcs_cmd->dcs_cmd.dsi_id == 0){
				MIPI_SCREEN_DBG("dcs_cmd.dsi_id == 0 line=%d\n",__LINE__);                
				dsi_send_packet(0, cmds, len);
			}
			else if (dcs_cmd->dcs_cmd.dsi_id == 1){
				MIPI_SCREEN_DBG("dcs_cmd.dsi_id == 1 line=%d\n",__LINE__);
				dsi_send_packet(1, cmds, len);
			}
			else if (dcs_cmd->dcs_cmd.dsi_id == 2){
				MIPI_SCREEN_DBG("dcs_cmd.dsi_id == 2 line=%d\n",__LINE__);
				dsi_send_packet(0, cmds, len);
				dsi_send_packet(1, cmds, len);
			}
			else{
			    MIPI_SCREEN_DBG("dsi is err.\n");
			}
			msleep(dcs_cmd->dcs_cmd.delay);
			
		}
		else if(dcs_cmd->dcs_cmd.type == HSDT){
			cmds[0] = HSDT;
			if(dcs_cmd->dcs_cmd.dsi_id == 0){
			
				MIPI_SCREEN_DBG("dcs_cmd.dsi_id == 0 line=%d\n",__LINE__); 
				dsi_send_packet(0, cmds, len);
			}
			else if (dcs_cmd->dcs_cmd.dsi_id == 1){
			
				MIPI_SCREEN_DBG("dcs_cmd.dsi_id == 1 line=%d\n",__LINE__);
				dsi_send_packet(1, cmds, len);
			}
			else if (dcs_cmd->dcs_cmd.dsi_id == 2){
				MIPI_SCREEN_DBG("dcs_cmd.dsi_id == 2 line=%d\n",__LINE__); 
				dsi_send_packet(0, cmds, len);
				dsi_send_packet(1, cmds, len);
			}
			else{
				MIPI_SCREEN_DBG("dsi is err.");
			}
			msleep(dcs_cmd->dcs_cmd.delay);
			
		}
		else
		    MIPI_SCREEN_DBG("cmd type err.\n");
	}

#ifdef CONFIG_RK_3288_DSI_UBOOT
	free(cmds);
#endif
#ifdef CONFIG_LCD_MIPI
	kfree(cmds);
#endif

}

int rk_mipi_screen(void) 
{
	u8 dcs[16] = {0}, rk_dsi_num;
	rk_dsi_num = gmipi_screen->mipi_dsi_num;
	if(gmipi_screen->screen_init == 0){
		rk_mipi_screen_pwr_enable(gmipi_screen);
		
		dsi_enable_hs_clk(0,1);
		if(rk_dsi_num == 2){
			dsi_enable_hs_clk(1, 1);
		}
		
		dsi_enable_video_mode(0,0);
		if(rk_dsi_num == 2){
			dsi_enable_video_mode(1,0);
		} 
		
		dsi_enable_command_mode(0, 1);
		if(rk_dsi_num == 2){
			dsi_enable_command_mode(1, 1);
		} 
		
		dcs[0] = LPDT;
		dcs[1] = DTYPE_DCS_SWRITE_0P;
		dcs[2] = dcs_exit_sleep_mode; 
		dsi_send_packet(0, dcs, 3);
		if(rk_dsi_num ==2)   
			dsi_send_packet(1, dcs, 3);
		
		msleep(20);
		
		dcs[0] = LPDT;
		dcs[1] = DTYPE_DCS_SWRITE_0P;
		dcs[2] = dcs_set_display_on; 
		dsi_send_packet(0, dcs, 3);
		if(rk_dsi_num ==2)
	        dsi_send_packet(1, dcs, 3); 

		msleep(20);
		
		dsi_enable_command_mode(0,0);
		if(rk_dsi_num == 2){
			dsi_enable_command_mode(1,0);
		} 

		dsi_enable_video_mode(0,1);
		if(rk_dsi_num == 2){
			dsi_enable_video_mode(1,1);
		} 
	}
	else{
		rk_mipi_screen_pwr_enable(gmipi_screen);

		dsi_enable_hs_clk(0,1);
		if(rk_dsi_num == 2){
			dsi_enable_hs_clk(1, 1);
		}

		dsi_enable_video_mode(0,0);
		if(rk_dsi_num == 2){
			dsi_enable_video_mode(1,0);
		} 
		
		dsi_enable_command_mode(0, 1);
		if(rk_dsi_num == 2){
			dsi_enable_command_mode(1, 1);
		} 

		rk_mipi_screen_cmd_init(gmipi_screen);

		dsi_enable_command_mode(0,0);
		if(rk_dsi_num == 2){
			dsi_enable_command_mode(1,0);
		} 

		dsi_enable_video_mode(0,1);
		if(rk_dsi_num == 2){
			dsi_enable_video_mode(1,1);
		} 
	}
	
	MIPI_SCREEN_DBG("++++++++++++++++%s:%d\n", __func__, __LINE__);
	return 0;
}

int rk_mipi_screen_standby(u8 enable) 
{
	u8 dcs[16] = {0}, rk_dsi_num;
	rk_dsi_num = gmipi_screen->mipi_dsi_num;

	if(dsi_is_active(0) != 1) 
		return -1;
	
	if(rk_dsi_num ==2)
		if((dsi_is_active(0) != 1) ||(dsi_is_active(1) != 1))
			return -1;
	
	if(enable) {
		/*below is changeable*/
		dcs[0] = LPDT;
		dcs[1] = DTYPE_DCS_SWRITE_0P;
		dcs[2] = dcs_set_display_off; 
		dsi_send_packet(0, dcs, 3);
		if(rk_dsi_num ==2)   
			dsi_send_packet(1, dcs, 3);
		
		msleep(30);
		
		dcs[0] = LPDT;
		dcs[1] = DTYPE_DCS_SWRITE_0P;
		dcs[2] = dcs_enter_sleep_mode; 
		dsi_send_packet(0, dcs, 3);
		if(rk_dsi_num ==2)
			dsi_send_packet(1, dcs, 3);   

		msleep(100);
		rk_mipi_screen_pwr_disable(gmipi_screen);
		MIPI_SCREEN_DBG("++++enable++++++++++++%s:%d\n", __func__, __LINE__);
	}
	else {
		rk_mipi_screen();
	}
    return 0;
}
#ifdef CONFIG_LCD_MIPI
static int rk_mipi_screen_init_dt(struct mipi_screen *screen)
{
	struct device_node *childnode, *grandchildnode, *root;
	struct mipi_dcs_cmd_ctr_list  *dcs_cmd;
	struct list_head *pos;
	struct property *prop;
	enum of_gpio_flags flags;
	u32 value, i, debug, gpio, ret, cmds[25], length;

	memset(screen, 0, sizeof(*screen));
	
	INIT_LIST_HEAD(&screen->cmdlist_head);

	childnode = of_find_node_by_name(NULL, "mipi_dsi_init");
	if(!childnode ){
		MIPI_SCREEN_DBG("%s: Can not get child => mipi_init.\n", __func__);
	}
	else{
		ret = of_property_read_u32(childnode, "rockchip,screen_init", &value);
		if (ret) {
			MIPI_SCREEN_DBG("%s: Can not read property: screen_init.\n", __func__);
		} 
		else {
			if((value != 0) && (value != 1) ){
				printk("err: rockchip,mipi_dsi_init not match.\n");
				return -1;
			}else
				screen->screen_init = value ;
			
			MIPI_SCREEN_DBG("%s: lcd->screen_init = %d.\n", __func__, screen->screen_init ); 
		}
		
		ret = of_property_read_u32(childnode, "rockchip,dsi_lane", &value);
		if (ret) {
			MIPI_SCREEN_DBG("%s: Can not read property: dsi_lane.\n", __func__);
		} else {
			screen->dsi_lane = value;
			MIPI_SCREEN_DBG("%s: mipi_lcd->dsi_lane = %d.\n", __func__, screen->dsi_lane ); 
		} 
	
		ret = of_property_read_u32(childnode, "rockchip,dsi_hs_clk", &value);
		if (ret) {
			MIPI_SCREEN_DBG("%s: Can not read property: dsi_hs_clk.\n", __func__);
		} else {
			if((value <= 90) || (value >= 1500)){
				printk("err: rockchip,hs_tx_clk not match.");
				return -1;
			}else
				screen->hs_tx_clk = value*MHz;
		
			MIPI_SCREEN_DBG("%s: lcd->screen->hs_tx_clk = %d.\n", __func__, screen->hs_tx_clk ); 
		} 
		
		ret = of_property_read_u32(childnode, "rockchip,mipi_dsi_num", &value);
		if (ret) {
		    MIPI_SCREEN_DBG("%s: Can not read property: mipi_dsi_num.\n", __func__);
		} else {
			if((value != 1) && (value != 2) ){
				printk("err: rockchip,mipi_dsi_num not match.\n");
				return -1;
			}
			else
				screen->mipi_dsi_num = value ;
	
			MIPI_SCREEN_DBG("%s: lcd->screen.mipi_dsi_num = %d.\n", __func__, screen->mipi_dsi_num ); 
		}  
	}

	childnode = of_find_node_by_name(NULL, "mipi_power_ctr");
	if(!childnode ){
		screen->lcd_rst_gpio = INVALID_GPIO;
		screen->lcd_en_gpio = INVALID_GPIO;
		MIPI_SCREEN_DBG("%s: Can not get child => mipi_power_ctr.\n", __func__);
	}
	else{
		grandchildnode = of_get_child_by_name(childnode, "mipi_lcd_rst");
		if (!grandchildnode){
			screen->lcd_rst_gpio = INVALID_GPIO;
			MIPI_SCREEN_DBG("%s: Can not read property: mipi_lcd_rst.\n", __func__);
		}
		else{
			ret = of_property_read_u32(grandchildnode, "rockchip,delay", &value);
			if (ret){
				MIPI_SCREEN_DBG("%s: Can not read property: delay.\n", __func__);
			} 
			else {
				screen->lcd_rst_delay = value;
				MIPI_SCREEN_DBG("%s: lcd->screen->lcd_rst_delay = %d.\n", __func__, screen->lcd_rst_delay );     
			} 
			
			gpio = of_get_named_gpio_flags(grandchildnode, "rockchip,gpios", 0, &flags);
			if (!gpio_is_valid(gpio)){
				MIPI_SCREEN_DBG("rest: Can not read property: %s->gpios.\n", __func__);
			} 
			
			ret = gpio_request(gpio,"mipi_lcd_rst");
			if (ret) {
				screen->lcd_rst_gpio = INVALID_GPIO;
				MIPI_SCREEN_DBG("request mipi_lcd_rst gpio fail:%d\n",gpio);
				return -1;
			}
			
			screen->lcd_rst_gpio = gpio;
			screen->lcd_rst_atv_val = (flags == GPIO_ACTIVE_HIGH)? 1:0; 
			
			MIPI_SCREEN_DBG("lcd->lcd_rst_gpio=%d,dsi->lcd_rst_atv_val=%d\n",screen->lcd_rst_gpio,screen->lcd_rst_atv_val);
		} 

		grandchildnode = of_get_child_by_name(childnode, "mipi_lcd_en");
		if (!grandchildnode){
			screen->lcd_en_gpio = INVALID_GPIO;
			MIPI_SCREEN_DBG("%s: Can not read property: mipi_lcd_en.\n", __func__);
		} 
		else {
			ret = of_property_read_u32(grandchildnode, "rockchip,delay", &value);
			if (ret){
				MIPI_SCREEN_DBG("%s: Can not read property: mipi_lcd_en-delay.\n", __func__);
			} 
			else {
				screen->lcd_en_delay = value;
				MIPI_SCREEN_DBG("%s: lcd->screen.lcd_en_delay = %d.\n", __func__, screen->lcd_en_delay ); 
			} 
			
			gpio = of_get_named_gpio_flags(grandchildnode, "rockchip,gpios", 0, &flags);
			if (!gpio_is_valid(gpio)){
				MIPI_SCREEN_DBG("rest: Can not read property: %s->gpios.\n", __func__);
			}

			ret = gpio_request(gpio,"mipi_lcd_en");
			if (ret) {
				screen->lcd_en_gpio = INVALID_GPIO;
				MIPI_SCREEN_DBG("request mipi_lcd_en gpio fail:%d\n",gpio);
				return -1;
			}
			screen->lcd_en_gpio = gpio;
			screen->lcd_en_atv_val= (flags == GPIO_ACTIVE_HIGH)? 1:0; 
			MIPI_SCREEN_DBG("dsi->lcd_en_gpio=%d, dsi->screen.lcd_en_atv_val=%d\n",screen->lcd_en_gpio,screen->lcd_en_atv_val);
		}
	}

	root= of_find_node_by_name(NULL,"screen-on-cmds");
	if (!root) {
	    MIPI_SCREEN_DBG("can't find screen-on-cmds node\n");
	}
	else{
		for_each_child_of_node(root, childnode){
			dcs_cmd = kmalloc(sizeof(struct mipi_dcs_cmd_ctr_list), GFP_KERNEL);
			strcpy(dcs_cmd->dcs_cmd.name, childnode->name);
			
			prop = of_find_property(childnode, "rockchip,cmd", &length);
			if (!prop){
				MIPI_SCREEN_DBG("Can not read property: cmds\n");
				return -EINVAL;
			}

			MIPI_SCREEN_DBG("\n childnode->name =%s:length=%d\n",childnode->name,(length / sizeof(u32)));
			
			ret = of_property_read_u32_array(childnode,  "rockchip,cmd", cmds, (length / sizeof(u32)));
			if(ret < 0) {
				MIPI_SCREEN_DBG("%s: Can not read property: %s--->cmds\n", __func__,childnode->name);
				return ret;
			}
			else{
				dcs_cmd->dcs_cmd.cmd_len =  length / sizeof(u32) ;
				
				for(i = 0; i < (length / sizeof(u32)); i++){   
					MIPI_SCREEN_DBG("cmd[%d]=%02x£¬",i+1,cmds[i]);
					dcs_cmd->dcs_cmd.cmds[i] = cmds[i];
				}
				
				MIPI_SCREEN_DBG("dcs_cmd->dcs_cmd.cmd_len=%d\n",dcs_cmd->dcs_cmd.cmd_len);
			}
			ret = of_property_read_u32(childnode, "rockchip,dsi_id", &value);
			if(ret){
				MIPI_SCREEN_DBG("%s: Can not read property: %s--->cmd_type\n", __func__,childnode->name);
			}
			else{
				if(screen->mipi_dsi_num ==1 ){
					if(value != 0) {
						printk("err: rockchip,dsi_id not match.\n");
					}
					else
						dcs_cmd->dcs_cmd.dsi_id = value;
				}
				else {
					if((value < 0) || ( value > 2)) 
						printk("err: rockchip,dsi_id not match.\n");
					else
						dcs_cmd->dcs_cmd.dsi_id = value;
				}
			}
			 
			ret = of_property_read_u32(childnode, "rockchip,cmd_type", &value);
			if(ret){
			    MIPI_SCREEN_DBG("%s: Can not read property: %s--->cmd_type\n", __func__,childnode->name);
			}
			else{
			    if((value != 0) && (value != 1)){
			        printk("err: rockchip, cmd_type not match.\n");
			    }
			    else
			        dcs_cmd->dcs_cmd.type = value;
			}

			ret = of_property_read_u32(childnode, "rockchip,cmd_delay", &value);
			if(ret)
			    MIPI_SCREEN_DBG("%s: Can not read property: %s--->cmd_delay\n", __func__,childnode->name);
			else
			    dcs_cmd->dcs_cmd.delay = value;

			list_add_tail(&dcs_cmd->list, &screen->cmdlist_head);
		}
	}
	ret = of_property_read_u32(root, "rockchip,cmd_debug", &debug);
	if(ret){
		MIPI_SCREEN_DBG("%s: Can not read property: rockchip,cmd_debug.\n", __func__);
	}
	else{
		if (debug) {
			list_for_each(pos, &screen->cmdlist_head) {
				dcs_cmd = list_entry(pos, struct mipi_dcs_cmd_ctr_list, list);
				printk("\n dcs_name:%s,dcs_type:%d,side_id:%d,cmd_len:%d,delay:%d\n\n",
					dcs_cmd->dcs_cmd.name,
					dcs_cmd->dcs_cmd.type, 
					dcs_cmd->dcs_cmd.dsi_id,
					dcs_cmd->dcs_cmd.cmd_len,
					dcs_cmd->dcs_cmd.delay);
				for(i=0; i < (dcs_cmd->dcs_cmd.cmd_len) ;i++){
					printk("[%d]=%02x,",i+1,dcs_cmd->dcs_cmd.cmds[i]);
				}
			}
		}
		else
			MIPI_SCREEN_DBG("---close cmd debug---\n");
	}
	return 0; 
}
#endif
int rk_mipi_get_dsi_num(void)
{
	return gmipi_screen->mipi_dsi_num;
}
#ifdef CONFIG_LCD_MIPI
EXPORT_SYMBOL(rk_mipi_get_dsi_num);
#endif
int rk_mipi_get_dsi_lane(void)
{
	return gmipi_screen->dsi_lane;
}
#ifdef CONFIG_LCD_MIPI
EXPORT_SYMBOL(rk_mipi_get_dsi_lane);
#endif

int rk_mipi_get_dsi_clk(void)
{
    return gmipi_screen->hs_tx_clk;
}
#ifdef CONFIG_LCD_MIPI
EXPORT_SYMBOL(rk_mipi_get_dsi_clk);
#endif
#ifdef CONFIG_RK_3288_DSI_UBOOT
#ifdef CONFIG_OF_LIBFDT
static int rk_mipi_screen_init_dt(struct mipi_screen *screen)
{
    struct mipi_dcs_cmd_ctr_list  *dcs_cmd;
    u32 i,cmds[20];
    int length;
    int err;
    int node;
    const void *blob;
    struct fdt_gpio_state gpio_val;
    int noffset;
    
    INIT_LIST_HEAD(&screen->cmdlist_head);

    blob = gd->fdt_blob;//getenv_hex("fdtaddr", 0);
    node = fdtdec_next_compatible(blob, 0, COMPAT_ROCKCHIP_MIPI_INIT);
    if(node < 0){
    	MIPI_SCREEN_DBG("Can not get node of COMPAT_ROCKCHIP_MIPI_INIT\n");
    }
    screen->screen_init = fdtdec_get_int(blob, node, "rockchip,screen_init", -1);
    if(screen->screen_init < 0){
    	MIPI_SCREEN_DBG("Can not get screen_init\n");
    }
    screen->dsi_lane = fdtdec_get_int(blob, node, "rockchip,dsi_lane", -1);
    if(screen->dsi_lane < 0){
    	MIPI_SCREEN_DBG("Can not get dsi_lane\n");
    }
    screen->hs_tx_clk= fdtdec_get_int(blob, node, "rockchip,dsi_hs_clk", -1);
    if(screen->hs_tx_clk < 0){
    	MIPI_SCREEN_DBG("Can not get dsi_hs_clk\n");
    }else{
    	screen->hs_tx_clk = screen->hs_tx_clk*MHZ;
    }
    screen->mipi_dsi_num= fdtdec_get_int(blob, node, "rockchip,mipi_dsi_num", -1);
    if(screen->mipi_dsi_num < 0){
    	MIPI_SCREEN_DBG("Can't get mipi_dsi_num\n");
    }
    #if 0   
    node = fdtdec_next_compatible(blob, 0, COMPAT_ROCKCHIP_MIPI_PWR);
    if(node < 0){
    	printf("Can not get node of COMPAT_ROCKCHIP_MIPI_PWR\n");
    }
    #endif

#if 0
    /*get the lcd rst status*/    
//    handle = fdt_getprop_u32_default(blob, "/mipi_power_ctr", "mipi_lcd_rst", -1);
//    node = fdt_node_offset_by_phandle(blob, handle);
    node = fdtdec_next_compatible(blob, 0, COMPAT_ROCKCHIP_MIPI_PWR);
    if(node < 0){
        printf("Can not get node of COMPAT_ROCKCHIP_MIPI_PWR\n");
    }else{
        subnode = fdtdec_next_compatible_subnode(blob, node,
				COMPAT_ROCKCHIP_MIPI_LCD_RST, &depth);
	if (subnode <=0) {
	    screen->lcd_rst_gpio = INVALID_GPIO;
	    printf("Can't get pin of mipi_lcd_rst\n");
	} else {
	   err = fdtdec_decode_gpio(blob, subnode, "rockchip,gpios", &gpio_val);
	   gpio_val.gpio = rk_gpio_base_to_bank(gpio_val.gpio & RK_GPIO_BANK_MASK) | (gpio_val.gpio & RK_GPIO_PIN_MASK);
	    if(err < 0){    
		screen->lcd_rst_gpio = INVALID_GPIO;
		printf("Can't find GPIO rst\n");
	    }else{
		screen->lcd_rst_gpio = gpio_val.gpio;
		screen->lcd_rst_atv_val = !(gpio_val.flags & OF_GPIO_ACTIVE_LOW);
	    }	    
	    screen->lcd_rst_delay = fdtdec_get_int(blob, subnode, "rockchip,delay", -1);
	    if(screen->lcd_rst_delay < 0){
		printf("Can't get delay of rst delay\n");
	    }
	    printf("Get lcd rst gpio and delay successfully!\n");
	}
    }
    #endif
	/*get the lcd rst & en status*/
    node = fdtdec_next_compatible(blob, 0, COMPAT_ROCKCHIP_MIPI_PWR);
    if(node < 0){
        MIPI_SCREEN_DBG("Can not get node of COMPAT_ROCKCHIP_MIPI_PWR\n");
    }else{	
	#if 0
	noffset = fdt_first_subnode(blob,node);
	const char *name = fdt_get_name(blob, noffset, NULL);
	printf("XJH_DEBUG1:%s\n",name);
	noffset = fdt_next_subnode(blob,noffset);
	const char *name1 = fdt_get_name(blob, noffset, NULL);
	printf("XJH_DEBUG2:%s\n",name1);	
        #endif
	for (noffset = fdt_first_subnode(blob,node);
	     noffset >= 0;
	     noffset = fdt_next_subnode(blob, noffset)) {
	    if ( 0 == fdt_node_check_compatible(blob, noffset, "rockchip,lcd_rst")){
                err = fdtdec_decode_gpio(blob, noffset, "rockchip,gpios", &gpio_val);
                gpio_val.gpio = rk_gpio_base_to_bank(gpio_val.gpio & RK_GPIO_BANK_MASK) | (gpio_val.gpio & RK_GPIO_PIN_MASK);
                if(err < 0){    
                    screen->lcd_rst_gpio = INVALID_GPIO;
                    MIPI_SCREEN_DBG("Can't find GPIO rst\n");
                }else{
                    screen->lcd_rst_gpio = gpio_val.gpio;
                    screen->lcd_rst_atv_val = !(gpio_val.flags & OF_GPIO_ACTIVE_LOW);
	        }	    
	        screen->lcd_rst_delay = fdtdec_get_int(blob, noffset, "rockchip,delay", -1);
	        if(screen->lcd_rst_delay < 0){
		    MIPI_SCREEN_DBG("Can't get delay of rst delay\n");
	        }
	        MIPI_SCREEN_DBG("Get lcd rst gpio and delay successfully!\n");
	    }
	    if ( 0 == fdt_node_check_compatible(blob, noffset, "rockchip,lcd_en")){
	    
	        err = fdtdec_decode_gpio(blob, noffset, "rockchip,gpios", &gpio_val);
	         gpio_val.gpio = rk_gpio_base_to_bank(gpio_val.gpio & RK_GPIO_BANK_MASK) | (gpio_val.gpio & RK_GPIO_PIN_MASK);
	        if(err < 0){    
	            screen->lcd_en_gpio = INVALID_GPIO;
	            MIPI_SCREEN_DBG("Can't find GPIO en\n");
                }else{
                    screen->lcd_en_gpio = gpio_val.gpio;
                    screen->lcd_en_atv_val = !(gpio_val.flags & OF_GPIO_ACTIVE_LOW);
		}	     
		screen->lcd_en_delay = fdtdec_get_int(blob, noffset, "rockchip,delay", -1);
		if(screen->lcd_en_delay < 0){
		 MIPI_SCREEN_DBG("Can't get delay of lcd_en delay\n");
		}
		MIPI_SCREEN_DBG("Get lcd en gpio and delay successfully:delay %d!\n",screen->lcd_en_delay);
	    }
	}
    }

    /*get the initial command list*/
    node = fdtdec_next_compatible(blob, 0, COMPAT_ROCKCHIP_MIPI_SONCMDS);
    if(node < 0){
        MIPI_SCREEN_DBG("Can not get node of COMPAT_ROCKCHIP_MIPI_SONCMDS\n");
    }else{
           for (noffset = fdt_first_subnode(blob,node);
                 noffset >= 0;
		 noffset = fdt_next_subnode(blob, noffset)) {

            MIPI_SCREEN_DBG("build MIPI LCD init cmd tables\n");
	   // subnode = fdtdec_next_compatible_subnode(blob, node,
	   //			COMPAT_ROCKCHIP_MIPI_ONCMDS, &depth);
	   // if (noffset < 0)
	//	break;
            dcs_cmd = calloc(1,sizeof(struct mipi_dcs_cmd_ctr_list));
	   //node = fdt_node_offset_by_phandle(blob, handle);
            strcpy(dcs_cmd->dcs_cmd.name, fdt_get_name(blob, noffset, NULL));
	    MIPI_SCREEN_DBG("%s\n",dcs_cmd->dcs_cmd.name);
	    dcs_cmd->dcs_cmd.type = fdtdec_get_int(blob, noffset, "rockchip,cmd_type", -1);
	    MIPI_SCREEN_DBG("dcs_cmd.type=%02x\n",dcs_cmd->dcs_cmd.type);
	    dcs_cmd->dcs_cmd.dsi_id = fdtdec_get_int(blob, noffset, "rockchip,dsi_id", -1);
	    MIPI_SCREEN_DBG("dcs_cmd.dsi_id=%02x\n",dcs_cmd->dcs_cmd.dsi_id);
	    fdt_getprop(blob, noffset, "rockchip,cmd", &length);
	    dcs_cmd->dcs_cmd.cmd_len =	length / sizeof(u32) ;
	    err = fdtdec_get_int_array(blob, noffset, "rockchip,cmd", cmds, dcs_cmd->dcs_cmd.cmd_len);
	    MIPI_SCREEN_DBG("length=%d,cmd_len = %d  err = %d\n",length,dcs_cmd->dcs_cmd.cmd_len,err);
	    for(i = 0; i < (length / sizeof(u32)); i++){   
	       MIPI_SCREEN_DBG("cmd[%d]=0x%08x, ",i+1,cmds[i]);
	       dcs_cmd->dcs_cmd.cmds[i] = cmds[i];
	    }	    
	    MIPI_SCREEN_DBG("\n");
	    dcs_cmd->dcs_cmd.delay = fdtdec_get_int(blob, noffset, "rockchip,cmd_delay", -1);
	    MIPI_SCREEN_DBG("dcs_cmd.delay=%d\n",dcs_cmd->dcs_cmd.delay);
	    list_add_tail(&dcs_cmd->list, &screen->cmdlist_head);
        }
    }

    return 0; 
}
#endif /* CONFIG_OF_LIBFDT */

int rk_mipi_screen_probe(void)
{
    int ret = 0;
    gmipi_screen = calloc(1, sizeof(struct mipi_screen));
	if(!gmipi_screen) {
		printf("request struct screen fail!\n");
		return -ENOMEM;
	}
#ifdef CONFIG_OF_LIBFDT
	ret = rk_mipi_screen_init_dt(gmipi_screen);
	if(ret < 0){
		printf(" rk_mipi_screen_init_dt fail!\n");
		return -1;
	}
#endif /* CONFIG_OF_LIBFDT */

//    MIPI_SCREEN_DBG("---rk_mipi_screen_probe--end\n");

	return 0;
}

#endif /* CONFIG_RK_3288_DSI_UBOOT */
#ifdef CONFIG_LCD_MIPI
static int __init rk_mipi_screen_probe(struct platform_device *pdev)
{
	static int ret = 0;

	gmipi_screen = devm_kzalloc(&pdev->dev, sizeof(struct mipi_screen), GFP_KERNEL);
	if(!gmipi_screen) {
		dev_err(&pdev->dev,"request struct screen fail!\n");
		return -ENOMEM;
	}
	
	ret = rk_mipi_screen_init_dt(gmipi_screen);
	if(ret < 0){
		dev_err(&pdev->dev," rk_mipi_screen_init_dt fail!\n");
		return -1;
	}
	
	MIPI_SCREEN_DBG("---rk_mipi_screen_probe--end\n");

	return 0;
}

static struct platform_driver mipi_screen_platform_driver = {
	.driver = {
		.name = "rk_mipi_screen",
	},
};

static int __init rk_mipi_screen_init(void)
{
	platform_device_register_simple("rk_mipi_screen", -1, NULL, 0);
	return platform_driver_probe(&mipi_screen_platform_driver, rk_mipi_screen_probe);
}

static void __exit rk_mipi_screen_exit(void)
{
	platform_driver_unregister(&mipi_screen_platform_driver);
}

subsys_initcall_sync(rk_mipi_screen_init);
module_exit(rk_mipi_screen_exit);
#endif
