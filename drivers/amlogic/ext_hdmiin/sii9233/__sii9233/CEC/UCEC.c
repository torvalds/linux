//------------------------------------------------------------------------------
// Copyright © 2002-2005, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1060 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------

// Turn down the warning level for this C file to prevent the compiler 
// from complaining about the unused parameters in the stub functions below
#pragma WARNINGLEVEL (1)

#include <local_types.h>
#include <config.h>
#include <registers.h>
#include <hal.h>
#include <amf.h>
#include <CEC.h>

#if(CONF__CEC_ENABLE == ENABLE)


//  Enable rejection of self-broadcast messages.
//
#define CEC_RX_OWN_BRCST_MSG_FIX
//
//  Enable CEC port switching
//
#define SI_SUPPORT_CEC_AUTO_PORT_SWITCHING

static void CEC_EVT_HDLR_one_touch_play( SiI_CEC_t * sii_cec )
{
    //printf("\n CEC_EVT_HDLR_one_touch_play") ;
}

static void CEC_EVT_HDLR_power_on( SiI_CEC_t * sii_cec )
{
    //printf("\n CEC_EVT_HDLR_power_on") ;
}

static void CEC_EVT_HDLR_power_off( SiI_CEC_t * sii_cec )
{
    //printf("\n CEC_EVT_HDLR_power_off") ;
}

ROM const LUT_CEC_EVENT_HANDLER CEC_RX_REPLY_LUT[] =
{
  /*
   ========================= ==========================
      Task-Event               ID Task-Event Handler
   ========================= ==========================   */
  { CEC_EVT_ONE_TOUCH_PLAY,   CEC_EVT_HDLR_one_touch_play },
  { CEC_EVT_POWER_ON,         CEC_EVT_HDLR_power_on       },
  { CEC_EVT_POWER_OFF,        CEC_EVT_HDLR_power_off      }
};

static POWER_STATUS_ET TV_state = STATE_STBY ;
//
// cec_event_dscrptr sure be guarded using a semaphor or by disabling
// interrupt prior to changing it's content and enabling interrupts
//
//
static CEC_EVENT_DESCRIPTOR cec_event_dscrptr ;


ROM const uint8_t abEnumTblTV[3]  = { 2, SiI_CEC_LogAddr_TV,   SiI_CEC_LogAddr_FreeUse };
ROM const uint8_t abEnumTblDVD[4] = { 3, SiI_CEC_LogAddr_DVD1, SiI_CEC_LogAddr_DVD2, SiI_CEC_LogAddr_FreeUse };
//------------------------------------------------------------------------------
// Function Name:
// Function Description:
//------------------------------------------------------------------------------
void PrintLogAddr ( uint8_t bLogAddr ){

    if ( bLogAddr <= CEC_LOGADDR_UNREGORBC_MSG )
    {
        printf (" [%X] ", (int) bLogAddr );
        switch ( bLogAddr )
        {
            case SiI_CEC_LogAddr_TV:        printf("TV");           break;
            case SiI_CEC_LogAddr_RecDev1:   printf("RecDev1");      break;
            case SiI_CEC_LogAddr_RecDev2:   printf("RecDev2");      break;
            case SiI_CEC_LogAddr_STB1:      printf("STB1");         break;
            case SiI_CEC_LogAddr_DVD1:      printf("DVD1");         break;
            case SiI_CEC_LogAddr_AudSys:    printf("AudSys");       break;
            case SiI_CEC_LogAddr_STB2:      printf("STB2");         break;
            case SiI_CEC_LogAddr_STB3:      printf("STB3");         break;
            case SiI_CEC_LogAddr_DVD2:      printf("DVD2");         break;
            case SiI_CEC_LogAddr_RecDev3:   printf("RecDev3");      break;
            case SiI_CEC_LogAddr_Res1:      printf("Res1");         break;
            case SiI_CEC_LogAddr_Res2:      printf("Res2");         break;
            case SiI_CEC_LogAddr_Res3:      printf("Res3");         break;
            case SiI_CEC_LogAddr_Res4:      printf("Res4");         break;
            case SiI_CEC_LogAddr_FreeUse:   printf("FreeUse");      break;
            case CEC_LOGADDR_UNREGORBC_MSG: printf("UnregOrBC_MSG");    break;
        }
    }
}
//------------------------------------------------------------------------------
// Function Name: PrintCommand
// Function Description:
//------------------------------------------------------------------------------
static void PrintCommand( SiI_CEC_t * SiI_CEC )
{
    uint8_t i;
    printf ("\n [FROM][TO][OPCODE]{OPERANDS}: ");
    PrintLogAddr( (SiI_CEC->bDestOrRXHeader & 0xF0) >> 4 );
    PrintLogAddr( SiI_CEC->bDestOrRXHeader & 0x0F );
    printf (" [%02X]", (int) SiI_CEC->bOpcode);
    if ( SiI_CEC->bCount & 0x0F)
    {
        //printf("\n Operands: ");
        for ( i = 0; i < ( SiI_CEC->bCount & 0x0F); i++ )
            printf (" {%02X}", (int) SiI_CEC->bOperand[i]);
    }
    printf ("\n");
}

