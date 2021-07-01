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
#include <rdma/ib_sysfs.h>

struct port_table_attribute {
	struct ib_port_attribute attr;
	char			name[8];
	int			index;
	__be16			attr_id;
};

struct gid_attr_group {
	struct ib_port *port;
	struct kobject kobj;
	struct attribute_group groups[2];
	const struct attribute_group *groups_list[3];
	struct port_table_attribute attrs_list[];
};

struct ib_port {
	struct kobject kobj;
	struct ib_device *ibdev;
	struct gid_attr_group *gid_attr_group;
	struct hw_stats_port_data *hw_stats_data;

	struct attribute_group groups[3];
	const struct attribute_group *groups_list[5];
	u32 port_num;
	struct port_table_attribute attrs_list[];
};

struct hw_stats_device_attribute {
	struct device_attribute attr;
	ssize_t (*show)(struct ib_device *ibdev, struct rdma_hw_stats *stats,
			unsigned int index, unsigned int port_num, char *buf);
	ssize_t (*store)(struct ib_device *ibdev, struct rdma_hw_stats *stats,
			 unsigned int index, unsigned int port_num,
			 const char *buf, size_t count);
};

struct hw_stats_port_attribute {
	struct ib_port_attribute attr;
	ssize_t (*show)(struct ib_device *ibdev, struct rdma_hw_stats *stats,
			unsigned int index, unsigned int port_num, char *buf);
	ssize_t (*store)(struct ib_device *ibdev, struct rdma_hw_stats *stats,
			 unsigned int index, unsigned int port_num,
			 const char *buf, size_t count);
};

struct hw_stats_device_data {
	struct attribute_group group;
	struct rdma_hw_stats *stats;
	struct hw_stats_device_attribute attrs[];
};

struct hw_stats_port_data {
	struct rdma_hw_stats *stats;
	struct hw_stats_port_attribute attrs[];
};

static ssize_t port_attr_show(struct kobject *kobj,
			      struct attribute *attr, char *buf)
{
	struct ib_port_attribute *port_attr =
		container_of(attr, struct ib_port_attribute, attr);
	struct ib_port *p = container_of(kobj, struct ib_port, kobj);

	if (!port_attr->show)
		return -EIO;

	return port_attr->show(p->ibdev, p->port_num, port_attr, buf);
}

static ssize_t port_attr_store(struct kobject *kobj,
			       struct attribute *attr,
			       const char *buf, size_t count)
{
	struct ib_port_attribute *port_attr =
		container_of(attr, struct ib_port_attribute, attr);
	struct ib_port *p = container_of(kobj, struct ib_port, kobj);

	if (!port_attr->store)
		return -EIO;
	return port_attr->store(p->ibdev, p->port_num, port_attr, buf, count);
}

struct ib_device *ib_port_sysfs_get_ibdev_kobj(struct kobject *kobj,
					       u32 *port_num)
{
	struct ib_port *port = container_of(kobj, struct ib_port, kobj);

	*port_num = port->port_num;
	return port->ibdev;
}
EXPORT_SYMBOL(ib_port_sysfs_get_ibdev_kobj);

static const struct sysfs_ops port_sysfs_ops = {
	.show	= port_attr_show,
	.store	= port_attr_store
};

static ssize_t hw_stat_device_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct hw_stats_device_attribute *stat_attr =
		container_of(attr, struct hw_stats_device_attribute, attr);
	struct ib_device *ibdev = container_of(dev, struct ib_device, dev);

	return stat_attr->show(ibdev, ibdev->hw_stats_data->stats,
			       stat_attr - ibdev->hw_stats_data->attrs, 0, buf);
}

static ssize_t hw_stat_device_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct hw_stats_device_attribute *stat_attr =
		container_of(attr, struct hw_stats_device_attribute, attr);
	struct ib_device *ibdev = container_of(dev, struct ib_device, dev);

	return stat_attr->store(ibdev, ibdev->hw_stats_data->stats,
				stat_attr - ibdev->hw_stats_data->attrs, 0, buf,
				count);
}

