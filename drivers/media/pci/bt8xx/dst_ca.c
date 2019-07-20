// SPDX-License-Identifier: GPL-2.0-or-later
/*
	CA-driver for TwinHan DST Frontend/Card

	Copyright (C) 2004, 2005 Manu Abraham (manu@kromtek.com)

*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/dvb/ca.h>
#include <media/dvbdev.h>
#include <media/dvb_frontend.h>
#include "dst_ca.h"
#include "dst_common.h"

#define DST_CA_ERROR		0
#define DST_CA_NOTICE		1
#define DST_CA_INFO		2
#define DST_CA_DEBUG		3

#define dprintk(x, y, z, format, arg...) do {						\
	if (z) {									\
		if	((x > DST_CA_ERROR) && (x > y))					\
			printk(KERN_ERR "%s: " format "\n", __func__ , ##arg);	\
		else if	((x > DST_CA_NOTICE) && (x > y))				\
			printk(KERN_NOTICE "%s: " format "\n", __func__ , ##arg);	\
		else if ((x > DST_CA_INFO) && (x > y))					\
			printk(KERN_INFO "%s: " format "\n", __func__ , ##arg);	\
		else if ((x > DST_CA_DEBUG) && (x > y))					\
			printk(KERN_DEBUG "%s: " format "\n", __func__ , ##arg);	\
	} else {									\
		if (x > y)								\
			printk(format, ## arg);						\
	}										\
} while(0)


static DEFINE_MUTEX(dst_ca_mutex);
static unsigned int verbose = 5;
module_param(verbose, int, 0644);
MODULE_PARM_DESC(verbose, "verbose startup messages, default is 1 (yes)");

static void put_command_and_length(u8 *data, int command, int length)
{
	data[0] = (command >> 16) & 0xff;
	data[1] = (command >> 8) & 0xff;
	data[2] = command & 0xff;
	data[3] = length;
}

static void put_checksum(u8 *check_string, int length)
{
	dprintk(verbose, DST_CA_DEBUG, 1, " Computing string checksum.");
	dprintk(verbose, DST_CA_DEBUG, 1, "  -> string length : 0x%02x", length);
	check_string[length] = dst_check_sum (check_string, length);
	dprintk(verbose, DST_CA_DEBUG, 1, "  -> checksum      : 0x%02x", check_string[length]);
}

static int dst_ci_command(struct dst_state* state, u8 * data, u8 *ca_string, u8 len, int read)
{
	u8 reply;

	mutex_lock(&state->dst_mutex);
	dst_comm_init(state);
	msleep(65);

	if (write_dst(state, data, len)) {
		dprintk(verbose, DST_CA_INFO, 1, " Write not successful, trying to recover");
		dst_error_recovery(state);
		goto error;
	}
	if ((dst_pio_disable(state)) < 0) {
		dprintk(verbose, DST_CA_ERROR, 1, " DST PIO disable failed.");
		goto error;
	}
	if (read_dst(state, &reply, GET_ACK) < 0) {
		dprintk(verbose, DST_CA_INFO, 1, " Read not successful, trying to recover");
		dst_error_recovery(state);
		goto error;
	}
	if (read) {
		if (! dst_wait_dst_ready(state, LONG_DELAY)) {
			dprintk(verbose, DST_CA_NOTICE, 1, " 8820 not ready");
			goto error;
		}
		if (read_dst(state, ca_string, 128) < 0) {	/*	Try to make this dynamic	*/
			dprintk(verbose, DST_CA_INFO, 1, " Read not successful, trying to recover");
			dst_error_recovery(state);
			goto error;
		}
	}
	mutex_unlock(&state->dst_mutex);
	return 0;

error:
	mutex_unlock(&state->dst_mutex);
	return -EIO;
}


static int dst_put_ci(struct dst_state *state, u8 *data, int len, u8 *ca_string, int read)
{
	u8 dst_ca_comm_err = 0;

	while (dst_ca_comm_err < RETRIES) {
		dprintk(verbose, DST_CA_NOTICE, 1, " Put Command");
		if (dst_ci_command(state, data, ca_string, len, read)) {	// If error
			dst_error_recovery(state);
			dst_ca_comm_err++; // work required here.
		} else {
			break;
		}
	}

	if(dst_ca_comm_err == RETRIES)
		return -EIO;

	return 0;
}



