// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_mst_types.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>

#include <drm/drm_drv.h>
#include <drm/drm_fixed.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_mode_config.h>
#include <drm/display/drm_dp.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dp_mst_helper.h>

#include "dc.h"
#include "dpcd_defs.h"
#include "dmub_cmd.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_mst_types.h"
#include "amdgpu_dm_kunit_test_helpers.h"
#include "inc/link_service.h"

/*
 * Minimal mock DPCD backing store and AUX transfer callback used to exercise
 * the DPCD read paths without real hardware.
 */
static u8 dm_mst_test_dpcd[0x10];
static u8 dm_mst_test_desc_dpcd[0x10];
static struct aux_payload dm_mst_test_last_payload;
static int dm_mst_test_aux_transfer_raw_result;
static u8 dm_mst_test_aux_transfer_raw_reply;
static enum aux_return_code_type dm_mst_test_aux_transfer_raw_operation_result;
static ssize_t dm_mst_test_aux_transfer_override;

static int dm_mst_test_aux_transfer_raw(struct ddc_service *ddc,
						struct aux_payload *payload,
						enum aux_return_code_type *operation_result)
{
	size_t i;

	dm_mst_test_last_payload = *payload;
	*operation_result = dm_mst_test_aux_transfer_raw_operation_result;
	payload->reply[0] = dm_mst_test_aux_transfer_raw_reply;

	if (dm_mst_test_aux_transfer_raw_result)
		return dm_mst_test_aux_transfer_raw_result;

	if (payload->write)
		return 0;

	for (i = 0; i < payload->length; i++)
		payload->data[i] = dm_mst_test_dpcd[(payload->address + i) & 0xf];

	return payload->length;
}

static void dm_mst_test_setup_dm_aux(struct amdgpu_dm_dp_aux *dm_aux,
					    struct ddc_service *ddc,
					    struct dc_link *link,
					    struct dc *dc,
					    struct link_service *link_srv,
					    struct dc_context *ctx,
					    struct amdgpu_device *adev)
{
	memset(&dm_mst_test_last_payload, 0, sizeof(dm_mst_test_last_payload));
	dm_mst_test_aux_transfer_raw_result = 0;
	dm_mst_test_aux_transfer_raw_reply = 0;
	dm_mst_test_aux_transfer_raw_operation_result = AUX_RET_SUCCESS;
	link_srv->aux_transfer_raw = dm_mst_test_aux_transfer_raw;
	dc->link_srv = link_srv;
	link->dc = dc;
	ctx->driver_context = adev;
	ddc->link = link;
	ddc->ctx = ctx;
	dm_aux->ddc_service = ddc;
	dm_aux->aux.name = "dm_mst_test_dm_aux";
	dm_aux->aux.transfer = dm_dp_aux_transfer;
	drm_dp_aux_init(&dm_aux->aux);
	drm_dp_dpcd_set_probe(&dm_aux->aux, false);
}

static const struct dc_link_status *dm_mst_test_get_status(const struct dc_link *link)
{
	return &link->link_status;
}

static ssize_t dm_mst_test_aux_transfer(struct drm_dp_aux *aux,
					struct drm_dp_aux_msg *msg)
{
	size_t i;
	ssize_t ret;

	ret = dm_mst_test_aux_transfer_override;
	if (ret)
		return ret;

	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_READ:
		for (i = 0; i < msg->size; i++)
			((u8 *)msg->buffer)[i] =
				dm_mst_test_dpcd[(msg->address + i) & 0xf];
		msg->reply = DP_AUX_NATIVE_REPLY_ACK;
		return msg->size;
	case DP_AUX_NATIVE_WRITE:
		msg->reply = DP_AUX_NATIVE_REPLY_ACK;
		return msg->size;
	default:
		return -EINVAL;
	}
}

static struct amdgpu_dm_connector *dm_mst_test_alloc_sideband_connector(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct link_service *link_srv;
	struct dc_link *link;
	struct dc *dc;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, link);
	KUNIT_ASSERT_NOT_NULL(test, dc);

	mutex_init(&aconnector->handle_mst_msg_ready);
	link_srv->get_status = dm_mst_test_get_status;
	dc->link_srv = link_srv;
	link->dc = dc;
	link->dpcd_caps.dpcd_rev.raw = DPCD_REV_14;
	link->link_status.dpcd_caps = &link->dpcd_caps;
	aconnector->dc_link = link;
	aconnector->dm_dp_aux.aux.name = "dm_mst_test_sideband_aux";
	aconnector->dm_dp_aux.aux.transfer = dm_mst_test_aux_transfer;
	drm_dp_aux_init(&aconnector->dm_dp_aux.aux);
	drm_dp_dpcd_set_probe(&aconnector->dm_dp_aux.aux, false);

	memset(dm_mst_test_dpcd, 0, sizeof(dm_mst_test_dpcd));
	dm_mst_test_aux_transfer_override = 0;

	return aconnector;
}

static uint32_t dm_mst_test_dp_link_bandwidth_kbps(
	const struct dc_link *link,
	const struct dc_link_settings *link_settings)
{
	return 4320000;
}

static const struct dc_link_settings *dm_mst_test_dp_get_verified_link_cap(
	const struct dc_link *link)
{
	return &link->verified_link_cap;
}

static ssize_t dm_mst_test_desc_aux_transfer(struct drm_dp_aux *aux,
					     struct drm_dp_aux_msg *msg)
{
	size_t i;

	if ((msg->request & ~DP_AUX_I2C_MOT) != DP_AUX_NATIVE_READ)
		return -EINVAL;

	for (i = 0; i < msg->size; i++)
		((u8 *)msg->buffer)[i] = dm_mst_test_desc_dpcd[msg->address + i - DP_BRANCH_OUI];

	msg->reply = DP_AUX_NATIVE_REPLY_ACK;
	return msg->size;
}

/* Tests for needs_dsc_aux_workaround */

/**
 * dm_mst_test_needs_dsc_aux_workaround_match - Test workaround triggers for matching device
 * @test: KUnit test context
 *
 * Verify that needs_dsc_aux_workaround() returns true when the link has
 * the specific branch device ID, DPCD rev 1.4, and sink count >= 2.
 */
static void dm_mst_test_needs_dsc_aux_workaround_match(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.branch_dev_id = DP_BRANCH_DEVICE_ID_90CC24;
	link->dpcd_caps.dpcd_rev.raw = DPCD_REV_14;
	link->dpcd_caps.sink_count.bits.SINK_COUNT = 2;

	KUNIT_EXPECT_TRUE(test, needs_dsc_aux_workaround(link));
}

/**
 * dm_mst_test_needs_dsc_aux_workaround_rev12 - Test workaround triggers for DPCD rev 1.2
 * @test: KUnit test context
 *
 * Verify that needs_dsc_aux_workaround() returns true when the link has
 * the specific branch device ID, DPCD rev 1.2, and sink count >= 2.
 */
