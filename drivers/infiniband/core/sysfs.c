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
 */

#include "core_priv.h"

#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>

#include <rdma/ib_mad.h>
#include <rdma/ib_pma.h>
#include <rdma/ib_cache.h>
#include <rdma/rdma_counter.h>

struct ib_port;

struct gid_attr_group {
	struct ib_port		*port;
	struct kobject		kobj;
	struct attribute_group	ndev;
	struct attribute_group	type;
};
struct ib_port {
	struct kobject         kobj;
	struct ib_device      *ibdev;
	struct gid_attr_group *gid_attr_group;
	struct attribute_group gid_group;
	struct attribute_group *pkey_group;
	const struct attribute_group *pma_table;
	struct attribute_group *hw_stats_ag;
	struct rdma_hw_stats   *hw_stats;
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
	__be16			attr_id;
};

struct hw_stats_attribute {
	struct attribute	attr;
	ssize_t			(*show)(struct kobject *kobj,
					struct attribute *attr, char *buf);
	ssize_t			(*store)(struct kobject *kobj,
					 struct attribute *attr,
					 const char *buf,
					 size_t count);
	int			index;
	u8			port_num;
};

static ssize_t port_attr_show(struct kobject *kobj,
			      struct attribute *attr, char *buf)
{
	struct port_attribute *port_attr =
		container_of(attr, struct port_attribute, attr);
	struct ib_port *p = container_of(kobj, struct ib_port, kobj);

	if (!port_attr->show)
		return -EIO;

	return port_attr->show(p, port_attr, buf);
}

static ssize_t port_attr_store(struct kobject *kobj,
			       struct attribute *attr,
			       const char *buf, size_t count)
{
	struct port_attribute *port_attr =
		container_of(attr, struct port_attribute, attr);
	struct ib_port *p = container_of(kobj, struct ib_port, kobj);

	if (!port_attr->store)
		return -EIO;
	return port_attr->store(p, port_attr, buf, count);
}

static const struct sysfs_ops port_sysfs_ops = {
	.show	= port_attr_show,
	.store	= port_attr_store
};

static ssize_t gid_attr_show(struct kobject *kobj,
			     struct attribute *attr, char *buf)
{
	struct port_attribute *port_attr =
		container_of(attr, struct port_attribute, attr);
	struct ib_port *p = container_of(kobj, struct gid_attr_group,
					 kobj)->port;

	if (!port_attr->show)
		return -EIO;

	return port_attr->show(p, port_attr, buf);
}

static const struct sysfs_ops gid_attr_sysfs_ops = {
	.show = gid_attr_show
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

	return sysfs_emit(buf, "%d: %s\n", attr.state,
			  attr.state >= 0 &&
					  attr.state < ARRAY_SIZE(state_name) ?
				  state_name[attr.state] :
				  "UNKNOWN");
}

static ssize_t lid_show(struct ib_port *p, struct port_attribute *unused,
			char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%x\n", attr.lid);
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

	return sysfs_emit(buf, "%d\n", attr.lmc);
}

static ssize_t sm_lid_show(struct ib_port *p, struct port_attribute *unused,
			   char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%x\n", attr.sm_lid);
}

static ssize_t sm_sl_show(struct ib_port *p, struct port_attribute *unused,
			  char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", attr.sm_sl);
}

static ssize_t cap_mask_show(struct ib_port *p, struct port_attribute *unused,
			     char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%08x\n", attr.port_cap_flags);
}

static ssize_t rate_show(struct ib_port *p, struct port_attribute *unused,
			 char *buf)
{
	struct ib_port_attr attr;
	char *speed = "";
	int rate;		/* in deci-Gb/sec */
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	switch (attr.active_speed) {
	case IB_SPEED_DDR:
		speed = " DDR";
		rate = 50;
		break;
	case IB_SPEED_QDR:
		speed = " QDR";
		rate = 100;
		break;
	case IB_SPEED_FDR10:
		speed = " FDR10";
		rate = 100;
		break;
	case IB_SPEED_FDR:
		speed = " FDR";
		rate = 140;
		break;
	case IB_SPEED_EDR:
		speed = " EDR";
		rate = 250;
		break;
	case IB_SPEED_HDR:
		speed = " HDR";
		rate = 500;
		break;
	case IB_SPEED_NDR:
		speed = " NDR";
		rate = 1000;
		break;
	case IB_SPEED_SDR:
	default:		/* default to SDR for invalid rates */
		speed = " SDR";
		rate = 25;
		break;
	}

	rate *= ib_width_enum_to_int(attr.active_width);
	if (rate < 0)
		return -EINVAL;

	return sysfs_emit(buf, "%d%s Gb/sec (%dX%s)\n", rate / 10,
			  rate % 10 ? ".5" : "",
			  ib_width_enum_to_int(attr.active_width), speed);
}

