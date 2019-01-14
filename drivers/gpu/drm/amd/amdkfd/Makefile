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
# Makefile for Heterogenous System Architecture support for AMD GPU devices
#

AMDKFD_FILES	:= $(AMDKFD_PATH)/kfd_module.o \
		$(AMDKFD_PATH)/kfd_device.o \
		$(AMDKFD_PATH)/kfd_chardev.o \
		$(AMDKFD_PATH)/kfd_topology.o \
		$(AMDKFD_PATH)/kfd_pasid.o \
		$(AMDKFD_PATH)/kfd_doorbell.o \
		$(AMDKFD_PATH)/kfd_flat_memory.o \
		$(AMDKFD_PATH)/kfd_process.o \
		$(AMDKFD_PATH)/kfd_queue.o \
		$(AMDKFD_PATH)/kfd_mqd_manager.o \
		$(AMDKFD_PATH)/kfd_mqd_manager_cik.o \
		$(AMDKFD_PATH)/kfd_mqd_manager_vi.o \
		$(AMDKFD_PATH)/kfd_mqd_manager_v9.o \
		$(AMDKFD_PATH)/kfd_kernel_queue.o \
		$(AMDKFD_PATH)/kfd_kernel_queue_cik.o \
		$(AMDKFD_PATH)/kfd_kernel_queue_vi.o \
		$(AMDKFD_PATH)/kfd_kernel_queue_v9.o \
		$(AMDKFD_PATH)/kfd_packet_manager.o \
		$(AMDKFD_PATH)/kfd_process_queue_manager.o \
		$(AMDKFD_PATH)/kfd_device_queue_manager.o \
		$(AMDKFD_PATH)/kfd_device_queue_manager_cik.o \
		$(AMDKFD_PATH)/kfd_device_queue_manager_vi.o \
		$(AMDKFD_PATH)/kfd_device_queue_manager_v9.o \
		$(AMDKFD_PATH)/kfd_interrupt.o \
		$(AMDKFD_PATH)/kfd_events.o \
		$(AMDKFD_PATH)/cik_event_interrupt.o \
		$(AMDKFD_PATH)/kfd_int_process_v9.o \
		$(AMDKFD_PATH)/kfd_dbgdev.o \
		$(AMDKFD_PATH)/kfd_dbgmgr.o \
		$(AMDKFD_PATH)/kfd_crat.o

ifneq ($(CONFIG_AMD_IOMMU_V2),)
AMDKFD_FILES += $(AMDKFD_PATH)/kfd_iommu.o
endif

ifneq ($(CONFIG_DEBUG_FS),)
AMDKFD_FILES += $(AMDKFD_PATH)/kfd_debugfs.o
endif
