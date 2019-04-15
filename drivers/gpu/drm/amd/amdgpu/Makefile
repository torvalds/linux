#
# Copyright 2017 Advanced Micro Devices, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

FULL_AMD_PATH=$(srctree)/$(src)/..
DISPLAY_FOLDER_NAME=display
FULL_AMD_DISPLAY_PATH = $(FULL_AMD_PATH)/$(DISPLAY_FOLDER_NAME)

ccflags-y := -I$(FULL_AMD_PATH)/include/asic_reg \
	-I$(FULL_AMD_PATH)/include \
	-I$(FULL_AMD_PATH)/amdgpu \
	-I$(FULL_AMD_PATH)/powerplay/inc \
	-I$(FULL_AMD_PATH)/acp/include \
	-I$(FULL_AMD_DISPLAY_PATH) \
	-I$(FULL_AMD_DISPLAY_PATH)/include \
	-I$(FULL_AMD_DISPLAY_PATH)/dc \
	-I$(FULL_AMD_DISPLAY_PATH)/amdgpu_dm \
	-I$(FULL_AMD_PATH)/amdkfd

amdgpu-y := amdgpu_drv.o

# add KMS driver
amdgpu-y += amdgpu_device.o amdgpu_kms.o \
	amdgpu_atombios.o atombios_crtc.o amdgpu_connectors.o \
	atom.o amdgpu_fence.o amdgpu_ttm.o amdgpu_object.o amdgpu_gart.o \
	amdgpu_encoders.o amdgpu_display.o amdgpu_i2c.o \
	amdgpu_fb.o amdgpu_gem.o amdgpu_ring.o \
	amdgpu_cs.o amdgpu_bios.o amdgpu_benchmark.o amdgpu_test.o \
	amdgpu_pm.o atombios_dp.o amdgpu_afmt.o amdgpu_trace_points.o \
	atombios_encoders.o amdgpu_sa.o atombios_i2c.o \
	amdgpu_dma_buf.o amdgpu_vm.o amdgpu_ib.o amdgpu_pll.o \
	amdgpu_ucode.o amdgpu_bo_list.o amdgpu_ctx.o amdgpu_sync.o \
	amdgpu_gtt_mgr.o amdgpu_vram_mgr.o amdgpu_virt.o amdgpu_atomfirmware.o \
	amdgpu_vf_error.o amdgpu_sched.o amdgpu_debugfs.o amdgpu_ids.o \
	amdgpu_gmc.o amdgpu_xgmi.o amdgpu_csa.o amdgpu_ras.o amdgpu_vm_cpu.o \
	amdgpu_vm_sdma.o amdgpu_discovery.o

amdgpu-$(CONFIG_PERF_EVENTS) += amdgpu_pmu.o

# add asic specific block
amdgpu-$(CONFIG_DRM_AMDGPU_CIK)+= cik.o cik_ih.o kv_smc.o kv_dpm.o \
	dce_v8_0.o gfx_v7_0.o cik_sdma.o uvd_v4_2.o vce_v2_0.o

amdgpu-$(CONFIG_DRM_AMDGPU_SI)+= si.o gmc_v6_0.o gfx_v6_0.o si_ih.o si_dma.o dce_v6_0.o si_dpm.o si_smc.o

amdgpu-y += \
	vi.o mxgpu_vi.o nbio_v6_1.o soc15.o emu_soc.o mxgpu_ai.o nbio_v7_0.o vega10_reg_init.o \
	vega20_reg_init.o nbio_v7_4.o nbio_v2_3.o nv.o navi10_reg_init.o navi14_reg_init.o \
	arct_reg_init.o

# add DF block
amdgpu-y += \
	df_v1_7.o \
	df_v3_6.o

# add GMC block
amdgpu-y += \
	gmc_v7_0.o \
	gmc_v8_0.o \
	gfxhub_v1_0.o mmhub_v1_0.o gmc_v9_0.o gfxhub_v1_1.o mmhub_v9_4.o \
	gfxhub_v2_0.o mmhub_v2_0.o gmc_v10_0.o

