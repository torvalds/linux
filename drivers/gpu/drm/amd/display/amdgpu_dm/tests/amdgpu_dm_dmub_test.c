// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_dmub.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <linux/firmware.h>

#include "dc.h"
#include "dc/inc/core_types.h"
#include "dc/inc/hw/dmcu.h"
#include "dc/inc/hw/abm.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "dm_services.h"
#include "dmub/dmub_srv.h"
#include "amdgpu_dm_dmub.h"

#define DM_TEST_FW_SIZE	512

/* Tests for dm_register_dmub_notify_callback() */

static void dummy_callback(struct amdgpu_device *adev,
			   struct dmub_notification *notify)
{
}

static bool dm_test_dmub_supported(struct dmub_srv *dmub)
{
	return true;
}

static bool dm_test_dmub_unsupported(struct dmub_srv *dmub)
{
	return false;
}

static bool dm_test_dmub_hw_initialized(struct dmub_srv *dmub)
{
	return true;
}

static union dmub_fw_boot_status dm_test_dmub_fw_ready(struct dmub_srv *dmub)
{
	union dmub_fw_boot_status status = { 0 };

	status.bits.dal_fw = 1;
	status.bits.mailbox_rdy = 1;
	return status;
}

static union dmub_fw_boot_status dm_test_dmub_fw_not_ready(struct dmub_srv *dmub)
{
	union dmub_fw_boot_status status = { 0 };

	return status;
}

static void dm_test_dmub_init_reg_offsets(struct dmub_srv *dmub,
					  struct dc_context *ctx)
{
}

static bool dm_test_dmcu_init(struct dmcu *dmcu)
{
	return true;
}

static bool dm_test_dmcu_is_initialized(struct dmcu *dmcu)
{
	return true;
}

static const struct dmcu_funcs dm_test_dmcu_funcs = {
	.dmcu_init = dm_test_dmcu_init,
	.is_dmcu_initialized = dm_test_dmcu_is_initialized,
};

static struct dmub_srv *dm_test_alloc_dmub_srv(struct kunit *test)
{
	struct dmub_srv *dmub_srv;

