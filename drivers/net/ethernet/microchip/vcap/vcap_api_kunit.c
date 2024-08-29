// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (C) 2022 Microchip Technology Inc. and its subsidiaries.
 * Microchip VCAP API kunit test suite
 */

#include <kunit/test.h>
#include "vcap_api.h"
#include "vcap_api_client.h"
#include "vcap_model_kunit.h"

/* First we have the test infrastructure that emulates the platform
 * implementation
 */
#define TEST_BUF_CNT 100
#define TEST_BUF_SZ  350
#define STREAMWSIZE 64

static u32 test_updateaddr[STREAMWSIZE] = {};
static int test_updateaddridx;
static int test_cache_erase_count;
static u32 test_init_start;
static u32 test_init_count;
static u32 test_hw_counter_id;
static struct vcap_cache_data test_hw_cache;
static struct net_device test_netdev = {};
static int test_move_addr;
static int test_move_offset;
static int test_move_count;

/* Callback used by the VCAP API */
static enum vcap_keyfield_set test_val_keyset(struct net_device *ndev,
					      struct vcap_admin *admin,
					      struct vcap_rule *rule,
					      struct vcap_keyset_list *kslist,
					      u16 l3_proto)
{
	int idx;

	if (kslist->cnt > 0) {
		switch (admin->vtype) {
		case VCAP_TYPE_IS0:
			for (idx = 0; idx < kslist->cnt; idx++) {
				if (kslist->keysets[idx] == VCAP_KFS_ETAG)
					return kslist->keysets[idx];
				if (kslist->keysets[idx] == VCAP_KFS_PURE_5TUPLE_IP4)
					return kslist->keysets[idx];
				if (kslist->keysets[idx] == VCAP_KFS_NORMAL_5TUPLE_IP4)
					return kslist->keysets[idx];
				if (kslist->keysets[idx] == VCAP_KFS_NORMAL_7TUPLE)
					return kslist->keysets[idx];
			}
			break;
		case VCAP_TYPE_IS2:
			for (idx = 0; idx < kslist->cnt; idx++) {
				if (kslist->keysets[idx] == VCAP_KFS_MAC_ETYPE)
					return kslist->keysets[idx];
				if (kslist->keysets[idx] == VCAP_KFS_ARP)
					return kslist->keysets[idx];
				if (kslist->keysets[idx] == VCAP_KFS_IP_7TUPLE)
					return kslist->keysets[idx];
			}
			break;
		default:
			pr_info("%s:%d: no validation for VCAP %d\n",
				__func__, __LINE__, admin->vtype);
			break;
		}
	}
	return -EINVAL;
}

/* Callback used by the VCAP API */
static void test_add_def_fields(struct net_device *ndev,
				struct vcap_admin *admin,
				struct vcap_rule *rule)
{
	if (admin->vinst == 0 || admin->vinst == 2)
		vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS, VCAP_BIT_1);
	else
		vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS, VCAP_BIT_0);
}

/* Callback used by the VCAP API */
static void test_cache_erase(struct vcap_admin *admin)
{
	if (test_cache_erase_count) {
		memset(admin->cache.keystream, 0, test_cache_erase_count);
		memset(admin->cache.maskstream, 0, test_cache_erase_count);
		memset(admin->cache.actionstream, 0, test_cache_erase_count);
		test_cache_erase_count = 0;
	}
}

/* Callback used by the VCAP API */
static void test_cache_init(struct net_device *ndev, struct vcap_admin *admin,
			    u32 start, u32 count)
{
	test_init_start = start;
	test_init_count = count;
}

/* Callback used by the VCAP API */
static void test_cache_read(struct net_device *ndev, struct vcap_admin *admin,
			    enum vcap_selection sel, u32 start, u32 count)
{
	u32 *keystr, *mskstr, *actstr;
	int idx;

	pr_debug("%s:%d: %d %d\n", __func__, __LINE__, start, count);
	switch (sel) {
	case VCAP_SEL_ENTRY:
		keystr = &admin->cache.keystream[start];
		mskstr = &admin->cache.maskstream[start];
		for (idx = 0; idx < count; ++idx) {
			pr_debug("%s:%d: keydata[%02d]: 0x%08x\n", __func__,
				 __LINE__, start + idx, keystr[idx]);
		}
		for (idx = 0; idx < count; ++idx) {
			/* Invert the mask before decoding starts */
			mskstr[idx] = ~mskstr[idx];
			pr_debug("%s:%d: mskdata[%02d]: 0x%08x\n", __func__,
				 __LINE__, start + idx, mskstr[idx]);
		}
		break;
	case VCAP_SEL_ACTION:
		actstr = &admin->cache.actionstream[start];
		for (idx = 0; idx < count; ++idx) {
			pr_debug("%s:%d: actdata[%02d]: 0x%08x\n", __func__,
				 __LINE__, start + idx, actstr[idx]);
		}
		break;
	case VCAP_SEL_COUNTER:
		pr_debug("%s:%d\n", __func__, __LINE__);
		test_hw_counter_id = start;
		admin->cache.counter = test_hw_cache.counter;
		admin->cache.sticky = test_hw_cache.sticky;
		break;
	case VCAP_SEL_ALL:
		pr_debug("%s:%d\n", __func__, __LINE__);
		break;
	}
}

/* Callback used by the VCAP API */
static void test_cache_write(struct net_device *ndev, struct vcap_admin *admin,
			     enum vcap_selection sel, u32 start, u32 count)
{
	u32 *keystr, *mskstr, *actstr;
	int idx;

	switch (sel) {
	case VCAP_SEL_ENTRY:
		keystr = &admin->cache.keystream[start];
		mskstr = &admin->cache.maskstream[start];
		for (idx = 0; idx < count; ++idx) {
			pr_debug("%s:%d: keydata[%02d]: 0x%08x\n", __func__,
				 __LINE__, start + idx, keystr[idx]);
		}
		for (idx = 0; idx < count; ++idx) {
			/* Invert the mask before encoding starts */
			mskstr[idx] = ~mskstr[idx];
			pr_debug("%s:%d: mskdata[%02d]: 0x%08x\n", __func__,
				 __LINE__, start + idx, mskstr[idx]);
		}
		break;
	case VCAP_SEL_ACTION:
		actstr = &admin->cache.actionstream[start];
		for (idx = 0; idx < count; ++idx) {
			pr_debug("%s:%d: actdata[%02d]: 0x%08x\n", __func__,
				 __LINE__, start + idx, actstr[idx]);
		}
		break;
	case VCAP_SEL_COUNTER:
		pr_debug("%s:%d\n", __func__, __LINE__);
		test_hw_counter_id = start;
		test_hw_cache.counter = admin->cache.counter;
		test_hw_cache.sticky = admin->cache.sticky;
		break;
	case VCAP_SEL_ALL:
		pr_err("%s:%d: cannot write all streams at once\n",
		       __func__, __LINE__);
		break;
	}
}

/* Callback used by the VCAP API */
static void test_cache_update(struct net_device *ndev, struct vcap_admin *admin,
			      enum vcap_command cmd,
			      enum vcap_selection sel, u32 addr)
{
	if (test_updateaddridx < ARRAY_SIZE(test_updateaddr))
		test_updateaddr[test_updateaddridx] = addr;
	else
		pr_err("%s:%d: overflow: %d\n", __func__, __LINE__, test_updateaddridx);
	test_updateaddridx++;
}

static void test_cache_move(struct net_device *ndev, struct vcap_admin *admin,
			    u32 addr, int offset, int count)
{
	test_move_addr = addr;
	test_move_offset = offset;
	test_move_count = count;
}

/* Provide port information via a callback interface */
static int vcap_test_port_info(struct net_device *ndev,
			       struct vcap_admin *admin,
			       struct vcap_output_print *out)
{
	return 0;
}

static const struct vcap_operations test_callbacks = {
	.validate_keyset = test_val_keyset,
	.add_default_fields = test_add_def_fields,
	.cache_erase = test_cache_erase,
	.cache_write = test_cache_write,
	.cache_read = test_cache_read,
	.init = test_cache_init,
	.update = test_cache_update,
	.move = test_cache_move,
	.port_info = vcap_test_port_info,
};

static struct vcap_control test_vctrl = {
	.vcaps = kunit_test_vcaps,
	.stats = &kunit_test_vcap_stats,
	.ops = &test_callbacks,
};

static void vcap_test_api_init(struct vcap_admin *admin)
{
	/* Initialize the shared objects */
	INIT_LIST_HEAD(&test_vctrl.list);
	INIT_LIST_HEAD(&admin->list);
	INIT_LIST_HEAD(&admin->rules);
	INIT_LIST_HEAD(&admin->enabled);
	mutex_init(&admin->lock);
	list_add_tail(&admin->list, &test_vctrl.list);
	memset(test_updateaddr, 0, sizeof(test_updateaddr));
	test_updateaddridx = 0;
}

/* Helper function to create a rule of a specific size */
static void test_vcap_xn_rule_creator(struct kunit *test, int cid,
				      enum vcap_user user, u16 priority,
				      int id, int size, int expected_addr)
{
	struct vcap_rule *rule;
	struct vcap_rule_internal *ri;
	enum vcap_keyfield_set keyset = VCAP_KFS_NO_VALUE;
	enum vcap_actionfield_set actionset = VCAP_AFS_NO_VALUE;
	int ret;

	/* init before testing */
	memset(test_updateaddr, 0, sizeof(test_updateaddr));
	test_updateaddridx = 0;
	test_move_addr = 0;
	test_move_offset = 0;
	test_move_count = 0;

	switch (size) {
	case 2:
		keyset = VCAP_KFS_ETAG;
		actionset = VCAP_AFS_CLASS_REDUCED;
		break;
	case 3:
		keyset = VCAP_KFS_PURE_5TUPLE_IP4;
		actionset = VCAP_AFS_CLASSIFICATION;
		break;
	case 6:
		keyset = VCAP_KFS_NORMAL_5TUPLE_IP4;
		actionset = VCAP_AFS_CLASSIFICATION;
		break;
	case 12:
		keyset = VCAP_KFS_NORMAL_7TUPLE;
		actionset = VCAP_AFS_FULL;
		break;
	default:
		break;
	}

	/* Check that a valid size was used */
	KUNIT_ASSERT_NE(test, VCAP_KFS_NO_VALUE, keyset);

	/* Allocate the rule */
	rule = vcap_alloc_rule(&test_vctrl, &test_netdev, cid, user, priority,
			       id);
	KUNIT_EXPECT_PTR_NE(test, NULL, rule);

	ri = (struct vcap_rule_internal *)rule;

	/* Override rule keyset */
	ret = vcap_set_rule_set_keyset(rule, keyset);

	/* Add rule actions : there must be at least one action */
	ret = vcap_rule_add_action_u32(rule, VCAP_AF_ISDX_VAL, 0);

	/* Override rule actionset */
	ret = vcap_set_rule_set_actionset(rule, actionset);

	ret = vcap_val_rule(rule, ETH_P_ALL);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, keyset, rule->keyset);
	KUNIT_EXPECT_EQ(test, actionset, rule->actionset);
	KUNIT_EXPECT_EQ(test, size, ri->size);

	/* Add rule with write callback */
	ret = vcap_add_rule(rule);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, expected_addr, ri->addr);
	vcap_free_rule(rule);
}

/* Prepare testing rule deletion */
static void test_init_rule_deletion(void)
{
	test_move_addr = 0;
	test_move_offset = 0;
	test_move_count = 0;
	test_init_start = 0;
	test_init_count = 0;
}

/* Define the test cases. */

static void vcap_api_set_bit_1_test(struct kunit *test)
{
	struct vcap_stream_iter iter = {
		.offset = 35,
		.sw_width = 52,
		.reg_idx = 1,
		.reg_bitpos = 20,
		.tg = NULL,
	};
	u32 stream[2] = {0};

	vcap_set_bit(stream, &iter, 1);

	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[0]);
	KUNIT_EXPECT_EQ(test, (u32)BIT(20), stream[1]);
}

