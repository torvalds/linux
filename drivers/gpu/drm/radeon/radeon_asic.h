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
void radeon_legacy_set_engine_clock(struct radeon_device *rdev, uint32_t eng_clock);
void radeon_legacy_set_clock_gating(struct radeon_device *rdev, int enable);

void radeon_atom_set_engine_clock(struct radeon_device *rdev, uint32_t eng_clock);
void radeon_atom_set_memory_clock(struct radeon_device *rdev, uint32_t mem_clock);
void radeon_atom_set_clock_gating(struct radeon_device *rdev, int enable);

/*
 * r100,rv100,rs100,rv200,rs200,r200,rv250,rs300,rv280
 */
int r100_init(struct radeon_device *rdev);
uint32_t r100_mm_rreg(struct radeon_device *rdev, uint32_t reg);
void r100_mm_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void r100_errata(struct radeon_device *rdev);
void r100_vram_info(struct radeon_device *rdev);
int r100_gpu_reset(struct radeon_device *rdev);
int r100_mc_init(struct radeon_device *rdev);
void r100_mc_fini(struct radeon_device *rdev);
u32 r100_get_vblank_counter(struct radeon_device *rdev, int crtc);
int r100_wb_init(struct radeon_device *rdev);
void r100_wb_fini(struct radeon_device *rdev);
int r100_gart_enable(struct radeon_device *rdev);
void r100_pci_gart_disable(struct radeon_device *rdev);
void r100_pci_gart_tlb_flush(struct radeon_device *rdev);
int r100_pci_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr);
int r100_cp_init(struct radeon_device *rdev, unsigned ring_size);
void r100_cp_fini(struct radeon_device *rdev);
void r100_cp_disable(struct radeon_device *rdev);
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

static struct radeon_asic r100_asic = {
	.init = &r100_init,
	.errata = &r100_errata,
	.vram_info = &r100_vram_info,
	.gpu_reset = &r100_gpu_reset,
	.mc_init = &r100_mc_init,
	.mc_fini = &r100_mc_fini,
	.wb_init = &r100_wb_init,
	.wb_fini = &r100_wb_fini,
	.gart_enable = &r100_gart_enable,
	.gart_disable = &r100_pci_gart_disable,
	.gart_tlb_flush = &r100_pci_gart_tlb_flush,
	.gart_set_page = &r100_pci_gart_set_page,
	.cp_init = &r100_cp_init,
	.cp_fini = &r100_cp_fini,
	.cp_disable = &r100_cp_disable,
	.ring_start = &r100_ring_start,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r100_fence_ring_emit,
	.cs_parse = &r100_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = NULL,
	.copy = &r100_copy_blit,
	.set_engine_clock = &radeon_legacy_set_engine_clock,
	.set_memory_clock = NULL,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_legacy_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
};


/*
 * r300,r350,rv350,rv380
 */
int r300_init(struct radeon_device *rdev);
void r300_errata(struct radeon_device *rdev);
void r300_vram_info(struct radeon_device *rdev);
int r300_gpu_reset(struct radeon_device *rdev);
int r300_mc_init(struct radeon_device *rdev);
void r300_mc_fini(struct radeon_device *rdev);
void r300_ring_start(struct radeon_device *rdev);
void r300_fence_ring_emit(struct radeon_device *rdev,
			  struct radeon_fence *fence);
int r300_cs_parse(struct radeon_cs_parser *p);
int r300_gart_enable(struct radeon_device *rdev);
void rv370_pcie_gart_disable(struct radeon_device *rdev);
void rv370_pcie_gart_tlb_flush(struct radeon_device *rdev);
int rv370_pcie_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr);
uint32_t rv370_pcie_rreg(struct radeon_device *rdev, uint32_t reg);
void rv370_pcie_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void rv370_set_pcie_lanes(struct radeon_device *rdev, int lanes);
int r300_copy_dma(struct radeon_device *rdev,
		  uint64_t src_offset,
		  uint64_t dst_offset,
		  unsigned num_pages,
		  struct radeon_fence *fence);

static struct radeon_asic r300_asic = {
	.init = &r300_init,
	.errata = &r300_errata,
	.vram_info = &r300_vram_info,
	.gpu_reset = &r300_gpu_reset,
	.mc_init = &r300_mc_init,
	.mc_fini = &r300_mc_fini,
	.wb_init = &r100_wb_init,
	.wb_fini = &r100_wb_fini,
	.gart_enable = &r300_gart_enable,
	.gart_disable = &r100_pci_gart_disable,
	.gart_tlb_flush = &r100_pci_gart_tlb_flush,
	.gart_set_page = &r100_pci_gart_set_page,
	.cp_init = &r100_cp_init,
	.cp_fini = &r100_cp_fini,
	.cp_disable = &r100_cp_disable,
	.ring_start = &r300_ring_start,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r100_copy_blit,
	.set_engine_clock = &radeon_legacy_set_engine_clock,
	.set_memory_clock = NULL,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_legacy_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
};

/*
 * r420,r423,rv410
 */
