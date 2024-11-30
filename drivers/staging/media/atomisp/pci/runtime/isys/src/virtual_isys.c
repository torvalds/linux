// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include <linux/bitops.h>
#include <linux/math.h>
#include <linux/string.h> /* for memcpy() */

#include "system_global.h"


#include "ia_css_isys.h"
#include "ia_css_debug.h"
#include "virtual_isys.h"
#include "isp.h"
#include "sh_css_defs.h"

/*************************************************
 *
 * Forwarded Declaration
 *
 *************************************************/

static bool create_input_system_channel(
    isp2401_input_system_cfg_t	*cfg,
    bool			metadata,
    input_system_channel_t	*channel);

static void destroy_input_system_channel(
    input_system_channel_t	*channel);

static bool create_input_system_input_port(
    isp2401_input_system_cfg_t		*cfg,
    input_system_input_port_t	*input_port);

static void destroy_input_system_input_port(
    input_system_input_port_t	*input_port);

static bool calculate_input_system_channel_cfg(
    input_system_channel_t		*channel,
    input_system_input_port_t	*input_port,
    isp2401_input_system_cfg_t		*isys_cfg,
    input_system_channel_cfg_t	*channel_cfg,
    bool metadata);

static bool calculate_input_system_input_port_cfg(
    input_system_channel_t		*channel,
    input_system_input_port_t	*input_port,
    isp2401_input_system_cfg_t		*isys_cfg,
    input_system_input_port_cfg_t	*input_port_cfg);

static bool acquire_sid(
    stream2mmio_ID_t	stream2mmio,
    stream2mmio_sid_ID_t	*sid);

static void release_sid(
    stream2mmio_ID_t	stream2mmio,
    stream2mmio_sid_ID_t	*sid);

static bool acquire_ib_buffer(
    s32 bits_per_pixel,
    s32 pixels_per_line,
    s32 lines_per_frame,
    s32 align_in_bytes,
    bool online,
    isp2401_ib_buffer_t *buf);

static void release_ib_buffer(
    isp2401_ib_buffer_t *buf);

static bool acquire_dma_channel(
    isys2401_dma_ID_t	dma_id,
    isys2401_dma_channel	*channel);

static void release_dma_channel(
    isys2401_dma_ID_t	dma_id,
    isys2401_dma_channel	*channel);

static bool acquire_be_lut_entry(
    csi_rx_backend_ID_t		backend,
    csi_mipi_packet_type_t		packet_type,
    csi_rx_backend_lut_entry_t	*entry);

static void release_be_lut_entry(
    csi_rx_backend_ID_t		backend,
    csi_mipi_packet_type_t		packet_type,
    csi_rx_backend_lut_entry_t	*entry);

static bool calculate_prbs_cfg(
    input_system_channel_t		*channel,
    input_system_input_port_t	*input_port,
    isp2401_input_system_cfg_t		*isys_cfg,
    pixelgen_prbs_cfg_t		*cfg);

static bool calculate_fe_cfg(
    const isp2401_input_system_cfg_t	*isys_cfg,
    csi_rx_frontend_cfg_t		*cfg);

static bool calculate_be_cfg(
    const input_system_input_port_t	*input_port,
    const isp2401_input_system_cfg_t	*isys_cfg,
    bool				metadata,
    csi_rx_backend_cfg_t		*cfg);

static bool calculate_stream2mmio_cfg(
    const isp2401_input_system_cfg_t	*isys_cfg,
    bool				metadata,
    stream2mmio_cfg_t		*cfg);

static bool calculate_ibuf_ctrl_cfg(
    const input_system_channel_t	*channel,
    const input_system_input_port_t	*input_port,
    const isp2401_input_system_cfg_t	*isys_cfg,
    ibuf_ctrl_cfg_t			*cfg);

static bool calculate_isys2401_dma_cfg(
    const input_system_channel_t	*channel,
    const isp2401_input_system_cfg_t	*isys_cfg,
    isys2401_dma_cfg_t		*cfg);

static bool calculate_isys2401_dma_port_cfg(
    const isp2401_input_system_cfg_t	*isys_cfg,
    bool				raw_packed,
    bool				metadata,
    isys2401_dma_port_cfg_t		*cfg);

