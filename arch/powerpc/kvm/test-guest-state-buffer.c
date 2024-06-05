// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/init.h>
#include <linux/log2.h>
#include <kunit/test.h>

#include <asm/guest-state-buffer.h>

static void test_creating_buffer(struct kunit *test)
{
	struct kvmppc_gs_buff *gsb;
	size_t size = 0x100;

	gsb = kvmppc_gsb_new(size, 0, 0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gsb);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gsb->hdr);

	KUNIT_EXPECT_EQ(test, gsb->capacity, roundup_pow_of_two(size));
	KUNIT_EXPECT_EQ(test, gsb->len, sizeof(__be32));

	kvmppc_gsb_free(gsb);
}

static void test_adding_element(struct kunit *test)
{
	const struct kvmppc_gs_elem *head, *curr;
	union {
		__vector128 v;
		u64 dw[2];
	} u;
	int rem;
	struct kvmppc_gs_buff *gsb;
	size_t size = 0x1000;
	int i, rc;
	u64 data;

	gsb = kvmppc_gsb_new(size, 0, 0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gsb);

	/* Single elements, direct use of __kvmppc_gse_put() */
	data = 0xdeadbeef;
	rc = __kvmppc_gse_put(gsb, KVMPPC_GSID_GPR(0), 8, &data);
	KUNIT_EXPECT_GE(test, rc, 0);

	head = kvmppc_gsb_data(gsb);
	KUNIT_EXPECT_EQ(test, kvmppc_gse_iden(head), KVMPPC_GSID_GPR(0));
	KUNIT_EXPECT_EQ(test, kvmppc_gse_len(head), 8);
	data = 0;
	memcpy(&data, kvmppc_gse_data(head), 8);
	KUNIT_EXPECT_EQ(test, data, 0xdeadbeef);

	/* Multiple elements, simple wrapper */
	rc = kvmppc_gse_put_u64(gsb, KVMPPC_GSID_GPR(1), 0xcafef00d);
	KUNIT_EXPECT_GE(test, rc, 0);

	u.dw[0] = 0x1;
	u.dw[1] = 0x2;
	rc = kvmppc_gse_put_vector128(gsb, KVMPPC_GSID_VSRS(0), &u.v);
	KUNIT_EXPECT_GE(test, rc, 0);
	u.dw[0] = 0x0;
	u.dw[1] = 0x0;

	kvmppc_gsb_for_each_elem(i, curr, gsb, rem) {
		switch (i) {
		case 0:
			KUNIT_EXPECT_EQ(test, kvmppc_gse_iden(curr),
					KVMPPC_GSID_GPR(0));
			KUNIT_EXPECT_EQ(test, kvmppc_gse_len(curr), 8);
			KUNIT_EXPECT_EQ(test, kvmppc_gse_get_be64(curr),
					0xdeadbeef);
			break;
		case 1:
			KUNIT_EXPECT_EQ(test, kvmppc_gse_iden(curr),
					KVMPPC_GSID_GPR(1));
			KUNIT_EXPECT_EQ(test, kvmppc_gse_len(curr), 8);
			KUNIT_EXPECT_EQ(test, kvmppc_gse_get_u64(curr),
					0xcafef00d);
			break;
		case 2:
			KUNIT_EXPECT_EQ(test, kvmppc_gse_iden(curr),
					KVMPPC_GSID_VSRS(0));
			KUNIT_EXPECT_EQ(test, kvmppc_gse_len(curr), 16);
			kvmppc_gse_get_vector128(curr, &u.v);
			KUNIT_EXPECT_EQ(test, u.dw[0], 0x1);
			KUNIT_EXPECT_EQ(test, u.dw[1], 0x2);
			break;
		}
	}
	KUNIT_EXPECT_EQ(test, i, 3);

	kvmppc_gsb_reset(gsb);
	KUNIT_EXPECT_EQ(test, kvmppc_gsb_nelems(gsb), 0);
	KUNIT_EXPECT_EQ(test, kvmppc_gsb_len(gsb),
			sizeof(struct kvmppc_gs_header));

	kvmppc_gsb_free(gsb);
}

static void test_gs_parsing(struct kunit *test)
{
	struct kvmppc_gs_elem *gse;
	struct kvmppc_gs_parser gsp = { 0 };
	struct kvmppc_gs_buff *gsb;
	size_t size = 0x1000;
	u64 tmp1, tmp2;

	gsb = kvmppc_gsb_new(size, 0, 0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gsb);

	tmp1 = 0xdeadbeefull;
	kvmppc_gse_put_u64(gsb, KVMPPC_GSID_GPR(0), tmp1);

	KUNIT_EXPECT_GE(test, kvmppc_gse_parse(&gsp, gsb), 0);

	gse = kvmppc_gsp_lookup(&gsp, KVMPPC_GSID_GPR(0));
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gse);

	tmp2 = kvmppc_gse_get_u64(gse);
	KUNIT_EXPECT_EQ(test, tmp2, 0xdeadbeefull);

	kvmppc_gsb_free(gsb);
}

