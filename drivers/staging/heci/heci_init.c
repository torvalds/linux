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

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/reboot.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/moduleparam.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/kthread.h>

#include "heci_data_structures.h"
#include "heci_interface.h"
#include "heci.h"


const __u8 heci_start_wd_params[] = { 0x02, 0x12, 0x13, 0x10 };
const __u8 heci_stop_wd_params[] = { 0x02, 0x02, 0x14, 0x10 };

const __u8 heci_wd_state_independence_msg[3][4] = {
	{0x05, 0x02, 0x51, 0x10},
	{0x05, 0x02, 0x52, 0x10},
	{0x07, 0x02, 0x01, 0x10}
};

static const struct guid heci_asf_guid = {
	0x75B30CD6, 0xA29E, 0x4AF7,
	{0xA7, 0x12, 0xE6, 0x17, 0x43, 0x93, 0xC8, 0xA6}
};
const struct guid heci_wd_guid = {
	0x05B79A6F, 0x4628, 0x4D7F,
	{0x89, 0x9D, 0xA9, 0x15, 0x14, 0xCB, 0x32, 0xAB}
};
const struct guid heci_pthi_guid = {
	0x12f80028, 0xb4b7, 0x4b2d,
	{0xac, 0xa8, 0x46, 0xe0, 0xff, 0x65, 0x81, 0x4c}
};


/*
 *  heci init function prototypes
 */
static void heci_check_asf_mode(struct iamt_heci_device *dev);
static int host_start_message(struct iamt_heci_device *dev);
static int host_enum_clients_message(struct iamt_heci_device *dev);
static int allocate_me_clients_storage(struct iamt_heci_device *dev);
static void host_init_wd(struct iamt_heci_device *dev);
static void host_init_iamthif(struct iamt_heci_device *dev);
static int heci_wait_event_int_timeout(struct iamt_heci_device *dev,
				       long timeout);


/**
 * heci_initialize_list - Sets up a queue list.
 *
 * @list: An instance of our list structure
 * @dev: Device object for our driver
 */
void heci_initialize_list(struct io_heci_list *list,
			  struct iamt_heci_device *dev)
{
	/* initialize our queue list */
	INIT_LIST_HEAD(&list->heci_cb.cb_list);
	list->status = 0;
	list->device_extension = dev;
}

/**
 * heci_flush_queues - flush our queues list belong to file_ext.
 *
 * @dev: Device object for our driver
 * @file_ext: private data of the file object
 */
void heci_flush_queues(struct iamt_heci_device *dev,
		       struct heci_file_private *file_ext)
{
	int i;

	if (!dev || !file_ext)
		return;

	/* flush our queue list belong to file_ext */
	for (i = 0; i < HECI_IO_LISTS_NUMBER; i++) {
		DBG("remove list entry belong to file_ext\n");
		heci_flush_list(dev->io_list_array[i], file_ext);
	}
}


/**
 * heci_flush_list - remove list entry belong to file_ext.
 *
 * @list:  An instance of our list structure
 * @file_ext: private data of the file object
 */
void heci_flush_list(struct io_heci_list *list,
		struct heci_file_private *file_ext)
{
	struct heci_file_private *file_ext_tmp;
	struct heci_cb_private *priv_cb_pos = NULL;
	struct heci_cb_private *priv_cb_next = NULL;

	if (!list || !file_ext)
		return;

	if (list->status != 0)
		return;

	if (list_empty(&list->heci_cb.cb_list))
		return;

	list_for_each_entry_safe(priv_cb_pos, priv_cb_next,
				 &list->heci_cb.cb_list, cb_list) {
		if (priv_cb_pos) {
			file_ext_tmp = (struct heci_file_private *)
				priv_cb_pos->file_private;
			if (file_ext_tmp) {
				if (heci_fe_same_id(file_ext, file_ext_tmp))
					list_del(&priv_cb_pos->cb_list);
			}
		}
	}
}

/**
 * heci_reset_iamthif_params - initializes heci device iamthif
 *
 * @dev: The heci device structure
 */
static void heci_reset_iamthif_params(struct iamt_heci_device *dev)
{
	/* reset iamthif parameters. */
	dev->iamthif_current_cb = NULL;
	dev->iamthif_msg_buf_size = 0;
	dev->iamthif_msg_buf_index = 0;
	dev->iamthif_canceled = 0;
	dev->iamthif_file_ext.file = NULL;
	dev->iamthif_ioctl = 0;
	dev->iamthif_state = HECI_IAMTHIF_IDLE;
	dev->iamthif_timer = 0;
}