static csi_mipi_packet_type_t get_csi_mipi_packet_type(
    int32_t data_type);

static int32_t calculate_stride(
    s32 bits_per_pixel,
    s32 pixels_per_line,
    bool	raw_packed,
    int32_t	align_in_bytes);

/* end of Forwarded Declaration */

/**************************************************
 *
 * Public Methods
 *
 **************************************************/
ia_css_isys_error_t ia_css_isys_stream_create(
    ia_css_isys_descr_t	*isys_stream_descr,
    ia_css_isys_stream_h	isys_stream,
    uint32_t isys_stream_id)
{
	ia_css_isys_error_t rc;

	if (!isys_stream_descr || !isys_stream ||
	    isys_stream_id >= SH_CSS_MAX_ISYS_CHANNEL_NODES)
		return	false;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "ia_css_isys_stream_create() enter:\n");

	/*Reset isys_stream to 0*/
	memset(isys_stream, 0, sizeof(*isys_stream));
	isys_stream->enable_metadata = isys_stream_descr->metadata.enable;
	isys_stream->id = isys_stream_id;

	isys_stream->linked_isys_stream_id = isys_stream_descr->linked_isys_stream_id;
	rc = create_input_system_input_port(isys_stream_descr,
					    &isys_stream->input_port);
	if (!rc)
		return false;

	rc = create_input_system_channel(isys_stream_descr, false,
					 &isys_stream->channel);
	if (!rc) {
		destroy_input_system_input_port(&isys_stream->input_port);
		return false;
	}

	/* create metadata channel */
	if (isys_stream_descr->metadata.enable) {
		rc = create_input_system_channel(isys_stream_descr, true,
						 &isys_stream->md_channel);
		if (!rc) {
			destroy_input_system_input_port(&isys_stream->input_port);
			destroy_input_system_channel(&isys_stream->channel);
			return false;
		}
	}
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "ia_css_isys_stream_create() leave:\n");

	return true;
}

void ia_css_isys_stream_destroy(
    ia_css_isys_stream_h	isys_stream)
{
	destroy_input_system_input_port(&isys_stream->input_port);
	destroy_input_system_channel(&isys_stream->channel);
	if (isys_stream->enable_metadata) {
		/* Destroy metadata channel only if its allocated*/
		destroy_input_system_channel(&isys_stream->md_channel);
	}
}

ia_css_isys_error_t ia_css_isys_stream_calculate_cfg(
    ia_css_isys_stream_h		isys_stream,
    ia_css_isys_descr_t		*isys_stream_descr,
    ia_css_isys_stream_cfg_t	*isys_stream_cfg)
{
	ia_css_isys_error_t rc;

	if (!isys_stream_cfg		||
	    !isys_stream_descr	||
	    !isys_stream)
		return false;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "ia_css_isys_stream_calculate_cfg() enter:\n");

	rc  = calculate_input_system_channel_cfg(
		  &isys_stream->channel,
		  &isys_stream->input_port,
		  isys_stream_descr,
		  &isys_stream_cfg->channel_cfg,
		  false);
	if (!rc)
		return false;

	/* configure metadata channel */
	if (isys_stream_descr->metadata.enable) {
		isys_stream_cfg->enable_metadata = true;
		rc  = calculate_input_system_channel_cfg(
			  &isys_stream->md_channel,
			  &isys_stream->input_port,
			  isys_stream_descr,
			  &isys_stream_cfg->md_channel_cfg,
			  true);
		if (!rc)
			return false;
	}

	rc = calculate_input_system_input_port_cfg(
		 &isys_stream->channel,
		 &isys_stream->input_port,
		 isys_stream_descr,
		 &isys_stream_cfg->input_port_cfg);
	if (!rc)
		return false;

	isys_stream->valid = 1;
	isys_stream_cfg->valid = 1;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "ia_css_isys_stream_calculate_cfg() leave:\n");
	return rc;
}

/* end of Public Methods */

/**************************************************
 *
 * Private Methods
 *
 **************************************************/
