// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test cases for the DRM DP MST helpers
 *
 * Copyright (c) 2022 Maíra Canal <mairacanal@riseup.net>
 */

#define PREFIX_STR "[drm_dp_mst_helper]"

#include <kunit/test.h>

#include <linux/random.h>

#include <drm/display/drm_dp_mst_helper.h>
#include <drm/drm_print.h>

#include "../display/drm_dp_mst_topology_internal.h"

static void drm_test_dp_mst_calc_pbn_mode(struct kunit *test)
{
	int pbn, i;
	const struct {
		int rate;
		int bpp;
		int expected;
		bool dsc;
	} test_params[] = {
		{ 154000, 30, 689, false },
		{ 234000, 30, 1047, false },
		{ 297000, 24, 1063, false },
		{ 332880, 24, 50, true },
		{ 324540, 24, 49, true },
	};

	for (i = 0; i < ARRAY_SIZE(test_params); i++) {
		pbn = drm_dp_calc_pbn_mode(test_params[i].rate,
					   test_params[i].bpp,
					   test_params[i].dsc);
		KUNIT_EXPECT_EQ_MSG(test, pbn, test_params[i].expected,
				    "Expected PBN %d for clock %d bpp %d, got %d\n",
		     test_params[i].expected, test_params[i].rate,
		     test_params[i].bpp, pbn);
	}
}

static bool
sideband_msg_req_equal(const struct drm_dp_sideband_msg_req_body *in,
		       const struct drm_dp_sideband_msg_req_body *out)
{
	const struct drm_dp_remote_i2c_read_tx *txin, *txout;
	int i;

	if (in->req_type != out->req_type)
		return false;

	switch (in->req_type) {
	/*
	 * Compare struct members manually for request types which can't be
	 * compared simply using memcmp(). This is because said request types
	 * contain pointers to other allocated structs
	 */
	case DP_REMOTE_I2C_READ:
#define IN in->u.i2c_read
#define OUT out->u.i2c_read
		if (IN.num_bytes_read != OUT.num_bytes_read ||
		    IN.num_transactions != OUT.num_transactions ||
		    IN.port_number != OUT.port_number ||
		    IN.read_i2c_device_id != OUT.read_i2c_device_id)
			return false;

		for (i = 0; i < IN.num_transactions; i++) {
			txin = &IN.transactions[i];
			txout = &OUT.transactions[i];

			if (txin->i2c_dev_id != txout->i2c_dev_id ||
			    txin->no_stop_bit != txout->no_stop_bit ||
			    txin->num_bytes != txout->num_bytes ||
			    txin->i2c_transaction_delay !=
			    txout->i2c_transaction_delay)
				return false;

			if (memcmp(txin->bytes, txout->bytes,
				   txin->num_bytes) != 0)
				return false;
		}
		break;
#undef IN
#undef OUT

	case DP_REMOTE_DPCD_WRITE:
#define IN in->u.dpcd_write
#define OUT out->u.dpcd_write
		if (IN.dpcd_address != OUT.dpcd_address ||
		    IN.num_bytes != OUT.num_bytes ||
		    IN.port_number != OUT.port_number)
			return false;

		return memcmp(IN.bytes, OUT.bytes, IN.num_bytes) == 0;
#undef IN
#undef OUT

	case DP_REMOTE_I2C_WRITE:
#define IN in->u.i2c_write
#define OUT out->u.i2c_write
		if (IN.port_number != OUT.port_number ||
		    IN.write_i2c_device_id != OUT.write_i2c_device_id ||
		    IN.num_bytes != OUT.num_bytes)
			return false;

		return memcmp(IN.bytes, OUT.bytes, IN.num_bytes) == 0;
#undef IN
#undef OUT

	default:
		return memcmp(in, out, sizeof(*in)) == 0;
	}

	return true;
}

static bool
sideband_msg_req_encode_decode(struct drm_dp_sideband_msg_req_body *in)
{
	struct drm_dp_sideband_msg_req_body *out;
	struct drm_printer p = drm_err_printer(PREFIX_STR);
	struct drm_dp_sideband_msg_tx *txmsg;
	int i, ret;
	bool result = true;

	out = kzalloc(sizeof(*out), GFP_KERNEL);
	if (!out)
		return false;

	txmsg = kzalloc(sizeof(*txmsg), GFP_KERNEL);
	if (!txmsg) {
		kfree(out);
		return false;
	}

	drm_dp_encode_sideband_req(in, txmsg);
	ret = drm_dp_decode_sideband_req(txmsg, out);
	if (ret < 0) {
		drm_printf(&p, "Failed to decode sideband request: %d\n",
			   ret);
		result = false;
		goto out;
	}

	if (!sideband_msg_req_equal(in, out)) {
		drm_printf(&p, "Encode/decode failed, expected:\n");
		drm_dp_dump_sideband_msg_req_body(in, 1, &p);
		drm_printf(&p, "Got:\n");
		drm_dp_dump_sideband_msg_req_body(out, 1, &p);
		result = false;
		goto out;
	}

	switch (in->req_type) {
	case DP_REMOTE_DPCD_WRITE:
		kfree(out->u.dpcd_write.bytes);
		break;
	case DP_REMOTE_I2C_READ:
		for (i = 0; i < out->u.i2c_read.num_transactions; i++)
			kfree(out->u.i2c_read.transactions[i].bytes);
		break;
	case DP_REMOTE_I2C_WRITE:
		kfree(out->u.i2c_write.bytes);
		break;
	}

	/* Clear everything but the req_type for the input */
	memset(&in->u, 0, sizeof(in->u));

out:
	kfree(out);
	kfree(txmsg);
	return result;
}

