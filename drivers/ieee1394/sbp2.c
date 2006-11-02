/*
 * sbp2.c - SBP-2 protocol driver for IEEE-1394
 *
 * Copyright (C) 2000 James Goodwin, Filanet Corporation (www.filanet.com)
 * jamesg@filanet.com (JSG)
 *
 * Copyright (C) 2003 Ben Collins <bcollins@debian.org>
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
 * Brief Description:
 *
 * This driver implements the Serial Bus Protocol 2 (SBP-2) over IEEE-1394
 * under Linux. The SBP-2 driver is implemented as an IEEE-1394 high-level
 * driver. It also registers as a SCSI lower-level driver in order to accept
 * SCSI commands for transport using SBP-2.
 *
 * You may access any attached SBP-2 storage devices as if they were SCSI
 * devices (e.g. mount /dev/sda1,  fdisk, mkfs, etc.).
 *
 * Current Issues:
 *
 *	- Error Handling: SCSI aborts and bus reset requests are handled somewhat
 *	  but the code needs additional debugging.
 */

#include <linux/blkdev.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <asm/byteorder.h>
#include <asm/errno.h>
#include <asm/param.h>
#include <asm/scatterlist.h>
#include <asm/system.h>
#include <asm/types.h>

#ifdef CONFIG_IEEE1394_SBP2_PHYS_DMA
#include <asm/io.h> /* for bus_to_virt */
#endif

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "csr1212.h"
#include "highlevel.h"
#include "hosts.h"
#include "ieee1394.h"
#include "ieee1394_core.h"
#include "ieee1394_hotplug.h"
#include "ieee1394_transactions.h"
#include "ieee1394_types.h"
#include "nodemgr.h"
#include "sbp2.h"

/*
 * Module load parameter definitions
 */

/*
 * Change max_speed on module load if you have a bad IEEE-1394
 * controller that has trouble running 2KB packets at 400mb.
 *
 * NOTE: On certain OHCI parts I have seen short packets on async transmit
 * (probably due to PCI latency/throughput issues with the part). You can
 * bump down the speed if you are running into problems.
 */
static int sbp2_max_speed = IEEE1394_SPEED_MAX;
module_param_named(max_speed, sbp2_max_speed, int, 0644);
MODULE_PARM_DESC(max_speed, "Force max speed "
		 "(3 = 800Mb/s, 2 = 400Mb/s, 1 = 200Mb/s, 0 = 100Mb/s)");

/*
 * Set serialize_io to 1 if you'd like only one scsi command sent
 * down to us at a time (debugging). This might be necessary for very
 * badly behaved sbp2 devices.
 *
 * TODO: Make this configurable per device.
 */
static int sbp2_serialize_io = 1;
module_param_named(serialize_io, sbp2_serialize_io, int, 0444);
MODULE_PARM_DESC(serialize_io, "Serialize I/O coming from scsi drivers "
		 "(default = 1, faster = 0)");

/*
 * Bump up max_sectors if you'd like to support very large sized
 * transfers. Please note that some older sbp2 bridge chips are broken for
 * transfers greater or equal to 128KB.  Default is a value of 255
 * sectors, or just under 128KB (at 512 byte sector size). I can note that
 * the Oxsemi sbp2 chipsets have no problems supporting very large
 * transfer sizes.
 */
static int sbp2_max_sectors = SBP2_MAX_SECTORS;
module_param_named(max_sectors, sbp2_max_sectors, int, 0444);
MODULE_PARM_DESC(max_sectors, "Change max sectors per I/O supported "
		 "(default = " __stringify(SBP2_MAX_SECTORS) ")");

/*
 * Exclusive login to sbp2 device? In most cases, the sbp2 driver should
 * do an exclusive login, as it's generally unsafe to have two hosts
 * talking to a single sbp2 device at the same time (filesystem coherency,
 * etc.). If you're running an sbp2 device that supports multiple logins,
 * and you're either running read-only filesystems or some sort of special
 * filesystem supporting multiple hosts, e.g. OpenGFS, Oracle Cluster
 * File System, or Lustre, then set exclusive_login to zero.
 *
 * So far only bridges from Oxford Semiconductor are known to support
 * concurrent logins. Depending on firmware, four or two concurrent logins
 * are possible on OXFW911 and newer Oxsemi bridges.
 */
static int sbp2_exclusive_login = 1;
module_param_named(exclusive_login, sbp2_exclusive_login, int, 0644);
MODULE_PARM_DESC(exclusive_login, "Exclusive login to sbp2 device "
		 "(default = 1)");

/*
 * If any of the following workarounds is required for your device to work,
 * please submit the kernel messages logged by sbp2 to the linux1394-devel
 * mailing list.
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
 * - override internal blacklist
 *   Instead of adding to the built-in blacklist, use only the workarounds
 *   specified in the module load parameter.
 *   Useful if a blacklist entry interfered with a non-broken device.
 */
static int sbp2_default_workarounds;
module_param_named(workarounds, sbp2_default_workarounds, int, 0644);
MODULE_PARM_DESC(workarounds, "Work around device bugs (default = 0"
	", 128kB max transfer = " __stringify(SBP2_WORKAROUND_128K_MAX_TRANS)
	", 36 byte inquiry = "    __stringify(SBP2_WORKAROUND_INQUIRY_36)
	", skip mode page 8 = "   __stringify(SBP2_WORKAROUND_MODE_SENSE_8)
	", fix capacity = "       __stringify(SBP2_WORKAROUND_FIX_CAPACITY)
	", override internal blacklist = " __stringify(SBP2_WORKAROUND_OVERRIDE)
	", or a combination)");


#define SBP2_INFO(fmt, args...)	HPSB_INFO("sbp2: "fmt, ## args)
#define SBP2_ERR(fmt, args...)	HPSB_ERR("sbp2: "fmt, ## args)

/*
 * Globals
 */
static void sbp2scsi_complete_all_commands(struct scsi_id_instance_data *, u32);
static void sbp2scsi_complete_command(struct scsi_id_instance_data *, u32,
				      struct scsi_cmnd *,
				      void (*)(struct scsi_cmnd *));
static struct scsi_id_instance_data *sbp2_alloc_device(struct unit_directory *);
static int sbp2_start_device(struct scsi_id_instance_data *);
static void sbp2_remove_device(struct scsi_id_instance_data *);
static int sbp2_login_device(struct scsi_id_instance_data *);
static int sbp2_reconnect_device(struct scsi_id_instance_data *);
static int sbp2_logout_device(struct scsi_id_instance_data *);
static void sbp2_host_reset(struct hpsb_host *);
static int sbp2_handle_status_write(struct hpsb_host *, int, int, quadlet_t *,
				    u64, size_t, u16);
static int sbp2_agent_reset(struct scsi_id_instance_data *, int);
static void sbp2_parse_unit_directory(struct scsi_id_instance_data *,
				      struct unit_directory *);
static int sbp2_set_busy_timeout(struct scsi_id_instance_data *);
static int sbp2_max_speed_and_size(struct scsi_id_instance_data *);


static const u8 sbp2_speedto_max_payload[] = { 0x7, 0x8, 0x9, 0xA, 0xB, 0xC };

static struct hpsb_highlevel sbp2_highlevel = {
	.name		= SBP2_DEVICE_NAME,
	.host_reset	= sbp2_host_reset,
};

static struct hpsb_address_ops sbp2_ops = {
	.write		= sbp2_handle_status_write
};

#ifdef CONFIG_IEEE1394_SBP2_PHYS_DMA
static int sbp2_handle_physdma_write(struct hpsb_host *, int, int, quadlet_t *,
				     u64, size_t, u16);
static int sbp2_handle_physdma_read(struct hpsb_host *, int, quadlet_t *, u64,
				    size_t, u16);

static struct hpsb_address_ops sbp2_physdma_ops = {
	.read		= sbp2_handle_physdma_read,
	.write		= sbp2_handle_physdma_write,
};
#endif


/*
 * Interface to driver core and IEEE 1394 core
 */
static struct ieee1394_device_id sbp2_id_table[] = {
	{
	 .match_flags	= IEEE1394_MATCH_SPECIFIER_ID | IEEE1394_MATCH_VERSION,
	 .specifier_id	= SBP2_UNIT_SPEC_ID_ENTRY & 0xffffff,
	 .version	= SBP2_SW_VERSION_ENTRY & 0xffffff},
	{}
};
MODULE_DEVICE_TABLE(ieee1394, sbp2_id_table);

static int sbp2_probe(struct device *);
static int sbp2_remove(struct device *);
static int sbp2_update(struct unit_directory *);

static struct hpsb_protocol_driver sbp2_driver = {
	.name		= "SBP2 Driver",
	.id_table	= sbp2_id_table,
	.update		= sbp2_update,
	.driver		= {
		.name		= SBP2_DEVICE_NAME,
		.bus		= &ieee1394_bus_type,
		.probe		= sbp2_probe,
		.remove		= sbp2_remove,
	},
};


/*
 * Interface to SCSI core
 */
static int sbp2scsi_queuecommand(struct scsi_cmnd *,
				 void (*)(struct scsi_cmnd *));
static int sbp2scsi_abort(struct scsi_cmnd *);
static int sbp2scsi_reset(struct scsi_cmnd *);
static int sbp2scsi_slave_alloc(struct scsi_device *);
static int sbp2scsi_slave_configure(struct scsi_device *);
static void sbp2scsi_slave_destroy(struct scsi_device *);
static ssize_t sbp2_sysfs_ieee1394_id_show(struct device *,
					   struct device_attribute *, char *);

static DEVICE_ATTR(ieee1394_id, S_IRUGO, sbp2_sysfs_ieee1394_id_show, NULL);

static struct device_attribute *sbp2_sysfs_sdev_attrs[] = {
	&dev_attr_ieee1394_id,
	NULL
};

static struct scsi_host_template sbp2_shost_template = {
	.module			 = THIS_MODULE,
	.name			 = "SBP-2 IEEE-1394",
	.proc_name		 = SBP2_DEVICE_NAME,
	.queuecommand		 = sbp2scsi_queuecommand,
	.eh_abort_handler	 = sbp2scsi_abort,
	.eh_device_reset_handler = sbp2scsi_reset,
	.slave_alloc		 = sbp2scsi_slave_alloc,
	.slave_configure	 = sbp2scsi_slave_configure,
	.slave_destroy		 = sbp2scsi_slave_destroy,
	.this_id		 = -1,
	.sg_tablesize		 = SG_ALL,
	.use_clustering		 = ENABLE_CLUSTERING,
	.cmd_per_lun		 = SBP2_MAX_CMDS,
	.can_queue		 = SBP2_MAX_CMDS,
	.emulated		 = 1,
	.sdev_attrs		 = sbp2_sysfs_sdev_attrs,
};


