/*
 * Support for Clovertrail PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <media/videobuf-vmalloc.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>

#include "mmu/isp_mmu.h"
#include "mmu/sh_mmu_mrfld.h"
#include "hmm/hmm_bo.h"
#include "hmm/hmm.h"

#include "atomisp_compat.h"
#include "atomisp_internal.h"
#include "atomisp_cmd.h"
#include "atomisp-regs.h"
#include "atomisp_fops.h"
#include "atomisp_ioctl.h"
#include "atomisp_acc.h"

#include "hrt/hive_isp_css_mm_hrt.h"

#include <asm/intel-mid.h>

#include "ia_css_debug.h"
#include "ia_css_isp_param.h"
#include "sh_css_hrt.h"
#include "ia_css_isys.h"

#include <linux/pm_runtime.h>

/* Assume max number of ACC stages */
#define MAX_ACC_STAGES	20

/* Ideally, this should come from CSS headers */
#define NO_LINK -1

/*
 * to serialize MMIO access , this is due to ISP2400 silicon issue Sighting
 * #4684168, if concurrency access happened, system may hard hang.
 */
static DEFINE_SPINLOCK(mmio_lock);

enum frame_info_type {
	ATOMISP_CSS_VF_FRAME,
	ATOMISP_CSS_SECOND_VF_FRAME,
	ATOMISP_CSS_OUTPUT_FRAME,
	ATOMISP_CSS_SECOND_OUTPUT_FRAME,
	ATOMISP_CSS_RAW_FRAME,
};

struct bayer_ds_factor {
	unsigned int numerator;
	unsigned int denominator;
};

void atomisp_css_debug_dump_sp_sw_debug_info(void)
{
	ia_css_debug_dump_sp_sw_debug_info();
}

void atomisp_css_debug_dump_debug_info(const char *context)
{
	ia_css_debug_dump_debug_info(context);
}

void atomisp_css_debug_set_dtrace_level(const unsigned int trace_level)
{
	ia_css_debug_set_dtrace_level(trace_level);
}

unsigned int atomisp_css_debug_get_dtrace_level(void)
{
	return ia_css_debug_trace_level;
}

void atomisp_css2_hw_store_8(hrt_address addr, uint8_t data)
{
	unsigned long flags;

	spin_lock_irqsave(&mmio_lock, flags);
	_hrt_master_port_store_8(addr, data);
	spin_unlock_irqrestore(&mmio_lock, flags);
}

static void atomisp_css2_hw_store_16(hrt_address addr, uint16_t data)
{
	unsigned long flags;

	spin_lock_irqsave(&mmio_lock, flags);
	_hrt_master_port_store_16(addr, data);
	spin_unlock_irqrestore(&mmio_lock, flags);
}

static void atomisp_css2_hw_store_32(hrt_address addr, uint32_t data)
{
	unsigned long flags;

	spin_lock_irqsave(&mmio_lock, flags);
	_hrt_master_port_store_32(addr, data);
	spin_unlock_irqrestore(&mmio_lock, flags);
}

static uint8_t atomisp_css2_hw_load_8(hrt_address addr)
{
	unsigned long flags;
	uint8_t ret;

	spin_lock_irqsave(&mmio_lock, flags);
	ret = _hrt_master_port_load_8(addr);
	spin_unlock_irqrestore(&mmio_lock, flags);
	return ret;
}

uint16_t atomisp_css2_hw_load_16(hrt_address addr)
{
	unsigned long flags;
	uint16_t ret;

	spin_lock_irqsave(&mmio_lock, flags);
	ret = _hrt_master_port_load_16(addr);
	spin_unlock_irqrestore(&mmio_lock, flags);
	return ret;
}
uint32_t atomisp_css2_hw_load_32(hrt_address addr)
{
	unsigned long flags;
	uint32_t ret;

	spin_lock_irqsave(&mmio_lock, flags);
	ret = _hrt_master_port_load_32(addr);
	spin_unlock_irqrestore(&mmio_lock, flags);
	return ret;
}

static void atomisp_css2_hw_store(hrt_address addr,
				  const void *from, uint32_t n)
{
	unsigned long flags;
	unsigned i;
	unsigned int _to = (unsigned int)addr;
	const char *_from = (const char *)from;

	spin_lock_irqsave(&mmio_lock, flags);
	for (i = 0; i < n; i++, _to++, _from++)
		_hrt_master_port_store_8(_to , *_from);
	spin_unlock_irqrestore(&mmio_lock, flags);
}

static void atomisp_css2_hw_load(hrt_address addr, void *to, uint32_t n)
{
	unsigned long flags;
	unsigned i;
	char *_to = (char *)to;
	unsigned int _from = (unsigned int)addr;

	spin_lock_irqsave(&mmio_lock, flags);
	for (i = 0; i < n; i++, _to++, _from++)
		*_to = _hrt_master_port_load_8(_from);
	spin_unlock_irqrestore(&mmio_lock, flags);
}

static int atomisp_css2_dbg_print(const char *fmt, va_list args)
{
	vprintk(fmt, args);
	return 0;
}

static int atomisp_css2_dbg_ftrace_print(const char *fmt, va_list args)
{
	ftrace_vprintk(fmt, args);
	return 0;
}

static int atomisp_css2_err_print(const char *fmt, va_list args)
{
	vprintk(fmt, args);
	return 0;
}

void atomisp_store_uint32(hrt_address addr, uint32_t data)
{
	atomisp_css2_hw_store_32(addr, data);
}

void atomisp_load_uint32(hrt_address addr, uint32_t *data)
{
	*data = atomisp_css2_hw_load_32(addr);
}
static int hmm_get_mmu_base_addr(unsigned int *mmu_base_addr)
{
	if (sh_mmu_mrfld.get_pd_base == NULL) {
		dev_err(atomisp_dev, "get mmu base address failed.\n");
		return -EINVAL;
	}

	*mmu_base_addr = sh_mmu_mrfld.get_pd_base(&bo_device.mmu,
					bo_device.mmu.base_address);
	return 0;
}

static void atomisp_isp_parameters_clean_up(
				struct atomisp_css_isp_config *config)
{
	/*
	 * Set NULL to configs pointer to avoid they are set into isp again when
	 * some configs are changed and need to be updated later.
	 */
	memset(config, 0, sizeof(*config));
}

static void __dump_pipe_config(struct atomisp_sub_device *asd,
			       struct atomisp_stream_env *stream_env,
			       unsigned int pipe_id)
{
	struct atomisp_device *isp = asd->isp;
	if (stream_env->pipes[pipe_id]) {
		struct ia_css_pipe_config *p_config;
		struct ia_css_pipe_extra_config *pe_config;
		p_config = &stream_env->pipe_configs[pipe_id];
		pe_config = &stream_env->pipe_extra_configs[pipe_id];
		dev_dbg(isp->dev, "dumping pipe[%d] config:\n", pipe_id);
		dev_dbg(isp->dev,
			 "pipe_config.pipe_mode:%d.\n", p_config->mode);
		dev_dbg(isp->dev,
			 "pipe_config.output_info[0] w=%d, h=%d.\n",
			 p_config->output_info[0].res.width,
			 p_config->output_info[0].res.height);
		dev_dbg(isp->dev,
			 "pipe_config.vf_pp_in_res w=%d, h=%d.\n",
			 p_config->vf_pp_in_res.width,
			 p_config->vf_pp_in_res.height);
		dev_dbg(isp->dev,
			 "pipe_config.capt_pp_in_res w=%d, h=%d.\n",
			 p_config->capt_pp_in_res.width,
			 p_config->capt_pp_in_res.height);
		dev_dbg(isp->dev,
			 "pipe_config.output.padded w=%d.\n",
			 p_config->output_info[0].padded_width);
		dev_dbg(isp->dev,
			 "pipe_config.vf_output_info[0] w=%d, h=%d.\n",
			 p_config->vf_output_info[0].res.width,
			 p_config->vf_output_info[0].res.height);
		dev_dbg(isp->dev,
			 "pipe_config.bayer_ds_out_res w=%d, h=%d.\n",
			 p_config->bayer_ds_out_res.width,
			 p_config->bayer_ds_out_res.height);
		dev_dbg(isp->dev,
			 "pipe_config.envelope w=%d, h=%d.\n",
			 p_config->dvs_envelope.width,
			 p_config->dvs_envelope.height);
		dev_dbg(isp->dev,
			 "pipe_config.dvs_frame_delay=%d.\n",
			 p_config->dvs_frame_delay);
		dev_dbg(isp->dev,
			 "pipe_config.isp_pipe_version:%d.\n",
			p_config->isp_pipe_version);
		dev_dbg(isp->dev,
			 "pipe_config.acc_extension=%p.\n",
			 p_config->acc_extension);
		dev_dbg(isp->dev,
			 "pipe_config.acc_stages=%p.\n",
			 p_config->acc_stages);
		dev_dbg(isp->dev,
			 "pipe_config.num_acc_stages=%d.\n",
			 p_config->num_acc_stages);
		dev_dbg(isp->dev,
			 "pipe_config.acc_num_execs=%d.\n",
			 p_config->acc_num_execs);
		dev_dbg(isp->dev,
			 "pipe_config.default_capture_config.capture_mode=%d.\n",
			 p_config->default_capture_config.mode);
		dev_dbg(isp->dev,
			 "pipe_config.enable_dz=%d.\n",
			 p_config->enable_dz);
		dev_dbg(isp->dev,
			 "pipe_config.default_capture_config.enable_xnr=%d.\n",
			 p_config->default_capture_config.enable_xnr);
		dev_dbg(isp->dev,
			 "dumping pipe[%d] extra config:\n", pipe_id);
		dev_dbg(isp->dev,
			 "pipe_extra_config.enable_raw_binning:%d.\n",
			 pe_config->enable_raw_binning);
		dev_dbg(isp->dev,
			 "pipe_extra_config.enable_yuv_ds:%d.\n",
			 pe_config->enable_yuv_ds);
		dev_dbg(isp->dev,
			 "pipe_extra_config.enable_high_speed:%d.\n",
			 pe_config->enable_high_speed);
		dev_dbg(isp->dev,
			 "pipe_extra_config.enable_dvs_6axis:%d.\n",
			 pe_config->enable_dvs_6axis);
		dev_dbg(isp->dev,
			 "pipe_extra_config.enable_reduced_pipe:%d.\n",
			 pe_config->enable_reduced_pipe);
		dev_dbg(isp->dev,
			 "pipe_(extra_)config.enable_dz:%d.\n",
			 p_config->enable_dz);
		dev_dbg(isp->dev,
			 "pipe_extra_config.disable_vf_pp:%d.\n",
			 pe_config->disable_vf_pp);
	}
}

static void __dump_stream_config(struct atomisp_sub_device *asd,
				struct atomisp_stream_env *stream_env)
{
	struct atomisp_device *isp = asd->isp;
	struct ia_css_stream_config *s_config;
	int j;
	bool valid_stream = false;

	for (j = 0; j < IA_CSS_PIPE_ID_NUM; j++) {
		if (stream_env->pipes[j]) {
			__dump_pipe_config(asd, stream_env, j);
			valid_stream = true;
		}
	}
	if (!valid_stream)
		return;
	s_config = &stream_env->stream_config;
	dev_dbg(isp->dev, "stream_config.mode=%d.\n", s_config->mode);

	if (s_config->mode == IA_CSS_INPUT_MODE_SENSOR ||
	    s_config->mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR) {
		dev_dbg(isp->dev, "stream_config.source.port.port=%d.\n",
				s_config->source.port.port);
		dev_dbg(isp->dev, "stream_config.source.port.num_lanes=%d.\n",
				s_config->source.port.num_lanes);
		dev_dbg(isp->dev, "stream_config.source.port.timeout=%d.\n",
				s_config->source.port.timeout);
		dev_dbg(isp->dev, "stream_config.source.port.rxcount=0x%x.\n",
				s_config->source.port.rxcount);
		dev_dbg(isp->dev, "stream_config.source.port.compression.type=%d.\n",
				s_config->source.port.compression.type);
		dev_dbg(isp->dev, "stream_config.source.port.compression.compressed_bits_per_pixel=%d.\n",
				s_config->source.port.compression.
				compressed_bits_per_pixel);
		dev_dbg(isp->dev, "stream_config.source.port.compression.uncompressed_bits_per_pixel=%d.\n",
				s_config->source.port.compression.
				uncompressed_bits_per_pixel);
	} else if (s_config->mode == IA_CSS_INPUT_MODE_TPG) {
		dev_dbg(isp->dev, "stream_config.source.tpg.id=%d.\n",
				s_config->source.tpg.id);
		dev_dbg(isp->dev, "stream_config.source.tpg.mode=%d.\n",
				s_config->source.tpg.mode);
		dev_dbg(isp->dev, "stream_config.source.tpg.x_mask=%d.\n",
				s_config->source.tpg.x_mask);
		dev_dbg(isp->dev, "stream_config.source.tpg.x_delta=%d.\n",
				s_config->source.tpg.x_delta);
		dev_dbg(isp->dev, "stream_config.source.tpg.y_mask=%d.\n",
				s_config->source.tpg.y_mask);
		dev_dbg(isp->dev, "stream_config.source.tpg.y_delta=%d.\n",
				s_config->source.tpg.y_delta);
		dev_dbg(isp->dev, "stream_config.source.tpg.xy_mask=%d.\n",
				s_config->source.tpg.xy_mask);
	} else if (s_config->mode == IA_CSS_INPUT_MODE_PRBS) {
		dev_dbg(isp->dev, "stream_config.source.prbs.id=%d.\n",
				s_config->source.prbs.id);
		dev_dbg(isp->dev, "stream_config.source.prbs.h_blank=%d.\n",
				s_config->source.prbs.h_blank);
		dev_dbg(isp->dev, "stream_config.source.prbs.v_blank=%d.\n",
				s_config->source.prbs.v_blank);
		dev_dbg(isp->dev, "stream_config.source.prbs.seed=%d.\n",
				s_config->source.prbs.seed);
		dev_dbg(isp->dev, "stream_config.source.prbs.seed1=%d.\n",
				s_config->source.prbs.seed1);
	}

	for (j = 0; j < IA_CSS_STREAM_MAX_ISYS_STREAM_PER_CH; j++) {
		dev_dbg(isp->dev, "stream_configisys_config[%d].input_res w=%d, h=%d.\n",
			j,
			s_config->isys_config[j].input_res.width,
			s_config->isys_config[j].input_res.height);

		dev_dbg(isp->dev, "stream_configisys_config[%d].linked_isys_stream_id=%d\n",
			j,
			s_config->isys_config[j].linked_isys_stream_id);

		dev_dbg(isp->dev, "stream_configisys_config[%d].format=%d\n",
			j,
			s_config->isys_config[j].format);

		dev_dbg(isp->dev, "stream_configisys_config[%d].valid=%d.\n",
			j,
			s_config->isys_config[j].valid);
	}

	dev_dbg(isp->dev, "stream_config.input_config.input_res w=%d, h=%d.\n",
		s_config->input_config.input_res.width,
		s_config->input_config.input_res.height);

	dev_dbg(isp->dev, "stream_config.input_config.effective_res w=%d, h=%d.\n",
		s_config->input_config.effective_res.width,
		s_config->input_config.effective_res.height);

	dev_dbg(isp->dev, "stream_config.input_config.format=%d\n",
		s_config->input_config.format);

	dev_dbg(isp->dev, "stream_config.input_config.bayer_order=%d.\n",
		s_config->input_config.bayer_order);

	dev_dbg(isp->dev, "stream_config.pixels_per_clock=%d.\n",
			s_config->pixels_per_clock);
	dev_dbg(isp->dev, "stream_config.online=%d.\n", s_config->online);
	dev_dbg(isp->dev, "stream_config.continuous=%d.\n",
			s_config->continuous);
	dev_dbg(isp->dev, "stream_config.disable_cont_viewfinder=%d.\n",
			s_config->disable_cont_viewfinder);
	dev_dbg(isp->dev, "stream_config.channel_id=%d.\n",
			s_config->channel_id);
	dev_dbg(isp->dev, "stream_config.init_num_cont_raw_buf=%d.\n",
			s_config->init_num_cont_raw_buf);
	dev_dbg(isp->dev, "stream_config.target_num_cont_raw_buf=%d.\n",
			s_config->target_num_cont_raw_buf);
	dev_dbg(isp->dev, "stream_config.left_padding=%d.\n",
			s_config->left_padding);
	dev_dbg(isp->dev, "stream_config.sensor_binning_factor=%d.\n",
			s_config->sensor_binning_factor);
	dev_dbg(isp->dev, "stream_config.pixels_per_clock=%d.\n",
			s_config->pixels_per_clock);
	dev_dbg(isp->dev, "stream_config.pack_raw_pixels=%d.\n",
			s_config->pack_raw_pixels);
	dev_dbg(isp->dev, "stream_config.flash_gpio_pin=%d.\n",
			s_config->flash_gpio_pin);
	dev_dbg(isp->dev, "stream_config.mipi_buffer_config.size_mem_words=%d.\n",
			s_config->mipi_buffer_config.size_mem_words);
	dev_dbg(isp->dev, "stream_config.mipi_buffer_config.contiguous=%d.\n",
			s_config->mipi_buffer_config.contiguous);
	dev_dbg(isp->dev, "stream_config.metadata_config.data_type=%d.\n",
			s_config->metadata_config.data_type);
	dev_dbg(isp->dev, "stream_config.metadata_config.resolution w=%d, h=%d.\n",
			s_config->metadata_config.resolution.width,
			s_config->metadata_config.resolution.height);
}

static int __destroy_stream(struct atomisp_sub_device *asd,
			struct atomisp_stream_env *stream_env, bool force)
{
	struct atomisp_device *isp = asd->isp;
	int i;
	unsigned long timeout;

	if (!stream_env->stream)
		return 0;

	if (!force) {
		for (i = 0; i < IA_CSS_PIPE_ID_NUM; i++)
			if (stream_env->update_pipe[i])
				break;

		if (i == IA_CSS_PIPE_ID_NUM)
			return 0;
	}

	if (stream_env->stream_state == CSS_STREAM_STARTED
	    && ia_css_stream_stop(stream_env->stream) != IA_CSS_SUCCESS) {
		dev_err(isp->dev, "stop stream failed.\n");
		return -EINVAL;
	}

	if (stream_env->stream_state == CSS_STREAM_STARTED) {
		timeout = jiffies + msecs_to_jiffies(40);
		while (1) {
			if (ia_css_stream_has_stopped(stream_env->stream))
				break;

			if (time_after(jiffies, timeout)) {
				dev_warn(isp->dev, "stop stream timeout.\n");
				break;
			}

			usleep_range(100, 200);
		}
	}

	stream_env->stream_state = CSS_STREAM_STOPPED;

	if (ia_css_stream_destroy(stream_env->stream) != IA_CSS_SUCCESS) {
		dev_err(isp->dev, "destroy stream failed.\n");
		return -EINVAL;
	}
	stream_env->stream_state = CSS_STREAM_UNINIT;
	stream_env->stream = NULL;

	return 0;
}

static int __destroy_streams(struct atomisp_sub_device *asd, bool force)
{
	int ret, i;
	for (i = 0; i < ATOMISP_INPUT_STREAM_NUM; i++) {
		ret = __destroy_stream(asd, &asd->stream_env[i], force);
		if (ret)
			return ret;
	}
	asd->stream_prepared = false;
	return 0;
}
static int __create_stream(struct atomisp_sub_device *asd,
			   struct atomisp_stream_env *stream_env)
{
	int pipe_index = 0, i;
	struct ia_css_pipe *multi_pipes[IA_CSS_PIPE_ID_NUM];

	for (i = 0; i < IA_CSS_PIPE_ID_NUM; i++) {
		if (stream_env->pipes[i])
			multi_pipes[pipe_index++] = stream_env->pipes[i];
	}
	if (pipe_index == 0)
		return 0;

	stream_env->stream_config.target_num_cont_raw_buf =
		asd->continuous_raw_buffer_size->val;
	stream_env->stream_config.channel_id = stream_env->ch_id;
	stream_env->stream_config.ia_css_enable_raw_buffer_locking =
		asd->enable_raw_buffer_lock->val;

	__dump_stream_config(asd, stream_env);
	if (ia_css_stream_create(&stream_env->stream_config,
	    pipe_index, multi_pipes, &stream_env->stream) != IA_CSS_SUCCESS)
		return -EINVAL;
	if (ia_css_stream_get_info(stream_env->stream,
				&stream_env->stream_info) != IA_CSS_SUCCESS) {
		ia_css_stream_destroy(stream_env->stream);
		stream_env->stream = NULL;
		return -EINVAL;
	}

	stream_env->stream_state = CSS_STREAM_CREATED;
	return 0;
}

