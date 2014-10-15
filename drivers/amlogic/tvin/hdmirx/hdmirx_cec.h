/*
 * hdmirx_drv.h for HDMI device driver, and declare IO function,
 * structure, enum, used in TVIN AFE sub-module processing
 *
 * Copyright (C) 2013 AMLOGIC, INC. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef _HDMICEC_H
#define _HDMICEC_H
#if CEC_FUNC_ENABLE
#define CEC_MSG_QUEUE_SIZE 40

typedef enum _cec_dev_logic_addr_
{
    E_LA_TV              =0,
    E_LA_RECORDER1       =1,
    E_LA_RECORDER2       =2,
    E_LA_TUNER1          =3,
    E_LA_PLAYBACK1       =4,
    E_LA_AUDIO_SYS       =5,
    E_LA_TUNER2          =6,
    E_LA_TUNER3          =7,
    E_LA_PLAYBACK2       =8,
    E_LA_RECORER3        =9,
    E_LA_TUNER4          =10,
    E_LA_PLYBACK3        =11,
    RESERVED_1           =12,
    RESERVED_2           =13,
    E_LA_FREE_USE        =14,
    E_LA_UNREGISTERED    =15,
    E_LA_BROADCAST       =15,
    E_LA_MAX = 15,
} _cec_dev_logic_addr_;

typedef enum _cec_dev_type_
{
    E_DEVICE_TYPE_TV                    =0,
    E_DEVICE_TYPE_RECORDING_DEVICE      =1,
    E_DEVICE_TYPE_RESERVED              =2,
    E_DEVICE_TYPE_TUNER                 =3,
    E_DEVICE_TYPE_PLAYBACK_DEVICE       =4,
    E_DEVICE_TYPE_AUDIO_SYSTEM          =5,
    E_DEVICE_TYPE_PURE_CEC_SWITCH		=6,
    E_DEVICE_TYPE_VIDEO_PROCESSOR		=7
} _cec_dev_type_;

typedef enum _cec_op_code_
{
//----- One Touch Play ----------------------------
    E_MSG_ACTIVE_SOURCE                         = 0x82,
    E_MSG_IMAGE_VIEW_ON                     	= 0x04,
    E_MSG_TEXT_VIEW_ON                      	= 0x0D,
//----- Routing Control ---------------------------
    //E_MSG_RC_ACTIVE_SOURCE                    = 0x82,
    E_MSG_INACTIVE_SOURCE						= 0x9D,
    E_MSG_REQUEST_ACTIVE_SOURCE					= 0x85,
    E_MSG_ROUTING_CHANGE						= 0x80,
    E_MSG_ROUTING_INFO							= 0x81,
    E_MSG_SET_STREM_PATH						= 0x86,
//----- Standby Command ---------------------------
    E_MSG_STANDBY                               = 0x36,
//----- One Touch Record---------------------------
    E_MSG_RECORD_ON								= 0x09,
    E_MSG_RECORD_OFF							= 0x0B,
    E_MSG_RECORD_STATUS							= 0x0A,
    E_MSG_RECORD_TV_SCREEN						= 0x0F,
//----- Timer programmer -------------------------- CEC1.3a
    E_MSG_CLEAR_ANALOG_TIMER					= 0x33,
    E_MSG_CLEAR_DIGITAL_TIMER					= 0x99,
    E_MSG_CLEAR_EXT_TIMER						= 0xA1,
    E_MSG_SET_ANALOG_TIMER						= 0x34,
    E_MSG_SET_DIGITAL_TIMER						= 0x97,
    E_MSG_SET_EXT_TIMER							= 0xA2,
    E_MSG_SET_TIMER_PROGRAM_TITLE				= 0x67,
    E_MSG_TIMER_CLEARD_STATUS					= 0x43,
    E_MSG_TIMER_STATUS							= 0x35,
//----- System Information ------------------------
    E_MSG_CEC_VERSION                        	= 0x9E,			//1.3a
    E_MSG_GET_CEC_VERSION                   	= 0x9F,			//1.3a
    E_MSG_GIVE_PHYSICAL_ADDRESS              	= 0x83,
    E_MSG_REPORT_PHYSICAL_ADDRESS				= 0x84,
    E_MSG_GET_MENU_LANGUAGE						= 0x91,
    E_MSG_SET_MENU_LANGUAGE						= 0x32,
    //E_MSG_POLLING_MESSAGE						= ?,
    //E_MSG_REC_TYPE_PRESET						= 0x00,			//parameter   ?
    //E_MSG_REC_TYPE_OWNSRC						= 0x01,  		//parameter   ?
//----- Deck Control Feature-----------------------
    E_MSG_DECK_CTRL								= 0x42,
    E_MSG_DECK_STATUS                        	= 0x1B,
    E_MSG_GIVE_DECK_STATUS                   	= 0x1A,
    E_MSG_PLAY									= 0x41,
//----- Tuner Control ------------------------------
    E_MSG_GIVE_TUNER_STATUS                  	= 0x08,
    E_MSG_SEL_ANALOG_SERVICE                 	= 0x92,
    E_MSG_SEL_DIGITAL_SERVICE                	= 0x93,
    E_MSG_TUNER_DEVICE_STATUS                	= 0x07,
    E_MSG_TUNER_STEP_DEC                     	= 0x06,
    E_MSG_TUNER_STEP_INC                     	= 0x05,
//---------Vendor Specific -------------------------
    //E_MSG_CEC_VERSION                      	= 0x9E,       	//1.3a
    //E_MSG_GET_CEC_VERSION                    	= 0x9F,       	//1.3a
    E_MSG_DEVICE_VENDOR_ID                   	= 0x87,
    E_MSG_GIVE_DEVICE_VENDOR_ID              	= 0x8C,
    E_MSG_VENDOR_COMMAND                     	= 0x89,
    E_MSG_VENDOR_COMMAND_WITH_ID             	= 0xA0,      	//1.3a
    E_MSG_VENDOR_RC_BUT_DOWN                 	= 0x8A,
    E_MSG_VENDOR_RC_BUT_UP                   	= 0x8B,
//----- OSD Display --------------------------------
    E_MSG_SET_OSD_STRING                        = 0x64,
//----- Device OSD Name Transfer  -------------------------
    E_MSG_OSDNT_GIVE_OSD_NAME                   = 0x46,
    E_MSG_OSDNT_SET_OSD_NAME                    = 0x47,
//----- Device Menu Control ------------------------
    E_MSG_DMC_MENU_REQUEST                      = 0x8D,
    E_MSG_DMC_MENU_STATUS                       = 0x8E,
    E_MSG_UI_PRESS                              = 0x44,
    E_MSG_UI_RELEASE                            = 0x45,
//----- Remote Control Passthrough ----------------
	//E_MSG_UI_PRESS							= 0x44,
    //E_MSG_UI_RELEASE							= 0x45,
//----- Power Status  ------------------------------
    E_MSG_GIVE_DEVICE_POWER_STATUS           	= 0x8F,
    E_MSG_REPORT_POWER_STATUS                	= 0x90,
//----- General Protocal Message ------------------
    E_MSG_ABORT_MESSAGE                         = 0xFF,			//Abort msg
    E_MSG_FEATURE_ABORT                         = 0x00,			//Feature Abort
//----- System Audio Control ----------------------
    E_MSG_ARC_GIVE_AUDIO_STATUS                 = 0x71,
    E_MSG_ARC_GIVE_SYSTEM_AUDIO_MODE_STATUS     = 0x7D,
    E_MSG_ARC_REPORT_AUDIO_STATUS               = 0x7A,
    E_MSG_ARC_SET_SYSTEM_AUDIO_MODE             = 0x72,
    E_MSG_ARC_SYSTEM_AUDIO_MODE_REQUEST         = 0x70,
    E_MSG_ARC_SYSTEM_AUDIO_MODE_STATUS          = 0x7E,
    E_MSG_ARC_SET_AUDIO_RATE                    = 0x9A,
//----- Audio Return Channel  Control -------------
    E_MSG_ARC_INITIATE_ARC                      = 0xC0,
    E_MSG_ARC_REPORT_ARC_INITIATED              = 0xC1,
    E_MSG_ARC_REPORT_ARC_TERMINATED             = 0xC2,
    E_MSG_ARC_REQUEST_ARC_INITATION             = 0xC3,
    E_MSG_ARC_REQUEST_ARC_TERMINATION           = 0xC4,
    E_MSG_ARC_TERMINATED_ARC                    = 0xC5,

	E_MSG_CDC_MESSAGE                           = 0xF8,
} _cec_op_code_;

typedef enum _msg_info_
{
	MSG_NULL = 0,
	MSG_TX = 1,
	MSG_RX = 2,
}_msg_info_;

typedef struct _cec_msg_
{
    enum _msg_info_ info;
    int addr;
    enum _cec_op_code_ opcode;
    int msg_data[14];
    int msg_len;
} _cec_msg_;

typedef struct _cec_dev_map_
{
	int cec_dev_logicaddr;
	unsigned int cec_dev_phyaddr;
	int cec_dev_type;
	char cec_dev_name[14];
}_cec_dev_map_;

typedef struct _cec_msg_queue_
{
    struct _cec_msg_ msg[CEC_MSG_QUEUE_SIZE];
    int head;
    int end;
} _cec_msg_queue_;



typedef enum _cec_status
{
    E_CEC_FEATURE_ABORT = 0x00,
    E_CEC_RX_SUCCESS    = 0x01,
    E_CEC_TX_SUCCESS    = 0x02,
    E_CEC_RF            = 0x04,
    E_CEC_LOST_ABT      = 0x08,
    E_CEC_BIT_SHORT     = 0x10,
    E_CEC_BIT_LONG      = 0x20,
    E_CEC_NACK          = 0x40,
    E_CEC_SYSTEM_BUSY   = 0x80,
} _cec_status;


//#include <linux/tvin/tvin.h>
//#include "../tvin_global.h"
//#include "../tvin_format_table.h"

extern void dump_cec_message(int all);
extern void cec_dump_dev_map(void);
extern void clean_cec_message(void);
extern int cec_init(void);
extern void cec_state(bool cec_rx);
extern int cec_handler(bool get_msg, bool get_ack);
extern int test_cec(int flag);
extern int hdmirx_cec_rx_monitor(void);
extern int hdmirx_cec_tx_monitor(void);

#endif
#endif