	dmub_srv = kunit_kzalloc(test, sizeof(*dmub_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dmub_srv);

	dmub_srv->sw_init = true;
	dmub_srv->hw_init = true;
	dmub_srv->power_state = DMUB_POWER_STATE_D0;
	dmub_srv->hw_funcs.is_supported = dm_test_dmub_supported;
	dmub_srv->hw_funcs.is_hw_init = dm_test_dmub_hw_initialized;
	dmub_srv->hw_funcs.get_fw_status = dm_test_dmub_fw_ready;
	dmub_srv->hw_funcs.init_reg_offsets = dm_test_dmub_init_reg_offsets;

	return dmub_srv;
}

static const struct firmware *dm_test_alloc_dmub_fw(struct kunit *test)
{
	struct dmcub_firmware_header_v1_0 *hdr;
	struct firmware *fw;
	u8 *data;

	fw = kunit_kzalloc(test, sizeof(*fw), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fw);

	data = kunit_kzalloc(test, DM_TEST_FW_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, data);

	hdr = (struct dmcub_firmware_header_v1_0 *)data;
	hdr->header.ucode_array_offset_bytes = cpu_to_le32(0);
	hdr->header.ucode_version = cpu_to_le32(DMUB_FW_VERSION(9, 9, 9));
	hdr->inst_const_bytes = cpu_to_le32(PSP_HEADER_BYTES_256);
	hdr->bss_data_bytes = cpu_to_le32(0);

	fw->size = DM_TEST_FW_SIZE;
	fw->data = data;

	return fw;
}

/**
 * dm_test_register_dmub_notify_callback_null_callback - Test null callback is rejected
 * @test: The KUnit test context
 */
static void dm_test_register_dmub_notify_callback_null_callback(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_EXPECT_FALSE(test, dm_register_dmub_notify_callback(adev,
		DMUB_NOTIFICATION_AUX_REPLY, NULL, false));
}

/**
 * dm_test_register_dmub_notify_callback_type_out_of_range - Test out-of-range type is rejected
 * @test: The KUnit test context
 */
static void dm_test_register_dmub_notify_callback_type_out_of_range(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_EXPECT_FALSE(test, dm_register_dmub_notify_callback(adev,
		AMDGPU_DMUB_NOTIFICATION_MAX, dummy_callback, false));
}

/**
 * dm_test_register_dmub_notify_callback_valid - Test Register dmub notify callback valid
 * @test: The KUnit test context
 */
static void dm_test_register_dmub_notify_callback_valid(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_EXPECT_TRUE(test, dm_register_dmub_notify_callback(adev,
		DMUB_NOTIFICATION_AUX_REPLY, dummy_callback, true));

	KUNIT_EXPECT_TRUE(test,
		adev->dm.dmub_callback[DMUB_NOTIFICATION_AUX_REPLY] == dummy_callback);
	KUNIT_EXPECT_TRUE(test,
		adev->dm.dmub_thread_offload[DMUB_NOTIFICATION_AUX_REPLY]);
}

/**
 * dm_test_register_dmub_notify_callback_offload_false - Test registration with offload disabled
 * @test: The KUnit test context
 */
static void dm_test_register_dmub_notify_callback_offload_false(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	KUNIT_EXPECT_TRUE(test, dm_register_dmub_notify_callback(adev,
		DMUB_NOTIFICATION_HPD, dummy_callback, false));

	KUNIT_EXPECT_TRUE(test,
		adev->dm.dmub_callback[DMUB_NOTIFICATION_HPD] == dummy_callback);
	KUNIT_EXPECT_FALSE(test,
		adev->dm.dmub_thread_offload[DMUB_NOTIFICATION_HPD]);
}

/* Tests for dm_dmub_aux_setconfig_callback() */

/**
 * dm_test_dmub_aux_setconfig_callback_copies_and_completes - Test copy and complete on AUX reply
 * @test: The KUnit test context
 */
static void dm_test_dmub_aux_setconfig_callback_copies_and_completes(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dmub_notification *dm_notify;
	struct dmub_notification notify = {};

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	dm_notify = kunit_kzalloc(test, sizeof(*dm_notify), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_notify);

	init_completion(&adev->dm.dmub_aux_transfer_done);
	adev->dm.dmub_notify = dm_notify;

	notify.type = DMUB_NOTIFICATION_AUX_REPLY;
	notify.result = AUX_RET_SUCCESS;
	notify.aux_reply.command = 0xA5;
	notify.aux_reply.length = 3;
	notify.aux_reply.data[0] = 0x11;
	notify.aux_reply.data[1] = 0x22;
	notify.aux_reply.data[2] = 0x33;

	dm_dmub_aux_setconfig_callback(adev, &notify);

	KUNIT_EXPECT_EQ(test, dm_notify->type, notify.type);
	KUNIT_EXPECT_EQ(test, dm_notify->result, notify.result);
	KUNIT_EXPECT_EQ(test, dm_notify->aux_reply.command, notify.aux_reply.command);
	KUNIT_EXPECT_EQ(test, dm_notify->aux_reply.length, notify.aux_reply.length);
	KUNIT_EXPECT_EQ(test, dm_notify->aux_reply.data[0], notify.aux_reply.data[0]);
	KUNIT_EXPECT_EQ(test, dm_notify->aux_reply.data[1], notify.aux_reply.data[1]);
	KUNIT_EXPECT_EQ(test, dm_notify->aux_reply.data[2], notify.aux_reply.data[2]);
	KUNIT_EXPECT_TRUE(test, completion_done(&adev->dm.dmub_aux_transfer_done));
}

/**
 * dm_test_dmub_aux_setconfig_callback_non_aux_no_complete - Test non-AUX type skips completion
 * @test: The KUnit test context
 */
static void dm_test_dmub_aux_setconfig_callback_non_aux_no_complete(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dmub_notification *dm_notify;
	struct dmub_notification notify = {};

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	dm_notify = kunit_kzalloc(test, sizeof(*dm_notify), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_notify);

	init_completion(&adev->dm.dmub_aux_transfer_done);
	adev->dm.dmub_notify = dm_notify;

	notify.type = DMUB_NOTIFICATION_HPD;
	notify.result = AUX_RET_ERROR_TIMEOUT;

	dm_dmub_aux_setconfig_callback(adev, &notify);

	KUNIT_EXPECT_EQ(test, dm_notify->type, notify.type);
	KUNIT_EXPECT_FALSE(test, completion_done(&adev->dm.dmub_aux_transfer_done));
}

/**
 * dm_test_dmub_aux_setconfig_callback_aux_with_null_dm_notify - Test AUX with NULL dm_notify
 * @test: The KUnit test context
 */
static void dm_test_dmub_aux_setconfig_callback_aux_with_null_dm_notify(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dmub_notification notify = {};

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	init_completion(&adev->dm.dmub_aux_transfer_done);
	adev->dm.dmub_notify = NULL;

	notify.type = DMUB_NOTIFICATION_AUX_REPLY;

	dm_dmub_aux_setconfig_callback(adev, &notify);

	KUNIT_EXPECT_TRUE(test, completion_done(&adev->dm.dmub_aux_transfer_done));
}

/**
 * dm_test_dmub_aux_setconfig_callback_set_config_reply - Test SET_CONFIG reply copies status
 * @test: The KUnit test context
 */
static void dm_test_dmub_aux_setconfig_callback_set_config_reply(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dmub_notification *dm_notify;
	struct dmub_notification notify = {};

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	dm_notify = kunit_kzalloc(test, sizeof(*dm_notify), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dm_notify);

	init_completion(&adev->dm.dmub_aux_transfer_done);
	adev->dm.dmub_notify = dm_notify;

	notify.type = DMUB_NOTIFICATION_SET_CONFIG_REPLY;
	notify.sc_status = SET_CONFIG_RX_TIMEOUT;

	dm_dmub_aux_setconfig_callback(adev, &notify);

	KUNIT_EXPECT_EQ(test, dm_notify->type, notify.type);
	KUNIT_EXPECT_EQ(test, dm_notify->sc_status, notify.sc_status);
	KUNIT_EXPECT_FALSE(test, completion_done(&adev->dm.dmub_aux_transfer_done));
}

/* Tests for dm_dmub_aux_fused_io_callback() */

/**
 * dm_test_dmub_aux_fused_io_callback_copies_reply_and_completes - Test copy and complete
 * @test: The KUnit test context
 */
static void dm_test_dmub_aux_fused_io_callback_copies_reply_and_completes(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dmub_notification notify = {};
	struct dmub_cmd_fused_request *reply;
	u32 reply_ddc_line;
	u32 notify_ddc_line;
	u32 reply_address;
	u32 notify_address;
	u32 reply_length;
	u32 notify_length;
	uint8_t ddc_line = 2;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	init_completion(&adev->dm.fused_io[ddc_line].replied);

	notify.fused_request.identifier = 0x34;
	notify.fused_request.status = FUSED_REQUEST_STATUS_SUCCESS;
	notify.fused_request.u.aux.ddc_line = ddc_line;
	notify.fused_request.u.aux.address = 0x50;
	notify.fused_request.u.aux.length = 4;

	dm_dmub_aux_fused_io_callback(adev, &notify);

	KUNIT_EXPECT_TRUE(test, completion_done(&adev->dm.fused_io[ddc_line].replied));

	reply = (struct dmub_cmd_fused_request *)adev->dm.fused_io[ddc_line].reply_data;
	reply_ddc_line = reply->u.aux.ddc_line;
	notify_ddc_line = notify.fused_request.u.aux.ddc_line;
	reply_address = reply->u.aux.address;
	notify_address = notify.fused_request.u.aux.address;
	reply_length = reply->u.aux.length;
	notify_length = notify.fused_request.u.aux.length;

	KUNIT_EXPECT_EQ(test, reply->identifier, notify.fused_request.identifier);
	KUNIT_EXPECT_EQ(test, reply->status, notify.fused_request.status);
	KUNIT_EXPECT_EQ(test, reply_ddc_line, notify_ddc_line);
	KUNIT_EXPECT_EQ(test, reply_address, notify_address);
	KUNIT_EXPECT_EQ(test, reply_length, notify_length);
}

/**
 * dm_test_dmub_aux_fused_io_callback_max_ddc_line - Test Dmub aux fused io callback max ddc line
 * @test: The KUnit test context
 */
static void dm_test_dmub_aux_fused_io_callback_max_ddc_line(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dmub_notification notify = {};
	struct dmub_cmd_fused_request *reply;
	u32 reply_ddc_line;
	u32 notify_ddc_line;
	uint8_t ddc_line;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	ddc_line = ARRAY_SIZE(adev->dm.fused_io) - 1;
	init_completion(&adev->dm.fused_io[ddc_line].replied);

	notify.fused_request.identifier = 0x56;
	notify.fused_request.status = FUSED_REQUEST_STATUS_SUCCESS;
	notify.fused_request.u.aux.ddc_line = ddc_line;
	notify.fused_request.u.aux.address = 0x50;
	notify.fused_request.u.aux.length = 1;

	dm_dmub_aux_fused_io_callback(adev, &notify);

	KUNIT_EXPECT_TRUE(test, completion_done(&adev->dm.fused_io[ddc_line].replied));

	reply = (struct dmub_cmd_fused_request *)adev->dm.fused_io[ddc_line].reply_data;
	reply_ddc_line = reply->u.aux.ddc_line;
	notify_ddc_line = notify.fused_request.u.aux.ddc_line;

	KUNIT_EXPECT_EQ(test, reply->identifier, notify.fused_request.identifier);
	KUNIT_EXPECT_EQ(test, reply_ddc_line, notify_ddc_line);
}

/**
 * dm_test_dmub_aux_fused_io_callback_null_args - Test the NULL-argument guard
 * @test: The KUnit test context
 *
 * Passing a NULL device triggers the defensive guard (an ASSERT that maps to
 * WARN_ON_ONCE in this build) and returns early without dereferencing the
 * arguments. The call must not crash.
 */
static void dm_test_dmub_aux_fused_io_callback_null_args(struct kunit *test)
{
	struct dmub_notification notify = {};

	/* Must not crash; guard hits ASSERT (WARN_ON_ONCE) and returns. */
	dm_dmub_aux_fused_io_callback(NULL, &notify);
}

/* Tests for dm_get_default_ips_mode() */

/**
 * dm_test_get_default_ips_mode_dcn35 - Test Get default ips mode dcn35
 * @test: The KUnit test context
 */
static void dm_test_get_default_ips_mode_dcn35(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 5, 0);

