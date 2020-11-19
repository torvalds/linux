/*
 * Thunderbolt XDomain discovery protocol support
 *
 * Copyright (C) 2017, Intel Corporation
 * Authors: Michael Jamet <michael.jamet@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/utsname.h>
#include <linux/uuid.h>
#include <linux/workqueue.h>

#include "tb.h"

#define XDOMAIN_DEFAULT_TIMEOUT			5000 /* ms */
#define XDOMAIN_PROPERTIES_RETRIES		60
#define XDOMAIN_PROPERTIES_CHANGED_RETRIES	10

struct xdomain_request_work {
	struct work_struct work;
	struct tb_xdp_header *pkg;
	struct tb *tb;
};

/* Serializes access to the properties and protocol handlers below */
static DEFINE_MUTEX(xdomain_lock);

/* Properties exposed to the remote domains */
static struct tb_property_dir *xdomain_property_dir;
static u32 *xdomain_property_block;
static u32 xdomain_property_block_len;
static u32 xdomain_property_block_gen;

/* Additional protocol handlers */
static LIST_HEAD(protocol_handlers);

/* UUID for XDomain discovery protocol: b638d70e-42ff-40bb-97c2-90e2c0b2ff07 */
static const uuid_t tb_xdp_uuid =
	UUID_INIT(0xb638d70e, 0x42ff, 0x40bb,
		  0x97, 0xc2, 0x90, 0xe2, 0xc0, 0xb2, 0xff, 0x07);

static bool tb_xdomain_match(const struct tb_cfg_request *req,
			     const struct ctl_pkg *pkg)
{
	switch (pkg->frame.eof) {
	case TB_CFG_PKG_ERROR:
		return true;

	case TB_CFG_PKG_XDOMAIN_RESP: {
		const struct tb_xdp_header *res_hdr = pkg->buffer;
		const struct tb_xdp_header *req_hdr = req->request;

		if (pkg->frame.size < req->response_size / 4)
			return false;

		/* Make sure route matches */
		if ((res_hdr->xd_hdr.route_hi & ~BIT(31)) !=
		     req_hdr->xd_hdr.route_hi)
			return false;
		if ((res_hdr->xd_hdr.route_lo) != req_hdr->xd_hdr.route_lo)
			return false;

		/* Check that the XDomain protocol matches */
		if (!uuid_equal(&res_hdr->uuid, &req_hdr->uuid))
			return false;

		return true;
	}

	default:
		return false;
	}
}

static bool tb_xdomain_copy(struct tb_cfg_request *req,
			    const struct ctl_pkg *pkg)
{
	memcpy(req->response, pkg->buffer, req->response_size);
	req->result.err = 0;
	return true;
}

static void response_ready(void *data)
{
	tb_cfg_request_put(data);
}

static int __tb_xdomain_response(struct tb_ctl *ctl, const void *response,
				 size_t size, enum tb_cfg_pkg_type type)
{
	struct tb_cfg_request *req;

	req = tb_cfg_request_alloc();
	if (!req)
		return -ENOMEM;

	req->match = tb_xdomain_match;
	req->copy = tb_xdomain_copy;
	req->request = response;
	req->request_size = size;
	req->request_type = type;

	return tb_cfg_request(ctl, req, response_ready, req);
}

/**
 * tb_xdomain_response() - Send a XDomain response message
 * @xd: XDomain to send the message
 * @response: Response to send
 * @size: Size of the response
 * @type: PDF type of the response
 *
 * This can be used to send a XDomain response message to the other
 * domain. No response for the message is expected.
 *
 * Return: %0 in case of success and negative errno in case of failure
 */
int tb_xdomain_response(struct tb_xdomain *xd, const void *response,
			size_t size, enum tb_cfg_pkg_type type)
{
	return __tb_xdomain_response(xd->tb->ctl, response, size, type);
}
EXPORT_SYMBOL_GPL(tb_xdomain_response);

static int __tb_xdomain_request(struct tb_ctl *ctl, const void *request,
	size_t request_size, enum tb_cfg_pkg_type request_type, void *response,
	size_t response_size, enum tb_cfg_pkg_type response_type,
	unsigned int timeout_msec)
{
	struct tb_cfg_request *req;
	struct tb_cfg_result res;

	req = tb_cfg_request_alloc();
	if (!req)
		return -ENOMEM;

	req->match = tb_xdomain_match;
	req->copy = tb_xdomain_copy;
	req->request = request;
	req->request_size = request_size;
	req->request_type = request_type;
	req->response = response;
	req->response_size = response_size;
	req->response_type = response_type;

	res = tb_cfg_request_sync(ctl, req, timeout_msec);

	tb_cfg_request_put(req);

	return res.err == 1 ? -EIO : res.err;
}

/**
 * tb_xdomain_request() - Send a XDomain request
 * @xd: XDomain to send the request
 * @request: Request to send
 * @request_size: Size of the request in bytes
 * @request_type: PDF type of the request
 * @response: Response is copied here
 * @response_size: Expected size of the response in bytes
 * @response_type: Expected PDF type of the response
 * @timeout_msec: Timeout in milliseconds to wait for the response
 *
 * This function can be used to send XDomain control channel messages to
 * the other domain. The function waits until the response is received
 * or when timeout triggers. Whichever comes first.
 *
 * Return: %0 in case of success and negative errno in case of failure
 */
int tb_xdomain_request(struct tb_xdomain *xd, const void *request,
	size_t request_size, enum tb_cfg_pkg_type request_type,
	void *response, size_t response_size,
	enum tb_cfg_pkg_type response_type, unsigned int timeout_msec)
{
	return __tb_xdomain_request(xd->tb->ctl, request, request_size,
				    request_type, response, response_size,
				    response_type, timeout_msec);
}
EXPORT_SYMBOL_GPL(tb_xdomain_request);

