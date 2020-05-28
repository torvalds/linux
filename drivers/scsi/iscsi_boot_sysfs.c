/*
 * Export the iSCSI boot info to userland via sysfs.
 *
 * Copyright (C) 2010 Red Hat, Inc.  All rights reserved.
 * Copyright (C) 2010 Mike Christie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/capability.h>
#include <linux/iscsi_boot_sysfs.h>


MODULE_AUTHOR("Mike Christie <michaelc@cs.wisc.edu>");
MODULE_DESCRIPTION("sysfs interface and helpers to export iSCSI boot information");
MODULE_LICENSE("GPL");
/*
 * The kobject and attribute structures.
 */
struct iscsi_boot_attr {
	struct attribute attr;
	int type;
	ssize_t (*show) (void *data, int type, char *buf);
};

/*
 * The routine called for all sysfs attributes.
 */
static ssize_t iscsi_boot_show_attribute(struct kobject *kobj,
					 struct attribute *attr, char *buf)
{
	struct iscsi_boot_kobj *boot_kobj =
			container_of(kobj, struct iscsi_boot_kobj, kobj);
	struct iscsi_boot_attr *boot_attr =
			container_of(attr, struct iscsi_boot_attr, attr);
	ssize_t ret = -EIO;
	char *str = buf;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (boot_kobj->show)
		ret = boot_kobj->show(boot_kobj->data, boot_attr->type, str);
	return ret;
}

static const struct sysfs_ops iscsi_boot_attr_ops = {
	.show = iscsi_boot_show_attribute,
};

static void iscsi_boot_kobj_release(struct kobject *kobj)
{
	struct iscsi_boot_kobj *boot_kobj =
			container_of(kobj, struct iscsi_boot_kobj, kobj);

	if (boot_kobj->release)
		boot_kobj->release(boot_kobj->data);
	kfree(boot_kobj);
}

static struct kobj_type iscsi_boot_ktype = {
	.release = iscsi_boot_kobj_release,
	.sysfs_ops = &iscsi_boot_attr_ops,
};

#define iscsi_boot_rd_attr(fnname, sysfs_name, attr_type)		\
static struct iscsi_boot_attr iscsi_boot_attr_##fnname = {	\
	.attr	= { .name = __stringify(sysfs_name), .mode = 0444 },	\
	.type	= attr_type,						\
}

/* Target attrs */
iscsi_boot_rd_attr(tgt_index, index, ISCSI_BOOT_TGT_INDEX);
iscsi_boot_rd_attr(tgt_flags, flags, ISCSI_BOOT_TGT_FLAGS);
iscsi_boot_rd_attr(tgt_ip, ip-addr, ISCSI_BOOT_TGT_IP_ADDR);
iscsi_boot_rd_attr(tgt_port, port, ISCSI_BOOT_TGT_PORT);
iscsi_boot_rd_attr(tgt_lun, lun, ISCSI_BOOT_TGT_LUN);
iscsi_boot_rd_attr(tgt_chap, chap-type, ISCSI_BOOT_TGT_CHAP_TYPE);
iscsi_boot_rd_attr(tgt_nic, nic-assoc, ISCSI_BOOT_TGT_NIC_ASSOC);
iscsi_boot_rd_attr(tgt_name, target-name, ISCSI_BOOT_TGT_NAME);
iscsi_boot_rd_attr(tgt_chap_name, chap-name, ISCSI_BOOT_TGT_CHAP_NAME);
iscsi_boot_rd_attr(tgt_chap_secret, chap-secret, ISCSI_BOOT_TGT_CHAP_SECRET);
iscsi_boot_rd_attr(tgt_chap_rev_name, rev-chap-name,
		   ISCSI_BOOT_TGT_REV_CHAP_NAME);
iscsi_boot_rd_attr(tgt_chap_rev_secret, rev-chap-name-secret,
		   ISCSI_BOOT_TGT_REV_CHAP_SECRET);

static struct attribute *target_attrs[] = {
	&iscsi_boot_attr_tgt_index.attr,
	&iscsi_boot_attr_tgt_flags.attr,
	&iscsi_boot_attr_tgt_ip.attr,
	&iscsi_boot_attr_tgt_port.attr,
	&iscsi_boot_attr_tgt_lun.attr,
	&iscsi_boot_attr_tgt_chap.attr,
	&iscsi_boot_attr_tgt_nic.attr,
	&iscsi_boot_attr_tgt_name.attr,
	&iscsi_boot_attr_tgt_chap_name.attr,
	&iscsi_boot_attr_tgt_chap_secret.attr,
	&iscsi_boot_attr_tgt_chap_rev_name.attr,
	&iscsi_boot_attr_tgt_chap_rev_secret.attr,
	NULL
};