	KUNIT_EXPECT_EQ(test, dm_get_default_ips_mode(adev),
			DMUB_IPS_RCG_IN_ACTIVE_IPS2_IN_OFF);
}

/**
 * dm_test_get_default_ips_mode_dcn351 - Test Get default ips mode dcn351
 * @test: The KUnit test context
 */
static void dm_test_get_default_ips_mode_dcn351(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 5, 1);

	KUNIT_EXPECT_EQ(test, dm_get_default_ips_mode(adev),
			DMUB_IPS_RCG_IN_ACTIVE_IPS2_IN_OFF);
}

/**
 * dm_test_get_default_ips_mode_dcn36 - Test Get default ips mode dcn36
 * @test: The KUnit test context
 */
static void dm_test_get_default_ips_mode_dcn36(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 6, 0);

	KUNIT_EXPECT_EQ(test, dm_get_default_ips_mode(adev),
			DMUB_IPS_RCG_IN_ACTIVE_IPS2_IN_OFF);
}

/**
 * dm_test_get_default_ips_mode_older_than_dcn35 - Test Get default ips mode older than dcn35
 * @test: The KUnit test context
 */
static void dm_test_get_default_ips_mode_older_than_dcn35(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 2, 0);

	KUNIT_EXPECT_EQ(test, dm_get_default_ips_mode(adev),
			DMUB_IPS_DISABLE_ALL);
}

/**
 * dm_test_get_default_ips_mode_newer_default - Test Get default ips mode newer default
 * @test: The KUnit test context
 */
static void dm_test_get_default_ips_mode_newer_default(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	/* DCN 4.0.1 is >= 3.5 but has no explicit case, returns ENABLE */
	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(4, 0, 1);

	KUNIT_EXPECT_EQ(test, dm_get_default_ips_mode(adev),
			DMUB_IPS_ENABLE);
}

/* Tests for dm_dmub_hw_init() */

/*
 * Build an amdgpu_device with the minimal dc/res_pool pointers that
 * dm_dmub_hw_init() and dm_dmub_hw_resume() dereference before their
 * early-return checks.
 */
static struct amdgpu_device *dm_test_alloc_adev_with_dc(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc *dc;
	struct resource_pool *res_pool;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);

	res_pool = kunit_kzalloc(test, sizeof(*res_pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, res_pool);

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	dc->res_pool = res_pool;
	dc->ctx = ctx;
	ctx->dc = dc;
	ctx->driver_context = adev;
	adev->dm.dc = dc;

	return adev;
}

static struct amdgpu_device *dm_test_alloc_adev_with_dmub(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dmub_srv_fb_info *fb_info;
	int i;

	adev = dm_test_alloc_adev_with_dc(test);

	fb_info = kunit_kzalloc(test, sizeof(*fb_info), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fb_info);

	fb_info->num_fb = DMUB_WINDOW_TOTAL;
	for (i = 0; i < DMUB_WINDOW_TOTAL; i++) {
		fb_info->fb[i].size = PAGE_SIZE;
		fb_info->fb[i].cpu_addr = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fb_info->fb[i].cpu_addr);
	}

	adev->dm.dmub_srv = dm_test_alloc_dmub_srv(test);
	adev->dm.dmub_fb_info = fb_info;
	adev->dm.dmub_fw = dm_test_alloc_dmub_fw(test);
	adev->bios = kunit_kzalloc(test, 4, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev->bios);
	adev->bios_size = 4;
	adev->dm.fw_inst_size = 0;

	return adev;
}

