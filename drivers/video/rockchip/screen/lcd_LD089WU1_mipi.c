#ifndef __LCD_LD089WU1__
#define __LCD_LD089WU1__

#if defined(CONFIG_MIPI_DSI)
#include "../transmitter/mipi_dsi.h"
#endif




#define RK_SCREEN_INIT 	1

/* about mipi */
#define MIPI_DSI_LANE 4
#define MIPI_DSI_HS_CLK 1000*1000000

#if defined(RK_SCREEN_INIT)
static struct rk29lcd_info *gLcd_info = NULL;

int rk_lcd_init(void) {

	u8 dcs[16] = {0};
	if(dsi_is_active() != 1)
		return -1;
		
	/*below is changeable*/
	dsi_enable_hs_clk(1);
	dsi_enable_video_mode(0);
	dsi_enable_command_mode(1);
	dcs[0] = dcs_exit_sleep_mode; 
	dsi_send_dcs_packet(dcs, 1);
	msleep(1);
	dcs[0] = dcs_set_display_on;
	dsi_send_dcs_packet(dcs, 1);
	msleep(10);
	dsi_enable_command_mode(0);
	dsi_enable_video_mode(1);
	//printk("++++++++++++++++%s:%d\n", __func__, __LINE__);
};



int rk_lcd_standby(u8 enable) {

	u8 dcs[16] = {0};
	if(dsi_is_active() != 1)
		return -1;
		
	if(enable) {
		dsi_enable_video_mode(0);
		dsi_enable_command_mode(1);
		/*below is changeable*/
		dcs[0] = dcs_set_display_off; 
		dsi_send_dcs_packet(dcs, 1);
		msleep(1);
		dcs[0] = dcs_enter_sleep_mode; 
		dsi_send_dcs_packet(dcs, 1);
		msleep(1);
		//printk("++++++++++++++++%s:%d\n", __func__, __LINE__);
	
	} else {
		/*below is changeable*/
		rk_lcd_init();
		//printk("++++++++++++++++%s:%d\n", __func__, __LINE__);
	
	}
};
#endif

#endif  
