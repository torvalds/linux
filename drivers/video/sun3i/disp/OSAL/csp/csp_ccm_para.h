/*
*******************************************************************************
*           				eBase
*                 the Abstract of Hardware
*
*
*              (c) Copyright 2006-2010, ALL WINNER TECH.
*           								All Rights Reserved
*
* File     :  D:\winners\eBase\eBSP\CSP\sun_20\HW_CCMU\ccm_para.h
* Date     :  2010/11/27 12:19
* By       :  Sam.Wu
* Version  :  V1.00
* Description :
* Update   :  date      author      version
*
* notes    :
*******************************************************************************
*/
#ifndef _CSP_CCM_PARA_H_
#define _CSP_CCM_PARA_H_



typedef enum {
    CSP_CCM_ERR_NONE,
    CSP_CCM_ERR_PARA_NULL,
    CSP_CCM_ERR_OSC_FREQ_CANNOT_BE_SET,
    CSP_CCM_ERR_PLL_FREQ_LOW,
    CSP_CCM_ERR_PLL_FREQ_HIGH,
    CSP_CCM_ERR_FREQ_NOT_STANDARD,
    CSP_CCM_ERR_CLK_NUM_NOT_SUPPORTED,
    CSP_CCM_ERR_DIVIDE_RATIO,
    CSP_CCM_ERR_CLK_IS_OFF,
    CSP_CCM_ERR_SRC_CLK_NOT_AVAILABLE,
    CSP_CCM_ERR_GET_CLK_FREQ,
    CSP_CCM_ERR_CLK_NO_INVALID,

    CSP_CCM_ERR_RESET_CONTROL_DENIED,
    CSP_CCM_ERR_NULL_PARA,
    CSP_CCM_ERR_PARA_VALUE,
}CSP_CCM_err_t;

/************************************************************************/
/* SYS CLK: system clocks, which are the source clocks of the chip
 * 3 kinds of system clock: oscillate, PLL output, and CPU/AHB/APB.
*Note: when the frequency of the system clock has been changed, the clock frequency of
*all the clocks sourced form it will changed. As a result, we must reconfigure the module
*clocks which  source clock is been changed!*/
/************************************************************************/
typedef enum _CSP_CCM_SYS_CLK{
    CSP_CCM_SYS_CLK_HOSC,
    CSP_CCM_SYS_CLK_LOSC,

    CSP_CCM_SYS_CLK_CORE_PLL,
    CSP_CCM_SYS_CLK_VE_PLL,
    CSP_CCM_SYS_CLK_SDRAM_PLL,
    CSP_CCM_SYS_CLK_AUDIO_PLL,
    CSP_CCM_SYS_CLK_VIDEO_PLL0,
    CSP_CCM_SYS_CLK_VIDEO_PLL1,

/*xx_NX is not needed to set, and always has (xx_NX) == N*(xx)*/
    CSP_CCM_SYS_CLK_AUDIO_PLL_4X,
    CSP_CCM_SYS_CLK_AUDIO_PLL_8X,
    CSP_CCM_SYS_CLK_VIDEO_PLL0_2X,
    CSP_CCM_SYS_CLK_VIDEO_PLL1_2X,

    CSP_CCM_SYS_CLK_CPU,
    CSP_CCM_SYS_CLK_AHB,
    CSP_CCM_SYS_CLK_APB,
    CSP_CCM_SYS_CLK_SDRAM,

    //Change the following 2 to sys clock since they
    //are internal node in the clock tree
    CSP_CCM_SYS_CLK_TVENC_0,//TVE_CLK0
    CSP_CCM_SYS_CLK_TVENC_1,//TVE_CLK1

    CSP_CCM_SYS_CLK_TOTAL_NUM
}CSP_CCM_sysClkNo_t;


