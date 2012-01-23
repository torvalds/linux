#include "disp_display_i.h"
#include "disp_display.h"
#include "disp_clk.h"


#define CLK_ON 1
#define CLK_OFF 0
#define RST_INVAILD 0
#define RST_VAILD   1

#define CLK_DEBE0_AHB_ON	0x00000001
#define CLK_DEBE0_MOD_ON 	0x00000002
#define CLK_DEBE0_DRAM_ON	0x00000004
#define CLK_DEBE1_AHB_ON	0x00000010
#define CLK_DEBE1_MOD_ON 	0x00000020
#define CLK_DEBE1_DRAM_ON	0x00000040
#define CLK_DEFE0_AHB_ON	0x00000100
#define CLK_DEFE0_MOD_ON 	0x00000200
#define CLK_DEFE0_DRAM_ON	0x00000400
#define CLK_DEFE1_AHB_ON	0x00001000
#define CLK_DEFE1_MOD_ON 	0x00002000
#define CLK_DEFE1_DRAM_ON	0x00004000
#define CLK_LCDC0_AHB_ON	0x00010000
#define CLK_LCDC0_MOD0_ON  	0x00020000
#define CLK_LCDC0_MOD1_ON  	0x00040000
#define CLK_LCDC1_AHB_ON    0x00100000
#define CLK_LCDC1_MOD0_ON  	0x00200000
#define CLK_LCDC1_MOD1_ON  	0x00400000
#define CLK_TVE_AHB_ON		0x01000000
#define CLK_TVE_MOD1X_ON 	0x02000000
#define CLK_TVE_MOD2X_ON 	0x04000000
#define CLK_LVDS_MOD_ON 	0x10000000

#define CLK_DEBE0_AHB_OFF	(~(CLK_DEBE0_AHB_ON	    ))
#define CLK_DEBE0_MOD_OFF 	(~(CLK_DEBE0_MOD_ON 	))
#define CLK_DEBE0_DRAM_OFF	(~(CLK_DEBE0_DRAM_ON	))
#define CLK_DEBE1_AHB_OFF	(~(CLK_DEBE1_AHB_ON	    ))
#define CLK_DEBE1_MOD_OFF 	(~(CLK_DEBE1_MOD_ON 	))
#define CLK_DEBE1_DRAM_OFF	(~(CLK_DEBE1_DRAM_ON	))
#define CLK_DEFE0_AHB_OFF	(~(CLK_DEFE0_AHB_ON	    ))
#define CLK_DEFE0_MOD_OFF 	(~(CLK_DEFE0_MOD_ON 	))
#define CLK_DEFE0_DRAM_OFF	(~(CLK_DEFE0_DRAM_ON	))
#define CLK_DEFE1_AHB_OFF	(~(CLK_DEFE1_AHB_ON	    ))
#define CLK_DEFE1_MOD_OFF 	(~(CLK_DEFE1_MOD_ON 	))
#define CLK_DEFE1_DRAM_OFF	(~(CLK_DEFE1_DRAM_ON	))
#define CLK_LCDC0_AHB_OFF	(~(CLK_LCDC0_AHB_ON	    ))
#define CLK_LCDC0_MOD0_OFF  	(~(CLK_LCDC0_MOD0_ON  	))
#define CLK_LCDC0_MOD1_OFF  	(~(CLK_LCDC0_MOD1_ON  	))
#define CLK_LCDC1_AHB_OFF    (~(CLK_LCDC1_AHB_ON     ))
#define CLK_LCDC1_MOD0_OFF  	(~(CLK_LCDC1_MOD0_ON  	))
#define CLK_LCDC1_MOD1_OFF  	(~(CLK_LCDC1_MOD1_ON  	))
#define CLK_TVE_AHB_OFF		(~(CLK_TVE_AHB_ON		))
#define CLK_TVE_MOD1X_OFF 	(~(CLK_TVE_MOD1X_ON 	))
#define CLK_TVE_MOD2X_OFF 	(~(CLK_TVE_MOD2X_ON 	))
#define CLK_LVDS_MOD_OFF 	(~(CLK_LVDS_MOD_ON 	    ))

__hdle h_debe0ahbclk,h_debe0mclk,h_debe0dramclk;
__hdle h_debe1ahbclk,h_debe1mclk,h_debe1dramclk;
__hdle h_defe0ahbclk,h_defe0mclk,h_defe0dramclk;
__hdle h_defe1ahbclk,h_defe1mclk,h_defe1dramclk;
__hdle h_tveahbclk,h_tvemclk1x,h_tvemclk2x;
__hdle h_lcd0ahbclk,h_lcd0mclk0,h_lcd0mclk1;
__hdle h_lcd1ahbclk,h_lcd1mclk0,h_lcd1mclk1;
__hdle h_lvdsmclk;

__u32 g_clk_status = 0x0;

#define RESET_OSAL

extern __panel_para_t       gpanel_info[2];

