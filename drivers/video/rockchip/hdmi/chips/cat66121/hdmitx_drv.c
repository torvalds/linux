///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <hdmitx_drv.c>
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2012/12/20
//   @fileversion: ITE_HDMITX_SAMPLE_3.14
//******************************************/

/////////////////////////////////////////////////////////////////////
// HDMITX.C
// Driver code for platform independent
/////////////////////////////////////////////////////////////////////
#include "hdmitx.h"
#include "hdmitx_drv.h"
#define FALLING_EDGE_TRIGGER

#define MSCOUNT 1000
#define LOADING_UPDATE_TIMEOUT (3000/32)    // 3sec
// USHORT u8msTimer = 0 ;
// USHORT TimerServF = TRUE ;

//////////////////////////////////////////////////////////////////////
// Authentication status
//////////////////////////////////////////////////////////////////////

// #define TIMEOUT_WAIT_AUTH MS(2000)

HDMITXDEV hdmiTxDev[HDMITX_MAX_DEV_COUNT] ;

#ifndef INV_INPUT_PCLK
#define PCLKINV 0
#else
#define PCLKINV B_TX_VDO_LATCH_EDGE
#endif

#ifndef INV_INPUT_ACLK
    #define InvAudCLK 0
#else
    #define InvAudCLK B_TX_AUDFMT_FALL_EDGE_SAMPLE_WS
#endif

#define INIT_CLK_HIGH
// #define INIT_CLK_LOW

_CODE RegSetEntry HDMITX_Init_Table[] = {

    {0x0F, 0x40, 0x00},

    {0x62, 0x08, 0x00},
    {0x64, 0x04, 0x00},
    {0x01,0x00,0x00},//idle(100);

    {0x04, 0x20, 0x20},
    {0x04, 0x1D, 0x1D},
    {0x01,0x00,0x00},//idle(100);
    {0x0F, 0x01, 0x00}, // bank 0 ;
    #ifdef INIT_CLK_LOW
        {0x62, 0x90, 0x10},
        {0x64, 0x89, 0x09},
        {0x68, 0x10, 0x10},
    #endif

    {0xD1, 0x0E, 0x0C},
    {0x65, 0x03, 0x00},
    #ifdef NON_SEQUENTIAL_YCBCR422 // for ITE HDMIRX
        {0x71, 0xFC, 0x1C},
    #else
        {0x71, 0xFC, 0x18},
    #endif

    {0x8D, 0xFF, CEC_I2C_SLAVE_ADDR},
    {0x0F, 0x08, 0x08},

    {0xF8,0xFF,0xC3},
    {0xF8,0xFF,0xA5},
    {0x20, 0x80, 0x80},
    {0x37, 0x01, 0x00},
    {0x20, 0x80, 0x00},
    {0xF8,0xFF,0xFF},

    {0x59, 0xD8, 0x40|PCLKINV},
    {0xE1, 0x20, InvAudCLK},
    {0x05, 0xC0, 0x40},
    {REG_TX_INT_MASK1, 0xFF, ~(B_TX_RXSEN_MASK|B_TX_HPD_MASK)},
    {REG_TX_INT_MASK2, 0xFF, ~(B_TX_KSVLISTCHK_MASK|B_TX_AUTH_DONE_MASK|B_TX_AUTH_FAIL_MASK)},
    {REG_TX_INT_MASK3, 0xFF, ~(0x0)},
    {0x0C, 0xFF, 0xFF},
    {0x0D, 0xFF, 0xFF},
    {0x0E, 0x03, 0x03},

    {0x0C, 0xFF, 0x00},
    {0x0D, 0xFF, 0x00},
    {0x0E, 0x02, 0x00},
    {0x09, 0x03, 0x00}, // Enable HPD and RxSen Interrupt
    {0,0,0}
};

_CODE RegSetEntry HDMITX_DefaultVideo_Table[] = {

    ////////////////////////////////////////////////////
    // Config default output format.
    ////////////////////////////////////////////////////
    {0x72, 0xff, 0x00},
    {0x70, 0xff, 0x00},
#ifndef DEFAULT_INPUT_YCBCR
// GenCSC\RGB2YUV_ITU709_16_235.c
    {0x72, 0xFF, 0x02},
    {0x73, 0xFF, 0x00},
    {0x74, 0xFF, 0x80},
    {0x75, 0xFF, 0x00},
    {0x76, 0xFF, 0xB8},
    {0x77, 0xFF, 0x05},
    {0x78, 0xFF, 0xB4},
    {0x79, 0xFF, 0x01},
    {0x7A, 0xFF, 0x93},
    {0x7B, 0xFF, 0x00},
    {0x7C, 0xFF, 0x49},
    {0x7D, 0xFF, 0x3C},
    {0x7E, 0xFF, 0x18},
    {0x7F, 0xFF, 0x04},
    {0x80, 0xFF, 0x9F},
    {0x81, 0xFF, 0x3F},
    {0x82, 0xFF, 0xD9},
    {0x83, 0xFF, 0x3C},
    {0x84, 0xFF, 0x10},
    {0x85, 0xFF, 0x3F},
    {0x86, 0xFF, 0x18},
    {0x87, 0xFF, 0x04},
#else
// GenCSC\YUV2RGB_ITU709_16_235.c
    {0x0F, 0x01, 0x00},
    {0x72, 0xFF, 0x03},
    {0x73, 0xFF, 0x00},
    {0x74, 0xFF, 0x80},
    {0x75, 0xFF, 0x00},
    {0x76, 0xFF, 0x00},
    {0x77, 0xFF, 0x08},
    {0x78, 0xFF, 0x53},
    {0x79, 0xFF, 0x3C},
    {0x7A, 0xFF, 0x89},
    {0x7B, 0xFF, 0x3E},
    {0x7C, 0xFF, 0x00},
    {0x7D, 0xFF, 0x08},
    {0x7E, 0xFF, 0x51},
    {0x7F, 0xFF, 0x0C},
    {0x80, 0xFF, 0x00},
    {0x81, 0xFF, 0x00},
    {0x82, 0xFF, 0x00},
    {0x83, 0xFF, 0x08},
    {0x84, 0xFF, 0x00},
    {0x85, 0xFF, 0x00},
    {0x86, 0xFF, 0x87},
    {0x87, 0xFF, 0x0E},
#endif
    // 2012/12/20 added by Keming's suggestion test
    {0x88, 0xF0, 0x00},
    //~jauchih.tseng@ite.com.tw
    {0x04, 0x08, 0x00},
    {0,0,0}
};
_CODE RegSetEntry HDMITX_SetHDMI_Table[] = {

    ////////////////////////////////////////////////////
    // Config default HDMI Mode
    ////////////////////////////////////////////////////
    {0xC0, 0x01, 0x01},
    {0xC1, 0x03, 0x03},
    {0xC6, 0x03, 0x03},
    {0,0,0}
};

_CODE RegSetEntry HDMITX_SetDVI_Table[] = {

    ////////////////////////////////////////////////////
    // Config default HDMI Mode
    ////////////////////////////////////////////////////
    {0x0F, 0x01, 0x01},
    {0x58, 0xFF, 0x00},
    {0x0F, 0x01, 0x00},
    {0xC0, 0x01, 0x00},
    {0xC1, 0x03, 0x02},
    {0xC6, 0x03, 0x00},
    {0,0,0}
};

_CODE RegSetEntry HDMITX_DefaultAVIInfo_Table[] = {

    ////////////////////////////////////////////////////
    // Config default avi infoframe
    ////////////////////////////////////////////////////
    {0x0F, 0x01, 0x01},
    {0x58, 0xFF, 0x10},
    {0x59, 0xFF, 0x08},
    {0x5A, 0xFF, 0x00},
    {0x5B, 0xFF, 0x00},
    {0x5C, 0xFF, 0x00},
    {0x5D, 0xFF, 0x57},
    {0x5E, 0xFF, 0x00},
    {0x5F, 0xFF, 0x00},
    {0x60, 0xFF, 0x00},
    {0x61, 0xFF, 0x00},
    {0x62, 0xFF, 0x00},
    {0x63, 0xFF, 0x00},
    {0x64, 0xFF, 0x00},
    {0x65, 0xFF, 0x00},
    {0x0F, 0x01, 0x00},
    {0xCD, 0x03, 0x03},
    {0,0,0}
};
_CODE RegSetEntry HDMITX_DeaultAudioInfo_Table[] = {

    ////////////////////////////////////////////////////
    // Config default audio infoframe
    ////////////////////////////////////////////////////
    {0x0F, 0x01, 0x01},
    {0x68, 0xFF, 0x00},
    {0x69, 0xFF, 0x00},
    {0x6A, 0xFF, 0x00},
    {0x6B, 0xFF, 0x00},
    {0x6C, 0xFF, 0x00},
    {0x6D, 0xFF, 0x71},
    {0x0F, 0x01, 0x00},
    {0xCE, 0x03, 0x03},

    {0,0,0}
};

_CODE RegSetEntry HDMITX_Aud_CHStatus_LPCM_20bit_48Khz[] =
{
    {0x0F, 0x01, 0x01},
    {0x33, 0xFF, 0x00},
    {0x34, 0xFF, 0x18},
    {0x35, 0xFF, 0x00},
    {0x91, 0xFF, 0x00},
    {0x92, 0xFF, 0x00},
    {0x93, 0xFF, 0x01},
    {0x94, 0xFF, 0x00},
    {0x98, 0xFF, 0x02},
    {0x99, 0xFF, 0xDA},
    {0x0F, 0x01, 0x00},
    {0,0,0}//end of table
} ;

_CODE RegSetEntry HDMITX_AUD_SPDIF_2ch_24bit[] =
{
    {0x0F, 0x11, 0x00},
    {0x04, 0x14, 0x04},
    {0xE0, 0xFF, 0xD1},
    {0xE1, 0xFF, 0x01},
    {0xE2, 0xFF, 0xE4},
    {0xE3, 0xFF, 0x10},
    {0xE4, 0xFF, 0x00},
    {0xE5, 0xFF, 0x00},
    {0x04, 0x14, 0x00},
    {0,0,0}//end of table
} ;

_CODE RegSetEntry HDMITX_AUD_I2S_2ch_24bit[] =
{
    {0x0F, 0x11, 0x00},
    {0x04, 0x14, 0x04},
    {0xE0, 0xFF, 0xC1},
    {0xE1, 0xFF, 0x01},
    {0xE2, 0xFF, 0xE4},
    {0xE3, 0xFF, 0x00},
    {0xE4, 0xFF, 0x00},
    {0xE5, 0xFF, 0x00},
    {0x04, 0x14, 0x00},
    {0,0,0}//end of table
} ;

_CODE RegSetEntry HDMITX_DefaultAudio_Table[] = {

    ////////////////////////////////////////////////////
    // Config default audio output format.
    ////////////////////////////////////////////////////
    {0x0F, 0x21, 0x00},
    {0x04, 0x14, 0x04},
    {0xE0, 0xFF, 0xC1},
    {0xE1, 0xFF, 0x01},
    {0xE2, 0xFF, 0xE4},
    {0xE3, 0xFF, 0x00},
    {0xE4, 0xFF, 0x00},
    {0xE5, 0xFF, 0x00},
    {0x0F, 0x01, 0x01},
    {0x33, 0xFF, 0x00},
    {0x34, 0xFF, 0x18},
    {0x35, 0xFF, 0x00},
    {0x91, 0xFF, 0x00},
    {0x92, 0xFF, 0x00},
    {0x93, 0xFF, 0x01},
    {0x94, 0xFF, 0x00},
    {0x98, 0xFF, 0x02},
    {0x99, 0xFF, 0xDB},
    {0x0F, 0x01, 0x00},
    {0x04, 0x14, 0x00},

    {0x00, 0x00, 0x00} // End of Table.
} ;

_CODE RegSetEntry HDMITX_PwrDown_Table[] = {
     // Enable GRCLK
     {0x0F, 0x40, 0x00},
     // PLL Reset
     {0x61, 0x10, 0x10},   // DRV_RST
     {0x62, 0x08, 0x00},   // XP_RESETB
     {0x64, 0x04, 0x00},   // IP_RESETB
     {0x01, 0x00, 0x00}, // idle(100);

     // PLL PwrDn
     {0x61, 0x20, 0x20},   // PwrDn DRV
     {0x62, 0x44, 0x44},   // PwrDn XPLL
     {0x64, 0x40, 0x40},   // PwrDn IPLL

     // HDMITX PwrDn
     {0x05, 0x01, 0x01},   // PwrDn PCLK
     {0x0F, 0x78, 0x78},   // PwrDn GRCLK
     {0x00, 0x00, 0x00} // End of Table.
};

_CODE RegSetEntry HDMITX_PwrOn_Table[] = {
    {0x0F, 0x78, 0x38},   // PwrOn GRCLK
    {0x05, 0x01, 0x00},   // PwrOn PCLK

    // PLL PwrOn
    {0x61, 0x20, 0x00},   // PwrOn DRV
    {0x62, 0x44, 0x00},   // PwrOn XPLL
    {0x64, 0x40, 0x00},   // PwrOn IPLL

    // PLL Reset OFF
    {0x61, 0x10, 0x00},   // DRV_RST
    {0x62, 0x08, 0x08},   // XP_RESETB
    {0x64, 0x04, 0x04},   // IP_RESETB
    {0x0F, 0x78, 0x08},   // PwrOn IACLK
    {0x00, 0x00, 0x00} // End of Table.
};

#ifdef DETECT_VSYNC_CHG_IN_SAV
BOOL EnSavVSync = FALSE ;
#endif
static bool PowerStatus=FALSE;

//////////////////////////////////////////////////////////////////////
// Function Prototype
//////////////////////////////////////////////////////////////////////
void hdmitx_LoadRegSetting(RegSetEntry table[]);

void HDMITX_InitTxDev(HDMITXDEV *pInstance)
{
	if(pInstance && 0 < HDMITX_MAX_DEV_COUNT)
	{
		hdmiTxDev[0] = *pInstance ;
	}
}

void InitHDMITX()
{
    hdmitx_LoadRegSetting(HDMITX_Init_Table);
//    HDMITX_WriteI2C_Byte(REG_TX_PLL_CTRL,0xff);
    hdmiTxDev[0].bIntPOL = (hdmiTxDev[0].bIntType&B_TX_INTPOL_ACTH)?TRUE:FALSE ;

    // Avoid power loading in un play status.
	//////////////////////////////////////////////////////////////////
	// Setup HDCP ROM
	//////////////////////////////////////////////////////////////////
#ifdef HDMITX_INPUT_INFO
    hdmiTxDev[0].RCLK = CalcRCLK();
#endif
    hdmitx_LoadRegSetting(HDMITX_DefaultVideo_Table);
    hdmitx_LoadRegSetting(HDMITX_SetHDMI_Table);
    hdmitx_LoadRegSetting(HDMITX_DefaultAVIInfo_Table);
    hdmitx_LoadRegSetting(HDMITX_DeaultAudioInfo_Table);
    hdmitx_LoadRegSetting(HDMITX_Aud_CHStatus_LPCM_20bit_48Khz);
    hdmitx_LoadRegSetting(HDMITX_AUD_SPDIF_2ch_24bit);
    HDMITX_PowerDown();

    HDMITX_DEBUG_PRINTF((
        "-----------------------------------------------------\n"
        "Init HDMITX\n"
        "-----------------------------------------------------\n"));

    DumpHDMITXReg();
}

