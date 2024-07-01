/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CZ.NIC's Turris Omnia MCU driver
 *
 * 2024 by Marek Behún <kabel@kernel.org>
 */

#ifndef __TURRIS_OMNIA_MCU_H
#define __TURRIS_OMNIA_MCU_H

#include <linux/if_ether.h>
#include <linux/types.h>
#include <asm/byteorder.h>

struct i2c_client;

struct omnia_mcu {
	struct i2c_client *client;
	const char *type;
	u32 features;

	/* board information */
	u64 board_serial_number;
	u8 board_first_mac[ETH_ALEN];
	u8 board_revision;
};

int omnia_cmd_write_read(const struct i2c_client *client,
			 void *cmd, unsigned int cmd_len,
			 void *reply, unsigned int reply_len);

static inline int omnia_cmd_read(const struct i2c_client *client, u8 cmd,
				 void *reply, unsigned int len)
{
	return omnia_cmd_write_read(client, &cmd, 1, reply, len);
}

static inline int omnia_cmd_read_u32(const struct i2c_client *client, u8 cmd,
				     u32 *dst)
{
	__le32 reply;
	int err;

	err = omnia_cmd_read(client, cmd, &reply, sizeof(reply));
	if (err)
		return err;

	*dst = le32_to_cpu(reply);

	return 0;
}

static inline int omnia_cmd_read_u16(const struct i2c_client *client, u8 cmd,
				     u16 *dst)
{
	__le16 reply;
	int err;

	err = omnia_cmd_read(client, cmd, &reply, sizeof(reply));
	if (err)
		return err;

	*dst = le16_to_cpu(reply);

	return 0;
}

static inline int omnia_cmd_read_u8(const struct i2c_client *client, u8 cmd,
				    u8 *reply)
{
	return omnia_cmd_read(client, cmd, reply, sizeof(*reply));
}

#endif /* __TURRIS_OMNIA_MCU_H */
