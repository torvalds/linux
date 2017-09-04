#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

FULL_AMD_PATH=$(src)/..

ccflags-y := -I$(FULL_AMD_PATH)/include/asic_reg \
	-I$(FULL_AMD_PATH)/include \
	-I$(FULL_AMD_PATH)/amdgpu \
	-I$(FULL_AMD_PATH)/scheduler \
	-I$(FULL_AMD_PATH)/powerplay/inc \
	-I$(FULL_AMD_PATH)/acp/include

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
	amdgpu_prime.o amdgpu_vm.o amdgpu_ib.o amdgpu_pll.o \
	amdgpu_ucode.o amdgpu_bo_list.o amdgpu_ctx.o amdgpu_sync.o \
	amdgpu_gtt_mgr.o amdgpu_vram_mgr.o amdgpu_virt.o amdgpu_atomfirmware.o \
	amdgpu_queue_mgr.o amdgpu_vf_error.o

# add asic specific block
amdgpu-$(CONFIG_DRM_AMDGPU_CIK)+= cik.o cik_ih.o kv_smc.o kv_dpm.o \
	ci_smc.o ci_dpm.o dce_v8_0.o gfx_v7_0.o cik_sdma.o uvd_v4_2.o vce_v2_0.o \
	amdgpu_amdkfd_gfx_v7.o

amdgpu-$(CONFIG_DRM_AMDGPU_SI)+= si.o gmc_v6_0.o gfx_v6_0.o si_ih.o si_dma.o dce_v6_0.o si_dpm.o si_smc.o

amdgpu-y += \
	vi.o mxgpu_vi.o nbio_v6_1.o soc15.o mxgpu_ai.o nbio_v7_0.o

# add GMC block
amdgpu-y += \
	gmc_v7_0.o \
	gmc_v8_0.o \
	gfxhub_v1_0.o mmhub_v1_0.o gmc_v9_0.o

# add IH block
amdgpu-y += \
	amdgpu_irq.o \
	amdgpu_ih.o \
	iceland_ih.o \
	tonga_ih.o \
	cz_ih.o \
	vega10_ih.o

# add PSP block
amdgpu-y += \
	amdgpu_psp.o \
	psp_v3_1.o \
	psp_v10_0.o

# add SMC block
amdgpu-y += \
	amdgpu_dpm.o \
	amdgpu_powerplay.o

# add DCE block
amdgpu-y += \
	dce_v10_0.o \
	dce_v11_0.o \
	dce_virtual.o

# add GFX block
amdgpu-y += \
	amdgpu_gfx.o \
	gfx_v8_0.o \
	gfx_v9_0.o

# add async DMA block
amdgpu-y += \
	sdma_v2_4.o \
	sdma_v3_0.o \
	sdma_v4_0.o

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
	vcn_v1_0.o

# add amdkfd interfaces
amdgpu-y += \
	 amdgpu_amdkfd.o \
	 amdgpu_amdkfd_gfx_v8.o

# add cgs
amdgpu-y += amdgpu_cgs.o

# GPU scheduler
amdgpu-y += \
	../scheduler/gpu_scheduler.o \
	../scheduler/sched_fence.o \
	amdgpu_job.o

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
amdgpu-$(CONFIG_MMU_NOTIFIER) += amdgpu_mn.o

include $(FULL_AMD_PATH)/powerplay/Makefile

amdgpu-y += $(AMD_POWERPLAY_FILES)

obj-$(CONFIG_DRM_AMDGPU)+= amdgpu.o

CFLAGS_amdgpu_trace_points.o := -I$(src)
