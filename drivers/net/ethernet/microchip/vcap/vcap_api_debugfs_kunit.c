// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (C) 2022 Microchip Technology Inc. and its subsidiaries.
 * Microchip VCAP API kunit test suite
 */

#include <kunit/test.h>
#include "vcap_api.h"
#include "vcap_api_client.h"
#include "vcap_api_debugfs.h"
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
static char test_pr_buffer[TEST_BUF_CNT][TEST_BUF_SZ];
static int test_pr_bufferidx;
static int test_pr_idx;

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
				if (kslist->keysets[idx] ==
				    VCAP_KFS_PURE_5TUPLE_IP4)
					return kslist->keysets[idx];
				if (kslist->keysets[idx] ==
				    VCAP_KFS_NORMAL_5TUPLE_IP4)
					return kslist->keysets[idx];
				if (kslist->keysets[idx] ==
				    VCAP_KFS_NORMAL_7TUPLE)
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
		vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS,
				      VCAP_BIT_1);
	else
		vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS,
				      VCAP_BIT_0);
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
		pr_err("%s:%d: overflow: %d\n", __func__, __LINE__,
		       test_updateaddridx);
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

static int vcap_test_enable(struct net_device *ndev,
			    struct vcap_admin *admin,
			    bool enable)
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
	.enable = vcap_test_enable,
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
	test_pr_bufferidx = 0;
	test_pr_idx = 0;
}

/* callback used by the show_admin function */
static __printf(2, 3)
int test_prf(void *out, const char *fmt, ...)
{
	static char test_buffer[TEST_BUF_SZ];
	va_list args;
	int idx, cnt;

	if (test_pr_bufferidx >= TEST_BUF_CNT) {
		pr_err("%s:%d: overflow: %d\n", __func__, __LINE__,
		       test_pr_bufferidx);
		return 0;
	}

	va_start(args, fmt);
	cnt = vscnprintf(test_buffer, TEST_BUF_SZ, fmt, args);
	va_end(args);

	for (idx = 0; idx < cnt; ++idx) {
		test_pr_buffer[test_pr_bufferidx][test_pr_idx] =
			test_buffer[idx];
		if (test_buffer[idx] == '\n') {
			test_pr_buffer[test_pr_bufferidx][++test_pr_idx] = 0;
			test_pr_idx = 0;
			test_pr_bufferidx++;
		} else {
			++test_pr_idx;
		}
	}

	return cnt;
}

/* Define the test cases. */

static void vcap_api_addr_keyset_test(struct kunit *test)
{
	u32 keydata[12] = {
		0x40450042, 0x000feaf3, 0x00000003, 0x00050600,
		0x10203040, 0x00075880, 0x633c6864, 0x00040003,
		0x00000020, 0x00000008, 0x00000240, 0x00000000,
	};
	u32 mskdata[12] = {
		0x0030ff80, 0xfff00000, 0xfffffffc, 0xfff000ff,
		0x00000000, 0xfff00000, 0x00000000, 0xfff3fffc,
		0xffffffc0, 0xffffffff, 0xfffffc03, 0xffffffff,
	};
	u32 actdata[12] = {};
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
		.cache = {
			.keystream = keydata,
			.maskstream = mskdata,
			.actionstream = actdata,
		},
	};
	enum vcap_keyfield_set keysets[10];
	struct vcap_keyset_list matches;
	int ret, idx, addr;

	vcap_test_api_init(&admin);

	/* Go from higher to lower addresses searching for a keyset */
	matches.keysets = keysets;
	matches.cnt = 0;
	matches.max = ARRAY_SIZE(keysets);
	for (idx = ARRAY_SIZE(keydata) - 1, addr = 799; idx > 0;
	     --idx, --addr) {
		admin.cache.keystream = &keydata[idx];
		admin.cache.maskstream = &mskdata[idx];
		ret = vcap_addr_keysets(&test_vctrl, &test_netdev, &admin,
					addr, &matches);
		KUNIT_EXPECT_EQ(test, -EINVAL, ret);
	}

	/* Finally we hit the start of the rule */
	admin.cache.keystream = &keydata[idx];
	admin.cache.maskstream = &mskdata[idx];
	matches.cnt = 0;
	ret = vcap_addr_keysets(&test_vctrl, &test_netdev, &admin,
				addr, &matches);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, matches.cnt, 1);
	KUNIT_EXPECT_EQ(test, matches.keysets[0], VCAP_KFS_MAC_ETYPE);
}