static void vcap_api_set_bit_0_test(struct kunit *test)
{
	struct vcap_stream_iter iter = {
		.offset = 35,
		.sw_width = 52,
		.reg_idx = 2,
		.reg_bitpos = 11,
		.tg = NULL,
	};
	u32 stream[3] = {~0, ~0, ~0};

	vcap_set_bit(stream, &iter, 0);

	KUNIT_EXPECT_EQ(test, (u32)~0, stream[0]);
	KUNIT_EXPECT_EQ(test, (u32)~0, stream[1]);
	KUNIT_EXPECT_EQ(test, (u32)~BIT(11), stream[2]);
}

static void vcap_api_iterator_init_test(struct kunit *test)
{
	struct vcap_stream_iter iter;
	struct vcap_typegroup typegroups[] = {
		{ .offset = 0, .width = 2, .value = 2, },
		{ .offset = 156, .width = 1, .value = 0, },
		{ .offset = 0, .width = 0, .value = 0, },
	};
	struct vcap_typegroup typegroups2[] = {
		{ .offset = 0, .width = 3, .value = 4, },
		{ .offset = 49, .width = 2, .value = 0, },
		{ .offset = 98, .width = 2, .value = 0, },
	};

	vcap_iter_init(&iter, 52, typegroups, 86);

	KUNIT_EXPECT_EQ(test, 52, iter.sw_width);
	KUNIT_EXPECT_EQ(test, 86 + 2, iter.offset);
	KUNIT_EXPECT_EQ(test, 3, iter.reg_idx);
	KUNIT_EXPECT_EQ(test, 4, iter.reg_bitpos);

	vcap_iter_init(&iter, 49, typegroups2, 134);

	KUNIT_EXPECT_EQ(test, 49, iter.sw_width);
	KUNIT_EXPECT_EQ(test, 134 + 7, iter.offset);
	KUNIT_EXPECT_EQ(test, 5, iter.reg_idx);
	KUNIT_EXPECT_EQ(test, 11, iter.reg_bitpos);
}

static void vcap_api_iterator_next_test(struct kunit *test)
{
	struct vcap_stream_iter iter;
	struct vcap_typegroup typegroups[] = {
		{ .offset = 0, .width = 4, .value = 8, },
		{ .offset = 49, .width = 1, .value = 0, },
		{ .offset = 98, .width = 2, .value = 0, },
		{ .offset = 147, .width = 3, .value = 0, },
		{ .offset = 196, .width = 2, .value = 0, },
		{ .offset = 245, .width = 1, .value = 0, },
	};
	int idx;

	vcap_iter_init(&iter, 49, typegroups, 86);

	KUNIT_EXPECT_EQ(test, 49, iter.sw_width);
	KUNIT_EXPECT_EQ(test, 86 + 5, iter.offset);
	KUNIT_EXPECT_EQ(test, 3, iter.reg_idx);
	KUNIT_EXPECT_EQ(test, 10, iter.reg_bitpos);

	vcap_iter_next(&iter);

	KUNIT_EXPECT_EQ(test, 91 + 1, iter.offset);
	KUNIT_EXPECT_EQ(test, 3, iter.reg_idx);
	KUNIT_EXPECT_EQ(test, 11, iter.reg_bitpos);

	for (idx = 0; idx < 6; idx++)
		vcap_iter_next(&iter);

	KUNIT_EXPECT_EQ(test, 92 + 6 + 2, iter.offset);
	KUNIT_EXPECT_EQ(test, 4, iter.reg_idx);
	KUNIT_EXPECT_EQ(test, 2, iter.reg_bitpos);
}

static void vcap_api_encode_typegroups_test(struct kunit *test)
{
	u32 stream[12] = {0};
	struct vcap_typegroup typegroups[] = {
		{ .offset = 0, .width = 4, .value = 8, },
		{ .offset = 49, .width = 1, .value = 1, },
		{ .offset = 98, .width = 2, .value = 3, },
		{ .offset = 147, .width = 3, .value = 5, },
		{ .offset = 196, .width = 2, .value = 2, },
		{ .offset = 245, .width = 5, .value = 27, },
		{ .offset = 0, .width = 0, .value = 0, },
	};

	vcap_encode_typegroups(stream, 49, typegroups, false);

	KUNIT_EXPECT_EQ(test, (u32)0x8, stream[0]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[1]);
	KUNIT_EXPECT_EQ(test, (u32)0x1, stream[2]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[3]);
	KUNIT_EXPECT_EQ(test, (u32)0x3, stream[4]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[5]);
	KUNIT_EXPECT_EQ(test, (u32)0x5, stream[6]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[7]);
	KUNIT_EXPECT_EQ(test, (u32)0x2, stream[8]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[9]);
	KUNIT_EXPECT_EQ(test, (u32)27, stream[10]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[11]);
}

static void vcap_api_encode_bit_test(struct kunit *test)
{
	struct vcap_stream_iter iter;
	u32 stream[4] = {0};
	struct vcap_typegroup typegroups[] = {
		{ .offset = 0, .width = 4, .value = 8, },
		{ .offset = 49, .width = 1, .value = 1, },
		{ .offset = 98, .width = 2, .value = 3, },
		{ .offset = 147, .width = 3, .value = 5, },
		{ .offset = 196, .width = 2, .value = 2, },
		{ .offset = 245, .width = 1, .value = 0, },
	};

	vcap_iter_init(&iter, 49, typegroups, 44);

	KUNIT_EXPECT_EQ(test, 48, iter.offset);
	KUNIT_EXPECT_EQ(test, 1, iter.reg_idx);
	KUNIT_EXPECT_EQ(test, 16, iter.reg_bitpos);

	vcap_encode_bit(stream, &iter, 1);

	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[0]);
	KUNIT_EXPECT_EQ(test, (u32)BIT(16), stream[1]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[2]);
}

static void vcap_api_encode_field_test(struct kunit *test)
{
	struct vcap_stream_iter iter;
	u32 stream[16] = {0};
	struct vcap_typegroup typegroups[] = {
		{ .offset = 0, .width = 4, .value = 8, },
		{ .offset = 49, .width = 1, .value = 1, },
		{ .offset = 98, .width = 2, .value = 3, },
		{ .offset = 147, .width = 3, .value = 5, },
		{ .offset = 196, .width = 2, .value = 2, },
		{ .offset = 245, .width = 5, .value = 27, },
		{ .offset = 0, .width = 0, .value = 0, },
	};
	struct vcap_field rf = {
		.type = VCAP_FIELD_U32,
		.offset = 86,
		.width = 4,
	};
	u8 value[] = {0x5};

	vcap_iter_init(&iter, 49, typegroups, rf.offset);

	KUNIT_EXPECT_EQ(test, 91, iter.offset);
	KUNIT_EXPECT_EQ(test, 3, iter.reg_idx);
	KUNIT_EXPECT_EQ(test, 10, iter.reg_bitpos);

	vcap_encode_field(stream, &iter, rf.width, value);

	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[0]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[1]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[2]);
	KUNIT_EXPECT_EQ(test, (u32)(0x5 << 10), stream[3]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[4]);

	vcap_encode_typegroups(stream, 49, typegroups, false);

	KUNIT_EXPECT_EQ(test, (u32)0x8, stream[0]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[1]);
	KUNIT_EXPECT_EQ(test, (u32)0x1, stream[2]);
	KUNIT_EXPECT_EQ(test, (u32)(0x5 << 10), stream[3]);
	KUNIT_EXPECT_EQ(test, (u32)0x3, stream[4]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[5]);
	KUNIT_EXPECT_EQ(test, (u32)0x5, stream[6]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[7]);
	KUNIT_EXPECT_EQ(test, (u32)0x2, stream[8]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[9]);
	KUNIT_EXPECT_EQ(test, (u32)27, stream[10]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[11]);
}

/* In this testcase the subword is smaller than a register */
static void vcap_api_encode_short_field_test(struct kunit *test)
{
	struct vcap_stream_iter iter;
	int sw_width = 21;
	u32 stream[6] = {0};
	struct vcap_typegroup tgt[] = {
		{ .offset = 0, .width = 3, .value = 7, },
		{ .offset = 21, .width = 2, .value = 3, },
		{ .offset = 42, .width = 1, .value = 1, },
		{ .offset = 0, .width = 0, .value = 0, },
	};
	struct vcap_field rf = {
		.type = VCAP_FIELD_U32,
		.offset = 25,
		.width = 4,
	};
	u8 value[] = {0x5};

	vcap_iter_init(&iter, sw_width, tgt, rf.offset);

	KUNIT_EXPECT_EQ(test, 1, iter.regs_per_sw);
	KUNIT_EXPECT_EQ(test, 21, iter.sw_width);
	KUNIT_EXPECT_EQ(test, 25 + 3 + 2, iter.offset);
	KUNIT_EXPECT_EQ(test, 1, iter.reg_idx);
	KUNIT_EXPECT_EQ(test, 25 + 3 + 2 - sw_width, iter.reg_bitpos);

	vcap_encode_field(stream, &iter, rf.width, value);

	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[0]);
	KUNIT_EXPECT_EQ(test, (u32)(0x5 << (25 + 3 + 2 - sw_width)), stream[1]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[2]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[3]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[4]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, stream[5]);

	vcap_encode_typegroups(stream, sw_width, tgt, false);

	KUNIT_EXPECT_EQ(test, (u32)7, stream[0]);
	KUNIT_EXPECT_EQ(test, (u32)((0x5 << (25 + 3 + 2 - sw_width)) + 3), stream[1]);
	KUNIT_EXPECT_EQ(test, (u32)1, stream[2]);
	KUNIT_EXPECT_EQ(test, (u32)0, stream[3]);
	KUNIT_EXPECT_EQ(test, (u32)0, stream[4]);
	KUNIT_EXPECT_EQ(test, (u32)0, stream[5]);
}

static void vcap_api_encode_keyfield_test(struct kunit *test)
{
	u32 keywords[16] = {0};
	u32 maskwords[16] = {0};
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
		.cache = {
			.keystream = keywords,
			.maskstream = maskwords,
			.actionstream = keywords,
		},
	};
	struct vcap_rule_internal rule = {
		.admin = &admin,
		.data = {
			.keyset = VCAP_KFS_MAC_ETYPE,
		},
		.vctrl = &test_vctrl,
	};
	struct vcap_client_keyfield ckf = {
		.ctrl.list = {},
		.ctrl.key = VCAP_KF_ISDX_CLS,
		.ctrl.type = VCAP_FIELD_U32,
		.data.u32.value = 0xeef014a1,
		.data.u32.mask = 0xfff,
	};
	struct vcap_field rf = {
		.type = VCAP_FIELD_U32,
		.offset = 56,
		.width = 12,
	};
	struct vcap_typegroup tgt[] = {
		{ .offset = 0, .width = 2, .value = 2, },
		{ .offset = 156, .width = 1, .value = 1, },
		{ .offset = 0, .width = 0, .value = 0, },
	};

	vcap_test_api_init(&admin);
	vcap_encode_keyfield(&rule, &ckf, &rf, tgt);

	/* Key */
	KUNIT_EXPECT_EQ(test, (u32)0x0, keywords[0]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, keywords[1]);
	KUNIT_EXPECT_EQ(test, (u32)(0x04a1 << 6), keywords[2]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, keywords[3]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, keywords[4]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, keywords[5]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, keywords[6]);

	/* Mask */
	KUNIT_EXPECT_EQ(test, (u32)0x0, maskwords[0]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, maskwords[1]);
	KUNIT_EXPECT_EQ(test, (u32)(0x0fff << 6), maskwords[2]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, maskwords[3]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, maskwords[4]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, maskwords[5]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, maskwords[6]);
}

