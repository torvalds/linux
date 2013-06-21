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

void atombios_set_backlight_level(struct radeon_encoder *radeon_encoder, u8 level);
u8 atombios_get_backlight_level(struct radeon_encoder *radeon_encoder);
void radeon_legacy_set_backlight_level(struct radeon_encoder *radeon_encoder, u8 level);
u8 radeon_legacy_get_backlight_level(struct radeon_encoder *radeon_encoder);

u32 radeon_ring_generic_get_rptr(struct radeon_device *rdev,
				 struct radeon_ring *ring);
u32 radeon_ring_generic_get_wptr(struct radeon_device *rdev,
				 struct radeon_ring *ring);
void radeon_ring_generic_set_wptr(struct radeon_device *rdev,
				  struct radeon_ring *ring);

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
bool r100_gpu_is_lockup(struct radeon_device *rdev, struct radeon_ring *cp);
int r100_asic_reset(struct radeon_device *rdev);
u32 r100_get_vblank_counter(struct radeon_device *rdev, int crtc);
void r100_pci_gart_tlb_flush(struct radeon_device *rdev);
int r100_pci_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr);
void r100_ring_start(struct radeon_device *rdev, struct radeon_ring *ring);
int r100_irq_set(struct radeon_device *rdev);
int r100_irq_process(struct radeon_device *rdev);
void r100_fence_ring_emit(struct radeon_device *rdev,
			  struct radeon_fence *fence);
void r100_semaphore_ring_emit(struct radeon_device *rdev,
			      struct radeon_ring *cp,
			      struct radeon_semaphore *semaphore,
			      bool emit_wait);
int r100_cs_parse(struct radeon_cs_parser *p);
void r100_pll_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
uint32_t r100_pll_rreg(struct radeon_device *rdev, uint32_t reg);
int r100_copy_blit(struct radeon_device *rdev,
		   uint64_t src_offset,
		   uint64_t dst_offset,
		   unsigned num_gpu_pages,
		   struct radeon_fence **fence);
int r100_set_surface_reg(struct radeon_device *rdev, int reg,
			 uint32_t tiling_flags, uint32_t pitch,
			 uint32_t offset, uint32_t obj_size);
void r100_clear_surface_reg(struct radeon_device *rdev, int reg);
void r100_bandwidth_update(struct radeon_device *rdev);
void r100_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
int r100_ring_test(struct radeon_device *rdev, struct radeon_ring *cp);
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
int r100_ib_test(struct radeon_device *rdev, struct radeon_ring *ring);
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
extern void r100_wait_for_vblank(struct radeon_device *rdev, int crtc);
extern int r100_mc_wait_for_idle(struct radeon_device *rdev);

/*
 * r200,rv250,rs300,rv280
 */
extern int r200_copy_dma(struct radeon_device *rdev,
			 uint64_t src_offset,
			 uint64_t dst_offset,
			 unsigned num_gpu_pages,
			 struct radeon_fence **fence);
void r200_set_safe_registers(struct radeon_device *rdev);

/*
 * r300,r350,rv350,rv380
 */
extern int r300_init(struct radeon_device *rdev);
extern void r300_fini(struct radeon_device *rdev);
extern int r300_suspend(struct radeon_device *rdev);
extern int r300_resume(struct radeon_device *rdev);
extern int r300_asic_reset(struct radeon_device *rdev);
extern void r300_ring_start(struct radeon_device *rdev, struct radeon_ring *ring);
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
extern int r300_mc_wait_for_idle(struct radeon_device *rdev);

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
extern int rs400_mc_wait_for_idle(struct radeon_device *rdev);

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
extern void avivo_wait_for_vblank(struct radeon_device *rdev, int crtc);
extern int rs600_mc_wait_for_idle(struct radeon_device *rdev);

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
extern int rs690_mc_wait_for_idle(struct radeon_device *rdev);

/*
 * rv515
 */
struct rv515_mc_save {
	u32 vga_render_control;
	u32 vga_hdp_control;
	bool crtc_enabled[2];
};

