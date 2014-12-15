//******************************************************************************
//!file     si_drv_mhawb.h
//!brief    SiI5293 MDT Driver
//
// No part of this work may be reproduced, modified, distributed,
// transmitted, transcribed, or translated into any language or computer
// format, in any form or by any means without written permission of
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2012, Silicon Image, Inc.  All rights reserved.
//*****************************************************************************/

#ifndef __SI_DRV_MHAWB_H__
#define __SI_DRV_MHAWB_H__

#define		MHAWB_SUPPORT				1

#ifdef MHAWB_SUPPORT

//#include "../../application/sk_application.h"
//#include "../cra_drv/si_drv_cra_cfg.h"
#include "si_mdt_inputdev.h"
#ifdef __KERNEL__
#include "si_common.h"
#include "si_platform.h"
#include <linux/gpio.h>
#include <linux/wakelock.h>
#else
#include "AT89C51XD2.h"
#endif
/*
 * PUBLIC CONSTANTS
 */

#define 	MHAWB_SUPPORT_USE_REFERENCE
#define		MHAWB_WORKAROUND_FOR_INCOMPLETE_5293_FIRMWARE
#define		MHAWB_WORKAROUND_FOR_5293_BUG

//For C51, to simplify data transfer out of UART buffer into the MDT module
// 	use "data" or "idata" memory for MDT global variables.
//
//Don't know if this memory type must be used for performance reasons or if
//  this memory type must be used to allow copy from data used by ISR. Experimentally,
//  found that xdata memory will cause 8051 to corrupt MHAWB data at higher event rates.
#ifdef __KERNEL__
#define 	MHAWB_MEMORY
#define 	MHAWB_CODE_MEMORY
#else
#define 	MHAWB_MEMORY 						data
#define 	MHAWB_CODE_MEMORY					code
#define		MCP5293_INT_PIN						INT0
#endif


//Allow compile or runtime configuration and selection of MDT HAWB.
#define 	MHAWB_ACCEL_NOTHING			0//up to 17 byte I2C block writes to FIFO; least optimized
#define 	MHAWB_ACCEL_LENGTH			1//up to 16 byte I2C block writes to FIFO
#define 	MHAWB_ACCEL_ADOPTER_ID			2//up to 15 byte I2C block writes to FIFO
#define 	MHAWB_ACCEL_ADOTPER_ID_AND_LENGTH	3//up to 14 byte I2C block writes to FIFO
#define 	MHAWB_RUNTIME				4//configurable at runtime 

//Set MDT_ACCEL_SETTING to one of the above 5 choices above
#define 	MHAWB_ACCEL_SETTING														MHAWB_ACCEL_ADOTPER_ID_AND_LENGTH

/*
 * PUBLIC CONSTANTS FOR MDT_ACCEL_SETTING
 */

#define 	MHAWB_LEVEL_BYTE_MAX_OFFSET_FOR_LENGTH			0
#define 	MHAWB_LEVEL_BYTE_MAX_OFFSET_FOR_ADOPTERID		1
#define		MHAWB_LEVEL_BYTE_MAX_OFFEST_FOR_DATA			3

#define 	MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_LENGTH			1
#define		MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_ADOPTERID		2
#define		MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA			14
#define		MHAWB_FIELD_BYTE_MAX_LENGTH_SUM				MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_LENGTH 		\
									+ MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_ADOPTERID \
									+ MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA

#define 	MHAWB_ACCEL_NONE_LEVEL_BYTE_OFFSET_FOR_LENGTH		MHAWB_LEVEL_BYTE_MAX_OFFSET_FOR_LENGTH
#define 	MHAWB_ACCEL_NONE_LEVEL_BYTE_OFFSET_FOR_ADOPTERID  	MHAWB_LEVEL_BYTE_MAX_OFFSET_FOR_ADOPTERID
#define		MHAWB_ACCEL_NONE_LEVEL_BYTE_OFFEST_FOR_DATA		MHAWB_LEVEL_BYTE_MAX_OFFEST_FOR_DATA
#define 	MHAWB_ACCEL_NONE_FIELD_BYTE_LENGTH_FOR_LENGTH		MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_LENGTH
#define		MHAWB_ACCEL_NONE_FIELD_BYTE_LENGTH_FOR_ADOPTERID	MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_ADOPTERID
#define		MHAWB_ACCEL_NONE_FIELD_BYTE_LENGTH_FOR_DATA		MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA

