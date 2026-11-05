// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_mst_types.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>

#include <drm/drm_drv.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_edid.h>
#include <drm/drm_fixed.h>
#include <drm/drm_kunit_helpers.h>
#include <drm/drm_managed.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_modeset_lock.h>
#include <drm/drm_property.h>
#include <drm/display/drm_dp.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dp_mst_helper.h>

#include "dc.h"
#include "dpcd_defs.h"
#include "dmub_cmd.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_hdcp.h"
#include "amdgpu_dm_mst_types.h"
#include "amdgpu_dm_kunit_test_helpers.h"
#include "dsc/dsc.h"
#include "inc/core_types.h"
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
static ssize_t dm_mst_test_aux_write_override;

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
		if (dm_mst_test_aux_write_override)
			return dm_mst_test_aux_write_override;
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
	dm_mst_test_aux_write_override = 0;

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
 * dm_mst_test_aux_transfer_oversized - oversized AUX message is rejected.
 * @test: KUnit test context.
 *
 * The payload is copied into a 16-byte stack buffer, so a larger message must
 * be rejected with -E2BIG before the AUX handle is touched. The guard warns,
 * which is the expected behaviour for this caller bug.
 */
static void dm_mst_test_aux_transfer_oversized(struct kunit *test)
{
	struct drm_dp_aux_msg msg = { 0 };
	u8 buffer[17] = { 0 };

	msg.request = DP_AUX_NATIVE_WRITE;
	msg.buffer = buffer;
	msg.size = sizeof(buffer);

	KUNIT_EXPECT_EQ(test, dm_dp_aux_transfer(NULL, &msg), (ssize_t)-E2BIG);
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

/* Tests for dm_dp_add_mst_connector */

struct dm_mst_test_add_ctx {
	struct amdgpu_device *adev;
	struct amdgpu_dm_connector *master;
	struct drm_dp_mst_port *port;
	struct dc_link *link;
};

static void dm_mst_test_cleanup_add_ctx(void *data)
{
	struct amdgpu_dm_connector *master = data;

	drm_connector_cleanup(&master->base);
}

/*
 * Stand in for amdgpu_display_modeset_create_props(), which the test module
 * cannot link against. Only the properties amdgpu_dm_connector_init_helper()
 * attaches are created.
 */
static void dm_mst_test_create_mode_props(struct kunit *test, struct amdgpu_device *adev)
{
	struct amdgpu_mode_info *mode_info = &adev->mode_info;
	struct drm_device *drm = adev_to_drm(adev);

	KUNIT_ASSERT_EQ(test, drm_mode_create_scaling_mode_property(drm), 0);

	mode_info->underscan_property =
		drm_property_create_range(drm, 0, "underscan", 0, 1);
	mode_info->underscan_hborder_property =
		drm_property_create_range(drm, 0, "underscan hborder", 0, 128);
	mode_info->underscan_vborder_property =
		drm_property_create_range(drm, 0, "underscan vborder", 0, 128);
	KUNIT_ASSERT_NOT_NULL(test, mode_info->underscan_property);
	KUNIT_ASSERT_NOT_NULL(test, mode_info->underscan_hborder_property);
	KUNIT_ASSERT_NOT_NULL(test, mode_info->underscan_vborder_property);
}

/*
 * Build the MST root connector and the topology port the new downstream
 * connector hangs off. The port has no parent branch so the callback takes the
 * "no branch descriptor" path without needing a sideband AUX backend.
 */
static void dm_mst_test_init_add_ctx(struct kunit *test, struct dm_mst_test_add_ctx *ctx)
{
	struct amdgpu_device *adev;
	struct drm_device *drm;
	int ret;

	adev = dm_kunit_alloc_adev(test);
	drm = &adev->ddev;
	ret = drmm_mode_config_init(drm);
	KUNIT_ASSERT_EQ(test, ret, 0);
	dm_mst_test_create_mode_props(test, adev);

	adev->dm.adev = adev;
	adev->dm.ddev = drm;
	adev->dm.display_indexes_num = 2;
	adev->mode_info.num_crtc = 2;
	dm_dp_create_fake_mst_encoders(adev);

	ctx->adev = adev;
	ctx->link = dm_kunit_alloc_link(test);
	/* Inflexible DIG mapping lets link_enc_cfg_get_link_enc() skip the dc lookup. */
	ctx->link->link_enc = kunit_kzalloc(test, sizeof(*ctx->link->link_enc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->link->link_enc);

	ctx->master = dm_kunit_alloc_connector(test, adev, ctx->link);
	ctx->master->connector_id = 5;
	ret = drm_connector_init(drm, &ctx->master->base, &dm_mst_test_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_mst_test_cleanup_add_ctx, ctx->master), 0);

	ctx->port = kunit_kzalloc(test, sizeof(*ctx->port), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->port);
	kref_init(&ctx->port->malloc_kref);
	ctx->port->mgr = &ctx->master->mst_mgr;
}

/* True when @prop is attached to the connector's mode object. */
static bool dm_mst_test_has_prop(struct drm_connector *connector, struct drm_property *prop)
{
	struct drm_object_properties *props = connector->base.properties;
	int i;

	if (!prop)
		return false;

	for (i = 0; i < props->count; i++)
		if (props->properties[i] == prop)
			return true;

	return false;
}

/**
 * dm_mst_test_add_mst_connector_creates - Test downstream connector creation
 * @test: KUnit test context
 *
 * The topology callback must allocate a DisplayPort connector bound to the port
 * and the root connector, inherit the root's dc_link and connector id, attach
 * every fake MST encoder, publish the path property and take a malloc
 * reference on the port.
 */
static void dm_mst_test_add_mst_connector_creates(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct dm_mst_test_add_ctx ctx;
	struct drm_connector *connector;

	dm_mst_test_init_add_ctx(test, &ctx);

	connector = dm_dp_add_mst_connector(&ctx.master->mst_mgr, ctx.port, "1-3");
	KUNIT_ASSERT_NOT_NULL(test, connector);

	aconnector = to_amdgpu_dm_connector(connector);
	KUNIT_EXPECT_PTR_EQ(test, aconnector->mst_output_port, ctx.port);
	KUNIT_EXPECT_PTR_EQ(test, aconnector->mst_root, ctx.master);
	KUNIT_EXPECT_PTR_EQ(test, aconnector->dc_link, ctx.link);
	KUNIT_EXPECT_EQ(test, aconnector->connector_id, 5);
	KUNIT_EXPECT_TRUE(test, aconnector->mst_status & MST_PROBE);
	KUNIT_EXPECT_EQ(test, aconnector->branch_ieee_oui, 0U);
	KUNIT_EXPECT_EQ(test, connector->connector_type, DRM_MODE_CONNECTOR_DisplayPort);
	KUNIT_EXPECT_EQ(test, connector->possible_encoders, 0x3U);
	KUNIT_EXPECT_NOT_NULL(test, connector->state);
	KUNIT_EXPECT_NOT_NULL(test, connector->path_blob_ptr);
	KUNIT_EXPECT_EQ(test, kref_read(&ctx.port->malloc_kref), 2U);
	KUNIT_EXPECT_TRUE(test,
			  dm_mst_test_has_prop(connector,
					       ctx.adev->ddev.mode_config.path_property));
	KUNIT_EXPECT_TRUE(test,
			  dm_mst_test_has_prop(connector,
					       ctx.adev->ddev.mode_config.tile_property));

	dm_dp_mst_connector_destroy(connector);
}

/**
 * dm_mst_test_add_mst_connector_inherits_props - Test optional property sharing
 * @test: KUnit test context
 *
 * Max bpc, VRR capable and colorspace properties are only attached to the new
 * connector when the root connector already owns them, so the downstream
 * connector shares the root's property objects.
 */
static void dm_mst_test_add_mst_connector_inherits_props(struct kunit *test)
{
	struct dm_mst_test_add_ctx ctx;
	struct drm_connector *connector;
	struct drm_device *drm;

	dm_mst_test_init_add_ctx(test, &ctx);
	drm = &ctx.adev->ddev;

	ctx.master->base.max_bpc_property =
		drm_property_create_range(drm, 0, "max bpc", 8, 16);
	ctx.master->base.vrr_capable_property =
		drm_property_create_bool(drm, DRM_MODE_PROP_IMMUTABLE, "vrr_capable");
	KUNIT_ASSERT_NOT_NULL(test, ctx.master->base.max_bpc_property);
	KUNIT_ASSERT_NOT_NULL(test, ctx.master->base.vrr_capable_property);
	KUNIT_ASSERT_EQ(test, drm_mode_create_dp_colorspace_property(&ctx.master->base, 0), 0);

	connector = dm_dp_add_mst_connector(&ctx.master->mst_mgr, ctx.port, "1-4");
	KUNIT_ASSERT_NOT_NULL(test, connector);

	KUNIT_EXPECT_PTR_EQ(test, connector->max_bpc_property, ctx.master->base.max_bpc_property);
	KUNIT_EXPECT_PTR_EQ(test, connector->vrr_capable_property,
			    ctx.master->base.vrr_capable_property);
	KUNIT_EXPECT_PTR_EQ(test, connector->colorspace_property,
			    ctx.master->base.colorspace_property);
	KUNIT_EXPECT_TRUE(test, dm_mst_test_has_prop(connector, connector->max_bpc_property));
	KUNIT_EXPECT_TRUE(test, dm_mst_test_has_prop(connector, connector->colorspace_property));

	dm_dp_mst_connector_destroy(connector);
}

/**
 * dm_mst_test_add_mst_connector_init_fails - Test the connector init failure path
 * @test: KUnit test context
 *
 * Exhausting the device connector index space makes drm_connector_dynamic_init()
 * fail, so the callback must free the connector it allocated, leave the port
 * malloc reference untouched and report no connector to the MST helpers.
 */
static void dm_mst_test_add_mst_connector_init_fails(struct kunit *test)
{
	struct dm_mst_test_add_ctx ctx;
	struct drm_device *drm;
	int id;

	dm_mst_test_init_add_ctx(test, &ctx);
	drm = &ctx.adev->ddev;

	/* drm_connector_init_only() takes an index out of the same [0, 31] space. */
	do {
		id = ida_alloc_max(&drm->mode_config.connector_ida, 31, GFP_KERNEL);
	} while (id >= 0);
	KUNIT_ASSERT_EQ(test, id, -ENOSPC);

	KUNIT_EXPECT_NULL(test, dm_dp_add_mst_connector(&ctx.master->mst_mgr, ctx.port, "1-5"));
	KUNIT_EXPECT_EQ(test, kref_read(&ctx.port->malloc_kref), 1U);
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

/*
 * Fake DC remote-sink backend. dc_link_add_remote_sink() and
 * dc_link_remove_remote_sink() are thin wrappers over these link_service
 * callbacks, so overriding them is enough to run the MST connector paths
 * without a DC core.
 */
static struct dc_sink *dm_mst_test_next_remote_sink;
static struct dc_sink *dm_mst_test_removed_sink;
static unsigned int dm_mst_test_add_remote_sink_calls;
static unsigned int dm_mst_test_remove_remote_sink_calls;

/* Signature copied verbatim from struct link_service::add_remote_sink. */
static struct dc_sink *dm_mst_test_add_remote_sink(
		struct dc_link *link,
		const uint8_t *edid,
		unsigned int len,
		struct dc_sink_init_data *init_data)
{
	struct dc_sink *sink = dm_mst_test_next_remote_sink;

	dm_mst_test_add_remote_sink_calls++;

	if (sink) {
		sink->link = link;
		sink->sink_signal = init_data->sink_signal;
		link->sink_count++;
	}

	return sink;
}

/* Signature copied verbatim from struct link_service::remove_remote_sink. */
static void dm_mst_test_remove_remote_sink(struct dc_link *link, struct dc_sink *sink)
{
	dm_mst_test_remove_remote_sink_calls++;
	dm_mst_test_removed_sink = sink;

	if (link->sink_count)
		link->sink_count--;
}

/*
 * dc_sink_release() frees the sink with kfree(), so these must not come from
 * the KUnit managed allocator.
 */
static struct dc_sink *dm_mst_test_alloc_sink(struct kunit *test)
{
	struct dc_sink *sink = kzalloc_obj(*sink);

	KUNIT_ASSERT_NOT_NULL(test, sink);
	kref_init(&sink->refcount);

	return sink;
}

struct dm_mst_test_child {
	struct amdgpu_device *adev;
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_dm_connector *root;
	struct drm_dp_mst_port *port;
	struct dc_link *link;
};

/*
 * Build an MST downstream connector hanging off a root connector. The topology
 * manager is left empty (no mst_primary) so the DRM MST helpers take their
 * "port is gone" paths instead of requiring a real branch device.
 */
static void dm_mst_test_init_child(struct kunit *test, struct dm_mst_test_child *child)
{
	struct link_service *link_srv;
	struct amdgpu_device *adev;
	struct dc *dc;
	int ret;

	adev = dm_kunit_alloc_adev(test);
	ret = drmm_mode_config_init(&adev->ddev);
	KUNIT_ASSERT_EQ(test, ret, 0);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	child->port = kunit_kzalloc(test, sizeof(*child->port), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dc);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, child->port);

	link_srv->add_remote_sink = dm_mst_test_add_remote_sink;
	link_srv->remove_remote_sink = dm_mst_test_remove_remote_sink;
	dc->link_srv = link_srv;

	child->adev = adev;
	child->link = dm_kunit_alloc_link(test);
	child->link->dc = dc;

	child->root = dm_kunit_alloc_connector(test, adev, child->link);
	child->root->mst_mgr.dev = &adev->ddev;
	mutex_init(&child->root->mst_mgr.lock);
	drm_modeset_lock_init(&child->root->mst_mgr.base.lock);

	child->port->mgr = &child->root->mst_mgr;
	child->port->aux.name = "dm_mst_test_port_aux";
	child->port->aux.transfer = dm_mst_test_aux_transfer;
	drm_dp_aux_init(&child->port->aux);
	drm_dp_dpcd_set_probe(&child->port->aux, false);

	child->aconnector = dm_kunit_alloc_connector(test, adev, child->link);
	child->aconnector->mst_root = child->root;
	child->aconnector->mst_output_port = child->port;

	ret = drm_connector_init(&adev->ddev, &child->aconnector->base,
				 &dm_mst_test_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	KUNIT_ASSERT_EQ(test, ret, 0);
	/* The MST (un)register helpers log through the connector's kernel device. */
	child->aconnector->base.kdev = adev->ddev.dev;

	memset(dm_mst_test_dpcd, 0, sizeof(dm_mst_test_dpcd));
	dm_mst_test_aux_transfer_override = 0;
	dm_mst_test_aux_write_override = 0;
	dm_mst_test_next_remote_sink = NULL;
	dm_mst_test_removed_sink = NULL;
	dm_mst_test_add_remote_sink_calls = 0;
	dm_mst_test_remove_remote_sink_calls = 0;
}

static void dm_mst_test_fini_child(struct dm_mst_test_child *child)
{
	drm_edid_free(child->aconnector->drm_edid);
	drm_edid_free(child->port->cached_edid);
	drm_connector_cleanup(&child->aconnector->base);
	drm_modeset_lock_fini(&child->root->mst_mgr.base.lock);
	mutex_destroy(&child->root->mst_mgr.lock);
}

/* Tests for dm_dp_mst_get_modes */

/**
 * dm_mst_test_get_modes_no_edid_adds_default_sink - Test default remote sink
 * @test: KUnit test context
 *
 * When the remote EDID cannot be read, dm_dp_mst_get_modes() must clear the
 * MST_REMOTE_EDID status and register a default remote sink for the connector.
 */
static void dm_mst_test_get_modes_no_edid_adds_default_sink(struct kunit *test)
{
	struct dm_mst_test_child child;
	struct dc_sink *sink;

	dm_mst_test_init_child(test, &child);
	sink = dm_mst_test_alloc_sink(test);
	dm_mst_test_next_remote_sink = sink;
	child.aconnector->mst_status = MST_REMOTE_EDID;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_get_modes(&child.aconnector->base), 0);

	KUNIT_EXPECT_EQ(test, dm_mst_test_add_remote_sink_calls, 1U);
	KUNIT_EXPECT_PTR_EQ(test, child.aconnector->dc_sink, sink);
	KUNIT_EXPECT_PTR_EQ(test, sink->priv, (void *)child.aconnector);
	KUNIT_EXPECT_EQ(test, child.aconnector->mst_status & MST_REMOTE_EDID, 0);

	dc_sink_release(sink);
	dm_mst_test_fini_child(&child);
}

/**
 * dm_mst_test_get_modes_no_edid_sink_alloc_fails - Test remote sink failure
 * @test: KUnit test context
 *
 * If DC cannot add the default remote sink, dm_dp_mst_get_modes() must bail out
 * with zero modes and leave the connector without a sink.
 */
static void dm_mst_test_get_modes_no_edid_sink_alloc_fails(struct kunit *test)
{
	struct dm_mst_test_child child;

	dm_mst_test_init_child(test, &child);

	KUNIT_EXPECT_EQ(test, dm_dp_mst_get_modes(&child.aconnector->base), 0);

	KUNIT_EXPECT_EQ(test, dm_mst_test_add_remote_sink_calls, 1U);
	KUNIT_EXPECT_NULL(test, child.aconnector->dc_sink);

	dm_mst_test_fini_child(&child);
}

/**
 * dm_mst_test_get_modes_no_edid_keeps_existing_sink - Test cached sink reuse
 * @test: KUnit test context
 *
 * With no readable EDID but an existing remote sink, dm_dp_mst_get_modes() must
 * keep that sink and must not ask DC for another one.
 */
static void dm_mst_test_get_modes_no_edid_keeps_existing_sink(struct kunit *test)
{
	struct dm_mst_test_child child;
	struct dc_sink *sink;

	dm_mst_test_init_child(test, &child);
	sink = dm_mst_test_alloc_sink(test);
	child.aconnector->dc_sink = sink;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_get_modes(&child.aconnector->base), 0);

	KUNIT_EXPECT_EQ(test, dm_mst_test_add_remote_sink_calls, 0U);
	KUNIT_EXPECT_PTR_EQ(test, child.aconnector->dc_sink, sink);

	dc_sink_release(sink);
	dm_mst_test_fini_child(&child);
}