BOOL getHDMITX_LinkStatus()
{
    if(B_TX_RXSENDETECT & HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS))
    {
        if(0==HDMITX_ReadI2C_Byte(REG_TX_AFE_DRV_CTRL))
        {
            //HDMITX_DEBUG_PRINTF(("getHDMITX_LinkStatus()!!\n") );
            return TRUE;
        }
    }
    //HDMITX_DEBUG_PRINTF(("GetTMDS not Ready()!!\n") );

    return FALSE;
}

#if 0
BYTE CheckHDMITX(BYTE *pHPD,BYTE *pHPDChange)
{
    BYTE intdata1,intdata2,intdata3,sysstat;
    BYTE  intclr3 = 0 ;
    BYTE PrevHPD = hdmiTxDev[0].bHPD ;
    BYTE HPD ;
    sysstat = HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS);
	// HDMITX_DEBUG_PRINTF(("REG_TX_SYS_STATUS = %X \n",sysstat));

	if((sysstat & (B_TX_HPDETECT/*|B_TX_RXSENDETECT*/)) == (B_TX_HPDETECT/*|B_TX_RXSENDETECT*/))
	{
    	HPD = TRUE;
    }
	else
	{
	    HPD = FALSE;
	}
    // CheckClockStable(sysstat);
    // 2007/06/20 added by jj_tseng@chipadvanced.com

    if(pHPDChange)
    {
    	*pHPDChange = (HPD!=PrevHPD)?TRUE:FALSE ; // default give pHPDChange value compared to previous HPD value.

    }
    //~jj_tseng@chipadvanced.com 2007/06/20

    if(HPD==FALSE)
    {
        hdmiTxDev[0].bAuthenticated = FALSE ;
    }
    if(sysstat & B_TX_INT_ACTIVE)
    {
		HDMITX_DEBUG_PRINTF(("REG_TX_SYS_STATUS = 0x%02X \n",(int)sysstat));

        intdata1 = HDMITX_ReadI2C_Byte(REG_TX_INT_STAT1);
        HDMITX_DEBUG_PRINTF(("INT_Handler: reg%X = %X\n",(int)REG_TX_INT_STAT1,(int)intdata1));
        if(intdata1 & B_TX_INT_AUD_OVERFLOW)
        {
            HDMITX_DEBUG_PRINTF(("B_TX_INT_AUD_OVERFLOW.\n"));
            HDMITX_OrReg_Byte(REG_TX_SW_RST,(B_HDMITX_AUD_RST|B_TX_AREF_RST));
            HDMITX_AndReg_Byte(REG_TX_SW_RST,~(B_HDMITX_AUD_RST|B_TX_AREF_RST));
            //AudioDelayCnt=AudioOutDelayCnt;
            //LastRefaudfreqnum=0;
        }
		if(intdata1 & B_TX_INT_DDCFIFO_ERR)
		{
		    HDMITX_DEBUG_PRINTF(("DDC FIFO Error.\n"));
		    hdmitx_ClearDDCFIFO();
		    hdmiTxDev[0].bAuthenticated= FALSE ;
		}
		if(intdata1 & B_TX_INT_DDC_BUS_HANG)
		{
		    HDMITX_DEBUG_PRINTF(("DDC BUS HANG.\n"));
            hdmitx_AbortDDC();

            if(hdmiTxDev[0].bAuthenticated)
            {
                HDMITX_DEBUG_PRINTF(("when DDC hang,and aborted DDC,the HDCP authentication need to restart.\n"));
                #ifdef SUPPORT_HDCP
                hdmitx_hdcp_ResumeAuthentication();
                #endif
            }
		}
		if(intdata1 & (B_TX_INT_HPD_PLUG/*|B_TX_INT_RX_SENSE*/))
		{

            if(pHPDChange)
            {
				*pHPDChange = TRUE ;
			}
            if(HPD == FALSE)
            {
                /*
                HDMITX_WriteI2C_Byte(REG_TX_SW_RST,B_TX_AREF_RST|B_HDMITX_VID_RST|B_HDMITX_AUD_RST|B_TX_HDCP_RST_HDMITX);
                delay1ms(1);
                HDMITX_WriteI2C_Byte(REG_TX_AFE_DRV_CTRL,B_TX_AFE_DRV_RST|B_TX_AFE_DRV_PWD);
                */
                //HDMITX_DEBUG_PRINTF(("Unplug,%x %x\n",(int)HDMITX_ReadI2C_Byte(REG_TX_SW_RST),(int)HDMITX_ReadI2C_Byte(REG_TX_AFE_DRV_CTRL)));
            }
		}
		if(intdata1 & (B_TX_INT_RX_SENSE))
		{
            hdmiTxDev[0].bAuthenticated = FALSE;
		}
        intdata2 = HDMITX_ReadI2C_Byte(REG_TX_INT_STAT2);
        HDMITX_DEBUG_PRINTF(("INT_Handler: reg%X = %X\n",(int)REG_TX_INT_STAT2,(int)intdata2));

		#ifdef SUPPORT_HDCP
		if(intdata2 & B_TX_INT_AUTH_DONE)
		{
            HDMITX_DEBUG_PRINTF(("interrupt Authenticate Done.\n"));
            HDMITX_OrReg_Byte(REG_TX_INT_MASK2,(BYTE)B_TX_AUTH_DONE_MASK);
            //hdmiTxDev[0].bAuthenticated = TRUE ;
            //setHDMITX_AVMute(FALSE);
		}
		if(intdata2 & B_TX_INT_AUTH_FAIL)
		{
		    hdmiTxDev[0].bAuthenticated = FALSE;
            //HDMITX_DEBUG_PRINTF(("interrupt Authenticate Fail.\n"));
			hdmitx_AbortDDC();   // @emily add
            //hdmitx_hdcp_ResumeAuthentication();
        }
        #endif // SUPPORT_HDCP

#if 1
		intdata3 = HDMITX_ReadI2C_Byte(REG_TX_INT_STAT3);
        HDMITX_DEBUG_PRINTF(("INT_Handler: reg%X = %X\n",(int)REG_TX_INT_STAT3,(int)intdata3));
		if(intdata3 & B_TX_INT_VIDSTABLE)
		{
			sysstat = HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS);
			if(sysstat & B_TXVIDSTABLE)
			{
				hdmitx_FireAFE();
			}
		}
#endif
        intdata3= HDMITX_ReadI2C_Byte(0xEE);
        if( intdata3 )
        {
            HDMITX_WriteI2C_Byte(0xEE,intdata3); // clear ext interrupt ;
            HDMITX_DEBUG_PRINTF(("%s%s%s%s%s%s%s\n",
                (intdata3&0x40)?"video parameter change \n":"",
                (intdata3&0x20)?"HDCP Pj check done \n":"",
                (intdata3&0x10)?"HDCP Ri check done \n":"",
                (intdata3&0x8)? "DDC bus hang \n":"",
                (intdata3&0x4)? "Video input FIFO auto reset \n":"",
                (intdata3&0x2)? "No audio input interrupt  \n":"",
                (intdata3&0x1)? "Audio decode error interrupt \n":""));
        }

	intclr3 = (HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS))|B_TX_CLR_AUD_CTS | B_TX_INTACTDONE ;
	HDMITX_WriteI2C_Byte(REG_TX_SYS_STATUS,intclr3); // clear interrupt.
	HDMITX_WriteI2C_Byte(REG_TX_INT_CLR0,0xFF);
	HDMITX_WriteI2C_Byte(REG_TX_INT_CLR1,0xFF);
	intclr3 &= ~(B_TX_INTACTDONE);
	HDMITX_WriteI2C_Byte(REG_TX_SYS_STATUS,intclr3); // INTACTDONE reset to zero.
    }
    //
    // else
    // {
    //     if(pHPDChange)
    //     {
    // 	    if(HPD != PrevHPD)
    // 	    {
    //             *pHPDChange = TRUE;
    //         }
    //         else
    //         {
    //            *pHPDChange = FALSE;
    //         }
    //     }
    // }
    if(pHPDChange)
    {
        if((*pHPDChange==TRUE) &&(HPD==FALSE))
        {
            HDMITX_WriteI2C_Byte(REG_TX_AFE_DRV_CTRL,B_TX_AFE_DRV_RST|B_TX_AFE_DRV_PWD);
        }
    }
    if(pHPD)
    {
         *pHPD = HPD    ;
    }
    hdmiTxDev[0].bHPD = HPD ;
    return HPD ;
}
#endif
void HDMITX_PowerOn()
{
	PowerStatus = TRUE;
    hdmitx_LoadRegSetting(HDMITX_PwrOn_Table);
}

void HDMITX_PowerDown()
{
	PowerStatus = FALSE;
    hdmitx_LoadRegSetting(HDMITX_PwrDown_Table);
}
BOOL getHDMI_PowerStatus()
{
	return PowerStatus;
}

void setHDMITX_AVMute(BYTE bEnable)
{
    Switch_HDMITX_Bank(0);
    HDMITX_SetI2C_Byte(REG_TX_GCP,B_TX_SETAVMUTE, bEnable?B_TX_SETAVMUTE:0 );
    HDMITX_WriteI2C_Byte(REG_TX_PKT_GENERAL_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT);
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_LoadRegSetting()
// Input: RegSetEntry SettingTable[] ;
// Return: N/A
// Remark: if an entry {0, 0, 0} will be terminated.
//////////////////////////////////////////////////////////////////////

void hdmitx_LoadRegSetting(RegSetEntry table[])
{
    int i ;

    for( i = 0 ;  ; i++ )
    {
        if( table[i].offset == 0 && table[i].invAndMask == 0 && table[i].OrMask == 0 )
        {
            return ;
        }
        else if( table[i].invAndMask == 0 && table[i].OrMask == 0 )
        {
            HDMITX_DEBUG_PRINTF2(("delay(%d)\n",(int)table[i].offset));
            delay1ms(table[i].offset);
        }
        else if( table[i].invAndMask == 0xFF )
        {
            HDMITX_DEBUG_PRINTF2(("HDMITX_WriteI2C_Byte(%02x,%02x)\n",(int)table[i].offset,(int)table[i].OrMask));
            HDMITX_WriteI2C_Byte(table[i].offset,table[i].OrMask);
        }
        else
        {
            HDMITX_DEBUG_PRINTF2(("HDMITX_SetI2C_Byte(%02x,%02x,%02x)\n",(int)table[i].offset,(int)table[i].invAndMask,(int)table[i].OrMask));
            HDMITX_SetI2C_Byte(table[i].offset,table[i].invAndMask,table[i].OrMask);
        }
    }
}

///*****************************************
//   @file   <hdmitx_ddc.c>
//******************************************/

BOOL getHDMITX_EDIDBlock(int EDIDBlockID,BYTE *pEDIDData)
{
	if(!pEDIDData)
	{
		return FALSE ;
	}
    if(getHDMITX_EDIDBytes(pEDIDData,EDIDBlockID/2,(EDIDBlockID%2)*128,128) == ER_FAIL)
    {
        return FALSE ;
    }
#if Debug_message
    {
	    int j=0;
	    EDID_DEBUG_PRINTF(("------BlockID=%d------\n",EDIDBlockID));
	    for( j = 0 ; j < 128 ; j++ )
	    {
		    EDID_DEBUG_PRINTF(("%02X%c",(int)pEDIDData[j],(7 == (j&7))?'\n':' '));
	    }
    }
#endif
    return TRUE ;
}

//////////////////////////////////////////////////////////////////////
// Function: getHDMITX_EDIDBytes
// Parameter: pData - the pointer of buffer to receive EDID ucdata.
//            bSegment - the segment of EDID readback.
//            offset - the offset of EDID ucdata in the segment. in byte.
//            count - the read back bytes count,cannot exceed 32
// Return: ER_SUCCESS if successfully getting EDID. ER_FAIL otherwise.
// Remark: function for read EDID ucdata from reciever.
// Side-Effect: DDC master will set to be HOST. DDC FIFO will be used and dirty.
//////////////////////////////////////////////////////////////////////

SYS_STATUS getHDMITX_EDIDBytes(BYTE *pData,BYTE bSegment,BYTE offset,SHORT Count)
{
    SHORT RemainedCount,ReqCount ;
    BYTE bCurrOffset ;
    SHORT TimeOut ;
    BYTE *pBuff = pData ;
    BYTE ucdata ;

    // HDMITX_DEBUG_PRINTF(("getHDMITX_EDIDBytes(%08lX,%d,%d,%d)\n",(ULONG)pData,(int)bSegment,(int)offset,(int)Count));
    if(!pData)
    {
//        HDMITX_DEBUG_PRINTF(("getHDMITX_EDIDBytes(): Invallid pData pointer %08lX\n",(ULONG)pData));
        return ER_FAIL ;
    }
    if(HDMITX_ReadI2C_Byte(REG_TX_INT_STAT1) & B_TX_INT_DDC_BUS_HANG)
    {
        HDMITX_DEBUG_PRINTF(("Called hdmitx_AboutDDC()\n"));
        hdmitx_AbortDDC();

    }
    // HDMITX_OrReg_Byte(REG_TX_INT_CTRL,(1<<1));

    hdmitx_ClearDDCFIFO();

    RemainedCount = Count ;
    bCurrOffset = offset ;

    Switch_HDMITX_Bank(0);

    while(RemainedCount > 0)
    {

        ReqCount = (RemainedCount > DDC_FIFO_MAXREQ)?DDC_FIFO_MAXREQ:RemainedCount ;
        HDMITX_DEBUG_PRINTF(("getHDMITX_EDIDBytes(): ReqCount = %d,bCurrOffset = %d\n",(int)ReqCount,(int)bCurrOffset));

        HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERDDC|B_TX_MASTERHOST);
        HDMITX_WriteI2C_Byte(REG_TX_DDC_CMD,CMD_FIFO_CLR);

        for(TimeOut = 0 ; TimeOut < 200 ; TimeOut++)
        {
            ucdata = HDMITX_ReadI2C_Byte(REG_TX_DDC_STATUS);

            if(ucdata&B_TX_DDC_DONE)
            {
                break ;
            }
            if((ucdata & B_TX_DDC_ERROR)||(HDMITX_ReadI2C_Byte(REG_TX_INT_STAT1) & B_TX_INT_DDC_BUS_HANG))
            {
                HDMITX_DEBUG_PRINTF(("Called hdmitx_AboutDDC()\n"));
                hdmitx_AbortDDC();
                return ER_FAIL ;
            }
        }
        HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERDDC|B_TX_MASTERHOST);
        HDMITX_WriteI2C_Byte(REG_TX_DDC_HEADER,DDC_EDID_ADDRESS); // for EDID ucdata get
        HDMITX_WriteI2C_Byte(REG_TX_DDC_REQOFF,bCurrOffset);
        HDMITX_WriteI2C_Byte(REG_TX_DDC_REQCOUNT,(BYTE)ReqCount);
        HDMITX_WriteI2C_Byte(REG_TX_DDC_EDIDSEG,bSegment);
        HDMITX_WriteI2C_Byte(REG_TX_DDC_CMD,CMD_EDID_READ);

        bCurrOffset += ReqCount ;
        RemainedCount -= ReqCount ;

        for(TimeOut = 250 ; TimeOut > 0 ; TimeOut --)
        {
            delay1ms(1);
            ucdata = HDMITX_ReadI2C_Byte(REG_TX_DDC_STATUS);
            if(ucdata & B_TX_DDC_DONE)
            {
                break ;
            }
            if(ucdata & B_TX_DDC_ERROR)
            {
                HDMITX_DEBUG_PRINTF(("getHDMITX_EDIDBytes(): DDC_STATUS = %02X,fail.\n",(int)ucdata));
                // HDMITX_AndReg_Byte(REG_TX_INT_CTRL,~(1<<1));
                return ER_FAIL ;
            }
        }
        if(TimeOut == 0)
        {
            HDMITX_DEBUG_PRINTF(("getHDMITX_EDIDBytes(): DDC TimeOut %d . \n",(int)ucdata));
            // HDMITX_AndReg_Byte(REG_TX_INT_CTRL,~(1<<1));
            return ER_FAIL ;
        }
        do
        {
            *(pBuff++) = HDMITX_ReadI2C_Byte(REG_TX_DDC_READFIFO);
            ReqCount -- ;
        }while(ReqCount > 0);

    }
    // HDMITX_AndReg_Byte(REG_TX_INT_CTRL,~(1<<1));
    return ER_SUCCESS ;
}