static void vcap_api_encode_max_keyfield_test(struct kunit *test)
{
	int idx;
	u32 keywords[6] = {0};
	u32 maskwords[6] = {0};
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
		/* IS2 sw_width = 52 bit */
		.cache = {
			.keystream = keywords,
			.maskstream = maskwords,
			.actionstream = keywords,
		},
	};
	struct vcap_rule_internal rule = {
		.admin = &admin,
		.data = {
			.keyset = VCAP_KFS_IP_7TUPLE,
		},
		.vctrl = &test_vctrl,
	};
	struct vcap_client_keyfield ckf = {
		.ctrl.list = {},
		.ctrl.key = VCAP_KF_L3_IP6_DIP,
		.ctrl.type = VCAP_FIELD_U128,
		.data.u128.value = { 0xa1, 0xa2, 0xa3, 0xa4, 0, 0, 0x43, 0,
			0, 0, 0, 0, 0, 0, 0x78, 0x8e, },
		.data.u128.mask =  { 0xff, 0xff, 0xff, 0xff, 0, 0, 0xff, 0,
			0, 0, 0, 0, 0, 0, 0xff, 0xff },
	};
	struct vcap_field rf = {
		.type = VCAP_FIELD_U128,
		.offset = 0,
		.width = 128,
	};
	struct vcap_typegroup tgt[] = {
		{ .offset = 0, .width = 2, .value = 2, },
		{ .offset = 156, .width = 1, .value = 1, },
		{ .offset = 0, .width = 0, .value = 0, },
	};
	u32 keyres[] = {
		0x928e8a84,
		0x000c0002,
		0x00000010,
		0x00000000,
		0x0239e000,
		0x00000000,
	};
	u32 mskres[] = {
		0xfffffffc,
		0x000c0003,
		0x0000003f,
		0x00000000,
		0x03fffc00,
		0x00000000,
	};

	vcap_encode_keyfield(&rule, &ckf, &rf, tgt);

	/* Key */
	for (idx = 0; idx < ARRAY_SIZE(keyres); ++idx)
		KUNIT_EXPECT_EQ(test, keyres[idx], keywords[idx]);
	/* Mask */
	for (idx = 0; idx < ARRAY_SIZE(mskres); ++idx)
		KUNIT_EXPECT_EQ(test, mskres[idx], maskwords[idx]);
}

static void vcap_api_encode_actionfield_test(struct kunit *test)
{
	u32 actwords[16] = {0};
	int sw_width = 21;
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_ES2, /* act_width = 21 */
		.cache = {
			.actionstream = actwords,
		},
	};
	struct vcap_rule_internal rule = {
		.admin = &admin,
		.data = {
			.actionset = VCAP_AFS_BASE_TYPE,
		},
		.vctrl = &test_vctrl,
	};
	struct vcap_client_actionfield caf = {
		.ctrl.list = {},
		.ctrl.action = VCAP_AF_POLICE_IDX,
		.ctrl.type = VCAP_FIELD_U32,
		.data.u32.value = 0x67908032,
	};
	struct vcap_field rf = {
		.type = VCAP_FIELD_U32,
		.offset = 35,
		.width = 6,
	};
	struct vcap_typegroup tgt[] = {
		{ .offset = 0, .width = 2, .value = 2, },
		{ .offset = 21, .width = 1, .value = 1, },
		{ .offset = 42, .width = 1, .value = 0, },
		{ .offset = 0, .width = 0, .value = 0, },
	};

	vcap_encode_actionfield(&rule, &caf, &rf, tgt);

	/* Action */
	KUNIT_EXPECT_EQ(test, (u32)0x0, actwords[0]);
	KUNIT_EXPECT_EQ(test, (u32)((0x32 << (35 + 2 + 1 - sw_width)) & 0x1fffff), actwords[1]);
	KUNIT_EXPECT_EQ(test, (u32)((0x32 >> ((2 * sw_width) - 38 - 1))), actwords[2]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, actwords[3]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, actwords[4]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, actwords[5]);
	KUNIT_EXPECT_EQ(test, (u32)0x0, actwords[6]);
}

static void vcap_api_keyfield_typegroup_test(struct kunit *test)
{
	const struct vcap_typegroup *tg;

	tg = vcap_keyfield_typegroup(&test_vctrl, VCAP_TYPE_IS2, VCAP_KFS_MAC_ETYPE);
	KUNIT_EXPECT_PTR_NE(test, NULL, tg);
	KUNIT_EXPECT_EQ(test, 0, tg[0].offset);
	KUNIT_EXPECT_EQ(test, 2, tg[0].width);
	KUNIT_EXPECT_EQ(test, 2, tg[0].value);
	KUNIT_EXPECT_EQ(test, 156, tg[1].offset);
	KUNIT_EXPECT_EQ(test, 1, tg[1].width);
	KUNIT_EXPECT_EQ(test, 0, tg[1].value);
	KUNIT_EXPECT_EQ(test, 0, tg[2].offset);
	KUNIT_EXPECT_EQ(test, 0, tg[2].width);
	KUNIT_EXPECT_EQ(test, 0, tg[2].value);

	tg = vcap_keyfield_typegroup(&test_vctrl, VCAP_TYPE_ES2, VCAP_KFS_LL_FULL);
	KUNIT_EXPECT_PTR_EQ(test, NULL, tg);
}

static void vcap_api_actionfield_typegroup_test(struct kunit *test)
{
	const struct vcap_typegroup *tg;

	tg = vcap_actionfield_typegroup(&test_vctrl, VCAP_TYPE_IS0, VCAP_AFS_FULL);
	KUNIT_EXPECT_PTR_NE(test, NULL, tg);
	KUNIT_EXPECT_EQ(test, 0, tg[0].offset);
	KUNIT_EXPECT_EQ(test, 3, tg[0].width);
	KUNIT_EXPECT_EQ(test, 4, tg[0].value);
	KUNIT_EXPECT_EQ(test, 110, tg[1].offset);
	KUNIT_EXPECT_EQ(test, 2, tg[1].width);
	KUNIT_EXPECT_EQ(test, 0, tg[1].value);
	KUNIT_EXPECT_EQ(test, 220, tg[2].offset);
	KUNIT_EXPECT_EQ(test, 2, tg[2].width);
	KUNIT_EXPECT_EQ(test, 0, tg[2].value);
	KUNIT_EXPECT_EQ(test, 0, tg[3].offset);
	KUNIT_EXPECT_EQ(test, 0, tg[3].width);
	KUNIT_EXPECT_EQ(test, 0, tg[3].value);

	tg = vcap_actionfield_typegroup(&test_vctrl, VCAP_TYPE_IS2, VCAP_AFS_CLASSIFICATION);
	KUNIT_EXPECT_PTR_EQ(test, NULL, tg);
}

static void vcap_api_vcap_keyfields_test(struct kunit *test)
{
	const struct vcap_field *ft;

	ft = vcap_keyfields(&test_vctrl, VCAP_TYPE_IS2, VCAP_KFS_MAC_ETYPE);
	KUNIT_EXPECT_PTR_NE(test, NULL, ft);

	/* Keyset that is not available and within the maximum keyset enum value */
	ft = vcap_keyfields(&test_vctrl, VCAP_TYPE_ES2, VCAP_KFS_PURE_5TUPLE_IP4);
	KUNIT_EXPECT_PTR_EQ(test, NULL, ft);

	/* Keyset that is not available and beyond the maximum keyset enum value */
	ft = vcap_keyfields(&test_vctrl, VCAP_TYPE_ES2, VCAP_KFS_LL_FULL);
	KUNIT_EXPECT_PTR_EQ(test, NULL, ft);
}

static void vcap_api_vcap_actionfields_test(struct kunit *test)
{
	const struct vcap_field *ft;

	ft = vcap_actionfields(&test_vctrl, VCAP_TYPE_IS0, VCAP_AFS_FULL);
	KUNIT_EXPECT_PTR_NE(test, NULL, ft);

	ft = vcap_actionfields(&test_vctrl, VCAP_TYPE_IS2, VCAP_AFS_FULL);
	KUNIT_EXPECT_PTR_EQ(test, NULL, ft);

	ft = vcap_actionfields(&test_vctrl, VCAP_TYPE_IS2, VCAP_AFS_CLASSIFICATION);
	KUNIT_EXPECT_PTR_EQ(test, NULL, ft);
}

static void vcap_api_encode_rule_keyset_test(struct kunit *test)
{
	u32 keywords[16] = {0};
	u32 maskwords[16] = {0};
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
		.cache = {
			.keystream = keywords,
			.maskstream = maskwords,
		},
	};
	struct vcap_rule_internal rule = {
		.admin = &admin,
		.data = {
			.keyset = VCAP_KFS_MAC_ETYPE,
		},
		.vctrl = &test_vctrl,
	};
	struct vcap_client_keyfield ckf[] = {
		{
			.ctrl.key = VCAP_KF_TYPE,
			.ctrl.type = VCAP_FIELD_U32,
			.data.u32.value = 0x00,
			.data.u32.mask = 0x0f,
		},
		{
			.ctrl.key = VCAP_KF_LOOKUP_FIRST_IS,
			.ctrl.type = VCAP_FIELD_BIT,
			.data.u1.value = 0x01,
			.data.u1.mask = 0x01,
		},
		{
			.ctrl.key = VCAP_KF_IF_IGR_PORT_MASK_L3,
			.ctrl.type = VCAP_FIELD_BIT,
			.data.u1.value = 0x00,
			.data.u1.mask = 0x01,
		},
		{
			.ctrl.key = VCAP_KF_IF_IGR_PORT_MASK_RNG,
			.ctrl.type = VCAP_FIELD_U32,
			.data.u32.value = 0x00,
			.data.u32.mask = 0x0f,
		},
		{
			.ctrl.key = VCAP_KF_IF_IGR_PORT_MASK,
			.ctrl.type = VCAP_FIELD_U72,
			.data.u72.value = {0x0, 0x00, 0x00, 0x00},
			.data.u72.mask = {0xfd, 0xff, 0xff, 0xff},
		},
		{
			.ctrl.key = VCAP_KF_L2_DMAC,
			.ctrl.type = VCAP_FIELD_U48,
			/* Opposite endianness */
			.data.u48.value = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
			.data.u48.mask = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
		},
		{
			.ctrl.key = VCAP_KF_ETYPE_LEN_IS,
			.ctrl.type = VCAP_FIELD_BIT,
			.data.u1.value = 0x01,
			.data.u1.mask = 0x01,
		},
		{
			.ctrl.key = VCAP_KF_ETYPE,
			.ctrl.type = VCAP_FIELD_U32,
			.data.u32.value = 0xaabb,
			.data.u32.mask = 0xffff,
		},
	};
	int idx;
	int ret;

	/* Empty entry list */
	INIT_LIST_HEAD(&rule.data.keyfields);
	ret = vcap_encode_rule_keyset(&rule);
	KUNIT_EXPECT_EQ(test, -EINVAL, ret);

	for (idx = 0; idx < ARRAY_SIZE(ckf); idx++)
		list_add_tail(&ckf[idx].ctrl.list, &rule.data.keyfields);
	ret = vcap_encode_rule_keyset(&rule);
	KUNIT_EXPECT_EQ(test, 0, ret);

	/* The key and mask values below are from an actual Sparx5 rule config */
	/* Key */
	KUNIT_EXPECT_EQ(test, (u32)0x00000042, keywords[0]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, keywords[1]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, keywords[2]);
	KUNIT_EXPECT_EQ(test, (u32)0x00020100, keywords[3]);
	KUNIT_EXPECT_EQ(test, (u32)0x60504030, keywords[4]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, keywords[5]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, keywords[6]);
	KUNIT_EXPECT_EQ(test, (u32)0x0002aaee, keywords[7]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, keywords[8]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, keywords[9]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, keywords[10]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, keywords[11]);

	/* Mask: they will be inverted when applied to the register */
	KUNIT_EXPECT_EQ(test, (u32)~0x00b07f80, maskwords[0]);
	KUNIT_EXPECT_EQ(test, (u32)~0xfff00000, maskwords[1]);
	KUNIT_EXPECT_EQ(test, (u32)~0xfffffffc, maskwords[2]);
	KUNIT_EXPECT_EQ(test, (u32)~0xfff000ff, maskwords[3]);
	KUNIT_EXPECT_EQ(test, (u32)~0x00000000, maskwords[4]);
	KUNIT_EXPECT_EQ(test, (u32)~0xfffffff0, maskwords[5]);
	KUNIT_EXPECT_EQ(test, (u32)~0xfffffffe, maskwords[6]);
	KUNIT_EXPECT_EQ(test, (u32)~0xfffc0001, maskwords[7]);
	KUNIT_EXPECT_EQ(test, (u32)~0xffffffff, maskwords[8]);
	KUNIT_EXPECT_EQ(test, (u32)~0xffffffff, maskwords[9]);
	KUNIT_EXPECT_EQ(test, (u32)~0xffffffff, maskwords[10]);
	KUNIT_EXPECT_EQ(test, (u32)~0xffffffff, maskwords[11]);
}

