/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __IA_CSS_STREAM_PUBLIC_H
#define __IA_CSS_STREAM_PUBLIC_H

/* @file
 * This file contains support for configuring and controlling streams
 */

#include <type_support.h>
#include "ia_css_types.h"
#include "ia_css_pipe_public.h"
#include "ia_css_metadata.h"
#include "ia_css_tpg.h"
#include "ia_css_prbs.h"
#include "ia_css_input_port.h"

/* Input modes, these enumerate all supported input modes.
 *  Note that not all ISP modes support all input modes.
 */
enum ia_css_input_mode {
	IA_CSS_INPUT_MODE_SENSOR, /** data from sensor */
	IA_CSS_INPUT_MODE_FIFO,   /** data from input-fifo */
	IA_CSS_INPUT_MODE_TPG,    /** data from test-pattern generator */
	IA_CSS_INPUT_MODE_PRBS,   /** data from pseudo-random bit stream */
	IA_CSS_INPUT_MODE_MEMORY, /** data from a frame in memory */
	IA_CSS_INPUT_MODE_BUFFERED_SENSOR /** data is sent through mipi buffer */
};

/* Structure of the MIPI buffer configuration
 */
struct ia_css_mipi_buffer_config {
	unsigned int size_mem_words; /** The frame size in the system memory
					  words (32B) */
	bool contiguous;	     /** Allocated memory physically
					  contiguously or not. \deprecated{Will be false always.}*/
	unsigned int nof_mipi_buffers; /** The number of MIPI buffers required for this
					stream */
};

enum {
	IA_CSS_STREAM_ISYS_STREAM_0 = 0,
	IA_CSS_STREAM_DEFAULT_ISYS_STREAM_IDX = IA_CSS_STREAM_ISYS_STREAM_0,
	IA_CSS_STREAM_ISYS_STREAM_1,
	IA_CSS_STREAM_MAX_ISYS_STREAM_PER_CH
};

/* This is input data configuration for one MIPI data type. We can have
 *  multiple of this in one virtual channel.
 */
struct ia_css_stream_isys_stream_config {
	struct ia_css_resolution  input_res; /** Resolution of input data */
	enum atomisp_input_format format; /** Format of input stream. This data
					       format will be mapped to MIPI data
					       type internally. */
	int linked_isys_stream_id; /** default value is -1, other value means
							current isys_stream shares the same buffer with
							indicated isys_stream*/
	bool valid; /** indicate whether other fields have valid value */
};

struct ia_css_stream_input_config {
	struct ia_css_resolution  input_res; /** Resolution of input data */
	struct ia_css_resolution  effective_res; /** Resolution of input data.
							Used for CSS 2400/1 System and deprecated for other
							systems (replaced by input_effective_res in
							ia_css_pipe_config) */
	enum atomisp_input_format format; /** Format of input stream. This data
					       format will be mapped to MIPI data
					       type internally. */
	enum ia_css_bayer_order bayer_order; /** Bayer order for RAW streams */
};

/* Input stream description. This describes how input will flow into the
 *  CSS. This is used to program the CSS hardware.
 */
struct ia_css_stream_config {
	enum ia_css_input_mode    mode; /** Input mode */
	union {
		struct ia_css_input_port  port; /** Port, for sensor only. */
		struct ia_css_tpg_config  tpg;  /** TPG configuration */
		struct ia_css_prbs_config prbs; /** PRBS configuration */
	} source; /** Source of input data */
	unsigned int	      channel_id; /** Channel on which input data
						   will arrive. Use this field
						   to specify virtual channel id.
						   Valid values are: 0, 1, 2, 3 */
	struct ia_css_stream_isys_stream_config
		isys_config[IA_CSS_STREAM_MAX_ISYS_STREAM_PER_CH];
	struct ia_css_stream_input_config input_config;

