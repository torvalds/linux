/***********************************************************************************/
/*  Copyright (c) 2002-2006, Silicon Image, Inc.  All rights reserved.             */
/*  No part of this work may be reproduced, modified, distributed, transmitted,    */
/*  transcribed, or translated into any language or computer format, in any form   */
/*  or by any means without written permission of: Silicon Image, Inc.,            */
/*  1060 East Arques Avenue, Sunnyvale, California 94085                           */
/***********************************************************************************/
#include <local_types.h>
#include <config.h>
#include <registers.h>
#include <hal.h>
#include <amf.h>
#include <stdio.h>

#ifndef _CEC_H_
#define _CEC_H_
typedef enum
{

    SiI_CEC_API_Version = 0x00,
    SiI_CEC_API_Revision = 0x00,
    SiI_CEC_API_Build = 0x01

} eSiI_CEC_VerInfo_t;

#define ON 1    // Turn ON
#define OFF 0   // Turn OFF
#define SII_MAX_CMD_SIZE 16 // defnes number of operands

typedef enum
{
    SiI_CEC_LogAddr_TV          = 0x00,
    SiI_CEC_LogAddr_RecDev1     = 0x01,
    SiI_CEC_LogAddr_RecDev2     = 0x02,
    SiI_CEC_LogAddr_STB1        = 0x03,
    SiI_CEC_LogAddr_DVD1        = 0x04,
    SiI_CEC_LogAddr_AudSys      = 0x05,
    SiI_CEC_LogAddr_STB2        = 0x06,
    SiI_CEC_LogAddr_STB3        = 0x07,
    SiI_CEC_LogAddr_DVD2        = 0x08,
    SiI_CEC_LogAddr_RecDev3     = 0x09,
    SiI_CEC_LogAddr_Res1        = 0x0A,
    SiI_CEC_LogAddr_Res2        = 0x0B,
    SiI_CEC_LogAddr_Res3        = 0x0C,
    SiI_CEC_LogAddr_Res4        = 0x0D,
    SiI_CEC_LogAddr_FreeUse     = 0x0E,
    CEC_LOGADDR_UNREGORBC_MSG   = 0x0F

} ecSiI_CEC_LogAddr_t;

#define CEC_PA_EDID_CH0      0x1000
#define CEC_PA_EDID_CH1      0x2000
#define CEC_PA_EDID_CH2      0x3000
#define CEC_PA_EDID_CH3      0x4000


typedef enum {

    eSiI_UseOldDest = 0xFF

} eSiI_TXCmdMod_t;


typedef struct
{
    uint8_t bCount;
    uint8_t bRXNextCount;
    uint8_t bDestOrRXHeader;
    uint8_t bOpcode;
    uint8_t bOperand[ SII_MAX_CMD_SIZE ];

} SiI_CEC_t;

typedef enum {
    eSiI_CEC_ShortPulse = 0x80,
    eSiI_CEC_StartIrregular = 0x40,
    eSiI_CEC_RXOverFlow = 0x20,
} eSiI_CECErrors_t;

typedef enum {
    eSiI_TXWaitCmd,
    eSiI_TXSending,
    eSiI_TXSendAcked,
    eSiI_TXFailedToSend
} eSiI_TXState;

typedef struct {

    uint8_t bRXState;
    uint8_t bTXState;
    uint8_t bCECErrors;

} SiI_CEC_Int_t;

//uint8_t SiI_9185_CEC_CAPTURE_ID_Set( uint8_t );
uint8_t CEC_CAPTURE_ID_Set( uint8_t );
void SiI_CEC_SendPing ( uint8_t );
uint8_t SiI_CEC_SetCommand( SiI_CEC_t * );
uint8_t SiI_CEC_GetCommand( SiI_CEC_t * );
//uint8_t SiI_CEC_IntProcessing ( SiI_CEC_Int_t * );
uint8_t CEC_IntProcessing ( SiI_CEC_Int_t * );

void CEC_Init( void );


// debug functions

uint8_t SiI_CEC_SetSnoop ( uint8_t , bool_t );


// CEC_A6_TX_MSG_SENT_EVENT

#define CEC_A6_TX_MSG_SENT_EVENT 0x20

// CEC_A7_TX_RETRY_EXCEEDED_EVENT

#define CEC_A7_TX_RETRY_EXCEEDED_EVENT 0x02
extern uint8_t CECCalibration ( void ) ;


//=============================================================================
// C E C   M e s s a g e   I n f o r m a t i o n   L o o k  u p  T a b l e
//=============================================================================
/* Request Opcode */
#define RP (8)
/* Reply Opcode   */
#define RQ (4)
/* Direct Addressing Mode    */
#define DA (2)
/* Broadcast Addressing Mode */
#define BC (1)

typedef enum
{
  CEC_REQST_DA_E = (RQ | DA),
  CEC_REQST_BC_E = (RQ | BC),
  CEC_REQST_BOTH_DA_BC = (RQ | BC | DA),
  CEC_REPLY_DA_E = (RP|DA),
  CEC_REPLY_BC_E = (RP|DA),
  CEC_REPLY_BOTH_DA_BC = (RP|DA|BC),
  CEC_BOTH_REQ_DA_REP_DA = (RP|RQ|DA),
  CEC_BOTH_REQ_BC_REP_BC = (RP|RQ|BC),
  CEC_BOTH_REQ_REP_BOTH_DA_BC =(RP|RQ|DA|BC)
} CEC_RX_MSG_TYPE_ET;