//
//  Sets the
//
//
#ifdef NOT_USED_NOW
uint8_t CEC_event_descriptor_set( CEC_EVENT_DESCRIPTOR *p_new_event )
{
  //
  // System Implementation Note:
  // recommend semaphor be used to guard event descriptor.
  //
  cec_event_dscrptr.dscrptr_status    = DSCRPTR_BUSY ;
  cec_event_dscrptr.event_hdlr_id     = p_new_event->event_hdlr_id ;
  cec_event_dscrptr.event_next_state  = p_new_event->event_next_state ;
  cec_event_dscrptr.trgt_opcode       = p_new_event->trgt_opcode ;
  cec_event_dscrptr.trgt_source_addr  = p_new_event->trgt_source_addr ;
  cec_event_dscrptr.dscrptr_status    = DSCRPTR_FULL ;
  return 0 ;

}//e.o. uint8_t CEC_event_descriptor_set( CEC_EVENT_DESCRIPTOR *p_cec_evt_dscrptr )
#endif

//
// Clear the Event Descriptor regardless
//
void CEC_event_descriptor_clear()
{
  //System Implementation Note:
  //recommend semaphor be used to guard event descriptor when being updated.
  //
  cec_event_dscrptr.dscrptr_status    = DSCRPTR_EMPTY ;

}//e.o. uint8_t CEC_event_descriptor_set( CEC_EVENT_DESCRIPTOR *p_cec_evt_dscrptr )

//
// HDMI CEC Auto Port Switching Related Code
//
static void cecAutoPortSwitch( SiI_CEC_t * SiI_CEC )
{
    static uint8_t currentActiveSource=0, old_ActiveSource=0;
    static uint8_t currentActiveLA=0, old_ActiveLA=0 ; // Logical Address (LA)
    uint16_t physicalAddr ;

    if( SiI_CEC->bOpcode == 0x82 )
    {
        // Save the old Active device info
        old_ActiveSource = currentActiveSource ;
        old_ActiveLA     = currentActiveLA     ;
        currentActiveLA = (SiI_CEC->bDestOrRXHeader & 0xF0) >> 4 ;

        physicalAddr = ((uint16_t)SiI_CEC->bOperand[0] << 8 ) | SiI_CEC->bOperand[1] ;
        // Check initiator physical address
        switch( physicalAddr )
        {
            case CEC_PA_EDID_CH0:
                //printf("\n  Active Source => HDMI Port0\n");
                // Switch to HDMI Port0
                // update current active source
                currentActiveSource = PORT_SELECT__PORT_0 ;
                break;

            case CEC_PA_EDID_CH1:
                //printf("\n  Active Source => HDMI Port1\n");
                // Switch to HDMI Port1
                currentActiveSource = PORT_SELECT__PORT_1 ;
                break;

            case CEC_PA_EDID_CH2:
                //printf("\n  Active Source => HDMI Port2\n");
                // Switch to HDMI Port2
                currentActiveSource = PORT_SELECT__PORT_2 ;
                break;

            case CEC_PA_EDID_CH3:
                //printf("\n  Active Source => HDMI Port3\n");
                // Switch to HDMI Port2
                currentActiveSource = PORT_SELECT__PORT_3 ;
                break;


            default:
                //printf("\n Unregistered Active Source whose PhysicalAddress=0x%X", physicalAddr) ;
                currentActiveSource = PORT_SELECT__PORT_0 ;
                //printf("\n  Active Source => HDMI Port2\n");
        }
        //
        // Actuate the port switching to selected current active port.
        //
        if( currentActiveSource != old_ActiveSource )
        {
            SiI_CEC_t SiI_CEC;
            // stop play at the current active logical address device
            #define DECK_CONTROL 0x42              // Operand is Deck Control Mode
            SiI_CEC.bOpcode     = DECK_CONTROL ;
            SiI_CEC.bOperand[0] = 0x3 ;            // Stop
            SiI_CEC.bCount      = 1;               //number of Operands
            SiI_CEC.bDestOrRXHeader = 0x0 | ( old_ActiveLA&0xF ) ; //[initiator=0x00][follower=0x2]
            SiI_CEC_SetCommand( &SiI_CEC );
            //printf ("\n Sending DECK_CONTROL opcode \n");
        }


	 if(CurrentStatus.PortSelection !=  currentActiveSource)
     {
		 CurrentStatus.PortSelection =  currentActiveSource;
	     SystemDataReset();
	 }
	}
}

