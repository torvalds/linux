// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/xarray.h>
#include <net/genetlink.h>

#include <drm/drm_ras.h>

#include "drm_ras_nl.h"

/**
 * DOC: DRM RAS Node Management
 *
 * This module provides the infrastructure to manage RAS (Reliability,
 * Availability, and Serviceability) nodes for DRM drivers. Each
 * DRM driver may register one or more RAS nodes, which represent
 * logical components capable of reporting error counters and other
 * reliability metrics.
 *
 * The nodes are stored in a global xarray `drm_ras_xa` to allow
 * efficient lookup by ID. Nodes can be registered or unregistered
 * dynamically at runtime.
 *
 * A Generic Netlink family `drm_ras` exposes two main operations to
 * userspace:
 *
 * 1. LIST_NODES: Dump all currently registered RAS nodes.
 *    The user receives an array of node IDs, names, and types.
 *
 * 2. GET_ERROR_COUNTER: Get error counters of a given node.
 *    Userspace must provide Node ID, Error ID (Optional for specific counter).
 *    Returns all counters of a node if only Node ID is provided or specific
 *    error counters.
 *
 * Node registration:
 *
 * - drm_ras_node_register(): Registers a new node and assigns
 *   it a unique ID in the xarray.
 * - drm_ras_node_unregister(): Removes a previously registered
 *   node from the xarray.
 *
 * Node type:
 *
 * - ERROR_COUNTER:
 *     + Currently, only error counters are supported.
 *     + The driver must implement the query_error_counter() callback to provide
 *       the name and the value of the error counter.
 *     + The driver must provide a error_counter_range.last value informing the
 *       last valid error ID.
 *     + The driver can provide a error_counter_range.first value informing the
 *       first valid error ID.
 *     + The error counters in the driver doesn't need to be contiguous, but the
 *       driver must return -ENOENT to the query_error_counter as an indication
 *       that the ID should be skipped and not listed in the netlink API.
 *
 * Netlink handlers:
 *
 * - drm_ras_nl_list_nodes_dumpit(): Implements the LIST_NODES
 *   operation, iterating over the xarray.
 * - drm_ras_nl_get_error_counter_dumpit(): Implements the GET_ERROR_COUNTER dumpit
 *   operation, fetching all counters from a specific node.
 * - drm_ras_nl_get_error_counter_doit(): Implements the GET_ERROR_COUNTER doit
 *   operation, fetching a counter value from a specific node.
 */

static DEFINE_XARRAY_ALLOC(drm_ras_xa);

/*
 * The netlink callback context carries dump state across multiple dumpit calls
 */
struct drm_ras_ctx {
	/* Which xarray id to restart the dump from */
	unsigned long restart;
};

/**
 * drm_ras_nl_list_nodes_dumpit() - Dump all registered RAS nodes
 * @skb: Netlink message buffer
 * @cb: Callback context for multi-part dumps
 *
 * Iterates over all registered RAS nodes in the global xarray and appends
 * their attributes (ID, name, type) to the given netlink message buffer.
 * Uses @cb->ctx to track progress in case the message buffer fills up, allowing
 * multi-part dump support. On buffer overflow, updates the context to resume
 * from the last node on the next invocation.
 *
 * Return: 0 if all nodes fit in @skb, number of bytes added to @skb if
 *          the buffer filled up (requires multi-part continuation), or
 *          a negative error code on failure.
 */
