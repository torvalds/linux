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
#include <CEC.h>  



#define CEC_TX_AUTO_CALC_ENABLED

//
// Target System Note:  Recalibration frequency is system dependent.
//
unsigned int cec_recal_timer = 0;

uint8_t bCECTask = 0;
uint8_t InitiatorAddress = 0x0F;
uint8_t bFollowerAddress = 0x0f;
uint8_t bTXState = eSiI_TXWaitCmd;
uint8_t bCECDev = SII_CEC_TV;
uint8_t bEnumType;




//------------------------------------------------------------------------------
// Function Name: SiI_918x_Start
// Function Description: Intilize Stream Switch Device
// CEC Software Connection #1
//------------------------------------------------------------------------------
//void SiI_918x_Start( void )
void CEC_Init( void )
{

//  uint8_t cec_phyAddr[2];
#if(CONF__CEC_ENABLE == ENABLE)


    DEBUG_PRINT(("\nStream switch start\n"));
  //CEC workaround for ES0
    GPIO_ClearCecD();
    RegisterModify(REG__CEC_CONFIG_CPI,BIT__CEC_PASS_THROUGH,CLEAR);
    if((RegisterRead(DEV_REV_RX)& 0x0F)==VAL__REV_1_2) //if rev1.2 CEC by default reset
		RegisterModify(REG__C0_SRST2, BIT__CEC_SRST, SET); //0 is reset, 1 is normal
    else
		RegisterBitToggle(REG__C0_SRST2,BIT__CEC_SRST);


    RegisterWrite(REG__SLAVE_ADDR_EDID,CONF__I2C_SLAVE_PAGE_9);
    RegisterWrite(REG__SLAVE_ADDR_CEC,CONF__I2C_SLAVE_PAGE_8);


      // Set CEC device type = TV and logical address to capture.
     InitiatorAddress = CEC_LOGADDR_UNREGORBC_MSG ;
     if( CEC_CAPTURE_ID_Set( InitiatorAddress ) )
          {
              DEBUG_PRINT(("\n Cannot init CEC"));
          }
	
	  //
	  // 4. Initialize Event Descriptor
	  //
	  CEC_event_descriptor_clear();
	
	  // Enumirate as an TV
	  bCECTask  = SiI_CEC_Enumiration;
	  bEnumType = SiI_EnumTV;

#endif  // #if(CONF__CEC_ENABLE == ENABLE)

      //====================
      // 5. Program EDID and set port specific address space.
      //    Program (a) EDID and (b) Physical address
      //====================

      ProgramEDID();

}



//------------------------------------------------------------------------------
// Function Name: SiI_CEC_SetSnoop
// Function Description: This mode is used to listen a specified CEC address
//                       Device doesn't acknowledge specified address
// Accepts: uint8_t bSnoopAddr bool_t qOn
// Returns: uint8_t (reports about I2C errors)
// Globals: none
//------------------------------------------------------------------------------
#ifdef NOT_USED_NOW
uint8_t SiI_CEC_SetSnoop ( uint8_t bSnoopAddr, bool_t qOn )
{
    uint8_t error = FALSE;

    if ( qOn )
    {
		RegisterModify(REG__CEC_DEBUG_3,BIT_SNOOP_EN, BIT_SNOOP_EN);  
        bSnoopAddr <<= 4;
    }
    else {
		RegisterModify(REG__CEC_DEBUG_3, BIT_SNOOP_EN, CLEAR);
        bSnoopAddr = 0;
    }
	RegisterWriteBlock(REG__CEC_DEBUG_2, &bSnoopAddr, 1);

    return error;

}
#endif

#if(CONF__CEC_ENABLE == ENABLE)
//------------------------------------------------------------------------------
// Function Name: SiI_9185_CEC_CAPTURE_ID_Set
// Function Description:
//
// Accepts: uint8_t bInitLA
// Returns: error code
// Globals: none

