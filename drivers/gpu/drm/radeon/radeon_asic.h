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
 * r100,rv100,rs100,rv200,rs200,r200,rv250,rs300,rv280
 */
extern int r100_init(struct radeon_device *rdev);
extern void r100_fini(struct radeon_device *rdev);
extern int r100_suspend(struct radeon_device *rdev);
extern int r100_resume(struct radeon_device *rdev);
uint32_t r100_mm_rreg(struct radeon_device *rdev, uint32_t reg);
void r100_mm_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void r100_vga_set_state(struct radeon_device *rdev, bool state);
int r100_gpu_reset(struct radeon_device *rdev);
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
		   unsigned num_pages,
		   struct radeon_fence *fence);
int r100_set_surface_reg(struct radeon_device *rdev, int reg,
			 uint32_t tiling_flags, uint32_t pitch,
			 uint32_t offset, uint32_t obj_size);
int r100_clear_surface_reg(struct radeon_device *rdev, int reg);
void r100_bandwidth_update(struct radeon_device *rdev);
void r100_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
int r100_ring_test(struct radeon_device *rdev);
void r100_hpd_init(struct radeon_device *rdev);
void r100_hpd_fini(struct radeon_device *rdev);
bool r100_hpd_sense(struct radeon_device *rdev, enum radeon_hpd_id hpd);
void r100_hpd_set_polarity(struct radeon_device *rdev,
			   enum radeon_hpd_id hpd);

static struct radeon_asic r100_asic = {
	.init = &r100_init,
	.fini = &r100_fini,
	.suspend = &r100_suspend,
	.resume = &r100_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_reset = &r100_gpu_reset,
	.gart_tlb_flush = &r100_pci_gart_tlb_flush,
	.gart_set_page = &r100_pci_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r100_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r100_fence_ring_emit,
	.cs_parse = &r100_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = NULL,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_legacy_get_engine_clock,
	.set_engine_clock = &radeon_legacy_set_engine_clock,
	.get_memory_clock = &radeon_legacy_get_memory_clock,
	.set_memory_clock = NULL,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_legacy_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
	.hpd_init = &r100_hpd_init,
	.hpd_fini = &r100_hpd_fini,
	.hpd_sense = &r100_hpd_sense,
	.hpd_set_polarity = &r100_hpd_set_polarity,
};


/*
 * r300,r350,rv350,rv380
 */
extern int r300_init(struct radeon_device *rdev);
extern void r300_fini(struct radeon_device *rdev);
extern int r300_suspend(struct radeon_device *rdev);
extern int r300_resume(struct radeon_device *rdev);
extern int r300_gpu_reset(struct radeon_device *rdev);
extern void r300_ring_start(struct radeon_device *rdev);
extern void r300_fence_ring_emit(struct radeon_device *rdev,
				struct radeon_fence *fence);
extern int r300_cs_parse(struct radeon_cs_parser *p);
extern void rv370_pcie_gart_tlb_flush(struct radeon_device *rdev);
extern int rv370_pcie_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr);
extern uint32_t rv370_pcie_rreg(struct radeon_device *rdev, uint32_t reg);
extern void rv370_pcie_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
extern void rv370_set_pcie_lanes(struct radeon_device *rdev, int lanes);
extern int r300_copy_dma(struct radeon_device *rdev,
			uint64_t src_offset,
			uint64_t dst_offset,
			unsigned num_pages,
			struct radeon_fence *fence);
static struct radeon_asic r300_asic = {
	.init = &r300_init,
	.fini = &r300_fini,
	.suspend = &r300_suspend,
	.resume = &r300_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_reset = &r300_gpu_reset,
	.gart_tlb_flush = &r100_pci_gart_tlb_flush,
	.gart_set_page = &r100_pci_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r300_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_legacy_get_engine_clock,
	.set_engine_clock = &radeon_legacy_set_engine_clock,
	.get_memory_clock = &radeon_legacy_get_memory_clock,
	.set_memory_clock = NULL,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_legacy_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
	.hpd_init = &r100_hpd_init,
	.hpd_fini = &r100_hpd_fini,
	.hpd_sense = &r100_hpd_sense,
	.hpd_set_polarity = &r100_hpd_set_polarity,
};

/*
 * r420,r423,rv410
 */