/*
 * List of devices with known bugs.
 *
 * The firmware_revision field, masked with 0xffff00, is the best indicator
 * for the type of bridge chip of a device.  It yields a few false positives
 * but this did not break correctly behaving devices so far.
 */
static const struct {
	u32 firmware_revision;
	u32 model_id;
	unsigned workarounds;
} sbp2_workarounds_table[] = {
	/* DViCO Momobay CX-1 with TSB42AA9 bridge */ {
		.firmware_revision	= 0x002800,
		.model_id		= 0x001010,
		.workarounds		= SBP2_WORKAROUND_INQUIRY_36 |
					  SBP2_WORKAROUND_MODE_SENSE_8,
	},
	/* Initio bridges, actually only needed for some older ones */ {
		.firmware_revision	= 0x000200,
		.workarounds		= SBP2_WORKAROUND_INQUIRY_36,
	},
	/* Symbios bridge */ {
		.firmware_revision	= 0xa0b800,
		.workarounds		= SBP2_WORKAROUND_128K_MAX_TRANS,
	},
	/*
	 * Note about the following Apple iPod blacklist entries:
	 *
	 * There are iPods (2nd gen, 3rd gen) with model_id==0.  Since our
	 * matching logic treats 0 as a wildcard, we cannot match this ID
	 * without rewriting the matching routine.  Fortunately these iPods
	 * do not feature the read_capacity bug according to one report.
	 * Read_capacity behaviour as well as model_id could change due to
	 * Apple-supplied firmware updates though.
	 */
	/* iPod 4th generation */ {
		.firmware_revision	= 0x0a2700,
		.model_id		= 0x000021,
		.workarounds		= SBP2_WORKAROUND_FIX_CAPACITY,
	},
	/* iPod mini */ {
		.firmware_revision	= 0x0a2700,
		.model_id		= 0x000023,
		.workarounds		= SBP2_WORKAROUND_FIX_CAPACITY,
	},
	/* iPod Photo */ {
		.firmware_revision	= 0x0a2700,
		.model_id		= 0x00007e,
		.workarounds		= SBP2_WORKAROUND_FIX_CAPACITY,
	}
};

/**************************************
 * General utility functions
 **************************************/

#ifndef __BIG_ENDIAN
/*
 * Converts a buffer from be32 to cpu byte ordering. Length is in bytes.
 */
static inline void sbp2util_be32_to_cpu_buffer(void *buffer, int length)
{
	u32 *temp = buffer;

	for (length = (length >> 2); length--; )
		temp[length] = be32_to_cpu(temp[length]);

	return;
}

/*
 * Converts a buffer from cpu to be32 byte ordering. Length is in bytes.
 */
static inline void sbp2util_cpu_to_be32_buffer(void *buffer, int length)
{
	u32 *temp = buffer;

	for (length = (length >> 2); length--; )
		temp[length] = cpu_to_be32(temp[length]);

	return;
}
#else /* BIG_ENDIAN */
/* Why waste the cpu cycles? */
#define sbp2util_be32_to_cpu_buffer(x,y) do {} while (0)
#define sbp2util_cpu_to_be32_buffer(x,y) do {} while (0)
#endif

static DECLARE_WAIT_QUEUE_HEAD(sbp2_access_wq);

/*
 * Waits for completion of an SBP-2 access request.
 * Returns nonzero if timed out or prematurely interrupted.
 */
static int sbp2util_access_timeout(struct scsi_id_instance_data *scsi_id,
				   int timeout)
{
	long leftover;

	leftover = wait_event_interruptible_timeout(
			sbp2_access_wq, scsi_id->access_complete, timeout);
	scsi_id->access_complete = 0;
	return leftover <= 0;
}

static void sbp2_free_packet(struct hpsb_packet *packet)
{
	hpsb_free_tlabel(packet);
	hpsb_free_packet(packet);
}

/*
 * This is much like hpsb_node_write(), except it ignores the response
 * subaction and returns immediately. Can be used from atomic context.
 */
static int sbp2util_node_write_no_wait(struct node_entry *ne, u64 addr,
				       quadlet_t *buffer, size_t length)
{
	struct hpsb_packet *packet;

	packet = hpsb_make_writepacket(ne->host, ne->nodeid,
				       addr, buffer, length);
	if (!packet)
		return -ENOMEM;

	hpsb_set_packet_complete_task(packet,
				      (void (*)(void *))sbp2_free_packet,
				      packet);

	hpsb_node_fill_packet(ne, packet);

	if (hpsb_send_packet(packet) < 0) {
		sbp2_free_packet(packet);
		return -EIO;
	}

	return 0;
}

static void sbp2util_notify_fetch_agent(struct scsi_id_instance_data *scsi_id,
					u64 offset, quadlet_t *data, size_t len)
{
	/*
	 * There is a small window after a bus reset within which the node
	 * entry's generation is current but the reconnect wasn't completed.
	 */
	if (unlikely(atomic_read(&scsi_id->state) == SBP2LU_STATE_IN_RESET))
		return;

	if (hpsb_node_write(scsi_id->ne,
			    scsi_id->command_block_agent_addr + offset,
			    data, len))
		SBP2_ERR("sbp2util_notify_fetch_agent failed.");
	/*
	 * Now accept new SCSI commands, unless a bus reset happended during
	 * hpsb_node_write.
	 */
	if (likely(atomic_read(&scsi_id->state) != SBP2LU_STATE_IN_RESET))
		scsi_unblock_requests(scsi_id->scsi_host);
}

static void sbp2util_write_orb_pointer(struct work_struct *work)
{
	quadlet_t data[2];

	data[0] = ORB_SET_NODE_ID(
			(container_of(work, struct scsi_id_instance_data, protocol_work))->hi->host->node_id);
	data[1] = (container_of(work, struct scsi_id_instance_data, protocol_work))->last_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);
	sbp2util_notify_fetch_agent(container_of(work, struct scsi_id_instance_data, protocol_work), SBP2_ORB_POINTER_OFFSET, data, 8);
}

static void sbp2util_write_doorbell(struct work_struct *work)
{
	sbp2util_notify_fetch_agent(container_of(work, struct scsi_id_instance_data, protocol_work), SBP2_DOORBELL_OFFSET, NULL, 4);
}

static int sbp2util_create_command_orb_pool(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2_fwhost_info *hi = scsi_id->hi;
	int i;
	unsigned long flags, orbs;
	struct sbp2_command_info *command;

	orbs = sbp2_serialize_io ? 2 : SBP2_MAX_CMDS;

	spin_lock_irqsave(&scsi_id->cmd_orb_lock, flags);
	for (i = 0; i < orbs; i++) {
		command = kzalloc(sizeof(*command), GFP_ATOMIC);
		if (!command) {
			spin_unlock_irqrestore(&scsi_id->cmd_orb_lock,
					       flags);
			return -ENOMEM;
		}
		command->command_orb_dma =
		    pci_map_single(hi->host->pdev, &command->command_orb,
				   sizeof(struct sbp2_command_orb),
				   PCI_DMA_TODEVICE);
		command->sge_dma =
		    pci_map_single(hi->host->pdev,
				   &command->scatter_gather_element,
				   sizeof(command->scatter_gather_element),
				   PCI_DMA_BIDIRECTIONAL);
		INIT_LIST_HEAD(&command->list);
		list_add_tail(&command->list, &scsi_id->cmd_orb_completed);
	}
	spin_unlock_irqrestore(&scsi_id->cmd_orb_lock, flags);
	return 0;
}

static void sbp2util_remove_command_orb_pool(struct scsi_id_instance_data *scsi_id)
{
	struct hpsb_host *host = scsi_id->hi->host;
	struct list_head *lh, *next;
	struct sbp2_command_info *command;
	unsigned long flags;

	spin_lock_irqsave(&scsi_id->cmd_orb_lock, flags);
	if (!list_empty(&scsi_id->cmd_orb_completed)) {
		list_for_each_safe(lh, next, &scsi_id->cmd_orb_completed) {
			command = list_entry(lh, struct sbp2_command_info, list);
			pci_unmap_single(host->pdev, command->command_orb_dma,
					 sizeof(struct sbp2_command_orb),
					 PCI_DMA_TODEVICE);
			pci_unmap_single(host->pdev, command->sge_dma,
					 sizeof(command->scatter_gather_element),
					 PCI_DMA_BIDIRECTIONAL);
			kfree(command);
		}
	}
	spin_unlock_irqrestore(&scsi_id->cmd_orb_lock, flags);
	return;
}

/*
 * Finds the sbp2_command for a given outstanding command ORB.
 * Only looks at the in-use list.
 */
static struct sbp2_command_info *sbp2util_find_command_for_orb(
		struct scsi_id_instance_data *scsi_id, dma_addr_t orb)
{
	struct sbp2_command_info *command;
	unsigned long flags;

	spin_lock_irqsave(&scsi_id->cmd_orb_lock, flags);
	if (!list_empty(&scsi_id->cmd_orb_inuse)) {
		list_for_each_entry(command, &scsi_id->cmd_orb_inuse, list) {
			if (command->command_orb_dma == orb) {
				spin_unlock_irqrestore(&scsi_id->cmd_orb_lock, flags);
				return command;
			}
		}
	}
	spin_unlock_irqrestore(&scsi_id->cmd_orb_lock, flags);
	return NULL;
}

/*
 * Finds the sbp2_command for a given outstanding SCpnt.
 * Only looks at the in-use list.
 * Must be called with scsi_id->cmd_orb_lock held.
 */
static struct sbp2_command_info *sbp2util_find_command_for_SCpnt(
		struct scsi_id_instance_data *scsi_id, void *SCpnt)
{
	struct sbp2_command_info *command;

	if (!list_empty(&scsi_id->cmd_orb_inuse))
		list_for_each_entry(command, &scsi_id->cmd_orb_inuse, list)
			if (command->Current_SCpnt == SCpnt)
				return command;
	return NULL;
}

static struct sbp2_command_info *sbp2util_allocate_command_orb(
		struct scsi_id_instance_data *scsi_id,
		struct scsi_cmnd *Current_SCpnt,
		void (*Current_done)(struct scsi_cmnd *))
{
	struct list_head *lh;
	struct sbp2_command_info *command = NULL;
	unsigned long flags;

	spin_lock_irqsave(&scsi_id->cmd_orb_lock, flags);
	if (!list_empty(&scsi_id->cmd_orb_completed)) {
		lh = scsi_id->cmd_orb_completed.next;
		list_del(lh);
		command = list_entry(lh, struct sbp2_command_info, list);
		command->Current_done = Current_done;
		command->Current_SCpnt = Current_SCpnt;
		list_add_tail(&command->list, &scsi_id->cmd_orb_inuse);
	} else {
		SBP2_ERR("%s: no orbs available", __FUNCTION__);
	}
	spin_unlock_irqrestore(&scsi_id->cmd_orb_lock, flags);
	return command;
}