static ssize_t hw_stat_port_show(struct ib_device *ibdev, u32 port_num,
				 struct ib_port_attribute *attr, char *buf)
{
	struct hw_stats_port_attribute *stat_attr =
		container_of(attr, struct hw_stats_port_attribute, attr);
	struct ib_port *port = ibdev->port_data[port_num].sysfs;

	return stat_attr->show(ibdev, port->hw_stats_data->stats,
			       stat_attr - port->hw_stats_data->attrs,
			       port->port_num, buf);
}

static ssize_t hw_stat_port_store(struct ib_device *ibdev, u32 port_num,
				  struct ib_port_attribute *attr,
				  const char *buf, size_t count)
{
	struct hw_stats_port_attribute *stat_attr =
		container_of(attr, struct hw_stats_port_attribute, attr);
	struct ib_port *port = ibdev->port_data[port_num].sysfs;

	return stat_attr->store(ibdev, port->hw_stats_data->stats,
				stat_attr - port->hw_stats_data->attrs,
				port->port_num, buf, count);
}

static ssize_t gid_attr_show(struct kobject *kobj,
			     struct attribute *attr, char *buf)
{
	struct ib_port_attribute *port_attr =
		container_of(attr, struct ib_port_attribute, attr);
	struct ib_port *p = container_of(kobj, struct gid_attr_group,
					 kobj)->port;

	if (!port_attr->show)
		return -EIO;

	return port_attr->show(p->ibdev, p->port_num, port_attr, buf);
}

static const struct sysfs_ops gid_attr_sysfs_ops = {
	.show = gid_attr_show
};

static ssize_t state_show(struct ib_device *ibdev, u32 port_num,
			  struct ib_port_attribute *unused, char *buf)
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

	ret = ib_query_port(ibdev, port_num, &attr);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d: %s\n", attr.state,
			  attr.state >= 0 &&
					  attr.state < ARRAY_SIZE(state_name) ?
				  state_name[attr.state] :
				  "UNKNOWN");
}

static ssize_t lid_show(struct ib_device *ibdev, u32 port_num,
			struct ib_port_attribute *unused, char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(ibdev, port_num, &attr);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%x\n", attr.lid);
}

static ssize_t lid_mask_count_show(struct ib_device *ibdev, u32 port_num,
				   struct ib_port_attribute *unused, char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(ibdev, port_num, &attr);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", attr.lmc);
}

static ssize_t sm_lid_show(struct ib_device *ibdev, u32 port_num,
			   struct ib_port_attribute *unused, char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(ibdev, port_num, &attr);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%x\n", attr.sm_lid);
}

static ssize_t sm_sl_show(struct ib_device *ibdev, u32 port_num,
			  struct ib_port_attribute *unused, char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(ibdev, port_num, &attr);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", attr.sm_sl);
}

static ssize_t cap_mask_show(struct ib_device *ibdev, u32 port_num,
			     struct ib_port_attribute *unused, char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(ibdev, port_num, &attr);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%08x\n", attr.port_cap_flags);
}

static ssize_t rate_show(struct ib_device *ibdev, u32 port_num,
			 struct ib_port_attribute *unused, char *buf)
{
	struct ib_port_attr attr;
	char *speed = "";
	int rate;		/* in deci-Gb/sec */
	ssize_t ret;