/**
 * init_heci_device - allocates and initializes the heci device structure
 *
 * @pdev: The pci device structure
 *
 * returns The heci_device_device pointer on success, NULL on failure.
 */
struct iamt_heci_device *init_heci_device(struct pci_dev *pdev)
{
	int i;
	struct iamt_heci_device *dev;

	dev = kzalloc(sizeof(struct iamt_heci_device), GFP_KERNEL);
	if (!dev)
		return NULL;

	/* setup our list array */
	dev->io_list_array[0] = &dev->read_list;
	dev->io_list_array[1] = &dev->write_list;
	dev->io_list_array[2] = &dev->write_waiting_list;
	dev->io_list_array[3] = &dev->ctrl_wr_list;
	dev->io_list_array[4] = &dev->ctrl_rd_list;
	dev->io_list_array[5] = &dev->pthi_cmd_list;
	dev->io_list_array[6] = &dev->pthi_read_complete_list;
	INIT_LIST_HEAD(&dev->file_list);
	INIT_LIST_HEAD(&dev->wd_file_ext.link);
	INIT_LIST_HEAD(&dev->iamthif_file_ext.link);
	spin_lock_init(&dev->device_lock);
	init_waitqueue_head(&dev->wait_recvd_msg);
	init_waitqueue_head(&dev->wait_stop_wd);
	dev->heci_state = HECI_INITIALIZING;
	dev->iamthif_state = HECI_IAMTHIF_IDLE;

	/* init work for schedule work */
	INIT_WORK(&dev->work, NULL);
	for (i = 0; i < HECI_IO_LISTS_NUMBER; i++)
		heci_initialize_list(dev->io_list_array[i], dev);
	dev->pdev = pdev;
	return dev;
}




static int heci_wait_event_int_timeout(struct iamt_heci_device *dev,
		long timeout)
{
	return wait_event_interruptible_timeout(dev->wait_recvd_msg,
			(dev->recvd_msg), timeout);
}

/**
 * heci_hw_init  - init host and fw to start work.
 *
 * @dev: Device object for our driver
 *
 * returns 0 on success, <0 on failure.
 */
int heci_hw_init(struct iamt_heci_device *dev)
{
	int err = 0;

	dev->host_hw_state = read_heci_register(dev, H_CSR);
	dev->me_hw_state = read_heci_register(dev, ME_CSR_HA);
	DBG("host_hw_state = 0x%08x, mestate = 0x%08x.\n",
	    dev->host_hw_state, dev->me_hw_state);

	if ((dev->host_hw_state & H_IS) == H_IS) {
		/* acknowledge interrupt and stop interupts */
		heci_csr_clear_his(dev);
	}
	dev->recvd_msg = 0;
	DBG("reset in start the heci device.\n");

	heci_reset(dev, 1);

	DBG("host_hw_state = 0x%08x, me_hw_state = 0x%08x.\n",
	    dev->host_hw_state, dev->me_hw_state);

	/* wait for ME to turn on ME_RDY */
	if (!dev->recvd_msg)
		err = heci_wait_event_int_timeout(dev, HECI_INTEROP_TIMEOUT);

	if (!err && !dev->recvd_msg) {
		dev->heci_state = HECI_DISABLED;
		DBG("wait_event_interruptible_timeout failed"
		    "on wait for ME to turn on ME_RDY.\n");
		return -ENODEV;
	} else {
		if (!(((dev->host_hw_state & H_RDY) == H_RDY)
		      && ((dev->me_hw_state & ME_RDY_HRA) == ME_RDY_HRA))) {
			dev->heci_state = HECI_DISABLED;
			DBG("host_hw_state = 0x%08x, me_hw_state = 0x%08x.\n",
			    dev->host_hw_state,
			    dev->me_hw_state);

			if (!(dev->host_hw_state & H_RDY) != H_RDY)
				DBG("host turn off H_RDY.\n");

			if (!(dev->me_hw_state & ME_RDY_HRA) != ME_RDY_HRA)
				DBG("ME turn off ME_RDY.\n");

			printk(KERN_ERR
			       "heci: link layer initialization failed.\n");
			return -ENODEV;
		}
	}
	dev->recvd_msg = 0;
	DBG("host_hw_state = 0x%08x, me_hw_state = 0x%08x.\n",
	    dev->host_hw_state, dev->me_hw_state);
	DBG("ME turn on ME_RDY and host turn on H_RDY.\n");
	printk(KERN_INFO "heci: link layer has been established.\n");
	return 0;
}

