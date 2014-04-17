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

#include "../transmitter/mipi_dsi.h"
#include <linux/delay.h>

#if 0
#define	MIPI_SCREEN_DBG(x...)	printk(KERN_ERR x)
#else
#define	MIPI_SCREEN_DBG(x...)  
#endif

struct mipi_screen *gmipi_screen;

static void rk_mipi_screen_pwr_disable(struct mipi_screen *screen)
{   
    if(screen->lcd_en_gpio != INVALID_GPIO){
        gpio_direction_output(screen->lcd_en_gpio, !screen->lcd_en_atv_val);
        mdelay(screen->lcd_en_delay);
    }
    else{
        MIPI_SCREEN_DBG("lcd_en_gpio is null");
    }
    
    if(screen->lcd_rst_gpio != INVALID_GPIO){     

        gpio_direction_output(screen->lcd_rst_gpio, !screen->lcd_rst_atv_val);
        mdelay(screen->lcd_rst_delay);
    }
    else{
        MIPI_SCREEN_DBG("lcd_rst_gpio is null");
    }    
}


static void rk_mipi_screen_pwr_enable(struct mipi_screen *screen)
{   
    if(screen->lcd_en_gpio != INVALID_GPIO){
        gpio_direction_output(screen->lcd_en_gpio, screen->lcd_en_atv_val);
        mdelay(screen->lcd_en_delay);
    }
    else{
        MIPI_SCREEN_DBG("lcd_en_gpio is null\n");
    }
    
    if(screen->lcd_rst_gpio != INVALID_GPIO){     

        mdelay (screen->lcd_rst_delay);
        gpio_direction_output(screen->lcd_rst_gpio, screen->lcd_rst_atv_val);
        mdelay(screen->lcd_rst_delay);
    }
    else{
        MIPI_SCREEN_DBG("lcd_rst_gpio is null\n");
    }    
}

static void rk_mipi_screen_cmd_init(struct mipi_screen *screen)
{
    u8 len, i; 
    u8 cmds[25] = {0}, tempcmds[25] = {0};
    struct list_head *screen_pos;
    struct mipi_dcs_cmd_ctr_list  *dcs_cmd;
    
    list_for_each(screen_pos, &screen->cmdlist_head){
    
        dcs_cmd = list_entry(screen_pos, struct mipi_dcs_cmd_ctr_list, list);
        len = dcs_cmd->dcs_cmd.cmd_len + 1;
        
        for( i = 1; i < len ; i++){
            cmds[i] = dcs_cmd->dcs_cmd.cmds[i-1];
            tempcmds[i] = cmds[i]; 
            }
            printk("dcs_cmd.name:%s\n",dcs_cmd->dcs_cmd.name);
        if(dcs_cmd->dcs_cmd.type == LPDT){
            cmds[0] = LPDT;
            tempcmds[0] = LPDT;
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
                dsi_send_packet(1, tempcmds, len);

            }
            else{
                MIPI_SCREEN_DBG("dsi is err.\n");
            }
            mdelay(dcs_cmd->dcs_cmd.delay);
            
        }
        else if(dcs_cmd->dcs_cmd.type == HSDT){
            cmds[0] = HSDT;
            tempcmds[0] = HSDT;
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
                dsi_send_packet(1, tempcmds, len);
            }
            else{
                MIPI_SCREEN_DBG("dsi is err.");
            }
            mdelay(dcs_cmd->dcs_cmd.delay);
            
        }
        else
            MIPI_SCREEN_DBG("cmd type err.\n");
    }
}

int rk_mipi_screen(void) 
{
    u8 dcs[16] = {0};
	u8 rk_dsi_num = gmipi_screen->mipi_dsi_num;
	
	if(gmipi_screen->screen_init == 0){
	
		dsi_enable_hs_clk(0,1);
		if(rk_dsi_num == 2){
			dsi_enable_hs_clk(1, 1);
		}
		
		dcs[0] = LPDT;
		dcs[1] = DTYPE_DCS_SWRITE_0P;
		dcs[2] = dcs_exit_sleep_mode; 
		dsi_send_packet(0, dcs, 3);
		if(rk_dsi_num ==2)   
            dsi_send_packet(1, dcs, 3);
			
		msleep(10);
		
		dcs[0] = LPDT;
		dcs[1] = DTYPE_DCS_SWRITE_0P;
		dcs[2] = dcs_set_display_on; 
		dsi_send_packet(0, dcs, 3);
		if(rk_dsi_num ==2)
            dsi_send_packet(1, dcs, 3);   

		msleep(10);
		
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
	u8 dcs[16] = {0};
	u8 rk_dsi_num = 0;
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

static int rk_mipi_screen_init_dt(struct mipi_screen *screen)
{
    struct device_node *childnode, *grandchildnode,*root;
    struct mipi_dcs_cmd_ctr_list  *dcs_cmd;
    struct list_head *pos;
    struct property *prop;
    enum of_gpio_flags flags;
    u32 value,i,debug,gpio,ret,cmds[25],length;

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
            
            MIPI_SCREEN_DBG(
            "lcd->lcd_rst_gpio=%d,dsi->lcd_rst_atv_val=%d\n",screen->lcd_rst_gpio,screen->lcd_rst_atv_val);
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
            MIPI_SCREEN_DBG(
                    "dsi->lcd_en_gpio=%d, dsi->screen.lcd_en_atv_val=%d\n",screen->lcd_en_gpio,screen->lcd_en_atv_val);
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
                    if((value < 0) || ( value > 2)) {
                        printk("err: rockchip,dsi_id not match.\n");
                    }
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
            if(ret) {
                MIPI_SCREEN_DBG("%s: Can not read property: %s--->cmd_delay\n", __func__,childnode->name);
            }
            else{
                dcs_cmd->dcs_cmd.delay = value;
            }

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

int rk_mipi_get_dsi_num(void)
{
    return gmipi_screen->mipi_dsi_num;
}
EXPORT_SYMBOL(rk_mipi_get_dsi_num);

int rk_mipi_get_dsi_lane(void)
{
    return gmipi_screen->dsi_lane;
}
EXPORT_SYMBOL(rk_mipi_get_dsi_lane);


int rk_mipi_get_dsi_clk(void)
{
    return gmipi_screen->hs_tx_clk;
}
EXPORT_SYMBOL(rk_mipi_get_dsi_clk);

static int __init rk_mipi_screen_probe(struct platform_device *pdev)
{
    int ret = 0;

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
