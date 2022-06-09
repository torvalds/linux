// SPDX-License-Identifier: GPL-2.0
/*
 * Kunit tests for ChromeOS Embedded Controller protocol.
 */

#include <kunit/test.h>

#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>

#include "cros_ec.h"
#include "cros_kunit_util.h"

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

static void cros_ec_proto_test_query_all_pretest(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;

	/*
	 * cros_ec_query_all() will free din and dout and allocate them again to fit the usage by
	 * calling devm_kfree() and devm_kzalloc().  Set them to NULL as they aren't managed by
	 * ec_dev->dev but allocated statically in struct cros_ec_proto_test_priv
	 * (see cros_ec_proto_test_init()).
	 */
	ec_dev->din = NULL;
	ec_dev->dout = NULL;
}

static void cros_ec_proto_test_query_all_normal(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		struct ec_response_get_protocol_info *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		data = (struct ec_response_get_protocol_info *)mock->o_data;
		data->protocol_versions = BIT(3) | BIT(2);
		data->max_request_packet_size = 0xbe;
		data->max_response_packet_size = 0xef;
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		struct ec_response_get_protocol_info *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		data = (struct ec_response_get_protocol_info *)mock->o_data;
		data->max_request_packet_size = 0xbf;
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		struct ec_response_get_cmd_versions *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		data = (struct ec_response_get_cmd_versions *)mock->o_data;
		data->version_mask = BIT(6) | BIT(5);
	}

	/* For cros_ec_get_host_command_version_mask() for host sleep v1. */
	{
		struct ec_response_get_cmd_versions *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		data = (struct ec_response_get_cmd_versions *)mock->o_data;
		data->version_mask = BIT(1);
	}

	/* For cros_ec_get_host_event_wake_mask(). */
	{
		struct ec_response_host_event_mask *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		data = (struct ec_response_host_event_mask *)mock->o_data;
		data->mask = 0xbeef;
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);

		KUNIT_EXPECT_EQ(test, ec_dev->max_request, 0xbe - sizeof(struct ec_host_request));
		KUNIT_EXPECT_EQ(test, ec_dev->max_response, 0xef - sizeof(struct ec_host_response));
		KUNIT_EXPECT_EQ(test, ec_dev->proto_version, 3);
		KUNIT_EXPECT_EQ(test, ec_dev->din_size, 0xef + EC_MAX_RESPONSE_OVERHEAD);
		KUNIT_EXPECT_EQ(test, ec_dev->dout_size, 0xbe + EC_MAX_REQUEST_OVERHEAD);
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command,
				EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_PD_INDEX) |
				EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);

		KUNIT_EXPECT_EQ(test, ec_dev->max_passthru, 0xbf - sizeof(struct ec_host_request));
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		struct ec_params_get_cmd_versions *data;

		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(*data));

		data = (struct ec_params_get_cmd_versions *)mock->i_data;
		KUNIT_EXPECT_EQ(test, data->cmd, EC_CMD_GET_NEXT_EVENT);

		KUNIT_EXPECT_EQ(test, ec_dev->mkbp_event_supported, 7);
	}

	/* For cros_ec_get_host_command_version_mask() for host sleep v1. */
	{
		struct ec_params_get_cmd_versions *data;

		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(*data));

		data = (struct ec_params_get_cmd_versions *)mock->i_data;
		KUNIT_EXPECT_EQ(test, data->cmd, EC_CMD_HOST_SLEEP_EVENT);

		KUNIT_EXPECT_TRUE(test, ec_dev->host_sleep_v1);
	}

	/* For cros_ec_get_host_event_wake_mask(). */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_HOST_EVENT_GET_WAKE_MASK);
		KUNIT_EXPECT_EQ(test, mock->msg.insize, sizeof(struct ec_response_host_event_mask));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);

		KUNIT_EXPECT_EQ(test, ec_dev->host_event_wake_mask, 0xbeef);
	}
}

static void cros_ec_proto_test_query_all_no_pd_return_error(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* Set some garbage bytes. */
	ec_dev->max_passthru = 0xbf;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		struct ec_response_get_protocol_info *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		/*
		 * Although it doesn't check the value, provides valid sizes so that
		 * cros_ec_query_all() allocates din and dout correctly.
		 */
		data = (struct ec_response_get_protocol_info *)mock->o_data;
		data->max_request_packet_size = 0xbe;
		data->max_response_packet_size = 0xef;
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command,
				EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_PD_INDEX) |
				EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);

		KUNIT_EXPECT_EQ(test, ec_dev->max_passthru, 0);
	}
}

