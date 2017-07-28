/* Release Version: irci_stable_candrpv_0415_20150521_0458 */
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

#ifndef _IA_CSS_TYPES_H
#define _IA_CSS_TYPES_H

/** @file
 * This file contains types used for the ia_css parameters.
 * These types are in a separate file because they are expected
 * to be used in software layers that do not access the CSS API
 * directly but still need to forward parameters for it.
 */

#include <type_support.h>

#include "ia_css_frac.h"

#include "isp/kernels/aa/aa_2/ia_css_aa2_types.h"
#include "isp/kernels/anr/anr_1.0/ia_css_anr_types.h"
#include "isp/kernels/anr/anr_2/ia_css_anr2_types.h"
#include "isp/kernels/cnr/cnr_2/ia_css_cnr2_types.h"
#include "isp/kernels/csc/csc_1.0/ia_css_csc_types.h"
#include "isp/kernels/ctc/ctc_1.0/ia_css_ctc_types.h"
#include "isp/kernels/dp/dp_1.0/ia_css_dp_types.h"
#include "isp/kernels/de/de_1.0/ia_css_de_types.h"
#include "isp/kernels/de/de_2/ia_css_de2_types.h"
#include "isp/kernels/fc/fc_1.0/ia_css_formats_types.h"
#include "isp/kernels/fpn/fpn_1.0/ia_css_fpn_types.h"
#include "isp/kernels/gc/gc_1.0/ia_css_gc_types.h"
#include "isp/kernels/gc/gc_2/ia_css_gc2_types.h"
#include "isp/kernels/macc/macc_1.0/ia_css_macc_types.h"
#include "isp/kernels/ob/ob_1.0/ia_css_ob_types.h"
#include "isp/kernels/s3a/s3a_1.0/ia_css_s3a_types.h"
#include "isp/kernels/sc/sc_1.0/ia_css_sc_types.h"
#include "isp/kernels/sdis/sdis_1.0/ia_css_sdis_types.h"
#include "isp/kernels/sdis/sdis_2/ia_css_sdis2_types.h"
#include "isp/kernels/tnr/tnr_1.0/ia_css_tnr_types.h"
#include "isp/kernels/wb/wb_1.0/ia_css_wb_types.h"
#include "isp/kernels/xnr/xnr_1.0/ia_css_xnr_types.h"
#include "isp/kernels/xnr/xnr_3.0/ia_css_xnr3_types.h"
#ifdef ISP2401
#include "isp/kernels/tnr/tnr3/ia_css_tnr3_types.h"
#endif
#include "isp/kernels/ynr/ynr_1.0/ia_css_ynr_types.h"
#include "isp/kernels/ynr/ynr_2/ia_css_ynr2_types.h"
#include "isp/kernels/output/output_1.0/ia_css_output_types.h"

#define IA_CSS_DVS_STAT_GRID_INFO_SUPPORTED
/**< Should be removed after Driver adaptation will be done */

#define IA_CSS_VERSION_MAJOR    2
#define IA_CSS_VERSION_MINOR    0
#define IA_CSS_VERSION_REVISION 2

#define IA_CSS_MORPH_TABLE_NUM_PLANES  6

/* Min and max exposure IDs. These macros are here to allow
 * the drivers to get this information. Changing these macros
 * constitutes a CSS API change. */
#define IA_CSS_ISYS_MIN_EXPOSURE_ID 1   /**< Minimum exposure ID */
#define IA_CSS_ISYS_MAX_EXPOSURE_ID 250 /**< Maximum exposure ID */

/* opaque types */
struct ia_css_isp_parameters;
struct ia_css_pipe;
struct ia_css_memory_offsets;
struct ia_css_config_memory_offsets;
struct ia_css_state_memory_offsets;

/** Virtual address within the CSS address space. */
typedef uint32_t ia_css_ptr;

/** Generic resolution structure.
 */
struct ia_css_resolution {
	uint32_t width;  /**< Width */
	uint32_t height; /**< Height */
};

/** Generic coordinate structure.
 */
struct ia_css_coordinate {
	int32_t x;	/**< Value of a coordinate on the horizontal axis */
	int32_t y;	/**< Value of a coordinate on the vertical axis */
};

/** Vector with signed values. This is used to indicate motion for
 * Digital Image Stabilization.
 */
struct ia_css_vector {
	int32_t x; /**< horizontal motion (in pixels) */
	int32_t y; /**< vertical motion (in pixels) */
};