/////////////////////////////
// DDC Function.
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_ClearDDCFIFO
// Parameter: N/A
// Return: N/A
// Remark: clear the DDC FIFO.
// Side-Effect: DDC master will set to be HOST.
//////////////////////////////////////////////////////////////////////

void hdmitx_ClearDDCFIFO()
{
    HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERDDC|B_TX_MASTERHOST);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_CMD,CMD_FIFO_CLR);
}

void hdmitx_GenerateDDCSCLK()
{
    HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERDDC|B_TX_MASTERHOST);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_CMD,CMD_GEN_SCLCLK);
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_AbortDDC
// Parameter: N/A
// Return: N/A
// Remark: Force abort DDC and reset DDC bus.
// Side-Effect:
//////////////////////////////////////////////////////////////////////

void hdmitx_AbortDDC()
{
    BYTE CPDesire,SWReset,DDCMaster ;
    BYTE uc, timeout, i ;
    // save the SW reset,DDC master,and CP Desire setting.
    SWReset = HDMITX_ReadI2C_Byte(REG_TX_SW_RST);
    CPDesire = HDMITX_ReadI2C_Byte(REG_TX_HDCP_DESIRE);
    DDCMaster = HDMITX_ReadI2C_Byte(REG_TX_DDC_MASTER_CTRL);

    HDMITX_WriteI2C_Byte(REG_TX_HDCP_DESIRE,CPDesire&(~B_TX_CPDESIRE)); // @emily change order
    HDMITX_WriteI2C_Byte(REG_TX_SW_RST,SWReset|B_TX_HDCP_RST_HDMITX);         // @emily change order
    HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERDDC|B_TX_MASTERHOST);

    // 2009/01/15 modified by Jau-Chih.Tseng@ite.com.tw
    // do abort DDC twice.
    for( i = 0 ; i < 2 ; i++ )
    {
        HDMITX_WriteI2C_Byte(REG_TX_DDC_CMD,CMD_DDC_ABORT);

        for( timeout = 0 ; timeout < 200 ; timeout++ )
        {
            uc = HDMITX_ReadI2C_Byte(REG_TX_DDC_STATUS);
            if (uc&B_TX_DDC_DONE)
            {
                break ; // success
            }
            if( uc & (B_TX_DDC_NOACK|B_TX_DDC_WAITBUS|B_TX_DDC_ARBILOSE) )
            {
//                HDMITX_DEBUG_PRINTF(("hdmitx_AbortDDC Fail by reg16=%02X\n",(int)uc));
                break ;
            }
            delay1ms(1); // delay 1 ms to stable.
        }
    }
    //~Jau-Chih.Tseng@ite.com.tw

}

///*****************************************
//   @file   <hdmitx_vid.c>
//******************************************/

extern HDMITXDEV hdmiTxDev[HDMITX_MAX_DEV_COUNT] ;

void WaitTxVidStable(void);
void hdmitx_SetInputMode(BYTE InputMode,BYTE bInputSignalType);
void hdmitx_SetCSCScale(BYTE bInputMode,BYTE bOutputMode);
void hdmitx_SetupAFE(VIDEOPCLKLEVEL PCLKLevel);
void hdmitx_FireAFE(void);

//////////////////////////////////////////////////////////////////////
// utility function for main..
//////////////////////////////////////////////////////////////////////

#ifndef DISABLE_HDMITX_CSC
    #if (defined (SUPPORT_OUTPUTYUV)) && (defined (SUPPORT_INPUTRGB))
        extern _CODE BYTE bCSCMtx_RGB2YUV_ITU601_16_235[] ;
        extern _CODE BYTE bCSCMtx_RGB2YUV_ITU601_0_255[] ;
        extern _CODE BYTE bCSCMtx_RGB2YUV_ITU709_16_235[] ;
        extern _CODE BYTE bCSCMtx_RGB2YUV_ITU709_0_255[] ;
    #endif

    #if (defined (SUPPORT_OUTPUTRGB)) && (defined (SUPPORT_INPUTYUV))
        extern _CODE BYTE bCSCMtx_YUV2RGB_ITU601_16_235[] ;
        extern _CODE BYTE bCSCMtx_YUV2RGB_ITU601_0_255[] ;
        extern _CODE BYTE bCSCMtx_YUV2RGB_ITU709_16_235[] ;
        extern _CODE BYTE bCSCMtx_YUV2RGB_ITU709_0_255[] ;

    #endif
#endif// DISABLE_HDMITX_CSC

//////////////////////////////////////////////////////////////////////
// Function Body.
//////////////////////////////////////////////////////////////////////

void HDMITX_DisableVideoOutput()
{
    BYTE uc = HDMITX_ReadI2C_Byte(REG_TX_SW_RST) | B_HDMITX_VID_RST ;
    HDMITX_WriteI2C_Byte(REG_TX_SW_RST,uc);
    HDMITX_WriteI2C_Byte(REG_TX_AFE_DRV_CTRL,B_TX_AFE_DRV_RST|B_TX_AFE_DRV_PWD);
    HDMITX_SetI2C_Byte(0x62, 0x90, 0x00);
    HDMITX_SetI2C_Byte(0x64, 0x89, 0x00);
}

BOOL HDMITX_EnableVideoOutput(VIDEOPCLKLEVEL level,BYTE inputColorMode,BYTE outputColorMode,BYTE bHDMI)
{
    // bInputVideoMode,bOutputVideoMode,hdmiTxDev[0].bInputVideoSignalType,bAudioInputType,should be configured by upper F/W or loaded from EEPROM.
    // should be configured by initsys.c
    // VIDEOPCLKLEVEL level ;
    switch(level)
    {
	    case PCLK_HIGH:
		    HDMITX_WriteI2C_Byte(REG_TX_PLL_CTRL,0x30 /*0xff*/);
		    break ;
	    default:
		    HDMITX_WriteI2C_Byte(REG_TX_PLL_CTRL,0x00);
            break ;
    }
    HDMITX_WriteI2C_Byte(REG_TX_SW_RST,B_HDMITX_VID_RST|B_HDMITX_AUD_RST|B_TX_AREF_RST|B_TX_HDCP_RST_HDMITX);

    hdmiTxDev[0].bHDMIMode = (BYTE)bHDMI ;
    // 2009/12/09 added by jau-chih.tseng@ite.com.tw
    Switch_HDMITX_Bank(1);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB1,0x00);
    Switch_HDMITX_Bank(0);
    //~jau-chih.tseng@ite.com.tw

    if(hdmiTxDev[0].bHDMIMode)
    {
        setHDMITX_AVMute(TRUE);
    }
    hdmitx_SetInputMode(inputColorMode,hdmiTxDev[0].bInputVideoSignalType);

    hdmitx_SetCSCScale(inputColorMode,outputColorMode);

    if(hdmiTxDev[0].bHDMIMode)
    {
        HDMITX_WriteI2C_Byte(REG_TX_HDMI_MODE,B_TX_HDMI_MODE);
    }
    else
    {
        HDMITX_WriteI2C_Byte(REG_TX_HDMI_MODE,B_TX_DVI_MODE);
    }
#ifdef INVERT_VID_LATCHEDGE
    uc = HDMITX_ReadI2C_Byte(REG_TX_CLK_CTRL1);
    uc |= B_TX_VDO_LATCH_EDGE ;
    HDMITX_WriteI2C_Byte(REG_TX_CLK_CTRL1, uc);
#endif

    hdmitx_SetupAFE(level); // pass if High Freq request
    HDMITX_WriteI2C_Byte(REG_TX_SW_RST,          B_HDMITX_AUD_RST|B_TX_AREF_RST|B_TX_HDCP_RST_HDMITX);

    hdmitx_FireAFE();

	return TRUE ;
}

//////////////////////////////////////////////////////////////////////
// export this for dynamic change input signal
//////////////////////////////////////////////////////////////////////
BOOL setHDMITX_VideoSignalType(BYTE inputSignalType)
{
	hdmiTxDev[0].bInputVideoSignalType = inputSignalType ;
    // hdmitx_SetInputMode(inputColorMode,hdmiTxDev[0].bInputVideoSignalType);
    return TRUE ;
}

void WaitTxVidStable()
{
#if 0
    BYTE i ;
    for( i = 0 ; i < 20 ; i++ )
    {
        delay1ms(15);
        if((HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS) & B_TXVIDSTABLE) == 0 )
        {
            continue ;
        }
        delay1ms(15);
        if((HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS) & B_TXVIDSTABLE) == 0 )
        {
            continue ;
        }
        delay1ms(15);
        if((HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS) & B_TXVIDSTABLE) == 0 )
        {
            continue ;
        }
        delay1ms(15);
        if((HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS) & B_TXVIDSTABLE) == 0 )
        {
            continue ;
        }
        break ;
    }
#endif
}
// void CheckClockStable(BYTE SystemStat)
// {
//     static BYTE Stablecnt=20;
//     if(0==(SystemStat&B_TXVIDSTABLE))
//     {
//         if(0==Stablecnt--)
//         {
//             HDMITX_ToggleBit(0x59,3);
//             Stablecnt=20;
//         }
//     }
//     else
//     {
//         Stablecnt=20;
//     }
// }

void setHDMITX_ColorDepthPhase(BYTE ColorDepth,BYTE bPhase)
{
#ifdef IT6615
    BYTE uc ;
    BYTE bColorDepth ;

    if(ColorDepth == 30)
    {
        bColorDepth = B_TX_CD_30 ;
        HDMITX_DEBUG_PRINTF(("bColorDepth = B_TX_CD_30\n"));
    }
    else if (ColorDepth == 36)
    {
        bColorDepth = B_TX_CD_36 ;
        HDMITX_DEBUG_PRINTF(("bColorDepth = B_TX_CD_36\n"));
    }
    /*
    else if (ColorDepth == 24)
    {
        bColorDepth = B_TX_CD_24 ;
        //bColorDepth = 0 ;//modify JJ by mail 20100423 1800 // not indicated
    }
    */
    else
    {
        bColorDepth = 0 ; // not indicated
    }
    Switch_HDMITX_Bank(0);
    HDMITX_SetI2C_Byte(REG_TX_GCP,B_TX_COLOR_DEPTH_MASK ,bColorDepth);
	HDMITX_DEBUG_PRINTF(("setHDMITX_ColorDepthPhase(%02X), regC1 = %02X\n",(int)bColorDepth,(int)HDMITX_ReadI2C_Byte(REG_TX_GCP))) ;
#endif
}

#ifdef SUPPORT_SYNCEMBEDDED

struct CRT_TimingSetting {
	BYTE fmt;
    WORD HActive;
    WORD VActive;
    WORD HTotal;
    WORD VTotal;
    WORD H_FBH;
    WORD H_SyncW;
    WORD H_BBH;
    WORD V_FBH;
    WORD V_SyncW;
    WORD V_BBH;
    BYTE Scan:1;
    BYTE VPolarity:1;
    BYTE HPolarity:1;
};

