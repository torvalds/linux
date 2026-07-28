// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_irq.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>

#include "dc.h"
#include "inc/core_types.h"
#include "irq/irq_service.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_irq.h"
#include "amdgpu_dm_kunit_test_helpers.h"
#include "dc_dmub_srv.h"
#include "ivsrcid/ivsrcid_vislands30.h"
#include "ivsrcid/dcn/irqsrcs_dcn_1_0.h"
#include "link_service.h"
#include "dmub/dmub_srv.h"
#include "dal_asic_id.h"
#include "../../amdgpu/amdgpu_reset.h"

static void dm_test_irq_handler(void *arg)
{
}

static void dm_test_irq_handler_alt(void *arg)
{
}

static void dm_test_irq_handler_count(void *arg)
{
	int *count = arg;

	if (count)
		(*count)++;
}

static bool dm_test_detect_connection_none(struct dc_link *link,
					   enum dc_connection_type *type)
{
	*type = dc_connection_none;

	return true;
}

static bool dm_test_detect_link_false(struct dc_link *link,
				      enum dc_detect_reason reason)
{
	return false;
}

static bool dm_test_detect_connection_single(struct dc_link *link,
					     enum dc_connection_type *type)
{
	*type = dc_connection_single;

	return true;
}

/* Recording stubs for the dm_handle_hpd_rx_offload_work() DP-IRQ branches. */
static int dm_test_automated_test_count;
static int dm_test_handle_link_loss_count;

static void dm_test_dp_handle_automated_test(struct dc_link *link)
{
	dm_test_automated_test_count++;
}

static void dm_test_dp_handle_link_loss(struct dc_link *link)
{
	dm_test_handle_link_loss_count++;
}

static bool dm_test_dp_parse_link_loss_true(struct dc_link *link,
					    union hpd_irq_data *hpd_irq_dpcd_data)
{
	return true;
}

static bool dm_test_dp_should_allow_hpd_rx_irq_true(const struct dc_link *link)
{
	return true;
}

static enum dc_status dm_test_dp_read_hpd_rx_irq_data_ok(struct dc_link *link,
							 union hpd_irq_data *irq_data)
{
	return DC_OK;
}

/*
 * Allocate a refcounted dc_sink for tests without pulling in the DC-core
 * dc_sink_create()/dc_sink_release() symbols. Production code under test still
 * uses dc_sink_retain()/dc_sink_release() (resolved inside the amdgpu module).
 * Use kzalloc (not kunit_kzalloc) so the final kref_put frees it exactly once.
 */
static struct dc_sink *dm_test_sink_create(struct dc_link *link)
{
	struct dc_sink *sink = kzalloc(sizeof(*sink), GFP_KERNEL);

	if (!sink)
		return NULL;

	sink->link = link;
	sink->ctx = link->ctx;
	kref_init(&sink->refcount);

	return sink;
}

static void dm_test_sink_free(struct kref *kref)
{
	struct dc_sink *sink = container_of(kref, struct dc_sink, refcount);

	kfree(sink->dc_container_id);
	kfree(sink);
}

static void dm_test_sink_release(struct dc_sink *sink)
{
	kref_put(&sink->refcount, dm_test_sink_free);
}

static bool dm_test_handle_hpd_rx_no_work(struct dc_link *link,
					  union hpd_irq_data *hpd_irq_data,
					  bool *link_loss,
					  bool defer_handling,
					  bool *has_left_work)
{
	*link_loss = false;
	*has_left_work = false;

	return false;
}

static bool dm_test_handle_hpd_rx_automated(struct dc_link *link,
					    union hpd_irq_data *hpd_irq_data,
					    bool *link_loss,
					    bool defer_handling,
					    bool *has_left_work)
{
	*link_loss = false;
	*has_left_work = true;
	hpd_irq_data->bytes.device_service_irq.bits.AUTOMATED_TEST = 1;

	return false;
}

static bool dm_test_handle_hpd_rx_msg_rdy(struct dc_link *link,
					  union hpd_irq_data *hpd_irq_data,
					  bool *link_loss,
					  bool defer_handling,
					  bool *has_left_work)
{
	*link_loss = false;
	*has_left_work = true;
	hpd_irq_data->bytes.device_service_irq.bits.UP_REQ_MSG_RDY = 1;

	return false;
}

static bool dm_test_handle_hpd_rx_link_loss(struct dc_link *link,
					    union hpd_irq_data *hpd_irq_data,
					    bool *link_loss,
					    bool defer_handling,
					    bool *has_left_work)
{
	*link_loss = true;
	*has_left_work = true;

	return false;
}

static bool dm_test_allow_hpd_rx_irq_true(const struct dc_link *link)
{
	return true;
}


static uint32_t dm_test_dmub_get_outbox0_wptr(struct dmub_srv *dmub)
{
	return 0;
}

static uint32_t dm_test_dmub_get_outbox1_wptr(struct dmub_srv *dmub)
{
	return 0;
}

static int dm_test_dmub_notify_count;

static void dm_test_dmub_notify_callback(struct amdgpu_device *adev,
					 struct dmub_notification *notify)
{
	dm_test_dmub_notify_count++;
}

static struct dc *dm_test_alloc_dc_with_ctx(struct kunit *test)
{
	struct dc_context *ctx;
	struct dc *dc;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	dc->ctx = ctx;
	ctx->dc = dc;

	return dc;
}

static enum dc_irq_source dm_test_to_dal_irq_source_dce110(
		struct irq_service *irq_service,
		uint32_t src_id,
		uint32_t ext_id)
{
	switch (src_id) {
	case VISLANDS30_IV_SRCID_D1_VERTICAL_INTERRUPT0:
		return DC_IRQ_SOURCE_VBLANK1;
	case VISLANDS30_IV_SRCID_D1_V_UPDATE_INT:
		return DC_IRQ_SOURCE_VUPDATE1;
	case VISLANDS30_IV_SRCID_D1_GRPH_PFLIP:
		return DC_IRQ_SOURCE_PFLIP1;
	case VISLANDS30_IV_SRCID_D1_GRPH_PFLIP + 2:
		return DC_IRQ_SOURCE_PFLIP2;
	case VISLANDS30_IV_SRCID_D1_GRPH_PFLIP + 4:
		return DC_IRQ_SOURCE_PFLIP3;
	case VISLANDS30_IV_SRCID_D1_GRPH_PFLIP + 6:
		return DC_IRQ_SOURCE_PFLIP4;
	case VISLANDS30_IV_SRCID_D1_GRPH_PFLIP + 8:
		return DC_IRQ_SOURCE_PFLIP5;
	case VISLANDS30_IV_SRCID_D1_GRPH_PFLIP + 10:
		return DC_IRQ_SOURCE_PFLIP6;
	default:
		return DC_IRQ_SOURCE_INVALID;
	}
}

static const struct irq_service_funcs dm_test_irq_service_funcs_dce110 = {
	.to_dal_irq_source = dm_test_to_dal_irq_source_dce110
};

static enum dc_irq_source dm_test_to_dal_irq_source_dcn10(
		struct irq_service *irq_service,
		uint32_t src_id,
		uint32_t ext_id)
{
	switch (src_id) {
	case DCN_1_0__SRCID__DC_D1_OTG_VSTARTUP:
		return DC_IRQ_SOURCE_VBLANK1;
	case DCN_1_0__SRCID__OTG0_IHC_V_UPDATE_NO_LOCK_INTERRUPT:
		return DC_IRQ_SOURCE_VUPDATE1;
	case DCN_1_0__SRCID__HUBP0_FLIP_INTERRUPT:
		return DC_IRQ_SOURCE_PFLIP1;
	case DCN_1_0__SRCID__DMCUB_OUTBOX_LOW_PRIORITY_READY_INT:
		return DC_IRQ_SOURCE_DMCUB_OUTBOX;
	default:
		return DC_IRQ_SOURCE_INVALID;
	}
}

static const struct irq_service_funcs dm_test_irq_service_funcs_dcn10 = {
	.to_dal_irq_source = dm_test_to_dal_irq_source_dcn10
};

static bool dm_test_irq_src_set(struct irq_service *irq_service,
				const struct irq_source_info *info, bool enable)
{
	return true;
}

static bool dm_test_irq_src_ack(struct irq_service *irq_service,
				const struct irq_source_info *info)
{
	return true;
}

/* Per-source funcs let dc_interrupt_set() succeed without register access. */
static struct irq_source_info_funcs dm_test_irq_src_funcs = {
	.set = dm_test_irq_src_set,
	.ack = dm_test_irq_src_ack,
};

static struct dc *dm_test_alloc_dc_with_irq_service(struct kunit *test,
						    const struct irq_service_funcs *funcs)
{
	struct irq_source_info *info;
	struct resource_pool *res_pool;
	struct irq_service *irqs;
	struct dc *dc;
	int i;

	dc = dm_test_alloc_dc_with_ctx(test);
	res_pool = kunit_kzalloc(test, sizeof(*res_pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, res_pool);
	irqs = kunit_kzalloc(test, sizeof(*irqs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, irqs);

	/*
	 * Populate the per-source info table so dc_interrupt_set()/_ack()
	 * succeed without touching hardware registers.
	 */
	info = kunit_kzalloc(test, sizeof(*info) * DAL_IRQ_SOURCES_NUMBER,
			     GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, info);
	for (i = 0; i < DAL_IRQ_SOURCES_NUMBER; i++)
		info[i].funcs = &dm_test_irq_src_funcs;

	irqs->funcs = funcs;
	irqs->info = info;
	res_pool->irqs = irqs;
	dc->res_pool = res_pool;

	return dc;
}

static void dm_test_free_irq_sources(void *data)
{
	struct amdgpu_device *adev = data;
	int i;

	for (i = 0; i < AMDGPU_IRQ_CLIENTID_MAX; i++) {
		kfree(adev->irq.client[i].sources);
		adev->irq.client[i].sources = NULL;
	}

	kfree(adev->crtc_irq.enabled_types);
	adev->crtc_irq.enabled_types = NULL;
	kfree(adev->vline0_irq.enabled_types);
	adev->vline0_irq.enabled_types = NULL;
	kfree(adev->vupdate_irq.enabled_types);
	adev->vupdate_irq.enabled_types = NULL;
	kfree(adev->pageflip_irq.enabled_types);
	adev->pageflip_irq.enabled_types = NULL;
	kfree(adev->dmub_outbox_irq.enabled_types);
	adev->dmub_outbox_irq.enabled_types = NULL;
	kfree(adev->dmub_trace_irq.enabled_types);
	adev->dmub_trace_irq.enabled_types = NULL;
	kfree(adev->hpd_irq.enabled_types);
	adev->hpd_irq.enabled_types = NULL;
}

static void dm_test_crtc_list_del(void *data)
{
	struct amdgpu_crtc *acrtc = data;

	list_del_init(&acrtc->base.head);
}

struct dm_test_hpd_rx_wq_ctx {
	struct hpd_rx_irq_offload_work_queue *wq;
	int count;
};

static void dm_test_destroy_hpd_rx_wq(void *data)
{
	struct dm_test_hpd_rx_wq_ctx *ctx = data;
	int i;

	for (i = 0; i < ctx->count; i++)
		if (ctx->wq[i].wq)
			destroy_workqueue(ctx->wq[i].wq);
	kfree(ctx->wq);
}

static const struct drm_connector_funcs dm_test_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
};

static void dm_test_connector_cleanup(void *data)
{
	drm_connector_cleanup(data);
}

/* Tests for amdgpu_dm_hpd_to_dal_irq_source() */

/**
 * dm_test_hpd_to_dal_irq_source_hpd1 - Test Hpd to dal irq source hpd1
 * @test: The KUnit test context
 */
static void dm_test_hpd_to_dal_irq_source_hpd1(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_hpd_to_dal_irq_source(AMDGPU_HPD_1),
			(int)DC_IRQ_SOURCE_HPD1);
}

/**
 * dm_test_hpd_to_dal_irq_source_hpd2 - Test Hpd to dal irq source hpd2
 * @test: The KUnit test context
 */
static void dm_test_hpd_to_dal_irq_source_hpd2(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_hpd_to_dal_irq_source(AMDGPU_HPD_2),
			(int)DC_IRQ_SOURCE_HPD2);
}

/**
 * dm_test_hpd_to_dal_irq_source_hpd3 - Test Hpd to dal irq source hpd3
 * @test: The KUnit test context
 */
static void dm_test_hpd_to_dal_irq_source_hpd3(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_hpd_to_dal_irq_source(AMDGPU_HPD_3),
			(int)DC_IRQ_SOURCE_HPD3);
}

/**
 * dm_test_hpd_to_dal_irq_source_hpd4 - Test Hpd to dal irq source hpd4
 * @test: The KUnit test context
 */
static void dm_test_hpd_to_dal_irq_source_hpd4(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_hpd_to_dal_irq_source(AMDGPU_HPD_4),
			(int)DC_IRQ_SOURCE_HPD4);
}

/**
 * dm_test_hpd_to_dal_irq_source_hpd5 - Test Hpd to dal irq source hpd5
 * @test: The KUnit test context
 */
static void dm_test_hpd_to_dal_irq_source_hpd5(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_hpd_to_dal_irq_source(AMDGPU_HPD_5),
			(int)DC_IRQ_SOURCE_HPD5);
}

/**
 * dm_test_hpd_to_dal_irq_source_hpd6 - Test Hpd to dal irq source hpd6
 * @test: The KUnit test context
 */
static void dm_test_hpd_to_dal_irq_source_hpd6(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_hpd_to_dal_irq_source(AMDGPU_HPD_6),
			(int)DC_IRQ_SOURCE_HPD6);
}

/**
 * dm_test_hpd_to_dal_irq_source_invalid - Test Hpd to dal irq source invalid
 * @test: The KUnit test context
 */
static void dm_test_hpd_to_dal_irq_source_invalid(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_hpd_to_dal_irq_source(AMDGPU_HPD_NONE),
			(int)DC_IRQ_SOURCE_INVALID);
}

/**
 * dm_test_hpd_to_dal_irq_source_out_of_range - Test Hpd to dal irq source out of range
 * @test: The KUnit test context
 */
static void dm_test_hpd_to_dal_irq_source_out_of_range(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, (int)amdgpu_dm_hpd_to_dal_irq_source(99),
			(int)DC_IRQ_SOURCE_INVALID);
}

/* Tests for are_sinks_equal() */

/**
 * dm_test_are_sinks_equal_both_null - Test Are sinks equal both null
 * @test: The KUnit test context
 */
static void dm_test_are_sinks_equal_both_null(struct kunit *test)
{
	KUNIT_EXPECT_FALSE(test, are_sinks_equal(NULL, NULL));
}

/**
 * dm_test_are_sinks_equal_first_null - Test Are sinks equal first null
 * @test: The KUnit test context
 */