__disp_clk_tab clk_tab = //record tv/vga/hdmi mode clock requirement
{
	//TV mode and HDMI mode
//     TVE_CLK   ,        PRE_SCALE,       PLL_CLK    , PLLX2 req      //       TV_VGA_MODE                     //INDEX, FOLLOW enum order
   {{27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_480I                //0x0
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_576I                //0x1
    {54000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_480P                //0x2
    {54000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_576P                //0x3
    {74250000    ,  1   ,   297000000   ,   0   },   //    DISP_TV_MOD_720P_50HZ           //0x4
    {74250000    ,  1   ,   297000000   ,   0   },   //    DISP_TV_MOD_720P_60HZ           //0x5
    {74250000    ,  1   ,   297000000   ,   0   },   //    DISP_TV_MOD_1080I_50HZ          //0x6
    {74250000    ,  1   ,   297000000   ,   0   },   //    DISP_TV_MOD_1080I_60HZ          //0x7
    {74250000    ,  1   ,   297000000   ,   0   },   //    DISP_TV_MOD_1080P_24HZ          //0x8
    {148500000   ,  1   ,   297000000   ,   0   },   //    DISP_TV_MOD_1080P_50HZ          //0x9
    {148500000   ,  1   ,   297000000   ,   0   },   //    DISP_TV_MOD_1080P_60HZ          //0xa
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_PAL                 //0xb
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_PAL_SVIDEO          //0xc
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_PAL_CVBS_SVIDEO     //0xd
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_NTSC                //0xe
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_NTSC_SVIDEO         //0xf
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_NTSC_CVBS_SVIDEO    //0x10
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_PAL_M               //0x11
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_PAL_M_SVIDEO        //0x12
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_PAL_M_CVBS_SVIDEO   //0x13
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_PAL_NC              //0x14
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_PAL_NC_SVIDEO       //0x15
    {27000000    ,  2   ,   270000000   ,   0   },   //    DISP_TV_MOD_PAL_NC_CVBS_SVIDEO  //0x16
    {       0    ,  1   ,   0           ,   0   },   //    reserved                        //0x17
    {       0    ,  1   ,   0           ,   0   },   //    reserved                        //0x18
    {       0    ,  1   ,   0           ,   0   },   //    reserved                        //0x19
    {       0    ,  1   ,   0           ,   0   },   //    reserved                        //0x1a
    {       0    ,  1   ,   0           ,   0   },   //    reserved                        //0x1b
    {       0    ,  1   ,   0           ,   0   },   //    reserved                        //0x1c
    {       0    ,  1   ,   0           ,   0   }},  //    reserved                        //0x1d
    //VGA mode
   {{147000000   ,  1   ,   294000000   ,   0   },   //    DISP_VGA_H1680_V1050            // 0X0
    {106800000   ,  1   ,   267000000   ,   1   },   //    DISP_VGA_H1440_V900             // 0X1
    {86000000    ,  1   ,   258000000   ,   0   },   //    DISP_VGA_H1360_V768             // 0X2
    {108000000   ,  1   ,   270000000   ,   1   },   //    DISP_VGA_H1280_V1024            // 0X3
    {65250000    ,  1   ,   261000000   ,   0   },   //    DISP_VGA_H1024_V768             // 0X4
    {39857143    ,  1   ,   279000000 	,   0   },   //    DISP_VGA_H800_V600              // 0X5
    {25090909    ,  1   ,   276000000 	,   0   },   //    DISP_VGA_H640_V480              // 0X6
    {       0    ,  1   ,   0           ,   0   },   //    DISP_VGA_H1440_V900_RB          // 0X7
    {       0    ,  1   ,   0           ,   0   },   //    DISP_VGA_H1680_V1050_RB         // 0X8
    {138000000   ,  1   ,   276000000   ,   0   },   //    DISP_VGA_H1920_V1080_RB         // 0X9
    {148500000   ,  1   ,   297000000   ,   0   },   //    DISP_VGA_H1920_V1080            // 0xa
	{       0    ,  1   ,   0           ,   0   }}   //   reserved				  //0xb
};


//===================disp_clk_init===================//
//description: open AHB clock for display devices
//===================================================//
__s32 disp_clk_init(void)
{
   h_debe0ahbclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_DE_IMAGE0);
   h_debe1ahbclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_DE_IMAGE1);
   h_defe0ahbclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_DE_SCALE0);
   h_defe1ahbclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_DE_SCALE1);
   h_tveahbclk	 = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_TVENC);
   h_lcd0ahbclk  = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_TCON0);
   h_lcd1ahbclk  = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_TCON1);


   OSAL_CCMU_MclkOnOff(h_debe0ahbclk, CLK_ON);
   OSAL_CCMU_MclkOnOff(h_debe1ahbclk, CLK_ON);
   OSAL_CCMU_MclkOnOff(h_defe0ahbclk, CLK_ON);
   OSAL_CCMU_MclkOnOff(h_defe1ahbclk, CLK_ON);
   OSAL_CCMU_MclkOnOff(h_tveahbclk	, CLK_ON);
   OSAL_CCMU_MclkOnOff(h_lcd0ahbclk , CLK_ON);
   OSAL_CCMU_MclkOnOff(h_lcd1ahbclk , CLK_ON);

	g_clk_status |= CLK_DEBE0_AHB_ON;
	g_clk_status |= CLK_DEBE1_AHB_ON;
	g_clk_status |= CLK_DEFE0_AHB_ON;
	g_clk_status |= CLK_DEFE1_AHB_ON;
	g_clk_status |= CLK_TVE_AHB_ON;
	g_clk_status |= CLK_LCDC0_AHB_ON;
	g_clk_status |= CLK_LCDC1_AHB_ON;

   	return DIS_SUCCESS;
}

