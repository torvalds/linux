# SPDX-License-Identifier: GPL-2.0-only

include $(src)/common/mmu/Makefile
habanalabs-y += $(HL_COMMON_MMU_FILES)

include $(src)/common/pci/Makefile
habanalabs-y += $(HL_COMMON_PCI_FILES)

HL_COMMON_FILES := common/habanalabs_drv.o common/device.o common/context.o \
		common/asid.o common/habanalabs_ioctl.o \
		common/command_buffer.o common/hw_queue.o common/irq.o \
		common/sysfs.o common/hwmon.o common/memory.o \
		common/command_submission.o common/firmware_if.o \
		common/state_dump.o common/hwmgr.o