static void cros_ec_proto_test_query_all_no_pd_return0(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* Set some garbage bytes. */
	ec_dev->max_passthru = 0xbf;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		struct ec_response_get_protocol_info *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		/*
		 * Although it doesn't check the value, provides valid sizes so that
		 * cros_ec_query_all() allocates din and dout correctly.
		 */
		data = (struct ec_response_get_protocol_info *)mock->o_data;
		data->max_request_packet_size = 0xbe;
		data->max_response_packet_size = 0xef;
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_add(test, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command,
				EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_PD_INDEX) |
				EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);

		KUNIT_EXPECT_EQ(test, ec_dev->max_passthru, 0);
	}
}

static void cros_ec_proto_test_query_all_legacy_normal_v3_return_error(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_proto_info_legacy(). */
	{
		struct ec_response_hello *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		data = (struct ec_response_hello *)mock->o_data;
		data->out_data = 0xa1b2c3d4;
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info_legacy(). */
	{
		struct ec_params_hello *data;

		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_HELLO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize, sizeof(struct ec_response_hello));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(*data));

		data = (struct ec_params_hello *)mock->i_data;
		KUNIT_EXPECT_EQ(test, data->in_data, 0xa0b0c0d0);

		KUNIT_EXPECT_EQ(test, ec_dev->proto_version, 2);
		KUNIT_EXPECT_EQ(test, ec_dev->max_request, EC_PROTO2_MAX_PARAM_SIZE);
		KUNIT_EXPECT_EQ(test, ec_dev->max_response, EC_PROTO2_MAX_PARAM_SIZE);
		KUNIT_EXPECT_EQ(test, ec_dev->max_passthru, 0);
		KUNIT_EXPECT_PTR_EQ(test, ec_dev->pkt_xfer, NULL);
		KUNIT_EXPECT_EQ(test, ec_dev->din_size, EC_PROTO2_MSG_BYTES);
		KUNIT_EXPECT_EQ(test, ec_dev->dout_size, EC_PROTO2_MSG_BYTES);
	}
}

static void cros_ec_proto_test_query_all_legacy_normal_v3_return0(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_add(test, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_proto_info_legacy(). */
	{
		struct ec_response_hello *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		data = (struct ec_response_hello *)mock->o_data;
		data->out_data = 0xa1b2c3d4;
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info_legacy(). */
	{
		struct ec_params_hello *data;

		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_HELLO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize, sizeof(struct ec_response_hello));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(*data));

		data = (struct ec_params_hello *)mock->i_data;
		KUNIT_EXPECT_EQ(test, data->in_data, 0xa0b0c0d0);

		KUNIT_EXPECT_EQ(test, ec_dev->proto_version, 2);
		KUNIT_EXPECT_EQ(test, ec_dev->max_request, EC_PROTO2_MAX_PARAM_SIZE);
		KUNIT_EXPECT_EQ(test, ec_dev->max_response, EC_PROTO2_MAX_PARAM_SIZE);
		KUNIT_EXPECT_EQ(test, ec_dev->max_passthru, 0);
		KUNIT_EXPECT_PTR_EQ(test, ec_dev->pkt_xfer, NULL);
		KUNIT_EXPECT_EQ(test, ec_dev->din_size, EC_PROTO2_MSG_BYTES);
		KUNIT_EXPECT_EQ(test, ec_dev->dout_size, EC_PROTO2_MSG_BYTES);
	}
}

static void cros_ec_proto_test_query_all_legacy_xfer_error(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_proto_info_legacy(). */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, -EIO, EC_RES_SUCCESS, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_EQ(test, ec_dev->proto_version, EC_PROTO_VERSION_UNKNOWN);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info_legacy(). */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_HELLO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize, sizeof(struct ec_response_hello));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(struct ec_params_hello));
	}
}

static void cros_ec_proto_test_query_all_legacy_return_error(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_proto_info_legacy(). */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, ec_dev->proto_version, EC_PROTO_VERSION_UNKNOWN);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info_legacy(). */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_HELLO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize, sizeof(struct ec_response_hello));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(struct ec_params_hello));
	}
}

static void cros_ec_proto_test_query_all_legacy_data_error(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_proto_info_legacy(). */
	{
		struct ec_response_hello *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		data = (struct ec_response_hello *)mock->o_data;
		data->out_data = 0xbeefbfbf;
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, -EBADMSG);
	KUNIT_EXPECT_EQ(test, ec_dev->proto_version, EC_PROTO_VERSION_UNKNOWN);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info_legacy(). */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_HELLO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize, sizeof(struct ec_response_hello));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(struct ec_params_hello));
	}
}