static void dm_test_are_sinks_equal_first_null(struct kunit *test)
{
	struct dc_sink *sink2;

	sink2 = kunit_kzalloc(test, sizeof(*sink2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink2);

	KUNIT_EXPECT_FALSE(test, are_sinks_equal(NULL, sink2));
}

/**
 * dm_test_are_sinks_equal_second_null - Test Are sinks equal second null
 * @test: The KUnit test context
 */
static void dm_test_are_sinks_equal_second_null(struct kunit *test)
{
	struct dc_sink *sink1;

	sink1 = kunit_kzalloc(test, sizeof(*sink1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink1);

	KUNIT_EXPECT_FALSE(test, are_sinks_equal(sink1, NULL));
}

/**
 * dm_test_are_sinks_equal_different_signal - Test Are sinks equal different signal
 * @test: The KUnit test context
 */
static void dm_test_are_sinks_equal_different_signal(struct kunit *test)
{
	struct dc_sink *sink1, *sink2;

	sink1 = kunit_kzalloc(test, sizeof(*sink1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink1);
	sink2 = kunit_kzalloc(test, sizeof(*sink2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink2);

	sink1->sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	sink2->sink_signal = SIGNAL_TYPE_DISPLAY_PORT;

	KUNIT_EXPECT_FALSE(test, are_sinks_equal(sink1, sink2));
}

/**
 * dm_test_are_sinks_equal_different_edid_length - Test Are sinks equal different edid length
 * @test: The KUnit test context
 */
static void dm_test_are_sinks_equal_different_edid_length(struct kunit *test)
{
	struct dc_sink *sink1, *sink2;

	sink1 = kunit_kzalloc(test, sizeof(*sink1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink1);
	sink2 = kunit_kzalloc(test, sizeof(*sink2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink2);

	sink1->sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	sink2->sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	sink1->dc_edid.length = 128;
	sink2->dc_edid.length = 256;

	KUNIT_EXPECT_FALSE(test, are_sinks_equal(sink1, sink2));
}

/**
 * dm_test_are_sinks_equal_different_edid_data - Test Are sinks equal different edid data
 * @test: The KUnit test context
 */
static void dm_test_are_sinks_equal_different_edid_data(struct kunit *test)
{
	struct dc_sink *sink1, *sink2;

	sink1 = kunit_kzalloc(test, sizeof(*sink1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink1);
	sink2 = kunit_kzalloc(test, sizeof(*sink2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink2);

	sink1->sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	sink2->sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	sink1->dc_edid.length = 4;
	sink2->dc_edid.length = 4;
	memset(sink1->dc_edid.raw_edid, 0xAA, 4);
	memset(sink2->dc_edid.raw_edid, 0xBB, 4);

	KUNIT_EXPECT_FALSE(test, are_sinks_equal(sink1, sink2));
}

/**
 * dm_test_are_sinks_equal_identical - Test Are sinks equal identical
 * @test: The KUnit test context
 */
static void dm_test_are_sinks_equal_identical(struct kunit *test)
{
	struct dc_sink *sink1, *sink2;

	sink1 = kunit_kzalloc(test, sizeof(*sink1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink1);
	sink2 = kunit_kzalloc(test, sizeof(*sink2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink2);

	sink1->sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	sink2->sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	sink1->dc_edid.length = 4;
	sink2->dc_edid.length = 4;
	memset(sink1->dc_edid.raw_edid, 0xAA, 4);
	memset(sink2->dc_edid.raw_edid, 0xAA, 4);

	KUNIT_EXPECT_TRUE(test, are_sinks_equal(sink1, sink2));
}

/**
 * dm_test_are_sinks_equal_zero_length - Test Are sinks equal zero length
 * @test: The KUnit test context
 */
static void dm_test_are_sinks_equal_zero_length(struct kunit *test)
{
	struct dc_sink *sink1, *sink2;

	sink1 = kunit_kzalloc(test, sizeof(*sink1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink1);
	sink2 = kunit_kzalloc(test, sizeof(*sink2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink2);

	sink1->sink_signal = SIGNAL_TYPE_DISPLAY_PORT;
	sink2->sink_signal = SIGNAL_TYPE_DISPLAY_PORT;
	sink1->dc_edid.length = 0;
	sink2->dc_edid.length = 0;

	KUNIT_EXPECT_TRUE(test, are_sinks_equal(sink1, sink2));
}

/**
 * dm_test_are_sinks_equal_full_edid_identical - Test Are sinks equal full edid identical
 * @test: The KUnit test context
 */
static void dm_test_are_sinks_equal_full_edid_identical(struct kunit *test)
{
	struct dc_sink *sink1, *sink2;

	sink1 = kunit_kzalloc(test, sizeof(*sink1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink1);
	sink2 = kunit_kzalloc(test, sizeof(*sink2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink2);

	sink1->sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	sink2->sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	sink1->dc_edid.length = 128;
	sink2->dc_edid.length = 128;
	memset(sink1->dc_edid.raw_edid, 0x5A, 128);
	memset(sink2->dc_edid.raw_edid, 0x5A, 128);

	KUNIT_EXPECT_TRUE(test, are_sinks_equal(sink1, sink2));
}

/**
 * dm_test_are_sinks_equal_full_edid_last_byte_differs - Test Are sinks equal last byte differs
 * @test: The KUnit test context
 */
static void dm_test_are_sinks_equal_full_edid_last_byte_differs(struct kunit *test)
{
	struct dc_sink *sink1, *sink2;

	sink1 = kunit_kzalloc(test, sizeof(*sink1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink1);
	sink2 = kunit_kzalloc(test, sizeof(*sink2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sink2);

	sink1->sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	sink2->sink_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	sink1->dc_edid.length = 128;
	sink2->dc_edid.length = 128;
	memset(sink1->dc_edid.raw_edid, 0x5A, 128);
	memset(sink2->dc_edid.raw_edid, 0x5A, 128);
	sink2->dc_edid.raw_edid[127] = 0x5B;

	KUNIT_EXPECT_FALSE(test, are_sinks_equal(sink1, sink2));
}

/* Tests for dmub_notification_type_str() */

/**
 * dm_test_notification_str_no_data - Test Notification str no data
 * @test: The KUnit test context
 */
static void dm_test_notification_str_no_data(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, dmub_notification_type_str(DMUB_NOTIFICATION_NO_DATA), "NO_DATA");
}

/**
 * dm_test_notification_str_aux_reply - Test Notification str aux reply
 * @test: The KUnit test context
 */
static void dm_test_notification_str_aux_reply(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, dmub_notification_type_str(DMUB_NOTIFICATION_AUX_REPLY), "AUX_REPLY");
}

/**
 * dm_test_notification_str_hpd - Test Notification str hpd
 * @test: The KUnit test context
 */
static void dm_test_notification_str_hpd(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, dmub_notification_type_str(DMUB_NOTIFICATION_HPD), "HPD");
}

/**
 * dm_test_notification_str_hpd_irq - Test Notification str hpd irq
 * @test: The KUnit test context
 */
static void dm_test_notification_str_hpd_irq(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, dmub_notification_type_str(DMUB_NOTIFICATION_HPD_IRQ), "HPD_IRQ");
}

/**
 * dm_test_notification_str_set_config - Test Notification str set config
 * @test: The KUnit test context
 */
static void dm_test_notification_str_set_config(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, dmub_notification_type_str(DMUB_NOTIFICATION_SET_CONFIG_REPLY),
			   "SET_CONFIG_REPLY");
}

/**
 * dm_test_notification_str_dpia - Test Notification str dpia
 * @test: The KUnit test context
 */
static void dm_test_notification_str_dpia(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, dmub_notification_type_str(DMUB_NOTIFICATION_DPIA_NOTIFICATION),
			   "DPIA_NOTIFICATION");
}

/**
 * dm_test_notification_str_hpd_sense - Test Notification str hpd sense
 * @test: The KUnit test context
 */
static void dm_test_notification_str_hpd_sense(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, dmub_notification_type_str(DMUB_NOTIFICATION_HPD_SENSE_NOTIFY),
			   "HPD_SENSE_NOTIFY");
}

/**
 * dm_test_notification_str_fused_io - Test Notification str fused io
 * @test: The KUnit test context
 */
static void dm_test_notification_str_fused_io(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, dmub_notification_type_str(DMUB_NOTIFICATION_FUSED_IO),
			   "FUSED_IO");
}

/**
 * dm_test_notification_str_unknown - Test Notification str unknown
 * @test: The KUnit test context
 */
static void dm_test_notification_str_unknown(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, dmub_notification_type_str(DMUB_NOTIFICATION_MAX), "<unknown>");
}

/* Tests for amdgpu_dm_irq_init() */

/**
 * dm_test_irq_init_initializes_lists - Test irq init initializes list heads
 * @test: The KUnit test context
 */
static void dm_test_irq_init_initializes_lists(struct kunit *test)
{
	struct amdgpu_device *adev;
	int src;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	for (src = 0; src < DAL_IRQ_SOURCES_NUMBER; src++) {
		KUNIT_EXPECT_TRUE(test,
				  list_empty(&adev->dm.irq_handler_list_low_tab[src]));
		KUNIT_EXPECT_TRUE(test,
				  list_empty(&adev->dm.irq_handler_list_high_tab[src]));
	}
}

/* Tests for amdgpu_dm_irq_register_interrupt() */

/**
 * dm_test_irq_register_rejects_null_params - Test register rejects null params
 * @test: The KUnit test context
 */
static void dm_test_irq_register_rejects_null_params(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_interrupt_params int_params = { 0 };

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD1;

	KUNIT_EXPECT_NULL(test,
		amdgpu_dm_irq_register_interrupt(adev, NULL,
						   dm_test_irq_handler, NULL));
	KUNIT_EXPECT_NULL(test,
		amdgpu_dm_irq_register_interrupt(adev, &int_params, NULL, NULL));
}

/**
 * dm_test_irq_register_rejects_invalid_context - Test register rejects context
 * @test: The KUnit test context
 */
static void dm_test_irq_register_rejects_invalid_context(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_interrupt_params int_params = { 0 };

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	int_params.int_context = INTERRUPT_CONTEXT_NUMBER;
	int_params.irq_source = DC_IRQ_SOURCE_HPD1;

	KUNIT_EXPECT_NULL(test,
		amdgpu_dm_irq_register_interrupt(adev, &int_params,
						   dm_test_irq_handler, NULL));
}

/**
 * dm_test_irq_register_rejects_invalid_source - Test register rejects source
 * @test: The KUnit test context
 */
static void dm_test_irq_register_rejects_invalid_source(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_interrupt_params int_params = { 0 };

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_INVALID;

	KUNIT_EXPECT_NULL(test,
		amdgpu_dm_irq_register_interrupt(adev, &int_params,
						   dm_test_irq_handler, NULL));
}

/**
 * dm_test_irq_register_adds_low_context_handler - Test register adds low handler
 * @test: The KUnit test context
 */
static void dm_test_irq_register_adds_low_context_handler(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_interrupt_params int_params = { 0 };
	void *handler;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD1;

	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						    dm_test_irq_handler, adev);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);
	KUNIT_EXPECT_FALSE(test,
			   list_empty(&adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD1]));
	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_high_tab[DC_IRQ_SOURCE_HPD1]));

	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD1,
						dm_test_irq_handler);
	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD1]));
}

/**
 * dm_test_irq_register_adds_high_context_handler - Test register adds high handler
 * @test: The KUnit test context
 */
static void dm_test_irq_register_adds_high_context_handler(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_interrupt_params int_params = { 0 };
	void *handler;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD2;

	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						    dm_test_irq_handler, adev);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);
	KUNIT_EXPECT_FALSE(test,
			   list_empty(&adev->dm.irq_handler_list_high_tab[DC_IRQ_SOURCE_HPD2]));
	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD2]));

	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD2,
						dm_test_irq_handler);
	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_high_tab[DC_IRQ_SOURCE_HPD2]));
}

/**
 * dm_test_irq_register_multiple_handlers - Test register keeps multiple handlers
 * @test: The KUnit test context
 */
static void dm_test_irq_register_multiple_handlers(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_interrupt_params int_params = { 0 };
	struct list_head *hnd_list;
	void *handler1, *handler2;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD1;

	handler1 = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						    dm_test_irq_handler, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler1);
	handler2 = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						    dm_test_irq_handler_alt, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler2);

	hnd_list = &adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD1];
	KUNIT_EXPECT_EQ(test, list_count_nodes(hnd_list), 2);

	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD1,
						dm_test_irq_handler);
	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD1,
						dm_test_irq_handler_alt);
	KUNIT_EXPECT_TRUE(test, list_empty(hnd_list));
}

/**
 * dm_test_irq_register_separate_contexts - Test register same source in two contexts
 * @test: The KUnit test context
 */
static void dm_test_irq_register_separate_contexts(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_interrupt_params int_params = { 0 };
	void *handler;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.irq_source = DC_IRQ_SOURCE_HPD5;

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						    dm_test_irq_handler, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						    dm_test_irq_handler, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	KUNIT_EXPECT_FALSE(test,
			   list_empty(&adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD5]));
	KUNIT_EXPECT_FALSE(test,
			   list_empty(&adev->dm.irq_handler_list_high_tab[DC_IRQ_SOURCE_HPD5]));

	/*
	 * A single unregister call stops at the first context where the handler
	 * is found (low context), leaving the high context handler in place.
	 */
	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD5,
						dm_test_irq_handler);

	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD5]));
	KUNIT_EXPECT_FALSE(test,
			   list_empty(&adev->dm.irq_handler_list_high_tab[DC_IRQ_SOURCE_HPD5]));

	/* A second call removes the remaining high context handler. */
	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD5,
						dm_test_irq_handler);

	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_high_tab[DC_IRQ_SOURCE_HPD5]));
}

/* Tests for amdgpu_dm_irq_unregister_interrupt() */

/**
 * dm_test_irq_unregister_rejects_invalid_source - Test unregister rejects source
 * @test: The KUnit test context
 */
static void dm_test_irq_unregister_rejects_invalid_source(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_INVALID,
						dm_test_irq_handler);

	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD1]));
	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_high_tab[DC_IRQ_SOURCE_HPD1]));
}

/**
 * dm_test_irq_unregister_rejects_null_handler - Test unregister rejects handler
 * @test: The KUnit test context
 */
static void dm_test_irq_unregister_rejects_null_handler(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD1,
						DAL_INVALID_IRQ_HANDLER_IDX);

	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD1]));
	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_high_tab[DC_IRQ_SOURCE_HPD1]));
}

/**
 * dm_test_irq_unregister_handler_not_found - Test unregister keeps unmatched handler
 * @test: The KUnit test context
 */
static void dm_test_irq_unregister_handler_not_found(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_interrupt_params int_params = { 0 };
	void *handler;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD1;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						    dm_test_irq_handler, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	/* Unregister a handler that was never registered for this source. */
	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD1,
						dm_test_irq_handler_alt);

	/* The originally registered handler must still be present. */
	KUNIT_EXPECT_FALSE(test,
			   list_empty(&adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD1]));

	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD1,
						dm_test_irq_handler);
	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD1]));
}

/* Tests for amdgpu_dm_irq_fini() */

/**
 * dm_test_irq_fini_removes_registered_handlers - Test fini removes handlers
 * @test: The KUnit test context
 */
static void dm_test_irq_fini_removes_registered_handlers(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_interrupt_params int_params = { 0 };
	void *handler;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD3;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						    dm_test_irq_handler, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD4;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						    dm_test_irq_handler, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	amdgpu_dm_irq_fini(adev);

	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD3]));
	KUNIT_EXPECT_TRUE(test,
			  list_empty(&adev->dm.irq_handler_list_high_tab[DC_IRQ_SOURCE_HPD4]));
}

/**
 * dm_test_irq_fini_on_empty_tables - Test fini on tables with no handlers
 * @test: The KUnit test context
 */
static void dm_test_irq_fini_on_empty_tables(struct kunit *test)
{
	struct amdgpu_device *adev;
	int src;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	amdgpu_dm_irq_fini(adev);

	for (src = 0; src < DAL_IRQ_SOURCES_NUMBER; src++) {
		KUNIT_EXPECT_TRUE(test,
				  list_empty(&adev->dm.irq_handler_list_low_tab[src]));
		KUNIT_EXPECT_TRUE(test,
				  list_empty(&adev->dm.irq_handler_list_high_tab[src]));
	}
}

/* Tests for amdgpu_dm_get_crtc_by_otg_inst() */

/**
 * dm_test_get_crtc_by_otg_inst_returns_match - Test CRTC lookup by OTG instance
 * @test: The KUnit test context
 */
static void dm_test_get_crtc_by_otg_inst_returns_match(struct kunit *test)
{
	struct amdgpu_crtc *acrtc_a, *acrtc_b;
	struct amdgpu_device *adev;
	struct drm_device *drm;

	adev = dm_kunit_alloc_adev(test);
	drm = &adev->ddev;

	acrtc_a = kunit_kzalloc(test, sizeof(*acrtc_a), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc_a);
	acrtc_b = kunit_kzalloc(test, sizeof(*acrtc_b), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc_b);

	INIT_LIST_HEAD(&acrtc_a->base.head);
	INIT_LIST_HEAD(&acrtc_b->base.head);
	acrtc_a->otg_inst = 1;
	acrtc_b->otg_inst = 3;

	list_add_tail(&acrtc_a->base.head, &drm->mode_config.crtc_list);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_crtc_list_del, acrtc_a), 0);
	list_add_tail(&acrtc_b->base.head, &drm->mode_config.crtc_list);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_crtc_list_del, acrtc_b), 0);

	KUNIT_EXPECT_PTR_EQ(test, amdgpu_dm_get_crtc_by_otg_inst(adev, 3), acrtc_b);
}

/**
 * dm_test_get_crtc_by_otg_inst_returns_null - Test CRTC lookup misses unknown OTG
 * @test: The KUnit test context
 */
