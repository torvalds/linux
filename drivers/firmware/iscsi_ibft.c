/*
 *  Copyright 2007 Red Hat, Inc.
 *  by Peter Jones <pjones@redhat.com>
 *  Copyright 2008 IBM, Inc.
 *  by Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *  Copyright 2008
 *  by Konrad Rzeszutek <ketuzsezr@darnok.org>
 *
 * This code exposes the iSCSI Boot Format Table to userland via sysfs.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Changelog:
 *
 *  14 Mar 2008 - Konrad Rzeszutek <ketuzsezr@darnok.org>
 *    Updated comments and copyrights. (v0.4.9)
 *
 *  11 Feb 2008 - Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *    Converted to using ibft_addr. (v0.4.8)
 *
 *   8 Feb 2008 - Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *    Combined two functions in one: reserve_ibft_region. (v0.4.7)
 *
 *  30 Jan 2008 - Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *   Added logic to handle IPv6 addresses. (v0.4.6)
 *
 *  25 Jan 2008 - Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *   Added logic to handle badly not-to-spec iBFT. (v0.4.5)
 *
 *   4 Jan 2008 - Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *   Added __init to function declarations. (v0.4.4)
 *
 *  21 Dec 2007 - Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *   Updated kobject registration, combined unregister functions in one
 *   and code and style cleanup. (v0.4.3)
 *
 *   5 Dec 2007 - Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *   Added end-markers to enums and re-organized kobject registration. (v0.4.2)
 *
 *   4 Dec 2007 - Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *   Created 'device' sysfs link to the NIC and style cleanup. (v0.4.1)
 *
 *  28 Nov 2007 - Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *   Added sysfs-ibft documentation, moved 'find_ibft' function to
 *   in its own file and added text attributes for every struct field.  (v0.4)
 *
 *  21 Nov 2007 - Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *   Added text attributes emulating OpenFirmware /proc/device-tree naming.
 *   Removed binary /sysfs interface (v0.3)
 *
 *  29 Aug 2007 - Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *   Added functionality in setup.c to reserve iBFT region. (v0.2)
 *
 *  27 Aug 2007 - Konrad Rzeszutek <konradr@linux.vnet.ibm.com>
 *   First version exposing iBFT data via a binary /sysfs. (v0.1)
 *
 */


#include <linux/blkdev.h>
#include <linux/capability.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/iscsi_ibft.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>

#define IBFT_ISCSI_VERSION "0.4.9"
#define IBFT_ISCSI_DATE "2008-Mar-14"

MODULE_AUTHOR("Peter Jones <pjones@redhat.com> and \
Konrad Rzeszutek <ketuzsezr@darnok.org>");
MODULE_DESCRIPTION("sysfs interface to BIOS iBFT information");
MODULE_LICENSE("GPL");
MODULE_VERSION(IBFT_ISCSI_VERSION);

struct ibft_hdr {
	u8 id;
	u8 version;
	u16 length;
	u8 index;
	u8 flags;
} __attribute__((__packed__));

struct ibft_control {
	struct ibft_hdr hdr;
	u16 extensions;
	u16 initiator_off;
	u16 nic0_off;
	u16 tgt0_off;
	u16 nic1_off;
	u16 tgt1_off;
} __attribute__((__packed__));

struct ibft_initiator {
	struct ibft_hdr hdr;
	char isns_server[16];
	char slp_server[16];
	char pri_radius_server[16];
	char sec_radius_server[16];
	u16 initiator_name_len;
	u16 initiator_name_off;
} __attribute__((__packed__));

struct ibft_nic {
	struct ibft_hdr hdr;
	char ip_addr[16];
	u8 subnet_mask_prefix;
	u8 origin;
	char gateway[16];
	char primary_dns[16];
	char secondary_dns[16];
	char dhcp[16];
	u16 vlan;
	char mac[6];
	u16 pci_bdf;
	u16 hostname_len;
	u16 hostname_off;
} __attribute__((__packed__));