static const char *phys_state_to_str(enum ib_port_phys_state phys_state)
{
	static const char * phys_state_str[] = {
		"<unknown>",
		"Sleep",
		"Polling",
		"Disabled",
		"PortConfigurationTraining",
		"LinkUp",
		"LinkErrorRecovery",
		"Phy Test",
	};

	if (phys_state < ARRAY_SIZE(phys_state_str))
		return phys_state_str[phys_state];
	return "<unknown>";
}

static ssize_t phys_state_show(struct ib_port *p, struct port_attribute *unused,
			       char *buf)
{
	struct ib_port_attr attr;

	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d: %s\n", attr.phys_state,
			  phys_state_to_str(attr.phys_state));
}

static ssize_t link_layer_show(struct ib_port *p, struct port_attribute *unused,
			       char *buf)
{
	const char *output;

	switch (rdma_port_get_link_layer(p->ibdev, p->port_num)) {
	case IB_LINK_LAYER_INFINIBAND:
		output = "InfiniBand";
		break;
	case IB_LINK_LAYER_ETHERNET:
		output = "Ethernet";
		break;
	default:
		output = "Unknown";
		break;
	}

	return sysfs_emit(buf, "%s\n", output);
}

static PORT_ATTR_RO(state);
static PORT_ATTR_RO(lid);
static PORT_ATTR_RO(lid_mask_count);
static PORT_ATTR_RO(sm_lid);
static PORT_ATTR_RO(sm_sl);
static PORT_ATTR_RO(cap_mask);
static PORT_ATTR_RO(rate);
static PORT_ATTR_RO(phys_state);
static PORT_ATTR_RO(link_layer);

static struct attribute *port_default_attrs[] = {
	&port_attr_state.attr,
	&port_attr_lid.attr,
	&port_attr_lid_mask_count.attr,
	&port_attr_sm_lid.attr,
	&port_attr_sm_sl.attr,
	&port_attr_cap_mask.attr,
	&port_attr_rate.attr,
	&port_attr_phys_state.attr,
	&port_attr_link_layer.attr,
	NULL
};

static ssize_t print_ndev(const struct ib_gid_attr *gid_attr, char *buf)
{
	struct net_device *ndev;
	int ret = -EINVAL;

	rcu_read_lock();
	ndev = rcu_dereference(gid_attr->ndev);
	if (ndev)
		ret = sysfs_emit(buf, "%s\n", ndev->name);
	rcu_read_unlock();
	return ret;
}

static ssize_t print_gid_type(const struct ib_gid_attr *gid_attr, char *buf)
{
	return sysfs_emit(buf, "%s\n",
			  ib_cache_gid_type_str(gid_attr->gid_type));
}

static ssize_t _show_port_gid_attr(
	struct ib_port *p, struct port_attribute *attr, char *buf,
	ssize_t (*print)(const struct ib_gid_attr *gid_attr, char *buf))
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	const struct ib_gid_attr *gid_attr;
	ssize_t ret;

	gid_attr = rdma_get_gid_attr(p->ibdev, p->port_num, tab_attr->index);
	if (IS_ERR(gid_attr))
		/* -EINVAL is returned for user space compatibility reasons. */
		return -EINVAL;

	ret = print(gid_attr, buf);
	rdma_put_gid_attr(gid_attr);
	return ret;
}

static ssize_t show_port_gid(struct ib_port *p, struct port_attribute *attr,
			     char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	const struct ib_gid_attr *gid_attr;
	int len;

	gid_attr = rdma_get_gid_attr(p->ibdev, p->port_num, tab_attr->index);
	if (IS_ERR(gid_attr)) {
		const union ib_gid zgid = {};

		/* If reading GID fails, it is likely due to GID entry being
		 * empty (invalid) or reserved GID in the table.  User space
		 * expects to read GID table entries as long as it given index
		 * is within GID table size.  Administrative/debugging tool
		 * fails to query rest of the GID entries if it hits error
		 * while querying a GID of the given index.  To avoid user
		 * space throwing such error on fail to read gid, return zero
		 * GID as before. This maintains backward compatibility.
		 */
		return sysfs_emit(buf, "%pI6\n", zgid.raw);
	}

	len = sysfs_emit(buf, "%pI6\n", gid_attr->gid.raw);
	rdma_put_gid_attr(gid_attr);
	return len;
}

static ssize_t show_port_gid_attr_ndev(struct ib_port *p,
				       struct port_attribute *attr, char *buf)
{
	return _show_port_gid_attr(p, attr, buf, print_ndev);
}

static ssize_t show_port_gid_attr_gid_type(struct ib_port *p,
					   struct port_attribute *attr,
					   char *buf)
{
	return _show_port_gid_attr(p, attr, buf, print_gid_type);
}

