// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "xe_device.h"
#include "xe_kunit_helpers.h"
#include "xe_pci_test.h"

#define TEST_RID	1234
#define TEST_VFID	5
#define TEST_LEN	6
#define TEST_ACTION	0xa
#define TEST_DATA(n)	(0xd0 + (n))

static int replacement_relay_get_totalvfs(struct xe_guc_relay *relay)
{
	return TEST_VFID;
}

static int relay_test_init(struct kunit *test)
{
	struct xe_pci_fake_data fake = {
		.sriov_mode = XE_SRIOV_MODE_PF,
		.platform = XE_TIGERLAKE, /* some random platform */
		.subplatform = XE_SUBPLATFORM_NONE,
	};
	struct xe_guc_relay *relay;
	struct xe_device *xe;

	test->priv = &fake;
	xe_kunit_helper_xe_device_test_init(test);

	xe = test->priv;
	KUNIT_ASSERT_EQ(test, xe_sriov_init(xe), 0);

	relay = &xe_device_get_gt(xe, 0)->uc.guc.relay;
	kunit_activate_static_stub(test, relay_get_totalvfs,
				   replacement_relay_get_totalvfs);

	KUNIT_ASSERT_EQ(test, xe_guc_relay_init(relay), 0);
	KUNIT_EXPECT_TRUE(test, relay_is_ready(relay));
	relay->last_rid = TEST_RID - 1;

	test->priv = relay;
	return 0;
}

static const u32 TEST_MSG[TEST_LEN] = {
	FIELD_PREP_CONST(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
	FIELD_PREP_CONST(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |
	FIELD_PREP_CONST(GUC_HXG_EVENT_MSG_0_ACTION, TEST_ACTION) |
	FIELD_PREP_CONST(GUC_HXG_EVENT_MSG_0_DATA0, TEST_DATA(0)),
	TEST_DATA(1), TEST_DATA(2), TEST_DATA(3), TEST_DATA(4),
};

static int replacement_xe_guc_ct_send_recv_always_fails(struct xe_guc_ct *ct,
							const u32 *msg, u32 len,
							u32 *response_buffer)
{
	struct kunit *test = kunit_get_current_test();

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ct);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, msg);
	KUNIT_ASSERT_GE(test, len, GUC_HXG_MSG_MIN_LEN);

	return -ECOMM;
}

static int replacement_xe_guc_ct_send_recv_expects_pf2guc_relay(struct xe_guc_ct *ct,
								const u32 *msg, u32 len,
								u32 *response_buffer)
{
	struct kunit *test = kunit_get_current_test();

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ct);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, msg);
	KUNIT_ASSERT_GE(test, len, PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN);
	KUNIT_ASSERT_EQ(test, len, PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN + TEST_LEN);
	KUNIT_EXPECT_EQ(test, GUC_HXG_ORIGIN_HOST, FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]));
	KUNIT_EXPECT_EQ(test, GUC_HXG_TYPE_REQUEST, FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]));
	KUNIT_EXPECT_EQ(test, XE_GUC_ACTION_PF2GUC_RELAY_TO_VF,
			FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, msg[0]));
	KUNIT_EXPECT_EQ(test, TEST_VFID,
			FIELD_GET(PF2GUC_RELAY_TO_VF_REQUEST_MSG_1_VFID, msg[1]));
	KUNIT_EXPECT_EQ(test, TEST_RID,
			FIELD_GET(PF2GUC_RELAY_TO_VF_REQUEST_MSG_2_RELAY_ID, msg[2]));
	KUNIT_EXPECT_MEMEQ(test, TEST_MSG, msg + PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN,
			   sizeof(u32) * TEST_LEN);
	return 0;
}