static int __create_streams(struct atomisp_sub_device *asd)
{
	int ret, i;

	for (i = 0; i < ATOMISP_INPUT_STREAM_NUM; i++) {
		ret = __create_stream(asd, &asd->stream_env[i]);
		if (ret)
			goto rollback;
	}
	asd->stream_prepared = true;
	return 0;
rollback:
	for (i--; i >= 0; i--)
		__destroy_stream(asd, &asd->stream_env[i], true);
	return ret;
}

static int __destroy_stream_pipes(struct atomisp_sub_device *asd,
				  struct atomisp_stream_env *stream_env,
				  bool force)
{
	struct atomisp_device *isp = asd->isp;
	int ret = 0;
	int i;
	for (i = 0; i < IA_CSS_PIPE_ID_NUM; i++) {
		if (!stream_env->pipes[i] ||
		    !(force || stream_env->update_pipe[i]))
			continue;
		if (ia_css_pipe_destroy(stream_env->pipes[i])
		    != IA_CSS_SUCCESS) {
			dev_err(isp->dev,
				"destroy pipe[%d]failed.cannot recover.\n", i);
			ret = -EINVAL;
		}
		stream_env->pipes[i] = NULL;
		stream_env->update_pipe[i] = false;
	}
	return ret;
}

static int __destroy_pipes(struct atomisp_sub_device *asd, bool force)
{
	struct atomisp_device *isp = asd->isp;
	int i;
	int ret = 0;

	for (i = 0; i < ATOMISP_INPUT_STREAM_NUM; i++) {
		if (asd->stream_env[i].stream) {

			dev_err(isp->dev,
				"cannot destroy css pipes for stream[%d].\n",
				i);
			continue;
		}

		ret = __destroy_stream_pipes(asd, &asd->stream_env[i], force);
		if (ret)
			return ret;
	}

	return 0;
}

void atomisp_destroy_pipes_stream_force(struct atomisp_sub_device *asd)
{
	__destroy_streams(asd, true);
	__destroy_pipes(asd, true);
}

static void __apply_additional_pipe_config(
				struct atomisp_sub_device *asd,
				struct atomisp_stream_env *stream_env,
				enum ia_css_pipe_id pipe_id)
{
	struct atomisp_device *isp = asd->isp;

	if (pipe_id < 0 || pipe_id >= IA_CSS_PIPE_ID_NUM) {
		dev_err(isp->dev,
			 "wrong pipe_id for additional pipe config.\n");
		return;
	}

	/* apply default pipe config */
	stream_env->pipe_configs[pipe_id].isp_pipe_version = 2;
	stream_env->pipe_configs[pipe_id].enable_dz =
				asd->disable_dz->val ? false : true;
	/* apply isp 2.2 specific config for baytrail*/
	switch (pipe_id) {
	case IA_CSS_PIPE_ID_CAPTURE:
		/* enable capture pp/dz manually or digital zoom would
		 * fail*/
		if (stream_env->pipe_configs[pipe_id].
			default_capture_config.mode == CSS_CAPTURE_MODE_RAW)
			stream_env->pipe_configs[pipe_id].enable_dz = false;
#ifdef ISP2401

		/* the isp default to use ISP2.2 and the camera hal will
		 * control whether use isp2.7 */
		if (asd->select_isp_version->val ==
			ATOMISP_CSS_ISP_PIPE_VERSION_2_7)
			stream_env->pipe_configs[pipe_id].isp_pipe_version =
				SH_CSS_ISP_PIPE_VERSION_2_7;
		else
			stream_env->pipe_configs[pipe_id].isp_pipe_version =
				SH_CSS_ISP_PIPE_VERSION_2_2;
#endif
		break;
	case IA_CSS_PIPE_ID_VIDEO:
		/* enable reduced pipe to have binary
		 * video_dz_2_min selected*/
		stream_env->pipe_extra_configs[pipe_id]
		    .enable_reduced_pipe = true;
		stream_env->pipe_configs[pipe_id]
		    .enable_dz = false;
		if (ATOMISP_SOC_CAMERA(asd))
			stream_env->pipe_configs[pipe_id].enable_dz = true;

		if (asd->params.video_dis_en) {
			stream_env->pipe_extra_configs[pipe_id]
				.enable_dvs_6axis = true;
			stream_env->pipe_configs[pipe_id]
				.dvs_frame_delay =
					ATOMISP_CSS2_NUM_DVS_FRAME_DELAY;
		}
		break;
	case IA_CSS_PIPE_ID_PREVIEW:
		break;
	case IA_CSS_PIPE_ID_YUVPP:
	case IA_CSS_PIPE_ID_COPY:
		if (ATOMISP_SOC_CAMERA(asd))
			stream_env->pipe_configs[pipe_id].enable_dz = true;
		else
			stream_env->pipe_configs[pipe_id].enable_dz = false;
		break;
	case IA_CSS_PIPE_ID_ACC:
		stream_env->pipe_configs[pipe_id].mode = IA_CSS_PIPE_MODE_ACC;
		stream_env->pipe_configs[pipe_id].enable_dz = false;
		break;
	default:
		break;
	}
}

static bool is_pipe_valid_to_current_run_mode(struct atomisp_sub_device *asd,
					enum ia_css_pipe_id pipe_id)
{
	if (!asd)
		return false;

	if (pipe_id == CSS_PIPE_ID_ACC || pipe_id == CSS_PIPE_ID_YUVPP)
		return true;

	if (asd->vfpp) {
		if (asd->vfpp->val == ATOMISP_VFPP_DISABLE_SCALER) {
			if (pipe_id == IA_CSS_PIPE_ID_VIDEO)
				return true;
			else
				return false;
		} else if (asd->vfpp->val == ATOMISP_VFPP_DISABLE_LOWLAT) {
			if (pipe_id == IA_CSS_PIPE_ID_CAPTURE)
				return true;
			else
				return false;
		}
	}

	if (!asd->run_mode)
		return false;

	if (asd->copy_mode && pipe_id == IA_CSS_PIPE_ID_COPY)
		return true;

	switch (asd->run_mode->val) {
	case ATOMISP_RUN_MODE_STILL_CAPTURE:
		if (pipe_id == IA_CSS_PIPE_ID_CAPTURE)
			return true;
		else
			return false;
	case ATOMISP_RUN_MODE_PREVIEW:
		if (!asd->continuous_mode->val) {
			if (pipe_id == IA_CSS_PIPE_ID_PREVIEW)
				return true;
			else
				return false;
		}
		/* fall through to ATOMISP_RUN_MODE_CONTINUOUS_CAPTURE */
	case ATOMISP_RUN_MODE_CONTINUOUS_CAPTURE:
		if (pipe_id == IA_CSS_PIPE_ID_CAPTURE ||
		    pipe_id == IA_CSS_PIPE_ID_PREVIEW)
			return true;
		else
			return false;
	case ATOMISP_RUN_MODE_VIDEO:
		if (!asd->continuous_mode->val) {
			if (pipe_id == IA_CSS_PIPE_ID_VIDEO ||
			    pipe_id == IA_CSS_PIPE_ID_YUVPP)
				return true;
			else
				return false;
		}
		/* fall through to ATOMISP_RUN_MODE_SDV */
	case ATOMISP_RUN_MODE_SDV:
		if (pipe_id == IA_CSS_PIPE_ID_CAPTURE ||
		    pipe_id == IA_CSS_PIPE_ID_VIDEO)
			return true;
		else
			return false;
	}

	return false;
}

static int __create_pipe(struct atomisp_sub_device *asd,
			 struct atomisp_stream_env *stream_env,
			 enum ia_css_pipe_id pipe_id)
{
	struct atomisp_device *isp = asd->isp;
	struct ia_css_pipe_extra_config extra_config;
	enum ia_css_err ret;

	if (pipe_id >= IA_CSS_PIPE_ID_NUM)
		return -EINVAL;

	if (pipe_id != CSS_PIPE_ID_ACC &&
	    !stream_env->pipe_configs[pipe_id].output_info[0].res.width)
		return 0;

	if (pipe_id == CSS_PIPE_ID_ACC &&
	    !stream_env->pipe_configs[pipe_id].acc_extension)
		return 0;

	if (!is_pipe_valid_to_current_run_mode(asd, pipe_id))
		return 0;

	ia_css_pipe_extra_config_defaults(&extra_config);

	__apply_additional_pipe_config(asd, stream_env, pipe_id);
	if (!memcmp(&extra_config,
		    &stream_env->pipe_extra_configs[pipe_id],
		    sizeof(extra_config)))
		ret = ia_css_pipe_create(
			&stream_env->pipe_configs[pipe_id],
			&stream_env->pipes[pipe_id]);
	else
		ret = ia_css_pipe_create_extra(
			&stream_env->pipe_configs[pipe_id],
			&stream_env->pipe_extra_configs[pipe_id],
			&stream_env->pipes[pipe_id]);
	if (ret != IA_CSS_SUCCESS)
		dev_err(isp->dev, "create pipe[%d] error.\n", pipe_id);
	return ret;
}

static int __create_pipes(struct atomisp_sub_device *asd)
{
	enum ia_css_err ret;
	int i, j;

	for (i = 0; i < ATOMISP_INPUT_STREAM_NUM; i++) {
		for (j = 0; j < IA_CSS_PIPE_ID_NUM; j++) {
			ret = __create_pipe(asd, &asd->stream_env[i], j);
			if (ret != IA_CSS_SUCCESS)
				break;
		}
		if (j < IA_CSS_PIPE_ID_NUM)
			goto pipe_err;
	}
	return 0;
pipe_err:
	for (; i >= 0; i--) {
		for (j--; j >= 0; j--) {
			if (asd->stream_env[i].pipes[j]) {
				ia_css_pipe_destroy(asd->stream_env[i].pipes[j]);
				asd->stream_env[i].pipes[j] = NULL;
			}
		}
		j = IA_CSS_PIPE_ID_NUM;
	}
	return -EINVAL;
}

void atomisp_create_pipes_stream(struct atomisp_sub_device *asd)
{
	__create_pipes(asd);
	__create_streams(asd);
}

int atomisp_css_update_stream(struct atomisp_sub_device *asd)
{
	int ret;
	struct atomisp_device *isp = asd->isp;

	if (__destroy_streams(asd, true) != IA_CSS_SUCCESS)
		dev_warn(isp->dev, "destroy stream failed.\n");

	if (__destroy_pipes(asd, true) != IA_CSS_SUCCESS)
		dev_warn(isp->dev, "destroy pipe failed.\n");

	ret = __create_pipes(asd);
	if (ret != IA_CSS_SUCCESS) {
		dev_err(isp->dev, "create pipe failed %d.\n", ret);
		return -EIO;
	}

	ret = __create_streams(asd);
	if (ret != IA_CSS_SUCCESS) {
		dev_warn(isp->dev, "create stream failed %d.\n", ret);
		__destroy_pipes(asd, true);
		return -EIO;
	}

	return 0;
}

int atomisp_css_init(struct atomisp_device *isp)
{
	unsigned int mmu_base_addr;
	int ret;
	enum ia_css_err err;

	ret = hmm_get_mmu_base_addr(&mmu_base_addr);
	if (ret)
		return ret;

	/* Init ISP */
	err = ia_css_init(&isp->css_env.isp_css_env, NULL,
			  (uint32_t)mmu_base_addr, IA_CSS_IRQ_TYPE_PULSE);
	if (err != IA_CSS_SUCCESS) {
		dev_err(isp->dev, "css init failed --- bad firmware?\n");
		return -EINVAL;
	}
	ia_css_enable_isys_event_queue(true);

	isp->css_initialized = true;
	dev_dbg(isp->dev, "sh_css_init success\n");

	return 0;
}

static inline int __set_css_print_env(struct atomisp_device *isp, int opt)
{
	int ret = 0;

	if (0 == opt)
		isp->css_env.isp_css_env.print_env.debug_print = NULL;
	else if (1 == opt)
		isp->css_env.isp_css_env.print_env.debug_print =
			atomisp_css2_dbg_ftrace_print;
	else if (2 == opt)
		isp->css_env.isp_css_env.print_env.debug_print =
			atomisp_css2_dbg_print;
	else
		ret = -EINVAL;

	return ret;
}

int atomisp_css_check_firmware_version(struct atomisp_device *isp)
{
	if (!sh_css_check_firmware_version((void *)isp->firmware->data)) {
		dev_err(isp->dev, "Fw version check failed.\n");
		return -EINVAL;
	}
	return 0;
}

int atomisp_css_load_firmware(struct atomisp_device *isp)
{
	enum ia_css_err err;

	/* set css env */
	isp->css_env.isp_css_fw.data = (void *)isp->firmware->data;
	isp->css_env.isp_css_fw.bytes = isp->firmware->size;

	isp->css_env.isp_css_env.hw_access_env.store_8 =
							atomisp_css2_hw_store_8;
	isp->css_env.isp_css_env.hw_access_env.store_16 =
						atomisp_css2_hw_store_16;
	isp->css_env.isp_css_env.hw_access_env.store_32 =
						atomisp_css2_hw_store_32;

	isp->css_env.isp_css_env.hw_access_env.load_8 = atomisp_css2_hw_load_8;
	isp->css_env.isp_css_env.hw_access_env.load_16 =
							atomisp_css2_hw_load_16;
	isp->css_env.isp_css_env.hw_access_env.load_32 =
							atomisp_css2_hw_load_32;

	isp->css_env.isp_css_env.hw_access_env.load = atomisp_css2_hw_load;
	isp->css_env.isp_css_env.hw_access_env.store = atomisp_css2_hw_store;

	__set_css_print_env(isp, dbg_func);

	isp->css_env.isp_css_env.print_env.error_print = atomisp_css2_err_print;

	/* load isp fw into ISP memory */
	err = ia_css_load_firmware(&isp->css_env.isp_css_env,
				   &isp->css_env.isp_css_fw);
	if (err != IA_CSS_SUCCESS) {
		dev_err(isp->dev, "css load fw failed.\n");
		return -EINVAL;
	}

	return 0;
}

void atomisp_css_unload_firmware(struct atomisp_device *isp)
{
	ia_css_unload_firmware();
}

void atomisp_css_uninit(struct atomisp_device *isp)
{
	struct atomisp_sub_device *asd;
	unsigned int i;

	for (i = 0; i < isp->num_of_streams; i++) {
		asd = &isp->asd[i];
		atomisp_isp_parameters_clean_up(&asd->params.config);
		asd->params.css_update_params_needed = false;
	}

	isp->css_initialized = false;
	ia_css_uninit();
}

void atomisp_css_suspend(struct atomisp_device *isp)
{
	isp->css_initialized = false;
	ia_css_uninit();
}

int atomisp_css_resume(struct atomisp_device *isp)
{
	unsigned int mmu_base_addr;
	int ret;

	ret = hmm_get_mmu_base_addr(&mmu_base_addr);
	if (ret) {
		dev_err(isp->dev, "get base address error.\n");
		return -EINVAL;
	}

	ret = ia_css_init(&isp->css_env.isp_css_env, NULL,
			  mmu_base_addr, IA_CSS_IRQ_TYPE_PULSE);
	if (ret) {
		dev_err(isp->dev, "re-init css failed.\n");
		return -EINVAL;
	}
	ia_css_enable_isys_event_queue(true);

	isp->css_initialized = true;
	return 0;
}

int atomisp_css_irq_translate(struct atomisp_device *isp,
			      unsigned int *infos)
{
	int err;

	err = ia_css_irq_translate(infos);
	if (err != IA_CSS_SUCCESS) {
		dev_warn(isp->dev,
			  "%s:failed to translate irq (err = %d,infos = %d)\n",
			  __func__, err, *infos);
		return -EINVAL;
	}

	return 0;
}

void atomisp_css_rx_get_irq_info(enum ia_css_csi2_port port,
					unsigned int *infos)
{
#ifndef ISP2401_NEW_INPUT_SYSTEM
	ia_css_isys_rx_get_irq_info(port, infos);
#else
	*infos = 0;
#endif
}

void atomisp_css_rx_clear_irq_info(enum ia_css_csi2_port port,
					unsigned int infos)
{
#ifndef ISP2401_NEW_INPUT_SYSTEM
	ia_css_isys_rx_clear_irq_info(port, infos);
#endif
}

int atomisp_css_irq_enable(struct atomisp_device *isp,
			    enum atomisp_css_irq_info info, bool enable)
{
	if (ia_css_irq_enable(info, enable) != IA_CSS_SUCCESS) {
		dev_warn(isp->dev, "%s:Invalid irq info.\n", __func__);
		return -EINVAL;
	}

	return 0;
}

void atomisp_css_init_struct(struct atomisp_sub_device *asd)
{
	int i, j;
	for (i = 0; i < ATOMISP_INPUT_STREAM_NUM; i++) {
		asd->stream_env[i].stream = NULL;
		for (j = 0; j < IA_CSS_PIPE_MODE_NUM; j++) {
			asd->stream_env[i].pipes[j] = NULL;
			asd->stream_env[i].update_pipe[j] = false;
			ia_css_pipe_config_defaults(
				&asd->stream_env[i].pipe_configs[j]);
			ia_css_pipe_extra_config_defaults(
				&asd->stream_env[i].pipe_extra_configs[j]);
		}
		ia_css_stream_config_defaults(&asd->stream_env[i].stream_config);
	}
}

int atomisp_q_video_buffer_to_css(struct atomisp_sub_device *asd,
			struct videobuf_vmalloc_memory *vm_mem,
			enum atomisp_input_stream_id stream_id,
			enum atomisp_css_buffer_type css_buf_type,
			enum atomisp_css_pipe_id css_pipe_id)
{
	struct atomisp_stream_env *stream_env = &asd->stream_env[stream_id];
	struct ia_css_buffer css_buf = {0};
	enum ia_css_err err;

	css_buf.type = css_buf_type;
	css_buf.data.frame = vm_mem->vaddr;

	err = ia_css_pipe_enqueue_buffer(
				stream_env->pipes[css_pipe_id], &css_buf);
	if (err != IA_CSS_SUCCESS)
		return -EINVAL;

	return 0;
}

int atomisp_q_metadata_buffer_to_css(struct atomisp_sub_device *asd,
			struct atomisp_metadata_buf *metadata_buf,
			enum atomisp_input_stream_id stream_id,
			enum atomisp_css_pipe_id css_pipe_id)
{
	struct atomisp_stream_env *stream_env = &asd->stream_env[stream_id];
	struct ia_css_buffer buffer = {0};
	struct atomisp_device *isp = asd->isp;

	buffer.type = IA_CSS_BUFFER_TYPE_METADATA;
	buffer.data.metadata = metadata_buf->metadata;
	if (ia_css_pipe_enqueue_buffer(stream_env->pipes[css_pipe_id],
				&buffer)) {
		dev_err(isp->dev, "failed to q meta data buffer\n");
		return -EINVAL;
	}

	return 0;
}

int atomisp_q_s3a_buffer_to_css(struct atomisp_sub_device *asd,
			struct atomisp_s3a_buf *s3a_buf,
			enum atomisp_input_stream_id stream_id,
			enum atomisp_css_pipe_id css_pipe_id)
{
	struct atomisp_stream_env *stream_env = &asd->stream_env[stream_id];
	struct ia_css_buffer buffer = {0};
	struct atomisp_device *isp = asd->isp;

	buffer.type = IA_CSS_BUFFER_TYPE_3A_STATISTICS;
	buffer.data.stats_3a = s3a_buf->s3a_data;
	if (ia_css_pipe_enqueue_buffer(
				stream_env->pipes[css_pipe_id],
				&buffer)) {
		dev_dbg(isp->dev, "failed to q s3a stat buffer\n");
		return -EINVAL;
	}

	return 0;
}

int atomisp_q_dis_buffer_to_css(struct atomisp_sub_device *asd,
			struct atomisp_dis_buf *dis_buf,
			enum atomisp_input_stream_id stream_id,
			enum atomisp_css_pipe_id css_pipe_id)
{
	struct atomisp_stream_env *stream_env = &asd->stream_env[stream_id];
	struct ia_css_buffer buffer = {0};
	struct atomisp_device *isp = asd->isp;

	buffer.type = IA_CSS_BUFFER_TYPE_DIS_STATISTICS;
	buffer.data.stats_dvs = dis_buf->dis_data;
	if (ia_css_pipe_enqueue_buffer(
				stream_env->pipes[css_pipe_id],
				&buffer)) {
		dev_dbg(isp->dev, "failed to q dvs stat buffer\n");
		return -EINVAL;
	}

	return 0;
}

void atomisp_css_mmu_invalidate_cache(void)
{
	ia_css_mmu_invalidate_cache();
}

void atomisp_css_mmu_invalidate_tlb(void)
{
	ia_css_mmu_invalidate_cache();
}

void atomisp_css_mmu_set_page_table_base_index(unsigned long base_index)
{
}