static void sbp2util_free_command_dma(struct sbp2_command_info *command)
{
	struct scsi_id_instance_data *scsi_id =
		(struct scsi_id_instance_data *)command->Current_SCpnt->device->host->hostdata[0];
	struct hpsb_host *host;

	if (!scsi_id) {
		SBP2_ERR("%s: scsi_id == NULL", __FUNCTION__);
		return;
	}

	host = scsi_id->ud->ne->host;

	if (command->cmd_dma) {
		if (command->dma_type == CMD_DMA_SINGLE)
			pci_unmap_single(host->pdev, command->cmd_dma,
					 command->dma_size, command->dma_dir);
		else if (command->dma_type == CMD_DMA_PAGE)
			pci_unmap_page(host->pdev, command->cmd_dma,
				       command->dma_size, command->dma_dir);
		/* XXX: Check for CMD_DMA_NONE bug */
		command->dma_type = CMD_DMA_NONE;
		command->cmd_dma = 0;
	}

	if (command->sge_buffer) {
		pci_unmap_sg(host->pdev, command->sge_buffer,
			     command->dma_size, command->dma_dir);
		command->sge_buffer = NULL;
	}
}

/*
 * This function moves a command to the completed orb list.
 * Must be called with scsi_id->cmd_orb_lock held.
 */
static void sbp2util_mark_command_completed(
		struct scsi_id_instance_data *scsi_id,
		struct sbp2_command_info *command)
{
	list_del(&command->list);
	sbp2util_free_command_dma(command);
	list_add_tail(&command->list, &scsi_id->cmd_orb_completed);
}

/*
 * Is scsi_id valid? Is the 1394 node still present?
 */
static inline int sbp2util_node_is_available(struct scsi_id_instance_data *scsi_id)
{
	return scsi_id && scsi_id->ne && !scsi_id->ne->in_limbo;
}

/*********************************************
 * IEEE-1394 core driver stack related section
 *********************************************/

static int sbp2_probe(struct device *dev)
{
	struct unit_directory *ud;
	struct scsi_id_instance_data *scsi_id;

	ud = container_of(dev, struct unit_directory, device);

	/* Don't probe UD's that have the LUN flag. We'll probe the LUN(s)
	 * instead. */
	if (ud->flags & UNIT_DIRECTORY_HAS_LUN_DIRECTORY)
		return -ENODEV;

	scsi_id = sbp2_alloc_device(ud);

	if (!scsi_id)
		return -ENOMEM;

	sbp2_parse_unit_directory(scsi_id, ud);

	return sbp2_start_device(scsi_id);
}

static int sbp2_remove(struct device *dev)
{
	struct unit_directory *ud;
	struct scsi_id_instance_data *scsi_id;
	struct scsi_device *sdev;

	ud = container_of(dev, struct unit_directory, device);
	scsi_id = ud->device.driver_data;
	if (!scsi_id)
		return 0;

	if (scsi_id->scsi_host) {
		/* Get rid of enqueued commands if there is no chance to
		 * send them. */
		if (!sbp2util_node_is_available(scsi_id))
			sbp2scsi_complete_all_commands(scsi_id, DID_NO_CONNECT);
		/* scsi_remove_device() may trigger shutdown functions of SCSI
		 * highlevel drivers which would deadlock if blocked. */
		atomic_set(&scsi_id->state, SBP2LU_STATE_IN_SHUTDOWN);
		scsi_unblock_requests(scsi_id->scsi_host);
	}
	sdev = scsi_id->sdev;
	if (sdev) {
		scsi_id->sdev = NULL;
		scsi_remove_device(sdev);
	}

	sbp2_logout_device(scsi_id);
	sbp2_remove_device(scsi_id);

	return 0;
}

static int sbp2_update(struct unit_directory *ud)
{
	struct scsi_id_instance_data *scsi_id = ud->device.driver_data;

	if (sbp2_reconnect_device(scsi_id)) {
		/* Reconnect has failed. Perhaps we didn't reconnect fast
		 * enough. Try a regular login, but first log out just in
		 * case of any weirdness. */
		sbp2_logout_device(scsi_id);

		if (sbp2_login_device(scsi_id)) {
			/* Login failed too, just fail, and the backend
			 * will call our sbp2_remove for us */
			SBP2_ERR("Failed to reconnect to sbp2 device!");
			return -EBUSY;
		}
	}

	sbp2_set_busy_timeout(scsi_id);
	sbp2_agent_reset(scsi_id, 1);
	sbp2_max_speed_and_size(scsi_id);

	/* Complete any pending commands with busy (so they get retried)
	 * and remove them from our queue. */
	sbp2scsi_complete_all_commands(scsi_id, DID_BUS_BUSY);

	/* Accept new commands unless there was another bus reset in the
	 * meantime. */
	if (hpsb_node_entry_valid(scsi_id->ne)) {
		atomic_set(&scsi_id->state, SBP2LU_STATE_RUNNING);
		scsi_unblock_requests(scsi_id->scsi_host);
	}
	return 0;
}

static struct scsi_id_instance_data *sbp2_alloc_device(struct unit_directory *ud)
{
	struct sbp2_fwhost_info *hi;
	struct Scsi_Host *scsi_host = NULL;
	struct scsi_id_instance_data *scsi_id = NULL;

	scsi_id = kzalloc(sizeof(*scsi_id), GFP_KERNEL);
	if (!scsi_id) {
		SBP2_ERR("failed to create scsi_id");
		goto failed_alloc;
	}

	scsi_id->ne = ud->ne;
	scsi_id->ud = ud;
	scsi_id->speed_code = IEEE1394_SPEED_100;
	scsi_id->max_payload_size = sbp2_speedto_max_payload[IEEE1394_SPEED_100];
	scsi_id->status_fifo_addr = CSR1212_INVALID_ADDR_SPACE;
	INIT_LIST_HEAD(&scsi_id->cmd_orb_inuse);
	INIT_LIST_HEAD(&scsi_id->cmd_orb_completed);
	INIT_LIST_HEAD(&scsi_id->scsi_list);
	spin_lock_init(&scsi_id->cmd_orb_lock);
	atomic_set(&scsi_id->state, SBP2LU_STATE_RUNNING);
	INIT_WORK(&scsi_id->protocol_work, NULL);

	ud->device.driver_data = scsi_id;

	hi = hpsb_get_hostinfo(&sbp2_highlevel, ud->ne->host);
	if (!hi) {
		hi = hpsb_create_hostinfo(&sbp2_highlevel, ud->ne->host, sizeof(*hi));
		if (!hi) {
			SBP2_ERR("failed to allocate hostinfo");
			goto failed_alloc;
		}
		hi->host = ud->ne->host;
		INIT_LIST_HEAD(&hi->scsi_ids);

#ifdef CONFIG_IEEE1394_SBP2_PHYS_DMA
		/* Handle data movement if physical dma is not
		 * enabled or not supported on host controller */
		if (!hpsb_register_addrspace(&sbp2_highlevel, ud->ne->host,
					     &sbp2_physdma_ops,
					     0x0ULL, 0xfffffffcULL)) {
			SBP2_ERR("failed to register lower 4GB address range");
			goto failed_alloc;
		}
#endif
	}

	/* Prevent unloading of the 1394 host */
	if (!try_module_get(hi->host->driver->owner)) {
		SBP2_ERR("failed to get a reference on 1394 host driver");
		goto failed_alloc;
	}

	scsi_id->hi = hi;

	list_add_tail(&scsi_id->scsi_list, &hi->scsi_ids);

	/* Register the status FIFO address range. We could use the same FIFO
	 * for targets at different nodes. However we need different FIFOs per
	 * target in order to support multi-unit devices.
	 * The FIFO is located out of the local host controller's physical range
	 * but, if possible, within the posted write area. Status writes will
	 * then be performed as unified transactions. This slightly reduces
	 * bandwidth usage, and some Prolific based devices seem to require it.
	 */
	scsi_id->status_fifo_addr = hpsb_allocate_and_register_addrspace(
			&sbp2_highlevel, ud->ne->host, &sbp2_ops,
			sizeof(struct sbp2_status_block), sizeof(quadlet_t),
			ud->ne->host->low_addr_space, CSR1212_ALL_SPACE_END);
	if (scsi_id->status_fifo_addr == CSR1212_INVALID_ADDR_SPACE) {
		SBP2_ERR("failed to allocate status FIFO address range");
		goto failed_alloc;
	}

	scsi_host = scsi_host_alloc(&sbp2_shost_template,
				    sizeof(unsigned long));
	if (!scsi_host) {
		SBP2_ERR("failed to register scsi host");
		goto failed_alloc;
	}

	scsi_host->hostdata[0] = (unsigned long)scsi_id;

	if (!scsi_add_host(scsi_host, &ud->device)) {
		scsi_id->scsi_host = scsi_host;
		return scsi_id;
	}

	SBP2_ERR("failed to add scsi host");
	scsi_host_put(scsi_host);

failed_alloc:
	sbp2_remove_device(scsi_id);
	return NULL;
}

static void sbp2_host_reset(struct hpsb_host *host)
{
	struct sbp2_fwhost_info *hi;
	struct scsi_id_instance_data *scsi_id;

	hi = hpsb_get_hostinfo(&sbp2_highlevel, host);
	if (!hi)
		return;
	list_for_each_entry(scsi_id, &hi->scsi_ids, scsi_list)
		if (likely(atomic_read(&scsi_id->state) !=
			   SBP2LU_STATE_IN_SHUTDOWN)) {
			atomic_set(&scsi_id->state, SBP2LU_STATE_IN_RESET);
			scsi_block_requests(scsi_id->scsi_host);
		}
}

