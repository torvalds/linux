// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include <assert_support.h>
#include <ia_css_frame_public.h>
#include <ia_css_frame.h>
#include <ia_css_binary.h>
#define IA_CSS_INCLUDE_CONFIGURATIONS
#include "ia_css_isp_configs.h"
#include "isp.h"
#include "ia_css_ref.host.h"

int ia_css_ref_config(struct sh_css_isp_ref_isp_config *to,
		      const struct ia_css_ref_configuration  *from,
		      unsigned int size)
{
	unsigned int elems_a = ISP_VEC_NELEMS, i;
	int ret;

	if (from->ref_frames[0]) {
		ret = ia_css_dma_configure_from_info(&to->port_b, &from->ref_frames[0]->frame_info);
		if (ret)
			return ret;
		to->width_a_over_b = elems_a / to->port_b.elems;
		to->dvs_frame_delay = from->dvs_frame_delay;
	} else {
		to->width_a_over_b = 1;
		to->dvs_frame_delay = 0;
		to->port_b.elems = elems_a;
	}
	for (i = 0; i < MAX_NUM_VIDEO_DELAY_FRAMES; i++) {
		if (from->ref_frames[i]) {
			to->ref_frame_addr_y[i] = from->ref_frames[i]->data +
						  from->ref_frames[i]->planes.yuv.y.offset;
			to->ref_frame_addr_c[i] = from->ref_frames[i]->data +
						  from->ref_frames[i]->planes.yuv.u.offset;
		} else {
			to->ref_frame_addr_y[i] = 0;
			to->ref_frame_addr_c[i] = 0;
		}
	}

	/* Assume divisiblity here, may need to generalize to fixed point. */
	if (elems_a % to->port_b.elems != 0)
		return -EINVAL;

	return 0;
}

int ia_css_ref_configure(const struct ia_css_binary        *binary,
			 const struct ia_css_frame * const *ref_frames,
			 const uint32_t dvs_frame_delay)
{
	struct ia_css_ref_configuration config;
	unsigned int i;

	for (i = 0; i < MAX_NUM_VIDEO_DELAY_FRAMES; i++)
		config.ref_frames[i] = ref_frames[i];

	config.dvs_frame_delay = dvs_frame_delay;

	return ia_css_configure_ref(binary, &config);
}

void
ia_css_init_ref_state(
    struct sh_css_isp_ref_dmem_state *state,
    unsigned int size)
{
	(void)size;
	assert(MAX_NUM_VIDEO_DELAY_FRAMES >= 2);
	state->ref_in_buf_idx = 0;
	state->ref_out_buf_idx = 1;
}
