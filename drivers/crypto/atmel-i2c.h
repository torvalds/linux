/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2017, Microchip Technology Inc.
 * Author: Tudor Ambarus <tudor.ambarus@microchip.com>
 */

#ifndef __ATMEL_I2C_H__
#define __ATMEL_I2C_H__

#include <linux/hw_random.h>
#include <linux/types.h>

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
#define RANDOM_RSP_SIZE			(32 + CMD_OVERHEAD_SIZE)
#define MAX_RSP_SIZE			GENKEY_RSP_SIZE

/**
 * atmel_i2c_cmd - structure used for communicating with the device.
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
struct atmel_i2c_cmd {
	u8 word_addr;
	u8 count;
	u8 opcode;
	u8 param1;
	__le16 param2;
	u8 data[MAX_RSP_SIZE];
	u8 msecs;
	u16 rxsize;
} __packed;

/* Status/Error codes */
#define STATUS_SIZE			0x04
#define STATUS_NOERR			0x00
#define STATUS_WAKE_SUCCESSFUL		0x11

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
#define MAX_EXEC_TIME_RANDOM		50

/* Command opcode */
#define OPCODE_ECDH			0x43
#define OPCODE_GENKEY			0x40
#define OPCODE_READ			0x02
#define OPCODE_RANDOM			0x1b

/* Definitions for the READ Command */
#define READ_COUNT			7

/* Definitions for the RANDOM Command */
#define RANDOM_COUNT			7

/* Definitions for the GenKey Command */
#define GENKEY_COUNT			7
#define GENKEY_MODE_PRIVATE		0x04

/* Definitions for the ECDH Command */
#define ECDH_COUNT			71
#define ECDH_PREFIX_MODE		0x00

/* Used for binding tfm objects to i2c clients. */
struct atmel_ecc_driver_data {
	struct list_head i2c_client_list;
	spinlock_t i2c_list_lock;
} ____cacheline_aligned;

/**
 * atmel_i2c_client_priv - i2c_client private data
 * @client              : pointer to i2c client device
 * @i2c_client_list_node: part of i2c_client_list
 * @lock                : lock for sending i2c commands
 * @wake_token          : wake token array of zeros
 * @wake_token_sz       : size in bytes of the wake_token
 * @tfm_count           : number of active crypto transformations on i2c client
 *
 * Reads and writes from/to the i2c client are sequential. The first byte
 * transmitted to the device is treated as the byte size. Any attempt to send
 * more than this number of bytes will cause the device to not ACK those bytes.
 * After the host writes a single command byte to the input buffer, reads are
 * prohibited until after the device completes command execution. Use a mutex
 * when sending i2c commands.
 */
struct atmel_i2c_client_priv {
	struct i2c_client *client;
	struct list_head i2c_client_list_node;
	struct mutex lock;
	u8 wake_token[WAKE_TOKEN_MAX_SIZE];
	size_t wake_token_sz;
	atomic_t tfm_count ____cacheline_aligned;
	struct hwrng hwrng;
};

/**
 * atmel_i2c_work_data - data structure representing the work
 * @ctx : transformation context.
 * @cbk : pointer to a callback function to be invoked upon completion of this
 *        request. This has the form:
 *        callback(struct atmel_i2c_work_data *work_data, void *areq, u8 status)
 *        where:
 *        @work_data: data structure representing the work
 *        @areq     : optional pointer to an argument passed with the original
 *                    request.
 *        @status   : status returned from the i2c client device or i2c error.
 * @areq: optional pointer to a user argument for use at callback time.
 * @work: describes the task to be executed.
 * @cmd : structure used for communicating with the device.
 */
struct atmel_i2c_work_data {
	void *ctx;
	struct i2c_client *client;
	void (*cbk)(struct atmel_i2c_work_data *work_data, void *areq,
		    int status);
	void *areq;
	struct work_struct work;
	struct atmel_i2c_cmd cmd;
};

int atmel_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);

void atmel_i2c_enqueue(struct atmel_i2c_work_data *work_data,
		       void (*cbk)(struct atmel_i2c_work_data *work_data,
				   void *areq, int status),
		       void *areq);

int atmel_i2c_send_receive(struct i2c_client *client, struct atmel_i2c_cmd *cmd);

void atmel_i2c_init_read_cmd(struct atmel_i2c_cmd *cmd);
void atmel_i2c_init_random_cmd(struct atmel_i2c_cmd *cmd);
void atmel_i2c_init_genkey_cmd(struct atmel_i2c_cmd *cmd, u16 keyid);
int atmel_i2c_init_ecdh_cmd(struct atmel_i2c_cmd *cmd,
			    struct scatterlist *pubkey);

#endif /* __ATMEL_I2C_H__ */
