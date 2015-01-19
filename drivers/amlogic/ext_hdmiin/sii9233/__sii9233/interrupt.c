//------------------------------------------------------------------------------
// Copyright ? 2007, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1060 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------
#include <local_types.h>
#include <config.h>
#include <hal.h>
#include <registers.h>
#include <amf.h>
#if (CONF__SUPPORT_3D == ENABLE)	  
#include "infofrm.h"
#endif //#if (CONF__SUPPORT_3D == ENABLE)


//------------------------------------------------------------------------------
// Choose interrupt polling method
//------------------------------------------------------------------------------

//read interrupt register bit
#define INTERRUPT_PENDING  ((RegisterRead(REG__INTR_STATE) & (BIT__INTR_GROUP0|BIT__INTR_GROUP1)) != 0)





//------------------------------------------------------------------------------
// Function: SetupInterruptMasks
// Description: Configure interrupt mask registers.
//              Any unmasked interrupt causes the hardware interrupt pin to be set when pendng.
//------------------------------------------------------------------------------
void SetupInterruptMasks(void)
{
    uint8_t abIntrMasks[8];

    abIntrMasks[0] = 0;
    abIntrMasks[1] = BIT__SCDT_CHG;
    abIntrMasks[2] = 0;
    abIntrMasks[3] = 0;
    abIntrMasks[4] = BIT__VRCHG | BIT__HRCHG;
    abIntrMasks[5] = BIT__NEW_ACP_PKT;  //check ACP type2 packet
    abIntrMasks[6] = BIT__VIDEO_READY;
    abIntrMasks[7] = BIT__NO_ACP_INF;


#if (CONF__ODCK_LIMITED==ENABLE)
    abIntrMasks[6] |= BIT__PCLK_STOP;
#endif //(CONF__ODCK_LIMITED==ENABLE)

    RegisterWriteBlock(REG__INTR1_UNMASK, &abIntrMasks[0], 4);
    RegisterWriteBlock(REG__INTR5_UNMASK, &abIntrMasks[4], 2);
    RegisterWriteBlock(REG__INTR7_UNMASK, &abIntrMasks[6], 2);

}

//------------------------------------------------------------------------------
// Function: ResetZoneControl
// Description:
// Note: reset the zone
//------------------------------------------------------------------------------

#if(PEBBLES_ES1_ZONE_WORKAROUND == ENABLE)

static void ResetZoneControl(void)
{
    {
        RegisterWrite(REG__FACTORY_A87,0x03);
        RegisterWrite(REG__FACTORY_A81,0x10);
        RegisterWrite(REG__FACTORY_A88,0x40);
        RegisterWrite(REG__FACTORY_A87,0x43);
        RegisterWrite(REG__FACTORY_A81,0x18);
        DEBUG_PRINT(("reset zone\n"));
        CurrentStatus.AudioState = STATE_AUDIO__REQUEST_AUDIO;
        CurrentStatus.VideoState = STATE_VIDEO__MUTED;
    }

}
#if (PEBBLES_VIDEO_STATUS_2ND_CHECK==ENABLE)
//------------------------------------------------------------------------------
// Function: PclkCheck
// Description:  check if the pclk stopped by reading the xpcnt register
//------------------------------------------------------------------------------

