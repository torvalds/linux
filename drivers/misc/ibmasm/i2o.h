/*
 * IBM ASM Service Processor Device Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2004
 *
 * Author: Max Asböck <amax@us.ibm.com>
 *
 */

#pragma pack(1)
struct i2o_header {
	u8	version;
	u8	message_flags;
	u16	message_size;
	u8	target;
	u8	initiator_and_target;
	u8	initiator;
	u8	function;
	u32	initiator_context;
};
#pragma pack()

#define I2O_HEADER_TEMPLATE \
      { .version              = 0x01, \
	.message_flags        = 0x00, \
	.function             = 0xFF, \
	.initiator            = 0x00, \
	.initiator_and_target = 0x40, \
	.target               = 0x00, \
	.initiator_context    = 0x0 }

#define I2O_MESSAGE_SIZE	0x1000
#define I2O_COMMAND_SIZE	(I2O_MESSAGE_SIZE - sizeof(struct i2o_header))

#pragma pack(1)
struct i2o_message {
	struct i2o_header	header;
	void			*data;
};
#pragma pack()

static inline unsigned short outgoing_message_size(unsigned int data_size)
{
	unsigned int size;
	unsigned short i2o_size;

	if (data_size > I2O_COMMAND_SIZE)
		data_size = I2O_COMMAND_SIZE;

	size = sizeof(struct i2o_header) + data_size;

	i2o_size = size / sizeof(u32);

	if (size % sizeof(u32))
	       i2o_size++;

	return i2o_size;
}

static inline u32 incoming_data_size(struct i2o_message *i2o_message)
{
	return (sizeof(u32) * i2o_message->header.message_size);
}