static umode_t iscsi_boot_tgt_attr_is_visible(struct kobject *kobj,
					     struct attribute *attr, int i)
{
	struct iscsi_boot_kobj *boot_kobj =
			container_of(kobj, struct iscsi_boot_kobj, kobj);

	if (attr ==  &iscsi_boot_attr_tgt_index.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_TGT_INDEX);
	else if (attr == &iscsi_boot_attr_tgt_flags.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_TGT_FLAGS);
	else if (attr == &iscsi_boot_attr_tgt_ip.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					      ISCSI_BOOT_TGT_IP_ADDR);
	else if (attr == &iscsi_boot_attr_tgt_port.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					      ISCSI_BOOT_TGT_PORT);
	else if (attr == &iscsi_boot_attr_tgt_lun.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					      ISCSI_BOOT_TGT_LUN);
	else if (attr == &iscsi_boot_attr_tgt_chap.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_TGT_CHAP_TYPE);
	else if (attr == &iscsi_boot_attr_tgt_nic.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_TGT_NIC_ASSOC);
	else if (attr == &iscsi_boot_attr_tgt_name.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_TGT_NAME);
	else if (attr == &iscsi_boot_attr_tgt_chap_name.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_TGT_CHAP_NAME);
	else if (attr == &iscsi_boot_attr_tgt_chap_secret.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_TGT_CHAP_SECRET);
	else if (attr == &iscsi_boot_attr_tgt_chap_rev_name.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_TGT_REV_CHAP_NAME);
	else if (attr == &iscsi_boot_attr_tgt_chap_rev_secret.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_TGT_REV_CHAP_SECRET);
	return 0;
}

static struct attribute_group iscsi_boot_target_attr_group = {
	.attrs = target_attrs,
	.is_visible = iscsi_boot_tgt_attr_is_visible,
};

/* Ethernet attrs */
iscsi_boot_rd_attr(eth_index, index, ISCSI_BOOT_ETH_INDEX);
iscsi_boot_rd_attr(eth_flags, flags, ISCSI_BOOT_ETH_FLAGS);
iscsi_boot_rd_attr(eth_ip, ip-addr, ISCSI_BOOT_ETH_IP_ADDR);
iscsi_boot_rd_attr(eth_prefix, prefix-len, ISCSI_BOOT_ETH_PREFIX_LEN);
iscsi_boot_rd_attr(eth_subnet, subnet-mask, ISCSI_BOOT_ETH_SUBNET_MASK);
iscsi_boot_rd_attr(eth_origin, origin, ISCSI_BOOT_ETH_ORIGIN);
iscsi_boot_rd_attr(eth_gateway, gateway, ISCSI_BOOT_ETH_GATEWAY);
iscsi_boot_rd_attr(eth_primary_dns, primary-dns, ISCSI_BOOT_ETH_PRIMARY_DNS);
iscsi_boot_rd_attr(eth_secondary_dns, secondary-dns,
		   ISCSI_BOOT_ETH_SECONDARY_DNS);
iscsi_boot_rd_attr(eth_dhcp, dhcp, ISCSI_BOOT_ETH_DHCP);
iscsi_boot_rd_attr(eth_vlan, vlan, ISCSI_BOOT_ETH_VLAN);
iscsi_boot_rd_attr(eth_mac, mac, ISCSI_BOOT_ETH_MAC);
iscsi_boot_rd_attr(eth_hostname, hostname, ISCSI_BOOT_ETH_HOSTNAME);

static struct attribute *ethernet_attrs[] = {
	&iscsi_boot_attr_eth_index.attr,
	&iscsi_boot_attr_eth_flags.attr,
	&iscsi_boot_attr_eth_ip.attr,
	&iscsi_boot_attr_eth_prefix.attr,
	&iscsi_boot_attr_eth_subnet.attr,
	&iscsi_boot_attr_eth_origin.attr,
	&iscsi_boot_attr_eth_gateway.attr,
	&iscsi_boot_attr_eth_primary_dns.attr,
	&iscsi_boot_attr_eth_secondary_dns.attr,
	&iscsi_boot_attr_eth_dhcp.attr,
	&iscsi_boot_attr_eth_vlan.attr,
	&iscsi_boot_attr_eth_mac.attr,
	&iscsi_boot_attr_eth_hostname.attr,
	NULL
};

static umode_t iscsi_boot_eth_attr_is_visible(struct kobject *kobj,
					     struct attribute *attr, int i)
{
	struct iscsi_boot_kobj *boot_kobj =
			container_of(kobj, struct iscsi_boot_kobj, kobj);

