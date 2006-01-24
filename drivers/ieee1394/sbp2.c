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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/blkdev.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/current.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/scatterlist.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "csr1212.h"
#include "ieee1394.h"
#include "ieee1394_types.h"
#include "ieee1394_core.h"
#include "nodemgr.h"
#include "hosts.h"
#include "highlevel.h"
#include "ieee1394_transactions.h"
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
static int max_speed = IEEE1394_SPEED_MAX;
module_param(max_speed, int, 0644);
MODULE_PARM_DESC(max_speed, "Force max speed (3 = 800mb, 2 = 400mb, 1 = 200mb, 0 = 100mb)");

/*
 * Set serialize_io to 1 if you'd like only one scsi command sent
 * down to us at a time (debugging). This might be necessary for very
 * badly behaved sbp2 devices.
 *
 * TODO: Make this configurable per device.
 */
static int serialize_io = 1;
module_param(serialize_io, int, 0444);
MODULE_PARM_DESC(serialize_io, "Serialize I/O coming from scsi drivers (default = 1, faster = 0)");

/*
 * Bump up max_sectors if you'd like to support very large sized
 * transfers. Please note that some older sbp2 bridge chips are broken for
 * transfers greater or equal to 128KB.  Default is a value of 255
 * sectors, or just under 128KB (at 512 byte sector size). I can note that
 * the Oxsemi sbp2 chipsets have no problems supporting very large
 * transfer sizes.
 */
static int max_sectors = SBP2_MAX_SECTORS;
module_param(max_sectors, int, 0444);
MODULE_PARM_DESC(max_sectors, "Change max sectors per I/O supported (default = 255)");

/*
 * Exclusive login to sbp2 device? In most cases, the sbp2 driver should
 * do an exclusive login, as it's generally unsafe to have two hosts
 * talking to a single sbp2 device at the same time (filesystem coherency,
 * etc.). If you're running an sbp2 device that supports multiple logins,
 * and you're either running read-only filesystems or some sort of special
 * filesystem supporting multiple hosts (one such filesystem is OpenGFS,
 * see opengfs.sourceforge.net for more info), then set exclusive_login
 * to zero. Note: The Oxsemi OXFW911 sbp2 chipset supports up to four
 * concurrent logins.
 */
static int exclusive_login = 1;
module_param(exclusive_login, int, 0644);
MODULE_PARM_DESC(exclusive_login, "Exclusive login to sbp2 device (default = 1)");

/*
 * SCSI inquiry hack for really badly behaved sbp2 devices. Turn this on
 * if your sbp2 device is not properly handling the SCSI inquiry command.
 * This hack makes the inquiry look more like a typical MS Windows
 * inquiry.
 *
 * If force_inquiry_hack=1 is required for your device to work,
 * please submit the logged sbp2_firmware_revision value of this device to
 * the linux1394-devel mailing list.
 */
static int force_inquiry_hack;
module_param(force_inquiry_hack, int, 0444);
MODULE_PARM_DESC(force_inquiry_hack, "Force SCSI inquiry hack (default = 0)");

/*
 * Export information about protocols/devices supported by this driver.
 */
static struct ieee1394_device_id sbp2_id_table[] = {
	{
	 .match_flags = IEEE1394_MATCH_SPECIFIER_ID | IEEE1394_MATCH_VERSION,
	 .specifier_id = SBP2_UNIT_SPEC_ID_ENTRY & 0xffffff,
	 .version = SBP2_SW_VERSION_ENTRY & 0xffffff},
	{}
};

MODULE_DEVICE_TABLE(ieee1394, sbp2_id_table);

/*
 * Debug levels, configured via kernel config, or enable here.
 */

#define CONFIG_IEEE1394_SBP2_DEBUG 0
/* #define CONFIG_IEEE1394_SBP2_DEBUG_ORBS */
/* #define CONFIG_IEEE1394_SBP2_DEBUG_DMA */
/* #define CONFIG_IEEE1394_SBP2_DEBUG 1 */
/* #define CONFIG_IEEE1394_SBP2_DEBUG 2 */
/* #define CONFIG_IEEE1394_SBP2_PACKET_DUMP */

#ifdef CONFIG_IEEE1394_SBP2_DEBUG_ORBS
#define SBP2_ORB_DEBUG(fmt, args...)	HPSB_ERR("sbp2(%s): "fmt, __FUNCTION__, ## args)
static u32 global_outstanding_command_orbs = 0;
#define outstanding_orb_incr global_outstanding_command_orbs++
#define outstanding_orb_decr global_outstanding_command_orbs--
#else
#define SBP2_ORB_DEBUG(fmt, args...)
#define outstanding_orb_incr
#define outstanding_orb_decr
#endif

