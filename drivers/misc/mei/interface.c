/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/pci.h>
#include "mei_dev.h"
#include <linux/mei.h>
#include "interface.h"



/**
 * mei_set_csr_register - writes H_CSR register to the mei device,
 * and ignores the H_IS bit for it is write-one-to-zero.
 *
 * @dev: the device structure
 */
void mei_hcsr_set(struct mei_device *dev)
{
	if ((dev->host_hw_state & H_IS) == H_IS)
		dev->host_hw_state &= ~H_IS;
	mei_reg_write(dev, H_CSR, dev->host_hw_state);
	dev->host_hw_state = mei_hcsr_read(dev);
}

/**
 * mei_csr_enable_interrupts - enables mei device interrupts
 *
 * @dev: the device structure
 */
void mei_enable_interrupts(struct mei_device *dev)
{
	dev->host_hw_state |= H_IE;
	mei_hcsr_set(dev);
}

/**
 * mei_csr_disable_interrupts - disables mei device interrupts
 *
 * @dev: the device structure
 */
void mei_disable_interrupts(struct mei_device *dev)
{
	dev->host_hw_state &= ~H_IE;
	mei_hcsr_set(dev);
}

/**
 * mei_hbuf_filled_slots - gets number of device filled buffer slots
 *
 * @device: the device structure
 *
 * returns number of filled slots
 */
static unsigned char mei_hbuf_filled_slots(struct mei_device *dev)
{
	char read_ptr, write_ptr;

	dev->host_hw_state = mei_hcsr_read(dev);

	read_ptr = (char) ((dev->host_hw_state & H_CBRP) >> 8);
	write_ptr = (char) ((dev->host_hw_state & H_CBWP) >> 16);

	return (unsigned char) (write_ptr - read_ptr);
}

/**
 * mei_hbuf_is_empty - checks if host buffer is empty.
 *
 * @dev: the device structure
 *
 * returns true if empty, false - otherwise.
 */
bool mei_hbuf_is_empty(struct mei_device *dev)
{
	return mei_hbuf_filled_slots(dev) == 0;
}

/**
 * mei_hbuf_empty_slots - counts write empty slots.
 *
 * @dev: the device structure
 *
 * returns -1(ESLOTS_OVERFLOW) if overflow, otherwise empty slots count
 */
int mei_hbuf_empty_slots(struct mei_device *dev)
{
	unsigned char filled_slots, empty_slots;

	filled_slots = mei_hbuf_filled_slots(dev);
	empty_slots = dev->hbuf_depth - filled_slots;

	/* check for overflow */
	if (filled_slots > dev->hbuf_depth)
		return -EOVERFLOW;

	return empty_slots;
}

/**
 * mei_write_message - writes a message to mei device.
 *
 * @dev: the device structure
 * @header: header of message
 * @write_buffer: message buffer will be written
 * @write_length: message size will be written
 *
 * This function returns -EIO if write has failed
 */
int mei_write_message(struct mei_device *dev, struct mei_msg_hdr *header,
		      unsigned char *buf, unsigned long length)
{
	unsigned long rem, dw_cnt;
	u32 *reg_buf = (u32 *)buf;
	int i;
	int empty_slots;


	dev_dbg(&dev->pdev->dev,
			"mei_write_message header=%08x.\n",
			*((u32 *) header));

	empty_slots = mei_hbuf_empty_slots(dev);
	dev_dbg(&dev->pdev->dev, "empty slots = %hu.\n", empty_slots);

	dw_cnt = mei_data2slots(length);
	if (empty_slots < 0 || dw_cnt > empty_slots)
		return -EIO;

	mei_reg_write(dev, H_CB_WW, *((u32 *) header));

	for (i = 0; i < length / 4; i++)
		mei_reg_write(dev, H_CB_WW, reg_buf[i]);

	rem = length & 0x3;
	if (rem > 0) {
		u32 reg = 0;
		memcpy(&reg, &buf[length - rem], rem);
		mei_reg_write(dev, H_CB_WW, reg);
	}

	dev->host_hw_state = mei_hcsr_read(dev);
	dev->host_hw_state |= H_IG;
	mei_hcsr_set(dev);
	dev->me_hw_state = mei_mecsr_read(dev);
	if ((dev->me_hw_state & ME_RDY_HRA) != ME_RDY_HRA)
		return -EIO;

	return 0;
}

