// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Base Protocol
 *
 * Copyright (C) 2018 ARM Ltd.
 */

#define pr_fmt(fmt) "SCMI Notifications BASE - " fmt

#include <linux/scmi_protocol.h>

#include "common.h"
#include "notify.h"

#define SCMI_BASE_NUM_SOURCES		1
#define SCMI_BASE_MAX_CMD_ERR_COUNT	1024

enum scmi_base_protocol_cmd {
	BASE_DISCOVER_VENDOR = 0x3,
	BASE_DISCOVER_SUB_VENDOR = 0x4,
	BASE_DISCOVER_IMPLEMENT_VERSION = 0x5,
	BASE_DISCOVER_LIST_PROTOCOLS = 0x6,
	BASE_DISCOVER_AGENT = 0x7,
	BASE_NOTIFY_ERRORS = 0x8,
	BASE_SET_DEVICE_PERMISSIONS = 0x9,
	BASE_SET_PROTOCOL_PERMISSIONS = 0xa,
	BASE_RESET_AGENT_CONFIGURATION = 0xb,
};

struct scmi_msg_resp_base_attributes {
	u8 num_protocols;
	u8 num_agents;
	__le16 reserved;
};

struct scmi_msg_base_error_notify {
	__le32 event_control;
#define BASE_TP_NOTIFY_ALL	BIT(0)
};

struct scmi_base_error_notify_payld {
	__le32 agent_id;
	__le32 error_status;
#define IS_FATAL_ERROR(x)	((x) & BIT(31))
#define ERROR_CMD_COUNT(x)	FIELD_GET(GENMASK(9, 0), (x))
	__le64 msg_reports[SCMI_BASE_MAX_CMD_ERR_COUNT];
};

/**
 * scmi_base_attributes_get() - gets the implementation details
 *	that are associated with the base protocol.
 *
 * @handle: SCMI entity handle
 *
 * Return: 0 on success, else appropriate SCMI error.
 */
static int scmi_base_attributes_get(const struct scmi_handle *handle)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_resp_base_attributes *attr_info;
	struct scmi_revision_info *rev = handle->version;

	ret = scmi_xfer_get_init(handle, PROTOCOL_ATTRIBUTES,
				 SCMI_PROTOCOL_BASE, 0, sizeof(*attr_info), &t);
	if (ret)
		return ret;

	ret = scmi_do_xfer(handle, t);
	if (!ret) {
		attr_info = t->rx.buf;
		rev->num_protocols = attr_info->num_protocols;
		rev->num_agents = attr_info->num_agents;
	}

	scmi_xfer_put(handle, t);

	return ret;
}

/**
 * scmi_base_vendor_id_get() - gets vendor/subvendor identifier ASCII string.
 *
 * @handle: SCMI entity handle
 * @sub_vendor: specify true if sub-vendor ID is needed
 *
 * Return: 0 on success, else appropriate SCMI error.
 */
static int
scmi_base_vendor_id_get(const struct scmi_handle *handle, bool sub_vendor)
{
	u8 cmd;
	int ret, size;
	char *vendor_id;
	struct scmi_xfer *t;
	struct scmi_revision_info *rev = handle->version;

	if (sub_vendor) {
		cmd = BASE_DISCOVER_SUB_VENDOR;
		vendor_id = rev->sub_vendor_id;
		size = ARRAY_SIZE(rev->sub_vendor_id);
	} else {
		cmd = BASE_DISCOVER_VENDOR;
		vendor_id = rev->vendor_id;
		size = ARRAY_SIZE(rev->vendor_id);
	}

	ret = scmi_xfer_get_init(handle, cmd, SCMI_PROTOCOL_BASE, 0, size, &t);
	if (ret)
		return ret;

	ret = scmi_do_xfer(handle, t);
	if (!ret)
		memcpy(vendor_id, t->rx.buf, size);

	scmi_xfer_put(handle, t);

	return ret;
}

/**
 * scmi_base_implementation_version_get() - gets a vendor-specific
 *	implementation 32-bit version. The format of the version number is
 *	vendor-specific
 *
 * @handle: SCMI entity handle
 *
 * Return: 0 on success, else appropriate SCMI error.
 */