/**
 * heci_hw_reset - reset fw via heci csr register.
 *
 * @dev: Device object for our driver
 * @interrupts: if interrupt should be enable after reset.
 */
static void heci_hw_reset(struct iamt_heci_device *dev, int interrupts)
{
	dev->host_hw_state |= (H_RST | H_IG);

	if (interrupts)
		heci_csr_enable_interrupts(dev);
	else
		heci_csr_disable_interrupts(dev);

	BUG_ON((dev->host_hw_state & H_RST) != H_RST);
	BUG_ON((dev->host_hw_state & H_RDY) != 0);
}

/**
 * heci_reset - reset host and fw.
 *
 * @dev: Device object for our driver
 * @interrupts: if interrupt should be enable after reset.
 */
void heci_reset(struct iamt_heci_device *dev, int interrupts)
{
	struct heci_file_private *file_pos = NULL;
	struct heci_file_private *file_next = NULL;
	struct heci_cb_private *priv_cb_pos = NULL;
	struct heci_cb_private *priv_cb_next = NULL;
	int unexpected = 0;

	if (dev->heci_state == HECI_RECOVERING_FROM_RESET) {
		dev->need_reset = 1;
		return;
	}

	if (dev->heci_state != HECI_INITIALIZING &&
	    dev->heci_state != HECI_DISABLED &&
	    dev->heci_state != HECI_POWER_DOWN &&
	    dev->heci_state != HECI_POWER_UP)
		unexpected = 1;

	if (dev->reinit_tsk != NULL) {
		kthread_stop(dev->reinit_tsk);
		dev->reinit_tsk = NULL;
	}

	dev->host_hw_state = read_heci_register(dev, H_CSR);

	DBG("before reset host_hw_state = 0x%08x.\n",
	    dev->host_hw_state);

	heci_hw_reset(dev, interrupts);

	dev->host_hw_state &= ~H_RST;
	dev->host_hw_state |= H_IG;

	heci_set_csr_register(dev);

	DBG("currently saved host_hw_state = 0x%08x.\n",
	    dev->host_hw_state);

	dev->need_reset = 0;

	if (dev->heci_state != HECI_INITIALIZING) {
		if ((dev->heci_state != HECI_DISABLED) &&
		    (dev->heci_state != HECI_POWER_DOWN))
			dev->heci_state = HECI_RESETING;

		list_for_each_entry_safe(file_pos,
				file_next, &dev->file_list, link) {
			file_pos->state = HECI_FILE_DISCONNECTED;
			file_pos->flow_ctrl_creds = 0;
			file_pos->read_cb = NULL;
			file_pos->timer_count = 0;
		}
		/* remove entry if already in list */
		DBG("list del iamthif and wd file list.\n");
		heci_remove_client_from_file_list(dev,
				dev->wd_file_ext.host_client_id);

		heci_remove_client_from_file_list(dev,
				dev->iamthif_file_ext.host_client_id);

		heci_reset_iamthif_params(dev);
		dev->wd_due_counter = 0;
		dev->extra_write_index = 0;
	}

	dev->num_heci_me_clients = 0;
	dev->rd_msg_hdr = 0;
	dev->stop = 0;
	dev->wd_pending = 0;

	/* update the state of the registers after reset */
	dev->host_hw_state =  read_heci_register(dev, H_CSR);
	dev->me_hw_state =  read_heci_register(dev, ME_CSR_HA);

	DBG("after reset host_hw_state = 0x%08x, me_hw_state = 0x%08x.\n",
	    dev->host_hw_state, dev->me_hw_state);

	if (unexpected)
		printk(KERN_WARNING "heci: unexpected reset.\n");

	/* Wake up all readings so they can be interrupted */
	list_for_each_entry_safe(file_pos, file_next, &dev->file_list, link) {
		if (&file_pos->rx_wait &&
		    waitqueue_active(&file_pos->rx_wait)) {
			printk(KERN_INFO "heci: Waking up client!\n");
			wake_up_interruptible(&file_pos->rx_wait);
		}
	}
	/* remove all waiting requests */
	if (dev->write_list.status == 0 &&
		!list_empty(&dev->write_list.heci_cb.cb_list)) {
		list_for_each_entry_safe(priv_cb_pos, priv_cb_next,
				&dev->write_list.heci_cb.cb_list, cb_list) {
			if (priv_cb_pos) {
				list_del(&priv_cb_pos->cb_list);
				heci_free_cb_private(priv_cb_pos);
			}
		}
	}
}