uint8_t PclkCheck(void)
{
    uint8_t xpcnt0_0;
    uint8_t xpcnt1_0;
    uint8_t xpcnt0_1;
    uint8_t xpcnt1_1;
    uint8_t wrongzone = 0;


    if((RegisterRead(REG__STATE))&BIT__PCLK_STABLE) //pclk stable
    {
        xpcnt0_0 = RegisterRead(0x06E);
        xpcnt1_0 = RegisterRead(0x06F);
        RegisterWrite(0x069,0x80);      //change base

//      DEBUG_PRINT(("full cnt=%02X %02X  ",(int)xpcnt0_0, (int)xpcnt1_0));
        xpcnt0_1 = RegisterRead(0x06E);
        xpcnt1_1 = RegisterRead(0x06F);
//      DEBUG_PRINT(("half cnt=%02X %02X\n",(int)xpcnt0_1, (int)xpcnt1_1));

        RegisterWrite(0x069,0xFF);

		if((xpcnt0_0==xpcnt0_1)&&(xpcnt1_0 <= xpcnt1_1))  //if xpcnt changes
            wrongzone=1;
        else
            return TRUE;
    }
    else
    {
        wrongzone=1;
    }
    if(wrongzone==1)
    {
        DEBUG_PRINT(("pclk stopped"));
        TurnVideoMute(ON);
        CurrentStatus.ColorDepth = 0;
        RegisterModify(REG__TMDS_CCTRL2, MSK__DC_CTL, VAL__DC_CTL_8BPP_1X);
    }

    return FALSE;
}
#endif //#if (PEBBLES_VIDEO_STATUS_2ND_CHECK==ENABLE)
//------------------------------------------------------------------------------
// Function: ConfigureZone
// Description:
// Note: check pclk status to configure the zone
//------------------------------------------------------------------------------

static uint8_t ConfigureZone(void)
{

    if((RegisterRead(REG__STATE))&BIT__PCLK_STABLE) //pclk stable
    {
        return TRUE;
    }
    else
    {
        DEBUG_PRINT((" **** pclk not stable\n"));
        ResetZoneControl();
    }

    return FALSE;
}

//------------------------------------------------------------------------------
// Function: ResetVideoControl
// Description:
// Note: check pclk status to configure the zone
//------------------------------------------------------------------------------
uint8_t ResetVideoControl(void)
{
    if(RegisterRead(REG__HDMI_MUTE)&BIT__VIDM_STATUS)
    {
        DEBUG_PRINT(("reset video control\n"));
        TurnVideoMute(ON);
        CurrentStatus.ColorDepth = 0;
        RegisterModify(REG__TMDS_CCTRL2, MSK__DC_CTL, VAL__DC_CTL_8BPP_1X);
        ResetZoneControl();
        return FALSE;
    }
    else //video unmute status OK
    {
        TurnVideoMute(OFF);
        return TRUE;
    }

}
#endif
//------------------------------------------------------------------------------
// Function: SetUpAudioOutput
// Description: Setup registers for audio output formatting for each format (PCM, DSD, HBR)
//              Each frmat has its own unique MCLK value.
//              Only PCM and DSD support multi channel output.
//
// Notes: Compile time configuration is done using CONF__* defines in config.h
//------------------------------------------------------------------------------
static void SetUpAudioOutput(void)
{
    uint8_t bAudioStatus;


    bAudioStatus = RegisterRead(REG__AUDP_STAT);

    if (bAudioStatus & BIT__DSD_STATUS) //DSD Audio
    {
        if(CurrentStatus.AudioMode!=AUDIO_MODE__DSD)
        {
            CurrentStatus.AudioMode = AUDIO_MODE__DSD;

            RegisterWrite(REG__I2S_CTRL1, CONF__DSD__I2S_CTRL1);

            if (bAudioStatus & BIT__HDMI_LO) //layout = 1, enable multi channel
                RegisterWrite(REG__I2S_CTRL2, CONF__DSD__I2S_CTRL2__LAYOUT_1 | BIT__MCLKEN);  //enable MCLK
            else  //layout = 0, enable 2 channel only
                RegisterWrite(REG__I2S_CTRL2, CONF__DSD__I2S_CTRL2__LAYOUT_0 | BIT__MCLKEN);  //enable MCLK
            DEBUG_PRINT(("DSD aud\n"));

        }
    }
    else if (bAudioStatus & BIT__HBRA_ON) //HBR Audio
    {

        CurrentStatus.AudioMode = AUDIO_MODE__HBR;

        RegisterWrite(REG__I2S_CTRL1, CONF__HBR__I2S_CTRL1);
        RegisterWrite(REG__I2S_CTRL2, CONF__HBR__I2S_CTRL2 | BIT__MCLKEN);  //enable MCLK
        DEBUG_PRINT(("HBR aud\n"));



    }
    else
    {
        if(CurrentStatus.AudioMode!=AUDIO_MODE__PCM)
        {
            CurrentStatus.AudioMode = AUDIO_MODE__PCM;
            RegisterWrite(REG__I2S_CTRL1, CONF__PCM__I2S_CTRL1);

            if (bAudioStatus & BIT__HDMI_LO) //layout = 1, enable multi channel
                RegisterWrite(REG__I2S_CTRL2, CONF__PCM__I2S_CTRL2__LAYOUT_1 | BIT__MCLKEN);  //enable MCLK
            else  //layout = 0, enable 2 channel only
                RegisterWrite(REG__I2S_CTRL2, CONF__PCM__I2S_CTRL2__LAYOUT_0 | BIT__MCLKEN);  //enable MCLK
            DEBUG_PRINT(("PCM aud\n"));
        }
    }

}
//------------------------------------------------------------------------------
// Function: AcpPacketHandler
// Description: disable digital output for ACP type 2 and above
//
//------------------------------------------------------------------------------
static void AcpPacketHandler(uint8_t qOn)
{
    //disable SPIDF when ACP type >=2 For simply test.
    if((qOn)&&(RegisterRead(REG__ACP_BYTE2) > 1))
    {
        RegisterWrite(REG__AUD_CTRL, (CONF__AUD_CTRL&(~BIT__SPEN)));
        DEBUG_PRINT(("ACP type > 1, digital output blocked\n"));
    }
    else
        RegisterWrite(REG__AUD_CTRL, CONF__AUD_CTRL);
}