static void dm_test_get_crtc_by_otg_inst_returns_null(struct kunit *test)
{
	struct amdgpu_crtc *acrtc;
	struct amdgpu_device *adev;
	struct drm_device *drm;

	adev = dm_kunit_alloc_adev(test);
	drm = &adev->ddev;

	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	INIT_LIST_HEAD(&acrtc->base.head);
	acrtc->otg_inst = 2;

	list_add_tail(&acrtc->base.head, &drm->mode_config.crtc_list);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_crtc_list_del, acrtc), 0);

	KUNIT_EXPECT_NULL(test, amdgpu_dm_get_crtc_by_otg_inst(adev, 5));
}

/**
 * dm_test_get_crtc_by_otg_inst_empty_list - Test CRTC lookup on empty CRTC list
 * @test: The KUnit test context
 */
static void dm_test_get_crtc_by_otg_inst_empty_list(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);

	KUNIT_EXPECT_NULL(test, amdgpu_dm_get_crtc_by_otg_inst(adev, 0));
}

/* Tests for amdgpu_dm_set_irq_funcs() */

/**
 * dm_test_set_irq_funcs - Test irq src funcs and counts are populated
 * @test: The KUnit test context
 */
static void dm_test_set_irq_funcs(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->mode_info.num_crtc = 6;
	adev->mode_info.num_hpd = 4;

	amdgpu_dm_set_irq_funcs(adev);

	KUNIT_EXPECT_EQ(test, adev->crtc_irq.num_types, 6);
	KUNIT_EXPECT_EQ(test, adev->vline0_irq.num_types, 6);
	KUNIT_EXPECT_EQ(test, adev->vupdate_irq.num_types, 6);
	KUNIT_EXPECT_EQ(test, adev->pageflip_irq.num_types, 6);
	KUNIT_EXPECT_EQ(test, adev->dmub_outbox_irq.num_types, 1);
	KUNIT_EXPECT_EQ(test, adev->dmub_trace_irq.num_types, 1);
	KUNIT_EXPECT_EQ(test, adev->hpd_irq.num_types, 4);

	KUNIT_EXPECT_TRUE(test, adev->crtc_irq.funcs != NULL);
	KUNIT_EXPECT_TRUE(test, adev->vline0_irq.funcs != NULL);
	KUNIT_EXPECT_TRUE(test, adev->dmub_outbox_irq.funcs != NULL);
	KUNIT_EXPECT_TRUE(test, adev->vupdate_irq.funcs != NULL);
	KUNIT_EXPECT_TRUE(test, adev->dmub_trace_irq.funcs != NULL);
	KUNIT_EXPECT_TRUE(test, adev->pageflip_irq.funcs != NULL);
	KUNIT_EXPECT_TRUE(test, adev->hpd_irq.funcs != NULL);
}

/* Tests for amdgpu_dm_irq_suspend()/resume_early()/resume_late() */

/**
 * dm_test_irq_suspend_empty - Test suspend walks empty handler tables safely
 * @test: The KUnit test context
 */
static void dm_test_irq_suspend_empty(struct kunit *test)
{
	struct amdgpu_device *adev;
	int src;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	/*
	 * With no registered handlers the HW dc_interrupt_set() calls are
	 * skipped, so suspend must complete without touching the (absent) DC.
	 */
	amdgpu_dm_irq_suspend(adev);

	for (src = 0; src < DAL_IRQ_SOURCES_NUMBER; src++) {
		KUNIT_EXPECT_TRUE(test, list_empty(&adev->dm.irq_handler_list_low_tab[src]));
		KUNIT_EXPECT_TRUE(test, list_empty(&adev->dm.irq_handler_list_high_tab[src]));
	}
}

/**
 * dm_test_irq_resume_early_empty - Test early resume walks empty tables safely
 * @test: The KUnit test context
 */
static void dm_test_irq_resume_early_empty(struct kunit *test)
{
	struct amdgpu_device *adev;
	int src;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	amdgpu_dm_irq_resume_early(adev);

	for (src = 0; src < DAL_IRQ_SOURCES_NUMBER; src++)
		KUNIT_EXPECT_TRUE(test, list_empty(&adev->dm.irq_handler_list_high_tab[src]));
}

/**
 * dm_test_irq_resume_late_empty - Test late resume walks empty tables safely
 * @test: The KUnit test context
 */
static void dm_test_irq_resume_late_empty(struct kunit *test)
{
	struct amdgpu_device *adev;
	int src;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	amdgpu_dm_irq_resume_late(adev);

	for (src = 0; src < DAL_IRQ_SOURCES_NUMBER; src++)
		KUNIT_EXPECT_TRUE(test, list_empty(&adev->dm.irq_handler_list_low_tab[src]));
}

/**
 * dm_test_irq_suspend_registered - Test suspend reaches the dc_interrupt_set path
 * @test: The KUnit test context
 *
 * Registers a low-context HPD handler so the handler list is non-empty,
 * forcing amdgpu_dm_irq_suspend() to call dc_interrupt_set() (NULL-safe with
 * no DC) and flush_work() on the registered handler.
 */
static void dm_test_irq_suspend_registered(struct kunit *test)
{
	struct dc_interrupt_params int_params = { 0 };
	struct amdgpu_device *adev;
	void *handler;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD1;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						   dm_test_irq_handler, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	amdgpu_dm_irq_suspend(adev);

	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD1,
					   dm_test_irq_handler);
}

/**
 * dm_test_irq_suspend_disables_polling - Test suspend disables KMS polling
 * @test: The KUnit test context
 *
 * With KMS polling active, suspend must take the poll-disable branch.
 * drm_kms_helper_poll_disable() only cancels the poll work; it leaves the
 * poll_enabled flag set (cleared later by drm_kms_helper_poll_fini()).
 */
static void dm_test_irq_suspend_disables_polling(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	/* Enable KMS polling so suspend takes the poll-disable branch. */
	drm_kms_helper_poll_init(&adev->ddev);
	KUNIT_ASSERT_TRUE(test, adev->ddev.mode_config.poll_enabled);

	amdgpu_dm_irq_suspend(adev);

	KUNIT_EXPECT_TRUE(test, adev->ddev.mode_config.poll_enabled);

	drm_kms_helper_poll_fini(&adev->ddev);
}

/**
 * dm_test_irq_resume_early_registered - Test early resume reaches dc_interrupt_set
 * @test: The KUnit test context
 *
 * Registers a low-context HPD RX handler so early resume calls
 * dc_interrupt_set() for the short-pulse interrupt source.
 */
static void dm_test_irq_resume_early_registered(struct kunit *test)
{
	struct dc_interrupt_params int_params = { 0 };
	struct amdgpu_device *adev;
	void *handler;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD1RX;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						   dm_test_irq_handler, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	amdgpu_dm_irq_resume_early(adev);

	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD1RX,
					   dm_test_irq_handler);
}

/**
 * dm_test_irq_resume_late_registered - Test late resume reaches dc_interrupt_set
 * @test: The KUnit test context
 *
 * Registers a low-context HPD handler so late resume calls dc_interrupt_set()
 * for the HPD interrupt source.
 */
static void dm_test_irq_resume_late_registered(struct kunit *test)
{
	struct dc_interrupt_params int_params = { 0 };
	struct amdgpu_device *adev;
	void *handler;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD1;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						   dm_test_irq_handler, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	amdgpu_dm_irq_resume_late(adev);

	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD1,
					   dm_test_irq_handler);
}

/**
 * dm_test_irq_resume_late_enables_polling - Test late resume re-enables polling
 * @test: The KUnit test context
 *
 * With KMS polling active, late resume must take the poll-enable branch and
 * leave polling enabled.
 */
static void dm_test_irq_resume_late_enables_polling(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	/* Enable KMS polling so resume_late takes the poll-enable branch. */
	drm_kms_helper_poll_init(&adev->ddev);
	KUNIT_ASSERT_TRUE(test, adev->ddev.mode_config.poll_enabled);

	amdgpu_dm_irq_resume_late(adev);

	KUNIT_EXPECT_TRUE(test, adev->ddev.mode_config.poll_enabled);

	drm_kms_helper_poll_fini(&adev->ddev);
}

/* Tests for amdgpu_dm_hpd_rx_irq_create_workqueue() */

/**
 * dm_test_hpd_rx_irq_create_workqueue - Test workqueue array creation
 * @test: The KUnit test context
 */
static void dm_test_hpd_rx_irq_create_workqueue(struct kunit *test)
{
	struct dm_test_hpd_rx_wq_ctx *ctx;
	struct amdgpu_device *adev;
	struct dc *dc;
	int i;

	adev = dm_kunit_alloc_adev(test);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	dc->caps.max_links = 4;
	adev->dm.dc = dc;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	ctx->wq = amdgpu_dm_hpd_rx_irq_create_workqueue(adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx->wq);
	ctx->count = dc->caps.max_links;
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_destroy_hpd_rx_wq, ctx), 0);

	for (i = 0; i < dc->caps.max_links; i++)
		KUNIT_EXPECT_TRUE(test, ctx->wq[i].wq != NULL);
}

/* Tests for amdgpu_dm_hpd_rx_irq_work_suspend() */

/**
 * dm_test_hpd_rx_irq_work_suspend_null - Test suspend with no work queue
 * @test: The KUnit test context
 */
static void dm_test_hpd_rx_irq_work_suspend_null(struct kunit *test)
{
	struct amdgpu_display_manager *dm;

	dm = kunit_kzalloc(test, sizeof(*dm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm);

	/* A NULL hpd_rx_offload_wq must be a safe no-op (DC untouched). */
	amdgpu_dm_hpd_rx_irq_work_suspend(dm);
}

/**
 * dm_test_hpd_rx_irq_work_suspend_flushes - Test suspend flushes queues
 * @test: The KUnit test context
 */
static void dm_test_hpd_rx_irq_work_suspend_flushes(struct kunit *test)
{
	struct dm_test_hpd_rx_wq_ctx *ctx;
	struct amdgpu_device *adev;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	dc->caps.max_links = 2;
	adev->dm.dc = dc;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	ctx->wq = amdgpu_dm_hpd_rx_irq_create_workqueue(adev);
	KUNIT_ASSERT_NOT_NULL(test, ctx->wq);
	ctx->count = dc->caps.max_links;
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_destroy_hpd_rx_wq, ctx), 0);

	adev->dm.hpd_rx_offload_wq = ctx->wq;

	amdgpu_dm_hpd_rx_irq_work_suspend(&adev->dm);
}

/* Tests for CRTC-based irq state callbacks (no-CRTC early return) */

/**
 * dm_test_set_crtc_irq_state_no_crtc - Test crtc irq state with missing CRTC
 * @test: The KUnit test context
 */
static void dm_test_set_crtc_irq_state_no_crtc(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	/* mode_info.crtcs[0] is NULL -> returns 0 without dereferencing DC. */
	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_set_crtc_irq_state(adev, NULL, 0, AMDGPU_IRQ_STATE_ENABLE), 0);
}

/**
 * dm_test_set_pflip_irq_state_no_crtc - Test pflip irq state with missing CRTC
 * @test: The KUnit test context
 */
static void dm_test_set_pflip_irq_state_no_crtc(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_set_pflip_irq_state(adev, NULL, 0, AMDGPU_IRQ_STATE_DISABLE), 0);
}

/**
 * dm_test_set_vline0_irq_state_no_crtc - Test vline0 irq state with missing CRTC
 * @test: The KUnit test context
 */
static void dm_test_set_vline0_irq_state_no_crtc(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_set_vline0_irq_state(adev, NULL, 0, AMDGPU_IRQ_STATE_ENABLE), 0);
}

/**
 * dm_test_set_vupdate_irq_state_no_crtc - Test vupdate irq state with missing CRTC
 * @test: The KUnit test context
 */
static void dm_test_set_vupdate_irq_state_no_crtc(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_set_vupdate_irq_state(adev, NULL, 0, AMDGPU_IRQ_STATE_ENABLE), 0);
}

/* Tests for CRTC-based irq state callbacks (dm_irq_state happy path) */

/**
 * dm_test_set_crtc_irq_state_otg_disabled - Test crtc irq state with disabled OTG
 * @test: The KUnit test context
 */
static void dm_test_set_crtc_irq_state_otg_disabled(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	/* otg_inst == -1 short-circuits before computing the irq source. */
	acrtc->otg_inst = -1;
	adev->mode_info.crtcs[0] = acrtc;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_set_crtc_irq_state(adev, NULL, 0, AMDGPU_IRQ_STATE_ENABLE), 0);
}

/**
 * dm_test_set_crtc_irq_state_enable - Test crtc irq state reaches DC (enable)
 * @test: The KUnit test context
 */
static void dm_test_set_crtc_irq_state_enable(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	/*
	 * otg_inst >= 0 computes the irq source and reaches the NULL-safe
	 * dc_interrupt_set(); the ips_support branch is skipped (dc == NULL).
	 */
	acrtc->otg_inst = 3;
	adev->mode_info.crtcs[0] = acrtc;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_set_crtc_irq_state(adev, NULL, 0, AMDGPU_IRQ_STATE_ENABLE), 0);
}

/**
 * dm_test_set_pflip_irq_state_disable - Test pflip irq state reaches DC (disable)
 * @test: The KUnit test context
 */
static void dm_test_set_pflip_irq_state_disable(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	/* The disable state exercises the st == false path. */
	acrtc->otg_inst = 1;
	adev->mode_info.crtcs[0] = acrtc;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_set_pflip_irq_state(adev, NULL, 0, AMDGPU_IRQ_STATE_DISABLE), 0);
}

/**
 * dm_test_set_vline0_irq_state_enable - Test vline0 irq state reaches DC (enable)
 * @test: The KUnit test context
 */
static void dm_test_set_vline0_irq_state_enable(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	acrtc->otg_inst = 0;
	adev->mode_info.crtcs[0] = acrtc;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_set_vline0_irq_state(adev, NULL, 0, AMDGPU_IRQ_STATE_ENABLE), 0);
}

/**
 * dm_test_set_vupdate_irq_state_enable - Test vupdate irq state reaches DC (enable)
 * @test: The KUnit test context
 */
static void dm_test_set_vupdate_irq_state_enable(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	acrtc->otg_inst = 2;
	adev->mode_info.crtcs[0] = acrtc;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_set_vupdate_irq_state(adev, NULL, 0, AMDGPU_IRQ_STATE_ENABLE), 0);
}

/**
 * dm_test_set_crtc_irq_state_allows_idle - Test the idle-optimization branch
 * @test: The KUnit test context
 *
 * With a non-NULL DC that advertises IPS support and currently allows idle
 * optimizations, dm_irq_state() must call dc_allow_idle_optimizations() before
 * dc_interrupt_set(). disable_idle_power_optimizations makes that call a safe
 * early return, and per-source stub funcs let dc_interrupt_set() succeed.
 */
static void dm_test_set_crtc_irq_state_allows_idle(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;
	struct dal_logger *logger;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	dc = dm_test_alloc_dc_with_irq_service(test, &dm_test_irq_service_funcs_dcn10);

	/* DC_LOG_* dereferences ctx->logger->dev, so wire a real drm device. */
	logger = kunit_kzalloc(test, sizeof(*logger), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, logger);
	logger->dev = &adev->ddev;
	dc->ctx->logger = logger;

	dc->caps.ips_support = true;
	dc->idle_optimizations_allowed = true;
	/* Keep dc_allow_idle_optimizations() a safe early return. */
	dc->debug.disable_idle_power_optimizations = true;
	adev->dm.dc = dc;

	acrtc->otg_inst = 0;
	adev->mode_info.crtcs[0] = acrtc;

	KUNIT_EXPECT_EQ(test,
			amdgpu_dm_set_crtc_irq_state(adev, NULL, 0, AMDGPU_IRQ_STATE_ENABLE), 0);
}

/* Tests for amdgpu_dm_irq_immediate_work() */

/**
 * dm_test_irq_immediate_work_empty - Test immediate work on empty high table
 * @test: The KUnit test context
 */
static void dm_test_irq_immediate_work_empty(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	/* No registered high-context handlers: must be a safe no-op. */
	amdgpu_dm_irq_immediate_work(adev, DC_IRQ_SOURCE_HPD1);
}

/**
 * dm_test_irq_immediate_work_invokes_handler - Test immediate work calls handler
 * @test: The KUnit test context
 */
static void dm_test_irq_immediate_work_invokes_handler(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_interrupt_params int_params = { 0 };
	int count = 0;
	void *handler;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD1;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						   dm_test_irq_handler_count, &count);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	/* High-context handlers are invoked synchronously, in-place. */
	amdgpu_dm_irq_immediate_work(adev, DC_IRQ_SOURCE_HPD1);
	KUNIT_EXPECT_EQ(test, count, 1);

	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD1, dm_test_irq_handler_count);
}