static void test_gs_bitmap(struct kunit *test)
{
	struct kvmppc_gs_bitmap gsbm = { 0 };
	struct kvmppc_gs_bitmap gsbm1 = { 0 };
	struct kvmppc_gs_bitmap gsbm2 = { 0 };
	u16 iden;
	int i, j;

	i = 0;
	for (u16 iden = KVMPPC_GSID_HOST_STATE_SIZE;
	     iden <= KVMPPC_GSID_PROCESS_TABLE; iden++) {
		kvmppc_gsbm_set(&gsbm, iden);
		kvmppc_gsbm_set(&gsbm1, iden);
		KUNIT_EXPECT_TRUE(test, kvmppc_gsbm_test(&gsbm, iden));
		kvmppc_gsbm_clear(&gsbm, iden);
		KUNIT_EXPECT_FALSE(test, kvmppc_gsbm_test(&gsbm, iden));
		i++;
	}

	for (u16 iden = KVMPPC_GSID_RUN_INPUT; iden <= KVMPPC_GSID_VPA;
	     iden++) {
		kvmppc_gsbm_set(&gsbm, iden);
		kvmppc_gsbm_set(&gsbm1, iden);
		KUNIT_EXPECT_TRUE(test, kvmppc_gsbm_test(&gsbm, iden));
		kvmppc_gsbm_clear(&gsbm, iden);
		KUNIT_EXPECT_FALSE(test, kvmppc_gsbm_test(&gsbm, iden));
		i++;
	}

	for (u16 iden = KVMPPC_GSID_GPR(0); iden <= KVMPPC_GSE_DW_REGS_END; iden++) {
		kvmppc_gsbm_set(&gsbm, iden);
		kvmppc_gsbm_set(&gsbm1, iden);
		KUNIT_EXPECT_TRUE(test, kvmppc_gsbm_test(&gsbm, iden));
		kvmppc_gsbm_clear(&gsbm, iden);
		KUNIT_EXPECT_FALSE(test, kvmppc_gsbm_test(&gsbm, iden));
		i++;
	}

	for (u16 iden = KVMPPC_GSID_CR; iden <= KVMPPC_GSID_PSPB; iden++) {
		kvmppc_gsbm_set(&gsbm, iden);
		kvmppc_gsbm_set(&gsbm1, iden);
		KUNIT_EXPECT_TRUE(test, kvmppc_gsbm_test(&gsbm, iden));
		kvmppc_gsbm_clear(&gsbm, iden);
		KUNIT_EXPECT_FALSE(test, kvmppc_gsbm_test(&gsbm, iden));
		i++;
	}

	for (u16 iden = KVMPPC_GSID_VSRS(0); iden <= KVMPPC_GSID_VSRS(63);
	     iden++) {
		kvmppc_gsbm_set(&gsbm, iden);
		kvmppc_gsbm_set(&gsbm1, iden);
		KUNIT_EXPECT_TRUE(test, kvmppc_gsbm_test(&gsbm, iden));
		kvmppc_gsbm_clear(&gsbm, iden);
		KUNIT_EXPECT_FALSE(test, kvmppc_gsbm_test(&gsbm, iden));
		i++;
	}

	for (u16 iden = KVMPPC_GSID_HDAR; iden <= KVMPPC_GSID_ASDR; iden++) {
		kvmppc_gsbm_set(&gsbm, iden);
		kvmppc_gsbm_set(&gsbm1, iden);
		KUNIT_EXPECT_TRUE(test, kvmppc_gsbm_test(&gsbm, iden));
		kvmppc_gsbm_clear(&gsbm, iden);
		KUNIT_EXPECT_FALSE(test, kvmppc_gsbm_test(&gsbm, iden));
		i++;
	}

	j = 0;
	kvmppc_gsbm_for_each(&gsbm1, iden)
	{
		kvmppc_gsbm_set(&gsbm2, iden);
		j++;
	}
	KUNIT_EXPECT_EQ(test, i, j);
	KUNIT_EXPECT_MEMEQ(test, &gsbm1, &gsbm2, sizeof(gsbm1));
}

struct kvmppc_gs_msg_test1_data {
	u64 a;
	u32 b;
	struct kvmppc_gs_part_table c;
	struct kvmppc_gs_proc_table d;
	struct kvmppc_gs_buff_info e;
};

