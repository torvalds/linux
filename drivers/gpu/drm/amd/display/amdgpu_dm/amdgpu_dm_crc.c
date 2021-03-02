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
static void amdgpu_dm_set_crc_window_default(struct drm_crtc *crtc)
{
	struct drm_device *drm_dev = crtc->dev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

	spin_lock_irq(&drm_dev->event_lock);
	acrtc->dm_irq_params.crc_window.x_start = 0;
	acrtc->dm_irq_params.crc_window.y_start = 0;
	acrtc->dm_irq_params.crc_window.x_end = 0;
	acrtc->dm_irq_params.crc_window.y_end = 0;
	acrtc->dm_irq_params.crc_window.activated = false;
	acrtc->dm_irq_params.crc_window.update_win = false;
	acrtc->dm_irq_params.crc_window.skip_frame_cnt = 0;
	spin_unlock_irq(&drm_dev->event_lock);
}

static void amdgpu_dm_crtc_notify_ta_to_read(struct work_struct *work)
{
	struct crc_rd_work *crc_rd_wrk;
	struct amdgpu_device *adev;
	struct psp_context *psp;
	struct securedisplay_cmd *securedisplay_cmd;
	struct drm_crtc *crtc;
	uint8_t phy_id;
	int ret;

	crc_rd_wrk = container_of(work, struct crc_rd_work, notify_ta_work);
	spin_lock_irq(&crc_rd_wrk->crc_rd_work_lock);
	crtc = crc_rd_wrk->crtc;

	if (!crtc) {
		spin_unlock_irq(&crc_rd_wrk->crc_rd_work_lock);
		return;
	}

	adev = drm_to_adev(crtc->dev);
	psp = &adev->psp;
	phy_id = crc_rd_wrk->phy_inst;
	spin_unlock_irq(&crc_rd_wrk->crc_rd_work_lock);

	psp_prep_securedisplay_cmd_buf(psp, &securedisplay_cmd,
						TA_SECUREDISPLAY_COMMAND__SEND_ROI_CRC);
	securedisplay_cmd->securedisplay_in_message.send_roi_crc.phy_id =
						phy_id;
	ret = psp_securedisplay_invoke(psp, TA_SECUREDISPLAY_COMMAND__SEND_ROI_CRC);
	if (!ret) {
		if (securedisplay_cmd->status != TA_SECUREDISPLAY_STATUS__SUCCESS) {
			psp_securedisplay_parse_resp_status(psp, securedisplay_cmd->status);
		}
	}
}

