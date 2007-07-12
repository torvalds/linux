/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: sysfs.c 1349 2004-12-16 21:09:43Z roland $
 */

#include "core_priv.h"

#include <linux/slab.h>
#include <linux/string.h>

#include <rdma/ib_mad.h>

struct ib_port {
	struct kobject         kobj;
	struct ib_device      *ibdev;
	struct attribute_group gid_group;
	struct attribute_group pkey_group;
	u8                     port_num;
};

struct port_attribute {
	struct attribute attr;
	ssize_t (*show)(struct ib_port *, struct port_attribute *, char *buf);
	ssize_t (*store)(struct ib_port *, struct port_attribute *,
			 const char *buf, size_t count);
};

#define PORT_ATTR(_name, _mode, _show, _store) \
struct port_attribute port_attr_##_name = __ATTR(_name, _mode, _show, _store)

#define PORT_ATTR_RO(_name) \
struct port_attribute port_attr_##_name = __ATTR_RO(_name)

struct port_table_attribute {
	struct port_attribute	attr;
	char			name[8];
	int			index;
};

static inline int ibdev_is_alive(const struct ib_device *dev)
{
	return dev->reg_state == IB_DEV_REGISTERED;
}

static ssize_t port_attr_show(struct kobject *kobj,
			      struct attribute *attr, char *buf)
{
	struct port_attribute *port_attr =
		container_of(attr, struct port_attribute, attr);
	struct ib_port *p = container_of(kobj, struct ib_port, kobj);

	if (!port_attr->show)
		return -EIO;
	if (!ibdev_is_alive(p->ibdev))
		return -ENODEV;

	return port_attr->show(p, port_attr, buf);
}

static struct sysfs_ops port_sysfs_ops = {
	.show = port_attr_show
};

static ssize_t state_show(struct ib_port *p, struct port_attribute *unused,
			  char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	static const char *state_name[] = {
		[IB_PORT_NOP]		= "NOP",
		[IB_PORT_DOWN]		= "DOWN",
		[IB_PORT_INIT]		= "INIT",
		[IB_PORT_ARMED]		= "ARMED",
		[IB_PORT_ACTIVE]	= "ACTIVE",
		[IB_PORT_ACTIVE_DEFER]	= "ACTIVE_DEFER"
	};

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "%d: %s\n", attr.state,
		       attr.state >= 0 && attr.state < ARRAY_SIZE(state_name) ?
		       state_name[attr.state] : "UNKNOWN");
}

static ssize_t lid_show(struct ib_port *p, struct port_attribute *unused,
			char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "0x%x\n", attr.lid);
}

static ssize_t lid_mask_count_show(struct ib_port *p,
				   struct port_attribute *unused,
				   char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", attr.lmc);
}

static ssize_t sm_lid_show(struct ib_port *p, struct port_attribute *unused,
			   char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "0x%x\n", attr.sm_lid);
}

static ssize_t sm_sl_show(struct ib_port *p, struct port_attribute *unused,
			  char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", attr.sm_sl);
}

static ssize_t cap_mask_show(struct ib_port *p, struct port_attribute *unused,
			     char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "0x%08x\n", attr.port_cap_flags);
}

static ssize_t rate_show(struct ib_port *p, struct port_attribute *unused,
			 char *buf)
{
	struct ib_port_attr attr;
	char *speed = "";
	int rate;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	switch (attr.active_speed) {
	case 2: speed = " DDR"; break;
	case 4: speed = " QDR"; break;
	}

	rate = 25 * ib_width_enum_to_int(attr.active_width) * attr.active_speed;
	if (rate < 0)
		return -EINVAL;

	return sprintf(buf, "%d%s Gb/sec (%dX%s)\n",
		       rate / 10, rate % 10 ? ".5" : "",
		       ib_width_enum_to_int(attr.active_width), speed);
}

static ssize_t phys_state_show(struct ib_port *p, struct port_attribute *unused,
			       char *buf)
{
	struct ib_port_attr attr;

	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	switch (attr.phys_state) {
	case 1:  return sprintf(buf, "1: Sleep\n");
	case 2:  return sprintf(buf, "2: Polling\n");
	case 3:  return sprintf(buf, "3: Disabled\n");
	case 4:  return sprintf(buf, "4: PortConfigurationTraining\n");
	case 5:  return sprintf(buf, "5: LinkUp\n");
	case 6:  return sprintf(buf, "6: LinkErrorRecovery\n");
	case 7:  return sprintf(buf, "7: Phy Test\n");
	default: return sprintf(buf, "%d: <unknown>\n", attr.phys_state);
	}
}