/*
 * Check whether currently running MIPI buffer size fulfill
 * the requirement of the stream to be run
 */
bool __need_realloc_mipi_buffer(struct atomisp_device *isp)
{
	unsigned int i;

	for (i = 0; i < isp->num_of_streams; i++) {
		struct atomisp_sub_device *asd = &isp->asd[i];

		if (asd->streaming !=
				ATOMISP_DEVICE_STREAMING_ENABLED)
			continue;
		if (asd->mipi_frame_size < isp->mipi_frame_size)
			return true;
	}

	return false;
}

int atomisp_css_start(struct atomisp_sub_device *asd,
			enum atomisp_css_pipe_id pipe_id, bool in_reset)
{
	struct atomisp_device *isp = asd->isp;
	bool sp_is_started = false;
	int ret = 0, i = 0;
	if (in_reset) {
		if (__destroy_streams(asd, true))
			dev_warn(isp->dev, "destroy stream failed.\n");

		if (__destroy_pipes(asd, true))
			dev_warn(isp->dev, "destroy pipe failed.\n");

		if (__create_pipes(asd)) {
			dev_err(isp->dev, "create pipe error.\n");
			return -EINVAL;
		}
		if (__create_streams(asd)) {
			dev_err(isp->dev, "create stream error.\n");
			ret = -EINVAL;
			goto stream_err;
		}
		/* in_reset == true, extension firmwares are reloaded after the recovery */
		atomisp_acc_load_extensions(asd);
	}

	/*
	 * For dual steam case, it is possible that:
	 * 1: for this stream, it is at the stage that:
	 * - after set_fmt is called
	 * - before stream on is called
	 * 2: for the other stream, the stream off is called which css reset
	 * has been done.
	 *
	 * Thus the stream created in set_fmt get destroyed and need to be
	 * recreated in the next stream on.
	 */
	if (asd->stream_prepared == false) {
		if (__create_pipes(asd)) {
			dev_err(isp->dev, "create pipe error.\n");
			return -EINVAL;
		}
		if (__create_streams(asd)) {
			dev_err(isp->dev, "create stream error.\n");
			ret = -EINVAL;
			goto stream_err;
		}
	}
	/*
	 * SP can only be started one time
	 * if atomisp_subdev_streaming_count() tell there already has some
	 * subdev at streamming, then SP should already be started previously,
	 * so need to skip start sp procedure
	 */
	if (atomisp_streaming_count(isp)) {
		dev_dbg(isp->dev, "skip start sp\n");
	} else {
		if (!sh_css_hrt_system_is_idle())
			dev_err(isp->dev, "CSS HW not idle before starting SP\n");
		if (ia_css_start_sp() != IA_CSS_SUCCESS) {
			dev_err(isp->dev, "start sp error.\n");
			ret = -EINVAL;
			goto start_err;
		} else {
			sp_is_started = true;
		}
	}

	for (i = 0; i < ATOMISP_INPUT_STREAM_NUM; i++) {
		if (asd->stream_env[i].stream) {
			if (ia_css_stream_start(asd->stream_env[i]
						.stream) != IA_CSS_SUCCESS) {
				dev_err(isp->dev, "stream[%d] start error.\n", i);
				ret = -EINVAL;
				goto start_err;
			} else {
				asd->stream_env[i].stream_state = CSS_STREAM_STARTED;
				dev_dbg(isp->dev, "stream[%d] started.\n", i);
			}
		}
	}

	return 0;

start_err:
	__destroy_streams(asd, true);
stream_err:
	__destroy_pipes(asd, true);

	/* css 2.0 API limitation: ia_css_stop_sp() could be only called after
	 * destroy all pipes
	 */
	/*
	 * SP can not be stop if other streams are in use
	 */
	if ((atomisp_streaming_count(isp) == 0) && sp_is_started)
		ia_css_stop_sp();

	return ret;
}

void atomisp_css_update_isp_params(struct atomisp_sub_device *asd)
{
	/*
	 * FIXME!
	 * for ISP2401 new input system, this api is under development.
	 * Calling it would cause kernel panic.
	 *
	 * VIED BZ: 1458
	 *
	 * Check if it is Cherry Trail and also new input system
	 */
	if (asd->copy_mode) {
		dev_warn(asd->isp->dev,
			 "%s: ia_css_stream_set_isp_config() not supported in copy mode!.\n",
				__func__);
		return;
	}

	ia_css_stream_set_isp_config(
			asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
			&asd->params.config);
	atomisp_isp_parameters_clean_up(&asd->params.config);
}


void atomisp_css_update_isp_params_on_pipe(struct atomisp_sub_device *asd,
					struct ia_css_pipe *pipe)
{
	enum ia_css_err ret;

	if (!pipe) {
		atomisp_css_update_isp_params(asd);
		return;
	}

	dev_dbg(asd->isp->dev, "%s: apply parameter for ia_css_frame %p with isp_config_id %d on pipe %p.\n",
		__func__, asd->params.config.output_frame,
		asd->params.config.isp_config_id, pipe);

	ret = ia_css_stream_set_isp_config_on_pipe(
			asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
			&asd->params.config, pipe);
	if (ret != IA_CSS_SUCCESS)
		dev_warn(asd->isp->dev, "%s: ia_css_stream_set_isp_config_on_pipe failed %d\n",
			__func__, ret);
	atomisp_isp_parameters_clean_up(&asd->params.config);
}

int atomisp_css_queue_buffer(struct atomisp_sub_device *asd,
			     enum atomisp_input_stream_id stream_id,
			     enum atomisp_css_pipe_id pipe_id,
			     enum atomisp_css_buffer_type buf_type,
			     struct atomisp_css_buffer *isp_css_buffer)
{
	if (ia_css_pipe_enqueue_buffer(
		asd->stream_env[stream_id].pipes[pipe_id],
					&isp_css_buffer->css_buffer)
					!= IA_CSS_SUCCESS)
		return -EINVAL;

	return 0;
}

int atomisp_css_dequeue_buffer(struct atomisp_sub_device *asd,
				enum atomisp_input_stream_id stream_id,
				enum atomisp_css_pipe_id pipe_id,
				enum atomisp_css_buffer_type buf_type,
				struct atomisp_css_buffer *isp_css_buffer)
{
	struct atomisp_device *isp = asd->isp;
	enum ia_css_err err;

	err = ia_css_pipe_dequeue_buffer(
		asd->stream_env[stream_id].pipes[pipe_id],
					&isp_css_buffer->css_buffer);
	if (err != IA_CSS_SUCCESS) {
		dev_err(isp->dev,
			"ia_css_pipe_dequeue_buffer failed: 0x%x\n", err);
		return -EINVAL;
	}

	return 0;
}

int atomisp_css_allocate_stat_buffers(struct atomisp_sub_device   *asd,
				      uint16_t stream_id,
				      struct atomisp_s3a_buf      *s3a_buf,
				      struct atomisp_dis_buf      *dis_buf,
				      struct atomisp_metadata_buf *md_buf)
{
	struct atomisp_device *isp = asd->isp;
	struct atomisp_css_dvs_grid_info *dvs_grid_info =
		atomisp_css_get_dvs_grid_info(&asd->params.curr_grid_info);

	if (s3a_buf && asd->params.curr_grid_info.s3a_grid.enable) {
		void *s3a_ptr;

		s3a_buf->s3a_data = ia_css_isp_3a_statistics_allocate(
				&asd->params.curr_grid_info.s3a_grid);
		if (!s3a_buf->s3a_data) {
			dev_err(isp->dev, "3a buf allocation failed.\n");
			return -EINVAL;
		}

		s3a_ptr = hmm_vmap(s3a_buf->s3a_data->data_ptr, true);
		s3a_buf->s3a_map = ia_css_isp_3a_statistics_map_allocate(
						s3a_buf->s3a_data, s3a_ptr);
	}

	if (dis_buf && dvs_grid_info && dvs_grid_info->enable) {
		void *dvs_ptr;

		dis_buf->dis_data = ia_css_isp_dvs2_statistics_allocate(
					dvs_grid_info);
		if (!dis_buf->dis_data) {
			dev_err(isp->dev, "dvs buf allocation failed.\n");
			if (s3a_buf)
				ia_css_isp_3a_statistics_free(s3a_buf->s3a_data);
			return -EINVAL;
		}

		dvs_ptr = hmm_vmap(dis_buf->dis_data->data_ptr, true);
		dis_buf->dvs_map = ia_css_isp_dvs_statistics_map_allocate(
						dis_buf->dis_data, dvs_ptr);
	}

	if (asd->stream_env[stream_id].stream_info.
			metadata_info.size && md_buf) {
		md_buf->metadata = ia_css_metadata_allocate(
			&asd->stream_env[stream_id].stream_info.metadata_info);
		if (!md_buf->metadata) {
			if (s3a_buf)
				ia_css_isp_3a_statistics_free(s3a_buf->s3a_data);
			if (dis_buf)
				ia_css_isp_dvs2_statistics_free(dis_buf->dis_data);
			dev_err(isp->dev, "metadata buf allocation failed.\n");
			return -EINVAL;
		}
		md_buf->md_vptr = hmm_vmap(md_buf->metadata->address, false);
	}

	return 0;
}

void atomisp_css_free_3a_buffer(struct atomisp_s3a_buf *s3a_buf)
{
	if (s3a_buf->s3a_data)
		hmm_vunmap(s3a_buf->s3a_data->data_ptr);

	ia_css_isp_3a_statistics_map_free(s3a_buf->s3a_map);
	s3a_buf->s3a_map = NULL;
	ia_css_isp_3a_statistics_free(s3a_buf->s3a_data);
}

void atomisp_css_free_dis_buffer(struct atomisp_dis_buf *dis_buf)
{
	if (dis_buf->dis_data)
		hmm_vunmap(dis_buf->dis_data->data_ptr);

	ia_css_isp_dvs_statistics_map_free(dis_buf->dvs_map);
	dis_buf->dvs_map = NULL;
	ia_css_isp_dvs2_statistics_free(dis_buf->dis_data);
}

void atomisp_css_free_metadata_buffer(struct atomisp_metadata_buf *metadata_buf)
{
	if (metadata_buf->md_vptr) {
		hmm_vunmap(metadata_buf->metadata->address);
		metadata_buf->md_vptr = NULL;
	}
	ia_css_metadata_free(metadata_buf->metadata);
}

void atomisp_css_free_stat_buffers(struct atomisp_sub_device *asd)
{
	struct atomisp_s3a_buf *s3a_buf, *_s3a_buf;
	struct atomisp_dis_buf *dis_buf, *_dis_buf;
	struct atomisp_metadata_buf *md_buf, *_md_buf;
	struct atomisp_css_dvs_grid_info *dvs_grid_info =
		atomisp_css_get_dvs_grid_info(&asd->params.curr_grid_info);
	unsigned int i;

	/* 3A statistics use vmalloc, DIS use kmalloc */
	if (dvs_grid_info && dvs_grid_info->enable) {
		ia_css_dvs2_coefficients_free(asd->params.css_param.dvs2_coeff);
		ia_css_dvs2_statistics_free(asd->params.dvs_stat);
		asd->params.css_param.dvs2_coeff = NULL;
		asd->params.dvs_stat = NULL;
		asd->params.dvs_hor_proj_bytes = 0;
		asd->params.dvs_ver_proj_bytes = 0;
		asd->params.dvs_hor_coef_bytes = 0;
		asd->params.dvs_ver_coef_bytes = 0;
		asd->params.dis_proj_data_valid = false;
		list_for_each_entry_safe(dis_buf, _dis_buf,
						&asd->dis_stats, list) {
			atomisp_css_free_dis_buffer(dis_buf);
			list_del(&dis_buf->list);
			kfree(dis_buf);
		}
		list_for_each_entry_safe(dis_buf, _dis_buf,
						&asd->dis_stats_in_css, list) {
			atomisp_css_free_dis_buffer(dis_buf);
			list_del(&dis_buf->list);
			kfree(dis_buf);
		}
	}
	if (asd->params.curr_grid_info.s3a_grid.enable) {
		ia_css_3a_statistics_free(asd->params.s3a_user_stat);
		asd->params.s3a_user_stat = NULL;
		asd->params.s3a_output_bytes = 0;
		list_for_each_entry_safe(s3a_buf, _s3a_buf,
						&asd->s3a_stats, list) {
			atomisp_css_free_3a_buffer(s3a_buf);
			list_del(&s3a_buf->list);
			kfree(s3a_buf);
		}
		list_for_each_entry_safe(s3a_buf, _s3a_buf,
						&asd->s3a_stats_in_css, list) {
			atomisp_css_free_3a_buffer(s3a_buf);
			list_del(&s3a_buf->list);
			kfree(s3a_buf);
		}
		list_for_each_entry_safe(s3a_buf, _s3a_buf,
						&asd->s3a_stats_ready, list) {
			atomisp_css_free_3a_buffer(s3a_buf);
			list_del(&s3a_buf->list);
			kfree(s3a_buf);
		}
	}

	if (asd->params.css_param.dvs_6axis) {
		ia_css_dvs2_6axis_config_free(asd->params.css_param.dvs_6axis);
		asd->params.css_param.dvs_6axis = NULL;
	}

	for (i = 0; i < ATOMISP_METADATA_TYPE_NUM; i++) {
		list_for_each_entry_safe(md_buf, _md_buf,
					&asd->metadata[i], list) {
			atomisp_css_free_metadata_buffer(md_buf);
			list_del(&md_buf->list);
			kfree(md_buf);
		}
		list_for_each_entry_safe(md_buf, _md_buf,
					&asd->metadata_in_css[i], list) {
			atomisp_css_free_metadata_buffer(md_buf);
			list_del(&md_buf->list);
			kfree(md_buf);
		}
		list_for_each_entry_safe(md_buf, _md_buf,
					&asd->metadata_ready[i], list) {
			atomisp_css_free_metadata_buffer(md_buf);
			list_del(&md_buf->list);
			kfree(md_buf);
		}
	}
	asd->params.metadata_width_size = 0;
	atomisp_free_metadata_output_buf(asd);
}

int atomisp_css_get_grid_info(struct atomisp_sub_device *asd,
				enum atomisp_css_pipe_id pipe_id,
				int source_pad)
{
	struct ia_css_pipe_info p_info;
	struct ia_css_grid_info old_info;
	struct atomisp_device *isp = asd->isp;
	int stream_index = atomisp_source_pad_to_stream_id(asd, source_pad);
	int md_width = asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].
		stream_config.metadata_config.resolution.width;

	memset(&p_info, 0, sizeof(struct ia_css_pipe_info));
	memset(&old_info, 0, sizeof(struct ia_css_grid_info));

	if (ia_css_pipe_get_info(
		asd->stream_env[stream_index].pipes[pipe_id],
		&p_info) != IA_CSS_SUCCESS) {
		dev_err(isp->dev, "ia_css_pipe_get_info failed\n");
		return -EINVAL;
	}

	memcpy(&old_info, &asd->params.curr_grid_info,
					sizeof(struct ia_css_grid_info));
	memcpy(&asd->params.curr_grid_info, &p_info.grid_info,
					sizeof(struct ia_css_grid_info));
	/*
	 * Record which css pipe enables s3a_grid.
	 * Currently would have one css pipe that need it
	 */
	if (asd->params.curr_grid_info.s3a_grid.enable) {
		if (asd->params.s3a_enabled_pipe != CSS_PIPE_ID_NUM)
			dev_dbg(isp->dev, "css pipe %d enabled s3a grid replaced by: %d.\n",
					asd->params.s3a_enabled_pipe, pipe_id);
		asd->params.s3a_enabled_pipe = pipe_id;
	}

	/* If the grid info has not changed and the buffers for 3A and
	 * DIS statistics buffers are allocated or buffer size would be zero
	 * then no need to do anything. */
	if (((!memcmp(&old_info, &asd->params.curr_grid_info, sizeof(old_info))
	    && asd->params.s3a_user_stat && asd->params.dvs_stat)
	    || asd->params.curr_grid_info.s3a_grid.width == 0
	    || asd->params.curr_grid_info.s3a_grid.height == 0)
	    && asd->params.metadata_width_size == md_width) {
		dev_dbg(isp->dev,
			"grid info change escape. memcmp=%d, s3a_user_stat=%d,"
			"dvs_stat=%d, s3a.width=%d, s3a.height=%d, metadata width =%d\n",
			!memcmp(&old_info, &asd->params.curr_grid_info,
				 sizeof(old_info)),
			 !!asd->params.s3a_user_stat, !!asd->params.dvs_stat,
			 asd->params.curr_grid_info.s3a_grid.width,
			 asd->params.curr_grid_info.s3a_grid.height,
			 asd->params.metadata_width_size);
		return -EINVAL;
	}
	asd->params.metadata_width_size = md_width;

	return 0;
}

int atomisp_alloc_3a_output_buf(struct atomisp_sub_device *asd)
{
	if (!asd->params.curr_grid_info.s3a_grid.width ||
			!asd->params.curr_grid_info.s3a_grid.height)
		return 0;

	asd->params.s3a_user_stat = ia_css_3a_statistics_allocate(
				&asd->params.curr_grid_info.s3a_grid);
	if (!asd->params.s3a_user_stat)
		return -ENOMEM;
	/* 3A statistics. These can be big, so we use vmalloc. */
	asd->params.s3a_output_bytes =
	    asd->params.curr_grid_info.s3a_grid.width *
	    asd->params.curr_grid_info.s3a_grid.height *
	    sizeof(*asd->params.s3a_user_stat->data);

	return 0;
}

int atomisp_alloc_dis_coef_buf(struct atomisp_sub_device *asd)
{
	struct atomisp_css_dvs_grid_info *dvs_grid =
		atomisp_css_get_dvs_grid_info(&asd->params.curr_grid_info);

	if (!dvs_grid)
		return 0;

	if (!dvs_grid->enable) {
		dev_dbg(asd->isp->dev, "%s: dvs_grid not enabled.\n", __func__);
		return 0;
	}

	/* DIS coefficients. */
	asd->params.css_param.dvs2_coeff = ia_css_dvs2_coefficients_allocate(
			dvs_grid);
	if (!asd->params.css_param.dvs2_coeff)
		return -ENOMEM;

	asd->params.dvs_hor_coef_bytes = dvs_grid->num_hor_coefs *
		sizeof(*asd->params.css_param.dvs2_coeff->hor_coefs.odd_real);

	asd->params.dvs_ver_coef_bytes = dvs_grid->num_ver_coefs *
		sizeof(*asd->params.css_param.dvs2_coeff->ver_coefs.odd_real);

	/* DIS projections. */
	asd->params.dis_proj_data_valid = false;
	asd->params.dvs_stat = ia_css_dvs2_statistics_allocate(dvs_grid);
	if (!asd->params.dvs_stat)
		return -ENOMEM;

	asd->params.dvs_hor_proj_bytes =
		dvs_grid->aligned_height * dvs_grid->aligned_width *
		sizeof(*asd->params.dvs_stat->hor_prod.odd_real);

	asd->params.dvs_ver_proj_bytes =
		dvs_grid->aligned_height * dvs_grid->aligned_width *
		sizeof(*asd->params.dvs_stat->ver_prod.odd_real);

	return 0;
}

int atomisp_alloc_metadata_output_buf(struct atomisp_sub_device *asd)
{
	int i;

	/* We allocate the cpu-side buffer used for communication with user
	 * space */
	for (i = 0; i < ATOMISP_METADATA_TYPE_NUM; i++) {
		asd->params.metadata_user[i] = atomisp_kernel_malloc(
				asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].
				stream_info.metadata_info.size);
		if (!asd->params.metadata_user[i]) {
			while (--i >= 0) {
				kvfree(asd->params.metadata_user[i]);
				asd->params.metadata_user[i] = NULL;
			}
			return -ENOMEM;
		}
	}

	return 0;
}

void atomisp_free_metadata_output_buf(struct atomisp_sub_device *asd)
{
	unsigned int i;

	for (i = 0; i < ATOMISP_METADATA_TYPE_NUM; i++) {
		if (asd->params.metadata_user[i]) {
			kvfree(asd->params.metadata_user[i]);
			asd->params.metadata_user[i] = NULL;
		}
	}
}

void atomisp_css_get_dis_statistics(struct atomisp_sub_device *asd,
				    struct atomisp_css_buffer *isp_css_buffer,
				    struct ia_css_isp_dvs_statistics_map *dvs_map)
{
	if (asd->params.dvs_stat) {
		if (dvs_map)
			ia_css_translate_dvs2_statistics(
				asd->params.dvs_stat, dvs_map);
		else
			ia_css_get_dvs2_statistics(asd->params.dvs_stat,
				isp_css_buffer->css_buffer.data.stats_dvs);

	}
}

