// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test cases for the DRM DP MST helpers
 *
 * Copyright (c) 2022 Maíra Canal <mairacanal@riseup.net>
 */

#include <kunit/test.h>

#include <drm/display/drm_dp_mst_helper.h>
#include <drm/drm_print.h>

#include "../display/drm_dp_mst_topology_internal.h"

struct drm_dp_mst_calc_pbn_mode_test {
	const int clock;
	const int bpp;
	const bool dsc;
	const int expected;
};

static const struct drm_dp_mst_calc_pbn_mode_test drm_dp_mst_calc_pbn_mode_cases[] = {
	{
		.clock = 154000,
		.bpp = 30,
		.dsc = false,
		.expected = 689
	},
	{
		.clock = 234000,
		.bpp = 30,
		.dsc = false,
		.expected = 1047
	},
	{
		.clock = 297000,
		.bpp = 24,
		.dsc = false,
		.expected = 1063
	},
	{
		.clock = 332880,
		.bpp = 24,
		.dsc = true,
		.expected = 1191
	},
	{
		.clock = 324540,
		.bpp = 24,
		.dsc = true,
		.expected = 1161
	},
};

static void drm_test_dp_mst_calc_pbn_mode(struct kunit *test)
{
	const struct drm_dp_mst_calc_pbn_mode_test *params = test->param_value;

	KUNIT_EXPECT_EQ(test, drm_dp_calc_pbn_mode(params->clock, params->bpp << 4),
			params->expected);
}

static void dp_mst_calc_pbn_mode_desc(const struct drm_dp_mst_calc_pbn_mode_test *t, char *desc)
{
	sprintf(desc, "Clock %d BPP %d DSC %s", t->clock, t->bpp, t->dsc ? "enabled" : "disabled");
}

KUNIT_ARRAY_PARAM(drm_dp_mst_calc_pbn_mode, drm_dp_mst_calc_pbn_mode_cases,
		  dp_mst_calc_pbn_mode_desc);

struct drm_dp_mst_calc_pbn_div_test {
	int link_rate;
	int lane_count;
	fixed20_12 expected;
};

#define fp_init(__int, __frac) { \
	.full = (__int) * (1 << 12) + \
		(__frac) * (1 << 12) / 100000 \
}

static const struct drm_dp_mst_calc_pbn_div_test drm_dp_mst_calc_pbn_div_dp1_4_cases[] = {
	/*
	 * UHBR rates (DP Standard v2.1 2.7.6.3, specifying the rounded to
	 *             closest value to 2 decimal places):
	 * .expected = .link_rate * .lane_count * 0.9671 / 8 / 54 / 100
	 * DP1.4 rates (DP Standard v2.1 2.6.4.2):
	 * .expected = .link_rate * .lane_count * 0.8000 / 8 / 54 / 100
	 *
	 * truncated to 5 decimal places.
	 */
	{
		.link_rate = 2000000,
		.lane_count = 4,
		.expected = fp_init(179,  9259),  /* 179.09259 */
	},
	{
		.link_rate = 2000000,
		.lane_count = 2,
		.expected = fp_init(89, 54629),
	},
	{
		.link_rate = 2000000,
		.lane_count = 1,
		.expected = fp_init(44, 77314),
	},
	{
		.link_rate = 1350000,
		.lane_count = 4,
		.expected = fp_init(120, 88750),
	},
	{
		.link_rate = 1350000,
		.lane_count = 2,
		.expected = fp_init(60, 44375),
	},
	{
		.link_rate = 1350000,
		.lane_count = 1,
		.expected = fp_init(30, 22187),
	},
	{
		.link_rate = 1000000,
		.lane_count = 4,
		.expected = fp_init(89, 54629),
	},
	{
		.link_rate = 1000000,
		.lane_count = 2,
		.expected = fp_init(44, 77314),
	},
	{
		.link_rate = 1000000,
		.lane_count = 1,
		.expected = fp_init(22, 38657),
	},
	{
		.link_rate = 810000,
		.lane_count = 4,
		.expected = fp_init(60, 0),
	},
	{
		.link_rate = 810000,
		.lane_count = 2,
		.expected = fp_init(30, 0),
	},
	{
		.link_rate = 810000,
		.lane_count = 1,
		.expected = fp_init(15, 0),
	},
	{
		.link_rate = 540000,
		.lane_count = 4,
		.expected = fp_init(40, 0),
	},
	{
		.link_rate = 540000,
		.lane_count = 2,
		.expected = fp_init(20, 0),
	},
	{
		.link_rate = 540000,
		.lane_count = 1,
		.expected = fp_init(10, 0),
	},
	{
		.link_rate = 270000,
		.lane_count = 4,
		.expected = fp_init(20, 0),
	},
	{
		.link_rate = 270000,
		.lane_count = 2,
		.expected = fp_init(10, 0),
	},
	{
		.link_rate = 270000,
		.lane_count = 1,
		.expected = fp_init(5, 0),
	},
	{
		.link_rate = 162000,
		.lane_count = 4,
		.expected = fp_init(12, 0),
	},
	{
		.link_rate = 162000,
		.lane_count = 2,
		.expected = fp_init(6, 0),
	},
	{
		.link_rate = 162000,
		.lane_count = 1,
		.expected = fp_init(3, 0),
	},
};