static const u32 test_guc2pf[GUC2PF_RELAY_FROM_VF_EVENT_MSG_MAX_LEN] = {
	/* transport */
	FIELD_PREP_CONST(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |
	FIELD_PREP_CONST(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |
	FIELD_PREP_CONST(GUC_HXG_EVENT_MSG_0_ACTION, XE_GUC_ACTION_GUC2PF_RELAY_FROM_VF),
	FIELD_PREP_CONST(GUC2PF_RELAY_FROM_VF_EVENT_MSG_1_VFID, TEST_VFID),
	FIELD_PREP_CONST(GUC2PF_RELAY_FROM_VF_EVENT_MSG_2_RELAY_ID, TEST_RID),
	/* payload */
	FIELD_PREP_CONST(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
	FIELD_PREP_CONST(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS),
};

static const u32 test_guc2vf[GUC2VF_RELAY_FROM_PF_EVENT_MSG_MAX_LEN] = {
	/* transport */
	FIELD_PREP_CONST(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |
	FIELD_PREP_CONST(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |
	FIELD_PREP_CONST(GUC_HXG_EVENT_MSG_0_ACTION, XE_GUC_ACTION_GUC2VF_RELAY_FROM_PF),
	FIELD_PREP_CONST(GUC2VF_RELAY_FROM_PF_EVENT_MSG_1_RELAY_ID, TEST_RID),
	/* payload */
	FIELD_PREP_CONST(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
	FIELD_PREP_CONST(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS),
};

static void pf_rejects_guc2pf_too_short(struct kunit *test)
{
	const u32 len = GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN - 1;
	struct xe_guc_relay *relay = test->priv;
	const u32 *msg = test_guc2pf;

	KUNIT_ASSERT_EQ(test, -EPROTO, xe_guc_relay_process_guc2pf(relay, msg, len));
}

static void pf_rejects_guc2pf_too_long(struct kunit *test)
{
	const u32 len = GUC2PF_RELAY_FROM_VF_EVENT_MSG_MAX_LEN + 1;
	struct xe_guc_relay *relay = test->priv;
	const u32 *msg = test_guc2pf;

	KUNIT_ASSERT_EQ(test, -EMSGSIZE, xe_guc_relay_process_guc2pf(relay, msg, len));
}

static void pf_rejects_guc2pf_no_payload(struct kunit *test)
{
	const u32 len = GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN;
	struct xe_guc_relay *relay = test->priv;
	const u32 *msg = test_guc2pf;

	KUNIT_ASSERT_EQ(test, -EPROTO, xe_guc_relay_process_guc2pf(relay, msg, len));
}

static void pf_fails_no_payload(struct kunit *test)
{
	struct xe_guc_relay *relay = test->priv;
	const u32 msg = 0;

	KUNIT_ASSERT_EQ(test, -EPROTO, relay_process_msg(relay, TEST_VFID, TEST_RID, &msg, 0));
}

static void pf_fails_bad_origin(struct kunit *test)
{
	struct xe_guc_relay *relay = test->priv;
	static const u32 msg[] = {
		FIELD_PREP_CONST(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |
		FIELD_PREP_CONST(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_RESPONSE_SUCCESS),
	};
	u32 len = ARRAY_SIZE(msg);

	KUNIT_ASSERT_EQ(test, -EPROTO, relay_process_msg(relay, TEST_VFID, TEST_RID, msg, len));
}

static void pf_fails_bad_type(struct kunit *test)
{
	struct xe_guc_relay *relay = test->priv;
	const u32 msg[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, 4), /* only 4 is undefined */
	};
	u32 len = ARRAY_SIZE(msg);

	KUNIT_ASSERT_EQ(test, -EBADRQC, relay_process_msg(relay, TEST_VFID, TEST_RID, msg, len));
}

static void pf_txn_reports_error(struct kunit *test)
{
	struct xe_guc_relay *relay = test->priv;
	struct relay_transaction *txn;

	txn = __relay_get_transaction(relay, false, TEST_VFID, TEST_RID,
				      TEST_MSG, TEST_LEN, NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, txn);

	kunit_activate_static_stub(test, xe_guc_ct_send_recv,
				   replacement_xe_guc_ct_send_recv_always_fails);
	KUNIT_EXPECT_EQ(test, -ECOMM, relay_send_transaction(relay, txn));

	relay_release_transaction(relay, txn);
}

static void pf_txn_sends_pf2guc(struct kunit *test)
{
	struct xe_guc_relay *relay = test->priv;
	struct relay_transaction *txn;

	txn = __relay_get_transaction(relay, false, TEST_VFID, TEST_RID,
				      TEST_MSG, TEST_LEN, NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, txn);

	kunit_activate_static_stub(test, xe_guc_ct_send_recv,
				   replacement_xe_guc_ct_send_recv_expects_pf2guc_relay);
	KUNIT_ASSERT_EQ(test, 0, relay_send_transaction(relay, txn));

	relay_release_transaction(relay, txn);
}

static void pf_sends_pf2guc(struct kunit *test)
{
	struct xe_guc_relay *relay = test->priv;

	kunit_activate_static_stub(test, xe_guc_ct_send_recv,
				   replacement_xe_guc_ct_send_recv_expects_pf2guc_relay);
	KUNIT_ASSERT_EQ(test, 0,
			xe_guc_relay_send_to_vf(relay, TEST_VFID,
						TEST_MSG, TEST_LEN, NULL, 0));
}

static int replacement_xe_guc_ct_send_recv_loopback_relay(struct xe_guc_ct *ct,
							  const u32 *msg, u32 len,
							  u32 *response_buffer)
{
	struct kunit *test = kunit_get_current_test();
	struct xe_guc_relay *relay = test->priv;
	u32 *reply = kunit_kzalloc(test, len * sizeof(u32), GFP_KERNEL);
	int (*guc2relay)(struct xe_guc_relay *, const u32 *, u32);
	u32 action;
	int err;

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ct);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, msg);
	KUNIT_ASSERT_GE(test, len, GUC_HXG_MSG_MIN_LEN);
	KUNIT_ASSERT_EQ(test, GUC_HXG_TYPE_REQUEST,
			FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]));
	KUNIT_ASSERT_GE(test, len, GUC_HXG_REQUEST_MSG_MIN_LEN);
	KUNIT_ASSERT_NOT_NULL(test, reply);

	switch (FIELD_GET(GUC_HXG_REQUEST_MSG_0_ACTION, msg[0])) {
	case XE_GUC_ACTION_PF2GUC_RELAY_TO_VF:
		KUNIT_ASSERT_GE(test, len, PF2GUC_RELAY_TO_VF_REQUEST_MSG_MIN_LEN);
		action = XE_GUC_ACTION_GUC2PF_RELAY_FROM_VF;
		guc2relay = xe_guc_relay_process_guc2pf;
		break;
	case XE_GUC_ACTION_VF2GUC_RELAY_TO_PF:
		KUNIT_ASSERT_GE(test, len, VF2GUC_RELAY_TO_PF_REQUEST_MSG_MIN_LEN);
		action = XE_GUC_ACTION_GUC2VF_RELAY_FROM_PF;
		guc2relay = xe_guc_relay_process_guc2vf;
		break;
	default:
		KUNIT_FAIL(test, "bad RELAY action %#x", msg[0]);
		return -EINVAL;
	}

	reply[0] = FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_GUC) |
		   FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_EVENT) |
		   FIELD_PREP(GUC_HXG_EVENT_MSG_0_ACTION, action);
	memcpy(reply + 1, msg + 1, sizeof(u32) * (len - 1));

	err = guc2relay(relay, reply, len);
	KUNIT_EXPECT_EQ(test, err, 0);

	return err;
}

