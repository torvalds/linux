/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2004, 2005, 2012 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/configfs.h>

#include "tcp.h"
#include "nodemanager.h"
#include "heartbeat.h"
#include "masklog.h"

/* for now we operate under the assertion that there can be only one
 * cluster active at a time.  Changing this will require trickling
 * cluster references throughout where nodes are looked up */
struct r2nm_cluster *r2nm_single_cluster;

char *r2nm_fence_method_desc[R2NM_FENCE_METHODS] = {
		"reset",	/* R2NM_FENCE_RESET */
		"panic",	/* R2NM_FENCE_PANIC */
};

struct r2nm_node *r2nm_get_node_by_num(u8 node_num)
{
	struct r2nm_node *node = NULL;

	if (node_num >= R2NM_MAX_NODES || r2nm_single_cluster == NULL)
		goto out;

	read_lock(&r2nm_single_cluster->cl_nodes_lock);
	node = r2nm_single_cluster->cl_nodes[node_num];
	if (node)
		config_item_get(&node->nd_item);
	read_unlock(&r2nm_single_cluster->cl_nodes_lock);
out:
	return node;
}
EXPORT_SYMBOL_GPL(r2nm_get_node_by_num);

int r2nm_configured_node_map(unsigned long *map, unsigned bytes)
{
	struct r2nm_cluster *cluster = r2nm_single_cluster;

	BUG_ON(bytes < (sizeof(cluster->cl_nodes_bitmap)));

	if (cluster == NULL)
		return -EINVAL;

	read_lock(&cluster->cl_nodes_lock);
	memcpy(map, cluster->cl_nodes_bitmap, sizeof(cluster->cl_nodes_bitmap));
	read_unlock(&cluster->cl_nodes_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(r2nm_configured_node_map);

static struct r2nm_node *r2nm_node_ip_tree_lookup(struct r2nm_cluster *cluster,
						  __be32 ip_needle,
						  struct rb_node ***ret_p,
						  struct rb_node **ret_parent)
{
	struct rb_node **p = &cluster->cl_node_ip_tree.rb_node;
	struct rb_node *parent = NULL;
	struct r2nm_node *node, *ret = NULL;

	while (*p) {
		int cmp;

		parent = *p;
		node = rb_entry(parent, struct r2nm_node, nd_ip_node);

		cmp = memcmp(&ip_needle, &node->nd_ipv4_address,
				sizeof(ip_needle));
		if (cmp < 0)
			p = &(*p)->rb_left;
		else if (cmp > 0)
			p = &(*p)->rb_right;
		else {
			ret = node;
			break;
		}
	}

	if (ret_p != NULL)
		*ret_p = p;
	if (ret_parent != NULL)
		*ret_parent = parent;

	return ret;
}

struct r2nm_node *r2nm_get_node_by_ip(__be32 addr)
{
	struct r2nm_node *node = NULL;
	struct r2nm_cluster *cluster = r2nm_single_cluster;

	if (cluster == NULL)
		goto out;

	read_lock(&cluster->cl_nodes_lock);
	node = r2nm_node_ip_tree_lookup(cluster, addr, NULL, NULL);
	if (node)
		config_item_get(&node->nd_item);
	read_unlock(&cluster->cl_nodes_lock);

out:
	return node;
}
EXPORT_SYMBOL_GPL(r2nm_get_node_by_ip);

void r2nm_node_put(struct r2nm_node *node)
{
	config_item_put(&node->nd_item);
}
EXPORT_SYMBOL_GPL(r2nm_node_put);

void r2nm_node_get(struct r2nm_node *node)
{
	config_item_get(&node->nd_item);
}
EXPORT_SYMBOL_GPL(r2nm_node_get);

u8 r2nm_this_node(void)
{
	u8 node_num = R2NM_MAX_NODES;

	if (r2nm_single_cluster && r2nm_single_cluster->cl_has_local)
		node_num = r2nm_single_cluster->cl_local_node;

	return node_num;
}
EXPORT_SYMBOL_GPL(r2nm_this_node);

/* node configfs bits */

static struct r2nm_cluster *to_r2nm_cluster(struct config_item *item)
{
	return item ?
		container_of(to_config_group(item), struct r2nm_cluster,
			     cl_group)
		: NULL;
}

static struct r2nm_node *to_r2nm_node(struct config_item *item)
{
	return item ? container_of(item, struct r2nm_node, nd_item) : NULL;
}

static void r2nm_node_release(struct config_item *item)
{
	struct r2nm_node *node = to_r2nm_node(item);
	kfree(node);
}

static ssize_t r2nm_node_num_read(struct r2nm_node *node, char *page)
{
	return sprintf(page, "%d\n", node->nd_num);
}

static struct r2nm_cluster *to_r2nm_cluster_from_node(struct r2nm_node *node)
{
	/* through the first node_set .parent
	 * mycluster/nodes/mynode == r2nm_cluster->r2nm_node_group->r2nm_node */
	return to_r2nm_cluster(node->nd_item.ci_parent->ci_parent);
}

enum {
	R2NM_NODE_ATTR_NUM = 0,
	R2NM_NODE_ATTR_PORT,
	R2NM_NODE_ATTR_ADDRESS,
	R2NM_NODE_ATTR_LOCAL,
};

static ssize_t r2nm_node_num_write(struct r2nm_node *node, const char *page,
				   size_t count)
{
	struct r2nm_cluster *cluster = to_r2nm_cluster_from_node(node);
	unsigned long tmp;
	char *p = (char *)page;
	int err;

	err = kstrtoul(p, 10, &tmp);
	if (err)
		return err;

	if (tmp >= R2NM_MAX_NODES)
		return -ERANGE;

	/* once we're in the cl_nodes tree networking can look us up by
	 * node number and try to use our address and port attributes
	 * to connect to this node.. make sure that they've been set
	 * before writing the node attribute? */
	if (!test_bit(R2NM_NODE_ATTR_ADDRESS, &node->nd_set_attributes) ||
	    !test_bit(R2NM_NODE_ATTR_PORT, &node->nd_set_attributes))
		return -EINVAL; /* XXX */

	write_lock(&cluster->cl_nodes_lock);
	if (cluster->cl_nodes[tmp])
		p = NULL;
	else  {
		cluster->cl_nodes[tmp] = node;
		node->nd_num = tmp;
		set_bit(tmp, cluster->cl_nodes_bitmap);
	}
	write_unlock(&cluster->cl_nodes_lock);
	if (p == NULL)
		return -EEXIST;

	return count;
}
static ssize_t r2nm_node_ipv4_port_read(struct r2nm_node *node, char *page)
{
	return sprintf(page, "%u\n", ntohs(node->nd_ipv4_port));
}

static ssize_t r2nm_node_ipv4_port_write(struct r2nm_node *node,
					 const char *page, size_t count)
{
	unsigned long tmp;
	char *p = (char *)page;
	int err;

	err = kstrtoul(p, 10, &tmp);
	if (err)
		return err;

	if (tmp == 0)
		return -EINVAL;
	if (tmp >= (u16)-1)
		return -ERANGE;

	node->nd_ipv4_port = htons(tmp);

	return count;
}

static ssize_t r2nm_node_ipv4_address_read(struct r2nm_node *node, char *page)
{
	return sprintf(page, "%pI4\n", &node->nd_ipv4_address);
}

static ssize_t r2nm_node_ipv4_address_write(struct r2nm_node *node,
					    const char *page,
					    size_t count)
{
	struct r2nm_cluster *cluster = to_r2nm_cluster_from_node(node);
	int ret, i;
	struct rb_node **p, *parent;
	unsigned int octets[4];
	__be32 ipv4_addr = 0;

	ret = sscanf(page, "%3u.%3u.%3u.%3u", &octets[3], &octets[2],
		     &octets[1], &octets[0]);
	if (ret != 4)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(octets); i++) {
		if (octets[i] > 255)
			return -ERANGE;
		be32_add_cpu(&ipv4_addr, octets[i] << (i * 8));
	}

	ret = 0;
	write_lock(&cluster->cl_nodes_lock);
	if (r2nm_node_ip_tree_lookup(cluster, ipv4_addr, &p, &parent))
		ret = -EEXIST;
	else {
		rb_link_node(&node->nd_ip_node, parent, p);
		rb_insert_color(&node->nd_ip_node, &cluster->cl_node_ip_tree);
	}
	write_unlock(&cluster->cl_nodes_lock);
	if (ret)
		return ret;

	memcpy(&node->nd_ipv4_address, &ipv4_addr, sizeof(ipv4_addr));

	return count;
}

static ssize_t r2nm_node_local_read(struct r2nm_node *node, char *page)
{
	return sprintf(page, "%d\n", node->nd_local);
}

static ssize_t r2nm_node_local_write(struct r2nm_node *node, const char *page,
				     size_t count)
{
	struct r2nm_cluster *cluster = to_r2nm_cluster_from_node(node);
	unsigned long tmp;
	char *p = (char *)page;
	ssize_t ret;
	int err;

	err = kstrtoul(p, 10, &tmp);
	if (err)
		return err;

	tmp = !!tmp; /* boolean of whether this node wants to be local */

	/* setting local turns on networking rx for now so we require having
	 * set everything else first */
	if (!test_bit(R2NM_NODE_ATTR_ADDRESS, &node->nd_set_attributes) ||
	    !test_bit(R2NM_NODE_ATTR_NUM, &node->nd_set_attributes) ||
	    !test_bit(R2NM_NODE_ATTR_PORT, &node->nd_set_attributes))
		return -EINVAL; /* XXX */

	/* the only failure case is trying to set a new local node
	 * when a different one is already set */
	if (tmp && tmp == cluster->cl_has_local &&
	    cluster->cl_local_node != node->nd_num)
		return -EBUSY;

	/* bring up the rx thread if we're setting the new local node. */
	if (tmp && !cluster->cl_has_local) {
		ret = r2net_start_listening(node);
		if (ret)
			return ret;
	}

	if (!tmp && cluster->cl_has_local &&
	    cluster->cl_local_node == node->nd_num) {
		r2net_stop_listening(node);
		cluster->cl_local_node = R2NM_INVALID_NODE_NUM;
	}

	node->nd_local = tmp;
	if (node->nd_local) {
		cluster->cl_has_local = tmp;
		cluster->cl_local_node = node->nd_num;
	}

	return count;
}

struct r2nm_node_attribute {
	struct configfs_attribute attr;
	ssize_t (*show)(struct r2nm_node *, char *);
	ssize_t (*store)(struct r2nm_node *, const char *, size_t);
};

static struct r2nm_node_attribute r2nm_node_attr_num = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "num",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= r2nm_node_num_read,
	.store	= r2nm_node_num_write,
};

