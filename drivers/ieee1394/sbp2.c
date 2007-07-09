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
 * You may access any attached SBP-2 (usually storage devices) as regular
 * SCSI devices. E.g. mount /dev/sda1, fdisk, mkfs, etc..
 *
 * See http://www.t10.org/drafts.htm#sbp2 for the final draft of the SBP-2
 * specification and for where to purchase the official standard.
 *
 * TODO:
 *   - look into possible improvements of the SCSI error handlers
 *   - handle Unit_Characteristics.mgt_ORB_timeout and .ORB_size
 *   - handle Logical_Unit_Number.ordered
 *   - handle src == 1 in status blocks
 *   - reimplement the DMA mapping in absence of physical DMA so that
 *     bus_to_virt is no longer required
 *   - debug the handling of absent physical DMA
 *   - replace CONFIG_IEEE1394_SBP2_PHYS_DMA by automatic detection
 *     (this is easy but depends on the previous two TODO items)
 *   - make the parameter serialize_io configurable per device
 *   - move all requests to fetch agent registers into non-atomic context,
 *     replace all usages of sbp2util_node_write_no_wait by true transactions
 * Grep for inline FIXME comments below.
 */

#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

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
 * Set serialize_io to 0 or N to use dynamically appended lists of command ORBs.
 * This is and always has been buggy in multiple subtle ways. See above TODOs.
 */
static int sbp2_serialize_io = 1;
module_param_named(serialize_io, sbp2_serialize_io, bool, 0444);
MODULE_PARM_DESC(serialize_io, "Serialize requests coming from SCSI drivers "
		 "(default = Y, faster but buggy = N)");

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
module_param_named(exclusive_login, sbp2_exclusive_login, bool, 0644);
MODULE_PARM_DESC(exclusive_login, "Exclusive login to sbp2 device "
		 "(default = Y, use N for concurrent initiators)");

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

/*
 * This influences the format of the sysfs attribute
 * /sys/bus/scsi/devices/.../ieee1394_id.
 *
 * The default format is like in older kernels:  %016Lx:%d:%d
 * It contains the target's EUI-64, a number given to the logical unit by
 * the ieee1394 driver's nodemgr (starting at 0), and the LUN.
 *
 * The long format is:  %016Lx:%06x:%04x
 * It contains the target's EUI-64, the unit directory's directory_ID as per
 * IEEE 1212 clause 7.7.19, and the LUN.  This format comes closest to the
 * format of SBP(-3) target port and logical unit identifier as per SAM (SCSI
 * Architecture Model) rev.2 to 4 annex A.  Therefore and because it is
 * independent of the implementation of the ieee1394 nodemgr, the longer format
 * is recommended for future use.
 */
static int sbp2_long_sysfs_ieee1394_id;
module_param_named(long_ieee1394_id, sbp2_long_sysfs_ieee1394_id, bool, 0644);
MODULE_PARM_DESC(long_ieee1394_id, "8+3+2 bytes format of ieee1394_id in sysfs "
		 "(default = backwards-compatible = N, SAM-conforming = Y)");


#define SBP2_INFO(fmt, args...)	HPSB_INFO("sbp2: "fmt, ## args)
#define SBP2_ERR(fmt, args...)	HPSB_ERR("sbp2: "fmt, ## args)

/*
 * Globals
 */
static void sbp2scsi_complete_all_commands(struct sbp2_lu *, u32);
static void sbp2scsi_complete_command(struct sbp2_lu *, u32, struct scsi_cmnd *,
				      void (*)(struct scsi_cmnd *));
static struct sbp2_lu *sbp2_alloc_device(struct unit_directory *);
static int sbp2_start_device(struct sbp2_lu *);
static void sbp2_remove_device(struct sbp2_lu *);
static int sbp2_login_device(struct sbp2_lu *);
static int sbp2_reconnect_device(struct sbp2_lu *);
static int sbp2_logout_device(struct sbp2_lu *);
static void sbp2_host_reset(struct hpsb_host *);
static int sbp2_handle_status_write(struct hpsb_host *, int, int, quadlet_t *,
				    u64, size_t, u16);
static int sbp2_agent_reset(struct sbp2_lu *, int);
static void sbp2_parse_unit_directory(struct sbp2_lu *,
				      struct unit_directory *);
static int sbp2_set_busy_timeout(struct sbp2_lu *);
static int sbp2_max_speed_and_size(struct sbp2_lu *);


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
	.name		= SBP2_DEVICE_NAME,
	.id_table	= sbp2_id_table,
	.update		= sbp2_update,
	.driver		= {
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
	.sdev_attrs		 = sbp2_sysfs_sdev_attrs,
};

/* for match-all entries in sbp2_workarounds_table */
#define SBP2_ROM_VALUE_WILDCARD 0x1000000

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
		.model_id		= SBP2_ROM_VALUE_WILDCARD,
		.workarounds		= SBP2_WORKAROUND_INQUIRY_36,
	},
	/* Symbios bridge */ {
		.firmware_revision	= 0xa0b800,
		.model_id		= SBP2_ROM_VALUE_WILDCARD,
		.workarounds		= SBP2_WORKAROUND_128K_MAX_TRANS,
	},
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
}

/*
 * Converts a buffer from cpu to be32 byte ordering. Length is in bytes.
 */
static inline void sbp2util_cpu_to_be32_buffer(void *buffer, int length)
{
	u32 *temp = buffer;

	for (length = (length >> 2); length--; )
		temp[length] = cpu_to_be32(temp[length]);
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
static int sbp2util_access_timeout(struct sbp2_lu *lu, int timeout)
{
	long leftover;

	leftover = wait_event_interruptible_timeout(
			sbp2_access_wq, lu->access_complete, timeout);
	lu->access_complete = 0;
	return leftover <= 0;
}

static void sbp2_free_packet(void *packet)
{
	hpsb_free_tlabel(packet);
	hpsb_free_packet(packet);
}

/*
 * This is much like hpsb_node_write(), except it ignores the response
 * subaction and returns immediately. Can be used from atomic context.
 */
static int sbp2util_node_write_no_wait(struct node_entry *ne, u64 addr,
				       quadlet_t *buf, size_t len)
{
	struct hpsb_packet *packet;

	packet = hpsb_make_writepacket(ne->host, ne->nodeid, addr, buf, len);
	if (!packet)
		return -ENOMEM;

	hpsb_set_packet_complete_task(packet, sbp2_free_packet, packet);
	hpsb_node_fill_packet(ne, packet);
	if (hpsb_send_packet(packet) < 0) {
		sbp2_free_packet(packet);
		return -EIO;
	}
	return 0;
}

static void sbp2util_notify_fetch_agent(struct sbp2_lu *lu, u64 offset,
					quadlet_t *data, size_t len)
{
	/* There is a small window after a bus reset within which the node
	 * entry's generation is current but the reconnect wasn't completed. */
	if (unlikely(atomic_read(&lu->state) == SBP2LU_STATE_IN_RESET))
		return;

	if (hpsb_node_write(lu->ne, lu->command_block_agent_addr + offset,
			    data, len))
		SBP2_ERR("sbp2util_notify_fetch_agent failed.");

	/* Now accept new SCSI commands, unless a bus reset happended during
	 * hpsb_node_write. */
	if (likely(atomic_read(&lu->state) != SBP2LU_STATE_IN_RESET))
		scsi_unblock_requests(lu->shost);
}

static void sbp2util_write_orb_pointer(struct work_struct *work)
{
	struct sbp2_lu *lu = container_of(work, struct sbp2_lu, protocol_work);
	quadlet_t data[2];

	data[0] = ORB_SET_NODE_ID(lu->hi->host->node_id);
	data[1] = lu->last_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);
	sbp2util_notify_fetch_agent(lu, SBP2_ORB_POINTER_OFFSET, data, 8);
}