int rv515_init(struct radeon_device *rdev);
void rv515_fini(struct radeon_device *rdev);
uint32_t rv515_mc_rreg(struct radeon_device *rdev, uint32_t reg);
void rv515_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void rv515_ring_start(struct radeon_device *rdev, struct radeon_ring *ring);
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
int rv515_mc_wait_for_idle(struct radeon_device *rdev);

/*
 * r520,rv530,rv560,rv570,r580
 */
int r520_init(struct radeon_device *rdev);
int r520_resume(struct radeon_device *rdev);
int r520_mc_wait_for_idle(struct radeon_device *rdev);

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
void r600_pcie_gart_tlb_flush(struct radeon_device *rdev);
uint32_t r600_pciep_rreg(struct radeon_device *rdev, uint32_t reg);
void r600_pciep_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
int r600_cs_parse(struct radeon_cs_parser *p);
int r600_dma_cs_parse(struct radeon_cs_parser *p);
void r600_fence_ring_emit(struct radeon_device *rdev,
			  struct radeon_fence *fence);
void r600_semaphore_ring_emit(struct radeon_device *rdev,
			      struct radeon_ring *cp,
			      struct radeon_semaphore *semaphore,
			      bool emit_wait);
void r600_dma_fence_ring_emit(struct radeon_device *rdev,
			      struct radeon_fence *fence);
void r600_dma_semaphore_ring_emit(struct radeon_device *rdev,
				  struct radeon_ring *ring,
				  struct radeon_semaphore *semaphore,
				  bool emit_wait);
void r600_dma_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
bool r600_dma_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring);
bool r600_gfx_is_lockup(struct radeon_device *rdev, struct radeon_ring *cp);
int r600_asic_reset(struct radeon_device *rdev);
int r600_set_surface_reg(struct radeon_device *rdev, int reg,
			 uint32_t tiling_flags, uint32_t pitch,
			 uint32_t offset, uint32_t obj_size);
void r600_clear_surface_reg(struct radeon_device *rdev, int reg);
int r600_ib_test(struct radeon_device *rdev, struct radeon_ring *ring);
int r600_dma_ib_test(struct radeon_device *rdev, struct radeon_ring *ring);
void r600_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
int r600_ring_test(struct radeon_device *rdev, struct radeon_ring *cp);
int r600_dma_ring_test(struct radeon_device *rdev, struct radeon_ring *cp);
int r600_uvd_ring_test(struct radeon_device *rdev, struct radeon_ring *ring);
int r600_copy_cpdma(struct radeon_device *rdev,
		    uint64_t src_offset, uint64_t dst_offset,
		    unsigned num_gpu_pages, struct radeon_fence **fence);
