#
# Makefile for Kernel-based Virtual Machine module, HYP part
#

KVM=../../../../virt/kvm

obj-$(CONFIG_KVM_ARM_HOST) += $(KVM)/arm/hyp/vgic-v2-sr.o
obj-$(CONFIG_KVM_ARM_HOST) += $(KVM)/arm/hyp/timer-sr.o

obj-$(CONFIG_KVM_ARM_HOST) += tlb.o
obj-$(CONFIG_KVM_ARM_HOST) += cp15-sr.o
obj-$(CONFIG_KVM_ARM_HOST) += vfp.o
obj-$(CONFIG_KVM_ARM_HOST) += banked-sr.o
obj-$(CONFIG_KVM_ARM_HOST) += entry.o
obj-$(CONFIG_KVM_ARM_HOST) += hyp-entry.o
obj-$(CONFIG_KVM_ARM_HOST) += switch.o
obj-$(CONFIG_KVM_ARM_HOST) += s2-setup.o