/* Minimal EDID base block: valid header, no extensions, correct checksum. */
static const u8 dm_mst_test_edid[EDID_LENGTH] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
	[EDID_LENGTH - 1] = 0x06,
};

/**
 * dm_mst_test_get_modes_cached_edid_replaces_virtual_sink - Test EDID sink path
 * @test: KUnit test context
 *
 * With a cached EDID and a placeholder virtual sink, dm_dp_mst_get_modes() must
 * release the virtual sink, add a real remote sink built from the EDID, and
 * update the connector's mode list.
 */
static void dm_mst_test_get_modes_cached_edid_replaces_virtual_sink(struct kunit *test)
{
	struct dm_mst_test_child child;
	struct dc_sink *virtual_sink;
	struct dc_sink *sink;

	dm_mst_test_init_child(test, &child);

	child.aconnector->drm_edid = drm_edid_alloc(dm_mst_test_edid, sizeof(dm_mst_test_edid));
	KUNIT_ASSERT_NOT_NULL(test, child.aconnector->drm_edid);

	virtual_sink = dm_mst_test_alloc_sink(test);
	virtual_sink->sink_signal = SIGNAL_TYPE_VIRTUAL;
	child.aconnector->dc_sink = virtual_sink;

	sink = dm_mst_test_alloc_sink(test);
	dm_mst_test_next_remote_sink = sink;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_get_modes(&child.aconnector->base), 0);

	KUNIT_EXPECT_EQ(test, dm_mst_test_add_remote_sink_calls, 1U);
	KUNIT_EXPECT_PTR_EQ(test, child.aconnector->dc_sink, sink);
	KUNIT_EXPECT_EQ(test, sink->sink_signal, SIGNAL_TYPE_DISPLAY_PORT_MST);
	KUNIT_EXPECT_PTR_EQ(test, sink->priv, (void *)child.aconnector);

	dc_sink_release(sink);
	dm_mst_test_fini_child(&child);
}

/**
 * dm_mst_test_get_modes_cached_edid_sink_alloc_fails - Test EDID sink failure
 * @test: KUnit test context
 *
 * If DC cannot add the remote sink built from the cached EDID,
 * dm_dp_mst_get_modes() must bail out with zero modes.
 */
static void dm_mst_test_get_modes_cached_edid_sink_alloc_fails(struct kunit *test)
{
	struct dm_mst_test_child child;

	dm_mst_test_init_child(test, &child);

	child.aconnector->drm_edid = drm_edid_alloc(dm_mst_test_edid, sizeof(dm_mst_test_edid));
	KUNIT_ASSERT_NOT_NULL(test, child.aconnector->drm_edid);

	KUNIT_EXPECT_EQ(test, dm_dp_mst_get_modes(&child.aconnector->base), 0);

	KUNIT_EXPECT_EQ(test, dm_mst_test_add_remote_sink_calls, 1U);
	KUNIT_EXPECT_NULL(test, child.aconnector->dc_sink);

	dm_mst_test_fini_child(&child);
}

/**
 * dm_mst_test_get_modes_restores_hdcp_properties - Test HDCP property restore
 * @test: KUnit test context
 *
 * A connector re-plugged at the same display index must have its content
 * protection state restored from the HDCP workqueue when the remote sink is
 * re-created.
 */
static void dm_mst_test_get_modes_restores_hdcp_properties(struct kunit *test)
{
	struct dm_connector_state *conn_state;
	struct dm_mst_test_child child;
	struct hdcp_workqueue *hdcp;
	struct dc_sink *sink;
	unsigned int index;

	dm_mst_test_init_child(test, &child);

	child.aconnector->drm_edid = drm_edid_alloc(dm_mst_test_edid, sizeof(dm_mst_test_edid));
	KUNIT_ASSERT_NOT_NULL(test, child.aconnector->drm_edid);

	/* drm_connector_cleanup() hands the state back to the DRM helper's kfree(). */
	conn_state = kzalloc_obj(*conn_state);
	hdcp = kunit_kzalloc(test, sizeof(*hdcp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, conn_state);
	KUNIT_ASSERT_NOT_NULL(test, hdcp);

	index = child.aconnector->base.index;
	hdcp->hdcp_content_type[index] = DRM_MODE_HDCP_CONTENT_TYPE1;
	hdcp->content_protection[index] = DRM_MODE_CONTENT_PROTECTION_ENABLED;
	child.adev->dm.hdcp_workqueue = hdcp;
	child.aconnector->base.state = &conn_state->base;

	sink = dm_mst_test_alloc_sink(test);
	dm_mst_test_next_remote_sink = sink;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_get_modes(&child.aconnector->base), 0);

	KUNIT_EXPECT_PTR_EQ(test, child.aconnector->dc_sink, sink);
	KUNIT_EXPECT_EQ(test, child.aconnector->base.state->hdcp_content_type,
			(unsigned int)DRM_MODE_HDCP_CONTENT_TYPE1);
	KUNIT_EXPECT_EQ(test, child.aconnector->base.state->content_protection,
			(unsigned int)DRM_MODE_CONTENT_PROTECTION_ENABLED);

	dc_sink_release(sink);
	dm_mst_test_fini_child(&child);
}

/**
 * dm_mst_test_get_modes_reads_remote_edid - Test remote EDID caching
 * @test: KUnit test context
 *
 * When the MST port is still in the topology, dm_dp_mst_get_modes() must read
 * its EDID, cache it on the connector and flag MST_REMOTE_EDID.
 */
static void dm_mst_test_get_modes_reads_remote_edid(struct kunit *test)
{
	struct drm_dp_mst_branch *mstb;
	struct dm_mst_test_child child;
	struct dc_sink *sink;

	dm_mst_test_init_child(test, &child);

	/* Minimal topology so drm_dp_mst_edid_read() can validate the port. */
	mstb = kunit_kzalloc(test, sizeof(*mstb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mstb);
	mstb->mgr = &child.root->mst_mgr;
	INIT_LIST_HEAD(&mstb->ports);
	list_add(&child.port->next, &mstb->ports);
	kref_init(&child.port->topology_kref);
	child.root->mst_mgr.mst_primary = mstb;

	child.port->cached_edid = drm_edid_alloc(dm_mst_test_edid, sizeof(dm_mst_test_edid));
	KUNIT_ASSERT_NOT_NULL(test, child.port->cached_edid);

	sink = dm_mst_test_alloc_sink(test);
	dm_mst_test_next_remote_sink = sink;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_get_modes(&child.aconnector->base), 0);

	KUNIT_EXPECT_NOT_NULL(test, child.aconnector->drm_edid);
	KUNIT_EXPECT_EQ(test, child.aconnector->mst_status & MST_REMOTE_EDID, (int)MST_REMOTE_EDID);
	KUNIT_EXPECT_PTR_EQ(test, child.aconnector->dc_sink, sink);

	dc_sink_release(sink);
	dm_mst_test_fini_child(&child);
}

/* Tests for dm_dp_mst_detect */

/**
 * dm_mst_test_detect_reads_port_dpcd_rev - Test DPCD revision probing
 * @test: KUnit test context
 *
 * For a peer device with an unknown DPCD revision, dm_dp_mst_detect() must probe
 * DP_DP13_DPCD_REV over the port AUX and cache the returned revision.
 */
static void dm_mst_test_detect_reads_port_dpcd_rev(struct kunit *test)
{
	struct dm_mst_test_child child;

	dm_mst_test_init_child(test, &child);
	child.port->pdt = DP_PEER_DEVICE_SST_SINK;
	/* Both DP_DP13_DPCD_REV and DP_DPCD_REV alias to offset 0 in the fake. */
	dm_mst_test_dpcd[0] = 0x13;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_detect(&child.aconnector->base, NULL, false),
			(int)connector_status_disconnected);

	KUNIT_EXPECT_EQ(test, (int)child.port->dpcd_rev, 0x13);

	dm_mst_test_fini_child(&child);
}

/**
 * dm_mst_test_detect_unknown_dpcd_rev - Test unreadable DPCD revision
 * @test: KUnit test context
 *
 * When both DPCD revision registers read back as zero, dm_dp_mst_detect() must
 * leave the cached revision cleared instead of reporting a bogus value.
 */
static void dm_mst_test_detect_unknown_dpcd_rev(struct kunit *test)
{
	struct dm_mst_test_child child;

	dm_mst_test_init_child(test, &child);
	child.port->pdt = DP_PEER_DEVICE_SST_SINK;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_detect(&child.aconnector->base, NULL, false),
			(int)connector_status_disconnected);

	KUNIT_EXPECT_EQ(test, (int)child.port->dpcd_rev, 0);

	dm_mst_test_fini_child(&child);
}

/**
 * dm_mst_test_detect_dpcd_read_error - Test unreachable port DPCD
 * @test: KUnit test context
 *
 * When the remote DPCD read is NAKed, dm_dp_mst_detect() must leave the cached
 * revision untouched instead of storing a garbage value.
 */
static void dm_mst_test_detect_dpcd_read_error(struct kunit *test)
{
	struct dm_mst_test_child child;

	dm_mst_test_init_child(test, &child);
	child.port->pdt = DP_PEER_DEVICE_SST_SINK;
	dm_mst_test_aux_transfer_override = -EIO;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_detect(&child.aconnector->base, NULL, false),
			(int)connector_status_disconnected);

	KUNIT_EXPECT_EQ(test, (int)child.port->dpcd_rev, 0);

	dm_mst_test_fini_child(&child);
}

/**
 * dm_mst_test_detect_disconnect_releases_sink - Test unplug sink release
 * @test: KUnit test context
 *
 * A port with no peer device must have its cached DPCD revision cleared, and the
 * resulting disconnected status must release the remote sink and reset the MST
 * connector state.
 */
static void dm_mst_test_detect_disconnect_releases_sink(struct kunit *test)
{
	struct dm_mst_test_child child;
	struct dc_sink *sink;

	dm_mst_test_init_child(test, &child);
	child.port->pdt = DP_PEER_DEVICE_NONE;
	child.port->dpcd_rev = 0x14;

	sink = dm_mst_test_alloc_sink(test);
	child.aconnector->dc_sink = sink;
	child.aconnector->dsc_aux = &child.port->aux;
	child.aconnector->mst_local_bw = 1234;
	child.link->sink_count = 1;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_detect(&child.aconnector->base, NULL, false),
			(int)connector_status_disconnected);

	KUNIT_EXPECT_EQ(test, (int)child.port->dpcd_rev, 0);
	KUNIT_EXPECT_EQ(test, dm_mst_test_remove_remote_sink_calls, 1U);
	KUNIT_EXPECT_PTR_EQ(test, dm_mst_test_removed_sink, sink);
	KUNIT_EXPECT_NULL(test, child.aconnector->dc_sink);
	KUNIT_EXPECT_NULL(test, child.aconnector->dsc_aux);
	KUNIT_EXPECT_EQ(test, child.aconnector->mst_local_bw, 0U);

	dm_mst_test_fini_child(&child);
}

/* Tests for amdgpu_dm_mst_connector_late_register */

/**
 * dm_mst_test_connector_late_register - Test MST connector late registration
 * @test: KUnit test context
 *
 * amdgpu_dm_mst_connector_late_register() must register the port's remote AUX
 * bus and report success.
 */
static void dm_mst_test_connector_late_register(struct kunit *test)
{
	struct dm_mst_test_child child;

	dm_mst_test_init_child(test, &child);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_mst_connector_late_register(&child.aconnector->base), 0);

	dm_mst_test_fini_child(&child);
}

/* Tests for amdgpu_dm_mst_connector_early_unregister */

/**
 * dm_mst_test_connector_early_unregister_no_sink - Test unregister without sink
 * @test: KUnit test context
 *
 * With no remote sink attached, amdgpu_dm_mst_connector_early_unregister() must
 * only reset the MST status.
 */
static void dm_mst_test_connector_early_unregister_no_sink(struct kunit *test)
{
	struct dm_mst_test_child child;

	dm_mst_test_init_child(test, &child);
	child.aconnector->mst_status = MST_REMOTE_EDID;

	amdgpu_dm_mst_connector_early_unregister(&child.aconnector->base);

	KUNIT_EXPECT_EQ(test, dm_mst_test_remove_remote_sink_calls, 0U);
	KUNIT_EXPECT_EQ(test, (int)child.aconnector->mst_status, (int)MST_STATUS_DEFAULT);

	dm_mst_test_fini_child(&child);
}

