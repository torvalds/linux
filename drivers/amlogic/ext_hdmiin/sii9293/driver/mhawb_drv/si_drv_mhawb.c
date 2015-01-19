//******************************************************************************
//!file     si_drv_mhawb.c
//!brief    SiI5293 CBUS Driver Extension
//
// No part of this work may be reproduced, modified, distributed,
// transmitted, transcribed, or translated into any language or computer
// format, in any form or by any means without written permission of
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2012, Silicon Image, Inc.  All rights reserved.
//*****************************************************************************/

#ifndef __KERNEL__
#include "si_drv_internal.h"
#endif

#include "si_drv_mhawb.h"
#include "../../application/si_common.h"

#ifdef __KERNEL__
#include "si_drv_hawb.h"
#include "si_cbus_enums.h"
#include "si_mdt_inputdev.h"
#include <linux/sched.h>
#include <linux/wait.h>
#include "si_cbus_component.h"
#include "si_regs_mhl5293.h"
#include "sii_hal.h"
#endif

//#define FLOOD_TEST
#ifdef  MHAWB_SUPPORT
DEFINE_SEMAPHORE(g_api_lock);
struct mhawb_t MHAWB_MEMORY g_mhawb = {
	MHAWB_STATE_UNINITIALIZED,				//state
	0,							//start_time
	0,							//max_packet_length
	{0,0},							//peer_adopter_id
	MHAWB_XFIFO_EMPTY_LEVELS_MAX,				//empty_levels
	#if (MHAWB_ACCEL_SETTING == MHAWB_RUNTIME)
	MHAWB_ACCEL_SETTING,					//flags_accelerations
	#endif
};

static  struct workqueue_struct 	*mdt_hid_bridge_wq = NULL;	
        struct work_struct		mhawb_work;
static void   delayed_mhawb_do_work_func(struct work_struct *p);

#define FIELD_FIRST_BYTE_OFFEST	0
#define FIELD_HEADER_SUM				1

uint8_t MHAWB_CODE_MEMORY offsets_and_header_lengths[4][2] = {
	//length must include 2 byte adopter id,  1 byte length, and # of MDT bytes
	{MHAWB_ACCEL_NONE_LEVEL_BYTE_OFFSET_FOR_FIRST_BYTE, 		MHAWB_ACCEL_NONE_LEVEL_BYTE_LENGHT_HEADER_SUM},
	//length must include 2 byte adopter id and # of MDT bytes
  {MHAWB_ACCEL_LENGTH_LEVEL_BYTE_OFFSET_FOR_FIRST_BYTE,			MHAWB_ACCEL_LENGTH_LEVEL_BYTE_LENGHT_HEADER_SUM},
	//length must include 1 byte length  and # of MDT bytes
	{MHAWB_ACCEL_ADOPTERID_LEVEL_BYTE_OFFSET_FOR_FIRST_BYTE,	MHAWB_ACCEL_ADOPTERID_LEVEL_BYTE_LENGHT_HEADER_SUM},
	//length must be # of MDT bytes
	{MHAWB_ACCEL_ALL_BYTE_OFFSET_FOR_FIRST_BYTE,			MHAWB_ACCEL_ALL_LEVEL_BYTE_LENGHT_HEADER_SUM}	
};

uint8_t MHAWB_CODE_MEMORY clear_tx_fifo[3] = { 
	MHAWB_GEN_2_WRITE_BURST_XFIFO_EMPTY,
	MHAWB_REG_GEN_2_WRITE_BURST_INTR_MASK_VALUE,
	0xE0};

uint8_t	cached_state = 0xff;
uint8_t	cached_intr  = 0xff;

union mhawb_circular_level_buffer_t MHAWB_MEMORY *next_tx;
union mhawb_circular_level_buffer_t MHAWB_MEMORY *next_rx;

extern uint8_t (*mdt_burst_value_update)(struct hid_usage *, __s32);
extern uint8_t (*mdt_burst_sync_and_send)(void);

//2013-05-22 - support timeouts
#ifndef __KERNEL__
bool_t is_expired(clock_time_t ms_start, clock_time_t ms_timeout) {
	
	clock_time_t ms_now = SiiTimerTotalElapsed();
	clock_time_t ms_elapsed;
	
	if (ms_now >= ms_start)
		ms_elapsed = ms_now - ms_start;
	else
		ms_elapsed = (0xffffffff - ms_start) + ms_now;
	
	if (ms_elapsed > ms_timeout)
		return true;
	else
		return false;
}
#endif

