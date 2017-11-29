/*
#ifndef ISP2401
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
#else
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.
#endif

#ifdef ISP2401
This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif
#ifndef __CSS_API_VERSION_H
#define __CSS_API_VERSION_H

/* @file
 * CSS API version file. This file contains the version number of the CSS-API.
 *
 * This file is generated from a set of input files describing the CSS-API
 * changes. Don't edit this file directly.
 */


/**

The version string has four dot-separated numbers, read left to right:
  The first two are the API version, and should not be changed.
  The third number is incremented by a CSS firmware developer when the
    API change is not backwards compatible.
  The fourth number is incremented by the a CSS firmware developer for
    every API change.
    It should be zeroed when the third number changes.

*/

#ifndef ISP2401
#define CSS_API_VERSION_STRING	"2.1.15.3"
#else
#define CSS_API_VERSION_STRING	"2.1.20.9"
#endif

/*
Change log

v2.0.1.0, initial version:
- added API versioning

v2.0.1.1, activate CSS-API versioning:
- added description of major and minor version numbers

v2.0.1.2, modified struct ia_css_frame_info:
- added new member ia_css_crop_info

v2.0.1.3, added IA_CSS_ERR_NOT_SUPPORTED

v2.1.0.0
- moved version number to 2.1.0.0
- created new files for refactoring the code

v2.1.1.0, modified struct ia_css_pipe_config and struct ia_css_pipe_info and struct ia_css_pipe:
- use array to handle multiple output ports

v2.1.1.1
- added api to lock/unlock of RAW Buffers to Support HALv3 Feature

v2.1.1.2, modified struct ia_css_stream_config:
- to support multiple isys streams in one virtual channel, keep the old one for backward compatibility

v2.1.2.0, modify ia_css_stream_config:
- add isys_config and input_config to support multiple isys stream within one virtual channel

v2.1.2.1, add IA_CSS_STREAM_FORMAT_NUM
- add IA_CSS_STREAM_FORMAT_NUM definition to reflect the number of ia_css_stream_format enums

v2.1.2.2, modified enum ia_css_stream_format
- Add 16bit YUV formats to ia_css_stream_format enum:
- IA_CSS_STREAM_FORMAT_YUV420_16 (directly after IA_CSS_STREAM_FORMAT_YUV420_10)
- IA_CSS_STREAM_FORMAT_YUV422_16 (directly after IA_CSS_STREAM_FORMAT_YUV422_10)

v2.1.2.3
- added api to enable/disable digital zoom for capture pipe.

v2.1.2.4, change CSS API to generate the shading table which should be directly sent to ISP:
- keep the old CSS API (which uses the conversion of the shading table in CSS) for backward compatibility

v2.1.2.5
- Added SP frame time measurement (in ticks) and result is sent on a new member
- in ia_css_buffer.h.

v2.1.2.6, add function ia_css_check_firmware_version()
- the function ia_css_check_firmware_version() returns true when the firmware version matches and returns false otherwise.

v2.1.2.7
- rename dynamic_data_index to dynamic_queue_id in struct ia_css_frame.
- update IA_CSS_PIPE_MODE_NUM

v2.1.2.8
- added flag for video full range

v2.1.2.9
- add public parameters for xnr3 kernel

v2.1.2.10
- add new interface to enable output mirroring

v2.1.2.11, MIPI buffers optimization
- modified struct ia_css_mipi_buffer_config, added number of MIPI buffers needed for the stream
- backwards compatible, need another patch to remove legacy function and code

v2.1.2.12
- create consolidated  firmware package for 2400, 2401, csi2p, bxtpoc

v2.1.3.0
- rename ia_css_output_config.enable_mirror
- add new interface to enable vertical output flipping

v2.1.3.1
- deprecated ia_css_rx_get_irq_info and ia_css_rx_clear_irq_info because both are hardcoded to work on CSI port 1.
- added new functions ia_css_rx_port_get_irq_info and ia_css_rx_port_clear_irq_info, both have a port ID as extra argument.

v2.1.3.2
- reverted v2.1.3.0 change

v2.1.3.3
- Added isys event queue.
- Renamed ia_css_dequeue_event to ia_css_dequeue_psys_event
- Made ia_css_dequeue_event deprecated

v2.1.3.4
- added new interface to support ACC extension QoS feature.
- added IA_CSS_EVENT_TYPE_ACC_STAGE_COMPLETE.

v2.1.3.5
- added tiled frame format IA_CSS_FRAME_FORMAT_NV12_TILEY

v2.1.3.6
- added functions ia_css_host_data_allocate and ia_css_host_data_free

v2.1.4.0, default pipe config change
- disable enable_dz param by default

v2.1.5.0
- removed mix_range field from yuvp1_y_ee_nr_frng_public_config

v2.1.5.1, exposure IDs per stream
- added MIN/MAX exposure ID macros
- made exposure ID sequence per-stream instead of global (across all streams)

#ifdef ISP2401
v2.1.5.1, Add parameters to mmgr routines via a macro.
- Replaced mmgr funtions with macros to add caller func name + line #.
- This is done to help debug memory access issues, allocation issues, etc.

#endif
v2.1.6.0, Interface for vertical output flip
- add new interface to enable vertical output flipping
- rename ia_css_output_config.enable_mirror

#ifndef ISP2401
v2.1.6.1, Effective res on pipe
#else
v2.1.6.2 (2 changes parallel), Effective res on pipe
#endif
- Added input_effective_res to struct ia_css_pipe_config in ia_css_pipe_public.h.

#ifndef ISP2401
v2.1.6.2, CSS-API version file generated from individual changes
#else
v2.1.6.3 (2 changes parallel), CSS-API version file generated from individual changes
#endif
- Avoid merge-conflicts by generating version file from individual CSS-API changes.
- Parallel CSS-API changes can map to the same version number after this change.
- Version numbers for a change could increase due to parallel changes being merged.
- The version number would not decrease for a change.

#ifndef ISP2401
v2.1.6.5 (2 changes parallel), Add SP FW error event
#else
v2.1.6.6 (4 changes parallel), Add SP FW error event
#endif
- Added FW error event. This gets raised when the SP FW runs into an
- error situation from which it cannot recover.

#ifndef ISP2401
v2.1.6.5 (2 changes parallel), expose bnr FF enable bits in bnr public API
#else
v2.1.6.6 (4 changes parallel), expose bnr FF enable bits in bnr public API
#endif
- Added ff enable bits to bnr_public_config_dn_detect_ctrl_config_t struct

#ifndef ISP2401
v2.1.6.5 (2 changes parallel), ISP configuration per pipe 
#else
v2.1.6.6 (4 changes parallel), ISP configuration per pipe 
#endif
- Added ISP configuration per pipe support: p_isp_config field in
- struct ia_css_pipe_config and ia_css_pipe_set_isp_config_on_pipe
- and ia_css_pipe_set_isp_config functions

#ifndef ISP2401
v2.1.7.0, removed css_version.h
#else
v2.1.7.0 (2 changes parallel), removed css_version.h
#endif
- Removed css_version.h that was used for versioning in manual (non-CI) releases.

#ifndef ISP2401
v2.1.7.1, Add helpers (get and set) for ISP cfg per pipe
#else
v2.1.7.2 (2 changes parallel), Add helpers (get and set) for ISP cfg per pipe
#endif
- Add helpers (get and set) for ISP configuration per pipe

#ifndef ISP2401
v2.1.7.2, Add feature to lock all RAW buffers
#else
v2.1.7.3 (2 changes parallel), Add feature to lock all RAW buffers
#endif
- This API change adds a boolean flag (lock_all) in the stream_config struct.
- If this flag is set to true, then all frames will be locked if locking is
- enabled. By default this flag is set to false.
- When this flag is false, then only buffers that are sent to the preview pipe
- will be locked. If continuous viewfinder is disabled, the flag should be set
- to true.

#ifndef ISP2401
v2.1.8.0 (2 changes parallel), Various changes to support ACC configuration per pipe
#else
v2.1.8.0 (4 changes parallel), Various changes to support ACC configuration per pipe
#endif
- Add ia_css_pipe_get_isp_config()
- Remove ia_css_pipe_set_isp_config_on_pipe (duplicated
- by ia_css_pipe_set_isp_config)
- Add isp configuration as parameter for
- ia_css_pipe_set_isp_config
- Remove ia_css_pipe_isp_config_set()
- Remove ia_css_pipe_isp_config_get()

#ifndef ISP2401
v2.1.8.2 (2 changes parallel), Added member num_invalid_frames to ia_css_pipe_info structure.
#else
v2.1.8.3 (4 changes parallel), Added member num_invalid_frames to ia_css_pipe_info structure.
#endif
- Added member num_invalid_frames to ia_css_pipe_info structure.
- This helps the driver make sure that the first valid output
- frame goes into the first user-supplied output buffer.

#ifndef ISP2401
v2.1.8.4 (2 changes parallel), ISYS EOF timestamp for output buffers
#else
v2.1.8.5 (4 changes parallel), ISYS EOF timestamp for output buffers
#endif
- driver gets EOF timer to every out frame . ia_css_buffer modified to accomodate same.

#ifndef ISP2401
v2.1.8.4 (4 changes parallel), display_config
#else
v2.1.8.5 (6 changes parallel), display_config
#endif
- Added formats- and output config parameters for configuration of the (optional) display output.

#ifndef ISP2401
v2.1.8.4 (2 changes parallel), Adding zoom region parameters to CSS API
#else
v2.1.8.5 (4 changes parallel), Adding zoom region parameters to CSS API
#endif
- Adding ia_css_point and ia_css_region structures to css-api.
- Adding zoom_region(type ia_css_region) parameter to ia_css_dz_config structure.
- By using this user can do the zoom based on zoom region and
- the center of the zoom region is not restricted at the center of the input frame.

#ifndef ISP2401
v2.1.8.6 (1 changes parallel), Add new ia_css_fw_warning type
#else
v2.1.8.7 (3 changes parallel), Add new ia_css_fw_warning type
#endif
- Add IA_CSS_FW_WARNING_TAG_EXP_ID_FAILED enum to ia_css_fw_warning type
- Extend sp_warning() with exp_id parameter

#ifndef ISP2401
v2.1.8.6 (1 changes parallel), Add includes in GC, GC2 kernel interface files
#else
v2.1.8.7 (3 changes parallel), Add includes in GC, GC2 kernel interface files
#endif
- add ia_css_ctc_types.h includes in ia_css_gc_types.h and ia_css_gc2_types.h. Needed to get ia_css_vamem_type.

#ifndef ISP2401
v2.1.9.0 (1 changes parallel), Introduce sp assert event.
#else
v2.1.9.0 (3 changes parallel), Introduce sp assert event.
#endif
- Add IA_CSS_EVENT_TYPE_FW_ASSERT. The FW sends the event in case an assert goes off.

#ifndef ISP2401
v2.1.9.1 (1 changes parallel), Exclude driver part from ia_css_buffer.h as it is also used by SP
#else
v2.1.9.2 (3 changes parallel), Exclude driver part from ia_css_buffer.h as it is also used by SP
#endif
- Excluded driver part of the interface from SP/ISP code
- Driver I/F is not affected

#ifndef ISP2401
v2.1.9.2, added IA_CSS_EVENT_TYPE_TIMER
#else
v2.1.9.3 (2 changes parallel), added IA_CSS_EVENT_TYPE_TIMER
#endif
- Added a new event called IA_CSS_EVENT_TYPE_TIMER

#ifndef ISP2401
v2.1.10.0 (4 changes parallel), Add a flag "enable_dpc" to "struct ia_css_pipe_config"
#else
v2.1.10.0 (6 changes parallel), Add a flag "enable_dpc" to "struct ia_css_pipe_config"
#endif
- Add a flag "enable_dpc" to "struct ia_css_pipe_config"

#ifndef ISP2401
v2.1.10.6 (6 changes parallel), change the pipe version type from integer to enum
#else
v2.1.10.8 (9 changes parallel), change the pipe version type from integer to enum
#endif
- add new enum to enumerate ISP pipe version
- change the pipe version type in pipe_config from integer to enum

#ifndef ISP2401
v2.1.13.0 (8 changes parallel), Stop Support for Skycam B0
#else
v2.1.14.0 (12 changes parallel), Stop Support for Skycam B0
#endif
- Remove a few pre-processor defines for Skycam B0/C0 as support

#ifndef ISP2401
v2.1.14.0 (24 changes parallel), change the pipe version type from integer to enum
#else
v2.1.15.0 (28 changes parallel), change the pipe version type from integer to enum
#endif
- remove the temporary workaround for backward compatability

#ifndef ISP2401
v2.1.14.0 (13 changes parallel), expose_gamma_enable_option
#else
v2.1.15.0 (17 changes parallel), expose_gamma_enable_option
#endif
- added enable param to gamma_corr_public_config
- added documentation to rgbpp_public.h

#ifndef ISP2401
v2.1.14.0 (12 changes parallel), Remove deprecated FW_ERROR event.
#else
v2.1.15.0 (16 changes parallel), Remove deprecated FW_ERROR event.
#endif
- Remove code for deprecated FW_ERROR event.

#ifndef ISP2401
v2.1.14.3 (5 changes parallel), fix IEFD's puclic API types
#else
v2.1.15.5 (8 changes parallel), fix IEFD's puclic API types
#endif
- fix IEFD public API members types: rad_cu6_x1,rad_cu_unsharp_x1 & unsharp_amount

#ifndef ISP2401
v2.1.14.3 (5 changes parallel), Add IA_CSS_FW_WARNING_FRAME_PARAM_MISMATCH
#else
v2.1.15.5 (8 changes parallel), Add IA_CSS_FW_WARNING_FRAME_PARAM_MISMATCH
#endif
- Add IA_CSS_FW_WARNING_FRAME_PARAM_MISMATCH enum to ia_css_fw_warning type

#ifndef ISP2401
v2.1.14.4 (5 changes parallel), new API getter functions for gdc in buffer information
#else
v2.1.15.8 (11 changes parallel), add_flag_to_disable_continous_viewfinder
- add a new flag in stream_config to disable continuous viewfinder
- in ZSL use case.

v2.1.16.0 (8 changes parallel), revert ia_css_skc_dvs_statistics field size change 
- Reverted field size change, change was not ready for driver yet.

v2.1.17.0 (7 changes parallel), change CSS API to fix the shading correction off-center issue
- update the ia_css_shading_info structure in ia_css_types.h

v2.1.17.0 (32 changes parallel), add_flag_to_disable_continous_viewfinder_part2
- remove the old interfaces

v2.1.17.4 (8 changes parallel), Added public interface for setting the scaler LUT.
- Added the public struct to output system and modified the global config struct.

v2.1.17.5 (7 changes parallel), Add parameters for new TNR3 component
- Add new parameters for new TNR3 component

v2.1.17.6 (9 changes parallel), Update skycam DPC_MAX_NUMBER_OF_DP
- Automated tooling requires an API change request
- This change changes the implementation of #define DPC_MAX_NUMBER_OF_DP
- it now returns a different number

v2.1.17.6 (8 changes parallel), Return an error when both DPC and BDS are enabled in a pipe config
- Return an error when both DPC and BDS are enabled in a pipe config

v2.1.17.6 (9 changes parallel), new API getter functions for gdc in buffer information
#endif
- ia_css_pipe_get_dvs_filter() added
- ia_css_pipe_get_gdc_in_buffer_info() added

#ifndef ISP2401
v2.1.14.5 (8 changes parallel), Update CNR2 ineffective values
#else
v2.1.17.7 (12 changes parallel), Update CNR2 ineffective values
#endif
- Fixed Incorrect ineffective values listed in ia_css_cnr_config
- Correct Ineffective value is 8191

#ifndef ISP2401
v2.1.14.5 (8 changes parallel), af_roi_api
#else
v2.1.17.7 (12 changes parallel), af_roi_api
#endif
- added a new function to set AF ROI ia_css_set_af_roi
- added a new struct ia_css_s3a_roi_offset

#ifndef ISP2401
v2.1.14.5 (8 changes parallel), remove x_y_end_from_ae_and_awb
#else
v2.1.17.7 (12 changes parallel), Enlarge AF AWB_FR stats buffers
- Enlarge AF and AWB_FR stats buffers to support max grid width per stripe as oppose to per frame

v2.1.17.7 (12 changes parallel), remove x_y_end_from_ae_and_awb
#endif
- added a flag to prepare removal of x_end and y_end from ae grid public config
- added a flag to prepare removal of x_end and y_end from awb grid public config

#ifndef ISP2401
v2.1.14.5 (4 changes parallel), Added public interface for setting the scaler LUT.
- Added the public struct to output system and modified the global config struct.
#else
v2.1.17.8 (5 changes parallel)
- added input_yuv , input_raw to ia_css_binary_info.enable 
- struct, these attributes were always there but not saved
- in the binary_info struct
#endif

#ifndef ISP2401
v2.1.14.6 (8 changes parallel), add_flag_to_disable_continous_viewfinder
- add a new flag in stream_config to disable continuous viewfinder
- in ZSL use case.
#else
v2.1.17.9 (6 changes parallel), cleanup_awb_ae_rgb_integration_flags
- this change only cleans up an approved api CR see wikis below
#endif

#ifndef ISP2401
v2.1.14.6 (8 changes parallel), Enlarge AF AWB_FR stats buffers
- Enlarge AF and AWB_FR stats buffers to support max grid width per stripe as oppose to per frame
#else
v2.1.17.10 (6 changes parallel), output_system_input_resolution
- adedd gdc_output_system_in_resolution to pipe config struct
#endif

#ifndef ISP2401
v2.1.14.8 (6 changes parallel), pipe config option for vf output bci mode downscaling
#else
v2.1.17.10 (5 changes parallel), Per pipe DPC configuration is added to ia_css_isp_parameters
- Per pipe DPC configuration is added to ia_css_isp_parameters 

v2.1.17.10 (10 changes parallel), pipe config option for vf output bci mode downscaling
#endif
- vf downscaling using yuv_scale binary.

#ifndef ISP2401
v2.1.14.10 (7 changes parallel), Add scale mode GDC V2 LUT to CSS API
#else
v2.1.17.12 (11 changes parallel), Add scale mode GDC V2 LUT to CSS API
#endif
- Allow client to set global LUT for gdc v2 (First step in this change. See wiki page for more details)

#ifndef ISP2401
v2.1.14.10 (8 changes parallel), Include added to type-support.h.
#else
v2.1.17.12 (12 changes parallel), Include added to type-support.h.
#endif
- Include of hive/cell_support.h was added to type-support.h, in order to
- have access to define HAVE_STDINT.

#ifndef ISP2401
v2.1.14.11 (7 changes parallel), Pipe configuration to enable BLI mode downscaling for
#else
v2.1.17.13 (11 changes parallel), Pipe configuration to enable BLI mode downscaling for
#endif
- BLI mode downscaling for capture post-processing

#ifndef ISP2401
v2.1.14.14 (9 changes parallel), Fix copyright headers (no functional change)
#else
v2.1.17.15 (8 changes parallel), Add copyright headers to css files
- Add copyright headers to css API files

v2.1.17.15 (8 changes parallel), add copyright header to include files
- add copyright header to include files

v2.1.17.15 (8 changes parallel), add copyright header to isp files
- add copyright header to isp files

v2.1.17.15 (8 changes parallel), add copyright header to refactored code
- add copyright header to refactored code
- (base, camera, runtime directories)

v2.1.17.16 (13 changes parallel), Fix copyright headers (no functional change)
#endif
- No functional change; only fixes copyright headers

#ifndef ISP2401
v2.1.14.14 (6 changes parallel), Remove continuous mode special case handling in ia_css_pipe_set_isp_config
#else
v2.1.17.16 (10 changes parallel), Remove continuous mode special case handling in ia_css_pipe_set_isp_config
#endif
- For continuous mode isp_config was being send to all pipes,
- even though API ia_css_pipe_set_isp_config is for single pipe
- Removed incorrect case

#ifndef ISP2401
v2.1.14.14 (6 changes parallel), DVS statistics grid produced by accelerator
#else
v2.1.17.16 (5 changes parallel), Added documentation to formats_config header file
- Added description of ranges for full-range flag

v2.1.17.16 (10 changes parallel), DVS statistics grid produced by accelerator
#endif
- Add DVS statistics produced by accelerator grid to pipe info
- Add ia_css_pipe_has_dvs_stats function

#ifndef ISP2401
v2.1.14.15 (7 changes parallel), cont_remove_x_y_end_from_ae_and_awb
#else
v2.1.17.17 (5 changes parallel), Provide the CSS interface to select the luma only binaries
- Add a flag "enable_luma_only" to "struct ia_css_pipe_config"

v2.1.17.17 (11 changes parallel), cont_remove_x_y_end_from_ae_and_awb
#endif
- this patch doesn't introduce any new api change, it only fixes a recent
- api merged change (#31938) , in order to have success CI i had to upload an api change request

#ifndef ISP2401
v2.1.14.17 (6 changes parallel), Add XNR3 blending strength to kernel interface
- Added a blending strength field to the XNR3 kernel interface to add
- support for blending.
#else
v2.1.17.17 (10 changes parallel), GAC state dump for debug
- added ia_css_dump_gac_state function

v2.1.17.18 (23 changes parallel), output_format_nv12_16
- added new output fromat nv12_16
#endif

#ifndef ISP2401
v2.1.14.18 (22 changes parallel), eliminate two_pixels_per_clock field
#else
v2.1.17.18 (4 changes parallel), Remove author details from SKC src code
- remove author details from skc src code

v2.1.17.19 (26 changes parallel), eliminate two_pixels_per_clock field
#endif
- remove obsolete field two_pixels_per_clock

#ifndef ISP2401
v2.1.14.19 (3 changes parallel), Fix copyright headers (no functional change)
#else
v2.1.17.20 (7 changes parallel), Fix copyright headers (no functional change)
#endif
- No functional change; only fixes copyright headers

#ifndef ISP2401
v2.1.14.21 (3 changes parallel), ia_css_skc_dvs_statistics field size change
- ia_css_skc_dvs_statistics field size change
#else
v2.1.17.20 (11 changes parallel), Add XNR3 blending strength to kernel interface
- Added a blending strength field to the XNR3 kernel interface to add
- support for blending.
#endif

#ifndef ISP2401
v2.1.15.0 (3 changes parallel), revert ia_css_skc_dvs_statistics field size change 
- Reverted field size change, change was not ready for driver yet.
#else
v2.1.17.21 (24 changes parallel), Add N_CSS_PRBS_IDS and N_CSS_TPG_IDS
- Add N_CSS_PRBS_IDS to reflect the number of ia_css_prbs_id enum
- Add N_CSS_TPG_IDS to reflect the number of ia_css_tpg_id enum
#endif

#ifndef ISP2401
v2.1.15.2 (3 changes parallel), Return an error when both DPC and BDS are enabled in a pipe config
- Return an error when both DPC and BDS are enabled in a pipe config
#else
v2.1.17.23 (8 changes parallel), ia_css_skc_dvs_statistics field size change
- ia_css_skc_dvs_statistics field size change
#endif

#ifndef ISP2401
v2.1.15.3 (2 changes parallel), Update skycam DPC_MAX_NUMBER_OF_DP
- Automated tooling requires an API change request
- This change changes the implementation of #define DPC_MAX_NUMBER_OF_DP
- it now returns a different number
#else
v2.1.19.0 (6 changes parallel)
- Added code to calculate input_res using the Windows specification of binning
#endif

#ifndef ISP2401
v2.1.15.3 (18 changes parallel), output_format_nv12_16
- added new output fromat nv12_16
#else
v2.1.20.0 (7 changes parallel), Add interface to select TNR enabled binaries
- Add a bool "enable_tnr" to "struct ia_css_pipe_config"

v2.1.20.0 (6 changes parallel), OSYS & GDC Debug dump function addition
- add GDC state dump function
- add OSYS state dump function

v2.1.20.4 (7 changes parallel), Add ref_buf_select parameter for TNR3 to kernel interface
- Added a ref_buf_select parameter to the TNR3 kernel interface to add
- support for multiple reference buffers.

v2.1.20.4 (6 changes parallel), DVS MAX grid dimensions to cover maximal resolution
- rename DVS_TABLE_HEIGHT/WIDTH to MAX_DVS_COORDS_TABLE_HEIGHT/WIDTH
- modify value of the above macros to cover max resolution

v2.1.20.5 (54 changes parallel), add input feeder calculations getter
- add input_feeder_config public struct
- add get_input_feeder_config getter

v2.1.20.5 (4 changes parallel), Enable runtime updating mapped args for QoS extension pipe
- added ia_css_pipe_update_qos_ext_mapped_arg()

v2.1.20.7 (77 changes parallel), Add parameters to CPU routines via a macro.
- Replaced CPU memory allocation functions with macros to add caller func name + line number.
- This is done to help debug memory access issues, allocation issues, etc.
- Changed API: only ia_css_env.h

v2.1.20.7 (2 changes parallel), Frame format override
- Added a function call to the pipe interface for overriding
- the frame format as set in the pipe.
- This is an optional interface that can be used under
- some strict conditions.

v2.1.20.7 (2 changes parallel), Output_system_in_res Information
- Output_system_in_res_info field added to pipe_info struct 

v2.1.20.8, Temprarily disable memory debug features for SVOS.
- Temporary commented out the additions to allow SKC testing till root cause found
- Changed files ia_css_env.h and sh_css.c.

v2.1.20.9, Enable ISP 2.7 naming
- Add IA_CSS_PIPE_VERSION_2_7 to enum ia_css_pipe_version
- Add #define SH_CSS_ISP_PIPE_VERSION_2_7 4
#endif

*/

#endif /*__CSS_API_VERSION_H*/
