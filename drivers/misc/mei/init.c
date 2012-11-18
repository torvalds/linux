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
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include "mei_dev.h"
#include "hw.h"
#include "interface.h"
#include <linux/mei.h>

const char *mei_dev_state_str(int state)
{
#define MEI_DEV_STATE(state) case MEI_DEV_##state: return #state
	switch (state) {
	MEI_DEV_STATE(INITIALIZING);
	MEI_DEV_STATE(INIT_CLIENTS);
	MEI_DEV_STATE(ENABLED);
	MEI_DEV_STATE(RESETING);
	MEI_DEV_STATE(DISABLED);
	MEI_DEV_STATE(RECOVERING_FROM_RESET);
	MEI_DEV_STATE(POWER_DOWN);
	MEI_DEV_STATE(POWER_UP);
	default:
		return "unkown";
	}
#undef MEI_DEV_STATE
}



/**
 * mei_io_list_flush - removes list entry belonging to cl.
 *
 * @list:  An instance of our list structure
 * @cl: private data of the file object
 */
void mei_io_list_flush(struct mei_cl_cb *list, struct mei_cl *cl)
{
	struct mei_cl_cb *pos;
	struct mei_cl_cb *next;

	list_for_each_entry_safe(pos, next, &list->list, list) {
		if (pos->cl) {
			if (mei_cl_cmp_id(cl, pos->cl))
				list_del(&pos->list);
		}
	}
}
/**
 * mei_cl_flush_queues - flushes queue lists belonging to cl.
 *
 * @dev: the device structure
 * @cl: private data of the file object
 */
int mei_cl_flush_queues(struct mei_cl *cl)
{
	if (!cl || !cl->dev)
		return -EINVAL;

	dev_dbg(&cl->dev->pdev->dev, "remove list entry belonging to cl\n");
	mei_io_list_flush(&cl->dev->read_list, cl);
	mei_io_list_flush(&cl->dev->write_list, cl);
	mei_io_list_flush(&cl->dev->write_waiting_list, cl);
	mei_io_list_flush(&cl->dev->ctrl_wr_list, cl);
	mei_io_list_flush(&cl->dev->ctrl_rd_list, cl);
	mei_io_list_flush(&cl->dev->amthif_cmd_list, cl);
	mei_io_list_flush(&cl->dev->amthif_rd_complete_list, cl);
	return 0;
}



/**
 * init_mei_device - allocates and initializes the mei device structure
 *
 * @pdev: The pci device structure
 *
 * returns The mei_device_device pointer on success, NULL on failure.
 */
struct mei_device *mei_device_init(struct pci_dev *pdev)
{
	struct mei_device *dev;

	dev = kzalloc(sizeof(struct mei_device), GFP_KERNEL);
	if (!dev)
		return NULL;

	/* setup our list array */
	INIT_LIST_HEAD(&dev->file_list);
	INIT_LIST_HEAD(&dev->wd_cl.link);
	INIT_LIST_HEAD(&dev->iamthif_cl.link);
	mutex_init(&dev->device_lock);
	init_waitqueue_head(&dev->wait_recvd_msg);
	init_waitqueue_head(&dev->wait_stop_wd);
	dev->dev_state = MEI_DEV_INITIALIZING;
	dev->iamthif_state = MEI_IAMTHIF_IDLE;

	mei_io_list_init(&dev->read_list);
	mei_io_list_init(&dev->write_list);
	mei_io_list_init(&dev->write_waiting_list);
	mei_io_list_init(&dev->ctrl_wr_list);
	mei_io_list_init(&dev->ctrl_rd_list);
	mei_io_list_init(&dev->amthif_cmd_list);
	mei_io_list_init(&dev->amthif_rd_complete_list);
	dev->pdev = pdev;
	return dev;
}

/**
 * mei_hw_init - initializes host and fw to start work.
 *
 * @dev: the device structure
 *
 * returns 0 on success, <0 on failure.
 */
