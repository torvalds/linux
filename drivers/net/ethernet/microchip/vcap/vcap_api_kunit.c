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
}

/* Provide port information via a callback interface */
static int vcap_test_port_info(struct net_device *ndev, enum vcap_type vtype,
			       int (*pf)(void *out, int arg, const char *fmt, ...),
			       void *out, int arg)
{
	return 0;
}

static struct vcap_operations test_callbacks = {
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
	list_add_tail(&admin->list, &test_vctrl.list);
	memset(test_updateaddr, 0, sizeof(test_updateaddr));
	test_updateaddridx = 0;
}

/* Define the test cases. */

static void vcap_api_set_bit_1_test(struct kunit *test)
{
	struct vcap_stream_iter iter = {
		.offset = 35,
		.sw_width = 52,
		.reg_idx = 1,
		.reg_bitpos = 20,
		.tg = 0
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
		.tg = 0
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

kunit_test_suite(vcap_api_encoding_test_suite);
