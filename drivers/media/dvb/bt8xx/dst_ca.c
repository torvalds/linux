/*
	CA-driver for TwinHan DST Frontend/Card

	Copyright (C) 2004, 2005 Manu Abraham (manu@kromtek.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/



#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>

#include <linux/dvb/ca.h>
#include "dvbdev.h"
#include "dvb_frontend.h"

#include "dst_ca.h"
#include "dst_common.h"

static unsigned int verbose = 5;
module_param(verbose, int, 0644);
MODULE_PARM_DESC(verbose, "verbose startup messages, default is 1 (yes)");

static unsigned int debug = 1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug messages, default is 1 (yes)");

#define dprintk if (debug) printk

/*	Need some more work	*/
static int ca_set_slot_descr(void)
{
	/*	We could make this more graceful ?	*/
	return -EOPNOTSUPP;
}

/*	Need some more work	*/
static int ca_set_pid(void)
{
	/*	We could make this more graceful ?	*/
	return -EOPNOTSUPP;
}


static int put_checksum(u8 *check_string, int length)
{
	u8 i = 0, checksum = 0;

	if (verbose > 3) {
		dprintk("%s: ========================= Checksum calculation ===========================\n", __FUNCTION__);
		dprintk("%s: String Length=[0x%02x]\n", __FUNCTION__, length);

		dprintk("%s: String=[", __FUNCTION__);
	}
	while (i < length) {
		if (verbose > 3)
			dprintk(" %02x", check_string[i]);
		checksum += check_string[i];
		i++;
	}
	if (verbose > 3) {
		dprintk(" ]\n");
		dprintk("%s: Sum=[%02x]\n", __FUNCTION__, checksum);
	}
	check_string[length] = ~checksum + 1;
	if (verbose > 3) {
		dprintk("%s: Checksum=[%02x]\n", __FUNCTION__, check_string[length]);
		dprintk("%s: ==========================================================================\n", __FUNCTION__);
	}

	return 0;
}

static int dst_ci_command(struct dst_state* state, u8 * data, u8 *ca_string, u8 len, int read)
{
	u8 reply;

	dst_comm_init(state);
	msleep(65);

	if (write_dst(state, data, len)) {
		dprintk("%s: Write not successful, trying to recover\n", __FUNCTION__);
		dst_error_recovery(state);
		return -1;
	}

	if ((dst_pio_disable(state)) < 0) {
		dprintk("%s: DST PIO disable failed.\n", __FUNCTION__);
		return -1;
	}

	if (read_dst(state, &reply, GET_ACK) < 0) {
		dprintk("%s: Read not successful, trying to recover\n", __FUNCTION__);
		dst_error_recovery(state);
		return -1;
	}

	if (read) {
		if (! dst_wait_dst_ready(state, LONG_DELAY)) {
			dprintk("%s: 8820 not ready\n", __FUNCTION__);
			return -1;
		}

		if (read_dst(state, ca_string, 128) < 0) {	/*	Try to make this dynamic	*/
			dprintk("%s: Read not successful, trying to recover\n", __FUNCTION__);
			dst_error_recovery(state);
			return -1;
		}
	}

	return 0;
}


static int dst_put_ci(struct dst_state *state, u8 *data, int len, u8 *ca_string, int read)
{
	u8 dst_ca_comm_err = 0;

	while (dst_ca_comm_err < RETRIES) {
		dst_comm_init(state);
		if (verbose > 2)
			dprintk("%s: Put Command\n", __FUNCTION__);
		if (dst_ci_command(state, data, ca_string, len, read)) {	// If error
			dst_error_recovery(state);
			dst_ca_comm_err++; // work required here.
		}
		break;
	}

	return 0;
}