/**
 * dm_test_irq_immediate_work_invokes_all - Test immediate work calls all handlers
 * @test: The KUnit test context
 */
static void dm_test_irq_immediate_work_invokes_all(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_interrupt_params int_params = { 0 };
	int count = 0;
	void *handler;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD2;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						   dm_test_irq_handler_count, &count);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						   dm_test_irq_handler_count, &count);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	/* Both registered high-context handlers must run. */
	amdgpu_dm_irq_immediate_work(adev, DC_IRQ_SOURCE_HPD2);
	KUNIT_EXPECT_EQ(test, count, 2);

	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD2, dm_test_irq_handler_count);
	amdgpu_dm_irq_unregister_interrupt(adev, DC_IRQ_SOURCE_HPD2, dm_test_irq_handler_count);
}

/* Tests for amdgpu_dm_irq_schedule_work() */

/**
 * dm_test_irq_schedule_work_empty - Test schedule work on empty low table
 * @test: The KUnit test context
 */
static void dm_test_irq_schedule_work_empty(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	/* Empty handler list: schedule_work returns immediately. */
	amdgpu_dm_irq_schedule_work(adev, DC_IRQ_SOURCE_HPD1);
}

/**
 * dm_test_irq_schedule_work_queues_handler - Test schedule work runs handler
 * @test: The KUnit test context
 */
static void dm_test_irq_schedule_work_queues_handler(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_interrupt_params int_params = { 0 };
	int count = 0;
	void *handler;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD1;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						   dm_test_irq_handler_count, &count);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	amdgpu_dm_irq_schedule_work(adev, DC_IRQ_SOURCE_HPD1);

	/*
	 * Low-context work runs asynchronously on system_highpri_wq.
	 * amdgpu_dm_irq_fini() flushes each pending work item before freeing
	 * the handlers, so the handler is guaranteed to have run afterwards.
	 */
	amdgpu_dm_irq_fini(adev);
	KUNIT_EXPECT_EQ(test, count, 1);
}

/**
 * dm_test_irq_schedule_work_requeue_fallback - Test the re-queue fallback path
 * @test: The KUnit test context
 *
 * The first schedule queues the handler's work item. Issuing a second
 * schedule before the work has run makes queue_work() fail for the
 * still-pending item, forcing amdgpu_dm_irq_schedule_work() into the fallback
 * that allocates and queues a fresh handler copy. Both work items run when
 * amdgpu_dm_irq_fini() flushes the queue, so the handler fires twice.
 */
static void dm_test_irq_schedule_work_requeue_fallback(struct kunit *test)
{
	struct dc_interrupt_params int_params = { 0 };
	struct amdgpu_device *adev;
	int count = 0;
	void *handler;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_HPD1;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						   dm_test_irq_handler_count, &count);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	amdgpu_dm_irq_schedule_work(adev, DC_IRQ_SOURCE_HPD1);
	amdgpu_dm_irq_schedule_work(adev, DC_IRQ_SOURCE_HPD1);

	amdgpu_dm_irq_fini(adev);
	KUNIT_EXPECT_EQ(test, count, 2);
}

/* Tests for amdgpu_dm_set_hpd_irq_state() */

/**
 * dm_test_set_hpd_irq_state_null_dc - Test HPD irq state with no DC
 * @test: The KUnit test context
 */
static void dm_test_set_hpd_irq_state_null_dc(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	/* dc_interrupt_set() is a no-op when dc is NULL, so both states
	 * return 0 without dereferencing the (absent) DC.
	 */
	KUNIT_EXPECT_EQ(test, amdgpu_dm_set_hpd_irq_state(adev, NULL, AMDGPU_HPD_1,
							  AMDGPU_IRQ_STATE_ENABLE), 0);
	KUNIT_EXPECT_EQ(test, amdgpu_dm_set_hpd_irq_state(adev, NULL, AMDGPU_HPD_1,
							  AMDGPU_IRQ_STATE_DISABLE), 0);
}

/* Tests for amdgpu_dm_set_dmub_outbox_irq_state() */

/**
 * dm_test_set_dmub_outbox_irq_state_null_dc - Test outbox irq state with no DC
 * @test: The KUnit test context
 */
static void dm_test_set_dmub_outbox_irq_state_null_dc(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_set_dmub_outbox_irq_state(adev, NULL, 0,
								  AMDGPU_IRQ_STATE_ENABLE), 0);
	KUNIT_EXPECT_EQ(test, amdgpu_dm_set_dmub_outbox_irq_state(adev, NULL, 0,
								  AMDGPU_IRQ_STATE_DISABLE), 0);
}

/* Tests for amdgpu_dm_set_dmub_trace_irq_state() */

/**
 * dm_test_set_dmub_trace_irq_state_null_dc - Test trace irq state with no DC
 * @test: The KUnit test context
 */
static void dm_test_set_dmub_trace_irq_state_null_dc(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_set_dmub_trace_irq_state(adev, NULL, 0,
								 AMDGPU_IRQ_STATE_ENABLE), 0);
	KUNIT_EXPECT_EQ(test, amdgpu_dm_set_dmub_trace_irq_state(adev, NULL, 0,
								 AMDGPU_IRQ_STATE_DISABLE), 0);
}

/* Tests for amdgpu_dm_outbox_init() */

/**
 * dm_test_outbox_init_null_dc - Test outbox init is a safe no-op with no DC
 * @test: The KUnit test context
 */
static void dm_test_outbox_init_null_dc(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	/* Single dc_interrupt_set() call must be skipped when dc is NULL. */
	amdgpu_dm_outbox_init(adev);
}

/* Tests for amdgpu_dm_hpd_init()/amdgpu_dm_hpd_fini() */

/**
 * dm_test_hpd_init_empty_connectors - Test HPD init with no connectors
 * @test: The KUnit test context
 */
static void dm_test_hpd_init_empty_connectors(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);

	/*
	 * With an empty connector list the per-connector loop is skipped and
	 * the initial clear loop relies on dc_interrupt_set() being a no-op
	 * for a NULL dc, so init must complete without touching the DC.
	 */
	amdgpu_dm_hpd_init(adev);
}

/**
 * dm_test_hpd_fini_empty_connectors - Test HPD fini with no connectors
 * @test: The KUnit test context
 */
static void dm_test_hpd_fini_empty_connectors(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);

	/* Empty connector list and disabled polling: fini is a safe no-op. */
	amdgpu_dm_hpd_fini(adev);
}

/**
 * dm_test_hpd_init_fini_with_connectors - Test HPD init/fini walk connectors
 * @test: The KUnit test context
 */
static void dm_test_hpd_init_fini_with_connectors(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct drm_connector *wbconn;
	struct amdgpu_device *adev;
	struct dc_link *link;

	adev = dm_kunit_alloc_adev(test);

	/*
	 * num_hpd = 0 forces irq_type >= num_hpd so the loop takes the HW
	 * fallback (dc_interrupt_set()) instead of amdgpu_irq_get(); with a
	 * NULL dc that fallback is a safe no-op.
	 */
	adev->mode_info.num_hpd = 0;

	/* A writeback connector must be skipped by the per-connector loop. */
	wbconn = kunit_kzalloc(test, sizeof(*wbconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, wbconn);
	KUNIT_ASSERT_EQ(test, drm_connector_init(&adev->ddev, wbconn, &dm_test_connector_funcs,
						 DRM_MODE_CONNECTOR_WRITEBACK), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_connector_cleanup, wbconn), 0);

	/* A DisplayPort connector with a dc_link exercises the loop body. */
	aconn = kunit_kzalloc(test, sizeof(*aconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconn);
	KUNIT_ASSERT_EQ(test, drm_connector_init(&adev->ddev, &aconn->base,
						 &dm_test_connector_funcs,
						 DRM_MODE_CONNECTOR_DisplayPort), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_connector_cleanup, &aconn->base), 0);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);
	link->irq_source_hpd = DC_IRQ_SOURCE_HPD1;
	link->irq_source_hpd_rx = DC_IRQ_SOURCE_HPD1RX;
	aconn->dc_link = link;

	/* DC is absent (NULL), so the HW writes are NULL-safe no-ops. */
	amdgpu_dm_hpd_init(adev);
	amdgpu_dm_hpd_fini(adev);
}

/**
 * dm_test_hpd_init_fini_analog_connector - Test HPD init/fini analog polling path
 * @test: The KUnit test context
 */
static void dm_test_hpd_init_fini_analog_connector(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct amdgpu_device *adev;
	struct dc_link *link;

	adev = dm_kunit_alloc_adev(test);
	adev->mode_info.num_hpd = 0;

	aconn = kunit_kzalloc(test, sizeof(*aconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconn);
	KUNIT_ASSERT_EQ(test, drm_connector_init(&adev->ddev, &aconn->base,
						 &dm_test_connector_funcs,
						 DRM_MODE_CONNECTOR_VGA), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_connector_cleanup, &aconn->base), 0);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);
	link->irq_source_hpd = DC_IRQ_SOURCE_HPD1;
	link->irq_source_hpd_rx = DC_IRQ_SOURCE_HPD1RX;
	/* An analog connector id makes the loop request polling. */
	link->link_id.id = CONNECTOR_ID_VGA;
	aconn->dc_link = link;

	/* use_polling becomes true, so init must enable KMS polling. */
	amdgpu_dm_hpd_init(adev);
	KUNIT_EXPECT_TRUE(test, adev->ddev.mode_config.poll_enabled);

	/* fini must tear the polling back down. */
	amdgpu_dm_hpd_fini(adev);
	KUNIT_EXPECT_FALSE(test, adev->ddev.mode_config.poll_enabled);
}

/**
 * dm_test_hpd_init_fini_irq_ref - Test HPD init/fini base-driver irq ref path
 * @test: The KUnit test context
 */
static void dm_test_hpd_init_fini_irq_ref(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct amdgpu_device *adev;
	struct dc_link *link;

	adev = dm_kunit_alloc_adev(test);

	/*
	 * num_hpd >= 1 makes irq_type (0) < num_hpd, so the loop takes the
	 * amdgpu_irq_get()/amdgpu_irq_put() branch instead of the
	 * dc_interrupt_set() fallback. The mock device has irq.installed ==
	 * false, so both calls fail early with -ENOENT (logging an error)
	 * without touching the base-driver irq state.
	 */
	adev->mode_info.num_hpd = 1;

	aconn = kunit_kzalloc(test, sizeof(*aconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconn);
	KUNIT_ASSERT_EQ(test, drm_connector_init(&adev->ddev, &aconn->base,
						 &dm_test_connector_funcs,
						 DRM_MODE_CONNECTOR_DisplayPort), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_connector_cleanup, &aconn->base), 0);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);
	link->irq_source_hpd = DC_IRQ_SOURCE_HPD1;
	link->irq_source_hpd_rx = DC_IRQ_SOURCE_INVALID;
	aconn->dc_link = link;

	amdgpu_dm_hpd_init(adev);
	amdgpu_dm_hpd_fini(adev);
}

/* Tests for dm_handle_hpd_rx_offload_work() */

/**
 * dm_test_hpd_rx_offload_work_no_connector - Test missing connector early exit
 * @test: The KUnit test context
 */
static void dm_test_hpd_rx_offload_work_no_connector(struct kunit *test)
{
	struct hpd_rx_irq_offload_work_queue *offload_wq;
	struct hpd_rx_irq_offload_work *offload_work;
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);

	offload_wq = kunit_kzalloc(test, sizeof(*offload_wq), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, offload_wq);

	offload_work = kzalloc(sizeof(*offload_work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, offload_work);
	offload_work->offload_wq = offload_wq;
	offload_work->adev = adev;
	INIT_WORK(&offload_work->work, dm_handle_hpd_rx_offload_work);

	dm_handle_hpd_rx_offload_work(&offload_work->work);
}

/**
 * dm_test_hpd_rx_offload_work_no_connection - Test no connection early exit
 * @test: The KUnit test context
 */
static void dm_test_hpd_rx_offload_work_no_connection(struct kunit *test)
{
	struct hpd_rx_irq_offload_work_queue *offload_wq;
	struct hpd_rx_irq_offload_work *offload_work;
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;
	struct amdgpu_device *adev;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	mutex_init(&adev->dm.dc_lock);

	offload_wq = kunit_kzalloc(test, sizeof(*offload_wq), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, offload_wq);

	aconn = dm_kunit_alloc_connector(test, adev, NULL);
	mutex_init(&aconn->hpd_lock);
	offload_wq->aconnector = aconn;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);

	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link_srv);
	link_srv->detect_connection_type = dm_test_detect_connection_none;
	dc->link_srv = link_srv;

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);
	link->dc = dc;
	aconn->dc_link = link;

	offload_work = kzalloc(sizeof(*offload_work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, offload_work);
	offload_work->offload_wq = offload_wq;
	offload_work->adev = adev;
	INIT_WORK(&offload_work->work, dm_handle_hpd_rx_offload_work);

	dm_handle_hpd_rx_offload_work(&offload_work->work);
}

/**
 * dm_test_hpd_rx_offload_work_automated_test - Test AUTOMATED_TEST branch
 * @test: The KUnit test context
 *
 * With a present connection and the AUTOMATED_TEST service-IRQ bit set, the
 * worker runs dc_link_dp_handle_automated_test() (stubbed via link_srv) and
 * writes the test response with core_link_write_dpcd(). timing_changed is left
 * false so force_connector_state() (which needs a registered DRM device) is
 * skipped, and aux_access_disabled makes the DPCD write a safe no-op.
 */
static void dm_test_hpd_rx_offload_work_automated_test(struct kunit *test)
{
	struct hpd_rx_irq_offload_work_queue *offload_wq;
	struct hpd_rx_irq_offload_work *offload_work;
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_link *link;
	struct dc *dc;

	dm_test_automated_test_count = 0;

	adev = dm_kunit_alloc_adev(test);
	mutex_init(&adev->dm.dc_lock);
	adev->reset_domain = kunit_kzalloc(test, sizeof(*adev->reset_domain),
					   GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->reset_domain);

	offload_wq = kunit_kzalloc(test, sizeof(*offload_wq), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, offload_wq);
	spin_lock_init(&offload_wq->offload_lock);

	aconn = dm_kunit_alloc_connector(test, adev, NULL);
	mutex_init(&aconn->hpd_lock);
	aconn->timing_changed = false;
	offload_wq->aconnector = aconn;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link_srv);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link_srv->detect_connection_type = dm_test_detect_connection_single;
	link_srv->dp_handle_automated_test = dm_test_dp_handle_automated_test;
	dc->link_srv = link_srv;
	dc->ctx = ctx;
	ctx->dc = dc;
	link->dc = dc;
	link->ctx = ctx;
	link->aux_access_disabled = true;
	link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;
	aconn->dc_link = link;

	offload_work = kzalloc(sizeof(*offload_work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, offload_work);
	offload_work->offload_wq = offload_wq;
	offload_work->adev = adev;
	offload_work->data.bytes.device_service_irq.bits.AUTOMATED_TEST = 1;
	INIT_WORK(&offload_work->work, dm_handle_hpd_rx_offload_work);

	dm_handle_hpd_rx_offload_work(&offload_work->work);

	KUNIT_EXPECT_EQ(test, dm_test_automated_test_count, 1);
}

/**
 * dm_test_hpd_rx_offload_work_link_loss - Test link-loss branch
 * @test: The KUnit test context
 *
 * With a present non-eDP connection and no service-IRQ bits set, the worker
 * takes the else-if link-loss path: dc_link_check_link_loss_status() and
 * dc_link_dp_allow_hpd_rx_irq() gate entry, then dc_link_dp_read_hpd_rx_irq_data()
 * returns DC_OK and a second link-loss check triggers dc_link_dp_handle_link_loss().
 * All four are stubbed via link_srv.
 */
