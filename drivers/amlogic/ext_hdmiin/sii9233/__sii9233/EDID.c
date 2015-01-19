//------------------------------------------------------------------------------
// Copyright ? 2002-2005, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1060 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------
/***********************************************************************************/
/* Module Name:  UEDIDTbl.c                                                        */
/*                                                                                 */
/* Module Description: Contains EDID table which will be copyed into Stream Switch */
/*                     Channel 0                                                   */
/***********************************************************************************/
#include <local_types.h>
#include <config.h>
#include <hal.h>
#include <registers.h>
#include <amf.h>
#include <EDID.h>
#include <CEC.h>  


//supports 24Hz, HBR, 1080P
ROM const uint8_t abEDIDTabl [ 256 ] = {

/*  00    01    02   03     04    05    06    07   08     09    0A    0B    0C    0D    0E   0F */
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x4D, 0x29, 0x23, 0x92, 0x01, 0x00, 0x00, 0x00,
	0x00, 0x12, 0x01, 0x03, 0x80, 0x00, 0x00, 0x78, 0x0A, 0x0D, 0xC9, 0xA0, 0x57, 0x47, 0x98, 0x27,
	0x12, 0x48, 0x4C, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C,
	0x45, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x1E, 0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20,
	0x6E, 0x28, 0x55, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x43,
	0x50, 0x39, 0x32, 0x32, 0x33, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFD,
	0x00, 0x17, 0x78, 0x0F, 0x7E, 0x17, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x66,
	
	0x02, 0x03, 0x3D, 0x72, 0x55, 0x90, 0x04, 0x03, 0x02, 0x0E, 0x0F, 0x07, 0x23, 0x24, 0x05, 0x94,
	0x13, 0x12, 0x11, 0x1D, 0x1E, 0xA0, 0xA1, 0xA2, 0x01, 0x1F, 0x35, 0x09, 0x7F, 0x07, 0x09, 0x7F,
	0x07, 0x17, 0x07, 0x50, 0x3F, 0x06, 0xC0, 0x57, 0x06, 0x00, 0x5F, 0x7F, 0x01, 0x67, 0x1F, 0x00,
	0x83, 0x4F, 0x00, 0x00, 0x68, 0x03, 0x0C, 0x00, 0x10, 0x00, 0xB8, 0x2D, 0x00, 0x8C, 0x0A, 0xD0,
	0x8A, 0x20, 0xE0, 0x2D, 0x10, 0x10, 0x3E, 0x96, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x18, 0x8C,
	0x0A, 0xD0, 0x90, 0x20, 0x40, 0x31, 0x20, 0x0C, 0x40, 0x55, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00,
	0x18, 0x01, 0x1D, 0x80, 0x18, 0x71, 0x1C, 0x16, 0x20, 0x58, 0x2C, 0x25, 0x00, 0xC4, 0x8E, 0x21,
	0x00, 0x00, 0x9E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E


};




ROM const uint8_t abEXTRATabl	 [ 32 ] = {
0xAA, 0x55, 0x04, 0x0F, 0x0F, 0x00, 0x00, 0x00,      //0 -7
0x00, 0x00, 0xB8, 0x00, 0x10, 0x00, 0x20, 0x00,      //8 - f
0x30, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,      //10 - 17 
0x00, 0x00, 0x00, 0x00, 0x7E, 0x6E, 0x5E, 0x4E	     //18 -1F										    
};


ROM const uint8_t abEXTRATabl9127 [ 32 ] = {
0xAA, 0x55, 0x04, 0x0F, 0x0F, 0x00, 0x00, 0x00,      //0 -7
0x00, 0x00, 0xB8, 0x00, 0x00, 0x00, 0x10, 0x00,      //8 - f
0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,      //10 - 17 
0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x6E, 0x00	     //18 -1F										    
};

ROM const uint8_t physicalAddr[4] = {(CEC_PA_EDID_CH0>>8)&0xFF, (CEC_PA_EDID_CH1>>8)&0xFF , (CEC_PA_EDID_CH2>>8)&0xFF , (CEC_PA_EDID_CH3>>8)&0xFF  };


