#
# Makefile for Heterogenous System Architecture support for AMD GPU devices
#

ccflags-y := -Iinclude/drm -Idrivers/gpu/drm/amd/include/

amdkfd-y	:= kfd_module.o kfd_device.o kfd_chardev.o kfd_topology.o

obj-$(CONFIG_HSA_AMD)	+= amdkfd.o
