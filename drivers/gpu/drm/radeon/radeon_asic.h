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
#ifndef __RADEON_ASIC_H__
#define __RADEON_ASIC_H__

/*
 * common functions
 */
uint32_t radeon_legacy_get_engine_clock(struct radeon_device *rdev);
void radeon_legacy_set_engine_clock(struct radeon_device *rdev, uint32_t eng_clock);
uint32_t radeon_legacy_get_memory_clock(struct radeon_device *rdev);
void radeon_legacy_set_clock_gating(struct radeon_device *rdev, int enable);

uint32_t radeon_atom_get_engine_clock(struct radeon_device *rdev);
void radeon_atom_set_engine_clock(struct radeon_device *rdev, uint32_t eng_clock);
uint32_t radeon_atom_get_memory_clock(struct radeon_device *rdev);
void radeon_atom_set_memory_clock(struct radeon_device *rdev, uint32_t mem_clock);
void radeon_atom_set_clock_gating(struct radeon_device *rdev, int enable);

/*
 * r100,rv100,rs100,rv200,rs200
 */
struct r100_mc_save {
	u32	GENMO_WT;
	u32	CRTC_EXT_CNTL;
	u32	CRTC_GEN_CNTL;
	u32	CRTC2_GEN_CNTL;
	u32	CUR_OFFSET;
	u32	CUR2_OFFSET;
};
int r100_init(struct radeon_device *rdev);
void r100_fini(struct radeon_device *rdev);
int r100_suspend(struct radeon_device *rdev);
int r100_resume(struct radeon_device *rdev);
void r100_vga_set_state(struct radeon_device *rdev, bool state);
bool r100_gpu_is_lockup(struct radeon_device *rdev);
int r100_asic_reset(struct radeon_device *rdev);
u32 r100_get_vblank_counter(struct radeon_device *rdev, int crtc);
void r100_pci_gart_tlb_flush(struct radeon_device *rdev);
int r100_pci_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr);
void r100_cp_commit(struct radeon_device *rdev);
void r100_ring_start(struct radeon_device *rdev);
int r100_irq_set(struct radeon_device *rdev);
int r100_irq_process(struct radeon_device *rdev);
void r100_fence_ring_emit(struct radeon_device *rdev,
			  struct radeon_fence *fence);
int r100_cs_parse(struct radeon_cs_parser *p);
void r100_pll_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
uint32_t r100_pll_rreg(struct radeon_device *rdev, uint32_t reg);
int r100_copy_blit(struct radeon_device *rdev,
		   uint64_t src_offset,
		   uint64_t dst_offset,
		   unsigned num_gpu_pages,
		   struct radeon_fence *fence);
int r100_set_surface_reg(struct radeon_device *rdev, int reg,
			 uint32_t tiling_flags, uint32_t pitch,
			 uint32_t offset, uint32_t obj_size);
void r100_clear_surface_reg(struct radeon_device *rdev, int reg);
void r100_bandwidth_update(struct radeon_device *rdev);
void r100_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
int r100_ring_test(struct radeon_device *rdev);
void r100_hpd_init(struct radeon_device *rdev);
void r100_hpd_fini(struct radeon_device *rdev);
bool r100_hpd_sense(struct radeon_device *rdev, enum radeon_hpd_id hpd);
void r100_hpd_set_polarity(struct radeon_device *rdev,
			   enum radeon_hpd_id hpd);
int r100_debugfs_rbbm_init(struct radeon_device *rdev);
int r100_debugfs_cp_init(struct radeon_device *rdev);
void r100_cp_disable(struct radeon_device *rdev);
int r100_cp_init(struct radeon_device *rdev, unsigned ring_size);
void r100_cp_fini(struct radeon_device *rdev);
int r100_pci_gart_init(struct radeon_device *rdev);
void r100_pci_gart_fini(struct radeon_device *rdev);
int r100_pci_gart_enable(struct radeon_device *rdev);
void r100_pci_gart_disable(struct radeon_device *rdev);
int r100_debugfs_mc_info_init(struct radeon_device *rdev);
int r100_gui_wait_for_idle(struct radeon_device *rdev);
void r100_gpu_lockup_update(struct r100_gpu_lockup *lockup,
			    struct radeon_cp *cp);
bool r100_gpu_cp_is_lockup(struct radeon_device *rdev,
			   struct r100_gpu_lockup *lockup,
			   struct radeon_cp *cp);
