//------------------------------------------------------------------------------
// Project: HDMI Repeater
// Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1140 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------

#include "si_common.h"
#include "si_drv_rx_isr.h"
#include "si_rx_video_mode_detection.h"
#include "si_rx_info.h"
#include "si_rx_audio.h"

#include "si_cra.h"

#include "si_drv_rx.h"

#include "si_sii5293_registers.h"

#define HDCP_FAIL_THRESHOLD_1ST			4
#define HDCP_FAIL_THRESHOLD_CONTINUED	100

#define NMB_OF_RX_INTERRUPTS	8
#define INT1	0
#define INT2	1
#define INT3	2
#define INT4	3
#define INT5	4
#define INT6	5
#define INT7	6
#define INT8	7

typedef struct
{
	//bit_fld_t check_hdcp_on_vsync;
	uint8_t hdcp_fail_cntr;
	uint8_t shadow_interrupt_mask[NMB_OF_RX_INTERRUPTS];
    bool_t bVidStableChgEvent;
    bool_t bScdtState;
    bool_t bCableChgEvent;
    bool_t bCableState;
}
rx_isr_type;

static rx_isr_type rx_isr = {0};


ROM const uint8_t default_interrupt_masks[NMB_OF_RX_INTERRUPTS] =
{
	// Interrupt 1
	//RX_M__INTR1__ACR_HW_CTS_CHANGED |
	//RX_M__INTR1__ACR_HW_N_CHANGED |
	//RX_M__INTR1__ACR_PACKET_ERROR |
	//RX_M__INTR1__ACR_PLL_UNLOCKED |
	//RX_M__INTR1__AUD_FIFO_ERROR |
	//RX_M__INTR1__ECC_ERROR |
	//RX_M__INTR1__AUTH_START |
	RX_M__INTR1__AUTH_DONE |
	0,

	// Interrupt 2
	RX_M__INTR2__HDMI_MODE |
	RX_M__INTR2__VSYNC |
	//RX_M__INTR2__SOFTW_INTR |
	RX_M__INTR2__CLOCK_DETECT | // enabled to clear an AAC exception
	RX_M__INTR2__SCDT |
	//RX_M__INTR2__GOT_CTS_PACKET |
	//RX_M__INTR2__GOT_AUDIO_PKT |
	RX_M__INTR2__VID_CLK_CHANGED |
	0,

	// Interrupt 3
	//RX_M__INTR3__NEW_CP_PACKET | // for Deep Color (called on every packet, not on the packet change)
	//RX_M__INTR3__CP_SET_MUTE |    // cannot use since continues coming when AVMUTE
	//RX_M__INTR3__PARITY_ERR |
    //RX_M__INTR3__NEW_MPEG_PACKET |
    RX_M__INTR3__NEW_AUD_PACKET |
	//RX_M__INTR3__NEW_SP_PACKET | 
	RX_M__INTR3__NEW_AVI_PACKET|
	0,

	// Interrupt 4
	RX_M__INTR4__HDCP |
	//RX_M__INTR4__T4 |
	RX_M__INTR4__NO_AVI |
	//RX_M__INTR4__CTS_OVERRUN |
	//RX_M__INTR4__CTS_UNDERRUN |
	//RX_M__INTR4__FIFO_OVERUN |
	//RX_M__INTR4__FIFO_UNDERUN |
	0,

	// Interrupt 5
	//RX_M__INTR5__FN_CHANGED |
	//RX_M__INTR5__AAC_DONE |
	//RX_M__INTR5__AUDIO_LINK_EROR |
	//RX_M__INTR5__V_RES_CHANGE |   //There should vid_clk change when resolution change, 
	//RX_M__INTR5__H_RES_CHANGE |
	//RX_M__INTR5__POLARITY_CHANGE |
	//RX_M__INTR5__INTERLACED_CHANGED |
	RX_M__INTR5__AUDIO_FS_CHANGED |
	0,

	// Interrupt 6
	//RX_M__INTR6__DC_ERROR | // this interrupt is enabled to just clear-up the error flag
	//RX_M__INTR6__AUD_FLAT |
	// RX_M__INTR6__CHST_READY | // cannot use this interrupt because it is triggered on every CHST receiving even nothing changes
	//RX_M__INTR6__DSD_MUTE_PATTERN_DETECT |
 	RX_M__INTR6__NEW_ACP_PACKET |
	RX_M__INTR6__CABLE_UNPLUG | // enabled to clear an AAC exception
	0,

	// Interrupt 7
	RX_M__INTR7__NO_VSI_PACKET |
	RX_M__INTR7__NEW_VSI_PACKET |
	//RX_M__INTR7__POWER_CHANGE |
	//RX_M__INTR7__CEC_FIFO_FULL |
	//RX_M__INTR7__VIDEO_CLK_CHANGED |
	//RX_M__INTR7__PCLK_STOPPED |
	//RX_M__INTR7__NO_DEEP_COLOR_PKT |
	//RX_M__INTR7__PCLK_STABLE_CHANGED |
	0,

	// Interrupt 8
	RX_M__INTR8__CABLE_IN |
	0
};