static void vcap_api_show_admin_raw_test(struct kunit *test)
{
	u32 keydata[4] = {
		0x40450042, 0x000feaf3, 0x00000003, 0x00050600,
	};
	u32 mskdata[4] = {
		0x0030ff80, 0xfff00000, 0xfffffffc, 0xfff000ff,
	};
	u32 actdata[12] = {};
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
		.cache = {
			.keystream = keydata,
			.maskstream = mskdata,
			.actionstream = actdata,
		},
		.first_valid_addr = 786,
		.last_valid_addr = 788,
	};
	struct vcap_rule_internal ri = {
		.ndev = &test_netdev,
	};
	struct vcap_output_print out = {
		.prf = (void *)test_prf,
	};
	const char *test_expected =
		"  addr: 786, X6 rule, keysets: VCAP_KFS_MAC_ETYPE\n";
	int ret;

	vcap_test_api_init(&admin);
	list_add_tail(&ri.list, &admin.rules);

	ret = vcap_show_admin_raw(&test_vctrl, &admin, &out);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_STREQ(test, test_expected, test_pr_buffer[0]);
}

static const char * const test_admin_info_expect[] = {
	"name: is2\n",
	"rows: 256\n",
	"sw_count: 12\n",
	"sw_width: 52\n",
	"sticky_width: 1\n",
	"act_width: 110\n",
	"default_cnt: 73\n",
	"require_cnt_dis: 0\n",
	"version: 1\n",
	"vtype: 2\n",
	"vinst: 0\n",
	"first_cid: 10000\n",
	"last_cid: 19999\n",
	"lookups: 4\n",
	"first_valid_addr: 0\n",
	"last_valid_addr: 3071\n",
	"last_used_addr: 794\n",
};

static void vcap_api_show_admin_test(struct kunit *test)
{
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
		.first_cid = 10000,
		.last_cid = 19999,
		.lookups = 4,
		.last_valid_addr = 3071,
		.first_valid_addr = 0,
		.last_used_addr = 794,
	};
	struct vcap_output_print out = {
		.prf = (void *)test_prf,
	};
	int idx;

	vcap_test_api_init(&admin);

	vcap_show_admin_info(&test_vctrl, &admin, &out);
	for (idx = 0; idx < test_pr_bufferidx; ++idx) {
		/* pr_info("log[%02d]: %s", idx, test_pr_buffer[idx]); */
		KUNIT_EXPECT_STREQ(test, test_admin_info_expect[idx],
				   test_pr_buffer[idx]);
	}
}