static void cros_ec_proto_test_query_all_legacy_return0(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_proto_info_legacy(). */
	{
		mock = cros_kunit_ec_xfer_mock_add(test, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, -EPROTO);
	KUNIT_EXPECT_EQ(test, ec_dev->proto_version, EC_PROTO_VERSION_UNKNOWN);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info_legacy(). */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_HELLO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize, sizeof(struct ec_response_hello));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(struct ec_params_hello));
	}
}

static void cros_ec_proto_test_query_all_no_mkbp(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* Set some garbage bytes. */
	ec_dev->mkbp_event_supported = 0xbf;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		struct ec_response_get_protocol_info *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		/*
		 * Although it doesn't check the value, provides valid sizes so that
		 * cros_ec_query_all() allocates din and dout correctly.
		 */
		data = (struct ec_response_get_protocol_info *)mock->o_data;
		data->max_request_packet_size = 0xbe;
		data->max_response_packet_size = 0xef;
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		struct ec_response_get_cmd_versions *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		data = (struct ec_response_get_cmd_versions *)mock->o_data;
		data->version_mask = 0;
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command,
				EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_PD_INDEX) |
				EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		struct ec_params_get_cmd_versions *data;

		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(*data));

		data = (struct ec_params_get_cmd_versions *)mock->i_data;
		KUNIT_EXPECT_EQ(test, data->cmd, EC_CMD_GET_NEXT_EVENT);

		KUNIT_EXPECT_EQ(test, ec_dev->mkbp_event_supported, 0);
	}
}

static void cros_ec_proto_test_query_all_no_mkbp_return_error(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* Set some garbage bytes. */
	ec_dev->mkbp_event_supported = 0xbf;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		struct ec_response_get_protocol_info *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		/*
		 * Although it doesn't check the value, provides valid sizes so that
		 * cros_ec_query_all() allocates din and dout correctly.
		 */
		data = (struct ec_response_get_protocol_info *)mock->o_data;
		data->max_request_packet_size = 0xbe;
		data->max_response_packet_size = 0xef;
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command,
				EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_PD_INDEX) |
				EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		struct ec_params_get_cmd_versions *data;

		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(*data));

		data = (struct ec_params_get_cmd_versions *)mock->i_data;
		KUNIT_EXPECT_EQ(test, data->cmd, EC_CMD_GET_NEXT_EVENT);

		KUNIT_EXPECT_EQ(test, ec_dev->mkbp_event_supported, 0);
	}
}

static void cros_ec_proto_test_query_all_no_mkbp_return0(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* Set some garbage bytes. */
	ec_dev->mkbp_event_supported = 0xbf;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		struct ec_response_get_protocol_info *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		/*
		 * Although it doesn't check the value, provides valid sizes so that
		 * cros_ec_query_all() allocates din and dout correctly.
		 */
		data = (struct ec_response_get_protocol_info *)mock->o_data;
		data->max_request_packet_size = 0xbe;
		data->max_response_packet_size = 0xef;
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		mock = cros_kunit_ec_xfer_mock_add(test, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command,
				EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_PD_INDEX) |
				EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		struct ec_params_get_cmd_versions *data;

		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(*data));

		data = (struct ec_params_get_cmd_versions *)mock->i_data;
		KUNIT_EXPECT_EQ(test, data->cmd, EC_CMD_GET_NEXT_EVENT);

		KUNIT_EXPECT_EQ(test, ec_dev->mkbp_event_supported, 0);
	}
}

