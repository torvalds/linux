//------------------------------------------------------------------------------
// Copyright © 2007, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1060 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------
#include <local_types.h>
#include <config.h>
#include <hal.h>



//------------------------------------------------------------------------------
// Local Defines for port switch value
//------------------------------------------------------------------------------

#define SWITCH_VALUE_PORT_0      0x07
#define SWITCH_VALUE_PORT_1      0x06
#define SWITCH_VALUE_PORT_2      0x05
#define SWITCH_VALUE_PORT_3      0x04


//------------------------------------------------------------------------------
// Local Defines for APL control
//------------------------------------------------------------------------------

//I2C slave address
#define APL_SLAVE_ADDR      0x64
#define APL_CTRL_ADDR		0x08


//------------------------------------------------------------------------------
// Local Defines for Audio DAC control
//------------------------------------------------------------------------------

//I2C slave address
#define ADAC_SLAVE_ADDR     0x24	 //changed from 0x26 to avoid conflict with Simon

//register offsets
#define ADAC_CTRL1_ADDR     0x00
#define ADAC_CTRL2_ADDR     0x01
#define ADAC_SPEED_PD_ADDR  0x02
#define ADAC_CTRL3_ADDR     0x0A

//power down control bits
#define ADAC_POWER_DOWN        0x00
#define ADAC_NORMAL_OPERATION  0x4F

//dac operating modes
#define ADAC_PCM_MODE       0x00
#define ADAC_DSD_MODE       0x18

//------------------------------------------------------------------------------
// Function: HAL_Init
// Description: Initialize HAL subsystem
//------------------------------------------------------------------------------
void HAL_Init(void)
{
    TIMER_Init();

#if (CONF__DEBUG_PRINT == ENABLE) || (CONF__DEBUG_CMD_HANDLER == ENABLE)
    UART_Init();
#endif
}

#ifdef NOT_USED_NOW
//------------------------------------------------------------------------------
// Function: HAL_HardwareReset
// Description: Give a hardware reset pulse to the chip
//------------------------------------------------------------------------------
void HAL_HardwareReset(void)
{
    GPIO_ClearPins(GPIO_PIN__HARDWARE_RESET);
    TIMER_Wait(TIMER__DELAY, 2);

    GPIO_SetPins(GPIO_PIN__HARDWARE_RESET);
    TIMER_Wait(TIMER__DELAY, 5);

}



//------------------------------------------------------------------------------
// Function: HAL_SetHotPlug
// Description: Set hot plug for one port or clear hot plug for all ports
//------------------------------------------------------------------------------
void HAL_SetHotPlug(uint8_t portMask)
{
    switch (portMask)
    {
        case HOT_PLUG__PORT_NONE:
#if 0
		    I2C_WriteByte(ADAC_SLAVE_ADDR, ADAC_SPEED_PD_ADDR, ADAC_POWER_DOWN);
#endif
            break;
        case HOT_PLUG__PORT_0:
            break;
        case HOT_PLUG__PORT_1:
            break;
    }
}
#endif



#if (CONF__POLL_INTERRUPT_PIN == ENABLE)
//------------------------------------------------------------------------------
// Function: HAL_GetInterruptPinState
// Description: Get current state of interrupt pin input
//------------------------------------------------------------------------------
uint8_t HAL_GetInterruptPinState(void)
{
    return (GPIO_GetPins(GPIO_PIN__RX_INTERRUPT));
}
#endif



//------------------------------------------------------------------------------
// Function: HAL_GetPortSelection
// Description: Get current state of port select switch
//              This read may not be stable, so it needs debounce routine below
//------------------------------------------------------------------------------
uint8_t HAL_GetPortSelection(void)
{
    uint8_t bPortSelection;
	
    bPortSelection = GPIO_GetPins(GPIO_PIN__PORT_SELECT_SWITCH_0 | GPIO_PIN__PORT_SELECT_SWITCH_1 |
                                  GPIO_PIN__PORT_SELECT_SWITCH_2 );

    return (bPortSelection);
}