#if (PEBBLES_ES1_NVM == ENABLE)
static void ProgramExtra(void)
{
	uint8_t i;

//program  extra to NVM
	 RegisterWrite(REG__EDID_FIFO_SEL,BIT__SEL_EXTRA); //enable extra
	 RegisterWrite(REG__EDID_FIFO_ADDR,VAL__FIFO_ADDR_00); //address to start

	if(0x27 == RegisterRead(REG__IDL_RX))
		 for(i=0; i<=32; i++)
		  {
		     RegisterWrite(REG__EDID_FIFO_DATA,abEXTRATabl9127[i]);       
		
		  }	
	else
		 for(i=0; i<=32; i++)
		  {
		     RegisterWrite(REG__EDID_FIFO_DATA,abEXTRATabl[i]);       
		
		  }	
			  		   
	
	  RegisterWrite(REG__NVM_COMMAND,VAL__PRG_EXTRA);  //prog extra
	  while( (RegisterRead(REG__NVM_COMMAND_DONE)& BIT__NVM_COMMAND_DONE) == 0) //wait done
	  ;
	  
}

void ProgramEDID(void)
{
	uint16_t i;
	
	RegisterWrite(REG__EN_EDID,VAL__EN_EDID_NONE); //disable EDID DDC for all ports
	//program EDID
#if (PEBBLES_ES1_NVM_LOADED_CHECK == ENABLE)
	if(RegisterRead(REG__NVM_STAT)==VAL__NVM_VALID)
	{
	   RegisterWrite(REG__EN_EDID,VAL__EN_EDID_ALL); //enable EDID DDC for all ports
	   return;
	 }
	else

#endif
	
    RegisterWrite(REG__EDID_FIFO_SEL,BIT__SEL_EDID0); //enable port 0
    RegisterWrite(REG__EDID_FIFO_ADDR,VAL__FIFO_ADDR_00); //address to start


	for(i=0; i<=255; i++)
       RegisterWrite(REG__EDID_FIFO_DATA,abEDIDTabl[i]);	

    RegisterWrite(REG__NVM_COMMAND,VAL__PRG_EDID);  //prog EDID
    while( ((RegisterRead(REG__NVM_COMMAND_DONE)& BIT__NVM_COMMAND_DONE) == 0)) //wait done
   	 ;				 
	ProgramExtra();

	//re - init with new EDID
    RegisterWrite(REG__BSM_INIT,BIT__BSM_INIT);
    while( (RegisterRead(REG__BSM_STAT)& BIT__BOOT_DONE )== 0) //wait done
	  ;	
    if((RegisterRead(REG__BSM_STAT)& BIT__BOOT_ERROR)!=0)
		DEBUG_PRINT(("Re-Boot error! \n"));

    RegisterWrite(REG__EN_EDID,VAL__EN_EDID_ALL); //enable EDID DDC for all ports

}
#else
void ProgramEDID()
{
    uint16_t i; 
	uint8_t k = 0;
	uint8_t j = 0;
    uint8_t cksum;
	uint8_t firstPort=0x01;
	uint8_t lastPort=0x08;

	DEBUG_PRINT(("program SRAM! \n"));

    RegisterWrite(REG__HP_CTRL,VAL__HP_PORT_NONE); //disable hotplug before changing

	if(0x27 == RegisterRead(REG__IDL_RX))  //9127 has only 2 ports
	{
	   firstPort = 0x02;
	   lastPort = 0x04;																	
	}																				
	
    for( j=firstPort; j<=lastPort; j=j<<1 )
    {

        cksum=0;
        RegisterWrite(REG__EDID_FIFO_SEL,j); //enable ports
        RegisterWrite(REG__EDID_FIFO_ADDR,0x00); //address to start

        for(i=0; i<255; i++)
       {
          if(i==CEC_PA_EDID_OFFSET)
          {
             // Program specific physical address
             //
             // Port0 1000
             // Port1 2000
             // Port2 3000
             // Port3 4000


             RegisterWrite( REG__EDID_FIFO_DATA, physicalAddr[k] );	//High uint8_t
             cksum = cksum + (uint8_t)physicalAddr[k];	
			
             RegisterWrite( REG__EDID_FIFO_DATA, 0x00 );					//Low uint8_t
             i = i+1 ;
			 k = k+1;
          }

          else
         {
           RegisterWrite(REG__EDID_FIFO_DATA,abEDIDTabl[i]);
           cksum = cksum + abEDIDTabl[i];
          }
       }

       RegisterWrite(REG__EDID_FIFO_DATA,0-cksum);    //2nd block

     }

   RegisterWrite(REG__EN_EDID,VAL__EN_EDID_ALL); //enable ports
   RegisterWrite(REG__HP_CTRL,VAL__HP_PORT_ALL); //enable hotplug
}

#endif
