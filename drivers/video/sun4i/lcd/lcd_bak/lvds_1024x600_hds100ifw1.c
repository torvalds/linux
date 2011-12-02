/**********************************************************************
 *
 *  lvds_1024x600_hds100ifw1.c
 *	
 **********************************************************************/

#include "lcd_panel_cfg.h"

//delete this line if you want to use the lcd para define in sys_config1.fex
#define LCD_PARA_USE_CONFIG

#ifdef LCD_PARA_USE_CONFIG

/**********************************************************************
 *
 *  tcon parameters
 *	
 **********************************************************************/
static void LCD_cfg_panel_info(__panel_para_t * info)
{ 
    memset(info,0,sizeof(__panel_para_t));

	//interface
    info->lcd_if            = 3;        	//0:hv; 		1:cpu/8080; 	2:reserved; 	3:lvds
    info->lcd_lvds_ch       = 0;        	//0:single link	1:dual link
    info->lcd_lvds_bitwidth = 1; 			//0:24bit;		1:18bit; 
    
    //timing                       	
    info->lcd_x             = 1024;			//Hor Pixels
    info->lcd_y             = 600;			//Ver Pixels
    info->lcd_dclk_freq     = 52;       	//Pixel Data Cycle,in MHz
    info->lcd_ht            = 1344;     	//Hor Total Time
    info->lcd_hbp           = 20;      		//Hor Back Porch
    info->lcd_vt            = 635*2;  		//Ver Total Time*2
    info->lcd_vbp           = 20;       	//Ver Back Porch

    info->lcd_hv_hspw       = 10;       	//Hor Sync Time
    info->lcd_hv_vspw       = 10;       	//Ver Sync Time
    info->lcd_io_cfg0       = 0x00000000;	//Clock Phase
    
	//color
    info->lcd_frm           = 1;        	//0: direct; 	1: rgb666 dither;	2:rgb656 dither
    info->lcd_gamma_correction_en = 0;

    info->lcd_pwm_not_used  = 0;
    info->lcd_pwm_ch        = 0;
    info->lcd_pwm_freq      = 12500;		//Hz
    info->lcd_pwm_pol       = 0;

}
#endif


/**********************************************************************
 *
 *  lcd flow function
 *	
 **********************************************************************/
static __s32 LCD_open_flow(__u32 sel)
{
	LCD_OPEN_FUNC(sel, LCD_power_on,	50);   //open lcd power, than delay 50ms
//	LCD_OPEN_FUNC(sel, LCD_panel_init,	50);   //not in need
	LCD_OPEN_FUNC(sel, TCON_open,		500);  //open lcd controller, than delay 500ms
	LCD_OPEN_FUNC(sel, LCD_bl_open, 	0);    //open lcd backlight, than delay 0ms

	return 0;
}

static __s32 LCD_close_flow(__u32 sel)
{	
	LCD_CLOSE_FUNC(sel, LCD_bl_close, 0);	   //close lcd backlight, and delay 0ms
	LCD_CLOSE_FUNC(sel, TCON_close, 0); 	   //close lcd controller, and delay 0ms
//	LCD_CLOSE_FUNC(sel, LCD_panel_exit, 0);    //not in need
	LCD_CLOSE_FUNC(sel, LCD_power_off, 1000);  //close lcd power, and delay 1000ms

	return 0;
}

/**********************************************************************
 *
 *  lcd step function
 *	
 **********************************************************************/
static void LCD_power_on(__u32 sel)
{
    LCD_POWER_EN(sel, 1);						
}

static void LCD_power_off(__u32 sel)
{
    LCD_POWER_EN(sel, 0);						
}

static void LCD_bl_open(__u32 sel)
{
    LCD_PWM_EN(sel, 1);							
    LCD_BL_EN(sel, 1);						
}

static void LCD_bl_close(__u32 sel)
{
    LCD_BL_EN(sel, 0);
    LCD_PWM_EN(sel, 0);							
}

/**********************************************************************
 *
 *  user define function
 *
 **********************************************************************/
static __s32 LCD_user_defined_func(__u32 sel, __u32 para1, __u32 para2, __u32 para3)
{
    return 0;
}

/**********************************************************************
 *
 *  do not modify
 *
 **********************************************************************/
void LCD_get_panel_funs_0(__lcd_panel_fun_t * fun)
{
#ifdef LCD_PARA_USE_CONFIG
    fun->cfg_panel_info = LCD_cfg_panel_info;
#endif
    fun->cfg_open_flow = LCD_open_flow;
    fun->cfg_close_flow = LCD_close_flow;
    fun->lcd_user_defined_func = LCD_user_defined_func;
}