static ssize_t show_port_pkey(struct ib_port *p, struct port_attribute *attr,
			      char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	u16 pkey;
	int ret;

	ret = ib_query_pkey(p->ibdev, p->port_num, tab_attr->index, &pkey);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%04x\n", pkey);
}

#define PORT_PMA_ATTR(_name, _counter, _width, _offset)			\
struct port_table_attribute port_pma_attr_##_name = {			\
	.attr  = __ATTR(_name, S_IRUGO, show_pma_counter, NULL),	\
	.index = (_offset) | ((_width) << 16) | ((_counter) << 24),	\
	.attr_id = IB_PMA_PORT_COUNTERS ,				\
}

#define PORT_PMA_ATTR_EXT(_name, _width, _offset)			\
struct port_table_attribute port_pma_attr_ext_##_name = {		\
	.attr  = __ATTR(_name, S_IRUGO, show_pma_counter, NULL),	\
	.index = (_offset) | ((_width) << 16),				\
	.attr_id = IB_PMA_PORT_COUNTERS_EXT ,				\
}

/*
 * Get a Perfmgmt MAD block of data.
 * Returns error code or the number of bytes retrieved.
 */
static int get_perf_mad(struct ib_device *dev, int port_num, __be16 attr,
		void *data, int offset, size_t size)
{
	struct ib_mad *in_mad;
	struct ib_mad *out_mad;
	size_t mad_size = sizeof(*out_mad);
	u16 out_mad_pkey_index = 0;
	ssize_t ret;

	if (!dev->ops.process_mad)
		return -ENOSYS;

	in_mad = kzalloc(sizeof(*in_mad), GFP_KERNEL);
	out_mad = kzalloc(sizeof(*out_mad), GFP_KERNEL);
	if (!in_mad || !out_mad) {
		ret = -ENOMEM;
		goto out;
	}

	in_mad->mad_hdr.base_version  = 1;
	in_mad->mad_hdr.mgmt_class    = IB_MGMT_CLASS_PERF_MGMT;
	in_mad->mad_hdr.class_version = 1;
	in_mad->mad_hdr.method        = IB_MGMT_METHOD_GET;
	in_mad->mad_hdr.attr_id       = attr;

	if (attr != IB_PMA_CLASS_PORT_INFO)
		in_mad->data[41] = port_num;	/* PortSelect field */

	if ((dev->ops.process_mad(dev, IB_MAD_IGNORE_MKEY, port_num, NULL, NULL,
				  in_mad, out_mad, &mad_size,
				  &out_mad_pkey_index) &
	     (IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY)) !=
	    (IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY)) {
		ret = -EINVAL;
		goto out;
	}
	memcpy(data, out_mad->data + offset, size);
	ret = size;
out:
	kfree(in_mad);
	kfree(out_mad);
	return ret;
}

static ssize_t show_pma_counter(struct ib_port *p, struct port_attribute *attr,
				char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	int offset = tab_attr->index & 0xffff;
	int width  = (tab_attr->index >> 16) & 0xff;
	int ret;
	u8 data[8];
	int len;

	ret = get_perf_mad(p->ibdev, p->port_num, tab_attr->attr_id, &data,
			40 + offset / 8, sizeof(data));
	if (ret < 0)
		return ret;

	switch (width) {
	case 4:
		len = sysfs_emit(buf, "%u\n",
				 (*data >> (4 - (offset % 8))) & 0xf);
		break;
	case 8:
		len = sysfs_emit(buf, "%u\n", *data);
		break;
	case 16:
		len = sysfs_emit(buf, "%u\n", be16_to_cpup((__be16 *)data));
		break;
	case 32:
		len = sysfs_emit(buf, "%u\n", be32_to_cpup((__be32 *)data));
		break;
	case 64:
		len = sysfs_emit(buf, "%llu\n", be64_to_cpup((__be64 *)data));
		break;
	default:
		len = 0;
		break;
	}

	return len;
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
static PORT_PMA_ATTR(port_xmit_wait		    ,  0, 32, 320);

/*
 * Counters added by extended set
 */
static PORT_PMA_ATTR_EXT(port_xmit_data		    , 64,  64);
static PORT_PMA_ATTR_EXT(port_rcv_data		    , 64, 128);
static PORT_PMA_ATTR_EXT(port_xmit_packets	    , 64, 192);
static PORT_PMA_ATTR_EXT(port_rcv_packets	    , 64, 256);
static PORT_PMA_ATTR_EXT(unicast_xmit_packets	    , 64, 320);
static PORT_PMA_ATTR_EXT(unicast_rcv_packets	    , 64, 384);
static PORT_PMA_ATTR_EXT(multicast_xmit_packets	    , 64, 448);
static PORT_PMA_ATTR_EXT(multicast_rcv_packets	    , 64, 512);

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
	&port_pma_attr_port_xmit_wait.attr.attr,
	NULL
};