//------------------------------------------------------------------------------
// Function: AudioUnmuteHandler
// Description: State machine for performing audio unmute in firmware.
//------------------------------------------------------------------------------
void AudioUnmuteHandler(void)
{
    uint8_t bIntrStatus2;
    uint8_t bIntrStatus4;

    RegisterModify(REG__ECC_CTRL, BIT__CAPCNT, SET);        //clear the error cont
    switch (CurrentStatus.AudioState)
    {
        case STATE_AUDIO__REQUEST_AUDIO:
        {
            bIntrStatus2 = RegisterRead(REG__INTR2);
            bIntrStatus4 = RegisterRead(REG__INTR4);

            if ((bIntrStatus2 & BIT__GOTAUD) && (bIntrStatus2 & BIT__GOTCTS))        //audio stable
            {
                if(bIntrStatus4 & MSK__CTS_ERROR)
                    // reset ACR
                    RegisterBitToggle(REG__SRST, BIT__ACRRST);
                else
                {
                    // init ACR
                    RegisterWrite(REG__ACR_CTRL1, BIT__ACR_INIT);

                    DEBUG_PRINT(("aud rdy\n"));

                    SetUpAudioOutput();
                    CurrentStatus.AudioState = STATE_AUDIO__AUDIO_READY;
                }
            }


                // clear all the audio interrupts
                RegisterWrite(REG__INTR2, (BIT__GOTAUD    | BIT__GOTCTS));
                RegisterWrite(REG__INTR4, (MSK__CTS_ERROR | MSK__FIFO_ERROR));

                TIMER_Set(TIMER__AUDIO, 20);  // delay 20ms before request audio again

        }
        break;

        case STATE_AUDIO__AUDIO_READY:
        {
            bIntrStatus4 = RegisterRead(REG__INTR4);
            if(bIntrStatus4 & MSK__CTS_ERROR)
                CurrentStatus.AudioState = STATE_AUDIO__REQUEST_AUDIO;

            else if (!(bIntrStatus4 & MSK__FIFO_ERROR))      // no audio FIFO error
            {
                if ((CurrentStatus.VideoState == STATE_VIDEO__ON)
                    ||(CurrentStatus.VideoState == STATE_VIDEO__CHECKED))
                    //||ResetVideoControl())//video unmute status ok
                {
                    if(CurrentStatus.AudioMode==AUDIO_MODE__DSD)
                        HAL_SetAudioDACMode(AUDIO_MODE__DSD);
                    else
                        HAL_SetAudioDACMode(AUDIO_MODE__PCM);

                    RegisterWrite(REG__INTR5, MSK__AUDIO_INTR);// clear all audio interrupts
                    CurrentStatus.AudioState = STATE_AUDIO__ON;
                    TurnAudioMute(OFF);
                }
            }
            else
            {
                // reset audio FIFO
                RegisterBitToggle(REG__SRST, BIT__FIFORST);

                // clear audio FIFO interrupts
                RegisterWrite(REG__INTR4, MSK__FIFO_ERROR);

                TIMER_Set(TIMER__AUDIO, 20);  // delay 20ms before ready audio again
            }
        }
    }
}