void print_intr_status(void) {
#if 0
	if(SiiRegRead(REG_INTR_STATE_2)) {
			printk(KERN_ERR "INTR2 %02X\n",SiiRegRead(REG_INTR_STATE_2));
			//SiiRegWrite(REG_INTR_STATE_2, SiiRegRead(REG_INTR_STATE_2));
	}

	if(SiiRegRead(MHAWB_REG_INTR_STATE)) {
			printk(KERN_ERR "INTR1 %02X\n",SiiRegRead(MHAWB_REG_INTR_STATE));
			//SiiRegWrite(MHAWB_REG_INTR_STATE, SiiRegRead( MHAWB_REG_INTR_STATE ));
	}

	if(SiiRegRead(MHAWB_CBUS_DDC_ABORT)) {
			printk(KERN_ERR "DDC %02X\n",SiiRegRead(MHAWB_CBUS_DDC_ABORT));
			//SiiRegWrite(MHAWB_CBUS_DDC_ABORT, SiiRegRead( MHAWB_CBUS_DDC_ABORT ));
	}

	if(SiiRegRead(REG_CBUS_SET_INT_0 )) {
			printk(KERN_ERR "SI0 %02X\n",SiiRegRead(REG_CBUS_SET_INT_0 ));
			//SiiRegWrite(REG_CBUS_SET_INT_0 , SiiRegRead( REG_CBUS_SET_INT_0  ));
	}
	if(SiiRegRead(REG_CBUS_SET_INT_1 )) {
			printk(KERN_ERR "SI1 %02X\n",SiiRegRead(REG_CBUS_SET_INT_1 ));
			//SiiRegWrite(REG_CBUS_SET_INT_1 , SiiRegRead( REG_CBUS_SET_INT_1  ));
	}
	if(SiiRegRead(REG_CBUS_SET_INT_2 )) {
			printk(KERN_ERR "SI2 %02X\n",SiiRegRead(REG_CBUS_SET_INT_2 ));
			//SiiRegWrite(REG_CBUS_SET_INT_2 , SiiRegRead( REG_CBUS_SET_INT_2  ));
	}
	if(SiiRegRead(REG_CBUS_SET_INT_3 )) {
			printk(KERN_ERR "SI3 %02X\n",SiiRegRead(REG_CBUS_SET_INT_3 ));
			SiiRegWrite(REG_CBUS_SET_INT_3 , SiiRegRead( REG_CBUS_SET_INT_3  ));
	}

	if(SiiRegRead(REG_CBUS_WRITE_STAT_0 )) {
			printk(KERN_ERR "WS0 %02X\n",SiiRegRead(REG_CBUS_WRITE_STAT_0  ));
			//SiiRegWrite(REG_CBUS_WRITE_STAT_0  , SiiRegRead( REG_CBUS_WRITE_STAT_0   ));
			//SiiRegWrite(REG_CBUS_WRITE_STAT_0  , 0);
	}
	if(SiiRegRead(REG_CBUS_WRITE_STAT_1 )) {
			printk(KERN_ERR "WS1 %02X\n",SiiRegRead(REG_CBUS_WRITE_STAT_1  ));
			//SiiRegWrite(REG_CBUS_WRITE_STAT_1  , SiiRegRead( REG_CBUS_WRITE_STAT_1   ));
			//SiiRegWrite(REG_CBUS_WRITE_STAT_1  , 0);
	}
	if(SiiRegRead(REG_CBUS_WRITE_STAT_2 )) {
			printk(KERN_ERR "WS2 %02X\n",SiiRegRead(REG_CBUS_WRITE_STAT_2  ));
			//SiiRegWrite(REG_CBUS_WRITE_STAT_2  , SiiRegRead( REG_CBUS_WRITE_STAT_2   ));
			//SiiRegWrite(REG_CBUS_WRITE_STAT_2  , 0);
	}
	if(SiiRegRead(REG_CBUS_WRITE_STAT_3 )) {
			printk(KERN_ERR "WS3 %02X\n",SiiRegRead(REG_CBUS_WRITE_STAT_3  ));
			//SiiRegWrite(REG_CBUS_WRITE_STAT_3  , SiiRegRead( REG_CBUS_WRITE_STAT_3   ));
			//SiiRegWrite(REG_CBUS_WRITE_STAT_3  , 0);
	}


	if ((int)SiiRegRead(RX_A__INTR1  ) & (int)SiiRegRead( RX_A__INTR1_MASK))
		printk(KERN_ERR "I1 %02X %02X", (int)SiiRegRead( RX_A__INTR1  ), (int)SiiRegRead( RX_A__INTR1_MASK ));
	if ((int)SiiRegRead(RX_A__INTR2  ) & (int)SiiRegRead( RX_A__INTR2_MASK))
		printk(KERN_ERR "I2 %02X %02X", (int)SiiRegRead( RX_A__INTR2  ), (int)SiiRegRead( RX_A__INTR2_MASK ));
	if ((int)SiiRegRead(RX_A__INTR3  ) & (int)SiiRegRead( RX_A__INTR3_MASK))
		printk(KERN_ERR "I3 %02X %02X", (int)SiiRegRead( RX_A__INTR3  ), (int)SiiRegRead( RX_A__INTR3_MASK ));
	if ((int)SiiRegRead(RX_A__INTR4  ) & (int)SiiRegRead( RX_A__INTR4_MASK))
		printk(KERN_ERR "I4 %02X %02X", (int)SiiRegRead( RX_A__INTR4  ), (int)SiiRegRead( RX_A__INTR4_MASK ));
	if ((int)SiiRegRead(RX_A__INTR5  ) & (int)SiiRegRead( RX_A__INTR5_MASK))
		printk(KERN_ERR "I5 %02X %02X", (int)SiiRegRead( RX_A__INTR5  ), (int)SiiRegRead( RX_A__INTR5_MASK ));
	if ((int)SiiRegRead(RX_A__INTR6  ) & (int)SiiRegRead( RX_A__INTR6_MASK))
		printk(KERN_ERR "I6 %02X %02X", (int)SiiRegRead( RX_A__INTR6  ), (int)SiiRegRead( RX_A__INTR6_MASK ));
	if ((int)SiiRegRead(RX_A__INTR7  ) & (int)SiiRegRead( RX_A__INTR7_MASK))
		printk(KERN_ERR "I7 %02X %02X", (int)SiiRegRead( RX_A__INTR7  ), (int)SiiRegRead( RX_A__INTR7_MASK ));
	if ((int)SiiRegRead(RX_A__INTR8  ) & (int)SiiRegRead( RX_A__INTR8_MASK))
		printk(KERN_ERR "I8 %02X %02X", (int)SiiRegRead( RX_A__INTR8  ), (int)SiiRegRead( RX_A__INTR8_MASK ));
	if ((int)SiiRegRead( MHAWB_INT_STATUS_CBUS1  ) & (int)SiiRegRead( MHAWB_INT_STATUS_CBUS1 + 1  ))
		printk(KERN_ERR "CBUS1 %02X %02X", (int)SiiRegRead( MHAWB_INT_STATUS_CBUS1  ), (int)SiiRegRead( MHAWB_INT_STATUS_CBUS1 + 1  ));
	if ((int)SiiRegRead( MHAWB_INT_STATUS_CBUS2  ) & (int)SiiRegRead( MHAWB_INT_STATUS_CBUS2 + 1  ))
		printk(KERN_ERR "CBUS2 %02X %02X", (int)SiiRegRead( MHAWB_INT_STATUS_CBUS2  ), (int)SiiRegRead( MHAWB_INT_STATUS_CBUS2 + 1  ));

	if ((int)SiiRegRead( MHAWB_REG_GEN_2_WRITE_BURST_INTR  ) & (int)SiiRegRead(MHAWB_REG_GEN_2_WRITE_BURST_INTR + 1  ))
		printk(KERN_ERR "WBI %02X %02X", (int)SiiRegRead( MHAWB_REG_GEN_2_WRITE_BURST_INTR ),
							 (int)SiiRegRead( MHAWB_REG_GEN_2_WRITE_BURST_INTR + 1  ));

	if ((int)SiiRegRead( MHAWB_REG_GEN_2_WRITE_BURST_ERROR_INTR  ) & (int)SiiRegRead(MHAWB_REG_GEN_2_WRITE_BURST_ERROR_INTR + 1  ))
		printk(KERN_ERR "WBE %02X %02X", (int)SiiRegRead( MHAWB_REG_GEN_2_WRITE_BURST_ERROR_INTR ),
							 (int)SiiRegRead( MHAWB_REG_GEN_2_WRITE_BURST_ERROR_INTR + 1  ));

	if ((int)I2C_ReadByte(0x72, 0x3D) & I2C_ReadByte(0x72, 0x3C) )
		printk(KERN_ERR "3D %02X %02X", (int)I2C_ReadByte(0x72, 0x3D),
							 (int)I2C_ReadByte(0x72, 0x3C));

	if ((int)I2C_ReadByte(0xC0, 0x9A) & I2C_ReadByte(0xC0, 0x9A) )
		printk(KERN_ERR "XFR %02X %02X", (int)I2C_ReadByte(0xC0, 0x9A),
							 (int)I2C_ReadByte(0xC0, 0x9A));

	if ((int)I2C_ReadByte(0xC0, 0x9C) & I2C_ReadByte(0xC0, 0x9C) )
		printk(KERN_ERR "XFR %02X %02X", (int)I2C_ReadByte(0xC0, 0x9C),
							 (int)I2C_ReadByte(0xC0, 0x9C));


	if ((int)I2C_ReadByte(0xC0, 0x98) & I2C_ReadByte(0xC0, 0x98) )
		printk(KERN_ERR "DDC %02X %02X", (int)I2C_ReadByte(0xC0, 0x98),
							 (int)I2C_ReadByte(0xC0, 0x98));

	if ((int)I2C_ReadByte(0xC0, 0x9C) & I2C_ReadByte(0xC0, 0x9C) )
		printk(KERN_ERR "XFR %02X %02X", (int)I2C_ReadByte(0xC0, 0x9C),
							 (int)I2C_ReadByte(0xC0, 0x9C));

	if (SiiRegRead( MHAWB_WRITE_BURST_DATA_LEN) !=  g_mhawb.max_packet_length)
		printk(KERN_ERR "LEN %02X", (int)SiiRegRead( MHAWB_WRITE_BURST_DATA_LEN));

#endif
}
#if 0
static void MHAWB_XFIFO_Timer_Callback(void *pArg)
{
	printk(KERN_ERR "MHAWB XFIFO EMPTY timer expired.\n");
	if (g_mhawb.state == MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY_AS_PROXY) {
		//since MHAWB interrupts haven't occured; task other driver code to clear INTR
		SiiDrvHawbProcessInterrupts();		
		if  (g_mhawb.empty_levels == MHAWB_XFIFO_EMPTY_LEVELS_MAX) {
			//can't wait any longer; reset MHAWB  with full FIFO
			g_mhawb.state	   = MHAWB_STATE_RESET;
		} else {
			//reset timer to wait for interrupt
			SiiOsTimerStart(g_mhawb.timer_xfifoempty, MHAWB_TIMEOUT_FOR_XFIFO_EMPTY);
		}
	} else if (g_mhawb.state >= MHAWB_STATE_INIT)   //xyu: do not set to to reset if MHAWB is disabled
	{
		g_mhawb.state = MHAWB_STATE_RESET;
	}

	//2013-07-16 --
	//    if RESET pending, cycle the state machine to clear it.
	if (g_mhawb.state == MHAWB_STATE_RESET) {
		//SiiOsTimerStop(g_mhawb.timer_xfifoempty);
		if (!g_mhawb.hid_event_received) {
			printk(KERN_ERR "############ clear reset ############\n");
			g_mhawb.hid_event_received = 1;
			wake_up_interruptible(&g_mhawb.wait);
		}
	}

}
#endif
uint8_t mdt_burst_value_update_local(struct hid_usage *usage, __s32 value) {

	struct mhawb_fifo_level_data_t *hid_event = &g_mhawb.hid_event;
	struct mdt_burst_01_t *mdt_burst 	  = (struct mdt_burst_01_t *)hid_event->adopter_id;
	union mdt_event_t     *mdt_packet	  = &mdt_burst->events[0];
	uint8_t		       mdt_packet_type;

	//printk(KERN_ERR "mdt_s\t%x\t%x\t%x\t%x",(int)hid_event->length,
	//					(int)mdt_packet->event_keyboard.header.isHID,
	//				 	(int)mdt_packet->event_mouse.header.isKeyboard,
	//				 	(int)mdt_packet->event_mouse.header.isNotMouse);

	if (g_mhawb.state < MHAWB_STATE_WAIT_FOR_DATA) {
		//printk(KERN_ERR "                 - mhawb_not_ready ------------");
		return MHAWB_SENDAPI_ERROR_UNKNOWNMHLERROR;
	}

	if (down_trylock(&g_api_lock)) {
		printk(KERN_ERR " mdt_burst_value_update failed to lock");
		//up(&g_mhawb_lock);
		return 0;
	}

	if (g_mhawb.next_rx_level->fields.level.length != 0) {	
		printk(KERN_ERR "-#-#-#-#-#-# mhawb_local_fifo_full #-#-#-#-#-#-\n");	
		up(&g_api_lock);
		return MHAWB_SENDAPI_ERROR_UNKNOWNMHLERROR;
	}


	if (hid_event->length >= MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA) {
		//printk(KERN_ERR "packet exceeds allowed length\n");	
		up(&g_api_lock);
		return MHAWB_UPDATEAPI_ERROR_EXCEEDSRANGE;
	}
        switch (usage->type) {
		case EV_KEY:
			if (usage->code & BTN_MOUSE)
				mdt_packet_type = DEV_TYPE_MOUSE;
			else if (usage->code & BTN_MISC)
				mdt_packet_type = DEV_TYPE_GAME;
			else
				mdt_packet_type = DEV_TYPE_KEYBOARD;
			// add directional pad
			break;
		case EV_REL:
			mdt_packet_type = DEV_TYPE_MOUSE;
			break;
		case EV_ABS:			
			mdt_packet_type = DEV_TYPE_TOUCH;
			break;
		default:
			up(&g_api_lock);
			return MHAWB_UPDATEAPI_ERROR_UNSUPPORTEDFIELD;
	};


	switch (mdt_packet_type) {
		case DEV_TYPE_KEYBOARD:
			if ((hid_event->length == 0) &&
				(!mdt_packet->event_keyboard.header.isHID)) {
				hid_event->length = MDT_BYTE_LENGTH_HEADER;
				mdt_packet->event_keyboard.header.isHID 		= 1;
				mdt_packet->event_keyboard.header.isKeyboard		= 1;
				//mdt_packet->event_cursor.header.touch.isNotMouse	= 0;
			} else if ((hid_event->length == MDT_BYTE_LENGTH_MAX_PACKET) &&
				(!mdt_burst->events[1].event_keyboard.header.isHID)) {
				mdt_packet	 = &mdt_burst->events[1];
				hid_event->length = (MDT_BYTE_LENGTH_HEADER + MDT_BYTE_LENGTH_MAX_PACKET);
				mdt_packet->event_keyboard.header.isHID 		= 1;
				mdt_packet->event_keyboard.header.isKeyboard		= 1;
				//mdt_packet->event_cursor.header.touch.isNotMouse	= 0;
			} else if (hid_event->length > MDT_BYTE_LENGTH_MAX_PACKET)
				mdt_packet	 = &mdt_burst->events[1];

			printk(KERN_ERR "mdt_k\t%x\t%x\t%x\t%x",(int)hid_event->length,
								(int)mdt_packet->event_keyboard.header.isHID,
							 	(int)mdt_packet->event_keyboard.header.isKeyboard,
							 	(int)mdt_packet->event_mouse.header.isNotMouse);


			//  current MDT packet in BURST is already a keyboard packet
			if  ((mdt_packet->header.isKeyboard == 1) &&				
				(mdt_packet->event_cursor.header.touch.isNotMouse == 0)) {

				if (mdt_packet	== &mdt_burst->events[0]) {
					if (hid_event->length > MDT_BYTE_LENGTH_KEYBOARD) {
						hid_event->length = MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA + 1;
						up(&g_api_lock);
						return MHAWB_UPDATEAPI_ERROR_EXCEEDSRANGE;
					}
				} else {
					if (hid_event->length > (MDT_BYTE_LENGTH_KEYBOARD + MDT_BYTE_LENGTH_MAX_PACKET)) {
						hid_event->length = MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA + 1;
						up(&g_api_lock);
						return MHAWB_UPDATEAPI_ERROR_EXCEEDSRANGE;
					}
				}				

				// room for more key codes in the packet
				if (value) {
					*((uint8_t *)mdt_burst->events + hid_event->length) = (uint8_t)usage->code;
					hid_event->length++;
				}

			//  current packet already written but, isn't keyboard and
			// 	current packet is the first packet and
			//	second packet uninitialized
			} else if (hid_event->length < MDT_BYTE_LENGTH_MAX_PACKET) {

				mdt_packet	 = &mdt_burst->events[1];
				hid_event->length = (MDT_BYTE_LENGTH_HEADER + MDT_BYTE_LENGTH_MAX_PACKET);
				mdt_packet->event_keyboard.header.isHID 		= 1;
				mdt_packet->event_keyboard.header.isKeyboard		= 1;

				// room for more key codes in the packet
				if (value) {
					*((uint8_t *)mdt_burst->events + hid_event->length) = (uint8_t)usage->code;
					hid_event->length++;
				}
			} else {
				hid_event->length = MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA + 1;
				up(&g_api_lock);
				return MHAWB_UPDATEAPI_ERROR_EXCEEDSRANGE;
			}

			break;
		case DEV_TYPE_MOUSE:
			if ((hid_event->length == 0) &&
				(!mdt_packet->event_keyboard.header.isHID)) {
				hid_event->length = MDT_BYTE_LENGTH_HEADER;
				mdt_packet->event_mouse.header.isHID 			= 1;
				//mdt_packet->event_mouse.header.isKeyboard		= 0;
				//mdt_packet->event_mouse.header.touch.isNotMouse 	= 0;
			} else if ((hid_event->length == MDT_BYTE_LENGTH_MAX_PACKET) &&
				(!mdt_burst->events[1].event_keyboard.header.isHID)) {
				mdt_packet	 = &mdt_burst->events[1];
				hid_event->length = (MDT_BYTE_LENGTH_HEADER + MDT_BYTE_LENGTH_MAX_PACKET);
				mdt_packet->event_mouse.header.isHID 			= 1;
				//mdt_packet->event_mouse.header.isKeyboard		= 0;
				//mdt_packet->event_mouse.header.touch.isNotMouse 	= 0;
			} else if (hid_event->length > MDT_BYTE_LENGTH_MAX_PACKET) {
				mdt_packet	 = &mdt_burst->events[1];
			}

			//printk(KERN_ERR "mdt_m\t%x\t%x\t%x\t%x",(int)hid_event->length,
			//					(int)mdt_packet->event_keyboard.header.isHID,
			//				 	(int)mdt_packet->event_mouse.header.isKeyboard,
			//				 	(int)mdt_packet->event_mouse.header.isNotMouse);

			//  current MDT packet in BURST is already a mouse packet
			if  ((mdt_packet->event_mouse.header.isKeyboard == 0) && 
				(mdt_packet->event_mouse.header.isNotMouse == 0)) {

				if (mdt_packet	== &mdt_burst->events[0]) {
					if (hid_event->length > MDT_BYTE_LENGTH_MOUSE) {

						mdt_packet	  = &mdt_burst->events[1];
						hid_event->length = (MDT_BYTE_LENGTH_HEADER + MDT_BYTE_LENGTH_MAX_PACKET);
						mdt_packet->event_mouse.header.isHID			= 1;
						//mdt_packet->event_mouse.header.isKeyboard		= 0;
						//mdt_packet->event_mouse.header.touch.isNotMouse 	= 0;
					}
				} else {
					if (hid_event->length >= (MDT_BYTE_LENGTH_HEADER + MDT_BYTE_LENGTH_MOUSE + MDT_BYTE_LENGTH_MAX_PACKET)) {
						hid_event->length = MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA + 1;
						up(&g_api_lock);
						return MHAWB_UPDATEAPI_ERROR_EXCEEDSRANGE;
					}
				}

				if (usage->code < REL_MAX)	//since BTN is in header, only increment REL change
					hid_event->length++;

				switch (usage->code) {
					case BTN_LEFT:
						if (value)
							mdt_packet->event_mouse.header.button |= MDT_BUTTON_LEFT;
						else
							mdt_packet->event_mouse.header.button &= ~MDT_BUTTON_LEFT;
						break;
					case BTN_MIDDLE:
						if (value)
							mdt_packet->event_mouse.header.button |= MDT_BUTTON_MIDDLE;
						else
							mdt_packet->event_mouse.header.button &= ~MDT_BUTTON_MIDDLE;
						break;
					case BTN_RIGHT:
						if (value)
							mdt_packet->event_mouse.header.button |= MDT_BUTTON_RIGHT;
						else
							mdt_packet->event_mouse.header.button &= ~MDT_BUTTON_RIGHT;
						break;
					case REL_X:	mdt_packet->event_mouse.body.XYZ.x_byteLen = (uint8_t)value;
						break;
					case REL_Y:	mdt_packet->event_mouse.body.XYZ.y_byteLen = (uint8_t)value;
						break;
					case REL_WHEEL:
					case REL_Z:	mdt_packet->event_mouse.body.XYZ.z_byteLen = (uint8_t)value;
						break;
					default: 
						up(&g_api_lock);
						return MHAWB_UPDATEAPI_ERROR_UNSUPPORTEDFIELD;
				}

			//  current packet already written but, isn't keyboard and
			// 	current packet is the first packet and
			//	second packet uninitialized
			} else if (hid_event->length < MDT_BYTE_LENGTH_MAX_PACKET) {

				mdt_packet	 = &mdt_burst->events[1];
				hid_event->length = (MDT_BYTE_LENGTH_HEADER + MDT_BYTE_LENGTH_MAX_PACKET);
				mdt_packet->event_mouse.header.isHID			= 1;
				//mdt_packet->event_mouse.header.isKeyboard		= 0;
				//mdt_packet->event_mouse.header.touch.isNotMouse 	= 0;
				
				if (usage->code < REL_MAX)	//since BTN is in header, only increment REL change
					hid_event->length++;

				switch (usage->code) {
					case BTN_LEFT:
						if (value)
							mdt_packet->event_mouse.header.button |= MDT_BUTTON_LEFT;
						else
							mdt_packet->event_mouse.header.button &= ~MDT_BUTTON_LEFT;
						break;
					case BTN_MIDDLE:
						if (value)
							mdt_packet->event_mouse.header.button |= MDT_BUTTON_MIDDLE;
						else
							mdt_packet->event_mouse.header.button &= ~MDT_BUTTON_MIDDLE;
						break;
					case BTN_RIGHT:
						if (value)
							mdt_packet->event_mouse.header.button |= MDT_BUTTON_RIGHT;
						else
							mdt_packet->event_mouse.header.button &= ~MDT_BUTTON_RIGHT;
						break;
					case REL_X:	mdt_packet->event_mouse.body.XYZ.x_byteLen = (uint8_t)value;
						break;
					case REL_Y:	mdt_packet->event_mouse.body.XYZ.y_byteLen = (uint8_t)value;
						break;
					case REL_WHEEL:
					case REL_Z:	mdt_packet->event_mouse.body.XYZ.z_byteLen = (uint8_t)value;
						break;
					default: 
						up(&g_api_lock);
						return MHAWB_UPDATEAPI_ERROR_UNSUPPORTEDFIELD;
				}
			} else {
				hid_event->length = MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA + 1;
				up(&g_api_lock);
				return MHAWB_UPDATEAPI_ERROR_EXCEEDSRANGE;
			}
			break;
		case EV_ABS:
			break;
		default:
			up(&g_api_lock);
			return MHAWB_UPDATEAPI_ERROR_UNSUPPORTEDFIELD;
	};	

	//printk(KERN_ERR "mdt_e\t%x\t%x\t%x\t%x",(int)hid_event->length,
	//					(int)mdt_packet->event_keyboard.header.isHID,
	//				 	(int)mdt_packet->event_mouse.header.isKeyboard,
	//				 	(int)mdt_packet->event_mouse.header.isNotMouse);

	
	up(&g_api_lock);	
	return MHAWB_UPDATEAPI_SUCCESS;
			
}
//EXPORT_SYMBOL(mdt_burst_value_update);

