/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#ifndef _KOMEDA_PIPELINE_H_
#define _KOMEDA_PIPELINE_H_

#include <linux/types.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include "malidp_utils.h"

#define KOMEDA_MAX_PIPELINES		2
#define KOMEDA_PIPELINE_MAX_LAYERS	4
#define KOMEDA_PIPELINE_MAX_SCALERS	2
#define KOMEDA_COMPONENT_N_INPUTS	5

/* pipeline component IDs */
enum {
	KOMEDA_COMPONENT_LAYER0		= 0,
	KOMEDA_COMPONENT_LAYER1		= 1,
	KOMEDA_COMPONENT_LAYER2		= 2,
	KOMEDA_COMPONENT_LAYER3		= 3,
	KOMEDA_COMPONENT_WB_LAYER	= 7, /* write back layer */
	KOMEDA_COMPONENT_SCALER0	= 8,
	KOMEDA_COMPONENT_SCALER1	= 9,
	KOMEDA_COMPONENT_SPLITTER	= 12,
	KOMEDA_COMPONENT_MERGER		= 14,
	KOMEDA_COMPONENT_COMPIZ0	= 16, /* compositor */
	KOMEDA_COMPONENT_COMPIZ1	= 17,
	KOMEDA_COMPONENT_IPS0		= 20, /* post image processor */
	KOMEDA_COMPONENT_IPS1		= 21,
	KOMEDA_COMPONENT_TIMING_CTRLR	= 22, /* timing controller */
};

#define KOMEDA_PIPELINE_LAYERS		(BIT(KOMEDA_COMPONENT_LAYER0) |\
					 BIT(KOMEDA_COMPONENT_LAYER1) |\
					 BIT(KOMEDA_COMPONENT_LAYER2) |\
					 BIT(KOMEDA_COMPONENT_LAYER3))

#define KOMEDA_PIPELINE_SCALERS		(BIT(KOMEDA_COMPONENT_SCALER0) |\
					 BIT(KOMEDA_COMPONENT_SCALER1))

#define KOMEDA_PIPELINE_COMPIZS		(BIT(KOMEDA_COMPONENT_COMPIZ0) |\
					 BIT(KOMEDA_COMPONENT_COMPIZ1))

#define KOMEDA_PIPELINE_IMPROCS		(BIT(KOMEDA_COMPONENT_IPS0) |\
					 BIT(KOMEDA_COMPONENT_IPS1))
struct komeda_component;
struct komeda_component_state;

/** komeda_component_funcs - component control functions */
struct komeda_component_funcs {
	/** @validate: optional,
	 * component may has special requirements or limitations, this function
	 * supply HW the ability to do the further HW specific check.
	 */
	int (*validate)(struct komeda_component *c,
			struct komeda_component_state *state);
	/** @update: update is a active update */
	void (*update)(struct komeda_component *c,
		       struct komeda_component_state *state);
	/** @disable: disable component */
	void (*disable)(struct komeda_component *c);
	/** @dump_register: Optional, dump registers to seq_file */
	void (*dump_register)(struct komeda_component *c, struct seq_file *seq);
};

/**
 * struct komeda_component
 *
 * struct komeda_component describe the data flow capabilities for how to link a
 * component into the display pipeline.
 * all specified components are subclass of this structure.
 */
struct komeda_component {
	/** @obj: treat component as private obj */
	struct drm_private_obj obj;
	/** @pipeline: the komeda pipeline this component belongs to */
	struct komeda_pipeline *pipeline;
	/** @name: component name */
	char name[32];
	/**
	 * @reg:
	 * component register base,
	 * which is initialized by chip and used by chip only
	 */
	u32 __iomem *reg;
	/** @id: component id */
	u32 id;
	/**
	 * @hw_id: component hw id,
	 * which is initialized by chip and used by chip only
	 */
	u32 hw_id;