static inline void tb_xdp_fill_header(struct tb_xdp_header *hdr, u64 route,
	u8 sequence, enum tb_xdp_type type, size_t size)
{
	u32 length_sn;

	length_sn = (size - sizeof(hdr->xd_hdr)) / 4;
	length_sn |= (sequence << TB_XDOMAIN_SN_SHIFT) & TB_XDOMAIN_SN_MASK;

	hdr->xd_hdr.route_hi = upper_32_bits(route);
	hdr->xd_hdr.route_lo = lower_32_bits(route);
	hdr->xd_hdr.length_sn = length_sn;
	hdr->type = type;
	memcpy(&hdr->uuid, &tb_xdp_uuid, sizeof(tb_xdp_uuid));
}

static int tb_xdp_handle_error(const struct tb_xdp_header *hdr)
{
	const struct tb_xdp_error_response *error;

	if (hdr->type != ERROR_RESPONSE)
		return 0;

	error = (const struct tb_xdp_error_response *)hdr;

	switch (error->error) {
	case ERROR_UNKNOWN_PACKET:
	case ERROR_UNKNOWN_DOMAIN:
		return -EIO;
	case ERROR_NOT_SUPPORTED:
		return -ENOTSUPP;
	case ERROR_NOT_READY:
		return -EAGAIN;
	default:
		break;
	}

	return 0;
}

static int tb_xdp_error_response(struct tb_ctl *ctl, u64 route, u8 sequence,
				 enum tb_xdp_error error)
{
	struct tb_xdp_error_response res;

	memset(&res, 0, sizeof(res));
	tb_xdp_fill_header(&res.hdr, route, sequence, ERROR_RESPONSE,
			   sizeof(res));
	res.error = error;

	return __tb_xdomain_response(ctl, &res, sizeof(res),
				     TB_CFG_PKG_XDOMAIN_RESP);
}