#define 	MHAWB_ACCEL_NONE_LEVEL_BYTE_OFFSET_FOR_FIRST_BYTE	MHAWB_ACCEL_NONE_LEVEL_BYTE_OFFSET_FOR_LENGTH
#define 	MHAWB_ACCEL_NONE_LEVEL_BYTE_LENGHT_HEADER_SUM		MHAWB_ACCEL_NONE_FIELD_BYTE_LENGTH_FOR_LENGTH \
									+ MHAWB_ACCEL_NONE_FIELD_BYTE_LENGTH_FOR_ADOPTERID
#define 	MHAWB_ACCEL_NONE_LEVEL_BYTE_LENGHT_SUM			MHAWB_ACCEL_NONE_FIELD_BYTE_LENGTH_FOR_LENGTH \
									+ MHAWB_ACCEL_NONE_FIELD_BYTE_LENGTH_FOR_ADOPTERID \
									+ MHAWB_ACCEL_NONE_FIELD_BYTE_LENGTH_FOR_DATA

#define 	MHAWB_ACCEL_LENGTH_LEVEL_BYTE_OFFSET_FOR_ADOPTERID	MHAWB_LEVEL_BYTE_MAX_OFFSET_FOR_ADOPTERID
#define		MHAWB_ACCEL_LENGTH_LEVEL_BYTE_OFFEST_FOR_DATA		MHAWB_LEVEL_BYTE_MAX_OFFEST_FOR_DATA
#define 	MHAWB_ACCEL_LENGTH_FIELD_BYTE_LENGTH_FOR_LENGTH		0
#define		MHAWB_ACCEL_LENGTH_FIELD_BYTE_LENGTH_FOR_ADOPTERID	MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_ADOPTERID
#define		MHAWB_ACCEL_LENGTH_FIELD_BYTE_LENGTH_FOR_DATA		MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA
#define 	MHAWB_ACCEL_LENGTH_LEVEL_BYTE_OFFSET_FOR_FIRST_BYTE	MHAWB_ACCEL_LENGTH_LEVEL_BYTE_OFFSET_FOR_ADOPTERID
#define 	MHAWB_ACCEL_LENGTH_LEVEL_BYTE_LENGHT_HEADER_SUM		MHAWB_ACCEL_LENGTH_FIELD_BYTE_LENGTH_FOR_LENGTH \
									+ MHAWB_ACCEL_LENGTH_FIELD_BYTE_LENGTH_FOR_ADOPTERID
#define 	MHAWB_ACCEL_LENGTH_LEVEL_BYTE_LENGHT_SUM		MHAWB_ACCEL_LENGTH_FIELD_BYTE_LENGTH_FOR_LENGTH \
									+ MHAWB_ACCEL_LENGTH_FIELD_BYTE_LENGTH_FOR_ADOPTERID \
									+ MHAWB_ACCEL_LENGTH_FIELD_BYTE_LENGTH_FOR_DATA

#define 	MHAWB_ACCEL_ADOPTERID_LEVEL_BYTE_OFFSET_FOR_LENGTH	2
#define		MHAWB_ACCEL_ADOPTERID_LEVEL_BYTE_OFFEST_FOR_DATA	MHAWB_LEVEL_BYTE_MAX_OFFEST_FOR_DATA
#define 	MHAWB_ACCEL_ADOPTERID_FIELD_BYTE_LENGTH_FOR_LENGTH	MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_LENGTH
#define		MHAWB_ACCEL_ADOPTERID_FIELD_BYTE_LENGTH_FOR_ADOPTERID	0
#define		MHAWB_ACCEL_ADOPTERID_FIELD_BYTE_LENGTH_FOR_DATA	MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA
#define 	MHAWB_ACCEL_ADOPTERID_LEVEL_BYTE_OFFSET_FOR_FIRST_BYTE	MHAWB_ACCEL_ADOPTERID_LEVEL_BYTE_OFFSET_FOR_LENGTH
#define 	MHAWB_ACCEL_ADOPTERID_LEVEL_BYTE_LENGHT_HEADER_SUM	MHAWB_ACCEL_ADOPTERID_FIELD_BYTE_LENGTH_FOR_LENGTH \
									+ MHAWB_ACCEL_ADOPTERID_FIELD_BYTE_LENGTH_FOR_ADOPTERID
