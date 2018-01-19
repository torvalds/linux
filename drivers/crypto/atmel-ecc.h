/*
 * Copyright (c) 2017, Microchip Technology Inc.
 * Author: Tudor Ambarus <tudor.ambarus@microchip.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __ATMEL_ECC_H__
#define __ATMEL_ECC_H__

#define ATMEL_ECC_PRIORITY		300

#define COMMAND				0x03 /* packet function */
#define SLEEP_TOKEN			0x01
#define WAKE_TOKEN_MAX_SIZE		8

/* Definitions of Data and Command sizes */
#define WORD_ADDR_SIZE			1
#define COUNT_SIZE			1
#define CRC_SIZE			2
#define CMD_OVERHEAD_SIZE		(COUNT_SIZE + CRC_SIZE)

/* size in bytes of the n prime */
#define ATMEL_ECC_NIST_P256_N_SIZE	32
#define ATMEL_ECC_PUBKEY_SIZE		(2 * ATMEL_ECC_NIST_P256_N_SIZE)

#define STATUS_RSP_SIZE			4
#define ECDH_RSP_SIZE			(32 + CMD_OVERHEAD_SIZE)
#define GENKEY_RSP_SIZE			(ATMEL_ECC_PUBKEY_SIZE + \
					 CMD_OVERHEAD_SIZE)
#define READ_RSP_SIZE			(4 + CMD_OVERHEAD_SIZE)
#define MAX_RSP_SIZE			GENKEY_RSP_SIZE

/**
 * atmel_ecc_cmd - structure used for communicating with the device.
 * @word_addr: indicates the function of the packet sent to the device. This
 *             byte should have a value of COMMAND for normal operation.
 * @count    : number of bytes to be transferred to (or from) the device.
 * @opcode   : the command code.
 * @param1   : the first parameter; always present.
 * @param2   : the second parameter; always present.
 * @data     : optional remaining input data. Includes a 2-byte CRC.
 * @rxsize   : size of the data received from i2c client.
 * @msecs    : command execution time in milliseconds
 */
struct atmel_ecc_cmd {
	u8 word_addr;
	u8 count;
	u8 opcode;
	u8 param1;
	u16 param2;
	u8 data[MAX_RSP_SIZE];
	u8 msecs;
	u16 rxsize;
} __packed;

/* Status/Error codes */
#define STATUS_SIZE			0x04
#define STATUS_NOERR			0x00
#define STATUS_WAKE_SUCCESSFUL		0x11

static const struct {
	u8 value;
	const char *error_text;
} error_list[] = {
	{ 0x01, "CheckMac or Verify miscompare" },
	{ 0x03, "Parse Error" },
	{ 0x05, "ECC Fault" },
	{ 0x0F, "Execution Error" },
	{ 0xEE, "Watchdog about to expire" },
	{ 0xFF, "CRC or other communication error" },
};

/* Definitions for eeprom organization */
#define CONFIG_ZONE			0

/* Definitions for Indexes common to all commands */
#define RSP_DATA_IDX			1 /* buffer index of data in response */
#define DATA_SLOT_2			2 /* used for ECDH private key */

/* Definitions for the device lock state */
#define DEVICE_LOCK_ADDR		0x15
#define LOCK_VALUE_IDX			(RSP_DATA_IDX + 2)
#define LOCK_CONFIG_IDX			(RSP_DATA_IDX + 3)

/*
 * Wake High delay to data communication (microseconds). SDA should be stable
 * high for this entire duration.
 */
#define TWHI_MIN			1500
#define TWHI_MAX			1550

/* Wake Low duration */
#define TWLO_USEC			60

/* Command execution time (milliseconds) */
#define MAX_EXEC_TIME_ECDH		58
#define MAX_EXEC_TIME_GENKEY		115
#define MAX_EXEC_TIME_READ		1

/* Command opcode */
#define OPCODE_ECDH			0x43
#define OPCODE_GENKEY			0x40
#define OPCODE_READ			0x02

/* Definitions for the READ Command */
#define READ_COUNT			7

/* Definitions for the GenKey Command */
#define GENKEY_COUNT			7
#define GENKEY_MODE_PRIVATE		0x04

/* Definitions for the ECDH Command */
#define ECDH_COUNT			71
#define ECDH_PREFIX_MODE		0x00

#endif /* __ATMEL_ECC_H__ */
