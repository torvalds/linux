/*
 * Part of Intel(R) Manageability Engine Interface Linux driver
 *
 * Copyright (c) 2003 - 2008 Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */


#include "heci.h"
#include "heci_interface.h"


/**
 * heci_set_csr_register - write H_CSR register to the heci device,
 * and ignore the H_IS bit for it is write-one-to-zero.
 *
 * @dev: device object for our driver
 */
void heci_set_csr_register(struct iamt_heci_device *dev)
{
	if ((dev->host_hw_state & H_IS) == H_IS)
		dev->host_hw_state &= ~H_IS;
	write_heci_register(dev, H_CSR, dev->host_hw_state);
	dev->host_hw_state = read_heci_register(dev, H_CSR);
}

/**
 * heci_csr_enable_interrupts - enable heci device interrupts
 *
 * @dev: device object for our driver
 */
void heci_csr_enable_interrupts(struct iamt_heci_device *dev)
{
	dev->host_hw_state |= H_IE;
	heci_set_csr_register(dev);
}

/**
 * heci_csr_disable_interrupts - disable heci device interrupts
 *
 * @dev: device object for our driver
 */
void heci_csr_disable_interrupts(struct iamt_heci_device *dev)
{
	dev->host_hw_state &= ~H_IE;
	heci_set_csr_register(dev);
}

/**
 * heci_csr_clear_his - clear H_IS bit in H_CSR
 *
 * @dev: device object for our driver
 */
void heci_csr_clear_his(struct iamt_heci_device *dev)
{
	write_heci_register(dev, H_CSR, dev->host_hw_state);
	dev->host_hw_state = read_heci_register(dev, H_CSR);
}

/**
 * _host_get_filled_slots - get number of device filled buffer slots
 *
 * @device: the device structure
 *
 * returns numer of filled slots
 */
static unsigned char _host_get_filled_slots(const struct iamt_heci_device *dev)
{
	char read_ptr, write_ptr;

	read_ptr = (char) ((dev->host_hw_state & H_CBRP) >> 8);
	write_ptr = (char) ((dev->host_hw_state & H_CBWP) >> 16);

	return (unsigned char) (write_ptr - read_ptr);
}

/**
 * host_buffer_is_empty  - check if host buffer is empty.
 *
 * @dev: device object for our driver
 *
 * returns  1 if empty, 0 - otherwise.
 */
int host_buffer_is_empty(struct iamt_heci_device *dev)
{
	unsigned char filled_slots;

	dev->host_hw_state = read_heci_register(dev, H_CSR);
	filled_slots = _host_get_filled_slots(dev);

	if (filled_slots > 0)
		return 0;

	return 1;
}

/**
 * count_empty_write_slots  - count write empty slots.
 *
 * @dev: device object for our driver
 *
 * returns -1(ESLOTS_OVERFLOW) if overflow, otherwise empty slots count
 */
__s32 count_empty_write_slots(const struct iamt_heci_device *dev)
{
	unsigned char buffer_depth, filled_slots, empty_slots;

	buffer_depth = (unsigned char) ((dev->host_hw_state & H_CBD) >> 24);
	filled_slots = _host_get_filled_slots(dev);
	empty_slots = buffer_depth - filled_slots;

	if (filled_slots > buffer_depth) {
		/* overflow */
		return -ESLOTS_OVERFLOW;
	}

	return (__s32) empty_slots;
}

/**
 * heci_write_message  - write a message to heci device.
 *
 * @dev: device object for our driver
 * @heci_hdr: header of  message
 * @write_buffer: message buffer will be write
 * @write_length: message size will be write
 *
 * returns 1 if success, 0 - otherwise.
 */