__s32 image_clk_init(__u32 sel)
{
    __u32 dram_pll;

	if(sel == 0)
	{
		h_debe0ahbclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_DE_IMAGE0);
		h_debe0mclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_DE_IMAGE0);
		h_debe0dramclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_SDRAM_DE_IMAGE0);

		//NEW OSAL_clk reset
	#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_debe0mclk, RST_INVAILD);
	#endif
		OSAL_CCMU_SetMclkSrc(h_debe0mclk, CSP_CCM_SYS_CLK_SDRAM_PLL);	//FIX CONNECT TO VIDEO PLL0

		dram_pll = OSAL_CCMU_GetSrcFreq(CSP_CCM_SYS_CLK_SDRAM_PLL);
		if(dram_pll < 300000000)
		{
		    OSAL_CCMU_SetMclkDiv(h_debe0mclk, 1);
		}
		else
		{
		    OSAL_CCMU_SetMclkDiv(h_debe0mclk, 2);
		}
		OSAL_CCMU_MclkOnOff(h_debe0ahbclk, CLK_ON);

		OSAL_CCMU_MclkOnOff(h_debe0mclk, CLK_ON);

		g_clk_status |= (CLK_DEBE0_AHB_ON | CLK_DEBE0_MOD_ON);

	}
	else if(sel == 1)
	{
		h_debe1ahbclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_DE_IMAGE1);
		h_debe1mclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_DE_IMAGE1);
		h_debe1dramclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_SDRAM_DE_IMAGE1);
#ifdef RESET_OSAL

		OSAL_CCMU_MclkReset(h_debe1mclk, RST_INVAILD);
#endif
		OSAL_CCMU_SetMclkSrc(h_debe1mclk, CSP_CCM_SYS_CLK_SDRAM_PLL);	//FIX CONNECT TO VIDEO PLL0

		dram_pll = OSAL_CCMU_GetSrcFreq(CSP_CCM_SYS_CLK_SDRAM_PLL);
		if(dram_pll < 300000000)
		{
		    OSAL_CCMU_SetMclkDiv(h_debe1mclk, 1);
		}
		else
		{
		    OSAL_CCMU_SetMclkDiv(h_debe1mclk, 2);
		}

		OSAL_CCMU_MclkOnOff(h_debe1ahbclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_debe1mclk, CLK_ON);

		g_clk_status |= (CLK_DEBE1_AHB_ON | CLK_DEBE1_MOD_ON);
	}
	return DIS_SUCCESS;

}


__s32 image_clk_exit(__u32 sel)
{
	if(sel == 0)
	{
#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_debe0mclk, RST_VAILD);
#endif
		OSAL_CCMU_MclkOnOff(h_debe0ahbclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_debe0dramclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_debe0mclk, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_debe0ahbclk);
		OSAL_CCMU_CloseMclk(h_debe0dramclk);
		OSAL_CCMU_CloseMclk(h_debe0mclk);

		g_clk_status &= (CLK_DEBE0_AHB_OFF & CLK_DEBE0_MOD_OFF & CLK_DEBE0_DRAM_OFF);
	}
	else if(sel == 1)
	{
#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_debe1mclk, RST_VAILD);
#endif
		OSAL_CCMU_MclkOnOff(h_debe1ahbclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_debe1dramclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_debe1mclk, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_debe1ahbclk);
		OSAL_CCMU_CloseMclk(h_debe1dramclk);
		OSAL_CCMU_CloseMclk(h_debe1mclk);

		g_clk_status &= (CLK_DEBE1_AHB_OFF & CLK_DEBE1_MOD_OFF & CLK_DEBE1_DRAM_OFF);
	}

    return DIS_SUCCESS;
}

__s32 image_clk_on(__u32 sel)
{
	if(sel == 0)
	{

		OSAL_CCMU_MclkOnOff(h_debe0dramclk, CLK_ON);
		g_clk_status |= CLK_DEBE0_DRAM_ON;
	}
	else if(sel == 1)
	{

		OSAL_CCMU_MclkOnOff(h_debe1dramclk, CLK_ON);
		g_clk_status |= CLK_DEBE1_DRAM_ON;
	}
	return  DIS_SUCCESS;
}

__s32 image_clk_off(__u32 sel)
{
	if(sel == 0)
	{

		OSAL_CCMU_MclkOnOff(h_debe0dramclk, CLK_OFF);
		g_clk_status &= CLK_DEBE0_DRAM_OFF;
	}
	else if(sel == 1)
	{

		OSAL_CCMU_MclkOnOff(h_debe1dramclk, CLK_OFF);
		g_clk_status &= CLK_DEBE1_DRAM_OFF;
	}
	return  DIS_SUCCESS;
}

__s32 scaler_clk_init(__u32 sel)
{
    if(sel == 0)
   	{
   		h_defe0ahbclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_DE_SCALE0);
		h_defe0dramclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_SDRAM_DE_SCALE0);
		h_defe0mclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_DE_SCALE0);
#ifdef RESET_OSAL

		OSAL_CCMU_MclkReset(h_defe0mclk, RST_INVAILD);
#endif

		OSAL_CCMU_SetMclkSrc(h_defe0mclk, CSP_CCM_SYS_CLK_VIDEO_PLL0);	//FIX CONNECT TO VIDEO PLL0
		OSAL_CCMU_SetMclkDiv(h_defe0mclk, 1);

		OSAL_CCMU_MclkOnOff(h_defe0ahbclk, CLK_ON);

		g_clk_status |= CLK_DEFE0_AHB_ON;
	}
	else if(sel == 1)
	{
   		h_defe1ahbclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_DE_SCALE1);
		h_defe1dramclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_SDRAM_DE_SCALE1);
		h_defe1mclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_DE_SCALE1);