uint8_t enqueue_packet(union mdt_event_t *packet_payload, uint8_t *packet_length) {
	
	//int i;

	//mhawb_init is a prerequisite
	if (g_mhawb.state == MHAWB_STATE_UNINITIALIZED) {
		printk(KERN_ERR "                 - mhawb_uninitialized ############\n");
		return MHAWB_EVENT_HANDLER_FAILED;
	}
#if 0
	if (g_mhawb.state == MHAWB_STATE_DISABLED || g_mhawb.state == MHAWB_STATE_TAKEOVER) {
		printk(KERN_ERR "############ mhawb disabled ############\n");
	
		//2013-07-16 ---
		//
		//DISABLE should trigger initialization instead of reset
		//
		// g_mhawb.state = MHAWB_STATE_RESET;
		// xyu: MHAWB will be enable by main CBUS handler, do not change state here
		//g_mhawb.state = MHAWB_STATE_INIT;
		if (!g_mhawb.hid_event_received) {
			printk(KERN_ERR "############ activate delay ############\n");
			g_mhawb.hid_event_received = 1;
			wake_up_interruptible(&g_mhawb.wait);
		}
		return MHAWB_EVENT_HANDLER_FAILED;
	}
#endif
	//printk(KERN_ERR "------------ mhawb_a ------------\n");

	if (g_mhawb.state < MHAWB_STATE_WAIT_FOR_DATA)	{		//init, disable, or reset
		//printk(KERN_ERR "                 - mhawb_error_state ############\n");
#if 0
		memset(packet_payload->bytes, 0, *packet_length);
		*packet_length = 0;
		if (!g_mhawb.hid_event_received) {
			g_mhawb.hid_event_received = 1;
			wake_up_interruptible(&g_mhawb.wait);
		}
#endif
		return MHAWB_EVENT_HANDLER_FAILED;
	}
	
	//printk(KERN_ERR "------------ mhawb_b ------------\n");

	// LOCAL buffer is full; DROP PACKET
	if (g_mhawb.next_rx_level->fields.level.length != 0) {	
		printk(KERN_ERR "                 - mhawb_local_fifo_full ############\n");
		#ifndef FLOOD_TEST
		memset(packet_payload->bytes, 0, *packet_length);		
		*packet_length = 0;
		#endif
		
		//2013-07-16 --
		//	no need to force delayed work; wait for interrupt or timeout
		//g_mhawb.hid_event_received = 1;
	
		// something's wrong; MHAWB state machine should have caught this problem
		//if (g_mhawb.state != MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY) {
		//	printk(KERN_ERR "############ mhawb_unexpected_state %d ############\n", g_mhawb.state);
			//g_mhawb.state = MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY;
			//SiiOsTimerStart(g_mhawb.timer_xfifoempty, MHAWB_TIMEOUT_FOR_XFIFO_EMPTY);
		//}

		//2013-07-30-MIK; why is this needed here ????

		//2013-07-30, need delayed work if possible
		//if (!g_mhawb.hid_event_received) {
		//	g_mhawb.hid_event_received = 1;
		//	wake_up_interruptible(&g_mhawb.wait);
		//}

		//2013-07-16 --
		//	no need to force delayed work; wait for interrupt or timeout
		//wake_up_interruptible(&g_mhawb.wait);
		return MHAWB_SYNCAPI_ERROR_PRIORPENDING;
	}

	//field error; reset
	if (*packet_length == (MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA + 1)) {
		printk(KERN_ERR "                 - mhawb_packet_error ############\n");
		#ifndef FLOOD_TEST
		memset(packet_payload->bytes, 0, *packet_length);
		*packet_length = 0;
		#endif
		if (!g_mhawb.hid_event_received){
			g_mhawb.hid_event_received = 1;
			wake_up_interruptible(&g_mhawb.wait);
		}
		return MHAWB_SYNCAPI_ERROR_INVALIDDATA;
	}

	//printk(KERN_ERR "                 - mhawb_c ------------\n");

	if (*packet_length == 0) {				// nothing to sync
		printk(KERN_ERR "                 - mhawb_empty_packet ############\n");
		#ifndef FLOOD_TEST
		memset(packet_payload->bytes, 0, *packet_length);
		*packet_length = 0;
		#endif
		if (!g_mhawb.hid_event_received){
			g_mhawb.hid_event_received = 1;
			wake_up_interruptible(&g_mhawb.wait);
		}
		return MHAWB_SYNCAPI_ERROR_INVALIDDATA;
	}

	//printk(KERN_ERR "                 - mhawb_d ------------\n");

	memcpy(g_mhawb.next_rx_level->fields.level.burst_bytes, packet_payload->bytes, *packet_length);

	//printk(KERN_ERR "                 - mhawb_e ------------\n");

	//for (i=0; i<=*packet_length; i++) {
	//	printk( KERN_ERR "mhawb byte %x=%x",i, g_mhawb.next_rx_level->fields.level.burst_bytes[i]);
	//}

	//This length also serves as flag to trigger MDT
	g_mhawb.next_rx_level->fields.level.length = *packet_length;
										
	//It would be easy to copy the length at the head of the data to
	//   simplify future processing but, the future code might be 
	//	 confusing to someone that missed the detail here.
	g_mhawb.next_rx_level = g_mhawb.next_rx_level->fields.next;

	//printk(KERN_ERR "                 - mhawb_f ------------\n");
	
	
	#ifndef FLOOD_TEST		
	memset(packet_payload->bytes, 0, (*packet_length));
	*packet_length = 0;
	#endif

	//i = queue_work(mdt_hid_bridge_wq, &mhawb_work);
	if (!g_mhawb.hid_event_received){
		g_mhawb.hid_event_received = 1;
		wake_up_interruptible(&g_mhawb.wait);
	}
	return MHAWB_SYNCAPI_SUCCESS;
}