static struct r2nm_node_attribute r2nm_node_attr_ipv4_port = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "ipv4_port",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= r2nm_node_ipv4_port_read,
	.store	= r2nm_node_ipv4_port_write,
};

static struct r2nm_node_attribute r2nm_node_attr_ipv4_address = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "ipv4_address",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= r2nm_node_ipv4_address_read,
	.store	= r2nm_node_ipv4_address_write,
};

static struct r2nm_node_attribute r2nm_node_attr_local = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "local",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= r2nm_node_local_read,
	.store	= r2nm_node_local_write,
};

static struct configfs_attribute *r2nm_node_attrs[] = {
	[R2NM_NODE_ATTR_NUM] = &r2nm_node_attr_num.attr,
	[R2NM_NODE_ATTR_PORT] = &r2nm_node_attr_ipv4_port.attr,
	[R2NM_NODE_ATTR_ADDRESS] = &r2nm_node_attr_ipv4_address.attr,
	[R2NM_NODE_ATTR_LOCAL] = &r2nm_node_attr_local.attr,
	NULL,
};

static int r2nm_attr_index(struct configfs_attribute *attr)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(r2nm_node_attrs); i++) {
		if (attr == r2nm_node_attrs[i])
			return i;
	}
	BUG();
	return 0;
}