struct ibft_tgt {
	struct ibft_hdr hdr;
	char ip_addr[16];
	u16 port;
	char lun[8];
	u8 chap_type;
	u8 nic_assoc;
	u16 tgt_name_len;
	u16 tgt_name_off;
	u16 chap_name_len;
	u16 chap_name_off;
	u16 chap_secret_len;
	u16 chap_secret_off;
	u16 rev_chap_name_len;
	u16 rev_chap_name_off;
	u16 rev_chap_secret_len;
	u16 rev_chap_secret_off;
} __attribute__((__packed__));

/*
 * The kobject different types and its names.
 *
*/
enum ibft_id {
	id_reserved = 0, /* We don't support. */
	id_control = 1, /* Should show up only once and is not exported. */
	id_initiator = 2,
	id_nic = 3,
	id_target = 4,
	id_extensions = 5, /* We don't support. */
	id_end_marker,
};

/*
 * We do not support the other types, hence the usage of NULL.
 * This maps to the enum ibft_id.
 */
static const char *ibft_id_names[] =
	{NULL, NULL, "initiator", "ethernet%d", "target%d", NULL, NULL};

/*
 * The text attributes names for each of the kobjects.
*/
enum ibft_eth_properties_enum {
	ibft_eth_index,
	ibft_eth_flags,
	ibft_eth_ip_addr,
	ibft_eth_subnet_mask,
	ibft_eth_origin,
	ibft_eth_gateway,
	ibft_eth_primary_dns,
	ibft_eth_secondary_dns,
	ibft_eth_dhcp,
	ibft_eth_vlan,
	ibft_eth_mac,
	/* ibft_eth_pci_bdf - this is replaced by link to the device itself. */
	ibft_eth_hostname,
	ibft_eth_end_marker,
};

static const char *ibft_eth_properties[] =
	{"index", "flags", "ip-addr", "subnet-mask", "origin", "gateway",
	"primary-dns", "secondary-dns", "dhcp", "vlan", "mac", "hostname",
	NULL};

enum ibft_tgt_properties_enum {
	ibft_tgt_index,
	ibft_tgt_flags,
	ibft_tgt_ip_addr,
	ibft_tgt_port,
	ibft_tgt_lun,
	ibft_tgt_chap_type,
	ibft_tgt_nic_assoc,
	ibft_tgt_name,
	ibft_tgt_chap_name,
	ibft_tgt_chap_secret,
	ibft_tgt_rev_chap_name,
	ibft_tgt_rev_chap_secret,
	ibft_tgt_end_marker,
};

static const char *ibft_tgt_properties[] =
	{"index", "flags", "ip-addr", "port", "lun", "chap-type", "nic-assoc",
	"target-name", "chap-name", "chap-secret", "rev-chap-name",
	"rev-chap-name-secret", NULL};

enum ibft_initiator_properties_enum {
	ibft_init_index,
	ibft_init_flags,
	ibft_init_isns_server,
	ibft_init_slp_server,
	ibft_init_pri_radius_server,
	ibft_init_sec_radius_server,
	ibft_init_initiator_name,
	ibft_init_end_marker,
};

static const char *ibft_initiator_properties[] =
	{"index", "flags", "isns-server", "slp-server", "pri-radius-server",
	"sec-radius-server", "initiator-name", NULL};

/*
 * The kobject and attribute structures.
 */

struct ibft_kobject {
	struct ibft_table_header *header;
	union {
		struct ibft_initiator *initiator;
		struct ibft_nic *nic;
		struct ibft_tgt *tgt;
		struct ibft_hdr *hdr;
	};
	struct kobject kobj;
	struct list_head node;
};

struct ibft_attribute {
	struct attribute attr;
	ssize_t (*show) (struct  ibft_kobject *entry,
			 struct ibft_attribute *attr, char *buf);
	union {
		struct ibft_initiator *initiator;
		struct ibft_nic *nic;
		struct ibft_tgt *tgt;
		struct ibft_hdr *hdr;
	};
	struct kobject *kobj;
	int type; /* The enum of the type. This can be any value of:
		ibft_eth_properties_enum, ibft_tgt_properties_enum,
		or ibft_initiator_properties_enum. */
	struct list_head node;
};

static LIST_HEAD(ibft_attr_list);
static LIST_HEAD(ibft_kobject_list);

static const char nulls[16];