static void dm_mst_test_needs_dsc_aux_workaround_rev12(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.branch_dev_id = DP_BRANCH_DEVICE_ID_90CC24;
	link->dpcd_caps.dpcd_rev.raw = DPCD_REV_12;
	link->dpcd_caps.sink_count.bits.SINK_COUNT = 3;

	KUNIT_EXPECT_TRUE(test, needs_dsc_aux_workaround(link));
}

/**
 * dm_mst_test_needs_dsc_aux_workaround_wrong_dev_id - Test workaround skipped for wrong device
 * @test: KUnit test context
 *
 * Verify that needs_dsc_aux_workaround() returns false when the branch
 * device ID does not match DP_BRANCH_DEVICE_ID_90CC24.
 */
static void dm_mst_test_needs_dsc_aux_workaround_wrong_dev_id(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.branch_dev_id = 0x123456;
	link->dpcd_caps.dpcd_rev.raw = DPCD_REV_14;
	link->dpcd_caps.sink_count.bits.SINK_COUNT = 2;

	KUNIT_EXPECT_FALSE(test, needs_dsc_aux_workaround(link));
}

/**
 * dm_mst_test_needs_dsc_aux_workaround_wrong_rev - Test workaround skipped for unsupported rev
 * @test: KUnit test context
 *
 * Verify that needs_dsc_aux_workaround() returns false when the DPCD
 * revision is neither 1.2 nor 1.4.
 */
static void dm_mst_test_needs_dsc_aux_workaround_wrong_rev(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.branch_dev_id = DP_BRANCH_DEVICE_ID_90CC24;
	link->dpcd_caps.dpcd_rev.raw = 0x11; /* DPCD 1.1 */
	link->dpcd_caps.sink_count.bits.SINK_COUNT = 2;

	KUNIT_EXPECT_FALSE(test, needs_dsc_aux_workaround(link));
}

/**
 * dm_mst_test_needs_dsc_aux_workaround_low_sink_count - Test workaround skipped for single sink
 * @test: KUnit test context
 *
 * Verify that needs_dsc_aux_workaround() returns false when the sink
 * count is less than 2, even if device ID and DPCD rev match.
 */
static void dm_mst_test_needs_dsc_aux_workaround_low_sink_count(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.branch_dev_id = DP_BRANCH_DEVICE_ID_90CC24;
	link->dpcd_caps.dpcd_rev.raw = DPCD_REV_14;
	link->dpcd_caps.sink_count.bits.SINK_COUNT = 1;

	KUNIT_EXPECT_FALSE(test, needs_dsc_aux_workaround(link));
}

/**
 * dm_mst_test_needs_dsc_aux_workaround_zero_sink_count - Test workaround skipped for zero sinks
 * @test: KUnit test context
 *
 * Verify that needs_dsc_aux_workaround() returns false when the sink
 * count is zero, even if device ID and DPCD rev match.
 */
static void dm_mst_test_needs_dsc_aux_workaround_zero_sink_count(struct kunit *test)
{
	struct dc_link *link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	link->dpcd_caps.branch_dev_id = DP_BRANCH_DEVICE_ID_90CC24;
	link->dpcd_caps.dpcd_rev.raw = DPCD_REV_14;
	link->dpcd_caps.sink_count.bits.SINK_COUNT = 0;

	KUNIT_EXPECT_FALSE(test, needs_dsc_aux_workaround(link));
}

/* Tests for dm_mst_get_pbn_divider */

/**
 * dm_mst_test_pbn_divider_null_link - Test pbn_divider with NULL link
 * @test: KUnit test context
 *
 * Verify that dm_mst_get_pbn_divider() returns 0 when passed a NULL
 * link pointer without crashing.
 */
static void dm_mst_test_pbn_divider_null_link(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, dm_mst_get_pbn_divider(NULL), 0U);
}

/**
 * dm_mst_test_pbn_divider_uses_link_bandwidth - Test pbn_divider with link cap
 * @test: KUnit test context
 *
 * Verify that dm_mst_get_pbn_divider() uses the DC link service to derive the
 * fixed-point PBN divider when a link is present.
 */
static void dm_mst_test_pbn_divider_uses_link_bandwidth(struct kunit *test)
{
	struct link_service *link_srv;
	struct dc_link *link;
	struct dc *dc;

	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, link);
	KUNIT_ASSERT_NOT_NULL(test, dc);

	link_srv->dp_get_verified_link_cap = dm_mst_test_dp_get_verified_link_cap;
	link_srv->dp_link_bandwidth_kbps = dm_mst_test_dp_link_bandwidth_kbps;
	dc->link_srv = link_srv;
	link->dc = dc;

	KUNIT_EXPECT_EQ(test, dm_mst_get_pbn_divider(link),
			 (uint32_t)(dfixed_const(1000) / 100));
}

/* Tests for amdgpu_dm_mst_reset_mst_connector_setting */

/**
 * dm_mst_test_reset_connector_setting - Test MST connector setting reset
 * @test: KUnit test context
 *
 * Verify that amdgpu_dm_mst_reset_mst_connector_setting() clears the cached
 * EDID, DSC AUX, passthrough AUX, local bandwidth, and VC PBN state.
 */
