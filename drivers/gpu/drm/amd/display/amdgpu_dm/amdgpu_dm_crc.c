/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 *
 */

#include <drm/drm_crtc.h>
#include <drm/drm_vblank.h>

#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "dc.h"
#include "amdgpu_securedisplay.h"

static const char *const pipe_crc_sources[] = {
	"none",
	"crtc",
	"crtc dither",
	"dprx",
	"dprx dither",
	"auto",
};

static enum amdgpu_dm_pipe_crc_source dm_parse_crc_source(const char *source)
{
	if (!source || !strcmp(source, "none"))
		return AMDGPU_DM_PIPE_CRC_SOURCE_NONE;
	if (!strcmp(source, "auto") || !strcmp(source, "crtc"))
		return AMDGPU_DM_PIPE_CRC_SOURCE_CRTC;
	if (!strcmp(source, "dprx"))
		return AMDGPU_DM_PIPE_CRC_SOURCE_DPRX;
	if (!strcmp(source, "crtc dither"))
		return AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER;
	if (!strcmp(source, "dprx dither"))
		return AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER;

	return AMDGPU_DM_PIPE_CRC_SOURCE_INVALID;
}

static bool dm_is_crc_source_crtc(enum amdgpu_dm_pipe_crc_source src)
{
	return (src == AMDGPU_DM_PIPE_CRC_SOURCE_CRTC) ||
	       (src == AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER);
}

static bool dm_is_crc_source_dprx(enum amdgpu_dm_pipe_crc_source src)
{
	return (src == AMDGPU_DM_PIPE_CRC_SOURCE_DPRX) ||
	       (src == AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER);
}

static bool dm_need_crc_dither(enum amdgpu_dm_pipe_crc_source src)
{
	return (src == AMDGPU_DM_PIPE_CRC_SOURCE_CRTC_DITHER) ||
	       (src == AMDGPU_DM_PIPE_CRC_SOURCE_DPRX_DITHER) ||
	       (src == AMDGPU_DM_PIPE_CRC_SOURCE_NONE);
}

const char *const *amdgpu_dm_crtc_get_crc_sources(struct drm_crtc *crtc,
						  size_t *count)
{
	*count = ARRAY_SIZE(pipe_crc_sources);
	return pipe_crc_sources;
}