#ifdef RESET_OSAL

		OSAL_CCMU_MclkReset(h_defe1mclk, RST_INVAILD);
#endif
		OSAL_CCMU_SetMclkSrc(h_defe1mclk, CSP_CCM_SYS_CLK_VIDEO_PLL0);	//FIX CONNECT TO VIDEO PLL0
		OSAL_CCMU_SetMclkDiv(h_defe1mclk, 1);

		OSAL_CCMU_MclkOnOff(h_defe1ahbclk, CLK_ON);

		g_clk_status |= CLK_DEFE1_AHB_ON;
	}
	return DIS_SUCCESS;
}

__s32 scaler_clk_exit(__u32 sel)
{
	if(sel == 0)
	{
#ifdef RESET_OSAL

		OSAL_CCMU_MclkReset(h_defe0mclk, RST_VAILD);
#endif
		OSAL_CCMU_MclkOnOff(h_defe0ahbclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe0dramclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe0mclk, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_defe0ahbclk);
		OSAL_CCMU_CloseMclk(h_defe0dramclk);
		OSAL_CCMU_CloseMclk(h_defe0mclk);

		g_clk_status &= (CLK_DEFE0_AHB_OFF & CLK_DEFE0_MOD_OFF & CLK_DEFE0_DRAM_OFF);

	}
	else if(sel == 1)
	{
#ifdef RESET_OSAL

		OSAL_CCMU_MclkReset(h_defe1mclk, RST_VAILD);
#endif
		OSAL_CCMU_MclkOnOff(h_defe1ahbclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe1dramclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe1mclk, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_defe1ahbclk);
		OSAL_CCMU_CloseMclk(h_defe1dramclk);
		OSAL_CCMU_CloseMclk(h_defe1mclk);

		g_clk_status &= (CLK_DEFE1_AHB_OFF & CLK_DEFE1_MOD_OFF & CLK_DEFE1_DRAM_OFF);
	}

    return DIS_SUCCESS;
}

__s32 scaler_clk_on(__u32 sel)
{
	if(sel == 0)
	{
		OSAL_CCMU_MclkOnOff(h_defe0mclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_defe0dramclk, CLK_ON);

		g_clk_status |= ( CLK_DEFE0_MOD_ON | CLK_DEFE0_DRAM_ON);
	}
	else if(sel == 1)
	{
		OSAL_CCMU_MclkOnOff(h_defe1mclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_defe1dramclk, CLK_ON);

		g_clk_status |= ( CLK_DEFE1_MOD_ON | CLK_DEFE1_DRAM_ON);
	}
	return  DIS_SUCCESS;

}

__s32 scaler_clk_off(__u32 sel)
{
	if(sel == 0)
	{
		OSAL_CCMU_MclkOnOff(h_defe0mclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe0dramclk, CLK_OFF);

		g_clk_status &= ( CLK_DEFE0_MOD_OFF & CLK_DEFE0_DRAM_OFF);
	}
	else if(sel == 1)
	{
		OSAL_CCMU_MclkOnOff(h_defe1mclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe1dramclk, CLK_OFF);

		g_clk_status &= ( CLK_DEFE1_MOD_OFF & CLK_DEFE1_DRAM_OFF);
	}
	return	DIS_SUCCESS;

}

__s32 lcdc_clk_init(__u32 sel)
{
	if(sel == 0)
 	{
		h_lcd0ahbclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_TCON0);
		h_lcd0mclk0 = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_TCON0_0);
		h_lcd0mclk1 = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_TCON0_1);

		OSAL_CCMU_SetMclkSrc(h_lcd0mclk0, CSP_CCM_SYS_CLK_VIDEO_PLL1);  //Default to Video Pll0
		//TCON0_CLK1 only need to be gating, freqence depend on TVE_CLK0
		OSAL_CCMU_SetMclkDiv(h_lcd0mclk0, 1);

		OSAL_CCMU_MclkOnOff(h_lcd0ahbclk, CLK_ON);

		g_clk_status |= CLK_LCDC0_AHB_ON;
	}
	else if(sel == 1)
 	{
		h_lcd1ahbclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_TCON1);
		h_lcd1mclk0 = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_TCON1_0);
		h_lcd1mclk1 = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_TCON1_1);

		OSAL_CCMU_SetMclkSrc(h_lcd1mclk0, CSP_CCM_SYS_CLK_VIDEO_PLL0);  //Default to Video Pll0
		//TCON1_CLK1 only need to be gating, freqence depend on TVE_CLK1
		OSAL_CCMU_SetMclkDiv(h_lcd1mclk0, 1);

		OSAL_CCMU_MclkOnOff(h_lcd1ahbclk, CLK_ON);

		g_clk_status |= CLK_LCDC1_AHB_ON;
	}
	return DIS_SUCCESS;

}