static void dm_test_hpd_rx_offload_work_link_loss(struct kunit *test)
{
	struct hpd_rx_irq_offload_work_queue *offload_wq;
	struct hpd_rx_irq_offload_work *offload_work;
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_link *link;
	struct dc *dc;

	dm_test_handle_link_loss_count = 0;

	adev = dm_kunit_alloc_adev(test);
	mutex_init(&adev->dm.dc_lock);
	adev->reset_domain = kunit_kzalloc(test, sizeof(*adev->reset_domain),
					   GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->reset_domain);

	offload_wq = kunit_kzalloc(test, sizeof(*offload_wq), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, offload_wq);
	spin_lock_init(&offload_wq->offload_lock);

	aconn = dm_kunit_alloc_connector(test, adev, NULL);
	mutex_init(&aconn->hpd_lock);
	offload_wq->aconnector = aconn;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link_srv);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link_srv->detect_connection_type = dm_test_detect_connection_single;
	link_srv->dp_parse_link_loss_status = dm_test_dp_parse_link_loss_true;
	link_srv->dp_should_allow_hpd_rx_irq = dm_test_dp_should_allow_hpd_rx_irq_true;
	link_srv->dp_read_hpd_rx_irq_data = dm_test_dp_read_hpd_rx_irq_data_ok;
	link_srv->dp_handle_link_loss = dm_test_dp_handle_link_loss;
	dc->link_srv = link_srv;
	dc->ctx = ctx;
	ctx->dc = dc;
	link->dc = dc;
	link->ctx = ctx;
	link->connector_signal = SIGNAL_TYPE_DISPLAY_PORT;
	aconn->dc_link = link;

	offload_work = kzalloc(sizeof(*offload_work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, offload_work);
	offload_work->offload_wq = offload_wq;
	offload_work->adev = adev;
	INIT_WORK(&offload_work->work, dm_handle_hpd_rx_offload_work);

	dm_handle_hpd_rx_offload_work(&offload_work->work);

	KUNIT_EXPECT_EQ(test, dm_test_handle_link_loss_count, 1);
}

/* Tests for amdgpu_dm_hdmi_hpd_debounce_work() */

/**
 * dm_test_hdmi_hpd_debounce_detect_false - Test debounce false detect path
 * @test: The KUnit test context
 */
static void dm_test_hdmi_hpd_debounce_detect_false(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	mutex_init(&adev->dm.dc_lock);

	aconn = dm_kunit_alloc_connector(test, adev, NULL);
	mutex_init(&aconn->hpd_lock);
	INIT_DELAYED_WORK(&aconn->hdmi_hpd_debounce_work,
			  amdgpu_dm_hdmi_hpd_debounce_work);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link_srv);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link_srv->detect_link = dm_test_detect_link_false;
	dc->ctx = ctx;
	dc->link_srv = link_srv;
	ctx->dc = dc;
	link->dc = dc;
	link->ctx = ctx;
	aconn->dc_link = link;

	amdgpu_dm_hdmi_hpd_debounce_work(&aconn->hdmi_hpd_debounce_work.work);
	KUNIT_EXPECT_NULL(test, aconn->hdmi_prev_sink);
}

/**
 * dm_test_hdmi_hpd_debounce_reallow_idle - Test debounce idle/sink-release tail
 * @test: The KUnit test context
 *
 * With detection stubbed to return false, the if (ret) block is skipped, but
 * the function tail is still exercised: IPS support plus an idle-allowed dmub
 * makes reallow_idle true so dc_allow_idle_optimizations() runs on both entry
 * and exit (disable_idle_power_optimizations keeps those safe early returns),
 * and a cached hdmi_prev_sink is released and cleared.
 */
static void dm_test_hdmi_hpd_debounce_reallow_idle(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;
	struct dc_dmub_srv *dmub_srv;
	struct amdgpu_device *adev;
	struct dal_logger *logger;
	struct dc_context *ctx;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	mutex_init(&adev->dm.dc_lock);

	aconn = dm_kunit_alloc_connector(test, adev, NULL);
	mutex_init(&aconn->hpd_lock);
	INIT_DELAYED_WORK(&aconn->hdmi_hpd_debounce_work,
			  amdgpu_dm_hdmi_hpd_debounce_work);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link_srv);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);
	logger = kunit_kzalloc(test, sizeof(*logger), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, logger);
	dmub_srv = kunit_kzalloc(test, sizeof(*dmub_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dmub_srv);

	link_srv->detect_link = dm_test_detect_link_false;
	dc->ctx = ctx;
	dc->link_srv = link_srv;
	ctx->dc = dc;
	link->dc = dc;
	link->ctx = ctx;
	aconn->dc_link = link;

	/* DC_LOG_* dereferences ctx->logger->dev, so wire a real drm device. */
	logger->dev = &adev->ddev;
	dc->ctx->logger = logger;

	/* Make reallow_idle true and keep dc_allow_idle*() a safe early return. */
	dmub_srv->idle_allowed = true;
	dc->ctx->dmub_srv = dmub_srv;
	dc->caps.ips_support = true;
	dc->debug.disable_idle_power_optimizations = true;

	/* A cached previous sink must be released and cleared. */
	aconn->hdmi_prev_sink = dm_test_sink_create(link);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconn->hdmi_prev_sink);

	amdgpu_dm_hdmi_hpd_debounce_work(&aconn->hdmi_hpd_debounce_work.work);
	KUNIT_EXPECT_NULL(test, aconn->hdmi_prev_sink);
}

/* Tests for handle_hpd_irq()/handle_hpd_irq_helper() */

/**
 * dm_test_handle_hpd_irq_disabled - Test HPD helper returns when disabled
 * @test: The KUnit test context
 */
static void dm_test_handle_hpd_irq_disabled(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct dm_connector_state *state;
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	adev->dm.disable_hpd_irq = true;

	aconn = dm_kunit_alloc_connector(test, adev, NULL);
	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);
	aconn->base.state = &state->base;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	dc->ctx = ctx;
	ctx->dc = dc;
	link->ctx = ctx;
	aconn->dc_link = link;
	aconn->fake_enable = true;

	handle_hpd_irq_helper(aconn, DETECT_REASON_HPD);
	KUNIT_EXPECT_TRUE(test, aconn->fake_enable);

	handle_hpd_irq(aconn);
	KUNIT_EXPECT_TRUE(test, aconn->fake_enable);
}

/**
 * dm_test_handle_hpd_irq_helper_debounce_schedule - Test HDMI debounce branch
 * @test: The KUnit test context
 *
 * An HDMI link reporting a disconnect (connection type none) with a non-zero
 * debounce delay and a cached local_sink must take the debounce branch: it
 * caches local_sink in hdmi_prev_sink and schedules the delayed debounce work
 * instead of detecting immediately.
 */
static void dm_test_handle_hpd_irq_helper_debounce_schedule(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct dm_connector_state *state;
	struct link_service *link_srv;
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);

	aconn = dm_kunit_alloc_connector(test, adev, NULL);
	mutex_init(&aconn->hpd_lock);
	INIT_DELAYED_WORK(&aconn->hdmi_hpd_debounce_work,
			  amdgpu_dm_hdmi_hpd_debounce_work);

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);
	aconn->base.state = &state->base;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link_srv);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link_srv->detect_connection_type = dm_test_detect_connection_none;
	dc->ctx = ctx;
	dc->link_srv = link_srv;
	ctx->dc = dc;
	link->dc = dc;
	link->ctx = ctx;
	aconn->dc_link = link;

	/* HDMI signal + debounce delay + cached sink -> debounce branch. */
	aconn->hdmi_hpd_debounce_delay_ms = 100;
	link->connector_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	link->local_sink = dm_test_sink_create(link);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link->local_sink);

	handle_hpd_irq_helper(aconn, DETECT_REASON_HPD);

	/* local_sink is cached for later comparison by the debounce work. */
	KUNIT_EXPECT_PTR_EQ(test, aconn->hdmi_prev_sink, link->local_sink);

	cancel_delayed_work_sync(&aconn->hdmi_hpd_debounce_work);
	dm_test_sink_release(aconn->hdmi_prev_sink);
	dm_test_sink_release(link->local_sink);
}

/**
 * dm_test_handle_hpd_irq_helper_debounce_release_prev - Test stale prev_sink
 * @test: The KUnit test context
 *
 * When the debounce branch is taken and a stale hdmi_prev_sink is already
 * cached from a previous HPD, it must be released before caching the current
 * local_sink. This exercises the dc_sink_release() of the previous sink.
 */
static void dm_test_handle_hpd_irq_helper_debounce_release_prev(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct dm_connector_state *state;
	struct link_service *link_srv;
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);

	aconn = dm_kunit_alloc_connector(test, adev, NULL);
	mutex_init(&aconn->hpd_lock);
	INIT_DELAYED_WORK(&aconn->hdmi_hpd_debounce_work,
			  amdgpu_dm_hdmi_hpd_debounce_work);

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);
	aconn->base.state = &state->base;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link_srv);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link_srv->detect_connection_type = dm_test_detect_connection_none;
	dc->ctx = ctx;
	dc->link_srv = link_srv;
	ctx->dc = dc;
	link->dc = dc;
	link->ctx = ctx;
	aconn->dc_link = link;

	/* HDMI signal + debounce delay + cached sink -> debounce branch. */
	aconn->hdmi_hpd_debounce_delay_ms = 100;
	link->connector_signal = SIGNAL_TYPE_HDMI_TYPE_A;
	link->local_sink = dm_test_sink_create(link);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link->local_sink);

	/* Stale sink from a previous HPD must be released by the helper. */
	aconn->hdmi_prev_sink = dm_test_sink_create(link);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconn->hdmi_prev_sink);
	KUNIT_ASSERT_PTR_NE(test, aconn->hdmi_prev_sink, link->local_sink);

	handle_hpd_irq_helper(aconn, DETECT_REASON_HPD);

	/* Stale sink replaced by the current local_sink. */
	KUNIT_EXPECT_PTR_EQ(test, aconn->hdmi_prev_sink, link->local_sink);

	cancel_delayed_work_sync(&aconn->hdmi_hpd_debounce_work);
	dm_test_sink_release(aconn->hdmi_prev_sink);
	dm_test_sink_release(link->local_sink);
}

/**
 * dm_test_handle_hpd_irq_helper_detect_false - Test immediate detect branch
 * @test: The KUnit test context
 *
 * With no force, no debounce, and detection stubbed to report no connection,
 * the helper takes the else branch: dc_exit_ips_for_hw_access() is a safe
 * no-op (no IPS support) and dc_link_detect() returns false, so the connected
 * if (ret) block is skipped. fake_enable must still be cleared.
 */
static void dm_test_handle_hpd_irq_helper_detect_false(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct dm_connector_state *state;
	struct link_service *link_srv;
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	mutex_init(&adev->dm.dc_lock);

	aconn = dm_kunit_alloc_connector(test, adev, NULL);
	mutex_init(&aconn->hpd_lock);
	INIT_DELAYED_WORK(&aconn->hdmi_hpd_debounce_work,
			  amdgpu_dm_hdmi_hpd_debounce_work);

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, state);
	aconn->base.state = &state->base;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link_srv);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link_srv->detect_connection_type = dm_test_detect_connection_none;
	link_srv->detect_link = dm_test_detect_link_false;
	dc->ctx = ctx;
	dc->link_srv = link_srv;
	ctx->dc = dc;
	link->dc = dc;
	link->ctx = ctx;
	aconn->dc_link = link;
	aconn->fake_enable = true;

	/* No debounce delay and no force -> immediate-detect else branch. */
	handle_hpd_irq_helper(aconn, DETECT_REASON_HPD);

	KUNIT_EXPECT_FALSE(test, aconn->fake_enable);
}

/* Tests for handle_hpd_rx_irq()/schedule_hpd_rx_offload_work() */

/**
 * dm_test_handle_hpd_rx_irq_disabled - Test HPDRX handler returns when disabled
 * @test: The KUnit test context
 */
static void dm_test_handle_hpd_rx_irq_disabled(struct kunit *test)
{
	struct hpd_rx_irq_offload_work_queue *offload_wq;
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	adev->dm.disable_hpd_irq = true;

	offload_wq = kunit_kzalloc(test, sizeof(*offload_wq), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, offload_wq);
	adev->dm.hpd_rx_offload_wq = offload_wq;

	aconn = dm_kunit_alloc_connector(test, adev, NULL);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link_srv);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link_srv->dp_handle_hpd_rx_irq = dm_test_handle_hpd_rx_no_work;
	dc->ctx = ctx;
	dc->link_srv = link_srv;
	ctx->dc = dc;
	link->dc = dc;
	link->ctx = ctx;
	link->link_index = 0;
	aconn->dc_link = link;

	handle_hpd_rx_irq(aconn);
}

/**
 * dm_test_schedule_hpd_rx_offload_work - Test offload work is queued
 * @test: The KUnit test context
 */
static void dm_test_schedule_hpd_rx_offload_work(struct kunit *test)
{
	struct hpd_rx_irq_offload_work_queue *offload_wq;
	struct amdgpu_device *adev;
	union hpd_irq_data data = { 0 };

	adev = dm_kunit_alloc_adev(test);

	offload_wq = kunit_kzalloc(test, sizeof(*offload_wq), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, offload_wq);
	offload_wq->wq = create_singlethread_workqueue("dm_irq_test_hpd_rx");
	KUNIT_ASSERT_NOT_NULL(test, offload_wq->wq);

	schedule_hpd_rx_offload_work(adev, offload_wq, data);
	flush_workqueue(offload_wq->wq);
	destroy_workqueue(offload_wq->wq);
}

static void dm_test_destroy_offload_wq(void *data)
{
	struct workqueue_struct *wq = data;

	flush_workqueue(wq);
	destroy_workqueue(wq);
}

/*
 * Build an aconnector wired for handle_hpd_rx_irq(): HPD enabled, an MST-root
 * connector on an MST-branch link so the post-detect block and drm_dp_cec_irq
 * are skipped, and a flushed offload work queue. Caller sets the link_srv
 * stubs (dp_handle_hpd_rx_irq and, if needed, dp_should_allow_hpd_rx_irq).
 */
static struct amdgpu_dm_connector *dm_test_setup_hpd_rx_irq(struct kunit *test,
							    struct link_service **link_srv_out)
{
	struct hpd_rx_irq_offload_work_queue *offload_wq;
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	mutex_init(&adev->dm.dc_lock);

	offload_wq = kunit_kzalloc(test, sizeof(*offload_wq), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, offload_wq);
	spin_lock_init(&offload_wq->offload_lock);
	offload_wq->wq = create_singlethread_workqueue("dm_irq_test_hpd_rx");
	KUNIT_ASSERT_NOT_NULL(test, offload_wq->wq);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_destroy_offload_wq,
							offload_wq->wq), 0);
	adev->dm.hpd_rx_offload_wq = offload_wq;

	aconn = dm_kunit_alloc_connector(test, adev, NULL);
	mutex_init(&aconn->hpd_lock);
	aconn->mst_mgr.mst_state = true;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link_srv);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	dc->ctx = ctx;
	dc->link_srv = link_srv;
	ctx->dc = dc;
	link->dc = dc;
	link->ctx = ctx;
	link->link_index = 0;
	link->type = dc_connection_mst_branch;
	aconn->dc_link = link;

	*link_srv_out = link_srv;

	return aconn;
}

/**
 * dm_test_handle_hpd_rx_irq_no_left_work - Test HPDRX early out (no left work)
 * @test: The KUnit test context
 *
 * When dc_link_handle_hpd_rx_irq() reports no left-over work, the handler
 * jumps to out and returns without scheduling any offload work.
 */
static void dm_test_handle_hpd_rx_irq_no_left_work(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;

	aconn = dm_test_setup_hpd_rx_irq(test, &link_srv);
	link_srv->dp_handle_hpd_rx_irq = dm_test_handle_hpd_rx_no_work;

	handle_hpd_rx_irq(aconn);
}

/**
 * dm_test_handle_hpd_rx_irq_automated_test - Test HPDRX automated-test path
 * @test: The KUnit test context
 *
 * The AUTOMATED_TEST device-service bit must schedule offload work and jump to
 * out before the allow-hpd-rx-irq checks.
 */
static void dm_test_handle_hpd_rx_irq_automated_test(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;

	aconn = dm_test_setup_hpd_rx_irq(test, &link_srv);
	link_srv->dp_handle_hpd_rx_irq = dm_test_handle_hpd_rx_automated;

	handle_hpd_rx_irq(aconn);
}

/**
 * dm_test_handle_hpd_rx_irq_msg_rdy - Test HPDRX MST message-ready path
 * @test: The KUnit test context
 *
 * With HPD RX IRQs allowed and an UP_REQ_MSG_RDY bit set, the handler must take
 * the MST message-ready branch, mark is_handling_mst_msg_rdy_event and schedule
 * offload work.
 */
