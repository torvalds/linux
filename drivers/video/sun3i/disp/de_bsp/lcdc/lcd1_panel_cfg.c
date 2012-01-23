
#include "lcd_panel_cfg.h"


static void LCD_cfg_panel_info(__panel_para_t * info)
{
    memset(info,0,sizeof(__panel_para_t));

    info->tcon_index = 0;
    info->lcd_x                   = 800;
    info->lcd_y                   = 480;
    info->lcd_dclk_freq           = 27;  //MHz
    info->lcd_pwm_freq            = 1;  //KHz
    info->lcd_srgb                = 0x00202020;
    info->lcd_swap                = 0;

    /*屏的接口配置信息*/
    info->lcd_if                  = 0;

    /*屏的HV模块相关信息*/
    info->lcd_hv_if               = 0;
    info->lcd_hv_hspw             = 48;
    info->lcd_hv_lde_iovalue      = 0;
    info->lcd_hv_lde_used         = 0;
    info->lcd_hv_smode            = 0;
    info->lcd_hv_syuv_if          = 0;
    info->lcd_hv_vspw             = 3;

    /*屏的HV配置信息*/
    info->lcd_hbp           = 40;
    info->lcd_ht            = 928;
    info->lcd_vbp           = 29;
    info->lcd_vt            = (2 * 525);

    /*屏的IO配置信息*/
    info->lcd_io_cfg0             = 0x24000000;
    info->lcd_io_cfg1             = 0x00000000;
    info->lcd_io_strength         = 0;
}

static __s32 LCD_open_flow(__u32 sel)
{
	LCD_OPEN_FUNC(sel, LCD_power_on, 10); //打开LCD供电,并延迟10ms
	LCD_OPEN_FUNC(sel, TCON_open, 200); //打开LCD控制器,并延迟200ms
	LCD_OPEN_FUNC(sel, LCD_bl_open, 0); //打开背光,并延迟0ms

	return 0;
}

static __s32 LCD_close_flow(__u32 sel)
{
	LCD_CLOSE_FUNC(sel, LCD_bl_close, 0); //关闭背光,并延迟0ms
	LCD_CLOSE_FUNC(sel, TCON_close, 0); //关闭LCD 控制器,并延迟0ms
	LCD_CLOSE_FUNC(sel, LCD_power_off, 20); //关闭LCD供电,并延迟20ms

	return 0;
}

////////////////////////////////////////   POWER   ////////////////////////////////////////////////////////////////////
static void LCD_power_on(__u32 sel)//PH27, 0 active
{
  __u32 tmp = 0;
	__lcd_panel_init_para_t para;

	LCD_get_init_para(&para);

// LCD-PWR, PH27 set to 0
    tmp = sys_get_wvalue(para.base_pioc + 0x10c);
    tmp &= 0xf7ffffff;//clear bit27
    sys_put_wvalue(para.base_pioc+0x10c, tmp);

    tmp = sys_get_wvalue(para.base_pioc+0x108);
    tmp &= 0xffff8fff;
    sys_put_wvalue(para.base_pioc+0x108,tmp | (1<<12));//bit18:16, 1:output

    tmp = sys_get_wvalue(para.base_pioc + 0x10c);
    tmp &= 0xf7ffffff;//clear bit27
    sys_put_wvalue(para.base_pioc+0x10c, tmp);
}

static void LCD_power_off(__u32 sel)//PH27,0 active
{
	__u32 tmp = 0;
	__lcd_panel_init_para_t para;

	LCD_get_init_para(&para);

// LCD-PWR, PH27 set to 1
    tmp = sys_get_wvalue(para.base_pioc + 0x10c);
    tmp |= 0x08000000;//set bit27
    sys_put_wvalue(para.base_pioc+0x10c, tmp);

    tmp = sys_get_wvalue(para.base_pioc+0x108);
    tmp &= 0xffff8fff;
    sys_put_wvalue(para.base_pioc+0x108,tmp | (1<<12));//bit18:16, 1:output

    tmp = sys_get_wvalue(para.base_pioc + 0x10c);
    tmp |= 0x08000000;//set bit27
    sys_put_wvalue(para.base_pioc+0x10c, tmp);
}
////////////////////////////////////////   back light   ////////////////////////////////////////////////////////////////////
static void LCD_bl_open(__u32 sel)
{
    __u32 tmp;
	__lcd_panel_init_para_t para;

	LCD_get_init_para(&para);

// PWM enable
    tmp = sys_get_wvalue(para.base_ccmu+0xe0);
    tmp |= (1<<4);
    sys_put_wvalue(para.base_ccmu+0xe0,tmp);

// GPIO_O_1_EN-BL, PA5 set to 1
/*
    tmp = sys_get_wvalue(para.base_pioc + 0x10);
    tmp |= 0x00000020;//set bit5
    sys_put_wvalue(para.base_pioc+0x10, tmp);

    tmp = sys_get_wvalue(para.base_pioc+0x00);
    tmp &= 0xff8fffff;
    sys_put_wvalue(para.base_pioc+0x00,tmp | (1<<20));//bit22:20, 1:output

    tmp = sys_get_wvalue(para.base_pioc + 0x10);
    tmp |= 0x00000020;//set bit5
    sys_put_wvalue(para.base_pioc+0x10, tmp);
    */
}

static void LCD_bl_close(__u32 sel)
{
    __u32 tmp;
    __lcd_panel_init_para_t para;

	LCD_get_init_para(&para);
/*
// GPIO_O_1_EN-BL, PA5 set to 0
    tmp = sys_get_wvalue(para.base_pioc + 0x10);
    tmp &= 0xffffffdf;//clear bit5
    sys_put_wvalue(para.base_pioc+0x10, tmp);

    tmp = sys_get_wvalue(para.base_pioc+0x00);
    tmp &= 0xff8fffff;
    sys_put_wvalue(para.base_pioc+0x00,tmp | (1<<20));//bit22:20, 1:output

    tmp = sys_get_wvalue(para.base_pioc + 0x10);
    tmp &= 0xffffffdf;//clear bit5
    sys_put_wvalue(para.base_pioc+0x10, tmp);
*/

// PWM disable
    tmp = sys_get_wvalue(para.base_ccmu+0xe0);
    tmp &= (~(1<<4));
    sys_put_wvalue(para.base_ccmu+0xe0,tmp);
}


void LCD_get_panel_funs_1(__lcd_panel_fun_t * fun)
{
    fun->cfg_panel_info = LCD_cfg_panel_info;
    fun->cfg_open_flow = LCD_open_flow;
    fun->cfg_close_flow = LCD_close_flow;
}