static void sbp2util_write_doorbell(struct work_struct *work)
{
	struct sbp2_lu *lu = container_of(work, struct sbp2_lu, protocol_work);

	sbp2util_notify_fetch_agent(lu, SBP2_DOORBELL_OFFSET, NULL, 4);
}

static int sbp2util_create_command_orb_pool(struct sbp2_lu *lu)
{
	struct sbp2_fwhost_info *hi = lu->hi;
	struct sbp2_command_info *cmd;
	int i, orbs = sbp2_serialize_io ? 2 : SBP2_MAX_CMDS;

	for (i = 0; i < orbs; i++) {
		cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
		if (!cmd)
			return -ENOMEM;
		cmd->command_orb_dma = dma_map_single(hi->host->device.parent,
						&cmd->command_orb,
						sizeof(struct sbp2_command_orb),
						DMA_TO_DEVICE);
		cmd->sge_dma = dma_map_single(hi->host->device.parent,
					&cmd->scatter_gather_element,
					sizeof(cmd->scatter_gather_element),
					DMA_TO_DEVICE);
		INIT_LIST_HEAD(&cmd->list);
		list_add_tail(&cmd->list, &lu->cmd_orb_completed);
	}
	return 0;
}

static void sbp2util_remove_command_orb_pool(struct sbp2_lu *lu)
{
	struct hpsb_host *host = lu->hi->host;
	struct list_head *lh, *next;
	struct sbp2_command_info *cmd;
	unsigned long flags;

	spin_lock_irqsave(&lu->cmd_orb_lock, flags);
	if (!list_empty(&lu->cmd_orb_completed))
		list_for_each_safe(lh, next, &lu->cmd_orb_completed) {
			cmd = list_entry(lh, struct sbp2_command_info, list);
			dma_unmap_single(host->device.parent,
					 cmd->command_orb_dma,
					 sizeof(struct sbp2_command_orb),
					 DMA_TO_DEVICE);
			dma_unmap_single(host->device.parent, cmd->sge_dma,
					 sizeof(cmd->scatter_gather_element),
					 DMA_TO_DEVICE);
			kfree(cmd);
		}
	spin_unlock_irqrestore(&lu->cmd_orb_lock, flags);
	return;
}

/*
 * Finds the sbp2_command for a given outstanding command ORB.
 * Only looks at the in-use list.
 */
static struct sbp2_command_info *sbp2util_find_command_for_orb(
				struct sbp2_lu *lu, dma_addr_t orb)
{
	struct sbp2_command_info *cmd;
	unsigned long flags;

	spin_lock_irqsave(&lu->cmd_orb_lock, flags);
	if (!list_empty(&lu->cmd_orb_inuse))
		list_for_each_entry(cmd, &lu->cmd_orb_inuse, list)
			if (cmd->command_orb_dma == orb) {
				spin_unlock_irqrestore(
						&lu->cmd_orb_lock, flags);
				return cmd;
			}
	spin_unlock_irqrestore(&lu->cmd_orb_lock, flags);
	return NULL;
}

/*
 * Finds the sbp2_command for a given outstanding SCpnt.
 * Only looks at the in-use list.
 * Must be called with lu->cmd_orb_lock held.
 */
static struct sbp2_command_info *sbp2util_find_command_for_SCpnt(
				struct sbp2_lu *lu, void *SCpnt)
{
	struct sbp2_command_info *cmd;

	if (!list_empty(&lu->cmd_orb_inuse))
		list_for_each_entry(cmd, &lu->cmd_orb_inuse, list)
			if (cmd->Current_SCpnt == SCpnt)
				return cmd;
	return NULL;
}

static struct sbp2_command_info *sbp2util_allocate_command_orb(
				struct sbp2_lu *lu,
				struct scsi_cmnd *Current_SCpnt,
				void (*Current_done)(struct scsi_cmnd *))
{
	struct list_head *lh;
	struct sbp2_command_info *cmd = NULL;
	unsigned long flags;

	spin_lock_irqsave(&lu->cmd_orb_lock, flags);
	if (!list_empty(&lu->cmd_orb_completed)) {
		lh = lu->cmd_orb_completed.next;
		list_del(lh);
		cmd = list_entry(lh, struct sbp2_command_info, list);
		cmd->Current_done = Current_done;
		cmd->Current_SCpnt = Current_SCpnt;
		list_add_tail(&cmd->list, &lu->cmd_orb_inuse);
	} else
		SBP2_ERR("%s: no orbs available", __FUNCTION__);
	spin_unlock_irqrestore(&lu->cmd_orb_lock, flags);
	return cmd;
}

/*
 * Unmaps the DMAs of a command and moves the command to the completed ORB list.
 * Must be called with lu->cmd_orb_lock held.
 */
static void sbp2util_mark_command_completed(struct sbp2_lu *lu,
					    struct sbp2_command_info *cmd)
{
	struct hpsb_host *host = lu->ud->ne->host;

	if (cmd->cmd_dma) {
		if (cmd->dma_type == CMD_DMA_SINGLE)
			dma_unmap_single(host->device.parent, cmd->cmd_dma,
					 cmd->dma_size, cmd->dma_dir);
		else if (cmd->dma_type == CMD_DMA_PAGE)
			dma_unmap_page(host->device.parent, cmd->cmd_dma,
				       cmd->dma_size, cmd->dma_dir);
		/* XXX: Check for CMD_DMA_NONE bug */
		cmd->dma_type = CMD_DMA_NONE;
		cmd->cmd_dma = 0;
	}
	if (cmd->sge_buffer) {
		dma_unmap_sg(host->device.parent, cmd->sge_buffer,
			     cmd->dma_size, cmd->dma_dir);
		cmd->sge_buffer = NULL;
	}
	list_move_tail(&cmd->list, &lu->cmd_orb_completed);
}

/*
 * Is lu valid? Is the 1394 node still present?
 */
static inline int sbp2util_node_is_available(struct sbp2_lu *lu)
{
	return lu && lu->ne && !lu->ne->in_limbo;
}

/*********************************************
 * IEEE-1394 core driver stack related section
 *********************************************/

static int sbp2_probe(struct device *dev)
{
	struct unit_directory *ud;
	struct sbp2_lu *lu;

	ud = container_of(dev, struct unit_directory, device);

	/* Don't probe UD's that have the LUN flag. We'll probe the LUN(s)
	 * instead. */
	if (ud->flags & UNIT_DIRECTORY_HAS_LUN_DIRECTORY)
		return -ENODEV;

	lu = sbp2_alloc_device(ud);
	if (!lu)
		return -ENOMEM;

	sbp2_parse_unit_directory(lu, ud);
	return sbp2_start_device(lu);
}

static int sbp2_remove(struct device *dev)
{
	struct unit_directory *ud;
	struct sbp2_lu *lu;
	struct scsi_device *sdev;

	ud = container_of(dev, struct unit_directory, device);
	lu = ud->device.driver_data;
	if (!lu)
		return 0;

	if (lu->shost) {
		/* Get rid of enqueued commands if there is no chance to
		 * send them. */
		if (!sbp2util_node_is_available(lu))
			sbp2scsi_complete_all_commands(lu, DID_NO_CONNECT);
		/* scsi_remove_device() may trigger shutdown functions of SCSI
		 * highlevel drivers which would deadlock if blocked. */
		atomic_set(&lu->state, SBP2LU_STATE_IN_SHUTDOWN);
		scsi_unblock_requests(lu->shost);
	}
	sdev = lu->sdev;
	if (sdev) {
		lu->sdev = NULL;
		scsi_remove_device(sdev);
	}

	sbp2_logout_device(lu);
	sbp2_remove_device(lu);

	return 0;
}

