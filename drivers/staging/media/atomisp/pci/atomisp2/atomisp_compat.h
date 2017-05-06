/*
 * Support for Clovertrail PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
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

#ifndef __ATOMISP_COMPAT_H__
#define __ATOMISP_COMPAT_H__

#include "atomisp_compat_css20.h"

#include "../../include/linux/atomisp.h"
#include <media/videobuf-vmalloc.h>

#define CSS_RX_IRQ_INFO_BUFFER_OVERRUN \
	CSS_ID(CSS_RX_IRQ_INFO_BUFFER_OVERRUN)
#define CSS_RX_IRQ_INFO_ENTER_SLEEP_MODE \
	CSS_ID(CSS_RX_IRQ_INFO_ENTER_SLEEP_MODE)
#define CSS_RX_IRQ_INFO_EXIT_SLEEP_MODE \
	CSS_ID(CSS_RX_IRQ_INFO_EXIT_SLEEP_MODE)
#define CSS_RX_IRQ_INFO_ECC_CORRECTED \
	CSS_ID(CSS_RX_IRQ_INFO_ECC_CORRECTED)
#define CSS_RX_IRQ_INFO_ERR_SOT \
	CSS_ID(CSS_RX_IRQ_INFO_ERR_SOT)
#define CSS_RX_IRQ_INFO_ERR_SOT_SYNC \
	CSS_ID(CSS_RX_IRQ_INFO_ERR_SOT_SYNC)
#define CSS_RX_IRQ_INFO_ERR_CONTROL \
	CSS_ID(CSS_RX_IRQ_INFO_ERR_CONTROL)
#define CSS_RX_IRQ_INFO_ERR_ECC_DOUBLE \
	CSS_ID(CSS_RX_IRQ_INFO_ERR_ECC_DOUBLE)
#define CSS_RX_IRQ_INFO_ERR_CRC \
	CSS_ID(CSS_RX_IRQ_INFO_ERR_CRC)
#define CSS_RX_IRQ_INFO_ERR_UNKNOWN_ID \
	CSS_ID(CSS_RX_IRQ_INFO_ERR_UNKNOWN_ID)
#define CSS_RX_IRQ_INFO_ERR_FRAME_SYNC \
	CSS_ID(CSS_RX_IRQ_INFO_ERR_FRAME_SYNC)
#define CSS_RX_IRQ_INFO_ERR_FRAME_DATA \
	CSS_ID(CSS_RX_IRQ_INFO_ERR_FRAME_DATA)
#define CSS_RX_IRQ_INFO_ERR_DATA_TIMEOUT \
	CSS_ID(CSS_RX_IRQ_INFO_ERR_DATA_TIMEOUT)
#define CSS_RX_IRQ_INFO_ERR_UNKNOWN_ESC \
	CSS_ID(CSS_RX_IRQ_INFO_ERR_UNKNOWN_ESC)
#define CSS_RX_IRQ_INFO_ERR_LINE_SYNC \
	CSS_ID(CSS_RX_IRQ_INFO_ERR_LINE_SYNC)
#define CSS_RX_IRQ_INFO_INIT_TIMEOUT \
	CSS_ID(CSS_RX_IRQ_INFO_INIT_TIMEOUT)

#define CSS_IRQ_INFO_CSS_RECEIVER_SOF	CSS_ID(CSS_IRQ_INFO_CSS_RECEIVER_SOF)
#define CSS_IRQ_INFO_CSS_RECEIVER_EOF	CSS_ID(CSS_IRQ_INFO_CSS_RECEIVER_EOF)
#define CSS_IRQ_INFO_CSS_RECEIVER_FIFO_OVERFLOW \
	CSS_ID(CSS_IRQ_INFO_CSS_RECEIVER_FIFO_OVERFLOW)
#define CSS_EVENT_OUTPUT_FRAME_DONE	CSS_EVENT(OUTPUT_FRAME_DONE)
#define CSS_EVENT_SEC_OUTPUT_FRAME_DONE	CSS_EVENT(SECOND_OUTPUT_FRAME_DONE)
#define CSS_EVENT_VF_OUTPUT_FRAME_DONE	CSS_EVENT(VF_OUTPUT_FRAME_DONE)
#define CSS_EVENT_SEC_VF_OUTPUT_FRAME_DONE	CSS_EVENT(SECOND_VF_OUTPUT_FRAME_DONE)
#define CSS_EVENT_3A_STATISTICS_DONE	CSS_EVENT(3A_STATISTICS_DONE)
#define CSS_EVENT_DIS_STATISTICS_DONE	CSS_EVENT(DIS_STATISTICS_DONE)
#define CSS_EVENT_PIPELINE_DONE		CSS_EVENT(PIPELINE_DONE)
#define CSS_EVENT_METADATA_DONE		CSS_EVENT(METADATA_DONE)
#define CSS_EVENT_ACC_STAGE_COMPLETE	CSS_EVENT(ACC_STAGE_COMPLETE)
#define CSS_EVENT_TIMER			CSS_EVENT(TIMER)

#define CSS_BUFFER_TYPE_METADATA	CSS_ID(CSS_BUFFER_TYPE_METADATA)
#define CSS_BUFFER_TYPE_3A_STATISTICS	CSS_ID(CSS_BUFFER_TYPE_3A_STATISTICS)
#define CSS_BUFFER_TYPE_DIS_STATISTICS	CSS_ID(CSS_BUFFER_TYPE_DIS_STATISTICS)
#define CSS_BUFFER_TYPE_INPUT_FRAME	CSS_ID(CSS_BUFFER_TYPE_INPUT_FRAME)
#define CSS_BUFFER_TYPE_OUTPUT_FRAME	CSS_ID(CSS_BUFFER_TYPE_OUTPUT_FRAME)
#define CSS_BUFFER_TYPE_SEC_OUTPUT_FRAME	CSS_ID(CSS_BUFFER_TYPE_SEC_OUTPUT_FRAME)
#define CSS_BUFFER_TYPE_VF_OUTPUT_FRAME	CSS_ID(CSS_BUFFER_TYPE_VF_OUTPUT_FRAME)
#define CSS_BUFFER_TYPE_SEC_VF_OUTPUT_FRAME	CSS_ID(CSS_BUFFER_TYPE_SEC_VF_OUTPUT_FRAME)
#define CSS_BUFFER_TYPE_RAW_OUTPUT_FRAME \
	CSS_ID(CSS_BUFFER_TYPE_RAW_OUTPUT_FRAME)

#define CSS_FORMAT_RAW_8	CSS_FORMAT(RAW_8)
#define CSS_FORMAT_RAW_10	CSS_FORMAT(RAW_10)
#define CSS_FORMAT_RAW_12	CSS_FORMAT(RAW_12)
#define CSS_FORMAT_RAW_16	CSS_FORMAT(RAW_16)

#define CSS_CAPTURE_MODE_RAW		CSS_ID(CSS_CAPTURE_MODE_RAW)
#define CSS_CAPTURE_MODE_BAYER		CSS_ID(CSS_CAPTURE_MODE_BAYER)
#define CSS_CAPTURE_MODE_PRIMARY	CSS_ID(CSS_CAPTURE_MODE_PRIMARY)
#define CSS_CAPTURE_MODE_ADVANCED	CSS_ID(CSS_CAPTURE_MODE_ADVANCED)
#define CSS_CAPTURE_MODE_LOW_LIGHT	CSS_ID(CSS_CAPTURE_MODE_LOW_LIGHT)

#define CSS_MORPH_TABLE_NUM_PLANES	CSS_ID(CSS_MORPH_TABLE_NUM_PLANES)

#define CSS_FRAME_FORMAT_NV11		CSS_ID(CSS_FRAME_FORMAT_NV11)
#define CSS_FRAME_FORMAT_NV12		CSS_ID(CSS_FRAME_FORMAT_NV12)
#define CSS_FRAME_FORMAT_NV16		CSS_ID(CSS_FRAME_FORMAT_NV16)
#define CSS_FRAME_FORMAT_NV21		CSS_ID(CSS_FRAME_FORMAT_NV21)
#define CSS_FRAME_FORMAT_NV61		CSS_ID(CSS_FRAME_FORMAT_NV61)
#define CSS_FRAME_FORMAT_YV12		CSS_ID(CSS_FRAME_FORMAT_YV12)
#define CSS_FRAME_FORMAT_YV16		CSS_ID(CSS_FRAME_FORMAT_YV16)
#define CSS_FRAME_FORMAT_YUV420		CSS_ID(CSS_FRAME_FORMAT_YUV420)
#define CSS_FRAME_FORMAT_YUV420_16	CSS_ID(CSS_FRAME_FORMAT_YUV420_16)
#define CSS_FRAME_FORMAT_YUV422		CSS_ID(CSS_FRAME_FORMAT_YUV422)
#define CSS_FRAME_FORMAT_YUV422_16	CSS_ID(CSS_FRAME_FORMAT_YUV422_16)
#define CSS_FRAME_FORMAT_UYVY		CSS_ID(CSS_FRAME_FORMAT_UYVY)
#define CSS_FRAME_FORMAT_YUYV		CSS_ID(CSS_FRAME_FORMAT_YUYV)
#define CSS_FRAME_FORMAT_YUV444		CSS_ID(CSS_FRAME_FORMAT_YUV444)
#define CSS_FRAME_FORMAT_YUV_LINE	CSS_ID(CSS_FRAME_FORMAT_YUV_LINE)
#define CSS_FRAME_FORMAT_RAW		CSS_ID(CSS_FRAME_FORMAT_RAW)
#define CSS_FRAME_FORMAT_RGB565		CSS_ID(CSS_FRAME_FORMAT_RGB565)
#define CSS_FRAME_FORMAT_PLANAR_RGB888	CSS_ID(CSS_FRAME_FORMAT_PLANAR_RGB888)
#define CSS_FRAME_FORMAT_RGBA888	CSS_ID(CSS_FRAME_FORMAT_RGBA888)
#define CSS_FRAME_FORMAT_QPLANE6	CSS_ID(CSS_FRAME_FORMAT_QPLANE6)
#define CSS_FRAME_FORMAT_BINARY_8	CSS_ID(CSS_FRAME_FORMAT_BINARY_8)

struct atomisp_device;
struct atomisp_sub_device;
struct video_device;
enum atomisp_input_stream_id;

struct atomisp_metadata_buf {
	struct ia_css_metadata *metadata;
	void *md_vptr;
	struct list_head list;
};

void atomisp_css_debug_dump_sp_sw_debug_info(void);
void atomisp_css_debug_dump_debug_info(const char *context);
void atomisp_css_debug_set_dtrace_level(const unsigned int trace_level);

void atomisp_store_uint32(hrt_address addr, uint32_t data);
void atomisp_load_uint32(hrt_address addr, uint32_t *data);

int atomisp_css_init(struct atomisp_device *isp);

void atomisp_css_uninit(struct atomisp_device *isp);

void atomisp_css_suspend(struct atomisp_device *isp);

int atomisp_css_resume(struct atomisp_device *isp);

void atomisp_css_init_struct(struct atomisp_sub_device *asd);

int atomisp_css_irq_translate(struct atomisp_device *isp,
			      unsigned int *infos);

void atomisp_css_rx_get_irq_info(enum ia_css_csi2_port port,
					unsigned int *infos);

void atomisp_css_rx_clear_irq_info(enum ia_css_csi2_port port,
					unsigned int infos);

int atomisp_css_irq_enable(struct atomisp_device *isp,
			   enum atomisp_css_irq_info info, bool enable);

int atomisp_q_video_buffer_to_css(struct atomisp_sub_device *asd,
			struct videobuf_vmalloc_memory *vm_mem,
			enum atomisp_input_stream_id stream_id,
			enum atomisp_css_buffer_type css_buf_type,
			enum atomisp_css_pipe_id css_pipe_id);

int atomisp_q_s3a_buffer_to_css(struct atomisp_sub_device *asd,
			struct atomisp_s3a_buf *s3a_buf,
			enum atomisp_input_stream_id stream_id,
			enum atomisp_css_pipe_id css_pipe_id);

int atomisp_q_metadata_buffer_to_css(struct atomisp_sub_device *asd,
			struct atomisp_metadata_buf *metadata_buf,
			enum atomisp_input_stream_id stream_id,
			enum atomisp_css_pipe_id css_pipe_id);

int atomisp_q_dis_buffer_to_css(struct atomisp_sub_device *asd,
			struct atomisp_dis_buf *dis_buf,
			enum atomisp_input_stream_id stream_id,
			enum atomisp_css_pipe_id css_pipe_id);

void atomisp_css_mmu_invalidate_cache(void);

void atomisp_css_mmu_invalidate_tlb(void);

void atomisp_css_mmu_set_page_table_base_index(unsigned long base_index);

int atomisp_css_start(struct atomisp_sub_device *asd,
		      enum atomisp_css_pipe_id pipe_id, bool in_reset);

void atomisp_css_update_isp_params(struct atomisp_sub_device *asd);
void atomisp_css_update_isp_params_on_pipe(struct atomisp_sub_device *asd,
					struct ia_css_pipe *pipe);

int atomisp_css_queue_buffer(struct atomisp_sub_device *asd,
			     enum atomisp_input_stream_id stream_id,
			     enum atomisp_css_pipe_id pipe_id,
			     enum atomisp_css_buffer_type buf_type,
			     struct atomisp_css_buffer *isp_css_buffer);

int atomisp_css_dequeue_buffer(struct atomisp_sub_device *asd,
				enum atomisp_input_stream_id stream_id,
				enum atomisp_css_pipe_id pipe_id,
				enum atomisp_css_buffer_type buf_type,
				struct atomisp_css_buffer *isp_css_buffer);

int atomisp_css_allocate_stat_buffers(struct atomisp_sub_device *asd,
				      uint16_t stream_id,
				      struct atomisp_s3a_buf *s3a_buf,
				      struct atomisp_dis_buf *dis_buf,
				      struct atomisp_metadata_buf *md_buf);

void atomisp_css_free_stat_buffers(struct atomisp_sub_device *asd);

void atomisp_css_free_3a_buffer(struct atomisp_s3a_buf *s3a_buf);

void atomisp_css_free_dis_buffer(struct atomisp_dis_buf *dis_buf);

void atomisp_css_free_metadata_buffer(struct atomisp_metadata_buf *metadata_buf);

int atomisp_css_get_grid_info(struct atomisp_sub_device *asd,
				enum atomisp_css_pipe_id pipe_id,
				int source_pad);

int atomisp_alloc_3a_output_buf(struct atomisp_sub_device *asd);

int atomisp_alloc_dis_coef_buf(struct atomisp_sub_device *asd);

int atomisp_alloc_metadata_output_buf(struct atomisp_sub_device *asd);

void atomisp_free_metadata_output_buf(struct atomisp_sub_device *asd);

void atomisp_css_get_dis_statistics(struct atomisp_sub_device *asd,
				    struct atomisp_css_buffer *isp_css_buffer,
				    struct ia_css_isp_dvs_statistics_map *dvs_map);

int atomisp_css_dequeue_event(struct atomisp_css_event *current_event);

void atomisp_css_temp_pipe_to_pipe_id(struct atomisp_sub_device *asd,
				      struct atomisp_css_event *current_event);

int atomisp_css_isys_set_resolution(struct atomisp_sub_device *asd,
				    enum atomisp_input_stream_id stream_id,
				    struct v4l2_mbus_framefmt *ffmt,
				    int isys_stream);

void atomisp_css_isys_set_link(struct atomisp_sub_device *asd,
			       enum atomisp_input_stream_id stream_id,
			       int link,
			       int isys_stream);

void atomisp_css_isys_set_valid(struct atomisp_sub_device *asd,
				enum atomisp_input_stream_id stream_id,
				bool valid,
				int isys_stream);

void atomisp_css_isys_set_format(struct atomisp_sub_device *asd,
				 enum atomisp_input_stream_id stream_id,
				 enum atomisp_css_stream_format format,
				 int isys_stream);

int atomisp_css_set_default_isys_config(struct atomisp_sub_device *asd,
					enum atomisp_input_stream_id stream_id,
					struct v4l2_mbus_framefmt *ffmt);

int atomisp_css_isys_two_stream_cfg(struct atomisp_sub_device *asd,
				    enum atomisp_input_stream_id stream_id,
				    enum atomisp_css_stream_format input_format);

void atomisp_css_isys_two_stream_cfg_update_stream1(
				    struct atomisp_sub_device *asd,
				    enum atomisp_input_stream_id stream_id,
				    enum atomisp_css_stream_format input_format,
				    unsigned int width, unsigned int height);

void atomisp_css_isys_two_stream_cfg_update_stream2(
				    struct atomisp_sub_device *asd,
				    enum atomisp_input_stream_id stream_id,
				    enum atomisp_css_stream_format input_format,
				    unsigned int width, unsigned int height);

int atomisp_css_input_set_resolution(struct atomisp_sub_device *asd,
					enum atomisp_input_stream_id stream_id,
					struct v4l2_mbus_framefmt *ffmt);

void atomisp_css_input_set_binning_factor(struct atomisp_sub_device *asd,
					enum atomisp_input_stream_id stream_id,
					unsigned int bin_factor);

void atomisp_css_input_set_bayer_order(struct atomisp_sub_device *asd,
				enum atomisp_input_stream_id stream_id,
				enum atomisp_css_bayer_order bayer_order);

void atomisp_css_input_set_format(struct atomisp_sub_device *asd,
				enum atomisp_input_stream_id stream_id,
				enum atomisp_css_stream_format format);

int atomisp_css_input_set_effective_resolution(
					struct atomisp_sub_device *asd,
					enum atomisp_input_stream_id stream_id,
					unsigned int width,
					unsigned int height);

void atomisp_css_video_set_dis_envelope(struct atomisp_sub_device *asd,
					unsigned int dvs_w, unsigned int dvs_h);

void atomisp_css_input_set_two_pixels_per_clock(
					struct atomisp_sub_device *asd,
					bool two_ppc);

void atomisp_css_enable_raw_binning(struct atomisp_sub_device *asd,
					bool enable);

void atomisp_css_enable_dz(struct atomisp_sub_device *asd, bool enable);

void atomisp_css_capture_set_mode(struct atomisp_sub_device *asd,
				enum atomisp_css_capture_mode mode);

void atomisp_css_input_set_mode(struct atomisp_sub_device *asd,
				enum atomisp_css_input_mode mode);

void atomisp_css_capture_enable_online(struct atomisp_sub_device *asd,
				unsigned short stream_index, bool enable);

void atomisp_css_preview_enable_online(struct atomisp_sub_device *asd,
				unsigned short stream_index, bool enable);

void atomisp_css_video_enable_online(struct atomisp_sub_device *asd,
							bool enable);

void atomisp_css_enable_continuous(struct atomisp_sub_device *asd,
							bool enable);

void atomisp_css_enable_cvf(struct atomisp_sub_device *asd,
							bool enable);

int atomisp_css_input_configure_port(struct atomisp_sub_device *asd,
				mipi_port_ID_t port,
				unsigned int num_lanes,
				unsigned int timeout,
				unsigned int mipi_freq,
				enum atomisp_css_stream_format metadata_format,
				unsigned int metadata_width,
				unsigned int metadata_height);

int atomisp_css_frame_allocate(struct atomisp_css_frame **frame,
				unsigned int width, unsigned int height,
				enum atomisp_css_frame_format format,
				unsigned int padded_width,
				unsigned int raw_bit_depth);

int atomisp_css_frame_allocate_from_info(struct atomisp_css_frame **frame,
				const struct atomisp_css_frame_info *info);

void atomisp_css_frame_free(struct atomisp_css_frame *frame);

int atomisp_css_frame_map(struct atomisp_css_frame **frame,
				const struct atomisp_css_frame_info *info,
				const void *data, uint16_t attribute,
				void *context);

int atomisp_css_set_black_frame(struct atomisp_sub_device *asd,
			const struct atomisp_css_frame *raw_black_frame);

int atomisp_css_allocate_continuous_frames(bool init_time,
			struct atomisp_sub_device *asd);

void atomisp_css_update_continuous_frames(struct atomisp_sub_device *asd);

void atomisp_create_pipes_stream(struct atomisp_sub_device *asd);
void atomisp_destroy_pipes_stream_force(struct atomisp_sub_device *asd);

int atomisp_css_stop(struct atomisp_sub_device *asd,
			enum atomisp_css_pipe_id pipe_id, bool in_reset);

int atomisp_css_continuous_set_num_raw_frames(
					struct atomisp_sub_device *asd,
					int num_frames);

void atomisp_css_disable_vf_pp(struct atomisp_sub_device *asd,
			       bool disable);

int atomisp_css_copy_configure_output(struct atomisp_sub_device *asd,
				unsigned int stream_index,
				unsigned int width, unsigned int height,
				unsigned int padded_width,
				enum atomisp_css_frame_format format);

int atomisp_css_yuvpp_configure_output(struct atomisp_sub_device *asd,
				unsigned int stream_index,
				unsigned int width, unsigned int height,
				unsigned int padded_width,
				enum atomisp_css_frame_format format);

int atomisp_css_yuvpp_configure_viewfinder(
				struct atomisp_sub_device *asd,
				unsigned int stream_index,
				unsigned int width, unsigned int height,
				unsigned int min_width,
				enum atomisp_css_frame_format format);

int atomisp_css_yuvpp_get_output_frame_info(
					struct atomisp_sub_device *asd,
					unsigned int stream_index,
					struct atomisp_css_frame_info *info);

int atomisp_css_yuvpp_get_viewfinder_frame_info(
					struct atomisp_sub_device *asd,
					unsigned int stream_index,
					struct atomisp_css_frame_info *info);

int atomisp_css_preview_configure_output(struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height,
				unsigned int min_width,
				enum atomisp_css_frame_format format);

int atomisp_css_capture_configure_output(struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height,
				unsigned int min_width,
				enum atomisp_css_frame_format format);

int atomisp_css_video_configure_output(struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height,
				unsigned int min_width,
				enum atomisp_css_frame_format format);

int atomisp_get_css_frame_info(struct atomisp_sub_device *asd,
				uint16_t source_pad,
				struct atomisp_css_frame_info *frame_info);

int atomisp_css_video_configure_viewfinder(struct atomisp_sub_device *asd,
					unsigned int width, unsigned int height,
					unsigned int min_width,
					enum atomisp_css_frame_format format);

int atomisp_css_capture_configure_viewfinder(
					struct atomisp_sub_device *asd,
					unsigned int width, unsigned int height,
					unsigned int min_width,
					enum atomisp_css_frame_format format);

int atomisp_css_video_get_viewfinder_frame_info(
					struct atomisp_sub_device *asd,
					struct atomisp_css_frame_info *info);

int atomisp_css_capture_get_viewfinder_frame_info(
					struct atomisp_sub_device *asd,
					struct atomisp_css_frame_info *info);

int atomisp_css_copy_get_output_frame_info(
					struct atomisp_sub_device *asd,
					unsigned int stream_index,
					struct atomisp_css_frame_info *info);

int atomisp_css_capture_get_output_raw_frame_info(
					struct atomisp_sub_device *asd,
					struct atomisp_css_frame_info *info);

int atomisp_css_preview_get_output_frame_info(
					struct atomisp_sub_device *asd,
					struct atomisp_css_frame_info *info);

int atomisp_css_capture_get_output_frame_info(
					struct atomisp_sub_device *asd,
					struct atomisp_css_frame_info *info);

int atomisp_css_video_get_output_frame_info(
					struct atomisp_sub_device *asd,
					struct atomisp_css_frame_info *info);

int atomisp_css_preview_configure_pp_input(
				struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height);

int atomisp_css_capture_configure_pp_input(
				struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height);

int atomisp_css_video_configure_pp_input(
				struct atomisp_sub_device *asd,
				unsigned int width, unsigned int height);

int atomisp_css_offline_capture_configure(struct atomisp_sub_device *asd,
			int num_captures, unsigned int skip, int offset);
int atomisp_css_exp_id_capture(struct atomisp_sub_device *asd, int exp_id);
int atomisp_css_exp_id_unlock(struct atomisp_sub_device *asd, int exp_id);

int atomisp_css_capture_enable_xnr(struct atomisp_sub_device *asd,
				   bool enable);

void atomisp_css_send_input_frame(struct atomisp_sub_device *asd,
				  unsigned short *data, unsigned int width,
				  unsigned int height);

bool atomisp_css_isp_has_started(void);

void atomisp_css_request_flash(struct atomisp_sub_device *asd);

void atomisp_css_set_wb_config(struct atomisp_sub_device *asd,
			struct atomisp_css_wb_config *wb_config);

void atomisp_css_set_ob_config(struct atomisp_sub_device *asd,
			struct atomisp_css_ob_config *ob_config);

void atomisp_css_set_dp_config(struct atomisp_sub_device *asd,
			struct atomisp_css_dp_config *dp_config);

void atomisp_css_set_de_config(struct atomisp_sub_device *asd,
			struct atomisp_css_de_config *de_config);

void atomisp_css_set_dz_config(struct atomisp_sub_device *asd,
			struct atomisp_css_dz_config *dz_config);

void atomisp_css_set_default_de_config(struct atomisp_sub_device *asd);

void atomisp_css_set_ce_config(struct atomisp_sub_device *asd,
			struct atomisp_css_ce_config *ce_config);

void atomisp_css_set_nr_config(struct atomisp_sub_device *asd,
			struct atomisp_css_nr_config *nr_config);

void atomisp_css_set_ee_config(struct atomisp_sub_device *asd,
			struct atomisp_css_ee_config *ee_config);

void atomisp_css_set_tnr_config(struct atomisp_sub_device *asd,
			struct atomisp_css_tnr_config *tnr_config);

void atomisp_css_set_cc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_cc_config *cc_config);

void atomisp_css_set_macc_table(struct atomisp_sub_device *asd,
			struct atomisp_css_macc_table *macc_table);

void atomisp_css_set_gamma_table(struct atomisp_sub_device *asd,
			struct atomisp_css_gamma_table *gamma_table);

void atomisp_css_set_ctc_table(struct atomisp_sub_device *asd,
			struct atomisp_css_ctc_table *ctc_table);

void atomisp_css_set_gc_config(struct atomisp_sub_device *asd,
			struct atomisp_css_gc_config *gc_config);

void atomisp_css_set_3a_config(struct atomisp_sub_device *asd,
			struct atomisp_css_3a_config *s3a_config);

void atomisp_css_video_set_dis_vector(struct atomisp_sub_device *asd,
				struct atomisp_dis_vector *vector);

void atomisp_css_set_dvs2_coefs(struct atomisp_sub_device *asd,
				struct ia_css_dvs2_coefficients *coefs);

int atomisp_css_set_dis_coefs(struct atomisp_sub_device *asd,
			  struct atomisp_dis_coefficients *coefs);

void atomisp_css_set_zoom_factor(struct atomisp_sub_device *asd,
					unsigned int zoom);

int atomisp_css_get_wb_config(struct atomisp_sub_device *asd,
			struct atomisp_wb_config *config);

int atomisp_css_get_ob_config(struct atomisp_sub_device *asd,
			struct atomisp_ob_config *config);

int atomisp_css_get_dp_config(struct atomisp_sub_device *asd,
			struct atomisp_dp_config *config);

int atomisp_css_get_de_config(struct atomisp_sub_device *asd,
			struct atomisp_de_config *config);

int atomisp_css_get_nr_config(struct atomisp_sub_device *asd,
			struct atomisp_nr_config *config);

int atomisp_css_get_ee_config(struct atomisp_sub_device *asd,
			struct atomisp_ee_config *config);

int atomisp_css_get_tnr_config(struct atomisp_sub_device *asd,
			struct atomisp_tnr_config *config);

int atomisp_css_get_ctc_table(struct atomisp_sub_device *asd,
			struct atomisp_ctc_table *config);

int atomisp_css_get_gamma_table(struct atomisp_sub_device *asd,
			struct atomisp_gamma_table *config);

int atomisp_css_get_gc_config(struct atomisp_sub_device *asd,
			struct atomisp_gc_config *config);

int atomisp_css_get_3a_config(struct atomisp_sub_device *asd,
			struct atomisp_3a_config *config);

int atomisp_css_get_formats_config(struct atomisp_sub_device *asd,
			struct atomisp_formats_config *formats_config);

void atomisp_css_set_formats_config(struct atomisp_sub_device *asd,
			struct atomisp_css_formats_config *formats_config);

int atomisp_css_get_zoom_factor(struct atomisp_sub_device *asd,
					unsigned int *zoom);

struct atomisp_css_shading_table *atomisp_css_shading_table_alloc(
				unsigned int width, unsigned int height);

void atomisp_css_set_shading_table(struct atomisp_sub_device *asd,
				struct atomisp_css_shading_table *table);

void atomisp_css_shading_table_free(struct atomisp_css_shading_table *table);

struct atomisp_css_morph_table *atomisp_css_morph_table_allocate(
				unsigned int width, unsigned int height);

void atomisp_css_set_morph_table(struct atomisp_sub_device *asd,
				struct atomisp_css_morph_table *table);

void atomisp_css_get_morph_table(struct atomisp_sub_device *asd,
				struct atomisp_css_morph_table *table);

void atomisp_css_morph_table_free(struct atomisp_css_morph_table *table);

void atomisp_css_set_cont_prev_start_time(struct atomisp_device *isp,
					unsigned int overlap);

int atomisp_css_get_dis_stat(struct atomisp_sub_device *asd,
			 struct atomisp_dis_statistics *stats);

int atomisp_css_update_stream(struct atomisp_sub_device *asd);

int atomisp_css_create_acc_pipe(struct atomisp_sub_device *asd);

int atomisp_css_start_acc_pipe(struct atomisp_sub_device *asd);

int atomisp_css_stop_acc_pipe(struct atomisp_sub_device *asd);

void atomisp_css_destroy_acc_pipe(struct atomisp_sub_device *asd);

int atomisp_css_load_acc_extension(struct atomisp_sub_device *asd,
					struct atomisp_css_fw_info *fw,
					enum atomisp_css_pipe_id pipe_id,
					unsigned int type);

void atomisp_css_unload_acc_extension(struct atomisp_sub_device *asd,
					struct atomisp_css_fw_info *fw,
					enum atomisp_css_pipe_id pipe_id);

int atomisp_css_wait_acc_finish(struct atomisp_sub_device *asd);

void atomisp_css_acc_done(struct atomisp_sub_device *asd);

int atomisp_css_load_acc_binary(struct atomisp_sub_device *asd,
					struct atomisp_css_fw_info *fw,
					unsigned int index);

void atomisp_css_unload_acc_binary(struct atomisp_sub_device *asd);

struct atomisp_acc_fw;
int atomisp_css_set_acc_parameters(struct atomisp_acc_fw *acc_fw);

int atomisp_css_isr_thread(struct atomisp_device *isp,
			   bool *frame_done_found,
			   bool *css_pipe_done);

bool atomisp_css_valid_sof(struct atomisp_device *isp);

void atomisp_en_dz_capt_pipe(struct atomisp_sub_device *asd, bool enable);

#endif