static void test_requires_relay_testloop(struct kunit *test)
{
	/*
	 * The debug relay action GUC_RELAY_ACTION_VFXPF_TESTLOOP is available
	 * only on builds with CONFIG_DRM_XE_DEBUG_SRIOV enabled.
	 * See "kunit.py --kconfig_add" option if it's missing.
	 */
	if (!IS_ENABLED(CONFIG_DRM_XE_DEBUG_SRIOV))
		kunit_skip(test, "requires %s\n", __stringify(CONFIG_DRM_XE_DEBUG_SRIOV));
}

static void pf_loopback_nop(struct kunit *test)
{
	struct xe_guc_relay *relay = test->priv;
	u32 request[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_RELAY_ACTION_VFXPF_TESTLOOP) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_DATA0, VFXPF_TESTLOOP_OPCODE_NOP),
	};
	u32 response[GUC_HXG_RESPONSE_MSG_MIN_LEN];
	int ret;

	test_requires_relay_testloop(test);

	kunit_activate_static_stub(test, relay_kick_worker, relay_process_incoming_action);
	kunit_activate_static_stub(test, xe_guc_ct_send_recv,
				   replacement_xe_guc_ct_send_recv_loopback_relay);
	ret = xe_guc_relay_send_to_vf(relay, TEST_VFID,
				      request, ARRAY_SIZE(request),
				      response, ARRAY_SIZE(response));
	KUNIT_ASSERT_EQ(test, ret, GUC_HXG_RESPONSE_MSG_MIN_LEN);
	KUNIT_EXPECT_EQ(test, FIELD_GET(GUC_HXG_MSG_0_ORIGIN, response[0]),
			GUC_HXG_ORIGIN_HOST);
	KUNIT_EXPECT_EQ(test, FIELD_GET(GUC_HXG_MSG_0_TYPE, response[0]),
			GUC_HXG_TYPE_RESPONSE_SUCCESS);
	KUNIT_EXPECT_EQ(test, FIELD_GET(GUC_HXG_RESPONSE_MSG_0_DATA0, response[0]), 0);
}

