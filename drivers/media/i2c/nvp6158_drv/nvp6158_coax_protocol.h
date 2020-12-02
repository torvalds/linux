// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
*
*  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
*  Module		: Raptor3 Device Driver
*  Description	: coax_protocol.h
*  Author		:
*  Date         :
*  Version		: Version 1.0
*
********************************************************************************
*  History      :
*
*
********************************************************************************/
#ifndef _RAPTOR3_COAX_PROTOCOL_
#define _RAPTOR3_COAX_PROTOCOL_

#include "nvp6158_common.h"

//#define BANK1 0x01
//#define BANK2 0x02
//#define BANK3 0x03
//#define BANKC 0x0C

#define FW_SUCCESS 0
#define FW_FAILURE -1

#define DBG_TX_INIT_PRINT 0
#define DBG_TX_CMD_PRINT 0
#define DBG_RX_INIT_PRINT 0

typedef enum NC_COAX_CMD_DEF
{
	COAX_CMD_UNKNOWN = 0,
	COAX_CMD_IRIS_INC,
	COAX_CMD_IRIS_DEC,
	COAX_CMD_FOCUS_INC,
	COAX_CMD_FOCUS_DEC,
	COAX_CMD_ZOOM_INC,
	COAX_CMD_ZOOM_DEC,
	COAX_CMD_OSD_ON,
	COAX_CMD_PTZ_UP,
	COAX_CMD_PTZ_DOWN,
	COAX_CMD_PTZ_LEFT,
	COAX_CMD_PTZ_RIGHT,
	COAX_CMD_OSD_ENTER,
	COAX_CMD_SPECIAL_FW,
	COAX_CMD_SPECIAL_CAMEQ,
	COAX_CMD_SPECIAL_FPS,
	COAX_CMD_SPECIAL_MOTION,
	COAX_CMD_SPECIAL_3DNR_REQUEST,
	COAX_CMD_TVI_DOWNSTREAM_REQUEST,
	COAX_CMD_OSD_UP,
	COAX_CMD_OSD_DOWN,
	COAX_CMD_OSD_LEFT,
	COAX_CMD_OSD_RIGHT,
	COAX_CMD_STOP,

	COAX_CMD_MAX,

} NC_COAX_CMD_DEF;

/*=============================================================
 * Coaxial Test Structure[APP <-> DRV]
 ==============================================================*/
typedef struct NC_VD_COAX_TEST_STR{
	 unsigned char ch;
	 unsigned char chip_num;
	 unsigned char bank;
	 unsigned char data_addr;
	 unsigned char param;

	 unsigned char rx_src;             //B5/6/7/8 0x7C
	 unsigned char rx_slice_lev;       //B5/6/7/8 0x7D
	 unsigned char tx_baud;            //B3/4 0x00/80
	 unsigned char tx_pel_baud;        //B3/4 0x02/82
	 unsigned char tx_line_pos0;       //B3/4 0x03/83
	 unsigned char tx_line_pos1;       //B3/4 0x04/84
	 unsigned char tx_pel_line_pos0;   //B3/4 0x07/87
	 unsigned char tx_pel_line_pos1;   //B3/4 0x08/88
	 unsigned char tx_line_count;      //B3/4 0x05/85
	 unsigned char tx_line_count_max;  //B3/4 0x0A/8A
	 unsigned char tx_mode;            //B3/4 0x0B/8B
	 unsigned char tx_sync_pos0;       //B3/4 0x0D/8D
	 unsigned char tx_sync_pos1;       //B3/4 0x0E/8E
	 unsigned char tx_even;            //B3/4 0x2F/AF
	 unsigned char tx_zero_length;     //B3/4 0x0C/
} NC_VD_COAX_TEST_STR;

typedef struct NC_VD_COAX_BANK_DUMP_STR{
	 unsigned char ch;
	 unsigned char vd_dev;
	 unsigned char bank;

	 unsigned char rx_pelco_data[256];

} NC_VD_COAX_BANK_DUMP_STR;

/*=============================================================
 * Coaxial UP/Down Stream Initialize Structure[APP -> DRV]
 ==============================================================*/
typedef struct NC_VD_COAX_STR{
	 char *name;
	 unsigned char ch;
	 unsigned char vd_dev;
	 unsigned char param;
	 NC_FORMAT_STANDARD format_standard;
	 NC_FORMAT_RESOLUTION format_resolution;
	 NC_FORMAT_FPS format_fps;
	 NC_VIVO_CH_FORMATDEF vivo_fmt;
	 NC_COAX_CMD_DEF cmd;

	 unsigned char rx_pelco_data[8];
	 unsigned char rx_data1[8];
	 unsigned char rx_data2[8];
	 unsigned char rx_data3[8];
	 unsigned char rx_data4[8];
	 unsigned char rx_data5[8];
	 unsigned char rx_data6[8];

} NC_VD_COAX_STR;