static const char * const test_admin_expect[] = {
	"name: is2\n",
	"rows: 256\n",
	"sw_count: 12\n",
	"sw_width: 52\n",
	"sticky_width: 1\n",
	"act_width: 110\n",
	"default_cnt: 73\n",
	"require_cnt_dis: 0\n",
	"version: 1\n",
	"vtype: 2\n",
	"vinst: 0\n",
	"first_cid: 8000000\n",
	"last_cid: 8199999\n",
	"lookups: 4\n",
	"first_valid_addr: 0\n",
	"last_valid_addr: 3071\n",
	"last_used_addr: 794\n",
	"\n",
	"rule: 100, addr: [794,799], X6, ctr[0]: 0, hit: 0\n",
	"  chain_id: 0\n",
	"  user: 0\n",
	"  priority: 0\n",
	"  keysets: VCAP_KFS_MAC_ETYPE\n",
	"  keyset_sw: 6\n",
	"  keyset_sw_regs: 2\n",
	"    ETYPE_LEN_IS: W1: 1/1\n",
	"    IF_IGR_PORT_MASK: W32: 0xffabcd01/0xffffffff\n",
	"    IF_IGR_PORT_MASK_RNG: W4: 5/15\n",
	"    L2_DMAC: W48: 01:02:03:04:05:06/ff:ff:ff:ff:ff:ff\n",
	"    L2_PAYLOAD_ETYPE: W64: 0x9000002000000081/0xff000000000000ff\n",
	"    L2_SMAC: W48: b1:9e:34:32:75:88/ff:ff:ff:ff:ff:ff\n",
	"    LOOKUP_FIRST_IS: W1: 1/1\n",
	"    TYPE: W4: 0/15\n",
	"  actionset: VCAP_AFS_BASE_TYPE\n",
	"  actionset_sw: 3\n",
	"  actionset_sw_regs: 4\n",
	"    CNT_ID: W12: 100\n",
	"    MATCH_ID: W16: 1\n",
	"    MATCH_ID_MASK: W16: 1\n",
	"    POLICE_ENA: W1: 1\n",
	"    PORT_MASK: W68: 0x0514670115f3324589\n",
};

static void vcap_api_show_admin_rule_test(struct kunit *test)
{
	u32 keydata[] = {
		0x40450042, 0x000feaf3, 0x00000003, 0x00050600,
		0x10203040, 0x00075880, 0x633c6864, 0x00040003,
		0x00000020, 0x00000008, 0x00000240, 0x00000000,
	};
	u32 mskdata[] = {
		0x0030ff80, 0xfff00000, 0xfffffffc, 0xfff000ff,
		0x00000000, 0xfff00000, 0x00000000, 0xfff3fffc,
		0xffffffc0, 0xffffffff, 0xfffffc03, 0xffffffff,
	};
	u32 actdata[] = {
		0x00040002, 0xf3324589, 0x14670115, 0x00000005,
		0x00000000, 0x00100000, 0x06400010, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
	};
	struct vcap_admin admin = {
		.vtype = VCAP_TYPE_IS2,
		.first_cid = 8000000,
		.last_cid = 8199999,
		.lookups = 4,
		.last_valid_addr = 3071,
		.first_valid_addr = 0,
		.last_used_addr = 794,
		.cache = {
			.keystream = keydata,
			.maskstream = mskdata,
			.actionstream = actdata,
		},
	};
	struct vcap_rule_internal ri = {
		.admin = &admin,
		.data = {
			.id = 100,
			.keyset = VCAP_KFS_MAC_ETYPE,
			.actionset = VCAP_AFS_BASE_TYPE,
		},
		.size = 6,
		.keyset_sw = 6,
		.keyset_sw_regs = 2,
		.actionset_sw = 3,
		.actionset_sw_regs = 4,
		.addr = 794,
		.vctrl = &test_vctrl,
	};
	struct vcap_output_print out = {
		.prf = (void *)test_prf,
	};
	int ret, idx;

	vcap_test_api_init(&admin);
	list_add_tail(&ri.list, &admin.rules);

	ret = vcap_show_admin(&test_vctrl, &admin, &out);
	KUNIT_EXPECT_EQ(test, 0, ret);
	for (idx = 0; idx < test_pr_bufferidx; ++idx) {
		/* pr_info("log[%02d]: %s", idx, test_pr_buffer[idx]); */
		KUNIT_EXPECT_STREQ(test, test_admin_expect[idx],
				   test_pr_buffer[idx]);
	}
}

static struct kunit_case vcap_api_debugfs_test_cases[] = {
	KUNIT_CASE(vcap_api_addr_keyset_test),
	KUNIT_CASE(vcap_api_show_admin_raw_test),
	KUNIT_CASE(vcap_api_show_admin_test),
	KUNIT_CASE(vcap_api_show_admin_rule_test),
	{}
};

static struct kunit_suite vcap_api_debugfs_test_suite = {
	.name = "VCAP_API_DebugFS_Testsuite",
	.test_cases = vcap_api_debugfs_test_cases,
};

kunit_test_suite(vcap_api_debugfs_test_suite);
