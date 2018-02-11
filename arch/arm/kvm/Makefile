# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module
#

plus_virt := $(call as-instr,.arch_extension virt,+virt)
ifeq ($(plus_virt),+virt)
	plus_virt_def := -DREQUIRES_VIRT=1
endif

ccflags-y += -Iarch/arm/kvm -Ivirt/kvm/arm/vgic
CFLAGS_arm.o := -I. $(plus_virt_def)
CFLAGS_mmu.o := -I.

AFLAGS_init.o := -Wa,-march=armv7-a$(plus_virt)
AFLAGS_interrupts.o := -Wa,-march=armv7-a$(plus_virt)

KVM := ../../../virt/kvm
kvm-arm-y = $(KVM)/kvm_main.o $(KVM)/coalesced_mmio.o $(KVM)/eventfd.o $(KVM)/vfio.o

obj-$(CONFIG_KVM_ARM_HOST) += hyp/

obj-y += kvm-arm.o init.o interrupts.o
obj-y += handle_exit.o guest.o emulate.o reset.o
obj-y += coproc.o coproc_a15.o coproc_a7.o   vgic-v3-coproc.o
obj-y += $(KVM)/arm/arm.o $(KVM)/arm/mmu.o $(KVM)/arm/mmio.o
obj-y += $(KVM)/arm/psci.o $(KVM)/arm/perf.o
obj-y += $(KVM)/arm/aarch32.o

obj-y += $(KVM)/arm/vgic/vgic.o
obj-y += $(KVM)/arm/vgic/vgic-init.o
obj-y += $(KVM)/arm/vgic/vgic-irqfd.o
obj-y += $(KVM)/arm/vgic/vgic-v2.o
obj-y += $(KVM)/arm/vgic/vgic-v3.o
obj-y += $(KVM)/arm/vgic/vgic-v4.o
obj-y += $(KVM)/arm/vgic/vgic-mmio.o
obj-y += $(KVM)/arm/vgic/vgic-mmio-v2.o
obj-y += $(KVM)/arm/vgic/vgic-mmio-v3.o
obj-y += $(KVM)/arm/vgic/vgic-kvm-device.o
obj-y += $(KVM)/arm/vgic/vgic-its.o
obj-y += $(KVM)/arm/vgic/vgic-debug.o
obj-y += $(KVM)/irqchip.o
obj-y += $(KVM)/arm/arch_timer.o