static void dm_mst_test_reset_connector_setting(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_port *port;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	port = kunit_kzalloc(test, sizeof(*port), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	KUNIT_ASSERT_NOT_NULL(test, port);

	aconnector->drm_edid = (const struct drm_edid *)test;
	aconnector->dsc_aux = (struct drm_dp_aux *)test;
	aconnector->mst_output_port = port;
	aconnector->mst_output_port->passthrough_aux = (struct drm_dp_aux *)test;
	aconnector->mst_local_bw = 12345;
	aconnector->vc_full_pbn = 678;

	amdgpu_dm_mst_reset_mst_connector_setting(aconnector);

	KUNIT_EXPECT_TRUE(test, aconnector->drm_edid == NULL);
	KUNIT_EXPECT_TRUE(test, aconnector->dsc_aux == NULL);
	KUNIT_EXPECT_TRUE(test, aconnector->mst_output_port->passthrough_aux == NULL);
	KUNIT_EXPECT_EQ(test, aconnector->mst_local_bw, 0U);
	KUNIT_EXPECT_EQ(test, aconnector->vc_full_pbn, 0U);
}

/* Tests for retrieve_downstream_port_device */

/**
 * dm_mst_test_retrieve_downstream_no_aux - Test retrieval bails out without AUX
 * @test: KUnit test context
 *
 * Verify that retrieve_downstream_port_device() returns false when the
 * connector has no DSC AUX channel and therefore cannot read DPCD.
 */
static void dm_mst_test_retrieve_downstream_no_aux(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->dsc_aux = NULL;

	KUNIT_EXPECT_FALSE(test, retrieve_downstream_port_device(aconnector));
}

/**
 * dm_mst_test_retrieve_downstream_present - Test retrieval parses DPCD 0x05
 * @test: KUnit test context
 *
 * Verify that retrieve_downstream_port_device() reads DP_DOWNSTREAMPORT_PRESENT
 * over a mock AUX channel and caches the parsed downstream port fields.
 */
static void dm_mst_test_retrieve_downstream_present(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_aux *aux;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	aux = kunit_kzalloc(test, sizeof(*aux), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	KUNIT_ASSERT_NOT_NULL(test, aux);

	memset(dm_mst_test_dpcd, 0, sizeof(dm_mst_test_dpcd));
	/* PORT_PRESENT = 1, PORT_TYPE = 2 (0b101) */
	dm_mst_test_dpcd[DP_DOWNSTREAMPORT_PRESENT] = 0x05;
	dm_mst_test_aux_transfer_override = 0;

	aux->name = "dm_mst_test_aux";
	aux->transfer = dm_mst_test_aux_transfer;
	drm_dp_aux_init(aux);
	drm_dp_dpcd_set_probe(aux, false);
	aconnector->dsc_aux = aux;

	KUNIT_EXPECT_TRUE(test, retrieve_downstream_port_device(aconnector));
	KUNIT_EXPECT_EQ(test,
			(int)aconnector->mst_downstream_port_present.fields.PORT_PRESENT, 1);
	KUNIT_EXPECT_EQ(test,
			(int)aconnector->mst_downstream_port_present.fields.PORT_TYPE, 2);
}

/**
 * dm_mst_test_retrieve_downstream_aux_error - Test downstream read failure
 * @test: KUnit test context
 *
 * Verify that retrieve_downstream_port_device() returns false when the AUX
 * DPCD read fails.
 */
static void dm_mst_test_retrieve_downstream_aux_error(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_aux *aux;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	aux = kunit_kzalloc(test, sizeof(*aux), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	KUNIT_ASSERT_NOT_NULL(test, aux);

	dm_mst_test_aux_transfer_override = -EIO;
	aux->name = "dm_mst_test_aux";
	aux->transfer = dm_mst_test_aux_transfer;
	drm_dp_aux_init(aux);
	drm_dp_dpcd_set_probe(aux, false);
	aconnector->dsc_aux = aux;

	KUNIT_EXPECT_FALSE(test, retrieve_downstream_port_device(aconnector));

	dm_mst_test_aux_transfer_override = 0;
}

/* Tests for retrieve_branch_specific_data */

/**
 * dm_mst_test_retrieve_branch_no_parent - Test branch lookup needs a parent port
 * @test: KUnit test context
 *
 * Verify that retrieve_branch_specific_data() returns false when the MST
 * output port has no parent branch device to query.
 */
static void dm_mst_test_retrieve_branch_no_parent(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_port *port;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	port = kunit_kzalloc(test, sizeof(*port), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	KUNIT_ASSERT_NOT_NULL(test, port);

	port->parent = NULL;
	aconnector->mst_output_port = port;

	KUNIT_EXPECT_FALSE(test, retrieve_branch_specific_data(aconnector));
}

/**
 * dm_mst_test_retrieve_branch_reads_oui - Test branch OUI parsing
 * @test: KUnit test context
 *
 * Verify that retrieve_branch_specific_data() reads the immediate upstream
 * branch descriptor and caches its IEEE OUI value on the connector.
 */
static void dm_mst_test_retrieve_branch_reads_oui(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_topology_mgr *mgr;
	struct drm_dp_mst_branch *branch;
	struct drm_dp_mst_port *port;
	struct drm_dp_aux *aux;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	mgr = kunit_kzalloc(test, sizeof(*mgr), GFP_KERNEL);
	branch = kunit_kzalloc(test, sizeof(*branch), GFP_KERNEL);
	port = kunit_kzalloc(test, sizeof(*port), GFP_KERNEL);
	aux = kunit_kzalloc(test, sizeof(*aux), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	KUNIT_ASSERT_NOT_NULL(test, mgr);
	KUNIT_ASSERT_NOT_NULL(test, branch);
	KUNIT_ASSERT_NOT_NULL(test, port);
	KUNIT_ASSERT_NOT_NULL(test, aux);

	memset(dm_mst_test_desc_dpcd, 0, sizeof(dm_mst_test_desc_dpcd));
	dm_mst_test_desc_dpcd[0] = 0x12;
	dm_mst_test_desc_dpcd[1] = 0x34;
	dm_mst_test_desc_dpcd[2] = 0x56;

	aux->name = "dm_mst_test_desc_aux";
	aux->transfer = dm_mst_test_desc_aux_transfer;
	drm_dp_aux_init(aux);
	drm_dp_dpcd_set_probe(aux, false);
	mgr->aux = aux;
	port->parent = branch;
	port->mgr = mgr;
	port->aux.drm_dev = NULL;
	aconnector->mst_output_port = port;

	KUNIT_EXPECT_TRUE(test, retrieve_branch_specific_data(aconnector));
	KUNIT_EXPECT_EQ(test, aconnector->branch_ieee_oui, 0x123456U);
}

/**
 * dm_mst_test_aux_result_success - AUX_RET_SUCCESS preserves the input result.
 * @test: KUnit test context.
 *
 * On success the original (negative) transfer result must be returned unchanged.
 */
static void dm_mst_test_aux_result_success(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, dm_dp_aux_transfer_result(-5, AUX_RET_SUCCESS), (ssize_t)-5);
	KUNIT_EXPECT_EQ(test, dm_dp_aux_transfer_result(3, AUX_RET_SUCCESS), (ssize_t)3);
}

/**
 * dm_mst_test_aux_result_eio - HPD/unknown/protocol errors map to -EIO.
 * @test: KUnit test context.
 *
 * AUX_RET_ERROR_HPD_DISCON, AUX_RET_ERROR_UNKNOWN,
 * AUX_RET_ERROR_INVALID_OPERATION and AUX_RET_ERROR_PROTOCOL_ERROR all map to -EIO.
 */
static void dm_mst_test_aux_result_eio(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, dm_dp_aux_transfer_result(-1, AUX_RET_ERROR_HPD_DISCON),
			(ssize_t)-EIO);
	KUNIT_EXPECT_EQ(test, dm_dp_aux_transfer_result(-1, AUX_RET_ERROR_UNKNOWN),
			(ssize_t)-EIO);
	KUNIT_EXPECT_EQ(test, dm_dp_aux_transfer_result(-1, AUX_RET_ERROR_INVALID_OPERATION),
			(ssize_t)-EIO);
	KUNIT_EXPECT_EQ(test, dm_dp_aux_transfer_result(-1, AUX_RET_ERROR_PROTOCOL_ERROR),
			(ssize_t)-EIO);
}

/**
 * dm_mst_test_aux_result_ebusy - invalid reply / engine acquire map to -EBUSY.
 * @test: KUnit test context.
 *
 * AUX_RET_ERROR_INVALID_REPLY and AUX_RET_ERROR_ENGINE_ACQUIRE map to -EBUSY.
 */
static void dm_mst_test_aux_result_ebusy(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, dm_dp_aux_transfer_result(-1, AUX_RET_ERROR_INVALID_REPLY),
			(ssize_t)-EBUSY);
	KUNIT_EXPECT_EQ(test, dm_dp_aux_transfer_result(-1, AUX_RET_ERROR_ENGINE_ACQUIRE),
			(ssize_t)-EBUSY);
}

/**
 * dm_mst_test_aux_result_timeout - AUX_RET_ERROR_TIMEOUT maps to -ETIMEDOUT.
 * @test: KUnit test context.
 */
static void dm_mst_test_aux_result_timeout(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, dm_dp_aux_transfer_result(-1, AUX_RET_ERROR_TIMEOUT),
			(ssize_t)-ETIMEDOUT);
}

/**
 * dm_mst_test_aux_transfer_native_read - native AUX read through DM callback.
 * @test: KUnit test context.
 *
 * The DM AUX transfer callback should build a read payload, call the DC link
 * service, and return the number of bytes provided by the fake backend.
 */
static void dm_mst_test_aux_transfer_native_read(struct kunit *test)
{
	struct amdgpu_dm_dp_aux *dm_aux;
	struct amdgpu_device *adev;
	struct ddc_service *ddc;
	struct dc_link *link;
	struct dc *dc;
	struct link_service *link_srv;
	struct dc_context *ctx;
	u8 buffer[3] = { 0 };
	ssize_t ret;

	dm_aux = kunit_kzalloc(test, sizeof(*dm_aux), GFP_KERNEL);
	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_aux);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ddc);
	KUNIT_ASSERT_NOT_NULL(test, link);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	memset(dm_mst_test_dpcd, 0, sizeof(dm_mst_test_dpcd));
	dm_mst_test_dpcd[4] = 0xaa;
	dm_mst_test_dpcd[5] = 0xbb;
	dm_mst_test_dpcd[6] = 0xcc;
	dm_mst_test_setup_dm_aux(dm_aux, ddc, link, dc, link_srv, ctx, adev);

	ret = drm_dp_dpcd_read(&dm_aux->aux, 4, buffer, sizeof(buffer));

	KUNIT_EXPECT_EQ(test, ret, (ssize_t)sizeof(buffer));
	KUNIT_EXPECT_EQ(test, buffer[0], (u8)0xaa);
	KUNIT_EXPECT_EQ(test, buffer[1], (u8)0xbb);
	KUNIT_EXPECT_EQ(test, buffer[2], (u8)0xcc);
	KUNIT_EXPECT_FALSE(test, dm_mst_test_last_payload.write);
	KUNIT_EXPECT_FALSE(test, dm_mst_test_last_payload.i2c_over_aux);
	KUNIT_EXPECT_EQ(test, dm_mst_test_last_payload.address, 4U);
}