/*****************************************************************************/
/**
 *  @brief		Interrupt Service Routine Initialization
 *
 *****************************************************************************/
void RxIsr_Init(void)
{
	rx_isr.hdcp_fail_cntr = 0;
	//rx_isr.check_hdcp_on_vsync = false;
	memcpy(rx_isr.shadow_interrupt_mask, default_interrupt_masks, NMB_OF_RX_INTERRUPTS);

	SiiRegWriteBlock(RX_A__INTR1_MASK, &rx_isr.shadow_interrupt_mask[INT1], 4);
	SiiRegWriteBlock(RX_A__INTR5_MASK, &rx_isr.shadow_interrupt_mask[INT5], 2);
	SiiRegWriteBlock(RX_A__INTR7_MASK, &rx_isr.shadow_interrupt_mask[INT7], 2);
}


#if 0
/*****************************************************************************/
/**
 *  @brief		Switch HDCP Interrupts Handler
 *
 *  @param[in]		switch_on		true to switch on; false to switch off
 *
 *****************************************************************************/
void RxIsr_SwitchRxHdcpInterrupts(bool_t switch_on)
{
	if(switch_on)
	{
		rx_isr.shadow_interrupt_mask[INT1] |= (RX_M__INTR1__AUTH_START | RX_M__INTR1__AUTH_DONE);

		SiiRegWrite(RX_A__INTR1, RX_M__INTR1__AUTH_START | RX_M__INTR1__AUTH_DONE); // interrupt reset
	}
	else
	{
		rx_isr.shadow_interrupt_mask[INT1] &= ~RX_M__INTR1__AUTH_START;
		rx_isr.shadow_interrupt_mask[INT1] |= RX_M__INTR1__AUTH_DONE;
	}
	SiiRegWrite(RX_A__INTR1_MASK, rx_isr.shadow_interrupt_mask[INT1]); // set mask

	if(!switch_on) // just in case: reset interrupt if they were set
	{
		SiiRegWrite(RX_A__INTR1, RX_M__INTR1__AUTH_START | RX_M__INTR1__AUTH_DONE);
	}
}

/*****************************************************************************/
/**
 *  @brief		Switch No AVI InfoFrame Interrupts Handler
 *
 *  @param[in]		switch_on		true to switch on; false to switch off
 *
 *****************************************************************************/
static void switch_NoAVI_interrupt(bool_t switch_on)
{
	uint8_t pipe = SiiDrvRxInstanceGet();
	uint8_t mask4 = rx_isr[pipe].shadow_interrupt_mask[INT4];
	if(switch_on)
	{
		SiiRegWrite(RX_A__INTR4, RX_M__INTR4__NO_AVI); // clear No AVI interrupt if it was raised
		rx_isr[pipe].shadow_interrupt_mask[INT4] |= RX_M__INTR4_MASK__NO_AVI;
	}
	else
	{
		rx_isr[pipe].shadow_interrupt_mask[INT4] &= ~RX_M__INTR4_MASK__NO_AVI;
	}
	if(mask4 != rx_isr[pipe].shadow_interrupt_mask[INT4])
		SiiRegWrite(RX_A__INTR4_MASK, rx_isr[pipe].shadow_interrupt_mask[INT4]);

	// If NO AVI interrupt is ON, look for NEW AVI only.
	// If NO AVI interrupt is OFF, look for ANY AVI.
	RxIsr_SwitchReceiveInfoFrameOnEveryPacket(INFO_AVI, !switch_on);
}