void r100_ib_fini(struct radeon_device *rdev);
int r100_ib_init(struct radeon_device *rdev);
void r100_irq_disable(struct radeon_device *rdev);
void r100_mc_stop(struct radeon_device *rdev, struct r100_mc_save *save);
void r100_mc_resume(struct radeon_device *rdev, struct r100_mc_save *save);
void r100_vram_init_sizes(struct radeon_device *rdev);
int r100_cp_reset(struct radeon_device *rdev);
void r100_vga_render_disable(struct radeon_device *rdev);
void r100_restore_sanity(struct radeon_device *rdev);
int r100_cs_track_check_pkt3_indx_buffer(struct radeon_cs_parser *p,
					 struct radeon_cs_packet *pkt,
					 struct radeon_bo *robj);
int r100_cs_parse_packet0(struct radeon_cs_parser *p,
			  struct radeon_cs_packet *pkt,
			  const unsigned *auth, unsigned n,
			  radeon_packet0_check_t check);
int r100_cs_packet_parse(struct radeon_cs_parser *p,
			 struct radeon_cs_packet *pkt,
			 unsigned idx);
void r100_enable_bm(struct radeon_device *rdev);
void r100_set_common_regs(struct radeon_device *rdev);
void r100_bm_disable(struct radeon_device *rdev);
extern bool r100_gui_idle(struct radeon_device *rdev);
extern void r100_pm_misc(struct radeon_device *rdev);
extern void r100_pm_prepare(struct radeon_device *rdev);
extern void r100_pm_finish(struct radeon_device *rdev);
extern void r100_pm_init_profile(struct radeon_device *rdev);
extern void r100_pm_get_dynpm_state(struct radeon_device *rdev);
extern void r100_pre_page_flip(struct radeon_device *rdev, int crtc);
extern u32 r100_page_flip(struct radeon_device *rdev, int crtc, u64 crtc_base);
extern void r100_post_page_flip(struct radeon_device *rdev, int crtc);

/*
 * r200,rv250,rs300,rv280
 */
extern int r200_copy_dma(struct radeon_device *rdev,
			 uint64_t src_offset,
			 uint64_t dst_offset,
			 unsigned num_gpu_pages,
			 struct radeon_fence *fence);
void r200_set_safe_registers(struct radeon_device *rdev);

/*
 * r300,r350,rv350,rv380
 */
extern int r300_init(struct radeon_device *rdev);
extern void r300_fini(struct radeon_device *rdev);
extern int r300_suspend(struct radeon_device *rdev);
extern int r300_resume(struct radeon_device *rdev);
extern bool r300_gpu_is_lockup(struct radeon_device *rdev);
extern int r300_asic_reset(struct radeon_device *rdev);
extern void r300_ring_start(struct radeon_device *rdev);
extern void r300_fence_ring_emit(struct radeon_device *rdev,
				struct radeon_fence *fence);
extern int r300_cs_parse(struct radeon_cs_parser *p);
extern void rv370_pcie_gart_tlb_flush(struct radeon_device *rdev);
extern int rv370_pcie_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr);
extern void rv370_set_pcie_lanes(struct radeon_device *rdev, int lanes);
extern int rv370_get_pcie_lanes(struct radeon_device *rdev);
extern void r300_set_reg_safe(struct radeon_device *rdev);
extern void r300_mc_program(struct radeon_device *rdev);
extern void r300_mc_init(struct radeon_device *rdev);
extern void r300_clock_startup(struct radeon_device *rdev);
extern int r300_mc_wait_for_idle(struct radeon_device *rdev);
extern int rv370_pcie_gart_init(struct radeon_device *rdev);
extern void rv370_pcie_gart_fini(struct radeon_device *rdev);
extern int rv370_pcie_gart_enable(struct radeon_device *rdev);
extern void rv370_pcie_gart_disable(struct radeon_device *rdev);

/*
 * r420,r423,rv410
 */
extern int r420_init(struct radeon_device *rdev);
extern void r420_fini(struct radeon_device *rdev);
extern int r420_suspend(struct radeon_device *rdev);
extern int r420_resume(struct radeon_device *rdev);
extern void r420_pm_init_profile(struct radeon_device *rdev);
extern u32 r420_mc_rreg(struct radeon_device *rdev, u32 reg);
extern void r420_mc_wreg(struct radeon_device *rdev, u32 reg, u32 v);
extern int r420_debugfs_pipes_info_init(struct radeon_device *rdev);
extern void r420_pipes_init(struct radeon_device *rdev);