bool amdgpu_dm_crc_window_is_activated(struct drm_crtc *crtc)
{
	struct drm_device *drm_dev = crtc->dev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	bool ret = false;

	spin_lock_irq(&drm_dev->event_lock);
	ret = acrtc->dm_irq_params.crc_window.activated;
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
		return 0;

	mutex_lock(&adev->dm.dc_lock);

	/* Enable CRTC CRC generation if necessary. */
	if (dm_is_crc_source_crtc(source) || source == AMDGPU_DM_PIPE_CRC_SOURCE_NONE) {
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
		if (!enable) {
			if (adev->dm.crc_rd_wrk) {
				flush_work(&adev->dm.crc_rd_wrk->notify_ta_work);
				spin_lock_irq(&adev->dm.crc_rd_wrk->crc_rd_work_lock);
				if (adev->dm.crc_rd_wrk->crtc == crtc) {
					dc_stream_stop_dmcu_crc_win_update(stream_state->ctx->dc,
									dm_crtc_state->stream);
					adev->dm.crc_rd_wrk->crtc = NULL;
				}
				spin_unlock_irq(&adev->dm.crc_rd_wrk->crc_rd_work_lock);
			}
		}
#endif
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

			aconn = to_amdgpu_dm_connector(connector);
			break;
		}
		drm_connector_list_iter_end(&conn_iter);

		if (!aconn) {
			DRM_DEBUG_DRIVER("No amd connector matching CRTC-%d\n", crtc->index);
			ret = -EINVAL;
			goto cleanup;
		}

		aux = &aconn->dm_dp_aux.aux;

		if (!aux) {
			DRM_DEBUG_DRIVER("No dp aux for amd connector\n");
			ret = -EINVAL;
			goto cleanup;
		}
	}

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	amdgpu_dm_set_crc_window_default(crtc);
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
	struct dc_stream_state *stream_state;
	struct drm_device *drm_dev = NULL;
	enum amdgpu_dm_pipe_crc_source cur_crc_src;
	struct amdgpu_crtc *acrtc = NULL;
	struct amdgpu_device *adev = NULL;
	struct crc_rd_work *crc_rd_wrk = NULL;
	struct crc_params *crc_window = NULL, tmp_window;
	unsigned long flags;
	struct crtc_position position;
	uint32_t v_blank;
	uint32_t v_back_porch;
	uint32_t crc_window_latch_up_line;
	struct dc_crtc_timing *timing_out;

	if (crtc == NULL)
		return;

	acrtc = to_amdgpu_crtc(crtc);
	adev = drm_to_adev(crtc->dev);
	drm_dev = crtc->dev;

	spin_lock_irqsave(&drm_dev->event_lock, flags);
	stream_state = acrtc->dm_irq_params.stream;
	cur_crc_src = acrtc->dm_irq_params.crc_src;
	timing_out = &stream_state->timing;

	/* Early return if CRC capture is not enabled. */
	if (!amdgpu_dm_is_valid_crc_source(cur_crc_src))
		goto cleanup;

	if (dm_is_crc_source_crtc(cur_crc_src)) {
		if (acrtc->dm_irq_params.crc_window.activated) {
			if (acrtc->dm_irq_params.crc_window.update_win) {
				if (acrtc->dm_irq_params.crc_window.skip_frame_cnt) {
					acrtc->dm_irq_params.crc_window.skip_frame_cnt -= 1;
					goto cleanup;
				}
				crc_window = &tmp_window;

				tmp_window.windowa_x_start =
							acrtc->dm_irq_params.crc_window.x_start;
				tmp_window.windowa_y_start =
							acrtc->dm_irq_params.crc_window.y_start;
				tmp_window.windowa_x_end =
							acrtc->dm_irq_params.crc_window.x_end;
				tmp_window.windowa_y_end =
							acrtc->dm_irq_params.crc_window.y_end;
				tmp_window.windowb_x_start =
							acrtc->dm_irq_params.crc_window.x_start;
				tmp_window.windowb_y_start =
							acrtc->dm_irq_params.crc_window.y_start;
				tmp_window.windowb_x_end =
							acrtc->dm_irq_params.crc_window.x_end;
				tmp_window.windowb_y_end =
							acrtc->dm_irq_params.crc_window.y_end;

				dc_stream_forward_dmcu_crc_window(stream_state->ctx->dc,
									stream_state, crc_window);

				acrtc->dm_irq_params.crc_window.update_win = false;

				dc_stream_get_crtc_position(stream_state->ctx->dc, &stream_state, 1,
					&position.vertical_count,
					&position.nominal_vcount);

				v_blank = timing_out->v_total - timing_out->v_border_top -
					timing_out->v_addressable - timing_out->v_border_bottom;

				v_back_porch = v_blank - timing_out->v_front_porch -
					timing_out->v_sync_width;

				crc_window_latch_up_line = v_back_porch + timing_out->v_sync_width;

				/* take 3 lines margin*/
				if ((position.vertical_count + 3) >= crc_window_latch_up_line)
					acrtc->dm_irq_params.crc_window.skip_frame_cnt = 1;
				else
					acrtc->dm_irq_params.crc_window.skip_frame_cnt = 0;
			} else {
				if (acrtc->dm_irq_params.crc_window.skip_frame_cnt == 0) {
					if (adev->dm.crc_rd_wrk) {
						crc_rd_wrk = adev->dm.crc_rd_wrk;
						spin_lock_irq(&crc_rd_wrk->crc_rd_work_lock);
						crc_rd_wrk->phy_inst =
							stream_state->link->link_enc_hw_inst;
						spin_unlock_irq(&crc_rd_wrk->crc_rd_work_lock);
						schedule_work(&crc_rd_wrk->notify_ta_work);
					}
				} else {
					acrtc->dm_irq_params.crc_window.skip_frame_cnt -= 1;
				}
			}
		}
	}