static void pf_loopback_echo(struct kunit *test)
{
	struct xe_guc_relay *relay = test->priv;
	u32 request[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_RELAY_ACTION_VFXPF_TESTLOOP) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_DATA0, VFXPF_TESTLOOP_OPCODE_ECHO),
		TEST_DATA(1), TEST_DATA(2), TEST_DATA(3), TEST_DATA(4),
	};
	u32 response[ARRAY_SIZE(request)];
	unsigned int n;
	int ret;

	test_requires_relay_testloop(test);

	kunit_activate_static_stub(test, relay_kick_worker, relay_process_incoming_action);
	kunit_activate_static_stub(test, xe_guc_ct_send_recv,
				   replacement_xe_guc_ct_send_recv_loopback_relay);
	ret = xe_guc_relay_send_to_vf(relay, TEST_VFID,
				      request, ARRAY_SIZE(request),
				      response, ARRAY_SIZE(response));
	KUNIT_ASSERT_EQ(test, ret, ARRAY_SIZE(response));
	KUNIT_EXPECT_EQ(test, FIELD_GET(GUC_HXG_MSG_0_ORIGIN, response[0]),
			GUC_HXG_ORIGIN_HOST);
	KUNIT_EXPECT_EQ(test, FIELD_GET(GUC_HXG_MSG_0_TYPE, response[0]),
			GUC_HXG_TYPE_RESPONSE_SUCCESS);
	KUNIT_EXPECT_EQ(test, FIELD_GET(GUC_HXG_RESPONSE_MSG_0_DATA0, response[0]),
			ARRAY_SIZE(response));
	for (n = GUC_HXG_RESPONSE_MSG_MIN_LEN; n < ret; n++)
		KUNIT_EXPECT_EQ(test, request[n], response[n]);
}