int drm_ras_nl_list_nodes_dumpit(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	const struct genl_info *info = genl_info_dump(cb);
	struct drm_ras_ctx *ctx = (void *)cb->ctx;
	struct drm_ras_node *node;
	struct nlattr *hdr;
	unsigned long id;
	int ret;

	xa_for_each_start(&drm_ras_xa, id, node, ctx->restart) {
		hdr = genlmsg_iput(skb, info);
		if (!hdr) {
			ret = -EMSGSIZE;
			break;
		}

		ret = nla_put_u32(skb, DRM_RAS_A_NODE_ATTRS_NODE_ID, node->id);
		if (ret) {
			genlmsg_cancel(skb, hdr);
			break;
		}

		ret = nla_put_string(skb, DRM_RAS_A_NODE_ATTRS_DEVICE_NAME,
				     node->device_name);
		if (ret) {
			genlmsg_cancel(skb, hdr);
			break;
		}

		ret = nla_put_string(skb, DRM_RAS_A_NODE_ATTRS_NODE_NAME,
				     node->node_name);
		if (ret) {
			genlmsg_cancel(skb, hdr);
			break;
		}

		ret = nla_put_u32(skb, DRM_RAS_A_NODE_ATTRS_NODE_TYPE,
				  node->type);
		if (ret) {
			genlmsg_cancel(skb, hdr);
			break;
		}

		genlmsg_end(skb, hdr);
	}

	if (ret == -EMSGSIZE)
		ctx->restart = id;

	return ret;
}

static int get_node_error_counter(u32 node_id, u32 error_id,
				  const char **name, u32 *value)
{
	struct drm_ras_node *node;

	node = xa_load(&drm_ras_xa, node_id);
	if (!node || !node->query_error_counter)
		return -ENOENT;

	if (error_id < node->error_counter_range.first ||
	    error_id > node->error_counter_range.last)
		return -EINVAL;

	return node->query_error_counter(node, error_id, name, value);
}

static int msg_reply_value(struct sk_buff *msg, u32 error_id,
			   const char *error_name, u32 value)
{
	int ret;

	ret = nla_put_u32(msg, DRM_RAS_A_ERROR_COUNTER_ATTRS_ERROR_ID, error_id);
	if (ret)
		return ret;

	ret = nla_put_string(msg, DRM_RAS_A_ERROR_COUNTER_ATTRS_ERROR_NAME,
			     error_name);
	if (ret)
		return ret;

	return nla_put_u32(msg, DRM_RAS_A_ERROR_COUNTER_ATTRS_ERROR_VALUE,
			   value);
}

static int doit_reply_value(struct genl_info *info, u32 node_id,
			    u32 error_id)
{
	struct sk_buff *msg;
	struct nlattr *hdr;
	const char *error_name;
	u32 value;
	int ret;

	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_iput(msg, info);
	if (!hdr) {
		nlmsg_free(msg);
		return -EMSGSIZE;
	}

	ret = get_node_error_counter(node_id, error_id,
				     &error_name, &value);
	if (ret)
		return ret;

	ret = msg_reply_value(msg, error_id, error_name, value);
	if (ret) {
		genlmsg_cancel(msg, hdr);
		nlmsg_free(msg);
		return ret;
	}

	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);
}

/**
 * drm_ras_nl_get_error_counter_dumpit() - Dump all Error Counters
 * @skb: Netlink message buffer
 * @cb: Callback context for multi-part dumps
 *
 * Iterates over all error counters in a given Node and appends
 * their attributes (ID, name, value) to the given netlink message buffer.
 * Uses @cb->ctx to track progress in case the message buffer fills up, allowing
 * multi-part dump support. On buffer overflow, updates the context to resume
 * from the last node on the next invocation.
 *
 * Return: 0 if all errors fit in @skb, number of bytes added to @skb if
 *          the buffer filled up (requires multi-part continuation), or
 *          a negative error code on failure.
 */
int drm_ras_nl_get_error_counter_dumpit(struct sk_buff *skb,
					struct netlink_callback *cb)
{
	const struct genl_info *info = genl_info_dump(cb);
	struct drm_ras_ctx *ctx = (void *)cb->ctx;
	struct drm_ras_node *node;
	struct nlattr *hdr;
	const char *error_name;
	u32 node_id, error_id, value;
	int ret;