void r420_errata(struct radeon_device *rdev);
void r420_vram_info(struct radeon_device *rdev);
int r420_mc_init(struct radeon_device *rdev);
void r420_mc_fini(struct radeon_device *rdev);
static struct radeon_asic r420_asic = {
	.init = &r300_init,
	.errata = &r420_errata,
	.vram_info = &r420_vram_info,
	.gpu_reset = &r300_gpu_reset,
	.mc_init = &r420_mc_init,
	.mc_fini = &r420_mc_fini,
	.wb_init = &r100_wb_init,
	.wb_fini = &r100_wb_fini,
	.gart_enable = &r300_gart_enable,
	.gart_disable = &rv370_pcie_gart_disable,
	.gart_tlb_flush = &rv370_pcie_gart_tlb_flush,
	.gart_set_page = &rv370_pcie_gart_set_page,
	.cp_init = &r100_cp_init,
	.cp_fini = &r100_cp_fini,
	.cp_disable = &r100_cp_disable,
	.ring_start = &r300_ring_start,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r100_copy_blit,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
};


/*
 * rs400,rs480
 */
void rs400_errata(struct radeon_device *rdev);
void rs400_vram_info(struct radeon_device *rdev);
int rs400_mc_init(struct radeon_device *rdev);
void rs400_mc_fini(struct radeon_device *rdev);
int rs400_gart_enable(struct radeon_device *rdev);
void rs400_gart_disable(struct radeon_device *rdev);
void rs400_gart_tlb_flush(struct radeon_device *rdev);
int rs400_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr);
uint32_t rs400_mc_rreg(struct radeon_device *rdev, uint32_t reg);
void rs400_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
static struct radeon_asic rs400_asic = {
	.init = &r300_init,
	.errata = &rs400_errata,
	.vram_info = &rs400_vram_info,
	.gpu_reset = &r300_gpu_reset,
	.mc_init = &rs400_mc_init,
	.mc_fini = &rs400_mc_fini,
	.wb_init = &r100_wb_init,
	.wb_fini = &r100_wb_fini,
	.gart_enable = &rs400_gart_enable,
	.gart_disable = &rs400_gart_disable,
	.gart_tlb_flush = &rs400_gart_tlb_flush,
	.gart_set_page = &rs400_gart_set_page,
	.cp_init = &r100_cp_init,
	.cp_fini = &r100_cp_fini,
	.cp_disable = &r100_cp_disable,
	.ring_start = &r300_ring_start,
	.irq_set = &r100_irq_set,
	.irq_process = &r100_irq_process,
	.get_vblank_counter = &r100_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r100_copy_blit,
	.set_engine_clock = &radeon_legacy_set_engine_clock,
	.set_memory_clock = NULL,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_legacy_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r100_bandwidth_update,
};


/*
 * rs600.
 */
int rs600_init(struct radeon_device *dev);
void rs600_errata(struct radeon_device *rdev);
void rs600_vram_info(struct radeon_device *rdev);
int rs600_mc_init(struct radeon_device *rdev);
void rs600_mc_fini(struct radeon_device *rdev);
int rs600_irq_set(struct radeon_device *rdev);
int rs600_irq_process(struct radeon_device *rdev);
u32 rs600_get_vblank_counter(struct radeon_device *rdev, int crtc);
int rs600_gart_enable(struct radeon_device *rdev);
void rs600_gart_disable(struct radeon_device *rdev);
void rs600_gart_tlb_flush(struct radeon_device *rdev);
int rs600_gart_set_page(struct radeon_device *rdev, int i, uint64_t addr);
uint32_t rs600_mc_rreg(struct radeon_device *rdev, uint32_t reg);
void rs600_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void rs600_bandwidth_update(struct radeon_device *rdev);
static struct radeon_asic rs600_asic = {
	.init = &rs600_init,
	.errata = &rs600_errata,
	.vram_info = &rs600_vram_info,
	.gpu_reset = &r300_gpu_reset,
	.mc_init = &rs600_mc_init,
	.mc_fini = &rs600_mc_fini,
	.wb_init = &r100_wb_init,
	.wb_fini = &r100_wb_fini,
	.gart_enable = &rs600_gart_enable,
	.gart_disable = &rs600_gart_disable,
	.gart_tlb_flush = &rs600_gart_tlb_flush,
	.gart_set_page = &rs600_gart_set_page,
	.cp_init = &r100_cp_init,
	.cp_fini = &r100_cp_fini,
	.cp_disable = &r100_cp_disable,
	.ring_start = &r300_ring_start,
	.irq_set = &rs600_irq_set,
	.irq_process = &rs600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r100_copy_blit,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.bandwidth_update = &rs600_bandwidth_update,
};


/*
 * rs690,rs740
 */