/**
 * mei_count_full_read_slots - counts read full slots.
 *
 * @dev: the device structure
 *
 * returns -1(ESLOTS_OVERFLOW) if overflow, otherwise filled slots count
 */
int mei_count_full_read_slots(struct mei_device *dev)
{
	char read_ptr, write_ptr;
	unsigned char buffer_depth, filled_slots;

	dev->me_hw_state = mei_mecsr_read(dev);
	buffer_depth = (unsigned char)((dev->me_hw_state & ME_CBD_HRA) >> 24);
	read_ptr = (char) ((dev->me_hw_state & ME_CBRP_HRA) >> 8);
	write_ptr = (char) ((dev->me_hw_state & ME_CBWP_HRA) >> 16);
	filled_slots = (unsigned char) (write_ptr - read_ptr);

	/* check for overflow */
	if (filled_slots > buffer_depth)
		return -EOVERFLOW;

	dev_dbg(&dev->pdev->dev, "filled_slots =%08x\n", filled_slots);
	return (int)filled_slots;
}

/**
 * mei_read_slots - reads a message from mei device.
 *
 * @dev: the device structure
 * @buffer: message buffer will be written
 * @buffer_length: message size will be read
 */
void mei_read_slots(struct mei_device *dev, unsigned char *buffer,
		    unsigned long buffer_length)
{
	u32 *reg_buf = (u32 *)buffer;

	for (; buffer_length >= sizeof(u32); buffer_length -= sizeof(u32))
		*reg_buf++ = mei_mecbrw_read(dev);

	if (buffer_length > 0) {
		u32 reg = mei_mecbrw_read(dev);
		memcpy(reg_buf, &reg, buffer_length);
	}

	dev->host_hw_state |= H_IG;
	mei_hcsr_set(dev);
}

/**
 * mei_flow_ctrl_creds - checks flow_control credentials.
 *
 * @dev: the device structure
 * @cl: private data of the file object
 *
 * returns 1 if mei_flow_ctrl_creds >0, 0 - otherwise.
 *	-ENOENT if mei_cl is not present
 *	-EINVAL if single_recv_buf == 0
 */
int mei_flow_ctrl_creds(struct mei_device *dev, struct mei_cl *cl)
{
	int i;

	if (!dev->me_clients_num)
		return 0;

	if (cl->mei_flow_ctrl_creds > 0)
		return 1;

	for (i = 0; i < dev->me_clients_num; i++) {
		struct mei_me_client  *me_cl = &dev->me_clients[i];
		if (me_cl->client_id == cl->me_client_id) {
			if (me_cl->mei_flow_ctrl_creds) {
				if (WARN_ON(me_cl->props.single_recv_buf == 0))
					return -EINVAL;
				return 1;
			} else {
				return 0;
			}
		}
	}
	return -ENOENT;
}

/**
 * mei_flow_ctrl_reduce - reduces flow_control.
 *
 * @dev: the device structure
 * @cl: private data of the file object
 * @returns
 *	0 on success
 *	-ENOENT when me client is not found
 *	-EINVAL when ctrl credits are <= 0
 */
int mei_flow_ctrl_reduce(struct mei_device *dev, struct mei_cl *cl)
{
	int i;

	if (!dev->me_clients_num)
		return -ENOENT;

	for (i = 0; i < dev->me_clients_num; i++) {
		struct mei_me_client  *me_cl = &dev->me_clients[i];
		if (me_cl->client_id == cl->me_client_id) {
			if (me_cl->props.single_recv_buf != 0) {
				if (WARN_ON(me_cl->mei_flow_ctrl_creds <= 0))
					return -EINVAL;
				dev->me_clients[i].mei_flow_ctrl_creds--;
			} else {
				if (WARN_ON(cl->mei_flow_ctrl_creds <= 0))
					return -EINVAL;
				cl->mei_flow_ctrl_creds--;
			}
			return 0;
		}
	}
	return -ENOENT;
}

/**
 * mei_send_flow_control - sends flow control to fw.
 *
 * @dev: the device structure
 * @cl: private data of the file object
 *
 * This function returns -EIO on write failure
 */