/*****************************************************************************/
/**
 *  @brief		Switch No VSI InfoFrame Interrupts Handler
 *
 *  @param[in]		switch_on		true to switch on; false to switch off
 *
 *****************************************************************************/
static void switch_NoVSI_interrupt(bool_t switch_on)
{
	uint8_t pipe = SiiDrvRxInstanceGet();
	uint8_t mask7 = rx_isr[pipe].shadow_interrupt_mask[INT7];
	if(switch_on)
	{
		SiiRegWrite(RX_A__INTR7, RX_M__INTR7__NO_VSI_PACKET); // clear No AVI interrupt if it was raised
		rx_isr[pipe].shadow_interrupt_mask[INT7] |= RX_M__INTR7_MASK__NO_VSI_PACKET;
	}
	else
	{
		rx_isr[pipe].shadow_interrupt_mask[INT7] &= ~RX_M__INTR7_MASK__NO_VSI_PACKET;
	}
	if(mask7!= rx_isr[pipe].shadow_interrupt_mask[INT7])
		SiiRegWrite(RX_A__INTR7_MASK, rx_isr[pipe].shadow_interrupt_mask[INT7]);

	// If NO AVI interrupt is ON, look for NEW AVI only.
	// If NO AVI interrupt is OFF, look for ANY AVI.
	RxIsr_SwitchReceiveInfoFrameOnEveryPacket(INFO_VSI, !switch_on);
}


/*****************************************************************************/
/**
 *  @brief		Switch Receive Audio InfoFrame On Every Packet Interrupts Handler
 *
 *  @param[in]		switch_on		true to switch on; false to switch off
 *
 *****************************************************************************/
void RxIsr_SwitchReceiveInfoFrameOnEveryPacket(uint8_t info_type, bool_t switch_on)
{
    switch(info_type)
    {
        case INFO_AVI:
            SiiRegBitsSet(RX_A__INT_IF_CTRL, RX_M__INT_IF_CTRL__NEW_AVI, switch_on);
            SiiRegWrite(RX_A__INTR3, RX_M__INTR3__NEW_AVI_PACKET); // reset the interrupt
            break;
        case INFO_AUD:
            SiiRegBitsSet(RX_A__INT_IF_CTRL, RX_M__INT_IF_CTRL__NEW_AUD, switch_on);
            SiiRegWrite(RX_A__INTR3, RX_M__INTR3__NEW_AUD_PACKET); // reset the interrupt
            break;
        case INFO_ACP:
            SiiRegBitsSet(RX_A__INT_IF_CTRL, RX_M__INT_IF_CTRL__NEW_ACP, switch_on);
            SiiRegWrite(RX_A__INTR6, RX_M__INTR6__NEW_ACP_PACKET); // reset the interrupt
            break;
        case INFO_VSI:
            SiiRegBitsSet(RX_A__INT_IF_CTRL, RX_M__INT_IF_CTRL__NEW_VSI, switch_on);
            SiiRegWrite(RX_A__INTR7, RX_M__INTR7__NEW_VSI_PACKET); // reset the interrupt
            break;
        default:
            break;
            
    }
    
}
#endif

/*****************************************************************************/
/**
 *  @brief		Interrupt Handler for HDMI / DVI Transition
 *
 *****************************************************************************/
static void RxIsr_HdmiDviTransition(void)
{
	if(SiiDrvRxHdmiModeGet())
	{
		// Clear BCH counter and the interrupt associated with it.
		// It'll help avoiding a false HDCP Error interrupt caused by pre-HDMI
		// counter content.
		// Capture and clear BCH T4 errors.
		SiiRegWrite(RX_A__ECC_CTRL, RX_M__ECC_CTRL__CAPTURE_CNT);
		SiiRegWrite(RX_A__INTR4, RX_M__INTR4__HDCP); // reset the HDCP BCH error interrupt

		DEBUG_PRINT(MSG_STAT, ("RX: HDMI\n"));
		RxAudio_ReStart();
	}
	else
	{
		DEBUG_PRINT(MSG_STAT, ("RX: DVI\n"));
		// forget all HDMI settings
		RxInfo_ResetData();
		RxAudio_Stop();
	}
}