static int ca_get_app_info(struct dst_state *state)
{
	static u8 command[8] = {0x07, 0x40, 0x01, 0x00, 0x01, 0x00, 0x00, 0xff};

	put_checksum(&command[0], command[0]);
	if ((dst_put_ci(state, command, sizeof(command), state->messages, GET_REPLY)) < 0) {
		dprintk("%s: -->dst_put_ci FAILED !\n", __FUNCTION__);
		return -1;
	}
	if (verbose > 1) {
		dprintk("%s: -->dst_put_ci SUCCESS !\n", __FUNCTION__);

		dprintk("%s: ================================ CI Module Application Info ======================================\n", __FUNCTION__);
		dprintk("%s: Application Type=[%d], Application Vendor=[%d], Vendor Code=[%d]\n%s: Application info=[%s]\n",
			__FUNCTION__, state->messages[7], (state->messages[8] << 8) | state->messages[9],
			(state->messages[10] << 8) | state->messages[11], __FUNCTION__, (char *)(&state->messages[12]));
		dprintk("%s: ==================================================================================================\n", __FUNCTION__);
	}

	return 0;
}

static int ca_get_slot_caps(struct dst_state *state, struct ca_caps *p_ca_caps, void *arg)
{
	int i;
	u8 slot_cap[256];
	static u8 slot_command[8] = {0x07, 0x40, 0x02, 0x00, 0x02, 0x00, 0x00, 0xff};

	put_checksum(&slot_command[0], slot_command[0]);
	if ((dst_put_ci(state, slot_command, sizeof (slot_command), slot_cap, GET_REPLY)) < 0) {
		dprintk("%s: -->dst_put_ci FAILED !\n", __FUNCTION__);
		return -1;
	}
	if (verbose > 1)
		dprintk("%s: -->dst_put_ci SUCCESS !\n", __FUNCTION__);

	/*	Will implement the rest soon		*/

	if (verbose > 1) {
		dprintk("%s: Slot cap = [%d]\n", __FUNCTION__, slot_cap[7]);
		dprintk("===================================\n");
		for (i = 0; i < 8; i++)
			dprintk(" %d", slot_cap[i]);
		dprintk("\n");
	}

	p_ca_caps->slot_num = 1;
	p_ca_caps->slot_type = 1;
	p_ca_caps->descr_num = slot_cap[7];
	p_ca_caps->descr_type = 1;


	if (copy_to_user((struct ca_caps *)arg, p_ca_caps, sizeof (struct ca_caps))) {
		return -EFAULT;
	}

	return 0;
}

/*	Need some more work	*/
static int ca_get_slot_descr(struct dst_state *state, struct ca_msg *p_ca_message, void *arg)
{
	return -EOPNOTSUPP;
}


static int ca_get_slot_info(struct dst_state *state, struct ca_slot_info *p_ca_slot_info, void *arg)
{
	int i;
	static u8 slot_command[8] = {0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff};

	u8 *slot_info = state->rxbuffer;

	put_checksum(&slot_command[0], 7);
	if ((dst_put_ci(state, slot_command, sizeof (slot_command), slot_info, GET_REPLY)) < 0) {
		dprintk("%s: -->dst_put_ci FAILED !\n", __FUNCTION__);
		return -1;
	}
	if (verbose > 1)
		dprintk("%s: -->dst_put_ci SUCCESS !\n", __FUNCTION__);

	/*	Will implement the rest soon		*/

	if (verbose > 1) {
		dprintk("%s: Slot info = [%d]\n", __FUNCTION__, slot_info[3]);
		dprintk("===================================\n");
		for (i = 0; i < 8; i++)
			dprintk(" %d", slot_info[i]);
		dprintk("\n");
	}

	if (slot_info[4] & 0x80) {
		p_ca_slot_info->flags = CA_CI_MODULE_PRESENT;
		p_ca_slot_info->num = 1;
		p_ca_slot_info->type = CA_CI;
	}
	else if (slot_info[4] & 0x40) {
		p_ca_slot_info->flags = CA_CI_MODULE_READY;
		p_ca_slot_info->num = 1;
		p_ca_slot_info->type = CA_CI;
	}
	else {
		p_ca_slot_info->flags = 0;
	}

	if (copy_to_user((struct ca_slot_info *)arg, p_ca_slot_info, sizeof (struct ca_slot_info))) {
		return -EFAULT;
	}

	return 0;
}