static int tb_xdp_properties_request(struct tb_ctl *ctl, u64 route,
	const uuid_t *src_uuid, const uuid_t *dst_uuid, int retry,
	u32 **block, u32 *generation)
{
	struct tb_xdp_properties_response *res;
	struct tb_xdp_properties req;
	u16 data_len, len;
	size_t total_size;
	u32 *data = NULL;
	int ret;

	total_size = sizeof(*res) + TB_XDP_PROPERTIES_MAX_DATA_LENGTH * 4;
	res = kzalloc(total_size, GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	memset(&req, 0, sizeof(req));
	tb_xdp_fill_header(&req.hdr, route, retry % 4, PROPERTIES_REQUEST,
			   sizeof(req));
	memcpy(&req.src_uuid, src_uuid, sizeof(*src_uuid));
	memcpy(&req.dst_uuid, dst_uuid, sizeof(*dst_uuid));

	len = 0;
	data_len = 0;

	do {
		ret = __tb_xdomain_request(ctl, &req, sizeof(req),
					   TB_CFG_PKG_XDOMAIN_REQ, res,
					   total_size, TB_CFG_PKG_XDOMAIN_RESP,
					   XDOMAIN_DEFAULT_TIMEOUT);
		if (ret)
			goto err;

		ret = tb_xdp_handle_error(&res->hdr);
		if (ret)
			goto err;

		/*
		 * Package length includes the whole payload without the
		 * XDomain header. Validate first that the package is at
		 * least size of the response structure.
		 */
		len = res->hdr.xd_hdr.length_sn & TB_XDOMAIN_LENGTH_MASK;
		if (len < sizeof(*res) / 4) {
			ret = -EINVAL;
			goto err;
		}

		len += sizeof(res->hdr.xd_hdr) / 4;
		len -= sizeof(*res) / 4;

		if (res->offset != req.offset) {
			ret = -EINVAL;
			goto err;
		}

		/*
		 * First time allocate block that has enough space for
		 * the whole properties block.
		 */
		if (!data) {
			data_len = res->data_length;
			if (data_len > TB_XDP_PROPERTIES_MAX_LENGTH) {
				ret = -E2BIG;
				goto err;
			}

			data = kcalloc(data_len, sizeof(u32), GFP_KERNEL);
			if (!data) {
				ret = -ENOMEM;
				goto err;
			}
		}

		memcpy(data + req.offset, res->data, len * 4);
		req.offset += len;
	} while (!data_len || req.offset < data_len);

	*block = data;
	*generation = res->generation;

	kfree(res);

	return data_len;

err:
	kfree(data);
	kfree(res);

	return ret;
}

static int tb_xdp_properties_response(struct tb *tb, struct tb_ctl *ctl,
	u64 route, u8 sequence, const uuid_t *src_uuid,
	const struct tb_xdp_properties *req)
{
	struct tb_xdp_properties_response *res;
	size_t total_size;
	u16 len;
	int ret;

	/*
	 * Currently we expect all requests to be directed to us. The
	 * protocol supports forwarding, though which we might add
	 * support later on.
	 */
	if (!uuid_equal(src_uuid, &req->dst_uuid)) {
		tb_xdp_error_response(ctl, route, sequence,
				      ERROR_UNKNOWN_DOMAIN);
		return 0;
	}

	mutex_lock(&xdomain_lock);

	if (req->offset >= xdomain_property_block_len) {
		mutex_unlock(&xdomain_lock);
		return -EINVAL;
	}

	len = xdomain_property_block_len - req->offset;
	len = min_t(u16, len, TB_XDP_PROPERTIES_MAX_DATA_LENGTH);
	total_size = sizeof(*res) + len * 4;

	res = kzalloc(total_size, GFP_KERNEL);
	if (!res) {
		mutex_unlock(&xdomain_lock);
		return -ENOMEM;
	}

	tb_xdp_fill_header(&res->hdr, route, sequence, PROPERTIES_RESPONSE,
			   total_size);
	res->generation = xdomain_property_block_gen;
	res->data_length = xdomain_property_block_len;
	res->offset = req->offset;
	uuid_copy(&res->src_uuid, src_uuid);
	uuid_copy(&res->dst_uuid, &req->src_uuid);
	memcpy(res->data, &xdomain_property_block[req->offset], len * 4);

	mutex_unlock(&xdomain_lock);

	ret = __tb_xdomain_response(ctl, res, total_size,
				    TB_CFG_PKG_XDOMAIN_RESP);

	kfree(res);
	return ret;
}

static int tb_xdp_properties_changed_request(struct tb_ctl *ctl, u64 route,
					     int retry, const uuid_t *uuid)
{
	struct tb_xdp_properties_changed_response res;
	struct tb_xdp_properties_changed req;
	int ret;

	memset(&req, 0, sizeof(req));
	tb_xdp_fill_header(&req.hdr, route, retry % 4,
			   PROPERTIES_CHANGED_REQUEST, sizeof(req));
	uuid_copy(&req.src_uuid, uuid);

	memset(&res, 0, sizeof(res));
	ret = __tb_xdomain_request(ctl, &req, sizeof(req),
				   TB_CFG_PKG_XDOMAIN_REQ, &res, sizeof(res),
				   TB_CFG_PKG_XDOMAIN_RESP,
				   XDOMAIN_DEFAULT_TIMEOUT);
	if (ret)
		return ret;

	return tb_xdp_handle_error(&res.hdr);
}

static int
tb_xdp_properties_changed_response(struct tb_ctl *ctl, u64 route, u8 sequence)
{
	struct tb_xdp_properties_changed_response res;

	memset(&res, 0, sizeof(res));
	tb_xdp_fill_header(&res.hdr, route, sequence,
			   PROPERTIES_CHANGED_RESPONSE, sizeof(res));
	return __tb_xdomain_response(ctl, &res, sizeof(res),
				     TB_CFG_PKG_XDOMAIN_RESP);
}

/**
 * tb_register_protocol_handler() - Register protocol handler
 * @handler: Handler to register
 *
 * This allows XDomain service drivers to hook into incoming XDomain
 * messages. After this function is called the service driver needs to
 * be able to handle calls to callback whenever a package with the
 * registered protocol is received.
 */
int tb_register_protocol_handler(struct tb_protocol_handler *handler)
{
	if (!handler->uuid || !handler->callback)
		return -EINVAL;
	if (uuid_equal(handler->uuid, &tb_xdp_uuid))
		return -EINVAL;

	mutex_lock(&xdomain_lock);
	list_add_tail(&handler->list, &protocol_handlers);
	mutex_unlock(&xdomain_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(tb_register_protocol_handler);

/**
 * tb_unregister_protocol_handler() - Unregister protocol handler
 * @handler: Handler to unregister
 *
 * Removes the previously registered protocol handler.
 */
void tb_unregister_protocol_handler(struct tb_protocol_handler *handler)
{
	mutex_lock(&xdomain_lock);
	list_del_init(&handler->list);
	mutex_unlock(&xdomain_lock);
}
EXPORT_SYMBOL_GPL(tb_unregister_protocol_handler);

static void tb_xdp_handle_request(struct work_struct *work)
{
	struct xdomain_request_work *xw = container_of(work, typeof(*xw), work);
	const struct tb_xdp_header *pkg = xw->pkg;
	const struct tb_xdomain_header *xhdr = &pkg->xd_hdr;
	struct tb *tb = xw->tb;
	struct tb_ctl *ctl = tb->ctl;
	const uuid_t *uuid;
	int ret = 0;
	u32 sequence;
	u64 route;

	route = ((u64)xhdr->route_hi << 32 | xhdr->route_lo) & ~BIT_ULL(63);
	sequence = xhdr->length_sn & TB_XDOMAIN_SN_MASK;
	sequence >>= TB_XDOMAIN_SN_SHIFT;

	mutex_lock(&tb->lock);
	if (tb->root_switch)
		uuid = tb->root_switch->uuid;
	else
		uuid = NULL;
	mutex_unlock(&tb->lock);

	if (!uuid) {
		tb_xdp_error_response(ctl, route, sequence, ERROR_NOT_READY);
		goto out;
	}

	switch (pkg->type) {
	case PROPERTIES_REQUEST:
		ret = tb_xdp_properties_response(tb, ctl, route, sequence, uuid,
			(const struct tb_xdp_properties *)pkg);
		break;

	case PROPERTIES_CHANGED_REQUEST: {
		const struct tb_xdp_properties_changed *xchg =
			(const struct tb_xdp_properties_changed *)pkg;
		struct tb_xdomain *xd;

		ret = tb_xdp_properties_changed_response(ctl, route, sequence);

		/*
		 * Since the properties have been changed, let's update
		 * the xdomain related to this connection as well in
		 * case there is a change in services it offers.
		 */
		xd = tb_xdomain_find_by_uuid_locked(tb, &xchg->src_uuid);
		if (xd) {
			queue_delayed_work(tb->wq, &xd->get_properties_work,
					   msecs_to_jiffies(50));
			tb_xdomain_put(xd);
		}

		break;
	}

	default:
		break;
	}

	if (ret) {
		tb_warn(tb, "failed to send XDomain response for %#x\n",
			pkg->type);
	}

out:
	kfree(xw->pkg);
	kfree(xw);
}

static void
tb_xdp_schedule_request(struct tb *tb, const struct tb_xdp_header *hdr,
			size_t size)
{
	struct xdomain_request_work *xw;

	xw = kmalloc(sizeof(*xw), GFP_KERNEL);
	if (!xw)
		return;

	INIT_WORK(&xw->work, tb_xdp_handle_request);
	xw->pkg = kmemdup(hdr, size, GFP_KERNEL);
	xw->tb = tb;

	queue_work(tb->wq, &xw->work);
}

/**
 * tb_register_service_driver() - Register XDomain service driver
 * @drv: Driver to register
 *
 * Registers new service driver from @drv to the bus.
 */
int tb_register_service_driver(struct tb_service_driver *drv)
{
	drv->driver.bus = &tb_bus_type;
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(tb_register_service_driver);

/**
 * tb_unregister_service_driver() - Unregister XDomain service driver
 * @xdrv: Driver to unregister
 *
 * Unregisters XDomain service driver from the bus.
 */
void tb_unregister_service_driver(struct tb_service_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(tb_unregister_service_driver);

static ssize_t key_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct tb_service *svc = container_of(dev, struct tb_service, dev);

	/*
	 * It should be null terminated but anything else is pretty much
	 * allowed.
	 */
	return sprintf(buf, "%*pEp\n", (int)strlen(svc->key), svc->key);
}
static DEVICE_ATTR_RO(key);

static int get_modalias(struct tb_service *svc, char *buf, size_t size)
{
	return snprintf(buf, size, "tbsvc:k%sp%08Xv%08Xr%08X", svc->key,
			svc->prtcid, svc->prtcvers, svc->prtcrevs);
}

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct tb_service *svc = container_of(dev, struct tb_service, dev);

	/* Full buffer size except new line and null termination */
	get_modalias(svc, buf, PAGE_SIZE - 2);
	return sprintf(buf, "%s\n", buf);
}
static DEVICE_ATTR_RO(modalias);

static ssize_t prtcid_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct tb_service *svc = container_of(dev, struct tb_service, dev);

	return sprintf(buf, "%u\n", svc->prtcid);
}
static DEVICE_ATTR_RO(prtcid);

static ssize_t prtcvers_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct tb_service *svc = container_of(dev, struct tb_service, dev);

	return sprintf(buf, "%u\n", svc->prtcvers);
}
static DEVICE_ATTR_RO(prtcvers);

static ssize_t prtcrevs_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct tb_service *svc = container_of(dev, struct tb_service, dev);

	return sprintf(buf, "%u\n", svc->prtcrevs);
}
static DEVICE_ATTR_RO(prtcrevs);