static void dm_test_handle_hpd_rx_irq_msg_rdy(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;

	aconn = dm_test_setup_hpd_rx_irq(test, &link_srv);
	link_srv->dp_handle_hpd_rx_irq = dm_test_handle_hpd_rx_msg_rdy;
	link_srv->dp_should_allow_hpd_rx_irq = dm_test_allow_hpd_rx_irq_true;

	handle_hpd_rx_irq(aconn);

	KUNIT_EXPECT_TRUE(test, drm_to_adev(aconn->base.dev)
				->dm.hpd_rx_offload_wq->is_handling_mst_msg_rdy_event);
}

/**
 * dm_test_handle_hpd_rx_irq_link_loss - Test HPDRX link-loss path
 * @test: The KUnit test context
 *
 * With HPD RX IRQs allowed and a reported link loss (no message-ready bits),
 * the handler must take the link-loss branch, mark is_handling_link_loss and
 * schedule offload work.
 */
static void dm_test_handle_hpd_rx_irq_link_loss(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;

	aconn = dm_test_setup_hpd_rx_irq(test, &link_srv);
	link_srv->dp_handle_hpd_rx_irq = dm_test_handle_hpd_rx_link_loss;
	link_srv->dp_should_allow_hpd_rx_irq = dm_test_allow_hpd_rx_irq_true;

	handle_hpd_rx_irq(aconn);

	KUNIT_EXPECT_TRUE(test, drm_to_adev(aconn->base.dev)
				->dm.hpd_rx_offload_wq->is_handling_link_loss);
}

/* Tests for dmub_hpd_callback()/dmub_hpd_sense_callback() */

/**
 * dm_test_dmub_hpd_callback_null_inputs - Test DMUB callback null inputs
 * @test: The KUnit test context
 */
static void dm_test_dmub_hpd_callback_null_inputs(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);

	dmub_hpd_callback(NULL, NULL);
	dmub_hpd_callback(adev, NULL);
}

/**
 * dm_test_dmub_hpd_callback_invalid_index - Test DMUB callback index check
 * @test: The KUnit test context
 */
static void dm_test_dmub_hpd_callback_invalid_index(struct kunit *test)
{
	struct dmub_notification notify = { 0 };
	struct amdgpu_device *adev;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	adev->dm.dc = dc;
	notify.link_index = 1;

	dmub_hpd_callback(adev, &notify);
}

/**
 * dm_test_dmub_hpd_callback_empty_connectors - Test DMUB callback without match
 * @test: The KUnit test context
 */
static void dm_test_dmub_hpd_callback_empty_connectors(struct kunit *test)
{
	struct dmub_notification notify = { 0 };
	struct amdgpu_device *adev;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	adev->dm.dc = dc;
	adev->dm.ddev = &adev->ddev;
	dc->link_count = 1;
	notify.type = DMUB_NOTIFICATION_HPD_SENSE_NOTIFY;

	dmub_hpd_callback(adev, &notify);
	dmub_hpd_sense_callback(adev, &notify);
}

/**
 * dm_test_dmub_hpd_callback_unknown_type_match - Test DMUB callback matched
 *						   connector, unknown type
 * @test: The KUnit test context
 *
 * A writeback connector must be skipped (continue) and a non-writeback
 * connector whose dc_link matches the notification link must be selected.
 * An unrecognized notification type takes the "unknown" warn branch, and the
 * post-loop dispatch is a no-op (neither HPD nor HPD_IRQ), so no real DC/MST
 * handler runs.
 */
static void dm_test_dmub_hpd_callback_unknown_type_match(struct kunit *test)
{
	struct dmub_notification notify = { 0 };
	struct amdgpu_dm_connector *aconn;
	struct drm_connector *wbconn;
	struct amdgpu_device *adev;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	adev->dm.dc = dc;
	adev->dm.ddev = &adev->ddev;
	dc->link_count = 1;

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);
	dc->links[0] = link;

	/* A writeback connector must be skipped by the loop. */
	wbconn = kunit_kzalloc(test, sizeof(*wbconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, wbconn);
	KUNIT_ASSERT_EQ(test, drm_connector_init(&adev->ddev, wbconn, &dm_test_connector_funcs,
						 DRM_MODE_CONNECTOR_WRITEBACK), 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_connector_cleanup, wbconn), 0);

	/* A DisplayPort connector whose dc_link matches drives the match branch. */
	aconn = kunit_kzalloc(test, sizeof(*aconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconn);
	KUNIT_ASSERT_EQ(test, drm_connector_init(&adev->ddev, &aconn->base,
						 &dm_test_connector_funcs,
						 DRM_MODE_CONNECTOR_DisplayPort), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_connector_cleanup, &aconn->base), 0);
	aconn->dc_link = link;

	notify.link_index = 0;
	notify.type = DMUB_NOTIFICATION_HPD_SENSE_NOTIFY;

	dmub_hpd_callback(adev, &notify);
}

/**
 * dm_test_dmub_hpd_callback_hpd_type - Test DMUB callback HPD type dispatch
 * @test: The KUnit test context
 *
 * A matched connector with notification type DMUB_NOTIFICATION_HPD logs the
 * HPD callback and dispatches to handle_hpd_irq_helper(). Detection is stubbed
 * to report no connection and link detect to fail, so the connected branches
 * are skipped and no real DC/DRM hotplug path runs.
 */
static void dm_test_dmub_hpd_callback_hpd_type(struct kunit *test)
{
	struct dmub_notification notify = { 0 };
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	mutex_init(&adev->dm.dc_lock);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link_srv);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link_srv->detect_connection_type = dm_test_detect_connection_none;
	link_srv->detect_link = dm_test_detect_link_false;
	dc->ctx = ctx;
	dc->link_srv = link_srv;
	ctx->dc = dc;
	link->dc = dc;
	link->ctx = ctx;

	adev->dm.dc = dc;
	adev->dm.ddev = &adev->ddev;
	dc->link_count = 1;
	dc->links[0] = link;

	aconn = kunit_kzalloc(test, sizeof(*aconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconn);
	KUNIT_ASSERT_EQ(test, drm_connector_init(&adev->ddev, &aconn->base,
						 &dm_test_connector_funcs,
						 DRM_MODE_CONNECTOR_DisplayPort), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_connector_cleanup, &aconn->base), 0);
	mutex_init(&aconn->hpd_lock);
	INIT_DELAYED_WORK(&aconn->hdmi_hpd_debounce_work,
			  amdgpu_dm_hdmi_hpd_debounce_work);
	aconn->dc_link = link;

	notify.link_index = 0;
	notify.type = DMUB_NOTIFICATION_HPD;

	dmub_hpd_callback(adev, &notify);
}

/**
 * dm_test_dmub_hpd_callback_hpd_irq_type - Test DMUB callback HPD_IRQ dispatch
 * @test: The KUnit test context
 *
 * A matched connector with notification type DMUB_NOTIFICATION_HPD_IRQ logs
 * the HPD RX callback and dispatches to handle_hpd_rx_irq(). The HPD RX handler
 * stub reports no left work, so the function returns through the early goto
 * without scheduling offload work; an MST-branch link skips the trailing
 * drm_dp_cec_irq().
 */
static void dm_test_dmub_hpd_callback_hpd_irq_type(struct kunit *test)
{
	struct hpd_rx_irq_offload_work_queue *offload_wq;
	struct dmub_notification notify = { 0 };
	struct amdgpu_dm_connector *aconn;
	struct link_service *link_srv;
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	mutex_init(&adev->dm.dc_lock);

	offload_wq = kunit_kzalloc(test, sizeof(*offload_wq), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, offload_wq);
	spin_lock_init(&offload_wq->offload_lock);
	adev->dm.hpd_rx_offload_wq = offload_wq;

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link_srv);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link_srv->dp_handle_hpd_rx_irq = dm_test_handle_hpd_rx_no_work;
	dc->ctx = ctx;
	dc->link_srv = link_srv;
	ctx->dc = dc;
	link->dc = dc;
	link->ctx = ctx;
	link->link_index = 0;
	link->type = dc_connection_mst_branch;

	adev->dm.dc = dc;
	adev->dm.ddev = &adev->ddev;
	dc->link_count = 1;
	dc->links[0] = link;

	aconn = kunit_kzalloc(test, sizeof(*aconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconn);
	KUNIT_ASSERT_EQ(test, drm_connector_init(&adev->ddev, &aconn->base,
						 &dm_test_connector_funcs,
						 DRM_MODE_CONNECTOR_DisplayPort), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_connector_cleanup, &aconn->base), 0);
	mutex_init(&aconn->hpd_lock);
	aconn->mst_mgr.mst_state = true;
	aconn->dc_link = link;

	notify.link_index = 0;
	notify.type = DMUB_NOTIFICATION_HPD_IRQ;

	dmub_hpd_callback(adev, &notify);
}

/* Tests for amdgpu_dm_register_hpd_handlers() */

/**
 * dm_test_register_hpd_handlers_empty - Test HPD registration with no connectors
 * @test: The KUnit test context
 */
static void dm_test_register_hpd_handlers_empty(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	adev->dm.dc = dc;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_register_hpd_handlers(adev), 0);
}

/**
 * dm_test_register_hpd_handlers_valid - Test HPD/HPDRX registration succeeds
 * @test: The KUnit test context
 */
static void dm_test_register_hpd_handlers_valid(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct amdgpu_device *adev;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	adev->dm.dc = dc;

	aconn = kunit_kzalloc(test, sizeof(*aconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconn);
	KUNIT_ASSERT_EQ(test, drm_connector_init(&adev->ddev, &aconn->base,
						 &dm_test_connector_funcs,
						 DRM_MODE_CONNECTOR_DisplayPort), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_connector_cleanup, &aconn->base), 0);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);
	link->irq_source_hpd = DC_IRQ_SOURCE_HPD1;
	link->irq_source_hpd_rx = DC_IRQ_SOURCE_HPD1RX;
	aconn->dc_link = link;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_register_hpd_handlers(adev), 0);
	KUNIT_EXPECT_FALSE(test,
			   list_empty(&adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD1]));
	KUNIT_EXPECT_FALSE(test,
			   list_empty(&adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_HPD1RX]));

	amdgpu_dm_irq_fini(adev);
}

/**
 * dm_test_register_hpd_handlers_invalid_hpd - Test invalid HPD source fails
 * @test: The KUnit test context
 */
static void dm_test_register_hpd_handlers_invalid_hpd(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct amdgpu_device *adev;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	adev->dm.dc = dc;

	aconn = kunit_kzalloc(test, sizeof(*aconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconn);
	KUNIT_ASSERT_EQ(test, drm_connector_init(&adev->ddev, &aconn->base,
						 &dm_test_connector_funcs,
						 DRM_MODE_CONNECTOR_DisplayPort), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_connector_cleanup, &aconn->base), 0);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);
	link->irq_source_hpd = DC_IRQ_SOURCE_HPD1RX;
	link->irq_source_hpd_rx = DC_IRQ_SOURCE_INVALID;
	aconn->dc_link = link;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_register_hpd_handlers(adev), -EINVAL);
	amdgpu_dm_irq_fini(adev);
}

/**
 * dm_test_register_hpd_handlers_invalid_hpd_rx - Test invalid HPDRX source fails
 * @test: The KUnit test context
 */
static void dm_test_register_hpd_handlers_invalid_hpd_rx(struct kunit *test)
{
	struct amdgpu_dm_connector *aconn;
	struct amdgpu_device *adev;
	struct dc_link *link;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	adev->dm.dc = dc;

	aconn = kunit_kzalloc(test, sizeof(*aconn), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, aconn);
	KUNIT_ASSERT_EQ(test, drm_connector_init(&adev->ddev, &aconn->base,
						 &dm_test_connector_funcs,
						 DRM_MODE_CONNECTOR_DisplayPort), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_test_connector_cleanup, &aconn->base), 0);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);
	link->irq_source_hpd = DC_IRQ_SOURCE_INVALID;
	link->irq_source_hpd_rx = DC_IRQ_SOURCE_HPD1;
	aconn->dc_link = link;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_register_hpd_handlers(adev), -EINVAL);
	amdgpu_dm_irq_fini(adev);
}

/**
 * dm_test_register_hpd_handlers_dmub_outbox - Test DMUB outbox callbacks register
 * @test: The KUnit test context
 *
 * Enables DMUB outbox support so dc_is_dmub_outbox_supported() returns true,
 * exercising the dm_register_dmub_notify_callback() block. Asserts that the
 * HPD, HPD_IRQ and HPD_SENSE_NOTIFY callbacks are registered.
 */
static void dm_test_register_hpd_handlers_dmub_outbox(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);
	dc = dm_test_alloc_dc_with_ctx(test);
	adev->dm.dc = dc;

	/* Make dc_is_dmub_outbox_supported() return true. */
	dc->caps.dmcub_support = true;
	dc->ctx->asic_id.chip_family = AMDGPU_FAMILY_GC_11_0_1;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_register_hpd_handlers(adev), 0);
	KUNIT_EXPECT_PTR_EQ(test, adev->dm.dmub_callback[DMUB_NOTIFICATION_HPD],
			    (dmub_notify_interrupt_callback_t)dmub_hpd_callback);
	KUNIT_EXPECT_PTR_EQ(test, adev->dm.dmub_callback[DMUB_NOTIFICATION_HPD_IRQ],
			    (dmub_notify_interrupt_callback_t)dmub_hpd_callback);
	KUNIT_EXPECT_PTR_EQ(test,
			    adev->dm.dmub_callback[DMUB_NOTIFICATION_HPD_SENSE_NOTIFY],
			    (dmub_notify_interrupt_callback_t)dmub_hpd_sense_callback);

	amdgpu_dm_irq_fini(adev);
}

/* Tests for CRTC/pflip/vupdate high IRQ callbacks */

/**
 * dm_test_pflip_high_irq_no_crtc - Test pflip high IRQ with no matching CRTC
 * @test: The KUnit test context
 */
static void dm_test_pflip_high_irq_no_crtc(struct kunit *test)
{
	struct common_irq_params params = { 0 };
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	params.adev = adev;
	params.irq_src = (enum dc_irq_source)IRQ_TYPE_PFLIP;

	dm_pflip_high_irq(&params);
}

/**
 * dm_test_pflip_high_irq_not_submitted - Test pflip high IRQ early status exit
 * @test: The KUnit test context
 */
static void dm_test_pflip_high_irq_not_submitted(struct kunit *test)
{
	struct common_irq_params params = { 0 };
	struct amdgpu_crtc *acrtc;
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	INIT_LIST_HEAD(&acrtc->base.head);
	acrtc->base.dev = &adev->ddev;
	acrtc->otg_inst = 0;
	acrtc->pflip_status = AMDGPU_FLIP_NONE;
	list_add_tail(&acrtc->base.head, &adev->ddev.mode_config.crtc_list);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test,
				dm_test_crtc_list_del, acrtc), 0);

	params.adev = adev;
	params.irq_src = (enum dc_irq_source)IRQ_TYPE_PFLIP;

	dm_pflip_high_irq(&params);
	KUNIT_EXPECT_EQ(test, acrtc->pflip_status, AMDGPU_FLIP_NONE);
}

/**
 * dm_test_vupdate_high_irq_no_crtc - Test vupdate high IRQ with no CRTC
 * @test: The KUnit test context
 */
static void dm_test_vupdate_high_irq_no_crtc(struct kunit *test)
{
	struct common_irq_params params = { 0 };
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	params.adev = adev;
	params.irq_src = (enum dc_irq_source)IRQ_TYPE_VUPDATE;

	dm_vupdate_high_irq(&params);
}

/**
 * dm_test_crtc_high_irq_no_crtc - Test crtc high IRQ with no CRTC
 * @test: The KUnit test context
 */
static void dm_test_crtc_high_irq_no_crtc(struct kunit *test)
{
	struct common_irq_params params = { 0 };
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	params.adev = adev;
	params.irq_src = (enum dc_irq_source)IRQ_TYPE_VBLANK;

	dm_crtc_high_irq(&params);
}

/**
 * dm_test_crtc_high_irq_vrr_pre_ai - Test crtc high IRQ VRR path on pre-AI ASIC
 * @test: The KUnit test context
 *
 * With a matching CRTC, no writeback, VRR active (so the !vrr_active vblank
 * handler is skipped) and a pre-AI family, the handler runs through CRC
 * handling and returns before the freesync section.
 */