void update_position(uint16_t *origin, int16_t delta) {

	if (delta < 0) {
		delta *= -1;
		if (delta > *origin)
			*origin = 0;
		else
			*origin -= delta;
	} else {
		*origin += delta;
		if (*origin > XY_MAX)
			*origin = XY_MAX;
	}	
}

uint8_t mdt_burst_simulate_touch(struct mdt_cursor_mouse_t *mousePacket) {

	int ret;
	struct mdt_cursor_other_t	mdt_touch;

	//printk(KERN_ERR "mouse pkt x:%x y:%x btn:%x"	,(int)(mousePacket->body.XYZ.x_byteLen)
	//						,(int)(mousePacket->body.XYZ.y_byteLen)
	//
	//						,(int)(mousePacket->header.button));

	//1. There's just no reason to lock here. The data is read out of a global into a local
	//2. The call to this function is already syncrhonized by API lock
	//if (mousePacket->header.button == 0)
	//	down(&g_mhawb_lock);
	//elseif (down_trylock(&g_mhawb_lock)) {
	//	return 0;
	//}

	update_position(&g_mhawb.touch_x, mousePacket->body.XYZ.x_byteLen);
	update_position(&g_mhawb.touch_y, mousePacket->body.XYZ.y_byteLen);

	//printk(KERN_ERR "              x: %02x y: %02x", (int)g_mhawb.touch_x, (int)g_mhawb.touch_y);

	mdt_touch.header.touch.isHID 		= 1;
	mdt_touch.header.touch.isPortB		= 0;
	mdt_touch.header.touch.isKeyboard	= 0;
	mdt_touch.header.touch.isNotLast	= 0;
	mdt_touch.header.touch.isNotMouse	= 1;
	// MDT limits support to the first 4 contacts
	mdt_touch.header.touch.contactID	= 1;
	mdt_touch.header.touch.isTouched	= (mousePacket->header.button != 0);

	memcpy(mdt_touch.body.touchXYZ.xy_wordLen[MDT_TOUCH_X], &g_mhawb.touch_x, 2);
	memcpy(mdt_touch.body.touchXYZ.xy_wordLen[MDT_TOUCH_Y], &g_mhawb.touch_y, 2);
	//mdt_touch.body.touchXYZ.xy_wordLen[MDT_TOUCH_X][MDT_TOUCH_X_LOW]	= (g_mhawb.touch_x & 0xff);
	//mdt_touch.body.touchXYZ.xy_wordLen[MDT_TOUCH_X][MDT_TOUCH_X_HIGH]	= (g_mhawb.touch_x >> 8) & 0xff;
	//mdt_touch.body.touchXYZ.xy_wordLen[MDT_TOUCH_Y][MDT_TOUCH_Y_LOW]	= (g_mhawb.touch_y & 0xff);
	//mdt_touch.body.touchXYZ.xy_wordLen[MDT_TOUCH_Y][MDT_TOUCH_Y_HIGH]	= (g_mhawb.touch_y >> 8) & 0xff;

	//up(&g_mhawb_lock);

	// this is short call; actual work done by a seperate MDT thread
	
	ret = mdt_burst_send_touch(&mdt_touch);
	#ifdef FLOOD_TEST
	ret = mdt_burst_send_touch(&mdt_touch);
	ret = mdt_burst_send_touch(&mdt_touch);
	ret = mdt_burst_send_touch(&mdt_touch);
	#endif
	if ((ret & MDT_SYNC_ERROR_FLAG_MASK) == MDT_SYNC_ERROR_FLAG) {
		pr_info("[TSP4MDT] ERROR %02x\n", ret);
		return (((ret & MDT_SYNC_ERROR_VALUE_MASK) - MDT_SYNC_ERROR_OFFSET) | MDT_SEND_ERROR_FLAG);
	} else
		return MHAWB_SENDAPI_SUCCESS;

		
		

	return ret;
}


