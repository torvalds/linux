// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit tests for ChromeOS Embedded Controller protocol.
 */

#include <kunit/test.h>

#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>

#include "cros_ec.h"

#define BUFSIZE 512

struct cros_ec_proto_test_priv {
	struct cros_ec_device ec_dev;
	u8 dout[BUFSIZE];
	u8 din[BUFSIZE];
	struct cros_ec_command *msg;
	u8 _msg[BUFSIZE];
};

static void cros_ec_proto_test_prepare_tx_legacy_normal(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct cros_ec_command *msg = priv->msg;
	int ret, i;
	u8 csum;

	ec_dev->proto_version = 2;

	msg->command = EC_CMD_HELLO;
	msg->outsize = EC_PROTO2_MAX_PARAM_SIZE;
	msg->data[0] = 0xde;
	msg->data[1] = 0xad;
	msg->data[2] = 0xbe;
	msg->data[3] = 0xef;

	ret = cros_ec_prepare_tx(ec_dev, msg);

	KUNIT_EXPECT_EQ(test, ret, EC_MSG_TX_PROTO_BYTES + EC_PROTO2_MAX_PARAM_SIZE);
	KUNIT_EXPECT_EQ(test, ec_dev->dout[0], EC_CMD_VERSION0);
	KUNIT_EXPECT_EQ(test, ec_dev->dout[1], EC_CMD_HELLO);
	KUNIT_EXPECT_EQ(test, ec_dev->dout[2], EC_PROTO2_MAX_PARAM_SIZE);
	KUNIT_EXPECT_EQ(test, EC_MSG_TX_HEADER_BYTES, 3);
	KUNIT_EXPECT_EQ(test, ec_dev->dout[EC_MSG_TX_HEADER_BYTES + 0], 0xde);
	KUNIT_EXPECT_EQ(test, ec_dev->dout[EC_MSG_TX_HEADER_BYTES + 1], 0xad);
	KUNIT_EXPECT_EQ(test, ec_dev->dout[EC_MSG_TX_HEADER_BYTES + 2], 0xbe);
	KUNIT_EXPECT_EQ(test, ec_dev->dout[EC_MSG_TX_HEADER_BYTES + 3], 0xef);
	for (i = 4; i < EC_PROTO2_MAX_PARAM_SIZE; ++i)
		KUNIT_EXPECT_EQ(test, ec_dev->dout[EC_MSG_TX_HEADER_BYTES + i], 0);

	csum = EC_CMD_VERSION0;
	csum += EC_CMD_HELLO;
	csum += EC_PROTO2_MAX_PARAM_SIZE;
	csum += 0xde;
	csum += 0xad;
	csum += 0xbe;
	csum += 0xef;
	KUNIT_EXPECT_EQ(test,
			ec_dev->dout[EC_MSG_TX_HEADER_BYTES + EC_PROTO2_MAX_PARAM_SIZE],
			csum);
}

