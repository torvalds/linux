# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module
#

ccflags-y += -I $(srctree)/$(src)

KVM=../../../virt/kvm

obj-$(CONFIG_KVM) += kvm.o
obj-$(CONFIG_KVM) += hyp/

kvm-$(CONFIG_KVM) += $(KVM)/kvm_main.o $(KVM)/coalesced_mmio.o
kvm-$(CONFIG_KVM) += $(KVM)/eventfd.o $(KVM)/vfio.o $(KVM)/irqchip.o
kvm-$(CONFIG_KVM) += arm.o mmu.o mmio.o
kvm-$(CONFIG_KVM) += psci.o perf.o
kvm-$(CONFIG_KVM) += hypercalls.o
kvm-$(CONFIG_KVM) += pvtime.o

kvm-$(CONFIG_KVM) += inject_fault.o regmap.o va_layout.o
kvm-$(CONFIG_KVM) += hyp.o hyp-init.o handle_exit.o
kvm-$(CONFIG_KVM) += guest.o debug.o reset.o sys_regs.o sys_regs_generic_v8.o
kvm-$(CONFIG_KVM) += vgic-sys-reg-v3.o fpsimd.o pmu.o
kvm-$(CONFIG_KVM) += aarch32.o
kvm-$(CONFIG_KVM) += arch_timer.o
kvm-$(CONFIG_KVM_ARM_PMU)  += pmu-emul.o

kvm-$(CONFIG_KVM) += vgic/vgic.o
kvm-$(CONFIG_KVM) += vgic/vgic-init.o
kvm-$(CONFIG_KVM) += vgic/vgic-irqfd.o
kvm-$(CONFIG_KVM) += vgic/vgic-v2.o
kvm-$(CONFIG_KVM) += vgic/vgic-v3.o
kvm-$(CONFIG_KVM) += vgic/vgic-v4.o
kvm-$(CONFIG_KVM) += vgic/vgic-mmio.o
kvm-$(CONFIG_KVM) += vgic/vgic-mmio-v2.o
kvm-$(CONFIG_KVM) += vgic/vgic-mmio-v3.o
kvm-$(CONFIG_KVM) += vgic/vgic-kvm-device.o
kvm-$(CONFIG_KVM) += vgic/vgic-its.o
kvm-$(CONFIG_KVM) += vgic/vgic-debug.o