static struct attribute *pma_attrs_ext[] = {
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
	&port_pma_attr_ext_port_xmit_data.attr.attr,
	&port_pma_attr_ext_port_rcv_data.attr.attr,
	&port_pma_attr_ext_port_xmit_packets.attr.attr,
	&port_pma_attr_port_xmit_wait.attr.attr,
	&port_pma_attr_ext_port_rcv_packets.attr.attr,
	&port_pma_attr_ext_unicast_rcv_packets.attr.attr,
	&port_pma_attr_ext_unicast_xmit_packets.attr.attr,
	&port_pma_attr_ext_multicast_rcv_packets.attr.attr,
	&port_pma_attr_ext_multicast_xmit_packets.attr.attr,
	NULL
};

static struct attribute *pma_attrs_noietf[] = {
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
	&port_pma_attr_ext_port_xmit_data.attr.attr,
	&port_pma_attr_ext_port_rcv_data.attr.attr,
	&port_pma_attr_ext_port_xmit_packets.attr.attr,
	&port_pma_attr_ext_port_rcv_packets.attr.attr,
	&port_pma_attr_port_xmit_wait.attr.attr,
	NULL
};

static const struct attribute_group pma_group = {
	.name  = "counters",
	.attrs  = pma_attrs
};

static const struct attribute_group pma_group_ext = {
	.name  = "counters",
	.attrs  = pma_attrs_ext
};

static const struct attribute_group pma_group_noietf = {
	.name  = "counters",
	.attrs  = pma_attrs_noietf
};

static void ib_port_release(struct kobject *kobj)
{
	struct ib_port *p = container_of(kobj, struct ib_port, kobj);
	struct attribute *a;
	int i;

	if (p->gid_group.attrs) {
		for (i = 0; (a = p->gid_group.attrs[i]); ++i)
			kfree(a);

		kfree(p->gid_group.attrs);
	}

	if (p->pkey_group) {
		if (p->pkey_group->attrs) {
			for (i = 0; (a = p->pkey_group->attrs[i]); ++i)
				kfree(a);

			kfree(p->pkey_group->attrs);
		}

		kfree(p->pkey_group);
		p->pkey_group = NULL;
	}

	kfree(p);
}

static void ib_port_gid_attr_release(struct kobject *kobj)
{
	struct gid_attr_group *g = container_of(kobj, struct gid_attr_group,
						kobj);
	struct attribute *a;
	int i;

	if (g->ndev.attrs) {
		for (i = 0; (a = g->ndev.attrs[i]); ++i)
			kfree(a);

		kfree(g->ndev.attrs);
	}

	if (g->type.attrs) {
		for (i = 0; (a = g->type.attrs[i]); ++i)
			kfree(a);

		kfree(g->type.attrs);
	}

	kfree(g);
}

static struct kobj_type port_type = {
	.release       = ib_port_release,
	.sysfs_ops     = &port_sysfs_ops,
	.default_attrs = port_default_attrs
};

static struct kobj_type gid_attr_type = {
	.sysfs_ops      = &gid_attr_sysfs_ops,
	.release        = ib_port_gid_attr_release
};

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
		sysfs_attr_init(&element->attr.attr);

		tab_attr[i] = &element->attr.attr;
	}

	return tab_attr;

err:
	while (--i >= 0)
		kfree(tab_attr[i]);
	kfree(tab_attr);
	return NULL;
}

/*
 * Figure out which counter table to use depending on
 * the device capabilities.
 */
static const struct attribute_group *get_counter_table(struct ib_device *dev,
						       int port_num)
{
	struct ib_class_port_info cpi;

	if (get_perf_mad(dev, port_num, IB_PMA_CLASS_PORT_INFO,
				&cpi, 40, sizeof(cpi)) >= 0) {
		if (cpi.capability_mask & IB_PMA_CLASS_CAP_EXT_WIDTH)
			/* We have extended counters */
			return &pma_group_ext;

		if (cpi.capability_mask & IB_PMA_CLASS_CAP_EXT_WIDTH_NOIETF)
			/* But not the IETF ones */
			return &pma_group_noietf;
	}

	/* Fall back to normal counters */
	return &pma_group;
}

static int update_hw_stats(struct ib_device *dev, struct rdma_hw_stats *stats,
			   u8 port_num, int index)
{
	int ret;

	if (time_is_after_eq_jiffies(stats->timestamp + stats->lifespan))
		return 0;
	ret = dev->ops.get_hw_stats(dev, stats, port_num, index);
	if (ret < 0)
		return ret;
	if (ret == stats->num_counters)
		stats->timestamp = jiffies;

	return 0;
}