#ifdef CONFIG_IEEE1394_SBP2_DEBUG_DMA
#define SBP2_DMA_ALLOC(fmt, args...) \
	HPSB_ERR("sbp2(%s)alloc(%d): "fmt, __FUNCTION__, \
		 ++global_outstanding_dmas, ## args)
#define SBP2_DMA_FREE(fmt, args...) \
	HPSB_ERR("sbp2(%s)free(%d): "fmt, __FUNCTION__, \
		 --global_outstanding_dmas, ## args)
static u32 global_outstanding_dmas = 0;
#else
#define SBP2_DMA_ALLOC(fmt, args...)
#define SBP2_DMA_FREE(fmt, args...)
#endif

#if CONFIG_IEEE1394_SBP2_DEBUG >= 2
#define SBP2_DEBUG(fmt, args...)	HPSB_ERR("sbp2: "fmt, ## args)
#define SBP2_INFO(fmt, args...)		HPSB_ERR("sbp2: "fmt, ## args)
#define SBP2_NOTICE(fmt, args...)	HPSB_ERR("sbp2: "fmt, ## args)
#define SBP2_WARN(fmt, args...)		HPSB_ERR("sbp2: "fmt, ## args)
#elif CONFIG_IEEE1394_SBP2_DEBUG == 1
#define SBP2_DEBUG(fmt, args...)	HPSB_DEBUG("sbp2: "fmt, ## args)
#define SBP2_INFO(fmt, args...)		HPSB_INFO("sbp2: "fmt, ## args)
#define SBP2_NOTICE(fmt, args...)	HPSB_NOTICE("sbp2: "fmt, ## args)
#define SBP2_WARN(fmt, args...)		HPSB_WARN("sbp2: "fmt, ## args)
#else
#define SBP2_DEBUG(fmt, args...)
#define SBP2_INFO(fmt, args...)		HPSB_INFO("sbp2: "fmt, ## args)
#define SBP2_NOTICE(fmt, args...)       HPSB_NOTICE("sbp2: "fmt, ## args)
#define SBP2_WARN(fmt, args...)         HPSB_WARN("sbp2: "fmt, ## args)
#endif

#define SBP2_ERR(fmt, args...)		HPSB_ERR("sbp2: "fmt, ## args)

/*
 * Globals
 */

static void sbp2scsi_complete_all_commands(struct scsi_id_instance_data *scsi_id,
					   u32 status);

static void sbp2scsi_complete_command(struct scsi_id_instance_data *scsi_id,
				      u32 scsi_status, struct scsi_cmnd *SCpnt,
				      void (*done)(struct scsi_cmnd *));

static struct scsi_host_template scsi_driver_template;

static const u8 sbp2_speedto_max_payload[] = { 0x7, 0x8, 0x9, 0xA, 0xB, 0xC };

static void sbp2_host_reset(struct hpsb_host *host);

static int sbp2_probe(struct device *dev);
static int sbp2_remove(struct device *dev);
static int sbp2_update(struct unit_directory *ud);

static struct hpsb_highlevel sbp2_highlevel = {
	.name =		SBP2_DEVICE_NAME,
	.host_reset =	sbp2_host_reset,
};

static struct hpsb_address_ops sbp2_ops = {
	.write = sbp2_handle_status_write
};

#ifdef CONFIG_IEEE1394_SBP2_PHYS_DMA
static struct hpsb_address_ops sbp2_physdma_ops = {
	.read = sbp2_handle_physdma_read,
	.write = sbp2_handle_physdma_write,
};
#endif

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


/* List of device firmware's that require a forced 36 byte inquiry.  */
static u32 sbp2_broken_inquiry_list[] = {
	0x00002800,	/* Stefan Richter <richtest@bauwesen.tu-cottbus.de> */
			/* DViCO Momobay CX-1 */
	0x00000200	/* Andreas Plesch <plesch@fas.harvard.edu> */
			/* QPS Fire DVDBurner */
};

#define NUM_BROKEN_INQUIRY_DEVS \
	(sizeof(sbp2_broken_inquiry_list)/sizeof(*sbp2_broken_inquiry_list))

/**************************************
 * General utility functions
 **************************************/

#ifndef __BIG_ENDIAN
/*
 * Converts a buffer from be32 to cpu byte ordering. Length is in bytes.
 */
static __inline__ void sbp2util_be32_to_cpu_buffer(void *buffer, int length)
{
	u32 *temp = buffer;

	for (length = (length >> 2); length--; )
		temp[length] = be32_to_cpu(temp[length]);

	return;
}

/*
 * Converts a buffer from cpu to be32 byte ordering. Length is in bytes.
 */
static __inline__ void sbp2util_cpu_to_be32_buffer(void *buffer, int length)
{
	u32 *temp = buffer;

	for (length = (length >> 2); length--; )
		temp[length] = cpu_to_be32(temp[length]);

	return;
}
#else /* BIG_ENDIAN */
/* Why waste the cpu cycles? */
#define sbp2util_be32_to_cpu_buffer(x,y)
#define sbp2util_cpu_to_be32_buffer(x,y)
#endif

#ifdef CONFIG_IEEE1394_SBP2_PACKET_DUMP
/*
 * Debug packet dump routine. Length is in bytes.
 */
static void sbp2util_packet_dump(void *buffer, int length, char *dump_name,
				 u32 dump_phys_addr)
{
	int i;
	unsigned char *dump = buffer;

	if (!dump || !length || !dump_name)
		return;

	if (dump_phys_addr)
		printk("[%s, 0x%x]", dump_name, dump_phys_addr);
	else
		printk("[%s]", dump_name);
	for (i = 0; i < length; i++) {
		if (i > 0x3f) {
			printk("\n   ...");
			break;
		}
		if ((i & 0x3) == 0)
			printk("  ");
		if ((i & 0xf) == 0)
			printk("\n   ");
		printk("%02x ", (int)dump[i]);
	}
	printk("\n");

	return;
}
#else
#define sbp2util_packet_dump(w,x,y,z)
#endif

/*
 * Goofy routine that basically does a down_timeout function.
 */
static int sbp2util_down_timeout(atomic_t *done, int timeout)
{
	int i;

	for (i = timeout; (i > 0 && atomic_read(done) == 0); i-= HZ/10) {
		if (msleep_interruptible(100))	/* 100ms */
			return 1;
	}
	return (i > 0) ? 0 : 1;
}

/* Free's an allocated packet */
static void sbp2_free_packet(struct hpsb_packet *packet)
{
	hpsb_free_tlabel(packet);
	hpsb_free_packet(packet);
}

/* This is much like hpsb_node_write(), except it ignores the response
 * subaction and returns immediately. Can be used from interrupts.
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

/*
 * This function is called to create a pool of command orbs used for
 * command processing. It is called when a new sbp2 device is detected.
 */
static int sbp2util_create_command_orb_pool(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2scsi_host_info *hi = scsi_id->hi;
	int i;
	unsigned long flags, orbs;
	struct sbp2_command_info *command;

	orbs = serialize_io ? 2 : SBP2_MAX_CMDS;

	spin_lock_irqsave(&scsi_id->sbp2_command_orb_lock, flags);
	for (i = 0; i < orbs; i++) {
		command = kzalloc(sizeof(*command), GFP_ATOMIC);
		if (!command) {
			spin_unlock_irqrestore(&scsi_id->sbp2_command_orb_lock,
					       flags);
			return -ENOMEM;
		}
		command->command_orb_dma =
		    pci_map_single(hi->host->pdev, &command->command_orb,
				   sizeof(struct sbp2_command_orb),
				   PCI_DMA_BIDIRECTIONAL);
		SBP2_DMA_ALLOC("single command orb DMA");
		command->sge_dma =
		    pci_map_single(hi->host->pdev,
				   &command->scatter_gather_element,
				   sizeof(command->scatter_gather_element),
				   PCI_DMA_BIDIRECTIONAL);
		SBP2_DMA_ALLOC("scatter_gather_element");
		INIT_LIST_HEAD(&command->list);
		list_add_tail(&command->list, &scsi_id->sbp2_command_orb_completed);
	}
	spin_unlock_irqrestore(&scsi_id->sbp2_command_orb_lock, flags);
	return 0;
}

/*
 * This function is called to delete a pool of command orbs.
 */
static void sbp2util_remove_command_orb_pool(struct scsi_id_instance_data *scsi_id)
{
	struct hpsb_host *host = scsi_id->hi->host;
	struct list_head *lh, *next;
	struct sbp2_command_info *command;
	unsigned long flags;

	spin_lock_irqsave(&scsi_id->sbp2_command_orb_lock, flags);
	if (!list_empty(&scsi_id->sbp2_command_orb_completed)) {
		list_for_each_safe(lh, next, &scsi_id->sbp2_command_orb_completed) {
			command = list_entry(lh, struct sbp2_command_info, list);

			/* Release our generic DMA's */
			pci_unmap_single(host->pdev, command->command_orb_dma,
					 sizeof(struct sbp2_command_orb),
					 PCI_DMA_BIDIRECTIONAL);
			SBP2_DMA_FREE("single command orb DMA");
			pci_unmap_single(host->pdev, command->sge_dma,
					 sizeof(command->scatter_gather_element),
					 PCI_DMA_BIDIRECTIONAL);
			SBP2_DMA_FREE("scatter_gather_element");

			kfree(command);
		}
	}
	spin_unlock_irqrestore(&scsi_id->sbp2_command_orb_lock, flags);
	return;
}

/*
 * This function finds the sbp2_command for a given outstanding command
 * orb.Only looks at the inuse list.
 */
static struct sbp2_command_info *sbp2util_find_command_for_orb(
		struct scsi_id_instance_data *scsi_id, dma_addr_t orb)
{
	struct sbp2_command_info *command;
	unsigned long flags;

	spin_lock_irqsave(&scsi_id->sbp2_command_orb_lock, flags);
	if (!list_empty(&scsi_id->sbp2_command_orb_inuse)) {
		list_for_each_entry(command, &scsi_id->sbp2_command_orb_inuse, list) {
			if (command->command_orb_dma == orb) {
				spin_unlock_irqrestore(&scsi_id->sbp2_command_orb_lock, flags);
				return command;
			}
		}
	}
	spin_unlock_irqrestore(&scsi_id->sbp2_command_orb_lock, flags);

	SBP2_ORB_DEBUG("could not match command orb %x", (unsigned int)orb);

	return NULL;
}

/*
 * This function finds the sbp2_command for a given outstanding SCpnt.
 * Only looks at the inuse list.
 */
static struct sbp2_command_info *sbp2util_find_command_for_SCpnt(struct scsi_id_instance_data *scsi_id, void *SCpnt)
{
	struct sbp2_command_info *command;
	unsigned long flags;

	spin_lock_irqsave(&scsi_id->sbp2_command_orb_lock, flags);
	if (!list_empty(&scsi_id->sbp2_command_orb_inuse)) {
		list_for_each_entry(command, &scsi_id->sbp2_command_orb_inuse, list) {
			if (command->Current_SCpnt == SCpnt) {
				spin_unlock_irqrestore(&scsi_id->sbp2_command_orb_lock, flags);
				return command;
			}
		}
	}
	spin_unlock_irqrestore(&scsi_id->sbp2_command_orb_lock, flags);
	return NULL;
}

/*
 * This function allocates a command orb used to send a scsi command.
 */
static struct sbp2_command_info *sbp2util_allocate_command_orb(
		struct scsi_id_instance_data *scsi_id,
		struct scsi_cmnd *Current_SCpnt,
		void (*Current_done)(struct scsi_cmnd *))
{
	struct list_head *lh;
	struct sbp2_command_info *command = NULL;
	unsigned long flags;

	spin_lock_irqsave(&scsi_id->sbp2_command_orb_lock, flags);
	if (!list_empty(&scsi_id->sbp2_command_orb_completed)) {
		lh = scsi_id->sbp2_command_orb_completed.next;
		list_del(lh);
		command = list_entry(lh, struct sbp2_command_info, list);
		command->Current_done = Current_done;
		command->Current_SCpnt = Current_SCpnt;
		list_add_tail(&command->list, &scsi_id->sbp2_command_orb_inuse);
	} else {
		SBP2_ERR("sbp2util_allocate_command_orb - No orbs available!");
	}
	spin_unlock_irqrestore(&scsi_id->sbp2_command_orb_lock, flags);
	return command;
}

/* Free our DMA's */
static void sbp2util_free_command_dma(struct sbp2_command_info *command)
{
	struct scsi_id_instance_data *scsi_id =
		(struct scsi_id_instance_data *)command->Current_SCpnt->device->host->hostdata[0];
	struct hpsb_host *host;

	if (!scsi_id) {
		printk(KERN_ERR "%s: scsi_id == NULL\n", __FUNCTION__);
		return;
	}

	host = scsi_id->ud->ne->host;

	if (command->cmd_dma) {
		if (command->dma_type == CMD_DMA_SINGLE) {
			pci_unmap_single(host->pdev, command->cmd_dma,
					 command->dma_size, command->dma_dir);
			SBP2_DMA_FREE("single bulk");
		} else if (command->dma_type == CMD_DMA_PAGE) {
			pci_unmap_page(host->pdev, command->cmd_dma,
				       command->dma_size, command->dma_dir);
			SBP2_DMA_FREE("single page");
		} /* XXX: Check for CMD_DMA_NONE bug */
		command->dma_type = CMD_DMA_NONE;
		command->cmd_dma = 0;
	}

	if (command->sge_buffer) {
		pci_unmap_sg(host->pdev, command->sge_buffer,
			     command->dma_size, command->dma_dir);
		SBP2_DMA_FREE("scatter list");
		command->sge_buffer = NULL;
	}
}

/*
 * This function moves a command to the completed orb list.
 */
static void sbp2util_mark_command_completed(struct scsi_id_instance_data *scsi_id,
					    struct sbp2_command_info *command)
{
	unsigned long flags;

	spin_lock_irqsave(&scsi_id->sbp2_command_orb_lock, flags);
	list_del(&command->list);
	sbp2util_free_command_dma(command);
	list_add_tail(&command->list, &scsi_id->sbp2_command_orb_completed);
	spin_unlock_irqrestore(&scsi_id->sbp2_command_orb_lock, flags);
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
static struct scsi_id_instance_data *sbp2_alloc_device(struct unit_directory *ud);

static int sbp2_probe(struct device *dev)
{
	struct unit_directory *ud;
	struct scsi_id_instance_data *scsi_id;

	SBP2_DEBUG("sbp2_probe");

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

	SBP2_DEBUG("sbp2_remove");

	ud = container_of(dev, struct unit_directory, device);
	scsi_id = ud->device.driver_data;
	if (!scsi_id)
		return 0;

	/* Trigger shutdown functions in scsi's highlevel. */
	if (scsi_id->scsi_host)
		scsi_unblock_requests(scsi_id->scsi_host);
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

	SBP2_DEBUG("sbp2_update");

	if (sbp2_reconnect_device(scsi_id)) {

		/*
		 * Ok, reconnect has failed. Perhaps we didn't
		 * reconnect fast enough. Try doing a regular login, but
		 * first do a logout just in case of any weirdness.
		 */
		sbp2_logout_device(scsi_id);

		if (sbp2_login_device(scsi_id)) {
			/* Login failed too, just fail, and the backend
			 * will call our sbp2_remove for us */
			SBP2_ERR("Failed to reconnect to sbp2 device!");
			return -EBUSY;
		}
	}

	/* Set max retries to something large on the device. */
	sbp2_set_busy_timeout(scsi_id);

	/* Do a SBP-2 fetch agent reset. */
	sbp2_agent_reset(scsi_id, 1);

	/* Get the max speed and packet size that we can use. */
	sbp2_max_speed_and_size(scsi_id);

	/* Complete any pending commands with busy (so they get
	 * retried) and remove them from our queue
	 */
	sbp2scsi_complete_all_commands(scsi_id, DID_BUS_BUSY);

	/* Make sure we unblock requests (since this is likely after a bus
	 * reset). */
	scsi_unblock_requests(scsi_id->scsi_host);

	return 0;
}

/* This functions is called by the sbp2_probe, for each new device. We now
 * allocate one scsi host for each scsi_id (unit directory). */
static struct scsi_id_instance_data *sbp2_alloc_device(struct unit_directory *ud)
{
	struct sbp2scsi_host_info *hi;
	struct Scsi_Host *scsi_host = NULL;
	struct scsi_id_instance_data *scsi_id = NULL;

	SBP2_DEBUG("sbp2_alloc_device");

	scsi_id = kzalloc(sizeof(*scsi_id), GFP_KERNEL);
	if (!scsi_id) {
		SBP2_ERR("failed to create scsi_id");
		goto failed_alloc;
	}

	scsi_id->ne = ud->ne;
	scsi_id->ud = ud;
	scsi_id->speed_code = IEEE1394_SPEED_100;
	scsi_id->max_payload_size = sbp2_speedto_max_payload[IEEE1394_SPEED_100];
	atomic_set(&scsi_id->sbp2_login_complete, 0);
	INIT_LIST_HEAD(&scsi_id->sbp2_command_orb_inuse);
	INIT_LIST_HEAD(&scsi_id->sbp2_command_orb_completed);
	INIT_LIST_HEAD(&scsi_id->scsi_list);
	spin_lock_init(&scsi_id->sbp2_command_orb_lock);
	scsi_id->sbp2_lun = 0;

	ud->device.driver_data = scsi_id;

	hi = hpsb_get_hostinfo(&sbp2_highlevel, ud->ne->host);
	if (!hi) {
		hi = hpsb_create_hostinfo(&sbp2_highlevel, ud->ne->host, sizeof(*hi));
		if (!hi) {
			SBP2_ERR("failed to allocate hostinfo");
			goto failed_alloc;
		}
		SBP2_DEBUG("sbp2_alloc_device: allocated hostinfo");
		hi->host = ud->ne->host;
		INIT_LIST_HEAD(&hi->scsi_ids);

		/* Register our sbp2 status address space... */
		hpsb_register_addrspace(&sbp2_highlevel, ud->ne->host, &sbp2_ops,
					SBP2_STATUS_FIFO_ADDRESS,
					SBP2_STATUS_FIFO_ADDRESS +
					SBP2_STATUS_FIFO_ENTRY_TO_OFFSET(SBP2_MAX_UDS_PER_NODE+1));
#ifdef CONFIG_IEEE1394_SBP2_PHYS_DMA
		/* Handle data movement if physical dma is not
		 * enabled/supportedon host controller */
		hpsb_register_addrspace(&sbp2_highlevel, ud->ne->host, &sbp2_physdma_ops,
					0x0ULL, 0xfffffffcULL);
#endif
	}

	scsi_id->hi = hi;

	list_add_tail(&scsi_id->scsi_list, &hi->scsi_ids);

	/* Register our host with the SCSI stack. */
	scsi_host = scsi_host_alloc(&scsi_driver_template,
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
	struct sbp2scsi_host_info *hi;
	struct scsi_id_instance_data *scsi_id;

	hi = hpsb_get_hostinfo(&sbp2_highlevel, host);

	if (hi) {
		list_for_each_entry(scsi_id, &hi->scsi_ids, scsi_list)
			scsi_block_requests(scsi_id->scsi_host);
	}
}

/*
 * This function is where we first pull the node unique ids, and then
 * allocate memory and register a SBP-2 device.
 */
static int sbp2_start_device(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2scsi_host_info *hi = scsi_id->hi;
	int error;

	SBP2_DEBUG("sbp2_start_device");

	/* Login FIFO DMA */
	scsi_id->login_response =
		pci_alloc_consistent(hi->host->pdev,
				     sizeof(struct sbp2_login_response),
				     &scsi_id->login_response_dma);
	if (!scsi_id->login_response)
		goto alloc_fail;
	SBP2_DMA_ALLOC("consistent DMA region for login FIFO");

	/* Query logins ORB DMA */
	scsi_id->query_logins_orb =
		pci_alloc_consistent(hi->host->pdev,
				     sizeof(struct sbp2_query_logins_orb),
				     &scsi_id->query_logins_orb_dma);
	if (!scsi_id->query_logins_orb)
		goto alloc_fail;
	SBP2_DMA_ALLOC("consistent DMA region for query logins ORB");

	/* Query logins response DMA */
	scsi_id->query_logins_response =
		pci_alloc_consistent(hi->host->pdev,
				     sizeof(struct sbp2_query_logins_response),
				     &scsi_id->query_logins_response_dma);
	if (!scsi_id->query_logins_response)
		goto alloc_fail;
	SBP2_DMA_ALLOC("consistent DMA region for query logins response");

	/* Reconnect ORB DMA */
	scsi_id->reconnect_orb =
		pci_alloc_consistent(hi->host->pdev,
				     sizeof(struct sbp2_reconnect_orb),
				     &scsi_id->reconnect_orb_dma);
	if (!scsi_id->reconnect_orb)
		goto alloc_fail;
	SBP2_DMA_ALLOC("consistent DMA region for reconnect ORB");

	/* Logout ORB DMA */
	scsi_id->logout_orb =
		pci_alloc_consistent(hi->host->pdev,
				     sizeof(struct sbp2_logout_orb),
				     &scsi_id->logout_orb_dma);
	if (!scsi_id->logout_orb)
		goto alloc_fail;
	SBP2_DMA_ALLOC("consistent DMA region for logout ORB");

	/* Login ORB DMA */
	scsi_id->login_orb =
		pci_alloc_consistent(hi->host->pdev,
				     sizeof(struct sbp2_login_orb),
				     &scsi_id->login_orb_dma);
	if (!scsi_id->login_orb)
		goto alloc_fail;
	SBP2_DMA_ALLOC("consistent DMA region for login ORB");

	SBP2_DEBUG("New SBP-2 device inserted, SCSI ID = %x", scsi_id->ud->id);

	/*
	 * Create our command orb pool
	 */
	if (sbp2util_create_command_orb_pool(scsi_id)) {
		SBP2_ERR("sbp2util_create_command_orb_pool failed!");
		sbp2_remove_device(scsi_id);
		return -ENOMEM;
	}

	/* Schedule a timeout here. The reason is that we may be so close
	 * to a bus reset, that the device is not available for logins.
	 * This can happen when the bus reset is caused by the host
	 * connected to the sbp2 device being removed. That host would
	 * have a certain amount of time to relogin before the sbp2 device
	 * allows someone else to login instead. One second makes sense. */
	msleep_interruptible(1000);
	if (signal_pending(current)) {
		SBP2_WARN("aborting sbp2_start_device due to event");
		sbp2_remove_device(scsi_id);
		return -EINTR;
	}

	/*
	 * Login to the sbp-2 device
	 */
	if (sbp2_login_device(scsi_id)) {
		/* Login failed, just remove the device. */
		sbp2_remove_device(scsi_id);
		return -EBUSY;
	}

	/*
	 * Set max retries to something large on the device
	 */
	sbp2_set_busy_timeout(scsi_id);

	/*
	 * Do a SBP-2 fetch agent reset
	 */
	sbp2_agent_reset(scsi_id, 1);

	/*
	 * Get the max speed and packet size that we can use
	 */
	sbp2_max_speed_and_size(scsi_id);

	/* Add this device to the scsi layer now */
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

/*
 * This function removes an sbp2 device from the sbp2scsi_host_info struct.
 */
static void sbp2_remove_device(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2scsi_host_info *hi;

	SBP2_DEBUG("sbp2_remove_device");

	if (!scsi_id)
		return;

	hi = scsi_id->hi;

	/* This will remove our scsi device aswell */
	if (scsi_id->scsi_host) {
		scsi_remove_host(scsi_id->scsi_host);
		scsi_host_put(scsi_id->scsi_host);
	}

	sbp2util_remove_command_orb_pool(scsi_id);

	list_del(&scsi_id->scsi_list);

	if (scsi_id->login_response) {
		pci_free_consistent(hi->host->pdev,
				    sizeof(struct sbp2_login_response),
				    scsi_id->login_response,
				    scsi_id->login_response_dma);
		SBP2_DMA_FREE("single login FIFO");
	}

	if (scsi_id->login_orb) {
		pci_free_consistent(hi->host->pdev,
				    sizeof(struct sbp2_login_orb),
				    scsi_id->login_orb,
				    scsi_id->login_orb_dma);
		SBP2_DMA_FREE("single login ORB");
	}

	if (scsi_id->reconnect_orb) {
		pci_free_consistent(hi->host->pdev,
				    sizeof(struct sbp2_reconnect_orb),
				    scsi_id->reconnect_orb,
				    scsi_id->reconnect_orb_dma);
		SBP2_DMA_FREE("single reconnect orb");
	}

	if (scsi_id->logout_orb) {
		pci_free_consistent(hi->host->pdev,
				    sizeof(struct sbp2_logout_orb),
				    scsi_id->logout_orb,
				    scsi_id->logout_orb_dma);
		SBP2_DMA_FREE("single logout orb");
	}

	if (scsi_id->query_logins_orb) {
		pci_free_consistent(hi->host->pdev,
				    sizeof(struct sbp2_query_logins_orb),
				    scsi_id->query_logins_orb,
				    scsi_id->query_logins_orb_dma);
		SBP2_DMA_FREE("single query logins orb");
	}

	if (scsi_id->query_logins_response) {
		pci_free_consistent(hi->host->pdev,
				    sizeof(struct sbp2_query_logins_response),
				    scsi_id->query_logins_response,
				    scsi_id->query_logins_response_dma);
		SBP2_DMA_FREE("single query logins data");
	}

	scsi_id->ud->device.driver_data = NULL;

	SBP2_DEBUG("SBP-2 device removed, SCSI ID = %d", scsi_id->ud->id);

	kfree(scsi_id);
}

#ifdef CONFIG_IEEE1394_SBP2_PHYS_DMA
/*
 * This function deals with physical dma write requests (for adapters that do not support
 * physical dma in hardware). Mostly just here for debugging...
 */
static int sbp2_handle_physdma_write(struct hpsb_host *host, int nodeid,
				     int destid, quadlet_t *data, u64 addr,
				     size_t length, u16 flags)
{

	/*
	 * Manually put the data in the right place.
	 */
	memcpy(bus_to_virt((u32) addr), data, length);
	sbp2util_packet_dump(data, length, "sbp2 phys dma write by device",
			     (u32) addr);
	return RCODE_COMPLETE;
}

/*
 * This function deals with physical dma read requests (for adapters that do not support
 * physical dma in hardware). Mostly just here for debugging...
 */
static int sbp2_handle_physdma_read(struct hpsb_host *host, int nodeid,
				    quadlet_t *data, u64 addr, size_t length,
				    u16 flags)
{

	/*
	 * Grab data from memory and send a read response.
	 */
	memcpy(data, bus_to_virt((u32) addr), length);
	sbp2util_packet_dump(data, length, "sbp2 phys dma read by device",
			     (u32) addr);
	return RCODE_COMPLETE;
}
#endif

/**************************************
 * SBP-2 protocol related section
 **************************************/

/*
 * This function queries the device for the maximum concurrent logins it
 * supports.
 */
static int sbp2_query_logins(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2scsi_host_info *hi = scsi_id->hi;
	quadlet_t data[2];
	int max_logins;
	int active_logins;

	SBP2_DEBUG("sbp2_query_logins");

	scsi_id->query_logins_orb->reserved1 = 0x0;
	scsi_id->query_logins_orb->reserved2 = 0x0;

	scsi_id->query_logins_orb->query_response_lo = scsi_id->query_logins_response_dma;
	scsi_id->query_logins_orb->query_response_hi = ORB_SET_NODE_ID(hi->host->node_id);
	SBP2_DEBUG("sbp2_query_logins: query_response_hi/lo initialized");

	scsi_id->query_logins_orb->lun_misc = ORB_SET_FUNCTION(SBP2_QUERY_LOGINS_REQUEST);
	scsi_id->query_logins_orb->lun_misc |= ORB_SET_NOTIFY(1);
	scsi_id->query_logins_orb->lun_misc |= ORB_SET_LUN(scsi_id->sbp2_lun);
	SBP2_DEBUG("sbp2_query_logins: lun_misc initialized");

	scsi_id->query_logins_orb->reserved_resp_length =
		ORB_SET_QUERY_LOGINS_RESP_LENGTH(sizeof(struct sbp2_query_logins_response));
	SBP2_DEBUG("sbp2_query_logins: reserved_resp_length initialized");

	scsi_id->query_logins_orb->status_FIFO_lo = SBP2_STATUS_FIFO_ADDRESS_LO +
						    SBP2_STATUS_FIFO_ENTRY_TO_OFFSET(scsi_id->ud->id);
	scsi_id->query_logins_orb->status_FIFO_hi = (ORB_SET_NODE_ID(hi->host->node_id) |
						     SBP2_STATUS_FIFO_ADDRESS_HI);
	SBP2_DEBUG("sbp2_query_logins: status FIFO initialized");

	sbp2util_cpu_to_be32_buffer(scsi_id->query_logins_orb, sizeof(struct sbp2_query_logins_orb));

	SBP2_DEBUG("sbp2_query_logins: orb byte-swapped");

	sbp2util_packet_dump(scsi_id->query_logins_orb, sizeof(struct sbp2_query_logins_orb),
			     "sbp2 query logins orb", scsi_id->query_logins_orb_dma);

	memset(scsi_id->query_logins_response, 0, sizeof(struct sbp2_query_logins_response));
	memset(&scsi_id->status_block, 0, sizeof(struct sbp2_status_block));

	SBP2_DEBUG("sbp2_query_logins: query_logins_response/status FIFO memset");

	data[0] = ORB_SET_NODE_ID(hi->host->node_id);
	data[1] = scsi_id->query_logins_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);

	atomic_set(&scsi_id->sbp2_login_complete, 0);

	SBP2_DEBUG("sbp2_query_logins: prepared to write");
	hpsb_node_write(scsi_id->ne, scsi_id->sbp2_management_agent_addr, data, 8);
	SBP2_DEBUG("sbp2_query_logins: written");

	if (sbp2util_down_timeout(&scsi_id->sbp2_login_complete, 2*HZ)) {
		SBP2_INFO("Error querying logins to SBP-2 device - timed out");
		return -EIO;
	}

	if (scsi_id->status_block.ORB_offset_lo != scsi_id->query_logins_orb_dma) {
		SBP2_INFO("Error querying logins to SBP-2 device - timed out");
		return -EIO;
	}

	if (STATUS_GET_RESP(scsi_id->status_block.ORB_offset_hi_misc) ||
	    STATUS_GET_DEAD_BIT(scsi_id->status_block.ORB_offset_hi_misc) ||
	    STATUS_GET_SBP_STATUS(scsi_id->status_block.ORB_offset_hi_misc)) {

		SBP2_INFO("Error querying logins to SBP-2 device - timed out");
		return -EIO;
	}

	sbp2util_cpu_to_be32_buffer(scsi_id->query_logins_response, sizeof(struct sbp2_query_logins_response));

	SBP2_DEBUG("length_max_logins = %x",
		   (unsigned int)scsi_id->query_logins_response->length_max_logins);

	SBP2_DEBUG("Query logins to SBP-2 device successful");

	max_logins = RESPONSE_GET_MAX_LOGINS(scsi_id->query_logins_response->length_max_logins);
	SBP2_DEBUG("Maximum concurrent logins supported: %d", max_logins);

	active_logins = RESPONSE_GET_ACTIVE_LOGINS(scsi_id->query_logins_response->length_max_logins);
	SBP2_DEBUG("Number of active logins: %d", active_logins);

	if (active_logins >= max_logins) {
		return -EIO;
	}

	return 0;
}

/*
 * This function is called in order to login to a particular SBP-2 device,
 * after a bus reset.
 */
static int sbp2_login_device(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2scsi_host_info *hi = scsi_id->hi;
	quadlet_t data[2];

	SBP2_DEBUG("sbp2_login_device");

	if (!scsi_id->login_orb) {
		SBP2_DEBUG("sbp2_login_device: login_orb not alloc'd!");
		return -EIO;
	}

	if (!exclusive_login) {
		if (sbp2_query_logins(scsi_id)) {
			SBP2_INFO("Device does not support any more concurrent logins");
			return -EIO;
		}
	}

	/* Set-up login ORB, assume no password */
	scsi_id->login_orb->password_hi = 0;
	scsi_id->login_orb->password_lo = 0;
	SBP2_DEBUG("sbp2_login_device: password_hi/lo initialized");

	scsi_id->login_orb->login_response_lo = scsi_id->login_response_dma;
	scsi_id->login_orb->login_response_hi = ORB_SET_NODE_ID(hi->host->node_id);
	SBP2_DEBUG("sbp2_login_device: login_response_hi/lo initialized");

	scsi_id->login_orb->lun_misc = ORB_SET_FUNCTION(SBP2_LOGIN_REQUEST);
	scsi_id->login_orb->lun_misc |= ORB_SET_RECONNECT(0);	/* One second reconnect time */
	scsi_id->login_orb->lun_misc |= ORB_SET_EXCLUSIVE(exclusive_login);	/* Exclusive access to device */
	scsi_id->login_orb->lun_misc |= ORB_SET_NOTIFY(1);	/* Notify us of login complete */
	scsi_id->login_orb->lun_misc |= ORB_SET_LUN(scsi_id->sbp2_lun);
	SBP2_DEBUG("sbp2_login_device: lun_misc initialized");

	scsi_id->login_orb->passwd_resp_lengths =
		ORB_SET_LOGIN_RESP_LENGTH(sizeof(struct sbp2_login_response));
	SBP2_DEBUG("sbp2_login_device: passwd_resp_lengths initialized");

	scsi_id->login_orb->status_FIFO_lo = SBP2_STATUS_FIFO_ADDRESS_LO +
					     SBP2_STATUS_FIFO_ENTRY_TO_OFFSET(scsi_id->ud->id);
	scsi_id->login_orb->status_FIFO_hi = (ORB_SET_NODE_ID(hi->host->node_id) |
					      SBP2_STATUS_FIFO_ADDRESS_HI);
	SBP2_DEBUG("sbp2_login_device: status FIFO initialized");

	/*
	 * Byte swap ORB if necessary
	 */
	sbp2util_cpu_to_be32_buffer(scsi_id->login_orb, sizeof(struct sbp2_login_orb));

	SBP2_DEBUG("sbp2_login_device: orb byte-swapped");

	sbp2util_packet_dump(scsi_id->login_orb, sizeof(struct sbp2_login_orb),
			     "sbp2 login orb", scsi_id->login_orb_dma);

	/*
	 * Initialize login response and status fifo
	 */
	memset(scsi_id->login_response, 0, sizeof(struct sbp2_login_response));
	memset(&scsi_id->status_block, 0, sizeof(struct sbp2_status_block));

	SBP2_DEBUG("sbp2_login_device: login_response/status FIFO memset");

	/*
	 * Ok, let's write to the target's management agent register
	 */
	data[0] = ORB_SET_NODE_ID(hi->host->node_id);
	data[1] = scsi_id->login_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);

	atomic_set(&scsi_id->sbp2_login_complete, 0);

	SBP2_DEBUG("sbp2_login_device: prepared to write to %08x",
		   (unsigned int)scsi_id->sbp2_management_agent_addr);
	hpsb_node_write(scsi_id->ne, scsi_id->sbp2_management_agent_addr, data, 8);
	SBP2_DEBUG("sbp2_login_device: written");

	/*
	 * Wait for login status (up to 20 seconds)...
	 */
	if (sbp2util_down_timeout(&scsi_id->sbp2_login_complete, 20*HZ)) {
		SBP2_ERR("Error logging into SBP-2 device - login timed-out");
		return -EIO;
	}

	/*
	 * Sanity. Make sure status returned matches login orb.
	 */
	if (scsi_id->status_block.ORB_offset_lo != scsi_id->login_orb_dma) {
		SBP2_ERR("Error logging into SBP-2 device - login timed-out");
		return -EIO;
	}

	/*
	 * Check status
	 */
	if (STATUS_GET_RESP(scsi_id->status_block.ORB_offset_hi_misc) ||
	    STATUS_GET_DEAD_BIT(scsi_id->status_block.ORB_offset_hi_misc) ||
	    STATUS_GET_SBP_STATUS(scsi_id->status_block.ORB_offset_hi_misc)) {

		SBP2_ERR("Error logging into SBP-2 device - login failed");
		return -EIO;
	}

	/*
	 * Byte swap the login response, for use when reconnecting or
	 * logging out.
	 */
	sbp2util_cpu_to_be32_buffer(scsi_id->login_response, sizeof(struct sbp2_login_response));

	/*
	 * Grab our command block agent address from the login response.
	 */
	SBP2_DEBUG("command_block_agent_hi = %x",
		   (unsigned int)scsi_id->login_response->command_block_agent_hi);
	SBP2_DEBUG("command_block_agent_lo = %x",
		   (unsigned int)scsi_id->login_response->command_block_agent_lo);

	scsi_id->sbp2_command_block_agent_addr =
		((u64)scsi_id->login_response->command_block_agent_hi) << 32;
	scsi_id->sbp2_command_block_agent_addr |= ((u64)scsi_id->login_response->command_block_agent_lo);
	scsi_id->sbp2_command_block_agent_addr &= 0x0000ffffffffffffULL;

	SBP2_INFO("Logged into SBP-2 device");

	return 0;

}

/*
 * This function is called in order to logout from a particular SBP-2
 * device, usually called during driver unload.
 */
static int sbp2_logout_device(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2scsi_host_info *hi = scsi_id->hi;
	quadlet_t data[2];
	int error;

	SBP2_DEBUG("sbp2_logout_device");

	/*
	 * Set-up logout ORB
	 */
	scsi_id->logout_orb->reserved1 = 0x0;
	scsi_id->logout_orb->reserved2 = 0x0;
	scsi_id->logout_orb->reserved3 = 0x0;
	scsi_id->logout_orb->reserved4 = 0x0;

	scsi_id->logout_orb->login_ID_misc = ORB_SET_FUNCTION(SBP2_LOGOUT_REQUEST);
	scsi_id->logout_orb->login_ID_misc |= ORB_SET_LOGIN_ID(scsi_id->login_response->length_login_ID);

	/* Notify us when complete */
	scsi_id->logout_orb->login_ID_misc |= ORB_SET_NOTIFY(1);

	scsi_id->logout_orb->reserved5 = 0x0;
	scsi_id->logout_orb->status_FIFO_lo = SBP2_STATUS_FIFO_ADDRESS_LO +
					      SBP2_STATUS_FIFO_ENTRY_TO_OFFSET(scsi_id->ud->id);
	scsi_id->logout_orb->status_FIFO_hi = (ORB_SET_NODE_ID(hi->host->node_id) |
					       SBP2_STATUS_FIFO_ADDRESS_HI);

	/*
	 * Byte swap ORB if necessary
	 */
	sbp2util_cpu_to_be32_buffer(scsi_id->logout_orb, sizeof(struct sbp2_logout_orb));

	sbp2util_packet_dump(scsi_id->logout_orb, sizeof(struct sbp2_logout_orb),
			     "sbp2 logout orb", scsi_id->logout_orb_dma);

	/*
	 * Ok, let's write to the target's management agent register
	 */
	data[0] = ORB_SET_NODE_ID(hi->host->node_id);
	data[1] = scsi_id->logout_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);

	atomic_set(&scsi_id->sbp2_login_complete, 0);

	error = hpsb_node_write(scsi_id->ne,
				scsi_id->sbp2_management_agent_addr, data, 8);
	if (error)
		return error;

	/* Wait for device to logout...1 second. */
	if (sbp2util_down_timeout(&scsi_id->sbp2_login_complete, HZ))
		return -EIO;

	SBP2_INFO("Logged out of SBP-2 device");

	return 0;

}

/*
 * This function is called in order to reconnect to a particular SBP-2
 * device, after a bus reset.
 */
static int sbp2_reconnect_device(struct scsi_id_instance_data *scsi_id)
{
	struct sbp2scsi_host_info *hi = scsi_id->hi;
	quadlet_t data[2];
	int error;

	SBP2_DEBUG("sbp2_reconnect_device");

	/*
	 * Set-up reconnect ORB
	 */
	scsi_id->reconnect_orb->reserved1 = 0x0;
	scsi_id->reconnect_orb->reserved2 = 0x0;
	scsi_id->reconnect_orb->reserved3 = 0x0;
	scsi_id->reconnect_orb->reserved4 = 0x0;

	scsi_id->reconnect_orb->login_ID_misc = ORB_SET_FUNCTION(SBP2_RECONNECT_REQUEST);
	scsi_id->reconnect_orb->login_ID_misc |=
		ORB_SET_LOGIN_ID(scsi_id->login_response->length_login_ID);

	/* Notify us when complete */
	scsi_id->reconnect_orb->login_ID_misc |= ORB_SET_NOTIFY(1);

	scsi_id->reconnect_orb->reserved5 = 0x0;
	scsi_id->reconnect_orb->status_FIFO_lo = SBP2_STATUS_FIFO_ADDRESS_LO +
						 SBP2_STATUS_FIFO_ENTRY_TO_OFFSET(scsi_id->ud->id);
	scsi_id->reconnect_orb->status_FIFO_hi =
		(ORB_SET_NODE_ID(hi->host->node_id) | SBP2_STATUS_FIFO_ADDRESS_HI);

	/*
	 * Byte swap ORB if necessary
	 */
	sbp2util_cpu_to_be32_buffer(scsi_id->reconnect_orb, sizeof(struct sbp2_reconnect_orb));

	sbp2util_packet_dump(scsi_id->reconnect_orb, sizeof(struct sbp2_reconnect_orb),
			     "sbp2 reconnect orb", scsi_id->reconnect_orb_dma);

	/*
	 * Initialize status fifo
	 */
	memset(&scsi_id->status_block, 0, sizeof(struct sbp2_status_block));

	/*
	 * Ok, let's write to the target's management agent register
	 */
	data[0] = ORB_SET_NODE_ID(hi->host->node_id);
	data[1] = scsi_id->reconnect_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);

	atomic_set(&scsi_id->sbp2_login_complete, 0);

	error = hpsb_node_write(scsi_id->ne,
				scsi_id->sbp2_management_agent_addr, data, 8);
	if (error)
		return error;

	/*
	 * Wait for reconnect status (up to 1 second)...
	 */
	if (sbp2util_down_timeout(&scsi_id->sbp2_login_complete, HZ)) {
		SBP2_ERR("Error reconnecting to SBP-2 device - reconnect timed-out");
		return -EIO;
	}

	/*
	 * Sanity. Make sure status returned matches reconnect orb.
	 */
	if (scsi_id->status_block.ORB_offset_lo != scsi_id->reconnect_orb_dma) {
		SBP2_ERR("Error reconnecting to SBP-2 device - reconnect timed-out");
		return -EIO;
	}

	/*
	 * Check status
	 */
	if (STATUS_GET_RESP(scsi_id->status_block.ORB_offset_hi_misc) ||
	    STATUS_GET_DEAD_BIT(scsi_id->status_block.ORB_offset_hi_misc) ||
	    STATUS_GET_SBP_STATUS(scsi_id->status_block.ORB_offset_hi_misc)) {

		SBP2_ERR("Error reconnecting to SBP-2 device - reconnect failed");
		return -EIO;
	}

	HPSB_DEBUG("Reconnected to SBP-2 device");

	return 0;

}

/*
 * This function is called in order to set the busy timeout (number of
 * retries to attempt) on the sbp2 device.
 */
static int sbp2_set_busy_timeout(struct scsi_id_instance_data *scsi_id)
{
	quadlet_t data;

	SBP2_DEBUG("sbp2_set_busy_timeout");

	/*
	 * Ok, let's write to the target's busy timeout register
	 */
	data = cpu_to_be32(SBP2_BUSY_TIMEOUT_VALUE);

	if (hpsb_node_write(scsi_id->ne, SBP2_BUSY_TIMEOUT_ADDRESS, &data, 4)) {
		SBP2_ERR("sbp2_set_busy_timeout error");
	}

	return 0;
}

/*
 * This function is called to parse sbp2 device's config rom unit
 * directory. Used to determine things like sbp2 management agent offset,
 * and command set used (SCSI or RBC).
 */
static void sbp2_parse_unit_directory(struct scsi_id_instance_data *scsi_id,
				      struct unit_directory *ud)
{
	struct csr1212_keyval *kv;
	struct csr1212_dentry *dentry;
	u64 management_agent_addr;
	u32 command_set_spec_id, command_set, unit_characteristics,
	    firmware_revision, workarounds;
	int i;

	SBP2_DEBUG("sbp2_parse_unit_directory");

	management_agent_addr = 0x0;
	command_set_spec_id = 0x0;
	command_set = 0x0;
	unit_characteristics = 0x0;
	firmware_revision = 0x0;

	/* Handle different fields in the unit directory, based on keys */
	csr1212_for_each_dir_entry(ud->ne->csr, kv, ud->ud_kv, dentry) {
		switch (kv->key.id) {
		case CSR1212_KV_ID_DEPENDENT_INFO:
			if (kv->key.type == CSR1212_KV_TYPE_CSR_OFFSET) {
				/* Save off the management agent address */
				management_agent_addr =
				    CSR1212_REGISTER_SPACE_BASE +
				    (kv->value.csr_offset << 2);

				SBP2_DEBUG("sbp2_management_agent_addr = %x",
					   (unsigned int)management_agent_addr);
			} else if (kv->key.type == CSR1212_KV_TYPE_IMMEDIATE) {
				scsi_id->sbp2_lun =
				    ORB_SET_LUN(kv->value.immediate);
			}
			break;

		case SBP2_COMMAND_SET_SPEC_ID_KEY:
			/* Command spec organization */
			command_set_spec_id = kv->value.immediate;
			SBP2_DEBUG("sbp2_command_set_spec_id = %x",
				   (unsigned int)command_set_spec_id);
			break;

		case SBP2_COMMAND_SET_KEY:
			/* Command set used by sbp2 device */
			command_set = kv->value.immediate;
			SBP2_DEBUG("sbp2_command_set = %x",
				   (unsigned int)command_set);
			break;

		case SBP2_UNIT_CHARACTERISTICS_KEY:
			/*
			 * Unit characterisitcs (orb related stuff
			 * that I'm not yet paying attention to)
			 */
			unit_characteristics = kv->value.immediate;
			SBP2_DEBUG("sbp2_unit_characteristics = %x",
				   (unsigned int)unit_characteristics);
			break;

		case SBP2_FIRMWARE_REVISION_KEY:
			/* Firmware revision */
			firmware_revision = kv->value.immediate;
			if (force_inquiry_hack)
				SBP2_INFO("sbp2_firmware_revision = %x",
					  (unsigned int)firmware_revision);
			else
				SBP2_DEBUG("sbp2_firmware_revision = %x",
					   (unsigned int)firmware_revision);
			break;

		default:
			break;
		}
	}

	/* This is the start of our broken device checking. We try to hack
	 * around oddities and known defects.  */
	workarounds = 0x0;

	/* If the vendor id is 0xa0b8 (Symbios vendor id), then we have a
	 * bridge with 128KB max transfer size limitation. For sanity, we
	 * only voice this when the current max_sectors setting
	 * exceeds the 128k limit. By default, that is not the case.
	 *
	 * It would be really nice if we could detect this before the scsi
	 * host gets initialized. That way we can down-force the
	 * max_sectors to account for it. That is not currently
	 * possible.  */
	if ((firmware_revision & 0xffff00) ==
			SBP2_128KB_BROKEN_FIRMWARE &&
			(max_sectors * 512) > (128*1024)) {
		SBP2_WARN("Node " NODE_BUS_FMT ": Bridge only supports 128KB max transfer size.",
				NODE_BUS_ARGS(ud->ne->host, ud->ne->nodeid));
		SBP2_WARN("WARNING: Current max_sectors setting is larger than 128KB (%d sectors)!",
				max_sectors);
		workarounds |= SBP2_BREAKAGE_128K_MAX_TRANSFER;
	}

	/* Check for a blacklisted set of devices that require us to force
	 * a 36 byte host inquiry. This can be overriden as a module param
	 * (to force all hosts).  */
	for (i = 0; i < NUM_BROKEN_INQUIRY_DEVS; i++) {
		if ((firmware_revision & 0xffff00) ==
				sbp2_broken_inquiry_list[i]) {
			SBP2_WARN("Node " NODE_BUS_FMT ": Using 36byte inquiry workaround",
					NODE_BUS_ARGS(ud->ne->host, ud->ne->nodeid));
			workarounds |= SBP2_BREAKAGE_INQUIRY_HACK;
			break; /* No need to continue. */
		}
	}

	/* If this is a logical unit directory entry, process the parent
	 * to get the values. */
	if (ud->flags & UNIT_DIRECTORY_LUN_DIRECTORY) {
		struct unit_directory *parent_ud =
			container_of(ud->device.parent, struct unit_directory, device);
		sbp2_parse_unit_directory(scsi_id, parent_ud);
	} else {
		scsi_id->sbp2_management_agent_addr = management_agent_addr;
		scsi_id->sbp2_command_set_spec_id = command_set_spec_id;
		scsi_id->sbp2_command_set = command_set;
		scsi_id->sbp2_unit_characteristics = unit_characteristics;
		scsi_id->sbp2_firmware_revision = firmware_revision;
		scsi_id->workarounds = workarounds;
		if (ud->flags & UNIT_DIRECTORY_HAS_LUN)
			scsi_id->sbp2_lun = ORB_SET_LUN(ud->lun);
	}
}

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
	struct sbp2scsi_host_info *hi = scsi_id->hi;

	SBP2_DEBUG("sbp2_max_speed_and_size");

	/* Initial setting comes from the hosts speed map */
	scsi_id->speed_code =
	    hi->host->speed_map[NODEID_TO_NODE(hi->host->node_id) * 64 +
				NODEID_TO_NODE(scsi_id->ne->nodeid)];

	/* Bump down our speed if the user requested it */
	if (scsi_id->speed_code > max_speed) {
		scsi_id->speed_code = max_speed;
		SBP2_ERR("Forcing SBP-2 max speed down to %s",
			 hpsb_speedto_str[scsi_id->speed_code]);
	}

	/* Payload size is the lesser of what our speed supports and what
	 * our host supports.  */
	scsi_id->max_payload_size =
	    min(sbp2_speedto_max_payload[scsi_id->speed_code],
		(u8) (hi->host->csr.max_rec - 1));

	HPSB_DEBUG("Node " NODE_BUS_FMT ": Max speed [%s] - Max payload [%u]",
		   NODE_BUS_ARGS(hi->host, scsi_id->ne->nodeid),
		   hpsb_speedto_str[scsi_id->speed_code],
		   1 << ((u32) scsi_id->max_payload_size + 2));

	return 0;
}