/* Short hands */
#define IA_CSS_ISP_DMEM IA_CSS_ISP_DMEM0
#define IA_CSS_ISP_VMEM IA_CSS_ISP_VMEM0

/** CSS data descriptor */
struct ia_css_data {
	ia_css_ptr address; /**< CSS virtual address */
	uint32_t   size;    /**< Disabled if 0 */
};

/** Host data descriptor */
struct ia_css_host_data {
	char      *address; /**< Host address */
	uint32_t   size;    /**< Disabled if 0 */
};

/** ISP data descriptor */
struct ia_css_isp_data {
	uint32_t   address; /**< ISP address */
	uint32_t   size;    /**< Disabled if 0 */
};

/** Shading Correction types. */
enum ia_css_shading_correction_type {
#ifndef ISP2401
	IA_CSS_SHADING_CORRECTION_TYPE_1 /**< Shading Correction 1.0 (pipe 1.0 on ISP2300, pipe 2.2 on ISP2400) */
#else
	IA_CSS_SHADING_CORRECTION_NONE,	 /**< Shading Correction is not processed in the pipe. */
	IA_CSS_SHADING_CORRECTION_TYPE_1 /**< Shading Correction 1.0 (pipe 1.0 on ISP2300, pipe 2.2 on ISP2400/2401) */
#endif

	/**< More shading correction types can be added in the future. */
};

/** Shading Correction information. */
struct ia_css_shading_info {
	enum ia_css_shading_correction_type type; /**< Shading Correction type. */

	union {	/** Shading Correction information of each Shading Correction types. */