/**
 * dm_test_dmub_hw_init_no_dmub_srv - Test hw init returns 0 when DMUB unsupported
 * @test: The KUnit test context
 *
 * When adev->dm.dmub_srv is NULL the ASIC does not support DMUB and
 * dm_dmub_hw_init() should return 0 without touching the hardware.
 */
static void dm_test_dmub_hw_init_no_dmub_srv(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dc(test);

	adev->dm.dmub_srv = NULL;

	KUNIT_EXPECT_EQ(test, dm_dmub_hw_init(adev), 0);
}

/**
 * dm_test_dmub_hw_init_no_fb_info - Test hw init fails without framebuffer info
 * @test: The KUnit test context
 *
 * With a DMUB service present but no framebuffer info, dm_dmub_hw_init()
 * should return -EINVAL.
 */
static void dm_test_dmub_hw_init_no_fb_info(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dc(test);
	struct dmub_srv *dmub_srv;

	dmub_srv = kunit_kzalloc(test, sizeof(*dmub_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dmub_srv);

	adev->dm.dmub_srv = dmub_srv;
	adev->dm.dmub_fb_info = NULL;

	KUNIT_EXPECT_EQ(test, dm_dmub_hw_init(adev), -EINVAL);
}

/**
 * dm_test_dmub_hw_init_no_firmware - Test hw init fails without firmware
 * @test: The KUnit test context
 *
 * With a DMUB service and framebuffer info present but no firmware,
 * dm_dmub_hw_init() should return -EINVAL.
 */
static void dm_test_dmub_hw_init_no_firmware(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dc(test);
	struct dmub_srv *dmub_srv;
	struct dmub_srv_fb_info *fb_info;

	dmub_srv = kunit_kzalloc(test, sizeof(*dmub_srv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dmub_srv);

	fb_info = kunit_kzalloc(test, sizeof(*fb_info), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fb_info);

	adev->dm.dmub_srv = dmub_srv;
	adev->dm.dmub_fb_info = fb_info;
	adev->dm.dmub_fw = NULL;

	KUNIT_EXPECT_EQ(test, dm_dmub_hw_init(adev), -EINVAL);
}

/**
 * dm_test_dmub_hw_init_success_fake_dmub - Test hw init with a fake DMUB service
 * @test: The KUnit test context
 *
 * With fake DMUB callbacks and preallocated framebuffer windows, the init path
 * should reach DMUB service initialization without real register access.
 */
static void dm_test_dmub_hw_init_success_fake_dmub(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dmub(test);

	KUNIT_EXPECT_EQ(test, dm_dmub_hw_init(adev), 0);
	KUNIT_EXPECT_TRUE(test, adev->dm.dmub_srv->hw_init);
	KUNIT_EXPECT_NOT_NULL(test, adev->dm.dc->ctx->dmub_srv);
}

/**
 * dm_test_dmub_hw_init_no_hw_support - Test hw init returns 0 when HW is unsupported
 * @test: The KUnit test context
 *
 * When the DMUB service reports no hardware support, dm_dmub_hw_init() should
 * log and return 0 without initializing the DMUB hardware.
 */
static void dm_test_dmub_hw_init_no_hw_support(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dmub(test);

	adev->dm.dmub_srv->hw_funcs.is_supported = dm_test_dmub_unsupported;

	KUNIT_EXPECT_EQ(test, dm_dmub_hw_init(adev), 0);
	KUNIT_EXPECT_NULL(test, adev->dm.dc->ctx->dmub_srv);
}

/**
 * dm_test_dmub_hw_init_bss_data - Test hw init copies BSS data into FB memory
 * @test: The KUnit test context
 *
 * When the DMUB firmware declares a non-zero BSS data size, dm_dmub_hw_init()
 * should copy that region into the BSS framebuffer window.
 */
static void dm_test_dmub_hw_init_bss_data(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dmub(test);
	struct dmcub_firmware_header_v1_0 *hdr;

	hdr = (struct dmcub_firmware_header_v1_0 *)adev->dm.dmub_fw->data;
	hdr->bss_data_bytes = cpu_to_le32(16);

	KUNIT_EXPECT_EQ(test, dm_dmub_hw_init(adev), 0);
	KUNIT_EXPECT_TRUE(test, adev->dm.dmub_srv->hw_init);
}

/**
 * dm_test_dmub_hw_init_hw_init_fails - Test hw init returns -EINVAL on DMUB init failure
 * @test: The KUnit test context
 *
 * A framebuffer-info window count below the required total makes
 * dmub_srv_hw_init() reject the request, so dm_dmub_hw_init() logs and
 * returns -EINVAL. (The rejection path emits a one-time WARN via ASSERT.)
 */
static void dm_test_dmub_hw_init_hw_init_fails(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dmub(test);

	adev->dm.dmub_fb_info->num_fb = 0;

	KUNIT_EXPECT_EQ(test, dm_dmub_hw_init(adev), -EINVAL);
}

/**
 * dm_test_dmub_hw_init_auto_load_timeout - Test hw init tolerates an auto-load timeout
 * @test: The KUnit test context
 *
 * When the DMUB firmware never reports ready, dmub_srv_wait_for_auto_load()
 * times out; dm_dmub_hw_init() only warns and still completes successfully.
 */
static void dm_test_dmub_hw_init_auto_load_timeout(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dmub(test);

	adev->dm.dmub_srv->hw_funcs.get_fw_status = dm_test_dmub_fw_not_ready;

	KUNIT_EXPECT_EQ(test, dm_dmub_hw_init(adev), 0);
	KUNIT_EXPECT_NOT_NULL(test, adev->dm.dc->ctx->dmub_srv);
}

/**
 * dm_test_dmub_hw_init_apu_dpia_dcn35 - Test hw init APU DPIA and DCN35 hw params
 * @test: The KUnit test context
 *
 * On a DCN3.5 APU with a USB4 DPIA link, dm_dmub_hw_init() should populate the
 * DPIA hw params and the DCN3.5 IPS-sequential hw params before initializing
 * the fake DMUB service.
 */
static void dm_test_dmub_hw_init_apu_dpia_dcn35(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dmub(test);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 5, 0);
	adev->dm.dc->caps.is_apu = true;
	adev->dm.dc->res_pool->usb4_dpia_count = 1;

	KUNIT_EXPECT_EQ(test, dm_dmub_hw_init(adev), 0);
	KUNIT_EXPECT_TRUE(test, adev->dm.dmub_srv->hw_init);
}

/**
 * dm_test_dmub_hw_init_sanity_checks_dcn31 - Test hw init enables DCN31 sanity checks
 * @test: The KUnit test context
 *
 * On DCN3.1.2 with a DMCUB firmware version in the affected range,
 * dm_dmub_hw_init() should enable the DC sanity-check debug flag.
 */
static void dm_test_dmub_hw_init_sanity_checks_dcn31(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dmub(test);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 2);
	adev->dm.dmcub_fw_version = DMUB_FW_VERSION(4, 0, 10);

	KUNIT_EXPECT_EQ(test, dm_dmub_hw_init(adev), 0);
	KUNIT_EXPECT_TRUE(test, adev->dm.dc->debug.sanity_checks);
}