static void pf_loopback_fail(struct kunit *test)
{
	struct xe_guc_relay *relay = test->priv;
	u32 request[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_RELAY_ACTION_VFXPF_TESTLOOP) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_DATA0, VFXPF_TESTLOOP_OPCODE_FAIL),
	};
	u32 response[GUC_HXG_RESPONSE_MSG_MIN_LEN];
	int ret;

	test_requires_relay_testloop(test);

	kunit_activate_static_stub(test, relay_kick_worker, relay_process_incoming_action);
	kunit_activate_static_stub(test, xe_guc_ct_send_recv,
				   replacement_xe_guc_ct_send_recv_loopback_relay);
	ret = xe_guc_relay_send_to_vf(relay, TEST_VFID,
				      request, ARRAY_SIZE(request),
				      response, ARRAY_SIZE(response));
	KUNIT_ASSERT_EQ(test, ret, -EREMOTEIO);
}

static void pf_loopback_busy(struct kunit *test)
{
	struct xe_guc_relay *relay = test->priv;
	u32 request[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_RELAY_ACTION_VFXPF_TESTLOOP) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_DATA0, VFXPF_TESTLOOP_OPCODE_BUSY),
		TEST_DATA(0xb),
	};
	u32 response[GUC_HXG_RESPONSE_MSG_MIN_LEN];
	int ret;

	test_requires_relay_testloop(test);

	kunit_activate_static_stub(test, relay_testonly_nop, relay_process_incoming_action);
	kunit_activate_static_stub(test, relay_kick_worker, relay_process_incoming_action);
	kunit_activate_static_stub(test, xe_guc_ct_send_recv,
				   replacement_xe_guc_ct_send_recv_loopback_relay);
	ret = xe_guc_relay_send_to_vf(relay, TEST_VFID,
				      request, ARRAY_SIZE(request),
				      response, ARRAY_SIZE(response));
	KUNIT_ASSERT_EQ(test, ret, GUC_HXG_RESPONSE_MSG_MIN_LEN);
}

static void pf_loopback_retry(struct kunit *test)
{
	struct xe_guc_relay *relay = test->priv;
	u32 request[] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_RELAY_ACTION_VFXPF_TESTLOOP) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_DATA0, VFXPF_TESTLOOP_OPCODE_RETRY),
		TEST_DATA(0xd), TEST_DATA(0xd),
	};
	u32 response[GUC_HXG_RESPONSE_MSG_MIN_LEN];
	int ret;

	test_requires_relay_testloop(test);

	kunit_activate_static_stub(test, relay_kick_worker, relay_process_incoming_action);
	kunit_activate_static_stub(test, xe_guc_ct_send_recv,
				   replacement_xe_guc_ct_send_recv_loopback_relay);
	ret = xe_guc_relay_send_to_vf(relay, TEST_VFID,
				      request, ARRAY_SIZE(request),
				      response, ARRAY_SIZE(response));
	KUNIT_ASSERT_EQ(test, ret, GUC_HXG_RESPONSE_MSG_MIN_LEN);
}

static struct kunit_case pf_relay_test_cases[] = {
	KUNIT_CASE(pf_rejects_guc2pf_too_short),
	KUNIT_CASE(pf_rejects_guc2pf_too_long),
	KUNIT_CASE(pf_rejects_guc2pf_no_payload),
	KUNIT_CASE(pf_fails_no_payload),
	KUNIT_CASE(pf_fails_bad_origin),
	KUNIT_CASE(pf_fails_bad_type),
	KUNIT_CASE(pf_txn_reports_error),
	KUNIT_CASE(pf_txn_sends_pf2guc),
	KUNIT_CASE(pf_sends_pf2guc),
	KUNIT_CASE(pf_loopback_nop),
	KUNIT_CASE(pf_loopback_echo),
	KUNIT_CASE(pf_loopback_fail),
	KUNIT_CASE_SLOW(pf_loopback_busy),
	KUNIT_CASE_SLOW(pf_loopback_retry),
	{}
};

static struct kunit_suite pf_relay_suite = {
	.name = "pf_relay",
	.test_cases = pf_relay_test_cases,
	.init = relay_test_init,
};