/*
 * Helper functions to parse data properly.
 */
static ssize_t sprintf_ipaddr(char *buf, u8 *ip)
{
	char *str = buf;

	if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0 &&
	    ip[4] == 0 && ip[5] == 0 && ip[6] == 0 && ip[7] == 0 &&
	    ip[8] == 0 && ip[9] == 0 && ip[10] == 0xff && ip[11] == 0xff) {
		/*
		 * IPV4
		 */
		str += sprintf(buf, "%pI4", ip + 12);
	} else {
		/*
		 * IPv6
		 */
		str += sprintf(str, "%pI6", ip);
	}
	str += sprintf(str, "\n");
	return str - buf;
}

static ssize_t sprintf_string(char *str, int len, char *buf)
{
	return sprintf(str, "%.*s\n", len, buf);
}

/*
 * Helper function to verify the IBFT header.
 */
static int ibft_verify_hdr(char *t, struct ibft_hdr *hdr, int id, int length)
{
	if (hdr->id != id) {
		printk(KERN_ERR "iBFT error: We expected the " \
				"field header.id to have %d but " \
				"found %d instead!\n", id, hdr->id);
		return -ENODEV;
	}
	if (hdr->length != length) {
		printk(KERN_ERR "iBFT error: We expected the " \
				"field header.length to have %d but " \
				"found %d instead!\n", length, hdr->length);
		return -ENODEV;
	}

	return 0;
}

static void ibft_release(struct kobject *kobj)
{
	struct ibft_kobject *ibft =
		container_of(kobj, struct ibft_kobject, kobj);
	kfree(ibft);
}

/*
 *  Routines for parsing the iBFT data to be human readable.
 */
static ssize_t ibft_attr_show_initiator(struct ibft_kobject *entry,
					struct ibft_attribute *attr,
					char *buf)
{
	struct ibft_initiator *initiator = entry->initiator;
	void *ibft_loc = entry->header;
	char *str = buf;

	if (!initiator)
		return 0;

	switch (attr->type) {
	case ibft_init_index:
		str += sprintf(str, "%d\n", initiator->hdr.index);
		break;
	case ibft_init_flags:
		str += sprintf(str, "%d\n", initiator->hdr.flags);
		break;
	case ibft_init_isns_server:
		str += sprintf_ipaddr(str, initiator->isns_server);
		break;
	case ibft_init_slp_server:
		str += sprintf_ipaddr(str, initiator->slp_server);
		break;
	case ibft_init_pri_radius_server:
		str += sprintf_ipaddr(str, initiator->pri_radius_server);
		break;
	case ibft_init_sec_radius_server:
		str += sprintf_ipaddr(str, initiator->sec_radius_server);
		break;
	case ibft_init_initiator_name:
		str += sprintf_string(str, initiator->initiator_name_len,
				      (char *)ibft_loc +
				      initiator->initiator_name_off);
		break;
	default:
		break;
	}

	return str - buf;
}

static ssize_t ibft_attr_show_nic(struct ibft_kobject *entry,
				  struct ibft_attribute *attr,
				  char *buf)
{
	struct ibft_nic *nic = entry->nic;
	void *ibft_loc = entry->header;
	char *str = buf;
	char *mac;
	__be32 val;

	if (!nic)
		return 0;

	switch (attr->type) {
	case ibft_eth_index:
		str += sprintf(str, "%d\n", nic->hdr.index);
		break;
	case ibft_eth_flags:
		str += sprintf(str, "%d\n", nic->hdr.flags);
		break;
	case ibft_eth_ip_addr:
		str += sprintf_ipaddr(str, nic->ip_addr);
		break;
	case ibft_eth_subnet_mask:
		val = cpu_to_be32(~((1 << (32-nic->subnet_mask_prefix))-1));
		str += sprintf(str, "%pI4", &val);
		break;
	case ibft_eth_origin:
		str += sprintf(str, "%d\n", nic->origin);
		break;
	case ibft_eth_gateway:
		str += sprintf_ipaddr(str, nic->gateway);
		break;
	case ibft_eth_primary_dns:
		str += sprintf_ipaddr(str, nic->primary_dns);
		break;
	case ibft_eth_secondary_dns:
		str += sprintf_ipaddr(str, nic->secondary_dns);
		break;
	case ibft_eth_dhcp:
		str += sprintf_ipaddr(str, nic->dhcp);
		break;
	case ibft_eth_vlan:
		str += sprintf(str, "%d\n", nic->vlan);
		break;
	case ibft_eth_mac:
		mac = nic->mac;
		str += sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			       (u8)mac[0], (u8)mac[1], (u8)mac[2],
			       (u8)mac[3], (u8)mac[4], (u8)mac[5]);
		break;
	case ibft_eth_hostname:
		str += sprintf_string(str, nic->hostname_len,
				      (char *)ibft_loc + nic->hostname_off);
		break;
	default:
		break;
	}

	return str - buf;
};