static void dm_test_crtc_high_irq_vrr_pre_ai(struct kunit *test)
{
	struct common_irq_params params = { 0 };
	struct amdgpu_crtc *acrtc;
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	INIT_LIST_HEAD(&acrtc->base.head);
	acrtc->base.dev = &adev->ddev;
	acrtc->otg_inst = 0;
	/* VRR active so the !vrr_active vblank handler is skipped. */
	acrtc->dm_irq_params.freesync_config.state = VRR_STATE_ACTIVE_VARIABLE;
	list_add_tail(&acrtc->base.head, &adev->ddev.mode_config.crtc_list);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test,
				dm_test_crtc_list_del, acrtc), 0);

	/* Pre-AI family returns right after CRC handling. */
	adev->family = AMDGPU_FAMILY_SI;

	params.adev = adev;
	params.irq_src = (enum dc_irq_source)IRQ_TYPE_VBLANK;

	dm_crtc_high_irq(&params);
}

/**
 * dm_test_crtc_high_irq_vrr_ai_no_stream - Test crtc high IRQ AI path, no stream
 * @test: The KUnit test context
 *
 * On an AI+ family the handler runs the post-CRC freesync section. With no
 * stream and no pending flip, both inner blocks are skipped and the handler
 * completes through the event-lock critical section, leaving pflip_status
 * untouched.
 */
static void dm_test_crtc_high_irq_vrr_ai_no_stream(struct kunit *test)
{
	struct common_irq_params params = { 0 };
	struct amdgpu_crtc *acrtc;
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acrtc);

	INIT_LIST_HEAD(&acrtc->base.head);
	acrtc->base.dev = &adev->ddev;
	acrtc->otg_inst = 0;
	acrtc->pflip_status = AMDGPU_FLIP_NONE;
	/* VRR active so the !vrr_active vblank handler is skipped. */
	acrtc->dm_irq_params.freesync_config.state = VRR_STATE_ACTIVE_VARIABLE;
	list_add_tail(&acrtc->base.head, &adev->ddev.mode_config.crtc_list);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test,
				dm_test_crtc_list_del, acrtc), 0);

	/* AI+ family runs the freesync section; no stream skips it. */
	adev->family = AMDGPU_FAMILY_AI;

	params.adev = adev;
	params.irq_src = (enum dc_irq_source)IRQ_TYPE_VBLANK;

	dm_crtc_high_irq(&params);
	KUNIT_EXPECT_EQ(test, acrtc->pflip_status, AMDGPU_FLIP_NONE);
}

/* Tests for dm_handle_hpd_work() */

/**
 * dm_test_handle_hpd_work_out_of_range - Test HPD work frees unknown notification
 * @test: The KUnit test context
 */
static void dm_test_handle_hpd_work_out_of_range(struct kunit *test)
{
	struct dmub_hpd_work *hpd_work;
	struct amdgpu_device *adev;

	adev = dm_kunit_alloc_adev(test);
	hpd_work = kzalloc(sizeof(*hpd_work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hpd_work);
	hpd_work->dmub_notify = kzalloc(sizeof(*hpd_work->dmub_notify), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, hpd_work->dmub_notify);
	hpd_work->dmub_notify->type =
		(enum dmub_notification_type)ARRAY_SIZE(adev->dm.dmub_callback);
	hpd_work->adev = adev;
	INIT_WORK(&hpd_work->handle_hpd_work, dm_handle_hpd_work);

	dm_handle_hpd_work(&hpd_work->handle_hpd_work);
}

/* Tests for dm_dmub_outbox1_low_irq() */

/**
 * dm_test_dmub_outbox1_low_irq_empty - Test outbox low IRQ with empty trace queue
 * @test: The KUnit test context
 */
static void dm_test_dmub_outbox1_low_irq_empty(struct kunit *test)
{
	struct common_irq_params params = { 0 };
	struct dc_dmub_srv *dc_dmub_srv;
	struct amdgpu_device *adev;
	struct dmub_srv *dmub;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	dc = dm_test_alloc_dc_with_ctx(test);
	dc_dmub_srv = kunit_kzalloc(test, sizeof(*dc_dmub_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_dmub_srv);
	dmub = kunit_kzalloc(test, sizeof(*dmub), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dmub);

	dmub->hw_funcs.get_outbox0_wptr = dm_test_dmub_get_outbox0_wptr;
	dc_dmub_srv->dmub = dmub;
	dc->ctx->dmub_srv = dc_dmub_srv;
	adev->dm.dc = dc;
	params.adev = adev;
	params.irq_src = DC_IRQ_SOURCE_DMCUB_OUTBOX;

	dm_dmub_outbox1_low_irq(&params);
}

/*
 * dm_test_alloc_adev_outbox_notify - Build an adev wired for DMUB outbox
 * notification handling.
 *
 * Configures dc/dc_dmub_srv/dmub so that the trace queue is empty and
 * dc_enable_dmub_notifications() returns true, allowing the notification
 * handling block of dm_dmub_outbox1_low_irq() to execute. The outbox1 ring
 * buffer is left empty, so each notification read returns
 * DMUB_NOTIFICATION_NO_DATA with no pending notification.
 */
static struct amdgpu_device *dm_test_alloc_adev_outbox_notify(struct kunit *test)
{
	struct dc_dmub_srv *dc_dmub_srv;
	struct amdgpu_device *adev;
	struct dmub_srv *dmub;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	dc = dm_test_alloc_dc_with_ctx(test);
	dc_dmub_srv = kunit_kzalloc(test, sizeof(*dc_dmub_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_dmub_srv);
	dmub = kunit_kzalloc(test, sizeof(*dmub), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dmub);

	dmub->hw_init = true;
	dmub->hw_funcs.get_outbox0_wptr = dm_test_dmub_get_outbox0_wptr;
	dmub->hw_funcs.get_outbox1_wptr = dm_test_dmub_get_outbox1_wptr;
	dc_dmub_srv->dmub = dmub;
	dc->ctx->dmub_srv = dc_dmub_srv;

	/* Make dc_enable_dmub_notifications() return true. */
	dc->caps.dmcub_support = true;
	dc->ctx->asic_id.chip_family = AMDGPU_FAMILY_GC_11_0_1;

	adev->dm.dc = dc;

	return adev;
}

/**
 * dm_test_dmub_outbox1_low_irq_no_handler - Test notification with no handler
 * @test: The KUnit test context
 *
 * Notifications are enabled but no callback is registered for the returned
 * notification type, exercising the skip-with-warning path.
 */
static void dm_test_dmub_outbox1_low_irq_no_handler(struct kunit *test)
{
	struct common_irq_params params = { 0 };
	struct amdgpu_device *adev;

	adev = dm_test_alloc_adev_outbox_notify(test);
	params.adev = adev;
	params.irq_src = DC_IRQ_SOURCE_DMCUB_OUTBOX;

	dm_dmub_outbox1_low_irq(&params);
}

/**
 * dm_test_dmub_outbox1_low_irq_direct_callback - Test direct callback dispatch
 * @test: The KUnit test context
 *
 * Notifications are enabled with a registered callback and thread offload
 * disabled, so the callback is invoked directly from the IRQ handler.
 */
static void dm_test_dmub_outbox1_low_irq_direct_callback(struct kunit *test)
{
	struct common_irq_params params = { 0 };
	struct amdgpu_device *adev;

	adev = dm_test_alloc_adev_outbox_notify(test);
	adev->dm.dmub_callback[DMUB_NOTIFICATION_NO_DATA] = dm_test_dmub_notify_callback;
	adev->dm.dmub_thread_offload[DMUB_NOTIFICATION_NO_DATA] = false;
	params.adev = adev;
	params.irq_src = DC_IRQ_SOURCE_DMCUB_OUTBOX;

	dm_test_dmub_notify_count = 0;

	dm_dmub_outbox1_low_irq(&params);

	KUNIT_EXPECT_EQ(test, dm_test_dmub_notify_count, 1);
}

/**
 * dm_test_dmub_outbox1_low_irq_offload - Test offloaded callback dispatch
 * @test: The KUnit test context
 *
 * Notifications are enabled with a registered callback and thread offload
 * enabled, so the callback is dispatched via the delayed HPD work queue.
 */
static void dm_test_dmub_outbox1_low_irq_offload(struct kunit *test)
{
	struct common_irq_params params = { 0 };
	struct amdgpu_device *adev;

	adev = dm_test_alloc_adev_outbox_notify(test);
	adev->dm.dmub_callback[DMUB_NOTIFICATION_NO_DATA] = dm_test_dmub_notify_callback;
	adev->dm.dmub_thread_offload[DMUB_NOTIFICATION_NO_DATA] = true;
	adev->dm.delayed_hpd_wq = create_singlethread_workqueue("dm_irq_test_outbox");
	KUNIT_ASSERT_NOT_NULL(test, adev->dm.delayed_hpd_wq);
	params.adev = adev;
	params.irq_src = DC_IRQ_SOURCE_DMCUB_OUTBOX;

	dm_test_dmub_notify_count = 0;

	dm_dmub_outbox1_low_irq(&params);

	flush_workqueue(adev->dm.delayed_hpd_wq);
	destroy_workqueue(adev->dm.delayed_hpd_wq);

	KUNIT_EXPECT_EQ(test, dm_test_dmub_notify_count, 1);
}


/**
 * dm_test_dce110_register_irq_handlers_rejects_uninitialized_sources - Test DCE110 error
 * @test: The KUnit test context
 */
static void dm_test_dce110_register_irq_handlers_rejects_uninitialized_sources(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	dc = dm_test_alloc_dc_with_ctx(test);
	adev->dm.dc = dc;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_dce110_register_irq_handlers(adev), -EINVAL);
}

/**
 * dm_test_dce110_register_irq_handlers_one_crtc - Test DCE110 with 1 CRTC
 * @test: The KUnit test context
 *
 * Exercises the VBLANK, VUPDATE, PFLIP and HPD for-loop bodies with a
 * fake IRQ service that maps source IDs to DC IRQ sources.
 */
static void dm_test_dce110_register_irq_handlers_one_crtc(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_free_irq_sources,
							 adev), 0);
	dc = dm_test_alloc_dc_with_irq_service(test, &dm_test_irq_service_funcs_dce110);
	dc->ctx->dce_version = DCE_VERSION_11_0;
	adev->dm.dc = dc;
	adev->mode_info.num_crtc = 1;
	adev->mode_info.num_hpd = 1;
	amdgpu_dm_set_irq_funcs(adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_dce110_register_irq_handlers(adev), 0);

	/* Verify VBLANK params were populated */
	KUNIT_EXPECT_EQ(test, (int)adev->dm.vblank_params[0].irq_src,
			(int)DC_IRQ_SOURCE_VBLANK1);
	KUNIT_EXPECT_PTR_EQ(test, adev->dm.vblank_params[0].adev, adev);

	/* Verify VUPDATE params were populated (VRR supported on DCE 11) */
	KUNIT_EXPECT_EQ(test, (int)adev->dm.vupdate_params[0].irq_src,
			(int)DC_IRQ_SOURCE_VUPDATE1);

	/* Verify PFLIP params were populated (6 fixed entries) */
	KUNIT_EXPECT_EQ(test, (int)adev->dm.pflip_params[0].irq_src,
			(int)DC_IRQ_SOURCE_PFLIP1);
	KUNIT_EXPECT_EQ(test, (int)adev->dm.pflip_params[5].irq_src,
			(int)DC_IRQ_SOURCE_PFLIP6);

	amdgpu_dm_irq_fini(adev);
}

/**
 * dm_test_dcn10_register_irq_handlers_zero_crtc - Test DCN10 zero-CRTC registration
 * @test: The KUnit test context
 */
static void dm_test_dcn10_register_irq_handlers_zero_crtc(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_free_irq_sources,
							 adev), 0);
	dc = dm_test_alloc_dc_with_ctx(test);
	adev->dm.dc = dc;
	adev->mode_info.num_hpd = 1;
	amdgpu_dm_set_irq_funcs(adev);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_dcn10_register_irq_handlers(adev), 0);
	KUNIT_ASSERT_NOT_NULL(test, adev->irq.client[SOC15_IH_CLIENTID_DCE].sources);
	KUNIT_EXPECT_PTR_EQ(test,
		adev->irq.client[SOC15_IH_CLIENTID_DCE].sources[DCN_1_0__SRCID__DC_HPD1_INT],
		&adev->hpd_irq);
}

/**
 * dm_test_dcn10_register_irq_handlers_one_crtc - Test DCN10 with 1 CRTC
 * @test: The KUnit test context
 *
 * Exercises the VUPDATE_NO_LOCK for-loop bodies with a fake IRQ service, and
 * that it's the only one populated (other than VLINE0, for secure display).
 */
static void dm_test_dcn10_register_irq_handlers_one_crtc(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_free_irq_sources,
							 adev), 0);
	dc = dm_test_alloc_dc_with_irq_service(test, &dm_test_irq_service_funcs_dcn10);
	adev->dm.dc = dc;
	adev->mode_info.num_crtc = 1;
	adev->mode_info.num_hpd = 1;
	dc->caps.max_otg_num = 1;
	amdgpu_dm_set_irq_funcs(adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_dcn10_register_irq_handlers(adev), 0);

	/* Verify VBLANK params were *not* populated */
	KUNIT_EXPECT_EQ(test, (int)adev->dm.vblank_params[0].irq_src, 0);
	KUNIT_EXPECT_NULL(test, adev->dm.vblank_params[0].adev);

	/* Verify VUPDATE params were populated */
	KUNIT_EXPECT_EQ(test, (int)adev->dm.vupdate_params[0].irq_src,
			(int)DC_IRQ_SOURCE_VUPDATE1);

	/* Verify PFLIP params were *not* populated */
	KUNIT_EXPECT_EQ(test, (int)adev->dm.pflip_params[0].irq_src, 0);

	amdgpu_dm_irq_fini(adev);
}

/**
 * dm_test_register_outbox_irq_handlers_without_dmub - Test outbox registration without DMUB
 * @test: The KUnit test context
 */
static void dm_test_register_outbox_irq_handlers_without_dmub(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_free_irq_sources,
							 adev), 0);
	dc = dm_test_alloc_dc_with_ctx(test);
	adev->dm.dc = dc;
	amdgpu_dm_set_irq_funcs(adev);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_register_outbox_irq_handlers(adev), 0);
	KUNIT_ASSERT_NOT_NULL(test, adev->irq.client[SOC15_IH_CLIENTID_DCE].sources);
	KUNIT_EXPECT_PTR_EQ(test,
		adev->irq.client[SOC15_IH_CLIENTID_DCE].sources[
			DCN_1_0__SRCID__DMCUB_OUTBOX_LOW_PRIORITY_READY_INT],
		&adev->dmub_outbox_irq);
}

/**
 * dm_test_register_outbox_irq_handlers_with_dmub - Test outbox registration with DMUB
 * @test: The KUnit test context
 *
 * Exercises the dc->ctx->dmub_srv branch which maps the outbox source and
 * registers dm_dmub_outbox1_low_irq in the low IRQ context table.
 */
static void dm_test_register_outbox_irq_handlers_with_dmub(struct kunit *test)
{
	struct dc_dmub_srv *dc_dmub_srv;
	struct amdgpu_device *adev;
	struct list_head *hnd_list;
	struct dc *dc;

	adev = dm_kunit_alloc_adev(test);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, dm_test_free_irq_sources,
							 adev), 0);
	dc = dm_test_alloc_dc_with_irq_service(test, &dm_test_irq_service_funcs_dcn10);
	dc_dmub_srv = kunit_kzalloc(test, sizeof(*dc_dmub_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_dmub_srv);
	dc->ctx->dmub_srv = dc_dmub_srv;
	adev->dm.dc = dc;
	amdgpu_dm_set_irq_funcs(adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_register_outbox_irq_handlers(adev), 0);

	/* Verify the outbox IRQ params were populated */
	KUNIT_EXPECT_EQ(test, (int)adev->dm.dmub_outbox_params[0].irq_src,
			(int)DC_IRQ_SOURCE_DMCUB_OUTBOX);
	KUNIT_EXPECT_PTR_EQ(test, adev->dm.dmub_outbox_params[0].adev, adev);

	/* Verify a low-context handler was registered for the outbox source */
	hnd_list = &adev->dm.irq_handler_list_low_tab[DC_IRQ_SOURCE_DMCUB_OUTBOX];
	KUNIT_EXPECT_FALSE(test, list_empty(hnd_list));

	amdgpu_dm_irq_fini(adev);
}

/* Tests for amdgpu_dm_irq_handler() */

/**
 * dm_test_irq_handler_dispatches_work - Test the top-level IRQ handler
 * @test: The KUnit test context
 *
 * amdgpu_dm_irq_handler() translates the hardware IRQ entry to a DC IRQ
 * source, acknowledges it, then dispatches to the high-context (immediate)
 * and low-context (scheduled) handler lists. A fake dc with a stubbed
 * irq_service maps the source id to DC_IRQ_SOURCE_VBLANK1 and lets the ack
 * succeed without touching hardware registers.
 */