	/*
	 * Currently, Linux and Windows platforms interpret the binning_factor
	 * parameter differently. In Linux, the binning factor is expressed
	 * in the form 2^N * 2^N
	 */
	/* ISP2401 */
	unsigned int sensor_binning_factor; /** Binning factor used by sensor
						 to produce image data. This is
						 used for shading correction. */
	unsigned int pixels_per_clock; /** Number of pixels per clock, which can be
					    1, 2 or 4. */
	bool online; /** offline will activate RAW copy on SP, use this for
			  continuous capture. */
	/* ISYS2401 usage: ISP receives data directly from sensor, no copy. */
	unsigned int init_num_cont_raw_buf; /** initial number of raw buffers to
					     allocate */
	unsigned int target_num_cont_raw_buf; /** total number of raw buffers to
					     allocate */
	bool pack_raw_pixels; /** Pack pixels in the raw buffers */
	bool continuous; /** Use SP copy feature to continuously capture frames
			      to system memory and run pipes in offline mode */
	bool disable_cont_viewfinder; /** disable continuous viewfinder for ZSL use case */
	s32 flash_gpio_pin; /** pin on which the flash is connected, -1 for no flash */
	int left_padding; /** The number of input-formatter left-paddings, -1 for default from binary.*/
	struct ia_css_mipi_buffer_config
		mipi_buffer_config; /** mipi buffer configuration */
	struct ia_css_metadata_config
		metadata_config;     /** Metadata configuration. */
	bool ia_css_enable_raw_buffer_locking; /** Enable Raw Buffer Locking for HALv3 Support */
	bool lock_all;
	/** Lock all RAW buffers (true) or lock only buffers processed by
	     video or preview pipe (false).
	     This setting needs to be enabled to allow raw buffer locking
	     without continuous viewfinder. */
};

struct ia_css_stream;

/* Stream info, this struct describes properties of a stream after it has been
 *  created.
 */
struct ia_css_stream_info {
	struct ia_css_metadata_info metadata_info;
	/** Info about the metadata layout, this contains the stride. */
};

/* @brief Load default stream configuration
 * @param[in,out]	stream_config The stream configuration.
 * @return	None
 *
 * This function will reset the stream configuration to the default state:
@code
	memset(stream_config, 0, sizeof(*stream_config));
	stream_config->online = true;
	stream_config->left_padding = -1;
@endcode
 */
void ia_css_stream_config_defaults(struct ia_css_stream_config *stream_config);

/*
 * create the internal structures and fill in the configuration data and pipes
 */

/* @brief Creates a stream
* @param[in]	stream_config The stream configuration.
* @param[in]	num_pipes The number of pipes to incorporate in the stream.
* @param[in]	pipes The pipes.
* @param[out]	stream The stream.
* @return	0 or the error code.
*
* This function will create a stream with a given configuration and given pipes.
*/
int
ia_css_stream_create(const struct ia_css_stream_config *stream_config,
		     int num_pipes,
		     struct ia_css_pipe *pipes[],
		     struct ia_css_stream **stream);

/* @brief Destroys a stream
 * @param[in]	stream The stream.
 * @return	0 or the error code.
 *
 * This function will destroy a given stream.
 */
int
ia_css_stream_destroy(struct ia_css_stream *stream);

/* @brief Provides information about a stream
 * @param[in]	stream The stream.
 * @param[out]	stream_info The information about the stream.
 * @return	0 or the error code.
 *
 * This function will destroy a given stream.
 */
int
ia_css_stream_get_info(const struct ia_css_stream *stream,
		       struct ia_css_stream_info *stream_info);


/* @brief Starts the stream.
 * @param[in]	stream The stream.
 * @return 0 or the error code.
 *
 * The dynamic data in
 * the buffers are not used and need to be queued with a separate call
 * to ia_css_pipe_enqueue_buffer.
 * NOTE: this function will only send start event to corresponding
 * thread and will not start SP any more.
 */
int
ia_css_stream_start(struct ia_css_stream *stream);

/* @brief Stop the stream.
 * @param[in]	stream The stream.
 * @return	0 or the error code.
 *
 * NOTE: this function will send stop event to pipes belong to this
 * stream but will not terminate threads.
 */
int
ia_css_stream_stop(struct ia_css_stream *stream);

/* @brief Check if a stream has stopped
 * @param[in]	stream The stream.
 * @return	boolean flag
 *
 * This function will check if the stream has stopped and return the correspondent boolean flag.
 */