static ssize_t ibft_attr_show_target(struct ibft_kobject *entry,
				     struct ibft_attribute *attr,
				     char *buf)
{
	struct ibft_tgt *tgt = entry->tgt;
	void *ibft_loc = entry->header;
	char *str = buf;
	int i;

	if (!tgt)
		return 0;

	switch (attr->type) {
	case ibft_tgt_index:
		str += sprintf(str, "%d\n", tgt->hdr.index);
		break;
	case ibft_tgt_flags:
		str += sprintf(str, "%d\n", tgt->hdr.flags);
		break;
	case ibft_tgt_ip_addr:
		str += sprintf_ipaddr(str, tgt->ip_addr);
		break;
	case ibft_tgt_port:
		str += sprintf(str, "%d\n", tgt->port);
		break;
	case ibft_tgt_lun:
		for (i = 0; i < 8; i++)
			str += sprintf(str, "%x", (u8)tgt->lun[i]);
		str += sprintf(str, "\n");
		break;
	case ibft_tgt_nic_assoc:
		str += sprintf(str, "%d\n", tgt->nic_assoc);
		break;
	case ibft_tgt_chap_type:
		str += sprintf(str, "%d\n", tgt->chap_type);
		break;
	case ibft_tgt_name:
		str += sprintf_string(str, tgt->tgt_name_len,
				      (char *)ibft_loc + tgt->tgt_name_off);
		break;
	case ibft_tgt_chap_name:
		str += sprintf_string(str, tgt->chap_name_len,
				      (char *)ibft_loc + tgt->chap_name_off);
		break;
	case ibft_tgt_chap_secret:
		str += sprintf_string(str, tgt->chap_secret_len,
				      (char *)ibft_loc + tgt->chap_secret_off);
		break;
	case ibft_tgt_rev_chap_name:
		str += sprintf_string(str, tgt->rev_chap_name_len,
				      (char *)ibft_loc +
				      tgt->rev_chap_name_off);
		break;
	case ibft_tgt_rev_chap_secret:
		str += sprintf_string(str, tgt->rev_chap_secret_len,
				      (char *)ibft_loc +
				      tgt->rev_chap_secret_off);
		break;
	default:
		break;
	}

	return str - buf;
}

/*
 * The routine called for all sysfs attributes.
 */
static ssize_t ibft_show_attribute(struct kobject *kobj,
				    struct attribute *attr,
				    char *buf)
{
	struct ibft_kobject *dev =
		container_of(kobj, struct ibft_kobject, kobj);
	struct ibft_attribute *ibft_attr =
		container_of(attr, struct ibft_attribute, attr);
	ssize_t ret = -EIO;
	char *str = buf;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (ibft_attr->show)
		ret = ibft_attr->show(dev, ibft_attr, str);

	return ret;
}

static struct sysfs_ops ibft_attr_ops = {
	.show = ibft_show_attribute,
};

static struct kobj_type ibft_ktype = {
	.release = ibft_release,
	.sysfs_ops = &ibft_attr_ops,
};

static struct kset *ibft_kset;

