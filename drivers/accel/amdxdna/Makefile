# SPDX-License-Identifier: GPL-2.0-only

amdxdna-y := \
	aie.o \
	aie_psp.o \
	aie_smu.o \
	aie2_ctx.o \
	aie2_error.o \
	aie2_message.o \
	aie2_pci.o \
	aie2_pm.o \
	aie2_solver.o \
	aie4_ctx.o \
	aie4_message.o \
	aie4_pci.o \
	amdxdna_cbuf.o \
	amdxdna_ctx.o \
	amdxdna_gem.o \
	amdxdna_iommu.o \
	amdxdna_mailbox.o \
	amdxdna_mailbox_helper.o \
	amdxdna_pci_drv.o \
	amdxdna_pm.o \
	amdxdna_sysfs.o \
	amdxdna_ubuf.o \
	npu1_regs.o \
	npu3_regs.o \
	npu4_regs.o \
	npu5_regs.o \
	npu6_regs.o

amdxdna-$(CONFIG_PCI_IOV) += aie4_sriov.o
amdxdna-$(CONFIG_DEBUG_FS) += amdxdna_debugfs.o
obj-$(CONFIG_DRM_ACCEL_AMDXDNA) = amdxdna.o