bool
ia_css_stream_has_stopped(struct ia_css_stream *stream);

/* @brief	destroy a stream according to the stream seed previosly saved in the seed array.
 * @param[in]	stream The stream.
 * @return	0 (no other errors are generated now)
 *
 * Destroy the stream and all the pipes related to it.
 */
int
ia_css_stream_unload(struct ia_css_stream *stream);

/* @brief Returns stream format
 * @param[in]	stream The stream.
 * @return	format of the string
 *
 * This function will return the stream format.
 */
enum atomisp_input_format
ia_css_stream_get_format(const struct ia_css_stream *stream);

/* @brief Check if the stream is configured for 2 pixels per clock
 * @param[in]	stream The stream.
 * @return	boolean flag
 *
 * This function will check if the stream is configured for 2 pixels per clock and
 * return the correspondent boolean flag.
 */
bool
ia_css_stream_get_two_pixels_per_clock(const struct ia_css_stream *stream);

/* @brief Sets the output frame stride (at the last pipe)
 * @param[in]	stream The stream
 * @param[in]	output_padded_width - the output buffer stride.
 * @return	ia_css_err
 *
 * This function will Set the output frame stride (at the last pipe)
 */
int
ia_css_stream_set_output_padded_width(struct ia_css_stream *stream,
				      unsigned int output_padded_width);

/* @brief Return max number of continuous RAW frames.
 * @param[in]	stream The stream.
 * @param[out]	buffer_depth The maximum number of continuous RAW frames.
 * @return	0 or -EINVAL
 *
 * This function will return the maximum number of continuous RAW frames
 * the system can support.
 */
int
ia_css_stream_get_max_buffer_depth(struct ia_css_stream *stream,
				   int *buffer_depth);

/* @brief Set nr of continuous RAW frames to use.
 *
 * @param[in]	stream The stream.
 * @param[in]	buffer_depth	Number of frames to set.
 * @return	0 or error code upon error.
 *
 * Set the number of continuous frames to use during continuous modes.
 */
int
ia_css_stream_set_buffer_depth(struct ia_css_stream *stream, int buffer_depth);

/* @brief Get number of continuous RAW frames to use.
 * @param[in]	stream The stream.
 * @param[out]	buffer_depth The number of frames to use
 * @return	0 or -EINVAL
 *
 * Get the currently set number of continuous frames
 * to use during continuous modes.
 */
int
ia_css_stream_get_buffer_depth(struct ia_css_stream *stream, int *buffer_depth);

/* ===== CAPTURE ===== */

/* @brief Configure the continuous capture
 *
 * @param[in]	stream		The stream.
 * @param[in]	num_captures	The number of RAW frames to be processed to
 *				YUV. Setting this to -1 will make continuous
 *				capture run until it is stopped.
 *				This number will also be used to allocate RAW
 *				buffers. To allow the viewfinder to also
 *				keep operating, 2 extra buffers will always be
 *				allocated.
 *				If the offset is negative and the skip setting
 *				is greater than 0, additional buffers may be
 *				needed.
 * @param[in]	skip		Skip N frames in between captures. This can be
 *				used to select a slower capture frame rate than
 *				the sensor output frame rate.
 * @param[in]	offset		Start the RAW-to-YUV processing at RAW buffer
 *				with this offset. This allows the user to
 *				process RAW frames that were captured in the
 *				past or future.
 * @return			0 or error code upon error.
 *
 *  For example, to capture the current frame plus the 2 previous
 *  frames and 2 subsequent frames, you would call
 *  ia_css_stream_capture(5, 0, -2).
 */
int
ia_css_stream_capture(struct ia_css_stream *stream,
		      int num_captures,
		      unsigned int skip,
		      int offset);

/* @brief Specify which raw frame to tag based on exp_id found in frame info
 *
 * @param[in]	stream The stream.
 * @param[in]	exp_id	The exposure id of the raw frame to tag.
 *
 * @return			0 or error code upon error.
 *
 * This function allows the user to tag a raw frame based on the exposure id
 * found in the viewfinder frames' frame info.
 */
int
ia_css_stream_capture_frame(struct ia_css_stream *stream,
			    unsigned int exp_id);

/* ===== VIDEO ===== */