	/**
	 * @max_active_inputs:
	 * @max_active_outputs:
	 *
	 * maximum number of inputs/outputs that can be active at the same time
	 * Note:
	 * the number isn't the bit number of @supported_inputs or
	 * @supported_outputs, but may be less than it, since component may not
	 * support enabling all @supported_inputs/outputs at the same time.
	 */
	u8 max_active_inputs;
	/** @max_active_outputs: maximum number of outputs */
	u8 max_active_outputs;
	/**
	 * @supported_inputs:
	 * @supported_outputs:
	 *
	 * bitmask of BIT(component->id) for the supported inputs/outputs,
	 * describes the possibilities of how a component is linked into a
	 * pipeline.
	 */
	u32 supported_inputs;
	/** @supported_outputs: bitmask of supported output componenet ids */
	u32 supported_outputs;

	/**
	 * @funcs: chip functions to access HW
	 */
	const struct komeda_component_funcs *funcs;
};

/**
 * struct komeda_component_output
 *
 * a component has multiple outputs, if want to know where the data
 * comes from, only know the component is not enough, we still need to know
 * its output port
 */
struct komeda_component_output {
	/** @component: indicate which component the data comes from */
	struct komeda_component *component;
	/**
	 * @output_port:
	 * the output port of the &komeda_component_output.component
	 */
	u8 output_port;
};

/**
 * struct komeda_component_state
 *
 * component_state is the data flow configuration of the component, and it's
 * the superclass of all specific component_state like @komeda_layer_state,
 * @komeda_scaler_state
 */
struct komeda_component_state {
	/** @obj: tracking component_state by drm_atomic_state */
	struct drm_private_state obj;
	/** @component: backpointer to the component */
	struct komeda_component *component;
	/**
	 * @binding_user:
	 * currently bound user, the user can be @crtc, @plane or @wb_conn,
	 * which is valid decided by @component and @inputs
	 *
	 * -  Layer: its user always is plane.
	 * -  compiz/improc/timing_ctrlr: the user is crtc.
	 * -  wb_layer: wb_conn;
	 * -  scaler: plane when input is layer, wb_conn if input is compiz.
	 */
	union {
		/** @crtc: backpointer for user crtc */
		struct drm_crtc *crtc;
		/** @plane: backpointer for user plane */
		struct drm_plane *plane;
		/** @wb_conn: backpointer for user wb_connector  */
		struct drm_connector *wb_conn;
		void *binding_user;
	};

	/**
	 * @active_inputs:
	 *
	 * active_inputs is bitmask of @inputs index
	 *
	 * -  active_inputs = changed_active_inputs | unchanged_active_inputs
	 * -  affected_inputs = old->active_inputs | new->active_inputs;
	 * -  disabling_inputs = affected_inputs ^ active_inputs;
	 * -  changed_inputs = disabling_inputs | changed_active_inputs;
	 *
	 * NOTE:
	 * changed_inputs doesn't include all active_input but only
	 * @changed_active_inputs, and this bitmask can be used in chip
	 * level for dirty update.
	 */
	u16 active_inputs;
	/** @changed_active_inputs: bitmask of the changed @active_inputs */
	u16 changed_active_inputs;
	/** @affected_inputs: bitmask for affected @inputs */
	u16 affected_inputs;
	/**
	 * @inputs:
	 *
	 * the specific inputs[i] only valid on BIT(i) has been set in
	 * @active_inputs, if not the inputs[i] is undefined.
	 */
	struct komeda_component_output inputs[KOMEDA_COMPONENT_N_INPUTS];
};

static inline u16 component_disabling_inputs(struct komeda_component_state *st)
{
	return st->affected_inputs ^ st->active_inputs;
}

static inline u16 component_changed_inputs(struct komeda_component_state *st)
{
	return component_disabling_inputs(st) | st->changed_active_inputs;
}

#define for_each_changed_input(st, i)	\
	for ((i) = 0; (i) < (st)->component->max_active_inputs; (i)++)	\
		if (has_bit((i), component_changed_inputs(st)))

#define to_comp(__c)	(((__c) == NULL) ? NULL : &((__c)->base))
#define to_cpos(__c)	((struct komeda_component **)&(__c))