		/** Shading Correction information of IA_CSS_SHADING_CORRECTION_TYPE_1.
		 *
		 *  This structure contains the information necessary to generate
		 *  the shading table required in the isp.
		 *  This structure is filled in the css,
		 *  and the driver needs to get it to generate the shading table.
		 *
		 *  Before the shading correction is applied, NxN-filter and/or scaling
		 *  are applied in the isp, depending on the isp binaries.
		 *  Then, these should be considered in generating the shading table.
		 *    - Bad pixels on left/top sides generated by NxN-filter
		 *      (Bad pixels are NOT considered currently,
		 *      because they are subtle.)
		 *    - Down-scaling/Up-scaling factor
		 *
		 *  Shading correction is applied to the area
		 *  which has real sensor data and margin.
		 *  Then, the shading table should cover the area including margin.
		 *  This structure has this information.
		 *    - Origin coordinate of bayer (real sensor data)
		 *      on the shading table
		 *
		 * ------------------------ISP 2401-----------------------
		 *
		 *  the shading table directly required from ISP.
		 *  This structure is filled in CSS, and the driver needs to get it to generate the shading table.
		 *
		 *  The shading correction is applied to the bayer area which contains sensor data and padding data.
		 *  The shading table should cover this bayer area.
		 *
		 *  The shading table size directly required from ISP is expressed by these parameters.
		 *    1. uint32_t num_hor_grids;
		 *    2. uint32_t num_ver_grids;
		 *    3. uint32_t bqs_per_grid_cell;
		 *
		 *  In some isp binaries, the bayer scaling is applied before the shading correction is applied.
		 *  Then, this scaling factor should be considered in generating the shading table.
		 *  The scaling factor is expressed by these parameters.
		 *    4. uint32_t bayer_scale_hor_ratio_in;
		 *    5. uint32_t bayer_scale_hor_ratio_out;
		 *    6. uint32_t bayer_scale_ver_ratio_in;
		 *    7. uint32_t bayer_scale_ver_ratio_out;
		 *
		 *  The sensor data size inputted to ISP is expressed by this parameter.
		 *  This is the size BEFORE the bayer scaling is applied.
		 *    8. struct ia_css_resolution isp_input_sensor_data_res_bqs;
		 *
		 *  The origin of the sensor data area positioned on the shading table at the shading correction
		 *  is expressed by this parameter.
		 *  The size of this area assumes the size AFTER the bayer scaling is applied
		 *  to the isp_input_sensor_data_resolution_bqs.
		 *    9. struct ia_css_coordinate sensor_data_origin_bqs_on_sctbl;
		 *
		 *  ****** Definitions of the shading table and the sensor data at the shading correction ******
		 *
		 * (0,0)--------------------- TW -------------------------------
		 *   |                                            shading table |
		 *   |      (ox,oy)---------- W --------------------------      |
		 *   |        |                               sensor data |     |
		 *   |        |                                           |     |
		 *  TH        H             sensor data center            |     |
		 *   |        |                  (cx,cy)                  |     |
		 *   |        |                                           |     |
		 *   |        |                                           |     |
		 *   |        |                                           |     |
		 *   |         -------------------------------------------      |
		 *   |                                                          |
		 *    ----------------------------------------------------------
		 *
		 *    Example of still mode for output 1080p:
		 *
		 *    num_hor_grids = 66
		 *    num_ver_grids = 37
		 *    bqs_per_grid_cell = 16
		 *    bayer_scale_hor_ratio_in = 1
		 *    bayer_scale_hor_ratio_out = 1
		 *    bayer_scale_ver_ratio_in = 1
		 *    bayer_scale_ver_ratio_out = 1
		 *    isp_input_sensor_data_resolution_bqs = {966, 546}
		 *    sensor_data_origin_bqs_on_sctbl = {61, 15}
		 *
		 *    TW, TH [bqs]: width and height of shading table
		 *        TW = (num_hor_grids - 1) * bqs_per_grid_cell = (66 - 1) * 16 = 1040
		 *        TH = (num_ver_grids - 1) * bqs_per_grid_cell = (37 - 1) * 16 = 576
		 *
		 *    W, H [bqs]: width and height of sensor data at shading correction
		 *        W = sensor_data_res_bqs.width
		 *          = isp_input_sensor_data_res_bqs.width
		 *              * bayer_scale_hor_ratio_out / bayer_scale_hor_ratio_in + 0.5 = 966
		 *        H = sensor_data_res_bqs.height
		 *          = isp_input_sensor_data_res_bqs.height
		 *               * bayer_scale_ver_ratio_out / bayer_scale_ver_ratio_in + 0.5 = 546
		 *
		 *    (ox, oy) [bqs]: origin of sensor data positioned on shading table at shading correction
		 *        ox = sensor_data_origin_bqs_on_sctbl.x = 61
		 *        oy = sensor_data_origin_bqs_on_sctbl.y = 15
		 *
		 *    (cx, cy) [bqs]: center of sensor data positioned on shading table at shading correction
		 *        cx = ox + W/2 = 61 + 966/2 = 544
		 *        cy = oy + H/2 = 15 + 546/2 = 288
		 *
		 *  ****** Relation between the shading table and the sensor data ******
		 *
		 *    The origin of the sensor data should be on the shading table.
		 *        0 <= ox < TW,  0 <= oy < TH
		 *
		 *  ****** How to center the shading table on the sensor data ******
		 *
		 *    To center the shading table on the sensor data,
		 *    CSS decides the shading table size so that a certain grid point is positioned
		 *    on the center of the sensor data at the shading correction.
		 *    CSS expects the shading center is set on this grid point
		 *    when the shading table data is calculated in AIC.
		 *
		 *    W, H [bqs]: width and height of sensor data at shading correction
		 *	W = sensor_data_res_bqs.width
		 *	H = sensor_data_res_bqs.height
		 *
		 *    (cx, cy) [bqs]: center of sensor data positioned on shading table at shading correction
		 *	cx = sensor_data_origin_bqs_on_sctbl.x + W/2
		 *	cy = sensor_data_origin_bqs_on_sctbl.y + H/2
		 *
		 *    CSS decides the shading table size and the sensor data position
		 *    so that the (cx, cy) satisfies this condition.
		 *	mod(cx, bqs_per_grid_cell) = 0
		 *	mod(cy, bqs_per_grid_cell) = 0
		 *
		 *  ****** How to change the sensor data size by processes in the driver and ISP ******
		 *
		 *    1. sensor data size: Physical sensor size
		 *			   (The struct ia_css_shading_info does not have this information.)
		 *    2. process:          Driver applies the sensor cropping/binning/scaling to physical sensor size.
		 *    3. sensor data size: ISP input size (== shading_info.isp_input_sensor_data_res_bqs)
		 *			   (ISP assumes the ISP input sensor data is centered on the physical sensor.)
		 *    4. process:          ISP applies the bayer scaling by the factor of shading_info.bayer_scale_*.
		 *    5. sensor data size: Scaling factor * ISP input size (== shading_info.sensor_data_res_bqs)
		 *    6. process:          ISP applies the shading correction.
		 *
		 *  ISP block: SC1
		 *  ISP1: SC1 is used.
		 *  ISP2: SC1 is used.
		 */
		struct {
#ifndef ISP2401
			uint32_t enable;	/**< Shading correction enabled.
						     0:disabled, 1:enabled */
			uint32_t num_hor_grids;	/**< Number of data points per line
						     per color on shading table. */
			uint32_t num_ver_grids;	/**< Number of lines of data points
						     per color on shading table. */
			uint32_t bqs_per_grid_cell; /**< Grid cell size
						in BQ(Bayer Quad) unit.
						(1BQ means {Gr,R,B,Gb}(2x2 pixels).)
						Valid values are 8,16,32,64. */
#else
			uint32_t num_hor_grids;	/**< Number of data points per line per color on shading table. */
			uint32_t num_ver_grids;	/**< Number of lines of data points per color on shading table. */
			uint32_t bqs_per_grid_cell; /**< Grid cell size in BQ unit.
							 NOTE: bqs = size in BQ(Bayer Quad) unit.
							       1BQ means {Gr,R,B,Gb} (2x2 pixels).
							       Horizontal 1 bqs corresponds to horizontal 2 pixels.
							       Vertical 1 bqs corresponds to vertical 2 pixels. */
#endif
			uint32_t bayer_scale_hor_ratio_in;
			uint32_t bayer_scale_hor_ratio_out;
#ifndef ISP2401
			/**< Horizontal ratio of bayer scaling
			between input width and output width, for the scaling
			which should be done before shading correction.
			  output_width = input_width * bayer_scale_hor_ratio_out
						/ bayer_scale_hor_ratio_in */
#else
				/**< Horizontal ratio of bayer scaling between input width and output width,
				     for the scaling which should be done before shading correction.
					output_width = input_width * bayer_scale_hor_ratio_out
									/ bayer_scale_hor_ratio_in + 0.5 */
#endif
			uint32_t bayer_scale_ver_ratio_in;
			uint32_t bayer_scale_ver_ratio_out;
#ifndef ISP2401
			/**< Vertical ratio of bayer scaling
			between input height and output height, for the scaling
			which should be done before shading correction.
			  output_height = input_height * bayer_scale_ver_ratio_out
						/ bayer_scale_ver_ratio_in */
			uint32_t sc_bayer_origin_x_bqs_on_shading_table;
			/**< X coordinate (in bqs) of bayer origin on shading table.
			This indicates the left-most pixel of bayer
			(not include margin) inputted to the shading correction.
			This corresponds to the left-most pixel of bayer
			inputted to isp from sensor. */
			uint32_t sc_bayer_origin_y_bqs_on_shading_table;
			/**< Y coordinate (in bqs) of bayer origin on shading table.
			This indicates the top pixel of bayer
			(not include margin) inputted to the shading correction.
			This corresponds to the top pixel of bayer
			inputted to isp from sensor. */
#else
				/**< Vertical ratio of bayer scaling between input height and output height,
				     for the scaling which should be done before shading correction.
					output_height = input_height * bayer_scale_ver_ratio_out
									/ bayer_scale_ver_ratio_in + 0.5 */
			struct ia_css_resolution isp_input_sensor_data_res_bqs;
				/**< Sensor data size (in bqs) inputted to ISP. This is the size BEFORE bayer scaling.
				     NOTE: This is NOT the size of the physical sensor size.
					   CSS requests the driver that ISP inputs sensor data
					   by the size of isp_input_sensor_data_res_bqs.
					   The driver sends the sensor data to ISP,
					   after the adequate cropping/binning/scaling
					   are applied to the physical sensor data area.
					   ISP assumes the area of isp_input_sensor_data_res_bqs
					   is centered on the physical sensor. */
			struct ia_css_resolution sensor_data_res_bqs;
				/**< Sensor data size (in bqs) at shading correction.
				     This is the size AFTER bayer scaling. */
			struct ia_css_coordinate sensor_data_origin_bqs_on_sctbl;
				/**< Origin of sensor data area positioned on shading table at shading correction.
				     The coordinate x,y should be positive values. */
#endif
		} type_1;