/*****************************************************************************/
/**
 *  @brief		Switch HDCP Failure Check with Vertical Sync Rate
 *
 *  @param[in]		switch_on		true to switch on; false to switch off
 *
 *****************************************************************************/
static void switch_hdcp_failure_check_with_v_sync_rate(bool_t switch_on)
{
	if(switch_on)
	{
		rx_isr.hdcp_fail_cntr = 1;
		//rx_isr.check_hdcp_on_vsync = true; // don't clear HDCP Failure Int if this bit is set
		rx_isr.shadow_interrupt_mask[INT2] |= RX_M__INTR2__VSYNC;
		rx_isr.shadow_interrupt_mask[INT4] &= ~RX_M__INTR4__HDCP;
	}
	else
	{
		rx_isr.hdcp_fail_cntr = 0;
		//rx_isr.check_hdcp_on_vsync = false;
		rx_isr.shadow_interrupt_mask[INT2] &= ~RX_M__INTR2__VSYNC;
		rx_isr.shadow_interrupt_mask[INT4] |= RX_M__INTR4__HDCP;
	}
	SiiRegWrite(RX_A__INTR2_MASK, rx_isr.shadow_interrupt_mask[INT2]);
	SiiRegWrite(RX_A__INTR4_MASK, rx_isr.shadow_interrupt_mask[INT4]);

	// Clear BCH counter.
	// The counter accomulates BCH errors and  if it is not cleared it can cause an HDCP failure interrupt
	// Capture and clear BCH T4 errors.
	SiiRegWrite(RX_A__ECC_CTRL, RX_M__ECC_CTRL__CAPTURE_CNT);

}

/*****************************************************************************/
/**
 *  @brief		Handler for HDCP Error
 *
 *****************************************************************************/
static void hdcp_error_handler(bool_t v_sync_mode)
{
	if(v_sync_mode)
	{
		if(0 == SiiRegReadWord(RX_A__HDCP_ERR))
		{
			// Recovered- return to normal checking mode
			DEBUG_PRINT(MSG_STAT, ( "RX: BCH recovered ***\n"));
			switch_hdcp_failure_check_with_v_sync_rate(OFF);
		}
		else
		{
			// Another failure
			rx_isr.hdcp_fail_cntr++;
			if(HDCP_FAIL_THRESHOLD_1ST == rx_isr.hdcp_fail_cntr)
			{
				DEBUG_PRINT(MSG_STAT, ( "RX: Cont. BCH Error ***\n"));

				// Reset Ri to notify an upstream device about the failure.
				// In most cases Ri is already mismatched if we see BCH errors,
				// but there is one rare case when the reseting can help.
				// It is when Ri and Ri' are matched all the time but Ri and Ri'
				// switching happens not synchronously causing a snow
				// screen flashing every 2 seconds. It may happen with
				// some old incomplaint sources or sinks (especially DVI).
				SiiRegWrite(RX_A__HDCP_DEBUG, RX_M__HDCP_DEBUG__CLEAR_RI);

				// Clear BCH counter.
				// The counter accumulates BCH errors and
				// if it is not cleared it can cause an HDCP failure interrupt.
				// Capture and clear BCH T4 errors.
				SiiRegWrite(RX_A__ECC_CTRL, RX_M__ECC_CTRL__CAPTURE_CNT);


				// repeat HPD cycle if HDCP is not recovered
				// in some time
				rx_isr.hdcp_fail_cntr = HDCP_FAIL_THRESHOLD_1ST - HDCP_FAIL_THRESHOLD_CONTINUED;
			}
		}
	}
	else
	{
		switch_hdcp_failure_check_with_v_sync_rate(ON);
		DEBUG_PRINT(MSG_STAT, ( "RX: 1st BCH Error ***\n"));
	}
}

/*****************************************************************************/
/**
 *  @brief		ISR Interrupt Handler
 *
 *  @param[in]
 *
 *  @return	ISR interrupt service time
 *  @retval
 *
 *****************************************************************************/
