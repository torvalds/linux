/*
********************************************************************************
*                                                   OSAL
*
*                                     (c) Copyright 2008-2009, Kevin China
*                                             				All Rights Reserved
*
* File    : OSAL_Clock.c
* By      : Sam.Wu
* Version : V1.00
* Date    : 2011/3/25 20:25
* Description :
* Update   :  date      author      version     notes
********************************************************************************
*/
#include "OSAL.h"
#include "OSAL_Clock.h"

static char* _sysClkName[CSP_CCM_SYS_CLK_TOTAL_NUM] =
{
    "hosc",
    "losc",

    "core_pll",
    "ve_pll",
    "sdram_pll",
    "audio_pll",
    "video_pll0",
    "video_pll1",

    "audio_pll_4x",
    "audio_pll_8x",
    "video_pll0_2x",
    "video_pll1_2x",

    "cpu",
    "ahb",
    "apb",
    "sdram",
    "tvenc_0",
    "tvenc_1",
};

static char* _modClkName[CSP_CCM_MOD_CLK_TOTAL_NUM] =
{
    "nfc",
    "msc",//memory stick controller
    "sdc0",
    "sdc1",
    "sdc2",
    "sdc3",
    "de_image1",
    "de_image0",
    "de_scale1",
    "de_scale0",
    "ve",
    "csi1",
    "csi0",
    "ir",

    "ac97",
    "i2s",
    "spdif",
    "audio_codec",
    "ace",//audio/compressed engine

    "ss",
    "ts",

    "usb_phy0",
    "usb_phy1",
    "usb_phy2",
    "avs",

    "ata",

    "de_mix",

    "key_pad",
    "com",

    "tvenc_1x",
    "tvenc_2x",

    "tcon0_0",
    "tcon0_1",
    "tcon1_0",
    "tcon1_1",
    "lvds",


    "ahb_usb0",
    "ahb_usb1",
    "ahb_ss",
    "ahb_ata",
    "ahb_tvenc",
    "ahb_csi0",
    "dmac",
    "ahb_sdc0",
    "ahb_sdc1",
    "ahb_sdc2",
    "ahb_sdc3",
    "ahb_msc",
    "ahb_nfc",
    "ahb_sdramc",
    "ahb_tcon0",
    "ahb_ve",
    "bist",
    "emac",
    "ahb_ts",
    "spi0",
    "spi1",
    "spi2",
    "ahb_usb2",
    "ahb_csi1",
    "ahb_com",
    "ahb_ace",
    "ahb_de_scale0",
    "ahb_de_image0",
    "ahb_de_mix",
    "ahb_de_scale1",
    "ahb_de_image1",
    "ahb_tcon1",


    "key_pad",
    "twi2",
    "twi0",
    "twi1",
    "pio",
    "uart0",
    "uart1",
    "uart2",
    "uart3",
    "apb_audio_codec",
    "apb_ir",
    "apb_i2s",
    "apb_spdif",
    "apb_ac97",
    "ps0",
    "ps1",
    "uart4",
    "uart5",
    "uart6",
    "uart7",
    "can",
    "smc",//smart card controller

    "sdram_output",
    "sdram_de_scale0",
    "sdram_de_scale1",
    "sdram_de_image0",
    "sdram_de_image1",
    "sdram_csi0",
    "sdram_csi1",
    "sdram_de_mix",
    "sdram_ve",
    "sdram_ace",//audio/compress engine
    "sdram_ts",
    "sdram_com_engine",
};


__s32 OSAL_CCMU_SetSrcFreq( CSP_CCM_sysClkNo_t nSclkNo, __u32 nFreq )
{
    struct clk* hSysClk = NULL;
    s32 retCode = -1;

    hSysClk = clk_get(NULL, _sysClkName[nSclkNo]);
    if(NULL == hSysClk){
        printk("Fail to get handle for system clock [%d].\n", nSclkNo);
        return -1;
    }
    if(nFreq == clk_get_rate(hSysClk)){
       // printk("Sys clk[%d] freq is alreay %d, not need to set.\n", nSclkNo, nFreq);
        clk_put(hSysClk);
        return 0;
    }
    retCode = clk_set_rate(hSysClk, nFreq);
    if(-1 == retCode){
        printk("Fail to set nFreq[%d] for sys clk[%d].\n", nFreq, nSclkNo);
        clk_put(hSysClk);
        return retCode;
    }
    clk_put(hSysClk);
    hSysClk = NULL;

    return retCode;
}