static int ca_get_message(struct dst_state *state, struct ca_msg *p_ca_message, void *arg)
{
	u8 i = 0;
	u32 command = 0;

	if (copy_from_user(p_ca_message, (void *)arg, sizeof (struct ca_msg)))
		return -EFAULT;


	if (p_ca_message->msg) {
		if (verbose > 3)
			dprintk("Message = [%02x %02x %02x]\n", p_ca_message->msg[0], p_ca_message->msg[1], p_ca_message->msg[2]);

		for (i = 0; i < 3; i++) {
			command = command | p_ca_message->msg[i];
			if (i < 2)
				command = command << 8;
		}
		if (verbose > 3)
			dprintk("%s:Command=[0x%x]\n", __FUNCTION__, command);

		switch (command) {
			case CA_APP_INFO:
				memcpy(p_ca_message->msg, state->messages, 128);
				if (copy_to_user((void *)arg, p_ca_message, sizeof (struct ca_msg)) )
					return -EFAULT;
			break;
		}
	}

	return 0;
}

static int handle_dst_tag(struct dst_state *state, struct ca_msg *p_ca_message, struct ca_msg *hw_buffer, u32 length)
{
	if (state->dst_hw_cap & DST_TYPE_HAS_SESSION) {
		hw_buffer->msg[2] = p_ca_message->msg[1];		/*		MSB			*/
		hw_buffer->msg[3] = p_ca_message->msg[2];		/*		LSB			*/
	}
	else {
		hw_buffer->msg[0] = (length & 0xff) + 7;
		hw_buffer->msg[1] = 0x40;
		hw_buffer->msg[2] = 0x03;
		hw_buffer->msg[3] = 0x00;
		hw_buffer->msg[4] = 0x03;
		hw_buffer->msg[5] = length & 0xff;
		hw_buffer->msg[6] = 0x00;
	}
	return 0;
}


static int write_to_8820(struct dst_state *state, struct ca_msg *hw_buffer, u8 length, u8 reply)
{
	if ((dst_put_ci(state, hw_buffer->msg, length, hw_buffer->msg, reply)) < 0) {
		dprintk("%s: DST-CI Command failed.\n", __FUNCTION__);
		dprintk("%s: Resetting DST.\n", __FUNCTION__);
		rdc_reset_state(state);
		return -1;
	}
	if (verbose > 2)
		dprintk("%s: DST-CI Command succes.\n", __FUNCTION__);

	return 0;
}

u32 asn_1_decode(u8 *asn_1_array)
{
	u8 length_field = 0, word_count = 0, count = 0;
	u32 length = 0;

	length_field = asn_1_array[0];
	dprintk("%s: Length field=[%02x]\n", __FUNCTION__, length_field);
	if (length_field < 0x80) {
		length = length_field & 0x7f;
		dprintk("%s: Length=[%02x]\n", __FUNCTION__, length);
	} else {
		word_count = length_field & 0x7f;
		for (count = 0; count < word_count; count++) {
			length = (length | asn_1_array[count + 1]) << 8;
			dprintk("%s: Length=[%04x]\n", __FUNCTION__, length);
		}
	}
	return length;
}

static int init_buffer(u8 *buffer, u32 length)
{
	u32 i;
	for (i = 0; i < length; i++)
		buffer[i] = 0;

	return 0;
}

static int debug_string(u8 *msg, u32 length, u32 offset)
{
	u32 i;

	dprintk(" String=[ ");
	for (i = offset; i < length; i++)
		dprintk("%02x ", msg[i]);
	dprintk("]\n");

	return 0;
}

