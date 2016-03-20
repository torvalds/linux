#
# Makefile for Kernel-based Virtual Machine module
#

plus_virt := $(call as-instr,.arch_extension virt,+virt)
ifeq ($(plus_virt),+virt)
	plus_virt_def := -DREQUIRES_VIRT=1
endif

ccflags-y += -Iarch/arm/kvm
CFLAGS_arm.o := -I. $(plus_virt_def)
CFLAGS_mmu.o := -I.

AFLAGS_init.o := -Wa,-march=armv7-a$(plus_virt)
AFLAGS_interrupts.o := -Wa,-march=armv7-a$(plus_virt)

KVM := ../../../virt/kvm
kvm-arm-y = $(KVM)/kvm_main.o $(KVM)/coalesced_mmio.o $(KVM)/eventfd.o $(KVM)/vfio.o

obj-$(CONFIG_KVM_ARM_HOST) += hyp/
obj-y += kvm-arm.o init.o interrupts.o
obj-y += arm.o handle_exit.o guest.o mmu.o emulate.o reset.o
obj-y += coproc.o coproc_a15.o coproc_a7.o mmio.o psci.o perf.o
obj-y += $(KVM)/arm/vgic.o
obj-y += $(KVM)/arm/vgic-v2.o
obj-y += $(KVM)/arm/vgic-v2-emul.o
obj-y += $(KVM)/arm/arch_timer.o