static void cros_ec_proto_test_query_all_no_host_sleep(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* Set some garbage bytes. */
	ec_dev->host_sleep_v1 = true;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		struct ec_response_get_protocol_info *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		/*
		 * Although it doesn't check the value, provides valid sizes so that
		 * cros_ec_query_all() allocates din and dout correctly.
		 */
		data = (struct ec_response_get_protocol_info *)mock->o_data;
		data->max_request_packet_size = 0xbe;
		data->max_response_packet_size = 0xef;
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		mock = cros_kunit_ec_xfer_mock_add(test, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_host_command_version_mask() for host sleep v1. */
	{
		struct ec_response_get_cmd_versions *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		data = (struct ec_response_get_cmd_versions *)mock->o_data;
		data->version_mask = 0;
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command,
				EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_PD_INDEX) |
				EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(struct ec_params_get_cmd_versions));
	}

	/* For cros_ec_get_host_command_version_mask() for host sleep v1. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(struct ec_params_get_cmd_versions));

		KUNIT_EXPECT_FALSE(test, ec_dev->host_sleep_v1);
	}
}

static void cros_ec_proto_test_query_all_no_host_sleep_return0(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* Set some garbage bytes. */
	ec_dev->host_sleep_v1 = true;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		struct ec_response_get_protocol_info *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		/*
		 * Although it doesn't check the value, provides valid sizes so that
		 * cros_ec_query_all() allocates din and dout correctly.
		 */
		data = (struct ec_response_get_protocol_info *)mock->o_data;
		data->max_request_packet_size = 0xbe;
		data->max_response_packet_size = 0xef;
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		struct ec_response_get_cmd_versions *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		/* In order to pollute next cros_ec_get_host_command_version_mask(). */
		data = (struct ec_response_get_cmd_versions *)mock->o_data;
		data->version_mask = 0xbeef;
	}

	/* For cros_ec_get_host_command_version_mask() for host sleep v1. */
	{
		mock = cros_kunit_ec_xfer_mock_add(test, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command,
				EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_PD_INDEX) |
				EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(struct ec_params_get_cmd_versions));
	}

	/* For cros_ec_get_host_command_version_mask() for host sleep v1. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(struct ec_params_get_cmd_versions));

		KUNIT_EXPECT_FALSE(test, ec_dev->host_sleep_v1);
	}
}

static void cros_ec_proto_test_query_all_default_wake_mask_return_error(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* Set some garbage bytes. */
	ec_dev->host_event_wake_mask = U32_MAX;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		struct ec_response_get_protocol_info *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		/*
		 * Although it doesn't check the value, provides valid sizes so that
		 * cros_ec_query_all() allocates din and dout correctly.
		 */
		data = (struct ec_response_get_protocol_info *)mock->o_data;
		data->max_request_packet_size = 0xbe;
		data->max_response_packet_size = 0xef;
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		mock = cros_kunit_ec_xfer_mock_add(test, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_host_command_version_mask() for host sleep v1. */
	{
		mock = cros_kunit_ec_xfer_mock_add(test, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_host_event_wake_mask(). */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command,
				EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_PD_INDEX) |
				EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(struct ec_params_get_cmd_versions));
	}

	/* For cros_ec_get_host_command_version_mask() for host sleep v1. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(struct ec_params_get_cmd_versions));
	}

	/* For cros_ec_get_host_event_wake_mask(). */
	{
		u32 mask;

		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_HOST_EVENT_GET_WAKE_MASK);
		KUNIT_EXPECT_EQ(test, mock->msg.insize, sizeof(struct ec_response_host_event_mask));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);

		mask = ec_dev->host_event_wake_mask;
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_CLOSED), 0);
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED), 0);
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_LOW), 0);
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_CRITICAL), 0);
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY), 0);
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_PD_MCU), 0);
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_STATUS), 0);
	}
}

static void cros_ec_proto_test_query_all_default_wake_mask_return0(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;
	struct ec_xfer_mock *mock;
	int ret;

	/* Set some garbage bytes. */
	ec_dev->host_event_wake_mask = U32_MAX;

	/* For cros_ec_get_proto_info() without passthru. */
	{
		struct ec_response_get_protocol_info *data;

		mock = cros_kunit_ec_xfer_mock_add(test, sizeof(*data));
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);

		/*
		 * Although it doesn't check the value, provides valid sizes so that
		 * cros_ec_query_all() allocates din and dout correctly.
		 */
		data = (struct ec_response_get_protocol_info *)mock->o_data;
		data->max_request_packet_size = 0xbe;
		data->max_response_packet_size = 0xef;
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_addx(test, 0, EC_RES_INVALID_COMMAND, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		mock = cros_kunit_ec_xfer_mock_add(test, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For cros_ec_get_host_command_version_mask() for host sleep v1. */
	{
		mock = cros_kunit_ec_xfer_mock_add(test, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	/* For get_host_event_wake_mask(). */
	{
		mock = cros_kunit_ec_xfer_mock_add(test, 0);
		KUNIT_ASSERT_PTR_NE(test, mock, NULL);
	}

	cros_ec_proto_test_query_all_pretest(test);
	ret = cros_ec_query_all(ec_dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* For cros_ec_get_proto_info() without passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_proto_info() with passthru. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command,
				EC_CMD_PASSTHRU_OFFSET(CROS_EC_DEV_PD_INDEX) |
				EC_CMD_GET_PROTOCOL_INFO);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_protocol_info));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);
	}

	/* For cros_ec_get_host_command_version_mask() for MKBP. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(struct ec_params_get_cmd_versions));
	}

	/* For cros_ec_get_host_command_version_mask() for host sleep v1. */
	{
		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_GET_CMD_VERSIONS);
		KUNIT_EXPECT_EQ(test, mock->msg.insize,
				sizeof(struct ec_response_get_cmd_versions));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, sizeof(struct ec_params_get_cmd_versions));
	}

	/* For get_host_event_wake_mask(). */
	{
		u32 mask;

		mock = cros_kunit_ec_xfer_mock_next();
		KUNIT_EXPECT_PTR_NE(test, mock, NULL);

		KUNIT_EXPECT_EQ(test, mock->msg.version, 0);
		KUNIT_EXPECT_EQ(test, mock->msg.command, EC_CMD_HOST_EVENT_GET_WAKE_MASK);
		KUNIT_EXPECT_EQ(test, mock->msg.insize, sizeof(struct ec_response_host_event_mask));
		KUNIT_EXPECT_EQ(test, mock->msg.outsize, 0);

		mask = ec_dev->host_event_wake_mask;
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_CLOSED), 0);
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED), 0);
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_LOW), 0);
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_CRITICAL), 0);
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY), 0);
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_PD_MCU), 0);
		KUNIT_EXPECT_EQ(test, mask & EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_STATUS), 0);
	}
}