int r600_copy_dma(struct radeon_device *rdev,
		  uint64_t src_offset, uint64_t dst_offset,
		  unsigned num_gpu_pages, struct radeon_fence **fence);
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
extern uint32_t rs780_mc_rreg(struct radeon_device *rdev, uint32_t reg);
extern void rs780_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
extern void r600_pm_get_dynpm_state(struct radeon_device *rdev);
extern void r600_set_pcie_lanes(struct radeon_device *rdev, int lanes);
extern int r600_get_pcie_lanes(struct radeon_device *rdev);
bool r600_card_posted(struct radeon_device *rdev);
void r600_cp_stop(struct radeon_device *rdev);
int r600_cp_start(struct radeon_device *rdev);
void r600_ring_init(struct radeon_device *rdev, struct radeon_ring *cp, unsigned ring_size);
int r600_cp_resume(struct radeon_device *rdev);
void r600_cp_fini(struct radeon_device *rdev);
int r600_count_pipe_bits(uint32_t val);
int r600_mc_wait_for_idle(struct radeon_device *rdev);
int r600_pcie_gart_init(struct radeon_device *rdev);
void r600_scratch_init(struct radeon_device *rdev);
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
struct r600_audio r600_audio_status(struct radeon_device *rdev);
void r600_audio_fini(struct radeon_device *rdev);
int r600_hdmi_buffer_status_changed(struct drm_encoder *encoder);
void r600_hdmi_update_audio_settings(struct drm_encoder *encoder);
void r600_hdmi_enable(struct drm_encoder *encoder, bool enable);
void r600_hdmi_setmode(struct drm_encoder *encoder, struct drm_display_mode *mode);
int r600_mc_wait_for_idle(struct radeon_device *rdev);
u32 r600_get_xclk(struct radeon_device *rdev);
uint64_t r600_get_gpu_clock_counter(struct radeon_device *rdev);
int rv6xx_get_temp(struct radeon_device *rdev);
int r600_dpm_pre_set_power_state(struct radeon_device *rdev);
void r600_dpm_post_set_power_state(struct radeon_device *rdev);
/* rv6xx dpm */
int rv6xx_dpm_init(struct radeon_device *rdev);
int rv6xx_dpm_enable(struct radeon_device *rdev);
void rv6xx_dpm_disable(struct radeon_device *rdev);
int rv6xx_dpm_set_power_state(struct radeon_device *rdev);
void rv6xx_setup_asic(struct radeon_device *rdev);
void rv6xx_dpm_display_configuration_changed(struct radeon_device *rdev);
void rv6xx_dpm_fini(struct radeon_device *rdev);
u32 rv6xx_dpm_get_sclk(struct radeon_device *rdev, bool low);
u32 rv6xx_dpm_get_mclk(struct radeon_device *rdev, bool low);
void rv6xx_dpm_print_power_state(struct radeon_device *rdev,
				 struct radeon_ps *ps);
void rv6xx_dpm_debugfs_print_current_performance_level(struct radeon_device *rdev,
						       struct seq_file *m);
int rv6xx_dpm_force_performance_level(struct radeon_device *rdev,
				      enum radeon_dpm_forced_level level);
/* rs780 dpm */
int rs780_dpm_init(struct radeon_device *rdev);
int rs780_dpm_enable(struct radeon_device *rdev);
void rs780_dpm_disable(struct radeon_device *rdev);
int rs780_dpm_set_power_state(struct radeon_device *rdev);
void rs780_dpm_setup_asic(struct radeon_device *rdev);
void rs780_dpm_display_configuration_changed(struct radeon_device *rdev);
void rs780_dpm_fini(struct radeon_device *rdev);
u32 rs780_dpm_get_sclk(struct radeon_device *rdev, bool low);
u32 rs780_dpm_get_mclk(struct radeon_device *rdev, bool low);
void rs780_dpm_print_power_state(struct radeon_device *rdev,
				 struct radeon_ps *ps);
void rs780_dpm_debugfs_print_current_performance_level(struct radeon_device *rdev,
						       struct seq_file *m);

/* uvd */
int r600_uvd_init(struct radeon_device *rdev);
int r600_uvd_rbc_start(struct radeon_device *rdev);
void r600_uvd_stop(struct radeon_device *rdev);
int r600_uvd_ib_test(struct radeon_device *rdev, struct radeon_ring *ring);
void r600_uvd_fence_emit(struct radeon_device *rdev,
			 struct radeon_fence *fence);
void r600_uvd_semaphore_emit(struct radeon_device *rdev,
			     struct radeon_ring *ring,
			     struct radeon_semaphore *semaphore,
			     bool emit_wait);
void r600_uvd_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);

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
int rv770_copy_dma(struct radeon_device *rdev,
		  uint64_t src_offset, uint64_t dst_offset,
		  unsigned num_gpu_pages,
		   struct radeon_fence **fence);
u32 rv770_get_xclk(struct radeon_device *rdev);
int rv770_uvd_resume(struct radeon_device *rdev);
int rv770_set_uvd_clocks(struct radeon_device *rdev, u32 vclk, u32 dclk);
int rv770_get_temp(struct radeon_device *rdev);
/* rv7xx pm */
int rv770_dpm_init(struct radeon_device *rdev);
int rv770_dpm_enable(struct radeon_device *rdev);
void rv770_dpm_disable(struct radeon_device *rdev);
int rv770_dpm_set_power_state(struct radeon_device *rdev);
void rv770_dpm_setup_asic(struct radeon_device *rdev);
void rv770_dpm_display_configuration_changed(struct radeon_device *rdev);
void rv770_dpm_fini(struct radeon_device *rdev);
u32 rv770_dpm_get_sclk(struct radeon_device *rdev, bool low);
u32 rv770_dpm_get_mclk(struct radeon_device *rdev, bool low);
void rv770_dpm_print_power_state(struct radeon_device *rdev,
				 struct radeon_ps *ps);
