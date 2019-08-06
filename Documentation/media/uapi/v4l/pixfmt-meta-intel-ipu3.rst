.. -*- coding: utf-8; mode: rst -*-

.. _v4l2-meta-fmt-params:
.. _v4l2-meta-fmt-stat-3a:

******************************************************************
V4L2_META_FMT_IPU3_PARAMS ('ip3p'), V4L2_META_FMT_IPU3_3A ('ip3s')
******************************************************************

.. c:type:: ipu3_uapi_stats_3a

3A statistics
=============

For IPU3 ImgU, the 3A statistics accelerators collect different statistics over
an input bayer frame. Those statistics, defined in data struct :c:type:`ipu3_uapi_stats_3a`,
are obtained from "ipu3-imgu 3a stat" metadata capture video node, which are then
passed to user space for statistics analysis using :c:type:`v4l2_meta_format` interface.

The statistics collected are AWB (Auto-white balance) RGBS (Red, Green, Blue and
Saturation measure) cells, AWB filter response, AF (Auto-focus) filter response,
and AE (Auto-exposure) histogram.

struct :c:type:`ipu3_uapi_4a_config` saves configurable parameters for all above.

.. code-block:: c

	struct ipu3_uapi_stats_3a {
		struct ipu3_uapi_awb_raw_buffer awb_raw_buffer;
		struct ipu3_uapi_ae_raw_buffer_aligned ae_raw_buffer[IPU3_UAPI_MAX_STRIPES];
		struct ipu3_uapi_af_raw_buffer af_raw_buffer;
		struct ipu3_uapi_awb_fr_raw_buffer awb_fr_raw_buffer;
		struct ipu3_uapi_4a_config stats_4a_config;
		__u32 ae_join_buffers;
		__u8 padding[28];
		struct ipu3_uapi_stats_3a_bubble_info_per_stripe stats_3a_bubble_per_stripe;
		struct ipu3_uapi_ff_status stats_3a_status;
	};

.. c:type:: ipu3_uapi_params

Pipeline parameters
===================

IPU3 pipeline has a number of image processing stages, each of which takes a
set of parameters as input. The major stages of pipelines are shown here:

Raw pixels -> Bayer Downscaling -> Optical Black Correction ->

Linearization -> Lens Shading Correction -> White Balance / Exposure /

Focus Apply -> Bayer Noise Reduction -> ANR -> Demosaicing -> Color

Correction Matrix -> Gamma correction -> Color Space Conversion ->

Chroma Down Scaling -> Chromatic Noise Reduction -> Total Color

Correction -> XNR3 -> TNR -> DDR

The table below presents a description of the above algorithms.

======================== =======================================================
Name			 Description
======================== =======================================================
Optical Black Correction Optical Black Correction block subtracts a pre-defined
			 value from the respective pixel values to obtain better
			 image quality.
			 Defined in :c:type:`ipu3_uapi_obgrid_param`.
Linearization		 This algo block uses linearization parameters to
			 address non-linearity sensor effects. The Lookup table
			 table is defined in
			 :c:type:`ipu3_uapi_isp_lin_vmem_params`.
SHD			 Lens shading correction is used to correct spatial
			 non-uniformity of the pixel response due to optical
			 lens shading. This is done by applying a different gain
			 for each pixel. The gain, black level etc are
			 configured in :c:type:`ipu3_uapi_shd_config_static`.
BNR			 Bayer noise reduction block removes image noise by
			 applying a bilateral filter.
			 See :c:type:`ipu3_uapi_bnr_static_config` for details.
ANR			 Advanced Noise Reduction is a block based algorithm
			 that performs noise reduction in the Bayer domain. The
			 convolution matrix etc can be found in
			 :c:type:`ipu3_uapi_anr_config`.
Demosaicing		 Demosaicing converts raw sensor data in Bayer format
			 into RGB (Red, Green, Blue) presentation. Then add
			 outputs of estimation of Y channel for following stream
			 processing by Firmware. The struct is defined as
			 :c:type:`ipu3_uapi_dm_config`. (TODO)
