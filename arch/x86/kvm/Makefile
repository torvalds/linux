#
# Makefile for Kernel-based Virtual Machine module
#

common-objs = $(addprefix ../../../drivers/kvm/, kvm_main.o)

EXTRA_CFLAGS += -I drivers/kvm

kvm-objs := $(common-objs) x86.o mmu.o x86_emulate.o i8259.o irq.o lapic.o \
	ioapic.o
obj-$(CONFIG_KVM) += kvm.o
kvm-intel-objs = vmx.o
obj-$(CONFIG_KVM_INTEL) += kvm-intel.o
kvm-amd-objs = svm.o
obj-$(CONFIG_KVM_AMD) += kvm-amd.o
