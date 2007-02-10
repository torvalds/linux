/*
 * Copyright (C) 2000	Andreas E. Bombe
 *               2001	Ben Collins <bcollins@debian.org>
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

#ifndef _IEEE1394_NODEMGR_H
#define _IEEE1394_NODEMGR_H

#include <linux/device.h>
#include <asm/types.h>

#include "ieee1394_core.h"
#include "ieee1394_types.h"

struct csr1212_csr;
struct csr1212_keyval;
struct hpsb_host;
struct ieee1394_device_id;

/* '1' '3' '9' '4' in ASCII */
#define IEEE1394_BUSID_MAGIC	__constant_cpu_to_be32(0x31333934)

/* This is the start of a Node entry structure. It should be a stable API
 * for which to gather info from the Node Manager about devices attached
 * to the bus.  */
struct bus_options {
	u8	irmc;		/* Iso Resource Manager Capable */
	u8	cmc;		/* Cycle Master Capable */
	u8	isc;		/* Iso Capable */
	u8	bmc;		/* Bus Master Capable */
	u8	pmc;		/* Power Manager Capable (PNP spec) */
	u8	cyc_clk_acc;	/* Cycle clock accuracy */
	u8	max_rom;	/* Maximum block read supported in the CSR */
	u8	generation;	/* Incremented when configrom changes */
	u8	lnkspd;		/* Link speed */
	u16	max_rec;	/* Maximum packet size node can receive */
};

#define UNIT_DIRECTORY_VENDOR_ID		0x01
#define UNIT_DIRECTORY_MODEL_ID			0x02
#define UNIT_DIRECTORY_SPECIFIER_ID		0x04
#define UNIT_DIRECTORY_VERSION			0x08
#define UNIT_DIRECTORY_HAS_LUN_DIRECTORY	0x10
#define UNIT_DIRECTORY_LUN_DIRECTORY		0x20
#define UNIT_DIRECTORY_HAS_LUN			0x40

/*
 * A unit directory corresponds to a protocol supported by the
 * node. If a node supports eg. IP/1394 and AV/C, its config rom has a
 * unit directory for each of these protocols.
 */
struct unit_directory {
	struct node_entry *ne;	/* The node which this directory belongs to */
	octlet_t address;	/* Address of the unit directory on the node */
	u8 flags;		/* Indicates which entries were read */

	quadlet_t vendor_id;
	struct csr1212_keyval *vendor_name_kv;

	quadlet_t model_id;
	struct csr1212_keyval *model_name_kv;
	quadlet_t specifier_id;
	quadlet_t version;

	unsigned int id;

	int ignore_driver;

	int length;		/* Number of quadlets */

	struct device device;
	struct class_device class_dev;

	struct csr1212_keyval *ud_kv;
	u32 lun;		/* logical unit number immediate value */
};

struct node_entry {
	u64 guid;			/* GUID of this node */
	u32 guid_vendor_id;		/* Top 24bits of guid */

	struct hpsb_host *host;		/* Host this node is attached to */
	nodeid_t nodeid;		/* NodeID */
	struct bus_options busopt;	/* Bus Options */
	int needs_probe;
	unsigned int generation;	/* Synced with hpsb generation */

	/* The following is read from the config rom */
	u32 vendor_id;
	struct csr1212_keyval *vendor_name_kv;

	u32 capabilities;

	struct device device;
	struct class_device class_dev;

	/* Means this node is not attached anymore */
	int in_limbo;

	struct csr1212_csr *csr;
};

struct hpsb_protocol_driver {
	/* The name of the driver, e.g. SBP2 or IP1394 */
	const char *name;

	/*
	 * The device id table describing the protocols and/or devices
	 * supported by this driver.  This is used by the nodemgr to
	 * decide if a driver could support a given node, but the
	 * probe function below can implement further protocol
	 * dependent or vendor dependent checking.
	 */
	struct ieee1394_device_id *id_table;

	/*
	 * The update function is called when the node has just
	 * survived a bus reset, i.e. it is still present on the bus.
	 * However, it may be necessary to reestablish the connection
	 * or login into the node again, depending on the protocol. If the
	 * probe fails (returns non-zero), we unbind the driver from this
	 * device.
	 */
	int (*update)(struct unit_directory *ud);

	/* Our LDM structure */
	struct device_driver driver;
};

int __hpsb_register_protocol(struct hpsb_protocol_driver *, struct module *);
static inline int hpsb_register_protocol(struct hpsb_protocol_driver *driver)
{
	return __hpsb_register_protocol(driver, THIS_MODULE);
}

void hpsb_unregister_protocol(struct hpsb_protocol_driver *driver);

static inline int hpsb_node_entry_valid(struct node_entry *ne)
{
	return ne->generation == get_hpsb_generation(ne->host);
}

/*
 * This will fill in the given, pre-initialised hpsb_packet with the current
 * information from the node entry (host, node ID, generation number).  It will
 * return false if the node owning the GUID is not accessible (and not modify
 * the hpsb_packet) and return true otherwise.
 *
 * Note that packet sending may still fail in hpsb_send_packet if a bus reset
 * happens while you are trying to set up the packet (due to obsolete generation
 * number).  It will at least reliably fail so that you don't accidentally and
 * unknowingly send your packet to the wrong node.
 */
void hpsb_node_fill_packet(struct node_entry *ne, struct hpsb_packet *pkt);

int hpsb_node_read(struct node_entry *ne, u64 addr,
		   quadlet_t *buffer, size_t length);
int hpsb_node_write(struct node_entry *ne, u64 addr,
		    quadlet_t *buffer, size_t length);
int hpsb_node_lock(struct node_entry *ne, u64 addr,
		   int extcode, quadlet_t *data, quadlet_t arg);

/* Iterate the hosts, calling a given function with supplied data for each
 * host. */
int nodemgr_for_each_host(void *__data, int (*cb)(struct hpsb_host *, void *));

int init_ieee1394_nodemgr(void);
void cleanup_ieee1394_nodemgr(void);

/* The template for a host device */
extern struct device nodemgr_dev_template_host;

/* Bus attributes we export */
extern struct bus_attribute *const fw_bus_attrs[];

#endif /* _IEEE1394_NODEMGR_H */