#ifdef CONFIG_DRM_AMD_SECURE_DISPLAY
static void update_phy_id_mapping(struct amdgpu_device *adev)
{
	struct drm_device *ddev = adev_to_drm(adev);
	struct amdgpu_display_manager *dm = &adev->dm;
	struct drm_connector *connector;
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_dm_connector *sort_connector[AMDGPU_DM_MAX_CRTC] = {NULL};
	struct drm_connector_list_iter iter;
	uint8_t idx = 0, idx_2 = 0, connector_cnt = 0;

	dm->secure_display_ctx.phy_mapping_updated = false;

	mutex_lock(&ddev->mode_config.mutex);
	drm_connector_list_iter_begin(ddev, &iter);
	drm_for_each_connector_iter(connector, &iter) {

		if (connector->status != connector_status_connected)
			continue;

		if (idx >= AMDGPU_DM_MAX_CRTC) {
			DRM_WARN("%s connected connectors exceed max crtc\n", __func__);
			mutex_unlock(&ddev->mode_config.mutex);
			return;
		}

		aconnector = to_amdgpu_dm_connector(connector);

		sort_connector[idx] = aconnector;
		idx++;
		connector_cnt++;
	}
	drm_connector_list_iter_end(&iter);

	/* sort connectors by link_enc_hw_instance first */
	for (idx = connector_cnt; idx > 1 ; idx--) {
		for (idx_2 = 0; idx_2 < (idx - 1); idx_2++) {
			if (sort_connector[idx_2]->dc_link->link_enc_hw_inst >
				sort_connector[idx_2 + 1]->dc_link->link_enc_hw_inst) {
				aconnector = sort_connector[idx_2];
				sort_connector[idx_2] = sort_connector[idx_2 + 1];
				sort_connector[idx_2 + 1] = aconnector;
			}
		}
	}

	/*
	 * Sort mst connectors by RAD. mst connectors with the same enc_hw_instance are already
	 * sorted together above.
	 */
	for (idx = 0; idx < connector_cnt; /*Do nothing*/) {
		if (sort_connector[idx]->mst_root) {
			uint8_t i, j, k;
			uint8_t mst_con_cnt = 1;

			for (idx_2 = (idx + 1); idx_2 < connector_cnt; idx_2++) {
				if (sort_connector[idx_2]->mst_root == sort_connector[idx]->mst_root)
					mst_con_cnt++;
				else
					break;
			}

			for (i = mst_con_cnt; i > 1; i--) {
				for (j = idx; j < (idx + i - 2); j++) {
					int mstb_lct = sort_connector[j]->mst_output_port->parent->lct;
					int next_mstb_lct = sort_connector[j + 1]->mst_output_port->parent->lct;
					u8 *rad;
					u8 *next_rad;
					bool swap = false;

					/* Sort by mst tree depth first. Then compare RAD if depth is the same*/
					if (mstb_lct > next_mstb_lct) {
						swap = true;
					} else if (mstb_lct == next_mstb_lct) {
						if (mstb_lct == 1) {
							if (sort_connector[j]->mst_output_port->port_num > sort_connector[j + 1]->mst_output_port->port_num)
								swap = true;
						} else if (mstb_lct > 1) {
							rad = sort_connector[j]->mst_output_port->parent->rad;
							next_rad = sort_connector[j + 1]->mst_output_port->parent->rad;

							for (k = 0; k < mstb_lct - 1; k++) {
								int shift = (k % 2) ? 0 : 4;
								int port_num = (rad[k / 2] >> shift) & 0xf;
								int next_port_num = (next_rad[k / 2] >> shift) & 0xf;

								if (port_num > next_port_num) {
									swap = true;
									break;
								}
							}
						} else {
							DRM_ERROR("MST LCT shouldn't be set as < 1");
							mutex_unlock(&ddev->mode_config.mutex);
							return;
						}
					}

					if (swap) {
						aconnector = sort_connector[j];
						sort_connector[j] = sort_connector[j + 1];
						sort_connector[j + 1] = aconnector;
					}
				}
			}

			idx += mst_con_cnt;
		} else {
			idx++;
		}
	}

	/* Complete sorting. Assign relavant result to dm->secure_display_ctx.phy_id_mapping[]*/
	memset(dm->secure_display_ctx.phy_id_mapping, 0, sizeof(dm->secure_display_ctx.phy_id_mapping));
	for (idx = 0; idx < connector_cnt; idx++) {
		aconnector = sort_connector[idx];

		dm->secure_display_ctx.phy_id_mapping[idx].assigned = true;
		dm->secure_display_ctx.phy_id_mapping[idx].is_mst = false;
		dm->secure_display_ctx.phy_id_mapping[idx].enc_hw_inst = aconnector->dc_link->link_enc_hw_inst;

		if (sort_connector[idx]->mst_root) {
			dm->secure_display_ctx.phy_id_mapping[idx].is_mst = true;
			dm->secure_display_ctx.phy_id_mapping[idx].lct = aconnector->mst_output_port->parent->lct;
			dm->secure_display_ctx.phy_id_mapping[idx].port_num = aconnector->mst_output_port->port_num;
			memcpy(dm->secure_display_ctx.phy_id_mapping[idx].rad,
				aconnector->mst_output_port->parent->rad, sizeof(aconnector->mst_output_port->parent->rad));
		}
	}
	mutex_unlock(&ddev->mode_config.mutex);

	dm->secure_display_ctx.phy_id_mapping_cnt = connector_cnt;
	dm->secure_display_ctx.phy_mapping_updated = true;
}

static bool get_phy_id(struct amdgpu_display_manager *dm,
			struct amdgpu_dm_connector *aconnector, uint8_t *phy_id)
{
	int idx, idx_2;
	bool found = false;

	/*
	 * Assume secure display start after all connectors are probed. The connection
	 * config is static as well
	 */
	if (!dm->secure_display_ctx.phy_mapping_updated) {
		DRM_WARN("%s Should update the phy id table before get it's value", __func__);
		return false;
	}

