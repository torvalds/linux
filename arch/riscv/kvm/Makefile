# SPDX-License-Identifier: GPL-2.0
#
# Makefile for RISC-V KVM support
#

ccflags-y += -I $(srctree)/$(src)

KVM := ../../../virt/kvm

obj-$(CONFIG_KVM) += kvm.o

kvm-y += $(KVM)/kvm_main.o
kvm-y += $(KVM)/coalesced_mmio.o
kvm-y += $(KVM)/binary_stats.o
kvm-y += $(KVM)/eventfd.o
kvm-y += main.o
kvm-y += vm.o
kvm-y += vmid.o
kvm-y += tlb.o
kvm-y += mmu.o
kvm-y += vcpu.o
kvm-y += vcpu_exit.o
kvm-y += vcpu_fp.o
kvm-y += vcpu_switch.o
kvm-y += vcpu_sbi.o
kvm-y += vcpu_timer.o