__s32 lcdc_clk_exit(__u32 sel)
{
   	if(sel == 0)
	{
		OSAL_CCMU_MclkOnOff(h_lcd0ahbclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd0mclk0, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd0mclk1, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_lcd0ahbclk);
		OSAL_CCMU_CloseMclk(h_lcd0mclk0);
		OSAL_CCMU_CloseMclk(h_lcd0mclk1);

		g_clk_status &= (CLK_LCDC0_AHB_OFF & CLK_LCDC0_MOD0_OFF & CLK_LCDC0_MOD1_OFF);
	}
	else if(sel == 1)
	{
		OSAL_CCMU_MclkOnOff(h_lcd1ahbclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd1mclk0, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd1mclk1, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_lcd1ahbclk);
		OSAL_CCMU_CloseMclk(h_lcd1mclk0);
		OSAL_CCMU_CloseMclk(h_lcd1mclk1);

		g_clk_status &= (CLK_LCDC1_AHB_OFF & CLK_LCDC1_MOD0_OFF & CLK_LCDC1_MOD1_OFF);
	}
    return DIS_SUCCESS;
}

__s32 lcdc_clk_on(__u32 sel)
{
	if(sel == 0)
	{
		OSAL_CCMU_MclkOnOff(h_lcd0mclk0, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd0mclk1, CLK_ON);

		g_clk_status |= (CLK_LCDC0_MOD0_ON | CLK_LCDC0_MOD1_ON);
	}
	else if(sel == 1)
	{
		OSAL_CCMU_MclkOnOff(h_lcd1mclk0, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd1mclk1, CLK_ON);

		g_clk_status |= (CLK_LCDC1_MOD0_ON | CLK_LCDC1_MOD1_ON);
	}
	return  DIS_SUCCESS;

}

__s32 lcdc_clk_off(__u32 sel)
{
    if(sel == 0)
	{
		OSAL_CCMU_MclkOnOff(h_lcd0mclk0, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd0mclk1, CLK_OFF);

		g_clk_status &= (CLK_LCDC0_MOD0_OFF & CLK_LCDC0_MOD1_OFF);
	}
	else if(sel == 1)
	{
		OSAL_CCMU_MclkOnOff(h_lcd1mclk0, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd1mclk1, CLK_OFF);

		g_clk_status &= (CLK_LCDC1_MOD0_OFF & CLK_LCDC1_MOD1_OFF);
	}
	return	DIS_SUCCESS;

}

__s32 tve_clk_init(void)
{
	h_tveahbclk = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_AHB_TVENC);
	h_tvemclk1x = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_TVENC_1X);
	h_tvemclk2x = OSAL_CCMU_OpenMclk(CSP_CCM_MOD_CLK_TVENC_2X);

	OSAL_CCMU_SetMclkSrc(h_tvemclk1x, CSP_CCM_SYS_CLK_TVENC_0);
	OSAL_CCMU_SetMclkSrc(h_tvemclk2x, CSP_CCM_SYS_CLK_TVENC_0);
	OSAL_CCMU_SetMclkDiv(h_tvemclk2x, 2);	//??

	OSAL_CCMU_MclkOnOff(h_tveahbclk, CLK_ON);

	g_clk_status |= CLK_TVE_AHB_ON;

	return DIS_SUCCESS;
}

__s32 tve_clk_exit(void)
{
	OSAL_CCMU_MclkOnOff(h_tveahbclk, CLK_OFF);
	OSAL_CCMU_MclkOnOff(h_tvemclk1x, CLK_OFF);
	OSAL_CCMU_MclkOnOff(h_tvemclk2x, CLK_OFF);
	OSAL_CCMU_CloseMclk(h_tveahbclk);
	OSAL_CCMU_CloseMclk(h_tvemclk1x);
	OSAL_CCMU_CloseMclk(h_tvemclk2x);

	g_clk_status &= (CLK_TVE_AHB_OFF & CLK_TVE_MOD1X_OFF & CLK_TVE_MOD2X_OFF);

    return DIS_SUCCESS;
}


__s32 tve_clk_on(void)
{
	OSAL_CCMU_MclkOnOff(h_tvemclk1x, CLK_ON);
	OSAL_CCMU_MclkOnOff(h_tvemclk2x, CLK_ON);

	g_clk_status |= (CLK_TVE_MOD2X_ON | CLK_TVE_MOD1X_ON);

	return DIS_SUCCESS;
}

__s32 tve_clk_off(void)
{
	OSAL_CCMU_MclkOnOff(h_tvemclk1x, CLK_OFF);
	OSAL_CCMU_MclkOnOff(h_tvemclk2x, CLK_OFF);

	g_clk_status &= (CLK_TVE_MOD2X_OFF & CLK_TVE_MOD1X_OFF);

	return DIS_SUCCESS;
}


__s32 disp_pll_init(void)
{
	OSAL_CCMU_SetSrcFreq(CSP_CCM_SYS_CLK_VIDEO_PLL0, 276000000);	//fail to set 270M
	OSAL_CCMU_SetSrcFreq(CSP_CCM_SYS_CLK_VIDEO_PLL1, 297000000);
	OSAL_CCMU_SetSrcFreq(CSP_CCM_SYS_CLK_TVENC_0, 33000000); //180000000	//fail to set 180M
	OSAL_CCMU_SetSrcFreq(CSP_CCM_SYS_CLK_TVENC_1, 33000000);

	return DIS_SUCCESS;
}
static __s32 LCD_PLL_Calc(__u32 sel, __ebios_panel_para_t * info)	//所有屏的	pll频率计算，包括HV，LVDS。。
{
	__u32 lcd_dclk_freq;	//Hz
	__u32 divider;
	__u32 pll_fac;
	__u32 pll_freq;
	lcd_dclk_freq = info->lcd_dclk_freq * 1000000;
	if (info->lcd_if == 0 || info->lcd_if == 2)// hv panel and  ttl panel
	{
		if (lcd_dclk_freq > 2000000 && lcd_dclk_freq <= 300000000) //MHz
		{
			//divider = 300000000/(lcd_dclk_freq + 1500000);	//divider for dclk in tcon0
			divider = 300000000/(lcd_dclk_freq);	//divider for dclk in tcon0
			pll_freq = lcd_dclk_freq * divider;
			TCON0_set_dclk_div(sel, divider);
		}
		else
		{
			return -1;
		}
	}
	else if(info->lcd_if == 1) // lvds panel
	{
		if (lcd_dclk_freq > 2000000 && lcd_dclk_freq <= 94000000) //pixel clk
		{
			pll_fac = (lcd_dclk_freq * 7 + 3000000)/6;	//divider for tve_clkx
			pll_freq = pll_fac * 6;
		}
		else
		{
			return -1;
		}
	}
	return pll_freq;
}