/*
 * This function is called in order to perform a SBP-2 agent reset.
 */
static int sbp2_agent_reset(struct scsi_id_instance_data *scsi_id, int wait)
{
	quadlet_t data;
	u64 addr;
	int retval;

	SBP2_DEBUG("sbp2_agent_reset");

	/*
	 * Ok, let's write to the target's management agent register
	 */
	data = ntohl(SBP2_AGENT_RESET_DATA);
	addr = scsi_id->sbp2_command_block_agent_addr + SBP2_AGENT_RESET_OFFSET;

	if (wait)
		retval = hpsb_node_write(scsi_id->ne, addr, &data, 4);
	else
		retval = sbp2util_node_write_no_wait(scsi_id->ne, addr, &data, 4);

	if (retval < 0) {
		SBP2_ERR("hpsb_node_write failed.\n");
		return -EIO;
	}

	/*
	 * Need to make sure orb pointer is written on next command
	 */
	scsi_id->last_orb = NULL;

	return 0;
}

static void sbp2_prep_command_orb_sg(struct sbp2_command_orb *orb,
				     struct sbp2scsi_host_info *hi,
				     struct sbp2_command_info *command,
				     unsigned int scsi_use_sg,
				     struct scatterlist *sgpnt,
				     u32 orb_direction,
				     enum dma_data_direction dma_dir)
{
	command->dma_dir = dma_dir;
	orb->data_descriptor_hi = ORB_SET_NODE_ID(hi->host->node_id);
	orb->misc |= ORB_SET_DIRECTION(orb_direction);