int mei_hw_init(struct mei_device *dev)
{
	int err = 0;
	int ret;

	mutex_lock(&dev->device_lock);

	dev->host_hw_state = mei_hcsr_read(dev);
	dev->me_hw_state = mei_mecsr_read(dev);
	dev_dbg(&dev->pdev->dev, "host_hw_state = 0x%08x, mestate = 0x%08x.\n",
	    dev->host_hw_state, dev->me_hw_state);

	/* acknowledge interrupt and stop interupts */
	if ((dev->host_hw_state & H_IS) == H_IS)
		mei_reg_write(dev, H_CSR, dev->host_hw_state);

	/* Doesn't change in runtime */
	dev->hbuf_depth = (dev->host_hw_state & H_CBD) >> 24;

	dev->recvd_msg = false;
	dev_dbg(&dev->pdev->dev, "reset in start the mei device.\n");

	mei_reset(dev, 1);

	dev_dbg(&dev->pdev->dev, "host_hw_state = 0x%08x, me_hw_state = 0x%08x.\n",
	    dev->host_hw_state, dev->me_hw_state);

	/* wait for ME to turn on ME_RDY */
	if (!dev->recvd_msg) {
		mutex_unlock(&dev->device_lock);
		err = wait_event_interruptible_timeout(dev->wait_recvd_msg,
			dev->recvd_msg,
			mei_secs_to_jiffies(MEI_INTEROP_TIMEOUT));
		mutex_lock(&dev->device_lock);
	}

	if (err <= 0 && !dev->recvd_msg) {
		dev->dev_state = MEI_DEV_DISABLED;
		dev_dbg(&dev->pdev->dev,
			"wait_event_interruptible_timeout failed"
			"on wait for ME to turn on ME_RDY.\n");
		ret = -ENODEV;
		goto out;
	}

	if (!(((dev->host_hw_state & H_RDY) == H_RDY) &&
	      ((dev->me_hw_state & ME_RDY_HRA) == ME_RDY_HRA))) {
		dev->dev_state = MEI_DEV_DISABLED;
		dev_dbg(&dev->pdev->dev,
			"host_hw_state = 0x%08x, me_hw_state = 0x%08x.\n",
			dev->host_hw_state, dev->me_hw_state);

		if (!(dev->host_hw_state & H_RDY))
			dev_dbg(&dev->pdev->dev, "host turn off H_RDY.\n");

		if (!(dev->me_hw_state & ME_RDY_HRA))
			dev_dbg(&dev->pdev->dev, "ME turn off ME_RDY.\n");

		dev_err(&dev->pdev->dev, "link layer initialization failed.\n");
		ret = -ENODEV;
		goto out;
	}

	if (dev->version.major_version != HBM_MAJOR_VERSION ||
	    dev->version.minor_version != HBM_MINOR_VERSION) {
		dev_dbg(&dev->pdev->dev, "MEI start failed.\n");
		ret = -ENODEV;
		goto out;
	}

	dev->recvd_msg = false;
	dev_dbg(&dev->pdev->dev, "host_hw_state = 0x%08x, me_hw_state = 0x%08x.\n",
	    dev->host_hw_state, dev->me_hw_state);
	dev_dbg(&dev->pdev->dev, "ME turn on ME_RDY and host turn on H_RDY.\n");
	dev_dbg(&dev->pdev->dev, "link layer has been established.\n");
	dev_dbg(&dev->pdev->dev, "MEI  start success.\n");
	ret = 0;

out:
	mutex_unlock(&dev->device_lock);
	return ret;
}

/**
 * mei_hw_reset - resets fw via mei csr register.
 *
 * @dev: the device structure
 * @interrupts_enabled: if interrupt should be enabled after reset.
 */
static void mei_hw_reset(struct mei_device *dev, int interrupts_enabled)
{
	dev->host_hw_state |= (H_RST | H_IG);

	if (interrupts_enabled)
		mei_enable_interrupts(dev);
	else
		mei_disable_interrupts(dev);
}

/**
 * mei_reset - resets host and fw.
 *
 * @dev: the device structure
 * @interrupts_enabled: if interrupt should be enabled after reset.
 */