typedef enum _CSP_CCM_MOD_CLK_{
    CSP_CCM_MOD_CLK_NFC,
    CSP_CCM_MOD_CLK_MSC,//Memory Stick Controller
    CSP_CCM_MOD_CLK_SDC0,
    CSP_CCM_MOD_CLK_SDC1,
    CSP_CCM_MOD_CLK_SDC2,
    CSP_CCM_MOD_CLK_SDC3,
    CSP_CCM_MOD_CLK_DE_IMAGE1,
    CSP_CCM_MOD_CLK_DE_IMAGE0,
    CSP_CCM_MOD_CLK_DE_SCALE1,
    CSP_CCM_MOD_CLK_DE_SCALE0,
    CSP_CCM_MOD_CLK_VE,
    CSP_CCM_MOD_CLK_CSI1,
    CSP_CCM_MOD_CLK_CSI0,
    CSP_CCM_MOD_CLK_IR,

    CSP_CCM_MOD_CLK_AC97,
    CSP_CCM_MOD_CLK_I2S,
    CSP_CCM_MOD_CLK_SPDIF,
    CSP_CCM_MOD_CLK_AUDIO_CODEC,
    CSP_CCM_MOD_CLK_ACE,//Audio/Compressed engine

    CSP_CCM_MOD_CLK_SS,
    CSP_CCM_MOD_CLK_TS,

    CSP_CCM_MOD_CLK_USB_PHY0,
    CSP_CCM_MOD_CLK_USB_PHY1,
    CSP_CCM_MOD_CLK_USB_PHY2,
    CSP_CCM_MOD_CLK_AVS,

    CSP_CCM_MOD_CLK_ATA,

    CSP_CCM_MOD_CLK_DE_MIX,

    CSP_CCM_MOD_CLK_KEY_PAD,
    CSP_CCM_MOD_CLK_COM,


    CSP_CCM_MOD_CLK_TVENC_1X,//TVE_CLK_1x, can source from TVE_CLK0 or TVE_CLK1
    CSP_CCM_MOD_CLK_TVENC_2X,//TVE_CLK_2x, can source from TVE_CLK0 or TVE_CLK1

    CSP_CCM_MOD_CLK_TCON0_0,
    CSP_CCM_MOD_CLK_TCON0_1,//freq is equal to TVE_CLK0 and dependent with TVENC, need only gating
    CSP_CCM_MOD_CLK_TCON1_0,
    CSP_CCM_MOD_CLK_TCON1_1,//freq is equal to TVE_CLK1 and dependent with TVENC, need only gating
    CSP_CCM_MOD_CLK_LVDS,

/*Clocks for AHB Devices*/
    CSP_CCM_MOD_CLK_AHB_USB0,  // ahb gating start boundary
    CSP_CCM_MOD_CLK_AHB_USB1,
    CSP_CCM_MOD_CLK_AHB_SS,
    CSP_CCM_MOD_CLK_AHB_ATA,
    CSP_CCM_MOD_CLK_AHB_TVENC,
    CSP_CCM_MOD_CLK_AHB_CSI0,
    CSP_CCM_MOD_CLK_AHB_DMAC,
    CSP_CCM_MOD_CLK_AHB_SDC0,
    CSP_CCM_MOD_CLK_AHB_SDC1,
    CSP_CCM_MOD_CLK_AHB_SDC2,
    CSP_CCM_MOD_CLK_AHB_SDC3,
    CSP_CCM_MOD_CLK_AHB_MSC,
    CSP_CCM_MOD_CLK_AHB_NFC,
    CSP_CCM_MOD_CLK_AHB_SDRAMC,
    CSP_CCM_MOD_CLK_AHB_TCON0,
    CSP_CCM_MOD_CLK_AHB_VE,
    CSP_CCM_MOD_CLK_AHB_BIST,
    CSP_CCM_MOD_CLK_AHB_EMAC,
    CSP_CCM_MOD_CLK_AHB_TS,
    CSP_CCM_MOD_CLK_AHB_SPI0,
    CSP_CCM_MOD_CLK_AHB_SPI1,
    CSP_CCM_MOD_CLK_AHB_SPI2,
    CSP_CCM_MOD_CLK_AHB_USB2,
    CSP_CCM_MOD_CLK_AHB_CSI1,
    CSP_CCM_MOD_CLK_AHB_COM,
    CSP_CCM_MOD_CLK_AHB_ACE,
    CSP_CCM_MOD_CLK_AHB_DE_SCALE0,
    CSP_CCM_MOD_CLK_AHB_DE_IMAGE0,
    CSP_CCM_MOD_CLK_AHB_DE_MIX,
    CSP_CCM_MOD_CLK_AHB_DE_SCALE1,
    CSP_CCM_MOD_CLK_AHB_DE_IMAGE1,
    CSP_CCM_MOD_CLK_AHB_TCON1,  // ahb gating end boundary

/*Clocks for the APB Devices*/
    CSP_CCM_MOD_CLK_APB_KEY_PAD, // apb gating start boundary
    CSP_CCM_MOD_CLK_APB_TWI2,
    CSP_CCM_MOD_CLK_APB_TWI0,
    CSP_CCM_MOD_CLK_APB_TWI1,
    CSP_CCM_MOD_CLK_APB_PIO,
    CSP_CCM_MOD_CLK_APB_UART0,
    CSP_CCM_MOD_CLK_APB_UART1,
    CSP_CCM_MOD_CLK_APB_UART2,
    CSP_CCM_MOD_CLK_APB_UART3,
    CSP_CCM_MOD_CLK_APB_AUDIO_CODEC,
    CSP_CCM_MOD_CLK_APB_IR,
    CSP_CCM_MOD_CLK_APB_I2S,
    CSP_CCM_MOD_CLK_APB_SPDIF,
    CSP_CCM_MOD_CLK_APB_AC97,
    CSP_CCM_MOD_CLK_APB_PS0,
    CSP_CCM_MOD_CLK_APB_PS1,
    CSP_CCM_MOD_CLK_APB_UART4,
    CSP_CCM_MOD_CLK_APB_UART5,
    CSP_CCM_MOD_CLK_APB_UART6,
    CSP_CCM_MOD_CLK_APB_UART7,
    CSP_CCM_MOD_CLK_APB_CAN,
    CSP_CCM_MOD_CLK_APB_SMC,//Smart card controller

/* Clocks for DRAM devices, i.e., which clock source is Dram clock*/
    CSP_CCM_MOD_CLK_SDRAM_OUTPUT,
    CSP_CCM_MOD_CLK_SDRAM_DE_SCALE0,
    CSP_CCM_MOD_CLK_SDRAM_DE_SCALE1,
    CSP_CCM_MOD_CLK_SDRAM_DE_IMAGE0,
    CSP_CCM_MOD_CLK_SDRAM_DE_IMAGE1,
    CSP_CCM_MOD_CLK_SDRAM_CSI0,
    CSP_CCM_MOD_CLK_SDRAM_CSI1,
    CSP_CCM_MOD_CLK_SDRAM_DE_MIX,
    CSP_CCM_MOD_CLK_SDRAM_VE,
    CSP_CCM_MOD_CLK_SDRAM_ACE,//Audio/Compress Engine
    CSP_CCM_MOD_CLK_SDRAM_TS,
    CSP_CCM_MOD_CLK_SDRAM_COM_ENGINE,

    CSP_CCM_MOD_CLK_TOTAL_NUM
}CSP_CCM_modClkNo_t;




#endif //#ifndef _CSP_CCM_PARA_H_