static int ca_get_app_info(struct dst_state *state)
{
	int length, str_length;
	static u8 command[8] = {0x07, 0x40, 0x01, 0x00, 0x01, 0x00, 0x00, 0xff};

	put_checksum(&command[0], command[0]);
	if ((dst_put_ci(state, command, sizeof(command), state->messages, GET_REPLY)) < 0) {
		dprintk(verbose, DST_CA_ERROR, 1, " -->dst_put_ci FAILED !");
		return -EIO;
	}
	dprintk(verbose, DST_CA_INFO, 1, " -->dst_put_ci SUCCESS !");
	dprintk(verbose, DST_CA_INFO, 1, " ================================ CI Module Application Info ======================================");
	dprintk(verbose, DST_CA_INFO, 1, " Application Type=[%d], Application Vendor=[%d], Vendor Code=[%d]\n%s: Application info=[%s]",
		state->messages[7], (state->messages[8] << 8) | state->messages[9],
		(state->messages[10] << 8) | state->messages[11], __func__, (char *)(&state->messages[12]));
	dprintk(verbose, DST_CA_INFO, 1, " ==================================================================================================");

	// Transform dst message to correct application_info message
	length = state->messages[5];
	str_length = length - 6;
	if (str_length < 0) {
		str_length = 0;
		dprintk(verbose, DST_CA_ERROR, 1, "Invalid string length returned in ca_get_app_info(). Recovering.");
	}

	// First, the command and length fields
	put_command_and_length(&state->messages[0], CA_APP_INFO, length);

	// Copy application_type, application_manufacturer and manufacturer_code
	memmove(&state->messages[4], &state->messages[7], 5);

	// Set string length and copy string
	state->messages[9] = str_length;
	memmove(&state->messages[10], &state->messages[12], str_length);

	return 0;
}

static int ca_get_ca_info(struct dst_state *state)
{
	int srcPtr, dstPtr, i, num_ids;
	static u8 slot_command[8] = {0x07, 0x40, 0x00, 0x00, 0x02, 0x00, 0x00, 0xff};
	const int in_system_id_pos = 8, out_system_id_pos = 4, in_num_ids_pos = 7;

	put_checksum(&slot_command[0], slot_command[0]);
	if ((dst_put_ci(state, slot_command, sizeof (slot_command), state->messages, GET_REPLY)) < 0) {
		dprintk(verbose, DST_CA_ERROR, 1, " -->dst_put_ci FAILED !");
		return -EIO;
	}
	dprintk(verbose, DST_CA_INFO, 1, " -->dst_put_ci SUCCESS !");

	// Print raw data
	dprintk(verbose, DST_CA_INFO, 0, " DST data = [");
	for (i = 0; i < state->messages[0] + 1; i++) {
		dprintk(verbose, DST_CA_INFO, 0, " 0x%02x", state->messages[i]);
	}
	dprintk(verbose, DST_CA_INFO, 0, "]\n");

	// Set the command and length of the output
	num_ids = state->messages[in_num_ids_pos];
	if (num_ids >= 100) {
		num_ids = 100;
		dprintk(verbose, DST_CA_ERROR, 1, "Invalid number of ids (>100). Recovering.");
	}
	put_command_and_length(&state->messages[0], CA_INFO, num_ids * 2);

	dprintk(verbose, DST_CA_INFO, 0, " CA_INFO = [");
	srcPtr = in_system_id_pos;
	dstPtr = out_system_id_pos;
	for(i = 0; i < num_ids; i++) {
		dprintk(verbose, DST_CA_INFO, 0, " 0x%02x%02x", state->messages[srcPtr + 0], state->messages[srcPtr + 1]);
		// Append to output
		state->messages[dstPtr + 0] = state->messages[srcPtr + 0];
		state->messages[dstPtr + 1] = state->messages[srcPtr + 1];
		srcPtr += 2;
		dstPtr += 2;
	}
	dprintk(verbose, DST_CA_INFO, 0, "]\n");

	return 0;
}