int atomisp_css_dequeue_event(struct atomisp_css_event *current_event)
{
	if (ia_css_dequeue_event(&current_event->event) != IA_CSS_SUCCESS)
		return -EINVAL;

	return 0;
}

void atomisp_css_temp_pipe_to_pipe_id(struct atomisp_sub_device *asd,
		struct atomisp_css_event *current_event)
{
	/*
	 * FIXME!
	 * Pipe ID reported in CSS event is not correct for new system's
	 * copy pipe.
	 * VIED BZ: 1463
	 */
	ia_css_temp_pipe_to_pipe_id(current_event->event.pipe,
				    &current_event->pipe);
	if (asd && asd->copy_mode &&
	    current_event->pipe == IA_CSS_PIPE_ID_CAPTURE)
		current_event->pipe = IA_CSS_PIPE_ID_COPY;
}

int atomisp_css_isys_set_resolution(struct atomisp_sub_device *asd,
				    enum atomisp_input_stream_id stream_id,
				    struct v4l2_mbus_framefmt *ffmt,
				    int isys_stream)
{
	struct ia_css_stream_config *s_config =
			&asd->stream_env[stream_id].stream_config;

	if (isys_stream >= IA_CSS_STREAM_MAX_ISYS_STREAM_PER_CH)
		return -EINVAL;

	s_config->isys_config[isys_stream].input_res.width = ffmt->width;
	s_config->isys_config[isys_stream].input_res.height = ffmt->height;
	return 0;
}

int atomisp_css_input_set_resolution(struct atomisp_sub_device *asd,
				enum atomisp_input_stream_id stream_id,
				struct v4l2_mbus_framefmt *ffmt)
{
	struct ia_css_stream_config *s_config =
			&asd->stream_env[stream_id].stream_config;

	s_config->input_config.input_res.width = ffmt->width;
	s_config->input_config.input_res.height = ffmt->height;
	return 0;
}

void atomisp_css_input_set_binning_factor(struct atomisp_sub_device *asd,
					enum atomisp_input_stream_id stream_id,
					unsigned int bin_factor)
{
	asd->stream_env[stream_id]
	    .stream_config.sensor_binning_factor = bin_factor;
}

void atomisp_css_input_set_bayer_order(struct atomisp_sub_device *asd,
				enum atomisp_input_stream_id stream_id,
				enum atomisp_css_bayer_order bayer_order)
{
	struct ia_css_stream_config *s_config =
			&asd->stream_env[stream_id].stream_config;
	s_config->input_config.bayer_order = bayer_order;
}

void atomisp_css_isys_set_link(struct atomisp_sub_device *asd,
			       enum atomisp_input_stream_id stream_id,
			       int link,
			       int isys_stream)
{
	struct ia_css_stream_config *s_config =
		&asd->stream_env[stream_id].stream_config;

	s_config->isys_config[isys_stream].linked_isys_stream_id = link;
}

void atomisp_css_isys_set_valid(struct atomisp_sub_device *asd,
				enum atomisp_input_stream_id stream_id,
				bool valid,
				int isys_stream)
{
	struct ia_css_stream_config *s_config =
		&asd->stream_env[stream_id].stream_config;

	s_config->isys_config[isys_stream].valid = valid;
}

void atomisp_css_isys_set_format(struct atomisp_sub_device *asd,
				 enum atomisp_input_stream_id stream_id,
				 enum atomisp_css_stream_format format,
				 int isys_stream)
{

	struct ia_css_stream_config *s_config =
			&asd->stream_env[stream_id].stream_config;

	s_config->isys_config[isys_stream].format = format;
}

void atomisp_css_input_set_format(struct atomisp_sub_device *asd,
					enum atomisp_input_stream_id stream_id,
					enum atomisp_css_stream_format format)
{

	struct ia_css_stream_config *s_config =
			&asd->stream_env[stream_id].stream_config;

	s_config->input_config.format = format;
}

int atomisp_css_set_default_isys_config(struct atomisp_sub_device *asd,
					enum atomisp_input_stream_id stream_id,
					struct v4l2_mbus_framefmt *ffmt)
{
	int i;
	struct ia_css_stream_config *s_config =
			&asd->stream_env[stream_id].stream_config;
	/*
	 * Set all isys configs to not valid.
	 * Currently we support only one stream per channel
	 */
	for (i = IA_CSS_STREAM_ISYS_STREAM_0;
	     i < IA_CSS_STREAM_MAX_ISYS_STREAM_PER_CH; i++)
		s_config->isys_config[i].valid = false;

	atomisp_css_isys_set_resolution(asd, stream_id, ffmt,
					IA_CSS_STREAM_DEFAULT_ISYS_STREAM_IDX);
	atomisp_css_isys_set_format(asd, stream_id,
				    s_config->input_config.format,
				    IA_CSS_STREAM_DEFAULT_ISYS_STREAM_IDX);
	atomisp_css_isys_set_link(asd, stream_id, NO_LINK,
				  IA_CSS_STREAM_DEFAULT_ISYS_STREAM_IDX);
	atomisp_css_isys_set_valid(asd, stream_id, true,
				   IA_CSS_STREAM_DEFAULT_ISYS_STREAM_IDX);

	return 0;
}

int atomisp_css_isys_two_stream_cfg(struct atomisp_sub_device *asd,
				    enum atomisp_input_stream_id stream_id,
				    enum atomisp_css_stream_format input_format)
{
	struct ia_css_stream_config *s_config =
		&asd->stream_env[stream_id].stream_config;

	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_1].input_res.width =
	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_0].input_res.width;

	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_1].input_res.height =
	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_0].input_res.height / 2;

	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_1].linked_isys_stream_id
		= IA_CSS_STREAM_ISYS_STREAM_0;
	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_0].format =
		IA_CSS_STREAM_FORMAT_USER_DEF1;
	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_1].format =
		IA_CSS_STREAM_FORMAT_USER_DEF2;
	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_1].valid = true;
	return 0;
}

void atomisp_css_isys_two_stream_cfg_update_stream1(
				    struct atomisp_sub_device *asd,
				    enum atomisp_input_stream_id stream_id,
				    enum atomisp_css_stream_format input_format,
				    unsigned int width, unsigned int height)
{
	struct ia_css_stream_config *s_config =
		&asd->stream_env[stream_id].stream_config;

	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_0].input_res.width =
		width;
	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_0].input_res.height =
		height;
	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_0].format =
		input_format;
	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_0].valid = true;
}

void atomisp_css_isys_two_stream_cfg_update_stream2(
				    struct atomisp_sub_device *asd,
				    enum atomisp_input_stream_id stream_id,
				    enum atomisp_css_stream_format input_format,
				    unsigned int width, unsigned int height)
{
	struct ia_css_stream_config *s_config =
		&asd->stream_env[stream_id].stream_config;

	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_1].input_res.width =
		width;
	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_1].input_res.height =
	height;
	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_1].linked_isys_stream_id
		= IA_CSS_STREAM_ISYS_STREAM_0;
	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_1].format =
		input_format;
	s_config->isys_config[IA_CSS_STREAM_ISYS_STREAM_1].valid = true;
}

int atomisp_css_input_set_effective_resolution(
					struct atomisp_sub_device *asd,
					enum atomisp_input_stream_id stream_id,
					unsigned int width, unsigned int height)
{
	struct ia_css_stream_config *s_config =
			&asd->stream_env[stream_id].stream_config;
	s_config->input_config.effective_res.width = width;
	s_config->input_config.effective_res.height = height;
	return 0;
}

void atomisp_css_video_set_dis_envelope(struct atomisp_sub_device *asd,
					unsigned int dvs_w, unsigned int dvs_h)
{
	asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.pipe_configs[IA_CSS_PIPE_ID_VIDEO].dvs_envelope.width = dvs_w;
	asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.pipe_configs[IA_CSS_PIPE_ID_VIDEO].dvs_envelope.height = dvs_h;
}

void atomisp_css_input_set_two_pixels_per_clock(
					struct atomisp_sub_device *asd,
					bool two_ppc)
{
	int i;

	if (asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.stream_config.pixels_per_clock == (two_ppc ? 2 : 1))
		return;

	asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.stream_config.pixels_per_clock = (two_ppc ? 2 : 1);
	for (i = 0; i < IA_CSS_PIPE_ID_NUM; i++)
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.update_pipe[i] = true;
}

void atomisp_css_enable_raw_binning(struct atomisp_sub_device *asd,
					bool enable)
{
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];
	unsigned int pipe;

	if (asd->run_mode->val == ATOMISP_RUN_MODE_VIDEO)
		pipe = IA_CSS_PIPE_ID_VIDEO;
	else
		pipe = IA_CSS_PIPE_ID_PREVIEW;

	stream_env->pipe_extra_configs[pipe].enable_raw_binning = enable;
	stream_env->update_pipe[pipe] = true;
	if (enable)
		stream_env->pipe_configs[pipe].output_info[0].padded_width =
			stream_env->stream_config.input_config.effective_res.width;
}

void atomisp_css_enable_dz(struct atomisp_sub_device *asd, bool enable)
{
	int i;
	for (i = 0; i < IA_CSS_PIPE_ID_NUM; i++)
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
			.pipe_configs[i].enable_dz = enable;
}

void atomisp_css_capture_set_mode(struct atomisp_sub_device *asd,
				enum atomisp_css_capture_mode mode)
{
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];

	if (stream_env->pipe_configs[IA_CSS_PIPE_ID_CAPTURE]
		.default_capture_config.mode == mode)
		return;

	stream_env->pipe_configs[IA_CSS_PIPE_ID_CAPTURE].
					default_capture_config.mode = mode;
	stream_env->update_pipe[IA_CSS_PIPE_ID_CAPTURE] = true;
}

void atomisp_css_input_set_mode(struct atomisp_sub_device *asd,
				enum atomisp_css_input_mode mode)
{
	int i;
	struct atomisp_device *isp = asd->isp;
	unsigned int size_mem_words;
	for (i = 0; i < ATOMISP_INPUT_STREAM_NUM; i++)
		asd->stream_env[i].stream_config.mode = mode;

	if (isp->inputs[asd->input_curr].type == TEST_PATTERN) {
		struct ia_css_stream_config *s_config =
		    &asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream_config;
		s_config->mode = IA_CSS_INPUT_MODE_TPG;
		s_config->source.tpg.mode = IA_CSS_TPG_MODE_CHECKERBOARD;
		s_config->source.tpg.x_mask = (1 << 4) - 1;
		s_config->source.tpg.x_delta = -2;
		s_config->source.tpg.y_mask = (1 << 4) - 1;
		s_config->source.tpg.y_delta = 3;
		s_config->source.tpg.xy_mask = (1 << 8) - 1;
		return;
	}

	if (mode != IA_CSS_INPUT_MODE_BUFFERED_SENSOR)
		return;

	for (i = 0; i < ATOMISP_INPUT_STREAM_NUM; i++) {
		/*
		 * TODO: sensor needs to export the embedded_data_size_words
		 * information to atomisp for each setting.
		 * Here using a large safe value.
		 */
		struct ia_css_stream_config *s_config =
			&asd->stream_env[i].stream_config;

		if (s_config->input_config.input_res.width == 0)
			continue;

		if (ia_css_mipi_frame_calculate_size(
					s_config->input_config.input_res.width,
					s_config->input_config.input_res.height,
					s_config->input_config.format,
					true,
					0x13000,
					&size_mem_words) != IA_CSS_SUCCESS) {
			if (intel_mid_identify_cpu() ==
				INTEL_MID_CPU_CHIP_TANGIER)
				size_mem_words = CSS_MIPI_FRAME_BUFFER_SIZE_2;
			else
				size_mem_words = CSS_MIPI_FRAME_BUFFER_SIZE_1;
			dev_warn(asd->isp->dev,
				"ia_css_mipi_frame_calculate_size failed,"
				"applying pre-defined MIPI buffer size %u.\n",
				size_mem_words);
		}
		s_config->mipi_buffer_config.size_mem_words = size_mem_words;
		s_config->mipi_buffer_config.nof_mipi_buffers = 2;
	}
}

void atomisp_css_capture_enable_online(struct atomisp_sub_device *asd,
				unsigned short stream_index, bool enable)
{
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[stream_index];

	if (stream_env->stream_config.online == !!enable)
		return;

	stream_env->stream_config.online = !!enable;
	stream_env->update_pipe[IA_CSS_PIPE_ID_CAPTURE] = true;
}

void atomisp_css_preview_enable_online(struct atomisp_sub_device *asd,
				unsigned short stream_index, bool enable)
{
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[stream_index];
	int i;

	if (stream_env->stream_config.online != !!enable) {
		stream_env->stream_config.online = !!enable;
		for (i = 0; i < IA_CSS_PIPE_ID_NUM; i++)
			stream_env->update_pipe[i] = true;
	}
}

void atomisp_css_video_enable_online(struct atomisp_sub_device *asd,
							bool enable)
{
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_VIDEO];
	int i;

	if (stream_env->stream_config.online != enable) {
		stream_env->stream_config.online = enable;
		for (i = 0; i < IA_CSS_PIPE_ID_NUM; i++)
			stream_env->update_pipe[i] = true;
	}
}

void atomisp_css_enable_continuous(struct atomisp_sub_device *asd,
							bool enable)
{
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];
	int i;

	/*
	 * To SOC camera, there is only one YUVPP pipe in any case
	 * including ZSL/SDV/continuous viewfinder, so always set
	 * stream_config.continuous to 0.
	 */
	if (ATOMISP_USE_YUVPP(asd)) {
		stream_env->stream_config.continuous = 0;
		stream_env->stream_config.online = 1;
		return;
	}

	if (stream_env->stream_config.continuous != !!enable) {
		stream_env->stream_config.continuous = !!enable;
		stream_env->stream_config.pack_raw_pixels = true;
		for (i = 0; i < IA_CSS_PIPE_ID_NUM; i++)
			stream_env->update_pipe[i] = true;
	}
}

void atomisp_css_enable_cvf(struct atomisp_sub_device *asd,
				bool enable)
{
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];
	int i;

	if (stream_env->stream_config.disable_cont_viewfinder != !enable) {
		stream_env->stream_config.disable_cont_viewfinder = !enable;
		for (i = 0; i < IA_CSS_PIPE_ID_NUM; i++)
			stream_env->update_pipe[i] = true;
	}
}

int atomisp_css_input_configure_port(
		struct atomisp_sub_device *asd,
		mipi_port_ID_t port,
		unsigned int num_lanes,
		unsigned int timeout,
		unsigned int mipi_freq,
		enum atomisp_css_stream_format metadata_format,
		unsigned int metadata_width,
		unsigned int metadata_height)
{
	int i;
	struct atomisp_stream_env *stream_env;
	/*
	 * Calculate rx_count as follows:
	 * Input: mipi_freq                 : CSI-2 bus frequency in Hz
	 * UI = 1 / (2 * mipi_freq)         : period of one bit on the bus
	 * min = 85e-9 + 6 * UI             : Limits for rx_count in seconds
	 * max = 145e-9 + 10 * UI
	 * rxcount0 = min / (4 / mipi_freq) : convert seconds to byte clocks
	 * rxcount = rxcount0 - 2           : adjust for better results
	 * The formula below is simplified version of the above with
	 * 10-bit fixed points for improved accuracy.
	 */
	const unsigned int rxcount =
		min(((mipi_freq / 46000) - 1280) >> 10, 0xffU) * 0x01010101U;

	for (i = 0; i < ATOMISP_INPUT_STREAM_NUM; i++) {
		stream_env = &asd->stream_env[i];
		stream_env->stream_config.source.port.port = port;
		stream_env->stream_config.source.port.num_lanes = num_lanes;
		stream_env->stream_config.source.port.timeout = timeout;
		if (mipi_freq)
			stream_env->stream_config.source.port.rxcount = rxcount;
		stream_env->stream_config.
			metadata_config.data_type = metadata_format;
		stream_env->stream_config.
			metadata_config.resolution.width = metadata_width;
		stream_env->stream_config.
			metadata_config.resolution.height = metadata_height;
	}

	return 0;
}

int atomisp_css_frame_allocate(struct atomisp_css_frame **frame,
				unsigned int width, unsigned int height,
				enum atomisp_css_frame_format format,
				unsigned int padded_width,
				unsigned int raw_bit_depth)
{
	if (ia_css_frame_allocate(frame, width, height, format,
			padded_width, raw_bit_depth) != IA_CSS_SUCCESS)
		return -ENOMEM;

	return 0;
}

int atomisp_css_frame_allocate_from_info(struct atomisp_css_frame **frame,
				const struct atomisp_css_frame_info *info)
{
	if (ia_css_frame_allocate_from_info(frame, info) != IA_CSS_SUCCESS)
		return -ENOMEM;

	return 0;
}

void atomisp_css_frame_free(struct atomisp_css_frame *frame)
{
	ia_css_frame_free(frame);
}

int atomisp_css_frame_map(struct atomisp_css_frame **frame,
				const struct atomisp_css_frame_info *info,
				const void *data, uint16_t attribute,
				void *context)
{
	if (ia_css_frame_map(frame, info, data, attribute, context)
	    != IA_CSS_SUCCESS)
		return -ENOMEM;

	return 0;
}

int atomisp_css_set_black_frame(struct atomisp_sub_device *asd,
				const struct atomisp_css_frame *raw_black_frame)
{
	if (sh_css_set_black_frame(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		raw_black_frame) != IA_CSS_SUCCESS)
		return -ENOMEM;

	return 0;
}

int atomisp_css_allocate_continuous_frames(bool init_time,
				struct atomisp_sub_device *asd)
{
	if (ia_css_alloc_continuous_frame_remain(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream)
			!= IA_CSS_SUCCESS)
		return -EINVAL;
	return 0;
}

void atomisp_css_update_continuous_frames(struct atomisp_sub_device *asd)
{
	ia_css_update_continuous_frames(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream);
}

int atomisp_css_stop(struct atomisp_sub_device *asd,
			enum atomisp_css_pipe_id pipe_id, bool in_reset)
{
	struct atomisp_device *isp = asd->isp;
	struct atomisp_s3a_buf *s3a_buf;
	struct atomisp_dis_buf *dis_buf;
	struct atomisp_metadata_buf *md_buf;
	unsigned long irqflags;
	unsigned int i;

	/* if is called in atomisp_reset(), force destroy stream */
	if (__destroy_streams(asd, true))
		dev_err(isp->dev, "destroy stream failed.\n");

	/* if is called in atomisp_reset(), force destroy all pipes */
	if (__destroy_pipes(asd, true))
		dev_err(isp->dev, "destroy pipes failed.\n");

	atomisp_init_raw_buffer_bitmap(asd);

	/*
	 * SP can not be stop if other streams are in use
	 */
	if (atomisp_streaming_count(isp) == 0)
		ia_css_stop_sp();

	if (!in_reset) {
		struct atomisp_stream_env *stream_env;
		int i, j;
		for (i = 0; i < ATOMISP_INPUT_STREAM_NUM; i++) {
			stream_env = &asd->stream_env[i];
			for (j = 0; j < IA_CSS_PIPE_ID_NUM; j++) {
				ia_css_pipe_config_defaults(
					&stream_env->pipe_configs[j]);
				ia_css_pipe_extra_config_defaults(
					&stream_env->pipe_extra_configs[j]);
			}
			ia_css_stream_config_defaults(
				&stream_env->stream_config);
		}
		atomisp_isp_parameters_clean_up(&asd->params.config);
		asd->params.css_update_params_needed = false;
	}

	/* move stats buffers to free queue list */
	while (!list_empty(&asd->s3a_stats_in_css)) {
		s3a_buf = list_entry(asd->s3a_stats_in_css.next,
				struct atomisp_s3a_buf, list);
		list_del(&s3a_buf->list);
		list_add_tail(&s3a_buf->list, &asd->s3a_stats);
	}
	while (!list_empty(&asd->s3a_stats_ready)) {
		s3a_buf = list_entry(asd->s3a_stats_ready.next,
				struct atomisp_s3a_buf, list);
		list_del(&s3a_buf->list);
		list_add_tail(&s3a_buf->list, &asd->s3a_stats);
	}

	spin_lock_irqsave(&asd->dis_stats_lock, irqflags);
	while (!list_empty(&asd->dis_stats_in_css)) {
		dis_buf = list_entry(asd->dis_stats_in_css.next,
				struct atomisp_dis_buf, list);
		list_del(&dis_buf->list);
		list_add_tail(&dis_buf->list, &asd->dis_stats);
	}
	asd->params.dis_proj_data_valid = false;
	spin_unlock_irqrestore(&asd->dis_stats_lock, irqflags);