static void vcap_api_encode_rule_actionset_test(struct kunit *test)
{
	u32 actwords[16] = {0};
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
		.cache = {
			.actionstream = actwords,
		},
	};
	struct vcap_rule_internal rule = {
		.admin = &admin,
		.data = {
			.actionset = VCAP_AFS_BASE_TYPE,
		},
		.vctrl = &test_vctrl,
	};
	struct vcap_client_actionfield caf[] = {
		{
			.ctrl.action = VCAP_AF_MATCH_ID,
			.ctrl.type = VCAP_FIELD_U32,
			.data.u32.value = 0x01,
		},
		{
			.ctrl.action = VCAP_AF_MATCH_ID_MASK,
			.ctrl.type = VCAP_FIELD_U32,
			.data.u32.value = 0x01,
		},
		{
			.ctrl.action = VCAP_AF_CNT_ID,
			.ctrl.type = VCAP_FIELD_U32,
			.data.u32.value = 0x64,
		},
	};
	int idx;
	int ret;

	/* Empty entry list */
	INIT_LIST_HEAD(&rule.data.actionfields);
	ret = vcap_encode_rule_actionset(&rule);
	/* We allow rules with no actions */
	KUNIT_EXPECT_EQ(test, 0, ret);

	for (idx = 0; idx < ARRAY_SIZE(caf); idx++)
		list_add_tail(&caf[idx].ctrl.list, &rule.data.actionfields);
	ret = vcap_encode_rule_actionset(&rule);
	KUNIT_EXPECT_EQ(test, 0, ret);

	/* The action values below are from an actual Sparx5 rule config */
	KUNIT_EXPECT_EQ(test, (u32)0x00000002, actwords[0]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, actwords[1]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, actwords[2]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, actwords[3]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, actwords[4]);
	KUNIT_EXPECT_EQ(test, (u32)0x00100000, actwords[5]);
	KUNIT_EXPECT_EQ(test, (u32)0x06400010, actwords[6]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, actwords[7]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, actwords[8]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, actwords[9]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, actwords[10]);
	KUNIT_EXPECT_EQ(test, (u32)0x00000000, actwords[11]);
}

static void vcap_free_ckf(struct vcap_rule *rule)
{
	struct vcap_client_keyfield *ckf, *next_ckf;

	list_for_each_entry_safe(ckf, next_ckf, &rule->keyfields, ctrl.list) {
		list_del(&ckf->ctrl.list);
		kfree(ckf);
	}
}

static void vcap_api_rule_add_keyvalue_test(struct kunit *test)
{
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
	};
	struct vcap_rule_internal ri = {
		.admin = &admin,
		.data = {
			.keyset = VCAP_KFS_NO_VALUE,
		},
		.vctrl = &test_vctrl,
	};
	struct vcap_rule *rule = (struct vcap_rule *)&ri;
	struct vcap_client_keyfield *kf;
	int ret;
	struct vcap_u128_key dip = {
		.value = {0x17, 0x26, 0x35, 0x44, 0x63, 0x62, 0x71},
		.mask = {0xf1, 0xf2, 0xf3, 0xf4, 0x4f, 0x3f, 0x2f, 0x1f},
	};
	int idx;

	INIT_LIST_HEAD(&rule->keyfields);
	ret = vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS, VCAP_BIT_0);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = list_empty(&rule->keyfields);
	KUNIT_EXPECT_EQ(test, false, ret);
	kf = list_first_entry(&rule->keyfields, struct vcap_client_keyfield,
			      ctrl.list);
	KUNIT_EXPECT_EQ(test, VCAP_KF_LOOKUP_FIRST_IS, kf->ctrl.key);
	KUNIT_EXPECT_EQ(test, VCAP_FIELD_BIT, kf->ctrl.type);
	KUNIT_EXPECT_EQ(test, 0x0, kf->data.u1.value);
	KUNIT_EXPECT_EQ(test, 0x1, kf->data.u1.mask);
	vcap_free_ckf(rule);

	INIT_LIST_HEAD(&rule->keyfields);
	ret = vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS, VCAP_BIT_1);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = list_empty(&rule->keyfields);
	KUNIT_EXPECT_EQ(test, false, ret);
	kf = list_first_entry(&rule->keyfields, struct vcap_client_keyfield,
			      ctrl.list);
	KUNIT_EXPECT_EQ(test, VCAP_KF_LOOKUP_FIRST_IS, kf->ctrl.key);
	KUNIT_EXPECT_EQ(test, VCAP_FIELD_BIT, kf->ctrl.type);
	KUNIT_EXPECT_EQ(test, 0x1, kf->data.u1.value);
	KUNIT_EXPECT_EQ(test, 0x1, kf->data.u1.mask);
	vcap_free_ckf(rule);

	INIT_LIST_HEAD(&rule->keyfields);
	ret = vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS,
				    VCAP_BIT_ANY);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = list_empty(&rule->keyfields);
	KUNIT_EXPECT_EQ(test, false, ret);
	kf = list_first_entry(&rule->keyfields, struct vcap_client_keyfield,
			      ctrl.list);
	KUNIT_EXPECT_EQ(test, VCAP_KF_LOOKUP_FIRST_IS, kf->ctrl.key);
	KUNIT_EXPECT_EQ(test, VCAP_FIELD_BIT, kf->ctrl.type);
	KUNIT_EXPECT_EQ(test, 0x0, kf->data.u1.value);
	KUNIT_EXPECT_EQ(test, 0x0, kf->data.u1.mask);
	vcap_free_ckf(rule);

	INIT_LIST_HEAD(&rule->keyfields);
	ret = vcap_rule_add_key_u32(rule, VCAP_KF_TYPE, 0x98765432, 0xff00ffab);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = list_empty(&rule->keyfields);
	KUNIT_EXPECT_EQ(test, false, ret);
	kf = list_first_entry(&rule->keyfields, struct vcap_client_keyfield,
			      ctrl.list);
	KUNIT_EXPECT_EQ(test, VCAP_KF_TYPE, kf->ctrl.key);
	KUNIT_EXPECT_EQ(test, VCAP_FIELD_U32, kf->ctrl.type);
	KUNIT_EXPECT_EQ(test, 0x98765432, kf->data.u32.value);
	KUNIT_EXPECT_EQ(test, 0xff00ffab, kf->data.u32.mask);
	vcap_free_ckf(rule);

	INIT_LIST_HEAD(&rule->keyfields);
	ret = vcap_rule_add_key_u128(rule, VCAP_KF_L3_IP6_SIP, &dip);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = list_empty(&rule->keyfields);
	KUNIT_EXPECT_EQ(test, false, ret);
	kf = list_first_entry(&rule->keyfields, struct vcap_client_keyfield,
			      ctrl.list);
	KUNIT_EXPECT_EQ(test, VCAP_KF_L3_IP6_SIP, kf->ctrl.key);
	KUNIT_EXPECT_EQ(test, VCAP_FIELD_U128, kf->ctrl.type);
	for (idx = 0; idx < ARRAY_SIZE(dip.value); ++idx)
		KUNIT_EXPECT_EQ(test, dip.value[idx], kf->data.u128.value[idx]);
	for (idx = 0; idx < ARRAY_SIZE(dip.mask); ++idx)
		KUNIT_EXPECT_EQ(test, dip.mask[idx], kf->data.u128.mask[idx]);
	vcap_free_ckf(rule);
}

static void vcap_free_caf(struct vcap_rule *rule)
{
	struct vcap_client_actionfield *caf, *next_caf;

	list_for_each_entry_safe(caf, next_caf,
				 &rule->actionfields, ctrl.list) {
		list_del(&caf->ctrl.list);
		kfree(caf);
	}
}

static void vcap_api_rule_add_actionvalue_test(struct kunit *test)
{
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
	};
	struct vcap_rule_internal ri = {
		.admin = &admin,
		.data = {
			.actionset = VCAP_AFS_NO_VALUE,
		},
	};
	struct vcap_rule *rule = (struct vcap_rule *)&ri;
	struct vcap_client_actionfield *af;
	int ret;

	INIT_LIST_HEAD(&rule->actionfields);
	ret = vcap_rule_add_action_bit(rule, VCAP_AF_POLICE_ENA, VCAP_BIT_0);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = list_empty(&rule->actionfields);
	KUNIT_EXPECT_EQ(test, false, ret);
	af = list_first_entry(&rule->actionfields,
			      struct vcap_client_actionfield, ctrl.list);
	KUNIT_EXPECT_EQ(test, VCAP_AF_POLICE_ENA, af->ctrl.action);
	KUNIT_EXPECT_EQ(test, VCAP_FIELD_BIT, af->ctrl.type);
	KUNIT_EXPECT_EQ(test, 0x0, af->data.u1.value);
	vcap_free_caf(rule);

	INIT_LIST_HEAD(&rule->actionfields);
	ret = vcap_rule_add_action_bit(rule, VCAP_AF_POLICE_ENA, VCAP_BIT_1);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = list_empty(&rule->actionfields);
	KUNIT_EXPECT_EQ(test, false, ret);
	af = list_first_entry(&rule->actionfields,
			      struct vcap_client_actionfield, ctrl.list);
	KUNIT_EXPECT_EQ(test, VCAP_AF_POLICE_ENA, af->ctrl.action);
	KUNIT_EXPECT_EQ(test, VCAP_FIELD_BIT, af->ctrl.type);
	KUNIT_EXPECT_EQ(test, 0x1, af->data.u1.value);
	vcap_free_caf(rule);

	INIT_LIST_HEAD(&rule->actionfields);
	ret = vcap_rule_add_action_bit(rule, VCAP_AF_POLICE_ENA, VCAP_BIT_ANY);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = list_empty(&rule->actionfields);
	KUNIT_EXPECT_EQ(test, false, ret);
	af = list_first_entry(&rule->actionfields,
			      struct vcap_client_actionfield, ctrl.list);
	KUNIT_EXPECT_EQ(test, VCAP_AF_POLICE_ENA, af->ctrl.action);
	KUNIT_EXPECT_EQ(test, VCAP_FIELD_BIT, af->ctrl.type);
	KUNIT_EXPECT_EQ(test, 0x0, af->data.u1.value);
	vcap_free_caf(rule);

	INIT_LIST_HEAD(&rule->actionfields);
	ret = vcap_rule_add_action_u32(rule, VCAP_AF_TYPE, 0x98765432);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = list_empty(&rule->actionfields);
	KUNIT_EXPECT_EQ(test, false, ret);
	af = list_first_entry(&rule->actionfields,
			      struct vcap_client_actionfield, ctrl.list);
	KUNIT_EXPECT_EQ(test, VCAP_AF_TYPE, af->ctrl.action);
	KUNIT_EXPECT_EQ(test, VCAP_FIELD_U32, af->ctrl.type);
	KUNIT_EXPECT_EQ(test, 0x98765432, af->data.u32.value);
	vcap_free_caf(rule);

	INIT_LIST_HEAD(&rule->actionfields);
	ret = vcap_rule_add_action_u32(rule, VCAP_AF_MASK_MODE, 0xaabbccdd);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = list_empty(&rule->actionfields);
	KUNIT_EXPECT_EQ(test, false, ret);
	af = list_first_entry(&rule->actionfields,
			      struct vcap_client_actionfield, ctrl.list);
	KUNIT_EXPECT_EQ(test, VCAP_AF_MASK_MODE, af->ctrl.action);
	KUNIT_EXPECT_EQ(test, VCAP_FIELD_U32, af->ctrl.type);
	KUNIT_EXPECT_EQ(test, 0xaabbccdd, af->data.u32.value);
	vcap_free_caf(rule);
}