static int print_hw_stat(struct ib_device *dev, int port_num,
			 struct rdma_hw_stats *stats, int index, char *buf)
{
	u64 v = rdma_counter_get_hwstat_value(dev, port_num, index);

	return sysfs_emit(buf, "%llu\n", stats->value[index] + v);
}

static ssize_t show_hw_stats(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct ib_device *dev;
	struct ib_port *port;
	struct hw_stats_attribute *hsa;
	struct rdma_hw_stats *stats;
	int ret;

	hsa = container_of(attr, struct hw_stats_attribute, attr);
	if (!hsa->port_num) {
		dev = container_of((struct device *)kobj,
				   struct ib_device, dev);
		stats = dev->hw_stats;
	} else {
		port = container_of(kobj, struct ib_port, kobj);
		dev = port->ibdev;
		stats = port->hw_stats;
	}
	mutex_lock(&stats->lock);
	ret = update_hw_stats(dev, stats, hsa->port_num, hsa->index);
	if (ret)
		goto unlock;
	ret = print_hw_stat(dev, hsa->port_num, stats, hsa->index, buf);
unlock:
	mutex_unlock(&stats->lock);

	return ret;
}

static ssize_t show_stats_lifespan(struct kobject *kobj,
				   struct attribute *attr,
				   char *buf)
{
	struct hw_stats_attribute *hsa;
	struct rdma_hw_stats *stats;
	int msecs;

	hsa = container_of(attr, struct hw_stats_attribute, attr);
	if (!hsa->port_num) {
		struct ib_device *dev = container_of((struct device *)kobj,
						     struct ib_device, dev);

		stats = dev->hw_stats;
	} else {
		struct ib_port *p = container_of(kobj, struct ib_port, kobj);

		stats = p->hw_stats;
	}

	mutex_lock(&stats->lock);
	msecs = jiffies_to_msecs(stats->lifespan);
	mutex_unlock(&stats->lock);

	return sysfs_emit(buf, "%d\n", msecs);
}

static ssize_t set_stats_lifespan(struct kobject *kobj,
				  struct attribute *attr,
				  const char *buf, size_t count)
{
	struct hw_stats_attribute *hsa;
	struct rdma_hw_stats *stats;
	int msecs;
	int jiffies;
	int ret;

	ret = kstrtoint(buf, 10, &msecs);
	if (ret)
		return ret;
	if (msecs < 0 || msecs > 10000)
		return -EINVAL;
	jiffies = msecs_to_jiffies(msecs);
	hsa = container_of(attr, struct hw_stats_attribute, attr);
	if (!hsa->port_num) {
		struct ib_device *dev = container_of((struct device *)kobj,
						     struct ib_device, dev);

		stats = dev->hw_stats;
	} else {
		struct ib_port *p = container_of(kobj, struct ib_port, kobj);

		stats = p->hw_stats;
	}

	mutex_lock(&stats->lock);
	stats->lifespan = jiffies;
	mutex_unlock(&stats->lock);

	return count;
}

static void free_hsag(struct kobject *kobj, struct attribute_group *attr_group)
{
	struct attribute **attr;

	sysfs_remove_group(kobj, attr_group);

	for (attr = attr_group->attrs; *attr; attr++)
		kfree(*attr);
	kfree(attr_group);
}

static struct attribute *alloc_hsa(int index, u8 port_num, const char *name)
{
	struct hw_stats_attribute *hsa;

	hsa = kmalloc(sizeof(*hsa), GFP_KERNEL);
	if (!hsa)
		return NULL;

	hsa->attr.name = (char *)name;
	hsa->attr.mode = S_IRUGO;
	hsa->show = show_hw_stats;
	hsa->store = NULL;
	hsa->index = index;
	hsa->port_num = port_num;

	return &hsa->attr;
}

static struct attribute *alloc_hsa_lifespan(char *name, u8 port_num)
{
	struct hw_stats_attribute *hsa;

	hsa = kmalloc(sizeof(*hsa), GFP_KERNEL);
	if (!hsa)
		return NULL;

	hsa->attr.name = name;
	hsa->attr.mode = S_IWUSR | S_IRUGO;
	hsa->show = show_stats_lifespan;
	hsa->store = set_stats_lifespan;
	hsa->index = 0;
	hsa->port_num = port_num;

	return &hsa->attr;
}