static int ca_get_slot_caps(struct dst_state *state, struct ca_caps *p_ca_caps, void __user *arg)
{
	int i;
	u8 slot_cap[256];
	static u8 slot_command[8] = {0x07, 0x40, 0x02, 0x00, 0x02, 0x00, 0x00, 0xff};

	put_checksum(&slot_command[0], slot_command[0]);
	if ((dst_put_ci(state, slot_command, sizeof (slot_command), slot_cap, GET_REPLY)) < 0) {
		dprintk(verbose, DST_CA_ERROR, 1, " -->dst_put_ci FAILED !");
		return -EIO;
	}
	dprintk(verbose, DST_CA_NOTICE, 1, " -->dst_put_ci SUCCESS !");

	/*	Will implement the rest soon		*/

	dprintk(verbose, DST_CA_INFO, 1, " Slot cap = [%d]", slot_cap[7]);
	dprintk(verbose, DST_CA_INFO, 0, "===================================\n");
	for (i = 0; i < slot_cap[0] + 1; i++)
		dprintk(verbose, DST_CA_INFO, 0, " %d", slot_cap[i]);
	dprintk(verbose, DST_CA_INFO, 0, "\n");

	p_ca_caps->slot_num = 1;
	p_ca_caps->slot_type = 1;
	p_ca_caps->descr_num = slot_cap[7];
	p_ca_caps->descr_type = 1;

	if (copy_to_user(arg, p_ca_caps, sizeof (struct ca_caps)))
		return -EFAULT;

	return 0;
}

/*	Need some more work	*/
static int ca_get_slot_descr(struct dst_state *state, struct ca_msg *p_ca_message, void __user *arg)
{
	return -EOPNOTSUPP;
}


static int ca_get_slot_info(struct dst_state *state, struct ca_slot_info *p_ca_slot_info, void __user *arg)
{
	int i;
	static u8 slot_command[8] = {0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff};

	u8 *slot_info = state->messages;

	put_checksum(&slot_command[0], 7);
	if ((dst_put_ci(state, slot_command, sizeof (slot_command), slot_info, GET_REPLY)) < 0) {
		dprintk(verbose, DST_CA_ERROR, 1, " -->dst_put_ci FAILED !");
		return -EIO;
	}
	dprintk(verbose, DST_CA_INFO, 1, " -->dst_put_ci SUCCESS !");

	/*	Will implement the rest soon		*/

	dprintk(verbose, DST_CA_INFO, 1, " Slot info = [%d]", slot_info[3]);
	dprintk(verbose, DST_CA_INFO, 0, "===================================\n");
	for (i = 0; i < 8; i++)
		dprintk(verbose, DST_CA_INFO, 0, " %d", slot_info[i]);
	dprintk(verbose, DST_CA_INFO, 0, "\n");

	if (slot_info[4] & 0x80) {
		p_ca_slot_info->flags = CA_CI_MODULE_PRESENT;
		p_ca_slot_info->num = 1;
		p_ca_slot_info->type = CA_CI;
	} else if (slot_info[4] & 0x40) {
		p_ca_slot_info->flags = CA_CI_MODULE_READY;
		p_ca_slot_info->num = 1;
		p_ca_slot_info->type = CA_CI;
	} else
		p_ca_slot_info->flags = 0;

	if (copy_to_user(arg, p_ca_slot_info, sizeof (struct ca_slot_info)))
		return -EFAULT;

	return 0;
}


static int ca_get_message(struct dst_state *state, struct ca_msg *p_ca_message, void __user *arg)
{
	u8 i = 0;
	u32 command = 0;

	if (copy_from_user(p_ca_message, arg, sizeof (struct ca_msg)))
		return -EFAULT;

	dprintk(verbose, DST_CA_NOTICE, 1, " Message = [%*ph]",
		3, p_ca_message->msg);

	for (i = 0; i < 3; i++) {
		command = command | p_ca_message->msg[i];
		if (i < 2)
			command = command << 8;
	}
	dprintk(verbose, DST_CA_NOTICE, 1, " Command=[0x%x]", command);

	switch (command) {
	case CA_APP_INFO:
		memcpy(p_ca_message->msg, state->messages, 128);
		if (copy_to_user(arg, p_ca_message, sizeof (struct ca_msg)) )
			return -EFAULT;
		break;
	case CA_INFO:
		memcpy(p_ca_message->msg, state->messages, 128);
		if (copy_to_user(arg, p_ca_message, sizeof (struct ca_msg)) )
			return -EFAULT;
		break;
	}

	return 0;
}