static int sbp2_start_device(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2_fwhost_info *hi = scsi_id->hi;
	int error;

	scsi_id->login_response =
		pci_alloc_consistent(hi->host->pdev,
				     sizeof(struct sbp2_login_response),
				     &scsi_id->login_response_dma);
	if (!scsi_id->login_response)
		goto alloc_fail;

	scsi_id->query_logins_orb =
		pci_alloc_consistent(hi->host->pdev,
				     sizeof(struct sbp2_query_logins_orb),
				     &scsi_id->query_logins_orb_dma);
	if (!scsi_id->query_logins_orb)
		goto alloc_fail;

	scsi_id->query_logins_response =
		pci_alloc_consistent(hi->host->pdev,
				     sizeof(struct sbp2_query_logins_response),
				     &scsi_id->query_logins_response_dma);
	if (!scsi_id->query_logins_response)
		goto alloc_fail;

	scsi_id->reconnect_orb =
		pci_alloc_consistent(hi->host->pdev,
				     sizeof(struct sbp2_reconnect_orb),
				     &scsi_id->reconnect_orb_dma);
	if (!scsi_id->reconnect_orb)
		goto alloc_fail;

	scsi_id->logout_orb =
		pci_alloc_consistent(hi->host->pdev,
				     sizeof(struct sbp2_logout_orb),
				     &scsi_id->logout_orb_dma);
	if (!scsi_id->logout_orb)
		goto alloc_fail;

	scsi_id->login_orb =
		pci_alloc_consistent(hi->host->pdev,
				     sizeof(struct sbp2_login_orb),
				     &scsi_id->login_orb_dma);
	if (!scsi_id->login_orb)
		goto alloc_fail;

	if (sbp2util_create_command_orb_pool(scsi_id)) {
		SBP2_ERR("sbp2util_create_command_orb_pool failed!");
		sbp2_remove_device(scsi_id);
		return -ENOMEM;
	}

	/* Wait a second before trying to log in. Previously logged in
	 * initiators need a chance to reconnect. */
	if (msleep_interruptible(1000)) {
		sbp2_remove_device(scsi_id);
		return -EINTR;
	}

	if (sbp2_login_device(scsi_id)) {
		sbp2_remove_device(scsi_id);
		return -EBUSY;
	}

	sbp2_set_busy_timeout(scsi_id);
	sbp2_agent_reset(scsi_id, 1);
	sbp2_max_speed_and_size(scsi_id);

	error = scsi_add_device(scsi_id->scsi_host, 0, scsi_id->ud->id, 0);
	if (error) {
		SBP2_ERR("scsi_add_device failed");
		sbp2_logout_device(scsi_id);
		sbp2_remove_device(scsi_id);
		return error;
	}

	return 0;

alloc_fail:
	SBP2_ERR("Could not allocate memory for scsi_id");
	sbp2_remove_device(scsi_id);
	return -ENOMEM;
}

static void sbp2_remove_device(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2_fwhost_info *hi;

	if (!scsi_id)
		return;

	hi = scsi_id->hi;

	if (scsi_id->scsi_host) {
		scsi_remove_host(scsi_id->scsi_host);
		scsi_host_put(scsi_id->scsi_host);
	}
	flush_scheduled_work();
	sbp2util_remove_command_orb_pool(scsi_id);

	list_del(&scsi_id->scsi_list);

	if (scsi_id->login_response)
		pci_free_consistent(hi->host->pdev,
				    sizeof(struct sbp2_login_response),
				    scsi_id->login_response,
				    scsi_id->login_response_dma);
	if (scsi_id->login_orb)
		pci_free_consistent(hi->host->pdev,
				    sizeof(struct sbp2_login_orb),
				    scsi_id->login_orb,
				    scsi_id->login_orb_dma);
	if (scsi_id->reconnect_orb)
		pci_free_consistent(hi->host->pdev,
				    sizeof(struct sbp2_reconnect_orb),
				    scsi_id->reconnect_orb,
				    scsi_id->reconnect_orb_dma);
	if (scsi_id->logout_orb)
		pci_free_consistent(hi->host->pdev,
				    sizeof(struct sbp2_logout_orb),
				    scsi_id->logout_orb,
				    scsi_id->logout_orb_dma);
	if (scsi_id->query_logins_orb)
		pci_free_consistent(hi->host->pdev,
				    sizeof(struct sbp2_query_logins_orb),
				    scsi_id->query_logins_orb,
				    scsi_id->query_logins_orb_dma);
	if (scsi_id->query_logins_response)
		pci_free_consistent(hi->host->pdev,
				    sizeof(struct sbp2_query_logins_response),
				    scsi_id->query_logins_response,
				    scsi_id->query_logins_response_dma);

	if (scsi_id->status_fifo_addr != CSR1212_INVALID_ADDR_SPACE)
		hpsb_unregister_addrspace(&sbp2_highlevel, hi->host,
					  scsi_id->status_fifo_addr);

	scsi_id->ud->device.driver_data = NULL;

	if (hi)
		module_put(hi->host->driver->owner);

	kfree(scsi_id);
}

#ifdef CONFIG_IEEE1394_SBP2_PHYS_DMA
/*
 * Deal with write requests on adapters which do not support physical DMA or
 * have it switched off.
 */
static int sbp2_handle_physdma_write(struct hpsb_host *host, int nodeid,
				     int destid, quadlet_t *data, u64 addr,
				     size_t length, u16 flags)
{
	memcpy(bus_to_virt((u32) addr), data, length);
	return RCODE_COMPLETE;
}

/*
 * Deal with read requests on adapters which do not support physical DMA or
 * have it switched off.
 */
static int sbp2_handle_physdma_read(struct hpsb_host *host, int nodeid,
				    quadlet_t *data, u64 addr, size_t length,
				    u16 flags)
{
	memcpy(data, bus_to_virt((u32) addr), length);
	return RCODE_COMPLETE;
}
#endif

/**************************************
 * SBP-2 protocol related section
 **************************************/

static int sbp2_query_logins(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2_fwhost_info *hi = scsi_id->hi;
	quadlet_t data[2];
	int max_logins;
	int active_logins;

	scsi_id->query_logins_orb->reserved1 = 0x0;
	scsi_id->query_logins_orb->reserved2 = 0x0;

	scsi_id->query_logins_orb->query_response_lo = scsi_id->query_logins_response_dma;
	scsi_id->query_logins_orb->query_response_hi = ORB_SET_NODE_ID(hi->host->node_id);

	scsi_id->query_logins_orb->lun_misc = ORB_SET_FUNCTION(SBP2_QUERY_LOGINS_REQUEST);
	scsi_id->query_logins_orb->lun_misc |= ORB_SET_NOTIFY(1);
	scsi_id->query_logins_orb->lun_misc |= ORB_SET_LUN(scsi_id->lun);

	scsi_id->query_logins_orb->reserved_resp_length =
		ORB_SET_QUERY_LOGINS_RESP_LENGTH(sizeof(struct sbp2_query_logins_response));

	scsi_id->query_logins_orb->status_fifo_hi =
		ORB_SET_STATUS_FIFO_HI(scsi_id->status_fifo_addr, hi->host->node_id);
	scsi_id->query_logins_orb->status_fifo_lo =
		ORB_SET_STATUS_FIFO_LO(scsi_id->status_fifo_addr);

	sbp2util_cpu_to_be32_buffer(scsi_id->query_logins_orb, sizeof(struct sbp2_query_logins_orb));

	memset(scsi_id->query_logins_response, 0, sizeof(struct sbp2_query_logins_response));

	data[0] = ORB_SET_NODE_ID(hi->host->node_id);
	data[1] = scsi_id->query_logins_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);

	hpsb_node_write(scsi_id->ne, scsi_id->management_agent_addr, data, 8);

	if (sbp2util_access_timeout(scsi_id, 2*HZ)) {
		SBP2_INFO("Error querying logins to SBP-2 device - timed out");
		return -EIO;
	}

	if (scsi_id->status_block.ORB_offset_lo != scsi_id->query_logins_orb_dma) {
		SBP2_INFO("Error querying logins to SBP-2 device - timed out");
		return -EIO;
	}

	if (STATUS_TEST_RDS(scsi_id->status_block.ORB_offset_hi_misc)) {
		SBP2_INFO("Error querying logins to SBP-2 device - failed");
		return -EIO;
	}

	sbp2util_cpu_to_be32_buffer(scsi_id->query_logins_response, sizeof(struct sbp2_query_logins_response));

	max_logins = RESPONSE_GET_MAX_LOGINS(scsi_id->query_logins_response->length_max_logins);
	SBP2_INFO("Maximum concurrent logins supported: %d", max_logins);

	active_logins = RESPONSE_GET_ACTIVE_LOGINS(scsi_id->query_logins_response->length_max_logins);
	SBP2_INFO("Number of active logins: %d", active_logins);

	if (active_logins >= max_logins) {
		return -EIO;
	}

	return 0;
}

static int sbp2_login_device(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2_fwhost_info *hi = scsi_id->hi;
	quadlet_t data[2];

	if (!scsi_id->login_orb)
		return -EIO;

	if (!sbp2_exclusive_login && sbp2_query_logins(scsi_id)) {
		SBP2_INFO("Device does not support any more concurrent logins");
		return -EIO;
	}

	/* assume no password */
	scsi_id->login_orb->password_hi = 0;
	scsi_id->login_orb->password_lo = 0;

	scsi_id->login_orb->login_response_lo = scsi_id->login_response_dma;
	scsi_id->login_orb->login_response_hi = ORB_SET_NODE_ID(hi->host->node_id);
	scsi_id->login_orb->lun_misc = ORB_SET_FUNCTION(SBP2_LOGIN_REQUEST);

	/* one second reconnect time */
	scsi_id->login_orb->lun_misc |= ORB_SET_RECONNECT(0);
	scsi_id->login_orb->lun_misc |= ORB_SET_EXCLUSIVE(sbp2_exclusive_login);
	scsi_id->login_orb->lun_misc |= ORB_SET_NOTIFY(1);
	scsi_id->login_orb->lun_misc |= ORB_SET_LUN(scsi_id->lun);

	scsi_id->login_orb->passwd_resp_lengths =
		ORB_SET_LOGIN_RESP_LENGTH(sizeof(struct sbp2_login_response));

	scsi_id->login_orb->status_fifo_hi =
		ORB_SET_STATUS_FIFO_HI(scsi_id->status_fifo_addr, hi->host->node_id);
	scsi_id->login_orb->status_fifo_lo =
		ORB_SET_STATUS_FIFO_LO(scsi_id->status_fifo_addr);

	sbp2util_cpu_to_be32_buffer(scsi_id->login_orb, sizeof(struct sbp2_login_orb));

	memset(scsi_id->login_response, 0, sizeof(struct sbp2_login_response));

	data[0] = ORB_SET_NODE_ID(hi->host->node_id);
	data[1] = scsi_id->login_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);

	hpsb_node_write(scsi_id->ne, scsi_id->management_agent_addr, data, 8);

	/* wait up to 20 seconds for login status */
	if (sbp2util_access_timeout(scsi_id, 20*HZ)) {
		SBP2_ERR("Error logging into SBP-2 device - timed out");
		return -EIO;
	}

	/* make sure that the returned status matches the login ORB */
	if (scsi_id->status_block.ORB_offset_lo != scsi_id->login_orb_dma) {
		SBP2_ERR("Error logging into SBP-2 device - timed out");
		return -EIO;
	}

	if (STATUS_TEST_RDS(scsi_id->status_block.ORB_offset_hi_misc)) {
		SBP2_ERR("Error logging into SBP-2 device - failed");
		return -EIO;
	}

	sbp2util_cpu_to_be32_buffer(scsi_id->login_response, sizeof(struct sbp2_login_response));
	scsi_id->command_block_agent_addr =
		((u64)scsi_id->login_response->command_block_agent_hi) << 32;
	scsi_id->command_block_agent_addr |= ((u64)scsi_id->login_response->command_block_agent_lo);
	scsi_id->command_block_agent_addr &= 0x0000ffffffffffffULL;

	SBP2_INFO("Logged into SBP-2 device");
	return 0;
}