/**
 * dm_mst_test_connector_early_unregister_releases_sink - Test sink release
 * @test: KUnit test context
 *
 * When the port leaves the topology, amdgpu_dm_mst_connector_early_unregister()
 * must remove the remote sink from the link and reset the MST connector state.
 */
static void dm_mst_test_connector_early_unregister_releases_sink(struct kunit *test)
{
	struct dm_mst_test_child child;
	struct dc_sink *sink;

	dm_mst_test_init_child(test, &child);

	sink = dm_mst_test_alloc_sink(test);
	child.aconnector->dc_sink = sink;
	child.aconnector->vc_full_pbn = 42;
	child.link->sink_count = 1;

	amdgpu_dm_mst_connector_early_unregister(&child.aconnector->base);

	KUNIT_EXPECT_EQ(test, dm_mst_test_remove_remote_sink_calls, 1U);
	KUNIT_EXPECT_PTR_EQ(test, dm_mst_test_removed_sink, sink);
	KUNIT_EXPECT_NULL(test, child.aconnector->dc_sink);
	KUNIT_EXPECT_EQ(test, child.aconnector->vc_full_pbn, 0U);
	KUNIT_EXPECT_EQ(test, (int)child.aconnector->mst_status, (int)MST_STATUS_DEFAULT);

	dm_mst_test_fini_child(&child);
}

/* Tests for dm_dp_mst_connector_destroy */

/*
 * dm_dp_mst_connector_destroy() frees both the connector and the MST port, so
 * they are allocated outside the KUnit managed allocator. The parent branch
 * device takes an extra malloc reference so it survives the port teardown.
 */