__s32 disp_clk_cfg(__u32 sel, __u32 type, __u8 mode)
{	__u32 pll_freq, tve_freq = 27000000;

	__s32 videopll_sel, pre_scale = 1;

	if(type == DISP_OUTPUT_TYPE_TV || type == DISP_OUTPUT_TYPE_HDMI)
	{
		pll_freq = clk_tab.tv_clk_tab[mode].pll_clk;
		tve_freq = clk_tab.tv_clk_tab[mode].tve_clk;
		pre_scale = clk_tab.tv_clk_tab[mode].pre_scale;
	}
	else if(type == DISP_OUTPUT_TYPE_VGA)
	{
		pll_freq = clk_tab.vga_clk_tab[mode].pll_clk;
		tve_freq = clk_tab.vga_clk_tab[mode].tve_clk;
		pre_scale = clk_tab.vga_clk_tab[mode].pre_scale;
	}
	else if(type == DISP_OUTPUT_TYPE_LCD)
	{
		pll_freq = LCD_PLL_Calc(sel, (__ebios_panel_para_t*)&gpanel_info[sel]);
		if (gpanel_info[sel].lcd_if == 1)	//lvds panel
		{
			tve_freq = pll_freq/14;
		}
	}
	else
	{
	    return DIS_SUCCESS;
	}

	if ( (videopll_sel = disp_pll_assign(sel, pll_freq)) == -1)
	{
		return DIS_FAIL;
	}
	disp_pll_set(sel, videopll_sel, pll_freq, tve_freq, pre_scale, type);
	gdisp.screen[sel].pll_use_status |= ((videopll_sel == 0)?VIDEO_PLL0_USED : VIDEO_PLL1_USED);

	return DIS_SUCCESS;
}

__s32 disp_pll_assign(__u32 sel, __u32 pll_clk)
{
	__u32 another_lcdc, another_pll_use_status;
	another_lcdc = (sel == 0)? 1:0;
	another_pll_use_status = gdisp.screen[another_lcdc].pll_use_status;

	if(pll_clk >= 270000000 && pll_clk <= 300000000)
	{
		if(!(another_pll_use_status & VIDEO_PLL0_USED))	//No lcdc use PLL0
		{
			return 0;	//Video pll0 assign
		}
		else if(OSAL_CCMU_GetSrcFreq(CSP_CCM_SYS_CLK_VIDEO_PLL0) == pll_clk)	//PLL0 used by another lcdc, but freq equal to what you want to set
		{
			return 0;	//Video pll0 assign
		}
		else if(!(another_pll_use_status & VIDEO_PLL1_USED)) //Normally wont use pll0 and pll1 at the same time, unless sth wrong
		{
			return 1;	//Video pll1 assign
		}
		else	//Normally  wont jump to here
		{
			DE_WRN("Can't assign Video PLL for this device\n");
			return -1;	//fail to assign
		}
	}
	else//pll_clk not in [270, 300]MHz, must set in PLL1. So when both two devices need to set in PLL1, mostly wont work
	{
		if(!(another_pll_use_status & VIDEO_PLL1_USED))	//No lcdc use PLL0
		{
			return 1;	//Video pll1 assign
		}
		else if(OSAL_CCMU_GetSrcFreq(CSP_CCM_SYS_CLK_VIDEO_PLL1) == pll_clk)	//PLL1 used by another lcdc, but freq equal to what you want to set
		{
			return 1;	//Video pll1 assign
		}
		else	// when both two devices need to set in PLL1, mostly wont work
		{
			DE_WRN("Can't assign Video PLL for this device\n");
			return -1;	//fail to assign
		}
	}
}
__s32 disp_pll_set(__u32 sel, __s32 videopll_sel, __u32 pll_freq, __u32 tve_freq, __s32 pre_scale, __u32 type)
{
	CSP_CCM_sysClkNo_t videopll, tveclk;
	__hdle h_lcdmclk0;
	__hdle h_lcdmclk1;

	if((gpanel_info[sel].lcd_if == 1) && (type == DISP_OUTPUT_TYPE_LCD))
	{
		videopll = (videopll_sel == 0) ? CSP_CCM_SYS_CLK_VIDEO_PLL0_2X : CSP_CCM_SYS_CLK_VIDEO_PLL1_2X;
		tveclk = (sel == 0) ? CSP_CCM_SYS_CLK_TVENC_0 : CSP_CCM_SYS_CLK_TVENC_1;
		h_lcdmclk0 = (sel == 0)? h_lcd0mclk0: h_lcd1mclk0;
		h_lcdmclk1 = (sel == 0)? h_lcd0mclk1: h_lcd1mclk1;

		OSAL_CCMU_SetSrcFreq(videopll,pll_freq);	//Set related Video Pll Frequency
		OSAL_CCMU_SetMclkSrc(h_lvdsmclk, videopll);
		OSAL_CCMU_SetSrcFreq(tveclk, tve_freq);  //Set related Tve_Clk Frequency
	}
	else
	{
		videopll = (videopll_sel == 0) ? CSP_CCM_SYS_CLK_VIDEO_PLL0 : CSP_CCM_SYS_CLK_VIDEO_PLL1;
		tveclk = (sel == 0) ? CSP_CCM_SYS_CLK_TVENC_0 : CSP_CCM_SYS_CLK_TVENC_1;
		h_lcdmclk0 = (sel == 0)? h_lcd0mclk0: h_lcd1mclk0;
		h_lcdmclk1 = (sel == 0)? h_lcd0mclk1: h_lcd1mclk1;

		OSAL_CCMU_SetSrcFreq(videopll,pll_freq);	//Set related Video Pll Frequency
		OSAL_CCMU_SetMclkSrc(h_lcdmclk0, videopll);
		OSAL_CCMU_SetMclkSrc(h_lcdmclk1, tveclk);

		if(type != DISP_OUTPUT_TYPE_LCD)
		{
			OSAL_CCMU_SetSrcFreq(tveclk, tve_freq);  //Set related Tve_Clk Frequency
			if(type != DISP_OUTPUT_TYPE_HDMI)
			{
				OSAL_CCMU_SetMclkSrc(h_tvemclk1x, tveclk);
				OSAL_CCMU_SetMclkSrc(h_tvemclk2x, tveclk);
			}

			OSAL_CCMU_SetMclkDiv(h_lcdmclk1, pre_scale);
		}
	}
	return DIS_SUCCESS;
}