//------------------------------------------------------------------------------
// Function: ProcessScdtModeChange
// Description: Process SYNC detect on and off changes
//              This function is only called when the SCDT_CHG interrupt is set.
//------------------------------------------------------------------------------
extern void sii9233a_output_mode_trigger(unsigned int flag);

static void ProcessScdtModeChange(void)
{
    uint8_t bStateReg;
    static uint8_t current_scdt = 0;

    bStateReg = RegisterRead(REG__STATE);  //read scdt off or on

    if (bStateReg & BIT__SCDT) //SCDT on
    {
        //mute the audio if it is still on, it happens when SCDT off and on in a very short time
        if(CurrentStatus.AudioState == STATE_AUDIO__ON)
        {
            TurnAudioMute(ON);
            CurrentStatus.AudioState = STATE_AUDIO__MUTED;
        }

        CurrentStatus.ColorDepth = 0;
        RegisterModify(REG__TMDS_CCTRL2, MSK__DC_CTL, VAL__DC_CTL_8BPP_1X);

        CurrentStatus.ResolutionChangeCount = 0;
        CurrentStatus.VideoStabilityCheckCount = 0;

        CurrentStatus.AudioState = STATE_AUDIO__REQUEST_AUDIO;
        RegisterModify(REG__ECC_CTRL, BIT__CAPCNT, SET);        //clear the error cont
        CurrentStatus.VideoState = STATE_VIDEO__UNMUTE;
        TIMER_Set(TIMER__VIDEO, VIDEO_UNMUTE_TIMEOUT);  // start the video timer

		HdmiInitIf(); //yma add for 3D packet support
        printk("sii9233a SCDT on!\n");
        if( current_scdt == 0 )
        {
            sii9233a_output_mode_trigger(1);
            current_scdt = 1;
        }
    }
    else //SCDT off
    {
        TurnVideoMute(ON);
        printk("sii9233a SCDT off!\n");
        if( current_scdt == 1 )
        {
            sii9233a_output_mode_trigger(0);
            current_scdt = 0;
        }
    }
}

//------------------------------------------------------------------------------
// Function: ConfigureDeepColor
// Description: Configure the deep color clock based on input video format.
//              Change is made only when input color depth changes.
//------------------------------------------------------------------------------

