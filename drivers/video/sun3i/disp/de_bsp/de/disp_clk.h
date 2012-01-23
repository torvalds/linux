#ifndef __DISP_CLK_H__
#define __DISP_CLK_H__

#include "disp_display_i.h"

typedef struct
{
	__u32 tve_clk;	//required clock frequency for TVE_CLK0 or TVE_CLK1, ALSO is the frequency of TVE_CLK_2x, Hz
	__u32 pre_scale;//required divide TVE_CLK by 2 for TVE_CLK_1x or NOT: 1:not divided , 2: divide by two
	__u32 pll_clk;	//required pll frequency for VIDEO_PLL0(1x) or VIDEO_PLL1(1x), Hz
	__u32 pll_2x;	//required 2x VIDEO_PLL or NOT: 0:no, 1: required

}__disp_tv_vga_clk_t;	//record tv/vga/hdmi mode clock requirement

typedef struct
{
	__disp_tv_vga_clk_t tv_clk_tab[30];	//number related to number of tv mode supported
	__disp_tv_vga_clk_t vga_clk_tab[12];//number related to number of vga mode supported

}__disp_clk_tab;



__s32 image_clk_init(__u32 sel);
__s32 image_clk_exit(__u32 sel);
__s32 image_clk_on(__u32 sel);
__s32 image_clk_off(__u32 sel);

__s32 scaler_clk_init(__u32 sel);
__s32 scaler_clk_exit(__u32 sel);
__s32 scaler_clk_on(__u32 sel);
__s32 scaler_clk_off(__u32 sel);

__s32 lcdc_clk_init(__u32 sel);
__s32 lcdc_clk_exit(__u32 sel);
__s32 lcdc_clk_on(__u32 sel);
__s32 lcdc_clk_off(__u32 sel);

__s32 tve_clk_init(void);
__s32 tve_clk_exit(void);
__s32 tve_clk_on(void);
__s32 tve_clk_off(void);

__s32 disp_pll_init(void);
__s32 disp_clk_init(void);
__s32 disp_clk_cfg(__u32 sel, __u32 type, __u8 mode);
__s32 disp_pll_assign(__u32 sel, __u32 pll_clk);
__s32 disp_pll_set(__u32 sel, __s32 videopll_sel, __u32 pll_clk, __u32 tve_clk, __s32 pre_scale, __u32 type);



#endif
