#
# Copyright 2017-2024 Advanced Micro Devices, Inc. All rights reserved.
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

FULL_AMD_PATH=$(src)/..
DISPLAY_FOLDER_NAME=display
FULL_AMD_DISPLAY_PATH = $(FULL_AMD_PATH)/$(DISPLAY_FOLDER_NAME)

ccflags-y := -I$(FULL_AMD_PATH)/include/asic_reg \
	-I$(FULL_AMD_PATH)/include \
	-I$(FULL_AMD_PATH)/amdgpu \
	-I$(FULL_AMD_PATH)/pm/inc \
	-I$(FULL_AMD_PATH)/acp/include \
	-I$(FULL_AMD_DISPLAY_PATH) \
	-I$(FULL_AMD_DISPLAY_PATH)/include \
	-I$(FULL_AMD_DISPLAY_PATH)/modules/inc \
	-I$(FULL_AMD_DISPLAY_PATH)/dc \
	-I$(FULL_AMD_DISPLAY_PATH)/amdgpu_dm \
	-I$(FULL_AMD_PATH)/amdkfd

# Locally disable W=1 warnings enabled in drm subsystem Makefile
subdir-ccflags-y += -Wno-override-init
subdir-ccflags-$(CONFIG_DRM_AMDGPU_WERROR) += -Werror

amdgpu-y := amdgpu_drv.o

# add KMS driver
amdgpu-y += amdgpu_device.o amdgpu_doorbell_mgr.o amdgpu_kms.o \
	amdgpu_atombios.o atombios_crtc.o amdgpu_connectors.o \
	atom.o amdgpu_fence.o amdgpu_ttm.o amdgpu_object.o amdgpu_gart.o \
	amdgpu_encoders.o amdgpu_display.o amdgpu_i2c.o \
	amdgpu_gem.o amdgpu_ring.o \
	amdgpu_cs.o amdgpu_bios.o amdgpu_benchmark.o \
	atombios_dp.o amdgpu_afmt.o amdgpu_trace_points.o \
	atombios_encoders.o amdgpu_sa.o atombios_i2c.o \
	amdgpu_dma_buf.o amdgpu_vm.o amdgpu_vm_pt.o amdgpu_vm_tlb_fence.o \
	amdgpu_ib.o amdgpu_pll.o \
	amdgpu_ucode.o amdgpu_bo_list.o amdgpu_ctx.o amdgpu_sync.o \
	amdgpu_gtt_mgr.o amdgpu_preempt_mgr.o amdgpu_vram_mgr.o amdgpu_virt.o \
	amdgpu_atomfirmware.o amdgpu_vf_error.o amdgpu_sched.o \
	amdgpu_debugfs.o amdgpu_ids.o amdgpu_gmc.o amdgpu_mmhub.o amdgpu_hdp.o \
	amdgpu_xgmi.o amdgpu_csa.o amdgpu_ras.o amdgpu_vm_cpu.o \
	amdgpu_vm_sdma.o amdgpu_discovery.o amdgpu_ras_eeprom.o amdgpu_nbio.o \
	amdgpu_umc.o smu_v11_0_i2c.o amdgpu_fru_eeprom.o amdgpu_rap.o \
	amdgpu_fw_attestation.o amdgpu_securedisplay.o \
	amdgpu_eeprom.o amdgpu_mca.o amdgpu_psp_ta.o amdgpu_lsdma.o \
	amdgpu_ring_mux.o amdgpu_xcp.o amdgpu_seq64.o amdgpu_aca.o amdgpu_dev_coredump.o \
	amdgpu_cper.o

amdgpu-$(CONFIG_PROC_FS) += amdgpu_fdinfo.o

amdgpu-$(CONFIG_PERF_EVENTS) += amdgpu_pmu.o

# add asic specific block
amdgpu-$(CONFIG_DRM_AMDGPU_CIK)+= cik.o cik_ih.o \
	dce_v8_0.o gfx_v7_0.o cik_sdma.o uvd_v4_2.o vce_v2_0.o

amdgpu-$(CONFIG_DRM_AMDGPU_SI)+= si.o gmc_v6_0.o gfx_v6_0.o si_ih.o si_dma.o dce_v6_0.o \
	uvd_v3_1.o

amdgpu-y += \
	vi.o mxgpu_vi.o nbio_v6_1.o soc15.o emu_soc.o mxgpu_ai.o nbio_v7_0.o vega10_reg_init.o \
	vega20_reg_init.o nbio_v7_4.o nbio_v2_3.o nv.o arct_reg_init.o mxgpu_nv.o \
	nbio_v7_2.o hdp_v4_0.o hdp_v5_0.o aldebaran_reg_init.o aldebaran.o soc21.o soc24.o \
	sienna_cichlid.o smu_v13_0_10.o nbio_v4_3.o hdp_v6_0.o nbio_v7_7.o hdp_v5_2.o lsdma_v6_0.o \
	nbio_v7_9.o aqua_vanjaram.o nbio_v7_11.o lsdma_v7_0.o hdp_v7_0.o nbif_v6_3_1.o

# add DF block
amdgpu-y += \
	df_v1_7.o \
	df_v3_6.o \
	df_v4_3.o \
	df_v4_6_2.o \
	df_v4_15.o

