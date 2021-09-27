# SPDX-License-Identifier: GPL-2.0
#
# Makefile for RISC-V KVM support
#

ccflags-y += -I $(srctree)/$(src)

KVM := ../../../virt/kvm

obj-$(CONFIG_KVM) += kvm.o

kvm-y += $(KVM)/kvm_main.o $(KVM)/coalesced_mmio.o $(KVM)/binary_stats.o \
	 main.o vm.o mmu.o vcpu.o vcpu_exit.o vcpu_switch.o