#define 	MHAWB_ACCEL_ADOPTERID_LEVEL_BYTE_LENGHT_SUM		MHAWB_ACCEL_ADOPTERID_FIELD_BYTE_LENGTH_FOR_LENGTH \
									+ MHAWB_ACCEL_ADOPTERID_FIELD_BYTE_LENGTH_FOR_ADOPTERID \
									+ MHAWB_ACCEL_ADOPTERID_FIELD_BYTE_LENGTH_FOR_DATA

#define		MHAWB_ACCEL_ALL_LEVEL_BYTE_OFFEST_FOR_DATA		MHAWB_LEVEL_BYTE_MAX_OFFEST_FOR_DATA
#define 	MHAWB_ACCEL_ALL_FIELD_BYTE_LENGTH_FOR_LENGTH		0
#define		MHAWB_ACCEL_ALL_FIELD_BYTE_LENGTH_FOR_ADOPTERID		0
#define		MHAWB_ACCEL_ALL_FIELD_BYTE_LENGTH_FOR_DATA		MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_DATA
#define 	MHAWB_ACCEL_ALL_BYTE_OFFSET_FOR_FIRST_BYTE		MHAWB_ACCEL_ALL_LEVEL_BYTE_OFFEST_FOR_DATA
#define 	MHAWB_ACCEL_ALL_LEVEL_BYTE_LENGHT_HEADER_SUM		MHAWB_ACCEL_ALL_FIELD_BYTE_LENGTH_FOR_LENGTH \
									+ MHAWB_ACCEL_ALL_FIELD_BYTE_LENGTH_FOR_ADOPTERID
#define 	MHAWB_ACCEL_ALL_LEVEL_BYTE_LENGHT_SUM			MHAWB_ACCEL_ALL_FIELD_BYTE_LENGTH_FOR_LENGTH \
									+ MHAWB_ACCEL_ALL_FIELD_BYTE_LENGTH_FOR_ADOPTERID \
									+ MHAWB_ACCEL_ALL_FIELD_BYTE_LENGTH_FOR_DATA

#define 	MHAWB_BYTE_LENGTH_NULL					0

/*
 * PUBLIC CONSTANTS FOR MDT PROTOCOL
 *
 * The SiI5293 reference firmware acts like a UART to I2C bridge.
 * This code does not decode or encode MDT.
 * As part of code reduction, this brige function requires minor data manipulation.
 *
 * MDT protocol support may be found inside the SiIMon SiI5293 plug-in. 
 */

#define		MDT_BYTE_LENGTH_MOUSE					3
#define		MDT_BYTE_LENGTH_TOUCH					4
#define		MDT_BYTE_LENGTH_KEYBOARD				6
#define		MDT_BYTE_LENGTH_HEADER					1
#define		MDT_BYTE_LENGTH_MIN_PACKET				MDT_BYTE_LENGTH_HEADER \
									+ MDT_BYTE_LENGTH_MOUSE

#define		MDT_BYTE_LENGTH_MAX_PACKET				MDT_BYTE_LENGTH_HEADER \
									+ MDT_BYTE_LENGTH_KEYBOARD

#define 	SIIMON_DATA_BURST_START_OFFSET				2


/*
 * PUBLIC CONSTANTS AND TYPE FOR SII5293 REGISTERS
 *
 * Duplicates exist. See "CP5293 PLATFORM" above for detail.
 * Prefix added.		 See "CP5293 PLATFORM" above for detail.
 */

