/*
 * Copyright 2004 Digi International (www.digi.com)
 *      Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 */

#include "dgrp_common.h"

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/serial_reg.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>


#define PORTSERVER_DIVIDEND 1843200
#define SERIAL_TYPE_NORMAL      1
#define SERIAL_TYPE_CALLOUT     2
#define SERIAL_TYPE_XPRINT      3


static struct class *dgrp_class;
static struct device *dgrp_class_nodes_dev;
static struct device *dgrp_class_global_settings_dev;


static ssize_t dgrp_class_version_show(struct class *class,
				       struct class_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", DIGI_VERSION);
}
static CLASS_ATTR(driver_version, 0400, dgrp_class_version_show, NULL);


static ssize_t dgrp_class_register_with_sysfs_show(struct device *c,
						   struct device_attribute *attr,
						   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "1\n");
}
static DEVICE_ATTR(register_with_sysfs, 0400,
		   dgrp_class_register_with_sysfs_show, NULL);


static ssize_t dgrp_class_pollrate_show(struct device *c,
					struct device_attribute *attr,
					char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", dgrp_poll_tick);
}

static ssize_t dgrp_class_pollrate_store(struct device *c,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	sscanf(buf, "0x%x\n", &dgrp_poll_tick);
	return count;
}
static DEVICE_ATTR(pollrate, 0600, dgrp_class_pollrate_show,
		   dgrp_class_pollrate_store);

static struct attribute *dgrp_sysfs_global_settings_entries[] = {
	&dev_attr_pollrate.attr,
	&dev_attr_register_with_sysfs.attr,
	NULL
};


static struct attribute_group dgrp_global_settings_attribute_group = {
	.name = NULL,
	.attrs = dgrp_sysfs_global_settings_entries,
};



void dgrp_create_class_sysfs_files(void)
{
	int ret = 0;
	int max_majors = 1U << (32 - MINORBITS);

	dgrp_class = class_create(THIS_MODULE, "digi_realport");
	ret = class_create_file(dgrp_class, &class_attr_driver_version);

	dgrp_class_global_settings_dev = device_create(dgrp_class, NULL,
		MKDEV(0, max_majors + 1), NULL, "driver_settings");

	ret = sysfs_create_group(&dgrp_class_global_settings_dev->kobj,
		&dgrp_global_settings_attribute_group);
	if (ret) {
		pr_alert("%s: failed to create sysfs global settings device attributes.\n",
			__func__);
		sysfs_remove_group(&dgrp_class_global_settings_dev->kobj,
			&dgrp_global_settings_attribute_group);
		return;
	}

	dgrp_class_nodes_dev = device_create(dgrp_class, NULL,
		MKDEV(0, max_majors + 2), NULL, "nodes");

}


void dgrp_remove_class_sysfs_files(void)
{
	struct nd_struct *nd;
	int max_majors = 1U << (32 - MINORBITS);

	list_for_each_entry(nd, &nd_struct_list, list)
		dgrp_remove_node_class_sysfs_files(nd);

	sysfs_remove_group(&dgrp_class_global_settings_dev->kobj,
		&dgrp_global_settings_attribute_group);

	class_remove_file(dgrp_class, &class_attr_driver_version);

	device_destroy(dgrp_class, MKDEV(0, max_majors + 1));
	device_destroy(dgrp_class, MKDEV(0, max_majors + 2));
	class_destroy(dgrp_class);
}

static ssize_t dgrp_node_state_show(struct device *c,
				    struct device_attribute *attr, char *buf)
{
	struct nd_struct *nd;

	if (!c)
		return 0;
	nd = (struct nd_struct *) dev_get_drvdata(c);
	if (!nd)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%s\n", ND_STATE_STR(nd->nd_state));
}

static DEVICE_ATTR(state, 0600, dgrp_node_state_show, NULL);

static ssize_t dgrp_node_description_show(struct device *c,
					  struct device_attribute *attr,
					  char *buf)
{
	struct nd_struct *nd;

	if (!c)
		return 0;
	nd = (struct nd_struct *) dev_get_drvdata(c);
	if (!nd)
		return 0;

	if (nd->nd_state == NS_READY && nd->nd_ps_desc)
		return snprintf(buf, PAGE_SIZE, "%s\n", nd->nd_ps_desc);
	return 0;
}
static DEVICE_ATTR(description_info, 0600, dgrp_node_description_show, NULL);

static ssize_t dgrp_node_hw_version_show(struct device *c,
					 struct device_attribute *attr,
					 char *buf)
{
	struct nd_struct *nd;

	if (!c)
		return 0;
	nd = (struct nd_struct *) dev_get_drvdata(c);
	if (!nd)
		return 0;

	if (nd->nd_state == NS_READY)
		return snprintf(buf, PAGE_SIZE, "%d.%d\n",
				(nd->nd_hw_ver >> 8) & 0xff,
				nd->nd_hw_ver & 0xff);

	return 0;
}
static DEVICE_ATTR(hw_version_info, 0600, dgrp_node_hw_version_show, NULL);