struct komeda_layer {
	struct komeda_component base;
	/* accepted h/v input range before rotation */
	struct malidp_range hsize_in, vsize_in;
	u32 layer_type; /* RICH, SIMPLE or WB */
	u32 supported_rots;
	/* komeda supports layer split which splits a whole image to two parts
	 * left and right and handle them by two individual layer processors
	 * Note: left/right are always according to the final display rect,
	 * not the source buffer.
	 */
	struct komeda_layer *right;
};

struct komeda_layer_state {
	struct komeda_component_state base;
	/* layer specific configuration state */
	u16 hsize, vsize;
	u32 rot;
	u16 afbc_crop_l;
	u16 afbc_crop_r;
	u16 afbc_crop_t;
	u16 afbc_crop_b;
	dma_addr_t addr[3];
};

struct komeda_scaler {
	struct komeda_component base;
	struct malidp_range hsize, vsize;
	u32 max_upscaling;
	u32 max_downscaling;
	u8 scaling_split_overlap; /* split overlap for scaling */
	u8 enh_split_overlap; /* split overlap for image enhancement */
};

struct komeda_scaler_state {
	struct komeda_component_state base;
	u16 hsize_in, vsize_in;
	u16 hsize_out, vsize_out;
	u16 total_hsize_in, total_vsize_in;
	u16 total_hsize_out; /* total_xxxx are size before split */
	u16 left_crop, right_crop;
	u8 en_scaling : 1,
	   en_alpha : 1, /* enable alpha processing */
	   en_img_enhancement : 1,
	   en_split : 1,
	   right_part : 1; /* right part of split image */
};

struct komeda_compiz {
	struct komeda_component base;
	struct malidp_range hsize, vsize;
};

struct komeda_compiz_input_cfg {
	u16 hsize, vsize;
	u16 hoffset, voffset;
	u8 pixel_blend_mode, layer_alpha;
};

struct komeda_compiz_state {
	struct komeda_component_state base;
	/* composition size */
	u16 hsize, vsize;
	struct komeda_compiz_input_cfg cins[KOMEDA_COMPONENT_N_INPUTS];
};

struct komeda_merger {
	struct komeda_component base;
	struct malidp_range hsize_merged;
	struct malidp_range vsize_merged;
};

struct komeda_merger_state {
	struct komeda_component_state base;
	u16 hsize_merged;
	u16 vsize_merged;
};

struct komeda_splitter {
	struct komeda_component base;
	struct malidp_range hsize, vsize;
};

struct komeda_splitter_state {
	struct komeda_component_state base;
	u16 hsize, vsize;
	u16 overlap;
};

struct komeda_improc {
	struct komeda_component base;
	u32 supported_color_formats;  /* DRM_RGB/YUV444/YUV420*/
	u32 supported_color_depths; /* BIT(8) | BIT(10)*/
	u8 supports_degamma : 1;
	u8 supports_csc : 1;
	u8 supports_gamma : 1;
};

struct komeda_improc_state {
	struct komeda_component_state base;
	u16 hsize, vsize;
};

/* display timing controller */
struct komeda_timing_ctrlr {
	struct komeda_component base;
	u8 supports_dual_link : 1;
};

struct komeda_timing_ctrlr_state {
	struct komeda_component_state base;
};

/* Why define A separated structure but not use plane_state directly ?
 * 1. Komeda supports layer_split which means a plane_state can be split and
 *    handled by two layers, one layer only handle half of plane image.
 * 2. Fix up the user properties according to HW's capabilities, like user
 *    set rotation to R180, but HW only supports REFLECT_X+Y. the rot here is
 *    after drm_rotation_simplify()
 */
struct komeda_data_flow_cfg {
	struct komeda_component_output input;
	u16 in_x, in_y, in_w, in_h;
	u32 out_x, out_y, out_w, out_h;
	u16 total_in_h, total_in_w;
	u16 total_out_w;
	u16 left_crop, right_crop, overlap;
	u32 rot;
	int blending_zorder;
	u8 pixel_blend_mode, layer_alpha;
	u8 en_scaling : 1,
	   en_img_enhancement : 1,
	   en_split : 1,
	   is_yuv : 1,
	   right_part : 1; /* right part of display image if split enabled */
};

