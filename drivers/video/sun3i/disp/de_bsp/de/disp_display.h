
#ifndef __DISP_DISPLAY_H__
#define __DISP_DISPLAY_H__

#include "disp_display_i.h"
#include "disp_layer.h"
#include "disp_scaler.h"

#define IMAGE_USED              0x00000004
#define IMAGE_USED_MASK         (~(IMAGE_USED))
#define YUV_CH_USED             0x00000010
#define YUV_CH_USED_MASK        (~(YUV_CH_USED))
#define HWC_USED                0x00000040
#define HWC_USED_MASK           (~(HWC_USED))
#define LCDC_TCON0_USED         0x00000080
#define LCDC_TCON0_USED_MASK    (~(LCDC_TCON0_USED))
#define LCDC_TCON1_USED         0x00000100
#define LCDC_TCON1_USED_MASK    (~(LCDC_TCON1_USED))
#define SCALER_USED             0x00000200
#define SCALER_USED_MASK        (~(SCALER_USED))

#define LCD_ON      0x00010000
#define LCD_OFF     (~(LCD_ON))
#define TV_ON       0x00020000
#define TV_OFF      (~(TV_ON))
#define HDMI_ON     0x00040000
#define HDMI_OFF    (~(HDMI_ON))
#define VGA_ON      0x00080000
#define VGA_OFF     (~(VGA_ON))

#define VIDEO_PLL0_USED	0x00100000
#define VIDEO_PLL0_USED_MASK (~(VIDEO_PLL0_USED))
#define VIDEO_PLL1_USED 0x00200000
#define VIDEO_PLL1_USED_MASK (~(VIDEO_PLL1_USED))
typedef struct disp_screen
{
    __u32                   status; /*display engine,lcd,tv,vga,hdmi status*/
    __u32                   lcdc_status;//tcon0 used, tcon1 used
    __bool                  have_cfg_reg;
    __u32                   cache_flag;
    __u32                   cfg_cnt;

    __disp_color_t          bk_color;
    __disp_colorkey_t       color_key;
    __u32                   bright;
    __u32                   contrast;
    __u32                   saturation;
    __bool                  enhance_en;
    __u32                   max_layers;
    __layer_man_t           layer_manage[4];

    __disp_output_type_t    output_type;//sw status
	__disp_vga_mode_t       vga_mode;
	__disp_tv_mode_t        tv_mode;
	__disp_tv_mode_t        hdmi_mode;
	__disp_tv_dac_source    dac_source[4];

    __s32                   (*LCD_CPUIF_XY_Swap)(__s32 mode);
    void                    (*LCD_CPUIF_ISR)(void);
	__u32	pll_use_status;	//lcdc0/lcdc1 using which video pll(0 or 1)
}__disp_screen_t;

typedef struct disp_dev
{
    __disp_bsp_init_para    init_para;//para from driver
    __disp_screen_t         screen[2];
    __disp_scaler_t         scaler[2];
}__disp_dev_t;

extern __disp_dev_t gdisp;


#endif