#define		MHAWB_PP_PAGE						PP_PAGE
#define		MHAWB_CBUS_PAGE						CBUS_PAGE

#define		MHAWB_XFIFO_EMPTY_LEVELS_MAX				4

#define 	MHAWB_REG_INTR_STATE					( MHAWB_PP_PAGE | 0x70 )
#define 	MHAWB_INTR						( 1 << 0 )

#define 	MHAWB_WRITE_BURST_DATA_LEN				( MHAWB_CBUS_PAGE | 0xC6 )

#define		MHAWB_GEN_2_WRITE_BURST_PEER_ADPT_ID_LBYTE		( MHAWB_CBUS_PAGE | 0xB6 )
#define		MHAWB_GEN_2_WRITE_BURST_PEER_ADPT_ID_HBYTE		( MHAWB_CBUS_PAGE | 0xB7 )

#define		MHAWB_REG_GEN_2_WRITE_BURST_XMIT_TIMEOUT 		( MHAWB_CBUS_PAGE | 0x85 )


#define 	MHAWB_REG_GEN_2_WRITE_BURST_XMIT_CTRL			( MHAWB_CBUS_PAGE | 0x88 )

#define 	MHAWB_GEN_2_WRITE_BURST_XMIT_EN				( 1 << 7 )
#define 	MHAWB_GEN_2_WRITE_BURST_XMIT_CMD_MERGE_EN		( 1 << 6 )
#define 	MHAWB_GEN_2_WRITE_BURST_XMIT_FIXED_LENGTH 		( 1 << 5 )
#define 	MHAWB_GEN_2_WRITE_BURST_XMIT_FIXED_ADOPTER_ID 		( 1 << 4 )
#define 	MHAWB_GEN_2_WRITE_BURST_XIMT_SINGLE_RUN_EN 		( 1 << 3 )
#define 	MHAWB_GEN_2_WRITE_BURST_CLR_ABORT_WAIT			( 1 << 2 )
#define		MHAWB_GEN_2_WRITE_BURST_XFIFO_CLR_ALL 			( 1 << 1 )
#define		MHAWB_GEN_2_WRITE_BURST_XFIFO_CLR_CUR		 	( 1 << 0 )

#define 	MHAWB_REG_GEN_2_WRITE_BURST_XMIT_CTRL_VALUE		( MHAWB_GEN_2_WRITE_BURST_XMIT_CMD_MERGE_EN | \
									MHAWB_GEN_2_WRITE_BURST_XMIT_FIXED_LENGTH | \
									MHAWB_GEN_2_WRITE_BURST_XMIT_FIXED_ADOPTER_ID | \
									MHAWB_GEN_2_WRITE_BURST_XFIFO_CLR_ALL | \
									MHAWB_GEN_2_WRITE_BURST_CLR_ABORT_WAIT) 

#define 	MHAWB_GEN_2_WRITE_BURST_XFIFO_DATA_OFFSET		0x89
#define 	MHAWB_GEN_2_WRITE_BURST_XFIFO_DATA			( MHAWB_CBUS_PAGE | \
									MHAWB_GEN_2_WRITE_BURST_XFIFO_DATA_OFFSET )
#define		MHAWB_REG_GEN_2_WRITE_BURST_INTR			( MHAWB_CBUS_PAGE | 0x8C )
#define 	MHAWB_REG_GEN_2_WRITE_BURST_INTR_MASK			( MHAWB_CBUS_PAGE | 0x8D )

#define 	MHAWB_GEN_2_WRITE_BURST_XFIFO_EMPTY			( 1 << 3 )
#define 	MHAWB_GEN_2_WRITE_BURST_SM_IDLE				( 1 << 2 )
#define 	MHAWB_GEN_2_WRITE_BURST_XFIFO_FULL 			( 1 << 1 )
#define		MHAWB_GEN_2_WRITE_BURST_RFIFO_RDY 			( 1 << 0 )