static void drm_test_dp_mst_sideband_msg_req_decode(struct kunit *test)
{
	struct drm_dp_sideband_msg_req_body in = { 0 };
	u8 data[] = { 0xff, 0x0, 0xdd };
	int i;

	in.req_type = DP_ENUM_PATH_RESOURCES;
	in.u.port_num.port_number = 5;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));

	in.req_type = DP_POWER_UP_PHY;
	in.u.port_num.port_number = 5;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));

	in.req_type = DP_POWER_DOWN_PHY;
	in.u.port_num.port_number = 5;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));

	in.req_type = DP_ALLOCATE_PAYLOAD;
	in.u.allocate_payload.number_sdp_streams = 3;
	for (i = 0; i < in.u.allocate_payload.number_sdp_streams; i++)
		in.u.allocate_payload.sdp_stream_sink[i] = i + 1;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.allocate_payload.port_number = 0xf;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.allocate_payload.vcpi = 0x7f;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.allocate_payload.pbn = U16_MAX;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));

	in.req_type = DP_QUERY_PAYLOAD;
	in.u.query_payload.port_number = 0xf;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.query_payload.vcpi = 0x7f;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));

	in.req_type = DP_REMOTE_DPCD_READ;
	in.u.dpcd_read.port_number = 0xf;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.dpcd_read.dpcd_address = 0xfedcb;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.dpcd_read.num_bytes = U8_MAX;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));

	in.req_type = DP_REMOTE_DPCD_WRITE;
	in.u.dpcd_write.port_number = 0xf;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.dpcd_write.dpcd_address = 0xfedcb;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.dpcd_write.num_bytes = ARRAY_SIZE(data);
	in.u.dpcd_write.bytes = data;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));

	in.req_type = DP_REMOTE_I2C_READ;
	in.u.i2c_read.port_number = 0xf;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.i2c_read.read_i2c_device_id = 0x7f;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.i2c_read.num_transactions = 3;
	in.u.i2c_read.num_bytes_read = ARRAY_SIZE(data) * 3;
	for (i = 0; i < in.u.i2c_read.num_transactions; i++) {
		in.u.i2c_read.transactions[i].bytes = data;
		in.u.i2c_read.transactions[i].num_bytes = ARRAY_SIZE(data);
		in.u.i2c_read.transactions[i].i2c_dev_id = 0x7f & ~i;
		in.u.i2c_read.transactions[i].i2c_transaction_delay = 0xf & ~i;
	}
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));

	in.req_type = DP_REMOTE_I2C_WRITE;
	in.u.i2c_write.port_number = 0xf;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.i2c_write.write_i2c_device_id = 0x7f;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.i2c_write.num_bytes = ARRAY_SIZE(data);
	in.u.i2c_write.bytes = data;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));

	in.req_type = DP_QUERY_STREAM_ENC_STATUS;
	in.u.enc_status.stream_id = 1;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	get_random_bytes(in.u.enc_status.client_id,
			 sizeof(in.u.enc_status.client_id));
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.enc_status.stream_event = 3;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.enc_status.valid_stream_event = 0;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.enc_status.stream_behavior = 3;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
	in.u.enc_status.valid_stream_behavior = 1;
	KUNIT_EXPECT_TRUE(test, sideband_msg_req_encode_decode(&in));
}

static struct kunit_case drm_dp_mst_helper_tests[] = {
	KUNIT_CASE(drm_test_dp_mst_calc_pbn_mode),
	KUNIT_CASE(drm_test_dp_mst_sideband_msg_req_decode),
	{ }
};

static struct kunit_suite drm_dp_mst_helper_test_suite = {
	.name = "drm_dp_mst_helper",
	.test_cases = drm_dp_mst_helper_tests,
};

kunit_test_suite(drm_dp_mst_helper_test_suite);

MODULE_LICENSE("GPL");
