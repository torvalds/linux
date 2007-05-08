/*
 * SBP2 driver (SCSI over IEEE1394)
 *
 * Copyright (C) 2005-2007  Kristian Hoegsberg <krh@bitplanet.net>
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * The basic structure of this driver is based on the old storage driver,
 * drivers/ieee1394/sbp2.c, originally written by
 *     James Goodwin <jamesg@filanet.com>
 * with later contributions and ongoing maintenance from
 *     Ben Collins <bcollins@debian.org>,
 *     Stefan Richter <stefanr@s5r6.in-berlin.de>
 * and many others.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/timer.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "fw-transaction.h"
#include "fw-topology.h"
#include "fw-device.h"

/* I don't know why the SCSI stack doesn't define something like this... */
typedef void (*scsi_done_fn_t) (struct scsi_cmnd *);

static const char sbp2_driver_name[] = "sbp2";

struct sbp2_device {
	struct kref kref;
	struct fw_unit *unit;
	struct fw_address_handler address_handler;
	struct list_head orb_list;
	u64 management_agent_address;
	u64 command_block_agent_address;
	u32 workarounds;
	int login_id;

	/*
	 * We cache these addresses and only update them once we've
	 * logged in or reconnected to the sbp2 device.  That way, any
	 * IO to the device will automatically fail and get retried if
	 * it happens in a window where the device is not ready to
	 * handle it (e.g. after a bus reset but before we reconnect).
	 */
	int node_id;
	int address_high;
	int generation;

	int retries;
	struct delayed_work work;
	struct Scsi_Host *scsi_host;
};

#define SBP2_MAX_SG_ELEMENT_LENGTH	0xf000
#define SBP2_MAX_SECTORS		255	/* Max sectors supported */
#define SBP2_ORB_TIMEOUT		2000	/* Timeout in ms */

#define SBP2_ORB_NULL			0x80000000

#define SBP2_DIRECTION_TO_MEDIA		0x0
#define SBP2_DIRECTION_FROM_MEDIA	0x1

/* Unit directory keys */
#define SBP2_COMMAND_SET_SPECIFIER	0x38
#define SBP2_COMMAND_SET		0x39
#define SBP2_COMMAND_SET_REVISION	0x3b
#define SBP2_FIRMWARE_REVISION		0x3c

/* Flags for detected oddities and brokeness */
#define SBP2_WORKAROUND_128K_MAX_TRANS	0x1
#define SBP2_WORKAROUND_INQUIRY_36	0x2
#define SBP2_WORKAROUND_MODE_SENSE_8	0x4
#define SBP2_WORKAROUND_FIX_CAPACITY	0x8
#define SBP2_WORKAROUND_OVERRIDE	0x100

/* Management orb opcodes */
#define SBP2_LOGIN_REQUEST		0x0
#define SBP2_QUERY_LOGINS_REQUEST	0x1
#define SBP2_RECONNECT_REQUEST		0x3
#define SBP2_SET_PASSWORD_REQUEST	0x4
#define SBP2_LOGOUT_REQUEST		0x7
#define SBP2_ABORT_TASK_REQUEST		0xb
#define SBP2_ABORT_TASK_SET		0xc
#define SBP2_LOGICAL_UNIT_RESET		0xe
#define SBP2_TARGET_RESET_REQUEST	0xf

/* Offsets for command block agent registers */
#define SBP2_AGENT_STATE		0x00
#define SBP2_AGENT_RESET		0x04
#define SBP2_ORB_POINTER		0x08
#define SBP2_DOORBELL			0x10
#define SBP2_UNSOLICITED_STATUS_ENABLE	0x14

/* Status write response codes */
#define SBP2_STATUS_REQUEST_COMPLETE	0x0
#define SBP2_STATUS_TRANSPORT_FAILURE	0x1
#define SBP2_STATUS_ILLEGAL_REQUEST	0x2
#define SBP2_STATUS_VENDOR_DEPENDENT	0x3

#define status_get_orb_high(v)		((v).status & 0xffff)
#define status_get_sbp_status(v)	(((v).status >> 16) & 0xff)
#define status_get_len(v)		(((v).status >> 24) & 0x07)
#define status_get_dead(v)		(((v).status >> 27) & 0x01)
#define status_get_response(v)		(((v).status >> 28) & 0x03)
#define status_get_source(v)		(((v).status >> 30) & 0x03)
#define status_get_orb_low(v)		((v).orb_low)
#define status_get_data(v)		((v).data)

struct sbp2_status {
	u32 status;
	u32 orb_low;
	u8 data[24];
};

struct sbp2_pointer {
	u32 high;
	u32 low;
};

struct sbp2_orb {
	struct fw_transaction t;
	dma_addr_t request_bus;
	int rcode;
	struct sbp2_pointer pointer;
	void (*callback) (struct sbp2_orb * orb, struct sbp2_status * status);
	struct list_head link;
};

#define management_orb_lun(v)			((v))
#define management_orb_function(v)		((v) << 16)
#define management_orb_reconnect(v)		((v) << 20)
#define management_orb_exclusive		((1) << 28)
#define management_orb_request_format(v)	((v) << 29)
#define management_orb_notify			((1) << 31)