/**
 * dm_test_dmub_hw_init_sanity_checks_dcn314 - Test hw init enables DCN314 sanity checks
 * @test: The KUnit test context
 *
 * On DCN3.1.4 with a DMCUB firmware version in the affected range,
 * dm_dmub_hw_init() should enable the DC sanity-check debug flag.
 */
static void dm_test_dmub_hw_init_sanity_checks_dcn314(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dmub(test);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 1, 4);
	adev->dm.dmcub_fw_version = DMUB_FW_VERSION(4, 0, 10);

	KUNIT_EXPECT_EQ(test, dm_dmub_hw_init(adev), 0);
	KUNIT_EXPECT_TRUE(test, adev->dm.dc->debug.sanity_checks);
}

/**
 * dm_test_dmub_hw_init_dmcu_abm - Test hw init initializes DMCU and ABM when present
 * @test: The KUnit test context
 *
 * When the resource pool exposes a DMCU and ABM, dm_dmub_hw_init() should
 * program the PSP version, invoke the DMCU init callback, and record the
 * running state reported by the DMCU.
 */
static void dm_test_dmub_hw_init_dmcu_abm(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dmub(test);
	struct dmcu *dmcu;
	struct abm *abm;

	dmcu = kunit_kzalloc(test, sizeof(*dmcu), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dmcu);

	abm = kunit_kzalloc(test, sizeof(*abm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, abm);

	dmcu->funcs = &dm_test_dmcu_funcs;
	dmcu->psp_version = 0x12345678;
	adev->dm.dc->res_pool->dmcu = dmcu;
	adev->dm.dc->res_pool->abm = abm;

	KUNIT_EXPECT_EQ(test, dm_dmub_hw_init(adev), 0);
	KUNIT_EXPECT_TRUE(test, abm->dmcu_is_running);
}

/* Tests for dm_dmub_hw_resume() */

/**
 * dm_test_dmub_hw_resume_no_dmub_srv - Test hw resume is a no-op when DMUB unsupported
 * @test: The KUnit test context
 *
 * When adev->dm.dmub_srv is NULL, dm_dmub_hw_resume() should return early
 * without dereferencing the (absent) DMUB service.
 */
static void dm_test_dmub_hw_resume_no_dmub_srv(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dc(test);

	adev->dm.dmub_srv = NULL;

	/* Must not crash. */
	dm_dmub_hw_resume(adev);
}

/**
 * dm_test_dmub_hw_resume_initialized_dmub - Test resume waits for initialized DMUB
 * @test: The KUnit test context
 *
 * When the fake DMUB service reports hardware already initialized, resume
 * should only wait for firmware readiness and skip full reinitialization.
 */
static void dm_test_dmub_hw_resume_initialized_dmub(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dc(test);

	adev->dm.dmub_srv = dm_test_alloc_dmub_srv(test);

	/* Must not crash. */
	dm_dmub_hw_resume(adev);
}

/**
 * dm_test_dmub_hw_resume_full_init - Test resume performs full init when uninitialized
 * @test: The KUnit test context
 *
 * When the fake DMUB service reports hardware not yet initialized, resume
 * should continue into a full dm_dmub_hw_init() and create the DC DMUB
 * server.
 */
static void dm_test_dmub_hw_resume_full_init(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dmub(test);

	adev->dm.dmub_srv->hw_init = false;

	dm_dmub_hw_resume(adev);

	KUNIT_EXPECT_NOT_NULL(test, adev->dm.dc->ctx->dmub_srv);
}

/**
 * dm_test_dmub_hw_resume_init_check_failed - Test resume handles a failed init check
 * @test: The KUnit test context
 *
 * When the DMUB service is not software-initialized, the init-state query
 * fails and resume continues into dm_dmub_hw_init(), which also fails; the
 * call must warn and return without crashing.
 */
static void dm_test_dmub_hw_resume_init_check_failed(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dmub(test);

	adev->dm.dmub_srv->sw_init = false;

	/* Must not crash. */
	dm_dmub_hw_resume(adev);
}

/**
 * dm_test_dmub_hw_resume_auto_load_timeout - Test resume tolerates an auto-load timeout
 * @test: The KUnit test context
 *
 * When the DMUB reports hardware already initialized but the firmware never
 * signals ready, resume's auto-load wait times out and only warns; the call
 * must not crash.
 */
static void dm_test_dmub_hw_resume_auto_load_timeout(struct kunit *test)
{
	struct amdgpu_device *adev = dm_test_alloc_adev_with_dc(test);

	adev->dm.dmub_srv = dm_test_alloc_dmub_srv(test);
	adev->dm.dmub_srv->hw_funcs.get_fw_status = dm_test_dmub_fw_not_ready;

	/* Must not crash; auto-load times out and only warns. */
	dm_dmub_hw_resume(adev);
}

/* Tests for dm_dmub_sw_init() */

/**
 * dm_test_dmub_sw_init_unsupported_asic - Test sw init returns 0 for unsupported ASIC
 * @test: The KUnit test context
 *
 * For an IP version with no DMUB support, dm_dmub_sw_init() should return 0
 * before attempting to access the firmware.
 */
static void dm_test_dmub_sw_init_unsupported_asic(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(1, 0, 0);

	KUNIT_EXPECT_EQ(test, dm_dmub_sw_init(adev), 0);
}

/* Tests for dm_init_microcode() */

/**
 * dm_test_init_microcode_unsupported_asic - Test microcode init returns 0 for unsupported ASIC
 * @test: The KUnit test context
 *
 * For an IP version with no DMUB support, dm_init_microcode() should return 0
 * without requesting any firmware.
 */
static void dm_test_init_microcode_unsupported_asic(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(1, 0, 0);

	KUNIT_EXPECT_EQ(test, dm_init_microcode(adev), 0);
}

/* Tests for dm_dmub_get_vbios_bounding_box() */

/**
 * dm_test_dmub_get_vbios_bounding_box_default_null - Test default IP version returns NULL
 * @test: The KUnit test context
 *
 * For an IP version without a bounding-box size mapping, the switch falls
 * through to the default case and dm_dmub_get_vbios_bounding_box() returns
 * NULL without allocating GPU memory or issuing GPINT commands.
 */
static void dm_test_dmub_get_vbios_bounding_box_default_null(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->ip_versions[DCE_HWIP][0] = IP_VERSION(3, 5, 0);

	KUNIT_EXPECT_NULL(test, dm_dmub_get_vbios_bounding_box(adev));
}

/* Tests for dm_execute_dmub_cmd() */

/**
 * dm_test_execute_dmub_cmd_null_dmub_srv - Test command execution fails without DMUB service
 * @test: The KUnit test context
 *
 * With no DC DMUB service on the context, dc_dmub_srv_cmd_run() returns false
 * and dm_execute_dmub_cmd() propagates that failure.
 */
static void dm_test_execute_dmub_cmd_null_dmub_srv(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	union dmub_rb_cmd *cmd;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	cmd = kunit_kzalloc(test, sizeof(*cmd), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cmd);

	spin_lock_init(&adev->dm.dmub_lock);
	ctx->driver_context = adev;
	ctx->dmub_srv = NULL;

	KUNIT_EXPECT_FALSE(test,
			   dm_execute_dmub_cmd(ctx, cmd, DM_DMUB_WAIT_TYPE_NO_WAIT));
}

/* Tests for amdgpu_dm_process_dmub_aux_transfer_sync() */

/**
 * dm_test_process_dmub_aux_transfer_sync_engine_acquire - Test AUX transfer engine-acquire failure
 * @test: The KUnit test context
 *
 * With dc->link_count == 0, dc_process_dmub_aux_transfer_async() rejects the
 * link index and amdgpu_dm_process_dmub_aux_transfer_sync() reports an
 * engine-acquire error and returns -1.
 */
static void dm_test_process_dmub_aux_transfer_sync_engine_acquire(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc *dc;
	struct aux_payload *payload;
	struct dmub_notification *notify;
	enum aux_return_code_type result = AUX_RET_SUCCESS;
	int ret;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);

	payload = kunit_kzalloc(test, sizeof(*payload), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload);

	notify = kunit_kzalloc(test, sizeof(*notify), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, notify);

	dc->link_count = 0;
	ctx->dc = dc;
	ctx->driver_context = adev;
	adev->dm.dmub_notify = notify;
	mutex_init(&adev->dm.dpia_aux_lock);
	init_completion(&adev->dm.dmub_aux_transfer_done);

	ret = amdgpu_dm_process_dmub_aux_transfer_sync(ctx, 0, payload, &result);

	KUNIT_EXPECT_EQ(test, ret, -1);
	KUNIT_EXPECT_EQ(test, result, AUX_RET_ERROR_ENGINE_ACQUIRE);
}

