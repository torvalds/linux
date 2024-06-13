/* SPDX-License-Identifier: GPL-2.0 */
#ifndef	_CM4040_H_
#define	_CM4040_H_

#define	CM_MAX_DEV		4

#define	DEVICE_NAME		"cmx"
#define	MODULE_NAME		"cm4040_cs"

#define REG_OFFSET_BULK_OUT      0
#define REG_OFFSET_BULK_IN       0
#define REG_OFFSET_BUFFER_STATUS 1
#define REG_OFFSET_SYNC_CONTROL  2

#define BSR_BULK_IN_FULL  0x02
#define BSR_BULK_OUT_FULL 0x01

#define SCR_HOST_TO_READER_START 0x80
#define SCR_ABORT                0x40
#define SCR_EN_NOTIFY            0x20
#define SCR_ACK_NOTIFY           0x10
#define SCR_READER_TO_HOST_DONE  0x08
#define SCR_HOST_TO_READER_DONE  0x04
#define SCR_PULSE_INTERRUPT      0x02
#define SCR_POWER_DOWN           0x01


#define  CMD_PC_TO_RDR_ICCPOWERON       0x62
#define  CMD_PC_TO_RDR_GETSLOTSTATUS    0x65
#define  CMD_PC_TO_RDR_ICCPOWEROFF      0x63
#define  CMD_PC_TO_RDR_SECURE           0x69
#define  CMD_PC_TO_RDR_GETPARAMETERS    0x6C
#define  CMD_PC_TO_RDR_RESETPARAMETERS  0x6D
#define  CMD_PC_TO_RDR_SETPARAMETERS    0x61
#define  CMD_PC_TO_RDR_XFRBLOCK         0x6F
#define  CMD_PC_TO_RDR_ESCAPE           0x6B
#define  CMD_PC_TO_RDR_ICCCLOCK         0x6E
#define  CMD_PC_TO_RDR_TEST_SECURE      0x74
#define  CMD_PC_TO_RDR_OK_SECURE        0x89


#define  CMD_RDR_TO_PC_SLOTSTATUS         0x81
#define  CMD_RDR_TO_PC_DATABLOCK          0x80
#define  CMD_RDR_TO_PC_PARAMETERS         0x82
#define  CMD_RDR_TO_PC_ESCAPE             0x83
#define  CMD_RDR_TO_PC_OK_SECURE          0x89

#endif	/* _CM4040_H_ */