/* @brief Send streaming data into the css input FIFO
 *
 * @param[in]	stream	The stream.
 * @param[in]	data	Pointer to the pixels to be send.
 * @param[in]	width	Width of the input frame.
 * @param[in]	height	Height of the input frame.
 * @return	None
 *
 * Send streaming data into the css input FIFO. This is for testing purposes
 * only. This uses the channel ID and input format as set by the user with
 * the regular functions for this.
 * This function blocks until the entire frame has been written into the
 * input FIFO.
 *
 * Note:
 * For higher flexibility the ia_css_stream_send_input_frame is replaced by
 * three separate functions:
 * 1) ia_css_stream_start_input_frame
 * 2) ia_css_stream_send_input_line
 * 3) ia_css_stream_end_input_frame
 * In this way it is possible to stream multiple frames on different
 * channel ID's on a line basis. It will be possible to simulate
 * line-interleaved Stereo 3D muxed on 1 mipi port.
 * These 3 functions are for testing purpose only and can be used in
 * conjunction with ia_css_stream_send_input_frame
 */
void
ia_css_stream_send_input_frame(const struct ia_css_stream *stream,
			       const unsigned short *data,
			       unsigned int width,
			       unsigned int height);

/* @brief Start an input frame on the CSS input FIFO.
 *
 * @param[in]	stream The stream.
 * @return	None
 *
 * Starts the streaming to mipi frame by sending SoF for channel channel_id.
 * It will use the input_format and two_pixels_per_clock as provided by
 * the user.
 * For the "correct" use-case, input_format and two_pixels_per_clock must match
 * with the values as set by the user with the regular functions.
 * To simulate an error, the user can provide "incorrect" values for
 * input_format and/or two_pixels_per_clock.
 */
void
ia_css_stream_start_input_frame(const struct ia_css_stream *stream);

/* @brief Send a line of input data into the CSS input FIFO.
 *
 * @param[in]	stream		The stream.
 * @param[in]	data	Array of the first line of image data.
 * @param	width	The width (in pixels) of the first line.
 * @param[in]	data2	Array of the second line of image data.
 * @param	width2	The width (in pixels) of the second line.
 * @return	None
 *
 * Sends 1 frame line. Start with SoL followed by width bytes of data, followed
 * by width2 bytes of data2 and followed by and EoL
 * It will use the input_format and two_pixels_per_clock settings as provided
 * with the ia_css_stream_start_input_frame function call.
 *
 * This function blocks until the entire line has been written into the
 * input FIFO.
 */
void
ia_css_stream_send_input_line(const struct ia_css_stream *stream,
			      const unsigned short *data,
			      unsigned int width,
			      const unsigned short *data2,
			      unsigned int width2);

/* @brief Send a line of input embedded data into the CSS input FIFO.
 *
 * @param[in]	stream     Pointer of the stream.
 * @param[in]	format     Format of the embedded data.
 * @param[in]	data       Pointer of the embedded data line.
 * @param[in]	width      The width (in pixels) of the line.
 * @return		None
 *
 * Sends one embedded data line to input fifo. Start with SoL followed by
 * width bytes of data, and followed by and EoL.
 * It will use the two_pixels_per_clock settings as provided with the
 * ia_css_stream_start_input_frame function call.
 *
 * This function blocks until the entire line has been written into the
 * input FIFO.
 */
void
ia_css_stream_send_input_embedded_line(const struct ia_css_stream *stream,
				       enum atomisp_input_format format,
				       const unsigned short *data,
				       unsigned int width);

/* @brief End an input frame on the CSS input FIFO.
 *
 * @param[in]	stream	The stream.
 * @return	None
 *
 * Send the end-of-frame signal into the CSS input FIFO.
 */
void
ia_css_stream_end_input_frame(const struct ia_css_stream *stream);

/* @brief send a request flash command to SP
 *
 * @param[in]	stream The stream.
 * @return	None
 *
 * Driver needs to call this function to send a flash request command
 * to SP, SP will be responsible for switching on/off the flash at proper
 * time. Due to the SP multi-threading environment, this request may have
 * one-frame delay, the driver needs to check the flashed flag in frame info
 * to determine which frame is being flashed.
 */
