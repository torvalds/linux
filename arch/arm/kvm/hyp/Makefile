# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module, HYP part
#

ccflags-y += -fno-stack-protector -DDISABLE_BRANCH_PROFILING

KVM=../../../../virt/kvm

CFLAGS_ARMV7VE		   :=$(call cc-option, -march=armv7ve)

obj-$(CONFIG_KVM_ARM_HOST) += $(KVM)/arm/hyp/vgic-v3-sr.o
obj-$(CONFIG_KVM_ARM_HOST) += $(KVM)/arm/hyp/timer-sr.o

obj-$(CONFIG_KVM_ARM_HOST) += tlb.o
obj-$(CONFIG_KVM_ARM_HOST) += cp15-sr.o
obj-$(CONFIG_KVM_ARM_HOST) += vfp.o
obj-$(CONFIG_KVM_ARM_HOST) += banked-sr.o
CFLAGS_banked-sr.o	   += $(CFLAGS_ARMV7VE)

obj-$(CONFIG_KVM_ARM_HOST) += entry.o
obj-$(CONFIG_KVM_ARM_HOST) += hyp-entry.o
obj-$(CONFIG_KVM_ARM_HOST) += switch.o
CFLAGS_switch.o		   += $(CFLAGS_ARMV7VE)
obj-$(CONFIG_KVM_ARM_HOST) += s2-setup.o

# KVM code is run at a different exception code with a different map, so
# compiler instrumentation that inserts callbacks or checks into the code may
# cause crashes. Just disable it.
GCOV_PROFILE	:= n
KASAN_SANITIZE	:= n
UBSAN_SANITIZE	:= n
KCOV_INSTRUMENT	:= n