static void drm_test_dp_mst_calc_pbn_div(struct kunit *test)
{
	const struct drm_dp_mst_calc_pbn_div_test *params = test->param_value;
	/* mgr->dev is only needed by drm_dbg_kms(), but it's not called for the test cases. */
	struct drm_dp_mst_topology_mgr *mgr = test->priv;

	KUNIT_EXPECT_EQ(test, drm_dp_get_vc_payload_bw(mgr, params->link_rate, params->lane_count).full,
			params->expected.full);
}

static void dp_mst_calc_pbn_div_desc(const struct drm_dp_mst_calc_pbn_div_test *t, char *desc)
{
	sprintf(desc, "Link rate %d lane count %d", t->link_rate, t->lane_count);
}

KUNIT_ARRAY_PARAM(drm_dp_mst_calc_pbn_div, drm_dp_mst_calc_pbn_div_dp1_4_cases,
		  dp_mst_calc_pbn_div_desc);

static u8 data[] = { 0xff, 0x00, 0xdd };

struct drm_dp_mst_sideband_msg_req_test {
	const char *desc;
	const struct drm_dp_sideband_msg_req_body in;
};

static const struct drm_dp_mst_sideband_msg_req_test drm_dp_mst_sideband_msg_req_cases[] = {
	{
		.desc = "DP_ENUM_PATH_RESOURCES with port number",
		.in = {
			.req_type = DP_ENUM_PATH_RESOURCES,
			.u.port_num.port_number = 5,
		},
	},
	{
		.desc = "DP_POWER_UP_PHY with port number",
		.in = {
			.req_type = DP_POWER_UP_PHY,
			.u.port_num.port_number = 5,
		},
	},
	{
		.desc = "DP_POWER_DOWN_PHY with port number",
		.in = {
			.req_type = DP_POWER_DOWN_PHY,
			.u.port_num.port_number = 5,
		},
	},
	{
		.desc = "DP_ALLOCATE_PAYLOAD with SDP stream sinks",
		.in = {
			.req_type = DP_ALLOCATE_PAYLOAD,
			.u.allocate_payload.number_sdp_streams = 3,
			.u.allocate_payload.sdp_stream_sink = { 1, 2, 3 },
		},
	},
	{
		.desc = "DP_ALLOCATE_PAYLOAD with port number",
		.in = {
			.req_type = DP_ALLOCATE_PAYLOAD,
			.u.allocate_payload.port_number = 0xf,
		},
	},
	{
		.desc = "DP_ALLOCATE_PAYLOAD with VCPI",
		.in = {
			.req_type = DP_ALLOCATE_PAYLOAD,
			.u.allocate_payload.vcpi = 0x7f,
		},
	},
	{
		.desc = "DP_ALLOCATE_PAYLOAD with PBN",
		.in = {
			.req_type = DP_ALLOCATE_PAYLOAD,
			.u.allocate_payload.pbn = U16_MAX,
		},
	},
	{
		.desc = "DP_QUERY_PAYLOAD with port number",
		.in = {
			.req_type = DP_QUERY_PAYLOAD,
			.u.query_payload.port_number = 0xf,
		},
	},
	{
		.desc = "DP_QUERY_PAYLOAD with VCPI",
		.in = {
			.req_type = DP_QUERY_PAYLOAD,
			.u.query_payload.vcpi = 0x7f,
		},
	},
	{
		.desc = "DP_REMOTE_DPCD_READ with port number",
		.in = {
			.req_type = DP_REMOTE_DPCD_READ,
			.u.dpcd_read.port_number = 0xf,
		},
	},
	{
		.desc = "DP_REMOTE_DPCD_READ with DPCD address",
		.in = {
			.req_type = DP_REMOTE_DPCD_READ,
			.u.dpcd_read.dpcd_address = 0xfedcb,
		},
	},
	{
		.desc = "DP_REMOTE_DPCD_READ with max number of bytes",
		.in = {
			.req_type = DP_REMOTE_DPCD_READ,
			.u.dpcd_read.num_bytes = U8_MAX,
		},
	},
	{
		.desc = "DP_REMOTE_DPCD_WRITE with port number",
		.in = {
			.req_type = DP_REMOTE_DPCD_WRITE,
			.u.dpcd_write.port_number = 0xf,
		},
	},
	{
		.desc = "DP_REMOTE_DPCD_WRITE with DPCD address",
		.in = {
			.req_type = DP_REMOTE_DPCD_WRITE,
			.u.dpcd_write.dpcd_address = 0xfedcb,
		},
	},
	{
		.desc = "DP_REMOTE_DPCD_WRITE with data array",
		.in = {
			.req_type = DP_REMOTE_DPCD_WRITE,
			.u.dpcd_write.num_bytes = ARRAY_SIZE(data),
			.u.dpcd_write.bytes = data,
		},
	},
	{
		.desc = "DP_REMOTE_I2C_READ with port number",
		.in = {
			.req_type = DP_REMOTE_I2C_READ,
			.u.i2c_read.port_number = 0xf,
		},
	},
	{
		.desc = "DP_REMOTE_I2C_READ with I2C device ID",
		.in = {
			.req_type = DP_REMOTE_I2C_READ,
			.u.i2c_read.read_i2c_device_id = 0x7f,
		},
	},
	{
		.desc = "DP_REMOTE_I2C_READ with transactions array",
		.in = {
			.req_type = DP_REMOTE_I2C_READ,
			.u.i2c_read.num_transactions = 3,
			.u.i2c_read.num_bytes_read = ARRAY_SIZE(data) * 3,
			.u.i2c_read.transactions = {
				{ .bytes = data, .num_bytes = ARRAY_SIZE(data), .i2c_dev_id = 0x7f,
				  .i2c_transaction_delay = 0xf, },
				{ .bytes = data, .num_bytes = ARRAY_SIZE(data), .i2c_dev_id = 0x7e,
				  .i2c_transaction_delay = 0xe, },
				{ .bytes = data, .num_bytes = ARRAY_SIZE(data), .i2c_dev_id = 0x7d,
				  .i2c_transaction_delay = 0xd, },
			},
		},
	},
	{
		.desc = "DP_REMOTE_I2C_WRITE with port number",
		.in = {
			.req_type = DP_REMOTE_I2C_WRITE,
			.u.i2c_write.port_number = 0xf,
		},
	},
	{
		.desc = "DP_REMOTE_I2C_WRITE with I2C device ID",
		.in = {
			.req_type = DP_REMOTE_I2C_WRITE,
			.u.i2c_write.write_i2c_device_id = 0x7f,
		},
	},
	{
		.desc = "DP_REMOTE_I2C_WRITE with data array",
		.in = {
			.req_type = DP_REMOTE_I2C_WRITE,
			.u.i2c_write.num_bytes = ARRAY_SIZE(data),
			.u.i2c_write.bytes = data,
		},
	},
	{
		.desc = "DP_QUERY_STREAM_ENC_STATUS with stream ID",
		.in = {
			.req_type = DP_QUERY_STREAM_ENC_STATUS,
			.u.enc_status.stream_id = 1,
		},
	},
	{
		.desc = "DP_QUERY_STREAM_ENC_STATUS with client ID",
		.in = {
			.req_type = DP_QUERY_STREAM_ENC_STATUS,
			.u.enc_status.client_id = { 0x4f, 0x7f, 0xb4, 0x00, 0x8c, 0x0d, 0x67 },
		},
	},
	{
		.desc = "DP_QUERY_STREAM_ENC_STATUS with stream event",
		.in = {
			.req_type = DP_QUERY_STREAM_ENC_STATUS,
			.u.enc_status.stream_event = 3,
		},
	},
	{
		.desc = "DP_QUERY_STREAM_ENC_STATUS with valid stream event",
		.in = {
			.req_type = DP_QUERY_STREAM_ENC_STATUS,
			.u.enc_status.valid_stream_event = 0,
		},
	},
	{
		.desc = "DP_QUERY_STREAM_ENC_STATUS with stream behavior",
		.in = {
			.req_type = DP_QUERY_STREAM_ENC_STATUS,
			.u.enc_status.stream_behavior = 3,
		},
	},
	{
		.desc = "DP_QUERY_STREAM_ENC_STATUS with a valid stream behavior",
		.in = {
			.req_type = DP_QUERY_STREAM_ENC_STATUS,
			.u.enc_status.valid_stream_behavior = 1,
		}
	},
};

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