__s32 BSP_disp_clk_on(void)
{
	//AHB CLK
	if((g_clk_status & CLK_DEFE0_AHB_ON) == CLK_DEFE0_AHB_ON)
	{
		OSAL_CCMU_MclkOnOff(h_defe0ahbclk, CLK_ON);
	}
	if((g_clk_status & CLK_DEFE1_AHB_ON) == CLK_DEFE1_AHB_ON)
	{
		OSAL_CCMU_MclkOnOff(h_defe1ahbclk, CLK_ON);
	}
	if((g_clk_status & CLK_DEBE0_AHB_ON) == CLK_DEBE0_AHB_ON)
	{
		OSAL_CCMU_MclkOnOff(h_debe0ahbclk, CLK_ON);
	}
	if((g_clk_status & CLK_DEBE1_AHB_ON) == CLK_DEBE1_AHB_ON)
	{
		OSAL_CCMU_MclkOnOff(h_debe1ahbclk, CLK_ON);
	}
	//OSAL_CCMU_MclkOnOff(h_lcd0ahbclk, CLK_ON);
	//OSAL_CCMU_MclkOnOff(h_lcd1ahbclk, CLK_ON);
	//OSAL_CCMU_MclkOnOff(h_tveahbclk, CLK_ON);

	//DRAM CLK
	if((g_clk_status & CLK_DEFE0_DRAM_ON) == CLK_DEFE0_DRAM_ON)
	{
		OSAL_CCMU_MclkOnOff(h_defe0dramclk, CLK_ON);
	}
	if((g_clk_status & CLK_DEFE1_DRAM_ON) == CLK_DEFE1_DRAM_ON)
	{
		OSAL_CCMU_MclkOnOff(h_defe1dramclk, CLK_ON);
	}
	if((g_clk_status & CLK_DEBE0_DRAM_ON) == CLK_DEBE0_DRAM_ON)
	{
		OSAL_CCMU_MclkOnOff(h_debe0dramclk, CLK_ON);
	}
	if((g_clk_status & CLK_DEBE1_DRAM_ON) == CLK_DEBE1_DRAM_ON)
	{
		OSAL_CCMU_MclkOnOff(h_debe1dramclk, CLK_ON);
	}

	//MODULE CLK
	if((g_clk_status & CLK_DEFE0_MOD_ON) == CLK_DEFE0_MOD_ON)
	{
		OSAL_CCMU_MclkOnOff(h_defe0mclk, CLK_ON);
	}
	if((g_clk_status & CLK_DEFE1_MOD_ON) == CLK_DEFE1_MOD_ON)
	{
		OSAL_CCMU_MclkOnOff(h_defe1mclk, CLK_ON);
	}
	if((g_clk_status & CLK_DEBE0_MOD_ON) == CLK_DEBE0_MOD_ON)
	{
		OSAL_CCMU_MclkOnOff(h_debe0mclk, CLK_ON);
	}
	if((g_clk_status & CLK_DEBE1_MOD_ON) == CLK_DEBE1_MOD_ON)
	{
		OSAL_CCMU_MclkOnOff(h_debe1mclk, CLK_ON);
	}
	if((g_clk_status & CLK_LCDC0_MOD0_ON) == CLK_LCDC0_MOD0_ON)
	{
		OSAL_CCMU_MclkOnOff(h_lcd0mclk0, CLK_ON);
	}
	if((g_clk_status & CLK_LCDC0_MOD1_ON) == CLK_LCDC0_MOD1_ON)
	{
		OSAL_CCMU_MclkOnOff(h_lcd0mclk1, CLK_ON);
	}
	if((g_clk_status & CLK_LCDC1_MOD0_ON) == CLK_LCDC1_MOD0_ON)
	{
		OSAL_CCMU_MclkOnOff(h_lcd1mclk0, CLK_ON);
	}
	if((g_clk_status & CLK_LCDC1_MOD1_ON) == CLK_LCDC1_MOD1_ON)
	{
		OSAL_CCMU_MclkOnOff(h_lcd1mclk1, CLK_ON);
	}
	if((g_clk_status & CLK_TVE_MOD1X_ON) == CLK_TVE_MOD1X_ON)
	{
		OSAL_CCMU_MclkOnOff(h_tvemclk1x, CLK_ON);
	}
	if((g_clk_status & CLK_TVE_MOD2X_ON) == CLK_TVE_MOD2X_ON)
	{
		OSAL_CCMU_MclkOnOff(h_tvemclk2x, CLK_ON);
	}


	return DIS_SUCCESS;
}