		/**< More structures can be added here when more shading correction types will be added
		     in the future. */
	} info;
};

#ifndef ISP2401

/** Default Shading Correction information of Shading Correction Type 1. */
#define DEFAULT_SHADING_INFO_TYPE_1 \
{ \
	IA_CSS_SHADING_CORRECTION_TYPE_1,	/* type */ \
	{					/* info */ \
		{ \
			0,	/* enable */ \
			0,	/* num_hor_grids */ \
			0,	/* num_ver_grids */ \
			0,	/* bqs_per_grid_cell */ \
			1,	/* bayer_scale_hor_ratio_in */ \
			1,	/* bayer_scale_hor_ratio_out */ \
			1,	/* bayer_scale_ver_ratio_in */ \
			1,	/* bayer_scale_ver_ratio_out */ \
			0,	/* sc_bayer_origin_x_bqs_on_shading_table */ \
			0	/* sc_bayer_origin_y_bqs_on_shading_table */ \
		} \
	} \
}

#else

/** Default Shading Correction information of Shading Correction Type 1. */
#define DEFAULT_SHADING_INFO_TYPE_1 \
{ \
	IA_CSS_SHADING_CORRECTION_TYPE_1,	/* type */ \
	{					/* info */ \
		{ \
			0,			/* num_hor_grids */ \
			0,			/* num_ver_grids */ \
			0,			/* bqs_per_grid_cell */ \
			1,			/* bayer_scale_hor_ratio_in */ \
			1,			/* bayer_scale_hor_ratio_out */ \
			1,			/* bayer_scale_ver_ratio_in */ \
			1,			/* bayer_scale_ver_ratio_out */ \
			{0, 0},			/* isp_input_sensor_data_res_bqs */ \
			{0, 0},			/* sensor_data_res_bqs */ \
			{0, 0}			/* sensor_data_origin_bqs_on_sctbl */ \
		} \
	} \
}

