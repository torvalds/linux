// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 */

#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_audio.h"
#include "amdgpu_dm_kunit_helpers.h"
#include "dc.h"

#include <linux/component.h>
#include <drm/drm_atomic.h>
#include <drm/drm_audio_component.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_eld.h>

#include "dc/inc/core_types.h"

static int amdgpu_dm_audio_component_get_eld(struct device *kdev, int port,
					  int pipe, bool *enabled,
					  unsigned char *buf, int max_bytes)
{
	struct drm_device *dev = dev_get_drvdata(kdev);
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct amdgpu_dm_connector *aconnector;
	int ret = 0;

	*enabled = false;

	mutex_lock(&adev->dm.audio_lock);

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (aconnector->audio_inst != port)
			continue;

		*enabled = true;
		mutex_lock(&connector->eld_mutex);
		ret = drm_eld_size(connector->eld);
		memcpy(buf, connector->eld, min(max_bytes, ret));
		mutex_unlock(&connector->eld_mutex);

		break;
	}
	drm_connector_list_iter_end(&conn_iter);

	mutex_unlock(&adev->dm.audio_lock);

	drm_dbg_kms(adev_to_drm(adev), "Get ELD : idx=%d ret=%d en=%d\n", port, ret, *enabled);

	return ret;
}

static const struct drm_audio_component_ops amdgpu_dm_audio_component_ops = {
	.get_eld = amdgpu_dm_audio_component_get_eld,
};

STATIC_IFN_KUNIT int amdgpu_dm_audio_component_bind(struct device *kdev,
				       struct device *hda_kdev, void *data)
{
	struct drm_device *dev = dev_get_drvdata(kdev);
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct drm_audio_component *acomp = data;

	acomp->ops = &amdgpu_dm_audio_component_ops;
	acomp->dev = kdev;
	adev->dm.audio_component = acomp;

	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_audio_component_bind);

STATIC_IFN_KUNIT void amdgpu_dm_audio_component_unbind(struct device *kdev,
					  struct device *hda_kdev, void *data)
{
	struct amdgpu_device *adev = drm_to_adev(dev_get_drvdata(kdev));
	struct drm_audio_component *acomp = data;

	acomp->ops = NULL;
	acomp->dev = NULL;
	adev->dm.audio_component = NULL;
}
EXPORT_IF_KUNIT(amdgpu_dm_audio_component_unbind);

static const struct component_ops amdgpu_dm_audio_component_bind_ops = {
	.bind	= amdgpu_dm_audio_component_bind,
	.unbind	= amdgpu_dm_audio_component_unbind,
};