static void vcap_api_rule_find_keyset_basic_test(struct kunit *test)
{
	struct vcap_keyset_list matches = {};
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
	};
	struct vcap_rule_internal ri = {
		.admin = &admin,
		.vctrl = &test_vctrl,
	};
	struct vcap_client_keyfield ckf[] = {
		{
			.ctrl.key = VCAP_KF_TYPE,
		}, {
			.ctrl.key = VCAP_KF_LOOKUP_FIRST_IS,
		}, {
			.ctrl.key = VCAP_KF_IF_IGR_PORT_MASK_L3,
		}, {
			.ctrl.key = VCAP_KF_IF_IGR_PORT_MASK_RNG,
		}, {
			.ctrl.key = VCAP_KF_IF_IGR_PORT_MASK,
		}, {
			.ctrl.key = VCAP_KF_L2_DMAC,
		}, {
			.ctrl.key = VCAP_KF_ETYPE_LEN_IS,
		}, {
			.ctrl.key = VCAP_KF_ETYPE,
		},
	};
	int idx;
	bool ret;
	enum vcap_keyfield_set keysets[10] = {};

	matches.keysets = keysets;
	matches.max = ARRAY_SIZE(keysets);

	INIT_LIST_HEAD(&ri.data.keyfields);
	for (idx = 0; idx < ARRAY_SIZE(ckf); idx++)
		list_add_tail(&ckf[idx].ctrl.list, &ri.data.keyfields);

	ret = vcap_rule_find_keysets(&ri.data, &matches);

	KUNIT_EXPECT_EQ(test, true, ret);
	KUNIT_EXPECT_EQ(test, 1, matches.cnt);
	KUNIT_EXPECT_EQ(test, VCAP_KFS_MAC_ETYPE, matches.keysets[0]);
}

static void vcap_api_rule_find_keyset_failed_test(struct kunit *test)
{
	struct vcap_keyset_list matches = {};
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
	};
	struct vcap_rule_internal ri = {
		.admin = &admin,
		.vctrl = &test_vctrl,
	};
	struct vcap_client_keyfield ckf[] = {
		{
			.ctrl.key = VCAP_KF_TYPE,
		}, {
			.ctrl.key = VCAP_KF_LOOKUP_FIRST_IS,
		}, {
			.ctrl.key = VCAP_KF_ARP_OPCODE,
		}, {
			.ctrl.key = VCAP_KF_L3_IP4_SIP,
		}, {
			.ctrl.key = VCAP_KF_L3_IP4_DIP,
		}, {
			.ctrl.key = VCAP_KF_8021Q_PCP_CLS,
		}, {
			.ctrl.key = VCAP_KF_ETYPE_LEN_IS, /* Not with ARP */
		}, {
			.ctrl.key = VCAP_KF_ETYPE, /* Not with ARP */
		},
	};
	int idx;
	bool ret;
	enum vcap_keyfield_set keysets[10] = {};

	matches.keysets = keysets;
	matches.max = ARRAY_SIZE(keysets);

	INIT_LIST_HEAD(&ri.data.keyfields);
	for (idx = 0; idx < ARRAY_SIZE(ckf); idx++)
		list_add_tail(&ckf[idx].ctrl.list, &ri.data.keyfields);

	ret = vcap_rule_find_keysets(&ri.data, &matches);

	KUNIT_EXPECT_EQ(test, false, ret);
	KUNIT_EXPECT_EQ(test, 0, matches.cnt);
	KUNIT_EXPECT_EQ(test, VCAP_KFS_NO_VALUE, matches.keysets[0]);
}

static void vcap_api_rule_find_keyset_many_test(struct kunit *test)
{
	struct vcap_keyset_list matches = {};
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
	};
	struct vcap_rule_internal ri = {
		.admin = &admin,
		.vctrl = &test_vctrl,
	};
	struct vcap_client_keyfield ckf[] = {
		{
			.ctrl.key = VCAP_KF_TYPE,
		}, {
			.ctrl.key = VCAP_KF_LOOKUP_FIRST_IS,
		}, {
			.ctrl.key = VCAP_KF_8021Q_DEI_CLS,
		}, {
			.ctrl.key = VCAP_KF_8021Q_PCP_CLS,
		}, {
			.ctrl.key = VCAP_KF_8021Q_VID_CLS,
		}, {
			.ctrl.key = VCAP_KF_ISDX_CLS,
		}, {
			.ctrl.key = VCAP_KF_L2_MC_IS,
		}, {
			.ctrl.key = VCAP_KF_L2_BC_IS,
		},
	};
	int idx;
	bool ret;
	enum vcap_keyfield_set keysets[10] = {};

	matches.keysets = keysets;
	matches.max = ARRAY_SIZE(keysets);

	INIT_LIST_HEAD(&ri.data.keyfields);
	for (idx = 0; idx < ARRAY_SIZE(ckf); idx++)
		list_add_tail(&ckf[idx].ctrl.list, &ri.data.keyfields);

	ret = vcap_rule_find_keysets(&ri.data, &matches);

	KUNIT_EXPECT_EQ(test, true, ret);
	KUNIT_EXPECT_EQ(test, 6, matches.cnt);
	KUNIT_EXPECT_EQ(test, VCAP_KFS_ARP, matches.keysets[0]);
	KUNIT_EXPECT_EQ(test, VCAP_KFS_IP4_OTHER, matches.keysets[1]);
	KUNIT_EXPECT_EQ(test, VCAP_KFS_IP4_TCP_UDP, matches.keysets[2]);
	KUNIT_EXPECT_EQ(test, VCAP_KFS_IP6_STD, matches.keysets[3]);
	KUNIT_EXPECT_EQ(test, VCAP_KFS_IP_7TUPLE, matches.keysets[4]);
	KUNIT_EXPECT_EQ(test, VCAP_KFS_MAC_ETYPE, matches.keysets[5]);
}

static void vcap_api_encode_rule_test(struct kunit *test)
{
	/* Data used by VCAP Library callback */
	static u32 keydata[32] = {};
	static u32 mskdata[32] = {};
	static u32 actdata[32] = {};

	struct vcap_admin is2_admin = {
		.vtype = VCAP_TYPE_IS2,
		.first_cid = 8000000,
		.last_cid = 8099999,
		.lookups = 4,
		.last_valid_addr = 3071,
		.first_valid_addr = 0,
		.last_used_addr = 800,
		.cache = {
			.keystream = keydata,
			.maskstream = mskdata,
			.actionstream = actdata,
		},
	};
	struct vcap_rule *rule;
	struct vcap_rule_internal *ri;
	int vcap_chain_id = 8000000;
	enum vcap_user user = VCAP_USER_VCAP_UTIL;
	u16 priority = 10;
	int id = 100;
	int ret;
	struct vcap_u48_key smac = {
		.value = { 0x88, 0x75, 0x32, 0x34, 0x9e, 0xb1 },
		.mask = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
	};
	struct vcap_u48_key dmac = {
		.value = { 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 },
		.mask = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
	};
	u32 port_mask_rng_value = 0x05;
	u32 port_mask_rng_mask = 0x0f;
	u32 igr_port_mask_value = 0xffabcd01;
	u32 igr_port_mask_mask = ~0;
	/* counter is written as the first operation */
	u32 expwriteaddr[] = {792, 792, 793, 794, 795, 796, 797};
	int idx;

	vcap_test_api_init(&is2_admin);

	/* Allocate the rule */
	rule = vcap_alloc_rule(&test_vctrl, &test_netdev, vcap_chain_id, user,
			       priority, id);
	KUNIT_EXPECT_PTR_NE(test, NULL, rule);
	ri = (struct vcap_rule_internal *)rule;

	/* Add rule keys */
	ret = vcap_rule_add_key_u48(rule, VCAP_KF_L2_DMAC, &dmac);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = vcap_rule_add_key_u48(rule, VCAP_KF_L2_SMAC, &smac);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = vcap_rule_add_key_bit(rule, VCAP_KF_ETYPE_LEN_IS, VCAP_BIT_1);
	KUNIT_EXPECT_EQ(test, 0, ret);
	/* Cannot add the same field twice */
	ret = vcap_rule_add_key_bit(rule, VCAP_KF_ETYPE_LEN_IS, VCAP_BIT_1);
	KUNIT_EXPECT_EQ(test, -EINVAL, ret);
	ret = vcap_rule_add_key_bit(rule, VCAP_KF_IF_IGR_PORT_MASK_L3,
				    VCAP_BIT_ANY);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = vcap_rule_add_key_u32(rule, VCAP_KF_IF_IGR_PORT_MASK_RNG,
				    port_mask_rng_value, port_mask_rng_mask);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = vcap_rule_add_key_u32(rule, VCAP_KF_IF_IGR_PORT_MASK,
				    igr_port_mask_value, igr_port_mask_mask);
	KUNIT_EXPECT_EQ(test, 0, ret);

	/* Add rule actions */
	ret = vcap_rule_add_action_bit(rule, VCAP_AF_POLICE_ENA, VCAP_BIT_1);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = vcap_rule_add_action_u32(rule, VCAP_AF_CNT_ID, id);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = vcap_rule_add_action_u32(rule, VCAP_AF_MATCH_ID, 1);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = vcap_rule_add_action_u32(rule, VCAP_AF_MATCH_ID_MASK, 1);
	KUNIT_EXPECT_EQ(test, 0, ret);

	/* For now the actionset is hardcoded */
	ret = vcap_set_rule_set_actionset(rule, VCAP_AFS_BASE_TYPE);
	KUNIT_EXPECT_EQ(test, 0, ret);

	/* Validation with validate keyset callback */
	ret = vcap_val_rule(rule, ETH_P_ALL);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, VCAP_KFS_MAC_ETYPE, rule->keyset);
	KUNIT_EXPECT_EQ(test, VCAP_AFS_BASE_TYPE, rule->actionset);
	KUNIT_EXPECT_EQ(test, 6, ri->size);
	KUNIT_EXPECT_EQ(test, 2, ri->keyset_sw_regs);
	KUNIT_EXPECT_EQ(test, 4, ri->actionset_sw_regs);

	/* Enable lookup, so the rule will be written */
	ret = vcap_enable_lookups(&test_vctrl, &test_netdev, 0,
				  rule->vcap_chain_id, rule->cookie, true);
	KUNIT_EXPECT_EQ(test, 0, ret);

	/* Add rule with write callback */
	ret = vcap_add_rule(rule);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 792, is2_admin.last_used_addr);
	for (idx = 0; idx < ARRAY_SIZE(expwriteaddr); ++idx)
		KUNIT_EXPECT_EQ(test, expwriteaddr[idx], test_updateaddr[idx]);

	/* Check that the rule has been added */
	ret = list_empty(&is2_admin.rules);
	KUNIT_EXPECT_EQ(test, false, ret);
	KUNIT_EXPECT_EQ(test, 0, ret);

	vcap_enable_lookups(&test_vctrl, &test_netdev, 0, 0,
			    rule->cookie, false);

	ret = vcap_del_rule(&test_vctrl, &test_netdev, id);
	KUNIT_EXPECT_EQ(test, 0, ret);
}