static ssize_t r2nm_node_show(struct config_item *item,
			      struct configfs_attribute *attr,
			      char *page)
{
	struct r2nm_node *node = to_r2nm_node(item);
	struct r2nm_node_attribute *r2nm_node_attr =
		container_of(attr, struct r2nm_node_attribute, attr);
	ssize_t ret = 0;

	if (r2nm_node_attr->show)
		ret = r2nm_node_attr->show(node, page);
	return ret;
}

static ssize_t r2nm_node_store(struct config_item *item,
			       struct configfs_attribute *attr,
			       const char *page, size_t count)
{
	struct r2nm_node *node = to_r2nm_node(item);
	struct r2nm_node_attribute *r2nm_node_attr =
		container_of(attr, struct r2nm_node_attribute, attr);
	ssize_t ret;
	int attr_index = r2nm_attr_index(attr);

	if (r2nm_node_attr->store == NULL) {
		ret = -EINVAL;
		goto out;
	}

	if (test_bit(attr_index, &node->nd_set_attributes))
		return -EBUSY;

	ret = r2nm_node_attr->store(node, page, count);
	if (ret < count)
		goto out;

	set_bit(attr_index, &node->nd_set_attributes);
out:
	return ret;
}

static struct configfs_item_operations r2nm_node_item_ops = {
	.release		= r2nm_node_release,
	.show_attribute		= r2nm_node_show,
	.store_attribute	= r2nm_node_store,
};

