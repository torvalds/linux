//------------------------------------------------------------------------------
// Copyright © 2007, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1060 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------
#ifndef __HAL_CP9135_H__
#define __HAL_CP9135_H__



///-------------------------------------------------------------------------------
// Hot Plug Control Mask Bits
//-------------------------------------------------------------------------------
#define HOT_PLUG__PORT_NONE     (0)
#define HOT_PLUG__PORT_0        (1)
#define HOT_PLUG__PORT_1        (2)
#define HOT_PLUG__PORT_2        (3)
#define HOT_PLUG__PORT_3        (4)


//-------------------------------------------------------------------------------
// Port Select Switch Mask Bits
//-------------------------------------------------------------------------------
#define PORT_SELECT__PORT_0     (0x07)	 //Pebbles starter ES0
#define PORT_SELECT__PORT_1     (0x06)
#define PORT_SELECT__PORT_2     (0x05)
#define PORT_SELECT__PORT_3     (0x04)
#define PORT_SELECT__PORT_7     (0x00)

#define PORT_SELECT__NO_CHANGE  (0xFF)


//-------------------------------------------------------------------------------
// Audio Mux Settings
//-------------------------------------------------------------------------------
#define AUDIO_MUX__HDMI         		(0)
#define AUDIO_MUX__DVI_ANALOG_PORT0   	(1)
#define AUDIO_MUX__DVI_ANALOG_PORT1   	(2)


//-------------------------------------------------------------------------------
// GPIO Pin Definitions (8 pins max)
//-------------------------------------------------------------------------------
#define GPIO_PIN__PORT_SELECT_SWITCH_0     (1 << 0)
#define GPIO_PIN__PORT_SELECT_SWITCH_1     (1 << 1)
#define GPIO_PIN__PORT_SELECT_SWITCH_2     (1 << 2)
#define GPIO_PIN__VCCEN					   (1 << 3)
#define GPIO_PIN__AUDIO_DAC_MUTE      	   (1 << 4)
#define GPIO_PIN__AUDIO_DAC_RESET     	   (1 << 5)
#define GPIO_PIN__HARDWARE_RESET		   (1 << 6)
#define GPIO_PIN__RX_INTERRUPT             (1 << 7)



//-------------------------------------------------------------------------------
// 8051 GPIO Default Pin Mappings
//-------------------------------------------------------------------------------
#define GPIO_PIN_MAP__PORT_SELECT_SWITCH_0  P0^2
#define GPIO_PIN_MAP__PORT_SELECT_SWITCH_1  P0^3
#define GPIO_PIN_MAP__PORT_SELECT_SWITCH_2  P0^4

#define GPIO_PIN_MAP__COM_MODE				P0^0
#define GPIO_PIN_MAP__VCCEN					P0^5

#define GPIO_PIN_MAP__AUDIO_DAC_MUTE        P2^0
#define GPIO_PIN_MAP__AUDIO_DAC_RESET       P2^6
#define GPIO_PIN_MAP__HARDWARE_RESET        P0^1
#define GPIO_PIN_MAP__RX_INTRRUPT           P3^2

#define GPIO_PIN_MAP__CECD					P1^3

//-------------------------------------------------------------------------------
// 8051 SW I2C Pin Mappings
//-------------------------------------------------------------------------------
#define GPIO_PIN_MAP__I2C_SCL            P1^6
#define GPIO_PIN_MAP__I2C_SDA            P1^7


//-------------------------------------------------------------------------------
// Board Support HAL Function Prototypes
//-------------------------------------------------------------------------------
void HAL_Init(void);
void HAL_HardwareReset(void);
void HAL_VccEnable(uint8_t qON);

void HAL_SetHotPlug(uint8_t portMask);
void HAL_SetAudioMux(uint8_t muxInput);

uint8_t HAL_GetInterruptPinState(void);
uint8_t HAL_GetPortSelection(void);
uint8_t HAL_GetPortSelectionDebounced(void);

void HAL_PowerDownAudioDAC(void);
void HAL_WakeUpAudioDAC(void);
void HAL_SetAudioDACMode(uint8_t dacModeDSD);

void GPIO_ClearCecD(void);
uint8_t GPIO_GetComMode(void);


#endif  // __HAL_CP9135_H__