static int handle_dst_tag(struct dst_state *state, struct ca_msg *p_ca_message, struct ca_msg *hw_buffer, u32 length)
{
	if (state->dst_hw_cap & DST_TYPE_HAS_SESSION) {
		hw_buffer->msg[2] = p_ca_message->msg[1];	/*	MSB	*/
		hw_buffer->msg[3] = p_ca_message->msg[2];	/*	LSB	*/
	} else {
		if (length > 247) {
			dprintk(verbose, DST_CA_ERROR, 1, " Message too long ! *** Bailing Out *** !");
			return -EIO;
		}
		hw_buffer->msg[0] = (length & 0xff) + 7;
		hw_buffer->msg[1] = 0x40;
		hw_buffer->msg[2] = 0x03;
		hw_buffer->msg[3] = 0x00;
		hw_buffer->msg[4] = 0x03;
		hw_buffer->msg[5] = length & 0xff;
		hw_buffer->msg[6] = 0x00;

		/*
		 *	Need to compute length for EN50221 section 8.3.2, for the time being
		 *	assuming 8.3.2 is not applicable
		 */
		memcpy(&hw_buffer->msg[7], &p_ca_message->msg[4], length);
	}

	return 0;
}

static int write_to_8820(struct dst_state *state, struct ca_msg *hw_buffer, u8 length, u8 reply)
{
	if ((dst_put_ci(state, hw_buffer->msg, length, hw_buffer->msg, reply)) < 0) {
		dprintk(verbose, DST_CA_ERROR, 1, " DST-CI Command failed.");
		dprintk(verbose, DST_CA_NOTICE, 1, " Resetting DST.");
		rdc_reset_state(state);
		return -EIO;
	}
	dprintk(verbose, DST_CA_NOTICE, 1, " DST-CI Command success.");

	return 0;
}

static u32 asn_1_decode(u8 *asn_1_array)
{
	u8 length_field = 0, word_count = 0, count = 0;
	u32 length = 0;

	length_field = asn_1_array[0];
	dprintk(verbose, DST_CA_DEBUG, 1, " Length field=[%02x]", length_field);
	if (length_field < 0x80) {
		length = length_field & 0x7f;
		dprintk(verbose, DST_CA_DEBUG, 1, " Length=[%02x]\n", length);
	} else {
		word_count = length_field & 0x7f;
		for (count = 0; count < word_count; count++) {
			length = length  << 8;
			length += asn_1_array[count + 1];
			dprintk(verbose, DST_CA_DEBUG, 1, " Length=[%04x]", length);
		}
	}
	return length;
}

static int debug_string(u8 *msg, u32 length, u32 offset)
{
	u32 i;

	dprintk(verbose, DST_CA_DEBUG, 0, " String=[ ");
	for (i = offset; i < length; i++)
		dprintk(verbose, DST_CA_DEBUG, 0, "%02x ", msg[i]);
	dprintk(verbose, DST_CA_DEBUG, 0, "]\n");

	return 0;
}


static int ca_set_pmt(struct dst_state *state, struct ca_msg *p_ca_message, struct ca_msg *hw_buffer, u8 reply, u8 query)
{
	u32 length = 0;
	u8 tag_length = 8;

	length = asn_1_decode(&p_ca_message->msg[3]);
	dprintk(verbose, DST_CA_DEBUG, 1, " CA Message length=[%d]", length);
	debug_string(&p_ca_message->msg[4], length, 0); /*	length is excluding tag & length	*/

	memset(hw_buffer->msg, '\0', length);
	handle_dst_tag(state, p_ca_message, hw_buffer, length);
	put_checksum(hw_buffer->msg, hw_buffer->msg[0]);

	debug_string(hw_buffer->msg, (length + tag_length), 0); /*	tags too	*/
	write_to_8820(state, hw_buffer, (length + tag_length), reply);

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
			dprintk(verbose, DST_CA_ERROR, 1, " ca_set_pmt.. failed !");
			return -EIO;
		}

	/*	Process CA PMT Reply		*/
	/*	will implement soon		*/
		dprintk(verbose, DST_CA_ERROR, 1, " Not there yet");
	}
	/*	CA PMT Reply not capable	*/
	if (!ca_pmt_reply_test) {
		if ((ca_set_pmt(state, p_ca_message, hw_buffer, 0, NO_REPLY)) < 0) {
			dprintk(verbose, DST_CA_ERROR, 1, " ca_set_pmt.. failed !");
			return -EIO;
		}
		dprintk(verbose, DST_CA_NOTICE, 1, " ca_set_pmt.. success !");
	/*	put a dummy message		*/

	}
	return 0;
}