static struct config_item_type r2nm_node_type = {
	.ct_item_ops	= &r2nm_node_item_ops,
	.ct_attrs	= r2nm_node_attrs,
	.ct_owner	= THIS_MODULE,
};

/* node set */

struct r2nm_node_group {
	struct config_group ns_group;
	/* some stuff? */
};

#if 0
static struct r2nm_node_group *to_r2nm_node_group(struct config_group *group)
{
	return group ?
		container_of(group, struct r2nm_node_group, ns_group)
		: NULL;
}
#endif

struct r2nm_cluster_attribute {
	struct configfs_attribute attr;
	ssize_t (*show)(struct r2nm_cluster *, char *);
	ssize_t (*store)(struct r2nm_cluster *, const char *, size_t);
};

static ssize_t r2nm_cluster_attr_write(const char *page, ssize_t count,
					unsigned int *val)
{
	unsigned long tmp;
	char *p = (char *)page;
	int err;

	err = kstrtoul(p, 10, &tmp);
	if (err)
		return err;

	if (tmp == 0)
		return -EINVAL;
	if (tmp >= (u32)-1)
		return -ERANGE;

	*val = tmp;

	return count;
}

static ssize_t r2nm_cluster_attr_idle_timeout_ms_read(
	struct r2nm_cluster *cluster, char *page)
{
	return sprintf(page, "%u\n", cluster->cl_idle_timeout_ms);
}

static ssize_t r2nm_cluster_attr_idle_timeout_ms_write(
	struct r2nm_cluster *cluster, const char *page, size_t count)
{
	ssize_t ret;
	unsigned int val = 0;

	ret =  r2nm_cluster_attr_write(page, count, &val);

	if (ret > 0) {
		if (cluster->cl_idle_timeout_ms != val
			&& r2net_num_connected_peers()) {
			mlog(ML_NOTICE,
			     "r2net: cannot change idle timeout after "
			     "the first peer has agreed to it."
			     "  %d connected peers\n",
			     r2net_num_connected_peers());
			ret = -EINVAL;
		} else if (val <= cluster->cl_keepalive_delay_ms) {
			mlog(ML_NOTICE,
			     "r2net: idle timeout must be larger "
			     "than keepalive delay\n");
			ret = -EINVAL;
		} else {
			cluster->cl_idle_timeout_ms = val;
		}
	}

	return ret;
}

static ssize_t r2nm_cluster_attr_keepalive_delay_ms_read(
	struct r2nm_cluster *cluster, char *page)
{
	return sprintf(page, "%u\n", cluster->cl_keepalive_delay_ms);
}

static ssize_t r2nm_cluster_attr_keepalive_delay_ms_write(
	struct r2nm_cluster *cluster, const char *page, size_t count)
{
	ssize_t ret;
	unsigned int val = 0;

	ret =  r2nm_cluster_attr_write(page, count, &val);

	if (ret > 0) {
		if (cluster->cl_keepalive_delay_ms != val
		    && r2net_num_connected_peers()) {
			mlog(ML_NOTICE,
			     "r2net: cannot change keepalive delay after"
			     " the first peer has agreed to it."
			     "  %d connected peers\n",
			     r2net_num_connected_peers());
			ret = -EINVAL;
		} else if (val >= cluster->cl_idle_timeout_ms) {
			mlog(ML_NOTICE,
			     "r2net: keepalive delay must be "
			     "smaller than idle timeout\n");
			ret = -EINVAL;
		} else {
			cluster->cl_keepalive_delay_ms = val;
		}
	}

	return ret;
}

static ssize_t r2nm_cluster_attr_reconnect_delay_ms_read(
	struct r2nm_cluster *cluster, char *page)
{
	return sprintf(page, "%u\n", cluster->cl_reconnect_delay_ms);
}

