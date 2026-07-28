// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * KUnit tests for amdgpu_dm_audio.c
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include <kunit/test.h>
#include <linux/device.h>

#include <drm/drm_audio_component.h>

#include "dc.h"
#include "dc/inc/core_types.h"
#include "dc/inc/hw/audio.h"
#include "amdgpu.h"
#include "amdgpu_mode.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_audio.h"

/* Tests for amdgpu_dm_audio_init() */

/**
 * dm_test_audio_init_disabled - Test audio init exits when audio is disabled
 * @test: The KUnit test context
 */
static void dm_test_audio_init_disabled(struct kunit *test)
{
	struct amdgpu_device *adev;
	int saved_audio = amdgpu_dm_audio_get_param();

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	amdgpu_dm_audio_set_param(0);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_audio_init(adev), 0);
	KUNIT_EXPECT_FALSE(test, adev->mode_info.audio.enabled);
	KUNIT_EXPECT_FALSE(test, adev->dm.audio_registered);

	amdgpu_dm_audio_set_param(saved_audio);
}

/**
 * dm_test_audio_init_enabled_success - Test init deeper path when audio is enabled
 * @test: The KUnit test context
 */
static void dm_test_audio_init_enabled_success(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct dc *dc;
	struct resource_pool *res_pool;
	struct audio *audio0;
	struct audio *audio1;
	struct device *dev;
	int saved_audio = amdgpu_dm_audio_get_param();

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	dc = kunit_kzalloc(test, sizeof(*dc), GFP_KERNEL);
	res_pool = kunit_kzalloc(test, sizeof(*res_pool), GFP_KERNEL);
	audio0 = kunit_kzalloc(test, sizeof(*audio0), GFP_KERNEL);
	audio1 = kunit_kzalloc(test, sizeof(*audio1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, res_pool);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, audio0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, audio1);

	dev = root_device_register("kunit-dm-audio-init");
	KUNIT_ASSERT_FALSE(test, IS_ERR(dev));

	audio0->inst = 2;
	audio1->inst = 6;
	res_pool->audio_count = 2;
	res_pool->audios[0] = audio0;
	res_pool->audios[1] = audio1;
	dc->res_pool = res_pool;
	adev->dm.dc = dc;
	adev->dev = dev;

	amdgpu_dm_audio_set_param(1);

	KUNIT_EXPECT_EQ(test, amdgpu_dm_audio_init(adev), 0);
	KUNIT_EXPECT_TRUE(test, adev->mode_info.audio.enabled);
	KUNIT_EXPECT_TRUE(test, adev->dm.audio_registered);
	KUNIT_EXPECT_EQ(test, adev->mode_info.audio.num_pins, 2);
	KUNIT_EXPECT_EQ(test, adev->mode_info.audio.pin[0].id, 2U);
	KUNIT_EXPECT_EQ(test, adev->mode_info.audio.pin[1].id, 6U);

	amdgpu_dm_audio_fini(adev);
	root_device_unregister(dev);
	amdgpu_dm_audio_set_param(saved_audio);
}

/* Tests for amdgpu_dm_audio_fini() */

/**
 * dm_test_audio_fini_without_enabled_audio - Test fini exits when audio is not enabled
 * @test: The KUnit test context
 */
static void dm_test_audio_fini_without_enabled_audio(struct kunit *test)
{
	struct amdgpu_device *adev;
	int saved_audio = amdgpu_dm_audio_get_param();

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	amdgpu_dm_audio_set_param(1);
	adev->mode_info.audio.enabled = false;
	adev->dm.audio_registered = true;

	amdgpu_dm_audio_fini(adev);

	KUNIT_EXPECT_FALSE(test, adev->mode_info.audio.enabled);
	KUNIT_EXPECT_TRUE(test, adev->dm.audio_registered);

	amdgpu_dm_audio_set_param(saved_audio);
}

/* Tests for amdgpu_dm_fill_audio_info() */

/**
 * dm_test_fill_audio_info_ids_name_flags - Test Fill audio info ids name flags
 * @test: The KUnit test context
 */