	/* Special case if only one element (and less than 64KB in size) */
	if ((scsi_use_sg == 1) &&
	    (sgpnt[0].length <= SBP2_MAX_SG_ELEMENT_LENGTH)) {

		SBP2_DEBUG("Only one s/g element");
		command->dma_size = sgpnt[0].length;
		command->dma_type = CMD_DMA_PAGE;
		command->cmd_dma = pci_map_page(hi->host->pdev,
						sgpnt[0].page,
						sgpnt[0].offset,
						command->dma_size,
						command->dma_dir);
		SBP2_DMA_ALLOC("single page scatter element");

		orb->data_descriptor_lo = command->cmd_dma;
		orb->misc |= ORB_SET_DATA_SIZE(command->dma_size);

	} else {
		struct sbp2_unrestricted_page_table *sg_element =
					&command->scatter_gather_element[0];
		u32 sg_count, sg_len;
		dma_addr_t sg_addr;
		int i, count = pci_map_sg(hi->host->pdev, sgpnt, scsi_use_sg,
					  dma_dir);

		SBP2_DMA_ALLOC("scatter list");

		command->dma_size = scsi_use_sg;
		command->sge_buffer = sgpnt;

		/* use page tables (s/g) */
		orb->misc |= ORB_SET_PAGE_TABLE_PRESENT(0x1);
		orb->data_descriptor_lo = command->sge_dma;

		/*
		 * Loop through and fill out our sbp-2 page tables
		 * (and split up anything too large)
		 */
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

		/* Number of page table (s/g) elements */
		orb->misc |= ORB_SET_DATA_SIZE(sg_count);

		sbp2util_packet_dump(sg_element,
				     (sizeof(struct sbp2_unrestricted_page_table)) * sg_count,
				     "sbp2 s/g list", command->sge_dma);

		/* Byte swap page tables if necessary */
		sbp2util_cpu_to_be32_buffer(sg_element,
					    (sizeof(struct sbp2_unrestricted_page_table)) *
					    sg_count);
	}
}