//   VDEE_L,   VDEE_H, VRS2S_L, VRS2S_H, VRS2E_L, VRS2E_H, HalfL_L, HalfL_H, VDE2S_L, VDE2S_H, HVP&Progress
_CODE struct CRT_TimingSetting TimingTable[] =
{
    //  VIC   H     V    HTotal VTotal  HFT   HSW     HBP VF VSW   VB
    {  1,  640,  480,    800,  525,   16,    96,    48, 10, 2,  33,      PROG, Vneg, Hneg},// 640x480@60Hz         - CEA Mode [ 1]
    {  2,  720,  480,    858,  525,   16,    62,    60,  9, 6,  30,      PROG, Vneg, Hneg},// 720x480@60Hz         - CEA Mode [ 2]
    {  3,  720,  480,    858,  525,   16,    62,    60,  9, 6,  30,      PROG, Vneg, Hneg},// 720x480@60Hz         - CEA Mode [ 3]
    {  4, 1280,  720,   1650,  750,  110,    40,   220,  5, 5,  20,      PROG, Vpos, Hpos},// 1280x720@60Hz        - CEA Mode [ 4]
    {  5, 1920,  540,   2200,  562,   88,    44,   148,  2, 5,  15, INTERLACE, Vpos, Hpos},// 1920x1080(I)@60Hz    - CEA Mode [ 5]
    {  6,  720,  240,    858,  262,   19,    62,    57,  4, 3,  15, INTERLACE, Vneg, Hneg},// 720x480(I)@60Hz      - CEA Mode [ 6]
    {  7,  720,  240,    858,  262,   19,    62,    57,  4, 3,  15, INTERLACE, Vneg, Hneg},// 720x480(I)@60Hz      - CEA Mode [ 7]
    // {  8,  720,  240,    858,  262,   19,    62,    57,  4, 3,  15,      PROG, Vneg, Hneg},// 720x480(I)@60Hz      - CEA Mode [ 8]
    // {  9,  720,  240,    858,  262,   19,    62,    57,  4, 3,  15,      PROG, Vneg, Hneg},// 720x480(I)@60Hz      - CEA Mode [ 9]
    // { 10,  720,  240,    858,  262,   19,    62,    57,  4, 3,  15, INTERLACE, Vneg, Hneg},// 720x480(I)@60Hz      - CEA Mode [10]
    // { 11,  720,  240,    858,  262,   19,    62,    57,  4, 3,  15, INTERLACE, Vneg, Hneg},// 720x480(I)@60Hz      - CEA Mode [11]
    // { 12,  720,  240,    858,  262,   19,    62,    57,  4, 3,  15,      PROG, Vneg, Hneg},// 720x480(I)@60Hz      - CEA Mode [12]
    // { 13,  720,  240,    858,  262,   19,    62,    57,  4, 3,  15,      PROG, Vneg, Hneg},// 720x480(I)@60Hz      - CEA Mode [13]
    // { 14, 1440,  480,   1716,  525,   32,   124,   120,  9, 6,  30,      PROG, Vneg, Hneg},// 1440x480@60Hz        - CEA Mode [14]
    // { 15, 1440,  480,   1716,  525,   32,   124,   120,  9, 6,  30,      PROG, Vneg, Hneg},// 1440x480@60Hz        - CEA Mode [15]
    { 16, 1920, 1080,   2200, 1125,   88,    44,   148,  4, 5,  36,      PROG, Vpos, Hpos},// 1920x1080@60Hz       - CEA Mode [16]
    { 17,  720,  576,    864,  625,   12,    64,    68,  5, 5,  39,      PROG, Vneg, Hneg},// 720x576@50Hz         - CEA Mode [17]
    { 18,  720,  576,    864,  625,   12,    64,    68,  5, 5,  39,      PROG, Vneg, Hneg},// 720x576@50Hz         - CEA Mode [18]
    { 19, 1280,  720,   1980,  750,  440,    40,   220,  5, 5,  20,      PROG, Vpos, Hpos},// 1280x720@50Hz        - CEA Mode [19]
    { 20, 1920,  540,   2640,  562,  528,    44,   148,  2, 5,  15, INTERLACE, Vpos, Hpos},// 1920x1080(I)@50Hz    - CEA Mode [20]
    { 21,  720,  288,    864,  312,   12,    63,    69,  2, 3,  19, INTERLACE, Vneg, Hneg},// 1440x576(I)@50Hz     - CEA Mode [21]
    { 22,  720,  288,    864,  312,   12,    63,    69,  2, 3,  19, INTERLACE, Vneg, Hneg},// 1440x576(I)@50Hz     - CEA Mode [22]
    // { 23,  720,  288,    864,  312,   12,    63,    69,  2, 3,  19,      PROG, Vneg, Hneg},// 1440x288@50Hz        - CEA Mode [23]
    // { 24,  720,  288,    864,  312,   12,    63,    69,  2, 3,  19,      PROG, Vneg, Hneg},// 1440x288@50Hz        - CEA Mode [24]
    // { 25,  720,  288,    864,  312,   12,    63,    69,  2, 3,  19, INTERLACE, Vneg, Hneg},// 1440x576(I)@50Hz     - CEA Mode [25]
    // { 26,  720,  288,    864,  312,   12,    63,    69,  2, 3,  19, INTERLACE, Vneg, Hneg},// 1440x576(I)@50Hz     - CEA Mode [26]
    // { 27,  720,  288,    864,  312,   12,    63,    69,  2, 3,  19,      PROG, Vneg, Hneg},// 1440x288@50Hz        - CEA Mode [27]
    // { 28,  720,  288,    864,  312,   12,    63,    69,  2, 3,  19,      PROG, Vneg, Hneg},// 1440x288@50Hz        - CEA Mode [28]
    // { 29, 1440,  576,   1728,  625,   24,   128,   136,  5, 5,  39,      PROG, Vpos, Hneg},// 1440x576@50Hz        - CEA Mode [29]
    // { 30, 1440,  576,   1728,  625,   24,   128,   136,  5, 5,  39,      PROG, Vpos, Hneg},// 1440x576@50Hz        - CEA Mode [30]
    { 31, 1920, 1080,   2640, 1125,  528,    44,   148,  4, 5,  36,      PROG, Vpos, Hpos},// 1920x1080@50Hz       - CEA Mode [31]
    { 32, 1920, 1080,   2750, 1125,  638,    44,   148,  4, 5,  36,      PROG, Vpos, Hpos},// 1920x1080@24Hz       - CEA Mode [32]
    { 33, 1920, 1080,   2640, 1125,  528,    44,   148,  4, 5,  36,      PROG, Vpos, Hpos},// 1920x1080@25Hz       - CEA Mode [33]
    { 34, 1920, 1080,   2200, 1125,   88,    44,   148,  4, 5,  36,      PROG, Vpos, Hpos},// 1920x1080@30Hz       - CEA Mode [34]
    // { 35, 2880,  480, 1716*2,  525, 32*2, 124*2, 120*2,  9, 6,  30,      PROG, Vneg, Hneg},// 2880x480@60Hz        - CEA Mode [35]
    // { 36, 2880,  480, 1716*2,  525, 32*2, 124*2, 120*2,  9, 6,  30,      PROG, Vneg, Hneg},// 2880x480@60Hz        - CEA Mode [36]
    // { 37, 2880,  576,   3456,  625, 24*2, 128*2, 136*2,  5, 5,  39,      PROG, Vneg, Hneg},// 2880x576@50Hz        - CEA Mode [37]
    // { 38, 2880,  576,   3456,  625, 24*2, 128*2, 136*2,  5, 5,  39,      PROG, Vneg, Hneg},// 2880x576@50Hz        - CEA Mode [38]
    // { 39, 1920,  540,   2304,  625,   32,   168,   184, 23, 5,  57, INTERLACE, Vneg, Hpos},// 1920x1080@50Hz       - CEA Mode [39]
    // { 40, 1920,  540,   2640,  562,  528,    44,   148,  2, 5,  15, INTERLACE, Vpos, Hpos},// 1920x1080(I)@100Hz   - CEA Mode [40]
    // { 41, 1280,  720,   1980,  750,  440,    40,   220,  5, 5,  20,      PROG, Vpos, Hpos},// 1280x720@100Hz       - CEA Mode [41]
    // { 42,  720,  576,    864,  625,   12,    64,    68,  5, 5,  39,      PROG, Vneg, Hneg},// 720x576@100Hz        - CEA Mode [42]
    // { 43,  720,  576,    864,  625,   12,    64,    68,  5, 5,  39,      PROG, Vneg, Hneg},// 720x576@100Hz        - CEA Mode [43]
    // { 44,  720,  288,    864,  312,   12,    63,    69,  2, 3,  19, INTERLACE, Vneg, Hneg},// 1440x576(I)@100Hz    - CEA Mode [44]
    // { 45,  720,  288,    864,  312,   12,    63,    69,  2, 3,  19, INTERLACE, Vneg, Hneg},// 1440x576(I)@100Hz    - CEA Mode [45]
    // { 46, 1920,  540,   2200,  562,   88,    44,   148,  2, 5,  15, INTERLACE, Vpos, Hpos},// 1920x1080(I)@120Hz   - CEA Mode [46]
    // { 47, 1280,  720,   1650,  750,  110,    40,   220,  5, 5,  20,      PROG, Vpos, Hpos},// 1280x720@120Hz       - CEA Mode [47]
    // { 48,  720,  480,    858,  525,   16,    62,    60,  9, 6,  30,      PROG, Vneg, Hneg},// 720x480@120Hz        - CEA Mode [48]
    // { 49,  720,  480,    858,  525,   16,    62,    60,  9, 6,  30,      PROG, Vneg, Hneg},// 720x480@120Hz        - CEA Mode [49]
    // { 50,  720,  240,    858,  262,   19,    62,    57,  4, 3,  15, INTERLACE, Vneg, Hneg},// 720x480(I)@120Hz     - CEA Mode [50]
    // { 51,  720,  240,    858,  262,   19,    62,    57,  4, 3,  15, INTERLACE, Vneg, Hneg},// 720x480(I)@120Hz     - CEA Mode [51]
    // { 52,  720,  576,    864,  625,   12,    64,    68,  5, 5,  39,      PROG, Vneg, Hneg},// 720x576@200Hz        - CEA Mode [52]
    // { 53,  720,  576,    864,  625,   12,    64,    68,  5, 5,  39,      PROG, Vneg, Hneg},// 720x576@200Hz        - CEA Mode [53]
    // { 54,  720,  288,    864,  312,   12,    63,    69,  2, 3,  19, INTERLACE, Vneg, Hneg},// 1440x576(I)@200Hz    - CEA Mode [54]
    // { 55,  720,  288,    864,  312,   12,    63,    69,  2, 3,  19, INTERLACE, Vneg, Hneg},// 1440x576(I)@200Hz    - CEA Mode [55]
    // { 56,  720,  480,    858,  525,   16,    62,    60,  9, 6,  30,      PROG, Vneg, Hneg},// 720x480@120Hz        - CEA Mode [56]
    // { 57,  720,  480,    858,  525,   16,    62,    60,  9, 6,  30,      PROG, Vneg, Hneg},// 720x480@120Hz        - CEA Mode [57]
    // { 58,  720,  240,    858,  262,   19,    62,    57,  4, 3,  15, INTERLACE, Vneg, Hneg},// 720x480(I)@120Hz     - CEA Mode [58]
    // { 59,  720,  240,    858,  262,   19,    62,    57,  4, 3,  15, INTERLACE, Vneg, Hneg},// 720x480(I)@120Hz     - CEA Mode [59]
    { 60, 1280,  720,   3300,  750, 1760,    40,   220,  5, 5,  20,      PROG, Vpos, Hpos},// 1280x720@24Hz        - CEA Mode [60]
    { 61, 1280,  720,   3960,  750, 2420,    40,   220,  5, 5,  20,      PROG, Vpos, Hpos},// 1280x720@25Hz        - CEA Mode [61]
    { 62, 1280,  720,   3300,  750, 1760,    40,   220,  5, 5,  20,      PROG, Vpos, Hpos},// 1280x720@30Hz        - CEA Mode [62]
    // { 63, 1920, 1080,   2200, 1125,   88,    44,   148,  4, 5,  36,      PROG, Vpos, Hpos},// 1920x1080@120Hz      - CEA Mode [63]
    // { 64, 1920, 1080,   2640, 1125,  528,    44,   148,  4, 5,  36,      PROG, Vpos, Hpos},// 1920x1080@100Hz      - CEA Mode [64]
};

#define MaxIndex (sizeof(TimingTable)/sizeof(struct CRT_TimingSetting))
BOOL setHDMITX_SyncEmbeddedByVIC(BYTE VIC,BYTE bInputType)
{
    int i ;
    BYTE fmt_index=0;

    // if Embedded Video,need to generate timing with pattern register
    Switch_HDMITX_Bank(0);

    HDMITX_DEBUG_PRINTF(("setHDMITX_SyncEmbeddedByVIC(%d,%x)\n",(int)VIC,(int)bInputType));
    if( VIC > 0 )
    {
        for(i=0;i< MaxIndex;i ++)
        {
            if(TimingTable[i].fmt==VIC)
            {
                fmt_index=i;
                HDMITX_DEBUG_PRINTF(("fmt_index=%02x)\n",(int)fmt_index));
                HDMITX_DEBUG_PRINTF(("***Fine Match Table ***\n"));
                break;
            }
        }
    }
    else
    {
        HDMITX_DEBUG_PRINTF(("***No Match VIC == 0 ***\n"));
        return FALSE ;
    }

    if(i>=MaxIndex)
    {
        //return FALSE;
        HDMITX_DEBUG_PRINTF(("***No Match VIC ***\n"));
        return FALSE ;
    }
    //if( bInputSignalType & T_MODE_SYNCEMB )
    {
        int HTotal, HDES, VTotal, VDES;
        int HDEW, VDEW, HFP, HSW, VFP, VSW;
        int HRS, HRE;
        int VRS, VRE;
        int H2ndVRRise;
        int VRS2nd, VRE2nd;
        BYTE Pol;

        HTotal  =TimingTable[fmt_index].HTotal;
        HDEW    =TimingTable[fmt_index].HActive;
        HFP     =TimingTable[fmt_index].H_FBH;
        HSW     =TimingTable[fmt_index].H_SyncW;
        HDES    =HSW+TimingTable[fmt_index].H_BBH;
        VTotal  =TimingTable[fmt_index].VTotal;
        VDEW    =TimingTable[fmt_index].VActive;
        VFP     =TimingTable[fmt_index].V_FBH;
        VSW     =TimingTable[fmt_index].V_SyncW;
        VDES    =VSW+TimingTable[fmt_index].V_BBH;

        Pol = (TimingTable[fmt_index].HPolarity==Hpos)?(1<<1):0 ;
        Pol |= (TimingTable[fmt_index].VPolarity==Vpos)?(1<<2):0 ;

        // SyncEmb case=====
        if( bInputType & T_MODE_CCIR656)
        {
            HRS = HFP - 1;
        }
        else
        {
            HRS = HFP - 2;
            /*
            if(VIC==HDMI_1080p60 ||
               VIC==HDMI_1080p50 )
            {
                HDMITX_OrReg_Byte(0x59, (1<<3));
            }
            else
            {
                HDMITX_AndReg_Byte(0x59, ~(1<<3));
            }
            */
        }
        HRE = HRS + HSW;
        H2ndVRRise = HRS+ HTotal/2;

        VRS = VFP;
        VRE = VRS + VSW;

        // VTotal>>=1;

        if(PROG == TimingTable[fmt_index].Scan)
        { // progressive mode
            VRS2nd = 0xFFF;
            VRE2nd = 0x3F;
        }
        else
        { // interlaced mode
            if(39 == TimingTable[fmt_index].fmt)
            {
                VRS2nd = VRS + VTotal - 1;
                VRE2nd = VRS2nd + VSW;
            }
            else
            {
                VRS2nd = VRS + VTotal;
                VRE2nd = VRS2nd + VSW;
            }
        }
        #ifdef DETECT_VSYNC_CHG_IN_SAV
        if( EnSavVSync )
        {
            VRS -= 1;
            VRE -= 1;
            if( !pSetVTiming->ScanMode ) // interlaced mode
            {
                VRS2nd -= 1;
                VRE2nd -= 1;
            }
        }
        #endif // DETECT_VSYNC_CHG_IN_SAV
        HDMITX_SetI2C_Byte(0x90, 0x06, Pol);
        // write H2ndVRRise
        HDMITX_SetI2C_Byte(0x90, 0xF0, (H2ndVRRise&0x0F)<<4);
        HDMITX_WriteI2C_Byte(0x91, (H2ndVRRise&0x0FF0)>>4);
        // write HRS/HRE
        HDMITX_WriteI2C_Byte(0x95, HRS&0xFF);
        HDMITX_WriteI2C_Byte(0x96, HRE&0xFF);
        HDMITX_WriteI2C_Byte(0x97, ((HRE&0x0F00)>>4)+((HRS&0x0F00)>>8));
        // write VRS/VRE
        HDMITX_WriteI2C_Byte(0xa0, VRS&0xFF);
        HDMITX_WriteI2C_Byte(0xa1, ((VRE&0x0F)<<4)+((VRS&0x0F00)>>8));
        HDMITX_WriteI2C_Byte(0xa2, VRS2nd&0xFF);
        HDMITX_WriteI2C_Byte(0xa6, (VRE2nd&0xF0)+((VRE&0xF0)>>4));
        HDMITX_WriteI2C_Byte(0xa3, ((VRE2nd&0x0F)<<4)+((VRS2nd&0xF00)>>8));
        HDMITX_WriteI2C_Byte(0xa4, H2ndVRRise&0xFF);
        HDMITX_WriteI2C_Byte(0xa5, (/*EnDEOnly*/0<<5)+((TimingTable[fmt_index].Scan==INTERLACE)?(1<<4):0)+((H2ndVRRise&0xF00)>>8));
        HDMITX_SetI2C_Byte(0xb1, 0x51, ((HRE&0x1000)>>6)+((HRS&0x1000)>>8)+((HDES&0x1000)>>12));
        HDMITX_SetI2C_Byte(0xb2, 0x05, ((H2ndVRRise&0x1000)>>10)+((H2ndVRRise&0x1000)>>12));
    }
    return TRUE ;
}