static int sbp2_logout_device(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2_fwhost_info *hi = scsi_id->hi;
	quadlet_t data[2];
	int error;

	scsi_id->logout_orb->reserved1 = 0x0;
	scsi_id->logout_orb->reserved2 = 0x0;
	scsi_id->logout_orb->reserved3 = 0x0;
	scsi_id->logout_orb->reserved4 = 0x0;

	scsi_id->logout_orb->login_ID_misc = ORB_SET_FUNCTION(SBP2_LOGOUT_REQUEST);
	scsi_id->logout_orb->login_ID_misc |= ORB_SET_LOGIN_ID(scsi_id->login_response->length_login_ID);
	scsi_id->logout_orb->login_ID_misc |= ORB_SET_NOTIFY(1);

	scsi_id->logout_orb->reserved5 = 0x0;
	scsi_id->logout_orb->status_fifo_hi =
		ORB_SET_STATUS_FIFO_HI(scsi_id->status_fifo_addr, hi->host->node_id);
	scsi_id->logout_orb->status_fifo_lo =
		ORB_SET_STATUS_FIFO_LO(scsi_id->status_fifo_addr);

	sbp2util_cpu_to_be32_buffer(scsi_id->logout_orb, sizeof(struct sbp2_logout_orb));

	data[0] = ORB_SET_NODE_ID(hi->host->node_id);
	data[1] = scsi_id->logout_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);

	error = hpsb_node_write(scsi_id->ne,
				scsi_id->management_agent_addr, data, 8);
	if (error)
		return error;

	/* wait up to 1 second for the device to complete logout */
	if (sbp2util_access_timeout(scsi_id, HZ))
		return -EIO;

	SBP2_INFO("Logged out of SBP-2 device");
	return 0;
}

static int sbp2_reconnect_device(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2_fwhost_info *hi = scsi_id->hi;
	quadlet_t data[2];
	int error;

	scsi_id->reconnect_orb->reserved1 = 0x0;
	scsi_id->reconnect_orb->reserved2 = 0x0;
	scsi_id->reconnect_orb->reserved3 = 0x0;
	scsi_id->reconnect_orb->reserved4 = 0x0;

	scsi_id->reconnect_orb->login_ID_misc = ORB_SET_FUNCTION(SBP2_RECONNECT_REQUEST);
	scsi_id->reconnect_orb->login_ID_misc |=
		ORB_SET_LOGIN_ID(scsi_id->login_response->length_login_ID);
	scsi_id->reconnect_orb->login_ID_misc |= ORB_SET_NOTIFY(1);

	scsi_id->reconnect_orb->reserved5 = 0x0;
	scsi_id->reconnect_orb->status_fifo_hi =
		ORB_SET_STATUS_FIFO_HI(scsi_id->status_fifo_addr, hi->host->node_id);
	scsi_id->reconnect_orb->status_fifo_lo =
		ORB_SET_STATUS_FIFO_LO(scsi_id->status_fifo_addr);

	sbp2util_cpu_to_be32_buffer(scsi_id->reconnect_orb, sizeof(struct sbp2_reconnect_orb));

	data[0] = ORB_SET_NODE_ID(hi->host->node_id);
	data[1] = scsi_id->reconnect_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);

	error = hpsb_node_write(scsi_id->ne,
				scsi_id->management_agent_addr, data, 8);
	if (error)
		return error;

	/* wait up to 1 second for reconnect status */
	if (sbp2util_access_timeout(scsi_id, HZ)) {
		SBP2_ERR("Error reconnecting to SBP-2 device - timed out");
		return -EIO;
	}

	/* make sure that the returned status matches the reconnect ORB */
	if (scsi_id->status_block.ORB_offset_lo != scsi_id->reconnect_orb_dma) {
		SBP2_ERR("Error reconnecting to SBP-2 device - timed out");
		return -EIO;
	}

	if (STATUS_TEST_RDS(scsi_id->status_block.ORB_offset_hi_misc)) {
		SBP2_ERR("Error reconnecting to SBP-2 device - failed");
		return -EIO;
	}

	SBP2_INFO("Reconnected to SBP-2 device");
	return 0;
}

/*
 * Set the target node's Single Phase Retry limit. Affects the target's retry
 * behaviour if our node is too busy to accept requests.
 */
static int sbp2_set_busy_timeout(struct scsi_id_instance_data *scsi_id)
{
	quadlet_t data;

	data = cpu_to_be32(SBP2_BUSY_TIMEOUT_VALUE);
	if (hpsb_node_write(scsi_id->ne, SBP2_BUSY_TIMEOUT_ADDRESS, &data, 4))
		SBP2_ERR("%s error", __FUNCTION__);
	return 0;
}

static void sbp2_parse_unit_directory(struct scsi_id_instance_data *scsi_id,
				      struct unit_directory *ud)
{
	struct csr1212_keyval *kv;
	struct csr1212_dentry *dentry;
	u64 management_agent_addr;
	u32 command_set_spec_id, command_set, unit_characteristics,
	    firmware_revision;
	unsigned workarounds;
	int i;

	management_agent_addr = 0x0;
	command_set_spec_id = 0x0;
	command_set = 0x0;
	unit_characteristics = 0x0;
	firmware_revision = 0x0;

	csr1212_for_each_dir_entry(ud->ne->csr, kv, ud->ud_kv, dentry) {
		switch (kv->key.id) {
		case CSR1212_KV_ID_DEPENDENT_INFO:
			if (kv->key.type == CSR1212_KV_TYPE_CSR_OFFSET)
				management_agent_addr =
				    CSR1212_REGISTER_SPACE_BASE +
				    (kv->value.csr_offset << 2);

			else if (kv->key.type == CSR1212_KV_TYPE_IMMEDIATE)
				scsi_id->lun =
				    ORB_SET_LUN(kv->value.immediate);
			break;

		case SBP2_COMMAND_SET_SPEC_ID_KEY:
			command_set_spec_id = kv->value.immediate;
			break;

		case SBP2_COMMAND_SET_KEY:
			command_set = kv->value.immediate;
			break;

		case SBP2_UNIT_CHARACTERISTICS_KEY:
			/* FIXME: This is ignored so far.
			 * See SBP-2 clause 7.4.8. */
			unit_characteristics = kv->value.immediate;
			break;

		case SBP2_FIRMWARE_REVISION_KEY:
			firmware_revision = kv->value.immediate;
			break;

		default:
			/* FIXME: Check for SBP2_DEVICE_TYPE_AND_LUN_KEY.
			 * Its "ordered" bit has consequences for command ORB
			 * list handling. See SBP-2 clauses 4.6, 7.4.11, 10.2 */
			break;
		}
	}

	workarounds = sbp2_default_workarounds;

	if (!(workarounds & SBP2_WORKAROUND_OVERRIDE))
		for (i = 0; i < ARRAY_SIZE(sbp2_workarounds_table); i++) {
			if (sbp2_workarounds_table[i].firmware_revision &&
			    sbp2_workarounds_table[i].firmware_revision !=
			    (firmware_revision & 0xffff00))
				continue;
			if (sbp2_workarounds_table[i].model_id &&
			    sbp2_workarounds_table[i].model_id != ud->model_id)
				continue;
			workarounds |= sbp2_workarounds_table[i].workarounds;
			break;
		}

	if (workarounds)
		SBP2_INFO("Workarounds for node " NODE_BUS_FMT ": 0x%x "
			  "(firmware_revision 0x%06x, vendor_id 0x%06x,"
			  " model_id 0x%06x)",
			  NODE_BUS_ARGS(ud->ne->host, ud->ne->nodeid),
			  workarounds, firmware_revision,
			  ud->vendor_id ? ud->vendor_id : ud->ne->vendor_id,
			  ud->model_id);

	/* We would need one SCSI host template for each target to adjust
	 * max_sectors on the fly, therefore warn only. */
	if (workarounds & SBP2_WORKAROUND_128K_MAX_TRANS &&
	    (sbp2_max_sectors * 512) > (128 * 1024))
		SBP2_INFO("Node " NODE_BUS_FMT ": Bridge only supports 128KB "
			  "max transfer size. WARNING: Current max_sectors "
			  "setting is larger than 128KB (%d sectors)",
			  NODE_BUS_ARGS(ud->ne->host, ud->ne->nodeid),
			  sbp2_max_sectors);

	/* If this is a logical unit directory entry, process the parent
	 * to get the values. */
	if (ud->flags & UNIT_DIRECTORY_LUN_DIRECTORY) {
		struct unit_directory *parent_ud =
			container_of(ud->device.parent, struct unit_directory, device);
		sbp2_parse_unit_directory(scsi_id, parent_ud);
	} else {
		scsi_id->management_agent_addr = management_agent_addr;
		scsi_id->command_set_spec_id = command_set_spec_id;
		scsi_id->command_set = command_set;
		scsi_id->unit_characteristics = unit_characteristics;
		scsi_id->firmware_revision = firmware_revision;
		scsi_id->workarounds = workarounds;
		if (ud->flags & UNIT_DIRECTORY_HAS_LUN)
			scsi_id->lun = ORB_SET_LUN(ud->lun);
	}
}

#define SBP2_PAYLOAD_TO_BYTES(p) (1 << ((p) + 2))

/*
 * This function is called in order to determine the max speed and packet
 * size we can use in our ORBs. Note, that we (the driver and host) only
 * initiate the transaction. The SBP-2 device actually transfers the data
 * (by reading from the DMA area we tell it). This means that the SBP-2
 * device decides the actual maximum data it can transfer. We just tell it
 * the speed that it needs to use, and the max_rec the host supports, and
 * it takes care of the rest.
 */