static void sbp2_prep_command_orb_no_sg(struct sbp2_command_orb *orb,
					struct sbp2scsi_host_info *hi,
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

	SBP2_DMA_ALLOC("single bulk");

	/*
	 * Handle case where we get a command w/o s/g enabled (but
	 * check for transfers larger than 64K)
	 */
	if (scsi_request_bufflen <= SBP2_MAX_SG_ELEMENT_LENGTH) {

		orb->data_descriptor_lo = command->cmd_dma;
		orb->misc |= ORB_SET_DATA_SIZE(scsi_request_bufflen);

	} else {
		struct sbp2_unrestricted_page_table *sg_element =
			&command->scatter_gather_element[0];
		u32 sg_count, sg_len;
		dma_addr_t sg_addr;

		/*
		 * Need to turn this into page tables, since the
		 * buffer is too large.
		 */
		orb->data_descriptor_lo = command->sge_dma;

		/* Use page tables (s/g) */
		orb->misc |= ORB_SET_PAGE_TABLE_PRESENT(0x1);

		/*
		 * fill out our sbp-2 page tables (and split up
		 * the large buffer)
		 */
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

		/* Number of page table (s/g) elements */
		orb->misc |= ORB_SET_DATA_SIZE(sg_count);

		sbp2util_packet_dump(sg_element,
				     (sizeof(struct sbp2_unrestricted_page_table)) * sg_count,
				     "sbp2 s/g list", command->sge_dma);

		/* Byte swap page tables if necessary */
		sbp2util_cpu_to_be32_buffer(sg_element,
					    (sizeof(struct sbp2_unrestricted_page_table)) *
					     sg_count);
	}
}