static int sbp2_update(struct unit_directory *ud)
{
	struct sbp2_lu *lu = ud->device.driver_data;

	if (sbp2_reconnect_device(lu)) {
		/* Reconnect has failed. Perhaps we didn't reconnect fast
		 * enough. Try a regular login, but first log out just in
		 * case of any weirdness. */
		sbp2_logout_device(lu);

		if (sbp2_login_device(lu)) {
			/* Login failed too, just fail, and the backend
			 * will call our sbp2_remove for us */
			SBP2_ERR("Failed to reconnect to sbp2 device!");
			return -EBUSY;
		}
	}

	sbp2_set_busy_timeout(lu);
	sbp2_agent_reset(lu, 1);
	sbp2_max_speed_and_size(lu);

	/* Complete any pending commands with busy (so they get retried)
	 * and remove them from our queue. */
	sbp2scsi_complete_all_commands(lu, DID_BUS_BUSY);

	/* Accept new commands unless there was another bus reset in the
	 * meantime. */
	if (hpsb_node_entry_valid(lu->ne)) {
		atomic_set(&lu->state, SBP2LU_STATE_RUNNING);
		scsi_unblock_requests(lu->shost);
	}
	return 0;
}

static struct sbp2_lu *sbp2_alloc_device(struct unit_directory *ud)
{
	struct sbp2_fwhost_info *hi;
	struct Scsi_Host *shost = NULL;
	struct sbp2_lu *lu = NULL;

	lu = kzalloc(sizeof(*lu), GFP_KERNEL);
	if (!lu) {
		SBP2_ERR("failed to create lu");
		goto failed_alloc;
	}

	lu->ne = ud->ne;
	lu->ud = ud;
	lu->speed_code = IEEE1394_SPEED_100;
	lu->max_payload_size = sbp2_speedto_max_payload[IEEE1394_SPEED_100];
	lu->status_fifo_addr = CSR1212_INVALID_ADDR_SPACE;
	INIT_LIST_HEAD(&lu->cmd_orb_inuse);
	INIT_LIST_HEAD(&lu->cmd_orb_completed);
	INIT_LIST_HEAD(&lu->lu_list);
	spin_lock_init(&lu->cmd_orb_lock);
	atomic_set(&lu->state, SBP2LU_STATE_RUNNING);
	INIT_WORK(&lu->protocol_work, NULL);

	ud->device.driver_data = lu;

	hi = hpsb_get_hostinfo(&sbp2_highlevel, ud->ne->host);
	if (!hi) {
		hi = hpsb_create_hostinfo(&sbp2_highlevel, ud->ne->host,
					  sizeof(*hi));
		if (!hi) {
			SBP2_ERR("failed to allocate hostinfo");
			goto failed_alloc;
		}
		hi->host = ud->ne->host;
		INIT_LIST_HEAD(&hi->logical_units);

#ifdef CONFIG_IEEE1394_SBP2_PHYS_DMA
		/* Handle data movement if physical dma is not
		 * enabled or not supported on host controller */
		if (!hpsb_register_addrspace(&sbp2_highlevel, ud->ne->host,
					     &sbp2_physdma_ops,
					     0x0ULL, 0xfffffffcULL)) {
			SBP2_ERR("failed to register lower 4GB address range");
			goto failed_alloc;
		}
#else
		if (dma_set_mask(hi->host->device.parent, DMA_32BIT_MASK)) {
			SBP2_ERR("failed to set 4GB DMA mask");
			goto failed_alloc;
		}
#endif
	}

	/* Prevent unloading of the 1394 host */
	if (!try_module_get(hi->host->driver->owner)) {
		SBP2_ERR("failed to get a reference on 1394 host driver");
		goto failed_alloc;
	}

	lu->hi = hi;

	list_add_tail(&lu->lu_list, &hi->logical_units);

	/* Register the status FIFO address range. We could use the same FIFO
	 * for targets at different nodes. However we need different FIFOs per
	 * target in order to support multi-unit devices.
	 * The FIFO is located out of the local host controller's physical range
	 * but, if possible, within the posted write area. Status writes will
	 * then be performed as unified transactions. This slightly reduces
	 * bandwidth usage, and some Prolific based devices seem to require it.
	 */
	lu->status_fifo_addr = hpsb_allocate_and_register_addrspace(
			&sbp2_highlevel, ud->ne->host, &sbp2_ops,
			sizeof(struct sbp2_status_block), sizeof(quadlet_t),
			ud->ne->host->low_addr_space, CSR1212_ALL_SPACE_END);
	if (lu->status_fifo_addr == CSR1212_INVALID_ADDR_SPACE) {
		SBP2_ERR("failed to allocate status FIFO address range");
		goto failed_alloc;
	}

	shost = scsi_host_alloc(&sbp2_shost_template, sizeof(unsigned long));
	if (!shost) {
		SBP2_ERR("failed to register scsi host");
		goto failed_alloc;
	}

	shost->hostdata[0] = (unsigned long)lu;

	if (!scsi_add_host(shost, &ud->device)) {
		lu->shost = shost;
		return lu;
	}

	SBP2_ERR("failed to add scsi host");
	scsi_host_put(shost);

failed_alloc:
	sbp2_remove_device(lu);
	return NULL;
}

static void sbp2_host_reset(struct hpsb_host *host)
{
	struct sbp2_fwhost_info *hi;
	struct sbp2_lu *lu;

	hi = hpsb_get_hostinfo(&sbp2_highlevel, host);
	if (!hi)
		return;
	list_for_each_entry(lu, &hi->logical_units, lu_list)
		if (likely(atomic_read(&lu->state) !=
			   SBP2LU_STATE_IN_SHUTDOWN)) {
			atomic_set(&lu->state, SBP2LU_STATE_IN_RESET);
			scsi_block_requests(lu->shost);
		}
}

static int sbp2_start_device(struct sbp2_lu *lu)
{
	struct sbp2_fwhost_info *hi = lu->hi;
	int error;

	lu->login_response = dma_alloc_coherent(hi->host->device.parent,
				     sizeof(struct sbp2_login_response),
				     &lu->login_response_dma, GFP_KERNEL);
	if (!lu->login_response)
		goto alloc_fail;

	lu->query_logins_orb = dma_alloc_coherent(hi->host->device.parent,
				     sizeof(struct sbp2_query_logins_orb),
				     &lu->query_logins_orb_dma, GFP_KERNEL);
	if (!lu->query_logins_orb)
		goto alloc_fail;

	lu->query_logins_response = dma_alloc_coherent(hi->host->device.parent,
				     sizeof(struct sbp2_query_logins_response),
				     &lu->query_logins_response_dma, GFP_KERNEL);
	if (!lu->query_logins_response)
		goto alloc_fail;

	lu->reconnect_orb = dma_alloc_coherent(hi->host->device.parent,
				     sizeof(struct sbp2_reconnect_orb),
				     &lu->reconnect_orb_dma, GFP_KERNEL);
	if (!lu->reconnect_orb)
		goto alloc_fail;

	lu->logout_orb = dma_alloc_coherent(hi->host->device.parent,
				     sizeof(struct sbp2_logout_orb),
				     &lu->logout_orb_dma, GFP_KERNEL);
	if (!lu->logout_orb)
		goto alloc_fail;

	lu->login_orb = dma_alloc_coherent(hi->host->device.parent,
				     sizeof(struct sbp2_login_orb),
				     &lu->login_orb_dma, GFP_KERNEL);
	if (!lu->login_orb)
		goto alloc_fail;

	if (sbp2util_create_command_orb_pool(lu))
		goto alloc_fail;

	/* Wait a second before trying to log in. Previously logged in
	 * initiators need a chance to reconnect. */
	if (msleep_interruptible(1000)) {
		sbp2_remove_device(lu);
		return -EINTR;
	}

	if (sbp2_login_device(lu)) {
		sbp2_remove_device(lu);
		return -EBUSY;
	}

	sbp2_set_busy_timeout(lu);
	sbp2_agent_reset(lu, 1);
	sbp2_max_speed_and_size(lu);

	error = scsi_add_device(lu->shost, 0, lu->ud->id, 0);
	if (error) {
		SBP2_ERR("scsi_add_device failed");
		sbp2_logout_device(lu);
		sbp2_remove_device(lu);
		return error;
	}

	return 0;

alloc_fail:
	SBP2_ERR("Could not allocate memory for lu");
	sbp2_remove_device(lu);
	return -ENOMEM;
}