static bool create_input_system_channel(
    isp2401_input_system_cfg_t	*cfg,
    bool			metadata,
    input_system_channel_t	*me)
{
	bool rc = true;

	me->dma_id = ISYS2401_DMA0_ID;

	switch (cfg->input_port_id) {
	case INPUT_SYSTEM_CSI_PORT0_ID:
	case INPUT_SYSTEM_PIXELGEN_PORT0_ID:
		me->stream2mmio_id = STREAM2MMIO0_ID;
		me->ibuf_ctrl_id = IBUF_CTRL0_ID;
		break;

	case INPUT_SYSTEM_CSI_PORT1_ID:
	case INPUT_SYSTEM_PIXELGEN_PORT1_ID:
		me->stream2mmio_id = STREAM2MMIO1_ID;
		me->ibuf_ctrl_id = IBUF_CTRL1_ID;
		break;

	case INPUT_SYSTEM_CSI_PORT2_ID:
	case INPUT_SYSTEM_PIXELGEN_PORT2_ID:
		me->stream2mmio_id = STREAM2MMIO2_ID;
		me->ibuf_ctrl_id = IBUF_CTRL2_ID;
		break;
	default:
		rc = false;
		break;
	}

	if (!rc)
		return false;

	if (!acquire_sid(me->stream2mmio_id, &me->stream2mmio_sid_id)) {
		return false;
	}

	if (!acquire_ib_buffer(
		metadata ? cfg->metadata.bits_per_pixel :
		cfg->input_port_resolution.bits_per_pixel,
		metadata ? cfg->metadata.pixels_per_line :
		cfg->input_port_resolution.pixels_per_line,
		metadata ? cfg->metadata.lines_per_frame :
		cfg->input_port_resolution.lines_per_frame,
		metadata ? cfg->metadata.align_req_in_bytes :
		cfg->input_port_resolution.align_req_in_bytes,
		cfg->online,
		&me->ib_buffer)) {
		release_sid(me->stream2mmio_id, &me->stream2mmio_sid_id);
		return false;
	}

	if (!acquire_dma_channel(me->dma_id, &me->dma_channel)) {
		release_sid(me->stream2mmio_id, &me->stream2mmio_sid_id);
		release_ib_buffer(&me->ib_buffer);
		return false;
	}

	return true;
}

static void destroy_input_system_channel(
    input_system_channel_t	*me)
{
	release_sid(me->stream2mmio_id,
		    &me->stream2mmio_sid_id);

	release_ib_buffer(&me->ib_buffer);

	release_dma_channel(me->dma_id, &me->dma_channel);
}

static bool create_input_system_input_port(
    isp2401_input_system_cfg_t		*cfg,
    input_system_input_port_t	*me)
{
	csi_mipi_packet_type_t packet_type;
	bool rc = true;

	switch (cfg->input_port_id) {
	case INPUT_SYSTEM_CSI_PORT0_ID:
		me->csi_rx.frontend_id = CSI_RX_FRONTEND0_ID;
		me->csi_rx.backend_id = CSI_RX_BACKEND0_ID;

		packet_type = get_csi_mipi_packet_type(cfg->csi_port_attr.fmt_type);
		me->csi_rx.packet_type = packet_type;

		rc = acquire_be_lut_entry(
			 me->csi_rx.backend_id,
			 packet_type,
			 &me->csi_rx.backend_lut_entry);
		break;
	case INPUT_SYSTEM_PIXELGEN_PORT0_ID:
		me->pixelgen.pixelgen_id = PIXELGEN0_ID;
		break;
	case INPUT_SYSTEM_CSI_PORT1_ID:
		me->csi_rx.frontend_id = CSI_RX_FRONTEND1_ID;
		me->csi_rx.backend_id = CSI_RX_BACKEND1_ID;

		packet_type = get_csi_mipi_packet_type(cfg->csi_port_attr.fmt_type);
		me->csi_rx.packet_type = packet_type;

		rc = acquire_be_lut_entry(
			 me->csi_rx.backend_id,
			 packet_type,
			 &me->csi_rx.backend_lut_entry);
		break;
	case INPUT_SYSTEM_PIXELGEN_PORT1_ID:
		me->pixelgen.pixelgen_id = PIXELGEN1_ID;

		break;
	case INPUT_SYSTEM_CSI_PORT2_ID:
		me->csi_rx.frontend_id = CSI_RX_FRONTEND2_ID;
		me->csi_rx.backend_id = CSI_RX_BACKEND2_ID;

		packet_type = get_csi_mipi_packet_type(cfg->csi_port_attr.fmt_type);
		me->csi_rx.packet_type = packet_type;

		rc = acquire_be_lut_entry(
			 me->csi_rx.backend_id,
			 packet_type,
			 &me->csi_rx.backend_lut_entry);
		break;
	case INPUT_SYSTEM_PIXELGEN_PORT2_ID:
		me->pixelgen.pixelgen_id = PIXELGEN2_ID;
		break;
	default:
		rc = false;
		break;
	}

	me->source_type = cfg->mode;

	/* for metadata */
	me->metadata.packet_type = CSI_MIPI_PACKET_TYPE_UNDEFINED;
	if (rc && cfg->metadata.enable) {
		me->metadata.packet_type = get_csi_mipi_packet_type(
					       cfg->metadata.fmt_type);
		rc = acquire_be_lut_entry(
			 me->csi_rx.backend_id,
			 me->metadata.packet_type,
			 &me->metadata.backend_lut_entry);
	}

	return rc;
}