static int sbp2_max_speed_and_size(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2_fwhost_info *hi = scsi_id->hi;
	u8 payload;

	scsi_id->speed_code =
	    hi->host->speed[NODEID_TO_NODE(scsi_id->ne->nodeid)];

	if (scsi_id->speed_code > sbp2_max_speed) {
		scsi_id->speed_code = sbp2_max_speed;
		SBP2_INFO("Reducing speed to %s",
			  hpsb_speedto_str[sbp2_max_speed]);
	}

	/* Payload size is the lesser of what our speed supports and what
	 * our host supports.  */
	payload = min(sbp2_speedto_max_payload[scsi_id->speed_code],
		      (u8) (hi->host->csr.max_rec - 1));

	/* If physical DMA is off, work around limitation in ohci1394:
	 * packet size must not exceed PAGE_SIZE */
	if (scsi_id->ne->host->low_addr_space < (1ULL << 32))
		while (SBP2_PAYLOAD_TO_BYTES(payload) + 24 > PAGE_SIZE &&
		       payload)
			payload--;

	SBP2_INFO("Node " NODE_BUS_FMT ": Max speed [%s] - Max payload [%u]",
		  NODE_BUS_ARGS(hi->host, scsi_id->ne->nodeid),
		  hpsb_speedto_str[scsi_id->speed_code],
		  SBP2_PAYLOAD_TO_BYTES(payload));

	scsi_id->max_payload_size = payload;
	return 0;
}

static int sbp2_agent_reset(struct scsi_id_instance_data *scsi_id, int wait)
{
	quadlet_t data;
	u64 addr;
	int retval;
	unsigned long flags;

	/* cancel_delayed_work(&scsi_id->protocol_work); */
	if (wait)
		flush_scheduled_work();

	data = ntohl(SBP2_AGENT_RESET_DATA);
	addr = scsi_id->command_block_agent_addr + SBP2_AGENT_RESET_OFFSET;

	if (wait)
		retval = hpsb_node_write(scsi_id->ne, addr, &data, 4);
	else
		retval = sbp2util_node_write_no_wait(scsi_id->ne, addr, &data, 4);

	if (retval < 0) {
		SBP2_ERR("hpsb_node_write failed.\n");
		return -EIO;
	}

	/* make sure that the ORB_POINTER is written on next command */
	spin_lock_irqsave(&scsi_id->cmd_orb_lock, flags);
	scsi_id->last_orb = NULL;
	spin_unlock_irqrestore(&scsi_id->cmd_orb_lock, flags);

	return 0;
}

static void sbp2_prep_command_orb_sg(struct sbp2_command_orb *orb,
				     struct sbp2_fwhost_info *hi,
				     struct sbp2_command_info *command,
				     unsigned int scsi_use_sg,
				     struct scatterlist *sgpnt,
				     u32 orb_direction,
				     enum dma_data_direction dma_dir)
{
	command->dma_dir = dma_dir;
	orb->data_descriptor_hi = ORB_SET_NODE_ID(hi->host->node_id);
	orb->misc |= ORB_SET_DIRECTION(orb_direction);

	/* special case if only one element (and less than 64KB in size) */
	if ((scsi_use_sg == 1) &&
	    (sgpnt[0].length <= SBP2_MAX_SG_ELEMENT_LENGTH)) {

		command->dma_size = sgpnt[0].length;
		command->dma_type = CMD_DMA_PAGE;
		command->cmd_dma = pci_map_page(hi->host->pdev,
						sgpnt[0].page,
						sgpnt[0].offset,
						command->dma_size,
						command->dma_dir);

		orb->data_descriptor_lo = command->cmd_dma;
		orb->misc |= ORB_SET_DATA_SIZE(command->dma_size);

	} else {
		struct sbp2_unrestricted_page_table *sg_element =
					&command->scatter_gather_element[0];
		u32 sg_count, sg_len;
		dma_addr_t sg_addr;
		int i, count = pci_map_sg(hi->host->pdev, sgpnt, scsi_use_sg,
					  dma_dir);

		command->dma_size = scsi_use_sg;
		command->sge_buffer = sgpnt;

		/* use page tables (s/g) */
		orb->misc |= ORB_SET_PAGE_TABLE_PRESENT(0x1);
		orb->data_descriptor_lo = command->sge_dma;

		/* loop through and fill out our SBP-2 page tables
		 * (and split up anything too large) */
		for (i = 0, sg_count = 0 ; i < count; i++, sgpnt++) {
			sg_len = sg_dma_len(sgpnt);
			sg_addr = sg_dma_address(sgpnt);
			while (sg_len) {
				sg_element[sg_count].segment_base_lo = sg_addr;
				if (sg_len > SBP2_MAX_SG_ELEMENT_LENGTH) {
					sg_element[sg_count].length_segment_base_hi =
						PAGE_TABLE_SET_SEGMENT_LENGTH(SBP2_MAX_SG_ELEMENT_LENGTH);
					sg_addr += SBP2_MAX_SG_ELEMENT_LENGTH;
					sg_len -= SBP2_MAX_SG_ELEMENT_LENGTH;
				} else {
					sg_element[sg_count].length_segment_base_hi =
						PAGE_TABLE_SET_SEGMENT_LENGTH(sg_len);
					sg_len = 0;
				}
				sg_count++;
			}
		}

		orb->misc |= ORB_SET_DATA_SIZE(sg_count);

		sbp2util_cpu_to_be32_buffer(sg_element,
					    (sizeof(struct sbp2_unrestricted_page_table)) *
					    sg_count);
	}
}

static void sbp2_prep_command_orb_no_sg(struct sbp2_command_orb *orb,
					struct sbp2_fwhost_info *hi,
					struct sbp2_command_info *command,
					struct scatterlist *sgpnt,
					u32 orb_direction,
					unsigned int scsi_request_bufflen,
					void *scsi_request_buffer,
					enum dma_data_direction dma_dir)
{
	command->dma_dir = dma_dir;
	command->dma_size = scsi_request_bufflen;
	command->dma_type = CMD_DMA_SINGLE;
	command->cmd_dma = pci_map_single(hi->host->pdev, scsi_request_buffer,
					  command->dma_size, command->dma_dir);
	orb->data_descriptor_hi = ORB_SET_NODE_ID(hi->host->node_id);
	orb->misc |= ORB_SET_DIRECTION(orb_direction);

	/* handle case where we get a command w/o s/g enabled
	 * (but check for transfers larger than 64K) */
	if (scsi_request_bufflen <= SBP2_MAX_SG_ELEMENT_LENGTH) {

		orb->data_descriptor_lo = command->cmd_dma;
		orb->misc |= ORB_SET_DATA_SIZE(scsi_request_bufflen);

	} else {
		/* The buffer is too large. Turn this into page tables. */

		struct sbp2_unrestricted_page_table *sg_element =
			&command->scatter_gather_element[0];
		u32 sg_count, sg_len;
		dma_addr_t sg_addr;

		orb->data_descriptor_lo = command->sge_dma;
		orb->misc |= ORB_SET_PAGE_TABLE_PRESENT(0x1);

		/* fill out our SBP-2 page tables; split up the large buffer */
		sg_count = 0;
		sg_len = scsi_request_bufflen;
		sg_addr = command->cmd_dma;
		while (sg_len) {
			sg_element[sg_count].segment_base_lo = sg_addr;
			if (sg_len > SBP2_MAX_SG_ELEMENT_LENGTH) {
				sg_element[sg_count].length_segment_base_hi =
					PAGE_TABLE_SET_SEGMENT_LENGTH(SBP2_MAX_SG_ELEMENT_LENGTH);
				sg_addr += SBP2_MAX_SG_ELEMENT_LENGTH;
				sg_len -= SBP2_MAX_SG_ELEMENT_LENGTH;
			} else {
				sg_element[sg_count].length_segment_base_hi =
					PAGE_TABLE_SET_SEGMENT_LENGTH(sg_len);
				sg_len = 0;
			}
			sg_count++;
		}

		orb->misc |= ORB_SET_DATA_SIZE(sg_count);

		sbp2util_cpu_to_be32_buffer(sg_element,
					    (sizeof(struct sbp2_unrestricted_page_table)) *
					     sg_count);
	}
}

static void sbp2_create_command_orb(struct scsi_id_instance_data *scsi_id,
				    struct sbp2_command_info *command,
				    unchar *scsi_cmd,
				    unsigned int scsi_use_sg,
				    unsigned int scsi_request_bufflen,
				    void *scsi_request_buffer,
				    enum dma_data_direction dma_dir)
{
	struct sbp2_fwhost_info *hi = scsi_id->hi;
	struct scatterlist *sgpnt = (struct scatterlist *)scsi_request_buffer;
	struct sbp2_command_orb *command_orb = &command->command_orb;
	u32 orb_direction;

	/*
	 * Set-up our command ORB.
	 *
	 * NOTE: We're doing unrestricted page tables (s/g), as this is
	 * best performance (at least with the devices I have). This means
	 * that data_size becomes the number of s/g elements, and
	 * page_size should be zero (for unrestricted).
	 */
	command_orb->next_ORB_hi = ORB_SET_NULL_PTR(1);
	command_orb->next_ORB_lo = 0x0;
	command_orb->misc = ORB_SET_MAX_PAYLOAD(scsi_id->max_payload_size);
	command_orb->misc |= ORB_SET_SPEED(scsi_id->speed_code);
	command_orb->misc |= ORB_SET_NOTIFY(1);

	if (dma_dir == DMA_NONE)
		orb_direction = ORB_DIRECTION_NO_DATA_TRANSFER;
	else if (dma_dir == DMA_TO_DEVICE && scsi_request_bufflen)
		orb_direction = ORB_DIRECTION_WRITE_TO_MEDIA;
	else if (dma_dir == DMA_FROM_DEVICE && scsi_request_bufflen)
		orb_direction = ORB_DIRECTION_READ_FROM_MEDIA;
	else {
		SBP2_INFO("Falling back to DMA_NONE");
		orb_direction = ORB_DIRECTION_NO_DATA_TRANSFER;
	}

	/* set up our page table stuff */
	if (orb_direction == ORB_DIRECTION_NO_DATA_TRANSFER) {
		command_orb->data_descriptor_hi = 0x0;
		command_orb->data_descriptor_lo = 0x0;
		command_orb->misc |= ORB_SET_DIRECTION(1);
	} else if (scsi_use_sg)
		sbp2_prep_command_orb_sg(command_orb, hi, command, scsi_use_sg,
					 sgpnt, orb_direction, dma_dir);
	else
		sbp2_prep_command_orb_no_sg(command_orb, hi, command, sgpnt,
					    orb_direction, scsi_request_bufflen,
					    scsi_request_buffer, dma_dir);

	sbp2util_cpu_to_be32_buffer(command_orb, sizeof(struct sbp2_command_orb));

	memset(command_orb->cdb, 0, 12);
	memcpy(command_orb->cdb, scsi_cmd, COMMAND_SIZE(*scsi_cmd));
}

static void sbp2_link_orb_command(struct scsi_id_instance_data *scsi_id,
				 struct sbp2_command_info *command)
{
	struct sbp2_fwhost_info *hi = scsi_id->hi;
	struct sbp2_command_orb *command_orb = &command->command_orb;
	struct sbp2_command_orb *last_orb;
	dma_addr_t last_orb_dma;
	u64 addr = scsi_id->command_block_agent_addr;
	quadlet_t data[2];
	size_t length;
	unsigned long flags;