static ssize_t dgrp_node_hw_id_show(struct device *c,
				    struct device_attribute *attr, char *buf)
{
	struct nd_struct *nd;

	if (!c)
		return 0;
	nd = (struct nd_struct *) dev_get_drvdata(c);
	if (!nd)
		return 0;


	if (nd->nd_state == NS_READY)
		return snprintf(buf, PAGE_SIZE, "%d\n", nd->nd_hw_id);
	return 0;
}
static DEVICE_ATTR(hw_id_info, 0600, dgrp_node_hw_id_show, NULL);

static ssize_t dgrp_node_sw_version_show(struct device *c,
					 struct device_attribute *attr,
					 char *buf)
{
	struct nd_struct *nd;

	if (!c)
		return 0;

	nd = (struct nd_struct *) dev_get_drvdata(c);
	if (!nd)
		return 0;

	if (nd->nd_state == NS_READY)
		return snprintf(buf, PAGE_SIZE, "%d.%d\n",
				(nd->nd_sw_ver >> 8) & 0xff,
				nd->nd_sw_ver & 0xff);

	return 0;
}
static DEVICE_ATTR(sw_version_info, 0600, dgrp_node_sw_version_show, NULL);


static struct attribute *dgrp_sysfs_node_entries[] = {
	&dev_attr_state.attr,
	&dev_attr_description_info.attr,
	&dev_attr_hw_version_info.attr,
	&dev_attr_hw_id_info.attr,
	&dev_attr_sw_version_info.attr,
	NULL
};


static struct attribute_group dgrp_node_attribute_group = {
	.name = NULL,
	.attrs = dgrp_sysfs_node_entries,
};


void dgrp_create_node_class_sysfs_files(struct nd_struct *nd)
{
	int ret;
	char name[10];

	if (nd->nd_ID)
		ID_TO_CHAR(nd->nd_ID, name);
	else
		sprintf(name, "node%ld", nd->nd_major);

	nd->nd_class_dev = device_create(dgrp_class, dgrp_class_nodes_dev,
		MKDEV(0, nd->nd_major), NULL, name);

	ret = sysfs_create_group(&nd->nd_class_dev->kobj,
				 &dgrp_node_attribute_group);

	if (ret) {
		pr_alert("%s: failed to create sysfs node device attributes.\n",
			__func__);
		sysfs_remove_group(&nd->nd_class_dev->kobj,
				   &dgrp_node_attribute_group);
		return;
	}

	dev_set_drvdata(nd->nd_class_dev, nd);

}


void dgrp_remove_node_class_sysfs_files(struct nd_struct *nd)
{
	if (nd->nd_class_dev) {
		sysfs_remove_group(&nd->nd_class_dev->kobj,
				   &dgrp_node_attribute_group);

		device_destroy(dgrp_class, MKDEV(0, nd->nd_major));
		nd->nd_class_dev = NULL;
	}
}



static ssize_t dgrp_tty_state_show(struct device *d,
				   struct device_attribute *attr, char *buf)
{
	struct un_struct *un;

	if (!d)
		return 0;
	un = (struct un_struct *) dev_get_drvdata(d);
	if (!un)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%s\n",
			un->un_open_count ? "Open" : "Closed");
}
static DEVICE_ATTR(state_info, 0600, dgrp_tty_state_show, NULL);

static ssize_t dgrp_tty_baud_show(struct device *d,
				  struct device_attribute *attr, char *buf)
{
	struct ch_struct *ch;
	struct un_struct *un;

	if (!d)
		return 0;
	un = (struct un_struct *) dev_get_drvdata(d);
	if (!un)
		return 0;
	ch = un->un_ch;
	if (!ch)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d\n",
		un->un_open_count ? (PORTSERVER_DIVIDEND / ch->ch_s_brate) : 0);
}
static DEVICE_ATTR(baud_info, 0400, dgrp_tty_baud_show, NULL);


static ssize_t dgrp_tty_msignals_show(struct device *d,
				      struct device_attribute *attr, char *buf)
{
	struct ch_struct *ch;
	struct un_struct *un;

	if (!d)
		return 0;
	un = (struct un_struct *) dev_get_drvdata(d);
	if (!un)
		return 0;
	ch = un->un_ch;
	if (!ch)
		return 0;

	if (ch->ch_open_count) {
		return snprintf(buf, PAGE_SIZE, "%s %s %s %s %s %s\n",
			(ch->ch_s_mlast & DM_RTS) ? "RTS" : "",
			(ch->ch_s_mlast & DM_CTS) ? "CTS" : "",
			(ch->ch_s_mlast & DM_DTR) ? "DTR" : "",
			(ch->ch_s_mlast & DM_DSR) ? "DSR" : "",
			(ch->ch_s_mlast & DM_CD) ? "DCD" : "",
			(ch->ch_s_mlast & DM_RI)  ? "RI"  : "");
	}
	return 0;
}
static DEVICE_ATTR(msignals_info, 0400, dgrp_tty_msignals_show, NULL);