static void sbp2_remove_device(struct sbp2_lu *lu)
{
	struct sbp2_fwhost_info *hi;

	if (!lu)
		return;

	hi = lu->hi;

	if (lu->shost) {
		scsi_remove_host(lu->shost);
		scsi_host_put(lu->shost);
	}
	flush_scheduled_work();
	sbp2util_remove_command_orb_pool(lu);

	list_del(&lu->lu_list);

	if (lu->login_response)
		dma_free_coherent(hi->host->device.parent,
				    sizeof(struct sbp2_login_response),
				    lu->login_response,
				    lu->login_response_dma);
	if (lu->login_orb)
		dma_free_coherent(hi->host->device.parent,
				    sizeof(struct sbp2_login_orb),
				    lu->login_orb,
				    lu->login_orb_dma);
	if (lu->reconnect_orb)
		dma_free_coherent(hi->host->device.parent,
				    sizeof(struct sbp2_reconnect_orb),
				    lu->reconnect_orb,
				    lu->reconnect_orb_dma);
	if (lu->logout_orb)
		dma_free_coherent(hi->host->device.parent,
				    sizeof(struct sbp2_logout_orb),
				    lu->logout_orb,
				    lu->logout_orb_dma);
	if (lu->query_logins_orb)
		dma_free_coherent(hi->host->device.parent,
				    sizeof(struct sbp2_query_logins_orb),
				    lu->query_logins_orb,
				    lu->query_logins_orb_dma);
	if (lu->query_logins_response)
		dma_free_coherent(hi->host->device.parent,
				    sizeof(struct sbp2_query_logins_response),
				    lu->query_logins_response,
				    lu->query_logins_response_dma);

	if (lu->status_fifo_addr != CSR1212_INVALID_ADDR_SPACE)
		hpsb_unregister_addrspace(&sbp2_highlevel, hi->host,
					  lu->status_fifo_addr);

	lu->ud->device.driver_data = NULL;

	if (hi)
		module_put(hi->host->driver->owner);

	kfree(lu);
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

static int sbp2_query_logins(struct sbp2_lu *lu)
{
	struct sbp2_fwhost_info *hi = lu->hi;
	quadlet_t data[2];
	int max_logins;
	int active_logins;

	lu->query_logins_orb->reserved1 = 0x0;
	lu->query_logins_orb->reserved2 = 0x0;

	lu->query_logins_orb->query_response_lo = lu->query_logins_response_dma;
	lu->query_logins_orb->query_response_hi =
			ORB_SET_NODE_ID(hi->host->node_id);
	lu->query_logins_orb->lun_misc =
			ORB_SET_FUNCTION(SBP2_QUERY_LOGINS_REQUEST);
	lu->query_logins_orb->lun_misc |= ORB_SET_NOTIFY(1);
	lu->query_logins_orb->lun_misc |= ORB_SET_LUN(lu->lun);

	lu->query_logins_orb->reserved_resp_length =
		ORB_SET_QUERY_LOGINS_RESP_LENGTH(
			sizeof(struct sbp2_query_logins_response));

	lu->query_logins_orb->status_fifo_hi =
		ORB_SET_STATUS_FIFO_HI(lu->status_fifo_addr, hi->host->node_id);
	lu->query_logins_orb->status_fifo_lo =
		ORB_SET_STATUS_FIFO_LO(lu->status_fifo_addr);

	sbp2util_cpu_to_be32_buffer(lu->query_logins_orb,
				    sizeof(struct sbp2_query_logins_orb));

	memset(lu->query_logins_response, 0,
	       sizeof(struct sbp2_query_logins_response));

	data[0] = ORB_SET_NODE_ID(hi->host->node_id);
	data[1] = lu->query_logins_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);

	hpsb_node_write(lu->ne, lu->management_agent_addr, data, 8);

	if (sbp2util_access_timeout(lu, 2*HZ)) {
		SBP2_INFO("Error querying logins to SBP-2 device - timed out");
		return -EIO;
	}

	if (lu->status_block.ORB_offset_lo != lu->query_logins_orb_dma) {
		SBP2_INFO("Error querying logins to SBP-2 device - timed out");
		return -EIO;
	}

	if (STATUS_TEST_RDS(lu->status_block.ORB_offset_hi_misc)) {
		SBP2_INFO("Error querying logins to SBP-2 device - failed");
		return -EIO;
	}

	sbp2util_cpu_to_be32_buffer(lu->query_logins_response,
				    sizeof(struct sbp2_query_logins_response));

	max_logins = RESPONSE_GET_MAX_LOGINS(
			lu->query_logins_response->length_max_logins);
	SBP2_INFO("Maximum concurrent logins supported: %d", max_logins);

	active_logins = RESPONSE_GET_ACTIVE_LOGINS(
			lu->query_logins_response->length_max_logins);
	SBP2_INFO("Number of active logins: %d", active_logins);

	if (active_logins >= max_logins) {
		return -EIO;
	}

	return 0;
}