extern void sii5293_output_mode_trigger(unsigned int flag);
static uint8_t signal_status = 0;

void sii_signal_notify(unsigned int status)
{
	printk("sii9293, [%s] status = %d, signal_status = %d\n", __FUNCTION__, status, signal_status);
    // signal detection for vdin utility.
    if( (status==1) && (signal_status==0) )
    {
    	signal_status = status;
        sii5293_output_mode_trigger(1);
    }
    else if( (status==0) && (signal_status==1) )
    {
    	signal_status = status;
    	sii5293_output_mode_trigger(0);
    }

    return ;
}

extern void sii9293_cable_status_notify(unsigned int cable_status);
void SiiRxInterruptHandler(void)
{
    uint8_t interrupts[NMB_OF_RX_INTERRUPTS];
   
    //DEBUG_PRINT(MSG_STAT, ("RX Interrupt detected!\n"));
    // get interrupt requests
    SiiRegReadBlock(RX_A__INTR1, &interrupts[INT1], 4);
    SiiRegReadBlock(RX_A__INTR5, &interrupts[INT5], 2);
    SiiRegReadBlock(RX_A__INTR7, &interrupts[INT7], 2);

    // do not touch interrupts which are masked out
    interrupts[INT1] &= rx_isr.shadow_interrupt_mask[INT1];
    interrupts[INT2] &= rx_isr.shadow_interrupt_mask[INT2];
    interrupts[INT3] &= rx_isr.shadow_interrupt_mask[INT3];
    interrupts[INT4] &= rx_isr.shadow_interrupt_mask[INT4];
    interrupts[INT5] &= rx_isr.shadow_interrupt_mask[INT5];
    interrupts[INT6] &= rx_isr.shadow_interrupt_mask[INT6];
    interrupts[INT7] &= rx_isr.shadow_interrupt_mask[INT7];
    interrupts[INT8] &= rx_isr.shadow_interrupt_mask[INT8];

    // Cable plug-in / plug-out interrupts are handled elsewhere
    //interrupts[INT6] &= ~RX_M__INTR6__CABLE_UNPLUG;
    //interrupts[INT8] &= ~RX_M__INTR8__CABLE_IN;

    // clear interrupt requests
    SiiRegWriteBlock(RX_A__INTR1, &interrupts[INT1], 4);
    SiiRegWriteBlock(RX_A__INTR5, &interrupts[INT5], 2);
    SiiRegWriteBlock(RX_A__INTR7, &interrupts[INT7], 2);

    if(interrupts[INT1] & RX_M__INTR1__AUTH_DONE)
    {
        DEBUG_PRINT(MSG_STAT, ("RX: Authentication done!\n"));
        switch_hdcp_failure_check_with_v_sync_rate(OFF);
    }

    if(interrupts[INT2] & RX_M__INTR2__VID_CLK_CHANGED)
    {
        rx_isr.bVidStableChgEvent = true;
        DEBUG_PRINT(MSG_STAT, ("RX: video clock change\n"));
    }

    if(interrupts[INT2] & RX_M__INTR2__SCDT)
    {
        switch_hdcp_failure_check_with_v_sync_rate(OFF);
        SiiDrvRxMuteVideo(ON);
        rx_isr.bVidStableChgEvent = true;

        if(SiiDrvRxIsSyncDetected())
        {
        	// SCDT detection for vdin utility.
        	printk("sii9293 irq got SCDT!\n");

            rx_isr.bScdtState = true;
#if defined(__KERNEL__)
            SiiConnectionStateNotify(true);
#endif  
        }
        else
        {
        	// SCDT detection for vdin utility.
			printk("sii9293 irq lost SCDT!\n");
			sii_signal_notify(0);

            rx_isr.bScdtState = false;
            SiiDrvSoftwareReset(RX_M__SRST__SRST);
            VMD_ResetTimingData();
            DEBUG_PRINT(MSG_STAT, ("RX: IDLE!\n"));


        }

    }

    if(interrupts[INT2] & RX_M__INTR2__HDMI_MODE)
    {
        DEBUG_PRINT(MSG_STAT, ("RX: HDMI mode change!\n"));
        RxIsr_HdmiDviTransition();
    }

    if(interrupts[INT2] & RX_M__INTR2__VSYNC)
    {
        hdcp_error_handler(true);
    }

    if(interrupts[INT4] & RX_M__INTR4__HDCP)
    {
        hdcp_error_handler(false);
    }

    if((interrupts[INT5] & RX_M__INTR5__AUDIO_FS_CHANGED) || (interrupts[INT6] & RX_M__INTR6__CHST_READY))
    {
        // Note: RX_M__INTR6__CHST_READY interrupt may be disabled
        //DEBUG_PRINT(MSG_STAT, ("RX: New Audio Fs\n"));
        RxAudio_OnChannelStatusChange();
    }
    if(interrupts[INT4] & RX_M__INTR4__NO_AVI)
    {
        RxInfo_NoAviHandler();
        rx_isr.bVidStableChgEvent = true;
    }

    if(interrupts[INT3] & RX_M__INTR3__NEW_AVI_PACKET)
    {
        RxInfo_InterruptHandler(INFO_AVI);
        rx_isr.bVidStableChgEvent = true;
    }

    if(interrupts[INT7] & RX_M__INTR7__NO_VSI_PACKET)
    {
        // Clear also vsif_received flag (indicating any VSIF packet detection).
        // If there is any other VSIF packet, the flag will be set again shortly.
        RxInfo_NoVsiHandler();
    }

    if(interrupts[INT7] & RX_M__INTR7__NEW_VSI_PACKET)
    {
        RxInfo_InterruptHandler(INFO_VSI);
    }

    if(interrupts[INT3] & RX_M__INTR3__NEW_AUD_PACKET)
    {
        RxInfo_InterruptHandler(INFO_AUD);
    }

    if(interrupts[INT6] & RX_M__INTR6__NEW_ACP_PACKET)
    {
        RxInfo_InterruptHandler(INFO_AUD);
    }
    if (interrupts[INT6] & RX_M__INTR6__CABLE_UNPLUG)
    {
        if (SiiRegRead(RX_A__INTR6) & RX_M__INTR6__CABLE_UNPLUG)
        {
            rx_isr.bCableChgEvent = true;
            rx_isr.bCableState = false;
            rx_isr.shadow_interrupt_mask[INT6] &= ~RX_M__INTR6__CABLE_UNPLUG;       // Disable 5v plug-out interrup
            rx_isr.shadow_interrupt_mask[INT8] |= RX_M__INTR8__CABLE_IN;            // Enable 5v plug-in interrupt
            SiiRegWrite(RX_A__INTR6_MASK, rx_isr.shadow_interrupt_mask[INT6]);
            SiiRegWrite(RX_A__INTR8_MASK, rx_isr.shadow_interrupt_mask[INT8]);
            sii9293_cable_status_notify(0);
            
        }
            
    }
    if (interrupts[INT8] & RX_M__INTR8__CABLE_IN)
    {
        if (SiiRegRead(RX_A__INTR8) & RX_M__INTR8__CABLE_IN)
        {
            rx_isr.bCableChgEvent = true;
            rx_isr.bCableState = true;
            rx_isr.shadow_interrupt_mask[INT6] |= RX_M__INTR6__CABLE_UNPLUG;        // Enable 5v plug-out interrup
            rx_isr.shadow_interrupt_mask[INT8] &= ~RX_M__INTR8__CABLE_IN;           // Disable 5v plug-in interrupt
            SiiRegWrite(RX_A__INTR6_MASK, rx_isr.shadow_interrupt_mask[INT6]);
            SiiRegWrite(RX_A__INTR8_MASK, rx_isr.shadow_interrupt_mask[INT8]);
			sii9293_cable_status_notify(1);
        }
    }
}

bool_t SiiDrvCableStatusGet ( bool_t *pData )
{
    if (rx_isr.bCableChgEvent)
    {
        rx_isr.bCableChgEvent = false;
        *pData = rx_isr.bCableState;
        return true;
    }
    return false;
}

bool_t SiiDrvVidStableGet ( bool_t *pData )
{
    if (rx_isr.bVidStableChgEvent)
    {
        rx_isr.bVidStableChgEvent = false;
        *pData = rx_isr.bScdtState;
        return true;
    }
    return false;
}