	if (attr ==  &iscsi_boot_attr_eth_index.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_INDEX);
	else if (attr ==  &iscsi_boot_attr_eth_flags.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_FLAGS);
	else if (attr ==  &iscsi_boot_attr_eth_ip.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_IP_ADDR);
	else if (attr ==  &iscsi_boot_attr_eth_prefix.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_PREFIX_LEN);
	else if (attr ==  &iscsi_boot_attr_eth_subnet.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_SUBNET_MASK);
	else if (attr ==  &iscsi_boot_attr_eth_origin.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_ORIGIN);
	else if (attr ==  &iscsi_boot_attr_eth_gateway.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_GATEWAY);
	else if (attr ==  &iscsi_boot_attr_eth_primary_dns.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_PRIMARY_DNS);
	else if (attr ==  &iscsi_boot_attr_eth_secondary_dns.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_SECONDARY_DNS);
	else if (attr ==  &iscsi_boot_attr_eth_dhcp.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_DHCP);
	else if (attr ==  &iscsi_boot_attr_eth_vlan.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_VLAN);
	else if (attr ==  &iscsi_boot_attr_eth_mac.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_MAC);
	else if (attr ==  &iscsi_boot_attr_eth_hostname.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ETH_HOSTNAME);
	return 0;
}

static struct attribute_group iscsi_boot_ethernet_attr_group = {
	.attrs = ethernet_attrs,
	.is_visible = iscsi_boot_eth_attr_is_visible,
};

/* Initiator attrs */
iscsi_boot_rd_attr(ini_index, index, ISCSI_BOOT_INI_INDEX);
iscsi_boot_rd_attr(ini_flags, flags, ISCSI_BOOT_INI_FLAGS);
iscsi_boot_rd_attr(ini_isns, isns-server, ISCSI_BOOT_INI_ISNS_SERVER);
iscsi_boot_rd_attr(ini_slp, slp-server, ISCSI_BOOT_INI_SLP_SERVER);
iscsi_boot_rd_attr(ini_primary_radius, pri-radius-server,
		   ISCSI_BOOT_INI_PRI_RADIUS_SERVER);
iscsi_boot_rd_attr(ini_secondary_radius, sec-radius-server,
		   ISCSI_BOOT_INI_SEC_RADIUS_SERVER);
iscsi_boot_rd_attr(ini_name, initiator-name, ISCSI_BOOT_INI_INITIATOR_NAME);

static struct attribute *initiator_attrs[] = {
	&iscsi_boot_attr_ini_index.attr,
	&iscsi_boot_attr_ini_flags.attr,
	&iscsi_boot_attr_ini_isns.attr,
	&iscsi_boot_attr_ini_slp.attr,
	&iscsi_boot_attr_ini_primary_radius.attr,
	&iscsi_boot_attr_ini_secondary_radius.attr,
	&iscsi_boot_attr_ini_name.attr,
	NULL
};

static umode_t iscsi_boot_ini_attr_is_visible(struct kobject *kobj,
					     struct attribute *attr, int i)
{
	struct iscsi_boot_kobj *boot_kobj =
			container_of(kobj, struct iscsi_boot_kobj, kobj);

	if (attr ==  &iscsi_boot_attr_ini_index.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_INI_INDEX);
	if (attr ==  &iscsi_boot_attr_ini_flags.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_INI_FLAGS);
	if (attr ==  &iscsi_boot_attr_ini_isns.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_INI_ISNS_SERVER);
	if (attr ==  &iscsi_boot_attr_ini_slp.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_INI_SLP_SERVER);
	if (attr ==  &iscsi_boot_attr_ini_primary_radius.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_INI_PRI_RADIUS_SERVER);
	if (attr ==  &iscsi_boot_attr_ini_secondary_radius.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_INI_SEC_RADIUS_SERVER);
	if (attr ==  &iscsi_boot_attr_ini_name.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_INI_INITIATOR_NAME);

	return 0;
}

static struct attribute_group iscsi_boot_initiator_attr_group = {
	.attrs = initiator_attrs,
	.is_visible = iscsi_boot_ini_attr_is_visible,
};

/* iBFT ACPI Table attributes */
iscsi_boot_rd_attr(acpitbl_signature, signature, ISCSI_BOOT_ACPITBL_SIGNATURE);
iscsi_boot_rd_attr(acpitbl_oem_id, oem_id, ISCSI_BOOT_ACPITBL_OEM_ID);
iscsi_boot_rd_attr(acpitbl_oem_table_id, oem_table_id,
		   ISCSI_BOOT_ACPITBL_OEM_TABLE_ID);

