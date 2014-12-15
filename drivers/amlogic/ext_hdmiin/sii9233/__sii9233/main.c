//------------------------------------------------------------------------------
// Copyright ? 2007, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1060 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------
#include <linux/err.h>
#include <linux/delay.h>
#include <local_types.h>
#include <config.h>
#include <hal.h>
//#include <debug_cmd.h>
#include <registers.h>
#include <amf.h>
#include "infofrm.h"

#include <CEC.h>


//------------------------------------------------------------------------------
// Global State Structure
//------------------------------------------------------------------------------
GlobalStatus_t CurrentStatus;



//------------------------------------------------------------------------------
// Function: AutoVideoSetup
// Description: Setup registers for Auto Video Mode
//
// Notes: Compile time configuration is done using CONF__* defines in config.h
//------------------------------------------------------------------------------
static void AutoVideoSetup(void)
{
    
	const uint8_t unmuteTimeConf[] = {0xFF,0x00,0x00,0xFF,0x00,0x00};
	RegisterWriteBlock(REG__WAIT_CYCLE, (uint8_t *)&unmuteTimeConf[0],6);	//video unmute wait 

    RegisterWrite(REG__VID_CTRL,  (BIT__IVS   & CONF__VSYNC_INVERT) |
                                  (BIT__IHS   & CONF__HSYNC_INVERT) );  //set HSYNC,VSNC polarity
    RegisterWrite(REG__RPI_AUTO_CONFIG, BIT__CHECKSUM_EN|BIT__V_UNMUTE_EN|BIT__HCDP_EN|BIT__TERM_EN);        //auto config
    RegisterWrite(REG__SRST,      BIT__SWRST_AUTO);            //enable auto sw reset
    RegisterWrite(REG__VID_AOF,   CONF__OUTPUT_VIDEO_FORMAT);  //set output video format
    RegisterModify(REG__AEC_CTRL,  BIT__AVC_EN, SET);                //enable auto video configuration

#if (CONF__ODCK_LIMITED == ENABLE)
	RegisterModify(REG__SYS_PSTOP, MSK__PCLK_MAX, CONF__PCLK_MAX_CNT);
#endif //(CONF__ODCK_LIMITED==ENABLE)


#if (PEBBLES_ES1_ZONE_WORKAROUND == ENABLE)	
	RegisterWrite(REG__AVC_EN2, BIT__AUTO_DC_CONF);			   //mask out auto configure deep color clock
	RegisterWrite(REG__VIDA_XPCNT_EN, BIT__VIDA_XPCNT_EN);	   //en read xpcnt
#endif //(PEBBLES_ES1_ZONE_WORKAROUND == ENABLE)	 

#if (PEBBLES_STARTER_NO_CLK_DIVIDER == ENABLE)
	RegisterModify(REG__AVC_EN2, BIT__AUTO_CLK_DIVIDER,SET);	  //msk out auto clk divider
#endif //(PEBBLES_STARTER_NO_CLK_DIVIDER == ENABLE)
}



//------------------------------------------------------------------------------
// Function: AutoAudioSetup
// Description: Setup registers for Auto Audio Mode
//------------------------------------------------------------------------------
static void AutoAudioSetup(void)
{
    uint8_t abAecEnables[3];

    RegisterModify(REG__ACR_CTRL3, MSK__CTS_THRESH, VAL__CTS_THRESH( CONF__CTS_THRESH_VALUE ));

    abAecEnables[0] = (BIT__SYNC_DETECT        |
                       BIT__CKDT_DETECT        |
                       BIT__CABLE_UNPLUG       );
    abAecEnables[1] = (BIT__HDMI_MODE_CHANGED  |
                       BIT__CTS_REUSED         |
                       BIT__AUDIO_FIFO_UNDERUN |
                       BIT__AUDIO_FIFO_OVERRUN |
                       BIT__FS_CHANGED         |
                       BIT__H_RES_CHANGED      );
#if (CONF__VSYNC_OVERFLOW != ENABLE)   
    abAecEnables[2] = (BIT__V_RES_CHANGED      );
#endif
    RegisterWriteBlock(REG__AEC_EN1, abAecEnables, 3);
	RegisterModify(REG__AEC_CTRL, BIT__CTRL_ACR_EN, SET);

}