#define 	MHAWB_REG_GEN_2_WRITE_BURST_XFIFO_STATUS		(CBUS_PAGE | 0x8B)
#define		MHAWB_GEN_2_WRITE_BURST_XFIFO_LEVEL_AVAIL_LSB	5
#define 	MHAWB_GEN_2_WRITE_BURST_XMIT_PRE_HS_EN 			0x10

#define 	MHAWB_REG_GEN_2_WRITE_BURST_ERROR_INTR			( MHAWB_CBUS_PAGE | 0x8E )
#define 	MHAWB_GEN_2_WRITE_BURST_ERROR_INTR_MASK			( MHAWB_CBUS_PAGE | 0x8F )
#define		MHAWB_GEN_2_WRITE_BURST_XSM_ERROR			( 1 << 7 )
#define		MHAWB_GEN_2_WRITE_BURST_XSM_RCVD_ABORTPKT		( 1 << 6 )
#define		MHAWB_GEN_2_WRITE_BURST_XTIMEOUT			( 1 << 5 )
#define		MHAWB_GEN_2_WRITE_BURST_RSM_ERROR			( 1 << 2 )
#define 	MHAWB_GEN_2_WRITE_BURST_RSM_RCVD_ABORTPKT		( 1 << 1 )
#define		MHAWB_GEN_2_WRITE_BURST_RTIMEOUT			( 1 << 0 )
#define 	MHAWB_GEN_2_WRITE_BURST_ERROR_INTR_MASK_VALUE		( MHAWB_GEN_2_WRITE_BURST_XSM_ERROR |\
									MHAWB_GEN_2_WRITE_BURST_XSM_RCVD_ABORTPKT )

// To start, support HAWB Tx; HAWB Rx not implemented here
#define		MHAWB_REG_GEN_2_WRITE_BURST_INTR_MASK_VALUE	 	MHAWB_GEN_2_WRITE_BURST_XFIFO_FULL
									//( MHAWB_GEN_2_WRITE_BURST_XFIFO_EMPTY |
									//MHAWB_GEN_2_WRITE_BURST_SM_IDLE )

#define 	MHAWB_INT_STATUS_CBUS1 					( MHAWB_CBUS_PAGE | 0x92 )
#define 	MHAWB_MSC_MSG_DONE_WITH_NACK				( 1 << 7 )
#define		MHAWB_SET_INT_RCVD					( 1 << 6 )
#define		MHAWB_WRITE_BURST_RCVD					( 1 << 5 )
#define		MHAWB_MSC_MSG_RCVD					( 1 << 4 )
#define		MHAWB_WRITE_STAT_RCVD					( 1 << 3 )
#define		MHAWB_MSC_CMD_DONE					( 1 << 1 )
#define		MHAWB_CONNECT_CHANGE					( 1 << 0 )

#define 	MHAWB_INT_STATUS_CBUS2 					(CBUS_PAGE | 0x94)
#define 	MHAWB_MSC_CMD_ABORT					( 1 << 6 )
#define		MHAWB_MSC_ABORT_RCVD					( 1 << 3 )
#define		MHAWB_DDC_ABORT						( 1 << 2 )

#define 	MHAWB_CBUS_XFR_ABORT 					(CBUS_PAGE | 0x9A)
#define 	MHAWB_PEER_ABORT					( 1 << 7 )
#define 	MHAWB_XFR_UNDEF_CMD					( 1 << 3 )
#define		MHAWB_XFR_TIMEOUT					( 1 << 2 )
#define		MHAWB_XFR_PROTO_ERR 					( 1 << 1 )
#define		MHAWB_XFR_MAX_FAIL					( 1 << 0 )

#define 	MHAWB_CBUS_DDC_ABORT					(CBUS_PAGE | 0x98)		


// TIMER TIMEOUTS
#define	  MHAWB_TIMEOUT_FOR_XFIFO_EMPTY					100
#define	  MHAWB_TIMEOUT_FOR_NEW_DATA					100
#define   MHAWB_WAIT_FOR_XFIFOEMPTY_TIMEOUT				7770
#define   MHAWB_WAIT_FOR_NEWDATA_TIMEOUT				7771