	for (i = 0; i < ATOMISP_METADATA_TYPE_NUM; i++) {
		while (!list_empty(&asd->metadata_in_css[i])) {
			md_buf = list_entry(asd->metadata_in_css[i].next,
					struct atomisp_metadata_buf, list);
			list_del(&md_buf->list);
			list_add_tail(&md_buf->list, &asd->metadata[i]);
		}
		while (!list_empty(&asd->metadata_ready[i])) {
			md_buf = list_entry(asd->metadata_ready[i].next,
					struct atomisp_metadata_buf, list);
			list_del(&md_buf->list);
			list_add_tail(&md_buf->list, &asd->metadata[i]);
		}
	}

	atomisp_flush_params_queue(&asd->video_out_capture);
	atomisp_flush_params_queue(&asd->video_out_vf);
	atomisp_flush_params_queue(&asd->video_out_preview);
	atomisp_flush_params_queue(&asd->video_out_video_capture);
	atomisp_free_css_parameters(&asd->params.css_param);
	memset(&asd->params.css_param, 0, sizeof(asd->params.css_param));
	return 0;
}

int atomisp_css_continuous_set_num_raw_frames(
					struct atomisp_sub_device *asd,
					int num_frames)
{
	if (asd->enable_raw_buffer_lock->val) {
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.stream_config.init_num_cont_raw_buf =
			ATOMISP_CSS2_NUM_OFFLINE_INIT_CONTINUOUS_FRAMES_LOCK_EN;
		if (asd->run_mode->val == ATOMISP_RUN_MODE_VIDEO &&
		    asd->params.video_dis_en)
			asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
			.stream_config.init_num_cont_raw_buf +=
				ATOMISP_CSS2_NUM_DVS_FRAME_DELAY;
	} else {
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.stream_config.init_num_cont_raw_buf =
			ATOMISP_CSS2_NUM_OFFLINE_INIT_CONTINUOUS_FRAMES;
	}

	if (asd->params.video_dis_en)
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
			.stream_config.init_num_cont_raw_buf +=
				ATOMISP_CSS2_NUM_DVS_FRAME_DELAY;

	asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.stream_config.target_num_cont_raw_buf = num_frames;
	return 0;
}

void atomisp_css_disable_vf_pp(struct atomisp_sub_device *asd,
			       bool disable)
{
	int i;

	for (i = 0; i < IA_CSS_PIPE_ID_NUM; i++)
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.pipe_extra_configs[i].disable_vf_pp = !!disable;
}

static enum ia_css_pipe_mode __pipe_id_to_pipe_mode(
					struct atomisp_sub_device *asd,
					enum ia_css_pipe_id pipe_id)
{
	struct atomisp_device *isp = asd->isp;
	struct camera_mipi_info *mipi_info = atomisp_to_sensor_mipi_info(
			isp->inputs[asd->input_curr].camera);

	switch (pipe_id) {
	case IA_CSS_PIPE_ID_COPY:
		/* Currently only YUVPP mode supports YUV420_Legacy format.
		 * Revert this when other pipe modes can support
		 * YUV420_Legacy format.
		 */
		if (mipi_info && mipi_info->input_format ==
			ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY)
			return IA_CSS_PIPE_MODE_YUVPP;
		return IA_CSS_PIPE_MODE_COPY;
	case IA_CSS_PIPE_ID_PREVIEW:
		return IA_CSS_PIPE_MODE_PREVIEW;
	case IA_CSS_PIPE_ID_CAPTURE:
		return IA_CSS_PIPE_MODE_CAPTURE;
	case IA_CSS_PIPE_ID_VIDEO:
		return IA_CSS_PIPE_MODE_VIDEO;
	case IA_CSS_PIPE_ID_ACC:
		return IA_CSS_PIPE_MODE_ACC;
	case IA_CSS_PIPE_ID_YUVPP:
		return IA_CSS_PIPE_MODE_YUVPP;
	default:
		WARN_ON(1);
		return IA_CSS_PIPE_MODE_PREVIEW;
	}

}

static void __configure_output(struct atomisp_sub_device *asd,
			       unsigned int stream_index,
			       unsigned int width, unsigned int height,
			       unsigned int min_width,
			       enum ia_css_frame_format format,
			       enum ia_css_pipe_id pipe_id)
{
	struct atomisp_device *isp = asd->isp;
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[stream_index];
	struct ia_css_stream_config *s_config = &stream_env->stream_config;

	stream_env->pipe_configs[pipe_id].mode =
		__pipe_id_to_pipe_mode(asd, pipe_id);
	stream_env->update_pipe[pipe_id] = true;

	stream_env->pipe_configs[pipe_id].output_info[0].res.width = width;
	stream_env->pipe_configs[pipe_id].output_info[0].res.height = height;
	stream_env->pipe_configs[pipe_id].output_info[0].format = format;
	stream_env->pipe_configs[pipe_id].output_info[0].padded_width = min_width;

	/* isp binary 2.2 specific setting*/
	if (width > s_config->input_config.effective_res.width ||
	    height > s_config->input_config.effective_res.height) {
		s_config->input_config.effective_res.width = width;
		s_config->input_config.effective_res.height = height;
	}

	dev_dbg(isp->dev, "configuring pipe[%d] output info w=%d.h=%d.f=%d.\n",
		pipe_id, width, height, format);
}

static void __configure_video_preview_output(struct atomisp_sub_device *asd,
			       unsigned int stream_index,
			       unsigned int width, unsigned int height,
			       unsigned int min_width,
			       enum ia_css_frame_format format,
			       enum ia_css_pipe_id pipe_id)
{
	struct atomisp_device *isp = asd->isp;
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[stream_index];
	struct ia_css_frame_info *css_output_info;
	struct ia_css_stream_config *stream_config = &stream_env->stream_config;

	stream_env->pipe_configs[pipe_id].mode =
		__pipe_id_to_pipe_mode(asd, pipe_id);
	stream_env->update_pipe[pipe_id] = true;

	/*
	 * second_output will be as video main output in SDV mode
	 * with SOC camera. output will be as video main output in
	 * normal video mode.
	 */
	if (asd->continuous_mode->val)
		css_output_info = &stream_env->pipe_configs[pipe_id].
			output_info[ATOMISP_CSS_OUTPUT_SECOND_INDEX];
	else
		css_output_info = &stream_env->pipe_configs[pipe_id].
			output_info[ATOMISP_CSS_OUTPUT_DEFAULT_INDEX];

	css_output_info->res.width = width;
	css_output_info->res.height = height;
	css_output_info->format = format;
	css_output_info->padded_width = min_width;

	/* isp binary 2.2 specific setting*/
	if (width > stream_config->input_config.effective_res.width ||
	    height > stream_config->input_config.effective_res.height) {
		stream_config->input_config.effective_res.width = width;
		stream_config->input_config.effective_res.height = height;
	}

	dev_dbg(isp->dev, "configuring pipe[%d] output info w=%d.h=%d.f=%d.\n",
		pipe_id, width, height, format);
}

/*
 * For CSS2.1, capture pipe uses capture_pp_in_res to configure yuv
 * downscaling input resolution.
 */
static void __configure_capture_pp_input(struct atomisp_sub_device *asd,
				 unsigned int width, unsigned int height,
				 enum ia_css_pipe_id pipe_id)
{
	struct atomisp_device *isp = asd->isp;
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];
	struct ia_css_stream_config *stream_config = &stream_env->stream_config;
	struct ia_css_pipe_config *pipe_configs =
		&stream_env->pipe_configs[pipe_id];
	struct ia_css_pipe_extra_config *pipe_extra_configs =
		&stream_env->pipe_extra_configs[pipe_id];
	unsigned int hor_ds_factor = 0, ver_ds_factor = 0;

	if (width == 0 && height == 0)
		return;

	if (width * 9 / 10 < pipe_configs->output_info[0].res.width ||
	    height * 9 / 10 < pipe_configs->output_info[0].res.height)
		return;
	/* here just copy the calculation in css */
	hor_ds_factor = CEIL_DIV(width >> 1,
			pipe_configs->output_info[0].res.width);
	ver_ds_factor = CEIL_DIV(height >> 1,
			pipe_configs->output_info[0].res.height);

	if ((asd->isp->media_dev.hw_revision <
	    (ATOMISP_HW_REVISION_ISP2401 << ATOMISP_HW_REVISION_SHIFT) ||
	    IS_CHT) && hor_ds_factor != ver_ds_factor) {
		dev_warn(asd->isp->dev,
				"Cropping for capture due to FW limitation");
		return;
	}

	pipe_configs->mode = __pipe_id_to_pipe_mode(asd, pipe_id);
	stream_env->update_pipe[pipe_id] = true;

	pipe_extra_configs->enable_yuv_ds = true;

	pipe_configs->capt_pp_in_res.width =
		stream_config->input_config.effective_res.width;
	pipe_configs->capt_pp_in_res.height =
		stream_config->input_config.effective_res.height;

	dev_dbg(isp->dev, "configuring pipe[%d]capture pp input w=%d.h=%d.\n",
		pipe_id, width, height);
}

/*
 * For CSS2.1, preview pipe could support bayer downscaling, yuv decimation and
 * yuv downscaling, which needs addtional configurations.
 */
static void __configure_preview_pp_input(struct atomisp_sub_device *asd,
				 unsigned int width, unsigned int height,
				 enum ia_css_pipe_id pipe_id)
{
	struct atomisp_device *isp = asd->isp;
	int out_width, out_height, yuv_ds_in_width, yuv_ds_in_height;
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];
	struct ia_css_stream_config *stream_config = &stream_env->stream_config;
	struct ia_css_pipe_config *pipe_configs =
		&stream_env->pipe_configs[pipe_id];
	struct ia_css_pipe_extra_config *pipe_extra_configs =
		&stream_env->pipe_extra_configs[pipe_id];
	struct ia_css_resolution *bayer_ds_out_res =
		&pipe_configs->bayer_ds_out_res;
	struct ia_css_resolution *vf_pp_in_res =
		&pipe_configs->vf_pp_in_res;
	struct ia_css_resolution  *effective_res =
		&stream_config->input_config.effective_res;

	const struct bayer_ds_factor bds_fct[] = {{2, 1}, {3, 2}, {5, 4} };
	/*
	 * BZ201033: YUV decimation factor of 4 causes couple of rightmost
	 * columns to be shaded. Remove this factor to work around the CSS bug.
	 * const unsigned int yuv_dec_fct[] = {4, 2};
	 */
	const unsigned int yuv_dec_fct[] = { 2 };
	unsigned int i;

	if (width == 0 && height == 0)
		return;

	pipe_configs->mode = __pipe_id_to_pipe_mode(asd, pipe_id);
	stream_env->update_pipe[pipe_id] = true;

	out_width = pipe_configs->output_info[0].res.width;
	out_height = pipe_configs->output_info[0].res.height;

	/*
	 * The ISP could do bayer downscaling, yuv decimation and yuv
	 * downscaling:
	 * 1: Bayer Downscaling: between effective resolution and
	 * bayer_ds_res_out;
	 * 2: YUV Decimation: between bayer_ds_res_out and vf_pp_in_res;
	 * 3: YUV Downscaling: between vf_pp_in_res and final vf output
	 *
	 * Rule for Bayer Downscaling: support factor 2, 1.5 and 1.25
	 * Rule for YUV Decimation: support factor 2, 4
	 * Rule for YUV Downscaling: arbitary value below 2
	 *
	 * General rule of factor distribution among these stages:
	 * 1: try to do Bayer downscaling first if not in online mode.
	 * 2: try to do maximum of 2 for YUV downscaling
	 * 3: the remainling for YUV decimation
	 *
	 * Note:
	 * Do not configure bayer_ds_out_res if:
	 * online == 1 or continuous == 0 or raw_binning = 0
	 */
	if (stream_config->online || !stream_config->continuous ||
			!pipe_extra_configs->enable_raw_binning) {
		bayer_ds_out_res->width = 0;
		bayer_ds_out_res->height = 0;
	} else {
		bayer_ds_out_res->width = effective_res->width;
		bayer_ds_out_res->height = effective_res->height;

		for (i = 0; i < ARRAY_SIZE(bds_fct); i++) {
			if (effective_res->width >= out_width *
			    bds_fct[i].numerator / bds_fct[i].denominator &&
			    effective_res->height >= out_height *
			    bds_fct[i].numerator / bds_fct[i].denominator) {
				bayer_ds_out_res->width =
				    effective_res->width *
				    bds_fct[i].denominator /
				    bds_fct[i].numerator;
				bayer_ds_out_res->height =
				    effective_res->height *
				    bds_fct[i].denominator /
				    bds_fct[i].numerator;
				break;
			}
		}
	}
	/*
	 * calculate YUV Decimation, YUV downscaling facor:
	 * YUV Downscaling factor must not exceed 2.
	 * YUV Decimation factor could be 2, 4.
	 */
	/* first decide the yuv_ds input resolution */
	if (bayer_ds_out_res->width == 0) {
		yuv_ds_in_width = effective_res->width;
		yuv_ds_in_height = effective_res->height;
	} else {
		yuv_ds_in_width = bayer_ds_out_res->width;
		yuv_ds_in_height = bayer_ds_out_res->height;
	}

	vf_pp_in_res->width = yuv_ds_in_width;
	vf_pp_in_res->height = yuv_ds_in_height;

	/* find out the yuv decimation factor */
	for (i = 0; i < ARRAY_SIZE(yuv_dec_fct); i++) {
		if (yuv_ds_in_width >= out_width * yuv_dec_fct[i] &&
		    yuv_ds_in_height >= out_height * yuv_dec_fct[i]) {
			vf_pp_in_res->width = yuv_ds_in_width / yuv_dec_fct[i];
			vf_pp_in_res->height = yuv_ds_in_height / yuv_dec_fct[i];
			break;
		}
	}

	if (vf_pp_in_res->width == out_width &&
		vf_pp_in_res->height == out_height) {
		pipe_extra_configs->enable_yuv_ds = false;
		vf_pp_in_res->width = 0;
		vf_pp_in_res->height = 0;
	} else {
		pipe_extra_configs->enable_yuv_ds = true;
	}

	dev_dbg(isp->dev, "configuring pipe[%d]preview pp input w=%d.h=%d.\n",
		pipe_id, width, height);
}

/*
 * For CSS2.1, offline video pipe could support bayer decimation, and
 * yuv downscaling, which needs addtional configurations.
 */
static void __configure_video_pp_input(struct atomisp_sub_device *asd,
				 unsigned int width, unsigned int height,
				 enum ia_css_pipe_id pipe_id)
{
	struct atomisp_device *isp = asd->isp;
	int out_width, out_height;
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];
	struct ia_css_stream_config *stream_config = &stream_env->stream_config;
	struct ia_css_pipe_config *pipe_configs =
		&stream_env->pipe_configs[pipe_id];
	struct ia_css_pipe_extra_config *pipe_extra_configs =
		&stream_env->pipe_extra_configs[pipe_id];
	struct ia_css_resolution *bayer_ds_out_res =
		&pipe_configs->bayer_ds_out_res;
	struct ia_css_resolution  *effective_res =
		&stream_config->input_config.effective_res;

	const struct bayer_ds_factor bds_factors[] = {
		{8, 1}, {6, 1}, {4, 1}, {3, 1}, {2, 1}, {3, 2} };
	unsigned int i;

	if (width == 0 && height == 0)
		return;

	pipe_configs->mode = __pipe_id_to_pipe_mode(asd, pipe_id);
	stream_env->update_pipe[pipe_id] = true;

	pipe_extra_configs->enable_yuv_ds = false;

	/*
	 * If DVS is enabled,  video binary will take care the dvs envelope
	 * and usually the bayer_ds_out_res should be larger than 120% of
	 * destination resolution, the extra 20% will be cropped as DVS
	 * envelope. But,  if the bayer_ds_out_res is less than 120% of the
	 * destination. The ISP can still work,  but DVS quality is not good.
	 */
	/* taking at least 10% as envelope */
	if (asd->params.video_dis_en) {
		out_width = pipe_configs->output_info[0].res.width * 110 / 100;
		out_height = pipe_configs->output_info[0].res.height * 110 / 100;
	} else {
		out_width = pipe_configs->output_info[0].res.width;
		out_height = pipe_configs->output_info[0].res.height;
	}

	/*
	 * calculate bayer decimate factor:
	 * 1: only 1.5, 2, 4 and 8 get supported
	 * 2: Do not configure bayer_ds_out_res if:
	 *    online == 1 or continuous == 0 or raw_binning = 0
	 */
	if (stream_config->online || !stream_config->continuous) {
		bayer_ds_out_res->width = 0;
		bayer_ds_out_res->height = 0;
		goto done;
	}

	pipe_extra_configs->enable_raw_binning = true;
	bayer_ds_out_res->width = effective_res->width;
	bayer_ds_out_res->height = effective_res->height;

	for (i = 0; i < sizeof(bds_factors) / sizeof(struct bayer_ds_factor);
	     i++) {
		if (effective_res->width >= out_width *
		    bds_factors[i].numerator / bds_factors[i].denominator &&
		    effective_res->height >= out_height *
		    bds_factors[i].numerator / bds_factors[i].denominator) {
			bayer_ds_out_res->width = effective_res->width *
			    bds_factors[i].denominator /
			    bds_factors[i].numerator;
			bayer_ds_out_res->height = effective_res->height *
			    bds_factors[i].denominator /
			    bds_factors[i].numerator;
			break;
		}
	}

	/*
	 * DVS is cropped from BDS output, so we do not really need to set the
	 * envelope to 20% of output resolution here. always set it to 12x12
	 * per firmware requirement.
	 */
	pipe_configs->dvs_envelope.width = 12;
	pipe_configs->dvs_envelope.height = 12;

done:
	if (pipe_id == IA_CSS_PIPE_ID_YUVPP)
		stream_config->left_padding = -1;
	else
		stream_config->left_padding = 12;
	dev_dbg(isp->dev, "configuring pipe[%d]video pp input w=%d.h=%d.\n",
		pipe_id, width, height);
}

static void __configure_vf_output(struct atomisp_sub_device *asd,
				  unsigned int width, unsigned int height,
				  unsigned int min_width,
				  enum atomisp_css_frame_format format,
				  enum ia_css_pipe_id pipe_id)
{
	struct atomisp_device *isp = asd->isp;
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];
	stream_env->pipe_configs[pipe_id].mode =
		__pipe_id_to_pipe_mode(asd, pipe_id);
	stream_env->update_pipe[pipe_id] = true;

	stream_env->pipe_configs[pipe_id].vf_output_info[0].res.width = width;
	stream_env->pipe_configs[pipe_id].vf_output_info[0].res.height = height;
	stream_env->pipe_configs[pipe_id].vf_output_info[0].format = format;
	stream_env->pipe_configs[pipe_id].vf_output_info[0].padded_width =
		min_width;
	dev_dbg(isp->dev,
		"configuring pipe[%d] vf output info w=%d.h=%d.f=%d.\n",
		 pipe_id, width, height, format);
}

static void __configure_video_vf_output(struct atomisp_sub_device *asd,
				  unsigned int width, unsigned int height,
				  unsigned int min_width,
				  enum atomisp_css_frame_format format,
				  enum ia_css_pipe_id pipe_id)
{
	struct atomisp_device *isp = asd->isp;
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];
	struct ia_css_frame_info *css_output_info;
	stream_env->pipe_configs[pipe_id].mode =
					__pipe_id_to_pipe_mode(asd, pipe_id);
	stream_env->update_pipe[pipe_id] = true;

	/*
	 * second_vf_output will be as video viewfinder in SDV mode
	 * with SOC camera. vf_output will be as video viewfinder in
	 * normal video mode.
	 */
	if (asd->continuous_mode->val)
		css_output_info = &stream_env->pipe_configs[pipe_id].
			vf_output_info[ATOMISP_CSS_OUTPUT_SECOND_INDEX];
	else
		css_output_info = &stream_env->pipe_configs[pipe_id].
			vf_output_info[ATOMISP_CSS_OUTPUT_DEFAULT_INDEX];

	css_output_info->res.width = width;
	css_output_info->res.height = height;
	css_output_info->format = format;
	css_output_info->padded_width = min_width;
	dev_dbg(isp->dev,
		"configuring pipe[%d] vf output info w=%d.h=%d.f=%d.\n",
		 pipe_id, width, height, format);
}

static int __get_frame_info(struct atomisp_sub_device *asd,
				unsigned int stream_index,
				struct atomisp_css_frame_info *info,
				enum frame_info_type type,
				enum ia_css_pipe_id pipe_id)
{
	struct atomisp_device *isp = asd->isp;
	enum ia_css_err ret;
	struct ia_css_pipe_info p_info;

	/* FIXME! No need to destroy/recreate all streams */
	if (__destroy_streams(asd, true))
		dev_warn(isp->dev, "destroy stream failed.\n");

	if (__destroy_pipes(asd, true))
		dev_warn(isp->dev, "destroy pipe failed.\n");

	if (__create_pipes(asd))
		return -EINVAL;

