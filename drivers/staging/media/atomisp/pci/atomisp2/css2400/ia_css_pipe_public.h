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

#ifndef __IA_CSS_PIPE_PUBLIC_H
#define __IA_CSS_PIPE_PUBLIC_H

/* @file
 * This file contains the public interface for CSS pipes.
 */

#include <type_support.h>
#include <ia_css_err.h>
#include <ia_css_types.h>
#include <ia_css_frame_public.h>
#include <ia_css_buffer.h>
#ifdef ISP2401
#include <ia_css_acc_types.h>
#endif

enum {
	IA_CSS_PIPE_OUTPUT_STAGE_0 = 0,
	IA_CSS_PIPE_OUTPUT_STAGE_1,
	IA_CSS_PIPE_MAX_OUTPUT_STAGE,
};

/* Enumeration of pipe modes. This mode can be used to create
 *  an image pipe for this mode. These pipes can be combined
 *  to configure and run streams on the ISP.
 *
 *  For example, one can create a preview and capture pipe to
 *  create a continuous capture stream.
 */
enum ia_css_pipe_mode {
	IA_CSS_PIPE_MODE_PREVIEW,	/** Preview pipe */
	IA_CSS_PIPE_MODE_VIDEO,		/** Video pipe */
	IA_CSS_PIPE_MODE_CAPTURE,	/** Still capture pipe */
	IA_CSS_PIPE_MODE_ACC,		/** Accelerated pipe */
	IA_CSS_PIPE_MODE_COPY,		/** Copy pipe, only used for embedded/image data copying */
	IA_CSS_PIPE_MODE_YUVPP,		/** YUV post processing pipe, used for all use cases with YUV input,
									for SoC sensor and external ISP */
};
/* Temporary define  */
#define IA_CSS_PIPE_MODE_NUM (IA_CSS_PIPE_MODE_YUVPP + 1)

/**
 * Enumeration of pipe versions.
 * the order should match with definition in sh_css_defs.h
 */
enum ia_css_pipe_version {
	IA_CSS_PIPE_VERSION_1 = 1,		/** ISP1.0 pipe */
	IA_CSS_PIPE_VERSION_2_2 = 2,		/** ISP2.2 pipe */
	IA_CSS_PIPE_VERSION_2_6_1 = 3,		/** ISP2.6.1 pipe */
	IA_CSS_PIPE_VERSION_2_7 = 4		/** ISP2.7 pipe */
};

/**
 * Pipe configuration structure.
 * Resolution properties are filled by Driver, kernel configurations are
 * set by AIC
 */