//------------------------------------------------------------------------------
// Function: ConfigureSelectedPort
// Description: Setup new input port after port change
//------------------------------------------------------------------------------
static void ConfigureSelectedPort(void)
{		  
	
    switch (CurrentStatus.PortSelection)
    {
        case PORT_SELECT__PORT_0:
        {	
            RegisterModify(REG__PORT_SWTCH2, MSK__PORT_EN,VAL__PORT0_EN);     //select port 0
            RegisterWrite(REG__PORT_SWTCH, BIT__DDC0_EN);     //select DDC 0
            HAL_VccEnable(ON);
        }
        break;

        case PORT_SELECT__PORT_1:
        {
            RegisterModify(REG__PORT_SWTCH2, MSK__PORT_EN,VAL__PORT1_EN);     //select port 1
			RegisterWrite(REG__PORT_SWTCH, BIT__DDC1_EN);     //select DDC 1
            HAL_VccEnable(ON);
        }
        break;

        case PORT_SELECT__PORT_2:
        {
            RegisterModify(REG__PORT_SWTCH2, MSK__PORT_EN,VAL__PORT2_EN);     //select port 2
			RegisterWrite(REG__PORT_SWTCH, BIT__DDC2_EN);     //select DDC 2
            HAL_VccEnable(ON);
        }
        break;

        case PORT_SELECT__PORT_3:
        {
            RegisterModify(REG__PORT_SWTCH2, MSK__PORT_EN,VAL__PORT3_EN);     //select port 3
			RegisterWrite(REG__PORT_SWTCH, BIT__DDC3_EN);     //select DDC 3
            HAL_VccEnable(ON);
        }
        break;

        case PORT_SELECT__PORT_7:
        {
            DEBUG_PRINT(("turn off 3V power! \n"));	   //power down
            HAL_VccEnable(OFF);
        }
        break;
    }	//end of switch

}




//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Function: SystemDataReset
// Description: Re-initialize receiver state
//------------------------------------------------------------------------------
void SystemDataReset(void)
{

    TurnAudioMute(ON);
    TurnVideoMute(ON);

	CurrentStatus.ResolutionChangeCount = 0;
    CurrentStatus.ColorDepth = 0;
	CurrentStatus.AudioMode = AUDIO_MODE__NOT_INIT;
    RegisterModify(REG__TMDS_CCTRL2, MSK__DC_CTL, VAL__DC_CTL_8BPP_1X);

    ConfigureSelectedPort();

}



//------------------------------------------------------------------------------
// Function: PollPortSwitch
// Description:
//------------------------------------------------------------------------------
static void PollPortSwitch(void)
{
    uint8_t bNewPortSelection;

    bNewPortSelection = HAL_GetPortSelectionDebounced();

    if (bNewPortSelection != PORT_SELECT__NO_CHANGE)
    {
        if (CurrentStatus.PortSelection != bNewPortSelection)
		{
		    CurrentStatus.PortSelection = bNewPortSelection;
			SystemDataReset();
		}
    }
}


//------------------------------------------------------------------------------
// Function: InitializePortSwitch
// Description:	called once in SystemInit() function
//------------------------------------------------------------------------------

static void InitializePortSwitch(void)
{
    CurrentStatus.PortSelection = PORT_SELECT__NO_CHANGE;
	PollPortSwitch();

}

//------------------------------------------------------------------------------
// Function: System_Init
// Description: One time initialization at statup
//------------------------------------------------------------------------------
static void SystemInit(void)
{
	const uint8_t EQTable[] = {0x8A,0xAA,0x1A,0x2A};

    while( (RegisterRead(REG__BSM_STAT)& BIT__BOOT_DONE )== 0) //wait done
        DEBUG_PRINT(("BIT__BOOT_DONE = 0; \n"));

    if((RegisterRead(REG__BSM_STAT)& BIT__BOOT_ERROR)!=0)
        DEBUG_PRINT(("First Boot error! \n"));

	RegisterModify(REG__HPD_HW_CTRL,MSK__INVALIDATE_ALL, SET);	//disable auto HPD conf at RESET
	TurnAudioMute(ON);
	TurnVideoMute(ON);

#if(PEBBLES_ES1_STARTER_CONF==ENABLE)
    RegisterWrite(REG__TERMCTRL2, VAL__45OHM); 			//1.term default value	

    RegisterWrite(REG__FACTORY_A87,0x43);              //2.Set PLL mode to internal and set selcalrefg to F
    RegisterWrite(REG__FACTORY_A81,0x18);              //Set PLL zone to auto and set Div20 to 1

    RegisterWrite(REG__DRIVE_CNTL,0x64);               //3.change output strength,  

    RegisterWrite(REG__FACTORY_ABB,0x04);              //4.desable DEC_CON

    RegisterWriteBlock(REG__FACTORY_A92,(uint8_t *)&EQTable[0],4);//5.Repgrogram EQ table
    RegisterWrite(REG__FACTORY_AB5,0x40);              //EnableEQ

    RegisterWrite(REG__FACTORY_9E5, 0x02);             //6. DLL by pass
	RegisterWrite(REG__FACTORY_A89,0x00);			   //7. configure the PLLbias 	
	RegisterWrite(REG__FACTORY_00E,0x40);  			   //for ES1.1 conf only
#endif
			
    CEC_Init();					  
    //set recommended values
    RegisterWrite(REG__AACR_CFG1, CONF__AACR_CFG1_VALUE);   //pll config #1
    RegisterWrite(REG__CBUS_PAD_SC, VAL__SC_CONF);  		//CBUS slew rate 
    RegisterWrite(REG__SRST,  BIT__SWRST_AUTO);             //enable auto sw reset
	RegisterWrite(REG__INFM_CLR,BIT__CLR_GBD|BIT__CLR_ACP);	//clr GBD & ACP

    RegisterWrite(REG__ECC_HDCP_THRES, CONF__HDCPTHRESH & 0xff);      //HDCP threshold low uint8_t
    RegisterWrite(REG__ECC_HDCP_THRES+1, (CONF__HDCPTHRESH>>8) & 0xff);  //HDCP threshold high uint8_t
    AutoVideoSetup();
    AutoAudioSetup();    
    SetupInterruptMasks();
	InitializePortSwitch();
    TurnPowerDown(OFF);	 						   	
	RegisterModify(REG__HPD_HW_CTRL,MSK__INVALIDATE_ALL, CLEAR); //CLEAR disable auto HPD conf 

	/* Inti Hdmi Info frame related chip registers and data */
	HdmiInitIf ();
}