// Affected Register(s): CEC_CAPTURE_ID
// Document Reference  : CEC Promgramming Interface (CPI) Programmer's Reference
// Warning: Only a single CEC device can be select with this interface even though
//          the all 16 devices can be selected.
//------------------------------------------------------------------------------
//
uint8_t CEC_CAPTURE_ID_Set( uint8_t logical_address )
{
    uint8_t error = FALSE;
    uint8_t capture_address[2];
    uint8_t capture_addr_sel = 0x01;

    capture_address[0] = 0;
    capture_address[1] = 0;
    if( logical_address < 8 )
    {
        capture_addr_sel = capture_addr_sel << logical_address;
        capture_address[0] = capture_addr_sel;
    }
    else
    {
        capture_addr_sel   = capture_addr_sel << ( logical_address - 8 );
        capture_address[1] = capture_addr_sel;
    }

    // Set Capture Address
        RegisterWriteBlock(REG__CEC_CAPTURE_ID0,capture_address,2);
        RegisterWrite(REG__CEC_TX_INIT, logical_address);

    return 0;

}

//------------------------------------------------------------------------------
// Function Name: SiI_CEC_SendPing
// Function Description: This command intiate sending a ping, and used for checking available
//                       CEC devices
//
// Accepts: bCECLogAddr
// Returns: none
// Globals: none
//------------------------------------------------------------------------------
void SiI_CEC_SendPing ( uint8_t bCECLogAddr )
{

    RegisterWrite( REG__CEC_TX_INIT, BIT_SEND_POLL | bCECLogAddr );

}

//------------------------------------------------------------------------------
// Function Name: SiI_CEC_SetCommand
// Function Description: This command sets data for CEC transmission
//
// Accepts: pSiI_CEC
// Returns: 1 if error; 0 if no error.
// Globals: none
//------------------------------------------------------------------------------


uint8_t SiI_CEC_SetCommand( SiI_CEC_t * pSiI_CEC )
{
  uint8_t error = FALSE;
  uint8_t cec_int_status_reg[2];
  uint8_t sw_retry_counter = 0 ;

  // Clear Tx Buffer
  RegisterModify(REG__CEC_DEBUG_3, BIT_FLUSH_TX_FIFO, BIT_FLUSH_TX_FIFO);

  DEBUG_PRINT(("\n TX: HDR[0x%02X],OPC[0x%02X],OPR[0x%02X, %02X, %02X]", (int)pSiI_CEC->bDestOrRXHeader, (int)pSiI_CEC->bOpcode, (int)pSiI_CEC->bOperand[0], (int)pSiI_CEC->bOperand[1], (int)pSiI_CEC->bOperand[2])) ;

  #ifdef   CEC_TX_AUTO_CALC_ENABLED
  //
  // Enable TX_AUTO_CALC
  //
  RegisterWrite(REG__CEC_TRANSMIT_DATA, BIT__TX_AUTO_CALC);

  #endif// CEC_TX_AUTO_CALC_ENABLED
  //
  // Clear Tx-related buffers; write 1 to bits to be clear directly; writing 0 has no effect on the status bit
  //
  cec_int_status_reg[0] = 0x64 ; // Clear Tx Transmit Buffer Full Bit, Tx msg Sent Event Bit, and Tx FIFO Empty Event Bit
  cec_int_status_reg[1] = 0x02 ; // Clear Tx Frame Retranmit Count Exceeded Bit.
  RegisterWriteBlock(REG__CEC_INT_STATUS_0, cec_int_status_reg, 2);

  // Write Source and Destination address
  RegisterWrite(REG__CEC_TX_DEST,pSiI_CEC->bDestOrRXHeader);

  // Send CEC Opcode AND up to 15 Operands
 RegisterWriteBlock( REG__CEC_TX_COMMAND, &pSiI_CEC->bOpcode, pSiI_CEC->bCount + 1);


  if( error )
  {
      DEBUG_PRINT(("\n SiI_CEC_SetCommand(): Fail to write CEC opcode and operands\n")) ;
  }

  #ifndef CEC_TX_AUTO_CALC_ENABLED
  //
  // Write Operand count and activate send
  //
      RegisterWrite(REG__CEC_TRANSMIT_DATA, BIT_TRANSMIT_CMD | pSiI_CEC->bCount );
  #endif // CEC_TX_AUTO_CALC_ENABLED

    return error;

}//e.o. uint8_t SiI_CEC_SetCommand( SiI_CEC_t * pSiI_CEC )