#endif // SUPPORT_SYNCEMBEDDED

//~jj_tseng@chipadvanced.com 2007/01/02

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_SetInputMode
// Parameter: InputMode,bInputSignalType
//      InputMode - use [1:0] to identify the color space for reg70[7:6],
//                  definition:
//                     #define F_MODE_RGB444  0
//                     #define F_MODE_YUV422 1
//                     #define F_MODE_YUV444 2
//                     #define F_MODE_CLRMOD_MASK 3
//      bInputSignalType - defined the CCIR656 D[0],SYNC Embedded D[1],and
//                     DDR input in D[2].
// Return: N/A
// Remark: program Reg70 with the input value.
// Side-Effect: Reg70.
//////////////////////////////////////////////////////////////////////

void hdmitx_SetInputMode(BYTE InputColorMode,BYTE bInputSignalType)
{
    BYTE ucData ;

    ucData = HDMITX_ReadI2C_Byte(REG_TX_INPUT_MODE);
    ucData &= ~(M_TX_INCOLMOD|B_TX_2X656CLK|B_TX_SYNCEMB|B_TX_INDDR|B_TX_PCLKDIV2);
    ucData |= 0x01;//input clock delay 1 for 1080P DDR

    switch(InputColorMode & F_MODE_CLRMOD_MASK)
    {
    case F_MODE_YUV422:
        ucData |= B_TX_IN_YUV422 ;
        break ;
    case F_MODE_YUV444:
        ucData |= B_TX_IN_YUV444 ;
        break ;
    case F_MODE_RGB444:
    default:
        ucData |= B_TX_IN_RGB ;
        break ;
    }
    if(bInputSignalType & T_MODE_PCLKDIV2)
    {
        ucData |= B_TX_PCLKDIV2 ; HDMITX_DEBUG_PRINTF(("PCLK Divided by 2 mode\n"));
    }
    if(bInputSignalType & T_MODE_CCIR656)
    {
        ucData |= B_TX_2X656CLK ; HDMITX_DEBUG_PRINTF(("CCIR656 mode\n"));
    }
    if(bInputSignalType & T_MODE_SYNCEMB)
    {
        ucData |= B_TX_SYNCEMB ; HDMITX_DEBUG_PRINTF(("Sync Embedded mode\n"));
    }
    if(bInputSignalType & T_MODE_INDDR)
    {
        ucData |= B_TX_INDDR ; HDMITX_DEBUG_PRINTF(("Input DDR mode\n"));
    }
    HDMITX_WriteI2C_Byte(REG_TX_INPUT_MODE,ucData);
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_SetCSCScale
// Parameter: bInputMode -
//             D[1:0] - Color Mode
//             D[4] - Colorimetry 0: ITU_BT601 1: ITU_BT709
//             D[5] - Quantization 0: 0_255 1: 16_235
//             D[6] - Up/Dn Filter 'Required'
//                    0: no up/down filter
//                    1: enable up/down filter when csc need.
//             D[7] - Dither Filter 'Required'
//                    0: no dither enabled.
//                    1: enable dither and dither free go "when required".
//            bOutputMode -
//             D[1:0] - Color mode.
// Return: N/A
// Remark: reg72~reg8D will be programmed depended the input with table.
// Side-Effect:
//////////////////////////////////////////////////////////////////////

void hdmitx_SetCSCScale(BYTE bInputMode,BYTE bOutputMode)
{
    BYTE ucData = 0,csc = B_HDMITX_CSC_BYPASS ;
    BYTE i ;
    BYTE filter = 0 ; // filter is for Video CTRL DN_FREE_GO,EN_DITHER,and ENUDFILT

    // (1) YUV422 in,RGB/YUV444 output (Output is 8-bit,input is 12-bit)
    // (2) YUV444/422  in,RGB output (CSC enable,and output is not YUV422)
    // (3) RGB in,YUV444 output   (CSC enable,and output is not YUV422)
    //
    // YUV444/RGB24 <-> YUV422 need set up/down filter.
    HDMITX_DEBUG_PRINTF(("hdmitx_SetCSCScale(BYTE bInputMode = %x,BYTE bOutputMode = %x)\n", (int)bInputMode, (int)bOutputMode)) ;
    switch(bInputMode&F_MODE_CLRMOD_MASK)
    {
    #ifdef SUPPORT_INPUTYUV444
    case F_MODE_YUV444:
        HDMITX_DEBUG_PRINTF(("Input mode is YUV444 "));
        switch(bOutputMode&F_MODE_CLRMOD_MASK)
        {
        case F_MODE_YUV444:
            HDMITX_DEBUG_PRINTF(("Output mode is YUV444\n"));
            csc = B_HDMITX_CSC_BYPASS ;
            break ;

        case F_MODE_YUV422:
            HDMITX_DEBUG_PRINTF(("Output mode is YUV422\n"));
            if(bInputMode & F_VIDMODE_EN_UDFILT) // YUV444 to YUV422 need up/down filter for processing.
            {
                filter |= B_TX_EN_UDFILTER ;
            }
            csc = B_HDMITX_CSC_BYPASS ;
            break ;
        case F_MODE_RGB444:
            HDMITX_DEBUG_PRINTF(("Output mode is RGB24\n"));
            csc = B_HDMITX_CSC_YUV2RGB ;
            if(bInputMode & F_VIDMODE_EN_DITHER) // YUV444 to RGB24 need dither
            {
                filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO ;
            }
            break ;
        }
        break ;
    #endif

    #ifdef SUPPORT_INPUTYUV422
    case F_MODE_YUV422:
        HDMITX_DEBUG_PRINTF(("Input mode is YUV422\n"));
        switch(bOutputMode&F_MODE_CLRMOD_MASK)
        {
        case F_MODE_YUV444:
            HDMITX_DEBUG_PRINTF(("Output mode is YUV444\n"));
            csc = B_HDMITX_CSC_BYPASS ;
            if(bInputMode & F_VIDMODE_EN_UDFILT) // YUV422 to YUV444 need up filter
            {
                filter |= B_TX_EN_UDFILTER ;
            }
            if(bInputMode & F_VIDMODE_EN_DITHER) // YUV422 to YUV444 need dither
            {
                filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO ;
            }
            break ;
        case F_MODE_YUV422:
            HDMITX_DEBUG_PRINTF(("Output mode is YUV422\n"));
            csc = B_HDMITX_CSC_BYPASS ;

            break ;

        case F_MODE_RGB444:
            HDMITX_DEBUG_PRINTF(("Output mode is RGB24\n"));
            csc = B_HDMITX_CSC_YUV2RGB ;
            if(bInputMode & F_VIDMODE_EN_UDFILT) // YUV422 to RGB24 need up/dn filter.
            {
                filter |= B_TX_EN_UDFILTER ;
            }
            if(bInputMode & F_VIDMODE_EN_DITHER) // YUV422 to RGB24 need dither
            {
                filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO ;
            }
            break ;
        }
        break ;
    #endif

    #ifdef SUPPORT_INPUTRGB
    case F_MODE_RGB444:
        HDMITX_DEBUG_PRINTF(("Input mode is RGB24\n"));
        switch(bOutputMode&F_MODE_CLRMOD_MASK)
        {
        case F_MODE_YUV444:
            HDMITX_DEBUG_PRINTF(("Output mode is YUV444\n"));
            csc = B_HDMITX_CSC_RGB2YUV ;

            if(bInputMode & F_VIDMODE_EN_DITHER) // RGB24 to YUV444 need dither
            {
                filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO ;
            }
            break ;

        case F_MODE_YUV422:
            HDMITX_DEBUG_PRINTF(("Output mode is YUV422\n"));
            if(bInputMode & F_VIDMODE_EN_UDFILT) // RGB24 to YUV422 need down filter.
            {
                filter |= B_TX_EN_UDFILTER ;
            }
            if(bInputMode & F_VIDMODE_EN_DITHER) // RGB24 to YUV422 need dither
            {
                filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO ;
            }
            csc = B_HDMITX_CSC_RGB2YUV ;
            break ;

        case F_MODE_RGB444:
            HDMITX_DEBUG_PRINTF(("Output mode is RGB24\n"));
            csc = B_HDMITX_CSC_BYPASS ;
            break ;
        }
        break ;
    #endif
    }
#ifndef DISABLE_HDMITX_CSC

    #ifdef SUPPORT_INPUTRGB
    // set the CSC metrix registers by colorimetry and quantization
    if(csc == B_HDMITX_CSC_RGB2YUV)
    {
        HDMITX_DEBUG_PRINTF(("CSC = RGB2YUV %x ",csc));
        switch(bInputMode&(F_VIDMODE_ITU709|F_VIDMODE_16_235))
        {
        case F_VIDMODE_ITU709|F_VIDMODE_16_235:
            HDMITX_DEBUG_PRINTF(("ITU709 16-235 "));
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ HDMITX_WriteI2C_Byte(REG_TX_CSC_YOFF+i,bCSCMtx_RGB2YUV_ITU709_16_235[i]) ; HDMITX_DEBUG_PRINTF(("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_RGB2YUV_ITU709_16_235[i]));}
            break ;
        case F_VIDMODE_ITU709|F_VIDMODE_0_255:
            HDMITX_DEBUG_PRINTF(("ITU709 0-255 "));
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ HDMITX_WriteI2C_Byte(REG_TX_CSC_YOFF+i,bCSCMtx_RGB2YUV_ITU709_0_255[i]) ; HDMITX_DEBUG_PRINTF(("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_RGB2YUV_ITU709_0_255[i]));}
            break ;
        case F_VIDMODE_ITU601|F_VIDMODE_16_235:
            HDMITX_DEBUG_PRINTF(("ITU601 16-235 "));
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ HDMITX_WriteI2C_Byte(REG_TX_CSC_YOFF+i,bCSCMtx_RGB2YUV_ITU601_16_235[i]) ; HDMITX_DEBUG_PRINTF(("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_RGB2YUV_ITU601_16_235[i]));}
            break ;
        case F_VIDMODE_ITU601|F_VIDMODE_0_255:
        default:
            HDMITX_DEBUG_PRINTF(("ITU601 0-255 "));
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ HDMITX_WriteI2C_Byte(REG_TX_CSC_YOFF+i,bCSCMtx_RGB2YUV_ITU601_0_255[i]) ; HDMITX_DEBUG_PRINTF(("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_RGB2YUV_ITU601_0_255[i]));}
            break ;
        }
    }
    #endif

    #ifdef SUPPORT_INPUTYUV
    if (csc == B_HDMITX_CSC_YUV2RGB)
    {
        HDMITX_DEBUG_PRINTF(("CSC = YUV2RGB %x ",csc));

        switch(bInputMode&(F_VIDMODE_ITU709|F_VIDMODE_16_235))
        {
        case F_VIDMODE_ITU709|F_VIDMODE_16_235:
            HDMITX_DEBUG_PRINTF(("ITU709 16-235 "));
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ HDMITX_WriteI2C_Byte(REG_TX_CSC_YOFF+i,bCSCMtx_YUV2RGB_ITU709_16_235[i]) ; HDMITX_DEBUG_PRINTF(("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_YUV2RGB_ITU709_16_235[i]));}
            break ;
        case F_VIDMODE_ITU709|F_VIDMODE_0_255:
            HDMITX_DEBUG_PRINTF(("ITU709 0-255 "));
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ HDMITX_WriteI2C_Byte(REG_TX_CSC_YOFF+i,bCSCMtx_YUV2RGB_ITU709_0_255[i]) ; HDMITX_DEBUG_PRINTF(("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_YUV2RGB_ITU709_0_255[i]));}
            break ;
        case F_VIDMODE_ITU601|F_VIDMODE_16_235:
            HDMITX_DEBUG_PRINTF(("ITU601 16-235 "));
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ HDMITX_WriteI2C_Byte(REG_TX_CSC_YOFF+i,bCSCMtx_YUV2RGB_ITU601_16_235[i]) ; HDMITX_DEBUG_PRINTF(("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_YUV2RGB_ITU601_16_235[i]));}
            break ;
        case F_VIDMODE_ITU601|F_VIDMODE_0_255:
        default:
            HDMITX_DEBUG_PRINTF(("ITU601 0-255 "));
            for( i = 0 ; i < SIZEOF_CSCMTX ; i++ ){ HDMITX_WriteI2C_Byte(REG_TX_CSC_YOFF+i,bCSCMtx_YUV2RGB_ITU601_0_255[i]) ; HDMITX_DEBUG_PRINTF(("reg%02X <- %02X\n",(int)(i+REG_TX_CSC_YOFF),(int)bCSCMtx_YUV2RGB_ITU601_0_255[i]));}
            break ;
        }
    }
    #endif
#else// DISABLE_HDMITX_CSC
    csc = B_HDMITX_CSC_BYPASS ;
#endif// DISABLE_HDMITX_CSC

	if( csc == B_HDMITX_CSC_BYPASS )
	{
		HDMITX_SetI2C_Byte(0xF, 0x10, 0x10);
	}
	else
	{
		HDMITX_SetI2C_Byte(0xF, 0x10, 0x00);
	}
    ucData = HDMITX_ReadI2C_Byte(REG_TX_CSC_CTRL) & ~(M_TX_CSC_SEL|B_TX_DNFREE_GO|B_TX_EN_DITHER|B_TX_EN_UDFILTER);
    ucData |= filter|csc ;

    HDMITX_WriteI2C_Byte(REG_TX_CSC_CTRL,ucData);

    // set output Up/Down Filter,Dither control

}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_SetupAFE
// Parameter: VIDEOPCLKLEVEL level
//            PCLK_LOW - for 13.5MHz (for mode less than 1080p)
//            PCLK MEDIUM - for 25MHz~74MHz
//            PCLK HIGH - PCLK > 80Hz (for 1080p mode or above)
// Return: N/A
// Remark: set reg62~reg65 depended on HighFreqMode
//         reg61 have to be programmed at last and after video stable input.
// Side-Effect:
//////////////////////////////////////////////////////////////////////

void hdmitx_SetupAFE(VIDEOPCLKLEVEL level)
{

    HDMITX_WriteI2C_Byte(REG_TX_AFE_DRV_CTRL,B_TX_AFE_DRV_RST);/* 0x10 */
    switch(level)
    {
        case PCLK_HIGH:
            HDMITX_SetI2C_Byte(0x62, 0x90, 0x80);
            HDMITX_SetI2C_Byte(0x64, 0x89, 0x80);
            HDMITX_SetI2C_Byte(0x68, 0x10, 0x80);
            HDMITX_DEBUG_PRINTF(("hdmitx_SetupAFE()===================HIGHT\n"));
            break ;
        default:
            HDMITX_SetI2C_Byte(0x62, 0x90, 0x10);
            HDMITX_SetI2C_Byte(0x64, 0x89, 0x09);
            HDMITX_SetI2C_Byte(0x68, 0x10, 0x10);
            HDMITX_DEBUG_PRINTF(("hdmitx_SetupAFE()===================LOW\n"));
            break ;
    }
    HDMITX_SetI2C_Byte(REG_TX_SW_RST,B_TX_REF_RST_HDMITX|B_HDMITX_VID_RST,0);
    HDMITX_WriteI2C_Byte(REG_TX_AFE_DRV_CTRL,0);
    delay1ms(1);
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_FireAFE
// Parameter: N/A
// Return: N/A
// Remark: write reg61 with 0x04
//         When program reg61 with 0x04,then audio and video circuit work.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////

void hdmitx_FireAFE()
{
    Switch_HDMITX_Bank(0);
    HDMITX_WriteI2C_Byte(REG_TX_AFE_DRV_CTRL,0);
}

///*****************************************
//   @file   <hdmitx_aud.c>
//******************************************/

BYTE AudioDelayCnt=0;
BYTE LastRefaudfreqnum=0;
BOOL bForceCTS = FALSE;

//////////////////////////////////////////////////////////////////////
// Audio Output
//////////////////////////////////////////////////////////////////////

void setHDMITX_ChStat(BYTE ucIEC60958ChStat[])
{
    BYTE uc ;

    Switch_HDMITX_Bank(1);
    uc = (ucIEC60958ChStat[0] <<1)& 0x7C ;
    HDMITX_WriteI2C_Byte(REG_TX_AUDCHST_MODE,uc);
    HDMITX_WriteI2C_Byte(REG_TX_AUDCHST_CAT,ucIEC60958ChStat[1]); // 192, audio CATEGORY
    HDMITX_WriteI2C_Byte(REG_TX_AUDCHST_SRCNUM,ucIEC60958ChStat[2]&0xF);
    HDMITX_WriteI2C_Byte(REG_TX_AUD0CHST_CHTNUM,(ucIEC60958ChStat[2]>>4)&0xF);
    HDMITX_WriteI2C_Byte(REG_TX_AUDCHST_CA_FS,ucIEC60958ChStat[3]); // choose clock
    HDMITX_WriteI2C_Byte(REG_TX_AUDCHST_OFS_WL,ucIEC60958ChStat[4]);
    Switch_HDMITX_Bank(0);
}

void setHDMITX_UpdateChStatFs(ULONG Fs)
{
    BYTE uc ;

    /////////////////////////////////////
    // Fs should be the following value.
    // #define AUDFS_22p05KHz  4
    // #define AUDFS_44p1KHz 0
    // #define AUDFS_88p2KHz 8
    // #define AUDFS_176p4KHz    12
    //
    // #define AUDFS_24KHz  6
    // #define AUDFS_48KHz  2
    // #define AUDFS_96KHz  10
    // #define AUDFS_192KHz 14
    //
    // #define AUDFS_768KHz 9
    //
    // #define AUDFS_32KHz  3
    // #define AUDFS_OTHER    1
    /////////////////////////////////////

    Switch_HDMITX_Bank(1);
    uc = HDMITX_ReadI2C_Byte(REG_TX_AUDCHST_CA_FS); // choose clock
    HDMITX_WriteI2C_Byte(REG_TX_AUDCHST_CA_FS,uc); // choose clock
    uc &= 0xF0 ;
    uc |= (Fs&0xF);

    uc = HDMITX_ReadI2C_Byte(REG_TX_AUDCHST_OFS_WL);
    uc &= 0xF ;
    uc |= ((~Fs) << 4)&0xF0 ;
    HDMITX_WriteI2C_Byte(REG_TX_AUDCHST_OFS_WL,uc);

    Switch_HDMITX_Bank(0);
}

void setHDMITX_LPCMAudio(BYTE AudioSrcNum, BYTE AudSWL, BOOL bSPDIF)
{

    BYTE AudioEnable, AudioFormat ;

    AudioEnable = 0 ;
    AudioFormat = hdmiTxDev[0].bOutputAudioMode ;

    switch(AudSWL)
    {
    case 16:
        AudioEnable |= M_TX_AUD_16BIT ;
        break ;
    case 18:
        AudioEnable |= M_TX_AUD_18BIT ;
        break ;
    case 20:
        AudioEnable |= M_TX_AUD_20BIT ;
        break ;
    case 24:
    default:
        AudioEnable |= M_TX_AUD_24BIT ;
        break ;
    }
    if( bSPDIF )
    {
        AudioFormat &= ~0x40 ;
        AudioEnable |= B_TX_AUD_SPDIF|B_TX_AUD_EN_I2S0 ;
    }
    else
    {
        AudioFormat |= 0x40 ;
        switch(AudioSrcNum)
        {
        case 4:
            AudioEnable |= B_TX_AUD_EN_I2S3|B_TX_AUD_EN_I2S2|B_TX_AUD_EN_I2S1|B_TX_AUD_EN_I2S0 ;
            break ;

        case 3:
            AudioEnable |= B_TX_AUD_EN_I2S2|B_TX_AUD_EN_I2S1|B_TX_AUD_EN_I2S0 ;
            break ;

        case 2:
            AudioEnable |= B_TX_AUD_EN_I2S1|B_TX_AUD_EN_I2S0 ;
            break ;

        case 1:
        default:
            AudioFormat &= ~0x40 ;
            AudioEnable |= B_TX_AUD_EN_I2S0 ;
            break ;

        }
    }
    AudioFormat|=0x01;//mingchih add
    hdmiTxDev[0].bAudioChannelEnable=AudioEnable;

    Switch_HDMITX_Bank(0);
    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL0,AudioEnable&0xF0);

    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL1,AudioFormat); // regE1 bOutputAudioMode should be loaded from ROM image.
    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_FIFOMAP,0xE4); // default mapping.
#ifdef USE_SPDIF_CHSTAT
    if( bSPDIF )
    {
        HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL3,B_TX_CHSTSEL);
    }
    else
    {
        HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL3,0);
    }
#else // not USE_SPDIF_CHSTAT
    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL3,0);
#endif // USE_SPDIF_CHSTAT

    HDMITX_WriteI2C_Byte(REG_TX_AUD_SRCVALID_FLAT,0x00);
    HDMITX_WriteI2C_Byte(REG_TX_AUD_HDAUDIO,0x00); // regE5 = 0 ;

    if( bSPDIF )
    {
        BYTE i ;
        HDMITX_OrReg_Byte(0x5c,(1<<6));
        for( i = 0 ; i < 100 ; i++ )
        {
            if(HDMITX_ReadI2C_Byte(REG_TX_CLK_STATUS2) & B_TX_OSF_LOCK)
            {
                break ; // stable clock.
            }
        }
    }
}

void setHDMITX_NLPCMAudio(BOOL bSPDIF) // no Source Num, no I2S.
{
    BYTE AudioEnable, AudioFormat ;
    BYTE i ;

    AudioFormat = 0x01 ; // NLPCM must use standard I2S mode.
    if( bSPDIF )
    {
        AudioEnable = M_TX_AUD_24BIT|B_TX_AUD_SPDIF;
    }
    else
    {
        AudioEnable = M_TX_AUD_24BIT;
    }

    Switch_HDMITX_Bank(0);
    // HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT|B_TX_AUD_SPDIF);
    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL0, AudioEnable);
    //HDMITX_AndREG_Byte(REG_TX_SW_RST,~(B_HDMITX_AUD_RST|B_TX_AREF_RST));

    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL1,0x01); // regE1 bOutputAudioMode should be loaded from ROM image.
    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_FIFOMAP,0xE4); // default mapping.

#ifdef USE_SPDIF_CHSTAT
    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL3,B_TX_CHSTSEL);
#else // not USE_SPDIF_CHSTAT
    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL3,0);
#endif // USE_SPDIF_CHSTAT

    HDMITX_WriteI2C_Byte(REG_TX_AUD_SRCVALID_FLAT,0x00);
    HDMITX_WriteI2C_Byte(REG_TX_AUD_HDAUDIO,0x00); // regE5 = 0 ;

    if( bSPDIF )
    {
        for( i = 0 ; i < 100 ; i++ )
        {
            if(HDMITX_ReadI2C_Byte(REG_TX_CLK_STATUS2) & B_TX_OSF_LOCK)
            {
                break ; // stable clock.
            }
        }
    }
    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL0, AudioEnable|B_TX_AUD_EN_I2S0);
}

void setHDMITX_HBRAudio(BOOL bSPDIF)
{
    // BYTE rst;
    Switch_HDMITX_Bank(0);

    // rst = HDMITX_ReadI2C_Byte(REG_TX_SW_RST);
	// rst &= ~(B_HDMITX_AUD_RST|B_TX_AREF_RST);

    // HDMITX_WriteI2C_Byte(REG_TX_SW_RST, rst | B_HDMITX_AUD_RST );

    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL1,0x47); // regE1 bOutputAudioMode should be loaded from ROM image.
    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_FIFOMAP,0xE4); // default mapping.

    if( bSPDIF )
    {
        HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT|B_TX_AUD_SPDIF);
        HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL3,B_TX_CHSTSEL);
    }
    else
    {
        HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT);
        HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL3,0);
    }
    HDMITX_WriteI2C_Byte(REG_TX_AUD_SRCVALID_FLAT,0x08);
    HDMITX_WriteI2C_Byte(REG_TX_AUD_HDAUDIO,B_TX_HBR); // regE5 = 0 ;

    //uc = HDMITX_ReadI2C_Byte(REG_TX_CLK_CTRL1);
    //uc &= ~M_TX_AUD_DIV ;
    //HDMITX_WriteI2C_Byte(REG_TX_CLK_CTRL1, uc);

    if( bSPDIF )
    {
        BYTE i ;
        for( i = 0 ; i < 100 ; i++ )
        {
            if(HDMITX_ReadI2C_Byte(REG_TX_CLK_STATUS2) & B_TX_OSF_LOCK)
            {
                break ; // stable clock.
            }
        }
        HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT|B_TX_AUD_SPDIF|B_TX_AUD_EN_SPDIF);
    }
    else
    {
        HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT|B_TX_AUD_EN_I2S3|B_TX_AUD_EN_I2S2|B_TX_AUD_EN_I2S1|B_TX_AUD_EN_I2S0);
    }
    HDMITX_AndReg_Byte(0x5c,~(1<<6));
    hdmiTxDev[0].bAudioChannelEnable=HDMITX_ReadI2C_Byte(REG_TX_AUDIO_CTRL0);
    // HDMITX_WriteI2C_Byte(REG_TX_SW_RST, rst  );
}