/*
 * rs400,rs480
 */
extern int rs400_init(struct radeon_device *rdev);
extern void rs400_fini(struct radeon_device *rdev);
extern int rs400_suspend(struct radeon_device *rdev);
extern int rs400_resume(struct radeon_device *rdev);
void rs400_gart_tlb_flush(struct radeon_device *rdev);
int rs400_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr);
uint32_t rs400_mc_rreg(struct radeon_device *rdev, uint32_t reg);
void rs400_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
int rs400_gart_init(struct radeon_device *rdev);
int rs400_gart_enable(struct radeon_device *rdev);
void rs400_gart_adjust_size(struct radeon_device *rdev);
void rs400_gart_disable(struct radeon_device *rdev);
void rs400_gart_fini(struct radeon_device *rdev);

/*
 * rs600.
 */
extern int rs600_asic_reset(struct radeon_device *rdev);
extern int rs600_init(struct radeon_device *rdev);
extern void rs600_fini(struct radeon_device *rdev);
extern int rs600_suspend(struct radeon_device *rdev);
extern int rs600_resume(struct radeon_device *rdev);
int rs600_irq_set(struct radeon_device *rdev);
int rs600_irq_process(struct radeon_device *rdev);
void rs600_irq_disable(struct radeon_device *rdev);
u32 rs600_get_vblank_counter(struct radeon_device *rdev, int crtc);
void rs600_gart_tlb_flush(struct radeon_device *rdev);
int rs600_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr);
uint32_t rs600_mc_rreg(struct radeon_device *rdev, uint32_t reg);
void rs600_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void rs600_bandwidth_update(struct radeon_device *rdev);
void rs600_hpd_init(struct radeon_device *rdev);
void rs600_hpd_fini(struct radeon_device *rdev);
bool rs600_hpd_sense(struct radeon_device *rdev, enum radeon_hpd_id hpd);
void rs600_hpd_set_polarity(struct radeon_device *rdev,
			    enum radeon_hpd_id hpd);
extern void rs600_pm_misc(struct radeon_device *rdev);
extern void rs600_pm_prepare(struct radeon_device *rdev);
extern void rs600_pm_finish(struct radeon_device *rdev);
extern void rs600_pre_page_flip(struct radeon_device *rdev, int crtc);
extern u32 rs600_page_flip(struct radeon_device *rdev, int crtc, u64 crtc_base);
extern void rs600_post_page_flip(struct radeon_device *rdev, int crtc);
void rs600_set_safe_registers(struct radeon_device *rdev);


/*
 * rs690,rs740
 */
int rs690_init(struct radeon_device *rdev);
void rs690_fini(struct radeon_device *rdev);
int rs690_resume(struct radeon_device *rdev);
int rs690_suspend(struct radeon_device *rdev);
uint32_t rs690_mc_rreg(struct radeon_device *rdev, uint32_t reg);
void rs690_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void rs690_bandwidth_update(struct radeon_device *rdev);
void rs690_line_buffer_adjust(struct radeon_device *rdev,
					struct drm_display_mode *mode1,
					struct drm_display_mode *mode2);

/*
 * rv515
 */
struct rv515_mc_save {
	u32 d1vga_control;
	u32 d2vga_control;
	u32 vga_render_control;
	u32 vga_hdp_control;
	u32 d1crtc_control;
	u32 d2crtc_control;
};
int rv515_init(struct radeon_device *rdev);
void rv515_fini(struct radeon_device *rdev);
uint32_t rv515_mc_rreg(struct radeon_device *rdev, uint32_t reg);
void rv515_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void rv515_ring_start(struct radeon_device *rdev);
void rv515_bandwidth_update(struct radeon_device *rdev);
int rv515_resume(struct radeon_device *rdev);
int rv515_suspend(struct radeon_device *rdev);
void rv515_bandwidth_avivo_update(struct radeon_device *rdev);
void rv515_vga_render_disable(struct radeon_device *rdev);
void rv515_set_safe_registers(struct radeon_device *rdev);
void rv515_mc_stop(struct radeon_device *rdev, struct rv515_mc_save *save);
void rv515_mc_resume(struct radeon_device *rdev, struct rv515_mc_save *save);
void rv515_clock_startup(struct radeon_device *rdev);
void rv515_debugfs(struct radeon_device *rdev);