	if (__create_streams(asd))
		goto stream_err;

	ret = ia_css_pipe_get_info(
		asd->stream_env[stream_index]
		.pipes[pipe_id], &p_info);
	if (ret == IA_CSS_SUCCESS) {
		switch (type) {
		case ATOMISP_CSS_VF_FRAME:
			*info = p_info.vf_output_info[0];
			dev_dbg(isp->dev, "getting vf frame info.\n");
			break;
		case ATOMISP_CSS_SECOND_VF_FRAME:
			*info = p_info.vf_output_info[1];
			dev_dbg(isp->dev, "getting second vf frame info.\n");
			break;
		case ATOMISP_CSS_OUTPUT_FRAME:
			*info = p_info.output_info[0];
			dev_dbg(isp->dev, "getting main frame info.\n");
			break;
		case ATOMISP_CSS_SECOND_OUTPUT_FRAME:
			*info = p_info.output_info[1];
			dev_dbg(isp->dev, "getting second main frame info.\n");
			break;
		case ATOMISP_CSS_RAW_FRAME:
			*info = p_info.raw_output_info;
			dev_dbg(isp->dev, "getting raw frame info.\n");
		}
		dev_dbg(isp->dev, "get frame info: w=%d, h=%d, num_invalid_frames %d.\n",
			info->res.width, info->res.height, p_info.num_invalid_frames);
		return 0;
	}

stream_err:
	__destroy_pipes(asd, true);
	return -EINVAL;
}

unsigned int atomisp_get_pipe_index(struct atomisp_sub_device *asd,
					uint16_t source_pad)
{
	struct atomisp_device *isp = asd->isp;
	/*
	 * to SOC camera, use yuvpp pipe.
	 */
	if (ATOMISP_USE_YUVPP(asd))
		return IA_CSS_PIPE_ID_YUVPP;

	switch (source_pad) {
	case ATOMISP_SUBDEV_PAD_SOURCE_VIDEO:
		if (asd->yuvpp_mode)
			return IA_CSS_PIPE_ID_YUVPP;
		if (asd->copy_mode)
			return IA_CSS_PIPE_ID_COPY;
		if (asd->run_mode->val == ATOMISP_RUN_MODE_VIDEO
		    || asd->vfpp->val == ATOMISP_VFPP_DISABLE_SCALER)
			return IA_CSS_PIPE_ID_VIDEO;
		else
			return IA_CSS_PIPE_ID_CAPTURE;
	case ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE:
		if (asd->copy_mode)
			return IA_CSS_PIPE_ID_COPY;
		return IA_CSS_PIPE_ID_CAPTURE;
	case ATOMISP_SUBDEV_PAD_SOURCE_VF:
		if (!atomisp_is_mbuscode_raw(
		    asd->fmt[asd->capture_pad].fmt.code))
			return IA_CSS_PIPE_ID_CAPTURE;
	case ATOMISP_SUBDEV_PAD_SOURCE_PREVIEW:
		if (asd->yuvpp_mode)
			return IA_CSS_PIPE_ID_YUVPP;
		if (asd->copy_mode)
			return IA_CSS_PIPE_ID_COPY;
		if (asd->run_mode->val == ATOMISP_RUN_MODE_VIDEO)
			return IA_CSS_PIPE_ID_VIDEO;
		else
			return IA_CSS_PIPE_ID_PREVIEW;
	}
	dev_warn(isp->dev,
		 "invalid source pad:%d, return default preview pipe index.\n",
		 source_pad);
	return IA_CSS_PIPE_ID_PREVIEW;
}

int atomisp_get_css_frame_info(struct atomisp_sub_device *asd,
				uint16_t source_pad,
				struct atomisp_css_frame_info *frame_info)
{
	struct ia_css_pipe_info info;
	int pipe_index = atomisp_get_pipe_index(asd, source_pad);
	int stream_index;
	struct atomisp_device *isp = asd->isp;

	if (ATOMISP_SOC_CAMERA(asd))
		stream_index = atomisp_source_pad_to_stream_id(asd, source_pad);
	else {
		stream_index = (pipe_index == IA_CSS_PIPE_ID_YUVPP) ?
			   ATOMISP_INPUT_STREAM_VIDEO :
			   atomisp_source_pad_to_stream_id(asd, source_pad);
	}

	if (IA_CSS_SUCCESS != ia_css_pipe_get_info(asd->stream_env[stream_index]
				 .pipes[pipe_index], &info)) {
		dev_err(isp->dev, "ia_css_pipe_get_info FAILED");
		return -EINVAL;
	}

	switch (source_pad) {
	case ATOMISP_SUBDEV_PAD_SOURCE_CAPTURE:
		*frame_info = info.output_info[0];
		break;
	case ATOMISP_SUBDEV_PAD_SOURCE_VIDEO:
		if (ATOMISP_USE_YUVPP(asd) && asd->continuous_mode->val)
			*frame_info = info.
				output_info[ATOMISP_CSS_OUTPUT_SECOND_INDEX];
		else
			*frame_info = info.
				output_info[ATOMISP_CSS_OUTPUT_DEFAULT_INDEX];
		break;
	case ATOMISP_SUBDEV_PAD_SOURCE_VF:
		if (stream_index == ATOMISP_INPUT_STREAM_POSTVIEW)
			*frame_info = info.output_info[0];
		else
			*frame_info = info.vf_output_info[0];
		break;
	case ATOMISP_SUBDEV_PAD_SOURCE_PREVIEW:
		if (asd->run_mode->val == ATOMISP_RUN_MODE_VIDEO &&
		    (pipe_index == IA_CSS_PIPE_ID_VIDEO ||
		     pipe_index == IA_CSS_PIPE_ID_YUVPP))
			if (ATOMISP_USE_YUVPP(asd) && asd->continuous_mode->val)
				*frame_info = info.
					vf_output_info[ATOMISP_CSS_OUTPUT_SECOND_INDEX];
			else
				*frame_info = info.
					vf_output_info[ATOMISP_CSS_OUTPUT_DEFAULT_INDEX];
		else if (ATOMISP_USE_YUVPP(asd) && asd->continuous_mode->val)
			*frame_info =
				info.output_info[ATOMISP_CSS_OUTPUT_SECOND_INDEX];
		else
			*frame_info =
				info.output_info[ATOMISP_CSS_OUTPUT_DEFAULT_INDEX];

		break;
	default:
		frame_info = NULL;
		break;
	}
	return frame_info ? 0 : -EINVAL;
}

int atomisp_css_copy_configure_output(struct atomisp_sub_device *asd,
				unsigned int stream_index,
				unsigned int width, unsigned int height,
				unsigned int padded_width,
				enum atomisp_css_frame_format format)
{
	asd->stream_env[stream_index].pipe_configs[IA_CSS_PIPE_ID_COPY].
					default_capture_config.mode =
					CSS_CAPTURE_MODE_RAW;

	__configure_output(asd, stream_index, width, height, padded_width,
			   format, IA_CSS_PIPE_ID_COPY);
	return 0;
}

int atomisp_css_yuvpp_configure_output(struct atomisp_sub_device *asd,
				unsigned int stream_index,
				unsigned int width, unsigned int height,
				unsigned int padded_width,
				enum atomisp_css_frame_format format)
{
	asd->stream_env[stream_index].pipe_configs[IA_CSS_PIPE_ID_YUVPP].
					default_capture_config.mode =
					CSS_CAPTURE_MODE_RAW;

	__configure_output(asd, stream_index, width, height, padded_width,
			   format, IA_CSS_PIPE_ID_YUVPP);
	return 0;
}

int atomisp_css_yuvpp_configure_viewfinder(
				struct atomisp_sub_device *asd,
				unsigned int stream_index,
				unsigned int width, unsigned int height,
				unsigned int min_width,
				enum atomisp_css_frame_format format)
{
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[stream_index];
	enum ia_css_pipe_id pipe_id = IA_CSS_PIPE_ID_YUVPP;

	stream_env->pipe_configs[pipe_id].mode =
		__pipe_id_to_pipe_mode(asd, pipe_id);
	stream_env->update_pipe[pipe_id] = true;

	stream_env->pipe_configs[pipe_id].vf_output_info[0].res.width = width;
	stream_env->pipe_configs[pipe_id].vf_output_info[0].res.height = height;
	stream_env->pipe_configs[pipe_id].vf_output_info[0].format = format;
	stream_env->pipe_configs[pipe_id].vf_output_info[0].padded_width =
		min_width;
	return 0;
}

int atomisp_css_yuvpp_get_output_frame_info(
					struct atomisp_sub_device *asd,
					unsigned int stream_index,
					struct atomisp_css_frame_info *info)
{
	return __get_frame_info(asd, stream_index, info,
			ATOMISP_CSS_OUTPUT_FRAME, IA_CSS_PIPE_ID_YUVPP);
}

int atomisp_css_yuvpp_get_viewfinder_frame_info(
					struct atomisp_sub_device *asd,
					unsigned int stream_index,
					struct atomisp_css_frame_info *info)
{
	return __get_frame_info(asd, stream_index, info,
			ATOMISP_CSS_VF_FRAME, IA_CSS_PIPE_ID_YUVPP);
}

int atomisp_css_preview_configure_output(struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height,
				unsigned int min_width,
				enum atomisp_css_frame_format format)
{
	/*
	 * to SOC camera, use yuvpp pipe.
	 */
	if (ATOMISP_USE_YUVPP(asd))
		__configure_video_preview_output(asd, ATOMISP_INPUT_STREAM_GENERAL, width, height,
						min_width, format, IA_CSS_PIPE_ID_YUVPP);
	else
		__configure_output(asd, ATOMISP_INPUT_STREAM_GENERAL, width, height,
					min_width, format, IA_CSS_PIPE_ID_PREVIEW);
	return 0;
}

int atomisp_css_capture_configure_output(struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height,
				unsigned int min_width,
				enum atomisp_css_frame_format format)
{
	enum ia_css_pipe_id pipe_id;

	/*
	 * to SOC camera, use yuvpp pipe.
	 */
	if (ATOMISP_USE_YUVPP(asd))
		pipe_id = IA_CSS_PIPE_ID_YUVPP;
	else
		pipe_id = IA_CSS_PIPE_ID_CAPTURE;

	__configure_output(asd, ATOMISP_INPUT_STREAM_GENERAL, width, height,
						min_width, format, pipe_id);
	return 0;
}

int atomisp_css_video_configure_output(struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height,
				unsigned int min_width,
				enum atomisp_css_frame_format format)
{
	/*
	 * to SOC camera, use yuvpp pipe.
	 */
	if (ATOMISP_USE_YUVPP(asd))
		__configure_video_preview_output(asd, ATOMISP_INPUT_STREAM_GENERAL, width, height,
					min_width, format, IA_CSS_PIPE_ID_YUVPP);
	else
		__configure_output(asd, ATOMISP_INPUT_STREAM_GENERAL, width, height,
					min_width, format, IA_CSS_PIPE_ID_VIDEO);
	return 0;
}

int atomisp_css_video_configure_viewfinder(
				struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height,
				unsigned int min_width,
				enum atomisp_css_frame_format format)
{
	/*
	 * to SOC camera, video will use yuvpp pipe.
	 */
	if (ATOMISP_USE_YUVPP(asd))
		__configure_video_vf_output(asd, width, height, min_width, format,
							IA_CSS_PIPE_ID_YUVPP);
	else
		__configure_vf_output(asd, width, height, min_width, format,
							IA_CSS_PIPE_ID_VIDEO);
	return 0;
}

int atomisp_css_capture_configure_viewfinder(
				struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height,
				unsigned int min_width,
				enum atomisp_css_frame_format format)
{
	enum ia_css_pipe_id pipe_id;

	/*
	 * to SOC camera, video will use yuvpp pipe.
	 */
	if (ATOMISP_USE_YUVPP(asd))
		pipe_id = IA_CSS_PIPE_ID_YUVPP;
	else
		pipe_id = IA_CSS_PIPE_ID_CAPTURE;

	__configure_vf_output(asd, width, height, min_width, format,
							pipe_id);
	return 0;
}

int atomisp_css_video_get_viewfinder_frame_info(
					struct atomisp_sub_device *asd,
					struct atomisp_css_frame_info *info)
{
	enum ia_css_pipe_id pipe_id;
	enum frame_info_type frame_type = ATOMISP_CSS_VF_FRAME;

	if (ATOMISP_USE_YUVPP(asd)) {
		pipe_id = IA_CSS_PIPE_ID_YUVPP;
		if (asd->continuous_mode->val)
			frame_type = ATOMISP_CSS_SECOND_VF_FRAME;
	} else {
		pipe_id = IA_CSS_PIPE_ID_VIDEO;
	}

	return __get_frame_info(asd, ATOMISP_INPUT_STREAM_GENERAL, info,
						frame_type, pipe_id);
}

int atomisp_css_capture_get_viewfinder_frame_info(
					struct atomisp_sub_device *asd,
					struct atomisp_css_frame_info *info)
{
	enum ia_css_pipe_id pipe_id;

	if (ATOMISP_USE_YUVPP(asd))
		pipe_id = IA_CSS_PIPE_ID_YUVPP;
	else
		pipe_id = IA_CSS_PIPE_ID_CAPTURE;

	return __get_frame_info(asd, ATOMISP_INPUT_STREAM_GENERAL, info,
						ATOMISP_CSS_VF_FRAME, pipe_id);
}

int atomisp_css_capture_get_output_raw_frame_info(
					struct atomisp_sub_device *asd,
					struct atomisp_css_frame_info *info)
{
	if (ATOMISP_USE_YUVPP(asd))
		return 0;

	return __get_frame_info(asd, ATOMISP_INPUT_STREAM_GENERAL, info,
			ATOMISP_CSS_RAW_FRAME, IA_CSS_PIPE_ID_CAPTURE);
}

int atomisp_css_copy_get_output_frame_info(
					struct atomisp_sub_device *asd,
					unsigned int stream_index,
					struct atomisp_css_frame_info *info)
{
	return __get_frame_info(asd, stream_index, info,
			ATOMISP_CSS_OUTPUT_FRAME, IA_CSS_PIPE_ID_COPY);
}

int atomisp_css_preview_get_output_frame_info(
					struct atomisp_sub_device *asd,
					struct atomisp_css_frame_info *info)
{
	enum ia_css_pipe_id pipe_id;
	enum frame_info_type frame_type = ATOMISP_CSS_OUTPUT_FRAME;

	if (ATOMISP_USE_YUVPP(asd)) {
		pipe_id = IA_CSS_PIPE_ID_YUVPP;
		if (asd->continuous_mode->val)
			frame_type = ATOMISP_CSS_SECOND_OUTPUT_FRAME;
	} else {
		pipe_id = IA_CSS_PIPE_ID_PREVIEW;
	}

	return __get_frame_info(asd, ATOMISP_INPUT_STREAM_GENERAL, info,
					frame_type, pipe_id);
}

int atomisp_css_capture_get_output_frame_info(
					struct atomisp_sub_device *asd,
					struct atomisp_css_frame_info *info)
{
	enum ia_css_pipe_id pipe_id;

	if (ATOMISP_USE_YUVPP(asd))
		pipe_id = IA_CSS_PIPE_ID_YUVPP;
	else
		pipe_id = IA_CSS_PIPE_ID_CAPTURE;

	return __get_frame_info(asd, ATOMISP_INPUT_STREAM_GENERAL, info,
					ATOMISP_CSS_OUTPUT_FRAME, pipe_id);
}

int atomisp_css_video_get_output_frame_info(
					struct atomisp_sub_device *asd,
					struct atomisp_css_frame_info *info)
{
	enum ia_css_pipe_id pipe_id;
	enum frame_info_type frame_type = ATOMISP_CSS_OUTPUT_FRAME;

	if (ATOMISP_USE_YUVPP(asd)) {
		pipe_id = IA_CSS_PIPE_ID_YUVPP;
		if (asd->continuous_mode->val)
			frame_type = ATOMISP_CSS_SECOND_OUTPUT_FRAME;
	} else {
		pipe_id = IA_CSS_PIPE_ID_VIDEO;
	}

	return __get_frame_info(asd, ATOMISP_INPUT_STREAM_GENERAL, info,
					frame_type, pipe_id);
}

int atomisp_css_preview_configure_pp_input(
				struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height)
{
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];
	__configure_preview_pp_input(asd, width, height,
		ATOMISP_USE_YUVPP(asd) ?
		IA_CSS_PIPE_ID_YUVPP : IA_CSS_PIPE_ID_PREVIEW);

	if (width > stream_env->pipe_configs[IA_CSS_PIPE_ID_CAPTURE].
					capt_pp_in_res.width)
		__configure_capture_pp_input(asd, width, height,
			ATOMISP_USE_YUVPP(asd) ?
		IA_CSS_PIPE_ID_YUVPP : IA_CSS_PIPE_ID_CAPTURE);
	return 0;
}

int atomisp_css_capture_configure_pp_input(
				struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height)
{
	__configure_capture_pp_input(asd, width, height,
		ATOMISP_USE_YUVPP(asd) ?
		IA_CSS_PIPE_ID_YUVPP : IA_CSS_PIPE_ID_CAPTURE);
	return 0;
}

int atomisp_css_video_configure_pp_input(
				struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height)
{
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];

	__configure_video_pp_input(asd, width, height,
		ATOMISP_USE_YUVPP(asd) ?
		IA_CSS_PIPE_ID_YUVPP : IA_CSS_PIPE_ID_VIDEO);

	if (width > stream_env->pipe_configs[IA_CSS_PIPE_ID_CAPTURE].
					capt_pp_in_res.width)
		__configure_capture_pp_input(asd, width, height,
			ATOMISP_USE_YUVPP(asd) ?
			IA_CSS_PIPE_ID_YUVPP : IA_CSS_PIPE_ID_CAPTURE);
	return 0;
}

int atomisp_css_offline_capture_configure(struct atomisp_sub_device *asd,
			int num_captures, unsigned int skip, int offset)
{
	enum ia_css_err ret;

#ifdef ISP2401
	dev_dbg(asd->isp->dev, "%s num_capture:%d skip:%d offset:%d\n",
			__func__, num_captures, skip, offset);
#endif
	ret = ia_css_stream_capture(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		num_captures, skip, offset);
	if (ret != IA_CSS_SUCCESS)
		return -EINVAL;

	return 0;
}

int atomisp_css_exp_id_capture(struct atomisp_sub_device *asd, int exp_id)
{
	enum ia_css_err ret;

	ret = ia_css_stream_capture_frame(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		exp_id);
	if (ret == IA_CSS_ERR_QUEUE_IS_FULL) {
		/* capture cmd queue is full */
		return -EBUSY;
	} else if (ret != IA_CSS_SUCCESS) {
		return -EIO;
	}

	return 0;
}

int atomisp_css_exp_id_unlock(struct atomisp_sub_device *asd, int exp_id)
{
	enum ia_css_err ret;

	ret = ia_css_unlock_raw_frame(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		exp_id);
	if (ret == IA_CSS_ERR_QUEUE_IS_FULL)
		return -EAGAIN;
	else if (ret != IA_CSS_SUCCESS)
		return -EIO;

	return 0;
}

int atomisp_css_capture_enable_xnr(struct atomisp_sub_device *asd,
				   bool enable)
{
	asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.pipe_configs[IA_CSS_PIPE_ID_CAPTURE]
		.default_capture_config.enable_xnr = enable;
	asd->params.capture_config.enable_xnr = enable;
	asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.update_pipe[IA_CSS_PIPE_ID_CAPTURE] = true;

	return 0;
}

void atomisp_css_send_input_frame(struct atomisp_sub_device *asd,
				  unsigned short *data, unsigned int width,
				  unsigned int height)
{
	ia_css_stream_send_input_frame(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		data, width, height);
}

bool atomisp_css_isp_has_started(void)
{
	return ia_css_isp_has_started();
}

void atomisp_css_request_flash(struct atomisp_sub_device *asd)
{
	ia_css_stream_request_flash(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream);
}

void atomisp_css_set_wb_config(struct atomisp_sub_device *asd,
			struct atomisp_css_wb_config *wb_config)
{
	asd->params.config.wb_config = wb_config;
}

void atomisp_css_set_ob_config(struct atomisp_sub_device *asd,
			struct atomisp_css_ob_config *ob_config)
{
	asd->params.config.ob_config = ob_config;
}

void atomisp_css_set_dp_config(struct atomisp_sub_device *asd,
			struct atomisp_css_dp_config *dp_config)
{
	asd->params.config.dp_config = dp_config;
}

void atomisp_css_set_de_config(struct atomisp_sub_device *asd,
			struct atomisp_css_de_config *de_config)
{
	asd->params.config.de_config = de_config;
}