/**
 * heci_initialize_clients - heci communication initialization.
 *
 * @dev: Device object for our driver
 */
int heci_initialize_clients(struct iamt_heci_device *dev)
{
	int status;

	msleep(100); /* FW needs time to be ready to talk with us */
	DBG("link is established start sending messages.\n");
	/* link is established start sending messages. */
	status = host_start_message(dev);
	if (status != 0) {
		spin_lock_bh(&dev->device_lock);
		dev->heci_state = HECI_DISABLED;
		spin_unlock_bh(&dev->device_lock);
		DBG("start sending messages failed.\n");
		return status;
	}

	/* enumerate clients */
	status = host_enum_clients_message(dev);
	if (status != 0) {
		spin_lock_bh(&dev->device_lock);
		dev->heci_state = HECI_DISABLED;
		spin_unlock_bh(&dev->device_lock);
		DBG("enum clients failed.\n");
		return status;
	}
	/* allocate storage for ME clients representation */
	status = allocate_me_clients_storage(dev);
	if (status != 0) {
		spin_lock_bh(&dev->device_lock);
		dev->num_heci_me_clients = 0;
		dev->heci_state = HECI_DISABLED;
		spin_unlock_bh(&dev->device_lock);
		DBG("allocate clients failed.\n");
		return status;
	}

	heci_check_asf_mode(dev);
	/*heci initialization wd */
	host_init_wd(dev);
	/*heci initialization iamthif client */
	host_init_iamthif(dev);

	spin_lock_bh(&dev->device_lock);
	if (dev->need_reset) {
		dev->need_reset = 0;
		dev->heci_state = HECI_DISABLED;
		spin_unlock_bh(&dev->device_lock);
		return -ENODEV;
	}

	memset(dev->heci_host_clients, 0, sizeof(dev->heci_host_clients));
	dev->open_handle_count = 0;
	dev->heci_host_clients[0] |= 7;
	dev->current_host_client_id = 3;
	dev->heci_state = HECI_ENABLED;
	spin_unlock_bh(&dev->device_lock);
	DBG("initialization heci clients successful.\n");
	return 0;
}

/**
 * heci_task_initialize_clients - heci reinitialization task
 *
 * @data: Device object for our driver
 */
int heci_task_initialize_clients(void *data)
{
	int ret;
	struct iamt_heci_device *dev = (struct iamt_heci_device *) data;

	spin_lock_bh(&dev->device_lock);
	if (dev->reinit_tsk != NULL) {
		spin_unlock_bh(&dev->device_lock);
		DBG("reinit task already started.\n");
		return 0;
	}
	dev->reinit_tsk = current;
	current->flags |= PF_NOFREEZE;
	spin_unlock_bh(&dev->device_lock);

	ret = heci_initialize_clients(dev);

	spin_lock_bh(&dev->device_lock);
	dev->reinit_tsk = NULL;
	spin_unlock_bh(&dev->device_lock);

	return ret;
}

/**
 * host_start_message - heci host send start message.
 *
 * @dev: Device object for our driver
 *
 * returns 0 on success, <0 on failure.
 */
static int host_start_message(struct iamt_heci_device *dev)
{
	long timeout = 60;	/* 60 second */

	struct heci_msg_hdr *heci_hdr;
	struct hbm_host_version_request *host_start_req;
	struct hbm_host_stop_request *host_stop_req;
	int err = 0;

	/* host start message */
	heci_hdr = (struct heci_msg_hdr *) &dev->wr_msg_buf[0];
	heci_hdr->host_addr = 0;
	heci_hdr->me_addr = 0;
	heci_hdr->length = sizeof(struct hbm_host_version_request);
	heci_hdr->msg_complete = 1;
	heci_hdr->reserved = 0;

	host_start_req =
	    (struct hbm_host_version_request *) &dev->wr_msg_buf[1];
	memset(host_start_req, 0, sizeof(struct hbm_host_version_request));
	host_start_req->cmd.cmd = HOST_START_REQ_CMD;
	host_start_req->host_version.major_version = HBM_MAJOR_VERSION;
	host_start_req->host_version.minor_version = HBM_MINOR_VERSION;
	dev->recvd_msg = 0;
	if (!heci_write_message(dev, heci_hdr,
				       (unsigned char *) (host_start_req),
				       heci_hdr->length)) {
		DBG("send version to fw fail.\n");
		return -ENODEV;
	}
	DBG("call wait_event_interruptible_timeout for response message.\n");
	/* wait for response */
	err = heci_wait_event_int_timeout(dev, timeout * HZ);
	if (!err && !dev->recvd_msg) {
		DBG("wait_timeout failed on host start response message.\n");
		return -ENODEV;
	}
	dev->recvd_msg = 0;
	DBG("wait_timeout successful on host start response message.\n");
	if ((dev->version.major_version != HBM_MAJOR_VERSION) ||
	    (dev->version.minor_version != HBM_MINOR_VERSION)) {
		/* send stop message */
		heci_hdr->host_addr = 0;
		heci_hdr->me_addr = 0;
		heci_hdr->length = sizeof(struct hbm_host_stop_request);
		heci_hdr->msg_complete = 1;
		heci_hdr->reserved = 0;

		host_stop_req =
		    (struct hbm_host_stop_request *) &dev->wr_msg_buf[1];

		memset(host_stop_req, 0, sizeof(struct hbm_host_stop_request));
		host_stop_req->cmd.cmd = HOST_STOP_REQ_CMD;
		host_stop_req->reason = DRIVER_STOP_REQUEST;
		heci_write_message(dev, heci_hdr,
				   (unsigned char *) (host_stop_req),
				   heci_hdr->length);
		DBG("version mismatch.\n");
		return -ENODEV;
	}

	return 0;
}