void setHDMITX_DSDAudio()
{
    // to be continue
    // BYTE rst;
    // rst = HDMITX_ReadI2C_Byte(REG_TX_SW_RST);

    //HDMITX_WriteI2C_Byte(REG_TX_SW_RST, rst | (B_HDMITX_AUD_RST|B_TX_AREF_RST) );

    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL1,0x41); // regE1 bOutputAudioMode should be loaded from ROM image.
    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_FIFOMAP,0xE4); // default mapping.

    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT);
    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL3,0);

    HDMITX_WriteI2C_Byte(REG_TX_AUD_SRCVALID_FLAT,0x00);
    HDMITX_WriteI2C_Byte(REG_TX_AUD_HDAUDIO,B_TX_DSD); // regE5 = 0 ;
    //HDMITX_WriteI2C_Byte(REG_TX_SW_RST, rst & ~(B_HDMITX_AUD_RST|B_TX_AREF_RST) );

    //uc = HDMITX_ReadI2C_Byte(REG_TX_CLK_CTRL1);
    //uc &= ~M_TX_AUD_DIV ;
    //HDMITX_WriteI2C_Byte(REG_TX_CLK_CTRL1, uc);

    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL0, M_TX_AUD_24BIT|B_TX_AUD_EN_I2S3|B_TX_AUD_EN_I2S2|B_TX_AUD_EN_I2S1|B_TX_AUD_EN_I2S0);
}

void HDMITX_DisableAudioOutput()
{
    //BYTE uc = (HDMITX_ReadI2C_Byte(REG_TX_SW_RST) | (B_HDMITX_AUD_RST | B_TX_AREF_RST));
    //HDMITX_WriteI2C_Byte(REG_TX_SW_RST,uc);
    AudioDelayCnt=AudioOutDelayCnt;
    LastRefaudfreqnum=0;
    HDMITX_SetI2C_Byte(REG_TX_SW_RST, (B_HDMITX_AUD_RST | B_TX_AREF_RST), (B_HDMITX_AUD_RST | B_TX_AREF_RST) );
    HDMITX_SetI2C_Byte(0x0F, 0x10, 0x10 );
}

void HDMITX_EnableAudioOutput(BYTE AudioType, BOOL bSPDIF,  ULONG SampleFreq,  BYTE ChNum, BYTE *pIEC60958ChStat, ULONG TMDSClock)
{
    static _IDATA BYTE ucIEC60958ChStat[5] ;

    BYTE Fs ;
    AudioDelayCnt=36;
    LastRefaudfreqnum=0;
    hdmiTxDev[0].TMDSClock=TMDSClock;
    hdmiTxDev[0].bAudioChannelEnable=0;
    hdmiTxDev[0].bSPDIF_OUT=bSPDIF;

    HDMITX_DEBUG_PRINTF1(("HDMITX_EnableAudioOutput(%02X, %s, %ld, %d, %p, %ld);\n",
        AudioType, bSPDIF?"SPDIF":"I2S",SampleFreq, ChNum, pIEC60958ChStat, TMDSClock
        ));

    HDMITX_OrReg_Byte(REG_TX_SW_RST,(B_HDMITX_AUD_RST | B_TX_AREF_RST));
    HDMITX_WriteI2C_Byte(REG_TX_CLK_CTRL0,B_TX_AUTO_OVER_SAMPLING_CLOCK|B_TX_EXT_256FS|0x01);

    HDMITX_SetI2C_Byte(0x0F, 0x10, 0x00 ); // power on the ACLK

    if(bSPDIF)
    {
        if(AudioType==T_AUDIO_HBR)
        {
            HDMITX_WriteI2C_Byte(REG_TX_CLK_CTRL0,0x81);
        }
        HDMITX_OrReg_Byte(REG_TX_AUDIO_CTRL0,B_TX_AUD_SPDIF);
    }
    else
    {
        HDMITX_AndReg_Byte(REG_TX_AUDIO_CTRL0,(~B_TX_AUD_SPDIF));
    }
    if( AudioType != T_AUDIO_DSD)
    {
        // one bit audio have no channel status.
        switch(SampleFreq)
        {
        case  44100L: Fs =  AUDFS_44p1KHz ; break ;
        case  88200L: Fs =  AUDFS_88p2KHz ; break ;
        case 176400L: Fs = AUDFS_176p4KHz ; break ;
        case  32000L: Fs =    AUDFS_32KHz ; break ;
        case  48000L: Fs =    AUDFS_48KHz ; break ;
        case  96000L: Fs =    AUDFS_96KHz ; break ;
        case 192000L: Fs =   AUDFS_192KHz ; break ;
        case 768000L: Fs =   AUDFS_768KHz ; break ;
        default:
            SampleFreq = 48000L ;
            Fs =    AUDFS_48KHz ;
            break ; // default, set Fs = 48KHz.
        }
    #ifdef SUPPORT_AUDIO_MONITOR
        hdmiTxDev[0].bAudFs=AUDFS_OTHER;
    #else
        hdmiTxDev[0].bAudFs=Fs;
    #endif
        setHDMITX_NCTS(hdmiTxDev[0].bAudFs);
        if( pIEC60958ChStat == NULL )
        {
            ucIEC60958ChStat[0] = 0 ;
            ucIEC60958ChStat[1] = 0 ;
            ucIEC60958ChStat[2] = (ChNum+1)/2 ;

            if(ucIEC60958ChStat[2]<1)
            {
                ucIEC60958ChStat[2] = 1 ;
            }
            else if( ucIEC60958ChStat[2] >4 )
            {
                ucIEC60958ChStat[2] = 4 ;
            }
            ucIEC60958ChStat[3] = Fs ;
            ucIEC60958ChStat[4] = (((~Fs)<<4) & 0xF0) | CHTSTS_SWCODE ; // Fs | 24bit word length
            pIEC60958ChStat = ucIEC60958ChStat ;
        }
    }
    HDMITX_SetI2C_Byte(REG_TX_SW_RST,(B_HDMITX_AUD_RST|B_TX_AREF_RST),B_TX_AREF_RST);

    switch(AudioType)
    {
    case T_AUDIO_HBR:
        HDMITX_DEBUG_PRINTF(("T_AUDIO_HBR\n"));
        pIEC60958ChStat[0] |= 1<<1 ;
        pIEC60958ChStat[2] = 0;
        pIEC60958ChStat[3] &= 0xF0 ;
        pIEC60958ChStat[3] |= AUDFS_768KHz ;
        pIEC60958ChStat[4] |= (((~AUDFS_768KHz)<<4) & 0xF0)| 0xB ;
        setHDMITX_ChStat(pIEC60958ChStat);
        setHDMITX_HBRAudio(bSPDIF);

        break ;
    case T_AUDIO_DSD:
        HDMITX_DEBUG_PRINTF(("T_AUDIO_DSD\n"));
        setHDMITX_DSDAudio();
        break ;
    case T_AUDIO_NLPCM:
        HDMITX_DEBUG_PRINTF(("T_AUDIO_NLPCM\n"));
        pIEC60958ChStat[0] |= 1<<1 ;
        setHDMITX_ChStat(pIEC60958ChStat);
        setHDMITX_NLPCMAudio(bSPDIF);
        break ;
    case T_AUDIO_LPCM:
        HDMITX_DEBUG_PRINTF(("T_AUDIO_LPCM\n"));
        pIEC60958ChStat[0] &= ~(1<<1);

        setHDMITX_ChStat(pIEC60958ChStat);
        setHDMITX_LPCMAudio((ChNum+1)/2, SUPPORT_AUDI_AudSWL, bSPDIF);
        // can add auto adjust
        break ;
    }
    HDMITX_AndReg_Byte(REG_TX_INT_MASK1,(~B_TX_AUDIO_OVFLW_MASK));
    HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL0, hdmiTxDev[0].bAudioChannelEnable);

    HDMITX_SetI2C_Byte(REG_TX_SW_RST,(B_HDMITX_AUD_RST|B_TX_AREF_RST),0);
}