//------------------------------------------------------------------------------
// Function: HAL_GetPortSelectionDebounced
// Description: Debounce read of port select switch - return debounced value
//------------------------------------------------------------------------------
uint8_t HAL_GetPortSelectionDebounced(void)
{
    static uint8_t bDebounceCount = CONF__SWITCH_DEBOUNCE_COUNT + 1;
    static uint8_t bOldPortSelection = PORT_SELECT__NO_CHANGE;
	static bool_t init = TRUE;

    uint8_t bNewPortSelection;

    bNewPortSelection = HAL_GetPortSelection();
	if(init)
	{
	  bOldPortSelection = bNewPortSelection;
	  init = FALSE;
	  return (bNewPortSelection);
	}
    if(bNewPortSelection != bOldPortSelection)      //port change detected
    {
        bOldPortSelection = bNewPortSelection;
        bDebounceCount = 0;
    }
    else if(bDebounceCount < CONF__SWITCH_DEBOUNCE_COUNT)        //keep counting while stable
    {
        bDebounceCount++;
    }
    else if(bDebounceCount == CONF__SWITCH_DEBOUNCE_COUNT)       //change confirmed
    {
        bDebounceCount = CONF__SWITCH_DEBOUNCE_COUNT + 1;
        DEBUG_PRINT(("Port=0x%X\n", (int) bNewPortSelection));

        return (bNewPortSelection);                 //return new port
    }

    return (PORT_SELECT__NO_CHANGE);                //indicate no change
}


//------------------------------------------------------------------------------
// Function: HAL_PowerDownAudioDAC
// Description: Send power down command to audio DAC
//              Also mute the output by disableing output latch seperate from audio DAC
//------------------------------------------------------------------------------
void HAL_PowerDownAudioDAC(void)
{
#if 0
    I2C_WriteByte(ADAC_SLAVE_ADDR, ADAC_SPEED_PD_ADDR, ADAC_POWER_DOWN);    
    GPIO_SetPins(GPIO_PIN__AUDIO_DAC_MUTE);   //set mute pin (latch on 9135)
#endif
}


//------------------------------------------------------------------------------
// Function: HAL_WakeUpAudioDAC
// Description: Send wake up command to audio DAC
//              Also un-mute the output by enabling output latch seperate from audio DAC
//------------------------------------------------------------------------------
void HAL_WakeUpAudioDAC(void)
{
#if 0
    I2C_WriteByte(ADAC_SLAVE_ADDR, ADAC_SPEED_PD_ADDR, ADAC_NORMAL_OPERATION);  
    GPIO_ClearPins(GPIO_PIN__AUDIO_DAC_MUTE);  //clear mute pin (latch on 9135)
#endif
}


//------------------------------------------------------------------------------
// Function: HAL_SetAudioDACMode
// Description: Send mode change command to audio DAC to select DSD or standard PCM mode
//------------------------------------------------------------------------------
void HAL_SetAudioDACMode(uint8_t dacMode)
{
#if 0
    I2C_WriteByte(ADAC_SLAVE_ADDR, ADAC_SPEED_PD_ADDR, ADAC_POWER_DOWN);

    if (dacMode)
    {
        I2C_WriteByte(ADAC_SLAVE_ADDR, ADAC_CTRL3_ADDR, ADAC_DSD_MODE);
        DEBUG_PRINT(("dsd dac\n"));
    }
    else {
        I2C_WriteByte(ADAC_SLAVE_ADDR, ADAC_CTRL3_ADDR, ADAC_PCM_MODE);
        DEBUG_PRINT(("PCM dac\n"));
    }
#endif
}

//------------------------------------------------------------------------------
// Function: HAL_VccEnable
// Description: enable or disable the main +5V power from system
//------------------------------------------------------------------------------
 void HAL_VccEnable(uint8_t qON)
 {
 	  if(qON)
	  	  	GPIO_SetPins(GPIO_PIN__VCCEN);

	  else
	  		GPIO_ClearPins(GPIO_PIN__VCCEN);
 }