static ssize_t r2nm_cluster_attr_reconnect_delay_ms_write(
	struct r2nm_cluster *cluster, const char *page, size_t count)
{
	return r2nm_cluster_attr_write(page, count,
					&cluster->cl_reconnect_delay_ms);
}

static ssize_t r2nm_cluster_attr_fence_method_read(
	struct r2nm_cluster *cluster, char *page)
{
	ssize_t ret = 0;

	if (cluster)
		ret = sprintf(page, "%s\n",
			      r2nm_fence_method_desc[cluster->cl_fence_method]);
	return ret;
}

static ssize_t r2nm_cluster_attr_fence_method_write(
	struct r2nm_cluster *cluster, const char *page, size_t count)
{
	unsigned int i;

	if (page[count - 1] != '\n')
		goto bail;

	for (i = 0; i < R2NM_FENCE_METHODS; ++i) {
		if (count != strlen(r2nm_fence_method_desc[i]) + 1)
			continue;
		if (strncasecmp(page, r2nm_fence_method_desc[i], count - 1))
			continue;
		if (cluster->cl_fence_method != i) {
			pr_info("ramster: Changing fence method to %s\n",
			       r2nm_fence_method_desc[i]);
			cluster->cl_fence_method = i;
		}
		return count;
	}

bail:
	return -EINVAL;
}

static struct r2nm_cluster_attribute r2nm_cluster_attr_idle_timeout_ms = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "idle_timeout_ms",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= r2nm_cluster_attr_idle_timeout_ms_read,
	.store	= r2nm_cluster_attr_idle_timeout_ms_write,
};

static struct r2nm_cluster_attribute r2nm_cluster_attr_keepalive_delay_ms = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "keepalive_delay_ms",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= r2nm_cluster_attr_keepalive_delay_ms_read,
	.store	= r2nm_cluster_attr_keepalive_delay_ms_write,
};

static struct r2nm_cluster_attribute r2nm_cluster_attr_reconnect_delay_ms = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "reconnect_delay_ms",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= r2nm_cluster_attr_reconnect_delay_ms_read,
	.store	= r2nm_cluster_attr_reconnect_delay_ms_write,
};

static struct r2nm_cluster_attribute r2nm_cluster_attr_fence_method = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "fence_method",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= r2nm_cluster_attr_fence_method_read,
	.store	= r2nm_cluster_attr_fence_method_write,
};

static struct configfs_attribute *r2nm_cluster_attrs[] = {
	&r2nm_cluster_attr_idle_timeout_ms.attr,
	&r2nm_cluster_attr_keepalive_delay_ms.attr,
	&r2nm_cluster_attr_reconnect_delay_ms.attr,
	&r2nm_cluster_attr_fence_method.attr,
	NULL,
};
static ssize_t r2nm_cluster_show(struct config_item *item,
					struct configfs_attribute *attr,
					char *page)
{
	struct r2nm_cluster *cluster = to_r2nm_cluster(item);
	struct r2nm_cluster_attribute *r2nm_cluster_attr =
		container_of(attr, struct r2nm_cluster_attribute, attr);
	ssize_t ret = 0;

	if (r2nm_cluster_attr->show)
		ret = r2nm_cluster_attr->show(cluster, page);
	return ret;
}

static ssize_t r2nm_cluster_store(struct config_item *item,
					struct configfs_attribute *attr,
					const char *page, size_t count)
{
	struct r2nm_cluster *cluster = to_r2nm_cluster(item);
	struct r2nm_cluster_attribute *r2nm_cluster_attr =
		container_of(attr, struct r2nm_cluster_attribute, attr);
	ssize_t ret;

	if (r2nm_cluster_attr->store == NULL) {
		ret = -EINVAL;
		goto out;
	}

	ret = r2nm_cluster_attr->store(cluster, page, count);
	if (ret < count)
		goto out;
out:
	return ret;
}

