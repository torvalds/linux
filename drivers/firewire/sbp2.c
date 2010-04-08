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

#include <linux/blkdev.h>
#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/stringify.h>
#include <linux/workqueue.h>

#include <asm/byteorder.h>
#include <asm/system.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

/*
 * So far only bridges from Oxford Semiconductor are known to support
 * concurrent logins. Depending on firmware, four or two concurrent logins
 * are possible on OXFW911 and newer Oxsemi bridges.
 *
 * Concurrent logins are useful together with cluster filesystems.
 */
static int sbp2_param_exclusive_login = 1;
module_param_named(exclusive_login, sbp2_param_exclusive_login, bool, 0644);
MODULE_PARM_DESC(exclusive_login, "Exclusive login to sbp2 device "
		 "(default = Y, use N for concurrent initiators)");

/*
 * Flags for firmware oddities
 *
 * - 128kB max transfer
 *   Limit transfer size. Necessary for some old bridges.
 *
 * - 36 byte inquiry
 *   When scsi_mod probes the device, let the inquiry command look like that
 *   from MS Windows.
 *
 * - skip mode page 8
 *   Suppress sending of mode_sense for mode page 8 if the device pretends to
 *   support the SCSI Primary Block commands instead of Reduced Block Commands.
 *
 * - fix capacity
 *   Tell sd_mod to correct the last sector number reported by read_capacity.
 *   Avoids access beyond actual disk limits on devices with an off-by-one bug.
 *   Don't use this with devices which don't have this bug.
 *
 * - delay inquiry
 *   Wait extra SBP2_INQUIRY_DELAY seconds after login before SCSI inquiry.
 *
 * - power condition
 *   Set the power condition field in the START STOP UNIT commands sent by
 *   sd_mod on suspend, resume, and shutdown (if manage_start_stop is on).
 *   Some disks need this to spin down or to resume properly.
 *
 * - override internal blacklist
 *   Instead of adding to the built-in blacklist, use only the workarounds
 *   specified in the module load parameter.
 *   Useful if a blacklist entry interfered with a non-broken device.
 */
#define SBP2_WORKAROUND_128K_MAX_TRANS	0x1
#define SBP2_WORKAROUND_INQUIRY_36	0x2
#define SBP2_WORKAROUND_MODE_SENSE_8	0x4
#define SBP2_WORKAROUND_FIX_CAPACITY	0x8
#define SBP2_WORKAROUND_DELAY_INQUIRY	0x10
#define SBP2_INQUIRY_DELAY		12
#define SBP2_WORKAROUND_POWER_CONDITION	0x20
#define SBP2_WORKAROUND_OVERRIDE	0x100

static int sbp2_param_workarounds;
module_param_named(workarounds, sbp2_param_workarounds, int, 0644);
MODULE_PARM_DESC(workarounds, "Work around device bugs (default = 0"
	", 128kB max transfer = " __stringify(SBP2_WORKAROUND_128K_MAX_TRANS)
	", 36 byte inquiry = "    __stringify(SBP2_WORKAROUND_INQUIRY_36)
	", skip mode page 8 = "   __stringify(SBP2_WORKAROUND_MODE_SENSE_8)
	", fix capacity = "       __stringify(SBP2_WORKAROUND_FIX_CAPACITY)
	", delay inquiry = "      __stringify(SBP2_WORKAROUND_DELAY_INQUIRY)
	", set power condition in start stop unit = "
				  __stringify(SBP2_WORKAROUND_POWER_CONDITION)
	", override internal blacklist = " __stringify(SBP2_WORKAROUND_OVERRIDE)
	", or a combination)");

/* I don't know why the SCSI stack doesn't define something like this... */
typedef void (*scsi_done_fn_t)(struct scsi_cmnd *);

static const char sbp2_driver_name[] = "sbp2";

/*
 * We create one struct sbp2_logical_unit per SBP-2 Logical Unit Number Entry
 * and one struct scsi_device per sbp2_logical_unit.
 */
struct sbp2_logical_unit {
	struct sbp2_target *tgt;
	struct list_head link;
	struct fw_address_handler address_handler;
	struct list_head orb_list;

	u64 command_block_agent_address;
	u16 lun;
	int login_id;

	/*
	 * The generation is updated once we've logged in or reconnected
	 * to the logical unit.  Thus, I/O to the device will automatically
	 * fail and get retried if it happens in a window where the device
	 * is not ready, e.g. after a bus reset but before we reconnect.
	 */
	int generation;
	int retries;
	struct delayed_work work;
	bool has_sdev;
	bool blocked;
};

/*
 * We create one struct sbp2_target per IEEE 1212 Unit Directory
 * and one struct Scsi_Host per sbp2_target.
 */
struct sbp2_target {
	struct kref kref;
	struct fw_unit *unit;
	const char *bus_id;
	struct list_head lu_list;

	u64 management_agent_address;
	u64 guid;
	int directory_id;
	int node_id;
	int address_high;
	unsigned int workarounds;
	unsigned int mgt_orb_timeout;
	unsigned int max_payload;

	int dont_block;	/* counter for each logical unit */
	int blocked;	/* ditto */
};

static struct fw_device *target_device(struct sbp2_target *tgt)
{
	return fw_parent_device(tgt->unit);
}

/* Impossible login_id, to detect logout attempt before successful login */
#define INVALID_LOGIN_ID 0x10000

#define SBP2_ORB_TIMEOUT		2000U		/* Timeout in ms */
#define SBP2_ORB_NULL			0x80000000
#define SBP2_RETRY_LIMIT		0xf		/* 15 retries */
#define SBP2_CYCLE_LIMIT		(0xc8 << 12)	/* 200 125us cycles */

/*
 * There is no transport protocol limit to the CDB length,  but we implement
 * a fixed length only.  16 bytes is enough for disks larger than 2 TB.
 */
#define SBP2_MAX_CDB_SIZE		16

/*
 * The default maximum s/g segment size of a FireWire controller is
 * usually 0x10000, but SBP-2 only allows 0xffff. Since buffers have to
 * be quadlet-aligned, we set the length limit to 0xffff & ~3.
 */
#define SBP2_MAX_SEG_SIZE		0xfffc

/* Unit directory keys */
#define SBP2_CSR_UNIT_CHARACTERISTICS	0x3a
#define SBP2_CSR_FIRMWARE_REVISION	0x3c
#define SBP2_CSR_LOGICAL_UNIT_NUMBER	0x14
#define SBP2_CSR_LOGICAL_UNIT_DIRECTORY	0xd4

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

#define STATUS_GET_ORB_HIGH(v)		((v).status & 0xffff)
#define STATUS_GET_SBP_STATUS(v)	(((v).status >> 16) & 0xff)
#define STATUS_GET_LEN(v)		(((v).status >> 24) & 0x07)
#define STATUS_GET_DEAD(v)		(((v).status >> 27) & 0x01)
#define STATUS_GET_RESPONSE(v)		(((v).status >> 28) & 0x03)
#define STATUS_GET_SOURCE(v)		(((v).status >> 30) & 0x03)
#define STATUS_GET_ORB_LOW(v)		((v).orb_low)
#define STATUS_GET_DATA(v)		((v).data)