/**
 * host_enum_clients_message - host send enumeration client request message.
 *
 * @dev: Device object for our driver
 *
 * returns 0 on success, <0 on failure.
 */
static int host_enum_clients_message(struct iamt_heci_device *dev)
{
	long timeout = 5;	/*5 second */
	struct heci_msg_hdr *heci_hdr;
	struct hbm_host_enum_request *host_enum_req;
	int err = 0;
	int i, j;

	heci_hdr = (struct heci_msg_hdr *) &dev->wr_msg_buf[0];
	/* enumerate clients */
	heci_hdr->host_addr = 0;
	heci_hdr->me_addr = 0;
	heci_hdr->length = sizeof(struct hbm_host_enum_request);
	heci_hdr->msg_complete = 1;
	heci_hdr->reserved = 0;

	host_enum_req = (struct hbm_host_enum_request *) &dev->wr_msg_buf[1];
	memset(host_enum_req, 0, sizeof(struct hbm_host_enum_request));
	host_enum_req->cmd.cmd = HOST_ENUM_REQ_CMD;
	if (!heci_write_message(dev, heci_hdr,
			       (unsigned char *) (host_enum_req),
			       heci_hdr->length)) {
		DBG("send enumeration request failed.\n");
		return -ENODEV;
	}
	/* wait for response */
	dev->recvd_msg = 0;
	err = heci_wait_event_int_timeout(dev, timeout * HZ);
	if (!err && !dev->recvd_msg) {
		DBG("wait_event_interruptible_timeout failed "
				"on enumeration clients response message.\n");
		return -ENODEV;
	}
	dev->recvd_msg = 0;

	spin_lock_bh(&dev->device_lock);
	/* count how many ME clients we have */
	for (i = 0; i < sizeof(dev->heci_me_clients); i++) {
		for (j = 0; j < 8; j++) {
			if ((dev->heci_me_clients[i] & (1 << j)) != 0)
				dev->num_heci_me_clients++;

		}
	}
	spin_unlock_bh(&dev->device_lock);

	return 0;
}

/**
 * host_client_properties - reads properties for client
 *
 * @dev: Device object for our driver
 * @idx: client index in me client array
 * @client_id: id of the client
 *
 * returns 0 on success, <0 on failure.
 */
static int host_client_properties(struct iamt_heci_device *dev,
				  struct heci_me_client *client)
{
	struct heci_msg_hdr *heci_hdr;
	struct hbm_props_request *host_cli_req;
	int err;

	heci_hdr = (struct heci_msg_hdr *) &dev->wr_msg_buf[0];
	heci_hdr->host_addr = 0;
	heci_hdr->me_addr = 0;
	heci_hdr->length = sizeof(struct hbm_props_request);
	heci_hdr->msg_complete = 1;
	heci_hdr->reserved = 0;

	host_cli_req = (struct hbm_props_request *) &dev->wr_msg_buf[1];
	memset(host_cli_req, 0, sizeof(struct hbm_props_request));
	host_cli_req->cmd.cmd = HOST_CLIENT_PROPERTEIS_REQ_CMD;
	host_cli_req->address = client->client_id;
	if (!heci_write_message(dev, heci_hdr,
				(unsigned char *) (host_cli_req),
				heci_hdr->length)) {
		DBG("send props request failed.\n");
		return -ENODEV;
	}
	/* wait for response */
	dev->recvd_msg = 0;
	err = heci_wait_event_int_timeout(dev, 10 * HZ);
	if (!err && !dev->recvd_msg) {
		DBG("wait failed on props resp msg.\n");
		return -ENODEV;
	}
	dev->recvd_msg = 0;
	return 0;
}