static void CEC_HDLR_ff_Abort( SiI_CEC_t * sii_cec )
{
    SiI_CEC_t cec_frame;
    //printf("\n <abort> rcvd; send out feature abort");
    //
    // Unsupported opcode; send f e a t u r e   a b o r t
    //
    cec_frame.bOpcode         = 0x00;
    cec_frame.bDestOrRXHeader = (sii_cec->bDestOrRXHeader & 0xf0) >> 4 ;
    cec_frame.bOperand[0]     = 0xff;
    cec_frame.bOperand[1]     = 0;
    cec_frame.bCount          = 2;
    SiI_CEC_SetCommand( &cec_frame );
}

static void CEC_HDLR_00_FeatureAbort( SiI_CEC_t * sii_cec )
{
    //printf("\n CEC_HDLR_00_FeatureAbort") ;
}

static void CEC_HDLR_8f_giveDevicePowerStatus( SiI_CEC_t * sii_cec )
{
    // send reply indicating that power is on
    // printf("\n CEC_HDLR_8f_giveDevicePowerStatus") ;

}

static void CEC_HDLR_90_reportPowerStatus( SiI_CEC_t * sii_cec )
{
    // Use power status info in FeatureFSM
    //printf("\n CEC_HDLR_90_reportPowerStatus") ;

}

static void CEC_HDLR_82_ActiveSource( SiI_CEC_t * sii_cec )
{
    //printf("\n CEC_HDLR_82_ActiveSource ");
    #ifdef SI_SUPPORT_CEC_AUTO_PORT_SWITCHING
    cecAutoPortSwitch( sii_cec ) ;
    #endif // SI_SUPPORT_CEC_AUTO_PORT_SWITCHING
}

static void CEC_HDLR_04_ImageViewOn( SiI_CEC_t * sii_cec )
{
    //printf("\n CEC_HDLR_04_ImageViewOn ");
    TV_state = STATE_ON ;
}

static void CEC_HDLR_0d_TextViewOn( SiI_CEC_t * sii_cec )
{
    //printf("\n CEC_HDLR_0d_TextViewOn ");
    TV_state = STATE_ON ;
}

static void CEC_HDLR_85_RequestActiveSource( SiI_CEC_t * sii_cec )
{
    //printf("\n CEC_HDLR_85_RequestActiveSource");
}

static void CEC_HDLR_36_Standby( SiI_CEC_t * sii_cec )
{
    //printf("\n CEC_HDLR_36_Standby");
    TV_state = STATE_STBY ;
}

static void CEC_HDLR_83_GivePhysicalAddr( SiI_CEC_t * sii_cec )
{
    SiI_CEC_t cec_frame;
    //printf("\n CEC_HDLR_83_GivePhysicalAddr");
    //
    // Transmit Physical Address: 0.0.0.0
    //
    cec_frame.bOpcode         = 0x84;
    cec_frame.bDestOrRXHeader = 0x0F ;
    cec_frame.bOperand[0]     = 0x00 ; // [Physical Address]
    cec_frame.bOperand[1]     = 0x00 ; // [Physical Address]
    cec_frame.bOperand[2]     = 0x00 ; // [Device Type] = 0 = TV
    cec_frame.bCount          = 3 ;
    SiI_CEC_SetCommand( &cec_frame ) ;
}