struct sbp2_status {
	u32 status;
	u32 orb_low;
	u8 data[24];
};

struct sbp2_pointer {
	__be32 high;
	__be32 low;
};

struct sbp2_orb {
	struct fw_transaction t;
	struct kref kref;
	dma_addr_t request_bus;
	int rcode;
	struct sbp2_pointer pointer;
	void (*callback)(struct sbp2_orb * orb, struct sbp2_status * status);
	struct list_head link;
};

#define MANAGEMENT_ORB_LUN(v)			((v))
#define MANAGEMENT_ORB_FUNCTION(v)		((v) << 16)
#define MANAGEMENT_ORB_RECONNECT(v)		((v) << 20)
#define MANAGEMENT_ORB_EXCLUSIVE(v)		((v) ? 1 << 28 : 0)
#define MANAGEMENT_ORB_REQUEST_FORMAT(v)	((v) << 29)
#define MANAGEMENT_ORB_NOTIFY			((1) << 31)

#define MANAGEMENT_ORB_RESPONSE_LENGTH(v)	((v))
#define MANAGEMENT_ORB_PASSWORD_LENGTH(v)	((v) << 16)

struct sbp2_management_orb {
	struct sbp2_orb base;
	struct {
		struct sbp2_pointer password;
		struct sbp2_pointer response;
		__be32 misc;
		__be32 length;
		struct sbp2_pointer status_fifo;
	} request;
	__be32 response[4];
	dma_addr_t response_bus;
	struct completion done;
	struct sbp2_status status;
};

struct sbp2_login_response {
	__be32 misc;
	struct sbp2_pointer command_block_agent;
	__be32 reconnect_hold;
};
#define COMMAND_ORB_DATA_SIZE(v)	((v))
#define COMMAND_ORB_PAGE_SIZE(v)	((v) << 16)
#define COMMAND_ORB_PAGE_TABLE_PRESENT	((1) << 19)
#define COMMAND_ORB_MAX_PAYLOAD(v)	((v) << 20)
#define COMMAND_ORB_SPEED(v)		((v) << 24)
#define COMMAND_ORB_DIRECTION		((1) << 27)
#define COMMAND_ORB_REQUEST_FORMAT(v)	((v) << 29)
#define COMMAND_ORB_NOTIFY		((1) << 31)

struct sbp2_command_orb {
	struct sbp2_orb base;
	struct {
		struct sbp2_pointer next;
		struct sbp2_pointer data_descriptor;
		__be32 misc;
		u8 command_block[SBP2_MAX_CDB_SIZE];
	} request;
	struct scsi_cmnd *cmd;
	scsi_done_fn_t done;
	struct sbp2_logical_unit *lu;

	struct sbp2_pointer page_table[SG_ALL] __attribute__((aligned(8)));
	dma_addr_t page_table_bus;
};

#define SBP2_ROM_VALUE_WILDCARD ~0         /* match all */
#define SBP2_ROM_VALUE_MISSING  0xff000000 /* not present in the unit dir. */

/*
 * List of devices with known bugs.
 *
 * The firmware_revision field, masked with 0xffff00, is the best
 * indicator for the type of bridge chip of a device.  It yields a few
 * false positives but this did not break correctly behaving devices
 * so far.
 */