static void dm_test_fill_audio_info_ids_name_flags(struct kunit *test)
{
	struct audio_info *audio_info;
	struct drm_connector *connector;
	struct dc_sink *dc_sink;
	const char *name = "DM-AUDIO-PANEL";

	audio_info = kunit_kzalloc(test, sizeof(*audio_info), GFP_KERNEL);
	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	dc_sink = kunit_kzalloc(test, sizeof(*dc_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, audio_info);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_sink);

	dc_sink->edid_caps.manufacturer_id = 0x1234;
	dc_sink->edid_caps.product_id = 0xABCD;
	dc_sink->edid_caps.speaker_flags = 0x5;
	strscpy(dc_sink->edid_caps.display_name, name,
		AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS);

	connector->display_info.cea_rev = 1;

	amdgpu_dm_fill_audio_info(audio_info, connector, dc_sink);

	KUNIT_EXPECT_EQ(test, audio_info->manufacture_id, 0x1234U);
	KUNIT_EXPECT_EQ(test, audio_info->product_id, 0xABCDU);
	KUNIT_EXPECT_EQ(test, audio_info->flags.all, 0x5U);
	KUNIT_EXPECT_STREQ(test, audio_info->display_name, name);
}

/**
 * dm_test_fill_audio_info_cea_lt_3_skips_modes - Test Fill audio info cea lt 3 skips modes
 * @test: The KUnit test context
 */
static void dm_test_fill_audio_info_cea_lt_3_skips_modes(struct kunit *test)
{
	struct audio_info *audio_info;
	struct drm_connector *connector;
	struct dc_sink *dc_sink;

	audio_info = kunit_kzalloc(test, sizeof(*audio_info), GFP_KERNEL);
	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	dc_sink = kunit_kzalloc(test, sizeof(*dc_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, audio_info);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_sink);

	connector->display_info.cea_rev = 2;
	dc_sink->edid_caps.audio_mode_count = 2;
	dc_sink->edid_caps.audio_modes[0].format_code = 1;
	dc_sink->edid_caps.audio_modes[0].channel_count = 2;
	dc_sink->edid_caps.audio_modes[0].sample_rate = 0x07;
	dc_sink->edid_caps.audio_modes[0].sample_size = 16;

	amdgpu_dm_fill_audio_info(audio_info, connector, dc_sink);

	KUNIT_EXPECT_EQ(test, audio_info->mode_count, 0U);
}

/**
 * dm_test_fill_audio_info_cea_ge_3_copies_modes - Test Fill audio info cea ge 3 copies modes
 * @test: The KUnit test context
 */
static void dm_test_fill_audio_info_cea_ge_3_copies_modes(struct kunit *test)
{
	struct audio_info *audio_info;
	struct drm_connector *connector;
	struct dc_sink *dc_sink;

	audio_info = kunit_kzalloc(test, sizeof(*audio_info), GFP_KERNEL);
	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	dc_sink = kunit_kzalloc(test, sizeof(*dc_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, audio_info);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_sink);

	connector->display_info.cea_rev = 3;
	dc_sink->edid_caps.audio_mode_count = 2;

	dc_sink->edid_caps.audio_modes[0].format_code = 1;
	dc_sink->edid_caps.audio_modes[0].channel_count = 2;
	dc_sink->edid_caps.audio_modes[0].sample_rate = 0x07;
	dc_sink->edid_caps.audio_modes[0].sample_size = 16;

	dc_sink->edid_caps.audio_modes[1].format_code = 11;
	dc_sink->edid_caps.audio_modes[1].channel_count = 6;
	dc_sink->edid_caps.audio_modes[1].sample_rate = 0x1F;
	dc_sink->edid_caps.audio_modes[1].sample_size = 24;

	amdgpu_dm_fill_audio_info(audio_info, connector, dc_sink);

	KUNIT_EXPECT_EQ(test, audio_info->mode_count, 2U);

	KUNIT_EXPECT_EQ(test, (int)audio_info->modes[0].format_code, 1);
	KUNIT_EXPECT_EQ(test, audio_info->modes[0].channel_count, 2);
	KUNIT_EXPECT_EQ(test, audio_info->modes[0].sample_rates.all, 0x07U);
	KUNIT_EXPECT_EQ(test, audio_info->modes[0].sample_size, 16);

	KUNIT_EXPECT_EQ(test, (int)audio_info->modes[1].format_code, 11);
	KUNIT_EXPECT_EQ(test, audio_info->modes[1].channel_count, 6);
	KUNIT_EXPECT_EQ(test, audio_info->modes[1].sample_rates.all, 0x1FU);
	KUNIT_EXPECT_EQ(test, audio_info->modes[1].sample_size, 24);
}

/**
 * dm_test_fill_audio_info_latency_present - Test Fill audio info latency present
 * @test: The KUnit test context
 */