//------------------------------------------------------------------------------
// Function: HdmiTask
// Description: One time initialization at startup
//------------------------------------------------------------------------------
static void HdmiTask(void)
{

    if ((CurrentStatus.AudioState == STATE_AUDIO__REQUEST_AUDIO) ||
        (CurrentStatus.AudioState == STATE_AUDIO__AUDIO_READY))
    {
        if (TIMER_Expired(TIMER__AUDIO))
        {
            AudioUnmuteHandler();
        }
    }

    if (CurrentStatus.VideoState == STATE_VIDEO__UNMUTE)
    {
        if (TIMER_Expired(TIMER__VIDEO))
        {
			DEBUG_PRINT(("v time out\n"));
			ResetVideoControl();
        }
    }
#if (PEBBLES_VIDEO_STATUS_2ND_CHECK==ENABLE)
	if (CurrentStatus.VideoState == STATE_VIDEO__ON)
	{
        if (TIMER_Expired(TIMER__VIDEO))
        {
			PclkCheck();
        	TIMER_Set(TIMER__VIDEO, VIDEO_STABLITY_CHECK_INTERVAL);  // start the video timer 
			CurrentStatus.VideoStabilityCheckCount++;

        }
		if (CurrentStatus.VideoStabilityCheckCount == VIDEO_STABLITY_CHECK_TOTAL/VIDEO_STABLITY_CHECK_INTERVAL)
		{
			DEBUG_PRINT(("V CHECK %d times\n",(int)CurrentStatus.VideoStabilityCheckCount));
			CurrentStatus.VideoState = STATE_VIDEO__CHECKED;
			CurrentStatus.VideoStabilityCheckCount = 0;
		}					  										
	}
#endif //#if (PEBBLES_VIDEO_STATUS_2ND_CHECK==ENABLE)
}



//------------------------------------------------------------------------------
// Function: main
// Description: Main loop
//------------------------------------------------------------------------------
void sii9223a_main(void)
{
	uint16_t wOldTickCnt = 0;
	uint16_t wNewTickCnt = 0;
	uint16_t wTickDiff;

//    HAL_Init();

//    DEBUG_INIT();
    DEBUG_PRINT(("AMF Pebble ES1_2 starter board version 2.3.0\n"));

    SystemInit();

    while (1)
    {
//        if ( (!DEBUG_PAUSE_FIRMWARE) && (!GPIO_GetComMode())  )
//        if ( !GPIO_GetComMode() )
        {
            if (TIMER_Expired(TIMER__POLLING))
            {
                TIMER_Set(TIMER__POLLING, 20);       //poll every 20ms	   
                PollPortSwitch();  
                PollInterrupt();
            }
#if (CONF__CEC_ENABLE == ENABLE)
			CEC_Event_Handler();
#endif

			HdmiTask();
			wNewTickCnt = TIMER_GetTickCounter();
			if ( wNewTickCnt > wOldTickCnt ){
				wTickDiff = wNewTickCnt - wOldTickCnt;
			}
			else { /* counter wrapping */
				wTickDiff = ( 0xFFFF - wOldTickCnt ) + wNewTickCnt;
			}
			wTickDiff >>= 1; /* scaling ticks to ms */
			if ( wTickDiff > 36 ){
				wOldTickCnt = wNewTickCnt;
				HdmiProcIfTo( wTickDiff );
			}
        }
        msleep(10);
//        DEBUG_POLL();
    }
}