static void setup_hw_stats(struct ib_device *device, struct ib_port *port,
			   u8 port_num)
{
	struct attribute_group *hsag;
	struct rdma_hw_stats *stats;
	int i, ret;

	stats = device->ops.alloc_hw_stats(device, port_num);

	if (!stats)
		return;

	if (!stats->names || stats->num_counters <= 0)
		goto err_free_stats;

	/*
	 * Two extra attribue elements here, one for the lifespan entry and
	 * one to NULL terminate the list for the sysfs core code
	 */
	hsag = kzalloc(sizeof(*hsag) +
		       sizeof(void *) * (stats->num_counters + 2),
		       GFP_KERNEL);
	if (!hsag)
		goto err_free_stats;

	ret = device->ops.get_hw_stats(device, stats, port_num,
				       stats->num_counters);
	if (ret != stats->num_counters)
		goto err_free_hsag;

	stats->timestamp = jiffies;

	hsag->name = "hw_counters";
	hsag->attrs = (void *)hsag + sizeof(*hsag);

	for (i = 0; i < stats->num_counters; i++) {
		hsag->attrs[i] = alloc_hsa(i, port_num, stats->names[i]);
		if (!hsag->attrs[i])
			goto err;
		sysfs_attr_init(hsag->attrs[i]);
	}

	mutex_init(&stats->lock);
	/* treat an error here as non-fatal */
	hsag->attrs[i] = alloc_hsa_lifespan("lifespan", port_num);
	if (hsag->attrs[i])
		sysfs_attr_init(hsag->attrs[i]);

	if (port) {
		struct kobject *kobj = &port->kobj;
		ret = sysfs_create_group(kobj, hsag);
		if (ret)
			goto err;
		port->hw_stats_ag = hsag;
		port->hw_stats = stats;
		if (device->port_data)
			device->port_data[port_num].hw_stats = stats;
	} else {
		struct kobject *kobj = &device->dev.kobj;
		ret = sysfs_create_group(kobj, hsag);
		if (ret)
			goto err;
		device->hw_stats_ag = hsag;
		device->hw_stats = stats;
	}

	return;

err:
	for (; i >= 0; i--)
		kfree(hsag->attrs[i]);
err_free_hsag:
	kfree(hsag);
err_free_stats:
	kfree(stats);
	return;
}

static int add_port(struct ib_core_device *coredev, int port_num)
{
	struct ib_device *device = rdma_device_to_ibdev(&coredev->dev);
	bool is_full_dev = &device->coredev == coredev;
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

	ret = kobject_init_and_add(&p->kobj, &port_type,
				   coredev->ports_kobj,
				   "%d", port_num);
	if (ret) {
		goto err_put;
	}

	p->gid_attr_group = kzalloc(sizeof(*p->gid_attr_group), GFP_KERNEL);
	if (!p->gid_attr_group) {
		ret = -ENOMEM;
		goto err_put;
	}

	p->gid_attr_group->port = p;
	ret = kobject_init_and_add(&p->gid_attr_group->kobj, &gid_attr_type,
				   &p->kobj, "gid_attrs");
	if (ret) {
		goto err_put_gid_attrs;
	}

	if (device->ops.process_mad && is_full_dev) {
		p->pma_table = get_counter_table(device, port_num);
		ret = sysfs_create_group(&p->kobj, p->pma_table);
		if (ret)
			goto err_put_gid_attrs;
	}

	p->gid_group.name  = "gids";
	p->gid_group.attrs = alloc_group_attrs(show_port_gid, attr.gid_tbl_len);
	if (!p->gid_group.attrs) {
		ret = -ENOMEM;
		goto err_remove_pma;
	}

	ret = sysfs_create_group(&p->kobj, &p->gid_group);
	if (ret)
		goto err_free_gid;

	p->gid_attr_group->ndev.name = "ndevs";
	p->gid_attr_group->ndev.attrs = alloc_group_attrs(show_port_gid_attr_ndev,
							  attr.gid_tbl_len);
	if (!p->gid_attr_group->ndev.attrs) {
		ret = -ENOMEM;
		goto err_remove_gid;
	}

	ret = sysfs_create_group(&p->gid_attr_group->kobj,
				 &p->gid_attr_group->ndev);
	if (ret)
		goto err_free_gid_ndev;

	p->gid_attr_group->type.name = "types";
	p->gid_attr_group->type.attrs = alloc_group_attrs(show_port_gid_attr_gid_type,
							  attr.gid_tbl_len);
	if (!p->gid_attr_group->type.attrs) {
		ret = -ENOMEM;
		goto err_remove_gid_ndev;
	}

	ret = sysfs_create_group(&p->gid_attr_group->kobj,
				 &p->gid_attr_group->type);
	if (ret)
		goto err_free_gid_type;

	if (attr.pkey_tbl_len) {
		p->pkey_group = kzalloc(sizeof(*p->pkey_group), GFP_KERNEL);
		if (!p->pkey_group) {
			ret = -ENOMEM;
			goto err_remove_gid_type;
		}

		p->pkey_group->name  = "pkeys";
		p->pkey_group->attrs = alloc_group_attrs(show_port_pkey,
							 attr.pkey_tbl_len);
		if (!p->pkey_group->attrs) {
			ret = -ENOMEM;
			goto err_free_pkey_group;
		}

		ret = sysfs_create_group(&p->kobj, p->pkey_group);
		if (ret)
			goto err_free_pkey;
	}


	if (device->ops.init_port && is_full_dev) {
		ret = device->ops.init_port(device, port_num, &p->kobj);
		if (ret)
			goto err_remove_pkey;
	}

	/*
	 * If port == 0, it means hw_counters are per device and not per
	 * port, so holder should be device. Therefore skip per port conunter
	 * initialization.
	 */
	if (device->ops.alloc_hw_stats && port_num && is_full_dev)
		setup_hw_stats(device, p, port_num);

	list_add_tail(&p->kobj.entry, &coredev->port_list);

	kobject_uevent(&p->kobj, KOBJ_ADD);
	return 0;

err_remove_pkey:
	if (p->pkey_group)
		sysfs_remove_group(&p->kobj, p->pkey_group);

err_free_pkey:
	if (p->pkey_group) {
		for (i = 0; i < attr.pkey_tbl_len; ++i)
			kfree(p->pkey_group->attrs[i]);

		kfree(p->pkey_group->attrs);
		p->pkey_group->attrs = NULL;
	}

err_free_pkey_group:
	kfree(p->pkey_group);

err_remove_gid_type:
	sysfs_remove_group(&p->gid_attr_group->kobj,
			   &p->gid_attr_group->type);

err_free_gid_type:
	for (i = 0; i < attr.gid_tbl_len; ++i)
		kfree(p->gid_attr_group->type.attrs[i]);

	kfree(p->gid_attr_group->type.attrs);
	p->gid_attr_group->type.attrs = NULL;

err_remove_gid_ndev:
	sysfs_remove_group(&p->gid_attr_group->kobj,
			   &p->gid_attr_group->ndev);

err_free_gid_ndev:
	for (i = 0; i < attr.gid_tbl_len; ++i)
		kfree(p->gid_attr_group->ndev.attrs[i]);

	kfree(p->gid_attr_group->ndev.attrs);
	p->gid_attr_group->ndev.attrs = NULL;

err_remove_gid:
	sysfs_remove_group(&p->kobj, &p->gid_group);

err_free_gid:
	for (i = 0; i < attr.gid_tbl_len; ++i)
		kfree(p->gid_group.attrs[i]);

	kfree(p->gid_group.attrs);
	p->gid_group.attrs = NULL;

err_remove_pma:
	if (p->pma_table)
		sysfs_remove_group(&p->kobj, p->pma_table);

err_put_gid_attrs:
	kobject_put(&p->gid_attr_group->kobj);

err_put:
	kobject_put(&p->kobj);
	return ret;
}