static struct amdgpu_dm_connector *
dm_mst_test_alloc_destroyable_connector(struct kunit *test, struct dm_mst_test_child *child)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_branch *mstb;
	struct drm_dp_mst_port *port;
	int ret;

	aconnector = kzalloc_obj(*aconnector);
	port = kzalloc_obj(*port);
	mstb = kunit_kzalloc(test, sizeof(*mstb), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	KUNIT_ASSERT_NOT_NULL(test, port);
	KUNIT_ASSERT_NOT_NULL(test, mstb);

	mstb->mgr = &child->root->mst_mgr;
	kref_init(&mstb->malloc_kref);
	kref_get(&mstb->malloc_kref);

	port->mgr = &child->root->mst_mgr;
	port->parent = mstb;
	kref_init(&port->malloc_kref);

	aconnector->dc_link = child->link;
	aconnector->mst_root = child->root;
	aconnector->mst_output_port = port;

	ret = drm_connector_init(&child->adev->ddev, &aconnector->base,
				 &dm_mst_test_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	KUNIT_ASSERT_EQ(test, ret, 0);

	return aconnector;
}

/**
 * dm_mst_test_connector_destroy_no_sink - Test destroy without a remote sink
 * @test: KUnit test context
 *
 * dm_dp_mst_connector_destroy() must clean up the DRM connector and drop the MST
 * port reference even when no remote sink was ever attached.
 */
static void dm_mst_test_connector_destroy_no_sink(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct dm_mst_test_child child;

	dm_mst_test_init_child(test, &child);
	aconnector = dm_mst_test_alloc_destroyable_connector(test, &child);

	dm_dp_mst_connector_destroy(&aconnector->base);

	KUNIT_EXPECT_EQ(test, dm_mst_test_remove_remote_sink_calls, 0U);

	dm_mst_test_fini_child(&child);
}

/**
 * dm_mst_test_connector_destroy_releases_sink - Test destroy releases the sink
 * @test: KUnit test context
 *
 * dm_dp_mst_connector_destroy() must remove the remote sink from the DC link
 * before tearing down the connector.
 */
static void dm_mst_test_connector_destroy_releases_sink(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct dm_mst_test_child child;
	struct dc_sink *sink;

	dm_mst_test_init_child(test, &child);
	aconnector = dm_mst_test_alloc_destroyable_connector(test, &child);

	sink = dm_mst_test_alloc_sink(test);
	aconnector->dc_sink = sink;
	child.link->sink_count = 1;

	dm_dp_mst_connector_destroy(&aconnector->base);

	KUNIT_EXPECT_EQ(test, dm_mst_test_remove_remote_sink_calls, 1U);
	KUNIT_EXPECT_PTR_EQ(test, dm_mst_test_removed_sink, sink);

	dm_mst_test_fini_child(&child);
}

/*
 * Sideband connector with a live topology manager and the DOWN_REP ready bit
 * armed, so dm_handle_mst_sideband_msg_ready_event() reaches its ack path.
 */
static struct amdgpu_dm_connector *
dm_mst_test_alloc_armed_sideband_connector(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_device *adev;
	int ret;

	adev = dm_kunit_alloc_adev(test);
	ret = drmm_mode_config_init(&adev->ddev);
	KUNIT_ASSERT_EQ(test, ret, 0);

	aconnector = dm_mst_test_alloc_sideband_connector(test);

	ret = drm_dp_mst_topology_mgr_init(&aconnector->mst_mgr, &adev->ddev,
					   &aconnector->dm_dp_aux.aux, 16, 4, 0);
	KUNIT_ASSERT_EQ(test, ret, 0);
	aconnector->mst_mgr.mst_state = true;
	dm_mst_test_dpcd[(DP_SINK_COUNT_ESI + 1) & 0xf] = DP_DOWN_REP_MSG_RDY;

	return aconnector;
}

static void dm_mst_test_free_armed_sideband_connector(struct amdgpu_dm_connector *aconnector)
{
	aconnector->mst_mgr.mst_state = false;
	drm_dp_mst_topology_mgr_destroy(&aconnector->mst_mgr);
}

/**
 * dm_mst_test_sideband_msg_ready_acks_down_rep - Test DOWN_REP ack handling
 * @test: KUnit test context
 *
 * With an active topology manager and the DOWN_REP ready bit set, the sideband
 * handler must acknowledge the event at DPCD and keep polling until the
 * iteration limit is reached, since the fake sideband message never completes.
 */
static void dm_mst_test_sideband_msg_ready_acks_down_rep(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = dm_mst_test_alloc_armed_sideband_connector(test);

	dm_handle_mst_sideband_msg_ready_event(&aconnector->mst_mgr, DOWN_REP_MSG_RDY_EVENT);

	KUNIT_EXPECT_EQ(test, aconnector->mst_mgr.sink_count, 0);

	dm_mst_test_free_armed_sideband_connector(aconnector);
}

/**
 * dm_mst_test_sideband_msg_ready_ack_write_fails - Test failed DPCD ack
 * @test: KUnit test context
 *
 * When the DPCD acknowledge write keeps failing, the sideband handler must give
 * up after the third retry instead of looping forever.
 */
static void dm_mst_test_sideband_msg_ready_ack_write_fails(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;

	aconnector = dm_mst_test_alloc_armed_sideband_connector(test);
	dm_mst_test_aux_write_override = -EIO;

	dm_handle_mst_sideband_msg_ready_event(&aconnector->mst_mgr, DOWN_REP_MSG_RDY_EVENT);

	KUNIT_EXPECT_EQ(test, aconnector->mst_mgr.sink_count, 0);

	dm_mst_test_free_armed_sideband_connector(aconnector);
}

/*
 * Fake DPCD backing store for the DSC helpers. Unlike the sideband fake above
 * it is addressed absolutely, because these helpers read offsets spread across
 * the whole map (0x60 DSC caps, 0x100 link settings, 0x500 branch descriptor).
 */
#define DM_MST_TEST_DSC_DPCD_SIZE 0x600

static u8 dm_mst_test_dsc_dpcd[DM_MST_TEST_DSC_DPCD_SIZE];
static struct drm_dp_aux *dm_mst_test_dsc_aux_fail;

static ssize_t dm_mst_test_dsc_aux_transfer(struct drm_dp_aux *aux,
					    struct drm_dp_aux_msg *msg)
{
	size_t i;

	if (aux == dm_mst_test_dsc_aux_fail)
		return -EIO;

	if (msg->address + msg->size > DM_MST_TEST_DSC_DPCD_SIZE)
		return -EINVAL;

	msg->reply = DP_AUX_NATIVE_REPLY_ACK;

	if ((msg->request & ~DP_AUX_I2C_MOT) == DP_AUX_NATIVE_WRITE)
		return msg->size;

	for (i = 0; i < msg->size; i++)
		((u8 *)msg->buffer)[i] = dm_mst_test_dsc_dpcd[msg->address + i];

	return msg->size;
}

/* Clears the shared store; call once per test before initialising any AUX. */
static void dm_mst_test_reset_dsc_dpcd(void)
{
	memset(dm_mst_test_dsc_dpcd, 0, sizeof(dm_mst_test_dsc_dpcd));
	dm_mst_test_dsc_aux_fail = NULL;
}

static void dm_mst_test_init_dsc_aux(struct drm_dp_aux *aux, const char *name)
{
	aux->name = name;
	aux->transfer = dm_mst_test_dsc_aux_transfer;
	drm_dp_aux_init(aux);
	drm_dp_dpcd_set_probe(aux, false);
}

static struct drm_dp_aux *dm_mst_test_alloc_dsc_aux(struct kunit *test, const char *name)
{
	struct drm_dp_aux *aux = kunit_kzalloc(test, sizeof(*aux), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, aux);
	dm_mst_test_init_dsc_aux(aux, name);

	return aux;
}

/* Tests for dp_get_link_current_set_bw */

/*
 * Program DPCD 0x100..0x10f, the 16-byte window the helper reads in one go.
 * @coding is the raw DP_MAIN_LINK_CHANNEL_CODING_SET byte at 0x108.
 */
static void dm_mst_test_set_link_settings(u8 link_bw_set, u8 lane_count, u8 coding)
{
	dm_mst_test_dsc_dpcd[DP_LINK_BW_SET] = link_bw_set;
	dm_mst_test_dsc_dpcd[DP_LANE_COUNT_SET] = lane_count;
	dm_mst_test_dsc_dpcd[DP_MAIN_LINK_CHANNEL_CODING_SET] = coding;
}

struct dm_mst_link_bw_param {
	const char *name;
	u8 link_bw_set;
	u8 lane_count;
	u8 coding;
	bool aux_fails;
	bool supported;
	uint32_t cur_link_bw;
};

static const struct dm_mst_link_bw_param dm_mst_link_bw_params[] = {
	{ "hbr2_8b_10b", DP_LINK_BW_5_4, 4, DP_8b_10b_ENCODING, false, true, 16761600 },
	{ "uhbr10", DP_LINK_BW_10, 4, DP_128b_132b_ENCODING, false, true, 38564000 },
	{ "uhbr13_5", DP_LINK_BW_13_5, 4, DP_128b_132b_ENCODING, false, true, 52061400 },
	{ "uhbr20", DP_LINK_BW_20, 4, DP_128b_132b_ENCODING, false, true, 77128000 },
	{ "unlisted_uhbr_rate", 0x1e, 4, DP_128b_132b_ENCODING, false, false, 0 },
	{ "unknown_encoding", DP_LINK_BW_5_4, 4, DP_UNKNOWN_ENCODING, false, false, 0 },
	{ "dpcd_read_error", DP_LINK_BW_5_4, 4, DP_8b_10b_ENCODING, true, false, 0 },
};

KUNIT_ARRAY_PARAM_DESC(dm_mst_link_bw, dm_mst_link_bw_params, name);

/**
 * dm_mst_test_link_current_set_bw - the current link settings are priced
 * @test: KUnit test context
 *
 * For 8b/10b the raw DP_LINK_BW_SET byte is the link rate in 27MHz units, so
 * HBR2 (0x14) yields 20 * 27000 * 10 kbps per lane scaled by the 80% data
 * efficiency and the 97% FEC efficiency. For 128b/132b the byte instead
 * selects a UHBR rate scaled by its own efficiency. An unlisted UHBR rate, an
 * unknown channel coding and a short DPCD read all report no bandwidth.
 */
static void dm_mst_test_link_current_set_bw(struct kunit *test)
{
	const struct dm_mst_link_bw_param *param = test->param_value;
	uint32_t cur_link_bw = 0xdeadbeef;
	struct drm_dp_aux *aux;

	dm_mst_test_reset_dsc_dpcd();
	aux = dm_mst_test_alloc_dsc_aux(test, "dm_mst_test_link_bw_aux");
	dm_mst_test_set_link_settings(param->link_bw_set, param->lane_count, param->coding);
	if (param->aux_fails)
		dm_mst_test_dsc_aux_fail = aux;

	KUNIT_EXPECT_EQ(test, dp_get_link_current_set_bw(aux, &cur_link_bw), param->supported);
	KUNIT_EXPECT_EQ(test, cur_link_bw, param->cur_link_bw);
}

/* Tests for is_synaptics_cascaded_panamera */

/*
 * A Panamera hub is identified by its branch device ID plus the high nibble of
 * the fifth branch device name byte; the cascaded variant additionally reports
 * SYNAPTICS_CASCADED_HUB_ID at DPCD 0x50e.
 */
static void dm_mst_test_set_panamera_ids(struct dc_link *link, u32 branch_dev_id,
					 u8 dev_name_4, u8 cascaded_id)
{
	link->dpcd_caps.branch_dev_id = branch_dev_id;
	link->dpcd_caps.branch_dev_name[4] = dev_name_4;
	dm_mst_test_dsc_dpcd[DP_BRANCH_VENDOR_SPECIFIC_START + 2] = cascaded_id;
}

static struct drm_dp_mst_port *dm_mst_test_alloc_mgr_port(struct kunit *test)
{
	struct drm_dp_mst_topology_mgr *mgr;
	struct drm_dp_mst_port *port;

	mgr = kunit_kzalloc(test, sizeof(*mgr), GFP_KERNEL);
	port = kunit_kzalloc(test, sizeof(*port), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, mgr);
	KUNIT_ASSERT_NOT_NULL(test, port);

	mutex_init(&mgr->lock);
	mgr->aux = dm_mst_test_alloc_dsc_aux(test, "dm_mst_test_mgr_aux");
	port->mgr = mgr;

	return port;
}

struct dm_mst_panamera_param {
	const char *name;
	u32 branch_dev_id;
	u8 dev_name_4;
	u8 cascaded_id;
	bool aux_fails;
	bool cascaded;
};

static const struct dm_mst_panamera_param dm_mst_panamera_params[] = {
	{ "cascaded_hub", DP_BRANCH_DEVICE_ID_90CC24, 0x50,
	  SYNAPTICS_CASCADED_HUB_ID, false, true },
	{ "wrong_dev_id", 0x001122, 0x50, SYNAPTICS_CASCADED_HUB_ID, false, false },
	{ "not_panamera", DP_BRANCH_DEVICE_ID_90CC24, 0x40,
	  SYNAPTICS_CASCADED_HUB_ID, false, false },
	{ "not_cascaded", DP_BRANCH_DEVICE_ID_90CC24, 0x50, 0, false, false },
	{ "dpcd_read_error", DP_BRANCH_DEVICE_ID_90CC24, 0x50,
	  SYNAPTICS_CASCADED_HUB_ID, true, false },
};

KUNIT_ARRAY_PARAM_DESC(dm_mst_panamera, dm_mst_panamera_params, name);

/**
 * dm_mst_test_synaptics_cascaded - only a cascaded Panamera hub is detected
 * @test: KUnit test context
 *
 * All three conditions must hold: the Synaptics branch device ID, a Panamera
 * device name and the cascaded hub ID in the branch vendor data. A failed
 * vendor read leaves the hub undetected.
 */
static void dm_mst_test_synaptics_cascaded(struct kunit *test)
{
	const struct dm_mst_panamera_param *param = test->param_value;
	struct drm_dp_mst_port *port;
	struct dc_link *link;

	dm_mst_test_reset_dsc_dpcd();
	port = dm_mst_test_alloc_mgr_port(test);
	link = dm_kunit_alloc_link(test);
	dm_mst_test_set_panamera_ids(link, param->branch_dev_id, param->dev_name_4,
				     param->cascaded_id);
	if (param->aux_fails)
		dm_mst_test_dsc_aux_fail = port->mgr->aux;

	KUNIT_EXPECT_EQ(test, is_synaptics_cascaded_panamera(link, port), param->cascaded);
}

/* Tests for validate_dsc_caps_on_connector */

struct dm_mst_test_dsc_ctx {
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_dm_connector *root;
	struct drm_dp_mst_port *port;
	struct dc_sink *sink;
	struct dc_link *link;
};

/*
 * Build an MST endpoint whose port hangs directly off the primary branch. With
 * an all-zero DPCD, drm_dp_mst_dsc_aux_for_port() finds no DSC-capable AUX, so
 * each test can steer the driver's own fallbacks in isolation.
 */
static void dm_mst_test_init_dsc_ctx(struct kunit *test, struct dm_mst_test_dsc_ctx *ctx)
{
	struct drm_dp_mst_branch *branch;
	struct dc *dc;

	dm_mst_test_reset_dsc_dpcd();

	ctx->aconnector = kunit_kzalloc(test, sizeof(*ctx->aconnector), GFP_KERNEL);
	ctx->root = kunit_kzalloc(test, sizeof(*ctx->root), GFP_KERNEL);
	ctx->sink = kunit_kzalloc(test, sizeof(*ctx->sink), GFP_KERNEL);
	branch = kunit_kzalloc(test, sizeof(*branch), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	KUNIT_ASSERT_NOT_NULL(test, ctx->root);
	KUNIT_ASSERT_NOT_NULL(test, ctx->sink);
	KUNIT_ASSERT_NOT_NULL(test, branch);

	dc = dm_kunit_alloc_dc_with_ctx(test);
	ctx->link = dm_kunit_alloc_link(test);
	ctx->link->ctx = dc->ctx;

	ctx->port = dm_mst_test_alloc_mgr_port(test);
	ctx->port->parent = branch;
	dm_mst_test_init_dsc_aux(&ctx->port->aux, "dm_mst_test_port_dsc_aux");
	dm_mst_test_init_dsc_aux(&ctx->root->dm_dp_aux.aux, "dm_mst_test_root_dsc_aux");

	ctx->aconnector->dc_link = ctx->link;
	ctx->aconnector->dc_sink = ctx->sink;
	ctx->aconnector->mst_output_port = ctx->port;
	ctx->aconnector->mst_root = ctx->root;
}

/* Make needs_dsc_aux_workaround() accept the link. */
static void dm_mst_test_arm_dsc_aux_workaround(struct dc_link *link)
{
	link->dpcd_caps.branch_dev_id = DP_BRANCH_DEVICE_ID_90CC24;
	link->dpcd_caps.dpcd_rev.raw = DPCD_REV_14;
	link->dpcd_caps.sink_count.bits.SINK_COUNT = 2;
}

/**
 * dm_mst_test_validate_dsc_caps_no_aux - no DSC AUX means no DSC capabilities
 * @test: KUnit test context
 *
 * When neither the DRM helper, the Synaptics workaround nor the cascaded hub
 * quirk yields a DSC AUX channel, validation must fail without touching DPCD.
 */
static void dm_mst_test_validate_dsc_caps_no_aux(struct kunit *test)
{
	struct dm_mst_test_dsc_ctx ctx;

	dm_mst_test_init_dsc_ctx(test, &ctx);

	KUNIT_EXPECT_FALSE(test, validate_dsc_caps_on_connector(ctx.aconnector));
	KUNIT_EXPECT_NULL(test, ctx.aconnector->dsc_aux);
}

/**
 * dm_mst_test_validate_dsc_caps_aux_workaround - MST dock AUX fallback is used
 * @test: KUnit test context
 *
 * A DSC capable dock in front of a non-DSC display leaves the DRM helper
 * without an AUX, so the driver must fall back to the MST root's AUX and parse
 * the DSC capabilities read from it.
 */
static void dm_mst_test_validate_dsc_caps_aux_workaround(struct kunit *test)
{
	struct dm_mst_test_dsc_ctx ctx;

	dm_mst_test_init_dsc_ctx(test, &ctx);
	dm_mst_test_arm_dsc_aux_workaround(ctx.link);
	dm_mst_test_dsc_dpcd[DP_DSC_SUPPORT] = DP_DSC_DECOMPRESSION_IS_SUPPORTED;

	KUNIT_EXPECT_TRUE(test, validate_dsc_caps_on_connector(ctx.aconnector));
	KUNIT_EXPECT_PTR_EQ(test, ctx.aconnector->dsc_aux, &ctx.root->dm_dp_aux.aux);
	KUNIT_EXPECT_TRUE(test, ctx.sink->dsc_caps.dsc_dec_caps.is_dsc_supported);
}

/**
 * dm_mst_test_validate_dsc_caps_cascaded_hub - cascaded hub uses the mgr AUX
 * @test: KUnit test context
 *
 * On a cascaded Synaptics Panamera hub the DSC decoder lives on the primary
 * branch, so the topology manager's AUX must win over every other fallback.
 */
static void dm_mst_test_validate_dsc_caps_cascaded_hub(struct kunit *test)
{
	struct dm_mst_test_dsc_ctx ctx;

	dm_mst_test_init_dsc_ctx(test, &ctx);
	dm_mst_test_arm_dsc_aux_workaround(ctx.link);
	dm_mst_test_set_panamera_ids(ctx.link, DP_BRANCH_DEVICE_ID_90CC24, 0x50,
				     SYNAPTICS_CASCADED_HUB_ID);
	dm_mst_test_dsc_dpcd[DP_DSC_SUPPORT] = DP_DSC_DECOMPRESSION_IS_SUPPORTED;

	KUNIT_EXPECT_TRUE(test, validate_dsc_caps_on_connector(ctx.aconnector));
	KUNIT_EXPECT_PTR_EQ(test, ctx.aconnector->dsc_aux, ctx.port->mgr->aux);
}

/**
 * dm_mst_test_validate_dsc_caps_read_error - a failed DSC caps read is fatal
 * @test: KUnit test context
 */
static void dm_mst_test_validate_dsc_caps_read_error(struct kunit *test)
{
	struct dm_mst_test_dsc_ctx ctx;

	dm_mst_test_init_dsc_ctx(test, &ctx);
	dm_mst_test_arm_dsc_aux_workaround(ctx.link);
	dm_mst_test_dsc_aux_fail = &ctx.root->dm_dp_aux.aux;

	KUNIT_EXPECT_FALSE(test, validate_dsc_caps_on_connector(ctx.aconnector));
	KUNIT_EXPECT_PTR_EQ(test, ctx.aconnector->dsc_aux, &ctx.root->dm_dp_aux.aux);
}

/**
 * dm_mst_test_validate_dsc_caps_unsupported - a sink without DSC is rejected
 * @test: KUnit test context
 *
 * The AUX resolves but the sink reports no decompression support, so parsing
 * the raw capabilities fails and the connector must not be marked DSC capable.
 */
static void dm_mst_test_validate_dsc_caps_unsupported(struct kunit *test)
{
	struct dm_mst_test_dsc_ctx ctx;

	dm_mst_test_init_dsc_ctx(test, &ctx);
	dm_mst_test_arm_dsc_aux_workaround(ctx.link);

	KUNIT_EXPECT_FALSE(test, validate_dsc_caps_on_connector(ctx.aconnector));
	KUNIT_EXPECT_FALSE(test, ctx.sink->dsc_caps.dsc_dec_caps.is_dsc_supported);
}

/* Tests for dm_dp_mst_is_port_support_mode */

static uint32_t dm_mst_test_root_link_bw_kbps;

static uint32_t dm_mst_test_root_bandwidth_kbps(const struct dc_link *link,
						const struct dc_link_settings *link_settings)
{
	return dm_mst_test_root_link_bw_kbps;
}

struct dm_mst_test_port_mode_ctx {
	struct amdgpu_dm_connector *aconnector;
	struct dc_stream_state *stream;
	struct drm_dp_mst_port *port;
	struct dc_sink *sink;
	struct dc_link *link;
	struct dc *dc;
};

/*
 * Build the minimum needed to price a mode: a link whose root bandwidth is
 * faked, a virtual channel sized by the port's full_pbn, and a 24bpp timing
 * that needs pix_clk_100hz / 10 * 24 kbps uncompressed.
 */
static void dm_mst_test_init_port_mode_ctx(struct kunit *test,
					   struct dm_mst_test_port_mode_ctx *ctx,
					   uint32_t root_link_bw_kbps, u32 full_pbn,
					   uint32_t pix_clk_100hz)
{
	struct link_service *link_srv;
	struct resource_pool *res_pool;

	dm_mst_test_reset_dsc_dpcd();

	ctx->aconnector = kunit_kzalloc(test, sizeof(*ctx->aconnector), GFP_KERNEL);
	ctx->sink = kunit_kzalloc(test, sizeof(*ctx->sink), GFP_KERNEL);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	res_pool = kunit_kzalloc(test, sizeof(*res_pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	KUNIT_ASSERT_NOT_NULL(test, ctx->sink);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, res_pool);

	dm_mst_test_root_link_bw_kbps = root_link_bw_kbps;
	link_srv->dp_link_bandwidth_kbps = dm_mst_test_root_bandwidth_kbps;

	ctx->dc = dm_kunit_alloc_dc_with_ctx(test);
	ctx->dc->link_srv = link_srv;
	ctx->dc->res_pool = res_pool;

	ctx->link = dm_kunit_alloc_link(test);
	ctx->link->dc = ctx->dc;
	ctx->link->ctx = ctx->dc->ctx;

	ctx->sink->ctx = ctx->dc->ctx;
	ctx->port = dm_mst_test_alloc_mgr_port(test);
	ctx->port->full_pbn = full_pbn;

	ctx->stream = dm_kunit_alloc_stream(test, ctx->link);
	ctx->stream->sink = ctx->sink;
	ctx->stream->timing.display_color_depth = COLOR_DEPTH_888;
	ctx->stream->timing.pix_clk_100hz = pix_clk_100hz;

	ctx->aconnector->dc_link = ctx->link;
	ctx->aconnector->dc_sink = ctx->sink;
	ctx->aconnector->mst_output_port = ctx->port;
}

/**
 * dm_mst_test_port_mode_fits_without_dsc - sufficient bandwidth needs no DSC
 * @test: KUnit test context
 *
 * When the uncompressed stream fits into the smaller of the root link and the
 * virtual channel bandwidth, the mode is accepted before any DSC evaluation.
 */
static void dm_mst_test_port_mode_fits_without_dsc(struct kunit *test)
{
	struct dm_mst_test_port_mode_ctx ctx;

	dm_mst_test_init_port_mode_ctx(test, &ctx, 1000000, 1000, 100000);

	KUNIT_EXPECT_EQ(test, dm_dp_mst_is_port_support_mode(ctx.aconnector, ctx.stream), DC_OK);
	KUNIT_EXPECT_EQ(test, (u32)ctx.stream->timing.flags.DSC, 0U);
}

/**
 * dm_mst_test_port_mode_no_dsc_aux - DSC is required but unavailable
 * @test: KUnit test context
 *
 * A mode that exceeds the end-to-end bandwidth on a connector without a DSC
 * AUX channel cannot be compressed, so validation must fail.
 */
static void dm_mst_test_port_mode_no_dsc_aux(struct kunit *test)
{
	struct dm_mst_test_port_mode_ctx ctx;

	dm_mst_test_init_port_mode_ctx(test, &ctx, 100000, 10, 100000);

	KUNIT_EXPECT_EQ(test, dm_dp_mst_is_port_support_mode(ctx.aconnector, ctx.stream),
			DC_FAIL_BANDWIDTH_VALIDATE);
}

/**
 * dm_mst_test_port_mode_no_common_dsc_config - no shared DSC config is fatal
 * @test: KUnit test context
 *
 * With a DSC AUX but no DSC encoder in the resource pool, source and sink share
 * no usable DSC configuration and the mode must be rejected.
 */
static void dm_mst_test_port_mode_no_common_dsc_config(struct kunit *test)
{
	struct dm_mst_test_port_mode_ctx ctx;

	dm_mst_test_init_port_mode_ctx(test, &ctx, 100000, 10, 100000);
	ctx.aconnector->dsc_aux = ctx.port->mgr->aux;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_is_port_support_mode(ctx.aconnector, ctx.stream),
			DC_FAIL_BANDWIDTH_VALIDATE);
}

/*
 * 1920x1080@60 RGB 8bpc needs 3564000 kbps uncompressed, and roughly 1188000
 * kbps at the 8bpp DSC policy minimum.
 */
#define DM_MST_TEST_DSC_PIX_CLK_100HZ	1485000

/* Enough of dsc2_get_enc_caps() for the DSC policy and bandwidth maths. */
static void dm_mst_test_dsc_get_enc_caps(struct dsc_enc_caps *dsc_enc_caps, int pixel_clock_100Hz)
{
	dsc_enc_caps->dsc_version = 0x21;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_1 = 1;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_2 = 1;
	dsc_enc_caps->slice_caps.bits.NUM_SLICES_4 = 1;
	dsc_enc_caps->lb_bit_depth = 13;
	dsc_enc_caps->is_block_pred_supported = true;
	dsc_enc_caps->color_formats.bits.RGB = 1;
	dsc_enc_caps->color_depth.bits.COLOR_DEPTH_8_BPC = 1;
	dsc_enc_caps->max_total_throughput_mps = 4800;
	dsc_enc_caps->max_slice_width = 5184;
	dsc_enc_caps->bpp_increment_div = 16;
}

static const struct dsc_funcs dm_mst_test_dsc_funcs = {
	.dsc_get_enc_caps = dm_mst_test_dsc_get_enc_caps,
};

/* Install the DSC encoder fake and matching sink decoder capabilities. */
static void dm_mst_test_setup_dsc_caps(struct kunit *test, struct dc *dc, struct dc_sink *sink)
{
	struct dsc_dec_dpcd_caps *dec = &sink->dsc_caps.dsc_dec_caps;
	struct display_stream_compressor *dsc;

	dsc = kunit_kzalloc(test, sizeof(*dsc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dsc);

	dsc->funcs = &dm_mst_test_dsc_funcs;
	dsc->ctx = dc->ctx;
	dc->res_pool->dscs[0] = dsc;

	dec->is_dsc_supported = true;
	dec->dsc_version = 0x21;
	dec->rc_buffer_size = 16 * 1024;
	dec->slice_caps1.bits.NUM_SLICES_1 = 1;
	dec->slice_caps1.bits.NUM_SLICES_2 = 1;
	dec->slice_caps1.bits.NUM_SLICES_4 = 1;
	dec->lb_bit_depth = 13;
	dec->is_block_pred_supported = true;
	dec->color_formats.bits.RGB = 1;
	dec->color_depth.bits.COLOR_DEPTH_8_BPC = 1;
	dec->throughput_mode_0_mps = 1000;
	dec->throughput_mode_1_mps = 1000;
	dec->max_slice_width = 5120;
	dec->bpp_increment_div = 16;
}

static void dm_mst_test_set_dsc_timing(struct dc_crtc_timing *timing)
{
	timing->display_color_depth = COLOR_DEPTH_888;
	timing->pixel_encoding = PIXEL_ENCODING_RGB;
	timing->h_addressable = 1920;
	timing->v_addressable = 1080;
	timing->pix_clk_100hz = DM_MST_TEST_DSC_PIX_CLK_100HZ;
}

/*
 * Give the source a DSC encoder and the sink matching decoder capabilities so
 * is_dsc_common_config_possible() succeeds, and switch to a timing that does
 * not fit uncompressed.
 */
static void dm_mst_test_enable_dsc(struct kunit *test, struct dm_mst_test_port_mode_ctx *ctx)
{
	struct drm_dp_mst_branch *branch;

	branch = kunit_kzalloc(test, sizeof(*branch), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, branch);

	dm_mst_test_setup_dsc_caps(test, ctx->dc, ctx->sink);
	dm_mst_test_set_dsc_timing(&ctx->stream->timing);

	ctx->port->parent = branch;
	dm_mst_test_init_dsc_aux(&ctx->port->aux, "dm_mst_test_port_mode_aux");
	ctx->aconnector->dsc_aux = ctx->port->mgr->aux;
}

/**
 * dm_mst_test_port_mode_dsc_passthrough_fits - DSC passthrough accepts the mode
 * @test: KUnit test context
 *
 * With DSC passthrough the compressed stream travels the whole path, so a
 * minimum compression that fits the end-to-end bandwidth enables DSC.
 */
static void dm_mst_test_port_mode_dsc_passthrough_fits(struct kunit *test)
{
	struct dm_mst_test_port_mode_ctx ctx;

	dm_mst_test_init_port_mode_ctx(test, &ctx, 2500000, 1000, 100000);
	dm_mst_test_enable_dsc(test, &ctx);
	ctx.port->passthrough_aux = ctx.port->mgr->aux;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_is_port_support_mode(ctx.aconnector, ctx.stream), DC_OK);
	KUNIT_EXPECT_EQ(test, (u32)ctx.stream->timing.flags.DSC, 1U);
}

/**
 * dm_mst_test_port_mode_dsc_passthrough_too_narrow - max compression still too big
 * @test: KUnit test context
 *
 * When even the smallest DSC bitstream exceeds the end-to-end bandwidth the
 * mode must be rejected instead of enabling DSC.
 */
static void dm_mst_test_port_mode_dsc_passthrough_too_narrow(struct kunit *test)
{
	struct dm_mst_test_port_mode_ctx ctx;

	dm_mst_test_init_port_mode_ctx(test, &ctx, 1000000, 1000, 100000);
	dm_mst_test_enable_dsc(test, &ctx);
	ctx.port->passthrough_aux = ctx.port->mgr->aux;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_is_port_support_mode(ctx.aconnector, ctx.stream),
			DC_FAIL_BANDWIDTH_VALIDATE);
	KUNIT_EXPECT_EQ(test, (u32)ctx.stream->timing.flags.DSC, 0U);
}

/**
 * dm_mst_test_port_mode_last_link_too_slow - the uncompressed last link is checked
 * @test: KUnit test context
 *
 * DSC is decoded at the endpoint, so the last DP link still carries the
 * uncompressed stream. Its current link settings are read from DPCD and cached
 * on the connector, and a mode that does not fit is rejected.
 */
static void dm_mst_test_port_mode_last_link_too_slow(struct kunit *test)
{
	struct dm_mst_test_port_mode_ctx ctx;

	dm_mst_test_init_port_mode_ctx(test, &ctx, 2500000, 1000, 100000);
	dm_mst_test_enable_dsc(test, &ctx);
	ctx.port->pdt = DP_PEER_DEVICE_SST_SINK;
	/* RBR x1: 1257120 kbps, far below the 3564000 kbps the mode needs. */
	dm_mst_test_set_link_settings(DP_LINK_BW_1_62, 1, DP_8b_10b_ENCODING);

	KUNIT_EXPECT_EQ(test, dm_dp_mst_is_port_support_mode(ctx.aconnector, ctx.stream),
			DC_FAIL_BANDWIDTH_VALIDATE);
	KUNIT_EXPECT_EQ(test, ctx.aconnector->mst_local_bw, 1257120U);
	KUNIT_EXPECT_EQ(test, ctx.aconnector->vc_full_pbn, ctx.port->full_pbn);
}

/**
 * dm_mst_test_port_mode_last_link_cached_bw - a cached last link bandwidth is reused
 * @test: KUnit test context
 *
 * While the virtual channel allocation is unchanged the previously read link
 * bandwidth is reused instead of going out to DPCD again.
 */
static void dm_mst_test_port_mode_last_link_cached_bw(struct kunit *test)
{
	struct dm_mst_test_port_mode_ctx ctx;

	dm_mst_test_init_port_mode_ctx(test, &ctx, 2500000, 1000, 100000);
	dm_mst_test_enable_dsc(test, &ctx);
	ctx.port->pdt = DP_PEER_DEVICE_SST_SINK;
	ctx.aconnector->vc_full_pbn = ctx.port->full_pbn;
	ctx.aconnector->mst_local_bw = 100000;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_is_port_support_mode(ctx.aconnector, ctx.stream),
			DC_FAIL_BANDWIDTH_VALIDATE);
	KUNIT_EXPECT_EQ(test, ctx.aconnector->mst_local_bw, 100000U);
}