static struct config_item *r2nm_node_group_make_item(struct config_group *group,
						     const char *name)
{
	struct r2nm_node *node = NULL;

	if (strlen(name) > R2NM_MAX_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	node = kzalloc(sizeof(struct r2nm_node), GFP_KERNEL);
	if (node == NULL)
		return ERR_PTR(-ENOMEM);

	strcpy(node->nd_name, name); /* use item.ci_namebuf instead? */
	config_item_init_type_name(&node->nd_item, name, &r2nm_node_type);
	spin_lock_init(&node->nd_lock);

	mlog(ML_CLUSTER, "r2nm: Registering node %s\n", name);

	return &node->nd_item;
}

static void r2nm_node_group_drop_item(struct config_group *group,
				      struct config_item *item)
{
	struct r2nm_node *node = to_r2nm_node(item);
	struct r2nm_cluster *cluster =
				to_r2nm_cluster(group->cg_item.ci_parent);

	r2net_disconnect_node(node);

	if (cluster->cl_has_local &&
	    (cluster->cl_local_node == node->nd_num)) {
		cluster->cl_has_local = 0;
		cluster->cl_local_node = R2NM_INVALID_NODE_NUM;
		r2net_stop_listening(node);
	}

	/* XXX call into net to stop this node from trading messages */

	write_lock(&cluster->cl_nodes_lock);

	/* XXX sloppy */
	if (node->nd_ipv4_address)
		rb_erase(&node->nd_ip_node, &cluster->cl_node_ip_tree);

	/* nd_num might be 0 if the node number hasn't been set.. */
	if (cluster->cl_nodes[node->nd_num] == node) {
		cluster->cl_nodes[node->nd_num] = NULL;
		clear_bit(node->nd_num, cluster->cl_nodes_bitmap);
	}
	write_unlock(&cluster->cl_nodes_lock);

	mlog(ML_CLUSTER, "r2nm: Unregistered node %s\n",
	     config_item_name(&node->nd_item));

	config_item_put(item);
}

static struct configfs_group_operations r2nm_node_group_group_ops = {
	.make_item	= r2nm_node_group_make_item,
	.drop_item	= r2nm_node_group_drop_item,
};

static struct config_item_type r2nm_node_group_type = {
	.ct_group_ops	= &r2nm_node_group_group_ops,
	.ct_owner	= THIS_MODULE,
};

/* cluster */

static void r2nm_cluster_release(struct config_item *item)
{
	struct r2nm_cluster *cluster = to_r2nm_cluster(item);

	kfree(cluster->cl_group.default_groups);
	kfree(cluster);
}

static struct configfs_item_operations r2nm_cluster_item_ops = {
	.release	= r2nm_cluster_release,
	.show_attribute		= r2nm_cluster_show,
	.store_attribute	= r2nm_cluster_store,
};

static struct config_item_type r2nm_cluster_type = {
	.ct_item_ops	= &r2nm_cluster_item_ops,
	.ct_attrs	= r2nm_cluster_attrs,
	.ct_owner	= THIS_MODULE,
};

/* cluster set */

struct r2nm_cluster_group {
	struct configfs_subsystem cs_subsys;
	/* some stuff? */
};

#if 0
static struct r2nm_cluster_group *
to_r2nm_cluster_group(struct config_group *group)
{
	return group ?
		container_of(to_configfs_subsystem(group),
				struct r2nm_cluster_group, cs_subsys)
	       : NULL;
}
#endif

static struct config_group *
r2nm_cluster_group_make_group(struct config_group *group,
							  const char *name)
{
	struct r2nm_cluster *cluster = NULL;
	struct r2nm_node_group *ns = NULL;
	struct config_group *r2hb_group = NULL, *ret = NULL;
	void *defs = NULL;

	/* this runs under the parent dir's i_mutex; there can be only
	 * one caller in here at a time */
	if (r2nm_single_cluster)
		return ERR_PTR(-ENOSPC);

	cluster = kzalloc(sizeof(struct r2nm_cluster), GFP_KERNEL);
	ns = kzalloc(sizeof(struct r2nm_node_group), GFP_KERNEL);
	defs = kcalloc(3, sizeof(struct config_group *), GFP_KERNEL);
	r2hb_group = r2hb_alloc_hb_set();
	if (cluster == NULL || ns == NULL || r2hb_group == NULL || defs == NULL)
		goto out;

	config_group_init_type_name(&cluster->cl_group, name,
				    &r2nm_cluster_type);
	config_group_init_type_name(&ns->ns_group, "node",
				    &r2nm_node_group_type);

	cluster->cl_group.default_groups = defs;
	cluster->cl_group.default_groups[0] = &ns->ns_group;
	cluster->cl_group.default_groups[1] = r2hb_group;
	cluster->cl_group.default_groups[2] = NULL;
	rwlock_init(&cluster->cl_nodes_lock);
	cluster->cl_node_ip_tree = RB_ROOT;
	cluster->cl_reconnect_delay_ms = R2NET_RECONNECT_DELAY_MS_DEFAULT;
	cluster->cl_idle_timeout_ms    = R2NET_IDLE_TIMEOUT_MS_DEFAULT;
	cluster->cl_keepalive_delay_ms = R2NET_KEEPALIVE_DELAY_MS_DEFAULT;
	cluster->cl_fence_method       = R2NM_FENCE_RESET;

	ret = &cluster->cl_group;
	r2nm_single_cluster = cluster;

out:
	if (ret == NULL) {
		kfree(cluster);
		kfree(ns);
		r2hb_free_hb_set(r2hb_group);
		kfree(defs);
		ret = ERR_PTR(-ENOMEM);
	}

	return ret;
}

static void r2nm_cluster_group_drop_item(struct config_group *group,
						struct config_item *item)
{
	struct r2nm_cluster *cluster = to_r2nm_cluster(item);
	int i;
	struct config_item *killme;

	BUG_ON(r2nm_single_cluster != cluster);
	r2nm_single_cluster = NULL;

	for (i = 0; cluster->cl_group.default_groups[i]; i++) {
		killme = &cluster->cl_group.default_groups[i]->cg_item;
		cluster->cl_group.default_groups[i] = NULL;
		config_item_put(killme);
	}

	config_item_put(item);
}

static struct configfs_group_operations r2nm_cluster_group_group_ops = {
	.make_group	= r2nm_cluster_group_make_group,
	.drop_item	= r2nm_cluster_group_drop_item,
};

static struct config_item_type r2nm_cluster_group_type = {
	.ct_group_ops	= &r2nm_cluster_group_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct r2nm_cluster_group r2nm_cluster_group = {
	.cs_subsys = {
		.su_group = {
			.cg_item = {
				.ci_namebuf = "cluster",
				.ci_type = &r2nm_cluster_group_type,
			},
		},
	},
};

int r2nm_depend_item(struct config_item *item)
{
	return configfs_depend_item(&r2nm_cluster_group.cs_subsys, item);
}

void r2nm_undepend_item(struct config_item *item)
{
	configfs_undepend_item(&r2nm_cluster_group.cs_subsys, item);
}

int r2nm_depend_this_node(void)
{
	int ret = 0;
	struct r2nm_node *local_node;

	local_node = r2nm_get_node_by_num(r2nm_this_node());
	if (!local_node) {
		ret = -EINVAL;
		goto out;
	}

	ret = r2nm_depend_item(&local_node->nd_item);
	r2nm_node_put(local_node);

out:
	return ret;
}

void r2nm_undepend_this_node(void)
{
	struct r2nm_node *local_node;

	local_node = r2nm_get_node_by_num(r2nm_this_node());
	BUG_ON(!local_node);

	r2nm_undepend_item(&local_node->nd_item);
	r2nm_node_put(local_node);
}


static void __exit exit_r2nm(void)
{
	/* XXX sync with hb callbacks and shut down hb? */
	r2net_unregister_hb_callbacks();
	configfs_unregister_subsystem(&r2nm_cluster_group.cs_subsys);

	r2net_exit();
	r2hb_exit();
}

int r2nm_init(void)
{
	int ret = -1;

	ret = r2hb_init();
	if (ret)
		goto out;

	ret = r2net_init();
	if (ret)
		goto out_r2hb;

	ret = r2net_register_hb_callbacks();
	if (ret)
		goto out_r2net;

	config_group_init(&r2nm_cluster_group.cs_subsys.su_group);
	mutex_init(&r2nm_cluster_group.cs_subsys.su_mutex);
	ret = configfs_register_subsystem(&r2nm_cluster_group.cs_subsys);
	if (ret) {
		pr_err("nodemanager: Registration returned %d\n", ret);
		goto out_callbacks;
	}

	if (!ret)
		goto out;

	configfs_unregister_subsystem(&r2nm_cluster_group.cs_subsys);
out_callbacks:
	r2net_unregister_hb_callbacks();
out_r2net:
	r2net_exit();
out_r2hb:
	r2hb_exit();
out:
	return ret;
}
EXPORT_SYMBOL_GPL(r2nm_init);

MODULE_AUTHOR("Oracle");
MODULE_LICENSE("GPL");

#ifndef CONFIG_RAMSTER_MODULE
late_initcall(r2nm_init);
#endif