struct komeda_pipeline_funcs {
	/* check if the aclk (main engine clock) can satisfy the clock
	 * requirements of the downscaling that specified by dflow
	 */
	int (*downscaling_clk_check)(struct komeda_pipeline *pipe,
				     struct drm_display_mode *mode,
				     unsigned long aclk_rate,
				     struct komeda_data_flow_cfg *dflow);
	/* dump_register: Optional, dump registers to seq_file */
	void (*dump_register)(struct komeda_pipeline *pipe,
			      struct seq_file *sf);
};

/**
 * struct komeda_pipeline
 *
 * Represent a complete display pipeline and hold all functional components.
 */
struct komeda_pipeline {
	/** @obj: link pipeline as private obj of drm_atomic_state */
	struct drm_private_obj obj;
	/** @mdev: the parent komeda_dev */
	struct komeda_dev *mdev;
	/** @pxlclk: pixel clock */
	struct clk *pxlclk;
	/** @id: pipeline id */
	int id;
	/** @avail_comps: available components mask of pipeline */
	u32 avail_comps;
	/** @n_layers: the number of layer on @layers */
	int n_layers;
	/** @layers: the pipeline layers */
	struct komeda_layer *layers[KOMEDA_PIPELINE_MAX_LAYERS];
	/** @n_scalers: the number of scaler on @scalers */
	int n_scalers;
	/** @scalers: the pipeline scalers */
	struct komeda_scaler *scalers[KOMEDA_PIPELINE_MAX_SCALERS];
	/** @compiz: compositor */
	struct komeda_compiz *compiz;
	/** @splitter: for split the compiz output to two half data flows */
	struct komeda_splitter *splitter;
	/** @merger: merger */
	struct komeda_merger *merger;
	/** @wb_layer: writeback layer */
	struct komeda_layer  *wb_layer;
	/** @improc: post image processor */
	struct komeda_improc *improc;
	/** @ctrlr: timing controller */
	struct komeda_timing_ctrlr *ctrlr;
	/** @funcs: chip private pipeline functions */
	const struct komeda_pipeline_funcs *funcs;

	/** @of_node: pipeline dt node */
	struct device_node *of_node;
	/** @of_output_port: pipeline output port */
	struct device_node *of_output_port;
	/** @of_output_dev: output connector device node */
	struct device_node *of_output_dev;
};

/**
 * struct komeda_pipeline_state
 *
 * NOTE:
 * Unlike the pipeline, pipeline_state doesn’t gather any component_state
 * into it. It because all component will be managed by drm_atomic_state.
 */
struct komeda_pipeline_state {
	/** @obj: tracking pipeline_state by drm_atomic_state */
	struct drm_private_state obj;
	/** @pipe: backpointer to the pipeline */
	struct komeda_pipeline *pipe;
	/** @crtc: currently bound crtc */
	struct drm_crtc *crtc;
	/**
	 * @active_comps:
	 *
	 * bitmask - BIT(component->id) of active components
	 */
	u32 active_comps;
};

#define to_layer(c)	container_of(c, struct komeda_layer, base)
#define to_compiz(c)	container_of(c, struct komeda_compiz, base)
#define to_scaler(c)	container_of(c, struct komeda_scaler, base)
#define to_splitter(c)	container_of(c, struct komeda_splitter, base)
#define to_merger(c)	container_of(c, struct komeda_merger, base)
#define to_improc(c)	container_of(c, struct komeda_improc, base)
#define to_ctrlr(c)	container_of(c, struct komeda_timing_ctrlr, base)

#define to_layer_st(c)	container_of(c, struct komeda_layer_state, base)
#define to_compiz_st(c)	container_of(c, struct komeda_compiz_state, base)
#define to_scaler_st(c)	container_of(c, struct komeda_scaler_state, base)
#define to_splitter_st(c) container_of(c, struct komeda_splitter_state, base)
#define to_merger_st(c)	container_of(c, struct komeda_merger_state, base)
#define to_improc_st(c)	container_of(c, struct komeda_improc_state, base)
#define to_ctrlr_st(c)	container_of(c, struct komeda_timing_ctrlr_state, base)