static int sbp2_login_device(struct sbp2_lu *lu)
{
	struct sbp2_fwhost_info *hi = lu->hi;
	quadlet_t data[2];

	if (!lu->login_orb)
		return -EIO;

	if (!sbp2_exclusive_login && sbp2_query_logins(lu)) {
		SBP2_INFO("Device does not support any more concurrent logins");
		return -EIO;
	}

	/* assume no password */
	lu->login_orb->password_hi = 0;
	lu->login_orb->password_lo = 0;

	lu->login_orb->login_response_lo = lu->login_response_dma;
	lu->login_orb->login_response_hi = ORB_SET_NODE_ID(hi->host->node_id);
	lu->login_orb->lun_misc = ORB_SET_FUNCTION(SBP2_LOGIN_REQUEST);

	/* one second reconnect time */
	lu->login_orb->lun_misc |= ORB_SET_RECONNECT(0);
	lu->login_orb->lun_misc |= ORB_SET_EXCLUSIVE(sbp2_exclusive_login);
	lu->login_orb->lun_misc |= ORB_SET_NOTIFY(1);
	lu->login_orb->lun_misc |= ORB_SET_LUN(lu->lun);

	lu->login_orb->passwd_resp_lengths =
		ORB_SET_LOGIN_RESP_LENGTH(sizeof(struct sbp2_login_response));

	lu->login_orb->status_fifo_hi =
		ORB_SET_STATUS_FIFO_HI(lu->status_fifo_addr, hi->host->node_id);
	lu->login_orb->status_fifo_lo =
		ORB_SET_STATUS_FIFO_LO(lu->status_fifo_addr);

	sbp2util_cpu_to_be32_buffer(lu->login_orb,
				    sizeof(struct sbp2_login_orb));

	memset(lu->login_response, 0, sizeof(struct sbp2_login_response));

	data[0] = ORB_SET_NODE_ID(hi->host->node_id);
	data[1] = lu->login_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);

	hpsb_node_write(lu->ne, lu->management_agent_addr, data, 8);

	/* wait up to 20 seconds for login status */
	if (sbp2util_access_timeout(lu, 20*HZ)) {
		SBP2_ERR("Error logging into SBP-2 device - timed out");
		return -EIO;
	}

	/* make sure that the returned status matches the login ORB */
	if (lu->status_block.ORB_offset_lo != lu->login_orb_dma) {
		SBP2_ERR("Error logging into SBP-2 device - timed out");
		return -EIO;
	}

	if (STATUS_TEST_RDS(lu->status_block.ORB_offset_hi_misc)) {
		SBP2_ERR("Error logging into SBP-2 device - failed");
		return -EIO;
	}

	sbp2util_cpu_to_be32_buffer(lu->login_response,
				    sizeof(struct sbp2_login_response));
	lu->command_block_agent_addr =
			((u64)lu->login_response->command_block_agent_hi) << 32;
	lu->command_block_agent_addr |=
			((u64)lu->login_response->command_block_agent_lo);
	lu->command_block_agent_addr &= 0x0000ffffffffffffULL;

	SBP2_INFO("Logged into SBP-2 device");
	return 0;
}

static int sbp2_logout_device(struct sbp2_lu *lu)
{
	struct sbp2_fwhost_info *hi = lu->hi;
	quadlet_t data[2];
	int error;

	lu->logout_orb->reserved1 = 0x0;
	lu->logout_orb->reserved2 = 0x0;
	lu->logout_orb->reserved3 = 0x0;
	lu->logout_orb->reserved4 = 0x0;

	lu->logout_orb->login_ID_misc = ORB_SET_FUNCTION(SBP2_LOGOUT_REQUEST);
	lu->logout_orb->login_ID_misc |=
			ORB_SET_LOGIN_ID(lu->login_response->length_login_ID);
	lu->logout_orb->login_ID_misc |= ORB_SET_NOTIFY(1);

	lu->logout_orb->reserved5 = 0x0;
	lu->logout_orb->status_fifo_hi =
		ORB_SET_STATUS_FIFO_HI(lu->status_fifo_addr, hi->host->node_id);
	lu->logout_orb->status_fifo_lo =
		ORB_SET_STATUS_FIFO_LO(lu->status_fifo_addr);

	sbp2util_cpu_to_be32_buffer(lu->logout_orb,
				    sizeof(struct sbp2_logout_orb));

	data[0] = ORB_SET_NODE_ID(hi->host->node_id);
	data[1] = lu->logout_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);

	error = hpsb_node_write(lu->ne, lu->management_agent_addr, data, 8);
	if (error)
		return error;

	/* wait up to 1 second for the device to complete logout */
	if (sbp2util_access_timeout(lu, HZ))
		return -EIO;

	SBP2_INFO("Logged out of SBP-2 device");
	return 0;
}