/*
 * This function is called to create the actual command orb and s/g list
 * out of the scsi command itself.
 */
static void sbp2_create_command_orb(struct scsi_id_instance_data *scsi_id,
				    struct sbp2_command_info *command,
				    unchar *scsi_cmd,
				    unsigned int scsi_use_sg,
				    unsigned int scsi_request_bufflen,
				    void *scsi_request_buffer,
				    enum dma_data_direction dma_dir)
{
	struct sbp2scsi_host_info *hi = scsi_id->hi;
	struct scatterlist *sgpnt = (struct scatterlist *)scsi_request_buffer;
	struct sbp2_command_orb *command_orb = &command->command_orb;
	u32 orb_direction;

	/*
	 * Set-up our command ORB..
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
	command_orb->misc |= ORB_SET_NOTIFY(1);	/* Notify us when complete */

	if (dma_dir == DMA_NONE)
		orb_direction = ORB_DIRECTION_NO_DATA_TRANSFER;
	else if (dma_dir == DMA_TO_DEVICE && scsi_request_bufflen)
		orb_direction = ORB_DIRECTION_WRITE_TO_MEDIA;
	else if (dma_dir == DMA_FROM_DEVICE && scsi_request_bufflen)
		orb_direction = ORB_DIRECTION_READ_FROM_MEDIA;
	else {
		SBP2_WARN("Falling back to DMA_NONE");
		orb_direction = ORB_DIRECTION_NO_DATA_TRANSFER;
	}

	/* Set-up our pagetable stuff */
	if (orb_direction == ORB_DIRECTION_NO_DATA_TRANSFER) {
		SBP2_DEBUG("No data transfer");
		command_orb->data_descriptor_hi = 0x0;
		command_orb->data_descriptor_lo = 0x0;
		command_orb->misc |= ORB_SET_DIRECTION(1);
	} else if (scsi_use_sg) {
		SBP2_DEBUG("Use scatter/gather");
		sbp2_prep_command_orb_sg(command_orb, hi, command, scsi_use_sg,
					 sgpnt, orb_direction, dma_dir);
	} else {
		SBP2_DEBUG("No scatter/gather");
		sbp2_prep_command_orb_no_sg(command_orb, hi, command, sgpnt,
					    orb_direction, scsi_request_bufflen,
					    scsi_request_buffer, dma_dir);
	}

	/* Byte swap command ORB if necessary */
	sbp2util_cpu_to_be32_buffer(command_orb, sizeof(struct sbp2_command_orb));

	/* Put our scsi command in the command ORB */
	memset(command_orb->cdb, 0, 12);
	memcpy(command_orb->cdb, scsi_cmd, COMMAND_SIZE(*scsi_cmd));
}

/*
 * This function is called in order to begin a regular SBP-2 command.
 */
static int sbp2_link_orb_command(struct scsi_id_instance_data *scsi_id,
				 struct sbp2_command_info *command)
{
	struct sbp2scsi_host_info *hi = scsi_id->hi;
	struct sbp2_command_orb *command_orb = &command->command_orb;
	struct node_entry *ne = scsi_id->ne;
	u64 addr;

	outstanding_orb_incr;
	SBP2_ORB_DEBUG("sending command orb %p, total orbs = %x",
		       command_orb, global_outstanding_command_orbs);

	pci_dma_sync_single_for_device(hi->host->pdev, command->command_orb_dma,
				       sizeof(struct sbp2_command_orb),
				       PCI_DMA_BIDIRECTIONAL);
	pci_dma_sync_single_for_device(hi->host->pdev, command->sge_dma,
				       sizeof(command->scatter_gather_element),
				       PCI_DMA_BIDIRECTIONAL);
	/*
	 * Check to see if there are any previous orbs to use
	 */
	if (scsi_id->last_orb == NULL) {
		quadlet_t data[2];

		/*
		 * Ok, let's write to the target's management agent register
		 */
		addr = scsi_id->sbp2_command_block_agent_addr + SBP2_ORB_POINTER_OFFSET;
		data[0] = ORB_SET_NODE_ID(hi->host->node_id);
		data[1] = command->command_orb_dma;
		sbp2util_cpu_to_be32_buffer(data, 8);

		SBP2_ORB_DEBUG("write command agent, command orb %p", command_orb);

		if (sbp2util_node_write_no_wait(ne, addr, data, 8) < 0) {
			SBP2_ERR("sbp2util_node_write_no_wait failed.\n");
			return -EIO;
		}

		SBP2_ORB_DEBUG("write command agent complete");

		scsi_id->last_orb = command_orb;
		scsi_id->last_orb_dma = command->command_orb_dma;

	} else {
		quadlet_t data;

		/*
		 * We have an orb already sent (maybe or maybe not
		 * processed) that we can append this orb to. So do so,
		 * and ring the doorbell. Have to be very careful
		 * modifying these next orb pointers, as they are accessed
		 * both by the sbp2 device and us.
		 */
		scsi_id->last_orb->next_ORB_lo =
		    cpu_to_be32(command->command_orb_dma);
		/* Tells hardware that this pointer is valid */
		scsi_id->last_orb->next_ORB_hi = 0x0;
		pci_dma_sync_single_for_device(hi->host->pdev,
					       scsi_id->last_orb_dma,
					       sizeof(struct sbp2_command_orb),
					       PCI_DMA_BIDIRECTIONAL);

		/*
		 * Ring the doorbell
		 */
		data = cpu_to_be32(command->command_orb_dma);
		addr = scsi_id->sbp2_command_block_agent_addr + SBP2_DOORBELL_OFFSET;

		SBP2_ORB_DEBUG("ring doorbell, command orb %p", command_orb);

		if (sbp2util_node_write_no_wait(ne, addr, &data, 4) < 0) {
			SBP2_ERR("sbp2util_node_write_no_wait failed");
			return -EIO;
		}

		scsi_id->last_orb = command_orb;
		scsi_id->last_orb_dma = command->command_orb_dma;

	}
	return 0;
}

