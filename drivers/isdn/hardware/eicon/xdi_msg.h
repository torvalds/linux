/* SPDX-License-Identifier: GPL-2.0 */
/* $Id: xdi_msg.h,v 1.1.2.2 2001/02/16 08:40:36 armin Exp $ */

#ifndef __DIVA_XDI_UM_CFG_MESSAGE_H__
#define __DIVA_XDI_UM_CFG_MESSAGE_H__

/*
  Definition of messages used to communicate between
  XDI device driver and user mode configuration utility
*/

/*
  As acknowledge one DWORD - card ordinal will be read from the card
*/
#define DIVA_XDI_UM_CMD_GET_CARD_ORDINAL	0

/*
  no acknowledge will be generated, memory block will be written in the
  memory at given offset
*/
#define DIVA_XDI_UM_CMD_WRITE_SDRAM_BLOCK	1

/*
  no acknowledge will be genatated, FPGA will be programmed
*/
#define DIVA_XDI_UM_CMD_WRITE_FPGA				2

/*
  As acknowledge block of SDRAM will be read in the user buffer
*/
#define DIVA_XDI_UM_CMD_READ_SDRAM				3

/*
  As acknowledge dword with serial number will be read in the user buffer
*/
#define DIVA_XDI_UM_CMD_GET_SERIAL_NR			4

/*
  As acknowledge struct consisting from 9 dwords with PCI info.
  dword[0...7] = 8 PCI BARS
  dword[9]		 = IRQ
*/
#define DIVA_XDI_UM_CMD_GET_PCI_HW_CONFIG	5

/*
  Reset of the board + activation of primary
  boot loader
*/
#define DIVA_XDI_UM_CMD_RESET_ADAPTER			6

/*
  Called after code download to start adapter
  at specified address
  Start does set new set of features due to fact that we not know
  if protocol features have changed
*/
#define DIVA_XDI_UM_CMD_START_ADAPTER			7

/*
  Stop adapter, called if user
  wishes to stop adapter without unload
  of the driver, to reload adapter with
  different protocol
*/
#define DIVA_XDI_UM_CMD_STOP_ADAPTER			8

/*
  Get state of current adapter
  Acknowledge is one dword with following values:
  0 - adapter ready for download
  1 - adapter running
  2 - adapter dead
  3 - out of service, driver should be restarted or hardware problem
*/
#define DIVA_XDI_UM_CMD_GET_CARD_STATE		9

/*
  Reads XLOG entry from the card
*/
#define DIVA_XDI_UM_CMD_READ_XLOG_ENTRY		10

/*
  Set untranslated protocol code features
*/
#define DIVA_XDI_UM_CMD_SET_PROTOCOL_FEATURES	11

typedef struct _diva_xdi_um_cfg_cmd_data_set_features {
	dword features;
} diva_xdi_um_cfg_cmd_data_set_features_t;

typedef struct _diva_xdi_um_cfg_cmd_data_start {
	dword offset;
	dword features;
} diva_xdi_um_cfg_cmd_data_start_t;

typedef struct _diva_xdi_um_cfg_cmd_data_write_sdram {
	dword ram_number;
	dword offset;
	dword length;
} diva_xdi_um_cfg_cmd_data_write_sdram_t;

typedef struct _diva_xdi_um_cfg_cmd_data_write_fpga {
	dword fpga_number;
	dword image_length;
} diva_xdi_um_cfg_cmd_data_write_fpga_t;

typedef struct _diva_xdi_um_cfg_cmd_data_read_sdram {
	dword ram_number;
	dword offset;
	dword length;
} diva_xdi_um_cfg_cmd_data_read_sdram_t;

typedef union _diva_xdi_um_cfg_cmd_data {
	diva_xdi_um_cfg_cmd_data_write_sdram_t write_sdram;
	diva_xdi_um_cfg_cmd_data_write_fpga_t write_fpga;
	diva_xdi_um_cfg_cmd_data_read_sdram_t read_sdram;
	diva_xdi_um_cfg_cmd_data_start_t start;
	diva_xdi_um_cfg_cmd_data_set_features_t features;
} diva_xdi_um_cfg_cmd_data_t;

typedef struct _diva_xdi_um_cfg_cmd {
	dword adapter;		/* Adapter number 1...N */
	dword command;
	diva_xdi_um_cfg_cmd_data_t command_data;
	dword data_length;	/* Plain binary data will follow */
} diva_xdi_um_cfg_cmd_t;

#endif