//#define SIMULATE_TOUCH
uint8_t mdt_burst_sync_and_send_local(void) {

	uint8_t ret;	

	if (down_trylock(&g_api_lock)) {
		printk(KERN_ERR " mdt_burst_sync_and_send_local failed to lock");
		return 0;
	}

#ifdef SIMULATE_TOUCH
	ret = mdt_burst_simulate_touch((struct mdt_cursor_mouse_t *)g_mhawb.hid_event.burst_bytes);	
	g_mhawb.hid_event.length = 0;
	up(&g_api_lock);
	return ret;
#else
	
	//printk(KERN_ERR "mouse pkt x:%x y:%x btn:%x"	,((struct mdt_cursor_mouse_t *)(g_mhawb.hid_event.burst_bytes))->body.XYZ.x_byteLen
	//						,((struct mdt_cursor_mouse_t *)(g_mhawb.hid_event.burst_bytes))->body.XYZ.y_byteLen
	//
    #ifdef FLOOD_TEST
    	g_mhawb.hid_event.length = MDT_MOUSE_PACKET_LENGTH + 1;
    	enqueue_packet((union mdt_event_t*)g_mhawb.hid_event.burst_bytes, &g_mhawb.hid_event.length);
    	enqueue_packet((union mdt_event_t*)g_mhawb.hid_event.burst_bytes, &g_mhawb.hid_event.length);
    #endif
	ret = enqueue_packet((union mdt_event_t*)g_mhawb.hid_event.burst_bytes, &g_mhawb.hid_event.length);
	up(&g_api_lock);
	return ret;
#endif
}
//EXPORT_SYMBOL(mdt_burst_sync_and_send);

uint8_t mdt_burst_send_mouse(struct mdt_cursor_mouse_t *mousePacket) {

	uint8_t length = MDT_MOUSE_PACKET_LENGTH + 1;
	uint8_t ret;
	//
	// Don't overwrite a pending packet. Rather than over write a prior packet, drop the new packet.
	//    It may be preferable to drop earlier packets. For now, drop the new one.
	if ((mousePacket->header.isNotMouse) 	||
		 (mousePacket->header.isKeyboard)	||
		!(mousePacket->header.isHID))
		return MHAWB_SENDAPI_ERROR_INVALIDTOUCHDATA;

	ret = enqueue_packet((union mdt_event_t*)mousePacket, &length);

	if ((ret & MDT_SYNC_ERROR_FLAG_MASK) == MDT_SYNC_ERROR_FLAG)
		return ((ret & MDT_SYNC_ERROR_VALUE_MASK) - MDT_SYNC_ERROR_OFFSET);
	else
		return MHAWB_SENDAPI_SUCCESS;
}
EXPORT_SYMBOL(mdt_burst_send_mouse);

uint8_t mdt_burst_send_touch(struct mdt_cursor_other_t *touchPacket) {

	uint8_t length = MDT_TOUCH_PACKET_LENGTH + 1;
	uint8_t ret;
	
	//		
	// Qualify packet
	//					
	if (!(touchPacket->header.touch.isNotMouse) 	||
		 (touchPacket->header.touch.isKeyboard)	||
		!(touchPacket->header.touch.isHID)	||
		 (touchPacket->body.touchXYZ.xy_wordLen[MDT_TOUCH_X][MDT_TOUCH_X_HIGH] > 0x07) || // must be <= 1920
		 (touchPacket->body.touchXYZ.xy_wordLen[MDT_TOUCH_Y][MDT_TOUCH_Y_HIGH] > 0x07))   // must be <= 1920
	{
		printk(KERN_ERR "ERROR. Poorly formatted packet.\n");
		if (!touchPacket->header.touch.isHID)
				printk(KERN_ERR "   isHID incorrectly set to 0\n");
		if (!touchPacket->header.touch.isNotMouse)
				printk(KERN_ERR "   isNotMouse incorrectly set to 0\n");
		if (touchPacket->header.touch.isKeyboard)
				printk(KERN_ERR "   isKeyboard incorrectly set to 1\n");
		if (touchPacket->body.touchXYZ.xy_wordLen[MDT_TOUCH_X][MDT_TOUCH_X_HIGH] > 0x07)
				printk(KERN_ERR "   x coordinate greater than 1920\n");
		if (touchPacket->body.touchXYZ.xy_wordLen[MDT_TOUCH_Y][MDT_TOUCH_Y_HIGH] > 0x07)
				printk(KERN_ERR "   y coordinate greater than 1920\n");
		return MHAWB_SENDAPI_ERROR_INVALIDTOUCHDATA;
	}

	ret = enqueue_packet((union mdt_event_t*)touchPacket, &length);


	if ((ret & MDT_SYNC_ERROR_FLAG_MASK) == MDT_SYNC_ERROR_FLAG)
		return (((ret & MDT_SYNC_ERROR_VALUE_MASK) - MDT_SYNC_ERROR_OFFSET) | MDT_SEND_ERROR_FLAG);

	else
		return MHAWB_SENDAPI_SUCCESS;
}
EXPORT_SYMBOL(mdt_burst_send_touch);


static void delayed_mhawb_do_work_func(struct work_struct *p) {
	
	//printk(KERN_ERR "                 - mhawb_delayed_func ------\n");

	if (wait_event_interruptible(g_mhawb.wait,
		((g_mhawb.hid_event_received == 1)
		//2013-06-16 -- this will create unnecessary work load 
		/*||
		 (g_mhawb.empty_levels != MHAWB_XFIFO_EMPTY_LEVELS_MAX)*/))) {
		printk(KERN_ERR "              ++ mhawb_wait_failed -------\n");
		queue_work(mdt_hid_bridge_wq, &mhawb_work);
		return;
	}

	//printk(KERN_ERR "MHAWB_+ s%x l%x l%x s%x x%x\n", (int)g_mhawb.state, (int)g_mhawb.empty_levels,
	//					(int)g_mhawb.hid_event.length, (int)SiiCbusRequestStatus(),
	//					(int)SiiMhlRxCbusConnected());
	//xyu: only do following work when mdt_burst_sync_and_send is available
	if (mdt_burst_sync_and_send)
	{
    	//print_intr_status();

    	//printk(KERN_ERR "------------ mhawb_delayed_start -----\n");	
    	if (HalAcquireIsrLock() == HAL_RET_SUCCESS) {
    		//printk(KERN_ERR "------------ mhawb_delayed_cont -----\n");
    		// 2013-07-16 --
    		//	this while loop means that register will be polled once for expediency
    		while (mhawb_do_work() == MHAWB_EVENT_HANDLER_WAITING);
    		//only clear do_work request from API client if work is done
    		g_mhawb.hid_event_received = 0;
    		//printk(KERN_ERR "------------ mhawb_delayed_end -------\n");
    		HalReleaseIsrLock();
    	}
    	queue_work(mdt_hid_bridge_wq, &mhawb_work);
	}
}