static void destroy_input_system_input_port(
    input_system_input_port_t	*me)
{
	if (me->source_type == INPUT_SYSTEM_SOURCE_TYPE_SENSOR) {
		release_be_lut_entry(
		    me->csi_rx.backend_id,
		    me->csi_rx.packet_type,
		    &me->csi_rx.backend_lut_entry);
	}

	if (me->metadata.packet_type != CSI_MIPI_PACKET_TYPE_UNDEFINED) {
		/*Free the backend lut allocated for metadata*/
		release_be_lut_entry(
		    me->csi_rx.backend_id,
		    me->metadata.packet_type,
		    &me->metadata.backend_lut_entry);
	}
}

static bool calculate_input_system_channel_cfg(
    input_system_channel_t		*channel,
    input_system_input_port_t	*input_port,
    isp2401_input_system_cfg_t		*isys_cfg,
    input_system_channel_cfg_t	*channel_cfg,
    bool metadata)
{
	bool rc;

	rc = calculate_stream2mmio_cfg(isys_cfg, metadata,
				       &channel_cfg->stream2mmio_cfg);
	if (!rc)
		return false;

	rc = calculate_ibuf_ctrl_cfg(
		 channel,
		 input_port,
		 isys_cfg,
		 &channel_cfg->ibuf_ctrl_cfg);
	if (!rc)
		return false;
	if (metadata)
		channel_cfg->ibuf_ctrl_cfg.stores_per_frame =
		    isys_cfg->metadata.lines_per_frame;

	rc = calculate_isys2401_dma_cfg(
		 channel,
		 isys_cfg,
		 &channel_cfg->dma_cfg);
	if (!rc)
		return false;

	rc = calculate_isys2401_dma_port_cfg(
		 isys_cfg,
		 false,
		 metadata,
		 &channel_cfg->dma_src_port_cfg);
	if (!rc)
		return false;

	rc = calculate_isys2401_dma_port_cfg(
		 isys_cfg,
		 isys_cfg->raw_packed,
		 metadata,
		 &channel_cfg->dma_dest_port_cfg);
	if (!rc)
		return false;

	return true;
}

static bool calculate_input_system_input_port_cfg(
    input_system_channel_t		*channel,
    input_system_input_port_t	*input_port,
    isp2401_input_system_cfg_t		*isys_cfg,
    input_system_input_port_cfg_t	*input_port_cfg)
{
	bool rc;

	switch (input_port->source_type) {
	case INPUT_SYSTEM_SOURCE_TYPE_SENSOR:
		rc  = calculate_fe_cfg(
			  isys_cfg,
			  &input_port_cfg->csi_rx_cfg.frontend_cfg);

		rc &= calculate_be_cfg(
			  input_port,
			  isys_cfg,
			  false,
			  &input_port_cfg->csi_rx_cfg.backend_cfg);

		if (rc && isys_cfg->metadata.enable)
			rc &= calculate_be_cfg(input_port, isys_cfg, true,
					       &input_port_cfg->csi_rx_cfg.md_backend_cfg);
		break;
	case INPUT_SYSTEM_SOURCE_TYPE_PRBS:
		rc = calculate_prbs_cfg(
			 channel,
			 input_port,
			 isys_cfg,
			 &input_port_cfg->pixelgen_cfg.prbs_cfg);
		break;
	default:
		rc = false;
		break;
	}

	return rc;
}