static void ConfigureDeepColor(void)
{
    uint8_t abDcCtlVal1x[] = { VAL__DC_CTL_8BPP_1X, VAL__DC_CTL_10BPP_1X, VAL__DC_CTL_12BPP_1X, 0 };
    uint8_t abDcCtlVal2x[] = { VAL__DC_CTL_8BPP_2X, VAL__DC_CTL_10BPP_2X, VAL__DC_CTL_12BPP_2X, 0 };
    uint8_t bColorDepth;
    uint8_t bDcCtlValue;
    uint8_t vStatus;

    bColorDepth = RegisterRead(REG__DC_STAT) & MSK__PIXEL_DEPTH;


    if (bColorDepth != CurrentStatus.ColorDepth)
    {
        if(CurrentStatus.VideoState == STATE_VIDEO__ON)
            vStatus = ON;
        CurrentStatus.ColorDepth = bColorDepth;

        if(vStatus == ON)
            TurnVideoMute(ON);

        //value is 2x if muxYC output is enabled
        if (RegisterRead(REG__VID_AOF) & BIT__MUXYC)
        {
            bDcCtlValue = abDcCtlVal2x[ bColorDepth ];
        }
        else
        {
            bDcCtlValue = abDcCtlVal1x[ bColorDepth ];
        }
        RegisterModify(REG__TMDS_CCTRL2, MSK__DC_CTL, bDcCtlValue);
        RegisterBitToggle(REG__SRST2, BIT__DCFIFO_RST);  //reset the deep color FIFO

        DEBUG_PRINT(("DC stat=%02X ctl=%02X\n", (int)bColorDepth, (int)bDcCtlValue));

        if(vStatus == ON)
            TurnVideoMute(OFF);
    }
}





//------------------------------------------------------------------------------
// Function: TurnAudioMute
// Description:
// Note: ON is to mute, OFF is to unmute
//------------------------------------------------------------------------------
void TurnAudioMute (uint8_t qOn)
{

    if (qOn) //mute
    {
        //mute audio
        RegisterModify(REG__HDMI_MUTE, BIT__AUDM, SET);

        //AAC off
        RegisterModify(REG__AEC_CTRL, BIT__AAC_EN, CLEAR);
        RegisterModify(REG__INTR5_UNMASK, BIT__AACDONE, CLEAR);  //AAC intr off

        //power down the audio DAC
        HAL_PowerDownAudioDAC();

        DEBUG_PRINT(("Audio off\n"));

    }
    else //unmute
    {
        //AAC on
        RegisterModify(REG__INTR5_UNMASK, BIT__AACDONE, SET);   //AAC intr on
        RegisterModify(REG__AEC_CTRL, (BIT__AAC_EN ), SET); //AAC enable

        //unmute audio
        RegisterModify(REG__HDMI_MUTE, BIT__AUDM, CLEAR);

        //power up the audio DAC
        if(CurrentStatus.AudioMode!=AUDIO_MODE__HBR)
            HAL_WakeUpAudioDAC();

        DEBUG_PRINT(("Audio on\n"));

    }
}






//------------------------------------------------------------------------------
// Function: TurnPowerDown
// Description: Enter or exit powerdown mode
// Note: ON is to powerdown, OFF is to wakeup
//------------------------------------------------------------------------------
void TurnPowerDown(uint8_t qOn)
{
    if (qOn) //powerDown
    {
        RegisterModify(REG__SYS_CTRL1, BIT__PD, CLEAR);
    }
    else  //wakeup
    {
        RegisterModify(REG__SYS_CTRL1, BIT__PD, SET);
        DEBUG_PRINT(("wake up\n"));
    }
}




#if (PEBBLES_ES1_FF_WORKAROUND == ENABLE)
//------------------------------------------------------------------------------
// Function: ProcessVhResolutionChange
// Description: workaround for zone detection issue with FF part
//------------------------------------------------------------------------------

static uint8_t ProcessVhResolutionChange(void)
{
    if (CurrentStatus.ResolutionChangeCount++ == 10)
        {
            CurrentStatus.ResolutionChangeCount = 0;
            DEBUG_PRINT((" **** VHreschng\n"));
            if((RegisterRead(REG__FACTORY_A88)&0x0F)>=0x02) //not 1x zone
            ResetZoneControl();
            return TRUE;
        }
    return FALSE;
}
#endif