/**
 * allocate_me_clients_storage - allocate storage for me clients
 *
 * @dev: Device object for our driver
 *
 * returns 0 on success, <0 on failure.
 */
static int allocate_me_clients_storage(struct iamt_heci_device *dev)
{
	struct heci_me_client *clients;
	struct heci_me_client *client;
	__u8 num, i, j;
	int err;

	if (dev->num_heci_me_clients <= 0)
		return 0;

	spin_lock_bh(&dev->device_lock);
	kfree(dev->me_clients);
	dev->me_clients = NULL;
	spin_unlock_bh(&dev->device_lock);

	/* allocate storage for ME clients representation */
	clients = kcalloc(dev->num_heci_me_clients,
			sizeof(struct heci_me_client), GFP_KERNEL);
	if (!clients) {
		DBG("memory allocation for ME clients failed.\n");
		return -ENOMEM;
	}

	spin_lock_bh(&dev->device_lock);
	dev->me_clients = clients;
	spin_unlock_bh(&dev->device_lock);

	num = 0;
	for (i = 0; i < sizeof(dev->heci_me_clients); i++) {
		for (j = 0; j < 8; j++) {
			if ((dev->heci_me_clients[i] & (1 << j)) != 0) {
				client = &dev->me_clients[num];
				client->client_id = (i * 8) + j;
				client->flow_ctrl_creds = 0;
				err = host_client_properties(dev, client);
				if (err != 0) {
					spin_lock_bh(&dev->device_lock);
					kfree(dev->me_clients);
					dev->me_clients = NULL;
					spin_unlock_bh(&dev->device_lock);
					return err;
				}
				num++;
			}
		}
	}

	return 0;
}

/**
 * heci_init_file_private - initializes private file structure.
 *
 * @priv: private file structure to be initialized
 * @file: the file structure
 */
static void heci_init_file_private(struct heci_file_private *priv,
				   struct file *file)
{
	memset(priv, 0, sizeof(struct heci_file_private));
	spin_lock_init(&priv->file_lock);
	spin_lock_init(&priv->read_io_lock);
	spin_lock_init(&priv->write_io_lock);
	init_waitqueue_head(&priv->wait);
	init_waitqueue_head(&priv->rx_wait);
	DBG("priv->rx_wait =%p\n", &priv->rx_wait);
	init_waitqueue_head(&priv->tx_wait);
	INIT_LIST_HEAD(&priv->link);
	priv->reading_state = HECI_IDLE;
	priv->writing_state = HECI_IDLE;
}

/**
 * heci_find_me_client - search for ME client guid
 *                       sets client_id in heci_file_private if found
 * @dev: Device object for our driver
 * @priv: private file structure to set client_id in
 * @cguid: searched guid of ME client
 * @client_id: id of host client to be set in file private structure
 *
 * returns ME client index
 */
static __u8 heci_find_me_client(struct iamt_heci_device *dev,
				struct heci_file_private *priv,
				const struct guid *cguid, __u8 client_id)
{
	__u8 i;

	if ((dev == NULL) || (priv == NULL) || (cguid == NULL))
		return 0;

	for (i = 0; i < dev->num_heci_me_clients; i++) {
		if (memcmp(cguid,
			   &dev->me_clients[i].props.protocol_name,
			   sizeof(struct guid)) == 0) {
			priv->me_client_id = dev->me_clients[i].client_id;
			priv->state = HECI_FILE_CONNECTING;
			priv->host_client_id = client_id;

			list_add_tail(&priv->link, &dev->file_list);
			return i;
		}
	}
	return 0;
}

/**
 * heci_check_asf_mode - check for ASF client
 *
 * @dev: Device object for our driver
 */
static void heci_check_asf_mode(struct iamt_heci_device *dev)
{
	__u8 i;

	spin_lock_bh(&dev->device_lock);
	dev->asf_mode = 0;
	/* find ME ASF client - otherwise assume AMT mode */
	DBG("find ME ASF client - otherwise assume AMT mode.\n");
	for (i = 0; i < dev->num_heci_me_clients; i++) {
		if (memcmp(&heci_asf_guid,
				&dev->me_clients[i].props.protocol_name,
				sizeof(struct guid)) == 0) {
			dev->asf_mode = 1;
			spin_unlock_bh(&dev->device_lock);
			DBG("found ME ASF client.\n");
			return;
		}
	}
	spin_unlock_bh(&dev->device_lock);
	DBG("assume AMT mode.\n");
}