	pci_dma_sync_single_for_device(hi->host->pdev, command->command_orb_dma,
				       sizeof(struct sbp2_command_orb),
				       PCI_DMA_TODEVICE);
	pci_dma_sync_single_for_device(hi->host->pdev, command->sge_dma,
				       sizeof(command->scatter_gather_element),
				       PCI_DMA_BIDIRECTIONAL);

	/* check to see if there are any previous orbs to use */
	spin_lock_irqsave(&scsi_id->cmd_orb_lock, flags);
	last_orb = scsi_id->last_orb;
	last_orb_dma = scsi_id->last_orb_dma;
	if (!last_orb) {
		/*
		 * last_orb == NULL means: We know that the target's fetch agent
		 * is not active right now.
		 */
		addr += SBP2_ORB_POINTER_OFFSET;
		data[0] = ORB_SET_NODE_ID(hi->host->node_id);
		data[1] = command->command_orb_dma;
		sbp2util_cpu_to_be32_buffer(data, 8);
		length = 8;
	} else {
		/*
		 * last_orb != NULL means: We know that the target's fetch agent
		 * is (very probably) not dead or in reset state right now.
		 * We have an ORB already sent that we can append a new one to.
		 * The target's fetch agent may or may not have read this
		 * previous ORB yet.
		 */
		pci_dma_sync_single_for_cpu(hi->host->pdev, last_orb_dma,
					    sizeof(struct sbp2_command_orb),
					    PCI_DMA_TODEVICE);
		last_orb->next_ORB_lo = cpu_to_be32(command->command_orb_dma);
		wmb();
		/* Tells hardware that this pointer is valid */
		last_orb->next_ORB_hi = 0;
		pci_dma_sync_single_for_device(hi->host->pdev, last_orb_dma,
					       sizeof(struct sbp2_command_orb),
					       PCI_DMA_TODEVICE);
		addr += SBP2_DOORBELL_OFFSET;
		data[0] = 0;
		length = 4;
	}
	scsi_id->last_orb = command_orb;
	scsi_id->last_orb_dma = command->command_orb_dma;
	spin_unlock_irqrestore(&scsi_id->cmd_orb_lock, flags);

	if (sbp2util_node_write_no_wait(scsi_id->ne, addr, data, length)) {
		/*
		 * sbp2util_node_write_no_wait failed. We certainly ran out
		 * of transaction labels, perhaps just because there were no
		 * context switches which gave khpsbpkt a chance to collect
		 * free tlabels. Try again in non-atomic context. If necessary,
		 * the workqueue job will sleep to guaranteedly get a tlabel.
		 * We do not accept new commands until the job is over.
		 */
		scsi_block_requests(scsi_id->scsi_host);
		PREPARE_WORK(&scsi_id->protocol_work,
			     last_orb ? sbp2util_write_doorbell:
					sbp2util_write_orb_pointer
			     /* */);
		schedule_work(&scsi_id->protocol_work);
	}
}

static int sbp2_send_command(struct scsi_id_instance_data *scsi_id,
			     struct scsi_cmnd *SCpnt,
			     void (*done)(struct scsi_cmnd *))
{
	unchar *cmd = (unchar *) SCpnt->cmnd;
	unsigned int request_bufflen = SCpnt->request_bufflen;
	struct sbp2_command_info *command;

	command = sbp2util_allocate_command_orb(scsi_id, SCpnt, done);
	if (!command)
		return -EIO;

	sbp2_create_command_orb(scsi_id, command, cmd, SCpnt->use_sg,
				request_bufflen, SCpnt->request_buffer,
				SCpnt->sc_data_direction);
	sbp2_link_orb_command(scsi_id, command);

	return 0;
}

/*
 * Translates SBP-2 status into SCSI sense data for check conditions
 */
static unsigned int sbp2_status_to_sense_data(unchar *sbp2_status, unchar *sense_data)
{
	/* OK, it's pretty ugly... ;-) */
	sense_data[0] = 0x70;
	sense_data[1] = 0x0;
	sense_data[2] = sbp2_status[9];
	sense_data[3] = sbp2_status[12];
	sense_data[4] = sbp2_status[13];
	sense_data[5] = sbp2_status[14];
	sense_data[6] = sbp2_status[15];
	sense_data[7] = 10;
	sense_data[8] = sbp2_status[16];
	sense_data[9] = sbp2_status[17];
	sense_data[10] = sbp2_status[18];
	sense_data[11] = sbp2_status[19];
	sense_data[12] = sbp2_status[10];
	sense_data[13] = sbp2_status[11];
	sense_data[14] = sbp2_status[20];
	sense_data[15] = sbp2_status[21];

	return sbp2_status[8] & 0x3f;
}

static int sbp2_handle_status_write(struct hpsb_host *host, int nodeid,
				    int destid, quadlet_t *data, u64 addr,
				    size_t length, u16 fl)
{
	struct sbp2_fwhost_info *hi;
	struct scsi_id_instance_data *scsi_id = NULL, *scsi_id_tmp;
	struct scsi_cmnd *SCpnt = NULL;
	struct sbp2_status_block *sb;
	u32 scsi_status = SBP2_SCSI_STATUS_GOOD;
	struct sbp2_command_info *command;
	unsigned long flags;

	if (unlikely(length < 8 || length > sizeof(struct sbp2_status_block))) {
		SBP2_ERR("Wrong size of status block");
		return RCODE_ADDRESS_ERROR;
	}
	if (unlikely(!host)) {
		SBP2_ERR("host is NULL - this is bad!");
		return RCODE_ADDRESS_ERROR;
	}
	hi = hpsb_get_hostinfo(&sbp2_highlevel, host);
	if (unlikely(!hi)) {
		SBP2_ERR("host info is NULL - this is bad!");
		return RCODE_ADDRESS_ERROR;
	}

	/* Find the unit which wrote the status. */
	list_for_each_entry(scsi_id_tmp, &hi->scsi_ids, scsi_list) {
		if (scsi_id_tmp->ne->nodeid == nodeid &&
		    scsi_id_tmp->status_fifo_addr == addr) {
			scsi_id = scsi_id_tmp;
			break;
		}
	}
	if (unlikely(!scsi_id)) {
		SBP2_ERR("scsi_id is NULL - device is gone?");
		return RCODE_ADDRESS_ERROR;
	}

	/* Put response into scsi_id status fifo buffer. The first two bytes
	 * come in big endian bit order. Often the target writes only a
	 * truncated status block, minimally the first two quadlets. The rest
	 * is implied to be zeros. */
	sb = &scsi_id->status_block;
	memset(sb->command_set_dependent, 0, sizeof(sb->command_set_dependent));
	memcpy(sb, data, length);
	sbp2util_be32_to_cpu_buffer(sb, 8);

	/* Ignore unsolicited status. Handle command ORB status. */
	if (unlikely(STATUS_GET_SRC(sb->ORB_offset_hi_misc) == 2))
		command = NULL;
	else
		command = sbp2util_find_command_for_orb(scsi_id,
							sb->ORB_offset_lo);
	if (command) {
		pci_dma_sync_single_for_cpu(hi->host->pdev, command->command_orb_dma,
					    sizeof(struct sbp2_command_orb),
					    PCI_DMA_TODEVICE);
		pci_dma_sync_single_for_cpu(hi->host->pdev, command->sge_dma,
					    sizeof(command->scatter_gather_element),
					    PCI_DMA_BIDIRECTIONAL);
		/* Grab SCSI command pointers and check status. */
		/*
		 * FIXME: If the src field in the status is 1, the ORB DMA must
		 * not be reused until status for a subsequent ORB is received.
		 */
		SCpnt = command->Current_SCpnt;
		spin_lock_irqsave(&scsi_id->cmd_orb_lock, flags);
		sbp2util_mark_command_completed(scsi_id, command);
		spin_unlock_irqrestore(&scsi_id->cmd_orb_lock, flags);

		if (SCpnt) {
			u32 h = sb->ORB_offset_hi_misc;
			u32 r = STATUS_GET_RESP(h);

			if (r != RESP_STATUS_REQUEST_COMPLETE) {
				SBP2_INFO("resp 0x%x, sbp_status 0x%x",
					  r, STATUS_GET_SBP_STATUS(h));
				scsi_status =
					r == RESP_STATUS_TRANSPORT_FAILURE ?
					SBP2_SCSI_STATUS_BUSY :
					SBP2_SCSI_STATUS_COMMAND_TERMINATED;
			}

			if (STATUS_GET_LEN(h) > 1)
				scsi_status = sbp2_status_to_sense_data(
					(unchar *)sb, SCpnt->sense_buffer);

			if (STATUS_TEST_DEAD(h))
                                sbp2_agent_reset(scsi_id, 0);
		}

		/* Check here to see if there are no commands in-use. If there
		 * are none, we know that the fetch agent left the active state
		 * _and_ that we did not reactivate it yet. Therefore clear
		 * last_orb so that next time we write directly to the
		 * ORB_POINTER register. That way the fetch agent does not need
		 * to refetch the next_ORB. */
		spin_lock_irqsave(&scsi_id->cmd_orb_lock, flags);
		if (list_empty(&scsi_id->cmd_orb_inuse))
			scsi_id->last_orb = NULL;
		spin_unlock_irqrestore(&scsi_id->cmd_orb_lock, flags);

	} else {
		/* It's probably status after a management request. */
		if ((sb->ORB_offset_lo == scsi_id->reconnect_orb_dma) ||
		    (sb->ORB_offset_lo == scsi_id->login_orb_dma) ||
		    (sb->ORB_offset_lo == scsi_id->query_logins_orb_dma) ||
		    (sb->ORB_offset_lo == scsi_id->logout_orb_dma)) {
			scsi_id->access_complete = 1;
			wake_up_interruptible(&sbp2_access_wq);
		}
	}

	if (SCpnt)
		sbp2scsi_complete_command(scsi_id, scsi_status, SCpnt,
					  command->Current_done);
	return RCODE_COMPLETE;
}

/**************************************
 * SCSI interface related section
 **************************************/