static ssize_t prtcstns_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct tb_service *svc = container_of(dev, struct tb_service, dev);

	return sprintf(buf, "0x%08x\n", svc->prtcstns);
}
static DEVICE_ATTR_RO(prtcstns);

static struct attribute *tb_service_attrs[] = {
	&dev_attr_key.attr,
	&dev_attr_modalias.attr,
	&dev_attr_prtcid.attr,
	&dev_attr_prtcvers.attr,
	&dev_attr_prtcrevs.attr,
	&dev_attr_prtcstns.attr,
	NULL,
};

static struct attribute_group tb_service_attr_group = {
	.attrs = tb_service_attrs,
};

static const struct attribute_group *tb_service_attr_groups[] = {
	&tb_service_attr_group,
	NULL,
};

static int tb_service_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct tb_service *svc = container_of(dev, struct tb_service, dev);
	char modalias[64];

	get_modalias(svc, modalias, sizeof(modalias));
	return add_uevent_var(env, "MODALIAS=%s", modalias);
}

static void tb_service_release(struct device *dev)
{
	struct tb_service *svc = container_of(dev, struct tb_service, dev);
	struct tb_xdomain *xd = tb_service_parent(svc);

	ida_simple_remove(&xd->service_ids, svc->id);
	kfree(svc->key);
	kfree(svc);
}

struct device_type tb_service_type = {
	.name = "thunderbolt_service",
	.groups = tb_service_attr_groups,
	.uevent = tb_service_uevent,
	.release = tb_service_release,
};
EXPORT_SYMBOL_GPL(tb_service_type);

static int remove_missing_service(struct device *dev, void *data)
{
	struct tb_xdomain *xd = data;
	struct tb_service *svc;

	svc = tb_to_service(dev);
	if (!svc)
		return 0;

	if (!tb_property_find(xd->properties, svc->key,
			      TB_PROPERTY_TYPE_DIRECTORY))
		device_unregister(dev);

	return 0;
}

static int find_service(struct device *dev, void *data)
{
	const struct tb_property *p = data;
	struct tb_service *svc;

	svc = tb_to_service(dev);
	if (!svc)
		return 0;

	return !strcmp(svc->key, p->key);
}

static int populate_service(struct tb_service *svc,
			    struct tb_property *property)
{
	struct tb_property_dir *dir = property->value.dir;
	struct tb_property *p;

	/* Fill in standard properties */
	p = tb_property_find(dir, "prtcid", TB_PROPERTY_TYPE_VALUE);
	if (p)
		svc->prtcid = p->value.immediate;
	p = tb_property_find(dir, "prtcvers", TB_PROPERTY_TYPE_VALUE);
	if (p)
		svc->prtcvers = p->value.immediate;
	p = tb_property_find(dir, "prtcrevs", TB_PROPERTY_TYPE_VALUE);
	if (p)
		svc->prtcrevs = p->value.immediate;
	p = tb_property_find(dir, "prtcstns", TB_PROPERTY_TYPE_VALUE);
	if (p)
		svc->prtcstns = p->value.immediate;

	svc->key = kstrdup(property->key, GFP_KERNEL);
	if (!svc->key)
		return -ENOMEM;

	return 0;
}

static void enumerate_services(struct tb_xdomain *xd)
{
	struct tb_service *svc;
	struct tb_property *p;
	struct device *dev;
	int id;

	/*
	 * First remove all services that are not available anymore in
	 * the updated property block.
	 */
	device_for_each_child_reverse(&xd->dev, xd, remove_missing_service);

	/* Then re-enumerate properties creating new services as we go */
	tb_property_for_each(xd->properties, p) {
		if (p->type != TB_PROPERTY_TYPE_DIRECTORY)
			continue;

		/* If the service exists already we are fine */
		dev = device_find_child(&xd->dev, p, find_service);
		if (dev) {
			put_device(dev);
			continue;
		}

		svc = kzalloc(sizeof(*svc), GFP_KERNEL);
		if (!svc)
			break;

		if (populate_service(svc, p)) {
			kfree(svc);
			break;
		}

		id = ida_simple_get(&xd->service_ids, 0, 0, GFP_KERNEL);
		if (id < 0) {
			kfree(svc->key);
			kfree(svc);
			break;
		}
		svc->id = id;
		svc->dev.bus = &tb_bus_type;
		svc->dev.type = &tb_service_type;
		svc->dev.parent = &xd->dev;
		dev_set_name(&svc->dev, "%s.%d", dev_name(&xd->dev), svc->id);

		if (device_register(&svc->dev)) {
			put_device(&svc->dev);
			break;
		}
	}
}

static int populate_properties(struct tb_xdomain *xd,
			       struct tb_property_dir *dir)
{
	const struct tb_property *p;

	/* Required properties */
	p = tb_property_find(dir, "deviceid", TB_PROPERTY_TYPE_VALUE);
	if (!p)
		return -EINVAL;
	xd->device = p->value.immediate;