static struct attribute *acpitbl_attrs[] = {
	&iscsi_boot_attr_acpitbl_signature.attr,
	&iscsi_boot_attr_acpitbl_oem_id.attr,
	&iscsi_boot_attr_acpitbl_oem_table_id.attr,
	NULL
};

static umode_t iscsi_boot_acpitbl_attr_is_visible(struct kobject *kobj,
					     struct attribute *attr, int i)
{
	struct iscsi_boot_kobj *boot_kobj =
			container_of(kobj, struct iscsi_boot_kobj, kobj);

	if (attr ==  &iscsi_boot_attr_acpitbl_signature.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ACPITBL_SIGNATURE);
	if (attr ==  &iscsi_boot_attr_acpitbl_oem_id.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ACPITBL_OEM_ID);
	if (attr ==  &iscsi_boot_attr_acpitbl_oem_table_id.attr)
		return boot_kobj->is_visible(boot_kobj->data,
					     ISCSI_BOOT_ACPITBL_OEM_TABLE_ID);
	return 0;
}

static struct attribute_group iscsi_boot_acpitbl_attr_group = {
	.attrs = acpitbl_attrs,
	.is_visible = iscsi_boot_acpitbl_attr_is_visible,
};

static struct iscsi_boot_kobj *
iscsi_boot_create_kobj(struct iscsi_boot_kset *boot_kset,
		       struct attribute_group *attr_group,
		       const char *name, int index, void *data,
		       ssize_t (*show) (void *data, int type, char *buf),
		       umode_t (*is_visible) (void *data, int type),
		       void (*release) (void *data))
{
	struct iscsi_boot_kobj *boot_kobj;

	boot_kobj = kzalloc(sizeof(*boot_kobj), GFP_KERNEL);
	if (!boot_kobj)
		return NULL;
	INIT_LIST_HEAD(&boot_kobj->list);

	boot_kobj->kobj.kset = boot_kset->kset;
	if (kobject_init_and_add(&boot_kobj->kobj, &iscsi_boot_ktype,
				 NULL, name, index)) {
		kobject_put(&boot_kobj->kobj);
		return NULL;
	}
	boot_kobj->data = data;
	boot_kobj->show = show;
	boot_kobj->is_visible = is_visible;
	boot_kobj->release = release;

	if (sysfs_create_group(&boot_kobj->kobj, attr_group)) {
		/*
		 * We do not want to free this because the caller
		 * will assume that since the creation call failed
		 * the boot kobj was not setup and the normal release
		 * path is not being run.
		 */
		boot_kobj->release = NULL;
		kobject_put(&boot_kobj->kobj);
		return NULL;
	}
	boot_kobj->attr_group = attr_group;

	kobject_uevent(&boot_kobj->kobj, KOBJ_ADD);
	/* Nothing broke so lets add it to the list. */
	list_add_tail(&boot_kobj->list, &boot_kset->kobj_list);
	return boot_kobj;
}

static void iscsi_boot_remove_kobj(struct iscsi_boot_kobj *boot_kobj)
{
	list_del(&boot_kobj->list);
	sysfs_remove_group(&boot_kobj->kobj, boot_kobj->attr_group);
	kobject_put(&boot_kobj->kobj);
}

/**
 * iscsi_boot_create_target() - create boot target sysfs dir
 * @boot_kset: boot kset
 * @index: the target id
 * @data: driver specific data for target
 * @show: attr show function
 * @is_visible: attr visibility function
 * @release: release function
 *
 * Note: The boot sysfs lib will free the data passed in for the caller
 * when all refs to the target kobject have been released.
 */
struct iscsi_boot_kobj *
iscsi_boot_create_target(struct iscsi_boot_kset *boot_kset, int index,
			 void *data,
			 ssize_t (*show) (void *data, int type, char *buf),
			 umode_t (*is_visible) (void *data, int type),
			 void (*release) (void *data))
{
	return iscsi_boot_create_kobj(boot_kset, &iscsi_boot_target_attr_group,
				      "target%d", index, data, show, is_visible,
				      release);
}
EXPORT_SYMBOL_GPL(iscsi_boot_create_target);

/**
 * iscsi_boot_create_initiator() - create boot initiator sysfs dir
 * @boot_kset: boot kset
 * @index: the initiator id
 * @data: driver specific data
 * @show: attr show function
 * @is_visible: attr visibility function
 * @release: release function
 *
 * Note: The boot sysfs lib will free the data passed in for the caller
 * when all refs to the initiator kobject have been released.
 */