static int __init ibft_check_device(void)
{
	int len;
	u8 *pos;
	u8 csum = 0;

	len = ibft_addr->length;

	/* Sanity checking of iBFT. */
	if (ibft_addr->revision != 1) {
		printk(KERN_ERR "iBFT module supports only revision 1, " \
				"while this is %d.\n", ibft_addr->revision);
		return -ENOENT;
	}
	for (pos = (u8 *)ibft_addr; pos < (u8 *)ibft_addr + len; pos++)
		csum += *pos;

	if (csum) {
		printk(KERN_ERR "iBFT has incorrect checksum (0x%x)!\n", csum);
		return -ENOENT;
	}

	return 0;
}

/*
 * Helper function for ibft_register_kobjects.
 */
static int __init ibft_create_kobject(struct ibft_table_header *header,
				       struct ibft_hdr *hdr,
				       struct list_head *list)
{
	struct ibft_kobject *ibft_kobj = NULL;
	struct ibft_nic *nic = (struct ibft_nic *)hdr;
	struct pci_dev *pci_dev;
	int rc = 0;

	ibft_kobj = kzalloc(sizeof(*ibft_kobj), GFP_KERNEL);
	if (!ibft_kobj)
		return -ENOMEM;

	ibft_kobj->header = header;
	ibft_kobj->hdr = hdr;

	switch (hdr->id) {
	case id_initiator:
		rc = ibft_verify_hdr("initiator", hdr, id_initiator,
				     sizeof(*ibft_kobj->initiator));
		break;
	case id_nic:
		rc = ibft_verify_hdr("ethernet", hdr, id_nic,
				     sizeof(*ibft_kobj->nic));
		break;
	case id_target:
		rc = ibft_verify_hdr("target", hdr, id_target,
				     sizeof(*ibft_kobj->tgt));
		break;
	case id_reserved:
	case id_control:
	case id_extensions:
		/* Fields which we don't support. Ignore them */
		rc = 1;
		break;
	default:
		printk(KERN_ERR "iBFT has unknown structure type (%d). " \
				"Report this bug to %.6s!\n", hdr->id,
				header->oem_id);
		rc = 1;
		break;
	}

	if (rc) {
		/* Skip adding this kobject, but exit with non-fatal error. */
		kfree(ibft_kobj);
		goto out_invalid_struct;
	}

	ibft_kobj->kobj.kset = ibft_kset;

	rc = kobject_init_and_add(&ibft_kobj->kobj, &ibft_ktype,
				  NULL, ibft_id_names[hdr->id], hdr->index);

	if (rc) {
		kfree(ibft_kobj);
		goto out;
	}

	kobject_uevent(&ibft_kobj->kobj, KOBJ_ADD);

	if (hdr->id == id_nic) {
		/*
		* We don't search for the device in other domains than
		* zero. This is because on x86 platforms the BIOS
		* executes only devices which are in domain 0. Furthermore, the
		* iBFT spec doesn't have a domain id field :-(
		*/
		pci_dev = pci_get_bus_and_slot((nic->pci_bdf & 0xff00) >> 8,
					       (nic->pci_bdf & 0xff));
		if (pci_dev) {
			rc = sysfs_create_link(&ibft_kobj->kobj,
					       &pci_dev->dev.kobj, "device");
			pci_dev_put(pci_dev);
		}
	}

	/* Nothing broke so lets add it to the list. */
	list_add_tail(&ibft_kobj->node, list);
out:
	return rc;
out_invalid_struct:
	/* Unsupported structs are skipped. */
	return 0;
}

/*
 * Scan the IBFT table structure for the NIC and Target fields. When
 * found add them on the passed-in list. We do not support the other
 * fields at this point, so they are skipped.
 */
static int __init ibft_register_kobjects(struct ibft_table_header *header,
					  struct list_head *list)
{
	struct ibft_control *control = NULL;
	void *ptr, *end;
	int rc = 0;
	u16 offset;
	u16 eot_offset;

	control = (void *)header + sizeof(*header);
	end = (void *)control + control->hdr.length;
	eot_offset = (void *)header + header->length - (void *)control;
	rc = ibft_verify_hdr("control", (struct ibft_hdr *)control, id_control,
			     sizeof(*control));