/*
 * r520,rv530,rv560,rv570,r580
 */
int r520_init(struct radeon_device *rdev);
int r520_resume(struct radeon_device *rdev);

/*
 * r600,rv610,rv630,rv620,rv635,rv670,rs780,rs880
 */
int r600_init(struct radeon_device *rdev);
void r600_fini(struct radeon_device *rdev);
int r600_suspend(struct radeon_device *rdev);
int r600_resume(struct radeon_device *rdev);
void r600_vga_set_state(struct radeon_device *rdev, bool state);
int r600_wb_init(struct radeon_device *rdev);
void r600_wb_fini(struct radeon_device *rdev);
void r600_cp_commit(struct radeon_device *rdev);
void r600_pcie_gart_tlb_flush(struct radeon_device *rdev);
uint32_t r600_pciep_rreg(struct radeon_device *rdev, uint32_t reg);
void r600_pciep_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
int r600_cs_parse(struct radeon_cs_parser *p);
void r600_fence_ring_emit(struct radeon_device *rdev,
			  struct radeon_fence *fence);
bool r600_gpu_is_lockup(struct radeon_device *rdev);
int r600_asic_reset(struct radeon_device *rdev);
int r600_set_surface_reg(struct radeon_device *rdev, int reg,
			 uint32_t tiling_flags, uint32_t pitch,
			 uint32_t offset, uint32_t obj_size);
void r600_clear_surface_reg(struct radeon_device *rdev, int reg);
int r600_ib_test(struct radeon_device *rdev);
void r600_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
int r600_ring_test(struct radeon_device *rdev);
int r600_copy_blit(struct radeon_device *rdev,
		   uint64_t src_offset, uint64_t dst_offset,
		   unsigned num_gpu_pages, struct radeon_fence *fence);
void r600_hpd_init(struct radeon_device *rdev);
void r600_hpd_fini(struct radeon_device *rdev);
bool r600_hpd_sense(struct radeon_device *rdev, enum radeon_hpd_id hpd);
void r600_hpd_set_polarity(struct radeon_device *rdev,
			   enum radeon_hpd_id hpd);
extern void r600_ioctl_wait_idle(struct radeon_device *rdev, struct radeon_bo *bo);
extern bool r600_gui_idle(struct radeon_device *rdev);
extern void r600_pm_misc(struct radeon_device *rdev);
extern void r600_pm_init_profile(struct radeon_device *rdev);
extern void rs780_pm_init_profile(struct radeon_device *rdev);
extern void r600_pm_get_dynpm_state(struct radeon_device *rdev);
extern void r600_set_pcie_lanes(struct radeon_device *rdev, int lanes);
extern int r600_get_pcie_lanes(struct radeon_device *rdev);
bool r600_card_posted(struct radeon_device *rdev);
void r600_cp_stop(struct radeon_device *rdev);
int r600_cp_start(struct radeon_device *rdev);
void r600_ring_init(struct radeon_device *rdev, unsigned ring_size);
int r600_cp_resume(struct radeon_device *rdev);
void r600_cp_fini(struct radeon_device *rdev);
int r600_count_pipe_bits(uint32_t val);
int r600_mc_wait_for_idle(struct radeon_device *rdev);
int r600_pcie_gart_init(struct radeon_device *rdev);
void r600_scratch_init(struct radeon_device *rdev);
int r600_blit_init(struct radeon_device *rdev);
void r600_blit_fini(struct radeon_device *rdev);
int r600_init_microcode(struct radeon_device *rdev);
/* r600 irq */
int r600_irq_process(struct radeon_device *rdev);
int r600_irq_init(struct radeon_device *rdev);
void r600_irq_fini(struct radeon_device *rdev);
void r600_ih_ring_init(struct radeon_device *rdev, unsigned ring_size);
int r600_irq_set(struct radeon_device *rdev);
void r600_irq_suspend(struct radeon_device *rdev);
void r600_disable_interrupts(struct radeon_device *rdev);
void r600_rlc_stop(struct radeon_device *rdev);
/* r600 audio */
int r600_audio_init(struct radeon_device *rdev);
int r600_audio_tmds_index(struct drm_encoder *encoder);
void r600_audio_set_clock(struct drm_encoder *encoder, int clock);
int r600_audio_channels(struct radeon_device *rdev);
int r600_audio_bits_per_sample(struct radeon_device *rdev);
int r600_audio_rate(struct radeon_device *rdev);
uint8_t r600_audio_status_bits(struct radeon_device *rdev);
uint8_t r600_audio_category_code(struct radeon_device *rdev);
void r600_audio_schedule_polling(struct radeon_device *rdev);
void r600_audio_enable_polling(struct drm_encoder *encoder);
void r600_audio_disable_polling(struct drm_encoder *encoder);
void r600_audio_fini(struct radeon_device *rdev);
void r600_hdmi_init(struct drm_encoder *encoder);
int r600_hdmi_buffer_status_changed(struct drm_encoder *encoder);
void r600_hdmi_update_audio_settings(struct drm_encoder *encoder);
/* r600 blit */
int r600_blit_prepare_copy(struct radeon_device *rdev, unsigned num_gpu_pages);
void r600_blit_done_copy(struct radeon_device *rdev, struct radeon_fence *fence);
void r600_kms_blit_copy(struct radeon_device *rdev,
			u64 src_gpu_addr, u64 dst_gpu_addr,
			unsigned num_gpu_pages);