static const char *node_type_string(int node_type)
{
	switch (node_type) {
	case RDMA_NODE_IB_CA:
		return "CA";
	case RDMA_NODE_IB_SWITCH:
		return "switch";
	case RDMA_NODE_IB_ROUTER:
		return "router";
	case RDMA_NODE_RNIC:
		return "RNIC";
	case RDMA_NODE_USNIC:
		return "usNIC";
	case RDMA_NODE_USNIC_UDP:
		return "usNIC UDP";
	case RDMA_NODE_UNSPECIFIED:
		return "unspecified";
	}
	return "<unknown>";
}

static ssize_t node_type_show(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	struct ib_device *dev = rdma_device_to_ibdev(device);

	return sysfs_emit(buf, "%d: %s\n", dev->node_type,
			  node_type_string(dev->node_type));
}
static DEVICE_ATTR_RO(node_type);

static ssize_t sys_image_guid_show(struct device *device,
				   struct device_attribute *dev_attr, char *buf)
{
	struct ib_device *dev = rdma_device_to_ibdev(device);
	__be16 *guid = (__be16 *)&dev->attrs.sys_image_guid;

	return sysfs_emit(buf, "%04x:%04x:%04x:%04x\n",
			  be16_to_cpu(guid[0]),
			  be16_to_cpu(guid[1]),
			  be16_to_cpu(guid[2]),
			  be16_to_cpu(guid[3]));
}
static DEVICE_ATTR_RO(sys_image_guid);

static ssize_t node_guid_show(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	struct ib_device *dev = rdma_device_to_ibdev(device);
	__be16 *node_guid = (__be16 *)&dev->node_guid;

	return sysfs_emit(buf, "%04x:%04x:%04x:%04x\n",
			  be16_to_cpu(node_guid[0]),
			  be16_to_cpu(node_guid[1]),
			  be16_to_cpu(node_guid[2]),
			  be16_to_cpu(node_guid[3]));
}
static DEVICE_ATTR_RO(node_guid);

static ssize_t node_desc_show(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	struct ib_device *dev = rdma_device_to_ibdev(device);

	return sysfs_emit(buf, "%.64s\n", dev->node_desc);
}