static int copy_string(u8 *destination, u8 *source, u32 dest_offset, u32 source_offset, u32 length)
{
	u32 i;
	dprintk("%s: Copying [", __FUNCTION__);
	for (i = 0; i < length; i++) {
		destination[i + dest_offset] = source[i + source_offset];
		dprintk(" %02x", source[i + source_offset]);
	}
	dprintk("]\n");

	return i;
}

static int modify_4_bits(u8 *message, u32 pos)
{
	message[pos] &= 0x0f;

	return 0;
}



static int ca_set_pmt(struct dst_state *state, struct ca_msg *p_ca_message, struct ca_msg *hw_buffer, u8 reply, u8 query)
{
	u32 length = 0, count = 0;
	u8 asn_1_words, program_header_length;
	u16 program_info_length = 0, es_info_length = 0;
	u32 hw_offset = 0, buf_offset = 0, i;
	u8 dst_tag_length;

	length = asn_1_decode(&p_ca_message->msg[3]);
	dprintk("%s: CA Message length=[%d]\n", __FUNCTION__, length);
	dprintk("%s: ASN.1 ", __FUNCTION__);
	debug_string(&p_ca_message->msg[4], length, 0); // length does not include tag and length

	init_buffer(hw_buffer->msg, length);
	handle_dst_tag(state, p_ca_message, hw_buffer, length);

	hw_offset = 7;
	asn_1_words = 1; // just a hack to test, should compute this one
	buf_offset = 3;
	program_header_length = 6;
	dst_tag_length = 7;

//	debug_twinhan_ca_params(state, p_ca_message, hw_buffer, reply, query, length, hw_offset, buf_offset);
//	dprintk("%s: Program Header(BUF)", __FUNCTION__);
//	debug_string(&p_ca_message->msg[4], program_header_length, 0);
//	dprintk("%s: Copying Program header\n", __FUNCTION__);
	copy_string(hw_buffer->msg, p_ca_message->msg, hw_offset, (buf_offset + asn_1_words), program_header_length);
	buf_offset += program_header_length, hw_offset += program_header_length;
	modify_4_bits(hw_buffer->msg, (hw_offset - 2));
	if (state->type_flags & DST_TYPE_HAS_INC_COUNT) {	// workaround
		dprintk("%s: Probably an ASIC bug !!!\n", __FUNCTION__);
		debug_string(hw_buffer->msg, (hw_offset + program_header_length), 0);
		hw_buffer->msg[hw_offset - 1] += 1;
	}

//	dprintk("%s: Program Header(HW), Count=[%d]", __FUNCTION__, count);
//	debug_string(hw_buffer->msg, hw_offset, 0);

	program_info_length =  ((program_info_length | (p_ca_message->msg[buf_offset - 1] & 0x0f)) << 8) | p_ca_message->msg[buf_offset];
	dprintk("%s: Program info length=[%02x]\n", __FUNCTION__, program_info_length);
	if (program_info_length) {
		count = copy_string(hw_buffer->msg, p_ca_message->msg, hw_offset, (buf_offset + 1), (program_info_length + 1) ); // copy next elem, not current
		buf_offset += count, hw_offset += count;
//		dprintk("%s: Program level ", __FUNCTION__);
//		debug_string(hw_buffer->msg, hw_offset, 0);
	}

	buf_offset += 1;// hw_offset += 1;
	for (i = buf_offset; i < length; i++) {
//		dprintk("%s: Stream Header ", __FUNCTION__);
		count = copy_string(hw_buffer->msg, p_ca_message->msg, hw_offset, buf_offset, 5);
		modify_4_bits(hw_buffer->msg, (hw_offset + 3));

		hw_offset += 5, buf_offset += 5, i += 4;
//		debug_string(hw_buffer->msg, hw_offset, (hw_offset - 5));
		es_info_length = ((es_info_length | (p_ca_message->msg[buf_offset - 1] & 0x0f)) << 8) | p_ca_message->msg[buf_offset];
		dprintk("%s: ES info length=[%02x]\n", __FUNCTION__, es_info_length);
		if (es_info_length) {
			// copy descriptors @ STREAM level
			dprintk("%s: Descriptors @ STREAM level...!!! \n", __FUNCTION__);
		}

	}
	hw_buffer->msg[length + dst_tag_length] = dst_check_sum(hw_buffer->msg, (length + dst_tag_length));
//	dprintk("%s: Total length=[%d], Checksum=[%02x]\n", __FUNCTION__, (length + dst_tag_length), hw_buffer->msg[length + dst_tag_length]);
	debug_string(hw_buffer->msg, (length + dst_tag_length + 1), 0);	// dst tags also
	write_to_8820(state, hw_buffer, (length + dst_tag_length + 1), reply);	// checksum

	return 0;
}