/**
 * dm_mst_test_port_mode_last_link_synaptics_quirk - Synaptics hubs skip the check
 * @test: KUnit test context
 *
 * Synaptics branch devices misreport their last link settings, so the last
 * link bandwidth check is skipped for them and the mode is accepted.
 */
static void dm_mst_test_port_mode_last_link_synaptics_quirk(struct kunit *test)
{
	struct dm_mst_test_port_mode_ctx ctx;

	dm_mst_test_init_port_mode_ctx(test, &ctx, 2500000, 1000, 100000);
	dm_mst_test_enable_dsc(test, &ctx);
	ctx.port->pdt = DP_PEER_DEVICE_SST_SINK;
	ctx.aconnector->branch_ieee_oui = DP_BRANCH_DEVICE_ID_90CC24;
	dm_mst_test_set_link_settings(DP_LINK_BW_1_62, 1, DP_8b_10b_ENCODING);

	KUNIT_EXPECT_EQ(test, dm_dp_mst_is_port_support_mode(ctx.aconnector, ctx.stream), DC_OK);
	KUNIT_EXPECT_EQ(test, (u32)ctx.stream->timing.flags.DSC, 1U);
}

/**
 * dm_mst_test_port_mode_upstream_vc_too_small - the upstream hop is the bottleneck
 * @test: KUnit test context
 *
 * For a nested topology the virtual channel of the link before the last one
 * caps the compressed bandwidth, and a mode that exceeds it is rejected.
 */
static void dm_mst_test_port_mode_upstream_vc_too_small(struct kunit *test)
{
	struct dm_mst_test_port_mode_ctx ctx;
	struct drm_dp_mst_port *upstream;

	dm_mst_test_init_port_mode_ctx(test, &ctx, 2500000, 1000, 100000);
	dm_mst_test_enable_dsc(test, &ctx);
	ctx.port->pdt = DP_PEER_DEVICE_NONE;

	upstream = kunit_kzalloc(test, sizeof(*upstream), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, upstream);
	/* 100 PBN is about 670950 kbps, below the DSC minimum for this mode. */
	upstream->full_pbn = 100;
	ctx.port->parent->port_parent = upstream;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_is_port_support_mode(ctx.aconnector, ctx.stream),
			DC_FAIL_BANDWIDTH_VALIDATE);
}

/**
 * dm_mst_test_port_mode_branch_throughput_exceeded - branch decoder limit applies
 * @test: KUnit test context
 *
 * Even with a valid DSC configuration the branch decoder's overall throughput
 * for RGB must be able to carry the pixel rate.
 */
static void dm_mst_test_port_mode_branch_throughput_exceeded(struct kunit *test)
{
	struct dm_mst_test_port_mode_ctx ctx;

	dm_mst_test_init_port_mode_ctx(test, &ctx, 2500000, 1000, 100000);
	dm_mst_test_enable_dsc(test, &ctx);
	ctx.port->passthrough_aux = ctx.port->mgr->aux;
	/* 100 Mpix/s cannot carry the 148.5 Mpix/s this mode needs. */
	ctx.sink->dsc_caps.dsc_dec_caps.branch_overall_throughput_0_mps = 100;

	KUNIT_EXPECT_EQ(test, dm_dp_mst_is_port_support_mode(ctx.aconnector, ctx.stream),
			DC_FAIL_BANDWIDTH_VALIDATE);
}

/* Tests for get_conv_frl_bw */

/* Deterministic stand-in for the DC raw-FRL-rate lookup table. */
static uint32_t dm_mst_test_bw_kbps_from_raw_frl(uint8_t bw)
{
	return bw * 3000;
}

/*
 * Wire up a DP-to-HDMI2.1 protocol converter: the PCON capability lives on the
 * DC caps, the converter's own limit in the downstream port caps, and the sink
 * limits in the EDID caps.
 */
static struct amdgpu_dm_connector *dm_mst_test_alloc_frl_connector(struct kunit *test,
								   bool pcon_support,
								   u8 dwn_strm_port_type,
								   u8 max_encoded_link_bw,
								   u8 max_frl_rate,
								   u8 frl_dsc_max_frl_rate)
{
	struct amdgpu_dm_connector *aconnector;
	struct link_service *link_srv;
	struct dc_sink *sink;
	struct dc_link *link;
	struct dc *dc;

	aconnector = kunit_kzalloc(test, sizeof(*aconnector), GFP_KERNEL);
	sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);
	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, aconnector);
	KUNIT_ASSERT_NOT_NULL(test, sink);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);

	link_srv->bw_kbps_from_raw_frl_link_rate_data = dm_mst_test_bw_kbps_from_raw_frl;

	dc = dm_kunit_alloc_dc_with_ctx(test);
	dc->link_srv = link_srv;
	dc->caps.dp_hdmi21_pcon_support = pcon_support;

	link = dm_kunit_alloc_link(test);
	link->dc = dc;

	sink->edid_caps.max_frl_rate = max_frl_rate;
	sink->edid_caps.frl_dsc_max_frl_rate = frl_dsc_max_frl_rate;

	aconnector->dc_link = link;
	aconnector->dc_sink = sink;
	aconnector->mst_downstream_port_caps.bytes.byte0.bits.DWN_STRM_PORTX_TYPE =
		dwn_strm_port_type;
	aconnector->mst_downstream_port_caps.bytes.byte2.bits.MAX_ENCODED_LINK_BW_SUPPORT =
		max_encoded_link_bw;

	return aconnector;
}

/**
 * dm_mst_test_conv_frl_bw_no_pcon_support - DC without PCON support finds no FRL
 * @test: KUnit test context
 */
static void dm_mst_test_conv_frl_bw_no_pcon_support(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	uint32_t bw_in_kbps = 0;
	uint32_t dsc_bw_in_kbps = 0;

	aconnector = dm_mst_test_alloc_frl_connector(test, false, DOWN_STREAM_DETAILED_HDMI,
						     3, 5, 2);

	KUNIT_EXPECT_FALSE(test, get_conv_frl_bw(aconnector, &bw_in_kbps, &dsc_bw_in_kbps));
	KUNIT_EXPECT_EQ(test, bw_in_kbps, 0U);
	KUNIT_EXPECT_EQ(test, dsc_bw_in_kbps, 0U);
}

/**
 * dm_mst_test_conv_frl_bw_not_hdmi_port - a non-HDMI downstream port has no FRL
 * @test: KUnit test context
 */
static void dm_mst_test_conv_frl_bw_not_hdmi_port(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	uint32_t bw_in_kbps = 0;
	uint32_t dsc_bw_in_kbps = 0;

	aconnector = dm_mst_test_alloc_frl_connector(test, true, DOWN_STREAM_DETAILED_DP, 3, 5, 2);

	KUNIT_EXPECT_FALSE(test, get_conv_frl_bw(aconnector, &bw_in_kbps, &dsc_bw_in_kbps));
	KUNIT_EXPECT_EQ(test, bw_in_kbps, 0U);
}

/**
 * dm_mst_test_conv_frl_bw_sink_without_frl - a sink not reporting FRL is skipped
 * @test: KUnit test context
 *
 * Without a sink FRL rate in the EDID there is no endpoint to negotiate with,
 * so no bandwidth is reported even though the converter advertises one.
 */
static void dm_mst_test_conv_frl_bw_sink_without_frl(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	uint32_t bw_in_kbps = 0;
	uint32_t dsc_bw_in_kbps = 0;

	aconnector = dm_mst_test_alloc_frl_connector(test, true, DOWN_STREAM_DETAILED_HDMI,
						     3, 0, 2);

	KUNIT_EXPECT_FALSE(test, get_conv_frl_bw(aconnector, &bw_in_kbps, &dsc_bw_in_kbps));
	KUNIT_EXPECT_EQ(test, bw_in_kbps, 0U);
}

/**
 * dm_mst_test_conv_frl_bw_bottleneck - the converter and sink limits are combined
 * @test: KUnit test context
 *
 * The reported bandwidth is the smaller of the converter and sink FRL rates,
 * and the DSC bandwidth is further capped by the sink's DSC FRL rate.
 */
static void dm_mst_test_conv_frl_bw_bottleneck(struct kunit *test)
{
	struct amdgpu_dm_connector *aconnector;
	uint32_t bw_in_kbps = 0;
	uint32_t dsc_bw_in_kbps = 0;

	aconnector = dm_mst_test_alloc_frl_connector(test, true, DOWN_STREAM_DETAILED_HDMI,
						     3, 5, 2);

	KUNIT_EXPECT_TRUE(test, get_conv_frl_bw(aconnector, &bw_in_kbps, &dsc_bw_in_kbps));
	KUNIT_EXPECT_EQ(test, bw_in_kbps, 9000U);
	KUNIT_EXPECT_EQ(test, dsc_bw_in_kbps, 6000U);
}

/* Tests for log_dsc_params */

/**
 * dm_mst_test_log_dsc_params - logging the fairness vars leaves them untouched
 * @test: KUnit test context
 *
 * log_dsc_params() only traces, so the only observable contract is that it
 * walks @count entries starting at @k without modifying them.
 */