/**
 * dm_mst_test_aux_transfer_native_write - native AUX write through DM callback.
 * @test: KUnit test context.
 *
 * A successful write with an ACK reply should report the requested write size
 * and pass a write payload into the fake DC link service.
 */
static void dm_mst_test_aux_transfer_native_write(struct kunit *test)
{
	struct amdgpu_dm_dp_aux *dm_aux;
	struct amdgpu_device *adev;
	struct ddc_service *ddc;
	struct dc_link *link;
	struct dc *dc;
	struct link_service *link_srv;
	struct dc_context *ctx;
	u8 buffer[2] = { 0x11, 0x22 };
	ssize_t ret;

	dm_aux = kunit_kzalloc(test, sizeof(*dm_aux), GFP_KERNEL);
	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_aux);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ddc);
	KUNIT_ASSERT_NOT_NULL(test, link);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dm_mst_test_setup_dm_aux(dm_aux, ddc, link, dc, link_srv, ctx, adev);

	ret = drm_dp_dpcd_write(&dm_aux->aux, 7, buffer, sizeof(buffer));

	KUNIT_EXPECT_EQ(test, ret, (ssize_t)sizeof(buffer));
	KUNIT_EXPECT_TRUE(test, dm_mst_test_last_payload.write);
	KUNIT_EXPECT_FALSE(test, dm_mst_test_last_payload.i2c_over_aux);
	KUNIT_EXPECT_EQ(test, dm_mst_test_last_payload.address, 7U);
	KUNIT_EXPECT_EQ(test, dm_mst_test_last_payload.length,
			(u32)sizeof(buffer));
}

/**
 * dm_mst_test_aux_transfer_partial_write - partial write reports byte count.
 * @test: KUnit test context.
 *
 * A positive write result from the DC link service should be interpreted as a
 * partial write and replaced with the first payload byte.
 */
static void dm_mst_test_aux_transfer_partial_write(struct kunit *test)
{
	struct amdgpu_dm_dp_aux *dm_aux;
	struct amdgpu_device *adev;
	struct ddc_service *ddc;
	struct dc_link *link;
	struct dc *dc;
	struct link_service *link_srv;
	struct dc_context *ctx;
	u8 buffer[2] = { 1, 0xaa };
	struct drm_dp_aux_msg msg = {
		.address = 7,
		.request = DP_AUX_NATIVE_WRITE,
		.buffer = buffer,
		.size = sizeof(buffer),
	};
	ssize_t ret;

	dm_aux = kunit_kzalloc(test, sizeof(*dm_aux), GFP_KERNEL);
	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_aux);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ddc);
	KUNIT_ASSERT_NOT_NULL(test, link);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dm_mst_test_setup_dm_aux(dm_aux, ddc, link, dc, link_srv, ctx, adev);
	dm_mst_test_aux_transfer_raw_result = 1;

	ret = dm_dp_aux_transfer(&dm_aux->aux, &msg);

	KUNIT_EXPECT_EQ(test, ret, (ssize_t)buffer[0]);
	KUNIT_EXPECT_TRUE(test, dm_mst_test_last_payload.write);
	KUNIT_EXPECT_EQ(test, dm_mst_test_last_payload.address, 7U);
}

/**
 * dm_mst_test_aux_transfer_error_result - transfer errors are remapped.
 * @test: KUnit test context.
 *
 * A negative DC link service result should be converted through
 * dm_dp_aux_transfer_result() using the returned AUX operation result.
 */
static void dm_mst_test_aux_transfer_error_result(struct kunit *test)
{
	struct amdgpu_dm_dp_aux *dm_aux;
	struct amdgpu_device *adev;
	struct ddc_service *ddc;
	struct dc_link *link;
	struct dc *dc;
	struct link_service *link_srv;
	struct dc_context *ctx;
	u8 buffer[2] = { 0 };
	ssize_t ret;

	dm_aux = kunit_kzalloc(test, sizeof(*dm_aux), GFP_KERNEL);
	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_aux);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ddc);
	KUNIT_ASSERT_NOT_NULL(test, link);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dm_mst_test_setup_dm_aux(dm_aux, ddc, link, dc, link_srv, ctx, adev);
	dm_mst_test_aux_transfer_raw_result = -EIO;
	dm_mst_test_aux_transfer_raw_operation_result = AUX_RET_ERROR_TIMEOUT;

	ret = drm_dp_dpcd_read(&dm_aux->aux, 4, buffer, sizeof(buffer));

	KUNIT_EXPECT_EQ(test, ret, (ssize_t)-ETIMEDOUT);
	KUNIT_EXPECT_FALSE(test, dm_mst_test_last_payload.write);
	KUNIT_EXPECT_EQ(test, dm_mst_test_last_payload.address, 4U);
}

