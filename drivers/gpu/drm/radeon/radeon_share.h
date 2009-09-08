/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#ifndef __RADEON_SHARE_H__
#define __RADEON_SHARE_H__

/* Common */
struct radeon_device;
struct radeon_cs_parser;
int radeon_clocks_init(struct radeon_device *rdev);
void radeon_clocks_fini(struct radeon_device *rdev);
void radeon_scratch_init(struct radeon_device *rdev);
void radeon_surface_init(struct radeon_device *rdev);
int radeon_cs_parser_init(struct radeon_cs_parser *p, void *data);


/* R100, RV100, RS100, RV200, RS200, R200, RV250, RS300, RV280 */
void r100_vram_init_sizes(struct radeon_device *rdev);


/* R300, R350, RV350, RV380 */
struct r300_asic {
	const unsigned	*reg_safe_bm;
	unsigned	reg_safe_bm_size;
};


/* RS690, RS740 */
void rs690_line_buffer_adjust(struct radeon_device *rdev,
			      struct drm_display_mode *mode1,
			      struct drm_display_mode *mode2);


/* RV515 */
void rv515_bandwidth_avivo_update(struct radeon_device *rdev);


/* R600, RV610, RV630, RV620, RV635, RV670, RS780, RS880 */
bool r600_card_posted(struct radeon_device *rdev);
void r600_cp_stop(struct radeon_device *rdev);
void r600_ring_init(struct radeon_device *rdev, unsigned ring_size);
int r600_cp_resume(struct radeon_device *rdev);
int r600_count_pipe_bits(uint32_t val);
int r600_gart_clear_page(struct radeon_device *rdev, int i);
int r600_mc_wait_for_idle(struct radeon_device *rdev);
void r600_pcie_gart_tlb_flush(struct radeon_device *rdev);
int r600_ib_test(struct radeon_device *rdev);
int r600_ring_test(struct radeon_device *rdev);
int r600_wb_init(struct radeon_device *rdev);
void r600_wb_fini(struct radeon_device *rdev);
void r600_scratch_init(struct radeon_device *rdev);
int r600_blit_init(struct radeon_device *rdev);
void r600_blit_fini(struct radeon_device *rdev);
int r600_cp_init_microcode(struct radeon_device *rdev);
struct r600_asic {
	unsigned max_pipes;
	unsigned max_tile_pipes;
	unsigned max_simds;
	unsigned max_backends;
	unsigned max_gprs;
	unsigned max_threads;
	unsigned max_stack_entries;
	unsigned max_hw_contexts;
	unsigned max_gs_threads;
	unsigned sx_max_export_size;
	unsigned sx_max_export_pos_size;
	unsigned sx_max_export_smx_size;
	unsigned sq_num_cf_insts;
};

/* RV770, RV7300, RV710 */
struct rv770_asic {
	unsigned max_pipes;
	unsigned max_tile_pipes;
	unsigned max_simds;
	unsigned max_backends;
	unsigned max_gprs;
	unsigned max_threads;
	unsigned max_stack_entries;
	unsigned max_hw_contexts;
	unsigned max_gs_threads;
	unsigned sx_max_export_size;
	unsigned sx_max_export_pos_size;
	unsigned sx_max_export_smx_size;
	unsigned sq_num_cf_insts;
	unsigned sx_num_of_sets;
	unsigned sc_prim_fifo_size;
	unsigned sc_hiz_tile_fifo_size;
	unsigned sc_earlyz_tile_fifo_fize;
};

#endif