static void drm_test_dp_mst_msg_printf(struct drm_printer *p, struct va_format *vaf)
{
	struct kunit *test = p->arg;

	kunit_err(test, "%pV", vaf);
}

static void drm_test_dp_mst_sideband_msg_req_decode(struct kunit *test)
{
	const struct drm_dp_mst_sideband_msg_req_test *params = test->param_value;
	const struct drm_dp_sideband_msg_req_body *in = &params->in;
	struct drm_dp_sideband_msg_req_body *out;
	struct drm_dp_sideband_msg_tx *txmsg;
	struct drm_printer p = {
		.printfn = drm_test_dp_mst_msg_printf,
		.arg = test
	};
	int i;

	out = kunit_kzalloc(test, sizeof(*out), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, out);

	txmsg = kunit_kzalloc(test, sizeof(*txmsg), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, txmsg);

	drm_dp_encode_sideband_req(in, txmsg);
	KUNIT_EXPECT_GE_MSG(test, drm_dp_decode_sideband_req(txmsg, out), 0,
			    "Failed to decode sideband request");

	if (!sideband_msg_req_equal(in, out)) {
		KUNIT_FAIL(test, "Encode/decode failed");
		kunit_err(test, "Expected:");
		drm_dp_dump_sideband_msg_req_body(in, 1, &p);
		kunit_err(test, "Got:");
		drm_dp_dump_sideband_msg_req_body(out, 1, &p);
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
}

static void
drm_dp_mst_sideband_msg_req_desc(const struct drm_dp_mst_sideband_msg_req_test *t, char *desc)
{
	strcpy(desc, t->desc);
}

KUNIT_ARRAY_PARAM(drm_dp_mst_sideband_msg_req, drm_dp_mst_sideband_msg_req_cases,
		  drm_dp_mst_sideband_msg_req_desc);

static struct kunit_case drm_dp_mst_helper_tests[] = {
	KUNIT_CASE_PARAM(drm_test_dp_mst_calc_pbn_mode, drm_dp_mst_calc_pbn_mode_gen_params),
	KUNIT_CASE_PARAM(drm_test_dp_mst_calc_pbn_div, drm_dp_mst_calc_pbn_div_gen_params),
	KUNIT_CASE_PARAM(drm_test_dp_mst_sideband_msg_req_decode,
			 drm_dp_mst_sideband_msg_req_gen_params),
	{ }
};

static int drm_dp_mst_helper_tests_init(struct kunit *test)
{
	struct drm_dp_mst_topology_mgr *mgr;

	mgr = kunit_kzalloc(test, sizeof(*mgr), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mgr);

	test->priv = mgr;

	return 0;
}

static struct kunit_suite drm_dp_mst_helper_test_suite = {
	.name = "drm_dp_mst_helper",
	.init = drm_dp_mst_helper_tests_init,
	.test_cases = drm_dp_mst_helper_tests,
};

kunit_test_suite(drm_dp_mst_helper_test_suite);

MODULE_DESCRIPTION("Test cases for the DRM DP MST helpers");
MODULE_LICENSE("GPL");
