// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/platform_data/wilco-ec.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/unaligned.h>

/* Operation code; what the EC should do with the property */
enum ec_property_op {
	EC_OP_GET = 0,
	EC_OP_SET = 1,
};

struct ec_property_request {
	u8 op; /* One of enum ec_property_op */
	u8 property_id[4]; /* The 32 bit PID is stored Little Endian */
	u8 length;
	u8 data[WILCO_EC_PROPERTY_MAX_SIZE];
} __packed;

struct ec_property_response {
	u8 reserved[2];
	u8 op; /* One of enum ec_property_op */
	u8 property_id[4]; /* The 32 bit PID is stored Little Endian */
	u8 length;
	u8 data[WILCO_EC_PROPERTY_MAX_SIZE];
} __packed;

static int send_property_msg(struct wilco_ec_device *ec,
			     struct ec_property_request *rq,
			     struct ec_property_response *rs)
{
	struct wilco_ec_message ec_msg;
	int ret;

	memset(&ec_msg, 0, sizeof(ec_msg));
	ec_msg.type = WILCO_EC_MSG_PROPERTY;
	ec_msg.request_data = rq;
	ec_msg.request_size = sizeof(*rq);
	ec_msg.response_data = rs;
	ec_msg.response_size = sizeof(*rs);

	ret = wilco_ec_mailbox(ec, &ec_msg);
	if (ret < 0)
		return ret;
	if (rs->op != rq->op)
		return -EBADMSG;
	if (memcmp(rq->property_id, rs->property_id, sizeof(rs->property_id)))
		return -EBADMSG;

	return 0;
}

int wilco_ec_get_property(struct wilco_ec_device *ec,
			  struct wilco_ec_property_msg *prop_msg)
{
	struct ec_property_request rq;
	struct ec_property_response rs;
	int ret;

	memset(&rq, 0, sizeof(rq));
	rq.op = EC_OP_GET;
	put_unaligned_le32(prop_msg->property_id, rq.property_id);

	ret = send_property_msg(ec, &rq, &rs);
	if (ret < 0)
		return ret;

	prop_msg->length = rs.length;
	memcpy(prop_msg->data, rs.data, rs.length);

	return 0;
}
EXPORT_SYMBOL_GPL(wilco_ec_get_property);

int wilco_ec_set_property(struct wilco_ec_device *ec,
			  struct wilco_ec_property_msg *prop_msg)
{
	struct ec_property_request rq;
	struct ec_property_response rs;
	int ret;

	memset(&rq, 0, sizeof(rq));
	rq.op = EC_OP_SET;
	put_unaligned_le32(prop_msg->property_id, rq.property_id);
	rq.length = prop_msg->length;
	memcpy(rq.data, prop_msg->data, prop_msg->length);

	ret = send_property_msg(ec, &rq, &rs);
	if (ret < 0)
		return ret;
	if (rs.length != prop_msg->length)
		return -EBADMSG;

	return 0;
}
EXPORT_SYMBOL_GPL(wilco_ec_set_property);

int wilco_ec_get_byte_property(struct wilco_ec_device *ec, u32 property_id,
			       u8 *val)
{
	struct wilco_ec_property_msg msg;
	int ret;

	msg.property_id = property_id;

	ret = wilco_ec_get_property(ec, &msg);
	if (ret < 0)
		return ret;
	if (msg.length != 1)
		return -EBADMSG;

	*val = msg.data[0];

	return 0;
}
EXPORT_SYMBOL_GPL(wilco_ec_get_byte_property);

int wilco_ec_set_byte_property(struct wilco_ec_device *ec, u32 property_id,
			       u8 val)
{
	struct wilco_ec_property_msg msg;

	msg.property_id = property_id;
	msg.data[0] = val;
	msg.length = 1;

	return wilco_ec_set_property(ec, &msg);
}
EXPORT_SYMBOL_GPL(wilco_ec_set_byte_property);
