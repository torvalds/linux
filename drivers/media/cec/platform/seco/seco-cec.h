/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * SECO X86 Boards CEC register defines
 *
 * Author:  Ettore Chimenti <ek5.chimenti@gmail.com>
 * Copyright (C) 2018, SECO Spa.
 * Copyright (C) 2018, Aidilab Srl.
 */

#ifndef __SECO_CEC_H__
#define __SECO_CEC_H__

#define SECOCEC_MAX_ADDRS		1
#define SECOCEC_DEV_NAME		"secocec"
#define SECOCEC_LATEST_FW		0x0f0b

#define SMBTIMEOUT			0xfff
#define SMB_POLL_UDELAY			10

#define SMBUS_WRITE			0
#define SMBUS_READ			1

#define CMD_BYTE_DATA			0
#define CMD_WORD_DATA			1

/*
 * SMBus definitons for Braswell
 */

#define BRA_DONE_STATUS			BIT(7)
#define BRA_INUSE_STS			BIT(6)
#define BRA_FAILED_OP			BIT(4)
#define BRA_BUS_ERR			BIT(3)
#define BRA_DEV_ERR			BIT(2)
#define BRA_INTR			BIT(1)
#define BRA_HOST_BUSY			BIT(0)
#define BRA_HSTS_ERR_MASK   (BRA_FAILED_OP | BRA_BUS_ERR | BRA_DEV_ERR)

#define BRA_PEC_EN			BIT(7)
#define BRA_START			BIT(6)
#define BRA_LAST__BYTE			BIT(5)
#define BRA_INTREN			BIT(0)
#define BRA_SMB_CMD			(7 << 2)
#define BRA_SMB_CMD_QUICK		(0 << 2)
#define BRA_SMB_CMD_BYTE		(1 << 2)
#define BRA_SMB_CMD_BYTE_DATA		(2 << 2)
#define BRA_SMB_CMD_WORD_DATA		(3 << 2)
#define BRA_SMB_CMD_PROCESS_CALL	(4 << 2)
#define BRA_SMB_CMD_BLOCK		(5 << 2)
#define BRA_SMB_CMD_I2CREAD		(6 << 2)
#define BRA_SMB_CMD_BLOCK_PROCESS	(7 << 2)

#define BRA_SMB_BASE_ADDR  0x2040
#define HSTS               (BRA_SMB_BASE_ADDR + 0)
#define HCNT               (BRA_SMB_BASE_ADDR + 2)
#define HCMD               (BRA_SMB_BASE_ADDR + 3)
#define XMIT_SLVA          (BRA_SMB_BASE_ADDR + 4)
#define HDAT0              (BRA_SMB_BASE_ADDR + 5)
#define HDAT1              (BRA_SMB_BASE_ADDR + 6)

/*
 * Microcontroller Address
 */

#define SECOCEC_MICRO_ADDRESS		0x40

/*
 * STM32 SMBus Registers
 */

#define SECOCEC_VERSION			0x00
#define SECOCEC_ENABLE_REG_1		0x01
#define SECOCEC_ENABLE_REG_2		0x02
#define SECOCEC_STATUS_REG_1		0x03
#define SECOCEC_STATUS_REG_2		0x04

#define SECOCEC_STATUS			0x28
#define SECOCEC_DEVICE_LA		0x29
#define SECOCEC_READ_OPERATION_ID	0x2a
#define SECOCEC_READ_DATA_LENGTH	0x2b
#define SECOCEC_READ_DATA_00		0x2c
#define SECOCEC_READ_DATA_02		0x2d
#define SECOCEC_READ_DATA_04		0x2e
#define SECOCEC_READ_DATA_06		0x2f
#define SECOCEC_READ_DATA_08		0x30
#define SECOCEC_READ_DATA_10		0x31
#define SECOCEC_READ_DATA_12		0x32
#define SECOCEC_READ_BYTE0		0x33
#define SECOCEC_WRITE_OPERATION_ID	0x34
#define SECOCEC_WRITE_DATA_LENGTH	0x35
#define SECOCEC_WRITE_DATA_00		0x36
#define SECOCEC_WRITE_DATA_02		0x37
#define SECOCEC_WRITE_DATA_04		0x38
#define SECOCEC_WRITE_DATA_06		0x39
#define SECOCEC_WRITE_DATA_08		0x3a
#define SECOCEC_WRITE_DATA_10		0x3b
#define SECOCEC_WRITE_DATA_12		0x3c
#define SECOCEC_WRITE_BYTE0		0x3d

#define SECOCEC_IR_READ_DATA		0x3e

/*
 * IR
 */

#define SECOCEC_IR_COMMAND_MASK		0x007F
#define SECOCEC_IR_COMMAND_SHL		0
#define SECOCEC_IR_ADDRESS_MASK		0x1F00
#define SECOCEC_IR_ADDRESS_SHL		8
#define SECOCEC_IR_TOGGLE_MASK		0x8000
#define SECOCEC_IR_TOGGLE_SHL		15

/*
 * Enabling register
 */

#define SECOCEC_ENABLE_REG_1_CEC		0x1000
#define SECOCEC_ENABLE_REG_1_IR			0x2000
#define SECOCEC_ENABLE_REG_1_IR_PASSTHROUGH	0x4000

/*
 * Status register
 */

#define SECOCEC_STATUS_REG_1_CEC	SECOCEC_ENABLE_REG_1_CEC
#define SECOCEC_STATUS_REG_1_IR		SECOCEC_ENABLE_REG_1_IR
#define SECOCEC_STATUS_REG_1_IR_PASSTHR	SECOCEC_ENABLE_REG_1_IR_PASSTHR

/*
 * Status data
 */

#define SECOCEC_STATUS_MSG_RECEIVED_MASK	BIT(0)
#define SECOCEC_STATUS_RX_ERROR_MASK		BIT(1)
#define SECOCEC_STATUS_MSG_SENT_MASK		BIT(2)
#define SECOCEC_STATUS_TX_ERROR_MASK		BIT(3)

#define SECOCEC_STATUS_TX_NACK_ERROR		BIT(4)
#define SECOCEC_STATUS_RX_OVERFLOW_MASK		BIT(5)

#endif /* __SECO_CEC_H__ */