#if (CONF__ODCK_LIMITED==ENABLE)
//------------------------------------------------------------------------------
// Function: ProcessOdckStop
// Description: handles the ODCK (internally pclk) stop interrupt
//------------------------------------------------------------------------------
static void ProcessOdckStop(void)
{
     RegisterModify(REG__DRIVE_CNTL, MSK__ODCK_STRENGTH,CLEAR);  //clear the ODCK first
     RegisterModify(REG__SYS_PSTOP,BIT__PSTOP_EN, CLEAR);        //disable PSTOP feature
     RegisterModify(REG__INTR7,BIT__PCLK_STOP,SET);              //clear the interrupt

}
#endif //(CONF__ODCK_LIMITED==ENABLE)
//------------------------------------------------------------------------------
// Function: TurnVideoMute
// Description: Enable or disable Video Mute
// Note: ON is to mute, OFF is to unmute
//------------------------------------------------------------------------------
void TurnVideoMute(uint8_t qOn)
{
    if (qOn) //mute
    {
        RegisterModify(REG__HDMI_MUTE, BIT__VIDM, SET);
        CurrentStatus.VideoState = STATE_VIDEO__MUTED;
        DEBUG_PRINT(("Video off\n"));

    }
    else  //unmute
    {
#if(PEBBLES_ES1_ZONE_WORKAROUND == ENABLE)
        if((RegisterRead(REG__STATE)& BIT__SCDT)            //check sync status
            &&ConfigureZone())
#endif
        {
#if (CONF__ODCK_LIMITED==ENABLE)
     RegisterModify(REG__SYS_PSTOP,BIT__PSTOP_EN, SET);      //enable PSTOP feature
     RegisterModify(REG__DRIVE_CNTL, MSK__ODCK_STRENGTH,SET);    //clear the ODCK first
#endif// #if (CONF__ODCK_LIMITED==ENABLE)

            RegisterModify(REG__HDMI_MUTE, BIT__VIDM, CLEAR);
            CurrentStatus.VideoState = STATE_VIDEO__ON;
            DEBUG_PRINT(("Video on\n"));
#if (PEBBLES_VIDEO_STATUS_2ND_CHECK==ENABLE)
            TIMER_Set(TIMER__VIDEO, VIDEO_STABLITY_CHECK_INTERVAL);  // start the video timer
            CurrentStatus.VideoStabilityCheckCount = 0;
#endif//#if (PEBBLES_VIDEO_STATUS_2ND_CHECK==ENABLE)

        }

    }
}
//------------------------------------------------------------------------------
// Function: InterruptHandler
// Description: Main interrupt handler called when HDMI interrupt is pending.
//              All pending interrupts are cleared immediately.
//              Each pending interrupt is then handled by calling the associated handler function.
//------------------------------------------------------------------------------
#define INTR3_NEW_INFOFR            2
#define INTR3_NEW_INFOFR_SEL        0x1F

#define INTR4_NO_AVI                3
#define INTR4_NO_AVI_SEL            0x10

#define INTR5_NEW_ACP               5
#define INTR5_NEW_ACP_SEL           0x04

#define INTR8_NEW_GMT_NO_INFOFR     7
#define INTR8_NEW_GMT_SEL           0x80
#define INTR8_NO_INFOFR_SEL         0x7E