void
ia_css_stream_request_flash(struct ia_css_stream *stream);

/* @brief Configure a stream with filter coefficients.
 *	   @deprecated {Replaced by
 *				   ia_css_pipe_set_isp_config_on_pipe()}
 *
 * @param[in]	stream The stream.
 * @param[in]	config	The set of filter coefficients.
 * @param[in]   pipe Pipe to be updated when set isp config, NULL means to
 *		   update all pipes in the stream.
 * @return		0 or error code upon error.
 *
 * This function configures the filter coefficients for an image
 * stream. For image pipes that do not execute any ISP filters, this
 * function will have no effect.
 * It is safe to call this function while the image stream is running,
 * in fact this is the expected behavior most of the time. Proper
 * resource locking and double buffering is in place to allow for this.
 */
int
ia_css_stream_set_isp_config_on_pipe(struct ia_css_stream *stream,
				     const struct ia_css_isp_config *config,
				     struct ia_css_pipe *pipe);

/* @brief Configure a stream with filter coefficients.
 *	   @deprecated {Replaced by
 *				   ia_css_pipe_set_isp_config()}
 * @param[in]	stream	The stream.
 * @param[in]	config	The set of filter coefficients.
 * @return		0 or error code upon error.
 *
 * This function configures the filter coefficients for an image
 * stream. For image pipes that do not execute any ISP filters, this
 * function will have no effect. All pipes of a stream will be updated.
 * See ::ia_css_stream_set_isp_config_on_pipe() for the per-pipe alternative.
 * It is safe to call this function while the image stream is running,
 * in fact this is the expected behaviour most of the time. Proper
 * resource locking and double buffering is in place to allow for this.
 */
int
ia_css_stream_set_isp_config(
    struct ia_css_stream *stream,
    const struct ia_css_isp_config *config);

/* @brief Get selected configuration settings
 * @param[in]	stream	The stream.
 * @param[out]	config	Configuration settings.
 * @return		None
 */
void
ia_css_stream_get_isp_config(const struct ia_css_stream *stream,
			     struct ia_css_isp_config *config);

/* @brief allocate continuous raw frames for continuous capture
 * @param[in]	stream The stream.
 * @return 0 or error code.
 *
 *  because this allocation takes a long time (around 120ms per frame),
 *  we separate the allocation part and update part to let driver call
 *  this function without locking. This function is the allocation part
 *  and next one is update part
 */
int
ia_css_alloc_continuous_frame_remain(struct ia_css_stream *stream);

/* @brief allocate continuous raw frames for continuous capture
 * @param[in]	stream The stream.
 * @return	0 or error code.
 *
 *  because this allocation takes a long time (around 120ms per frame),
 *  we separate the allocation part and update part to let driver call
 *  this function without locking. This function is the update part
 */
int
ia_css_update_continuous_frames(struct ia_css_stream *stream);

/* @brief ia_css_unlock_raw_frame . unlock a raw frame (HALv3 Support)
 * @param[in]	stream The stream.
 * @param[in]   exp_id exposure id that uniquely identifies the locked Raw Frame Buffer
 * @return      ia_css_err 0 or error code
 *
 * As part of HALv3 Feature requirement, SP locks raw buffer until the Application
 * releases its reference to a raw buffer (which are managed by SP), this function allows
 * application to explicitly unlock that buffer in SP.
 */
int
ia_css_unlock_raw_frame(struct ia_css_stream *stream, uint32_t exp_id);

/* @brief ia_css_en_dz_capt_pipe . Enable/Disable digital zoom for capture pipe
 * @param[in]   stream The stream.
 * @param[in]   enable - true, disable - false
 * @return      None
 *
 * Enables or disables digital zoom for capture pipe in provided stream, if capture pipe
 * exists. This function sets enable_zoom flag in CAPTURE_PP stage of the capture pipe.
 * In process_zoom_and_motion(), decision to enable or disable zoom for every stage depends
 * on this flag.
 */
void
ia_css_en_dz_capt_pipe(struct ia_css_stream *stream, bool enable);
#endif /* __IA_CSS_STREAM_PUBLIC_H */