#endif

/** Default Shading Correction information. */
#define DEFAULT_SHADING_INFO	DEFAULT_SHADING_INFO_TYPE_1

/** structure that describes the 3A and DIS grids */
struct ia_css_grid_info {
	/** \name ISP input size
	  * that is visible for user
	  * @{
	  */
	uint32_t isp_in_width;
	uint32_t isp_in_height;
	/** @}*/

	struct ia_css_3a_grid_info  s3a_grid; /**< 3A grid info */
	union ia_css_dvs_grid_u dvs_grid;
		/**< All types of DVS statistics grid info union */

	enum ia_css_vamem_type vamem_type;
};

/** defaults for ia_css_grid_info structs */
#define DEFAULT_GRID_INFO \
{ \
	0,				/* isp_in_width */ \
	0,				/* isp_in_height */ \
	DEFAULT_3A_GRID_INFO,		/* s3a_grid */ \
	DEFAULT_DVS_GRID_INFO,		/* dvs_grid */ \
	IA_CSS_VAMEM_TYPE_1		/* vamem_type */ \
}

/** Morphing table, used for geometric distortion and chromatic abberration
 *  correction (GDCAC, also called GDC).
 *  This table describes the imperfections introduced by the lens, the
 *  advanced ISP can correct for these imperfections using this table.
 */
struct ia_css_morph_table {
	uint32_t enable; /**< To disable GDC, set this field to false. The
			  coordinates fields can be set to NULL in this case. */
	uint32_t height; /**< Table height */
	uint32_t width;  /**< Table width */
	uint16_t *coordinates_x[IA_CSS_MORPH_TABLE_NUM_PLANES];
	/**< X coordinates that describe the sensor imperfection */
	uint16_t *coordinates_y[IA_CSS_MORPH_TABLE_NUM_PLANES];
	/**< Y coordinates that describe the sensor imperfection */
};

struct ia_css_dvs_6axis_config {
	unsigned int exp_id;
	/**< Exposure ID, see ia_css_event_public.h for more detail */
	uint32_t width_y;
	uint32_t height_y;
	uint32_t width_uv;
	uint32_t height_uv;
	uint32_t *xcoords_y;
	uint32_t *ycoords_y;
	uint32_t *xcoords_uv;
	uint32_t *ycoords_uv;
};

/**
 * This specifies the coordinates (x,y)
 */
struct ia_css_point {
	int32_t x; /**< x coordinate */
	int32_t y; /**< y coordinate */
};

/**
 * This specifies the region
 */
struct ia_css_region {
	struct ia_css_point origin; /**< Starting point coordinates for the region */
	struct ia_css_resolution resolution; /**< Region resolution */
};