static int ca_send_message(struct dst_state *state, struct ca_msg *p_ca_message, void __user *arg)
{
	int i;
	u32 command;
	struct ca_msg *hw_buffer;
	int result = 0;

	hw_buffer = kmalloc(sizeof(*hw_buffer), GFP_KERNEL);
	if (!hw_buffer)
		return -ENOMEM;
	dprintk(verbose, DST_CA_DEBUG, 1, " ");

	if (copy_from_user(p_ca_message, arg, sizeof (struct ca_msg))) {
		result = -EFAULT;
		goto free_mem_and_exit;
	}

	/*	EN50221 tag	*/
	command = 0;

	for (i = 0; i < 3; i++) {
		command = command | p_ca_message->msg[i];
		if (i < 2)
			command = command << 8;
	}
	dprintk(verbose, DST_CA_DEBUG, 1, " Command=[0x%x]\n", command);

	switch (command) {
	case CA_PMT:
		dprintk(verbose, DST_CA_DEBUG, 1, "Command = SEND_CA_PMT");
		if ((ca_set_pmt(state, p_ca_message, hw_buffer, 0, 0)) < 0) {	// code simplification started
			dprintk(verbose, DST_CA_ERROR, 1, " -->CA_PMT Failed !");
			result = -1;
			goto free_mem_and_exit;
		}
		dprintk(verbose, DST_CA_INFO, 1, " -->CA_PMT Success !");
		break;
	case CA_PMT_REPLY:
		dprintk(verbose, DST_CA_INFO, 1, "Command = CA_PMT_REPLY");
		/*      Have to handle the 2 basic types of cards here  */
		if ((dst_check_ca_pmt(state, p_ca_message, hw_buffer)) < 0) {
			dprintk(verbose, DST_CA_ERROR, 1, " -->CA_PMT_REPLY Failed !");
			result = -1;
			goto free_mem_and_exit;
		}
		dprintk(verbose, DST_CA_INFO, 1, " -->CA_PMT_REPLY Success !");
		break;
	case CA_APP_INFO_ENQUIRY:		// only for debugging
		dprintk(verbose, DST_CA_INFO, 1, " Getting Cam Application information");

		if ((ca_get_app_info(state)) < 0) {
			dprintk(verbose, DST_CA_ERROR, 1, " -->CA_APP_INFO_ENQUIRY Failed !");
			result = -1;
			goto free_mem_and_exit;
		}
		dprintk(verbose, DST_CA_INFO, 1, " -->CA_APP_INFO_ENQUIRY Success !");
		break;
	case CA_INFO_ENQUIRY:
		dprintk(verbose, DST_CA_INFO, 1, " Getting CA Information");

		if ((ca_get_ca_info(state)) < 0) {
			dprintk(verbose, DST_CA_ERROR, 1, " -->CA_INFO_ENQUIRY Failed !");
			result = -1;
			goto free_mem_and_exit;
		}
		dprintk(verbose, DST_CA_INFO, 1, " -->CA_INFO_ENQUIRY Success !");
		break;
	}

free_mem_and_exit:
	kfree (hw_buffer);

	return result;
}