struct iscsi_boot_kobj *
iscsi_boot_create_initiator(struct iscsi_boot_kset *boot_kset, int index,
			    void *data,
			    ssize_t (*show) (void *data, int type, char *buf),
			    umode_t (*is_visible) (void *data, int type),
			    void (*release) (void *data))
{
	return iscsi_boot_create_kobj(boot_kset,
				      &iscsi_boot_initiator_attr_group,
				      "initiator", index, data, show,
				      is_visible, release);
}
EXPORT_SYMBOL_GPL(iscsi_boot_create_initiator);

/**
 * iscsi_boot_create_ethernet() - create boot ethernet sysfs dir
 * @boot_kset: boot kset
 * @index: the ethernet device id
 * @data: driver specific data
 * @show: attr show function
 * @is_visible: attr visibility function
 * @release: release function
 *
 * Note: The boot sysfs lib will free the data passed in for the caller
 * when all refs to the ethernet kobject have been released.
 */
struct iscsi_boot_kobj *
iscsi_boot_create_ethernet(struct iscsi_boot_kset *boot_kset, int index,
			   void *data,
			   ssize_t (*show) (void *data, int type, char *buf),
			   umode_t (*is_visible) (void *data, int type),
			   void (*release) (void *data))
{
	return iscsi_boot_create_kobj(boot_kset,
				      &iscsi_boot_ethernet_attr_group,
				      "ethernet%d", index, data, show,
				      is_visible, release);
}
EXPORT_SYMBOL_GPL(iscsi_boot_create_ethernet);

/**
 * iscsi_boot_create_acpitbl() - create boot acpi table sysfs dir
 * @boot_kset: boot kset
 * @index: not used
 * @data: driver specific data
 * @show: attr show function
 * @is_visible: attr visibility function
 * @release: release function
 *
 * Note: The boot sysfs lib will free the data passed in for the caller
 * when all refs to the acpitbl kobject have been released.
 */
struct iscsi_boot_kobj *
iscsi_boot_create_acpitbl(struct iscsi_boot_kset *boot_kset, int index,
			   void *data,
			   ssize_t (*show)(void *data, int type, char *buf),
			   umode_t (*is_visible)(void *data, int type),
			   void (*release)(void *data))
{
	return iscsi_boot_create_kobj(boot_kset,
				      &iscsi_boot_acpitbl_attr_group,
				      "acpi_header", index, data, show,
				      is_visible, release);
}
EXPORT_SYMBOL_GPL(iscsi_boot_create_acpitbl);

/**
 * iscsi_boot_create_kset() - creates root sysfs tree
 * @set_name: name of root dir
 */
struct iscsi_boot_kset *iscsi_boot_create_kset(const char *set_name)
{
	struct iscsi_boot_kset *boot_kset;

	boot_kset = kzalloc(sizeof(*boot_kset), GFP_KERNEL);
	if (!boot_kset)
		return NULL;

	boot_kset->kset = kset_create_and_add(set_name, NULL, firmware_kobj);
	if (!boot_kset->kset) {
		kfree(boot_kset);
		return NULL;
	}

	INIT_LIST_HEAD(&boot_kset->kobj_list);
	return boot_kset;
}
EXPORT_SYMBOL_GPL(iscsi_boot_create_kset);

/**
 * iscsi_boot_create_host_kset() - creates root sysfs tree for a scsi host
 * @hostno: host number of scsi host
 */
struct iscsi_boot_kset *iscsi_boot_create_host_kset(unsigned int hostno)
{
	struct iscsi_boot_kset *boot_kset;
	char *set_name;

	set_name = kasprintf(GFP_KERNEL, "iscsi_boot%u", hostno);
	if (!set_name)
		return NULL;

	boot_kset = iscsi_boot_create_kset(set_name);
	kfree(set_name);
	return boot_kset;
}
EXPORT_SYMBOL_GPL(iscsi_boot_create_host_kset);

/**
 * iscsi_boot_destroy_kset() - destroy kset and kobjects under it
 * @boot_kset: boot kset
 *
 * This will remove the kset and kobjects and attrs under it.
 */
void iscsi_boot_destroy_kset(struct iscsi_boot_kset *boot_kset)
{
	struct iscsi_boot_kobj *boot_kobj, *tmp_kobj;

	if (!boot_kset)
		return;

	list_for_each_entry_safe(boot_kobj, tmp_kobj,
				 &boot_kset->kobj_list, list)
		iscsi_boot_remove_kobj(boot_kobj);

	kset_unregister(boot_kset->kset);
	kfree(boot_kset);
}
EXPORT_SYMBOL_GPL(iscsi_boot_destroy_kset);