/**
 * heci_connect_me_client - connect ME client
 * @dev: Device object for our driver
 * @priv: private file structure
 * @timeout: connect timeout in seconds
 *
 * returns 1 - if connected, 0 - if not
 */
static __u8 heci_connect_me_client(struct iamt_heci_device *dev,
				   struct heci_file_private *priv,
				   long timeout)
{
	int err = 0;

	if ((dev == NULL) || (priv == NULL))
		return 0;

	if (!heci_connect(dev, priv)) {
		DBG("failed to call heci_connect for client_id=%d.\n",
		    priv->host_client_id);
		spin_lock_bh(&dev->device_lock);
		heci_remove_client_from_file_list(dev, priv->host_client_id);
		priv->state = HECI_FILE_DISCONNECTED;
		spin_unlock_bh(&dev->device_lock);
		return 0;
	}

	err = wait_event_timeout(dev->wait_recvd_msg,
	    (HECI_FILE_CONNECTED == priv->state ||
	     HECI_FILE_DISCONNECTED == priv->state),
	    timeout * HZ);
	if (HECI_FILE_CONNECTED != priv->state) {
		spin_lock_bh(&dev->device_lock);
		heci_remove_client_from_file_list(dev, priv->host_client_id);
		DBG("failed to connect client_id=%d state=%d.\n",
		    priv->host_client_id, priv->state);
		if (err)
			DBG("failed connect err=%08x\n", err);
		priv->state = HECI_FILE_DISCONNECTED;
		spin_unlock_bh(&dev->device_lock);
		return 0;
	}
	DBG("successfully connected client_id=%d.\n",
	    priv->host_client_id);
	return 1;
}

/**
 * host_init_wd - heci initialization wd.
 *
 * @dev: Device object for our driver
 */
static void host_init_wd(struct iamt_heci_device *dev)
{
	spin_lock_bh(&dev->device_lock);

	heci_init_file_private(&dev->wd_file_ext, NULL);

	/* look for WD client and connect to it */
	dev->wd_file_ext.state = HECI_FILE_DISCONNECTED;
	dev->wd_timeout = 0;

	if (dev->asf_mode) {
		memcpy(dev->wd_data, heci_stop_wd_params, HECI_WD_PARAMS_SIZE);
	} else {
		/* AMT mode */
		dev->wd_timeout = AMT_WD_VALUE;
		DBG("dev->wd_timeout=%d.\n", dev->wd_timeout);
		memcpy(dev->wd_data, heci_start_wd_params, HECI_WD_PARAMS_SIZE);
		memcpy(dev->wd_data + HECI_WD_PARAMS_SIZE,
		       &dev->wd_timeout, sizeof(__u16));
	}

	/* find ME WD client */
	heci_find_me_client(dev, &dev->wd_file_ext,
			    &heci_wd_guid, HECI_WD_HOST_CLIENT_ID);
	spin_unlock_bh(&dev->device_lock);

	DBG("check wd_file_ext\n");
	if (HECI_FILE_CONNECTING == dev->wd_file_ext.state) {
		if (heci_connect_me_client(dev, &dev->wd_file_ext, 15) == 1) {
			DBG("dev->wd_timeout=%d.\n", dev->wd_timeout);
			if (dev->wd_timeout != 0)
				dev->wd_due_counter = 1;
			else
				dev->wd_due_counter = 0;
			DBG("successfully connected to WD client.\n");
		}
	} else
		DBG("failed to find WD client.\n");


	spin_lock_bh(&dev->device_lock);
	dev->wd_timer.function = &heci_wd_timer;
	dev->wd_timer.data = (unsigned long) dev;
	spin_unlock_bh(&dev->device_lock);
}


/**
 * host_init_iamthif - heci initialization iamthif client.
 *
 * @dev: Device object for our driver
 *
 */