	p = tb_property_find(dir, "vendorid", TB_PROPERTY_TYPE_VALUE);
	if (!p)
		return -EINVAL;
	xd->vendor = p->value.immediate;

	kfree(xd->device_name);
	xd->device_name = NULL;
	kfree(xd->vendor_name);
	xd->vendor_name = NULL;

	/* Optional properties */
	p = tb_property_find(dir, "deviceid", TB_PROPERTY_TYPE_TEXT);
	if (p)
		xd->device_name = kstrdup(p->value.text, GFP_KERNEL);
	p = tb_property_find(dir, "vendorid", TB_PROPERTY_TYPE_TEXT);
	if (p)
		xd->vendor_name = kstrdup(p->value.text, GFP_KERNEL);

	return 0;
}

/* Called with @xd->lock held */
static void tb_xdomain_restore_paths(struct tb_xdomain *xd)
{
	if (!xd->resume)
		return;

	xd->resume = false;
	if (xd->transmit_path) {
		dev_dbg(&xd->dev, "re-establishing DMA path\n");
		tb_domain_approve_xdomain_paths(xd->tb, xd);
	}
}

static void tb_xdomain_get_properties(struct work_struct *work)
{
	struct tb_xdomain *xd = container_of(work, typeof(*xd),
					     get_properties_work.work);
	struct tb_property_dir *dir;
	struct tb *tb = xd->tb;
	bool update = false;
	u32 *block = NULL;
	u32 gen = 0;
	int ret;

	ret = tb_xdp_properties_request(tb->ctl, xd->route, xd->local_uuid,
					xd->remote_uuid, xd->properties_retries,
					&block, &gen);
	if (ret < 0) {
		if (xd->properties_retries-- > 0) {
			queue_delayed_work(xd->tb->wq, &xd->get_properties_work,
					   msecs_to_jiffies(1000));
		} else {
			/* Give up now */
			dev_err(&xd->dev,
				"failed read XDomain properties from %pUb\n",
				xd->remote_uuid);
		}
		return;
	}

	xd->properties_retries = XDOMAIN_PROPERTIES_RETRIES;

	mutex_lock(&xd->lock);

	/* Only accept newer generation properties */
	if (xd->properties && gen <= xd->property_block_gen) {
		/*
		 * On resume it is likely that the properties block is
		 * not changed (unless the other end added or removed
		 * services). However, we need to make sure the existing
		 * DMA paths are restored properly.
		 */
		tb_xdomain_restore_paths(xd);
		goto err_free_block;
	}

	dir = tb_property_parse_dir(block, ret);
	if (!dir) {
		dev_err(&xd->dev, "failed to parse XDomain properties\n");
		goto err_free_block;
	}

	ret = populate_properties(xd, dir);
	if (ret) {
		dev_err(&xd->dev, "missing XDomain properties in response\n");
		goto err_free_dir;
	}

	/* Release the existing one */
	if (xd->properties) {
		tb_property_free_dir(xd->properties);
		update = true;
	}

	xd->properties = dir;
	xd->property_block_gen = gen;

	tb_xdomain_restore_paths(xd);

	mutex_unlock(&xd->lock);

	kfree(block);

	/*
	 * Now the device should be ready enough so we can add it to the
	 * bus and let userspace know about it. If the device is already
	 * registered, we notify the userspace that it has changed.
	 */
	if (!update) {
		if (device_add(&xd->dev)) {
			dev_err(&xd->dev, "failed to add XDomain device\n");
			return;
		}
	} else {
		kobject_uevent(&xd->dev.kobj, KOBJ_CHANGE);
	}

	enumerate_services(xd);
	return;

err_free_dir:
	tb_property_free_dir(dir);
err_free_block:
	kfree(block);
	mutex_unlock(&xd->lock);
}

static void tb_xdomain_properties_changed(struct work_struct *work)
{
	struct tb_xdomain *xd = container_of(work, typeof(*xd),
					     properties_changed_work.work);
	int ret;

	ret = tb_xdp_properties_changed_request(xd->tb->ctl, xd->route,
				xd->properties_changed_retries, xd->local_uuid);
	if (ret) {
		if (xd->properties_changed_retries-- > 0)
			queue_delayed_work(xd->tb->wq,
					   &xd->properties_changed_work,
					   msecs_to_jiffies(1000));
		return;
	}

	xd->properties_changed_retries = XDOMAIN_PROPERTIES_CHANGED_RETRIES;
}

static ssize_t device_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct tb_xdomain *xd = container_of(dev, struct tb_xdomain, dev);

	return sprintf(buf, "%#x\n", xd->device);
}
static DEVICE_ATTR_RO(device);

static ssize_t
device_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct tb_xdomain *xd = container_of(dev, struct tb_xdomain, dev);
	int ret;

	if (mutex_lock_interruptible(&xd->lock))
		return -ERESTARTSYS;
	ret = sprintf(buf, "%s\n", xd->device_name ? xd->device_name : "");
	mutex_unlock(&xd->lock);

	return ret;
}
static DEVICE_ATTR_RO(device_name);

static ssize_t vendor_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct tb_xdomain *xd = container_of(dev, struct tb_xdomain, dev);

	return sprintf(buf, "%#x\n", xd->vendor);
}
static DEVICE_ATTR_RO(vendor);

static ssize_t
vendor_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct tb_xdomain *xd = container_of(dev, struct tb_xdomain, dev);
	int ret;

	if (mutex_lock_interruptible(&xd->lock))
		return -ERESTARTSYS;
	ret = sprintf(buf, "%s\n", xd->vendor_name ? xd->vendor_name : "");
	mutex_unlock(&xd->lock);

	return ret;
}
static DEVICE_ATTR_RO(vendor_name);

static ssize_t unique_id_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct tb_xdomain *xd = container_of(dev, struct tb_xdomain, dev);

	return sprintf(buf, "%pUb\n", xd->remote_uuid);
}
static DEVICE_ATTR_RO(unique_id);