int heci_write_message(struct iamt_heci_device *dev,
			     struct heci_msg_hdr *header,
			     unsigned char *write_buffer,
			     unsigned long write_length)
{
	__u32 temp_msg = 0;
	unsigned long bytes_written = 0;
	unsigned char buffer_depth, filled_slots, empty_slots;
	unsigned long dw_to_write;

	dev->host_hw_state = read_heci_register(dev, H_CSR);
	DBG("host_hw_state = 0x%08x.\n", dev->host_hw_state);
	DBG("heci_write_message header=%08x.\n", *((__u32 *) header));
	buffer_depth = (unsigned char) ((dev->host_hw_state & H_CBD) >> 24);
	filled_slots = _host_get_filled_slots(dev);
	empty_slots = buffer_depth - filled_slots;
	DBG("filled = %hu, empty = %hu.\n", filled_slots, empty_slots);

	dw_to_write = ((write_length + 3) / 4);

	if (dw_to_write > empty_slots)
		return 0;

	write_heci_register(dev, H_CB_WW, *((__u32 *) header));

	while (write_length >= 4) {
		write_heci_register(dev, H_CB_WW,
				*(__u32 *) (write_buffer + bytes_written));
		bytes_written += 4;
		write_length -= 4;
	}

	if (write_length > 0) {
		memcpy(&temp_msg, &write_buffer[bytes_written], write_length);
		write_heci_register(dev, H_CB_WW, temp_msg);
	}

	dev->host_hw_state |= H_IG;
	heci_set_csr_register(dev);
	dev->me_hw_state = read_heci_register(dev, ME_CSR_HA);
	if ((dev->me_hw_state & ME_RDY_HRA) != ME_RDY_HRA)
		return 0;

	dev->write_hang = 0;
	return 1;
}

/**
 * count_full_read_slots  - count read full slots.
 *
 * @dev: device object for our driver
 *
 * returns -1(ESLOTS_OVERFLOW) if overflow, otherwise filled slots count
 */
__s32 count_full_read_slots(struct iamt_heci_device *dev)
{
	char read_ptr, write_ptr;
	unsigned char buffer_depth, filled_slots;

	dev->me_hw_state = read_heci_register(dev, ME_CSR_HA);
	buffer_depth = (unsigned char)((dev->me_hw_state & ME_CBD_HRA) >> 24);
	read_ptr = (char) ((dev->me_hw_state & ME_CBRP_HRA) >> 8);
	write_ptr = (char) ((dev->me_hw_state & ME_CBWP_HRA) >> 16);
	filled_slots = (unsigned char) (write_ptr - read_ptr);

	if (filled_slots > buffer_depth) {
		/* overflow */
		return -ESLOTS_OVERFLOW;
	}

	DBG("filled_slots =%08x  \n", filled_slots);
	return (__s32) filled_slots;
}

/**
 * heci_read_slots  - read a message from heci device.
 *
 * @dev: device object for our driver
 * @buffer: message buffer will be write
 * @buffer_length: message size will be read
 */
void heci_read_slots(struct iamt_heci_device *dev,
		     unsigned char *buffer, unsigned long buffer_length)
{
	__u32 i = 0;
	unsigned char temp_buf[sizeof(__u32)];

	while (buffer_length >= sizeof(__u32)) {
		((__u32 *) buffer)[i] = read_heci_register(dev, ME_CB_RW);
		DBG("buffer[%d]= %d\n", i, ((__u32 *) buffer)[i]);
		i++;
		buffer_length -= sizeof(__u32);
	}

	if (buffer_length > 0) {
		*((__u32 *) &temp_buf) = read_heci_register(dev, ME_CB_RW);
		memcpy(&buffer[i * 4], temp_buf, buffer_length);
	}

	dev->host_hw_state |= H_IG;
	heci_set_csr_register(dev);
}

/**
 * flow_ctrl_creds  - check flow_control credentials.
 *
 * @dev: device object for our driver
 * @file_ext: private data of the file object
 *
 * returns 1 if flow_ctrl_creds >0, 0 - otherwise.
 */
int flow_ctrl_creds(struct iamt_heci_device *dev,
				   struct heci_file_private *file_ext)
{
	__u8 i;