#define priv_to_comp_st(o) container_of(o, struct komeda_component_state, obj)
#define priv_to_pipe_st(o) container_of(o, struct komeda_pipeline_state, obj)

/* pipeline APIs */
struct komeda_pipeline *
komeda_pipeline_add(struct komeda_dev *mdev, size_t size,
		    const struct komeda_pipeline_funcs *funcs);
void komeda_pipeline_destroy(struct komeda_dev *mdev,
			     struct komeda_pipeline *pipe);
struct komeda_pipeline *
komeda_pipeline_get_slave(struct komeda_pipeline *master);
int komeda_assemble_pipelines(struct komeda_dev *mdev);
struct komeda_component *
komeda_pipeline_get_component(struct komeda_pipeline *pipe, int id);
struct komeda_component *
komeda_pipeline_get_first_component(struct komeda_pipeline *pipe,
				    u32 comp_mask);

void komeda_pipeline_dump_register(struct komeda_pipeline *pipe,
				   struct seq_file *sf);

/* component APIs */
struct komeda_component *
komeda_component_add(struct komeda_pipeline *pipe,
		     size_t comp_sz, u32 id, u32 hw_id,
		     const struct komeda_component_funcs *funcs,
		     u8 max_active_inputs, u32 supported_inputs,
		     u8 max_active_outputs, u32 __iomem *reg,
		     const char *name_fmt, ...);

void komeda_component_destroy(struct komeda_dev *mdev,
			      struct komeda_component *c);

static inline struct komeda_component *
komeda_component_pickup_output(struct komeda_component *c, u32 avail_comps)
{
	u32 avail_inputs = c->supported_outputs & (avail_comps);

	return komeda_pipeline_get_first_component(c->pipeline, avail_inputs);
}

struct komeda_plane_state;
struct komeda_crtc_state;
struct komeda_crtc;

void pipeline_composition_size(struct komeda_crtc_state *kcrtc_st,
			       u16 *hsize, u16 *vsize);

int komeda_build_layer_data_flow(struct komeda_layer *layer,
				 struct komeda_plane_state *kplane_st,
				 struct komeda_crtc_state *kcrtc_st,
				 struct komeda_data_flow_cfg *dflow);
int komeda_build_wb_data_flow(struct komeda_layer *wb_layer,
			      struct drm_connector_state *conn_st,
			      struct komeda_crtc_state *kcrtc_st,
			      struct komeda_data_flow_cfg *dflow);
int komeda_build_display_data_flow(struct komeda_crtc *kcrtc,
				   struct komeda_crtc_state *kcrtc_st);

int komeda_build_layer_split_data_flow(struct komeda_layer *left,
				       struct komeda_plane_state *kplane_st,
				       struct komeda_crtc_state *kcrtc_st,
				       struct komeda_data_flow_cfg *dflow);
int komeda_build_wb_split_data_flow(struct komeda_layer *wb_layer,
				    struct drm_connector_state *conn_st,
				    struct komeda_crtc_state *kcrtc_st,
				    struct komeda_data_flow_cfg *dflow);

int komeda_release_unclaimed_resources(struct komeda_pipeline *pipe,
				       struct komeda_crtc_state *kcrtc_st);

struct komeda_pipeline_state *
komeda_pipeline_get_old_state(struct komeda_pipeline *pipe,
			      struct drm_atomic_state *state);
void komeda_pipeline_disable(struct komeda_pipeline *pipe,
			     struct drm_atomic_state *old_state);
void komeda_pipeline_update(struct komeda_pipeline *pipe,
			    struct drm_atomic_state *old_state);

void komeda_complete_data_flow_cfg(struct komeda_layer *layer,
				   struct komeda_data_flow_cfg *dflow,
				   struct drm_framebuffer *fb);

#endif /* _KOMEDA_PIPELINE_H_*/