static void dm_mst_test_log_dsc_params(struct kunit *test)
{
	struct dsc_mst_fairness_vars vars[3] = {
		{ .pbn = 100, .dsc_enabled = false, .bpp_x16 = 160 },
		{ .pbn = 200, .dsc_enabled = true, .bpp_x16 = 192 },
		{ .pbn = 300, .dsc_enabled = true, .bpp_x16 = 256 },
	};

	log_dsc_params(2, vars, 1);

	KUNIT_EXPECT_EQ(test, vars[1].pbn, 200);
	KUNIT_EXPECT_EQ(test, vars[2].bpp_x16, 256);
}

/* Tests for find_crtc_index_in_state_by_stream and is_dsc_precompute_needed */

struct dm_mst_test_crtc_state_ctx {
	struct drm_atomic_commit *state;
	struct drm_crtc *crtcs;
	struct dm_crtc_state *crtc_states;
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;
};

/*
 * Hand-build an atomic state with @num_crtc CRTCs, each carrying a DM CRTC
 * state. drm_atomic_state_alloc() would need a fully registered mode config,
 * and the helpers under test only walk the crtcs/connectors arrays.
 */
static void dm_mst_test_init_crtc_state_ctx(struct kunit *test,
					    struct dm_mst_test_crtc_state_ctx *ctx,
					    unsigned int num_crtc)
{
	struct amdgpu_device *adev;
	unsigned int i;

	adev = dm_kunit_alloc_adev(test);
	adev->ddev.mode_config.num_crtc = num_crtc;

	ctx->state = kunit_kzalloc(test, sizeof(*ctx->state), GFP_KERNEL);
	ctx->crtcs = kunit_kcalloc(test, num_crtc, sizeof(*ctx->crtcs), GFP_KERNEL);
	ctx->crtc_states = kunit_kcalloc(test, num_crtc, sizeof(*ctx->crtc_states), GFP_KERNEL);
	ctx->connector = kunit_kzalloc(test, sizeof(*ctx->connector), GFP_KERNEL);
	ctx->conn_state = kunit_kzalloc(test, sizeof(*ctx->conn_state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state);
	KUNIT_ASSERT_NOT_NULL(test, ctx->crtcs);
	KUNIT_ASSERT_NOT_NULL(test, ctx->crtc_states);
	KUNIT_ASSERT_NOT_NULL(test, ctx->connector);
	KUNIT_ASSERT_NOT_NULL(test, ctx->conn_state);

	ctx->state->dev = &adev->ddev;
	ctx->state->crtcs = kunit_kcalloc(test, num_crtc, sizeof(*ctx->state->crtcs), GFP_KERNEL);
	ctx->state->connectors = kunit_kzalloc(test, sizeof(*ctx->state->connectors), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state->crtcs);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state->connectors);

	for (i = 0; i < num_crtc; i++) {
		ctx->crtcs[i].dev = &adev->ddev;
		ctx->crtc_states[i].base.crtc = &ctx->crtcs[i];
		ctx->state->crtcs[i].ptr = &ctx->crtcs[i];
		ctx->state->crtcs[i].new_state = &ctx->crtc_states[i].base;
		ctx->state->crtcs[i].old_state = &ctx->crtc_states[i].base;
	}
}

/* Attach the context's connector to @crtc so the CRTC looks driven. */
static void dm_mst_test_attach_connector(struct dm_mst_test_crtc_state_ctx *ctx,
					 struct drm_crtc *crtc)
{
	ctx->conn_state->crtc = crtc;
	ctx->state->connectors[0].ptr = ctx->connector;
	ctx->state->connectors[0].new_state = ctx->conn_state;
	ctx->state->num_connector = 1;
}

/**
 * dm_mst_test_find_crtc_index_matches - the CRTC driving a stream is found
 * @test: KUnit test context
 */
static void dm_mst_test_find_crtc_index_matches(struct kunit *test)
{
	struct dm_mst_test_crtc_state_ctx ctx;
	struct dc_stream_state *stream;

	dm_mst_test_init_crtc_state_ctx(test, &ctx, 3);
	stream = dm_kunit_alloc_stream(test, NULL);
	ctx.crtc_states[2].stream = stream;

	KUNIT_EXPECT_EQ(test, find_crtc_index_in_state_by_stream(ctx.state, stream), 2);
}

/**
 * dm_mst_test_find_crtc_index_no_match - an unknown stream yields -1
 * @test: KUnit test context
 */
static void dm_mst_test_find_crtc_index_no_match(struct kunit *test)
{
	struct dm_mst_test_crtc_state_ctx ctx;
	struct dc_stream_state *stream;

	dm_mst_test_init_crtc_state_ctx(test, &ctx, 2);
	stream = dm_kunit_alloc_stream(test, NULL);

	KUNIT_EXPECT_EQ(test, find_crtc_index_in_state_by_stream(ctx.state, stream), -1);
}

struct dm_mst_precompute_param {
	const char *name;
	bool connector_attached;
	bool has_stream;
	enum dc_connection_type link_type;
	bool dsc_support;
	bool dsc_passthrough;
	bool needed;
};

static const struct dm_mst_precompute_param dm_mst_precompute_params[] = {
	{ "no_connector", false, true, dc_connection_mst_branch, true, false, false },
	{ "dsc_hub", true, true, dc_connection_mst_branch, true, false, true },
	{ "dsc_passthrough_hub", true, true, dc_connection_mst_branch, false, true, true },
	{ "sst_link", true, true, dc_connection_single, true, false, false },
	{ "mst_without_dsc", true, true, dc_connection_mst_branch, false, false, false },
	{ "no_stream", true, false, dc_connection_mst_branch, true, false, false },
};

KUNIT_ARRAY_PARAM_DESC(dm_mst_precompute, dm_mst_precompute_params, name);

/**
 * dm_mst_test_dsc_precompute_needed - precompute needs a driven DSC MST hub
 * @test: KUnit test context
 *
 * A DSC or DSC passthrough capable MST branch that a connector in the state
 * drives needs its bandwidth precomputed. A CRTC no connector drives aborts
 * the scan, and SST links or MST branches without DSC never need it.
 */
static void dm_mst_test_dsc_precompute_needed(struct kunit *test)
{
	const struct dm_mst_precompute_param *param = test->param_value;
	struct dm_mst_test_crtc_state_ctx ctx;
	struct dc_link *link;

	dm_mst_test_init_crtc_state_ctx(test, &ctx, 1);
	if (param->connector_attached)
		dm_mst_test_attach_connector(&ctx, &ctx.crtcs[0]);

	if (param->has_stream) {
		link = dm_kunit_alloc_link(test);
		link->type = param->link_type;
		link->dpcd_caps.dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_SUPPORT =
			param->dsc_support;
		link->dpcd_caps.dsc_caps.dsc_basic_caps.fields.dsc_support.DSC_PASSTHROUGH_SUPPORT =
			param->dsc_passthrough;
		ctx.crtc_states[0].stream = dm_kunit_alloc_stream(test, link);
	}

	KUNIT_EXPECT_EQ(test, is_dsc_precompute_needed(ctx.state), param->needed);
}

/* Tests for is_dsc_need_re_compute */

struct dm_mst_test_recompute_ctx {
	struct amdgpu_device *adev;
	struct drm_atomic_commit *state;
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	struct dc_state *dc_state;
	struct dc_link *link;
	struct dc *dc;
};

/*
 * One MST connector on one CRTC, with an empty new dc_state and an empty
 * current dc_state. Tests populate the streams they need and adjust the CRTC
 * state flags to select the branch under test.
 */
static void dm_mst_test_init_recompute_ctx(struct kunit *test,
					   struct dm_mst_test_recompute_ctx *ctx)
{
	struct drm_crtc *crtc;

	ctx->adev = dm_kunit_alloc_adev(test);
	ctx->adev->ddev.mode_config.num_crtc = 1;

	ctx->dc = dm_kunit_alloc_dc_with_ctx(test);
	ctx->dc->current_state = dm_kunit_alloc_dc_state(test);
	ctx->dc_state = dm_kunit_alloc_dc_state(test);

	ctx->link = dm_kunit_alloc_link(test);
	ctx->link->dc = ctx->dc;
	ctx->link->type = dc_connection_mst_branch;

	ctx->aconnector = dm_kunit_alloc_connector(test, ctx->adev, ctx->link);
	ctx->conn_state = kunit_kzalloc(test, sizeof(*ctx->conn_state), GFP_KERNEL);
	crtc = kunit_kzalloc(test, sizeof(*crtc), GFP_KERNEL);
	ctx->crtc_state = kunit_kzalloc(test, sizeof(*ctx->crtc_state), GFP_KERNEL);
	ctx->state = kunit_kzalloc(test, sizeof(*ctx->state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->conn_state);
	KUNIT_ASSERT_NOT_NULL(test, crtc);
	KUNIT_ASSERT_NOT_NULL(test, ctx->crtc_state);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state);

	ctx->state->dev = &ctx->adev->ddev;
	ctx->state->crtcs = kunit_kzalloc(test, sizeof(*ctx->state->crtcs), GFP_KERNEL);
	ctx->state->connectors = kunit_kzalloc(test, sizeof(*ctx->state->connectors), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state->crtcs);
	KUNIT_ASSERT_NOT_NULL(test, ctx->state->connectors);

	crtc->dev = &ctx->adev->ddev;
	ctx->crtc_state->crtc = crtc;
	ctx->state->crtcs[0].ptr = crtc;
	ctx->state->crtcs[0].new_state = ctx->crtc_state;
	ctx->state->crtcs[0].old_state = ctx->crtc_state;

	ctx->conn_state->connector = &ctx->aconnector->base;
	ctx->conn_state->crtc = crtc;
	ctx->state->connectors[0].ptr = &ctx->aconnector->base;
	ctx->state->connectors[0].new_state = ctx->conn_state;
	ctx->state->num_connector = 1;
}

static struct dc_stream_state *dm_mst_test_add_link_stream(struct kunit *test,
							   struct dc_state *dc_state,
							   struct dc_link *link,
							   struct amdgpu_dm_connector *aconnector)
{
	struct dc_stream_state *stream = dm_kunit_alloc_stream(test, link);

	KUNIT_ASSERT_LT(test, dc_state->stream_count, MAX_PIPES);
	stream->dm_stream_context = aconnector;
	dc_state->streams[dc_state->stream_count++] = stream;

	return stream;
}

/**
 * dm_mst_test_recompute_not_mst_branch - only MST branches are recomputed
 * @test: KUnit test context
 */
static void dm_mst_test_recompute_not_mst_branch(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;

	dm_mst_test_init_recompute_ctx(test, &ctx);
	ctx.link->type = dc_connection_single;

	KUNIT_EXPECT_FALSE(test, is_dsc_need_re_compute(ctx.state, ctx.dc_state, ctx.link));
}

/**
 * dm_mst_test_recompute_legacy_hub_without_dsc - old hubs without DSC are skipped
 * @test: KUnit test context
 *
 * A hub matching the no-virtual-DPCD workaround but reporting neither DSC nor
 * DSC passthrough support cannot use MST DSC at all.
 */
static void dm_mst_test_recompute_legacy_hub_without_dsc(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;

	dm_mst_test_init_recompute_ctx(test, &ctx);
	dm_mst_test_arm_dsc_aux_workaround(ctx.link);

	KUNIT_EXPECT_FALSE(test, is_dsc_need_re_compute(ctx.state, ctx.dc_state, ctx.link));
}

/**
 * dm_mst_test_recompute_no_stream_on_link - no stream on the link, nothing to do
 * @test: KUnit test context
 *
 * The new state only drives another link, so this hub keeps its current DSC
 * configuration.
 */
static void dm_mst_test_recompute_no_stream_on_link(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;
	struct dc_link *other_link;

	dm_mst_test_init_recompute_ctx(test, &ctx);
	other_link = dm_kunit_alloc_link(test);
	dm_mst_test_add_link_stream(test, ctx.dc_state, other_link, ctx.aconnector);

	KUNIT_EXPECT_FALSE(test, is_dsc_need_re_compute(ctx.state, ctx.dc_state, ctx.link));
}

/**
 * dm_mst_test_recompute_on_mode_change - a modeset on the link forces a recompute
 * @test: KUnit test context
 */
static void dm_mst_test_recompute_on_mode_change(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;

	dm_mst_test_init_recompute_ctx(test, &ctx);
	dm_mst_test_add_link_stream(test, ctx.dc_state, ctx.link, ctx.aconnector);
	ctx.crtc_state->enable = true;
	ctx.crtc_state->active = true;
	ctx.crtc_state->mode_changed = true;

	KUNIT_EXPECT_TRUE(test, is_dsc_need_re_compute(ctx.state, ctx.dc_state, ctx.link));
}

/**
 * dm_mst_test_recompute_unchanged_stream - an untouched stream needs no recompute
 * @test: KUnit test context
 *
 * The same stream is present in both the new and the current state and its
 * CRTC reports no change, so the existing DSC configuration still applies.
 */
static void dm_mst_test_recompute_unchanged_stream(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;

	dm_mst_test_init_recompute_ctx(test, &ctx);
	dm_mst_test_add_link_stream(test, ctx.dc_state, ctx.link, ctx.aconnector);
	dm_mst_test_add_link_stream(test, ctx.dc->current_state, ctx.link, ctx.aconnector);
	ctx.crtc_state->enable = true;
	ctx.crtc_state->active = true;

	KUNIT_EXPECT_FALSE(test, is_dsc_need_re_compute(ctx.state, ctx.dc_state, ctx.link));
}

/**
 * dm_mst_test_recompute_stream_removed - dropping a stream forces a recompute
 * @test: KUnit test context
 *
 * A stream that is on the link in the current state but absent from the new
 * state frees up bandwidth, so the remaining streams must be recomputed.
 */
static void dm_mst_test_recompute_stream_removed(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;
	struct amdgpu_dm_connector *gone;

	dm_mst_test_init_recompute_ctx(test, &ctx);
	gone = dm_kunit_alloc_connector(test, ctx.adev, ctx.link);
	dm_mst_test_add_link_stream(test, ctx.dc_state, ctx.link, ctx.aconnector);
	dm_mst_test_add_link_stream(test, ctx.dc->current_state, ctx.link, gone);
	ctx.crtc_state->enable = true;
	ctx.crtc_state->active = true;

	KUNIT_EXPECT_TRUE(test, is_dsc_need_re_compute(ctx.state, ctx.dc_state, ctx.link));
}

/**
 * dm_mst_test_recompute_stream_without_connector - streams need a DM connector
 * @test: KUnit test context
 *
 * A stream on the link whose DM context is not set cannot be attributed to a
 * connector, so it does not count towards the streams on the link.
 */
static void dm_mst_test_recompute_stream_without_connector(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;

	dm_mst_test_init_recompute_ctx(test, &ctx);
	dm_mst_test_add_link_stream(test, ctx.dc_state, ctx.link, NULL);

	KUNIT_EXPECT_FALSE(test, is_dsc_need_re_compute(ctx.state, ctx.dc_state, ctx.link));
}

/**
 * dm_mst_test_recompute_connector_without_crtc - a disabled connector is skipped
 * @test: KUnit test context
 */
static void dm_mst_test_recompute_connector_without_crtc(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;

	dm_mst_test_init_recompute_ctx(test, &ctx);
	dm_mst_test_add_link_stream(test, ctx.dc_state, ctx.link, ctx.aconnector);
	dm_mst_test_add_link_stream(test, ctx.dc->current_state, ctx.link, ctx.aconnector);
	ctx.conn_state->crtc = NULL;

	KUNIT_EXPECT_FALSE(test, is_dsc_need_re_compute(ctx.state, ctx.dc_state, ctx.link));
}