void rv770_dpm_debugfs_print_current_performance_level(struct radeon_device *rdev,
						       struct seq_file *m);
int rv770_dpm_force_performance_level(struct radeon_device *rdev,
				      enum radeon_dpm_forced_level level);
bool rv770_dpm_vblank_too_short(struct radeon_device *rdev);

/*
 * evergreen
 */
struct evergreen_mc_save {
	u32 vga_render_control;
	u32 vga_hdp_control;
	bool crtc_enabled[RADEON_MAX_CRTCS];
};

void evergreen_pcie_gart_tlb_flush(struct radeon_device *rdev);
int evergreen_init(struct radeon_device *rdev);
void evergreen_fini(struct radeon_device *rdev);
int evergreen_suspend(struct radeon_device *rdev);
int evergreen_resume(struct radeon_device *rdev);
bool evergreen_gfx_is_lockup(struct radeon_device *rdev, struct radeon_ring *cp);
bool evergreen_dma_is_lockup(struct radeon_device *rdev, struct radeon_ring *cp);
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
extern int evergreen_dma_cs_parse(struct radeon_cs_parser *p);
extern void evergreen_pm_misc(struct radeon_device *rdev);
extern void evergreen_pm_prepare(struct radeon_device *rdev);
extern void evergreen_pm_finish(struct radeon_device *rdev);
extern void sumo_pm_init_profile(struct radeon_device *rdev);
extern void btc_pm_init_profile(struct radeon_device *rdev);
int sumo_set_uvd_clocks(struct radeon_device *rdev, u32 vclk, u32 dclk);
int evergreen_set_uvd_clocks(struct radeon_device *rdev, u32 vclk, u32 dclk);
extern void evergreen_pre_page_flip(struct radeon_device *rdev, int crtc);
extern u32 evergreen_page_flip(struct radeon_device *rdev, int crtc, u64 crtc_base);
extern void evergreen_post_page_flip(struct radeon_device *rdev, int crtc);
extern void dce4_wait_for_vblank(struct radeon_device *rdev, int crtc);
void evergreen_disable_interrupt_state(struct radeon_device *rdev);
int evergreen_mc_wait_for_idle(struct radeon_device *rdev);
void evergreen_dma_fence_ring_emit(struct radeon_device *rdev,
				   struct radeon_fence *fence);
void evergreen_dma_ring_ib_execute(struct radeon_device *rdev,
				   struct radeon_ib *ib);
int evergreen_copy_dma(struct radeon_device *rdev,
		       uint64_t src_offset, uint64_t dst_offset,
		       unsigned num_gpu_pages,
		       struct radeon_fence **fence);