static void dm_test_fill_audio_info_latency_present(struct kunit *test)
{
	struct audio_info *audio_info;
	struct drm_connector *connector;
	struct dc_sink *dc_sink;

	audio_info = kunit_kzalloc(test, sizeof(*audio_info), GFP_KERNEL);
	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	dc_sink = kunit_kzalloc(test, sizeof(*dc_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, audio_info);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_sink);

	connector->display_info.cea_rev = 3;
	connector->latency_present[0] = true;
	connector->video_latency[0] = 11;
	connector->audio_latency[0] = 22;

	amdgpu_dm_fill_audio_info(audio_info, connector, dc_sink);

	KUNIT_EXPECT_EQ(test, audio_info->video_latency, 11U);
	KUNIT_EXPECT_EQ(test, audio_info->audio_latency, 22U);
}

/**
 * dm_test_fill_audio_info_latency_absent_keeps_zero - Test Fill audio info latency absent keeps zero
 * @test: The KUnit test context
 */
static void dm_test_fill_audio_info_latency_absent_keeps_zero(struct kunit *test)
{
	struct audio_info *audio_info;
	struct drm_connector *connector;
	struct dc_sink *dc_sink;

	audio_info = kunit_kzalloc(test, sizeof(*audio_info), GFP_KERNEL);
	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	dc_sink = kunit_kzalloc(test, sizeof(*dc_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, audio_info);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_sink);

	connector->display_info.cea_rev = 3;
	connector->latency_present[0] = false;
	connector->video_latency[0] = 99;
	connector->audio_latency[0] = 88;

	amdgpu_dm_fill_audio_info(audio_info, connector, dc_sink);

	KUNIT_EXPECT_EQ(test, audio_info->video_latency, 0U);
	KUNIT_EXPECT_EQ(test, audio_info->audio_latency, 0U);
}

/**
 * dm_test_fill_audio_info_cea_ge_3_zero_modes - Test cea >= 3 with zero modes
 * @test: The KUnit test context
 *
 * When cea_rev >= 3 but the sink reports no audio modes, mode_count must be
 * copied as 0 and no mode entries should be populated.
 */
static void dm_test_fill_audio_info_cea_ge_3_zero_modes(struct kunit *test)
{
	struct audio_info *audio_info;
	struct drm_connector *connector;
	struct dc_sink *dc_sink;

	audio_info = kunit_kzalloc(test, sizeof(*audio_info), GFP_KERNEL);
	connector = kunit_kzalloc(test, sizeof(*connector), GFP_KERNEL);
	dc_sink = kunit_kzalloc(test, sizeof(*dc_sink), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, audio_info);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, connector);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dc_sink);

	connector->display_info.cea_rev = 3;
	dc_sink->edid_caps.audio_mode_count = 0;

	amdgpu_dm_fill_audio_info(audio_info, connector, dc_sink);

	KUNIT_EXPECT_EQ(test, audio_info->mode_count, 0U);
	KUNIT_EXPECT_EQ(test, (int)audio_info->modes[0].format_code, 0);
}

/* Tests for amdgpu_dm_audio_component_bind()/unbind() */

/**
 * dm_test_audio_component_bind_sets_fields - Test bind wires up audio component
 * @test: The KUnit test context
 *
 * Binding must publish the DRM audio component ops, record the kernel device,
 * and store the component pointer in the display manager.
 */