/**
 * dm_test_process_dmub_aux_transfer_sync_protocol_error - Test AUX protocol error result
 * @test: The KUnit test context
 *
 * With the completion pre-signaled and a fake DC DMUB service that rejects the
 * command after construction, the sync helper should propagate the notification
 * result without waiting for real firmware.
 */
static void dm_test_process_dmub_aux_transfer_sync_protocol_error(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_context *dc_ctx;
	struct dc *dc;
	struct dc_link *link;
	struct ddc_service *ddc;
	struct aux_payload *payload;
	struct dmub_notification *notify;
	enum aux_return_code_type result = AUX_RET_SUCCESS;
	int ret;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	dc_ctx = kunit_kzalloc(test, sizeof(*dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_ctx);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ddc);

	payload = kunit_kzalloc(test, sizeof(*payload), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload);

	notify = kunit_kzalloc(test, sizeof(*notify), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, notify);

	link->ddc = ddc;
	dc->ctx = dc_ctx;
	dc->link_count = 1;
	dc->links[0] = link;
	dc_ctx->dc = dc;
	dc_ctx->driver_context = adev;
	dc_ctx->dmub_srv = NULL;
	ctx->dc = dc;
	ctx->driver_context = adev;
	spin_lock_init(&adev->dm.dmub_lock);
	adev->dm.dmub_notify = notify;
	mutex_init(&adev->dm.dpia_aux_lock);
	init_completion(&adev->dm.dmub_aux_transfer_done);
	complete(&adev->dm.dmub_aux_transfer_done);
	notify->result = AUX_RET_ERROR_PROTOCOL_ERROR;

	ret = amdgpu_dm_process_dmub_aux_transfer_sync(ctx, 0, payload, &result);

	KUNIT_EXPECT_EQ(test, ret, -1);
	KUNIT_EXPECT_EQ(test, result, AUX_RET_ERROR_PROTOCOL_ERROR);
}

