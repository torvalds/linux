#ifndef __LCD_B080XAN02__
#define __LCD_B080XAN02__

#if defined(CONFIG_MIPI_DSI)
#include "../transmitter/mipi_dsi.h"
#endif
#include <linux/delay.h>



#define RK_SCREEN_INIT 	1

/* about mipi */
#define MIPI_DSI_LANE 4
#define MIPI_DSI_HS_CLK 528*1000000 //1000*1000000


#if defined(RK_SCREEN_INIT)
static struct rk29lcd_info *gLcd_info = NULL;

int rk_lcd_init(void) {

	u8 dcs[16] = {0};
	if(dsi_is_active() != 1)
		return -1;
	/*below is changeable*/
	dsi_enable_hs_clk(1);

	dcs[0] = LPDT;
	dcs[1] = dcs_exit_sleep_mode; 
	dsi_send_dcs_packet(dcs, 2);
	msleep(1);
	dcs[0] = LPDT;
	dcs[1] = dcs_set_display_on;
	dsi_send_dcs_packet(dcs, 2);
	msleep(10);
   	//dsi_enable_command_mode(0);
	dsi_enable_video_mode(1);
	
	printk("++++++++++++++++%s:%d\n", __func__, __LINE__);
    return 0;
}

int rk_lcd_standby(u8 enable) {

	u8 dcs[16] = {0};
	if(dsi_is_active() != 1)
		return -1;
		
	if(enable) {
		/*below is changeable*/
		dcs[0] = LPDT;
		dcs[1] = dcs_set_display_off; 
		dsi_send_dcs_packet(dcs, 2);
		msleep(1);
		dcs[0] = LPDT;
		dcs[1] = dcs_enter_sleep_mode; 
		dsi_send_dcs_packet(dcs, 2);
		msleep(1);

		printk("++++enable++++++++++++%s:%d\n", __func__, __LINE__);
	
	} else {
		/*below is changeable*/
		rk_lcd_init();
		printk("++++++++++++++++%s:%d\n", __func__, __LINE__);	
	}
    return 0;
}
#endif

#endif  