static void cros_ec_proto_test_release(struct device *dev)
{
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
	ec_dev->dev = kunit_kzalloc(test, sizeof(*ec_dev->dev), GFP_KERNEL);
	if (!ec_dev->dev)
		return -ENOMEM;
	device_initialize(ec_dev->dev);
	dev_set_name(ec_dev->dev, "cros_ec_proto_test");
	ec_dev->dev->release = cros_ec_proto_test_release;
	ec_dev->cmd_xfer = cros_kunit_ec_xfer_mock;
	ec_dev->pkt_xfer = cros_kunit_ec_xfer_mock;

	priv->msg = (struct cros_ec_command *)priv->_msg;

	cros_kunit_mock_reset();

	return 0;
}

static void cros_ec_proto_test_exit(struct kunit *test)
{
	struct cros_ec_proto_test_priv *priv = test->priv;
	struct cros_ec_device *ec_dev = &priv->ec_dev;

	put_device(ec_dev->dev);
}

static struct kunit_case cros_ec_proto_test_cases[] = {
	KUNIT_CASE(cros_ec_proto_test_prepare_tx_legacy_normal),
	KUNIT_CASE(cros_ec_proto_test_prepare_tx_legacy_bad_msg_outsize),
	KUNIT_CASE(cros_ec_proto_test_prepare_tx_normal),
	KUNIT_CASE(cros_ec_proto_test_prepare_tx_bad_msg_outsize),
	KUNIT_CASE(cros_ec_proto_test_check_result),
	KUNIT_CASE(cros_ec_proto_test_query_all_normal),
	KUNIT_CASE(cros_ec_proto_test_query_all_no_pd_return_error),
	KUNIT_CASE(cros_ec_proto_test_query_all_no_pd_return0),
	KUNIT_CASE(cros_ec_proto_test_query_all_legacy_normal_v3_return_error),
	KUNIT_CASE(cros_ec_proto_test_query_all_legacy_normal_v3_return0),
	KUNIT_CASE(cros_ec_proto_test_query_all_legacy_xfer_error),
	KUNIT_CASE(cros_ec_proto_test_query_all_legacy_return_error),
	KUNIT_CASE(cros_ec_proto_test_query_all_legacy_data_error),
	KUNIT_CASE(cros_ec_proto_test_query_all_legacy_return0),
	KUNIT_CASE(cros_ec_proto_test_query_all_no_mkbp),
	KUNIT_CASE(cros_ec_proto_test_query_all_no_mkbp_return_error),
	KUNIT_CASE(cros_ec_proto_test_query_all_no_mkbp_return0),
	KUNIT_CASE(cros_ec_proto_test_query_all_no_host_sleep),
	KUNIT_CASE(cros_ec_proto_test_query_all_no_host_sleep_return0),
	KUNIT_CASE(cros_ec_proto_test_query_all_default_wake_mask_return_error),
	KUNIT_CASE(cros_ec_proto_test_query_all_default_wake_mask_return0),
	{}
};

static struct kunit_suite cros_ec_proto_test_suite = {
	.name = "cros_ec_proto_test",
	.init = cros_ec_proto_test_init,
	.exit = cros_ec_proto_test_exit,
	.test_cases = cros_ec_proto_test_cases,
};

kunit_test_suite(cros_ec_proto_test_suite);

MODULE_LICENSE("GPL");