	for (idx = 0; idx < dm->secure_display_ctx.phy_id_mapping_cnt; idx++) {
		if (!dm->secure_display_ctx.phy_id_mapping[idx].assigned) {
			DRM_ERROR("phy_id_mapping[%d] should be assigned", idx);
			return false;
		}

		if (aconnector->dc_link->link_enc_hw_inst ==
				dm->secure_display_ctx.phy_id_mapping[idx].enc_hw_inst) {
			if (!dm->secure_display_ctx.phy_id_mapping[idx].is_mst) {
				found = true;
				goto out;
			} else {
				/* Could caused by wrongly pass mst root connector */
				if (!aconnector->mst_output_port) {
					DRM_ERROR("%s Check mst case but connector without a port assigned", __func__);
					return false;
				}

				if (aconnector->mst_root &&
					aconnector->mst_root->mst_mgr.mst_primary == NULL) {
					DRM_WARN("%s pass in a stale mst connector", __func__);
				}

				if (aconnector->mst_output_port->parent->lct == dm->secure_display_ctx.phy_id_mapping[idx].lct &&
					aconnector->mst_output_port->port_num == dm->secure_display_ctx.phy_id_mapping[idx].port_num) {
					if (aconnector->mst_output_port->parent->lct == 1) {
						found = true;
						goto out;
					} else if (aconnector->mst_output_port->parent->lct > 1) {
						/* Check RAD */
						for (idx_2 = 0; idx_2 < aconnector->mst_output_port->parent->lct - 1; idx_2++) {
							int shift = (idx_2 % 2) ? 0 : 4;
							int port_num = (aconnector->mst_output_port->parent->rad[idx_2 / 2] >> shift) & 0xf;
							int port_num2 = (dm->secure_display_ctx.phy_id_mapping[idx].rad[idx_2 / 2] >> shift) & 0xf;

							if (port_num != port_num2)
								break;
						}

						if (idx_2 == aconnector->mst_output_port->parent->lct - 1) {
							found = true;
							goto out;
						}
					} else {
						DRM_ERROR("lCT should be >= 1");
						return false;
					}
				}
			}
		}
	}

out:
	if (found) {
		DRM_DEBUG_DRIVER("Associated secure display PHY ID as %d", idx);
		*phy_id = idx;
	} else {
		DRM_WARN("Can't find associated phy ID");
		return false;
	}

	return true;
}

static void amdgpu_dm_set_crc_window_default(struct drm_crtc *crtc, struct dc_stream_state *stream)
{
	struct drm_device *drm_dev = crtc->dev;
	struct amdgpu_display_manager *dm = &drm_to_adev(drm_dev)->dm;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	bool was_activated;
	struct amdgpu_dm_connector *aconnector;
	uint8_t phy_id;

	spin_lock_irq(&drm_dev->event_lock);
	was_activated = acrtc->dm_irq_params.window_param.activated;
	acrtc->dm_irq_params.window_param.x_start = 0;
	acrtc->dm_irq_params.window_param.y_start = 0;
	acrtc->dm_irq_params.window_param.x_end = 0;
	acrtc->dm_irq_params.window_param.y_end = 0;
	acrtc->dm_irq_params.window_param.activated = false;
	acrtc->dm_irq_params.window_param.update_win = false;
	acrtc->dm_irq_params.window_param.skip_frame_cnt = 0;
	spin_unlock_irq(&drm_dev->event_lock);

	/* Disable secure_display if it was enabled */
	if (was_activated) {
		/* stop ROI update on this crtc */
		flush_work(&dm->secure_display_ctx.crtc_ctx[crtc->index].notify_ta_work);
		flush_work(&dm->secure_display_ctx.crtc_ctx[crtc->index].forward_roi_work);

		aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;

		if (aconnector && get_phy_id(dm, aconnector, &phy_id))
			dc_stream_forward_crc_window(stream, NULL, phy_id, true);
		else
			DRM_DEBUG_DRIVER("%s Can't find matching phy id", __func__);
	}
}