void hdmitx_AutoAdjustAudio()
{
    unsigned long SampleFreq,cTMDSClock ;
    unsigned long N ;
    ULONG aCTS=0;
    BYTE fs, uc,LoopCnt=10;
    if(bForceCTS)
    {
        Switch_HDMITX_Bank(0);
        HDMITX_WriteI2C_Byte(0xF8, 0xC3);
        HDMITX_WriteI2C_Byte(0xF8, 0xA5);
        HDMITX_AndReg_Byte(REG_TX_PKT_SINGLE_CTRL,~B_TX_SW_CTS); // D[1] = 0, HW auto count CTS
        HDMITX_WriteI2C_Byte(0xF8, 0xFF);
    }
    //delay1ms(50);
    Switch_HDMITX_Bank(1);
    N = ((unsigned long)HDMITX_ReadI2C_Byte(REGPktAudN2)&0xF) << 16 ;
    N |= ((unsigned long)HDMITX_ReadI2C_Byte(REGPktAudN1)) <<8 ;
    N |= ((unsigned long)HDMITX_ReadI2C_Byte(REGPktAudN0));

    while(LoopCnt--)
    {   ULONG TempCTS=0;
        aCTS = ((unsigned long)HDMITX_ReadI2C_Byte(REGPktAudCTSCnt2)) << 12 ;
        aCTS |= ((unsigned long)HDMITX_ReadI2C_Byte(REGPktAudCTSCnt1)) <<4 ;
        aCTS |= ((unsigned long)HDMITX_ReadI2C_Byte(REGPktAudCTSCnt0)&0xf0)>>4  ;
        if(aCTS==TempCTS)
        {break;}
        TempCTS=aCTS;
    }
    Switch_HDMITX_Bank(0);
    if( aCTS == 0)
    {
        HDMITX_DEBUG_PRINTF(("aCTS== 0"));
        return;
    }
    uc = HDMITX_ReadI2C_Byte(REG_TX_GCP);

    cTMDSClock = hdmiTxDev[0].TMDSClock ;
    //TMDSClock=GetInputPclk();
    HDMITX_DEBUG_PRINTF(("PCLK = %u0,000\n",(WORD)(cTMDSClock/10000)));
    switch(uc & 0x70)
    {
    case 0x50:
        cTMDSClock *= 5 ;
        cTMDSClock /= 4 ;
        break ;
    case 0x60:
        cTMDSClock *= 3 ;
        cTMDSClock /= 2 ;
    }
    SampleFreq = cTMDSClock/aCTS ;
    SampleFreq *= N ;
    SampleFreq /= 128 ;
    //SampleFreq=48000;

    HDMITX_DEBUG_PRINTF(("SampleFreq = %u0\n",(WORD)(SampleFreq/10)));
    if( SampleFreq>31000L && SampleFreq<=38050L ){fs = AUDFS_32KHz ;}
    else if (SampleFreq < 46550L )  {fs = AUDFS_44p1KHz ;}//46050
    else if (SampleFreq < 68100L )  {fs = AUDFS_48KHz ;}
    else if (SampleFreq < 92100L )  {fs = AUDFS_88p2KHz ;}
    else if (SampleFreq < 136200L ) {fs = AUDFS_96KHz ;}
    else if (SampleFreq < 184200L ) {fs = AUDFS_176p4KHz ;}
    else if (SampleFreq < 240200L ) {fs = AUDFS_192KHz ;}
    else if (SampleFreq < 800000L ) {fs = AUDFS_768KHz ;}
    else
    {
        fs = AUDFS_OTHER;
        HDMITX_DEBUG_PRINTF(("fs = AUDFS_OTHER\n"));
    }
    if(hdmiTxDev[0].bAudFs != fs)
    {
        hdmiTxDev[0].bAudFs=fs;
        setHDMITX_NCTS(hdmiTxDev[0].bAudFs); // set N, CTS by new generated clock.
        //CurrCTS=0;
        return;
    }
    return;
}

BOOL hdmitx_IsAudioChang()
{
    //ULONG pCTS=0;
    BYTE FreDiff=0,Refaudfreqnum;

    //Switch_HDMITX_Bank(1);
    //pCTS = ((unsigned long)HDMITX_ReadI2C_Byte(REGPktAudCTSCnt2)) << 12 ;
    //pCTS |= ((unsigned long)HDMITX_ReadI2C_Byte(REGPktAudCTSCnt1)) <<4 ;
    //pCTS |= ((unsigned long)HDMITX_ReadI2C_Byte(REGPktAudCTSCnt0)&0xf0)>>4  ;
    //Switch_HDMITX_Bank(0);
    Switch_HDMITX_Bank(0);
    Refaudfreqnum=HDMITX_ReadI2C_Byte(0x60);
    //HDMITX_DEBUG_PRINTF(("Refaudfreqnum=%X    pCTS= %u",(WORD)Refaudfreqnum,(WORD)(pCTS/10000)));
    //if((pCTS%10000)<1000)HDMITX_DEBUG_PRINTF(("0"));
    //if((pCTS%10000)<100)HDMITX_DEBUG_PRINTF(("0"));
    //if((pCTS%10000)<10)HDMITX_DEBUG_PRINTF(("0"));
    //HDMITX_DEBUG_PRINTF(("%u\n",(WORD)(pCTS%10000)));
    if((1<<4)&HDMITX_ReadI2C_Byte(0x5f))
    {
        //printf("=======XXXXXXXXXXX=========\n");
        return FALSE;
    }
    if(LastRefaudfreqnum>Refaudfreqnum)
        {FreDiff=LastRefaudfreqnum-Refaudfreqnum;}
    else
        {FreDiff=Refaudfreqnum-LastRefaudfreqnum;}
    LastRefaudfreqnum=Refaudfreqnum;
    if(3<FreDiff)
    {
        HDMITX_DEBUG_PRINTF(("Aduio FreDiff=%d\n",(int)FreDiff));
        HDMITX_OrReg_Byte(REG_TX_PKT_SINGLE_CTRL,(1<<5));
        HDMITX_AndReg_Byte(REG_TX_AUDIO_CTRL0,0xF0);
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

void setHDMITX_AudioChannelEnable(BOOL EnableAudio_b)
{
    static BOOL AudioOutStatus=FALSE;
    if(EnableAudio_b)
    {
        if(AudioDelayCnt==0)
        {
            //if(hdmiTxDev[0].bAuthenticated==FALSE)
            //{HDMITX_EnableHDCP(TRUE);}
        #ifdef SUPPORT_AUDIO_MONITOR
            if(hdmitx_IsAudioChang())
            {
                hdmitx_AutoAdjustAudio();
        #else
            if(AudioOutStatus==FALSE)
            {
                setHDMITX_NCTS(hdmiTxDev[0].bAudFs);
        #endif
                HDMITX_WriteI2C_Byte(REG_TX_AUD_SRCVALID_FLAT,0);
                HDMITX_OrReg_Byte(REG_TX_PKT_SINGLE_CTRL,(1<<5));
                HDMITX_WriteI2C_Byte(REG_TX_AUDIO_CTRL0, hdmiTxDev[0].bAudioChannelEnable);
                //HDMITX_OrREG_Byte(0x59,(1<<2));  //for test
                HDMITX_AndReg_Byte(REG_TX_PKT_SINGLE_CTRL,(~0x3C));
                HDMITX_AndReg_Byte(REG_TX_PKT_SINGLE_CTRL,(~(1<<5)));
                printk("Audio Out Enable\n");
        #ifndef SUPPORT_AUDIO_MONITOR
                AudioOutStatus=TRUE;
        #endif
            }
        }
        else
        {
            AudioOutStatus=FALSE;
            if(0==(HDMITX_ReadI2C_Byte(REG_TX_CLK_STATUS2)&0x10))
            {
                AudioDelayCnt--;
            }
            else
            {
                AudioDelayCnt=AudioOutDelayCnt;
            }
        }
    }
    else
    {
       // CurrCTS=0;
    }
}

//////////////////////////////////////////////////////////////////////
// Function: setHDMITX_NCTS
// Parameter: PCLK - video clock in Hz.
//            Fs - Encoded audio sample rate
//                          AUDFS_22p05KHz  4
//                          AUDFS_44p1KHz 0
//                          AUDFS_88p2KHz 8
//                          AUDFS_176p4KHz    12
//
//                          AUDFS_24KHz  6
//                          AUDFS_48KHz  2
//                          AUDFS_96KHz  10
//                          AUDFS_192KHz 14
//
//                          AUDFS_768KHz 9
//
//                          AUDFS_32KHz  3
//                          AUDFS_OTHER    1
// Return: ER_SUCCESS if success
// Remark: set N value,the CTS will be auto generated by HW.
// Side-Effect: register bank will reset to bank 0.
//////////////////////////////////////////////////////////////////////

void setHDMITX_NCTS(BYTE Fs)
{
    ULONG n;
    BYTE LoopCnt=255,CTSStableCnt=0;
    ULONG diff;
    ULONG CTS=0,LastCTS=0;
    BOOL HBR_mode;
    // BYTE aVIC;

    if(B_TX_HBR & HDMITX_ReadI2C_Byte(REG_TX_AUD_HDAUDIO))
    {
        HBR_mode=TRUE;
    }
    else
    {
        HBR_mode=FALSE;
    }
    switch(Fs)
    {
    case AUDFS_32KHz: n = 4096; break;
    case AUDFS_44p1KHz: n = 6272; break;
    case AUDFS_48KHz: n = 6144; break;
    case AUDFS_88p2KHz: n = 12544; break;
    case AUDFS_96KHz: n = 12288; break;
    case AUDFS_176p4KHz: n = 25088; break;
    case AUDFS_192KHz: n = 24576; break;
    case AUDFS_768KHz: n = 24576; break ;
    default: n = 6144;
    }
    // tr_printf((" n = %ld\n",n));
    Switch_HDMITX_Bank(1);
    HDMITX_WriteI2C_Byte(REGPktAudN0,(BYTE)((n)&0xFF));
    HDMITX_WriteI2C_Byte(REGPktAudN1,(BYTE)((n>>8)&0xFF));
    HDMITX_WriteI2C_Byte(REGPktAudN2,(BYTE)((n>>16)&0xF));

    if(bForceCTS)
    {
        ULONG SumCTS=0;
        while(LoopCnt--)
        {
            delay1ms(30);
            CTS = ((unsigned long)HDMITX_ReadI2C_Byte(REGPktAudCTSCnt2)) << 12 ;
            CTS |= ((unsigned long)HDMITX_ReadI2C_Byte(REGPktAudCTSCnt1)) <<4 ;
            CTS |= ((unsigned long)HDMITX_ReadI2C_Byte(REGPktAudCTSCnt0)&0xf0)>>4  ;
            if( CTS == 0)
            {
                continue;
            }
            else
            {
                if(LastCTS>CTS )
                    {diff=LastCTS-CTS;}
                else
                    {diff=CTS-LastCTS;}
                //HDMITX_DEBUG_PRINTF(("LastCTS= %u%u",(WORD)(LastCTS/10000),(WORD)(LastCTS%10000)));
                //HDMITX_DEBUG_PRINTF(("       CTS= %u%u\n",(WORD)(CTS/10000),(WORD)(CTS%10000)));
                LastCTS=CTS;
                if(5>diff)
                {
                    CTSStableCnt++;
                    SumCTS+=CTS;
                }
                else
                {
                    CTSStableCnt=0;
                    SumCTS=0;
                    continue;
                }
                if(CTSStableCnt>=32)
                {
                    LastCTS=(SumCTS>>5);
                    break;
                }
            }
        }
    }
    HDMITX_WriteI2C_Byte(REGPktAudCTS0,(BYTE)((LastCTS)&0xFF));
    HDMITX_WriteI2C_Byte(REGPktAudCTS1,(BYTE)((LastCTS>>8)&0xFF));
    HDMITX_WriteI2C_Byte(REGPktAudCTS2,(BYTE)((LastCTS>>16)&0xF));
    Switch_HDMITX_Bank(0);
#ifdef Force_CTS
    bForceCTS = TRUE;
#endif
    HDMITX_WriteI2C_Byte(0xF8, 0xC3);
    HDMITX_WriteI2C_Byte(0xF8, 0xA5);
    if(bForceCTS)
    {
        HDMITX_OrReg_Byte(REG_TX_PKT_SINGLE_CTRL,B_TX_SW_CTS); // D[1] = 0, HW auto count CTS
    }
    else
    {
        HDMITX_AndReg_Byte(REG_TX_PKT_SINGLE_CTRL,~B_TX_SW_CTS); // D[1] = 0, HW auto count CTS
    }
    HDMITX_WriteI2C_Byte(0xF8, 0xFF);

    if(FALSE==HBR_mode) //LPCM
    {
        BYTE uData;
        Switch_HDMITX_Bank(1);
        Fs = AUDFS_768KHz ;
        HDMITX_WriteI2C_Byte(REG_TX_AUDCHST_CA_FS,0x00|Fs);
        Fs = ~Fs ; // OFS is the one's complement of FS
        uData = (0x0f&HDMITX_ReadI2C_Byte(REG_TX_AUDCHST_OFS_WL));
        HDMITX_WriteI2C_Byte(REG_TX_AUDCHST_OFS_WL,(Fs<<4)|uData);
        Switch_HDMITX_Bank(0);
    }
}

///*****************************************
//   @file   <hdmitx_pkt.c>
//******************************************/

BOOL HDMITX_EnableVSInfoFrame(BYTE bEnable,BYTE *pVSInfoFrame)
{
    if(!bEnable)
    {
        hdmitx_DISABLE_VSDB_PKT();
        return TRUE ;
    }
    if(hdmitx_SetVSIInfoFrame((VendorSpecific_InfoFrame *)pVSInfoFrame) == ER_SUCCESS)
    {
        return TRUE ;
    }
    return FALSE ;
}

BOOL HDMITX_EnableAVIInfoFrame(BYTE bEnable,BYTE *pAVIInfoFrame)
{
    if(!bEnable)
    {
        hdmitx_DISABLE_AVI_INFOFRM_PKT();
        return TRUE ;
    }
    if(hdmitx_SetAVIInfoFrame((AVI_InfoFrame *)pAVIInfoFrame) == ER_SUCCESS)
    {
        return TRUE ;
    }
    return FALSE ;
}

BOOL HDMITX_EnableAudioInfoFrame(BYTE bEnable,BYTE *pAudioInfoFrame)
{
    if(!bEnable)
    {
        hdmitx_DISABLE_AVI_INFOFRM_PKT();
        return TRUE ;
    }
    if(hdmitx_SetAudioInfoFrame((Audio_InfoFrame *)pAudioInfoFrame) == ER_SUCCESS)
    {
        return TRUE ;
    }
    return FALSE ;
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_SetAVIInfoFrame()
// Parameter: pAVIInfoFrame - the pointer to HDMI AVI Infoframe ucData
// Return: N/A
// Remark: Fill the AVI InfoFrame ucData,and count checksum,then fill into
//         AVI InfoFrame registers.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////

SYS_STATUS hdmitx_SetAVIInfoFrame(AVI_InfoFrame *pAVIInfoFrame)
{
    int i ;
    byte checksum ;

    if(!pAVIInfoFrame)
    {
        return ER_FAIL ;
    }
    Switch_HDMITX_Bank(1);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB1,pAVIInfoFrame->pktbyte.AVI_DB[0]);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB2,pAVIInfoFrame->pktbyte.AVI_DB[1]);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB3,pAVIInfoFrame->pktbyte.AVI_DB[2]);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB4,pAVIInfoFrame->pktbyte.AVI_DB[3]);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB5,pAVIInfoFrame->pktbyte.AVI_DB[4]);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB6,pAVIInfoFrame->pktbyte.AVI_DB[5]);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB7,pAVIInfoFrame->pktbyte.AVI_DB[6]);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB8,pAVIInfoFrame->pktbyte.AVI_DB[7]);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB9,pAVIInfoFrame->pktbyte.AVI_DB[8]);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB10,pAVIInfoFrame->pktbyte.AVI_DB[9]);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB11,pAVIInfoFrame->pktbyte.AVI_DB[10]);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB12,pAVIInfoFrame->pktbyte.AVI_DB[11]);
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_DB13,pAVIInfoFrame->pktbyte.AVI_DB[12]);
    for(i = 0,checksum = 0; i < 13 ; i++)
    {
        checksum -= pAVIInfoFrame->pktbyte.AVI_DB[i] ;
    }
    /*
    HDMITX_DEBUG_PRINTF(("SetAVIInfo(): "));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB1)));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB2)));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB3)));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB4)));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB5)));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB6)));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB7)));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB8)));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB9)));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB10)));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB11)));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB12)));
    HDMITX_DEBUG_PRINTF(("%02X ",(int)HDMITX_ReadI2C_Byte(REG_TX_AVIINFO_DB13)));
    HDMITX_DEBUG_PRINTF(("\n"));
    */
    checksum -= AVI_INFOFRAME_VER+AVI_INFOFRAME_TYPE+AVI_INFOFRAME_LEN ;
    HDMITX_WriteI2C_Byte(REG_TX_AVIINFO_SUM,checksum);

    Switch_HDMITX_Bank(0);
    hdmitx_ENABLE_AVI_INFOFRM_PKT();
    return ER_SUCCESS ;
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_SetAudioInfoFrame()
// Parameter: pAudioInfoFrame - the pointer to HDMI Audio Infoframe ucData
// Return: N/A
// Remark: Fill the Audio InfoFrame ucData,and count checksum,then fill into
//         Audio InfoFrame registers.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////