STATIC_IFN_KUNIT
void amdgpu_dm_audio_init_pins(struct amdgpu_device *adev, int audio_count,
			       const unsigned int *inst_array)
{
	int i;

	adev->mode_info.audio.num_pins = audio_count;

	for (i = 0; i < audio_count; i++) {
		adev->mode_info.audio.pin[i].channels = -1;
		adev->mode_info.audio.pin[i].rate = -1;
		adev->mode_info.audio.pin[i].bits_per_sample = -1;
		adev->mode_info.audio.pin[i].status_bits = 0;
		adev->mode_info.audio.pin[i].category_code = 0;
		adev->mode_info.audio.pin[i].connected = false;
		adev->mode_info.audio.pin[i].id = inst_array[i];
		adev->mode_info.audio.pin[i].offset = 0;
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_audio_init_pins);

int amdgpu_dm_audio_init(struct amdgpu_device *adev)
{
	unsigned int inst_array[MAX_AUDIOS];
	int audio_count;
	int i, ret;

	if (!amdgpu_audio)
		return 0;

	adev->mode_info.audio.enabled = true;

	audio_count = adev->dm.dc->res_pool->audio_count;
	for (i = 0; i < audio_count; i++)
		inst_array[i] = adev->dm.dc->res_pool->audios[i]->inst;
	amdgpu_dm_audio_init_pins(adev, audio_count, inst_array);

	ret = component_add(adev->dev, &amdgpu_dm_audio_component_bind_ops);
	if (ret < 0)
		return ret;

	adev->dm.audio_registered = true;

	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_audio_init);

void amdgpu_dm_audio_fini(struct amdgpu_device *adev)
{
	if (!amdgpu_audio)
		return;

	if (!adev->mode_info.audio.enabled)
		return;

	if (adev->dm.audio_registered) {
		component_del(adev->dev, &amdgpu_dm_audio_component_bind_ops);
		adev->dm.audio_registered = false;
	}

	/* TODO: Disable audio? */

	adev->mode_info.audio.enabled = false;
}
EXPORT_IF_KUNIT(amdgpu_dm_audio_fini);

void amdgpu_dm_audio_eld_notify(struct amdgpu_device *adev, int pin)
{
	struct drm_audio_component *acomp = adev->dm.audio_component;

	if (acomp && acomp->audio_ops && acomp->audio_ops->pin_eld_notify) {
		drm_dbg_kms(adev_to_drm(adev), "Notify ELD: %d\n", pin);

		acomp->audio_ops->pin_eld_notify(acomp->audio_ops->audio_ptr,
						 pin, -1);
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_audio_eld_notify);

void amdgpu_dm_fill_audio_info(struct audio_info *audio_info,
		     const struct drm_connector *drm_connector,
		     const struct dc_sink *dc_sink)
{
	int i = 0;
	int cea_revision = 0;
	const struct dc_edid_caps *edid_caps = &dc_sink->edid_caps;

	audio_info->manufacture_id = edid_caps->manufacturer_id;
	audio_info->product_id = edid_caps->product_id;

	cea_revision = drm_connector->display_info.cea_rev;

	strscpy(audio_info->display_name,
		edid_caps->display_name,
		AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS);

	if (cea_revision >= 3) {
		audio_info->mode_count = edid_caps->audio_mode_count;

		for (i = 0; i < audio_info->mode_count; ++i) {
			audio_info->modes[i].format_code =
					(enum audio_format_code)
					(edid_caps->audio_modes[i].format_code);
			audio_info->modes[i].channel_count =
					edid_caps->audio_modes[i].channel_count;
			audio_info->modes[i].sample_rates.all =
					edid_caps->audio_modes[i].sample_rate;
			audio_info->modes[i].sample_size =
					edid_caps->audio_modes[i].sample_size;
		}
	}

	audio_info->flags.all = edid_caps->speaker_flags;

	/* TODO: We only check for the progressive mode, check for interlace mode too */
	if (drm_connector->latency_present[0]) {
		audio_info->video_latency = drm_connector->video_latency[0];
		audio_info->audio_latency = drm_connector->audio_latency[0];
	}

	/* TODO: For DP, video and audio latency should be calculated from DPCD caps */

}
EXPORT_IF_KUNIT(amdgpu_dm_fill_audio_info);

void amdgpu_dm_commit_audio(struct drm_device *dev,
			    struct drm_atomic_commit *state)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct drm_connector_state *old_con_state, *new_con_state;
	struct drm_crtc_state *new_crtc_state;
	struct dm_crtc_state *new_dm_crtc_state;
	const struct dc_stream_status *status;
	int i, inst;

	/* Notify device removals. */
	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		if (old_con_state->crtc != new_con_state->crtc) {
			/* CRTC changes require notification. */
			goto notify;
		}

		if (!new_con_state->crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(
			state, new_con_state->crtc);

		if (!new_crtc_state)
			continue;

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

notify:
		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);

		mutex_lock(&adev->dm.audio_lock);
		inst = aconnector->audio_inst;
		aconnector->audio_inst = -1;
		mutex_unlock(&adev->dm.audio_lock);

		amdgpu_dm_audio_eld_notify(adev, inst);
	}

	/* Notify audio device additions. */
	for_each_new_connector_in_state(state, connector, new_con_state, i) {
		if (!new_con_state->crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(
			state, new_con_state->crtc);

		if (!new_crtc_state)
			continue;

		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

		new_dm_crtc_state = to_dm_crtc_state(new_crtc_state);
		if (!new_dm_crtc_state->stream)
			continue;

		status = dc_stream_get_status(new_dm_crtc_state->stream);
		if (!status)
			continue;

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);

		mutex_lock(&adev->dm.audio_lock);
		inst = status->audio_inst;
		aconnector->audio_inst = inst;
		mutex_unlock(&adev->dm.audio_lock);

		amdgpu_dm_audio_eld_notify(adev, inst);
	}
}

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
int amdgpu_dm_audio_get_param(void)
{
	return amdgpu_audio;
}
EXPORT_IF_KUNIT(amdgpu_dm_audio_get_param);

void amdgpu_dm_audio_set_param(int val)
{
	amdgpu_audio = val;
}
EXPORT_IF_KUNIT(amdgpu_dm_audio_set_param);
#endif