static void CEC_HDLR_91_GetMenuLanguage( SiI_CEC_t * SiI_CEC )
{
    SiI_CEC_t cec_frame;
    //printf("\n 91op");

    // Send Set Menu Language
    cec_frame.bOpcode         = 0x32;
    cec_frame.bDestOrRXHeader = 0x0F ;
    //
    // Target System Specific
    // - Set own language own according to specific system.
    //
    cec_frame.bOperand[0]     = 0x01 ; // [language code see iso/fdis 639-2]
    cec_frame.bOperand[1]     = 0x02 ; // [language code see iso/fdis 639-2]
    cec_frame.bOperand[2]     = 0x03 ; // [language code see iso/fdis 639-2]
    cec_frame.bCount          = 3 ;
    SiI_CEC_SetCommand( &cec_frame ) ;
}

static void CEC_HDLR_32_SetMenuLanguage( SiI_CEC_t * SiI_CEC )
{
  // Message valid only if it's sent by a TV.
  // Therefore don't not need to process as a TV even though
  // it's TV is also a switch since it has two or more hdmi ports.
  //
  //printf("\n CEC_HDLR_32_SetMenuLanguage");
}

static void CEC_HDLR_84_ReportPhysicalAddr( SiI_CEC_t * SiI_CEC )
{
  // REPLY to Get Physical Address 0x83
  // Process mostly likely thru CEC Task Handler (CTH).
  //printf("\n CEC_HDLR_84_ReportPhysicalAddr");
}

static void CEC_HDLR_80_RoutingChange( SiI_CEC_t * SiI_CEC )
{
  //printf("\n 80");
}

static void CEC_HDLR_86_SetStreamPath( SiI_CEC_t * SiI_CEC )
{
  //printf("\n 86");
}

static void CEC_HDLR_81_RoutingInformation( SiI_CEC_t * SiI_CEC )
{
  //printf("\n 81");
}

ROM const LUT_CEC_MSG_INFO CEC_RX_MSG_LUT[] =
{
  /*
   ====== ========  ===============         ==========================
   opcode #operand   MessageType            Message/Opcode Handler
   ====== ========  ===============         ==========================   */
  {0x8f,    0,      CEC_REQST_DA_E,         CEC_HDLR_8f_giveDevicePowerStatus   }, // <Give Device Power Status>
  {0x90,    1,      CEC_REPLY_DA_E,         CEC_HDLR_90_reportPowerStatus   }, // <Report Power Status>[Power Status]

  {0xFF,    0,      CEC_REQST_DA_E,         CEC_HDLR_ff_Abort                   }, // <Abort>
  {0x00,    2,      CEC_REPLY_DA_E,         CEC_HDLR_00_FeatureAbort            }, // <Feature Abort>[Feature Opcode][Abort Reason]

  {0x85,    0,      CEC_REQST_BC_E,         CEC_HDLR_85_RequestActiveSource     }, // <Request Active Source>
  {0x82,    1,      CEC_BOTH_REQ_BC_REP_BC, CEC_HDLR_82_ActiveSource            }, // <Active Source>[Physical Address]

  {0x04,    0,      CEC_REQST_DA_E,         CEC_HDLR_04_ImageViewOn             }, // <Image View On>
  {0x0d,    0,      CEC_REQST_DA_E,         CEC_HDLR_0d_TextViewOn              }, // <Text View On>

  {0x36,    0,      CEC_REQST_BOTH_DA_BC,   CEC_HDLR_36_Standby                 }, // <Standby>

  {0x91,    0,      CEC_REQST_DA_E,         CEC_HDLR_91_GetMenuLanguage         }, // <Get Menu Language>
  {0x32,    1,      CEC_BOTH_REQ_BC_REP_BC, CEC_HDLR_32_SetMenuLanguage         }, // <Get Menu Language>

  {0x80,    4,      CEC_REQST_BC_E,         CEC_HDLR_80_RoutingChange           }, // <Routing Change>
  {0x86,    2,      CEC_REQST_BC_E,         CEC_HDLR_86_SetStreamPath           }, // <Set Stream Path>
  {0x81,    2,      CEC_REQST_BC_E,         CEC_HDLR_81_RoutingInformation      }, // <Routing Information>

  {0x83,    0,      CEC_REQST_DA_E,         CEC_HDLR_83_GivePhysicalAddr        }, // <Give Physical Address  >
  {0x84,    3,      CEC_BOTH_REQ_BC_REP_BC, CEC_HDLR_84_ReportPhysicalAddr      }  // <Report Physical Address>
};