void evergreen_hdmi_enable(struct drm_encoder *encoder, bool enable);
void evergreen_hdmi_setmode(struct drm_encoder *encoder, struct drm_display_mode *mode);
int evergreen_get_temp(struct radeon_device *rdev);
int sumo_get_temp(struct radeon_device *rdev);
int tn_get_temp(struct radeon_device *rdev);
int cypress_dpm_init(struct radeon_device *rdev);
void cypress_dpm_setup_asic(struct radeon_device *rdev);
int cypress_dpm_enable(struct radeon_device *rdev);
void cypress_dpm_disable(struct radeon_device *rdev);
int cypress_dpm_set_power_state(struct radeon_device *rdev);
void cypress_dpm_display_configuration_changed(struct radeon_device *rdev);
void cypress_dpm_fini(struct radeon_device *rdev);
bool cypress_dpm_vblank_too_short(struct radeon_device *rdev);
int btc_dpm_init(struct radeon_device *rdev);
void btc_dpm_setup_asic(struct radeon_device *rdev);
int btc_dpm_enable(struct radeon_device *rdev);
void btc_dpm_disable(struct radeon_device *rdev);
int btc_dpm_pre_set_power_state(struct radeon_device *rdev);
int btc_dpm_set_power_state(struct radeon_device *rdev);
void btc_dpm_post_set_power_state(struct radeon_device *rdev);
void btc_dpm_fini(struct radeon_device *rdev);
u32 btc_dpm_get_sclk(struct radeon_device *rdev, bool low);
u32 btc_dpm_get_mclk(struct radeon_device *rdev, bool low);
bool btc_dpm_vblank_too_short(struct radeon_device *rdev);
int sumo_dpm_init(struct radeon_device *rdev);
int sumo_dpm_enable(struct radeon_device *rdev);
void sumo_dpm_disable(struct radeon_device *rdev);
int sumo_dpm_pre_set_power_state(struct radeon_device *rdev);
int sumo_dpm_set_power_state(struct radeon_device *rdev);
void sumo_dpm_post_set_power_state(struct radeon_device *rdev);
void sumo_dpm_setup_asic(struct radeon_device *rdev);
void sumo_dpm_display_configuration_changed(struct radeon_device *rdev);
void sumo_dpm_fini(struct radeon_device *rdev);
u32 sumo_dpm_get_sclk(struct radeon_device *rdev, bool low);
u32 sumo_dpm_get_mclk(struct radeon_device *rdev, bool low);
void sumo_dpm_print_power_state(struct radeon_device *rdev,
				struct radeon_ps *ps);
void sumo_dpm_debugfs_print_current_performance_level(struct radeon_device *rdev,
						      struct seq_file *m);
int sumo_dpm_force_performance_level(struct radeon_device *rdev,
				     enum radeon_dpm_forced_level level);

/*
 * cayman
 */
void cayman_fence_ring_emit(struct radeon_device *rdev,
			    struct radeon_fence *fence);
void cayman_uvd_semaphore_emit(struct radeon_device *rdev,
			       struct radeon_ring *ring,
			       struct radeon_semaphore *semaphore,
			       bool emit_wait);
void cayman_pcie_gart_tlb_flush(struct radeon_device *rdev);
int cayman_init(struct radeon_device *rdev);
void cayman_fini(struct radeon_device *rdev);
int cayman_suspend(struct radeon_device *rdev);
int cayman_resume(struct radeon_device *rdev);
int cayman_asic_reset(struct radeon_device *rdev);
void cayman_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
int cayman_vm_init(struct radeon_device *rdev);
void cayman_vm_fini(struct radeon_device *rdev);
void cayman_vm_flush(struct radeon_device *rdev, int ridx, struct radeon_vm *vm);
uint32_t cayman_vm_page_flags(struct radeon_device *rdev, uint32_t flags);
void cayman_vm_set_page(struct radeon_device *rdev,
			struct radeon_ib *ib,
			uint64_t pe,
			uint64_t addr, unsigned count,
			uint32_t incr, uint32_t flags);
int evergreen_ib_parse(struct radeon_device *rdev, struct radeon_ib *ib);
int evergreen_dma_ib_parse(struct radeon_device *rdev, struct radeon_ib *ib);
void cayman_dma_ring_ib_execute(struct radeon_device *rdev,
				struct radeon_ib *ib);
bool cayman_gfx_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring);
bool cayman_dma_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring);
void cayman_dma_vm_flush(struct radeon_device *rdev, int ridx, struct radeon_vm *vm);

int ni_dpm_init(struct radeon_device *rdev);
void ni_dpm_setup_asic(struct radeon_device *rdev);
int ni_dpm_enable(struct radeon_device *rdev);
void ni_dpm_disable(struct radeon_device *rdev);
int ni_dpm_pre_set_power_state(struct radeon_device *rdev);
int ni_dpm_set_power_state(struct radeon_device *rdev);
void ni_dpm_post_set_power_state(struct radeon_device *rdev);
void ni_dpm_fini(struct radeon_device *rdev);
u32 ni_dpm_get_sclk(struct radeon_device *rdev, bool low);
u32 ni_dpm_get_mclk(struct radeon_device *rdev, bool low);
void ni_dpm_print_power_state(struct radeon_device *rdev,
			      struct radeon_ps *ps);
