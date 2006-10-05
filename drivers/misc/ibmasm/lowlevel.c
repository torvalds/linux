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

#include "ibmasm.h"
#include "lowlevel.h"
#include "i2o.h"
#include "dot_command.h"
#include "remote.h"

static struct i2o_header header = I2O_HEADER_TEMPLATE;


int ibmasm_send_i2o_message(struct service_processor *sp)
{
	u32 mfa;
	unsigned int command_size;
	struct i2o_message *message;
	struct command *command = sp->current_command;

	mfa = get_mfa_inbound(sp->base_address);
	if (!mfa)
		return 1;

	command_size = get_dot_command_size(command->buffer);
	header.message_size = outgoing_message_size(command_size);

	message = get_i2o_message(sp->base_address, mfa);

	memcpy_toio(&message->header, &header, sizeof(struct i2o_header));
	memcpy_toio(&message->data, command->buffer, command_size);

	set_mfa_inbound(sp->base_address, mfa);

	return 0;
}

irqreturn_t ibmasm_interrupt_handler(int irq, void * dev_id)
{
	u32	mfa;
	struct service_processor *sp = (struct service_processor *)dev_id;
	void __iomem *base_address = sp->base_address;
	char tsbuf[32];

	if (!sp_interrupt_pending(base_address))
		return IRQ_NONE;

	dbg("respond to interrupt at %s\n", get_timestamp(tsbuf));

	if (mouse_interrupt_pending(sp)) {
		ibmasm_handle_mouse_interrupt(sp);
		clear_mouse_interrupt(sp);
	}

	mfa = get_mfa_outbound(base_address);
	if (valid_mfa(mfa)) {
		struct i2o_message *msg = get_i2o_message(base_address, mfa);
		ibmasm_receive_message(sp, &msg->data, incoming_data_size(msg));
	} else
		dbg("didn't get a valid MFA\n");

	set_mfa_outbound(base_address, mfa);
	dbg("finished interrupt at   %s\n", get_timestamp(tsbuf));

	return IRQ_HANDLED;
}