static int sbp2_reconnect_device(struct sbp2_lu *lu)
{
	struct sbp2_fwhost_info *hi = lu->hi;
	quadlet_t data[2];
	int error;

	lu->reconnect_orb->reserved1 = 0x0;
	lu->reconnect_orb->reserved2 = 0x0;
	lu->reconnect_orb->reserved3 = 0x0;
	lu->reconnect_orb->reserved4 = 0x0;

	lu->reconnect_orb->login_ID_misc =
			ORB_SET_FUNCTION(SBP2_RECONNECT_REQUEST);
	lu->reconnect_orb->login_ID_misc |=
			ORB_SET_LOGIN_ID(lu->login_response->length_login_ID);
	lu->reconnect_orb->login_ID_misc |= ORB_SET_NOTIFY(1);

	lu->reconnect_orb->reserved5 = 0x0;
	lu->reconnect_orb->status_fifo_hi =
		ORB_SET_STATUS_FIFO_HI(lu->status_fifo_addr, hi->host->node_id);
	lu->reconnect_orb->status_fifo_lo =
		ORB_SET_STATUS_FIFO_LO(lu->status_fifo_addr);

	sbp2util_cpu_to_be32_buffer(lu->reconnect_orb,
				    sizeof(struct sbp2_reconnect_orb));

	data[0] = ORB_SET_NODE_ID(hi->host->node_id);
	data[1] = lu->reconnect_orb_dma;
	sbp2util_cpu_to_be32_buffer(data, 8);

	error = hpsb_node_write(lu->ne, lu->management_agent_addr, data, 8);
	if (error)
		return error;

	/* wait up to 1 second for reconnect status */
	if (sbp2util_access_timeout(lu, HZ)) {
		SBP2_ERR("Error reconnecting to SBP-2 device - timed out");
		return -EIO;
	}

	/* make sure that the returned status matches the reconnect ORB */
	if (lu->status_block.ORB_offset_lo != lu->reconnect_orb_dma) {
		SBP2_ERR("Error reconnecting to SBP-2 device - timed out");
		return -EIO;
	}

	if (STATUS_TEST_RDS(lu->status_block.ORB_offset_hi_misc)) {
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
static int sbp2_set_busy_timeout(struct sbp2_lu *lu)
{
	quadlet_t data;

	data = cpu_to_be32(SBP2_BUSY_TIMEOUT_VALUE);
	if (hpsb_node_write(lu->ne, SBP2_BUSY_TIMEOUT_ADDRESS, &data, 4))
		SBP2_ERR("%s error", __FUNCTION__);
	return 0;
}

static void sbp2_parse_unit_directory(struct sbp2_lu *lu,
				      struct unit_directory *ud)
{
	struct csr1212_keyval *kv;
	struct csr1212_dentry *dentry;
	u64 management_agent_addr;
	u32 unit_characteristics, firmware_revision;
	unsigned workarounds;
	int i;

	management_agent_addr = 0;
	unit_characteristics = 0;
	firmware_revision = 0;

	csr1212_for_each_dir_entry(ud->ne->csr, kv, ud->ud_kv, dentry) {
		switch (kv->key.id) {
		case CSR1212_KV_ID_DEPENDENT_INFO:
			if (kv->key.type == CSR1212_KV_TYPE_CSR_OFFSET)
				management_agent_addr =
				    CSR1212_REGISTER_SPACE_BASE +
				    (kv->value.csr_offset << 2);

			else if (kv->key.type == CSR1212_KV_TYPE_IMMEDIATE)
				lu->lun = ORB_SET_LUN(kv->value.immediate);
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
			if (sbp2_workarounds_table[i].firmware_revision !=
			    SBP2_ROM_VALUE_WILDCARD &&
			    sbp2_workarounds_table[i].firmware_revision !=
			    (firmware_revision & 0xffff00))
				continue;
			if (sbp2_workarounds_table[i].model_id !=
			    SBP2_ROM_VALUE_WILDCARD &&
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
		struct unit_directory *parent_ud = container_of(
			ud->device.parent, struct unit_directory, device);
		sbp2_parse_unit_directory(lu, parent_ud);
	} else {
		lu->management_agent_addr = management_agent_addr;
		lu->workarounds = workarounds;
		if (ud->flags & UNIT_DIRECTORY_HAS_LUN)
			lu->lun = ORB_SET_LUN(ud->lun);
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
static int sbp2_max_speed_and_size(struct sbp2_lu *lu)
{
	struct sbp2_fwhost_info *hi = lu->hi;
	u8 payload;

	lu->speed_code = hi->host->speed[NODEID_TO_NODE(lu->ne->nodeid)];

	if (lu->speed_code > sbp2_max_speed) {
		lu->speed_code = sbp2_max_speed;
		SBP2_INFO("Reducing speed to %s",
			  hpsb_speedto_str[sbp2_max_speed]);
	}

	/* Payload size is the lesser of what our speed supports and what
	 * our host supports.  */
	payload = min(sbp2_speedto_max_payload[lu->speed_code],
		      (u8) (hi->host->csr.max_rec - 1));

	/* If physical DMA is off, work around limitation in ohci1394:
	 * packet size must not exceed PAGE_SIZE */
	if (lu->ne->host->low_addr_space < (1ULL << 32))
		while (SBP2_PAYLOAD_TO_BYTES(payload) + 24 > PAGE_SIZE &&
		       payload)
			payload--;

	SBP2_INFO("Node " NODE_BUS_FMT ": Max speed [%s] - Max payload [%u]",
		  NODE_BUS_ARGS(hi->host, lu->ne->nodeid),
		  hpsb_speedto_str[lu->speed_code],
		  SBP2_PAYLOAD_TO_BYTES(payload));

	lu->max_payload_size = payload;
	return 0;
}

static int sbp2_agent_reset(struct sbp2_lu *lu, int wait)
{
	quadlet_t data;
	u64 addr;
	int retval;
	unsigned long flags;

	/* flush lu->protocol_work */
	if (wait)
		flush_scheduled_work();

	data = ntohl(SBP2_AGENT_RESET_DATA);
	addr = lu->command_block_agent_addr + SBP2_AGENT_RESET_OFFSET;

	if (wait)
		retval = hpsb_node_write(lu->ne, addr, &data, 4);
	else
		retval = sbp2util_node_write_no_wait(lu->ne, addr, &data, 4);

	if (retval < 0) {
		SBP2_ERR("hpsb_node_write failed.\n");
		return -EIO;
	}

	/* make sure that the ORB_POINTER is written on next command */
	spin_lock_irqsave(&lu->cmd_orb_lock, flags);
	lu->last_orb = NULL;
	spin_unlock_irqrestore(&lu->cmd_orb_lock, flags);

	return 0;
}

static void sbp2_prep_command_orb_sg(struct sbp2_command_orb *orb,
				     struct sbp2_fwhost_info *hi,
				     struct sbp2_command_info *cmd,
				     unsigned int scsi_use_sg,
				     struct scatterlist *sgpnt,
				     u32 orb_direction,
				     enum dma_data_direction dma_dir)
{
	cmd->dma_dir = dma_dir;
	orb->data_descriptor_hi = ORB_SET_NODE_ID(hi->host->node_id);
	orb->misc |= ORB_SET_DIRECTION(orb_direction);

	/* special case if only one element (and less than 64KB in size) */
	if ((scsi_use_sg == 1) &&
	    (sgpnt[0].length <= SBP2_MAX_SG_ELEMENT_LENGTH)) {

		cmd->dma_size = sgpnt[0].length;
		cmd->dma_type = CMD_DMA_PAGE;
		cmd->cmd_dma = dma_map_page(hi->host->device.parent,
					    sgpnt[0].page, sgpnt[0].offset,
					    cmd->dma_size, cmd->dma_dir);

		orb->data_descriptor_lo = cmd->cmd_dma;
		orb->misc |= ORB_SET_DATA_SIZE(cmd->dma_size);

	} else {
		struct sbp2_unrestricted_page_table *sg_element =
						&cmd->scatter_gather_element[0];
		u32 sg_count, sg_len;
		dma_addr_t sg_addr;
		int i, count = dma_map_sg(hi->host->device.parent, sgpnt,
					  scsi_use_sg, dma_dir);

		cmd->dma_size = scsi_use_sg;
		cmd->sge_buffer = sgpnt;

		/* use page tables (s/g) */
		orb->misc |= ORB_SET_PAGE_TABLE_PRESENT(0x1);
		orb->data_descriptor_lo = cmd->sge_dma;

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
					struct sbp2_command_info *cmd,
					struct scatterlist *sgpnt,
					u32 orb_direction,
					unsigned int scsi_request_bufflen,
					void *scsi_request_buffer,
					enum dma_data_direction dma_dir)
{
	cmd->dma_dir = dma_dir;
	cmd->dma_size = scsi_request_bufflen;
	cmd->dma_type = CMD_DMA_SINGLE;
	cmd->cmd_dma = dma_map_single(hi->host->device.parent,
				      scsi_request_buffer,
				      cmd->dma_size, cmd->dma_dir);
	orb->data_descriptor_hi = ORB_SET_NODE_ID(hi->host->node_id);
	orb->misc |= ORB_SET_DIRECTION(orb_direction);

	/* handle case where we get a command w/o s/g enabled
	 * (but check for transfers larger than 64K) */
	if (scsi_request_bufflen <= SBP2_MAX_SG_ELEMENT_LENGTH) {

		orb->data_descriptor_lo = cmd->cmd_dma;
		orb->misc |= ORB_SET_DATA_SIZE(scsi_request_bufflen);

	} else {
		/* The buffer is too large. Turn this into page tables. */

		struct sbp2_unrestricted_page_table *sg_element =
						&cmd->scatter_gather_element[0];
		u32 sg_count, sg_len;
		dma_addr_t sg_addr;

		orb->data_descriptor_lo = cmd->sge_dma;
		orb->misc |= ORB_SET_PAGE_TABLE_PRESENT(0x1);

		/* fill out our SBP-2 page tables; split up the large buffer */
		sg_count = 0;
		sg_len = scsi_request_bufflen;
		sg_addr = cmd->cmd_dma;
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

static void sbp2_create_command_orb(struct sbp2_lu *lu,
				    struct sbp2_command_info *cmd,
				    unchar *scsi_cmd,
				    unsigned int scsi_use_sg,
				    unsigned int scsi_request_bufflen,
				    void *scsi_request_buffer,
				    enum dma_data_direction dma_dir)
{
	struct sbp2_fwhost_info *hi = lu->hi;
	struct scatterlist *sgpnt = (struct scatterlist *)scsi_request_buffer;
	struct sbp2_command_orb *orb = &cmd->command_orb;
	u32 orb_direction;

	/*
	 * Set-up our command ORB.
	 *
	 * NOTE: We're doing unrestricted page tables (s/g), as this is
	 * best performance (at least with the devices I have). This means
	 * that data_size becomes the number of s/g elements, and
	 * page_size should be zero (for unrestricted).
	 */
	orb->next_ORB_hi = ORB_SET_NULL_PTR(1);
	orb->next_ORB_lo = 0x0;
	orb->misc = ORB_SET_MAX_PAYLOAD(lu->max_payload_size);
	orb->misc |= ORB_SET_SPEED(lu->speed_code);
	orb->misc |= ORB_SET_NOTIFY(1);

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
		orb->data_descriptor_hi = 0x0;
		orb->data_descriptor_lo = 0x0;
		orb->misc |= ORB_SET_DIRECTION(1);
	} else if (scsi_use_sg)
		sbp2_prep_command_orb_sg(orb, hi, cmd, scsi_use_sg, sgpnt,
					 orb_direction, dma_dir);
	else
		sbp2_prep_command_orb_no_sg(orb, hi, cmd, sgpnt, orb_direction,
					    scsi_request_bufflen,
					    scsi_request_buffer, dma_dir);

	sbp2util_cpu_to_be32_buffer(orb, sizeof(*orb));

	memset(orb->cdb, 0, 12);
	memcpy(orb->cdb, scsi_cmd, COMMAND_SIZE(*scsi_cmd));
}

static void sbp2_link_orb_command(struct sbp2_lu *lu,
				  struct sbp2_command_info *cmd)
{
	struct sbp2_fwhost_info *hi = lu->hi;
	struct sbp2_command_orb *last_orb;
	dma_addr_t last_orb_dma;
	u64 addr = lu->command_block_agent_addr;
	quadlet_t data[2];
	size_t length;
	unsigned long flags;

	dma_sync_single_for_device(hi->host->device.parent,
				   cmd->command_orb_dma,
				   sizeof(struct sbp2_command_orb),
				   DMA_TO_DEVICE);
	dma_sync_single_for_device(hi->host->device.parent, cmd->sge_dma,
				   sizeof(cmd->scatter_gather_element),
				   DMA_TO_DEVICE);

	/* check to see if there are any previous orbs to use */
	spin_lock_irqsave(&lu->cmd_orb_lock, flags);
	last_orb = lu->last_orb;
	last_orb_dma = lu->last_orb_dma;
	if (!last_orb) {
		/*
		 * last_orb == NULL means: We know that the target's fetch agent
		 * is not active right now.
		 */
		addr += SBP2_ORB_POINTER_OFFSET;
		data[0] = ORB_SET_NODE_ID(hi->host->node_id);
		data[1] = cmd->command_orb_dma;
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
		dma_sync_single_for_cpu(hi->host->device.parent, last_orb_dma,
					sizeof(struct sbp2_command_orb),
					DMA_TO_DEVICE);
		last_orb->next_ORB_lo = cpu_to_be32(cmd->command_orb_dma);
		wmb();
		/* Tells hardware that this pointer is valid */
		last_orb->next_ORB_hi = 0;
		dma_sync_single_for_device(hi->host->device.parent,
					   last_orb_dma,
					   sizeof(struct sbp2_command_orb),
					   DMA_TO_DEVICE);
		addr += SBP2_DOORBELL_OFFSET;
		data[0] = 0;
		length = 4;
	}
	lu->last_orb = &cmd->command_orb;
	lu->last_orb_dma = cmd->command_orb_dma;
	spin_unlock_irqrestore(&lu->cmd_orb_lock, flags);

	if (sbp2util_node_write_no_wait(lu->ne, addr, data, length)) {
		/*
		 * sbp2util_node_write_no_wait failed. We certainly ran out
		 * of transaction labels, perhaps just because there were no
		 * context switches which gave khpsbpkt a chance to collect
		 * free tlabels. Try again in non-atomic context. If necessary,
		 * the workqueue job will sleep to guaranteedly get a tlabel.
		 * We do not accept new commands until the job is over.
		 */
		scsi_block_requests(lu->shost);
		PREPARE_WORK(&lu->protocol_work,
			     last_orb ? sbp2util_write_doorbell:
					sbp2util_write_orb_pointer);
		schedule_work(&lu->protocol_work);
	}
}

static int sbp2_send_command(struct sbp2_lu *lu, struct scsi_cmnd *SCpnt,
			     void (*done)(struct scsi_cmnd *))
{
	unchar *scsi_cmd = (unchar *)SCpnt->cmnd;
	unsigned int request_bufflen = SCpnt->request_bufflen;
	struct sbp2_command_info *cmd;

	cmd = sbp2util_allocate_command_orb(lu, SCpnt, done);
	if (!cmd)
		return -EIO;

	sbp2_create_command_orb(lu, cmd, scsi_cmd, SCpnt->use_sg,
				request_bufflen, SCpnt->request_buffer,
				SCpnt->sc_data_direction);
	sbp2_link_orb_command(lu, cmd);

	return 0;
}

/*
 * Translates SBP-2 status into SCSI sense data for check conditions
 */
static unsigned int sbp2_status_to_sense_data(unchar *sbp2_status,
					      unchar *sense_data)
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
	struct sbp2_lu *lu = NULL, *lu_tmp;
	struct scsi_cmnd *SCpnt = NULL;
	struct sbp2_status_block *sb;
	u32 scsi_status = SBP2_SCSI_STATUS_GOOD;
	struct sbp2_command_info *cmd;
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
	list_for_each_entry(lu_tmp, &hi->logical_units, lu_list) {
		if (lu_tmp->ne->nodeid == nodeid &&
		    lu_tmp->status_fifo_addr == addr) {
			lu = lu_tmp;
			break;
		}
	}
	if (unlikely(!lu)) {
		SBP2_ERR("lu is NULL - device is gone?");
		return RCODE_ADDRESS_ERROR;
	}

	/* Put response into lu status fifo buffer. The first two bytes
	 * come in big endian bit order. Often the target writes only a
	 * truncated status block, minimally the first two quadlets. The rest
	 * is implied to be zeros. */
	sb = &lu->status_block;
	memset(sb->command_set_dependent, 0, sizeof(sb->command_set_dependent));
	memcpy(sb, data, length);
	sbp2util_be32_to_cpu_buffer(sb, 8);

	/* Ignore unsolicited status. Handle command ORB status. */
	if (unlikely(STATUS_GET_SRC(sb->ORB_offset_hi_misc) == 2))
		cmd = NULL;
	else
		cmd = sbp2util_find_command_for_orb(lu, sb->ORB_offset_lo);
	if (cmd) {
		dma_sync_single_for_cpu(hi->host->device.parent,
					cmd->command_orb_dma,
					sizeof(struct sbp2_command_orb),
					DMA_TO_DEVICE);
		dma_sync_single_for_cpu(hi->host->device.parent, cmd->sge_dma,
					sizeof(cmd->scatter_gather_element),
					DMA_TO_DEVICE);
		/* Grab SCSI command pointers and check status. */
		/*
		 * FIXME: If the src field in the status is 1, the ORB DMA must
		 * not be reused until status for a subsequent ORB is received.
		 */
		SCpnt = cmd->Current_SCpnt;
		spin_lock_irqsave(&lu->cmd_orb_lock, flags);
		sbp2util_mark_command_completed(lu, cmd);
		spin_unlock_irqrestore(&lu->cmd_orb_lock, flags);

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
                                sbp2_agent_reset(lu, 0);
		}

		/* Check here to see if there are no commands in-use. If there
		 * are none, we know that the fetch agent left the active state
		 * _and_ that we did not reactivate it yet. Therefore clear
		 * last_orb so that next time we write directly to the
		 * ORB_POINTER register. That way the fetch agent does not need
		 * to refetch the next_ORB. */
		spin_lock_irqsave(&lu->cmd_orb_lock, flags);
		if (list_empty(&lu->cmd_orb_inuse))
			lu->last_orb = NULL;
		spin_unlock_irqrestore(&lu->cmd_orb_lock, flags);

	} else {
		/* It's probably status after a management request. */
		if ((sb->ORB_offset_lo == lu->reconnect_orb_dma) ||
		    (sb->ORB_offset_lo == lu->login_orb_dma) ||
		    (sb->ORB_offset_lo == lu->query_logins_orb_dma) ||
		    (sb->ORB_offset_lo == lu->logout_orb_dma)) {
			lu->access_complete = 1;
			wake_up_interruptible(&sbp2_access_wq);
		}
	}

	if (SCpnt)
		sbp2scsi_complete_command(lu, scsi_status, SCpnt,
					  cmd->Current_done);
	return RCODE_COMPLETE;
}

/**************************************
 * SCSI interface related section
 **************************************/

static int sbp2scsi_queuecommand(struct scsi_cmnd *SCpnt,
				 void (*done)(struct scsi_cmnd *))
{
	struct sbp2_lu *lu = (struct sbp2_lu *)SCpnt->device->host->hostdata[0];
	struct sbp2_fwhost_info *hi;
	int result = DID_NO_CONNECT << 16;

	if (unlikely(!sbp2util_node_is_available(lu)))
		goto done;

	hi = lu->hi;

	if (unlikely(!hi)) {
		SBP2_ERR("sbp2_fwhost_info is NULL - this is bad!");
		goto done;
	}

	/* Multiple units are currently represented to the SCSI core as separate
	 * targets, not as one target with multiple LUs. Therefore return
	 * selection time-out to any IO directed at non-zero LUNs. */
	if (unlikely(SCpnt->device->lun))
		goto done;

	if (unlikely(!hpsb_node_entry_valid(lu->ne))) {
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

	if (sbp2_send_command(lu, SCpnt, done)) {
		SBP2_ERR("Error sending SCSI command");
		sbp2scsi_complete_command(lu,
					  SBP2_SCSI_STATUS_SELECTION_TIMEOUT,
					  SCpnt, done);
	}
	return 0;

done:
	SCpnt->result = result;
	done(SCpnt);
	return 0;
}

static void sbp2scsi_complete_all_commands(struct sbp2_lu *lu, u32 status)
{
	struct sbp2_fwhost_info *hi = lu->hi;
	struct list_head *lh;
	struct sbp2_command_info *cmd;
	unsigned long flags;

	spin_lock_irqsave(&lu->cmd_orb_lock, flags);
	while (!list_empty(&lu->cmd_orb_inuse)) {
		lh = lu->cmd_orb_inuse.next;
		cmd = list_entry(lh, struct sbp2_command_info, list);
		dma_sync_single_for_cpu(hi->host->device.parent,
				        cmd->command_orb_dma,
					sizeof(struct sbp2_command_orb),
					DMA_TO_DEVICE);
		dma_sync_single_for_cpu(hi->host->device.parent, cmd->sge_dma,
					sizeof(cmd->scatter_gather_element),
					DMA_TO_DEVICE);
		sbp2util_mark_command_completed(lu, cmd);
		if (cmd->Current_SCpnt) {
			cmd->Current_SCpnt->result = status << 16;
			cmd->Current_done(cmd->Current_SCpnt);
		}
	}
	spin_unlock_irqrestore(&lu->cmd_orb_lock, flags);

	return;
}

/*
 * Complete a regular SCSI command. Can be called in atomic context.
 */
static void sbp2scsi_complete_command(struct sbp2_lu *lu, u32 scsi_status,
				      struct scsi_cmnd *SCpnt,
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
	if (!hpsb_node_entry_valid(lu->ne)
	    && (scsi_status != SBP2_SCSI_STATUS_GOOD)) {
		SBP2_ERR("Completing command with busy (bus reset)");
		SCpnt->result = DID_BUS_BUSY << 16;
	}

	/* Tell the SCSI stack that we're done with this command. */
	done(SCpnt);
}

static int sbp2scsi_slave_alloc(struct scsi_device *sdev)
{
	struct sbp2_lu *lu = (struct sbp2_lu *)sdev->host->hostdata[0];

	lu->sdev = sdev;
	sdev->allow_restart = 1;

	if (lu->workarounds & SBP2_WORKAROUND_INQUIRY_36)
		sdev->inquiry_len = 36;
	return 0;
}

static int sbp2scsi_slave_configure(struct scsi_device *sdev)
{
	struct sbp2_lu *lu = (struct sbp2_lu *)sdev->host->hostdata[0];

	sdev->use_10_for_rw = 1;

	if (sdev->type == TYPE_ROM)
		sdev->use_10_for_ms = 1;
	if (sdev->type == TYPE_DISK &&
	    lu->workarounds & SBP2_WORKAROUND_MODE_SENSE_8)
		sdev->skip_ms_page_8 = 1;
	if (lu->workarounds & SBP2_WORKAROUND_FIX_CAPACITY)
		sdev->fix_capacity = 1;
	return 0;
}

static void sbp2scsi_slave_destroy(struct scsi_device *sdev)
{
	((struct sbp2_lu *)sdev->host->hostdata[0])->sdev = NULL;
	return;
}

/*
 * Called by scsi stack when something has really gone wrong.
 * Usually called when a command has timed-out for some reason.
 */
static int sbp2scsi_abort(struct scsi_cmnd *SCpnt)
{
	struct sbp2_lu *lu = (struct sbp2_lu *)SCpnt->device->host->hostdata[0];
	struct sbp2_fwhost_info *hi = lu->hi;
	struct sbp2_command_info *cmd;
	unsigned long flags;

	SBP2_INFO("aborting sbp2 command");
	scsi_print_command(SCpnt);

	if (sbp2util_node_is_available(lu)) {
		sbp2_agent_reset(lu, 1);

		/* Return a matching command structure to the free pool. */
		spin_lock_irqsave(&lu->cmd_orb_lock, flags);
		cmd = sbp2util_find_command_for_SCpnt(lu, SCpnt);
		if (cmd) {
			dma_sync_single_for_cpu(hi->host->device.parent,
					cmd->command_orb_dma,
					sizeof(struct sbp2_command_orb),
					DMA_TO_DEVICE);
			dma_sync_single_for_cpu(hi->host->device.parent,
					cmd->sge_dma,
					sizeof(cmd->scatter_gather_element),
					DMA_TO_DEVICE);
			sbp2util_mark_command_completed(lu, cmd);
			if (cmd->Current_SCpnt) {
				cmd->Current_SCpnt->result = DID_ABORT << 16;
				cmd->Current_done(cmd->Current_SCpnt);
			}
		}
		spin_unlock_irqrestore(&lu->cmd_orb_lock, flags);

		sbp2scsi_complete_all_commands(lu, DID_BUS_BUSY);
	}

	return SUCCESS;
}

/*
 * Called by scsi stack when something has really gone wrong.
 */
static int sbp2scsi_reset(struct scsi_cmnd *SCpnt)
{
	struct sbp2_lu *lu = (struct sbp2_lu *)SCpnt->device->host->hostdata[0];

	SBP2_INFO("reset requested");

	if (sbp2util_node_is_available(lu)) {
		SBP2_INFO("generating sbp2 fetch agent reset");
		sbp2_agent_reset(lu, 1);
	}

	return SUCCESS;
}

static ssize_t sbp2_sysfs_ieee1394_id_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct scsi_device *sdev;
	struct sbp2_lu *lu;

	if (!(sdev = to_scsi_device(dev)))
		return 0;

	if (!(lu = (struct sbp2_lu *)sdev->host->hostdata[0]))
		return 0;

	if (sbp2_long_sysfs_ieee1394_id)
		return sprintf(buf, "%016Lx:%06x:%04x\n",
				(unsigned long long)lu->ne->guid,
				lu->ud->directory_id, ORB_SET_LUN(lu->lun));
	else
		return sprintf(buf, "%016Lx:%d:%d\n",
				(unsigned long long)lu->ne->guid,
				lu->ud->id, ORB_SET_LUN(lu->lun));
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