enum mhawb_return_e mhawb_init( void ) {

	// activate MDT state machine here
	if (g_mhawb.state == MHAWB_STATE_UNINITIALIZED) {
		g_mhawb.state = MHAWB_STATE_DISABLED;
		
//	        SiiOsTimerCreate("MHAWB XFIFO    Timeout", MHAWB_XFIFO_Timer_Callback,   NULL, &g_mhawb.timer_xfifoempty);

		g_mhawb.hid_event_received = 0;
		mdt_hid_bridge_wq = create_singlethread_workqueue("mdt_hid_bridge_work");

		if (mdt_hid_bridge_wq == NULL) {
			printk(KERN_ERR "FAILURE: mhawb_workqueue_create");
			return MHAWB_SYNCAPI_ERROR_UNKNOWNERROR;
		}

		INIT_WORK(&mhawb_work, delayed_mhawb_do_work_func);
		init_waitqueue_head(&g_mhawb.wait);

		queue_work(mdt_hid_bridge_wq, &mhawb_work);

        mdt_burst_value_update = mdt_burst_value_update_local;
        mdt_burst_sync_and_send = mdt_burst_sync_and_send_local;

		printk(KERN_ERR "                 - SUCCESS mhawb_init ------------\n");
		return MHAWB_SYNCAPI_SUCCESS;
	} else {
		printk(KERN_ERR "                 - FAILURE mhawb_invalid ------------");
		return MHAWB_SYNCAPI_ERROR_INVALIDDATA;
	}

}

enum mhawb_return_e mhawb_destroy( void )
{
    if (mdt_burst_sync_and_send)
    {
        mdt_burst_value_update = NULL;
        mdt_burst_sync_and_send = NULL;

        // wake_up work queue and let it finish its work
        g_mhawb.hid_event_received = 1;
        wake_up_interruptible(&g_mhawb.wait);

        flush_workqueue(mdt_hid_bridge_wq);
        destroy_workqueue(mdt_hid_bridge_wq);
        mdt_hid_bridge_wq = NULL;
    }
    return MHAWB_EVENT_HANDLER_SUCCESS;
}

enum mhawb_return_e mhawb_do_isr_work( void ) {

    
    SiiRegWrite(MHAWB_REG_GEN_2_WRITE_BURST_INTR, MHAWB_GEN_2_WRITE_BURST_XFIFO_FULL);
    if (is_interrupt_asserted())
    {
	    uint8_t	err;
        err = SiiRegRead( MHAWB_REG_GEN_2_WRITE_BURST_ERROR_INTR );
        if (err)
        {
            printk(KERN_ERR "MHAWB ERROR. %02x\n", (int)err);
            if (err & (MHAWB_GEN_2_WRITE_BURST_XSM_RCVD_ABORTPKT | MHAWB_GEN_2_WRITE_BURST_RSM_RCVD_ABORTPKT))
            {
                //g_mhawb.state = MHAWB_STATE_RESET;
                SiiCbusAbortTimerStart();
            }
            else if (err & (MHAWB_GEN_2_WRITE_BURST_XSM_ERROR | MHAWB_GEN_2_WRITE_BURST_RSM_ERROR))
            {
                g_mhawb.state = MHAWB_STATE_RESET;
            }
			SiiRegWrite( MHAWB_REG_GEN_2_WRITE_BURST_ERROR_INTR, err);
        }

        if (is_interrupt_asserted())
        {
            // There's interrupts other than MDT, let man isr handler to run the remain tasks.
            return MHAWB_EVENT_HANDLER_FAILED;
        }
    }
    else
    {
        printk(KERN_ERR "                MHAWB_XFIFO FULL\n");
        // XFIFO is full, drop the current content to let XFIFO full assert again.
        SiiRegBitsSet(MHAWB_REG_GEN_2_WRITE_BURST_XMIT_CTRL, MHAWB_GEN_2_WRITE_BURST_XFIFO_CLR_CUR, 1);
    }
    return MHAWB_EVENT_HANDLER_SUCCESS;
}

enum mhawb_return_e mhawb_do_work( void ) {
		
	enum mhawb_return_e	ret    = MHAWB_EVENT_HANDLER_SUCCESS;
	
	//This assignment is temporary. INTR status not needed in every state.
	uint8_t	volatile g_intr = is_interrupt_asserted();
	uint8_t	reg_val;
	uint8_t	level_index;			
	uint8_t	MHAWB_MEMORY	*buffer_start;
	uint8_t	buffer_length;

	union mhawb_circular_level_buffer_t	MHAWB_MEMORY *current_level;

	//mhawb_init is a prerequisite
	if (g_mhawb.state == MHAWB_STATE_UNINITIALIZED) {
		printk(KERN_ERR "                 - mhawb_uninitialized ------------\n");
		mhawb_init();
		return MHAWB_EVENT_HANDLER_FAILED;
	}