void mei_reset(struct mei_device *dev, int interrupts_enabled)
{
	struct mei_cl *cl_pos = NULL;
	struct mei_cl *cl_next = NULL;
	struct mei_cl_cb *cb_pos = NULL;
	struct mei_cl_cb *cb_next = NULL;
	bool unexpected;

	if (dev->dev_state == MEI_DEV_RECOVERING_FROM_RESET) {
		dev->need_reset = true;
		return;
	}

	unexpected = (dev->dev_state != MEI_DEV_INITIALIZING &&
			dev->dev_state != MEI_DEV_DISABLED &&
			dev->dev_state != MEI_DEV_POWER_DOWN &&
			dev->dev_state != MEI_DEV_POWER_UP);

	dev->host_hw_state = mei_hcsr_read(dev);

	dev_dbg(&dev->pdev->dev, "before reset host_hw_state = 0x%08x.\n",
	    dev->host_hw_state);

	mei_hw_reset(dev, interrupts_enabled);

	dev->host_hw_state &= ~H_RST;
	dev->host_hw_state |= H_IG;

	mei_hcsr_set(dev);

	dev_dbg(&dev->pdev->dev, "currently saved host_hw_state = 0x%08x.\n",
	    dev->host_hw_state);

	dev->need_reset = false;

	if (dev->dev_state != MEI_DEV_INITIALIZING) {
		if (dev->dev_state != MEI_DEV_DISABLED &&
		    dev->dev_state != MEI_DEV_POWER_DOWN)
			dev->dev_state = MEI_DEV_RESETING;

		list_for_each_entry_safe(cl_pos,
				cl_next, &dev->file_list, link) {
			cl_pos->state = MEI_FILE_DISCONNECTED;
			cl_pos->mei_flow_ctrl_creds = 0;
			cl_pos->read_cb = NULL;
			cl_pos->timer_count = 0;
		}
		/* remove entry if already in list */
		dev_dbg(&dev->pdev->dev, "remove iamthif and wd from the file list.\n");
		mei_me_cl_unlink(dev, &dev->wd_cl);

		mei_me_cl_unlink(dev, &dev->iamthif_cl);

		mei_amthif_reset_params(dev);
		dev->extra_write_index = 0;
	}

	dev->me_clients_num = 0;
	dev->rd_msg_hdr = 0;
	dev->wd_pending = false;

	/* update the state of the registers after reset */
	dev->host_hw_state = mei_hcsr_read(dev);
	dev->me_hw_state = mei_mecsr_read(dev);

	dev_dbg(&dev->pdev->dev, "after reset host_hw_state = 0x%08x, me_hw_state = 0x%08x.\n",
	    dev->host_hw_state, dev->me_hw_state);

	if (unexpected)
		dev_warn(&dev->pdev->dev, "unexpected reset: dev_state = %s\n",
			 mei_dev_state_str(dev->dev_state));

	/* Wake up all readings so they can be interrupted */
	list_for_each_entry_safe(cl_pos, cl_next, &dev->file_list, link) {
		if (waitqueue_active(&cl_pos->rx_wait)) {
			dev_dbg(&dev->pdev->dev, "Waking up client!\n");
			wake_up_interruptible(&cl_pos->rx_wait);
		}
	}
	/* remove all waiting requests */
	list_for_each_entry_safe(cb_pos, cb_next, &dev->write_list.list, list) {
		list_del(&cb_pos->list);
		mei_io_cb_free(cb_pos);
	}
}



/**
 * host_start_message - mei host sends start message.
 *
 * @dev: the device structure
 *
 * returns none.
 */
void mei_host_start_message(struct mei_device *dev)
{
	struct mei_msg_hdr *mei_hdr;
	struct hbm_host_version_request *start_req;
	const size_t len = sizeof(struct hbm_host_version_request);

	mei_hdr = mei_hbm_hdr(&dev->wr_msg_buf[0], len);

	/* host start message */
	start_req = (struct hbm_host_version_request *)&dev->wr_msg_buf[1];
	memset(start_req, 0, len);
	start_req->hbm_cmd = HOST_START_REQ_CMD;
	start_req->host_version.major_version = HBM_MAJOR_VERSION;
	start_req->host_version.minor_version = HBM_MINOR_VERSION;

	dev->recvd_msg = false;
	if (mei_write_message(dev, mei_hdr, (unsigned char *)start_req, len)) {
		dev_dbg(&dev->pdev->dev, "write send version message to FW fail.\n");
		dev->dev_state = MEI_DEV_RESETING;
		mei_reset(dev, 1);
	}
	dev->init_clients_state = MEI_START_MESSAGE;
	dev->init_clients_timer = MEI_CLIENTS_INIT_TIMEOUT;
	return ;
}

/**
 * host_enum_clients_message - host sends enumeration client request message.
 *
 * @dev: the device structure
 *
 * returns none.
 */
void mei_host_enum_clients_message(struct mei_device *dev)
{
	struct mei_msg_hdr *mei_hdr;
	struct hbm_host_enum_request *enum_req;
	const size_t len = sizeof(struct hbm_host_enum_request);
	/* enumerate clients */
	mei_hdr = mei_hbm_hdr(&dev->wr_msg_buf[0], len);

	enum_req = (struct hbm_host_enum_request *) &dev->wr_msg_buf[1];
	memset(enum_req, 0, sizeof(struct hbm_host_enum_request));
	enum_req->hbm_cmd = HOST_ENUM_REQ_CMD;

	if (mei_write_message(dev, mei_hdr, (unsigned char *)enum_req, len)) {
		dev->dev_state = MEI_DEV_RESETING;
		dev_dbg(&dev->pdev->dev, "write send enumeration request message to FW fail.\n");
		mei_reset(dev, 1);
	}
	dev->init_clients_state = MEI_ENUM_CLIENTS_MESSAGE;
	dev->init_clients_timer = MEI_CLIENTS_INIT_TIMEOUT;
	return;
}


