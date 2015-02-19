#
# Makefile for Heterogenous System Architecture support for AMD GPU devices
#

ccflags-y := -Iinclude/drm -Idrivers/gpu/drm/amd/include/

amdkfd-y	:= kfd_module.o kfd_device.o kfd_chardev.o kfd_topology.o \
		kfd_pasid.o kfd_doorbell.o kfd_flat_memory.o \
		kfd_process.o kfd_queue.o kfd_mqd_manager.o \
		kfd_mqd_manager_cik.o kfd_mqd_manager_vi.o \
		kfd_kernel_queue.o kfd_kernel_queue_cik.o \
		kfd_kernel_queue_vi.o kfd_packet_manager.o \
		kfd_process_queue_manager.o kfd_device_queue_manager.o \
		kfd_device_queue_manager_cik.o kfd_device_queue_manager_vi.o \

obj-$(CONFIG_HSA_AMD)	+= amdkfd.o