SYS_STATUS hdmitx_SetAudioInfoFrame(Audio_InfoFrame *pAudioInfoFrame)
{
    BYTE checksum ;

    if(!pAudioInfoFrame)
    {
        return ER_FAIL ;
    }
    Switch_HDMITX_Bank(1);
    checksum = 0x100-(AUDIO_INFOFRAME_VER+AUDIO_INFOFRAME_TYPE+AUDIO_INFOFRAME_LEN );
    HDMITX_WriteI2C_Byte(REG_TX_PKT_AUDINFO_CC,pAudioInfoFrame->pktbyte.AUD_DB[0]);
    checksum -= HDMITX_ReadI2C_Byte(REG_TX_PKT_AUDINFO_CC); checksum &= 0xFF ;
    HDMITX_WriteI2C_Byte(REG_TX_PKT_AUDINFO_SF,pAudioInfoFrame->pktbyte.AUD_DB[1]);
    checksum -= HDMITX_ReadI2C_Byte(REG_TX_PKT_AUDINFO_SF); checksum &= 0xFF ;
    HDMITX_WriteI2C_Byte(REG_TX_PKT_AUDINFO_CA,pAudioInfoFrame->pktbyte.AUD_DB[3]);
    checksum -= HDMITX_ReadI2C_Byte(REG_TX_PKT_AUDINFO_CA); checksum &= 0xFF ;
    HDMITX_WriteI2C_Byte(REG_TX_PKT_AUDINFO_DM_LSV,pAudioInfoFrame->pktbyte.AUD_DB[4]);
    checksum -= HDMITX_ReadI2C_Byte(REG_TX_PKT_AUDINFO_DM_LSV); checksum &= 0xFF ;

    HDMITX_WriteI2C_Byte(REG_TX_PKT_AUDINFO_SUM,checksum);

    Switch_HDMITX_Bank(0);
    hdmitx_ENABLE_AUD_INFOFRM_PKT();
    return ER_SUCCESS ;
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_SetSPDInfoFrame()
// Parameter: pSPDInfoFrame - the pointer to HDMI SPD Infoframe ucData
// Return: N/A
// Remark: Fill the SPD InfoFrame ucData,and count checksum,then fill into
//         SPD InfoFrame registers.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////

SYS_STATUS hdmitx_SetSPDInfoFrame(SPD_InfoFrame *pSPDInfoFrame)
{
    int i ;
    BYTE ucData ;

    if(!pSPDInfoFrame)
    {
        return ER_FAIL ;
    }
    Switch_HDMITX_Bank(1);
    for(i = 0,ucData = 0 ; i < 25 ; i++)
    {
        ucData -= pSPDInfoFrame->pktbyte.SPD_DB[i] ;
        HDMITX_WriteI2C_Byte(REG_TX_PKT_SPDINFO_PB1+i,pSPDInfoFrame->pktbyte.SPD_DB[i]);
    }
    ucData -= SPD_INFOFRAME_VER+SPD_INFOFRAME_TYPE+SPD_INFOFRAME_LEN ;
    HDMITX_WriteI2C_Byte(REG_TX_PKT_SPDINFO_SUM,ucData); // checksum
    Switch_HDMITX_Bank(0);
    hdmitx_ENABLE_SPD_INFOFRM_PKT();
    return ER_SUCCESS ;
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_SetMPEGInfoFrame()
// Parameter: pMPEGInfoFrame - the pointer to HDMI MPEG Infoframe ucData
// Return: N/A
// Remark: Fill the MPEG InfoFrame ucData,and count checksum,then fill into
//         MPEG InfoFrame registers.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////

SYS_STATUS hdmitx_SetMPEGInfoFrame(MPEG_InfoFrame *pMPGInfoFrame)
{
    int i ;
    BYTE ucData ;

    if(!pMPGInfoFrame)
    {
        return ER_FAIL ;
    }
    Switch_HDMITX_Bank(1);

    HDMITX_WriteI2C_Byte(REG_TX_PKT_MPGINFO_FMT,pMPGInfoFrame->info.FieldRepeat|(pMPGInfoFrame->info.MpegFrame<<1));
    HDMITX_WriteI2C_Byte(REG_TX_PKG_MPGINFO_DB0,pMPGInfoFrame->pktbyte.MPG_DB[0]);
    HDMITX_WriteI2C_Byte(REG_TX_PKG_MPGINFO_DB1,pMPGInfoFrame->pktbyte.MPG_DB[1]);
    HDMITX_WriteI2C_Byte(REG_TX_PKG_MPGINFO_DB2,pMPGInfoFrame->pktbyte.MPG_DB[2]);
    HDMITX_WriteI2C_Byte(REG_TX_PKG_MPGINFO_DB3,pMPGInfoFrame->pktbyte.MPG_DB[3]);

    for(ucData = 0,i = 0 ; i < 5 ; i++)
    {
        ucData -= pMPGInfoFrame->pktbyte.MPG_DB[i] ;
    }
    ucData -= MPEG_INFOFRAME_VER+MPEG_INFOFRAME_TYPE+MPEG_INFOFRAME_LEN ;

    HDMITX_WriteI2C_Byte(REG_TX_PKG_MPGINFO_SUM,ucData);

    Switch_HDMITX_Bank(0);
    hdmitx_ENABLE_SPD_INFOFRM_PKT();

    return ER_SUCCESS ;
}

// 2009/12/04 added by Ming-chih.lung@ite.com.tw

SYS_STATUS hdmitx_SetVSIInfoFrame(VendorSpecific_InfoFrame *pVSIInfoFrame)
{
    BYTE ucData=0 ;

    if(!pVSIInfoFrame)
    {
        return ER_FAIL ;
    }

    Switch_HDMITX_Bank(1);
    HDMITX_WriteI2C_Byte(0x80,pVSIInfoFrame->pktbyte.VS_DB[3]);
    HDMITX_WriteI2C_Byte(0x81,pVSIInfoFrame->pktbyte.VS_DB[4]);

    ucData -= pVSIInfoFrame->pktbyte.VS_DB[3] ;
    ucData -= pVSIInfoFrame->pktbyte.VS_DB[4] ;

    if(  pVSIInfoFrame->pktbyte.VS_DB[4] & (1<<7 ))
    {
        ucData -= pVSIInfoFrame->pktbyte.VS_DB[5] ;
        HDMITX_WriteI2C_Byte(0x82,pVSIInfoFrame->pktbyte.VS_DB[5]);
        ucData -= VENDORSPEC_INFOFRAME_TYPE + VENDORSPEC_INFOFRAME_VER + 6 + 0x0C + 0x03 ;
    }
    else
    {
        ucData -= VENDORSPEC_INFOFRAME_TYPE + VENDORSPEC_INFOFRAME_VER + 5 + 0x0C + 0x03 ;
    }

    pVSIInfoFrame->pktbyte.CheckSum=ucData;

    HDMITX_WriteI2C_Byte(0x83,pVSIInfoFrame->pktbyte.CheckSum);
    Switch_HDMITX_Bank(0);
    HDMITX_WriteI2C_Byte(REG_TX_3D_INFO_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT);
    return ER_SUCCESS ;
}

SYS_STATUS hdmitx_Set_GeneralPurpose_PKT(BYTE *pData)
{
    int i ;

    if( pData == NULL )
    {
        return ER_FAIL ;

    }
    Switch_HDMITX_Bank(1);
    for( i = 0x38 ; i <= 0x56 ; i++)
    {
        HDMITX_WriteI2C_Byte(i, pData[i-0x38] );
    }
    Switch_HDMITX_Bank(0);
    hdmitx_ENABLE_GeneralPurpose_PKT();
    //hdmitx_ENABLE_NULL_PKT();
    return ER_SUCCESS ;
}

//////////////////////////////////////////////////////////////////////
// Function: DumpHDMITXReg()
// Parameter: N/A
// Return: N/A
// Remark: Debug function,dumps the registers of CAT6611.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////

#if Debug_message
void DumpHDMITXReg()
{
    int i,j ;
    BYTE ucData ;

	printk( "[%s]\n", __FUNCTION__);
    HDMITX_DEBUG_PRINTF(("       "));
    for(j = 0 ; j < 16 ; j++)
    {
        HDMITX_DEBUG_PRINTF((" %02X",(int)j));
        if((j == 3)||(j==7)||(j==11))
        {
            HDMITX_DEBUG_PRINTF(("  "));
        }
    }
    HDMITX_DEBUG_PRINTF(("\n        -----------------------------------------------------\n"));

    Switch_HDMITX_Bank(0);

    for(i = 0 ; i < 0x100 ; i+=16)
    {
        HDMITX_DEBUG_PRINTF(("[%3X]  ",i));
        for(j = 0 ; j < 16 ; j++)
        {
            if( (i+j)!= 0x17)
            {
                ucData = HDMITX_ReadI2C_Byte((BYTE)((i+j)&0xFF));
                HDMITX_DEBUG_PRINTF((" %02X",(int)ucData));
            }
            else
            {
                HDMITX_DEBUG_PRINTF((" XX")); // for DDC FIFO
            }
            if((j == 3)||(j==7)||(j==11))
            {
                HDMITX_DEBUG_PRINTF((" -"));
            }
        }
        HDMITX_DEBUG_PRINTF(("\n"));
        if((i % 0x40) == 0x30)
        {
            HDMITX_DEBUG_PRINTF(("        -----------------------------------------------------\n"));
        }
    }
    Switch_HDMITX_Bank(1);
    for(i = 0x130; i < 0x200 ; i+=16)
    {
        HDMITX_DEBUG_PRINTF(("[%3X]  ",i));
        for(j = 0 ; j < 16 ; j++)
        {
            ucData = HDMITX_ReadI2C_Byte((BYTE)((i+j)&0xFF));
            HDMITX_DEBUG_PRINTF((" %02X",(int)ucData));
            if((j == 3)||(j==7)||(j==11))
            {
                HDMITX_DEBUG_PRINTF((" -"));
            }
        }
        HDMITX_DEBUG_PRINTF(("\n"));
        if((i % 0x40) == 0x20)
        {
            HDMITX_DEBUG_PRINTF(("        -----------------------------------------------------\n"));
        }
    }
            HDMITX_DEBUG_PRINTF(("        -----------------------------------------------------\n"));
    Switch_HDMITX_Bank(0);
}

#endif