extern int r420_init(struct radeon_device *rdev);
extern void r420_fini(struct radeon_device *rdev);
extern int r420_suspend(struct radeon_device *rdev);
extern int r420_resume(struct radeon_device *rdev);
static struct radeon_asic r420_asic = {
	.init = &r420_init,
	.fini = &r420_fini,
	.suspend = &r420_suspend,
	.resume = &r420_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_reset = &r300_gpu_reset,
	.gart_tlb_flush = &rv370_pcie_gart_tlb_flush,
	.gart_set_page = &rv370_pcie_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r300_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
	.hpd_init = &r100_hpd_init,
	.hpd_fini = &r100_hpd_fini,
	.hpd_sense = &r100_hpd_sense,
	.hpd_set_polarity = &r100_hpd_set_polarity,
};


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
static struct radeon_asic rs400_asic = {
	.init = &rs400_init,
	.fini = &rs400_fini,
	.suspend = &rs400_suspend,
	.resume = &rs400_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_reset = &r300_gpu_reset,
	.gart_tlb_flush = &rs400_gart_tlb_flush,
	.gart_set_page = &rs400_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r300_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_legacy_get_engine_clock,
	.set_engine_clock = &radeon_legacy_set_engine_clock,
	.get_memory_clock = &radeon_legacy_get_memory_clock,
	.set_memory_clock = NULL,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_legacy_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
	.hpd_init = &r100_hpd_init,
	.hpd_fini = &r100_hpd_fini,
	.hpd_sense = &r100_hpd_sense,
	.hpd_set_polarity = &r100_hpd_set_polarity,
};


/*
 * rs600.
 */
extern int rs600_init(struct radeon_device *rdev);
extern void rs600_fini(struct radeon_device *rdev);
extern int rs600_suspend(struct radeon_device *rdev);
extern int rs600_resume(struct radeon_device *rdev);
int rs600_irq_set(struct radeon_device *rdev);
int rs600_irq_process(struct radeon_device *rdev);
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

static struct radeon_asic rs600_asic = {
	.init = &rs600_init,
	.fini = &rs600_fini,
	.suspend = &rs600_suspend,
	.resume = &rs600_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_reset = &r300_gpu_reset,
	.gart_tlb_flush = &rs600_gart_tlb_flush,
	.gart_set_page = &rs600_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r300_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &rs600_irq_set,
	.irq_process = &rs600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.bandwidth_update = &rs600_bandwidth_update,
	.hpd_init = &rs600_hpd_init,
	.hpd_fini = &rs600_hpd_fini,
	.hpd_sense = &rs600_hpd_sense,
	.hpd_set_polarity = &rs600_hpd_set_polarity,
};


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
static struct radeon_asic rs690_asic = {
	.init = &rs690_init,
	.fini = &rs690_fini,
	.suspend = &rs690_suspend,
	.resume = &rs690_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_reset = &r300_gpu_reset,
	.gart_tlb_flush = &rs400_gart_tlb_flush,
	.gart_set_page = &rs400_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &r300_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &rs600_irq_set,
	.irq_process = &rs600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r300_copy_dma,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &rs690_bandwidth_update,
	.hpd_init = &rs600_hpd_init,
	.hpd_fini = &rs600_hpd_fini,
	.hpd_sense = &rs600_hpd_sense,
	.hpd_set_polarity = &rs600_hpd_set_polarity,
};


/*
 * rv515
 */
int rv515_init(struct radeon_device *rdev);
void rv515_fini(struct radeon_device *rdev);
int rv515_gpu_reset(struct radeon_device *rdev);
uint32_t rv515_mc_rreg(struct radeon_device *rdev, uint32_t reg);
void rv515_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void rv515_ring_start(struct radeon_device *rdev);
uint32_t rv515_pcie_rreg(struct radeon_device *rdev, uint32_t reg);
void rv515_pcie_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void rv515_bandwidth_update(struct radeon_device *rdev);
int rv515_resume(struct radeon_device *rdev);
int rv515_suspend(struct radeon_device *rdev);
static struct radeon_asic rv515_asic = {
	.init = &rv515_init,
	.fini = &rv515_fini,
	.suspend = &rv515_suspend,
	.resume = &rv515_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_reset = &rv515_gpu_reset,
	.gart_tlb_flush = &rv370_pcie_gart_tlb_flush,
	.gart_set_page = &rv370_pcie_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &rv515_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &rs600_irq_set,
	.irq_process = &rs600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &rv515_bandwidth_update,
	.hpd_init = &rs600_hpd_init,
	.hpd_fini = &rs600_hpd_fini,
	.hpd_sense = &rs600_hpd_sense,
	.hpd_set_polarity = &rs600_hpd_set_polarity,
};


/*
 * r520,rv530,rv560,rv570,r580
 */