Color Correction	 Color Correction algo transforms sensor specific color
			 space to the standard "sRGB" color space. This is done
			 by applying 3x3 matrix defined in
			 :c:type:`ipu3_uapi_ccm_mat_config`.
Gamma correction	 Gamma correction :c:type:`ipu3_uapi_gamma_config` is a
			 basic non-linear tone mapping correction that is
			 applied per pixel for each pixel component.
CSC			 Color space conversion transforms each pixel from the
			 RGB primary presentation to YUV (Y: brightness,
			 UV: Luminance) presentation. This is done by applying
			 a 3x3 matrix defined in
			 :c:type:`ipu3_uapi_csc_mat_config`
CDS			 Chroma down sampling
			 After the CSC is performed, the Chroma Down Sampling
			 is applied for a UV plane down sampling by a factor
			 of 2 in each direction for YUV 4:2:0 using a 4x2
			 configurable filter :c:type:`ipu3_uapi_cds_params`.
CHNR			 Chroma noise reduction
			 This block processes only the chrominance pixels and
			 performs noise reduction by cleaning the high
			 frequency noise.
			 See struct :c:type:`ipu3_uapi_yuvp1_chnr_config`.
TCC			 Total color correction as defined in struct
			 :c:type:`ipu3_uapi_yuvp2_tcc_static_config`.
XNR3			 eXtreme Noise Reduction V3 is the third revision of
			 noise reduction algorithm used to improve image
			 quality. This removes the low frequency noise in the
			 captured image. Two related structs are  being defined,
			 :c:type:`ipu3_uapi_isp_xnr3_params` for ISP data memory
			 and :c:type:`ipu3_uapi_isp_xnr3_vmem_params` for vector
			 memory.
TNR			 Temporal Noise Reduction block compares successive
			 frames in time to remove anomalies / noise in pixel
			 values. :c:type:`ipu3_uapi_isp_tnr3_vmem_params` and
			 :c:type:`ipu3_uapi_isp_tnr3_params` are defined for ISP
			 vector and data memory respectively.
======================== =======================================================

A few stages of the pipeline will be executed by firmware running on the ISP
processor, while many others will use a set of fixed hardware blocks also
called accelerator cluster (ACC) to crunch pixel data and produce statistics.

ACC parameters of individual algorithms, as defined by
:c:type:`ipu3_uapi_acc_param`, can be chosen to be applied by the user
space through struct :c:type:`ipu3_uapi_flags` embedded in
:c:type:`ipu3_uapi_params` structure. For parameters that are configured as
not enabled by the user space, the corresponding structs are ignored by the
driver, in which case the existing configuration of the algorithm will be
preserved.

Both 3A statistics and pipeline parameters described here are closely tied to
the underlying camera sub-system (CSS) APIs. They are usually consumed and
produced by dedicated user space libraries that comprise the important tuning
tools, thus freeing the developers from being bothered with the low level
hardware and algorithm details.

It should be noted that IPU3 DMA operations require the addresses of all data
structures (that includes both input and output) to be aligned on 32 byte
boundaries.

The meta data :c:type:`ipu3_uapi_params` will be sent to "ipu3-imgu parameters"
video node in ``V4L2_BUF_TYPE_META_CAPTURE`` format.

.. code-block:: c

	struct ipu3_uapi_params {
		/* Flags which of the settings below are to be applied */
		struct ipu3_uapi_flags use;

		/* Accelerator cluster parameters */
		struct ipu3_uapi_acc_param acc_param;

		/* ISP vector address space parameters */
		struct ipu3_uapi_isp_lin_vmem_params lin_vmem_params;
		struct ipu3_uapi_isp_tnr3_vmem_params tnr3_vmem_params;
		struct ipu3_uapi_isp_xnr3_vmem_params xnr3_vmem_params;

		/* ISP data memory (DMEM) parameters */
		struct ipu3_uapi_isp_tnr3_params tnr3_dmem_params;
		struct ipu3_uapi_isp_xnr3_params xnr3_dmem_params;

		/* Optical black level compensation */
		struct ipu3_uapi_obgrid_param obgrid_param;
	};

Intel IPU3 ImgU uAPI data types
===============================

.. kernel-doc:: drivers/staging/media/ipu3/include/intel-ipu3.h