/*
 * This function is called in order to begin a regular SBP-2 command.
 */
static int sbp2_send_command(struct scsi_id_instance_data *scsi_id,
			     struct scsi_cmnd *SCpnt,
			     void (*done)(struct scsi_cmnd *))
{
	unchar *cmd = (unchar *) SCpnt->cmnd;
	unsigned int request_bufflen = SCpnt->request_bufflen;
	struct sbp2_command_info *command;

	SBP2_DEBUG("sbp2_send_command");
#if (CONFIG_IEEE1394_SBP2_DEBUG >= 2) || defined(CONFIG_IEEE1394_SBP2_PACKET_DUMP)
	printk("[scsi command]\n   ");
	scsi_print_command(SCpnt);
#endif
	SBP2_DEBUG("SCSI transfer size = %x", request_bufflen);
	SBP2_DEBUG("SCSI s/g elements = %x", (unsigned int)SCpnt->use_sg);

	/*
	 * Allocate a command orb and s/g structure
	 */
	command = sbp2util_allocate_command_orb(scsi_id, SCpnt, done);
	if (!command) {
		return -EIO;
	}

	/*
	 * The scsi stack sends down a request_bufflen which does not match the
	 * length field in the scsi cdb. This causes some sbp2 devices to
	 * reject this inquiry command. Fix the request_bufflen.
	 */
	if (*cmd == INQUIRY) {
		if (force_inquiry_hack || scsi_id->workarounds & SBP2_BREAKAGE_INQUIRY_HACK)
			request_bufflen = cmd[4] = 0x24;
		else
			request_bufflen = cmd[4];
	}

	/*
	 * Now actually fill in the comamnd orb and sbp2 s/g list
	 */
	sbp2_create_command_orb(scsi_id, command, cmd, SCpnt->use_sg,
				request_bufflen, SCpnt->request_buffer,
				SCpnt->sc_data_direction);

	sbp2util_packet_dump(&command->command_orb, sizeof(struct sbp2_command_orb),
			     "sbp2 command orb", command->command_orb_dma);

	/*
	 * Initialize status fifo
	 */
	memset(&scsi_id->status_block, 0, sizeof(struct sbp2_status_block));

	/*
	 * Link up the orb, and ring the doorbell if needed
	 */
	sbp2_link_orb_command(scsi_id, command);

	return 0;
}

/*
 * Translates SBP-2 status into SCSI sense data for check conditions
 */
static unsigned int sbp2_status_to_sense_data(unchar *sbp2_status, unchar *sense_data)
{
	SBP2_DEBUG("sbp2_status_to_sense_data");

	/*
	 * Ok, it's pretty ugly...   ;-)
	 */
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

	return sbp2_status[8] & 0x3f;	/* return scsi status */
}

/*
 * This function is called after a command is completed, in order to do any necessary SBP-2
 * response data translations for the SCSI stack
 */
static void sbp2_check_sbp2_response(struct scsi_id_instance_data *scsi_id,
				     struct scsi_cmnd *SCpnt)
{
	u8 *scsi_buf = SCpnt->request_buffer;

	SBP2_DEBUG("sbp2_check_sbp2_response");

	switch (SCpnt->cmnd[0]) {

	case INQUIRY:
		/*
		 * Make sure data length is ok. Minimum length is 36 bytes
		 */
		if (scsi_buf[4] == 0) {
			scsi_buf[4] = 36 - 5;
		}

		/*
		 * Fix ansi revision and response data format
		 */
		scsi_buf[2] |= 2;
		scsi_buf[3] = (scsi_buf[3] & 0xf0) | 2;

		break;

	default:
		break;
	}
	return;
}

/*
 * This function deals with status writes from the SBP-2 device
 */
static int sbp2_handle_status_write(struct hpsb_host *host, int nodeid, int destid,
				    quadlet_t *data, u64 addr, size_t length, u16 fl)
{
	struct sbp2scsi_host_info *hi;
	struct scsi_id_instance_data *scsi_id = NULL, *scsi_id_tmp;
	u32 id;
	struct scsi_cmnd *SCpnt = NULL;
	u32 scsi_status = SBP2_SCSI_STATUS_GOOD;
	struct sbp2_command_info *command;
	unsigned long flags;

	SBP2_DEBUG("sbp2_handle_status_write");

	sbp2util_packet_dump(data, length, "sbp2 status write by device", (u32)addr);

	if (!host) {
		SBP2_ERR("host is NULL - this is bad!");
		return RCODE_ADDRESS_ERROR;
	}

	hi = hpsb_get_hostinfo(&sbp2_highlevel, host);

	if (!hi) {
		SBP2_ERR("host info is NULL - this is bad!");
		return RCODE_ADDRESS_ERROR;
	}

	/*
	 * Find our scsi_id structure by looking at the status fifo address written to by
	 * the sbp2 device.
	 */
	id = SBP2_STATUS_FIFO_OFFSET_TO_ENTRY((u32)(addr - SBP2_STATUS_FIFO_ADDRESS));
	list_for_each_entry(scsi_id_tmp, &hi->scsi_ids, scsi_list) {
		if (scsi_id_tmp->ne->nodeid == nodeid && scsi_id_tmp->ud->id == id) {
			scsi_id = scsi_id_tmp;
			break;
		}
	}

	if (!scsi_id) {
		SBP2_ERR("scsi_id is NULL - device is gone?");
		return RCODE_ADDRESS_ERROR;
	}

	/*
	 * Put response into scsi_id status fifo...
	 */
	memcpy(&scsi_id->status_block, data, length);

	/*
	 * Byte swap first two quadlets (8 bytes) of status for processing
	 */
	sbp2util_be32_to_cpu_buffer(&scsi_id->status_block, 8);

	/*
	 * Handle command ORB status here if necessary. First, need to match status with command.
	 */
	command = sbp2util_find_command_for_orb(scsi_id, scsi_id->status_block.ORB_offset_lo);
	if (command) {

		SBP2_DEBUG("Found status for command ORB");
		pci_dma_sync_single_for_cpu(hi->host->pdev, command->command_orb_dma,
					    sizeof(struct sbp2_command_orb),
					    PCI_DMA_BIDIRECTIONAL);
		pci_dma_sync_single_for_cpu(hi->host->pdev, command->sge_dma,
					    sizeof(command->scatter_gather_element),
					    PCI_DMA_BIDIRECTIONAL);

		SBP2_ORB_DEBUG("matched command orb %p", &command->command_orb);
		outstanding_orb_decr;

		/*
		 * Matched status with command, now grab scsi command pointers and check status
		 */
		SCpnt = command->Current_SCpnt;
		sbp2util_mark_command_completed(scsi_id, command);

		if (SCpnt) {

			/*
			 * See if the target stored any scsi status information
			 */
			if (STATUS_GET_LENGTH(scsi_id->status_block.ORB_offset_hi_misc) > 1) {
				/*
				 * Translate SBP-2 status to SCSI sense data
				 */
				SBP2_DEBUG("CHECK CONDITION");
				scsi_status = sbp2_status_to_sense_data((unchar *)&scsi_id->status_block, SCpnt->sense_buffer);
			}

			/*
			 * Check to see if the dead bit is set. If so, we'll have to initiate
			 * a fetch agent reset.
			 */
			if (STATUS_GET_DEAD_BIT(scsi_id->status_block.ORB_offset_hi_misc)) {

				/*
				 * Initiate a fetch agent reset.
				 */
				SBP2_DEBUG("Dead bit set - initiating fetch agent reset");
                                sbp2_agent_reset(scsi_id, 0);
			}

			SBP2_ORB_DEBUG("completing command orb %p", &command->command_orb);
		}

		/*
		 * Check here to see if there are no commands in-use. If there are none, we can
		 * null out last orb so that next time around we write directly to the orb pointer...
		 * Quick start saves one 1394 bus transaction.
		 */
		spin_lock_irqsave(&scsi_id->sbp2_command_orb_lock, flags);
		if (list_empty(&scsi_id->sbp2_command_orb_inuse)) {
			scsi_id->last_orb = NULL;
		}
		spin_unlock_irqrestore(&scsi_id->sbp2_command_orb_lock, flags);

	} else {

		/*
		 * It's probably a login/logout/reconnect status.
		 */
		if ((scsi_id->login_orb_dma == scsi_id->status_block.ORB_offset_lo) ||
		    (scsi_id->query_logins_orb_dma == scsi_id->status_block.ORB_offset_lo) ||
		    (scsi_id->reconnect_orb_dma == scsi_id->status_block.ORB_offset_lo) ||
		    (scsi_id->logout_orb_dma == scsi_id->status_block.ORB_offset_lo)) {
			atomic_set(&scsi_id->sbp2_login_complete, 1);
		}
	}

	if (SCpnt) {

		/* Complete the SCSI command. */
		SBP2_DEBUG("Completing SCSI command");
		sbp2scsi_complete_command(scsi_id, scsi_status, SCpnt,
					  command->Current_done);
		SBP2_ORB_DEBUG("command orb completed");
	}

	return RCODE_COMPLETE;
}

/**************************************
 * SCSI interface related section
 **************************************/

/*
 * This routine is the main request entry routine for doing I/O. It is
 * called from the scsi stack directly.
 */