void atomisp_css_set_dz_config(struct atomisp_sub_device *asd,
			struct atomisp_css_dz_config *dz_config)
{
	asd->params.config.dz_config = dz_config;
}

void atomisp_css_set_default_de_config(struct atomisp_sub_device *asd)
{
	asd->params.config.de_config = NULL;
}

void atomisp_css_set_ce_config(struct atomisp_sub_device *asd,
			struct atomisp_css_ce_config *ce_config)
{
	asd->params.config.ce_config = ce_config;
}

void atomisp_css_set_nr_config(struct atomisp_sub_device *asd,
			struct atomisp_css_nr_config *nr_config)
{
	asd->params.config.nr_config = nr_config;
}

void atomisp_css_set_ee_config(struct atomisp_sub_device *asd,
			struct atomisp_css_ee_config *ee_config)
{
	asd->params.config.ee_config = ee_config;
}

void atomisp_css_set_tnr_config(struct atomisp_sub_device *asd,
			struct atomisp_css_tnr_config *tnr_config)
{
	asd->params.config.tnr_config = tnr_config;
}

void atomisp_css_set_cc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_cc_config *cc_config)
{
	asd->params.config.cc_config = cc_config;
}

void atomisp_css_set_macc_table(struct atomisp_sub_device *asd,
			struct atomisp_css_macc_table *macc_table)
{
	asd->params.config.macc_table = macc_table;
}

void atomisp_css_set_macc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_macc_config *macc_config)
{
	asd->params.config.macc_config = macc_config;
}

void atomisp_css_set_ecd_config(struct atomisp_sub_device *asd,
			struct atomisp_css_ecd_config *ecd_config)
{
	asd->params.config.ecd_config = ecd_config;
}

void atomisp_css_set_ynr_config(struct atomisp_sub_device *asd,
			struct atomisp_css_ynr_config *ynr_config)
{
	asd->params.config.ynr_config = ynr_config;
}

void atomisp_css_set_fc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_fc_config *fc_config)
{
	asd->params.config.fc_config = fc_config;
}

void atomisp_css_set_ctc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_ctc_config *ctc_config)
{
	asd->params.config.ctc_config = ctc_config;
}

void atomisp_css_set_cnr_config(struct atomisp_sub_device *asd,
			struct atomisp_css_cnr_config *cnr_config)
{
	asd->params.config.cnr_config = cnr_config;
}

void atomisp_css_set_aa_config(struct atomisp_sub_device *asd,
			struct atomisp_css_aa_config *aa_config)
{
	asd->params.config.aa_config = aa_config;
}

void atomisp_css_set_baa_config(struct atomisp_sub_device *asd,
			struct atomisp_css_baa_config *baa_config)
{
	asd->params.config.baa_config = baa_config;
}

void atomisp_css_set_anr_config(struct atomisp_sub_device *asd,
			struct atomisp_css_anr_config *anr_config)
{
	asd->params.config.anr_config = anr_config;
}

void atomisp_css_set_xnr_config(struct atomisp_sub_device *asd,
			struct atomisp_css_xnr_config *xnr_config)
{
	asd->params.config.xnr_config = xnr_config;
}

void atomisp_css_set_yuv2rgb_cc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_cc_config *yuv2rgb_cc_config)
{
	asd->params.config.yuv2rgb_cc_config = yuv2rgb_cc_config;
}

void atomisp_css_set_rgb2yuv_cc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_cc_config *rgb2yuv_cc_config)
{
	asd->params.config.rgb2yuv_cc_config = rgb2yuv_cc_config;
}

void atomisp_css_set_xnr_table(struct atomisp_sub_device *asd,
			struct atomisp_css_xnr_table *xnr_table)
{
	asd->params.config.xnr_table = xnr_table;
}

void atomisp_css_set_r_gamma_table(struct atomisp_sub_device *asd,
			struct atomisp_css_rgb_gamma_table *r_gamma_table)
{
	asd->params.config.r_gamma_table = r_gamma_table;
}

void atomisp_css_set_g_gamma_table(struct atomisp_sub_device *asd,
			struct atomisp_css_rgb_gamma_table *g_gamma_table)
{
	asd->params.config.g_gamma_table = g_gamma_table;
}

void atomisp_css_set_b_gamma_table(struct atomisp_sub_device *asd,
			struct atomisp_css_rgb_gamma_table *b_gamma_table)
{
	asd->params.config.b_gamma_table = b_gamma_table;
}

void atomisp_css_set_gamma_table(struct atomisp_sub_device *asd,
			struct atomisp_css_gamma_table *gamma_table)
{
	asd->params.config.gamma_table = gamma_table;
}

void atomisp_css_set_ctc_table(struct atomisp_sub_device *asd,
			struct atomisp_css_ctc_table *ctc_table)
{
	int i;
	uint16_t *vamem_ptr = ctc_table->data.vamem_1;
	int data_size = IA_CSS_VAMEM_1_CTC_TABLE_SIZE;
	bool valid = false;

	/* workaround: if ctc_table is all 0, do not apply it */
	if (ctc_table->vamem_type == IA_CSS_VAMEM_TYPE_2) {
		vamem_ptr = ctc_table->data.vamem_2;
		data_size = IA_CSS_VAMEM_2_CTC_TABLE_SIZE;
	}

	for (i = 0; i < data_size; i++) {
		if (*(vamem_ptr + i)) {
			valid = true;
			break;
		}
	}

	if (valid)
		asd->params.config.ctc_table = ctc_table;
	else
		dev_warn(asd->isp->dev, "Bypass the invalid ctc_table.\n");
}

void atomisp_css_set_anr_thres(struct atomisp_sub_device *asd,
			struct atomisp_css_anr_thres *anr_thres)
{
	asd->params.config.anr_thres = anr_thres;
}

void atomisp_css_set_dvs_6axis(struct atomisp_sub_device *asd,
			struct atomisp_css_dvs_6axis *dvs_6axis)
{
	asd->params.config.dvs_6axis_config = dvs_6axis;
}

void atomisp_css_set_gc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_gc_config *gc_config)
{
	asd->params.config.gc_config = gc_config;
}

void atomisp_css_set_3a_config(struct atomisp_sub_device *asd,
			struct atomisp_css_3a_config *s3a_config)
{
	asd->params.config.s3a_config = s3a_config;
}

void atomisp_css_video_set_dis_vector(struct atomisp_sub_device *asd,
				struct atomisp_dis_vector *vector)
{
	if (!asd->params.config.motion_vector)
		asd->params.config.motion_vector = &asd->params.css_param.motion_vector;

	memset(asd->params.config.motion_vector,
			0, sizeof(struct ia_css_vector));
	asd->params.css_param.motion_vector.x = vector->x;
	asd->params.css_param.motion_vector.y = vector->y;
}

static int atomisp_compare_dvs_grid(struct atomisp_sub_device *asd,
				struct atomisp_dvs_grid_info *atomgrid)
{
	struct atomisp_css_dvs_grid_info *cur =
		atomisp_css_get_dvs_grid_info(&asd->params.curr_grid_info);

	if (!cur) {
		dev_err(asd->isp->dev, "dvs grid not available!\n");
		return -EINVAL;
	}

	if (sizeof(*cur) != sizeof(*atomgrid)) {
		dev_err(asd->isp->dev, "dvs grid mis-match!\n");
		return -EINVAL;
	}

	if (!cur->enable) {
		dev_err(asd->isp->dev, "dvs not enabled!\n");
		return -EINVAL;
	}

	return memcmp(atomgrid, cur, sizeof(*cur));
}

void  atomisp_css_set_dvs2_coefs(struct atomisp_sub_device *asd,
			       struct ia_css_dvs2_coefficients *coefs)
{
	asd->params.config.dvs2_coefs = coefs;
}

int atomisp_css_set_dis_coefs(struct atomisp_sub_device *asd,
			  struct atomisp_dis_coefficients *coefs)
{
	if (atomisp_compare_dvs_grid(asd, &coefs->grid_info) != 0)
		/* If the grid info in the argument differs from the current
		   grid info, we tell the caller to reset the grid size and
		   try again. */
		return -EAGAIN;

	if (coefs->hor_coefs.odd_real == NULL ||
	    coefs->hor_coefs.odd_imag == NULL ||
	    coefs->hor_coefs.even_real == NULL ||
	    coefs->hor_coefs.even_imag == NULL ||
	    coefs->ver_coefs.odd_real == NULL ||
	    coefs->ver_coefs.odd_imag == NULL ||
	    coefs->ver_coefs.even_real == NULL ||
	    coefs->ver_coefs.even_imag == NULL ||
	    asd->params.css_param.dvs2_coeff->hor_coefs.odd_real == NULL ||
	    asd->params.css_param.dvs2_coeff->hor_coefs.odd_imag == NULL ||
	    asd->params.css_param.dvs2_coeff->hor_coefs.even_real == NULL ||
	    asd->params.css_param.dvs2_coeff->hor_coefs.even_imag == NULL ||
	    asd->params.css_param.dvs2_coeff->ver_coefs.odd_real == NULL ||
	    asd->params.css_param.dvs2_coeff->ver_coefs.odd_imag == NULL ||
	    asd->params.css_param.dvs2_coeff->ver_coefs.even_real == NULL ||
	    asd->params.css_param.dvs2_coeff->ver_coefs.even_imag == NULL)
		return -EINVAL;

	if (copy_from_user(asd->params.css_param.dvs2_coeff->hor_coefs.odd_real,
	    coefs->hor_coefs.odd_real, asd->params.dvs_hor_coef_bytes))
		return -EFAULT;
	if (copy_from_user(asd->params.css_param.dvs2_coeff->hor_coefs.odd_imag,
	    coefs->hor_coefs.odd_imag, asd->params.dvs_hor_coef_bytes))
		return -EFAULT;
	if (copy_from_user(asd->params.css_param.dvs2_coeff->hor_coefs.even_real,
	    coefs->hor_coefs.even_real, asd->params.dvs_hor_coef_bytes))
		return -EFAULT;
	if (copy_from_user(asd->params.css_param.dvs2_coeff->hor_coefs.even_imag,
	    coefs->hor_coefs.even_imag, asd->params.dvs_hor_coef_bytes))
		return -EFAULT;

	if (copy_from_user(asd->params.css_param.dvs2_coeff->ver_coefs.odd_real,
	    coefs->ver_coefs.odd_real, asd->params.dvs_ver_coef_bytes))
		return -EFAULT;
	if (copy_from_user(asd->params.css_param.dvs2_coeff->ver_coefs.odd_imag,
	    coefs->ver_coefs.odd_imag, asd->params.dvs_ver_coef_bytes))
		return -EFAULT;
	if (copy_from_user(asd->params.css_param.dvs2_coeff->ver_coefs.even_real,
	    coefs->ver_coefs.even_real, asd->params.dvs_ver_coef_bytes))
		return -EFAULT;
	if (copy_from_user(asd->params.css_param.dvs2_coeff->ver_coefs.even_imag,
	    coefs->ver_coefs.even_imag, asd->params.dvs_ver_coef_bytes))
		return -EFAULT;

	asd->params.css_param.update_flag.dvs2_coefs =
		(struct atomisp_dvs2_coefficients *)
		asd->params.css_param.dvs2_coeff;
	/* FIXME! */
/*	asd->params.dis_proj_data_valid = false; */
	asd->params.css_update_params_needed = true;

	return 0;
}

void atomisp_css_set_zoom_factor(struct atomisp_sub_device *asd,
					unsigned int zoom)
{
	struct atomisp_device *isp = asd->isp;

	if (zoom == asd->params.css_param.dz_config.dx &&
		 zoom == asd->params.css_param.dz_config.dy) {
		dev_dbg(isp->dev, "same zoom scale. skipped.\n");
		return;
	}

	memset(&asd->params.css_param.dz_config, 0,
		sizeof(struct ia_css_dz_config));
	asd->params.css_param.dz_config.dx = zoom;
	asd->params.css_param.dz_config.dy = zoom;

	asd->params.css_param.update_flag.dz_config =
		(struct atomisp_dz_config *) &asd->params.css_param.dz_config;
	asd->params.css_update_params_needed = true;
}

void atomisp_css_set_formats_config(struct atomisp_sub_device *asd,
			struct atomisp_css_formats_config *formats_config)
{
	asd->params.config.formats_config = formats_config;
}

int atomisp_css_get_wb_config(struct atomisp_sub_device *asd,
			struct atomisp_wb_config *config)
{
	struct atomisp_css_wb_config wb_config;
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			__func__);
		return -EINVAL;
	}
	memset(&wb_config, 0, sizeof(struct atomisp_css_wb_config));
	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));
	isp_config.wb_config = &wb_config;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	memcpy(config, &wb_config, sizeof(*config));

	return 0;
}

int atomisp_css_get_ob_config(struct atomisp_sub_device *asd,
			struct atomisp_ob_config *config)
{
	struct atomisp_css_ob_config ob_config;
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			__func__);
		return -EINVAL;
	}
	memset(&ob_config, 0, sizeof(struct atomisp_css_ob_config));
	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));
	isp_config.ob_config = &ob_config;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	memcpy(config, &ob_config, sizeof(*config));

	return 0;
}

int atomisp_css_get_dp_config(struct atomisp_sub_device *asd,
			struct atomisp_dp_config *config)
{
	struct atomisp_css_dp_config dp_config;
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			__func__);
		return -EINVAL;
	}
	memset(&dp_config, 0, sizeof(struct atomisp_css_dp_config));
	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));
	isp_config.dp_config = &dp_config;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	memcpy(config, &dp_config, sizeof(*config));

	return 0;
}

int atomisp_css_get_de_config(struct atomisp_sub_device *asd,
			struct atomisp_de_config *config)
{
	struct atomisp_css_de_config de_config;
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			__func__);
		return -EINVAL;
	}
	memset(&de_config, 0, sizeof(struct atomisp_css_de_config));
	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));
	isp_config.de_config = &de_config;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	memcpy(config, &de_config, sizeof(*config));

	return 0;
}

int atomisp_css_get_nr_config(struct atomisp_sub_device *asd,
			struct atomisp_nr_config *config)
{
	struct atomisp_css_nr_config nr_config;
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			__func__);
		return -EINVAL;
	}
	memset(&nr_config, 0, sizeof(struct atomisp_css_nr_config));
	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));

	isp_config.nr_config = &nr_config;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	memcpy(config, &nr_config, sizeof(*config));

	return 0;
}

int atomisp_css_get_ee_config(struct atomisp_sub_device *asd,
			struct atomisp_ee_config *config)
{
	struct atomisp_css_ee_config ee_config;
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			 __func__);
		return -EINVAL;
	}
	memset(&ee_config, 0, sizeof(struct atomisp_css_ee_config));
	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));
	isp_config.ee_config = &ee_config;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	memcpy(config, &ee_config, sizeof(*config));

	return 0;
}

int atomisp_css_get_tnr_config(struct atomisp_sub_device *asd,
			struct atomisp_tnr_config *config)
{
	struct atomisp_css_tnr_config tnr_config;
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			__func__);
		return -EINVAL;
	}
	memset(&tnr_config, 0, sizeof(struct atomisp_css_tnr_config));
	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));
	isp_config.tnr_config = &tnr_config;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	memcpy(config, &tnr_config, sizeof(*config));

	return 0;
}

int atomisp_css_get_ctc_table(struct atomisp_sub_device *asd,
			struct atomisp_ctc_table *config)
{
	struct atomisp_css_ctc_table *tab;
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			__func__);
		return -EINVAL;
	}

	tab = vzalloc(sizeof(struct atomisp_css_ctc_table));
	if (!tab)
		return -ENOMEM;

	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));
	isp_config.ctc_table = tab;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	memcpy(config, tab, sizeof(*tab));
	vfree(tab);

	return 0;
}

int atomisp_css_get_gamma_table(struct atomisp_sub_device *asd,
			struct atomisp_gamma_table *config)
{
	struct atomisp_css_gamma_table *tab;
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			__func__);
		return -EINVAL;
	}

	tab = vzalloc(sizeof(struct atomisp_css_gamma_table));
	if (!tab)
		return -ENOMEM;

	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));
	isp_config.gamma_table = tab;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	memcpy(config, tab, sizeof(*tab));
	vfree(tab);

	return 0;
}

int atomisp_css_get_gc_config(struct atomisp_sub_device *asd,
			struct atomisp_gc_config *config)
{
	struct atomisp_css_gc_config gc_config;
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			__func__);
		return -EINVAL;
	}
	memset(&gc_config, 0, sizeof(struct atomisp_css_gc_config));
	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));
	isp_config.gc_config = &gc_config;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	/* Get gamma correction params from current setup */
	memcpy(config, &gc_config, sizeof(*config));

	return 0;
}

int atomisp_css_get_3a_config(struct atomisp_sub_device *asd,
			struct atomisp_3a_config *config)
{
	struct atomisp_css_3a_config s3a_config;
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			__func__);
		return -EINVAL;
	}
	memset(&s3a_config, 0, sizeof(struct atomisp_css_3a_config));
	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));
	isp_config.s3a_config = &s3a_config;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	/* Get white balance from current setup */
	memcpy(config, &s3a_config, sizeof(*config));

	return 0;
}

int atomisp_css_get_formats_config(struct atomisp_sub_device *asd,
			struct atomisp_formats_config *config)
{
	struct atomisp_css_formats_config formats_config;
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			__func__);
		return -EINVAL;
	}
	memset(&formats_config, 0, sizeof(formats_config));
	memset(&isp_config, 0, sizeof(isp_config));
	isp_config.formats_config = &formats_config;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	/* Get narrow gamma from current setup */
	memcpy(config, &formats_config, sizeof(*config));

	return 0;
}

int atomisp_css_get_zoom_factor(struct atomisp_sub_device *asd,
					unsigned int *zoom)
{
	struct ia_css_dz_config dz_config;  /**< Digital Zoom */
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev, "%s called after streamoff, skipping.\n",
			__func__);
		return -EINVAL;
	}
	memset(&dz_config, 0, sizeof(struct ia_css_dz_config));
	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));
	isp_config.dz_config = &dz_config;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
	*zoom = dz_config.dx;

	return 0;
}


/*
 * Function to set/get image stablization statistics
 */
int atomisp_css_get_dis_stat(struct atomisp_sub_device *asd,
			 struct atomisp_dis_statistics *stats)
{
	struct atomisp_device *isp = asd->isp;
	struct atomisp_dis_buf *dis_buf;
	unsigned long flags;

	if (asd->params.dvs_stat->hor_prod.odd_real == NULL ||
	    asd->params.dvs_stat->hor_prod.odd_imag == NULL ||
	    asd->params.dvs_stat->hor_prod.even_real == NULL ||
	    asd->params.dvs_stat->hor_prod.even_imag == NULL ||
	    asd->params.dvs_stat->ver_prod.odd_real == NULL ||
	    asd->params.dvs_stat->ver_prod.odd_imag == NULL ||
	    asd->params.dvs_stat->ver_prod.even_real == NULL ||
	    asd->params.dvs_stat->ver_prod.even_imag == NULL)
		return -EINVAL;