__u32 OSAL_CCMU_GetSrcFreq( CSP_CCM_sysClkNo_t nSclkNo )
{
    struct clk* hSysClk = NULL;
    u32 nFreq = 0;

    hSysClk = clk_get(NULL, _sysClkName[nSclkNo]);
    if(NULL == hSysClk){
        printk("Fail to get handle for system clock [%d].\n", nSclkNo);
        return -1;
    }
    nFreq = clk_get_rate(hSysClk);
    clk_put(hSysClk);
    hSysClk = NULL;

    return nFreq;
}

__hdle OSAL_CCMU_OpenMclk( __s32 nMclkNo )
{
    struct clk* hModClk = NULL;

    hModClk = clk_get(NULL, _modClkName[nMclkNo]);

    return (__hdle)hModClk;
}

__s32 OSAL_CCMU_CloseMclk( __hdle hMclk )
{
    struct clk* hModClk = (struct clk*)hMclk;

    clk_put(hModClk);

    return 0;
}

__s32 OSAL_CCMU_SetMclkSrc( __hdle hMclk, CSP_CCM_sysClkNo_t nSclkNo )
{
    struct clk* hSysClk = NULL;
    struct clk* hModClk = (struct clk*)hMclk;
    s32 retCode = -1;

    hSysClk = clk_get(NULL, _sysClkName[nSclkNo]);
    if(NULL == hSysClk){
        printk("Fail to get handle for system clock [%d].\n", nSclkNo);
        return -1;
    }
    if(clk_get_parent(hModClk) == hSysClk){
        //printk("Parent is alreay %d, not need to set.\n", nSclkNo);
        clk_put(hSysClk);
        return 0;
    }
    retCode = clk_set_parent(hModClk, hSysClk);
    if(-1 == retCode){
        printk("Fail to set parent for clk.\n");
        clk_put(hSysClk);
        return -1;
    }
    clk_put(hSysClk);

    return retCode;
}

__s32 OSAL_CCMU_GetMclkSrc( __hdle hMclk )
{
    int sysClkNo = 0;
    struct clk* hModClk = (struct clk*)hMclk;
    struct clk* hParentClk = clk_get_parent(hModClk);
    const int TOTAL_SYS_CLK = sizeof(_sysClkName)/sizeof(char*);

    for (; sysClkNo <  TOTAL_SYS_CLK; sysClkNo++)
    {
        struct clk* tmpSysClk = clk_get(NULL, _sysClkName[sysClkNo]);

        if(tmpSysClk == NULL)
        	continue;

        if(hParentClk == tmpSysClk){
            clk_put(tmpSysClk);
            break;
        }
        clk_put(tmpSysClk);
    }

    if(sysClkNo >= TOTAL_SYS_CLK){
        printk("Failed to get parent clk.\n");
        return -1;
    }

    return sysClkNo;
}

__s32 OSAL_CCMU_SetMclkDiv( __hdle hMclk, __s32 nDiv )
{
    struct clk* hModClk     = (struct clk*)hMclk;
    struct clk* hParentClk  = clk_get_parent(hModClk);
    u32         srcRate     = clk_get_rate(hParentClk);

    if(nDiv == 0){
    	return -1;
    }

    return clk_set_rate(hModClk, srcRate/nDiv);
}

__u32 OSAL_CCMU_GetMclkDiv( __hdle hMclk )
{
    struct clk* hModClk = (struct clk*)hMclk;
    struct clk* hParentClk = clk_get_parent(hModClk);
    u32 mod_freq = clk_get_rate(hModClk);

    if(mod_freq == 0){
    	return 0;
    }

    return clk_get_rate(hParentClk)/mod_freq;
}

__s32 OSAL_CCMU_MclkOnOff( __hdle hMclk, __s32 bOnOff )
{
    struct clk* hModClk = (struct clk*)hMclk;

    if(bOnOff)
    {
        return clk_enable(hModClk);
    }

    clk_disable(hModClk);

    return 0;
}

__s32 OSAL_CCMU_MclkReset(__hdle hMclk, __s32 bReset)
{
    struct clk* hModClk = (struct clk*)hMclk;

    return clk_reset(hModClk, bReset);
}