static bool acquire_sid(
    stream2mmio_ID_t	stream2mmio,
    stream2mmio_sid_ID_t	*sid)
{
	return ia_css_isys_stream2mmio_sid_rmgr_acquire(stream2mmio, sid);
}

static void release_sid(
    stream2mmio_ID_t	stream2mmio,
    stream2mmio_sid_ID_t	*sid)
{
	ia_css_isys_stream2mmio_sid_rmgr_release(stream2mmio, sid);
}

/* See also: ia_css_dma_configure_from_info() */
static int32_t calculate_stride(
    s32 bits_per_pixel,
    s32 pixels_per_line,
    bool	raw_packed,
    int32_t align_in_bytes)
{
	s32 bytes_per_line;
	s32 pixels_per_word;
	s32 words_per_line;
	s32 pixels_per_line_padded;

	pixels_per_line_padded = CEIL_MUL(pixels_per_line, align_in_bytes);

	if (!raw_packed)
		bits_per_pixel = CEIL_MUL(bits_per_pixel, 8);

	pixels_per_word = HIVE_ISP_DDR_WORD_BITS / bits_per_pixel;
	words_per_line  = DIV_ROUND_UP(pixels_per_line_padded, pixels_per_word);
	bytes_per_line  = HIVE_ISP_DDR_WORD_BYTES * words_per_line;

	return bytes_per_line;
}

static bool acquire_ib_buffer(
    s32 bits_per_pixel,
    s32 pixels_per_line,
    s32 lines_per_frame,
    s32 align_in_bytes,
    bool online,
    isp2401_ib_buffer_t *buf)
{
	buf->stride = calculate_stride(bits_per_pixel, pixels_per_line, false,
				       align_in_bytes);
	if (online)
		buf->lines = 4; /* use double buffering for online usecases */
	else
		buf->lines = 2;

	(void)(lines_per_frame);
	return ia_css_isys_ibuf_rmgr_acquire(buf->stride * buf->lines,
					     &buf->start_addr);
}

static void release_ib_buffer(
    isp2401_ib_buffer_t *buf)
{
	ia_css_isys_ibuf_rmgr_release(&buf->start_addr);
}

static bool acquire_dma_channel(
    isys2401_dma_ID_t	dma_id,
    isys2401_dma_channel	*channel)
{
	return ia_css_isys_dma_channel_rmgr_acquire(dma_id, channel);
}

static void release_dma_channel(
    isys2401_dma_ID_t	dma_id,
    isys2401_dma_channel	*channel)
{
	ia_css_isys_dma_channel_rmgr_release(dma_id, channel);
}

static bool acquire_be_lut_entry(
    csi_rx_backend_ID_t		backend,
    csi_mipi_packet_type_t		packet_type,
    csi_rx_backend_lut_entry_t	*entry)
{
	return ia_css_isys_csi_rx_lut_rmgr_acquire(backend, packet_type, entry);
}

static void release_be_lut_entry(
    csi_rx_backend_ID_t		backend,
    csi_mipi_packet_type_t		packet_type,
    csi_rx_backend_lut_entry_t	*entry)
{
	ia_css_isys_csi_rx_lut_rmgr_release(backend, packet_type, entry);
}

static bool calculate_prbs_cfg(
    input_system_channel_t		*channel,
    input_system_input_port_t	*input_port,
    isp2401_input_system_cfg_t		*isys_cfg,
    pixelgen_prbs_cfg_t		*cfg)
{
	memcpy(cfg, &isys_cfg->prbs_port_attr, sizeof(pixelgen_prbs_cfg_t));

	return true;
}

static bool calculate_fe_cfg(
    const isp2401_input_system_cfg_t	*isys_cfg,
    csi_rx_frontend_cfg_t		*cfg)
{
	cfg->active_lanes = isys_cfg->csi_port_attr.active_lanes;
	return true;
}