	/* isp needs to be streaming to get DIS statistics */
	spin_lock_irqsave(&isp->lock, flags);
	if (asd->streaming != ATOMISP_DEVICE_STREAMING_ENABLED) {
		spin_unlock_irqrestore(&isp->lock, flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&isp->lock, flags);

	if (atomisp_compare_dvs_grid(asd, &stats->dvs2_stat.grid_info) != 0)
		/* If the grid info in the argument differs from the current
		   grid info, we tell the caller to reset the grid size and
		   try again. */
		return -EAGAIN;

	spin_lock_irqsave(&asd->dis_stats_lock, flags);
	if (!asd->params.dis_proj_data_valid || list_empty(&asd->dis_stats)) {
		spin_unlock_irqrestore(&asd->dis_stats_lock, flags);
		dev_err(isp->dev, "dis statistics is not valid.\n");
		return -EAGAIN;
	}

	dis_buf = list_entry(asd->dis_stats.next,
			struct atomisp_dis_buf, list);
	list_del_init(&dis_buf->list);
	spin_unlock_irqrestore(&asd->dis_stats_lock, flags);

	if (dis_buf->dvs_map)
		ia_css_translate_dvs2_statistics(
			asd->params.dvs_stat, dis_buf->dvs_map);
	else
		ia_css_get_dvs2_statistics(asd->params.dvs_stat,
			dis_buf->dis_data);
	stats->exp_id = dis_buf->dis_data->exp_id;

	spin_lock_irqsave(&asd->dis_stats_lock, flags);
	list_add_tail(&dis_buf->list, &asd->dis_stats);
	spin_unlock_irqrestore(&asd->dis_stats_lock, flags);

	if (copy_to_user(stats->dvs2_stat.ver_prod.odd_real,
			 asd->params.dvs_stat->ver_prod.odd_real,
			 asd->params.dvs_ver_proj_bytes))
		return -EFAULT;
	if (copy_to_user(stats->dvs2_stat.ver_prod.odd_imag,
			 asd->params.dvs_stat->ver_prod.odd_imag,
			 asd->params.dvs_ver_proj_bytes))
		return -EFAULT;
	if (copy_to_user(stats->dvs2_stat.ver_prod.even_real,
			 asd->params.dvs_stat->ver_prod.even_real,
			 asd->params.dvs_ver_proj_bytes))
		return -EFAULT;
	if (copy_to_user(stats->dvs2_stat.ver_prod.even_imag,
			 asd->params.dvs_stat->ver_prod.even_imag,
			 asd->params.dvs_ver_proj_bytes))
		return -EFAULT;
	if (copy_to_user(stats->dvs2_stat.hor_prod.odd_real,
			 asd->params.dvs_stat->hor_prod.odd_real,
			 asd->params.dvs_hor_proj_bytes))
		return -EFAULT;
	if (copy_to_user(stats->dvs2_stat.hor_prod.odd_imag,
			 asd->params.dvs_stat->hor_prod.odd_imag,
			 asd->params.dvs_hor_proj_bytes))
		return -EFAULT;
	if (copy_to_user(stats->dvs2_stat.hor_prod.even_real,
			 asd->params.dvs_stat->hor_prod.even_real,
			 asd->params.dvs_hor_proj_bytes))
		return -EFAULT;
	if (copy_to_user(stats->dvs2_stat.hor_prod.even_imag,
			 asd->params.dvs_stat->hor_prod.even_imag,
			 asd->params.dvs_hor_proj_bytes))
		return -EFAULT;

	return 0;
}

struct atomisp_css_shading_table *atomisp_css_shading_table_alloc(
				unsigned int width, unsigned int height)
{
	return ia_css_shading_table_alloc(width, height);
}

void atomisp_css_set_shading_table(struct atomisp_sub_device *asd,
			struct atomisp_css_shading_table *table)
{
	asd->params.config.shading_table = table;
}

void atomisp_css_shading_table_free(struct atomisp_css_shading_table *table)
{
	ia_css_shading_table_free(table);
}

struct atomisp_css_morph_table *atomisp_css_morph_table_allocate(
				unsigned int width, unsigned int height)
{
	return ia_css_morph_table_allocate(width, height);
}

void atomisp_css_set_morph_table(struct atomisp_sub_device *asd,
					struct atomisp_css_morph_table *table)
{
	asd->params.config.morph_table = table;
}

void atomisp_css_get_morph_table(struct atomisp_sub_device *asd,
				struct atomisp_css_morph_table *table)
{
	struct ia_css_isp_config isp_config;
	struct atomisp_device *isp = asd->isp;

	if (!asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream) {
		dev_err(isp->dev,
			"%s called after streamoff, skipping.\n", __func__);
		return;
	}
	memset(table, 0, sizeof(struct atomisp_css_morph_table));
	memset(&isp_config, 0, sizeof(struct ia_css_isp_config));
	isp_config.morph_table = table;
	ia_css_stream_get_isp_config(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		&isp_config);
}

void atomisp_css_morph_table_free(struct atomisp_css_morph_table *table)
{
	ia_css_morph_table_free(table);
}

void atomisp_css_set_cont_prev_start_time(struct atomisp_device *isp,
					unsigned int overlap)
{
	/* CSS 2.0 doesn't support this API. */
	dev_dbg(isp->dev, "set cont prev start time is not supported.\n");
	return;
}

void atomisp_css_acc_done(struct atomisp_sub_device *asd)
{
	complete(&asd->acc.acc_done);
}

int atomisp_css_wait_acc_finish(struct atomisp_sub_device *asd)
{
	int ret = 0;
	struct atomisp_device *isp = asd->isp;

	/* Unlock the isp mutex taken in IOCTL handler before sleeping! */
	rt_mutex_unlock(&isp->mutex);
	if (wait_for_completion_interruptible_timeout(&asd->acc.acc_done,
					ATOMISP_ISP_TIMEOUT_DURATION) == 0) {
		dev_err(isp->dev, "<%s: completion timeout\n", __func__);
		atomisp_css_debug_dump_sp_sw_debug_info();
		atomisp_css_debug_dump_debug_info(__func__);
		ret = -EIO;
	}
	rt_mutex_lock(&isp->mutex);

	return ret;
}

/* Set the ACC binary arguments */
int atomisp_css_set_acc_parameters(struct atomisp_acc_fw *acc_fw)
{
	unsigned int mem;

	for (mem = 0; mem < ATOMISP_ACC_NR_MEMORY; mem++) {
		if (acc_fw->args[mem].length == 0)
			continue;

		ia_css_isp_param_set_css_mem_init(&acc_fw->fw->mem_initializers,
						IA_CSS_PARAM_CLASS_PARAM, mem,
						acc_fw->args[mem].css_ptr,
						acc_fw->args[mem].length);
	}

	return 0;
}

/* Load acc binary extension */
int atomisp_css_load_acc_extension(struct atomisp_sub_device *asd,
				   struct atomisp_css_fw_info *fw,
				   enum atomisp_css_pipe_id pipe_id,
				   unsigned int type)
{
	struct atomisp_css_fw_info **hd;

	fw->next = NULL;
	hd = &(asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
			.pipe_configs[pipe_id].acc_extension);
	while (*hd)
		hd = &(*hd)->next;
	*hd = fw;

	asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.update_pipe[pipe_id] = true;
	return 0;
}

/* Unload acc binary extension */
void atomisp_css_unload_acc_extension(struct atomisp_sub_device *asd,
					struct atomisp_css_fw_info *fw,
					enum atomisp_css_pipe_id pipe_id)
{
	struct atomisp_css_fw_info **hd;

	hd = &(asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
			.pipe_configs[pipe_id].acc_extension);
	while (*hd && *hd != fw)
		hd = &(*hd)->next;
	if (!*hd) {
		dev_err(asd->isp->dev, "did not find acc fw for removal\n");
		return;
	}
	*hd = fw->next;
	fw->next = NULL;

	asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
		.update_pipe[pipe_id] = true;
}

int atomisp_css_create_acc_pipe(struct atomisp_sub_device *asd)
{
	struct atomisp_device *isp = asd->isp;
	struct ia_css_pipe_config *pipe_config;
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];

	if (stream_env->acc_stream) {
		if (stream_env->acc_stream_state == CSS_STREAM_STARTED) {
			if (ia_css_stream_stop(stream_env->acc_stream)
				!= IA_CSS_SUCCESS) {
				dev_err(isp->dev, "stop acc_stream failed.\n");
				return -EBUSY;
			}
		}

		if (ia_css_stream_destroy(stream_env->acc_stream)
			!= IA_CSS_SUCCESS) {
			dev_err(isp->dev, "destroy acc_stream failed.\n");
			return -EBUSY;
		}
		stream_env->acc_stream = NULL;
	}

	pipe_config = &stream_env->pipe_configs[CSS_PIPE_ID_ACC];
	ia_css_pipe_config_defaults(pipe_config);
	asd->acc.acc_stages = kzalloc(MAX_ACC_STAGES *
				sizeof(void *), GFP_KERNEL);
	if (!asd->acc.acc_stages)
		return -ENOMEM;
	pipe_config->acc_stages = asd->acc.acc_stages;
	pipe_config->mode = IA_CSS_PIPE_MODE_ACC;
	pipe_config->num_acc_stages = 0;

	/*
	 * We delay the ACC pipeline creation to atomisp_css_start_acc_pipe,
	 * because pipe configuration will soon be changed by
	 * atomisp_css_load_acc_binary()
	 */
	return 0;
}

int atomisp_css_start_acc_pipe(struct atomisp_sub_device *asd)
{
	struct atomisp_device *isp = asd->isp;
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];
	struct ia_css_pipe_config *pipe_config =
			&stream_env->pipe_configs[IA_CSS_PIPE_ID_ACC];

	if (ia_css_pipe_create(pipe_config,
		&stream_env->pipes[IA_CSS_PIPE_ID_ACC]) != IA_CSS_SUCCESS) {
		dev_err(isp->dev, "%s: ia_css_pipe_create failed\n",
				__func__);
		return -EBADE;
	}

	memset(&stream_env->acc_stream_config, 0,
		sizeof(struct ia_css_stream_config));
	if (ia_css_stream_create(&stream_env->acc_stream_config, 1,
				&stream_env->pipes[IA_CSS_PIPE_ID_ACC],
				&stream_env->acc_stream) != IA_CSS_SUCCESS) {
		dev_err(isp->dev, "%s: create acc_stream error.\n", __func__);
		return -EINVAL;
	}
	stream_env->acc_stream_state = CSS_STREAM_CREATED;

	init_completion(&asd->acc.acc_done);
	asd->acc.pipeline = stream_env->pipes[IA_CSS_PIPE_ID_ACC];

	atomisp_freq_scaling(isp, ATOMISP_DFS_MODE_MAX, false);

	if (ia_css_start_sp() != IA_CSS_SUCCESS) {
		dev_err(isp->dev, "start sp error.\n");
		return -EIO;
	}

	if (ia_css_stream_start(stream_env->acc_stream)
		!= IA_CSS_SUCCESS) {
		dev_err(isp->dev, "acc_stream start error.\n");
		return -EIO;
	}

	stream_env->acc_stream_state = CSS_STREAM_STARTED;
	return 0;
}

int atomisp_css_stop_acc_pipe(struct atomisp_sub_device *asd)
{
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];
	if (stream_env->acc_stream_state == CSS_STREAM_STARTED) {
		ia_css_stream_stop(stream_env->acc_stream);
		stream_env->acc_stream_state = CSS_STREAM_STOPPED;
	}
	return 0;
}

void atomisp_css_destroy_acc_pipe(struct atomisp_sub_device *asd)
{
	struct atomisp_stream_env *stream_env =
		&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL];
	if (stream_env->acc_stream) {
		if (ia_css_stream_destroy(stream_env->acc_stream)
		    != IA_CSS_SUCCESS)
			dev_warn(asd->isp->dev,
				"destroy acc_stream failed.\n");
		stream_env->acc_stream = NULL;
	}

	if (stream_env->pipes[IA_CSS_PIPE_ID_ACC]) {
		if (ia_css_pipe_destroy(stream_env->pipes[IA_CSS_PIPE_ID_ACC])
			!= IA_CSS_SUCCESS)
			dev_warn(asd->isp->dev,
				"destroy ACC pipe failed.\n");
		stream_env->pipes[IA_CSS_PIPE_ID_ACC] = NULL;
		stream_env->update_pipe[IA_CSS_PIPE_ID_ACC] = false;
		ia_css_pipe_config_defaults(
			&stream_env->pipe_configs[IA_CSS_PIPE_ID_ACC]);
		ia_css_pipe_extra_config_defaults(
			&stream_env->pipe_extra_configs[IA_CSS_PIPE_ID_ACC]);
	}
	asd->acc.pipeline = NULL;

	/* css 2.0 API limitation: ia_css_stop_sp() could be only called after
	 * destroy all pipes
	 */
	ia_css_stop_sp();

	kfree(asd->acc.acc_stages);
	asd->acc.acc_stages = NULL;

	atomisp_freq_scaling(asd->isp, ATOMISP_DFS_MODE_LOW, false);
}

int atomisp_css_load_acc_binary(struct atomisp_sub_device *asd,
					struct atomisp_css_fw_info *fw,
					unsigned int index)
{
	struct ia_css_pipe_config *pipe_config =
			&asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL]
			.pipe_configs[IA_CSS_PIPE_ID_ACC];

	if (index >= MAX_ACC_STAGES) {
		dev_dbg(asd->isp->dev, "%s: index(%d) out of range\n",
				__func__, index);
		return -ENOMEM;
	}

	pipe_config->acc_stages[index] = fw;
	pipe_config->num_acc_stages = index + 1;
	pipe_config->acc_num_execs = 1;

	return 0;
}

static struct atomisp_sub_device *__get_atomisp_subdev(
					struct ia_css_pipe *css_pipe,
					struct atomisp_device *isp,
					enum atomisp_input_stream_id *stream_id) {
	int i, j, k;
	struct atomisp_sub_device *asd;
	struct atomisp_stream_env *stream_env;

	for (i = 0; i < isp->num_of_streams; i++) {
		asd = &isp->asd[i];
		if (asd->streaming == ATOMISP_DEVICE_STREAMING_DISABLED &&
		    !asd->acc.pipeline)
			continue;
		for (j = 0; j < ATOMISP_INPUT_STREAM_NUM; j++) {
			stream_env = &asd->stream_env[j];
			for (k = 0; k < IA_CSS_PIPE_ID_NUM; k++) {
				if (stream_env->pipes[k] &&
					stream_env->pipes[k] == css_pipe) {
						*stream_id = j;
						return asd;
					}
				}
		}
	}

	return NULL;
}

int atomisp_css_isr_thread(struct atomisp_device *isp,
			   bool *frame_done_found,
			   bool *css_pipe_done)
{
	enum atomisp_input_stream_id stream_id = 0;
	struct atomisp_css_event current_event;
	struct atomisp_sub_device *asd = &isp->asd[0];
#ifndef ISP2401
	bool reset_wdt_timer[MAX_STREAM_NUM] = {false};
#endif
	int i;

	while (!atomisp_css_dequeue_event(&current_event)) {
		if (current_event.event.type ==
			IA_CSS_EVENT_TYPE_FW_ASSERT) {
			/*
			 * Received FW assertion signal,
			 * trigger WDT to recover
			 */
			dev_err(isp->dev, "%s: ISP reports FW_ASSERT event! fw_assert_module_id %d fw_assert_line_no %d\n",
				__func__,
				current_event.event.fw_assert_module_id,
				current_event.event.fw_assert_line_no);
			for (i = 0; i < isp->num_of_streams; i++)
				atomisp_wdt_stop(&isp->asd[i], 0);
#ifndef ISP2401
			atomisp_wdt((unsigned long)isp);
#else
			queue_work(isp->wdt_work_queue, &isp->wdt_work);
#endif
			return -EINVAL;
		} else if (current_event.event.type == IA_CSS_EVENT_TYPE_FW_WARNING) {
			dev_warn(isp->dev, "%s: ISP reports warning, code is %d, exp_id %d\n",
				__func__, current_event.event.fw_warning,
				current_event.event.exp_id);
			continue;
		}

		asd = __get_atomisp_subdev(current_event.event.pipe,
					isp, &stream_id);
		if (!asd) {
			if (current_event.event.type == CSS_EVENT_TIMER)
				dev_dbg(isp->dev,
					"event: Timer event.");
			else
				dev_warn(isp->dev, "%s:no subdev.event:%d",
						__func__,
						current_event.event.type);
			continue;
		}

		atomisp_css_temp_pipe_to_pipe_id(asd, &current_event);
		switch (current_event.event.type) {
		case CSS_EVENT_OUTPUT_FRAME_DONE:
			frame_done_found[asd->index] = true;
			atomisp_buf_done(asd, 0, CSS_BUFFER_TYPE_OUTPUT_FRAME,
					 current_event.pipe, true, stream_id);
#ifndef ISP2401
			reset_wdt_timer[asd->index] = true; /* ISP running */
#endif
			break;
		case CSS_EVENT_SEC_OUTPUT_FRAME_DONE:
			frame_done_found[asd->index] = true;
			atomisp_buf_done(asd, 0, CSS_BUFFER_TYPE_SEC_OUTPUT_FRAME,
					 current_event.pipe, true, stream_id);
#ifndef ISP2401
			reset_wdt_timer[asd->index] = true; /* ISP running */
#endif
			break;
		case CSS_EVENT_3A_STATISTICS_DONE:
			atomisp_buf_done(asd, 0,
					 CSS_BUFFER_TYPE_3A_STATISTICS,
					 current_event.pipe,
					 false, stream_id);
			break;
		case CSS_EVENT_METADATA_DONE:
			atomisp_buf_done(asd, 0,
					 CSS_BUFFER_TYPE_METADATA,
					 current_event.pipe,
					 false, stream_id);
			break;
		case CSS_EVENT_VF_OUTPUT_FRAME_DONE:
			atomisp_buf_done(asd, 0,
					 CSS_BUFFER_TYPE_VF_OUTPUT_FRAME,
					 current_event.pipe, true, stream_id);
#ifndef ISP2401
			reset_wdt_timer[asd->index] = true; /* ISP running */
#endif
			break;
		case CSS_EVENT_SEC_VF_OUTPUT_FRAME_DONE:
			atomisp_buf_done(asd, 0,
					 CSS_BUFFER_TYPE_SEC_VF_OUTPUT_FRAME,
					 current_event.pipe, true, stream_id);
#ifndef ISP2401
			reset_wdt_timer[asd->index] = true; /* ISP running */
#endif
			break;
		case CSS_EVENT_DIS_STATISTICS_DONE:
			atomisp_buf_done(asd, 0,
					 CSS_BUFFER_TYPE_DIS_STATISTICS,
					 current_event.pipe,
					 false, stream_id);
			break;
		case CSS_EVENT_PIPELINE_DONE:
			css_pipe_done[asd->index] = true;
			break;
		case CSS_EVENT_ACC_STAGE_COMPLETE:
			atomisp_acc_done(asd, current_event.event.fw_handle);
			break;
		default:
			dev_dbg(isp->dev, "unhandled css stored event: 0x%x\n",
					current_event.event.type);
			break;
		}
	}
#ifndef ISP2401
	/* If there are no buffers queued then
	 * delete wdt timer. */
	for (i = 0; i < isp->num_of_streams; i++) {
		asd = &isp->asd[i];
		if (!asd)
			continue;
		if (asd->streaming != ATOMISP_DEVICE_STREAMING_ENABLED)
			continue;
		if (!atomisp_buffers_queued(asd))
			atomisp_wdt_stop(asd, false);
		else if (reset_wdt_timer[i])
		/* SOF irq should not reset wdt timer. */
			atomisp_wdt_refresh(asd,
					ATOMISP_WDT_KEEP_CURRENT_DELAY);
	}
#endif

	return 0;
}

bool atomisp_css_valid_sof(struct atomisp_device *isp)
{
	unsigned int i, j;

	/* Loop for each css stream */
	for (i = 0; i < isp->num_of_streams; i++) {
		struct atomisp_sub_device *asd = &isp->asd[i];
		/* Loop for each css vc stream */
		for (j = 0; j < ATOMISP_INPUT_STREAM_NUM; j++) {
			if (asd->stream_env[j].stream &&
				asd->stream_env[j].stream_config.mode ==
				IA_CSS_INPUT_MODE_BUFFERED_SENSOR)
				return false;
		}
	}

	return true;
}

int atomisp_css_debug_dump_isp_binary(void)
{
	ia_css_debug_dump_isp_binary();
	return 0;
}

int atomisp_css_dump_sp_raw_copy_linecount(bool reduced)
{
	sh_css_dump_sp_raw_copy_linecount(reduced);
	return 0;
}

int atomisp_css_dump_blob_infor(void)
{
	struct ia_css_blob_descr *bd = sh_css_blob_info;
	unsigned i, nm = sh_css_num_binaries;

	if (nm == 0)
		return -EPERM;
	if (bd == NULL)
		return -EPERM;

	for (i = 1; i < sh_css_num_binaries; i++)
		dev_dbg(atomisp_dev, "Num%d binary id is %d, name is %s\n", i,
			bd[i-1].header.info.isp.sp.id, bd[i-1].name);

	return 0;
}

void atomisp_css_set_isp_config_id(struct atomisp_sub_device *asd,
			uint32_t isp_config_id)
{
	asd->params.config.isp_config_id = isp_config_id;
}

void atomisp_css_set_isp_config_applied_frame(struct atomisp_sub_device *asd,
			struct atomisp_css_frame *output_frame)
{
	asd->params.config.output_frame = output_frame;
}

int atomisp_get_css_dbgfunc(void)
{
	return dbg_func;
}

int atomisp_set_css_dbgfunc(struct atomisp_device *isp, int opt)
{
	int ret;

	ret = __set_css_print_env(isp, opt);
	if (0 == ret)
		dbg_func = opt;

	return ret;
}
void atomisp_en_dz_capt_pipe(struct atomisp_sub_device *asd, bool enable)
{
	ia_css_en_dz_capt_pipe(
		asd->stream_env[ATOMISP_INPUT_STREAM_GENERAL].stream,
		enable);
}

struct atomisp_css_dvs_grid_info *atomisp_css_get_dvs_grid_info(
	struct atomisp_css_grid_info *grid_info)
{
	if (!grid_info)
		return NULL;

#ifdef IA_CSS_DVS_STAT_GRID_INFO_SUPPORTED
	return &grid_info->dvs_grid.dvs_grid_info;
#else
	return &grid_info->dvs_grid;
#endif
}
