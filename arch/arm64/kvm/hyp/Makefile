#
# Makefile for Kernel-based Virtual Machine module, HYP part
#

obj-$(CONFIG_KVM_ARM_HOST) += vgic-v2-sr.o
obj-$(CONFIG_KVM_ARM_HOST) += vgic-v3-sr.o
obj-$(CONFIG_KVM_ARM_HOST) += timer-sr.o
obj-$(CONFIG_KVM_ARM_HOST) += sysreg-sr.o
obj-$(CONFIG_KVM_ARM_HOST) += debug-sr.o
obj-$(CONFIG_KVM_ARM_HOST) += entry.o
obj-$(CONFIG_KVM_ARM_HOST) += switch.o
obj-$(CONFIG_KVM_ARM_HOST) += fpsimd.o
obj-$(CONFIG_KVM_ARM_HOST) += tlb.o
obj-$(CONFIG_KVM_ARM_HOST) += hyp-entry.o

GCOV_PROFILE	:= n
KASAN_SANITIZE	:= n
UBSAN_SANITIZE	:= n