static ssize_t dgrp_tty_iflag_show(struct device *d,
				   struct device_attribute *attr, char *buf)
{
	struct ch_struct *ch;
	struct un_struct *un;

	if (!d)
		return 0;
	un = (struct un_struct *) dev_get_drvdata(d);
	if (!un)
		return 0;
	ch = un->un_ch;
	if (!ch)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_s_iflag);
}
static DEVICE_ATTR(iflag_info, 0600, dgrp_tty_iflag_show, NULL);


static ssize_t dgrp_tty_cflag_show(struct device *d,
				   struct device_attribute *attr, char *buf)
{
	struct ch_struct *ch;
	struct un_struct *un;

	if (!d)
		return 0;
	un = (struct un_struct *) dev_get_drvdata(d);
	if (!un)
		return 0;
	ch = un->un_ch;
	if (!ch)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_s_cflag);
}
static DEVICE_ATTR(cflag_info, 0600, dgrp_tty_cflag_show, NULL);


static ssize_t dgrp_tty_oflag_show(struct device *d,
				   struct device_attribute *attr, char *buf)
{
	struct ch_struct *ch;
	struct un_struct *un;

	if (!d)
		return 0;
	un = (struct un_struct *) dev_get_drvdata(d);
	if (!un)
		return 0;
	ch = un->un_ch;
	if (!ch)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_s_oflag);
}
static DEVICE_ATTR(oflag_info, 0600, dgrp_tty_oflag_show, NULL);


static ssize_t dgrp_tty_digi_flag_show(struct device *d,
				       struct device_attribute *attr, char *buf)
{
	struct ch_struct *ch;
	struct un_struct *un;

	if (!d)
		return 0;
	un = (struct un_struct *) dev_get_drvdata(d);
	if (!un)
		return 0;
	ch = un->un_ch;
	if (!ch)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_digi.digi_flags);
}
static DEVICE_ATTR(digi_flag_info, 0600, dgrp_tty_digi_flag_show, NULL);


static ssize_t dgrp_tty_rxcount_show(struct device *d,
				     struct device_attribute *attr, char *buf)
{
	struct ch_struct *ch;
	struct un_struct *un;

	if (!d)
		return 0;
	un = (struct un_struct *) dev_get_drvdata(d);
	if (!un)
		return 0;
	ch = un->un_ch;
	if (!ch)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d\n", ch->ch_rxcount);
}
static DEVICE_ATTR(rxcount_info, 0600, dgrp_tty_rxcount_show, NULL);


static ssize_t dgrp_tty_txcount_show(struct device *d,
				     struct device_attribute *attr, char *buf)
{
	struct ch_struct *ch;
	struct un_struct *un;

	if (!d)
		return 0;
	un = (struct un_struct *) dev_get_drvdata(d);
	if (!un)
		return 0;
	ch = un->un_ch;
	if (!ch)
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d\n", ch->ch_txcount);
}
static DEVICE_ATTR(txcount_info, 0600, dgrp_tty_txcount_show, NULL);


static ssize_t dgrp_tty_name_show(struct device *d,
				  struct device_attribute *attr, char *buf)
{
	struct nd_struct *nd;
	struct ch_struct *ch;
	struct un_struct *un;
	char name[10];

	if (!d)
		return 0;
	un = (struct un_struct *) dev_get_drvdata(d);
	if (!un)
		return 0;
	ch = un->un_ch;
	if (!ch)
		return 0;
	nd = ch->ch_nd;
	if (!nd)
		return 0;

	ID_TO_CHAR(nd->nd_ID, name);

	return snprintf(buf, PAGE_SIZE, "%s%s%02d\n",
		un->un_type == SERIAL_TYPE_XPRINT ? "pr" : "tty",
		name, ch->ch_portnum);
}
static DEVICE_ATTR(custom_name, 0600, dgrp_tty_name_show, NULL);


static struct attribute *dgrp_sysfs_tty_entries[] = {
	&dev_attr_state_info.attr,
	&dev_attr_baud_info.attr,
	&dev_attr_msignals_info.attr,
	&dev_attr_iflag_info.attr,
	&dev_attr_cflag_info.attr,
	&dev_attr_oflag_info.attr,
	&dev_attr_digi_flag_info.attr,
	&dev_attr_rxcount_info.attr,
	&dev_attr_txcount_info.attr,
	&dev_attr_custom_name.attr,
	NULL
};


static struct attribute_group dgrp_tty_attribute_group = {
	.name = NULL,
	.attrs = dgrp_sysfs_tty_entries,
};


void dgrp_create_tty_sysfs(struct un_struct *un, struct device *c)
{
	int ret;

	ret = sysfs_create_group(&c->kobj, &dgrp_tty_attribute_group);
	if (ret) {
		pr_alert("%s: failed to create sysfs tty device attributes.\n",
			__func__);
		sysfs_remove_group(&c->kobj, &dgrp_tty_attribute_group);
		return;
	}

	dev_set_drvdata(c, un);

}


void dgrp_remove_tty_sysfs(struct device *c)
{
	sysfs_remove_group(&c->kobj, &dgrp_tty_attribute_group);
}