void rs690_errata(struct radeon_device *rdev);
void rs690_vram_info(struct radeon_device *rdev);
int rs690_mc_init(struct radeon_device *rdev);
void rs690_mc_fini(struct radeon_device *rdev);
uint32_t rs690_mc_rreg(struct radeon_device *rdev, uint32_t reg);
void rs690_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void rs690_bandwidth_update(struct radeon_device *rdev);
static struct radeon_asic rs690_asic = {
	.init = &rs600_init,
	.errata = &rs690_errata,
	.vram_info = &rs690_vram_info,
	.gpu_reset = &r300_gpu_reset,
	.mc_init = &rs690_mc_init,
	.mc_fini = &rs690_mc_fini,
	.wb_init = &r100_wb_init,
	.wb_fini = &r100_wb_fini,
	.gart_enable = &rs400_gart_enable,
	.gart_disable = &rs400_gart_disable,
	.gart_tlb_flush = &rs400_gart_tlb_flush,
	.gart_set_page = &rs400_gart_set_page,
	.cp_init = &r100_cp_init,
	.cp_fini = &r100_cp_fini,
	.cp_disable = &r100_cp_disable,
	.ring_start = &r300_ring_start,
	.irq_set = &rs600_irq_set,
	.irq_process = &rs600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r300_copy_dma,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.set_pcie_lanes = NULL,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &rs690_bandwidth_update,
};


/*
 * rv515
 */
int rv515_init(struct radeon_device *rdev);
void rv515_errata(struct radeon_device *rdev);
void rv515_vram_info(struct radeon_device *rdev);
int rv515_gpu_reset(struct radeon_device *rdev);
int rv515_mc_init(struct radeon_device *rdev);
void rv515_mc_fini(struct radeon_device *rdev);
uint32_t rv515_mc_rreg(struct radeon_device *rdev, uint32_t reg);
void rv515_mc_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void rv515_ring_start(struct radeon_device *rdev);
uint32_t rv515_pcie_rreg(struct radeon_device *rdev, uint32_t reg);
void rv515_pcie_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
void rv515_bandwidth_update(struct radeon_device *rdev);
static struct radeon_asic rv515_asic = {
	.init = &rv515_init,
	.errata = &rv515_errata,
	.vram_info = &rv515_vram_info,
	.gpu_reset = &rv515_gpu_reset,
	.mc_init = &rv515_mc_init,
	.mc_fini = &rv515_mc_fini,
	.wb_init = &r100_wb_init,
	.wb_fini = &r100_wb_fini,
	.gart_enable = &r300_gart_enable,
	.gart_disable = &rv370_pcie_gart_disable,
	.gart_tlb_flush = &rv370_pcie_gart_tlb_flush,
	.gart_set_page = &rv370_pcie_gart_set_page,
	.cp_init = &r100_cp_init,
	.cp_fini = &r100_cp_fini,
	.cp_disable = &r100_cp_disable,
	.ring_start = &rv515_ring_start,
	.irq_set = &rs600_irq_set,
	.irq_process = &rs600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r100_copy_blit,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &rv515_bandwidth_update,
};


/*
 * r520,rv530,rv560,rv570,r580
 */
void r520_errata(struct radeon_device *rdev);
void r520_vram_info(struct radeon_device *rdev);
int r520_mc_init(struct radeon_device *rdev);
void r520_mc_fini(struct radeon_device *rdev);
void r520_bandwidth_update(struct radeon_device *rdev);
static struct radeon_asic r520_asic = {
	.init = &rv515_init,
	.errata = &r520_errata,
	.vram_info = &r520_vram_info,
	.gpu_reset = &rv515_gpu_reset,
	.mc_init = &r520_mc_init,
	.mc_fini = &r520_mc_fini,
	.wb_init = &r100_wb_init,
	.wb_fini = &r100_wb_fini,
	.gart_enable = &r300_gart_enable,
	.gart_disable = &rv370_pcie_gart_disable,
	.gart_tlb_flush = &rv370_pcie_gart_tlb_flush,
	.gart_set_page = &rv370_pcie_gart_set_page,
	.cp_init = &r100_cp_init,
	.cp_fini = &r100_cp_fini,
	.cp_disable = &r100_cp_disable,
	.ring_start = &rv515_ring_start,
	.irq_set = &rs600_irq_set,
	.irq_process = &rs600_irq_process,
	.get_vblank_counter = &rs600_get_vblank_counter,
	.fence_ring_emit = &r300_fence_ring_emit,
	.cs_parse = &r300_cs_parse,
	.copy_blit = &r100_copy_blit,
	.copy_dma = &r300_copy_dma,
	.copy = &r100_copy_blit,
	.set_engine_clock = &radeon_atom_set_engine_clock,
	.set_memory_clock = &radeon_atom_set_memory_clock,
	.set_pcie_lanes = &rv370_set_pcie_lanes,
	.set_clock_gating = &radeon_atom_set_clock_gating,
	.set_surface_reg = r100_set_surface_reg,
	.clear_surface_reg = r100_clear_surface_reg,
	.bandwidth_update = &r520_bandwidth_update,
};

/*
 * r600,rv610,rv630,rv620,rv635,rv670,rs780,rv770,rv730,rv710
 */
uint32_t r600_pciep_rreg(struct radeon_device *rdev, uint32_t reg);
void r600_pciep_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);

#endif