//
// return 0 if msg is 0K
// return 1 if msg is 1nva1id
//
static uint8_t s6_cec_msg_validate( SiI_CEC_t * sii_cec )
{
    uint8_t i=0 ;
    SiI_CEC_t cec_frame;
    uint8_t num_of_msg_entries = sizeof(CEC_RX_MSG_LUT)/sizeof(LUT_CEC_MSG_INFO);
    //
    // 1. Check if msg is supported
    // 2. Check if msg meets operand requirements.  Min & Max[protocol extension].
    //
    for( i=0; i<num_of_msg_entries; i++ )
    {
        if( sii_cec->bOpcode == CEC_RX_MSG_LUT[i].opcode )
        {
            //2. check operand
            if( (sii_cec->bCount & 0x0F) >= CEC_RX_MSG_LUT[i].num_operand )
            {
              //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
              // Enforce CEC broadcast and direct addressing requirements:
              //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
              if( (( (sii_cec->bDestOrRXHeader & 0x0F) == 0x0F && CEC_RX_MSG_LUT[i].msg_type & (uint8_t)BC ) ||
                   ( (sii_cec->bDestOrRXHeader & 0x0F) != 0x0F && CEC_RX_MSG_LUT[i].msg_type & (uint8_t)DA )   ) &&
                  ( (sii_cec->bDestOrRXHeader & 0xF0) != 0xF0 ) // Filter invalid source address of 0xF //
                  #ifdef CEC_RX_OWN_BRCST_MSG_FIX
                  && (sii_cec->bDestOrRXHeader != 0x0F ) // Reject self-broadcast msgs;0x0F-Header-Broadcast Issue
                  #endif // CEC_RX_OWN_BRCST_MSG_FIX
                )
              {
                if( (sii_cec->bDestOrRXHeader & 0xF0) == 0xF0 )
                {
                  // Filter invalid source address of 0xF
                  // CEC 12.2 Protocol General Rules
                  // A follower shall ignore a message coming from address 15 (unregistered), unless:
                  //  • that message invokes a broadcast response (e.g. <Get Menu Language>), or,
                  //  • the message has been sent by a CEC Switch (a <Routing Change> or <Routing Information>
                  //    message), or,
                  //  • the message is <standby>.
                  if( sii_cec->bOpcode != 0x36 && sii_cec->bOpcode != 0x80 &&
                      sii_cec->bOpcode != 0x81 && sii_cec->bOpcode == 0x91 &&
                      sii_cec->bOpcode == 0x83 )
                      {
                        return 1;
                      }
                }

                //
                // !!!Merge Cec Task Handler (CTH) here!!!!
                //
                // <         Path One             >

                //
                // Got valid frame; call the assigned handler as described in the CEC Rx Message Look-up Table
                //
                // <         Path Two             >
                (*(CEC_RX_MSG_LUT[i].opcode_handler))( sii_cec ) ;
                return 0 ;
              }
              else
              {
                // Error; incorrect addressing mode; msg/frame ignored.
                printf("\n AddressingModeError.MsgIgnored[%02x]", (int)CEC_RX_MSG_LUT[i].msg_type ) ;
                return 0 ;
              }
            }
            else
            {
                printf("\n!Error!TooFewOperand!%X,%X,%X", CEC_RX_MSG_LUT[i].opcode, sii_cec->bCount, CEC_RX_MSG_LUT[i].num_operand ) ;
                return 1 ;
            }
        }

    }//e.o. for( i=0; i<num_of_msg_entries; i++ )

    //
    // Do not reply to Broadcast msgs, otherwise send Feature Abort
    // for all unsupported features.
    //
    if( (sii_cec->bDestOrRXHeader & 0x0F) != 0x0F
      #ifdef CEC_RX_OWN_BRCST_MSG_FIX
      && (sii_cec->bDestOrRXHeader != 0x0F ) // Workaround for 0x0F-Header-Broadcast Issue
      #endif // CEC_RX_OWN_BRCST_MSG_FIX
      )
    {
      //
      // Unsupported opcode; send f e a t u r e   a b o r t
      //
      cec_frame.bOpcode = 0x00;
      cec_frame.bDestOrRXHeader = (sii_cec->bDestOrRXHeader & 0xf0) >> 4 ;
      cec_frame.bOperand[0] = sii_cec->bOpcode;
      cec_frame.bOperand[1] = 0;
      cec_frame.bCount = 2;
      SiI_CEC_SetCommand( &cec_frame );
      printf("\n!UnsupportedFeature!");
    }
    else
    {
      //
      // Unsupported Broadcast Msg
      //
      printf("\n!UnsupportedBcMsg!");
    }

    return 1 ;

}//e.o. static void s6_cec_msg_validate( SiI_CEC_t * SiI_CEC )