/**
 * dm_mst_test_aux_transfer_hpd_discon_quirk - HPD disconnect quirk succeeds.
 * @test: KUnit test context.
 *
 * AUX_RET_ERROR_HPD_DISCON on the sideband down request address should be
 * treated as a successful transfer when the platform quirk is enabled.
 */
static void dm_mst_test_aux_transfer_hpd_discon_quirk(struct kunit *test)
{
	struct amdgpu_dm_dp_aux *dm_aux;
	struct amdgpu_device *adev;
	struct ddc_service *ddc;
	struct dc_link *link;
	struct dc *dc;
	struct link_service *link_srv;
	struct dc_context *ctx;
	u8 buffer[2] = { 2, 0 };
	ssize_t ret;

	dm_aux = kunit_kzalloc(test, sizeof(*dm_aux), GFP_KERNEL);
	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_aux);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ddc);
	KUNIT_ASSERT_NOT_NULL(test, link);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dm_mst_test_setup_dm_aux(dm_aux, ddc, link, dc, link_srv, ctx, adev);
	adev->dm.aux_hpd_discon_quirk = true;
	dm_mst_test_aux_transfer_raw_result = -EIO;
	dm_mst_test_aux_transfer_raw_operation_result = AUX_RET_ERROR_HPD_DISCON;

	ret = drm_dp_dpcd_write(&dm_aux->aux, DP_SIDEBAND_MSG_DOWN_REQ_BASE,
					 buffer, sizeof(buffer));

	KUNIT_EXPECT_EQ(test, ret, (ssize_t)sizeof(buffer));
	KUNIT_EXPECT_TRUE(test, dm_mst_test_last_payload.write);
	KUNIT_EXPECT_EQ(test, dm_mst_test_last_payload.address,
			DP_SIDEBAND_MSG_DOWN_REQ_BASE);
}

/**
 * dm_mst_test_aux_transfer_non_ack_reply - non-ACK AUX reply is logged.
 * @test: KUnit test context.
 *
 * A successful read with a nonzero reply byte should still return the backend
 * byte count while exercising the non-ACK reply handling path.
 */
static void dm_mst_test_aux_transfer_non_ack_reply(struct kunit *test)
{
	struct amdgpu_dm_dp_aux *dm_aux;
	struct amdgpu_device *adev;
	struct ddc_service *ddc;
	struct dc_link *link;
	struct dc *dc;
	struct link_service *link_srv;
	struct dc_context *ctx;
	u8 buffer[2] = { 0 };
	struct drm_dp_aux_msg msg = {
		.address = 4,
		.request = DP_AUX_NATIVE_READ,
		.buffer = buffer,
		.size = sizeof(buffer),
	};
	ssize_t ret;

	dm_aux = kunit_kzalloc(test, sizeof(*dm_aux), GFP_KERNEL);
	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dm_aux);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, ddc);
	KUNIT_ASSERT_NOT_NULL(test, link);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dm_mst_test_setup_dm_aux(dm_aux, ddc, link, dc, link_srv, ctx, adev);
	dm_mst_test_aux_transfer_raw_reply = DP_AUX_NATIVE_REPLY_NACK;

	ret = dm_dp_aux_transfer(&dm_aux->aux, &msg);

	KUNIT_EXPECT_EQ(test, ret, (ssize_t)sizeof(buffer));
	KUNIT_EXPECT_EQ(test, dm_mst_test_last_payload.address, 4U);
}

/**
 * dm_mst_test_fill_payload_flags_native_write - native write request decode.
 * @test: KUnit test context.
 *
 * DP_AUX_NATIVE_WRITE clears i2c_over_aux and sets write; no I2C bits set.
 */
static void dm_mst_test_fill_payload_flags_native_write(struct kunit *test)
{
	struct aux_payload payload = { 0 };

	dm_dp_aux_fill_payload_flags(DP_AUX_NATIVE_WRITE, &payload);

	KUNIT_EXPECT_FALSE(test, payload.i2c_over_aux);
	KUNIT_EXPECT_TRUE(test, payload.write);
	KUNIT_EXPECT_FALSE(test, payload.mot);
	KUNIT_EXPECT_FALSE(test, payload.write_status_update);
}

/**
 * dm_mst_test_fill_payload_flags_native_read - native read request decode.
 * @test: KUnit test context.
 *
 * DP_AUX_NATIVE_READ keeps i2c_over_aux clear; the I2C_READ bit clears write.
 */
static void dm_mst_test_fill_payload_flags_native_read(struct kunit *test)
{
	struct aux_payload payload = { 0 };

	dm_dp_aux_fill_payload_flags(DP_AUX_NATIVE_READ, &payload);

	KUNIT_EXPECT_FALSE(test, payload.i2c_over_aux);
	KUNIT_EXPECT_FALSE(test, payload.write);
	KUNIT_EXPECT_FALSE(test, payload.mot);
}

/**
 * dm_mst_test_fill_payload_flags_i2c_read_mot - I2C read with MOT request decode.
 * @test: KUnit test context.
 *
 * DP_AUX_I2C_READ sets i2c_over_aux and clears write; DP_AUX_I2C_MOT sets mot.
 */
static void dm_mst_test_fill_payload_flags_i2c_read_mot(struct kunit *test)
{
	struct aux_payload payload = { 0 };

	dm_dp_aux_fill_payload_flags(DP_AUX_I2C_READ | DP_AUX_I2C_MOT, &payload);

	KUNIT_EXPECT_TRUE(test, payload.i2c_over_aux);
	KUNIT_EXPECT_FALSE(test, payload.write);
	KUNIT_EXPECT_TRUE(test, payload.mot);
}

/**
 * dm_mst_test_fill_payload_flags_write_status - write status update decode.
 * @test: KUnit test context.
 *
 * DP_AUX_I2C_WRITE_STATUS_UPDATE sets write_status_update.
 */
static void dm_mst_test_fill_payload_flags_write_status(struct kunit *test)
{
	struct aux_payload payload = { 0 };

	dm_dp_aux_fill_payload_flags(DP_AUX_I2C_WRITE | DP_AUX_I2C_WRITE_STATUS_UPDATE,
				     &payload);

	KUNIT_EXPECT_TRUE(test, payload.i2c_over_aux);
	KUNIT_EXPECT_TRUE(test, payload.write_status_update);
}

/**
 * dm_mst_test_msg_ready_mask - ESI mask selection per message-ready type.
 * @test: KUnit test context.
 *
 * DOWN_REP and UP_REQ each select their single bit; other types select both.
 */