static bool calculate_be_cfg(
    const input_system_input_port_t	*input_port,
    const isp2401_input_system_cfg_t	*isys_cfg,
    bool				metadata,
    csi_rx_backend_cfg_t		*cfg)
{
	memcpy(&cfg->lut_entry,
	      metadata ? &input_port->metadata.backend_lut_entry :
			 &input_port->csi_rx.backend_lut_entry,
	      sizeof(csi_rx_backend_lut_entry_t));

	cfg->csi_mipi_cfg.virtual_channel = isys_cfg->csi_port_attr.ch_id;
	if (metadata) {
		cfg->csi_mipi_packet_type = get_csi_mipi_packet_type(
						isys_cfg->metadata.fmt_type);
		cfg->csi_mipi_cfg.comp_enable = false;
		cfg->csi_mipi_cfg.data_type = isys_cfg->metadata.fmt_type;
	} else {
		cfg->csi_mipi_packet_type = get_csi_mipi_packet_type(
						isys_cfg->csi_port_attr.fmt_type);
		cfg->csi_mipi_cfg.data_type = isys_cfg->csi_port_attr.fmt_type;
		cfg->csi_mipi_cfg.comp_enable = isys_cfg->csi_port_attr.comp_enable;
		cfg->csi_mipi_cfg.comp_scheme = isys_cfg->csi_port_attr.comp_scheme;
		cfg->csi_mipi_cfg.comp_predictor = isys_cfg->csi_port_attr.comp_predictor;
		cfg->csi_mipi_cfg.comp_bit_idx = cfg->csi_mipi_cfg.data_type -
						 MIPI_FORMAT_2401_CUSTOM0;
	}

	return true;
}

static bool calculate_stream2mmio_cfg(
    const isp2401_input_system_cfg_t	*isys_cfg,
    bool				metadata,
    stream2mmio_cfg_t		*cfg
)
{
	cfg->bits_per_pixel = metadata ? isys_cfg->metadata.bits_per_pixel :
			      isys_cfg->input_port_resolution.bits_per_pixel;

	cfg->enable_blocking = isys_cfg->mode == INPUT_SYSTEM_SOURCE_TYPE_PRBS;

	return true;
}

static bool calculate_ibuf_ctrl_cfg(
    const input_system_channel_t	*channel,
    const input_system_input_port_t	*input_port,
    const isp2401_input_system_cfg_t	*isys_cfg,
    ibuf_ctrl_cfg_t			*cfg)
{
	s32 bits_per_pixel;
	s32 bytes_per_pixel;
	s32 left_padding;

	(void)input_port;

	bits_per_pixel = isys_cfg->input_port_resolution.bits_per_pixel;
	bytes_per_pixel = BITS_TO_BYTES(bits_per_pixel);

	left_padding = CEIL_MUL(isys_cfg->output_port_attr.left_padding, ISP_VEC_NELEMS)
		       * bytes_per_pixel;

	cfg->online	= isys_cfg->online;

	cfg->dma_cfg.channel	= channel->dma_channel;
	cfg->dma_cfg.cmd	= _DMA_V2_MOVE_A2B_NO_SYNC_CHK_COMMAND;

	cfg->dma_cfg.shift_returned_items	= 0;
	cfg->dma_cfg.elems_per_word_in_ibuf	= 0;
	cfg->dma_cfg.elems_per_word_in_dest	= 0;

	cfg->ib_buffer.start_addr		= channel->ib_buffer.start_addr;
	cfg->ib_buffer.stride			= channel->ib_buffer.stride;
	cfg->ib_buffer.lines			= channel->ib_buffer.lines;

	/*
	#ifndef ISP2401
	 * zhengjie.lu@intel.com:
	#endif
	 * "dest_buf_cfg" should be part of the input system output
	 * port configuration.
	 *
	 * TODO: move "dest_buf_cfg" to the input system output
	 * port configuration.
	 */

	/* input_buf addr only available in sched mode;
	   this buffer is allocated in isp, crun mode addr
	   can be passed by after ISP allocation */
	if (cfg->online) {
		cfg->dest_buf_cfg.start_addr	= ISP_INPUT_BUF_START_ADDR + left_padding;
		cfg->dest_buf_cfg.stride	= bytes_per_pixel
					      * isys_cfg->output_port_attr.max_isp_input_width;
		cfg->dest_buf_cfg.lines		= LINES_OF_ISP_INPUT_BUF;
	} else if (isys_cfg->raw_packed) {
		cfg->dest_buf_cfg.stride	= calculate_stride(bits_per_pixel,
					      isys_cfg->input_port_resolution.pixels_per_line,
					      isys_cfg->raw_packed,
					      isys_cfg->input_port_resolution.align_req_in_bytes);
	} else {
		cfg->dest_buf_cfg.stride	= channel->ib_buffer.stride;
	}

	/*
	#ifndef ISP2401
	 * zhengjie.lu@intel.com:
	#endif
	 * "items_per_store" is hard coded as "1", which is ONLY valid
	 * when the CSI-MIPI long packet is transferred.
	 *
	 * TODO: After the 1st stage of MERR+,  make the proper solution to
	 * configure "items_per_store" so that it can also handle the CSI-MIPI
	 * short packet.
	 */
	cfg->items_per_store		= 1;

	cfg->stores_per_frame		= isys_cfg->input_port_resolution.lines_per_frame;

	cfg->stream2mmio_cfg.sync_cmd	= _STREAM2MMIO_CMD_TOKEN_SYNC_FRAME;

	/* TODO: Define conditions as when to use store words vs store packets */
	cfg->stream2mmio_cfg.store_cmd	= _STREAM2MMIO_CMD_TOKEN_STORE_PACKETS;

	return true;
}