/**
 * dm_test_process_dmub_aux_transfer_sync_copies_data - Test AUX reply data copy
 * @test: The KUnit test context
 *
 * On a successful notification, the sync helper should copy the bounded reply
 * data and report the high-nibble command reply when present.
 */
static void dm_test_process_dmub_aux_transfer_sync_copies_data(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_context *dc_ctx;
	struct dc *dc;
	struct dc_link *link;
	struct ddc_service *ddc;
	struct aux_payload *payload;
	struct dmub_notification *notify;
	enum aux_return_code_type result = AUX_RET_ERROR_UNKNOWN;
	u8 data[4] = { 0 };
	u8 reply = 0;
	int ret;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	dc_ctx = kunit_kzalloc(test, sizeof(*dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_ctx);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ddc);

	payload = kunit_kzalloc(test, sizeof(*payload), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload);

	notify = kunit_kzalloc(test, sizeof(*notify), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, notify);

	link->ddc = ddc;
	dc->ctx = dc_ctx;
	dc->link_count = 1;
	dc->links[0] = link;
	dc_ctx->dc = dc;
	dc_ctx->driver_context = adev;
	dc_ctx->dmub_srv = NULL;
	ctx->dc = dc;
	ctx->driver_context = adev;
	spin_lock_init(&adev->dm.dmub_lock);
	adev->dm.dmub_notify = notify;
	mutex_init(&adev->dm.dpia_aux_lock);
	init_completion(&adev->dm.dmub_aux_transfer_done);
	complete(&adev->dm.dmub_aux_transfer_done);
	payload->data = data;
	payload->reply = &reply;
	payload->length = sizeof(data);
	notify->result = AUX_RET_SUCCESS;
	notify->aux_reply.command = 0xA4;
	notify->aux_reply.length = 3;
	notify->aux_reply.data[0] = 0x11;
	notify->aux_reply.data[1] = 0x22;
	notify->aux_reply.data[2] = 0x33;

	ret = amdgpu_dm_process_dmub_aux_transfer_sync(ctx, 0, payload, &result);

	KUNIT_EXPECT_EQ(test, ret, 3);
	KUNIT_EXPECT_EQ(test, result, AUX_RET_SUCCESS);
	KUNIT_EXPECT_EQ(test, reply, 0xA);
	KUNIT_EXPECT_EQ(test, data[0], 0x11);
	KUNIT_EXPECT_EQ(test, data[1], 0x22);
	KUNIT_EXPECT_EQ(test, data[2], 0x33);
}

/**
 * dm_test_process_dmub_aux_transfer_sync_zero_length - Test AUX reply with no data
 * @test: The KUnit test context
 *
 * On a successful notification whose reply carries no data, the sync helper
 * takes the zero-length branch and returns the reply length (0) without
 * copying any payload data.
 */
static void dm_test_process_dmub_aux_transfer_sync_zero_length(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_context *dc_ctx;
	struct dc *dc;
	struct dc_link *link;
	struct ddc_service *ddc;
	struct aux_payload *payload;
	struct dmub_notification *notify;
	enum aux_return_code_type result = AUX_RET_ERROR_UNKNOWN;
	u8 data[4] = { 0 };
	u8 reply = 0;
	int ret;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	dc_ctx = kunit_kzalloc(test, sizeof(*dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_ctx);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	ddc = kunit_kzalloc(test, sizeof(*ddc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ddc);

	payload = kunit_kzalloc(test, sizeof(*payload), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload);

	notify = kunit_kzalloc(test, sizeof(*notify), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, notify);

	link->ddc = ddc;
	dc->ctx = dc_ctx;
	dc->link_count = 1;
	dc->links[0] = link;
	dc_ctx->dc = dc;
	dc_ctx->driver_context = adev;
	dc_ctx->dmub_srv = NULL;
	ctx->dc = dc;
	ctx->driver_context = adev;
	spin_lock_init(&adev->dm.dmub_lock);
	adev->dm.dmub_notify = notify;
	mutex_init(&adev->dm.dpia_aux_lock);
	init_completion(&adev->dm.dmub_aux_transfer_done);
	complete(&adev->dm.dmub_aux_transfer_done);
	payload->data = data;
	payload->reply = &reply;
	payload->length = sizeof(data);
	notify->result = AUX_RET_SUCCESS;
	notify->aux_reply.command = 0x03;
	notify->aux_reply.length = 0;

	ret = amdgpu_dm_process_dmub_aux_transfer_sync(ctx, 0, payload, &result);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, result, AUX_RET_SUCCESS);
	KUNIT_EXPECT_EQ(test, reply, 0x3);
}

/* Tests for amdgpu_dm_process_dmub_set_config_sync() */

/**
 * dm_test_process_dmub_set_config_sync_unknown_error - Test SET_CONFIG completes with unknown error
 * @test: The KUnit test context
 *
 * With no DC DMUB service, dc_process_dmub_set_config_async() cannot reach the
 * firmware and reports the command as completed with SET_CONFIG_UNKNOWN_ERROR,
 * so amdgpu_dm_process_dmub_set_config_sync() returns 0 with that status.
 */
static void dm_test_process_dmub_set_config_sync_unknown_error(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dc_context *dc_ctx;
	struct dc *dc;
	struct dc_link *link;
	struct set_config_cmd_payload *payload;
	struct dmub_notification *notify;
	enum set_config_status result = SET_CONFIG_PENDING;
	int ret;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	dc_ctx = kunit_kzalloc(test, sizeof(*dc_ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_ctx);

	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);

	link = kunit_kzalloc(test, sizeof(*link), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, link);

	payload = kunit_kzalloc(test, sizeof(*payload), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, payload);

	notify = kunit_kzalloc(test, sizeof(*notify), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, notify);

	dc->ctx = dc_ctx;
	dc_ctx->dmub_srv = NULL;
	dc->links[0] = link;
	ctx->dc = dc;
	ctx->driver_context = adev;
	adev->dm.dmub_notify = notify;
	mutex_init(&adev->dm.dpia_aux_lock);
	init_completion(&adev->dm.dmub_aux_transfer_done);

	ret = amdgpu_dm_process_dmub_set_config_sync(ctx, 0, payload, &result);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, result, SET_CONFIG_UNKNOWN_ERROR);
}