static struct attribute *xdomain_attrs[] = {
	&dev_attr_device.attr,
	&dev_attr_device_name.attr,
	&dev_attr_unique_id.attr,
	&dev_attr_vendor.attr,
	&dev_attr_vendor_name.attr,
	NULL,
};

static struct attribute_group xdomain_attr_group = {
	.attrs = xdomain_attrs,
};

static const struct attribute_group *xdomain_attr_groups[] = {
	&xdomain_attr_group,
	NULL,
};

static void tb_xdomain_release(struct device *dev)
{
	struct tb_xdomain *xd = container_of(dev, struct tb_xdomain, dev);

	put_device(xd->dev.parent);

	tb_property_free_dir(xd->properties);
	ida_destroy(&xd->service_ids);

	kfree(xd->local_uuid);
	kfree(xd->remote_uuid);
	kfree(xd->device_name);
	kfree(xd->vendor_name);
	kfree(xd);
}

static void start_handshake(struct tb_xdomain *xd)
{
	xd->properties_retries = XDOMAIN_PROPERTIES_RETRIES;
	xd->properties_changed_retries = XDOMAIN_PROPERTIES_CHANGED_RETRIES;

	/* Start exchanging properties with the other host */
	queue_delayed_work(xd->tb->wq, &xd->properties_changed_work,
			   msecs_to_jiffies(100));
	queue_delayed_work(xd->tb->wq, &xd->get_properties_work,
			   msecs_to_jiffies(1000));
}

static void stop_handshake(struct tb_xdomain *xd)
{
	xd->properties_retries = 0;
	xd->properties_changed_retries = 0;

	cancel_delayed_work_sync(&xd->get_properties_work);
	cancel_delayed_work_sync(&xd->properties_changed_work);
}

static int __maybe_unused tb_xdomain_suspend(struct device *dev)
{
	stop_handshake(tb_to_xdomain(dev));
	return 0;
}

static int __maybe_unused tb_xdomain_resume(struct device *dev)
{
	struct tb_xdomain *xd = tb_to_xdomain(dev);

	/*
	 * Ask tb_xdomain_get_properties() restore any existing DMA
	 * paths after properties are re-read.
	 */
	xd->resume = true;
	start_handshake(xd);

	return 0;
}

static const struct dev_pm_ops tb_xdomain_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tb_xdomain_suspend, tb_xdomain_resume)
};

struct device_type tb_xdomain_type = {
	.name = "thunderbolt_xdomain",
	.release = tb_xdomain_release,
	.pm = &tb_xdomain_pm_ops,
};
EXPORT_SYMBOL_GPL(tb_xdomain_type);

/**
 * tb_xdomain_alloc() - Allocate new XDomain object
 * @tb: Domain where the XDomain belongs
 * @parent: Parent device (the switch through the connection to the
 *	    other domain is reached).
 * @route: Route string used to reach the other domain
 * @local_uuid: Our local domain UUID
 * @remote_uuid: UUID of the other domain
 *
 * Allocates new XDomain structure and returns pointer to that. The
 * object must be released by calling tb_xdomain_put().
 */
struct tb_xdomain *tb_xdomain_alloc(struct tb *tb, struct device *parent,
				    u64 route, const uuid_t *local_uuid,
				    const uuid_t *remote_uuid)
{
	struct tb_xdomain *xd;

	xd = kzalloc(sizeof(*xd), GFP_KERNEL);
	if (!xd)
		return NULL;

	xd->tb = tb;
	xd->route = route;
	ida_init(&xd->service_ids);
	mutex_init(&xd->lock);
	INIT_DELAYED_WORK(&xd->get_properties_work, tb_xdomain_get_properties);
	INIT_DELAYED_WORK(&xd->properties_changed_work,
			  tb_xdomain_properties_changed);

	xd->local_uuid = kmemdup(local_uuid, sizeof(uuid_t), GFP_KERNEL);
	if (!xd->local_uuid)
		goto err_free;

	xd->remote_uuid = kmemdup(remote_uuid, sizeof(uuid_t), GFP_KERNEL);
	if (!xd->remote_uuid)
		goto err_free_local_uuid;

	device_initialize(&xd->dev);
	xd->dev.parent = get_device(parent);
	xd->dev.bus = &tb_bus_type;
	xd->dev.type = &tb_xdomain_type;
	xd->dev.groups = xdomain_attr_groups;
	dev_set_name(&xd->dev, "%u-%llx", tb->index, route);

	/*
	 * This keeps the DMA powered on as long as we have active
	 * connection to another host.
	 */
	pm_runtime_set_active(&xd->dev);
	pm_runtime_get_noresume(&xd->dev);
	pm_runtime_enable(&xd->dev);

	return xd;

err_free_local_uuid:
	kfree(xd->local_uuid);
err_free:
	kfree(xd);

	return NULL;
}

/**
 * tb_xdomain_add() - Add XDomain to the bus
 * @xd: XDomain to add
 *
 * This function starts XDomain discovery protocol handshake and
 * eventually adds the XDomain to the bus. After calling this function
 * the caller needs to call tb_xdomain_remove() in order to remove and
 * release the object regardless whether the handshake succeeded or not.
 */
void tb_xdomain_add(struct tb_xdomain *xd)
{
	/* Start exchanging properties with the other host */
	start_handshake(xd);
}

static int unregister_service(struct device *dev, void *data)
{
	device_unregister(dev);
	return 0;
}

/**
 * tb_xdomain_remove() - Remove XDomain from the bus
 * @xd: XDomain to remove
 *
 * This will stop all ongoing configuration work and remove the XDomain
 * along with any services from the bus. When the last reference to @xd
 * is released the object will be released as well.
 */
void tb_xdomain_remove(struct tb_xdomain *xd)
{
	stop_handshake(xd);

	device_for_each_child_reverse(&xd->dev, xd, unregister_service);

	/*
	 * Undo runtime PM here explicitly because it is possible that
	 * the XDomain was never added to the bus and thus device_del()
	 * is not called for it (device_del() would handle this otherwise).
	 */
	pm_runtime_disable(&xd->dev);
	pm_runtime_put_noidle(&xd->dev);
	pm_runtime_set_suspended(&xd->dev);

	if (!device_is_registered(&xd->dev))
		put_device(&xd->dev);
	else
		device_unregister(&xd->dev);
}