//------------------------------------------------------------------------------
// Function Name: ParsingReceivedData
//
// Function Description:
// 1. CEC7.3 Frame Validation
// 2. Protocol Extension
// 3. CEC12.3 Feature Abort
// 4. Amber Alert i.e. call to function specified by the handler that is expecting
//    a reply from the specified device, LA, LogicalAddress.
//------------------------------------------------------------------------------
static void s6_ParsingRecevedData ( uint8_t bRXState )
{
    uint8_t bAuxData;
    uint8_t i;
    SiI_CEC_t SiI_CEC ;

    //
    // CEC_RX_COUNT Register:  RX_ERROR | CEC_RX_CMD_CNT | CEC_RX_uint8_t_CNT
    // See CPI document for details.
    //
    bAuxData = bRXState & 0xF0;
    if( bAuxData )
    {
        SiI_CEC.bCount = bRXState;
        bAuxData >>=4;
        printf ("\n %i frames in Rx Fifo ", (int) bAuxData );


        for ( i = 0; i < bAuxData; i++ )
        {
            SiI_CEC_GetCommand( &SiI_CEC );

            PrintCommand( &SiI_CEC );
            // Check BC msg && init==0x00
            // Validation Message
            s6_cec_msg_validate( &SiI_CEC );

            if ( SiI_CEC.bRXNextCount)
                SiI_CEC.bCount = SiI_CEC.bRXNextCount;
        }
    }
}
//------------------------------------------------------------------------------
// Function Name: GetEnumLTbl()
// Function Description:  This function gets lodical adress from enumiration tables
//------------------------------------------------------------------------------
uint8_t GetEnumTbl ( uint8_t i )
{
    uint8_t bEnumTbl;

    if( bEnumType == SiI_EnumTV )
    {
        bEnumTbl = abEnumTblTV[i];
    }
    else
    {
        bEnumTbl = abEnumTblDVD[i];
    }

    return bEnumTbl;
}