static PORT_ATTR_RO(state);
static PORT_ATTR_RO(lid);
static PORT_ATTR_RO(lid_mask_count);
static PORT_ATTR_RO(sm_lid);
static PORT_ATTR_RO(sm_sl);
static PORT_ATTR_RO(cap_mask);
static PORT_ATTR_RO(rate);
static PORT_ATTR_RO(phys_state);

static struct attribute *port_default_attrs[] = {
	&port_attr_state.attr,
	&port_attr_lid.attr,
	&port_attr_lid_mask_count.attr,
	&port_attr_sm_lid.attr,
	&port_attr_sm_sl.attr,
	&port_attr_cap_mask.attr,
	&port_attr_rate.attr,
	&port_attr_phys_state.attr,
	NULL
};

static ssize_t show_port_gid(struct ib_port *p, struct port_attribute *attr,
			     char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	union ib_gid gid;
	ssize_t ret;

	ret = ib_query_gid(p->ibdev, p->port_num, tab_attr->index, &gid);
	if (ret)
		return ret;

	return sprintf(buf, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
		       be16_to_cpu(((__be16 *) gid.raw)[0]),
		       be16_to_cpu(((__be16 *) gid.raw)[1]),
		       be16_to_cpu(((__be16 *) gid.raw)[2]),
		       be16_to_cpu(((__be16 *) gid.raw)[3]),
		       be16_to_cpu(((__be16 *) gid.raw)[4]),
		       be16_to_cpu(((__be16 *) gid.raw)[5]),
		       be16_to_cpu(((__be16 *) gid.raw)[6]),
		       be16_to_cpu(((__be16 *) gid.raw)[7]));
}

static ssize_t show_port_pkey(struct ib_port *p, struct port_attribute *attr,
			      char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	u16 pkey;
	ssize_t ret;

	ret = ib_query_pkey(p->ibdev, p->port_num, tab_attr->index, &pkey);
	if (ret)
		return ret;

	return sprintf(buf, "0x%04x\n", pkey);
}

#define PORT_PMA_ATTR(_name, _counter, _width, _offset)			\
struct port_table_attribute port_pma_attr_##_name = {			\
	.attr  = __ATTR(_name, S_IRUGO, show_pma_counter, NULL),	\
	.index = (_offset) | ((_width) << 16) | ((_counter) << 24)	\
}

static ssize_t show_pma_counter(struct ib_port *p, struct port_attribute *attr,
				char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	int offset = tab_attr->index & 0xffff;
	int width  = (tab_attr->index >> 16) & 0xff;
	struct ib_mad *in_mad  = NULL;
	struct ib_mad *out_mad = NULL;
	ssize_t ret;

	if (!p->ibdev->process_mad)
		return sprintf(buf, "N/A (no PMA)\n");

	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *in_mad, GFP_KERNEL);
	if (!in_mad || !out_mad) {
		ret = -ENOMEM;
		goto out;
	}

	in_mad->mad_hdr.base_version  = 1;
	in_mad->mad_hdr.mgmt_class    = IB_MGMT_CLASS_PERF_MGMT;
	in_mad->mad_hdr.class_version = 1;
	in_mad->mad_hdr.method        = IB_MGMT_METHOD_GET;
	in_mad->mad_hdr.attr_id       = cpu_to_be16(0x12); /* PortCounters */

	in_mad->data[41] = p->port_num;	/* PortSelect field */

	if ((p->ibdev->process_mad(p->ibdev, IB_MAD_IGNORE_MKEY,
		 p->port_num, NULL, NULL, in_mad, out_mad) &
	     (IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY)) !=
	    (IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY)) {
		ret = -EINVAL;
		goto out;
	}

	switch (width) {
	case 4:
		ret = sprintf(buf, "%u\n", (out_mad->data[40 + offset / 8] >>
					    (4 - (offset % 8))) & 0xf);
		break;
	case 8:
		ret = sprintf(buf, "%u\n", out_mad->data[40 + offset / 8]);
		break;
	case 16:
		ret = sprintf(buf, "%u\n",
			      be16_to_cpup((__be16 *)(out_mad->data + 40 + offset / 8)));
		break;
	case 32:
		ret = sprintf(buf, "%u\n",
			      be32_to_cpup((__be32 *)(out_mad->data + 40 + offset / 8)));
		break;
	default:
		ret = 0;
	}

out:
	kfree(in_mad);
	kfree(out_mad);

	return ret;
}

