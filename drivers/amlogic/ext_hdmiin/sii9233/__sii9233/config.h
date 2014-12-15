//------------------------------------------------------------------------------
// Copyright © 2007, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1060 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------
#ifndef __CONFIG_H__
#define __CONFIG_H__



//------------------------------------------------------------------------------
// Include HAL for target board
//------------------------------------------------------------------------------
#include <hal_cp9223.h>

//------------------------------------------------------------------------------
// NVRAM Configuration
//------------------------------------------------------------------------------


#define PEBBLES_ES1_STARTER_CONF                        (ENABLE)
#define PEBBLES_ES1_NVM                                 (ENABLE)	//enable if chip is Not NVM programmed
#define PEBBLES_ES1_NVM_LOADED_CHECK                    (DISABLE)	//enable if chip is already NVM programmed


//------------------------------------------------------------------------------
// ZONE Workaround
//------------------------------------------------------------------------------

#define PEBBLES_ES1_ZONE_WORKAROUND						(ENABLE) //ES1.1 auto zone detection issue
#define PEBBLES_ES1_FF_WORKAROUND						(ENABLE) //ES1.1 FF part 
#define PEBBLES_STARTER_NO_CLK_DIVIDER					(ENABLE) //for demo 480i with starter board
#define PEBBLES_VIDEO_STATUS_2ND_CHECK					(ENABLE) //check pclk status again after video unmute
#define VIDEO_UNMUTE_TIMEOUT							(400)	 //set video unmute timeout 400ms 
#define VIDEO_STABLITY_CHECK_INTERVAL					(100)	 //set video stability check timeout 400ms 
#define VIDEO_STABLITY_CHECK_TOTAL						(5000)   //multiple check within this time
//------------------------------------------------------------------------------
// Receiver Configuration
//------------------------------------------------------------------------------

// Register Slave Addresses
#define CONF__I2C_SLAVE_PAGE_0          (0x60)
#define CONF__I2C_SLAVE_PAGE_1          (0x68)
#define CONF__I2C_SLAVE_PAGE_8          (0xC0)   //CEC
#define CONF__I2C_SLAVE_PAGE_9          (0xE0)   //EDID
#define CONF__I2C_SLAVE_PAGE_A          (0x64)   //xvYCC
#define CONF__I2C_SLAVE_PAGE_C          (0xE6)   //CBUS



//------------------------------------------------------------------------------
// Video Output Configuration
//------------------------------------------------------------------------------

// Video Output Bus Formatting
#define CONF__ODCK_INVERT               (DISABLE )
#define CONF__VSYNC_INVERT              (DISABLE)
#define CONF__HSYNC_INVERT              (DISABLE)
#define CONF__OUTOUT_COLOR_DEPTH        (VAL__OUT_COLOR_DEPTH_8)
#define CONF__OUTPUT_YCBCR        		(DISABLE)
#define CONF__OUTPUT_SAMPL_422        	(DISABLE)
#define CONF__OUTPUT_MUXYC				(DISABLE)	
#define CONF__OUTPUT_EMBEDDED_SYNC		(DISABLE)	

#define CONF__OUTPUT_VIDEO_FORMAT       (VAL__RGB)



//------------------------------------------------------------------------------
// Audio Output Configuration
//------------------------------------------------------------------------------

// PCM Audio Output Configuration
#define CONF__PCM__MCLK                 (VAL__SWMCLK_256)
#define CONF__PCM__I2S_CTRL1            (BIT__SCK)      //invert SCK, rest are defaults
#define CONF__PCM__I2S_CTRL2__LAYOUT_0  (BIT__SD0)      //channel 0 only for layout 0
#define CONF__PCM__I2S_CTRL2__LAYOUT_1  (BIT__SD0 | BIT__SD1 | BIT__SD2 | BIT__SD3)  //all channels for layout1

// DSD Audio Output Configuration
#define CONF__DSD__MCLK                 (VAL__SWMCLK_512)
#define CONF__DSD__I2S_CTRL1            (BIT__SCK)      //invert SCK, rest are defaults
#define CONF__DSD__I2S_CTRL2__LAYOUT_0  (BIT__SD0)      //channel 0 only for layout 0
#define CONF__DSD__I2S_CTRL2__LAYOUT_1  (BIT__SD0 | BIT__SD1 | BIT__SD2)  //all 6 channels for layout1