/* Tests for abort_fused_io() */

/**
 * dm_test_abort_fused_io_no_dmub_srv - Test fused IO abort is a safe no-op without DMUB service
 * @test: The KUnit test context
 *
 * abort_fused_io() builds an abort command and submits it via
 * dm_execute_dmub_cmd(); with no DC DMUB service the submission fails
 * silently and the call must not crash.
 */
static void dm_test_abort_fused_io_no_dmub_srv(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc_context *ctx;
	struct dmub_cmd_fused_request *req;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	req = kunit_kzalloc(test, sizeof(*req), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, req);

	spin_lock_init(&adev->dm.dmub_lock);
	ctx->driver_context = adev;
	ctx->dmub_srv = NULL;

	/* Must not crash. */
	abort_fused_io(ctx, req);
}

static struct kunit_case amdgpu_dm_dmub_tests[] = {
	/* dm_register_dmub_notify_callback() */
	KUNIT_CASE(dm_test_register_dmub_notify_callback_null_callback),
	KUNIT_CASE(dm_test_register_dmub_notify_callback_type_out_of_range),
	KUNIT_CASE(dm_test_register_dmub_notify_callback_valid),
	KUNIT_CASE(dm_test_register_dmub_notify_callback_offload_false),
	/* dm_dmub_aux_setconfig_callback() */
	KUNIT_CASE(dm_test_dmub_aux_setconfig_callback_copies_and_completes),
	KUNIT_CASE(dm_test_dmub_aux_setconfig_callback_non_aux_no_complete),
	KUNIT_CASE(dm_test_dmub_aux_setconfig_callback_aux_with_null_dm_notify),
	KUNIT_CASE(dm_test_dmub_aux_setconfig_callback_set_config_reply),
	/* dm_dmub_aux_fused_io_callback() */
	KUNIT_CASE(dm_test_dmub_aux_fused_io_callback_copies_reply_and_completes),
	KUNIT_CASE(dm_test_dmub_aux_fused_io_callback_max_ddc_line),
	KUNIT_CASE(dm_test_dmub_aux_fused_io_callback_null_args),
	/* dm_get_default_ips_mode() */
	KUNIT_CASE(dm_test_get_default_ips_mode_dcn35),
	KUNIT_CASE(dm_test_get_default_ips_mode_dcn351),
	KUNIT_CASE(dm_test_get_default_ips_mode_dcn36),
	KUNIT_CASE(dm_test_get_default_ips_mode_older_than_dcn35),
	KUNIT_CASE(dm_test_get_default_ips_mode_newer_default),
	/* dm_dmub_hw_init() */
	KUNIT_CASE(dm_test_dmub_hw_init_no_dmub_srv),
	KUNIT_CASE(dm_test_dmub_hw_init_no_fb_info),
	KUNIT_CASE(dm_test_dmub_hw_init_no_firmware),
	KUNIT_CASE(dm_test_dmub_hw_init_success_fake_dmub),
	KUNIT_CASE(dm_test_dmub_hw_init_no_hw_support),
	KUNIT_CASE(dm_test_dmub_hw_init_bss_data),
	KUNIT_CASE(dm_test_dmub_hw_init_hw_init_fails),
	KUNIT_CASE(dm_test_dmub_hw_init_auto_load_timeout),
	KUNIT_CASE(dm_test_dmub_hw_init_apu_dpia_dcn35),
	KUNIT_CASE(dm_test_dmub_hw_init_sanity_checks_dcn31),
	KUNIT_CASE(dm_test_dmub_hw_init_sanity_checks_dcn314),
	KUNIT_CASE(dm_test_dmub_hw_init_dmcu_abm),
	/* dm_dmub_hw_resume() */
	KUNIT_CASE(dm_test_dmub_hw_resume_no_dmub_srv),
	KUNIT_CASE(dm_test_dmub_hw_resume_initialized_dmub),
	KUNIT_CASE(dm_test_dmub_hw_resume_full_init),
	KUNIT_CASE(dm_test_dmub_hw_resume_init_check_failed),
	KUNIT_CASE(dm_test_dmub_hw_resume_auto_load_timeout),
	/* dm_dmub_sw_init() */
	KUNIT_CASE(dm_test_dmub_sw_init_unsupported_asic),
	/* dm_init_microcode() */
	KUNIT_CASE(dm_test_init_microcode_unsupported_asic),
	/* dm_dmub_get_vbios_bounding_box() */
	KUNIT_CASE(dm_test_dmub_get_vbios_bounding_box_default_null),
	/* dm_execute_dmub_cmd() */
	KUNIT_CASE(dm_test_execute_dmub_cmd_null_dmub_srv),
	/* amdgpu_dm_process_dmub_aux_transfer_sync() */
	KUNIT_CASE(dm_test_process_dmub_aux_transfer_sync_engine_acquire),
	KUNIT_CASE(dm_test_process_dmub_aux_transfer_sync_protocol_error),
	KUNIT_CASE(dm_test_process_dmub_aux_transfer_sync_copies_data),
	KUNIT_CASE(dm_test_process_dmub_aux_transfer_sync_zero_length),
	/* amdgpu_dm_process_dmub_set_config_sync() */
	KUNIT_CASE(dm_test_process_dmub_set_config_sync_unknown_error),
	/* abort_fused_io() */
	KUNIT_CASE(dm_test_abort_fused_io_no_dmub_srv),
	{}
};

static struct kunit_suite amdgpu_dm_dmub_test_suite = {
	.name = "amdgpu_dm_dmub",
	.test_cases = amdgpu_dm_dmub_tests,
};

kunit_test_suite(amdgpu_dm_dmub_test_suite);

MODULE_AUTHOR("AMD");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_dmub");
MODULE_LICENSE("Dual MIT/GPL");