/* Tests for compute_mst_dsc_configs_for_state and pre_validate_dsc */

typedef enum dc_status (*dm_mst_test_remove_stream_fn)(struct dc *dc, struct dc_state *new_ctx,
						       struct dc_stream_state *stream);

static enum dc_status dm_mst_test_remove_stream_fails(struct dc *dc, struct dc_state *new_ctx,
						      struct dc_stream_state *stream)
{
	return DC_ERROR_UNEXPECTED;
}

/*
 * Add a stream to @ctx that walks as far into the DSC config helpers as the
 * caller allows: an MST signal, a DM connector, an MST output port and a DSC
 * capable sink. The helpers dereference res_pool->funcs unconditionally, so
 * the pool is always given one, carrying @remove_stream.
 */
static struct dc_stream_state *
dm_mst_test_add_mst_dsc_stream(struct kunit *test, struct dm_mst_test_recompute_ctx *ctx,
			       dm_mst_test_remove_stream_fn remove_stream)
{
	struct dc_stream_state *stream;
	struct resource_pool *res_pool;
	struct drm_dp_mst_port *port;
	struct resource_funcs *funcs;
	struct dc_sink *sink;

	sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);
	port = kunit_kzalloc(test, sizeof(*port), GFP_KERNEL);
	res_pool = kunit_kzalloc(test, sizeof(*res_pool), GFP_KERNEL);
	funcs = kunit_kzalloc(test, sizeof(*funcs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sink);
	KUNIT_ASSERT_NOT_NULL(test, port);
	KUNIT_ASSERT_NOT_NULL(test, res_pool);
	KUNIT_ASSERT_NOT_NULL(test, funcs);

	funcs->remove_stream_from_ctx = remove_stream;
	res_pool->funcs = funcs;
	ctx->dc->res_pool = res_pool;
	sink->dsc_caps.dsc_dec_caps.is_dsc_supported = true;
	ctx->aconnector->dc_sink = sink;
	ctx->aconnector->mst_output_port = port;

	stream = dm_mst_test_add_link_stream(test, ctx->dc_state, ctx->link, ctx->aconnector);
	stream->ctx = ctx->dc->ctx;
	stream->signal = SIGNAL_TYPE_DISPLAY_PORT_MST;

	return stream;
}

/**
 * dm_mst_test_compute_configs_skips_sst - non-MST streams are not considered
 * @test: KUnit test context
 */
static void dm_mst_test_compute_configs_skips_sst(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dc_stream_state *stream;

	dm_mst_test_init_recompute_ctx(test, &ctx);
	stream = dm_mst_test_add_link_stream(test, ctx.dc_state, ctx.link, ctx.aconnector);
	stream->ctx = ctx.dc->ctx;
	stream->signal = SIGNAL_TYPE_DISPLAY_PORT;

	KUNIT_EXPECT_EQ(test, compute_mst_dsc_configs_for_state(ctx.state, ctx.dc_state, vars), 0);
}

/**
 * dm_mst_test_compute_configs_skips_incomplete - streams without a sink are skipped
 * @test: KUnit test context
 *
 * An MST stream whose connector has neither a sink nor an output port yet is
 * not ready for DSC bandwidth sharing.
 */
static void dm_mst_test_compute_configs_skips_incomplete(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dc_stream_state *stream;

	dm_mst_test_init_recompute_ctx(test, &ctx);
	stream = dm_mst_test_add_link_stream(test, ctx.dc_state, ctx.link, ctx.aconnector);
	stream->ctx = ctx.dc->ctx;
	stream->signal = SIGNAL_TYPE_DISPLAY_PORT_MST;

	KUNIT_EXPECT_EQ(test, compute_mst_dsc_configs_for_state(ctx.state, ctx.dc_state, vars), 0);
}

/**
 * dm_mst_test_compute_configs_remove_stream_fails - a DC resource failure aborts
 * @test: KUnit test context
 */
static void dm_mst_test_compute_configs_remove_stream_fails(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};

	dm_mst_test_init_recompute_ctx(test, &ctx);
	dm_mst_test_add_mst_dsc_stream(test, &ctx, dm_mst_test_remove_stream_fails);

	KUNIT_EXPECT_EQ(test, compute_mst_dsc_configs_for_state(ctx.state, ctx.dc_state, vars),
			-EINVAL);
}

/**
 * dm_mst_test_compute_configs_no_recompute - an unchanged topology is left alone
 * @test: KUnit test context
 *
 * The stream is DSC capable but is_dsc_need_re_compute() reports no change, so
 * the existing configuration is kept and no DSC resource is requested.
 */
static void dm_mst_test_compute_configs_no_recompute(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};

	dm_mst_test_init_recompute_ctx(test, &ctx);
	ctx.link->type = dc_connection_single;
	dm_mst_test_add_mst_dsc_stream(test, &ctx, NULL);

	KUNIT_EXPECT_EQ(test, compute_mst_dsc_configs_for_state(ctx.state, ctx.dc_state, vars), 0);
}

/**
 * dm_mst_test_pre_validate_dsc_not_needed - precompute is skipped when unneeded
 * @test: KUnit test context
 *
 * Without a DSC capable MST hub in the state there is nothing to precompute,
 * so pre_validate_dsc() must succeed without touching the DM atomic state.
 */
static void dm_mst_test_pre_validate_dsc_not_needed(struct kunit *test)
{
	struct dm_mst_test_crtc_state_ctx ctx;
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dm_atomic_state *dm_state = NULL;

	dm_mst_test_init_crtc_state_ctx(test, &ctx, 1);

	KUNIT_EXPECT_EQ(test, pre_validate_dsc(ctx.state, &dm_state, vars), 0);
	KUNIT_EXPECT_NULL(test, dm_state);
}

/* Tests for pre_compute_mst_dsc_configs_for_state */

/**
 * dm_mst_test_pre_compute_configs_skips_sst - non-MST streams are not considered
 * @test: KUnit test context
 */
static void dm_mst_test_pre_compute_configs_skips_sst(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dc_stream_state *stream;

	dm_mst_test_init_recompute_ctx(test, &ctx);
	stream = dm_mst_test_add_link_stream(test, ctx.dc_state, ctx.link, ctx.aconnector);
	stream->ctx = ctx.dc->ctx;
	stream->signal = SIGNAL_TYPE_DISPLAY_PORT;

	KUNIT_EXPECT_EQ(test, pre_compute_mst_dsc_configs_for_state(ctx.state, ctx.dc_state, vars), 0);
}

/**
 * dm_mst_test_pre_compute_configs_skips_incomplete - streams without a sink are skipped
 * @test: KUnit test context
 */
static void dm_mst_test_pre_compute_configs_skips_incomplete(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dc_stream_state *stream;

	dm_mst_test_init_recompute_ctx(test, &ctx);
	stream = dm_mst_test_add_link_stream(test, ctx.dc_state, ctx.link, ctx.aconnector);
	stream->ctx = ctx.dc->ctx;
	stream->signal = SIGNAL_TYPE_DISPLAY_PORT_MST;

	KUNIT_EXPECT_EQ(test, pre_compute_mst_dsc_configs_for_state(ctx.state, ctx.dc_state, vars), 0);
}

/**
 * dm_mst_test_pre_compute_configs_no_recompute - an unchanged topology is left alone
 * @test: KUnit test context
 *
 * The stream is DSC capable but is_dsc_need_re_compute() reports no change, so
 * the precompute leaves the existing configuration in place.
 */
static void dm_mst_test_pre_compute_configs_no_recompute(struct kunit *test)
{
	struct dm_mst_test_recompute_ctx ctx;
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};

	dm_mst_test_init_recompute_ctx(test, &ctx);
	ctx.link->type = dc_connection_single;
	dm_mst_test_add_mst_dsc_stream(test, &ctx, NULL);

	KUNIT_EXPECT_EQ(test, pre_compute_mst_dsc_configs_for_state(ctx.state, ctx.dc_state, vars), 0);
}

/* Tests for compute_mst_dsc_configs_for_link */

static const struct drm_connector_funcs dm_mst_test_dsc_link_conn_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static enum dp_link_encoding dm_mst_test_mst_encoding_format(const struct dc_link *link)
{
	return DP_8b_10b_ENCODING;
}

struct dm_mst_test_dsc_link_ctx {
	struct drm_modeset_acquire_ctx acquire_ctx;
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_atomic_commit *state;
	struct dc_stream_state *stream;
	struct drm_device *drm;
	struct dc_state *dc_state;
};

static void dm_mst_test_dsc_link_drop_locks(void *data)
{
	struct dm_mst_test_dsc_link_ctx *ctx = data;

	ctx->drm->mode_config.acquire_ctx = NULL;
	drm_modeset_drop_locks(&ctx->acquire_ctx);
	drm_modeset_acquire_fini(&ctx->acquire_ctx);
}

static void dm_mst_test_destroy_mst_mgr(void *data)
{
	struct drm_dp_mst_topology_mgr *mgr = data;

	mgr->mst_state = false;
	drm_dp_mst_topology_mgr_destroy(mgr);
}

/*
 * Full fixture for the DSC bandwidth sharing loop: a real DRM pipe and atomic
 * state (the DRM MST helpers need private object locking), a real topology
 * manager, and one MST stream whose 1920x1080 timing does not fit
 * uncompressed. @total_avail_slots decides which compression pass succeeds.
 *
 * The context is KUnit allocated because the modeset locks are dropped from a
 * deferred action, long after the test body has returned.
 */
static struct dm_mst_test_dsc_link_ctx *
dm_mst_test_alloc_dsc_link_ctx(struct kunit *test, int total_avail_slots)
{
	struct dm_mst_test_dsc_link_ctx *ctx;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	struct link_service *link_srv;
	struct resource_pool *res_pool;
	struct drm_dp_mst_port *port;
	struct drm_plane *primary;
	struct drm_crtc *crtc;
	struct dc_sink *sink;
	struct dc_link *link;
	struct device *dev;
	struct dc *dc;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	ctx->drm = __drm_kunit_helper_alloc_drm_device(test, dev,
						       sizeof(struct amdgpu_device),
						       offsetof(struct amdgpu_device, ddev),
						       DRIVER_MODESET | DRIVER_ATOMIC);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->drm);

	primary = drm_kunit_helper_create_primary_plane(test, ctx->drm, NULL, NULL, NULL, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, primary);
	crtc = drm_kunit_helper_create_crtc(test, ctx->drm, primary, NULL, NULL, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc);

	ctx->aconnector = drmm_kzalloc(ctx->drm, sizeof(*ctx->aconnector), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->aconnector);
	KUNIT_ASSERT_EQ(test,
			drmm_connector_init(ctx->drm, &ctx->aconnector->base,
					    &dm_mst_test_dsc_link_conn_funcs,
					    DRM_MODE_CONNECTOR_DisplayPort, NULL), 0);
	drm_mode_config_reset(ctx->drm);

	KUNIT_ASSERT_EQ(test,
			drm_dp_mst_topology_mgr_init(&ctx->aconnector->mst_mgr, ctx->drm,
						     &ctx->aconnector->dm_dp_aux.aux, 16, 4,
						     ctx->aconnector->base.base.id), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_mst_test_destroy_mst_mgr,
						  &ctx->aconnector->mst_mgr), 0);

	drm_modeset_acquire_init(&ctx->acquire_ctx, 0);
	ctx->drm->mode_config.acquire_ctx = &ctx->acquire_ctx;
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dm_mst_test_dsc_link_drop_locks, ctx), 0);

	ctx->state = drm_kunit_helper_atomic_state_alloc(test, ctx->drm, &ctx->acquire_ctx);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->state);

	crtc_state = drm_atomic_get_crtc_state(ctx->state, crtc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, crtc_state);
	crtc_state->enable = true;
	crtc_state->active = true;
	crtc_state->mode_changed = true;

	conn_state = drm_atomic_get_connector_state(ctx->state, &ctx->aconnector->base);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, conn_state);
	/* Takes the connector reference that the state teardown drops again. */
	KUNIT_ASSERT_EQ(test, drm_atomic_set_crtc_for_connector(conn_state, crtc), 0);

	ctx->mst_state = drm_atomic_get_mst_topology_state(ctx->state, &ctx->aconnector->mst_mgr);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->mst_state);
	/* One time slot per 10 PBN keeps the slot counts small and exact. */
	ctx->mst_state->pbn_div.full = dfixed_const(10);
	ctx->mst_state->total_avail_slots = total_avail_slots;
	/* Needed for the payload limit check; mst_primary stays NULL. */
	ctx->aconnector->mst_mgr.mst_state = true;

	link_srv = kunit_kzalloc(test, sizeof(*link_srv), GFP_KERNEL);
	res_pool = kunit_kzalloc(test, sizeof(*res_pool), GFP_KERNEL);
	sink = kunit_kzalloc(test, sizeof(*sink), GFP_KERNEL);
	port = kunit_kzalloc(test, sizeof(*port), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, link_srv);
	KUNIT_ASSERT_NOT_NULL(test, res_pool);
	KUNIT_ASSERT_NOT_NULL(test, sink);
	KUNIT_ASSERT_NOT_NULL(test, port);

	link_srv->mst_decide_link_encoding_format = dm_mst_test_mst_encoding_format;

	dc = dm_kunit_alloc_dc_with_ctx(test);
	dc->link_srv = link_srv;
	dc->res_pool = res_pool;
	dc->current_state = dm_kunit_alloc_dc_state(test);

	link = dm_kunit_alloc_link(test);
	link->dc = dc;
	link->ctx = dc->ctx;
	link->type = dc_connection_mst_branch;

	sink->ctx = dc->ctx;
	dm_mst_test_setup_dsc_caps(test, dc, sink);

	/* The payload allocation takes a reference, so start above zero. */
	kref_init(&port->malloc_kref);
	port->mgr = &ctx->aconnector->mst_mgr;
	port->connector = &ctx->aconnector->base;
	port->full_pbn = 2000;

	ctx->aconnector->dc_link = link;
	ctx->aconnector->dc_sink = sink;
	ctx->aconnector->mst_output_port = port;

	ctx->dc_state = dm_kunit_alloc_dc_state(test);
	ctx->stream = dm_kunit_alloc_stream(test, link);
	ctx->stream->ctx = dc->ctx;
	ctx->stream->sink = sink;
	ctx->stream->dm_stream_context = ctx->aconnector;
	ctx->stream->signal = SIGNAL_TYPE_DISPLAY_PORT_MST;
	dm_mst_test_set_dsc_timing(&ctx->stream->timing);
	ctx->dc_state->streams[0] = ctx->stream;
	ctx->dc_state->stream_count = 1;

	return ctx;
}

/**
 * dm_mst_test_dsc_link_fits_uncompressed - plenty of time slots means no DSC
 * @test: KUnit test context
 *
 * The first pass allocates the uncompressed peak PBN and the MST check
 * succeeds, so DSC is left disabled for the stream.
 */