	ret = ib_query_port(ibdev, port_num, &attr);
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
	static const char *phys_state_str[] = {
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

static ssize_t phys_state_show(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_attribute *unused, char *buf)
{
	struct ib_port_attr attr;

	ssize_t ret;

	ret = ib_query_port(ibdev, port_num, &attr);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u: %s\n", attr.phys_state,
			  phys_state_to_str(attr.phys_state));
}

static ssize_t link_layer_show(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_attribute *unused, char *buf)
{
	const char *output;

	switch (rdma_port_get_link_layer(ibdev, port_num)) {
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

static IB_PORT_ATTR_RO(state);
static IB_PORT_ATTR_RO(lid);
static IB_PORT_ATTR_RO(lid_mask_count);
static IB_PORT_ATTR_RO(sm_lid);
static IB_PORT_ATTR_RO(sm_sl);
static IB_PORT_ATTR_RO(cap_mask);
static IB_PORT_ATTR_RO(rate);
static IB_PORT_ATTR_RO(phys_state);
static IB_PORT_ATTR_RO(link_layer);

static struct attribute *port_default_attrs[] = {
	&ib_port_attr_state.attr,
	&ib_port_attr_lid.attr,
	&ib_port_attr_lid_mask_count.attr,
	&ib_port_attr_sm_lid.attr,
	&ib_port_attr_sm_sl.attr,
	&ib_port_attr_cap_mask.attr,
	&ib_port_attr_rate.attr,
	&ib_port_attr_phys_state.attr,
	&ib_port_attr_link_layer.attr,
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
	struct ib_device *ibdev, u32 port_num, struct ib_port_attribute *attr,
	char *buf,
	ssize_t (*print)(const struct ib_gid_attr *gid_attr, char *buf))
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	const struct ib_gid_attr *gid_attr;
	ssize_t ret;

	gid_attr = rdma_get_gid_attr(ibdev, port_num, tab_attr->index);
	if (IS_ERR(gid_attr))
		/* -EINVAL is returned for user space compatibility reasons. */
		return -EINVAL;

	ret = print(gid_attr, buf);
	rdma_put_gid_attr(gid_attr);
	return ret;
}

static ssize_t show_port_gid(struct ib_device *ibdev, u32 port_num,
			     struct ib_port_attribute *attr, char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	const struct ib_gid_attr *gid_attr;
	int len;

	gid_attr = rdma_get_gid_attr(ibdev, port_num, tab_attr->index);
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

static ssize_t show_port_gid_attr_ndev(struct ib_device *ibdev, u32 port_num,
				       struct ib_port_attribute *attr,
				       char *buf)
{
	return _show_port_gid_attr(ibdev, port_num, attr, buf, print_ndev);
}

static ssize_t show_port_gid_attr_gid_type(struct ib_device *ibdev,
					   u32 port_num,
					   struct ib_port_attribute *attr,
					   char *buf)
{
	return _show_port_gid_attr(ibdev, port_num, attr, buf, print_gid_type);
}

static ssize_t show_port_pkey(struct ib_device *ibdev, u32 port_num,
			      struct ib_port_attribute *attr, char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	u16 pkey;
	int ret;

	ret = ib_query_pkey(ibdev, port_num, tab_attr->index, &pkey);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%04x\n", pkey);
}

#define PORT_PMA_ATTR(_name, _counter, _width, _offset)			\
struct port_table_attribute port_pma_attr_##_name = {			\
	.attr  = __ATTR(_name, S_IRUGO, show_pma_counter, NULL),	\
	.index = (_offset) | ((_width) << 16) | ((_counter) << 24),	\
	.attr_id = IB_PMA_PORT_COUNTERS,				\
}

#define PORT_PMA_ATTR_EXT(_name, _width, _offset)			\
struct port_table_attribute port_pma_attr_ext_##_name = {		\
	.attr  = __ATTR(_name, S_IRUGO, show_pma_counter, NULL),	\
	.index = (_offset) | ((_width) << 16),				\
	.attr_id = IB_PMA_PORT_COUNTERS_EXT,				\
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

static ssize_t show_pma_counter(struct ib_device *ibdev, u32 port_num,
				struct ib_port_attribute *attr, char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	int offset = tab_attr->index & 0xffff;
	int width  = (tab_attr->index >> 16) & 0xff;
	int ret;
	u8 data[8];
	int len;

	ret = get_perf_mad(ibdev, port_num, tab_attr->attr_id, &data,
			40 + offset / 8, sizeof(data));
	if (ret < 0)
		return ret;

	switch (width) {
	case 4:
		len = sysfs_emit(buf, "%d\n",
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
	struct ib_port *port = container_of(kobj, struct ib_port, kobj);
	int i;

	for (i = 0; i != ARRAY_SIZE(port->groups); i++)
		kfree(port->groups[i].attrs);
	if (port->hw_stats_data)
		kfree(port->hw_stats_data->stats);
	kfree(port->hw_stats_data);
	kfree(port);
}

static void ib_port_gid_attr_release(struct kobject *kobj)
{
	struct gid_attr_group *gid_attr_group =
		container_of(kobj, struct gid_attr_group, kobj);
	int i;

	for (i = 0; i != ARRAY_SIZE(gid_attr_group->groups); i++)
		kfree(gid_attr_group->groups[i].attrs);
	kfree(gid_attr_group);
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
			   u32 port_num, int index)
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

static ssize_t show_hw_stats(struct ib_device *ibdev,
			     struct rdma_hw_stats *stats, unsigned int index,
			     unsigned int port_num, char *buf)
{
	int ret;

	mutex_lock(&stats->lock);
	ret = update_hw_stats(ibdev, stats, port_num, index);
	if (ret)
		goto unlock;
	ret = print_hw_stat(ibdev, port_num, stats, index, buf);
unlock:
	mutex_unlock(&stats->lock);

	return ret;
}

static ssize_t show_stats_lifespan(struct ib_device *ibdev,
				   struct rdma_hw_stats *stats,
				   unsigned int index, unsigned int port_num,
				   char *buf)
{
	int msecs;

	mutex_lock(&stats->lock);
	msecs = jiffies_to_msecs(stats->lifespan);
	mutex_unlock(&stats->lock);

	return sysfs_emit(buf, "%d\n", msecs);
}

static ssize_t set_stats_lifespan(struct ib_device *ibdev,
				   struct rdma_hw_stats *stats,
				   unsigned int index, unsigned int port_num,
				   const char *buf, size_t count)
{
	int msecs;
	int jiffies;
	int ret;

	ret = kstrtoint(buf, 10, &msecs);
	if (ret)
		return ret;
	if (msecs < 0 || msecs > 10000)
		return -EINVAL;
	jiffies = msecs_to_jiffies(msecs);

	mutex_lock(&stats->lock);
	stats->lifespan = jiffies;
	mutex_unlock(&stats->lock);

	return count;
}

static struct hw_stats_device_data *
alloc_hw_stats_device(struct ib_device *ibdev)
{
	struct hw_stats_device_data *data;
	struct rdma_hw_stats *stats;

	if (!ibdev->ops.alloc_hw_device_stats)
		return ERR_PTR(-EOPNOTSUPP);
	stats = ibdev->ops.alloc_hw_device_stats(ibdev);
	if (!stats)
		return ERR_PTR(-ENOMEM);
	if (!stats->names || stats->num_counters <= 0)
		goto err_free_stats;

	/*
	 * Two extra attribue elements here, one for the lifespan entry and
	 * one to NULL terminate the list for the sysfs core code
	 */
	data = kzalloc(struct_size(data, attrs, stats->num_counters + 1),
		       GFP_KERNEL);
	if (!data)
		goto err_free_stats;
	data->group.attrs = kcalloc(stats->num_counters + 2,
				    sizeof(*data->group.attrs), GFP_KERNEL);
	if (!data->group.attrs)
		goto err_free_data;

	mutex_init(&stats->lock);
	data->group.name = "hw_counters";
	data->stats = stats;
	return data;

err_free_data:
	kfree(data);
err_free_stats:
	kfree(stats);
	return ERR_PTR(-ENOMEM);
}

void ib_device_release_hw_stats(struct hw_stats_device_data *data)
{
	kfree(data->group.attrs);
	kfree(data->stats);
	kfree(data);
}

int ib_setup_device_attrs(struct ib_device *ibdev)
{
	struct hw_stats_device_attribute *attr;
	struct hw_stats_device_data *data;
	int i, ret;

	data = alloc_hw_stats_device(ibdev);
	if (IS_ERR(data)) {
		if (PTR_ERR(data) == -EOPNOTSUPP)
			return 0;
		return PTR_ERR(data);
	}
	ibdev->hw_stats_data = data;

	ret = ibdev->ops.get_hw_stats(ibdev, data->stats, 0,
				      data->stats->num_counters);
	if (ret != data->stats->num_counters) {
		if (WARN_ON(ret >= 0))
			return -EINVAL;
		return ret;
	}

	data->stats->timestamp = jiffies;

	for (i = 0; i < data->stats->num_counters; i++) {
		attr = &data->attrs[i];
		sysfs_attr_init(&attr->attr.attr);
		attr->attr.attr.name = data->stats->names[i];
		attr->attr.attr.mode = 0444;
		attr->attr.show = hw_stat_device_show;
		attr->show = show_hw_stats;
		data->group.attrs[i] = &attr->attr.attr;
	}

	attr = &data->attrs[i];
	sysfs_attr_init(&attr->attr.attr);
	attr->attr.attr.name = "lifespan";
	attr->attr.attr.mode = 0644;
	attr->attr.show = hw_stat_device_show;
	attr->show = show_stats_lifespan;
	attr->attr.store = hw_stat_device_store;
	attr->store = set_stats_lifespan;
	data->group.attrs[i] = &attr->attr.attr;
	for (i = 0; i != ARRAY_SIZE(ibdev->groups); i++)
		if (!ibdev->groups[i]) {
			ibdev->groups[i] = &data->group;
			return 0;
		}
	WARN(true, "struct ib_device->groups is too small");
	return -EINVAL;
}

static struct hw_stats_port_data *
alloc_hw_stats_port(struct ib_port *port, struct attribute_group *group)
{
	struct ib_device *ibdev = port->ibdev;
	struct hw_stats_port_data *data;
	struct rdma_hw_stats *stats;

	if (!ibdev->ops.alloc_hw_port_stats)
		return ERR_PTR(-EOPNOTSUPP);
	stats = ibdev->ops.alloc_hw_port_stats(port->ibdev, port->port_num);
	if (!stats)
		return ERR_PTR(-ENOMEM);
	if (!stats->names || stats->num_counters <= 0)
		goto err_free_stats;

	/*
	 * Two extra attribue elements here, one for the lifespan entry and
	 * one to NULL terminate the list for the sysfs core code
	 */
	data = kzalloc(struct_size(data, attrs, stats->num_counters + 1),
		       GFP_KERNEL);
	if (!data)
		goto err_free_stats;
	group->attrs = kcalloc(stats->num_counters + 2,
				    sizeof(*group->attrs), GFP_KERNEL);
	if (!group->attrs)
		goto err_free_data;

	mutex_init(&stats->lock);
	group->name = "hw_counters";
	data->stats = stats;
	return data;

err_free_data:
	kfree(data);
err_free_stats:
	kfree(stats);
	return ERR_PTR(-ENOMEM);
}

static int setup_hw_port_stats(struct ib_port *port,
			       struct attribute_group *group)
{
	struct hw_stats_port_attribute *attr;
	struct hw_stats_port_data *data;
	int i, ret;

	data = alloc_hw_stats_port(port, group);
	if (IS_ERR(data))
		return PTR_ERR(data);

	ret = port->ibdev->ops.get_hw_stats(port->ibdev, data->stats,
					    port->port_num,
					    data->stats->num_counters);
	if (ret != data->stats->num_counters) {
		if (WARN_ON(ret >= 0))
			return -EINVAL;
		return ret;
	}

	data->stats->timestamp = jiffies;

	for (i = 0; i < data->stats->num_counters; i++) {
		attr = &data->attrs[i];
		sysfs_attr_init(&attr->attr.attr);
		attr->attr.attr.name = data->stats->names[i];
		attr->attr.attr.mode = 0444;
		attr->attr.show = hw_stat_port_show;
		attr->show = show_hw_stats;
		group->attrs[i] = &attr->attr.attr;
	}

	attr = &data->attrs[i];
	sysfs_attr_init(&attr->attr.attr);
	attr->attr.attr.name = "lifespan";
	attr->attr.attr.mode = 0644;
	attr->attr.show = hw_stat_port_show;
	attr->show = show_stats_lifespan;
	attr->attr.store = hw_stat_port_store;
	attr->store = set_stats_lifespan;
	group->attrs[i] = &attr->attr.attr;

	port->hw_stats_data = data;
	return 0;
}

struct rdma_hw_stats *ib_get_hw_stats_port(struct ib_device *ibdev,
					   u32 port_num)
{
	if (!ibdev->port_data || !rdma_is_port_valid(ibdev, port_num) ||
	    !ibdev->port_data[port_num].sysfs->hw_stats_data)
		return NULL;
	return ibdev->port_data[port_num].sysfs->hw_stats_data->stats;
}

static int
alloc_port_table_group(const char *name, struct attribute_group *group,
		       struct port_table_attribute *attrs, size_t num,
		       ssize_t (*show)(struct ib_device *ibdev, u32 port_num,
				       struct ib_port_attribute *, char *buf))
{
	struct attribute **attr_list;
	int i;

	attr_list = kcalloc(num + 1, sizeof(*attr_list), GFP_KERNEL);
	if (!attr_list)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		struct port_table_attribute *element = &attrs[i];

		if (snprintf(element->name, sizeof(element->name), "%d", i) >=
		    sizeof(element->name))
			goto err;

		sysfs_attr_init(&element->attr.attr);
		element->attr.attr.name = element->name;
		element->attr.attr.mode = 0444;
		element->attr.show = show;
		element->index = i;

		attr_list[i] = &element->attr.attr;
	}
	group->name = name;
	group->attrs = attr_list;
	return 0;
err:
	kfree(attr_list);
	return -EINVAL;
}

/*
 * Create the sysfs:
 *  ibp0s9/ports/XX/gid_attrs/{ndevs,types}/YYY
 * YYY is the gid table index in decimal
 */
static int setup_gid_attrs(struct ib_port *port,
			   const struct ib_port_attr *attr)
{
	struct gid_attr_group *gid_attr_group;
	int ret;

	gid_attr_group = kzalloc(struct_size(gid_attr_group, attrs_list,
					     attr->gid_tbl_len * 2),
				 GFP_KERNEL);
	if (!gid_attr_group)
		return -ENOMEM;
	gid_attr_group->port = port;
	kobject_init(&gid_attr_group->kobj, &gid_attr_type);

	ret = alloc_port_table_group("ndevs", &gid_attr_group->groups[0],
				     gid_attr_group->attrs_list,
				     attr->gid_tbl_len,
				     show_port_gid_attr_ndev);
	if (ret)
		goto err_put;
	gid_attr_group->groups_list[0] = &gid_attr_group->groups[0];

	ret = alloc_port_table_group(
		"types", &gid_attr_group->groups[1],
		gid_attr_group->attrs_list + attr->gid_tbl_len,
		attr->gid_tbl_len, show_port_gid_attr_gid_type);
	if (ret)
		goto err_put;
	gid_attr_group->groups_list[1] = &gid_attr_group->groups[1];

	ret = kobject_add(&gid_attr_group->kobj, &port->kobj, "gid_attrs");
	if (ret)
		goto err_put;
	ret = sysfs_create_groups(&gid_attr_group->kobj,
				  gid_attr_group->groups_list);
	if (ret)
		goto err_del;
	port->gid_attr_group = gid_attr_group;
	return 0;

err_del:
	kobject_del(&gid_attr_group->kobj);
err_put:
	kobject_put(&gid_attr_group->kobj);
	return ret;
}

static void destroy_gid_attrs(struct ib_port *port)
{
	struct gid_attr_group *gid_attr_group = port->gid_attr_group;

	if (!gid_attr_group)
		return;
	sysfs_remove_groups(&gid_attr_group->kobj, gid_attr_group->groups_list);
	kobject_del(&gid_attr_group->kobj);
	kobject_put(&gid_attr_group->kobj);
}

/*
 * Create the sysfs:
 *  ibp0s9/ports/XX/{gids,pkeys,counters}/YYY
 */
static struct ib_port *setup_port(struct ib_core_device *coredev, int port_num,
				  const struct ib_port_attr *attr)
{
	struct ib_device *device = rdma_device_to_ibdev(&coredev->dev);
	bool is_full_dev = &device->coredev == coredev;
	const struct attribute_group **cur_group;
	struct ib_port *p;
	int ret;

	p = kzalloc(struct_size(p, attrs_list,
				attr->gid_tbl_len + attr->pkey_tbl_len),
		    GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);
	p->ibdev = device;
	p->port_num = port_num;
	kobject_init(&p->kobj, &port_type);

	cur_group = p->groups_list;
	ret = alloc_port_table_group("gids", &p->groups[0], p->attrs_list,
				     attr->gid_tbl_len, show_port_gid);
	if (ret)
		goto err_put;
	*cur_group++ = &p->groups[0];

	if (attr->pkey_tbl_len) {
		ret = alloc_port_table_group("pkeys", &p->groups[1],
					     p->attrs_list + attr->gid_tbl_len,
					     attr->pkey_tbl_len, show_port_pkey);
		if (ret)
			goto err_put;
		*cur_group++ = &p->groups[1];
	}

	/*
	 * If port == 0, it means hw_counters are per device and not per
	 * port, so holder should be device. Therefore skip per port
	 * counter initialization.
	 */
	if (port_num && is_full_dev) {
		ret = setup_hw_port_stats(p, &p->groups[2]);
		if (ret && ret != -EOPNOTSUPP)
			goto err_put;
		if (!ret)
			*cur_group++ = &p->groups[2];
	}

	if (device->ops.process_mad && is_full_dev)
		*cur_group++ = get_counter_table(device, port_num);

	ret = kobject_add(&p->kobj, coredev->ports_kobj, "%d", port_num);
	if (ret)
		goto err_put;
	ret = sysfs_create_groups(&p->kobj, p->groups_list);
	if (ret)
		goto err_del;
	if (is_full_dev) {
		ret = sysfs_create_groups(&p->kobj, device->ops.port_groups);
		if (ret)
			goto err_groups;
	}

	list_add_tail(&p->kobj.entry, &coredev->port_list);
	if (device->port_data && is_full_dev)
		device->port_data[port_num].sysfs = p;

	return p;

err_groups:
	sysfs_remove_groups(&p->kobj, p->groups_list);
err_del:
	kobject_del(&p->kobj);
err_put:
	kobject_put(&p->kobj);
	return ERR_PTR(ret);
}

static void destroy_port(struct ib_core_device *coredev, struct ib_port *port)
{
	bool is_full_dev = &port->ibdev->coredev == coredev;

	if (port->ibdev->port_data &&
	    port->ibdev->port_data[port->port_num].sysfs == port)
		port->ibdev->port_data[port->port_num].sysfs = NULL;
	list_del(&port->kobj.entry);
	if (is_full_dev)
		sysfs_remove_groups(&port->kobj, port->ibdev->ops.port_groups);
	sysfs_remove_groups(&port->kobj, port->groups_list);
	kobject_del(&port->kobj);
	kobject_put(&port->kobj);
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

	return sysfs_emit(buf, "%u: %s\n", dev->node_type,
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
	struct kobject *p, *t;

	list_for_each_entry_safe(p, t, &coredev->port_list, entry) {
		struct ib_port *port = container_of(p, struct ib_port, kobj);

		destroy_gid_attrs(port);
		destroy_port(coredev, port);
	}

	kobject_put(coredev->ports_kobj);
}

int ib_setup_port_attrs(struct ib_core_device *coredev)
{
	struct ib_device *device = rdma_device_to_ibdev(&coredev->dev);
	u32 port_num;
	int ret;

	coredev->ports_kobj = kobject_create_and_add("ports",
						     &coredev->dev.kobj);
	if (!coredev->ports_kobj)
		return -ENOMEM;

	rdma_for_each_port (device, port_num) {
		struct ib_port_attr attr;
		struct ib_port *port;

		ret = ib_query_port(device, port_num, &attr);
		if (ret)
			goto err_put;

		port = setup_port(coredev, port_num, &attr);
		if (IS_ERR(port)) {
			ret = PTR_ERR(port);
			goto err_put;
		}

		ret = setup_gid_attrs(port, &attr);
		if (ret)
			goto err_put;
	}
	return 0;

err_put:
	ib_free_port_attrs(coredev);
	return ret;
}

/**
 * ib_port_register_client_groups - Add an ib_client's attributes to the port
 *
 * @ibdev: IB device to add counters
 * @port_num: valid port number
 * @groups: Group list of attributes
 *
 * Do not use. Only for legacy sysfs compatibility.
 */
int ib_port_register_client_groups(struct ib_device *ibdev, u32 port_num,
				   const struct attribute_group **groups)
{
	return sysfs_create_groups(&ibdev->port_data[port_num].sysfs->kobj,
				   groups);
}
EXPORT_SYMBOL(ib_port_register_client_groups);

void ib_port_unregister_client_groups(struct ib_device *ibdev, u32 port_num,
				      const struct attribute_group **groups)
{
	return sysfs_remove_groups(&ibdev->port_data[port_num].sysfs->kobj,
				   groups);
}
EXPORT_SYMBOL(ib_port_unregister_client_groups);