#endif //#if(CONF__CEC_ENABLE == ENABLE)
//------------------------------------------------------------------------------
// Function Name: userSWTtask
// Function Description:
// This is function handler event from the CEC RX buffer.
// CEC Software Connection #5.
//------------------------------------------------------------------------------
//void SiI_918x_Event_Handler( void )
void CEC_Event_Handler( void )
{
#if(CONF__CEC_ENABLE == ENABLE)
    static uint8_t i = 0;
    static uint8_t bNewTask = SiI_CEC_Idle;
    SiI_CEC_Int_t CEC_Int;
    SiI_CEC_t SiI_CEC ;

    if( bNewTask != bCECTask )
    {
        bNewTask = bCECTask;
        i = 0;
    }

    //
    // Process CEC Events; at this point doesn't do much
    //
//    SiI_CEC_IntProcessing( &CEC_Int );
        CEC_IntProcessing(&CEC_Int);

    //===============================
    // Begin: 8051 Platform Specific
    //===============================
    // Process CP981x Board Functions e.g. buttons, test functions
    //
    if( bCECTask == SiI_CEC_ReqPing )
    {
      //
      // Test function 1: Ping Button Pressed
      //
      if( bTXState == eSiI_TXWaitCmd )
      {
          printf("\n %X", (int)i );
          SiI_CEC_SendPing( i );

          bTXState = eSiI_TXSending;
      }
      else if(bTXState == eSiI_TXSending )
      {
          if( CEC_Int.bTXState == eSiI_TXFailedToSend )
          {
              printf (" NoAck ");
              PrintLogAddr(i);
              i++;
              bTXState = eSiI_TXWaitCmd;
          }
          if( CEC_Int.bTXState == eSiI_TXSendAcked ){
              printf (" Ack "); PrintLogAddr(i);
              i++;
              bTXState = eSiI_TXWaitCmd;
          }
          if( i >= CEC_LOGADDR_UNREGORBC_MSG ){
              bCECTask = SiI_CEC_Idle;
              i = 0;
          }
      }
    }
    else if( (bCECTask == SiI_CEC_ReqCmd2) || (bCECTask == SiI_CEC_ReqCmd3) )
    {
      //
      //  Test function 2: Send test message
      //
      if ( CEC_Int.bTXState == eSiI_TXFailedToSend )
      {
          printf (" NoAck ");
          bTXState = eSiI_TXWaitCmd;
          bCECTask = SiI_CEC_Idle;
      }
      if ( CEC_Int.bTXState == eSiI_TXSendAcked )
      {
          printf (" Ack ");
          bTXState = eSiI_TXWaitCmd;
          bCECTask = SiI_CEC_Idle;
      }
    }
    else if ( bCECTask == SiI_CEC_Enumiration )
    {
      //
      //  Test function 3:  Enumerate CEC device Logical Address
      //  For TV 1st choice is Logical Address 0, but if 0 is taken
      //  0x0E is tried.
      //  a. Ping logical address 0. If no ACK received then 0 is
      //     not taken and 0 is assigned to the device.
      //  b. If 0 is taken, 0x0E is tried.  see abEnumTblTV[] also.
      //  c. If 0 and 0x0E are taken, 0x0F is assigned.
      //
      if ( bTXState == eSiI_TXWaitCmd )
      {
          // a. Ping 0, 0x0E
          SiI_CEC_SendPing ( GetEnumTbl(i + 1) );
          printf ("\n ping %02X", (int) GetEnumTbl(i + 1));
          bTXState = eSiI_TXSending;
      }
      else if (bTXState == eSiI_TXSending )
      {

          if ( CEC_Int.bTXState == eSiI_TXFailedToSend )
          {
            //
            // b. No ACK received, i.e. address available: take it.
            //
            printf (" NoAck "); PrintLogAddr( GetEnumTbl(i + 1) );
            InitiatorAddress = GetEnumTbl(i + 1);
            //
            // Set Capture address
            //
            CEC_CAPTURE_ID_Set( InitiatorAddress );
            printf ("\n Intiator address is ");
            PrintLogAddr ( InitiatorAddress );
            //
            // Restore Tx State to IDLE i.e. eSiI_TXWaitCmd, wait for new Tx command.
            //
            bTXState = eSiI_TXWaitCmd;
            bCECTask = SiI_CEC_Idle;
          }
          else if ( CEC_Int.bTXState == eSiI_TXSendAcked )
          {
            //
            // c. ACK received i.e. address taken; try the next choice or use 0x0F, CEC_LOGADDR_UNREGORBC_MSG.
            //
            printf (" Ack "); PrintLogAddr( GetEnumTbl( i + 1) );
            if ( i >= GetEnumTbl( 0 ) )
            {
                i = 0;
                bCECTask = SiI_CEC_Idle;
                printf ("Assign log addr: UnregOrBC_MSG");
                InitiatorAddress = CEC_LOGADDR_UNREGORBC_MSG;
                CEC_CAPTURE_ID_Set( InitiatorAddress );
            }
            else
                i++;
            bTXState = eSiI_TXWaitCmd;
          }
      }
    }//e.o. else if ( bCECTask == SiI_CEC_Enumiration )
    //===============================
    // End: 8051 Platform Specific
    //===============================

    //
    // Check for incoming CEC frames in the Rx Fifo.
    //
    //
    if( CEC_Int.bRXState )
    {
        s6_ParsingRecevedData( CEC_Int.bRXState );
    }

#endif	//#if(CONF__CEC_ENABLE == ENABLE)
}//e.o. void SiI_918x_Event_Handler( void )