static int
scmi_base_implementation_version_get(const struct scmi_handle *handle)
{
	int ret;
	__le32 *impl_ver;
	struct scmi_xfer *t;
	struct scmi_revision_info *rev = handle->version;

	ret = scmi_xfer_get_init(handle, BASE_DISCOVER_IMPLEMENT_VERSION,
				 SCMI_PROTOCOL_BASE, 0, sizeof(*impl_ver), &t);
	if (ret)
		return ret;

	ret = scmi_do_xfer(handle, t);
	if (!ret) {
		impl_ver = t->rx.buf;
		rev->impl_ver = le32_to_cpu(*impl_ver);
	}

	scmi_xfer_put(handle, t);

	return ret;
}

/**
 * scmi_base_implementation_list_get() - gets the list of protocols it is
 *	OSPM is allowed to access
 *
 * @handle: SCMI entity handle
 * @protocols_imp: pointer to hold the list of protocol identifiers
 *
 * Return: 0 on success, else appropriate SCMI error.
 */
static int scmi_base_implementation_list_get(const struct scmi_handle *handle,
					     u8 *protocols_imp)
{
	u8 *list;
	int ret, loop;
	struct scmi_xfer *t;
	__le32 *num_skip, *num_ret;
	u32 tot_num_ret = 0, loop_num_ret;
	struct device *dev = handle->dev;

	ret = scmi_xfer_get_init(handle, BASE_DISCOVER_LIST_PROTOCOLS,
				 SCMI_PROTOCOL_BASE, sizeof(*num_skip), 0, &t);
	if (ret)
		return ret;

	num_skip = t->tx.buf;
	num_ret = t->rx.buf;
	list = t->rx.buf + sizeof(*num_ret);

	do {
		/* Set the number of protocols to be skipped/already read */
		*num_skip = cpu_to_le32(tot_num_ret);

		ret = scmi_do_xfer(handle, t);
		if (ret)
			break;

		loop_num_ret = le32_to_cpu(*num_ret);
		if (loop_num_ret > MAX_PROTOCOLS_IMP - tot_num_ret) {
			dev_err(dev, "No. of Protocol > MAX_PROTOCOLS_IMP");
			break;
		}

		for (loop = 0; loop < loop_num_ret; loop++)
			protocols_imp[tot_num_ret + loop] = *(list + loop);

		tot_num_ret += loop_num_ret;

		scmi_reset_rx_to_maxsz(handle, t);
	} while (loop_num_ret);

	scmi_xfer_put(handle, t);

	return ret;
}

/**
 * scmi_base_discover_agent_get() - discover the name of an agent
 *
 * @handle: SCMI entity handle
 * @id: Agent identifier
 * @name: Agent identifier ASCII string
 *
 * An agent id of 0 is reserved to identify the platform itself.
 * Generally operating system is represented as "OSPM"
 *
 * Return: 0 on success, else appropriate SCMI error.
 */
static int scmi_base_discover_agent_get(const struct scmi_handle *handle,
					int id, char *name)
{
	int ret;
	struct scmi_xfer *t;

	ret = scmi_xfer_get_init(handle, BASE_DISCOVER_AGENT,
				 SCMI_PROTOCOL_BASE, sizeof(__le32),
				 SCMI_MAX_STR_SIZE, &t);
	if (ret)
		return ret;

	put_unaligned_le32(id, t->tx.buf);

	ret = scmi_do_xfer(handle, t);
	if (!ret)
		strlcpy(name, t->rx.buf, SCMI_MAX_STR_SIZE);

	scmi_xfer_put(handle, t);

	return ret;
}

