//------------------------------------------------------------------------------
// Copyright © 2007, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1060 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------
#ifndef __HAL_H__
#define __HAL_H__



//-------------------------------------------------------------------------------
// Debug Include
//-------------------------------------------------------------------------------
#include <stdio.h>      //for printf and putchar
#include <linux/kernel.h>


//-------------------------------------------------------------------------------
// Debug Print Macro 
//   Note: DEBUG_PRINT Requires double parenthesis
//   Example:  DEBUG_PRINT(("hello, world!\n"));
//-------------------------------------------------------------------------------
#if (CONF__DEBUG_PRINT == ENABLE)
    #define DEBUG_PRINT(x)      printf x

#else
    #define DEBUG_PRINT(x)
#endif



//-------------------------------------------------------------------------------
// Microprocessor HAL Function Prototypes
//-------------------------------------------------------------------------------
void UART_Init(void);

void TIMER_Init(void);
void TIMER_Set(uint8_t timer, uint16_t value);
uint8_t TIMER_Expired(uint8_t timer);
void TIMER_Wait(uint8_t index, uint16_t value);
uint16_t TIMER_GetTickCounter( void );

uint8_t GPIO_GetPins(uint8_t pinMask);
void GPIO_SetPins(uint8_t pinMask);
void GPIO_ClearPins(uint8_t pinMask);


uint8_t I2C_ReadByte(uint8_t deviceID, uint8_t offset);
void I2C_WriteByte(uint8_t deviceID, uint8_t offset, uint8_t value);
uint8_t I2C_ReadBlock(uint8_t deviceID, uint8_t offset, uint8_t* buffer, uint16_t length);
uint8_t I2C_WriteBlock(uint8_t deviceID, uint8_t offset, uint8_t* buffer, uint16_t length);



#endif  // _HAL_H__