/*
 * rv770,rv730,rv710,rv740
 */
int rv770_init(struct radeon_device *rdev);
void rv770_fini(struct radeon_device *rdev);
int rv770_suspend(struct radeon_device *rdev);
int rv770_resume(struct radeon_device *rdev);
void rv770_pm_misc(struct radeon_device *rdev);
u32 rv770_page_flip(struct radeon_device *rdev, int crtc, u64 crtc_base);
void r700_vram_gtt_location(struct radeon_device *rdev, struct radeon_mc *mc);
void r700_cp_stop(struct radeon_device *rdev);
void r700_cp_fini(struct radeon_device *rdev);

/*
 * evergreen
 */
struct evergreen_mc_save {
	u32 vga_control[6];
	u32 vga_render_control;
	u32 vga_hdp_control;
	u32 crtc_control[6];
};
void evergreen_pcie_gart_tlb_flush(struct radeon_device *rdev);
int evergreen_init(struct radeon_device *rdev);
void evergreen_fini(struct radeon_device *rdev);
int evergreen_suspend(struct radeon_device *rdev);
int evergreen_resume(struct radeon_device *rdev);
bool evergreen_gpu_is_lockup(struct radeon_device *rdev);
int evergreen_asic_reset(struct radeon_device *rdev);
void evergreen_bandwidth_update(struct radeon_device *rdev);
void evergreen_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
void evergreen_hpd_init(struct radeon_device *rdev);
void evergreen_hpd_fini(struct radeon_device *rdev);
bool evergreen_hpd_sense(struct radeon_device *rdev, enum radeon_hpd_id hpd);
void evergreen_hpd_set_polarity(struct radeon_device *rdev,
				enum radeon_hpd_id hpd);
u32 evergreen_get_vblank_counter(struct radeon_device *rdev, int crtc);
int evergreen_irq_set(struct radeon_device *rdev);
int evergreen_irq_process(struct radeon_device *rdev);
extern int evergreen_cs_parse(struct radeon_cs_parser *p);
extern void evergreen_pm_misc(struct radeon_device *rdev);
extern void evergreen_pm_prepare(struct radeon_device *rdev);
extern void evergreen_pm_finish(struct radeon_device *rdev);
extern void sumo_pm_init_profile(struct radeon_device *rdev);
extern void evergreen_pre_page_flip(struct radeon_device *rdev, int crtc);
extern u32 evergreen_page_flip(struct radeon_device *rdev, int crtc, u64 crtc_base);
extern void evergreen_post_page_flip(struct radeon_device *rdev, int crtc);
void evergreen_disable_interrupt_state(struct radeon_device *rdev);
int evergreen_blit_init(struct radeon_device *rdev);

/*
 * cayman
 */
void cayman_pcie_gart_tlb_flush(struct radeon_device *rdev);
int cayman_init(struct radeon_device *rdev);
void cayman_fini(struct radeon_device *rdev);
int cayman_suspend(struct radeon_device *rdev);
int cayman_resume(struct radeon_device *rdev);
bool cayman_gpu_is_lockup(struct radeon_device *rdev);
int cayman_asic_reset(struct radeon_device *rdev);

#endif