int r520_init(struct radeon_device *rdev);
int r520_resume(struct radeon_device *rdev);
static struct radeon_asic r520_asic = {
	.init = &r520_init,
	.fini = &rv515_fini,
	.suspend = &rv515_suspend,
	.resume = &r520_resume,
	.vga_set_state = &r100_vga_set_state,
	.gpu_reset = &rv515_gpu_reset,
	.gart_tlb_flush = &rv370_pcie_gart_tlb_flush,
	.gart_set_page = &rv370_pcie_gart_set_page,
	.cp_commit = &r100_cp_commit,
	.ring_start = &rv515_ring_start,
	.ring_test = &r100_ring_test,
	.ring_ib_execute = &r100_ring_ib_execute,
	.irq_set = &rs600_irq_set,
	.irq_process = &rs600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r100_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &rv515_bandwidth_update,
	.hpd_init = &rs600_hpd_init,
	.hpd_fini = &rs600_hpd_fini,
	.hpd_sense = &rs600_hpd_sense,
	.hpd_set_polarity = &rs600_hpd_set_polarity,
};

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
int r600_copy_dma(struct radeon_device *rdev,
		  uint64_t src_offset,
		  uint64_t dst_offset,
		  unsigned num_pages,
		  struct radeon_fence *fence);
int r600_irq_process(struct radeon_device *rdev);
int r600_irq_set(struct radeon_device *rdev);
int r600_gpu_reset(struct radeon_device *rdev);
int r600_set_surface_reg(struct radeon_device *rdev, int reg,
			 uint32_t tiling_flags, uint32_t pitch,
			 uint32_t offset, uint32_t obj_size);
int r600_clear_surface_reg(struct radeon_device *rdev, int reg);
void r600_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
int r600_ring_test(struct radeon_device *rdev);
int r600_copy_blit(struct radeon_device *rdev,
		   uint64_t src_offset, uint64_t dst_offset,
		   unsigned num_pages, struct radeon_fence *fence);
void r600_hpd_init(struct radeon_device *rdev);
void r600_hpd_fini(struct radeon_device *rdev);
bool r600_hpd_sense(struct radeon_device *rdev, enum radeon_hpd_id hpd);
void r600_hpd_set_polarity(struct radeon_device *rdev,
			   enum radeon_hpd_id hpd);

static struct radeon_asic r600_asic = {
	.init = &r600_init,
	.fini = &r600_fini,
	.suspend = &r600_suspend,
	.resume = &r600_resume,
	.cp_commit = &r600_cp_commit,
	.vga_set_state = &r600_vga_set_state,
	.gpu_reset = &r600_gpu_reset,
	.gart_tlb_flush = &r600_pcie_gart_tlb_flush,
	.gart_set_page = &rs600_gart_set_page,
	.ring_test = &r600_ring_test,
	.ring_ib_execute = &r600_ring_ib_execute,
	.irq_set = &r600_irq_set,
	.irq_process = &r600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r600_fence_ring_emit,
	.cs_parse = &r600_cs_parse,
	.copy_blit = &r600_copy_blit,
	.copy_dma = &r600_copy_blit,
	.copy = &r600_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r600_set_surface_reg,
	.clear_surface_reg = r600_clear_surface_reg,
	.bandwidth_update = &rv515_bandwidth_update,
	.hpd_init = &r600_hpd_init,
	.hpd_fini = &r600_hpd_fini,
	.hpd_sense = &r600_hpd_sense,
	.hpd_set_polarity = &r600_hpd_set_polarity,
};

/*
 * rv770,rv730,rv710,rv740
 */
int rv770_init(struct radeon_device *rdev);
void rv770_fini(struct radeon_device *rdev);
int rv770_suspend(struct radeon_device *rdev);
int rv770_resume(struct radeon_device *rdev);
int rv770_gpu_reset(struct radeon_device *rdev);

static struct radeon_asic rv770_asic = {
	.init = &rv770_init,
	.fini = &rv770_fini,
	.suspend = &rv770_suspend,
	.resume = &rv770_resume,
	.cp_commit = &r600_cp_commit,
	.gpu_reset = &rv770_gpu_reset,
	.vga_set_state = &r600_vga_set_state,
	.gart_tlb_flush = &r600_pcie_gart_tlb_flush,
	.gart_set_page = &rs600_gart_set_page,
	.ring_test = &r600_ring_test,
	.ring_ib_execute = &r600_ring_ib_execute,
	.irq_set = &r600_irq_set,
	.irq_process = &r600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r600_fence_ring_emit,
	.cs_parse = &r600_cs_parse,
	.copy_blit = &r600_copy_blit,
	.copy_dma = &r600_copy_blit,
	.copy = &r600_copy_blit,
	.get_engine_clock = &radeon_atom_get_engine_clock,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.get_memory_clock = &radeon_atom_get_memory_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r600_set_surface_reg,
	.clear_surface_reg = r600_clear_surface_reg,
	.bandwidth_update = &rv515_bandwidth_update,
	.hpd_init = &r600_hpd_init,
	.hpd_fini = &r600_hpd_fini,
	.hpd_sense = &r600_hpd_sense,
	.hpd_set_polarity = &r600_hpd_set_polarity,
};

#endif