/**
 * allocate_me_clients_storage - allocates storage for me clients
 *
 * @dev: the device structure
 *
 * returns none.
 */
void mei_allocate_me_clients_storage(struct mei_device *dev)
{
	struct mei_me_client *clients;
	int b;

	/* count how many ME clients we have */
	for_each_set_bit(b, dev->me_clients_map, MEI_CLIENTS_MAX)
		dev->me_clients_num++;

	if (dev->me_clients_num <= 0)
		return ;


	if (dev->me_clients != NULL) {
		kfree(dev->me_clients);
		dev->me_clients = NULL;
	}
	dev_dbg(&dev->pdev->dev, "memory allocation for ME clients size=%zd.\n",
		dev->me_clients_num * sizeof(struct mei_me_client));
	/* allocate storage for ME clients representation */
	clients = kcalloc(dev->me_clients_num,
			sizeof(struct mei_me_client), GFP_KERNEL);
	if (!clients) {
		dev_dbg(&dev->pdev->dev, "memory allocation for ME clients failed.\n");
		dev->dev_state = MEI_DEV_RESETING;
		mei_reset(dev, 1);
		return ;
	}
	dev->me_clients = clients;
	return ;
}
/**
 * host_client_properties - reads properties for client
 *
 * @dev: the device structure
 *
 * returns:
 * 	< 0 - Error.
 *  = 0 - no more clients.
 *  = 1 - still have clients to send properties request.
 */
int mei_host_client_properties(struct mei_device *dev)
{

	struct mei_msg_hdr *mei_hdr;
	struct hbm_props_request *prop_req;
	const size_t len = sizeof(struct hbm_props_request);

	int b;
	u8 client_num = dev->me_client_presentation_num;

	prop_req = (struct hbm_props_request *)&dev->wr_msg_buf[1];

	b = dev->me_client_index;
	b = find_next_bit(dev->me_clients_map, MEI_CLIENTS_MAX, b);
	if (b < MEI_CLIENTS_MAX) {
		dev->me_clients[client_num].client_id = b;
		dev->me_clients[client_num].mei_flow_ctrl_creds = 0;
		mei_hdr = mei_hbm_hdr(&dev->wr_msg_buf[0], len);


		memset(prop_req, 0, sizeof(struct hbm_props_request));

		prop_req->hbm_cmd = HOST_CLIENT_PROPERTIES_REQ_CMD;
		prop_req->address = b;

		if (mei_write_message(dev, mei_hdr,
				(unsigned char *)prop_req, len)) {
			dev->dev_state = MEI_DEV_RESETING;
			dev_dbg(&dev->pdev->dev, "write send enumeration request message to FW fail.\n");
			mei_reset(dev, 1);
			return -EIO;
		}

		dev->init_clients_timer = MEI_CLIENTS_INIT_TIMEOUT;
		dev->me_client_index = b;
		return 1;
	}

	return 0;
}

/**
 * mei_init_file_private - initializes private file structure.
 *
 * @priv: private file structure to be initialized
 * @file: the file structure
 */
void mei_cl_init(struct mei_cl *priv, struct mei_device *dev)
{
	memset(priv, 0, sizeof(struct mei_cl));
	init_waitqueue_head(&priv->wait);
	init_waitqueue_head(&priv->rx_wait);
	init_waitqueue_head(&priv->tx_wait);
	INIT_LIST_HEAD(&priv->link);
	priv->reading_state = MEI_IDLE;
	priv->writing_state = MEI_IDLE;
	priv->dev = dev;
}

int mei_me_cl_by_uuid(const struct mei_device *dev, const uuid_le *cuuid)
{
	int i, res = -ENOENT;

	for (i = 0; i < dev->me_clients_num; ++i)
		if (uuid_le_cmp(*cuuid,
				dev->me_clients[i].props.protocol_name) == 0) {
			res = i;
			break;
		}

	return res;
}