/**
 * tb_xdomain_enable_paths() - Enable DMA paths for XDomain connection
 * @xd: XDomain connection
 * @transmit_path: HopID of the transmit path the other end is using to
 *		   send packets
 * @transmit_ring: DMA ring used to receive packets from the other end
 * @receive_path: HopID of the receive path the other end is using to
 *		  receive packets
 * @receive_ring: DMA ring used to send packets to the other end
 *
 * The function enables DMA paths accordingly so that after successful
 * return the caller can send and receive packets using high-speed DMA
 * path.
 *
 * Return: %0 in case of success and negative errno in case of error
 */
int tb_xdomain_enable_paths(struct tb_xdomain *xd, u16 transmit_path,
			    u16 transmit_ring, u16 receive_path,
			    u16 receive_ring)
{
	int ret;

	mutex_lock(&xd->lock);

	if (xd->transmit_path) {
		ret = xd->transmit_path == transmit_path ? 0 : -EBUSY;
		goto exit_unlock;
	}

	xd->transmit_path = transmit_path;
	xd->transmit_ring = transmit_ring;
	xd->receive_path = receive_path;
	xd->receive_ring = receive_ring;

	ret = tb_domain_approve_xdomain_paths(xd->tb, xd);

exit_unlock:
	mutex_unlock(&xd->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(tb_xdomain_enable_paths);

/**
 * tb_xdomain_disable_paths() - Disable DMA paths for XDomain connection
 * @xd: XDomain connection
 *
 * This does the opposite of tb_xdomain_enable_paths(). After call to
 * this the caller is not expected to use the rings anymore.
 *
 * Return: %0 in case of success and negative errno in case of error
 */
int tb_xdomain_disable_paths(struct tb_xdomain *xd)
{
	int ret = 0;

	mutex_lock(&xd->lock);
	if (xd->transmit_path) {
		xd->transmit_path = 0;
		xd->transmit_ring = 0;
		xd->receive_path = 0;
		xd->receive_ring = 0;

		ret = tb_domain_disconnect_xdomain_paths(xd->tb, xd);
	}
	mutex_unlock(&xd->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(tb_xdomain_disable_paths);

struct tb_xdomain_lookup {
	const uuid_t *uuid;
	u8 link;
	u8 depth;
	u64 route;
};

static struct tb_xdomain *switch_find_xdomain(struct tb_switch *sw,
	const struct tb_xdomain_lookup *lookup)
{
	int i;

	for (i = 1; i <= sw->config.max_port_number; i++) {
		struct tb_port *port = &sw->ports[i];
		struct tb_xdomain *xd;

		if (tb_is_upstream_port(port))
			continue;

		if (port->xdomain) {
			xd = port->xdomain;

			if (lookup->uuid) {
				if (uuid_equal(xd->remote_uuid, lookup->uuid))
					return xd;
			} else if (lookup->link &&
				   lookup->link == xd->link &&
				   lookup->depth == xd->depth) {
				return xd;
			} else if (lookup->route &&
				   lookup->route == xd->route) {
				return xd;
			}
		} else if (port->remote) {
			xd = switch_find_xdomain(port->remote->sw, lookup);
			if (xd)
				return xd;
		}
	}

	return NULL;
}

/**
 * tb_xdomain_find_by_uuid() - Find an XDomain by UUID
 * @tb: Domain where the XDomain belongs to
 * @uuid: UUID to look for
 *
 * Finds XDomain by walking through the Thunderbolt topology below @tb.
 * The returned XDomain will have its reference count increased so the
 * caller needs to call tb_xdomain_put() when it is done with the
 * object.
 *
 * This will find all XDomains including the ones that are not yet added
 * to the bus (handshake is still in progress).
 *
 * The caller needs to hold @tb->lock.
 */
struct tb_xdomain *tb_xdomain_find_by_uuid(struct tb *tb, const uuid_t *uuid)
{
	struct tb_xdomain_lookup lookup;
	struct tb_xdomain *xd;

	memset(&lookup, 0, sizeof(lookup));
	lookup.uuid = uuid;

	xd = switch_find_xdomain(tb->root_switch, &lookup);
	return tb_xdomain_get(xd);
}
EXPORT_SYMBOL_GPL(tb_xdomain_find_by_uuid);

/**
 * tb_xdomain_find_by_link_depth() - Find an XDomain by link and depth
 * @tb: Domain where the XDomain belongs to
 * @link: Root switch link number
 * @depth: Depth in the link
 *
 * Finds XDomain by walking through the Thunderbolt topology below @tb.
 * The returned XDomain will have its reference count increased so the
 * caller needs to call tb_xdomain_put() when it is done with the
 * object.
 *
 * This will find all XDomains including the ones that are not yet added
 * to the bus (handshake is still in progress).
 *
 * The caller needs to hold @tb->lock.
 */
struct tb_xdomain *tb_xdomain_find_by_link_depth(struct tb *tb, u8 link,
						 u8 depth)
{
	struct tb_xdomain_lookup lookup;
	struct tb_xdomain *xd;

	memset(&lookup, 0, sizeof(lookup));
	lookup.link = link;
	lookup.depth = depth;

	xd = switch_find_xdomain(tb->root_switch, &lookup);
	return tb_xdomain_get(xd);
}

/**
 * tb_xdomain_find_by_route() - Find an XDomain by route string
 * @tb: Domain where the XDomain belongs to
 * @route: XDomain route string
 *
 * Finds XDomain by walking through the Thunderbolt topology below @tb.
 * The returned XDomain will have its reference count increased so the
 * caller needs to call tb_xdomain_put() when it is done with the
 * object.
 *
 * This will find all XDomains including the ones that are not yet added
 * to the bus (handshake is still in progress).
 *
 * The caller needs to hold @tb->lock.
 */
struct tb_xdomain *tb_xdomain_find_by_route(struct tb *tb, u64 route)
{
	struct tb_xdomain_lookup lookup;
	struct tb_xdomain *xd;

	memset(&lookup, 0, sizeof(lookup));
	lookup.route = route;

	xd = switch_find_xdomain(tb->root_switch, &lookup);
	return tb_xdomain_get(xd);
}
EXPORT_SYMBOL_GPL(tb_xdomain_find_by_route);

bool tb_xdomain_handle_request(struct tb *tb, enum tb_cfg_pkg_type type,
			       const void *buf, size_t size)
{
	const struct tb_protocol_handler *handler, *tmp;
	const struct tb_xdp_header *hdr = buf;
	unsigned int length;
	int ret = 0;

	/* We expect the packet is at least size of the header */
	length = hdr->xd_hdr.length_sn & TB_XDOMAIN_LENGTH_MASK;
	if (length != size / 4 - sizeof(hdr->xd_hdr) / 4)
		return true;
	if (length < sizeof(*hdr) / 4 - sizeof(hdr->xd_hdr) / 4)
		return true;

	/*
	 * Handle XDomain discovery protocol packets directly here. For
	 * other protocols (based on their UUID) we call registered
	 * handlers in turn.
	 */
	if (uuid_equal(&hdr->uuid, &tb_xdp_uuid)) {
		if (type == TB_CFG_PKG_XDOMAIN_REQ) {
			tb_xdp_schedule_request(tb, hdr, size);
			return true;
		}
		return false;
	}

	mutex_lock(&xdomain_lock);
	list_for_each_entry_safe(handler, tmp, &protocol_handlers, list) {
		if (!uuid_equal(&hdr->uuid, handler->uuid))
			continue;

		mutex_unlock(&xdomain_lock);
		ret = handler->callback(buf, size, handler->data);
		mutex_lock(&xdomain_lock);

		if (ret)
			break;
	}
	mutex_unlock(&xdomain_lock);

	return ret > 0;
}

static int rebuild_property_block(void)
{
	u32 *block, len;
	int ret;

	ret = tb_property_format_dir(xdomain_property_dir, NULL, 0);
	if (ret < 0)
		return ret;

	len = ret;

	block = kcalloc(len, sizeof(u32), GFP_KERNEL);
	if (!block)
		return -ENOMEM;

	ret = tb_property_format_dir(xdomain_property_dir, block, len);
	if (ret) {
		kfree(block);
		return ret;
	}

	kfree(xdomain_property_block);
	xdomain_property_block = block;
	xdomain_property_block_len = len;
	xdomain_property_block_gen++;

	return 0;
}

static int update_xdomain(struct device *dev, void *data)
{
	struct tb_xdomain *xd;

	xd = tb_to_xdomain(dev);
	if (xd) {
		queue_delayed_work(xd->tb->wq, &xd->properties_changed_work,
				   msecs_to_jiffies(50));
	}

	return 0;
}

static void update_all_xdomains(void)
{
	bus_for_each_dev(&tb_bus_type, NULL, NULL, update_xdomain);
}

static bool remove_directory(const char *key, const struct tb_property_dir *dir)
{
	struct tb_property *p;

	p = tb_property_find(xdomain_property_dir, key,
			     TB_PROPERTY_TYPE_DIRECTORY);
	if (p && p->value.dir == dir) {
		tb_property_remove(p);
		return true;
	}
	return false;
}

/**
 * tb_register_property_dir() - Register property directory to the host
 * @key: Key (name) of the directory to add
 * @dir: Directory to add
 *
 * Service drivers can use this function to add new property directory
 * to the host available properties. The other connected hosts are
 * notified so they can re-read properties of this host if they are
 * interested.
 *
 * Return: %0 on success and negative errno on failure
 */
int tb_register_property_dir(const char *key, struct tb_property_dir *dir)
{
	int ret;

	if (WARN_ON(!xdomain_property_dir))
		return -EAGAIN;

	if (!key || strlen(key) > 8)
		return -EINVAL;

	mutex_lock(&xdomain_lock);
	if (tb_property_find(xdomain_property_dir, key,
			     TB_PROPERTY_TYPE_DIRECTORY)) {
		ret = -EEXIST;
		goto err_unlock;
	}

	ret = tb_property_add_dir(xdomain_property_dir, key, dir);
	if (ret)
		goto err_unlock;

	ret = rebuild_property_block();
	if (ret) {
		remove_directory(key, dir);
		goto err_unlock;
	}

	mutex_unlock(&xdomain_lock);
	update_all_xdomains();
	return 0;

err_unlock:
	mutex_unlock(&xdomain_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tb_register_property_dir);

/**
 * tb_unregister_property_dir() - Removes property directory from host
 * @key: Key (name) of the directory
 * @dir: Directory to remove
 *
 * This will remove the existing directory from this host and notify the
 * connected hosts about the change.
 */
void tb_unregister_property_dir(const char *key, struct tb_property_dir *dir)
{
	int ret = 0;

	mutex_lock(&xdomain_lock);
	if (remove_directory(key, dir))
		ret = rebuild_property_block();
	mutex_unlock(&xdomain_lock);

	if (!ret)
		update_all_xdomains();
}
EXPORT_SYMBOL_GPL(tb_unregister_property_dir);

int tb_xdomain_init(void)
{
	int ret;

	xdomain_property_dir = tb_property_create_dir(NULL);
	if (!xdomain_property_dir)
		return -ENOMEM;

	/*
	 * Initialize standard set of properties without any service
	 * directories. Those will be added by service drivers
	 * themselves when they are loaded.
	 */
	tb_property_add_immediate(xdomain_property_dir, "vendorid",
				  PCI_VENDOR_ID_INTEL);
	tb_property_add_text(xdomain_property_dir, "vendorid", "Intel Corp.");
	tb_property_add_immediate(xdomain_property_dir, "deviceid", 0x1);
	tb_property_add_text(xdomain_property_dir, "deviceid",
			     utsname()->nodename);
	tb_property_add_immediate(xdomain_property_dir, "devicerv", 0x80000100);

	ret = rebuild_property_block();
	if (ret) {
		tb_property_free_dir(xdomain_property_dir);
		xdomain_property_dir = NULL;
	}

	return ret;
}

void tb_xdomain_exit(void)
{
	kfree(xdomain_property_block);
	tb_property_free_dir(xdomain_property_dir);
}