static void vcap_api_set_rule_counter_test(struct kunit *test)
{
	struct vcap_admin is2_admin = {
		.cache = {
			.counter = 100,
			.sticky = true,
		},
	};
	struct vcap_rule_internal ri = {
		.data = {
			.id = 1001,
		},
		.addr = 600,
		.admin = &is2_admin,
		.counter_id = 1002,
		.vctrl = &test_vctrl,
	};
	struct vcap_rule_internal ri2 = {
		.data = {
			.id = 2001,
		},
		.addr = 700,
		.admin = &is2_admin,
		.counter_id = 2002,
		.vctrl = &test_vctrl,
	};
	struct vcap_counter ctr = { .value = 0, .sticky = false};
	struct vcap_counter ctr2 = { .value = 101, .sticky = true};
	int ret;

	vcap_test_api_init(&is2_admin);
	list_add_tail(&ri.list, &is2_admin.rules);
	list_add_tail(&ri2.list, &is2_admin.rules);

	pr_info("%s:%d\n", __func__, __LINE__);
	ret = vcap_rule_set_counter(&ri.data, &ctr);
	pr_info("%s:%d\n", __func__, __LINE__);
	KUNIT_EXPECT_EQ(test, 0, ret);

	KUNIT_EXPECT_EQ(test, 1002, test_hw_counter_id);
	KUNIT_EXPECT_EQ(test, 0, test_hw_cache.counter);
	KUNIT_EXPECT_EQ(test, false, test_hw_cache.sticky);
	KUNIT_EXPECT_EQ(test, 600, test_updateaddr[0]);

	ret = vcap_rule_set_counter(&ri2.data, &ctr2);
	KUNIT_EXPECT_EQ(test, 0, ret);

	KUNIT_EXPECT_EQ(test, 2002, test_hw_counter_id);
	KUNIT_EXPECT_EQ(test, 101, test_hw_cache.counter);
	KUNIT_EXPECT_EQ(test, true, test_hw_cache.sticky);
	KUNIT_EXPECT_EQ(test, 700, test_updateaddr[1]);
}

static void vcap_api_get_rule_counter_test(struct kunit *test)
{
	struct vcap_admin is2_admin = {
		.cache = {
			.counter = 100,
			.sticky = true,
		},
	};
	struct vcap_rule_internal ri = {
		.data = {
			.id = 1010,
		},
		.addr = 400,
		.admin = &is2_admin,
		.counter_id = 1011,
		.vctrl = &test_vctrl,
	};
	struct vcap_rule_internal ri2 = {
		.data = {
			.id = 2011,
		},
		.addr = 300,
		.admin = &is2_admin,
		.counter_id = 2012,
		.vctrl = &test_vctrl,
	};
	struct vcap_counter ctr = {};
	struct vcap_counter ctr2 = {};
	int ret;

	vcap_test_api_init(&is2_admin);
	test_hw_cache.counter = 55;
	test_hw_cache.sticky = true;

	list_add_tail(&ri.list, &is2_admin.rules);
	list_add_tail(&ri2.list, &is2_admin.rules);

	ret = vcap_rule_get_counter(&ri.data, &ctr);
	KUNIT_EXPECT_EQ(test, 0, ret);

	KUNIT_EXPECT_EQ(test, 1011, test_hw_counter_id);
	KUNIT_EXPECT_EQ(test, 55, ctr.value);
	KUNIT_EXPECT_EQ(test, true, ctr.sticky);
	KUNIT_EXPECT_EQ(test, 400, test_updateaddr[0]);

	test_hw_cache.counter = 22;
	test_hw_cache.sticky = false;

	ret = vcap_rule_get_counter(&ri2.data, &ctr2);
	KUNIT_EXPECT_EQ(test, 0, ret);

	KUNIT_EXPECT_EQ(test, 2012, test_hw_counter_id);
	KUNIT_EXPECT_EQ(test, 22, ctr2.value);
	KUNIT_EXPECT_EQ(test, false, ctr2.sticky);
	KUNIT_EXPECT_EQ(test, 300, test_updateaddr[1]);
}

static void vcap_api_rule_insert_in_order_test(struct kunit *test)
{
	/* Data used by VCAP Library callback */
	static u32 keydata[32] = {};
	static u32 mskdata[32] = {};
	static u32 actdata[32] = {};

	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS0,
		.first_cid = 10000,
		.last_cid = 19999,
		.lookups = 4,
		.last_valid_addr = 3071,
		.first_valid_addr = 0,
		.last_used_addr = 800,
		.cache = {
			.keystream = keydata,
			.maskstream = mskdata,
			.actionstream = actdata,
		},
	};

	vcap_test_api_init(&admin);

	/* Create rules with different sizes and check that they are placed
	 * at the correct address in the VCAP according to size
	 */
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 10, 500, 12, 780);
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 20, 400, 6, 774);
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 30, 300, 3, 771);
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 40, 200, 2, 768);

	vcap_del_rule(&test_vctrl, &test_netdev, 200);
	vcap_del_rule(&test_vctrl, &test_netdev, 300);
	vcap_del_rule(&test_vctrl, &test_netdev, 400);
	vcap_del_rule(&test_vctrl, &test_netdev, 500);
}

static void vcap_api_rule_insert_reverse_order_test(struct kunit *test)
{
	/* Data used by VCAP Library callback */
	static u32 keydata[32] = {};
	static u32 mskdata[32] = {};
	static u32 actdata[32] = {};

	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS0,
		.first_cid = 10000,
		.last_cid = 19999,
		.lookups = 4,
		.last_valid_addr = 3071,
		.first_valid_addr = 0,
		.last_used_addr = 800,
		.cache = {
			.keystream = keydata,
			.maskstream = mskdata,
			.actionstream = actdata,
		},
	};
	struct vcap_rule_internal *elem;
	u32 exp_addr[] = {780, 774, 771, 768, 767};
	int idx;

	vcap_test_api_init(&admin);

	/* Create rules with different sizes and check that they are placed
	 * at the correct address in the VCAP according to size
	 */
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 20, 200, 2, 798);
	KUNIT_EXPECT_EQ(test, 0, test_move_offset);
	KUNIT_EXPECT_EQ(test, 0, test_move_count);
	KUNIT_EXPECT_EQ(test, 0, test_move_addr);

	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 30, 300, 3, 795);
	KUNIT_EXPECT_EQ(test, 6, test_move_offset);
	KUNIT_EXPECT_EQ(test, 3, test_move_count);
	KUNIT_EXPECT_EQ(test, 798, test_move_addr);

	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 40, 400, 6, 792);
	KUNIT_EXPECT_EQ(test, 6, test_move_offset);
	KUNIT_EXPECT_EQ(test, 6, test_move_count);
	KUNIT_EXPECT_EQ(test, 792, test_move_addr);

	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 50, 500, 12, 780);
	KUNIT_EXPECT_EQ(test, 18, test_move_offset);
	KUNIT_EXPECT_EQ(test, 12, test_move_count);
	KUNIT_EXPECT_EQ(test, 786, test_move_addr);

	idx = 0;
	list_for_each_entry(elem, &admin.rules, list) {
		KUNIT_EXPECT_EQ(test, exp_addr[idx], elem->addr);
		++idx;
	}
	KUNIT_EXPECT_EQ(test, 768, admin.last_used_addr);

	vcap_del_rule(&test_vctrl, &test_netdev, 500);
	vcap_del_rule(&test_vctrl, &test_netdev, 400);
	vcap_del_rule(&test_vctrl, &test_netdev, 300);
	vcap_del_rule(&test_vctrl, &test_netdev, 200);
}

static void vcap_api_rule_remove_at_end_test(struct kunit *test)
{
	/* Data used by VCAP Library callback */
	static u32 keydata[32] = {};
	static u32 mskdata[32] = {};
	static u32 actdata[32] = {};

	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS0,
		.first_cid = 10000,
		.last_cid = 19999,
		.lookups = 4,
		.last_valid_addr = 3071,
		.first_valid_addr = 0,
		.last_used_addr = 800,
		.cache = {
			.keystream = keydata,
			.maskstream = mskdata,
			.actionstream = actdata,
		},
	};
	int ret;

	vcap_test_api_init(&admin);
	test_init_rule_deletion();

	/* Create rules with different sizes and check that they are placed
	 * at the correct address in the VCAP according to size
	 */
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 10, 500, 12, 780);
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 20, 400, 6, 774);
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 30, 300, 3, 771);
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 40, 200, 2, 768);

	/* Remove rules again from the end */
	ret = vcap_del_rule(&test_vctrl, &test_netdev, 200);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 0, test_move_addr);
	KUNIT_EXPECT_EQ(test, 0, test_move_offset);
	KUNIT_EXPECT_EQ(test, 0, test_move_count);
	KUNIT_EXPECT_EQ(test, 768, test_init_start);
	KUNIT_EXPECT_EQ(test, 2, test_init_count);
	KUNIT_EXPECT_EQ(test, 771, admin.last_used_addr);

	ret = vcap_del_rule(&test_vctrl, &test_netdev, 300);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, 0, test_move_addr);
	KUNIT_EXPECT_EQ(test, 0, test_move_offset);
	KUNIT_EXPECT_EQ(test, 0, test_move_count);
	KUNIT_EXPECT_EQ(test, 771, test_init_start);
	KUNIT_EXPECT_EQ(test, 3, test_init_count);
	KUNIT_EXPECT_EQ(test, 774, admin.last_used_addr);

	ret = vcap_del_rule(&test_vctrl, &test_netdev, 400);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, 0, test_move_addr);
	KUNIT_EXPECT_EQ(test, 0, test_move_offset);
	KUNIT_EXPECT_EQ(test, 0, test_move_count);
	KUNIT_EXPECT_EQ(test, 774, test_init_start);
	KUNIT_EXPECT_EQ(test, 6, test_init_count);
	KUNIT_EXPECT_EQ(test, 780, admin.last_used_addr);

	ret = vcap_del_rule(&test_vctrl, &test_netdev, 500);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, 0, test_move_addr);
	KUNIT_EXPECT_EQ(test, 0, test_move_offset);
	KUNIT_EXPECT_EQ(test, 0, test_move_count);
	KUNIT_EXPECT_EQ(test, 780, test_init_start);
	KUNIT_EXPECT_EQ(test, 12, test_init_count);
	KUNIT_EXPECT_EQ(test, 3072, admin.last_used_addr);
}