# add GMC block
amdgpu-y += \
	gmc_v7_0.o \
	gmc_v8_0.o \
	gfxhub_v1_0.o mmhub_v1_0.o gmc_v9_0.o gfxhub_v1_1.o mmhub_v9_4.o \
	gfxhub_v2_0.o mmhub_v2_0.o gmc_v10_0.o gfxhub_v2_1.o mmhub_v2_3.o \
	mmhub_v1_7.o gfxhub_v3_0.o mmhub_v3_0.o mmhub_v3_0_2.o gmc_v11_0.o \
	mmhub_v3_0_1.o gfxhub_v3_0_3.o gfxhub_v1_2.o mmhub_v1_8.o mmhub_v3_3.o \
	gfxhub_v11_5_0.o mmhub_v4_1_0.o gfxhub_v12_0.o gmc_v12_0.o

# add UMC block
amdgpu-y += \
	umc_v6_0.o umc_v6_1.o umc_v6_7.o umc_v8_7.o umc_v8_10.o umc_v12_0.o umc_v8_14.o

# add IH block
amdgpu-y += \
	amdgpu_irq.o \
	amdgpu_ih.o \
	iceland_ih.o \
	tonga_ih.o \
	cz_ih.o \
	vega10_ih.o \
	vega20_ih.o \
	navi10_ih.o \
	ih_v6_0.o \
	ih_v6_1.o \
	ih_v7_0.o

# add PSP block
amdgpu-y += \
	amdgpu_psp.o \
	psp_v3_1.o \
	psp_v10_0.o \
	psp_v11_0.o \
	psp_v11_0_8.o \
	psp_v12_0.o \
	psp_v13_0.o \
	psp_v13_0_4.o \
	psp_v14_0.o

# add DCE block
amdgpu-y += \
	dce_v10_0.o \
	dce_v11_0.o \
	amdgpu_vkms.o

# add GFX block
amdgpu-y += \
	amdgpu_gfx.o \
	amdgpu_rlc.o \
	gfx_v8_0.o \
	gfx_v9_0.o \
	gfx_v9_4.o \
	gfx_v9_4_2.o \
	gfx_v9_4_3.o \
	gfx_v10_0.o \
	imu_v11_0.o \
	gfx_v11_0.o \
	gfx_v11_0_3.o \
	imu_v11_0_3.o \
	gfx_v12_0.o \
	imu_v12_0.o

# add async DMA block
amdgpu-y += \
	amdgpu_sdma.o \
	sdma_v2_4.o \
	sdma_v3_0.o \
	sdma_v4_0.o \
	sdma_v4_4.o \
	sdma_v4_4_2.o \
	sdma_v5_0.o \
	sdma_v5_2.o \
	sdma_v6_0.o \
	sdma_v7_0.o

# add MES block
amdgpu-y += \
	amdgpu_mes.o \
	mes_v11_0.o \
	mes_v12_0.o

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

# add VCN and JPEG block
amdgpu-y += \
	amdgpu_vcn.o \
	vcn_sw_ring.o \
	vcn_v1_0.o \
	vcn_v2_0.o \
	vcn_v2_5.o \
	vcn_v3_0.o \
	vcn_v4_0.o \
	vcn_v4_0_3.o \
	vcn_v4_0_5.o \
	vcn_v5_0_0.o \
	vcn_v5_0_1.o \
	amdgpu_jpeg.o \
	jpeg_v1_0.o \
	jpeg_v2_0.o \
	jpeg_v2_5.o \
	jpeg_v3_0.o \
	jpeg_v4_0.o \
	jpeg_v4_0_3.o \
	jpeg_v4_0_5.o \
	jpeg_v5_0_0.o \
	jpeg_v5_0_1.o

# add VPE block
amdgpu-y += \
	amdgpu_vpe.o \
	vpe_v6_1.o

# add UMSCH block
amdgpu-y += \
	amdgpu_umsch_mm.o \
	umsch_mm_v4_0.o

#
# add ATHUB block
amdgpu-y += \
	athub_v1_0.o \
	athub_v2_0.o \
	athub_v2_1.o \
	athub_v3_0.o \
	athub_v4_1_0.o

# add SMUIO block
amdgpu-y += \
	smuio_v9_0.o \
	smuio_v11_0.o \
	smuio_v11_0_6.o \
	smuio_v13_0.o \
	smuio_v13_0_3.o \
	smuio_v13_0_6.o \
	smuio_v14_0_2.o

# add reset block
amdgpu-y += \
	amdgpu_reset.o

# add MCA block
amdgpu-y += \
	mca_v3_0.o

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
	amdgpu_amdkfd_aldebaran.o \
	amdgpu_amdkfd_gc_9_4_3.o \
	amdgpu_amdkfd_gfx_v10.o \
	amdgpu_amdkfd_gfx_v10_3.o \
	amdgpu_amdkfd_gfx_v11.o \
	amdgpu_amdkfd_gfx_v12.o

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
amdgpu-$(CONFIG_HMM_MIRROR) += amdgpu_hmm.o

include $(FULL_AMD_PATH)/pm/Makefile

amdgpu-y += $(AMD_POWERPLAY_FILES)

ifneq ($(CONFIG_DRM_AMD_DC),)

RELATIVE_AMD_DISPLAY_PATH = ../$(DISPLAY_FOLDER_NAME)
include $(FULL_AMD_DISPLAY_PATH)/Makefile

amdgpu-y += $(AMD_DISPLAY_FILES)

endif

# add isp block
ifneq ($(CONFIG_DRM_AMD_ISP),)
amdgpu-y += \
	amdgpu_isp.o \
	isp_v4_1_0.o \
	isp_v4_1_1.o
endif

obj-$(CONFIG_DRM_AMDGPU)+= amdgpu.o