	if (g_mhawb.state == MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY_AS_PROXY)
		return 	MHAWB_EVENT_HANDLER_FAILED;

#if 0   //xyu: Disable & Enable HAWB has been moved to CBUS main loop
	// Emergency override. Other parts of firmware doing HAWB work. Disable MDT.
	#ifdef __KERNEL__
	if ((SiiCbusRequestStatus() == CBUS_REQ_PENDING) ||
	#else
	if ((( hawb3DXmitCurrentIndex != 0 ) && ( g_mhawb.state != MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY_AS_PROXY)) ||
	#endif
		 ( !SiiMhlRxCbusConnected() ) || 
		 ( g_mhawb.state == MHAWB_STATE_DISABLED )){		
		
		if (g_mhawb.state != MHAWB_STATE_DISABLED) {
			#ifndef __KERNEL__
			hawb3DXfifoEmptyFlag = true;
			#endif
			SiiDrvHawbInit();
			SiiDrvHawbEnable(false);	
			SiiOsTimerStop(g_mhawb.timer_xfifoempty);		
			g_mhawb.state = MHAWB_STATE_DISABLED;
		} 
		#ifdef __KERNEL__
			else {
				if ((SiiCbusRequestStatus() == CBUS_REQ_PENDING) &&
					(SiiMhlRxCbusConnected())) {
					SiiOsTimerStop(g_mhawb.timer_xfifoempty);
					g_mhawb.state = MHAWB_STATE_INIT;
				}
			}
		#endif

		//printk(KERN_ERR "MHAWB_! i:%x s:%x lev:%x len:%x stat:%x cn:%x\n", (int)g_intr, (int)g_mhawb.state, (int)g_mhawb.empty_levels,
		//				(int)g_mhawb.hid_event.length, (int)SiiCbusRequestStatus(),
		//				(int)SiiMhlRxCbusConnected());
		return MHAWB_EVENT_HANDLER_FAILED;
	} 
#endif	
//	printk(KERN_ERR "              MHAWB_S t:%x i:%x s:%x lev:%x len:%x stat:%x cn:%x\n", (int)g_mhawb.timer_running, (int)g_intr, (int)g_mhawb.state, 
//						(int)g_mhawb.empty_levels,
//						(int)g_mhawb.hid_event.length, (int)SiiCbusRequestStatus(),
//						(int)SiiMhlRxCbusConnected());

	// Polled part of the state machine supports initiation of init and tranmission.
	switch (g_mhawb.state) {		
		case MHAWB_STATE_INIT:
		case MHAWB_STATE_RESET:
			if (SiiCbusAbortStateGet())
			{
				//xyu: do not do remain work if in abort state.
				ret = MHAWB_EVENT_HANDLER_FAILED;
				break;
			}

//			if (g_mhawb.state != MHAWB_STATE_INIT)
//				g_mhawb.state	= MHAWB_STATE_WAIT_FOR_DATA;

			memset(&g_mhawb.circular_buffer, 0, sizeof(g_mhawb.circular_buffer));
			
			//less code to initialize 4 elements than write a loop
			g_mhawb.circular_buffer[0].fields.next = &g_mhawb.circular_buffer[1];
			g_mhawb.circular_buffer[1].fields.next = &g_mhawb.circular_buffer[2];
			g_mhawb.circular_buffer[2].fields.next = &g_mhawb.circular_buffer[3];
			g_mhawb.circular_buffer[3].fields.next = &g_mhawb.circular_buffer[0];
		
			g_mhawb.next_rx_level = &g_mhawb.circular_buffer[0];
		
			for (level_index = 0; level_index < MHAWB_XFIFO_EMPTY_LEVELS_MAX; level_index++)
				g_mhawb.circular_buffer[level_index].fields.level.length = 0;

			g_mhawb.peer_adopter_id[0] = SiiCbusRemoteDcapGet(0x03);
			g_mhawb.peer_adopter_id[1] = SiiCbusRemoteDcapGet(0x04);
									
			//g_mhawb.empty_levels = MHAWB_XFIFO_EMPTY_LEVELS_MAX;
			
			current_level = g_mhawb.next_rx_level;
			for ( g_mhawb.next_tx_level = current_level->fields.next;
						(g_mhawb.next_tx_level != current_level) && (g_mhawb.next_tx_level->fields.level.length == 0);
						g_mhawb.next_tx_level = g_mhawb.next_tx_level->fields.next);

            //xyu: Disable & Enable HAWB has been moved to CBUS main loop
			//SiiDrvHawbInit();
			//This call will generate an interrupt. Isr must be locked prior to this call!
			//SiiDrvHawbEnable(true);

			SiiRegBitsSet(MHAWB_REG_GEN_2_WRITE_BURST_XMIT_CTRL, 	MHAWB_REG_GEN_2_WRITE_BURST_XMIT_CTRL_VALUE, 1);

			#if ((MHAWB_ACCEL_SETTING == MHAWB_ACCEL_LENGTH) || (MHAWB_ACCEL_SETTING == MHAWB_ACCEL_ADOTPER_ID_AND_LENGTH))
			SiiRegBitsSet(MHAWB_REG_GEN_2_WRITE_BURST_XMIT_CTRL, 	MHAWB_GEN_2_WRITE_BURST_XMIT_FIXED_LENGTH, 1);
			#endif

			#if ((MHAWB_ACCEL_SETTING == MHAWB_ACCEL_ADOPTER_ID) || (MHAWB_ACCEL_SETTING == MHAWB_ACCEL_ADOTPER_ID_AND_LENGTH))	
			//SiiRegWriteBlock(MHAWB_GEN_2_WRITE_BURST_PEER_ADPT_ID_LBYTE,
			//		g_mhawb.peer_adopter_id, MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_ADOPTERID);
			SiiRegWrite(MHAWB_GEN_2_WRITE_BURST_PEER_ADPT_ID_LBYTE, g_mhawb.peer_adopter_id[1]);
			SiiRegWrite(MHAWB_GEN_2_WRITE_BURST_PEER_ADPT_ID_HBYTE, g_mhawb.peer_adopter_id[0]);
			SiiRegBitsSet(MHAWB_REG_GEN_2_WRITE_BURST_XMIT_CTRL, 	MHAWB_GEN_2_WRITE_BURST_XMIT_FIXED_ADOPTER_ID,	1);
			#endif
		
			SiiRegBitsSet(MHAWB_REG_GEN_2_WRITE_BURST_XFIFO_STATUS, MHAWB_GEN_2_WRITE_BURST_XMIT_PRE_HS_EN,1);
			SiiRegWrite(MHAWB_REG_GEN_2_WRITE_BURST_INTR_MASK,	MHAWB_REG_GEN_2_WRITE_BURST_INTR_MASK_VALUE);
			SiiRegWrite(MHAWB_GEN_2_WRITE_BURST_ERROR_INTR_MASK,	MHAWB_GEN_2_WRITE_BURST_ERROR_INTR_MASK_VALUE);
			
			//printk(KERN_ERR "CBUS CTRL %02X\n", (int)SiiRegRead( MHAWB_REG_GEN_2_WRITE_BURST_XMIT_CTRL  ));

			// Workaround. This is a hack. Timeout doesn't behavior as expected.
			// Set timer for about 50 ms
			SiiRegWrite(MHAWB_REG_GEN_2_WRITE_BURST_XMIT_TIMEOUT, 01);
		
			//2013-07-16 --
			//	reset was found to be without reset. let try more
			//Reset TX
			//xyu: In my test, toggle enable bit is not needed.
			//SiiRegBitsSet(MHAWB_REG_GEN_2_WRITE_BURST_XMIT_CTRL, 	MHAWB_GEN_2_WRITE_BURST_XMIT_EN, 0);
			//SiiRegBitsSet(MHAWB_REG_GEN_2_WRITE_BURST_XMIT_CTRL, 	MHAWB_GEN_2_WRITE_BURST_XMIT_EN, 1);
			
			//2013-07-16 --
			//	reset was found to be without reset. let try more
			//Reset HAWB
			 SiiRegBitsSet(REG_HAWB_CTRL, 				BIT_HAWB_WRITE_BURST_DISABLE, 	 1);
			 SiiRegBitsSet(REG_HAWB_CTRL, 				BIT_HAWB_WRITE_BURST_DISABLE, 	 0);  

			SiiRegWrite(MHAWB_REG_GEN_2_WRITE_BURST_INTR, MHAWB_GEN_2_WRITE_BURST_XFIFO_FULL);
			//printk(KERN_ERR "CBUS CTRL %02X\n", (int)SiiRegRead( MHAWB_REG_GEN_2_WRITE_BURST_XMIT_CTRL  ));

			// clear IDLE interrupt signalling HAWB is ready
			//xyu: this interrupt is not take care in the main CBUS handler
			//SiiRegWrite(MHAWB_REG_GEN_2_WRITE_BURST_INTR, 		MHAWB_GEN_2_WRITE_BURST_SM_IDLE);
			
			//SiiOsTimerStop(g_mhawb.timer_xfifoempty);
	
			// Support MDT activation while HAWB interrupts are pending.
			g_mhawb.hid_event.length = 0;
			memset(g_mhawb.hid_event.burst_bytes, 0, sizeof(struct mhawb_fifo_level_data_t));
			
			//2013-07-16
			//     -- interrupt will only occur during initialization
			// in an interrupt oriented driver this interrupt occurs first.
			g_mhawb.state	= MHAWB_STATE_WAIT_FOR_DATA;
			//2013-07-16
			//     --
			// no rush on this first transition; can wait indefinitely since XFIFO_EMPTY
			//  	also allow receipt of new data
			//SiiOsTimerStart(g_mhawb.timer_xfifoempty, MHAWB_TIMEOUT_FOR_XFIFO_EMPTY);
			//ret 		= MHAWB_EVENT_HANDLER_WAITING;
			
			//2013-07-16
			//    clear pending updates
			//g_mhawb.hid_event_received = 0;

			g_mhawb.touch_x	= (XY_MAX/2); g_mhawb.touch_y = (XY_MAX/2); 

			break;			
		case MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY_AS_PROXY:
			// This is the entry point from 3D code.
			
			// This is a temproary state to handle interrupts missed by other parts of the firmware.
			// Once the state machine exists this state, it will not return here.
			
			// After the remaining interrupt is handled the state machine will go through RESET.
			//
			{
				//2013-05-22 - support timeouts
				//In case the PROXY state was triggered without reset, reset the timer here.
				
				//Use timeouts to allow MDT prioritization in polling implementation.
				//Timer is started and will be considered in particular states 
				//	relative to the wait allowed for that state.
				#ifdef __KERNEL__
				//SiiOsTimerStart(g_mhawb.timer_xfifoempty, MHAWB_TIMEOUT_FOR_XFIFO_EMPTY);
				#else
				if (g_mhawb.start_time == 0)
					g_mhawb.start_time = SiiTimerTotalElapsed();
				#endif
			}
		case MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY:
			// The work done in this state depends on INT status 
			g_intr = is_interrupt_asserted();						
		
			// 
			// - 130715 - FIFO EMPTY check and supporting code removed -
			//				
			// if FIFO is already empty, the state machine is in the wrong state
			// since XFIFO_EMPTY support receipt of new data, don't worry about occurance
			//	since it will clear once new data arrives.

		
			if (g_intr) {
				// rather than check interrupt bit, check for a change in
				// the number of free levels								
				reg_val = SiiRegRead(MHAWB_REG_GEN_2_WRITE_BURST_XFIFO_STATUS);
				reg_val = reg_val >> MHAWB_GEN_2_WRITE_BURST_XFIFO_LEVEL_AVAIL_LSB;
				
				// Predictive interrupt handling looks for an expected condition.				
				// 	if the FIFO is now empty, clear related interrupt interrupt
				if (reg_val == MHAWB_XFIFO_EMPTY_LEVELS_MAX) {
					SiiRegWrite(MHAWB_REG_GEN_2_WRITE_BURST_INTR, 
						MHAWB_GEN_2_WRITE_BURST_XFIFO_EMPTY /*|
							MHAWB_GEN_2_WRITE_BURST_SM_IDLE*/ );
					
					if (g_mhawb.state == MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY_AS_PROXY)
						g_mhawb.state	= MHAWB_STATE_INIT;	
					else 
						g_mhawb.state	= MHAWB_STATE_WAIT_FOR_DATA;

					//2013-05-22 - support timeouts
					//Use timeouts to allow MDT prioritization in polling implementation.
					//Timer is started and will be considered in particular states 
					//	relative to the wait allowed for that state.
					//printk(KERN_ERR "MHAWB XFIFO timer stop.\n");
					//SiiOsTimerStop(g_mhawb.timer_xfifoempty);

					// Because this implementation does't currently support a circular buffer
					//		the pointer shall only be reset if data isn't pending.
					//    This mean that buffer may be exhausted while memory is free.
					g_mhawb.empty_levels		= MHAWB_XFIFO_EMPTY_LEVELS_MAX;

					// if FIFO isn't empty, interrupt may reflect an error condition
				} else {
					//2013-05-22 - support timeouts
					
					//if this wasn't an error, we're here for some unrelated reason
					//   and should allow MDT to continue running until timeout

					//2013-07-29, xyu: Timeout is not effect here since ISR lock,
					//There's interrupt but not expected, do not wait anymore.
					//ret = MHAWB_EVENT_HANDLER_WAITING;
				}
				break;
			}else
			{
				//2013-07-29, xyu: There's no interrupt, continue waiting for fifo empty or timeout.
				// Timeout is currently not work due to ISR lock, need to fix it.
				ret = MHAWB_EVENT_HANDLER_WAITING;
			}

			// if an interrupt isn't currently pending and there's room in the FIFO,
			//   	 allow receipt of more data but, continue to prioritize receipt
			//  	 of XFIFO EMPTY INTR

		case MHAWB_STATE_WAIT_FOR_DATA:
			//MHAWB_SUPPORT_USE_REFERENCE differs from the alternative because it only
			//	allows FIFO writes when FIFO is empty.
		
			// exit if nothing is to be sent, exit
			//if (g_mhawb.next_tx_level->fields.level.length == 0) {
			//	break;
			//}

			// if FIFO full, FOR loop below with exit due to empty_levels
			
			// If FIFO is empty, copy pending levels into FIFO
			//
			// To demonstrate use of various optimizations, the following code appropriately sets
			//    the buffer header and identifies the offest.
			//
			// In a production system, only one optimization configuration needs to be supported
			//			
			// Empty_levels should never be greater than MHAWB_XFIFO_EMPTY_LEVELS_MAX. If this happens
			//		counter overflowed. Remove condition in future code reduction efforts.					
			//g_mhawb.state					= MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY;		
			
			//as code reduction can replace current_level with g_mhawb.next_tx_level
			for( current_level = g_mhawb.next_tx_level; 
					 ((current_level->fields.level.length != 0) && (!is_interrupt_asserted()));
					 current_level = current_level->fields.next) {
							
				//g_mhawb.empty_levels--;

				#if (MHAWB_ACCEL_SETTING == MHAWB_RUNTIME)
						 
				buffer_start 	= (current_level->raw_bytes + 
						offsets_and_header_lengths[g_mhawb.flags_accelerations][FIELD_FIRST_BYTE_OFFEST]);
				buffer_length 	= (current_level->fields.level.length +
						offsets_and_header_lengths[g_mhawb.flags_accelerations][FIELD_HEADER_SUM]);
						 
				switch(g_mhawb.flags_accelerations) {
					case MHAWB_ACCEL_LENGTH:						
						memcpy(buffer_start,g_mhawb.peer_adopter_id, FIELD_BYTE_MAX_LENGTH_FOR_ADOPTERID);
					case MHAWB_ACCEL_ADOTPER_ID_AND_LENGTH:
						//length is ensured to be less than FIELD_BYTE_MAX_LENGTH_FOR_DATA
						// 	in si_debugger_hdmigear.c
						if (g_mhawb.max_packet_length < buffer_length) {
							g_mhawb.max_packet_length = buffer_length;
							SiiRegWrite( WRITE_BURST_DATA_LEN, g_mhawb.max_packet_length);
						}
						break;
					case MHAWB_ACCEL_ADOPTER_ID:
						*buffer_start = buffer_length;
						break;
					case MHAWB_ACCEL_NOTHING:
					default:					
						*buffer_start = buffer_length;
						memcpy((buffer_start + MHAWB_ACCEL_NONE_LEVEL_BYTE_OFFSET_FOR_ADOPTERID), 
							g_mhawb.peer_adopter_id, FIELD_BYTE_MAX_LENGTH_FOR_ADOPTERID);
						break;					
				}
				#else
				buffer_start 	= (current_level->raw_bytes + 
						offsets_and_header_lengths[MHAWB_ACCEL_SETTING][FIELD_FIRST_BYTE_OFFEST]);
				buffer_length 	= (current_level->fields.level.length +
						offsets_and_header_lengths[MHAWB_ACCEL_SETTING][FIELD_HEADER_SUM]);				
				
				#if (MHAWB_ACCEL_SETTING == MHAWB_ACCEL_LENGTH)
						memcpy(buffer_start,g_mhawb.peer_adopter_id, FIELD_BYTE_MAX_LENGTH_FOR_ADOPTERID);
						//length is ensured to be less than FIELD_BYTE_MAX_LENGTH_FOR_DATA
						// 	in si_debugger_hdmigear.c
						if (g_mhawb.max_packet_length < buffer_length) {
							g_mhawb.max_packet_length = buffer_length;
							SiiRegWrite( MHAWB_WRITE_BURST_DATA_LEN, g_mhawb.max_packet_length);
						}

				#elif (MHAWB_ACCEL_SETTING == MHAWB_ACCEL_ADOTPER_ID_AND_LENGTH)
						//length is ensured to be less than FIELD_BYTE_MAX_LENGTH_FOR_DATA
						// 	in si_debugger_hdmigear.c
						if (g_mhawb.max_packet_length < buffer_length) {
							g_mhawb.max_packet_length = buffer_length;
							SiiRegWrite( MHAWB_WRITE_BURST_DATA_LEN, g_mhawb.max_packet_length);
						}

				#elif	(MHAWB_ACCEL_SETTING == MHAWB_ACCEL_ADOPTER_ID)
						*buffer_start = buffer_length;

				#elif	(MHAWB_ACCEL_SETTING == MHAWB_ACCEL_NOTHING)
						*buffer_start = buffer_length;
						memcpy((buffer_start + MHAWB_ACCEL_NONE_LEVEL_BYTE_OFFSET_FOR_ADOPTERID), 
						g_mhawb.peer_adopter_id, FIELD_BYTE_MAX_LENGTH_FOR_ADOPTERID);

				#endif
				#endif
				//TODO - decide if length should be increase here by when to accomodate wrapper	

				//2013-07-30-bug in the i2c driver requires that this lenght be increased by 1
				SiiRegWriteBlock(MHAWB_GEN_2_WRITE_BURST_XFIFO_DATA , buffer_start, buffer_length);
				//DEBUG_PRINT( MSG_ALWAYS, "WRITEN %x %x\n",(int)buffer_start[0], (int)buffer_length);

				// Since hardware FIFO is never cleared, might as well free up buffer now.
				// This is especially important now to avoid sending the same data twice.
				current_level->fields.level.length = 0;
						
				#if (MHAWB_ACCEL_SETTING == MHAWB_ACCEL_ADOTPER_ID_AND_LENGTH)
				//Workaround. Mask is cleared for larger packet sizes. Not a problem for a 4 byte packet.
				//Need to cache this to avoid frequent writes
				if (buffer_length > 4)
					SiiRegWrite(MHAWB_REG_GEN_2_WRITE_BURST_INTR_MASK, 			
							MHAWB_REG_GEN_2_WRITE_BURST_INTR_MASK_VALUE);
				#endif						
			}
			
			g_mhawb.next_tx_level = current_level;
#if 0
			// to accept new data while waiting for XFIFO_EMPTY, 
			//	setup for XFIFO_EMPTY only when explicitely waiting for data
			if(g_mhawb.state == MHAWB_STATE_WAIT_FOR_DATA) {						
				//exit and come back again immediately for  further handling
				g_mhawb.state = MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY;

				//2013-07-16 --
				//	now that this code is syncrhonized with ISR, don't delay ISR
				//ret 	      = MHAWB_EVENT_HANDLER_WAITING;
			
				//initiate empty interrupt timeout
				//printk(KERN_ERR "MHAWB XFIFO timer start.\n");
				SiiOsTimerStart(g_mhawb.timer_xfifoempty, MHAWB_TIMEOUT_FOR_XFIFO_EMPTY);
			}
#endif
			break;
			//DEBUG_PRINT( MSG_ALWAYS, "M2 %x %x %x\n",(int)g_mhawb.state,(int)g_intr, (int)g_mhawb.empty_levels);			

		// the following 3 should never happen
		case MHAWB_STATE_UNINITIALIZED:
		case MHAWB_STATE_DISABLED:
		case MHAWB_STATE_TAKEOVER:
		default:
			return MHAWB_EVENT_HANDLER_FAILED;
	}
//	printk(KERN_ERR "              MHAWB_E t:%x i:%x s:%x lev:%x len:%x stat:%x cn:%x end:%x\n", (int)g_mhawb.timer_running, (int) g_intr, (int)g_mhawb.state, 
//						(int)g_mhawb.empty_levels,
//						(int)g_mhawb.hid_event.length, (int)SiiCbusRequestStatus(),
//						(int)SiiMhlRxCbusConnected(), (int) ret);

	//print_intr_status();
	
	return ret;
}

#endif