	if (!info->attrs || GENL_REQ_ATTR_CHECK(info, DRM_RAS_A_ERROR_COUNTER_ATTRS_NODE_ID))
		return -EINVAL;

	node_id = nla_get_u32(info->attrs[DRM_RAS_A_ERROR_COUNTER_ATTRS_NODE_ID]);

	node = xa_load(&drm_ras_xa, node_id);
	if (!node)
		return -ENOENT;

	for (error_id = max(node->error_counter_range.first, ctx->restart);
	     error_id <= node->error_counter_range.last;
	     error_id++) {
		ret = get_node_error_counter(node_id, error_id,
					     &error_name, &value);
		/*
		 * For non-contiguous range, driver return -ENOENT as indication
		 * to skip this ID when listing all errors.
		 */
		if (ret == -ENOENT)
			continue;
		if (ret)
			return ret;

		hdr = genlmsg_iput(skb, info);

		if (!hdr) {
			ret = -EMSGSIZE;
			break;
		}

		ret = msg_reply_value(skb, error_id, error_name, value);
		if (ret) {
			genlmsg_cancel(skb, hdr);
			break;
		}

		genlmsg_end(skb, hdr);
	}

	if (ret == -EMSGSIZE)
		ctx->restart = error_id;

	return ret;
}

/**
 * drm_ras_nl_get_error_counter_doit() - Query an error counter of an node
 * @skb: Netlink message buffer
 * @info: Generic Netlink info containing attributes of the request
 *
 * Extracts the node ID and error ID from the netlink attributes and
 * retrieves the current value of the corresponding error counter. Sends the
 * result back to the requesting user via the standard Genl reply.
 *
 * Return: 0 on success, or negative errno on failure.
 */
int drm_ras_nl_get_error_counter_doit(struct sk_buff *skb,
				      struct genl_info *info)
{
	u32 node_id, error_id;

	if (!info->attrs ||
	    GENL_REQ_ATTR_CHECK(info, DRM_RAS_A_ERROR_COUNTER_ATTRS_NODE_ID) ||
	    GENL_REQ_ATTR_CHECK(info, DRM_RAS_A_ERROR_COUNTER_ATTRS_ERROR_ID))
		return -EINVAL;

	node_id = nla_get_u32(info->attrs[DRM_RAS_A_ERROR_COUNTER_ATTRS_NODE_ID]);
	error_id = nla_get_u32(info->attrs[DRM_RAS_A_ERROR_COUNTER_ATTRS_ERROR_ID]);

	return doit_reply_value(info, node_id, error_id);
}

/**
 * drm_ras_node_register() - Register a new RAS node
 * @node: Node structure to register
 *
 * Adds the given RAS node to the global node xarray and assigns it
 * a unique ID. Both @node->name and @node->type must be valid.
 *
 * Return: 0 on success, or negative errno on failure:
 */
int drm_ras_node_register(struct drm_ras_node *node)
{
	if (!node->device_name || !node->node_name)
		return -EINVAL;

	/* Currently, only Error Counter Endpoints are supported */
	if (node->type != DRM_RAS_NODE_TYPE_ERROR_COUNTER)
		return -EINVAL;

	/* Mandatory entries for Error Counter Node */
	if (node->type == DRM_RAS_NODE_TYPE_ERROR_COUNTER &&
	    (!node->error_counter_range.last || !node->query_error_counter))
		return -EINVAL;

	return xa_alloc(&drm_ras_xa, &node->id, node, xa_limit_32b, GFP_KERNEL);
}
EXPORT_SYMBOL(drm_ras_node_register);

/**
 * drm_ras_node_unregister() - Unregister a previously registered node
 * @node: Node structure to unregister
 *
 * Removes the given node from the global node xarray using its ID.
 */
void drm_ras_node_unregister(struct drm_ras_node *node)
{
	xa_erase(&drm_ras_xa, node->id);
}
EXPORT_SYMBOL(drm_ras_node_unregister);