cleanup:
	spin_unlock_irqrestore(&drm_dev->event_lock, flags);
}

void amdgpu_dm_crtc_secure_display_resume(struct amdgpu_device *adev)
{
	struct drm_crtc *crtc;
	enum amdgpu_dm_pipe_crc_source cur_crc_src;
	struct crc_rd_work *crc_rd_wrk = adev->dm.crc_rd_wrk;
	struct crc_window_parm cur_crc_window;
	struct amdgpu_crtc *acrtc = NULL;

	drm_for_each_crtc(crtc, &adev->ddev) {
		acrtc = to_amdgpu_crtc(crtc);

		spin_lock_irq(&adev_to_drm(adev)->event_lock);
		cur_crc_src = acrtc->dm_irq_params.crc_src;
		cur_crc_window = acrtc->dm_irq_params.crc_window;
		spin_unlock_irq(&adev_to_drm(adev)->event_lock);

		if (amdgpu_dm_is_valid_crc_source(cur_crc_src)) {
			amdgpu_dm_crtc_set_crc_source(crtc,
				pipe_crc_sources[cur_crc_src]);
			spin_lock_irq(&adev_to_drm(adev)->event_lock);
			acrtc->dm_irq_params.crc_window = cur_crc_window;
			if (acrtc->dm_irq_params.crc_window.activated) {
				acrtc->dm_irq_params.crc_window.update_win = true;
				acrtc->dm_irq_params.crc_window.skip_frame_cnt = 1;
				spin_lock_irq(&crc_rd_wrk->crc_rd_work_lock);
				crc_rd_wrk->crtc = crtc;
				spin_unlock_irq(&crc_rd_wrk->crc_rd_work_lock);
			}
			spin_unlock_irq(&adev_to_drm(adev)->event_lock);
		}
	}
}

void amdgpu_dm_crtc_secure_display_suspend(struct amdgpu_device *adev)
{
	struct drm_crtc *crtc;
	struct crc_window_parm cur_crc_window;
	enum amdgpu_dm_pipe_crc_source cur_crc_src;
	struct amdgpu_crtc *acrtc = NULL;

	drm_for_each_crtc(crtc, &adev->ddev) {
		acrtc = to_amdgpu_crtc(crtc);

		spin_lock_irq(&adev_to_drm(adev)->event_lock);
		cur_crc_src = acrtc->dm_irq_params.crc_src;
		cur_crc_window = acrtc->dm_irq_params.crc_window;
		cur_crc_window.update_win = false;
		spin_unlock_irq(&adev_to_drm(adev)->event_lock);

		if (amdgpu_dm_is_valid_crc_source(cur_crc_src)) {
			amdgpu_dm_crtc_set_crc_source(crtc, NULL);
			spin_lock_irq(&adev_to_drm(adev)->event_lock);
			/* For resume to set back crc source*/
			acrtc->dm_irq_params.crc_src = cur_crc_src;
			acrtc->dm_irq_params.crc_window = cur_crc_window;
			spin_unlock_irq(&adev_to_drm(adev)->event_lock);
		}
	}

}

struct crc_rd_work *amdgpu_dm_crtc_secure_display_create_work(void)
{
	struct crc_rd_work *crc_rd_wrk = NULL;

	crc_rd_wrk = kzalloc(sizeof(*crc_rd_wrk), GFP_KERNEL);

	if (!crc_rd_wrk)
		return NULL;

	spin_lock_init(&crc_rd_wrk->crc_rd_work_lock);
	INIT_WORK(&crc_rd_wrk->notify_ta_work, amdgpu_dm_crtc_notify_ta_to_read);

	return crc_rd_wrk;
}
#endif