static size_t test1_get_size(struct kvmppc_gs_msg *gsm)
{
	size_t size = 0;
	u16 ids[] = {
		KVMPPC_GSID_PARTITION_TABLE,
		KVMPPC_GSID_PROCESS_TABLE,
		KVMPPC_GSID_RUN_INPUT,
		KVMPPC_GSID_GPR(0),
		KVMPPC_GSID_CR,
	};

	for (int i = 0; i < ARRAY_SIZE(ids); i++)
		size += kvmppc_gse_total_size(kvmppc_gsid_size(ids[i]));
	return size;
}

static int test1_fill_info(struct kvmppc_gs_buff *gsb,
			   struct kvmppc_gs_msg *gsm)
{
	struct kvmppc_gs_msg_test1_data *data = gsm->data;

	if (kvmppc_gsm_includes(gsm, KVMPPC_GSID_GPR(0)))
		kvmppc_gse_put_u64(gsb, KVMPPC_GSID_GPR(0), data->a);

	if (kvmppc_gsm_includes(gsm, KVMPPC_GSID_CR))
		kvmppc_gse_put_u32(gsb, KVMPPC_GSID_CR, data->b);

	if (kvmppc_gsm_includes(gsm, KVMPPC_GSID_PARTITION_TABLE))
		kvmppc_gse_put_part_table(gsb, KVMPPC_GSID_PARTITION_TABLE,
					  data->c);

	if (kvmppc_gsm_includes(gsm, KVMPPC_GSID_PROCESS_TABLE))
		kvmppc_gse_put_proc_table(gsb, KVMPPC_GSID_PARTITION_TABLE,
					  data->d);

	if (kvmppc_gsm_includes(gsm, KVMPPC_GSID_RUN_INPUT))
		kvmppc_gse_put_buff_info(gsb, KVMPPC_GSID_RUN_INPUT, data->e);

	return 0;
}

static int test1_refresh_info(struct kvmppc_gs_msg *gsm,
			      struct kvmppc_gs_buff *gsb)
{
	struct kvmppc_gs_parser gsp = { 0 };
	struct kvmppc_gs_msg_test1_data *data = gsm->data;
	struct kvmppc_gs_elem *gse;
	int rc;

	rc = kvmppc_gse_parse(&gsp, gsb);
	if (rc < 0)
		return rc;

	gse = kvmppc_gsp_lookup(&gsp, KVMPPC_GSID_GPR(0));
	if (gse)
		data->a = kvmppc_gse_get_u64(gse);

	gse = kvmppc_gsp_lookup(&gsp, KVMPPC_GSID_CR);
	if (gse)
		data->b = kvmppc_gse_get_u32(gse);

	return 0;
}

static struct kvmppc_gs_msg_ops gs_msg_test1_ops = {
	.get_size = test1_get_size,
	.fill_info = test1_fill_info,
	.refresh_info = test1_refresh_info,
};

static void test_gs_msg(struct kunit *test)
{
	struct kvmppc_gs_msg_test1_data test1_data = {
		.a = 0xdeadbeef,
		.b = 0x1,
	};
	struct kvmppc_gs_msg *gsm;
	struct kvmppc_gs_buff *gsb;

	gsm = kvmppc_gsm_new(&gs_msg_test1_ops, &test1_data, GSM_SEND,
			     GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gsm);

	gsb = kvmppc_gsb_new(kvmppc_gsm_size(gsm), 0, 0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gsb);

	kvmppc_gsm_include(gsm, KVMPPC_GSID_PARTITION_TABLE);
	kvmppc_gsm_include(gsm, KVMPPC_GSID_PROCESS_TABLE);
	kvmppc_gsm_include(gsm, KVMPPC_GSID_RUN_INPUT);
	kvmppc_gsm_include(gsm, KVMPPC_GSID_GPR(0));
	kvmppc_gsm_include(gsm, KVMPPC_GSID_CR);

	kvmppc_gsm_fill_info(gsm, gsb);

	memset(&test1_data, 0, sizeof(test1_data));

	kvmppc_gsm_refresh_info(gsm, gsb);
	KUNIT_EXPECT_EQ(test, test1_data.a, 0xdeadbeef);
	KUNIT_EXPECT_EQ(test, test1_data.b, 0x1);

	kvmppc_gsm_free(gsm);
}

static struct kunit_case guest_state_buffer_testcases[] = {
	KUNIT_CASE(test_creating_buffer),
	KUNIT_CASE(test_adding_element),
	KUNIT_CASE(test_gs_bitmap),
	KUNIT_CASE(test_gs_parsing),
	KUNIT_CASE(test_gs_msg),
	{}
};

static struct kunit_suite guest_state_buffer_test_suite = {
	.name = "guest_state_buffer_test",
	.test_cases = guest_state_buffer_testcases,
};

kunit_test_suites(&guest_state_buffer_test_suite);

MODULE_LICENSE("GPL");