/*	Board supports CA PMT reply ?		*/
static int dst_check_ca_pmt(struct dst_state *state, struct ca_msg *p_ca_message, struct ca_msg *hw_buffer)
{
	int ca_pmt_reply_test = 0;

	/*	Do test board			*/
	/*	Not there yet but soon		*/


	/*	CA PMT Reply capable		*/
	if (ca_pmt_reply_test) {
		if ((ca_set_pmt(state, p_ca_message, hw_buffer, 1, GET_REPLY)) < 0) {
			dprintk("%s: ca_set_pmt.. failed !\n", __FUNCTION__);
			return -1;
		}

	/*	Process CA PMT Reply		*/
	/*	will implement soon		*/
		dprintk("%s: Not there yet\n", __FUNCTION__);
	}
	/*	CA PMT Reply not capable	*/
	if (!ca_pmt_reply_test) {
		if ((ca_set_pmt(state, p_ca_message, hw_buffer, 0, NO_REPLY)) < 0) {
			dprintk("%s: ca_set_pmt.. failed !\n", __FUNCTION__);
			return -1;
		}
		if (verbose > 3)
			dprintk("%s: ca_set_pmt.. success !\n", __FUNCTION__);
	/*	put a dummy message		*/

	}
	return 0;
}

static int ca_send_message(struct dst_state *state, struct ca_msg *p_ca_message, void *arg)
{
	int i = 0;
	unsigned int ca_message_header_len;

	u32 command = 0;
	struct ca_msg *hw_buffer;

	if ((hw_buffer = (struct ca_msg *) kmalloc(sizeof (struct ca_msg), GFP_KERNEL)) == NULL) {
		dprintk("%s: Memory allocation failure\n", __FUNCTION__);
		return -ENOMEM;
	}
	if (verbose > 3)
		dprintk("%s\n", __FUNCTION__);

	if (copy_from_user(p_ca_message, (void *)arg, sizeof (struct ca_msg)))
		return -EFAULT;

	if (p_ca_message->msg) {
		ca_message_header_len = p_ca_message->length;	/*	Restore it back when you are done	*/
		/*	EN50221 tag	*/
		command = 0;

		for (i = 0; i < 3; i++) {
			command = command | p_ca_message->msg[i];
			if (i < 2)
				command = command << 8;
		}
		if (verbose > 3)
			dprintk("%s:Command=[0x%x]\n", __FUNCTION__, command);

		switch (command) {
			case CA_PMT:
				if (verbose > 3)
//					dprintk("Command = SEND_CA_PMT\n");
					dprintk("Command = SEND_CA_PMT\n");
//				if ((ca_set_pmt(state, p_ca_message, hw_buffer, 0, 0)) < 0) {
				if ((ca_set_pmt(state, p_ca_message, hw_buffer, 0, 0)) < 0) {	// code simplification started
					dprintk("%s: -->CA_PMT Failed !\n", __FUNCTION__);
					return -1;
				}
				if (verbose > 3)
					dprintk("%s: -->CA_PMT Success !\n", __FUNCTION__);
//				retval = dummy_set_pmt(state, p_ca_message, hw_buffer, 0, 0);

				break;

			case CA_PMT_REPLY:
				if (verbose > 3)
					dprintk("Command = CA_PMT_REPLY\n");
				/*      Have to handle the 2 basic types of cards here  */
				if ((dst_check_ca_pmt(state, p_ca_message, hw_buffer)) < 0) {
					dprintk("%s: -->CA_PMT_REPLY Failed !\n", __FUNCTION__);
					return -1;
				}
				if (verbose > 3)
					dprintk("%s: -->CA_PMT_REPLY Success !\n", __FUNCTION__);

				/*      Certain boards do behave different ?            */
//				retval = ca_set_pmt(state, p_ca_message, hw_buffer, 1, 1);

			case CA_APP_INFO_ENQUIRY:		// only for debugging
				if (verbose > 3)
					dprintk("%s: Getting Cam Application information\n", __FUNCTION__);

				if ((ca_get_app_info(state)) < 0) {
					dprintk("%s: -->CA_APP_INFO_ENQUIRY Failed !\n", __FUNCTION__);
					return -1;
				}
				if (verbose > 3)
					dprintk("%s: -->CA_APP_INFO_ENQUIRY Success !\n", __FUNCTION__);

				break;
		}
	}
	return 0;
}