static ssize_t node_desc_store(struct device *device,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ib_device *dev = rdma_device_to_ibdev(device);
	struct ib_device_modify desc = {};
	int ret;

	if (!dev->ops.modify_device)
		return -EOPNOTSUPP;

	memcpy(desc.node_desc, buf, min_t(int, count, IB_DEVICE_NODE_DESC_MAX));
	ret = ib_modify_device(dev, IB_DEVICE_MODIFY_NODE_DESC, &desc);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(node_desc);

static ssize_t fw_ver_show(struct device *device, struct device_attribute *attr,
			   char *buf)
{
	struct ib_device *dev = rdma_device_to_ibdev(device);
	char version[IB_FW_VERSION_NAME_MAX] = {};

	ib_get_device_fw_str(dev, version);

	return sysfs_emit(buf, "%s\n", version);
}
static DEVICE_ATTR_RO(fw_ver);

static struct attribute *ib_dev_attrs[] = {
	&dev_attr_node_type.attr,
	&dev_attr_node_guid.attr,
	&dev_attr_sys_image_guid.attr,
	&dev_attr_fw_ver.attr,
	&dev_attr_node_desc.attr,
	NULL,
};

const struct attribute_group ib_dev_attr_group = {
	.attrs = ib_dev_attrs,
};

void ib_free_port_attrs(struct ib_core_device *coredev)
{
	struct ib_device *device = rdma_device_to_ibdev(&coredev->dev);
	bool is_full_dev = &device->coredev == coredev;
	struct kobject *p, *t;

	list_for_each_entry_safe(p, t, &coredev->port_list, entry) {
		struct ib_port *port = container_of(p, struct ib_port, kobj);

		list_del(&p->entry);
		if (port->hw_stats_ag)
			free_hsag(&port->kobj, port->hw_stats_ag);
		kfree(port->hw_stats);
		if (device->port_data && is_full_dev)
			device->port_data[port->port_num].hw_stats = NULL;

		if (port->pma_table)
			sysfs_remove_group(p, port->pma_table);
		if (port->pkey_group)
			sysfs_remove_group(p, port->pkey_group);
		sysfs_remove_group(p, &port->gid_group);
		sysfs_remove_group(&port->gid_attr_group->kobj,
				   &port->gid_attr_group->ndev);
		sysfs_remove_group(&port->gid_attr_group->kobj,
				   &port->gid_attr_group->type);
		kobject_put(&port->gid_attr_group->kobj);
		kobject_put(p);
	}

	kobject_put(coredev->ports_kobj);
}

int ib_setup_port_attrs(struct ib_core_device *coredev)
{
	struct ib_device *device = rdma_device_to_ibdev(&coredev->dev);
	unsigned int port;
	int ret;

	coredev->ports_kobj = kobject_create_and_add("ports",
						     &coredev->dev.kobj);
	if (!coredev->ports_kobj)
		return -ENOMEM;

	rdma_for_each_port (device, port) {
		ret = add_port(coredev, port);
		if (ret)
			goto err_put;
	}

	return 0;

err_put:
	ib_free_port_attrs(coredev);
	return ret;
}

int ib_device_register_sysfs(struct ib_device *device)
{
	int ret;

	ret = ib_setup_port_attrs(&device->coredev);
	if (ret)
		return ret;

	if (device->ops.alloc_hw_stats)
		setup_hw_stats(device, NULL, 0);

	return 0;
}

void ib_device_unregister_sysfs(struct ib_device *device)
{
	if (device->hw_stats_ag)
		free_hsag(&device->dev.kobj, device->hw_stats_ag);
	kfree(device->hw_stats);

	ib_free_port_attrs(&device->coredev);
}

/**
 * ib_port_register_module_stat - add module counters under relevant port
 *  of IB device.
 *
 * @device: IB device to add counters
 * @port_num: valid port number
 * @kobj: pointer to the kobject to initialize
 * @ktype: pointer to the ktype for this kobject.
 * @name: the name of the kobject
 */
int ib_port_register_module_stat(struct ib_device *device, u8 port_num,
				 struct kobject *kobj, struct kobj_type *ktype,
				 const char *name)
{
	struct kobject *p, *t;
	int ret;

	list_for_each_entry_safe(p, t, &device->coredev.port_list, entry) {
		struct ib_port *port = container_of(p, struct ib_port, kobj);

		if (port->port_num != port_num)
			continue;

		ret = kobject_init_and_add(kobj, ktype, &port->kobj, "%s",
					   name);
		if (ret) {
			kobject_put(kobj);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(ib_port_register_module_stat);

/**
 * ib_port_unregister_module_stat - release module counters
 * @kobj: pointer to the kobject to release
 */
void ib_port_unregister_module_stat(struct kobject *kobj)
{
	kobject_put(kobj);
}
EXPORT_SYMBOL(ib_port_unregister_module_stat);
