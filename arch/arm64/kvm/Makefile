#
# Makefile for Kernel-based Virtual Machine module
#

ccflags-y += -Ivirt/kvm -Iarch/arm64/kvm
CFLAGS_arm.o := -I.
CFLAGS_mmu.o := -I.

KVM=../../../virt/kvm
ARM=../../../arch/arm/kvm

obj-$(CONFIG_KVM_ARM_HOST) += kvm.o

kvm-$(CONFIG_KVM_ARM_HOST) += $(KVM)/kvm_main.o $(KVM)/coalesced_mmio.o
kvm-$(CONFIG_KVM_ARM_HOST) += $(ARM)/arm.o $(ARM)/mmu.o $(ARM)/mmio.o
kvm-$(CONFIG_KVM_ARM_HOST) += $(ARM)/psci.o $(ARM)/perf.o

kvm-$(CONFIG_KVM_ARM_HOST) += emulate.o inject_fault.o regmap.o
kvm-$(CONFIG_KVM_ARM_HOST) += hyp.o hyp-init.o handle_exit.o
kvm-$(CONFIG_KVM_ARM_HOST) += guest.o reset.o sys_regs.o sys_regs_generic_v8.o

kvm-$(CONFIG_KVM_ARM_VGIC) += $(KVM)/arm/vgic.o
kvm-$(CONFIG_KVM_ARM_VGIC) += $(KVM)/arm/vgic-v2.o
kvm-$(CONFIG_KVM_ARM_VGIC) += $(KVM)/arm/vgic-v2-emul.o
kvm-$(CONFIG_KVM_ARM_VGIC) += vgic-v2-switch.o
kvm-$(CONFIG_KVM_ARM_VGIC) += $(KVM)/arm/vgic-v3.o
kvm-$(CONFIG_KVM_ARM_VGIC) += $(KVM)/arm/vgic-v3-emul.o
kvm-$(CONFIG_KVM_ARM_VGIC) += vgic-v3-switch.o
kvm-$(CONFIG_KVM_ARM_TIMER) += $(KVM)/arm/arch_timer.o