static void amdgpu_dm_crtc_notify_ta_to_read(struct work_struct *work)
{
	struct secure_display_crtc_context *crtc_ctx;
	struct psp_context *psp;
	struct ta_securedisplay_cmd *securedisplay_cmd;
	struct drm_crtc *crtc;
	struct dc_stream_state *stream;
	struct amdgpu_dm_connector *aconnector;
	uint8_t phy_inst;
	struct amdgpu_display_manager *dm;
	int ret;

	crtc_ctx = container_of(work, struct secure_display_crtc_context, notify_ta_work);
	crtc = crtc_ctx->crtc;

	if (!crtc)
		return;

	psp = &drm_to_adev(crtc->dev)->psp;

	if (!psp->securedisplay_context.context.initialized) {
		DRM_DEBUG_DRIVER("Secure Display fails to notify PSP TA\n");
		return;
	}

	dm = &drm_to_adev(crtc->dev)->dm;
	stream = to_amdgpu_crtc(crtc)->dm_irq_params.stream;
	aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;
	if (!aconnector)
		return;

	mutex_lock(&crtc->dev->mode_config.mutex);
	if (!get_phy_id(dm, aconnector, &phy_inst)) {
		DRM_WARN("%s Can't find mapping phy id!", __func__);
		mutex_unlock(&crtc->dev->mode_config.mutex);
		return;
	}
	mutex_unlock(&crtc->dev->mode_config.mutex);

	/* need lock for multiple crtcs to use the command buffer */
	mutex_lock(&psp->securedisplay_context.mutex);

	psp_prep_securedisplay_cmd_buf(psp, &securedisplay_cmd,
						TA_SECUREDISPLAY_COMMAND__SEND_ROI_CRC);

	securedisplay_cmd->securedisplay_in_message.send_roi_crc.phy_id = phy_inst;

	/* PSP TA is expected to finish data transmission over I2C within current frame,
	 * even there are up to 4 crtcs request to send in this frame.
	 */
	ret = psp_securedisplay_invoke(psp, TA_SECUREDISPLAY_COMMAND__SEND_ROI_CRC);

	if (!ret) {
		if (securedisplay_cmd->status != TA_SECUREDISPLAY_STATUS__SUCCESS)
			psp_securedisplay_parse_resp_status(psp, securedisplay_cmd->status);
	}

	mutex_unlock(&psp->securedisplay_context.mutex);
}

static void
amdgpu_dm_forward_crc_window(struct work_struct *work)
{
	struct secure_display_crtc_context *crtc_ctx;
	struct amdgpu_display_manager *dm;
	struct drm_crtc *crtc;
	struct dc_stream_state *stream;
	struct amdgpu_dm_connector *aconnector;
	uint8_t phy_id;

	crtc_ctx = container_of(work, struct secure_display_crtc_context, forward_roi_work);
	crtc = crtc_ctx->crtc;

	if (!crtc)
		return;

	dm = &drm_to_adev(crtc->dev)->dm;
	stream = to_amdgpu_crtc(crtc)->dm_irq_params.stream;
	aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;

	if (!aconnector)
		return;

	mutex_lock(&crtc->dev->mode_config.mutex);
	if (!get_phy_id(dm, aconnector, &phy_id)) {
		DRM_WARN("%s Can't find mapping phy id!", __func__);
		mutex_unlock(&crtc->dev->mode_config.mutex);
		return;
	}
	mutex_unlock(&crtc->dev->mode_config.mutex);

	mutex_lock(&dm->dc_lock);
	dc_stream_forward_crc_window(stream, &crtc_ctx->rect,
		phy_id, false);
	mutex_unlock(&dm->dc_lock);
}

bool amdgpu_dm_crc_window_is_activated(struct drm_crtc *crtc)
{
	struct drm_device *drm_dev = crtc->dev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	bool ret = false;

	spin_lock_irq(&drm_dev->event_lock);
	ret = acrtc->dm_irq_params.window_param.activated;
	spin_unlock_irq(&drm_dev->event_lock);

	return ret;
}
#endif

int
amdgpu_dm_crtc_verify_crc_source(struct drm_crtc *crtc, const char *src_name,
				 size_t *values_cnt)
{
	enum amdgpu_dm_pipe_crc_source source = dm_parse_crc_source(src_name);

	if (source < 0) {
		DRM_DEBUG_DRIVER("Unknown CRC source %s for CRTC%d\n",
				 src_name, crtc->index);
		return -EINVAL;
	}

	*values_cnt = 3;
	return 0;
}

int amdgpu_dm_crtc_configure_crc_source(struct drm_crtc *crtc,
					struct dm_crtc_state *dm_crtc_state,
					enum amdgpu_dm_pipe_crc_source source)
{
	struct amdgpu_device *adev = drm_to_adev(crtc->dev);
	struct dc_stream_state *stream_state = dm_crtc_state->stream;
	bool enable = amdgpu_dm_is_valid_crc_source(source);
	int ret = 0;

	/* Configuration will be deferred to stream enable. */
	if (!stream_state)
		return -EINVAL;

	mutex_lock(&adev->dm.dc_lock);