static int dst_ca_ioctl(struct inode *inode, struct file *file, unsigned int cmd, void *arg)
{
	struct dvb_device* dvbdev = (struct dvb_device*) file->private_data;
	struct dst_state* state = (struct dst_state*) dvbdev->priv;
	struct ca_slot_info *p_ca_slot_info;
	struct ca_caps *p_ca_caps;
	struct ca_msg *p_ca_message;

	if ((p_ca_message = (struct ca_msg *) kmalloc(sizeof (struct ca_msg), GFP_KERNEL)) == NULL) {
		dprintk("%s: Memory allocation failure\n", __FUNCTION__);
		return -ENOMEM;
	}

	if ((p_ca_slot_info = (struct ca_slot_info *) kmalloc(sizeof (struct ca_slot_info), GFP_KERNEL)) == NULL) {
		dprintk("%s: Memory allocation failure\n", __FUNCTION__);
		return -ENOMEM;
	}

	if ((p_ca_caps = (struct ca_caps *) kmalloc(sizeof (struct ca_caps), GFP_KERNEL)) == NULL) {
		dprintk("%s: Memory allocation failure\n", __FUNCTION__);
		return -ENOMEM;
	}

	/*	We have now only the standard ioctl's, the driver is upposed to handle internals.	*/
	switch (cmd) {
		case CA_SEND_MSG:
			if (verbose > 1)
				dprintk("%s: Sending message\n", __FUNCTION__);
			if ((ca_send_message(state, p_ca_message, arg)) < 0) {
				dprintk("%s: -->CA_SEND_MSG Failed !\n", __FUNCTION__);
				return -1;
			}

			break;

		case CA_GET_MSG:
			if (verbose > 1)
				dprintk("%s: Getting message\n", __FUNCTION__);
			if ((ca_get_message(state, p_ca_message, arg)) < 0) {
				dprintk("%s: -->CA_GET_MSG Failed !\n", __FUNCTION__);
				return -1;
			}
			if (verbose > 1)
				dprintk("%s: -->CA_GET_MSG Success !\n", __FUNCTION__);

			break;

		case CA_RESET:
			if (verbose > 1)
				dprintk("%s: Resetting DST\n", __FUNCTION__);
			dst_error_bailout(state);
			msleep(4000);

			break;

		case CA_GET_SLOT_INFO:
			if (verbose > 1)
				dprintk("%s: Getting Slot info\n", __FUNCTION__);
			if ((ca_get_slot_info(state, p_ca_slot_info, arg)) < 0) {
				dprintk("%s: -->CA_GET_SLOT_INFO Failed !\n", __FUNCTION__);
				return -1;
			}
			if (verbose > 1)
				dprintk("%s: -->CA_GET_SLOT_INFO Success !\n", __FUNCTION__);

			break;

		case CA_GET_CAP:
			if (verbose > 1)
				dprintk("%s: Getting Slot capabilities\n", __FUNCTION__);
			if ((ca_get_slot_caps(state, p_ca_caps, arg)) < 0) {
				dprintk("%s: -->CA_GET_CAP Failed !\n", __FUNCTION__);
				return -1;
			}
			if (verbose > 1)
				dprintk("%s: -->CA_GET_CAP Success !\n", __FUNCTION__);

			break;

		case CA_GET_DESCR_INFO:
			if (verbose > 1)
				dprintk("%s: Getting descrambler description\n", __FUNCTION__);
			if ((ca_get_slot_descr(state, p_ca_message, arg)) < 0) {
				dprintk("%s: -->CA_GET_DESCR_INFO Failed !\n", __FUNCTION__);
				return -1;
			}
			if (verbose > 1)
				dprintk("%s: -->CA_GET_DESCR_INFO Success !\n", __FUNCTION__);

			break;

		case CA_SET_DESCR:
			if (verbose > 1)
				dprintk("%s: Setting descrambler\n", __FUNCTION__);
			if ((ca_set_slot_descr()) < 0) {
				dprintk("%s: -->CA_SET_DESCR Failed !\n", __FUNCTION__);
				return -1;
			}
			if (verbose > 1)
				dprintk("%s: -->CA_SET_DESCR Success !\n", __FUNCTION__);

			break;

		case CA_SET_PID:
			if (verbose > 1)
				dprintk("%s: Setting PID\n", __FUNCTION__);
			if ((ca_set_pid()) < 0) {
				dprintk("%s: -->CA_SET_PID Failed !\n", __FUNCTION__);
				return -1;
			}
			if (verbose > 1)
				dprintk("%s: -->CA_SET_PID Success !\n", __FUNCTION__);

		default:
			return -EOPNOTSUPP;
		};

	return 0;
}