static void dm_mst_test_msg_ready_mask(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, dm_mst_msg_ready_mask(DOWN_REP_MSG_RDY_EVENT),
			(u8)DP_DOWN_REP_MSG_RDY);
	KUNIT_EXPECT_EQ(test, dm_mst_msg_ready_mask(UP_REQ_MSG_RDY_EVENT),
			(u8)DP_UP_REQ_MSG_RDY);
	KUNIT_EXPECT_EQ(test, dm_mst_msg_ready_mask(DOWN_OR_UP_MSG_RDY_EVENT),
			(u8)(DP_DOWN_REP_MSG_RDY | DP_UP_REQ_MSG_RDY));
	KUNIT_EXPECT_EQ(test, dm_mst_msg_ready_mask(NONE_MSG_RDY_EVENT),
			(u8)(DP_DOWN_REP_MSG_RDY | DP_UP_REQ_MSG_RDY));
}

/**
 * dm_mst_test_select_esi_dpcd_legacy - pre-1.2 DPCD ESI address/length.
 * @test: KUnit test context.
 *
 * For DPCD rev < 0x12 the legacy DP_SINK_COUNT address/length pair is selected.
 */
static void dm_mst_test_select_esi_dpcd_legacy(struct kunit *test)
{
	int dpcd_addr = -1;
	u8 dpcd_bytes_to_read = 0;

	dm_mst_select_esi_dpcd(0x11, &dpcd_addr, &dpcd_bytes_to_read);

	KUNIT_EXPECT_EQ(test, dpcd_addr, DP_SINK_COUNT);
	KUNIT_EXPECT_EQ(test, (int)dpcd_bytes_to_read,
			(int)(DP_LANE0_1_STATUS - DP_SINK_COUNT));
}

/**
 * dm_mst_test_select_esi_dpcd_esi - 1.2+ DPCD ESI address/length.
 * @test: KUnit test context.
 *
 * For DPCD rev >= 0x12 the ESI DP_SINK_COUNT_ESI address/length pair is selected.
 */
static void dm_mst_test_select_esi_dpcd_esi(struct kunit *test)
{
	int dpcd_addr = -1;
	u8 dpcd_bytes_to_read = 0;

	dm_mst_select_esi_dpcd(0x14, &dpcd_addr, &dpcd_bytes_to_read);

	KUNIT_EXPECT_EQ(test, dpcd_addr, DP_SINK_COUNT_ESI);
	KUNIT_EXPECT_EQ(test, (int)dpcd_bytes_to_read,
			(int)(DP_PSR_ERROR_STATUS - DP_SINK_COUNT_ESI));
}

/**
 * dm_mst_test_sideband_msg_ready_no_ready_bits - Test idle sideband event
 * @test: KUnit test context
 *
 * Verify that dm_handle_mst_sideband_msg_ready_event() returns cleanly when
 * the ESI read succeeds but no DOWN_REP/UP_REQ ready bits are set.
 */
static void dm_mst_test_sideband_msg_ready_no_ready_bits(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = dm_mst_test_alloc_sideband_connector(test);

	dm_handle_mst_sideband_msg_ready_event(&aconnector->mst_mgr,
					       DOWN_REP_MSG_RDY_EVENT);

	KUNIT_EXPECT_EQ(test, dm_mst_test_dpcd[1], (u8)0);
}

/**
 * dm_mst_test_sideband_msg_ready_read_error - Test ESI read failure path
 * @test: KUnit test context
 *
 * Verify that dm_handle_mst_sideband_msg_ready_event() returns cleanly when
 * the DPCD read fails before a ready bit can be handled.
 */
static void dm_mst_test_sideband_msg_ready_read_error(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = dm_mst_test_alloc_sideband_connector(test);
	dm_mst_test_aux_transfer_override = -EIO;

	dm_handle_mst_sideband_msg_ready_event(&aconnector->mst_mgr,
					       DOWN_REP_MSG_RDY_EVENT);

	KUNIT_EXPECT_EQ(test, dm_mst_test_dpcd[1], (u8)0);
	dm_mst_test_aux_transfer_override = 0;
}

/**
 * dm_mst_test_sideband_msg_ready_without_mst_state - Test ready bit no-op path
 * @test: KUnit test context
 *
 * Verify that a DOWN_REP ready bit is filtered and then ignored when the MST
 * topology manager is not enabled.
 */
static void dm_mst_test_sideband_msg_ready_without_mst_state(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = dm_mst_test_alloc_sideband_connector(test);
	dm_mst_test_dpcd[(DP_SINK_COUNT_ESI + 1) & 0xf] = DP_DOWN_REP_MSG_RDY;

	dm_handle_mst_sideband_msg_ready_event(&aconnector->mst_mgr,
					       DOWN_REP_MSG_RDY_EVENT);

	KUNIT_EXPECT_EQ(test, dm_mst_test_dpcd[(DP_SINK_COUNT_ESI + 1) & 0xf],
			 DP_DOWN_REP_MSG_RDY);
}

/**
 * dm_mst_test_down_rep_msg_ready_wrapper - Test DOWN_REP wrapper
 * @test: KUnit test context
 *
 * Verify that dm_handle_mst_down_rep_msg_ready() forwards to the generic MST
 * sideband handler with the DOWN_REP event selection.
 */
static void dm_mst_test_down_rep_msg_ready_wrapper(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = dm_mst_test_alloc_sideband_connector(test);

	dm_handle_mst_down_rep_msg_ready(&aconnector->mst_mgr);

	KUNIT_EXPECT_EQ(test, dm_mst_test_dpcd[1], (u8)0);
}

/**
 * dm_mst_test_initialize_dp_connector_edp - Test eDP initialization path
 * @test: KUnit test context
 *
 * Verify that amdgpu_dm_initialize_dp_connector() initializes the DP AUX state
 * and exits before MST topology setup for eDP connectors.
 */
static void dm_mst_test_initialize_dp_connector_edp(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_device *adev;
	struct ddc_service *ddc;
	struct dc_link *link;

	adev = dm_kunit_alloc_adev(test);
	link = dm_kunit_alloc_link(test);
	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ddc);

	adev->dm.adev = adev;
	adev->dm.ddev = &adev->ddev;
	link->ddc = ddc;
	aconnector = dm_kunit_alloc_connector(test, adev, link);
	aconnector->base.connector_type = DRM_MODE_CONNECTOR_eDP;

	amdgpu_dm_initialize_dp_connector(&adev->dm, aconnector, 5);

	KUNIT_EXPECT_TRUE(test, aconnector->dm_dp_aux.aux.transfer == dm_dp_aux_transfer);
	KUNIT_EXPECT_PTR_EQ(test, aconnector->dm_dp_aux.aux.drm_dev, &adev->ddev);
	KUNIT_EXPECT_PTR_EQ(test, aconnector->dm_dp_aux.ddc_service, ddc);
	KUNIT_EXPECT_PTR_EQ(test, aconnector->mst_mgr.dev, NULL);
	KUNIT_EXPECT_NOT_NULL(test, aconnector->dm_dp_aux.aux.name);
	if (aconnector->dm_dp_aux.aux.name)
		KUNIT_EXPECT_NOT_NULL(test, strstr(aconnector->dm_dp_aux.aux.name, "5"));

	drm_dp_cec_unregister_connector(&aconnector->dm_dp_aux.aux);
	kfree(aconnector->dm_dp_aux.aux.name);
}