static void vcap_api_rule_remove_in_middle_test(struct kunit *test)
{
	/* Data used by VCAP Library callback */
	static u32 keydata[32] = {};
	static u32 mskdata[32] = {};
	static u32 actdata[32] = {};

	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS0,
		.first_cid = 10000,
		.last_cid = 19999,
		.lookups = 4,
		.first_valid_addr = 0,
		.last_used_addr = 800,
		.last_valid_addr = 800 - 1,
		.cache = {
			.keystream = keydata,
			.maskstream = mskdata,
			.actionstream = actdata,
		},
	};
	int ret;

	vcap_test_api_init(&admin);

	/* Create rules with different sizes and check that they are placed
	 * at the correct address in the VCAP according to size
	 */
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 10, 500, 12, 780);
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 20, 400, 6, 774);
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 30, 300, 3, 771);
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 40, 200, 2, 768);

	/* Remove rules in the middle */
	test_init_rule_deletion();
	ret = vcap_del_rule(&test_vctrl, &test_netdev, 400);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 768, test_move_addr);
	KUNIT_EXPECT_EQ(test, -6, test_move_offset);
	KUNIT_EXPECT_EQ(test, 6, test_move_count);
	KUNIT_EXPECT_EQ(test, 768, test_init_start);
	KUNIT_EXPECT_EQ(test, 6, test_init_count);
	KUNIT_EXPECT_EQ(test, 774, admin.last_used_addr);

	test_init_rule_deletion();
	ret = vcap_del_rule(&test_vctrl, &test_netdev, 300);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 774, test_move_addr);
	KUNIT_EXPECT_EQ(test, -4, test_move_offset);
	KUNIT_EXPECT_EQ(test, 2, test_move_count);
	KUNIT_EXPECT_EQ(test, 774, test_init_start);
	KUNIT_EXPECT_EQ(test, 4, test_init_count);
	KUNIT_EXPECT_EQ(test, 778, admin.last_used_addr);

	test_init_rule_deletion();
	ret = vcap_del_rule(&test_vctrl, &test_netdev, 500);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 778, test_move_addr);
	KUNIT_EXPECT_EQ(test, -20, test_move_offset);
	KUNIT_EXPECT_EQ(test, 2, test_move_count);
	KUNIT_EXPECT_EQ(test, 778, test_init_start);
	KUNIT_EXPECT_EQ(test, 20, test_init_count);
	KUNIT_EXPECT_EQ(test, 798, admin.last_used_addr);

	test_init_rule_deletion();
	ret = vcap_del_rule(&test_vctrl, &test_netdev, 200);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 0, test_move_addr);
	KUNIT_EXPECT_EQ(test, 0, test_move_offset);
	KUNIT_EXPECT_EQ(test, 0, test_move_count);
	KUNIT_EXPECT_EQ(test, 798, test_init_start);
	KUNIT_EXPECT_EQ(test, 2, test_init_count);
	KUNIT_EXPECT_EQ(test, 800, admin.last_used_addr);
}

static void vcap_api_rule_remove_in_front_test(struct kunit *test)
{
	/* Data used by VCAP Library callback */
	static u32 keydata[32] = {};
	static u32 mskdata[32] = {};
	static u32 actdata[32] = {};

	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS0,
		.first_cid = 10000,
		.last_cid = 19999,
		.lookups = 4,
		.first_valid_addr = 0,
		.last_used_addr = 800,
		.last_valid_addr = 800 - 1,
		.cache = {
			.keystream = keydata,
			.maskstream = mskdata,
			.actionstream = actdata,
		},
	};
	int ret;

	vcap_test_api_init(&admin);

	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 10, 500, 12, 780);
	KUNIT_EXPECT_EQ(test, 780, admin.last_used_addr);

	test_init_rule_deletion();
	ret = vcap_del_rule(&test_vctrl, &test_netdev, 500);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 0, test_move_addr);
	KUNIT_EXPECT_EQ(test, 0, test_move_offset);
	KUNIT_EXPECT_EQ(test, 0, test_move_count);
	KUNIT_EXPECT_EQ(test, 780, test_init_start);
	KUNIT_EXPECT_EQ(test, 12, test_init_count);
	KUNIT_EXPECT_EQ(test, 800, admin.last_used_addr);

	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 20, 400, 6, 792);
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 30, 300, 3, 789);
	test_vcap_xn_rule_creator(test, 10000, VCAP_USER_QOS, 40, 200, 2, 786);

	test_init_rule_deletion();
	ret = vcap_del_rule(&test_vctrl, &test_netdev, 400);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 786, test_move_addr);
	KUNIT_EXPECT_EQ(test, -8, test_move_offset);
	KUNIT_EXPECT_EQ(test, 6, test_move_count);
	KUNIT_EXPECT_EQ(test, 786, test_init_start);
	KUNIT_EXPECT_EQ(test, 8, test_init_count);
	KUNIT_EXPECT_EQ(test, 794, admin.last_used_addr);

	vcap_del_rule(&test_vctrl, &test_netdev, 200);
	vcap_del_rule(&test_vctrl, &test_netdev, 300);
}

static struct kunit_case vcap_api_rule_remove_test_cases[] = {
	KUNIT_CASE(vcap_api_rule_remove_at_end_test),
	KUNIT_CASE(vcap_api_rule_remove_in_middle_test),
	KUNIT_CASE(vcap_api_rule_remove_in_front_test),
	{}
};

static void vcap_api_next_lookup_basic_test(struct kunit *test)
{
	struct vcap_admin admin1 = {
		.vtype = VCAP_TYPE_IS2,
		.vinst = 0,
		.first_cid = 8000000,
		.last_cid = 8199999,
		.lookups = 4,
		.lookups_per_instance = 2,
	};
	struct vcap_admin admin2 = {
		.vtype = VCAP_TYPE_IS2,
		.vinst = 1,
		.first_cid = 8200000,
		.last_cid = 8399999,
		.lookups = 4,
		.lookups_per_instance = 2,
	};
	bool ret;

	vcap_test_api_init(&admin1);
	list_add_tail(&admin2.list, &test_vctrl.list);

	ret = vcap_is_next_lookup(&test_vctrl, 8000000, 1001000);
	KUNIT_EXPECT_EQ(test, false, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 8000000, 8001000);
	KUNIT_EXPECT_EQ(test, false, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 8000000, 8101000);
	KUNIT_EXPECT_EQ(test, true, ret);

	ret = vcap_is_next_lookup(&test_vctrl, 8100000, 8101000);
	KUNIT_EXPECT_EQ(test, false, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 8100000, 8201000);
	KUNIT_EXPECT_EQ(test, true, ret);

	ret = vcap_is_next_lookup(&test_vctrl, 8200000, 8201000);
	KUNIT_EXPECT_EQ(test, false, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 8200000, 8301000);
	KUNIT_EXPECT_EQ(test, true, ret);

	ret = vcap_is_next_lookup(&test_vctrl, 8300000, 8301000);
	KUNIT_EXPECT_EQ(test, false, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 8300000, 8401000);
	KUNIT_EXPECT_EQ(test, false, ret);
}

static void vcap_api_next_lookup_advanced_test(struct kunit *test)
{
	struct vcap_admin admin[] = {
	{
		.vtype = VCAP_TYPE_IS0,
		.vinst = 0,
		.first_cid = 1000000,
		.last_cid =  1199999,
		.lookups = 6,
		.lookups_per_instance = 2,
	}, {
		.vtype = VCAP_TYPE_IS0,
		.vinst = 1,
		.first_cid = 1200000,
		.last_cid =  1399999,
		.lookups = 6,
		.lookups_per_instance = 2,
	}, {
		.vtype = VCAP_TYPE_IS0,
		.vinst = 2,
		.first_cid = 1400000,
		.last_cid =  1599999,
		.lookups = 6,
		.lookups_per_instance = 2,
	}, {
		.vtype = VCAP_TYPE_IS2,
		.vinst = 0,
		.first_cid = 8000000,
		.last_cid = 8199999,
		.lookups = 4,
		.lookups_per_instance = 2,
	}, {
		.vtype = VCAP_TYPE_IS2,
		.vinst = 1,
		.first_cid = 8200000,
		.last_cid = 8399999,
		.lookups = 4,
		.lookups_per_instance = 2,
	}
	};
	bool ret;

	vcap_test_api_init(&admin[0]);
	list_add_tail(&admin[1].list, &test_vctrl.list);
	list_add_tail(&admin[2].list, &test_vctrl.list);
	list_add_tail(&admin[3].list, &test_vctrl.list);
	list_add_tail(&admin[4].list, &test_vctrl.list);

	ret = vcap_is_next_lookup(&test_vctrl, 1000000, 1001000);
	KUNIT_EXPECT_EQ(test, false, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 1000000, 1101000);
	KUNIT_EXPECT_EQ(test, true, ret);

	ret = vcap_is_next_lookup(&test_vctrl, 1100000, 1201000);
	KUNIT_EXPECT_EQ(test, true, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 1100000, 1301000);
	KUNIT_EXPECT_EQ(test, true, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 1100000, 8101000);
	KUNIT_EXPECT_EQ(test, true, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 1300000, 1401000);
	KUNIT_EXPECT_EQ(test, true, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 1400000, 1501000);
	KUNIT_EXPECT_EQ(test, true, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 1500000, 8001000);
	KUNIT_EXPECT_EQ(test, true, ret);

	ret = vcap_is_next_lookup(&test_vctrl, 8000000, 8001000);
	KUNIT_EXPECT_EQ(test, false, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 8000000, 8101000);
	KUNIT_EXPECT_EQ(test, true, ret);

	ret = vcap_is_next_lookup(&test_vctrl, 8300000, 8301000);
	KUNIT_EXPECT_EQ(test, false, ret);
	ret = vcap_is_next_lookup(&test_vctrl, 8300000, 8401000);
	KUNIT_EXPECT_EQ(test, false, ret);
}

static void vcap_api_filter_unsupported_keys_test(struct kunit *test)
{
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
	};
	struct vcap_rule_internal ri = {
		.admin = &admin,
		.vctrl = &test_vctrl,
		.data.keyset = VCAP_KFS_MAC_ETYPE,
	};
	enum vcap_key_field keylist[] = {
		VCAP_KF_TYPE,
		VCAP_KF_LOOKUP_FIRST_IS,
		VCAP_KF_ARP_ADDR_SPACE_OK_IS,  /* arp keys are not in keyset */
		VCAP_KF_ARP_PROTO_SPACE_OK_IS,
		VCAP_KF_ARP_LEN_OK_IS,
		VCAP_KF_ARP_TGT_MATCH_IS,
		VCAP_KF_ARP_SENDER_MATCH_IS,
		VCAP_KF_ARP_OPCODE_UNKNOWN_IS,
		VCAP_KF_ARP_OPCODE,
		VCAP_KF_8021Q_DEI_CLS,
		VCAP_KF_8021Q_PCP_CLS,
		VCAP_KF_8021Q_VID_CLS,
		VCAP_KF_L2_MC_IS,
		VCAP_KF_L2_BC_IS,
	};
	enum vcap_key_field expected[] = {
		VCAP_KF_TYPE,
		VCAP_KF_LOOKUP_FIRST_IS,
		VCAP_KF_8021Q_DEI_CLS,
		VCAP_KF_8021Q_PCP_CLS,
		VCAP_KF_8021Q_VID_CLS,
		VCAP_KF_L2_MC_IS,
		VCAP_KF_L2_BC_IS,
	};
	struct vcap_client_keyfield *ckf, *next;
	bool ret;
	int idx;

	/* Add all keys to the rule */
	INIT_LIST_HEAD(&ri.data.keyfields);
	for (idx = 0; idx < ARRAY_SIZE(keylist); idx++) {
		ckf = kzalloc(sizeof(*ckf), GFP_KERNEL);
		if (ckf) {
			ckf->ctrl.key = keylist[idx];
			list_add_tail(&ckf->ctrl.list, &ri.data.keyfields);
		}
	}

	KUNIT_EXPECT_EQ(test, 14, ARRAY_SIZE(keylist));

	/* Drop unsupported keys from the rule */
	ret = vcap_filter_rule_keys(&ri.data, NULL, 0, true);

	KUNIT_EXPECT_EQ(test, 0, ret);

	/* Check remaining keys in the rule */
	idx = 0;
	list_for_each_entry_safe(ckf, next, &ri.data.keyfields, ctrl.list) {
		KUNIT_EXPECT_EQ(test, expected[idx], ckf->ctrl.key);
		list_del(&ckf->ctrl.list);
		kfree(ckf);
		++idx;
	}
	KUNIT_EXPECT_EQ(test, 7, idx);
}

