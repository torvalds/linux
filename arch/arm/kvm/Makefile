#
# Makefile for Kernel-based Virtual Machine module
#

plus_virt := $(call as-instr,.arch_extension virt,+virt)
ifeq ($(plus_virt),+virt)
	plus_virt_def := -DREQUIRES_VIRT=1
endif

ccflags-y += -Ivirt/kvm -Iarch/arm/kvm
CFLAGS_arm.o := -I. $(plus_virt_def)
CFLAGS_mmu.o := -I.

AFLAGS_init.o := -Wa,-march=armv7-a$(plus_virt)
AFLAGS_interrupts.o := -Wa,-march=armv7-a$(plus_virt)

KVM := ../../../virt/kvm
kvm-arm-y = $(KVM)/kvm_main.o $(KVM)/coalesced_mmio.o

obj-y += kvm-arm.o init.o interrupts.o
obj-y += arm.o handle_exit.o guest.o mmu.o emulate.o reset.o
obj-y += coproc.o coproc_a15.o coproc_a7.o mmio.o psci.o perf.o
obj-$(CONFIG_KVM_ARM_VGIC) += $(KVM)/arm/vgic.o
obj-$(CONFIG_KVM_ARM_VGIC) += $(KVM)/arm/vgic-v2.o
obj-$(CONFIG_KVM_ARM_VGIC) += $(KVM)/arm/vgic-v2-emul.o
obj-$(CONFIG_KVM_ARM_TIMER) += $(KVM)/arm/arch_timer.o