static int sbp2scsi_queuecommand(struct scsi_cmnd *SCpnt,
				 void (*done)(struct scsi_cmnd *))
{
	struct scsi_id_instance_data *scsi_id =
		(struct scsi_id_instance_data *)SCpnt->device->host->hostdata[0];
	struct sbp2scsi_host_info *hi;
	int result = DID_NO_CONNECT << 16;

	SBP2_DEBUG("sbp2scsi_queuecommand");

	if (!sbp2util_node_is_available(scsi_id))
		goto done;

	hi = scsi_id->hi;

	if (!hi) {
		SBP2_ERR("sbp2scsi_host_info is NULL - this is bad!");
		goto done;
	}

	/*
	 * Until we handle multiple luns, just return selection time-out
	 * to any IO directed at non-zero LUNs
	 */
	if (SCpnt->device->lun)
		goto done;

	/*
	 * Check for request sense command, and handle it here
	 * (autorequest sense)
	 */
	if (SCpnt->cmnd[0] == REQUEST_SENSE) {
		SBP2_DEBUG("REQUEST_SENSE");
		memcpy(SCpnt->request_buffer, SCpnt->sense_buffer, SCpnt->request_bufflen);
		memset(SCpnt->sense_buffer, 0, sizeof(SCpnt->sense_buffer));
		sbp2scsi_complete_command(scsi_id, SBP2_SCSI_STATUS_GOOD, SCpnt, done);
		return 0;
	}

	/*
	 * Check to see if we are in the middle of a bus reset.
	 */
	if (!hpsb_node_entry_valid(scsi_id->ne)) {
		SBP2_ERR("Bus reset in progress - rejecting command");
		result = DID_BUS_BUSY << 16;
		goto done;
	}

	/*
	 * Bidirectional commands are not yet implemented,
	 * and unknown transfer direction not handled.
	 */
	if (SCpnt->sc_data_direction == DMA_BIDIRECTIONAL) {
		SBP2_ERR("Cannot handle DMA_BIDIRECTIONAL - rejecting command");
		result = DID_ERROR << 16;
		goto done;
	}

	/*
	 * Try and send our SCSI command
	 */
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

/*
 * This function is called in order to complete all outstanding SBP-2
 * commands (in case of resets, etc.).
 */
static void sbp2scsi_complete_all_commands(struct scsi_id_instance_data *scsi_id,
					   u32 status)
{
	struct sbp2scsi_host_info *hi = scsi_id->hi;
	struct list_head *lh;
	struct sbp2_command_info *command;
	unsigned long flags;

	SBP2_DEBUG("sbp2scsi_complete_all_commands");

	spin_lock_irqsave(&scsi_id->sbp2_command_orb_lock, flags);
	while (!list_empty(&scsi_id->sbp2_command_orb_inuse)) {
		SBP2_DEBUG("Found pending command to complete");
		lh = scsi_id->sbp2_command_orb_inuse.next;
		command = list_entry(lh, struct sbp2_command_info, list);
		pci_dma_sync_single_for_cpu(hi->host->pdev, command->command_orb_dma,
					    sizeof(struct sbp2_command_orb),
					    PCI_DMA_BIDIRECTIONAL);
		pci_dma_sync_single_for_cpu(hi->host->pdev, command->sge_dma,
					    sizeof(command->scatter_gather_element),
					    PCI_DMA_BIDIRECTIONAL);
		sbp2util_mark_command_completed(scsi_id, command);
		if (command->Current_SCpnt) {
			command->Current_SCpnt->result = status << 16;
			command->Current_done(command->Current_SCpnt);
		}
	}
	spin_unlock_irqrestore(&scsi_id->sbp2_command_orb_lock, flags);

	return;
}

/*
 * This function is called in order to complete a regular SBP-2 command.
 *
 * This can be called in interrupt context.
 */
static void sbp2scsi_complete_command(struct scsi_id_instance_data *scsi_id,
				      u32 scsi_status, struct scsi_cmnd *SCpnt,
				      void (*done)(struct scsi_cmnd *))
{
	SBP2_DEBUG("sbp2scsi_complete_command");

	/*
	 * Sanity
	 */
	if (!SCpnt) {
		SBP2_ERR("SCpnt is NULL");
		return;
	}

	/*
	 * If a bus reset is in progress and there was an error, don't
	 * complete the command, just let it get retried at the end of the
	 * bus reset.
	 */
	if (!hpsb_node_entry_valid(scsi_id->ne)
	    && (scsi_status != SBP2_SCSI_STATUS_GOOD)) {
		SBP2_ERR("Bus reset in progress - retry command later");
		return;
	}

	/*
	 * Switch on scsi status
	 */
	switch (scsi_status) {
	case SBP2_SCSI_STATUS_GOOD:
		SCpnt->result = DID_OK;
		break;

	case SBP2_SCSI_STATUS_BUSY:
		SBP2_ERR("SBP2_SCSI_STATUS_BUSY");
		SCpnt->result = DID_BUS_BUSY << 16;
		break;

	case SBP2_SCSI_STATUS_CHECK_CONDITION:
		SBP2_DEBUG("SBP2_SCSI_STATUS_CHECK_CONDITION");
		SCpnt->result = CHECK_CONDITION << 1;

		/*
		 * Debug stuff
		 */
#if CONFIG_IEEE1394_SBP2_DEBUG >= 1
		scsi_print_command(SCpnt);
		scsi_print_sense("bh", SCpnt);
#endif

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

	/*
	 * Take care of any sbp2 response data mucking here (RBC stuff, etc.)
	 */
	if (SCpnt->result == DID_OK) {
		sbp2_check_sbp2_response(scsi_id, SCpnt);
	}

	/*
	 * If a bus reset is in progress and there was an error, complete
	 * the command as busy so that it will get retried.
	 */
	if (!hpsb_node_entry_valid(scsi_id->ne)
	    && (scsi_status != SBP2_SCSI_STATUS_GOOD)) {
		SBP2_ERR("Completing command with busy (bus reset)");
		SCpnt->result = DID_BUS_BUSY << 16;
	}

	/*
	 * If a unit attention occurs, return busy status so it gets
	 * retried... it could have happened because of a 1394 bus reset
	 * or hot-plug...
	 */
#if 0
	if ((scsi_status == SBP2_SCSI_STATUS_CHECK_CONDITION) &&
	    (SCpnt->sense_buffer[2] == UNIT_ATTENTION)) {
		SBP2_DEBUG("UNIT ATTENTION - return busy");
		SCpnt->result = DID_BUS_BUSY << 16;
	}
#endif

	/*
	 * Tell scsi stack that we're done with this command
	 */
	done(SCpnt);
}

static int sbp2scsi_slave_alloc(struct scsi_device *sdev)
{
	((struct scsi_id_instance_data *)sdev->host->hostdata[0])->sdev = sdev;
	return 0;
}

static int sbp2scsi_slave_configure(struct scsi_device *sdev)
{
	blk_queue_dma_alignment(sdev->request_queue, (512 - 1));
	sdev->use_10_for_rw = 1;
	sdev->use_10_for_ms = 1;
	return 0;
}

static void sbp2scsi_slave_destroy(struct scsi_device *sdev)
{
	((struct scsi_id_instance_data *)sdev->host->hostdata[0])->sdev = NULL;
	return;
}

/*
 * Called by scsi stack when something has really gone wrong.  Usually
 * called when a command has timed-out for some reason.
 */
static int sbp2scsi_abort(struct scsi_cmnd *SCpnt)
{
	struct scsi_id_instance_data *scsi_id =
		(struct scsi_id_instance_data *)SCpnt->device->host->hostdata[0];
	struct sbp2scsi_host_info *hi = scsi_id->hi;
	struct sbp2_command_info *command;

	SBP2_ERR("aborting sbp2 command");
	scsi_print_command(SCpnt);

	if (sbp2util_node_is_available(scsi_id)) {

		/*
		 * Right now, just return any matching command structures
		 * to the free pool.
		 */
		command = sbp2util_find_command_for_SCpnt(scsi_id, SCpnt);
		if (command) {
			SBP2_DEBUG("Found command to abort");
			pci_dma_sync_single_for_cpu(hi->host->pdev,
						    command->command_orb_dma,
						    sizeof(struct sbp2_command_orb),
						    PCI_DMA_BIDIRECTIONAL);
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

		/*
		 * Initiate a fetch agent reset.
		 */
		sbp2_agent_reset(scsi_id, 0);
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

	SBP2_ERR("reset requested");

	if (sbp2util_node_is_available(scsi_id)) {
		SBP2_ERR("Generating sbp2 fetch agent reset");
		sbp2_agent_reset(scsi_id, 0);
	}

	return SUCCESS;
}

static ssize_t sbp2_sysfs_ieee1394_id_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct scsi_device *sdev;
	struct scsi_id_instance_data *scsi_id;
	int lun;

	if (!(sdev = to_scsi_device(dev)))
		return 0;

	if (!(scsi_id = (struct scsi_id_instance_data *)sdev->host->hostdata[0]))
		return 0;

	lun = ORB_SET_LUN(scsi_id->sbp2_lun);

	return sprintf(buf, "%016Lx:%d:%d\n", (unsigned long long)scsi_id->ne->guid,
		       scsi_id->ud->id, lun);
}
static DEVICE_ATTR(ieee1394_id, S_IRUGO, sbp2_sysfs_ieee1394_id_show, NULL);

static struct device_attribute *sbp2_sysfs_sdev_attrs[] = {
	&dev_attr_ieee1394_id,
	NULL
};

MODULE_AUTHOR("Ben Collins <bcollins@debian.org>");
MODULE_DESCRIPTION("IEEE-1394 SBP-2 protocol driver");
MODULE_SUPPORTED_DEVICE(SBP2_DEVICE_NAME);
MODULE_LICENSE("GPL");

/* SCSI host template */
static struct scsi_host_template scsi_driver_template = {
	.module =			THIS_MODULE,
	.name =				"SBP-2 IEEE-1394",
	.proc_name =			SBP2_DEVICE_NAME,
	.queuecommand =			sbp2scsi_queuecommand,
	.eh_abort_handler =		sbp2scsi_abort,
	.eh_device_reset_handler =	sbp2scsi_reset,
	.slave_alloc =			sbp2scsi_slave_alloc,
	.slave_configure =		sbp2scsi_slave_configure,
	.slave_destroy =		sbp2scsi_slave_destroy,
	.this_id =			-1,
	.sg_tablesize =			SG_ALL,
	.use_clustering =		ENABLE_CLUSTERING,
	.cmd_per_lun =			SBP2_MAX_CMDS,
	.can_queue = 			SBP2_MAX_CMDS,
	.emulated =			1,
	.sdev_attrs =			sbp2_sysfs_sdev_attrs,
};

static int sbp2_module_init(void)
{
	int ret;

	SBP2_DEBUG("sbp2_module_init");

	/* Module load debug option to force one command at a time (serializing I/O) */
	if (serialize_io) {
		SBP2_INFO("Driver forced to serialize I/O (serialize_io=1)");
		SBP2_INFO("Try serialize_io=0 for better performance");
		scsi_driver_template.can_queue = 1;
		scsi_driver_template.cmd_per_lun = 1;
	}

	/* Set max sectors (module load option). Default is 255 sectors. */
	scsi_driver_template.max_sectors = max_sectors;

	/* Register our high level driver with 1394 stack */
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
	SBP2_DEBUG("sbp2_module_exit");

	hpsb_unregister_protocol(&sbp2_driver);

	hpsb_unregister_highlevel(&sbp2_highlevel);
}

module_init(sbp2_module_init);
module_exit(sbp2_module_exit);