void ni_dpm_debugfs_print_current_performance_level(struct radeon_device *rdev,
						    struct seq_file *m);
int ni_dpm_force_performance_level(struct radeon_device *rdev,
				   enum radeon_dpm_forced_level level);
bool ni_dpm_vblank_too_short(struct radeon_device *rdev);
int trinity_dpm_init(struct radeon_device *rdev);
int trinity_dpm_enable(struct radeon_device *rdev);
void trinity_dpm_disable(struct radeon_device *rdev);
int trinity_dpm_pre_set_power_state(struct radeon_device *rdev);
int trinity_dpm_set_power_state(struct radeon_device *rdev);
void trinity_dpm_post_set_power_state(struct radeon_device *rdev);
void trinity_dpm_setup_asic(struct radeon_device *rdev);
void trinity_dpm_display_configuration_changed(struct radeon_device *rdev);
void trinity_dpm_fini(struct radeon_device *rdev);
u32 trinity_dpm_get_sclk(struct radeon_device *rdev, bool low);
u32 trinity_dpm_get_mclk(struct radeon_device *rdev, bool low);
void trinity_dpm_print_power_state(struct radeon_device *rdev,
				   struct radeon_ps *ps);
void trinity_dpm_debugfs_print_current_performance_level(struct radeon_device *rdev,
							 struct seq_file *m);
int trinity_dpm_force_performance_level(struct radeon_device *rdev,
					enum radeon_dpm_forced_level level);

/* DCE6 - SI */
void dce6_bandwidth_update(struct radeon_device *rdev);

/*
 * si
 */
void si_fence_ring_emit(struct radeon_device *rdev,
			struct radeon_fence *fence);
void si_pcie_gart_tlb_flush(struct radeon_device *rdev);
int si_init(struct radeon_device *rdev);
void si_fini(struct radeon_device *rdev);
int si_suspend(struct radeon_device *rdev);
int si_resume(struct radeon_device *rdev);
bool si_gfx_is_lockup(struct radeon_device *rdev, struct radeon_ring *cp);
bool si_dma_is_lockup(struct radeon_device *rdev, struct radeon_ring *cp);
int si_asic_reset(struct radeon_device *rdev);
void si_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
int si_irq_set(struct radeon_device *rdev);
int si_irq_process(struct radeon_device *rdev);
int si_vm_init(struct radeon_device *rdev);
void si_vm_fini(struct radeon_device *rdev);
void si_vm_set_page(struct radeon_device *rdev,
		    struct radeon_ib *ib,
		    uint64_t pe,
		    uint64_t addr, unsigned count,
		    uint32_t incr, uint32_t flags);
void si_vm_flush(struct radeon_device *rdev, int ridx, struct radeon_vm *vm);
int si_ib_parse(struct radeon_device *rdev, struct radeon_ib *ib);
int si_copy_dma(struct radeon_device *rdev,
		uint64_t src_offset, uint64_t dst_offset,
		unsigned num_gpu_pages,
		struct radeon_fence **fence);
void si_dma_vm_flush(struct radeon_device *rdev, int ridx, struct radeon_vm *vm);
u32 si_get_xclk(struct radeon_device *rdev);
uint64_t si_get_gpu_clock_counter(struct radeon_device *rdev);
int si_set_uvd_clocks(struct radeon_device *rdev, u32 vclk, u32 dclk);
int si_get_temp(struct radeon_device *rdev);
int si_dpm_init(struct radeon_device *rdev);
void si_dpm_setup_asic(struct radeon_device *rdev);
int si_dpm_enable(struct radeon_device *rdev);
void si_dpm_disable(struct radeon_device *rdev);
int si_dpm_pre_set_power_state(struct radeon_device *rdev);
int si_dpm_set_power_state(struct radeon_device *rdev);
void si_dpm_post_set_power_state(struct radeon_device *rdev);
void si_dpm_fini(struct radeon_device *rdev);
void si_dpm_display_configuration_changed(struct radeon_device *rdev);
void si_dpm_debugfs_print_current_performance_level(struct radeon_device *rdev,
						    struct seq_file *m);