static PORT_PMA_ATTR(symbol_error		    ,  0, 16,  32);
static PORT_PMA_ATTR(link_error_recovery	    ,  1,  8,  48);
static PORT_PMA_ATTR(link_downed		    ,  2,  8,  56);
static PORT_PMA_ATTR(port_rcv_errors		    ,  3, 16,  64);
static PORT_PMA_ATTR(port_rcv_remote_physical_errors,  4, 16,  80);
static PORT_PMA_ATTR(port_rcv_switch_relay_errors   ,  5, 16,  96);
static PORT_PMA_ATTR(port_xmit_discards		    ,  6, 16, 112);
static PORT_PMA_ATTR(port_xmit_constraint_errors    ,  7,  8, 128);
static PORT_PMA_ATTR(port_rcv_constraint_errors	    ,  8,  8, 136);
static PORT_PMA_ATTR(local_link_integrity_errors    ,  9,  4, 152);
static PORT_PMA_ATTR(excessive_buffer_overrun_errors, 10,  4, 156);
static PORT_PMA_ATTR(VL15_dropped		    , 11, 16, 176);
static PORT_PMA_ATTR(port_xmit_data		    , 12, 32, 192);
static PORT_PMA_ATTR(port_rcv_data		    , 13, 32, 224);
static PORT_PMA_ATTR(port_xmit_packets		    , 14, 32, 256);
static PORT_PMA_ATTR(port_rcv_packets		    , 15, 32, 288);

static struct attribute *pma_attrs[] = {
	&port_pma_attr_symbol_error.attr.attr,
	&port_pma_attr_link_error_recovery.attr.attr,
	&port_pma_attr_link_downed.attr.attr,
	&port_pma_attr_port_rcv_errors.attr.attr,
	&port_pma_attr_port_rcv_remote_physical_errors.attr.attr,
	&port_pma_attr_port_rcv_switch_relay_errors.attr.attr,
	&port_pma_attr_port_xmit_discards.attr.attr,
	&port_pma_attr_port_xmit_constraint_errors.attr.attr,
	&port_pma_attr_port_rcv_constraint_errors.attr.attr,
	&port_pma_attr_local_link_integrity_errors.attr.attr,
	&port_pma_attr_excessive_buffer_overrun_errors.attr.attr,
	&port_pma_attr_VL15_dropped.attr.attr,
	&port_pma_attr_port_xmit_data.attr.attr,
	&port_pma_attr_port_rcv_data.attr.attr,
	&port_pma_attr_port_xmit_packets.attr.attr,
	&port_pma_attr_port_rcv_packets.attr.attr,
	NULL
};

static struct attribute_group pma_group = {
	.name  = "counters",
	.attrs  = pma_attrs
};

static void ib_port_release(struct kobject *kobj)
{
	struct ib_port *p = container_of(kobj, struct ib_port, kobj);
	struct attribute *a;
	int i;

	for (i = 0; (a = p->gid_group.attrs[i]); ++i)
		kfree(a);

	kfree(p->gid_group.attrs);

	for (i = 0; (a = p->pkey_group.attrs[i]); ++i)
		kfree(a);

	kfree(p->pkey_group.attrs);

	kfree(p);
}

static struct kobj_type port_type = {
	.release       = ib_port_release,
	.sysfs_ops     = &port_sysfs_ops,
	.default_attrs = port_default_attrs
};

static void ib_device_release(struct class_device *cdev)
{
	struct ib_device *dev = container_of(cdev, struct ib_device, class_dev);

	kfree(dev);
}

static int ib_device_uevent(struct class_device *cdev, char **envp,
			    int num_envp, char *buf, int size)
{
	struct ib_device *dev = container_of(cdev, struct ib_device, class_dev);
	int i = 0, len = 0;

	if (add_uevent_var(envp, num_envp, &i, buf, size, &len,
			   "NAME=%s", dev->name))
		return -ENOMEM;

	/*
	 * It would be nice to pass the node GUID with the event...
	 */

	envp[i] = NULL;
	return 0;
}

static struct attribute **
alloc_group_attrs(ssize_t (*show)(struct ib_port *,
				  struct port_attribute *, char *buf),
		  int len)
{
	struct attribute **tab_attr;
	struct port_table_attribute *element;
	int i;

	tab_attr = kcalloc(1 + len, sizeof(struct attribute *), GFP_KERNEL);
	if (!tab_attr)
		return NULL;

	for (i = 0; i < len; i++) {
		element = kzalloc(sizeof(struct port_table_attribute),
				  GFP_KERNEL);
		if (!element)
			goto err;

		if (snprintf(element->name, sizeof(element->name),
			     "%d", i) >= sizeof(element->name)) {
			kfree(element);
			goto err;
		}

		element->attr.attr.name  = element->name;
		element->attr.attr.mode  = S_IRUGO;
		element->attr.show       = show;
		element->index		 = i;

		tab_attr[i] = &element->attr.attr;
	}

	return tab_attr;

err:
	while (--i >= 0)
		kfree(tab_attr[i]);
	kfree(tab_attr);
	return NULL;
}