	/* Enable or disable CRTC CRC generation */
	if (dm_is_crc_source_crtc(source) || source == AMDGPU_DM_PIPE_CRC_SOURCE_NONE) {
		if (!dc_stream_configure_crc(stream_state->ctx->dc,
					     stream_state, NULL, enable, enable)) {
			ret = -EINVAL;
			goto unlock;
		}
	}

	/* Configure dithering */
	if (!dm_need_crc_dither(source)) {
		dc_stream_set_dither_option(stream_state, DITHER_OPTION_TRUN8);
		dc_stream_set_dyn_expansion(stream_state->ctx->dc, stream_state,
					    DYN_EXPANSION_DISABLE);
	} else {
		dc_stream_set_dither_option(stream_state,
					    DITHER_OPTION_DEFAULT);
		dc_stream_set_dyn_expansion(stream_state->ctx->dc, stream_state,
					    DYN_EXPANSION_AUTO);
	}

unlock:
	mutex_unlock(&adev->dm.dc_lock);

	return ret;
}

int amdgpu_dm_crtc_set_crc_source(struct drm_crtc *crtc, const char *src_name)
{
	enum amdgpu_dm_pipe_crc_source source = dm_parse_crc_source(src_name);
	enum amdgpu_dm_pipe_crc_source cur_crc_src;
	struct drm_crtc_commit *commit;
	struct dm_crtc_state *crtc_state;
	struct drm_device *drm_dev = crtc->dev;
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	struct amdgpu_device *adev = drm_to_adev(drm_dev);
	struct amdgpu_display_manager *dm = &adev->dm;
#endif
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct drm_dp_aux *aux = NULL;
	bool enable = false;
	bool enabled = false;
	int ret = 0;

	if (source < 0) {
		DRM_DEBUG_DRIVER("Unknown CRC source %s for CRTC%d\n",
				 src_name, crtc->index);
		return -EINVAL;
	}

	ret = drm_modeset_lock(&crtc->mutex, NULL);
	if (ret)
		return ret;

	spin_lock(&crtc->commit_lock);
	commit = list_first_entry_or_null(&crtc->commit_list,
					  struct drm_crtc_commit, commit_entry);
	if (commit)
		drm_crtc_commit_get(commit);
	spin_unlock(&crtc->commit_lock);

	if (commit) {
		/*
		 * Need to wait for all outstanding programming to complete
		 * in commit tail since it can modify CRC related fields and
		 * hardware state. Since we're holding the CRTC lock we're
		 * guaranteed that no other commit work can be queued off
		 * before we modify the state below.
		 */
		ret = wait_for_completion_interruptible_timeout(
			&commit->hw_done, 10 * HZ);
		if (ret)
			goto cleanup;
	}

	enable = amdgpu_dm_is_valid_crc_source(source);
	crtc_state = to_dm_crtc_state(crtc->state);
	spin_lock_irq(&drm_dev->event_lock);
	cur_crc_src = acrtc->dm_irq_params.crc_src;
	spin_unlock_irq(&drm_dev->event_lock);

	/*
	 * USER REQ SRC | CURRENT SRC | BEHAVIOR
	 * -----------------------------
	 * None         | None        | Do nothing
	 * None         | CRTC        | Disable CRTC CRC, set default to dither
	 * None         | DPRX        | Disable DPRX CRC, need 'aux', set default to dither
	 * None         | CRTC DITHER | Disable CRTC CRC
	 * None         | DPRX DITHER | Disable DPRX CRC, need 'aux'
	 * CRTC         | XXXX        | Enable CRTC CRC, no dither
	 * DPRX         | XXXX        | Enable DPRX CRC, need 'aux', no dither
	 * CRTC DITHER  | XXXX        | Enable CRTC CRC, set dither
	 * DPRX DITHER  | XXXX        | Enable DPRX CRC, need 'aux', set dither
	 */
	if (dm_is_crc_source_dprx(source) ||
	    (source == AMDGPU_DM_PIPE_CRC_SOURCE_NONE &&
	     dm_is_crc_source_dprx(cur_crc_src))) {
		struct amdgpu_dm_connector *aconn = NULL;
		struct drm_connector *connector;
		struct drm_connector_list_iter conn_iter;

		drm_connector_list_iter_begin(crtc->dev, &conn_iter);
		drm_for_each_connector_iter(connector, &conn_iter) {
			if (!connector->state || connector->state->crtc != crtc)
				continue;

			if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
				continue;

			aconn = to_amdgpu_dm_connector(connector);
			break;
		}
		drm_connector_list_iter_end(&conn_iter);

		if (!aconn) {
			DRM_DEBUG_DRIVER("No amd connector matching CRTC-%d\n", crtc->index);
			ret = -EINVAL;
			goto cleanup;
		}

		aux = (aconn->mst_output_port) ? &aconn->mst_output_port->aux : &aconn->dm_dp_aux.aux;

		if (!aux) {
			DRM_DEBUG_DRIVER("No dp aux for amd connector\n");
			ret = -EINVAL;
			goto cleanup;
		}

		if ((aconn->base.connector_type != DRM_MODE_CONNECTOR_DisplayPort) &&
				(aconn->base.connector_type != DRM_MODE_CONNECTOR_eDP)) {
			DRM_DEBUG_DRIVER("No DP connector available for CRC source\n");
			ret = -EINVAL;
			goto cleanup;
		}

	}

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	/* Reset secure_display when we change crc source from debugfs */
	amdgpu_dm_set_crc_window_default(crtc, crtc_state->stream);
#endif

	if (amdgpu_dm_crtc_configure_crc_source(crtc, crtc_state, source)) {
		ret = -EINVAL;
		goto cleanup;
	}

	/*
	 * Reading the CRC requires the vblank interrupt handler to be
	 * enabled. Keep a reference until CRC capture stops.
	 */
	enabled = amdgpu_dm_is_valid_crc_source(cur_crc_src);
	if (!enabled && enable) {
		ret = drm_crtc_vblank_get(crtc);
		if (ret)
			goto cleanup;

		if (dm_is_crc_source_dprx(source)) {
			if (drm_dp_start_crc(aux, crtc)) {
				DRM_DEBUG_DRIVER("dp start crc failed\n");
				ret = -EINVAL;
				goto cleanup;
			}
		}
	} else if (enabled && !enable) {
		drm_crtc_vblank_put(crtc);
		if (dm_is_crc_source_dprx(source)) {
			if (drm_dp_stop_crc(aux)) {
				DRM_DEBUG_DRIVER("dp stop crc failed\n");
				ret = -EINVAL;
				goto cleanup;
			}
		}
	}

	spin_lock_irq(&drm_dev->event_lock);
	acrtc->dm_irq_params.crc_src = source;
	spin_unlock_irq(&drm_dev->event_lock);

	/* Reset crc_skipped on dm state */
	crtc_state->crc_skip_count = 0;

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	/* Initialize phy id mapping table for secure display*/
	if (!dm->secure_display_ctx.phy_mapping_updated)
		update_phy_id_mapping(adev);
#endif

cleanup:
	if (commit)
		drm_crtc_commit_put(commit);

	drm_modeset_unlock(&crtc->mutex);

	return ret;
}