int si_dpm_force_performance_level(struct radeon_device *rdev,
				   enum radeon_dpm_forced_level level);

/* DCE8 - CIK */
void dce8_bandwidth_update(struct radeon_device *rdev);

/*
 * cik
 */
uint64_t cik_get_gpu_clock_counter(struct radeon_device *rdev);
u32 cik_get_xclk(struct radeon_device *rdev);
uint32_t cik_pciep_rreg(struct radeon_device *rdev, uint32_t reg);
void cik_pciep_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
int cik_set_uvd_clocks(struct radeon_device *rdev, u32 vclk, u32 dclk);
int cik_uvd_resume(struct radeon_device *rdev);
void cik_sdma_fence_ring_emit(struct radeon_device *rdev,
			      struct radeon_fence *fence);
void cik_sdma_semaphore_ring_emit(struct radeon_device *rdev,
				  struct radeon_ring *ring,
				  struct radeon_semaphore *semaphore,
				  bool emit_wait);
void cik_sdma_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
int cik_copy_dma(struct radeon_device *rdev,
		 uint64_t src_offset, uint64_t dst_offset,
		 unsigned num_gpu_pages,
		 struct radeon_fence **fence);
int cik_sdma_ring_test(struct radeon_device *rdev, struct radeon_ring *ring);
int cik_sdma_ib_test(struct radeon_device *rdev, struct radeon_ring *ring);
bool cik_sdma_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring);
void cik_fence_gfx_ring_emit(struct radeon_device *rdev,
			     struct radeon_fence *fence);
void cik_fence_compute_ring_emit(struct radeon_device *rdev,
				 struct radeon_fence *fence);
void cik_semaphore_ring_emit(struct radeon_device *rdev,
			     struct radeon_ring *cp,
			     struct radeon_semaphore *semaphore,
			     bool emit_wait);
void cik_pcie_gart_tlb_flush(struct radeon_device *rdev);
int cik_init(struct radeon_device *rdev);
void cik_fini(struct radeon_device *rdev);
int cik_suspend(struct radeon_device *rdev);
int cik_resume(struct radeon_device *rdev);
bool cik_gfx_is_lockup(struct radeon_device *rdev, struct radeon_ring *cp);
int cik_asic_reset(struct radeon_device *rdev);
void cik_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
int cik_ring_test(struct radeon_device *rdev, struct radeon_ring *ring);
int cik_ib_test(struct radeon_device *rdev, struct radeon_ring *ring);
int cik_irq_set(struct radeon_device *rdev);
int cik_irq_process(struct radeon_device *rdev);
int cik_vm_init(struct radeon_device *rdev);
void cik_vm_fini(struct radeon_device *rdev);
void cik_vm_flush(struct radeon_device *rdev, int ridx, struct radeon_vm *vm);
void cik_vm_set_page(struct radeon_device *rdev,
		     struct radeon_ib *ib,
		     uint64_t pe,
		     uint64_t addr, unsigned count,
		     uint32_t incr, uint32_t flags);
void cik_dma_vm_flush(struct radeon_device *rdev, int ridx, struct radeon_vm *vm);
int cik_ib_parse(struct radeon_device *rdev, struct radeon_ib *ib);
u32 cik_compute_ring_get_rptr(struct radeon_device *rdev,
			      struct radeon_ring *ring);
u32 cik_compute_ring_get_wptr(struct radeon_device *rdev,
			      struct radeon_ring *ring);
void cik_compute_ring_set_wptr(struct radeon_device *rdev,
			       struct radeon_ring *ring);
int ci_get_temp(struct radeon_device *rdev);
int kv_get_temp(struct radeon_device *rdev);

#endif