static const struct {
	u32 firmware_revision;
	u32 model;
	unsigned int workarounds;
} sbp2_workarounds_table[] = {
	/* DViCO Momobay CX-1 with TSB42AA9 bridge */ {
		.firmware_revision	= 0x002800,
		.model			= 0x001010,
		.workarounds		= SBP2_WORKAROUND_INQUIRY_36 |
					  SBP2_WORKAROUND_MODE_SENSE_8 |
					  SBP2_WORKAROUND_POWER_CONDITION,
	},
	/* DViCO Momobay FX-3A with TSB42AA9A bridge */ {
		.firmware_revision	= 0x002800,
		.model			= 0x000000,
		.workarounds		= SBP2_WORKAROUND_POWER_CONDITION,
	},
	/* Initio bridges, actually only needed for some older ones */ {
		.firmware_revision	= 0x000200,
		.model			= SBP2_ROM_VALUE_WILDCARD,
		.workarounds		= SBP2_WORKAROUND_INQUIRY_36,
	},
	/* PL-3507 bridge with Prolific firmware */ {
		.firmware_revision	= 0x012800,
		.model			= SBP2_ROM_VALUE_WILDCARD,
		.workarounds		= SBP2_WORKAROUND_POWER_CONDITION,
	},
	/* Symbios bridge */ {
		.firmware_revision	= 0xa0b800,
		.model			= SBP2_ROM_VALUE_WILDCARD,
		.workarounds		= SBP2_WORKAROUND_128K_MAX_TRANS,
	},
	/* Datafab MD2-FW2 with Symbios/LSILogic SYM13FW500 bridge */ {
		.firmware_revision	= 0x002600,
		.model			= SBP2_ROM_VALUE_WILDCARD,
		.workarounds		= SBP2_WORKAROUND_128K_MAX_TRANS,
	},
	/*
	 * iPod 2nd generation: needs 128k max transfer size workaround
	 * iPod 3rd generation: needs fix capacity workaround
	 */
	{
		.firmware_revision	= 0x0a2700,
		.model			= 0x000000,
		.workarounds		= SBP2_WORKAROUND_128K_MAX_TRANS |
					  SBP2_WORKAROUND_FIX_CAPACITY,
	},
	/* iPod 4th generation */ {
		.firmware_revision	= 0x0a2700,
		.model			= 0x000021,
		.workarounds		= SBP2_WORKAROUND_FIX_CAPACITY,
	},
	/* iPod mini */ {
		.firmware_revision	= 0x0a2700,
		.model			= 0x000022,
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

static void free_orb(struct kref *kref)
{
	struct sbp2_orb *orb = container_of(kref, struct sbp2_orb, kref);

	kfree(orb);
}

static void sbp2_status_write(struct fw_card *card, struct fw_request *request,
			      int tcode, int destination, int source,
			      int generation, int speed,
			      unsigned long long offset,
			      void *payload, size_t length, void *callback_data)
{
	struct sbp2_logical_unit *lu = callback_data;
	struct sbp2_orb *orb;
	struct sbp2_status status;
	unsigned long flags;

	if (tcode != TCODE_WRITE_BLOCK_REQUEST ||
	    length < 8 || length > sizeof(status)) {
		fw_send_response(card, request, RCODE_TYPE_ERROR);
		return;
	}

	status.status  = be32_to_cpup(payload);
	status.orb_low = be32_to_cpup(payload + 4);
	memset(status.data, 0, sizeof(status.data));
	if (length > 8)
		memcpy(status.data, payload + 8, length - 8);

	if (STATUS_GET_SOURCE(status) == 2 || STATUS_GET_SOURCE(status) == 3) {
		fw_notify("non-orb related status write, not handled\n");
		fw_send_response(card, request, RCODE_COMPLETE);
		return;
	}

	/* Lookup the orb corresponding to this status write. */
	spin_lock_irqsave(&card->lock, flags);
	list_for_each_entry(orb, &lu->orb_list, link) {
		if (STATUS_GET_ORB_HIGH(status) == 0 &&
		    STATUS_GET_ORB_LOW(status) == orb->request_bus) {
			orb->rcode = RCODE_COMPLETE;
			list_del(&orb->link);
			break;
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);

	if (&orb->link != &lu->orb_list) {
		orb->callback(orb, &status);
		kref_put(&orb->kref, free_orb);
	} else {
		fw_error("status write for unknown orb\n");
	}

	fw_send_response(card, request, RCODE_COMPLETE);
}

static void complete_transaction(struct fw_card *card, int rcode,
				 void *payload, size_t length, void *data)
{
	struct sbp2_orb *orb = data;
	unsigned long flags;

	/*
	 * This is a little tricky.  We can get the status write for
	 * the orb before we get this callback.  The status write
	 * handler above will assume the orb pointer transaction was
	 * successful and set the rcode to RCODE_COMPLETE for the orb.
	 * So this callback only sets the rcode if it hasn't already
	 * been set and only does the cleanup if the transaction
	 * failed and we didn't already get a status write.
	 */
	spin_lock_irqsave(&card->lock, flags);

	if (orb->rcode == -1)
		orb->rcode = rcode;
	if (orb->rcode != RCODE_COMPLETE) {
		list_del(&orb->link);
		spin_unlock_irqrestore(&card->lock, flags);
		orb->callback(orb, NULL);
	} else {
		spin_unlock_irqrestore(&card->lock, flags);
	}

	kref_put(&orb->kref, free_orb);
}

static void sbp2_send_orb(struct sbp2_orb *orb, struct sbp2_logical_unit *lu,
			  int node_id, int generation, u64 offset)
{
	struct fw_device *device = target_device(lu->tgt);
	unsigned long flags;

	orb->pointer.high = 0;
	orb->pointer.low = cpu_to_be32(orb->request_bus);

	spin_lock_irqsave(&device->card->lock, flags);
	list_add_tail(&orb->link, &lu->orb_list);
	spin_unlock_irqrestore(&device->card->lock, flags);

	/* Take a ref for the orb list and for the transaction callback. */
	kref_get(&orb->kref);
	kref_get(&orb->kref);

	fw_send_request(device->card, &orb->t, TCODE_WRITE_BLOCK_REQUEST,
			node_id, generation, device->max_speed, offset,
			&orb->pointer, sizeof(orb->pointer),
			complete_transaction, orb);
}

static int sbp2_cancel_orbs(struct sbp2_logical_unit *lu)
{
	struct fw_device *device = target_device(lu->tgt);
	struct sbp2_orb *orb, *next;
	struct list_head list;
	unsigned long flags;
	int retval = -ENOENT;

	INIT_LIST_HEAD(&list);
	spin_lock_irqsave(&device->card->lock, flags);
	list_splice_init(&lu->orb_list, &list);
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

static void complete_management_orb(struct sbp2_orb *base_orb,
				    struct sbp2_status *status)
{
	struct sbp2_management_orb *orb =
		container_of(base_orb, struct sbp2_management_orb, base);

	if (status)
		memcpy(&orb->status, status, sizeof(*status));
	complete(&orb->done);
}

static int sbp2_send_management_orb(struct sbp2_logical_unit *lu, int node_id,
				    int generation, int function,
				    int lun_or_login_id, void *response)
{
	struct fw_device *device = target_device(lu->tgt);
	struct sbp2_management_orb *orb;
	unsigned int timeout;
	int retval = -ENOMEM;

	if (function == SBP2_LOGOUT_REQUEST && fw_device_is_shutdown(device))
		return 0;

	orb = kzalloc(sizeof(*orb), GFP_ATOMIC);
	if (orb == NULL)
		return -ENOMEM;

	kref_init(&orb->base.kref);
	orb->response_bus =
		dma_map_single(device->card->device, &orb->response,
			       sizeof(orb->response), DMA_FROM_DEVICE);
	if (dma_mapping_error(device->card->device, orb->response_bus))
		goto fail_mapping_response;

	orb->request.response.high = 0;
	orb->request.response.low  = cpu_to_be32(orb->response_bus);

	orb->request.misc = cpu_to_be32(
		MANAGEMENT_ORB_NOTIFY |
		MANAGEMENT_ORB_FUNCTION(function) |
		MANAGEMENT_ORB_LUN(lun_or_login_id));
	orb->request.length = cpu_to_be32(
		MANAGEMENT_ORB_RESPONSE_LENGTH(sizeof(orb->response)));

	orb->request.status_fifo.high =
		cpu_to_be32(lu->address_handler.offset >> 32);
	orb->request.status_fifo.low  =
		cpu_to_be32(lu->address_handler.offset);

	if (function == SBP2_LOGIN_REQUEST) {
		/* Ask for 2^2 == 4 seconds reconnect grace period */
		orb->request.misc |= cpu_to_be32(
			MANAGEMENT_ORB_RECONNECT(2) |
			MANAGEMENT_ORB_EXCLUSIVE(sbp2_param_exclusive_login));
		timeout = lu->tgt->mgt_orb_timeout;
	} else {
		timeout = SBP2_ORB_TIMEOUT;
	}

	init_completion(&orb->done);
	orb->base.callback = complete_management_orb;

	orb->base.request_bus =
		dma_map_single(device->card->device, &orb->request,
			       sizeof(orb->request), DMA_TO_DEVICE);
	if (dma_mapping_error(device->card->device, orb->base.request_bus))
		goto fail_mapping_request;

	sbp2_send_orb(&orb->base, lu, node_id, generation,
		      lu->tgt->management_agent_address);

	wait_for_completion_timeout(&orb->done, msecs_to_jiffies(timeout));

	retval = -EIO;
	if (sbp2_cancel_orbs(lu) == 0) {
		fw_error("%s: orb reply timed out, rcode=0x%02x\n",
			 lu->tgt->bus_id, orb->base.rcode);
		goto out;
	}

	if (orb->base.rcode != RCODE_COMPLETE) {
		fw_error("%s: management write failed, rcode 0x%02x\n",
			 lu->tgt->bus_id, orb->base.rcode);
		goto out;
	}

	if (STATUS_GET_RESPONSE(orb->status) != 0 ||
	    STATUS_GET_SBP_STATUS(orb->status) != 0) {
		fw_error("%s: error status: %d:%d\n", lu->tgt->bus_id,
			 STATUS_GET_RESPONSE(orb->status),
			 STATUS_GET_SBP_STATUS(orb->status));
		goto out;
	}

	retval = 0;
 out:
	dma_unmap_single(device->card->device, orb->base.request_bus,
			 sizeof(orb->request), DMA_TO_DEVICE);
 fail_mapping_request:
	dma_unmap_single(device->card->device, orb->response_bus,
			 sizeof(orb->response), DMA_FROM_DEVICE);
 fail_mapping_response:
	if (response)
		memcpy(response, orb->response, sizeof(orb->response));
	kref_put(&orb->base.kref, free_orb);

	return retval;
}

static void sbp2_agent_reset(struct sbp2_logical_unit *lu)
{
	struct fw_device *device = target_device(lu->tgt);
	__be32 d = 0;

	fw_run_transaction(device->card, TCODE_WRITE_QUADLET_REQUEST,
			   lu->tgt->node_id, lu->generation, device->max_speed,
			   lu->command_block_agent_address + SBP2_AGENT_RESET,
			   &d, sizeof(d));
}

static void complete_agent_reset_write_no_wait(struct fw_card *card,
		int rcode, void *payload, size_t length, void *data)
{
	kfree(data);
}

static void sbp2_agent_reset_no_wait(struct sbp2_logical_unit *lu)
{
	struct fw_device *device = target_device(lu->tgt);
	struct fw_transaction *t;
	static __be32 d;

	t = kmalloc(sizeof(*t), GFP_ATOMIC);
	if (t == NULL)
		return;

	fw_send_request(device->card, t, TCODE_WRITE_QUADLET_REQUEST,
			lu->tgt->node_id, lu->generation, device->max_speed,
			lu->command_block_agent_address + SBP2_AGENT_RESET,
			&d, sizeof(d), complete_agent_reset_write_no_wait, t);
}

static inline void sbp2_allow_block(struct sbp2_logical_unit *lu)
{
	/*
	 * We may access dont_block without taking card->lock here:
	 * All callers of sbp2_allow_block() and all callers of sbp2_unblock()
	 * are currently serialized against each other.
	 * And a wrong result in sbp2_conditionally_block()'s access of
	 * dont_block is rather harmless, it simply misses its first chance.
	 */
	--lu->tgt->dont_block;
}

/*
 * Blocks lu->tgt if all of the following conditions are met:
 *   - Login, INQUIRY, and high-level SCSI setup of all of the target's
 *     logical units have been finished (indicated by dont_block == 0).
 *   - lu->generation is stale.
 *
 * Note, scsi_block_requests() must be called while holding card->lock,
 * otherwise it might foil sbp2_[conditionally_]unblock()'s attempt to
 * unblock the target.
 */
static void sbp2_conditionally_block(struct sbp2_logical_unit *lu)
{
	struct sbp2_target *tgt = lu->tgt;
	struct fw_card *card = target_device(tgt)->card;
	struct Scsi_Host *shost =
		container_of((void *)tgt, struct Scsi_Host, hostdata[0]);
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	if (!tgt->dont_block && !lu->blocked &&
	    lu->generation != card->generation) {
		lu->blocked = true;
		if (++tgt->blocked == 1)
			scsi_block_requests(shost);
	}
	spin_unlock_irqrestore(&card->lock, flags);
}

/*
 * Unblocks lu->tgt as soon as all its logical units can be unblocked.
 * Note, it is harmless to run scsi_unblock_requests() outside the
 * card->lock protected section.  On the other hand, running it inside
 * the section might clash with shost->host_lock.
 */
static void sbp2_conditionally_unblock(struct sbp2_logical_unit *lu)
{
	struct sbp2_target *tgt = lu->tgt;
	struct fw_card *card = target_device(tgt)->card;
	struct Scsi_Host *shost =
		container_of((void *)tgt, struct Scsi_Host, hostdata[0]);
	unsigned long flags;
	bool unblock = false;

	spin_lock_irqsave(&card->lock, flags);
	if (lu->blocked && lu->generation == card->generation) {
		lu->blocked = false;
		unblock = --tgt->blocked == 0;
	}
	spin_unlock_irqrestore(&card->lock, flags);

	if (unblock)
		scsi_unblock_requests(shost);
}

/*
 * Prevents future blocking of tgt and unblocks it.
 * Note, it is harmless to run scsi_unblock_requests() outside the
 * card->lock protected section.  On the other hand, running it inside
 * the section might clash with shost->host_lock.
 */
static void sbp2_unblock(struct sbp2_target *tgt)
{
	struct fw_card *card = target_device(tgt)->card;
	struct Scsi_Host *shost =
		container_of((void *)tgt, struct Scsi_Host, hostdata[0]);
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	++tgt->dont_block;
	spin_unlock_irqrestore(&card->lock, flags);

	scsi_unblock_requests(shost);
}

static int sbp2_lun2int(u16 lun)
{
	struct scsi_lun eight_bytes_lun;

	memset(&eight_bytes_lun, 0, sizeof(eight_bytes_lun));
	eight_bytes_lun.scsi_lun[0] = (lun >> 8) & 0xff;
	eight_bytes_lun.scsi_lun[1] = lun & 0xff;

	return scsilun_to_int(&eight_bytes_lun);
}

static void sbp2_release_target(struct kref *kref)
{
	struct sbp2_target *tgt = container_of(kref, struct sbp2_target, kref);
	struct sbp2_logical_unit *lu, *next;
	struct Scsi_Host *shost =
		container_of((void *)tgt, struct Scsi_Host, hostdata[0]);
	struct scsi_device *sdev;
	struct fw_device *device = target_device(tgt);

	/* prevent deadlocks */
	sbp2_unblock(tgt);

	list_for_each_entry_safe(lu, next, &tgt->lu_list, link) {
		sdev = scsi_device_lookup(shost, 0, 0, sbp2_lun2int(lu->lun));
		if (sdev) {
			scsi_remove_device(sdev);
			scsi_device_put(sdev);
		}
		if (lu->login_id != INVALID_LOGIN_ID) {
			int generation, node_id;
			/*
			 * tgt->node_id may be obsolete here if we failed
			 * during initial login or after a bus reset where
			 * the topology changed.
			 */
			generation = device->generation;
			smp_rmb(); /* node_id vs. generation */
			node_id    = device->node_id;
			sbp2_send_management_orb(lu, node_id, generation,
						 SBP2_LOGOUT_REQUEST,
						 lu->login_id, NULL);
		}
		fw_core_remove_address_handler(&lu->address_handler);
		list_del(&lu->link);
		kfree(lu);
	}
	scsi_remove_host(shost);
	fw_notify("released %s, target %d:0:0\n", tgt->bus_id, shost->host_no);

	fw_unit_put(tgt->unit);
	scsi_host_put(shost);
	fw_device_put(device);
}

static void sbp2_target_get(struct sbp2_target *tgt)
{
	kref_get(&tgt->kref);
}

static void sbp2_target_put(struct sbp2_target *tgt)
{
	kref_put(&tgt->kref, sbp2_release_target);
}

static struct workqueue_struct *sbp2_wq;

/*
 * Always get the target's kref when scheduling work on one its units.
 * Each workqueue job is responsible to call sbp2_target_put() upon return.
 */
static void sbp2_queue_work(struct sbp2_logical_unit *lu, unsigned long delay)
{
	sbp2_target_get(lu->tgt);
	if (!queue_delayed_work(sbp2_wq, &lu->work, delay))
		sbp2_target_put(lu->tgt);
}

/*
 * Write retransmit retry values into the BUSY_TIMEOUT register.
 * - The single-phase retry protocol is supported by all SBP-2 devices, but the
 *   default retry_limit value is 0 (i.e. never retry transmission). We write a
 *   saner value after logging into the device.
 * - The dual-phase retry protocol is optional to implement, and if not
 *   supported, writes to the dual-phase portion of the register will be
 *   ignored. We try to write the original 1394-1995 default here.
 * - In the case of devices that are also SBP-3-compliant, all writes are
 *   ignored, as the register is read-only, but contains single-phase retry of
 *   15, which is what we're trying to set for all SBP-2 device anyway, so this
 *   write attempt is safe and yields more consistent behavior for all devices.
 *
 * See section 8.3.2.3.5 of the 1394-1995 spec, section 6.2 of the SBP-2 spec,
 * and section 6.4 of the SBP-3 spec for further details.
 */
static void sbp2_set_busy_timeout(struct sbp2_logical_unit *lu)
{
	struct fw_device *device = target_device(lu->tgt);
	__be32 d = cpu_to_be32(SBP2_CYCLE_LIMIT | SBP2_RETRY_LIMIT);

	fw_run_transaction(device->card, TCODE_WRITE_QUADLET_REQUEST,
			   lu->tgt->node_id, lu->generation, device->max_speed,
			   CSR_REGISTER_BASE + CSR_BUSY_TIMEOUT,
			   &d, sizeof(d));
}

static void sbp2_reconnect(struct work_struct *work);

static void sbp2_login(struct work_struct *work)
{
	struct sbp2_logical_unit *lu =
		container_of(work, struct sbp2_logical_unit, work.work);
	struct sbp2_target *tgt = lu->tgt;
	struct fw_device *device = target_device(tgt);
	struct Scsi_Host *shost;
	struct scsi_device *sdev;
	struct sbp2_login_response response;
	int generation, node_id, local_node_id;

	if (fw_device_is_shutdown(device))
		goto out;

	generation    = device->generation;
	smp_rmb();    /* node IDs must not be older than generation */
	node_id       = device->node_id;
	local_node_id = device->card->node_id;

	/* If this is a re-login attempt, log out, or we might be rejected. */
	if (lu->has_sdev)
		sbp2_send_management_orb(lu, device->node_id, generation,
				SBP2_LOGOUT_REQUEST, lu->login_id, NULL);

	if (sbp2_send_management_orb(lu, node_id, generation,
				SBP2_LOGIN_REQUEST, lu->lun, &response) < 0) {
		if (lu->retries++ < 5) {
			sbp2_queue_work(lu, DIV_ROUND_UP(HZ, 5));
		} else {
			fw_error("%s: failed to login to LUN %04x\n",
				 tgt->bus_id, lu->lun);
			/* Let any waiting I/O fail from now on. */
			sbp2_unblock(lu->tgt);
		}
		goto out;
	}

	tgt->node_id	  = node_id;
	tgt->address_high = local_node_id << 16;
	smp_wmb();	  /* node IDs must not be older than generation */
	lu->generation	  = generation;

	lu->command_block_agent_address =
		((u64)(be32_to_cpu(response.command_block_agent.high) & 0xffff)
		      << 32) | be32_to_cpu(response.command_block_agent.low);
	lu->login_id = be32_to_cpu(response.misc) & 0xffff;

	fw_notify("%s: logged in to LUN %04x (%d retries)\n",
		  tgt->bus_id, lu->lun, lu->retries);

	/* set appropriate retry limit(s) in BUSY_TIMEOUT register */
	sbp2_set_busy_timeout(lu);

	PREPARE_DELAYED_WORK(&lu->work, sbp2_reconnect);
	sbp2_agent_reset(lu);

	/* This was a re-login. */
	if (lu->has_sdev) {
		sbp2_cancel_orbs(lu);
		sbp2_conditionally_unblock(lu);
		goto out;
	}

	if (lu->tgt->workarounds & SBP2_WORKAROUND_DELAY_INQUIRY)
		ssleep(SBP2_INQUIRY_DELAY);

	shost = container_of((void *)tgt, struct Scsi_Host, hostdata[0]);
	sdev = __scsi_add_device(shost, 0, 0, sbp2_lun2int(lu->lun), lu);
	/*
	 * FIXME:  We are unable to perform reconnects while in sbp2_login().
	 * Therefore __scsi_add_device() will get into trouble if a bus reset
	 * happens in parallel.  It will either fail or leave us with an
	 * unusable sdev.  As a workaround we check for this and retry the
	 * whole login and SCSI probing.
	 */

	/* Reported error during __scsi_add_device() */
	if (IS_ERR(sdev))
		goto out_logout_login;

	/* Unreported error during __scsi_add_device() */
	smp_rmb(); /* get current card generation */
	if (generation != device->card->generation) {
		scsi_remove_device(sdev);
		scsi_device_put(sdev);
		goto out_logout_login;
	}

	/* No error during __scsi_add_device() */
	lu->has_sdev = true;
	scsi_device_put(sdev);
	sbp2_allow_block(lu);
	goto out;

 out_logout_login:
	smp_rmb(); /* generation may have changed */
	generation = device->generation;
	smp_rmb(); /* node_id must not be older than generation */

	sbp2_send_management_orb(lu, device->node_id, generation,
				 SBP2_LOGOUT_REQUEST, lu->login_id, NULL);
	/*
	 * If a bus reset happened, sbp2_update will have requeued
	 * lu->work already.  Reset the work from reconnect to login.
	 */
	PREPARE_DELAYED_WORK(&lu->work, sbp2_login);
 out:
	sbp2_target_put(tgt);
}

static int sbp2_add_logical_unit(struct sbp2_target *tgt, int lun_entry)
{
	struct sbp2_logical_unit *lu;

	lu = kmalloc(sizeof(*lu), GFP_KERNEL);
	if (!lu)
		return -ENOMEM;

	lu->address_handler.length           = 0x100;
	lu->address_handler.address_callback = sbp2_status_write;
	lu->address_handler.callback_data    = lu;

	if (fw_core_add_address_handler(&lu->address_handler,
					&fw_high_memory_region) < 0) {
		kfree(lu);
		return -ENOMEM;
	}

	lu->tgt      = tgt;
	lu->lun      = lun_entry & 0xffff;
	lu->login_id = INVALID_LOGIN_ID;
	lu->retries  = 0;
	lu->has_sdev = false;
	lu->blocked  = false;
	++tgt->dont_block;
	INIT_LIST_HEAD(&lu->orb_list);
	INIT_DELAYED_WORK(&lu->work, sbp2_login);

	list_add_tail(&lu->link, &tgt->lu_list);
	return 0;
}

static int sbp2_scan_logical_unit_dir(struct sbp2_target *tgt, u32 *directory)
{
	struct fw_csr_iterator ci;
	int key, value;

	fw_csr_iterator_init(&ci, directory);
	while (fw_csr_iterator_next(&ci, &key, &value))
		if (key == SBP2_CSR_LOGICAL_UNIT_NUMBER &&
		    sbp2_add_logical_unit(tgt, value) < 0)
			return -ENOMEM;
	return 0;
}

static int sbp2_scan_unit_dir(struct sbp2_target *tgt, u32 *directory,
			      u32 *model, u32 *firmware_revision)
{
	struct fw_csr_iterator ci;
	int key, value;

	fw_csr_iterator_init(&ci, directory);
	while (fw_csr_iterator_next(&ci, &key, &value)) {
		switch (key) {

		case CSR_DEPENDENT_INFO | CSR_OFFSET:
			tgt->management_agent_address =
					CSR_REGISTER_BASE + 4 * value;
			break;

		case CSR_DIRECTORY_ID:
			tgt->directory_id = value;
			break;

		case CSR_MODEL:
			*model = value;
			break;

		case SBP2_CSR_FIRMWARE_REVISION:
			*firmware_revision = value;
			break;

		case SBP2_CSR_UNIT_CHARACTERISTICS:
			/* the timeout value is stored in 500ms units */
			tgt->mgt_orb_timeout = (value >> 8 & 0xff) * 500;
			break;

		case SBP2_CSR_LOGICAL_UNIT_NUMBER:
			if (sbp2_add_logical_unit(tgt, value) < 0)
				return -ENOMEM;
			break;

		case SBP2_CSR_LOGICAL_UNIT_DIRECTORY:
			/* Adjust for the increment in the iterator */
			if (sbp2_scan_logical_unit_dir(tgt, ci.p - 1 + value) < 0)
				return -ENOMEM;
			break;
		}
	}
	return 0;
}

/*
 * Per section 7.4.8 of the SBP-2 spec, a mgt_ORB_timeout value can be
 * provided in the config rom. Most devices do provide a value, which
 * we'll use for login management orbs, but with some sane limits.
 */
static void sbp2_clamp_management_orb_timeout(struct sbp2_target *tgt)
{
	unsigned int timeout = tgt->mgt_orb_timeout;

	if (timeout > 40000)
		fw_notify("%s: %ds mgt_ORB_timeout limited to 40s\n",
			  tgt->bus_id, timeout / 1000);

	tgt->mgt_orb_timeout = clamp_val(timeout, 5000, 40000);
}

static void sbp2_init_workarounds(struct sbp2_target *tgt, u32 model,
				  u32 firmware_revision)
{
	int i;
	unsigned int w = sbp2_param_workarounds;

	if (w)
		fw_notify("Please notify linux1394-devel@lists.sourceforge.net "
			  "if you need the workarounds parameter for %s\n",
			  tgt->bus_id);

	if (w & SBP2_WORKAROUND_OVERRIDE)
		goto out;

	for (i = 0; i < ARRAY_SIZE(sbp2_workarounds_table); i++) {

		if (sbp2_workarounds_table[i].firmware_revision !=
		    (firmware_revision & 0xffffff00))
			continue;

		if (sbp2_workarounds_table[i].model != model &&
		    sbp2_workarounds_table[i].model != SBP2_ROM_VALUE_WILDCARD)
			continue;

		w |= sbp2_workarounds_table[i].workarounds;
		break;
	}
 out:
	if (w)
		fw_notify("Workarounds for %s: 0x%x "
			  "(firmware_revision 0x%06x, model_id 0x%06x)\n",
			  tgt->bus_id, w, firmware_revision, model);
	tgt->workarounds = w;
}

static struct scsi_host_template scsi_driver_template;

static int sbp2_probe(struct device *dev)
{
	struct fw_unit *unit = fw_unit(dev);
	struct fw_device *device = fw_parent_device(unit);
	struct sbp2_target *tgt;
	struct sbp2_logical_unit *lu;
	struct Scsi_Host *shost;
	u32 model, firmware_revision;

	if (dma_get_max_seg_size(device->card->device) > SBP2_MAX_SEG_SIZE)
		BUG_ON(dma_set_max_seg_size(device->card->device,
					    SBP2_MAX_SEG_SIZE));

	shost = scsi_host_alloc(&scsi_driver_template, sizeof(*tgt));
	if (shost == NULL)
		return -ENOMEM;

	tgt = (struct sbp2_target *)shost->hostdata;
	dev_set_drvdata(&unit->device, tgt);
	tgt->unit = unit;
	kref_init(&tgt->kref);
	INIT_LIST_HEAD(&tgt->lu_list);
	tgt->bus_id = dev_name(&unit->device);
	tgt->guid = (u64)device->config_rom[3] << 32 | device->config_rom[4];

	if (fw_device_enable_phys_dma(device) < 0)
		goto fail_shost_put;

	shost->max_cmd_len = SBP2_MAX_CDB_SIZE;

	if (scsi_add_host(shost, &unit->device) < 0)
		goto fail_shost_put;

	fw_device_get(device);
	fw_unit_get(unit);

	/* implicit directory ID */
	tgt->directory_id = ((unit->directory - device->config_rom) * 4
			     + CSR_CONFIG_ROM) & 0xffffff;

	firmware_revision = SBP2_ROM_VALUE_MISSING;
	model		  = SBP2_ROM_VALUE_MISSING;

	if (sbp2_scan_unit_dir(tgt, unit->directory, &model,
			       &firmware_revision) < 0)
		goto fail_tgt_put;

	sbp2_clamp_management_orb_timeout(tgt);
	sbp2_init_workarounds(tgt, model, firmware_revision);

	/*
	 * At S100 we can do 512 bytes per packet, at S200 1024 bytes,
	 * and so on up to 4096 bytes.  The SBP-2 max_payload field
	 * specifies the max payload size as 2 ^ (max_payload + 2), so
	 * if we set this to max_speed + 7, we get the right value.
	 */
	tgt->max_payload = min(device->max_speed + 7, 10U);
	tgt->max_payload = min(tgt->max_payload, device->card->max_receive - 1);

	/* Do the login in a workqueue so we can easily reschedule retries. */
	list_for_each_entry(lu, &tgt->lu_list, link)
		sbp2_queue_work(lu, DIV_ROUND_UP(HZ, 5));
	return 0;

 fail_tgt_put:
	sbp2_target_put(tgt);
	return -ENOMEM;

 fail_shost_put:
	scsi_host_put(shost);
	return -ENOMEM;
}

static int sbp2_remove(struct device *dev)
{
	struct fw_unit *unit = fw_unit(dev);
	struct sbp2_target *tgt = dev_get_drvdata(&unit->device);

	sbp2_target_put(tgt);
	return 0;
}

static void sbp2_reconnect(struct work_struct *work)
{
	struct sbp2_logical_unit *lu =
		container_of(work, struct sbp2_logical_unit, work.work);
	struct sbp2_target *tgt = lu->tgt;
	struct fw_device *device = target_device(tgt);
	int generation, node_id, local_node_id;

	if (fw_device_is_shutdown(device))
		goto out;

	generation    = device->generation;
	smp_rmb();    /* node IDs must not be older than generation */
	node_id       = device->node_id;
	local_node_id = device->card->node_id;

	if (sbp2_send_management_orb(lu, node_id, generation,
				     SBP2_RECONNECT_REQUEST,
				     lu->login_id, NULL) < 0) {
		/*
		 * If reconnect was impossible even though we are in the
		 * current generation, fall back and try to log in again.
		 *
		 * We could check for "Function rejected" status, but
		 * looking at the bus generation as simpler and more general.
		 */
		smp_rmb(); /* get current card generation */
		if (generation == device->card->generation ||
		    lu->retries++ >= 5) {
			fw_error("%s: failed to reconnect\n", tgt->bus_id);
			lu->retries = 0;
			PREPARE_DELAYED_WORK(&lu->work, sbp2_login);
		}
		sbp2_queue_work(lu, DIV_ROUND_UP(HZ, 5));
		goto out;
	}

	tgt->node_id      = node_id;
	tgt->address_high = local_node_id << 16;
	smp_wmb();	  /* node IDs must not be older than generation */
	lu->generation	  = generation;

	fw_notify("%s: reconnected to LUN %04x (%d retries)\n",
		  tgt->bus_id, lu->lun, lu->retries);

	sbp2_agent_reset(lu);
	sbp2_cancel_orbs(lu);
	sbp2_conditionally_unblock(lu);
 out:
	sbp2_target_put(tgt);
}

static void sbp2_update(struct fw_unit *unit)
{
	struct sbp2_target *tgt = dev_get_drvdata(&unit->device);
	struct sbp2_logical_unit *lu;

	fw_device_enable_phys_dma(fw_parent_device(unit));

	/*
	 * Fw-core serializes sbp2_update() against sbp2_remove().
	 * Iteration over tgt->lu_list is therefore safe here.
	 */
	list_for_each_entry(lu, &tgt->lu_list, link) {
		sbp2_conditionally_block(lu);
		lu->retries = 0;
		sbp2_queue_work(lu, 0);
	}
}

#define SBP2_UNIT_SPEC_ID_ENTRY	0x0000609e
#define SBP2_SW_VERSION_ENTRY	0x00010483

static const struct ieee1394_device_id sbp2_id_table[] = {
	{
		.match_flags  = IEEE1394_MATCH_SPECIFIER_ID |
				IEEE1394_MATCH_VERSION,
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

static void sbp2_unmap_scatterlist(struct device *card_device,
				   struct sbp2_command_orb *orb)
{
	if (scsi_sg_count(orb->cmd))
		dma_unmap_sg(card_device, scsi_sglist(orb->cmd),
			     scsi_sg_count(orb->cmd),
			     orb->cmd->sc_data_direction);

	if (orb->request.misc & cpu_to_be32(COMMAND_ORB_PAGE_TABLE_PRESENT))
		dma_unmap_single(card_device, orb->page_table_bus,
				 sizeof(orb->page_table), DMA_TO_DEVICE);
}

static unsigned int sbp2_status_to_sense_data(u8 *sbp2_status, u8 *sense_data)
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

static void complete_command_orb(struct sbp2_orb *base_orb,
				 struct sbp2_status *status)
{
	struct sbp2_command_orb *orb =
		container_of(base_orb, struct sbp2_command_orb, base);
	struct fw_device *device = target_device(orb->lu->tgt);
	int result;

	if (status != NULL) {
		if (STATUS_GET_DEAD(*status))
			sbp2_agent_reset_no_wait(orb->lu);

		switch (STATUS_GET_RESPONSE(*status)) {
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

		if (result == DID_OK << 16 && STATUS_GET_LEN(*status) > 1)
			result = sbp2_status_to_sense_data(STATUS_GET_DATA(*status),
							   orb->cmd->sense_buffer);
	} else {
		/*
		 * If the orb completes with status == NULL, something
		 * went wrong, typically a bus reset happened mid-orb
		 * or when sending the write (less likely).
		 */
		result = DID_BUS_BUSY << 16;
		sbp2_conditionally_block(orb->lu);
	}

	dma_unmap_single(device->card->device, orb->base.request_bus,
			 sizeof(orb->request), DMA_TO_DEVICE);
	sbp2_unmap_scatterlist(device->card->device, orb);

	orb->cmd->result = result;
	orb->done(orb->cmd);
}

static int sbp2_map_scatterlist(struct sbp2_command_orb *orb,
		struct fw_device *device, struct sbp2_logical_unit *lu)
{
	struct scatterlist *sg = scsi_sglist(orb->cmd);
	int i, n;

	n = dma_map_sg(device->card->device, sg, scsi_sg_count(orb->cmd),
		       orb->cmd->sc_data_direction);
	if (n == 0)
		goto fail;

	/*
	 * Handle the special case where there is only one element in
	 * the scatter list by converting it to an immediate block
	 * request. This is also a workaround for broken devices such
	 * as the second generation iPod which doesn't support page
	 * tables.
	 */
	if (n == 1) {
		orb->request.data_descriptor.high =
			cpu_to_be32(lu->tgt->address_high);
		orb->request.data_descriptor.low  =
			cpu_to_be32(sg_dma_address(sg));
		orb->request.misc |=
			cpu_to_be32(COMMAND_ORB_DATA_SIZE(sg_dma_len(sg)));
		return 0;
	}

	for_each_sg(sg, sg, n, i) {
		orb->page_table[i].high = cpu_to_be32(sg_dma_len(sg) << 16);
		orb->page_table[i].low = cpu_to_be32(sg_dma_address(sg));
	}

	orb->page_table_bus =
		dma_map_single(device->card->device, orb->page_table,
			       sizeof(orb->page_table), DMA_TO_DEVICE);
	if (dma_mapping_error(device->card->device, orb->page_table_bus))
		goto fail_page_table;

	/*
	 * The data_descriptor pointer is the one case where we need
	 * to fill in the node ID part of the address.  All other
	 * pointers assume that the data referenced reside on the
	 * initiator (i.e. us), but data_descriptor can refer to data
	 * on other nodes so we need to put our ID in descriptor.high.
	 */
	orb->request.data_descriptor.high = cpu_to_be32(lu->tgt->address_high);
	orb->request.data_descriptor.low  = cpu_to_be32(orb->page_table_bus);
	orb->request.misc |= cpu_to_be32(COMMAND_ORB_PAGE_TABLE_PRESENT |
					 COMMAND_ORB_DATA_SIZE(n));

	return 0;

 fail_page_table:
	dma_unmap_sg(device->card->device, scsi_sglist(orb->cmd),
		     scsi_sg_count(orb->cmd), orb->cmd->sc_data_direction);
 fail:
	return -ENOMEM;
}

/* SCSI stack integration */

static int sbp2_scsi_queuecommand(struct scsi_cmnd *cmd, scsi_done_fn_t done)
{
	struct sbp2_logical_unit *lu = cmd->device->hostdata;
	struct fw_device *device = target_device(lu->tgt);
	struct sbp2_command_orb *orb;
	int generation, retval = SCSI_MLQUEUE_HOST_BUSY;

	/*
	 * Bidirectional commands are not yet implemented, and unknown
	 * transfer direction not handled.
	 */
	if (cmd->sc_data_direction == DMA_BIDIRECTIONAL) {
		fw_error("Can't handle DMA_BIDIRECTIONAL, rejecting command\n");
		cmd->result = DID_ERROR << 16;
		done(cmd);
		return 0;
	}

	orb = kzalloc(sizeof(*orb), GFP_ATOMIC);
	if (orb == NULL) {
		fw_notify("failed to alloc orb\n");
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	/* Initialize rcode to something not RCODE_COMPLETE. */
	orb->base.rcode = -1;
	kref_init(&orb->base.kref);

	orb->lu   = lu;
	orb->done = done;
	orb->cmd  = cmd;

	orb->request.next.high = cpu_to_be32(SBP2_ORB_NULL);
	orb->request.misc = cpu_to_be32(
		COMMAND_ORB_MAX_PAYLOAD(lu->tgt->max_payload) |
		COMMAND_ORB_SPEED(device->max_speed) |
		COMMAND_ORB_NOTIFY);

	if (cmd->sc_data_direction == DMA_FROM_DEVICE)
		orb->request.misc |= cpu_to_be32(COMMAND_ORB_DIRECTION);

	generation = device->generation;
	smp_rmb();    /* sbp2_map_scatterlist looks at tgt->address_high */

	if (scsi_sg_count(cmd) && sbp2_map_scatterlist(orb, device, lu) < 0)
		goto out;

	memcpy(orb->request.command_block, cmd->cmnd, cmd->cmd_len);

	orb->base.callback = complete_command_orb;
	orb->base.request_bus =
		dma_map_single(device->card->device, &orb->request,
			       sizeof(orb->request), DMA_TO_DEVICE);
	if (dma_mapping_error(device->card->device, orb->base.request_bus)) {
		sbp2_unmap_scatterlist(device->card->device, orb);
		goto out;
	}

	sbp2_send_orb(&orb->base, lu, lu->tgt->node_id, generation,
		      lu->command_block_agent_address + SBP2_ORB_POINTER);
	retval = 0;
 out:
	kref_put(&orb->base.kref, free_orb);
	return retval;
}

static int sbp2_scsi_slave_alloc(struct scsi_device *sdev)
{
	struct sbp2_logical_unit *lu = sdev->hostdata;

	/* (Re-)Adding logical units via the SCSI stack is not supported. */
	if (!lu)
		return -ENOSYS;

	sdev->allow_restart = 1;

	/* SBP-2 requires quadlet alignment of the data buffers. */
	blk_queue_update_dma_alignment(sdev->request_queue, 4 - 1);

	if (lu->tgt->workarounds & SBP2_WORKAROUND_INQUIRY_36)
		sdev->inquiry_len = 36;

	return 0;
}

static int sbp2_scsi_slave_configure(struct scsi_device *sdev)
{
	struct sbp2_logical_unit *lu = sdev->hostdata;

	sdev->use_10_for_rw = 1;

	if (sbp2_param_exclusive_login)
		sdev->manage_start_stop = 1;

	if (sdev->type == TYPE_ROM)
		sdev->use_10_for_ms = 1;

	if (sdev->type == TYPE_DISK &&
	    lu->tgt->workarounds & SBP2_WORKAROUND_MODE_SENSE_8)
		sdev->skip_ms_page_8 = 1;

	if (lu->tgt->workarounds & SBP2_WORKAROUND_FIX_CAPACITY)
		sdev->fix_capacity = 1;

	if (lu->tgt->workarounds & SBP2_WORKAROUND_POWER_CONDITION)
		sdev->start_stop_pwr_cond = 1;

	if (lu->tgt->workarounds & SBP2_WORKAROUND_128K_MAX_TRANS)
		blk_queue_max_hw_sectors(sdev->request_queue, 128 * 1024 / 512);

	blk_queue_max_segment_size(sdev->request_queue, SBP2_MAX_SEG_SIZE);

	return 0;
}

/*
 * Called by scsi stack when something has really gone wrong.  Usually
 * called when a command has timed-out for some reason.
 */
static int sbp2_scsi_abort(struct scsi_cmnd *cmd)
{
	struct sbp2_logical_unit *lu = cmd->device->hostdata;

	fw_notify("%s: sbp2_scsi_abort\n", lu->tgt->bus_id);
	sbp2_agent_reset(lu);
	sbp2_cancel_orbs(lu);

	return SUCCESS;
}

/*
 * Format of /sys/bus/scsi/devices/.../ieee1394_id:
 * u64 EUI-64 : u24 directory_ID : u16 LUN  (all printed in hexadecimal)
 *
 * This is the concatenation of target port identifier and logical unit
 * identifier as per SAM-2...SAM-4 annex A.
 */
static ssize_t sbp2_sysfs_ieee1394_id_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct sbp2_logical_unit *lu;

	if (!sdev)
		return 0;

	lu = sdev->hostdata;

	return sprintf(buf, "%016llx:%06x:%04x\n",
			(unsigned long long)lu->tgt->guid,
			lu->tgt->directory_id, lu->lun);
}

static DEVICE_ATTR(ieee1394_id, S_IRUGO, sbp2_sysfs_ieee1394_id_show, NULL);

static struct device_attribute *sbp2_scsi_sysfs_attrs[] = {
	&dev_attr_ieee1394_id,
	NULL
};

static struct scsi_host_template scsi_driver_template = {
	.module			= THIS_MODULE,
	.name			= "SBP-2 IEEE-1394",
	.proc_name		= sbp2_driver_name,
	.queuecommand		= sbp2_scsi_queuecommand,
	.slave_alloc		= sbp2_scsi_slave_alloc,
	.slave_configure	= sbp2_scsi_slave_configure,
	.eh_abort_handler	= sbp2_scsi_abort,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.use_clustering		= ENABLE_CLUSTERING,
	.cmd_per_lun		= 1,
	.can_queue		= 1,
	.sdev_attrs		= sbp2_scsi_sysfs_attrs,
};

MODULE_AUTHOR("Kristian Hoegsberg <krh@bitplanet.net>");
MODULE_DESCRIPTION("SCSI over IEEE1394");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(ieee1394, sbp2_id_table);

/* Provide a module alias so root-on-sbp2 initrds don't break. */
#ifndef CONFIG_IEEE1394_SBP2_MODULE
MODULE_ALIAS("sbp2");
#endif

static int __init sbp2_init(void)
{
	sbp2_wq = create_singlethread_workqueue(KBUILD_MODNAME);
	if (!sbp2_wq)
		return -ENOMEM;

	return driver_register(&sbp2_driver.driver);
}

static void __exit sbp2_cleanup(void)
{
	driver_unregister(&sbp2_driver.driver);
	destroy_workqueue(sbp2_wq);
}

module_init(sbp2_init);
module_exit(sbp2_cleanup);