static void dm_mst_test_dsc_link_fits_uncompressed(struct kunit *test)
{
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dm_mst_test_dsc_link_ctx *ctx;

	ctx = dm_mst_test_alloc_dsc_link_ctx(test, 64);

	KUNIT_EXPECT_EQ(test, pre_compute_mst_dsc_configs_for_state(ctx->state, ctx->dc_state, vars), 0);
	KUNIT_EXPECT_FALSE(test, vars[0].dsc_enabled);
	KUNIT_EXPECT_EQ(test, (u32)ctx->stream->timing.flags.DSC, 0U);
	KUNIT_EXPECT_PTR_EQ(test, vars[0].aconnector, ctx->aconnector);
}

/**
 * dm_mst_test_dsc_link_enables_compression - a tight link is compressed
 * @test: KUnit test context
 *
 * The uncompressed allocation does not fit, so the driver falls back to
 * maximum compression and then optimises the bits per pixel back up. DSC ends
 * up enabled with a bpp above the policy minimum.
 */
static void dm_mst_test_dsc_link_enables_compression(struct kunit *test)
{
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dm_mst_test_dsc_link_ctx *ctx;

	ctx = dm_mst_test_alloc_dsc_link_ctx(test, 30);

	KUNIT_EXPECT_EQ(test, pre_compute_mst_dsc_configs_for_state(ctx->state, ctx->dc_state, vars), 0);
	KUNIT_EXPECT_TRUE(test, vars[0].dsc_enabled);
	KUNIT_EXPECT_GT(test, vars[0].bpp_x16, 0);
	KUNIT_EXPECT_EQ(test, (u32)ctx->stream->timing.flags.DSC, 1U);
}

/**
 * dm_mst_test_dsc_link_out_of_slots - even max compression can be too big
 * @test: KUnit test context
 *
 * When the fully compressed stream still exceeds the available time slots the
 * -ENOSPC from the MST check is propagated to the caller.
 */
static void dm_mst_test_dsc_link_out_of_slots(struct kunit *test)
{
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dm_mst_test_dsc_link_ctx *ctx;

	ctx = dm_mst_test_alloc_dsc_link_ctx(test, 10);

	KUNIT_EXPECT_EQ(test, pre_compute_mst_dsc_configs_for_state(ctx->state, ctx->dc_state, vars), -ENOSPC);
}

/**
 * dm_mst_test_dsc_link_forced_dsc - forced DSC discards the uncompressed pass
 * @test: KUnit test context
 *
 * With DSC forced on from debugfs the uncompressed allocation is not applied
 * even though it fits. The MST check returned 0 rather than -ENOSPC, so the
 * helper returns that success without programming a DSC config.
 */
static void dm_mst_test_dsc_link_forced_dsc(struct kunit *test)
{
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dm_mst_test_dsc_link_ctx *ctx;

	ctx = dm_mst_test_alloc_dsc_link_ctx(test, 64);
	ctx->aconnector->dsc_settings.dsc_force_enable = DSC_CLK_FORCE_ENABLE;

	KUNIT_EXPECT_EQ(test, pre_compute_mst_dsc_configs_for_state(ctx->state, ctx->dc_state, vars), 0);
	KUNIT_EXPECT_FALSE(test, vars[0].dsc_enabled);
	KUNIT_EXPECT_EQ(test, (u32)ctx->stream->timing.flags.DSC, 0U);
}

/**
 * dm_mst_test_dsc_link_forced_dsc_stays_on - forced DSC is never disabled again
 * @test: KUnit test context
 *
 * Once compression is needed, try_disable_dsc() only reconsiders streams left
 * at the default DSC clock setting, so a forced stream keeps DSC enabled.
 */
static void dm_mst_test_dsc_link_forced_dsc_stays_on(struct kunit *test)
{
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dm_mst_test_dsc_link_ctx *ctx;

	ctx = dm_mst_test_alloc_dsc_link_ctx(test, 30);
	ctx->aconnector->dsc_settings.dsc_force_enable = DSC_CLK_FORCE_ENABLE;

	KUNIT_EXPECT_EQ(test, pre_compute_mst_dsc_configs_for_state(ctx->state, ctx->dc_state, vars), 0);
	KUNIT_EXPECT_TRUE(test, vars[0].dsc_enabled);
}

/**
 * dm_mst_test_dsc_link_bpp_overwrite - a forced bpp overrides the computed one
 * @test: KUnit test context
 */
static void dm_mst_test_dsc_link_bpp_overwrite(struct kunit *test)
{
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dm_mst_test_dsc_link_ctx *ctx;

	ctx = dm_mst_test_alloc_dsc_link_ctx(test, 30);
	ctx->aconnector->dsc_settings.dsc_bits_per_pixel = 10 * 16;
	ctx->aconnector->dsc_settings.dsc_num_slices_h = 2;
	ctx->aconnector->dsc_settings.dsc_num_slices_v = 4;

	KUNIT_EXPECT_EQ(test, pre_compute_mst_dsc_configs_for_state(ctx->state, ctx->dc_state, vars), 0);
	KUNIT_EXPECT_EQ(test, ctx->stream->timing.dsc_cfg.bits_per_pixel, 10 * 16);
	KUNIT_EXPECT_EQ(test, (u32)ctx->stream->timing.dsc_cfg.num_slices_h, 2U);
	KUNIT_EXPECT_EQ(test, (u32)ctx->stream->timing.dsc_cfg.num_slices_v, 4U);
}

/**
 * dm_mst_test_dsc_link_compression_disabled - forcing DSC off keeps it off
 * @test: KUnit test context
 *
 * A stream whose DSC clock is force disabled is allocated its uncompressed
 * bandwidth in the max compression pass as well.
 */
static void dm_mst_test_dsc_link_compression_disabled(struct kunit *test)
{
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dm_mst_test_dsc_link_ctx *ctx;

	ctx = dm_mst_test_alloc_dsc_link_ctx(test, 30);
	ctx->aconnector->dsc_settings.dsc_force_enable = DSC_CLK_FORCE_DISABLE;

	KUNIT_EXPECT_EQ(test, pre_compute_mst_dsc_configs_for_state(ctx->state, ctx->dc_state, vars), -ENOSPC);
}

/**
 * dm_mst_test_dsc_link_bpp_between_limits - spare slots raise the bits per pixel
 * @test: KUnit test context
 *
 * With only part of the slack available the optimisation loop hands out a fair
 * share of the free time slots and derives the resulting bits per pixel from
 * the new PBN, landing between the policy minimum and maximum.
 */
static void dm_mst_test_dsc_link_bpp_between_limits(struct kunit *test)
{
	struct dsc_mst_fairness_vars vars[MAX_PIPES] = {};
	struct dm_mst_test_dsc_link_ctx *ctx;

	ctx = dm_mst_test_alloc_dsc_link_ctx(test, 63);
	/* Smaller slots leave the fair share below the full slack. */
	ctx->mst_state->pbn_div.full = dfixed_const(4);

	KUNIT_EXPECT_EQ(test, pre_compute_mst_dsc_configs_for_state(ctx->state, ctx->dc_state, vars), 0);
	KUNIT_EXPECT_TRUE(test, vars[0].dsc_enabled);
	KUNIT_EXPECT_GT(test, vars[0].bpp_x16, 8 * 16);
	KUNIT_EXPECT_LT(test, vars[0].bpp_x16, 16 * 16);
}

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
	KUNIT_CASE(dm_mst_test_aux_transfer_oversized),
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
	KUNIT_CASE(dm_mst_test_sideband_msg_ready_acks_down_rep),
	KUNIT_CASE(dm_mst_test_sideband_msg_ready_ack_write_fails),
	KUNIT_CASE(dm_mst_test_down_rep_msg_ready_wrapper),
	/* amdgpu_dm_initialize_dp_connector tests */
	KUNIT_CASE(dm_mst_test_initialize_dp_connector_edp),
	KUNIT_CASE(dm_mst_test_initialize_dp_connector_mst),
	/* dm_mst_atomic_best_encoder tests */
	KUNIT_CASE(dm_mst_test_atomic_best_encoder),
	/* dm_dp_create_fake_mst_encoders tests */
	KUNIT_CASE(dm_mst_test_create_fake_mst_encoders),
	/* dm_dp_add_mst_connector tests */
	KUNIT_CASE(dm_mst_test_add_mst_connector_creates),
	KUNIT_CASE(dm_mst_test_add_mst_connector_inherits_props),
	KUNIT_CASE(dm_mst_test_add_mst_connector_init_fails),
	/* dm_dp_mst_atomic_check tests */
	KUNIT_CASE(dm_mst_test_atomic_check_no_old_crtc),
	/* dm_dp_mst_detect tests */
	KUNIT_CASE(dm_mst_test_detect_unregistered),
	KUNIT_CASE(dm_mst_test_detect_reads_port_dpcd_rev),
	KUNIT_CASE(dm_mst_test_detect_unknown_dpcd_rev),
	KUNIT_CASE(dm_mst_test_detect_dpcd_read_error),
	KUNIT_CASE(dm_mst_test_detect_disconnect_releases_sink),
	/* dm_dp_mst_get_modes tests */
	KUNIT_CASE(dm_mst_test_get_modes_no_edid_adds_default_sink),
	KUNIT_CASE(dm_mst_test_get_modes_no_edid_sink_alloc_fails),
	KUNIT_CASE(dm_mst_test_get_modes_no_edid_keeps_existing_sink),
	KUNIT_CASE(dm_mst_test_get_modes_cached_edid_replaces_virtual_sink),
	KUNIT_CASE(dm_mst_test_get_modes_cached_edid_sink_alloc_fails),
	KUNIT_CASE(dm_mst_test_get_modes_restores_hdcp_properties),
	KUNIT_CASE(dm_mst_test_get_modes_reads_remote_edid),
	/* amdgpu_dm_mst_connector_late_register tests */
	KUNIT_CASE(dm_mst_test_connector_late_register),
	/* amdgpu_dm_mst_connector_early_unregister tests */
	KUNIT_CASE(dm_mst_test_connector_early_unregister_no_sink),
	KUNIT_CASE(dm_mst_test_connector_early_unregister_releases_sink),
	/* dm_dp_mst_connector_destroy tests */
	KUNIT_CASE(dm_mst_test_connector_destroy_no_sink),
	KUNIT_CASE(dm_mst_test_connector_destroy_releases_sink),
	/* dp_get_link_current_set_bw tests */
	KUNIT_CASE_PARAM(dm_mst_test_link_current_set_bw, dm_mst_link_bw_gen_params),
	/* is_synaptics_cascaded_panamera tests */
	KUNIT_CASE_PARAM(dm_mst_test_synaptics_cascaded, dm_mst_panamera_gen_params),
	/* validate_dsc_caps_on_connector tests */
	KUNIT_CASE(dm_mst_test_validate_dsc_caps_no_aux),
	KUNIT_CASE(dm_mst_test_validate_dsc_caps_aux_workaround),
	KUNIT_CASE(dm_mst_test_validate_dsc_caps_cascaded_hub),
	KUNIT_CASE(dm_mst_test_validate_dsc_caps_read_error),
	KUNIT_CASE(dm_mst_test_validate_dsc_caps_unsupported),
	/* dm_dp_mst_is_port_support_mode tests */
	KUNIT_CASE(dm_mst_test_port_mode_fits_without_dsc),
	KUNIT_CASE(dm_mst_test_port_mode_no_dsc_aux),
	KUNIT_CASE(dm_mst_test_port_mode_no_common_dsc_config),
	KUNIT_CASE(dm_mst_test_port_mode_dsc_passthrough_fits),
	KUNIT_CASE(dm_mst_test_port_mode_dsc_passthrough_too_narrow),
	KUNIT_CASE(dm_mst_test_port_mode_last_link_too_slow),
	KUNIT_CASE(dm_mst_test_port_mode_last_link_cached_bw),
	KUNIT_CASE(dm_mst_test_port_mode_last_link_synaptics_quirk),
	KUNIT_CASE(dm_mst_test_port_mode_upstream_vc_too_small),
	KUNIT_CASE(dm_mst_test_port_mode_branch_throughput_exceeded),
	/* get_conv_frl_bw tests */
	KUNIT_CASE(dm_mst_test_conv_frl_bw_no_pcon_support),
	KUNIT_CASE(dm_mst_test_conv_frl_bw_not_hdmi_port),
	KUNIT_CASE(dm_mst_test_conv_frl_bw_sink_without_frl),
	KUNIT_CASE(dm_mst_test_conv_frl_bw_bottleneck),
	/* log_dsc_params tests */
	KUNIT_CASE(dm_mst_test_log_dsc_params),
	/* find_crtc_index_in_state_by_stream tests */
	KUNIT_CASE(dm_mst_test_find_crtc_index_matches),
	KUNIT_CASE(dm_mst_test_find_crtc_index_no_match),
	/* is_dsc_precompute_needed tests */
	KUNIT_CASE_PARAM(dm_mst_test_dsc_precompute_needed, dm_mst_precompute_gen_params),
	/* is_dsc_need_re_compute tests */
	KUNIT_CASE(dm_mst_test_recompute_not_mst_branch),
	KUNIT_CASE(dm_mst_test_recompute_legacy_hub_without_dsc),
	KUNIT_CASE(dm_mst_test_recompute_no_stream_on_link),
	KUNIT_CASE(dm_mst_test_recompute_on_mode_change),
	KUNIT_CASE(dm_mst_test_recompute_unchanged_stream),
	KUNIT_CASE(dm_mst_test_recompute_stream_removed),
	KUNIT_CASE(dm_mst_test_recompute_stream_without_connector),
	KUNIT_CASE(dm_mst_test_recompute_connector_without_crtc),
	/* compute_mst_dsc_configs_for_state tests */
	KUNIT_CASE(dm_mst_test_compute_configs_skips_sst),
	KUNIT_CASE(dm_mst_test_compute_configs_skips_incomplete),
	KUNIT_CASE(dm_mst_test_compute_configs_remove_stream_fails),
	KUNIT_CASE(dm_mst_test_compute_configs_no_recompute),
	/* pre_validate_dsc tests */
	KUNIT_CASE(dm_mst_test_pre_validate_dsc_not_needed),
	/* pre_compute_mst_dsc_configs_for_state tests */
	KUNIT_CASE(dm_mst_test_pre_compute_configs_skips_sst),
	KUNIT_CASE(dm_mst_test_pre_compute_configs_skips_incomplete),
	KUNIT_CASE(dm_mst_test_pre_compute_configs_no_recompute),
	/* compute_mst_dsc_configs_for_link tests */
	KUNIT_CASE(dm_mst_test_dsc_link_fits_uncompressed),
	KUNIT_CASE(dm_mst_test_dsc_link_enables_compression),
	KUNIT_CASE(dm_mst_test_dsc_link_out_of_slots),
	KUNIT_CASE(dm_mst_test_dsc_link_forced_dsc),
	KUNIT_CASE(dm_mst_test_dsc_link_forced_dsc_stays_on),
	KUNIT_CASE(dm_mst_test_dsc_link_bpp_overwrite),
	KUNIT_CASE(dm_mst_test_dsc_link_compression_disabled),
	KUNIT_CASE(dm_mst_test_dsc_link_bpp_between_limits),
	{}
};

static struct kunit_suite dm_mst_types_test_suite = {
	.name = "amdgpu_dm_mst_types",
	.test_cases = dm_mst_types_test_cases,
};

kunit_test_suite(dm_mst_types_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_mst_types");