//------------------------------------------------------------------------------
// Function Name: SiI_CEC_GetCommand
// Function Description: This function gets data from CEC Reception
//
// Accepts: pSiI_CEC
// Returns: none
// Globals: none
//------------------------------------------------------------------------------
uint8_t SiI_CEC_GetCommand( SiI_CEC_t * pSiI_CEC )
{
    uint8_t error = FALSE;
    uint8_t bCount;

    bCount = pSiI_CEC->bCount  & 0x0f; // extract uint8_t counter, ignore frame counter

    if ( !(pSiI_CEC->bCount & BIT_MSG_ERROR) )
        RegisterReadBlock(REG__CEC_RX_CMD_HEADER, &pSiI_CEC->bDestOrRXHeader , bCount + 2);
    else
        error = 1;

    // Clear CLR_RX_FIFO_CUR;
    // Clear current frame from Rx FIFO
    RegisterModify(REG__CEC_RX_CONTROL, BIT_CLR_RX_FIFO_CUR, BIT_CLR_RX_FIFO_CUR );

    // Check if more frame in Rx FIFO, if yes get uint8_t count of next frame.
    pSiI_CEC->bRXNextCount = 0;

    if( pSiI_CEC->bCount & 0xF0 )
    {
        pSiI_CEC->bRXNextCount = RegisterRead(REG__CEC_RX_COUNT);
    }

    return error;
}


//------------------------------------------------------------------------------
// Function Name: SiI_CEC_IntProcessing
// Function Description: This function is called on interrupt events
//                       it makes interrut service
// Accepts: SiI_CEC_Int_t * pInt
// Returns: none
// Globals: none
//------------------------------------------------------------------------------
uint8_t CEC_IntProcessing ( SiI_CEC_Int_t * pInt )
{
    uint8_t error = FALSE;
    uint8_t cec_int_status_reg[2];

    // Get Interrupts
    pInt->bTXState   = 0;
    pInt->bCECErrors = 0;
    pInt->bRXState   = 0;

    RegisterReadBlock(REG__CEC_INT_STATUS_0,cec_int_status_reg,2);


    {
        // Poll Interrupt
        if( (cec_int_status_reg[0] & 0x7F) || cec_int_status_reg[1] )
        {
           DEBUG_PRINT(("\nA6A7Reg: %02X %02X", (int) cec_int_status_reg[0], (int) cec_int_status_reg[1]));
            // Clear interrupts
            if ( cec_int_status_reg[1] & BIT_FRAME_RETRANSM_OV )
            {
               DEBUG_PRINT(("\n!CEC_A7_TX_RETRY_EXCEEDED![%02X][%02X]",(int) cec_int_status_reg[0], (int) cec_int_status_reg[1]));
                // flash TX otherwise after writing clear interrupt
                // BIT_FRAME_RETRANSM_OV the TX command will be re-send
               RegisterModify(REG__CEC_DEBUG_3,BIT_FLUSH_TX_FIFO, BIT_FLUSH_TX_FIFO);
            }
            //
            // Clear set bits that are set
            //
            RegisterWriteBlock(REG__CEC_INT_STATUS_0,cec_int_status_reg,2);

            DEBUG_PRINT(("\nA6A7Reg: %02X %02X", (int) cec_int_status_reg[0], (int) cec_int_status_reg[1]));

            // RX Processing
            if ( cec_int_status_reg[0] & BIT_RX_MSG_RECEIVED )
            {
                // Save number of frames in Rx Buffer
                pInt->bRXState = RegisterRead(REG__CEC_RX_COUNT);
            }

            // RX Errors processing
            if ( cec_int_status_reg[1] & BIT_SHORT_PULSE_DET )
            {
                pInt->bCECErrors |= eSiI_CEC_ShortPulse;
            }

            if ( cec_int_status_reg[1] & BIT_START_IRREGULAR )
            {
                pInt->bCECErrors |= eSiI_CEC_StartIrregular;
            }

            if ( cec_int_status_reg[1] & BIT_RX_FIFO_OVERRUN ) // fixed per Uematsu san
            {
                pInt->bCECErrors |= eSiI_CEC_RXOverFlow;
            }

            // TX Processing
            if ( cec_int_status_reg[0] & BIT_TX_FIFO_EMPTY )     //0x04
            {
                pInt->bTXState = eSiI_TXWaitCmd;
            }
            if ( cec_int_status_reg[0] & BIT_TX_MESSAGE_SENT )   //0x20
            {
                pInt->bTXState = eSiI_TXSendAcked;
            }
            if ( cec_int_status_reg[1] & BIT_FRAME_RETRANSM_OV )   //0x02
            {

                pInt->bTXState = eSiI_TXFailedToSend;
            }
        }
    }
    return error;
}

#endif //#if(CONF__CEC_ENABLE == ENABLE)