#define management_orb_response_length(v)	((v))
#define management_orb_password_length(v)	((v) << 16)

struct sbp2_management_orb {
	struct sbp2_orb base;
	struct {
		struct sbp2_pointer password;
		struct sbp2_pointer response;
		u32 misc;
		u32 length;
		struct sbp2_pointer status_fifo;
	} request;
	__be32 response[4];
	dma_addr_t response_bus;
	struct completion done;
	struct sbp2_status status;
};

#define login_response_get_login_id(v)	((v).misc & 0xffff)
#define login_response_get_length(v)	(((v).misc >> 16) & 0xffff)

struct sbp2_login_response {
	u32 misc;
	struct sbp2_pointer command_block_agent;
	u32 reconnect_hold;
};

#define command_orb_data_size(v)	((v))
#define command_orb_page_size(v)	((v) << 16)
#define command_orb_page_table_present	((1) << 19)
#define command_orb_max_payload(v)	((v) << 20)
#define command_orb_speed(v)		((v) << 24)
#define command_orb_direction(v)	((v) << 27)
#define command_orb_request_format(v)	((v) << 29)
#define command_orb_notify		((1) << 31)

struct sbp2_command_orb {
	struct sbp2_orb base;
	struct {
		struct sbp2_pointer next;
		struct sbp2_pointer data_descriptor;
		u32 misc;
		u8 command_block[12];
	} request;
	struct scsi_cmnd *cmd;
	scsi_done_fn_t done;
	struct fw_unit *unit;

	struct sbp2_pointer page_table[SG_ALL];
	dma_addr_t page_table_bus;
	dma_addr_t request_buffer_bus;
};

/*
 * List of devices with known bugs.
 *
 * The firmware_revision field, masked with 0xffff00, is the best
 * indicator for the type of bridge chip of a device.  It yields a few
 * false positives but this did not break correctly behaving devices
 * so far.  We use ~0 as a wildcard, since the 24 bit values we get
 * from the config rom can never match that.
 */
static const struct {
	u32 firmware_revision;
	u32 model;
	unsigned workarounds;
} sbp2_workarounds_table[] = {
	/* DViCO Momobay CX-1 with TSB42AA9 bridge */ {
		.firmware_revision	= 0x002800,
		.model			= 0x001010,
		.workarounds		= SBP2_WORKAROUND_INQUIRY_36 |
					  SBP2_WORKAROUND_MODE_SENSE_8,
	},
	/* Initio bridges, actually only needed for some older ones */ {
		.firmware_revision	= 0x000200,
		.model			= ~0,
		.workarounds		= SBP2_WORKAROUND_INQUIRY_36,
	},
	/* Symbios bridge */ {
		.firmware_revision	= 0xa0b800,
		.model			= ~0,
		.workarounds		= SBP2_WORKAROUND_128K_MAX_TRANS,
	},

	/*
	 * There are iPods (2nd gen, 3rd gen) with model_id == 0, but
	 * these iPods do not feature the read_capacity bug according
	 * to one report.  Read_capacity behaviour as well as model_id
	 * could change due to Apple-supplied firmware updates though.
	 */

	/* iPod 4th generation. */ {
		.firmware_revision	= 0x0a2700,
		.model			= 0x000021,
		.workarounds		= SBP2_WORKAROUND_FIX_CAPACITY,
	},
	/* iPod mini */ {
		.firmware_revision	= 0x0a2700,
		.model			= 0x000023,
		.workarounds		= SBP2_WORKAROUND_FIX_CAPACITY,
	},
	/* iPod Photo */ {
		.firmware_revision	= 0x0a2700,
		.model			= 0x00007e,
		.workarounds		= SBP2_WORKAROUND_FIX_CAPACITY,
	}
};