	/* iBFT table safety checking */
	rc |= ((control->hdr.index) ? -ENODEV : 0);
	if (rc) {
		printk(KERN_ERR "iBFT error: Control header is invalid!\n");
		return rc;
	}
	for (ptr = &control->initiator_off; ptr < end; ptr += sizeof(u16)) {
		offset = *(u16 *)ptr;
		if (offset && offset < header->length && offset < eot_offset) {
			rc = ibft_create_kobject(header,
						 (void *)header + offset,
						 list);
			if (rc)
				break;
		}
	}

	return rc;
}

static void ibft_unregister(struct list_head *attr_list,
			     struct list_head *kobj_list)
{
	struct ibft_kobject *data = NULL, *n;
	struct ibft_attribute *attr = NULL, *m;

	list_for_each_entry_safe(attr, m, attr_list, node) {
		sysfs_remove_file(attr->kobj, &attr->attr);
		list_del(&attr->node);
		kfree(attr);
	};
	list_del_init(attr_list);

	list_for_each_entry_safe(data, n, kobj_list, node) {
		list_del(&data->node);
		if (data->hdr->id == id_nic)
			sysfs_remove_link(&data->kobj, "device");
		kobject_put(&data->kobj);
	};
	list_del_init(kobj_list);
}

static int __init ibft_create_attribute(struct ibft_kobject *kobj_data,
					 int type,
					 const char *name,
					 ssize_t (*show)(struct ibft_kobject *,
							 struct ibft_attribute*,
							 char *buf),
					 struct list_head *list)
{
	struct ibft_attribute *attr = NULL;
	struct ibft_hdr *hdr = kobj_data->hdr;

	attr = kmalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	attr->attr.name = name;
	attr->attr.mode = S_IRUSR;

	attr->hdr = hdr;
	attr->show = show;
	attr->kobj = &kobj_data->kobj;
	attr->type = type;

	list_add_tail(&attr->node, list);

	return 0;
}

/*
 * Helper routiners to check to determine if the entry is valid
 * in the proper iBFT structure.
 */
static int __init ibft_check_nic_for(struct ibft_nic *nic, int entry)
{
	int rc = 0;

	switch (entry) {
	case ibft_eth_index:
	case ibft_eth_flags:
		rc = 1;
		break;
	case ibft_eth_ip_addr:
		if (memcmp(nic->ip_addr, nulls, sizeof(nic->ip_addr)))
			rc = 1;
		break;
	case ibft_eth_subnet_mask:
		if (nic->subnet_mask_prefix)
			rc = 1;
		break;
	case ibft_eth_origin:
		rc = 1;
		break;
	case ibft_eth_gateway:
		if (memcmp(nic->gateway, nulls, sizeof(nic->gateway)))
			rc = 1;
		break;
	case ibft_eth_primary_dns:
		if (memcmp(nic->primary_dns, nulls,
			   sizeof(nic->primary_dns)))
			rc = 1;
		break;
	case ibft_eth_secondary_dns:
		if (memcmp(nic->secondary_dns, nulls,
			   sizeof(nic->secondary_dns)))
			rc = 1;
		break;
	case ibft_eth_dhcp:
		if (memcmp(nic->dhcp, nulls, sizeof(nic->dhcp)))
			rc = 1;
		break;
	case ibft_eth_vlan:
	case ibft_eth_mac:
		rc = 1;
		break;
	case ibft_eth_hostname:
		if (nic->hostname_off)
			rc = 1;
		break;
	default:
		break;
	}

	return rc;
}

static int __init ibft_check_tgt_for(struct ibft_tgt *tgt, int entry)
{
	int rc = 0;

	switch (entry) {
	case ibft_tgt_index:
	case ibft_tgt_flags:
	case ibft_tgt_ip_addr:
	case ibft_tgt_port:
	case ibft_tgt_lun:
	case ibft_tgt_nic_assoc:
	case ibft_tgt_chap_type:
		rc = 1;
	case ibft_tgt_name:
		if (tgt->tgt_name_len)
			rc = 1;
		break;
	case ibft_tgt_chap_name:
	case ibft_tgt_chap_secret:
		if (tgt->chap_name_len)
			rc = 1;
		break;
	case ibft_tgt_rev_chap_name:
	case ibft_tgt_rev_chap_secret:
		if (tgt->rev_chap_name_len)
			rc = 1;
		break;
	default:
		break;
	}

	return rc;
}