static long dst_ca_ioctl(struct file *file, unsigned int cmd, unsigned long ioctl_arg)
{
	struct dvb_device *dvbdev;
	struct dst_state *state;
	struct ca_slot_info *p_ca_slot_info;
	struct ca_caps *p_ca_caps;
	struct ca_msg *p_ca_message;
	void __user *arg = (void __user *)ioctl_arg;
	int result = 0;

	mutex_lock(&dst_ca_mutex);
	dvbdev = file->private_data;
	state = (struct dst_state *)dvbdev->priv;
	p_ca_message = kmalloc(sizeof (struct ca_msg), GFP_KERNEL);
	p_ca_slot_info = kmalloc(sizeof (struct ca_slot_info), GFP_KERNEL);
	p_ca_caps = kmalloc(sizeof (struct ca_caps), GFP_KERNEL);
	if (!p_ca_message || !p_ca_slot_info || !p_ca_caps) {
		result = -ENOMEM;
		goto free_mem_and_exit;
	}

	/*	We have now only the standard ioctl's, the driver is upposed to handle internals.	*/
	switch (cmd) {
	case CA_SEND_MSG:
		dprintk(verbose, DST_CA_INFO, 1, " Sending message");
		result = ca_send_message(state, p_ca_message, arg);

		if (result < 0) {
			dprintk(verbose, DST_CA_ERROR, 1, " -->CA_SEND_MSG Failed !");
			goto free_mem_and_exit;
		}
		break;
	case CA_GET_MSG:
		dprintk(verbose, DST_CA_INFO, 1, " Getting message");
		result = ca_get_message(state, p_ca_message, arg);
		if (result < 0) {
			dprintk(verbose, DST_CA_ERROR, 1, " -->CA_GET_MSG Failed !");
			goto free_mem_and_exit;
		}
		dprintk(verbose, DST_CA_INFO, 1, " -->CA_GET_MSG Success !");
		break;
	case CA_RESET:
		dprintk(verbose, DST_CA_ERROR, 1, " Resetting DST");
		dst_error_bailout(state);
		msleep(4000);
		break;
	case CA_GET_SLOT_INFO:
		dprintk(verbose, DST_CA_INFO, 1, " Getting Slot info");
		result = ca_get_slot_info(state, p_ca_slot_info, arg);
		if (result < 0) {
			dprintk(verbose, DST_CA_ERROR, 1, " -->CA_GET_SLOT_INFO Failed !");
			result = -1;
			goto free_mem_and_exit;
		}
		dprintk(verbose, DST_CA_INFO, 1, " -->CA_GET_SLOT_INFO Success !");
		break;
	case CA_GET_CAP:
		dprintk(verbose, DST_CA_INFO, 1, " Getting Slot capabilities");
		result = ca_get_slot_caps(state, p_ca_caps, arg);
		if (result < 0) {
			dprintk(verbose, DST_CA_ERROR, 1, " -->CA_GET_CAP Failed !");
			goto free_mem_and_exit;
		}
		dprintk(verbose, DST_CA_INFO, 1, " -->CA_GET_CAP Success !");
		break;
	case CA_GET_DESCR_INFO:
		dprintk(verbose, DST_CA_INFO, 1, " Getting descrambler description");
		result = ca_get_slot_descr(state, p_ca_message, arg);
		if (result < 0) {
			dprintk(verbose, DST_CA_ERROR, 1, " -->CA_GET_DESCR_INFO Failed !");
			goto free_mem_and_exit;
		}
		dprintk(verbose, DST_CA_INFO, 1, " -->CA_GET_DESCR_INFO Success !");
		break;
	default:
		result = -EOPNOTSUPP;
	}
 free_mem_and_exit:
	kfree (p_ca_message);
	kfree (p_ca_slot_info);
	kfree (p_ca_caps);

	mutex_unlock(&dst_ca_mutex);
	return result;
}

static int dst_ca_open(struct inode *inode, struct file *file)
{
	dprintk(verbose, DST_CA_DEBUG, 1, " Device opened [%p] ", file);

	return 0;
}

static int dst_ca_release(struct inode *inode, struct file *file)
{
	dprintk(verbose, DST_CA_DEBUG, 1, " Device closed.");

	return 0;
}

static ssize_t dst_ca_read(struct file *file, char __user *buffer, size_t length, loff_t *offset)
{
	dprintk(verbose, DST_CA_DEBUG, 1, " Device read.");

	return 0;
}

static ssize_t dst_ca_write(struct file *file, const char __user *buffer, size_t length, loff_t *offset)
{
	dprintk(verbose, DST_CA_DEBUG, 1, " Device write.");

	return 0;
}

static const struct file_operations dst_ca_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = dst_ca_ioctl,
	.open = dst_ca_open,
	.release = dst_ca_release,
	.read = dst_ca_read,
	.write = dst_ca_write,
	.llseek = noop_llseek,
};

static struct dvb_device dvbdev_ca = {
	.priv = NULL,
	.users = 1,
	.readers = 1,
	.writers = 1,
	.fops = &dst_ca_fops
};

struct dvb_device *dst_ca_attach(struct dst_state *dst, struct dvb_adapter *dvb_adapter)
{
	struct dvb_device *dvbdev;

	dprintk(verbose, DST_CA_ERROR, 1, "registering DST-CA device");
	if (dvb_register_device(dvb_adapter, &dvbdev, &dvbdev_ca, dst,
				DVB_DEVICE_CA, 0) == 0) {
		dst->dst_ca = dvbdev;
		return dst->dst_ca;
	}

	return NULL;
}

EXPORT_SYMBOL(dst_ca_attach);

MODULE_DESCRIPTION("DST DVB-S/T/C Combo CA driver");
MODULE_AUTHOR("Manu Abraham");
MODULE_LICENSE("GPL");