	if (!dev->num_heci_me_clients)
		return 0;

	if (file_ext == NULL)
		return 0;

	if (file_ext->flow_ctrl_creds > 0)
		return 1;

	for (i = 0; i < dev->num_heci_me_clients; i++) {
		if (dev->me_clients[i].client_id == file_ext->me_client_id) {
			if (dev->me_clients[i].flow_ctrl_creds > 0) {
				BUG_ON(dev->me_clients[i].props.single_recv_buf
					 == 0);
				return 1;
			}
			return 0;
		}
	}
	BUG();
	return 0;
}

/**
 * flow_ctrl_reduce  - reduce flow_control.
 *
 * @dev: device object for our driver
 * @file_ext: private data of the file object
 */
void flow_ctrl_reduce(struct iamt_heci_device *dev,
			 struct heci_file_private *file_ext)
{
	__u8 i;

	if (!dev->num_heci_me_clients)
		return;

	for (i = 0; i < dev->num_heci_me_clients; i++) {
		if (dev->me_clients[i].client_id == file_ext->me_client_id) {
			if (dev->me_clients[i].props.single_recv_buf != 0) {
				BUG_ON(dev->me_clients[i].flow_ctrl_creds <= 0);
				dev->me_clients[i].flow_ctrl_creds--;
			} else {
				BUG_ON(file_ext->flow_ctrl_creds <= 0);
				file_ext->flow_ctrl_creds--;
			}
			return;
		}
	}
	BUG();
}

/**
 * heci_send_flow_control - send flow control to fw.
 *
 * @dev: device object for our driver
 * @file_ext: private data of the file object
 *
 * returns 1 if success, 0 - otherwise.
 */
int heci_send_flow_control(struct iamt_heci_device *dev,
				 struct heci_file_private *file_ext)
{
	struct heci_msg_hdr *heci_hdr;
	struct hbm_flow_control *heci_flow_control;

	heci_hdr = (struct heci_msg_hdr *) &dev->wr_msg_buf[0];
	heci_hdr->host_addr = 0;
	heci_hdr->me_addr = 0;
	heci_hdr->length = sizeof(struct hbm_flow_control);
	heci_hdr->msg_complete = 1;
	heci_hdr->reserved = 0;

	heci_flow_control = (struct hbm_flow_control *) &dev->wr_msg_buf[1];
	memset(heci_flow_control, 0, sizeof(heci_flow_control));
	heci_flow_control->host_addr = file_ext->host_client_id;
	heci_flow_control->me_addr = file_ext->me_client_id;
	heci_flow_control->cmd.cmd = HECI_FLOW_CONTROL_CMD;
	memset(heci_flow_control->reserved, 0,
			sizeof(heci_flow_control->reserved));
	DBG("sending flow control host client = %d, me client = %d\n",
	    file_ext->host_client_id, file_ext->me_client_id);
	if (!heci_write_message(dev, heci_hdr,
				(unsigned char *) heci_flow_control,
				sizeof(struct hbm_flow_control)))
		return 0;

	return 1;

}

/**
 * other_client_is_connecting  - check if other
 *    client with the same client id is connected.
 *
 * @dev: device object for our driver
 * @file_ext: private data of the file object
 *
 * returns 1 if other client is connected, 0 - otherwise.
 */
int other_client_is_connecting(struct iamt_heci_device *dev,
		struct heci_file_private *file_ext)
{
	struct heci_file_private *file_pos = NULL;
	struct heci_file_private *file_next = NULL;

	list_for_each_entry_safe(file_pos, file_next, &dev->file_list, link) {
		if ((file_pos->state == HECI_FILE_CONNECTING)
			&& (file_pos != file_ext)
			&& file_ext->me_client_id == file_pos->me_client_id)
			return 1;

	}
	return 0;
}

/**
 * heci_send_wd  - send watch dog message to fw.
 *
 * @dev: device object for our driver
 *
 * returns 1 if success, 0 - otherwise.
 */