// HBR Audio Output Configuration
#define CONF__HBR__MCLK                 (VAL__SWMCLK_128)
#define CONF__HBR__I2S_CTRL1            (BIT__SCK)
#define CONF__HBR__I2S_CTRL2            (BIT__SD0 | BIT__SD1 | BIT__SD2 | BIT__SD3)  //all channels - HBR requires layout1

// Common Audio Configuration
//$$ #define CONF__AUD_CTRL                  (BIT__MUTE_MODE | BIT__PSERR | BIT__PAERR | BIT__I2SMODE | BIT__SPEN)  //use soft mute - pass errors
#define CONF__AUD_MUTE_MODE             (ENABLE) 
#define CONF__AUD_MODE_EN				(BIT__HBR_ENABLE 	|	\
										 BIT__DSD_ENABLE	|	\
										 BIT__SPDIF_ENABLE	|	\
										 BIT__HBR_ENABLE)


#define CONF__AUD_CTRL                  (BIT__MUTE_MODE | BIT__PSERR | BIT__PAERR | BIT__I2SMODE | BIT__SPEN)  //use soft mute - pass errors


//------------------------------------------------------------------------------
// Other Chip Configuration
//------------------------------------------------------------------------------

// HDCP Error Threshold and Tolerance
#define CONF__HDCPTHRESH                (0x0B40)      // recommended value
#define CONF__HDCP_MAX_ERRORS           (30)

// Termination Impedence
#define CONF__TERMINATION_VALUE         (VAL__TERM_CNTL_45)

// Misc Register Configuration
#define CONF__AACR_CFG1_VALUE           (0x88)      // recommended value
#define CONF__CTS_THRESH_VALUE             (4)      // recommended value

#define CONF__SUPPORT_ALL_INFOFRAMES	(ENABLE)	// used for demonstration of repeater's configuratiom
#define CONF__SUPPORT_3D				(ENABLE)	// used for support device in 3 Mode
#define CONF__SUPPORT_REPEATER3D		(DISABLE)	// used for support device in 3 Mode
#define CONF__VSYNC_OVERFLOW			(ENABLE)	// overlow for line counter 

//------------------------------------------------------------------------------
// Application Configuration
//------------------------------------------------------------------------------

// Timeout Values
#define CONF__VIDEO_UNMUTE_DELAY        (100)       //delay before unmute video (mSec)
#define CONF__POWERDOWN_DELAY           (3000)      //delay before power down (mSec)

// Interrupt Polling Method
#define CONF__POLL_INTERRUPT_PIN        (DISABLE)   //enable to poll GPIO pin instead of register

// Debug Options
#define CONF__DEBUG_PRINT               (DISABLE)    //print debug messages
#define CONF__DEBUG_CMD_HANDLER         (DISABLE)    //run debug command handler



//------------------------------------------------------------------------------
// HAL Configuration
//------------------------------------------------------------------------------

// UART Timer 2 divider values for baud rate generator
//   65518 = 19,200 baud with 11.0592 MHz crystal
//   65503 = 19,200 baud with 20 MHz crystal
#define CONF__UART_BAUD_DIVIDER   (65503)

// Timer Configuration
#define TIMER__VIDEO        0  //for video state machine
#define TIMER__AUDIO        1  //for audio state machine
#define TIMER__DELAY        2  //for general blocking delay calls
#define TIMER__POLLING      3  //for main polling loop

#ifdef TEST_VERSION
#define CONF__TIMER_COUNT       5
#define TIMER_TEST              4

#else
#define CONF__TIMER_COUNT   4
#endif

// Port Switch Debounce
#define CONF__SWITCH_DEBOUNCE_COUNT  20



//------------------------------------------------------------------------------
// General Configuration Defines
//------------------------------------------------------------------------------
#define ENABLE      (0xFF)
#define DISABLE     (0x00)



//----------
//CEC configuration
//-----------

#define CONF__CEC_ENABLE (DISABLE)


//----------------------------------
//ODCK output stop at limit feature
//----------------------------------
//
#define CONF__ODCK_LIMITED (DISABLE)
#define CONF__PCLK_MAX_CNT (0x74)	 //Max ODCK will not exceed 148MHz






#endif  // _CONFIG_H__