/**
 * mei_me_cl_link - create link between host and me clinet and add
 *   me_cl to the list
 *
 * @dev: the device structure
 * @cl: link between me and host client assocated with opened file descriptor
 * @cuuid: uuid of ME client
 * @client_id: id of the host client
 *
 * returns ME client index if ME client
 *	-EINVAL on incorrect values
 *	-ENONET if client not found
 */
int mei_me_cl_link(struct mei_device *dev, struct mei_cl *cl,
			const uuid_le *cuuid, u8 host_cl_id)
{
	int i;

	if (!dev || !cl || !cuuid)
		return -EINVAL;

	/* check for valid client id */
	i = mei_me_cl_by_uuid(dev, cuuid);
	if (i >= 0) {
		cl->me_client_id = dev->me_clients[i].client_id;
		cl->state = MEI_FILE_CONNECTING;
		cl->host_client_id = host_cl_id;

		list_add_tail(&cl->link, &dev->file_list);
		return (u8)i;
	}

	return -ENOENT;
}
/**
 * mei_me_cl_unlink - remove me_cl from the list
 *
 * @dev: the device structure
 * @host_client_id: host client id to be removed
 */
void mei_me_cl_unlink(struct mei_device *dev, struct mei_cl *cl)
{
	struct mei_cl *pos, *next;
	list_for_each_entry_safe(pos, next, &dev->file_list, link) {
		if (cl->host_client_id == pos->host_client_id) {
			dev_dbg(&dev->pdev->dev, "remove host client = %d, ME client = %d\n",
					pos->host_client_id, pos->me_client_id);
			list_del_init(&pos->link);
			break;
		}
	}
}

/**
 * mei_alloc_file_private - allocates a private file structure and sets it up.
 * @file: the file structure
 *
 * returns  The allocated file or NULL on failure
 */
struct mei_cl *mei_cl_allocate(struct mei_device *dev)
{
	struct mei_cl *cl;

	cl = kmalloc(sizeof(struct mei_cl), GFP_KERNEL);
	if (!cl)
		return NULL;

	mei_cl_init(cl, dev);

	return cl;
}



/**
 * mei_disconnect_host_client - sends disconnect message to fw from host client.
 *
 * @dev: the device structure
 * @cl: private data of the file object
 *
 * Locking: called under "dev->device_lock" lock
 *
 * returns 0 on success, <0 on failure.
 */
int mei_disconnect_host_client(struct mei_device *dev, struct mei_cl *cl)
{
	struct mei_cl_cb *cb;
	int rets, err;

	if (!dev || !cl)
		return -ENODEV;

	if (cl->state != MEI_FILE_DISCONNECTING)
		return 0;

	cb = mei_io_cb_init(cl, NULL);
	if (!cb)
		return -ENOMEM;

	cb->fop_type = MEI_FOP_CLOSE;
	if (dev->mei_host_buffer_is_empty) {
		dev->mei_host_buffer_is_empty = false;
		if (mei_disconnect(dev, cl)) {
			rets = -ENODEV;
			dev_dbg(&dev->pdev->dev, "failed to call mei_disconnect.\n");
			goto free;
		}
		mdelay(10); /* Wait for hardware disconnection ready */
		list_add_tail(&cb->list, &dev->ctrl_rd_list.list);
	} else {
		dev_dbg(&dev->pdev->dev, "add disconnect cb to control write list\n");
		list_add_tail(&cb->list, &dev->ctrl_wr_list.list);

	}
	mutex_unlock(&dev->device_lock);

	err = wait_event_timeout(dev->wait_recvd_msg,
			MEI_FILE_DISCONNECTED == cl->state,
			mei_secs_to_jiffies(MEI_CL_CONNECT_TIMEOUT));

	mutex_lock(&dev->device_lock);
	if (MEI_FILE_DISCONNECTED == cl->state) {
		rets = 0;
		dev_dbg(&dev->pdev->dev, "successfully disconnected from FW client.\n");
	} else {
		rets = -ENODEV;
		if (MEI_FILE_DISCONNECTED != cl->state)
			dev_dbg(&dev->pdev->dev, "wrong status client disconnect.\n");

		if (err)
			dev_dbg(&dev->pdev->dev,
					"wait failed disconnect err=%08x\n",
					err);

		dev_dbg(&dev->pdev->dev, "failed to disconnect from FW client.\n");
	}

	mei_io_list_flush(&dev->ctrl_rd_list, cl);
	mei_io_list_flush(&dev->ctrl_wr_list, cl);
free:
	mei_io_cb_free(cb);
	return rets;
}