static bool dm_mst_test_dp_get_max_link_enc_cap(const struct dc_link *link,
						struct dc_link_settings *cap)
{
	return true;
}

static void dm_mst_test_connector_destroy(struct drm_connector *connector)
{
}

static const struct drm_connector_funcs dm_mst_test_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.destroy = dm_mst_test_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/**
 * dm_mst_test_initialize_dp_connector_mst - Test MST root initialization path
 * @test: KUnit test context
 *
 * Verify that amdgpu_dm_initialize_dp_connector() initializes the MST topology
 * manager for a non-eDP DisplayPort connector. This exercises the path past the
 * eDP early return, including dc_link_dp_get_max_link_enc_cap() and
 * drm_dp_mst_topology_mgr_init(). A fully initialized DRM mode config and
 * connector are required because the topology manager registers a private
 * atomic object and the subconnector property is attached to the connector.
 */
static void dm_mst_test_initialize_dp_connector_mst(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_device *adev;
	struct link_service *link_srv;
	struct ddc_service *ddc;
	struct dc_link *link;
	struct dc *dc;
	int ret;

	adev = dm_kunit_alloc_adev(test);

	ret = drmm_mode_config_init(&adev->ddev);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	link = dm_kunit_alloc_link(test);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ddc);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);

	link_srv->dp_get_max_link_enc_cap = dm_mst_test_dp_get_max_link_enc_cap;
	dc->link_srv = link_srv;
	link->dc = dc;
	link->ddc = ddc;

	adev->dm.adev = adev;
	adev->dm.ddev = &adev->ddev;

	aconnector = dm_kunit_alloc_connector(test, adev, link);

	ret = drm_connector_init(&adev->ddev, &aconnector->base,
				 &dm_mst_test_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	KUNIT_ASSERT_EQ(test, ret, 0);

	amdgpu_dm_initialize_dp_connector(&adev->dm, aconnector, 7);

	KUNIT_EXPECT_TRUE(test, aconnector->dm_dp_aux.aux.transfer == dm_dp_aux_transfer);
	KUNIT_EXPECT_PTR_EQ(test, aconnector->mst_mgr.dev, &adev->ddev);
	KUNIT_EXPECT_PTR_EQ(test, aconnector->mst_mgr.aux, &aconnector->dm_dp_aux.aux);
	KUNIT_EXPECT_EQ(test, aconnector->mst_mgr.max_payloads, 4);
	KUNIT_EXPECT_TRUE(test, aconnector->mst_mgr.cbs != NULL);

	drm_dp_mst_topology_mgr_destroy(&aconnector->mst_mgr);
	drm_dp_cec_unregister_connector(&aconnector->dm_dp_aux.aux);
	kfree(aconnector->dm_dp_aux.aux.name);
	drm_connector_cleanup(&aconnector->base);
}

/**
 * dm_mst_test_atomic_best_encoder - Test MST encoder selection
 * @test: KUnit test context
 *
 * Verify that dm_mst_atomic_best_encoder() selects the MST encoder indexed by
 * the CRTC ID in the connector's new atomic state. This uses structural DRM
 * mocks only; registering connector/CRTC objects is unnecessary for this helper.
 */
static void dm_mst_test_atomic_best_encoder(struct kunit *test)
{
	struct drm_connector_state connector_state = { 0 };
	struct drm_atomic_commit state = { 0 };
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_device *adev;
	struct amdgpu_crtc *acrtc;
	unsigned int connector_index = 3;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	acrtc = kunit_kzalloc(test, sizeof(*acrtc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, adev);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	KUNIT_ASSERT_NOT_NULL(test, acrtc);

	aconnector->base.dev = &adev->ddev;
	aconnector->base.index = connector_index;
	acrtc->crtc_id = 2;
	connector_state.connector = &aconnector->base;
	connector_state.crtc = &acrtc->base;
	state.num_connector = connector_index + 1;
	state.connectors = kunit_kzalloc(test,
					 sizeof(*state.connectors) * state.num_connector,
					 GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state.connectors);
	state.connectors[connector_index].ptr = &aconnector->base;
	state.connectors[connector_index].new_state = &connector_state;

	KUNIT_EXPECT_PTR_EQ(test, dm_mst_atomic_best_encoder(&aconnector->base, &state),
			     &adev->dm.mst_encoders[2].base);
}

/**
 * dm_mst_test_create_fake_mst_encoders - Test fake MST encoder setup
 * @test: KUnit test context
 *
 * Verify that dm_dp_create_fake_mst_encoders() initializes the requested MST
 * encoders as DPMST encoders with the CRTC mask derived from the device state.
 */
static void dm_mst_test_create_fake_mst_encoders(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_device *drm;
	int i;

	adev = dm_kunit_alloc_adev(test);
	drm = &adev->ddev;
	adev->dm.display_indexes_num = 3;
	adev->mode_info.num_crtc = 3;

	dm_dp_create_fake_mst_encoders(adev);

	for (i = 0; i < adev->dm.display_indexes_num; i++) {
		struct drm_encoder *encoder = &adev->dm.mst_encoders[i].base;

		KUNIT_EXPECT_PTR_EQ(test, encoder->dev, drm);
		KUNIT_EXPECT_EQ(test, encoder->encoder_type, DRM_MODE_ENCODER_DPMST);
		KUNIT_EXPECT_EQ(test, encoder->possible_crtcs, 0x7U);
		KUNIT_EXPECT_TRUE(test, encoder->helper_private != NULL);
	}
}

/**
 * dm_mst_test_atomic_check_no_old_crtc - Test atomic check no-op path
 * @test: KUnit test context
 *
 * Verify that dm_dp_mst_atomic_check() returns success when the MST port's old
 * connector state has no CRTC, before MST topology state is required.
 */
static void dm_mst_test_atomic_check_no_old_crtc(struct kunit *test)
{
	struct drm_connector_state *old_conn_state;
	struct drm_connector_state *new_conn_state;
	struct drm_atomic_commit *state;
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_dm_connector *root;
	struct drm_dp_mst_port *port;
	unsigned int connector_index = 2;

	old_conn_state = kunit_kzalloc(test, sizeof(*old_conn_state), GFP_KERNEL);
	new_conn_state = kunit_kzalloc(test, sizeof(*new_conn_state), GFP_KERNEL);
	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	root = kunit_kzalloc(test, sizeof(*root), GFP_KERNEL);
	port = kunit_kzalloc(test, sizeof(*port), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_conn_state);
	KUNIT_ASSERT_NOT_NULL(test, new_conn_state);
	KUNIT_ASSERT_NOT_NULL(test, state);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	KUNIT_ASSERT_NOT_NULL(test, root);
	KUNIT_ASSERT_NOT_NULL(test, port);

	aconnector->base.index = connector_index;
	aconnector->mst_root = root;
	aconnector->mst_output_port = port;
	port->connector = &aconnector->base;
	old_conn_state->connector = &aconnector->base;
	new_conn_state->connector = &aconnector->base;
	state->num_connector = connector_index + 1;
	state->connectors = kunit_kzalloc(test,
					 sizeof(*state->connectors) * state->num_connector,
					 GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state->connectors);
	state->connectors[connector_index].ptr = &aconnector->base;
	state->connectors[connector_index].old_state = old_conn_state;
	state->connectors[connector_index].new_state = new_conn_state;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_atomic_check(&aconnector->base, state), 0);
}