static bool calculate_isys2401_dma_cfg(
    const input_system_channel_t	*channel,
    const isp2401_input_system_cfg_t	*isys_cfg,
    isys2401_dma_cfg_t		*cfg)
{
	cfg->channel	= channel->dma_channel;

	/* only online/sensor mode goto vmem
	   offline/buffered_sensor, tpg and prbs will go to ddr */
	if (isys_cfg->online)
		cfg->connection = isys2401_dma_ibuf_to_vmem_connection;
	else
		cfg->connection = isys2401_dma_ibuf_to_ddr_connection;

	cfg->extension	= isys2401_dma_zero_extension;
	cfg->height	= 1;

	return true;
}

/* See also: ia_css_dma_configure_from_info() */
static bool calculate_isys2401_dma_port_cfg(
    const isp2401_input_system_cfg_t	*isys_cfg,
    bool				raw_packed,
    bool				metadata,
    isys2401_dma_port_cfg_t		*cfg)
{
	s32 bits_per_pixel;
	s32 pixels_per_line;
	s32 align_req_in_bytes;

	/* TODO: Move metadata away from isys_cfg to application layer */
	if (metadata) {
		bits_per_pixel = isys_cfg->metadata.bits_per_pixel;
		pixels_per_line = isys_cfg->metadata.pixels_per_line;
		align_req_in_bytes = isys_cfg->metadata.align_req_in_bytes;
	} else {
		bits_per_pixel = isys_cfg->input_port_resolution.bits_per_pixel;
		pixels_per_line = isys_cfg->input_port_resolution.pixels_per_line;
		align_req_in_bytes = isys_cfg->input_port_resolution.align_req_in_bytes;
	}

	cfg->stride	= calculate_stride(bits_per_pixel, pixels_per_line, raw_packed,
				       align_req_in_bytes);

	if (!raw_packed)
		bits_per_pixel = CEIL_MUL(bits_per_pixel, 8);

	cfg->elements	= HIVE_ISP_DDR_WORD_BITS / bits_per_pixel;
	cfg->cropping	= 0;
	cfg->width	= CEIL_DIV(cfg->stride, HIVE_ISP_DDR_WORD_BYTES);

	return true;
}

static csi_mipi_packet_type_t get_csi_mipi_packet_type(
    int32_t data_type)
{
	csi_mipi_packet_type_t packet_type;

	packet_type = CSI_MIPI_PACKET_TYPE_RESERVED;

	if (data_type >= 0 && data_type <= MIPI_FORMAT_2401_SHORT8)
		packet_type = CSI_MIPI_PACKET_TYPE_SHORT;

	if (data_type > MIPI_FORMAT_2401_SHORT8 && data_type <= N_MIPI_FORMAT_2401)
		packet_type = CSI_MIPI_PACKET_TYPE_LONG;

	return packet_type;
}

/* end of Private Methods */