static int __init ibft_check_initiator_for(struct ibft_initiator *init,
					    int entry)
{
	int rc = 0;

	switch (entry) {
	case ibft_init_index:
	case ibft_init_flags:
		rc = 1;
		break;
	case ibft_init_isns_server:
		if (memcmp(init->isns_server, nulls,
			   sizeof(init->isns_server)))
			rc = 1;
		break;
	case ibft_init_slp_server:
		if (memcmp(init->slp_server, nulls,
			   sizeof(init->slp_server)))
			rc = 1;
		break;
	case ibft_init_pri_radius_server:
		if (memcmp(init->pri_radius_server, nulls,
			   sizeof(init->pri_radius_server)))
			rc = 1;
		break;
	case ibft_init_sec_radius_server:
		if (memcmp(init->sec_radius_server, nulls,
			   sizeof(init->sec_radius_server)))
			rc = 1;
		break;
	case ibft_init_initiator_name:
		if (init->initiator_name_len)
			rc = 1;
		break;
	default:
		break;
	}

	return rc;
}

/*
 *  Register the attributes for all of the kobjects.
 */
static int __init ibft_register_attributes(struct list_head *kobject_list,
					    struct list_head *attr_list)
{
	int rc = 0, i = 0;
	struct ibft_kobject *data = NULL;
	struct ibft_attribute *attr = NULL, *m;

	list_for_each_entry(data, kobject_list, node) {
		switch (data->hdr->id) {
		case id_nic:
			for (i = 0; i < ibft_eth_end_marker && !rc; i++)
				if (ibft_check_nic_for(data->nic, i))
					rc = ibft_create_attribute(data, i,
						ibft_eth_properties[i],
						ibft_attr_show_nic, attr_list);
			break;
		case id_target:
			for (i = 0; i < ibft_tgt_end_marker && !rc; i++)
				if (ibft_check_tgt_for(data->tgt, i))
					rc = ibft_create_attribute(data, i,
						ibft_tgt_properties[i],
						ibft_attr_show_target,
						attr_list);
			break;
		case id_initiator:
			for (i = 0; i < ibft_init_end_marker && !rc; i++)
				if (ibft_check_initiator_for(
					data->initiator, i))
					rc = ibft_create_attribute(data, i,
						ibft_initiator_properties[i],
						ibft_attr_show_initiator,
						attr_list);
			break;
		default:
			break;
		}
		if (rc)
			break;
	}
	list_for_each_entry_safe(attr, m, attr_list, node) {
		rc = sysfs_create_file(attr->kobj, &attr->attr);
		if (rc) {
			list_del(&attr->node);
			kfree(attr);
			break;
		}
	}

	return rc;
}

/*
 * ibft_init() - creates sysfs tree entries for the iBFT data.
 */
static int __init ibft_init(void)
{
	int rc = 0;

	ibft_kset = kset_create_and_add("ibft", NULL, firmware_kobj);
	if (!ibft_kset)
		return -ENOMEM;

	if (ibft_addr) {
		printk(KERN_INFO "iBFT detected at 0x%llx.\n",
		       (u64)isa_virt_to_bus(ibft_addr));

		rc = ibft_check_device();
		if (rc)
			goto out_firmware_unregister;

		/* Scan the IBFT for data and register the kobjects. */
		rc = ibft_register_kobjects(ibft_addr, &ibft_kobject_list);
		if (rc)
			goto out_free;

		/* Register the attributes */
		rc = ibft_register_attributes(&ibft_kobject_list,
					      &ibft_attr_list);
		if (rc)
			goto out_free;
	} else
		printk(KERN_INFO "No iBFT detected.\n");

	return 0;

out_free:
	ibft_unregister(&ibft_attr_list, &ibft_kobject_list);
out_firmware_unregister:
	kset_unregister(ibft_kset);
	return rc;
}

static void __exit ibft_exit(void)
{
	ibft_unregister(&ibft_attr_list, &ibft_kobject_list);
	kset_unregister(ibft_kset);
}

module_init(ibft_init);
module_exit(ibft_exit);