static void
sbp2_status_write(struct fw_card *card, struct fw_request *request,
		  int tcode, int destination, int source,
		  int generation, int speed,
		  unsigned long long offset,
		  void *payload, size_t length, void *callback_data)
{
	struct sbp2_device *sd = callback_data;
	struct sbp2_orb *orb;
	struct sbp2_status status;
	size_t header_size;
	unsigned long flags;

	if (tcode != TCODE_WRITE_BLOCK_REQUEST ||
	    length == 0 || length > sizeof status) {
		fw_send_response(card, request, RCODE_TYPE_ERROR);
		return;
	}

	header_size = min(length, 2 * sizeof(u32));
	fw_memcpy_from_be32(&status, payload, header_size);
	if (length > header_size)
		memcpy(status.data, payload + 8, length - header_size);
	if (status_get_source(status) == 2 || status_get_source(status) == 3) {
		fw_notify("non-orb related status write, not handled\n");
		fw_send_response(card, request, RCODE_COMPLETE);
		return;
	}

	/* Lookup the orb corresponding to this status write. */
	spin_lock_irqsave(&card->lock, flags);
	list_for_each_entry(orb, &sd->orb_list, link) {
		if (status_get_orb_high(status) == 0 &&
		    status_get_orb_low(status) == orb->request_bus &&
		    orb->rcode == RCODE_COMPLETE) {
			list_del(&orb->link);
			break;
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);

	if (&orb->link != &sd->orb_list)
		orb->callback(orb, &status);
	else
		fw_error("status write for unknown orb\n");

	fw_send_response(card, request, RCODE_COMPLETE);
}

static void
complete_transaction(struct fw_card *card, int rcode,
		     void *payload, size_t length, void *data)
{
	struct sbp2_orb *orb = data;
	unsigned long flags;

	orb->rcode = rcode;
	if (rcode != RCODE_COMPLETE) {
		spin_lock_irqsave(&card->lock, flags);
		list_del(&orb->link);
		spin_unlock_irqrestore(&card->lock, flags);
		orb->callback(orb, NULL);
	}
}

static void
sbp2_send_orb(struct sbp2_orb *orb, struct fw_unit *unit,
	      int node_id, int generation, u64 offset)
{
	struct fw_device *device = fw_device(unit->device.parent);
	struct sbp2_device *sd = unit->device.driver_data;
	unsigned long flags;

	orb->pointer.high = 0;
	orb->pointer.low = orb->request_bus;
	fw_memcpy_to_be32(&orb->pointer, &orb->pointer, sizeof orb->pointer);

	spin_lock_irqsave(&device->card->lock, flags);
	list_add_tail(&orb->link, &sd->orb_list);
	spin_unlock_irqrestore(&device->card->lock, flags);

	fw_send_request(device->card, &orb->t, TCODE_WRITE_BLOCK_REQUEST,
			node_id, generation,
			device->node->max_speed, offset,
			&orb->pointer, sizeof orb->pointer,
			complete_transaction, orb);
}

static int sbp2_cancel_orbs(struct fw_unit *unit)
{
	struct fw_device *device = fw_device(unit->device.parent);
	struct sbp2_device *sd = unit->device.driver_data;
	struct sbp2_orb *orb, *next;
	struct list_head list;
	unsigned long flags;
	int retval = -ENOENT;

	INIT_LIST_HEAD(&list);
	spin_lock_irqsave(&device->card->lock, flags);
	list_splice_init(&sd->orb_list, &list);
	spin_unlock_irqrestore(&device->card->lock, flags);

	list_for_each_entry_safe(orb, next, &list, link) {
		retval = 0;
		if (fw_cancel_transaction(device->card, &orb->t) == 0)
			continue;

		orb->rcode = RCODE_CANCELLED;
		orb->callback(orb, NULL);
	}

	return retval;
}

static void
complete_management_orb(struct sbp2_orb *base_orb, struct sbp2_status *status)
{
	struct sbp2_management_orb *orb =
	    (struct sbp2_management_orb *)base_orb;

	if (status)
		memcpy(&orb->status, status, sizeof *status);
	complete(&orb->done);
}

static int
sbp2_send_management_orb(struct fw_unit *unit, int node_id, int generation,
			 int function, int lun, void *response)
{
	struct fw_device *device = fw_device(unit->device.parent);
	struct sbp2_device *sd = unit->device.driver_data;
	struct sbp2_management_orb *orb;
	int retval = -ENOMEM;

	orb = kzalloc(sizeof *orb, GFP_ATOMIC);
	if (orb == NULL)
		return -ENOMEM;

	/*
	 * The sbp2 device is going to send a block read request to
	 * read out the request from host memory, so map it for dma.
	 */
	orb->base.request_bus =
		dma_map_single(device->card->device, &orb->request,
			       sizeof orb->request, DMA_TO_DEVICE);
	if (dma_mapping_error(orb->base.request_bus))
		goto out;

	orb->response_bus =
		dma_map_single(device->card->device, &orb->response,
			       sizeof orb->response, DMA_FROM_DEVICE);
	if (dma_mapping_error(orb->response_bus))
		goto out;

	orb->request.response.high    = 0;
	orb->request.response.low     = orb->response_bus;

	orb->request.misc =
		management_orb_notify |
		management_orb_function(function) |
		management_orb_lun(lun);
	orb->request.length =
		management_orb_response_length(sizeof orb->response);

	orb->request.status_fifo.high = sd->address_handler.offset >> 32;
	orb->request.status_fifo.low  = sd->address_handler.offset;

	/*
	 * FIXME: Yeah, ok this isn't elegant, we hardwire exclusive
	 * login and 1 second reconnect time.  The reconnect setting
	 * is probably fine, but the exclusive login should be an option.
	 */
	if (function == SBP2_LOGIN_REQUEST) {
		orb->request.misc |=
			management_orb_exclusive |
			management_orb_reconnect(0);
	}

	fw_memcpy_to_be32(&orb->request, &orb->request, sizeof orb->request);

	init_completion(&orb->done);
	orb->base.callback = complete_management_orb;

	sbp2_send_orb(&orb->base, unit,
		      node_id, generation, sd->management_agent_address);

	wait_for_completion_timeout(&orb->done,
				    msecs_to_jiffies(SBP2_ORB_TIMEOUT));

	retval = -EIO;
	if (sbp2_cancel_orbs(unit) == 0) {
		fw_error("orb reply timed out, rcode=0x%02x\n",
			 orb->base.rcode);
		goto out;
	}

	if (orb->base.rcode != RCODE_COMPLETE) {
		fw_error("management write failed, rcode 0x%02x\n",
			 orb->base.rcode);
		goto out;
	}

	if (status_get_response(orb->status) != 0 ||
	    status_get_sbp_status(orb->status) != 0) {
		fw_error("error status: %d:%d\n",
			 status_get_response(orb->status),
			 status_get_sbp_status(orb->status));
		goto out;
	}

	retval = 0;
 out:
	dma_unmap_single(device->card->device, orb->base.request_bus,
			 sizeof orb->request, DMA_TO_DEVICE);
	dma_unmap_single(device->card->device, orb->response_bus,
			 sizeof orb->response, DMA_FROM_DEVICE);

	if (response)
		fw_memcpy_from_be32(response,
				    orb->response, sizeof orb->response);
	kfree(orb);

	return retval;
}

static void
complete_agent_reset_write(struct fw_card *card, int rcode,
			   void *payload, size_t length, void *data)
{
	struct fw_transaction *t = data;

	kfree(t);
}

static int sbp2_agent_reset(struct fw_unit *unit)
{
	struct fw_device *device = fw_device(unit->device.parent);
	struct sbp2_device *sd = unit->device.driver_data;
	struct fw_transaction *t;
	static u32 zero;

	t = kzalloc(sizeof *t, GFP_ATOMIC);
	if (t == NULL)
		return -ENOMEM;

	fw_send_request(device->card, t, TCODE_WRITE_QUADLET_REQUEST,
			sd->node_id, sd->generation, SCODE_400,
			sd->command_block_agent_address + SBP2_AGENT_RESET,
			&zero, sizeof zero, complete_agent_reset_write, t);

	return 0;
}

static int add_scsi_devices(struct fw_unit *unit);
static void remove_scsi_devices(struct fw_unit *unit);
static void sbp2_reconnect(struct work_struct *work);

static void
release_sbp2_device(struct kref *kref)
{
	struct sbp2_device *sd = container_of(kref, struct sbp2_device, kref);

	sbp2_send_management_orb(sd->unit, sd->node_id, sd->generation,
				 SBP2_LOGOUT_REQUEST, sd->login_id, NULL);

	remove_scsi_devices(sd->unit);

	fw_core_remove_address_handler(&sd->address_handler);
	fw_notify("removed sbp2 unit %s\n", sd->unit->device.bus_id);
	put_device(&sd->unit->device);
	kfree(sd);
}

static void sbp2_login(struct work_struct *work)
{
	struct sbp2_device *sd =
		container_of(work, struct sbp2_device, work.work);
	struct fw_unit *unit = sd->unit;
	struct fw_device *device = fw_device(unit->device.parent);
	struct sbp2_login_response response;
	int generation, node_id, local_node_id, lun, retval;

	/* FIXME: Make this work for multi-lun devices. */
	lun = 0;

	generation    = device->card->generation;
	node_id       = device->node->node_id;
	local_node_id = device->card->local_node->node_id;

	if (sbp2_send_management_orb(unit, node_id, generation,
				     SBP2_LOGIN_REQUEST, lun, &response) < 0) {
		if (sd->retries++ < 5) {
			schedule_delayed_work(&sd->work, DIV_ROUND_UP(HZ, 5));
		} else {
			fw_error("failed to login to %s\n",
				 unit->device.bus_id);
			remove_scsi_devices(unit);
			kref_put(&sd->kref, release_sbp2_device);
		}
		return;
	}

	sd->generation   = generation;
	sd->node_id      = node_id;
	sd->address_high = local_node_id << 16;

	/* Get command block agent offset and login id. */
	sd->command_block_agent_address =
		((u64) (response.command_block_agent.high & 0xffff) << 32) |
		response.command_block_agent.low;
	sd->login_id = login_response_get_login_id(response);

	fw_notify("logged in to sbp2 unit %s (%d retries)\n",
		  unit->device.bus_id, sd->retries);
	fw_notify(" - management_agent_address:    0x%012llx\n",
		  (unsigned long long) sd->management_agent_address);
	fw_notify(" - command_block_agent_address: 0x%012llx\n",
		  (unsigned long long) sd->command_block_agent_address);
	fw_notify(" - status write address:        0x%012llx\n",
		  (unsigned long long) sd->address_handler.offset);

#if 0
	/* FIXME: The linux1394 sbp2 does this last step. */
	sbp2_set_busy_timeout(scsi_id);
#endif

	PREPARE_DELAYED_WORK(&sd->work, sbp2_reconnect);
	sbp2_agent_reset(unit);

	retval = add_scsi_devices(unit);
	if (retval < 0) {
		sbp2_send_management_orb(unit, sd->node_id, sd->generation,
					 SBP2_LOGOUT_REQUEST, sd->login_id,
					 NULL);
		/*
		 * Set this back to sbp2_login so we fall back and
		 * retry login on bus reset.
		 */
		PREPARE_DELAYED_WORK(&sd->work, sbp2_login);
	}
	kref_put(&sd->kref, release_sbp2_device);
}

static int sbp2_probe(struct device *dev)
{
	struct fw_unit *unit = fw_unit(dev);
	struct fw_device *device = fw_device(unit->device.parent);
	struct sbp2_device *sd;
	struct fw_csr_iterator ci;
	int i, key, value;
	u32 model, firmware_revision;

	sd = kzalloc(sizeof *sd, GFP_KERNEL);
	if (sd == NULL)
		return -ENOMEM;

	unit->device.driver_data = sd;
	sd->unit = unit;
	INIT_LIST_HEAD(&sd->orb_list);
	kref_init(&sd->kref);

	sd->address_handler.length = 0x100;
	sd->address_handler.address_callback = sbp2_status_write;
	sd->address_handler.callback_data = sd;

	if (fw_core_add_address_handler(&sd->address_handler,
					&fw_high_memory_region) < 0) {
		kfree(sd);
		return -EBUSY;
	}

	if (fw_device_enable_phys_dma(device) < 0) {
		fw_core_remove_address_handler(&sd->address_handler);
		kfree(sd);
		return -EBUSY;
	}

	/*
	 * Scan unit directory to get management agent address,
	 * firmware revison and model.  Initialize firmware_revision
	 * and model to values that wont match anything in our table.
	 */
	firmware_revision = 0xff000000;
	model = 0xff000000;
	fw_csr_iterator_init(&ci, unit->directory);
	while (fw_csr_iterator_next(&ci, &key, &value)) {
		switch (key) {
		case CSR_DEPENDENT_INFO | CSR_OFFSET:
			sd->management_agent_address =
				0xfffff0000000ULL + 4 * value;
			break;
		case SBP2_FIRMWARE_REVISION:
			firmware_revision = value;
			break;
		case CSR_MODEL:
			model = value;
			break;
		}
	}

	for (i = 0; i < ARRAY_SIZE(sbp2_workarounds_table); i++) {
		if (sbp2_workarounds_table[i].firmware_revision !=
		    (firmware_revision & 0xffffff00))
			continue;
		if (sbp2_workarounds_table[i].model != model &&
		    sbp2_workarounds_table[i].model != ~0)
			continue;
		sd->workarounds |= sbp2_workarounds_table[i].workarounds;
		break;
	}

	if (sd->workarounds)
		fw_notify("Workarounds for node %s: 0x%x "
			  "(firmware_revision 0x%06x, model_id 0x%06x)\n",
			  unit->device.bus_id,
			  sd->workarounds, firmware_revision, model);

	get_device(&unit->device);

	/*
	 * We schedule work to do the login so we can easily
	 * reschedule retries. Always get the ref before scheduling
	 * work.
	 */
	INIT_DELAYED_WORK(&sd->work, sbp2_login);
	if (schedule_delayed_work(&sd->work, 0))
		kref_get(&sd->kref);

	return 0;
}

static int sbp2_remove(struct device *dev)
{
	struct fw_unit *unit = fw_unit(dev);
	struct sbp2_device *sd = unit->device.driver_data;

	kref_put(&sd->kref, release_sbp2_device);

	return 0;
}

static void sbp2_reconnect(struct work_struct *work)
{
	struct sbp2_device *sd =
		container_of(work, struct sbp2_device, work.work);
	struct fw_unit *unit = sd->unit;
	struct fw_device *device = fw_device(unit->device.parent);
	int generation, node_id, local_node_id;

	generation    = device->card->generation;
	node_id       = device->node->node_id;
	local_node_id = device->card->local_node->node_id;

	if (sbp2_send_management_orb(unit, node_id, generation,
				     SBP2_RECONNECT_REQUEST,
				     sd->login_id, NULL) < 0) {
		if (sd->retries++ >= 5) {
			fw_error("failed to reconnect to %s\n",
				 unit->device.bus_id);
			/* Fall back and try to log in again. */
			sd->retries = 0;
			PREPARE_DELAYED_WORK(&sd->work, sbp2_login);
		}
		schedule_delayed_work(&sd->work, DIV_ROUND_UP(HZ, 5));
		return;
	}

	sd->generation   = generation;
	sd->node_id      = node_id;
	sd->address_high = local_node_id << 16;

	fw_notify("reconnected to unit %s (%d retries)\n",
		  unit->device.bus_id, sd->retries);
	sbp2_agent_reset(unit);
	sbp2_cancel_orbs(unit);
	kref_put(&sd->kref, release_sbp2_device);
}

static void sbp2_update(struct fw_unit *unit)
{
	struct fw_device *device = fw_device(unit->device.parent);
	struct sbp2_device *sd = unit->device.driver_data;

	sd->retries = 0;
	fw_device_enable_phys_dma(device);
	if (schedule_delayed_work(&sd->work, 0))
		kref_get(&sd->kref);
}

#define SBP2_UNIT_SPEC_ID_ENTRY	0x0000609e
#define SBP2_SW_VERSION_ENTRY	0x00010483

static const struct fw_device_id sbp2_id_table[] = {
	{
		.match_flags  = FW_MATCH_SPECIFIER_ID | FW_MATCH_VERSION,
		.specifier_id = SBP2_UNIT_SPEC_ID_ENTRY,
		.version      = SBP2_SW_VERSION_ENTRY,
	},
	{ }
};

static struct fw_driver sbp2_driver = {
	.driver   = {
		.owner  = THIS_MODULE,
		.name   = sbp2_driver_name,
		.bus    = &fw_bus_type,
		.probe  = sbp2_probe,
		.remove = sbp2_remove,
	},
	.update   = sbp2_update,
	.id_table = sbp2_id_table,
};

static unsigned int
sbp2_status_to_sense_data(u8 *sbp2_status, u8 *sense_data)
{
	int sam_status;

	sense_data[0] = 0x70;
	sense_data[1] = 0x0;
	sense_data[2] = sbp2_status[1];
	sense_data[3] = sbp2_status[4];
	sense_data[4] = sbp2_status[5];
	sense_data[5] = sbp2_status[6];
	sense_data[6] = sbp2_status[7];
	sense_data[7] = 10;
	sense_data[8] = sbp2_status[8];
	sense_data[9] = sbp2_status[9];
	sense_data[10] = sbp2_status[10];
	sense_data[11] = sbp2_status[11];
	sense_data[12] = sbp2_status[2];
	sense_data[13] = sbp2_status[3];
	sense_data[14] = sbp2_status[12];
	sense_data[15] = sbp2_status[13];

	sam_status = sbp2_status[0] & 0x3f;

	switch (sam_status) {
	case SAM_STAT_GOOD:
	case SAM_STAT_CHECK_CONDITION:
	case SAM_STAT_CONDITION_MET:
	case SAM_STAT_BUSY:
	case SAM_STAT_RESERVATION_CONFLICT:
	case SAM_STAT_COMMAND_TERMINATED:
		return DID_OK << 16 | sam_status;

	default:
		return DID_ERROR << 16;
	}
}

static void
complete_command_orb(struct sbp2_orb *base_orb, struct sbp2_status *status)
{
	struct sbp2_command_orb *orb = (struct sbp2_command_orb *)base_orb;
	struct fw_unit *unit = orb->unit;
	struct fw_device *device = fw_device(unit->device.parent);
	struct scatterlist *sg;
	int result;

	if (status != NULL) {
		if (status_get_dead(*status))
			sbp2_agent_reset(unit);

		switch (status_get_response(*status)) {
		case SBP2_STATUS_REQUEST_COMPLETE:
			result = DID_OK << 16;
			break;
		case SBP2_STATUS_TRANSPORT_FAILURE:
			result = DID_BUS_BUSY << 16;
			break;
		case SBP2_STATUS_ILLEGAL_REQUEST:
		case SBP2_STATUS_VENDOR_DEPENDENT:
		default:
			result = DID_ERROR << 16;
			break;
		}

		if (result == DID_OK << 16 && status_get_len(*status) > 1)
			result = sbp2_status_to_sense_data(status_get_data(*status),
							   orb->cmd->sense_buffer);
	} else {
		/*
		 * If the orb completes with status == NULL, something
		 * went wrong, typically a bus reset happened mid-orb
		 * or when sending the write (less likely).
		 */
		result = DID_BUS_BUSY << 16;
	}

	dma_unmap_single(device->card->device, orb->base.request_bus,
			 sizeof orb->request, DMA_TO_DEVICE);

	if (orb->cmd->use_sg > 0) {
		sg = (struct scatterlist *)orb->cmd->request_buffer;
		dma_unmap_sg(device->card->device, sg, orb->cmd->use_sg,
			     orb->cmd->sc_data_direction);
	}

	if (orb->page_table_bus != 0)
		dma_unmap_single(device->card->device, orb->page_table_bus,
				 sizeof orb->page_table_bus, DMA_TO_DEVICE);

	if (orb->request_buffer_bus != 0)
		dma_unmap_single(device->card->device, orb->request_buffer_bus,
				 sizeof orb->request_buffer_bus,
				 DMA_FROM_DEVICE);

	orb->cmd->result = result;
	orb->done(orb->cmd);
	kfree(orb);
}

static void sbp2_command_orb_map_scatterlist(struct sbp2_command_orb *orb)
{
	struct fw_unit *unit =
		(struct fw_unit *)orb->cmd->device->host->hostdata[0];
	struct fw_device *device = fw_device(unit->device.parent);
	struct sbp2_device *sd = unit->device.driver_data;
	struct scatterlist *sg;
	int sg_len, l, i, j, count;
	size_t size;
	dma_addr_t sg_addr;

	sg = (struct scatterlist *)orb->cmd->request_buffer;
	count = dma_map_sg(device->card->device, sg, orb->cmd->use_sg,
			   orb->cmd->sc_data_direction);

	/*
	 * Handle the special case where there is only one element in
	 * the scatter list by converting it to an immediate block
	 * request. This is also a workaround for broken devices such
	 * as the second generation iPod which doesn't support page
	 * tables.
	 */
	if (count == 1 && sg_dma_len(sg) < SBP2_MAX_SG_ELEMENT_LENGTH) {
		orb->request.data_descriptor.high = sd->address_high;
		orb->request.data_descriptor.low  = sg_dma_address(sg);
		orb->request.misc |=
			command_orb_data_size(sg_dma_len(sg));
		return;
	}

	/*
	 * Convert the scatterlist to an sbp2 page table.  If any
	 * scatterlist entries are too big for sbp2 we split the as we go.
	 */
	for (i = 0, j = 0; i < count; i++) {
		sg_len = sg_dma_len(sg + i);
		sg_addr = sg_dma_address(sg + i);
		while (sg_len) {
			l = min(sg_len, SBP2_MAX_SG_ELEMENT_LENGTH);
			orb->page_table[j].low = sg_addr;
			orb->page_table[j].high = (l << 16);
			sg_addr += l;
			sg_len -= l;
			j++;
		}
	}

	size = sizeof orb->page_table[0] * j;

	/*
	 * The data_descriptor pointer is the one case where we need
	 * to fill in the node ID part of the address.  All other
	 * pointers assume that the data referenced reside on the
	 * initiator (i.e. us), but data_descriptor can refer to data
	 * on other nodes so we need to put our ID in descriptor.high.
	 */

	orb->page_table_bus =
		dma_map_single(device->card->device, orb->page_table,
			       size, DMA_TO_DEVICE);
	orb->request.data_descriptor.high = sd->address_high;
	orb->request.data_descriptor.low  = orb->page_table_bus;
	orb->request.misc |=
		command_orb_page_table_present |
		command_orb_data_size(j);

	fw_memcpy_to_be32(orb->page_table, orb->page_table, size);
}

static void sbp2_command_orb_map_buffer(struct sbp2_command_orb *orb)
{
	struct fw_unit *unit =
		(struct fw_unit *)orb->cmd->device->host->hostdata[0];
	struct fw_device *device = fw_device(unit->device.parent);
	struct sbp2_device *sd = unit->device.driver_data;

	/*
	 * As for map_scatterlist, we need to fill in the high bits of
	 * the data_descriptor pointer.
	 */

	orb->request_buffer_bus =
		dma_map_single(device->card->device,
			       orb->cmd->request_buffer,
			       orb->cmd->request_bufflen,
			       orb->cmd->sc_data_direction);
	orb->request.data_descriptor.high = sd->address_high;
	orb->request.data_descriptor.low  = orb->request_buffer_bus;
	orb->request.misc |=
		command_orb_data_size(orb->cmd->request_bufflen);
}

/* SCSI stack integration */

static int sbp2_scsi_queuecommand(struct scsi_cmnd *cmd, scsi_done_fn_t done)
{
	struct fw_unit *unit = (struct fw_unit *)cmd->device->host->hostdata[0];
	struct fw_device *device = fw_device(unit->device.parent);
	struct sbp2_device *sd = unit->device.driver_data;
	struct sbp2_command_orb *orb;

	/*
	 * Bidirectional commands are not yet implemented, and unknown
	 * transfer direction not handled.
	 */
	if (cmd->sc_data_direction == DMA_BIDIRECTIONAL) {
		fw_error("Cannot handle DMA_BIDIRECTIONAL - rejecting command");
		goto fail_alloc;
	}

	orb = kzalloc(sizeof *orb, GFP_ATOMIC);
	if (orb == NULL) {
		fw_notify("failed to alloc orb\n");
		goto fail_alloc;
	}

	/* Initialize rcode to something not RCODE_COMPLETE. */
	orb->base.rcode = -1;
	orb->base.request_bus =
		dma_map_single(device->card->device, &orb->request,
			       sizeof orb->request, DMA_TO_DEVICE);
	if (dma_mapping_error(orb->base.request_bus))
		goto fail_mapping;

	orb->unit = unit;
	orb->done = done;
	orb->cmd  = cmd;

	orb->request.next.high   = SBP2_ORB_NULL;
	orb->request.next.low    = 0x0;
	/*
	 * At speed 100 we can do 512 bytes per packet, at speed 200,
	 * 1024 bytes per packet etc.  The SBP-2 max_payload field
	 * specifies the max payload size as 2 ^ (max_payload + 2), so
	 * if we set this to max_speed + 7, we get the right value.
	 */
	orb->request.misc =
		command_orb_max_payload(device->node->max_speed + 7) |
		command_orb_speed(device->node->max_speed) |
		command_orb_notify;

	if (cmd->sc_data_direction == DMA_FROM_DEVICE)
		orb->request.misc |=
			command_orb_direction(SBP2_DIRECTION_FROM_MEDIA);
	else if (cmd->sc_data_direction == DMA_TO_DEVICE)
		orb->request.misc |=
			command_orb_direction(SBP2_DIRECTION_TO_MEDIA);

	if (cmd->use_sg) {
		sbp2_command_orb_map_scatterlist(orb);
	} else if (cmd->request_bufflen > SBP2_MAX_SG_ELEMENT_LENGTH) {
		/*
		 * FIXME: Need to split this into a sg list... but
		 * could we get the scsi or blk layer to do that by
		 * reporting our max supported block size?
		 */
		fw_error("command > 64k\n");
		goto fail_bufflen;
	} else if (cmd->request_bufflen > 0) {
		sbp2_command_orb_map_buffer(orb);
	}

	fw_memcpy_to_be32(&orb->request, &orb->request, sizeof orb->request);

	memset(orb->request.command_block,
	       0, sizeof orb->request.command_block);
	memcpy(orb->request.command_block, cmd->cmnd, COMMAND_SIZE(*cmd->cmnd));

	orb->base.callback = complete_command_orb;

	sbp2_send_orb(&orb->base, unit, sd->node_id, sd->generation,
		      sd->command_block_agent_address + SBP2_ORB_POINTER);

	return 0;

 fail_bufflen:
	dma_unmap_single(device->card->device, orb->base.request_bus,
			 sizeof orb->request, DMA_TO_DEVICE);
 fail_mapping:
	kfree(orb);
 fail_alloc:
	cmd->result = DID_ERROR << 16;
	done(cmd);
	return 0;
}

static int sbp2_scsi_slave_alloc(struct scsi_device *sdev)
{
	struct fw_unit *unit = (struct fw_unit *)sdev->host->hostdata[0];
	struct sbp2_device *sd = unit->device.driver_data;

	sdev->allow_restart = 1;

	if (sd->workarounds & SBP2_WORKAROUND_INQUIRY_36)
		sdev->inquiry_len = 36;
	return 0;
}

static int sbp2_scsi_slave_configure(struct scsi_device *sdev)
{
	struct fw_unit *unit = (struct fw_unit *)sdev->host->hostdata[0];
	struct sbp2_device *sd = unit->device.driver_data;

	sdev->use_10_for_rw = 1;

	if (sdev->type == TYPE_ROM)
		sdev->use_10_for_ms = 1;
	if (sdev->type == TYPE_DISK &&
	    sd->workarounds & SBP2_WORKAROUND_MODE_SENSE_8)
		sdev->skip_ms_page_8 = 1;
	if (sd->workarounds & SBP2_WORKAROUND_FIX_CAPACITY) {
		fw_notify("setting fix_capacity for %s\n", unit->device.bus_id);
		sdev->fix_capacity = 1;
	}

	return 0;
}

/*
 * Called by scsi stack when something has really gone wrong.  Usually
 * called when a command has timed-out for some reason.
 */
static int sbp2_scsi_abort(struct scsi_cmnd *cmd)
{
	struct fw_unit *unit = (struct fw_unit *)cmd->device->host->hostdata[0];

	fw_notify("sbp2_scsi_abort\n");
	sbp2_agent_reset(unit);
	sbp2_cancel_orbs(unit);

	return SUCCESS;
}

static struct scsi_host_template scsi_driver_template = {
	.module			= THIS_MODULE,
	.name			= "SBP-2 IEEE-1394",
	.proc_name		= (char *)sbp2_driver_name,
	.queuecommand		= sbp2_scsi_queuecommand,
	.slave_alloc		= sbp2_scsi_slave_alloc,
	.slave_configure	= sbp2_scsi_slave_configure,
	.eh_abort_handler	= sbp2_scsi_abort,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.use_clustering		= ENABLE_CLUSTERING,
	.cmd_per_lun		= 1,
	.can_queue		= 1,
};

static int add_scsi_devices(struct fw_unit *unit)
{
	struct sbp2_device *sd = unit->device.driver_data;
	int retval, lun;

	if (sd->scsi_host != NULL)
		return 0;

	sd->scsi_host = scsi_host_alloc(&scsi_driver_template,
					sizeof(unsigned long));
	if (sd->scsi_host == NULL) {
		fw_error("failed to register scsi host\n");
		return -1;
	}

	sd->scsi_host->hostdata[0] = (unsigned long)unit;
	retval = scsi_add_host(sd->scsi_host, &unit->device);
	if (retval < 0) {
		fw_error("failed to add scsi host\n");
		scsi_host_put(sd->scsi_host);
		sd->scsi_host = NULL;
		return retval;
	}

	/* FIXME: Loop over luns here. */
	lun = 0;
	retval = scsi_add_device(sd->scsi_host, 0, 0, lun);
	if (retval < 0) {
		fw_error("failed to add scsi device\n");
		scsi_remove_host(sd->scsi_host);
		scsi_host_put(sd->scsi_host);
		sd->scsi_host = NULL;
		return retval;
	}

	return 0;
}

static void remove_scsi_devices(struct fw_unit *unit)
{
	struct sbp2_device *sd = unit->device.driver_data;

	if (sd->scsi_host != NULL) {
		scsi_remove_host(sd->scsi_host);
		scsi_host_put(sd->scsi_host);
	}
	sd->scsi_host = NULL;
}

MODULE_AUTHOR("Kristian Hoegsberg <krh@bitplanet.net>");
MODULE_DESCRIPTION("SCSI over IEEE1394");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(ieee1394, sbp2_id_table);

static int __init sbp2_init(void)
{
	return driver_register(&sbp2_driver.driver);
}

static void __exit sbp2_cleanup(void)
{
	driver_unregister(&sbp2_driver.driver);
}

module_init(sbp2_init);
module_exit(sbp2_cleanup);