int heci_send_wd(struct iamt_heci_device *dev)
{
	struct heci_msg_hdr *heci_hdr;

	heci_hdr = (struct heci_msg_hdr *) &dev->wr_msg_buf[0];
	heci_hdr->host_addr = dev->wd_file_ext.host_client_id;
	heci_hdr->me_addr = dev->wd_file_ext.me_client_id;
	heci_hdr->msg_complete = 1;
	heci_hdr->reserved = 0;

	if (!memcmp(dev->wd_data, heci_start_wd_params,
			HECI_WD_PARAMS_SIZE)) {
		heci_hdr->length = HECI_START_WD_DATA_SIZE;
	} else {
		BUG_ON(memcmp(dev->wd_data, heci_stop_wd_params,
			HECI_WD_PARAMS_SIZE));
		heci_hdr->length = HECI_WD_PARAMS_SIZE;
	}

	if (!heci_write_message(dev, heci_hdr, dev->wd_data, heci_hdr->length))
		return 0;

	return 1;
}

/**
 * heci_disconnect  - send disconnect message to fw.
 *
 * @dev: device object for our driver
 * @file_ext: private data of the file object
 *
 * returns 1 if success, 0 - otherwise.
 */
int heci_disconnect(struct iamt_heci_device *dev,
			  struct heci_file_private *file_ext)
{
	struct heci_msg_hdr *heci_hdr;
	struct hbm_client_disconnect_request *heci_cli_disconnect;

	heci_hdr = (struct heci_msg_hdr *) &dev->wr_msg_buf[0];
	heci_hdr->host_addr = 0;
	heci_hdr->me_addr = 0;
	heci_hdr->length = sizeof(struct hbm_client_disconnect_request);
	heci_hdr->msg_complete = 1;
	heci_hdr->reserved = 0;

	heci_cli_disconnect =
	    (struct hbm_client_disconnect_request *) &dev->wr_msg_buf[1];
	memset(heci_cli_disconnect, 0, sizeof(heci_cli_disconnect));
	heci_cli_disconnect->host_addr = file_ext->host_client_id;
	heci_cli_disconnect->me_addr = file_ext->me_client_id;
	heci_cli_disconnect->cmd.cmd = CLIENT_DISCONNECT_REQ_CMD;
	heci_cli_disconnect->reserved[0] = 0;

	if (!heci_write_message(dev, heci_hdr,
				(unsigned char *) heci_cli_disconnect,
				sizeof(struct hbm_client_disconnect_request)))
		return 0;

	return 1;
}

/**
 * heci_connect - send connect message to fw.
 *
 * @dev: device object for our driver
 * @file_ext: private data of the file object
 *
 * returns 1 if success, 0 - otherwise.
 */
int heci_connect(struct iamt_heci_device *dev,
		       struct heci_file_private *file_ext)
{
	struct heci_msg_hdr *heci_hdr;
	struct hbm_client_connect_request *heci_cli_connect;

	heci_hdr = (struct heci_msg_hdr *) &dev->wr_msg_buf[0];
	heci_hdr->host_addr = 0;
	heci_hdr->me_addr = 0;
	heci_hdr->length = sizeof(struct hbm_client_connect_request);
	heci_hdr->msg_complete = 1;
	heci_hdr->reserved = 0;

	heci_cli_connect =
	    (struct hbm_client_connect_request *) &dev->wr_msg_buf[1];
	heci_cli_connect->host_addr = file_ext->host_client_id;
	heci_cli_connect->me_addr = file_ext->me_client_id;
	heci_cli_connect->cmd.cmd = CLIENT_CONNECT_REQ_CMD;
	heci_cli_connect->reserved = 0;

	if (!heci_write_message(dev, heci_hdr,
				(unsigned char *) heci_cli_connect,
				sizeof(struct hbm_client_connect_request)))
		return 0;

	return 1;
}