/**
 * dm_mst_test_detect_unregistered - Test detect skips unregistered connector
 * @test: KUnit test context
 *
 * Verify that dm_dp_mst_detect() returns disconnected for an unregistered
 * connector before calling into the MST topology helper.
 */
static void dm_mst_test_detect_unregistered(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);

	aconnector->base.registration_state = DRM_CONNECTOR_UNREGISTERED;

	KUNIT_EXPECT_EQ(test,
			dm_dp_mst_detect(&aconnector->base, NULL, false),
			(int)connector_status_disconnected);
}

#if !defined(CONFIG_DRM_AMD_DC_FP)
/**
 * dm_mst_test_fp_guarded_public_stubs - Test FP-off public fallbacks
 * @test: KUnit test context
 *
 * When CONFIG_DRM_AMD_DC_FP is disabled, the public DSC validation helper
 * has no FP body and must return DC_OK without touching its arguments.
 */
static void dm_mst_test_fp_guarded_public_stubs(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, dm_dp_mst_is_port_support_mode(NULL, NULL),
			(enum dc_status)DC_OK);
}
#endif

static struct kunit_case dm_mst_types_test_cases[] = {
	/* needs_dsc_aux_workaround tests */
	KUNIT_CASE(dm_mst_test_needs_dsc_aux_workaround_match),
	KUNIT_CASE(dm_mst_test_needs_dsc_aux_workaround_rev12),
	KUNIT_CASE(dm_mst_test_needs_dsc_aux_workaround_wrong_dev_id),
	KUNIT_CASE(dm_mst_test_needs_dsc_aux_workaround_wrong_rev),
	KUNIT_CASE(dm_mst_test_needs_dsc_aux_workaround_low_sink_count),
	KUNIT_CASE(dm_mst_test_needs_dsc_aux_workaround_zero_sink_count),
	/* dm_mst_get_pbn_divider tests */
	KUNIT_CASE(dm_mst_test_pbn_divider_null_link),
	KUNIT_CASE(dm_mst_test_pbn_divider_uses_link_bandwidth),
	/* amdgpu_dm_mst_reset_mst_connector_setting tests */
	KUNIT_CASE(dm_mst_test_reset_connector_setting),
	/* retrieve_downstream_port_device tests */
	KUNIT_CASE(dm_mst_test_retrieve_downstream_no_aux),
	KUNIT_CASE(dm_mst_test_retrieve_downstream_present),
	KUNIT_CASE(dm_mst_test_retrieve_downstream_aux_error),
	/* retrieve_branch_specific_data tests */
	KUNIT_CASE(dm_mst_test_retrieve_branch_no_parent),
	KUNIT_CASE(dm_mst_test_retrieve_branch_reads_oui),
	/* dm_dp_aux_transfer_result tests */
	KUNIT_CASE(dm_mst_test_aux_result_success),
	KUNIT_CASE(dm_mst_test_aux_result_eio),
	KUNIT_CASE(dm_mst_test_aux_result_ebusy),
	KUNIT_CASE(dm_mst_test_aux_result_timeout),
	KUNIT_CASE(dm_mst_test_aux_transfer_native_read),
	KUNIT_CASE(dm_mst_test_aux_transfer_native_write),
	KUNIT_CASE(dm_mst_test_aux_transfer_partial_write),
	KUNIT_CASE(dm_mst_test_aux_transfer_error_result),
	KUNIT_CASE(dm_mst_test_aux_transfer_hpd_discon_quirk),
	KUNIT_CASE(dm_mst_test_aux_transfer_non_ack_reply),
	/* dm_dp_aux_fill_payload_flags tests */
	KUNIT_CASE(dm_mst_test_fill_payload_flags_native_write),
	KUNIT_CASE(dm_mst_test_fill_payload_flags_native_read),
	KUNIT_CASE(dm_mst_test_fill_payload_flags_i2c_read_mot),
	KUNIT_CASE(dm_mst_test_fill_payload_flags_write_status),
	/* dm_mst_msg_ready_mask tests */
	KUNIT_CASE(dm_mst_test_msg_ready_mask),
	/* dm_mst_select_esi_dpcd tests */
	KUNIT_CASE(dm_mst_test_select_esi_dpcd_legacy),
	KUNIT_CASE(dm_mst_test_select_esi_dpcd_esi),
	/* dm_handle_mst_sideband_msg_ready_event tests */
	KUNIT_CASE(dm_mst_test_sideband_msg_ready_no_ready_bits),
	KUNIT_CASE(dm_mst_test_sideband_msg_ready_read_error),
	KUNIT_CASE(dm_mst_test_sideband_msg_ready_without_mst_state),
	KUNIT_CASE(dm_mst_test_down_rep_msg_ready_wrapper),
	/* amdgpu_dm_initialize_dp_connector tests */
	KUNIT_CASE(dm_mst_test_initialize_dp_connector_edp),
	KUNIT_CASE(dm_mst_test_initialize_dp_connector_mst),
	/* dm_mst_atomic_best_encoder tests */
	KUNIT_CASE(dm_mst_test_atomic_best_encoder),
	/* dm_dp_create_fake_mst_encoders tests */
	KUNIT_CASE(dm_mst_test_create_fake_mst_encoders),
	/* dm_dp_mst_atomic_check tests */
	KUNIT_CASE(dm_mst_test_atomic_check_no_old_crtc),
	/* dm_dp_mst_detect tests */
	KUNIT_CASE(dm_mst_test_detect_unregistered),
	/* CONFIG_DRM_AMD_DC_FP disabled public paths */
#if !defined(CONFIG_DRM_AMD_DC_FP)
	KUNIT_CASE(dm_mst_test_fp_guarded_public_stubs),
#endif
	{}
};

static struct kunit_suite dm_mst_types_test_suite = {
	.name = "amdgpu_dm_mst_types",
	.test_cases = dm_mst_types_test_cases,
};

kunit_test_suite(dm_mst_types_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_mst_types");