static void cros_ec_proto_test_prepare_tx_legacy_bad_msg_outsize(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct cros_ec_command *msg = priv->msg;
	int ret;

	ec_dev->proto_version = 2;

	msg->outsize = EC_PROTO2_MAX_PARAM_SIZE + 1;

	ret = cros_ec_prepare_tx(ec_dev, msg);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void cros_ec_proto_test_prepare_tx_normal(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct cros_ec_command *msg = priv->msg;
	struct ec_host_request *request = (struct ec_host_request *)ec_dev->dout;
	int ret, i;
	u8 csum;

	msg->command = EC_CMD_HELLO;
	msg->outsize = 0x88;
	msg->data[0] = 0xde;
	msg->data[1] = 0xad;
	msg->data[2] = 0xbe;
	msg->data[3] = 0xef;

	ret = cros_ec_prepare_tx(ec_dev, msg);

	KUNIT_EXPECT_EQ(test, ret, sizeof(*request) + 0x88);

	KUNIT_EXPECT_EQ(test, request->struct_version, EC_HOST_REQUEST_VERSION);
	KUNIT_EXPECT_EQ(test, request->command, EC_CMD_HELLO);
	KUNIT_EXPECT_EQ(test, request->command_version, 0);
	KUNIT_EXPECT_EQ(test, request->data_len, 0x88);
	KUNIT_EXPECT_EQ(test, ec_dev->dout[sizeof(*request) + 0], 0xde);
	KUNIT_EXPECT_EQ(test, ec_dev->dout[sizeof(*request) + 1], 0xad);
	KUNIT_EXPECT_EQ(test, ec_dev->dout[sizeof(*request) + 2], 0xbe);
	KUNIT_EXPECT_EQ(test, ec_dev->dout[sizeof(*request) + 3], 0xef);
	for (i = 4; i < 0x88; ++i)
		KUNIT_EXPECT_EQ(test, ec_dev->dout[sizeof(*request) + i], 0);

	csum = EC_HOST_REQUEST_VERSION;
	csum += EC_CMD_HELLO;
	csum += 0x88;
	csum += 0xde;
	csum += 0xad;
	csum += 0xbe;
	csum += 0xef;
	KUNIT_EXPECT_EQ(test, request->checksum, (u8)-csum);
}

static void cros_ec_proto_test_prepare_tx_bad_msg_outsize(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct cros_ec_command *msg = priv->msg;
	int ret;

	msg->outsize = ec_dev->dout_size - sizeof(struct ec_host_request) + 1;

	ret = cros_ec_prepare_tx(ec_dev, msg);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void cros_ec_proto_test_check_result(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct cros_ec_command *msg = priv->msg;
	int ret, i;
	static enum ec_status status[] = {
		EC_RES_SUCCESS,
		EC_RES_INVALID_COMMAND,
		EC_RES_ERROR,
		EC_RES_INVALID_PARAM,
		EC_RES_ACCESS_DENIED,
		EC_RES_INVALID_RESPONSE,
		EC_RES_INVALID_VERSION,
		EC_RES_INVALID_CHECKSUM,
		EC_RES_UNAVAILABLE,
		EC_RES_TIMEOUT,
		EC_RES_OVERFLOW,
		EC_RES_INVALID_HEADER,
		EC_RES_REQUEST_TRUNCATED,
		EC_RES_RESPONSE_TOO_BIG,
		EC_RES_BUS_ERROR,
		EC_RES_BUSY,
		EC_RES_INVALID_HEADER_VERSION,
		EC_RES_INVALID_HEADER_CRC,
		EC_RES_INVALID_DATA_CRC,
		EC_RES_DUP_UNAVAILABLE,
	};

	for (i = 0; i < ARRAY_SIZE(status); ++i) {
		msg->result = status[i];
		ret = cros_ec_check_result(ec_dev, msg);
		KUNIT_EXPECT_EQ(test, ret, 0);
	}

	msg->result = EC_RES_IN_PROGRESS;
	ret = cros_ec_check_result(ec_dev, msg);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);
}

static int cros_ec_proto_test_init(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv;
	struct cros_ec_device *ec_dev;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	test->priv = priv;

	ec_dev = &priv->ec_dev;
	ec_dev->dout = (u8 *)priv->dout;
	ec_dev->dout_size = ARRAY_SIZE(priv->dout);
	ec_dev->din = (u8 *)priv->din;
	ec_dev->din_size = ARRAY_SIZE(priv->din);
	ec_dev->proto_version = EC_HOST_REQUEST_VERSION;

	priv->msg = (struct cros_ec_command *)priv->_msg;

	return 0;
}

static struct kunit_case cros_ec_proto_test_cases[] = {
	KUNIT_CASE(cros_ec_proto_test_prepare_tx_legacy_normal),
	KUNIT_CASE(cros_ec_proto_test_prepare_tx_legacy_bad_msg_outsize),
	KUNIT_CASE(cros_ec_proto_test_prepare_tx_normal),
	KUNIT_CASE(cros_ec_proto_test_prepare_tx_bad_msg_outsize),
	KUNIT_CASE(cros_ec_proto_test_check_result),
	{}
};

static struct kunit_suite cros_ec_proto_test_suite = {
	.name = "cros_ec_proto_test",
	.init = cros_ec_proto_test_init,
	.test_cases = cros_ec_proto_test_cases,
};

kunit_test_suite(cros_ec_proto_test_suite);

MODULE_LICENSE("GPL");