static int dst_ca_open(struct inode *inode, struct file *file)
{
	if (verbose > 4)
		dprintk("%s:Device opened [%p]\n", __FUNCTION__, file);
	try_module_get(THIS_MODULE);

	return 0;
}

static int dst_ca_release(struct inode *inode, struct file *file)
{
	if (verbose > 4)
		dprintk("%s:Device closed.\n", __FUNCTION__);
	module_put(THIS_MODULE);

	return 0;
}

static int dst_ca_read(struct file *file, char __user * buffer, size_t length, loff_t * offset)
{
	int bytes_read = 0;

	if (verbose > 4)
		dprintk("%s:Device read.\n", __FUNCTION__);

	return bytes_read;
}

static int dst_ca_write(struct file *file, const char __user * buffer, size_t length, loff_t * offset)
{
	if (verbose > 4)
		dprintk("%s:Device write.\n", __FUNCTION__);

	return 0;
}

static struct file_operations dst_ca_fops = {
	.owner = THIS_MODULE,
	.ioctl = (void *)dst_ca_ioctl,
	.open = dst_ca_open,
	.release = dst_ca_release,
	.read = dst_ca_read,
	.write = dst_ca_write
};

static struct dvb_device dvbdev_ca = {
	.priv = NULL,
	.users = 1,
	.readers = 1,
	.writers = 1,
	.fops = &dst_ca_fops
};

int dst_ca_attach(struct dst_state *dst, struct dvb_adapter *dvb_adapter)
{
	struct dvb_device *dvbdev;
	if (verbose > 4)
		dprintk("%s:registering DST-CA device\n", __FUNCTION__);
	dvb_register_device(dvb_adapter, &dvbdev, &dvbdev_ca, dst, DVB_DEVICE_CA);
	return 0;
}

EXPORT_SYMBOL(dst_ca_attach);

MODULE_DESCRIPTION("DST DVB-S/T/C Combo CA driver");
MODULE_AUTHOR("Manu Abraham");
MODULE_LICENSE("GPL");