typedef void (*CEC_MSG_HANDLER)( SiI_CEC_t * SiI_CEC );

typedef struct
{
  uint8_t                  opcode ;
  uint8_t                  num_operand ;
  CEC_RX_MSG_TYPE_ET  msg_type ;
  CEC_MSG_HANDLER     opcode_handler ;
} LUT_CEC_MSG_INFO;

typedef enum
{
  STATE_ON      =0x00,
  STATE_STBY    =0x01,
  STATE_STBY2ON =0x02,
  STATE_ON2STBY =0x03,
} POWER_STATUS_ET;

//
//  Used to collect events that will be executed by the Event Handler.
//  Used in an array for deeper buffering.
//
typedef struct
{
  uint8_t  cec_event_id ;
  uint8_t  param[5] ;
  SiI_CEC_t * SiI_CEC ;
} EVENT_DESCRIPTOR;

typedef struct
{
  uint8_t                  cec_event_id ;
  CEC_MSG_HANDLER     cec_event_handler ;
} LUT_CEC_EVENT_HANDLER;

// ====================================================
//
// !!!!!!!Order is very important!!!!!! Don't Touch !!!
// !!Must be kept consistent with CEC_RX_REPLY_LUT[] !!
//
// =====================================================
typedef enum
{
  CEC_EVT_ONE_TOUCH_PLAY = 0x00,
  CEC_EVT_POWER_ON,
  CEC_EVT_POWER_OFF,
  //CEC_EVT_IR_REMOTE_KEY_PRESSED,
  //CEC_EVT_HDMI_PLUG_DETECT,
  //CEC_EVT_MSG_RECEIVED,
  //CEC_EVT_MSG_WAITING_TO_BE_SENT,
  CEC_EVT_LAST_EVENT,

} CEC_EVENT_HANDLER_ID_ET;

typedef enum
{
  DSCRPTR_EMPTY,  // Descriptor is available for use.
  DSCRPTR_BUSY,   // Descriptor is taken and is being filled/updated.
  DSCRPTR_FULL,   // Descriptor is taken and is full/ready for use.
} DESCRIPTOR_STATUS;

//
//  CEC Event Descriptor
//  is used for a task to communicate to the System Event Handler
//  to look out for a specific CEC Opcode from a specific
//  source Logical Address of the next frame received.
//  Once there is a match, the Event Handler specified i.e. cec_event_hdlr_id,
//  is call with the cec_event_next_state as a parameter.
//  cec_event_next_state tells where the
//
//
typedef struct
{
  DESCRIPTOR_STATUS       dscrptr_status;     // Used to provide the status of the descriptor.
  CEC_EVENT_HANDLER_ID_ET event_hdlr_id ;     // EVENT ID, must be registered in CEC_EVENT_HANDLER_ID_ET.
  uint8_t                      event_next_state;   // Next State of handler
  uint8_t                      trgt_opcode ;       // Targeted Opcode
  uint8_t                      trgt_source_addr ;  // Targeted Source Logical Address
} CEC_EVENT_DESCRIPTOR;

//void SiI_918x_Event_Handler( void );
void CEC_Event_Handler( void );

void CEC_event_descriptor_clear(void) ;

extern uint8_t bCECSlvAddr;

//
// CEC Software Connection #3: Target System to provide local I2C Read function.
//                        I.E. Replace hlReaduint8_t_8BA() with Target System's with own function.
//
#define SiI_918x_RegRead(DEVICE, ADDR) hlReaduint8_t_8BA( DEVICE, ADDR )

//
// CEC Software Connection #4: Target System to provide local I2C Write function.
//                        I.E. Replace hlWriteuint8_t_8BA() with Target System with own function.
//
#define SiI_918x_RegWrite(DEVICE, ADDR, DATA) hlWriteuint8_t_8BA( DEVICE, ADDR, DATA )


//
// CEC Software Connection #6: Target System to provide local I2C Read function.
//                        I.E. Replace SiI_918x_RegReadModWrite() with Target System's with own Read-Modify-Write function.
//
#define SiI_918x_RegReadModWrite( DEVICE, ADDR, MASK, DATA ) siiReadModWriteuint8_t ( DEVICE, ADDR, MASK, DATA )


// CEC Globals

typedef enum {

    SII_CEC_TV,
    SII_CEC_DVD,
    SII_CEC_STB,
    SII_CEC_SW

} CECDev_t;

typedef enum {

    SiI_EnumTV,
    SiI_EnumDVD

} SiI_EnumType;

typedef enum {

    SiI_CEC_Idle,
    SiI_CEC_ReqPing,
    SiI_CEC_ReqCmd1,
    SiI_CEC_ReqCmd2,
    SiI_CEC_ReqCmd3,
    SiI_CEC_Enumiration

} SiI_CECOp_t;

extern uint8_t bCECTask;
extern uint8_t InitiatorAddress;
extern uint8_t bFollowerAddress;
extern uint8_t bTXState;
extern uint8_t bCECDev;
extern uint8_t bEnumType;

#define UCEC_ACTIVE     0x82
#define UCEC_STANDBY    0x36

#endif // _SII_CEC_H_