typedef struct _nvp6158_coax_str{
	unsigned char ch;
	unsigned char param;
	NC_VIVO_CH_FORMATDEF fmt_def;
	NC_COAX_CMD_DEF cmd;
	unsigned char rx_pelco_data[8];
	unsigned char rx_data1[8];
	unsigned char rx_data2[8];
	unsigned char rx_data3[8];
	unsigned char rx_data4[8];
	unsigned char rx_data5[8];
	unsigned char rx_data6[8];

} nvp6158_coax_str;

/*=============================================================
 * COAX FW Upgrade
 ==============================================================*/
typedef struct __file_information
{
	unsigned int	channel;                // FirmUP Channel
	unsigned int	cp_mode;                // Channel Format
  	unsigned char 	filename[64];           //
  	unsigned char 	filePullname[64+32];    // FirmUP FileNmae
  	unsigned int	filesize;
  	unsigned int	filechecksum;		// (sum of file&0x0000FFFFF)
  	unsigned int	currentpacketnum;	// current packet sequnce number(0,1,2........)
  	unsigned int	filepacketnum;		// file packet number = (total size/128bytes), if remain exist, file packet number++
  	unsigned char 	onepacketbuf[128+32];

  	unsigned int	currentFileOffset;	// Current file offset
  	unsigned int	readsize;		// currnet read size

  	unsigned int	receive_addr;

  	unsigned int	ispossiblefirmup[16]; 	// is it possible to update firmware?
  	int 		result;

  	int		appstatus[16];		// Application status

} FIRMWARE_UP_FILE_INFO, *PFIRMWARE_UP_FILE_INFO;

// Coaxial UP Stream Function
void nvp6158_coax_tx_init( nvp6158_coax_str *ps_coax_str );     // Coax Tx : Initialize
void nvp6158_coax_tx_cmd_send( nvp6158_coax_str *ps_coax_str ); // Coax Tx : Command Send

int nvp6158_coax_tx_16bit_init( nvp6158_coax_str *ps_coax_str );
void nvp6158_coax_tx_16bit_cmd_send( nvp6158_coax_str *ps_coax_str );
void nvp6158_coax_tx_cvi_new_cmd_send( nvp6158_coax_str *ps_coax_str );

// Coaxial Down Stream Function
void nvp6158_coax_rx_init( nvp6158_coax_str *ps_coax_str );          	// Coax Rx : Initialize
void nvp6158_coax_rx_data_get( nvp6158_coax_str *coax_rx );      	// Coax Rx : All Rx Buffer read
void nvp6158_coax_rx_buffer_clear( nvp6158_coax_str *ps_coax_str );  	// Coax Rx : Rx Buffer Clear
void nvp6158_coax_rx_deinit( nvp6158_coax_str *ps_coax_str );        	// Coax Rx : 3x63 Set[ 1 -> 0 ]

// Coaxial FW Update Function
void nvp6158_coax_fw_ready_header_check_from_isp_recv(void *p_param);
void nvp6158_coax_fw_ready_cmd_to_isp_send(void *p_param);                // 1.1 FW Update Ready Command Send
void nvp6158_coax_fw_ready_cmd_ack_from_isp_recv(void *p_param);          // 1.2 FW Update Ready ACK
void nvp6158_coax_fw_start_cmd_to_isp_send( void *p_param );              // 2.1 FW Update Start Command Send
void nvp6158_coax_fw_start_cmd_ack_from_isp_recv( void *p_param );        // 2.2 FW Update Start ACK
void nvp6158_coax_fw_one_packet_data_to_isp_send( void *p_param );        // 3.1 FW Update One Packet Data Send
void nvp6158_coax_fw_one_packet_data_ack_from_isp_recv( void *p_param );  // 3.2 FW Update One Packet Data ACK
void nvp6158_coax_fw_end_cmd_to_isp_send( void *p_param );                // 4.1 FW Update End Command Send
void nvp6158_coax_fw_end_cmd_ack_from_isp_recv( void *p_param );          // 4.2 FW Update End ACK

// Coaxial Option
void nvp6158_coax_option_rt_nrt_mode_change_set(void *p_param);    // RT, NRT Mode change

// Coaxial Test Function
void nvp6158_coax_test_tx_init_read(NC_VD_COAX_TEST_STR *coax_tx_mode);    // Coax Test : Tx Init Read
void nvp6158_coax_test_data_set(NC_VD_COAX_TEST_STR *coax_data);           // Coax Test : 1byte Data write
void nvp6158_coax_test_data_get(NC_VD_COAX_TEST_STR *coax_data);           // Coax Test : 1byte Data read
void nvp6158_coax_test_Bank_dump_get(NC_VD_COAX_BANK_DUMP_STR *coax_data); // Bank Dump
unsigned char nvp6158_coax_acp_isp_read(unsigned char ch, unsigned int reg_addr);
unsigned char nvp6158_coax_acp_isp_write(unsigned char ch, unsigned int reg_addr, unsigned char reg_data);

#endif
/********************************************************************
 *  End of file
 ********************************************************************/