static void vf_rejects_guc2vf_too_short(struct kunit *test)
{
	const u32 len = GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN - 1;
	struct xe_guc_relay *relay = test->priv;
	const u32 *msg = test_guc2vf;

	KUNIT_ASSERT_EQ(test, -EPROTO, xe_guc_relay_process_guc2vf(relay, msg, len));
}

static void vf_rejects_guc2vf_too_long(struct kunit *test)
{
	const u32 len = GUC2VF_RELAY_FROM_PF_EVENT_MSG_MAX_LEN + 1;
	struct xe_guc_relay *relay = test->priv;
	const u32 *msg = test_guc2vf;

	KUNIT_ASSERT_EQ(test, -EMSGSIZE, xe_guc_relay_process_guc2vf(relay, msg, len));
}

static void vf_rejects_guc2vf_no_payload(struct kunit *test)
{
	const u32 len = GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN;
	struct xe_guc_relay *relay = test->priv;
	const u32 *msg = test_guc2vf;

	KUNIT_ASSERT_EQ(test, -EPROTO, xe_guc_relay_process_guc2vf(relay, msg, len));
}

static struct kunit_case vf_relay_test_cases[] = {
	KUNIT_CASE(vf_rejects_guc2vf_too_short),
	KUNIT_CASE(vf_rejects_guc2vf_too_long),
	KUNIT_CASE(vf_rejects_guc2vf_no_payload),
	{}
};

static struct kunit_suite vf_relay_suite = {
	.name = "vf_relay",
	.test_cases = vf_relay_test_cases,
	.init = relay_test_init,
};

static void xe_drops_guc2pf_if_not_ready(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_guc_relay *relay = &xe_device_get_gt(xe, 0)->uc.guc.relay;
	const u32 *msg = test_guc2pf;
	u32 len = GUC2PF_RELAY_FROM_VF_EVENT_MSG_MIN_LEN + GUC_RELAY_MSG_MIN_LEN;

	KUNIT_ASSERT_EQ(test, -ENODEV, xe_guc_relay_process_guc2pf(relay, msg, len));
}

static void xe_drops_guc2vf_if_not_ready(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_guc_relay *relay = &xe_device_get_gt(xe, 0)->uc.guc.relay;
	const u32 *msg = test_guc2vf;
	u32 len = GUC2VF_RELAY_FROM_PF_EVENT_MSG_MIN_LEN + GUC_RELAY_MSG_MIN_LEN;

	KUNIT_ASSERT_EQ(test, -ENODEV, xe_guc_relay_process_guc2vf(relay, msg, len));
}

static void xe_rejects_send_if_not_ready(struct kunit *test)
{
	struct xe_device *xe = test->priv;
	struct xe_guc_relay *relay = &xe_device_get_gt(xe, 0)->uc.guc.relay;
	u32 msg[GUC_RELAY_MSG_MIN_LEN];
	u32 len = ARRAY_SIZE(msg);

	KUNIT_ASSERT_EQ(test, -ENODEV, xe_guc_relay_send_to_pf(relay, msg, len, NULL, 0));
	KUNIT_ASSERT_EQ(test, -ENODEV, relay_send_to(relay, TEST_VFID, msg, len, NULL, 0));
}

static struct kunit_case no_relay_test_cases[] = {
	KUNIT_CASE(xe_drops_guc2pf_if_not_ready),
	KUNIT_CASE(xe_drops_guc2vf_if_not_ready),
	KUNIT_CASE(xe_rejects_send_if_not_ready),
	{}
};

static struct kunit_suite no_relay_suite = {
	.name = "no_relay",
	.test_cases = no_relay_test_cases,
	.init = xe_kunit_helper_xe_device_test_init,
};

kunit_test_suites(&no_relay_suite,
		  &pf_relay_suite,
		  &vf_relay_suite);