__s32 BSP_disp_clk_off(void)
{
	//AHB CLK
	if((g_clk_status & CLK_DEFE0_AHB_ON) == CLK_DEFE0_AHB_ON)
	{
		OSAL_CCMU_MclkOnOff(h_defe0ahbclk, CLK_OFF);
	}
	if((g_clk_status & CLK_DEFE1_AHB_ON) == CLK_DEFE1_AHB_ON)
	{
		OSAL_CCMU_MclkOnOff(h_defe1ahbclk, CLK_OFF);
	}
	if((g_clk_status & CLK_DEBE0_AHB_ON) == CLK_DEBE0_AHB_ON)
	{
		OSAL_CCMU_MclkOnOff(h_debe0ahbclk, CLK_OFF);
	}
	if((g_clk_status & CLK_DEBE1_AHB_ON) == CLK_DEBE1_AHB_ON)
	{
		OSAL_CCMU_MclkOnOff(h_debe1ahbclk, CLK_OFF);
	}
	//OSAL_CCMU_MclkOnOff(h_lcd0ahbclk, CLK_OFF);
	//OSAL_CCMU_MclkOnOff(h_lcd1ahbclk, CLK_OFF);
	//OSAL_CCMU_MclkOnOff(h_tveahbclk, CLK_OFF);

	//DRAM CLK
	if((g_clk_status & CLK_DEFE0_DRAM_ON) == CLK_DEFE0_DRAM_ON)
	{
		OSAL_CCMU_MclkOnOff(h_defe0dramclk, CLK_OFF);
	}
	if((g_clk_status & CLK_DEFE1_DRAM_ON) == CLK_DEFE1_DRAM_ON)
	{
		OSAL_CCMU_MclkOnOff(h_defe1dramclk, CLK_OFF);
	}
	if((g_clk_status & CLK_DEBE0_DRAM_ON) == CLK_DEBE0_DRAM_ON)
	{
		OSAL_CCMU_MclkOnOff(h_debe0dramclk, CLK_OFF);
	}
	if((g_clk_status & CLK_DEBE1_DRAM_ON) == CLK_DEBE1_DRAM_ON)
	{
		OSAL_CCMU_MclkOnOff(h_debe1dramclk, CLK_OFF);
	}

	//MODULE CLK
	if((g_clk_status & CLK_DEFE0_MOD_ON) == CLK_DEFE0_MOD_ON)
	{
		OSAL_CCMU_MclkOnOff(h_defe0mclk, CLK_OFF);
	}
	if((g_clk_status & CLK_DEFE1_MOD_ON) == CLK_DEFE1_MOD_ON)
	{
		OSAL_CCMU_MclkOnOff(h_defe1mclk, CLK_OFF);
	}
	if((g_clk_status & CLK_DEBE0_MOD_ON) == CLK_DEBE0_MOD_ON)
	{
		OSAL_CCMU_MclkOnOff(h_debe0mclk, CLK_OFF);
	}
	if((g_clk_status & CLK_DEBE1_MOD_ON) == CLK_DEBE1_MOD_ON)
	{
		OSAL_CCMU_MclkOnOff(h_debe1mclk, CLK_OFF);
	}
	if((g_clk_status & CLK_LCDC0_MOD0_ON) == CLK_LCDC0_MOD0_ON)
	{
		OSAL_CCMU_MclkOnOff(h_lcd0mclk0, CLK_OFF);
	}
	if((g_clk_status & CLK_LCDC0_MOD1_ON) == CLK_LCDC0_MOD1_ON)
	{
		OSAL_CCMU_MclkOnOff(h_lcd0mclk1, CLK_OFF);
	}
	if((g_clk_status & CLK_LCDC1_MOD0_ON) == CLK_LCDC1_MOD0_ON)
	{
		OSAL_CCMU_MclkOnOff(h_lcd1mclk0, CLK_OFF);
	}
	if((g_clk_status & CLK_LCDC1_MOD1_ON) == CLK_LCDC1_MOD1_ON)
	{
		OSAL_CCMU_MclkOnOff(h_lcd1mclk1, CLK_OFF);
	}
	if((g_clk_status & CLK_TVE_MOD1X_ON) == CLK_TVE_MOD1X_ON)
	{
		OSAL_CCMU_MclkOnOff(h_tvemclk1x, CLK_OFF);
	}
	if((g_clk_status & CLK_TVE_MOD2X_ON) == CLK_TVE_MOD2X_ON)
	{
		OSAL_CCMU_MclkOnOff(h_tvemclk2x, CLK_OFF);
	}

	return DIS_SUCCESS;
}