static void host_init_iamthif(struct iamt_heci_device *dev)
{
	__u8 i;

	spin_lock_bh(&dev->device_lock);

	heci_init_file_private(&dev->iamthif_file_ext, NULL);
	dev->iamthif_file_ext.state = HECI_FILE_DISCONNECTED;

	/* find ME PTHI client */
	i = heci_find_me_client(dev, &dev->iamthif_file_ext,
			    &heci_pthi_guid, HECI_IAMTHIF_HOST_CLIENT_ID);
	if (dev->iamthif_file_ext.state != HECI_FILE_CONNECTING) {
		DBG("failed to find iamthif client.\n");
		spin_unlock_bh(&dev->device_lock);
		return;
	}

	BUG_ON(dev->me_clients[i].props.max_msg_length != IAMTHIF_MTU);

	spin_unlock_bh(&dev->device_lock);
	if (heci_connect_me_client(dev, &dev->iamthif_file_ext, 15) == 1) {
		DBG("connected to iamthif client.\n");
		dev->iamthif_state = HECI_IAMTHIF_IDLE;
	}
}

/**
 * heci_alloc_file_private - allocates a private file structure and set it up.
 * @file: the file structure
 *
 * returns  The allocated file or NULL on failure
 */
struct heci_file_private *heci_alloc_file_private(struct file *file)
{
	struct heci_file_private *priv;

	priv = kmalloc(sizeof(struct heci_file_private), GFP_KERNEL);
	if (!priv)
		return NULL;

	heci_init_file_private(priv, file);

	return priv;
}



/**
 * heci_disconnect_host_client - send disconnect message  to fw from host client.
 *
 * @dev: Device object for our driver
 * @file_ext: private data of the file object
 *
 * returns 0 on success, <0 on failure.
 */
int heci_disconnect_host_client(struct iamt_heci_device *dev,
		struct heci_file_private *file_ext)
{
	int rets, err;
	long timeout = 15;	/* 15 seconds */
	struct heci_cb_private *priv_cb;

	if ((!dev) || (!file_ext))
		return -ENODEV;

	if (file_ext->state != HECI_FILE_DISCONNECTING)
		return 0;

	priv_cb = kzalloc(sizeof(struct heci_cb_private), GFP_KERNEL);
	if (!priv_cb)
		return -ENOMEM;

	INIT_LIST_HEAD(&priv_cb->cb_list);
	priv_cb->file_private = file_ext;
	priv_cb->major_file_operations = HECI_CLOSE;
	spin_lock_bh(&dev->device_lock);
	if (dev->host_buffer_is_empty) {
		dev->host_buffer_is_empty = 0;
		if (heci_disconnect(dev, file_ext)) {
			mdelay(10); /* Wait for hardware disconnection ready */
			list_add_tail(&priv_cb->cb_list,
				&dev->ctrl_rd_list.heci_cb.cb_list);
		} else {
			spin_unlock_bh(&dev->device_lock);
			rets = -ENODEV;
			DBG("failed to call heci_disconnect.\n");
			goto free;
		}
	} else {
		DBG("add disconnect cb to control write list\n");
		list_add_tail(&priv_cb->cb_list,
				&dev->ctrl_wr_list.heci_cb.cb_list);
	}
	spin_unlock_bh(&dev->device_lock);

	err = wait_event_timeout(dev->wait_recvd_msg,
		 (HECI_FILE_DISCONNECTED == file_ext->state),
		 timeout * HZ);
	if (HECI_FILE_DISCONNECTED == file_ext->state) {
		rets = 0;
		DBG("successfully disconnected from fw client.\n");
	} else {
		rets = -ENODEV;
		if (HECI_FILE_DISCONNECTED != file_ext->state)
			DBG("wrong status client disconnect.\n");

		if (err)
			DBG("wait failed disconnect err=%08x\n", err);

		DBG("failed to disconnect from fw client.\n");
	}

	spin_lock_bh(&dev->device_lock);
	heci_flush_list(&dev->ctrl_rd_list, file_ext);
	heci_flush_list(&dev->ctrl_wr_list, file_ext);
	spin_unlock_bh(&dev->device_lock);
free:
	heci_free_cb_private(priv_cb);
	return rets;
}

/**
 * heci_remove_client_from_file_list -
 *	remove file private data from device file list
 *
 * @dev: Device object for our driver
 * @host_client_id: host client id to be removed
 */
void heci_remove_client_from_file_list(struct iamt_heci_device *dev,
				       __u8 host_client_id)
{
	struct heci_file_private *file_pos = NULL;
	struct heci_file_private *file_next = NULL;
	list_for_each_entry_safe(file_pos, file_next, &dev->file_list, link) {
		if (host_client_id == file_pos->host_client_id) {
			DBG("remove host client = %d, ME client = %d\n",
					file_pos->host_client_id,
					file_pos->me_client_id);
			list_del_init(&file_pos->link);
			break;
		}
	}
}