static void dm_test_audio_component_bind_sets_fields(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct device *kdev;
	struct drm_audio_component *acomp;
	int ret;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	kdev = kunit_kzalloc(test, sizeof(*kdev), GFP_KERNEL);
	acomp = kunit_kzalloc(test, sizeof(*acomp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, kdev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acomp);

	dev_set_drvdata(kdev, &adev->ddev);

	ret = amdgpu_dm_audio_component_bind(kdev, NULL, acomp);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_NOT_NULL(test, acomp->ops);
	KUNIT_EXPECT_PTR_EQ(test, acomp->dev, kdev);
	KUNIT_EXPECT_PTR_EQ(test, adev->dm.audio_component, acomp);
}

/**
 * dm_test_audio_component_unbind_clears_fields - Test unbind tears down component
 * @test: The KUnit test context
 *
 * Unbinding must clear the component ops, the kernel device, and the display
 * manager's stored component pointer.
 */
static void dm_test_audio_component_unbind_clears_fields(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct device *kdev;
	struct drm_audio_component *acomp;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	kdev = kunit_kzalloc(test, sizeof(*kdev), GFP_KERNEL);
	acomp = kunit_kzalloc(test, sizeof(*acomp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, kdev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acomp);

	dev_set_drvdata(kdev, &adev->ddev);

	/* Pretend a prior bind already happened. */
	acomp->dev = kdev;
	adev->dm.audio_component = acomp;

	amdgpu_dm_audio_component_unbind(kdev, NULL, acomp);

	KUNIT_EXPECT_NULL(test, acomp->ops);
	KUNIT_EXPECT_NULL(test, acomp->dev);
	KUNIT_EXPECT_NULL(test, adev->dm.audio_component);
}

/* Tests for amdgpu_dm_audio_eld_notify() */

static int dm_test_eld_notify_count;
static int dm_test_eld_notify_port;
static void *dm_test_eld_notify_ptr;

static void dm_test_pin_eld_notify(void *audio_ptr, int port, int pipe)
{
	dm_test_eld_notify_count++;
	dm_test_eld_notify_port = port;
	dm_test_eld_notify_ptr = audio_ptr;
}

/**
 * dm_test_eld_notify_invokes_callback - Test ELD notify forwards to hda driver
 * @test: The KUnit test context
 *
 * When a component with a pin_eld_notify callback is registered, the notify
 * helper must invoke it with the audio pointer and the requested pin.
 */
static void dm_test_eld_notify_invokes_callback(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_audio_component *acomp;
	struct drm_audio_component_audio_ops *audio_ops;
	int marker = 0;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	acomp = kunit_kzalloc(test, sizeof(*acomp), GFP_KERNEL);
	audio_ops = kunit_kzalloc(test, sizeof(*audio_ops), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acomp);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, audio_ops);

	audio_ops->audio_ptr = &marker;
	audio_ops->pin_eld_notify = dm_test_pin_eld_notify;
	acomp->audio_ops = audio_ops;
	adev->dm.audio_component = acomp;

	dm_test_eld_notify_count = 0;
	dm_test_eld_notify_port = -100;
	dm_test_eld_notify_ptr = NULL;

	amdgpu_dm_audio_eld_notify(adev, 7);

	KUNIT_EXPECT_EQ(test, dm_test_eld_notify_count, 1);
	KUNIT_EXPECT_EQ(test, dm_test_eld_notify_port, 7);
	KUNIT_EXPECT_PTR_EQ(test, dm_test_eld_notify_ptr, (void *)&marker);
}

/**
 * dm_test_eld_notify_no_component - Test ELD notify is a no-op without component
 * @test: The KUnit test context
 *
 * With no registered audio component, the notify helper must return without
 * invoking any callback.
 */
static void dm_test_eld_notify_no_component(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	adev->dm.audio_component = NULL;

	dm_test_eld_notify_count = 0;

	amdgpu_dm_audio_eld_notify(adev, 3);

	KUNIT_EXPECT_EQ(test, dm_test_eld_notify_count, 0);
}

/**
 * dm_test_eld_notify_null_audio_ops - Test ELD notify is a no-op without audio_ops
 * @test: The KUnit test context
 *
 * A component without audio_ops must not trigger any callback.
 */
static void dm_test_eld_notify_null_audio_ops(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_audio_component *acomp;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	acomp = kunit_kzalloc(test, sizeof(*acomp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acomp);

	acomp->audio_ops = NULL;
	adev->dm.audio_component = acomp;

	dm_test_eld_notify_count = 0;

	amdgpu_dm_audio_eld_notify(adev, 3);

	KUNIT_EXPECT_EQ(test, dm_test_eld_notify_count, 0);
}

/**
 * dm_test_eld_notify_null_callback - Test ELD notify is a no-op without callback
 * @test: The KUnit test context
 *
 * audio_ops present but with a NULL pin_eld_notify must not crash or call
 * anything.
 */
static void dm_test_eld_notify_null_callback(struct kunit *test)
{
	struct amdgpu_device *adev;
	struct drm_audio_component *acomp;
	struct drm_audio_component_audio_ops *audio_ops;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	acomp = kunit_kzalloc(test, sizeof(*acomp), GFP_KERNEL);
	audio_ops = kunit_kzalloc(test, sizeof(*audio_ops), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, acomp);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, audio_ops);

	audio_ops->pin_eld_notify = NULL;
	acomp->audio_ops = audio_ops;
	adev->dm.audio_component = acomp;

	dm_test_eld_notify_count = 0;

	amdgpu_dm_audio_eld_notify(adev, 3);

	KUNIT_EXPECT_EQ(test, dm_test_eld_notify_count, 0);
}

/* Tests for amdgpu_dm_audio_init_pins() */

/**
 * dm_test_audio_init_pins_sets_defaults - pin entries are initialised to default values
 * @test: The KUnit test context
 *
 * amdgpu_dm_audio_init_pins() must set num_pins from audio_count, reset every
 * pin to the sentinel defaults (-1 for rate/channels/bits, 0 for the rest)
 * and copy each pin's hardware instance index from res_pool->audios[i]->inst.
 */
static void dm_test_audio_init_pins_sets_defaults(struct kunit *test)
{
	struct amdgpu_device *adev;
	const unsigned int inst_array[] = {3, 7};
	int i;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	amdgpu_dm_audio_init_pins(adev, 2, inst_array);

	KUNIT_EXPECT_EQ(test, adev->mode_info.audio.num_pins, 2);

	for (i = 0; i < 2; i++) {
		KUNIT_EXPECT_EQ(test, adev->mode_info.audio.pin[i].channels, -1);
		KUNIT_EXPECT_EQ(test, adev->mode_info.audio.pin[i].rate, -1);
		KUNIT_EXPECT_EQ(test, adev->mode_info.audio.pin[i].bits_per_sample, -1);
		KUNIT_EXPECT_EQ(test, adev->mode_info.audio.pin[i].status_bits, 0);
		KUNIT_EXPECT_EQ(test, adev->mode_info.audio.pin[i].category_code, 0);
		KUNIT_EXPECT_FALSE(test, adev->mode_info.audio.pin[i].connected);
		KUNIT_EXPECT_EQ(test, adev->mode_info.audio.pin[i].offset, 0);
	}
	KUNIT_EXPECT_EQ(test, adev->mode_info.audio.pin[0].id, 3U);
	KUNIT_EXPECT_EQ(test, adev->mode_info.audio.pin[1].id, 7U);
}

/**
 * dm_test_audio_init_pins_zero_count - zero audio_count leaves num_pins at zero
 * @test: The KUnit test context
 *
 * When res_pool->audio_count is 0, num_pins must be 0 and no pins touched.
 */
static void dm_test_audio_init_pins_zero_count(struct kunit *test)
{
	struct amdgpu_device *adev;

	adev = kunit_kzalloc(test, sizeof(*adev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, adev);

	/* Pre-fill a sentinel so we can confirm the loop never ran. */
	adev->mode_info.audio.pin[0].channels = 99;

	amdgpu_dm_audio_init_pins(adev, 0, NULL);

	KUNIT_EXPECT_EQ(test, adev->mode_info.audio.num_pins, 0);
	KUNIT_EXPECT_EQ(test, adev->mode_info.audio.pin[0].channels, 99);
}

/* End of tests for amdgpu_dm_audio_init_pins() */

static struct kunit_case dm_audio_test_cases[] = {
	/* amdgpu_dm_audio_init */
	KUNIT_CASE(dm_test_audio_init_disabled),
	KUNIT_CASE(dm_test_audio_init_enabled_success),
	/* amdgpu_dm_audio_init_pins */
	KUNIT_CASE(dm_test_audio_init_pins_sets_defaults),
	KUNIT_CASE(dm_test_audio_init_pins_zero_count),
	/* amdgpu_dm_audio_fini */
	KUNIT_CASE(dm_test_audio_fini_without_enabled_audio),
	/* amdgpu_dm_fill_audio_info */
	KUNIT_CASE(dm_test_fill_audio_info_ids_name_flags),
	KUNIT_CASE(dm_test_fill_audio_info_cea_lt_3_skips_modes),
	KUNIT_CASE(dm_test_fill_audio_info_cea_ge_3_copies_modes),
	KUNIT_CASE(dm_test_fill_audio_info_cea_ge_3_zero_modes),
	KUNIT_CASE(dm_test_fill_audio_info_latency_present),
	KUNIT_CASE(dm_test_fill_audio_info_latency_absent_keeps_zero),
	/* amdgpu_dm_audio_component_bind/unbind */
	KUNIT_CASE(dm_test_audio_component_bind_sets_fields),
	KUNIT_CASE(dm_test_audio_component_unbind_clears_fields),
	/* amdgpu_dm_audio_eld_notify */
	KUNIT_CASE(dm_test_eld_notify_invokes_callback),
	KUNIT_CASE(dm_test_eld_notify_no_component),
	KUNIT_CASE(dm_test_eld_notify_null_audio_ops),
	KUNIT_CASE(dm_test_eld_notify_null_callback),
	{}
};

static struct kunit_suite dm_audio_test_suite = {
	.name = "amdgpu_dm_audio",
	.test_cases = dm_audio_test_cases,
};

kunit_test_suite(dm_audio_test_suite);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("KUnit tests for amdgpu_dm_audio");
MODULE_AUTHOR("AMD");