static int sbp2scsi_queuecommand(struct scsi_cmnd *SCpnt,
				 void (*done)(struct scsi_cmnd *))
{
	struct scsi_id_instance_data *scsi_id =
		(struct scsi_id_instance_data *)SCpnt->device->host->hostdata[0];
	struct sbp2_fwhost_info *hi;
	int result = DID_NO_CONNECT << 16;

	if (unlikely(!sbp2util_node_is_available(scsi_id)))
		goto done;

	hi = scsi_id->hi;

	if (unlikely(!hi)) {
		SBP2_ERR("sbp2_fwhost_info is NULL - this is bad!");
		goto done;
	}

	/* Multiple units are currently represented to the SCSI core as separate
	 * targets, not as one target with multiple LUs. Therefore return
	 * selection time-out to any IO directed at non-zero LUNs. */
	if (unlikely(SCpnt->device->lun))
		goto done;

	/* handle the request sense command here (auto-request sense) */
	if (SCpnt->cmnd[0] == REQUEST_SENSE) {
		memcpy(SCpnt->request_buffer, SCpnt->sense_buffer, SCpnt->request_bufflen);
		memset(SCpnt->sense_buffer, 0, sizeof(SCpnt->sense_buffer));
		sbp2scsi_complete_command(scsi_id, SBP2_SCSI_STATUS_GOOD, SCpnt, done);
		return 0;
	}

	if (unlikely(!hpsb_node_entry_valid(scsi_id->ne))) {
		SBP2_ERR("Bus reset in progress - rejecting command");
		result = DID_BUS_BUSY << 16;
		goto done;
	}

	/* Bidirectional commands are not yet implemented,
	 * and unknown transfer direction not handled. */
	if (unlikely(SCpnt->sc_data_direction == DMA_BIDIRECTIONAL)) {
		SBP2_ERR("Cannot handle DMA_BIDIRECTIONAL - rejecting command");
		result = DID_ERROR << 16;
		goto done;
	}

	if (sbp2_send_command(scsi_id, SCpnt, done)) {
		SBP2_ERR("Error sending SCSI command");
		sbp2scsi_complete_command(scsi_id, SBP2_SCSI_STATUS_SELECTION_TIMEOUT,
					  SCpnt, done);
	}
	return 0;

done:
	SCpnt->result = result;
	done(SCpnt);
	return 0;
}

static void sbp2scsi_complete_all_commands(struct scsi_id_instance_data *scsi_id,
					   u32 status)
{
	struct sbp2_fwhost_info *hi = scsi_id->hi;
	struct list_head *lh;
	struct sbp2_command_info *command;
	unsigned long flags;

	spin_lock_irqsave(&scsi_id->cmd_orb_lock, flags);
	while (!list_empty(&scsi_id->cmd_orb_inuse)) {
		lh = scsi_id->cmd_orb_inuse.next;
		command = list_entry(lh, struct sbp2_command_info, list);
		pci_dma_sync_single_for_cpu(hi->host->pdev, command->command_orb_dma,
					    sizeof(struct sbp2_command_orb),
					    PCI_DMA_TODEVICE);
		pci_dma_sync_single_for_cpu(hi->host->pdev, command->sge_dma,
					    sizeof(command->scatter_gather_element),
					    PCI_DMA_BIDIRECTIONAL);
		sbp2util_mark_command_completed(scsi_id, command);
		if (command->Current_SCpnt) {
			command->Current_SCpnt->result = status << 16;
			command->Current_done(command->Current_SCpnt);
		}
	}
	spin_unlock_irqrestore(&scsi_id->cmd_orb_lock, flags);

	return;
}

/*
 * Complete a regular SCSI command. Can be called in atomic context.
 */
static void sbp2scsi_complete_command(struct scsi_id_instance_data *scsi_id,
				      u32 scsi_status, struct scsi_cmnd *SCpnt,
				      void (*done)(struct scsi_cmnd *))
{
	if (!SCpnt) {
		SBP2_ERR("SCpnt is NULL");
		return;
	}

	switch (scsi_status) {
	case SBP2_SCSI_STATUS_GOOD:
		SCpnt->result = DID_OK << 16;
		break;

	case SBP2_SCSI_STATUS_BUSY:
		SBP2_ERR("SBP2_SCSI_STATUS_BUSY");
		SCpnt->result = DID_BUS_BUSY << 16;
		break;

	case SBP2_SCSI_STATUS_CHECK_CONDITION:
		SCpnt->result = CHECK_CONDITION << 1 | DID_OK << 16;
		break;

	case SBP2_SCSI_STATUS_SELECTION_TIMEOUT:
		SBP2_ERR("SBP2_SCSI_STATUS_SELECTION_TIMEOUT");
		SCpnt->result = DID_NO_CONNECT << 16;
		scsi_print_command(SCpnt);
		break;

	case SBP2_SCSI_STATUS_CONDITION_MET:
	case SBP2_SCSI_STATUS_RESERVATION_CONFLICT:
	case SBP2_SCSI_STATUS_COMMAND_TERMINATED:
		SBP2_ERR("Bad SCSI status = %x", scsi_status);
		SCpnt->result = DID_ERROR << 16;
		scsi_print_command(SCpnt);
		break;

	default:
		SBP2_ERR("Unsupported SCSI status = %x", scsi_status);
		SCpnt->result = DID_ERROR << 16;
	}

	/* If a bus reset is in progress and there was an error, complete
	 * the command as busy so that it will get retried. */
	if (!hpsb_node_entry_valid(scsi_id->ne)
	    && (scsi_status != SBP2_SCSI_STATUS_GOOD)) {
		SBP2_ERR("Completing command with busy (bus reset)");
		SCpnt->result = DID_BUS_BUSY << 16;
	}

	/* Tell the SCSI stack that we're done with this command. */
	done(SCpnt);
}

static int sbp2scsi_slave_alloc(struct scsi_device *sdev)
{
	struct scsi_id_instance_data *scsi_id =
		(struct scsi_id_instance_data *)sdev->host->hostdata[0];

	scsi_id->sdev = sdev;
	sdev->allow_restart = 1;

	if (scsi_id->workarounds & SBP2_WORKAROUND_INQUIRY_36)
		sdev->inquiry_len = 36;
	return 0;
}

static int sbp2scsi_slave_configure(struct scsi_device *sdev)
{
	struct scsi_id_instance_data *scsi_id =
		(struct scsi_id_instance_data *)sdev->host->hostdata[0];

	blk_queue_dma_alignment(sdev->request_queue, (512 - 1));
	sdev->use_10_for_rw = 1;

	if (sdev->type == TYPE_DISK &&
	    scsi_id->workarounds & SBP2_WORKAROUND_MODE_SENSE_8)
		sdev->skip_ms_page_8 = 1;
	if (scsi_id->workarounds & SBP2_WORKAROUND_FIX_CAPACITY)
		sdev->fix_capacity = 1;
	return 0;
}

static void sbp2scsi_slave_destroy(struct scsi_device *sdev)
{
	((struct scsi_id_instance_data *)sdev->host->hostdata[0])->sdev = NULL;
	return;
}

/*
 * Called by scsi stack when something has really gone wrong.
 * Usually called when a command has timed-out for some reason.
 */
static int sbp2scsi_abort(struct scsi_cmnd *SCpnt)
{
	struct scsi_id_instance_data *scsi_id =
		(struct scsi_id_instance_data *)SCpnt->device->host->hostdata[0];
	struct sbp2_fwhost_info *hi = scsi_id->hi;
	struct sbp2_command_info *command;
	unsigned long flags;

	SBP2_INFO("aborting sbp2 command");
	scsi_print_command(SCpnt);

	if (sbp2util_node_is_available(scsi_id)) {
		sbp2_agent_reset(scsi_id, 1);

		/* Return a matching command structure to the free pool. */
		spin_lock_irqsave(&scsi_id->cmd_orb_lock, flags);
		command = sbp2util_find_command_for_SCpnt(scsi_id, SCpnt);
		if (command) {
			pci_dma_sync_single_for_cpu(hi->host->pdev,
						    command->command_orb_dma,
						    sizeof(struct sbp2_command_orb),
						    PCI_DMA_TODEVICE);
			pci_dma_sync_single_for_cpu(hi->host->pdev,
						    command->sge_dma,
						    sizeof(command->scatter_gather_element),
						    PCI_DMA_BIDIRECTIONAL);
			sbp2util_mark_command_completed(scsi_id, command);
			if (command->Current_SCpnt) {
				command->Current_SCpnt->result = DID_ABORT << 16;
				command->Current_done(command->Current_SCpnt);
			}
		}
		spin_unlock_irqrestore(&scsi_id->cmd_orb_lock, flags);

		sbp2scsi_complete_all_commands(scsi_id, DID_BUS_BUSY);
	}

	return SUCCESS;
}

/*
 * Called by scsi stack when something has really gone wrong.
 */
static int sbp2scsi_reset(struct scsi_cmnd *SCpnt)
{
	struct scsi_id_instance_data *scsi_id =
		(struct scsi_id_instance_data *)SCpnt->device->host->hostdata[0];

	SBP2_INFO("reset requested");

	if (sbp2util_node_is_available(scsi_id)) {
		SBP2_INFO("generating sbp2 fetch agent reset");
		sbp2_agent_reset(scsi_id, 1);
	}

	return SUCCESS;
}

static ssize_t sbp2_sysfs_ieee1394_id_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct scsi_device *sdev;
	struct scsi_id_instance_data *scsi_id;

	if (!(sdev = to_scsi_device(dev)))
		return 0;

	if (!(scsi_id = (struct scsi_id_instance_data *)sdev->host->hostdata[0]))
		return 0;

	return sprintf(buf, "%016Lx:%d:%d\n", (unsigned long long)scsi_id->ne->guid,
		       scsi_id->ud->id, ORB_SET_LUN(scsi_id->lun));
}

MODULE_AUTHOR("Ben Collins <bcollins@debian.org>");
MODULE_DESCRIPTION("IEEE-1394 SBP-2 protocol driver");
MODULE_SUPPORTED_DEVICE(SBP2_DEVICE_NAME);
MODULE_LICENSE("GPL");

static int sbp2_module_init(void)
{
	int ret;

	if (sbp2_serialize_io) {
		sbp2_shost_template.can_queue = 1;
		sbp2_shost_template.cmd_per_lun = 1;
	}

	if (sbp2_default_workarounds & SBP2_WORKAROUND_128K_MAX_TRANS &&
	    (sbp2_max_sectors * 512) > (128 * 1024))
		sbp2_max_sectors = 128 * 1024 / 512;
	sbp2_shost_template.max_sectors = sbp2_max_sectors;

	hpsb_register_highlevel(&sbp2_highlevel);
	ret = hpsb_register_protocol(&sbp2_driver);
	if (ret) {
		SBP2_ERR("Failed to register protocol");
		hpsb_unregister_highlevel(&sbp2_highlevel);
		return ret;
	}
	return 0;
}

static void __exit sbp2_module_exit(void)
{
	hpsb_unregister_protocol(&sbp2_driver);
	hpsb_unregister_highlevel(&sbp2_highlevel);
}

module_init(sbp2_module_init);
module_exit(sbp2_module_exit);