static int scmi_base_error_notify(const struct scmi_handle *handle, bool enable)
{
	int ret;
	u32 evt_cntl = enable ? BASE_TP_NOTIFY_ALL : 0;
	struct scmi_xfer *t;
	struct scmi_msg_base_error_notify *cfg;

	ret = scmi_xfer_get_init(handle, BASE_NOTIFY_ERRORS,
				 SCMI_PROTOCOL_BASE, sizeof(*cfg), 0, &t);
	if (ret)
		return ret;

	cfg = t->tx.buf;
	cfg->event_control = cpu_to_le32(evt_cntl);

	ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_base_set_notify_enabled(const struct scmi_handle *handle,
					u8 evt_id, u32 src_id, bool enable)
{
	int ret;

	ret = scmi_base_error_notify(handle, enable);
	if (ret)
		pr_debug("FAIL_ENABLED - evt[%X] ret:%d\n", evt_id, ret);

	return ret;
}

static void *scmi_base_fill_custom_report(const struct scmi_handle *handle,
					  u8 evt_id, ktime_t timestamp,
					  const void *payld, size_t payld_sz,
					  void *report, u32 *src_id)
{
	int i;
	const struct scmi_base_error_notify_payld *p = payld;
	struct scmi_base_error_report *r = report;

	/*
	 * BaseError notification payload is variable in size but
	 * up to a maximum length determined by the struct ponted by p.
	 * Instead payld_sz is the effective length of this notification
	 * payload so cannot be greater of the maximum allowed size as
	 * pointed by p.
	 */
	if (evt_id != SCMI_EVENT_BASE_ERROR_EVENT || sizeof(*p) < payld_sz)
		return NULL;

	r->timestamp = timestamp;
	r->agent_id = le32_to_cpu(p->agent_id);
	r->fatal = IS_FATAL_ERROR(le32_to_cpu(p->error_status));
	r->cmd_count = ERROR_CMD_COUNT(le32_to_cpu(p->error_status));
	for (i = 0; i < r->cmd_count; i++)
		r->reports[i] = le64_to_cpu(p->msg_reports[i]);
	*src_id = 0;

	return r;
}

static const struct scmi_event base_events[] = {
	{
		.id = SCMI_EVENT_BASE_ERROR_EVENT,
		.max_payld_sz = sizeof(struct scmi_base_error_notify_payld),
		.max_report_sz = sizeof(struct scmi_base_error_report) +
				  SCMI_BASE_MAX_CMD_ERR_COUNT * sizeof(u64),
	},
};

static const struct scmi_event_ops base_event_ops = {
	.set_notify_enabled = scmi_base_set_notify_enabled,
	.fill_custom_report = scmi_base_fill_custom_report,
};

int scmi_base_protocol_init(struct scmi_handle *h)
{
	int id, ret;
	u8 *prot_imp;
	u32 version;
	char name[SCMI_MAX_STR_SIZE];
	const struct scmi_handle *handle = h;
	struct device *dev = handle->dev;
	struct scmi_revision_info *rev = handle->version;

	ret = scmi_version_get(handle, SCMI_PROTOCOL_BASE, &version);
	if (ret)
		return ret;

	prot_imp = devm_kcalloc(dev, MAX_PROTOCOLS_IMP, sizeof(u8), GFP_KERNEL);
	if (!prot_imp)
		return -ENOMEM;

	rev->major_ver = PROTOCOL_REV_MAJOR(version),
	rev->minor_ver = PROTOCOL_REV_MINOR(version);

	scmi_base_attributes_get(handle);
	scmi_base_vendor_id_get(handle, false);
	scmi_base_vendor_id_get(handle, true);
	scmi_base_implementation_version_get(handle);
	scmi_base_implementation_list_get(handle, prot_imp);
	scmi_setup_protocol_implemented(handle, prot_imp);

	dev_info(dev, "SCMI Protocol v%d.%d '%s:%s' Firmware version 0x%x\n",
		 rev->major_ver, rev->minor_ver, rev->vendor_id,
		 rev->sub_vendor_id, rev->impl_ver);
	dev_dbg(dev, "Found %d protocol(s) %d agent(s)\n", rev->num_protocols,
		rev->num_agents);

	scmi_register_protocol_events(handle, SCMI_PROTOCOL_BASE,
				      (4 * SCMI_PROTO_QUEUE_SZ),
				      &base_event_ops, base_events,
				      ARRAY_SIZE(base_events),
				      SCMI_BASE_NUM_SOURCES);

	for (id = 0; id < rev->num_agents; id++) {
		scmi_base_discover_agent_get(handle, id, name);
		dev_dbg(dev, "Agent %d: %s\n", id, name);
	}

	return 0;
}