struct ia_css_pipe_config {
	enum ia_css_pipe_mode mode;
	/** mode, indicates which mode the pipe should use. */
	enum ia_css_pipe_version isp_pipe_version;
	/** pipe version, indicates which imaging pipeline the pipe should use. */
	struct ia_css_resolution input_effective_res;
	/** input effective resolution */
	struct ia_css_resolution bayer_ds_out_res;
	/** bayer down scaling */
	struct ia_css_resolution capt_pp_in_res;
#ifndef ISP2401
	/** bayer down scaling */
#else
	/** capture post processing input resolution */
#endif
	struct ia_css_resolution vf_pp_in_res;
#ifndef ISP2401
	/** bayer down scaling */
#else
	/** view finder post processing input resolution */
	struct ia_css_resolution output_system_in_res;
	/** For IPU3 only: use output_system_in_res to specify what input resolution
	     will OSYS receive, this resolution is equal to the output resolution of GDC
	     if not determined CSS will set output_system_in_res with main osys output pin resolution
	     All other IPUs may ignore this property */
#endif
	struct ia_css_resolution dvs_crop_out_res;
	/** dvs crop, video only, not in use yet. Use dvs_envelope below. */
	struct ia_css_frame_info output_info[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	/** output of YUV scaling */
	struct ia_css_frame_info vf_output_info[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	/** output of VF YUV scaling */
	struct ia_css_fw_info *acc_extension;
	/** Pipeline extension accelerator */
	struct ia_css_fw_info **acc_stages;
	/** Standalone accelerator stages */
	uint32_t num_acc_stages;
	/** Number of standalone accelerator stages */
	struct ia_css_capture_config default_capture_config;
	/** Default capture config for initial capture pipe configuration. */
	struct ia_css_resolution dvs_envelope; /** temporary */
	enum ia_css_frame_delay dvs_frame_delay;
	/** indicates the DVS loop delay in frame periods */
	int acc_num_execs;
	/** For acceleration pipes only: determine how many times the pipe
	     should be run. Setting this to -1 means it will run until
	     stopped. */
	bool enable_dz;
	/** Disabling digital zoom for a pipeline, if this is set to false,
	     then setting a zoom factor will have no effect.
	     In some use cases this provides better performance. */
	bool enable_dpc;
	/** Disabling "Defect Pixel Correction" for a pipeline, if this is set
	     to false. In some use cases this provides better performance. */
	bool enable_vfpp_bci;
	/** Enabling BCI mode will cause yuv_scale binary to be picked up
	     instead of vf_pp. This only applies to viewfinder post
	     processing stages. */
#ifdef ISP2401
	bool enable_luma_only;
	/** Enabling of monochrome mode for a pipeline. If enabled only luma processing
	     will be done. */
	bool enable_tnr;
	/** Enabling of TNR (temporal noise reduction). This is only applicable to video
	     pipes. Non video-pipes should always set this parameter to false. */
#endif
	struct ia_css_isp_config *p_isp_config;
	/** Pointer to ISP configuration */
	struct ia_css_resolution gdc_in_buffer_res;
	/** GDC in buffer resolution. */
	struct ia_css_point gdc_in_buffer_offset;
	/** GDC in buffer offset - indicates the pixel coordinates of the first valid pixel inside the buffer */
#ifdef ISP2401
	struct ia_css_coordinate internal_frame_origin_bqs_on_sctbl;
	/** Origin of internal frame positioned on shading table at shading correction in ISP.
	     NOTE: Shading table is larger than or equal to internal frame.
		   Shading table has shading gains and internal frame has bayer data.
		   The origin of internal frame is used in shading correction in ISP
		   to retrieve shading gains which correspond to bayer data. */
#endif
};


/**
 * Default settings for newly created pipe configurations.
 */
#define DEFAULT_PIPE_CONFIG \
(struct ia_css_pipe_config) { \
	.mode			= IA_CSS_PIPE_MODE_PREVIEW, \
	.isp_pipe_version	= 1, \
	.output_info		= {IA_CSS_BINARY_DEFAULT_FRAME_INFO}, \
	.vf_output_info		= {IA_CSS_BINARY_DEFAULT_FRAME_INFO}, \
	.default_capture_config	= DEFAULT_CAPTURE_CONFIG, \
	.dvs_frame_delay	= IA_CSS_FRAME_DELAY_1, \
	.acc_num_execs		= -1, \
}

/* Pipe info, this struct describes properties of a pipe after it's stream has
 * been created.
 * ~~~** DO NOT ADD NEW FIELD **~~~ This structure will be deprecated.
 *           - On the Behalf of CSS-API Committee.
 */
struct ia_css_pipe_info {
	struct ia_css_frame_info output_info[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	/** Info about output resolution. This contains the stride which
	     should be used for memory allocation. */
	struct ia_css_frame_info vf_output_info[IA_CSS_PIPE_MAX_OUTPUT_STAGE];
	/** Info about viewfinder output resolution (optional). This contains
	     the stride that should be used for memory allocation. */
	struct ia_css_frame_info raw_output_info;
	/** Raw output resolution. This indicates the resolution of the
	     RAW bayer output for pipes that support this. Currently, only the
	     still capture pipes support this feature. When this resolution is
	     smaller than the input resolution, cropping will be performed by
	     the ISP. The first cropping that will be performed is on the upper
	     left corner where we crop 8 lines and 8 columns to remove the
	     pixels normally used to initialize the ISP filters.
	     This is why the raw output resolution should normally be set to
	     the input resolution - 8x8. */
#ifdef ISP2401
	struct ia_css_resolution output_system_in_res_info;
	/** For IPU3 only. Info about output system in resolution which is considered
	     as gdc out resolution. */
#endif
	struct ia_css_shading_info shading_info;
	/** After an image pipe is created, this field will contain the info
	     for the shading correction. */
	struct ia_css_grid_info  grid_info;
	/** After an image pipe is created, this field will contain the grid
	     info for 3A and DVS. */
	int num_invalid_frames;
	/** The very first frames in a started stream do not contain valid data.
	     In this field, the CSS-firmware communicates to the host-driver how
	     many initial frames will contain invalid data; this allows the
	     host-driver to discard those initial invalid frames and start it's
	     output at the first valid frame. */
};

/**
 * Defaults for ia_css_pipe_info structs.
 */
#define DEFAULT_PIPE_INFO \
(struct ia_css_pipe_info) { \
	.output_info		= {IA_CSS_BINARY_DEFAULT_FRAME_INFO}, \
	.vf_output_info		= {IA_CSS_BINARY_DEFAULT_FRAME_INFO}, \
	.raw_output_info	= IA_CSS_BINARY_DEFAULT_FRAME_INFO, \
	.shading_info		= DEFAULT_SHADING_INFO, \
	.grid_info		= DEFAULT_GRID_INFO, \
}

/* @brief Load default pipe configuration
 * @param[out]	pipe_config The pipe configuration.
 * @return	None
 *
 * This function will load the default pipe configuration:
@code
	struct ia_css_pipe_config def_config = {
		IA_CSS_PIPE_MODE_PREVIEW,  // mode
		1,      // isp_pipe_version
		{0, 0}, // bayer_ds_out_res
		{0, 0}, // capt_pp_in_res
		{0, 0}, // vf_pp_in_res
		{0, 0}, // dvs_crop_out_res
		{{0, 0}, 0, 0, 0, 0}, // output_info
		{{0, 0}, 0, 0, 0, 0}, // second_output_info
		{{0, 0}, 0, 0, 0, 0}, // vf_output_info
		{{0, 0}, 0, 0, 0, 0}, // second_vf_output_info
		NULL,   // acc_extension
		NULL,   // acc_stages
		0,      // num_acc_stages
		{
			IA_CSS_CAPTURE_MODE_RAW, // mode
			false, // enable_xnr
			false  // enable_raw_output
		},      // default_capture_config
		{0, 0}, // dvs_envelope
		1,      // dvs_frame_delay
		-1,     // acc_num_execs
		true,   // enable_dz
		NULL,   // p_isp_config
	};
@endcode
 */
void ia_css_pipe_config_defaults(struct ia_css_pipe_config *pipe_config);

/* @brief Create a pipe
 * @param[in]	config The pipe configuration.
 * @param[out]	pipe The pipe.
 * @return	IA_CSS_SUCCESS or the error code.
 *
 * This function will create a pipe with the given
 * configuration.
 */
enum ia_css_err
ia_css_pipe_create(const struct ia_css_pipe_config *config,
		   struct ia_css_pipe **pipe);

/* @brief Destroy a pipe
 * @param[in]	pipe The pipe.
 * @return	IA_CSS_SUCCESS or the error code.
 *
 * This function will destroy a given pipe.
 */
enum ia_css_err
ia_css_pipe_destroy(struct ia_css_pipe *pipe);

/* @brief Provides information about a pipe
 * @param[in]	pipe The pipe.
 * @param[out]	pipe_info The pipe information.
 * @return	IA_CSS_SUCCESS or IA_CSS_ERR_INVALID_ARGUMENTS.
 *
 * This function will provide information about a given pipe.
 */
enum ia_css_err
ia_css_pipe_get_info(const struct ia_css_pipe *pipe,
		     struct ia_css_pipe_info *pipe_info);

/* @brief Configure a pipe with filter coefficients.
 * @param[in]	pipe	The pipe.
 * @param[in]	config	The pointer to ISP configuration.
 * @return		IA_CSS_SUCCESS or error code upon error.
 *
 * This function configures the filter coefficients for an image
 * pipe.
 */
enum ia_css_err
ia_css_pipe_set_isp_config(struct ia_css_pipe *pipe,
						   struct ia_css_isp_config *config);

/* @brief Controls when the Event generator raises an IRQ to the Host.
 *
 * @param[in]	pipe	The pipe.
 * @param[in]	or_mask	Binary or of enum ia_css_event_irq_mask_type. Each pipe
			related event that is part of this mask will directly
			raise an IRQ to	the Host when the event occurs in the
			CSS.
 * @param[in]	and_mask Binary or of enum ia_css_event_irq_mask_type. An event
			IRQ for the Host is only raised after all pipe related
			events have occurred at least once for all the active
			pipes. Events are remembered and don't need to occure
			at the same moment in time. There is no control over
			the order of these events. Once an IRQ has been raised
			all remembered events are reset.
 * @return		IA_CSS_SUCCESS.
 *
 Controls when the Event generator in the CSS raises an IRQ to the Host.
 The main purpose of this function is to reduce the amount of interrupts
 between the CSS and the Host. This will help saving power as it wakes up the
 Host less often. In case both or_mask and and_mask are
 IA_CSS_EVENT_TYPE_NONE for all pipes, no event IRQ's will be raised. An
 exception holds for IA_CSS_EVENT_TYPE_PORT_EOF, for this event an IRQ is always
 raised.
 Note that events are still queued and the Host can poll for them. The
 or_mask and and_mask may be active at the same time\n
 \n
 Default values, for all pipe id's, after ia_css_init:\n
 or_mask = IA_CSS_EVENT_TYPE_ALL\n
 and_mask = IA_CSS_EVENT_TYPE_NONE\n
 \n
 Examples\n
 \code
 ia_css_pipe_set_irq_mask(h_pipe,
 IA_CSS_EVENT_TYPE_3A_STATISTICS_DONE |
 IA_CSS_EVENT_TYPE_DIS_STATISTICS_DONE ,
 IA_CSS_EVENT_TYPE_NONE);
 \endcode
 The event generator will only raise an interrupt to the Host when there are
 3A or DIS statistics available from the preview pipe. It will not generate
 an interrupt for any other event of the preview pipe e.g when there is an
 output frame available.

 \code
 ia_css_pipe_set_irq_mask(h_pipe_preview,
	IA_CSS_EVENT_TYPE_NONE,
	IA_CSS_EVENT_TYPE_OUTPUT_FRAME_DONE |
	IA_CSS_EVENT_TYPE_3A_STATISTICS_DONE );

 ia_css_pipe_set_irq_mask(h_pipe_capture,
	IA_CSS_EVENT_TYPE_NONE,
	IA_CSS_EVENT_TYPE_OUTPUT_FRAME_DONE );
 \endcode
 The event generator will only raise an interrupt to the Host when there is
 both a frame done and 3A event available from the preview pipe AND when there
 is a frame done available from the capture pipe. Note that these events
 may occur at different moments in time. Also the order of the events is not
 relevant.

 \code
 ia_css_pipe_set_irq_mask(h_pipe_preview,
	IA_CSS_EVENT_TYPE_OUTPUT_FRAME_DONE,
	IA_CSS_EVENT_TYPE_ALL );

 ia_css_pipe_set_irq_mask(h_pipe_capture,
	IA_CSS_EVENT_TYPE_OUTPUT_FRAME_DONE,
	IA_CSS_EVENT_TYPE_ALL );
 \endcode
 The event generator will only raise an interrupt to the Host when there is an
 output frame from the preview pipe OR an output frame from the capture pipe.
 All other events (3A, VF output, pipeline done) will not raise an interrupt
 to the Host. These events are not lost but always stored in the event queue.
 */
enum ia_css_err
ia_css_pipe_set_irq_mask(struct ia_css_pipe *pipe,
			 unsigned int or_mask,
			 unsigned int and_mask);

/* @brief Reads the current event IRQ mask from the CSS.
 *
 * @param[in]	pipe The pipe.
 * @param[out]	or_mask	Current or_mask. The bits in this mask are a binary or
		of enum ia_css_event_irq_mask_type. Pointer may be NULL.
 * @param[out]	and_mask Current and_mask.The bits in this mask are a binary or
		of enum ia_css_event_irq_mask_type. Pointer may be NULL.
 * @return	IA_CSS_SUCCESS.
 *
 Reads the current event IRQ mask from the CSS. Reading returns the actual
 values as used by the SP and not any mirrored values stored at the Host.\n
\n
Precondition:\n
SP must be running.\n

*/
enum ia_css_err
ia_css_event_get_irq_mask(const struct ia_css_pipe *pipe,
			  unsigned int *or_mask,
			  unsigned int *and_mask);

/* @brief Queue a buffer for an image pipe.
 *
 * @param[in] pipe	The pipe that will own the buffer.
 * @param[in] buffer	Pointer to the buffer.
 *			Note that the caller remains owner of the buffer
 *			structure. Only the data pointer within it will
 *			be passed into the internal queues.
 * @return		IA_CSS_INTERNAL_ERROR in case of unexpected errors,
 *			IA_CSS_SUCCESS otherwise.
 *
 * This function adds a buffer (which has a certain buffer type) to the queue
 * for this type. This queue is owned by the image pipe. After this function
 * completes successfully, the buffer is now owned by the image pipe and should
 * no longer be accessed by any other code until it gets dequeued. The image
 * pipe will dequeue buffers from this queue, use them and return them to the
 * host code via an interrupt. Buffers will be consumed in the same order they
 * get queued, but may be returned to the host out of order.
 */
enum ia_css_err
ia_css_pipe_enqueue_buffer(struct ia_css_pipe *pipe,
			   const struct ia_css_buffer *buffer);

/* @brief Dequeue a buffer from an image pipe.
 *
 * @param[in]    pipe	 The pipeline that the buffer queue belongs to.
 * @param[in,out] buffer The buffer is used to lookup the type which determines
 *			 which internal queue to use.
 *			 The resulting buffer pointer is written into the dta
 *			 field.
 * @return		 IA_CSS_ERR_NO_BUFFER if the queue is empty or
 *			 IA_CSS_SUCCESS otherwise.
 *
 * This function dequeues a buffer from a buffer queue. The queue is indicated
 * by the buffer type argument. This function can be called after an interrupt
 * has been generated that signalled that a new buffer was available and can
 * be used in a polling-like situation where the NO_BUFFER return value is used
 * to determine whether a buffer was available or not.
 */
enum ia_css_err
ia_css_pipe_dequeue_buffer(struct ia_css_pipe *pipe,
			   struct ia_css_buffer *buffer);


/* @brief  Set the state (Enable or Disable) of the Extension stage in the
 *          given pipe.
 * @param[in] pipe         Pipe handle.
 * @param[in] fw_handle    Extension firmware Handle (ia_css_fw_info.handle)
 * @param[in] enable       Enable Flag (1 to enable ; 0 to disable)
 *
 * @return
 * IA_CSS_SUCCESS 			: Success
 * IA_CSS_ERR_INVALID_ARGUMENTS		: Invalid Parameters
 * IA_CSS_ERR_RESOURCE_NOT_AVAILABLE	: Inactive QOS Pipe
 * 					(No active stream with this pipe)
 *
 * This function will request state change (enable or disable) for the Extension
 * stage (firmware handle) in the given pipe.
 *
 * Note:
 * 	1. Extension can be enabled/disabled only on QOS Extensions
 * 	2. Extension can be enabled/disabled only with an active QOS Pipe
 * 	3. Initial(Default) state of QOS Extensions is Disabled
 * 	4. State change cannot be guaranteed immediately OR on frame boundary
 *
 */
enum ia_css_err
ia_css_pipe_set_qos_ext_state (struct ia_css_pipe *pipe,
                           uint32_t fw_handle,
                           bool  enable);

/* @brief  Get the state (Enable or Disable) of the Extension stage in the
 *          given pipe.
 * @param[in]  pipe        Pipe handle.
 * @param[in]  fw_handle   Extension firmware Handle (ia_css_fw_info.handle)
 * @param[out] *enable     Enable Flag
 *
 * @return
 * IA_CSS_SUCCESS 			: Success
 * IA_CSS_ERR_INVALID_ARGUMENTS		: Invalid Parameters
 * IA_CSS_ERR_RESOURCE_NOT_AVAILABLE	: Inactive QOS Pipe
 * 					(No active stream with this pipe)
 *
 * This function will query the state of the Extension stage (firmware handle)
 * in the given Pipe.
 *
 * Note:
 * 	1. Extension state can be queried only on QOS Extensions
 * 	2. Extension can be enabled/disabled only with an active QOS Pipe
 * 	3. Initial(Default) state of QOS Extensions is Disabled.
 *
 */
enum ia_css_err
ia_css_pipe_get_qos_ext_state (struct ia_css_pipe *pipe,
                           uint32_t fw_handle,
                           bool * enable);

#ifdef ISP2401
/* @brief  Update mapped CSS and ISP arguments for QoS pipe during SP runtime.
 * @param[in] pipe     	Pipe handle.
 * @param[in] fw_handle	Extension firmware Handle (ia_css_fw_info.handle).
 * @param[in] css_seg  	Parameter memory descriptors for CSS segments.
 * @param[in] isp_seg  	Parameter memory descriptors for ISP segments.
 *
 * @return
 * IA_CSS_SUCCESS 			: Success
 * IA_CSS_ERR_INVALID_ARGUMENTS		: Invalid Parameters
 * IA_CSS_ERR_RESOURCE_NOT_AVAILABLE	: Inactive QOS Pipe
 * 					(No active stream with this pipe)
 *
 * \deprecated{This interface is used to temporarily support a late-developed,
 * specific use-case on a specific IPU2 platform. It will not be supported or
 * maintained on IPU3 or further.}
 */
enum ia_css_err
ia_css_pipe_update_qos_ext_mapped_arg(struct ia_css_pipe *pipe, uint32_t fw_handle,
			struct ia_css_isp_param_css_segments *css_seg,
			struct ia_css_isp_param_isp_segments *isp_seg);

#endif
/* @brief Get selected configuration settings
 * @param[in]	pipe	The pipe.
 * @param[out]	config	Configuration settings.
 * @return		None
 */
void
ia_css_pipe_get_isp_config(struct ia_css_pipe *pipe,
			     struct ia_css_isp_config *config);

/* @brief Set the scaler lut on this pipe. A copy of lut is made in the inuit
 *         address space. So the LUT can be freed by caller.
 * @param[in]  pipe        Pipe handle.
 * @param[in]  lut         Look up tabel
 *
 * @return
 * IA_CSS_SUCCESS 			: Success
 * IA_CSS_ERR_INVALID_ARGUMENTS		: Invalid Parameters
 *
 * Note:
 * 1) Note that both GDC's are programmed with the same table.
 * 2) Current implementation ignores the pipe and overrides the
 *    global lut. This will be fixed in the future
 * 3) This function must be called before stream start
 *
 */
enum ia_css_err
ia_css_pipe_set_bci_scaler_lut( struct ia_css_pipe *pipe,
				const void *lut);
/* @brief Checking of DVS statistics ability
 * @param[in]	pipe_info	The pipe info.
 * @return		true - has DVS statistics ability
 * 			false - otherwise
 */
bool ia_css_pipe_has_dvs_stats(struct ia_css_pipe_info *pipe_info);

#ifdef ISP2401
/* @brief Override the frameformat set on the output pins.
 * @param[in]  pipe        Pipe handle.
 * @param[in]  output_pin  Pin index to set the format on
 *                         0 - main output pin
 *                         1 - display output pin
 * @param[in]  format      Format to set
 *
 * @return
 * IA_CSS_SUCCESS		: Success
 * IA_CSS_ERR_INVALID_ARGUMENTS	: Invalid Parameters
 * IA_CSS_ERR_INTERNAL_ERROR	: Pipe misses binary info
 *
 * Note:
 * 1) This is an optional function to override the formats set in the pipe.
 * 2) Only overriding with IA_CSS_FRAME_FORMAT_NV12_TILEY is currently allowed.
 * 3) This function is only to be used on pipes that use the output system.
 * 4) If this function is used, it MUST be called after ia_css_pipe_create.
 * 5) If this function is used, this function MUST be called before ia_css_stream_start.
 */
enum ia_css_err
ia_css_pipe_override_frame_format(struct ia_css_pipe *pipe,
				int output_pin,
				enum ia_css_frame_format format);

#endif
#endif /* __IA_CSS_PIPE_PUBLIC_H */
