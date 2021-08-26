/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __DC_HUBBUB_DCN20_H__
#define __DC_HUBBUB_DCN20_H__

#include "dcn10/dcn10_hubbub.h"
#include "dcn20_vmid.h"

#define HUBBUB_REG_LIST_DCN20_COMMON()\
	HUBBUB_REG_LIST_DCN_COMMON(), \
	SR(DCHUBBUB_CRC_CTRL), \
	SR(DCN_VM_FB_LOCATION_BASE),\
	SR(DCN_VM_FB_LOCATION_TOP),\
	SR(DCN_VM_FB_OFFSET),\
	SR(DCN_VM_AGP_BOT),\
	SR(DCN_VM_AGP_TOP),\
	SR(DCN_VM_AGP_BASE)

#define TO_DCN20_HUBBUB(hubbub)\
	container_of(hubbub, struct dcn20_hubbub, base)

#define HUBBUB_REG_LIST_DCN20_COMMON()\
	HUBBUB_REG_LIST_DCN_COMMON(), \
	SR(DCHUBBUB_CRC_CTRL), \
	SR(DCN_VM_FB_LOCATION_BASE),\
	SR(DCN_VM_FB_LOCATION_TOP),\
	SR(DCN_VM_FB_OFFSET),\
	SR(DCN_VM_AGP_BOT),\
	SR(DCN_VM_AGP_TOP),\
	SR(DCN_VM_AGP_BASE)

#define HUBBUB_REG_LIST_DCN20(id)\
	HUBBUB_REG_LIST_DCN20_COMMON(), \
	HUBBUB_SR_WATERMARK_REG_LIST(), \
	HUBBUB_VM_REG_LIST(),\
	SR(DCN_VM_PROTECTION_FAULT_DEFAULT_ADDR_MSB),\
	SR(DCN_VM_PROTECTION_FAULT_DEFAULT_ADDR_LSB)


#define HUBBUB_MASK_SH_LIST_DCN20(mask_sh)\
	HUBBUB_MASK_SH_LIST_DCN_COMMON(mask_sh), \
	HUBBUB_MASK_SH_LIST_STUTTER(mask_sh), \
	HUBBUB_SF(DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_REFDIV, mask_sh), \
	HUBBUB_SF(DCN_VM_FB_LOCATION_BASE, FB_BASE, mask_sh), \
	HUBBUB_SF(DCN_VM_FB_LOCATION_TOP, FB_TOP, mask_sh), \
	HUBBUB_SF(DCN_VM_FB_OFFSET, FB_OFFSET, mask_sh), \
	HUBBUB_SF(DCN_VM_AGP_BOT, AGP_BOT, mask_sh), \
	HUBBUB_SF(DCN_VM_AGP_TOP, AGP_TOP, mask_sh), \
	HUBBUB_SF(DCN_VM_AGP_BASE, AGP_BASE, mask_sh), \
	HUBBUB_SF(DCN_VM_PROTECTION_FAULT_DEFAULT_ADDR_MSB, DCN_VM_PROTECTION_FAULT_DEFAULT_ADDR_MSB, mask_sh), \
	HUBBUB_SF(DCN_VM_PROTECTION_FAULT_DEFAULT_ADDR_LSB, DCN_VM_PROTECTION_FAULT_DEFAULT_ADDR_LSB, mask_sh)

struct dcn20_hubbub {
	struct hubbub base;
	const struct dcn_hubbub_registers *regs;
	const struct dcn_hubbub_shift *shifts;
	const struct dcn_hubbub_mask *masks;
	unsigned int debug_test_index_pstate;
	struct dcn_watermark_set watermarks;
	int num_vmid;
	struct dcn20_vmid vmid[16];
	unsigned int detile_buf_size;
	unsigned int crb_size_segs;
	unsigned int compbuf_size_segments;
	unsigned int pixel_chunk_size;
	unsigned int det0_size;
	unsigned int det1_size;
	unsigned int det2_size;
	unsigned int det3_size;
};

void hubbub2_construct(struct dcn20_hubbub *hubbub,
	struct dc_context *ctx,
	const struct dcn_hubbub_registers *hubbub_regs,
	const struct dcn_hubbub_shift *hubbub_shift,
	const struct dcn_hubbub_mask *hubbub_mask);

bool hubbub2_dcc_support_swizzle(
		enum swizzle_mode_values swizzle,
		unsigned int bytes_per_element,
		enum segment_order *segment_order_horz,
		enum segment_order *segment_order_vert);

bool hubbub2_dcc_support_pixel_format(
		enum surface_pixel_format format,
		unsigned int *bytes_per_element);

bool hubbub2_get_dcc_compression_cap(struct hubbub *hubbub,
		const struct dc_dcc_surface_param *input,
		struct dc_surface_dcc_cap *output);

bool hubbub2_initialize_vmids(struct hubbub *hubbub,
		const struct dc_dcc_surface_param *input,
		struct dc_surface_dcc_cap *output);

int hubbub2_init_dchub_sys_ctx(struct hubbub *hubbub,
		struct dcn_hubbub_phys_addr_config *pa_config);
void hubbub2_init_vm_ctx(struct hubbub *hubbub,
		struct dcn_hubbub_virt_addr_config *va_config,
		int vmid);
void hubbub2_update_dchub(struct hubbub *hubbub,
		struct dchub_init_data *dh_data);

void hubbub2_get_dchub_ref_freq(struct hubbub *hubbub,
		unsigned int dccg_ref_freq_inKhz,
		unsigned int *dchub_ref_freq_inKhz);

void hubbub2_wm_read_state(struct hubbub *hubbub,
		struct dcn_hubbub_wm *wm);

#endif
