# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module
#

ccflags-y += -I $(srctree)/$(src)

KVM=../../../virt/kvm

obj-$(CONFIG_KVM_ARM_HOST) += kvm.o
obj-$(CONFIG_KVM_ARM_HOST) += hyp/

kvm-$(CONFIG_KVM_ARM_HOST) += $(KVM)/kvm_main.o $(KVM)/coalesced_mmio.o
kvm-$(CONFIG_KVM_ARM_HOST) += $(KVM)/eventfd.o $(KVM)/vfio.o $(KVM)/irqchip.o
kvm-$(CONFIG_KVM_ARM_HOST) += arm.o mmu.o mmio.o
kvm-$(CONFIG_KVM_ARM_HOST) += psci.o perf.o
kvm-$(CONFIG_KVM_ARM_HOST) += hypercalls.o
kvm-$(CONFIG_KVM_ARM_HOST) += pvtime.o

kvm-$(CONFIG_KVM_ARM_HOST) += inject_fault.o regmap.o va_layout.o
kvm-$(CONFIG_KVM_ARM_HOST) += hyp.o hyp-init.o handle_exit.o
kvm-$(CONFIG_KVM_ARM_HOST) += guest.o debug.o reset.o sys_regs.o sys_regs_generic_v8.o
kvm-$(CONFIG_KVM_ARM_HOST) += vgic-sys-reg-v3.o fpsimd.o pmu.o
kvm-$(CONFIG_KVM_ARM_HOST) += aarch32.o
kvm-$(CONFIG_KVM_ARM_HOST) += arch_timer.o
kvm-$(CONFIG_KVM_ARM_PMU)  += pmu-emul.o

kvm-$(CONFIG_KVM_ARM_HOST) += vgic/vgic.o
kvm-$(CONFIG_KVM_ARM_HOST) += vgic/vgic-init.o
kvm-$(CONFIG_KVM_ARM_HOST) += vgic/vgic-irqfd.o
kvm-$(CONFIG_KVM_ARM_HOST) += vgic/vgic-v2.o
kvm-$(CONFIG_KVM_ARM_HOST) += vgic/vgic-v3.o
kvm-$(CONFIG_KVM_ARM_HOST) += vgic/vgic-v4.o
kvm-$(CONFIG_KVM_ARM_HOST) += vgic/vgic-mmio.o
kvm-$(CONFIG_KVM_ARM_HOST) += vgic/vgic-mmio-v2.o
kvm-$(CONFIG_KVM_ARM_HOST) += vgic/vgic-mmio-v3.o
kvm-$(CONFIG_KVM_ARM_HOST) += vgic/vgic-kvm-device.o
kvm-$(CONFIG_KVM_ARM_HOST) += vgic/vgic-its.o
kvm-$(CONFIG_KVM_ARM_HOST) += vgic/vgic-debug.o