# add IH block
amdgpu-y += \
	amdgpu_irq.o \
	amdgpu_ih.o \
	iceland_ih.o \
	tonga_ih.o \
	cz_ih.o \
	vega10_ih.o \
	navi10_ih.o

# add PSP block
amdgpu-y += \
	amdgpu_psp.o \
	psp_v3_1.o \
	psp_v10_0.o \
	psp_v11_0.o

# add SMC block
amdgpu-y += \
	amdgpu_dpm.o

# add DCE block
amdgpu-y += \
	dce_v10_0.o \
	dce_v11_0.o \
	dce_virtual.o

# add GFX block
amdgpu-y += \
	amdgpu_gfx.o \
	amdgpu_rlc.o \
	gfx_v8_0.o \
	gfx_v9_0.o \
	gfx_v10_0.o

# add async DMA block
amdgpu-y += \
	amdgpu_sdma.o \
	sdma_v2_4.o \
	sdma_v3_0.o \
	sdma_v4_0.o \
	sdma_v5_0.o

# add MES block
amdgpu-y += \
	mes_v10_1.o

# add UVD block
amdgpu-y += \
	amdgpu_uvd.o \
	uvd_v5_0.o \
	uvd_v6_0.o \
	uvd_v7_0.o

# add VCE block
amdgpu-y += \
	amdgpu_vce.o \
	vce_v3_0.o \
	vce_v4_0.o

# add VCN block
amdgpu-y += \
	amdgpu_vcn.o \
	vcn_v1_0.o \
	vcn_v2_0.o \
	vcn_v2_5.o

# add ATHUB block
amdgpu-y += \
	athub_v2_0.o

# add amdkfd interfaces
amdgpu-y += amdgpu_amdkfd.o

ifneq ($(CONFIG_HSA_AMD),)
AMDKFD_PATH := ../amdkfd
include $(FULL_AMD_PATH)/amdkfd/Makefile
amdgpu-y += $(AMDKFD_FILES)
amdgpu-y += \
	 amdgpu_amdkfd_fence.o \
	 amdgpu_amdkfd_gpuvm.o \
	 amdgpu_amdkfd_gfx_v8.o \
	 amdgpu_amdkfd_gfx_v9.o \
	 amdgpu_amdkfd_arcturus.o \
	 amdgpu_amdkfd_gfx_v10.o

ifneq ($(CONFIG_DRM_AMDGPU_CIK),)
amdgpu-y += amdgpu_amdkfd_gfx_v7.o
endif

endif

# add cgs
amdgpu-y += amdgpu_cgs.o

# GPU scheduler
amdgpu-y += amdgpu_job.o

# ACP componet
ifneq ($(CONFIG_DRM_AMD_ACP),)
amdgpu-y += amdgpu_acp.o

AMDACPPATH := ../acp
include $(FULL_AMD_PATH)/acp/Makefile

amdgpu-y += $(AMD_ACP_FILES)
endif

amdgpu-$(CONFIG_COMPAT) += amdgpu_ioc32.o
amdgpu-$(CONFIG_VGA_SWITCHEROO) += amdgpu_atpx_handler.o
amdgpu-$(CONFIG_ACPI) += amdgpu_acpi.o
amdgpu-$(CONFIG_HMM_MIRROR) += amdgpu_mn.o

include $(FULL_AMD_PATH)/powerplay/Makefile

amdgpu-y += $(AMD_POWERPLAY_FILES)

ifneq ($(CONFIG_DRM_AMD_DC),)

RELATIVE_AMD_DISPLAY_PATH = ../$(DISPLAY_FOLDER_NAME)
include $(FULL_AMD_DISPLAY_PATH)/Makefile

amdgpu-y += $(AMD_DISPLAY_FILES)

endif

obj-$(CONFIG_DRM_AMDGPU)+= amdgpu.o
