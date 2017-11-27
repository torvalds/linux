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

ccflags-y := -Idrivers/gpu/drm/amd/include/  \
		-Idrivers/gpu/drm/amd/include/asic_reg

amdkfd-y	:= kfd_module.o kfd_device.o kfd_chardev.o kfd_topology.o \
		kfd_pasid.o kfd_doorbell.o kfd_flat_memory.o \
		kfd_process.o kfd_queue.o kfd_mqd_manager.o \
		kfd_mqd_manager_cik.o kfd_mqd_manager_vi.o \
		kfd_kernel_queue.o kfd_kernel_queue_cik.o \
		kfd_kernel_queue_vi.o kfd_packet_manager.o \
		kfd_process_queue_manager.o kfd_device_queue_manager.o \
		kfd_device_queue_manager_cik.o kfd_device_queue_manager_vi.o \
		kfd_interrupt.o kfd_events.o cik_event_interrupt.o \
		kfd_dbgdev.o kfd_dbgmgr.o

amdkfd-$(CONFIG_DEBUG_FS) += kfd_debugfs.o

obj-$(CONFIG_HSA_AMD)	+= amdkfd.o