static void InterruptHandler(void)
{
    uint8_t abIntrStatus[8];
    uint8_t bNewInfoFrm	= 0;
    uint8_t bNoInfoFrm		= 0;

    //read all interrupt registers
    RegisterReadBlock(REG__INTR1, &abIntrStatus[0], 4);
    RegisterReadBlock(REG__INTR5, &abIntrStatus[4], 2);

    RegisterReadBlock(REG__INTR7, &abIntrStatus[6], 2);

#if 0
    DEBUG_PRINT(("i: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                (int)abIntrStatus[0],(int)abIntrStatus[1],(int)abIntrStatus[2],(int)abIntrStatus[3],
                (int)abIntrStatus[4],(int)abIntrStatus[5],(int)abIntrStatus[6],(int)abIntrStatus[7]));
#endif
    //clear all pending interrupts
    RegisterWriteBlock(REG__INTR1, &abIntrStatus[0], 4);
    RegisterWriteBlock(REG__INTR5, &abIntrStatus[4], 2);
    RegisterWriteBlock(REG__INTR7, &abIntrStatus[6], 2);

#if (CONF__ODCK_LIMITED==ENABLE)
    if (abIntrStatus[6] & BIT__PCLK_STOP)   //ODCK stopped
    {
        DEBUG_PRINT(("ODCK stopped\n"));
        ProcessOdckStop();
    }
#endif //#if (CONF__ODCK_LIMITED==ENABLED)

    // process pending interrupts
    if (abIntrStatus[1] & BIT__SCDT_CHG)    //if SCDT change
        ProcessScdtModeChange();

    if (abIntrStatus[4] & BIT__AACDONE)    //if soft mute done
    {
        TurnAudioMute(ON);
        CurrentStatus.AudioState = STATE_AUDIO__REQUEST_AUDIO;
    }
#if (CONF__VSYNC_OVERFLOW == ENABLE)
    if (abIntrStatus[4] & BIT__HRCHG )       //if H res change
#else
    if (abIntrStatus[4] & (BIT__HRCHG|BIT__VRCHG))       //if H/V res change
#endif
    {
#if (PEBBLES_ES1_FF_WORKAROUND == ENABLE)
        if (!ProcessVhResolutionChange())
#endif
        ConfigureDeepColor();
    }

    if (abIntrStatus[5] & BIT__NEW_ACP_PKT)
       AcpPacketHandler(ON);

    if (abIntrStatus[6] & BIT__VIDEO_READY) //video ready for unmute
    {
        DEBUG_PRINT(("Video rdy\n"));
        TurnVideoMute(OFF);
    }

    if (abIntrStatus[7] & BIT__NO_ACP_INF)  // no ACP pkt
       AcpPacketHandler(OFF);

#if (CONF__SUPPORT_3D == ENABLE)
    /*****  Processing info frame interrupts    ***********************/
    bNewInfoFrm = abIntrStatus[INTR3_NEW_INFOFR] & INTR3_NEW_INFOFR_SEL;
    bNoInfoFrm = abIntrStatus[INTR8_NEW_GMT_NO_INFOFR] & INTR8_NO_INFOFR_SEL;
    if( abIntrStatus[INTR4_NO_AVI] & INTR4_NO_AVI_SEL ){
        bNoInfoFrm |= BIT__VIRT_NO_AVI_INF;
    }
    if ( abIntrStatus[INTR8_NEW_GMT_NO_INFOFR] & BIT__NEW_GDB_INF ){
        bNewInfoFrm |= BIT__VIRT_NEW_GDB_INF;
    }
    if ( abIntrStatus[INTR5_NEW_ACP] & BIT__NEW_ACP_PKT ){
        bNewInfoFrm |= BIT__VIRT_NEW_ACP_INF;
    }
    /*  Check if any of info frame interrupts has occured   */
    if( bNewInfoFrm || bNoInfoFrm ){
        InterInfoFrmProc ( bNewInfoFrm, bNoInfoFrm );
    }
#endif //#if (CONF__SUPPORT_3D == ENABLE)

    /*****  End Processing info frame interrupts    ***********************/
}



//------------------------------------------------------------------------------
// Function: PollInterrupt
// Description: Poll for HDMI interrupt.
//              This can be polling the interrupt pin on the processor,
//              or polling the register over I2C.
//------------------------------------------------------------------------------
void PollInterrupt(void)
{
    if (INTERRUPT_PENDING)
         InterruptHandler();
}