static void dm_test_irq_handler_dispatches_work(struct kunit *test)
{
	struct dc_interrupt_params int_params = { 0 };
	struct amdgpu_iv_entry entry = { 0 };
	struct amdgpu_device *adev;
	int high_count = 0;
	int low_count = 0;
	void *handler;
	struct dc *dc;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_EQ(test, amdgpu_dm_irq_init(adev), 0);

	dc = dm_test_alloc_dc_with_irq_service(test,
					       &dm_test_irq_service_funcs_dce110);
	adev->dm.dc = dc;

	/* High-context (immediate) handler on VBLANK1. */
	int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
	int_params.irq_source = DC_IRQ_SOURCE_VBLANK1;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						   dm_test_irq_handler_count,
						   &high_count);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	/* Low-context (scheduled) handler on VBLANK1. */
	int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
	handler = amdgpu_dm_irq_register_interrupt(adev, &int_params,
						   dm_test_irq_handler_count,
						   &low_count);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, handler);

	/* src_id maps to DC_IRQ_SOURCE_VBLANK1 via the dce110 stub. */
	entry.src_id = VISLANDS30_IV_SRCID_D1_VERTICAL_INTERRUPT0;
	entry.src_data[0] = 0;

	KUNIT_EXPECT_EQ(test, amdgpu_dm_irq_handler(adev, NULL, &entry), 0);

	/* High-context handler runs synchronously in-place. */
	KUNIT_EXPECT_EQ(test, high_count, 1);

	/*
	 * Low-context work runs asynchronously; amdgpu_dm_irq_fini() flushes
	 * each pending work item before freeing, so it has run afterwards.
	 */
	amdgpu_dm_irq_fini(adev);
	KUNIT_EXPECT_EQ(test, low_count, 1);
}

/* Tests for dm_handle_vmin_vmax_update() */

/**
 * dm_test_handle_vmin_vmax_update - Test the deferred vmin/vmax worker
 * @test: The KUnit test context
 *
 * The worker applies the cached timing adjust to the stream via
 * dc_stream_adjust_vmin_vmax(), drops the stream reference taken when the
 * work was scheduled, and frees the work and its adjust copy. A fake dc with
 * a current_state lets the adjust walk an empty pipe list without touching
 * hardware. The work and adjust are kmalloc'd because the worker frees them.
 */
static void dm_test_handle_vmin_vmax_update(struct kunit *test)
{
	struct dc_crtc_timing_adjust *adjust;
	struct vupdate_offload_work *work;
	struct dc_stream_state *stream;
	struct amdgpu_device *adev;
	struct dc *dc;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	mutex_init(&adev->dm.dc_lock);

	dc = dm_test_alloc_dc_with_ctx(test);
	dc->current_state = kunit_kzalloc(test, sizeof(*dc->current_state),
					  GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc->current_state);
	adev->dm.dc = dc;

	stream = kunit_kzalloc(test, sizeof(*stream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, stream);
	/*
	 * Start at two references: the worker's dc_stream_release() drops one,
	 * leaving the kunit-managed allocation intact (no kfree).
	 */
	kref_init(&stream->refcount);
	kref_get(&stream->refcount);

	/* The worker kfree()s both, so they must come from the slab. */
	work = kzalloc(sizeof(*work), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, work);
	adjust = kzalloc(sizeof(*adjust), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adjust);

	work->adev = adev;
	work->stream = stream;
	work->adjust = adjust;
	adjust->v_total_min = 1000;
	adjust->v_total_max = 1100;

	dm_handle_vmin_vmax_update(&work->work);

	/* The adjust was applied and one stream reference was dropped. */
	KUNIT_EXPECT_EQ(test, stream->adjust.v_total_min, 1000);
	KUNIT_EXPECT_EQ(test, stream->adjust.v_total_max, 1100);
	KUNIT_EXPECT_EQ(test, kref_read(&stream->refcount), 1);
}

static struct kunit_case amdgpu_dm_irq_tests[] = {
	/* amdgpu_dm_hpd_to_dal_irq_source */
	KUNIT_CASE(dm_test_hpd_to_dal_irq_source_hpd1),
	KUNIT_CASE(dm_test_hpd_to_dal_irq_source_hpd2),
	KUNIT_CASE(dm_test_hpd_to_dal_irq_source_hpd3),
	KUNIT_CASE(dm_test_hpd_to_dal_irq_source_hpd4),
	KUNIT_CASE(dm_test_hpd_to_dal_irq_source_hpd5),
	KUNIT_CASE(dm_test_hpd_to_dal_irq_source_hpd6),
	KUNIT_CASE(dm_test_hpd_to_dal_irq_source_invalid),
	KUNIT_CASE(dm_test_hpd_to_dal_irq_source_out_of_range),
	/* are_sinks_equal */
	KUNIT_CASE(dm_test_are_sinks_equal_both_null),
	KUNIT_CASE(dm_test_are_sinks_equal_first_null),
	KUNIT_CASE(dm_test_are_sinks_equal_second_null),
	KUNIT_CASE(dm_test_are_sinks_equal_different_signal),
	KUNIT_CASE(dm_test_are_sinks_equal_different_edid_length),
	KUNIT_CASE(dm_test_are_sinks_equal_different_edid_data),
	KUNIT_CASE(dm_test_are_sinks_equal_identical),
	KUNIT_CASE(dm_test_are_sinks_equal_zero_length),
	KUNIT_CASE(dm_test_are_sinks_equal_full_edid_identical),
	KUNIT_CASE(dm_test_are_sinks_equal_full_edid_last_byte_differs),
	/* dmub_notification_type_str */
	KUNIT_CASE(dm_test_notification_str_no_data),
	KUNIT_CASE(dm_test_notification_str_aux_reply),
	KUNIT_CASE(dm_test_notification_str_hpd),
	KUNIT_CASE(dm_test_notification_str_hpd_irq),
	KUNIT_CASE(dm_test_notification_str_set_config),
	KUNIT_CASE(dm_test_notification_str_dpia),
	KUNIT_CASE(dm_test_notification_str_hpd_sense),
	KUNIT_CASE(dm_test_notification_str_fused_io),
	KUNIT_CASE(dm_test_notification_str_unknown),
	/* amdgpu_dm_irq_init */
	KUNIT_CASE(dm_test_irq_init_initializes_lists),
	/* amdgpu_dm_irq_register_interrupt */
	KUNIT_CASE(dm_test_irq_register_rejects_null_params),
	KUNIT_CASE(dm_test_irq_register_rejects_invalid_context),
	KUNIT_CASE(dm_test_irq_register_rejects_invalid_source),
	KUNIT_CASE(dm_test_irq_register_adds_low_context_handler),
	KUNIT_CASE(dm_test_irq_register_adds_high_context_handler),
	KUNIT_CASE(dm_test_irq_register_multiple_handlers),
	KUNIT_CASE(dm_test_irq_register_separate_contexts),
	/* amdgpu_dm_irq_unregister_interrupt */
	KUNIT_CASE(dm_test_irq_unregister_rejects_invalid_source),
	KUNIT_CASE(dm_test_irq_unregister_rejects_null_handler),
	KUNIT_CASE(dm_test_irq_unregister_handler_not_found),
	/* amdgpu_dm_irq_fini */
	KUNIT_CASE(dm_test_irq_fini_removes_registered_handlers),
	KUNIT_CASE(dm_test_irq_fini_on_empty_tables),
	/* amdgpu_dm_get_crtc_by_otg_inst */
	KUNIT_CASE(dm_test_get_crtc_by_otg_inst_returns_match),
	KUNIT_CASE(dm_test_get_crtc_by_otg_inst_returns_null),
	KUNIT_CASE(dm_test_get_crtc_by_otg_inst_empty_list),
	/* amdgpu_dm_set_irq_funcs */
	KUNIT_CASE(dm_test_set_irq_funcs),
	/* amdgpu_dm_irq_suspend/resume_early/resume_late */
	KUNIT_CASE(dm_test_irq_suspend_empty),
	KUNIT_CASE(dm_test_irq_resume_early_empty),
	KUNIT_CASE(dm_test_irq_resume_late_empty),
	KUNIT_CASE(dm_test_irq_suspend_registered),
	KUNIT_CASE(dm_test_irq_suspend_disables_polling),
	KUNIT_CASE(dm_test_irq_resume_early_registered),
	KUNIT_CASE(dm_test_irq_resume_late_registered),
	KUNIT_CASE(dm_test_irq_resume_late_enables_polling),
	/* amdgpu_dm_hpd_rx_irq_create_workqueue */
	KUNIT_CASE(dm_test_hpd_rx_irq_create_workqueue),
	/* amdgpu_dm_hpd_rx_irq_work_suspend */
	KUNIT_CASE(dm_test_hpd_rx_irq_work_suspend_null),
	KUNIT_CASE(dm_test_hpd_rx_irq_work_suspend_flushes),
	/* CRTC-based irq state callbacks (no-CRTC early return) */
	KUNIT_CASE(dm_test_set_crtc_irq_state_no_crtc),
	KUNIT_CASE(dm_test_set_pflip_irq_state_no_crtc),
	KUNIT_CASE(dm_test_set_vline0_irq_state_no_crtc),
	KUNIT_CASE(dm_test_set_vupdate_irq_state_no_crtc),
	/* CRTC-based irq state callbacks (dm_irq_state happy path) */
	KUNIT_CASE(dm_test_set_crtc_irq_state_otg_disabled),
	KUNIT_CASE(dm_test_set_crtc_irq_state_enable),
	KUNIT_CASE(dm_test_set_pflip_irq_state_disable),
	KUNIT_CASE(dm_test_set_vline0_irq_state_enable),
	KUNIT_CASE(dm_test_set_vupdate_irq_state_enable),
	KUNIT_CASE(dm_test_set_crtc_irq_state_allows_idle),
	/* amdgpu_dm_irq_immediate_work */
	KUNIT_CASE(dm_test_irq_immediate_work_empty),
	KUNIT_CASE(dm_test_irq_immediate_work_invokes_handler),
	KUNIT_CASE(dm_test_irq_immediate_work_invokes_all),
	/* amdgpu_dm_irq_schedule_work */
	KUNIT_CASE(dm_test_irq_schedule_work_empty),
	KUNIT_CASE(dm_test_irq_schedule_work_queues_handler),
	KUNIT_CASE(dm_test_irq_schedule_work_requeue_fallback),
	/* amdgpu_dm_set_hpd_irq_state */
	KUNIT_CASE(dm_test_set_hpd_irq_state_null_dc),
	/* amdgpu_dm_set_dmub_outbox_irq_state */
	KUNIT_CASE(dm_test_set_dmub_outbox_irq_state_null_dc),
	/* amdgpu_dm_set_dmub_trace_irq_state */
	KUNIT_CASE(dm_test_set_dmub_trace_irq_state_null_dc),
	/* amdgpu_dm_outbox_init */
	KUNIT_CASE(dm_test_outbox_init_null_dc),
	/* amdgpu_dm_hpd_init/amdgpu_dm_hpd_fini */
	KUNIT_CASE(dm_test_hpd_init_empty_connectors),
	KUNIT_CASE(dm_test_hpd_fini_empty_connectors),
	KUNIT_CASE(dm_test_hpd_init_fini_with_connectors),
	KUNIT_CASE(dm_test_hpd_init_fini_analog_connector),
	KUNIT_CASE(dm_test_hpd_init_fini_irq_ref),
	/* dm_handle_hpd_rx_offload_work */
	KUNIT_CASE(dm_test_hpd_rx_offload_work_no_connector),
	KUNIT_CASE(dm_test_hpd_rx_offload_work_no_connection),
	KUNIT_CASE(dm_test_hpd_rx_offload_work_automated_test),
	KUNIT_CASE(dm_test_hpd_rx_offload_work_link_loss),
	/* amdgpu_dm_hdmi_hpd_debounce_work */
	KUNIT_CASE(dm_test_hdmi_hpd_debounce_detect_false),
	KUNIT_CASE(dm_test_hdmi_hpd_debounce_reallow_idle),
	/* handle_hpd_irq/handle_hpd_irq_helper */
	KUNIT_CASE(dm_test_handle_hpd_irq_disabled),
	KUNIT_CASE(dm_test_handle_hpd_irq_helper_debounce_schedule),
	KUNIT_CASE(dm_test_handle_hpd_irq_helper_debounce_release_prev),
	KUNIT_CASE(dm_test_handle_hpd_irq_helper_detect_false),
	/* handle_hpd_rx_irq/schedule_hpd_rx_offload_work */
	KUNIT_CASE(dm_test_handle_hpd_rx_irq_disabled),
	KUNIT_CASE(dm_test_handle_hpd_rx_irq_no_left_work),
	KUNIT_CASE(dm_test_handle_hpd_rx_irq_automated_test),
	KUNIT_CASE(dm_test_handle_hpd_rx_irq_msg_rdy),
	KUNIT_CASE(dm_test_handle_hpd_rx_irq_link_loss),
	KUNIT_CASE(dm_test_schedule_hpd_rx_offload_work),
	/* dmub_hpd_callback/dmub_hpd_sense_callback */
	KUNIT_CASE(dm_test_dmub_hpd_callback_null_inputs),
	KUNIT_CASE(dm_test_dmub_hpd_callback_invalid_index),
	KUNIT_CASE(dm_test_dmub_hpd_callback_empty_connectors),
	KUNIT_CASE(dm_test_dmub_hpd_callback_unknown_type_match),
	KUNIT_CASE(dm_test_dmub_hpd_callback_hpd_type),
	KUNIT_CASE(dm_test_dmub_hpd_callback_hpd_irq_type),
	/* amdgpu_dm_register_hpd_handlers */
	KUNIT_CASE(dm_test_register_hpd_handlers_empty),
	KUNIT_CASE(dm_test_register_hpd_handlers_valid),
	KUNIT_CASE(dm_test_register_hpd_handlers_invalid_hpd),
	KUNIT_CASE(dm_test_register_hpd_handlers_invalid_hpd_rx),
	KUNIT_CASE(dm_test_register_hpd_handlers_dmub_outbox),
	/* pflip/vupdate/crtc high IRQ callbacks */
	KUNIT_CASE(dm_test_pflip_high_irq_no_crtc),
	KUNIT_CASE(dm_test_pflip_high_irq_not_submitted),
	KUNIT_CASE(dm_test_vupdate_high_irq_no_crtc),
	KUNIT_CASE(dm_test_crtc_high_irq_no_crtc),
	KUNIT_CASE(dm_test_crtc_high_irq_vrr_pre_ai),
	KUNIT_CASE(dm_test_crtc_high_irq_vrr_ai_no_stream),
	/* dm_handle_hpd_work */
	KUNIT_CASE(dm_test_handle_hpd_work_out_of_range),
	/* dm_dmub_outbox1_low_irq */
	KUNIT_CASE(dm_test_dmub_outbox1_low_irq_empty),
	KUNIT_CASE(dm_test_dmub_outbox1_low_irq_no_handler),
	KUNIT_CASE(dm_test_dmub_outbox1_low_irq_direct_callback),
	KUNIT_CASE(dm_test_dmub_outbox1_low_irq_offload),
	/* IRQ handler registration helpers */
	KUNIT_CASE(dm_test_dce110_register_irq_handlers_rejects_uninitialized_sources),
	KUNIT_CASE(dm_test_dce110_register_irq_handlers_one_crtc),
	KUNIT_CASE(dm_test_dcn10_register_irq_handlers_zero_crtc),
	KUNIT_CASE(dm_test_dcn10_register_irq_handlers_one_crtc),
	KUNIT_CASE(dm_test_register_outbox_irq_handlers_without_dmub),
	KUNIT_CASE(dm_test_register_outbox_irq_handlers_with_dmub),
	/* amdgpu_dm_irq_handler */
	KUNIT_CASE(dm_test_irq_handler_dispatches_work),
	/* dm_handle_vmin_vmax_update */
	KUNIT_CASE(dm_test_handle_vmin_vmax_update),
	{}
};

static struct kunit_suite amdgpu_dm_irq_test_suite = {
	.name = "amdgpu_dm_irq",
	.test_cases = amdgpu_dm_irq_tests,
};

kunit_test_suite(amdgpu_dm_irq_test_suite);

MODULE_AUTHOR("AMD");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_irq");
MODULE_LICENSE("Dual MIT/GPL");