// Structure reflects the HAWB FIFO LEVEL
//
struct mhawb_fifo_level_data_t {
	uint8_t								length;
	uint8_t								adopter_id[MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_ADOPTERID];
	uint8_t								burst_bytes[MDT_BYTE_LENGTH_MAX_PACKET * 2];
};

/*
 * PUBLIC CONSTANTS AND TYPES FOR MHAWB IMPLEMENTATION
 */

enum 	mhawb_state_e {	  
	MHAWB_STATE_UNINITIALIZED
	, MHAWB_STATE_TAKEOVER				//during disable HAWB process
	, MHAWB_STATE_DISABLED				//state machine does nothign
	, MHAWB_STATE_INIT				//ENTRY POINT... intializes global variables
	, MHAWB_STATE_RESET				//resets global variables and HAWB registers
	, MHAWB_STATE_WAIT_FOR_DATA			//variable polling state; waits for mhawb_circular_buffer changes
	, MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY		//MCP5293_INT_PIN polling state; waits for interrupt				
	, MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY_AS_PROXY	//ENTRY POINT... workarounds to CP5293 FW + MHAWB_STATE_WAIT_FOR_XFIFO_EMPTY
};

enum 	mhawb_return_e {				  
	MHAWB_EVENT_HANDLER_FAILED  = ( 1 << 0)		//let all firmware run
	, MHAWB_EVENT_HANDLER_SUCCESS = ( 1 << 1)	//let polling parts of firmware run
	, MHAWB_EVENT_HANDLER_WAITING = ( 1 << 2)	//return to MHAWB after SiIMon handler runs
};

struct mhawb_level_data {
	struct mhawb_fifo_level_data_t		level;
	union mhawb_circular_level_buffer_t	MHAWB_MEMORY		*next;
};

union mhawb_circular_level_buffer_t {
		struct mhawb_level_data		fields;					
		uint8_t				raw_bytes[sizeof(struct mhawb_level_data)];
};


// consider moving these varables into CBUS or app

struct mhawb_t {
	enum mhawb_state_e			state;

	#ifdef __KERNEL__
        SiiOsTimer_t				timer_xfifoempty;
	#else
	clock_time_t 				start_time;
	#endif

	uint8_t					max_packet_length;
	uint8_t peer_adopter_id[MHAWB_FIELD_BYTE_MAX_LENGTH_FOR_ADOPTERID];									
	uint8_t					empty_levels;
#if (MHAWB_ACCEL_SETTING == MHAWB_RUNTIME)
	uint8_t					flags_accelerations;
#endif
	wait_queue_head_t 			wait;
	struct semaphore 			sem;
	uint8_t					hid_event_received;
	struct  mhawb_fifo_level_data_t		hid_event;		//temp variable

	union	mhawb_circular_level_buffer_t	MHAWB_MEMORY 	*next_rx_level;
	union	mhawb_circular_level_buffer_t	MHAWB_MEMORY 	*next_tx_level;
	union 	mhawb_circular_level_buffer_t			circular_buffer[MHAWB_XFIFO_EMPTY_LEVELS_MAX];

	uint16_t				touch_x;
	uint16_t				touch_y;
};

/*
 * GLOBAL VARIABLE DEFINED BY THIS MODULE
 */

extern struct mhawb_t MHAWB_MEMORY g_mhawb;

/*
 * GLOBAL VARIABLES DEFINED IN FIRMWARE
 */

#ifndef __KERNEL__
extern bool_t					hawb3DXfifoEmptyFlag;
extern uint8_t					hawb3DXmitCurrentIndex;
#endif

/*
 * PUBLIC FUNCTIONS VARIABLES DEFINED IN FIRMWARE
 */
enum mhawb_return_e mhawb_do_work ( void );
enum mhawb_return_e mhawb_do_isr_work(void);
enum mhawb_return_e mhawb_init ( void );
enum mhawb_return_e mhawb_destroy( void );
#endif
#endif // __SI_DRV_MHAWB_H__