int mei_send_flow_control(struct mei_device *dev, struct mei_cl *cl)
{
	struct mei_msg_hdr *mei_hdr;
	struct hbm_flow_control *mei_flow_control;

	mei_hdr = (struct mei_msg_hdr *) &dev->wr_msg_buf[0];
	mei_hdr->host_addr = 0;
	mei_hdr->me_addr = 0;
	mei_hdr->length = sizeof(struct hbm_flow_control);
	mei_hdr->msg_complete = 1;
	mei_hdr->reserved = 0;

	mei_flow_control = (struct hbm_flow_control *) &dev->wr_msg_buf[1];
	memset(mei_flow_control, 0, sizeof(*mei_flow_control));
	mei_flow_control->host_addr = cl->host_client_id;
	mei_flow_control->me_addr = cl->me_client_id;
	mei_flow_control->hbm_cmd = MEI_FLOW_CONTROL_CMD;
	memset(mei_flow_control->reserved, 0,
			sizeof(mei_flow_control->reserved));
	dev_dbg(&dev->pdev->dev, "sending flow control host client = %d, ME client = %d\n",
		cl->host_client_id, cl->me_client_id);

	return mei_write_message(dev, mei_hdr,
				(unsigned char *) mei_flow_control,
				sizeof(struct hbm_flow_control));
}

/**
 * mei_other_client_is_connecting - checks if other
 *    client with the same client id is connected.
 *
 * @dev: the device structure
 * @cl: private data of the file object
 *
 * returns 1 if other client is connected, 0 - otherwise.
 */
int mei_other_client_is_connecting(struct mei_device *dev,
				struct mei_cl *cl)
{
	struct mei_cl *cl_pos = NULL;
	struct mei_cl *cl_next = NULL;

	list_for_each_entry_safe(cl_pos, cl_next, &dev->file_list, link) {
		if ((cl_pos->state == MEI_FILE_CONNECTING) &&
			(cl_pos != cl) &&
			cl->me_client_id == cl_pos->me_client_id)
			return 1;

	}
	return 0;
}

/**
 * mei_disconnect - sends disconnect message to fw.
 *
 * @dev: the device structure
 * @cl: private data of the file object
 *
 * This function returns -EIO on write failure
 */
int mei_disconnect(struct mei_device *dev, struct mei_cl *cl)
{
	struct mei_msg_hdr *mei_hdr;
	struct hbm_client_connect_request *req;

	mei_hdr = (struct mei_msg_hdr *) &dev->wr_msg_buf[0];
	mei_hdr->host_addr = 0;
	mei_hdr->me_addr = 0;
	mei_hdr->length = sizeof(struct hbm_client_connect_request);
	mei_hdr->msg_complete = 1;
	mei_hdr->reserved = 0;

	req = (struct hbm_client_connect_request *)&dev->wr_msg_buf[1];
	memset(req, 0, sizeof(*req));
	req->host_addr = cl->host_client_id;
	req->me_addr = cl->me_client_id;
	req->hbm_cmd = CLIENT_DISCONNECT_REQ_CMD;
	req->reserved = 0;

	return mei_write_message(dev, mei_hdr, (unsigned char *)req,
				sizeof(struct hbm_client_connect_request));
}

/**
 * mei_connect - sends connect message to fw.
 *
 * @dev: the device structure
 * @cl: private data of the file object
 *
 * This function returns -EIO on write failure
 */
int mei_connect(struct mei_device *dev, struct mei_cl *cl)
{
	struct mei_msg_hdr *mei_hdr;
	struct hbm_client_connect_request *mei_cli_connect;

	mei_hdr = (struct mei_msg_hdr *) &dev->wr_msg_buf[0];
	mei_hdr->host_addr = 0;
	mei_hdr->me_addr = 0;
	mei_hdr->length = sizeof(struct hbm_client_connect_request);
	mei_hdr->msg_complete = 1;
	mei_hdr->reserved = 0;

	mei_cli_connect =
	    (struct hbm_client_connect_request *) &dev->wr_msg_buf[1];
	mei_cli_connect->host_addr = cl->host_client_id;
	mei_cli_connect->me_addr = cl->me_client_id;
	mei_cli_connect->hbm_cmd = CLIENT_CONNECT_REQ_CMD;
	mei_cli_connect->reserved = 0;

	return mei_write_message(dev, mei_hdr,
				(unsigned char *) mei_cli_connect,
				sizeof(struct hbm_client_connect_request));
}