static void vcap_api_filter_keylist_test(struct kunit *test)
{
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS0,
	};
	struct vcap_rule_internal ri = {
		.admin = &admin,
		.vctrl = &test_vctrl,
		.data.keyset = VCAP_KFS_NORMAL_7TUPLE,
	};
	enum vcap_key_field keylist[] = {
		VCAP_KF_TYPE,
		VCAP_KF_LOOKUP_FIRST_IS,
		VCAP_KF_LOOKUP_GEN_IDX_SEL,
		VCAP_KF_LOOKUP_GEN_IDX,
		VCAP_KF_IF_IGR_PORT_MASK_SEL,
		VCAP_KF_IF_IGR_PORT_MASK,
		VCAP_KF_L2_MC_IS,
		VCAP_KF_L2_BC_IS,
		VCAP_KF_8021Q_VLAN_TAGS,
		VCAP_KF_8021Q_TPID0,
		VCAP_KF_8021Q_PCP0,
		VCAP_KF_8021Q_DEI0,
		VCAP_KF_8021Q_VID0,
		VCAP_KF_8021Q_TPID1,
		VCAP_KF_8021Q_PCP1,
		VCAP_KF_8021Q_DEI1,
		VCAP_KF_8021Q_VID1,
		VCAP_KF_8021Q_TPID2,
		VCAP_KF_8021Q_PCP2,
		VCAP_KF_8021Q_DEI2,
		VCAP_KF_8021Q_VID2,
		VCAP_KF_L2_DMAC,
		VCAP_KF_L2_SMAC,
		VCAP_KF_IP_MC_IS,
		VCAP_KF_ETYPE_LEN_IS,
		VCAP_KF_ETYPE,
		VCAP_KF_IP_SNAP_IS,
		VCAP_KF_IP4_IS,
		VCAP_KF_L3_FRAGMENT_TYPE,
		VCAP_KF_L3_FRAG_INVLD_L4_LEN,
		VCAP_KF_L3_OPTIONS_IS,
		VCAP_KF_L3_DSCP,
		VCAP_KF_L3_IP6_DIP,
		VCAP_KF_L3_IP6_SIP,
		VCAP_KF_TCP_UDP_IS,
		VCAP_KF_TCP_IS,
		VCAP_KF_L4_SPORT,
		VCAP_KF_L4_RNG,
	};
	enum vcap_key_field droplist[] = {
		VCAP_KF_8021Q_TPID1,
		VCAP_KF_8021Q_PCP1,
		VCAP_KF_8021Q_DEI1,
		VCAP_KF_8021Q_VID1,
		VCAP_KF_8021Q_TPID2,
		VCAP_KF_8021Q_PCP2,
		VCAP_KF_8021Q_DEI2,
		VCAP_KF_8021Q_VID2,
		VCAP_KF_L3_IP6_DIP,
		VCAP_KF_L3_IP6_SIP,
		VCAP_KF_L4_SPORT,
		VCAP_KF_L4_RNG,
	};
	enum vcap_key_field expected[] = {
		VCAP_KF_TYPE,
		VCAP_KF_LOOKUP_FIRST_IS,
		VCAP_KF_LOOKUP_GEN_IDX_SEL,
		VCAP_KF_LOOKUP_GEN_IDX,
		VCAP_KF_IF_IGR_PORT_MASK_SEL,
		VCAP_KF_IF_IGR_PORT_MASK,
		VCAP_KF_L2_MC_IS,
		VCAP_KF_L2_BC_IS,
		VCAP_KF_8021Q_VLAN_TAGS,
		VCAP_KF_8021Q_TPID0,
		VCAP_KF_8021Q_PCP0,
		VCAP_KF_8021Q_DEI0,
		VCAP_KF_8021Q_VID0,
		VCAP_KF_L2_DMAC,
		VCAP_KF_L2_SMAC,
		VCAP_KF_IP_MC_IS,
		VCAP_KF_ETYPE_LEN_IS,
		VCAP_KF_ETYPE,
		VCAP_KF_IP_SNAP_IS,
		VCAP_KF_IP4_IS,
		VCAP_KF_L3_FRAGMENT_TYPE,
		VCAP_KF_L3_FRAG_INVLD_L4_LEN,
		VCAP_KF_L3_OPTIONS_IS,
		VCAP_KF_L3_DSCP,
		VCAP_KF_TCP_UDP_IS,
		VCAP_KF_TCP_IS,
	};
	struct vcap_client_keyfield *ckf, *next;
	bool ret;
	int idx;

	/* Add all keys to the rule */
	INIT_LIST_HEAD(&ri.data.keyfields);
	for (idx = 0; idx < ARRAY_SIZE(keylist); idx++) {
		ckf = kzalloc(sizeof(*ckf), GFP_KERNEL);
		if (ckf) {
			ckf->ctrl.key = keylist[idx];
			list_add_tail(&ckf->ctrl.list, &ri.data.keyfields);
		}
	}

	KUNIT_EXPECT_EQ(test, 38, ARRAY_SIZE(keylist));

	/* Drop listed keys from the rule */
	ret = vcap_filter_rule_keys(&ri.data, droplist, ARRAY_SIZE(droplist),
				    false);

	KUNIT_EXPECT_EQ(test, 0, ret);

	/* Check remaining keys in the rule */
	idx = 0;
	list_for_each_entry_safe(ckf, next, &ri.data.keyfields, ctrl.list) {
		KUNIT_EXPECT_EQ(test, expected[idx], ckf->ctrl.key);
		list_del(&ckf->ctrl.list);
		kfree(ckf);
		++idx;
	}
	KUNIT_EXPECT_EQ(test, 26, idx);
}

static void vcap_api_rule_chain_path_test(struct kunit *test)
{
	struct vcap_admin admin1 = {
		.vtype = VCAP_TYPE_IS0,
		.vinst = 0,
		.first_cid = 1000000,
		.last_cid =  1199999,
		.lookups = 6,
		.lookups_per_instance = 2,
	};
	struct vcap_enabled_port eport3 = {
		.ndev = &test_netdev,
		.cookie = 0x100,
		.src_cid = 0,
		.dst_cid = 1000000,
	};
	struct vcap_enabled_port eport2 = {
		.ndev = &test_netdev,
		.cookie = 0x200,
		.src_cid = 1000000,
		.dst_cid = 1100000,
	};
	struct vcap_enabled_port eport1 = {
		.ndev = &test_netdev,
		.cookie = 0x300,
		.src_cid = 1100000,
		.dst_cid = 8000000,
	};
	bool ret;
	int chain;

	vcap_test_api_init(&admin1);
	list_add_tail(&eport1.list, &admin1.enabled);
	list_add_tail(&eport2.list, &admin1.enabled);
	list_add_tail(&eport3.list, &admin1.enabled);

	ret = vcap_path_exist(&test_vctrl, &test_netdev, 1000000);
	KUNIT_EXPECT_EQ(test, true, ret);

	ret = vcap_path_exist(&test_vctrl, &test_netdev, 1100000);
	KUNIT_EXPECT_EQ(test, true, ret);

	ret = vcap_path_exist(&test_vctrl, &test_netdev, 1200000);
	KUNIT_EXPECT_EQ(test, false, ret);

	chain = vcap_get_next_chain(&test_vctrl, &test_netdev, 0);
	KUNIT_EXPECT_EQ(test, 1000000, chain);

	chain = vcap_get_next_chain(&test_vctrl, &test_netdev, 1000000);
	KUNIT_EXPECT_EQ(test, 1100000, chain);

	chain = vcap_get_next_chain(&test_vctrl, &test_netdev, 1100000);
	KUNIT_EXPECT_EQ(test, 8000000, chain);
}

static struct kunit_case vcap_api_rule_enable_test_cases[] = {
	KUNIT_CASE(vcap_api_rule_chain_path_test),
	{}
};

static struct kunit_suite vcap_api_rule_enable_test_suite = {
	.name = "VCAP_API_Rule_Enable_Testsuite",
	.test_cases = vcap_api_rule_enable_test_cases,
};

static struct kunit_suite vcap_api_rule_remove_test_suite = {
	.name = "VCAP_API_Rule_Remove_Testsuite",
	.test_cases = vcap_api_rule_remove_test_cases,
};

static struct kunit_case vcap_api_rule_insert_test_cases[] = {
	KUNIT_CASE(vcap_api_rule_insert_in_order_test),
	KUNIT_CASE(vcap_api_rule_insert_reverse_order_test),
	{}
};

static struct kunit_suite vcap_api_rule_insert_test_suite = {
	.name = "VCAP_API_Rule_Insert_Testsuite",
	.test_cases = vcap_api_rule_insert_test_cases,
};

static struct kunit_case vcap_api_rule_counter_test_cases[] = {
	KUNIT_CASE(vcap_api_set_rule_counter_test),
	KUNIT_CASE(vcap_api_get_rule_counter_test),
	{}
};

static struct kunit_suite vcap_api_rule_counter_test_suite = {
	.name = "VCAP_API_Rule_Counter_Testsuite",
	.test_cases = vcap_api_rule_counter_test_cases,
};

static struct kunit_case vcap_api_support_test_cases[] = {
	KUNIT_CASE(vcap_api_next_lookup_basic_test),
	KUNIT_CASE(vcap_api_next_lookup_advanced_test),
	KUNIT_CASE(vcap_api_filter_unsupported_keys_test),
	KUNIT_CASE(vcap_api_filter_keylist_test),
	{}
};

static struct kunit_suite vcap_api_support_test_suite = {
	.name = "VCAP_API_Support_Testsuite",
	.test_cases = vcap_api_support_test_cases,
};

static struct kunit_case vcap_api_full_rule_test_cases[] = {
	KUNIT_CASE(vcap_api_rule_find_keyset_basic_test),
	KUNIT_CASE(vcap_api_rule_find_keyset_failed_test),
	KUNIT_CASE(vcap_api_rule_find_keyset_many_test),
	KUNIT_CASE(vcap_api_encode_rule_test),
	{}
};

static struct kunit_suite vcap_api_full_rule_test_suite = {
	.name = "VCAP_API_Full_Rule_Testsuite",
	.test_cases = vcap_api_full_rule_test_cases,
};

static struct kunit_case vcap_api_rule_value_test_cases[] = {
	KUNIT_CASE(vcap_api_rule_add_keyvalue_test),
	KUNIT_CASE(vcap_api_rule_add_actionvalue_test),
	{}
};

static struct kunit_suite vcap_api_rule_value_test_suite = {
	.name = "VCAP_API_Rule_Value_Testsuite",
	.test_cases = vcap_api_rule_value_test_cases,
};

static struct kunit_case vcap_api_encoding_test_cases[] = {
	KUNIT_CASE(vcap_api_set_bit_1_test),
	KUNIT_CASE(vcap_api_set_bit_0_test),
	KUNIT_CASE(vcap_api_iterator_init_test),
	KUNIT_CASE(vcap_api_iterator_next_test),
	KUNIT_CASE(vcap_api_encode_typegroups_test),
	KUNIT_CASE(vcap_api_encode_bit_test),
	KUNIT_CASE(vcap_api_encode_field_test),
	KUNIT_CASE(vcap_api_encode_short_field_test),
	KUNIT_CASE(vcap_api_encode_keyfield_test),
	KUNIT_CASE(vcap_api_encode_max_keyfield_test),
	KUNIT_CASE(vcap_api_encode_actionfield_test),
	KUNIT_CASE(vcap_api_keyfield_typegroup_test),
	KUNIT_CASE(vcap_api_actionfield_typegroup_test),
	KUNIT_CASE(vcap_api_vcap_keyfields_test),
	KUNIT_CASE(vcap_api_vcap_actionfields_test),
	KUNIT_CASE(vcap_api_encode_rule_keyset_test),
	KUNIT_CASE(vcap_api_encode_rule_actionset_test),
	{}
};

static struct kunit_suite vcap_api_encoding_test_suite = {
	.name = "VCAP_API_Encoding_Testsuite",
	.test_cases = vcap_api_encoding_test_cases,
};

kunit_test_suite(vcap_api_rule_enable_test_suite);
kunit_test_suite(vcap_api_rule_remove_test_suite);
kunit_test_suite(vcap_api_rule_insert_test_suite);
kunit_test_suite(vcap_api_rule_counter_test_suite);
kunit_test_suite(vcap_api_support_test_suite);
kunit_test_suite(vcap_api_full_rule_test_suite);
kunit_test_suite(vcap_api_rule_value_test_suite);
kunit_test_suite(vcap_api_encoding_test_suite);