/**
 * Digital zoom:
 * This feature is currently available only for video, but will become
 * available for preview and capture as well.
 * Set the digital zoom factor, this is a logarithmic scale. The actual zoom
 * factor will be 64/x.
 * Setting dx or dy to 0 disables digital zoom for that direction.
 * New API change for Digital zoom:(added struct ia_css_region zoom_region)
 * zoom_region specifies the origin of the zoom region and width and
 * height of that region.
 * origin : This is the coordinate (x,y) within the effective input resolution
 * of the stream. where, x >= 0 and y >= 0. (0,0) maps to the upper left of the
 * effective input resolution.
 * resolution : This is resolution of zoom region.
 * where, x + width <= effective input width
 * y + height <= effective input height
 */
struct ia_css_dz_config {
	uint32_t dx; /**< Horizontal zoom factor */
	uint32_t dy; /**< Vertical zoom factor */
	struct ia_css_region zoom_region; /**< region for zoom */
};

/** The still capture mode, this can be RAW (simply copy sensor input to DDR),
 *  Primary ISP, the Advanced ISP (GDC) or the low-light ISP (ANR).
 */
enum ia_css_capture_mode {
	IA_CSS_CAPTURE_MODE_RAW,      /**< no processing, copy data only */
	IA_CSS_CAPTURE_MODE_BAYER,    /**< bayer processing, up to demosaic */
	IA_CSS_CAPTURE_MODE_PRIMARY,  /**< primary ISP */
	IA_CSS_CAPTURE_MODE_ADVANCED, /**< advanced ISP (GDC) */
	IA_CSS_CAPTURE_MODE_LOW_LIGHT /**< low light ISP (ANR) */
};

struct ia_css_capture_config {
	enum ia_css_capture_mode mode; /**< Still capture mode */
	uint32_t enable_xnr;	       /**< Enable/disable XNR */
	uint32_t enable_raw_output;
	bool enable_capture_pp_bli;    /**< Enable capture_pp_bli mode */
};

/** default settings for ia_css_capture_config structs */
#define DEFAULT_CAPTURE_CONFIG \
{ \
	IA_CSS_CAPTURE_MODE_PRIMARY,	/* mode (capture) */ \
	false,				/* enable_xnr */ \
	false,				/* enable_raw_output */ \
	false				/* enable_capture_pp_bli */ \
}


/** ISP filter configuration. This is a collection of configurations
 *  for each of the ISP filters (modules).
 *
 *  NOTE! The contents of all pointers is copied when get or set with the
 *  exception of the shading and morph tables. For these we only copy the
 *  pointer, so the caller must make sure the memory contents of these pointers
 *  remain valid as long as they are used by the CSS. This will be fixed in the
 *  future by copying the contents instead of just the pointer.
 *
 *  Comment:
 *    ["ISP block", 1&2]   : ISP block is used both for ISP1 and ISP2.
 *    ["ISP block", 1only] : ISP block is used only for ISP1.
 *    ["ISP block", 2only] : ISP block is used only for ISP2.
 */