static int add_port(struct ib_device *device, int port_num)
{
	struct ib_port *p;
	struct ib_port_attr attr;
	int i;
	int ret;

	ret = ib_query_port(device, port_num, &attr);
	if (ret)
		return ret;

	p = kzalloc(sizeof *p, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->ibdev      = device;
	p->port_num   = port_num;
	p->kobj.ktype = &port_type;

	p->kobj.parent = kobject_get(&device->ports_parent);
	if (!p->kobj.parent) {
		ret = -EBUSY;
		goto err;
	}

	ret = kobject_set_name(&p->kobj, "%d", port_num);
	if (ret)
		goto err_put;

	ret = kobject_register(&p->kobj);
	if (ret)
		goto err_put;

	ret = sysfs_create_group(&p->kobj, &pma_group);
	if (ret)
		goto err_put;

	p->gid_group.name  = "gids";
	p->gid_group.attrs = alloc_group_attrs(show_port_gid, attr.gid_tbl_len);
	if (!p->gid_group.attrs)
		goto err_remove_pma;

	ret = sysfs_create_group(&p->kobj, &p->gid_group);
	if (ret)
		goto err_free_gid;

	p->pkey_group.name  = "pkeys";
	p->pkey_group.attrs = alloc_group_attrs(show_port_pkey,
						attr.pkey_tbl_len);
	if (!p->pkey_group.attrs)
		goto err_remove_gid;

	ret = sysfs_create_group(&p->kobj, &p->pkey_group);
	if (ret)
		goto err_free_pkey;

	list_add_tail(&p->kobj.entry, &device->port_list);

	return 0;

err_free_pkey:
	for (i = 0; i < attr.pkey_tbl_len; ++i)
		kfree(p->pkey_group.attrs[i]);

	kfree(p->pkey_group.attrs);

err_remove_gid:
	sysfs_remove_group(&p->kobj, &p->gid_group);

err_free_gid:
	for (i = 0; i < attr.gid_tbl_len; ++i)
		kfree(p->gid_group.attrs[i]);

	kfree(p->gid_group.attrs);

err_remove_pma:
	sysfs_remove_group(&p->kobj, &pma_group);

err_put:
	kobject_put(&device->ports_parent);

err:
	kfree(p);
	return ret;
}

static ssize_t show_node_type(struct class_device *cdev, char *buf)
{
	struct ib_device *dev = container_of(cdev, struct ib_device, class_dev);

	if (!ibdev_is_alive(dev))
		return -ENODEV;

	switch (dev->node_type) {
	case RDMA_NODE_IB_CA:	  return sprintf(buf, "%d: CA\n", dev->node_type);
	case RDMA_NODE_RNIC:	  return sprintf(buf, "%d: RNIC\n", dev->node_type);
	case RDMA_NODE_IB_SWITCH: return sprintf(buf, "%d: switch\n", dev->node_type);
	case RDMA_NODE_IB_ROUTER: return sprintf(buf, "%d: router\n", dev->node_type);
	default:		  return sprintf(buf, "%d: <unknown>\n", dev->node_type);
	}
}

static ssize_t show_sys_image_guid(struct class_device *cdev, char *buf)
{
	struct ib_device *dev = container_of(cdev, struct ib_device, class_dev);
	struct ib_device_attr attr;
	ssize_t ret;

	if (!ibdev_is_alive(dev))
		return -ENODEV;

	ret = ib_query_device(dev, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "%04x:%04x:%04x:%04x\n",
		       be16_to_cpu(((__be16 *) &attr.sys_image_guid)[0]),
		       be16_to_cpu(((__be16 *) &attr.sys_image_guid)[1]),
		       be16_to_cpu(((__be16 *) &attr.sys_image_guid)[2]),
		       be16_to_cpu(((__be16 *) &attr.sys_image_guid)[3]));
}

static ssize_t show_node_guid(struct class_device *cdev, char *buf)
{
	struct ib_device *dev = container_of(cdev, struct ib_device, class_dev);

	if (!ibdev_is_alive(dev))
		return -ENODEV;

	return sprintf(buf, "%04x:%04x:%04x:%04x\n",
		       be16_to_cpu(((__be16 *) &dev->node_guid)[0]),
		       be16_to_cpu(((__be16 *) &dev->node_guid)[1]),
		       be16_to_cpu(((__be16 *) &dev->node_guid)[2]),
		       be16_to_cpu(((__be16 *) &dev->node_guid)[3]));
}

static ssize_t show_node_desc(struct class_device *cdev, char *buf)
{
	struct ib_device *dev = container_of(cdev, struct ib_device, class_dev);

	return sprintf(buf, "%.64s\n", dev->node_desc);
}

static ssize_t set_node_desc(struct class_device *cdev, const char *buf,
			      size_t count)
{
	struct ib_device *dev = container_of(cdev, struct ib_device, class_dev);
	struct ib_device_modify desc = {};
	int ret;

	if (!dev->modify_device)
		return -EIO;

	memcpy(desc.node_desc, buf, min_t(int, count, 64));
	ret = ib_modify_device(dev, IB_DEVICE_MODIFY_NODE_DESC, &desc);
	if (ret)
		return ret;

	return count;
}

static CLASS_DEVICE_ATTR(node_type, S_IRUGO, show_node_type, NULL);
static CLASS_DEVICE_ATTR(sys_image_guid, S_IRUGO, show_sys_image_guid, NULL);
static CLASS_DEVICE_ATTR(node_guid, S_IRUGO, show_node_guid, NULL);
static CLASS_DEVICE_ATTR(node_desc, S_IRUGO | S_IWUSR, show_node_desc,
			 set_node_desc);

static struct class_device_attribute *ib_class_attributes[] = {
	&class_device_attr_node_type,
	&class_device_attr_sys_image_guid,
	&class_device_attr_node_guid,
	&class_device_attr_node_desc
};

static struct class ib_class = {
	.name    = "infiniband",
	.release = ib_device_release,
	.uevent = ib_device_uevent,
};

int ib_device_register_sysfs(struct ib_device *device)
{
	struct class_device *class_dev = &device->class_dev;
	int ret;
	int i;

	class_dev->class      = &ib_class;
	class_dev->class_data = device;
	class_dev->dev	      = device->dma_device;
	strlcpy(class_dev->class_id, device->name, BUS_ID_SIZE);

	INIT_LIST_HEAD(&device->port_list);

	ret = class_device_register(class_dev);
	if (ret)
		goto err;

	for (i = 0; i < ARRAY_SIZE(ib_class_attributes); ++i) {
		ret = class_device_create_file(class_dev, ib_class_attributes[i]);
		if (ret)
			goto err_unregister;
	}

	device->ports_parent.parent = kobject_get(&class_dev->kobj);
	if (!device->ports_parent.parent) {
		ret = -EBUSY;
		goto err_unregister;
	}
	ret = kobject_set_name(&device->ports_parent, "ports");
	if (ret)
		goto err_put;
	ret = kobject_register(&device->ports_parent);
	if (ret)
		goto err_put;

	if (device->node_type == RDMA_NODE_IB_SWITCH) {
		ret = add_port(device, 0);
		if (ret)
			goto err_put;
	} else {
		for (i = 1; i <= device->phys_port_cnt; ++i) {
			ret = add_port(device, i);
			if (ret)
				goto err_put;
		}
	}

	return 0;

err_put:
	{
		struct kobject *p, *t;
		struct ib_port *port;

		list_for_each_entry_safe(p, t, &device->port_list, entry) {
			list_del(&p->entry);
			port = container_of(p, struct ib_port, kobj);
			sysfs_remove_group(p, &pma_group);
			sysfs_remove_group(p, &port->pkey_group);
			sysfs_remove_group(p, &port->gid_group);
			kobject_unregister(p);
		}
	}

	kobject_put(&class_dev->kobj);

err_unregister:
	class_device_unregister(class_dev);

err:
	return ret;
}

void ib_device_unregister_sysfs(struct ib_device *device)
{
	struct kobject *p, *t;
	struct ib_port *port;

	list_for_each_entry_safe(p, t, &device->port_list, entry) {
		list_del(&p->entry);
		port = container_of(p, struct ib_port, kobj);
		sysfs_remove_group(p, &pma_group);
		sysfs_remove_group(p, &port->pkey_group);
		sysfs_remove_group(p, &port->gid_group);
		kobject_unregister(p);
	}

	kobject_unregister(&device->ports_parent);
	class_device_unregister(&device->class_dev);
}

int ib_sysfs_setup(void)
{
	return class_register(&ib_class);
}

void ib_sysfs_cleanup(void)
{
	class_unregister(&ib_class);
}