/**
 * amdgpu_dm_crtc_handle_crc_irq: Report to DRM the CRC on given CRTC.
 * @crtc: DRM CRTC object.
 *
 * This function should be called at the end of a vblank, when the fb has been
 * fully processed through the pipe.
 */
void amdgpu_dm_crtc_handle_crc_irq(struct drm_crtc *crtc)
{
	struct dm_crtc_state *crtc_state;
	struct dc_stream_state *stream_state;
	struct drm_device *drm_dev = NULL;
	enum amdgpu_dm_pipe_crc_source cur_crc_src;
	struct amdgpu_crtc *acrtc = NULL;
	uint32_t crcs[3];
	unsigned long flags;

	if (crtc == NULL)
		return;

	crtc_state = to_dm_crtc_state(crtc->state);
	stream_state = crtc_state->stream;
	acrtc = to_amdgpu_crtc(crtc);
	drm_dev = crtc->dev;

	spin_lock_irqsave(&drm_dev->event_lock, flags);
	cur_crc_src = acrtc->dm_irq_params.crc_src;
	spin_unlock_irqrestore(&drm_dev->event_lock, flags);

	/* Early return if CRC capture is not enabled. */
	if (!amdgpu_dm_is_valid_crc_source(cur_crc_src))
		return;

	/*
	 * Since flipping and crc enablement happen asynchronously, we - more
	 * often than not - will be returning an 'uncooked' crc on first frame.
	 * Probably because hw isn't ready yet. For added security, skip the
	 * first two CRC values.
	 */
	if (crtc_state->crc_skip_count < 2) {
		crtc_state->crc_skip_count += 1;
		return;
	}

	if (dm_is_crc_source_crtc(cur_crc_src)) {
		if (!dc_stream_get_crc(stream_state->ctx->dc, stream_state,
				       &crcs[0], &crcs[1], &crcs[2]))
			return;

		drm_crtc_add_crc_entry(crtc, true,
				       drm_crtc_accurate_vblank_count(crtc), crcs);
	}
}

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
void amdgpu_dm_crtc_handle_crc_window_irq(struct drm_crtc *crtc)
{
	struct drm_device *drm_dev = NULL;
	enum amdgpu_dm_pipe_crc_source cur_crc_src;
	struct amdgpu_crtc *acrtc = NULL;
	struct amdgpu_device *adev = NULL;
	struct secure_display_crtc_context *crtc_ctx = NULL;
	unsigned long flags1;

	if (crtc == NULL)
		return;

	acrtc = to_amdgpu_crtc(crtc);
	adev = drm_to_adev(crtc->dev);
	drm_dev = crtc->dev;

	spin_lock_irqsave(&drm_dev->event_lock, flags1);
	cur_crc_src = acrtc->dm_irq_params.crc_src;

	/* Early return if CRC capture is not enabled. */
	if (!amdgpu_dm_is_valid_crc_source(cur_crc_src) ||
		!dm_is_crc_source_crtc(cur_crc_src))
		goto cleanup;

	if (!acrtc->dm_irq_params.window_param.activated)
		goto cleanup;

	if (acrtc->dm_irq_params.window_param.skip_frame_cnt) {
		acrtc->dm_irq_params.window_param.skip_frame_cnt -= 1;
		goto cleanup;
	}

	crtc_ctx = &adev->dm.secure_display_ctx.crtc_ctx[acrtc->crtc_id];
	if (WARN_ON(crtc_ctx->crtc != crtc)) {
		/* We have set the crtc when creating secure_display_crtc_context,
		 * don't expect it to be changed here.
		 */
		crtc_ctx->crtc = crtc;
	}

	if (acrtc->dm_irq_params.window_param.update_win) {
		/* prepare work for dmub to update ROI */
		crtc_ctx->rect.x = acrtc->dm_irq_params.window_param.x_start;
		crtc_ctx->rect.y = acrtc->dm_irq_params.window_param.y_start;
		crtc_ctx->rect.width = acrtc->dm_irq_params.window_param.x_end -
								acrtc->dm_irq_params.window_param.x_start;
		crtc_ctx->rect.height = acrtc->dm_irq_params.window_param.y_end -
								acrtc->dm_irq_params.window_param.y_start;
		schedule_work(&crtc_ctx->forward_roi_work);

		acrtc->dm_irq_params.window_param.update_win = false;

		/* Statically skip 1 frame, because we may need to wait below things
		 * before sending ROI to dmub:
		 * 1. We defer the work by using system workqueue.
		 * 2. We may need to wait for dc_lock before accessing dmub.
		 */
		acrtc->dm_irq_params.window_param.skip_frame_cnt = 1;

	} else {
		/* prepare work for psp to read ROI/CRC and send to I2C */
		schedule_work(&crtc_ctx->notify_ta_work);
	}

cleanup:
	spin_unlock_irqrestore(&drm_dev->event_lock, flags1);
}

void amdgpu_dm_crtc_secure_display_create_contexts(struct amdgpu_device *adev)
{
	struct secure_display_crtc_context *crtc_ctx = NULL;
	int i;

	crtc_ctx = kcalloc(adev->mode_info.num_crtc,
				      sizeof(struct secure_display_crtc_context),
				      GFP_KERNEL);

	if (!crtc_ctx) {
		adev->dm.secure_display_ctx.crtc_ctx = NULL;
		return;
	}

	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		INIT_WORK(&crtc_ctx[i].forward_roi_work, amdgpu_dm_forward_crc_window);
		INIT_WORK(&crtc_ctx[i].notify_ta_work, amdgpu_dm_crtc_notify_ta_to_read);
		crtc_ctx[i].crtc = &adev->mode_info.crtcs[i]->base;
	}

	adev->dm.secure_display_ctx.crtc_ctx = crtc_ctx;
	return;
}
#endif