struct ia_css_isp_config {
	struct ia_css_wb_config   *wb_config;	/**< White Balance
							[WB1, 1&2] */
	struct ia_css_cc_config   *cc_config;	/**< Color Correction
							[CSC1, 1only] */
	struct ia_css_tnr_config  *tnr_config;	/**< Temporal Noise Reduction
							[TNR1, 1&2] */
	struct ia_css_ecd_config  *ecd_config;	/**< Eigen Color Demosaicing
							[DE2, 2only] */
	struct ia_css_ynr_config  *ynr_config;	/**< Y(Luma) Noise Reduction
							[YNR2&YEE2, 2only] */
	struct ia_css_fc_config   *fc_config;	/**< Fringe Control
							[FC2, 2only] */
	struct ia_css_formats_config   *formats_config;	/**< Formats Control for main output
							[FORMATS, 1&2] */
	struct ia_css_cnr_config  *cnr_config;	/**< Chroma Noise Reduction
							[CNR2, 2only] */
	struct ia_css_macc_config *macc_config;	/**< MACC
							[MACC2, 2only] */
	struct ia_css_ctc_config  *ctc_config;	/**< Chroma Tone Control
							[CTC2, 2only] */
	struct ia_css_aa_config   *aa_config;	/**< YUV Anti-Aliasing
							[AA2, 2only]
							(not used currently) */
	struct ia_css_aa_config   *baa_config;	/**< Bayer Anti-Aliasing
							[BAA2, 1&2] */
	struct ia_css_ce_config   *ce_config;	/**< Chroma Enhancement
							[CE1, 1only] */
	struct ia_css_dvs_6axis_config *dvs_6axis_config;
	struct ia_css_ob_config   *ob_config;  /**< Objective Black
							[OB1, 1&2] */
	struct ia_css_dp_config   *dp_config;  /**< Defect Pixel Correction
							[DPC1/DPC2, 1&2] */
	struct ia_css_nr_config   *nr_config;  /**< Noise Reduction
							[BNR1&YNR1&CNR1, 1&2]*/
	struct ia_css_ee_config   *ee_config;  /**< Edge Enhancement
							[YEE1, 1&2] */
	struct ia_css_de_config   *de_config;  /**< Demosaic
							[DE1, 1only] */
	struct ia_css_gc_config   *gc_config;  /**< Gamma Correction (for YUV)
							[GC1, 1only] */
	struct ia_css_anr_config  *anr_config; /**< Advanced Noise Reduction */
	struct ia_css_3a_config   *s3a_config; /**< 3A Statistics config */
	struct ia_css_xnr_config  *xnr_config; /**< eXtra Noise Reduction */
	struct ia_css_dz_config   *dz_config;  /**< Digital Zoom */
	struct ia_css_cc_config *yuv2rgb_cc_config; /**< Color Correction
							[CCM2, 2only] */
	struct ia_css_cc_config *rgb2yuv_cc_config; /**< Color Correction
							[CSC2, 2only] */
	struct ia_css_macc_table  *macc_table;	/**< MACC
							[MACC1/MACC2, 1&2]*/
	struct ia_css_gamma_table *gamma_table;	/**< Gamma Correction (for YUV)
							[GC1, 1only] */
	struct ia_css_ctc_table   *ctc_table;	/**< Chroma Tone Control
							[CTC1, 1only] */

	/** \deprecated */
	struct ia_css_xnr_table   *xnr_table;	/**< eXtra Noise Reduction
							[XNR1, 1&2] */
	struct ia_css_rgb_gamma_table *r_gamma_table;/**< sRGB Gamma Correction
							[GC2, 2only] */
	struct ia_css_rgb_gamma_table *g_gamma_table;/**< sRGB Gamma Correction
							[GC2, 2only] */
	struct ia_css_rgb_gamma_table *b_gamma_table;/**< sRGB Gamma Correction
							[GC2, 2only] */
	struct ia_css_vector      *motion_vector; /**< For 2-axis DVS */
	struct ia_css_shading_table *shading_table;
	struct ia_css_morph_table   *morph_table;
	struct ia_css_dvs_coefficients *dvs_coefs; /**< DVS 1.0 coefficients */
	struct ia_css_dvs2_coefficients *dvs2_coefs; /**< DVS 2.0 coefficients */
	struct ia_css_capture_config   *capture_config;
	struct ia_css_anr_thres   *anr_thres;
	/** @deprecated{Old shading settings, see bugzilla bz675 for details} */
	struct ia_css_shading_settings *shading_settings;
	struct ia_css_xnr3_config *xnr3_config; /**< eXtreme Noise Reduction v3 */
	/** comment from Lasse: Be aware how this feature will affect coordinate
	 *  normalization in different parts of the system. (e.g. face detection,
	 *  touch focus, 3A statistics and windows of interest, shading correction,
	 *  DVS, GDC) from IQ tool level and application level down-to ISP FW level.
	 *  the risk for regression is not in the individual blocks, but how they
	 *  integrate together. */
	struct ia_css_output_config   *output_config;	/**< Main Output Mirroring, flipping */

#ifdef ISP2401
	struct ia_css_tnr3_kernel_config         *tnr3_config;           /**< TNR3 config */
#endif
	struct ia_css_scaler_config              *scaler_config;         /**< Skylake: scaler config (optional) */
	struct ia_css_formats_config             *formats_config_display;/**< Formats control for viewfinder/display output (optional)
										[OSYS, n/a] */
	struct ia_css_output_config              *output_config_display; /**< Viewfinder/display output mirroring, flipping (optional) */

	struct ia_css_frame                      *output_frame;          /**< Output frame the config is to be applied to (optional) */
	uint32_t			isp_config_id;	/**< Unique ID to track which config was actually applied to a particular frame */
};

#endif /* _IA_CSS_TYPES_H */
